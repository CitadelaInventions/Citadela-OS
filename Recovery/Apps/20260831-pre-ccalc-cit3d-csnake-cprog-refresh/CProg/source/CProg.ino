#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <esp_ota_ops.h>
#include "esp_partition.h"
#include "FS.h"
#include "SPIFFS.h"

#define MAX_LINES 100
#define VISIBLE_LINES 15
#define SPEAKER_PIN 12


int bufferStart = 0;
int bufferEnd = 0;
int cursorPoint = 10;
int heightPoint = 40;
int lettersPerRow = 0;
int line = 1;
int selected = 1;
int prevSel = 1;

bool CapsLock = false;
bool flashing = false;
bool menu = false;
bool menu1 = false;

String reverie = "";
String keyboardInput = "";
String boolRes = "";
String textBuffer[MAX_LINES];

CompositeColorDAC videodisplay;

static inline uint32_t grayColor(uint8_t value) {
  return videodisplay.RGB(value, value, value);
}

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

static inline AppCursorRawPixel appCursorGetRawPixel(int x, int y);
static inline void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
static bool appMouseRead(String input, AppMouseReport &report);

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

SerialLogger Logger;

void writeToSD(String expression) {
  File file = SD.open("/UserData/CProg/programmableBuffer.txt", FILE_WRITE);
  file.print(expression);
  file.close();
}

void boolUpdate() {
    File file = SPIFFS.open("/cProg.txt", FILE_WRITE);
    if (file) {
        file.seek(0);
        file.print("");
        file.println(boolRes);
        file.close();
        Serial.printf("Bool updated to %s\n", boolRes);
    } else {
        Serial.println("Failed to update bool!");
    }
}

