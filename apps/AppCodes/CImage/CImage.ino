#include <SPI.h>
#include <SD.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include "SPIFFS.h"
#include "FS.h"
#include <Update.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <Wire.h>
#include <PNGdec.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include "esp_heap_caps.h"
#include <stdarg.h>
#include <new>

#define SPEAKER_PIN 12
#define SCREEN_WIDTH 376
#define SCREEN_HEIGHT 192
#define TFT_WIDTH  280
#define TFT_HEIGHT 176
#define SIDEBAR_X 284

static const int IMAGE_VIEW_X = 4;
static const int IMAGE_VIEW_Y = 22;
static const int IMAGE_VIEW_WIDTH = 272;
static const int IMAGE_VIEW_HEIGHT = 164;
static const int SIDEBAR_WIDTH = SCREEN_WIDTH - SIDEBAR_X;

static const int MAX_IMAGE_FILES = 32;
static const char *IMAGE_DIRECTORY = "/Images";
static const char *IMAGE_CACHE_DIRECTORY = "/Images/.CImageCache";
static const char *IMAGE_PREFERENCES_NAMESPACE = "cit_image";
static const uint32_t IMAGE_CONVERSION_CACHE_VERSION = 2;

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 376;
static const int APP_CURSOR_SCREEN_H = 192;
static const int APP_CURSOR_RADIUS = 3;
static const int APP_CURSOR_OUTLINE_RADIUS = 4;
static const int APP_CURSOR_MAX_PIXELS = 81;

typedef CompositeColorDAC::BufferGraphicsUnit AppCursorRawPixel;

struct AppCursorSavedPixel {
    int8_t dx;
    int8_t dy;
    AppCursorRawPixel raw;
};

struct AppMouseReport {
    int x;
    int y;
    int buttons;
    int dx;
    int dy;
    int wheel;
    bool leftDown;
    bool leftPressed;
};

static AppCursorSavedPixel appCursorPixels[APP_CURSOR_MAX_PIXELS];
static int appCursorPixelCount = 0;
static int appCursorX = APP_CURSOR_SCREEN_W / 2;
static int appCursorY = APP_CURSOR_SCREEN_H / 2;
static int appCursorButtons = 0;
static bool appCursorEnabled = false;
static bool appCursorDrawn = false;
static bool appCursorWasLeftDown = false;
static bool appMouseCursorOutlined = false;
static bool appCursorConfigLoaded = false;

static void appCursorLoadConfigOnce() {
    if (appCursorConfigLoaded) return;
    appCursorConfigLoaded = true;
    File f = SPIFFS.open("/systemConfiguration.conf", FILE_READ);
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim();
        val.trim();
        if (key == "MouseCursorOutlined" || key == "CursorOutlineMode") {
            appMouseCursorOutlined = val.toInt() != 0;
        }
    }
    f.close();
}

static inline AppCursorRawPixel appCursorGetRawPixel(int x, int y) {
    if (!videodisplay.backBuffer || (unsigned int)x >= (unsigned int)videodisplay.xres || (unsigned int)y >= (unsigned int)videodisplay.yres) return 0;
    return videodisplay.backBuffer[videodisplay.graphics_swy(y)][videodisplay.graphics_swx(x)];
}

static inline void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw) {
    if (!videodisplay.backBuffer || (unsigned int)x >= (unsigned int)videodisplay.xres || (unsigned int)y >= (unsigned int)videodisplay.yres) return;
    videodisplay.backBuffer[videodisplay.graphics_swy(y)][videodisplay.graphics_swx(x)] = raw;
}

static bool appCursorInnerPoint(int dx, int dy) {
    int d2 = dx * dx + dy * dy;
    return d2 >= 5 && d2 <= 10;
}

static bool appCursorOutlinePoint(int dx, int dy) {
    int d2 = dx * dx + dy * dy;
    return d2 > 10 && d2 <= 18;
}

static bool appCursorCapturePoint(int dx, int dy) {
    return appCursorInnerPoint(dx, dy) || (appMouseCursorOutlined && appCursorOutlinePoint(dx, dy));
}

static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw) {
    uint8_t signal = (uint8_t)((raw >> 8) & 0xff);
    int threshold = (videodisplay.levelBlack + videodisplay.levelWhite) / 2;
    return signal >= threshold;
}

static void appCursorRestore() {
    if (!appCursorDrawn) return;
    for (int i = 0; i < appCursorPixelCount; ++i) {
        int px = appCursorX + appCursorPixels[i].dx;
        int py = appCursorY + appCursorPixels[i].dy;
        appCursorSetRawPixel(px, py, appCursorPixels[i].raw);
    }
    appCursorDrawn = false;
    appCursorPixelCount = 0;
}

static void appCursorSetPosition(int x, int y, int buttons) {
    appCursorX = constrain(x, 0, APP_CURSOR_SCREEN_W - 1);
    appCursorY = constrain(y, 0, APP_CURSOR_SCREEN_H - 1);
    appCursorButtons = buttons;
    appCursorEnabled = true;
}

static void appCursorCaptureAndDraw() {
    if (!appCursorEnabled || appCursorDrawn) return;
    appCursorLoadConfigOnce();
    appCursorPixelCount = 0;
    int radius = appMouseCursorOutlined ? APP_CURSOR_OUTLINE_RADIUS : APP_CURSOR_RADIUS;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (!appCursorCapturePoint(dx, dy)) continue;
            int px = appCursorX + dx;
            int py = appCursorY + dy;
            if ((unsigned int)px >= APP_CURSOR_SCREEN_W || (unsigned int)py >= APP_CURSOR_SCREEN_H) continue;
            if (appCursorPixelCount >= APP_CURSOR_MAX_PIXELS) continue;
            appCursorPixels[appCursorPixelCount].dx = dx;
            appCursorPixels[appCursorPixelCount].dy = dy;
            appCursorPixels[appCursorPixelCount].raw = appCursorGetRawPixel(px, py);
            appCursorPixelCount++;
        }
    }
    uint32_t white = videodisplay.RGB(255, 255, 255);
    uint32_t black = videodisplay.RGB(0, 0, 0);
    for (int i = 0; i < appCursorPixelCount; ++i) {
        int px = appCursorX + appCursorPixels[i].dx;
        int py = appCursorY + appCursorPixels[i].dy;
        bool outlinePoint = appCursorOutlinePoint(appCursorPixels[i].dx, appCursorPixels[i].dy);
        uint32_t cursorColor = white;
        if (appMouseCursorOutlined) cursorColor = outlinePoint ? white : black;
        else cursorColor = appCursorBackgroundLooksBright(appCursorPixels[i].raw) ? black : white;
        videodisplay.dotFast(px, py, cursorColor);
    }
    appCursorDrawn = true;
}

static void appCursorRefreshAfterRedraw() {
    if (appCursorEnabled && !appCursorDrawn) appCursorCaptureAndDraw();
}

class AppCursorDrawGuard {
  public:
    AppCursorDrawGuard() { appCursorRestore(); }
    ~AppCursorDrawGuard() { appCursorRefreshAfterRedraw(); }
};

static bool appMouseRead(String input, AppMouseReport &report) {
    input.trim();
    if (!input.startsWith("MOUSE")) return false;
    report.x = appCursorX;
    report.y = appCursorY;
    report.buttons = appCursorButtons;
    report.dx = 0;
    report.dy = 0;
    report.wheel = 0;
    int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d", &report.x, &report.y, &report.buttons, &report.dx, &report.dy, &report.wheel);
    if (parsed >= 3) {
        report.leftDown = (report.buttons & 0x01) != 0;
        report.leftPressed = report.leftDown && !appCursorWasLeftDown;
        appCursorWasLeftDown = report.leftDown;
    } else {
        report.leftDown = false;
        report.leftPressed = false;
    }
    return true;
}

static void restartWithFallbackVideo(const char *label) {
    const char *text = label ? label : "Restarting";
    Serial1.print("VDPREP 0 ");
    Serial1.println(text);
    Serial1.flush();
    delay(350);
    videodisplay.i2sStop();
    pinMode(25, INPUT_PULLDOWN);
    delay(5);
    Serial1.println("VDINIT");
    Serial1.print("VDPROG 100 ");
    Serial1.println(text);
    Serial1.flush();
    delay(90);
    ESP.restart();
}

static void releaseSerialControllerVideo() {
    for (uint8_t i = 0; i < 12; ++i) {
        Serial1.println("CVBSOFF");
        Serial1.println("VDAPPVIDEO");
        Serial1.flush();
        delay(80);
    }
}
File root;

volatile int imageCount = 0;
volatile int currentImage = 0;
static volatile bool kernelExitRequested = false;

int keepStable = 0;

String imageFiles[MAX_IMAGE_FILES];
String boolRes = "";
String appName = "";
String appString = "";

enum GalleryImageFormat : uint8_t {
    GALLERY_IMAGE_NONE = 0,
    GALLERY_IMAGE_BMP,
    GALLERY_IMAGE_PNG,
    GALLERY_IMAGE_JPEG
};

enum CImagePage : uint8_t {
    CIMAGE_PAGE_GALLERY = 0,
    CIMAGE_PAGE_OPTIONS
};

enum CImageScaleMode : uint8_t {
    CIMAGE_SCALE_FIT = 0,
    CIMAGE_SCALE_FILL,
    CIMAGE_SCALE_STRETCH,
    CIMAGE_SCALE_COUNT
};

static const char *cimageScaleModeName(CImageScaleMode mode);
static void setCImagePage(CImagePage page);
static void setCImageScaleMode(CImageScaleMode mode);
static void drawCImageChrome(CImagePage page, const char *status);
static uint32_t imageRenderableSignature(uint32_t sourceSignature,
                                         GalleryImageFormat format);

static volatile CImagePage cimagePage = CIMAGE_PAGE_GALLERY;
static volatile uint8_t cimageOptionSelection = 0;
static volatile uint32_t cimageRenderRevision = 1;
static CImageScaleMode cimageScaleMode = CIMAGE_SCALE_FIT;
static bool cimageColourEnabled = true;
static char imageConversionError[96] = "";

static GalleryImageFormat galleryImageFormat(const String &name);
static void imageHashWord(uint32_t &hash, uint32_t value);
static bool ensureRenderableBmp(const String &filename, int drawingImageIndex,
                                String &bmpPath, uint32_t &sourceSignature,
                                GalleryImageFormat &format,
                                int &sourceWidth, int &sourceHeight);

