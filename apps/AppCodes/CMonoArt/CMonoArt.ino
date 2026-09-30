#include <Arduino.h>
#include <ESP32Video.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"
#include "../../../System/Libraries/CitadelaStorage.h"
#include "fonts/InterMono.h"
#include "CMonoArt3D.h"

static constexpr int WIDTH = 1645;
static constexpr int HEIGHT = 288;
static constexpr int VIDEO_PIN = 25;
static constexpr int MAX_ITERATIONS = 72;
static constexpr const char *BOOT_STATE_PATH = "/evil.txt";
static constexpr int TOP_BAR = 30;
static constexpr int FOOTER_TOP = HEIGHT - 35;

enum Scene : uint8_t { FRACTAL, LETTERING, ETCHING, KNOT, GYROSCOPE, SCENE_COUNT };
static const char *const SCENE_NAMES[] = {
    "FRACTAL CONTOURS", "TYPE SPECIMEN", "MICRO ETCHING",
    "TREFOIL KNOT 3D", "ORBITAL GYROSCOPE 3D"
};

static Citadela::CitCompositeColorDAC video;
static Citadela::LineReader controllerInput(128);
static Citadela::LineReader usbInput(128);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);
static bool videoReady = false;
static bool spiffsReady = false;
static float centerReal = -0.65f;
static float centerImaginary = 0.0f;
static float viewHeight = 2.35f;
static int nextRow = 0;
static uint32_t renderStarted = 0;
static Scene scene = FRACTAL;
static bool is3DScene() { return scene == KNOT || scene == GYROSCOPE; }
static Scene displayedScene = SCENE_COUNT;
static bool frameComplete = false;
static float objectYaw = 0.45f;
static float objectPitch = -0.25f;
static float objectZoom = 1.0f;
static float displayedCenterReal = 0.0f;
static float displayedCenterImaginary = 0.0f;
static float displayedViewHeight = 0.0f;
static float displayedYaw = 0.0f;
static float displayedPitch = 0.0f;
static float displayedZoom = 0.0f;
static bool modelCanvasValid = false;
static bool captionReusable = false;
static bool modelInputDirty = false;
static CMonoArt3D::IncrementalStats modelStats = {false, 0, 0};
static bool ridgesReady = false;
static bool mouseSeen = false;
static bool mouseLeftDown = false;
static bool mouseOrbitLatched = false;
static bool cursorShown = false;
static int mouseControllerX = 188;
static int mouseControllerY = 142;
static int cursorX = WIDTH / 2;
static int cursorY = HEIGHT / 2;
static constexpr int CURSOR_RADIUS = 4;
static constexpr int CURSOR_SCALE_X = 4;
static constexpr int CURSOR_DIAMETER = CURSOR_RADIUS * 2 + 1;
static uint8_t cursorUnder[CURSOR_DIAMETER * CURSOR_DIAMETER * CURSOR_SCALE_X];
static uint32_t crcTable[256];
static int16_t farRidge[WIDTH];
static int16_t nearRidge[WIDTH];
static int16_t cloudLineA[WIDTH];
static int16_t cloudLineB[WIDTH];

// The system cursor is a small contrasting ring. Save the exact background
// bits so mouse motion never alters the scene or its reported framebuffer CRC.
static bool cursorPoint(int dx, int dy) {
    const int distance2 = dx * dx + dy * dy;
    return distance2 >= 5 && distance2 <= 18;
}

static void restoreCursor() {
    if (!cursorShown || !videoReady) return;
    for (int dy = -CURSOR_RADIUS; dy <= CURSOR_RADIUS; ++dy) {
        const int py = cursorY + dy;
        if ((unsigned)py >= HEIGHT) continue;
        for (int dx = -CURSOR_RADIUS; dx <= CURSOR_RADIUS; ++dx) {
            if (!cursorPoint(dx, dy)) continue;
            for (int sub = 0; sub < CURSOR_SCALE_X; ++sub) {
                const int px = cursorX + dx * CURSOR_SCALE_X + sub;
                if ((unsigned)px >= WIDTH) continue;
                const int offset = ((dy + CURSOR_RADIUS) * CURSOR_DIAMETER +
                                    dx + CURSOR_RADIUS) * CURSOR_SCALE_X + sub;
                video.mono1Pixel(px, py, cursorUnder[offset] != 0);
            }
        }
    }
    cursorShown = false;
}

