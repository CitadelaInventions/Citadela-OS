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

void handleKernelFlash() {
    File kernelFile1 = SD.open("/System/kernel.bin");
    if (!kernelFile1) {
        Serial.println("Kernel file not found!");
        return;
    }

    size_t kernelSize1 = kernelFile1.size();
    Serial.printf("Kernel size: %d bytes\n", kernelSize1);

    if (!Update.begin(kernelSize1)) {
        Serial.printf("Not enough space for the kernel! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), kernelSize1);
        Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
        kernelFile1.close();
        return;
    }


    Serial.printf("Free Sketch Space: %d bytes\n", ESP.getFreeSketchSpace());
    uint8_t buffer1[2048];
    while (kernelFile1.available()) {
        int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
        int written1 = Update.write(buffer1, len1);
        Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n", len1, len1, written1);

        if (written1 != len1) {
            Serial.printf("Write failed! Expected: %d, Wrote: %d\n", len1, written1);
            Update.abort();
            kernelFile1.close();
            return;
        }
    }

    kernelFile1.close();
    if (Update.end()) {
        Serial.println("Kernel written to flash successfully.");
        ESP.restart();
    } else {
        Serial.printf("Update failed: %s\n", Update.errorString());
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

void controller() {
    static String ctrlInput = "";
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            ctrlInput.trim();
            Serial.println(ctrlInput);
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
                ESP.restart();
            } else if (ctrlInput == "Escape"){
                Serial1.println("VDCLoadNSS");
                boolRes = "trueKernel";
                boolUpdate();
                ESP.restart();
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
