#pragma once

#include <Arduino.h>
#include <ESP32Video.h>
#include <driver/dac.h>
#include "CitadelaSerialDisplay.h"

namespace Citadela {

class CitCompositeColorDAC : public CompositeColorDAC {
  public:
    typedef CompositeColorDAC Base;
    typedef Base::Color Color;
    typedef Base::BufferGraphicsUnit RawPixel;

    CitCompositeColorDAC(bool *invertState = nullptr,
                         bool *polarityFilterState = nullptr,
                         bool *colourEnabledState = nullptr)
        : invertColorsState(invertState),
          polarityFilterState(polarityFilterState),
          colourEnabledState(colourEnabledState) {}

    bool init(const ModeComposite &mode, const int outputPin = 25, const bool voltageDivider = false) {
        if (videoMemoryAllocated) releaseVideoMemory();
        bool ok = Base::init(mode, outputPin, voltageDivider);
        if (!ok) {
            videoMemoryAllocated = true;
            releaseVideoMemory();
            return false;
        }
        videoMemoryAllocated = ok;
        return ok;
    }

    void releaseVideoMemory() {
        if (!videoMemoryAllocated) return;

        this->i2sStop();
        delay(2);
        dac_output_disable(this->outputPin == 25 ? DAC_CHANNEL_1 : DAC_CHANNEL_2);

        if (this->dmaBufferDescriptors) {
            free(this->dmaBufferDescriptors);
            this->dmaBufferDescriptors = nullptr;
            this->dmaBufferDescriptorCount = 0;
        }

        for (int frame = 0; frame < this->frameBufferCount; ++frame) {
            if (!this->frameBuffers[frame]) continue;
            for (int y = 0; y < this->yres; ++y) {
                free(this->frameBuffers[frame][y]);
            }
            free(this->frameBuffers[frame]);
            this->frameBuffers[frame] = nullptr;
        }

        void *blank0 = this->vBlankLineBuffer[0];
        void *blank1 = this->vBlankLineBuffer[1];
        if (blank0) free(blank0);
        if (blank1 && blank1 != blank0) free(blank1);
        if (this->equalizingLineBuffer) free(this->equalizingLineBuffer);
        if (this->vSyncLineBuffer) free(this->vSyncLineBuffer);

        this->vBlankLineBuffer[0] = nullptr;
        this->vBlankLineBuffer[1] = nullptr;
        this->equalizingLineBuffer = nullptr;
        this->vSyncLineBuffer = nullptr;
        this->normalFrontLineBuffer = nullptr;
        this->normalBackLineBuffer = nullptr;
        this->frontBuffer = nullptr;
        this->backBuffer = nullptr;
        this->currentFrameBuffer = 0;
        this->xres = 0;
        this->yres = 0;
        videoMemoryAllocated = false;
    }

    bool hasVideoMemory() const {
        return videoMemoryAllocated;
    }

    void bindSerialDisplay(SerialDisplay &recorder) { serialDisplay = &recorder; }

    void serialDisplayBeginScene() {
        if (!recording() || !backBuffer) return;
        serialDisplay->command("H %d %d", xres, yres);
        for (int i = 0; i < 256; i += 8) {
            char colors[65];
            for (int j = 0; j < 8; ++j)
                snprintf(colors + j * 8, 9, "%08lX", (unsigned long)buftocol(i + j));
            serialDisplay->command("P %d %s", i, colors);
        }
        serialDisplay->table(3, indexedRedLUT);
        serialDisplay->table(4, indexedGreenLUT);
        serialDisplay->table(5, indexedBlueLUT);
    }

    struct MirrorSilence {
        CitCompositeColorDAC &display;
        explicit MirrorSilence(CitCompositeColorDAC &target) : display(target) { ++display.recordDepth; }
        ~MirrorSilence() { --display.recordDepth; }
    };

    void serialCursor(int x, int y, bool visible, bool outlined) {
        if (recording()) serialDisplay->command("CURSOR %d %d %d %d %08lX %08lX", x, y, visible, outlined,
            mirrorColor(RGB(255, 255, 255)), mirrorColor(RGB(0, 0, 0)));
    }
    unsigned long serialColor(Color color) { return mirrorColor(color); }
    void beginSerialPixels() { ++recordDepth; }
    void endSerialPixels() { --recordDepth; }

    uint8_t indexedPixel(Color color) {
        return (uint8_t)(this->coltobuf(
            color & this->graphics_colormask(), 0, 0) & 0xff);
    }

