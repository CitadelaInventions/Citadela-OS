#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
// full sketch with window-mode support (centered windows, Escape to close)
// integrates with your existing menu / compose / answer logic

#include <Wire.h>
#include <ESP32Video.h>
#include <Update.h>
#include "esp_partition.h"
#include "SD.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "FS.h"
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 376;
static const int APP_CURSOR_SCREEN_H = 288;
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

#line 53 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorLoadConfigOnce();
#line 74 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 79 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 84 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 89 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 94 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 98 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 104 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorRestore();
#line 115 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 122 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorCaptureAndDraw();
#line 154 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void appCursorRefreshAfterRedraw();
#line 164 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 185 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void restartWithFallbackVideo(const char *label);
#line 202 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void releaseSerialControllerVideo();
#line 261 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void refreshBrowserPalette(bool inverted);
#line 290 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void verbosePrint(const String &message);
#line 317 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
bool ignoredWord(const String &w);
#line 326 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void boolTru();
#line 342 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void boolUpdate();
#line 354 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void clearTextAreaAtOffset(int offset, uint32_t rgbColor);
#line 358 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void clearTextArea();
#line 364 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 375 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 380 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 389 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void handleKernelFlash();
#line 446 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateLayoutMetrics();
#line 457 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void reflowAllLines();
#line 474 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void clearPageBuffer();
#line 492 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawLambda(int cx, int cy, float s, uint32_t fillColor, uint32_t strokeColor);
#line 531 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void ensureSpaceForNewLine();
#line 538 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void pushCurrentLine();
#line 547 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void appendWord(const String &w);
#line 577 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void applyFontForOffset(int offset);
#line 636 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateSpiffRom(String pmptName);
#line 666 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void appendPlainTextRange(const String &text, int &outStartLine, int &outEndLine);
#line 710 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void displayShort(const String &msg);
#line 756 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void saveViewConfig();
#line 771 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void loadViewConfig();
#line 815 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void commitViewPending();
#line 841 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void getConfig();
#line 846 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void getParam();
#line 852 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawComposeField();
#line 872 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawComposePanel();
#line 888 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void showSingleTopLeftCompose();
#line 893 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawWelcomeScreen();
#line 912 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawBrowserMainSurface();
#line 919 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void forwardToPeer(const String &line);
#line 951 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
String sanitizeLine(const String &in);
#line 972 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void switchViewToAnswer();
#line 980 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void switchViewToTop();
#line 986 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void appendToComposeBufferFromLine(const String &rawLine);
#line 1037 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
bool isIgnoredControlToken(const String &s);
#line 1063 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void submitComposeBuffer();
#line 1081 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawMenuBar();
#line 1115 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawCenteredWindowBase(const String &title, int winW, int winH);
#line 1138 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
String shortForDisplay(const String &s, int maxChars);
#line 1160 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawConfigWindow();
#line 1170 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateConfigRow(int row);
#line 1179 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawParamWindow();
#line 1189 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateParamRow(int row);
#line 1201 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
bool* viewActiveStateArray();
#line 1207 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawViewWindow();
#line 1253 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateViewToggleRow(int idx);
#line 1285 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void updateEnableButton(bool selected);
#line 1304 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawProtocolWindow();
#line 1326 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawToolsWindow();
#line 1335 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawHelpWindow();
#line 1354 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void activeWindowBounds(int &wx, int &wy, int &winW, int &winH);
#line 1370 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void drawActiveWindow();
#line 1382 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
bool handleWindowInput(const String &token);
#line 1463 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
String formatFilenameForPrompt(const String &raw);
#line 1523 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void processSerialIO();
#line 1591 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void setup();
#line 1682 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void switchToWindow(int idx);
#line 1697 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static void closeBrowserWindowFromMouse();
#line 1706 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
static bool handleBrowserMouse(String input);
#line 1779 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void handlePeerLine(const String &lineRaw);
#line 2023 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void handleLocalCommand(const String &cmdRaw);
#line 2176 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
void loop();
#line 53 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CitBrowser\\CitBrowser.ino"
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

#define SCREEN_WIDTH  376
#define SCREEN_HEIGHT 288
#define MAX_PAGE_LINES 200
#define LINE_SIZE 60
#define SPEAKER_PIN 12

char currentLine[LINE_SIZE];
char pageLines[MAX_PAGE_LINES][LINE_SIZE];

int R_CHAR_W = 6;
int R_CHAR_H = 8;

int CHARS_PER_LINE = 48;

const int contentOffsetDefault = 85;
const int contentOffsetAfterClear = 80;
const int SERIAL1_RX_PIN = 16;
const int SERIAL1_TX_PIN = 17;

int pageLineCount = 0;
int pageWindowStart = 0;
int currentLineLen = 0;
int pendingRectStart = -1;
int pendingRectEnd = -1;
int contentOffsetCurrent = contentOffsetDefault;
const unsigned long SERIAL1_BAUD = 256000UL;

struct BrowserPalette {
  uint32_t canvas;
  uint32_t chrome;
  uint32_t panel;
  uint32_t panelAlt;
  uint32_t input;
  uint32_t selection;
  uint32_t accent;
  uint32_t blue;
  uint32_t warm;
  uint32_t danger;
  uint32_t text;
  uint32_t muted;
  uint32_t border;
};

BrowserPalette ui;
bool uiInverted = false;
bool uiShowLineNumbers = false;
bool uiWrapText = true;
bool uiVerbose = false;
bool aiWaiting = false;

void refreshBrowserPalette(bool inverted) {
  uiInverted = inverted;
  if (inverted) {
    ui.canvas = videodisplay.RGB(220, 235, 232);
    ui.chrome = videodisplay.RGB(24, 48, 55);
    ui.panel = videodisplay.RGB(242, 247, 246);
    ui.panelAlt = videodisplay.RGB(205, 226, 222);
    ui.input = videodisplay.RGB(255, 255, 255);
    ui.selection = videodisplay.RGB(34, 111, 100);
    ui.text = videodisplay.RGB(7, 18, 22);
    ui.muted = videodisplay.RGB(72, 100, 105);
    ui.border = videodisplay.RGB(74, 112, 116);
  } else {
    ui.canvas = videodisplay.RGB(7, 12, 17);
    ui.chrome = videodisplay.RGB(14, 25, 31);
    ui.panel = videodisplay.RGB(18, 34, 40);
    ui.panelAlt = videodisplay.RGB(25, 47, 53);
    ui.input = videodisplay.RGB(8, 18, 23);
    ui.selection = videodisplay.RGB(31, 100, 92);
    ui.text = videodisplay.RGB(239, 246, 245);
    ui.muted = videodisplay.RGB(137, 169, 174);
    ui.border = videodisplay.RGB(61, 91, 97);
  }
  ui.accent = videodisplay.RGB(66, 215, 190);
  ui.blue = videodisplay.RGB(70, 143, 235);
  ui.warm = videodisplay.RGB(244, 196, 72);
  ui.danger = videodisplay.RGB(235, 78, 86);
}

static inline void verbosePrint(const String &message) {
  if (uiVerbose) Serial.println(message);
}

bool started = false;
bool nextAnswerUsesClearOffset = false;
bool collectingAnswer = false;
bool printingAnswers  = false;

String answerBuffer = "";
String composeBuffer = "";
String boolRes = "";
String appName = "";
String appString = "";
String lastPromptName = "";

String wifiSSID = "";
String wifiPass = "";
String wifiIP = "";
String hfToken = "";
String hfModel = "";
String hfSpace = "";

enum ViewMode { VM_TOP, VM_ANSWER };
ViewMode viewMode = VM_TOP;
std::vector<String> ignoreList;

bool ignoredWord(const String &w) {
  String wl = w;
  wl.toLowerCase();
  for (auto &x : ignoreList) {
    String xl = x; xl.toLowerCase();
    if (wl == xl) return true;
  }
  return false;
}
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
        Serial.printf("Bool updated to %s\n", boolRes.c_str());
    } else {
        Serial.println("Failed to update bool!");
    }
}
void clearTextAreaAtOffset(int offset, uint32_t rgbColor) {
  videodisplay.fillRect(0, offset, SCREEN_WIDTH, SCREEN_HEIGHT - offset, rgbColor);
}

void clearTextArea() {
  clearTextAreaAtOffset(contentOffsetCurrent, ui.canvas);
}

static int kernelFlashVideoLastPercent = -1;

static void kernelFlashVideoProgress(int percent, const char *label) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == kernelFlashVideoLastPercent) return;
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

