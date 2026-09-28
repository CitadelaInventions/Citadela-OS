#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static const uint32_t DEPLOY_BAUD = 921600;
static const int SD_CS = 5;
static String commandBuffer = "";

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
    while (length--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return crc;
}

static bool ensureParentDirectories(const String &path) {
    for (int index = 1; index < path.length(); ++index) {
        if (path[index] != '/') continue;
        String directory = path.substring(0, index);
        if (!SD.exists(directory) && !SD.mkdir(directory)) return false;
    }
    return true;
}

static void receiveFile(const String &path, size_t expectedSize,
                        uint32_t expectedCrc) {
    if (!path.startsWith("/") || path.indexOf("..") >= 0 ||
        !ensureParentDirectories(path)) {
        Serial.println("ERR PATH");
        return;
    }

    String temporaryPath = path + ".upload";
    String backupPath = path + ".backup";
    SD.remove(temporaryPath);
    File output = SD.open(temporaryPath, FILE_WRITE);
    if (!output) {
        Serial.println("ERR OPEN");
        return;
    }

    Serial.println("SEND");
    Serial.flush();
    uint8_t buffer[1024];
    size_t received = 0;
    uint32_t crc = 0xFFFFFFFFUL;
    unsigned long lastDataAt = millis();
    while (received < expectedSize) {
        size_t availableBytes = Serial.available();
        if (!availableBytes) {
            if (millis() - lastDataAt > 12000) {
                output.close();
                SD.remove(temporaryPath);
                Serial.println("ERR TIMEOUT");
                return;
            }
            delay(1);
            continue;
        }
        size_t wanted = min(sizeof(buffer), expectedSize - received);
        wanted = min(wanted, availableBytes);
        int count = Serial.read(buffer, wanted);
        if (count <= 0) continue;
        if (output.write(buffer, count) != (size_t)count) {
            output.close();
            SD.remove(temporaryPath);
            Serial.println("ERR WRITE");
            return;
        }
        crc = crc32Update(crc, buffer, count);
        received += count;
        lastDataAt = millis();
    }
    output.flush();
    output.close();
    crc ^= 0xFFFFFFFFUL;
    if (crc != expectedCrc) {
        SD.remove(temporaryPath);
        Serial.printf("ERR CRC %08lX\n", (unsigned long)crc);
        return;
    }

    SD.remove(backupPath);
    bool hadOriginal = SD.exists(path);
    if (hadOriginal && !SD.rename(path, backupPath)) {
        SD.remove(temporaryPath);
        Serial.println("ERR BACKUP");
        return;
    }
    if (!SD.rename(temporaryPath, path)) {
        if (hadOriginal) SD.rename(backupPath, path);
        SD.remove(temporaryPath);
        Serial.println("ERR RENAME");
        return;
    }
    SD.remove(backupPath);
    Serial.printf("OK %lu %08lX %s\n", (unsigned long)received,
                  (unsigned long)crc, path.c_str());
}

static void processCommand(String command) {
    command.trim();
    if (command == "PING") {
        Serial.println("PONG");
        return;
    }
    if (command == "BOOTKERNEL") {
        const esp_partition_t *kernel = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
        if (!kernel || esp_ota_set_boot_partition(kernel) != ESP_OK) {
            Serial.println("ERR BOOT");
            return;
        }
        Serial.println("OK BOOTKERNEL");
        Serial.flush();
        SD.end();
        delay(100);
        ESP.restart();
        return;
    }
    if (!command.startsWith("PUT ")) {
        Serial.println("ERR COMMAND");
        return;
    }

    int firstSpace = command.indexOf(' ', 4);
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) {
        Serial.println("ERR PUT");
        return;
    }
    size_t size = (size_t)strtoul(command.substring(4, firstSpace).c_str(),
                                  nullptr, 10);
    uint32_t crc = strtoul(command.substring(firstSpace + 1, secondSpace).c_str(),
                           nullptr, 16);
    String path = command.substring(secondSpace + 1);
    receiveFile(path, size, crc);
}

void setup() {
    Serial.setRxBufferSize(16384);
    Serial.begin(DEPLOY_BAUD);
    Serial.setTimeout(1000);
    SPI.begin(18, 19, 23, SD_CS);
    if (!SD.begin(SD_CS)) {
        Serial.println("DEPLOYER SD FAILED");
        return;
    }
    Serial.println("DEPLOYER READY");
}

void loop() {
    while (Serial.available()) {
        char character = (char)Serial.read();
        if (character == '\r') continue;
        if (character == '\n') {
            processCommand(commandBuffer);
            commandBuffer = "";
        } else if (commandBuffer.length() < 240) {
            commandBuffer += character;
        } else {
            commandBuffer = "";
            Serial.println("ERR LINE");
        }
    }
    delay(1);
}