void boolTru() {
    File file = SPIFFS.open("/cProg.txt", FILE_READ);
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
}
void save(){
  Serial.println("Saving");
}
void flash(){
  flashing = true;
  menu = false;
  Serial.println("Flash");
  videodisplay.clear();
  videodisplay.rect(0,13,255+130,170,grayColor(255));
  videodisplay.rect(0,13,255+130,10,grayColor(255));
  videodisplay.setCursor(255+30,14);
  videodisplay.println("Citadela Flasher");
  videodisplay.setCursor(30, 14);
  videodisplay.println("%");
  videodisplay.setCursor(3, 14);
  videodisplay.println("0");
  videodisplay.rect(40,14,225,8,grayColor(255));
  videodisplay.setFont(Font8x8);
  videodisplay.setCursor(3, 30);
  videodisplay.println("Steps to flash your device:");
  videodisplay.setFont(Font6x8);
  videodisplay.println("1) Ensure your code is functioning properly.");
  videodisplay.println("2) Follow wire instructions strictly!");
  videodisplay.println("3) Do not disconnect your device during flashing process.");
  videodisplay.println("4) Flashee must receive stable power during code upload.");
  videodisplay.rect(3,80,255+124,80,grayColor(255));
  videodisplay.setCursor(5, 81);
  videodisplay.println("Visual Wire Instructions:");

  //Wire Organisation
  videodisplay.setCursor(120,104);
  videodisplay.println("RED -> VCC");
  videodisplay.println("BLACK -> GND");
  videodisplay.println("BLUE -> RX");
  videodisplay.println("GREEN -> TX");
  videodisplay.println("YELLOW -> EN");
  videodisplay.println("BLACK2 -> GPIO 0 / BOOT");
  
  //Organisation
  videodisplay.setCursor(255+72, 100);
  videodisplay.println("*");
  videodisplay.line(120,103,255+70, 103,grayColor(255));
  videodisplay.println("*");
  videodisplay.line(120,111,255+70, 111,grayColor(255));
  videodisplay.println("*");
  videodisplay.line(120,119,255+70, 119,grayColor(255));
  videodisplay.println("*");
  videodisplay.line(120,127,255+70, 127,grayColor(255));
  videodisplay.println("*");
  videodisplay.line(120,135,255+70, 135,grayColor(255));
  videodisplay.println("*");
  //Extra Dec
  videodisplay.setCursor(255+113, 100);
  videodisplay.println("*");
  videodisplay.println("*");
  videodisplay.println("*");
  videodisplay.println("*");
  videodisplay.println("*");
  videodisplay.println("*");
  videodisplay.line(120,143,255+70, 143,grayColor(255));
  videodisplay.rect(255+70, 85, 50, 70,grayColor(255));
  videodisplay.fillRect(255+78, 120, 35, 25, grayColor(200));
  videodisplay.fillRect(255+78, 145, 35, 7, grayColor(230));
  videodisplay.fillRect(255+90, 86, 10, 6, grayColor(200));
  videodisplay.fillRect(255+80, 86, 5, 6, grayColor(200));
  videodisplay.fillRect(255+105, 86, 5, 6, grayColor(200));
  videodisplay.fillRect(255+80, 100, 10, 7, grayColor(255));
  videodisplay.fillRect(255+100, 100, 4, 2, grayColor(255));
  videodisplay.fillRect(255+100, 105, 4, 2, grayColor(255));

  videodisplay.setCursor(250, 163);
  videodisplay.setTextColor(0,grayColor(255));
  videodisplay.println(" Press Enter to Flash ");
  videodisplay.setTextColor(grayColor(255),0);
  videodisplay.println(" Press Esc to go back ");
}
void opt(){
  Serial.println("Options");
}
void setup() {
  pinMode(SPEAKER_PIN, 0);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(256000,SERIAL_8N1, 16, 17);
  Serial2.begin(115200,SERIAL_8N1, 21, 22);
  Serial1.println("res");
  if (!SPIFFS.begin(true)){
    return;
  }
  SPI.begin(18,19,23,5);
  if(!SD.begin(5)){
    Serial.println("SD Failed to init");
  } else {
    Serial.println("EUREKA");
  }
  boolTru();
  if (boolRes == ""){
      boolRes = "false";
      boolUpdate();
  }
  boolRes.trim();
  if (boolRes == "false"){
    videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
    releaseSerialControllerVideo();
    tone(SPEAKER_PIN, 0);
    videodisplay.setFont(Font6x8);
    visualPair();
  } else if (boolRes == "trueKernel"){
    tone(SPEAKER_PIN, 0);
    boolRes = "false";
    boolUpdate();
    handleKernelFlash();
  }
  
}

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

void handleKernelFlash() {
    File kernelFile1 = SD.open("/System/kernel.bin");
    if (!kernelFile1) {
        Serial.println("Kernel file not found!");
        return;
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
        return;
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
            return;
        }
    }

    kernelFile1.close();
    if (Update.end()) {
        Serial.println("Kernel written to flash successfully.");
        kernelFlashVideoBytes(kernelSize1);
        Serial1.println("VDSTOP");
        Serial1.flush();
        delay(25);
        ESP.restart();
    } else {
        Serial.printf("Update failed: %s\n", Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        Serial1.flush();
    }
}

void visualPair(){
  videodisplay.rect(0, 23, 255 + 140, 253, videodisplay.RGB(255,255,255));
  videodisplay.rect(0, 23, 15, 253, videodisplay.RGB(255,255,255));
  videodisplay.fillRect(15, 10, 35, 10, videodisplay.RGB(255,255,255));
  videodisplay.fillRect(55, 10, 35, 10, videodisplay.RGB(255,255,255));
  videodisplay.fillRect(95, 10, 35, 10, videodisplay.RGB(255,255,255));
  videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
  videodisplay.setCursor(17, 11);
  videodisplay.print("save");
  videodisplay.setCursor(57, 11);
  videodisplay.print("flash");
  videodisplay.setCursor(97, 11);
  videodisplay.print("opt");
  videodisplay.rect(0, 10, 255 + 140, 10, videodisplay.RGB(255,255,255));
  videodisplay.setCursor(255 + 42, 11);
  videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
  videodisplay.print("Citadela Notepad");
}