static void drawCursor() {
    if (!videoReady || !mouseSeen || !frameComplete || cursorShown) return;
    for (int dy = -CURSOR_RADIUS; dy <= CURSOR_RADIUS; ++dy) {
        const int py = cursorY + dy;
        if ((unsigned)py >= HEIGHT) continue;
        for (int dx = -CURSOR_RADIUS; dx <= CURSOR_RADIUS; ++dx) {
            if (!cursorPoint(dx, dy)) continue;
            for (int sub = 0; sub < CURSOR_SCALE_X; ++sub) {
                const int px = cursorX + dx * CURSOR_SCALE_X + sub;
                if ((unsigned)px >= WIDTH) continue;
                const int offset = ((dy + CURSOR_RADIUS) * CURSOR_DIAMETER +
                                    dx + CURSOR_RADIUS) * CURSOR_SCALE_X + sub;
                const bool under = video.mono1PixelAt(px, py);
                cursorUnder[offset] = under;
                video.mono1Pixel(px, py, !under);
            }
        }
    }
    cursorShown = true;
}

static bool writeBootState(const char *state) {
    if (!spiffsReady) return false;
    SPIFFS.remove(BOOT_STATE_PATH); // FILE_WRITE appends on this ESP32 core.
    File file = SPIFFS.open(BOOT_STATE_PATH, FILE_WRITE);
    if (!file) return false;
    bool ok = file.println(state) > 0;
    file.close();
    return ok;
}

static String readBootState() {
    if (!spiffsReady) return "false";
    File file = SPIFFS.open(BOOT_STATE_PATH, FILE_READ);
    if (!file) return "false";
    String state = file.readStringUntil('\n');
    file.close();
    state.trim();
    return state;
}

static bool mountSD() {
    const uint32_t frequencies[] = {4000000, 2000000, 1000000, 400000};
    return Citadela::Storage::beginBoardSDWithRetry(
        SD, SPI, frequencies, sizeof(frequencies) / sizeof(frequencies[0]),
        nullptr, &Serial);
}

static void flashKernel() {
    if (!mountSD()) {
        Serial.println("MONO RETURN FAILED: SD mount");
        return;
    }
    File image = SD.open("/System/kernel.bin", FILE_READ);
    const size_t bytes = image ? image.size() : 0;
    if (!image || bytes < 32768 || image.read() != 0xE9 || !image.seek(0)) {
        if (image) image.close();
        Serial.println("MONO RETURN FAILED: invalid kernel image");
        SD.end();
        return;
    }
    controllerVideo.appFlashStart(bytes, "Returning to Citadela OS");
    if (!Update.begin(bytes, U_FLASH)) {
        image.close();
        SD.end();
        Serial.printf("MONO RETURN FAILED: %s\n", Update.errorString());
        return;
    }
    uint8_t buffer[2048];
    size_t written = 0;
    while (written < bytes) {
        int count = image.read(buffer, min(sizeof(buffer), bytes - written));
        if (count <= 0 || Update.write(buffer, count) != (size_t)count) {
            Update.abort();
            image.close();
            SD.end();
            Serial.println("MONO RETURN FAILED: write");
            return;
        }
        written += count;
        controllerVideo.appFlashWrite(written);
        yield();
    }
    image.close();
    if (!Update.end(true)) {
        SD.end();
        Serial.printf("MONO RETURN FAILED: %s\n", Update.errorString());
        return;
    }
    SD.end();
    Serial1.println("VDSTOP");
    Serial1.flush();
    ESP.restart();
}

