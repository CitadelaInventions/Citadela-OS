#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
// Full revised sketch with non-blocking serial output queue and improved auto-reconnect
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "FS.h"
#include "SPIFFS.h"
#include <ESP32Video.h>
#include <driver/dac.h>
#include <time.h>
#include "CitadelaSplashLogo.h"
#include "../Libraries/CitadelaBLEHID.h"
#include "../Libraries/CitadelaBoardPins.h"
#include "../Libraries/CitadelaRTC.h"
#include "../Libraries/CitadelaFanControl.h"
#include "../Libraries/CitadelaSerialCommands.h"
#include "../Libraries/CitadelaDisplayRelay.h"

static Citadela::DisplayRelay displayRelay;
static Citadela::DS1302RTC controllerRTC(
    Citadela::BoardPins::RtcReset,
    Citadela::BoardPins::RtcData,
    Citadela::BoardPins::RtcClock);
static Citadela::FanControl controllerFan(Citadela::BoardPins::FanControl);

#ifndef ENABLE_NETWORK_FEATURES
#define ENABLE_NETWORK_FEATURES 1
#endif

#if ENABLE_NETWORK_FEATURES
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#endif

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <vector>
#include <deque>
#include <map>

int devices = 0;

String lastAddress = "";
String boolRes = "";

static BLEClient* pClient = nullptr;
static BLEScan* pBLEScan = nullptr;

BLERemoteCharacteristic* pTrackpadChar = nullptr;
std::vector<BLEAdvertisedDevice> deviceList;
std::vector<String> deviceNames;
BLERemoteService* pHIDService = nullptr;
BLERemoteCharacteristic* pInputReportChar = nullptr;
std::vector<BLERemoteCharacteristic*> inputReportChars;

static const BLEUUID HID_SERVICE_UUID("00001812-0000-1000-8000-00805f9b34fb");
static const BLEUUID HID_INPUT_CHAR_UUID("00002A4D-0000-1000-8000-00805f9b34fb");
static const BLEUUID HID_CONTROL_POINT_CHAR_UUID((uint16_t)0x2A4C);
static const BLEUUID HID_PROTOCOL_MODE_CHAR_UUID((uint16_t)0x2A4E);
static const BLEUUID HID_BOOT_KEYBOARD_INPUT_CHAR_UUID((uint16_t)0x2A22);
static const BLEUUID HID_BOOT_MOUSE_INPUT_CHAR_UUID((uint16_t)0x2A33);

static const int16_t HID_MOUSE_SCREEN_W = 376;
static const int16_t HID_MOUSE_SCREEN_H = 285;
static int16_t hidMouseX = HID_MOUSE_SCREEN_W / 2;
static int16_t hidMouseY = HID_MOUSE_SCREEN_H / 2;
static uint8_t hidMouseButtons = 0;
static int8_t hidMouseInputReportId = -1;

bool silence = false;
bool loading = false;
bool inputNotifyRegistered = false;


#if ENABLE_NETWORK_FEATURES
String savedSSID = "";
String savedPASS = "";
bool wifiConnected = false;
bool restartAfterNetworkTransaction = false;
String HF_SPACE_HOST = "";
String HF_TOKEN = "";
const char *DEFAULT_HF_MODEL = "Qwen/Qwen3-4B-Instruct-2507:cheapest";
String HF_MODEL = DEFAULT_HF_MODEL;
const char *HF_TOKEN_PATH = "/hftoken.txt";
const char *HF_MODEL_PATH = "/hfmodel.txt";
const char *HF_SPACE_PATH = "/hfspace.txt";
const char *WIFI_SSID_PATH = "/wifi_ssid.txt";
const char *WIFI_PASS_PATH = "/wifi_pass.txt";
size_t JSON_BUFFER_SIZE = 32 * 1024;
String getCurrentTimeString(const char *fmt);
#endif
static bool capsLock = false;

volatile bool blePausing = false;
volatile bool bleNeedReconnect = false;
volatile bool bleManualConnectInProgress = false;
volatile bool hidReady = false;
volatile unsigned long bleReconnectSuppressedUntil = 0;
unsigned long lastBtConnectAttempt = 0;
unsigned long nextReconnectAttempt = 0;
unsigned long reconnectBackoff = 250UL;
unsigned long lastBLEStackRecycleMs = 0;
uint8_t reconnectFailures = 0;
const unsigned long RECONNECT_MIN_BACKOFF = 250UL;
const unsigned long RECONNECT_MAX_BACKOFF = 2000UL;
const uint32_t BLE_SCAN_SECONDS = 2;
const uint32_t BLE_SCAN_CACHE_MS = 15000;
const uint32_t BLE_CONNECT_TIMEOUT_MS = 10000;
const uint32_t BLE_RECONNECT_TIMEOUT_MS = 3500;
const uint32_t BLE_RECONNECT_SCAN_SECONDS = 2;
const uint32_t BLE_STACK_RECYCLE_COOLDOWN_MS = 8000;
const size_t SERIAL_COMMAND_MAX = 192;
const bool USB_MOUSE_DEBUG = false;
uint8_t lastAddressType = BLE_ADDR_TYPE_PUBLIC;

bool keyboardAuthService();
void initBLEClient();
void scanDevices(bool force = false, bool preferSavedAddress = true);
bool connectToAddress(BLEAddress address, uint8_t addressType = 0xFF);
void onKeyPress(BLERemoteCharacteristic* pCharacteristic, uint8_t* data, size_t length, bool isNotify);
static void hidReportNotifyCallback(BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify);
static bool handleMouseReport(BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify);
static bool waitForLateBLEConnection(uint32_t graceMs, const char *context);
static void hardResetBLEClient(const char *reason);
static bool recycleBLEStack(const char *reason);
static bool scanForSavedBLEDevice(BLEAdvertisedDevice &matchedDevice);
static bool handleVideoProgressCommand(const String &command, bool fromSerial1);
static bool handleCVBSFailoverCommand(const String &command, bool fromSerial1);
static bool cvbsEnsureHardwareVideo(bool allowBLEPause = false);
static void setCVBSFailoverDriving(bool driving);
static void cvbsRenderBootSplash(int percent, const String &label);
static void cvbsUpdateBootSplash(int percent, const String &label);
static void cvbsRenderFetchingFiles(bool fullRedraw);
static void serviceCVBSFetchAnimation();
static void serviceCVBSFailover();
static void stopBLERuntimeForVideo();
static void serviceBluetoothRuntime();

#line 149 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cancelBLEReconnect();
#line 166 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void ensureReconnectScheduledIfIdle();
#line 325 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void enqueuePeerLine(const String &s);
#line 336 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void enqueuePeerMouseTransition(const String &s);
#line 355 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendKernelVideoAck(const char *ack);
#line 364 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void enqueuePeerMultiLine(const String &text);
#line 382 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendPeerLineImmediate(const String &s);
#line 389 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendPeerMultiLineImmediate(const String &text);
#line 406 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void flushPeerImmediate();
#line 413 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void flushPeerQueue();
#line 445 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void fastCommand(const String &s);
#line 451 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendHFOutput(const String &text);
#line 454 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendHFShort(const String &text);
#line 458 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void enqueueBLEWork(const String &work, bool fromSerial1);
#line 470 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool dequeueBLEWork(String &work, bool &fromSerial1);
#line 491 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String loadFile(const char *p);
#line 501 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void saveFile(const char *p, const String &v);
#line 512 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool parseFanPercent(String valueText, uint8_t &percent);
#line 524 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void loadFanControl();
#line 534 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void sendHardwareReply(const String &reply, bool fromSerial1);
#line 539 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool handleFanControlCommand(const String &command, bool fromSerial1);
#line 580 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool clockLeapYear(int y);
#line 584 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static int clockDaysInMonth(int y, int m);
#line 591 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool parseDateTimePayload(const String &payload, int &year, int &month, int &day, int &hour, int &minute, int &second);
#line 613 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint32_t dateTimeToEpochUTC(int year, int month, int day, int hour, int minute, int second);
#line 621 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint8_t weekdayFromEpoch(uint32_t epoch);
#line 625 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static Citadela::DS1302RTC::DateTime rtcDateTimeFromEpoch(uint32_t epoch);
#line 640 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool readHardwareClockEpoch(uint32_t &epoch);
#line 652 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool writeHardwareClockEpoch(uint32_t epoch);
#line 664 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint32_t currentManualClockEpochUnlocked(unsigned long now);
#line 669 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint32_t currentManualClockEpoch();
#line 681 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void saveManualClockEpoch(uint32_t epoch);
#line 693 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void setManualClockEpoch(uint32_t epoch, bool persistNow);
#line 708 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void loadManualClock();
#line 733 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void initializeHardwareClock();
#line 757 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void serviceHardwareClock();
#line 783 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static String formatEpochForCBTIME(uint32_t epoch);
#line 795 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static String controllerTimeString();
#line 805 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool handleControllerTimeCommand(const String &command, bool fromSerial1);
#line 880 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void serviceManualClockPersistence();
#line 912 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String sanitizeHost(const String &h);
#line 921 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String extractHostFromEndpoint(const String &endpoint);
#line 983 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void startWiFiAuto();
#line 1001 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void pollWiFiStatus();
#line 1012 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void boolTru();
#line 1022 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void boolUpdate();
#line 1034 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void saveAddress();
#line 1042 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void readAddress();
#line 1054 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void clearSavedBLEDevice(bool fromSerial1);
#line 1073 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void clearBLEHandles();
#line 1082 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void suppressAutoReconnect(uint32_t durationMs);
#line 1087 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void unregisterHIDNotifications();
#line 1100 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void disconnectCurrentBLE(bool suppressReconnect);
#line 1237 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void emitCachedDeviceList();
#line 1245 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void scanDevices(bool force, bool preferSavedAddress);
#line 1296 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void setBulgariaTZ();
#line 1339 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String getCurrentTimeISO();
#line 1343 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void dumpRemoteServices(const char *reason);
#line 1362 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool looksLikeKeyboardReport(uint8_t* data, size_t length, size_t offset);
#line 1366 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static int16_t clampMouseCoord(int32_t value, int16_t limit);
#line 1372 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool looksLikeMouseButtons(uint8_t buttons);
#line 1376 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool applyRelativeMouseReport(uint8_t* data, size_t length, size_t offset, int8_t reportId);
#line 1455 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool isSubscribedHIDReport(BLERemoteCharacteristic *characteristic);
#line 1464 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool subscribeHIDReportCharacteristic(BLERemoteCharacteristic *characteristic, const char *label);
#line 1488 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void wakeHIDDevice();
#line 1737 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
bool connectToAddress(BLEAddress address, uint8_t addressType);
#line 1819 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void connectToDevice(int index);
#line 1883 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void attemptReconnectIfNeeded();
#line 2005 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
bool saveHttpResponseToFile(HTTPClient &http, const char *filepath);
#line 2048 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String extractJsonStringValueFromFile(const char *filepath, const char *key);
#line 2114 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String extractTextFromFile(const char *filepath, bool useSpace, const String &endpoint);
#line 2212 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
String extractTextFromJson(const String &body, bool useSpace, const String &endpoint);
#line 2292 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
int freeAskHF(const String &prompt, String &outExtracted);
#line 2519 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void sendStatusOverSerial1();
#line 2529 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void handleCBCommand(const String &cmdRaw, bool fromSerial1);
#line 2756 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void handleSerialCommand(String command, bool fromSerial1);
#line 2841 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void pumpSerialLine(Stream &port, Citadela::LineReader &reader, bool fromSerial1);
#line 2848 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void checkSerialCommand();
#line 2861 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void loadHFConfig();
#line 2875 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void pauseBLEForTLS();
#line 2907 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void resumeBLEAfterTLS();
#line 2913 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void processBLEWork(const String &work, bool fromSerial1);
#line 2945 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsEmitStatus(bool fromSerial1);
#line 2961 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsListenMode();
#line 2967 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsPrintHeap(const char *stage);
#line 2976 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool cvbsEnsureHardwareVideo(bool allowBLEPause);
#line 3072 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool cvbsDriveMode();
#line 3079 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static String cvbsFitBootLabel(const String &label);
#line 3087 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint16_t cvbsLogoDrawHeight();
#line 3094 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static uint16_t cvbsTinyGlyph(char ch);
#line 3144 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static int cvbsTinyTextWidth(const String &text);
#line 3149 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawTinyText(const String &text, int x, int y, uint8_t color);
#line 3169 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsUpdateSplashLayout();
#line 3192 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawCenteredText(const String &text, int y, uint8_t color);
#line 3201 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawSplashLogo();
#line 3221 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawSplashPercent(int percent);
#line 3230 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsRenderBootSplashFull(int percent, const String &text);
#line 3283 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawFetchImageFile(int x, int y, uint8_t color);
#line 3292 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsDrawFetchScene(uint8_t frame);
#line 3371 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsPrepareBootSplash(int percent, const String &label);
#line 3383 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool sampleKernelCVBSSignal();
#line 3709 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cvbsFailoverTask(void *param);
#line 3717 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void clockTask(void *param);
#line 3726 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void bluetoothTask(void *param);
#line 3742 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool bleCanRunNow();
#line 3751 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void resetBLERuntimeHandles();
#line 3781 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static bool startBLERuntimeIfAllowed();
#line 3823 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void startControllerTasks();
#line 3840 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void setup();
#line 3900 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
void loop();
#line 149 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\System\\SerialController\\SerialController.ino"
static void cancelBLEReconnect() {
  bleNeedReconnect = false;
  nextReconnectAttempt = 0;
}

static void scheduleBLEReconnect(uint32_t delayMs, bool resetBackoff = false) {
  if (!lastAddress.length()) {
    cancelBLEReconnect();
    return;
  }
  unsigned long now = millis();
  bleNeedReconnect = true;
  nextReconnectAttempt = now + delayMs;
  lastBtConnectAttempt = now;
  if (resetBackoff) reconnectBackoff = RECONNECT_MIN_BACKOFF;
}

static void ensureReconnectScheduledIfIdle() {
  if (bleNeedReconnect || blePausing || bleManualConnectInProgress || !lastAddress.length()) return;
  if (pClient && pClient->isConnected()) {
    if (hidReady) return;
    Serial.println("BLE link connected without HID notifications; retrying HID setup");
    if (keyboardAuthService()) {
      cancelBLEReconnect();
      bleReconnectSuppressedUntil = 0;
      reconnectBackoff = RECONNECT_MIN_BACKOFF;
      reconnectFailures = 0;
      enqueuePeerLine("BL1X");
      return;
    }
    hardResetBLEClient("connected without HID");
    scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
    return;
  }
  unsigned long now = millis();
  if (now < bleReconnectSuppressedUntil) return;
  scheduleBLEReconnect(0);
}

static void configureBLESecurity(bool force = false) {
  static bool configured = false;
  static BLESecurity *security = nullptr;
  if (!security) security = new BLESecurity();
  if (configured && !force) return;
  BLESecurity::setCapability(ESP_IO_CAP_OUT);
  BLESecurity::setAuthenticationMode(true, false, false);
  BLESecurity::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  configured = true;
}

class MyClientCallbacks : public BLEClientCallbacks {
public:
  void onConnect(BLEClient* pclient) override {
    Serial.println("BLE Client Connected");
  }
  void onDisconnect(BLEClient* pclient) override {
    Serial.println("BLE Client Disconnected");
    inputNotifyRegistered = false;
    hidReady = false;
    inputReportChars.clear();
    hidMouseButtons = 0;
    hidMouseInputReportId = -1;

    // clear references (they may be invalid after disconnect)
    pHIDService = nullptr;
    pInputReportChar = nullptr;
    pTrackpadChar = nullptr;

    unsigned long now = millis();
    if (blePausing || bleManualConnectInProgress) {
      cancelBLEReconnect();
      return;
    }

    reconnectFailures = 0;
    if (!lastAddress.length()) {
      cancelBLEReconnect();
      return;
    }

    uint32_t delayMs = 50;
    if (now < bleReconnectSuppressedUntil) {
      delayMs = (uint32_t)(bleReconnectSuppressedUntil - now + 25);
    }
    Serial.printf("BLE reconnect scheduled in %lu ms\n", (unsigned long)delayMs);
    scheduleBLEReconnect(delayMs, true);
  }
};

static MyClientCallbacks myClientCallbacks;

// ----- Non-blocking serial output queue -----
std::deque<String> peerQueue;
unsigned long lastPeerSend = 0;
const unsigned long PEER_SEND_INTERVAL_MS = 5;
const uint8_t PEER_LINES_PER_FLUSH = 4;
const size_t PEER_QUEUE_MAX_LINES = 96;

std::deque<String> bleCommandQueue;
const size_t BLE_COMMAND_QUEUE_MAX = 8;

SemaphoreHandle_t peerQueueMutex = nullptr;
SemaphoreHandle_t bleCommandMutex = nullptr;
SemaphoreHandle_t clockMutex = nullptr;
TaskHandle_t clockTaskHandle = nullptr;
TaskHandle_t bluetoothTaskHandle = nullptr;
TaskHandle_t cvbsFailoverTaskHandle = nullptr;

static CompositeGrayDACI cvbsFallbackVideo;
static portMUX_TYPE cvbsInitMux = portMUX_INITIALIZER_UNLOCKED;

const uint8_t CVBS_FAILOVER_PIN = 25;
const int CVBS_SPLASH_W = 96;
const int CVBS_SPLASH_H = 144;
const char *CVBS_FAILOVER_MODE_NAME = "MODEPALQuarter144P";
const uint8_t CVBS_SPLASH_TEXT_W = 3;
const uint8_t CVBS_SPLASH_TEXT_H = 5;
const uint8_t CVBS_SPLASH_TEXT_SPACING = 1;
const uint8_t CVBS_SPLASH_LOGO_Y_DIV = 1;
const uint16_t CVBS_SIGNAL_SPAN_MIN = 140;
const uint16_t CVBS_SIGNAL_LOW_MAX = 520;
const uint16_t CVBS_SIGNAL_HIGH_MIN = 620;
const uint8_t CVBS_MISSING_CONFIRM_READS = 2;
const uint32_t CVBS_PASSIVE_CHECK_MS = 20;
const uint32_t CVBS_ACTIVE_RECHECK_MS = 180;
const uint32_t CVBS_ACTIVE_RECHECK_GRACE_MS = 250;
const uint32_t CVBS_INIT_RETRY_MS = 5000;
const size_t CVBS_INIT_MIN_DMA_FREE = 36U * 1024U;
const size_t CVBS_INIT_MIN_DMA_LARGEST = 16U * 1024U;

