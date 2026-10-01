#include <Arduino.h>
#include <ESP32Video.h>
#include <JPEGDEC.h>
#include <Ressources/Font6x8.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include "../../../System/Libraries/CitadelaSerialCommands.h"
#include "../../../System/Libraries/CitadelaStorage.h"
#include "CQualDisMp4.h"

// CImage's PAL4x low-memory colour mode: one indexed 376 x 192 framebuffer.
static constexpr int SCREEN_W = 376;
static constexpr int SCREEN_H = 192;
static constexpr int VIDEO_PIN = 25;
static constexpr int MAX_VIDEOS = 32;
static constexpr int STRIPE_H = 16;
static constexpr int THUMB_W = 80;
static constexpr int THUMB_H = 45;
static constexpr const char *VIDEO_DIR = "/Videos";
static constexpr const char *BOOT_STATE_PATH = "/evil.txt";

static CompositeColorDAC video;
// JPEGDEC owns a large working structure. Reserving it before video.init()
// avoids requiring one contiguous heap block after the PAL framebuffer starts.
static JPEGDEC decoder;
static Citadela::LineReader controllerInput(128);
static Citadela::LineReader usbInput(128);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);
static bool videoReady = false;
static bool spiffsReady = false;
static bool sdReady = false;

enum Page : uint8_t { BROWSER, OPENING, PLAYING, STOPPING };
static Page page = BROWSER;

struct VideoItem {
    char name[72];
    char path[104];
    char thumbPath[104];
    bool hasPreview;
};
static VideoItem videos[MAX_VIDEOS];
static int videoCount = 0;
static int selected = 0;
static char browserStatus[80] = "Select a prepared video";
static uint8_t thumbnail[THUMB_W * THUMB_H];

// The PAL interrupt runs on core 1, where setup() initializes the display.
// Core 0 decodes JPEG MCUs into two bounded stripes. Core 1 commits only
// palette bytes that differ from the displayed framebuffer.
struct StripeBuffer { uint8_t index[SCREEN_W * STRIPE_H]; };
static StripeBuffer stripes[2];
static QueueHandle_t freeStripes = nullptr;
static QueueHandle_t readyEvents = nullptr;
static TaskHandle_t decodeTaskHandle = nullptr;
static volatile bool stopRequested = false;
static volatile bool decodeTaskDone = true;
static char playPath[104];

enum EventKind : uint8_t {
    EVENT_OPEN, EVENT_FRAME_START, EVENT_STRIPE, EVENT_FRAME_END,
    EVENT_FINISHED, EVENT_ERROR
};
struct PlayerEvent {
    EventKind kind;
    uint8_t stripe;
    uint16_t y;
    uint32_t frame;
    uint32_t ptsMs;
    char message[64];
};
static bool pendingEventValid = false;
static PlayerEvent pendingEvent;
static uint32_t playbackStartedMs = 0;
static uint32_t playbackDurationMs = 0;
static uint32_t pausedAtMs = 0;
static uint32_t frameChangedBytes = 0;
static uint32_t totalFrames = 0;
static uint32_t expectedFrames = 0;
static volatile bool paused = false;
static bool pauseAfterFrame = false;
static bool frameInProgress = false;

// The cursor is kept out of the moving picture. The browser restores exact
// underlying palette values before a redraw or a page transition.
static bool mouseSeen = false;
static bool cursorShown = false;
static bool mouseLeftDown = false;
static int mouseX = SCREEN_W / 2;
static int mouseY = SCREEN_H / 2;
static uint16_t cursorUnder[25];

static uint16_t &rawPixel(int x, int y) {
    return video.backBuffer[video.graphics_swy(y)][video.graphics_swx(x)];
}

static void restoreCursor() {
    if (!cursorShown || !videoReady) return;
    int at = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx, ++at) {
            const int x = mouseX + dx, y = mouseY + dy;
            if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H)
                rawPixel(x, y) = cursorUnder[at];
        }
    }
    cursorShown = false;
}

static void drawCursor() {
    if (!videoReady || page != BROWSER || !mouseSeen || cursorShown) return;
    int at = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx, ++at) {
            const int x = mouseX + dx, y = mouseY + dy;
            if ((unsigned)x >= SCREEN_W || (unsigned)y >= SCREEN_H) continue;
            cursorUnder[at] = rawPixel(x, y);
            if (dx == 0 || dy == 0)
                rawPixel(x, y) = (uint16_t)31 << 8;
        }
    }
    cursorShown = true;
}

