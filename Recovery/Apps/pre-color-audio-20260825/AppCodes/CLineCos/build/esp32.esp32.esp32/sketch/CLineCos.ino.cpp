#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
// Full updated sketch — improved icons, resistor flicker fix, added windows for Distance/IR/NFC/Humidity
#include <SPI.h>
#include <SD.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <Ressources/Font8x8.h>
#include "SPIFFS.h"
#include "FS.h"
#include <Update.h>
#include "esp_partition.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

#define SPEAKER_PIN 12
#define LED_PIN 13
#define RESISTOR_ADC_PIN graphPins[2]
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 400;
static const int APP_CURSOR_SCREEN_H = 240;
static const int APP_CURSOR_RADIUS = 3;
static const int APP_CURSOR_OUTLINE_RADIUS = 4;
static const int APP_CURSOR_MAX_PIXELS = 81;

typedef CompositeColorDAC::BufferGraphicsUnit AppCursorRawPixel;

struct AppCursorSavedPixel {
  int8_t dx;
  int8_t dy;
  AppCursorRawPixel raw;
};

struct AppMouseReport {
  int x;
  int y;
  int buttons;
  int dx;
  int dy;
  int wheel;
  bool leftDown;
  bool leftPressed;
};

static AppCursorSavedPixel appCursorPixels[APP_CURSOR_MAX_PIXELS];
static int appCursorPixelCount = 0;
static int appCursorX = APP_CURSOR_SCREEN_W / 2;
static int appCursorY = APP_CURSOR_SCREEN_H / 2;
static int appCursorButtons = 0;
static bool appCursorEnabled = false;
static bool appCursorDrawn = false;
static bool appCursorWasLeftDown = false;
static bool appMouseCursorOutlined = false;
static bool appCursorConfigLoaded = false;

#line 60 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorLoadConfigOnce();
#line 81 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 86 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 91 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 96 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 101 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 105 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 111 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorRestore();
#line 122 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 129 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorCaptureAndDraw();
#line 161 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorRefreshAfterRedraw();
#line 171 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 192 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void restartWithFallbackVideo(const char *label);
#line 209 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void releaseSerialControllerVideo();
#line 476 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void _setPixelClamped(int imgX, int imgY, int imgW, int imgH, int px, int py, uint32_t c);
#line 513 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 524 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 529 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 1724 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static bool handleCLineCosMouse(String command);
#line 2767 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
void setup();
#line 2824 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
void loop();
#line 60 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CLineCos\\CLineCos.ino"
static void appCursorLoadConfigOnce() {
  if (appCursorConfigLoaded) return;
  appCursorConfigLoaded = true;
  File f = SPIFFS.open("/systemConfiguration.conf", FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    val.trim();
    if (key == "MouseCursorOutlined" || key == "CursorOutlineMode") {
      appMouseCursorOutlined = val.toInt() != 0;
    }
  }
  f.close();
}

static inline AppCursorRawPixel appCursorGetRawPixel(int x, int y) {
  if (!videodisplay.backBuffer || (unsigned int)x >= (unsigned int)videodisplay.xres || (unsigned int)y >= (unsigned int)videodisplay.yres) return 0;
  return videodisplay.backBuffer[videodisplay.graphics_swy(y)][videodisplay.graphics_swx(x)];
}

static inline void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw) {
  if (!videodisplay.backBuffer || (unsigned int)x >= (unsigned int)videodisplay.xres || (unsigned int)y >= (unsigned int)videodisplay.yres) return;
  videodisplay.backBuffer[videodisplay.graphics_swy(y)][videodisplay.graphics_swx(x)] = raw;
}

static bool appCursorInnerPoint(int dx, int dy) {
  int d2 = dx * dx + dy * dy;
  return d2 >= 5 && d2 <= 10;
}

static bool appCursorOutlinePoint(int dx, int dy) {
  int d2 = dx * dx + dy * dy;
  return d2 > 10 && d2 <= 18;
}

static bool appCursorCapturePoint(int dx, int dy) {
  return appCursorInnerPoint(dx, dy) || (appMouseCursorOutlined && appCursorOutlinePoint(dx, dy));
}

static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw) {
  uint8_t signal = (uint8_t)((raw >> 8) & 0xff);
  int threshold = (videodisplay.levelBlack + videodisplay.levelWhite) / 2;
  return signal >= threshold;
}

static void appCursorRestore() {
  if (!appCursorDrawn) return;
  for (int i = 0; i < appCursorPixelCount; ++i) {
    int px = appCursorX + appCursorPixels[i].dx;
    int py = appCursorY + appCursorPixels[i].dy;
    appCursorSetRawPixel(px, py, appCursorPixels[i].raw);
  }
  appCursorDrawn = false;
  appCursorPixelCount = 0;
}

static void appCursorSetPosition(int x, int y, int buttons) {
  appCursorX = constrain(x, 0, APP_CURSOR_SCREEN_W - 1);
  appCursorY = constrain(y, 0, APP_CURSOR_SCREEN_H - 1);
  appCursorButtons = buttons;
  appCursorEnabled = true;
}

static void appCursorCaptureAndDraw() {
  if (!appCursorEnabled || appCursorDrawn) return;
  appCursorLoadConfigOnce();
  appCursorPixelCount = 0;
  int radius = appMouseCursorOutlined ? APP_CURSOR_OUTLINE_RADIUS : APP_CURSOR_RADIUS;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (!appCursorCapturePoint(dx, dy)) continue;
      int px = appCursorX + dx;
      int py = appCursorY + dy;
      if ((unsigned int)px >= APP_CURSOR_SCREEN_W || (unsigned int)py >= APP_CURSOR_SCREEN_H) continue;
      if (appCursorPixelCount >= APP_CURSOR_MAX_PIXELS) continue;
      appCursorPixels[appCursorPixelCount].dx = dx;
      appCursorPixels[appCursorPixelCount].dy = dy;
      appCursorPixels[appCursorPixelCount].raw = appCursorGetRawPixel(px, py);
      appCursorPixelCount++;
    }
  }
  uint32_t white = videodisplay.RGB(255, 255, 255);
  uint32_t black = videodisplay.RGB(0, 0, 0);
  for (int i = 0; i < appCursorPixelCount; ++i) {
    int px = appCursorX + appCursorPixels[i].dx;
    int py = appCursorY + appCursorPixels[i].dy;
    bool outlinePoint = appCursorOutlinePoint(appCursorPixels[i].dx, appCursorPixels[i].dy);
    uint32_t cursorColor = white;
    if (appMouseCursorOutlined) cursorColor = outlinePoint ? white : black;
    else cursorColor = appCursorBackgroundLooksBright(appCursorPixels[i].raw) ? black : white;
    videodisplay.dotFast(px, py, cursorColor);
  }
  appCursorDrawn = true;
}

static void appCursorRefreshAfterRedraw() {
  if (appCursorEnabled && !appCursorDrawn) appCursorCaptureAndDraw();
}

class AppCursorDrawGuard {
  public:
    AppCursorDrawGuard() { appCursorRestore(); }
    ~AppCursorDrawGuard() { appCursorRefreshAfterRedraw(); }
};

static bool appMouseRead(String input, AppMouseReport &report) {
  input.trim();
  if (!input.startsWith("MOUSE")) return false;
  report.x = appCursorX;
  report.y = appCursorY;
  report.buttons = appCursorButtons;
  report.dx = 0;
  report.dy = 0;
  report.wheel = 0;
  int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d", &report.x, &report.y, &report.buttons, &report.dx, &report.dy, &report.wheel);
  if (parsed >= 3) {
    report.leftDown = (report.buttons & 0x01) != 0;
    report.leftPressed = report.leftDown && !appCursorWasLeftDown;
    appCursorWasLeftDown = report.leftDown;
  } else {
    report.leftDown = false;
    report.leftPressed = false;
  }
  return true;
}

static void restartWithFallbackVideo(const char *label) {
  const char *text = label ? label : "Restarting";
  Serial1.print("VDPREP 0 ");
  Serial1.println(text);
  Serial1.flush();
  delay(350);
  videodisplay.i2sStop();
  pinMode(25, INPUT_PULLDOWN);
  delay(5);
  Serial1.println("VDINIT");
  Serial1.print("VDPROG 100 ");
  Serial1.println(text);
  Serial1.flush();
  delay(90);
  ESP.restart();
}

static void releaseSerialControllerVideo() {
  for (uint8_t i = 0; i < 12; ++i) {
    Serial1.println("CVBSOFF");
    Serial1.println("VDAPPVIDEO");
    Serial1.flush();
    delay(80);
  }
}
File root;

String boolRes = "";
String appName = "";
String appString = "";

int selectedStreamline = 0;
int prevSelectedStreamline = -1;

bool editMode = false;
int editingIndex = -1;

int streamlineState[3] = {0,0,0};

uint32_t COLOR_WHITE;
uint32_t COLOR_BLACK;
uint32_t COLOR_LT;
uint32_t COLOR_MD;
uint32_t COLOR_DK;
uint32_t COLOR_SH;

const int graphPins[3] = {15, 4, 2};

const int samplesPerSecond = 10;
unsigned long graphInterval = 1000 / samplesPerSecond;

const int g_w  = 120;
const int g_h  = 70;
const int gap  = 8;
const int g1_x = 9;
const int g2_x = g1_x + g_w + gap;
const int g3_x = g2_x + g_w + gap;

const int g1_y = 115;
const int g2_y = g1_y;
const int g3_y = g1_y;

const int plotMargin = 4;
const int plotW  = g_w - (plotMargin * 2);
const int plotH  = g_h - (plotMargin * 2);

const int MAX_HISTORY = 1024;
static int graph_hist[3][MAX_HISTORY];
static int graph_head[3] = {0,0,0};
static int graph_count[3] = {0,0,0};

int zoomLevel = 0;
int currentGraphWidth = plotW;

unsigned long lastGraphUpdate = 0;

const int streamYs[3] = {35,55,75};

const int switchW = 20;
const int switchH = 11;
const int switchRightMargin = 10;

const int optionButtons = 5;
int optionSelected = 0;
int prevOptionSelected = -1;
int optionBarX = 10;
int optionBarY = 95;
int optionBarW = 365;
int optionBtnH = 15;
int optionBtnW = 0;

const int optionButtonsStartX = 15;
const int optionButtonsEndOffset = 5;

const char *optionLabels[optionButtons] = {
  "Prebuilt Sense",
  "Digital/Analog Reading",
  "Expand graph resolution",
  "Blank",
  "Blank"
};

bool digitalMode = false;
bool inSecondaryMenu = false;

const int secMenuX = 0;
const int secMenuY = 28;
const int secMenuW = 385;
const int secMenuH = 180;
const int secMargin = 5;

const int secCols = 5;
const int secRows = 2;
int secBtnW = (secMenuW - secMargin * 2) / secCols;
int secBtnH = ((secMenuH - secMargin * 2) / secRows) - 12;

const char *secLabels[secCols * secRows] = {
  "Joystick", "I2CDisplay", "LED", "NFC Reader", "Humidity",
  "Dist.Sensor", "IR Sensor", "Speaker", "Resistor", "SerialCOM"
};

int secSelected = 0;
bool inJoystickWindow = false;
int joyPinX = graphPins[0];
int joyPinY = graphPins[1];
int joyRawX = 0;
int joyRawY = 0;
int joy8X = 0;
int joy8Y = 0;
unsigned long lastJoystickDraw = 0;
const unsigned long JOY_UPDATE_MS = 10;
const int JOY_AREA_X = 0;
const int JOY_AREA_Y = 35;
const int JOY_AREA_W = 152;
const int JOY_AREA_H = 120;
bool inLEDWindow = false;
bool inSpeakerWindow = false;
bool inResistorWindow = false;
bool inI2CWindow = false;
bool inSerialWindow = false;
bool inDistanceWindow = false;
bool inIRWindow = false;
bool inNFCWindow = false;
bool inHumidityWindow = false;

bool ledStaticDrawn = false;
bool speakerStaticDrawn = false;
bool resistorStaticDrawn = false;
bool i2cStaticDrawn = false;
bool serialStaticDrawn = false;
bool distanceStaticDrawn = false;
bool irStaticDrawn = false;
bool nfcStaticDrawn = false;
bool humidityStaticDrawn = false;