static void returnToKernel() {
    if (!writeBootState("trueKernel")) {
        Serial.println("MONO EXIT FAILED: boot marker unavailable");
        return;
    }
    restoreCursor();
    controllerVideo.prepare("Returning to Citadela OS", 0);
    if (videoReady) video.releaseVideoMemory();
    videoReady = false;
    pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(50);
    ESP.restart();
}

static void strokeLine(int x0, int y0, int x1, int y1, bool white = true,
                       int weight = 1) {
    const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        video.mono1Pixel(x0, y0, white);
        if (weight > 1) video.mono1Pixel(x0 + 1, y0, white);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * error;
        if (e2 >= dy) { error += dy; x0 += sx; }
        if (e2 <= dx) { error += dx; y0 += sy; }
    }
}

static void prepareRidges() {
    for (int x = 0; x < WIDTH; ++x) {
        const float u = (float)x / WIDTH;
        farRidge[x] = (int16_t)(120 + 17 * sinf(u * 17.3f) +
            9 * sinf(u * 51.7f) + 4 * sinf(u * 127.0f));
        nearRidge[x] = (int16_t)(175 + 19 * sinf(u * 14.1f + 1.7f) +
            8 * sinf(u * 43.0f) + 3 * sinf(u * 123.0f));
        cloudLineA[x] = (int16_t)(66 + 8 * sinf(u * 16.1f));
        cloudLineB[x] = (int16_t)(94 + 5 * sinf(u * 31.0f + 1.8f));
    }
}

static void beginRender() {
    if (!videoReady) return;
    if (frameComplete && displayedScene == scene) {
        const bool unchanged = scene == FRACTAL ?
            centerReal == displayedCenterReal &&
            centerImaginary == displayedCenterImaginary &&
            viewHeight == displayedViewHeight :
            is3DScene() ?
            objectYaw == displayedYaw &&
            objectPitch == displayedPitch &&
            objectZoom == displayedZoom : true;
        if (unchanged) {
            drawCursor();
            Serial.printf("MONO SKIP mode=%u unchanged\n", (unsigned)scene);
            return;
        }
    }
    restoreCursor();
    captionReusable = is3DScene() && modelCanvasValid &&
                      displayedScene == scene;
    if (!captionReusable) video.mono1Clear(false);
    if (!is3DScene() || displayedScene != scene) modelCanvasValid = false;
    nextRow = 0;
    renderStarted = millis();
    frameComplete = false;
    if (scene == ETCHING && !ridgesReady) {
        prepareRidges();
        ridgesReady = true;
    }
    Serial.printf("MONO MODE %u %s\n", (unsigned)scene, SCENE_NAMES[scene]);
}

static void renderMandelbrotRow(int y) {
    uint8_t *row = video.mono1Row(y);
    if (!row) return;
    if (centerImaginary == 0.0f && y >= HEIGHT / 2) {
        memcpy(row, video.mono1Row(HEIGHT - 1 - y), video.mono1Stride());
        return;
    }
    const float imaginary = centerImaginary +
        ((float)y + 0.5f - HEIGHT * 0.5f) * viewHeight / HEIGHT;
    const float step = viewHeight * (4.0f / 3.0f) / WIDTH;
    float real = centerReal - step * (WIDTH * 0.5f - 0.5f);
    const float imaginarySquared = imaginary * imaginary;
    for (int byte = 0; byte < video.mono1Stride(); ++byte) {
        uint8_t packed = 0;
        for (int bit = 0; bit < 8; ++bit) {
            int x = byte * 8 + bit;
            if (x >= WIDTH) break;
            float zr = 0.0f, zi = 0.0f;
            int iterations = MAX_ITERATIONS;
            const float bulbReal = real + 1.0f;
            const float cardioidReal = real - 0.25f;
            const float q = cardioidReal * cardioidReal + imaginarySquared;
            if (bulbReal * bulbReal + imaginarySquared > 0.0625f &&
                q * (q + cardioidReal) > 0.25f * imaginarySquared) {
                iterations = 0;
                while (iterations < MAX_ITERATIONS && zr * zr + zi * zi < 64.0f) {
                    const float nextReal = zr * zr - zi * zi + real;
                    zi = 2.0f * zr * zi + imaginary;
                    zr = nextReal;
                    ++iterations;
                }
            }
            // Alternating escape bands give fine black-and-white contours.
            if (iterations < MAX_ITERATIONS && (iterations % 6) < 3)
                packed |= (uint8_t)(0x80U >> bit);
            real += step;
        }
        row[byte] = packed;
    }
}