void visualKeyboard() {
  if (!flashing){
    if (keyboardInput == "\\b") {
        if (textBuffer[bufferEnd].length() > 0) {
            textBuffer[bufferEnd].remove(textBuffer[bufferEnd].length() - 1, 1);
            lettersPerRow--;
        } else if (bufferEnd != bufferStart) {
            bufferEnd = (bufferEnd - 1 + MAX_LINES) % MAX_LINES;
            textBuffer[bufferEnd].remove(textBuffer[bufferEnd].length() - 1, 1);
            lettersPerRow--;
        }
        videodisplay.fillRect(2, 24, 255, 510, 0);
        visualPair();
    } else if (keyboardInput == " ") {
        if (textBuffer[bufferEnd].length() < 80) {
            textBuffer[bufferEnd] += " ";
        }
    } else if (keyboardInput == "\n") {
        // Clear the cursor from the current (old) editing line without wiping the whole screen.
        int oldLine = bufferEnd;
        int oldVisibleIndex = (oldLine - bufferStart + MAX_LINES) % MAX_LINES;
        if (oldVisibleIndex < VISIBLE_LINES) {
            videodisplay.fillRect(20, 28 + oldVisibleIndex * 10, 255, 10, 0);
            videodisplay.setCursor(2, 28 + oldVisibleIndex * 10);
            videodisplay.setTextColor(videodisplay.RGB(255,255,255));
            videodisplay.print(oldLine);
            videodisplay.setCursor(20, 28 + oldVisibleIndex * 10);
            videodisplay.print(textBuffer[oldLine].c_str());
        }
        
        // Advance to new line
        bufferEnd = (bufferEnd + 1) % MAX_LINES;
        if (bufferEnd == bufferStart) {
            bufferStart = (bufferStart + 1) % MAX_LINES;
        }
        textBuffer[bufferEnd] = "";
        lettersPerRow = 0;
    } else if (textBuffer[bufferEnd].length() < 80) {
        textBuffer[bufferEnd] += keyboardInput;
    } else {
        bufferEnd = (bufferEnd + 1) % MAX_LINES;
        if (bufferEnd == bufferStart) {
            bufferStart = (bufferStart + 1) % MAX_LINES;
        }
        textBuffer[bufferEnd] = keyboardInput;
    }

    lettersPerRow++;
    if (lettersPerRow >= 54) {
        bufferEnd = (bufferEnd + 1) % MAX_LINES;
        if (bufferEnd == bufferStart) bufferStart = (bufferStart + 1) % MAX_LINES;
        lettersPerRow = 0;
    }
    if ((bufferEnd - bufferStart + MAX_LINES) % MAX_LINES >= VISIBLE_LINES) {
        bufferStart = (bufferStart + 1) % MAX_LINES;
    }

    redrawScreen();
    keyboardInput = "";
  } else {return;}
}