static bool writeBootState(const char *state) {
    if (!spiffsReady) return false;
    SPIFFS.remove(BOOT_STATE_PATH);
    File file = SPIFFS.open(BOOT_STATE_PATH, FILE_WRITE);
    if (!file) return false;
    const bool ok = file.println(state) > 0;
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
        Serial.println("CQUALDIS RETURN FAILED: SD mount");
        return;
    }
    File image = SD.open("/System/kernel.bin", FILE_READ);
    const size_t bytes = image ? image.size() : 0;
    if (!image || bytes < 32768 || image.read() != 0xE9 || !image.seek(0)) {
        if (image) image.close();
        SD.end();
        Serial.println("CQUALDIS RETURN FAILED: invalid kernel image");
        return;
    }
    controllerVideo.appFlashStart(bytes, "Returning to Citadela OS");
    if (!Update.begin(bytes, U_FLASH)) {
        image.close();
        SD.end();
        Serial.printf("CQUALDIS RETURN FAILED: %s\n", Update.errorString());
        return;
    }
    uint8_t buffer[2048];
    size_t written = 0;
    while (written < bytes) {
        const int count = image.read(buffer, min(sizeof(buffer), bytes - written));
        if (count <= 0 || Update.write(buffer, count) != (size_t)count) {
            Update.abort();
            image.close();
            SD.end();
            Serial.println("CQUALDIS RETURN FAILED: write");
            return;
        }
        written += count;
        controllerVideo.appFlashWrite(written);
        yield();
    }
    image.close();
    if (!Update.end(true)) {
        SD.end();
        Serial.printf("CQUALDIS RETURN FAILED: %s\n", Update.errorString());
        return;
    }
    SD.end();
    Serial1.println("VDSTOP");
    Serial1.flush();
    ESP.restart();
}

static void returnToKernel() {
    if (!writeBootState("trueKernel")) {
        Serial.println("CQUALDIS EXIT FAILED: boot marker unavailable");
        return;
    }
    restoreCursor();
    controllerVideo.prepare("Returning to Citadela OS", 0);
    if (videoReady) video.i2sStop();
    videoReady = false;
    pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(50);
    ESP.restart();
}

static bool endsWithMp4(const char *name) {
    const size_t length = strlen(name);
    if (length < 5) return false;
    const char *ext = name + length - 4;
    return ext[0] == '.' && (ext[1] == 'm' || ext[1] == 'M') &&
           (ext[2] == 'p' || ext[2] == 'P') && ext[3] == '4';
}

static void scanVideos() {
    videoCount = 0;
    selected = 0;
    if (!sdReady) {
        snprintf(browserStatus, sizeof(browserStatus), "SD card unavailable");
        return;
    }
    File directory = SD.open(VIDEO_DIR, FILE_READ);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        snprintf(browserStatus, sizeof(browserStatus), "Create /Videos on the SD card");
        return;
    }
    while (videoCount < MAX_VIDEOS) {
        File entry = directory.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            const char *fullName = entry.name();
            const char *base = strrchr(fullName, '/');
            base = base ? base + 1 : fullName;
            if (endsWithMp4(base)) {
                VideoItem &item = videos[videoCount];
                const int n = snprintf(item.path, sizeof(item.path),
                                       "%s/%s", VIDEO_DIR, base);
                if (n > 0 && n < (int)sizeof(item.path) &&
                    strlen(base) < sizeof(item.name)) {
                    strlcpy(item.name, base, sizeof(item.name));
                    strlcpy(item.thumbPath, item.path, sizeof(item.thumbPath));
                    char *extension = strrchr(item.thumbPath, '.');
                    if (extension && (size_t)(extension - item.thumbPath) + 4 <
                                         sizeof(item.thumbPath)) {
                        strcpy(extension, ".cth");
                        item.hasPreview = SD.exists(item.thumbPath);
                        ++videoCount;
                    }
                }
            }
        }
        entry.close();
    }
    directory.close();
    snprintf(browserStatus, sizeof(browserStatus), videoCount ?
             "%d video%s found in /Videos" : "No MP4 files in /Videos",
             videoCount, videoCount == 1 ? "" : "s");
    Serial.printf("CQUALDIS SCAN videos=%d heap=%u\n",
                  videoCount, (unsigned)ESP.getFreeHeap());
}

static uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
    return video.RGB(r, g, b);
}

