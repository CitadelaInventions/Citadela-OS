#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <esp_partition.h>
#include <ctype.h>

#define SPEAKER_PIN 12
#define SCREEN_WIDTH 376
#define SCREEN_HEIGHT 192

static const int SERIAL1_RX_PIN = 16;
static const int SERIAL1_TX_PIN = 17;
static const unsigned long SERIAL1_BAUD = 256000UL;

static const char *BROWSER_ROOT = "/UserData/Browser";
static const int TOP_BAR_H = 22;
static const int STATUS_H = 14;
static const int VIEW_X = 4;
static const int VIEW_Y = TOP_BAR_H + 2;
static const int VIEW_W = SCREEN_WIDTH - 8;
static const int VIEW_H = SCREEN_HEIGHT - TOP_BAR_H - STATUS_H - 4;
static const int TEXT_LEFT = VIEW_X + 4;
static const int TEXT_RIGHT = VIEW_X + VIEW_W - 4;
static const int TEXT_TOP = VIEW_Y + 4;
static const int TEXT_BOTTOM = VIEW_Y + VIEW_H - 4;
static const int LIST_START_Y = TEXT_TOP + 14;

static const int MAX_ENTRIES = 48;
static const int ENTRY_NAME_LEN = 42;
static const int ENTRY_PATH_LEN = 112;
static const int LIST_ROW_H = 12;
static const int LIST_VISIBLE_ROWS = 19;
static const int MAX_LINKS = 18;
static const int MAX_STYLE_STACK = 12;
static const int TAG_BUF_MAX = 176;
static const int TEXT_BUF_MAX = 128;

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = SCREEN_WIDTH;
static const int APP_CURSOR_SCREEN_H = SCREEN_HEIGHT;
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

struct BrowserEntry {
  char name[ENTRY_NAME_LEN];
  char path[ENTRY_PATH_LEN];
  bool isDir;
};

struct LinkHit {
  int x;
  int y;
  int w;
  int h;
  char href[ENTRY_PATH_LEN];
};

struct RenderStyle {
  uint32_t fg;
  uint32_t bg;
  bool hasBg;
  bool bold;
  bool underline;
  bool link;
  bool pre;
  uint8_t fontW;
  uint8_t fontH;
  uint8_t lineH;
  int indent;
  char href[ENTRY_PATH_LEN];
};

struct StyleFrame {
  RenderStyle style;
  char tag[12];
};

enum BrowserMode {
  MODE_FILES,
  MODE_PAGE
};

static BrowserEntry entries[MAX_ENTRIES];
static int entryCount = 0;
static int selectedEntry = 0;
static int listTop = 0;
static BrowserMode mode = MODE_FILES;

static LinkHit linkHits[MAX_LINKS];
static int linkHitCount = 0;
static StyleFrame styleStack[MAX_STYLE_STACK];
static int styleDepth = 0;
static RenderStyle style;

static String currentDir = BROWSER_ROOT;
static String currentPagePath = "";
static String statusText = "";
static String boolRes = "";

static int scrollY = 0;
static int maxScrollY = 0;
static int docX = TEXT_LEFT;
static int docY = 0;
static int docMaxY = 0;
static int orderedDepth = 0;
static int orderedCounter[4] = {0, 0, 0, 0};
static bool skipText = false;
static String skipTagName = "";
static int kernelFlashVideoLastPercent = -1;

static uint32_t COL_BLACK;
static uint32_t COL_WHITE;
static uint32_t COL_DIM;
static uint32_t COL_MID;
static uint32_t COL_PANEL;
static uint32_t COL_LINK;
static uint32_t COL_WARN;

#line 157 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool isHtmlName(const String &name);
#line 163 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool isBmpName(const String &name);
#line 169 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String leafName(String path);
#line 175 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String parentDir(String path);
#line 184 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String baseDir(String path);
#line 190 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String normalizePath(String path);
#line 221 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String resolveRelativePath(const String &raw, const String &fromFile);
#line 235 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void copyString(char *dst, size_t dstSize, const String &src);
#line 243 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorLoadConfigOnce();
#line 264 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 269 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 274 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 279 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 284 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 288 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 294 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorRestore();
#line 305 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 312 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorCaptureAndDraw();
#line 342 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void appCursorRefreshAfterRedraw();
#line 352 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 373 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static uint16_t readLE16(uint8_t *buf, int off);
#line 377 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static uint32_t readLE32(uint8_t *buf, int off);
#line 384 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void boolTru();
#line 392 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void boolUpdate();
#line 402 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 413 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 418 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 427 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void handleKernelFlash();
#line 469 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void restartWithFallbackVideo(const char *label);
#line 495 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void setPalette();
#line 505 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void setFontMetrics(uint8_t fw, uint8_t fh, uint8_t lh);
#line 512 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void resetStyle();
#line 529 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void pushStyle(const char *tag);
#line 536 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void popStyle(const String &tag);
#line 547 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawChrome(const String &title);
#line 560 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawStatus(const String &text);
#line 570 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void addEntry(const String &name, const String &path, bool isDir);
#line 578 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void listBrowserDir();
#line 628 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawFileRow(int rowIndex);
#line 647 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawFileBrowser();
#line 670 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void ensureSelectionVisible();
#line 676 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static uint32_t parseColorValue(String value, uint32_t fallback);
#line 705 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String getAttr(const String &rawTag, const String &attr);
#line 736 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void applyInlineStyle(const String &styleText);
#line 766 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static int currentLineLeft();
#line 776 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void startBlock(int margin);
#line 782 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool yVisible(int y, int h);
#line 787 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void addLinkHit(int x, int y, int w, int h, const char *href);
#line 800 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawTextFragment(const String &text);
#line 825 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void emitWord(String word);
#line 845 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String decodeEntity(const String &entity);
#line 863 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void renderTextChunk(const String &chunk);
#line 949 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool bmpInfo(const String &path, int &w, int &h, uint16_t &bpp, uint32_t &pixelOffset, uint32_t &rowStride, bool &topDown);
#line 968 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawBmpScaled(const String &path, int x, int docImageY, int targetW, int targetH, int srcW, int srcH, uint16_t bpp, uint32_t pixelOffset, uint32_t rowStride, bool topDown);
#line 1005 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void renderImageTag(const String &srcRaw, const String &altRaw);
#line 1051 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawHorizontalRule();
#line 1063 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void beginListItem();
#line 1084 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static String tagNameFromRaw(String raw);
#line 1097 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void handleEndTag(const String &name);
#line 1131 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void handleStartTag(String raw, const String &name, bool selfClosing);
#line 1215 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void handleTag(String raw);
#line 1227 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void resetRenderer();
#line 1240 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool renderHtmlPass();
#line 1286 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawPage();
#line 1310 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void openHtmlFile(const String &path);
#line 1318 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool tryOpenLink(const String &href);
#line 1334 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void activateSelectedEntry();
#line 1345 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static int hitFileRow(int x, int y);
#line 1354 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool handleBrowserMouse(String command);
#line 1394 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void handleKeyboardCommand(String command);
#line 1451 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void controller();
#line 1465 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static void drawBootError(const char *message);
#line 1474 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
void setup();
#line 1518 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
void loop();
#line 157 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CHtml\\CHtml.ino"
static bool isHtmlName(const String &name) {
  String low = name;
  low.toLowerCase();
  return low.endsWith(".html") || low.endsWith(".htm");
}

