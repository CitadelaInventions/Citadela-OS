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
#define MAX_WORLD_OBJECTS 14
#define FRAME_INTERVAL_MS 40

// --- Global Objects and Variables ---
CompositeColorDAC videodisplay;

#line 31 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void restartWithFallbackVideo(const char *label);
#line 48 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void releaseSerialControllerVideo();
#line 186 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 197 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 202 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 269 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool mapIsWall(int x, int y);
#line 274 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static float normalizeAngle(float angle);
#line 280 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool canStand(float x, float y);
#line 288 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void tryMove(float dx, float dy);
#line 349 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool hasLineOfSight(float targetX, float targetY);
#line 399 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool handleCit3DMouseReport(String input);
#line 418 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void handleCit3DCommand(String command);
#line 443 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void pollCit3DPort(HardwareSerial &port, String &buffer);
#line 617 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void drawHud(bool force);
#line 692 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void drawFrame(bool forceHud);
#line 748 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void setup();
#line 792 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void loop();
#line 31 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
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
};

struct ProjectedObject {
    WorldObjectType type;
    float depth;
    int centerX;
    int spriteWidth;
    int left;
    int right;
    int top;
    int bottom;
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
static ProjectedObject projectedObjects[MAX_WORLD_OBJECTS];

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
static RayHit castRay(float rayDirX, float rayDirY);
static uint32_t wallColor(const RayHit &hit, int extraDark);
static uint32_t objectColor(WorldObjectType type, int stripe, int width);
static int projectWorldObjects(float dirX, float dirY, float planeX,
                               float planeY, int horizon);
static void drawProjectedObjectsForStripe(int x, int stripeWidth,
                                          float wallDepth, int objectCount);

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

static float normalizeAngle(float angle) {
    while (angle > PI) angle -= TWO_PI;
    while (angle < -PI) angle += TWO_PI;
    return angle;
}

static bool canStand(float x, float y) {
    const float radius = 0.18f;
    return !mapIsWall((int)(x - radius), (int)(y - radius)) &&
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
    return {distance, mapX, mapY, side};
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

static uint32_t wallColor(const RayHit &hit, int extraDark) {
    int band = 0;
    if (hit.distance >= 2.0f) band = 1;
    if (hit.distance >= 3.5f) band = 2;
    if (hit.distance >= 5.5f) band = 3;
    if (hit.distance >= 8.0f) band = 4;
    if (hit.distance >= 12.0f) band = 5;
    if (hit.distance >= 17.0f) band = 6;
    if (hit.side) ++band;
    band = constrain(band + extraDark, 0, 6);

    // These are exact centres of separate 4x-palette cells. Every distance
    // band therefore remains visibly darker after indexed colour conversion.
    static const uint8_t wallPalette[7][3] = {
        {0, 182, 255},
        {0, 182, 170},
        {0, 146, 170},
        {0, 109, 170},
        {0, 109, 85},
        {0, 73, 85},
        {0, 36, 85}
    };
    return videodisplay.RGB(wallPalette[band][0],
                            wallPalette[band][1],
                            wallPalette[band][2]);
}

static uint32_t objectColor(WorldObjectType type, int stripe, int width) {
    switch (type) {
        case OBJECT_TARGET:
            if (abs(stripe - width / 2) < max(1, width / 9))
                return videodisplay.RGB(245, 244, 236);
            return videodisplay.RGB(235, 48, 76);
        case OBJECT_CRATE:
            return ((stripe / 3) & 1) ? videodisplay.RGB(190, 106, 38)
                                      : videodisplay.RGB(232, 153, 52);
        case OBJECT_LAMP:
            return abs(stripe - width / 2) < max(1, width / 4)
                ? videodisplay.RGB(255, 232, 92)
                : videodisplay.RGB(55, 191, 169);
        default:
            return ((stripe / 2) & 1) ? videodisplay.RGB(41, 128, 151)
                                      : videodisplay.RGB(72, 190, 187);
    }
}

static int projectWorldObjects(float dirX, float dirY, float planeX,
                               float planeY, int horizon) {
    int count = 0;
    float determinant = planeX * dirY - dirX * planeY;
    if (fabsf(determinant) < 0.0001f) return 0;
    float inverse = 1.0f / determinant;

    for (int i = 0; i < MAX_WORLD_OBJECTS; ++i) {
        if (!worldObjects[i].active) continue;
        WorldObject &object = worldObjects[i];
        float relX = object.x - playerX;
        float relY = object.y - playerY;
        float transformX = inverse * (dirY * relX - dirX * relY);
        float transformY = inverse * (-planeY * relX + planeX * relY);
        if (transformY <= 0.12f || transformY >= MAX_DEPTH) continue;

        float scale = object.type == OBJECT_TARGET ? 0.82f
                    : object.type == OBJECT_COLUMN ? 0.95f
                    : object.type == OBJECT_LAMP ? 0.68f : 0.52f;
        int spriteHeight = constrain((int)(VIEW_HEIGHT * scale / transformY),
                                     3, VIEW_HEIGHT * 2);
        float widthScale = object.type == OBJECT_CRATE ? 0.72f
                         : object.type == OBJECT_TARGET ? 0.46f : 0.34f;
        int spriteWidth = max(3, (int)(spriteHeight * widthScale));
        int centerX = (int)((SCREEN_WIDTH * 0.5f) *
                            (1.0f + transformX / transformY));
        projectedObjects[count] = {
            object.type,
            transformY,
            centerX,
            spriteWidth,
            centerX - spriteWidth / 2,
            centerX + spriteWidth / 2,
            horizon - spriteHeight / 2,
            horizon + spriteHeight / 2
        };
        ++count;
    }

    // Far-to-near ordering lets each stripe finish in its final visible state.
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (projectedObjects[i].depth < projectedObjects[j].depth) {
                ProjectedObject temporary = projectedObjects[i];
                projectedObjects[i] = projectedObjects[j];
                projectedObjects[j] = temporary;
            }
        }
    }
    return count;
}

