#include <Wire.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <MD5Builder.h>
#include "esp_partition.h"
#include <WiFi.h>
#include <WebServer.h>
#include "../Libraries/CitadelaBoardPins.h"

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

const int speakerPin = 33;
CompositeGrayDAC videodisplay;
WebServer server(80);

void handleFileUpload() {
    static File file;
    if (server.uri() == "/upload" && server.method() == HTTP_POST) {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            String filename = "/" + upload.filename;
            Serial.print("Uploading file: ");
            Serial.println(filename);
            file = SD.open(filename, FILE_WRITE);
            if (!file) {
                Serial.println("Failed to open file for writing.");
                server.send(500, "text/plain", "Failed to open file for writing.");
                return;
            }
        } 
        else if (upload.status == UPLOAD_FILE_WRITE) {
            if (file) {
                file.write(upload.buf, upload.currentSize);
            }
        } 
        else if (upload.status == UPLOAD_FILE_END) {
            if (file) {
                file.close();
                Serial.println("Upload finished.");
                server.send(200, "text/plain", "File uploaded successfully!");
            } else {
                server.send(500, "text/plain", "Failed to save the file.");
            }
        } 
        else if (upload.status == UPLOAD_FILE_ABORTED) {
            if (file) {
                file.close();
                Serial.println("Upload aborted.");
                server.send(500, "text/plain", "Upload aborted.");
            }
        }
    } else {
        server.send(404, "text/plain", "Not found.");
    }
}

void handleBootloaderFlash() {
    File bootloaderFile = SD.open("/bootloader.bin");
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
void handleRoot() {
    String html = "<!DOCTYPE html><html><body>";
    html += "<h2>Upload File</h2>";
    html += "<form action='/upload' method='post' enctype='multipart/form-data'>";
    html += "<input type='file' name='file'>";
    html += "<input type='submit' value='Upload'>";
    html += "</form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}
void cleanFlashPartitions() {
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
    handleBootloaderFlash();
}
void setup() {
    Serial.begin(115200);
    Citadela::BoardPins::beginSDCardSPI(SPI);
    if (!SD.begin(Citadela::BoardPins::SdChipSelect, SPI)) {
        Serial.println("SD Card initialization failed!");
        return;
    }
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("WiFi connected");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
    server.on("/", HTTP_GET, handleRoot);
    server.on("/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "Uploading file...");
    }, handleFileUpload);
    server.begin();
    Serial.println("Web server started.");
    videodisplay.init(CompMode::MODEPALQuarter144P, 25, true);
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(1, 10);
    videodisplay.print("Wifi Editor");
    videodisplay.circle(85, 10, 15, 255);
    videodisplay.setCursor(1, 30);
    videodisplay.print("SEL to Reload    Bootstrap");
}
void loop() {
  server.handleClient();
}