void handleKernelFlash() {
    File kernelFile1 = SD.open("/System/kernel.bin");
    if (!kernelFile1) {
        Serial.println("Kernel file not found!");
        return;
    }

    size_t kernelSize1 = kernelFile1.size();
    Serial.printf("Kernel size: %u bytes\n", (unsigned)kernelSize1);
    kernelFlashVideoStart(kernelSize1, "Flashing kernel");

    if (!Update.begin(kernelSize1)) {
        Serial.printf("Not enough space for the kernel! Available: %u bytes, Needed: %u bytes\n", (unsigned)ESP.getFreeSketchSpace(), (unsigned)kernelSize1);
        Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        Serial1.flush();
        kernelFile1.close();
        return;
    }

    Serial.printf("Free Sketch Space: %u bytes\n", (unsigned)ESP.getFreeSketchSpace());
    uint8_t buffer1[2048];
    size_t flashedKernelBytes = 0;
    while (kernelFile1.available()) {
        int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
        int written1 = Update.write(buffer1, len1);
        Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n", len1, len1, written1);
        if (written1 > 0) {
            flashedKernelBytes += written1;
            kernelFlashVideoBytes(flashedKernelBytes);
        }

        if (written1 != len1) {
            Serial.printf("Write failed! Expected: %d, Wrote: %d\n", len1, written1);
            Update.abort();
            kernelFlashVideoProgress(100, "Kernel flash failed");
            Serial1.flush();
            kernelFile1.close();
            return;
        }
    }

    kernelFile1.close();
    if (Update.end()) {
        Serial.println("Kernel written to flash successfully.");
        kernelFlashVideoBytes(kernelSize1);
        Serial1.println("VDSTOP");
        Serial1.flush();
        delay(25);
        ESP.restart();
    } else {
        Serial.printf("Update failed: %s\n", Update.errorString());
        kernelFlashVideoProgress(100, "Kernel flash failed");
        Serial1.flush();
    }
}

void updateLayoutMetrics() {
  int rectX = 6;
  int rectW = SCREEN_WIDTH - rectX * 2;
  int usable = rectW - 8;
  if (uiShowLineNumbers) usable -= 24;
  int cp = usable / max(1, R_CHAR_W);
  if (cp < 8) cp = 8;
  if (cp > LINE_SIZE - 4) cp = LINE_SIZE - 4;
  CHARS_PER_LINE = cp;
}

void reflowAllLines() {
  String full = "";
  full.reserve(pageLineCount * min(CHARS_PER_LINE + 1, LINE_SIZE));
  for (int i = 0; i < pageLineCount; ++i) {
    if (i) full += ' ';
    full += String(pageLines[i]);
  }
  pageLineCount = 0;
  pageWindowStart = 0;
  currentLineLen = 0;
  currentLine[0] = 0;

  int s, e;
  appendPlainTextRange(full, s, e);
  Serial.println("Reflowed all lines to new CHARS_PER_LINE.");
}

void clearPageBuffer() {
  pageLineCount = 0;
  pageWindowStart = 0;
  currentLineLen = 0;
  currentLine[0] = 0;
  nextAnswerUsesClearOffset = true;

  clearTextAreaAtOffset(contentOffsetAfterClear, ui.canvas);

  videodisplay.setFont(Font8x8);
  videodisplay.setCursor(12, contentOffsetAfterClear + 10);
  videodisplay.setTextColor(ui.accent, ui.canvas);
  videodisplay.print("NEW CONVERSATION");
  videodisplay.setTextColor(ui.text, ui.canvas);
  // restore font based on current R_CHAR_W/H
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
}

void drawLambda(int cx, int cy, float s, uint32_t fillColor, uint32_t strokeColor) {
  auto scaled = [&](int off) -> int {
    return (int)roundf(off * s);
  };

  int r = scaled(20);
  videodisplay.fillCircle(cx, cy, r, fillColor);

  const int main_lines[][4] = {
    { -10, -10,  -5, -10 },
    {  -5, -10,  10,  10 },
    {  10,  10,  15,  10 },
    {   0,   0, -10,  10 }
  };

  const int outline_lines[][4] = {
    { -10, -11,  -5, -11 },
    {  -4, -11,  11,   9 },
    {  11,   9,  16,   9 },
    {  -1,  -1,  -9,   9 }
  };

  for (unsigned int i = 0; i < (sizeof(main_lines) / sizeof(main_lines[0])); ++i) {
    int x1 = cx + scaled(main_lines[i][0]);
    int y1 = cy + scaled(main_lines[i][1]);
    int x2 = cx + scaled(main_lines[i][2]);
    int y2 = cy + scaled(main_lines[i][3]);
    videodisplay.line(x1, y1, x2, y2, strokeColor);
  }

  for (unsigned int i = 0; i < (sizeof(outline_lines) / sizeof(outline_lines[0])); ++i) {
    int x1 = cx + scaled(outline_lines[i][0]);
    int y1 = cy + scaled(outline_lines[i][1]);
    int x2 = cx + scaled(outline_lines[i][2]);
    int y2 = cy + scaled(outline_lines[i][3]);
    videodisplay.line(x1, y1, x2, y2, strokeColor);
  }
}

void ensureSpaceForNewLine() {
  if (pageLineCount < MAX_PAGE_LINES) return;
  memmove(pageLines[0], pageLines[1], (MAX_PAGE_LINES - 1) * LINE_SIZE);
  pageLineCount = MAX_PAGE_LINES - 1;
  if (pageWindowStart > 0) pageWindowStart--;
}

void pushCurrentLine() {
  ensureSpaceForNewLine();
  strncpy(pageLines[pageLineCount], currentLine, LINE_SIZE - 1);
  pageLines[pageLineCount][LINE_SIZE - 1] = 0;
  pageLineCount++;
  currentLineLen = 0;
  currentLine[0] = 0;
}

void appendWord(const String &w) {
  if ((int)w.length() >= CHARS_PER_LINE) {
    if (currentLineLen > 0) pushCurrentLine();
    int pos = 0;
    while (pos < (int)w.length()) {
      int take = min(CHARS_PER_LINE, (int)w.length() - pos);
      memset(currentLine, 0, LINE_SIZE);
      for (int i = 0; i < take && i < LINE_SIZE - 1; ++i) currentLine[i] = w[pos + i];
      currentLineLen = take;
      currentLine[currentLineLen] = 0;
      pushCurrentLine();
      pos += take;
    }
    return;
  }

  int needed = (currentLineLen ? 1 : 0) + (int)w.length();
  if (currentLineLen + needed >= CHARS_PER_LINE) {
    pushCurrentLine();
  } else {
    if (currentLineLen) {
      currentLine[currentLineLen++] = ' ';
    }
  }
  for (size_t i = 0; i < w.length() && currentLineLen < LINE_SIZE - 1; ++i) {
    currentLine[currentLineLen++] = w[i];
  }
  currentLine[currentLineLen] = 0;
}

void applyFontForOffset(int offset) {
  if (R_CHAR_W == 8) {
    videodisplay.setFont(Font8x8);
  } else {
    videodisplay.setFont(Font6x8);
  }
}

void displayPage(int start, int offset = -1) {
  if (offset < 0) offset = contentOffsetCurrent;
  if (start < 0) start = 0;

  updateLayoutMetrics();

  int linesPerScreen = max(1, (SCREEN_HEIGHT - offset - 10) / (R_CHAR_H + 2));
  if (pageLineCount <= linesPerScreen) start = 0;
  else if (start > pageLineCount - linesPerScreen) start = pageLineCount - linesPerScreen;
  pageWindowStart = start;

  clearTextAreaAtOffset(offset, ui.canvas);

  int rectX = 6;
  int rectY = offset + 3;
  int rectW = SCREEN_WIDTH - rectX * 2;
  int rectH = SCREEN_HEIGHT - rectY - 4;
  videodisplay.fillRect(rectX, rectY, rectW, rectH, ui.panel);
  videodisplay.rect(rectX, rectY, rectW, rectH, ui.border);
  videodisplay.fillRect(rectX, rectY, 3, rectH, ui.accent);
  videodisplay.setTextColor(ui.text, ui.panel);
  int baseY = rectY + 5;
  int y = baseY;
  applyFontForOffset(offset);

  for (int i = start; i < pageLineCount && i < start + linesPerScreen; i++) {
    int textX = rectX + 8;
    if (uiShowLineNumbers) {
      char lineNumber[6];
      snprintf(lineNumber, sizeof(lineNumber), "%3d", i + 1);
      videodisplay.setFont(Font6x8);
      videodisplay.setTextColor(ui.muted, ui.panel);
      videodisplay.setCursor(textX, y);
      videodisplay.print(lineNumber);
      textX += 24;
      applyFontForOffset(offset);
      videodisplay.setTextColor(ui.text, ui.panel);
    }
    videodisplay.setCursor(textX, y);
    videodisplay.print(pageLines[i]);
    y += R_CHAR_H + 2;
  }
  if (pageLineCount > linesPerScreen) {
    int thumbH = max(10, rectH * linesPerScreen / pageLineCount);
    int maxStart = pageLineCount - linesPerScreen;
    int thumbY = rectY + (rectH - thumbH) * pageWindowStart / max(1, maxStart);
    videodisplay.fillRect(rectX + rectW - 3, thumbY, 2, thumbH, ui.blue);
  }
  videodisplay.setTextColor(ui.text, ui.canvas);
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
}
void updateSpiffRom(String pmptName) {
    pmptName.trim();
    if (pmptName.length() == 0) {
      pmptName = lastPromptName;
    }
    pmptName.trim();
    String filename = formatFilenameForPrompt(pmptName);
    if (filename.length() > 60) filename = filename.substring(0, 60);
    String full = "";
    for (int i = 0; i < pageLineCount; ++i) {
      if (i) full += ' ';
      full += String(pageLines[i]);
    }

    File file = SPIFFS.open("/PromptCache.txt", FILE_WRITE);
    if (file) {
        file.seek(0);
        file.print("");
        file.println(full);
        file.flush();
        file.close();
        Serial.println("PromptCache.txt saved OK.");
        boolRes = "task SPIFF2SD " + filename;
        boolUpdate();
    } else {
        Serial.println("Failed to save /PromptCache.txt — SPIFFS.open returned NULL");
        Serial.printf("SPIFFS mounted? %s\n", SPIFFS.begin(true) ? "yes (re-mounted)" : "no (mount failed)");
    }
}

