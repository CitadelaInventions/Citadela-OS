#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <FS.h>
#include <Update.h>
#include <ESP32Video.h>
#include <PNGdec.h>
#include <JPEGDEC.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <ctype.h>
#include <new>
#include "../../../System/Libraries/CitadelaDisplay.h"

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
static const int LIST_VISIBLE_ROWS = 11;
static const int MAX_LINKS = 18;
static const int MAX_STYLE_STACK = 12;
static const int MAX_LAYOUT_BOXES = 40;
static const int MAX_CSS_RULES = 24;
static const int CSS_SELECTOR_LEN = 36;
static const int CSS_DECLARATION_LEN = 176;
static const int TAG_BUF_MAX = 320;
static const int TEXT_BUF_MAX = 128;
static const int MAX_IMAGE_SOURCE_WIDTH = 1600;
static const int MAX_IMAGE_HEIGHT = 126;

static Citadela::CitCompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = SCREEN_WIDTH;
static const int APP_CURSOR_SCREEN_H = SCREEN_HEIGHT;
static const int APP_CURSOR_RADIUS = 3;
static const int APP_CURSOR_OUTLINE_RADIUS = 4;
static const int APP_CURSOR_MAX_PIXELS = 81;

typedef Citadela::CitCompositeColorDAC::BufferGraphicsUnit AppCursorRawPixel;

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

enum HitKind : uint8_t {
  HIT_LINK,
  HIT_BUTTON
};

struct LinkHit {
  int x;
  int y;
  int w;
  int h;
  HitKind kind;
  char href[ENTRY_PATH_LEN];
};

struct RenderStyle {
  uint32_t fg;
  uint32_t bg;
  uint32_t textBg;
  uint32_t border;
  bool hasBg;
  bool hasBorder;
  bool bold;
  bool underline;
  bool link;
  bool pre;
  bool hidden;
  uint8_t fontW;
  uint8_t fontH;
  uint8_t lineH;
  uint8_t borderW;
  uint8_t padding;
  uint8_t marginTop;
  uint8_t marginBottom;
  uint8_t textAlign;
  int indent;
  int right;
  int requestedW;
  int requestedH;
  char href[ENTRY_PATH_LEN];
};

struct StyleFrame {
  RenderStyle style;
  char tag[12];
  bool block;
  int boxIndex;
  int boxY;
  int requestedH;
};

struct LayoutBox {
  int x;
  int y;
  int w;
  int h;
  uint32_t bg;
  uint32_t border;
  uint8_t borderW;
  bool hasBg;
  bool hasBorder;
};

struct CssRule {
  char selector[CSS_SELECTOR_LEN];
  char declarations[CSS_DECLARATION_LEN];
};

struct ImageDrawContext {
  int x;
  int screenY;
  int targetW;
  int targetH;
  int sourceW;
  int sourceH;
  int decodedW;
  int decodedH;
  uint32_t background;
  uint16_t *line565;
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
static LayoutBox layoutBoxes[MAX_LAYOUT_BOXES];
static int layoutBoxCount = 0;
static int layoutBoxCursor = 0;
static CssRule cssRules[MAX_CSS_RULES];
static int cssRuleCount = 0;
static bool layoutOnly = false;
static int hiddenDepth = 0;
static String pageTitle = "";
static String controlCaptureTag = "";
static String controlCaptureRaw = "";
static String controlCaptureText = "";
static bool controlCapture = false;
static int focusedHit = -1;
static bool layoutValid = false;
static int measuredDocHeight = 0;
static uint32_t documentBackground = 0;
static int tableCellIndex = 0;
static String pageHistory[6];
static int pageHistoryDepth = 0;

static File imageDecodeFile;
static PNG *activePngDecoder = nullptr;
static ImageDrawContext imageDrawContext;

static String currentDir = BROWSER_ROOT;
static String currentPagePath = "";
static String statusText = "";
static String boolRes = "";

static int scrollY = 0;
static int maxScrollY = 0;
static int docX = TEXT_LEFT;
static int docY = 0;
static int docMaxY = 0;
static int lineHeightUsed = 0;
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
static uint32_t COL_SURFACE;
static uint32_t COL_SURFACE_ALT;
static uint32_t COL_ACCENT;
static uint32_t COL_ACCENT_DARK;
static uint32_t COL_SUCCESS;

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

static bool isPngName(const String &name) {
  String low = name;
  low.toLowerCase();
  return low.endsWith(".png");
}

static bool isJpegName(const String &name) {
  String low = name;
  low.toLowerCase();
  return low.endsWith(".jpg") || low.endsWith(".jpeg");
}

static bool isSupportedImageName(const String &name) {
  return isBmpName(name) || isPngName(name) || isJpegName(name);
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
  COL_WHITE = videodisplay.RGB(235, 242, 240);
  COL_DIM = videodisplay.RGB(55, 77, 74);
  COL_MID = videodisplay.RGB(145, 163, 159);
  COL_PANEL = videodisplay.RGB(17, 25, 24);
  COL_SURFACE = videodisplay.RGB(25, 38, 36);
  COL_SURFACE_ALT = videodisplay.RGB(35, 51, 48);
  COL_ACCENT = videodisplay.RGB(58, 225, 191);
  COL_ACCENT_DARK = videodisplay.RGB(23, 117, 101);
  COL_LINK = videodisplay.RGB(82, 227, 255);
  COL_WARN = videodisplay.RGB(255, 210, 84);
  COL_SUCCESS = videodisplay.RGB(105, 235, 150);
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
  style.textBg = COL_BLACK;
  style.border = COL_DIM;
  style.hasBg = false;
  style.hasBorder = false;
  style.bold = false;
  style.underline = false;
  style.link = false;
  style.pre = false;
  style.hidden = false;
  style.fontW = 6;
  style.fontH = 8;
  style.lineH = 10;
  style.borderW = 0;
  style.padding = 0;
  style.marginTop = 0;
  style.marginBottom = 0;
  style.textAlign = 0;
  style.indent = 0;
  style.right = TEXT_RIGHT;
  style.requestedW = -1;
  style.requestedH = -1;
  style.href[0] = '\0';
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(style.fg, COL_BLACK);
}

static void pushStyle(const char *tag) {
  if (styleDepth >= MAX_STYLE_STACK) return;
  styleStack[styleDepth].style = style;
  copyString(styleStack[styleDepth].tag, sizeof(styleStack[styleDepth].tag), String(tag ? tag : ""));
  styleStack[styleDepth].block = false;
  styleStack[styleDepth].boxIndex = -1;
  styleStack[styleDepth].boxY = docY;
  styleStack[styleDepth].requestedH = -1;
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
  videodisplay.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, COL_SURFACE_ALT);
  videodisplay.fillRect(0, 0, 3, TOP_BAR_H, COL_ACCENT);
  videodisplay.setFont(Font6x8);
  if (mode == MODE_PAGE) {
    videodisplay.fillRect(6, 4, 16, 14, COL_ACCENT_DARK);
    videodisplay.setTextColor(COL_WHITE, COL_ACCENT_DARK);
    videodisplay.setCursor(11, 7);
    videodisplay.print("<");
  }
  videodisplay.setTextColor(COL_WHITE, COL_SURFACE_ALT);
  videodisplay.setCursor(mode == MODE_PAGE ? 28 : 8, 7);
  String shown = title;
  if (shown.length() > 46) shown = "..." + shown.substring(shown.length() - 43);
  videodisplay.print(shown.c_str());
  videodisplay.setTextColor(COL_ACCENT, COL_SURFACE_ALT);
  videodisplay.setCursor(SCREEN_WIDTH - 42, 7);
  videodisplay.print("HTML");
  videodisplay.rect(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, COL_ACCENT_DARK);
  videodisplay.fillRect(0, SCREEN_HEIGHT - STATUS_H, SCREEN_WIDTH, STATUS_H, COL_PANEL);
}

static void drawStatus(const String &text) {
  videodisplay.fillRect(0, SCREEN_HEIGHT - STATUS_H, SCREEN_WIDTH, STATUS_H, COL_PANEL);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(COL_MID, COL_PANEL);
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
  uint32_t bg = selected ? COL_ACCENT_DARK : ((rowIndex & 1) ? COL_PANEL : COL_BLACK);
  uint32_t fg = selected ? COL_WHITE : COL_MID;
  videodisplay.fillRect(x, y, w, LIST_ROW_H, bg);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(fg, bg);
  videodisplay.setCursor(x + 5, y + 2);
  videodisplay.print(entries[rowIndex].isDir ? ">" : "#");
  videodisplay.setTextColor(selected ? COL_WHITE : (entries[rowIndex].isDir ? COL_ACCENT : COL_WHITE), bg);
  videodisplay.setCursor(x + 17, y + 2);
  String label = String(entries[rowIndex].name);
  if (label.length() > 52) label = label.substring(0, 49) + "...";
  videodisplay.print(label.c_str());
}

