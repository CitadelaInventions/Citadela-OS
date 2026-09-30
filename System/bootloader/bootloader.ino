#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <Ressources/Font8x8.h>
// This app uses the standard PAL mode and can build with stock bitluni.
#define CITADELA_DISPLAY_STOCK_BITLUNI
#include "../Libraries/CitadelaDisplay.h"
#include "../Libraries/CitadelaSerialCommands.h"
#include "../Libraries/CitadelaStorage.h"

// This is an OTA application image, despite its historical name. The ESP32
// ROM/second-stage bootloader is a different image at the start of flash.
static constexpr int SCREEN_WIDTH = 376;
static constexpr int SCREEN_HEIGHT = 288;
static constexpr int VIDEO_PIN = 25;
static constexpr const char *BOOT_STATE_PATH = "/evil.txt";

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);
static Citadela::LineReader controllerInput(128);
static Citadela::LineReader usbInput(128);

enum Screen : uint8_t { HOME, BLE_DEVICES, DIAGNOSTICS };
static Screen currentScreen = HOME;
static bool videoReady = false;
static bool spiffsReady = false;
static bool bluetoothConnected = false;
static bool mouseWasDown = false;
static int selectedItem = 0;
static String statusLine;
static String bleDevices[5];

static const char *const MENU_ITEMS[] = {
    "Enter Operating System",
    "WiFi Registry Editor",
    "Rewrite Bootloader",
    "System Info and Diagnostics",
    "Repair System"
};

static bool writeBootState(const char *value) {
    if (!spiffsReady) return false;
    SPIFFS.remove(BOOT_STATE_PATH); // FILE_WRITE appends on this core.
    File file = SPIFFS.open(BOOT_STATE_PATH, FILE_WRITE);
    if (!file) return false;
    bool written = file.println(value) > 0;
    file.close();
    return written;
}

static void clearStaleBootState() {
    if (!spiffsReady) return;
    File file = SPIFFS.open(BOOT_STATE_PATH, FILE_READ);
    String value = file ? file.readStringUntil('\n') : "";
    if (file) file.close();
    value.trim();
    if (value != "false" && !writeBootState("false"))
        Serial.println("Could not clear the boot state.");
}

static uint32_t white() { return videodisplay.RGB(236, 246, 245); }
static uint32_t accent() { return videodisplay.RGB(35, 210, 190); }
static uint32_t background() { return videodisplay.RGB(9, 17, 23); }

static void printAt(int x, int y, const String &message, uint32_t color) {
    videodisplay.setTextColor(color, background());
    videodisplay.setCursor(x, y);
    videodisplay.print(message.c_str());
}

static void drawFrame(const char *title) {
    if (!videoReady) return;
    videodisplay.clear(background());
    videodisplay.fillRect(0, 0, SCREEN_WIDTH, 34, videodisplay.RGB(19, 51, 63));
    videodisplay.setFont(Font8x8);
    printAt(16, 11, title, white());
    videodisplay.setFont(Font6x8);
    printAt(16, 266, "Arrows: select   Enter: open   Tab: BLE   Esc: OS/menu", white());
}

static void drawStatus() {
    if (!videoReady) return;
    videodisplay.fillRect(16, 226, SCREEN_WIDTH - 32, 29, background());
    String shown = statusLine.length() ? statusLine :
        (bluetoothConnected ? "Keyboard connected" : "Keyboard not connected");
    printAt(18, 235, shown.substring(0, 55), accent());
}

static void drawMenu() {
    if (!videoReady) return;
    drawFrame("Citadela Bootloader");
    for (int i = 0; i < 5; ++i) {
        int y = 52 + i * 32;
        bool selected = i == selectedItem;
        uint32_t fill = selected ? accent() : videodisplay.RGB(20, 37, 45);
        videodisplay.fillRect(16, y, SCREEN_WIDTH - 32, 27, fill);
        videodisplay.rect(16, y, SCREEN_WIDTH - 32, 27, selected ? white() : accent());
        videodisplay.setTextColor(selected ? background() : white(), fill);
        videodisplay.setCursor(25, y + 9);
        videodisplay.print(MENU_ITEMS[i]);
    }
    drawStatus();
}

static void drawBleDevices() {
    if (!videoReady) return;
    drawFrame("Bluetooth Device Connector");
    printAt(18, 42, "Choose a keyboard discovered by the controller", white());
    for (int i = 0; i < 5; ++i) {
        int y = 68 + i * 30;
        uint32_t fill = i == selectedItem ? accent() : videodisplay.RGB(20, 37, 45);
        videodisplay.fillRect(16, y, SCREEN_WIDTH - 32, 25, fill);
        videodisplay.setTextColor(i == selectedItem ? background() : white(), fill);
        videodisplay.setCursor(24, y + 8);
        String label = bleDevices[i].length() ? bleDevices[i] : String("Device ") + (i + 1) + " - scanning";
        videodisplay.print(label.substring(0, 54).c_str());
    }
    drawStatus();
}