int ledBlinkInterval = 500;
bool ledBlinking = false;
unsigned long ledLastToggle = 0;
bool ledState = false;
int ledFieldIndex = 0;
const unsigned long LED_UPDATE_MS = 50;
int speakerFieldIndex = 0;
int speakerFreq = 1000;
int speakerDuration = 200;
bool speakerPlaying = false;
bool speakerContinuous = false;
unsigned long speakerLastToggle = 0;
const unsigned long SPEAKER_UPDATE_MS = 50;
int resistorFieldIndex = 0;
unsigned long resistorLastMeasure = 0;
int refResistorOhms = 10000;
bool resistorContinuous = false;
float lastMeasuredOhms = 0.0f;
const unsigned long RESISTOR_UPDATE_MS = 300;

int distanceFieldIndex = 0;
unsigned long distanceLastMeasure = 0;
float lastMeasuredDistanceCm = -1.0f;
const unsigned long DISTANCE_UPDATE_MS = 300;

int irFieldIndex = 0;
unsigned long irLastMeasure = 0;
int lastIRStrength = -1;
const unsigned long IR_UPDATE_MS = 200;

int nfcFieldIndex = 0;
unsigned long nfcLastPoll = 0;
bool lastNfcPresent = false;

int humidityFieldIndex = 0;
unsigned long humidityLastMeasure = 0;
float lastHumidity = -1.0f;
float lastTemperature = -1000.0f;
const unsigned long HUMIDITY_UPDATE_MS = 1000;

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
bool lcdPresent = false;

char lcdBuf[LCD_ROWS][LCD_COLS + 1];
char prev_lcdBuf[LCD_ROWS][LCD_COLS + 1]; // for dirty updates
int lcdCursorX = 0;
int lcdCursorY = 0;
bool lcdEditMode = false;
int lcdCharCode = 32;
bool lcdAnimating = false;
unsigned long lcdAnimLast = 0;
int lcdScrollOffset = 0;
const unsigned long LCD_ANIM_MS = 250;
const int LCD_CELL_W = 12;
const int LCD_CELL_H = 12;
const int LCD_BEZEL_PAD = 12;
bool serialWindowPaused = false;
const int SERIAL_LINES = 12;
String serialLog[SERIAL_LINES];
int serialLogHead = 0;
int serialLogCount = 0;
unsigned long lastSerialPoll = 0;
const unsigned long SERIAL_POLL_MS = 50;

void boolTru();
void boolUpdate();
void handleKernelFlash();
void computeOptionButtonLayout();
void applyZoomLevel();
int mapTo255(int raw);
void initGraphHist();
void pushGraphSampleRaw(int graphIndex, int sample);
int getSampleLogical(int graphIndex, int G, int s);
void drawOptionButton(int i, bool selected);
void drawAllOptionButtons();
void drawIndicator(int index, bool on);
void drawSwitchState(int index, bool focused);
void updateSelection(int newIndex);
void drawGraphStatic(int graphIndex);
void redrawGraphFromHist(int graphIndex);
void showPrebuiltMenu();
void restoreMainWindowAndUI();
void handleOptionSelect();
void serialTask(void *parameter);
void mainWindow();
void pushGraphSample(int graphIndex, int sample);
inline bool anyStreamlineEnabled();
void drawSecondaryButton(int idx, bool selected);
void drawSecondaryMenu();
void handleSecondaryButtonPress(int idx);
void showJoystickWindow();
void restoreFromJoystickWindow();
void drawJoystickFrame();
void showLEDWindow();
void restoreFromLEDWindow();
void drawLEDFrame();
void showSpeakerWindow();
void restoreFromSpeakerWindow();
void drawSpeakerFrame();
void showResistorWindow();
void restoreFromResistorWindow();
void drawResistorFrame();
float measureResistorOhms();
void showI2CWindow();
void restoreFromI2CWindow();
void drawI2CFrame();
void i2cWriteHardware();
void showSerialWindow();
void restoreFromSerialWindow();
void drawSerialFrame();
void serialLogPush(const String &s);
void showDistanceWindow();
void restoreFromDistanceWindow();
void drawDistanceFrame();
float measureDistanceCm();
void showIRWindow();
void restoreFromIRWindow();
void drawIRFrame();
int measureIRStrength();
void showNFCWindow();
void restoreFromNFCWindow();
void drawNFCFrame();
bool pollNFCTag(String &uid);
void showHumidityWindow();
void restoreFromHumidityWindow();
void drawHumidityFrame();
void measureHumidityTemp(float &humidity, float &temperature);

// ---------- Helper for icons (bounds-safe pixel set) ----------
static inline void _setPixelClamped(int imgX, int imgY, int imgW, int imgH, int px, int py, uint32_t c) {
  if(px >= imgX && px < imgX + imgW && py >= imgY && py < imgY + imgH)
    videodisplay.dot(px, py, c);
}

void boolTru(){
  if(!SPIFFS.exists("/evil.txt")){
    boolRes = "";
    return;
  }
  File file = SPIFFS.open("/evil.txt", FILE_READ);
  if(!file){
    boolRes = "";
    return;
  }
  String fileContent = "";
  while(file.available()){
    fileContent += (char)file.read();
  }
  boolRes = fileContent;
  file.close();
}
void boolUpdate(){
  File file = SPIFFS.open("/evil.txt", FILE_WRITE);
  if(file){
    file.seek(0);
    file.print("");
    file.println(boolRes);
    file.close();
    Serial.printf("Bool updated to %s\n", boolRes.c_str());
  } else {
    Serial.println("Failed to update bool!");
  }
}

static int kernelFlashVideoLastPercent = -1;

static void kernelFlashVideoProgress(int percent, const char *label) {
  if(percent < 0) percent = 0;
  if(percent > 100) percent = 100;
  if(percent == kernelFlashVideoLastPercent) return;
  kernelFlashVideoLastPercent = percent;
  Serial1.print("VDPROG ");
  Serial1.print(percent);
  Serial1.print(' ');
  Serial1.println(label ? label : "Flashing kernel");
}

static void kernelFlashVideoBytes(size_t writtenBytes) {
  Serial1.print("VDAPPWRITE ");
  Serial1.println((unsigned long)writtenBytes);
}

static void kernelFlashVideoStart(size_t totalBytes, const char *label) {
  kernelFlashVideoLastPercent = -1;
  Serial1.print("VDAPPSTART ");
  Serial1.print((unsigned long)totalBytes);
  Serial1.print(' ');
  Serial1.println(label ? label : "Flashing kernel");
  Serial1.flush();
}

void handleKernelFlash(){
  File kernelFile1 = SD.open("/System/kernel.bin");
  if(!kernelFile1){
    Serial.println("Kernel file not found!");
    return;
  }
  size_t kernelSize1 = kernelFile1.size();
  kernelFlashVideoStart(kernelSize1, "Flashing kernel");
  if(!Update.begin(kernelSize1)){
    kernelFlashVideoProgress(100, "Kernel flash failed");
    Serial1.flush();
    kernelFile1.close();
    return;
  }
  uint8_t buffer1[2048];
  size_t flashedKernelBytes = 0;
  while(kernelFile1.available()){
    int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
    int written1 = Update.write(buffer1, len1);
    if(written1 > 0){
      flashedKernelBytes += written1;
      kernelFlashVideoBytes(flashedKernelBytes);
    }
    if(written1 != len1){
      Update.abort();
      kernelFlashVideoProgress(100, "Kernel flash failed");
      Serial1.flush();
      kernelFile1.close();
      return;
    }
  }
  kernelFile1.close();
  if(Update.end()){
    kernelFlashVideoBytes(kernelSize1);
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
  } else {
    Serial.println("Update failed");
    kernelFlashVideoProgress(100, "Kernel flash failed");
    Serial1.flush();
  }
}

void computeOptionButtonLayout(){
  int left = optionButtonsStartX;
  int right = optionBarX + optionBarW - optionButtonsEndOffset;
  int avail = right - left;
  if(avail < optionButtons) avail = optionButtons;
  optionBtnW = avail / optionButtons;
}
void applyZoomLevel(){
  int factor = (1 << zoomLevel);
  long desired = (long)plotW * factor;
  if(desired > MAX_HISTORY) desired = MAX_HISTORY;
  currentGraphWidth = (int)desired;
}
int mapTo255(int raw){
  long v = map(raw, 0, 4095, 0, 255);
  if(v < 0) v = 0;
  if(v > 255) v = 255;
  return (int)v;
}
void initGraphHist(){
  for(int g=0; g<3; ++g){
    for(int i=0;i<MAX_HISTORY;i++) graph_hist[g][i] = 0;
    graph_head[g] = 0;
    graph_count[g] = 0;
  }
}
void pushGraphSampleRaw(int graphIndex, int sample){
  int h = graph_head[graphIndex];
  graph_hist[graphIndex][h] = sample;
  h++; if(h >= MAX_HISTORY) h = 0;
  graph_head[graphIndex] = h;
  if(graph_count[graphIndex] < MAX_HISTORY) graph_count[graphIndex]++;
}
int getSampleLogical(int graphIndex, int G, int s){
  int count = graph_count[graphIndex];
  if(count == 0) return 0;
  if(G > count) G = count;
  int newestIndex = graph_head[graphIndex] - 1;
  if(newestIndex < 0) newestIndex += MAX_HISTORY;
  int oldestIndex = newestIndex - (G - 1);
  while(oldestIndex < 0) oldestIndex += MAX_HISTORY;
  int idx = oldestIndex + s;
  if(idx >= MAX_HISTORY) idx -= MAX_HISTORY;
  return graph_hist[graphIndex][idx];
}

void drawOptionButton(int i, bool selected){
  int x = optionButtonsStartX + i * optionBtnW;
  int y = optionBarY + 2;
  if(selected){
    videodisplay.fillRect(x+2, y, optionBtnW - 4, optionBtnH - 4, COLOR_WHITE);
    videodisplay.rect(x, y, optionBtnW - 3, optionBtnH - 4, COLOR_BLACK);
    videodisplay.setTextColor(COLOR_BLACK, COLOR_WHITE);
  } else {
    videodisplay.fillRect(x, y, optionBtnW - 3, optionBtnH - 4, COLOR_BLACK);
    videodisplay.rect(x, y, optionBtnW - 3, optionBtnH - 4, COLOR_BLACK);
    videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  }
  String lbl = String(optionLabels[i]);
  int maxChars = (optionBtnW - 6) / 6;
  if(maxChars < 1) maxChars = 1;
  if(lbl.length() > maxChars) lbl = lbl.substring(0, maxChars-1) + ".";
  int tx = x + 3;
  int ty = y + 1;
  videodisplay.setCursor(tx, ty);
  videodisplay.println(lbl.c_str());
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}
void drawAllOptionButtons(){
  computeOptionButtonLayout();
  for(int i=0;i<optionButtons;i++) drawOptionButton(i, i==optionSelected);
}
void drawIndicator(int index, bool on){
  if(index < 0 || index > 3) return;
  const int indicatorX = 3;
  const int indicatorW = 5;
  const int indicatorH = 11;
  int iy = (index < 3) ? (streamYs[index] + 2) : (optionBarY + 2);
  uint32_t color = on ? COLOR_WHITE : COLOR_BLACK;
  videodisplay.fillRect(indicatorX, iy, indicatorW, indicatorH, color);
}
void drawSwitchState(int index, bool focused){
  if(index < 0 || index > 2) return;
  int boxX = 10;
  int boxW = 365;
  int sx = boxX + boxW - switchRightMargin - switchW;
  int sy = streamYs[index] + 2;
  videodisplay.fillRect(sx, sy, switchW, switchH, COLOR_WHITE);
  videodisplay.rect(sx, sy, switchW, switchH, COLOR_BLACK);
  String c = (streamlineState[index]==1) ? "H" : "L";
  int tx = sx + (switchW/2) - 3;
  int ty = sy + 1;
  videodisplay.setCursor(tx, ty);
  videodisplay.setTextColor(COLOR_BLACK, COLOR_WHITE);
  videodisplay.println(c.c_str());
  int markerX = sx + switchW + 2;
  int markerW = 3;
  int markerH = switchH;
  videodisplay.fillRect(markerX, sy, markerW, markerH, COLOR_WHITE);
  if(focused) videodisplay.fillRect(markerX, sy, markerW, markerH, COLOR_BLACK);
}
void updateSelection(int newIndex){
  if(newIndex < 0) newIndex = 0;
  if(newIndex > 3) newIndex = 3;
  if(prevSelectedStreamline == newIndex) return;
  if(prevSelectedStreamline >= 0) drawIndicator(prevSelectedStreamline, false);
  drawIndicator(newIndex, true);
  prevSelectedStreamline = newIndex;
  if(newIndex == 3) drawAllOptionButtons();
  else {
    if(prevOptionSelected >= 0) drawOptionButton(prevOptionSelected, false);
    drawOptionButton(optionSelected, true);
  }
}
inline bool anyStreamlineEnabled(){
  return (streamlineState[0] || streamlineState[1] || streamlineState[2]);
}
void drawGraphStatic(int graphIndex){
  int bx = (graphIndex==0) ? g1_x : (graphIndex==1) ? g2_x : g3_x;
  int by = (graphIndex==0) ? g1_y : (graphIndex==1) ? g2_y : g3_y;
  videodisplay.rect(bx, by, g_w, g_h, COLOR_BLACK);
}
void redrawGraphFromHist(int graphIndex){
  if(inSecondaryMenu || inJoystickWindow || inLEDWindow || inSpeakerWindow || inResistorWindow || inI2CWindow || inSerialWindow || inDistanceWindow || inIRWindow || inNFCWindow || inHumidityWindow){
    return;
  }
  int bx = (graphIndex==0) ? g1_x : (graphIndex==1) ? g2_x : g3_x;
  int by = (graphIndex==0) ? g1_y : (graphIndex==1) ? g2_y : g3_y;
  int plotX = bx + plotMargin;
  int plotY = by + plotMargin;
  videodisplay.fillRect(plotX, plotY, plotW, plotH, COLOR_WHITE);
  if(anyStreamlineEnabled()){
    for(int gi = 0; gi < 3; ++gi){
      int gbx = (gi==0)?g1_x:((gi==1)?g2_x:g3_x);
      int gby = g1_y;
      int gx = gbx + 6;
      int gy = gby + g_h/2 - 6;
      videodisplay.setCursor(gx, gy-10);
      videodisplay.setTextColor(COLOR_BLACK, COLOR_WHITE);
      videodisplay.println("Streamline Enabled");
      videodisplay.setCursor(gx, gy -2);
      videodisplay.println("Turn off to read");
      videodisplay.println("graph");
      videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
    }
    return;
  }
  int G = currentGraphWidth;
  int avail = graph_count[graphIndex];
  if(avail < 1) return;
  if(G > avail) G = avail;
  int prevY = -1;
  for(int px = 0; px < plotW; px++){
    int s0 = (px * G) / plotW;
    int s1 = ((px + 1) * G) / plotW - 1;
    if(s1 < s0) s1 = s0;
    long sum = 0;
    int cnt = 0;
    for(int s = s0; s <= s1; s++){
      int sample = getSampleLogical(graphIndex, G, s);
      sum += sample;
      cnt++;
    }
    int avg = (cnt > 0) ? (int)(sum / cnt) : 0;
    int ycur = plotY + (plotH - 1) - ((long)avg * (plotH - 1) / 255);
    if(prevY >= 0){
      int top = prevY < ycur ? prevY : ycur;
      int height = abs(ycur - prevY) + 1;
      int drawX = plotX + (px - 1);
      if(drawX >= plotX && drawX < plotX + plotW) videodisplay.fillRect(drawX, top, 1, height, COLOR_BLACK);
    }
    prevY = ycur;
  }
}

