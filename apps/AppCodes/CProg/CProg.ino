#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <esp_ota_ops.h>
#include "esp_partition.h"
#include "FS.h"
#include "SPIFFS.h"

#define MAX_LINES 100
#define VISIBLE_LINES 14
#define MAX_LINE_CHARS 52
#define SPEAKER_PIN 12

static const int SCREEN_WIDTH = 376;
static const int SCREEN_HEIGHT = 192;
static const int EDITOR_X = 4;
static const int EDITOR_Y = 45;
static const int EDITOR_W = 368;
static const int LINE_HEIGHT = 9;

CompositeColorDAC videodisplay;

static int bufferStart = 0;
static int bufferEnd = 0;
static int cursorColumn = 0;
static int documentLineCount = 1;
static int selectedTool = 0;
static int tabWidth = 2;
static int transmitProgress = 0;
static bool capsLock = false;
static bool flashing = false;
static bool toolbarFocused = false;
static bool optionsOpen = false;
static bool editorDirty = false;
static String boolRes = "";
static String editorStatus = "READY";
static String textBuffer[MAX_LINES];

static const int APP_CURSOR_SCREEN_W = SCREEN_WIDTH;
static const int APP_CURSOR_SCREEN_H = SCREEN_HEIGHT;
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

static inline AppCursorRawPixel appCursorGetRawPixel(int x, int y);
static inline void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
static bool appMouseRead(String input, AppMouseReport &report);
static void redrawScreen();
static void spitFirmware();
static void save();
static void flash();
static void opt();

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
    File file = SPIFFS.open("/systemConfiguration.conf", FILE_READ);
    if (!file) return;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        int equals = line.indexOf('=');
        if (equals <= 0) continue;
        String key = line.substring(0, equals);
        String value = line.substring(equals + 1);
        key.trim();
        value.trim();
        if (key == "MouseCursorOutlined" || key == "CursorOutlineMode")
            appMouseCursorOutlined = value.toInt() != 0;
    }
    file.close();
}

static inline AppCursorRawPixel appCursorGetRawPixel(int x, int y) {
    if (!videodisplay.backBuffer ||
        (unsigned int)x >= (unsigned int)videodisplay.xres ||
        (unsigned int)y >= (unsigned int)videodisplay.yres) return 0;
    return videodisplay.backBuffer[videodisplay.graphics_swy(y)]
                                  [videodisplay.graphics_swx(x)];
}

static inline void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw) {
    if (!videodisplay.backBuffer ||
        (unsigned int)x >= (unsigned int)videodisplay.xres ||
        (unsigned int)y >= (unsigned int)videodisplay.yres) return;
    videodisplay.backBuffer[videodisplay.graphics_swy(y)]
                           [videodisplay.graphics_swx(x)] = raw;
}

static bool appCursorInnerPoint(int dx, int dy) {
    int distanceSquared = dx * dx + dy * dy;
    return distanceSquared >= 5 && distanceSquared <= 10;
}

static bool appCursorOutlinePoint(int dx, int dy) {
    int distanceSquared = dx * dx + dy * dy;
    return distanceSquared > 10 && distanceSquared <= 18;
}

static bool appCursorCapturePoint(int dx, int dy) {
    return appCursorInnerPoint(dx, dy) ||
           (appMouseCursorOutlined && appCursorOutlinePoint(dx, dy));
}

static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw) {
    uint8_t signal = (uint8_t)((raw >> 8) & 0xff);
    int threshold = (videodisplay.levelBlack + videodisplay.levelWhite) / 2;
    return signal >= threshold;
}

