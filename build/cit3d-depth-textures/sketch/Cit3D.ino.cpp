#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <Ressources/CodePage437_9x16.h>
#include <esp_ota_ops.h>
#include "esp_partition.h"
#include <FS.h>
#include <SPIFFS.h>
#include <math.h>

#define SPEAKER_PIN 12
#define SCREEN_WIDTH 376
#define SCREEN_HEIGHT 192
#define VIEW_TOP 20
#define VIEW_BOTTOM 170
#define VIEW_HEIGHT (VIEW_BOTTOM - VIEW_TOP)
#define MAP_WIDTH 16
#define MAP_HEIGHT 16
#define FOV 1.04719755f
#define MAX_DEPTH 24.0f
#define RENDER_STEP 2
#define FLOOR_SAMPLE_X 4
#define FLOOR_SAMPLE_Y 3
#define WALL_SAMPLE_Y 4
#define MAX_WORLD_OBJECTS 14
#define FRAME_INTERVAL_MS 40

// --- Global Objects and Variables ---
CompositeColorDAC videodisplay;

#line 34 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void restartWithFallbackVideo(const char *label);
#line 51 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void releaseSerialControllerVideo();
#line 215 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 226 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 231 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 298 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool mapIsWall(int x, int y);
#line 317 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool objectBlocksPosition(float x, float y);
#line 330 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static float normalizeAngle(float angle);
#line 336 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool canStand(float x, float y);
#line 345 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void tryMove(float dx, float dy);
#line 416 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool hasLineOfSight(float targetX, float targetY);
#line 466 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool handleCit3DMouseReport(String input);
#line 485 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void handleCit3DCommand(String command);
#line 510 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void pollCit3DPort(HardwareSerial &port, String &buffer);
#line 576 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static uint8_t quantizedChannel(int value, int levels, uint8_t threshold);
#line 585 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static uint32_t sceneColor(int r, int g, int b, int x, int y);
#line 602 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static uint32_t litSceneColor(int r, int g, int b, float light, int x, int y);
#line 610 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static uint32_t floorTextureColor(float worldX, float worldY, float distance, int x, int y);
#line 979 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void drawHud(bool force);
#line 1054 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void drawFrame(bool forceHud);
#line 1086 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void setup();
#line 1132 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void loop();
#line 34 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
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
String boolRes = "";

enum WorldObjectType : uint8_t {
    OBJECT_TARGET = 0,
    OBJECT_CRATE,
    OBJECT_LAMP,
    OBJECT_COLUMN
};

struct WorldObject {
    float x;
    float y;
    WorldObjectType type;
    bool objective;
    bool active;
};

struct RayHit {
    float distance;
    int mapX;
    int mapY;
    bool side;
    float wallU;
    float normalX;
    float normalY;
};

struct ObjectRayHit {
    bool hit;
    WorldObjectType type;
    int objectIndex;
    float distance;
    float normalX;
    float normalY;
    float textureU;
    float silhouetteX;
};

static float playerX = 1.7f;
static float playerY = 1.7f;
static float playerAngle = 0.0f;
static float cameraPitch = 0.0f;
static unsigned long lastFrameTime = 0;
static String currentAction = "";
static bool cit3DMouseWasLeftDown = false;
static int objectivesRemaining = 0;
static int score = 0;
static int health = 100;
static int shotFlashFrames = 0;
static int lastFrameMs = 0;
static bool sceneDirty = true;
static float depthBuffer[SCREEN_WIDTH / RENDER_STEP];
static uint8_t visibleObjectIndices[MAX_WORLD_OBJECTS];
static int visibleObjectCount = 0;

static const char worldMap[MAP_HEIGHT][MAP_WIDTH + 1] = {
    "1111111111111111",
    "1000000000000001",
    "1011100010111001",
    "1000100010000001",
    "1010101110101101",
    "1010001000100001",
    "1011101011101001",
    "1000001000001001",
    "1011111010111011",
    "1000000010000001",
    "1010111110101101",
    "1010000010100001",
    "1011101010111001",
    "1000001000000001",
    "1000000000000001",
    "1111111111111111"
};

static WorldObject worldObjects[MAX_WORLD_OBJECTS] = {
    {6.5f, 1.5f, OBJECT_TARGET, true, true},
    {13.5f, 3.5f, OBJECT_TARGET, true, true},
    {5.5f, 5.5f, OBJECT_TARGET, true, true},
    {11.5f, 7.5f, OBJECT_TARGET, true, true},
    {6.5f, 9.5f, OBJECT_TARGET, true, true},
    {13.5f, 13.5f, OBJECT_TARGET, true, true},
    {3.5f, 1.5f, OBJECT_CRATE, false, true},
    {10.5f, 3.5f, OBJECT_LAMP, false, true},
    {3.5f, 7.5f, OBJECT_COLUMN, false, true},
    {13.5f, 9.5f, OBJECT_CRATE, false, true},
    {5.5f, 11.5f, OBJECT_LAMP, false, true},
    {9.5f, 13.5f, OBJECT_COLUMN, false, true},
    {2.5f, 14.5f, OBJECT_CRATE, false, true},
    {12.5f, 14.5f, OBJECT_LAMP, false, true}
};