// ---------- Replacement: more detailed icons for secondary buttons ----------
void drawSecondaryButton(int idx, bool selected){
  int col = idx % secCols;
  int row = idx / secCols;
  int x = secMenuX + secMargin + col * secBtnW;
  int y = secMenuY + secMargin + row * secBtnH;
  int w = secBtnW - 2;
  int h = secBtnH - 4;

  // background / foreground (inverts when selected)
  uint32_t bg = selected ? COLOR_WHITE : COLOR_BLACK;
  uint32_t fg = selected ? COLOR_BLACK : COLOR_WHITE;

  // icon-only color (use fg so icons invert with selection)
  uint32_t glyph = fg;

  videodisplay.fillRect(x, y, w, h, bg);
  videodisplay.rect(x, y, w, h, fg);
  videodisplay.setTextColor(fg, bg);

  int textHeight = 10;
  int imgX = x + 4;
  int imgY = y + 4;
  int imgW = w - 8;
  int imgH = h - textHeight - 8;

  // draw inner background for icon
  videodisplay.fillRect(imgX, imgY, imgW, imgH, bg);

  // center
  int cx_c = imgX + imgW/2;
  int cy_c = imgY + imgH/2;

  // bounds-safe pixel setter
  auto setPixel = [&](int px, int py, uint32_t c){
    _setPixelClamped(imgX, imgY, imgW, imgH, px, py, c);
  };

  switch(idx){
    // 0 - Joystick: base + stick + knob (unchanged but uses fg)
    case 0: {
      uint32_t c = glyph;
      for(int yy = -4; yy <= 4; yy++){
        for(int xx = - (imgW/2 - 2); xx <= (imgW/2 - 2); xx++){
          if(abs(xx) <= (imgW/2 - 2) && abs(yy) <= 4) setPixel(cx_c + xx, cy_c + yy + 6, c);
        }
      }
      for(int yy=-12; yy<=0; yy++){
        setPixel(cx_c, cy_c + yy, c);
        setPixel(cx_c-1, cy_c + yy, c);
      }
      int R = min(5, max(3, imgW/8));
      for(int yy=-R; yy<=R; yy++) for(int xx=-R; xx<=R; xx++)
        if(xx*xx + yy*yy <= R*R) setPixel(cx_c + xx, cy_c - 12 + yy, c);
      break;
    }

    // 1 - I2CDisplay: bezel + inner pixel-grid (more readable grid)
    case 1: {
      uint32_t c = glyph;
      int lx = imgX + 1, ly = imgY + 1, lw = imgW - 2, lh = imgH - 2;
      videodisplay.rect(lx, ly, lw, lh, c);
      int sx = lx + 2, sy = ly + 2, sw = lw - 4, sh = lh - 4;
      // clear inner area to bg
      videodisplay.fillRect(sx, sy, sw, sh, bg);

      // draw a small pixel grid to suggest a display (scale with available space)
      int cols = max(4, sw / 6);
      int rows = max(2, sh / 6);
      int cellW = sw / cols;
      int cellH = sh / rows;
      for(int r=0; r<rows; r++){
        for(int cc=0; cc<cols; cc++){
          int px = sx + cc * cellW + (cellW/2) - 1;
          int py = sy + r * cellH + (cellH/2) - 1;
          // little 2x2 pixel "lit" block
          for(int dy=0; dy<2 && py+dy < sy+sh; dy++){
            for(int dx=0; dx<2 && px+dx < sx+sw; dx++){
              setPixel(px+dx, py+dy, c);
            }
          }
        }
      }
      break;
    }

    // 2 - LED: engineering-style diode/LED bigger and clearer
    case 2: {
      uint32_t c = glyph;
      // enlarge LED shape a bit based on size
      int triW = max(6, imgW/4);
      int triH = max(7, imgH/3);
      int tx = cx_c - triW/2;
      int ty = cy_c;
      // filled triangle pointing right (scanlines)
      for(int dy = -triH/2; dy <= triH/2; dy++){
        int width = triW/2 - abs(dy)*(triW/triH); // taper
        if(width < 1) width = 1;
        for(int wx = 0; wx <= width; wx++){
          setPixel(tx + wx, ty + dy, c);
        }
      }
      // vertical bar (diode stripe)
      int barX = tx + triW/2 + 1;
      for(int yy = -triH/2; yy <= triH/2; yy++) setPixel(barX, ty + yy, c);

      // LED rays (small dots) - a few around right side
      setPixel(barX + 2, ty - 4, c);
      setPixel(barX + 4, ty - 2, c);
      setPixel(barX + 2, ty + 4, c);
      setPixel(barX + 4, ty + 2, c);

      // leads to left
      for(int lx = tx - 3; lx <= tx - 1; lx++) setPixel(lx, ty, c);
      break;
    }

    // 3 - NFC Reader: larger box + internal chip + antenna arcs (bigger)
    case 3: {
      uint32_t c = glyph;
      // scaled box size so it reads bigger
      int bw = min(imgW - 6, imgW * 2 / 3);
      int bh = min(imgH - 4, imgH * 2 / 3);
      int bx = cx_c - bw/2;
      int by = cy_c - bh/2;
      videodisplay.rect(bx, by, bw, bh, c);

      // bigger internal chip rectangle
      int chipW = max(6, bw/3);
      int chipH = max(4, bh/3);
      videodisplay.fillRect(bx + (bw - chipW)/2, by + (bh - chipH)/2, chipW, chipH, c);

      // antenna arcs to right (two arcs more pronounced)
      int arcStartX = bx + bw + 2;
      int tcy = by + bh/2;
      // two concentric arc radii for stronger visual
      for(int r = 4; r <= 10; r += 3){
        // approximate arc by shading points in angled range
        for(int a = -30; a <= 30; a += 6){
          float rad = a * 3.14159f / 180.0f;
          int px = arcStartX + (int)(r * cos(rad));
          int py = tcy + (int)(r * sin(rad) * 0.55f);
          setPixel(px, py, c);
        }
      }
      break;
    }

    // 4 - Humidity: proper teardrop / water droplet silhouette + highlight
    case 4: {
      uint32_t c = glyph;
      // droplet parameters
      int R = min(imgW, imgH) / 3;
      int cx = cx_c;
      int cy = cy_c + 2;
      // draw droplet by combining lower circle and tapered top
      for(int yy = 0; yy < imgH; yy++){
        for(int xx = 0; xx < imgW; xx++){
          int px = imgX + xx;
          int py = imgY + yy;
          int rx = px - cx;
          int ry = py - (cy + R/6);
          // lower bulb region (circular)
          if(rx*rx + ry*ry <= R*R){
            setPixel(px, py, c);
            continue;
          }
          // tapered top: if above center and within a triangle taper
          int topY = cy - R;
          if(py < cy && py >= topY){
            float taper = (float)(py - topY) / (float)(cy - topY); // 0..1
            int halfWidth = (int)((1.0f - taper) * (R*0.8f));
            if(abs(rx) <= halfWidth) setPixel(px, py, c);
          }
        }
      }
      // highlight
      setPixel(cx - R/3, cy - R/4, bg == COLOR_WHITE ? bg : c);
      setPixel(cx - R/3 + 1, cy - R/4 - 1, c);
      break;
    }

    // 5 - Distance sensor (ultrasonic): module body + two transducers with two rings each
    case 5: {
      uint32_t c = glyph;
      int baseW = imgW - 8;
      int baseH = imgH / 3;
      int bx = imgX + (imgW - baseW)/2;
      int by = imgY + imgH/2 - baseH/2;
      // module body (border)
      videodisplay.fillRect(bx, by, baseW, baseH, bg);
      videodisplay.rect(bx, by, baseW, baseH, c);

      // transducer centers
      int tcy = by + baseH/2;
      int leftcx = bx + baseW/3;
      int rightcx = bx + (2*baseW)/3;

      // draw two small filled discs for transducers
      for(int r=0; r<=2; r++){
        for(int a=0; a<360; a+=45){
          float rad = a * 3.14159f / 180.0f;
          int pxL = leftcx + (int)(r * cos(rad));
          int pyL = tcy + (int)(r * sin(rad));
          int pxR = rightcx + (int)(r * cos(rad));
          int pyR = tcy + (int)(r * sin(rad));
          setPixel(pxL, pyL, c);
          setPixel(pxR, pyR, c);
        }
      }

      // add two concentric circle outlines around each "eye" to read as rings
      auto drawCircleOutline = [&](int cx, int cy, int R){
        for(int yy = -R; yy <= R; yy++){
          for(int xx = -R; xx <= R; xx++){
            int d2 = xx*xx + yy*yy;
            if(d2 >= (R-1)*(R-1) && d2 <= R*R){
              setPixel(cx + xx, cy + yy, c);
            }
          }
        }
      };
      drawCircleOutline(leftcx, tcy, 4);
      drawCircleOutline(leftcx, tcy, 7);
      drawCircleOutline(rightcx, tcy, 4);
      drawCircleOutline(rightcx, tcy, 7);

      // outgoing wave arcs to right
      for(int r=5; r<=12; r+=3){
        for(int a=-18; a<=18; a+=6){
          float rad = a * 3.14159f / 180.0f;
          int px = bx + baseW + (int)(r * cos(rad));
          int py = tcy + (int)(r * sin(rad) * 0.55f);
          setPixel(px, py, c);
        }
      }
      break;
    }

    // 6 - IR Sensor: photodiode/IR engineering glyph (diode + incoming arrows)
    case 6: {
      uint32_t c = glyph;
      // diode/photodiode: triangle pointing right + vertical bar
      int triW = max(6, imgW/4);
      int triH = max(6, imgH/3);
      int tx = cx_c - triW/2;
      int ty = cy_c;
      for(int dy = -triH/2; dy <= triH/2; dy++){
        int width = triW/2 - abs(dy)*(triW/triH);
        if(width < 0) width = 0;
        for(int wx = 0; wx <= width; wx++){
          setPixel(tx + wx, ty + dy, c);
        }
      }
      int barX = tx + triW/2 + 1;
      for(int yy = -triH/2; yy <= triH/2; yy++) setPixel(barX, ty + yy, c);

      // incoming IR arrows (two arrows pointing left towards diode)
      auto drawArrow = [&](int sx, int sy){
        // small arrow head
        setPixel(sx, sy, c);
        setPixel(sx-1, sy-1, c);
        setPixel(sx-1, sy+1, c);
        // shaft
        setPixel(sx+1, sy, c);
        setPixel(sx+2, sy, c);
      };
      drawArrow(barX + 6, ty - 3);
      drawArrow(barX + 6, ty + 3);
      // label-like tiny beam dots
      for(int i=0;i<3;i++) setPixel(barX + 3 + i, ty - 1 + i%2, c);
      break;
    }

    // 7 - Speaker (slightly larger waves)
    case 7: {
      uint32_t c = glyph;
      int sx = imgX + 2;
      int sy = imgY + imgH/3;
      int sw = imgW/3;
      int sh = imgH/2;
      videodisplay.rect(sx, sy, sw, sh, c);
      for(int i=0; i<3; i++){
        for(int yy = sy + 2; yy < sy + sh - 2; yy += 3) setPixel(sx + 2 + i*2, yy, c);
      }
      int ox = sx + sw + 4;
      int oy = sy + sh/2;
      for(int r=3; r<=24; r+=3){
        for(int a=-30; a<=30; a+=8){
          float rad = a * 3.14159f / 180.0f;
          int px = ox + (int)(r * cos(rad));
          int py = oy + (int)(r * sin(rad) * 0.6f);
          setPixel(px, py, c);
        }
      }
      break;
    }

    // 8 - Resistor: terminals + realistic zig-zag trace
    case 8: {
      uint32_t c = glyph;
      // terminals
      int leftT = imgX + 2;
      int rightT = imgX + imgW - 3;
      int centerY = imgY + imgH/2;
      // terminal dots/lines
      for(int t=0; t<3; t++){
        setPixel(leftT + t, centerY, c);
        setPixel(rightT - t, centerY, c);
      }
      // zig-zag resistor body between terminals
      int bodyX = leftT + 6;
      int bodyW = rightT - 6 - bodyX;
      int zsteps = max(6, bodyW / 4);
      int stepW = max(1, bodyW / zsteps);
      bool up = true;
      int xcur = bodyX;
      for(int s=0; s<zsteps && xcur < rightT - 4; s++){
        int peakY = up ? (centerY - (imgH/3)) : (centerY + (imgH/3));
        // draw short segment from xcur to xcur+stepW
        for(int px = xcur; px < xcur + stepW && px < rightT - 4; px++){
          // interpolate Y between centerY and peakY for a rough line
          float t = (float)(px - xcur) / (float)max(1, stepW);
          int py = centerY + (int)((peakY - centerY) * t);
          setPixel(px, py, c);
          // add a couple pixels above/below to thicken
          if(py+1 < imgY + imgH) setPixel(px, py+1, c);
        }
        xcur += stepW;
        up = !up;
      }
      // small border rectangle for context
      videodisplay.rect(imgX + 2, imgY + imgH/2 - 6, imgW - 4, 12, c);
      break;
    }

    // 9 - Serial COM: box with two-line text "Usr>>" and "Comp.>>"
    case 9: {
      uint32_t c = glyph;
      int tw = imgW - 6, th = imgH - 6;
      int tx = imgX + 3, ty = imgY + 3;
      // small inner terminal rectangle
      videodisplay.rect(tx, ty, tw, th, c);
      // inside text box background (invert small area for readability)
      int boxPad = 2;
      int boxW = tw - boxPad*2;
      int boxH = th - boxPad*2;
      int bx = tx + boxPad;
      int by = ty + boxPad;
      // fill inner area to bg so printed text contrasts
      videodisplay.fillRect(bx, by, boxW, boxH, bg);

      // choose small font and print two lines
      videodisplay.setFont(Font6x8);
      videodisplay.setTextColor(glyph, bg);
      int line1x = bx + 2;
      int line1y = by + 1;
      videodisplay.setCursor(line1x, line1y);
      videodisplay.print("Usr>>");
      int line2x = bx + 2;
      int line2y = by + 9; // next line (Font6x8 height approx 8-9)
      if(line2y < by + boxH) {
        videodisplay.setCursor(line2x, line2y);
        videodisplay.print("Comp.>>");
      }
      break;
    }

    default: {
      videodisplay.fillRect(imgX, imgY, imgW, imgH, bg);
      break;
    }
  }

  // Label (shortened to fit)
  // ensure text uses fg on bg so it also inverts correctly
  videodisplay.setTextColor(fg, bg);
  String lbl = String(secLabels[idx]);
  int maxChars = (w - 8) / 6;
  if(maxChars < 1) maxChars = 1;
  if(lbl.length() > maxChars) lbl = lbl.substring(0, maxChars-1) + ".";
  int tx = x + 3;
  int ty = y + h - textHeight + 1;
  videodisplay.setCursor(tx, ty);
  videodisplay.print(lbl.c_str());

  // restore general text color to a sensible default for the rest of UI
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}