volatile bool cvbsFailoverEnabled = true;
volatile bool cvbsFailoverDriving = false;
volatile bool cvbsFallbackVideoReady = false;
volatile bool cvbsFallbackInitFailed = false;
volatile bool cvbsFallbackInitInProgress = false;
volatile bool cvbsForceFallback = false;
volatile bool cvbsKernelSignalPresent = false;
volatile unsigned long cvbsFallbackInitFailedAtMs = 0;
volatile uint16_t cvbsLastMin = 0;
volatile uint16_t cvbsLastMax = 0;
volatile uint16_t cvbsLastSpan = 0;
volatile unsigned long cvbsLastSignalSeenMs = 0;
volatile unsigned long cvbsKernelSignalStableSince = 0;
volatile unsigned long cvbsDriveStartedMs = 0;
volatile int cvbsBootProgressPercent = 0;
volatile unsigned long cvbsFailoverSuppressUntilMs = 0;
volatile bool cvbsFetchModeActive = false;
String cvbsBootProgressLabel = "Preparing Citadela";
uint32_t cvbsAppFlashTotalBytes = 0;
String cvbsAppFlashLabel = "Flashing app";
bool cvbsSplashStaticDrawn = false;
String cvbsSplashStaticLabel = "";
int cvbsSplashLastPercent = -1;
int cvbsSplashLastFillW = 0;
int cvbsSplashLogoX = 0;
int cvbsSplashLogoY = 0;
int cvbsSplashBarX = 0;
int cvbsSplashBarY = 0;
int cvbsSplashBarW = 0;
int cvbsSplashBarH = 0;
int cvbsSplashTextY = 0;
int cvbsSplashPercentBoxX = 0;
int cvbsSplashPercentBoxY = 0;
int cvbsSplashPercentBoxW = 34;
int cvbsSplashPercentBoxH = 8;
uint8_t cvbsFetchAnimationFrame = 0;
unsigned long cvbsFetchAnimationLastMs = 0;
bool bleRuntimeEnabled = false;
unsigned long nextBLEEnableTryMs = 0;
unsigned long blePausedUntilMs = 0;
const uint32_t BLE_ENABLE_RETRY_MS = 1000;
const uint32_t BLE_VIDEO_PAUSE_MS = 1500;

// enqueue a single line (without extra newline)
static inline void enqueuePeerLine(const String &s) {
  if (peerQueueMutex && xSemaphoreTake(peerQueueMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (peerQueue.size() >= PEER_QUEUE_MAX_LINES) peerQueue.pop_front();
    peerQueue.push_back(s);
    xSemaphoreGive(peerQueueMutex);
  } else {
    if (peerQueue.size() >= PEER_QUEUE_MAX_LINES) peerQueue.pop_front();
    peerQueue.push_back(s);
  }
}

static inline void enqueuePeerMouseTransition(const String &s) {
  if (peerQueueMutex && xSemaphoreTake(peerQueueMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    for (auto it = peerQueue.begin(); it != peerQueue.end();) {
      if (it->startsWith("MOUSE ")) it = peerQueue.erase(it);
      else ++it;
    }
    if (peerQueue.size() >= PEER_QUEUE_MAX_LINES) peerQueue.pop_front();
    peerQueue.push_back(s);
    xSemaphoreGive(peerQueueMutex);
  } else {
    for (auto it = peerQueue.begin(); it != peerQueue.end();) {
      if (it->startsWith("MOUSE ")) it = peerQueue.erase(it);
      else ++it;
    }
    if (peerQueue.size() >= PEER_QUEUE_MAX_LINES) peerQueue.pop_front();
    peerQueue.push_back(s);
  }
}

static void sendKernelVideoAck(const char *ack) {
  if (!ack || !ack[0]) return;
  Serial1.println(ack);
  Serial1.flush();
  Serial2.println(ack);
  Serial.println(ack);
}

// enqueue text but split into lines
static inline void enqueuePeerMultiLine(const String &text) {
  int start = 0;
  int len = text.length();
  while (start <= len) {
    int nl = text.indexOf('\n', start);
    String line;
    if (nl == -1) {
      line = text.substring(start);
      start = len + 1;
    } else {
      line = text.substring(start, nl);
      start = nl + 1;
    }
    enqueuePeerLine(line);
  }
}

// Protocol control messages must reach the kernel before a blocking network call.
static inline void sendPeerLineImmediate(const String &s) {
  Serial.println(s);
  String peerMessage = s + '\n';
  Serial1.write((const uint8_t *)peerMessage.c_str(), peerMessage.length());
  Serial2.println(s);
}

static void sendPeerMultiLineImmediate(const String &text) {
  int start = 0;
  int len = text.length();
  while (start <= len) {
    int nl = text.indexOf('\n', start);
    String line;
    if (nl == -1) {
      line = text.substring(start);
      start = len + 1;
    } else {
      line = text.substring(start, nl);
      start = nl + 1;
    }
    sendPeerLineImmediate(line);
  }
}

static inline void flushPeerImmediate() {
  Serial.flush();
  Serial1.flush();
  Serial2.flush();
}

// send at most one (or a small number) queued peer lines per loop iteration
static void flushPeerQueue() {
  unsigned long now = millis();
  if (now - lastPeerSend < PEER_SEND_INTERVAL_MS) return;
  uint8_t sent = 0;
  while (sent < PEER_LINES_PER_FLUSH) {
    String s;
    bool hasLine = false;
    if (peerQueueMutex && xSemaphoreTake(peerQueueMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (!peerQueue.empty()) {
        s = peerQueue.front();
        peerQueue.pop_front();
        hasLine = true;
      }
      xSemaphoreGive(peerQueueMutex);
    } else if (!peerQueueMutex && !peerQueue.empty()) {
      s = peerQueue.front();
      peerQueue.pop_front();
      hasLine = true;
    }
    if (!hasLine) break;
    if (USB_MOUSE_DEBUG || !s.startsWith("MOUSE ")) {
      Serial.println(s);
    }
    String peerMessage = s + '\n';
    Serial1.write((const uint8_t *)peerMessage.c_str(), peerMessage.length());
    Serial2.println(s);
    sent++;
  }
  if (sent > 0) lastPeerSend = now;
}

// fastCommand used for immediate small outputs (kept as enqueue to avoid blocking)
static inline void fastCommand(const String &s) {
  enqueuePeerLine(s);
}

// ----- End non-blocking serial helpers -----

static void sendHFOutput(const String &text) {
  enqueuePeerMultiLine(text);
}
static void sendHFShort(const String &text) {
  enqueuePeerLine(text);
}

static void enqueueBLEWork(const String &work, bool fromSerial1) {
  String item = String(fromSerial1 ? "1:" : "0:") + work;
  if (bleCommandMutex && xSemaphoreTake(bleCommandMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (bleCommandQueue.size() >= BLE_COMMAND_QUEUE_MAX) bleCommandQueue.pop_front();
    bleCommandQueue.push_back(item);
    xSemaphoreGive(bleCommandMutex);
  } else {
    if (bleCommandQueue.size() >= BLE_COMMAND_QUEUE_MAX) bleCommandQueue.pop_front();
    bleCommandQueue.push_back(item);
  }
}

static bool dequeueBLEWork(String &work, bool &fromSerial1) {
  String item;
  bool hasWork = false;
  if (bleCommandMutex && xSemaphoreTake(bleCommandMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (!bleCommandQueue.empty()) {
      item = bleCommandQueue.front();
      bleCommandQueue.pop_front();
      hasWork = true;
    }
    xSemaphoreGive(bleCommandMutex);
  } else if (!bleCommandMutex && !bleCommandQueue.empty()) {
    item = bleCommandQueue.front();
    bleCommandQueue.pop_front();
    hasWork = true;
  }
  if (!hasWork || item.length() < 3 || item.charAt(1) != ':') return false;
  fromSerial1 = item.charAt(0) == '1';
  work = item.substring(2);
  return true;
}

String loadFile(const char *p) {
  if (!SPIFFS.exists(p)) return "";
  File f = SPIFFS.open(p, "r");
  if(!f) return "";
  String s = f.readString();
  f.close();
  s.trim();
  return s;
}

void saveFile(const char *p, const String &v) {
  File f = SPIFFS.open(p, "w");
  if (f) {
    f.print(v);
    f.close();
  }
}

const char *FAN_SPEED_PATH = "/fan_percent.txt";
uint8_t fanPercent = 100;

static bool parseFanPercent(String valueText, uint8_t &percent) {
  valueText.trim();
  if (!valueText.length()) return false;
  for (size_t i = 0; i < valueText.length(); ++i) {
    if (!isDigit(valueText.charAt(i))) return false;
  }
  int value = valueText.toInt();
  if (value < 0 || value > 100) return false;
  percent = (uint8_t)value;
  return true;
}

static void loadFanControl() {
  String saved = loadFile(FAN_SPEED_PATH);
  uint8_t savedPercent = 0;
  if (parseFanPercent(saved, savedPercent)) fanPercent = savedPercent;
  controllerFan.setPercent(fanPercent);
  Serial.printf("FANCTL initialized at %u%% on GPIO %u.\n",
                (unsigned)fanPercent,
                (unsigned)Citadela::BoardPins::FanControl);
}

static void sendHardwareReply(const String &reply, bool fromSerial1) {
  if (fromSerial1) enqueuePeerLine(reply);
  else Serial.println(reply);
}

static bool handleFanControlCommand(const String &command, bool fromSerial1) {
  if (command == "CBFAN" || command == "FANCTL") {
    sendHardwareReply(String("CBFAN ") + fanPercent, fromSerial1);
    return true;
  }

  if (command == "CBFANON" || command == "FANCTL ON") {
    fanPercent = 100;
  } else if (command == "CBFANOFF" || command == "FANCTL OFF") {
    fanPercent = 0;
  } else if (command.startsWith("CBFAN ") || command.startsWith("FANCTL ")) {
    int separator = command.indexOf(' ');
    String valueText = command.substring(separator + 1);
    uint8_t requestedPercent = 0;
    if (!parseFanPercent(valueText, requestedPercent)) {
      sendHardwareReply("CBFAN BAD", fromSerial1);
      return true;
    }
    fanPercent = requestedPercent;
  } else {
    return false;
  }

  controllerFan.setPercent(fanPercent);
  saveFile(FAN_SPEED_PATH, String(fanPercent));
  sendHardwareReply(String("CBFAN OK ") + fanPercent, fromSerial1);
  return true;
}

const char *MANUAL_CLOCK_PATH = "/manual_clock_epoch.txt";
bool manualClockValid = false;
uint32_t manualClockBaseEpoch = 0;
unsigned long manualClockBaseMillis = 0;
unsigned long lastManualClockSave = 0;
uint32_t lastCBTimeReplyEpoch = 0;
unsigned long lastCBTimeReplyMs = 0;
const unsigned long MANUAL_CLOCK_SAVE_INTERVAL_MS = 1000UL;
volatile bool rtcAvailable = false;
uint8_t rtcReadFailures = 0;
unsigned long lastRTCReadMs = 0;

static bool clockLeapYear(int y) {
  return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int clockDaysInMonth(int y, int m) {
  static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && clockLeapYear(y)) return 29;
  if (m < 1 || m > 12) return 31;
  return days[m - 1];
}

static bool parseDateTimePayload(const String &payload, int &year, int &month, int &day, int &hour, int &minute, int &second) {
  if (payload.length() < 19) return false;
  if (payload.charAt(4) != '-' || payload.charAt(7) != '-' ||
      payload.charAt(10) != ' ' || payload.charAt(13) != ':' ||
      payload.charAt(16) != ':') return false;

  year = payload.substring(0, 4).toInt();
  month = payload.substring(5, 7).toInt();
  day = payload.substring(8, 10).toInt();
  hour = payload.substring(11, 13).toInt();
  minute = payload.substring(14, 16).toInt();
  second = payload.substring(17, 19).toInt();

  if (year < 2020 || year > 2099) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > clockDaysInMonth(year, month)) return false;
  if (hour < 0 || hour > 23) return false;
  if (minute < 0 || minute > 59) return false;
  if (second < 0 || second > 59) return false;
  return true;
}

static uint32_t dateTimeToEpochUTC(int year, int month, int day, int hour, int minute, int second) {
  uint32_t days = 0;
  for (int y = 1970; y < year; ++y) days += clockLeapYear(y) ? 366 : 365;
  for (int m = 1; m < month; ++m) days += clockDaysInMonth(year, m);
  days += (uint32_t)(day - 1);
  return days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + (uint32_t)second;
}

static uint8_t weekdayFromEpoch(uint32_t epoch) {
  return (uint8_t)(((epoch / 86400UL + 4UL) % 7UL) + 1UL);
}

static Citadela::DS1302RTC::DateTime rtcDateTimeFromEpoch(uint32_t epoch) {
  time_t value = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&value, &tmv);
  Citadela::DS1302RTC::DateTime dateTime;
  dateTime.year = (uint16_t)(tmv.tm_year + 1900);
  dateTime.month = (uint8_t)(tmv.tm_mon + 1);
  dateTime.day = (uint8_t)tmv.tm_mday;
  dateTime.hour = (uint8_t)tmv.tm_hour;
  dateTime.minute = (uint8_t)tmv.tm_min;
  dateTime.second = (uint8_t)tmv.tm_sec;
  dateTime.weekday = weekdayFromEpoch(epoch);
  return dateTime;
}

static bool readHardwareClockEpoch(uint32_t &epoch) {
  bool locked = !clockMutex || xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE;
  if (!locked) return false;
  Citadela::DS1302RTC::DateTime dateTime;
  bool ok = controllerRTC.read(dateTime);
  if (clockMutex) xSemaphoreGive(clockMutex);
  if (!ok) return false;
  epoch = dateTimeToEpochUTC(dateTime.year, dateTime.month, dateTime.day,
                             dateTime.hour, dateTime.minute, dateTime.second);
  return epoch > 1577836800UL;
}

static bool writeHardwareClockEpoch(uint32_t epoch) {
  if (epoch <= 1577836800UL) return false;
  Citadela::DS1302RTC::DateTime dateTime = rtcDateTimeFromEpoch(epoch);
  bool locked = !clockMutex || xSemaphoreTake(clockMutex, pdMS_TO_TICKS(40)) == pdTRUE;
  if (!locked) return false;
  bool ok = controllerRTC.write(dateTime);
  if (clockMutex) xSemaphoreGive(clockMutex);
  rtcAvailable = ok;
  rtcReadFailures = ok ? 0 : rtcReadFailures;
  return ok;
}

static uint32_t currentManualClockEpochUnlocked(unsigned long now) {
  if (!manualClockValid) return 0;
  return manualClockBaseEpoch + ((now - manualClockBaseMillis) / 1000UL);
}

static uint32_t currentManualClockEpoch() {
  unsigned long now = millis();
  uint32_t epoch = 0;
  if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    epoch = currentManualClockEpochUnlocked(now);
    xSemaphoreGive(clockMutex);
  } else {
    epoch = currentManualClockEpochUnlocked(now);
  }
  return epoch;
}

static void saveManualClockEpoch(uint32_t epoch) {
  if (epoch == 0) return;
  saveFile(MANUAL_CLOCK_PATH, String(epoch));
  unsigned long now = millis();
  if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    lastManualClockSave = now;
    xSemaphoreGive(clockMutex);
  } else {
    lastManualClockSave = now;
  }
}

static void setManualClockEpoch(uint32_t epoch, bool persistNow) {
  unsigned long now = millis();
  if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    manualClockValid = (epoch > 0);
    manualClockBaseEpoch = epoch;
    manualClockBaseMillis = now;
    xSemaphoreGive(clockMutex);
  } else {
    manualClockValid = (epoch > 0);
    manualClockBaseEpoch = epoch;
    manualClockBaseMillis = now;
  }
  if (persistNow) saveManualClockEpoch(epoch);
}

static void loadManualClock() {
  String saved = loadFile(MANUAL_CLOCK_PATH);
  if (!saved.length()) {
    if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      manualClockValid = false;
      xSemaphoreGive(clockMutex);
    } else {
      manualClockValid = false;
    }
    return;
  }
  uint32_t epoch = (uint32_t)saved.toInt();
  if (epoch > 1577836800UL) {
    setManualClockEpoch(epoch, false);
    Serial.printf("Manual clock loaded from SPIFFS: %lu\n", (unsigned long)epoch);
  } else {
    if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      manualClockValid = false;
      xSemaphoreGive(clockMutex);
    } else {
      manualClockValid = false;
    }
  }
}

static void initializeHardwareClock() {
  controllerRTC.begin();
  uint32_t rtcEpoch = 0;
  if (readHardwareClockEpoch(rtcEpoch)) {
    rtcAvailable = true;
    rtcReadFailures = 0;
    setManualClockEpoch(rtcEpoch, true);
    Serial.printf("DS1302 RTC ready: epoch=%lu (RST=%u IO=%u SCLK=%u).\n",
                  (unsigned long)rtcEpoch,
                  (unsigned)Citadela::BoardPins::RtcReset,
                  (unsigned)Citadela::BoardPins::RtcData,
                  (unsigned)Citadela::BoardPins::RtcClock);
    return;
  }

  uint32_t savedEpoch = currentManualClockEpoch();
  if (savedEpoch > 0 && writeHardwareClockEpoch(savedEpoch)) {
    Serial.println("DS1302 RTC initialized from the SPIFFS clock.");
  } else {
    rtcAvailable = false;
    Serial.println("DS1302 RTC unavailable or not set; using the SPIFFS clock fallback.");
  }
}

static void serviceHardwareClock() {
  unsigned long now = millis();
  if (now - lastRTCReadMs < 1000UL) return;
  lastRTCReadMs = now;

  uint32_t rtcEpoch = 0;
  if (readHardwareClockEpoch(rtcEpoch)) {
    bool wasAvailable = rtcAvailable;
    rtcAvailable = true;
    rtcReadFailures = 0;
    uint32_t softwareEpoch = currentManualClockEpoch();
    int64_t drift = (int64_t)rtcEpoch - (int64_t)softwareEpoch;
    if (softwareEpoch == 0 || drift < -1 || drift > 1) {
      setManualClockEpoch(rtcEpoch, false);
    }
    if (!wasAvailable) Serial.println("DS1302 RTC communication restored.");
    return;
  }

  if (rtcReadFailures < 255) ++rtcReadFailures;
  if (rtcReadFailures >= 3 && rtcAvailable) {
    rtcAvailable = false;
    Serial.println("DS1302 RTC read failed; continuing from the software clock.");
  }
}

static String formatEpochForCBTIME(uint32_t epoch) {
  if (epoch == 0) return String("2000-01-01 00:00:00");
  time_t t = (time_t)epoch;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(buf);
}

static String controllerTimeString() {
  uint32_t manualEpoch = currentManualClockEpoch();
  if (manualEpoch > 0) return formatEpochForCBTIME(manualEpoch);
#if ENABLE_NETWORK_FEATURES
  return getCurrentTimeString("%Y-%m-%d %H:%M:%S");
#else
  return String("2000-01-01 00:00:00");
#endif
}

