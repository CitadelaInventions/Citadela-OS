/*
    PAL 4x-subcarrier scanline backend for CompositeColorDAC.

    The two-line DMA/ISR architecture follows Peter Barrett's ESP_8_BIT
    composite encoder, adapted here to bitluni's framebuffer and calibration
    API. ESP_8_BIT is distributed under the ISC license; see the vendored
    project in CitContents/vendor/ESP_8_BIT_composite for its full notice.
*/

#include "CompositeColorDAC.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <math.h>

namespace
{
    const int PAL4X_SHORT_SYNC = 32;
    const int PAL4X_LONG_SYNC = 536;
    const int PAL4X_CHROMA_REFERENCE = 112;

    int clampInteger(int value, int minimum, int maximum)
    {
        return value < minimum ? minimum : (value > maximum ? maximum : value);
    }

    int roundedDivide(int64_t numerator, int denominator)
    {
        if (numerator >= 0)
            return (int)((numerator + denominator / 2) / denominator);
        return (int)((numerator - denominator / 2) / denominator);
    }
}

bool CompositeColorDAC::supportsPAL4x(const ModeComposite &candidate) const
{
    bool nativeMode = candidate.phaseAlternating &&
                      !candidate.interlaced &&
                      candidate.hRes == 376 &&
                      candidate.activeLineCount == PAL4X_ACTIVE_LINES &&
                      candidate.colorClock >= 4300000UL &&
                      candidate.colorClock <= 4550000UL;

    // Existing apps use MODEPAL576Idiv3 because its 400x192 framebuffer leaves
    // enough memory for SD. Present its first 376 columns through the same
    // calibrated progressive PAL encoder, scaling 192 source rows to 288.
    bool lowMemoryAppMode = candidate.interlaced &&
                            candidate.hRes == 400 &&
                            candidate.activeLineCount == 192 &&
                            candidate.pixelClock >= 7000000UL &&
                            candidate.pixelClock <= 9000000UL;
    return nativeMode || lowMemoryAppMode;
}

void CompositeColorDAC::configurePAL4xTiming()
{
    uint32_t colorClock = mode.colorClock;
    if (colorClock < 4300000UL || colorClock > 4550000UL)
        colorClock = adjustedClock(4433619UL, calibration.colorClockPpm);
    pal4xSampleRate = (long)(colorClock * 4UL);
    pal4xHSyncSamples = clampInteger(84 + calibration.hSyncAdjust * 2, 56, 116);
    pal4xBurstStart = clampInteger(
        100 + calibration.burstStartAdjust * 2,
        pal4xHSyncSamples + 4,
        156);
    pal4xBurstSamples = clampInteger(
        44 + calibration.burstLengthAdjust * 2,
        16,
        80);
    if (pal4xBurstStart + pal4xBurstSamples > 180)
        pal4xBurstSamples = 180 - pal4xBurstStart;

    pal4xPictureStart = clampInteger(
        184 + calibration.activeStartSamples,
        180,
        PAL4X_LINE_SAMPLES - PAL4X_PICTURE_SAMPLES);
    // The packed blitter writes two DAC samples at once. Keep the active
    // picture on a 32-bit boundary; this changes calibration by at most one
    // 4x-subcarrier sample (about 56 ns).
    pal4xPictureStart &= ~1;
    rebuildPAL4xTables();
}

