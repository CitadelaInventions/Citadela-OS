#pragma once

#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>

namespace Citadela {

struct WallpaperOptions {
    int brightnessPercent = 100;
    bool invert = false;
    bool monochrome = false;
    bool redAvailable = true;
    bool skipWhite = false;
    bool skipBlack = false;
    int whiteAlphaThreshold = 255;
    int blackAlphaThreshold = 160;
    int whiteThreshold = 255;
    int blackThreshold = 85;
    int whiteBlendQ256 = 256;
    int blackBlendQ256 = 256;
    int yieldEveryNRows = 128;
};

class WallpaperMath {
  public:
    static inline uint32_t readLE32(const uint8_t *buffer, int offset) {
        return (uint32_t)buffer[offset] |
               ((uint32_t)buffer[offset + 1] << 8) |
               ((uint32_t)buffer[offset + 2] << 16) |
               ((uint32_t)buffer[offset + 3] << 24);
    }

    static inline int luminance(int r, int g, int b) {
        return ((299 * r) + (587 * g) + (114 * b)) / 1000;
    }

    static inline int clampInt(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    static inline int blendQ256(int from, int to, int weight) {
        return ((from * (256 - weight)) + (to * weight) + 128) >> 8;
    }
};

class WallpaperPreviewCache {
  public:
    static bool create24BitBMP(fs::FS &srcFs,
                               const char *srcPath,
                               fs::FS &dstFs,
                               const char *dstPath,
                               int dstW,
                               int dstH,
                               Stream *log = nullptr) {
        if (!srcPath || !dstPath || dstW <= 0 || dstH <= 0) return false;
        File src = srcFs.open(srcPath, FILE_READ);
        if (!src) return false;

        uint8_t header[54];
        if (src.read(header, sizeof(header)) != sizeof(header) || header[0] != 'B' || header[1] != 'M') {
            src.close();
            return false;
        }
        int32_t srcW = (int32_t)WallpaperMath::readLE32(header, 18);
        int32_t rawH = (int32_t)WallpaperMath::readLE32(header, 22);
        uint16_t bpp = (uint16_t)header[28] | ((uint16_t)header[29] << 8);
        uint32_t compression = WallpaperMath::readLE32(header, 30);
        uint32_t pixelOffset = WallpaperMath::readLE32(header, 10);
        if (srcW <= 0 || rawH == 0 || bpp != 24 || compression != 0) {
            src.close();
            return false;
        }

        bool topDown = rawH < 0;
        int srcH = topDown ? -rawH : rawH;
        int srcStride = ((srcW * 3) + 3) & ~3;
        int dstStride = ((dstW * 3) + 3) & ~3;
        uint32_t imageBytes = (uint32_t)dstStride * (uint32_t)dstH;
        uint32_t fileBytes = 54U + imageBytes;

        uint8_t *srcRow = (uint8_t *)malloc(srcStride);
        uint8_t *dstRow = (uint8_t *)malloc(dstStride);
        if (!srcRow || !dstRow) {
            if (srcRow) free(srcRow);
            if (dstRow) free(dstRow);
            src.close();
            return false;
        }

        bool reusedBackingFile = false;
        File dst;
        if (dstFs.exists(dstPath)) {
            File existing = dstFs.open(dstPath, FILE_READ);
            bool exactSize = existing && existing.size() == fileBytes;
            if (existing) existing.close();
            if (exactSize) {
                dst = dstFs.open(dstPath, "r+");
                reusedBackingFile = (bool)dst;
            }
        }
        if (!dst) {
            if (dstFs.exists(dstPath)) dstFs.remove(dstPath);
            dst = dstFs.open(dstPath, FILE_WRITE);
        }
        if (!dst) {
            free(srcRow);
            free(dstRow);
            src.close();
            return false;
        }

        memset(header, 0, sizeof(header));
        header[0] = 'B';
        header[1] = 'M';
        writeLE32(header, 2, fileBytes);
        writeLE32(header, 10, 54);
        writeLE32(header, 14, 40);
        writeLE32(header, 18, (uint32_t)dstW);
        writeLE32(header, 22, (uint32_t)dstH);
        writeLE16(header, 26, 1);
        writeLE16(header, 28, 24);
        writeLE32(header, 34, imageBytes);

        bool ok = dst.seek(0) && writeAll(dst, header, sizeof(header));
        int loadedSourceFileRow = -1;
        int nextSourceFileRow = -1;
        for (int fileRow = 0; ok && fileRow < dstH; ++fileRow) {
            int logicalY = dstH - 1 - fileRow;
            int srcY = (int)((int64_t)logicalY * srcH / dstH);
            int sourceFileRow = topDown ? srcY : (srcH - 1 - srcY);

            if (!topDown) {
                if (loadedSourceFileRow < 0 || sourceFileRow < loadedSourceFileRow) {
                    ok = src.seek(pixelOffset + (uint32_t)sourceFileRow * (uint32_t)srcStride);
                    nextSourceFileRow = sourceFileRow;
                    loadedSourceFileRow = sourceFileRow - 1;
                }
                while (ok && loadedSourceFileRow < sourceFileRow) {
                    ok = src.read(srcRow, srcStride) == srcStride;
                    if (ok) loadedSourceFileRow = nextSourceFileRow++;
                }
            } else {
                ok = src.seek(pixelOffset + (uint32_t)sourceFileRow * (uint32_t)srcStride) &&
                     src.read(srcRow, srcStride) == srcStride;
                loadedSourceFileRow = sourceFileRow;
            }
            if (!ok) break;

            memset(dstRow, 0, dstStride);
            for (int x = 0; x < dstW; ++x) {
                int srcX = (int)((int64_t)x * srcW / dstW);
                dstRow[x * 3 + 0] = srcRow[srcX * 3 + 0];
                dstRow[x * 3 + 1] = srcRow[srcX * 3 + 1];
                dstRow[x * 3 + 2] = srcRow[srcX * 3 + 2];
            }
            ok = writeAll(dst, dstRow, dstStride);
            if ((fileRow & 15) == 0) yield();
        }

        dst.flush();
        size_t writtenSize = dst.size();
        dst.close();
        src.close();
        free(srcRow);
        free(dstRow);
        ok = ok && writtenSize == fileBytes;
        if (log) {
            log->printf("Wallpaper preview cache %s: %u/%u bytes.\n",
                        ok ? "ready" : "failed",
                        (unsigned)writtenSize,
                        (unsigned)fileBytes);
        }
        if (!ok && !reusedBackingFile) dstFs.remove(dstPath);
        return ok;
    }