static void appCursorRestore() {
    if (!appCursorDrawn) return;
    for (int i = 0; i < appCursorPixelCount; ++i) {
        appCursorSetRawPixel(appCursorX + appCursorPixels[i].dx,
                             appCursorY + appCursorPixels[i].dy,
                             appCursorPixels[i].raw);
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
    int radius = appMouseCursorOutlined
        ? APP_CURSOR_OUTLINE_RADIUS : APP_CURSOR_RADIUS;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (!appCursorCapturePoint(dx, dy)) continue;
            int x = appCursorX + dx;
            int y = appCursorY + dy;
            if ((unsigned int)x >= APP_CURSOR_SCREEN_W ||
                (unsigned int)y >= APP_CURSOR_SCREEN_H ||
                appCursorPixelCount >= APP_CURSOR_MAX_PIXELS) continue;
            appCursorPixels[appCursorPixelCount] = {
                (int8_t)dx, (int8_t)dy, appCursorGetRawPixel(x, y)
            };
            ++appCursorPixelCount;
        }
    }
    uint32_t white = videodisplay.RGB(255, 255, 255);
    uint32_t black = videodisplay.RGB(0, 0, 0);
    for (int i = 0; i < appCursorPixelCount; ++i) {
        int x = appCursorX + appCursorPixels[i].dx;
        int y = appCursorY + appCursorPixels[i].dy;
        bool outline = appCursorOutlinePoint(appCursorPixels[i].dx,
                                             appCursorPixels[i].dy);
        uint32_t color = appMouseCursorOutlined
            ? (outline ? white : black)
            : (appCursorBackgroundLooksBright(appCursorPixels[i].raw)
                ? black : white);
        videodisplay.dotFast(x, y, color);
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
    int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d",
                        &report.x, &report.y, &report.buttons,
                        &report.dx, &report.dy, &report.wheel);
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

class SerialLogger : public Print {
  public:
    size_t write(uint8_t character) override {
        Serial2.write(character);
        Serial.write(character);
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        Serial2.write(buffer, size);
        Serial.write(buffer, size);
        return size;
    }
};

static SerialLogger Logger;

static void boolUpdate() {
    SPIFFS.remove("/cProg.txt");
    File file = SPIFFS.open("/cProg.txt", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to update CProg launch state");
        return;
    }
    file.println(boolRes);
    file.close();
}

static void boolTru() {
    File file = SPIFFS.open("/cProg.txt", FILE_READ);
    boolRes = "";
    while (file && file.available()) boolRes += (char)file.read();
    if (file) file.close();
}

static int kernelFlashVideoLastPercent = -1;

