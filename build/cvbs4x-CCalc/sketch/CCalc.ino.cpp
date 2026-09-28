#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
#include <Wire.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <Ressources/CodePage437_9x16.h>
#include <Update.h>
#include "esp_partition.h"
#include "SD.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "FS.h"

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 380;
static const int APP_CURSOR_SCREEN_H = 285;
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

#line 50 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorLoadConfigOnce();
#line 71 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 76 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 81 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 86 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 91 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 95 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 101 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorRestore();
#line 112 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 119 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorCaptureAndDraw();
#line 151 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void appCursorRefreshAfterRedraw();
#line 161 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 182 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void restartWithFallbackVideo(const char *label);
#line 199 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void releaseSerialControllerVideo();
#line 219 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void boolTru();
#line 235 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void boolUpdate();
#line 248 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void drawCalculatorUI();
#line 329 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void computeResult();
#line 343 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void processInput(char c);
#line 389 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static char calculatorMouseKeyAt(int x, int y);
#line 411 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static bool handleCalculatorMouse(String input);
#line 424 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void processSerialCommand(String command);
#line 447 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 458 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 463 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 472 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void handleKernelFlash();
#line 530 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void setup();
#line 573 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
void loop();
#line 50 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CCalc\\CCalc.ino"
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

String inputBuffer = "";
String expression = "";
String boolRes = "";
String appName = "";
String appString = "";

#define SPEAKER_PIN 12

float lastResult = 0;
char lastOperator = '\0';