static bool handleControllerTimeCommand(const String &command, bool fromSerial1) {
  if (command == "CBTIME") {
    unsigned long now = millis();
    uint32_t manualEpoch = currentManualClockEpoch();
    if (manualEpoch > 0) {
      if (manualEpoch == lastCBTimeReplyEpoch && now - lastCBTimeReplyMs < 950UL) return true;
      lastCBTimeReplyEpoch = manualEpoch;
      lastCBTimeReplyMs = now;
      enqueuePeerLine(String("CBTIME ") + formatEpochForCBTIME(manualEpoch));
    } else {
      if (now - lastCBTimeReplyMs < 950UL) return true;
      lastCBTimeReplyMs = now;
      enqueuePeerLine(String("CBTIME ") + controllerTimeString());
    }
    return true;
  }

  if (command.startsWith("CBSETTIME ")) {
    String payload = command.substring(10);
    payload.trim();
    int y, mo, d, h, mi, s;
    if (!parseDateTimePayload(payload, y, mo, d, h, mi, s)) {
      if (fromSerial1) enqueuePeerLine("CBTIMESET BAD");
      else Serial.println("CBTIMESET BAD");
      return true;
    }

    uint32_t epoch = dateTimeToEpochUTC(y, mo, d, h, mi, s);
    setManualClockEpoch(epoch, true);
    bool rtcWritten = writeHardwareClockEpoch(epoch);
    Serial.printf("Manual clock set: %s epoch=%lu\n", payload.c_str(), (unsigned long)epoch);
    if (!rtcWritten) Serial.println("DS1302 RTC write failed; the SPIFFS fallback remains active.");
    enqueuePeerLine("CBTIMESET OK");
    enqueuePeerLine(String("CBTIME ") + formatEpochForCBTIME(currentManualClockEpoch()));
    return true;
  }

  if (command == "CBTIMECLR") {
    if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      manualClockValid = false;
      manualClockBaseEpoch = 0;
      manualClockBaseMillis = millis();
      xSemaphoreGive(clockMutex);
    } else {
      manualClockValid = false;
      manualClockBaseEpoch = 0;
      manualClockBaseMillis = millis();
    }
    bool locked = !clockMutex || xSemaphoreTake(clockMutex, pdMS_TO_TICKS(40)) == pdTRUE;
    if (locked) {
      controllerRTC.stop();
      if (clockMutex) xSemaphoreGive(clockMutex);
    }
    rtcAvailable = false;
    rtcReadFailures = 3;
    SPIFFS.remove(MANUAL_CLOCK_PATH);
    if (fromSerial1) enqueuePeerLine("CBTIMECLR OK");
    else Serial.println("Manual clock cleared.");
    return true;
  }

  if (command == "CBRTC") {
    uint32_t epoch = 0;
    if (readHardwareClockEpoch(epoch)) {
      rtcAvailable = true;
      sendHardwareReply(String("CBRTC OK ") + formatEpochForCBTIME(epoch), fromSerial1);
    } else {
      sendHardwareReply("CBRTC UNAVAILABLE", fromSerial1);
    }
    return true;
  }

  return false;
}

static void serviceManualClockPersistence() {
  unsigned long now = millis();
  uint32_t epoch = 0;
  bool shouldSave = false;
  if (clockMutex && xSemaphoreTake(clockMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (manualClockValid && now - lastManualClockSave >= MANUAL_CLOCK_SAVE_INTERVAL_MS) {
      epoch = currentManualClockEpochUnlocked(now);
      lastManualClockSave = now;
      shouldSave = true;
    }
    xSemaphoreGive(clockMutex);
  } else if (manualClockValid && now - lastManualClockSave >= MANUAL_CLOCK_SAVE_INTERVAL_MS) {
    epoch = currentManualClockEpochUnlocked(now);
    lastManualClockSave = now;
    shouldSave = true;
  }
  if (shouldSave && epoch > 0) {
    saveFile(MANUAL_CLOCK_PATH, String(epoch));
  }
}

#if ENABLE_NETWORK_FEATURES
void syncTime(unsigned long timeoutMs = 15000) {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  unsigned long t0 = millis();
  while (time(nullptr) < 1600000000 && millis() - t0 < timeoutMs) {
    delay(200);
  }
  setBulgariaTZ();
  Serial.printf("NTP done, epoch=%ld\n", time(nullptr));
}

String sanitizeHost(const String &h) {
  String s = h;
  s.trim();
  if (s.startsWith("http://")) s = s.substring(7);
  if (s.startsWith("https://")) s = s.substring(8);
  while (s.endsWith("/")) s = s.substring(0, s.length() - 1);
  return s;
}

String extractHostFromEndpoint(const String &endpoint) {
  int idx = endpoint.indexOf("://");
  int start = (idx >= 0) ? idx + 3 : 0;
  int slash = endpoint.indexOf('/', start);
  if (slash > 0) return endpoint.substring(start, slash);
  return endpoint.substring(start);
}

void testNetwork(const String &testHost = "example.com") {
  Serial.printf("WiFi status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());

  IPAddress ip;
  if (WiFi.hostByName(testHost.c_str(), ip)) {
    Serial.printf("DNS OK: %s -> %s\n", testHost.c_str(), ip.toString().c_str());
  } else {
    Serial.printf("DNS FAIL for %s\n", testHost.c_str());
  }

  HTTPClient http;
  WiFiClient plain;
  String url = String("http://") + testHost + "/";
  if (http.begin(plain, url)) {
    int code = http.GET();
    Serial.printf("http GET %s -> code %d\n", url.c_str(), code);
    String b = code > 0 ? http.getString() : String();
    Serial.println("body (first 200 chars):");
    Serial.println(b.substring(0, min((int)b.length(), 200)));
    http.end();
  } else {
    Serial.println("http.begin() failed for http test");
  }
}

bool connectWiFiAuto(unsigned long timeoutMs = 20000) {
  if (!savedSSID.length()) return false;
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    syncTime();
    return true;
  }
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    if (bleRuntimeEnabled || pClient || pBLEScan) {
      Serial.println("Pausing BLE to free heap for WiFi connection...");
      stopBLERuntimeForVideo();
      delay(120);
    }
    WiFi.persistent(false);
    if (!WiFi.mode(WIFI_STA)) {
      Serial.printf("WiFi station mode failed: free heap=%u\n", ESP.getFreeHeap());
      return false;
    }
    delay(120);
  }
  WiFi.setHostname("CITADELA");
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(250);
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) syncTime();
  return wifiConnected;
}

void startWiFiAuto() {
  if (!savedSSID.length()) {
    wifiConnected = false;
    return;
  }
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("CITADELA");
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  wifiConnected = false;
  Serial.println("WiFi background connect started");
}

void pollWiFiStatus() {
  static wl_status_t lastStatus = WL_IDLE_STATUS;
  wl_status_t status = WiFi.status();
  wifiConnected = (status == WL_CONNECTED);
  if (status != lastStatus) {
    Serial.printf("WiFi status changed: %d\n", status);
    lastStatus = status;
  }
}
#endif

void boolTru() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    if (!file) return;
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
        Serial.printf("Bool updated to %s\n", boolRes.c_str());
    } else {
        Serial.println("Failed to update bool!");
    }
}
void saveAddress() {
    if (!Citadela::BLEHID::PairingStore::save(SPIFFS, "/addresser.txt", lastAddress, lastAddressType)) {
        Serial.println("Failed to open address file for writing");
        return;
    }
    Serial.printf("File written successfully %s type %u\n", lastAddress.c_str(), lastAddressType);
}

void readAddress() {
    String loadedAddress;
    uint8_t loadedType = BLE_ADDR_TYPE_PUBLIC;
    if (!Citadela::BLEHID::PairingStore::load(SPIFFS, "/addresser.txt", loadedAddress, loadedType)) {
        Serial.println("Failed to open file for reading");
        return;
    }
    lastAddress = loadedAddress;
    lastAddressType = loadedType;
    Serial.printf("Received Spiffs: %s type %u\n", lastAddress.c_str(), lastAddressType);
}

void clearSavedBLEDevice(bool fromSerial1) {
    if (pClient && pClient->isConnected()) {
        bleReconnectSuppressedUntil = millis() + 1500;
        pClient->disconnect();
    }
    inputNotifyRegistered = false;
    pHIDService = nullptr;
    pInputReportChar = nullptr;
    pTrackpadChar = nullptr;
    lastAddress = "";
    lastAddressType = BLE_ADDR_TYPE_PUBLIC;
    cancelBLEReconnect();
    deviceList.clear();
    deviceNames.clear();
    Citadela::BLEHID::PairingStore::clear(SPIFFS, "/addresser.txt");
    if (fromSerial1) enqueuePeerLine("BLEC OK");
    else Serial.println("Saved BLE pairing cleared.");
}

static void clearBLEHandles() {
    inputNotifyRegistered = false;
    hidReady = false;
    inputReportChars.clear();
    pHIDService = nullptr;
    pInputReportChar = nullptr;
    pTrackpadChar = nullptr;
}

static void suppressAutoReconnect(uint32_t durationMs) {
    cancelBLEReconnect();
    bleReconnectSuppressedUntil = millis() + durationMs;
}

static void unregisterHIDNotifications() {
    if (inputNotifyRegistered) {
        for (BLERemoteCharacteristic *ch : inputReportChars) {
            if (ch) ch->registerForNotify(nullptr);
        }
        if (pTrackpadChar) pTrackpadChar->registerForNotify(nullptr);
    }
    inputNotifyRegistered = false;
    hidReady = false;
    pInputReportChar = nullptr;
    inputReportChars.clear();
}

static void disconnectCurrentBLE(bool suppressReconnect) {
    if (!pClient) return;
    unregisterHIDNotifications();
    clearBLEHandles();
    if (!pClient->isConnected()) return;
    if (suppressReconnect) suppressAutoReconnect(2000);
    pClient->disconnect();
    unsigned long start = millis();
    while (pClient->isConnected() && millis() - start < 800) {
        delay(10);
    }
    if (suppressReconnect) suppressAutoReconnect(1000);
}

static bool waitForLateBLEConnection(uint32_t graceMs, const char *context) {
    if (!pClient) return false;
    unsigned long start = millis();
    while (!pClient->isConnected() && millis() - start < graceMs) {
        delay(25);
    }
    if (pClient->isConnected()) {
        Serial.printf("%s: BLE link became connected after connect returned false\n", context);
        return true;
    }
    return false;
}

static void hardResetBLEClient(const char *reason) {
    Serial.printf("Hard-resetting BLE client: %s\n", reason ? reason : "unspecified");
    if (pBLEScan) {
        pBLEScan->stop();
    }

    if (pClient) {
        unregisterHIDNotifications();
        clearBLEHandles();
        if (pClient->isConnected()) {
            pClient->disconnect();
            unsigned long start = millis();
            while (pClient->isConnected() && millis() - start < 1500) {
                delay(20);
            }
        }
        // BLEDevice owns this singleton client. Keep it alive so its internal
        // pointer cannot outlive a manually deleted object during TLS teardown.
        pClient->setClientCallbacks(&myClientCallbacks);
    } else {
        clearBLEHandles();
    }

    inputNotifyRegistered = false;
    hidReady = false;
    hidMouseButtons = 0;
    hidMouseInputReportId = -1;
}

static bool recycleBLEStack(const char *reason) {
    unsigned long now = millis();
    if (lastBLEStackRecycleMs && now - lastBLEStackRecycleMs < BLE_STACK_RECYCLE_COOLDOWN_MS) {
        return false;
    }
    lastBLEStackRecycleMs = now;
    Serial.printf("Recycling BLE stack: %s\n", reason ? reason : "unspecified");

    if (pBLEScan) pBLEScan->stop();
    if (pClient) {
        unregisterHIDNotifications();
        clearBLEHandles();
        pClient->setClientCallbacks(nullptr);
        if (pClient->isConnected()) {
            pClient->disconnect();
            unsigned long waitStart = millis();
            while (pClient->isConnected() && millis() - waitStart < 800) delay(20);
        }
    }

    delay(80);
    BLEDevice::deinit(false);
    pClient = nullptr;
    pBLEScan = nullptr;
    clearBLEHandles();
    bleRuntimeEnabled = false;
    delay(180);

    if (!BLEDevice::init("CITADELA")) {
        Serial.println("BLE stack recycle init failed");
        return false;
    }
    configureBLESecurity(true);
    pClient = BLEDevice::createClient();
    if (pClient) pClient->setClientCallbacks(&myClientCallbacks);
    pBLEScan = BLEDevice::getScan();
    if (pBLEScan) {
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(80);
        pBLEScan->setWindow(70);
    }

    bleRuntimeEnabled = pClient && pBLEScan;
    Serial.println(bleRuntimeEnabled ? "BLE stack recycle complete" : "BLE stack recycle failed");
    return bleRuntimeEnabled;
}

static bool scanForSavedBLEDevice(BLEAdvertisedDevice &matchedDevice) {
    if (!lastAddress.length()) return false;
    if (!pBLEScan) initBLEClient();
    if (!pBLEScan) return false;

    pBLEScan->stop();
    pBLEScan->clearResults();
    Serial.printf("Scanning for saved BLE keyboard %s...\n", lastAddress.c_str());

    BLEScanResults foundDevices = *pBLEScan->start(BLE_RECONNECT_SCAN_SECONDS, false);
    bool found = false;
    for (int i = 0; i < foundDevices.getCount(); i++) {
        BLEAdvertisedDevice device = foundDevices.getDevice(i);
        String address = device.getAddress().toString();
        if (!address.equalsIgnoreCase(lastAddress)) continue;

        matchedDevice = device;
        found = true;
        Serial.printf("Saved keyboard is advertising: %s type %u RSSI %d\n",
                      address.c_str(), device.getAddressType(), device.getRSSI());
        break;
    }
    pBLEScan->clearResults();

    if (!found) {
        Serial.println("Saved keyboard is not advertising yet");
        return false;
    }

    // Let the controller settle after scanning before opening the GATT link.
    delay(60);
    return true;
}

static void emitCachedDeviceList() {
    devices = 0;
    for (size_t i = 0; i < deviceNames.size(); i++) {
        devices++;
        enqueuePeerLine(String("dev") + devices + String(" ") + deviceNames[i]);
    }
}

void scanDevices(bool force, bool preferSavedAddress) {
    if (blePausing) return;
    devices = 0;
    static unsigned long lastScanMillis = 0;

    if (preferSavedAddress && lastAddress.length() > 0 && !(pClient && pClient->isConnected())) {
        Serial.printf("Trying saved BLE address before scan: %s type %u\n", lastAddress.c_str(), lastAddressType);
        BLEAddress saved(lastAddress.c_str(), lastAddressType);
        if (connectToAddress(saved, lastAddressType)) {
            return;
        }
    }

    if (!force && deviceList.size() > 0 && millis() - lastScanMillis < BLE_SCAN_CACHE_MS) {
        emitCachedDeviceList();
        return;
    }

    if (!pBLEScan) initBLEClient();
    if (!pBLEScan) return;

    pBLEScan->stop();
    BLEScanResults foundDevices = *pBLEScan->start(BLE_SCAN_SECONDS, false);
    int count = foundDevices.getCount();
    deviceList.clear();
    deviceNames.clear();
    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = foundDevices.getDevice(i);
        String deviceName = device.getName().c_str();
        if (deviceName.length() == 0) {
            deviceName = "Unknown";
        }
        if (deviceName != "Unknown") {
            devices++;
            deviceList.push_back(device);
            deviceNames.push_back(deviceName);
            Serial.printf("scan dev%d: %s addr=%s type=%u hidAdv=%s\n",
                          devices,
                          deviceName.c_str(),
                          device.getAddress().toString().c_str(),
                          device.getAddressType(),
                          device.isAdvertisingService(HID_SERVICE_UUID) ? "yes" : "no");
            String devLine = String("dev") + devices + String(" ") + deviceName;
            enqueuePeerLine(devLine);
        }
    }
    lastScanMillis = millis();
    pBLEScan->clearResults();
}

#if ENABLE_NETWORK_FEATURES
static void setBulgariaTZ() {
  setenv("TZ", "EET-2EEST-3,M3.5.0/3,M10.5.0/4", 1);
  tzset();
}

bool ensureTimeSynced(unsigned long timeoutMs = 15000) {
  if (time(nullptr) > 1600000000) return true;

  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFiAuto(timeoutMs)) {
      Serial.println("ensureTimeSynced: WiFi not connected, cannot sync time");
      return false;
    }
  }

  setBulgariaTZ();

  configTime(0, 0, "pool.ntp.org", "time.google.com");

  unsigned long start = millis();
  while (time(nullptr) < 1600000000 && millis() - start < timeoutMs) {
    delay(200);
  }
  if (time(nullptr) < 1600000000) {
    Serial.println("ensureTimeSynced: NTP sync failed / timeout");
    return false;
  }
  Serial.printf("ensureTimeSynced: epoch=%ld\n", time(nullptr));
  return true;
}

String getCurrentTimeString(const char *fmt = "%Y-%m-%d %H:%M:%S") {
  if (!ensureTimeSynced(15000)) {
    return String("<no-time>");
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), fmt, &timeinfo);
  return String(buf);
}

String getCurrentTimeISO() {
  return getCurrentTimeString("%Y-%m-%dT%H:%M:%S%z");
}
#endif
static void dumpRemoteServices(const char *reason) {
    if (!pClient || !pClient->isConnected()) {
        Serial.printf("Cannot dump services (%s): BLE client is not connected\n", reason);
        return;
    }
    auto *services = pClient->getServices();
    if (!services) {
        Serial.printf("Service dump (%s): getServices returned null\n", reason);
        return;
    }
    Serial.printf("Service dump (%s): %u services\n", reason, (unsigned)services->size());
    for (auto const &servicePair : *services) {
        BLERemoteService *service = servicePair.second;
        if (service) {
            Serial.printf("  service %s\n", service->getUUID().toString().c_str());
        }
    }
}

static bool looksLikeKeyboardReport(uint8_t* data, size_t length, size_t offset) {
    return Citadela::BLEHID::looksLikeKeyboardReport(data, length, offset);
}

static int16_t clampMouseCoord(int32_t value, int16_t limit) {
    if (value < 0) return 0;
    if (value >= limit) return limit - 1;
    return (int16_t)value;
}

static bool looksLikeMouseButtons(uint8_t buttons) {
    return Citadela::BLEHID::looksLikeMouseButtons(buttons);
}

static bool applyRelativeMouseReport(uint8_t* data, size_t length, size_t offset, int8_t reportId) {
    if (!data || length < offset + 3) return false;

    uint8_t buttons = data[offset];
    if (!looksLikeMouseButtons(buttons)) return false;

    int16_t dx = (int8_t)data[offset + 1];
    int16_t dy = (int8_t)data[offset + 2];
    int16_t wheel = 0;
    if (length > offset + 3) {
        wheel = (int8_t)data[offset + 3];
    }

    if (dx == 0 && dy == 0 && wheel == 0 && buttons == hidMouseButtons) {
        return false;
    }

    bool buttonsChanged = buttons != hidMouseButtons;
    hidMouseX = clampMouseCoord((int32_t)hidMouseX + dx, HID_MOUSE_SCREEN_W);
    hidMouseY = clampMouseCoord((int32_t)hidMouseY + dy, HID_MOUSE_SCREEN_H);
    hidMouseButtons = buttons;
    if (reportId >= 0) hidMouseInputReportId = reportId;

    char line[72];
    snprintf(line,
             sizeof(line),
             "MOUSE %d %d %u %d %d %d",
             hidMouseX,
             hidMouseY,
             (unsigned)hidMouseButtons,
             dx,
             dy,
             wheel);
    if (buttonsChanged) enqueuePeerMouseTransition(String(line));
    else enqueuePeerLine(String(line));
    return true;
}