static void kernelFlashVideoProgress(int percent, const char *label) {
    percent = constrain(percent, 0, 100);
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

static void handleKernelFlash() {
    File kernelFile = SD.open("/System/kernel.bin");
    if (!kernelFile) {
        Serial.println("Kernel file not found");
        return;
    }
    size_t kernelSize = kernelFile.size();
    kernelFlashVideoStart(kernelSize, "Flashing kernel");
    if (!Update.begin(kernelSize)) {
        Serial.printf("Kernel update begin failed: %s\n", Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        kernelFile.close();
        return;
    }
    uint8_t buffer[2048];
    size_t writtenTotal = 0;
    while (kernelFile.available()) {
        int bytesRead = kernelFile.read(buffer, sizeof(buffer));
        int bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            Update.abort();
            kernelFile.close();
            kernelFlashVideoProgress(100, "Kernel flash failed");
            return;
        }
        writtenTotal += bytesWritten;
        kernelFlashVideoBytes(writtenTotal);
    }
    kernelFile.close();
    if (!Update.end(true)) {
        Serial.printf("Kernel update failed: %s\n", Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        return;
    }
    kernelFlashVideoBytes(kernelSize);
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
}

static bool loadDocumentFile(File &file) {
    if (!file) return false;
    for (int i = 0; i < MAX_LINES; ++i) textBuffer[i] = "";
    documentLineCount = 1;
    String line = "";
    while (file.available() && documentLineCount <= MAX_LINES) {
        char character = (char)file.read();
        if (character == '\r') continue;
        if (character == '\n') {
            textBuffer[documentLineCount - 1] = line;
            line = "";
            if (documentLineCount < MAX_LINES) ++documentLineCount;
        } else if ((int)line.length() < MAX_LINE_CHARS) {
            line += character;
        }
    }
    if (line.length() || documentLineCount == 1)
        textBuffer[documentLineCount - 1] = line;
    while (documentLineCount > 1 &&
           textBuffer[documentLineCount - 1].length() == 0)
        --documentLineCount;
    file.close();
    return true;
}

static void loadDocumentBeforeVideo(bool sdReady) {
    bool loaded = false;
    if (sdReady) {
        File file = SD.open("/UserData/CProg/programmableBuffer.txt", FILE_READ);
        loaded = loadDocumentFile(file);
    }
    if (!loaded) {
        File file = SPIFFS.open("/cprog_buffer.txt", FILE_READ);
        loaded = loadDocumentFile(file);
    }
    if (!loaded) {
        textBuffer[0] = "// Citadela program";
        documentLineCount = 1;
    }
    bufferStart = 0;
    bufferEnd = 0;
    cursorColumn = textBuffer[0].length();
    editorDirty = false;
}

static bool writeDocumentToSpiffs() {
    SPIFFS.remove("/cprog_buffer.txt");
    File file = SPIFFS.open("/cprog_buffer.txt", FILE_WRITE);
    if (!file) return false;
    for (int i = 0; i < documentLineCount; ++i) {
        file.print(textBuffer[i]);
        if (i + 1 < documentLineCount) file.print('\n');
    }
    file.close();
    return true;
}

static bool writeDocumentToSd() {
    if (!SD.begin(5)) return false;
    SD.mkdir("/UserData");
    SD.mkdir("/UserData/CProg");
    SD.remove("/UserData/CProg/programmableBuffer.txt");
    File file = SD.open("/UserData/CProg/programmableBuffer.txt", FILE_WRITE);
    if (!file) {
        SD.end();
        return false;
    }
    for (int i = 0; i < documentLineCount; ++i) {
        file.print(textBuffer[i]);
        if (i + 1 < documentLineCount) file.print('\n');
    }
    file.close();
    SD.end();
    return true;
}

static void ensureCursorVisible() {
    cursorColumn = constrain(cursorColumn, 0,
                             (int)textBuffer[bufferEnd].length());
    if (bufferEnd < bufferStart) bufferStart = bufferEnd;
    if (bufferEnd >= bufferStart + VISIBLE_LINES)
        bufferStart = bufferEnd - VISIBLE_LINES + 1;
    bufferStart = constrain(bufferStart, 0,
                            max(0, documentLineCount - 1));
}

static void markEdited() {
    editorDirty = true;
    editorStatus = "UNSAVED CHANGES";
    ensureCursorVisible();
}

static void insertEditorText(const String &text) {
    for (unsigned int i = 0; i < text.length(); ++i) {
        if ((int)textBuffer[bufferEnd].length() >= MAX_LINE_CHARS) {
            if (documentLineCount >= MAX_LINES) break;
            String remainder = textBuffer[bufferEnd].substring(cursorColumn);
            textBuffer[bufferEnd].remove(cursorColumn);
            for (int line = documentLineCount; line > bufferEnd + 1; --line)
                textBuffer[line] = textBuffer[line - 1];
            ++documentLineCount;
            ++bufferEnd;
            textBuffer[bufferEnd] = remainder;
            cursorColumn = 0;
        }
        String updated = textBuffer[bufferEnd].substring(0, cursorColumn);
        updated += text[i];
        updated += textBuffer[bufferEnd].substring(cursorColumn);
        textBuffer[bufferEnd] = updated;
        ++cursorColumn;
    }
    markEdited();
    redrawScreen();
}

static void editorNewLine() {
    if (documentLineCount >= MAX_LINES) return;
    String remainder = textBuffer[bufferEnd].substring(cursorColumn);
    textBuffer[bufferEnd].remove(cursorColumn);
    for (int line = documentLineCount; line > bufferEnd + 1; --line)
        textBuffer[line] = textBuffer[line - 1];
    ++documentLineCount;
    ++bufferEnd;
    textBuffer[bufferEnd] = remainder;
    cursorColumn = 0;
    markEdited();
    redrawScreen();
}

static void editorBackspace() {
    bool changed = false;
    if (cursorColumn > 0) {
        textBuffer[bufferEnd].remove(cursorColumn - 1, 1);
        --cursorColumn;
        changed = true;
    } else if (bufferEnd > 0) {
        int previousLength = textBuffer[bufferEnd - 1].length();
        if (previousLength + (int)textBuffer[bufferEnd].length()
            <= MAX_LINE_CHARS) {
            textBuffer[bufferEnd - 1] += textBuffer[bufferEnd];
            for (int line = bufferEnd; line < documentLineCount - 1; ++line)
                textBuffer[line] = textBuffer[line + 1];
            textBuffer[documentLineCount - 1] = "";
            --documentLineCount;
            --bufferEnd;
            cursorColumn = previousLength;
            changed = true;
        }
    }
    if (changed) markEdited();
    redrawScreen();
}

static void editorDelete() {
    bool changed = false;
    String &line = textBuffer[bufferEnd];
    if (cursorColumn < (int)line.length()) {
        line.remove(cursorColumn, 1);
        changed = true;
    } else if (bufferEnd + 1 < documentLineCount &&
               (int)(line.length() + textBuffer[bufferEnd + 1].length())
               <= MAX_LINE_CHARS) {
        line += textBuffer[bufferEnd + 1];
        for (int i = bufferEnd + 1; i < documentLineCount - 1; ++i)
            textBuffer[i] = textBuffer[i + 1];
        textBuffer[documentLineCount - 1] = "";
        --documentLineCount;
        changed = true;
    }
    if (changed) markEdited();
    redrawScreen();
}

static void drawToolButton(int index, int x, const char *label) {
    bool focused = toolbarFocused && selectedTool == index;
    uint32_t fill = focused ? videodisplay.RGB(35, 99, 93)
                            : videodisplay.RGB(25, 42, 49);
    uint32_t edge = focused ? videodisplay.RGB(255, 255, 255)
                            : videodisplay.RGB(62, 92, 97);
    videodisplay.fillRect(x, 23, 58, 18, fill);
    videodisplay.rect(x, 23, 58, 18, edge);
    videodisplay.setTextColor(videodisplay.RGB(238, 246, 245), fill);
    videodisplay.setCursor(x + (58 - strlen(label) * 6) / 2, 28);
    videodisplay.print(label);
}

static void drawOptionsPanel() {
    uint32_t panel = videodisplay.RGB(18, 34, 40);
    uint32_t edge = videodisplay.RGB(66, 215, 190);
    uint32_t white = videodisplay.RGB(242, 248, 247);
    uint32_t muted = videodisplay.RGB(147, 176, 181);
    uint32_t toggle = videodisplay.RGB(31, 84, 78);
    videodisplay.fillRect(72, 49, 232, 96, panel);
    videodisplay.rect(72, 49, 232, 96, edge);
    videodisplay.setTextColor(white, panel);
    videodisplay.setCursor(84, 58);
    videodisplay.print("EDITOR OPTIONS");
    videodisplay.setTextColor(muted, panel);
    videodisplay.setCursor(84, 77);
    videodisplay.print("CAPS LOCK");
    videodisplay.setCursor(84, 101);
    videodisplay.print("TAB WIDTH");
    videodisplay.fillRect(211, 72, 72, 18, toggle);
    videodisplay.rect(211, 72, 72, 18, edge);
    videodisplay.setTextColor(white, toggle);
    videodisplay.setCursor(232, 77);
    videodisplay.print(capsLock ? "ON" : "OFF");
    videodisplay.fillRect(211, 96, 72, 18, toggle);
    videodisplay.rect(211, 96, 72, 18, edge);
    videodisplay.setCursor(244, 101);
    videodisplay.print(tabWidth);
    videodisplay.setTextColor(muted, panel);
    videodisplay.setCursor(84, 126);
    videodisplay.print("UART2 115200  RX21 TX22");
}

static void drawFlashScreen() {
    uint32_t background = videodisplay.RGB(7, 12, 17);
    uint32_t header = videodisplay.RGB(15, 27, 34);
    uint32_t panel = videodisplay.RGB(17, 34, 40);
    uint32_t accent = videodisplay.RGB(66, 215, 190);
    uint32_t white = videodisplay.RGB(242, 248, 247);
    uint32_t muted = videodisplay.RGB(145, 175, 181);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 21, header);
    videodisplay.fillRect(0, 20, SCREEN_WIDTH, 1, accent);
    videodisplay.setTextColor(white, header);
    videodisplay.setCursor(8, 7);
    videodisplay.print("CPROG / SERIAL TRANSMIT");
    videodisplay.fillRect(14, 35, 300, 14, panel);
    videodisplay.rect(12, 33, 304, 18, videodisplay.RGB(61, 87, 93));
    videodisplay.fillRect(14, 35, 300 * transmitProgress / 100, 14, accent);
    videodisplay.setTextColor(white, background);
    videodisplay.setCursor(329, 39);
    videodisplay.print(transmitProgress);
    videodisplay.print('%');
    videodisplay.setCursor(14, 61);
    videodisplay.print("OUTPUT");
    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(82, 61);
    videodisplay.print("UART2 / 115200 BAUD");
    videodisplay.fillRect(14, 78, 348, 60, panel);
    videodisplay.rect(14, 78, 348, 60, videodisplay.RGB(61, 87, 93));
    const char *pins[] = {"VCC", "GND", "RX 21", "TX 22", "EN", "BOOT 0"};
    const uint32_t pinColors[] = {
        videodisplay.RGB(235, 70, 77), videodisplay.RGB(120, 132, 136),
        videodisplay.RGB(70, 143, 235), videodisplay.RGB(63, 207, 126),
        videodisplay.RGB(244, 200, 70), videodisplay.RGB(184, 120, 220)
    };
    for (int i = 0; i < 6; ++i) {
        int x = 24 + (i % 3) * 112;
        int y = 89 + (i / 3) * 27;
        videodisplay.fillRect(x, y, 5, 14, pinColors[i]);
        videodisplay.setTextColor(white, panel);
        videodisplay.setCursor(x + 10, y + 3);
        videodisplay.print(pins[i]);
    }
    uint32_t back = videodisplay.RGB(41, 48, 57);
    uint32_t transmit = videodisplay.RGB(30, 112, 96);
    videodisplay.fillRect(12, 156, 78, 24, back);
    videodisplay.rect(12, 156, 78, 24, videodisplay.RGB(90, 105, 111));
    videodisplay.setTextColor(white, back);
    videodisplay.setCursor(37, 164);
    videodisplay.print("BACK");
    videodisplay.fillRect(236, 156, 128, 24, transmit);
    videodisplay.rect(236, 156, 128, 24, accent);
    videodisplay.setTextColor(white, transmit);
    videodisplay.setCursor(274, 164);
    videodisplay.print("TRANSMIT");
    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(104, 164);
    String flashStatus = editorStatus.substring(0, 20);
    videodisplay.print(flashStatus.c_str());
}

static void redrawScreen() {
    AppCursorDrawGuard cursorGuard;
    if (flashing) {
        drawFlashScreen();
        videodisplay.show();
        return;
    }
    uint32_t background = videodisplay.RGB(7, 12, 17);
    uint32_t header = videodisplay.RGB(14, 25, 31);
    uint32_t toolbar = videodisplay.RGB(12, 22, 28);
    uint32_t editor = videodisplay.RGB(10, 17, 22);
    uint32_t gutter = videodisplay.RGB(16, 29, 35);
    uint32_t active = videodisplay.RGB(21, 45, 48);
    uint32_t accent = videodisplay.RGB(66, 215, 190);
    uint32_t white = videodisplay.RGB(239, 246, 245);
    uint32_t muted = videodisplay.RGB(125, 153, 159);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 20, header);
    videodisplay.fillRect(0, 19, SCREEN_WIDTH, 1, accent);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, header);
    videodisplay.setCursor(8, 6);
    videodisplay.print("CPROG");
    videodisplay.setTextColor(muted, header);
    videodisplay.setCursor(50, 6);
    videodisplay.print("PROGRAM BUFFER");
    videodisplay.setCursor(286, 6);
    videodisplay.print(editorDirty ? "MODIFIED" : "SAVED");
    videodisplay.fillRect(0, 20, SCREEN_WIDTH, 23, toolbar);
    drawToolButton(0, 8, "SAVE");
    drawToolButton(1, 72, "FLASH");
    drawToolButton(2, 136, "OPTIONS");

    videodisplay.fillRect(EDITOR_X, EDITOR_Y, EDITOR_W, 126, editor);
    videodisplay.fillRect(EDITOR_X, EDITOR_Y, 34, 126, gutter);
    for (int visible = 0; visible < VISIBLE_LINES; ++visible) {
        int lineIndex = bufferStart + visible;
        int y = EDITOR_Y + 1 + visible * LINE_HEIGHT;
        if (lineIndex >= documentLineCount) continue;
        bool current = lineIndex == bufferEnd;
        uint32_t lineBackground = current ? active : editor;
        if (current)
            videodisplay.fillRect(38, y - 1, 334, LINE_HEIGHT, active);
        videodisplay.setTextColor(current ? accent : muted, gutter);
        videodisplay.setCursor(8, y);
        if (lineIndex < 9) videodisplay.print('0');
        videodisplay.print(lineIndex + 1);
        videodisplay.setTextColor(white, lineBackground);
        videodisplay.setCursor(43, y);
        videodisplay.print(textBuffer[lineIndex].c_str());
        if (current && !optionsOpen) {
            int cursorX = min(369, 43 + cursorColumn * 6);
            videodisplay.fillRect(cursorX, y, 1, 8, accent);
        }
    }
    videodisplay.fillRect(0, 174, SCREEN_WIDTH, 18, header);
    videodisplay.fillRect(0, 174, SCREEN_WIDTH, 1, accent);
    videodisplay.setTextColor(muted, header);
    videodisplay.setCursor(8, 180);
    String visibleStatus = editorStatus.substring(0, 42);
    videodisplay.print(visibleStatus.c_str());
    videodisplay.setCursor(286, 180);
    videodisplay.print("LN ");
    videodisplay.print(bufferEnd + 1);
    videodisplay.print(" COL ");
    videodisplay.print(cursorColumn + 1);
    if (optionsOpen) drawOptionsPanel();
    videodisplay.show();
}