static void textAt(int x, int y, const char *label, uint32_t foreground,
                   uint32_t background) {
    video.setFont(Font6x8);
    video.setTextColor(foreground, background);
    video.setCursor(x, y);
    video.print(label);
}

static void shortTitle(const char *name, char *out, size_t capacity) {
    strlcpy(out, name, capacity);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
    const size_t limit = 29;
    if (strlen(out) > limit) {
        out[limit - 2] = '.';
        out[limit - 1] = '.';
        out[limit] = 0;
    }
}

static bool drawPreview(const VideoItem &item, int x, int y,
                        int width, int height) {
    if (!item.hasPreview) return false;
    File file = SD.open(item.thumbPath, FILE_READ);
    if (!file) return false;
    uint8_t header[8];
    const bool valid = file.read(header, sizeof(header)) == sizeof(header) &&
                       memcmp(header, "CQTH", 4) == 0 &&
                       ((unsigned)header[4] | ((unsigned)header[5] << 8)) == THUMB_W &&
                       ((unsigned)header[6] | ((unsigned)header[7] << 8)) == THUMB_H &&
                       file.read(thumbnail, sizeof(thumbnail)) == sizeof(thumbnail);
    file.close();
    if (!valid) return false;
    for (int dy = 0; dy < height; ++dy) {
        const int sourceY = dy * THUMB_H / height;
        for (int dx = 0; dx < width; ++dx) {
            const int sourceX = dx * THUMB_W / width;
            rawPixel(x + dx, y + dy) =
                (uint16_t)thumbnail[sourceY * THUMB_W + sourceX] << 8;
        }
    }
    return true;
}

static void drawBrowser() {
    if (!videoReady) return;
    restoreCursor();
    const uint32_t background = color(7, 14, 24);
    const uint32_t top = color(14, 27, 40);
    const uint32_t panel = color(19, 34, 46);
    const uint32_t selectedFill = color(27, 54, 64);
    const uint32_t accent = color(63, 222, 205);
    const uint32_t white = color(246, 249, 250);
    const uint32_t muted = color(153, 174, 184);
    video.fillRect(0, 0, SCREEN_W, SCREEN_H, background);
    video.fillRect(0, 0, SCREEN_W, 22, top);
    video.fillRect(0, 21, SCREEN_W, 1, accent);
    textAt(10, 7, "CQUALDIS", white, top);
    textAt(270, 7, "VIDEO PLAYER", accent, top);

    if (videoCount == 0) {
        textAt(27, 78, "NO VIDEOS READY", white, background);
        textAt(27, 95, browserStatus, muted, background);
        textAt(27, 112, "PREPARE MP4S ON YOUR COMPUTER", accent, background);
    } else {
        int first = selected - 1;
        if (first < 0) first = 0;
        if (first > videoCount - 3) first = max(0, videoCount - 3);
        for (int card = 0; card < 3; ++card) {
            const int index = first + card;
            if (index >= videoCount) break;
            const int y = 27 + card * 47;
            const bool active = index == selected;
            const uint32_t fill = active ? selectedFill : panel;
            video.fillRect(8, y, 360, 43, fill);
            video.rect(8, y, 360, 43, active ? accent : color(48, 69, 78));
            video.fillRect(14, y + 4, 64, 35, color(2, 8, 13));
            const bool previewValid = drawPreview(videos[index], 14, y + 4, 64, 35);
            if (!previewValid)
                textAt(34, y + 18, "MP4", muted, color(2, 8, 13));
            char title[72];
            shortTitle(videos[index].name, title, sizeof(title));
            textAt(89, y + 9, title, active ? white : muted, fill);
            textAt(89, y + 26, previewValid ?
                   "PREPARED  /  PRESS ENTER" : "PREVIEW UNAVAILABLE",
                   active ? accent : muted, fill);
        }
    }
    video.fillRect(0, 170, SCREEN_W, 22, top);
    video.fillRect(0, 170, SCREEN_W, 1, color(49, 74, 83));
    textAt(10, 178, "UP/DOWN  SELECT   ENTER  PLAY", white, top);
    textAt(10, 185, "ESC  KERNEL  /  R  RESCAN", muted, top);
    drawCursor();
}

