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
#define TFT_WIDTH  300
#define TFT_HEIGHT 200

CompositeColorDAC videodisplay;
File root;

volatile int imageCount = 0;
volatile int currentImage = 0;

int keepStable = 0;

String imageFiles[20];
String boolRes = "";
String appName = "";
String appString = "";

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

void listImages() {
    videodisplay.fillRect(0, 0, 255 + 45, 190, 255);
    Serial.println("Listing all files on SD card:");
    root = SD.open("/Images/");
    imageCount = 0;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        String fileName = entry.name();
        Serial.println(fileName);
        if (fileName.endsWith(".bmp") || fileName.endsWith(".BMP")) {
            if (imageCount < 20) {
                imageFiles[imageCount] = fileName;
                Serial.println("Added BMP: " + fileName);
                imageCount++;
            }
        }
        entry.close();
    }
    Serial.print("Total BMP images found: ");
    Serial.println(imageCount);
}

void drawBMP(String filename) {
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

void deleteCurrentImage() {
    if (imageCount == 0) return;
    Serial.println("Delete this image? (Y/N)");
    videodisplay.fillRect(255+55,30,80,50,50);
    videodisplay.setCursor(255+55,32);
    videodisplay.setTextColor(0,50);
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
void serialTask(void *parameter) {
    for (;;) {
        if (Serial1.available()) {
            String command = Serial1.readStringUntil('\n');
            command.trim();
            Serial.println(command);
            if (command == "LeftArrow") {
                currentImage = (currentImage - 1 + imageCount) % imageCount;
            } else if (command == "RightArrow") {
                currentImage = (currentImage + 1) % imageCount;
            } else if (command == "d") {
                deleteCurrentImage();
            } else if (command == "Escape"){
                Serial1.println("VDCLoadNSS");
                boolRes = "trueKernel";
                boolUpdate();
                ESP.restart();
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
      videodisplay.init(CompMode::MODEPAL576Idiv3, 25, false);
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
      handleKernelFlash();
    } else {
      tone(SPEAKER_PIN,0);
      boolRes = false;
      boolUpdate();
    }
}

void loop() {
  if (imageCount > 0) {
    drawBMP(imageFiles[currentImage]);
  }
  delay(100);
}