static uint32_t pixelHash(uint32_t x, uint32_t y) {
    uint32_t v = x * 0x9E3779B1U + y * 0x85EBCA77U + 0xC2B2AE3DU;
    v ^= v >> 16;
    v *= 0x7FEB352DU;
    return v ^ (v >> 15);
}

static void renderEtchingRow(int y) {
    uint8_t *row = video.mono1Row(y);
    if (!row) return;
    const int moonX = WIDTH * 76 / 100;
    const int moonY = 78;
    const int moonRadius = 39;
    for (int byte = 0; byte < video.mono1Stride(); ++byte) {
        uint8_t packed = 0;
        for (int bit = 0; bit < 8; ++bit) {
            const int x = byte * 8 + bit;
            if (x >= WIDTH) break;
            const uint32_t noise = pixelHash((uint32_t)x, (uint32_t)y);
            bool black = false;
            const int dx = x - moonX, dy = y - moonY;
            const int moonDistance = dx * dx + dy * dy;
            if (moonDistance < moonRadius * moonRadius) {
                black = moonDistance > (moonRadius - 2) * (moonRadius - 2) ||
                    ((noise & 127U) < 2 && moonDistance < 980);
            } else if (y < farRidge[x]) {
                // Hairline cloud contours and sparse sky stipple.
                black = ((x < WIDTH * 58 / 100 && y == cloudLineA[x]) ||
                         (x > WIDTH * 30 / 100 && y == cloudLineB[x])) &&
                        ((noise & 15U) != 0);
                if ((noise & 8191U) == 7U) black = true;
            } else if (y < nearRidge[x]) {
                // Two engraving frequencies keep distant peaks legible.
                const int depth = y - farRidge[x];
                black = depth < 2 ||
                    (((x + 3 * y) % 11) < (depth > 20 ? 3 : 2)) ||
                    ((noise & 31U) == 0U);
            } else {
                const int depth = y - nearRidge[x];
                if (y < 230) {
                    black = depth < 2 ||
                        (((x - 2 * y) % 17 + 17) % 17 < (depth > 25 ? 5 : 3)) ||
                        ((noise & 15U) == 0U);
                } else {
                    // Water reflection: irregular one-pixel horizontal cuts.
                    black = ((y & 3) == 0 && (noise & 15U) < 11U) ||
                        ((noise & 31U) == 0U && y < 255);
                }
            }
            if (!black) packed |= (uint8_t)(0x80U >> bit);
        }
        row[byte] = packed;
    }
}

static void outlineRect(int x, int y, int w, int h) {
    strokeLine(x, y, x + w - 1, y, false);
    strokeLine(x, y + h - 1, x + w - 1, y + h - 1, false);
    strokeLine(x, y, x, y + h - 1, false);
    strokeLine(x + w - 1, y, x + w - 1, y + h - 1, false);
}

