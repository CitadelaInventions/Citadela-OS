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
#include <math.h>

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

static const int SCREEN_WIDTH = 376;
static const int SCREEN_HEIGHT = 192;
static const int ARENA_X = 32;
static const int ARENA_Y = 28;
static const int ARENA_W = 312;
static const int ARENA_H = 150;
static const int MAX_LENGTH = 110;
static const float SEGMENT_SPACING = 4.0f;
static const int SPEAKER_PIN = 12;
static const int FRAME_INTERVAL_MS = 40;
static const int GRID_THICKNESS = 2;

struct SmoothPoint {
    float x;
    float y;
};

static String boolRes = "";
static bool started = false;
static bool gameOver = false;
static SmoothPoint snake[MAX_LENGTH];
static int snakeLength = 0;
static int directionX = 1;
static int directionY = 0;
static SmoothPoint food = {0.0f, 0.0f};
static int score = 0;
static int bestScore = 0;
static unsigned long lastFrameTime = 0;
static int lastFrameMs = 0;
static SmoothPoint renderedSnake[MAX_LENGTH];
static int renderedSnakeLength = 0;
static SmoothPoint renderedFood = {0.0f, 0.0f};
static bool renderedFoodValid = false;
static int renderedScore = -1;
static int renderedBestScore = -1;
static bool renderedStarted = false;
static bool renderedGameOver = false;
static bool snakeSceneInitialized = false;

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
    if (Update.end(true)) {
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
}
void boolUpdate() {
    SPIFFS.remove("/evil.txt");
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (file) {
        file.println(boolRes);
        file.close();
        Serial.printf("Bool updated to %s\n", boolRes.c_str());
    } else {
        Serial.println("Failed to update bool!");
    }
}
static float pointDistanceSquared(float ax, float ay, float bx, float by) {
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

static void setDirection(int x, int y) {
    if (!started || gameOver) return;
    if (x == -directionX && y == -directionY) return;
    directionX = x;
    directionY = y;
}

void moveUp() { setDirection(0, -1); }
void moveDown() { setDirection(0, 1); }
void moveLeft() { setDirection(-1, 0); }
void moveRight() { setDirection(1, 0); }

static void spawnFood() {
    const int minX = ARENA_X + 10;
    const int maxX = ARENA_X + ARENA_W - 10;
    const int minY = ARENA_Y + 10;
    const int maxY = ARENA_Y + ARENA_H - 10;
    for (int attempt = 0; attempt < 80; ++attempt) {
        float candidateX = random(minX, maxX);
        float candidateY = random(minY, maxY);
        bool clear = true;
        for (int i = 0; i < snakeLength; ++i) {
            if (pointDistanceSquared(candidateX, candidateY,
                                     snake[i].x, snake[i].y) < 110.0f) {
                clear = false;
                break;
            }
        }
        if (clear) {
            food.x = candidateX;
            food.y = candidateY;
            return;
        }
    }
    food.x = ARENA_X + ARENA_W * 0.5f;
    food.y = ARENA_Y + ARENA_H * 0.25f;
}

void initializeGame() {
    score = 0;
    snakeLength = 14;
    directionX = 1;
    directionY = 0;
    gameOver = false;
    started = true;
    float startX = ARENA_X + ARENA_W * 0.5f;
    float startY = ARENA_Y + ARENA_H * 0.5f;
    for (int i = 0; i < snakeLength; ++i) {
        snake[i].x = startX - i * SEGMENT_SPACING;
        snake[i].y = startY;
    }
    spawnFood();
    lastFrameTime = millis();
}

static void growSnake(int amount) {
    while (amount-- > 0 && snakeLength < MAX_LENGTH) {
        snake[snakeLength] = snake[snakeLength - 1];
        ++snakeLength;
    }
}

static void updateSnake(float deltaTime) {
    if (!started || gameOver) return;
    float speed = min(82.0f, 45.0f + score * 1.35f);
    snake[0].x += directionX * speed * deltaTime;
    snake[0].y += directionY * speed * deltaTime;

    for (int i = 1; i < snakeLength; ++i) {
        float dx = snake[i - 1].x - snake[i].x;
        float dy = snake[i - 1].y - snake[i].y;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance > SEGMENT_SPACING && distance > 0.001f) {
            float correction = (distance - SEGMENT_SPACING) / distance;
            snake[i].x += dx * correction;
            snake[i].y += dy * correction;
        }
    }

    const float edge = 4.0f;
    if (snake[0].x < ARENA_X + edge ||
        snake[0].x > ARENA_X + ARENA_W - edge ||
        snake[0].y < ARENA_Y + edge ||
        snake[0].y > ARENA_Y + ARENA_H - edge) {
        gameOver = true;
    }
    for (int i = 9; i < snakeLength && !gameOver; ++i) {
        if (pointDistanceSquared(snake[0].x, snake[0].y,
                                 snake[i].x, snake[i].y) < 8.0f) {
            gameOver = true;
        }
    }

    if (!gameOver && pointDistanceSquared(snake[0].x, snake[0].y,
                                           food.x, food.y) < 50.0f) {
        ++score;
        bestScore = max(bestScore, score);
        growSnake(3);
        spawnFood();
        tone(SPEAKER_PIN, 760 + score * 12, 55);
    }
    if (gameOver) {
        bestScore = max(bestScore, score);
        tone(SPEAKER_PIN, 175, 250);
    }
}