static bool handleMouseReport(BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
    (void)isNotify;
    if (!data || len < 3) return false;

    bool bootMouse = c && c->getUUID().equals(HID_BOOT_MOUSE_INPUT_CHAR_UUID);
    if (bootMouse) {
        bool handled = applyRelativeMouseReport(data, len, 0, -1);
        if (handled) pTrackpadChar = c;
        return handled;
    }

    if (hidMouseInputReportId >= 0 && len >= 4 && data[0] == (uint8_t)hidMouseInputReportId) {
        if (applyRelativeMouseReport(data, len, 1, hidMouseInputReportId)) {
            pTrackpadChar = c;
            return true;
        }
    }

    if (len >= 4 && data[0] != 0 && looksLikeMouseButtons(data[1])) {
        bool reportIdLikely = (data[0] > 0x1F) ||
                              (data[1] == 0 && (data[2] != 0 || data[3] != 0));
        if (reportIdLikely && applyRelativeMouseReport(data, len, 1, (int8_t)data[0])) {
            pTrackpadChar = c;
            return true;
        }
    }

    if (applyRelativeMouseReport(data, len, 0, -1)) {
        pTrackpadChar = c;
        return true;
    }

    if (len >= 4 && data[0] != 0 && looksLikeMouseButtons(data[1])) {
        bool handled = applyRelativeMouseReport(data, len, 1, (int8_t)data[0]);
        if (handled) pTrackpadChar = c;
        return handled;
    }

    return false;
}

static bool isSubscribedHIDReport(BLERemoteCharacteristic *characteristic) {
    if (!characteristic) return true;
    uint16_t handle = characteristic->getHandle();
    for (BLERemoteCharacteristic *existing : inputReportChars) {
        if (existing && existing->getHandle() == handle) return true;
    }
    return false;
}

static bool subscribeHIDReportCharacteristic(BLERemoteCharacteristic *characteristic, const char *label) {
    if (!characteristic || isSubscribedHIDReport(characteristic)) return false;
    bool useNotifications = characteristic->canNotify();
    if (!useNotifications && !characteristic->canIndicate()) {
        String uuid = characteristic->getUUID().toString();
        Serial.printf("Skipping HID %s handle=0x%04x uuid=%s: no notify/indicate\n",
                      label,
                      characteristic->getHandle(),
                      uuid.c_str());
        return false;
    }

    characteristic->registerForNotify(hidReportNotifyCallback, useNotifications);
    inputReportChars.push_back(characteristic);
    if (!pInputReportChar) pInputReportChar = characteristic;
    String uuid = characteristic->getUUID().toString();
    Serial.printf("Subscribed HID %s handle=0x%04x uuid=%s via %s\n",
                  label,
                  characteristic->getHandle(),
                  uuid.c_str(),
                  useNotifications ? "notify" : "indicate");
    return true;
}

static void wakeHIDDevice() {
    Citadela::BLEHID::wakeDevice(pHIDService, &Serial);
}

static void hidReportNotifyCallback(BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
    if (!data || len == 0) return;
    bool bootMouse = c && c->getUUID().equals(HID_BOOT_MOUSE_INPUT_CHAR_UUID);
    bool knownMouseId = hidMouseInputReportId >= 0 && len >= 4 &&
                        data[0] == (uint8_t)hidMouseInputReportId;
    bool knownMouseCharacteristic = hidMouseInputReportId < 0 && c && pTrackpadChar &&
                                    c->getHandle() == pTrackpadChar->getHandle();
    if (bootMouse || knownMouseId || knownMouseCharacteristic) {
        if (handleMouseReport(c, data, len, isNotify)) return;
    }
    if (looksLikeKeyboardReport(data, len, 0) ||
        (len >= 9 && data[0] != 0 && looksLikeKeyboardReport(data, len, 1))) {
        onKeyPress(c, data, len, isNotify);
        return;
    }
    if (handleMouseReport(c, data, len, isNotify)) return;
    onKeyPress(c, data, len, isNotify);
}

bool keyboardAuthService() {
    hidReady = false;
    hidMouseButtons = 0;
    hidMouseInputReportId = -1;
    if (!pClient) {
      Serial.println("keyboardAuthService: pClient is null");
      return false;
  }
    if (!pClient->isConnected()) {
        Serial.println("keyboardAuthService: BLE client is not connected");
        return false;
    }

    BLESecurity::waitForAuthenticationComplete(4000);
    delay(200);
    pHIDService = nullptr;
    for (uint8_t attempt = 0; attempt < 5 && pHIDService == nullptr; attempt++) {
        pClient->getServices();
        pHIDService = pClient->getService(HID_SERVICE_UUID);
        if (pHIDService == nullptr) delay(300);
    }
    if (pHIDService == nullptr) {
        Serial.println("HID Service not found!");
        dumpRemoteServices("missing HID");
        return false;
    }

    unregisterHIDNotifications();
    wakeHIDDevice();

    uint8_t subscribedReports = 0;
    BLERemoteCharacteristic *bootKeyboardChar = pHIDService->getCharacteristic(HID_BOOT_KEYBOARD_INPUT_CHAR_UUID);
    if (subscribeHIDReportCharacteristic(bootKeyboardChar, "boot-keyboard")) {
        subscribedReports++;
    }

    BLERemoteCharacteristic *bootMouseChar = pHIDService->getCharacteristic(HID_BOOT_MOUSE_INPUT_CHAR_UUID);
    if (subscribeHIDReportCharacteristic(bootMouseChar, "boot-mouse")) {
        pTrackpadChar = bootMouseChar;
        subscribedReports++;
    }

    auto *characteristics = pHIDService->getCharacteristicsByHandle();
    if (characteristics) {
        for (auto const &entry : *characteristics) {
            BLERemoteCharacteristic *characteristic = entry.second;
            if (!characteristic) continue;
            BLEUUID uuid = characteristic->getUUID();
            if (uuid.equals(HID_INPUT_CHAR_UUID) ||
                uuid.equals(HID_BOOT_KEYBOARD_INPUT_CHAR_UUID) ||
                uuid.equals(HID_BOOT_MOUSE_INPUT_CHAR_UUID)) {
                if (subscribeHIDReportCharacteristic(characteristic, "input-report")) {
                    subscribedReports++;
                }
            }
        }
    }

    if (subscribedReports == 0) {
        BLERemoteCharacteristic *fallbackReportChar = pHIDService->getCharacteristic(HID_INPUT_CHAR_UUID);
        if (subscribeHIDReportCharacteristic(fallbackReportChar, "fallback-input-report")) {
            subscribedReports++;
        }
    }

    if (subscribedReports == 0) {
        Serial.println("Keyboard input report not found or not notifiable!");
        dumpRemoteServices("missing input report");
        return false;
    }

    inputNotifyRegistered = true;
    hidReady = true;
    Serial.printf("HID notifications active: %u report characteristic(s)\n", subscribedReports);
    return true;
}

void onKeyPress(BLERemoteCharacteristic* pCharacteristic,
                uint8_t* data,
                size_t length,
                bool isNotify) {
    size_t reportOffset = 0;
    if (length >= 9 && data[0] != 0 && looksLikeKeyboardReport(data, length, 1)) {
        reportOffset = 1;
    } else if (!looksLikeKeyboardReport(data, length, 0)) {
        return;
    }

    static uint8_t lastKeycode = 0;
    static bool reportHadInput = false;

    uint8_t modifier = data[reportOffset];
    uint8_t keycode = 0;

    bool reportHasInput = (modifier != 0);
    for (size_t i = reportOffset + 2; i < length && i < reportOffset + 8; i++) {
        if (data[i] != 0) {
            if (keycode == 0) keycode = data[i];
            reportHasInput = true;
        }
    }
    if (!reportHasInput) {
        if (reportHadInput) {
            fastCommand("rlsd");
        }
        reportHadInput = false;
        lastKeycode = 0;
        return;
    }
    reportHadInput = true;

    String key = "";

    if (modifier & 0x01) key += "LeftCtrl + ";
    if (modifier & 0x02) key += "LeftShift + ";
    if (modifier & 0x04) key += "LeftAlt + ";
    if (modifier & 0x08) key += "LeftGUI (Win) + ";
    if (modifier & 0x10) key += "RightCtrl + ";
    if (modifier & 0x20) key += "RightShift + ";
    if (modifier & 0x40) key += "RightAlt + ";
    if (modifier & 0x80) key += "RightGUI (Win) + ";

    if (keycode == 0x39 && lastKeycode != 0x39) {
        capsLock = !capsLock;
        enqueuePeerLine(String("CapsLock ") + (capsLock ? "ON" : "OFF"));
        lastKeycode = keycode;
        return;
    }

    switch (keycode) {
        case 0x28: key += "Enter";      break;
        case 0x29: key += "Escape";     break;
        case 0x2A: key += "Backspace";  break;
        case 0x2B: key += "Tab";        break;
        case 0x2C: key += "Space";      break;
        case 0x4C: key += "Delete";     break;
        case 0x4F: key += "RightArrow"; break;
        case 0x50: key += "LeftArrow";  break;
        case 0x51: key += "DownArrow";  break;
        case 0x52: key += "UpArrow";    break;
        case 0x33: key += ","; break;
        case 0x34: key += "."; break;
        case 0x35: key += "/"; break;
        case 0x2D: key += "-"; break;
        case 0x2E: key += "="; break;
        case 0x2F: key += "{"; break;
        case 0x30: key += "}"; break;
        case 0x31: key += "\\";break;
        case 0x32: key += ";"; break;
        case 0x36: key += "'"; break;
        case 0x38: key += "~"; break;
        case 0x64: key += "!"; break;
        case 0x65: key += "?"; break;
        case 0x66: key += "("; break;
        case 0x67: key += ")"; break;
        default:
            if (keycode >= 4 && keycode <= 29) {
                key += char('a' + (keycode - 4));
            } else if (keycode >= 30 && keycode <= 39) {
                // numbers 1..0
                key += String(keycode - 29);
            }
            break;
    }

    bool shift = (modifier & 0x02) || (modifier & 0x20);

    if (key.length() == 1) {
        char c = key.charAt(0);
        if (c >= 'a' && c <= 'z') {
            if (shift ^ capsLock) {
                c = c - 'a' + 'A';
                key = String(c);
            }
        }
    }

    if (shift) {
        if      (key == "1") key = "!";
        else if (key == "2") key = "@";
        else if (key == "3") key = "#";
        else if (key == "4") key = "$";
        else if (key == "5") key = "%";
        else if (key == "6") key = "^";
        else if (key == "7") key = "&";
        else if (key == "8") key = "*";
        else if (key == "9") key = "(";
        else if (key == "0") key = ")";
        else if (key == ",") key = "<";
        else if (key == ".") key = ">";
        else if (key == "/") key = "?";
        else if (key == ";") key = ":";
        else if (key == "'") key = "\"";
        else if (key == "[") key = "{";
        else if (key == "]") key = "}";
        else if (key == "\\") key = "|";
        else if (key == "-") key = "_";
        else if (key == "=") key = "+";
    }

    if (!key.isEmpty() && !silence) {
        fastCommand(key);
    }
    lastKeycode = keycode;
}


// Initialize BLE client & scan objects (idempotent)
void initBLEClient() {
  if (!pClient) {
    BLEDevice::init("CITADELA");
    configureBLESecurity();
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&myClientCallbacks);
  } else {
    pClient->setClientCallbacks(&myClientCallbacks);
  }
  if (!pBLEScan) {
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(80);
    pBLEScan->setWindow(70);
  }
}

// connect to address (explicit)
bool connectToAddress(BLEAddress address, uint8_t addressType) {
  if (blePausing) return false;

  if (!pClient) {
    Serial.println("connectToAddress: recreating BLE client");
    initBLEClient();
  }

  if (addressType != 0xFF) {
    address.setType(addressType);
  } else {
    addressType = address.getType();
  }

  String addrStr = address.toString();
  if (pClient->isConnected()) {
    if (lastAddress.equalsIgnoreCase(addrStr)) {
      if (hidReady || keyboardAuthService()) {
        cancelBLEReconnect();
        bleReconnectSuppressedUntil = 0;
        reconnectBackoff = RECONNECT_MIN_BACKOFF;
        enqueuePeerLine("BL1X");
        return true;
      }
      disconnectCurrentBLE(true);
      return false;
    }
    disconnectCurrentBLE(true);
  }

  Serial.printf("Attempting connection to %s type %u...\n", addrStr.c_str(), addressType);

  bleManualConnectInProgress = true;
  suppressAutoReconnect(BLE_CONNECT_TIMEOUT_MS + 1500);
  bool ok = pClient->connect(address, addressType, BLE_CONNECT_TIMEOUT_MS);
  if (!ok && waitForLateBLEConnection(1500, "connectToAddress")) {
    ok = true;
  }
  if (!ok) {
    bleManualConnectInProgress = false;
    Serial.println("BLE connect failed");
    bool retrySavedTarget = lastAddress.length() && lastAddress.equalsIgnoreCase(addrStr);
    hardResetBLEClient("connectToAddress failed");
    if (retrySavedTarget) {
      bleReconnectSuppressedUntil = 0;
      scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
    } else {
      suppressAutoReconnect(1000);
    }
    return false;
  }

  Serial.println("BLE connected");

  if (!keyboardAuthService()) {
    Serial.println("BLE connected but HID setup failed");
    bleManualConnectInProgress = false;
    bool retrySavedTarget = lastAddress.length() && lastAddress.equalsIgnoreCase(addrStr);
    hardResetBLEClient("connectToAddress HID setup failed");
    if (retrySavedTarget) {
      bleReconnectSuppressedUntil = 0;
      scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
    } else {
      suppressAutoReconnect(1000);
    }
    return false;
  }

  bleManualConnectInProgress = false;
  cancelBLEReconnect();
  bleReconnectSuppressedUntil = 0;
  reconnectBackoff = RECONNECT_MIN_BACKOFF;
  reconnectFailures = 0;
  lastAddress = addrStr;
  lastAddressType = addressType;
  saveAddress();

  enqueuePeerLine("BL1X");

  return true;
}

void connectToDevice(int index) {
    if (deviceList.empty()) {
        enqueuePeerLine("BLEX");
        return;
    }
    if (index < 1 || index > deviceList.size()) {
        enqueuePeerLine("BL4X");
        return;
    }
    BLEAdvertisedDevice &device = deviceList[index - 1];
    initBLEClient();
    if (pBLEScan) pBLEScan->stop();
    String addrStr = device.getAddress().toString();
    if (pClient->isConnected()) {
        if (lastAddress.equalsIgnoreCase(addrStr)) {
            if (hidReady || keyboardAuthService()) {
                cancelBLEReconnect();
                bleReconnectSuppressedUntil = 0;
                reconnectBackoff = RECONNECT_MIN_BACKOFF;
                enqueuePeerLine("BL1X");
                return;
            }
            disconnectCurrentBLE(true);
        } else {
            disconnectCurrentBLE(true);
        }
    }
    Serial.printf("Connecting to scan slot %d: %s type %u\n",
                  index,
                  addrStr.c_str(),
                  device.getAddressType());
    bleManualConnectInProgress = true;
    suppressAutoReconnect(BLE_CONNECT_TIMEOUT_MS + 1500);
    bool ok = pClient->connectTimeout(&device, BLE_CONNECT_TIMEOUT_MS);
    if (!ok && waitForLateBLEConnection(1500, "connectToDevice")) {
        ok = true;
    }
    if (ok && keyboardAuthService()) {
        bleManualConnectInProgress = false;
        cancelBLEReconnect();
        bleReconnectSuppressedUntil = 0;
        reconnectBackoff = RECONNECT_MIN_BACKOFF;
        reconnectFailures = 0;
        lastAddress = addrStr;
        lastAddressType = device.getAddressType();
        saveAddress();
        enqueuePeerLine("BL1X");
        // success
    } else {
        bleManualConnectInProgress = false;
        Serial.println(ok ? "BLE scanned device connected but HID setup failed" : "BLE connect failed from scanned device");
        bool retrySavedTarget = lastAddress.length() && lastAddress.equalsIgnoreCase(addrStr);
        hardResetBLEClient(ok ? "connectToDevice HID setup failed" : "connectToDevice failed");
        if (retrySavedTarget) {
            bleReconnectSuppressedUntil = 0;
            scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
        } else {
            suppressAutoReconnect(1000);
        }
        enqueuePeerLine("BL0X");
    }
}

