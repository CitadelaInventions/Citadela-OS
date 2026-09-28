// Full revised sketch with non-blocking serial output queue and improved auto-reconnect
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "FS.h"
#include "SPIFFS.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_err.h"

#include <vector>
#include <map>
#include <deque>

int devices = 1;

String dataToSave = "";
String lastAddress = "";
String filePath = "";
String boolRes = "";
String appName = "";
String appString = "";

static BLEClient* pClient = nullptr;
static BLEScan* pBLEScan = nullptr;

BLERemoteCharacteristic* pTrackpadChar = nullptr;
std::vector<BLEAdvertisedDevice> deviceList;
BLERemoteService* pHIDService = nullptr;
BLERemoteCharacteristic* pInputReportChar = nullptr;

static const BLEUUID HID_TRACKPAD_CHAR_UUID("00002A33-0000-1000-8000-00805F9B34FB");
static const BLEUUID HID_SERVICE_UUID("00001812-0000-1000-8000-00805f9b34fb");
static const BLEUUID HID_INPUT_CHAR_UUID("00002A4D-0000-1000-8000-00805f9b34fb");

bool silence = false;
bool loading = false;
bool inputNotifyRegistered = false;


String savedSSID = "";
String savedPASS = "";
bool wifiConnected = false;

String HF_SPACE_HOST = "";
String HF_TOKEN = "";
String HF_MODEL = "gpt2";

const char *HF_TOKEN_PATH = "/hftoken.txt";
const char *HF_MODEL_PATH = "/hfmodel.txt";
const char *HF_SPACE_PATH = "/hfspace.txt";
const char *WIFI_SSID_PATH = "/wifi_ssid.txt";
const char *WIFI_PASS_PATH = "/wifi_pass.txt";

size_t JSON_BUFFER_SIZE = 32 * 1024;

const bool FULL_BT_SHUTDOWN = true;
static bool capsLock = false;

volatile bool blePausing = false;
volatile bool bleNeedReconnect = false;
unsigned long lastBtConnectAttempt = 0;
unsigned long nextReconnectAttempt = 0;
unsigned long reconnectBackoff = 1000UL;
const unsigned long RECONNECT_MAX_BACKOFF = 3000UL;
const unsigned long AUTO_CONNECT_INTERVAL_MS = 1000UL;

void resumeBLEAfterTLS();
void keyboardAuthService();

class MyClientCallbacks : public BLEClientCallbacks {
public:
  void onConnect(BLEClient* pclient) override {
    Serial.println("BLE Client Connected");
    bleNeedReconnect = false;
    reconnectBackoff = 1000UL;
  }
  void onDisconnect(BLEClient* pclient) override {
    Serial.println("BLE Client Disconnected");
    // attempt to unregister notify (best-effort)
    if (pInputReportChar && inputNotifyRegistered) {
      // don't call lengthy operations here; just try to unregister
      pInputReportChar->registerForNotify(nullptr);
    }
    inputNotifyRegistered = false;

    // clear references (they may be invalid after disconnect)
    pHIDService = nullptr;
    pInputReportChar = nullptr;
    pTrackpadChar = nullptr;

    // schedule reconnect attempts from main loop
    bleNeedReconnect = true;
    nextReconnectAttempt = millis() + 200; // short delay before first attempt
    reconnectBackoff = 1000UL;
    lastBtConnectAttempt = millis();
  }
};

static MyClientCallbacks myClientCallbacks;

// ----- Non-blocking serial output queue -----
std::deque<String> peerQueue;
unsigned long lastPeerSend = 0;
const unsigned long PEER_SEND_INTERVAL_MS = 25; // small spacing so Serial buffers don't overflow