struct ImageConversionContext {
    int sourceWidth;
    int sourceHeight;
    int outputWidth;
    int outputHeight;
    uint32_t outputStride;
    int rowsWritten;
    int drawingImageIndex;
    bool failed;
    uint16_t *line565;
    uint16_t *xMap;
    uint16_t *yMap;
    uint8_t *rowBuffer;
    int rowBufferCapacityRows;
    int jpegBandY;
    int jpegBandHeight;
    int jpegBandOutputRows;
    char failure[72];
};

struct ImageRawCacheHeader {
    uint32_t magic;
    uint32_t sourceSignature;
    uint32_t calibrationSignature;
    uint32_t renderSignature;
    uint16_t drawX;
    uint16_t drawY;
    uint16_t width;
    uint16_t height;
    uint16_t screenWidth;
    uint16_t screenHeight;
};

static const uint32_t IMAGE_RAW_CACHE_MAGIC = 0x32524943UL; // "CIR2"
static File imageDecodeFile;
static File imageConversionFile;
static PNG *activePngDecoder = nullptr;
static ImageConversionContext imageConversion = {};
static volatile bool imageIoBusy = false;

static int kernelFlashVideoLastPercent = -1;

static void kernelFlashVideoProgress(int percent, const char *label) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == kernelFlashVideoLastPercent) return;
    kernelFlashVideoLastPercent = percent;
    Serial1.print("VDPROG ");
    Serial1.print(percent);
    Serial1.print(' ');
    Serial1.println(label ? label : "Flashing kernel");
}

static void kernelFlashVideoBytes(size_t writtenBytes) {
    Serial1.print("VDAPPWRITE ");
    Serial1.println((unsigned long)writtenBytes);
}

static void kernelFlashVideoStart(size_t totalBytes, const char *label) {
    kernelFlashVideoLastPercent = -1;
    Serial1.print("VDAPPSTART ");
    Serial1.print((unsigned long)totalBytes);
    Serial1.print(' ');
    Serial1.println(label ? label : "Flashing kernel");
    Serial1.flush();
}