  private:
    static void writeLE16(uint8_t *buffer, int offset, uint16_t value) {
        buffer[offset] = (uint8_t)value;
        buffer[offset + 1] = (uint8_t)(value >> 8);
    }

    static void writeLE32(uint8_t *buffer, int offset, uint32_t value) {
        buffer[offset] = (uint8_t)value;
        buffer[offset + 1] = (uint8_t)(value >> 8);
        buffer[offset + 2] = (uint8_t)(value >> 16);
        buffer[offset + 3] = (uint8_t)(value >> 24);
    }

    static bool writeAll(File &file, const uint8_t *data, size_t bytes) {
        size_t offset = 0;
        int zeroWrites = 0;
        while (offset < bytes) {
            size_t written = file.write(data + offset, bytes - offset);
            if (written > 0) {
                offset += written;
                zeroWrites = 0;
            } else if (++zeroWrites >= 3) {
                return false;
            } else {
                file.flush();
                delay(1);
            }
        }
        return true;
    }
};

template <typename Display>
class WallpaperRenderer {
  public:
    explicit WallpaperRenderer(Display &targetDisplay) : display(targetDisplay) {}

    bool renderFull(fs::FS &fs, const char *path, int dstW, int dstH, const WallpaperOptions &options) {
        return renderRegion(fs, path, 0, 0, dstW, dstH, dstW, dstH, options);
    }

