#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "CitadelaDisplay.h"

namespace Citadela {

class DaftEngine {
  public:
    typedef uint32_t PackedColor;
    static const uint8_t ShadeLevels = 16;
    static const uint8_t MaximumHueRamps = 3;

    struct HueRamp {
        PackedColor colors[ShadeLevels];
        uint8_t slot = 0;
        uint8_t minimumBrightness = 0;
        bool valid = false;
    };

    ~DaftEngine() {
        release();
    }

    bool begin(CitCompositeColorDAC &targetDisplay, int viewportWidth,
               int viewportHeight, int screenX = 0, int screenY = 0) {
        release();
        if (viewportWidth <= 0 || viewportHeight <= 0 ||
            (screenX & 1) != 0) return false;

        display = &targetDisplay;
        width = viewportWidth;
        height = viewportHeight;
        destinationX = screenX;
        destinationY = screenY;
        bufferBytes = (size_t)width * (size_t)height;
        pixels = (uint8_t *)heap_caps_malloc(
            bufferBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (!pixels) {
            release();
            return false;
        }

        buildColorCache();
        memset(pixels, 0, bufferBytes);
        return true;
    }

    void release() {
        if (pixels) heap_caps_free(pixels);
        pixels = nullptr;
        display = nullptr;
        width = 0;
        height = 0;
        destinationX = 0;
        destinationY = 0;
        bufferBytes = 0;
    }

    bool ready() const {
        return pixels && display;
    }

    size_t allocatedBytes() const {
        return bufferBytes;
    }

    PackedColor color(int red, int green, int blue) const {
        red = constrain(red, 0, 255);
        green = constrain(green, 0, 255);
        blue = constrain(blue, 0, 255);
        int maximum = max(red, max(green, blue));
        int minimum = min(red, min(green, blue));
        uint8_t indexed;
        int snappedRed;
        int snappedGreen;
        int snappedBlue;

        if (maximum - minimum <= 6) {
            int gray = (red + green + blue) / 3;
            int grayIndex = (gray * 31 + 127) / 255;
            int snapped = (grayIndex * 255 + 15) / 31;
            indexed = grayPalette[grayIndex];
            snappedRed = snappedGreen = snappedBlue = snapped;
        } else {
            int redIndex = (red * 6 + 127) / 255;
            int greenIndex = (green * 7 + 127) / 255;
            int blueIndex = (blue * 3 + 127) / 255;
            int cubeIndex = (redIndex * 8 + greenIndex) * 4 + blueIndex;
            indexed = colorCube[cubeIndex];
            snappedRed = (redIndex * 255 + 3) / 6;
            snappedGreen = (greenIndex * 255 + 3) / 7;
            snappedBlue = (blueIndex * 255 + 1) / 3;
        }
        return pack(indexed, snappedRed, snappedGreen, snappedBlue);
    }

    PackedColor litColor(int red, int green, int blue, float light) const {
        light = constrain(light, 0.0f, 1.15f);
        return color((int)(red * light + 0.5f),
                     (int)(green * light + 0.5f),
                     (int)(blue * light + 0.5f));
    }

    bool configureHueRamp(HueRamp &ramp, uint8_t slot,
                          uint8_t red, uint8_t green, uint8_t blue,
                          uint8_t minimumBrightness = 20) {
        if (!display || slot >= MaximumHueRamps) return false;
        ramp.slot = slot;
        ramp.minimumBrightness = minimumBrightness;
        for (int level = 0; level < ShadeLevels; ++level) {
            int brightness = minimumBrightness +
                ((255 - minimumBrightness) * level +
                 (ShadeLevels - 1) / 2) / (ShadeLevels - 1);
            int shadedRed = (red * brightness + 127) / 255;
            int shadedGreen = (green * brightness + 127) / 255;
            int shadedBlue = (blue * brightness + 127) / 255;
            uint8_t index = hueRampIndex(slot, level);
            if (!display->setIndexedPaletteOverride(
                    index, shadedRed, shadedGreen, shadedBlue)) {
                ramp.valid = false;
                return false;
            }
            ramp.colors[level] = pack(index, shadedRed,
                                      shadedGreen, shadedBlue);
        }
        display->applyIndexedPaletteOverrides();
        ramp.valid = true;
        return true;
    }

    PackedColor shade(const HueRamp &ramp, float brightness) const {
        if (!ramp.valid) return color(0, 0, 0);
        return ramp.colors[shadeLevel(ramp, brightness)];
    }

    uint8_t shadeLevel(const HueRamp &ramp, float brightness) const {
        if (!ramp.valid) return 0;
        int target = (int)(constrain(brightness, 0.0f, 1.0f) * 255.0f + 0.5f);
        if (target <= ramp.minimumBrightness) return 0;
        int span = 255 - ramp.minimumBrightness;
        return (uint8_t)constrain(
            ((target - ramp.minimumBrightness) * (ShadeLevels - 1) +
             span / 2) / span,
            0, ShadeLevels - 1);
    }

    void clear(PackedColor colorValue) {
        if (!pixels) return;
        memset(pixels, indexOf(colorValue), bufferBytes);
    }

    void fillRect(int x, int y, int rectWidth, int rectHeight,
                  PackedColor colorValue) {
        if (!pixels || rectWidth <= 0 || rectHeight <= 0) return;
        if (x < 0) { rectWidth += x; x = 0; }
        if (y < 0) { rectHeight += y; y = 0; }
        if (x + rectWidth > width) rectWidth = width - x;
        if (y + rectHeight > height) rectHeight = height - y;
        if (rectWidth <= 0 || rectHeight <= 0) return;

        uint8_t pixel = indexOf(colorValue);
        uint8_t *row = pixels + y * width + x;
        if (rectWidth == 1) {
            for (int remaining = rectHeight; remaining > 0; --remaining) {
                *row = pixel;
                row += width;
            }
            return;
        }
        if (rectWidth == 2 && (x & 1) == 0) {
            uint16_t pair = (uint16_t)pixel | ((uint16_t)pixel << 8);
            for (int remaining = rectHeight; remaining > 0; --remaining) {
                *(uint16_t *)row = pair;
                row += width;
            }
            return;
        }
        for (int remaining = rectHeight; remaining > 0; --remaining) {
            memset(row, pixel, rectWidth);
            row += width;
        }
    }

    void fillBlock2x2(int x, int y, PackedColor colorValue) {
        if (!pixels || x < 0 || y < 0 || x + 1 >= width ||
            y + 1 >= height || (x & 1) != 0) {
            fillRect(x, y, 2, 2, colorValue);
            return;
        }
        uint8_t pixel = indexOf(colorValue);
        uint16_t pair = (uint16_t)pixel | ((uint16_t)pixel << 8);
        uint8_t *first = pixels + y * width + x;
        *(uint16_t *)first = pair;
        *(uint16_t *)(first + width) = pair;
    }

    bool present() {
        return ready() && display->blitIndexedViewport(
            pixels, width, height, destinationX, destinationY);
    }

    static uint8_t indexOf(PackedColor colorValue) {
        return (uint8_t)(colorValue >> 24);
    }

  private:
    CitCompositeColorDAC *display = nullptr;
    uint8_t *pixels = nullptr;
    int width = 0;
    int height = 0;
    int destinationX = 0;
    int destinationY = 0;
    size_t bufferBytes = 0;
    uint8_t colorCube[7 * 8 * 4] = {};
    uint8_t grayPalette[32] = {};

    static PackedColor pack(uint8_t index, int red, int green, int blue) {
        return ((uint32_t)index << 24) |
               ((uint32_t)(blue & 0xff) << 16) |
               ((uint32_t)(green & 0xff) << 8) |
               (uint32_t)(red & 0xff);
    }

    static uint8_t hueRampIndex(uint8_t slot, uint8_t level) {
        // These are blue-max cube entries (index % 4 == 3), reserved while
        // DaftEngine is active and uncommon in low-resolution 3D materials.
        return (uint8_t)(35 + 4 * (slot * ShadeLevels + level));
    }

    void buildColorCache() {
        for (int grayIndex = 0; grayIndex < 32; ++grayIndex) {
            int value = (grayIndex * 255 + 15) / 31;
            grayPalette[grayIndex] =
                display->indexedPixel(display->RGB(value, value, value));
        }
        for (int redIndex = 0; redIndex < 7; ++redIndex) {
            int red = (redIndex * 255 + 3) / 6;
            for (int greenIndex = 0; greenIndex < 8; ++greenIndex) {
                int green = (greenIndex * 255 + 3) / 7;
                for (int blueIndex = 0; blueIndex < 4; ++blueIndex) {
                    int blue = (blueIndex * 255 + 1) / 3;
                    int cubeIndex =
                        (redIndex * 8 + greenIndex) * 4 + blueIndex;
                    colorCube[cubeIndex] = display->indexedPixel(
                        display->RGB(red, green, blue));
                }
            }
        }
    }
};

}