static bool isBmpName(const String &name) {
  String low = name;
  low.toLowerCase();
  return low.endsWith(".bmp");
}

static String leafName(String path) {
  int slash = path.lastIndexOf('/');
  if (slash >= 0) path = path.substring(slash + 1);
  return path;
}

static String parentDir(String path) {
  path.trim();
  if (path.length() <= 1) return "/";
  if (path.endsWith("/")) path.remove(path.length() - 1);
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

static String baseDir(String path) {
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

static String normalizePath(String path) {
  path.replace("\\", "/");
  while (path.indexOf("//") >= 0) path.replace("//", "/");
  if (!path.startsWith("/")) path = "/" + path;

  String parts[12];
  int partCount = 0;
  int start = 1;
  while (start <= path.length() && partCount < 12) {
    int slash = path.indexOf('/', start);
    String part = slash >= 0 ? path.substring(start, slash) : path.substring(start);
    part.trim();
    if (part.length() && part != ".") {
      if (part == "..") {
        if (partCount > 0) partCount--;
      } else {
        parts[partCount++] = part;
      }
    }
    if (slash < 0) break;
    start = slash + 1;
  }

  String out = "/";
  for (int i = 0; i < partCount; ++i) {
    if (i) out += "/";
    out += parts[i];
  }
  return out;
}

static String resolveRelativePath(const String &raw, const String &fromFile) {
  String href = raw;
  href.trim();
  int hash = href.indexOf('#');
  if (hash >= 0) href = href.substring(0, hash);
  int query = href.indexOf('?');
  if (query >= 0) href = href.substring(0, query);
  href.replace("%20", " ");
  if (!href.length()) return "";
  if (href.startsWith("http://") || href.startsWith("https://") || href.startsWith("mailto:")) return href;
  if (href.startsWith("/")) return normalizePath(href);
  return normalizePath(baseDir(fromFile) + "/" + href);
}

static void copyString(char *dst, size_t dstSize, const String &src) {
  if (!dst || dstSize == 0) return;
  String clipped = src;
  if (clipped.length() >= (int)dstSize) clipped = clipped.substring(0, dstSize - 1);
  strncpy(dst, clipped.c_str(), dstSize - 1);
  dst[dstSize - 1] = '\0';
}

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
  for (int i = 0; i < appCursorPixelCount; ++i) {
    int px = appCursorX + appCursorPixels[i].dx;
    int py = appCursorY + appCursorPixels[i].dy;
    bool outlinePoint = appCursorOutlinePoint(appCursorPixels[i].dx, appCursorPixels[i].dy);
    uint32_t cursorColor = COL_WHITE;
    if (appMouseCursorOutlined) cursorColor = outlinePoint ? COL_WHITE : COL_BLACK;
    else cursorColor = appCursorBackgroundLooksBright(appCursorPixels[i].raw) ? COL_BLACK : COL_WHITE;
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

static uint16_t readLE16(uint8_t *buf, int off) {
  return (uint16_t)buf[off] | ((uint16_t)buf[off + 1] << 8);
}

static uint32_t readLE32(uint8_t *buf, int off) {
  return (uint32_t)buf[off] |
         ((uint32_t)buf[off + 1] << 8) |
         ((uint32_t)buf[off + 2] << 16) |
         ((uint32_t)buf[off + 3] << 24);
}

static void boolTru() {
  File file = SPIFFS.open("/evil.txt", FILE_READ);
  String fileContent = "";
  while (file && file.available()) fileContent += (char)file.read();
  boolRes = fileContent;
  if (file) file.close();
}

static void boolUpdate() {
  File file = SPIFFS.open("/evil.txt", FILE_WRITE);
  if (file) {
    file.seek(0);
    file.print("");
    file.println(boolRes);
    file.close();
  }
}

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

static void handleKernelFlash() {
  File kernelFile = SD.open("/System/kernel.bin");
  if (!kernelFile) {
    Serial.println("Kernel file not found.");
    return;
  }

  size_t kernelSize = kernelFile.size();
  kernelFlashVideoStart(kernelSize, "Flashing kernel");
  if (!Update.begin(kernelSize)) {
    kernelFlashVideoProgress(100, "Kernel flash failed");
    kernelFile.close();
    return;
  }

  uint8_t buffer[2048];
  size_t writtenTotal = 0;
  while (kernelFile.available()) {
    int len = kernelFile.read(buffer, sizeof(buffer));
    int written = Update.write(buffer, len);
    if (written != len) {
      Update.abort();
      kernelFlashVideoProgress(100, "Kernel flash failed");
      kernelFile.close();
      return;
    }
    writtenTotal += written;
    kernelFlashVideoBytes(writtenTotal);
  }

  kernelFile.close();
  if (Update.end(true)) {
    kernelFlashVideoBytes(kernelSize);
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
  } else {
    kernelFlashVideoProgress(100, "Kernel flash failed");
  }
}

static void restartWithFallbackVideo(const char *label) {
  const char *text = label ? label : "Returning home";
  Serial1.print("VDPREP 0 ");
  Serial1.println(text);
  Serial1.flush();
  delay(250);
  videodisplay.i2sStop();
  pinMode(25, INPUT_PULLDOWN);
  delay(5);
  Serial1.println("VDINIT");
  Serial1.print("VDPROG 100 ");
  Serial1.println(text);
  Serial1.flush();
  delay(80);
  ESP.restart();
}

static void releaseSerialControllerVideo(uint8_t repeats = 12, uint16_t gapMs = 80) {
  for (uint8_t i = 0; i < repeats; ++i) {
    Serial1.println("CVBSOFF");
    Serial1.println("VDAPPVIDEO");
    Serial1.flush();
    delay(gapMs);
  }
}

static void setPalette() {
  COL_BLACK = videodisplay.RGB(0, 0, 0);
  COL_WHITE = videodisplay.RGB(255, 255, 255);
  COL_DIM = videodisplay.RGB(80, 80, 80);
  COL_MID = videodisplay.RGB(145, 145, 145);
  COL_PANEL = videodisplay.RGB(26, 26, 26);
  COL_LINK = videodisplay.RGB(60, 210, 255);
  COL_WARN = videodisplay.RGB(255, 220, 70);
}

static void setFontMetrics(uint8_t fw, uint8_t fh, uint8_t lh) {
  style.fontW = fw;
  style.fontH = fh;
  style.lineH = lh;
  videodisplay.setFont(fw >= 8 ? Font8x8 : Font6x8);
}

static void resetStyle() {
  style.fg = COL_WHITE;
  style.bg = COL_BLACK;
  style.hasBg = false;
  style.bold = false;
  style.underline = false;
  style.link = false;
  style.pre = false;
  style.fontW = 6;
  style.fontH = 8;
  style.lineH = 10;
  style.indent = 0;
  style.href[0] = '\0';
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(style.fg, COL_BLACK);
}

static void pushStyle(const char *tag) {
  if (styleDepth >= MAX_STYLE_STACK) return;
  styleStack[styleDepth].style = style;
  copyString(styleStack[styleDepth].tag, sizeof(styleStack[styleDepth].tag), String(tag ? tag : ""));
  styleDepth++;
}

static void popStyle(const String &tag) {
  for (int i = styleDepth - 1; i >= 0; --i) {
    if (tag == styleStack[i].tag) {
      style = styleStack[i].style;
      styleDepth = i;
      videodisplay.setFont(style.fontW >= 8 ? Font8x8 : Font6x8);
      return;
    }
  }
}

static void drawChrome(const String &title) {
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BLACK);
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, COL_WHITE);
  videodisplay.setFont(Font8x8);
  videodisplay.setTextColor(COL_BLACK, COL_WHITE);
  videodisplay.setCursor(5, 6);
  String shown = title;
  if (shown.length() > 38) shown = "..." + shown.substring(shown.length() - 35);
  videodisplay.print(shown.c_str());
  videodisplay.rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, COL_DIM);
  videodisplay.fillRect(0, SCREEN_HEIGHT - STATUS_H, SCREEN_WIDTH, STATUS_H, COL_PANEL);
}