static void drawProjectedObjectsForStripe(int x, int stripeWidth,
                                          float wallDepth, int objectCount) {
    for (int n = 0; n < objectCount; ++n) {
        ProjectedObject &object = projectedObjects[n];
        if (x > object.right || x + stripeWidth - 1 < object.left ||
            object.depth >= wallDepth) continue;

        int sampleX = constrain(x + stripeWidth / 2, object.left, object.right);
        int localStripe = sampleX - object.left;
        int drawTop = max(VIEW_TOP, object.top);
        int drawBottom = min(VIEW_BOTTOM - 1, object.bottom);
        if (object.type == OBJECT_TARGET) {
            int spriteHeight = object.bottom - object.top;
            float edge = fabsf((sampleX - object.centerX) /
                               max(1.0f, object.spriteWidth * 0.5f));
            int inset = (int)(edge * edge * spriteHeight * 0.17f);
            drawTop += inset;
            drawBottom -= inset;
        }
        if (drawBottom >= drawTop) {
            videodisplay.fillRect(x, drawTop, stripeWidth,
                                  drawBottom - drawTop + 1,
                                  objectColor(object.type, localStripe,
                                              object.spriteWidth));
        }
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

    uint32_t sky = videodisplay.RGB(0, 109, 170);
    uint32_t distantSky = videodisplay.RGB(0, 146, 170);
    uint32_t floor = videodisplay.RGB(0, 36, 85);
    uint32_t floorLine = videodisplay.RGB(0, 73, 85);
    int objectCount = projectWorldObjects(dirX, dirY, planeX, planeY, horizon);

    for (int x = 0; x < SCREEN_WIDTH; x += RENDER_STEP) {
        int stripeWidth = min(RENDER_STEP, SCREEN_WIDTH - x);
        videodisplay.fillRect(x, VIEW_TOP, stripeWidth,
                              horizon - VIEW_TOP, sky);
        int distantTop = max(VIEW_TOP, horizon - 10);
        if (horizon > distantTop) {
            videodisplay.fillRect(x, distantTop, stripeWidth,
                                  horizon - distantTop, distantSky);
        }
        videodisplay.fillRect(x, horizon, stripeWidth,
                              VIEW_BOTTOM - horizon, floor);
        for (int y = horizon + 8; y < VIEW_BOTTOM; y += 13) {
            videodisplay.fillRect(x, y, stripeWidth,
                                  min(2, VIEW_BOTTOM - y), floorLine);
        }

        float cameraX = 2.0f * x / (float)SCREEN_WIDTH - 1.0f;
        float rayDirX = dirX + planeX * cameraX;
        float rayDirY = dirY + planeY * cameraX;
        RayHit hit = castRay(rayDirX, rayDirY);
        depthBuffer[x / RENDER_STEP] = hit.distance;
        int wallHeight = constrain((int)(VIEW_HEIGHT / hit.distance), 1, VIEW_HEIGHT * 3);
        int top = max(VIEW_TOP, horizon - wallHeight / 2);
        int bottom = min(VIEW_BOTTOM - 1, horizon + wallHeight / 2);
        videodisplay.fillRect(x, top, stripeWidth, bottom - top + 1,
                              wallColor(hit, 0));
        if (((hit.mapX + hit.mapY) & 1) == 0 && bottom - top > 12) {
            videodisplay.fillRect(x, top + (bottom - top) / 2,
                                  stripeWidth, 2,
                                  wallColor(hit, 1));
        }
        drawProjectedObjectsForStripe(x, stripeWidth, hit.distance, objectCount);
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
        drawFrame(true);
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

