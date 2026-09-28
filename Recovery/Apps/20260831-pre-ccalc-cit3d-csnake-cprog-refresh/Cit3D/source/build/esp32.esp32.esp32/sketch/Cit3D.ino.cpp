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

// --- Definitions ---
#define SPEAKER_PIN 12
#define SCREEN_WIDTH 400
#define SCREEN_HEIGHT 160
#define MAP_WIDTH 8
#define MAP_HEIGHT 8
#define FOV (3.14159 / 2.0) // 90° field-of-view
#define MAX_DEPTH 16.0
#define MAX_BULLETS 10

// --- Global Objects and Variables ---
CompositeColorDAC videodisplay;

#line 27 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void restartWithFallbackVideo(const char *label);
#line 44 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void releaseSerialControllerVideo();
#line 135 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 146 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 151 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 283 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
static bool handleCit3DMouseReport(String input);
#line 437 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void setup();
#line 484 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
void loop();
#line 27 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\Cit3D\\Cit3D.ino"
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

// --- Bullet Struct and Global Array ---
struct Bullet {
    float x, y;   // Current position
    float dx, dy; // Velocity (direction * speed)
    bool active;  // Whether this bullet is active
};

Bullet bullets[MAX_BULLETS];

// Player and view variables
float playerX = 4.0, playerY = 4.0;
float playerAngle = 0.0;
float viewOffset = 0.0; // For vertical tilting

// Global timing variable
unsigned long lastFrameTime = 0;

// --- Floor Texture (8x8 grayscale pattern) ---
uint8_t floorTexture[8][8] = {
    {0,  255, 0,  255, 0,  255, 0,  255},
    {255, 0,  255, 0,  255, 0,  255, 0},
    {0,  255, 0,  255, 0,  255, 0,  255},
    {255, 0,  255, 0,  255, 0,  255, 0},
    {0,  255, 0,  255, 0,  255, 0,  255},
    {255, 0,  255, 0,  255, 0,  255, 0},
    {0,  255, 0,  255, 0,  255, 0,  255},
    {255, 0,  255, 0,  255, 0,  255, 0}
};

// --- World Map Definition ---
char worldMap[MAP_WIDTH * MAP_HEIGHT] = {
    '1','1','1','1','1','1','1','1',
    '1','0','0','0','0','0','0','1',
    '1','0','1','0','1','1','0','1',
    '1','0','1','0','0','1','0','1',
    '1','0','0','0','0','0','0','1',
    '1','0','1','1','1','1','0','1',
    '1','0','0','0','0','0','0','1',
    '1','1','1','1','1','1','1','1'
};

// --- Function Prototypes ---
void boolUpdate();
void boolTru();
void handleKernelFlash();
void initBullets();
void fireGun();
void updateBullets(float deltaTime);
void drawBullets();
void controllerSystem(float deltaTime);
void drawFrame();

// --- Function Implementations ---

// Updates the content of the bool file
void boolUpdate() {
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (file) {
        file.seek(0);
        file.print("");
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

// Initializes the bullet array
void initBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = false;
    }
}

// Fires a bullet by finding an inactive bullet and setting its initial position and velocity
void fireGun() {
    Serial.println("Gun fired!");
    tone(SPEAKER_PIN, 2000, 100);  // Play a gunshot sound

    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].active = true;
            bullets[i].x = playerX;
            bullets[i].y = playerY;
            float bulletSpeed = 5.0; // Adjust bullet speed as needed
            bullets[i].dx = cos(playerAngle) * bulletSpeed;
            bullets[i].dy = sin(playerAngle) * bulletSpeed;
            break; // Fire one bullet per "Enter" press
        }
    }
}