void boolTru() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
    File app = SPIFFS.open("/remApp.txt", FILE_READ);
    String appContent = "";
    while (app.available()) {
        appContent += (char)app.read();
    }
    appString = appContent;
    app.close();
}
void boolUpdate() {
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
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

void drawCalculatorUI() {
    videodisplay.setCursor(255,255+10);
    videodisplay.println("Citadela Calculator");
    videodisplay.fillRect(95, 60, 200, 160, videodisplay.RGB(255,255,255));
    videodisplay.fillRect(0, 60, 80, 160, videodisplay.RGB(255,255,255));
    videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
    videodisplay.setCursor(25, 65);
    videodisplay.println("Table");

    videodisplay.setCursor(10, 80);
    videodisplay.println("+ add (CTRL");
    videodisplay.println("     /SHIFT");
    videodisplay.println("- subtract");
    videodisplay.println("x multiply");
    videodisplay.println("% divide");
    videodisplay.println("= solve");
    videodisplay.println("28 char");
    videodisplay.println("render lmt");
    videodisplay.println(" Hold SHIFT");
    videodisplay.println("+ Number to");
    videodisplay.println("Permutate P");
    videodisplay.println(" Hold (-)");
    videodisplay.println("+ Number to");
    videodisplay.println("Permutate N");
    videodisplay.println(" Hold (x)");
    videodisplay.println("+ Number to");
    videodisplay.println("make Power");

    videodisplay.rect(105, 110, 180, 100, 0);
    videodisplay.setFont(CodePage437_9x16);
    videodisplay.setCursor(115, 120);
    videodisplay.println("1");
    videodisplay.setCursor(145, 120);
    videodisplay.println("2");
    videodisplay.setCursor(175, 120);
    videodisplay.println("3");
    videodisplay.setCursor(115, 150);
    videodisplay.println("4");
    videodisplay.setCursor(145, 150);
    videodisplay.println("5");
    videodisplay.setCursor(175, 150);
    videodisplay.println("6");
    videodisplay.setCursor(115, 182);
    videodisplay.println("7");
    videodisplay.setCursor(145, 182);
    videodisplay.println("8");
    videodisplay.setCursor(175, 182);
    videodisplay.println("9");

    videodisplay.setCursor(200, 110);
    videodisplay.println("+ ->(add)");
    videodisplay.setCursor(200, 128);
    videodisplay.println("- ->(sub)");
    videodisplay.setCursor(200, 145);
    videodisplay.println("x ->(mul)");
    videodisplay.setCursor(200, 160);
    videodisplay.println("% ->(div)");
    videodisplay.setCursor(200, 175);
    videodisplay.println("    0");
    videodisplay.setCursor(200, 193);
    videodisplay.println("28RenderL");

    videodisplay.rect(105, 110, 30, 100, 0);
    videodisplay.rect(105, 110, 60, 100, 0);
    videodisplay.rect(105, 110, 90, 100, 0);
    videodisplay.rect(105, 110, 90, 65, 0);
    videodisplay.rect(105, 110, 90, 35, 0);

    videodisplay.rect(194, 110, 91, 17.5, 0);
    videodisplay.rect(194, 128, 91, 17.5, 0); //127.5
    videodisplay.rect(194, 145, 91, 15, 0);
    videodisplay.rect(194, 160, 91, 15, 0);
    videodisplay.rect(194, 175, 91, 17.5, 0);
    videodisplay.rect(194, 193, 91, 17.5, 0);

    videodisplay.fillRect(105, 70, 180, 30, 0);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
    Serial.println(expression);  // Debugging output
}

void computeResult() {
    if (inputBuffer.length() == 0) return;  // Avoid computation if no input
    float currentNum = inputBuffer.toFloat();
    
    if (lastOperator == '+') lastResult += currentNum;
    else if (lastOperator == '-') lastResult -= currentNum;
    else if (lastOperator == 'x') lastResult *= currentNum;
    else if (lastOperator == '%' && currentNum != 0) lastResult /= currentNum;
    else lastResult = currentNum;  // First number input

    expression = String(lastResult);  // Update expression to result
    inputBuffer = "";  // Clear input buffer after calculation
}

void processInput(char c) {
    if (c == '\n' || c == '\r') return;  // Ignore carriage returns & line feeds
    Serial.println(c);
    if (isdigit(c)) {  
        inputBuffer += c;    // Append number to buffer
        expression += c;      // Show number on display
    } 
    else if (c == '+' || c == '-' || c == 'x' || c == '%') {  
        if (expression.length() > 0 && (expression[expression.length() - 1] == '+' || 
            expression[expression.length() - 1] == '-' || expression[expression.length() - 1] == 'x' || 
            expression[expression.length() - 1] == '%')) {
            return; // Prevents multiple consecutive operators
        }

        if (inputBuffer.length() > 0) {
            if (lastOperator != '\0') {
                computeResult();  // Compute last operation if an operator already exists
            } else {
                lastResult = inputBuffer.toFloat();  // Set first number
            }
            inputBuffer = "";  // Reset input for the next number
        }
        lastOperator = c;
        expression += " " + String(c) + " ";  // Add operator to display
    } else if (c == '=') {  
        if (inputBuffer.length() == 0 && lastOperator == '\0') {
            return; // Prevent unnecessary calculations
        }
        computeResult();  // Perform final calculation
        lastOperator = '\0';  // Reset operator
    } else if (c == 'E'){
      boolRes = "trueKernel";
      boolUpdate();
      restartWithFallbackVideo("Returning home");
    }

    videodisplay.fillRect(105, 70, 180, 30, 0);
    videodisplay.setCursor(110, 75);
    if (expression.length() < 28){
        videodisplay.println(expression.c_str());
    } else {
        expression = "";
        videodisplay.println(expression.c_str());
    }
}

static char calculatorMouseKeyAt(int x, int y) {
    if (x >= 105 && x < 195 && y >= 110 && y < 210) {
        int col = (x - 105) / 30;
        int row = (y < 145) ? 0 : (y < 175) ? 1 : 2;
        const char keys[3][3] = {
            {'1', '2', '3'},
            {'4', '5', '6'},
            {'7', '8', '9'}
        };
        return keys[row][col];
    }
    if (x >= 194 && x < 285) {
        if (y >= 110 && y < 128) return '+';
        if (y >= 128 && y < 145) return '-';
        if (y >= 145 && y < 160) return 'x';
        if (y >= 160 && y < 175) return '%';
        if (y >= 175 && y < 193) return '0';
        if (y >= 193 && y < 211) return '=';
    }
    return 0;
}

static bool handleCalculatorMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (mouse.leftPressed) {
        char key = calculatorMouseKeyAt(appCursorX, appCursorY);
        if (key) processInput(key);
    }
    appCursorCaptureAndDraw();
    return true;
}

static void processSerialCommand(String command) {
    command.trim();
    if (command.length() == 0) return;
    if (handleCalculatorMouse(command)) return;
    AppCursorDrawGuard cursorGuard;
    if (command == "Escape") {
        processInput('E');
        return;
    }
    if (command == "Enter") {
        processInput('=');
        return;
    }
    for (int i = 0; i < command.length(); ++i) {
        char c = command[i];
        if (isdigit(c) || c == '+' || c == '-' || c == 'x' || c == '%' || c == '=') {
            processInput(c);
        }
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
      SD.end();
      videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
      releaseSerialControllerVideo();
      tone(SPEAKER_PIN, 0);
      videodisplay.setFont(Font6x8);
      videodisplay.clear();
      drawCalculatorUI();
    } else if (boolRes == "trueKernel") {
      tone(SPEAKER_PIN, 0);
      boolRes = "false";
      boolUpdate();
      handleKernelFlash();
    }
}

void loop() {
    static String serialCommand = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\r') continue;
        if (c == '\n') {
            processSerialCommand(serialCommand);
            serialCommand = "";
        } else if (serialCommand.length() < 96) {
            serialCommand += c;
        } else {
            serialCommand = "";
        }
    }
}