static void drawObservatory() {
    const int wingLeft = WIDTH * 24 / 100;
    const int wingRight = WIDTH * 76 / 100;
    const int mainLeft = WIDTH * 35 / 100;
    const int mainRight = WIDTH * 65 / 100;
    const int center = WIDTH / 2;
    video.mono1FillRect(wingLeft, 200, wingRight - wingLeft, 46, true);
    outlineRect(wingLeft, 200, wingRight - wingLeft, 46);
    video.mono1FillRect(mainLeft, 170, mainRight - mainLeft, 76, true);
    outlineRect(mainLeft, 170, mainRight - mainLeft, 76);
    strokeLine(mainLeft - 8, 169, center, 153, false);
    strokeLine(center, 153, mainRight + 8, 169, false);
    strokeLine(mainLeft - 8, 169, mainRight + 8, 169, false);
    for (int x = mainLeft + 9; x < mainRight - 6; x += max(8, WIDTH / 43)) {
        strokeLine(x, 172, x, 243, false);
        strokeLine(x + 2, 172, x + 2, 243, false);
    }
    strokeLine(wingLeft + 4, 208, wingRight - 4, 208, false);
    strokeLine(wingLeft + 4, 235, wingRight - 4, 235, false);
    for (int x = wingLeft + 10; x < wingRight - 10; x += max(12, WIDTH / 34)) {
        outlineRect(x, 213, 8, 17);
        strokeLine(x + 4, 213, x + 4, 229, false);
        strokeLine(x, 221, x + 7, 221, false);
    }
    // One-pixel masonry joints under the columns.
    for (int y = 237; y < 246; y += 4)
        for (int x = wingLeft + 2 + ((y & 4) ? 7 : 0);
             x < wingRight - 3; x += 18)
            strokeLine(x, y, x + 9, y, false);

    const int domeRadiusX = max(28, WIDTH * 7 / 100);
    const int domeRadiusY = 30;
    for (int dx = -domeRadiusX; dx <= domeRadiusX; ++dx) {
        const float fraction = (float)dx / domeRadiusX;
        const int top = 161 - (int)(domeRadiusY *
            sqrtf(max(0.0f, 1.0f - fraction * fraction)));
        video.mono1FillRect(center + dx, top, 1, 161 - top, true);
        video.mono1Pixel(center + dx, top, false);
        if ((dx + domeRadiusX) % max(5, WIDTH / 140) == 0)
            video.mono1Pixel(center + dx, top + 3, false);
    }
    strokeLine(center - domeRadiusX - 4, 161, center + domeRadiusX + 4,
               161, false, 2);
    strokeLine(center, 132, center, 115, false);
    strokeLine(center - 9, 123, center + 9, 123, false);
    for (int side = -1; side <= 1; side += 2) {
        const int towerX = center + side * WIDTH * 23 / 100;
        video.mono1FillRect(towerX - 18, 164, 36, 80, true);
        outlineRect(towerX - 18, 164, 36, 80);
        strokeLine(towerX - 23, 164, towerX, 147, false);
        strokeLine(towerX, 147, towerX + 23, 164, false);
        outlineRect(towerX - 5, 178, 10, 22);
        strokeLine(towerX - 5, 188, towerX + 4, 188, false);
        for (int y = 209; y < 239; y += 8)
            strokeLine(towerX - 12, y, towerX + 11, y, false);
    }
    // Dense etched conifers frame the architecture without a bitmap asset.
    for (int i = 0; i < 25; ++i) {
        const int left = i < 13;
        const int local = left ? i : i - 13;
        const int x = left ? WIDTH * (4 + 15 * local / 13) / 100
                           : WIDTH * (80 + 17 * local / 12) / 100;
        const int base = 220 + (int)(pixelHash(i, 19) % 26U);
        const int height = 20 + (int)(pixelHash(i, 83) % 34U);
        strokeLine(x, base, x, base - height, false);
        for (int branch = 5; branch < height; branch += 5) {
            const int spread = (height - branch) / 3 + 2;
            strokeLine(x, base - branch, x - spread, base - branch + 8, false);
            strokeLine(x, base - branch, x + spread, base - branch + 8, false);
        }
    }
}