static void save() {
    bool localSaved = writeDocumentToSpiffs();
    bool sdSaved = writeDocumentToSd();
    editorDirty = !(localSaved || sdSaved);
    if (sdSaved) editorStatus = "SAVED TO SD";
    else if (localSaved) editorStatus = "SAVED LOCALLY - SD UNAVAILABLE";
    else editorStatus = "SAVE FAILED";
    Serial.println(editorStatus);
    redrawScreen();
}

static void flash() {
    toolbarFocused = false;
    optionsOpen = false;
    flashing = true;
    transmitProgress = 0;
    editorStatus = "READY TO TRANSMIT";
    redrawScreen();
}

static void opt() {
    toolbarFocused = false;
    optionsOpen = true;
    redrawScreen();
}

static void updateTransmitProgress(int percent) {
    transmitProgress = constrain(percent, 0, 100);
    uint32_t track = videodisplay.RGB(17, 34, 40);
    uint32_t accent = videodisplay.RGB(66, 215, 190);
    uint32_t background = videodisplay.RGB(7, 12, 17);
    videodisplay.fillRect(14, 35, 300, 14, track);
    videodisplay.fillRect(14, 35, 300 * transmitProgress / 100, 14, accent);
    videodisplay.fillRect(324, 34, 48, 16, background);
    videodisplay.setTextColor(videodisplay.RGB(242, 248, 247), background);
    videodisplay.setCursor(329, 39);
    videodisplay.print(transmitProgress);
    videodisplay.print('%');
    videodisplay.show();
}

