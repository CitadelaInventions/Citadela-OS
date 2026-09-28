#pragma once

#include <Arduino.h>
#include <ESP32Video.h>
#include <driver/dac.h>

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
    }

    void dot(int x, int y, Color color) override {
        Base::dot(x, y, polarityColor(color));
    }

    void fillRect(int x, int y, int w, int h, Color color) {
        if (this->usesPAL4xEncoder()) {
            Base::fillRect(x, y, w, h, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::fillRect(x, y, w, h, color);
    }

    void xLine(int x0, int x1, int y, Color color) override {
        if (this->usesPAL4xEncoder()) {
            Base::xLine(x0, x1, y, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::xLine(x0, x1, y, color);
    }

    void rect(int x, int y, int w, int h, Color color) {
        if (this->usesPAL4xEncoder()) {
            Base::rect(x, y, w, h, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::rect(x, y, w, h, color);
    }

    void clear(Color color = 0) override {
        if (this->usesPAL4xEncoder()) {
            Base::fillRect(0, 0, this->xres, this->yres, polarityColor(color));
            return;
        }
        GraphicsX8CA8Swapped::clear(color);
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

}  // namespace Citadela