// enqueue a single line (without extra newline)
static inline void enqueuePeerLine(const String &s) {
  peerQueue.push_back(s);
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

// send at most one (or a small number) queued peer lines per loop iteration
static void flushPeerQueue() {
  if (peerQueue.empty()) return;
  unsigned long now = millis();
  if (now - lastPeerSend < PEER_SEND_INTERVAL_MS) return;
  // send one line per call to keep loop responsive
  String s = peerQueue.front();
  peerQueue.pop_front();
  Serial.println(s);
  Serial1.println(s);
  lastPeerSend = now;
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

String loadFile(const char *p) {
  if (!SPIFFS.exists(p)) return "";
  File f = SPIFFS.open(p, "r");
  if(!f) return "";
  String s = f.readString();
  f.close();
  s.trim();
  return s;
}

void syncTime(unsigned long timeoutMs = 15000) {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  unsigned long t0 = millis();
  while (time(nullptr) < 1600000000 && millis() - t0 < timeoutMs) {
    delay(200);
  }
  setBulgariaTZ();
  Serial.printf("NTP done, epoch=%ld\n", time(nullptr));
}

void saveFile(const char *p, const String &v) {
  File f = SPIFFS.open(p, "w");
  if (f) {
    f.print(v);
    f.close();
  }
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
  WiFi.setHostname("CITADELA");
  WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(250);
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected) syncTime();
  return wifiConnected;
}

void boolTru() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    if (!file) return;
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    boolRes = fileContent;
    file.close();
    File app = SPIFFS.open("/remApp.txt", FILE_READ);
    if (!app) return;
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
        Serial.printf("Bool updated to %s\n", boolRes.c_str());
    } else {
        Serial.println("Failed to update bool!");
    }
}
void saveAddress() {
    File file = SPIFFS.open("/addresser.txt", FILE_WRITE);
    if (file.print(dataToSave)) {
        Serial.println("File written successfully " + dataToSave);
    }
    file.close();
}
void readAddress() {
    File file = SPIFFS.open("/addresser.txt", FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }
    String fileContent = "";
    while (file.available()) {
        fileContent += (char)file.read();
    }
    Serial.println("Received Spiffs: " + fileContent);
    lastAddress = fileContent;
    file.close();
}
void scanDevices() {
    devices = 1;
    if (!pBLEScan) return;
    BLEScanResults foundDevices = *pBLEScan->start(5, false);
    int count = foundDevices.getCount();
    deviceList.clear();
    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = foundDevices.getDevice(i);
        String deviceName = device.getName().c_str();
        if (deviceName.length() == 0) {
            deviceName = "Unknown";
        }
        if (deviceName != "Unknown") {
            devices++;
            deviceList.push_back(device);
            String devLine = String("dev") + devices + String(" ") + deviceName;
            enqueuePeerLine(devLine);
        }
    }
    pBLEScan->clearResults();
}
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
void keyboardAuthService() {
    if (!pClient) {
        Serial.println("keyboardAuthService: pClient is null");
        return;
    }

    pHIDService = pClient->getService(HID_SERVICE_UUID);
    if (pHIDService == nullptr) {
        Serial.println("HID Service not found!");
        return;
    }

    pInputReportChar = pHIDService->getCharacteristic(HID_INPUT_CHAR_UUID);
    if (pInputReportChar == nullptr) {
        Serial.println("Keyboard input report not found!");
        return;
    }

    // unregister any previous notify cleanly
    if (inputNotifyRegistered && pInputReportChar) {
      pInputReportChar->registerForNotify(nullptr);
      inputNotifyRegistered = false;
    }

    // register callback
    pInputReportChar->registerForNotify([](BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify){
        // delegate to onKeyPress behaviour but capture parameters here
        if (len >= 8) {
          onKeyPress(c, data, len, isNotify);
        }
    });
    inputNotifyRegistered = true;

    pTrackpadChar = pHIDService->getCharacteristic(HID_TRACKPAD_CHAR_UUID);
    if (pTrackpadChar != nullptr) {
        enqueuePeerLine("Found trackpad characteristic, subscribing...");
        pTrackpadChar->registerForNotify([](BLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify){
            String out = "Trackpad notification received: ";
            for (size_t i = 0; i < len; ++i) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", data[i]);
                out += buf;
            }
            enqueuePeerLine(out);
        });
    } else {
        enqueuePeerLine("Trackpad characteristic not found!");
    }
}

void onKeyPress(BLERemoteCharacteristic* pCharacteristic,
                uint8_t* data,
                size_t length,
                bool isNotify) {
    if (length < 8) return;
    static uint8_t lastKeycode = 0;

    uint8_t modifier = data[0];
    uint8_t keycode  = data[2];
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

    if (!key.isEmpty()) {
        if (!silence) {
            fastCommand(key);
        }
    } else {
        if (!silence) {
            fastCommand(key);
        }
    }
    lastKeycode = keycode;
}


void listAllUUIDs() {
    if (!pClient) return;
    std::map<std::string, BLERemoteService*>* services = pClient->getServices();
    for (auto const &servicePair: *services) {
        BLERemoteService* service = servicePair.second;
        Serial.print("Service UUID: ");
        Serial.println(service->getUUID().toString().c_str());
        std::map<std::string, BLERemoteCharacteristic*>* characteristics = service->getCharacteristics();
        for (auto const &charPair: *characteristics) {
            BLERemoteCharacteristic* characteristic = charPair.second;
            Serial.print("\tCharacteristic UUID: ");
            Serial.println(characteristic->getUUID().toString().c_str());
        }
    }
}