void boolUpdate();
void boolTru();
void handleKernelFlash();
void initializeGameWorld();
void fireGun();
void controllerSystem(float deltaTime);
void drawFrame(bool forceHud = false);
static float objectRadius(WorldObjectType type);
static float objectHeight(WorldObjectType type);
static RayHit castRay(float rayDirX, float rayDirY);
static ObjectRayHit castObjectRay(float rayDirX, float rayDirY,
                                  float wallDistance);
static void prepareVisibleObjects();
static void drawSkyAndFloor(float dirX, float dirY, float planeX,
                            float planeY, int horizon);
static void drawWallStripe(int x, int stripeWidth, int horizon,
                           const RayHit &hit);
static void drawObjectStripe(int x, int stripeWidth, int horizon,
                             const ObjectRayHit &hit);
static uint32_t wallTextureColor(const RayHit &hit, float v,
                                 int x, int y);
static bool intersectCircleObject(const WorldObject &object,
                                  float rayDirX, float rayDirY,
                                  float radius, float maxDistance,
                                  float &distance, float &normalX,
                                  float &normalY, float &textureU,
                                  float &silhouetteX);
static bool intersectBoxObject(const WorldObject &object,
                               float rayDirX, float rayDirY,
                               float halfSize, float maxDistance,
                               float &distance, float &normalX,
                               float &normalY, float &textureU,
                               float &silhouetteX);
static bool objectShapeVisible(const ObjectRayHit &hit, float v);
static uint32_t objectSurfaceColor(const ObjectRayHit &hit, float v,
                                   int x, int y);

// --- Function Implementations ---

// Updates the content of the bool file
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

// Reads the bool file and stores its content in boolRes
void boolTru() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
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

// Flashes a new kernel if required
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

static bool mapIsWall(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return true;
    return worldMap[y][x] == '1';
}

static float objectRadius(WorldObjectType type) {
    if (type == OBJECT_CRATE) return 0.29f;
    if (type == OBJECT_COLUMN) return 0.24f;
    if (type == OBJECT_TARGET) return 0.22f;
    return 0.14f;
}

static float objectHeight(WorldObjectType type) {
    if (type == OBJECT_COLUMN) return 1.16f;
    if (type == OBJECT_LAMP) return 0.96f;
    if (type == OBJECT_TARGET) return 0.92f;
    return 0.58f;
}

static bool objectBlocksPosition(float x, float y) {
    const float playerRadius = 0.18f;
    for (int i = 0; i < MAX_WORLD_OBJECTS; ++i) {
        const WorldObject &object = worldObjects[i];
        if (!object.active) continue;
        float limit = objectRadius(object.type) + playerRadius;
        float dx = x - object.x;
        float dy = y - object.y;
        if (dx * dx + dy * dy < limit * limit) return true;
    }
    return false;
}

static float normalizeAngle(float angle) {
    while (angle > PI) angle -= TWO_PI;
    while (angle < -PI) angle += TWO_PI;
    return angle;
}

static bool canStand(float x, float y) {
    const float radius = 0.18f;
    return !objectBlocksPosition(x, y) &&
           !mapIsWall((int)(x - radius), (int)(y - radius)) &&
           !mapIsWall((int)(x + radius), (int)(y - radius)) &&
           !mapIsWall((int)(x - radius), (int)(y + radius)) &&
           !mapIsWall((int)(x + radius), (int)(y + radius));
}

static void tryMove(float dx, float dy) {
    float oldX = playerX;
    float oldY = playerY;
    if (canStand(playerX + dx, playerY)) playerX += dx;
    if (canStand(playerX, playerY + dy)) playerY += dy;
    if (playerX != oldX || playerY != oldY) sceneDirty = true;
}

static RayHit castRay(float rayDirX, float rayDirY) {
    int mapX = (int)playerX;
    int mapY = (int)playerY;
    float deltaDistX = rayDirX == 0.0f ? 1.0e8f : fabsf(1.0f / rayDirX);
    float deltaDistY = rayDirY == 0.0f ? 1.0e8f : fabsf(1.0f / rayDirY);
    int stepX = rayDirX < 0.0f ? -1 : 1;
    int stepY = rayDirY < 0.0f ? -1 : 1;
    float sideDistX = rayDirX < 0.0f
        ? (playerX - mapX) * deltaDistX
        : (mapX + 1.0f - playerX) * deltaDistX;
    float sideDistY = rayDirY < 0.0f
        ? (playerY - mapY) * deltaDistY
        : (mapY + 1.0f - playerY) * deltaDistY;
    bool side = false;

    for (int step = 0; step < 64; ++step) {
        if (sideDistX < sideDistY) {
            sideDistX += deltaDistX;
            mapX += stepX;
            side = false;
        } else {
            sideDistY += deltaDistY;
            mapY += stepY;
            side = true;
        }
        if (mapIsWall(mapX, mapY)) break;
    }

    float distance = side
        ? (mapY - playerY + (1 - stepY) * 0.5f) / rayDirY
        : (mapX - playerX + (1 - stepX) * 0.5f) / rayDirX;
    if (!isfinite(distance) || distance < 0.02f) distance = 0.02f;
    if (distance > MAX_DEPTH) distance = MAX_DEPTH;

    float wallCoordinate = side
        ? playerX + distance * rayDirX
        : playerY + distance * rayDirY;
    wallCoordinate -= floorf(wallCoordinate);
    if ((!side && rayDirX > 0.0f) || (side && rayDirY < 0.0f)) {
        wallCoordinate = 1.0f - wallCoordinate;
    }
    float normalX = side ? 0.0f : (float)-stepX;
    float normalY = side ? (float)-stepY : 0.0f;
    return {distance, mapX, mapY, side, wallCoordinate, normalX, normalY};
}

