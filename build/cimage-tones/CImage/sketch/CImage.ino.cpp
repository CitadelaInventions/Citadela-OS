#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
#include <SPI.h>
#include <SD.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include "SPIFFS.h"
#include "FS.h"
#include <Update.h>
#include "esp_partition.h"
#include <Wire.h>

#define SPEAKER_PIN 12
#define SCREEN_WIDTH 376
#define SCREEN_HEIGHT 192
#define TFT_WIDTH  280
#define TFT_HEIGHT 176
#define SIDEBAR_X 284

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

#line 56 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorLoadConfigOnce();
#line 77 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 82 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 87 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 92 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 97 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 101 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 107 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorRestore();
#line 118 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 125 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorCaptureAndDraw();
#line 157 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void appCursorRefreshAfterRedraw();
#line 167 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 188 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void restartWithFallbackVideo(const char *label);
#line 205 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void releaseSerialControllerVideo();
#line 228 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 239 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 244 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 253 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void boolTru();
#line 262 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void boolUpdate();
#line 273 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
bool handleKernelFlash();
#line 334 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void returnToKernelNow();
#line 353 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void listImages();
#line 385 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void drawBMPLegacyUnused(String filename);
#line 473 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static uint16_t bmpLE16(const uint8_t *data);
#line 477 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static uint32_t bmpLE32(const uint8_t *data);
#line 484 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static void drawImageStatus(const char *message, uint32_t color);
#line 494 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void drawBMP(String filename);
#line 623 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void deleteCurrentImage();
#line 647 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
static bool handleImageMouse(String command);
#line 667 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void serialTask(void *parameter);
#line 695 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void setup();
#line 748 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
void loop();
#line 56 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CImage\\CImage.ino"
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

String imageFiles[20];
String boolRes = "";
String appName = "";
String appString = "";

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

void listImages() {
    videodisplay.clear(videodisplay.RGB(12, 16, 20));
    Serial.println("Listing all files on SD card:");
    root = SD.open("/Images/");
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
        String lowerName = fileName;
        lowerName.toLowerCase();
        if (!entry.isDirectory() && lowerName.endsWith(".bmp")) {
            if (imageCount < 20) {
                imageFiles[imageCount] = fileName;
                Serial.println("Added BMP: " + fileName);
                imageCount++;
            }
        }
        entry.close();
    }
    root.close();
    Serial.print("Total BMP images found: ");
    Serial.println(imageCount);
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