// Initialize BLE client & scan objects (idempotent)
void initBLEClient() {
  if (!pClient) {
    BLEDevice::init("CITADELA");
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&myClientCallbacks);
  } else {
    pClient->setClientCallbacks(&myClientCallbacks);
  }
  if (!pBLEScan) {
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
  }
}

// connect to address (explicit)
bool connectToAddress(BLEAddress address) {
  if (blePausing) return false;

  if (!pClient) {
    Serial.println("connectToAddress: recreating BLE client");
    BLEDevice::init("CITADELA");
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&myClientCallbacks);
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
    delay(50);
  }

  if (pClient->isConnected()) {
    pClient->disconnect();
    delay(50);
  }

  String addrStr = address.toString();
  Serial.printf("Attempting connection to %s...\n", addrStr.c_str());

  bool ok = pClient->connect(address, BLE_ADDR_TYPE_PUBLIC);
  if (!ok) {
    Serial.println("BLE connect failed");
    return false;
  }

  Serial.println("BLE connected");

  lastAddress = addrStr;
  saveAddress();

  enqueuePeerLine("BL1X");
  keyboardAuthService();

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
    BLEAddress address = deviceList[index - 1].getAddress();
    if (pClient && pClient->isConnected()) {
      pClient->disconnect();
      delay(20);
    }
    initBLEClient();
    if (connectToAddress(address)) {
        // success
    } else {
        enqueuePeerLine("BL0X");
    }
}

// Reconnection routine invoked from loop()
void attemptReconnectIfNeeded() {
  if (!bleNeedReconnect) return;
  if (blePausing) return;
  unsigned long now = millis();
  if (now < nextReconnectAttempt) return;

  if (!lastAddress.length()) {
    Serial.println("attemptReconnectIfNeeded: no saved address; nothing to reconnect to.");
    bleNeedReconnect = false;
    return;
  }

  if (!pClient) {
    Serial.println("attemptReconnectIfNeeded: reinitializing BLE client before reconnect");
    initBLEClient();
    delay(50);
  }

  lastBtConnectAttempt = now;
  BLEAddress addr(lastAddress.c_str());
  bool ok = false;
  delay(20);
  ok = pClient->connect(addr, BLE_ADDR_TYPE_PUBLIC);

  if (ok) {
    Serial.println("Auto-reconnect succeeded");
    enqueuePeerLine("BL1X");
    keyboardAuthService();
    bleNeedReconnect = false;
    reconnectBackoff = 1000UL;
    nextReconnectAttempt = 0;
  } else {
    Serial.println("Auto-reconnect failed");
    nextReconnectAttempt = millis() + reconnectBackoff + random(0, 500);
    reconnectBackoff = min(reconnectBackoff * 2, RECONNECT_MAX_BACKOFF);
    Serial.printf("Next reconnect attempt in %lu ms\n", (unsigned long)(nextReconnectAttempt - millis()));
    // keep bleNeedReconnect true
  }
}