void appendPlainTextRange(const String &text, int &outStartLine, int &outEndLine) {
  outStartLine = pageLineCount;
  if (!uiWrapText) {
    currentLineLen = 0;
    currentLine[0] = 0;
    bool clipping = false;
    for (size_t idx = 0; idx < text.length(); ++idx) {
      char c = text[idx];
      if (c == '\r') continue;
      if (c == '\n') {
        pushCurrentLine();
        clipping = false;
        continue;
      }
      if (!clipping && currentLineLen < min(CHARS_PER_LINE, LINE_SIZE - 1)) {
        currentLine[currentLineLen++] = c;
        currentLine[currentLineLen] = 0;
      } else {
        clipping = true;
      }
    }
    if (currentLineLen > 0) pushCurrentLine();
    outEndLine = pageLineCount - 1;
    return;
  }
  String word = "";
  for (size_t idx = 0; idx < text.length(); ++idx) {
    char c = text[idx];
    if (c == '\r') continue;
    if (c == '\n') {
      if (word.length()) { appendWord(word); word = ""; }
      pushCurrentLine();
    } else if (c == ' ') {
      if (word.length()) { appendWord(word); word = ""; }
    } else {
      word += c;
    }
  }
  if (word.length()) appendWord(word);
  // ensure any remaining currentLine becomes a pushed line
  if (currentLineLen > 0) pushCurrentLine();
  outEndLine = pageLineCount - 1;
}

void displayShort(const String &msg) {
  contentOffsetCurrent = contentOffsetDefault;
  nextAnswerUsesClearOffset = false;
  pageLineCount = 0;
  pageWindowStart = 0;
  currentLineLen = 0;
  currentLine[0] = 0;

  appendWord(msg);
  if (currentLineLen > 0) pushCurrentLine();
  displayPage(0, contentOffsetDefault);
}

// ---- menu state + drawing ----
bool menuMode = false;   // whether menu has focus; when true, other inputs are blocked
int menuIndex = 0;
const int MENU_BUTTONS = 6;
const String menuLabels[MENU_BUTTONS] = {
  "Config", "Param", "View", "Protocol", "Tools", "Help"
};

// ---- window state ----
bool windowMode = false;      // TRUE when a centered window is open
int activeWindow = -1;        // index of active window (0..MENU_BUTTONS-1)

// ---- View window toggles ----
const int VIEW_TOGGLES = 5;
const String viewToggleLabels[VIEW_TOGGLES] = {
  "Larger Text",
  "Show Line Numbers",
  "Invert Colors",
  "Wrap Text",
  "Verbose Mode"
};
// committed (persistent in-session) toggles (applied to UI only after commit)
bool viewToggleStates[VIEW_TOGGLES] = { false, false, false, true, false };

// pending toggles used while View window is open (edits go here)
bool viewPendingStates[VIEW_TOGGLES] = { false, false, false, true, false };
bool viewPendingActive = false; // true when there are pending edits (or the window is open)

// View window layout independent of R_CHAR_H so toggles don't move layout while editing
const int VIEW_ROW_H = 14; // row height for view window controls
int viewToggleIndex = 0;

// ---------- View config save/load ----------
void saveViewConfig() {
  File f = SPIFFS.open("/viewcfg.txt", FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open /viewcfg.txt for writing");
    return;
  }
  String out = "";
  for (int i = 0; i < VIEW_TOGGLES; ++i) {
    out += (viewToggleStates[i] ? "1" : "0");
    if (i < VIEW_TOGGLES-1) out += ",";
  }
  f.println(out);
  f.close();
  Serial.println(String("Saved view config: ") + out);
}
void loadViewConfig() {
  if (!SPIFFS.exists("/viewcfg.txt")) {
    Serial.println("/viewcfg.txt not found — using defaults");
    return;
  }
  File f = SPIFFS.open("/viewcfg.txt", FILE_READ);
  if (!f) { Serial.println("Failed to open /viewcfg.txt"); return; }
  String content = "";
  while (f.available()) content += (char)f.read();
  f.close();
  content.trim();
  if (content.length() == 0) return;
  int s = 0;
  for (int i = 0; i < VIEW_TOGGLES && s < (int)content.length(); ++i) {
    int comma = content.indexOf(',', s);
    String token;
    if (comma == -1) {
      token = content.substring(s);
      s = content.length();
    } else {
      token = content.substring(s, comma);
      s = comma + 1;
    }
    token.trim();
    viewToggleStates[i] = (token == "1");
  }
  // apply Larger Text at boot (committed state)
  if (viewToggleStates[0]) {
    R_CHAR_W = 8;
    R_CHAR_H = 8;
    videodisplay.setFont(Font8x8);
  } else {
    R_CHAR_W = 6;
    R_CHAR_H = 8;
    videodisplay.setFont(Font6x8);
  }
  uiShowLineNumbers = viewToggleStates[1];
  uiInverted = viewToggleStates[2];
  uiWrapText = viewToggleStates[3];
  uiVerbose = viewToggleStates[4];
  updateLayoutMetrics();
  Serial.println("Loaded view config");
}
// commit pending view edits to live state and persist; re-render UI accordingly
void commitViewPending() {
  if (!viewPendingActive) return;
  bool prevLarger = viewToggleStates[0];
  bool prevLineNumbers = viewToggleStates[1];
  for (int i = 0; i < VIEW_TOGGLES; ++i) viewToggleStates[i] = viewPendingStates[i];
  viewPendingActive = false;
  uiShowLineNumbers = viewToggleStates[1];
  uiWrapText = viewToggleStates[3];
  uiVerbose = viewToggleStates[4];
  refreshBrowserPalette(viewToggleStates[2]);
  R_CHAR_W = viewToggleStates[0] ? 8 : 6;
  R_CHAR_H = 8;
  if (viewToggleStates[0]) videodisplay.setFont(Font8x8);
  else videodisplay.setFont(Font6x8);
  bool layoutChanged = prevLarger != viewToggleStates[0] ||
                       prevLineNumbers != viewToggleStates[1];
  updateLayoutMetrics();
  if (layoutChanged && pageLineCount > 0) {
    reflowAllLines();
  }
  saveViewConfig();
  Serial.println("Committed view changes");
}

// ---- end view config/save/load ----

void getConfig(){
  Serial1.println("CBWR0");
  Serial1.println("CBWR1");
  Serial1.println("CBWR2");
}
void getParam(){
  Serial1.println("CBHFRM");
  Serial1.println("CBHFRS");
  Serial1.println("CBHFRT");
}

void drawComposeField() {
  const int rectX = 12;
  const int rectY = 56;
  const int rectW = SCREEN_WIDTH - 24;
  const int rectH = 20;
  videodisplay.fillRect(rectX, rectY, rectW, rectH, ui.input);
  videodisplay.rect(rectX, rectY, rectW, rectH, aiWaiting ? ui.warm : ui.border);
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.text, ui.input);
  videodisplay.setCursor(rectX + 5, rectY + 6);
  if (composeBuffer.length() == 0) {
    videodisplay.setTextColor(ui.muted, ui.input);
    videodisplay.print(aiWaiting ? "Waiting for response..." : "Ask Citadela AI");
    return;
  }
  int maxChars = max(8, (rectW - 12) / max(1, R_CHAR_W));
  int start = max(0, (int)composeBuffer.length() - maxChars);
  videodisplay.print(composeBuffer.substring(start).c_str());
}

void drawComposePanel() {
  videodisplay.fillRect(0, 29, SCREEN_WIDTH, contentOffsetDefault - 29, ui.canvas);
  videodisplay.fillRect(8, 34, SCREEN_WIDTH - 16, 47, ui.panel);
  videodisplay.rect(8, 34, SCREEN_WIDTH - 16, 47, ui.border);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.accent, ui.panel);
  videodisplay.setCursor(14, 42);
  videodisplay.print("PROMPT");
  const char *status = aiWaiting ? "THINKING" : "READY";
  int statusW = strlen(status) * 6;
  videodisplay.setTextColor(aiWaiting ? ui.warm : ui.blue, ui.panel);
  videodisplay.setCursor(SCREEN_WIDTH - 14 - statusW, 42);
  videodisplay.print(status);
  drawComposeField();
}

void showSingleTopLeftCompose() {
  drawMenuBar();
  drawComposePanel();
}