void CompositeColorDAC::rebuildPAL4xTables()
{
    const double phaseRadians =
        ((double)(pictureHueDegrees + activePhaseDegrees) * PI) / 180.0;
    const double phaseCosine = cos(phaseRadians);
    const double phaseSine = sin(phaseRadians);

    for (int index = 0; index < 256; ++index)
    {
        int r, g, b;
        uint32_t paletteOverride = indexedPaletteOverrides
            ? indexedPaletteOverrides[index]
            : 0;
        if (paletteOverride & 0x01000000UL)
        {
            r = paletteOverride & 0xff;
            g = (paletteOverride >> 8) & 0xff;
            b = (paletteOverride >> 16) & 0xff;
        }
        else
        {
            indexedRGB((uint8_t)index, r, g, b);
        }

        int sourceLuma =
            (19595L * r + 38470L * g + 7471L * b + 0x8000L) >> 16;
        int luma = 128 +
            ((sourceLuma - 128) * pictureContrast) / 100 +
            pictureBrightness;
        luma = clampInteger(luma, 0, 255);

        int blueDifference = b - sourceLuma;
        int redDifference = r - sourceLuma;
        int u = roundedDivide((int64_t)32251L * blueDifference, 65536);
        int v = roundedDivide((int64_t)57494L * redDifference, 65536);

        int rotatedU = (int)(u * phaseCosine - v * phaseSine +
            (u * phaseCosine - v * phaseSine >= 0.0 ? 0.5 : -0.5));
        int rotatedV = (int)(u * phaseSine + v * phaseCosine +
            (u * phaseSine + v * phaseCosine >= 0.0 ? 0.5 : -0.5));
        rotatedU = roundedDivide((int64_t)rotatedU * pictureSaturation, 100);
        rotatedV = roundedDivide((int64_t)rotatedV * pictureSaturation, 100);

        int lumaSpan = levelWhite - levelBlack;
        int outputLuma = levelBlack + roundedDivide((int64_t)luma * lumaSpan, 255);
        outputLuma = clampInteger(outputLuma, levelLowClipping, levelHighClipping);

        int components[2][4] = {
            {rotatedV, -rotatedU, -rotatedV, rotatedU},
            {-rotatedV, -rotatedU, rotatedV, rotatedU}
        };

        for (int parity = 0; parity < 2; ++parity)
        {
            int modulation[4];
            int maximumMagnitude = 0;
            for (int phase = 0; phase < 4; ++phase)
            {
                modulation[phase] = roundedDivide(
                    (int64_t)components[parity][phase] * amplitudeBurst,
                    PAL4X_CHROMA_REFERENCE);
                if (lumaSpan < 0)
                    modulation[phase] = -modulation[phase];
                int magnitude = modulation[phase] < 0
                    ? -modulation[phase]
                    : modulation[phase];
                if (magnitude > maximumMagnitude)
                    maximumMagnitude = magnitude;
            }

            int headroom = outputLuma - levelLowClipping;
            int upperHeadroom = levelHighClipping - outputLuma;
            if (upperHeadroom < headroom)
                headroom = upperHeadroom;
            if (headroom < 0)
                headroom = 0;

            for (int phase = 0; phase < 4; ++phase)
            {
                int value = outputLuma;
                if (maximumMagnitude > headroom && maximumMagnitude > 0)
                    value += roundedDivide(
                        (int64_t)modulation[phase] * headroom,
                        maximumMagnitude);
                else
                    value += modulation[phase];
                value = clampInteger(value, levelLowClipping, levelHighClipping);
                pal4xPalette[parity][index][phase] = (uint16_t)value << 8;
            }
        }
    }

    const double burstOffset =
        ((double)calibration.burstPhaseDegrees * PI) / 180.0;
    for (int parity = 0; parity < 2; ++parity)
    {
        const double palPhase = parity
            ? (PI + 3.0 * PI / 4.0)
            : (PI - 3.0 * PI / 4.0);
        for (int phase = 0; phase < 4; ++phase)
        {
            double burstSample =
                sin(palPhase + burstOffset + phase * PI / 2.0) * amplitudeBurst;
            int value = levelBlanking +
                (int)(burstSample + (burstSample >= 0.0 ? 0.5 : -0.5));
            value = clampInteger(value, levelLowClipping, levelHighClipping);
            pal4xBurst[parity][phase] = (uint16_t)value << 8;
        }
    }
}