// --- HF / HTTP / JSON helpers (kept functionally same as your original). ---
// To keep the response readable I included them unchanged; they use enqueuePeerLine/sendHFOutput above
// (Make sure in your real code you include the rest of your helper functions exactly as you had them,
// I left them in-place earlier in the snippet you provided and I've preserved their behavior here.)

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

  if (!wifiConnected && !connectWiFiAuto()) {
    enqueuePeerLine("Wi-Fi not configured/ connected");
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
        "\"max_tokens\":2048"
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

    enqueuePeerLine(String("HF request attempt ") + String(attempt + 1) + " -> " + endpoint);
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
        enqueuePeerLine(String(msg));
        delay(waitMs);
        backoffMs = min(backoffMs * 2, maxBackoff);
        continue;
      } else {
        enqueuePeerLine(String("HF UNAVAILABLE after retries.\n") + bodyPreview);
        return httpCode;
      }
    }

    if (httpCode == 401 || httpCode == 403) {
      enqueuePeerLine("HF AUTH ERROR\nCheck token or use public space.");
      return httpCode;
    }

    if (httpCode <= 0) {
      enqueuePeerLine("HTTP ERROR (no response).");
      String host = extractHostFromEndpoint(endpoint);
      IPAddress ip;
      if (WiFi.hostByName(host.c_str(), ip)) {
        enqueuePeerLine(String("DNS: ") + host + " -> " + ip.toString());
      } else {
        enqueuePeerLine(String("DNS lookup failed for ") + host);
      }
      return httpCode;
    }

    enqueuePeerLine(String("HF ERROR ") + String(httpCode) + "\n\n" + bodyPreview);
    return httpCode;
  }

  enqueuePeerLine("HF FAILED\nUnknown error.");
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
    HF_MODEL = cmd.substring(7);
    HF_MODEL.trim();
    saveFile(HF_MODEL_PATH, HF_MODEL);
    Serial.printf("HF model set: %s\n", HF_MODEL.c_str());
    if (fromSerial1) enqueuePeerLine("CBHFM OK");
    return;
  }

  if (cmd.startsWith("CBHFS ")) {
    HF_SPACE_HOST = cmd.substring(7);
    HF_SPACE_HOST.trim();
    HF_SPACE_HOST = sanitizeHost(HF_SPACE_HOST);
    saveFile(HF_SPACE_PATH, HF_SPACE_HOST);
    Serial.printf("HF space host set to: %s\n", HF_SPACE_HOST.c_str());
    if (fromSerial1) enqueuePeerLine("CBHFS OK");
    return;
  }

  if (cmd == "CBWSC") {
    Serial.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      enqueuePeerLine(String(i + 1) + ": " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + ")");
    }
    WiFi.scanDelete();
    if (fromSerial1) enqueuePeerLine("CBWSC OK");
    return;
  }

  if (cmd == "CBWS") {
    sendStatusOverSerial1();
    return;
  }

  if (cmd.startsWith("CBHFA ")) {
    enqueuePeerLine("CLEARVDD");
    String prompt = cmd.substring(6);
    prompt.trim();
    Serial.printf("Sending to HF: %s\n", prompt.c_str());
    enqueuePeerLine("CBHFR");
    String answer;
    int rc = freeAskHF(prompt, answer);
    if (rc == 0 && answer.length()) {
      enqueuePeerLine("CBHFR");
      enqueuePeerMultiLine(answer);
      delay(1000);
      enqueuePeerLine("CBHFT");
      Serial.println("HF -> Sent to Serial1");
      delay(1000);
      ESP.restart();
    } else {
      enqueuePeerLine(String("HF ERROR ") + String(rc));
    }
    return;
  }

  if (fromSerial1) enqueuePeerLine(String("UNKNOWN CMD: ") + cmd.c_str());
  else Serial.printf("UNKNOWN CMD: %s\n", cmd.c_str());
}

void checkSerialCommand() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "BLE00") {
            Serial.println("BLEX");
            scanDevices();
        } else if (command.startsWith("BLE0") && command.length() > 4) {
            int index = command.substring(4).toInt();
            connectToDevice(index);
        } else if (command.startsWith("BLEC")){
            File file = SPIFFS.open("/addresser.txt", FILE_WRITE);
            if (file.print("")) {
                Serial.println("Spiffs Cleared Successfully.");
            }
            file.close();
        } else {
            if (command.startsWith("CB")) {
              handleCBCommand(command, false);
            }
        }
    }
    if (Serial1.available()) {
        String command1 = Serial1.readStringUntil('\n');
        command1.trim();
        if (command1 == "BLE00") {
            enqueuePeerLine("BL5X");
            scanDevices();
        } else if (command1.startsWith("BLE0") && command1.length() > 4) {
            int index = command1.substring(4).toInt();
            connectToDevice(index);
        } else if (command1 == "res"){
            ESP.restart();
        } else if (command1 == "VDCLoadNSS"){
            boolRes = "VDCLoadNSS";
            boolUpdate();
            ESP.restart();
        } else if (command1 == "AudioSwing"){
            boolRes = "AudioSwing";
            boolUpdate();
            ESP.restart();
        } else if (command1.startsWith("CB")) {
            handleCBCommand(command1, true);
        }
    }
}

void loadHFConfig() {
  HF_TOKEN = loadFile(HF_TOKEN_PATH);
  HF_MODEL = loadFile(HF_MODEL_PATH);
  HF_SPACE_HOST = sanitizeHost(loadFile(HF_SPACE_PATH));
  savedSSID = loadFile(WIFI_SSID_PATH);
  savedPASS = loadFile(WIFI_PASS_PATH);
  if (!HF_MODEL.length()) HF_MODEL = "gpt2";
}