void drawWelcomeScreen() {
  clearTextAreaAtOffset(contentOffsetDefault, ui.canvas);
  videodisplay.fillRect(8, contentOffsetDefault + 4, SCREEN_WIDTH - 16, 128, ui.panel);
  videodisplay.rect(8, contentOffsetDefault + 4, SCREEN_WIDTH - 16, 128, ui.border);
  drawLambda(54, contentOffsetDefault + 66, 1.15f, ui.accent, ui.chrome);
  videodisplay.setFont(Font8x8);
  videodisplay.setTextColor(ui.text, ui.panel);
  videodisplay.setCursor(96, contentOffsetDefault + 34);
  videodisplay.print("CITADELA AI");
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.muted, ui.panel);
  videodisplay.setCursor(96, contentOffsetDefault + 54);
  videodisplay.print("Local interface, remote intelligence");
  videodisplay.setTextColor(ui.blue, ui.panel);
  videodisplay.setCursor(96, contentOffsetDefault + 76);
  videodisplay.print("READY FOR A NEW PROMPT");
  videodisplay.fillRect(96, contentOffsetDefault + 96, 118, 3, ui.accent);
}

void drawBrowserMainSurface() {
  showSingleTopLeftCompose();
  if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
  else drawWelcomeScreen();
}

// single definition - forward to peer
void forwardToPeer(const String &line) {
  Serial1.println(line);
  Serial.println(String("-> peer: ") + line);
}

void drawPendingRectIfVisibleAndReset(int offset = -1) {
  if (pendingRectStart == -1) return;
  if (offset < 0) offset = (viewMode == VM_ANSWER ? contentOffsetAfterClear : contentOffsetCurrent);

  int linesPerScreen = max(1, (SCREEN_HEIGHT - offset - 10) / (R_CHAR_H + 2));
  if (pageLineCount > linesPerScreen) pageWindowStart = pageLineCount - linesPerScreen;
  else pageWindowStart = 0;

  int visibleStart = pageWindowStart;
  int visibleEnd = visibleStart + linesPerScreen - 1;

  if (!(pendingRectEnd < visibleStart || pendingRectStart > visibleEnd)) {
    int inkStart = max(pendingRectStart, visibleStart);
    int inkEnd = min(pendingRectEnd, visibleEnd);
    int baseY = offset + 3 + 3;
    int rectX = 6 + 2;
    int rectY = baseY + (inkStart - visibleStart) * (R_CHAR_H + 2) - 3;
    if (rectY < offset) rectY = offset;
    int rectW = SCREEN_WIDTH - rectX * 2;
    int rectH = (inkEnd - inkStart + 1) * (R_CHAR_H + 2) + 6;
    videodisplay.rect(rectX, rectY, rectW, rectH, ui.accent);
  }

  pendingRectStart = -1;
  pendingRectEnd = -1;
}

String sanitizeLine(const String &in) {
  String out;
  out.reserve(in.length());
  bool inEsc = false;
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (inEsc) {
      if ((c >= '@' && c <= '~') || c == '[' || c == 'm' || c == ';') {
        inEsc = false;
      }
      continue;
    }
    if (c == 0x1B) { inEsc = true; continue; }
    if (c == '\r') continue;
    if (c == '\n') continue;
    if (c == '\t') { out += ' '; continue; }
    if (c >= 32 && c <= 126) out += c;
  }
  return out;
}

void switchViewToAnswer() {
  viewMode = VM_ANSWER;
  int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
  if (pageLineCount > linesPerScreen) pageWindowStart = pageLineCount - linesPerScreen;
  else pageWindowStart = 0;
  displayPage(pageWindowStart, contentOffsetAfterClear);
}

void switchViewToTop() {
  viewMode = VM_TOP;
  clearTextAreaAtOffset(contentOffsetDefault, ui.canvas);
  if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
}

void appendToComposeBufferFromLine(const String &rawLine) {
  if (menuMode || windowMode) {
    // while menu or window is focused, ignore compose input
    Serial.println("compose input ignored while menu/window active");
    return;
  }

  String s = sanitizeLine(rawLine);
  if (s.length() == 0) return;

  std::vector<String> tokens;
  String tok = "";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == ' ') {
      if (tok.length()) { tokens.push_back(tok); tok = ""; }
    } else {
      tok += c;
    }
  }
  if (tok.length()) tokens.push_back(tok);
  for (auto &t : tokens) {
    String lower = t;
    lower.toLowerCase();

    if (lower == "space") {
      composeBuffer += ' ';
      Serial.println("compose: inserted space");
      continue;
    }

    if (lower == "backspace") {
      int len = composeBuffer.length();
      if (len > 0) {
        composeBuffer.remove(len - 1);
        Serial.println("compose: backspace applied");
      } else {
        Serial.println("compose: backspace but buffer empty");
      }
      continue;
    }

    if (ignoredWord(t)) {
      Serial.println(String("ignored: ") + t);
      continue;
    }

    composeBuffer += t;
  }
}

bool isIgnoredControlToken(const String &s) {
  if (s.length() == 0) return true;

  String t = s;
  t.trim();
  t.toLowerCase();

  if (t == "bl1x") return true;
  if (t == "enter") return true;
  if (t == "leftarrow") return true;
  if (t == "rightarrow") return true;
  if (t == "escape") return true;

  if (t.startsWith("leftshift")) return true;
  if (t.startsWith("rightshift")) return true;
  if (t.startsWith("leftctrl")) return true;
  if (t.startsWith("rightctrl")) return true;
  if (t.startsWith("leftalt")) return true;
  if (t.startsWith("rightalt")) return true;
  if (t.startsWith("rightgui")) return true;
  if (t == "+") return true;
  if (t.endsWith("+")) return true;

  return false;
}

void submitComposeBuffer() {
  if (composeBuffer.length() == 0) {
    Serial.println("composeBuffer empty — nothing to send.");
    return;
  }
  // store last prompt so we still have it later when composeBuffer gets cleared
  lastPromptName = composeBuffer;

  String out = String("CBHFA ") + composeBuffer;
  Serial1.println(out);
  Serial.println(String("-> peer (submitted): ") + out);
  aiWaiting = true;
  displayShort(String("You: ") + (composeBuffer.length() > 46 ? composeBuffer.substring(0,46) + "..." : composeBuffer));
  composeBuffer = "";
  drawComposePanel();
}


void drawMenuBar() {
  const int barH = 28;
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, barH + 1, ui.chrome);
  int btnW = SCREEN_WIDTH / MENU_BUTTONS;
  videodisplay.setFont(Font6x8);

  for (int i = 0; i < MENU_BUTTONS; ++i) {
    int x = i * btnW;
    int w = btnW;
    int y = 3;
    int h = 22;

    bool highlight = false;
    if (menuMode && i == menuIndex) highlight = true;
    else if (windowMode && i == activeWindow) highlight = true; // highlight active window while in window mode

    if (highlight) {
      videodisplay.fillRect(x + 2, y, w - 4, h, ui.selection);
      videodisplay.rect(x + 2, y, w - 4, h, ui.accent);
      videodisplay.setTextColor(ui.text, ui.selection);
    } else {
      videodisplay.fillRect(x + 2, y, w - 4, h, ui.chrome);
      videodisplay.setTextColor(ui.muted, ui.chrome);
    }

    int labelW = menuLabels[i].length() * 6;
    int cx = x + (w - labelW) / 2;
    int cy = y + 7;
    videodisplay.setCursor(cx, cy);
    videodisplay.print(menuLabels[i].c_str());
  }
  videodisplay.fillRect(0, barH - 2, SCREEN_WIDTH, 2, ui.accent);
}

void drawCenteredWindowBase(const String &title, int winW, int winH) {
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;

  videodisplay.fillRect(0, 29, SCREEN_WIDTH, SCREEN_HEIGHT - 29, ui.canvas);
  videodisplay.fillRect(wx, wy, winW, winH, ui.panel);
  videodisplay.rect(wx, wy, winW, winH, ui.accent);
  videodisplay.fillRect(wx + 1, wy + 1, winW - 2, 20, ui.panelAlt);
  videodisplay.fillRect(wx + 1, wy + 1, 4, 20, ui.accent);
  videodisplay.setTextColor(ui.text, ui.panelAlt);
  videodisplay.setFont(Font8x8);
  int titleW = title.length() * 8;
  videodisplay.setCursor(wx + (winW - titleW) / 2, wy + 7);
  videodisplay.print(title.c_str());
  videodisplay.fillRect(wx + winW - 19, wy + 4, 14, 14, ui.danger);
  videodisplay.setTextColor(ui.text, ui.danger);
  videodisplay.setCursor(wx + winW - 16, wy + 7);
  videodisplay.print("X");
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.text, ui.panel);
}

// helper to produce a short truncated display string
String shortForDisplay(const String &s, int maxChars) {
  if (s.length() == 0) return String("<not set>");
  if (s.length() <= maxChars) return s;
  return s.substring(0, maxChars - 3) + "...";
}