    bool blitIndexedViewport(const uint8_t *source, int sourceWidth,
                             int sourceHeight, int destinationX,
                             int destinationY) {
        if (!source || !this->backBuffer || sourceWidth <= 0 ||
            sourceHeight <= 0 || (destinationX & 1) != 0 ||
            destinationX < 0 || destinationY < 0 ||
            destinationX + sourceWidth > this->xres ||
            destinationY + sourceHeight > this->yres) {
            return false;
        }

        for (int y = 0; y < sourceHeight; ++y) {
            uint16_t *target =
                (uint16_t *)this->backBuffer[destinationY + y] + destinationX;
            uint32_t *targetPairs = (uint32_t *)target;
            const uint8_t *sourceRow = source + y * sourceWidth;
            int x = 0;
            for (; x + 3 < sourceWidth; x += 4) {
                targetPairs[x >> 1] =
                    ((uint32_t)sourceRow[x] << 24) |
                    ((uint32_t)sourceRow[x + 1] << 8);
                targetPairs[(x >> 1) + 1] =
                    ((uint32_t)sourceRow[x + 2] << 24) |
                    ((uint32_t)sourceRow[x + 3] << 8);
            }
            if (x + 1 < sourceWidth) {
                targetPairs[x >> 1] =
                    ((uint32_t)sourceRow[x] << 24) |
                    ((uint32_t)sourceRow[x + 1] << 8);
            }
            if (recording())
                for (int px = 0; px < sourceWidth; ++px)
                    serialDisplay->pixel(destinationX + px, destinationY + y, sourceRow[px]);
        }
        return true;
    }

    void bindPolarity(bool &invertState, bool &filterState) {
        invertColorsState = &invertState;
        polarityFilterState = &filterState;
    }

    void bindColourMode(bool &enabledState) {
        colourEnabledState = &enabledState;
    }

    bool colourEnabled() const {
        return !colourEnabledState || *colourEnabledState;
    }

    Color colourModeColor(Color color) const {
        if (colourEnabled()) return color;
        const Color rgbMask = (Color)0x00ffffff;
        uint32_t r = color & 0xff;
        uint32_t g = (color >> 8) & 0xff;
        uint32_t b = (color >> 16) & 0xff;
        uint32_t gray = (19595UL * r + 38470UL * g + 7471UL * b + 0x8000UL) >> 16;
        Color monochrome = (Color)(gray | (gray << 8) | (gray << 16));
        return (color & ~rgbMask) | monochrome;
    }

    bool polarityFilterEnabled() const {
        return polarityFilterState ? *polarityFilterState : true;
    }

    void setPolarityFilterEnabled(bool enabled) {
        if (polarityFilterState) *polarityFilterState = enabled;
    }

    Color polarityColor(Color color) const {
        color = colourModeColor(color);
        if (!invertColorsState || !*invertColorsState || !polarityFilterEnabled()) return color;

        const Color rgbMask = (Color)0x00ffffff;
        Color rgb = color & rgbMask;
        if (rgb == 0) return (color & ~rgbMask) | rgbMask;
        if (rgb == rgbMask) return color & ~rgbMask;
        return color;
    }

    void dotFast(int x, int y, Color color) override {
        if ((unsigned int)x >= (unsigned int)this->xres ||
            (unsigned int)y >= (unsigned int)this->yres) return;
        Base::dotFast(x, y, polarityColor(color));
        recordPixel(x, y);
    }

    void dot(int x, int y, Color color) override {
        Base::dot(x, y, polarityColor(color));
        recordPixel(x, y);
    }