void pauseBLEForTLS() {
  Serial.println("Pausing BLE to free heap for TLS...");
  blePausing = true;

  if (pBLEScan) {
    pBLEScan->stop();
  }

  if (pClient) {
    if (pInputReportChar && inputNotifyRegistered) {
      pInputReportChar->registerForNotify(nullptr);
      inputNotifyRegistered = false;
    }

    pClient->setClientCallbacks(nullptr);
    if (pClient->isConnected()) {
      pClient->disconnect();
      delay(10);
    }
    delete pClient;
    pClient = nullptr;
  }
  pHIDService = nullptr;
  pInputReportChar = nullptr;
  pTrackpadChar = nullptr;

  if (FULL_BT_SHUTDOWN) {
    esp_err_t err;
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
      err = esp_bluedroid_disable();
      Serial.printf("esp_bluedroid_disable -> %d\n", err);
      err = esp_bluedroid_deinit();
      Serial.printf("esp_bluedroid_deinit -> %d\n", err);
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
      err = esp_bt_controller_disable();
      Serial.printf("esp_bt_controller_disable -> %d\n", err);
      err = esp_bt_controller_deinit();
      Serial.printf("esp_bt_controller_deinit -> %d\n", err);
    }
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    delay(60);
  }

  Serial.printf("Heap after BLE pause: %u\n", ESP.getFreeHeap());
}

void resumeBLEAfterTLS() {
  Serial.println("Resuming BLE after TLS...");
  esp_err_t err;

  if (FULL_BT_SHUTDOWN) {
    const int MAX_INIT_TRIES = 4;
    bool ok = false;
    for (int t=0; t<MAX_INIT_TRIES && !ok; ++t) {
      esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      err = esp_bt_controller_init(&bt_cfg);
      Serial.printf("esp_bt_controller_init -> %d\n", err);
      if (err != ESP_OK) {
        if (t < MAX_INIT_TRIES-1) {
          esp_bt_controller_deinit();
          delay(50);
          continue;
        } else break;
      }
      err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
      Serial.printf("esp_bt_controller_enable -> %d\n", err);
      if (err != ESP_OK) {
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        delay(50);
        continue;
      }
      err = esp_bluedroid_init();
      Serial.printf("esp_bluedroid_init -> %d\n", err);
      if (err != ESP_OK) {
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        delay(50);
        continue;
      }
      err = esp_bluedroid_enable();
      Serial.printf("esp_bluedroid_enable -> %d\n", err);
      if (err != ESP_OK) {
        esp_bluedroid_disable();
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        delay(50);
        continue;
      }
      ok = true;
    }
    if (!ok) {
      Serial.println("BLE controller/bluedroid init failed after retries.");
      return;
    }
  } else {
    BLEDevice::init("CITADELA");
  }

  // Use initBLEClient helper to create client and scan object safely
  if (!pClient) {
    // create new
    BLEDevice::init("CITADELA");
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&myClientCallbacks);
  }
  if (!pBLEScan) {
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
  }

  delay(120);

  if (WiFi.status() != WL_CONNECTED && savedSSID.length()) {
    Serial.println("WiFi lost during BLE restart - trying quick reconnect...");
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi reconnected: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("WiFi still disconnected after BLE resume.");
    }
  }

  blePausing = false;
  Serial.printf("Heap after BLE resume: %u\n", ESP.getFreeHeap());
}

void setup() {
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

    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == ""){
        boolRes = "false";
        boolUpdate();
    }
    loadHFConfig();
    bool ok = connectWiFiAuto();
    if(ok){Serial.println("Wifi Connected!");} else {Serial.println("Wifi Connection error!");}
    if (boolRes == "VDCLoadNSS"){
      boolRes = "false";
      boolUpdate();
    } else if (boolRes == "false") {
      readAddress();

      // Initialize BLE client & scan objects once here
      initBLEClient();

      delay(1000);
      if (lastAddress.length() > 0) {
          Serial.printf("Trying to auto-connect to saved address %s\n", lastAddress.c_str());
          BLEAddress addr(lastAddress.c_str());
          if (pClient->connect(addr, BLE_ADDR_TYPE_PUBLIC)) {
              enqueuePeerLine("BL1X");
              keyboardAuthService();
          } else {
              Serial.println("Initial auto-connect failed; scheduling reconnect attempts");
              bleNeedReconnect = true;
              nextReconnectAttempt = millis() + 500;
              reconnectBackoff = 1000UL;
          }
      }
    } else if (boolRes == "AudioSwing") {
      boolRes = "false";
      boolUpdate();
      sendHFOutput("Audio Receiver mode active. Connect via Bluetooth to 'Citadela' and play audio.");
    }

    Serial.println("Ready. Commands: CBW0 SSID, CBW1 PASS, CBW2 connect saved WiFi, CBHFT <token|set|clear|status>, CBHFM owner/model, CBHFS space, CBHFA <prompt>, CBWSC, CBWS");
}

void loop() {
    attemptReconnectIfNeeded();
    checkSerialCommand();
    flushPeerQueue();
    delay(1);
}