static void drawLettering() {
    video.mono1FillRect(0, TOP_BAR, WIDTH, FOOTER_TOP - TOP_BAR, true);
    CMonoFont::draw(video, 26, 68, "ABCDEFGHIJKLM",
                    CMonoFont::Large, false, 1, 4);
    CMonoFont::draw(video, 26, 109, "NOPQRSTUVWXYZ",
                    CMonoFont::Large, false, 1, 4);
    CMonoFont::draw(video, 26, 151, "abcdefghijklm",
                    CMonoFont::Large, false, 6, 4);
    CMonoFont::draw(video, 26, 193, "nopqrstuvwxyz",
                    CMonoFont::Large, false, 5, 4);
    CMonoFont::draw(video, 26, 244, "0123456789",
                    CMonoFont::Large, false, 6, 4);
}

static void drawCaption() {
    video.mono1FillRect(0, 0, WIDTH, TOP_BAR, false);
    video.mono1FillRect(0, TOP_BAR - 1, WIDTH, 1, true);
    video.mono1FillRect(0, FOOTER_TOP, WIDTH, 1, true);
    video.mono1FillRect(0, FOOTER_TOP + 1, WIDTH,
                        HEIGHT - FOOTER_TOP - 1, false);
    CMonoFont::draw(video, 20, 23, "CITADELA",
                    CMonoFont::Small, true, 1, 3);
    char sceneLabel[48];
    snprintf(sceneLabel, sizeof(sceneLabel), "%s  /  %02u",
             SCENE_NAMES[scene], (unsigned)scene + 1);
    const int sceneWidth = CMonoFont::measure(sceneLabel, CMonoFont::Small, 1, 3);
    CMonoFont::draw(video, WIDTH - 20 - sceneWidth, 23, sceneLabel,
                    CMonoFont::Small, true, 1, 3);
    const char *hint = scene == FRACTAL ?
        "TAB MODE   ARROWS PAN   +/- ZOOM" :
        is3DScene() ?
        "TAB MODE   ROTATE   +/- ZOOM   ESC" :
        "TAB MODE   ESC EXIT   1 BIT/PIXEL";
    CMonoFont::draw(video, 20, HEIGHT - 9, hint,
                    CMonoFont::Small, true, 1, 3);
}

static void prepareCRCTable() {
    for (uint32_t index = 0; index < 256; ++index) {
        uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1U) ? 0xEDB88320U : 0U);
        crcTable[index] = value;
    }
}

static void reportFrame() {
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t whitePixels = 0;
    const int stride = video.mono1Stride();
    for (int y = 0; y < HEIGHT; ++y) {
        const uint8_t *row = video.mono1Row(y);
        for (int byte = 0; byte < stride; ++byte) {
            const uint8_t value = row[byte];
            whitePixels += __builtin_popcount((unsigned)value);
            crc = (crc >> 8) ^ crcTable[(crc ^ value) & 0xffU];
        }
    }
    displayedScene = scene;
    displayedCenterReal = centerReal;
    displayedCenterImaginary = centerImaginary;
    displayedViewHeight = viewHeight;
    displayedYaw = objectYaw;
    displayedPitch = objectPitch;
    displayedZoom = objectZoom;
    frameComplete = true;
    Serial.printf("MONO FRAME mode=%u name=%s complete in %lu ms crc32=%08lX white=%lu\n",
        (unsigned)scene, SCENE_NAMES[scene],
        (unsigned long)(millis() - renderStarted),
        (unsigned long)(crc ^ 0xFFFFFFFFU), (unsigned long)whitePixels);
    if (is3DScene())
        Serial.printf("MONO DELTA ok=%u changedBytes=%lu changedPixels=%lu\n",
            modelStats.ok ? 1U : 0U,
            (unsigned long)modelStats.changedBytes,
            (unsigned long)modelStats.changedPixels);
    drawCursor();
}

static void dumpFrame() {
    if (!frameComplete) {
        Serial.println("MONO DUMP WAIT: frame incomplete");
        return;
    }
    restoreCursor();
    const int stride = video.mono1Stride();
    const int bytes = stride * HEIGHT;
    Serial.printf("MONO DUMP START %d %d %d %d\n", WIDTH, HEIGHT, stride, bytes);
    for (int y = 0; y < HEIGHT; ++y)
        Serial.write(video.mono1Row(y), stride);
    Serial.print("\nMONO DUMP END\n");
    Serial.flush();
    drawCursor();
}