void drawInfoRow(int x, int y, int w, const char *label, const String &value,
                 uint32_t labelColor, bool secret = false) {
  videodisplay.fillRect(x, y, w, 30, ui.panelAlt);
  videodisplay.rect(x, y, w, 30, ui.border);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(labelColor, ui.panelAlt);
  videodisplay.setCursor(x + 6, y + 5);
  videodisplay.print(label);
  String shown = value;
  if (secret && shown.length()) shown = "********";
  if (!shown.length()) shown = "Not configured";
  videodisplay.setTextColor(shown == "Not configured" ? ui.muted : ui.text, ui.panelAlt);
  videodisplay.setCursor(x + 6, y + 17);
  videodisplay.print(shortForDisplay(shown, max(8, (w - 12) / 6)).c_str());
}

void drawConfigWindow() {
  const int winW = 300, winH = 160;
  drawCenteredWindowBase("Connection", winW, winH);
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  drawInfoRow(wx + 10, wy + 28, winW - 20, "WI-FI NETWORK", wifiSSID, ui.accent);
  drawInfoRow(wx + 10, wy + 62, winW - 20, "PASSWORD", wifiPass, ui.warm, true);
  drawInfoRow(wx + 10, wy + 96, winW - 20, "IP ADDRESS", wifiIP, ui.blue);
}

void updateConfigRow(int row) {
  const int winW = 300, winH = 160;
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  if (row == 0) drawInfoRow(wx + 10, wy + 28, winW - 20, "WI-FI NETWORK", wifiSSID, ui.accent);
  if (row == 1) drawInfoRow(wx + 10, wy + 62, winW - 20, "PASSWORD", wifiPass, ui.warm, true);
  if (row == 2) drawInfoRow(wx + 10, wy + 96, winW - 20, "IP ADDRESS", wifiIP, ui.blue);
}

void drawParamWindow() {
  const int winW = 300, winH = 160;
  drawCenteredWindowBase("AI Parameters", winW, winH);
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  drawInfoRow(wx + 10, wy + 28, winW - 20, "MODEL", hfModel, ui.accent);
  drawInfoRow(wx + 10, wy + 62, winW - 20, "SPACE", hfSpace, ui.blue);
  drawInfoRow(wx + 10, wy + 96, winW - 20, "ACCESS TOKEN", hfToken, ui.warm, true);
}

void updateParamRow(int row) {
  const int winW = 300, winH = 160;
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  if (row == 0) drawInfoRow(wx + 10, wy + 28, winW - 20, "MODEL", hfModel, ui.accent);
  if (row == 1) drawInfoRow(wx + 10, wy + 62, winW - 20, "SPACE", hfSpace, ui.blue);
  if (row == 2) drawInfoRow(wx + 10, wy + 96, winW - 20, "ACCESS TOKEN", hfToken, ui.warm, true);
}

// ---------- View window drawing + partial updates ----------

// get pointer to the appropriate state array (pending while editing else committed)
bool *viewActiveStateArray() {
  if (viewPendingActive) return viewPendingStates;
  return viewToggleStates;
}

// full initial draw of view window (called once when opening the window)
void drawViewWindow() {
  drawCenteredWindowBase("View", 300, 220);
  int wx = (SCREEN_WIDTH - 300) / 2;
  int wy = (SCREEN_HEIGHT - 220) / 2;

  // body area start
  int x = wx + 12;
  int y = wy + 28;
  int spacing = VIEW_ROW_H; // fixed spacing independent of R_CHAR_H

  bool *sarr = viewActiveStateArray();

  // draw each toggle line
  for (int i = 0; i < VIEW_TOGGLES; ++i) {
    int itemY = y + i * spacing;
    // background for selected item
    if (i == viewToggleIndex) {
      videodisplay.fillRect(x - 6, itemY - 2, 260, VIEW_ROW_H - 2, ui.selection);
      videodisplay.setTextColor(ui.text, ui.selection);
    } else {
      videodisplay.fillRect(x - 6, itemY - 2, 260, VIEW_ROW_H - 2, ui.panel);
      videodisplay.setTextColor(ui.text, ui.panel);
    }
    // draw checkbox
    String cb = sarr[i] ? "[x] " : "[ ] ";
    // labels for window keep small readable font
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(x, itemY);
    videodisplay.print(cb.c_str());
    videodisplay.print(viewToggleLabels[i].c_str());
  }

  // Draw Enable button at bottom of window
  int enableY = wy + 28 + VIEW_TOGGLES * spacing + 6;
  int enableW = 120;
  int enableX = wx + (300 - enableW) / 2;
  // draw rounded-like rectangle (simple rect)
  videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, ui.blue);
  videodisplay.setTextColor(ui.text, ui.blue);
  videodisplay.setFont(Font6x8);
  int labelW = String("APPLY").length() * 6;
  videodisplay.setCursor(enableX + (enableW - labelW) / 2, enableY + 3);
  videodisplay.print("APPLY");
}

// Update only one toggle row (fast, partial redraw)
void updateViewToggleRow(int idx) {
  if (idx < 0 || idx >= VIEW_TOGGLES) return;

  int wx = (SCREEN_WIDTH - 300) / 2;
  int wy = (SCREEN_HEIGHT - 220) / 2;
  int x = wx + 12;
  int y = wy + 28;
  int spacing = VIEW_ROW_H;
  int itemY = y + idx * spacing;
  int rowW = 260;
  int rowH = VIEW_ROW_H - 2;

  bool *sarr = viewActiveStateArray();

  // choose background for selected row
  if (idx == viewToggleIndex) {
    videodisplay.fillRect(x - 6, itemY - 2, rowW, rowH, ui.selection);
    videodisplay.setTextColor(ui.text, ui.selection);
  } else {
    videodisplay.fillRect(x - 6, itemY - 2, rowW, rowH, ui.panel);
    videodisplay.setTextColor(ui.text, ui.panel);
  }

  // draw checkbox + label (use small font for window labels)
  videodisplay.setFont(Font6x8);
  videodisplay.setCursor(x, itemY);
  String cb = sarr[idx] ? "[x] " : "[ ] ";
  videodisplay.print(cb.c_str());
  videodisplay.print(viewToggleLabels[idx].c_str());
}

// Update the Enable button (if you want to change selected style)
void updateEnableButton(bool selected) {
  int wx = (SCREEN_WIDTH - 300) / 2;
  int wy = (SCREEN_HEIGHT - 220) / 2;
  int enableW = 120;
  int enableX = wx + (300 - enableW) / 2;
  int enableY = wy + 28 + VIEW_TOGGLES * VIEW_ROW_H + 6;
  if (selected) {
    videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, ui.blue);
    videodisplay.setTextColor(ui.text, ui.blue);
  } else {
    videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, ui.panelAlt);
    videodisplay.setTextColor(ui.text, ui.panelAlt);
  }
  videodisplay.setFont(Font6x8);
  int labelW = String("APPLY").length() * 6;
  videodisplay.setCursor(enableX + (enableW - labelW) / 2, enableY + 3);
  videodisplay.print("APPLY");
}

void drawProtocolWindow() {
  const int winW = 326, winH = 190;
  drawCenteredWindowBase("Protocol", winW, winH);
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.accent, ui.panel);
  videodisplay.setCursor(wx + 10, wy + 30);
  videodisplay.print("COMMAND        PURPOSE");
  videodisplay.setTextColor(ui.text, ui.panel);
  videodisplay.setCursor(wx + 10, wy + 46);
  String protocol =
    "CBHFA <text>   Send AI prompt\n"
    "CBHFR          Begin response\n"
    "CBHFAR         Render response\n"
    "CBHFT          Finish and save\n"
    "CBHFRM         Read model\n"
    "CBHFRT         Read token\n"
    "CBWR0/1/2      Read Wi-Fi data\n";
  videodisplay.println(protocol.c_str());
}
  
void drawToolsWindow() {
  const int winW = 280, winH = 150;
  drawCenteredWindowBase("Session", winW, winH);
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  drawInfoRow(wx + 10, wy + 30, winW - 20, "CACHED RESPONSE LINES", String(pageLineCount), ui.accent);
  drawInfoRow(wx + 10, wy + 66, winW - 20, "FREE HEAP", String(ESP.getFreeHeap()) + " bytes", ui.blue);
}

void drawHelpWindow() {
  const int winW = 300, winH = 160;
  drawCenteredWindowBase("Help", winW, winH);
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(ui.accent, ui.panel);
  videodisplay.setCursor(wx + 12, wy + 32);
  videodisplay.print("KEYBOARD");
  videodisplay.setTextColor(ui.text, ui.panel);
  videodisplay.setCursor(wx + 12, wy + 48);
  videodisplay.println("Tab       Send prompt");
  videodisplay.println("Up/Down   Scroll response");
  videodisplay.println("Left Ctrl Open menu");
  videodisplay.println("Escape    Close / return home");
  videodisplay.setTextColor(ui.blue, ui.panel);
  videodisplay.println("Mouse wheel and clicks are supported.");
}

void activeWindowBounds(int &wx, int &wy, int &winW, int &winH) {
  winW = 300;
  winH = 160;
  if (activeWindow == 2) {
    winH = 220;
  } else if (activeWindow == 3) {
    winW = 326;
    winH = 190;
  } else if (activeWindow == 4) {
    winW = 280;
    winH = 150;
  }
  wx = (SCREEN_WIDTH - winW) / 2;
  wy = (SCREEN_HEIGHT - winH) / 2;
}