static void drawStatus(const String &text) {
  videodisplay.fillRect(0, SCREEN_HEIGHT - STATUS_H, SCREEN_WIDTH, STATUS_H, COL_PANEL);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(COL_WHITE, COL_PANEL);
  videodisplay.setCursor(5, SCREEN_HEIGHT - STATUS_H + 3);
  String clipped = text;
  if (clipped.length() > 62) clipped = clipped.substring(0, 59) + "...";
  videodisplay.print(clipped.c_str());
}

static void addEntry(const String &name, const String &path, bool isDir) {
  if (entryCount >= MAX_ENTRIES) return;
  copyString(entries[entryCount].name, sizeof(entries[entryCount].name), name);
  copyString(entries[entryCount].path, sizeof(entries[entryCount].path), path);
  entries[entryCount].isDir = isDir;
  entryCount++;
}

static void listBrowserDir() {
  entryCount = 0;
  selectedEntry = 0;
  listTop = 0;

  if (!SD.exists(BROWSER_ROOT)) SD.mkdir(BROWSER_ROOT);
  currentDir = normalizePath(currentDir);
  if (!currentDir.startsWith(BROWSER_ROOT)) currentDir = BROWSER_ROOT;
  if (currentDir != BROWSER_ROOT) addEntry("..", parentDir(currentDir), true);

  File dir = SD.open(currentDir);
  if (!dir || !dir.isDirectory()) {
    statusText = "Cannot open " + currentDir;
    if (dir) dir.close();
    return;
  }

  while (entryCount < MAX_ENTRIES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String name = leafName(String(entry.name()));
    if (name.length() == 0 || name.startsWith("._")) {
      entry.close();
      continue;
    }
    String full = normalizePath(currentDir + "/" + name);
    if (entry.isDirectory()) {
      addEntry(name, full, true);
    }
    entry.close();
  }
  dir.rewindDirectory();
  while (entryCount < MAX_ENTRIES) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String name = leafName(String(entry.name()));
    if (name.length() == 0 || name.startsWith("._")) {
      entry.close();
      continue;
    }
    if (!entry.isDirectory() && isHtmlName(name)) {
      addEntry(name, normalizePath(currentDir + "/" + name), false);
    }
    entry.close();
  }
  dir.close();

  statusText = String(entryCount) + " item(s). Enter opens. Escape returns home.";
}

