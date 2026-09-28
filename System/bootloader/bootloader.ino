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
#include "../Libraries/CitadelaBoardPins.h"

String dev1 = "";
String dev2 = "";
String dev3 = "";
String dev4 = "";
String dev5 = "";

String boolRes = "";

bool coreUsed = false;
bool BLElaunched = false;
bool bluetoothConnection = false;
bool rk = true;
bool we = false;
bool rbl = false;
bool siad = false;
bool cfc = false;
bool shouldFlashKernel = false;
bool selection = false;
bool previousBluetoothConnection = false;
int dragvalve = 1;

static int previousDragValve = -1;

CompositeColorDAC videodisplay;

const char* FLAG_FILE = "/flag.txt";

void boolUpdate() {
    File file = SPIFFS.open("/boolres.txt", FILE_WRITE);
    file.print(boolRes);
    file.close();
}

void boolTru() {
    File file = SPIFFS.open("/boolres.txt", FILE_READ);
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
}

void displaySystemInfo() {
    videodisplay.clear(0);
    videodisplay.setCursor(75, 70);
    videodisplay.print("System Diagnostics");
    videodisplay.setCursor(75, 90);
    videodisplay.print(ESP.getFreeHeap());
    videodisplay.setCursor(75, 110);
    videodisplay.print(ESP.getChipRevision());
    videodisplay.setCursor(75, 130);
    videodisplay.print(ESP.getFlashChipSize());
    videodisplay.setCursor(75, 150);
    videodisplay.print(ESP.getSdkVersion());
}


