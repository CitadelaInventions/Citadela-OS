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
      Serial1.println("VDCLoadNSS");
      boolRes = "trueKernel";
      boolUpdate();
      ESP.restart();
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
    while (Serial1.available()) {
        char c = Serial1.read();
        processInput(c);
    }
}