void spitFirmware(){
  Serial.println("Spit Firmware DISKBARN CLONE FIRMWARE");
  File file = SD.open("/UserData/CProg/programmableBuffer.txt", FILE_READ);
  if (!file) {
    Serial.println("Error opening file for serial output.");
    return;
  }
  Serial.println("Sending file in 512 byte chunks:");
  videodisplay.setCursor(3, 14);
  uint8_t buffer[512];
  long fileSize = file.size();
  long totalRead = 0;
  while (file.available()) {
    int bytesRead = file.read(buffer, sizeof(buffer));
    totalRead += bytesRead;
    int progressWidth = (totalRead * 225) / fileSize;
    int bytesRec = (totalRead * 100) / fileSize;
    videodisplay.fillRect(40, 14, progressWidth, 8, grayColor(255));
    videodisplay.println(bytesRec);
    Logger.write(buffer, bytesRead);
    delay(10);
  }

  file.close();
  Serial.println("\n--- File Transmission Complete ---");
}
void redrawScreen() {
    static int lastBufferStart = -1;
    if (bufferStart != lastBufferStart) {
        videodisplay.fillRect(2, 24, 255+255, 255+255, 0);
        visualPair(); 
        lastBufferStart = bufferStart;
    }

    int drawLine = bufferStart;
    String newContent = "";
    for (int i = 0; i < VISIBLE_LINES; i++) {
        // Always draw the line if it is the current editing line, even if it's empty.
        bool isCurrentEditingLine = (drawLine == bufferEnd);
        if (textBuffer[drawLine] != "" || isCurrentEditingLine) {
            // Draw line number and text
            videodisplay.setCursor(2, 28 + (i * 10));
            videodisplay.setTextColor(videodisplay.RGB(255,255,255));
            videodisplay.print(drawLine);
            videodisplay.setCursor(20, 28 + (i * 10));
            videodisplay.print(textBuffer[drawLine].c_str());
            
            // Draw the cursor if this is the editing line.
            if (isCurrentEditingLine) {
                int cursorPos = textBuffer[drawLine].length();  // number of characters
                int pointerX = 20 + cursorPos * 6;  // estimate: 6 pixels per character (adjust if needed)
                videodisplay.setCursor(pointerX, 28 + (i * 10));
                videodisplay.print("|");  // cursor indicator
            }
            
            newContent += textBuffer[drawLine] + "\n";
        }
        drawLine = (drawLine + 1) % MAX_LINES;
    }

    if (newContent.length() > 0) {
        writeToSD(newContent);
    }
}

static bool handleCProgMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);

    if (mouse.wheel > 0 && bufferStart > 0) {
        bufferStart = (bufferStart - 1 + MAX_LINES) % MAX_LINES;
        redrawScreen();
    } else if (mouse.wheel < 0 && (bufferEnd - bufferStart + MAX_LINES) % MAX_LINES > VISIBLE_LINES) {
        bufferStart = (bufferStart + 1) % MAX_LINES;
        redrawScreen();
    }

    if (mouse.leftPressed) {
        if (flashing) {
            if (appCursorX >= 250 && appCursorX < 385 && appCursorY >= 160 && appCursorY < 178) {
                spitFirmware();
            }
        } else if (appCursorY >= 10 && appCursorY <= 22) {
            if (appCursorX >= 15 && appCursorX < 50) {
                selected = 1;
                save();
            } else if (appCursorX >= 55 && appCursorX < 90) {
                selected = 2;
                flash();
            } else if (appCursorX >= 95 && appCursorX < 130) {
                selected = 3;
                opt();
            }
        } else if (appCursorY >= 28 && appCursorY < 28 + VISIBLE_LINES * 10) {
            int visibleLine = (appCursorY - 28) / 10;
            int targetLine = (bufferStart + visibleLine) % MAX_LINES;
            bufferEnd = targetLine;
            lettersPerRow = textBuffer[bufferEnd].length();
            redrawScreen();
        }
    }

    appCursorCaptureAndDraw();
    return true;
}