static void drawFileRow(int rowIndex) {
  int visibleIndex = rowIndex - listTop;
  if (visibleIndex < 0 || visibleIndex >= LIST_VISIBLE_ROWS) return;

  int x = VIEW_X + 5;
  int y = LIST_START_Y + visibleIndex * LIST_ROW_H;
  int w = VIEW_W - 10;
  bool selected = rowIndex == selectedEntry;
  uint32_t bg = selected ? COL_WHITE : COL_BLACK;
  uint32_t fg = selected ? COL_BLACK : COL_WHITE;
  videodisplay.fillRect(x, y, w, LIST_ROW_H, bg);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(fg, bg);
  videodisplay.setCursor(x + 4, y + 2);
  String label = entries[rowIndex].isDir ? String("[") + entries[rowIndex].name + "]" : String(entries[rowIndex].name);
  if (label.length() > 55) label = label.substring(0, 52) + "...";
  videodisplay.print(label.c_str());
}

static void drawFileBrowser() {
  AppCursorDrawGuard cursorGuard;
  drawChrome("Citadela HTML Browser");
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(COL_MID, COL_BLACK);
  videodisplay.setCursor(TEXT_LEFT, TEXT_TOP);
  String pathLine = currentDir;
  if (pathLine.length() > 58) pathLine = "..." + pathLine.substring(pathLine.length() - 55);
  videodisplay.print(pathLine.c_str());

  if (entryCount <= 0) {
    videodisplay.setTextColor(COL_WARN, COL_BLACK);
    videodisplay.setCursor(TEXT_LEFT, TEXT_TOP + 22);
    videodisplay.print("No .html or .htm files found.");
    videodisplay.setCursor(TEXT_LEFT, TEXT_TOP + 34);
    videodisplay.print("Put files in /UserData/Browser.");
  } else {
    for (int i = listTop; i < entryCount && i < listTop + LIST_VISIBLE_ROWS; ++i) drawFileRow(i);
  }
  drawStatus(statusText);
  videodisplay.show();
}

static void ensureSelectionVisible() {
  if (selectedEntry < listTop) listTop = selectedEntry;
  if (selectedEntry >= listTop + LIST_VISIBLE_ROWS) listTop = selectedEntry - LIST_VISIBLE_ROWS + 1;
  if (listTop < 0) listTop = 0;
}

static uint32_t parseColorValue(String value, uint32_t fallback) {
  value.trim();
  value.toLowerCase();
  if (!value.length()) return fallback;
  if (value == "white") return COL_WHITE;
  if (value == "black") return COL_BLACK;
  if (value == "gray" || value == "grey") return videodisplay.RGB(128, 128, 128);
  if (value == "red") return videodisplay.RGB(255, 50, 50);
  if (value == "green") return videodisplay.RGB(40, 220, 80);
  if (value == "blue") return videodisplay.RGB(70, 120, 255);
  if (value == "yellow") return videodisplay.RGB(255, 230, 70);
  if (value == "cyan") return COL_LINK;
  if (value == "magenta" || value == "purple") return videodisplay.RGB(230, 80, 255);
  if (value.startsWith("#") && (value.length() == 7 || value.length() == 4)) {
    int r = 255, g = 255, b = 255;
    if (value.length() == 7) {
      r = strtol(value.substring(1, 3).c_str(), nullptr, 16);
      g = strtol(value.substring(3, 5).c_str(), nullptr, 16);
      b = strtol(value.substring(5, 7).c_str(), nullptr, 16);
    } else {
      r = strtol(value.substring(1, 2).c_str(), nullptr, 16) * 17;
      g = strtol(value.substring(2, 3).c_str(), nullptr, 16) * 17;
      b = strtol(value.substring(3, 4).c_str(), nullptr, 16) * 17;
    }
    return videodisplay.RGB(r, g, b);
  }
  return fallback;
}

static String getAttr(const String &rawTag, const String &attr) {
  String low = rawTag;
  low.toLowerCase();
  String needle = attr;
  needle.toLowerCase();
  int p = low.indexOf(needle);
  while (p >= 0) {
    bool leftOk = p == 0 || isspace((unsigned char)low[p - 1]);
    int after = p + needle.length();
    bool rightOk = after < low.length() && (low[after] == '=' || isspace((unsigned char)low[after]));
    if (leftOk && rightOk) break;
    p = low.indexOf(needle, p + 1);
  }
  if (p < 0) return "";
  p += needle.length();
  while (p < rawTag.length() && isspace((unsigned char)rawTag[p])) p++;
  if (p >= rawTag.length() || rawTag[p] != '=') return "";
  p++;
  while (p < rawTag.length() && isspace((unsigned char)rawTag[p])) p++;
  if (p >= rawTag.length()) return "";
  char quote = rawTag[p];
  if (quote == '"' || quote == '\'') {
    int end = rawTag.indexOf(quote, p + 1);
    if (end < 0) return rawTag.substring(p + 1);
    return rawTag.substring(p + 1, end);
  }
  int end = p;
  while (end < rawTag.length() && !isspace((unsigned char)rawTag[end]) && rawTag[end] != '>') end++;
  return rawTag.substring(p, end);
}

static void applyInlineStyle(const String &styleText) {
  String s = styleText;
  s.trim();
  int start = 0;
  while (start < s.length()) {
    int semi = s.indexOf(';', start);
    String decl = semi >= 0 ? s.substring(start, semi) : s.substring(start);
    int colon = decl.indexOf(':');
    if (colon > 0) {
      String key = decl.substring(0, colon);
      String val = decl.substring(colon + 1);
      key.trim();
      val.trim();
      key.toLowerCase();
      if (key == "color") {
        style.fg = parseColorValue(val, style.fg);
      } else if (key == "background" || key == "background-color") {
        style.bg = parseColorValue(val, style.bg);
        style.hasBg = true;
      } else if (key == "font-weight" && (val.indexOf("bold") >= 0 || val.toInt() >= 600)) {
        style.bold = true;
      } else if (key == "text-decoration" && val.indexOf("underline") >= 0) {
        style.underline = true;
      }
    }
    if (semi < 0) break;
    start = semi + 1;
  }
}

static int currentLineLeft() {
  return TEXT_LEFT + style.indent;
}

static void newLine(int extra = 0) {
  docX = currentLineLeft();
  docY += style.lineH + extra;
  if (docY > docMaxY) docMaxY = docY;
}

static void startBlock(int margin) {
  if (docX != currentLineLeft()) newLine();
  docY += margin;
  if (docY > docMaxY) docMaxY = docY;
}