static void spitFirmware() {
    size_t totalBytes = 0;
    for (int i = 0; i < documentLineCount; ++i)
        totalBytes += textBuffer[i].length() +
                      (i + 1 < documentLineCount ? 1 : 0);
    if (totalBytes == 0) {
        editorStatus = "BUFFER IS EMPTY";
        redrawScreen();
        return;
    }
    editorStatus = "TRANSMITTING";
    redrawScreen();
    size_t sent = 0;
    for (int i = 0; i < documentLineCount; ++i) {
        Logger.print(textBuffer[i]);
        sent += textBuffer[i].length();
        if (i + 1 < documentLineCount) {
            Logger.write('\n');
            ++sent;
        }
        updateTransmitProgress((int)(sent * 100 / totalBytes));
        delay(2);
    }
    Serial2.flush();
    editorStatus = "TRANSMISSION COMPLETE";
    tone(SPEAKER_PIN, 920, 90);
    redrawScreen();
}

static bool handleCProgMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);

    if (!flashing && !optionsOpen) {
        if (mouse.wheel > 0 && bufferStart > 0) {
            --bufferStart;
            redrawScreen();
        } else if (mouse.wheel < 0 &&
                   bufferStart + VISIBLE_LINES < documentLineCount) {
            ++bufferStart;
            redrawScreen();
        }
    }

    if (mouse.leftPressed) {
        if (flashing) {
            if (appCursorX >= 236 && appCursorX < 365 &&
                appCursorY >= 156 && appCursorY < 181) {
                spitFirmware();
            } else if (appCursorX >= 12 && appCursorX < 91 &&
                       appCursorY >= 156 && appCursorY < 181) {
                flashing = false;
                editorStatus = editorDirty ? "UNSAVED CHANGES" : "READY";
                redrawScreen();
            }
        } else if (optionsOpen) {
            if (appCursorX >= 211 && appCursorX < 284 &&
                appCursorY >= 72 && appCursorY < 91) {
                capsLock = !capsLock;
                redrawScreen();
            } else if (appCursorX >= 211 && appCursorX < 284 &&
                       appCursorY >= 96 && appCursorY < 115) {
                tabWidth = tabWidth == 2 ? 4 : 2;
                redrawScreen();
            } else if (!(appCursorX >= 72 && appCursorX < 305 &&
                         appCursorY >= 49 && appCursorY < 146)) {
                optionsOpen = false;
                redrawScreen();
            }
        } else if (appCursorY >= 23 && appCursorY <= 42) {
            if (appCursorX >= 8 && appCursorX < 67) {
                selectedTool = 0;
                save();
            } else if (appCursorX >= 72 && appCursorX < 131) {
                selectedTool = 1;
                flash();
            } else if (appCursorX >= 136 && appCursorX < 195) {
                selectedTool = 2;
                opt();
            }
        } else if (appCursorY >= EDITOR_Y && appCursorY < 171) {
            int visibleLine = (appCursorY - EDITOR_Y) / LINE_HEIGHT;
            int targetLine = bufferStart + visibleLine;
            if (targetLine < documentLineCount) {
                bufferEnd = targetLine;
                cursorColumn = constrain((appCursorX - 43 + 3) / 6, 0,
                                         (int)textBuffer[bufferEnd].length());
                redrawScreen();
            }
        }
    }
    appCursorCaptureAndDraw();
    return true;
}

