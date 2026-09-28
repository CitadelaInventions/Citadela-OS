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
    return candidate.phaseAlternating &&
           !candidate.interlaced &&
           candidate.hRes == 376 &&
           candidate.activeLineCount == PAL4X_ACTIVE_LINES &&
           candidate.colorClock >= 4300000UL &&
           candidate.colorClock <= 4550000UL;
}

void CompositeColorDAC::configurePAL4xTiming()
{
    pal4xSampleRate = (long)(mode.colorClock * 4UL);
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
        indexedRGB((uint8_t)index, r, g, b);

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
    int sample = pal4xPictureStart;
    for (int x = 0; x < 376; x += 2)
    {
        uint8_t first = (uint8_t)(source[x ^ 1] >> 8);
        uint8_t second = (uint8_t)(source[(x + 1) ^ 1] >> 8);

        line[sample ^ 1] = pal4xPalette[lineParity][first][sample & 3];
        ++sample;
        line[sample ^ 1] = pal4xPalette[lineParity][first][sample & 3];
        ++sample;
        line[sample ^ 1] = pal4xPalette[lineParity][first][sample & 3];
        ++sample;
        line[sample ^ 1] = pal4xPalette[lineParity][second][sample & 3];
        ++sample;
        line[sample ^ 1] = pal4xPalette[lineParity][second][sample & 3];
        ++sample;
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

    int sourceY = lineNumber - PAL4X_ACTIVE_FIRST_LINE;
    if ((unsigned int)sourceY < PAL4X_ACTIVE_LINES)
    {
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