static void drawOpening() {
    restoreCursor();
    video.fillRect(0, 0, SCREEN_W, SCREEN_H, color(5, 12, 20));
    textAt(20, 55, "CQUALDIS", color(77, 223, 205), color(5, 12, 20));
    textAt(20, 82, "OPENING VIDEO...", color(245, 249, 250), color(5, 12, 20));
    if (videoCount > 0) {
        char title[72];
        shortTitle(videos[selected].name, title, sizeof(title));
        textAt(20, 102, title,
               color(153, 174, 184), color(5, 12, 20));
    }
}

static bool uploadNameValid(const char *name) {
    const size_t length = strlen(name);
    if (length < 5 || length > 63 || name[0] == '.') return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return false;
    }
    const char *extension = strrchr(name, '.');
    return extension && (!strcasecmp(extension, ".mp4") ||
                         !strcasecmp(extension, ".cth"));
}

static uint32_t updateCRC32(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320UL : 0UL);
    }
    return crc;
}

static void receiveFileFromUSB(const String &line) {
    char name[72] = {};
    unsigned long bytes = 0;
    unsigned int expectedCRC = 0;
    char extra = 0;
    if (sscanf(line.c_str(), "CQUALDIS PUT %71s %lu %x %c",
               name, &bytes, &expectedCRC, &extra) != 3 ||
        !uploadNameValid(name) || bytes == 0 || bytes > 0x7fffffffUL) {
        Serial.println("CQUALDIS PUT_ERROR BAD_REQUEST");
        return;
    }
    if (page != BROWSER) {
        Serial.println("CQUALDIS PUT_ERROR BUSY");
        return;
    }
    if (!sdReady) sdReady = mountSD();
    if (!sdReady || (!SD.exists(VIDEO_DIR) && !SD.mkdir(VIDEO_DIR))) {
        Serial.println("CQUALDIS PUT_ERROR SD_UNAVAILABLE");
        return;
    }
    char destination[104];
    snprintf(destination, sizeof(destination), "%s/%s", VIDEO_DIR, name);
    if (SD.exists(destination)) {
        Serial.println("CQUALDIS PUT_ERROR EXISTS");
        return;
    }
    const char *temporary = "/Videos/.cqualdis-upload.part";
    SD.remove(temporary);
    File output = SD.open(temporary, FILE_WRITE);
    if (!output) {
        Serial.println("CQUALDIS PUT_ERROR SD_WRITE");
        return;
    }
    Serial.setTimeout(5000);
    Serial.println("CQUALDIS PUT_READY 512");
    uint8_t chunk[512];
    uint32_t written = 0;
    uint32_t crc = 0xffffffffUL;
    bool okay = true;
    while (written < bytes) {
        const size_t count = min((size_t)sizeof(chunk), (size_t)(bytes - written));
        if (Serial.readBytes(chunk, count) != count ||
            output.write(chunk, count) != count) {
            okay = false;
            break;
        }
        crc = updateCRC32(crc, chunk, count);
        written += count;
        Serial.printf("CQUALDIS ACK %lu\n", (unsigned long)written);
        yield();
    }
    output.flush();
    output.close();
    if (!okay || written != bytes || (crc ^ 0xffffffffUL) != expectedCRC) {
        SD.remove(temporary);
        Serial.println(okay ? "CQUALDIS PUT_ERROR CRC" :
                              "CQUALDIS PUT_ERROR TRANSFER");
        return;
    }
    if (!SD.rename(temporary, destination)) {
        SD.remove(temporary);
        Serial.println("CQUALDIS PUT_ERROR SD_RENAME");
        return;
    }
    Serial.printf("CQUALDIS PUT_DONE %s %lu %08lX\n", name, bytes,
                  (unsigned long)(crc ^ 0xffffffffUL));
    scanVideos();
    snprintf(browserStatus, sizeof(browserStatus), "Uploaded %s", name);
    drawBrowser();
}

static void deleteFileFromUSB(const String &line) {
    char name[72] = {};
    char extra = 0;
    if (sscanf(line.c_str(), "CQUALDIS DELETE %71s %c", name, &extra) != 1 ||
        !uploadNameValid(name) || page != BROWSER || !sdReady) {
        Serial.println("CQUALDIS DELETE_ERROR BAD_REQUEST");
        return;
    }
    char path[104];
    snprintf(path, sizeof(path), "%s/%s", VIDEO_DIR, name);
    if (!SD.exists(path) || !SD.remove(path)) {
        Serial.println("CQUALDIS DELETE_ERROR NOT_FOUND");
        return;
    }
    Serial.printf("CQUALDIS DELETED %s\n", name);
    scanVideos();
    drawBrowser();
}

