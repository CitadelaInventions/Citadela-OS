param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [int]$HighlightKnee = 255,

    [int]$HighlightCap = 255,

    [int]$MaxChannel = 255
)

Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

public static class AquaWallpaperProcessor
{
    private struct PaletteColor
    {
        public int R;
        public int G;
        public int B;

        public PaletteColor(int r, int g, int b)
        {
            R = r;
            G = g;
            B = b;
        }
    }

    public static void Process(
        string inputPath,
        string outputPath,
        int highlightKnee,
        int highlightCap,
        int maxChannel)
    {
        using (Bitmap source = new Bitmap(inputPath))
        using (Bitmap scaled = new Bitmap(400, 225, PixelFormat.Format24bppRgb))
        using (Bitmap output = new Bitmap(400, 225, PixelFormat.Format24bppRgb))
        {
            using (Graphics graphics = Graphics.FromImage(scaled))
            {
                graphics.Clear(Color.Black);
                graphics.CompositingMode = CompositingMode.SourceCopy;
                graphics.CompositingQuality = CompositingQuality.HighQuality;
                graphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                graphics.SmoothingMode = SmoothingMode.HighQuality;

                const double zoom = 1.025;
                float sourceWidth = (float)(source.Width / zoom);
                float sourceHeight = (float)(source.Height / zoom);
                float sourceX = (source.Width - sourceWidth) * 0.5f;
                float sourceY = (source.Height - sourceHeight) * 0.5f;
                graphics.DrawImage(
                    source,
                    new RectangleF(0, 0, 400, 225),
                    new RectangleF(sourceX, sourceY, sourceWidth, sourceHeight),
                    GraphicsUnit.Pixel);
            }

            for (int y = 0; y < 225; y++)
            {
                for (int x = 0; x < 400; x++)
                {
                    Color sourcePixel = scaled.GetPixel(x, y);
                    double luma = 0.299 * sourcePixel.R + 0.587 * sourcePixel.G + 0.114 * sourcePixel.B;
                    luma = Clamp(128.0 + (luma - 128.0) * 1.02, 0.0, 255.0);
                    int spread = Math.Max(sourcePixel.R, Math.Max(sourcePixel.G, sourcePixel.B)) -
                        Math.Min(sourcePixel.R, Math.Min(sourcePixel.G, sourcePixel.B));

                    bool neutralHighlight = luma >= 230.0 && spread <= 24;
                    double targetR;
                    double targetG;
                    double targetB;

                    if (neutralHighlight)
                    {
                        double neutralBlend = Clamp((luma - 230.0) / 20.0, 0.0, 1.0);
                        targetR = sourcePixel.R + (luma - sourcePixel.R) * neutralBlend;
                        targetG = sourcePixel.G + (luma - sourcePixel.G) * neutralBlend;
                        targetB = sourcePixel.B + (luma - sourcePixel.B) * neutralBlend;
                    }
                    else
                    {
                        double colorStrength = Clamp((235.0 - luma) / 180.0, 0.0, 1.0);
                        double aquaR = luma * (1.0 - 0.60 * colorStrength);
                        double aquaG = luma * (1.0 + 0.28 * colorStrength);
                        double aquaB = luma * (1.0 + 0.16 * colorStrength);
                        const double aquaBlend = 0.68;
                        targetR = sourcePixel.R * (1.0 - aquaBlend) + aquaR * aquaBlend;
                        targetG = sourcePixel.G * (1.0 - aquaBlend) + aquaG * aquaBlend;
                        targetB = sourcePixel.B * (1.0 - aquaBlend) + aquaB * aquaBlend;
                    }

                    double outputLuma = 0.299 * targetR + 0.587 * targetG + 0.114 * targetB;
                    if (highlightCap < 255 && highlightKnee < 255 && outputLuma > highlightKnee)
                    {
                        double highlightRange = 255.0 - highlightKnee;
                        double highlightPosition = Clamp(
                            (outputLuma - highlightKnee) / highlightRange,
                            0.0,
                            1.0);
                        double compressedLuma = highlightKnee +
                            highlightPosition * (highlightCap - highlightKnee);
                        double scale = compressedLuma / outputLuma;
                        targetR *= scale;
                        targetG *= scale;
                        targetB *= scale;

                        // Pull the brightest pixels toward neutral gray so no
                        // residual chroma can turn clipped whites pink.
                        double neutralBlend = highlightPosition * 0.82;
                        targetR += (compressedLuma - targetR) * neutralBlend;
                        targetG += (compressedLuma - targetG) * neutralBlend;
                        targetB += (compressedLuma - targetB) * neutralBlend;
                    }

                    output.SetPixel(
                        x,
                        y,
                        Color.FromArgb(
                            (int)Math.Round(Clamp(targetR, 0.0, maxChannel)),
                            (int)Math.Round(Clamp(targetG, 0.0, maxChannel)),
                            (int)Math.Round(Clamp(targetB, 0.0, maxChannel))));
                }
            }

            output.Save(outputPath, ImageFormat.Bmp);
        }
    }