static void drawArenaPatch(int x, int y, int w, int h) {
    int x0 = max(x, ARENA_X + 2);
    int y0 = max(y, ARENA_Y + 2);
    int x1 = min(x + w, ARENA_X + ARENA_W - 2);
    int y1 = min(y + h, ARENA_Y + ARENA_H - 2);
    if (x1 <= x0 || y1 <= y0) return;

    uint32_t arena = videodisplay.RGB(10, 31, 34);
    uint32_t grid = videodisplay.RGB(17, 49, 49);
    videodisplay.fillRect(x0, y0, x1 - x0, y1 - y0, arena);
    for (int gridX = ARENA_X + 24; gridX < ARENA_X + ARENA_W; gridX += 24) {
        int lineStart = max(x0, gridX);
        int lineEnd = min(x1, gridX + GRID_THICKNESS);
        if (lineEnd > lineStart) {
            videodisplay.fillRect(lineStart, y0, lineEnd - lineStart,
                                  y1 - y0, grid);
        }
    }
    for (int gridY = ARENA_Y + 22; gridY < ARENA_Y + ARENA_H; gridY += 22) {
        int lineStart = max(y0, gridY);
        int lineEnd = min(y1, gridY + GRID_THICKNESS);
        if (lineEnd > lineStart) {
            videodisplay.fillRect(x0, lineStart, x1 - x0,
                                  lineEnd - lineStart, grid);
        }
    }
}

static void drawArenaBorder() {
    uint32_t edge = videodisplay.RGB(35, 83, 84);
    uint32_t accent = videodisplay.RGB(65, 218, 187);
    videodisplay.fillRect(ARENA_X - 2, ARENA_Y - 2, ARENA_W + 4, 2, edge);
    videodisplay.fillRect(ARENA_X - 2, ARENA_Y + ARENA_H, ARENA_W + 4, 2, edge);
    videodisplay.fillRect(ARENA_X - 2, ARENA_Y, 2, ARENA_H, edge);
    videodisplay.fillRect(ARENA_X + ARENA_W, ARENA_Y, 2, ARENA_H, edge);
    videodisplay.fillRect(ARENA_X, ARENA_Y, ARENA_W, 2, accent);
    videodisplay.fillRect(ARENA_X, ARENA_Y + ARENA_H - 2, ARENA_W, 2, accent);
    videodisplay.fillRect(ARENA_X, ARENA_Y + 2, 2, ARENA_H - 4, accent);
    videodisplay.fillRect(ARENA_X + ARENA_W - 2, ARENA_Y + 2, 2,
                          ARENA_H - 4, accent);
}

static void drawArenaBase() {
    videodisplay.fillRect(ARENA_X, ARENA_Y, ARENA_W, ARENA_H,
                          videodisplay.RGB(10, 31, 34));
    drawArenaPatch(ARENA_X + 2, ARENA_Y + 2,
                   ARENA_W - 4, ARENA_H - 4);
    drawArenaBorder();
}

static void restoreActorPatch(float x, float y, int radius) {
    int centerX = (int)roundf(x);
    int centerY = (int)roundf(y);
    drawArenaPatch(centerX - radius, centerY - radius,
                   radius * 2 + 1, radius * 2 + 1);
}