static bool sendEvent(const PlayerEvent &event) {
    while (!stopRequested) {
        if (xQueueSend(readyEvents, &event, pdMS_TO_TICKS(20)) == pdTRUE)
            return true;
    }
    return false;
}

static void sendError(const char *message) {
    PlayerEvent event = {};
    event.kind = EVENT_ERROR;
    strlcpy(event.message, message, sizeof(event.message));
    // The UI continues draining events while stopping.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (xQueueSend(readyEvents, &event, pdMS_TO_TICKS(20)) == pdTRUE)
            break;
    }
}

struct SampleWindow {
    File *file;
    uint32_t offset;
    uint32_t size;
    uint32_t position;
};
static SampleWindow jpegWindow = {};
static int activeStripe = -1;
static int activeStripeY = 0;
static uint32_t decodingFrame = 0;

static void jpegClose(void *) {}

static int32_t jpegRead(JPEGFILE *, uint8_t *buffer, int32_t length) {
    if (!jpegWindow.file || length <= 0 || jpegWindow.position >= jpegWindow.size)
        return 0;
    const uint32_t available = jpegWindow.size - jpegWindow.position;
    if ((uint32_t)length > available) length = available;
    if (!jpegWindow.file->seek(jpegWindow.offset + jpegWindow.position)) return 0;
    const int result = jpegWindow.file->read(buffer, length);
    if (result > 0) jpegWindow.position += result;
    return result;
}

static int32_t jpegSeek(JPEGFILE *, int32_t position) {
    if (position < 0 || (uint32_t)position > jpegWindow.size) return 0;
    jpegWindow.position = position;
    return 1;
}

static bool beginStripe(int y) {
    int slot;
    while (!stopRequested) {
        if (xQueueReceive(freeStripes, &slot, pdMS_TO_TICKS(20)) == pdTRUE) {
            activeStripe = slot;
            activeStripeY = y;
            memset(stripes[slot].index, 0, sizeof(stripes[slot].index));
            return true;
        }
    }
    return false;
}

static bool flushStripe() {
    if (activeStripe < 0) return true;
    PlayerEvent event = {};
    event.kind = EVENT_STRIPE;
    event.stripe = activeStripe;
    event.y = activeStripeY;
    event.frame = decodingFrame;
    activeStripe = -1;
    if (sendEvent(event)) return true;
    int slot = event.stripe;
    xQueueSend(freeStripes, &slot, 0);
    return false;
}

static uint8_t indexRGB565(uint16_t pixel) {
    const int r = ((pixel >> 11) & 31) * 255 / 31;
    const int g = ((pixel >> 5) & 63) * 255 / 63;
    const int b = (pixel & 31) * 255 / 31;
    const int maximum = max(r, max(g, b));
    const int minimum = min(r, min(g, b));
    if (maximum - minimum <= 6) {
        const int gray = (19595L * r + 38470L * g + 7471L * b + 0x8000L) >> 16;
        return (uint8_t)((gray * 31 + 127) / 255);
    }
    const int ri = (r * 6 + 127) / 255;
    const int gi = (g * 7 + 127) / 255;
    const int bi = (b * 3 + 127) / 255;
    return (uint8_t)(32 + ((ri * 8 + gi) * 4 + bi));
}

static int jpegDraw(JPEGDRAW *draw) {
    if (stopRequested || !draw) return 0;
    const uint16_t *pixels = draw->pPixels;
    for (int row = 0; row < draw->iHeight; ++row) {
        const int y = draw->y + row;
        if ((unsigned)y >= SCREEN_H) continue;
        while (y >= activeStripeY + STRIPE_H) {
            if (!flushStripe() || !beginStripe(activeStripeY + STRIPE_H))
                return 0;
        }
        for (int col = 0; col < draw->iWidth; ++col) {
            const int x = draw->x + col;
            if ((unsigned)x >= SCREEN_W) continue;
            stripes[activeStripe].index[(y - activeStripeY) * SCREEN_W + x] =
                indexRGB565(pixels[row * draw->iWidth + col]);
        }
    }
    return 1;
}