void drawActiveWindow() {
  if (activeWindow < 0 || activeWindow >= MENU_BUTTONS) return;
  switch (activeWindow) {
    case 0: drawConfigWindow(); break;
    case 1: drawParamWindow(); break;
    case 2: drawViewWindow(); break;
    case 3: drawProtocolWindow(); break;
    case 4: drawToolsWindow(); break;
    case 5: drawHelpWindow(); break;
  }
}

bool handleWindowInput(const String &token) {
  if (token == "Escape" || token.equalsIgnoreCase("escape")) {
    if (viewPendingActive) {
      commitViewPending();
    }
    windowMode = false;
    activeWindow = -1;
    drawBrowserMainSurface();
    Serial.println("Window closed via Escape (changes committed when applicable)");
    return true;
  }

  if (token == "LeftArrow" || token.equalsIgnoreCase("leftarrow")) {
    int old = activeWindow;
    activeWindow = (activeWindow - 1 + MENU_BUTTONS) % MENU_BUTTONS;
    menuIndex = activeWindow;
    drawMenuBar();
    if (activeWindow == 2 && !viewPendingActive) {
      for (int i=0;i<VIEW_TOGGLES;++i) viewPendingStates[i] = viewToggleStates[i];
      viewPendingActive = true;
      viewToggleIndex = 0;
    }
    drawActiveWindow();
    Serial.printf("Window switched left: %d -> %d\n", old, activeWindow);
    return true;
  }
  if (token == "RightArrow" || token.equalsIgnoreCase("rightarrow")) {
    int old = activeWindow;
    activeWindow = (activeWindow + 1) % MENU_BUTTONS;
    menuIndex = activeWindow;
    drawMenuBar();
    if (activeWindow == 2 && !viewPendingActive) {
      for (int i=0;i<VIEW_TOGGLES;++i) viewPendingStates[i] = viewToggleStates[i];
      viewPendingActive = true;
      viewToggleIndex = 0;
    }
    drawActiveWindow();
    Serial.printf("Window switched right: %d -> %d\n", old, activeWindow);
    return true;
  }

  if (activeWindow == 2) {
    if (token == "UpArrow") {
      int old = viewToggleIndex;
      viewToggleIndex = (viewToggleIndex - 1 + VIEW_TOGGLES) % VIEW_TOGGLES;
      updateViewToggleRow(old);
      updateViewToggleRow(viewToggleIndex);
      Serial.printf("View: moved selection up -> %d\n", viewToggleIndex);
      return true;
    }
    if (token == "DownArrow") {
      int old = viewToggleIndex;
      viewToggleIndex = (viewToggleIndex + 1) % VIEW_TOGGLES;
      updateViewToggleRow(old);
      updateViewToggleRow(viewToggleIndex);
      Serial.printf("View: moved selection down -> %d\n", viewToggleIndex);
      return true;
    }
    if (token == "Enter" || token.equalsIgnoreCase("enter")) {
      if (!viewPendingActive) {
        for (int i=0;i<VIEW_TOGGLES;++i) viewPendingStates[i] = viewToggleStates[i];
        viewPendingActive = true;
      }
      viewPendingStates[viewToggleIndex] = !viewPendingStates[viewToggleIndex];
      Serial.printf("View: pending toggled '%s' -> %s\n", viewToggleLabels[viewToggleIndex].c_str(), viewPendingStates[viewToggleIndex] ? "ON" : "OFF");
      updateViewToggleRow(viewToggleIndex);
      return true;
    }

    Serial.println(String("View window - ignored token: ") + token);
    return true;
  }

  Serial.println(String("Window input: ") + token + " (window idx " + String(activeWindow) + ")");
  return true;
}

static char usbLineBuf[256];
static size_t usbLinePos = 0;
static char peerLineBuf[4096];
static size_t peerLinePos = 0;
String formatFilenameForPrompt(const String &raw) {
  String s = raw;
  s.trim();
  if (s.length() == 0) return "Prompt";

  // First pass: detect word count
  bool inWord = false;
  int wordCount = 0;

  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool isAlphaNum =
      (c >= '0' && c <= '9') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z');

    if (isAlphaNum) {
      if (!inWord) {
        inWord = true;
        wordCount++;
      }
    } else {
      inWord = false;
    }
  }

  // Second pass: build filename
  String out;
  out.reserve(64);

  inWord = false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool isAlphaNum =
      (c >= '0' && c <= '9') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z');

    if (!isAlphaNum) {
      inWord = false;
      continue;
    }

    if (!inWord) {
      // word start
      inWord = true;
      if (wordCount > 1) {
        out += (char)toupper((unsigned char)c);
      } else {
        out += (char)tolower((unsigned char)c);
      }
    } else {
      out += (char)tolower((unsigned char)c);
    }
  }

  if (out.length() == 0) return "Prompt";
  return out;
}

void processSerialIO() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (usbLinePos > 0) {
        usbLineBuf[usbLinePos] = '\0';
        String cmd = String(usbLineBuf);
        cmd.trim();
        if (cmd.length()) {
          handleLocalCommand(cmd);
        }
        usbLinePos = 0;
      }
    } else {
      if (usbLinePos < sizeof(usbLineBuf) - 1) {
        usbLineBuf[usbLinePos++] = (char)c;
      } else {
        usbLinePos = 0;
      }
    }
  }

  bool sawAnyPeer = false;
  while (Serial1.available()) {
    int c = Serial1.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      if (peerLinePos > 0) {
        peerLineBuf[peerLinePos] = '\0';
        String peerLine = String(peerLineBuf);
        peerLine.trim();
        if (peerLine.length()) {
          handlePeerLine(peerLine);
          sawAnyPeer = true;
        }
        peerLinePos = 0;
      }
    } else {
      if (peerLinePos < sizeof(peerLineBuf) - 1) {
        peerLineBuf[peerLinePos++] = (char)c;
      } else {
        peerLineBuf[peerLinePos] = '\0';
        String peerLine = String(peerLineBuf);
        peerLine.trim();
        if (peerLine.length()) {
          handlePeerLine(peerLine);
          sawAnyPeer = true;
        }
        peerLinePos = 0;
      }
    }
  }

  if (sawAnyPeer) {
    AppCursorDrawGuard cursorGuard;
    if (pendingRectStart != -1) {
      int offset = (printingAnswers || viewMode == VM_ANSWER) ? contentOffsetAfterClear : contentOffsetCurrent;
      displayPage(pageWindowStart, offset);
      drawPendingRectIfVisibleAndReset(offset);
    }
  }

  delay(1);
}

void setup() {
  pinMode(SPEAKER_PIN, 0);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(SERIAL1_BAUD, SERIAL_8N1, SERIAL1_RX_PIN, SERIAL1_TX_PIN);

    bool spiffsReady = SPIFFS.begin(true);
    if (!spiffsReady) Serial.println("SPIFFS mount failed; volatile session mode active.");
    SPI.begin(18,19,23,5);
    if(!SD.begin(5)){
      Serial.println("SD Failed to init");
    } else {
      Serial.println("EUREKA");
    }

    if (spiffsReady) loadViewConfig();
    updateLayoutMetrics();

    if (spiffsReady) boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == "") {
        boolRes = "false";
        if (spiffsReady) boolUpdate();
    }
    started = false;
    if (boolRes == "false") {
      SD.end();
      Serial1.println("res");
      videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
      releaseSerialControllerVideo();
      refreshBrowserPalette(uiInverted);
      if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
      videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ui.canvas);
      contentOffsetCurrent = contentOffsetDefault;
      nextAnswerUsesClearOffset = false;
      ignoreList.push_back("rlsd");
      answerBuffer.reserve(MAX_PAGE_LINES * (LINE_SIZE - 1));
      composeBuffer.reserve(256);
      pageLineCount = 0;
      menuMode = false;
      windowMode = false;
      showSingleTopLeftCompose();
      drawWelcomeScreen();
      Serial.println("Display unit ready.");
      getParam();
      getConfig();
      Serial.println("Type 'help' for commands. Peer messages will appear on screen.");
      noTone(SPEAKER_PIN);
    } else if (boolRes == "trueKernel") {
        noTone(SPEAKER_PIN);
        boolRes = "false";
        boolUpdate();
        handleKernelFlash();
    } else if (boolRes.startsWith("task")){
      noTone(SPEAKER_PIN);
      String task = boolRes.substring(5);
        if (task.startsWith("SPIFF2SD")){
          String direct = task.substring(9);
          direct.trim();
          String srcPath = "/PromptCache.txt";
          String dstPath = "/UserData/Browser/" + direct + ".txt";

          File src = SPIFFS.open(srcPath, FILE_READ);
          if (!src) {
              Serial.println("Failed to open SPIFFS source file");
              return;
          }
          SD.mkdir("/UserData/Browser");
          File dst = SD.open(dstPath, FILE_WRITE);
          if (!dst) {
              Serial.println("Failed to open SD destination file");
              src.close();
              return;
          }
          uint8_t buf[512];
          while (src.available()) {
              size_t n = src.read(buf, sizeof(buf));
              dst.write(buf, n);
          }
          dst.flush();
          dst.close();
          src.close();
          Serial.println("SPIFFS → SD save complete");
          boolRes = "false";
          boolUpdate();
          ESP.restart();
        }
    }
}