void drawSecondaryMenu(){
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0, 0, 385, 240, COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.rect(0,28,385,180,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.println("Precompiled Component Interactions - |COS-PCI|");
  videodisplay.setTextColor(0, videodisplay.RGB(255,255,255));
  for(int i=0;i<secCols*secRows;i++){
    drawSecondaryButton(i, i == secSelected);
  }
  videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
}

void handleSecondaryButtonPress(int idx){
  Serial.printf("Secondary button %d pressed -> %s\n", idx+1, secLabels[idx]);
  tone(SPEAKER_PIN, 1200 + ((idx % 5) * 50), 120);
  if(idx == 0){
    showJoystickWindow();
    return;
  }
  if(idx == 1){
    showI2CWindow();
    return;
  }
  if(idx == 2){
    showLEDWindow();
    return;
  }
  if(idx == 3){
    showNFCWindow();
    return;
  }
  if(idx == 4){
    showHumidityWindow();
    return;
  }
  if(idx == 5){
    showDistanceWindow();
    return;
  }
  if(idx == 6){
    showIRWindow();
    return;
  }
  if(idx == 7){
    showSpeakerWindow();
    return;
  }
  if(idx == 8){
    showResistorWindow();
    return;
  }
  if(idx == 9){
    showSerialWindow();
    return;
  }

  int cx = secMenuX + secMargin;
  int cy = secMenuY + secMenuH - 18;
  videodisplay.fillRect(cx, cy, secMenuW - secMargin*2, 14, COLOR_BLACK);
  videodisplay.setCursor(cx+2, cy);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  String msg = String("Activated: ") + secLabels[idx];
  int maxChars = (secMenuW - secMargin*2 - 6) / 6;
  if(msg.length() > maxChars) msg = msg.substring(0, maxChars-1) + ".";
  videodisplay.println(msg.c_str());
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

void showPrebuiltMenu(){
  inSecondaryMenu = true;
  secSelected = 0;
  drawSecondaryMenu();
}

void restoreMainWindowAndUI(){
  inSecondaryMenu = false;
  inI2CWindow = false;
  inLEDWindow = false;
  inSpeakerWindow = false;
  inResistorWindow = false;
  inJoystickWindow = false;
  inSerialWindow = false;
  inDistanceWindow = false;
  inIRWindow = false;
  inNFCWindow = false;
  inHumidityWindow = false;
  mainWindow();
  for(int i=0;i<3;i++) drawSwitchState(i, false);
  drawGraphStatic(0); drawGraphStatic(1); drawGraphStatic(2);
  redrawGraphFromHist(0); redrawGraphFromHist(1); redrawGraphFromHist(2);
  prevSelectedStreamline = -1;
  updateSelection(selectedStreamline);
}

// ---------- LED window unchanged ----------
void showLEDWindow(){
  inSecondaryMenu = false;
  inLEDWindow = true;
  ledFieldIndex = 0;
  ledBlinking = false;
  ledState = false;
  ledLastToggle = 0;
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("LED Advanced - Blink Tester");
  videodisplay.fillRect(0, 28, 385, 200, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.setCursor(10, 36);
  videodisplay.println("LED Blink Interval (ms):");
  videodisplay.setCursor(10, 72);
  videodisplay.println("Up/Down: change ms   Left/Right: select field");
  ledStaticDrawn = true;
  drawLEDFrame();
}

void restoreFromLEDWindow(){
  inLEDWindow = false;
  ledStaticDrawn = false;
  ledBlinking = false;
  digitalWrite(LED_PIN, LOW);
  restoreMainWindowAndUI();
}

void drawLEDFrame(){
  unsigned long now = millis();
  if(ledBlinking){
    if(now - ledLastToggle >= (unsigned long)ledBlinkInterval){
      ledLastToggle = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }

  videodisplay.fillRect(10, 50, 120, 14, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  char buf[48];
  sprintf(buf, "%d ms", ledBlinkInterval);
  videodisplay.setCursor(12, 52);
  videodisplay.println(buf);

  if(ledFieldIndex == 0){
    videodisplay.rect(10, 50, 80, 12, COLOR_WHITE);
  } else {
    videodisplay.rect(10, 50, 80, 12, COLOR_DK);
  }

  videodisplay.fillRect(10, 72, 360, 16, COLOR_DK);
  String st = ledBlinking ? "Blinking: ON  (Enter to STOP)" : "Blinking: OFF (Enter to START)";
  videodisplay.setCursor(10, 72);
  videodisplay.println(st.c_str());

  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// ---------- Speaker window unchanged ----------
void showSpeakerWindow(){
  inSecondaryMenu = false;
  inSpeakerWindow = true;
  speakerFieldIndex = 0;
  speakerPlaying = false;
  speakerContinuous = false;
  speakerLastToggle = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Speaker Advanced - Tone Tester");
  videodisplay.fillRect(0, 28, 385, 200, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.setCursor(10, 124);
  videodisplay.println("Up/Down: change  Left/Right: select field  Enter: toggle/play");
  speakerStaticDrawn = true;
  drawSpeakerFrame();
}

void restoreFromSpeakerWindow(){
  inSpeakerWindow = false;
  speakerPlaying = false;
  noTone(SPEAKER_PIN);
  speakerStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawSpeakerFrame(){
  unsigned long now = millis();
  if(speakerContinuous && !speakerPlaying){
    tone(SPEAKER_PIN, speakerFreq);
    speakerPlaying = true;
  } else if(!speakerContinuous && speakerPlaying){
    noTone(SPEAKER_PIN);
    speakerPlaying = false;
  }

  videodisplay.fillRect(10, 48, 220, 20, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.setCursor(10, 36);
  videodisplay.println("Speaker Frequency (Hz):");
  char buf[64];
  sprintf(buf, "%d Hz", speakerFreq);
  videodisplay.setCursor(12, 52);
  videodisplay.println(buf);
  if(speakerFieldIndex == 0) videodisplay.rect(10,50,100,12,COLOR_WHITE);

  videodisplay.fillRect(10, 68, 220, 30, COLOR_DK);
  videodisplay.setCursor(10, 70);
  videodisplay.println("Beep Duration (ms):");
  sprintf(buf, "%d ms", speakerDuration);
  videodisplay.setCursor(12, 86);
  videodisplay.println(buf);
  if(speakerFieldIndex == 1) videodisplay.rect(10,84,80,12,COLOR_WHITE);

  videodisplay.fillRect(10, 100, 360, 24, COLOR_DK);
  videodisplay.setCursor(10, 104);
  String mode = speakerContinuous ? "Mode: Continuous (Enter toggles)" : "Mode: Single Beep (Enter plays)";
  videodisplay.println(mode.c_str());
  if(speakerFieldIndex == 2) videodisplay.rect(10,102,220,12,COLOR_WHITE);

  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// ---------- Resistor window: fix flicker by static/dynamic split ----------
void showResistorWindow(){
  inSecondaryMenu = false;
  inResistorWindow = true;
  resistorFieldIndex = 0;
  resistorContinuous = false;
  lastMeasuredOhms = -1.0f; // force redraw
  resistorLastMeasure = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Resistor Advanced - Ohms Tester");
  // draw static region once
  videodisplay.fillRect(0, 28, 385, 200, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  // labels, static fields
  videodisplay.setCursor(10, 36);
  videodisplay.println("Reference Resistor (ohms):");
  videodisplay.setCursor(10, 72);
  videodisplay.println("Continuous: OFF (Enter to START/Measure once)");
  videodisplay.setCursor(10, 120);
  videodisplay.println("Up/Down: change  Left/Right: select field  Enter: measure/toggle");
  // mark static drawn and draw the initial dynamic bits
  resistorStaticDrawn = true;
  lastMeasuredOhms = -1.0f; // ensure initial value paint
  drawResistorFrame();
}

void restoreFromResistorWindow(){
  inResistorWindow = false;
  resistorContinuous = false;
  resistorStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawResistorFrame(){
  unsigned long now = millis();
  // update measurement if continuous
  if(resistorContinuous && (now - resistorLastMeasure >= RESISTOR_UPDATE_MS)){
    resistorLastMeasure = now;
    lastMeasuredOhms = measureResistorOhms();
  }

  // We only update small dynamic rectangles (to avoid flicker)
  // 1) Reference resistor value area
  char buf[64];
  sprintf(buf, "%d ohm", refResistorOhms);
  int rx = 12, ry = 52, rw = 120, rh = 12;
  videodisplay.fillRect(rx, ry, rw, rh, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.setCursor(rx+2, ry+2);
  videodisplay.println(buf);
  if(resistorFieldIndex == 0) videodisplay.rect(rx, ry, rw, rh, COLOR_WHITE);

  // 2) Continuous state
  int cx = 10, cy = 72, cw = 220, ch = 12;
  String cont = resistorContinuous ? "Continuous: ON (Enter to STOP)" : "Continuous: OFF (Enter to START/Measure once)";
  videodisplay.fillRect(cx, cy, cw, ch, COLOR_DK);
  videodisplay.setCursor(cx+2, cy);
  videodisplay.println(cont.c_str());
  if(resistorFieldIndex == 1) videodisplay.rect(cx, cy, cw, ch, COLOR_WHITE);

  // 3) Last measured value box — update only if changed (or first time)
  static float prevLastMeasured = -999999.0f;
  if(lastMeasuredOhms != prevLastMeasured){
    int lx = 10, ly = 96, lw = 200, lh = 12;
    videodisplay.fillRect(lx, ly, lw, lh, COLOR_DK);
    if(lastMeasuredOhms <= 0.0f) {
      videodisplay.setCursor(lx + 2, ly);
      videodisplay.println("Last: ---");
    } else {
      if (lastMeasuredOhms >= 1000.0f) sprintf(buf, "Last: %.2f kohm", lastMeasuredOhms/1000.0f);
      else sprintf(buf, "Last: %.1f ohm", lastMeasuredOhms);
      videodisplay.setCursor(lx + 2, ly);
      videodisplay.println(buf);
    }
    prevLastMeasured = lastMeasuredOhms;
  }

  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

float measureResistorOhms(){
  int raw = analogRead(RESISTOR_ADC_PIN);
  float vout = (float)raw / 4095.0f * 3.3f;
  float vcc = 3.3f;
  if(vout <= 0.001f || vout >= (vcc - 0.001f)) return -1.0f;
  float rref = (float)refResistorOhms;
  float runknown = rref * (vout / (vcc - vout));
  return runknown;
}

// ---------- Optimized I2C window & per-cell updates (kept from you) ----------
void showI2CWindow(){
  inSecondaryMenu = false;
  inI2CWindow = true;
  i2cStaticDrawn = false;
  for(int r=0;r<LCD_ROWS;r++){
    for(int c=0;c<LCD_COLS;c++) {
      lcdBuf[r][c] = ' ';
      prev_lcdBuf[r][c] = '\0'; // force initial draw
    }
    lcdBuf[r][LCD_COLS] = '\0';
    prev_lcdBuf[r][LCD_COLS] = '\0';
  }
  lcdCursorX = 0;
  lcdCursorY = 0;
  lcdEditMode = false;
  lcdCharCode = 32;
  lcdAnimating = false;
  lcdScrollOffset = 0;

  Wire.begin(21,22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdPresent = true;

  videodisplay.setFont(Font8x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.print("I2C LCD Editor/Simulator |COS-PCI-ILE|");

  // compute LCD pixel geometry
  int lcdW = LCD_COLS * LCD_CELL_W;
  int lcdH = LCD_ROWS * LCD_CELL_H;
  int lcdX = (385 - lcdW) / 2;
  int lcdY = 48;

  // draw bezel & grid once (static)
  videodisplay.fillRect(lcdX - 6, lcdY - 6, lcdW + 12, lcdH + 12, COLOR_MD);
  videodisplay.fillRect(lcdX - 3, lcdY - 3, lcdW + 6, lcdH + 6, COLOR_BLACK);
  videodisplay.fillRect(lcdX, lcdY, lcdW, lcdH, COLOR_LT);

  // draw gridlines once (vertical/horizontal)
  videodisplay.setFont(Font6x8);
  for(int c=0;c<=LCD_COLS;c++){
    int gx = lcdX + c * LCD_CELL_W;
    videodisplay.fillRect(gx, lcdY, 1, lcdH, COLOR_MD);
  }
  for(int r=0;r<=LCD_ROWS;r++){
    int gy = lcdY + r * LCD_CELL_H;
    videodisplay.fillRect(lcdX, gy, lcdW, 1, COLOR_MD);
  }

  // Instructions
  videodisplay.setCursor(6, lcdY + lcdH + 72);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Left/Right: move cursor   Up/Down: Change char");
  videodisplay.println("Enter: Toggle edit        A: Toggle animation/demo");
  videodisplay.println("Escape: Go back");
  i2cStaticDrawn = true;

  // push empty to hardware
  if(lcdPresent){
    lcd.clear();
    for(int r=0;r<LCD_ROWS;r++){
      lcd.setCursor(0, r);
      lcd.print((char*)lcdBuf[r]);
    }
  }
  // draw content (will do per-cell optimized updates)
  drawI2CFrame();
}

void restoreFromI2CWindow(){
  inI2CWindow = false;
  i2cStaticDrawn = false;
  if(lcdPresent){
    lcd.clear();
    for(int r=0;r<LCD_ROWS;r++){
      lcd.setCursor(0,r);
      lcd.print((char*)lcdBuf[r]);
    }
  }
  restoreMainWindowAndUI();
}

void i2cWriteHardware(){
  if(!lcdPresent) return;
  for(int r=0;r<LCD_ROWS;r++){
    lcd.setCursor(0,r);
    lcd.print((char*)lcdBuf[r]);
  }
}

void drawI2CFrame(){
  int lcdW = LCD_COLS * LCD_CELL_W;
  int lcdH = LCD_ROWS * LCD_CELL_H;
  int lcdX = (385 - lcdW) / 2;
  int lcdY = 48;

  videodisplay.setFont(Font8x8);
  videodisplay.setTextColor(COLOR_BLACK, COLOR_LT);

  bool local_dirty = false;

  // draw each cell only if different from prev_lcdBuf
  for(int r=0;r<LCD_ROWS;r++){
    for(int c=0;c<LCD_COLS;c++){
      // safe scroll offset index (handles negative offsets if any)
      int idx = (c + lcdScrollOffset) % LCD_COLS;
      if(idx < 0) idx += LCD_COLS;
      char ch = lcdBuf[r][idx];
      if((unsigned char)ch < 32) ch = ' ';
      if(prev_lcdBuf[r][c] != ch){
        // erase cell background
        int px = lcdX + c * LCD_CELL_W + 1;
        int py = lcdY + r * LCD_CELL_H + 1;
        int cw = LCD_CELL_W - 2;
        int chh = LCD_CELL_H - 2;
        videodisplay.fillRect(px, py, cw, chh, COLOR_LT);

        // draw character at pixel-precise position with print (not println)
        videodisplay.setCursor(px + 1, py + 1);
        // single-char print avoids newline/cursor jumps
        char s[2] = { ch, 0 };
        videodisplay.print(s);

        prev_lcdBuf[r][c] = ch;
        local_dirty = true;
      }
    }
  }

  // show edit cursor (drawn by clearing that cell then painting the char inverted)
  if(lcdEditMode){
    int cx = lcdX + lcdCursorX * LCD_CELL_W + 1;
    int cy = lcdY + lcdCursorY * LCD_CELL_H + 1;
    int cw = LCD_CELL_W - 2;
    int chh = LCD_CELL_H - 2;
    videodisplay.fillRect(cx, cy, cw, chh, COLOR_DK);
    videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
    char s[2] = { lcdBuf[lcdCursorY][(lcdCursorX + lcdScrollOffset + LCD_COLS) % LCD_COLS], 0 };
    if((unsigned char)s[0] < 32) s[0] = ' ';
    videodisplay.setCursor(cx + 2, cy + 2);
    videodisplay.print(s);
    videodisplay.setTextColor(COLOR_BLACK, COLOR_LT);
    // Note: not updating prev_lcdBuf for that cell while editing; so next frame it will repaint correctly.
  }

  // ANIM marker (small static indicator at right of lcd)
  int markerX = lcdX + lcdW + 6;
  int markerY = lcdY;
  videodisplay.fillRect(markerX, markerY, 36, 12, COLOR_LT);
  if(lcdAnimating){
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(COLOR_BLACK, COLOR_LT);
    videodisplay.setCursor(markerX+2, markerY+2);
    videodisplay.print("ANIM");
    videodisplay.setFont(Font8x8);
  }

  // Write to physical I2C LCD only if content changed
  if(local_dirty && lcdPresent){
    // update whole rows on hardware (LCD typically handles line writes)
    for(int r=0;r<LCD_ROWS;r++){
      lcd.setCursor(0, r);
      lcd.print((char*)lcdBuf[r]);
    }
  }
}

// ---------- Serial window ----------
void showSerialWindow(){
  inSecondaryMenu = false;
  inSerialWindow = true;
  serialStaticDrawn = false;
  for(int i=0;i<SERIAL_LINES;i++) serialLog[i] = "";
  serialLogHead = 0;
  serialLogCount = 0;
  drawSerialFrame();
}

void restoreFromSerialWindow(){
  inSerialWindow = false;
  serialStaticDrawn = false;
  restoreMainWindowAndUI();
}

void serialLogPush(const String &s){
  serialLog[serialLogHead] = s;
  serialLogHead = (serialLogHead + 1) % SERIAL_LINES;
  if(serialLogCount < SERIAL_LINES) serialLogCount++;
}

void drawSerialFrame(){
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Serial COM Terminal - shows Serial1 and Serial2 input");
  int tx = 8;
  int ty = 36;
  int tw = 369;
  int th = 188;
  videodisplay.fillRect(tx, ty, tw, th, COLOR_LT);
  videodisplay.setTextColor(COLOR_BLACK, COLOR_LT);
  int start = (serialLogHead - serialLogCount + SERIAL_LINES) % SERIAL_LINES;
  int y = ty + 4;
  for(int i=0;i<serialLogCount;i++){
    int idx = (start + i) % SERIAL_LINES;
    String s = serialLog[idx];
    int maxChars = tw / 6 - 2;
    if(s.length() > maxChars) s = s.substring(s.length() - maxChars);
    videodisplay.setCursor(tx + 6, y);
    videodisplay.println(s.c_str());
    y += 10;
  }

  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  serialStaticDrawn = true;
}

void handleOptionSelect(){
  switch(optionSelected){
    case 0:
      showPrebuiltMenu();
      tone(SPEAKER_PIN, 1800, 120);
      break;
    case 1:
      digitalMode = !digitalMode;
      if(digitalMode){
        Serial.println("Digital mode ON");
      } else {
        Serial.println("Analog mode ON");
      }
      tone(SPEAKER_PIN, 1400, 80);
      break;
    case 2:
      zoomLevel++;
      if(zoomLevel > 3) zoomLevel = 0;
      applyZoomLevel();
      Serial.printf("Zoom level %d, render width %d\n", zoomLevel, currentGraphWidth);
      tone(SPEAKER_PIN, 1200 + (zoomLevel*200), 100);
      break;
    default:
      tone(SPEAKER_PIN, 600, 60);
      break;
  }
}

static bool handleCLineCosMouse(String command){
  AppMouseReport mouse;
  if(!appMouseRead(command, mouse)) return false;
  appCursorRestore();
  appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);

  if(mouse.leftPressed){
    if(inSecondaryMenu){
      int relX = appCursorX - (secMenuX + secMargin);
      int relY = appCursorY - (secMenuY + secMargin);
      if(relX >= 0 && relY >= 0){
        int col = relX / secBtnW;
        int row = relY / secBtnH;
        if(col >= 0 && col < secCols && row >= 0 && row < secRows){
          int idx = row * secCols + col;
          if(idx >= 0 && idx < secCols * secRows){
            int prev = secSelected;
            secSelected = idx;
            if(prev != secSelected) drawSecondaryButton(prev, false);
            drawSecondaryButton(secSelected, true);
            handleSecondaryButtonPress(secSelected);
          }
        }
      }
      appCursorCaptureAndDraw();
      return true;
    }

    if(inLEDWindow){
      ledBlinking = !ledBlinking;
      if(!ledBlinking){
        digitalWrite(LED_PIN, LOW);
        ledState = false;
      } else {
        ledLastToggle = millis();
        ledState = false;
      }
      drawLEDFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inSpeakerWindow){
      tone(SPEAKER_PIN, speakerFreq);
      vTaskDelay(speakerDuration / portTICK_PERIOD_MS);
      noTone(SPEAKER_PIN);
      drawSpeakerFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inResistorWindow){
      lastMeasuredOhms = measureResistorOhms();
      drawResistorFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inI2CWindow){
      lcdEditMode = !lcdEditMode;
      if(lcdEditMode) lcdCharCode = (int)lcdBuf[lcdCursorY][lcdCursorX];
      drawI2CFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inDistanceWindow){
      distanceLastMeasure = 0;
      drawDistanceFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inIRWindow){
      lastIRStrength = measureIRStrength();
      drawIRFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inNFCWindow){
      String uid;
      lastNfcPresent = pollNFCTag(uid);
      nfcLastPoll = millis();
      drawNFCFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(inHumidityWindow){
      measureHumidityTemp(lastHumidity, lastTemperature);
      drawHumidityFrame();
      appCursorCaptureAndDraw();
      return true;
    }

    if(!inJoystickWindow && !inSerialWindow){
      for(int i = 0; i < 3; ++i){
        if(appCursorX >= 10 && appCursorX < 375 && appCursorY >= streamYs[i] && appCursorY < streamYs[i] + 15){
          selectedStreamline = i;
          updateSelection(selectedStreamline);
          int sx = 10 + 365 - switchRightMargin - switchW;
          if(appCursorX >= sx - 6){
            streamlineState[i] = streamlineState[i] ? 0 : 1;
            editMode = true;
            editingIndex = i;
            drawSwitchState(i, true);
          }
          appCursorCaptureAndDraw();
          return true;
        }
      }

      if(appCursorY >= optionBarY && appCursorY < optionBarY + optionBtnH){
        computeOptionButtonLayout();
        for(int i = 0; i < optionButtons; ++i){
          int x = optionButtonsStartX + i * optionBtnW;
          if(appCursorX >= x && appCursorX < x + optionBtnW){
            int prev = optionSelected;
            selectedStreamline = 3;
            optionSelected = i;
            updateSelection(selectedStreamline);
            if(prev != optionSelected) drawOptionButton(prev, false);
            drawOptionButton(optionSelected, true);
            handleOptionSelect();
            appCursorCaptureAndDraw();
            return true;
          }
        }
      }
    }
  }

  if(inJoystickWindow && (mouse.dx != 0 || mouse.dy != 0)){
    joy8X = constrain(joy8X + mouse.dx, 0, 255);
    joy8Y = constrain(joy8Y + mouse.dy, 0, 255);
    drawJoystickFrame();
  }

  appCursorCaptureAndDraw();
  return true;
}

void serialTask(void *parameter){
  for(;;){
    if(Serial1.available()){
      String command = Serial1.readStringUntil('\n');
      command.trim();
      Serial.println(command);
      if(handleCLineCosMouse(command)){
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      AppCursorDrawGuard cursorGuard;
      if(inSerialWindow){
        if(command.length()){
          serialLogPush("[S1] " + command);
        }
      }
      if(inLEDWindow){
        if(command == "Escape"){
          restoreFromLEDWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          drawLEDFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          drawLEDFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "UpArrow"){
          if(ledFieldIndex == 0){
            ledBlinkInterval = max(10, ledBlinkInterval - 10);
            drawLEDFrame();
            tone(SPEAKER_PIN,1500,40);
          }
        } else if(command == "DownArrow"){
          if(ledFieldIndex == 0){
            ledBlinkInterval = min(10000, ledBlinkInterval + 10);
            drawLEDFrame();
            tone(SPEAKER_PIN,1000,40);
          }
        } else if(command == "Enter"){
          ledBlinking = !ledBlinking;
          if(!ledBlinking){
            digitalWrite(LED_PIN, LOW);
            ledState = false;
          } else {
            ledLastToggle = millis();
            ledState = false;
          }
          drawLEDFrame();
          tone(SPEAKER_PIN, (ledBlinking?1800:800), 80);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      if(inSpeakerWindow){
        if(command == "Escape"){
          restoreFromSpeakerWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          speakerFieldIndex = max(0, speakerFieldIndex - 1);
          drawSpeakerFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          speakerFieldIndex = min(2, speakerFieldIndex + 1);
          drawSpeakerFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "UpArrow"){
          if(speakerFieldIndex == 0){
            speakerFreq = min(20000, speakerFreq + 50);
            drawSpeakerFrame();
          } else if(speakerFieldIndex == 1){
            speakerDuration = min(5000, speakerDuration + 10);
            drawSpeakerFrame();
          } else if(speakerFieldIndex == 2){
            tone(SPEAKER_PIN,1400,40);
          }
        } else if(command == "DownArrow"){
          if(speakerFieldIndex == 0){
            speakerFreq = max(20, speakerFreq - 50);
            drawSpeakerFrame();
          } else if(speakerFieldIndex == 1){
            speakerDuration = max(10, speakerDuration - 10);
            drawSpeakerFrame();
          } else if(speakerFieldIndex == 2){
            tone(SPEAKER_PIN,1000,40);
          }
        } else if(command == "Enter"){
          if(speakerFieldIndex == 2){
            speakerContinuous = !speakerContinuous;
            if(speakerContinuous){
              tone(SPEAKER_PIN, speakerFreq);
              speakerPlaying = true;
            } else {
              noTone(SPEAKER_PIN);
              speakerPlaying = false;
            }
            drawSpeakerFrame();
            tone(SPEAKER_PIN, 1600, 80);
          } else {
            tone(SPEAKER_PIN, speakerFreq);
            vTaskDelay(speakerDuration / portTICK_PERIOD_MS);
            noTone(SPEAKER_PIN);
            drawSpeakerFrame();
            tone(SPEAKER_PIN, 1200, 60);
          }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if(inResistorWindow){
        if(command == "Escape"){
          restoreFromResistorWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          resistorFieldIndex = max(0, resistorFieldIndex - 1);
          drawResistorFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          resistorFieldIndex = min(1, resistorFieldIndex + 1);
          drawResistorFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "UpArrow"){
          if(resistorFieldIndex == 0){
            refResistorOhms = min(1000000, refResistorOhms + 100);
            drawResistorFrame();
          }
        } else if(command == "DownArrow"){
          if(resistorFieldIndex == 0){
            refResistorOhms = max(10, refResistorOhms - 100);
            drawResistorFrame();
          }
        } else if(command == "Enter"){
          if(resistorFieldIndex == 1){
            resistorContinuous = !resistorContinuous;
            if(!resistorContinuous){
              lastMeasuredOhms = measureResistorOhms();
            }
            drawResistorFrame();
          } else {
            lastMeasuredOhms = measureResistorOhms();
            drawResistorFrame();
            tone(SPEAKER_PIN, 1400, 80);
          }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      if(inI2CWindow){
        if(command == "Escape"){
          restoreFromI2CWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          lcdCursorX = (lcdCursorX > 0) ? lcdCursorX - 1 : LCD_COLS - 1;
          drawI2CFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          lcdCursorX = (lcdCursorX + 1) % LCD_COLS;
          drawI2CFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "UpArrow"){
          if(lcdEditMode){
            lcdCharCode = min(126, lcdCharCode + 1);
            lcdBuf[lcdCursorY][lcdCursorX] = (char)lcdCharCode;
            drawI2CFrame();
            tone(SPEAKER_PIN,1500,40);
          } else {
            lcdCursorY = (lcdCursorY > 0) ? lcdCursorY - 1 : LCD_ROWS - 1;
            drawI2CFrame();
            tone(SPEAKER_PIN,1200,40);
          }
        } else if(command == "DownArrow"){
          if(lcdEditMode){
            lcdCharCode = max(32, lcdCharCode - 1);
            lcdBuf[lcdCursorY][lcdCursorX] = (char)lcdCharCode;
            drawI2CFrame();
            tone(SPEAKER_PIN,1000,40);
          } else {
            lcdCursorY = (lcdCursorY + 1) % LCD_ROWS;
            drawI2CFrame();
            tone(SPEAKER_PIN,1200,40);
          }
        } else if(command == "Enter"){
          lcdEditMode = !lcdEditMode;
          if(lcdEditMode){
            lcdCharCode = (int)lcdBuf[lcdCursorY][lcdCursorX];
          }
          drawI2CFrame();
          tone(SPEAKER_PIN,1600,80);
        } else if(command == "a"){
          lcdAnimating = !lcdAnimating;
          lcdAnimLast = millis();
          drawI2CFrame();
          tone(SPEAKER_PIN,1400,80);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if(inSerialWindow){
        if(command == "Escape"){
          restoreFromSerialWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "Pause"){
          serialWindowPaused = !serialWindowPaused;
          tone(SPEAKER_PIN, 1200, 60);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if(inSecondaryMenu){
        if(command == "Escape"){
          restoreMainWindowAndUI();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          int prev = secSelected;
          int col = secSelected % secCols;
          int row = secSelected / secCols;
          col = (col + secCols - 1) % secCols;
          secSelected = row * secCols + col;
          drawSecondaryButton(prev, false);
          drawSecondaryButton(secSelected, true);
          tone(SPEAKER_PIN, 1200, 50);
        } else if(command == "RightArrow"){
          int prev = secSelected;
          int col = secSelected % secCols;
          int row = secSelected / secCols;
          col = (col + 1) % secCols;
          secSelected = row * secCols + col;
          drawSecondaryButton(prev, false);
          drawSecondaryButton(secSelected, true);
          tone(SPEAKER_PIN, 1200, 50);
        } else if(command == "UpArrow"){
          int prev = secSelected;
          int col = secSelected % secCols;
          int row = secSelected / secCols;
          row = (row + secRows - 1) % secRows;
          secSelected = row * secCols + col;
          drawSecondaryButton(prev, false);
          drawSecondaryButton(secSelected, true);
          tone(SPEAKER_PIN, 1500, 60);
        } else if(command == "DownArrow"){
          int prev = secSelected;
          int col = secSelected % secCols;
          int row = secSelected / secCols;
          row = (row + 1) % secRows;
          secSelected = row * secCols + col;
          drawSecondaryButton(prev, false);
          drawSecondaryButton(secSelected, true);
          tone(SPEAKER_PIN, 1500, 60);
        } else if(command == "Enter"){
          handleSecondaryButtonPress(secSelected);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }
      if(inJoystickWindow){
        if(command == "Escape"){
          restoreFromJoystickWindow();
          tone(SPEAKER_PIN,600,80);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      // New windows: Distance, IR, NFC, Humidity input handling
      if(inDistanceWindow){
        if(command == "Escape"){
          restoreFromDistanceWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          distanceFieldIndex = max(0, distanceFieldIndex - 1);
          drawDistanceFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          distanceFieldIndex = min(1, distanceFieldIndex + 1);
          drawDistanceFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "UpArrow"){
          if(distanceFieldIndex == 0){
            // nothing numeric to change here in placeholder
            tone(SPEAKER_PIN,1500,40);
          }
        } else if(command == "DownArrow"){
          if(distanceFieldIndex == 0){
            tone(SPEAKER_PIN,1000,40);
          }
        } else if(command == "Enter"){
          if(distanceFieldIndex == 1){
            // toggle continuous
            distanceLastMeasure = 0;
          }
          drawDistanceFrame();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      if(inIRWindow){
        if(command == "Escape"){
          restoreFromIRWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "LeftArrow"){
          irFieldIndex = max(0, irFieldIndex - 1);
          drawIRFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "RightArrow"){
          irFieldIndex = min(1, irFieldIndex + 1);
          drawIRFrame();
          tone(SPEAKER_PIN,1200,50);
        } else if(command == "Enter"){
          // measure now
          lastIRStrength = measureIRStrength();
          drawIRFrame();
          tone(SPEAKER_PIN,1200,60);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      if(inNFCWindow){
        if(command == "Escape"){
          restoreFromNFCWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "Enter"){
          // poll once
          String uid;
          bool present = pollNFCTag(uid);
          nfcLastPoll = millis();
          lastNfcPresent = present;
          drawNFCFrame();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      if(inHumidityWindow){
        if(command == "Escape"){
          restoreFromHumidityWindow();
          tone(SPEAKER_PIN,600,80);
        } else if(command == "Enter"){
          // measure now
          measureHumidityTemp(lastHumidity, lastTemperature);
          drawHumidityFrame();
          tone(SPEAKER_PIN,1200,60);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
        continue;
      }

      // navigation for main UI level
      if(command == "LeftArrow"){
        if(selectedStreamline == 3){
          prevOptionSelected = optionSelected;
          optionSelected--;
          if(optionSelected < 0) optionSelected = optionButtons - 1;
          drawOptionButton(prevOptionSelected, false);
          drawOptionButton(optionSelected, true);
          tone(SPEAKER_PIN, 1200, 50);
        } else {
          selectedStreamline--;
          if(selectedStreamline < 0) selectedStreamline = 3;
          updateSelection(selectedStreamline);
          tone(SPEAKER_PIN,1500,60);
        }
      } else if(command == "RightArrow"){
        if(selectedStreamline == 3){
          prevOptionSelected = optionSelected;
          optionSelected++;
          if(optionSelected >= optionButtons) optionSelected = 0;
          drawOptionButton(prevOptionSelected, false);
          drawOptionButton(optionSelected, true);
          tone(SPEAKER_PIN, 1200, 50);
        } else {
          selectedStreamline++;
          if(selectedStreamline > 3) selectedStreamline = 0;
          updateSelection(selectedStreamline);
          tone(SPEAKER_PIN,1500,60);
        }
      } else if(command == "Escape"){
        if(editMode){
          if(editingIndex >= 0) drawSwitchState(editingIndex, false);
          editMode = false;
          editingIndex = -1;
          tone(SPEAKER_PIN,600,80);
        } else {
          boolRes = "trueKernel";
          boolUpdate();
          restartWithFallbackVideo("Returning home");
        }
      } else if(command == "Enter"){
        if(selectedStreamline == 3){
          Serial.printf("Option %d selected\n", optionSelected+1);
          Serial1.printf("Option %d\n", optionSelected+1);
          handleOptionSelect();
        } else {
          if(!editMode){
            editMode = true;
            editingIndex = selectedStreamline;
            drawSwitchState(editingIndex, true);
            tone(SPEAKER_PIN,1200,80);
          } else {
            drawSwitchState(editingIndex, false);
            editingIndex++;
            if(editingIndex > 2) editingIndex = 0;
            drawSwitchState(editingIndex, true);
            tone(SPEAKER_PIN,1600,80);
          }
        }
      } else if(command == "UpArrow"){
        if(editMode){
          if(editingIndex < 0) editingIndex = selectedStreamline;
          streamlineState[editingIndex] = 1;
          drawSwitchState(editingIndex, true);
          Serial.printf("Streamline %d set HIGH\n", editingIndex+1);
          tone(SPEAKER_PIN,1800,60);
        } else {
          selectedStreamline--;
          if(selectedStreamline < 0) selectedStreamline = 3;
          updateSelection(selectedStreamline);
          tone(SPEAKER_PIN,1500,60);
        }
      } else if(command == "DownArrow"){
        if(editMode){
          if(editingIndex < 0) editingIndex = selectedStreamline;
          streamlineState[editingIndex] = 0;
          drawSwitchState(editingIndex, true);
          Serial.printf("Streamline %d set LOW\n", editingIndex+1);
          tone(SPEAKER_PIN,800,60);
        } else {
          selectedStreamline++;
          if(selectedStreamline > 3) selectedStreamline = 0;
          updateSelection(selectedStreamline);
          tone(SPEAKER_PIN,1500,60);
        }
      } else if(command == "Back"){
        if(editMode){
          drawSwitchState(editingIndex, false);
          editMode = false;
          editingIndex = -1;
          tone(SPEAKER_PIN,600,60);
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void mainWindow(){
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0, 0, 385, 240, COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.rect(0,28,385,180,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.println("Component-Operating System - |COS|");
  videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
  for(int i=0;i<3;i++){
    videodisplay.fillRect(10, streamYs[i], 365, 15, COLOR_WHITE);
    videodisplay.setCursor(15, streamYs[i] + 4);
    String streamline = String("Streamline ") + String(i+1);
    videodisplay.println(streamline.c_str());
  }
  videodisplay.fillRect(optionBarX, optionBarY, optionBarW, optionBtnH, COLOR_WHITE);
  drawAllOptionButtons();
  videodisplay.fillRect(g1_x, g1_y, g_w, g_h, COLOR_WHITE);
  videodisplay.fillRect(g2_x, g2_y, g_w, g_h, COLOR_WHITE);
  videodisplay.fillRect(g3_x, g3_y, g_w, g_h, COLOR_WHITE);
}

void pushGraphSample(int graphIndex, int sample){
  pushGraphSampleRaw(graphIndex, sample);
  redrawGraphFromHist(graphIndex);
}

// ---------- Joystick window (unchanged) ----------
void showJoystickWindow(){
  inSecondaryMenu = false;
  inJoystickWindow = true;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Joystick Visualizer - COS-PCI-JOYSTICK");
  videodisplay.fillRect(JOY_AREA_X, JOY_AREA_Y, JOY_AREA_W, JOY_AREA_H, COLOR_DK);
  int ix = JOY_AREA_X + JOY_AREA_W + 42;
  int iy = JOY_AREA_Y;
  int readW = 200;
  int readH = JOY_AREA_H;
  if(ix + readW > 384) readW = 384 - ix - 2;
  videodisplay.setTextColor(COLOR_BLACK, COLOR_WHITE);
  videodisplay.fillRect(ix + 12, iy + 4, readW - 24, readH - 8, COLOR_WHITE);

  lastJoystickDraw = 0;
  drawJoystickFrame();
}

void restoreFromJoystickWindow(){
  inJoystickWindow = false;
  restoreMainWindowAndUI();
}

void drawJoystickFrame(){
  unsigned long now = millis();
  if(now - lastJoystickDraw < JOY_UPDATE_MS) return;
  lastJoystickDraw = now;

  joyRawX = analogRead(joyPinX);
  joyRawY = analogRead(joyPinY);

  joy8X = mapTo255(joyRawX);
  joy8Y = mapTo255(joyRawY);

  float nx = ((float)joy8X / 255.0f - 0.5f) * 2.0f;
  float ny = ((float)joy8Y / 255.0f - 0.5f) * 2.0f;

  int areaX = JOY_AREA_X;
  int areaY = JOY_AREA_Y;
  int areaW = JOY_AREA_W;
  int areaH = JOY_AREA_H;

  videodisplay.fillRect(areaX, areaY, areaW, areaH, COLOR_DK);

  for(int gx = areaX + 8; gx < areaX + areaW - 8; gx += 18){
    for(int gy = areaY + 8; gy < areaY + areaH - 8; gy += 18){
      videodisplay.dot(gx, gy, COLOR_MD);
    }
  }

  int baseCx = areaX + areaW / 2;
  int baseCy = areaY + areaH / 2 + 6;
  int outerR = 44;
  int midR = 34;
  int innerR = 24;

  for(int yy = -6; yy <= 6; yy++){
    for(int xx = -outerR; xx <= outerR; xx++){
      float nxs = (float)xx / (float)outerR;
      float nys = (float)yy / 6.0f;
      if(nxs*nxs + nys*nys <= 1.0f){
        videodisplay.dot(baseCx + xx + 3, baseCy + yy + 5, COLOR_SH);
      }
    }
  }
  for(int yy = -outerR; yy <= outerR; yy++){
    for(int xx = -outerR; xx <= outerR; xx++){
      if(xx*xx + yy*yy <= outerR*outerR){
        videodisplay.dot(baseCx + xx, baseCy + yy, COLOR_MD);
      }
    }
  }
  for(int yy = -midR; yy <= midR; yy++){
    for(int xx = -midR; xx <= midR; xx++){
      if(xx*xx + yy*yy <= midR*midR){
        videodisplay.dot(baseCx + xx, baseCy + yy, COLOR_LT);
      }
    }
  }
  for(int yy = -innerR; yy <= innerR; yy++){
    for(int xx = -innerR; xx <= innerR; xx++){
      if(xx*xx + yy*yy <= innerR*innerR){
        int col = ((xx - yy) > 0) ? 255 : 210;
        videodisplay.dot(baseCx + xx, baseCy + yy, videodisplay.RGB(col, col, col));
      }
    }
  }

  int pivotX = baseCx;
  int pivotY = baseCy - 10;
  int maxTilt = 42;
  int endX = pivotX + (int)(nx * maxTilt);
  int endY = pivotY + (int)(ny * maxTilt);

  int dx = endX - pivotX;
  int dy = endY - pivotY;
  int steps = max(abs(dx), abs(dy));
  if(steps < 1) steps = 1;
  for(int s=0; s<=steps; s++){
    int px = pivotX + (dx * s) / steps;
    int py = pivotY + (dy * s) / steps;
    for(int wx = -2; wx <= 2; wx++){
      for(int wy = -1; wy <= 1; wy++){
        videodisplay.dot(px + wx, py + wy, COLOR_DK);
      }
    }
  }
  for(int s=0; s<=steps; s++){
    int px = pivotX + (dx * s) / steps;
    int py = pivotY + (dy * s) / steps;
    videodisplay.dot(px - 2, py - 2, COLOR_LT);
  }
  int capR = 6;
  for(int yy=-capR; yy<=capR; yy++){
    for(int xx=-capR; xx<=capR; xx++){
      if(xx*xx + yy*yy <= capR*capR){
        int shade = 220 - ((xx + yy) % 24);
        uint32_t c = videodisplay.RGB(shade, shade, shade);
        videodisplay.dot(endX + xx, endY + yy, c);
      }
    }
  }
  videodisplay.dot(endX - 2, endY - 3, COLOR_WHITE);

  for(int r=0; r<=3; r++){
    for(int a=0; a<360; a+=12){
      float rad = a * 3.14159f / 180.0f;
      int px = pivotX + (int)(r * cos(rad));
      int py = pivotY + (int)(r * sin(rad));
      videodisplay.dot(px, py, (r==0) ? COLOR_WHITE : COLOR_MD);
    }
  }
  for(int r=innerR+6; r<=midR+4; r+=6){
    for(int a=0; a<360; a+=6){
      float rad = a * 3.14159f / 180.0f;
      int px = baseCx + (int)(r * cos(rad));
      int py = baseCy + (int)(r * sin(rad) * 0.5f);
      videodisplay.dot(px, py, COLOR_DK);
    }
  }

  int tx = areaX + areaW + 12;
  int ty = areaY;
  int readW = 200;
  int readH = areaH;
  if(tx + readW > 384) readW = 384 - tx - 2;
  videodisplay.fillRect(tx + 12, ty + 4, readW - 24, readH - 8, COLOR_WHITE);
  videodisplay.setTextColor(COLOR_BLACK, COLOR_WHITE);
  int cursorX = tx + 16;
  int cursorY = ty + 8;
  int lineSpacing = 8;

  videodisplay.setCursor(cursorX, cursorY);
  videodisplay.println("Joystick Readings:");
  char buf[64];
  sprintf(buf, "X raw: %4d", joy8X);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing); videodisplay.println(buf);
  sprintf(buf, "Y raw: %4d", joy8Y);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing*2); videodisplay.println(buf);

  int percX = (int)(nx * 100.0f);
  int percY = (int)(ny * 100.0f);
  sprintf(buf, "X %%: %4d%%", percX);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing*3); videodisplay.println(buf);
  sprintf(buf, "Y %%: %4d%%", percY);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing*4); videodisplay.println(buf);

  float vx = (float)joy8X / 255.0f * 3.3f;
  float vy = (float)joy8Y / 255.0f * 3.3f;
  sprintf(buf, "X V: %1.2fV", vx);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing*5); videodisplay.println(buf);
  sprintf(buf, "Y V: %1.2fV", vy);
  videodisplay.setCursor(cursorX, cursorY + lineSpacing*6); videodisplay.println(buf);

  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// ---------- Distance Window ----------
void showDistanceWindow(){
  inSecondaryMenu = false;
  inDistanceWindow = true;
  distanceFieldIndex = 0;
  lastMeasuredDistanceCm = -1.0f;
  distanceLastMeasure = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Distance Sensor - COS-PCI-DIST");
  // static area
  videodisplay.fillRect(0,28,385,200,COLOR_DK);
  videodisplay.setCursor(10,36);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.println("Distance (cm):");
  videodisplay.println("Enter: measure   Left/Right: select field   Up/Down: n/a");
  distanceStaticDrawn = true;
  drawDistanceFrame();
}

void restoreFromDistanceWindow(){
  inDistanceWindow = false;
  distanceStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawDistanceFrame(){
  unsigned long now = millis();
  if(now - distanceLastMeasure >= DISTANCE_UPDATE_MS){
    distanceLastMeasure = now;
    lastMeasuredDistanceCm = measureDistanceCm();
  }
  // only update the numeric area
  int dx = 12, dy = 52, dw = 120, dh = 12;
  videodisplay.fillRect(dx, dy, dw, dh, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  char buf[64];
  if(lastMeasuredDistanceCm < 0.0f) {
    videodisplay.setCursor(dx+2, dy);
    videodisplay.println("--- cm");
  } else {
    sprintf(buf, "%.1f cm", lastMeasuredDistanceCm);
    videodisplay.setCursor(dx+2, dy);
    videodisplay.println(buf);
  }
  if(distanceFieldIndex == 0) videodisplay.rect(dx, dy, dw, dh, COLOR_WHITE);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// Basic placeholder distance measurement (maps analog -> 2..200cm)
float measureDistanceCm(){
  // Use analog read as placeholder — replace with actual sensor code if available
  int raw = analogRead(graphPins[0]);
  int v = map(raw, 0, 4095, 0, 255);
  // map 0..255 to 2..200 cm
  float cm = 2.0f + (float)v * (198.0f/255.0f);
  return cm;
}

// ---------- IR Window ----------
void showIRWindow(){
  inSecondaryMenu = false;
  inIRWindow = true;
  irFieldIndex = 0;
  lastIRStrength = -1;
  irLastMeasure = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("IR Sensor - COS-PCI-IR");
  videodisplay.fillRect(0,28,385,200,COLOR_DK);
  videodisplay.setCursor(10,36);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.println("IR Strength:");
  videodisplay.println("Enter: measure   Left/Right: select field");
  irStaticDrawn = true;
  drawIRFrame();
}

void restoreFromIRWindow(){
  inIRWindow = false;
  irStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawIRFrame(){
  unsigned long now = millis();
  if(now - irLastMeasure >= IR_UPDATE_MS){
    irLastMeasure = now;
    lastIRStrength = measureIRStrength();
  }
  int ix = 12, iy = 52, iw = 120, ih = 12;
  videodisplay.fillRect(ix, iy, iw, ih, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  char buf[64];
  if(lastIRStrength < 0) {
    videodisplay.setCursor(ix+2, iy);
    videodisplay.println("---");
  } else {
    sprintf(buf, "%d", lastIRStrength);
    videodisplay.setCursor(ix+2, iy);
    videodisplay.println(buf);
  }
  if(irFieldIndex == 0) videodisplay.rect(ix, iy, iw, ih, COLOR_WHITE);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// Placeholder IR measurement (analog mapped)
int measureIRStrength(){
  int raw = analogRead(graphPins[1]);
  int val = map(raw, 0, 4095, 0, 1023);
  return val;
}

// ---------- NFC Window ----------
void showNFCWindow(){
  inSecondaryMenu = false;
  inNFCWindow = true;
  nfcFieldIndex = 0;
  lastNfcPresent = false;
  nfcLastPoll = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("NFC Reader - COS-PCI-NFC");
  videodisplay.fillRect(0,28,385,200,COLOR_DK);
  videodisplay.setCursor(10,36);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.println("Press Enter to poll; present tag info will show below.");
  nfcStaticDrawn = true;
  drawNFCFrame();
}

void restoreFromNFCWindow(){
  inNFCWindow = false;
  nfcStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawNFCFrame(){
  // we only update a small area for tag status/UID
  int nx = 12, ny = 60, nw = 360, nh = 40;
  videodisplay.fillRect(nx, ny, nw, nh, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.setCursor(nx+2, ny+2);
  if(lastNfcPresent){
    // placeholder UID rendering
    videodisplay.println("Tag: PRESENT");
    videodisplay.println("UID: DE AD BE EF");
  } else {
    videodisplay.println("No tag present.");
  }
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// Placeholder NFC polling (returns fake UID occasionally)
bool pollNFCTag(String &uid){
  // Simulate detection every other poll
  bool present = (millis() / 1000) % 2 == 0;
  if(present){
    uid = "DEADBEEF";
    lastNfcPresent = true;
  } else {
    uid = "";
    lastNfcPresent = false;
  }
  return present;
}

// ---------- Humidity Window ----------
void showHumidityWindow(){
  inSecondaryMenu = false;
  inHumidityWindow = true;
  humidityFieldIndex = 0;
  lastHumidity = -1.0f;
  lastTemperature = -1000.0f;
  humidityLastMeasure = 0;
  videodisplay.setFont(Font6x8);
  videodisplay.fillRect(0,0,385,240,COLOR_BLACK);
  videodisplay.rect(0,10,385,15,COLOR_WHITE);
  videodisplay.setCursor(5,14);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
  videodisplay.println("Humidity Sensor - COS-PCI-HUM");
  videodisplay.fillRect(0,28,385,200,COLOR_DK);
  videodisplay.setCursor(10,36);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  videodisplay.println("Enter: measure (placeholder)   Display: Humidity & Temperature");
  humidityStaticDrawn = true;
  drawHumidityFrame();
}

void restoreFromHumidityWindow(){
  inHumidityWindow = false;
  humidityStaticDrawn = false;
  restoreMainWindowAndUI();
}

void drawHumidityFrame(){
  unsigned long now = millis();
  if(now - humidityLastMeasure >= HUMIDITY_UPDATE_MS){
    humidityLastMeasure = now;
    measureHumidityTemp(lastHumidity, lastTemperature);
  }
  int hx = 12, hy = 56, hw = 220, hh = 12;
  videodisplay.fillRect(hx, hy, hw, hh, COLOR_DK);
  videodisplay.setTextColor(COLOR_WHITE, COLOR_DK);
  char buf[64];
  if(lastHumidity < 0.0f) {
    videodisplay.setCursor(hx+2, hy);
    videodisplay.println("Humidity: ---");
  } else {
    sprintf(buf, "Humidity: %.1f %%", lastHumidity);
    videodisplay.setCursor(hx+2, hy);
    videodisplay.println(buf);
  }
  int tx = 12, ty = 76;
  videodisplay.fillRect(tx, ty, hw, hh, COLOR_DK);
  if(lastTemperature < -900.0f){
    videodisplay.setCursor(tx+2, ty);
    videodisplay.println("Temp: ---");
  } else {
    sprintf(buf, "Temp: %.1f C", lastTemperature);
    videodisplay.setCursor(tx+2, ty);
    videodisplay.println(buf);
  }
  videodisplay.setTextColor(COLOR_WHITE, COLOR_BLACK);
}

// Placeholder humidity + temp measurement (maps analog -> 0..100% and -10..50C)
void measureHumidityTemp(float &humidity, float &temperature){
  int raw = analogRead(graphPins[2]);
  int v = map(raw, 0, 4095, 0, 255);
  humidity = (float)v * (100.0f/255.0f); // 0..100%
  temperature = -10.0f + (float)v * (60.0f/255.0f); // -10..50C
}

// ---------- End of new windows ----------

void setup(){
  pinMode(SPEAKER_PIN, OUTPUT);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(256000, SERIAL_8N1, 16, 17);
  Serial2.begin(115200, SERIAL_8N1, 21, 22);
  Serial1.println("res");
  if(!SPIFFS.begin(true)){
    SPIFFS.format();
    if(!SPIFFS.begin(true)) return;
  }
  SPI.begin(18,19,23,5);
  SD.begin(5);
  boolTru();
  boolRes.trim();
  if(boolRes == ""){ boolRes = "false"; boolUpdate(); }
  zoomLevel = 0;
  applyZoomLevel();
  secBtnW = (secMenuW - secMargin * 2) / secCols;
  secBtnH = ((secMenuH - secMargin * 2) / secRows) - 12;
  if(boolRes == "false"){
    videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
    releaseSerialControllerVideo();
    COLOR_WHITE = videodisplay.RGB(255,255,255);
    COLOR_BLACK = videodisplay.RGB(0,0,0);
    // Use brighter B/W range consistent with your suggestion
    COLOR_LT = videodisplay.RGB(220,220,220);
    COLOR_MD = videodisplay.RGB(180,180,180);
    COLOR_DK = videodisplay.RGB(90,90,90);
    COLOR_SH = videodisplay.RGB(50,50,50);

    tone(SPEAKER_PIN, 0);
    mainWindow();
    for(int i=0;i<3;i++) drawSwitchState(i, false);
    drawGraphStatic(0); drawGraphStatic(1); drawGraphStatic(2);
    initGraphHist();
    redrawGraphFromHist(0); redrawGraphFromHist(1); redrawGraphFromHist(2);
    prevSelectedStreamline = -1;
    updateSelection(selectedStreamline);
    xTaskCreatePinnedToCore(serialTask, "SerialTask", 8192, NULL, 1, NULL, 0);
  } else if(boolRes == "trueKernel"){
    tone(SPEAKER_PIN,0);
    boolRes = "false";
    boolUpdate();
    handleKernelFlash();
  } else {
    tone(SPEAKER_PIN,0);
    boolRes = "false";
    boolUpdate();
  }
  for(int i=0;i<3;i++){
    pinMode(graphPins[i], INPUT);
  }
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop(){
  unsigned long now = millis();

  if(Serial2.available()){
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if(s.length()){
      serialLogPush("[S2] " + s);
      if(inSerialWindow) {
        AppCursorDrawGuard cursorGuard;
        drawSerialFrame();
      }
    }
  }

  if(Serial.available()){
    String s = Serial.readStringUntil('\n');
    s.trim();
    if(s.length()){
      serialLogPush("[USB] " + s);
      if(inSerialWindow) {
        AppCursorDrawGuard cursorGuard;
        drawSerialFrame();
      }
    }
  }


  if(inI2CWindow && lcdAnimating && now - lcdAnimLast >= LCD_ANIM_MS){
    lcdAnimLast = now;
    for(int r=0;r<LCD_ROWS;r++){
      char first = lcdBuf[r][0];
      for(int c=0;c < LCD_COLS - 1; c++){
        lcdBuf[r][c] = lcdBuf[r][c+1];
      }
      lcdBuf[r][LCD_COLS - 1] = first;
    }
    drawI2CFrame();
  }

  if(now - lastGraphUpdate >= graphInterval){
    AppCursorDrawGuard cursorGuard;
    lastGraphUpdate = now;
    if(inSecondaryMenu){
      for(int i=0;i<3;i++){
        pinMode(graphPins[i], INPUT);
      }
      delay(10);
      return;
    }
    if(inJoystickWindow){
      pinMode(joyPinX, INPUT);
      pinMode(joyPinY, INPUT);
      drawJoystickFrame();
      delay(10);
      return;
    }
    if(inLEDWindow){
      drawLEDFrame();
      delay(10);
      return;
    }
    if(inSpeakerWindow){
      drawSpeakerFrame();
      delay(10);
      return;
    }
    if(inResistorWindow){
      drawResistorFrame();
      delay(10);
      return;
    }
    if(inI2CWindow){
      drawI2CFrame();
      delay(10);
      return;
    }
    if(inSerialWindow){
      if(millis() - lastSerialPoll >= SERIAL_POLL_MS){
        lastSerialPoll = millis();
        drawSerialFrame();
      }
      delay(10);
      return;
    }
    if(inDistanceWindow){
      drawDistanceFrame();
      delay(10);
      return;
    }
    if(inIRWindow){
      drawIRFrame();
      delay(10);
      return;
    }
    if(inNFCWindow){
      // poll occasionally (light) and show
      if(millis() - nfcLastPoll >= 1000){
        String dummy;
        pollNFCTag(dummy);
        nfcLastPoll = millis();
      }
      drawNFCFrame();
      delay(10);
      return;
    }
    if(inHumidityWindow){
      drawHumidityFrame();
      delay(10);
      return;
    }

    for(int i=0;i<3;i++){
      if(streamlineState[i] == 1){
        pinMode(graphPins[i], OUTPUT);
        digitalWrite(graphPins[i], HIGH);
      } else {
        pinMode(graphPins[i], INPUT);
      }
    }
    bool disabled = anyStreamlineEnabled();
    if(disabled){
      redrawGraphFromHist(0);
      redrawGraphFromHist(1);
      redrawGraphFromHist(2);
    } else {
      for(int gi = 0; gi < 3; ++gi){
        int pin = graphPins[gi];
        int sample;
        if(digitalMode){
          int d = digitalRead(pin);
          sample = (d == HIGH) ? 255 : 0;
        } else {
          int raw = analogRead(pin);
          sample = mapTo255(raw);
        }
        pushGraphSample(gi, sample);
      }
    }
  }
  delay(10);
}