void CompositeColorDAC::allocatePAL4xLineBuffers()
{
    dmaBufferDescriptorCount = 2;
    dmaBufferDescriptors = DMABufferDescriptor::allocateDescriptors(2);
    if (!dmaBufferDescriptors)
    {
        dmaBufferDescriptorCount = 0;
        return;
    }

    const int lineBytes = PAL4X_LINE_SAMPLES * bytesPerSample();
    vBlankLineBuffer[0] = DMABufferDescriptor::allocateBuffer(lineBytes, true);
    vBlankLineBuffer[1] = DMABufferDescriptor::allocateBuffer(lineBytes, true);
    if (!vBlankLineBuffer[0] || !vBlankLineBuffer[1])
        return;

    equalizingLineBuffer = 0;
    vSyncLineBuffer = 0;
    normalFrontLineBuffer = 0;
    normalBackLineBuffer = 0;
    for (int descriptor = 0; descriptor < 2; ++descriptor)
    {
        dmaBufferDescriptors[descriptor].setBuffer(
            vBlankLineBuffer[descriptor], lineBytes);
        dmaBufferDescriptors[descriptor].next(
            dmaBufferDescriptors[(descriptor + 1) & 1]);
        pal4xBufferState[descriptor] = 255;
        pal4xBufferHadPicture[descriptor] = false;
        renderPAL4xLine(
            (uint16_t *)vBlankLineBuffer[descriptor],
            descriptor,
            descriptor);
    }
    pal4xNextLine = 2;
}

void IRAM_ATTR CompositeColorDAC::interruptPAL4x(void *arg)
{
    CompositeColorDAC *display = (CompositeColorDAC *)arg;
    int descriptor = display->dmaBufferDescriptorActive;
    if ((unsigned int)descriptor >= 2U || !display->dmaBufferDescriptors)
        return;

    int lineNumber = display->pal4xNextLine;
    uint16_t *line = (uint16_t *)display->dmaBufferDescriptors[descriptor].buffer();
    display->renderPAL4xLine(line, lineNumber, descriptor);

    ++lineNumber;
    if (lineNumber >= PAL4X_LINE_COUNT)
    {
        lineNumber = 0;
        display->vSyncPassed = true;
    }
    display->currentLine = lineNumber;
    display->pal4xNextLine = lineNumber;
}

void IRAM_ATTR CompositeColorDAC::renderPAL4xNormalBase(
    uint16_t *line,
    int lineParity)
{
    uint16_t blank = (uint16_t)levelBlanking << 8;
    uint32_t blankPair = (uint32_t)blank | ((uint32_t)blank << 16);
    uint32_t *pairs = (uint32_t *)line;
    for (int i = 0; i < PAL4X_LINE_SAMPLES / 2; ++i)
        pairs[i] = blankPair;

    uint16_t sync = (uint16_t)levelSync << 8;
    for (int i = 0; i < pal4xHSyncSamples; ++i)
        line[i ^ 1] = sync;

    for (int i = 0; i < pal4xBurstSamples; ++i)
    {
        int sample = pal4xBurstStart + i;
        line[sample ^ 1] = pal4xBurst[lineParity][sample & 3];
    }
}

void IRAM_ATTR CompositeColorDAC::renderPAL4xSync(
    uint16_t *line,
    int lineNumber)
{
    uint16_t blank = (uint16_t)levelBlanking << 8;
    uint16_t sync = (uint16_t)levelSync << 8;
    uint32_t blankPair = (uint32_t)blank | ((uint32_t)blank << 16);
    uint32_t *pairs = (uint32_t *)line;
    for (int i = 0; i < PAL4X_LINE_SAMPLES / 2; ++i)
        pairs[i] = blankPair;

    int syncLine = lineNumber - PAL4X_SYNC_FIRST_LINE;
    int type = (syncLine == 3 || syncLine == 4) ? 3 :
               (syncLine == 5 ? 2 : 0);
    int firstWidth = (type & 2) ? PAL4X_LONG_SYNC : PAL4X_SHORT_SYNC;
    int secondWidth = (type & 1) ? PAL4X_LONG_SYNC : PAL4X_SHORT_SYNC;
    for (int i = 0; i < firstWidth; ++i)
        line[i ^ 1] = sync;
    for (int i = 0; i < secondWidth; ++i)
    {
        int sample = PAL4X_LINE_SAMPLES / 2 + i;
        line[sample ^ 1] = sync;
    }
}