static void decodeTask(void *) {
    File movie = SD.open(playPath, FILE_READ);
    if (!movie) {
        sendError("Cannot open video on SD");
        decodeTaskDone = true;
        vTaskDelete(nullptr);
        return;
    }
    CQualDisMp4::Reader parser;
    CQualDisMp4::Metadata metadata = {};
    if (!parser.open(movie, metadata)) {
        sendError(parser.errorText());
        movie.close();
        decodeTaskDone = true;
        vTaskDelete(nullptr);
        return;
    }
    if (metadata.width != SCREEN_W || metadata.height != SCREEN_H) {
        sendError("Prepare video at 376 x 192");
        movie.close();
        decodeTaskDone = true;
        vTaskDelete(nullptr);
        return;
    }
    PlayerEvent opened = {};
    opened.kind = EVENT_OPEN;
    opened.frame = metadata.frameCount;
    opened.ptsMs = metadata.durationMs;
    if (!sendEvent(opened)) {
        movie.close();
        decodeTaskDone = true;
        vTaskDelete(nullptr);
        return;
    }
    for (uint32_t frame = 0; frame < metadata.frameCount && !stopRequested; ++frame) {
        CQualDisMp4::Sample sample = {};
        if (!parser.nextSample(movie, sample)) {
            sendError(parser.errorText());
            break;
        }
        PlayerEvent start = {};
        start.kind = EVENT_FRAME_START;
        start.frame = frame;
        start.ptsMs = sample.ptsMs;
        if (!sendEvent(start)) break;
        jpegWindow = {&movie, sample.offset, sample.size, 0};
        decodingFrame = frame;
        activeStripe = -1;
        activeStripeY = 0;
        if (!beginStripe(0)) break;
        const bool openedJpeg = decoder.open(&jpegWindow, sample.size,
            jpegClose, jpegRead, jpegSeek, jpegDraw);
        bool decoded = false;
        if (openedJpeg && decoder.getWidth() == SCREEN_W &&
            decoder.getHeight() == SCREEN_H &&
            decoder.getJPEGType() != JPEG_MODE_PROGRESSIVE) {
            decoder.setPixelType(RGB565_LITTLE_ENDIAN);
            decoded = decoder.decode(0, 0, 0);
        }
        if (openedJpeg) decoder.close();
        if (!decoded || stopRequested) {
            if (activeStripe >= 0) {
                int slot = activeStripe;
                activeStripe = -1;
                xQueueSend(freeStripes, &slot, 0);
            }
            if (!stopRequested) sendError("JPEG frame decode failed");
            break;
        }
        if (!flushStripe()) break;
        PlayerEvent end = {};
        end.kind = EVENT_FRAME_END;
        end.frame = frame;
        if (!sendEvent(end)) break;
    }
    movie.close();
    if (!stopRequested) {
        PlayerEvent done = {};
        done.kind = EVENT_FINISHED;
        sendEvent(done);
    }
    decodeTaskDone = true;
    vTaskDelete(nullptr);
}

static void startPlayback() {
    if (videoCount == 0 || !sdReady || !decodeTaskDone) return;
    if (!freeStripes || !readyEvents) {
        snprintf(browserStatus, sizeof(browserStatus), "Playback queues unavailable");
        drawBrowser();
        return;
    }
    strlcpy(playPath, videos[selected].path, sizeof(playPath));
    xQueueReset(freeStripes);
    xQueueReset(readyEvents);
    for (int slot = 0; slot < 2; ++slot) xQueueSend(freeStripes, &slot, 0);
    pendingEventValid = false;
    stopRequested = false;
    decodeTaskDone = false;
    paused = false;
    pauseAfterFrame = false;
    frameInProgress = false;
    totalFrames = 0;
    expectedFrames = 0;
    playbackDurationMs = 0;
    page = OPENING;
    drawOpening();
    if (xTaskCreatePinnedToCore(decodeTask, "CQVideoDecode", 8192, nullptr,
                                1, &decodeTaskHandle, 0) != pdPASS) {
        decodeTaskDone = true;
        decodeTaskHandle = nullptr;
        page = BROWSER;
        snprintf(browserStatus, sizeof(browserStatus), "Decoder task memory unavailable");
        drawBrowser();
    }
}

static void stopPlayback() {
    if (page == BROWSER) return;
    stopRequested = true;
    paused = false;
    pauseAfterFrame = false;
    page = STOPPING;
    snprintf(browserStatus, sizeof(browserStatus), "Playback stopped");
}