// Reconnection routine invoked from loop()
void attemptReconnectIfNeeded() {
  if (!bleNeedReconnect) return;
  if (blePausing) return;
  if (bleManualConnectInProgress) return;
  unsigned long now = millis();
  if (now < bleReconnectSuppressedUntil) return;
  if (now < nextReconnectAttempt) return;

  if (!lastAddress.length()) {
    Serial.println("attemptReconnectIfNeeded: no saved address; nothing to reconnect to.");
    cancelBLEReconnect();
    return;
  }

  if (!pClient) {
    Serial.println("attemptReconnectIfNeeded: reinitializing BLE client before reconnect");
    initBLEClient();
  }

  if (pClient->isConnected()) {
    if (hidReady) {
      cancelBLEReconnect();
      bleReconnectSuppressedUntil = 0;
      reconnectBackoff = RECONNECT_MIN_BACKOFF;
      reconnectFailures = 0;
      return;
    }
    Serial.println("BLE link is up but HID is not ready; rebuilding HID subscription");
    if (keyboardAuthService()) {
      Serial.println("HID setup recovered on existing BLE link");
      enqueuePeerLine("BL1X");
      cancelBLEReconnect();
      bleReconnectSuppressedUntil = 0;
      reconnectBackoff = RECONNECT_MIN_BACKOFF;
      reconnectFailures = 0;
      return;
    }
    hardResetBLEClient("reconnect existing link HID setup failed");
    bleReconnectSuppressedUntil = 0;
    scheduleBLEReconnect(reconnectBackoff + random(0, 100));
    reconnectBackoff = min(reconnectBackoff * 2, RECONNECT_MAX_BACKOFF);
    return;
  }

  lastBtConnectAttempt = now;
  bleManualConnectInProgress = true;

  bool attemptedConnection = false;
  bool savedDeviceSeen = false;
  bool ok = false;
  if (reconnectFailures == 0) {
    // Try once immediately after a real disconnect. If the keyboard has gone to
    // sleep, later passes scan first so we only connect while it is advertising.
    BLEAddress addr(lastAddress.c_str(), lastAddressType);
    attemptedConnection = true;
    Serial.printf("Direct reconnect to saved keyboard %s type %u\n",
                  lastAddress.c_str(), lastAddressType);
    ok = pClient->connect(addr, lastAddressType, BLE_RECONNECT_TIMEOUT_MS);
  } else {
    BLEAdvertisedDevice advertisedDevice;
    if (scanForSavedBLEDevice(advertisedDevice)) {
      savedDeviceSeen = true;
      attemptedConnection = true;
      lastAddressType = advertisedDevice.getAddressType();
      ok = pClient->connectTimeout(&advertisedDevice, BLE_RECONNECT_TIMEOUT_MS);
    } else {
      // Bonded keyboards may wake with directed advertisements that are not
      // returned in normal scan results. Keep trying the saved identity too.
      BLEAddress addr(lastAddress.c_str(), lastAddressType);
      attemptedConnection = true;
      Serial.printf("Trying saved keyboard after scan miss: %s type %u\n",
                    lastAddress.c_str(), lastAddressType);
      ok = pClient->connect(addr, lastAddressType, BLE_RECONNECT_TIMEOUT_MS);
    }
  }

  if (attemptedConnection && !ok && waitForLateBLEConnection(1500, "auto reconnect")) {
    ok = true;
  }

  if (!ok && savedDeviceSeen && recycleBLEStack("advertising keyboard rejected stale GATT client")) {
    BLEAddress freshAddress(lastAddress.c_str(), lastAddressType);
    Serial.printf("Retrying saved keyboard with fresh BLE stack: %s type %u\n",
                  lastAddress.c_str(), lastAddressType);
    attemptedConnection = true;
    ok = pClient->connect(freshAddress, lastAddressType, BLE_RECONNECT_TIMEOUT_MS);
    if (!ok && waitForLateBLEConnection(1500, "fresh-stack reconnect")) {
      ok = true;
    }
  }

  if (ok && keyboardAuthService()) {
    Serial.println("Auto-reconnect succeeded");
    bleManualConnectInProgress = false;
    enqueuePeerLine("BL1X");
    cancelBLEReconnect();
    bleReconnectSuppressedUntil = 0;
    reconnectBackoff = RECONNECT_MIN_BACKOFF;
    reconnectFailures = 0;
    saveAddress();
  } else {
    if (attemptedConnection) {
      Serial.println(ok ? "Auto-reconnect link opened but HID setup failed" : "Auto-reconnect failed");
      hardResetBLEClient(ok ? "auto reconnect HID setup failed" : "auto reconnect failed");
    }
    bleManualConnectInProgress = false;
    if (reconnectFailures < 255) reconnectFailures++;
    bleReconnectSuppressedUntil = 0;
    scheduleBLEReconnect(reconnectBackoff + random(0, 100));
    reconnectBackoff = min(reconnectBackoff * 2, RECONNECT_MAX_BACKOFF);
    Serial.printf("Next reconnect attempt in %lu ms\n", (unsigned long)(nextReconnectAttempt - millis()));
    // keep bleNeedReconnect true
  }
}

// --- HF / HTTP / JSON helpers (kept functionally same as your original). ---
// To keep the response readable I included them unchanged; they use enqueuePeerLine/sendHFOutput above
// (Make sure in your real code you include the rest of your helper functions exactly as you had them,
// I left them in-place earlier in the snippet you provided and I've preserved their behavior here.)

#if ENABLE_NETWORK_FEATURES
// --- start of helpers (copied from your version) ---
bool saveHttpResponseToFile(HTTPClient &http, const char *filepath) {
  WiFiClient *stream = http.getStreamPtr();
  if (!stream) return false;

  File f = SPIFFS.open(filepath, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open tmp file for writing");
    return false;
  }

  int contentLength = http.getSize();
  Serial.printf("Content-Length reported: %d\n", contentLength);

  const size_t BUF_SZ = 1024;
  uint8_t buf[BUF_SZ];

  if (contentLength > 0) {
    int remaining = contentLength;
    while (remaining > 0) {
      int toRead = remaining > (int)BUF_SZ ? BUF_SZ : remaining;
      int r = stream->read(buf, toRead);
      if (r <= 0) break;
      f.write(buf, r);
      remaining -= r;
    }
  } else {
    unsigned long t0 = millis();
    while (http.connected() || stream->available()) {
      int r = stream->read(buf, BUF_SZ);
      if (r > 0) {
        f.write(buf, r);
        t0 = millis();
      } else {
        delay(10);
        if (millis() - t0 > 5000) break;
      }
    }
  }

  f.close();
  return true;
}

String extractJsonStringValueFromFile(const char *filepath, const char *key) {
  File f = SPIFFS.open(filepath, FILE_READ);
  if (!f) return String();

  String keyStr = String("\"") + key + String("\"");
  const int CHUNK = 1024;
  char buf[CHUNK + 1];
  String window;
  while (f.available()) {
    int r = f.readBytes(buf, CHUNK);
    buf[r] = '\0';
    window += String(buf);
    if (window.length() > 65536) window = window.substring(window.length() - 65536);
    int pos = window.indexOf(keyStr);
    if (pos >= 0) {
      int colonPos = window.indexOf(':', pos + keyStr.length());
      if (colonPos >= 0) {
        int quotePos = window.indexOf('"', colonPos + 1);
        while (quotePos >= 0 && quotePos + 1 < window.length() && window.charAt(quotePos - 1) == '\\') {
          quotePos = window.indexOf('"', quotePos + 1);
        }
        if (quotePos >= 0) {
          String acc;
          int i = quotePos + 1;
          while (true) {
            if (i >= window.length()) {
              if (!f.available()) break;
              int rr = f.readBytes(buf, CHUNK);
              buf[rr] = '\0';
              window += String(buf);
              continue;
            }
            char c = window.charAt(i);
            if (c == '\\') {
              if (i + 1 < window.length()) {
                char next = window.charAt(i + 1);
                if (next == 'n') acc += '\n';
                else if (next == 'r') acc += '\r';
                else if (next == 't') acc += '\t';
                else acc += next;
                i += 2;
                continue;
              } else {
                if (!f.available()) break;
                int rr = f.readBytes(buf, CHUNK);
                buf[rr] = '\0';
                window += String(buf);
                continue;
              }
            }
            if (c == '"') {
              f.close();
              return acc;
            }
            acc += c;
            i++;
          }
        }
      }
    }
  }

  f.close();
  return String();
}

String extractTextFromFile(const char *filepath, bool useSpace, const String &endpoint) {
  File f = SPIFFS.open(filepath, FILE_READ);
  if (!f) return String();
  size_t sz = f.size();
  f.close();
  Serial.printf("Response saved to %s, size=%u\n", filepath, (unsigned)sz);

  const size_t PARSE_LIMIT = JSON_BUFFER_SIZE;
  if (sz > 0 && sz <= PARSE_LIMIT) {
    File f2 = SPIFFS.open(filepath, FILE_READ);
    if (!f2) return String();
    DynamicJsonDocument doc(PARSE_LIMIT);
    DeserializationError err = deserializeJson(doc, f2);
    f2.close();
    if (!err) {
      if (useSpace) {
        if (doc.containsKey("data")) {
          JsonVariant data = doc["data"];
          if (data.is<JsonArray>()) {
            JsonVariant first = data[0];
            if (first.is<const char*>()) return String(first.as<const char*>());
            else {
              String out; serializeJson(first, out); return out;
            }
          } else if (data.is<const char*>()) {
            return String(data.as<const char*>());
          } else {
            String out; serializeJson(data, out); return out;
          }
        }
      }

      if (String(endpoint).indexOf("/v1/chat/completions") >= 0) {
        if (doc.containsKey("choices") && doc["choices"].is<JsonArray>()) {
          JsonVariant choice = doc["choices"][0];
          if (!choice.isNull()) {
            if (choice.containsKey("message") && choice["message"].containsKey("content")) {
              JsonVariant content = choice["message"]["content"];
              if (content.is<const char*>()) return String(content.as<const char*>());
              else { String out; serializeJson(content, out); return out; }
            }
            if (choice.containsKey("text")) {
              JsonVariant t = choice["text"];
              if (t.is<const char*>()) return String(t.as<const char*>());
            }
          }
        }
      }

      if (doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() > 0) {
          JsonVariant first = arr[0];
          if (first.is<const char*>()) return String(first.as<const char*>());
          else if (first.is<JsonObject>()) {
            if (first.containsKey("generated_text")) {
              JsonVariant gt = first["generated_text"];
              if (gt.is<const char*>()) return String(gt.as<const char*>());
            }
            if (first.containsKey("text")) {
              JsonVariant tx = first["text"];
              if (tx.is<const char*>()) return String(tx.as<const char*>());
            }
            String out; serializeJson(first, out); return out;
          }
        }
      }

      if (doc.containsKey("generated_text")) {
        JsonVariant gt = doc["generated_text"];
        if (gt.is<const char*>()) return String(gt.as<const char*>());
      }

      String out; serializeJson(doc, out);
      return out;
    }
    Serial.printf("ArduinoJson parse failed: %s\n", err.c_str());
  }

  const char *keys[] = { "generated_text", "content", "text", "message", "data" };
  for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); ++i) {
    String v = extractJsonStringValueFromFile(filepath, keys[i]);
    if (v.length()) return v;
  }

  if (sz <= 200000) {
    File f3 = SPIFFS.open(filepath, FILE_READ);
    if (!f3) return String();
    String raw;
    raw.reserve(min((size_t)sz, (size_t)131072));
    while (f3.available()) raw += (char)f3.read();
    f3.close();
    return raw;
  }

  return String();
}

String extractTextFromJson(const String &body, bool useSpace, const String &endpoint) {
  DynamicJsonDocument doc(JSON_BUFFER_SIZE);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    return String();
  }

  if (useSpace) {
    if (doc.containsKey("data")) {
      JsonVariant data = doc["data"];
      if (data.is<JsonArray>()) {
        JsonVariant first = data[0];
        if (first.is<const char*>()) {
          return String(first.as<const char*>());
        } else {
          String out;
          serializeJson(first, out);
          return out;
        }
      } else if (data.is<const char*>()) {
        return String(data.as<const char*>());
      } else {
        String out;
        serializeJson(data, out);
        return out;
      }
    }
  }

  if (endpoint.indexOf("/v1/chat/completions") >= 0) {
    if (doc.containsKey("choices") && doc["choices"].is<JsonArray>()) {
      JsonVariant choice = doc["choices"][0];
      if (!choice.isNull()) {
        if (choice.containsKey("message") && choice["message"].containsKey("content")) {
          JsonVariant content = choice["message"]["content"];
          if (content.is<const char*>()) return String(content.as<const char*>());
          else {
            String out; serializeJson(content, out); return out;
          }
        }
        if (choice.containsKey("text")) {
          JsonVariant t = choice["text"];
          if (t.is<const char*>()) return String(t.as<const char*>());
        }
      }
    }
  }

  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() > 0) {
      JsonVariant first = arr[0];
      if (first.is<const char*>()) {
        return String(first.as<const char*>());
      } else if (first.is<JsonObject>()) {
        if (first.containsKey("generated_text")) {
          JsonVariant gt = first["generated_text"];
          if (gt.is<const char*>()) return String(gt.as<const char*>());
        }
        if (first.containsKey("text")) {
          JsonVariant tx = first["text"];
          if (tx.is<const char*>()) return String(tx.as<const char*>());
        }
        String out; serializeJson(first, out); return out;
      }
    }
  }

  if (doc.containsKey("generated_text")) {
    JsonVariant gt = doc["generated_text"];
    if (gt.is<const char*>()) return String(gt.as<const char*>());
  }

  String out;
  serializeJson(doc, out);
  return out;
}

int freeAskHF(const String &prompt, String &outExtracted) {
  outExtracted = "";

  if (!HF_TOKEN.length() && !HF_SPACE_HOST.length()) {
    Serial.println("HF token required. Configure one in CitBrowser AI Parameters.");
    return -3;
  }

  // Any path beyond this point starts Wi-Fi/TLS. Always recover the lean
  // BLE/CVBS runtime with a clean restart once the response has been sent.
  restartAfterNetworkTransaction = true;

  if (bleRuntimeEnabled || pBLEScan || pClient) {
    pauseBLEForTLS();
  }

  if (!wifiConnected && !connectWiFiAuto()) {
    Serial.println("Wi-Fi not configured/connected");
    return -1;
  }

  if (time(nullptr) < 1600000000) {
    Serial.println("Time invalid before HF call, syncing NTP...");
    syncTime(15000);
    if (time(nullptr) < 1600000000) {
      Serial.println("Time not set - TLS will likely fail.");
      return -2;
    }
  }

  uint32_t heapBefore = ESP.getFreeHeap();
  Serial.printf("Free heap before HF: %u\n", heapBefore);
  if (heapBefore < 120000) {
    Serial.println("WARNING: Free heap is low (<120KB). TLS handshake may fail. Consider using NimBLE or reducing memory usage.");
  }

  bool useSpace = HF_SPACE_HOST.length();
  String endpoint;
  String payload;
  String esc = prompt;
  esc.replace("\\", "\\\\");
  esc.replace("\"", "\\\"");

  const String ROUTER_CHAT = "https://router.huggingface.co/v1/chat/completions";
  const String ROUTER_HF_INFERENCE_BASE = "https://router.huggingface.co/hf-inference/models/";

  auto buildSpace = [&]() {
    endpoint = "https://" + HF_SPACE_HOST + "/run/predict";
    payload = "{\"data\":[\"" + esc + "\"]}";
    useSpace = true;
  };
  auto buildRouterChat = [&]() {
    endpoint = ROUTER_CHAT;
    payload =
      "{"
        "\"model\":\"" + HF_MODEL + "\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"" + esc + "\"}],"
        "\"max_tokens\":384,"
        "\"stream\":false"
      "}";
    useSpace = false;
  };
  auto buildModelInference = [&]() {
    endpoint = ROUTER_HF_INFERENCE_BASE + HF_MODEL;
    payload = "{\"inputs\":\"" + esc + "\",\"parameters\":{\"max_new_tokens\":2048}}";
    useSpace = false;
  };

  if (useSpace) buildSpace(); else buildRouterChat();

  const int maxRetries = 6;
  unsigned long backoffMs = 1500;
  const unsigned long maxBackoff = 60000UL;

  const char *TMP_PATH = "/hf_response.tmp";

  for (int attempt = 0; attempt < maxRetries; ++attempt) {
    bool didPauseBLE = false;
    if (pBLEScan || pClient) {
      pauseBLEForTLS();
      didPauseBLE = true;
    }

    delay(120);

    bool netOk = false;
    for (int dnsTry = 0; dnsTry < 6; ++dnsTry) {
      if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip;
        String host = extractHostFromEndpoint(endpoint);
        if (WiFi.hostByName(host.c_str(), ip)) {
          netOk = true;
          break;
        }
      }
      if (WiFi.status() != WL_CONNECTED && savedSSID.length()) {
        WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
      }
      delay(250);
    }

    if (!netOk) {
      Serial.println("Network/DNS not ready after BLE pause; restoring BLE and aborting this attempt.");
      if (didPauseBLE) resumeBLEAfterTLS();
      return -1;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    bool began = http.begin(client, endpoint);
    if (!began) {
      Serial.println("http.begin(client, endpoint) failed — host/path fallback");
      String host = extractHostFromEndpoint(endpoint);
      String path = "/";
      int hostPos = endpoint.indexOf(host);
      if (hostPos >= 0) {
        int pathStart = hostPos + host.length();
        if (pathStart < endpoint.length()) path = endpoint.substring(pathStart);
        if (!path.startsWith("/")) path = "/" + path;
      }
      if (!http.begin(client, host.c_str(), 443, path.c_str(), true)) {
        Serial.println("http.begin(host,443,path,true) also failed");
        IPAddress ip;
        if (WiFi.hostByName(host.c_str(), ip)) {
          Serial1.printf("DNS: %s -> %s\n", host.c_str(), ip.toString().c_str());
        } else {
          Serial1.printf("DNS lookup failed for %s\n", host.c_str());
        }
        if (didPauseBLE) resumeBLEAfterTLS();
        return -1;
      }
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "keep-alive");
    http.addHeader("User-Agent", "ESP32-Citadel/1.0");
    if (HF_TOKEN.length()) http.addHeader("Authorization", "Bearer " + HF_TOKEN);
    http.setTimeout(180000);

    Serial.printf("HF request attempt %d -> %s\n", attempt + 1, endpoint.c_str());
    int httpCode = http.POST(payload);
    Serial.printf("HTTP code: %d\n", httpCode);

    if (httpCode > 0 && httpCode >= 200 && httpCode < 300) {
      if (!saveHttpResponseToFile(http, TMP_PATH)) {
        Serial.println("Failed to save response to file");
        http.end();
        if (didPauseBLE) resumeBLEAfterTLS();
        return -1;
      }

      http.end();

      if (didPauseBLE) resumeBLEAfterTLS();

      String extracted = extractTextFromFile(TMP_PATH, useSpace, endpoint);
      if (extracted.length()) {
        extracted.replace("\\n", "\n");
        extracted.replace("\\r", "\r");
        extracted.replace("\\\"", "\"");
        outExtracted = extracted;
        SPIFFS.remove(TMP_PATH);
        return 0;
      } else {
        String raw = extractTextFromFile(TMP_PATH, useSpace, endpoint);
        outExtracted = raw;
        SPIFFS.remove(TMP_PATH);
        return 0;
      }
    }

    String bodyPreview;
    saveHttpResponseToFile(http, TMP_PATH);
    File dbg = SPIFFS.open(TMP_PATH, FILE_READ);
    if (dbg) {
      bodyPreview = dbg.readString();
      dbg.close();
      SPIFFS.remove(TMP_PATH);
    }

    if (didPauseBLE) resumeBLEAfterTLS();

    if (httpCode == 503 || httpCode == 429) {
      if (attempt < maxRetries - 1) {
        unsigned long waitMs = backoffMs + (random(0, 800));
        char msg[64];
        snprintf(msg, sizeof(msg), "Transient HF (%d). Retrying %lus...", httpCode, waitMs / 1000);
        Serial.println(msg);
        delay(waitMs);
        backoffMs = min(backoffMs * 2, maxBackoff);
        continue;
      } else {
        Serial.println(String("HF UNAVAILABLE after retries.\n") + bodyPreview);
        return httpCode;
      }
    }

    if (httpCode == 401 || httpCode == 403) {
      Serial.println("HF AUTH ERROR\nCheck token or use public space.");
      return httpCode;
    }

    if (httpCode <= 0) {
      Serial.println("HTTP ERROR (no response).");
      String host = extractHostFromEndpoint(endpoint);
      IPAddress ip;
      if (WiFi.hostByName(host.c_str(), ip)) {
        Serial.println(String("DNS: ") + host + " -> " + ip.toString());
      } else {
        Serial.println(String("DNS lookup failed for ") + host);
      }
      return httpCode;
    }

    Serial.println(String("HF ERROR ") + String(httpCode) + "\n\n" + bodyPreview);
    return httpCode;
  }

  Serial.println("HF FAILED\nUnknown error.");
  return -1;
}
// --- end of helpers ---