void switchToWindow(int idx) {
  if (idx < 0 || idx >= MENU_BUTTONS) return;
  activeWindow = idx;
  windowMode = true;
  if (activeWindow == 2 && !viewPendingActive) {
    for (int i=0;i<VIEW_TOGGLES;++i) viewPendingStates[i] = viewToggleStates[i];
    viewPendingActive = true;
    viewToggleIndex = 0;
  }
  menuIndex = activeWindow;
  drawMenuBar();
  drawActiveWindow();
  Serial.println(String("Entered window mode: ") + menuLabels[idx]);
}

static void closeBrowserWindowFromMouse() {
  if (viewPendingActive) {
    commitViewPending();
  }
  windowMode = false;
  activeWindow = -1;
  drawBrowserMainSurface();
}

static bool handleBrowserMouse(String input) {
  AppMouseReport mouse;
  if (!appMouseRead(input, mouse)) return false;

  appCursorRestore();
  appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);

  if (mouse.wheel != 0 && pageLineCount > 0) {
    int offset = (viewMode == VM_ANSWER) ? contentOffsetAfterClear : contentOffsetCurrent;
    int linesPerScreen = max(1, (SCREEN_HEIGHT - offset - 10) / (R_CHAR_H + 2));
    int maxStart = max(0, pageLineCount - linesPerScreen);
    int nextStart = pageWindowStart + (mouse.wheel > 0 ? -1 : 1);
    displayPage(constrain(nextStart, 0, maxStart), offset);
  }

  if (mouse.leftPressed) {
    if (windowMode) {
      int wx, wy, winW, winH;
      activeWindowBounds(wx, wy, winW, winH);
      if (appCursorX >= wx + winW - 22 && appCursorX < wx + winW &&
          appCursorY >= wy && appCursorY < wy + 22) {
        closeBrowserWindowFromMouse();
        appCursorCaptureAndDraw();
        return true;
      }
    }

    if (appCursorY >= 0 && appCursorY < 29) {
      int btnW = SCREEN_WIDTH / MENU_BUTTONS;
      int idx = constrain(appCursorX / btnW, 0, MENU_BUTTONS - 1);
      menuMode = false;
      switchToWindow(idx);
      appCursorCaptureAndDraw();
      return true;
    }

    if (windowMode && activeWindow == 2) {
      int wx = (SCREEN_WIDTH - 300) / 2;
      int wy = (SCREEN_HEIGHT - 220) / 2;
      int rowX = wx + 6;
      int rowY = wy + 26;
      int rowW = 260;
      for (int i = 0; i < VIEW_TOGGLES; ++i) {
        int itemY = rowY + i * VIEW_ROW_H;
        if (appCursorX >= rowX && appCursorX < rowX + rowW && appCursorY >= itemY && appCursorY < itemY + VIEW_ROW_H) {
          int old = viewToggleIndex;
          viewToggleIndex = i;
          if (!viewPendingActive) {
            for (int j = 0; j < VIEW_TOGGLES; ++j) viewPendingStates[j] = viewToggleStates[j];
            viewPendingActive = true;
          }
          viewPendingStates[viewToggleIndex] = !viewPendingStates[viewToggleIndex];
          if (old != viewToggleIndex) updateViewToggleRow(old);
          updateViewToggleRow(viewToggleIndex);
          appCursorCaptureAndDraw();
          return true;
        }
      }
      int enableW = 120;
      int enableX = wx + (300 - enableW) / 2;
      int enableY = wy + 28 + VIEW_TOGGLES * VIEW_ROW_H + 6;
      if (appCursorX >= enableX && appCursorX < enableX + enableW && appCursorY >= enableY && appCursorY < enableY + VIEW_ROW_H) {
        closeBrowserWindowFromMouse();
        appCursorCaptureAndDraw();
        return true;
      }
    }
  }

  appCursorCaptureAndDraw();
  return true;
}

void handlePeerLine(const String &lineRaw) {
  String tmp = lineRaw;
  tmp.trim();
  String tmpLower = tmp;
  tmpLower.toLowerCase();

  if (handleBrowserMouse(tmp)) return;
  AppCursorDrawGuard cursorGuard;

  if (windowMode) {
    if (handleWindowInput(tmp)) return;
    return;
  }

  if (tmpLower.startsWith("leftctrl") && tmp.endsWith("+")) {
    menuMode = !menuMode;
    if (menuMode) {
      menuIndex = 0;
      Serial.println("Menu mode: ENTER (toggled)");
      drawMenuBar();
    } else {
      Serial.println("Menu mode: EXIT (toggled)");
      drawMenuBar();
    }
    return;
  }

  if (tmpLower.startsWith("leftctrl") && !tmp.endsWith("+")) {
    if (menuMode) {
      menuMode = false;
      Serial.println("Menu mode: EXIT (plain LeftCtrl)");
      drawMenuBar();
    }
    return;
  }

  if (menuMode) {
    if (tmp == "LeftArrow" || tmpLower == "leftarrow") {
      menuIndex = (menuIndex - 1 + MENU_BUTTONS) % MENU_BUTTONS;
      drawMenuBar();
      Serial.printf("Menu moved left -> %d\n", menuIndex);
      return;
    }
    if (tmp == "RightArrow" || tmpLower == "rightarrow") {
      menuIndex = (menuIndex + 1) % MENU_BUTTONS;
      drawMenuBar();
      Serial.printf("Menu moved right -> %d\n", menuIndex);
      return;
    }

    if (tmp == "Enter" || tmpLower == "enter") {
      Serial.println(String("Menu SELECT -> open window: ") + menuLabels[menuIndex]);
      menuMode = false;
      switchToWindow(menuIndex);
      return;
    }

    Serial.println(String("Ignored while menu active: ") + tmp);
    return;
  }

  if (tmp == "RightGUI (Win) +") {
    restartWithFallbackVideo("Restarting");
  }
  if (tmp == "Escape") {
    boolRes = "trueKernel";
    boolUpdate();
    restartWithFallbackVideo("Returning home");
  }

  if (tmpLower == "rlsd") return;

  if (isIgnoredControlToken(tmp)) {
    Serial.println(String("ignored control token: ") + tmp);
    return;
  }
  if (tmp == "CLEARVDD") {
    clearPageBuffer();
    Serial.println("Screen cleared.");
    pendingRectStart = -1;
    pendingRectEnd = -1;
    collectingAnswer = false;
    printingAnswers = false;
    answerBuffer = "";
    viewMode = VM_TOP;
    composeBuffer = "";
    return;
  }

  if (tmp == "UpArrow") {
    if (viewMode == VM_TOP) {
      if (pageLineCount > 0) switchViewToAnswer();
    } else { // VM_ANSWER
      int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
      displayPage(max(0, pageWindowStart - 1), contentOffsetAfterClear);
    }
    return;
  }
  if (tmp == "DownArrow") {
    if (viewMode == VM_TOP) {
      if (pageLineCount > 0) switchViewToAnswer();
    } else {
      int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
      int maxStart = max(0, pageLineCount - linesPerScreen);
      displayPage(min(maxStart, pageWindowStart + 1), contentOffsetAfterClear);
    }
    return;
  }

  if (tmp == "Tab") {
    Serial.println("Tab received -> submit composeBuffer if non-empty");
    submitComposeBuffer();
    return;
  }
  if (tmp == "CBHFA") {
    collectingAnswer = true;
    printingAnswers = false;
    answerBuffer = "";
    aiWaiting = true;
    drawComposePanel();
    Serial.println("CBHFA received: start collecting answer lines (buffering).");
    return;
  }
  if (tmp == "CBHFAR" || tmp == "CBHFR") {
    bool streamAlreadyOpen = printingAnswers;
    collectingAnswer = false;
    printingAnswers = true;
    aiWaiting = (tmp == "CBHFR" && !streamAlreadyOpen);
    drawComposePanel();
    Serial.println("CBHFAR received: accepting buffered answer and displaying it at answer area.");
    if (answerBuffer.length()) {
      int s, e;
      appendPlainTextRange(answerBuffer, s, e);
      if (pendingRectStart == -1) pendingRectStart = s;
      pendingRectEnd = e;
      int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
      if (pageLineCount > linesPerScreen) pageWindowStart = pageLineCount - linesPerScreen;
      else pageWindowStart = 0;
      displayPage(pageWindowStart, contentOffsetAfterClear);
      drawPendingRectIfVisibleAndReset(contentOffsetAfterClear);
    }
    answerBuffer = "";
    return;
  }
  if (tmp == "CBHFT") {
    updateSpiffRom(composeBuffer);
    collectingAnswer = false;
    printingAnswers = false;
    aiWaiting = false;
    answerBuffer = "";
    drawComposePanel();
    Serial.println("CBHFT received: stop printing answers; resume top-left single-line mode.");
    return;
  }

  if (collectingAnswer) {
    String line = sanitizeLine(tmp);
    if (line.length()) {
      const size_t maxAnswerBytes = MAX_PAGE_LINES * (LINE_SIZE - 1);
      size_t remaining = maxAnswerBytes > answerBuffer.length() ? maxAnswerBytes - answerBuffer.length() : 0;
      if (remaining > 1) {
        if (line.length() >= remaining) line.remove(remaining - 1);
        answerBuffer += line;
        answerBuffer += '\n';
      }
      verbosePrint(String("CBHFA-buffered: ") + line);
    }
    return;
  }

  if (printingAnswers) {
    String line = sanitizeLine(tmp);
    if (line.length() == 0) return;
    verbosePrint(String("<- peer (answer area): ") + line);
    int s, e;
    appendPlainTextRange(line, s, e);
    if (pendingRectStart == -1) pendingRectStart = s;
    pendingRectEnd = e;
    int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
    if (pageLineCount > linesPerScreen) pageWindowStart = pageLineCount - linesPerScreen;
    else pageWindowStart = 0;
    return;
  }

  if (tmp.startsWith("CBWR0")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiSSID = arg;
    Serial.println(String("CBWR0 -> WiFi SSID set to: ") + wifiSSID);
    if (windowMode && activeWindow == 0) updateConfigRow(0);
    return;
  }
  if (tmp.startsWith("CBWR1")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiPass = arg;
    Serial.println(String("CBWR1 -> WiFi PASS set to: ") + wifiPass);
    if (windowMode && activeWindow == 0) updateConfigRow(1);
    return;
  }
  if (tmp.startsWith("CBWR2")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiIP = arg;
    Serial.println(String("CBWR2 -> WiFi IP set to: ") + wifiIP);
    if (windowMode && activeWindow == 0) updateConfigRow(2);
    return;
  }

  if (tmp.startsWith("CBHFRM")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfModel = arg;
    Serial.println(String("CBHFRM -> HF Model set to: ") + hfModel);
    if (windowMode && activeWindow == 1) updateParamRow(0);
    return;
  }
  if (tmp.startsWith("CBHFRT")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfToken = arg;
    Serial.println(String("CBHFRT -> HF Token set to: ") + hfToken);
    if (windowMode && activeWindow == 1) updateParamRow(2);
    return;
  }
  if (tmp.startsWith("CBHFRS")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfSpace = arg;
    Serial.println(String("CBHFRS -> HF Space set to: ") + hfSpace);
    if (windowMode && activeWindow == 1) updateParamRow(1);
    return;
  }

  {
    String line = sanitizeLine(tmp);
    if (line.length() == 0) return;
    verbosePrint(String("<- peer (compose-mode): ") + line);
    appendToComposeBufferFromLine(line);
    drawComposeField();
    return;
  }
}