static void drawFileBrowser() {
  AppCursorDrawGuard cursorGuard;
  drawChrome("Citadela HTML Browser");
  videodisplay.fillRect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, COL_BLACK);
  videodisplay.fillRect(TEXT_LEFT - 2, TEXT_TOP - 1, TEXT_RIGHT - TEXT_LEFT + 4, 12, COL_SURFACE);
  videodisplay.setFont(Font6x8);
  videodisplay.setTextColor(COL_ACCENT, COL_SURFACE);
  videodisplay.setCursor(TEXT_LEFT + 3, TEXT_TOP + 1);
  String pathLine = currentDir;
  if (pathLine.length() > 54) pathLine = "..." + pathLine.substring(pathLine.length() - 51);
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
  value.replace("!important", "");
  value.trim();
  if (!value.length()) return fallback;
  String compact = value;
  compact.replace(" ", "");
  if (value == "white") return COL_WHITE;
  if (value == "black") return COL_BLACK;
  if (value == "gray" || value == "grey") return videodisplay.RGB(128, 128, 128);
  if (value == "darkgray" || value == "darkgrey") return videodisplay.RGB(70, 78, 76);
  if (value == "lightgray" || value == "lightgrey") return videodisplay.RGB(185, 196, 193);
  if (value == "red") return videodisplay.RGB(255, 50, 50);
  if (value == "green") return videodisplay.RGB(40, 220, 80);
  if (value == "blue") return videodisplay.RGB(70, 120, 255);
  if (value == "yellow") return videodisplay.RGB(255, 230, 70);
  if (value == "cyan") return COL_LINK;
  if (value == "aqua") return COL_ACCENT;
  if (value == "orange") return videodisplay.RGB(255, 154, 62);
  if (value == "pink") return videodisplay.RGB(255, 120, 184);
  if (value == "lime") return videodisplay.RGB(130, 245, 90);
  if (value == "navy") return videodisplay.RGB(35, 60, 125);
  if (value == "teal") return videodisplay.RGB(30, 150, 135);
  if (value == "magenta" || value == "purple") return videodisplay.RGB(230, 80, 255);
  if (compact.startsWith("rgb(") && compact.endsWith(")")) {
    int r = 0, g = 0, b = 0;
    if (sscanf(compact.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3) {
      return videodisplay.RGB(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
    }
  }
  if (compact.startsWith("rgba(") && compact.endsWith(")")) {
    int r = 0, g = 0, b = 0;
    if (sscanf(compact.c_str(), "rgba(%d,%d,%d", &r, &g, &b) == 3) {
      return videodisplay.RGB(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
    }
  }
  if (compact.startsWith("#") && (compact.length() == 9 || compact.length() == 7 || compact.length() == 4)) {
    int r = 255, g = 255, b = 255;
    if (compact.length() >= 7) {
      r = strtol(compact.substring(1, 3).c_str(), nullptr, 16);
      g = strtol(compact.substring(3, 5).c_str(), nullptr, 16);
      b = strtol(compact.substring(5, 7).c_str(), nullptr, 16);
    } else {
      r = strtol(compact.substring(1, 2).c_str(), nullptr, 16) * 17;
      g = strtol(compact.substring(2, 3).c_str(), nullptr, 16) * 17;
      b = strtol(compact.substring(3, 4).c_str(), nullptr, 16) * 17;
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

static bool hasAttr(const String &rawTag, String attr) {
  String low = rawTag;
  low.toLowerCase();
  attr.toLowerCase();
  int p = low.indexOf(attr);
  while (p >= 0) {
    bool leftOk = p == 0 || isspace((unsigned char)low[p - 1]);
    int after = p + attr.length();
    bool rightOk = after >= low.length() || isspace((unsigned char)low[after]) ||
                   low[after] == '=' || low[after] == '/' || low[after] == '>';
    if (leftOk && rightOk) return true;
    p = low.indexOf(attr, p + 1);
  }
  return false;
}

static String extractCssColor(String value) {
  value.replace("!important", "");
  value.trim();
  int hash = value.indexOf('#');
  if (hash >= 0) {
    int end = hash + 1;
    while (end < value.length() && isxdigit((unsigned char)value[end]) && end - hash <= 8) end++;
    return value.substring(hash, end);
  }
  String low = value;
  low.toLowerCase();
  int rgb = low.indexOf("rgb(");
  if (rgb < 0) rgb = low.indexOf("rgba(");
  if (rgb >= 0) {
    int end = low.indexOf(')', rgb);
    if (end >= 0) return low.substring(rgb, end + 1);
  }
  const char *names[] = {
    "transparent", "darkgray", "darkgrey", "lightgray", "lightgrey", "magenta",
    "purple", "white", "black", "gray", "grey", "yellow", "orange", "green",
    "blue", "cyan", "aqua", "pink", "lime", "navy", "teal", "red"
  };
  for (const char *name : names) {
    if (low.indexOf(name) >= 0) return String(name);
  }
  return value;
}

static int parseCssLength(String value, int percentBase, int fallback) {
  value.replace("!important", "");
  value.trim();
  value.toLowerCase();
  if (!value.length() || value == "auto" || value == "initial" || value == "inherit") return fallback;
  int space = value.indexOf(' ');
  if (space > 0) value = value.substring(0, space);
  float amount = value.toFloat();
  if (value.endsWith("%")) return (int)(percentBase * amount / 100.0f + 0.5f);
  if (value.endsWith("rem") || value.endsWith("em")) return (int)(amount * 8.0f + 0.5f);
  return (int)(amount + 0.5f);
}

static void applyStyleDeclarations(const String &styleText) {
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
        style.fg = parseColorValue(extractCssColor(val), style.fg);
      } else if (key == "background" || key == "background-color") {
        String color = extractCssColor(val);
        if (color == "transparent") {
          style.hasBg = false;
        } else {
          style.bg = parseColorValue(color, style.textBg);
          style.textBg = style.bg;
          style.hasBg = true;
        }
      } else if (key == "border" || key == "border-color") {
        String color = extractCssColor(val);
        if (val.indexOf("none") >= 0 || color == "transparent") {
          style.hasBorder = false;
          style.borderW = 0;
        } else {
          style.border = parseColorValue(color, style.border);
          style.borderW = key == "border" ? constrain(parseCssLength(val, 0, 1), 1, 3) : max((int)style.borderW, 1);
          style.hasBorder = true;
        }
      } else if (key == "border-width") {
        style.borderW = constrain(parseCssLength(val, 0, style.borderW), 0, 3);
        style.hasBorder = style.borderW > 0;
      } else if (key == "padding" || key == "padding-left" || key == "padding-right" ||
                 key == "padding-top" || key == "padding-bottom") {
        style.padding = constrain(parseCssLength(val, style.right - TEXT_LEFT, style.padding), 0, 16);
      } else if (key == "margin" || key == "margin-top") {
        style.marginTop = constrain(parseCssLength(val, style.right - TEXT_LEFT, style.marginTop), 0, 24);
        if (key == "margin") style.marginBottom = style.marginTop;
      } else if (key == "margin-bottom") {
        style.marginBottom = constrain(parseCssLength(val, style.right - TEXT_LEFT, style.marginBottom), 0, 24);
      } else if (key == "width" || key == "max-width" || key == "min-width") {
        int available = max(1, style.right - (TEXT_LEFT + style.indent));
        int parsed = parseCssLength(val, available, style.requestedW);
        if (key == "max-width" && style.requestedW > 0) style.requestedW = min(style.requestedW, parsed);
        else if (key == "min-width" && style.requestedW > 0) style.requestedW = max(style.requestedW, parsed);
        else style.requestedW = parsed;
      } else if (key == "height" || key == "min-height") {
        int parsed = parseCssLength(val, VIEW_H, style.requestedH);
        if (key == "min-height" && style.requestedH > 0) style.requestedH = max(style.requestedH, parsed);
        else style.requestedH = parsed;
      } else if (key == "font-weight" && (val.indexOf("bold") >= 0 || val.toInt() >= 600)) {
        style.bold = true;
      } else if (key == "font-weight" && val.indexOf("normal") >= 0) {
        style.bold = false;
      } else if (key == "font-size") {
        int px = parseCssLength(val, 8, style.fontW);
        setFontMetrics(px >= 11 ? 8 : 6, 8, px >= 11 ? 13 : 10);
      } else if (key == "line-height") {
        style.lineH = constrain(parseCssLength(val, style.lineH, style.lineH), 8, 24);
      } else if (key == "text-decoration") {
        if (val.indexOf("none") >= 0) style.underline = false;
        else if (val.indexOf("underline") >= 0) style.underline = true;
      } else if (key == "text-align") {
        if (val.indexOf("center") >= 0) style.textAlign = 1;
        else if (val.indexOf("right") >= 0) style.textAlign = 2;
        else style.textAlign = 0;
      } else if (key == "white-space" && (val.indexOf("pre") >= 0)) {
        style.pre = true;
      } else if ((key == "display" && val.indexOf("none") >= 0) ||
                 (key == "visibility" && val.indexOf("hidden") >= 0)) {
        style.hidden = true;
      }
    }
    if (semi < 0) break;
    start = semi + 1;
  }
}

static void applyInlineStyle(const String &styleText) {
  applyStyleDeclarations(styleText);
}

static bool classListContains(String classes, String wanted) {
  classes.trim();
  wanted.trim();
  if (!classes.length() || !wanted.length()) return false;
  classes = " " + classes + " ";
  return classes.indexOf(" " + wanted + " ") >= 0;
}

static bool selectorMatches(String selector, const String &tag, const String &classes, const String &id) {
  selector.trim();
  selector.toLowerCase();
  if (!selector.length() || selector.startsWith("@")) return false;

  int space = selector.lastIndexOf(' ');
  if (space >= 0) selector = selector.substring(space + 1);
  int child = selector.lastIndexOf('>');
  if (child >= 0) selector = selector.substring(child + 1);
  selector.trim();
  int pseudo = selector.indexOf(':');
  if (pseudo >= 0) selector = selector.substring(0, pseudo);
  int attr = selector.indexOf('[');
  if (attr >= 0) selector = selector.substring(0, attr);
  if (!selector.length()) return false;

  int marker = selector.length();
  int dot = selector.indexOf('.');
  int hash = selector.indexOf('#');
  if (dot >= 0) marker = min(marker, dot);
  if (hash >= 0) marker = min(marker, hash);
  String tagPart = selector.substring(0, marker);
  if (tagPart.length() && tagPart != "*" && tagPart != tag) return false;

  int p = marker;
  while (p < selector.length()) {
    char kind = selector[p++];
    int end = p;
    while (end < selector.length() && selector[end] != '.' && selector[end] != '#') end++;
    String token = selector.substring(p, end);
    if (kind == '.' && !classListContains(classes, token)) return false;
    if (kind == '#' && token != id) return false;
    p = end;
  }
  return true;
}

static void addCssRule(String selectors, String declarations) {
  declarations.trim();
  if (!selectors.length() || !declarations.length()) return;
  int start = 0;
  while (start < selectors.length() && cssRuleCount < MAX_CSS_RULES) {
    int comma = selectors.indexOf(',', start);
    String selector = comma >= 0 ? selectors.substring(start, comma) : selectors.substring(start);
    selector.trim();
    if (selector.length() && !selector.startsWith("@")) {
      copyString(cssRules[cssRuleCount].selector, sizeof(cssRules[cssRuleCount].selector), selector);
      copyString(cssRules[cssRuleCount].declarations, sizeof(cssRules[cssRuleCount].declarations), declarations);
      cssRuleCount++;
    }
    if (comma < 0) break;
    start = comma + 1;
  }
}

static void parseCssText(String css) {
  int comment = css.indexOf("/*");
  while (comment >= 0) {
    int end = css.indexOf("*/", comment + 2);
    if (end < 0) {
      css.remove(comment);
      break;
    }
    css.remove(comment, end + 2 - comment);
    comment = css.indexOf("/*");
  }

  int p = 0;
  while (p < css.length() && cssRuleCount < MAX_CSS_RULES) {
    int open = css.indexOf('{', p);
    if (open < 0) break;
    int close = css.indexOf('}', open + 1);
    if (close < 0) break;
    addCssRule(css.substring(p, open), css.substring(open + 1, close));
    p = close + 1;
  }
}

static void loadDocumentMetadata() {
  cssRuleCount = 0;
  pageTitle = leafName(currentPagePath);
  File html = SD.open(currentPagePath, FILE_READ);
  if (!html) return;

  size_t limit = min((size_t)html.size(), (size_t)16384);
  String source;
  source.reserve(limit + 1);
  while (html.available() && source.length() < limit) source += (char)html.read();
  html.close();

  String low = source;
  low.toLowerCase();
  int titleStart = low.indexOf("<title");
  if (titleStart >= 0) {
    titleStart = low.indexOf('>', titleStart);
    int titleEnd = titleStart >= 0 ? low.indexOf("</title>", titleStart + 1) : -1;
    if (titleStart >= 0 && titleEnd > titleStart) {
      pageTitle = source.substring(titleStart + 1, titleEnd);
      pageTitle.replace("\r", " ");
      pageTitle.replace("\n", " ");
      pageTitle.trim();
      if (pageTitle.length() > 54) pageTitle = pageTitle.substring(0, 51) + "...";
    }
  }

  int search = 0;
  while (search < low.length() && cssRuleCount < MAX_CSS_RULES) {
    int styleStart = low.indexOf("<style", search);
    if (styleStart < 0) break;
    styleStart = low.indexOf('>', styleStart);
    int styleEnd = styleStart >= 0 ? low.indexOf("</style>", styleStart + 1) : -1;
    if (styleStart < 0 || styleEnd < 0) break;
    parseCssText(source.substring(styleStart + 1, styleEnd));
    search = styleEnd + 8;
  }
}

static void resetElementLocalStyle() {
  style.bg = style.textBg;
  style.hasBg = false;
  style.border = COL_DIM;
  style.hasBorder = false;
  style.borderW = 0;
  style.padding = 0;
  style.marginTop = 0;
  style.marginBottom = 0;
  style.requestedW = -1;
  style.requestedH = -1;
  style.hidden = false;
}

static void applyCssForElement(const String &name, const String &raw) {
  String classes = getAttr(raw, "class");
  String id = getAttr(raw, "id");
  classes.toLowerCase();
  id.toLowerCase();
  for (int i = 0; i < cssRuleCount; ++i) {
    if (selectorMatches(String(cssRules[i].selector), name, classes, id)) {
      applyStyleDeclarations(String(cssRules[i].declarations));
    }
  }
  String inlineStyle = getAttr(raw, "style");
  if (inlineStyle.length()) applyInlineStyle(inlineStyle);
  String align = getAttr(raw, "align");
  align.toLowerCase();
  if (align == "center") style.textAlign = 1;
  else if (align == "right") style.textAlign = 2;
}

static int currentLineLeft() {
  return TEXT_LEFT + style.indent;
}

static int currentLineRight() {
  return min(TEXT_RIGHT, style.right);
}

static void newLine(int extra = 0) {
  docX = currentLineLeft();
  docY += max((int)style.lineH, lineHeightUsed) + extra;
  lineHeightUsed = 0;
  if (docY > docMaxY) docMaxY = docY;
}

static void startBlock(int margin) {
  if (docX != currentLineLeft()) newLine();
  docY += margin;
  if (docY > docMaxY) docMaxY = docY;
}

static bool styleHasVisualBox() {
  return style.hasBg || style.hasBorder;
}

static void beginElementBlock(int defaultTopMargin, bool forceBox = false) {
  int parentLeft = currentLineLeft();
  int parentRight = currentLineRight();
  if (docX != parentLeft) newLine();
  docY += max(defaultTopMargin, (int)style.marginTop);

  int available = max(1, parentRight - parentLeft);
  int boxW = style.requestedW > 0 ? constrain(style.requestedW, 1, available) : available;
  int boxX = parentLeft;
  if (boxW < available && style.textAlign == 1) boxX += (available - boxW) / 2;
  else if (boxW < available && style.textAlign == 2) boxX += available - boxW;

  bool visual = forceBox || styleHasVisualBox();
  int boxIndex = -1;
  if (visual) {
    if (layoutOnly) {
      if (layoutBoxCount < MAX_LAYOUT_BOXES) {
        boxIndex = layoutBoxCount++;
        layoutBoxes[boxIndex].x = boxX;
        layoutBoxes[boxIndex].y = docY;
        layoutBoxes[boxIndex].w = boxW;
        layoutBoxes[boxIndex].h = 1;
        layoutBoxes[boxIndex].bg = style.bg;
        layoutBoxes[boxIndex].border = style.border;
        layoutBoxes[boxIndex].borderW = max((int)style.borderW, forceBox ? 1 : 0);
        layoutBoxes[boxIndex].hasBg = style.hasBg;
        layoutBoxes[boxIndex].hasBorder = style.hasBorder || forceBox;
      }
    } else if (layoutBoxCursor < layoutBoxCount) {
      boxIndex = layoutBoxCursor++;
    }
  }

  if (styleDepth > 0) {
    StyleFrame &frame = styleStack[styleDepth - 1];
    frame.block = true;
    frame.boxIndex = boxIndex;
    frame.boxY = docY;
    frame.requestedH = style.requestedH;
  }

  int edge = style.padding + (style.hasBorder ? style.borderW : 0);
  style.indent = boxX - TEXT_LEFT + edge;
  style.right = max(currentLineLeft() + 1, boxX + boxW - edge);
  docY += edge;
  docX = currentLineLeft();
  if (docY > docMaxY) docMaxY = docY;
}

static void endElementBlock(const String &name, int defaultBottomMargin) {
  if (styleDepth <= 0) return;
  StyleFrame &frame = styleStack[styleDepth - 1];
  if (name != frame.tag) {
    popStyle(name);
    return;
  }

  if (docX != currentLineLeft()) newLine();
  int edge = style.padding + (style.hasBorder ? style.borderW : 0);
  docY += edge;
  int contentH = max(1, docY - frame.boxY);
  if (frame.requestedH > contentH) {
    docY += frame.requestedH - contentH;
    contentH = frame.requestedH;
  }
  if (layoutOnly && frame.boxIndex >= 0 && frame.boxIndex < layoutBoxCount) {
    layoutBoxes[frame.boxIndex].h = contentH;
  }
  int bottomMargin = max(defaultBottomMargin, (int)style.marginBottom);
  popStyle(name);
  docY += bottomMargin;
  docX = currentLineLeft();
  if (docY > docMaxY) docMaxY = docY;
}

static void drawLayoutBoxes() {
  for (int i = 0; i < layoutBoxCount; ++i) {
    const LayoutBox &box = layoutBoxes[i];
    int sy = TEXT_TOP + box.y - scrollY;
    int top = max(TEXT_TOP, sy);
    int bottom = min(TEXT_BOTTOM + 1, sy + box.h);
    if (bottom <= top || box.x >= TEXT_RIGHT || box.x + box.w <= TEXT_LEFT) continue;
    int left = max(TEXT_LEFT, box.x);
    int right = min(TEXT_RIGHT, box.x + box.w);
    if (box.hasBg) videodisplay.fillRect(left, top, right - left, bottom - top, box.bg);
    if (box.hasBorder && box.borderW > 0) {
      for (int b = 0; b < box.borderW; ++b) {
        int bx = box.x + b;
        int by = sy + b;
        int bw = box.w - b * 2;
        int bh = box.h - b * 2;
        if (bw <= 0 || bh <= 0) break;
        if (by >= TEXT_TOP && by <= TEXT_BOTTOM) videodisplay.line(bx, by, bx + bw - 1, by, box.border);
        int lower = by + bh - 1;
        if (lower >= TEXT_TOP && lower <= TEXT_BOTTOM) videodisplay.line(bx, lower, bx + bw - 1, lower, box.border);
        if (bx >= TEXT_LEFT && bx <= TEXT_RIGHT) videodisplay.line(bx, max(TEXT_TOP, by), bx, min(TEXT_BOTTOM, lower), box.border);
        int rx = bx + bw - 1;
        if (rx >= TEXT_LEFT && rx <= TEXT_RIGHT) videodisplay.line(rx, max(TEXT_TOP, by), rx, min(TEXT_BOTTOM, lower), box.border);
      }
    }
  }
}

static bool yVisible(int y, int h) {
  int screenY = TEXT_TOP + y - scrollY;
  return screenY + h >= TEXT_TOP && screenY <= TEXT_BOTTOM;
}

static void addLinkHit(int x, int y, int w, int h, const char *href, HitKind kind = HIT_LINK) {
  if (layoutOnly || !href || !href[0]) return;
  if (linkHitCount >= MAX_LINKS) return;
  int sy = TEXT_TOP + y - scrollY;
  if (sy + h < TEXT_TOP || sy > TEXT_BOTTOM) return;
  linkHits[linkHitCount].x = x;
  linkHits[linkHitCount].y = sy;
  linkHits[linkHitCount].w = w;
  linkHits[linkHitCount].h = h;
  linkHits[linkHitCount].kind = kind;
  copyString(linkHits[linkHitCount].href, sizeof(linkHits[linkHitCount].href), String(href));
  linkHitCount++;
}

static void drawTextFragment(const String &text) {
  if (!text.length()) return;
  int w = text.length() * style.fontW;
  int screenY = TEXT_TOP + docY - scrollY;
  if (!layoutOnly && screenY + style.lineH >= TEXT_TOP && screenY <= TEXT_BOTTOM) {
    uint32_t bg = style.textBg;
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
    if (style.link) addLinkHit(docX, docY, w, style.lineH, style.href, HIT_LINK);
  }
  docX += w;
  lineHeightUsed = max(lineHeightUsed, (int)style.lineH);
  if (docY + style.lineH > docMaxY) docMaxY = docY + style.lineH;
}

static void emitWord(String word) {
  if (!word.length()) return;
  int maxW = currentLineRight() - currentLineLeft();
  int spaceW = docX > currentLineLeft() ? style.fontW : 0;
  int wordW = word.length() * style.fontW;

  if (wordW > maxW) {
    for (int i = 0; i < word.length(); ++i) {
      String one = word.substring(i, i + 1);
      if (docX + style.fontW > currentLineRight()) newLine();
      drawTextFragment(one);
    }
    return;
  }

  if (docX + spaceW + wordW > currentLineRight()) newLine();
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
  if (!chunk.length() || skipText || hiddenDepth > 0) return;
  if (controlCapture) {
    if (controlCaptureText.length() < 96) {
      int room = 96 - controlCaptureText.length();
      if (controlCaptureTag == "select" && controlCaptureText.length() &&
          !isspace((unsigned char)controlCaptureText[controlCaptureText.length() - 1])) {
        controlCaptureText += ' ';
        room--;
      }
      controlCaptureText += chunk.substring(0, room);
    }
    return;
  }

  if (!style.pre && docX == currentLineLeft() && style.textAlign != 0) {
    int visualChars = 0;
    bool pendingSpace = false;
    for (int i = 0; i < chunk.length(); ++i) {
      if (isspace((unsigned char)chunk[i])) {
        if (visualChars > 0) pendingSpace = true;
      } else {
        if (pendingSpace) visualChars++;
        visualChars++;
        pendingSpace = false;
      }
    }
    int textW = min(currentLineRight() - currentLineLeft(), visualChars * style.fontW);
    if (style.textAlign == 1) docX += max(0, (currentLineRight() - currentLineLeft() - textW) / 2);
    else if (style.textAlign == 2) docX += max(0, currentLineRight() - currentLineLeft() - textW);
  }
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

enum ImageFormat : uint8_t {
  IMAGE_NONE,
  IMAGE_BMP,
  IMAGE_PNG,
  IMAGE_JPEG
};

static uint32_t readBE32(const uint8_t *buf, int off) {
  return ((uint32_t)buf[off] << 24) |
         ((uint32_t)buf[off + 1] << 16) |
         ((uint32_t)buf[off + 2] << 8) |
         (uint32_t)buf[off + 3];
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
  if (bw <= 0 || bw > MAX_IMAGE_SOURCE_WIDTH || bh == 0 || (bpp != 24 && bpp != 32)) return false;
  w = bw;
  h = bh > 0 ? bh : -bh;
  topDown = bh < 0;
  pixelOffset = readLE32(header, 10);
  rowStride = ((uint32_t)bpp * (uint32_t)w + 31) / 32 * 4;
  return true;
}

static bool pngInfo(const String &path, int &w, int &h) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t header[24];
  int got = f.read(header, sizeof(header));
  f.close();
  const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  if (got != (int)sizeof(header) || memcmp(header, signature, sizeof(signature)) != 0) return false;
  w = (int)readBE32(header, 16);
  h = (int)readBE32(header, 20);
  return w > 0 && w <= MAX_IMAGE_SOURCE_WIDTH && h > 0 && h <= 4096;
}

static bool jpegInfo(const String &path, int &w, int &h) {
  File f = SD.open(path, FILE_READ);
  if (!f || f.read() != 0xff || f.read() != 0xd8) {
    if (f) f.close();
    return false;
  }

  while (f.available()) {
    int prefix = f.read();
    if (prefix != 0xff) continue;
    int marker = f.read();
    while (marker == 0xff && f.available()) marker = f.read();
    if (marker == 0xd8 || marker == 0x01) continue;
    if (marker == 0xd9 || marker == 0xda) break;
    int hi = f.read();
    int lo = f.read();
    if (hi < 0 || lo < 0) break;
    int length = (hi << 8) | lo;
    if (length < 2) break;
    bool sof = (marker >= 0xc0 && marker <= 0xc3) ||
               (marker >= 0xc5 && marker <= 0xc7) ||
               (marker >= 0xc9 && marker <= 0xcb) ||
               (marker >= 0xcd && marker <= 0xcf);
    if (sof && length >= 7) {
      f.read();
      int hHi = f.read();
      int hLo = f.read();
      int wHi = f.read();
      int wLo = f.read();
      f.close();
      h = (hHi << 8) | hLo;
      w = (wHi << 8) | wLo;
      return w > 0 && w <= 8192 && h > 0 && h <= 8192;
    }
    f.seek(f.position() + length - 2);
  }
  f.close();
  return false;
}

static uint8_t imageInfo(const String &path, int &w, int &h) {
  w = 0;
  h = 0;
  if (isPngName(path) && pngInfo(path, w, h)) return IMAGE_PNG;
  if (isJpegName(path) && jpegInfo(path, w, h)) return IMAGE_JPEG;
  if (isBmpName(path)) {
    uint16_t bpp = 0;
    uint32_t offset = 0, stride = 0;
    bool topDown = false;
    if (bmpInfo(path, w, h, bpp, offset, stride, topDown)) return IMAGE_BMP;
  }
  return IMAGE_NONE;
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

static void *imageFileOpen(const char *filename, int32_t *size) {
  imageDecodeFile = SD.open(filename, FILE_READ);
  if (!imageDecodeFile) {
    *size = 0;
    return nullptr;
  }
  *size = imageDecodeFile.size();
  return &imageDecodeFile;
}

static void imageFileClose(void *handle) {
  (void)handle;
  if (imageDecodeFile) imageDecodeFile.close();
}

static int32_t imagePngRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  (void)handle;
  return imageDecodeFile ? imageDecodeFile.read(buffer, length) : 0;
}

static int32_t imagePngSeek(PNGFILE *handle, int32_t position) {
  (void)handle;
  return imageDecodeFile && imageDecodeFile.seek(position);
}

static int32_t imageJpegRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  (void)handle;
  return imageDecodeFile ? imageDecodeFile.read(buffer, length) : 0;
}

static int32_t imageJpegSeek(JPEGFILE *handle, int32_t position) {
  (void)handle;
  return imageDecodeFile && imageDecodeFile.seek(position);
}

static uint32_t color565ToDisplay(uint16_t pixel) {
  uint8_t r = ((pixel >> 11) & 0x1f) * 255 / 31;
  uint8_t g = ((pixel >> 5) & 0x3f) * 255 / 63;
  uint8_t b = (pixel & 0x1f) * 255 / 31;
  return videodisplay.RGB(r, g, b);
}

static int imagePngDraw(PNGDRAW *draw) {
  if (!activePngDecoder || !imageDrawContext.line565 || draw->iWidth > MAX_IMAGE_SOURCE_WIDTH) return 0;
  activePngDecoder->getLineAsRGB565(draw, imageDrawContext.line565, PNG_RGB565_LITTLE_ENDIAN,
                                    imageDrawContext.background & 0x00ffffff);
  int ty0 = draw->y * imageDrawContext.targetH / imageDrawContext.sourceH;
  int ty1 = ((draw->y + 1) * imageDrawContext.targetH / imageDrawContext.sourceH) - 1;
  if (ty1 < ty0) return 1;
  for (int ty = ty0; ty <= ty1; ++ty) {
    int py = imageDrawContext.screenY + ty;
    if (py < TEXT_TOP || py > TEXT_BOTTOM) continue;
    for (int tx = 0; tx < imageDrawContext.targetW; ++tx) {
      int sx = tx * imageDrawContext.sourceW / imageDrawContext.targetW;
      if (sx >= draw->iWidth) sx = draw->iWidth - 1;
      videodisplay.dotFast(imageDrawContext.x + tx, py, color565ToDisplay(imageDrawContext.line565[sx]));
    }
  }
  if ((draw->y & 0x0f) == 0) yield();
  return 1;
}

static int imageJpegDraw(JPEGDRAW *draw) {
  if (!draw || !draw->pPixels || imageDrawContext.decodedW <= 0 || imageDrawContext.decodedH <= 0) return 0;
  for (int sy = 0; sy < draw->iHeight; ++sy) {
    int sourceY = draw->y + sy;
    int ty0 = sourceY * imageDrawContext.targetH / imageDrawContext.decodedH;
    int ty1 = ((sourceY + 1) * imageDrawContext.targetH / imageDrawContext.decodedH) - 1;
    if (ty1 < ty0) continue;
    for (int sx = 0; sx < draw->iWidth; ++sx) {
      int sourceX = draw->x + sx;
      int tx0 = sourceX * imageDrawContext.targetW / imageDrawContext.decodedW;
      int tx1 = ((sourceX + 1) * imageDrawContext.targetW / imageDrawContext.decodedW) - 1;
      if (tx1 < tx0) continue;
      uint32_t color = color565ToDisplay(draw->pPixels[sy * draw->iWidth + sx]);
      for (int ty = ty0; ty <= ty1; ++ty) {
        int py = imageDrawContext.screenY + ty;
        if (py < TEXT_TOP || py > TEXT_BOTTOM) continue;
        for (int tx = tx0; tx <= tx1; ++tx) {
          if (tx >= 0 && tx < imageDrawContext.targetW) videodisplay.dotFast(imageDrawContext.x + tx, py, color);
        }
      }
    }
  }
  yield();
  return 1;
}

static bool drawPngScaled(const String &path, int x, int screenY, int targetW, int targetH, int srcW, int srcH, uint32_t background) {
  uint16_t *line = (uint16_t *)malloc(srcW * sizeof(uint16_t));
  if (!line) return false;
  PNG *decoder = new (std::nothrow) PNG();
  if (!decoder) {
    free(line);
    return false;
  }

  imageDrawContext = {x, screenY, targetW, targetH, srcW, srcH, srcW, srcH, background, line};
  activePngDecoder = decoder;
  int rc = decoder->open(path.c_str(), imageFileOpen, imageFileClose, imagePngRead, imagePngSeek, imagePngDraw);
  bool ok = rc == PNG_SUCCESS;
  if (ok) ok = decoder->decode(nullptr, PNG_FAST_PALETTE) == PNG_SUCCESS;
  decoder->close();
  activePngDecoder = nullptr;
  delete decoder;
  free(line);
  return ok;
}

static bool drawJpegScaled(const String &path, int x, int screenY, int targetW, int targetH, int srcW, int srcH, uint32_t background) {
  JPEGDEC *decoder = new (std::nothrow) JPEGDEC();
  if (!decoder) return false;
  bool opened = decoder->open(path.c_str(), imageFileOpen, imageFileClose, imageJpegRead, imageJpegSeek, imageJpegDraw);
  if (!opened) {
    delete decoder;
    return false;
  }

  int factor = 1;
  int option = 0;
  while (factor < 8 && ((srcW / factor) > targetW * 2 || (srcH / factor) > targetH * 2)) factor *= 2;
  if (factor == 2) option = JPEG_SCALE_HALF;
  else if (factor == 4) option = JPEG_SCALE_QUARTER;
  else if (factor == 8) option = JPEG_SCALE_EIGHTH;
  imageDrawContext = {x, screenY, targetW, targetH, srcW, srcH,
                      max(1, (srcW + factor - 1) / factor), max(1, (srcH + factor - 1) / factor),
                      background, nullptr};
  decoder->setPixelType(RGB565_LITTLE_ENDIAN);
  bool ok = decoder->decode(0, 0, option);
  decoder->close();
  delete decoder;
  return ok;
}

static void renderImageTag(const String &raw) {
  String src = resolveRelativePath(getAttr(raw, "src"), currentPagePath);
  String altRaw = getAttr(raw, "alt");
  if (!src.length()) return;
  if (docX != currentLineLeft()) newLine(2);

  int srcW = 0, srcH = 0;
  uint8_t format = imageInfo(src, srcW, srcH);
  bool ok = format != IMAGE_NONE;
  int available = max(1, currentLineRight() - currentLineLeft());
  int boxW = available;
  int boxH = 28;

  if (ok) {
    int attrW = parseCssLength(getAttr(raw, "width"), available, -1);
    int attrH = parseCssLength(getAttr(raw, "height"), MAX_IMAGE_HEIGHT, -1);
    int wantedW = style.requestedW > 0 ? style.requestedW : attrW;
    int wantedH = style.requestedH > 0 ? style.requestedH : attrH;
    if (wantedW > 0 && wantedH > 0) {
      boxW = wantedW;
      boxH = wantedH;
    } else if (wantedW > 0) {
      boxW = wantedW;
      boxH = max(1, (int)((long long)srcH * boxW / srcW));
    } else if (wantedH > 0) {
      boxH = wantedH;
      boxW = max(1, (int)((long long)srcW * boxH / srcH));
    } else {
      float scale = min(1.0f, (float)available / (float)srcW);
      boxW = max(1, (int)(srcW * scale));
      boxH = max(1, (int)(srcH * scale));
    }
    if (boxW > available) {
      boxH = max(1, (int)((long long)boxH * available / boxW));
      boxW = available;
    }
    if (boxH > MAX_IMAGE_HEIGHT) {
      boxW = max(1, (int)((long long)boxW * MAX_IMAGE_HEIGHT / boxH));
      boxH = MAX_IMAGE_HEIGHT;
    }
  }

  int imageX = currentLineLeft();
  if (boxW < available && style.textAlign == 1) imageX += (available - boxW) / 2;
  else if (boxW < available && style.textAlign == 2) imageX += available - boxW;

  int screenY = TEXT_TOP + docY - scrollY;
  if (!layoutOnly && screenY + boxH >= TEXT_TOP && screenY <= TEXT_BOTTOM) {
    if (ok) {
      bool drawn = false;
      if (format == IMAGE_BMP) {
        uint16_t bpp = 0;
        uint32_t pixelOffset = 0, rowStride = 0;
        bool topDown = false;
        if (bmpInfo(src, srcW, srcH, bpp, pixelOffset, rowStride, topDown)) {
          drawBmpScaled(src, imageX, docY, boxW, boxH, srcW, srcH, bpp, pixelOffset, rowStride, topDown);
          drawn = true;
        }
      } else if (format == IMAGE_PNG) {
        drawn = drawPngScaled(src, imageX, screenY, boxW, boxH, srcW, srcH, style.textBg);
      } else if (format == IMAGE_JPEG) {
        drawn = drawJpegScaled(src, imageX, screenY, boxW, boxH, srcW, srcH, style.textBg);
      }
      if (drawn) videodisplay.rect(imageX, screenY, boxW, boxH, style.hasBorder ? style.border : COL_DIM);
      else ok = false;
    }
    if (!ok) {
      videodisplay.fillRect(imageX, screenY, boxW, boxH, COL_SURFACE);
      videodisplay.rect(imageX, screenY, boxW, boxH, COL_MID);
      videodisplay.setFont(Font6x8);
      videodisplay.setTextColor(COL_WARN, COL_SURFACE);
      videodisplay.setCursor(imageX + 4, screenY + 8);
      String alt = altRaw.length() ? altRaw : "image";
      if (alt.length() > 45) alt = alt.substring(0, 42) + "...";
      videodisplay.print(alt.c_str());
    }
    if (style.link) addLinkHit(imageX, docY, boxW, boxH, style.href, HIT_LINK);
  }

  docY += boxH + 5;
  docX = currentLineLeft();
  if (docY > docMaxY) docMaxY = docY;
}

static String cleanControlText(String text) {
  String out = "";
  String entity = "";
  bool inEntity = false;
  bool pendingSpace = false;
  for (int i = 0; i < text.length(); ++i) {
    char c = text[i];
    if (inEntity) {
      if (c == ';') {
        String decoded = decodeEntity(entity);
        for (int j = 0; j < decoded.length(); ++j) {
          if (isspace((unsigned char)decoded[j])) pendingSpace = out.length() > 0;
          else {
            if (pendingSpace) out += ' ';
            out += decoded[j];
            pendingSpace = false;
          }
        }
        entity = "";
        inEntity = false;
      } else if (entity.length() < 12) entity += c;
      continue;
    }
    if (c == '&') {
      inEntity = true;
      entity = "";
    } else if (isspace((unsigned char)c)) {
      pendingSpace = out.length() > 0;
    } else {
      if (pendingSpace) out += ' ';
      out += c;
      pendingSpace = false;
    }
    if (out.length() >= 72) break;
  }
  out.trim();
  return out;
}

static String controlHref(const String &raw) {
  String href = getAttr(raw, "href");
  if (!href.length()) href = getAttr(raw, "data-href");
  if (!href.length()) href = getAttr(raw, "formaction");
  if (!href.length()) {
    String onclick = getAttr(raw, "onclick");
    int marker = onclick.indexOf("location");
    if (marker >= 0) {
      int firstSingle = onclick.indexOf('\'', marker);
      int firstDouble = onclick.indexOf('"', marker);
      int first = firstSingle < 0 ? firstDouble : (firstDouble < 0 ? firstSingle : min(firstSingle, firstDouble));
      if (first >= 0) {
        char quote = onclick[first];
        int end = onclick.indexOf(quote, first + 1);
        if (end > first) href = onclick.substring(first + 1, end);
      }
    }
  }
  if (!href.length() && style.link && style.href[0]) return String(style.href);
  return resolveRelativePath(href, currentPagePath);
}

static void renderButtonControl(const String &raw, String label, bool selectStyle = false) {
  label = cleanControlText(label);
  if (!label.length()) label = getAttr(raw, "value");
  if (!label.length()) label = selectStyle ? "Select" : "Button";
  if (label.length() > 42) label = label.substring(0, 39) + "...";

  int available = max(1, currentLineRight() - currentLineLeft());
  int requested = parseCssLength(getAttr(raw, "width"), available, -1);
  int w = style.requestedW > 0 ? style.requestedW : requested;
  if (w <= 0) w = label.length() * 6 + (selectStyle ? 24 : 16);
  w = constrain(w, min(34, available), available);
  int h = style.requestedH > 0 ? style.requestedH : 18;
  h = constrain(h, 14, 36);
  if (docX + w > currentLineRight() && docX != currentLineLeft()) newLine(2);

  int x = docX;
  int sy = TEXT_TOP + docY - scrollY;
  uint32_t bg = style.hasBg ? style.bg : (selectStyle ? COL_SURFACE : COL_ACCENT_DARK);
  uint32_t border = style.hasBorder ? style.border : (selectStyle ? COL_MID : COL_ACCENT);
  uint32_t fg = style.fg;
  if (!layoutOnly && sy + h >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.fillRect(x, sy, w, h, bg);
    videodisplay.rect(x, sy, w, h, border);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(fg, bg);
    int textW = label.length() * 6;
    videodisplay.setCursor(x + max(4, (w - textW) / 2) - (selectStyle ? 4 : 0), sy + (h - 8) / 2);
    videodisplay.print(label.c_str());
    if (selectStyle) {
      videodisplay.setCursor(x + w - 12, sy + (h - 8) / 2);
      videodisplay.print("v");
    }
  }

  String href = controlHref(raw);
  if (href.length()) addLinkHit(x, docY, w, h, href.c_str(), HIT_BUTTON);
  docX += w + 4;
  lineHeightUsed = max(lineHeightUsed, h);
  docMaxY = max(docMaxY, docY + h);
}

static void renderTextAreaControl(const String &raw, String value) {
  value = cleanControlText(value);
  int available = max(1, currentLineRight() - currentLineLeft());
  int requested = parseCssLength(getAttr(raw, "width"), available, -1);
  int w = style.requestedW > 0 ? style.requestedW : (requested > 0 ? requested : min(180, available));
  w = constrain(w, 42, available);
  int rows = constrain(getAttr(raw, "rows").toInt(), 1, 6);
  int h = style.requestedH > 0 ? style.requestedH : max(22, rows * 10 + 8);
  if (docX != currentLineLeft()) newLine(2);
  int x = currentLineLeft();
  int sy = TEXT_TOP + docY - scrollY;
  uint32_t bg = style.hasBg ? style.bg : COL_SURFACE;
  if (!layoutOnly && sy + h >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.fillRect(x, sy, w, h, bg);
    videodisplay.rect(x, sy, w, h, style.hasBorder ? style.border : COL_MID);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(style.fg, bg);
    videodisplay.setCursor(x + 5, sy + 5);
    if (value.length() > (w - 10) / 6) value = value.substring(0, max(0, (w - 16) / 6)) + "...";
    videodisplay.print(value.c_str());
  }
  docY += h + 4;
  docX = currentLineLeft();
  docMaxY = max(docMaxY, docY);
}

static void renderInputTag(const String &raw) {
  String type = getAttr(raw, "type");
  type.toLowerCase();
  if (!type.length()) type = "text";
  if (type == "hidden") return;
  if (type == "button" || type == "submit" || type == "reset") {
    renderButtonControl(raw, getAttr(raw, "value"));
    return;
  }

  int available = max(1, currentLineRight() - currentLineLeft());
  if (type == "checkbox" || type == "radio") {
    int size = 12;
    if (docX + size > currentLineRight() && docX != currentLineLeft()) newLine();
    int sy = TEXT_TOP + docY - scrollY;
    uint32_t bg = style.hasBg ? style.bg : COL_SURFACE;
    if (!layoutOnly && sy + size >= TEXT_TOP && sy <= TEXT_BOTTOM) {
      videodisplay.fillRect(docX, sy, size, size, bg);
      if (type == "radio") videodisplay.circle(docX + 5, sy + 5, 5, style.hasBorder ? style.border : COL_MID);
      else videodisplay.rect(docX, sy, size, size, style.hasBorder ? style.border : COL_MID);
      if (hasAttr(raw, "checked")) {
        if (type == "radio") videodisplay.fillCircle(docX + 5, sy + 5, 2, COL_ACCENT);
        else {
          videodisplay.line(docX + 2, sy + 6, docX + 5, sy + 9, COL_ACCENT);
          videodisplay.line(docX + 5, sy + 9, docX + 10, sy + 2, COL_ACCENT);
        }
      }
    }
    docX += size + 5;
    lineHeightUsed = max(lineHeightUsed, size);
    docMaxY = max(docMaxY, docY + size);
    return;
  }

  int requested = parseCssLength(getAttr(raw, "width"), available, -1);
  int w = style.requestedW > 0 ? style.requestedW : (requested > 0 ? requested : min(150, available));
  int h = style.requestedH > 0 ? style.requestedH : 18;
  w = constrain(w, min(44, available), available);
  h = constrain(h, 14, 28);
  if (docX + w > currentLineRight() && docX != currentLineLeft()) newLine(2);
  int x = docX;
  int sy = TEXT_TOP + docY - scrollY;
  uint32_t bg = style.hasBg ? style.bg : COL_SURFACE;

  if (type == "range") {
    int minimum = getAttr(raw, "min").length() ? getAttr(raw, "min").toInt() : 0;
    int maximum = getAttr(raw, "max").length() ? getAttr(raw, "max").toInt() : 100;
    int value = getAttr(raw, "value").length() ? getAttr(raw, "value").toInt() : minimum;
    if (maximum <= minimum) maximum = minimum + 1;
    int knob = x + 4 + (w - 8) * constrain(value, minimum, maximum) / (maximum - minimum);
    if (!layoutOnly && sy + h >= TEXT_TOP && sy <= TEXT_BOTTOM) {
      videodisplay.line(x + 4, sy + h / 2, x + w - 4, sy + h / 2, COL_MID);
      videodisplay.fillRect(x + 4, sy + h / 2 - 1, max(1, knob - x - 4), 3, COL_ACCENT);
      videodisplay.fillCircle(knob, sy + h / 2, 4, COL_WHITE);
    }
  } else if (type == "color") {
    uint32_t color = parseColorValue(getAttr(raw, "value"), COL_ACCENT);
    if (!layoutOnly && sy + h >= TEXT_TOP && sy <= TEXT_BOTTOM) {
      videodisplay.fillRect(x, sy, w, h, color);
      videodisplay.rect(x, sy, w, h, COL_WHITE);
    }
  } else {
    String value = getAttr(raw, "value");
    bool placeholder = false;
    if (!value.length()) {
      value = getAttr(raw, "placeholder");
      placeholder = true;
    }
    if (type == "password" && value.length()) value = String("********").substring(0, min(8, (int)value.length()));
    int maxChars = max(1, (w - 10) / 6);
    if (value.length() > maxChars) value = value.substring(0, maxChars - 1) + ">";
    if (!layoutOnly && sy + h >= TEXT_TOP && sy <= TEXT_BOTTOM) {
      videodisplay.fillRect(x, sy, w, h, bg);
      videodisplay.rect(x, sy, w, h, style.hasBorder ? style.border : COL_MID);
      videodisplay.setFont(Font6x8);
      videodisplay.setTextColor(placeholder ? COL_MID : style.fg, bg);
      videodisplay.setCursor(x + 5, sy + (h - 8) / 2);
      videodisplay.print(value.c_str());
    }
  }
  String href = controlHref(raw);
  if (href.length()) addLinkHit(x, docY, w, h, href.c_str(), HIT_BUTTON);
  docX += w + 4;
  lineHeightUsed = max(lineHeightUsed, h);
  docMaxY = max(docMaxY, docY + h);
}

static void drawHorizontalRule() {
  if (docX != currentLineLeft()) newLine();
  docY += 4;
  int sy = TEXT_TOP + docY - scrollY;
  if (!layoutOnly && sy >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.line(currentLineLeft(), sy, currentLineRight(), sy, COL_DIM);
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
  if (!layoutOnly && sy + style.lineH >= TEXT_TOP && sy <= TEXT_BOTTOM) {
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(style.fg, style.textBg);
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

static bool isVoidTag(const String &name) {
  return name == "br" || name == "hr" || name == "img" || name == "input" ||
         name == "meta" || name == "link" || name == "source" || name == "area" ||
         name == "base" || name == "embed" || name == "param" || name == "wbr";
}

static bool isBlockContainer(const String &name) {
  return name == "p" || name == "div" || name == "section" || name == "article" ||
         name == "main" || name == "aside" || name == "nav" || name == "header" ||
         name == "footer" || name == "form" || name == "fieldset" || name == "figure" ||
         name == "figcaption" || name == "details" || name == "summary" ||
         name == "blockquote" || name == "pre" || name == "dl" || name == "dt" ||
         name == "dd" || name == "table" || name == "tr";
}

static bool isHeadingTag(const String &name) {
  return name == "h1" || name == "h2" || name == "h3" ||
         name == "h4" || name == "h5" || name == "h6";
}

static bool isInlineTag(const String &name) {
  return name == "a" || name == "span" || name == "b" || name == "strong" ||
         name == "i" || name == "em" || name == "u" || name == "s" ||
         name == "del" || name == "ins" || name == "code" || name == "mark" ||
         name == "small" || name == "label" || name == "abbr" || name == "kbd" ||
         name == "sub" || name == "sup" || name == "td" || name == "th";
}

static void handleEndTag(const String &name) {
  if (name == "body") {
    if (docX != currentLineLeft()) newLine();
    docY += style.padding;
    popStyle(name);
  } else if (isHeadingTag(name)) {
    endElementBlock(name, 0);
  } else if (name == "ul") {
    endElementBlock(name, 0);
  } else if (name == "ol") {
    if (orderedDepth > 0) orderedDepth--;
    endElementBlock(name, 0);
  } else if (name == "li") {
    endElementBlock(name, 0);
  } else if (isBlockContainer(name)) {
    endElementBlock(name, 0);
  } else if (isInlineTag(name)) {
    popStyle(name);
  }
}

static void startCapturedControl(const String &name, const String &raw, bool selfClosing) {
  controlCapture = true;
  controlCaptureTag = name;
  controlCaptureRaw = raw;
  controlCaptureText = "";
  if (selfClosing) {
    if (name == "textarea") renderTextAreaControl(raw, getAttr(raw, "placeholder"));
    else renderButtonControl(raw, getAttr(raw, "value"), name == "select");
    controlCapture = false;
    controlCaptureTag = "";
    controlCaptureRaw = "";
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
  if (name == "script" || name == "style" || name == "head" || name == "svg" ||
      name == "canvas" || name == "video" || name == "audio" || name == "iframe") {
    skipText = true;
    skipTagName = name;
    return;
  }
  if (name == "meta" || name == "link" || name == "source" || name == "base" || name == "wbr") return;

  bool known = name == "body" || isBlockContainer(name) || isHeadingTag(name) ||
               name == "ul" || name == "ol" || name == "li" || isInlineTag(name) ||
               name == "img" || name == "input" || name == "button" ||
               name == "select" || name == "textarea" || name == "option";
  if (!known || name == "option") return;

  pushStyle(name.c_str());
  resetElementLocalStyle();

  if (name == "body") {
    String bg = getAttr(raw, "bgcolor");
    if (bg.length()) {
      style.bg = parseColorValue(bg, COL_BLACK);
      style.textBg = style.bg;
      style.hasBg = true;
    }
    String fg = getAttr(raw, "text");
    if (fg.length()) style.fg = parseColorValue(fg, style.fg);
  } else if (name == "a") {
    String href = resolveRelativePath(getAttr(raw, "href"), currentPagePath);
    style.link = true;
    style.underline = true;
    style.fg = COL_LINK;
    copyString(style.href, sizeof(style.href), href);
  } else if (name == "b" || name == "strong" || name == "th" || name == "dt" || name == "summary") {
    style.bold = true;
  } else if (name == "u" || name == "ins") {
    style.underline = true;
  } else if (name == "s" || name == "del") {
    style.fg = COL_MID;
  } else if (name == "small") {
    style.fg = COL_MID;
    setFontMetrics(6, 8, 9);
  } else if (name == "mark") {
    style.bg = COL_WARN;
    style.textBg = COL_WARN;
    style.fg = COL_BLACK;
    style.hasBg = true;
  } else if (name == "code" || name == "kbd") {
    style.bg = COL_SURFACE_ALT;
    style.textBg = style.bg;
    style.hasBg = true;
  } else if (name == "pre") {
    style.pre = true;
    style.bg = COL_PANEL;
    style.textBg = style.bg;
    style.hasBg = true;
    style.border = COL_DIM;
    style.borderW = 1;
    style.hasBorder = true;
    style.padding = 5;
    style.marginTop = 4;
    style.marginBottom = 4;
    setFontMetrics(6, 8, 10);
  } else if (name == "blockquote") {
    style.bg = COL_SURFACE;
    style.textBg = style.bg;
    style.hasBg = true;
    style.border = COL_ACCENT_DARK;
    style.borderW = 1;
    style.hasBorder = true;
    style.padding = 6;
    style.marginTop = 4;
    style.marginBottom = 4;
  } else if (name == "table") {
    style.border = COL_DIM;
    style.borderW = 1;
    style.hasBorder = true;
    style.padding = 4;
    style.marginTop = 2;
    style.marginBottom = 2;
  } else if (name == "fieldset") {
    style.border = COL_ACCENT_DARK;
    style.borderW = 1;
    style.hasBorder = true;
    style.padding = 6;
  } else if (name == "button") {
    style.bg = COL_ACCENT_DARK;
    style.textBg = style.bg;
    style.hasBg = true;
    style.border = COL_ACCENT;
    style.borderW = 1;
    style.hasBorder = true;
    style.bold = true;
  } else if (name == "select" || name == "textarea" || name == "input") {
    style.bg = COL_SURFACE;
    style.textBg = style.bg;
    style.hasBg = true;
    style.border = COL_MID;
    style.borderW = 1;
    style.hasBorder = true;
  } else if (isHeadingTag(name)) {
    style.bold = true;
    style.fg = name == "h1" ? COL_ACCENT : COL_WHITE;
    style.marginTop = 5;
    style.marginBottom = 5;
    if (name == "h1") setFontMetrics(8, 8, 16);
    else if (name == "h2") setFontMetrics(8, 8, 14);
    else setFontMetrics(6, 8, 12);
  } else if (name == "p") {
    style.marginTop = 2;
    style.marginBottom = 4;
  } else if (name == "section" || name == "article" || name == "main" || name == "aside" ||
             name == "nav" || name == "header" || name == "footer" || name == "form" ||
             name == "figure" || name == "details") {
    style.marginTop = 3;
    style.marginBottom = 3;
  } else if (name == "ul" || name == "ol") {
    style.marginTop = 2;
    style.marginBottom = 3;
  } else if (name == "li" || name == "tr") {
    style.marginTop = 1;
    style.marginBottom = 1;
  }

  applyCssForElement(name, raw);
  if (style.hidden) {
    popStyle(name);
    if (!selfClosing && !isVoidTag(name)) hiddenDepth = 1;
    return;
  }

  if (name == "img") {
    renderImageTag(raw);
    popStyle(name);
    return;
  }
  if (name == "input") {
    renderInputTag(raw);
    popStyle(name);
    return;
  }
  if (name == "button" || name == "select" || name == "textarea") {
    startCapturedControl(name, raw, selfClosing);
    return;
  }

  if (name == "body") {
    if (style.hasBg) documentBackground = style.bg;
    style.textBg = documentBackground;
    style.indent += style.padding;
    style.right -= style.padding;
    docY += style.padding;
    docX = currentLineLeft();
  } else if (isHeadingTag(name)) {
    beginElementBlock(0);
  } else if (name == "ul" || name == "ol") {
    beginElementBlock(0);
    style.indent += name == "ol" ? 18 : 14;
    if (name == "ol") {
      if (orderedDepth < 4) orderedCounter[orderedDepth] = 0;
      orderedDepth = min(orderedDepth + 1, 4);
    }
    docX = currentLineLeft();
  } else if (name == "li") {
    beginElementBlock(0);
    beginListItem();
  } else if (name == "tr") {
    beginElementBlock(0);
    tableCellIndex = 0;
  } else if (name == "td" || name == "th") {
    if (tableCellIndex > 0) drawTextFragment("  |  ");
    tableCellIndex++;
  } else if (isBlockContainer(name)) {
    beginElementBlock(0);
    if (name == "dd") {
      style.indent += 12;
      docX = currentLineLeft();
    }
  }

  if (selfClosing) handleEndTag(name);
}

static void finishCapturedControl() {
  String tag = controlCaptureTag;
  String raw = controlCaptureRaw;
  String text = controlCaptureText;
  controlCapture = false;
  controlCaptureTag = "";
  controlCaptureRaw = "";
  controlCaptureText = "";
  if (tag == "textarea") renderTextAreaControl(raw, text.length() ? text : getAttr(raw, "placeholder"));
  else renderButtonControl(raw, text, tag == "select");
  popStyle(tag);
}

static void handleTag(String raw) {
  raw.trim();
  if (!raw.length()) return;
  if (raw.startsWith("!--") || raw.startsWith("!doctype") || raw.startsWith("!DOCTYPE") || raw[0] == '?') return;
  bool endTag = raw[0] == '/';
  bool selfClosing = raw.endsWith("/");
  String name = tagNameFromRaw(raw);
  if (!name.length()) return;

  if (controlCapture) {
    if (endTag && name == controlCaptureTag) finishCapturedControl();
    return;
  }
  if (skipText) {
    if (endTag && name == skipTagName) {
      skipText = false;
      skipTagName = "";
    }
    return;
  }
  if (hiddenDepth > 0) {
    if (endTag) hiddenDepth--;
    else if (!selfClosing && !isVoidTag(name)) hiddenDepth++;
    return;
  }
  if (endTag) handleEndTag(name);
  else handleStartTag(raw, name, selfClosing || isVoidTag(name));
}

static void resetRenderer() {
  if (layoutOnly) {
    layoutBoxCount = 0;
  } else {
    linkHitCount = 0;
    layoutBoxCursor = 0;
  }
  styleDepth = 0;
  resetStyle();
  docX = currentLineLeft();
  docY = 0;
  docMaxY = 0;
  lineHeightUsed = 0;
  orderedDepth = 0;
  tableCellIndex = 0;
  for (int i = 0; i < 4; ++i) orderedCounter[i] = 0;
  skipText = false;
  skipTagName = "";
  hiddenDepth = 0;
  controlCapture = false;
  controlCaptureTag = "";
  controlCaptureRaw = "";
  controlCaptureText = "";
}

static bool renderHtmlPass(bool measurement) {
  File html = SD.open(currentPagePath, FILE_READ);
  if (!html) return false;

  layoutOnly = measurement;
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
  if (controlCapture) finishCapturedControl();
  html.close();
  docMaxY = max(docMaxY, docY + max((int)style.lineH, lineHeightUsed));
  return true;
}

static void drawPage() {
  AppCursorDrawGuard cursorGuard;
  drawChrome(pageTitle.length() ? pageTitle : leafName(currentPagePath));
  bool ok = true;
  if (!layoutValid) {
    documentBackground = COL_BLACK;
    ok = renderHtmlPass(true);
    if (ok) {
      measuredDocHeight = docMaxY;
      maxScrollY = max(0, measuredDocHeight - (TEXT_BOTTOM - TEXT_TOP) + 8);
      layoutValid = true;
      scrollY = constrain(scrollY, 0, maxScrollY);
    }
  }

  videodisplay.fillRect(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, documentBackground);
  if (ok) {
    drawLayoutBoxes();
    ok = renderHtmlPass(false);
  }
  if (!ok) {
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(COL_WARN, documentBackground);
    videodisplay.setCursor(TEXT_LEFT, TEXT_TOP);
    videodisplay.print("Could not open HTML file.");
    maxScrollY = 0;
  } else if (focusedHit >= linkHitCount) {
    focusedHit = linkHitCount > 0 ? linkHitCount - 1 : -1;
  }
  if (focusedHit >= 0 && focusedHit < linkHitCount) {
    const LinkHit &hit = linkHits[focusedHit];
    videodisplay.rect(hit.x - 1, hit.y - 1, hit.w + 2, hit.h + 2, COL_WHITE);
  }
  String stat = String("Scroll ") + scrollY + "/" + maxScrollY + "  CSS UI  BMP PNG JPG  Scripts off";
  drawStatus(stat);
  videodisplay.show();
}

static void openHtmlFile(const String &path, bool rememberCurrent = false) {
  if (rememberCurrent && currentPagePath.length()) {
    if (pageHistoryDepth >= 6) {
      for (int i = 1; i < 6; ++i) pageHistory[i - 1] = pageHistory[i];
      pageHistoryDepth = 5;
    }
    pageHistory[pageHistoryDepth++] = currentPagePath;
  }
  currentPagePath = normalizePath(path);
  scrollY = 0;
  maxScrollY = 0;
  measuredDocHeight = 0;
  layoutValid = false;
  focusedHit = -1;
  mode = MODE_PAGE;
  loadDocumentMetadata();
  drawPage();
}

static bool navigatePageBack() {
  if (pageHistoryDepth <= 0) return false;
  String previous = pageHistory[--pageHistoryDepth];
  currentPagePath = normalizePath(previous);
  scrollY = 0;
  maxScrollY = 0;
  measuredDocHeight = 0;
  layoutValid = false;
  focusedHit = -1;
  loadDocumentMetadata();
  drawPage();
  return true;
}

static bool tryOpenLink(const String &href) {
  if (!href.length()) return false;
  if (href.startsWith("http://") || href.startsWith("https://")) {
    statusText = "Network URLs are not supported: " + href;
    drawStatus(statusText);
    videodisplay.show();
    return false;
  }
  if (SD.exists(href) && isHtmlName(href)) {
    openHtmlFile(href, true);
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
    pageHistoryDepth = 0;
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
      if (mouse.x >= 6 && mouse.x < 22 && mouse.y >= 4 && mouse.y < 18) {
        if (!navigatePageBack()) {
          mode = MODE_FILES;
          listBrowserDir();
          drawFileBrowser();
        }
        appCursorCaptureAndDraw();
        return true;
      }
      for (int i = 0; i < linkHitCount; ++i) {
        if (mouse.x >= linkHits[i].x && mouse.x < linkHits[i].x + linkHits[i].w &&
            mouse.y >= linkHits[i].y && mouse.y < linkHits[i].y + linkHits[i].h) {
          focusedHit = i;
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

  if (command == "Escape") {
    mode = MODE_FILES;
    listBrowserDir();
    drawFileBrowser();
  } else if (command == "Backspace" || command == "LeftArrow") {
    if (!navigatePageBack()) {
      mode = MODE_FILES;
      listBrowserDir();
      drawFileBrowser();
    }
  } else if (command == "Tab" || command == "RightArrow") {
    if (linkHitCount > 0) {
      focusedHit = (focusedHit + 1) % linkHitCount;
      drawPage();
    }
  } else if (command == "Enter" && focusedHit >= 0 && focusedHit < linkHitCount) {
    tryOpenLink(String(linkHits[focusedHit].href));
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
  Serial.printf("CHtml video init: free=%u dmaLargest=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  bool videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
  Serial.printf("CHtml video %s: free=%u dmaLargest=%u\n",
                videoReady ? "ready" : "failed",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  if (!videoReady) {
    pinMode(25, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.println("VDPROG 100 HTML video allocation failed");
    Serial1.flush();
    tone(SPEAKER_PIN, 0);
    return;
  }
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