void sendStatusOverSerial1() {
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  enqueuePeerLine(String("WIFI connected: ") + (wifiConnected ? "yes" : "no"));
  if (wifiConnected) enqueuePeerLine(String("IP: ") + WiFi.localIP().toString());
  enqueuePeerLine(String("HF space: ") + HF_SPACE_HOST.c_str());
  enqueuePeerLine(String("HF model: ") + HF_MODEL.c_str());
  enqueuePeerLine(String("HF token stored: ") + (HF_TOKEN.length() ? "yes" : "no"));
  enqueuePeerLine(String("Free heap: ") + String(ESP.getFreeHeap()));
}

void handleCBCommand(const String &cmdRaw, bool fromSerial1) {
  String cmd = cmdRaw;
  cmd.trim();

  if (cmd == "CBWR0") {
    String out = savedSSID.length() ? savedSSID : "<not set>";
    enqueuePeerLine(String("CBWR0 ") + out);
    return;
  }
  if (cmd == "CBWR1") {
    String out = savedPASS.length() ? savedPASS : "<not set>";
    enqueuePeerLine(String("CBWR1 ") + out);
    return;
  }
  if (cmd == "CBWR2") {
    if (WiFi.status() == WL_CONNECTED) {
      enqueuePeerLine(String("CBWR2 ") + WiFi.localIP().toString());
    } else {
      enqueuePeerLine("CBWR2 NO_IP");
    }
    return;
  }
  if (cmd == "CBHFRM") {
    String out = HF_MODEL.length() ? HF_MODEL : "<not set>";
    enqueuePeerLine(String("CBHFRM ") + out);
    return;
  }
  if (cmd == "CBHFRT") {
    String out = HF_TOKEN.length() ? HF_TOKEN : "<not set>";
    enqueuePeerLine(String("CBHFRT ") + out);
    return;
  }
  if (cmd == "CBHFRS") {
    String out = HF_SPACE_HOST.length() ? HF_SPACE_HOST : "<not set>";
    enqueuePeerLine(String("CBHFRS ") + out);
    return;
  }
  if (cmd == "CBTIME") {
    String t = getCurrentTimeString("%Y-%m-%d %H:%M:%S %z");
    enqueuePeerLine(String("CBTIME ") + t);
    return;
  }

  if (cmd.startsWith("CBW0 ")) {
    savedSSID = cmd.substring(5);
    savedSSID.trim();
    saveFile(WIFI_SSID_PATH, savedSSID);
    Serial.printf("Saved SSID: %s\n", savedSSID.c_str());
    if (fromSerial1) enqueuePeerLine("CBW0 OK");
    else Serial.println("CBW0 OK");
    return;
  }

  if (cmd.startsWith("CBW1 ")) {
    savedPASS = cmd.substring(5);
    savedPASS.trim();
    saveFile(WIFI_PASS_PATH, savedPASS);
    Serial.println("Saved WiFi PASS");
    if (fromSerial1) enqueuePeerLine("CBW1 OK");
    return;
  }

  if (cmd == "CBW1C") {
    savedPASS = "";
    SPIFFS.remove(WIFI_PASS_PATH);
    Serial.println("Saved WiFi password cleared.");
    if (fromSerial1) enqueuePeerLine("CBW1 OK");
    return;
  }

  if (cmd == "CBW2") {
    if (!savedSSID.length()) {
      Serial.println("CBW2 NO_SSID");
      if (fromSerial1) enqueuePeerLine("CBW2 NO_SSID");
      return;
    }
    Serial.println("CBW2: attempting to connect to saved WiFi...");
    bool ok = connectWiFiAuto();
    if (ok) {
      Serial.printf("CBW2 OK - IP: %s\n", WiFi.localIP().toString().c_str());
      if (fromSerial1) enqueuePeerLine(String("CBW2 OK IP ") + WiFi.localIP().toString());
      else enqueuePeerLine(String("CBW2 OK IP ") + WiFi.localIP().toString());
    } else {
      Serial.println("CBW2 FAIL");
      if (fromSerial1) enqueuePeerLine("CBW2 FAIL");
    }
    return;
  }

  if (cmd.startsWith("CBHFT")) {
    String rest = cmd.substring(5);
    rest.trim();
    if (rest == "clear") {
      HF_TOKEN = "";
      SPIFFS.remove(HF_TOKEN_PATH);
      Serial.println("HF token cleared.");
      if (fromSerial1) enqueuePeerLine("CBHFT CLEARED");
      return;
    } else if (rest == "status") {
      Serial.printf("HF token stored: %s\n", HF_TOKEN.length() ? "yes" : "no");
      if (fromSerial1) enqueuePeerLine(String("CBHFT STATUS ") + (HF_TOKEN.length() ? "yes" : "no"));
      return;
    } else if (rest.startsWith("set ")) {
      HF_TOKEN = rest.substring(4);
      HF_TOKEN.trim();
      saveFile(HF_TOKEN_PATH, HF_TOKEN);
      Serial.println("HF token stored.");
      if (fromSerial1) enqueuePeerLine("CBHFT OK");
      return;
    } else if (rest.length() > 0) {
      HF_TOKEN = rest;
      HF_TOKEN.trim();
      saveFile(HF_TOKEN_PATH, HF_TOKEN);
      Serial.println("HF token stored.");
      if (fromSerial1) enqueuePeerLine("CBHFT OK");
      return;
    } else {
      Serial.printf("HF token stored: %s\n", HF_TOKEN.length() ? "yes" : "no");
      if (fromSerial1) enqueuePeerLine(String("CBHFT STATUS ") + (HF_TOKEN.length() ? "yes" : "no"));
      return;
    }
  }

  if (cmd.startsWith("CBHFM ")) {
    HF_MODEL = cmd.substring(6);
    HF_MODEL.trim();
    saveFile(HF_MODEL_PATH, HF_MODEL);
    Serial.printf("HF model set: %s\n", HF_MODEL.c_str());
    if (fromSerial1) enqueuePeerLine("CBHFM OK");
    return;
  }

  if (cmd == "CBHFSC") {
    HF_SPACE_HOST = "";
    SPIFFS.remove(HF_SPACE_PATH);
    Serial.println("HF space host cleared.");
    if (fromSerial1) enqueuePeerLine("CBHFSC OK");
    return;
  }

  if (cmd.startsWith("CBHFS ")) {
    HF_SPACE_HOST = cmd.substring(6);
    HF_SPACE_HOST.trim();
    HF_SPACE_HOST = sanitizeHost(HF_SPACE_HOST);
    saveFile(HF_SPACE_PATH, HF_SPACE_HOST);
    Serial.printf("HF space host set to: %s\n", HF_SPACE_HOST.c_str());
    if (fromSerial1) enqueuePeerLine("CBHFS OK");
    return;
  }

  if (cmd == "CBWSC") {
    Serial.println("Scanning WiFi...");
    const bool wifiWasEnabled = WiFi.getMode() != WIFI_MODE_NULL;
    WiFi.persistent(false);
    if (WiFi.getMode() == WIFI_MODE_NULL) {
      if (bleRuntimeEnabled || pClient || pBLEScan) {
        Serial.println("Pausing BLE to free heap for WiFi scan...");
        stopBLERuntimeForVideo();
        delay(120);
      }
      if (!WiFi.mode(WIFI_STA)) {
        Serial.printf("WiFi station mode failed: free heap=%u\n", ESP.getFreeHeap());
        enqueuePeerLine("CBWSC FAIL MODE");
        return;
      }
      delay(120);
    }
    int n = WiFi.scanNetworks();
    if (n < 0) {
      Serial.printf("WiFi scan failed: %d\n", n);
      enqueuePeerLine(String("CBWSC FAIL ") + String(n));
      return;
    }
    for (int i = 0; i < n; i++) {
      enqueuePeerLine(String(i + 1) + ": " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + ")");
    }
    WiFi.scanDelete();
    if (!wifiWasEnabled) {
      WiFi.disconnect(true, false);
      wifiConnected = false;
      blePausedUntilMs = millis() + 250;
      nextBLEEnableTryMs = 0;
    }
    if (fromSerial1) enqueuePeerLine("CBWSC OK");
    return;
  }

  if (cmd == "CBWS") {
    sendStatusOverSerial1();
    return;
  }

  if (cmd.startsWith("CBHFA ")) {
    restartAfterNetworkTransaction = false;
    String prompt = cmd.substring(6);
    prompt.trim();
    Serial.printf("Sending to HF: %s\n", prompt.c_str());
    sendPeerLineImmediate("CLEARVDD");
    sendPeerLineImmediate("CBHFR");
    flushPeerImmediate();
    String answer;
    int rc = freeAskHF(prompt, answer);
    if (rc == 0 && answer.length()) {
      sendPeerLineImmediate("CBHFR");
      sendPeerMultiLineImmediate(answer);
      sendPeerLineImmediate("CBHFT");
      flushPeerImmediate();
      Serial.println("HF -> Sent to Serial1");
    } else {
      sendPeerLineImmediate(rc == -3 ? String("HF ERROR TOKEN REQUIRED")
                                     : String("HF ERROR ") + String(rc));
      flushPeerImmediate();
    }
    if (restartAfterNetworkTransaction) {
      Serial.println("Network transaction complete; restarting controller to restore BLE cleanly.");
      Serial.flush();
      delay(80);
      ESP.restart();
    }
    return;
  }

  if (fromSerial1) enqueuePeerLine(String("UNKNOWN CMD: ") + cmd.c_str());
  else Serial.printf("UNKNOWN CMD: %s\n", cmd.c_str());
}
#endif

void handleSerialCommand(String command, bool fromSerial1) {
    command.trim();
    if (!command.length()) return;

    if (handleControllerTimeCommand(command, fromSerial1)) {
        return;
    }

    if (handleFanControlCommand(command, fromSerial1)) {
        return;
    }

    if (handleVideoProgressCommand(command, fromSerial1)) {
        return;
    }

    if (handleCVBSFailoverCommand(command, fromSerial1)) {
        return;
    }

    if (command == "BLE00") {
        if (fromSerial1) enqueuePeerLine("BL5X");
        else Serial.println("BLEX");
        enqueueBLEWork("SCAN", fromSerial1);
        return;
    }

    if (command == "BLESCAN") {
        if (fromSerial1) enqueuePeerLine("BL5X");
        else Serial.println("BLEX");
        enqueueBLEWork("SCAN", fromSerial1);
        return;
    }

    if (command == "BLER" || command == "BLEAUTO" || command == "BLERECONNECT") {
        enqueueBLEWork("RECONNECT", fromSerial1);
        return;
    }

    if (command.startsWith("BLE0") && command.length() > 4) {
        int index = command.substring(4).toInt();
        enqueueBLEWork(String("CONNECT ") + String(index), fromSerial1);
        return;
    }

    if (command == "BLEC" || command == "BLECLR" || command == "BLECLEAR") {
        enqueueBLEWork("CLEAR", fromSerial1);
        return;
    }

    if (fromSerial1 && command == "res") {
        cvbsForceFallback = false;
        setCVBSFailoverDriving(false);
        Serial.println("App requested SerialController restart; failover video released first.");
        Serial.flush();
        delay(25);
        ESP.restart();
        return;
    }

    if (fromSerial1 && command == "VDCLoadNSS") {
        boolRes = "VDCLoadNSS";
        boolUpdate();
        ESP.restart();
        return;
    }

    if (fromSerial1 && command == "AudioSwing") {
        boolRes = "AudioSwing";
        boolUpdate();
        ESP.restart();
        return;
    }

    if (command.startsWith("CB")) {
#if ENABLE_NETWORK_FEATURES
        handleCBCommand(command, fromSerial1);
#else
        if (fromSerial1) enqueuePeerLine("CB DISABLED");
        else Serial.println("CB commands disabled in Bluetooth-fast build");
#endif
        return;
    }
}

static void pumpSerialLine(Stream &port, Citadela::LineReader &reader, bool fromSerial1) {
    String command;
    while (reader.poll(port, command)) {
        handleSerialCommand(command, fromSerial1);
    }
}

void checkSerialCommand() {
    static Citadela::LineReader serialReader(SERIAL_COMMAND_MAX);
    static Citadela::LineReader serial1Reader(SERIAL_COMMAND_MAX);
    if (displayRelay.running()) {
        pumpSerialLine(displayRelay.pcInput, serialReader, false);
        pumpSerialLine(displayRelay.peerInput, serial1Reader, true);
    } else {
        pumpSerialLine(Serial, serialReader, false);
        pumpSerialLine(Serial1, serial1Reader, true);
    }
}

#if ENABLE_NETWORK_FEATURES
void loadHFConfig() {
  HF_TOKEN = loadFile(HF_TOKEN_PATH);
  HF_MODEL = loadFile(HF_MODEL_PATH);
  HF_SPACE_HOST = sanitizeHost(loadFile(HF_SPACE_PATH));
  savedSSID = loadFile(WIFI_SSID_PATH);
  savedPASS = loadFile(WIFI_PASS_PATH);
  if (!HF_MODEL.length() || HF_MODEL == "gpt2" ||
      HF_MODEL == "openai-community/gpt2" ||
      HF_MODEL == "penai-community/gpt2") {
    HF_MODEL = DEFAULT_HF_MODEL;
    saveFile(HF_MODEL_PATH, HF_MODEL);
  }
}

void pauseBLEForTLS() {
  Serial.println("Pausing BLE to free heap for TLS...");
  blePausing = true;
  bleRuntimeEnabled = false;
  restartAfterNetworkTransaction = true;

  if (pBLEScan) {
    pBLEScan->stop();
  }

  if (pClient) {
    pClient->setClientCallbacks(nullptr);
    if (pClient->isConnected()) {
      pClient->disconnect();
      delay(10);
    }
  }

  BLEDevice::deinit(false);
  pClient = nullptr;
  pBLEScan = nullptr;
  pHIDService = nullptr;
  pInputReportChar = nullptr;
  pTrackpadChar = nullptr;
  inputReportChars.clear();
  inputNotifyRegistered = false;
  hidReady = false;
  delay(60);

  Serial.printf("Heap after BLE pause: %u\n", ESP.getFreeHeap());
}

void resumeBLEAfterTLS() {
  restartAfterNetworkTransaction = true;
  Serial.println("BLE restore scheduled through clean controller restart after network reply.");
}
#endif

static void processBLEWork(const String &work, bool fromSerial1) {
    if (work == "SCAN") {
        scanDevices(true, false);
        return;
    }

    if (work == "RECONNECT") {
        if (!lastAddress.length()) {
            if (fromSerial1) enqueuePeerLine("BLEX");
            else Serial.println("No saved BLE address");
            return;
        }
        BLEAddress saved(lastAddress.c_str(), lastAddressType);
        if (!connectToAddress(saved, lastAddressType)) {
            if (fromSerial1) enqueuePeerLine("BL0X");
            else Serial.println("Saved BLE reconnect failed");
        }
        return;
    }

    if (work.startsWith("CONNECT ")) {
        int index = work.substring(8).toInt();
        connectToDevice(index);
        return;
    }

    if (work == "CLEAR") {
        clearSavedBLEDevice(fromSerial1);
        return;
    }
}

static void cvbsEmitStatus(bool fromSerial1) {
    String msg = String("CVBS enabled=") + (cvbsFailoverEnabled ? "1" : "0") +
                 " driving=" + (cvbsFailoverDriving ? "1" : "0") +
                 " ready=" + (cvbsFallbackVideoReady ? "1" : "0") +
                 " failed=" + (cvbsFallbackInitFailed ? "1" : "0") +
                 " ble=" + (bleRuntimeEnabled ? "1" : "0") +
                 " force=" + (cvbsForceFallback ? "1" : "0") +
                 " kernel=" + (cvbsKernelSignalPresent ? "1" : "0") +
                 " min=" + String((uint16_t)cvbsLastMin) +
                 " max=" + String((uint16_t)cvbsLastMax) +
                 " span=" + String((uint16_t)cvbsLastSpan) +
                 " driveMs=" + (cvbsFailoverDriving ? String(millis() - cvbsDriveStartedMs) : String(0));
    if (fromSerial1) enqueuePeerLine(msg);
    else Serial.println(msg);
}

static void cvbsListenMode() {
    if (cvbsFallbackVideoReady) cvbsFallbackVideo.i2sStop();
    dac_output_disable(CVBS_FAILOVER_PIN == 25 ? DAC_CHANNEL_1 : DAC_CHANNEL_2);
    pinMode(CVBS_FAILOVER_PIN, INPUT_PULLDOWN);
}