    bool renderRegion(fs::FS &fs,
                      const char *path,
                      int dstX,
                      int dstY,
                      int regionW,
                      int regionH,
                      int dstW,
                      int dstH,
                      const WallpaperOptions &options) {
        if (!path || dstW <= 0 || dstH <= 0 || regionW <= 0 || regionH <= 0) return false;
        if (!fs.exists(path)) return false;

        File bmp = fs.open(path, FILE_READ);
        if (!bmp) return false;

        BMPInfo info;
        if (!readBMPInfo(bmp, info)) {
            bmp.close();
            return false;
        }

        if (dstX < 0) { regionW += dstX; dstX = 0; }
        if (dstY < 0) { regionH += dstY; dstY = 0; }
        if (dstX + regionW > dstW) regionW = dstW - dstX;
        if (dstY + regionH > dstH) regionH = dstH - dstY;
        if (regionW <= 0 || regionH <= 0) {
            bmp.close();
            return true;
        }

        uint16_t *mapX = acquireXMap(info.width, dstW);
        if (!mapX) {
            bmp.close();
            return false;
        }

        bool rowHeap = false;
        uint8_t *row = acquireBuffer(info.rowStride, &rowHeap);
        if (!row) {
            releaseXMap(mapX, dstW);
            bmp.close();
            return false;
        }

        uint8_t tone[256];
        buildToneLUT(tone, options);

        int regionBottom = dstY + regionH - 1;
        int srcYFirst = WallpaperMath::clampInt(((long)dstY * info.height) / dstH - 1, 0, info.height - 1);
        int srcYLast = WallpaperMath::clampInt(((((long)(regionBottom + 1) * info.height) + dstH - 1) / dstH) + 1, 0, info.height - 1);
        int fileRowStart = info.topDown ? srcYFirst : (info.height - 1 - srcYLast);
        int fileRowEnd = info.topDown ? srcYLast : (info.height - 1 - srcYFirst);
        uint32_t firstOffset = info.pixelOffset + ((uint32_t)fileRowStart * (uint32_t)info.rowStride);

        bool ok = bmp.seek(firstOffset);
        for (int fileRow = fileRowStart; ok && fileRow <= fileRowEnd; ++fileRow) {
            int got = bmp.read(row, info.rowStride);
            if (got < info.rowStride) {
                int clearFrom = got > 0 ? got : 0;
                memset(row + clearFrom, 0, info.rowStride - clearFrom);
            }

            int srcY = info.topDown ? fileRow : (info.height - 1 - fileRow);
            long dyStart = (long)srcY * dstH / info.height;
            long dyEnd = (((long)(srcY + 1) * dstH) - 1) / info.height;
            if (dyStart < 0) dyStart = 0;
            if (dyEnd >= dstH) dyEnd = dstH - 1;
            if (dyStart > dyEnd) continue;

            int overlapTop = (int)max((long)dstY, dyStart);
            int overlapBottom = (int)min((long)regionBottom, dyEnd);
            if (overlapTop > overlapBottom) continue;

            drawScaledRow(row, mapX, dstX, regionW, overlapTop, overlapBottom - overlapTop + 1, tone, options);
            if (options.yieldEveryNRows > 0 && ((fileRow & (options.yieldEveryNRows - 1)) == 0)) yield();
        }

        releaseBuffer(row, rowHeap);
        releaseXMap(mapX, dstW);
        bmp.close();
        return ok;
    }

  private:
    struct BMPInfo {
        uint32_t pixelOffset = 0;
        int width = 0;
        int height = 0;
        int rowStride = 0;
        bool topDown = false;
    };

    Display &display;

    bool readBMPInfo(File &bmp, BMPInfo &info) {
        uint8_t header[54];
        if (!bmp.seek(0) || (int)bmp.read(header, 54) != 54) return false;
        if (header[0] != 'B' || header[1] != 'M') return false;
        uint16_t bpp = header[28] | (header[29] << 8);
        int32_t width = (int32_t)WallpaperMath::readLE32(header, 18);
        int32_t rawHeight = (int32_t)WallpaperMath::readLE32(header, 22);
        if (bpp != 24 || width <= 0 || rawHeight == 0) return false;

        info.pixelOffset = WallpaperMath::readLE32(header, 10);
        info.width = width;
        info.topDown = rawHeight < 0;
        info.height = rawHeight < 0 ? -rawHeight : rawHeight;
        int rowBytes = info.width * 3;
        info.rowStride = rowBytes + ((4 - (rowBytes % 4)) % 4);
        return true;
    }

    static void buildToneLUT(uint8_t *tone, const WallpaperOptions &options) {
        int brightness = WallpaperMath::clampInt(options.brightnessPercent, 0, 200);
        for (int i = 0; i < 256; ++i) {
            int v = (i * brightness + 50) / 100;
            if (v > 255) v = 255;
            if (options.invert) v = 255 - v;
            tone[i] = (uint8_t)v;
        }
    }