void initializeGameWorld() {
    playerX = 1.7f;
    playerY = 1.7f;
    playerAngle = 0.0f;
    cameraPitch = 0.0f;
    currentAction = "";
    score = 0;
    health = 100;
    objectivesRemaining = 0;
    shotFlashFrames = 0;
    for (int i = 0; i < MAX_WORLD_OBJECTS; ++i) {
        worldObjects[i].active = true;
        if (worldObjects[i].objective) ++objectivesRemaining;
    }
    sceneDirty = true;
}

static bool hasLineOfSight(float targetX, float targetY) {
    float dx = targetX - playerX;
    float dy = targetY - playerY;
    float distance = sqrtf(dx * dx + dy * dy);
    if (distance < 0.01f) return true;
    dx /= distance;
    dy /= distance;
    for (float travelled = 0.12f; travelled < distance - 0.18f; travelled += 0.10f) {
        if (mapIsWall((int)(playerX + dx * travelled),
                      (int)(playerY + dy * travelled))) return false;
    }
    return true;
}

void fireGun() {
    shotFlashFrames = 2;
    sceneDirty = true;
    tone(SPEAKER_PIN, 1850, 42);
    int bestTarget = -1;
    float bestDistance = MAX_DEPTH;
    int horizon = constrain((int)(VIEW_TOP + VIEW_HEIGHT * 0.5f + cameraPitch),
                            VIEW_TOP + 22, VIEW_BOTTOM - 22);
    const int crosshairY = VIEW_TOP + VIEW_HEIGHT / 2;

    for (int i = 0; i < MAX_WORLD_OBJECTS; ++i) {
        WorldObject &object = worldObjects[i];
        if (!object.active || !object.objective) continue;
        float dx = object.x - playerX;
        float dy = object.y - playerY;
        float distance = sqrtf(dx * dx + dy * dy);
        float angularError = fabsf(normalizeAngle(atan2f(dy, dx) - playerAngle));
        float projectedHeight = VIEW_HEIGHT * 0.78f / max(distance, 0.1f);
        float horizontalTolerance = 0.035f + 0.16f / max(distance, 0.4f);
        bool verticalHit = abs(crosshairY - horizon) <= projectedHeight * 0.48f;
        if (distance < bestDistance && angularError <= horizontalTolerance &&
            verticalHit && hasLineOfSight(object.x, object.y)) {
            bestTarget = i;
            bestDistance = distance;
        }
    }

    if (bestTarget >= 0) {
        worldObjects[bestTarget].active = false;
        --objectivesRemaining;
        score += 250 + (int)((MAX_DEPTH - bestDistance) * 10.0f);
        tone(SPEAKER_PIN, 980, 75);
        Serial.printf("Objective destroyed. Remaining: %d\n", objectivesRemaining);
    }
}

static bool handleCit3DMouseReport(String input) {
    input.trim();
    if (!input.startsWith("MOUSE")) return false;
    int x = 0, y = 0, buttons = 0, dx = 0, dy = 0, wheel = 0;
    int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d",
                        &x, &y, &buttons, &dx, &dy, &wheel);
    if (parsed >= 3) {
        if (dx != 0 || dy != 0) {
            playerAngle = normalizeAngle(playerAngle + dx * 0.0065f);
            cameraPitch = constrain(cameraPitch - dy * 0.42f, -46.0f, 46.0f);
            sceneDirty = true;
        }
        bool leftDown = (buttons & 0x01) != 0;
        if (leftDown && !cit3DMouseWasLeftDown) fireGun();
        cit3DMouseWasLeftDown = leftDown;
    }
    return true;
}

static void handleCit3DCommand(String command) {
    command.trim();
    if (command.length() == 0) return;
    if (handleCit3DMouseReport(command)) return;
    if (command == "STATUS") {
        Serial.printf("CIT3D x=%.2f y=%.2f angle=%.2f pitch=%.1f targets=%d score=%d frame=%dms\n",
                      playerX, playerY, playerAngle, cameraPitch,
                      objectivesRemaining, score, lastFrameMs);
    } else if (command == "rlsd") {
        currentAction = "";
    } else if (command == "Enter" || command == "Space") {
        fireGun();
    } else if (command == "Escape") {
        boolRes = "trueKernel";
        boolUpdate();
        restartWithFallbackVideo("Returning home");
    } else if (command == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting Cit3D");
    } else if (command == "r" || command == "R") {
        initializeGameWorld();
    } else {
        currentAction = command;
    }
}

static void pollCit3DPort(HardwareSerial &port, String &buffer) {
    while (port.available()) {
        char c = (char)port.read();
        if (c == '\r') continue;
        if (c == '\n') {
            handleCit3DCommand(buffer);
            buffer = "";
        } else if (buffer.length() < 96) {
            buffer += c;
        } else {
            buffer = "";
        }
    }
}