static void cvbsPrintHeap(const char *stage) {
    Serial.printf("CVBS %s: free=%u largest=%u dmaFree=%u dmaLargest=%u\n",
                  stage,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

static bool cvbsEnsureHardwareVideo(bool allowBLEPause) {
    bool waitForInit = false;
    portENTER_CRITICAL(&cvbsInitMux);
    if (cvbsFallbackInitFailed) {
        unsigned long failedAt = cvbsFallbackInitFailedAtMs;
        if (millis() - failedAt < CVBS_INIT_RETRY_MS) {
            portEXIT_CRITICAL(&cvbsInitMux);
            return false;
        }
        cvbsFallbackInitFailed = false;
    }
    if (cvbsFallbackVideoReady) {
        portEXIT_CRITICAL(&cvbsInitMux);
        return true;
    }
    if (cvbsFallbackInitInProgress) {
        waitForInit = true;
    } else {
        cvbsFallbackInitInProgress = true;
    }
    portEXIT_CRITICAL(&cvbsInitMux);

    if (waitForInit) {
        unsigned long waitStart = millis();
        while (cvbsFallbackInitInProgress && millis() - waitStart < 2500) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        return cvbsFallbackVideoReady;
    }

    auto tryInitSmall = []() -> bool {
        bool ok = false;
        try {
            ok = cvbsFallbackVideo.init(CompMode::MODEPALQuarter144P, CVBS_FAILOVER_PIN, true);
        } catch (...) {
            Serial.printf("CVBS %s init threw from bitluni allocator\n", CVBS_FAILOVER_MODE_NAME);
            ok = false;
        }
        return ok;
    };

    auto hasInitMemory = []() -> bool {
        return heap_caps_get_free_size(MALLOC_CAP_DMA) >= CVBS_INIT_MIN_DMA_FREE &&
               heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >= CVBS_INIT_MIN_DMA_LARGEST;
    };

    cvbsPrintHeap("failover init before MODEPALQuarter144P");
    if (!hasInitMemory() && allowBLEPause && (bleRuntimeEnabled || pClient || pBLEScan)) {
        Serial.println("CVBS init needs DMA heap; pausing BLE before entering bitluni allocator.");
        stopBLERuntimeForVideo();
        delay(120);
        cvbsPrintHeap("failover init after BLE pause");
    }

    if (!hasInitMemory()) {
        Serial.println("CVBS failover init deferred: DMA heap is below the safe allocation threshold");
        portENTER_CRITICAL(&cvbsInitMux);
        cvbsFallbackInitFailed = true;
        cvbsFallbackInitFailedAtMs = millis();
        cvbsFallbackInitInProgress = false;
        portEXIT_CRITICAL(&cvbsInitMux);
        cvbsListenMode();
        return false;
    }

    bool ok = tryInitSmall();
    if (!ok) {
        Serial.println("CVBS failover init failed");
        portENTER_CRITICAL(&cvbsInitMux);
        cvbsFallbackInitFailed = true;
        cvbsFallbackInitFailedAtMs = millis();
        cvbsFallbackInitInProgress = false;
        portEXIT_CRITICAL(&cvbsInitMux);
        cvbsListenMode();
        return false;
    }

    cvbsFallbackVideo.clear(0);
    cvbsFallbackVideo.show(false);
    cvbsSplashStaticDrawn = false;
    cvbsSplashStaticLabel = "";
    cvbsSplashLastPercent = -1;
    cvbsSplashLastFillW = 0;
    cvbsFallbackVideoReady = true;
    cvbsListenMode();
    portENTER_CRITICAL(&cvbsInitMux);
    cvbsFallbackInitInProgress = false;
    portEXIT_CRITICAL(&cvbsInitMux);
    Serial.printf("CVBS failover mode %s ready: %dx%d\n",
                  CVBS_FAILOVER_MODE_NAME,
                  cvbsFallbackVideo.xres,
                  cvbsFallbackVideo.yres);
    cvbsPrintHeap("failover init OK");
    return true;
}

static bool cvbsDriveMode() {
    if (!cvbsEnsureHardwareVideo(cvbsForceFallback || cvbsFetchModeActive)) return false;
    cvbsFallbackVideo.enableDAC(CVBS_FAILOVER_PIN == 25 ? 1 : 2);
    cvbsFallbackVideo.startTX();
    return true;
}

static String cvbsFitBootLabel(const String &label) {
    String out = label;
    out.trim();
    if (!out.length()) out = "Preparing Citadela";
    if (out.length() > 22) out = out.substring(0, 22);
    return out;
}

static uint16_t cvbsLogoDrawHeight() {
    return (CITADELA_SPLASH_LOGO_H + CVBS_SPLASH_LOGO_Y_DIV - 1) / CVBS_SPLASH_LOGO_Y_DIV;
}

#define CVBS_TINY_GLYPH(a, b, c, d, e) \
    ((uint16_t)((((a) & 7) << 12) | (((b) & 7) << 9) | (((c) & 7) << 6) | (((d) & 7) << 3) | ((e) & 7)))

static uint16_t cvbsTinyGlyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    switch (ch) {
        case '0': return CVBS_TINY_GLYPH(7, 5, 5, 5, 7);
        case '1': return CVBS_TINY_GLYPH(2, 6, 2, 2, 7);
        case '2': return CVBS_TINY_GLYPH(7, 1, 7, 4, 7);
        case '3': return CVBS_TINY_GLYPH(7, 1, 7, 1, 7);
        case '4': return CVBS_TINY_GLYPH(5, 5, 7, 1, 1);
        case '5': return CVBS_TINY_GLYPH(7, 4, 7, 1, 7);
        case '6': return CVBS_TINY_GLYPH(7, 4, 7, 5, 7);
        case '7': return CVBS_TINY_GLYPH(7, 1, 2, 2, 2);
        case '8': return CVBS_TINY_GLYPH(7, 5, 7, 5, 7);
        case '9': return CVBS_TINY_GLYPH(7, 5, 7, 1, 7);
        case 'A': return CVBS_TINY_GLYPH(2, 5, 7, 5, 5);
        case 'B': return CVBS_TINY_GLYPH(6, 5, 6, 5, 6);
        case 'C': return CVBS_TINY_GLYPH(7, 4, 4, 4, 7);
        case 'D': return CVBS_TINY_GLYPH(6, 5, 5, 5, 6);
        case 'E': return CVBS_TINY_GLYPH(7, 4, 6, 4, 7);
        case 'F': return CVBS_TINY_GLYPH(7, 4, 6, 4, 4);
        case 'G': return CVBS_TINY_GLYPH(7, 4, 5, 5, 7);
        case 'H': return CVBS_TINY_GLYPH(5, 5, 7, 5, 5);
        case 'I': return CVBS_TINY_GLYPH(7, 2, 2, 2, 7);
        case 'J': return CVBS_TINY_GLYPH(1, 1, 1, 5, 7);
        case 'K': return CVBS_TINY_GLYPH(5, 5, 6, 5, 5);
        case 'L': return CVBS_TINY_GLYPH(4, 4, 4, 4, 7);
        case 'M': return CVBS_TINY_GLYPH(5, 7, 7, 5, 5);
        case 'N': return CVBS_TINY_GLYPH(5, 7, 7, 7, 5);
        case 'O': return CVBS_TINY_GLYPH(7, 5, 5, 5, 7);
        case 'P': return CVBS_TINY_GLYPH(7, 5, 7, 4, 4);
        case 'Q': return CVBS_TINY_GLYPH(7, 5, 5, 7, 1);
        case 'R': return CVBS_TINY_GLYPH(6, 5, 6, 5, 5);
        case 'S': return CVBS_TINY_GLYPH(7, 4, 7, 1, 7);
        case 'T': return CVBS_TINY_GLYPH(7, 2, 2, 2, 2);
        case 'U': return CVBS_TINY_GLYPH(5, 5, 5, 5, 7);
        case 'V': return CVBS_TINY_GLYPH(5, 5, 5, 5, 2);
        case 'W': return CVBS_TINY_GLYPH(5, 5, 7, 7, 5);
        case 'X': return CVBS_TINY_GLYPH(5, 5, 2, 5, 5);
        case 'Y': return CVBS_TINY_GLYPH(5, 5, 2, 2, 2);
        case 'Z': return CVBS_TINY_GLYPH(7, 1, 2, 4, 7);
        case '-': return CVBS_TINY_GLYPH(0, 0, 7, 0, 0);
        case '_': return CVBS_TINY_GLYPH(0, 0, 0, 0, 7);
        case ':': return CVBS_TINY_GLYPH(0, 2, 0, 2, 0);
        case '.': return CVBS_TINY_GLYPH(0, 0, 0, 0, 2);
        case '/': return CVBS_TINY_GLYPH(1, 1, 2, 4, 4);
        case '%': return CVBS_TINY_GLYPH(5, 1, 2, 4, 5);
        case ' ': return CVBS_TINY_GLYPH(0, 0, 0, 0, 0);
        default: return CVBS_TINY_GLYPH(7, 1, 2, 0, 2);
    }
}

static int cvbsTinyTextWidth(const String &text) {
    if (!text.length()) return 0;
    return (int)text.length() * (CVBS_SPLASH_TEXT_W + CVBS_SPLASH_TEXT_SPACING) - CVBS_SPLASH_TEXT_SPACING;
}

static void cvbsDrawTinyText(const String &text, int x, int y, uint8_t color) {
    const int screenW = cvbsFallbackVideo.xres > 0 ? cvbsFallbackVideo.xres : CVBS_SPLASH_W;
    const int screenH = cvbsFallbackVideo.yres > 0 ? cvbsFallbackVideo.yres : CVBS_SPLASH_H;
    for (int i = 0; i < text.length(); ++i) {
        uint16_t glyph = cvbsTinyGlyph(text.charAt(i));
        int charX = x + i * (CVBS_SPLASH_TEXT_W + CVBS_SPLASH_TEXT_SPACING);
        for (uint8_t row = 0; row < CVBS_SPLASH_TEXT_H; ++row) {
            uint8_t bits = (glyph >> ((CVBS_SPLASH_TEXT_H - 1 - row) * CVBS_SPLASH_TEXT_W)) & 0x07;
            for (uint8_t col = 0; col < CVBS_SPLASH_TEXT_W; ++col) {
                if (!(bits & (1 << (CVBS_SPLASH_TEXT_W - 1 - col)))) continue;
                int px = charX + col;
                int py = y + row;
                if ((unsigned int)px < (unsigned int)screenW && (unsigned int)py < (unsigned int)screenH) {
                    cvbsFallbackVideo.dotFast(px, py, color);
                }
            }
        }
    }
}

static void cvbsUpdateSplashLayout() {
    const int screenW = cvbsFallbackVideo.xres > 0 ? cvbsFallbackVideo.xres : CVBS_SPLASH_W;
    const int screenH = cvbsFallbackVideo.yres > 0 ? cvbsFallbackVideo.yres : CVBS_SPLASH_H;
    const int logoH = cvbsLogoDrawHeight();
    const int totalH = logoH + 7 + 8 + 5 + CVBS_SPLASH_TEXT_H + 4 + CVBS_SPLASH_TEXT_H;
    cvbsSplashLogoX = max(0, (screenW - (int)CITADELA_SPLASH_LOGO_W) / 2);
    cvbsSplashLogoY = max(2, (screenH - totalH) / 2);
    cvbsSplashBarW = min(80, max(24, screenW - 16));
    cvbsSplashBarX = max(0, (screenW - cvbsSplashBarW) / 2);
    cvbsSplashBarH = 8;
    cvbsSplashBarY = constrain(cvbsSplashLogoY + logoH + 7,
                               0,
                               max(0, screenH - cvbsSplashBarH));
    cvbsSplashTextY = constrain(cvbsSplashBarY + cvbsSplashBarH + 5,
                                0,
                                max(0, screenH - CVBS_SPLASH_TEXT_H));
    cvbsSplashPercentBoxW = 22;
    cvbsSplashPercentBoxH = CVBS_SPLASH_TEXT_H;
    cvbsSplashPercentBoxX = max(0, (screenW - cvbsSplashPercentBoxW) / 2);
    cvbsSplashPercentBoxY = cvbsSplashTextY + CVBS_SPLASH_TEXT_H + 4;
    cvbsSplashPercentBoxY = constrain(cvbsSplashPercentBoxY, 0, max(0, screenH - cvbsSplashPercentBoxH));
}

static void cvbsDrawCenteredText(const String &text, int y, uint8_t color) {
    if (cvbsFallbackVideo.yres > 0) y = constrain(y, 0, max(0, cvbsFallbackVideo.yres - CVBS_SPLASH_TEXT_H));
    const int screenW = cvbsFallbackVideo.xres > 0 ? cvbsFallbackVideo.xres : CVBS_SPLASH_W;
    int textW = cvbsTinyTextWidth(text);
    int x = (screenW - textW) / 2;
    if (x < 0) x = 0;
    cvbsDrawTinyText(text, x, y, color);
}

static void cvbsDrawSplashLogo() {
    const uint16_t logoH = cvbsLogoDrawHeight();
    for (uint16_t y = 0; y < logoH; ++y) {
        uint16_t sy = y * CVBS_SPLASH_LOGO_Y_DIV;
        for (uint16_t x = 0; x < CITADELA_SPLASH_LOGO_W; ++x) {
            uint8_t v = 0;
            for (uint8_t sampleY = 0; sampleY < CVBS_SPLASH_LOGO_Y_DIV && sy + sampleY < CITADELA_SPLASH_LOGO_H; ++sampleY) {
                uint8_t sample = pgm_read_byte(&CITADELA_SPLASH_LOGO[((uint32_t)sy + sampleY) * CITADELA_SPLASH_LOGO_W + x]);
                if (sample > v) v = sample;
            }
            int px = cvbsSplashLogoX + x;
            int py = cvbsSplashLogoY + y;
            if (v && (unsigned int)px < (unsigned int)cvbsFallbackVideo.xres &&
                (unsigned int)py < (unsigned int)cvbsFallbackVideo.yres) {
                cvbsFallbackVideo.dotFast(px, py, v);
            }
        }
    }
}

static void cvbsDrawSplashPercent(int percent) {
    String percentText = String(percent) + "%";
    int textW = cvbsTinyTextWidth(percentText);
    int x = cvbsSplashPercentBoxX + max(0, (cvbsSplashPercentBoxW - textW) / 2);
    cvbsFallbackVideo.fillRect(cvbsSplashPercentBoxX, cvbsSplashPercentBoxY,
                               cvbsSplashPercentBoxW, cvbsSplashPercentBoxH, 0);
    cvbsDrawTinyText(percentText, x, cvbsSplashPercentBoxY, 255);
}

static void cvbsRenderBootSplashFull(int percent, const String &text) {
    cvbsFallbackVideo.clear(0);
    cvbsDrawSplashLogo();

    const int innerW = cvbsSplashBarW - 4;
    int fillW = (innerW * percent) / 100;

    cvbsFallbackVideo.rect(cvbsSplashBarX, cvbsSplashBarY, cvbsSplashBarW, cvbsSplashBarH, 255);
    cvbsFallbackVideo.fillRect(cvbsSplashBarX + 2, cvbsSplashBarY + 2, innerW, cvbsSplashBarH - 4, 24);
    if (fillW > 0) {
        cvbsFallbackVideo.fillRect(cvbsSplashBarX + 2, cvbsSplashBarY + 2, fillW, cvbsSplashBarH - 4, 220);
    }

    cvbsDrawCenteredText(text, cvbsSplashTextY, 255);
    cvbsDrawSplashPercent(percent);

    cvbsSplashStaticDrawn = true;
    cvbsSplashStaticLabel = text;
    cvbsSplashLastPercent = percent;
    cvbsSplashLastFillW = fillW;
}

static void cvbsRenderBootSplash(int percent, const String &label) {
    if (!cvbsFallbackVideoReady) return;

    percent = constrain(percent, 0, 100);
    String text = cvbsFitBootLabel(label);
    cvbsUpdateSplashLayout();

    if (!cvbsSplashStaticDrawn || text != cvbsSplashStaticLabel || percent < cvbsSplashLastPercent) {
        cvbsRenderBootSplashFull(percent, text);
        cvbsFallbackVideo.show(false);
        return;
    }

    if (percent == cvbsSplashLastPercent) return;

    const int innerW = cvbsSplashBarW - 4;
    int fillW = (innerW * percent) / 100;
    if (fillW > cvbsSplashLastFillW) {
        cvbsFallbackVideo.fillRect(cvbsSplashBarX + 2 + cvbsSplashLastFillW,
                                   cvbsSplashBarY + 2,
                                   fillW - cvbsSplashLastFillW,
                                   cvbsSplashBarH - 4,
                                   220);
        cvbsSplashLastFillW = fillW;
    }
    cvbsDrawSplashPercent(percent);
    cvbsSplashLastPercent = percent;

    cvbsFallbackVideo.show(false);
}

static void cvbsDrawFetchImageFile(int x, int y, uint8_t color) {
    cvbsFallbackVideo.fillRect(x, y, 8, 10, 0);
    cvbsFallbackVideo.rect(x, y, 8, 10, color);
    cvbsFallbackVideo.line(x + 1, y + 2, x + 6, y + 2, color);
    cvbsFallbackVideo.dotFast(x + 2, y + 5, color);
    cvbsFallbackVideo.line(x + 1, y + 8, x + 3, y + 6, color);
    cvbsFallbackVideo.line(x + 3, y + 6, x + 6, y + 8, color);
}

static void cvbsDrawFetchScene(uint8_t frame) {
    const int screenW = cvbsFallbackVideo.xres > 0 ? cvbsFallbackVideo.xres : CVBS_SPLASH_W;
    const int screenH = cvbsFallbackVideo.yres > 0 ? cvbsFallbackVideo.yres : CVBS_SPLASH_H;
    const int sceneTop = 39;
    const int sceneH = min(70, max(1, screenH - sceneTop - 27));
    static const uint8_t arcY[20] = {
        0, 2, 5, 8, 11, 13, 15, 16, 17, 17,
        16, 15, 13, 11, 8, 6, 4, 2, 1, 0
    };

    cvbsFallbackVideo.fillRect(0, sceneTop, screenW, sceneH, 0);

    const int sdX = 5;
    const int sdY = 58;
    cvbsFallbackVideo.fillRect(sdX, sdY, 24, 35, 25);
    cvbsFallbackVideo.rect(sdX, sdY, 24, 35, 255);
    cvbsFallbackVideo.fillRect(sdX + 15, sdY, 9, 5, 0);
    cvbsFallbackVideo.line(sdX + 15, sdY, sdX + 15, sdY + 5, 255);
    cvbsFallbackVideo.line(sdX + 15, sdY + 5, sdX + 23, sdY + 5, 255);
    for (int contact = 0; contact < 4; ++contact) {
        cvbsFallbackVideo.fillRect(sdX + 4 + contact * 4, sdY + 5, 2, 7, 180);
    }
    cvbsDrawTinyText("SD", sdX + 8, sdY + 22, 255);

    for (int file = 0; file < 5; ++file) {
        int travel = (frame + file * 4) % 20;
        int x = 24 + (travel * 45) / 19;
        int y = 59 - arcY[travel];
        cvbsDrawFetchImageFile(x, y, (uint8_t)(135 + file * 24));
    }

    const int folderX = max(62, screenW - 32);
    const int folderY = 61;
    cvbsFallbackVideo.fillRect(folderX + 2, folderY, 13, 6, 110);
    cvbsFallbackVideo.rect(folderX + 2, folderY, 13, 6, 255);
    cvbsFallbackVideo.fillRect(folderX, folderY + 5, 29, 27, 55);
    cvbsFallbackVideo.rect(folderX, folderY + 5, 29, 27, 255);
    cvbsFallbackVideo.line(folderX + 2, folderY + 10, folderX + 26, folderY + 10, 170);

    for (int dot = 0; dot < 5; ++dot) {
        uint8_t shade = dot == ((frame / 2) % 5) ? 255 : 70;
        cvbsFallbackVideo.fillRect((screenW / 2) - 12 + dot * 6, sceneTop + sceneH - 3, 3, 2, shade);
    }
}

static void cvbsRenderFetchingFiles(bool fullRedraw) {
    if (!cvbsFallbackVideoReady) return;
    if (fullRedraw) {
        cvbsFallbackVideo.clear(0);
        cvbsDrawCenteredText("FETCHING FILES", 18, 255);
        int screenW = cvbsFallbackVideo.xres > 0 ? cvbsFallbackVideo.xres : CVBS_SPLASH_W;
        cvbsFallbackVideo.line(8, 30, screenW - 9, 30, 110);
        cvbsDrawCenteredText("PLEASE WAIT", max(0, cvbsFallbackVideo.yres - 17), 180);
    }
    cvbsDrawFetchScene(cvbsFetchAnimationFrame);
    cvbsFallbackVideo.show(false);
}

static void serviceCVBSFetchAnimation() {
    if (!cvbsFetchModeActive || !cvbsFallbackVideoReady || !cvbsFailoverDriving) return;
    unsigned long now = millis();
    if (now - cvbsFetchAnimationLastMs < 90) return;
    cvbsFetchAnimationLastMs = now;
    cvbsFetchAnimationFrame = (cvbsFetchAnimationFrame + 1) % 20;
    cvbsRenderFetchingFiles(false);
}

static void cvbsUpdateBootSplash(int percent, const String &label) {
    cvbsFetchModeActive = false;
    cvbsBootProgressPercent = constrain(percent, 0, 100);
    cvbsBootProgressLabel = cvbsFitBootLabel(label);
    cvbsFailoverEnabled = true;
    cvbsForceFallback = true;
    setCVBSFailoverDriving(true);
    if (cvbsFallbackVideoReady && cvbsFailoverDriving) {
        cvbsRenderBootSplash(cvbsBootProgressPercent, cvbsBootProgressLabel);
    }
}

static void cvbsPrepareBootSplash(int percent, const String &label) {
    cvbsFetchModeActive = false;
    cvbsBootProgressPercent = constrain(percent, 0, 100);
    cvbsBootProgressLabel = cvbsFitBootLabel(label);
    cvbsFailoverEnabled = true;
    cvbsForceFallback = false;
    if (cvbsEnsureHardwareVideo(true)) {
        cvbsRenderBootSplash(cvbsBootProgressPercent, cvbsBootProgressLabel);
        cvbsListenMode();
    }
}

static bool sampleKernelCVBSSignal() {
    cvbsListenMode();
    delayMicroseconds(180);

    uint16_t mn = 4095;
    uint16_t mx = 0;
    uint8_t lowHits = 0;
    uint8_t highHits = 0;

    for (int i = 0; i < 128; ++i) {
        uint16_t v = analogRead(CVBS_FAILOVER_PIN);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        if (v <= CVBS_SIGNAL_LOW_MAX) lowHits++;
        if (v >= CVBS_SIGNAL_HIGH_MIN) highHits++;
        delayMicroseconds(43);
    }

    uint16_t span = mx - mn;
    cvbsLastMin = mn;
    cvbsLastMax = mx;
    cvbsLastSpan = span;

    bool present = (span >= CVBS_SIGNAL_SPAN_MIN &&
                    lowHits > 0 &&
                    (highHits > 0 || mx >= (uint16_t)(CVBS_SIGNAL_LOW_MAX + CVBS_SIGNAL_SPAN_MIN)));
    bool wasPresent = cvbsKernelSignalPresent;
    cvbsKernelSignalPresent = present;
    if (present) {
        unsigned long now = millis();
        if (!wasPresent) cvbsKernelSignalStableSince = now;
        cvbsLastSignalSeenMs = now;
    } else {
        cvbsKernelSignalStableSince = 0;
    }
    return present;
}

static void setCVBSFailoverDriving(bool driving) {
    if (driving == cvbsFailoverDriving) return;
    if (driving) {
        if (!cvbsDriveMode()) {
            cvbsFailoverDriving = false;
            return;
        }
        cvbsFailoverDriving = true;
        cvbsDriveStartedMs = millis();
        if (cvbsFallbackVideoReady) {
            if (cvbsFetchModeActive) cvbsRenderFetchingFiles(true);
            else cvbsRenderBootSplash(cvbsBootProgressPercent, cvbsBootProgressLabel);
        }
        Serial.println("CVBS failover active: controller is driving GPIO25");
    } else {
        cvbsFailoverDriving = false;
        cvbsListenMode();
        nextBLEEnableTryMs = 0;
        Serial.println("CVBS failover idle: external signal detected on GPIO25");
    }
}

static bool handleVideoProgressCommand(const String &command, bool fromSerial1) {
    String raw = command;
    raw.trim();
    String cmd = raw;
    cmd.toUpperCase();

    auto parseProgressPayload = [](const String &payloadRaw, int &percent, String &label) {
        String payload = payloadRaw;
        payload.trim();
        if (!payload.length()) return;
        int split = payload.indexOf(' ');
        String pctText = (split >= 0) ? payload.substring(0, split) : payload;
        pctText.trim();
        percent = constrain(pctText.toInt(), 0, 100);
        if (split >= 0) {
            label = payload.substring(split + 1);
            label.trim();
        }
    };

    if (cmd == "VDFETCHPREP") {
        cvbsFetchModeActive = true;
        cvbsFetchAnimationFrame = 0;
        cvbsFetchAnimationLastMs = millis();
        cvbsFailoverEnabled = true;
        cvbsForceFallback = false;
        cvbsFailoverSuppressUntilMs = 0;
        setCVBSFailoverDriving(false);
        if (!cvbsEnsureHardwareVideo(true)) {
            if (fromSerial1) sendKernelVideoAck("VDFETCHFAILED");
            else Serial.println("Wallpaper fetch screen preparation failed.");
            return true;
        }
        cvbsRenderFetchingFiles(true);
        cvbsListenMode();
        if (fromSerial1) sendKernelVideoAck("VDFETCHREADY");
        else Serial.println("Wallpaper fetch screen prepared.");
        return true;
    }

    if (cmd == "VDFETCHSTART") {
        cvbsFetchModeActive = true;
        cvbsFetchAnimationFrame = 0;
        cvbsFetchAnimationLastMs = millis();
        cvbsFailoverEnabled = true;
        cvbsForceFallback = true;
        cvbsFailoverSuppressUntilMs = 0;
        setCVBSFailoverDriving(true);
        if (cvbsFallbackVideoReady && cvbsFailoverDriving) cvbsRenderFetchingFiles(true);
        if (!fromSerial1) Serial.println("Wallpaper fetch animation started.");
        return true;
    }

    if (cmd == "VDFETCHEND" || cmd == "VDFETCHABORT") {
        cvbsFetchModeActive = false;
        cvbsForceFallback = false;
        cvbsFailoverSuppressUntilMs = millis() + 1500;
        setCVBSFailoverDriving(false);
        if (fromSerial1) sendKernelVideoAck("VDFETCHOFF");
        else Serial.println("Wallpaper fetch animation stopped for kernel handoff.");
        return true;
    }

    if (cmd == "VDFETCHKERNEL") {
        cvbsFetchModeActive = false;
        cvbsForceFallback = false;
        cvbsFailoverSuppressUntilMs = 0;
        setCVBSFailoverDriving(false);
        sampleKernelCVBSSignal();
        if (!fromSerial1) Serial.println("Kernel video resumed after wallpaper fetch.");
        return true;
    }

    if (cmd.startsWith("VDPREP")) {
        int percent = cvbsBootProgressPercent;
        String label = cvbsBootProgressLabel;
        parseProgressPayload(raw.substring(6), percent, label);
        cvbsPrepareBootSplash(percent, label);
        if (!fromSerial1) Serial.printf("Video boot splash prepared: %d%% %s\n", percent, cvbsBootProgressLabel.c_str());
        return true;
    }

    if (cmd.startsWith("VDAPPSTART")) {
        String payload = raw.substring(10);
        payload.trim();
        int split = payload.indexOf(' ');
        String totalText = (split >= 0) ? payload.substring(0, split) : payload;
        totalText.trim();
        cvbsAppFlashTotalBytes = (uint32_t)totalText.toInt();
        cvbsAppFlashLabel = (split >= 0) ? payload.substring(split + 1) : String("Flashing app");
        cvbsAppFlashLabel.trim();
        if (!cvbsAppFlashLabel.length()) cvbsAppFlashLabel = "Flashing app";
        cvbsUpdateBootSplash(0, cvbsAppFlashLabel);
        if (!fromSerial1) Serial.printf("Video app flash start: total=%lu %s\n",
                                        (unsigned long)cvbsAppFlashTotalBytes,
                                        cvbsAppFlashLabel.c_str());
        return true;
    }

    if (cmd.startsWith("VDAPPWRITE")) {
        uint32_t written = (uint32_t)raw.substring(10).toInt();
        int percent = 0;
        if (cvbsAppFlashTotalBytes > 0) {
            percent = constrain(10 + (int)((written * 88ULL) / cvbsAppFlashTotalBytes), 0, 99);
        }
        cvbsUpdateBootSplash(percent, cvbsAppFlashLabel);
        if (!fromSerial1) Serial.printf("Video app flash write: %lu/%lu -> %d%%\n",
                                        (unsigned long)written,
                                        (unsigned long)cvbsAppFlashTotalBytes,
                                        percent);
        return true;
    }

    if (cmd == "VDINIT" || cmd == "VDPROGSTART" || cmd == "CBLOADSTART") {
        cvbsUpdateBootSplash(0, "Preparing Citadela");
        if (!fromSerial1) Serial.println("Video boot splash started.");
        return true;
    }

    if (cmd == "VDAPPVIDEO" || cmd == "APPVIDEO" || cmd == "APPVIDEOSTART") {
        cvbsFetchModeActive = false;
        cvbsFailoverEnabled = true;
        cvbsForceFallback = false;
        setCVBSFailoverDriving(false);
        sampleKernelCVBSSignal();
        if (!fromSerial1) Serial.println("App/kernel video active; CVBS failover remains automatic.");
        return true;
    }

    if (cmd == "VDDONE" || cmd == "VDEND" || cmd == "CBLOADEND" ||
        cmd == "VDKERNEL" || cmd == "VDALIVE" || cmd == "VDSTOP" ||
        cmd == "VDRELEASE" || cmd == "KERNELVIDEO") {
        cvbsFetchModeActive = false;
        cvbsFailoverEnabled = true;
        cvbsForceFallback = false;
        setCVBSFailoverDriving(false);
        sampleKernelCVBSSignal();
        if (!fromSerial1) Serial.println("Video boot splash released; CVBS failover remains automatic.");
        return true;
    }

    int prefixLen = -1;
    if (cmd.startsWith("VDPROG")) {
        prefixLen = 6;
    } else if (cmd.startsWith("CBLOAD ")) {
        prefixLen = 6;
    }
    if (prefixLen < 0) return false;

    int percent = cvbsBootProgressPercent;
    String label = cvbsBootProgressLabel;
    parseProgressPayload(raw.substring(prefixLen), percent, label);

    cvbsUpdateBootSplash(percent, label);
    if (!fromSerial1) {
        Serial.printf("Video boot progress: %d%% %s\n", percent, cvbsBootProgressLabel.c_str());
    }
    return true;
}

static bool handleCVBSFailoverCommand(const String &command, bool fromSerial1) {
    String cmd = command;
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "CVBS?" || cmd == "CVBSSTATUS") {
        cvbsEmitStatus(fromSerial1);
        return true;
    }
    if (cmd == "CVBSON" || cmd == "CVBSENABLE") {
        cvbsFailoverEnabled = true;
        if (fromSerial1) enqueuePeerLine("CVBS ON");
        else Serial.println("CVBS watchdog enabled.");
        return true;
    }
    if (cmd == "CVBSOFF" || cmd == "CVBSDISABLE") {
        if (fromSerial1) {
            cvbsFailoverEnabled = true;
            cvbsForceFallback = false;
            setCVBSFailoverDriving(false);
            sampleKernelCVBSSignal();
            enqueuePeerLine("CVBS AUTO");
            return true;
        }
        cvbsFailoverEnabled = false;
        setCVBSFailoverDriving(false);
        Serial.println("CVBS watchdog disabled.");
        return true;
    }
    if (cmd == "CVBSFORCE" || cmd == "CVBSFORCEON") {
        cvbsFailoverEnabled = true;
        cvbsForceFallback = true;
        if (fromSerial1) enqueuePeerLine("CVBS FORCE ON");
        else Serial.println("CVBS force mode enabled. Controller will drive GPIO25.");
        return true;
    }
    if (cmd == "CVBSAUTO" || cmd == "CVBSFORCEOFF") {
        cvbsForceFallback = false;
        if (fromSerial1) enqueuePeerLine("CVBS AUTO");
        else Serial.println("CVBS automatic failover mode enabled.");
        return true;
    }
    return false;
}