void keyboardDriver() {
    static String ctrlInput1 = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            ctrlInput1.trim();
            if (handleCProgMouse(ctrlInput1)) {
                ctrlInput1 = "";
                continue;
            }
            AppCursorDrawGuard cursorGuard;
            if(!menu){
                Serial.println(ctrlInput1);
                if (ctrlInput1 == "UpArrow") {
                    if (bufferStart > 0) {
                        bufferStart = (bufferStart - 1 + MAX_LINES) % MAX_LINES;
                    }
                    redrawScreen();
                } else if (ctrlInput1 == "DownArrow") {
                    if ((bufferEnd - bufferStart + MAX_LINES) % MAX_LINES > VISIBLE_LINES) {
                        bufferStart = (bufferStart + 1) % MAX_LINES;
                    }
                    redrawScreen();
                } else if (ctrlInput1.length() == 1 && 
                    ((ctrlInput1[0] >= 'A' && ctrlInput1[0] <= 'Z') || 
                    (ctrlInput1[0] >= 'a' && ctrlInput1[0] <= 'z') || 
                    (ctrlInput1[0] >= '0' && ctrlInput1[0] <= '9'))) {
                    if (CapsLock){
                      ctrlInput1.toUpperCase();
                    }
                    keyboardInput = ctrlInput1;
                    visualKeyboard();
                } else if (ctrlInput1 == "Space"){
                    keyboardInput = " ";
                    visualKeyboard();
                } else if (ctrlInput1 == "Backspace") {
                    keyboardInput = "\\b";
                    visualKeyboard();
                } else if (ctrlInput1 == "Enter") {
                  if (!flashing){
                    keyboardInput = "\n";
                    visualKeyboard();
                  } else {
                    spitFirmware();
                  }
                } else if (ctrlInput1 == "."|| ctrlInput1 == ","){
                    keyboardInput = ctrlInput1;
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 9"){
                    keyboardInput = "(";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 10"){
                    keyboardInput = ")";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 1"){
                    keyboardInput = "!";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 2"){
                    keyboardInput = "@";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 3"){
                    keyboardInput = "#";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 4"){
                    keyboardInput = "$";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 5"){
                    keyboardInput = "%";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 6"){
                    keyboardInput = "|";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 7"){
                    keyboardInput = "&";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + 8"){
                    keyboardInput = "*";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + ~"){
                    keyboardInput = "?";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + ,"){
                    keyboardInput = ":";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + '"){
                    keyboardInput = "<";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + ="){
                    keyboardInput = "+";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + -"){
                    keyboardInput = "_";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + {"){
                    keyboardInput = "[";
                    visualKeyboard();
                } else if(ctrlInput1 == "LeftShift + }"){
                    keyboardInput = "]";
                    visualKeyboard();
                } else if(ctrlInput1 == "}"){
                    keyboardInput = "}";
                    visualKeyboard();
                } else if(ctrlInput1 == "{"){
                    keyboardInput = "{";
                    visualKeyboard();
                } else if(ctrlInput1 == "Tab"){
                    keyboardInput = "  ";
                    visualKeyboard();
                } else if(ctrlInput1 == "="){
                    keyboardInput = "=";
                    visualKeyboard();
                } else if(ctrlInput1 == "-"){
                    keyboardInput = "-";
                    visualKeyboard();
                } else if(ctrlInput1 == "10"){
                    keyboardInput = "0";
                    visualKeyboard();
                } else if(ctrlInput1 == "CapsLock"){
                  if (CapsLock){
                    CapsLock = false;
                  } else {
                    CapsLock = true;
                  }
                } else if (ctrlInput1 == "Escape"){
                    if(flashing){
                      flashing = false;
                      videodisplay.clear();
                      visualPair();
                      redrawScreen();
                    } else {
                      boolRes = "trueKernel";
                      boolUpdate();
                      restartWithFallbackVideo("Returning home");
                    }
                } else if (ctrlInput1 == "RightGUI (Win) +"){
                  restartWithFallbackVideo("Restarting");
                }
            } else {
                if (ctrlInput1 == "LeftArrow") {
                    if (selected>1){selected--;}
                } else if (ctrlInput1 == "RightArrow") {
                    if (selected<3){selected++;}
                } else if (ctrlInput1 == "Enter") {
                    switch(selected){
                      case 1: save(); break;
                      case 2: flash(); break;
                      case 3: opt(); break;
                    }
                }
            }
            if (ctrlInput1 == "LeftCtrl +") {
                if (!menu){
                  prevSel = -1;
                  selected = 0;
                  if (selected<3){selected++;}
                  menu = true;
                } else {
                  videodisplay.rect(15, 21, 255, 1,0);
                  menu = false;
                }
            } 
            
          } else {
              ctrlInput1 += c;
          }
          }
      ctrlInput1 = "";    
}
void loop() {
  keyboardDriver();
  if (menu) {
    AppCursorDrawGuard cursorGuard;
    if (menu) {
      if (prevSel != selected) {
          for (int i = 0; i < 3; i++) {
              videodisplay.rect(15 + (i * 40), 21, 35, 1, (i == selected - 1) ? videodisplay.RGB(255,255,255) : 0);
          }
      }
    }
}
  prevSel = selected;
}