static void eraseRenderedActors() {
    for (int i = 0; i < renderedSnakeLength; ++i) {
        restoreActorPatch(renderedSnake[i].x, renderedSnake[i].y, 5);
    }
    if (renderedFoodValid) {
        restoreActorPatch(renderedFood.x, renderedFood.y, 6);
    }
}

static void drawFoodSprite() {
    videodisplay.fillCircle((int)food.x, (int)food.y, 5,
                            videodisplay.RGB(112, 32, 47));
    videodisplay.fillCircle((int)food.x, (int)food.y, 3,
                            videodisplay.RGB(255, 92, 75));
    videodisplay.fillCircle((int)food.x - 1, (int)food.y - 2, 1,
                            videodisplay.RGB(255, 232, 132));
}

static void drawSnakeSprite() {
    for (int i = snakeLength - 1; i >= 0; --i) {
        float t = snakeLength > 1 ? i / (float)(snakeLength - 1) : 0.0f;
        int red = (int)(30 + t * 86);
        int green = (int)(224 - t * 66);
        int blue = (int)(196 - t * 126);
        int radius = i == 0 ? 4 : 3;
        videodisplay.fillCircle((int)roundf(snake[i].x),
                                (int)roundf(snake[i].y), radius,
                                videodisplay.RGB(4, 15, 18));
        videodisplay.fillCircle((int)roundf(snake[i].x),
                                (int)roundf(snake[i].y), radius - 1,
                                videodisplay.RGB(red, green, blue));
    }
    if (snakeLength > 0) {
        int hx = (int)roundf(snake[0].x);
        int hy = (int)roundf(snake[0].y);
        int eyeOffsetX = directionY != 0 ? 2 : directionX * 2;
        int eyeOffsetY = directionX != 0 ? -2 : directionY * 2;
        videodisplay.fillCircle(hx + eyeOffsetX, hy + eyeOffsetY, 1,
                                videodisplay.RGB(250, 250, 240));
    }
}

static void drawScoreHeader() {
    uint32_t header = videodisplay.RGB(14, 25, 31);
    uint32_t muted = videodisplay.RGB(145, 177, 181);
    videodisplay.fillRect(239, 2, SCREEN_WIDTH - 239, 17, header);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(muted, header);
    videodisplay.setCursor(244, 7);
    videodisplay.print("SCORE ");
    videodisplay.print(score);
    videodisplay.print("  BEST ");
    videodisplay.print(bestScore);
}

static void drawSnakePanel() {
    if (started && !gameOver) return;
    uint32_t panel = videodisplay.RGB(15, 39, 43);
    uint32_t accent = videodisplay.RGB(65, 218, 187);
    uint32_t white = videodisplay.RGB(242, 248, 247);
    uint32_t muted = videodisplay.RGB(145, 177, 181);
    uint32_t panelEdge = gameOver ? videodisplay.RGB(255, 91, 80) : accent;
    videodisplay.fillRect(101, 67, 174, 58, panel);
    videodisplay.rect(101, 67, 174, 58, panelEdge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(panelEdge, panel);
    videodisplay.setCursor(gameOver ? 155 : 143, 79);
    videodisplay.print(gameOver ? "GAME OVER" : "CITADELA SNAKE");
    videodisplay.setTextColor(white, panel);
    videodisplay.setCursor(129, 96);
    videodisplay.print(gameOver ? "ENTER TO TRY AGAIN" : "ENTER OR CLICK TO START");
    if (gameOver) {
        videodisplay.setTextColor(muted, panel);
        videodisplay.setCursor(158, 109);
        videodisplay.print("SCORE ");
        videodisplay.print(score);
    }
}

static void drawStaticSnakeScene() {
    uint32_t background = videodisplay.RGB(7, 12, 17);
    uint32_t header = videodisplay.RGB(14, 25, 31);
    uint32_t accent = videodisplay.RGB(65, 218, 187);
    uint32_t white = videodisplay.RGB(242, 248, 247);
    uint32_t muted = videodisplay.RGB(145, 177, 181);

    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 22, header);
    videodisplay.fillRect(0, 20, SCREEN_WIDTH, 2, accent);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, header);
    videodisplay.setCursor(8, 7);
    videodisplay.print("CSNAKE");
    videodisplay.setTextColor(muted, header);
    videodisplay.setCursor(57, 7);
    videodisplay.print("SMOOTH ARENA");
    drawArenaBase();

    videodisplay.setTextColor(muted, background);
    videodisplay.setCursor(72, 183);
    videodisplay.print("ARROWS / WASD / MOUSE MOVE   ENTER RESTART   ESC HOME");
}

