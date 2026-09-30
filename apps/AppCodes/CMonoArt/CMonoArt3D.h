#pragma once

#include <math.h>
#include <stdlib.h>
#include <stdint.h>

// A solid, depth-sorted trefoil tube. Geometry is sampled along a 3-D torus
// knot and projected once per frame. The renderer writes straight into the
// caller's 1-bit framebuffer; it needs no image or depth buffer of its own.
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

// Plot signature: void(int x, int y, bool white). The caller clears the
// framebuffer first, then calls this once per scene change or rotation.
// "width" is the actual horizontal pixel count; y is restricted to top..bottom.
template <class Plot>
void render(Plot plot, int width, int top, int bottom,
            float yaw, float pitch, float zoom) {
    static constexpr int COUNT = 240;
    static constexpr float FULL_CIRCLE = 6.28318530718f;
    static constexpr float TUBE_RADIUS = 0.105f;
    // The Arduino loop task has a small stack. Keep the only geometry cache
    // in static RAM (about 6.2 KB), not on that task's stack.
    static KnotPoint points[COUNT];
    static uint16_t order[COUNT];
    const float cosYaw = cosf(yaw), sinYaw = sinf(yaw);
    const float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
    float maxX = 0.0f, maxY = 0.0f;

    for (int i = 0; i < COUNT; ++i) {
        const float t = FULL_CIRCLE * i / COUNT;
        const float arm = 1.03f + 0.40f * cosf(3.0f * t);
        const float worldX = arm * cosf(2.0f * t);
        const float worldY = arm * sinf(2.0f * t);
        const float worldZ = 0.62f * sinf(3.0f * t);
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

} // namespace CMonoArt3D
