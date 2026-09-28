#pragma once

#include <Arduino.h>
#include <FS.h>
#include <BLEAddress.h>
#include <BLEUUID.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>

namespace Citadela {
namespace BLEHID {

static inline BLEUUID serviceUUID() { return BLEUUID("00001812-0000-1000-8000-00805f9b34fb"); }
static inline BLEUUID inputReportUUID() { return BLEUUID("00002A4D-0000-1000-8000-00805f9b34fb"); }
static inline BLEUUID controlPointUUID() { return BLEUUID((uint16_t)0x2A4C); }
static inline BLEUUID protocolModeUUID() { return BLEUUID((uint16_t)0x2A4E); }
static inline BLEUUID bootKeyboardInputUUID() { return BLEUUID((uint16_t)0x2A22); }
static inline BLEUUID bootMouseInputUUID() { return BLEUUID((uint16_t)0x2A33); }

static inline bool looksLikeKeyboardReport(uint8_t *data, size_t length, size_t offset) {
    if (!data || length < offset + 8) return false;
    if (data[offset + 1] != 0) return false;
    for (size_t i = offset + 2; i < offset + 8; i++) {
        uint8_t keycode = data[i];
        if (keycode != 0 && keycode > 0xE7) return false;
    }
    return true;
}

static inline bool looksLikeMouseButtons(uint8_t buttons) {
    return (buttons & 0xE0) == 0;
}

static inline bool wakeDevice(BLERemoteService *hidService, Print *log = nullptr) {
    if (!hidService) return false;
    bool touched = false;

    BLERemoteCharacteristic *protocolMode = hidService->getCharacteristic(protocolModeUUID());
    if (protocolMode && (protocolMode->canWrite() || protocolMode->canWriteNoResponse())) {
        uint8_t reportMode = 1;
        if (protocolMode->writeValue(&reportMode, 1, protocolMode->canWrite())) {
            touched = true;
            if (log) log->println("HID protocol mode set to report mode");
        }
    }

    BLERemoteCharacteristic *controlPoint = hidService->getCharacteristic(controlPointUUID());
    if (controlPoint && (controlPoint->canWrite() || controlPoint->canWriteNoResponse())) {
        uint8_t exitSuspend = 0;
        if (controlPoint->writeValue(&exitSuspend, 1, controlPoint->canWrite())) {
            touched = true;
            if (log) log->println("HID exit suspend sent");
        }
    }

    return touched;
}

class ReconnectSchedule {
  public:
    void request(uint32_t delayMs, bool resetBackoff = false) {
        needed = true;
        nextAttemptMs = millis() + delayMs;
        if (resetBackoff) backoffMs = minBackoffMs;
    }

    void clear() {
        needed = false;
        nextAttemptMs = 0;
    }

    bool due() const {
        return needed && (long)(millis() - nextAttemptMs) >= 0;
    }

    uint32_t backoffWithJitter(uint32_t jitterMax = 100) const {
        return backoffMs + (jitterMax ? random(0, jitterMax) : 0);
    }

    void growBackoff() {
        backoffMs = min(backoffMs * 2, maxBackoffMs);
    }

    void resetBackoff() {
        backoffMs = minBackoffMs;
    }

    bool needed = false;
    uint32_t nextAttemptMs = 0;
    uint32_t minBackoffMs = 250;
    uint32_t maxBackoffMs = 2000;
    uint32_t backoffMs = 250;
};

class PairingStore {
  public:
    static bool save(fs::FS &fs, const char *path, const String &address, uint8_t addressType) {
        File file = fs.open(path, FILE_WRITE);
        if (!file) return false;
        bool ok = file.println(address) && file.println(addressType);
        file.close();
        return ok;
    }

    static bool load(fs::FS &fs, const char *path, String &address, uint8_t &addressType) {
        File file = fs.open(path, FILE_READ);
        if (!file) return false;

        String fileContent;
        while (file.available()) {
            fileContent += (char)file.read();
        }
        file.close();
        fileContent.trim();
        if (!fileContent.length()) return false;

        addressType = BLE_ADDR_TYPE_PUBLIC;
        int newline = fileContent.indexOf('\n');
        if (newline >= 0) {
            address = fileContent.substring(0, newline);
            address.trim();
            String typeLine = fileContent.substring(newline + 1);
            typeLine.trim();
            if (typeLine.length()) {
                int parsedType = typeLine.toInt();
                if (parsedType >= BLE_ADDR_TYPE_PUBLIC && parsedType <= BLE_ADDR_TYPE_RPA_RANDOM) {
                    addressType = (uint8_t)parsedType;
                }
            }
        } else {
            address = fileContent;
            address.trim();
        }
        return address.length() > 0;
    }

    static void clear(fs::FS &fs, const char *path) {
        fs.remove(path);
    }
};

}  // namespace BLEHID
}  // namespace Citadela
