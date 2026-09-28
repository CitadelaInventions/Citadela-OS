#pragma once

#include <Arduino.h>
#include <FS.h>

namespace Citadela {
namespace UI {

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

static inline bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static inline bool pointInRect(int px, int py, const Rect &r) {
    return pointInRect(px, py, r.x, r.y, r.w, r.h);
}

struct Button {
    Rect bounds;
    int id;
};

static inline int buttonAt(const Button *buttons, int count, int x, int y) {
    if (!buttons || count <= 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (pointInRect(x, y, buttons[i].bounds)) return buttons[i].id;
    }
    return -1;
}

template <typename Display>
static void drawWindow(Display &display,
                       int x,
                       int y,
                       int w,
                       int h,
                       uint32_t bg,
                       uint32_t border,
                       uint32_t titleColor,
                       const char *title = nullptr) {
    display.fillRect(x, y, w, h, bg);
    display.rect(x, y, w, h, border);
    if (title && title[0]) {
        display.setTextColor(titleColor, bg);
        display.setCursor(x + 8, y + 7);
        display.print(title);
    }
}

template <typename Display>
class DesktopSnapshot {
  public:
    typedef typename Display::RawPixel RawPixel;

    bool capture(Display &display,
                 fs::FS &fs,
                 const char *path,
                 int width,
                 int height,
                 RawPixel *lineBuffer,
                 int lineCapacity) {
        discard(fs, path);
        if (!path || !lineBuffer || width <= 0 || height <= 0 || width > lineCapacity) return false;

        File file = fs.open(path, FILE_WRITE);
        if (!file) return false;

        Header header;
        header.magic = kMagic;
        header.width = (uint16_t)width;
        header.height = (uint16_t)height;
        header.pixelSize = (uint16_t)sizeof(RawPixel);
        header.reserved = 0;
        bool ok = file.write((const uint8_t *)&header, sizeof(header)) == sizeof(header);
        const size_t rowBytes = (size_t)width * sizeof(RawPixel);
        for (int y = 0; ok && y < height; ++y) {
            for (int x = 0; x < width; ++x) lineBuffer[x] = display.getRawPixelFast(x, y);
            ok = file.write((const uint8_t *)lineBuffer, rowBytes) == rowBytes;
            if ((y & 31) == 0) yield();
        }
        file.close();
        if (!ok) {
            fs.remove(path);
            valid = false;
            return false;
        }

        snapshotWidth = width;
        snapshotHeight = height;
        valid = true;
        return true;
    }

    bool restoreRect(Display &display,
                     fs::FS &fs,
                     const char *path,
                     int x,
                     int y,
                     int width,
                     int height,
                     RawPixel *lineBuffer,
                     int lineCapacity) {
        if (!valid || !path || !lineBuffer || width <= 0 || height <= 0) return false;
        x = constrain(x, 0, snapshotWidth);
        y = constrain(y, 0, snapshotHeight);
        width = min(width, snapshotWidth - x);
        height = min(height, snapshotHeight - y);
        if (width <= 0 || height <= 0 || width > lineCapacity) return false;

        File file = fs.open(path, FILE_READ);
        if (!file) return false;
        Header header;
        bool ok = file.read((uint8_t *)&header, sizeof(header)) == sizeof(header) &&
                  header.magic == kMagic &&
                  header.width == snapshotWidth &&
                  header.height == snapshotHeight &&
                  header.pixelSize == sizeof(RawPixel);
        const size_t rowBytes = (size_t)width * sizeof(RawPixel);
        for (int row = 0; ok && row < height; ++row) {
            size_t offset = sizeof(Header) +
                            ((size_t)(y + row) * snapshotWidth + x) * sizeof(RawPixel);
            ok = file.seek(offset) && file.read((uint8_t *)lineBuffer, rowBytes) == (int)rowBytes;
            for (int col = 0; ok && col < width; ++col) {
                display.setRawPixelFast(x + col, y + row, lineBuffer[col]);
            }
            if ((row & 31) == 0) yield();
        }
        file.close();
        return ok;
    }

    void discard(fs::FS &fs, const char *path) {
        if (path && fs.exists(path)) fs.remove(path);
        valid = false;
        snapshotWidth = 0;
        snapshotHeight = 0;
    }

    bool isValid() const { return valid; }

  private:
    struct __attribute__((packed)) Header {
        uint32_t magic;
        uint16_t width;
        uint16_t height;
        uint16_t pixelSize;
        uint16_t reserved;
    };

    static const uint32_t kMagic = 0x31574443UL; // CDW1
    int snapshotWidth = 0;
    int snapshotHeight = 0;
    bool valid = false;
};

}
}