// Updates all active bullets and deactivates them on collision or when out-of-bounds
void updateBullets(float deltaTime) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            bullets[i].x += bullets[i].dx * deltaTime;
            bullets[i].y += bullets[i].dy * deltaTime;

            int testX = (int)bullets[i].x;
            int testY = (int)bullets[i].y;

            // Deactivate if out of bounds
            if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                bullets[i].active = false;
            }
            // Deactivate if bullet hits a wall
            else if (worldMap[testY * MAP_WIDTH + testX] == '1') {
                Serial.printf("Bullet %d hit wall at (%d, %d)\n", i, testX, testY);
                bullets[i].active = false;
                // Optionally add an explosion effect here.
            }
        }
    }
}

// Draws all active bullets on the display
void drawBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) { 
            int screenX = (int)(bullets[i].x * (SCREEN_WIDTH / (float)MAP_WIDTH));
            int screenY = (int)(bullets[i].y * (SCREEN_HEIGHT / (float)MAP_HEIGHT));
            videodisplay.dot(screenX, screenY, 255);  // Draw bullet as a bright dot
        }
    }
}

// Processes input commands and moves the player or fires bullets
// --- at the top, after your globals ---
String currentAction = "";   // the key we're holding ("" = none)
static bool cit3DMouseWasLeftDown = false;

static bool handleCit3DMouseReport(String input) {
    input.trim();
    if (!input.startsWith("MOUSE")) return false;

    int x = 0;
    int y = 0;
    int buttons = 0;
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d", &x, &y, &buttons, &dx, &dy, &wheel);
    if (parsed >= 3) {
        playerAngle += (float)dx * 0.008f;
        viewOffset += (float)dy * 0.45f;
        viewOffset = constrain(viewOffset, -90.0f, 90.0f);

        bool leftDown = (buttons & 0x01) != 0;
        if (leftDown && !cit3DMouseWasLeftDown) {
            fireGun();
        }
        cit3DMouseWasLeftDown = leftDown;
    }
    return true;
}

// --- replace your controllerSystem() with this version ---
void controllerSystem(float deltaTime) {
    // 1) read any pending serial commands
    while (Serial1.available()) {
        String cmd = Serial1.readStringUntil('\n');
        cmd.trim();
        if (handleCit3DMouseReport(cmd)) continue;
        // release any held key
        if (cmd == "rlsd") {
            currentAction = "";
        }
        // immediate actions
        else if (cmd == "Enter") {
            fireGun();
        }
        else if (cmd == "Escape") {
            boolRes = "trueKernel";
            boolUpdate();
            restartWithFallbackVideo("Returning home");
        }
        // start holding this action
        else {
            currentAction = cmd;
        }
    }

    // 2) apply the held action each frame
    float moveSpeed = 2.0 * deltaTime;
    float rotSpeed  = 1.5 * deltaTime;

    if (currentAction == "w") {
        playerX += cos(playerAngle) * moveSpeed;
        playerY += sin(playerAngle) * moveSpeed;
    }
    else if (currentAction == "s") {
        playerX -= cos(playerAngle) * moveSpeed;
        playerY -= sin(playerAngle) * moveSpeed;
    }
    else if (currentAction == "a") {
        playerX += sin(playerAngle) * moveSpeed;
        playerY -= cos(playerAngle) * moveSpeed;
    }
    else if (currentAction == "d") {
        playerX -= sin(playerAngle) * moveSpeed;
        playerY += cos(playerAngle) * moveSpeed;
    }
    else if (currentAction == "LeftArrow") {
        playerAngle -= rotSpeed;
    }
    else if (currentAction == "RightArrow") {
        playerAngle += rotSpeed;
    }
    else if (currentAction == "UpArrow") {
        viewOffset -= 5;
    }
    else if (currentAction == "DownArrow") {
        viewOffset += 5;
    }
    // if currentAction == "" (or something else), we do nothing
}

