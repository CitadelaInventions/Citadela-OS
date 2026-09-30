#pragma once

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// A solid, depth-sorted trefoil tube. Geometry is sampled along a 3-D torus
// knot and projected once per frame. The direct path plots to the caller;
// the incremental path uses one packed scratch canvas for exact comparisons.
namespace CMonoArt3D {

struct KnotPoint {
    float x;
    float y;
    float depth;
    float perspective;
    int16_t screenX;
    int16_t screenY;
    uint8_t radiusX;
    uint8_t radiusY;
};

struct WorldPoint {
    float x;
    float y;
    float z;
};

// A temporary packed image of the 3-D scene. Unlike a pixel-at-a-time video
// callback, an ellipse row can write its ordered black/white dither pattern a
// byte at a time. The two paths have the same final bits, including overlaps.
struct PackedCanvas {
    uint8_t *bits;
    int stride;
    int top;

    inline void operator()(int x, int y, bool white) {
        uint8_t &value = bits[(y - top) * stride + (x >> 3)];
        const uint8_t mask = (uint8_t)(0x80U >> (x & 7));
        if (white) value |= mask;
        else value &= (uint8_t)~mask;
    }

    inline void shadeSpan(int firstX, int lastX, int y, int light) {
        if (firstX > lastX) return;
        // Each entry is the exact eight-pixel repeat of the original 4x4
        // Bayer test for one row and light level.
        static constexpr uint8_t PATTERN[4][16] = {
            {0x00,0x88,0x88,0xAA,0xAA,0xAA,0xAA,0xAA,
             0xAA,0xEE,0xEE,0xFF,0xFF,0xFF,0xFF,0xFF},
            {0x00,0x00,0x00,0x00,0x00,0x44,0x44,0x55,
             0x55,0x55,0x55,0x55,0x55,0xDD,0xDD,0xFF},
            {0x00,0x00,0x22,0x22,0xAA,0xAA,0xAA,0xAA,
             0xAA,0xAA,0xBB,0xBB,0xFF,0xFF,0xFF,0xFF},
            {0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x11,
             0x55,0x55,0x55,0x55,0x55,0x55,0x77,0x77}
        };
        const uint8_t pattern = PATTERN[y & 3][light];

        uint8_t *row = bits + (y - top) * stride;
        const int firstByte = firstX >> 3;
        const int lastByte = lastX >> 3;
        const uint8_t firstMask = (uint8_t)(0xffU >> (firstX & 7));
        const uint8_t lastMask = (uint8_t)(0xffU << (7 - (lastX & 7)));
        if (firstByte == lastByte) {
            const uint8_t mask = firstMask & lastMask;
            row[firstByte] = (row[firstByte] & (uint8_t)~mask) |
                             (pattern & mask);
            return;
        }
        row[firstByte] = (row[firstByte] & (uint8_t)~firstMask) |
                         (pattern & firstMask);
        if (lastByte > firstByte + 1)
            memset(row + firstByte + 1, pattern,
                   (size_t)(lastByte - firstByte - 1));
        row[lastByte] = (row[lastByte] & (uint8_t)~lastMask) |
                        (pattern & lastMask);
    }
};

template <class Plot>
inline void pixel(Plot &plot, int x, int y, bool white,
                  int width, int top, int bottom) {
    if ((unsigned)x < (unsigned)width && y >= top && y <= bottom)
        plot(x, y, white);
}

template <class Plot>
inline void line(Plot &plot, int x0, int y0, int x1, int y1,
                 int width, int top, int bottom) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(plot, x0, y0, true, width, top, bottom);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

template <class Plot>
inline void shadedEllipse(Plot &plot, int centerX, int centerY,
                          int radiusX, int radiusY,
                          int width, int top, int bottom) {
    static constexpr uint8_t BAYER[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6},
        {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    const int firstY = centerY - radiusY > top ? centerY - radiusY : top;
    const int lastY = centerY + radiusY < bottom ? centerY + radiusY : bottom;
    if (firstY > lastY) return;
    const float invRadiusSq = 1.0f / (float)(radiusY * radiusY);
    for (int y = firstY; y <= lastY; ++y) {
        const int dy = y - centerY;
        const int halfSpan = (int)(radiusX *
            sqrtf(1.0f - dy * dy * invRadiusSq));
        const int firstX = centerX - halfSpan > 0 ? centerX - halfSpan : 0;
        const int lastX = centerX + halfSpan < width - 1 ?
            centerX + halfSpan : width - 1;
        int light = 8 - dy * 5 / radiusY;
        if (light < 2) light = 2;
        if (light > 13) light = 13;
        for (int x = firstX; x <= lastX; ++x)
            plot(x, y, BAYER[y & 3][x & 3] < light);
    }
}

inline void shadedEllipse(PackedCanvas &plot, int centerX, int centerY,
                          int radiusX, int radiusY,
                          int width, int top, int bottom) {
    const int firstY = centerY - radiusY > top ? centerY - radiusY : top;
    const int lastY = centerY + radiusY < bottom ? centerY + radiusY : bottom;
    if (firstY > lastY) return;
    const float invRadiusSq = 1.0f / (float)(radiusY * radiusY);
    for (int y = firstY; y <= lastY; ++y) {
        const int dy = y - centerY;
        const int halfSpan = (int)(radiusX *
            sqrtf(1.0f - dy * dy * invRadiusSq));
        const int firstX = centerX - halfSpan > 0 ? centerX - halfSpan : 0;
        const int lastX = centerX + halfSpan < width - 1 ?
            centerX + halfSpan : width - 1;
        int light = 8 - dy * 5 / radiusY;
        if (light < 2) light = 2;
        if (light > 13) light = 13;
        plot.shadeSpan(firstX, lastX, y, light);
    }
}

template <class Plot>
inline void paintSegment(Plot &plot, const KnotPoint &a,
                         const KnotPoint &b, int segment,
                         float pixelAspect, int width, int top, int bottom) {
    const int dx = b.screenX - a.screenX;
    const int dy = b.screenY - a.screenY;
    const int radiusX = (a.radiusX + b.radiusX + 1) / 2;
    const int radiusY = (a.radiusY + b.radiusY + 1) / 2;

    // Stamp just enough dithered cross sections to cover the space between
    // the sample points. Near strokes erase distant strands at crossings.
    int stepsX = (abs(dx) + radiusX / 3) / (radiusX / 3 + 1);
    int stepsY = (abs(dy) + radiusY / 3) / (radiusY / 3 + 1);
    int stamps = stepsX > stepsY ? stepsX : stepsY;
    if (stamps < 1) stamps = 1;
    for (int i = 0; i <= stamps; ++i) {
        const int x = a.screenX + dx * i / stamps;
        const int y = a.screenY + dy * i / stamps;
        shadedEllipse(plot, x, y, radiusX, radiusY,
                      width, top, bottom);
    }

    // The normal is calculated in display-space units. A TV pixel is far
    // narrower horizontally than vertically in this 1645x288 PAL mode.
    const float physicalDx = dx / pixelAspect;
    const float physicalLength = sqrtf(physicalDx * physicalDx + dy * dy);
    if (physicalLength < 0.01f) return;
    const float normalX = -dy / physicalLength;
    const float normalY = physicalDx / physicalLength;
    const int ax = a.screenX, ay = a.screenY;
    const int bx = b.screenX, by = b.screenY;
    const int anx = (int)lroundf(normalX * a.radiusX);
    const int any = (int)lroundf(normalY * a.radiusY);
    const int bnx = (int)lroundf(normalX * b.radiusX);
    const int bny = (int)lroundf(normalY * b.radiusY);
    line(plot, ax + anx, ay + any, bx + bnx, by + bny,
         width, top, bottom);
    line(plot, ax - anx, ay - any, bx - bnx, by - bny,
         width, top, bottom);

    // Fine longitudinal engraving and cross ribs reveal curvature while
    // retaining a strictly black/white image at the native pixel pitch.
    line(plot, ax + anx / 3, ay + any / 3,
         bx + bnx / 3, by + bny / 3, width, top, bottom);
    if ((segment % 5) == 0) {
        line(plot, ax - anx, ay - any, ax + anx, ay + any,
             width, top, bottom);
        line(plot, ax - anx / 2, ay - any / 2,
             ax + anx / 2, ay + any / 2, width, top, bottom);
    }
}

// Plot signature: void(int x, int y, bool white). Direct callers clear the
// framebuffer first; renderIncremental below handles comparison and updates.
// "width" is the actual horizontal pixel count; y is restricted to top..bottom.
template <class Plot>
void render(Plot plot, int width, int top, int bottom,
            float yaw, float pitch, float zoom) {
    static constexpr int COUNT = 240;
    static constexpr float FULL_CIRCLE = 6.28318530718f;
    static constexpr float TUBE_RADIUS = 0.105f;
    // The Arduino loop task has a small stack. Keep geometry caches in static
    // RAM rather than on that task's stack.
    static KnotPoint points[COUNT];
    static uint16_t order[COUNT];
    // The knot's unrotated shape is immutable. Cache its samples so mouse
    // movements do not repeat 960 trigonometric calls per frame. Keep the
    // exact expressions used by the original renderer for identical pixels.
    static WorldPoint world[COUNT];
    static bool worldReady = false;
    if (!worldReady) {
        for (int i = 0; i < COUNT; ++i) {
            const float t = FULL_CIRCLE * i / COUNT;
            const float arm = 1.03f + 0.40f * cosf(3.0f * t);
            world[i].x = arm * cosf(2.0f * t);
            world[i].y = arm * sinf(2.0f * t);
            world[i].z = 0.62f * sinf(3.0f * t);
        }
        worldReady = true;
    }
    const float cosYaw = cosf(yaw), sinYaw = sinf(yaw);
    const float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
    float maxX = 0.0f, maxY = 0.0f;

    for (int i = 0; i < COUNT; ++i) {
        const float worldX = world[i].x;
        const float worldY = world[i].y;
        const float worldZ = world[i].z;
        const float rotatedX = cosYaw * worldX + sinYaw * worldZ;
        const float yawZ = -sinYaw * worldX + cosYaw * worldZ;
        const float rotatedY = cosPitch * worldY - sinPitch * yawZ;
        const float rotatedZ = sinPitch * worldY + cosPitch * yawZ;
        const float perspective = 4.8f / (4.8f - rotatedZ);
        KnotPoint &point = points[i];
        point.x = rotatedX * perspective;
        point.y = rotatedY * perspective;
        point.depth = rotatedZ;
        point.perspective = perspective;
        if (fabsf(point.x) > maxX) maxX = fabsf(point.x);
        if (fabsf(point.y) > maxY) maxY = fabsf(point.y);
        order[i] = (uint16_t)i;
    }

    // PAL's displayed picture is 4:3, so horizontal pixels are narrower.
    const float pixelAspect = width * (3.0f / (4.0f * 288.0f));
    const int midY = (top + bottom) / 2;
    const float halfY = (bottom - top + 1) * 0.5f - 13.0f;
    const float halfX = width * 0.5f - 24.0f;
    float scale = halfY / (maxY + TUBE_RADIUS * 1.6f);
    const float horizontalScale = halfX /
        ((maxX + TUBE_RADIUS * 1.6f) * pixelAspect);
    if (horizontalScale < scale) scale = horizontalScale;
    if (zoom < 0.5f) zoom = 0.5f;
    if (zoom > 1.6f) zoom = 1.6f;
    scale *= zoom;
    for (int i = 0; i < COUNT; ++i) {
        KnotPoint &point = points[i];
        point.screenX = (int16_t)lroundf(width * 0.5f +
            point.x * scale * pixelAspect);
        point.screenY = (int16_t)lroundf(midY - point.y * scale);
        point.radiusX = (uint8_t)ceilf(TUBE_RADIUS * point.perspective *
            scale * pixelAspect);
        point.radiusY = (uint8_t)ceilf(TUBE_RADIUS * point.perspective * scale);
        if (!point.radiusX) point.radiusX = 1;
        if (!point.radiusY) point.radiusY = 1;
    }

    // Insertion sorting just 240 indices avoids a second geometry buffer.
    // Rendering back to front lets each near tube wipe out far linework.
    for (int i = 1; i < COUNT; ++i) {
        const uint16_t current = order[i];
        const float depth = points[current].depth +
            points[(current + 1) % COUNT].depth;
        int j = i;
        while (j > 0) {
            const uint16_t previous = order[j - 1];
            const float previousDepth = points[previous].depth +
                points[(previous + 1) % COUNT].depth;
            if (previousDepth <= depth) break;
            order[j] = previous;
            --j;
        }
        order[j] = current;
    }
    for (int i = 0; i < COUNT; ++i) {
        const uint16_t segment = order[i];
        paintSegment(plot, points[segment], points[(segment + 1) % COUNT],
                     segment, pixelAspect, width, top, bottom);
    }
}

struct IncrementalStats {
    bool ok;
    uint32_t changedBytes;
    uint32_t changedPixels;
};

// Both interactive models use this one scratch allocation. Keeping it in a
// non-template function avoids one allocation per renderer/lambda type.
inline uint8_t *packedScratch(size_t required) {
    static uint8_t *scratch = nullptr;
    static size_t capacity = 0;
    if (capacity < required) {
        uint8_t *replacement = (uint8_t *)realloc(scratch, required);
        if (!replacement) return nullptr;
        scratch = replacement;
        capacity = required;
    }
    memset(scratch, 0, required);
    return scratch;
}

template <class Row>
IncrementalStats commitPacked(Row row, const uint8_t *source,
                              size_t stride, int top, int bottom) {
    IncrementalStats stats = {false, 0, 0};
    for (int y = top; y <= bottom; ++y) {
        uint8_t *destination = row(y);
        if (!destination) return stats;
        const uint8_t *packed = source + (size_t)(y - top) * stride;
        for (size_t byte = 0; byte < stride; ++byte) {
            const uint8_t before = destination[byte];
            const uint8_t after = packed[byte];
            if (before == after) continue;
            destination[byte] = after;
            ++stats.changedBytes;
            stats.changedPixels += __builtin_popcount((unsigned)(before ^ after));
        }
    }
    stats.ok = true;
    return stats;
}

// Row signature: uint8_t *(int y). Call without clearing the existing video
// image on rotations. Rendering to a packed scratch image resolves all strand
// overlap first; comparison then commits only bytes with changed final pixels.
// Other scene regions, captions, and identical pixels stay untouched.
template <class Row>
IncrementalStats renderIncremental(Row row, int width, int top, int bottom,
                                   float yaw, float pitch, float zoom) {
    IncrementalStats stats = {false, 0, 0};
    if (width <= 0 || bottom < top) return stats;
    const size_t stride = (size_t)(width + 7) / 8;
    const size_t required = stride * (size_t)(bottom - top + 1);
    uint8_t *scratch = packedScratch(required);
    if (!scratch) return stats;
    PackedCanvas canvas = {scratch, (int)stride, top};
    render(canvas, width, top, bottom, yaw, pitch, zoom);
    return commitPacked(row, scratch, stride, top, bottom);
}

// A second, separate 3-D object: a plated icosahedral hub inside three
// perpendicular graduated gimbals. Rear gimbals are drawn first, the opaque
// faceted hub covers them, and front gimbals are drawn last.
struct SecondPoint {
    int16_t x;
    int16_t y;
    float depth;
};

struct SecondVertex {
    SecondPoint screen;
    float x;
    float y;
    float z;
};

struct SecondCamera {
    float cy, sy, cp, sp;
    float scale, aspect;
    int middleX, middleY;
};

inline SecondVertex secondProject(const SecondCamera &camera,
                                  float x, float y, float z) {
    const float rx = camera.cy * x + camera.sy * z;
    const float yawZ = -camera.sy * x + camera.cy * z;
    const float ry = camera.cp * y - camera.sp * yawZ;
    const float rz = camera.sp * y + camera.cp * yawZ;
    const float perspective = 8.0f / (8.0f - rz);
    SecondVertex projected;
    projected.screen.x = (int16_t)lroundf(camera.middleX +
        rx * perspective * camera.scale * camera.aspect);
    projected.screen.y = (int16_t)lroundf(camera.middleY -
        ry * perspective * camera.scale);
    projected.screen.depth = rz;
    projected.x = rx;
    projected.y = ry;
    projected.z = rz;
    return projected;
}

template <class Plot>
inline void secondLine(Plot &plot, int x0, int y0, int x1, int y1,
                       bool white, int width, int top, int bottom) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(plot, x0, y0, white, width, top, bottom);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

template <class Plot>
inline void secondShadeSpan(Plot &plot, int firstX, int lastX,
                            int y, int light) {
    static constexpr uint8_t BAYER[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6},
        {3, 11, 1, 9}, {15, 7, 13, 5}
    };
    for (int x = firstX; x <= lastX; ++x)
        plot(x, y, BAYER[y & 3][x & 3] < light);
}

inline void secondShadeSpan(PackedCanvas &plot, int firstX, int lastX,
                            int y, int light) {
    plot.shadeSpan(firstX, lastX, y, light);
}

template <class Plot>
inline void secondTriangle(Plot &plot, int ax, int ay, int bx, int by,
                           int cx, int cy, int light,
                           int width, int top, int bottom) {
    int firstY = ay < by ? ay : by;
    if (cy < firstY) firstY = cy;
    int lastY = ay > by ? ay : by;
    if (cy > lastY) lastY = cy;
    if (firstY < top) firstY = top;
    if (lastY > bottom) lastY = bottom;
    const int vx[3] = {ax, bx, cx};
    const int vy[3] = {ay, by, cy};
    for (int y = firstY; y <= lastY; ++y) {
        const float scan = y + 0.5f;
        float firstX = 0.0f, lastX = 0.0f;
        int crossings = 0;
        for (int edge = 0; edge < 3; ++edge) {
            const int next = (edge + 1) % 3;
            const int y0 = vy[edge], y1 = vy[next];
            if (y0 == y1 || scan < (y0 < y1 ? y0 : y1) ||
                scan >= (y0 > y1 ? y0 : y1)) continue;
            const float x = vx[edge] +
                (scan - y0) * (vx[next] - vx[edge]) / (y1 - y0);
            if (!crossings || x < firstX) firstX = x;
            if (!crossings || x > lastX) lastX = x;
            ++crossings;
        }
        if (crossings < 2) continue;
        int left = (int)ceilf(firstX), right = (int)floorf(lastX);
        if (left < 0) left = 0;
        if (right >= width) right = width - 1;
        if (left <= right) secondShadeSpan(plot, left, right, y, light);
    }
}

template <class Plot>
inline void secondRings(Plot &plot, const SecondPoint (&rings)[3][128],
                        bool front, int width, int top, int bottom) {
    for (int ring = 0; ring < 3; ++ring) {
        for (int i = 0; i < 128; ++i) {
            const SecondPoint &a = rings[ring][i];
            const SecondPoint &b = rings[ring][(i + 1) & 127];
            if (((a.depth + b.depth) >= 0.0f) != front) continue;
            // Dual polished rails and a fine dark seam give each orbit band
            // thickness without any grayscale pixels.
            secondLine(plot, a.x, a.y - 1, b.x, b.y - 1,
                       true, width, top, bottom);
            secondLine(plot, a.x, a.y + 1, b.x, b.y + 1,
                       true, width, top, bottom);
            secondLine(plot, a.x, a.y, b.x, b.y,
                       false, width, top, bottom);
            if ((i & 15) == 0) {
                secondLine(plot, a.x - 3, a.y, a.x + 3, a.y,
                           true, width, top, bottom);
                secondLine(plot, a.x, a.y - 2, a.x, a.y + 2,
                           true, width, top, bottom);
            }
        }
    }
}

// Direct version is also the allocation-failure fallback. Plot has the same
// void(int x, int y, bool white) signature as the trefoil's direct renderer.
template <class Plot>
void renderSecond(Plot plot, int width, int top, int bottom,
                  float yaw, float pitch, float zoom) {
    static constexpr float PHI = 1.61803398875f;
    static constexpr float ICO[12][3] = {
        {-1, PHI, 0}, {1, PHI, 0}, {-1,-PHI, 0}, {1,-PHI, 0},
        {0,-1, PHI}, {0, 1, PHI}, {0,-1,-PHI}, {0, 1,-PHI},
        {PHI,0,-1}, {PHI,0, 1}, {-PHI,0,-1}, {-PHI,0, 1}
    };
    static constexpr uint8_t FACE[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    static float sine[128], cosine[128];
    static bool circleReady = false;
    if (!circleReady) {
        for (int i = 0; i < 128; ++i) {
            const float angle = 6.28318530718f * i / 128;
            sine[i] = sinf(angle);
            cosine[i] = cosf(angle);
        }
        circleReady = true;
    }
    if (zoom < 0.5f) zoom = 0.5f;
    if (zoom > 1.6f) zoom = 1.6f;
    const float aspect = width * (3.0f / (4.0f * 288.0f));
    const float halfY = (bottom - top + 1) * 0.5f - 13.0f;
    const float halfX = width * 0.5f - 24.0f;
    float scale = halfY / (1.75f * 1.28f);
    const float horizontalScale = halfX / (1.75f * 1.28f * aspect);
    if (horizontalScale < scale) scale = horizontalScale;
    SecondCamera camera = {cosf(yaw), sinf(yaw), cosf(pitch), sinf(pitch),
                           scale * zoom, aspect, width / 2, (top + bottom) / 2};

    static SecondPoint rings[3][128];
    for (int i = 0; i < 128; ++i) {
        const float c = cosine[i], s = sine[i];
        const SecondVertex a = secondProject(camera, 1.30f*c, 1.30f*s, 0);
        const SecondVertex b = secondProject(camera, 0, 1.52f*c, 1.52f*s);
        const SecondVertex d = secondProject(camera, 1.75f*s, 0, 1.75f*c);
        rings[0][i] = a.screen;
        rings[1][i] = b.screen;
        rings[2][i] = d.screen;
    }
    secondRings(plot, rings, false, width, top, bottom);

    SecondVertex hub[12];
    for (int i = 0; i < 12; ++i)
        hub[i] = secondProject(camera, ICO[i][0] * 0.55f,
                              ICO[i][1] * 0.55f, ICO[i][2] * 0.55f);
    uint8_t order[20];
    for (int i = 0; i < 20; ++i) order[i] = (uint8_t)i;
    for (int i = 1; i < 20; ++i) {
        const uint8_t current = order[i];
        const float depth = hub[FACE[current][0]].z +
            hub[FACE[current][1]].z + hub[FACE[current][2]].z;
        int j = i;
        while (j > 0) {
            const uint8_t previous = order[j - 1];
            const float priorDepth = hub[FACE[previous][0]].z +
                hub[FACE[previous][1]].z + hub[FACE[previous][2]].z;
            if (priorDepth <= depth) break;
            order[j] = previous;
            --j;
        }
        order[j] = current;
    }
    for (int f = 0; f < 20; ++f) {
        const int index = order[f];
        const SecondVertex &a = hub[FACE[index][0]];
        const SecondVertex &b = hub[FACE[index][1]];
        const SecondVertex &c = hub[FACE[index][2]];
        const float ux = b.x-a.x, uy = b.y-a.y, uz = b.z-a.z;
        const float vx = c.x-a.x, vy = c.y-a.y, vz = c.z-a.z;
        const float nx = uy*vz-uz*vy;
        const float ny = uz*vx-ux*vz;
        const float nz = ux*vy-uy*vx;
        const float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
        const float illumination = nlen > 0.0f ?
            (-0.38f*nx + 0.52f*ny + 0.76f*nz) / nlen : 0.0f;
        int light = (int)lroundf(8.0f + 5.0f*illumination);
        if (light < 2) light = 2;
        if (light > 14) light = 14;
        const int ax=a.screen.x, ay=a.screen.y;
        const int bx=b.screen.x, by=b.screen.y;
        const int cx=c.screen.x, cy=c.screen.y;
        secondTriangle(plot, ax,ay,bx,by,cx,cy,light,width,top,bottom);

        const int mx=(ax+bx+cx)/3, my=(ay+by+cy)/3;
        const int iax=(4*ax+mx)/5, iay=(4*ay+my)/5;
        const int ibx=(4*bx+mx)/5, iby=(4*by+my)/5;
        const int icx=(4*cx+mx)/5, icy=(4*cy+my)/5;
        const int panelLight = light < 12 ? light + 2 : light - 2;
        secondTriangle(plot, iax,iay,ibx,iby,icx,icy,panelLight,
                       width,top,bottom);
        secondLine(plot, ax,ay,bx,by,false,width,top,bottom);
        secondLine(plot, bx,by,cx,cy,false,width,top,bottom);
        secondLine(plot, cx,cy,ax,ay,false,width,top,bottom);
        secondLine(plot, iax,iay,ibx,iby,true,width,top,bottom);
        secondLine(plot, ibx,iby,icx,icy,true,width,top,bottom);
        secondLine(plot, icx,icy,iax,iay,true,width,top,bottom);
        // Each plate has a small dark fastener, kept in the same depth order
        // so that nearer facets cover details on the far side.
        secondLine(plot, mx-2,my,mx+2,my,false,width,top,bottom);
        secondLine(plot, mx,my-1,mx,my+1,false,width,top,bottom);
        pixel(plot,mx,my,true,width,top,bottom);
    }
    secondRings(plot, rings, true, width, top, bottom);
}

template <class Row>
IncrementalStats renderSecondIncremental(Row row, int width,
                                         int top, int bottom,
                                         float yaw, float pitch, float zoom) {
    IncrementalStats failed = {false, 0, 0};
    if (width <= 0 || bottom < top) return failed;
    const size_t stride = (size_t)(width + 7) / 8;
    uint8_t *scratch = packedScratch(stride * (size_t)(bottom - top + 1));
    if (!scratch) return failed;
    PackedCanvas canvas = {scratch, (int)stride, top};
    renderSecond(canvas, width, top, bottom, yaw, pitch, zoom);
    return commitPacked(row, scratch, stride, top, bottom);
}

} // namespace CMonoArt3D