static void serviceCVBSFailover() {
    static bool adcReady = false;
    static uint8_t missingReads = 0;
    static unsigned long nextServiceMs = 0;

    unsigned long now = millis();
    if ((long)(now - nextServiceMs) < 0) return;

    if (!adcReady) {
        analogReadResolution(12);
        analogSetPinAttenuation(CVBS_FAILOVER_PIN, ADC_11db);
        cvbsListenMode();
        adcReady = true;
    }

    if (!cvbsFailoverEnabled) {
        setCVBSFailoverDriving(false);
        nextServiceMs = now + 250;
        return;
    }

    if (cvbsFailoverSuppressUntilMs && (int32_t)(cvbsFailoverSuppressUntilMs - now) > 0) {
        setCVBSFailoverDriving(false);
        nextServiceMs = now + 20;
        return;
    }
    if (cvbsFailoverSuppressUntilMs) cvbsFailoverSuppressUntilMs = 0;

    if (cvbsForceFallback) {
        setCVBSFailoverDriving(true);
        nextServiceMs = now + 250;
        return;
    }

    if (!cvbsFailoverDriving) {
        bool present = sampleKernelCVBSSignal();
        if (present) {
            missingReads = 0;
        } else if (++missingReads >= CVBS_MISSING_CONFIRM_READS) {
            missingReads = 0;
            setCVBSFailoverDriving(true);
        }
        nextServiceMs = millis() + CVBS_PASSIVE_CHECK_MS;
        return;
    }

    if (cvbsFailoverDriving) {
        if (millis() - cvbsDriveStartedMs >= CVBS_ACTIVE_RECHECK_GRACE_MS) {
            if (sampleKernelCVBSSignal()) {
                cvbsForceFallback = false;
                setCVBSFailoverDriving(false);
            } else {
                cvbsDriveMode();
            }
        }
        nextServiceMs = now + CVBS_ACTIVE_RECHECK_MS;
        return;
    }
    nextServiceMs = millis() + CVBS_ACTIVE_RECHECK_MS;
}

static void cvbsFailoverTask(void *param) {
    (void)param;
    for (;;) {
        serviceCVBSFailover();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void clockTask(void *param) {
    (void)param;
    for (;;) {
        serviceHardwareClock();
        serviceManualClockPersistence();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void bluetoothTask(void *param) {
    (void)param;
    for (;;) {
        String work;
        bool fromSerial1 = false;
        if (dequeueBLEWork(work, fromSerial1)) {
            processBLEWork(work, fromSerial1);
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            ensureReconnectScheduledIfIdle();
            attemptReconnectIfNeeded();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

static bool bleCanRunNow() {
    if (!blePausing) return true;
    if ((long)(millis() - blePausedUntilMs) >= 0) {
        blePausing = false;
        return true;
    }
    return false;
}

static void resetBLERuntimeHandles() {
    pClient = nullptr;
    pBLEScan = nullptr;
    inputNotifyRegistered = false;
    hidReady = false;
    inputReportChars.clear();
    pHIDService = nullptr;
    pInputReportChar = nullptr;
    pTrackpadChar = nullptr;
}

static void stopBLERuntimeForVideo() {
    blePausing = true;
    blePausedUntilMs = millis() + BLE_VIDEO_PAUSE_MS;
    if (!bleRuntimeEnabled && !pClient && !pBLEScan) {
        return;
    }

    Serial.println("BLE paused briefly to free heap for CVBS video init.");
    if (pBLEScan) pBLEScan->stop();
    if (pClient && pClient->isConnected()) {
        pClient->disconnect();
        delay(10);
    }
    BLEDevice::deinit(false);
    resetBLERuntimeHandles();
    bleRuntimeEnabled = false;
    if (lastAddress.length()) scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
}

static bool startBLERuntimeIfAllowed() {
    if (bleRuntimeEnabled) return true;
    if (!bleCanRunNow()) return false;

    Serial.println("BLE runtime enabled.");
    blePausing = false;
    initBLEClient();
    bleRuntimeEnabled = (pClient != nullptr && pBLEScan != nullptr);
    if (!bleRuntimeEnabled) {
        Serial.println("BLE runtime enable failed.");
        blePausing = true;
        return false;
    }
    if (lastAddress.length()) {
        scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
    }
    return true;
}

static void serviceBluetoothRuntime() {
    if (cvbsFetchModeActive || cvbsFailoverDriving) return;
    if (!bleCanRunNow()) {
        return;
    }

    unsigned long now = millis();
    if (!bleRuntimeEnabled) {
        if (now < nextBLEEnableTryMs) return;
        nextBLEEnableTryMs = now + BLE_ENABLE_RETRY_MS;
        if (!startBLERuntimeIfAllowed()) return;
    }

    String work;
    bool fromSerial1 = false;
    if (dequeueBLEWork(work, fromSerial1)) {
        processBLEWork(work, fromSerial1);
    } else {
        ensureReconnectScheduledIfIdle();
        attemptReconnectIfNeeded();
    }
}

static void startControllerTasks() {
    if (!clockTaskHandle) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            clockTask,
            "ClockTask",
            6144,
            nullptr,
            2,
            &clockTaskHandle,
            1);
        if (ok != pdPASS) Serial.println("ClockTask start failed");
    }

    bluetoothTaskHandle = nullptr;
    cvbsFailoverTaskHandle = nullptr;
}

void setup() {
    Serial.setRxBufferSize(512);
    Serial1.setRxBufferSize(512);
    Serial2.setRxBufferSize(512);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial2.begin(115200, SERIAL_8N1, 21, 22);
    Serial.setTimeout(5);
    Serial1.setTimeout(5);
    Serial2.setTimeout(5);
    controllerFan.begin(100);
    peerQueueMutex = xSemaphoreCreateMutex();
    bleCommandMutex = xSemaphoreCreateMutex();
    clockMutex = xSemaphoreCreateMutex();
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        SPIFFS.format();
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed Again");
            return;
        }
    }
    loadManualClock();
    initializeHardwareClock();
    loadFanControl();
    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == ""){
        boolRes = "false";
        boolUpdate();
    }
#if ENABLE_NETWORK_FEATURES
    loadHFConfig();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    Serial.println("WiFi ready for on-demand AI connection");
#endif
    if (boolRes == "VDCLoadNSS"){
      boolRes = "false";
      boolUpdate();
    } else if (boolRes == "false") {
      readAddress();
      if (lastAddress.length() > 0) {
          Serial.printf("Saved BLE address %s type %u. Scheduling BLE reconnect.\n", lastAddress.c_str(), lastAddressType);
          scheduleBLEReconnect(RECONNECT_MIN_BACKOFF, true);
      }
    } else if (boolRes == "AudioSwing") {
      boolRes = "false";
      boolUpdate();
      sendHFOutput("Audio Receiver mode active. Connect via Bluetooth to 'Citadela' and play audio.");
    }

    Serial.println("Ready. Commands: CBTIME, CBSETTIME YYYY-MM-DD HH:MM:SS, CBTIMECLR, CBRTC, CBFAN 0-100, BLE00, BLE0n, BLEC");
    startControllerTasks();
    if (!displayRelay.begin()) Serial.println("DisplayRelay task start failed");
}

void loop() {
    checkSerialCommand();
    if (!cvbsFailoverTaskHandle) serviceCVBSFailover();
    serviceCVBSFetchAnimation();
    serviceBluetoothRuntime();
    flushPeerQueue();
#if ENABLE_NETWORK_FEATURES
    pollWiFiStatus();
#endif
    delay(1);
}