static void drawDiagnostics() {
    if (!videoReady) return;
    drawFrame("System Diagnostics");
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    printAt(18, 55, String("Free heap: ") + ESP.getFreeHeap() + " bytes", white());
    printAt(18, 75, String("Flash: ") + ESP.getFlashChipSize() + " bytes", white());
    printAt(18, 95, String("Chip revision: ") + ESP.getChipRevision(), white());
    printAt(18, 115, String("SDK: ") + ESP.getSdkVersion(), white());
    printAt(18, 135, String("Running OTA: ") + (running ? running->label : "unknown"), white());
    printAt(18, 155, String("Next OTA: ") + (next ? next->label : "unavailable"), white());
    printAt(18, 175, String("SPIFFS: ") + (spiffsReady ? "ready" : "unavailable"), white());
    printAt(18, 195, "Images load from /System on the SD card", accent());
    drawStatus();
}

static void drawCurrentScreen() {
    if (currentScreen == HOME) drawMenu();
    else if (currentScreen == BLE_DEVICES) drawBleDevices();
    else drawDiagnostics();
}

static bool startLocalVideo() {
    videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, VIDEO_PIN, true);
    if (videoReady) {
        controllerVideo.appVideoActive(8, 35);
        drawCurrentScreen();
    } else {
        Serial.println("Local video allocation failed; use USB serial controls.");
    }
    return videoReady;
}

static bool mountSD() {
    const uint32_t frequencies[] = {4000000, 2000000, 1000000, 400000};
    return Citadela::Storage::beginBoardSDWithRetry(
        SD, SPI, frequencies, sizeof(frequencies) / sizeof(frequencies[0]),
        nullptr, &Serial);
}

static void flashFailed(const char *reason) {
    Update.abort();
    SD.end();
    statusLine = reason;
    Serial.println(reason);
    controllerVideo.forceProgress(0, reason);
    Serial1.println("VDSTOP");
    Serial1.flush();
    currentScreen = HOME;
    startLocalVideo();
    drawCurrentScreen();
}

static void flashImage(const char *path, const char *label) {
    if (!writeBootState("false"))
        Serial.println("Boot marker unavailable; continuing with OTA recovery.");
    controllerVideo.prepare(label, 0);
    if (videoReady) {
        videodisplay.releaseVideoMemory();
        videoReady = false;
        pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    }

    if (!mountSD()) {
        flashFailed("SD card mount failed");
        return;
    }
    File image = SD.open(path, FILE_READ);
    if (!image) {
        flashFailed("System image not found on SD");
        return;
    }
    const size_t imageSize = image.size();
    int magic = image.read();
    if (imageSize < 32768 || magic != 0xE9 || !image.seek(0)) {
        image.close();
        flashFailed("Invalid ESP32 app image");
        return;
    }

    controllerVideo.appFlashStart(imageSize, label);
    if (!Update.begin(imageSize, U_FLASH)) {
        Serial.printf("OTA begin failed: %s\n", Update.errorString());
        image.close();
        flashFailed("OTA partition unavailable");
        return;
    }

    uint8_t buffer[2048];
    size_t flashed = 0;
    bool complete = true;
    while (flashed < imageSize) {
        int count = image.read(buffer, min(sizeof(buffer), imageSize - flashed));
        if (count <= 0 || Update.write(buffer, count) != (size_t)count) {
            complete = false;
            break;
        }
        flashed += count;
        controllerVideo.appFlashWrite(flashed);
        yield();
    }
    image.close();
    if (!complete) {
        Serial.printf("OTA write stopped at %u/%u: %s\n",
                      (unsigned)flashed, (unsigned)imageSize, Update.errorString());
        flashFailed("OTA write failed");
        return;
    }
    if (!Update.end(true)) {
        Serial.printf("OTA validation failed: %s\n", Update.errorString());
        flashFailed("Image validation failed");
        return;
    }

    SD.end();
    controllerVideo.forceProgress(100, "Restarting");
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
}