    private static List<PaletteColor> BuildAquaPalette()
    {
        List<PaletteColor> palette = new List<PaletteColor>();
        PaletteColor[] grays = BuildGrayPalette();
        palette.AddRange(grays);

        for (int ri = 0; ri < 7; ri++)
        {
            int r = (ri * 255 + 3) / 6;
            for (int gi = 0; gi < 8; gi++)
            {
                int g = (gi * 255 + 3) / 7;
                for (int bi = 0; bi < 4; bi++)
                {
                    int b = (bi * 255 + 1) / 3;
                    double max = Math.Max(r, Math.Max(g, b));
                    double min = Math.Min(r, Math.Min(g, b));
                    double saturation = max <= 0.0 ? 0.0 : (max - min) / max;
                    double hue = HueDegrees(r, g, b);

                    if (saturation <= 0.10 ||
                        (hue >= 145.0 && hue <= 185.0 && g >= r && b >= r && g + 8 >= b))
                        palette.Add(new PaletteColor(r, g, b));
                }
            }
        }

        return palette;
    }

    private static PaletteColor[] BuildGrayPalette()
    {
        PaletteColor[] palette = new PaletteColor[32];
        for (int i = 0; i < 32; i++)
        {
            int gray = (i * 255 + 15) / 31;
            palette[i] = new PaletteColor(gray, gray, gray);
        }
        return palette;
    }

    private static double HueDegrees(int r, int g, int b)
    {
        double rd = r / 255.0;
        double gd = g / 255.0;
        double bd = b / 255.0;
        double max = Math.Max(rd, Math.Max(gd, bd));
        double min = Math.Min(rd, Math.Min(gd, bd));
        double delta = max - min;
        if (delta <= 0.0001)
            return 0.0;

        double hue;
        if (max == rd)
            hue = 60.0 * (((gd - bd) / delta) % 6.0);
        else if (max == gd)
            hue = 60.0 * (((bd - rd) / delta) + 2.0);
        else
            hue = 60.0 * (((rd - gd) / delta) + 4.0);
        return hue < 0.0 ? hue + 360.0 : hue;
    }

    private static PaletteColor FindNearest(double r, double g, double b, IList<PaletteColor> palette)
    {
        PaletteColor best = palette[0];
        double bestDistance = double.MaxValue;
        for (int i = 0; i < palette.Count; i++)
        {
            PaletteColor candidate = palette[i];
            double dr = r - candidate.R;
            double dg = g - candidate.G;
            double db = b - candidate.B;
            double distance = dr * dr * 0.90 + dg * dg * 1.16 + db * db;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = candidate;
            }
        }
        return best;
    }

    private static void Diffuse(double[,] errors, int y, int x, bool reverse, double value)
    {
        int direction = reverse ? -1 : 1;
        errors[y, x + 1 + direction] += value * 7.0 / 16.0;
        errors[y + 1, x + 1 - direction] += value * 3.0 / 16.0;
        errors[y + 1, x + 1] += value * 5.0 / 16.0;
        errors[y + 1, x + 1 + direction] += value * 1.0 / 16.0;
    }

    private static double Clamp(double value, double minimum, double maximum)
    {
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }
}
'@

[AquaWallpaperProcessor]::Process(
    $InputPath,
    $OutputPath,
    $HighlightKnee,
    $HighlightCap,
    $MaxChannel)