static uint32_t commitStripe(uint8_t slot, int firstY) {
    if (slot >= 2 || firstY < 0 || firstY + STRIPE_H > SCREEN_H)
        return 0;
    uint32_t changed = 0;
    for (int dy = 0; dy < STRIPE_H; ++dy) {
        const int y = firstY + dy;
        auto *row = video.backBuffer[video.graphics_swy(y)];
        const uint8_t *source = stripes[slot].index + dy * SCREEN_W;
        for (int x = 0; x < SCREEN_W; ++x) {
            uint16_t &pixel = row[video.graphics_swx(x)];
            const uint8_t after = source[x];
            if ((uint8_t)(pixel >> 8) != after) {
                pixel = (uint16_t)after << 8;
                ++changed;
            }
        }
    }
    return changed;
}

static void finishPlayback(const char *message) {
    page = BROWSER;
    decodeTaskHandle = nullptr;
    pendingEventValid = false;
    if (message != browserStatus)
        snprintf(browserStatus, sizeof(browserStatus), "%s", message);
    drawBrowser();
}

static void processPlayback() {
    if (page == BROWSER) return;
    if (page == STOPPING) {
        PlayerEvent event;
        while (xQueueReceive(readyEvents, &event, 0) == pdTRUE) {
            if (event.kind == EVENT_STRIPE) {
                int slot = event.stripe;
                xQueueSend(freeStripes, &slot, 0);
            }
        }
        if (decodeTaskDone) finishPlayback(browserStatus);
        return;
    }
    for (int processed = 0; processed < 6; ++processed) {
        PlayerEvent event;
        if (pendingEventValid) {
            event = pendingEvent;
            pendingEventValid = false;
        } else if (xQueueReceive(readyEvents, &event, 0) != pdTRUE) {
            return;
        }
        if (paused && event.kind != EVENT_ERROR && event.kind != EVENT_FINISHED) {
            pendingEvent = event;
            pendingEventValid = true;
            return;
        }
        if (event.kind == EVENT_FRAME_START && page == PLAYING) {
            const uint32_t elapsed = millis() - playbackStartedMs;
            if (event.ptsMs > elapsed) {
                pendingEvent = event;
                pendingEventValid = true;
                return;
            }
            frameInProgress = true;
            frameChangedBytes = 0;
        } else if (event.kind == EVENT_OPEN) {
            page = PLAYING;
            playbackStartedMs = millis();
            expectedFrames = event.frame;
            playbackDurationMs = event.ptsMs;
            video.fillRect(0, 0, SCREEN_W, SCREEN_H, color(0, 0, 0));
            Serial.printf("CQUALDIS PLAY frames=%lu heap=%u dma=%u\n",
                          (unsigned long)event.frame,
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
        } else if (event.kind == EVENT_STRIPE) {
            if (page == PLAYING) frameChangedBytes += commitStripe(event.stripe, event.y);
            int slot = event.stripe;
            xQueueSend(freeStripes, &slot, 0);
        } else if (event.kind == EVENT_FRAME_END) {
            frameInProgress = false;
            ++totalFrames;
            if ((totalFrames % 10U) == 0 || frameChangedBytes == 0)
                Serial.printf("CQUALDIS FRAME %lu changed=%lu heap=%u\n",
                              (unsigned long)event.frame,
                              (unsigned long)frameChangedBytes,
                              (unsigned)ESP.getFreeHeap());
            if (pauseAfterFrame && totalFrames < expectedFrames) {
                paused = true;
                pausedAtMs = millis();
                Serial.println("CQUALDIS PAUSED");
            }
            pauseAfterFrame = false;
        } else if (event.kind == EVENT_ERROR) {
            Serial.printf("CQUALDIS ERROR %s\n", event.message);
            if (decodeTaskDone) finishPlayback(event.message);
            else {
                stopRequested = true;
                page = STOPPING;
                snprintf(browserStatus, sizeof(browserStatus), "%s", event.message);
            }
            return;
        } else if (event.kind == EVENT_FINISHED) {
            if (page == PLAYING && !paused &&
                millis() - playbackStartedMs < playbackDurationMs) {
                pendingEvent = event;
                pendingEventValid = true;
                return;
            }
            Serial.printf("CQUALDIS FINISHED frames=%lu\n",
                          (unsigned long)totalFrames);
            if (decodeTaskDone) finishPlayback("Playback complete");
            else {
                // The worker exits immediately after this event.
                page = STOPPING;
                snprintf(browserStatus, sizeof(browserStatus), "Playback complete");
            }
            return;
        }
    }
}

static void handleMouse(const String &line) {
    int x = mouseX, y = mouseY * 284 / (SCREEN_H - 1);
    int buttons = 0, dx = 0, dy = 0, wheel = 0;
    if (sscanf(line.c_str(), "MOUSE %d %d %d %d %d %d",
               &x, &y, &buttons, &dx, &dy, &wheel) < 3) return;
    const bool pressed = (buttons & 1) && !mouseLeftDown;
    mouseLeftDown = (buttons & 1) != 0;
    if (page != BROWSER) return;
    restoreCursor();
    mouseSeen = true;
    mouseX = constrain(x, 0, SCREEN_W - 1);
    mouseY = constrain(y * (SCREEN_H - 1) / 284, 0, SCREEN_H - 1);
    if (wheel != 0 && videoCount > 0) {
        const int next = constrain(selected + (wheel > 0 ? -1 : 1), 0, videoCount - 1);
        if (next != selected) {
            selected = next;
            drawBrowser();
        } else drawCursor();
        return;
    }
    if (pressed && videoCount > 0) {
        int first = selected - 1;
        if (first < 0) first = 0;
        if (first > videoCount - 3) first = max(0, videoCount - 3);
        for (int card = 0; card < 3; ++card) {
            const int rowY = 27 + card * 47;
            if (mouseY >= rowY && mouseY < rowY + 43 &&
                first + card < videoCount) {
                const int index = first + card;
                if (selected == index) startPlayback();
                else {
                    selected = index;
                    drawBrowser();
                }
                return;
            }
        }
    }
    drawCursor();
}

static void handleInput(String line, bool fromUSB = false) {
    line.trim();
    if (!line.length()) return;
    if (fromUSB && line == "CQUALDIS PING") {
        Serial.println("CQUALDIS PONG");
        return;
    }
    if (fromUSB && line.startsWith("CQUALDIS PUT ")) {
        receiveFileFromUSB(line);
        return;
    }
    if (fromUSB && line.startsWith("CQUALDIS DELETE ")) {
        deleteFileFromUSB(line);
        return;
    }
    if (line.startsWith("MOUSE ")) { handleMouse(line); return; }
    if (!videoReady) return;
    if (page == BROWSER) {
        if (line == "Escape" || line == "esc") { returnToKernel(); return; }
        if (line == "R" || line == "r") {
            if (!sdReady) sdReady = mountSD();
            scanVideos();
            drawBrowser();
            return;
        }
        if (videoCount > 0 && (line == "UpArrow" || line == "up" ||
                               line == "LeftArrow" || line == "left")) {
            selected = (selected + videoCount - 1) % videoCount;
            drawBrowser();
        } else if (videoCount > 0 && (line == "DownArrow" || line == "down" ||
                                      line == "RightArrow" || line == "right")) {
            selected = (selected + 1) % videoCount;
            drawBrowser();
        } else if (videoCount > 0 && (line == "Enter" || line == "enter")) {
            startPlayback();
        }
    } else if (line == "Escape" || line == "esc") {
        stopPlayback();
    } else if (page == PLAYING && (line == "Enter" || line == "enter" ||
                                   line == " " || line == "Space")) {
        if (paused) {
            paused = false;
            playbackStartedMs += millis() - pausedAtMs;
            Serial.println("CQUALDIS RESUMED");
        } else if (frameInProgress) {
            pauseAfterFrame = true;
        } else {
            paused = true;
            pausedAtMs = millis();
            Serial.println("CQUALDIS PAUSED");
        }
    }
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
    sdReady = mountSD();
    videoReady = video.init(CompMode::MODEPAL576Idiv3, VIDEO_PIN, true);
    if (!videoReady || video.xres != SCREEN_W || video.yres != SCREEN_H) {
        Serial.printf("CQUALDIS VIDEO INIT FAILED %dx%d\n", video.xres, video.yres);
        return;
    }
    controllerVideo.appVideoActive(8, 35);
    freeStripes = xQueueCreate(2, sizeof(int));
    readyEvents = xQueueCreate(8, sizeof(PlayerEvent));
    scanVideos();
    drawBrowser();
    Serial.printf("CQUALDIS READY %dx%d heap=%u dma=%u largest=%u jpeg=%u videos=%d\n",
                  video.xres, video.yres,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)sizeof(decoder), videoCount);
}

void loop() {
    String line;
    while (controllerInput.poll(Serial1, line)) handleInput(line, false);
    while (usbInput.poll(Serial, line)) handleInput(line, true);
    processPlayback();
    delay(1);
}