static void selectCurrentItem() {
    if (currentScreen == BLE_DEVICES) {
        Serial1.printf("BLE0%d\n", selectedItem + 1);
        statusLine = String("Connecting to device ") + (selectedItem + 1);
        drawBleDevices();
        return;
    }
    if (currentScreen == DIAGNOSTICS) {
        currentScreen = HOME;
        drawMenu();
        return;
    }
    switch (selectedItem) {
        case 0: flashImage("/System/kernel.bin", "Starting Citadela OS"); break;
        case 1: flashImage("/System/WifiEditor.bin", "Starting WiFi editor"); break;
        case 2: flashImage("/System/bootloader.bin", "Updating bootloader app"); break;
        case 3: currentScreen = DIAGNOSTICS; drawDiagnostics(); break;
        case 4:
            // Reinstalling a validated kernel is safer than erasing arbitrary
            // data partitions (including OTA state or persistent settings).
            flashImage("/System/kernel.bin", "Repairing Citadela OS");
            break;
    }
}

static void handleMouse(const String &line) {
    int x = 0, y = 0, buttons = 0;
    if (sscanf(line.c_str(), "MOUSE %d %d %d", &x, &y, &buttons) != 3) return;
    bool down = (buttons & 1) != 0;
    if (currentScreen == HOME && x >= 16 && x < SCREEN_WIDTH - 16 && y >= 52 && y < 207) {
        int item = (y - 52) / 32;
        if (item >= 0 && item < 5 && y < 52 + item * 32 + 27) {
            if (item != selectedItem) { selectedItem = item; drawMenu(); }
            if (down && !mouseWasDown) selectCurrentItem();
        }
    } else if (currentScreen == BLE_DEVICES && x >= 16 && x < SCREEN_WIDTH - 16 && y >= 68 && y < 213) {
        int item = (y - 68) / 30;
        if (item >= 0 && item < 5 && y < 68 + item * 30 + 25) {
            if (item != selectedItem) { selectedItem = item; drawBleDevices(); }
            if (down && !mouseWasDown) selectCurrentItem();
        }
    }
    mouseWasDown = down;
}

static void handleInput(String line) {
    line.trim();
    if (!line.length() || line == "rlsd" || line.startsWith("VDM ")) return;
    if (line == "BLE1X" || line == "BLE0X") {
        bluetoothConnected = line == "BLE1X";
        statusLine = "";
        drawCurrentScreen();
        return;
    }
    if (line.startsWith("dev") && line.length() >= 4 && line.charAt(3) >= '1' && line.charAt(3) <= '5') {
        int index = line.charAt(3) - '1';
        bleDevices[index] = line.substring(4);
        bleDevices[index].trim();
        if (currentScreen == BLE_DEVICES) drawBleDevices();
        return;
    }
    if (line.startsWith("MOUSE ")) { handleMouse(line); return; }
    if (line == "RightGUI (Win) +") {
        controllerVideo.prepare("Restarting bootloader", 0);
        if (videoReady) videodisplay.releaseVideoMemory();
        pinMode(VIDEO_PIN, INPUT_PULLDOWN);
        Serial1.println("VDINIT");
        Serial1.flush();
        ESP.restart();
        return;
    }
    if (line == "Tab" || line == "c" || line == "C") {
        currentScreen = currentScreen == BLE_DEVICES ? HOME : BLE_DEVICES;
        selectedItem = 0;
        if (currentScreen == BLE_DEVICES) Serial1.println("BLE00");
        drawCurrentScreen();
        return;
    }
    if (line == "Escape") {
        if (currentScreen == HOME) flashImage("/System/kernel.bin", "Starting Citadela OS");
        else { currentScreen = HOME; selectedItem = 0; drawMenu(); }
        return;
    }
    if (line == "UpArrow" || line == "DownArrow") {
        int count = currentScreen == DIAGNOSTICS ? 0 : 5;
        if (count) {
            selectedItem = (selectedItem + (line == "UpArrow" ? count - 1 : 1)) % count;
            drawCurrentScreen();
        }
        return;
    }
    if (line == "Enter") { selectCurrentItem(); return; }
    if (currentScreen == HOME && line.length() == 1 && line[0] >= '1' && line[0] <= '5') {
        selectedItem = line[0] - '1';
        selectCurrentItem();
    }
}

void setup() {
    Serial.setRxBufferSize(512);
    Serial1.setRxBufferSize(256);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
    controllerVideo.start("Starting bootloader");
    spiffsReady = SPIFFS.begin(false); // Recovery must not format settings.
    clearStaleBootState();
    controllerVideo.progress(45, "Preparing display");
    startLocalVideo();
    Serial.println("Citadela bootloader ready: 1 kernel, 2 WiFi editor, 3 self-update, 4 diagnostics, 5 repair.");
}

void loop() {
    String line;
    while (controllerInput.poll(Serial1, line)) handleInput(line);
    while (usbInput.poll(Serial, line)) handleInput(line);
    delay(2);
}