    static uint8_t *acquireBuffer(size_t bytes, bool *heapAllocated) {
        if (heapAllocated) *heapAllocated = false;
        uint8_t *buffer = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (buffer && heapAllocated) *heapAllocated = true;
        return buffer;
    }

    static void releaseBuffer(uint8_t *buffer, bool heapAllocated) {
        if (buffer && heapAllocated) free(buffer);
    }

    static uint16_t *acquireXMap(int srcW, int dstW) {
        uint16_t *map = (uint16_t*)malloc(sizeof(uint16_t) * dstW);
        if (!map) return nullptr;
        for (int dx = 0; dx < dstW; ++dx) {
            int sx = (int)((uint32_t)dx * (uint32_t)srcW / (uint32_t)dstW);
            if (sx < 0) sx = 0;
            if (sx >= srcW) sx = srcW - 1;
            map[dx] = (uint16_t)sx;
        }
        return map;
    }

    static void releaseXMap(uint16_t *map, int) {
        if (map) free(map);
    }

    uint32_t colorFromBGR(uint8_t b,
                          uint8_t g,
                          uint8_t r,
                          const uint8_t *tone,
                          const WallpaperOptions &options,
                          bool &drawPixel) {
        int rA = tone[r];
        int gA = tone[g];
        int bA = tone[b];
        int lum = WallpaperMath::luminance(rA, gA, bA);

        if ((options.skipWhite && lum >= options.whiteAlphaThreshold) ||
            (options.skipBlack && lum <= options.blackAlphaThreshold)) {
            drawPixel = false;
            return 0;
        }

        if (options.whiteBlendQ256 > 0 && lum >= options.whiteThreshold) {
            int sum = rA + gA + bA + 1;
            rA = WallpaperMath::blendQ256(rA, (lum * rA) / sum, options.whiteBlendQ256);
            gA = WallpaperMath::blendQ256(gA, (lum * gA) / sum, options.whiteBlendQ256);
            bA = WallpaperMath::blendQ256(bA, (lum * bA) / sum, options.whiteBlendQ256);
        }
        if (options.blackBlendQ256 > 0 && lum <= options.blackThreshold) {
            rA = WallpaperMath::blendQ256(rA, 0, options.blackBlendQ256);
            gA = WallpaperMath::blendQ256(gA, 0, options.blackBlendQ256);
            bA = WallpaperMath::blendQ256(bA, 0, options.blackBlendQ256);
        }

        if (options.monochrome) {
            bA = lum;
            gA = lum;
            rA = options.redAvailable ? lum : 0;
        } else {
            gA = bA;
            if (!options.redAvailable) rA = 0;
        }

        drawPixel = true;
        return display.RGB(rA, gA, bA);
    }

    void drawScaledRow(uint8_t *row,
                       uint16_t *mapX,
                       int dstX,
                       int regionW,
                       int y,
                       int dstHeight,
                       const uint8_t *tone,
                       const WallpaperOptions &options) {
        if (dstHeight == 1) {
            for (int dx = 0; dx < regionW; ++dx) {
                int sx = mapX[dstX + dx];
                int i = sx * 3;
                bool drawPixel = true;
                uint32_t color = colorFromBGR(row[i + 0], row[i + 1], row[i + 2], tone, options, drawPixel);
                if (drawPixel) display.dotFast(dstX + dx, y, color);
            }
            return;
        }

        int runStart = -1;
        uint32_t runColor = 0;
        int runLen = 0;
        for (int dx = 0; dx < regionW; ++dx) {
            int sx = mapX[dstX + dx];
            int i = sx * 3;
            bool drawPixel = true;
            uint32_t color = colorFromBGR(row[i + 0], row[i + 1], row[i + 2], tone, options, drawPixel);
            if (!drawPixel) {
                if (runStart != -1 && runLen > 0) {
                    display.fillRect(dstX + runStart, y, runLen, dstHeight, runColor);
                    runStart = -1;
                    runLen = 0;
                }
                continue;
            }
            if (runStart == -1) {
                runStart = dx;
                runColor = color;
                runLen = 1;
            } else if (color == runColor) {
                runLen++;
            } else {
                display.fillRect(dstX + runStart, y, runLen, dstHeight, runColor);
                runStart = dx;
                runColor = color;
                runLen = 1;
            }
        }
        if (runStart != -1 && runLen > 0) {
            display.fillRect(dstX + runStart, y, runLen, dstHeight, runColor);
        }
    }
};

}