static bool yVisible(int y, int h) {
  int screenY = TEXT_TOP + y - scrollY;
  return screenY + h >= TEXT_TOP && screenY <= TEXT_BOTTOM;
}

static void addLinkHit(int x, int y, int w, int h, const char *href) {
  if (!style.link || !href || !href[0]) return;
  if (linkHitCount >= MAX_LINKS) return;
  int sy = TEXT_TOP + y - scrollY;
  if (sy + h < TEXT_TOP || sy > TEXT_BOTTOM) return;
  linkHits[linkHitCount].x = x;
  linkHits[linkHitCount].y = sy;
  linkHits[linkHitCount].w = w;
  linkHits[linkHitCount].h = h;
  copyString(linkHits[linkHitCount].href, sizeof(linkHits[linkHitCount].href), String(href));
  linkHitCount++;
}

static void drawTextFragment(const String &text) {
  if (!text.length()) return;
  int w = text.length() * style.fontW;
  int screenY = TEXT_TOP + docY - scrollY;
  if (screenY + style.lineH >= TEXT_TOP && screenY <= TEXT_BOTTOM) {
    uint32_t bg = style.hasBg ? style.bg : COL_BLACK;
    if (style.hasBg) videodisplay.fillRect(docX, screenY, w, style.lineH, bg);
    videodisplay.setFont(style.fontW >= 8 ? Font8x8 : Font6x8);
    videodisplay.setTextColor(style.fg, bg);
    videodisplay.setCursor(docX, screenY);
    videodisplay.print(text.c_str());
    if (style.bold) {
      videodisplay.setCursor(docX + 1, screenY);
      videodisplay.print(text.c_str());
    }
    if (style.underline) {
      int uy = screenY + style.fontH + 1;
      videodisplay.line(docX, uy, docX + w - 1, uy, style.fg);
    }
    addLinkHit(docX, docY, w, style.lineH, style.href);
  }
  docX += w;
  if (docY + style.lineH > docMaxY) docMaxY = docY + style.lineH;
}

static void emitWord(String word) {
  if (!word.length()) return;
  int maxW = TEXT_RIGHT - currentLineLeft();
  int spaceW = docX > currentLineLeft() ? style.fontW : 0;
  int wordW = word.length() * style.fontW;

  if (wordW > maxW) {
    for (int i = 0; i < word.length(); ++i) {
      String one = word.substring(i, i + 1);
      if (docX + style.fontW > TEXT_RIGHT) newLine();
      drawTextFragment(one);
    }
    return;
  }

  if (docX + spaceW + wordW > TEXT_RIGHT) newLine();
  if (spaceW) drawTextFragment(" ");
  drawTextFragment(word);
}

static String decodeEntity(const String &entity) {
  if (entity == "amp") return "&";
  if (entity == "lt") return "<";
  if (entity == "gt") return ">";
  if (entity == "quot") return "\"";
  if (entity == "apos") return "'";
  if (entity == "nbsp") return " ";
  if (entity.startsWith("#x")) {
    char c = (char)strtol(entity.substring(2).c_str(), nullptr, 16);
    return String(c);
  }
  if (entity.startsWith("#")) {
    char c = (char)entity.substring(1).toInt();
    return String(c);
  }
  return "&" + entity + ";";
}

static void renderTextChunk(const String &chunk) {
  if (!chunk.length() || skipText) return;
  String word = "";
  String entity = "";
  bool inEntity = false;
  bool sawSpace = false;

  for (int i = 0; i < chunk.length(); ++i) {
    char c = chunk[i];
    if (inEntity) {
      if (c == ';') {
        String decoded = decodeEntity(entity);
        for (int j = 0; j < decoded.length(); ++j) {
          char dc = decoded[j];
          if (!style.pre && isspace((unsigned char)dc)) {
            if (word.length()) {
              emitWord(word);
              word = "";
            }
            sawSpace = true;
          } else if (style.pre && dc == '\n') {
            drawTextFragment(word);
            word = "";
            newLine();
          } else {
            if (sawSpace && !style.pre) {
              if (docX > currentLineLeft()) emitWord("");
              sawSpace = false;
            }
            word += dc;
          }
        }
        entity = "";
        inEntity = false;
      } else if (entity.length() < 12) {
        entity += c;
      } else {
        word += "&" + entity + c;
        entity = "";
        inEntity = false;
      }
      continue;
    }

    if (c == '&') {
      inEntity = true;
      entity = "";
      continue;
    }

    if (style.pre) {
      if (c == '\r') continue;
      if (c == '\n') {
        drawTextFragment(word);
        word = "";
        newLine();
      } else if (c == '\t') {
        word += "    ";
      } else {
        word += c;
        if (word.length() >= 32) {
          drawTextFragment(word);
          word = "";
        }
      }
      continue;
    }

    if (isspace((unsigned char)c)) {
      if (word.length()) {
        emitWord(word);
        word = "";
      }
      sawSpace = true;
    } else {
      sawSpace = false;
      word += c;
    }
  }
  if (inEntity) word += "&" + entity;
  if (word.length()) {
    if (style.pre) drawTextFragment(word);
    else emitWord(word);
  }
}

static bool bmpInfo(const String &path, int &w, int &h, uint16_t &bpp, uint32_t &pixelOffset, uint32_t &rowStride, bool &topDown) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t header[54];
  int got = f.read(header, sizeof(header));
  f.close();
  if (got != 54 || header[0] != 'B' || header[1] != 'M') return false;
  int32_t bw = (int32_t)readLE32(header, 18);
  int32_t bh = (int32_t)readLE32(header, 22);
  bpp = readLE16(header, 28);
  if (bw <= 0 || bh == 0 || (bpp != 24 && bpp != 32)) return false;
  w = bw;
  h = bh > 0 ? bh : -bh;
  topDown = bh < 0;
  pixelOffset = readLE32(header, 10);
  rowStride = ((uint32_t)bpp * (uint32_t)w + 31) / 32 * 4;
  return true;
}