static String translatedEditorInput(String command) {
    if (command == "10") return "0";
    if (command.length() == 1) {
        if (capsLock && command[0] >= 'a' && command[0] <= 'z')
            command.toUpperCase();
        return command;
    }
    if (command == "Space") return " ";
    if (command == "Tab") {
        String spaces = "";
        for (int i = 0; i < tabWidth; ++i) spaces += ' ';
        return spaces;
    }
    const char *names[] = {
        "LeftShift + 1", "LeftShift + 2", "LeftShift + 3",
        "LeftShift + 4", "LeftShift + 5", "LeftShift + 6",
        "LeftShift + 7", "LeftShift + 8", "LeftShift + 9",
        "LeftShift + 10", "LeftShift + =", "LeftShift + -",
        "LeftShift + ,", "LeftShift + '", "LeftShift + {",
        "LeftShift + }", "LeftShift + ~"
    };
    const char *values[] = {
        "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "+",
        "_", ":", "<", "[", "]", "?"
    };
    for (int i = 0; i < 17; ++i)
        if (command == names[i]) return values[i];
    return "";
}

static void handleCProgCommand(String command) {
    command.trim();
    if (command.length() == 0 || command == "rlsd") return;
    if (handleCProgMouse(command)) return;

    if (command == "STATUS") {
        Serial.printf("CPROG line=%d column=%d lines=%d dirty=%d flashing=%d status=%s\n",
                      bufferEnd + 1, cursorColumn + 1, documentLineCount,
                      editorDirty, flashing, editorStatus.c_str());
        return;
    }

    if (command == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting CProg");
        return;
    }
    if (command == "Escape") {
        if (optionsOpen) {
            optionsOpen = false;
            redrawScreen();
        } else if (flashing) {
            flashing = false;
            editorStatus = editorDirty ? "UNSAVED CHANGES" : "READY";
            redrawScreen();
        } else if (toolbarFocused) {
            toolbarFocused = false;
            redrawScreen();
        } else {
            boolRes = "trueKernel";
            boolUpdate();
            restartWithFallbackVideo("Returning home");
        }
        return;
    }
    if (flashing) {
        if (command == "Enter") spitFirmware();
        return;
    }
    if (optionsOpen) {
        if (command == "CapsLock" || command == "Enter")
            capsLock = !capsLock;
        else if (command == "LeftArrow" || command == "RightArrow")
            tabWidth = tabWidth == 2 ? 4 : 2;
        redrawScreen();
        return;
    }
    if (command == "LeftCtrl +") {
        toolbarFocused = !toolbarFocused;
        redrawScreen();
        return;
    }
    if (command == "LeftCtrl + s" || command == "LeftCtrl + S" ||
        command == "Ctrl+S") {
        save();
        return;
    }
    if (toolbarFocused) {
        if (command == "LeftArrow")
            selectedTool = max(0, selectedTool - 1);
        else if (command == "RightArrow")
            selectedTool = min(2, selectedTool + 1);
        else if (command == "Enter") {
            if (selectedTool == 0) save();
            else if (selectedTool == 1) flash();
            else opt();
            return;
        }
        redrawScreen();
        return;
    }

    if (command == "LeftArrow") {
        if (cursorColumn > 0) {
            --cursorColumn;
        } else if (bufferEnd > 0) {
            --bufferEnd;
            cursorColumn = textBuffer[bufferEnd].length();
        }
    } else if (command == "RightArrow") {
        if (cursorColumn < (int)textBuffer[bufferEnd].length()) {
            ++cursorColumn;
        } else if (bufferEnd + 1 < documentLineCount) {
            ++bufferEnd;
            cursorColumn = 0;
        }
    } else if (command == "UpArrow") {
        if (bufferEnd > 0) --bufferEnd;
        cursorColumn = min(cursorColumn,
                           (int)textBuffer[bufferEnd].length());
    } else if (command == "DownArrow") {
        if (bufferEnd + 1 < documentLineCount) ++bufferEnd;
        cursorColumn = min(cursorColumn,
                           (int)textBuffer[bufferEnd].length());
    } else if (command == "Home") {
        cursorColumn = 0;
    } else if (command == "End") {
        cursorColumn = textBuffer[bufferEnd].length();
    } else if (command == "Backspace") {
        editorBackspace();
        return;
    } else if (command == "Delete") {
        editorDelete();
        return;
    } else if (command == "Enter") {
        editorNewLine();
        return;
    } else if (command == "CapsLock") {
        capsLock = !capsLock;
        editorStatus = capsLock ? "CAPS LOCK ON" : "CAPS LOCK OFF";
    } else {
        String text = translatedEditorInput(command);
        if (text.length()) {
            insertEditorText(text);
            return;
        }
    }
    ensureCursorVisible();
    redrawScreen();
}