void controllerSystem(float deltaTime) {
    static String controllerBuffer = "";
    static String usbBuffer = "";
    pollCit3DPort(Serial1, controllerBuffer);
    pollCit3DPort(Serial, usbBuffer);

    float move = 2.35f * deltaTime;
    float turn = 1.75f * deltaTime;
    float dirX = cosf(playerAngle);
    float dirY = sinf(playerAngle);
    if (currentAction == "w" || currentAction == "W") {
        tryMove(dirX * move, dirY * move);
    } else if (currentAction == "s" || currentAction == "S") {
        tryMove(-dirX * move, -dirY * move);
    } else if (currentAction == "a" || currentAction == "A") {
        tryMove(dirY * move, -dirX * move);
    } else if (currentAction == "d" || currentAction == "D") {
        tryMove(-dirY * move, dirX * move);
    } else if (currentAction == "LeftArrow") {
        playerAngle = normalizeAngle(playerAngle - turn);
        sceneDirty = true;
    } else if (currentAction == "RightArrow") {
        playerAngle = normalizeAngle(playerAngle + turn);
        sceneDirty = true;
    } else if (currentAction == "UpArrow") {
        cameraPitch = min(46.0f, cameraPitch + 54.0f * deltaTime);
        sceneDirty = true;
    } else if (currentAction == "DownArrow") {
        cameraPitch = max(-46.0f, cameraPitch - 54.0f * deltaTime);
        sceneDirty = true;
    }
}

static const uint8_t orderedDither4x4[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
};