static bool handleMouseReport(const String &line) {
    if (!line.startsWith("MOUSE ")) return false;
    int x = mouseControllerX, y = mouseControllerY;
    int buttons = 0, dx = 0, dy = 0, wheel = 0;
    if (sscanf(line.c_str(), "MOUSE %d %d %d %d %d %d",
               &x, &y, &buttons, &dx, &dy, &wheel) < 3) return true;
    x = constrain(x, 0, 375);
    y = constrain(y, 0, 284);
    const int movedX = x - mouseControllerX;
    const int movedY = y - mouseControllerY;
    const bool leftDown = (buttons & 1) != 0;
    restoreCursor();
    mouseSeen = true;
    mouseControllerX = x;
    mouseControllerY = y;
    cursorX = x * (WIDTH - 1) / 375;
    cursorY = y * (HEIGHT - 1) / 284;
    const bool leftPressed = leftDown && !mouseLeftDown;
    // A click toggles orbit; subsequent motion works after button release.
    if (is3DScene() && leftPressed)
        mouseOrbitLatched = !mouseOrbitLatched;
    if (is3DScene() && mouseOrbitLatched && !leftPressed &&
        (movedX != 0 || movedY != 0)) {
        objectYaw += movedX * 0.012f;
        objectPitch = constrain(objectPitch + movedY * 0.012f, -1.4f, 1.4f);
        modelInputDirty = true;
    }
    mouseLeftDown = leftDown;
    if (!modelInputDirty) drawCursor();
    return true;
}