static void pollCProgPort(HardwareSerial &port, String &buffer) {
    while (port.available()) {
        char character = (char)port.read();
        if (character == '\r') continue;
        if (character == '\n') {
            handleCProgCommand(buffer);
            buffer = "";
        } else if (buffer.length() < 96) {
            buffer += character;
        } else {
            buffer = "";
        }
    }
}

void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial2.begin(115200, SERIAL_8N1, 21, 22);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
    Serial1.println("res");
    if (!SPIFFS.begin(true)) return;

    SPI.begin(18, 19, 23, 5);
    bool sdReady = SD.begin(5);
    Serial.println(sdReady ? "SD ready" : "SD unavailable");
    boolTru();
    boolRes.trim();
    if (boolRes.length() == 0) {
        boolRes = "false";
        boolUpdate();
    }
    if (boolRes == "trueKernel") {
        tone(SPEAKER_PIN, 0);
        boolRes = "false";
        boolUpdate();
        if (!sdReady) SD.begin(5);
        handleKernelFlash();
        return;
    }

    loadDocumentBeforeVideo(sdReady);
    if (sdReady) SD.end();
    bool videoReady =
        videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
    Serial.printf("CProg video init: %s\n",
                  videoReady ? "ready" : "failed");
    if (!videoReady) restartWithFallbackVideo("CProg video failed");
    releaseSerialControllerVideo();
    tone(SPEAKER_PIN, 0);
    videodisplay.setFont(Font6x8);
    redrawScreen();
}

void loop() {
    static String controllerBuffer = "";
    static String usbBuffer = "";
    pollCProgPort(Serial1, controllerBuffer);
    pollCProgPort(Serial, usbBuffer);
    delay(2);
}