static void drawBmpScaled(const String &path, int x, int docImageY, int targetW, int targetH, int srcW, int srcH, uint16_t bpp, uint32_t pixelOffset, uint32_t rowStride, bool topDown) {
  int screenY = TEXT_TOP + docImageY - scrollY;
  if (screenY + targetH < TEXT_TOP || screenY > TEXT_BOTTOM) return;

  File bmp = SD.open(path, FILE_READ);
  if (!bmp) return;
  uint8_t *row = (uint8_t*)malloc(rowStride);
  if (!row) {
    bmp.close();
    return;
  }

  int bytesPerPixel = bpp / 8;
  for (int dy = 0; dy < targetH; ++dy) {
    int sy = (int)((long long)dy * srcH / targetH);
    int fileRow = topDown ? sy : (srcH - 1 - sy);
    int py = screenY + dy;
    if (py < TEXT_TOP || py > TEXT_BOTTOM) continue;
    bmp.seek(pixelOffset + (uint32_t)fileRow * rowStride);
    int got = bmp.read(row, rowStride);
    if (got < (int)rowStride) memset(row + max(0, got), 0, rowStride - max(0, got));

    for (int dx = 0; dx < targetW; ++dx) {
      int sx = (int)((long long)dx * srcW / targetW);
      int idx = sx * bytesPerPixel;
      if (idx + 2 >= (int)rowStride) continue;
      uint8_t b = row[idx];
      uint8_t g = row[idx + 1];
      uint8_t r = row[idx + 2];
      videodisplay.dotFast(x + dx, py, videodisplay.RGB(r, g, b));
    }
    if ((dy & 0x0f) == 0) yield();
  }
  free(row);
  bmp.close();
}

static void renderImageTag(const String &srcRaw, const String &altRaw) {
  String src = resolveRelativePath(srcRaw, currentPagePath);
  if (!src.length()) return;
  if (docX != currentLineLeft()) newLine(2);

  int srcW = 0, srcH = 0;
  uint16_t bpp = 0;
  uint32_t pixelOffset = 0, rowStride = 0;
  bool topDown = false;
  bool ok = isBmpName(src) && bmpInfo(src, srcW, srcH, bpp, pixelOffset, rowStride, topDown);
  int boxW = TEXT_RIGHT - currentLineLeft();
  int boxH = 28;

  if (ok) {
    float scale = (float)boxW / (float)srcW;
    if (scale > 1.0f) scale = 1.0f;
    boxW = max(1, (int)(srcW * scale));
    boxH = max(1, (int)(srcH * scale));
    if (boxH > 120) {
      scale = 120.0f / (float)srcH;
      boxW = max(1, (int)(srcW * scale));
      boxH = 120;
    }
  }

  int screenY = TEXT_TOP + docY - scrollY;
  if (screenY + boxH >= TEXT_TOP && screenY <= TEXT_BOTTOM) {
    if (ok) {
      drawBmpScaled(src, currentLineLeft(), docY, boxW, boxH, srcW, srcH, bpp, pixelOffset, rowStride, topDown);
      videodisplay.rect(currentLineLeft(), screenY, boxW, boxH, COL_DIM);
    } else {
      videodisplay.rect(currentLineLeft(), screenY, boxW, boxH, COL_MID);
      videodisplay.setFont(Font6x8);
      videodisplay.setTextColor(COL_WARN, COL_BLACK);
      videodisplay.setCursor(currentLineLeft() + 4, screenY + 8);
      String alt = altRaw.length() ? altRaw : "image";
      if (alt.length() > 45) alt = alt.substring(0, 42) + "...";
      videodisplay.print(alt.c_str());
    }
  }

  docY += boxH + 5;
  docX = currentLineLeft();
  if (docY > docMaxY) docMaxY = docY;
}

static void drawHorizontalRule() {
  if (docX != currentLineLeft()) newLine();
  docY += 4;
  int sy = TEXT_TOP + docY - scrollY;
  if (sy >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.line(TEXT_LEFT, sy, TEXT_RIGHT, sy, COL_DIM);
  }
  docY += 6;
  docX = currentLineLeft();
  if (docY > docMaxY) docMaxY = docY;
}

static void beginListItem() {
  if (docX != currentLineLeft()) newLine();
  docY += 1;
  int bulletX = max(TEXT_LEFT, currentLineLeft() - 10);
  int sy = TEXT_TOP + docY - scrollY;
  if (sy + style.lineH >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(style.fg, COL_BLACK);
    videodisplay.setCursor(bulletX, sy);
    if (orderedDepth > 0) {
      int idx = min(orderedDepth - 1, 3);
      orderedCounter[idx]++;
      videodisplay.print(orderedCounter[idx]);
      videodisplay.print(".");
    } else {
      videodisplay.print("*");
    }
  }
  docX = currentLineLeft();
}

static String tagNameFromRaw(String raw) {
  raw.trim();
  if (raw.startsWith("/")) raw = raw.substring(1);
  int sp = raw.indexOf(' ');
  int slash = raw.indexOf('/');
  int end = raw.length();
  if (sp >= 0) end = min(end, sp);
  if (slash >= 0) end = min(end, slash);
  String name = raw.substring(0, end);
  name.toLowerCase();
  return name;
}

static void handleEndTag(const String &name) {
  if (skipText && name == skipTagName) {
    skipText = false;
    skipTagName = "";
    return;
  }
  if (skipText) return;

  if (name == "p" || name == "div" || name == "section" || name == "article" || name == "header" || name == "footer") {
    newLine(4);
    popStyle(name);
  } else if (name == "h1" || name == "h2" || name == "h3") {
    newLine(6);
    popStyle(name);
  } else if (name == "pre") {
    newLine(4);
    popStyle(name);
  } else if (name == "ul") {
    if (docX != currentLineLeft()) newLine(2);
    popStyle(name);
  } else if (name == "ol") {
    if (orderedDepth > 0) orderedDepth--;
    if (docX != currentLineLeft()) newLine(2);
    popStyle(name);
  } else if (name == "li") {
    newLine(1);
    popStyle(name);
  } else if (name == "a" || name == "span" || name == "b" || name == "strong" || name == "i" || name == "em" || name == "u" || name == "code") {
    popStyle(name);
  } else if (name == "body") {
    popStyle(name);
  }
}