static void handleInput(String line, bool fromUSB) {
    line.trim();
    if (line == "Escape" || line == "esc") { returnToKernel(); return; }
    if (!videoReady) return;
    if (handleMouseReport(line)) return;
    if (line == "MONO INFO") {
        Serial.printf("MONO 1BIT %dx%d stride=%d frame=%d mode=%u name=%s free=%u DMA=%u isrMax=%lu budget=%lu over=%lu lines=%lu\n",
            video.xres, video.yres, video.mono1Stride(),
            video.mono1Stride() * video.yres, (unsigned)scene,
            SCENE_NAMES[scene], (unsigned)ESP.getFreeHeap(),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
            (unsigned long)video.mono1MaxRenderCycles(),
            (unsigned long)video.mono1RenderBudgetCycles(),
            (unsigned long)video.mono1OverBudgetCount(),
            (unsigned long)video.mono1RenderedLines());
        return;
    }
    if (line == "MONO DUMP" && fromUSB) { dumpFrame(); return; }
    if (line == "Tab" || line == "tab" || line == "TAB") {
        scene = (Scene)(((unsigned)scene + 1U) % SCENE_COUNT);
        modelInputDirty = false;
        mouseOrbitLatched = false;
        beginRender();
        return;
    }
    const float pan = viewHeight * 0.18f;
    if (scene == FRACTAL && (line == "LeftArrow" || line == "left"))
        centerReal -= pan * (4.0f / 3.0f);
    else if (scene == FRACTAL && (line == "RightArrow" || line == "right"))
        centerReal += pan * (4.0f / 3.0f);
    else if (scene == FRACTAL && (line == "UpArrow" || line == "up"))
        centerImaginary -= pan;
    else if (scene == FRACTAL && (line == "DownArrow" || line == "down"))
        centerImaginary += pan;
    else if (is3DScene() && (line == "LeftArrow" || line == "left"))
        objectYaw -= 0.18f;
    else if (is3DScene() && (line == "RightArrow" || line == "right"))
        objectYaw += 0.18f;
    else if (is3DScene() && (line == "UpArrow" || line == "up"))
        objectPitch = max(-1.4f, objectPitch - 0.18f);
    else if (is3DScene() && (line == "DownArrow" || line == "down"))
        objectPitch = min(1.4f, objectPitch + 0.18f);
    else if (scene == FRACTAL && (line == "+" || line == "=" || line == "PageUp"))
        viewHeight *= 0.6f;
    else if (scene == FRACTAL && (line == "-" || line == "PageDown"))
        viewHeight *= 1.6f;
    else if (is3DScene() && (line == "+" || line == "=" || line == "PageUp"))
        objectZoom = min(1.55f, objectZoom * 1.12f);
    else if (is3DScene() && (line == "-" || line == "PageDown"))
        objectZoom = max(0.72f, objectZoom / 1.12f);
    else if (line == "r" || line == "R") {
        centerReal = -0.65f;
        centerImaginary = 0.0f;
        viewHeight = 2.35f;
        objectYaw = 0.45f;
        objectPitch = -0.25f;
        objectZoom = 1.0f;
    } else return;
    modelInputDirty = false;
    beginRender();
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    prepareCRCTable();
    spiffsReady = SPIFFS.begin(false);
    if (readBootState() == "trueKernel") {
        writeBootState("false");
        flashKernel();
        return;
    }
    Serial.printf("MONO before init: heap=%u DMA=%u\n",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    videoReady = video.init(CompMode::MODEPALMono1Ultra7x, VIDEO_PIN, true,
        CompositeColorDAC::PixelStorage::Mono1);
    if (!videoReady) {
        Serial.println("MONO 1BIT video allocation failed");
        return;
    }
    controllerVideo.appVideoActive(8, 35);
    Serial.printf("MONO READY %dx%d stride=%d frame=%d heap=%u DMA=%u\n",
        video.xres, video.yres, video.mono1Stride(),
        video.mono1Stride() * video.yres, (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    beginRender();
}

void loop() {
    String line;
    while (controllerInput.poll(Serial1, line)) handleInput(line, false);
    while (usbInput.poll(Serial, line)) handleInput(line, true);
    if (modelInputDirty) {
        modelInputDirty = false;
        beginRender();
    }
    if (videoReady && nextRow < HEIGHT) {
        if (scene == LETTERING) {
            drawLettering();
            nextRow = HEIGHT;
        } else if (is3DScene()) {
            auto row = [&](int y) { return video.mono1Row(y); };
            modelStats = scene == KNOT ?
                CMonoArt3D::renderIncremental(row,
                    WIDTH, TOP_BAR, FOOTER_TOP - 1,
                    objectYaw, objectPitch, objectZoom) :
                CMonoArt3D::renderSecondIncremental(row,
                    WIDTH, TOP_BAR, FOOTER_TOP - 1,
                    objectYaw, objectPitch, objectZoom);
            if (!modelStats.ok) {
                video.mono1FillRect(0, TOP_BAR, WIDTH,
                                    FOOTER_TOP - TOP_BAR, false);
                auto plot = [&](int x, int y, bool white) {
                    video.mono1Pixel(x, y, white);
                };
                if (scene == KNOT)
                    CMonoArt3D::render(plot, WIDTH, TOP_BAR, FOOTER_TOP - 1,
                                       objectYaw, objectPitch, objectZoom);
                else
                    CMonoArt3D::renderSecond(plot, WIDTH, TOP_BAR,
                                             FOOTER_TOP - 1,
                                             objectYaw, objectPitch, objectZoom);
            }
            nextRow = HEIGHT;
        } else {
            switch (scene) {
            case FRACTAL: renderMandelbrotRow(nextRow); break;
            case ETCHING: renderEtchingRow(nextRow); break;
            default: break;
            }
            ++nextRow;
        }
        if (nextRow == HEIGHT) {
            if (scene == ETCHING) drawObservatory();
            if (!captionReusable) drawCaption();
            if (is3DScene()) modelCanvasValid = true;
            reportFrame();
        }
        yield();
    } else {
        delay(1);
    }
}
