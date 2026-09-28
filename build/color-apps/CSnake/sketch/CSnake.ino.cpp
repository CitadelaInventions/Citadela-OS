#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
#include <Wire.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <Ressources/CodePage437_8x19.h>
#include <Update.h>
#include "esp_partition.h"
#include "SD.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "FS.h"

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 376;
static const int APP_CURSOR_SCREEN_H = 288;
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

#line 50 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorLoadConfigOnce();
#line 71 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 76 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 81 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 86 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 91 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 95 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 101 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorRestore();
#line 112 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 119 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorCaptureAndDraw();
#line 151 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void appCursorRefreshAfterRedraw();
#line 161 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 182 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void restartWithFallbackVideo(const char *label);
#line 199 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void releaseSerialControllerVideo();
#line 233 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 244 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 249 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 258 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void handleKernelFlash();
#line 315 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void boolTru();
#line 331 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void boolUpdate();
#line 343 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void spawnFood();
#line 348 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void moveUp();
#line 355 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void moveDown();
#line 362 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void moveLeft();
#line 369 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void moveRight();
#line 376 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void initializeGame();
#line 397 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
static bool handleSnakeMouse(String input);
#line 419 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void controller();
#line 456 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void setup();
#line 505 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
void loop();
#line 50 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CSnake\\CSnake.ino"
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

bool started = false;

#define SCREEN_WIDTH  370
#define SCREEN_HEIGHT 270
#define SNAKE_SIZE  4
#define MAX_LENGTH  100
#define SPEAKER_PIN 12

String boolRes = "";
String appName = "";
String appString = "";

struct Point {
    int x, y;
};

int score = 0;
Point snake[MAX_LENGTH];
int snakeLength;
int dirX, dirY;
Point food;
bool gameOver;

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
void spawnFood() {
    food.x = (random(SCREEN_WIDTH / SNAKE_SIZE) * SNAKE_SIZE);
    food.y = (random(SCREEN_HEIGHT / SNAKE_SIZE) * SNAKE_SIZE);
}

void moveUp() {
    if (dirY == 0) {
        dirX = 0;
        dirY = -SNAKE_SIZE;
    }
}

void moveDown() {
    if (dirY == 0) {
        dirX = 0;
        dirY = SNAKE_SIZE;
    }
}

void moveLeft() {
    if (dirX == 0) {
        dirX = -SNAKE_SIZE;
        dirY = 0;
    }
}

void moveRight() {
    if (dirX == 0) {
        dirX = SNAKE_SIZE;
        dirY = 0;
    }
}

void initializeGame() {
    AppCursorDrawGuard cursorGuard;
    videodisplay.clear();
    randomSeed(analogRead(0));
    // Reset game variables
    snakeLength = 5;
    dirX = SNAKE_SIZE;
    dirY = 0;
    gameOver = false;

    // Ensure snake starts within screen bounds
    int startX = (SCREEN_WIDTH / 2 / SNAKE_SIZE) * SNAKE_SIZE;
    int startY = (SCREEN_HEIGHT / 5 / SNAKE_SIZE) * SNAKE_SIZE;

    for (int i = 0; i < snakeLength; i++) {
        snake[i].x = startX - i * SNAKE_SIZE;
        snake[i].y = startY;
    }
    spawnFood();
}

static bool handleSnakeMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (mouse.leftPressed && !started) {
        initializeGame();
        started = true;
    }
    if (started && (mouse.dx != 0 || mouse.dy != 0)) {
        if (abs(mouse.dx) >= abs(mouse.dy)) {
            if (mouse.dx < 0) moveLeft();
            else if (mouse.dx > 0) moveRight();
        } else {
            if (mouse.dy < 0) moveUp();
            else if (mouse.dy > 0) moveDown();
        }
    }
    appCursorCaptureAndDraw();
    return true;
}

void controller() {
    static String ctrlInput = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            ctrlInput.trim();
            Serial.println(ctrlInput);
            if (handleSnakeMouse(ctrlInput)) {
                ctrlInput = "";
                continue;
            }
            AppCursorDrawGuard cursorGuard;
            if (ctrlInput == "LeftArrow") {
                moveLeft();
            } else if (ctrlInput == "RightArrow") {
                moveRight();
            } else if (ctrlInput == "UpArrow") {
                moveUp();
            } else if (ctrlInput == "DownArrow") {
                moveDown();
            } else if (ctrlInput == "Enter") {
                initializeGame();
                started = true;
            } else if (ctrlInput == "RightGUI (Win) +"){
                restartWithFallbackVideo("Restarting");
            } else if (ctrlInput == "Escape"){
                boolRes = "trueKernel";
                boolUpdate();
                restartWithFallbackVideo("Returning home");
            }
            ctrlInput = "";
        } else {
            ctrlInput += c;
        }
    }
}

void setup() {
    pinMode(SPEAKER_PIN, 0);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
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
    started = false;
    if (boolRes == "false"){
      SD.end();
      Serial1.println("res");
      videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
      releaseSerialControllerVideo();
      tone(SPEAKER_PIN, 0);
      videodisplay.setFont(CodePage437_8x19);
      videodisplay.setCursor(SCREEN_WIDTH / 2 - 60, SCREEN_HEIGHT / 2);
      videodisplay.println("Snake :: Citadela");
      videodisplay.setFont(Font6x8);
      videodisplay.println("Press Enter to start!");
      videodisplay.setCursor(255+60,255+10);
      videodisplay.print("Score:");
      videodisplay.print(score);
    } else if (boolRes == "trueKernel") {
      tone(SPEAKER_PIN, 0);
      boolRes = "false";
      boolUpdate();
      handleKernelFlash();
    }
}

void loop() {
    controller();
    if (started) {
        AppCursorDrawGuard cursorGuard;
        if (gameOver) {
            videodisplay.clear();
            videodisplay.setCursor(SCREEN_WIDTH / 2 - 24, SCREEN_HEIGHT / 2 - 5);
            videodisplay.print("Game Over");
            videodisplay.show();
            delay(2000);
            started = false;  // Prevent the game from continuing until restarted
            return;
        }

        // Erase the tail of the snake before moving
        videodisplay.rect(snake[snakeLength - 1].x, snake[snakeLength - 1].y, SNAKE_SIZE, SNAKE_SIZE, videodisplay.RGB(0, 0, 0));

        // Move snake
        for (int i = snakeLength - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0].x += dirX;
        snake[0].y += dirY;

        // Check collision with walls
        if (snake[0].x < 0 || snake[0].x >= SCREEN_WIDTH ||
            snake[0].y < 0 || snake[0].y >= SCREEN_HEIGHT) {
            gameOver = true;
        }

        // Check collision with itself
        for (int i = 1; i < snakeLength; i++) {
            if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
                gameOver = true;
            }
        }

        // Check food collision
        if (snake[0].x == food.x && snake[0].y == food.y) {
            if (snakeLength < MAX_LENGTH) snakeLength++;
            score+=1;
            videodisplay.fillRect(255+60,255+10, 10, 50,0);
            videodisplay.setCursor(255+60,255+10);
            videodisplay.print("Score:");
            videodisplay.print(score);
            spawnFood();
        }

        // Draw snake head
        videodisplay.rect(snake[0].x, snake[0].y, SNAKE_SIZE, SNAKE_SIZE, videodisplay.RGB(255, 255, 255));

        // Draw food
        videodisplay.rect(food.x, food.y, SNAKE_SIZE, SNAKE_SIZE, videodisplay.RGB(255, 255, 255));

        videodisplay.show();
        delay(100);
    }
}