void IRAM_ATTR CompositeColorDAC::clearPAL4xPicture(uint16_t *line)
{
    uint16_t blank = (uint16_t)levelBlanking << 8;
    for (int i = 0; i < PAL4X_PICTURE_SAMPLES; ++i)
    {
        int sample = pal4xPictureStart + i;
        line[sample ^ 1] = blank;
    }
}

void IRAM_ATTR CompositeColorDAC::blitPAL4xPicture(
    uint16_t *line,
    int sourceY,
    int lineParity)
{
    BufferUnit *source = frontBuffer[sourceY];
    uint32_t *destination = (uint32_t *)(line + pal4xPictureStart);
    int phase = pal4xPictureStart & 3;

    // Four framebuffer pixels become ten DAC samples (AAA BB CCC DD). The
    // framebuffer and I2S stream both swap adjacent 16-bit words, so loading
    // and storing aligned 32-bit pairs removes most of the ISR's work without
    // changing a single logical output sample.
    for (int x = 0; x < 376; x += 4)
    {
        uint32_t firstPair = *(const uint32_t *)(source + x);
        uint32_t secondPair = *(const uint32_t *)(source + x + 2);
        uint8_t a = (uint8_t)(firstPair >> 24);
        uint8_t b = (uint8_t)(firstPair >> 8);
        uint8_t c = (uint8_t)(secondPair >> 24);
        uint8_t d = (uint8_t)(secondPair >> 8);

        const uint16_t *pa = pal4xPalette[lineParity][a];
        const uint16_t *pb = pal4xPalette[lineParity][b];
        const uint16_t *pc = pal4xPalette[lineParity][c];
        const uint16_t *pd = pal4xPalette[lineParity][d];
        int p0 = phase;
        int p1 = (phase + 1) & 3;
        int p2 = (phase + 2) & 3;
        int p3 = (phase + 3) & 3;

        destination[0] = ((uint32_t)pa[p0] << 16) | pa[p1];
        destination[1] = ((uint32_t)pa[p2] << 16) | pb[p3];
        destination[2] = ((uint32_t)pb[p0] << 16) | pc[p1];
        destination[3] = ((uint32_t)pc[p2] << 16) | pc[p3];
        destination[4] = ((uint32_t)pd[p0] << 16) | pd[p1];
        destination += 5;
        phase ^= 2;
    }
}

void IRAM_ATTR CompositeColorDAC::renderPAL4xLine(
    uint16_t *line,
    int lineNumber,
    int descriptorIndex)
{
    if (lineNumber >= PAL4X_SYNC_FIRST_LINE)
    {
        renderPAL4xSync(line, lineNumber);
        pal4xBufferState[descriptorIndex] = 1;
        pal4xBufferHadPicture[descriptorIndex] = false;
        return;
    }

    int parity = lineNumber & 1;
    if (pal4xBufferState[descriptorIndex] != 0)
    {
        renderPAL4xNormalBase(line, parity);
        pal4xBufferState[descriptorIndex] = 0;
        pal4xBufferHadPicture[descriptorIndex] = false;
    }

    int activeY = lineNumber - PAL4X_ACTIVE_FIRST_LINE;
    if ((unsigned int)activeY < PAL4X_ACTIVE_LINES)
    {
        int sourceY = pal4xLowMemoryMode ? (activeY * 2) / 3 : activeY;
        blitPAL4xPicture(line, sourceY, parity);
        pal4xBufferHadPicture[descriptorIndex] = true;
    }
    else if (pal4xBufferHadPicture[descriptorIndex])
    {
        clearPAL4xPicture(line);
        pal4xBufferHadPicture[descriptorIndex] = false;
    }
}

#endif