void updateFlag(const char* value) {
    File flagFile = SD.open(FLAG_FILE, FILE_WRITE);
    if (flagFile) {
        flagFile.seek(0);
        flagFile.print("");
        flagFile.println(value);
        flagFile.close();
    } else {
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
void handleExceptions(){
    Serial.println("Running initializationConstruct.");
        if (SD.exists(FLAG_FILE)) {
            File flagFile = SD.open(FLAG_FILE, FILE_READ);
            if (flagFile) {
                String flag = flagFile.readStringUntil('\n');
                flag.trim();
                if (flag == "true") {
                    updateFlag("false");
                    shouldFlashKernel = true;
                } else {
                    updateFlag("true");
                    shouldFlashKernel = true;
                }
                flagFile.close();
              } else {
                Serial.println("Flag file not found, creating one and setting to false.");
                updateFlag("false");
              }
            }
        if (shouldFlashKernel){
          handleKernelFlash();
          shouldFlashKernel = false;
        } 
}
void handleWifiFlash() {
    File editorFile = SD.open("/System/WifiEditor.bin");
    if (!editorFile) {
        Serial.println("Editor file not found!");
        return;
    }

    size_t editorSize = editorFile.size();
    Serial.printf("Editor size: %d bytes\n", editorSize);

    if (!Update.begin(editorSize)) {
        Serial.printf("Not enough space for the editor! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), editorSize);
        Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
        editorFile.close();
        return;
    }
    Serial.printf("Free Sketch Space: %d bytes\n", ESP.getFreeSketchSpace());

    uint8_t buffer[2048];
    while (editorFile.available()) {
        int len = editorFile.read(buffer, sizeof(buffer));
        int written = Update.write(buffer, len);
        Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n", len, len, written);

        if (written != len) {
            Serial.printf("Write failed! Expected: %d, Wrote: %d\n", len, written);
            Update.abort();
            editorFile.close();
            return;
        }
    }

    editorFile.close();
    if (Update.end()) {
        Serial.println("Editor written to flash successfully.");
        ESP.restart();
    } else {
        Serial.printf("Update failed: %s\n", Update.errorString());
    }
}
void handleBootloaderFlash() {
    File bootloaderFile = SD.open("/System/bootloader.bin");
    if (!bootloaderFile) {
        Serial.println("Bootloader file not found!");
        return;
    }

    size_t bootloaderSize = bootloaderFile.size();
    Serial.printf("Bootloader size: %d bytes\n", bootloaderSize);

    if (!Update.begin(bootloaderSize)) {
        Serial.printf("Not enough space for the bootloader! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), bootloaderSize);
        Serial.printf("Error: Bootloader update failed: %s\n", Update.errorString());
        bootloaderFile.close();
        return;
    }

    uint8_t buffer[2048];
    while (bootloaderFile.available()) {
        int len = bootloaderFile.read(buffer, sizeof(buffer));
        int written = Update.write(buffer, len);

        Serial.printf("Read %d bytes, Wrote %d bytes\n", len, written);

        if (written != len) {
            Serial.println("Write failed! Aborting update.");
            Update.abort();
            bootloaderFile.close();
            return;
        }
    }

    if (Update.end(true)) {
        Serial.println("Bootloader updated successfully. Restarting...");
        ESP.restart();
    } else {
        Serial.printf("Bootloader update failed: %s\n", Update.errorString());
    }
}
void setup() {
    Serial.begin(115200);
    Serial1.begin(115200,SERIAL_8N1, 16, 17);
    if (!SPIFFS.begin(true)) {
        return;
    }
    boolTru();
    if (boolRes == ""){
      boolRes = "false";
      boolUpdate();
    }
    if (boolRes == "false"){
      videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
      home();
    } else if (boolRes == "trueKernel"){
      boolRes = "false";
      boolUpdate();
      Citadela::BoardPins::beginSDCardSPI(SPI);
      SD.begin(Citadela::BoardPins::SdChipSelect, SPI);
      handleKernelFlash();
    } else if (boolRes == "trueBootloader"){
      boolRes = "false";
      boolUpdate();
      Citadela::BoardPins::beginSDCardSPI(SPI);
      SD.begin(Citadela::BoardPins::SdChipSelect, SPI);
      handleBootloaderFlash();
    } else if (boolRes == "trueWifiFlash"){
      boolRes = "false";
      boolUpdate();
      Citadela::BoardPins::beginSDCardSPI(SPI);
      SD.begin(Citadela::BoardPins::SdChipSelect, SPI);
      handleWifiFlash();
    }
    Serial.println(ESP.getFreeHeap());
}
void clearFlashChip(){
    const esp_partition_t* partition;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);

    int partitionIndex = 0;

    while (it != NULL) {
        partition = esp_partition_get(it);

        if (partitionIndex % 2 == 1) {
            Serial.printf("Erasing partition: %s, Address: 0x%08x, Size: %d bytes\n",
                          partition->label, partition->address, partition->size);

            esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
            if (err != ESP_OK) {
                Serial.printf("Failed to erase partition %s: %s\n", partition->label, esp_err_to_name(err));
            } else {
                Serial.printf("Partition %s erased successfully.\n", partition->label);
            }
        }
        partitionIndex++;
        it = esp_partition_next(it);
    }

    esp_partition_iterator_release(it);
}

void startup(){
    videodisplay.clear();
    videodisplay.setFont(Font8x8);
    videodisplay.circle(255 ,150 , 40, videodisplay.RGB(255, 255, 255));
    videodisplay.fillCircle(180 ,150 , 20, videodisplay.RGB(255, 255, 255));
    videodisplay.fillCircle(255 ,70 , 20, videodisplay.RGB(255, 255, 255));
    videodisplay.fillCircle(255 ,230 , 20, videodisplay.RGB(255, 255, 255));
    videodisplay.fillCircle(90 ,150 , 20, videodisplay.RGB(255, 255, 255));
    
    videodisplay.setCursor(220, 150);
    videodisplay.print("LOADING CITADELA FS");
}

void loading(){
  videodisplay.setCursor(10, 255);
  videodisplay.println("- Loading -");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
  videodisplay.setCursor(10, 255);
  videodisplay.println("| Loading |");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
  videodisplay.setCursor(10, 255);
  videodisplay.println("- Loading -");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
  videodisplay.clear();
  videodisplay.setFont(Font8x8);
  videodisplay.circle(255 ,150 , 40, videodisplay.RGB(255, 255, 255));
  videodisplay.fillCircle(180 ,150 , 20, videodisplay.RGB(255, 255, 255));
  videodisplay.fillCircle(255 ,80 , 20, videodisplay.RGB(255, 255, 255));
  videodisplay.fillCircle(255 ,230 , 20, videodisplay.RGB(255, 255, 255));
  videodisplay.fillCircle(90 ,150 , 20, videodisplay.RGB(255, 255, 255));
  videodisplay.setCursor(220, 150);
  videodisplay.print("LOADING CITADELA FS");
}

void simpleLoad(){
  videodisplay.setCursor(10, 255);
  videodisplay.println("- Loading -");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
  videodisplay.setCursor(10, 255);
  videodisplay.println("| Loading |");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
  videodisplay.setCursor(10, 255);
  videodisplay.println("- Loading -");
  delay(500);
  videodisplay.fillRect(10, 255, 90, 50, 0);
}

void processSelection() {
  if (BLElaunched){
      if (selection) {
        selection = false;
        coreUsed = true;
        switch (dragvalve) {
          case 1: Serial.println("BLE01");simpleLoad();delay(1000);videodisplay.clear();home();BLElaunched = false;coreUsed = false; break;
          case 2: Serial.println("BLE02");simpleLoad();delay(1000);videodisplay.clear();home();BLElaunched = false;coreUsed = false; break;
          case 3: Serial.println("BLE03");simpleLoad();delay(1000);videodisplay.clear();home();BLElaunched = false;coreUsed = false; break;
          case 4: Serial.println("BLE04");simpleLoad();delay(1000);videodisplay.clear();home();BLElaunched = false;coreUsed = false; break;
          case 5: Serial.println("BLE05");simpleLoad();delay(1000);videodisplay.clear();home();BLElaunched = false;coreUsed = false; break;
        }
    }
  } else if (!BLElaunched){
      if (selection) {
        switch (dragvalve) {
          case 1: selection = false;coreUsed = true;loading();startup();boolRes = "trueKernel";boolUpdate();ESP.restart(); break;
          case 2: selection = false;coreUsed = true;loading();startup();boolRes = "trueWifiFlash";boolUpdate();ESP.restart(); break;
          case 3: selection = false;coreUsed = true;loading();startup();boolRes = "trueBootloader";boolUpdate();ESP.restart(); break;
          case 4: selection = false;displaySystemInfo();delay(5000);videodisplay.clear();home(); break;
          case 5: selection = false;coreUsed = true;loading();startup();clearFlashChip();ESP.restart(); break;
        }
      }
    }
  }

void updateDisplay() {
  if (dragvalve != previousDragValve) {
    videodisplay.fillRect(73, 90, 5, 120, 0);
    videodisplay.fillRect(235+82, 90, 5, 120, 0);
    int yOffset = 90 + (dragvalve - 1) * 20;
    videodisplay.fillRect(73, yOffset, 5, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(235 + 82, yOffset, 5, 10, videodisplay.RGB(255, 255, 255));
    previousDragValve = dragvalve;
  }
  if (bluetoothConnection != previousBluetoothConnection){
    videodisplay.setFont(Font6x8);
    videodisplay.fillRect(82,202,100,27, videodisplay.RGB(0,0,0));
    previousBluetoothConnection = bluetoothConnection;
    videodisplay.setCursor(82, 202);
    if (bluetoothConnection){
      videodisplay.print("Connection Established");
      videodisplay.setCursor(82, 210);
      videodisplay.print("Press C on the side of your machine");
      videodisplay.setCursor(82, 218);
      videodisplay.print("to browse connections.");
    } else if (!bluetoothConnection){
      videodisplay.print("Connection Unavailable");
      videodisplay.setCursor(82, 210);
      videodisplay.print("Press C on the side of your machine");
      videodisplay.setCursor(82, 218);
      videodisplay.print("to browse connections.");
    }
  } else {videodisplay.setFont(Font8x8);}
  
}

void home(){
    dragvalve = 1;
    updateDisplay();
    videodisplay.setFont(Font8x8);
    videodisplay.rect(70, 60, 255, 170, videodisplay.RGB(255, 255, 255));
    videodisplay.rect(70, 80, 255, 150, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(70, 60, 255, 20, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(120, 65);
    videodisplay.print("Citadela Bootloader");
    videodisplay.fillRect(80, 90, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(82, 90);
    videodisplay.print("Enter Operating System");
    videodisplay.fillRect(80, 110, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(82, 110);
    videodisplay.print("WiFi Registry Editor");
    videodisplay.fillRect(80, 130, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(82, 130);
    videodisplay.print("Rewrite Bootloader");
    videodisplay.fillRect(80, 150, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(82, 150);
    videodisplay.print("System Info and Diagnostics");
    videodisplay.fillRect(80, 170, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(82, 170);
    videodisplay.print("Repair System");
    videodisplay.setCursor(82, 202);
    videodisplay.setFont(Font6x8);
    if (bluetoothConnection){
      videodisplay.print("Connection Established");
      videodisplay.setCursor(82, 210);
      videodisplay.print("Press C on the side of your machine");
      videodisplay.setCursor(82, 218);
      videodisplay.print("to browse connections.");
    } else if (!bluetoothConnection){
      videodisplay.print("Connection Unavailable");
      videodisplay.setCursor(82, 210);
      videodisplay.print("Press C on the side of your machine");
      videodisplay.setCursor(82, 218);
      videodisplay.print("to browse connections.");
    }
    keyboardMenu();
}

void controller() {
    static String ctrlInput = "";
    static String ctrlInput1 = "";

    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            ctrlInput1.trim();
            Serial1.println(ctrlInput1);
            if (ctrlInput1 == "UpArrow") {
                if (dragvalve > 1) dragvalve--;
            } else if (ctrlInput1 == "DownArrow") {
                if (dragvalve < 5) dragvalve++;
            } else if (ctrlInput1 == "Enter") {
                selection = true;
            } else if (ctrlInput1 == "BLE1X"){
                bluetoothConnection = true;
            } else if (ctrlInput1 == "BLE0X"){
                bluetoothConnection = false;
            } else if (!BLElaunched & ctrlInput1 == "c"){
                dragvalve = 1;
                communicationConBLE();
            } else if (BLElaunched & ctrlInput1 == "Escape"){
                BLElaunched = false;
                videodisplay.clear();
                previousDragValve = -1;
                home();
            } else if (ctrlInput1 == "RightGUI (Win) +"){
                ESP.restart();
            } else if (BLElaunched & !coreUsed){
                if (ctrlInput1.startsWith("dev1")){
                  dev1 = ctrlInput1;
                  videodisplay.setCursor(82, 90);
                  videodisplay.print(dev1.c_str());
                    } else if (ctrlInput1.startsWith("dev2")){
                        dev2 = ctrlInput1;
                        videodisplay.setCursor(82, 110);
                        videodisplay.print(dev2.c_str());
                      } else if (ctrlInput1.startsWith("dev3")){
                          dev3 = ctrlInput1;
                          videodisplay.setCursor(82, 130);
                          videodisplay.print(dev3.c_str());
                          } else if (ctrlInput1.startsWith("dev4")){
                            dev4 = ctrlInput1;
                            videodisplay.setCursor(82, 150);
                            videodisplay.print(dev4.c_str());
                              } else if (ctrlInput1.startsWith("dev5")){
                                dev5 = ctrlInput1;
                                videodisplay.setCursor(82, 170);
                                videodisplay.print(dev5.c_str());
                                  }
            } else if (ctrlInput1 == "Tab"){
              boolRes = "false";
              boolUpdate();
            }
            ctrlInput1 = "";
        } else {
            ctrlInput1 += c;
        }
    }
}
void updateBTElist(){
}

void keyboardMenu(){
  
  videodisplay.rect(80, 190, 235,40, videodisplay.RGB(255,255,255));
  videodisplay.rect(80, 200, 235,30, videodisplay.RGB(255,255,255));
  videodisplay.setCursor(82, 192);
  videodisplay.setFont(Font6x8);
  videodisplay.print("             BLE Keyboard"); 
}

void communicationConBLE(){
    BLElaunched = true;
    Serial1.println("BLE00");
    videodisplay.clear();
    previousDragValve = -1;
    updateDisplay();
    videodisplay.rect(70, 60, 255, 170, videodisplay.RGB(255, 255, 255));
    videodisplay.rect(70, 80, 255, 150, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(70, 60, 255, 20, videodisplay.RGB(255, 255, 255));
    videodisplay.setCursor(100, 65);
    videodisplay.print("Bluetooth Device Connector");
    videodisplay.fillRect(80, 90, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(80, 110, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(80, 130, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(80, 150, 235, 10, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(80, 170, 235, 10, videodisplay.RGB(255, 255, 255));
}

void loop() {  
  if(!coreUsed){controller();updateDisplay();}
  previousDragValve = dragvalve;
  previousBluetoothConnection = bluetoothConnection;
  processSelection();
  if (shouldFlashKernel) {
    Serial.println("Bool Staged");
    handleKernelFlash();
    shouldFlashKernel = false;
  }
}