void boolTru() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
}
void boolUpdate() {
    SPIFFS.remove("/evil.txt");
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (file) {
        file.println(boolRes);
        file.close();
        Serial.printf("Bool updated to %s\n", boolRes);
    } else {
        Serial.println("Failed to update bool!");
    }
}
bool handleKernelFlash() {
    File kernelFile1 = SD.open("/System/kernel.bin");
    if (!kernelFile1) {
        Serial.println("Kernel file not found!");
        kernelFlashVideoProgress(100, "Kernel file missing");
        return false;
    }

    size_t kernelSize1 = kernelFile1.size();
    Serial.printf("Kernel size: %d bytes\n", kernelSize1);
    kernelFlashVideoStart(kernelSize1, "Flashing kernel");

    if (!Update.begin(kernelSize1)) {
        Serial.printf("Not enough space for the kernel! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), kernelSize1);
        Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        Serial1.flush();
        kernelFile1.close();
        return false;
    }


    Serial.printf("Free Sketch Space: %d bytes\n", ESP.getFreeSketchSpace());
    uint8_t buffer1[2048];
    size_t flashedKernelBytes = 0;
    while (kernelFile1.available()) {
        int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
        int written1 = Update.write(buffer1, len1);
        Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n", len1, len1, written1);
        if (written1 > 0) {
            flashedKernelBytes += written1;
            kernelFlashVideoBytes(flashedKernelBytes);
        }

        if (written1 != len1) {
            Serial.printf("Write failed! Expected: %d, Wrote: %d\n", len1, written1);
            Update.abort();
            kernelFlashVideoProgress(100, "Kernel flash failed");
            Serial1.flush();
            kernelFile1.close();
            return false;
        }
    }

    kernelFile1.close();
    if (Update.end(true)) {
        Serial.println("Kernel written to flash successfully.");
        kernelFlashVideoBytes(kernelSize1);
        Serial1.println("VDSTOP");
        Serial1.flush();
        delay(25);
        ESP.restart();
        return true;
    } else {
        Serial.printf("Update failed: %s\n", Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        Serial1.flush();
        return false;
    }
}

static void returnToKernelNow() {
    appCursorRestore();
    boolRes = "false";
    boolUpdate();

    Serial1.println("VDPREP 0 Returning home");
    Serial1.flush();
    delay(160);
    videodisplay.i2sStop();
    pinMode(25, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(60);

    if (!handleKernelFlash()) {
        restartWithFallbackVideo("Kernel flash failed");
    }
}

static const char *cimageScaleModeName(CImageScaleMode mode) {
    switch (mode) {
        case CIMAGE_SCALE_FILL: return "FILL";
        case CIMAGE_SCALE_STRETCH: return "STRETCH";
        case CIMAGE_SCALE_FIT:
        default: return "FIT";
    }
}

static void setImageConversionError(const char *format, ...) {
    if (!format) {
        imageConversionError[0] = '\0';
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(imageConversionError, sizeof(imageConversionError), format, args);
    va_end(args);
    Serial.print("CImage conversion error: ");
    Serial.println(imageConversionError);
}

static void setImageConversionFailure(const char *format, ...) {
    imageConversion.failed = true;
    if (!format) {
        imageConversion.failure[0] = '\0';
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(imageConversion.failure, sizeof(imageConversion.failure), format, args);
    va_end(args);
}

static void requestCImageRedraw() {
    ++cimageRenderRevision;
}

static void loadCImagePreferences() {
    Preferences preferences;
    if (!preferences.begin(IMAGE_PREFERENCES_NAMESPACE, true)) return;
    uint8_t storedMode = preferences.getUChar("scale", CIMAGE_SCALE_FIT);
    if (storedMode >= CIMAGE_SCALE_COUNT) storedMode = CIMAGE_SCALE_FIT;
    cimageScaleMode = (CImageScaleMode)storedMode;
    cimageColourEnabled = preferences.getBool("colour", true);
    preferences.end();
    Serial.printf("CImage options: sizing=%s colour=%s\n",
                  cimageScaleModeName(cimageScaleMode),
                  cimageColourEnabled ? "on" : "off");
}

static void saveCImagePreferences() {
    Preferences preferences;
    if (!preferences.begin(IMAGE_PREFERENCES_NAMESPACE, false)) return;
    preferences.putUChar("scale", (uint8_t)cimageScaleMode);
    preferences.putBool("colour", cimageColourEnabled);
    preferences.end();
}

static uint32_t cimageRenderSignatureValue() {
    uint32_t signature = 2166136261UL;
    imageHashWord(signature, (uint32_t)cimageScaleMode);
    imageHashWord(signature, cimageColourEnabled ? 1UL : 0UL);
    imageHashWord(signature, IMAGE_VIEW_WIDTH);
    imageHashWord(signature, IMAGE_VIEW_HEIGHT);
    return signature;
}

static GalleryImageFormat galleryImageFormat(const String &name) {
    String lower = name;
    lower.toLowerCase();
    if (lower.endsWith(".bmp")) return GALLERY_IMAGE_BMP;
    if (lower.endsWith(".png")) return GALLERY_IMAGE_PNG;
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return GALLERY_IMAGE_JPEG;
    return GALLERY_IMAGE_NONE;
}

static uint16_t imageReadLE16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t imageReadLE32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint32_t imageReadBE32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void imageWriteLE16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void imageWriteLE32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static bool readPngDimensions(const String &path, int &width, int &height) {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    uint8_t header[24];
    int count = file.read(header, sizeof(header));
    file.close();
    static const uint8_t signature[8] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
    };
    if (count != (int)sizeof(header) ||
        memcmp(header, signature, sizeof(signature)) != 0) return false;
    width = (int)imageReadBE32(header + 16);
    height = (int)imageReadBE32(header + 20);
    return width > 0 && width <= 8192 && height > 0 && height <= 8192;
}

static bool readJpegDimensions(const String &path, int &width, int &height,
                               bool *progressive = nullptr) {
    if (progressive) *progressive = false;
    File file = SD.open(path, FILE_READ);
    if (!file || file.read() != 0xff || file.read() != 0xd8) {
        if (file) file.close();
        return false;
    }
    while (file.available()) {
        int prefix = file.read();
        if (prefix != 0xff) continue;
        int marker = file.read();
        while (marker == 0xff && file.available()) marker = file.read();
        if (marker == 0xd8 || marker == 0x01) continue;
        if (marker == 0xd9 || marker == 0xda) break;
        int high = file.read();
        int low = file.read();
        if (high < 0 || low < 0) break;
        int length = (high << 8) | low;
        if (length < 2) break;
        bool startOfFrame = (marker >= 0xc0 && marker <= 0xc3) ||
                            (marker >= 0xc5 && marker <= 0xc7) ||
                            (marker >= 0xc9 && marker <= 0xcb) ||
                            (marker >= 0xcd && marker <= 0xcf);
        if (startOfFrame && length >= 7) {
            file.read();
            int hHigh = file.read();
            int hLow = file.read();
            int wHigh = file.read();
            int wLow = file.read();
            file.close();
            width = (wHigh << 8) | wLow;
            height = (hHigh << 8) | hLow;
            if (progressive) *progressive = marker == 0xc2;
            return width > 0 && width <= 8192 && height > 0 && height <= 8192;
        }
        if (!file.seek(file.position() + length - 2)) break;
    }
    file.close();
    return false;
}

static void fittedImageDimensions(int sourceWidth, int sourceHeight,
                                  int &outputWidth, int &outputHeight) {
    float scaleX = (float)IMAGE_VIEW_WIDTH / (float)sourceWidth;
    float scaleY = (float)IMAGE_VIEW_HEIGHT / (float)sourceHeight;
    float scale = min(1.0f, min(scaleX, scaleY));
    outputWidth = max(1, (int)(sourceWidth * scale + 0.5f));
    outputHeight = max(1, (int)(sourceHeight * scale + 0.5f));
}

static bool ensureImageCacheDirectory() {
    if (SD.exists(IMAGE_CACHE_DIRECTORY)) return true;
    bool created = SD.mkdir(IMAGE_CACHE_DIRECTORY);
    if (!created) setImageConversionError("Cannot create image cache directory");
    return created;
}

static void imageHashByte(uint32_t &hash, uint8_t value) {
    hash ^= value;
    hash *= 16777619UL;
}

static void imageHashWord(uint32_t &hash, uint32_t value) {
    imageHashByte(hash, (uint8_t)value);
    imageHashByte(hash, (uint8_t)(value >> 8));
    imageHashByte(hash, (uint8_t)(value >> 16));
    imageHashByte(hash, (uint8_t)(value >> 24));
}

static uint32_t imageSourceSignature(const String &path) {
    uint32_t hash = 2166136261UL;
    for (int index = 0; index < path.length(); ++index) {
        imageHashByte(hash, (uint8_t)path[index]);
    }
    File file = SD.open(path, FILE_READ);
    if (!file) return hash;
    uint32_t size = file.size();
    imageHashWord(hash, size);
    uint8_t sample[64];
    int count = file.read(sample, sizeof(sample));
    for (int index = 0; index < count; ++index) imageHashByte(hash, sample[index]);
    if (size > sizeof(sample) && file.seek(size - sizeof(sample))) {
        count = file.read(sample, sizeof(sample));
        for (int index = 0; index < count; ++index) imageHashByte(hash, sample[index]);
    }
    file.close();
    return hash;
}

static uint32_t imageRenderableSignature(uint32_t sourceSignature,
                                         GalleryImageFormat format) {
    if (format == GALLERY_IMAGE_PNG || format == GALLERY_IMAGE_JPEG) {
        imageHashWord(sourceSignature, IMAGE_CONVERSION_CACHE_VERSION);
    }
    return sourceSignature;
}

static String imageCachePath(uint32_t signature, const char *extension) {
    char path[72];
    snprintf(path, sizeof(path), "%s/C%08lX.%s", IMAGE_CACHE_DIRECTORY,
             (unsigned long)signature, extension ? extension : "dat");
    return String(path);
}

static bool writeTopDownBmpHeader(File &file, int width, int height,
                                  uint32_t &stride) {
    stride = ((uint32_t)width * 3U + 3U) & ~3U;
    uint64_t imageBytes = (uint64_t)stride * (uint32_t)height;
    if (imageBytes > 0xffffffffULL - 54ULL) return false;
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    imageWriteLE32(header + 2, (uint32_t)imageBytes + 54U);
    imageWriteLE32(header + 10, 54U);
    imageWriteLE32(header + 14, 40U);
    imageWriteLE32(header + 18, (uint32_t)width);
    imageWriteLE32(header + 22, (uint32_t)(int32_t)-height);
    imageWriteLE16(header + 26, 1);
    imageWriteLE16(header + 28, 24);
    imageWriteLE32(header + 34, (uint32_t)imageBytes);
    return file.write(header, sizeof(header)) == sizeof(header);
}

static bool cachedBmpMatches(const String &path, int width, int height) {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    uint8_t header[54];
    int count = file.read(header, sizeof(header));
    uint32_t stride = ((uint32_t)width * 3U + 3U) & ~3U;
    bool valid = count == (int)sizeof(header) && header[0] == 'B' && header[1] == 'M' &&
                 (int32_t)imageReadLE32(header + 18) == width &&
                 (int32_t)imageReadLE32(header + 22) == -height &&
                 imageReadLE16(header + 28) == 24 &&
                 imageReadLE32(header + 30) == 0 &&
                 file.size() >= 54U + stride * (uint32_t)height;
    file.close();
    return valid;
}

static bool imageConversionCancelled() {
    return kernelExitRequested || currentImage != imageConversion.drawingImageIndex;
}

static void *imageDecoderOpen(const char *filename, int32_t *size) {
    imageDecodeFile = SD.open(filename, FILE_READ);
    if (!imageDecodeFile) {
        *size = 0;
        return nullptr;
    }
    *size = imageDecodeFile.size();
    return &imageDecodeFile;
}

static void imageDecoderClose(void *handle) {
    (void)handle;
    if (imageDecodeFile) imageDecodeFile.close();
}

static int32_t imagePngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
    (void)handle;
    return imageDecodeFile ? imageDecodeFile.read(buffer, length) : 0;
}

static int32_t imagePngSeek(PNGFILE *handle, int32_t position) {
    (void)handle;
    return imageDecodeFile && imageDecodeFile.seek(position);
}

static int32_t imageJpegRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
    (void)handle;
    return imageDecodeFile ? imageDecodeFile.read(buffer, length) : 0;
}

static int32_t imageJpegSeek(JPEGFILE *handle, int32_t position) {
    (void)handle;
    return imageDecodeFile && imageDecodeFile.seek(position);
}

static void rgb565ToBgr(uint16_t pixel, uint8_t *destination) {
    destination[2] = (uint8_t)(((pixel >> 11) & 0x1f) * 255 / 31);
    destination[1] = (uint8_t)(((pixel >> 5) & 0x3f) * 255 / 63);
    destination[0] = (uint8_t)((pixel & 0x1f) * 255 / 31);
}

static const char *pngErrorName(int code) {
    switch (code) {
        case PNG_SUCCESS: return "success";
        case PNG_INVALID_PARAMETER: return "invalid parameter";
        case PNG_DECODE_ERROR: return "decode error";
        case PNG_MEM_ERROR: return "decoder memory";
        case PNG_NO_BUFFER: return "no output buffer";
        case PNG_UNSUPPORTED_FEATURE: return "unsupported PNG feature";
        case PNG_INVALID_FILE: return "invalid PNG";
        case PNG_TOO_BIG: return "scanline exceeds decoder limit";
        case PNG_QUIT_EARLY: return "conversion stopped early";
        default: return "unknown PNG error";
    }
}

static int imagePngConvertDraw(PNGDRAW *draw) {
    if (!draw || !activePngDecoder || !imageConversion.line565 ||
        !imageConversion.rowBuffer || imageConversionCancelled()) return 0;
    if (draw->iWidth < imageConversion.sourceWidth) {
        setImageConversionFailure("PNG scanline width changed (%d/%d)",
                                  draw->iWidth, imageConversion.sourceWidth);
        return 0;
    }
    if (imageConversion.rowsWritten >= imageConversion.outputHeight ||
        imageConversion.yMap[imageConversion.rowsWritten] != draw->y) return 1;

    activePngDecoder->getLineAsRGB565(
        draw, imageConversion.line565, PNG_RGB565_LITTLE_ENDIAN, 0x000000);
    memset(imageConversion.rowBuffer, 0, imageConversion.outputStride);
    for (int x = 0; x < imageConversion.outputWidth; ++x) {
        int sourceX = imageConversion.xMap[x];
        rgb565ToBgr(imageConversion.line565[sourceX],
                    imageConversion.rowBuffer + x * 3);
    }
    if (imageConversionFile.write(imageConversion.rowBuffer,
                                  imageConversion.outputStride) != imageConversion.outputStride) {
        setImageConversionFailure("SD write failed at PNG row %d",
                                  imageConversion.rowsWritten);
        return 0;
    }
    ++imageConversion.rowsWritten;
    if ((imageConversion.rowsWritten & 15) == 0) yield();
    return 1;
}

static int imageJpegConvertDraw(JPEGDRAW *draw) {
    if (!draw || !draw->pPixels || !imageConversion.rowBuffer ||
        imageConversionCancelled()) return 0;

    if (imageConversion.jpegBandY != draw->y) {
        if (imageConversion.jpegBandY >= 0 && imageConversion.jpegBandOutputRows > 0) {
            setImageConversionFailure("JPEG band ended before all tiles arrived");
            return 0;
        }
        imageConversion.jpegBandY = draw->y;
        imageConversion.jpegBandHeight = draw->iHeight;
        imageConversion.jpegBandOutputRows = 0;
        int bandEnd = draw->y + draw->iHeight;
        for (int row = imageConversion.rowsWritten;
             row < imageConversion.outputHeight && imageConversion.yMap[row] < bandEnd;
             ++row) {
            if (imageConversion.yMap[row] >= draw->y) {
                ++imageConversion.jpegBandOutputRows;
            }
        }
        if (imageConversion.jpegBandOutputRows > imageConversion.rowBufferCapacityRows) {
            setImageConversionFailure("JPEG output band exceeds row buffer");
            return 0;
        }
        memset(imageConversion.rowBuffer, 0,
               imageConversion.outputStride * imageConversion.jpegBandOutputRows);
    }

    for (int row = 0; row < imageConversion.jpegBandOutputRows; ++row) {
        int sourceY = imageConversion.yMap[imageConversion.rowsWritten + row];
        if (sourceY < draw->y || sourceY >= draw->y + draw->iHeight) continue;
        int localY = sourceY - draw->y;
        uint8_t *output = imageConversion.rowBuffer +
                          imageConversion.outputStride * row;
        for (int x = 0; x < imageConversion.outputWidth; ++x) {
            int sourceX = imageConversion.xMap[x];
            if (sourceX < draw->x || sourceX >= draw->x + draw->iWidth) continue;
            int localX = sourceX - draw->x;
            rgb565ToBgr(draw->pPixels[localY * draw->iWidth + localX],
                        output + x * 3);
        }
    }

    if (draw->x + draw->iWidth >= imageConversion.sourceWidth) {
        for (int row = 0; row < imageConversion.jpegBandOutputRows; ++row) {
            uint8_t *output = imageConversion.rowBuffer +
                              imageConversion.outputStride * row;
            if (imageConversionFile.write(output, imageConversion.outputStride) !=
                imageConversion.outputStride) {
                setImageConversionFailure("SD write failed at JPEG row %d",
                                          imageConversion.rowsWritten + row);
                return 0;
            }
        }
        imageConversion.rowsWritten += imageConversion.jpegBandOutputRows;
        imageConversion.jpegBandY = -1;
        imageConversion.jpegBandOutputRows = 0;
        yield();
    }
    return 1;
}

static void resetImageConversionContext() {
    imageConversion = {};
    imageConversion.jpegBandY = -1;
}

static void releaseImageConversionBuffers() {
    if (imageConversion.line565) free(imageConversion.line565);
    if (imageConversion.xMap) free(imageConversion.xMap);
    if (imageConversion.yMap) free(imageConversion.yMap);
    if (imageConversion.rowBuffer) free(imageConversion.rowBuffer);
    imageConversion.line565 = nullptr;
    imageConversion.xMap = nullptr;
    imageConversion.yMap = nullptr;
    imageConversion.rowBuffer = nullptr;
}

static bool prepareImageConversion(const String &outputPath,
                                   int sourceWidth, int sourceHeight,
                                   int outputWidth, int outputHeight,
                                   int drawingImageIndex) {
    resetImageConversionContext();
    imageConversion.sourceWidth = sourceWidth;
    imageConversion.sourceHeight = sourceHeight;
    imageConversion.outputWidth = outputWidth;
    imageConversion.outputHeight = outputHeight;
    imageConversion.drawingImageIndex = drawingImageIndex;
    imageConversion.xMap = (uint16_t*)malloc(outputWidth * sizeof(uint16_t));
    imageConversion.yMap = (uint16_t*)malloc(outputHeight * sizeof(uint16_t));
    if (!imageConversion.xMap || !imageConversion.yMap) {
        setImageConversionError("Not enough memory for conversion maps");
        return false;
    }
    for (int x = 0; x < outputWidth; ++x) {
        imageConversion.xMap[x] = (uint16_t)((uint64_t)x * sourceWidth / outputWidth);
    }
    for (int y = 0; y < outputHeight; ++y) {
        imageConversion.yMap[y] = (uint16_t)((uint64_t)y * sourceHeight / outputHeight);
    }

    SD.remove(outputPath);
    imageConversionFile = SD.open(outputPath, FILE_WRITE);
    if (!imageConversionFile) {
        setImageConversionError("Cannot create BMP cache file");
        return false;
    }
    if (!writeTopDownBmpHeader(imageConversionFile, outputWidth, outputHeight,
                               imageConversion.outputStride)) {
        setImageConversionError("Cannot write BMP cache header");
        return false;
    }
    return true;
}

static bool convertPngToBmp(const String &sourcePath, const String &outputPath,
                            int sourceWidth, int sourceHeight,
                            int outputWidth, int outputHeight,
                            int drawingImageIndex) {
    if (!prepareImageConversion(outputPath, sourceWidth, sourceHeight,
                                outputWidth, outputHeight, drawingImageIndex)) {
        releaseImageConversionBuffers();
        if (imageConversionFile) imageConversionFile.close();
        SD.remove(outputPath);
        return false;
    }
    Serial.printf("PNG conversion start: %dx%d -> %dx%d, heap=%u, largest=%u, decoder=%u bytes\n",
                  sourceWidth, sourceHeight, outputWidth, outputHeight,
                  ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned int)sizeof(PNG));
    imageConversion.line565 = (uint16_t*)malloc(sourceWidth * sizeof(uint16_t));
    imageConversion.rowBuffer = (uint8_t*)malloc(imageConversion.outputStride);
    PNG *decoder = new (std::nothrow) PNG();
    if (!imageConversion.line565 || !imageConversion.rowBuffer || !decoder) {
        setImageConversionError("PNG work memory unavailable (largest %u)",
                                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        delete decoder;
        releaseImageConversionBuffers();
        imageConversionFile.close();
        SD.remove(outputPath);
        return false;
    }

    activePngDecoder = decoder;
    int openResult = decoder->open(sourcePath.c_str(), imageDecoderOpen,
                                   imageDecoderClose, imagePngRead,
                                   imagePngSeek, imagePngConvertDraw);
    bool ok = openResult == PNG_SUCCESS;
    int decodeResult = openResult;
    if (ok) {
        Serial.printf("PNG opened: %dx%d, bpp=%d, type=%d, alpha=%d, interlaced=%d\n",
                      decoder->getWidth(), decoder->getHeight(), decoder->getBpp(),
                      decoder->getPixelType(), decoder->hasAlpha(), decoder->isInterlaced());
        decodeResult = decoder->decode(nullptr, PNG_FAST_PALETTE);
        ok = decodeResult == PNG_SUCCESS;
    }
    int decoderError = decoder->getLastError();
    Serial.printf("PNG decoder result: open=%d decode=%d last=%d (%s), rows=%d/%d\n",
                  openResult, decodeResult, decoderError, pngErrorName(decoderError),
                  imageConversion.rowsWritten, outputHeight);
    if (!ok) {
        setImageConversionError("PNG: %s (%d)", pngErrorName(decoderError), decoderError);
    } else if (imageConversion.failed) {
        setImageConversionError("PNG: %s", imageConversion.failure[0]
                                ? imageConversion.failure : "conversion callback failed");
    }
    decoder->close();
    activePngDecoder = nullptr;
    delete decoder;
    imageConversionFile.flush();
    imageConversionFile.close();
    ok = ok && !imageConversion.failed &&
         imageConversion.rowsWritten == outputHeight &&
         !imageConversionCancelled();
    if (!ok && imageConversionError[0] == '\0') {
        if (imageConversionCancelled()) setImageConversionError("PNG conversion cancelled");
        else setImageConversionError("PNG produced %d of %d rows",
                                     imageConversion.rowsWritten, outputHeight);
    }
    releaseImageConversionBuffers();
    if (!ok) SD.remove(outputPath);
    return ok;
}

static bool convertJpegToBmp(const String &sourcePath, const String &outputPath,
                             int originalWidth, int originalHeight,
                             int outputWidth, int outputHeight,
                             int drawingImageIndex) {
    JPEGDEC *decoder = new (std::nothrow) JPEGDEC();
    if (!decoder) {
        setImageConversionError("JPEG decoder memory unavailable");
        return false;
    }
    bool opened = decoder->open(sourcePath.c_str(), imageDecoderOpen,
                                imageDecoderClose, imageJpegRead,
                                imageJpegSeek, imageJpegConvertDraw);
    if (!opened) {
        setImageConversionError("JPEG open failed (%d)", decoder->getLastError());
        delete decoder;
        return false;
    }

    int scaleFactor = 1;
    if (decoder->getJPEGType() == JPEG_MODE_PROGRESSIVE) {
        // JPEGDEC exposes progressive images as their first 1/8-size scan.
        scaleFactor = 8;
    } else {
        while (scaleFactor < 8 &&
               originalWidth / (scaleFactor * 2) >= outputWidth &&
               originalHeight / (scaleFactor * 2) >= outputHeight) {
            scaleFactor *= 2;
        }
    }
    int decodedWidth = max(1, (originalWidth + scaleFactor - 1) / scaleFactor);
    int decodedHeight = max(1, (originalHeight + scaleFactor - 1) / scaleFactor);
    if (!prepareImageConversion(outputPath, decodedWidth, decodedHeight,
                                outputWidth, outputHeight, drawingImageIndex)) {
        decoder->close();
        delete decoder;
        releaseImageConversionBuffers();
        if (imageConversionFile) imageConversionFile.close();
        SD.remove(outputPath);
        return false;
    }

    int maximumBandRows = min(outputHeight,
        max(2, (16 * outputHeight + decodedHeight - 1) / decodedHeight + 2));
    imageConversion.rowBufferCapacityRows = maximumBandRows;
    imageConversion.rowBuffer = (uint8_t*)malloc(
        imageConversion.outputStride * maximumBandRows);
    if (!imageConversion.rowBuffer) {
        setImageConversionError("JPEG row memory unavailable");
        decoder->close();
        delete decoder;
        releaseImageConversionBuffers();
        imageConversionFile.close();
        SD.remove(outputPath);
        return false;
    }

    int option = 0;
    if (scaleFactor == 2) option = JPEG_SCALE_HALF;
    else if (scaleFactor == 4) option = JPEG_SCALE_QUARTER;
    else if (scaleFactor == 8) option = JPEG_SCALE_EIGHTH;
    decoder->setPixelType(RGB565_LITTLE_ENDIAN);
    bool ok = decoder->decode(0, 0, option);
    int decoderError = decoder->getLastError();
    Serial.printf("JPEG decoder result: ok=%d last=%d, rows=%d/%d\n",
                  ok, decoderError, imageConversion.rowsWritten, outputHeight);
    if (!ok) setImageConversionError("JPEG decode failed (%d)", decoderError);
    else if (imageConversion.failed) {
        setImageConversionError("JPEG: %s", imageConversion.failure[0]
                                ? imageConversion.failure : "conversion callback failed");
    }
    decoder->close();
    delete decoder;
    imageConversionFile.flush();
    imageConversionFile.close();
    ok = ok && !imageConversion.failed &&
         imageConversion.rowsWritten == outputHeight &&
         !imageConversionCancelled();
    if (!ok && imageConversionError[0] == '\0') {
        if (imageConversionCancelled()) setImageConversionError("JPEG conversion cancelled");
        else setImageConversionError("JPEG produced %d of %d rows",
                                     imageConversion.rowsWritten, outputHeight);
    }
    releaseImageConversionBuffers();
    if (!ok) SD.remove(outputPath);
    return ok;
}

static bool ensureRenderableBmp(const String &filename, int drawingImageIndex,
                                String &bmpPath, uint32_t &sourceSignature,
                                GalleryImageFormat &format,
                                int &sourceWidth, int &sourceHeight) {
    String sourcePath = String(IMAGE_DIRECTORY) + "/" + filename;
    imageConversionError[0] = '\0';
    format = galleryImageFormat(filename);
    sourceSignature = imageRenderableSignature(imageSourceSignature(sourcePath), format);
    if (format == GALLERY_IMAGE_BMP) {
        bmpPath = sourcePath;
        sourceWidth = 0;
        sourceHeight = 0;
        return true;
    }
    bool progressiveJpeg = false;
    if (format == GALLERY_IMAGE_PNG) {
        if (!readPngDimensions(sourcePath, sourceWidth, sourceHeight)) {
            setImageConversionError("Invalid PNG header");
            return false;
        }
    } else if (format == GALLERY_IMAGE_JPEG) {
        if (!readJpegDimensions(sourcePath, sourceWidth, sourceHeight,
                                &progressiveJpeg)) {
            setImageConversionError("Invalid JPEG header");
            return false;
        }
    } else {
        setImageConversionError("Unsupported image extension");
        return false;
    }
    File sourceInfo = SD.open(sourcePath, FILE_READ);
    uint32_t sourceBytes = sourceInfo ? sourceInfo.size() : 0;
    if (sourceInfo) sourceInfo.close();
    Serial.printf("Image source: %s, %dx%d, %lu bytes\n", filename.c_str(),
                  sourceWidth, sourceHeight, (unsigned long)sourceBytes);

    int outputWidth = 0;
    int outputHeight = 0;
    fittedImageDimensions(sourceWidth, sourceHeight, outputWidth, outputHeight);
    if (progressiveJpeg) {
        float progressiveScale = min(0.125f,
            min((float)IMAGE_VIEW_WIDTH / sourceWidth,
                (float)IMAGE_VIEW_HEIGHT / sourceHeight));
        outputWidth = max(1, (int)(sourceWidth * progressiveScale + 0.5f));
        outputHeight = max(1, (int)(sourceHeight * progressiveScale + 0.5f));
    }
    if (!ensureImageCacheDirectory()) return false;
    bmpPath = imageCachePath(sourceSignature, "bmp");
    if (cachedBmpMatches(bmpPath, outputWidth, outputHeight)) return true;

    Serial.printf("Converting %s to %dx%d 24-bit BMP cache...\n",
                  filename.c_str(), outputWidth, outputHeight);
    imageIoBusy = true;
    uint32_t started = millis();
    bool converted = format == GALLERY_IMAGE_PNG
        ? convertPngToBmp(sourcePath, bmpPath, sourceWidth, sourceHeight,
                          outputWidth, outputHeight, drawingImageIndex)
        : convertJpegToBmp(sourcePath, bmpPath, sourceWidth, sourceHeight,
                           outputWidth, outputHeight, drawingImageIndex);
    imageIoBusy = false;
    Serial.printf("Image conversion %s in %lu ms.\n",
                  converted ? "complete" : "failed",
                  (unsigned long)(millis() - started));
    return converted;
}

void listImages() {
    Serial.println("Listing all files on SD card:");
    root = SD.open(IMAGE_DIRECTORY);
    imageCount = 0;
    if (!root || !root.isDirectory()) {
        Serial.println("Images directory is unavailable.");
        return;
    }
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        String fileName = entry.name();
        int slash = fileName.lastIndexOf('/');
        if (slash >= 0) fileName = fileName.substring(slash + 1);
        Serial.println(fileName);
        GalleryImageFormat format = galleryImageFormat(fileName);
        if (!entry.isDirectory() && format != GALLERY_IMAGE_NONE) {
            if (imageCount < MAX_IMAGE_FILES) {
                imageFiles[imageCount] = fileName;
                Serial.println("Added image: " + fileName);
                imageCount++;
            }
        }
        entry.close();
    }
    root.close();
    Serial.print("Total BMP/PNG/JPEG images found: ");
    Serial.println(imageCount);
}

static void precacheCompressedImages() {
    int compressedCount = 0;
    for (int index = 0; index < imageCount; ++index) {
        GalleryImageFormat format = galleryImageFormat(imageFiles[index]);
        if (format == GALLERY_IMAGE_PNG || format == GALLERY_IMAGE_JPEG) {
            ++compressedCount;
        }
    }
    if (compressedCount == 0) return;

    Serial1.println("VDPREP 0 Preparing images");
    Serial1.flush();
    int completed = 0;
    int savedImage = currentImage;
    for (int index = 0; index < imageCount; ++index) {
        GalleryImageFormat format = galleryImageFormat(imageFiles[index]);
        if (format != GALLERY_IMAGE_PNG && format != GALLERY_IMAGE_JPEG) continue;
        currentImage = index;
        String bmpPath;
        uint32_t sourceSignature = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        GalleryImageFormat detected = format;
        bool ready = ensureRenderableBmp(imageFiles[index], index, bmpPath,
                                         sourceSignature, detected,
                                         sourceWidth, sourceHeight);
        ++completed;
        int percent = 5 + (completed * 90 / compressedCount);
        Serial1.print("VDPROG ");
        Serial1.print(percent);
        Serial1.print(' ');
        Serial1.println(ready ? "Preparing images" : "Image conversion failed");
        Serial1.flush();
        Serial.printf("Pre-cache %s: %s\n", imageFiles[index].c_str(),
                      ready ? "ready" : imageConversionError);
        yield();
    }
    currentImage = constrain(savedImage, 0, max(0, imageCount - 1));
}

static void drawBMPLegacyUnused(String filename) {
    AppCursorDrawGuard cursorGuard;
    int drawingImageIndex = currentImage;
    String filepath = String("/Images/") + filename;
    File bmpFile = SD.open(filepath);
    if (!bmpFile) {
        Serial.println("❌ Failed to open image: " + filename);
        return;
    }
     String displayName = filename;
    if (displayName.length() > 14) {
        displayName = displayName.substring(0, 7) + "... .bmp";
    }
    videodisplay.fillRect(255+45, 0, 100, 200, 0);
    videodisplay.line(255+46,0,255+46,200,255);
    videodisplay.line(255+45,0,255+45,200,255);
    videodisplay.setCursor(255+50,160);
    videodisplay.println("Citadela");
    videodisplay.println("Gallery");
    videodisplay.setCursor(255+50,180);
    videodisplay.println(" < Navigate >");
    videodisplay.setCursor(255+50, 10);
    videodisplay.fillCircle(255+120, 165, 7, 255);
    videodisplay.setFont(Font6x8);
    videodisplay.println(displayName.c_str());
    int size = bmpFile.size();
    float fileSizeKB = size / 1024.0;
    char buffer[10];
    dtostrf(fileSizeKB, 6, 2, buffer);
    String siez = String(buffer) + " KB";
    videodisplay.println(siez.c_str());
    byte header[54];
    bmpFile.read(header, 54);
    uint32_t bmpWidth  = *(uint32_t*)&header[18];
    uint32_t bmpHeight = *(uint32_t*)&header[22];
    uint16_t bitDepth  = header[28];  
    if (bitDepth != 24 && bitDepth != 32) {
        Serial.println("Unsupported bit depth. Only 24-bit or 32-bit supported.");
        bmpFile.close();
        return;
    }
    uint16_t dispWidth  = min((uint16_t)bmpWidth, (uint16_t)TFT_WIDTH);
    uint16_t dispHeight = min((uint16_t)bmpHeight, (uint16_t)TFT_HEIGHT);
    videodisplay.rect(0, 0, dispWidth+1, dispHeight+1, 0);
    videodisplay.fillRect(dispWidth+1, 0, 255+45 - (dispWidth+1), 190, 255);
    videodisplay.fillRect(0, dispHeight+1, 255+45, 190 - (dispHeight+1), 255);
    uint32_t rowSize = ((bitDepth * bmpWidth + 31) / 32) * 4;
    int startRow = bmpHeight - dispHeight;
    uint16_t bytesPerPixel = (bitDepth == 32) ? 4 : 3;
    uint32_t rowBufferSize = rowSize;
    byte* rowBuffer = (byte*)malloc(rowBufferSize);
    if (!rowBuffer) {
        Serial.println("Memory allocation failed for row buffer.");
        bmpFile.close();
        return;
    }
    for (int y = 0; y < dispHeight; y++) {
        if (currentImage != drawingImageIndex) {
            Serial.println("⏩ Aborting current image draw. New image selected.");
            break;
        }
        int bmpRow = startRow + dispHeight - 1 - y;
        long rowPosition = bmpWidth + (bmpRow * rowSize);
        bmpFile.seek(rowPosition);
        int bytesRead = bmpFile.read(rowBuffer, rowBufferSize);
        if (bytesRead < (int)rowBufferSize) {
            Serial.printf("Row read error: Expected %d, got %d bytes. Padding missing data.\n", rowBufferSize, bytesRead);
            memset(rowBuffer + bytesRead, 0, rowBufferSize - bytesRead);
        }
        for (int x = 0; x < dispWidth; x++) {
            int idx = x * bytesPerPixel;
            if (idx + 2 >= (int)rowBufferSize) break;
            byte b = rowBuffer[idx];
            byte g = rowBuffer[idx + 1];
            byte r = rowBuffer[idx + 2];
            byte gray = (byte)(0.3 * r + 0.59 * g + 0.11 * b);
            gray = (gray < 128) ? (byte)(gray * 0.4) : (byte)(gray * 0.5);
            if (x < TFT_WIDTH && y < TFT_HEIGHT) {
                videodisplay.dot(x, y, gray);
            }
        }
    }
    free(rowBuffer);
    videodisplay.show();
    bmpFile.close();
    Serial.println("✅ Image displayed!");
}

static uint16_t bmpLE16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t bmpLE32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void drawImageStatus(const char *message, uint32_t color) {
    uint32_t background = videodisplay.RGB(8, 10, 12);
    videodisplay.fillRect(8, 78, TFT_WIDTH - 16, 24, background);
    videodisplay.rect(8, 78, TFT_WIDTH - 16, 24, color);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(color, background);
    videodisplay.setCursor(14, 86);
    videodisplay.print(message ? message : "Image error");
}

static bool imageFileReadExact(File &file, void *destination, size_t length) {
    return file.read((uint8_t*)destination, length) == (int)length;
}

static bool restoreImageRawCache(const String &path, uint32_t sourceSignature,
                                 int drawX, int drawY, int width, int height,
                                 int drawingImageIndex) {
    File cache = SD.open(path, FILE_READ);
    if (!cache) return false;
    ImageRawCacheHeader header;
    bool valid = imageFileReadExact(cache, &header, sizeof(header)) &&
                 header.magic == IMAGE_RAW_CACHE_MAGIC &&
                 header.sourceSignature == sourceSignature &&
                 header.calibrationSignature == videodisplay.calibrationSignature() &&
                 header.renderSignature == cimageRenderSignatureValue() &&
                 header.drawX == drawX && header.drawY == drawY &&
                 header.width == width && header.height == height &&
                 header.screenWidth == SCREEN_WIDTH &&
                 header.screenHeight == SCREEN_HEIGHT &&
                 cache.size() >= sizeof(header) + (size_t)width * height;
    if (!valid) {
        cache.close();
        return false;
    }

    uint8_t signalRow[TFT_WIDTH];
    uint32_t started = millis();
    for (int y = 0; y < height; ++y) {
        if (kernelExitRequested || currentImage != drawingImageIndex ||
            !imageFileReadExact(cache, signalRow, width)) {
            cache.close();
            return false;
        }
        AppCursorRawPixel *outputRow =
            videodisplay.backBuffer[videodisplay.graphics_swy(drawY + y)];
        for (int x = 0; x < width; ++x) {
            outputRow[videodisplay.graphics_swx(drawX + x)] =
                (AppCursorRawPixel)signalRow[x] << 8;
        }
    }
    cache.close();
    Serial.printf("Raw image cache restored in %lu ms.\n",
                  (unsigned long)(millis() - started));
    return true;
}

static bool saveImageRawCache(const String &path, uint32_t sourceSignature,
                              int drawX, int drawY, int width, int height) {
    if (!ensureImageCacheDirectory()) return false;
    SD.remove(path);
    File cache = SD.open(path, FILE_WRITE);
    if (!cache) return false;
    ImageRawCacheHeader header = {
        IMAGE_RAW_CACHE_MAGIC,
        sourceSignature,
        videodisplay.calibrationSignature(),
        cimageRenderSignatureValue(),
        (uint16_t)drawX,
        (uint16_t)drawY,
        (uint16_t)width,
        (uint16_t)height,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    };
    if (cache.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        cache.close();
        SD.remove(path);
        return false;
    }

    uint8_t signalRow[TFT_WIDTH];
    bool ok = true;
    for (int y = 0; ok && y < height; ++y) {
        AppCursorRawPixel *inputRow =
            videodisplay.backBuffer[videodisplay.graphics_swy(drawY + y)];
        for (int x = 0; x < width; ++x) {
            signalRow[x] = (uint8_t)(
                (inputRow[videodisplay.graphics_swx(drawX + x)] >> 8) & 0xff);
        }
        ok = cache.write(signalRow, width) == (size_t)width;
    }
    cache.flush();
    cache.close();
    if (!ok) SD.remove(path);
    return ok;
}

static void drawCImageTab(int y, const char *label, bool selected) {
    uint32_t panel = videodisplay.RGB(22, 31, 38);
    uint32_t selectedFill = videodisplay.RGB(35, 57, 65);
    uint32_t edge = selected ? videodisplay.RGB(75, 214, 201)
                             : videodisplay.RGB(70, 86, 92);
    uint32_t text = selected ? videodisplay.RGB(255, 255, 255)
                             : videodisplay.RGB(170, 188, 194);
    videodisplay.fillRect(SIDEBAR_X + 7, y, SIDEBAR_WIDTH - 14, 18,
                          selected ? selectedFill : panel);
    videodisplay.rect(SIDEBAR_X + 7, y, SIDEBAR_WIDTH - 14, 18, edge);
    videodisplay.setTextColor(text, selected ? selectedFill : panel);
    int textWidth = strlen(label) * 6;
    videodisplay.setCursor(SIDEBAR_X + (SIDEBAR_WIDTH - textWidth) / 2, y + 5);
    videodisplay.print(label);
}

static void drawCImageChrome(CImagePage page, const char *status) {
    uint32_t background = videodisplay.RGB(9, 13, 17);
    uint32_t header = videodisplay.RGB(16, 24, 30);
    uint32_t panel = videodisplay.RGB(22, 31, 38);
    uint32_t edge = videodisplay.RGB(75, 214, 201);
    uint32_t text = videodisplay.RGB(238, 244, 246);
    uint32_t muted = videodisplay.RGB(150, 176, 184);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 20, header);
    videodisplay.fillRect(0, 19, SCREEN_WIDTH, 1, edge);
    videodisplay.fillRect(SIDEBAR_X, 20, SIDEBAR_WIDTH, SCREEN_HEIGHT - 20, panel);
    videodisplay.fillRect(SIDEBAR_X, 20, 2, SCREEN_HEIGHT - 20, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(text, header);
    videodisplay.setCursor(8, 6);
    videodisplay.print("CIMAGE");
    videodisplay.setTextColor(muted, header);
    videodisplay.setCursor(58, 6);
    videodisplay.print(page == CIMAGE_PAGE_OPTIONS ? "DISPLAY OPTIONS" : "IMAGE GALLERY");
    if (page == CIMAGE_PAGE_GALLERY && imageCount > 0) {
        char counter[16];
        snprintf(counter, sizeof(counter), "%d / %d", currentImage + 1, imageCount);
        videodisplay.setTextColor(text, header);
        videodisplay.setCursor(238, 6);
        videodisplay.print(counter);
    }
    drawCImageTab(27, "GALLERY", page == CIMAGE_PAGE_GALLERY);
    drawCImageTab(49, "OPTIONS", page == CIMAGE_PAGE_OPTIONS);
    videodisplay.setTextColor(muted, panel);
    videodisplay.setCursor(SIDEBAR_X + 8, 78);
    videodisplay.print(status ? status : "Tab switch view");
}

static void drawCImageOptions() {
    AppCursorDrawGuard cursorGuard;
    drawCImageChrome(CIMAGE_PAGE_OPTIONS, "Saved instantly");
    uint32_t background = videodisplay.RGB(9, 13, 17);
    uint32_t rowFill = videodisplay.RGB(18, 28, 34);
    uint32_t selectedFill = videodisplay.RGB(35, 57, 65);
    uint32_t edge = videodisplay.RGB(75, 214, 201);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    uint32_t text = videodisplay.RGB(226, 236, 239);
    uint32_t muted = videodisplay.RGB(145, 168, 176);

    videodisplay.setTextColor(text, background);
    videodisplay.setCursor(10, 31);
    videodisplay.print("IMAGE SIZING");
    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(10, 43);
    videodisplay.print("Fit, fill or stretch the display area");

    static const char *modeNames[CIMAGE_SCALE_COUNT] = {"FIT", "FILL", "STRETCH"};
    for (int mode = 0; mode < CIMAGE_SCALE_COUNT; ++mode) {
        int x = 10 + mode * 87;
        bool active = cimageScaleMode == (CImageScaleMode)mode;
        uint32_t fill = active ? selectedFill : rowFill;
        uint32_t border = active ? edge : videodisplay.RGB(65, 82, 88);
        videodisplay.fillRect(x, 59, 81, 28, fill);
        videodisplay.rect(x, 59, 81, 28, border);
        videodisplay.setTextColor(active ? white : text, fill);
        int textWidth = strlen(modeNames[mode]) * 6;
        videodisplay.setCursor(x + (81 - textWidth) / 2, 69);
        videodisplay.print(modeNames[mode]);
    }
    if (cimageOptionSelection == 0) {
        videodisplay.rect(7, 55, 265, 36, white);
    }

    videodisplay.setTextColor(text, background);
    videodisplay.setCursor(10, 108);
    videodisplay.print("COLOUR OUTPUT");
    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(10, 120);
    videodisplay.print("Disable for a calibrated monochrome image");
    videodisplay.fillRect(10, 137, 112, 28,
                          cimageColourEnabled ? selectedFill : rowFill);
    videodisplay.rect(10, 137, 112, 28,
                      cimageColourEnabled ? edge : videodisplay.RGB(65, 82, 88));
    videodisplay.setTextColor(cimageColourEnabled ? white : text,
                              cimageColourEnabled ? selectedFill : rowFill);
    videodisplay.setCursor(22, 147);
    videodisplay.print(cimageColourEnabled ? "COLOUR  ON" : "COLOUR OFF");
    if (cimageOptionSelection == 1) {
        videodisplay.rect(7, 133, 118, 36, white);
    }

    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(144, 143);
    videodisplay.print("Up/Down select");
    videodisplay.setCursor(144, 155);
    videodisplay.print("Left/Right change");
    videodisplay.setTextColor(muted, videodisplay.RGB(22, 31, 38));
    videodisplay.setCursor(SIDEBAR_X + 8, 112);
    videodisplay.print("Sizing");
    videodisplay.setTextColor(white, videodisplay.RGB(22, 31, 38));
    videodisplay.setCursor(SIDEBAR_X + 8, 124);
    videodisplay.print(cimageScaleModeName(cimageScaleMode));
    videodisplay.setTextColor(muted, videodisplay.RGB(22, 31, 38));
    videodisplay.setCursor(SIDEBAR_X + 8, 142);
    videodisplay.print("Colour");
    videodisplay.setTextColor(white, videodisplay.RGB(22, 31, 38));
    videodisplay.setCursor(SIDEBAR_X + 8, 154);
    videodisplay.print(cimageColourEnabled ? "ON" : "OFF");
    videodisplay.show();
}

void drawBMP(String filename) {
    AppCursorDrawGuard cursorGuard;
    const int drawingImageIndex = currentImage;
    GalleryImageFormat sourceFormat = galleryImageFormat(filename);
    if (sourceFormat == GALLERY_IMAGE_PNG || sourceFormat == GALLERY_IMAGE_JPEG) {
        videodisplay.clear(videodisplay.RGB(12, 16, 20));
        drawImageStatus(sourceFormat == GALLERY_IMAGE_PNG
            ? "Converting PNG to BMP..." : "Converting JPEG to BMP...",
            videodisplay.RGB(75, 214, 201));
    }

    String bmpPath;
    uint32_t sourceSignature = 0;
    int originalWidth = 0;
    int originalHeight = 0;
    if (!ensureRenderableBmp(filename, drawingImageIndex, bmpPath,
                             sourceSignature, sourceFormat,
                             originalWidth, originalHeight)) {
        char displayError[44];
        snprintf(displayError, sizeof(displayError), "%s",
                 imageConversionError[0] ? imageConversionError
                                         : "Image conversion failed");
        drawImageStatus(displayError, videodisplay.RGB(255, 105, 95));
        return;
    }

    imageIoBusy = true;
    File bmpFile = SD.open(bmpPath, FILE_READ);
    if (!bmpFile) {
        imageIoBusy = false;
        Serial.println("Failed to open image: " + filename);
        drawImageStatus("Unable to open image", videodisplay.RGB(255, 105, 95));
        return;
    }

    uint8_t header[54];
    int headerRead = bmpFile.read(header, sizeof(header));
    if (headerRead != (int)sizeof(header) || header[0] != 'B' || header[1] != 'M') {
        bmpFile.close();
        imageIoBusy = false;
        drawImageStatus("Invalid BMP header", videodisplay.RGB(255, 105, 95));
        return;
    }

    uint32_t pixelOffset = bmpLE32(header + 10);
    uint32_t dibSize = bmpLE32(header + 14);
    int32_t signedWidth = (int32_t)bmpLE32(header + 18);
    int32_t signedHeight = (int32_t)bmpLE32(header + 22);
    uint16_t planes = bmpLE16(header + 26);
    uint16_t bitDepth = bmpLE16(header + 28);
    uint32_t compression = bmpLE32(header + 30);
    bool topDown = signedHeight < 0;
    uint32_t bmpWidth = signedWidth > 0 ? (uint32_t)signedWidth : 0;
    uint32_t bmpHeight = signedHeight < 0
        ? (uint32_t)(-(int64_t)signedHeight)
        : (uint32_t)signedHeight;

    if (dibSize < 40 || planes != 1 || compression != 0 ||
        (bitDepth != 24 && bitDepth != 32) || bmpWidth == 0 || bmpHeight == 0 ||
        bmpWidth > 8192 || bmpHeight > 8192) {
        Serial.printf("Unsupported BMP: %lux%lu bpp=%u compression=%lu\n",
                      (unsigned long)bmpWidth, (unsigned long)bmpHeight,
                      bitDepth, (unsigned long)compression);
        bmpFile.close();
        imageIoBusy = false;
        drawImageStatus("Use 24/32-bit BI_RGB", videodisplay.RGB(255, 198, 72));
        return;
    }

    uint32_t bytesPerPixel = bitDepth / 8U;
    uint32_t rowSize = ((bmpWidth * bitDepth + 31U) / 32U) * 4U;
    uint64_t requiredEnd = (uint64_t)pixelOffset + (uint64_t)rowSize * bmpHeight;
    if (rowSize == 0 || rowSize > 32768 || requiredEnd > (uint64_t)bmpFile.size()) {
        bmpFile.close();
        imageIoBusy = false;
        drawImageStatus("BMP data is truncated", videodisplay.RGB(255, 105, 95));
        return;
    }

    float scaleX = (float)IMAGE_VIEW_WIDTH / (float)bmpWidth;
    float scaleY = (float)IMAGE_VIEW_HEIGHT / (float)bmpHeight;
    int scaledWidth = IMAGE_VIEW_WIDTH;
    int scaledHeight = IMAGE_VIEW_HEIGHT;
    int cropX = 0;
    int cropY = 0;
    int dispWidth = IMAGE_VIEW_WIDTH;
    int dispHeight = IMAGE_VIEW_HEIGHT;
    int drawX = IMAGE_VIEW_X;
    int drawY = IMAGE_VIEW_Y;
    if (cimageScaleMode == CIMAGE_SCALE_FIT) {
        float scale = min(scaleX, scaleY);
        scaledWidth = max(1, (int)(bmpWidth * scale + 0.5f));
        scaledHeight = max(1, (int)(bmpHeight * scale + 0.5f));
        dispWidth = min(IMAGE_VIEW_WIDTH, scaledWidth);
        dispHeight = min(IMAGE_VIEW_HEIGHT, scaledHeight);
        drawX = IMAGE_VIEW_X + (IMAGE_VIEW_WIDTH - dispWidth) / 2;
        drawY = IMAGE_VIEW_Y + (IMAGE_VIEW_HEIGHT - dispHeight) / 2;
    } else if (cimageScaleMode == CIMAGE_SCALE_FILL) {
        float scale = max(scaleX, scaleY);
        scaledWidth = max(1, (int)(bmpWidth * scale + 0.5f));
        scaledHeight = max(1, (int)(bmpHeight * scale + 0.5f));
        cropX = max(0, (scaledWidth - IMAGE_VIEW_WIDTH) / 2);
        cropY = max(0, (scaledHeight - IMAGE_VIEW_HEIGHT) / 2);
    }

    uint32_t background = videodisplay.RGB(9, 13, 17);
    uint32_t panel = videodisplay.RGB(22, 31, 38);
    uint32_t edge = videodisplay.RGB(75, 214, 201);
    uint32_t text = videodisplay.RGB(238, 244, 246);
    drawCImageChrome(CIMAGE_PAGE_GALLERY, cimageScaleModeName(cimageScaleMode));
    videodisplay.fillRect(IMAGE_VIEW_X, IMAGE_VIEW_Y,
                          IMAGE_VIEW_WIDTH, IMAGE_VIEW_HEIGHT,
                          videodisplay.RGB(0, 0, 0));
    videodisplay.rect(IMAGE_VIEW_X - 1, IMAGE_VIEW_Y - 1,
                      IMAGE_VIEW_WIDTH + 2, IMAGE_VIEW_HEIGHT + 2, edge);
    if (cimageScaleMode == CIMAGE_SCALE_FIT) {
        videodisplay.rect(drawX, drawY, dispWidth, dispHeight,
                          videodisplay.RGB(64, 82, 88));
    }
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(text, panel);
    videodisplay.setCursor(SIDEBAR_X + 7, 94);
    String displayName = filename;
    if (displayName.length() > 13) displayName = displayName.substring(0, 10) + "...";
    videodisplay.println(displayName.c_str());
    videodisplay.setTextColor(videodisplay.RGB(150, 176, 184), panel);
    videodisplay.setCursor(SIDEBAR_X + 7, 106);
    videodisplay.print((unsigned long)(originalWidth > 0 ? originalWidth : bmpWidth));
    videodisplay.print("x");
    videodisplay.println((unsigned long)(originalHeight > 0 ? originalHeight : bmpHeight));
    if (sourceFormat == GALLERY_IMAGE_PNG) videodisplay.println("PNG -> BMP");
    else if (sourceFormat == GALLERY_IMAGE_JPEG) videodisplay.println("JPEG -> BMP");
    else {
        videodisplay.print(bitDepth);
        videodisplay.println("-bit RGB");
    }
    videodisplay.setCursor(SIDEBAR_X + 7, 158);
    videodisplay.println("< > browse");
    videodisplay.println("Esc home");
    videodisplay.println("D   delete");

    String rawCachePath = imageCachePath(sourceSignature, "raw");
    if (restoreImageRawCache(rawCachePath, sourceSignature,
                             drawX, drawY, dispWidth, dispHeight,
                             drawingImageIndex)) {
        bmpFile.close();
        imageIoBusy = false;
        videodisplay.show();
        Serial.printf("Fast cached image render complete: %s\n", filename.c_str());
        return;
    }

    uint8_t *rowBuffer = (uint8_t*)malloc(rowSize);
    if (!rowBuffer) {
        bmpFile.close();
        imageIoBusy = false;
        drawImageStatus("Not enough row memory", videodisplay.RGB(255, 105, 95));
        return;
    }

    uint16_t sourceXMap[TFT_WIDTH];
    for (int x = 0; x < dispWidth; ++x) {
        int scaledX = x + cropX;
        sourceXMap[x] = (uint16_t)min((uint32_t)bmpWidth - 1U,
            (uint32_t)((uint64_t)scaledX * bmpWidth / max(1, scaledWidth)));
    }

    bool completed = true;
    uint32_t renderStarted = millis();
    for (int y = 0; y < dispHeight; ++y) {
        if (kernelExitRequested || currentImage != drawingImageIndex) {
            completed = false;
            break;
        }
        int scaledY = y + cropY;
        uint32_t sourceY = min((uint32_t)bmpHeight - 1U,
            (uint32_t)((uint64_t)scaledY * bmpHeight / max(1, scaledHeight)));
        uint32_t fileRow = topDown ? sourceY : (bmpHeight - 1U - sourceY);
        if (!bmpFile.seek(pixelOffset + fileRow * rowSize)) {
            completed = false;
            break;
        }
        int bytesRead = bmpFile.read(rowBuffer, rowSize);
        if (bytesRead < (int)rowSize) {
            int validBytes = bytesRead > 0 ? bytesRead : 0;
            memset(rowBuffer + validBytes, 0, rowSize - validBytes);
        }
        AppCursorRawPixel *outputRow =
            videodisplay.backBuffer[videodisplay.graphics_swy(drawY + y)];
        for (int x = 0; x < dispWidth; ++x) {
            uint32_t sourceX = sourceXMap[x];
            uint32_t index = sourceX * bytesPerPixel;
            uint8_t b = rowBuffer[index];
            uint8_t g = rowBuffer[index + 1];
            uint8_t r = rowBuffer[index + 2];
            if (!cimageColourEnabled) {
                uint8_t gray = (uint8_t)(((uint16_t)r * 77U +
                                          (uint16_t)g * 150U +
                                          (uint16_t)b * 29U) >> 8);
                r = gray;
                g = gray;
                b = gray;
            }
            uint32_t color = videodisplay.RGB(r, g, b);
            uint8_t signal = (uint8_t)(videodisplay.coltobuf(
                color, drawX + x, drawY + y) & 0xff);
            outputRow[videodisplay.graphics_swx(drawX + x)] =
                (AppCursorRawPixel)signal << 8;
        }
        if ((y & 15) == 0) yield();
    }
    free(rowBuffer);
    bmpFile.close();
    if (completed) {
        saveImageRawCache(rawCachePath, sourceSignature,
                          drawX, drawY, dispWidth, dispHeight);
    }
    imageIoBusy = false;
    videodisplay.show();
    Serial.printf("Fast BMP colour render %s in %lu ms: %s (%lux%lu, %u-bit)\n",
                  completed ? "complete" : "cancelled",
                  (unsigned long)(millis() - renderStarted), filename.c_str(),
                  (unsigned long)bmpWidth, (unsigned long)bmpHeight, bitDepth);
}

void deleteCurrentImage() {
    if (imageCount == 0) return;
    if (imageIoBusy) {
        Serial.println("Image is still loading; delete ignored.");
        return;
    }
    Serial.println("Delete this image? (Y/N)");
    uint32_t dialog = videodisplay.RGB(255, 205, 82);
    videodisplay.fillRect(SIDEBAR_X + 4,30,SCREEN_WIDTH - SIDEBAR_X - 8,50,dialog);
    videodisplay.setCursor(SIDEBAR_X + 7,34);
    videodisplay.setTextColor(videodisplay.RGB(0,0,0),dialog);
    videodisplay.println("Delete image?");
    videodisplay.println("Y/N");
    while (!Serial1.available());
    char response = Serial1.read();
    if (response == 'y') {
        String filepath = String(IMAGE_DIRECTORY) + "/" + imageFiles[currentImage];
        GalleryImageFormat format = galleryImageFormat(imageFiles[currentImage]);
        uint32_t signature = imageRenderableSignature(imageSourceSignature(filepath), format);
        if (SD.remove(filepath)) {
            SD.remove(imageCachePath(signature, "bmp"));
            SD.remove(imageCachePath(signature, "raw"));
            Serial.println("✅ Image deleted: " + imageFiles[currentImage]);
            listImages();
            currentImage = 0;
        } else {
            Serial.println("❌ Failed to delete image.");
        }
    } else {
        Serial.println("Image deletion canceled.");
    }
}

static void setCImagePage(CImagePage page) {
    if (cimagePage == page) return;
    cimagePage = page;
    requestCImageRedraw();
}

static void setCImageScaleMode(CImageScaleMode mode) {
    if (mode >= CIMAGE_SCALE_COUNT || cimageScaleMode == mode) return;
    cimageScaleMode = mode;
    saveCImagePreferences();
    requestCImageRedraw();
    Serial.printf("CImage sizing changed to %s\n", cimageScaleModeName(mode));
}

static void stepCImageScaleMode(int direction) {
    int next = ((int)cimageScaleMode + direction + CIMAGE_SCALE_COUNT) % CIMAGE_SCALE_COUNT;
    setCImageScaleMode((CImageScaleMode)next);
}

static void setCImageColourEnabled(bool enabled) {
    if (cimageColourEnabled == enabled) return;
    cimageColourEnabled = enabled;
    saveCImagePreferences();
    requestCImageRedraw();
    Serial.printf("CImage colour changed to %s\n", enabled ? "on" : "off");
}

static bool cimagePointInside(int x, int y, int left, int top, int width, int height) {
    return x >= left && y >= top && x < left + width && y < top + height;
}

static bool installRunningCImageToSd() {
    if (imageIoBusy) {
        Serial.println("CImage install skipped: image I/O is busy.");
        return false;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    size_t imageSize = ESP.getSketchSize();
    if (!running || imageSize == 0 || imageSize > running->size) {
        Serial.println("CImage install failed: invalid running partition.");
        return false;
    }

    const char *targetPath = "/apps/CImage.bin";
    const char *temporaryPath = "/apps/.CImage.installing";
    const char *backupPath = "/apps/.CImage.previous";
    SD.remove(temporaryPath);
    File output = SD.open(temporaryPath, FILE_WRITE);
    if (!output) {
        Serial.println("CImage install failed: cannot create temporary file.");
        return false;
    }

    uint8_t *buffer = (uint8_t*)malloc(4096);
    bool ok = buffer != nullptr;
    size_t copied = 0;
    while (ok && copied < imageSize) {
        size_t chunk = min((size_t)4096, imageSize - copied);
        ok = esp_partition_read(running, copied, buffer, chunk) == ESP_OK &&
             output.write(buffer, chunk) == chunk;
        if (ok) copied += chunk;
        if ((copied & 0x7fff) == 0) yield();
    }
    if (buffer) free(buffer);
    output.flush();
    output.close();

    File verification = SD.open(temporaryPath, FILE_READ);
    ok = ok && verification && verification.size() == imageSize;
    if (verification) verification.close();
    if (!ok) {
        SD.remove(temporaryPath);
        Serial.printf("CImage install failed after %lu/%lu bytes.\n",
                      (unsigned long)copied, (unsigned long)imageSize);
        return false;
    }

    SD.remove(backupPath);
    bool hadExisting = SD.exists(targetPath);
    if (hadExisting && !SD.rename(targetPath, backupPath)) {
        SD.remove(temporaryPath);
        Serial.println("CImage install failed: cannot preserve old app.");
        return false;
    }
    if (!SD.rename(temporaryPath, targetPath)) {
        if (hadExisting) SD.rename(backupPath, targetPath);
        SD.remove(temporaryPath);
        Serial.println("CImage install failed: cannot activate new app.");
        return false;
    }
    SD.remove(backupPath);
    Serial.printf("CImage installed to SD: %lu bytes.\n", (unsigned long)imageSize);
    return true;
}

static bool handleImageMouse(String command) {
    AppMouseReport mouse;
    if (!appMouseRead(command, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (mouse.leftPressed && cimagePointInside(mouse.x, mouse.y,
                                               SIDEBAR_X + 7, 27,
                                               SIDEBAR_WIDTH - 14, 18)) {
        setCImagePage(CIMAGE_PAGE_GALLERY);
    } else if (mouse.leftPressed && cimagePointInside(mouse.x, mouse.y,
                                                      SIDEBAR_X + 7, 49,
                                                      SIDEBAR_WIDTH - 14, 18)) {
        setCImagePage(CIMAGE_PAGE_OPTIONS);
    } else if (cimagePage == CIMAGE_PAGE_OPTIONS && mouse.leftPressed) {
        if (cimagePointInside(mouse.x, mouse.y, 10, 59, 255, 28)) {
            int mode = constrain((mouse.x - 10) / 87, 0, CIMAGE_SCALE_COUNT - 1);
            setCImageScaleMode((CImageScaleMode)mode);
        } else if (cimagePointInside(mouse.x, mouse.y, 10, 137, 112, 28)) {
            setCImageColourEnabled(!cimageColourEnabled);
        }
    } else if (cimagePage == CIMAGE_PAGE_GALLERY && imageCount > 0) {
        if (mouse.wheel > 0) {
            currentImage = (currentImage - 1 + imageCount) % imageCount;
            requestCImageRedraw();
        } else if (mouse.wheel < 0) {
            currentImage = (currentImage + 1) % imageCount;
            requestCImageRedraw();
        }
        if (mouse.leftPressed && mouse.x < SIDEBAR_X) {
            if (appCursorX < SIDEBAR_X / 2) {
                currentImage = (currentImage - 1 + imageCount) % imageCount;
            } else {
                currentImage = (currentImage + 1) % imageCount;
            }
            requestCImageRedraw();
        }
    }
    appCursorCaptureAndDraw();
    return true;
}

static bool readCImageCommand(HardwareSerial &port, String &command) {
    if (!port.available()) return false;
    command = port.readStringUntil('\n');
    command.trim();
    return command.length() > 0;
}

static void handleCImageCommand(const String &command) {
    if (handleImageMouse(command)) return;
    AppCursorDrawGuard cursorGuard;
    if (command == "SYNCAPP") {
        installRunningCImageToSd();
    } else if (command == "Tab") {
        setCImagePage(cimagePage == CIMAGE_PAGE_GALLERY
                      ? CIMAGE_PAGE_OPTIONS : CIMAGE_PAGE_GALLERY);
    } else if (command == "o" || command == "O" || command == "Options") {
        setCImagePage(CIMAGE_PAGE_OPTIONS);
    } else if (command == "g" || command == "G" || command == "Gallery") {
        setCImagePage(CIMAGE_PAGE_GALLERY);
    } else if (cimagePage == CIMAGE_PAGE_OPTIONS) {
        if (command == "UpArrow" || command == "DownArrow") {
            cimageOptionSelection = cimageOptionSelection == 0 ? 1 : 0;
            requestCImageRedraw();
        } else if (command == "LeftArrow") {
            if (cimageOptionSelection == 0) stepCImageScaleMode(-1);
            else setCImageColourEnabled(!cimageColourEnabled);
        } else if (command == "RightArrow" || command == "Enter") {
            if (cimageOptionSelection == 0) stepCImageScaleMode(1);
            else setCImageColourEnabled(!cimageColourEnabled);
        } else if (command == "Escape") {
            setCImagePage(CIMAGE_PAGE_GALLERY);
        }
    } else if (command == "LeftArrow" && imageCount > 0) {
        currentImage = (currentImage - 1 + imageCount) % imageCount;
        requestCImageRedraw();
    } else if (command == "RightArrow" && imageCount > 0) {
        currentImage = (currentImage + 1) % imageCount;
        requestCImageRedraw();
    } else if (command == "d" || command == "D") {
        deleteCurrentImage();
        requestCImageRedraw();
    } else if (command == "Escape") {
        kernelExitRequested = true;
    }
}

void serialTask(void *parameter) {
    for (;;) {
        if (kernelExitRequested) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        String command;
        if (readCImageCommand(Serial1, command) || readCImageCommand(Serial, command)) {
            Serial.println(command);
            handleCImageCommand(command);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
void setup() {
    pinMode(SPEAKER_PIN, 0);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
    Serial1.println("res");
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        SPIFFS.format();
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed Again");
            return;
        }
    }
    SPI.begin(18,19,23,5);
    if(!SD.begin(5)){
      Serial.println("SD Failed to init");
    } else {
      Serial.println("EUREKA");
    }
    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == ""){
        boolRes = "false";
        boolUpdate();
    }
    if (boolRes == "false"){
      tone(SPEAKER_PIN, 0);
      loadCImagePreferences();
      listImages();
      precacheCompressedImages();
      bool videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
      Serial.printf("CImage video init: %s, heap=%u, largest=%u\n",
                    videoReady ? "ready" : "failed", ESP.getFreeHeap(),
                    heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      if (!videoReady) restartWithFallbackVideo("CImage video failed");
      releaseSerialControllerVideo();
      xTaskCreatePinnedToCore(
        serialTask,
        "SerialTask",
        4096,
        NULL,
        1,
        NULL,
        0
      );
    } else if (boolRes == "trueKernel") {
      tone(SPEAKER_PIN, 0);
      boolRes = "false";
      boolUpdate();
      if (!handleKernelFlash()) restartWithFallbackVideo("Kernel flash failed");
    } else {
      tone(SPEAKER_PIN,0);
      boolRes = "false";
      boolUpdate();
    }
}

void loop() {
  static int lastDrawnImage = -1;
  static String lastDrawnName = "";
  static CImagePage lastDrawnPage = (CImagePage)255;
  static uint32_t lastRenderRevision = 0;
  if (kernelExitRequested) {
    returnToKernelNow();
    return;
  }
  uint32_t revisionToDraw = cimageRenderRevision;
  CImagePage pageToDraw = cimagePage;
  if (pageToDraw == CIMAGE_PAGE_OPTIONS) {
    if (lastDrawnPage != pageToDraw || lastRenderRevision != revisionToDraw) {
      drawCImageOptions();
      lastDrawnPage = pageToDraw;
      lastRenderRevision = revisionToDraw;
    }
  } else if (imageCount > 0) {
    int imageToDraw = currentImage;
    String imageToDrawName = imageFiles[imageToDraw];
    if (lastDrawnPage != pageToDraw || imageToDraw != lastDrawnImage ||
        imageToDrawName != lastDrawnName || lastRenderRevision != revisionToDraw) {
      drawBMP(imageToDrawName);
      lastDrawnImage = imageToDraw;
      lastDrawnName = imageToDrawName;
      lastDrawnPage = pageToDraw;
      lastRenderRevision = revisionToDraw;
    }
  } else if (lastDrawnPage != pageToDraw || lastRenderRevision != revisionToDraw) {
    AppCursorDrawGuard cursorGuard;
    drawCImageChrome(CIMAGE_PAGE_GALLERY, "No images found");
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(videodisplay.RGB(226, 236, 239),
                              videodisplay.RGB(9, 13, 17));
    videodisplay.setCursor(72, 94);
    videodisplay.print("ADD BMP, PNG OR JPEG TO /IMAGES");
    videodisplay.show();
    lastDrawnPage = pageToDraw;
    lastRenderRevision = revisionToDraw;
  }
  delay(20);
}