static void handleStartTag(String raw, const String &name, bool selfClosing) {
  if (name == "br") {
    newLine();
    return;
  }
  if (name == "hr") {
    drawHorizontalRule();
    return;
  }
  if (name == "img") {
    renderImageTag(getAttr(raw, "src"), getAttr(raw, "alt"));
    return;
  }
  if (name == "script" || name == "style" || name == "head" || name == "svg") {
    skipText = true;
    skipTagName = name;
    return;
  }

  bool known = name == "body" || name == "p" || name == "div" || name == "section" ||
               name == "article" || name == "header" || name == "footer" ||
               name == "h1" || name == "h2" || name == "h3" ||
               name == "ul" || name == "ol" || name == "li" || name == "a" ||
               name == "span" || name == "b" || name == "strong" || name == "i" ||
               name == "em" || name == "u" || name == "pre" || name == "code";
  if (!known) return;

  pushStyle(name.c_str());
  String inlineStyle = getAttr(raw, "style");
  if (inlineStyle.length()) applyInlineStyle(inlineStyle);

  if (name == "body") {
    String bg = getAttr(raw, "bgcolor");
    if (bg.length()) {
      style.bg = parseColorValue(bg, COL_BLACK);
      style.hasBg = true;
      videodisplay.fillRect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, style.bg);
    }
    String fg = getAttr(raw, "text");
    if (fg.length()) style.fg = parseColorValue(fg, style.fg);
  } else if (name == "p" || name == "div" || name == "section" || name == "article" || name == "header" || name == "footer") {
    startBlock(3);
  } else if (name == "h1" || name == "h2" || name == "h3") {
    startBlock(5);
    style.bold = true;
    if (name == "h1") setFontMetrics(8, 8, 16);
    else if (name == "h2") setFontMetrics(8, 8, 14);
    else setFontMetrics(6, 8, 12);
  } else if (name == "ul") {
    style.indent += 14;
    if (docX != currentLineLeft()) newLine();
    docX = currentLineLeft();
  } else if (name == "ol") {
    style.indent += 18;
    if (orderedDepth < 4) orderedCounter[orderedDepth] = 0;
    orderedDepth = min(orderedDepth + 1, 4);
    if (docX != currentLineLeft()) newLine();
    docX = currentLineLeft();
  } else if (name == "li") {
    beginListItem();
  } else if (name == "a") {
    String href = resolveRelativePath(getAttr(raw, "href"), currentPagePath);
    style.link = true;
    style.underline = true;
    style.fg = COL_LINK;
    copyString(style.href, sizeof(style.href), href);
  } else if (name == "b" || name == "strong") {
    style.bold = true;
  } else if (name == "u") {
    style.underline = true;
  } else if (name == "pre") {
    startBlock(4);
    style.pre = true;
    style.bg = COL_PANEL;
    style.hasBg = true;
    setFontMetrics(6, 8, 10);
  } else if (name == "code") {
    style.bg = COL_PANEL;
    style.hasBg = true;
  }

  if (selfClosing) handleEndTag(name);
}

static void handleTag(String raw) {
  raw.trim();
  if (!raw.length()) return;
  if (raw.startsWith("!--") || raw.startsWith("!DOCTYPE") || raw[0] == '?') return;
  bool endTag = raw[0] == '/';
  bool selfClosing = raw.endsWith("/");
  String name = tagNameFromRaw(raw);
  if (!name.length()) return;
  if (endTag) handleEndTag(name);
  else handleStartTag(raw, name, selfClosing);
}

static void resetRenderer() {
  linkHitCount = 0;
  styleDepth = 0;
  resetStyle();
  docX = currentLineLeft();
  docY = 0;
  docMaxY = 0;
  orderedDepth = 0;
  for (int i = 0; i < 4; ++i) orderedCounter[i] = 0;
  skipText = false;
  skipTagName = "";
}

static bool renderHtmlPass() {
  File html = SD.open(currentPagePath, FILE_READ);
  if (!html) return false;

  resetRenderer();
  String textBuf = "";
  String tagBuf = "";
  bool inTag = false;

  while (html.available()) {
    char c = (char)html.read();
    if (inTag) {
      if (c == '>') {
        handleTag(tagBuf);
        tagBuf = "";
        inTag = false;
      } else if (tagBuf.length() < TAG_BUF_MAX) {
        tagBuf += c;
      }
      continue;
    }

    if (c == '<') {
      if (textBuf.length()) {
        renderTextChunk(textBuf);
        textBuf = "";
      }
      inTag = true;
      tagBuf = "";
      continue;
    }

    if (textBuf.length() < TEXT_BUF_MAX) {
      textBuf += c;
    } else {
      renderTextChunk(textBuf);
      textBuf = c;
    }
  }

  if (textBuf.length()) renderTextChunk(textBuf);
  html.close();
  docMaxY = max(docMaxY, docY + style.lineH);
  return true;
}

static void drawPage() {
  AppCursorDrawGuard cursorGuard;
  drawChrome(leafName(currentPagePath));
  videodisplay.fillRect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, COL_BLACK);
  bool ok = renderHtmlPass();
  if (!ok) {
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(COL_WARN, COL_BLACK);
    videodisplay.setCursor(TEXT_LEFT, TEXT_TOP);
    videodisplay.print("Could not open HTML file.");
    maxScrollY = 0;
  } else {
    maxScrollY = max(0, docMaxY - (TEXT_BOTTOM - TEXT_TOP) + 8);
    if (scrollY > maxScrollY) {
      scrollY = maxScrollY;
      drawPage();
      return;
    }
  }
  String stat = String("Scroll ") + scrollY + "/" + maxScrollY + "  Esc: files  Links/BMP local only";
  drawStatus(stat);
  videodisplay.show();
}

static void openHtmlFile(const String &path) {
  currentPagePath = normalizePath(path);
  scrollY = 0;
  maxScrollY = 0;
  mode = MODE_PAGE;
  drawPage();
}