static void rememberRenderedActors() {
    renderedSnakeLength = snakeLength;
    for (int i = 0; i < snakeLength; ++i) renderedSnake[i] = snake[i];
    renderedFood = food;
    renderedFoodValid = true;
    renderedScore = score;
    renderedBestScore = bestScore;
    renderedStarted = started;
    renderedGameOver = gameOver;
}

static void drawSnakeScene() {
    bool stateChanged = snakeSceneInitialized &&
        (renderedStarted != started || renderedGameOver != gameOver);
    if (snakeSceneInitialized && !stateChanged && (!started || gameOver)) return;

    AppCursorDrawGuard cursorGuard;
    if (!snakeSceneInitialized) {
        drawStaticSnakeScene();
    } else {
        eraseRenderedActors();
        if (stateChanged) {
            drawArenaPatch(99, 65, 178, 62);
        }
        drawArenaBorder();
    }

    drawFoodSprite();
    drawSnakeSprite();
    if (!snakeSceneInitialized || renderedScore != score ||
        renderedBestScore != bestScore) {
        drawScoreHeader();
    }
    drawSnakePanel();
    videodisplay.show();
    rememberRenderedActors();
    snakeSceneInitialized = true;
}

static bool handleSnakeMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (mouse.leftPressed && !started) {
        initializeGame();
    } else if (mouse.leftPressed && gameOver) {
        initializeGame();
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

static void handleSnakeCommand(String ctrlInput) {
    ctrlInput.trim();
    if (ctrlInput.length() == 0 || ctrlInput == "rlsd") return;
    if (handleSnakeMouse(ctrlInput)) return;
    if (ctrlInput == "STATUS") {
        Serial.printf("CSNAKE started=%d gameOver=%d score=%d length=%d head=%.1f,%.1f\n",
                      started, gameOver, score, snakeLength,
                      snakeLength ? snake[0].x : -1.0f,
                      snakeLength ? snake[0].y : -1.0f);
    } else if (ctrlInput == "LeftArrow" || ctrlInput == "a" || ctrlInput == "A") {
        moveLeft();
    } else if (ctrlInput == "RightArrow" || ctrlInput == "d" || ctrlInput == "D") {
        moveRight();
    } else if (ctrlInput == "UpArrow" || ctrlInput == "w" || ctrlInput == "W") {
        moveUp();
    } else if (ctrlInput == "DownArrow" || ctrlInput == "s" || ctrlInput == "S") {
        moveDown();
    } else if (ctrlInput == "Enter") {
        initializeGame();
    } else if (ctrlInput == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting CSnake");
    } else if (ctrlInput == "Escape") {
        boolRes = "trueKernel";
        boolUpdate();
        restartWithFallbackVideo("Returning home");
    }
}

static void pollSnakePort(HardwareSerial &port, String &buffer) {
    while (port.available()) {
        char c = (char)port.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handleSnakeCommand(buffer);
            buffer = "";
        } else if (buffer.length() < 96) {
            buffer += c;
        } else {
            buffer = "";
        }
    }
}

void controller() {
    static String controllerBuffer = "";
    static String usbBuffer = "";
    pollSnakePort(Serial1, controllerBuffer);
    pollSnakePort(Serial, usbBuffer);
}

void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
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
      bool videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
      Serial.printf("CSnake video init: %s\n", videoReady ? "ready" : "failed");
      if (!videoReady) restartWithFallbackVideo("CSnake video failed");
      releaseSerialControllerVideo();
      tone(SPEAKER_PIN, 0);
      videodisplay.setFont(Font6x8);
      randomSeed(micros() ^ analogRead(34));
      snakeLength = 0;
      spawnFood();
      drawSnakeScene();
    } else if (boolRes == "trueKernel") {
      tone(SPEAKER_PIN, 0);
      boolRes = "false";
      boolUpdate();
      handleKernelFlash();
    }
    lastFrameTime = millis();
}

void loop() {
    controller();
    unsigned long frameStart = millis();
    unsigned long elapsed = frameStart - lastFrameTime;
    if (elapsed < FRAME_INTERVAL_MS) {
        delay(1);
        return;
    }
    lastFrameTime = frameStart;
    updateSnake(min(elapsed / 1000.0f, 0.05f));
    drawSnakeScene();
    lastFrameMs = millis() - frameStart;
}