void drawBMP(String filename) {
    AppCursorDrawGuard cursorGuard;
    const int drawingImageIndex = currentImage;
    File bmpFile = SD.open(String("/Images/") + filename, FILE_READ);
    if (!bmpFile) {
        Serial.println("Failed to open image: " + filename);
        drawImageStatus("Unable to open BMP", videodisplay.RGB(255, 105, 95));
        return;
    }

    uint8_t header[54];
    int headerRead = bmpFile.read(header, sizeof(header));
    if (headerRead != (int)sizeof(header) || header[0] != 'B' || header[1] != 'M') {
        bmpFile.close();
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
        drawImageStatus("Use 24/32-bit BI_RGB", videodisplay.RGB(255, 198, 72));
        return;
    }

    uint32_t bytesPerPixel = bitDepth / 8U;
    uint32_t rowSize = ((bmpWidth * bitDepth + 31U) / 32U) * 4U;
    uint64_t requiredEnd = (uint64_t)pixelOffset + (uint64_t)rowSize * bmpHeight;
    if (rowSize == 0 || rowSize > 32768 || requiredEnd > (uint64_t)bmpFile.size()) {
        bmpFile.close();
        drawImageStatus("BMP data is truncated", videodisplay.RGB(255, 105, 95));
        return;
    }

    float scaleX = (float)(TFT_WIDTH - 4) / (float)bmpWidth;
    float scaleY = (float)(TFT_HEIGHT - 4) / (float)bmpHeight;
    float scale = min(1.0f, min(scaleX, scaleY));
    int dispWidth = max(1, (int)(bmpWidth * scale + 0.5f));
    int dispHeight = max(1, (int)(bmpHeight * scale + 0.5f));
    int drawX = (TFT_WIDTH - dispWidth) / 2;
    int drawY = (TFT_HEIGHT - dispHeight) / 2;

    uint32_t background = videodisplay.RGB(12, 16, 20);
    uint32_t panel = videodisplay.RGB(22, 31, 38);
    uint32_t edge = videodisplay.RGB(75, 214, 201);
    uint32_t text = videodisplay.RGB(238, 244, 246);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, videodisplay.RGB(0, 0, 0));
    videodisplay.rect(drawX - 1, drawY - 1, dispWidth + 2, dispHeight + 2, edge);
    videodisplay.fillRect(SIDEBAR_X, 0, SCREEN_WIDTH - SIDEBAR_X, SCREEN_HEIGHT, panel);
    videodisplay.fillRect(SIDEBAR_X, 0, 2, SCREEN_HEIGHT, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(text, panel);
    videodisplay.setCursor(SIDEBAR_X + 7, 10);
    String displayName = filename;
    if (displayName.length() > 13) displayName = displayName.substring(0, 10) + "...";
    videodisplay.println(displayName.c_str());
    videodisplay.setTextColor(videodisplay.RGB(150, 176, 184), panel);
    videodisplay.print((unsigned long)bmpWidth);
    videodisplay.print("x");
    videodisplay.println((unsigned long)bmpHeight);
    videodisplay.print(bitDepth);
    videodisplay.println("-bit RGB");
    videodisplay.print((unsigned long)((bmpFile.size() + 1023) / 1024));
    videodisplay.println(" KB");
    videodisplay.setCursor(SIDEBAR_X + 7, 130);
    videodisplay.println("< > browse");
    videodisplay.println("D   delete");
    videodisplay.println("Esc home");

    uint8_t *rowBuffer = (uint8_t*)malloc(rowSize);
    if (!rowBuffer) {
        bmpFile.close();
        drawImageStatus("Not enough row memory", videodisplay.RGB(255, 105, 95));
        return;
    }

    bool completed = true;
    for (int y = 0; y < dispHeight; ++y) {
        if (kernelExitRequested || currentImage != drawingImageIndex) {
            completed = false;
            break;
        }
        uint32_t sourceY = (uint32_t)((uint64_t)y * bmpHeight / dispHeight);
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
        for (int x = 0; x < dispWidth; ++x) {
            uint32_t sourceX = (uint32_t)((uint64_t)x * bmpWidth / dispWidth);
            uint32_t index = sourceX * bytesPerPixel;
            uint8_t b = rowBuffer[index];
            uint8_t g = rowBuffer[index + 1];
            uint8_t r = rowBuffer[index + 2];
            videodisplay.dotFast(drawX + x, drawY + y, videodisplay.RGB(r, g, b));
        }
        if ((y & 15) == 0) yield();
    }
    free(rowBuffer);
    bmpFile.close();
    videodisplay.show();
    Serial.printf("BMP colour render %s: %s (%lux%lu, %u-bit)\n",
                  completed ? "complete" : "cancelled", filename.c_str(),
                  (unsigned long)bmpWidth, (unsigned long)bmpHeight, bitDepth);
}

void deleteCurrentImage() {
    if (imageCount == 0) return;
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
        String filepath = String("/Images/") + imageFiles[currentImage];
        if (SD.remove(filepath)) {
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
static bool handleImageMouse(String command) {
    AppMouseReport mouse;
    if (!appMouseRead(command, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (imageCount > 0) {
        if (mouse.wheel > 0) {
            currentImage = (currentImage - 1 + imageCount) % imageCount;
        } else if (mouse.wheel < 0) {
            currentImage = (currentImage + 1) % imageCount;
        }
        if (mouse.leftPressed) {
            if (appCursorX < APP_CURSOR_SCREEN_W / 2) currentImage = (currentImage - 1 + imageCount) % imageCount;
            else currentImage = (currentImage + 1) % imageCount;
        }
    }
    appCursorCaptureAndDraw();
    return true;
}

void serialTask(void *parameter) {
    for (;;) {
        if (kernelExitRequested) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }
        if (Serial1.available()) {
            String command = Serial1.readStringUntil('\n');
            command.trim();
            Serial.println(command);
            if (handleImageMouse(command)) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }
            AppCursorDrawGuard cursorGuard;
            if (command == "LeftArrow" && imageCount > 0) {
                currentImage = (currentImage - 1 + imageCount) % imageCount;
            } else if (command == "RightArrow" && imageCount > 0) {
                currentImage = (currentImage + 1) % imageCount;
            } else if (command == "d") {
                deleteCurrentImage();
            } else if (command == "Escape"){
                kernelExitRequested = true;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
void setup() {
    pinMode(SPEAKER_PIN, 0);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
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
      videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
      releaseSerialControllerVideo();
      tone(SPEAKER_PIN, 0);
      listImages();
      xTaskCreatePinnedToCore(
        serialTask,
        "SerialTask",
        2048,
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
  if (kernelExitRequested) {
    returnToKernelNow();
    return;
  }
  if (imageCount > 0) {
    int imageToDraw = currentImage;
    String imageToDrawName = imageFiles[imageToDraw];
    if (imageToDraw != lastDrawnImage || imageToDrawName != lastDrawnName) {
      drawBMP(imageToDrawName);
      lastDrawnImage = imageToDraw;
      lastDrawnName = imageToDrawName;
    }
  }
  delay(20);
}