static bool tryOpenLink(const String &href) {
  if (!href.length()) return false;
  if (href.startsWith("http://") || href.startsWith("https://")) {
    statusText = "Network URLs are not supported: " + href;
    drawStatus(statusText);
    return false;
  }
  if (SD.exists(href) && isHtmlName(href)) {
    openHtmlFile(href);
    return true;
  }
  statusText = "Link target is not a local HTML file.";
  drawStatus(statusText);
  return false;
}

static void activateSelectedEntry() {
  if (entryCount <= 0 || selectedEntry < 0 || selectedEntry >= entryCount) return;
  if (entries[selectedEntry].isDir) {
    currentDir = normalizePath(entries[selectedEntry].path);
    listBrowserDir();
    drawFileBrowser();
  } else {
    openHtmlFile(String(entries[selectedEntry].path));
  }
}

static int hitFileRow(int x, int y) {
  if (x < VIEW_X + 5 || x > VIEW_X + VIEW_W - 5) return -1;
  int row = (y - LIST_START_Y) / LIST_ROW_H;
  if (row < 0 || row >= LIST_VISIBLE_ROWS) return -1;
  int idx = listTop + row;
  if (idx < 0 || idx >= entryCount) return -1;
  return idx;
}

static bool handleBrowserMouse(String command) {
  AppMouseReport mouse;
  if (!appMouseRead(command, mouse)) return false;
  appCursorRestore();
  appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);

  if (mode == MODE_FILES) {
    if (mouse.wheel > 0 && selectedEntry > 0) selectedEntry--;
    if (mouse.wheel < 0 && selectedEntry < entryCount - 1) selectedEntry++;
    ensureSelectionVisible();
    if (mouse.wheel != 0) drawFileBrowser();
    if (mouse.leftPressed) {
      int idx = hitFileRow(mouse.x, mouse.y);
      if (idx >= 0) {
        selectedEntry = idx;
        ensureSelectionVisible();
        activateSelectedEntry();
      }
    }
  } else {
    if (mouse.wheel != 0) {
      scrollY += mouse.wheel > 0 ? -28 : 28;
      scrollY = constrain(scrollY, 0, maxScrollY);
      drawPage();
    }
    if (mouse.leftPressed) {
      for (int i = 0; i < linkHitCount; ++i) {
        if (mouse.x >= linkHits[i].x && mouse.x < linkHits[i].x + linkHits[i].w &&
            mouse.y >= linkHits[i].y && mouse.y < linkHits[i].y + linkHits[i].h) {
          tryOpenLink(String(linkHits[i].href));
          break;
        }
      }
    }
  }

  appCursorCaptureAndDraw();
  return true;
}

static void handleKeyboardCommand(String command) {
  command.trim();
  if (!command.length()) return;

  if (handleBrowserMouse(command)) return;

  AppCursorDrawGuard cursorGuard;
  if (command == "RightGUI (Win) +") {
    restartWithFallbackVideo("Restarting");
    return;
  }

  if (mode == MODE_FILES) {
    if (command == "Escape") {
      boolRes = "trueKernel";
      boolUpdate();
      restartWithFallbackVideo("Returning home");
    } else if (command == "UpArrow") {
      if (selectedEntry > 0) selectedEntry--;
      ensureSelectionVisible();
      drawFileBrowser();
    } else if (command == "DownArrow") {
      if (selectedEntry < entryCount - 1) selectedEntry++;
      ensureSelectionVisible();
      drawFileBrowser();
    } else if (command == "LeftArrow" || command == "Backspace") {
      if (currentDir != BROWSER_ROOT) {
        currentDir = parentDir(currentDir);
        if (!currentDir.startsWith(BROWSER_ROOT)) currentDir = BROWSER_ROOT;
        listBrowserDir();
        drawFileBrowser();
      }
    } else if (command == "Enter") {
      activateSelectedEntry();
    }
    return;
  }

  if (command == "Escape" || command == "Backspace" || command == "LeftArrow") {
    mode = MODE_FILES;
    listBrowserDir();
    drawFileBrowser();
  } else if (command == "UpArrow") {
    scrollY = constrain(scrollY - 14, 0, maxScrollY);
    drawPage();
  } else if (command == "DownArrow") {
    scrollY = constrain(scrollY + 14, 0, maxScrollY);
    drawPage();
  } else if (command == "PageUp") {
    scrollY = constrain(scrollY - 90, 0, maxScrollY);
    drawPage();
  } else if (command == "PageDown" || command == "Space") {
    scrollY = constrain(scrollY + 90, 0, maxScrollY);
    drawPage();
  }
}

static void controller() {
  static String line = "";
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      handleKeyboardCommand(line);
      line = "";
    } else if (c != '\r') {
      if (line.length() < 192) line += c;
      else line = "";
    }
  }
}

static void drawBootError(const char *message) {
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BLACK);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(COL_WARN, COL_BLACK);
  videodisplay.setCursor(8, 20);
  videodisplay.print(message);
  videodisplay.show();
}

void setup() {
  pinMode(SPEAKER_PIN, 0);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(SERIAL1_BAUD, SERIAL_8N1, SERIAL1_RX_PIN, SERIAL1_TX_PIN);

  if (!SPIFFS.begin(true)) {
    SPIFFS.format();
    SPIFFS.begin(true);
  }

  SPI.begin(18, 19, 23, 5);
  bool sdReady = SD.begin(5);
  boolTru();
  boolRes.trim();
  if (boolRes.length() == 0) {
    boolRes = "false";
    boolUpdate();
  }

  if (boolRes == "trueKernel") {
    tone(SPEAKER_PIN, 0);
    boolRes = "false";
    boolUpdate();
    handleKernelFlash();
    return;
  }

  releaseSerialControllerVideo(8, 60);
  videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
  releaseSerialControllerVideo(16, 80);
  setPalette();
  resetStyle();
  tone(SPEAKER_PIN, 0);

  if (!sdReady) {
    drawBootError("SD failed to initialize.");
    return;
  }

  listBrowserDir();
  drawFileBrowser();
}

void loop() {
  controller();
  delay(5);
}