void drawFrame() {
    int floorBoundary[SCREEN_WIDTH];

    // First pass: Draw sky and walls (without texture)
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        float rayAngle = (playerAngle - FOV / 2.0) + (((float)x / SCREEN_WIDTH) * FOV);
        float rayX = cos(rayAngle);
        float rayY = sin(rayAngle);

        float distanceToWall = 0.0;
        bool hitWall = false;

        while (!hitWall && distanceToWall < MAX_DEPTH) {
            distanceToWall += 0.1;
            int testX = (int)(playerX + rayX * distanceToWall);
            int testY = (int)(playerY + rayY * distanceToWall);

            if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                hitWall = true;
                distanceToWall = MAX_DEPTH;
            } else if (worldMap[testY * MAP_WIDTH + testX] == '1') {
                hitWall = true;
            }
        }
        int ceiling = (SCREEN_HEIGHT / 2.0) - (SCREEN_HEIGHT / distanceToWall) + viewOffset;
        int floorPos = SCREEN_HEIGHT - ceiling;
        floorBoundary[x] = floorPos;

        uint8_t shade = 255 - (distanceToWall / MAX_DEPTH) * 255;
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            if (y < ceiling) {
                videodisplay.dot(x, y, 0);
            } else if (y > floorPos) {
            } else {
                videodisplay.dot(x, y, shade);
            }
        }
    }
    float rayDirX0 = cos(playerAngle - FOV / 2.0);
    float rayDirY0 = sin(playerAngle - FOV / 2.0);
    float rayDirX1 = cos(playerAngle + FOV / 2.0);
    float rayDirY1 = sin(playerAngle + FOV / 2.0);

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        for (int y = floorBoundary[x]; y < SCREEN_HEIGHT; y++) {
            float p = (float)y - (SCREEN_HEIGHT / 2.0);
            if (p <= 0) continue;

            float posZ = 0.5 * SCREEN_HEIGHT;
            float rowDistance = posZ / p;
            float cameraX = (2.0 * x / SCREEN_WIDTH) - 1.0;
            float floorRayX = rayDirX0 + cameraX * (rayDirX1 - rayDirX0);
            float floorRayY = rayDirY0 + cameraX * (rayDirY1 - rayDirY0);

            float floorX = playerX + rowDistance * floorRayX;
            float floorY = playerY + rowDistance * floorRayY;

            int texWidth = 8, texHeight = 8;
            int floorTexX = ((int)(floorX * texWidth)) & (texWidth - 1);
            int floorTexY = ((int)(floorY * texHeight)) & (texHeight - 1);
            uint8_t texColor = floorTexture[floorTexY][floorTexX];

            videodisplay.dot(x, y, texColor);
        }
    }
}

// --- Arduino Setup and Loop Functions ---
void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial1.println("res");

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }

    SPI.begin(18, 19, 23, 5);
    if (!SD.begin(5)) {
        Serial.println("SD Failed to init");
    } else {
        Serial.println("EUREKA");
    }

    boolTru();
    if (boolRes == "") {
        boolRes = "false";
        boolUpdate();
    }
    boolRes.trim();

    if (boolRes == "false") {
        videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
        releaseSerialControllerVideo();
        tone(SPEAKER_PIN, 0);
        initBullets();
        videodisplay.setFont(Font8x8);
        videodisplay.setCursor(10, 162);
        videodisplay.println("3 Bullets Left");
        videodisplay.println("Shoot 3 objectives");
        videodisplay.setCursor(280, 162);
        videodisplay.setFont(CodePage437_9x16);
        videodisplay.println("Citadela 3D");
    } else if (boolRes == "trueKernel") {
        tone(SPEAKER_PIN, 0);
        boolRes = "false";
        boolUpdate();
        handleKernelFlash();
    }
    lastFrameTime = millis();
}

void loop() {
    unsigned long currentTime = millis();
    float deltaTime = (currentTime - lastFrameTime) / 1000.0;
    lastFrameTime = currentTime;

    controllerSystem(deltaTime);
    updateBullets(deltaTime);
    drawFrame();
    drawBullets();
}