    void fillRect(int x, int y, int w, int h, Color color) {
        if (recording()) serialDisplay->command("FILL %d %d %d %d %08lX", x, y, w, h, mirrorColor(color));
        RecordScope scope(*this);
        if (this->usesPAL4xEncoder()) {
            Base::fillRect(x, y, w, h, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::fillRect(x, y, w, h, color);
    }

    void xLine(int x0, int x1, int y, Color color) override {
        if (recording()) serialDisplay->command("FILL %d %d %d 1 %08lX", min(x0, x1), y, abs(x1 - x0), mirrorColor(color));
        RecordScope scope(*this);
        if (this->usesPAL4xEncoder()) {
            Base::xLine(x0, x1, y, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::xLine(x0, x1, y, color);
    }

    void rect(int x, int y, int w, int h, Color color) {
        if (recording()) serialDisplay->command("RECT %d %d %d %d %08lX", x, y, w, h, mirrorColor(color));
        RecordScope scope(*this);
        if (this->usesPAL4xEncoder()) {
            Base::rect(x, y, w, h, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::rect(x, y, w, h, color);
    }

    void clear(Color color = 0) override {
        if (recording()) serialDisplay->command("FILL 0 0 %d %d %08lX", xres, yres, mirrorColor(color));
        RecordScope scope(*this);
        if (this->usesPAL4xEncoder()) {
            Base::fillRect(0, 0, this->xres, this->yres, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::clear(color);
    }

    void line(int x0, int y0, int x1, int y1, Color color) {
        if (recording()) serialDisplay->command("LINE %d %d %d %d %08lX", x0, y0, x1, y1, mirrorColor(color));
        RecordScope scope(*this);
        Base::line(x0, y0, x1, y1, color);
    }

    void circle(int x, int y, int radius, Color color) {
        if (recording()) serialDisplay->command("CIRCLE %d %d %d 0 %08lX", x, y, radius, mirrorColor(color));
        RecordScope scope(*this);
        Base::circle(x, y, radius, color);
    }

    void fillCircle(int x, int y, int radius, Color color) {
        if (recording()) serialDisplay->command("CIRCLE %d %d %d 1 %08lX", x, y, radius, mirrorColor(color));
        RecordScope scope(*this);
        Base::fillCircle(x, y, radius, color);
    }

    void drawChar(int x, int y, int ch) override {
        if (recording() && font && font->valid(ch) && font->charWidth * font->charHeight <= 128) {
            char mask[33] = {};
            const char *hex = "0123456789ABCDEF";
            int count = font->charWidth * font->charHeight;
            const uint8_t *source = font->pixels + count * (ch - font->firstChar);
            for (int i = 0; i < count; i += 4) {
                unsigned nibble = 0;
                for (int j = 0; j < 4; ++j)
                    nibble = (nibble << 1) | (i + j < count && source[i + j] ? 1 : 0);
                mask[i / 4] = hex[nibble];
            }
            serialDisplay->command("GLYPH %d %d %d %d %08lX %08lX %d %s", x, y,
                font->charWidth, font->charHeight,
                mirrorColor(frontColor, true), mirrorColor(backColor, true), ch, mask);
            RecordScope scope(*this);
            Base::drawChar(x, y, ch);
            return;
        }
        Base::drawChar(x, y, ch);
    }

    void scroll(int dy, Color color) override {
        if (recording()) serialDisplay->command("SCROLL %d %08lX", dy, mirrorColor(color));
        RecordScope scope(*this);
        Base::scroll(dy, color);
    }

    RawPixel getRawPixelFast(int x, int y) {
        if (!this->backBuffer ||
            (unsigned int)x >= (unsigned int)this->xres ||
            (unsigned int)y >= (unsigned int)this->yres) {
            return 0;
        }
        return this->backBuffer[this->graphics_swy(y)][this->graphics_swx(x)];
    }

    void setRawPixelFast(int x, int y, RawPixel raw) {
        if (!this->backBuffer ||
            (unsigned int)x >= (unsigned int)this->xres ||
            (unsigned int)y >= (unsigned int)this->yres) {
            return;
        }
        this->backBuffer[this->graphics_swy(y)][this->graphics_swx(x)] = raw;
        if (recording()) serialDisplay->pixel(x, y, rawPixelSignal(raw));
    }

    uint8_t rawPixelSignal(RawPixel raw) const {
        return (uint8_t)((raw >> 8) & 0xff);
    }

    uint8_t rawPixelBrightness(RawPixel raw) const {
        uint8_t stored = rawPixelSignal(raw);
        if (!this->usesPAL4xEncoder()) {
            int span = this->levelWhite - this->levelBlack;
            if (span == 0) return stored;
            int brightness = ((int)stored - this->levelBlack) * 255 / span;
            return (uint8_t)constrain(brightness, 0, 255);
        }

        Color color = this->buftocol(stored);
        uint32_t r = color & 0xff;
        uint32_t g = (color >> 8) & 0xff;
        uint32_t b = (color >> 16) & 0xff;
        return (uint8_t)((19595UL * r + 38470UL * g + 7471UL * b + 0x8000UL) >> 16);
    }

  private:
    SerialDisplay *serialDisplay = nullptr;
    uint8_t recordDepth = 0;
    struct RecordScope {
        CitCompositeColorDAC &display;
        explicit RecordScope(CitCompositeColorDAC &target) : display(target) { ++display.recordDepth; }
        ~RecordScope() { --display.recordDepth; }
    };
    bool recording() const { return serialDisplay && serialDisplay->enabled() && recordDepth == 0; }
    unsigned long mirrorColor(Color color, bool preserveAlpha = false) {
        uint32_t result = buftocol(indexedPixel(polarityColor(color)));
        return (result & 0xffffffUL) | (preserveAlpha ? ((uint32_t)color & 0xff000000UL) : 0xff000000UL);
    }
    void recordPixel(int x, int y) {
        if (recording() && (unsigned)x < (unsigned)xres && (unsigned)y < (unsigned)yres)
            serialDisplay->pixel(x, y, rawPixelSignal(getRawPixelFast(x, y)));
    }
    bool *invertColorsState;
    bool *polarityFilterState;
    bool *colourEnabledState;
    bool videoMemoryAllocated = false;
};

class BoolStateScope {
  public:
    BoolStateScope(bool &targetState, bool temporaryValue)
        : target(targetState), previous(targetState) {
        target = temporaryValue;
    }

    ~BoolStateScope() {
        target = previous;
    }

  private:
    bool &target;
    bool previous;
};

}