static uint32_t textureHash(int a, int b, int c = 0, int d = 0) {
    uint32_t value = (uint32_t)a * 0x45d9f3bu;
    value ^= (uint32_t)b * 0x119de1f3u;
    value ^= (uint32_t)c * 0x3449f5u;
    value ^= (uint32_t)d * 0x27d4eb2du;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

static uint8_t quantizedChannel(int value, int levels, uint8_t threshold) {
    value = constrain(value, 0, 255);
    int scaled = value * (levels - 1);
    int lower = scaled / 255;
    int remainder = scaled - lower * 255;
    if (lower < levels - 1 && remainder * 16 > threshold * 255) ++lower;
    return (uint8_t)((lower * 255 + (levels - 2) / 2) / (levels - 1));
}

static uint32_t sceneColor(int r, int g, int b, int x, int y) {
    r = constrain(r, 0, 255);
    g = constrain(g, 0, 255);
    b = constrain(b, 0, 255);
    int maximum = max(r, max(g, b));
    int minimum = min(r, min(g, b));
    if (maximum - minimum <= 6) {
        int gray = (r + g + b) / 3;
        return videodisplay.RGB(gray, gray, gray);
    }

    uint8_t threshold = orderedDither4x4[((y & 3) << 2) | (x & 3)];
    return videodisplay.RGB(quantizedChannel(r, 7, threshold),
                            quantizedChannel(g, 8, threshold),
                            quantizedChannel(b, 4, threshold));
}

static uint32_t litSceneColor(int r, int g, int b, float light,
                              int x, int y) {
    light = constrain(light, 0.12f, 1.15f);
    return sceneColor((int)(r * light + 0.5f),
                      (int)(g * light + 0.5f),
                      (int)(b * light + 0.5f), x, y);
}

static uint32_t floorTextureColor(float worldX, float worldY,
                                  float distance, int x, int y) {
    const float tileScale = 1.65f;
    float rowCoordinate = worldY * tileScale;
    int tileRow = (int)floorf(rowCoordinate);
    float rowFraction = rowCoordinate - floorf(rowCoordinate);
    float shiftedX = worldX * tileScale + ((tileRow & 1) ? 0.5f : 0.0f);
    int tileColumn = (int)floorf(shiftedX);
    float columnFraction = shiftedX - floorf(shiftedX);
    bool mortar = rowFraction < 0.075f || columnFraction < 0.065f;

    int r = 74;
    int g = 74;
    int b = 74;
    uint32_t tileNoise = textureHash(tileColumn, tileRow);
    if (mortar) {
        r = g = b = 34 + (int)(tileNoise & 3);
    } else {
        switch ((tileNoise >> 3) & 3) {
            case 0: r = 84; g = 84; b = 80; break;
            case 1: r = 98; g = 79; b = 48; break;
            case 2: r = 67; g = 79; b = 57; break;
            default: r = g = b = 72; break;
        }
        uint32_t grain = textureHash((int)floorf(worldX * 9.0f),
                                     (int)floorf(worldY * 9.0f));
        int variation = (int)(grain & 15) - 7;
        r += variation;
        g += variation;
        b += variation;
    }

    float light = 0.30f + 0.74f / (1.0f + distance * 0.15f);
    return litSceneColor(r, g, b, light, x, y);
}

static void drawSkyAndFloor(float dirX, float dirY, float planeX,
                            float planeY, int horizon) {
    int skyHeight = max(1, horizon - VIEW_TOP);
    for (int y = VIEW_TOP; y < horizon; y += 2) {
        float t = (float)(y - VIEW_TOP) / skyHeight;
        int r = (int)(26 + 108 * t);
        int g = (int)(58 + 70 * t);
        int b = (int)(126 - 42 * t);
        videodisplay.fillRect(0, y, SCREEN_WIDTH, min(2, horizon - y),
                              sceneColor(r, g, b, 0, y));
    }

    float rayLeftX = dirX - planeX;
    float rayLeftY = dirY - planeY;
    float rayRightX = dirX + planeX;
    float rayRightY = dirY + planeY;
    for (int y = horizon; y < VIEW_BOTTOM; y += FLOOR_SAMPLE_Y) {
        int rowOffset = max(1, y - horizon);
        float rowDistance = (VIEW_HEIGHT * 0.5f) / rowOffset;
        rowDistance = min(rowDistance, MAX_DEPTH);
        float worldStepX = rowDistance * (rayRightX - rayLeftX) /
                           (float)SCREEN_WIDTH * FLOOR_SAMPLE_X;
        float worldStepY = rowDistance * (rayRightY - rayLeftY) /
                           (float)SCREEN_WIDTH * FLOOR_SAMPLE_X;
        float worldX = playerX + rowDistance * rayLeftX;
        float worldY = playerY + rowDistance * rayLeftY;
        for (int x = 0; x < SCREEN_WIDTH; x += FLOOR_SAMPLE_X) {
            videodisplay.fillRect(x, y,
                                  min(FLOOR_SAMPLE_X, SCREEN_WIDTH - x),
                                  min(FLOOR_SAMPLE_Y, VIEW_BOTTOM - y),
                                  floorTextureColor(worldX, worldY,
                                                    rowDistance, x, y));
            worldX += worldStepX;
            worldY += worldStepY;
        }
    }
    videodisplay.fillRect(0, horizon, SCREEN_WIDTH, 1,
                          sceneColor(132, 121, 86, 0, horizon));
}

static uint32_t wallTextureColor(const RayHit &hit, float v,
                                 int x, int y) {
    const float bricksAcross = 4.0f;
    const float brickRows = 6.0f;
    float rowCoordinate = v * brickRows;
    int brickRow = (int)floorf(rowCoordinate);
    float rowFraction = rowCoordinate - floorf(rowCoordinate);
    float columnCoordinate = hit.wallU * bricksAcross +
                             ((brickRow & 1) ? 0.5f : 0.0f);
    int brickColumn = (int)floorf(columnCoordinate);
    float columnFraction = columnCoordinate - floorf(columnCoordinate);
    bool mortar = rowFraction < 0.085f || columnFraction < 0.065f;

    int r = 178;
    int g = 104;
    int b = 60;
    uint32_t brickNoise = textureHash(hit.mapX, hit.mapY,
                                      brickColumn, brickRow);
    if (mortar) {
        r = g = b = 72 + (int)(brickNoise & 7);
    } else {
        int variation = (int)((brickNoise >> 5) & 31) - 15;
        r += variation;
        g += variation / 2;
        b += variation / 3;
        if ((brickNoise & 31) == 0) {
            r -= 30;
            g -= 18;
            b -= 8;
        }
    }

    const float lightX = -0.64f;
    const float lightY = -0.77f;
    float directional = 0.82f + 0.18f *
        max(0.0f, hit.normalX * lightX + hit.normalY * lightY);
    float distanceLight = 0.27f + 0.78f /
        (1.0f + hit.distance * 0.14f);
    float verticalLight = 0.91f + 0.09f * (1.0f - v);
    return litSceneColor(r, g, b,
                         directional * distanceLight * verticalLight,
                         x, y);
}

static void drawWallStripe(int x, int stripeWidth, int horizon,
                           const RayHit &hit) {
    int wallHeight = constrain((int)(VIEW_HEIGHT / hit.distance),
                               1, VIEW_HEIGHT * 3);
    int rawTop = horizon - wallHeight / 2;
    int rawBottom = horizon + wallHeight / 2;
    int top = max(VIEW_TOP, rawTop);
    int bottom = min(VIEW_BOTTOM - 1, rawBottom);
    for (int y = top; y <= bottom; y += WALL_SAMPLE_Y) {
        float v = (float)(y + WALL_SAMPLE_Y / 2 - rawTop) /
                  max(1, wallHeight);
        v = constrain(v, 0.0f, 1.0f);
        videodisplay.fillRect(x, y, stripeWidth,
                              min(WALL_SAMPLE_Y, bottom - y + 1),
                              wallTextureColor(hit, v, x, y));
    }
}

static bool intersectCircleObject(const WorldObject &object,
                                  float rayDirX, float rayDirY,
                                  float radius, float maxDistance,
                                  float &distance, float &normalX,
                                  float &normalY, float &textureU,
                                  float &silhouetteX) {
    float offsetX = playerX - object.x;
    float offsetY = playerY - object.y;
    float a = rayDirX * rayDirX + rayDirY * rayDirY;
    float b = 2.0f * (offsetX * rayDirX + offsetY * rayDirY);
    float c = offsetX * offsetX + offsetY * offsetY - radius * radius;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;
    float root = sqrtf(discriminant);
    float nearDistance = (-b - root) / (2.0f * a);
    if (nearDistance <= 0.03f) nearDistance = (-b + root) / (2.0f * a);
    if (nearDistance <= 0.03f || nearDistance >= maxDistance) return false;

    float hitX = playerX + rayDirX * nearDistance;
    float hitY = playerY + rayDirY * nearDistance;
    normalX = (hitX - object.x) / radius;
    normalY = (hitY - object.y) / radius;
    textureU = atan2f(normalY, normalX) / TWO_PI + 0.5f;
    float rayLength = sqrtf(a);
    silhouetteX = ((object.x - playerX) * rayDirY -
                   (object.y - playerY) * rayDirX) /
                  max(0.001f, radius * rayLength);
    silhouetteX = constrain(silhouetteX, -1.0f, 1.0f);
    distance = nearDistance;
    return true;
}

static bool intersectBoxObject(const WorldObject &object,
                               float rayDirX, float rayDirY,
                               float halfSize, float maxDistance,
                               float &distance, float &normalX,
                               float &normalY, float &textureU,
                               float &silhouetteX) {
    float nearDistance = -1.0e8f;
    float farDistance = 1.0e8f;
    float entryNormalX = 0.0f;
    float entryNormalY = 0.0f;

    float minX = object.x - halfSize;
    float maxX = object.x + halfSize;
    if (fabsf(rayDirX) < 0.00001f) {
        if (playerX < minX || playerX > maxX) return false;
    } else {
        float first = (minX - playerX) / rayDirX;
        float second = (maxX - playerX) / rayDirX;
        float firstNormal = -1.0f;
        if (first > second) {
            float swap = first; first = second; second = swap;
            firstNormal = 1.0f;
        }
        if (first > nearDistance) {
            nearDistance = first;
            entryNormalX = firstNormal;
            entryNormalY = 0.0f;
        }
        farDistance = min(farDistance, second);
    }

    float minY = object.y - halfSize;
    float maxY = object.y + halfSize;
    if (fabsf(rayDirY) < 0.00001f) {
        if (playerY < minY || playerY > maxY) return false;
    } else {
        float first = (minY - playerY) / rayDirY;
        float second = (maxY - playerY) / rayDirY;
        float firstNormal = -1.0f;
        if (first > second) {
            float swap = first; first = second; second = swap;
            firstNormal = 1.0f;
        }
        if (first > nearDistance) {
            nearDistance = first;
            entryNormalX = 0.0f;
            entryNormalY = firstNormal;
        }
        farDistance = min(farDistance, second);
    }

    if (farDistance < nearDistance || nearDistance <= 0.03f ||
        nearDistance >= maxDistance) return false;
    float hitX = playerX + rayDirX * nearDistance;
    float hitY = playerY + rayDirY * nearDistance;
    textureU = entryNormalX != 0.0f
        ? (hitY - minY) / (halfSize * 2.0f)
        : (hitX - minX) / (halfSize * 2.0f);
    textureU = constrain(textureU, 0.0f, 1.0f);
    silhouetteX = textureU * 2.0f - 1.0f;
    distance = nearDistance;
    normalX = entryNormalX;
    normalY = entryNormalY;
    return true;
}

static ObjectRayHit castObjectRay(float rayDirX, float rayDirY,
                                  float wallDistance) {
    ObjectRayHit closest = {false, OBJECT_TARGET, -1, wallDistance,
                            0.0f, 0.0f, 0.0f, 0.0f};
    for (int candidate = 0; candidate < visibleObjectCount; ++candidate) {
        int i = visibleObjectIndices[candidate];
        const WorldObject &object = worldObjects[i];
        float distance = wallDistance;
        float normalX = 0.0f;
        float normalY = 0.0f;
        float textureU = 0.0f;
        float silhouetteX = 0.0f;
        bool hit = object.type == OBJECT_CRATE
            ? intersectBoxObject(object, rayDirX, rayDirY,
                                 objectRadius(object.type), closest.distance,
                                 distance, normalX, normalY,
                                 textureU, silhouetteX)
            : intersectCircleObject(object, rayDirX, rayDirY,
                                    objectRadius(object.type), closest.distance,
                                    distance, normalX, normalY,
                                    textureU, silhouetteX);
        if (!hit || distance >= closest.distance) continue;
        closest = {true, object.type, i, distance, normalX, normalY,
                   textureU, silhouetteX};
    }
    return closest;
}

static void prepareVisibleObjects() {
    visibleObjectCount = 0;
    const float halfFov = FOV * 0.5f;
    for (int i = 0; i < MAX_WORLD_OBJECTS; ++i) {
        const WorldObject &object = worldObjects[i];
        if (!object.active) continue;
        float dx = object.x - playerX;
        float dy = object.y - playerY;
        float distance = sqrtf(dx * dx + dy * dy);
        if (distance <= 0.05f || distance >= MAX_DEPTH + objectRadius(object.type)) continue;
        float angularRadius = atan2f(objectRadius(object.type), distance) + 0.04f;
        float angleError = fabsf(normalizeAngle(atan2f(dy, dx) - playerAngle));
        if (angleError > halfFov + angularRadius) continue;
        visibleObjectIndices[visibleObjectCount++] = (uint8_t)i;
    }
}

static bool objectShapeVisible(const ObjectRayHit &hit, float v) {
    float edge = fabsf(hit.silhouetteX);
    if (hit.type == OBJECT_LAMP) {
        if (v < 0.30f) return edge <= 0.96f;
        if (v < 0.84f) return edge <= 0.23f;
        return edge <= 0.68f;
    }
    if (hit.type == OBJECT_TARGET && v < 0.14f) {
        float cap = (0.14f - v) / 0.14f;
        return edge <= sqrtf(max(0.0f, 1.0f - cap * cap));
    }
    if (hit.type == OBJECT_COLUMN) {
        if (v < 0.10f || v > 0.88f) return edge <= 0.98f;
        return edge <= 0.78f;
    }
    return true;
}

static uint32_t objectSurfaceColor(const ObjectRayHit &hit, float v,
                                   int x, int y) {
    const float lightX = -0.64f;
    const float lightY = -0.77f;
    float directional = 0.40f + 0.60f *
        max(0.0f, hit.normalX * lightX + hit.normalY * lightY);
    float distanceLight = 0.35f + 0.72f /
        (1.0f + hit.distance * 0.13f);
    float light = directional * distanceLight;
    int r = 160;
    int g = 160;
    int b = 160;

    if (hit.type == OBJECT_TARGET) {
        bool brightBand = (v > 0.38f && v < 0.54f) ||
                          (hit.textureU > 0.47f && hit.textureU < 0.53f);
        if (brightBand) { r = 238; g = 232; b = 214; }
        else { r = 224; g = 42; b = 24; }
    } else if (hit.type == OBJECT_CRATE) {
        bool seam = hit.textureU < 0.08f || hit.textureU > 0.92f ||
                    fabsf(v - 0.33f) < 0.035f ||
                    fabsf(v - 0.67f) < 0.035f;
        if (seam) { r = 58; g = 43; b = 28; }
        else {
            int grain = (int)(textureHash((int)(hit.textureU * 24.0f),
                                           (int)(v * 24.0f),
                                           hit.objectIndex) & 15) - 7;
            r = 178 + grain;
            g = 103 + grain / 2;
            b = 38;
        }
    } else if (hit.type == OBJECT_LAMP) {
        if (v < 0.30f) {
            r = 255; g = 214; b = 22;
            light = min(1.12f, light + 0.35f);
        } else {
            r = g = b = v > 0.84f ? 70 : 112;
        }
    } else {
        bool flute = ((int)(hit.textureU * 18.0f) & 1) == 0;
        r = flute ? 128 : 92;
        g = flute ? 142 : 108;
        b = flute ? 104 : 78;
        if (v < 0.10f || v > 0.88f) light *= 1.08f;
    }

    if (v > 0.94f) light *= 0.58f;
    return litSceneColor(r, g, b, light, x, y);
}

static void drawObjectStripe(int x, int stripeWidth, int horizon,
                             const ObjectRayHit &hit) {
    if (!hit.hit || hit.distance <= 0.03f) return;
    float projectedUnit = VIEW_HEIGHT / hit.distance;
    int rawBottom = horizon + (int)(projectedUnit * 0.5f);
    int rawTop = rawBottom - (int)(projectedUnit * objectHeight(hit.type));
    int top = max(VIEW_TOP, rawTop);
    int bottom = min(VIEW_BOTTOM - 1, rawBottom);
    int objectPixels = max(1, rawBottom - rawTop);

    for (int y = top; y <= bottom; y += 3) {
        float v = (float)(y + 1 - rawTop) / objectPixels;
        v = constrain(v, 0.0f, 1.0f);
        if (!objectShapeVisible(hit, v)) continue;
        videodisplay.fillRect(x, y, stripeWidth,
                              min(3, bottom - y + 1),
                              objectSurfaceColor(hit, v, x, y));
    }
}

static void drawHud(bool force) {
    static bool baseDrawn = false;
    static int displayedObjectives = -1;
    static int displayedScore = -1;
    static unsigned long lastStatsUpdate = 0;
    uint32_t header = videodisplay.RGB(10, 17, 23);
    uint32_t footer = videodisplay.RGB(12, 24, 28);
    uint32_t accent = videodisplay.RGB(63, 218, 191);
    uint32_t white = videodisplay.RGB(244, 248, 247);
    uint32_t warning = videodisplay.RGB(255, 91, 82);
    if (force || !baseDrawn) {
        videodisplay.fillRect(0, 0, SCREEN_WIDTH, VIEW_TOP, header);
        videodisplay.fillRect(0, VIEW_TOP - 2, SCREEN_WIDTH, 2, accent);
        videodisplay.fillRect(0, VIEW_BOTTOM, SCREEN_WIDTH,
                              SCREEN_HEIGHT - VIEW_BOTTOM, footer);
        videodisplay.fillRect(0, VIEW_BOTTOM, SCREEN_WIDTH, 2, accent);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(white, header);
        videodisplay.setCursor(7, 6);
        videodisplay.print("CITADELA 3D");
        videodisplay.setTextColor(videodisplay.RGB(165, 193, 196), footer);
        videodisplay.setCursor(7, 178);
        videodisplay.print("WASD MOVE  MOUSE LOOK  CLICK/ENTER FIRE  R RESET");
        displayedObjectives = -1;
        displayedScore = -1;
        lastStatsUpdate = 0;
        baseDrawn = true;
    }

    if (force || displayedObjectives != objectivesRemaining ||
        displayedScore != score) {
        videodisplay.fillRect(229, 2, SCREEN_WIDTH - 229, 15, header);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(objectivesRemaining ? warning : accent, header);
        videodisplay.setCursor(235, 6);
        videodisplay.print("TARGETS ");
        videodisplay.print(objectivesRemaining);
        videodisplay.print("  SCORE ");
        videodisplay.print(score);
        displayedObjectives = objectivesRemaining;
        displayedScore = score;
    }

    unsigned long now = millis();
    if (force || now - lastStatsUpdate >= 500) {
        videodisplay.fillRect(312, 173, SCREEN_WIDTH - 312, 15, footer);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(white, footer);
        videodisplay.setCursor(318, 178);
        videodisplay.print(lastFrameMs);
        videodisplay.print("MS");
        lastStatsUpdate = now;
    }

    int cx = SCREEN_WIDTH / 2;
    int cy = VIEW_TOP + VIEW_HEIGHT / 2;
    uint32_t crosshair = shotFlashFrames > 0
        ? videodisplay.RGB(255, 225, 90) : white;
    videodisplay.fillRect(cx - 6, cy - 1, 13, 2, crosshair);
    videodisplay.fillRect(cx - 1, cy - 6, 2, 13, crosshair);
    if (objectivesRemaining == 0) {
        uint32_t panel = videodisplay.RGB(13, 36, 38);
        videodisplay.fillRect(89, 69, 198, 48, panel);
        videodisplay.rect(89, 69, 198, 48, accent);
        videodisplay.setFont(Font8x8);
        videodisplay.setTextColor(accent, panel);
        videodisplay.setCursor(128, 80);
        videodisplay.print("MISSION COMPLETE");
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(white, panel);
        videodisplay.setCursor(141, 100);
        videodisplay.print("PRESS R TO RESTART");
    }
}

void drawFrame(bool forceHud) {
    float dirX = cosf(playerAngle);
    float dirY = sinf(playerAngle);
    float planeScale = tanf(FOV * 0.5f);
    float planeX = -dirY * planeScale;
    float planeY = dirX * planeScale;
    int horizon = constrain((int)(VIEW_TOP + VIEW_HEIGHT * 0.5f + cameraPitch),
                            VIEW_TOP + 22, VIEW_BOTTOM - 22);

    drawSkyAndFloor(dirX, dirY, planeX, planeY, horizon);
    prepareVisibleObjects();

    for (int x = 0; x < SCREEN_WIDTH; x += RENDER_STEP) {
        int stripeWidth = min(RENDER_STEP, SCREEN_WIDTH - x);
        float cameraX = 2.0f * (x + stripeWidth * 0.5f) /
                        (float)SCREEN_WIDTH - 1.0f;
        float rayDirX = dirX + planeX * cameraX;
        float rayDirY = dirY + planeY * cameraX;
        RayHit hit = castRay(rayDirX, rayDirY);
        depthBuffer[x / RENDER_STEP] = hit.distance;
        drawWallStripe(x, stripeWidth, horizon, hit);
        ObjectRayHit objectHit = castObjectRay(rayDirX, rayDirY, hit.distance);
        drawObjectStripe(x, stripeWidth, horizon, objectHit);
    }
    drawHud(forceHud);
    if (shotFlashFrames > 0) {
        --shotFlashFrames;
        sceneDirty = true;
    }
    videodisplay.show();
}

void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
    Serial1.println("res");

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }
    SPI.begin(18, 19, 23, 5);
    if (!SD.begin(5)) Serial.println("SD failed to init");
    else Serial.println("SD ready");

    boolTru();
    boolRes.trim();
    if (boolRes.length() == 0) {
        boolRes = "false";
        boolUpdate();
    }
    if (boolRes == "false") {
        SD.end();
        bool videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
        Serial.printf("Cit3D video init: %s\n", videoReady ? "ready" : "failed");
        if (!videoReady) restartWithFallbackVideo("Cit3D video failed");
        releaseSerialControllerVideo();
        tone(SPEAKER_PIN, 0);
        videodisplay.setFont(Font6x8);
        initializeGameWorld();
        videodisplay.clear(videodisplay.RGB(7, 12, 16));
        sceneDirty = false;
        unsigned long firstFrameStart = millis();
        drawFrame(true);
        lastFrameMs = millis() - firstFrameStart;
    } else if (boolRes == "trueKernel") {
        tone(SPEAKER_PIN, 0);
        boolRes = "false";
        boolUpdate();
        handleKernelFlash();
    }
    lastFrameTime = millis();
}

void loop() {
    unsigned long frameStart = millis();
    unsigned long elapsed = frameStart - lastFrameTime;
    if (elapsed < FRAME_INTERVAL_MS) {
        delay(1);
        return;
    }
    lastFrameTime = frameStart;
    float deltaTime = min(elapsed / 1000.0f, 0.05f);
    controllerSystem(deltaTime);
    if (!sceneDirty) return;
    sceneDirty = false;
    drawFrame(false);
    lastFrameMs = millis() - frameStart;
}