void handleLocalCommand(const String &cmdRaw) {
  String cmd = cmdRaw;
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "help" || cmd == "?") {
    Serial.println("Commands (typed on USB):");
    Serial.println("  send <text>       - send raw text to peer");
    Serial.println("  CB...             - forward CB* or other control lines to peer");
    Serial.println("  clear             - clear screen (and set display window to after-clear offset)");
    Serial.println("  res               - restart this ESP");
    Serial.println("  up / down         - scroll or toggle view");
    Serial.println("  submit            - submit composed buffer to peer (CBHFA <buffer>)");
    Serial.println("  sendbuf           - alias for submit");
    Serial.println("  compose clear     - clear the composition buffer");
    Serial.println("  ignore add <w>    - add a word to ignore dictionary");
    Serial.println("  ignore rm <w>     - remove a word from ignore dictionary");
    Serial.println("  ignore list       - list ignore words");
    Serial.println("  help              - this message");
    Serial.println("--- New: request config/params from peer ---");
    Serial.println("  get cbwr0         - ask peer for WiFi SSID (CBWR0)");
    Serial.println("  get cbwr1         - ask peer for WiFi PASS (CBWR1)");
    Serial.println("  get cbwr2         - ask peer for WiFi IP (CBWR2)");
    Serial.println("  get wifi          - shorthand: request all 3 CBWR* values");
    Serial.println("  get cbhfrm        - ask peer for HF Model (CBHFRM)");
    Serial.println("  get cbhfrt        - ask peer for HF Token (CBHFRT)");
    Serial.println("  get cbhfrs        - ask peer for HF Space (CBHFRS)");
    Serial.println("  get hf            - shorthand: request all 3 CBHF* values");
    return;
  }

  if (cmd.startsWith("send ")) {
    String payload = cmd.substring(5);
    forwardToPeer(payload);
    displayShort(String("Sent to peer: ") + (payload.length() > 20 ? payload.substring(0,20) + "..." : payload));
    return;
  }

  if (cmd == "clear") {
    clearPageBuffer();
    Serial.println("Screen cleared.");
    return;
  }
  if (cmd == "res") {
    Serial.println("Restarting...");
    delay(200);
    ESP.restart();
    return;
  }

  if (cmd == "up") {
    handlePeerLine("UpArrow");
    return;
  }
  if (cmd == "down") {
    handlePeerLine("DownArrow");
    return;
  }

  if (cmd == "submit" || cmd == "sendbuf") {
    submitComposeBuffer();
    return;
  }

  if (cmd == "compose clear") {
    composeBuffer = "";
    showSingleTopLeftCompose();
    Serial.println("composeBuffer cleared.");
    return;
  }

  if (cmd.startsWith("ignore ")) {
    String arg = cmd.substring(7);
    arg.trim();
    if (arg.startsWith("add ")) {
      String w = arg.substring(4);
      w.trim();
      if (w.length()) {
        ignoreList.push_back(w);
        Serial.println(String("ignore add: ") + w);
      }
      return;
    }
    if (arg.startsWith("rm ")) {
      String w = arg.substring(3); w.trim();
      if (w.length()) {
        for (auto it = ignoreList.begin(); it != ignoreList.end(); ++it) {
          if ((*it).equalsIgnoreCase(w)) { ignoreList.erase(it); Serial.println(String("ignore rm: ") + w); break; }
        }
      }
      return;
    }
    if (arg == "list") {
      Serial.println("Ignore list:");
      for (auto &x : ignoreList) Serial.println("  " + x);
      return;
    }
  }

  // scrolling / page navigation
  if (cmd.startsWith("scroll ")) {
    String arg = cmd.substring(7);
    int n = arg.toInt();
    if (viewMode == VM_ANSWER) displayPage(n, contentOffsetAfterClear);
    else displayPage(n);
    return;
  }

  if (cmd == "offset clear") {
    contentOffsetCurrent = contentOffsetAfterClear;
    Serial.println("Offset set to after-clear (80 px).");
    if (viewMode == VM_ANSWER) displayPage(pageWindowStart, contentOffsetAfterClear);
    else displayPage(pageWindowStart);
    return;
  }
  if (cmd == "offset default") {
    contentOffsetCurrent = contentOffsetDefault;
    Serial.println("Offset set to default (15 px).");
    if (viewMode == VM_ANSWER) displayPage(pageWindowStart, contentOffsetAfterClear);
    else displayPage(pageWindowStart);
    return;
  }
  if (cmd.startsWith("offset ")) {
    String arg = cmd.substring(7);
    int n = arg.toInt();
    if (n >= 0 && n < SCREEN_HEIGHT - 10) {
      contentOffsetCurrent = n;
      Serial.println(String("Offset set to ") + n + " px.");
      if (viewMode == VM_ANSWER) displayPage(pageWindowStart, contentOffsetAfterClear);
      else displayPage(pageWindowStart);
    } else {
      Serial.println("Invalid offset value.");
    }
    return;
  }

  if (cmd == "get cbwr0") { Serial1.println("CBWR0"); Serial.println("Requested CBWR0 (WiFi SSID)"); return; }
  if (cmd == "get cbwr1") { Serial1.println("CBWR1"); Serial.println("Requested CBWR1 (WiFi PASS)"); return; }
  if (cmd == "get cbwr2") { Serial1.println("CBWR2"); Serial.println("Requested CBWR2 (WiFi IP)"); return; }
  if (cmd == "get wifi") { Serial1.println("CBWR0"); Serial1.println("CBWR1"); Serial1.println("CBWR2"); Serial.println("Requested CBWR0/1/2 (wifi)"); return; }
  if (cmd == "get cbhfrm") { Serial1.println("CBHFRM"); Serial.println("Requested CBHFRM (HF Model)"); return; }
  if (cmd == "get cbhfrt") { Serial1.println("CBHFRT"); Serial.println("Requested CBHFRT (HF Token)"); return; }
  if (cmd == "get cbhfrs") { Serial1.println("CBHFRS"); Serial.println("Requested CBHFRS (HF Space)"); return; }
  if (cmd == "get hf") { Serial1.println("CBHFRM"); Serial1.println("CBHFRT"); Serial1.println("CBHFRS"); Serial.println("Requested CBHFRM/CBHFRT/CBHFRS (hf)"); return; }

  if (cmd.startsWith("CB") || cmd.startsWith("BLE") || cmd.startsWith("BL")) {
    forwardToPeer(cmd);
    return;
  }

  forwardToPeer(cmd);
}

void loop() {
  processSerialIO();
}

