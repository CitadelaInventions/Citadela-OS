#include <Arduino.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"
#include "../../../System/Libraries/CitadelaStorage.h"

static constexpr int WIDTH = 940;
static constexpr int HEIGHT = 288;
static constexpr int VIDEO_PIN = 25;
static constexpr int MAX_ITERATIONS = 72;
static constexpr const char *BOOT_STATE_PATH = "/evil.txt";

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
    controllerVideo.prepare("Returning to Citadela OS", 0);
    if (videoReady) video.releaseVideoMemory();
    videoReady = false;
    pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(50);
    ESP.restart();
}

static void beginRender() {
    if (!videoReady) return;
    video.mono1Clear(false);
    nextRow = 0;
    renderStarted = millis();
}

static void renderRow(int y) {
    uint8_t *row = video.mono1Row(y);
    if (!row) return;
    const float imaginary = centerImaginary +
        ((float)y + 0.5f - HEIGHT * 0.5f) * viewHeight / HEIGHT;
    const float step = viewHeight * (4.0f / 3.0f) / WIDTH;
    float real = centerReal - step * (WIDTH * 0.5f - 0.5f);
    for (int byte = 0; byte < video.mono1Stride(); ++byte) {
        uint8_t packed = 0;
        for (int bit = 0; bit < 8; ++bit) {
            int x = byte * 8 + bit;
            if (x >= WIDTH) break;
            float zr = 0.0f, zi = 0.0f;
            int iterations = 0;
            while (iterations < MAX_ITERATIONS && zr * zr + zi * zi < 64.0f) {
                float nextReal = zr * zr - zi * zi + real;
                zi = 2.0f * zr * zi + imaginary;
                zr = nextReal;
                ++iterations;
            }
            // Alternating escape bands give fine black-and-white contours.
            if (iterations < MAX_ITERATIONS && (iterations % 6) < 3)
                packed |= (uint8_t)(0x80U >> bit);
            real += step;
        }
        row[byte] = packed;
    }
}

static void drawScaledText(int x, int y, const char *message, const Font &font,
                           int scaleX, int scaleY) {
    for (const char *ch = message; *ch; ++ch) {
        if (font.valid(*ch)) {
            const uint8_t *pixels = font.pixels +
                (int)(*ch - font.firstChar) * font.charWidth * font.charHeight;
            for (int row = 0; row < font.charHeight; ++row)
                for (int column = 0; column < font.charWidth; ++column)
                    if (pixels[row * font.charWidth + column])
                        video.mono1FillRect(x + column * scaleX, y + row * scaleY,
                                            scaleX, scaleY, true);
        }
        x += font.charWidth * scaleX;
    }
}

static void drawCaption() {
    video.mono1FillRect(0, 0, WIDTH, 30, false);
    video.mono1FillRect(0, 30, WIDTH, 1, true);
    video.mono1FillRect(0, 265, WIDTH, 1, true);
    video.mono1FillRect(0, 266, WIDTH, 22, false);
    drawScaledText(16, 7, "CITADELA  /  ONE-BIT MANDELBROT CONTOURS",
                   Font8x8, 2, 2);
    drawScaledText(16, 270,
                   "940 x 288  |  ARROWS PAN  |  +/- ZOOM  |  R RESET  |  ESC EXIT",
                   Font6x8, 2, 2);
}

static void handleInput(String line) {
    line.trim();
    if (line == "Escape" || line == "esc") { returnToKernel(); return; }
    if (!videoReady) return;
    const float pan = viewHeight * 0.18f;
    if (line == "LeftArrow" || line == "left") centerReal -= pan * (4.0f / 3.0f);
    else if (line == "RightArrow" || line == "right") centerReal += pan * (4.0f / 3.0f);
    else if (line == "UpArrow" || line == "up") centerImaginary -= pan;
    else if (line == "DownArrow" || line == "down") centerImaginary += pan;
    else if (line == "+" || line == "=" || line == "PageUp") viewHeight *= 0.6f;
    else if (line == "-" || line == "PageDown") viewHeight *= 1.6f;
    else if (line == "r" || line == "R") {
        centerReal = -0.65f;
        centerImaginary = 0.0f;
        viewHeight = 2.35f;
    } else if (line == "MONO INFO") {
        Serial.printf("MONO 1BIT %dx%d stride=%d frame=%d free=%u DMA=%u\n",
            video.xres, video.yres, video.mono1Stride(),
            video.mono1Stride() * video.yres, (unsigned)ESP.getFreeHeap(),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
        return;
    } else return;
    beginRender();
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    spiffsReady = SPIFFS.begin(false);
    if (readBootState() == "trueKernel") {
        writeBootState("false");
        flashKernel();
        return;
    }
    Serial.printf("MONO before init: heap=%u DMA=%u\n",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    videoReady = video.init(CompMode::MODEPALMono1Super, VIDEO_PIN, true,
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
    while (controllerInput.poll(Serial1, line)) handleInput(line);
    while (usbInput.poll(Serial, line)) handleInput(line);
    if (videoReady && nextRow < HEIGHT) {
        renderRow(nextRow++);
        if (nextRow == HEIGHT) {
            drawCaption();
            Serial.printf("MONO FRAME complete in %lu ms\n",
                (unsigned long)(millis() - renderStarted));
        }
    }
    delay(1);
}
