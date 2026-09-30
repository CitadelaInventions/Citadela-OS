//5.95 contrastLadder
//9.52 brightnessLadder

#include "SPIFFS.h"
#include "FS.h"
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include "esp_partition.h"
#include "SD.h"
#include "SPI.h"
#include "Arduino.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include <unistd.h>
#include "../Libraries/CitadelaDisplay.h"
#include "../Libraries/CitadelaSerialCommands.h"
#include "../Libraries/CitadelaBoardPins.h"
#include "../Libraries/CitadelaStorage.h"
#include "../Libraries/CitadelaUI.h"
#include "../Libraries/CitadelaMouseCursor.h"
#include "../Libraries/CitadelaWallpaperRenderer.h"
#include "../Libraries/CitadelaConfig.h"
#include "../Libraries/CitadelaUARTUpload.h"

#ifdef CITADELA_DISPLAY_STOCK_BITLUNI
#error The kernel requires the project PAL4x composite encoder; use Tools/build-kernel-pal4x.sh.
#endif
 
#define CONSOLE_LINES 64
#define CONSOLE_VISIBLE_LINES 10
#define comPin 23
#define MAX_FILES 100
#define MAX_NAME_LENGTH 64
static const char *APP_NAME_CACHE_PATH = "/app_names.cache";
static const char *FILE_NAME_CACHE_PATH = "/file_names.cache";
static const char *WALLPAPER_NAME_CACHE_PATH = "/wallpaper_names.cache";
static const char *APP_CACHE_SCHEMA_PATH = "/app_names.schema";
static const char *APP_CACHE_SCHEMA_VERSION = "bin-only-v3-live-scan";
static const char *WALLPAPER_CACHE_SCHEMA_PATH = "/wallpaper.schema";
static const char *WALLPAPER_CACHE_SCHEMA_VERSION = "wp-index-v4";
static const char *FILE_TREE_CACHE_PATH = "/file_tree.cache";
static const char *FILE_TREE_SCHEMA_PATH = "/file_tree.schema";
static const char *FILE_TREE_SCHEMA_VERSION = "tree-v1";
static const char *FILE_TREE_REFRESH_FLAG_PATH = "/fe_refresh.flag";
static const char *UART_KERNEL_PENDING_PATH = "/uart_kernel_pending.flag";
#define TIMEOUT 1000
#define SPEAKER_PIN 12
#define SCREEN_WIDTH  376
#define SCREEN_HEIGHT 285
#define CLOCK_POS_X 255 + 55
#define CLOCK_POS_Y 272
 
uint64_t cardSize = 0;
uint64_t usedBytes = 0;
uint64_t freeBytes = 0;
uint64_t freeSketch = 0;
 
uint8_t gammaLUT[256];
 
String boolRes = "";
String appName = "";
String appString = "";
String warningMessage = "";
String SDpinStats = "";
String executable = "";
static const int SHELL_HISTORY_COUNT = 8;
static String shellHistory[SHELL_HISTORY_COUNT];
static int shellHistorySize = 0;
static int shellHistoryPosition = -1;
static int shellCursorPosition = 0;
String WALLPAPER_SD_PATH = "/UserData/Wallpapers/Default.bmp";
String consoleBuf[CONSOLE_LINES];
 
static int previousDragValve = -1;
static int dragvalve = 1;
static int homeSelectionIndex = 1;
 
const unsigned long TIME_INTERVAL_MS = 1000;
 
unsigned long lastTimeClockMs = 0;
unsigned long hoverStartMillis = 0;
 
bool tooltipShown = false;
int tooltipIndex = -1;
int tooltipX = 0;
int tooltipY = 0;
int tooltipW = 0;
int tooltipH = 0;
int FileCount = 0;
int heap = 0;
int logLine = 1;
int bmpBrightnessPercent = 100;
int skipLuminanceThreshold = 250;
int wallpaperWhiteAlphaThreshold = 255;
int yieldEveryNRows = 128;
int emBreakAction = 0;
int wallpaperBlackAlphaThreshold = 160;
int blackThreshold = 85;
int whiteThreshold = 255;
int consoleHead = 0;
int consoleCount = 0; 
int consoleScroll = 0;
static int consoleBatchDepth = 0;
int fileCount = 0;
int appFileCount = 0;
static int utilityScrollRow = 0;
 
const int consoleX = 16;
const int consoleY = 162;
const int consoleW = 255+97;
const int consoleH = 100;
const int consoleLineHeight = 8;
const int ICON_BLACK_ALPHA_THRESHOLD_DEFAULT = 100;
const int ICON_WHITE_ALPHA_THRESHOLD_DEFAULT = 256;
int iconBlackAlphaThreshold = ICON_BLACK_ALPHA_THRESHOLD_DEFAULT;
int iconWhiteAlphaThreshold = ICON_WHITE_ALPHA_THRESHOLD_DEFAULT;
const int HOVER_DELAY_MS = 1500;
const int outputPinAudio = 26;
const int speakerPin = 33;
const int WALLPAPER_W = 400;
const int WALLPAPER_H = 285;
const int APP_ICON_MAX_SIZE = 32;
const int FE_MAX_ENTRIES = 24;
const int FE_NAME_MAX = 44;
const int FE_PATH_MAX = 96;
const int FE_SEARCH_MAX = 28;
const int FE_VISIBLE_ROWS = 13;
const int FE_ROW_H = 12;
const int FE_LIST_X = 80;
const int FE_LIST_Y = 47;
const int FE_LIST_W = 180;
const int FE_PREVIEW_X = 266;
const int FE_PREVIEW_Y = 47;
const int FE_PREVIEW_W = 98;
const int FE_PANEL_H = 175;
const int FE_TEXT_CACHE_BYTES_PER_FILE = 8192;
const int FE_TEXT_CACHE_TOTAL_LIMIT = 131072;
const int FE_TREE_CACHE_LIMIT = 1024;
const int WALLPAPER_MAX_ENTRIES = 24;
const int WALLPAPER_NAME_MAX = 44;
const int WALLPAPER_LIST_VISIBLE = 7;
const int WALLPAPER_PREVIEW_CACHE_W = 100;
const int WALLPAPER_PREVIEW_CACHE_H = 70;
const char *WALLPAPER_SD_DIR = "/UserData/Wallpapers";
 
char tooltipText[64] = {0};
char (*FNB)[MAX_NAME_LENGTH] = nullptr;
const char* WALLPAPER_SPIFFS_PATH = "/wallpaper.bmp";
const char* WALLPAPER_RAW_CACHE_PATH = "/wallpaper.raw";
const char* WALLPAPER_PREVIEW_SPIFFS_PATH = "/wallpaper_preview.bmp";
const char* WALLPAPER_APPLY_TEMP_PATH = "/wallpaper.tmp";
const char* WALLPAPER_BACKUP_PATH = "/wallpaper.prev";
const char* WALLPAPER_CACHE_PREFIX = "/wp_cache_";
const char* WALLPAPER_CACHE_EXT = ".bmp";
const size_t WALLPAPER_CACHE_MIN_FREE = 32UL * 1024UL;
const size_t WALLPAPER_STAGING_HEADROOM = 192UL * 1024UL;

enum FileExplorerMode {
    FE_MODE_BROWSE,
    FE_MODE_VIEW
};

struct FileExplorerEntry {
    char name[FE_NAME_MAX];
    char path[FE_PATH_MAX];
    bool isDir;
    bool textCached;
    uint32_t size;
};

// Keep this dormant browser cache out of DMA-capable DRAM so PAL4x always has
// enough contiguous heap for both scanline buffers at display startup.
RTC_NOINIT_ATTR static FileExplorerEntry feEntries[FE_MAX_ENTRIES];
static int feEntryCount = 0;
static int feTotalCount = 0;
static int feWindowStart = 0;
static int feSelected = 0;
static int feScroll = 0;
static int feTreeCachedCount = 0;
static int feTextCachedBytes = 0;
static FileExplorerMode feMode = FE_MODE_BROWSE;
static bool feSearchActive = false;
static char feCurrentPath[FE_PATH_MAX] = "/";
static char feSearchQuery[FE_SEARCH_MAX] = "";
static char feViewerPath[FE_PATH_MAX] = "";
static char feViewerTitle[FE_NAME_MAX] = "";
static uint32_t feViewerSize = 0;
static bool feViewerTextCached = false;
static int feViewerScrollLine = 0;
static int feViewerLineCount = 0;

struct WallpaperListEntry {
    char name[WALLPAPER_NAME_MAX];
    char path[FE_PATH_MAX];
    char cachePath[FE_PATH_MAX];
};

static WallpaperListEntry wallpaperEntries[WALLPAPER_MAX_ENTRIES];
static int wallpaperEntryCount = 0;
static int wallpaperSelected = 0;
static int wallpaperScroll = 0;
static bool wallpaperPickerPreviewReady = false;
static bool wallpaperPickerPreviewQueued = false;
static uint32_t wallpaperPickerPreviewDueMs = 0;
static String wallpaperPickerOriginalPath = "";
static bool wallpaperFetchControllerReady = false;
static bool wallpaperFetchControllerOff = false;
static bool wallpaperFetchControllerFailed = false;
static bool wallpaperFetchInProgress = false;
static bool wallpaperFetchDisplayReinitialized = false;
static bool wallpaperRawCacheSaveEnabled = true;
static int wallpaperFetchCursorX = SCREEN_WIDTH / 2;
static int wallpaperFetchCursorY = SCREEN_HEIGHT / 2;
static int wallpaperFetchCursorButtons = 0;
 
bool fE = false;
bool SDInUse = false;
bool warning = false;
bool initVd = false;
bool util = false;
bool selection = false;
bool startup = true;
bool shouldFlash = false;
bool applaunched = false;
bool shell = false;
bool emBreak = false;
bool bmpMonochrome = false;
bool configurationPending = false;
bool configurationDirty = false;
bool serialDisplayEnabled = false;
static bool serialDisplayConnected = false;
static bool serialDisplayScenePending = false;
bool manualTimePending = false;
bool iconThresholdPending = false;
bool wallpaperPickerPending = false;
bool uartUploadPending = false;
bool uartUploadInProgress = false;
bool bluetoothConnectionPending = false;
bool wifiConnectionPending = false;
bool wallpaperPickerDirty = false;
bool wallpaperPickerColorEnabled = true;
bool wallpaperPickerOriginalMonochrome = false;
bool iconThresholdDirty = false;
bool iconThresholdSliding = false;
bool iconThresholdWhiteMode = false;
bool skipWallpaperBrightPixels = false;
bool skipBrightPixels = false;
bool invertColors = false;
bool mouseCursorOutlined = false;
bool displayPolarityFilterEnabled = true;
bool showLoggerOnScreen = true;
bool WallpaperToggle = true;
bool TooltipsToggle = true;
bool VerboseUART = false;
bool DisplayColour = true;
bool displayColourRenderEnabled = true;
bool WifiCoreToggle = true;
bool TelemetryData = false;
bool FastBoot = false;
bool AudioDriver = true;

static const int CONNECTION_MAX_ENTRIES = 16;
static const int CONNECTION_VISIBLE_ROWS = 8;
static const int CONNECTION_NAME_MAX = 48;
static const int WIFI_PASSWORD_MAX = 63;

enum ConnectionStage {
    CONNECTION_STAGE_LIST,
    CONNECTION_STAGE_PASSWORD,
    CONNECTION_STAGE_CONNECTING
};

struct ConnectionEntry {
    char name[CONNECTION_NAME_MAX];
    int controllerIndex;
    int rssi;
};

static ConnectionEntry connectionEntries[CONNECTION_MAX_ENTRIES];
static int connectionEntryCount = 0;
static int connectionSelected = 0;
static int connectionScroll = 0;
static bool connectionScanning = false;
static uint32_t connectionScanDeadlineMs = 0;
static ConnectionStage connectionStage = CONNECTION_STAGE_LIST;
static String connectionStatus = "";
static String wifiPassword = "";
static int wifiPasswordCursor = 0;
static bool wifiPasswordVisible = false;

static bool bootToneActive = false;
static String uartUploadStatus = "";

static void startBootTone(uint32_t frequency) {
    if (!AudioDriver || frequency == 0) return;
    if (!bootToneActive) {
        bootToneActive = ledcAttach(SPEAKER_PIN, frequency, 10);
    }
    if (bootToneActive) ledcWriteTone(SPEAKER_PIN, frequency);
}

static void stopBootTone() {
    if (!bootToneActive) return;
    ledcWriteTone(SPEAKER_PIN, 0);
    ledcDetach(SPEAKER_PIN);
    bootToneActive = false;
}

static Citadela::VideoProgressSerial bootVideoSerial(Serial1);

static void emitVideoProgressLine(int percent, const char *label) {
    bootVideoSerial.forceProgress(percent, label ? label : "Preparing Citadela");
}

static void bootVideoProgress(int percent, const char *label) {
    if (initVd) return;
    bootVideoSerial.progress(percent, label ? label : "Preparing Citadela");
}

static void bootVideoStart(const char *label) {
    if (initVd) return;
    bootVideoSerial.start(label ? label : "Preparing Citadela");
}

static void bootVideoRelease() {
    bootVideoSerial.releaseKernel();
}

static void fallbackVideoProgress(int percent, const char *label) {
    emitVideoProgressLine(percent, label);
}
bool skipWallpaperDarkPixels = false;
bool gammaLUT_built = false;
int manualTimeField = 0;
int manualYear = 2026;
int manualMonth = 1;
int manualDay = 1;
int manualHour = 0;
int manualMinute = 0;
int manualSecond = 0;
 
const bool redAvailable = true;
 
float whiteBlend = 1.0f;
float blackBlend = 1.0f;
float bmpGamma = 2.4f;

String currentClockTime = "00:00:00";
String currentClockDate = "2000-01-01";
static bool serialDisplayClockDeferred = false;
 
using CitCompositeColorDAC = Citadela::CitCompositeColorDAC;

CitCompositeColorDAC videodisplay(&invertColors, &displayPolarityFilterEnabled, &displayColourRenderEnabled);
static Citadela::SerialDisplay serialDisplay;

static bool handleDisplayMirrorCommand(const String &command);

using CitaCursorType = Citadela::MouseCursor<CitCompositeColorDAC, 81>;
static CitaCursorType CitaCursor;

static void kernelCursorUpdateHover(int x, int y, bool leftDown);
static void kernelCursorActivateClick(int x, int y);

static void kernelCursorRestore() {
    CitaCursor.Restore();
}

static void kernelCursorCaptureAndDraw() {
    if (initVd) CitaCursor.Redraw();
}

static void kernelCursorRefreshAfterRedraw() {
    CitaCursor.RefreshAfterRedraw();
}

class KernelCursorDrawGuard {
  public:
    KernelCursorDrawGuard() : guard(CitaCursor) {}

  private:
    Citadela::CursorDrawGuard<CitaCursorType> guard;
};

static void kernelCursorMoveTo(int x, int y, int buttons) {
    CitaCursor.MoveMouseTo(x, y, buttons);
}

static bool handleKernelMouseReport(String input) {
    return CitaCursor.HandleMouseReport(input);
}

static const uint32_t WALLPAPER_RAW_CACHE_MAGIC = 0x33525743UL; // CWR3
static const size_t WALLPAPER_RAW_CACHE_HEADER_BYTES = 16;
static const bool WALLPAPER_RAW_CACHE_WRITES_ENABLED = false;
static CitCompositeColorDAC::RawPixel wallpaperRawLineScratch[WALLPAPER_W];
static bool wallpaperRawCacheInvalidated = false;
static bool wallpaperArtifactsDirty = true;

static void markWallpaperArtifactsDirty();
static void cleanupWallpaperArtifacts(const char *keepPrimary, const char *keepSecondary = nullptr);
static void cleanupWallpaperArtifactsForRender(const char *renderPath);

enum KernelWindowKind {
    KERNEL_WINDOW_NONE,
    KERNEL_WINDOW_UTILITIES,
    KERNEL_WINDOW_FILES,
    KERNEL_WINDOW_CONFIG,
    KERNEL_WINDOW_TERMINAL,
    KERNEL_WINDOW_CONNECTION
};

static const int KERNEL_WINDOW_TITLE_H = 19;
static void restoreDesktopRect(int x, int y, int w, int h);

struct KernelWindowState {
    KernelWindowKind kind = KERNEL_WINDOW_NONE;
    const char *title = "";
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int outlineX = 0;
    int outlineY = 0;
    uint32_t lastDragDrawMs = 0;
    bool active = false;
    bool dragging = false;
    void (*drawContent)() = nullptr;
};

static KernelWindowState kernelWindow;
RTC_NOINIT_ATTR static CitCompositeColorDAC::RawPixel kernelWindowOutlinePixels[2 * (SCREEN_WIDTH + SCREEN_HEIGHT)];
static int kernelWindowOutlinePixelCount = 0;

static bool openKernelWindow(KernelWindowKind kind,
                             const char *title,
                             int width,
                             int height,
                             void (*drawContent)());

static int kernelWindowClientX() { return kernelWindow.x + 1; }
static int kernelWindowClientY() { return kernelWindow.y + KERNEL_WINDOW_TITLE_H; }
static int kernelWindowClientW() { return max(0, kernelWindow.w - 2); }
static int kernelWindowClientH() { return max(0, kernelWindow.h - KERNEL_WINDOW_TITLE_H - 1); }

static void drawKernelWindowChrome() {
    if (!kernelWindow.active) return;
    uint32_t close = videodisplay.RGB(178, 48, 61);
    uint32_t panel = videodisplay.RGB(18, 25, 30);
    uint32_t title = videodisplay.RGB(28, 67, 92);
    uint32_t edge = videodisplay.RGB(190, 215, 224);
    uint32_t accent = videodisplay.RGB(30, 203, 190);
    uint32_t white = videodisplay.RGB(255, 255, 255);

    videodisplay.fillRect(kernelWindow.x, kernelWindow.y, kernelWindow.w, kernelWindow.h, panel);
    videodisplay.fillRect(kernelWindow.x + 1, kernelWindow.y + 1,
                          kernelWindow.w - 2, KERNEL_WINDOW_TITLE_H - 1, title);
    videodisplay.fillRect(kernelWindow.x + 1, kernelWindow.y + KERNEL_WINDOW_TITLE_H - 1,
                          kernelWindow.w - 2, 1, accent);
    videodisplay.rect(kernelWindow.x, kernelWindow.y, kernelWindow.w, kernelWindow.h, edge);

    int closeX = kernelWindow.x + 5;
    int closeY = kernelWindow.y + 4;
    videodisplay.fillRect(closeX, closeY, 11, 11, close);
    videodisplay.rect(closeX, closeY, 11, 11, white);
    videodisplay.line(closeX + 3, closeY + 3, closeX + 7, closeY + 7, white);
    videodisplay.line(closeX + 7, closeY + 3, closeX + 3, closeY + 7, white);

    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(white, title);
    videodisplay.setCursor(kernelWindow.x + 23, kernelWindow.y + 6);
    videodisplay.print(kernelWindow.title ? kernelWindow.title : "Window");
}

static void redrawKernelWindow() {
    if (!kernelWindow.active) return;
    drawKernelWindowChrome();
    if (kernelWindow.drawContent) kernelWindow.drawContent();
}

static void restoreKernelWindowOutline() {
    if (kernelWindowOutlinePixelCount <= 0) return;
    serialDisplay.command("DRAG 0 0 0 0 0");
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
    int index = 0;
    for (int dx = 0; dx < kernelWindow.w; ++dx) {
        videodisplay.setRawPixelFast(kernelWindow.outlineX + dx, kernelWindow.outlineY,
                                     kernelWindowOutlinePixels[index++]);
    }
    if (kernelWindow.h > 1) {
        for (int dx = 0; dx < kernelWindow.w; ++dx) {
            videodisplay.setRawPixelFast(kernelWindow.outlineX + dx,
                                         kernelWindow.outlineY + kernelWindow.h - 1,
                                         kernelWindowOutlinePixels[index++]);
        }
    }
    for (int dy = 1; dy < kernelWindow.h - 1; ++dy) {
        videodisplay.setRawPixelFast(kernelWindow.outlineX, kernelWindow.outlineY + dy,
                                     kernelWindowOutlinePixels[index++]);
        if (kernelWindow.w > 1) {
            videodisplay.setRawPixelFast(kernelWindow.outlineX + kernelWindow.w - 1,
                                         kernelWindow.outlineY + dy,
                                         kernelWindowOutlinePixels[index++]);
        }
    }
    kernelWindowOutlinePixelCount = 0;
}

static void captureAndDrawKernelWindowOutline(int x, int y) {
    serialDisplay.command("DRAG 1 %d %d %d %d %08lX", x, y, kernelWindow.w, kernelWindow.h,
                          videodisplay.serialColor(videodisplay.RGB(255, 255, 255)));
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
    kernelWindow.outlineX = x;
    kernelWindow.outlineY = y;
    kernelWindowOutlinePixelCount = 0;
    uint32_t outline = videodisplay.RGB(255, 255, 255);
    for (int dx = 0; dx < kernelWindow.w; ++dx) {
        kernelWindowOutlinePixels[kernelWindowOutlinePixelCount++] = videodisplay.getRawPixelFast(x + dx, y);
        videodisplay.dotFast(x + dx, y, outline);
    }
    if (kernelWindow.h > 1) {
        for (int dx = 0; dx < kernelWindow.w; ++dx) {
            int py = y + kernelWindow.h - 1;
            kernelWindowOutlinePixels[kernelWindowOutlinePixelCount++] = videodisplay.getRawPixelFast(x + dx, py);
            videodisplay.dotFast(x + dx, py, outline);
        }
    }
    for (int dy = 1; dy < kernelWindow.h - 1; ++dy) {
        int py = y + dy;
        kernelWindowOutlinePixels[kernelWindowOutlinePixelCount++] = videodisplay.getRawPixelFast(x, py);
        videodisplay.dotFast(x, py, outline);
        if (kernelWindow.w > 1) {
            int px = x + kernelWindow.w - 1;
            kernelWindowOutlinePixels[kernelWindowOutlinePixelCount++] = videodisplay.getRawPixelFast(px, py);
            videodisplay.dotFast(px, py, outline);
        }
    }
}

static bool openKernelWindow(KernelWindowKind kind,
                             const char *title,
                             int width,
                             int height,
                             void (*drawContent)()) {
    kernelCursorRestore();
    if (kernelWindow.active && kernelWindow.kind == kind) {
        kernelWindow.title = title;
        kernelWindow.drawContent = drawContent;
        redrawKernelWindow();
        kernelCursorRefreshAfterRedraw();
        return true;
    }

    if (kernelWindow.active) restoreDesktopRect(kernelWindow.x, kernelWindow.y, kernelWindow.w, kernelWindow.h);

    kernelWindow.kind = kind;
    kernelWindow.title = title;
    kernelWindow.w = min(width, SCREEN_WIDTH);
    kernelWindow.h = min(height, SCREEN_HEIGHT);
    kernelWindow.x = max(0, (SCREEN_WIDTH - kernelWindow.w) / 2);
    kernelWindow.y = max(0, (SCREEN_HEIGHT - kernelWindow.h) / 2);
    kernelWindow.dragging = false;
    kernelWindow.lastDragDrawMs = 0;
    kernelWindow.drawContent = drawContent;
    kernelWindow.active = true;
    redrawKernelWindow();
    kernelCursorRefreshAfterRedraw();
    return true;
}

static bool restoreAndCloseKernelWindow() {
    if (!kernelWindow.active) return true;
    kernelCursorRestore();
    restoreKernelWindowOutline();
    restoreDesktopRect(kernelWindow.x, kernelWindow.y, kernelWindow.w, kernelWindow.h);

    kernelWindow = KernelWindowState();
    util = false;
    fE = false;
    shell = false;
    applaunched = false;
    configurationPending = false;
    uartUploadPending = false;
    uartUploadInProgress = false;
    bluetoothConnectionPending = false;
    wifiConnectionPending = false;
    selection = false;
    dragvalve = homeSelectionIndex;
    previousDragValve = homeSelectionIndex;
    kernelCursorRefreshAfterRedraw();
    return true;
}

static bool kernelWindowCloseButtonAt(int x, int y) {
    return kernelWindow.active &&
           Citadela::UI::pointInRect(x, y, kernelWindow.x + 3, kernelWindow.y + 2, 15, 15);
}

static bool kernelWindowTitleAt(int x, int y) {
    return kernelWindow.active &&
           Citadela::UI::pointInRect(x, y, kernelWindow.x + 19, kernelWindow.y + 1,
                                     kernelWindow.w - 20, KERNEL_WINDOW_TITLE_H - 2);
}

static void beginKernelWindowDrag(int mouseX, int mouseY) {
    if (!kernelWindowTitleAt(mouseX, mouseY)) return;
    kernelWindow.dragOffsetX = mouseX - kernelWindow.x;
    kernelWindow.dragOffsetY = mouseY - kernelWindow.y;
    kernelWindow.lastDragDrawMs = 0;
    KernelCursorDrawGuard cursorGuard;
    restoreDesktopRect(kernelWindow.x, kernelWindow.y, kernelWindow.w, kernelWindow.h);
    kernelWindow.dragging = true;
    captureAndDrawKernelWindowOutline(kernelWindow.x, kernelWindow.y);
}

static void moveKernelWindowToMouse(int mouseX, int mouseY, bool force) {
    if (!kernelWindow.active || !kernelWindow.dragging) return;
    uint32_t now = millis();
    if (!force && kernelWindow.lastDragDrawMs != 0 && now - kernelWindow.lastDragDrawMs < 16) return;

    int newX = constrain(mouseX - kernelWindow.dragOffsetX, 0, SCREEN_WIDTH - kernelWindow.w);
    int newY = constrain(mouseY - kernelWindow.dragOffsetY, 0, SCREEN_HEIGHT - kernelWindow.h);
    if (!force && newX == kernelWindow.outlineX && newY == kernelWindow.outlineY) return;
    KernelCursorDrawGuard cursorGuard;
    restoreKernelWindowOutline();
    if (force) {
        kernelWindow.x = newX;
        kernelWindow.y = newY;
        kernelWindow.dragging = false;
        redrawKernelWindow();
        return;
    }
    captureAndDrawKernelWindowOutline(newX, newY);
    kernelWindow.lastDragDrawMs = now;
}

static void removeWallpaperRawCache() {
    if (SPIFFS.exists(WALLPAPER_RAW_CACHE_PATH)) {
        SPIFFS.remove(WALLPAPER_RAW_CACHE_PATH);
    }
    wallpaperRawCacheInvalidated = false;
}

static bool fileReadExact(File &file, void *data, size_t bytes) {
    return file.read((uint8_t*)data, bytes) == (int)bytes;
}

static size_t fileWriteProgress(File &file, const uint8_t *data, size_t bytes) {
    size_t written = 0;
    int zeroWriteRetries = 0;
    while (written < bytes) {
        size_t count = file.write(data + written, bytes - written);
        if (count > 0) {
            written += count;
            zeroWriteRetries = 0;
        } else if (++zeroWriteRetries >= 3) {
            break;
        } else {
            file.flush();
            delay(1);
        }
    }
    return written;
}

static bool fileWriteExact(File &file, const uint8_t *data, size_t bytes) {
    return fileWriteProgress(file, data, bytes) == bytes;
}

static uint32_t wallpaperRawCacheSignature() {
    uint32_t hash = 2166136261UL;
    auto mixByte = [&](uint8_t value) {
        hash ^= value;
        hash *= 16777619UL;
    };
    auto mixWord = [&](uint32_t value) {
        mixByte((uint8_t)value);
        mixByte((uint8_t)(value >> 8));
        mixByte((uint8_t)(value >> 16));
        mixByte((uint8_t)(value >> 24));
    };

    for (int i = 0; i < WALLPAPER_SD_PATH.length(); ++i) mixByte((uint8_t)WALLPAPER_SD_PATH[i]);
    mixWord((uint32_t)bmpBrightnessPercent);
    mixWord((uint32_t)wallpaperWhiteAlphaThreshold);
    mixWord((uint32_t)wallpaperBlackAlphaThreshold);
    mixWord((uint32_t)whiteThreshold);
    mixWord((uint32_t)blackThreshold);
    mixWord((uint32_t)(whiteBlend * 1000.0f + 0.5f));
    mixWord((uint32_t)(blackBlend * 1000.0f + 0.5f));
    mixWord((uint32_t)(bmpGamma * 1000.0f + 0.5f));
    mixByte(bmpMonochrome ? 1 : 0);
    mixByte(displayColourRenderEnabled ? 1 : 0);
    mixByte(invertColors ? 1 : 0);
    mixByte(displayPolarityFilterEnabled ? 1 : 0);
    mixByte(skipWallpaperBrightPixels ? 1 : 0);
    mixByte(skipWallpaperDarkPixels ? 1 : 0);
    mixWord(videodisplay.calibrationSignature());

    File wallpaper = SPIFFS.open(WALLPAPER_SPIFFS_PATH, FILE_READ);
    if (wallpaper) {
        size_t fileSize = wallpaper.size();
        mixWord((uint32_t)fileSize);
        uint8_t sample[64];
        size_t firstBytes = wallpaper.read(sample, sizeof(sample));
        for (size_t i = 0; i < firstBytes; ++i) mixByte(sample[i]);
        if (fileSize > sizeof(sample) && wallpaper.seek(fileSize - sizeof(sample))) {
            size_t lastBytes = wallpaper.read(sample, sizeof(sample));
            for (size_t i = 0; i < lastBytes; ++i) mixByte(sample[i]);
        }
        wallpaper.close();
    }
    return hash;
}

static bool wallpaperRawCacheHeader(File &file,
                                    uint16_t &w,
                                    uint16_t &h,
                                    uint16_t &pixelSize,
                                    uint32_t &signature) {
    uint32_t magic = 0;
    uint16_t reserved = 0;
    if (!fileReadExact(file, &magic, sizeof(magic))) return false;
    if (!fileReadExact(file, &w, sizeof(w))) return false;
    if (!fileReadExact(file, &h, sizeof(h))) return false;
    if (!fileReadExact(file, &pixelSize, sizeof(pixelSize))) return false;
    if (!fileReadExact(file, &reserved, sizeof(reserved))) return false;
    if (!fileReadExact(file, &signature, sizeof(signature))) return false;
    return magic == WALLPAPER_RAW_CACHE_MAGIC;
}

static bool restoreWallpaperRawCache(int dstW, int dstH) {
    if (!initVd || dstW <= 0 || dstH <= 0 || dstW > WALLPAPER_W) return false;
    cleanupWallpaperArtifactsForRender(WALLPAPER_SPIFFS_PATH);
    if (wallpaperRawCacheInvalidated || !SPIFFS.exists(WALLPAPER_RAW_CACHE_PATH)) return false;

    File cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, FILE_READ);
    if (!cache) return false;

    uint16_t w = 0;
    uint16_t h = 0;
    uint16_t pixelSize = 0;
    uint32_t signature = 0;
    bool valid = wallpaperRawCacheHeader(cache, w, h, pixelSize, signature);
    size_t expectedSize = WALLPAPER_RAW_CACHE_HEADER_BYTES + ((size_t)dstW * (size_t)dstH);
    if (!valid || w != dstW || h != dstH || pixelSize != 1 ||
        signature != wallpaperRawCacheSignature() || cache.size() < expectedSize) {
        cache.close();
        return false;
    }

    uint32_t start = millis();
    uint8_t *signalRow = reinterpret_cast<uint8_t*>(wallpaperRawLineScratch);
    size_t rowBytes = (size_t)dstW;
    for (int y = 0; y < dstH; ++y) {
        if (!fileReadExact(cache, signalRow, rowBytes)) {
            cache.close();
            return false;
        }
        for (int x = 0; x < dstW; ++x) {
            videodisplay.setRawPixelFast(x, y, (CitCompositeColorDAC::RawPixel)signalRow[x] << 8);
        }
    }
    cache.close();
    wallpaperRawCacheInvalidated = false;
    Serial.printf("Wallpaper raw cache restored in %lu ms.\n", (unsigned long)(millis() - start));
    return true;
}

static void saveWallpaperRawCache(int dstW, int dstH) {
    if (!initVd || dstW <= 0 || dstH <= 0 || dstW > WALLPAPER_W) return;
    if (!WALLPAPER_RAW_CACHE_WRITES_ENABLED) {
        removeWallpaperRawCache();
        wallpaperRawCacheInvalidated = true;
        Serial.println("Wallpaper raw cache write skipped; calibrated BMP renderer remains active.");
        return;
    }

    const size_t expectedSize = WALLPAPER_RAW_CACHE_HEADER_BYTES + (size_t)dstW * (size_t)dstH;
    bool reuseBackingFile = false;
    size_t existingSize = 0;
    if (SPIFFS.exists(WALLPAPER_RAW_CACHE_PATH)) {
        File existing = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, FILE_READ);
        existingSize = existing ? existing.size() : 0;
        reuseBackingFile = existingSize >= WALLPAPER_RAW_CACHE_HEADER_BYTES;
        if (existing) existing.close();
    }

    const size_t rawCacheWriteHeadroom = 64UL * 1024UL;
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t reclaimableBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
    reclaimableBytes += existingSize;
    if (reclaimableBytes < expectedSize + rawCacheWriteHeadroom) {
        SPIFFS.remove(WALLPAPER_RAW_CACHE_PATH);
        wallpaperRawCacheInvalidated = true;
        Serial.printf("Wallpaper raw cache skipped: need %u bytes with headroom, have %u.\n",
                      (unsigned)(expectedSize + rawCacheWriteHeadroom),
                      (unsigned)reclaimableBytes);
        return;
    }

    File cache;
    if (reuseBackingFile) cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, "r+");
    if (!cache) {
        reuseBackingFile = false;
        SPIFFS.remove(WALLPAPER_RAW_CACHE_PATH);
        cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, FILE_WRITE);
    }
    if (!cache) {
        Serial.println("Failed to open wallpaper raw cache for write.");
        return;
    }

    uint32_t started = millis();
    uint32_t magic = WALLPAPER_RAW_CACHE_MAGIC;
    uint16_t w = (uint16_t)dstW;
    uint16_t h = (uint16_t)dstH;
    uint16_t pixelSize = 1;
    uint16_t reserved = 0;
    uint32_t invalidSignature = 0;
    auto writeCacheHeader = [&]() {
        return cache && cache.seek(0) &&
               fileWriteExact(cache, (uint8_t*)&magic, sizeof(magic)) &&
               fileWriteExact(cache, (uint8_t*)&w, sizeof(w)) &&
               fileWriteExact(cache, (uint8_t*)&h, sizeof(h)) &&
               fileWriteExact(cache, (uint8_t*)&pixelSize, sizeof(pixelSize)) &&
               fileWriteExact(cache, (uint8_t*)&reserved, sizeof(reserved)) &&
               fileWriteExact(cache, (uint8_t*)&invalidSignature, sizeof(invalidSignature));
    };
    bool ok = writeCacheHeader();

    const size_t preferredChunkBytes = 4096;
    uint8_t *chunk = (uint8_t*)malloc(preferredChunkBytes);
    bool chunkOnHeap = chunk != nullptr;
    size_t chunkCapacity = chunkOnHeap ? preferredChunkBytes : sizeof(wallpaperRawLineScratch);
    if (!chunk) chunk = reinterpret_cast<uint8_t*>(wallpaperRawLineScratch);
    const size_t totalPixels = (size_t)dstW * (size_t)dstH;
    size_t pixelOffset = 0;
    int reopenAttempts = 0;
    bool freshFileRetryUsed = false;
    if (ok) ok = cache.seek(WALLPAPER_RAW_CACHE_HEADER_BYTES);
    while (ok && pixelOffset < totalPixels) {
        size_t count = min(chunkCapacity, totalPixels - pixelOffset);
        for (size_t i = 0; i < count; ++i) {
            size_t flat = pixelOffset + i;
            int x = flat % (size_t)dstW;
            int y = flat / (size_t)dstW;
            chunk[i] = videodisplay.rawPixelSignal(videodisplay.getRawPixelFast(x, y));
        }

        size_t stored = fileWriteProgress(cache, chunk, count);
        pixelOffset += stored;
        if (stored == count) continue;

        cache.flush();
        cache.close();
        if (reuseBackingFile && !freshFileRetryUsed) {
            SPIFFS.remove(WALLPAPER_RAW_CACHE_PATH);
            delay(10);
            cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, FILE_WRITE);
            reuseBackingFile = false;
            freshFileRetryUsed = true;
            pixelOffset = 0;
            reopenAttempts = 0;
            ok = writeCacheHeader() && cache.seek(WALLPAPER_RAW_CACHE_HEADER_BYTES);
            Serial.printf("Wallpaper raw cache switched to a fresh backing file (%s).\n",
                          ok ? "ready" : "failed");
            continue;
        }
        if (++reopenAttempts > 5) {
            ok = false;
            break;
        }
        delay(10);
        cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, "r+");
        ok = cache && cache.seek(WALLPAPER_RAW_CACHE_HEADER_BYTES + pixelOffset);
        Serial.printf("Wallpaper raw cache resumed at %u/%u bytes (attempt %d).\n",
                      (unsigned)pixelOffset,
                      (unsigned)totalPixels,
                      reopenAttempts);
    }
    if (chunkOnHeap) free(chunk);

    uint32_t signature = wallpaperRawCacheSignature();
    if (ok) {
        ok = cache.seek(12) && fileWriteExact(cache, (uint8_t*)&signature, sizeof(signature));
    }
    cache.flush();
    size_t storedSize = cache.size();
    cache.close();
    if (ok && storedSize > expectedSize) {
        if (::truncate("/spiffs/wallpaper.raw", (off_t)expectedSize) == 0) {
            storedSize = expectedSize;
        } else {
            Serial.println("Wallpaper raw cache trim was unavailable; retaining reusable tail space.");
        }
    }
    ok = ok && storedSize >= expectedSize;
    wallpaperRawCacheInvalidated = !ok;
    Serial.printf("Wallpaper raw cache %s in %lu ms (%u bytes, %s backing file).\n",
                  ok ? "saved" : "failed",
                  (unsigned long)(millis() - started),
                  (unsigned)storedSize,
                  reuseBackingFile ? "reused" : "new");
}

static void fallbackVideoPreload(const char *label) {
    bootVideoSerial.prepare(label ? label : "Preparing Citadela", 0);
    delay(350);
}

static void fallbackVideoTakeover(const char *label) {
    if (initVd) {
        videodisplay.i2sStop();
        pinMode(25, INPUT_PULLDOWN);
        delay(5);
    }
    bootVideoSerial.markActive();
    Serial1.println("VDINIT");
    fallbackVideoProgress(0, label ? label : "Preparing Citadela");
    Serial1.flush();
    delay(90);
}

static void fallbackVideoRuntimeStart(const char *label) {
    fallbackVideoPreload(label);
    fallbackVideoTakeover(label);
}

static void restartWithFallbackVideo(const char *label) {
    fallbackVideoRuntimeStart(label);
    fallbackVideoProgress(100, label ? label : "Restarting");
    Serial1.flush();
    delay(90);
    ESP.restart();
}

class DisplayPolarityScope {
  public:
    DisplayPolarityScope(bool enabled) {
        previous = displayPolarityFilterEnabled;
        displayPolarityFilterEnabled = enabled;
    }

    ~DisplayPolarityScope() {
        displayPolarityFilterEnabled = previous;
    }

  private:
    bool previous;
};
 
void releaseFileNameBuffers() {
    if (FNB) {
        free(FNB);
        FNB = nullptr;
    }
}

String basenameNoExt(const String &fn);

void writeCachedName(File &cache, const char *name) {
    if (!cache || !name || !name[0]) return;
    char clipped[MAX_NAME_LENGTH];
    strncpy(clipped, name, MAX_NAME_LENGTH - 1);
    clipped[MAX_NAME_LENGTH - 1] = '\0';
    cache.println(clipped);
}

void writeCachedName(File &cache, const String &name) {
    writeCachedName(cache, name.c_str());
}

static void writeCachedPath(File &cache, const String &path) {
    if (!cache || path.length() == 0) return;
    char clipped[FE_PATH_MAX];
    strncpy(clipped, path.c_str(), FE_PATH_MAX - 1);
    clipped[FE_PATH_MAX - 1] = '\0';
    cache.println(clipped);
}

static bool isBMPName(const String &name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".bmp");
}

static bool appCacheSchemaCurrent() {
    if (!SPIFFS.exists(APP_CACHE_SCHEMA_PATH)) return false;
    File schema = SPIFFS.open(APP_CACHE_SCHEMA_PATH, FILE_READ);
    if (!schema) return false;
    String version = schema.readStringUntil('\n');
    version.trim();
    schema.close();
    return version == APP_CACHE_SCHEMA_VERSION;
}

static void writeAppCacheSchema() {
    SPIFFS.remove(APP_CACHE_SCHEMA_PATH);
    File schema = SPIFFS.open(APP_CACHE_SCHEMA_PATH, FILE_WRITE);
    if (!schema) {
        Serial.println("Failed to write app cache schema marker.");
        return;
    }
    schema.println(APP_CACHE_SCHEMA_VERSION);
    schema.close();
}

static bool wallpaperCacheSchemaCurrent() {
    if (!SPIFFS.exists(WALLPAPER_CACHE_SCHEMA_PATH)) return false;
    File schema = SPIFFS.open(WALLPAPER_CACHE_SCHEMA_PATH, FILE_READ);
    if (!schema) return false;
    String version = schema.readStringUntil('\n');
    version.trim();
    schema.close();
    return version == WALLPAPER_CACHE_SCHEMA_VERSION;
}

static void writeWallpaperCacheSchema() {
    SPIFFS.remove(WALLPAPER_CACHE_SCHEMA_PATH);
    File schema = SPIFFS.open(WALLPAPER_CACHE_SCHEMA_PATH, FILE_WRITE);
    if (!schema) {
        Serial.println("Failed to write wallpaper cache schema marker.");
        return;
    }
    schema.println(WALLPAPER_CACHE_SCHEMA_VERSION);
    schema.close();
}

static bool fileTreeCacheSchemaCurrent() {
    if (!SPIFFS.exists(FILE_TREE_SCHEMA_PATH)) return false;
    File schema = SPIFFS.open(FILE_TREE_SCHEMA_PATH, FILE_READ);
    if (!schema) return false;
    String version = schema.readStringUntil('\n');
    version.trim();
    schema.close();
    return version == FILE_TREE_SCHEMA_VERSION;
}

static void writeFileTreeCacheSchema() {
    SPIFFS.remove(FILE_TREE_SCHEMA_PATH);
    File schema = SPIFFS.open(FILE_TREE_SCHEMA_PATH, FILE_WRITE);
    if (!schema) {
        Serial.println("Failed to write file tree schema marker.");
        return;
    }
    schema.println(FILE_TREE_SCHEMA_VERSION);
    schema.close();
}

static String leafNameFromPath(String path) {
    path.replace('\\', '/');
    while (path.endsWith("/") && path.length() > 1) path.remove(path.length() - 1);
    int slash = path.lastIndexOf('/');
    if (slash >= 0) return path.substring(slash + 1);
    return path;
}

static String appEntryPayload(const String &entry) {
    if (entry.length() > 2 && entry.charAt(1) == '|') return entry.substring(2);
    return entry;
}

static char appEntryType(const String &entry) {
    if (entry.length() > 2 && entry.charAt(1) == '|') return entry.charAt(0);
    String payload = appEntryPayload(entry);
    payload.toLowerCase();
    return payload.endsWith(".bin") ? 'A' : 'F';
}

static bool appEntryIsFirmware(const String &entry) {
    String payload = appEntryPayload(entry);
    payload.toLowerCase();
    return appEntryType(entry) == 'A' && payload.endsWith(".bin");
}

static String appEntryDisplayLabel(const String &entry) {
    String payload = appEntryPayload(entry);
    String leaf = leafNameFromPath(payload);
    char type = appEntryType(entry);
    if (type == 'A') return basenameNoExt(leaf);
    if (type == 'D') return String("/") + leaf;
    return leaf;
}

int countCachedNames(const char *path) {
    if (!SPIFFS.exists(path)) return 0;
    File cache = SPIFFS.open(path, FILE_READ);
    if (!cache) return 0;

    bool appCache = strcmp(path, APP_NAME_CACHE_PATH) == 0;
    int count = 0;
    while (cache.available() && count < MAX_FILES) {
        String name = cache.readStringUntil('\n');
        name.trim();
        if (name.length() == 0) continue;
        if (appCache && !appEntryIsFirmware(name)) continue;
        count++;
    }
    cache.close();
    return count;
}

bool loadFNBufferFromSPIFFS() {
    int cachedFileCount = countCachedNames(FILE_NAME_CACHE_PATH);
    int cachedAppFileCount = countCachedNames(APP_NAME_CACHE_PATH);

    releaseFileNameBuffers();
    FileCount = cachedFileCount;
    appFileCount = cachedAppFileCount;
    if (FileCount <= 0 && appFileCount <= 0) {
        Serial.println("No cached filename buffers in SPIFFS.");
        return true;
    }

    Serial.printf("Loaded cached filename index: apps=%d files=%d\n", appFileCount, FileCount);
    return true;
}

static bool readCachedAppEntry(int index, String &entry) {
    entry = "";
    if (index < 0 || index >= appFileCount || !SPIFFS.exists(APP_NAME_CACHE_PATH)) return false;

    File cache = SPIFFS.open(APP_NAME_CACHE_PATH, FILE_READ);
    if (!cache) return false;

    int current = 0;
    while (cache.available()) {
        String candidate = cache.readStringUntil('\n');
        candidate.trim();
        if (!appEntryIsFirmware(candidate)) continue;
        if (current == index) {
            entry = candidate;
            cache.close();
            return true;
        }
        current++;
    }
    cache.close();
    return false;
}

void buildGammaLUT() {
    float brightnessMul = ((float)bmpBrightnessPercent+10) / 100.0f;
    if (brightnessMul < 0.0f) brightnessMul = 0.0f;
    if (brightnessMul > 4.0f) brightnessMul = 4.0f;
    for (int i = 0; i < 256; ++i) {
        float v = (float)i / 255.0f;
        v *= brightnessMul;
        if (v > 1.0f) v = 1.0f;
        float gval = powf(v, bmpGamma);
        int out = (int)(gval * 255.0f + 0.5f);
        if (out < 0) out = 0;
        if (out > 255) out = 255;
        gammaLUT[i] = (uint8_t)out;
    }
    gammaLUT_built = true;
}
void readSystemConfigFromSPIFFS() {
    if (!SPIFFS.exists("/systemConfiguration.conf")) {
        Serial.println("No systemConfiguration.conf in SPIFFS, using defaults.");
        return;
    }

    bool loaded = Citadela::ConfigFile::read(SPIFFS, "/systemConfiguration.conf", [](const String &key, const String &val) {
        bool v = Citadela::ConfigFile::boolValue(val);

        if (key == "WallpaperToggle") WallpaperToggle = v;
        else if (key == "TooltipsToggle") TooltipsToggle = v;
        else if (key == "VerboseUART") VerboseUART = v;
        else if (key == "DisplayColour") DisplayColour = v;
        else if (key == "WifiCoreToggle") WifiCoreToggle = v;
        else if (key == "TelemetryData") TelemetryData = v;
        else if (key == "FastBoot") FastBoot = v;
        else if (key == "AudioDriver") AudioDriver = v;
        else if (key == "InvertColors" || key == "InvertColours") invertColors = v;
        else if (key == "WallpaperColor" || key == "WallpaperColour") bmpMonochrome = !v;
        else if (key == "WallpaperMonochrome" || key == "WallpaperMono") bmpMonochrome = v;
        else if (key == "WallpaperPath" || key == "WallpaperSDPath") {
            if (val.length() > 0) WALLPAPER_SD_PATH = val;
        }
        else if (key == "MouseCursorOutlined" || key == "CursorOutlineMode") mouseCursorOutlined = v;
        else if (key == "SerialDisplay") serialDisplayEnabled = v;
        else if (key == "IconAlphaThreshold" || key == "IconTransparencyThreshold") {
            iconBlackAlphaThreshold = Citadela::ConfigFile::intValue(val, 0, 255);
        }
        else if (key == "IconBlackAlphaThreshold") {
            iconBlackAlphaThreshold = Citadela::ConfigFile::intValue(val, 0, 255);
        }
        else if (key == "IconWhiteAlphaThreshold") {
            iconWhiteAlphaThreshold = Citadela::ConfigFile::intValue(val, 0, 256);
        }
    });

    if (!loaded) {
        Serial.println("Failed to open /systemConfiguration.conf for read");
        return;
    }

    displayColourRenderEnabled = DisplayColour;
    Serial.println("Loaded systemConfiguration.conf from SPIFFS");
}
 
bool configEnabled(int id) {
    switch (id) {
        case 0: return WallpaperToggle;
        case 1: return TooltipsToggle;
        case 2: return VerboseUART;
        case 3: return DisplayColour;
        case 4: return WifiCoreToggle;
        case 5: return TelemetryData;
        case 6: return FastBoot;
        case 7: return AudioDriver;
        case 8: return invertColors;
        case 12: return mouseCursorOutlined;
        case 15: return serialDisplayEnabled;
        default: return false;
    }
}
 
void setConfigByIndex(int id, bool val) {
    switch (id) {
        case 0: WallpaperToggle = val; break;
        case 1: TooltipsToggle = val; break;
        case 2: VerboseUART = val; break;
        case 3: DisplayColour = val; break;
        case 4: WifiCoreToggle = val; break;
        case 5: TelemetryData = val; break;
        case 6: FastBoot = val; break;
        case 7: AudioDriver = val; break;
        case 8: invertColors = val; break;
        case 12: mouseCursorOutlined = val; break;
        case 15: serialDisplayEnabled = val; break;
    }
    writeSystemConfigToSPIFFS();
}
void toggleConfigByIndex(int id) {
    bool cur = configEnabled(id);
    setConfigByIndex(id, !cur);
}
 
void writeSystemConfigToSPIFFS() {
    Citadela::ConfigWriter writer;
    if (!writer.Begin(SPIFFS, "/systemConfiguration.conf")) {
        Serial.println("Failed to open /systemConfiguration.conf for write");
        return;
    }

    writer.Bool("WallpaperToggle", WallpaperToggle);
    writer.Bool("TooltipsToggle", TooltipsToggle);
    writer.Bool("VerboseUART", VerboseUART);
    writer.Bool("DisplayColour", DisplayColour);
    writer.Bool("WifiCoreToggle", WifiCoreToggle);
    writer.Bool("TelemetryData", TelemetryData);
    writer.Bool("FastBoot", FastBoot);
    writer.Bool("AudioDriver", AudioDriver);
    writer.Bool("InvertColors", invertColors);
    writer.Bool("WallpaperColor", !bmpMonochrome);
    writer.StringValue("WallpaperPath", WALLPAPER_SD_PATH);
    writer.Bool("MouseCursorOutlined", mouseCursorOutlined);
    writer.Bool("SerialDisplay", serialDisplayEnabled);
    writer.Int("IconBlackAlphaThreshold", iconBlackAlphaThreshold);
    writer.Int("IconWhiteAlphaThreshold", iconWhiteAlphaThreshold);
    writer.End();
 
    Serial.println("Wrote /systemConfiguration.conf to SPIFFS");
}
bool copyFileSPIFFSToSD(const char* spPath, const char* sdPath) {
    bool ok = Citadela::Storage::copyFile(SPIFFS, spPath, SD, sdPath, 4096);
    if (!ok) {
        Serial.printf("Failed to copy SPIFFS:%s -> SD:%s\n", spPath, sdPath);
        return false;
    }
    Serial.printf("Copied SPIFFS:%s -> SD:%s\n", spPath, sdPath);
    return true;
}

static bool sdFileMatchesRunningImage(File &file,
                                      const esp_partition_t *partition,
                                      size_t imageLength) {
    if (!file || file.size() != imageLength || imageLength == 0) return false;
    uint8_t flashBytes[128];
    uint8_t fileBytes[128];
    size_t offsets[] = {0, imageLength / 2, imageLength > sizeof(flashBytes) ? imageLength - sizeof(flashBytes) : 0};
    for (size_t offset : offsets) {
        size_t count = min(sizeof(flashBytes), imageLength - offset);
        if (esp_partition_read(partition, offset, flashBytes, count) != ESP_OK ||
            !file.seek(offset) || file.read(fileBytes, count) != (int)count ||
            memcmp(flashBytes, fileBytes, count) != 0) {
            return false;
        }
    }
    return true;
}

static bool syncRunningKernelImageToSD() {
    const char *kernelPath = "/System/kernel.bin";
    const char *temporaryPath = "/System/kernel.new";
    const char *backupPath = "/System/kernel.prev.bin";
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        Serial.println("Kernel SD sync skipped: running partition unavailable.");
        return false;
    }

    esp_partition_pos_t position = {running->address, running->size};
    esp_image_metadata_t metadata = {};
    if (esp_image_get_metadata(&position, &metadata) != ESP_OK ||
        metadata.image_len == 0 || metadata.image_len > running->size) {
        Serial.println("Kernel SD sync skipped: running image metadata invalid.");
        return false;
    }

    File installed = SD.open(kernelPath, FILE_READ);
    bool current = sdFileMatchesRunningImage(installed, running, metadata.image_len);
    if (installed) installed.close();
    if (current) {
        Serial.printf("SD kernel is current (%u bytes).\n", (unsigned)metadata.image_len);
        return true;
    }

    uint8_t *buffer = (uint8_t*)malloc(4096);
    if (!buffer) {
        Serial.println("Kernel SD sync failed: transfer buffer unavailable.");
        return false;
    }
    SD.remove(temporaryPath);
    File output = SD.open(temporaryPath, FILE_WRITE);
    bool ok = output;
    size_t offset = 0;
    while (ok && offset < metadata.image_len) {
        size_t count = min((size_t)4096, (size_t)metadata.image_len - offset);
        ok = esp_partition_read(running, offset, buffer, count) == ESP_OK &&
             output.write(buffer, count) == count;
        offset += ok ? count : 0;
        if ((offset & 0xffff) == 0) yield();
    }
    if (output) {
        output.flush();
        output.close();
    }
    free(buffer);

    File verification = SD.open(temporaryPath, FILE_READ);
    ok = ok && offset == metadata.image_len &&
         sdFileMatchesRunningImage(verification, running, metadata.image_len);
    if (verification) verification.close();
    if (!ok) {
        SD.remove(temporaryPath);
        Serial.printf("Kernel SD sync failed after %u/%u bytes.\n",
                      (unsigned)offset, (unsigned)metadata.image_len);
        return false;
    }

    SD.remove(backupPath);
    bool hadInstalled = SD.exists(kernelPath);
    bool backedUp = !hadInstalled || SD.rename(kernelPath, backupPath);
    if (!backedUp || !SD.rename(temporaryPath, kernelPath)) {
        SD.remove(temporaryPath);
        if (backedUp && hadInstalled && !SD.exists(kernelPath)) SD.rename(backupPath, kernelPath);
        Serial.println("Kernel SD sync failed during atomic rename.");
        return false;
    }
    Serial.printf("SD kernel updated from running image (%u bytes).\n", (unsigned)metadata.image_len);
    return true;
}

static bool preservePendingUARTKernelImage() {
    if (!SPIFFS.exists(UART_KERNEL_PENDING_PATH)) return false;

    const char *kernelPath = "/System/kernel.bin";
    File uploaded = SD.open(kernelPath, FILE_READ);
    uint8_t imageMagic = uploaded ? (uint8_t)uploaded.read() : 0;
    size_t uploadedSize = uploaded ? uploaded.size() : 0;
    if (!uploaded || imageMagic != 0xE9 || uploadedSize < 32768) {
        if (uploaded) uploaded.close();
        SPIFFS.remove(UART_KERNEL_PENDING_PATH);
        Serial.println("Discarded stale UART kernel marker: staged image is missing or invalid.");
        return false;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    bool runningMatches = false;
    if (running) {
        esp_partition_pos_t position = {running->address, running->size};
        esp_image_metadata_t metadata = {};
        if (esp_image_get_metadata(&position, &metadata) == ESP_OK && metadata.image_len > 0) {
            uploaded.seek(0);
            runningMatches = sdFileMatchesRunningImage(uploaded, running, metadata.image_len);
        }
    }
    uploaded.close();

    if (runningMatches) {
        SPIFFS.remove(UART_KERNEL_PENDING_PATH);
        Serial.println("UART kernel package is now running; normal SD synchronization resumed.");
        return false;
    }

    Serial.printf("Preserving pending UART kernel package on SD (%u bytes).\n", (unsigned)uploadedSize);
    return true;
}
 
void invalidateWallpaperRenderCache();

void setBMPBrightness(int percent) {
    bmpBrightnessPercent = constrain(percent, 0, 200);
    invalidateWallpaperRenderCache();
    Serial.printf("BMP brightness set to %d%%\n", bmpBrightnessPercent);
}
void setBMPMonochrome(bool mono) {
    bmpMonochrome = mono;
    invalidateWallpaperRenderCache();
    Serial.printf("BMP monochrome: %s\n", mono ? "ON" : "OFF");
}
void setWhiteThreshold(int t) {
    whiteThreshold = constrain(t, 0, 255);
    invalidateWallpaperRenderCache();
    Serial.printf("White threshold set to %d\n", whiteThreshold);
}
void setWhiteBlend(float b) {
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    whiteBlend = b;
    invalidateWallpaperRenderCache();
    Serial.printf("White blend set to %.2f\n", whiteBlend);
}
void setBlackThreshold(int t) {
    blackThreshold = constrain(t, 0, 255);
    invalidateWallpaperRenderCache();
    Serial.printf("Black threshold set to %d\n", blackThreshold);
}
void setBlackBlend(float b) {
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    blackBlend = b;
    invalidateWallpaperRenderCache();
    Serial.printf("Black blend set to %.2f\n", blackBlend);
}
void setInvertColors(bool inv) {
    invertColors = inv;
    invalidateWallpaperRenderCache();
    Serial.printf("Invert mode: %s\n", inv ? "ON" : "OFF");
}
void setShowLogger(bool on) {
    showLoggerOnScreen = on;
    if (on) {
        Serial.println("On-screen logger: ON");
        if (initVd) {
            logLine = 1;
            videodisplay.fillRect(15,160,205,90,videodisplay.RGB(255,255,255));
        }
    } else {
        Serial.println("On-screen logger: OFF");
        if (initVd) {
            videodisplay.fillRect(15,160,205,90,0);
        }
    }
}
 
static uint32_t readLE32(uint8_t *buf, int ofs) {
    return (uint32_t)buf[ofs] | ((uint32_t)buf[ofs+1] << 8) | ((uint32_t)buf[ofs+2] << 16) | ((uint32_t)buf[ofs+3] << 24);
}

static void putLE16(uint8_t *buf, int ofs, uint16_t value) {
    buf[ofs] = value & 0xFF;
    buf[ofs + 1] = (value >> 8) & 0xFF;
}

static void putLE32(uint8_t *buf, int ofs, uint32_t value) {
    buf[ofs] = value & 0xFF;
    buf[ofs + 1] = (value >> 8) & 0xFF;
    buf[ofs + 2] = (value >> 16) & 0xFF;
    buf[ofs + 3] = (value >> 24) & 0xFF;
}

static bool wallpaperToneLUTValid = false;
static int wallpaperToneBrightnessCache = -1;
static bool wallpaperToneInvertCache = false;
static uint8_t wallpaperToneLUT[256];
static uint8_t wallpaperRowScratch[(WALLPAPER_W * 3) + 4];
static uint16_t wallpaperMapXScratch[WALLPAPER_W];

static bool wallpaperBMPInfoValid = false;
static char wallpaperBMPPathCache[64] = {0};
static uint32_t wallpaperBMPPixelOffset = 0;
static int wallpaperBMPSrcW = 0;
static int wallpaperBMPSrcH = 0;
static bool wallpaperBMPTopDown = false;
static int wallpaperBMPRowStride = 0;

static uint16_t *wallpaperMapX = nullptr;
static bool wallpaperMapXHeapAllocated = false;
static int wallpaperMapSrcW = -1;
static int wallpaperMapDstW = -1;

static inline int clampIntFast(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int floatToQ256(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 256;
    return (int)(v * 256.0f + 0.5f);
}

static inline int blendQ256(int from, int to, int weight) {
    return ((from * (256 - weight)) + (to * weight) + 128) >> 8;
}

static inline int wallpaperLuminanceFast(int r, int g, int b) {
    return ((299 * r) + (587 * g) + (114 * b)) / 1000;
}

void invalidateWallpaperRenderCache() {
    serialDisplay.invalidateAssets();
    wallpaperRawCacheInvalidated = true;
    wallpaperToneLUTValid = false;
    wallpaperBMPInfoValid = false;
    wallpaperBMPPathCache[0] = '\0';
    wallpaperMapSrcW = -1;
    wallpaperMapDstW = -1;
    if (wallpaperMapX && wallpaperMapXHeapAllocated) {
        free(wallpaperMapX);
    }
    wallpaperMapX = nullptr;
    wallpaperMapXHeapAllocated = false;
}

static void ensureWallpaperToneLUT() {
    int brightness = clampIntFast(bmpBrightnessPercent, 0, 200);
    if (wallpaperToneLUTValid &&
        wallpaperToneBrightnessCache == brightness &&
        wallpaperToneInvertCache == invertColors) {
        return;
    }

    for (int i = 0; i < 256; ++i) {
        int v = (i * brightness + 50) / 100;
        if (v > 255) v = 255;
        if (invertColors) v = 255 - v;
        wallpaperToneLUT[i] = (uint8_t)v;
    }

    wallpaperToneBrightnessCache = brightness;
    wallpaperToneInvertCache = invertColors;
    wallpaperToneLUTValid = true;
}

static bool loadWallpaperBMPInfo(File &bmp, const char* spiffsPath, bool verbose) {
    if (wallpaperBMPInfoValid && strncmp(wallpaperBMPPathCache, spiffsPath, sizeof(wallpaperBMPPathCache)) == 0) {
        if (verbose) {
            Serial.printf("Wallpaper BMP cached: %dx%d (topDown=%d), offset=%u\n",
                          wallpaperBMPSrcW, wallpaperBMPSrcH, (int)wallpaperBMPTopDown,
                          (unsigned)wallpaperBMPPixelOffset);
        }
        return true;
    }

    uint8_t header[54];
    if (!bmp.seek(0) || (int)bmp.read(header, 54) != 54) {
        Serial.println("BMP header read failed.");
        return false;
    }
    if (header[0] != 'B' || header[1] != 'M') {
        Serial.println("Not a BMP file.");
        return false;
    }

    uint32_t pixelDataOffset = readLE32(header, 10);
    int32_t srcHRaw = (int32_t)readLE32(header, 22);
    int32_t srcW = (int32_t)readLE32(header, 18);
    uint16_t bpp = header[28] | (header[29] << 8);
    if (bpp != 24 || srcW <= 0 || srcHRaw == 0) {
        Serial.println("Unsupported or invalid BMP (must be 24bpp with nonzero height).");
        return false;
    }

    bool topDown = false;
    int srcH = srcHRaw;
    if (srcHRaw < 0) {
        topDown = true;
        srcH = -srcHRaw;
    }

    int srcRowBytes = srcW * 3;
    int srcRowStride = srcRowBytes + ((4 - (srcRowBytes % 4)) % 4);

    wallpaperBMPPixelOffset = pixelDataOffset;
    wallpaperBMPSrcW = srcW;
    wallpaperBMPSrcH = srcH;
    wallpaperBMPTopDown = topDown;
    wallpaperBMPRowStride = srcRowStride;
    strncpy(wallpaperBMPPathCache, spiffsPath, sizeof(wallpaperBMPPathCache) - 1);
    wallpaperBMPPathCache[sizeof(wallpaperBMPPathCache) - 1] = '\0';
    wallpaperBMPInfoValid = true;

    if (verbose) {
        Serial.printf("Wallpaper BMP: %dx%d (topDown=%d), offset=%u, bpp=%u\n",
                      wallpaperBMPSrcW, wallpaperBMPSrcH, (int)wallpaperBMPTopDown,
                      (unsigned)wallpaperBMPPixelOffset, (unsigned)bpp);
    }
    return true;
}

static uint8_t* acquireWallpaperRowBuffer(size_t bytes, bool *heapAllocated) {
    if (heapAllocated) *heapAllocated = false;
    if (bytes <= sizeof(wallpaperRowScratch)) return wallpaperRowScratch;

#ifdef ESP32
    uint8_t *buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
#else
    uint8_t *buf = (uint8_t*)malloc(bytes);
#endif
    if (buf && heapAllocated) *heapAllocated = true;
    return buf;
}

static void releaseWallpaperRowBuffer(uint8_t *buf, bool heapAllocated) {
    if (buf && heapAllocated) free(buf);
}

static bool ensureWallpaperMapX(int srcW, int dstW) {
    if (wallpaperMapX && wallpaperMapSrcW == srcW && wallpaperMapDstW == dstW) return true;

    bool newMapHeapAllocated = false;
    uint16_t *newMap = nullptr;
    if (dstW <= WALLPAPER_W) {
        newMap = wallpaperMapXScratch;
    } else {
        newMap = (uint16_t*)malloc(sizeof(uint16_t) * dstW);
        newMapHeapAllocated = (newMap != nullptr);
    }
    if (!newMap) return false;

    for (int dx = 0; dx < dstW; ++dx) {
        int sx = (int)((uint32_t)dx * (uint32_t)srcW / (uint32_t)dstW);
        if (sx < 0) sx = 0;
        if (sx >= srcW) sx = srcW - 1;
        newMap[dx] = (uint16_t)sx;
    }

    if (wallpaperMapX && wallpaperMapXHeapAllocated && wallpaperMapX != newMap) free(wallpaperMapX);
    wallpaperMapX = newMap;
    wallpaperMapXHeapAllocated = newMapHeapAllocated;
    wallpaperMapSrcW = srcW;
    wallpaperMapDstW = dstW;
    return true;
}

static inline uint32_t wallpaperColorFromBGR(uint8_t b, uint8_t g, uint8_t r,
                                             bool *drawPixel,
                                             int wpWhiteThresh,
                                             int wpBlackThresh,
                                             bool skipWPWhite,
                                             bool skipWPBlack,
                                             int whiteBlendQ,
                                             int blackBlendQ) {
    int rA = wallpaperToneLUT[r];
    int gA = wallpaperToneLUT[g];
    int bA = wallpaperToneLUT[b];
    int lum = wallpaperLuminanceFast(rA, gA, bA);

    if ((skipWPWhite && lum >= wpWhiteThresh) || (skipWPBlack && lum <= wpBlackThresh)) {
        *drawPixel = false;
        return 0;
    }

    if (whiteBlendQ > 0 && lum >= whiteThreshold) {
        int sum = rA + gA + bA + 1;
        rA = blendQ256(rA, (lum * rA) / sum, whiteBlendQ);
        gA = blendQ256(gA, (lum * gA) / sum, whiteBlendQ);
        bA = blendQ256(bA, (lum * bA) / sum, whiteBlendQ);
    }
    if (blackBlendQ > 0 && lum <= blackThreshold) {
        rA = blendQ256(rA, 0, blackBlendQ);
        gA = blendQ256(gA, 0, blackBlendQ);
        bA = blendQ256(bA, 0, blackBlendQ);
    }

    if (bmpMonochrome) {
        bA = lum;
        gA = lum;
        rA = redAvailable ? lum : 0;
    } else {
        gA = bA;
        if (!redAvailable) rA = 0;
    }

    *drawPixel = true;
    return videodisplay.RGB(rA, gA, bA);
}
 
static void mirrorBMP(const char *path, int mode, int outX, int outY, int outW, int outH,
                      int clipX, int clipY, int clipW, int clipH, bool skipWhite, bool skipBlack) {
    if (!serialDisplay.enabled()) return;
    uint32_t id = serialDisplay.asset(SPIFFS, path);
    if (!id) return;
    if (mode == 2) {
        if (!gammaLUT_built) buildGammaLUT();
        serialDisplay.table(2, gammaLUT);
    } else {
        ensureWallpaperToneLUT();
        serialDisplay.table(1, wallpaperToneLUT);
    }
    serialDisplay.command("BMP %08lX %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
        (unsigned long)id, mode, outX, outY, outW, outH, clipX, clipY, clipW, clipH,
        bmpBrightnessPercent, bmpMonochrome, redAvailable, skipWhite, skipBlack,
        whiteThreshold, blackThreshold, floatToQ256(whiteBlend), floatToQ256(blackBlend),
        mode ? iconWhiteAlphaThreshold : wallpaperWhiteAlphaThreshold,
        mode ? iconBlackAlphaThreshold : wallpaperBlackAlphaThreshold, displayColourRenderEnabled);
}

uint32_t fnv1a_checksum_of_file(File &f) {
    return Citadela::Storage::fnv1a(f);
}
 
uint32_t fnv1a_checksum_sd(String sdPath) {
    File f = SD.open(sdPath, FILE_READ);
    if (!f) return 0;
    uint32_t h = fnv1a_checksum_of_file(f);
    f.close();
    return h;
}
uint32_t fnv1a_checksum_spiffs(String spPath) {
    File f = SPIFFS.open(spPath, FILE_READ);
    if (!f) return 0;
    uint32_t h = fnv1a_checksum_of_file(f);
    f.close();
    return h;
}
bool filesMatchSDvsSPIFFS(String sdPath, String spPath) {
    return Citadela::Storage::filesMatch(SD, sdPath.c_str(), SPIFFS, spPath.c_str());
}
bool copyFileSDToSPIFFS(String sdPath, String spiffsPath) {
    bool isWallpaperTarget = (spiffsPath == WALLPAPER_SPIFFS_PATH);
    if (isWallpaperTarget) invalidateWallpaperRenderCache();

    bool ok = Citadela::Storage::copyFile(SD, sdPath.c_str(), SPIFFS, spiffsPath.c_str(), 4096);
    if (!ok) {
        Serial.printf("Failed to copy SD:%s -> SPIFFS:%s\n", sdPath.c_str(), spiffsPath.c_str());
        return false;
    }

    if (isWallpaperTarget) invalidateWallpaperRenderCache();
    Serial.printf("Copied SD:%s -> SPIFFS:%s\n", sdPath.c_str(), spiffsPath.c_str());
    return true;
}
 
static inline int luminance_int(int r, int g, int b) {
    return ( (299 * r) + (587 * g) + (114 * b) ) / 1000;
}
 
void setWallpaperWhiteAlphaThreshold(int t) {
    wallpaperWhiteAlphaThreshold = constrain(t, 0, 255);
    invalidateWallpaperRenderCache();
    Serial.printf("Wallpaper white-alpha threshold set to %d\n", wallpaperWhiteAlphaThreshold);
}
void setWallpaperWhiteAlphaMode(const String &arg) {
    String a = arg;
    a.toLowerCase();
    if (a == "on" || a == "1" || a == "true") {
        skipWallpaperBrightPixels = true;
        invalidateWallpaperRenderCache();
        Serial.println("Wallpaper white-alpha mode: ON");
    } else if (a == "off" || a == "0" || a == "false") {
        skipWallpaperBrightPixels = false;
        invalidateWallpaperRenderCache();
        Serial.println("Wallpaper white-alpha mode: OFF");
    } else if (a == "toggle") {
        skipWallpaperBrightPixels = !skipWallpaperBrightPixels;
        invalidateWallpaperRenderCache();
        Serial.printf("Wallpaper white-alpha mode toggled: %s\n", skipWallpaperBrightPixels ? "ON" : "OFF");
    } else {
        int v = a.toInt();
        if (v >= 0 && v <= 255) setWallpaperWhiteAlphaThreshold(v);
        else Serial.println("Invalid argument for wallpaper white-alpha. Use on/off/toggle or 0-255.");
    }
}
 
void setWallpaperBlackAlphaThreshold(int t) {
    wallpaperBlackAlphaThreshold = constrain(t, 0, 255);
    invalidateWallpaperRenderCache();
    Serial.printf("Wallpaper black-alpha threshold set to %d\n", wallpaperBlackAlphaThreshold);
}
void setWallpaperBlackAlphaMode(const String &arg) {
    String a = arg;
    a.toLowerCase();
    if (a == "on" || a == "1" || a == "true") {
        skipWallpaperDarkPixels = true;
        invalidateWallpaperRenderCache();
        Serial.println("Wallpaper black-alpha mode: ON");
    } else if (a == "off" || a == "0" || a == "false") {
        skipWallpaperDarkPixels = false;
        invalidateWallpaperRenderCache();
        Serial.println("Wallpaper black-alpha mode: OFF");
    } else if (a == "toggle") {
        skipWallpaperDarkPixels = !skipWallpaperDarkPixels;
        invalidateWallpaperRenderCache();
        Serial.printf("Wallpaper black-alpha mode toggled: %s\n", skipWallpaperDarkPixels ? "ON" : "OFF");
    } else {
        int v = a.toInt();
        if (v >= 0 && v <= 255) setWallpaperBlackAlphaThreshold(v);
        else Serial.println("Invalid argument for wallpaper black-alpha. Use on/off/toggle or 0-255.");
    }
}
static inline bool isSkipLum(int lum, int whiteThresh, bool skipWhite, int blackThresh, bool skipBlack) {
    if (skipWhite && lum >= whiteThresh) return true;
    if (skipBlack && lum <= blackThresh) return true;
    return false;
}
 
void renderBMPRegionFromSPIFFS(const char* spiffsPath, int dstX, int dstY, int regionW, int regionH, int dstW /*= WALLPAPER_W*/, int dstH /*= WALLPAPER_H*/) {
    DisplayPolarityScope rawBitmapDraw(false);
    if (dstW <= 0 || dstH <= 0 || regionW <= 0 || regionH <= 0) return;
    cleanupWallpaperArtifactsForRender(spiffsPath);
    if (!SPIFFS.exists(spiffsPath)) {
        Serial.printf("No BMP in SPIFFS: %s\n", spiffsPath);
        return;
    }
    mirrorBMP(spiffsPath, 0, 0, 0, dstW, dstH, dstX, dstY, regionW, regionH,
              skipWallpaperBrightPixels, skipWallpaperDarkPixels);
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
    File bmp = SPIFFS.open(spiffsPath, FILE_READ);
    if (!bmp) { Serial.println("Failed to open wallpaper in SPIFFS."); return; }

    if (!loadWallpaperBMPInfo(bmp, spiffsPath, false)) { bmp.close(); return; }
    uint32_t pixelDataOffset = wallpaperBMPPixelOffset;
    int srcW = wallpaperBMPSrcW;
    int srcH = wallpaperBMPSrcH;
    bool topDown = wallpaperBMPTopDown;
    int srcRowStride = wallpaperBMPRowStride;
    if (dstX < 0) { regionW += dstX; dstX = 0; }
    if (dstY < 0) { regionH += dstY; dstY = 0; }
    if (dstX + regionW > dstW) regionW = dstW - dstX;
    if (dstY + regionH > dstH) regionH = dstH - dstY;
    if (regionW <= 0 || regionH <= 0) { bmp.close(); return; }

    if (!ensureWallpaperMapX(srcW, dstW)) {
        Serial.println("Out of memory for wallpaper X map.");
        bmp.close();
        return;
    }

    ensureWallpaperToneLUT();
    const int wpWhiteThresh = wallpaperWhiteAlphaThreshold;
    const int wpBlackThresh = wallpaperBlackAlphaThreshold;
    const bool skipWPWhite = skipWallpaperBrightPixels;
    const bool skipWPBlack = skipWallpaperDarkPixels;
    const int whiteBlendQ = floatToQ256(whiteBlend);
    const int blackBlendQ = floatToQ256(blackBlend);
    const int regionBottom = dstY + regionH - 1;
    const int srcYFirst = clampIntFast(((long)dstY * srcH) / dstH - 1, 0, srcH - 1);
    const int srcYLast = clampIntFast(((((long)(regionBottom + 1) * srcH) + dstH - 1) / dstH) + 1, 0, srcH - 1);
    const int fileRowStart = topDown ? srcYFirst : (srcH - 1 - srcYLast);
    const int fileRowEnd = topDown ? srcYLast : (srcH - 1 - srcYFirst);
    const uint32_t firstRowOffset = pixelDataOffset + ((uint32_t)fileRowStart * (uint32_t)srcRowStride);
    bool rowBufHeapAllocated = false;
    uint8_t *rowBuf = acquireWallpaperRowBuffer((size_t)srcRowStride, &rowBufHeapAllocated);
    if (!rowBuf) { Serial.println("Out of memory for region rowBuf."); bmp.close(); return; }

    uint32_t regionStartMs = millis();
    int rowsRead = 0;

    if (!bmp.seek(firstRowOffset)) {
        Serial.println("Failed to seek to wallpaper region row.");
        releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
        bmp.close();
        return;
    }

    for (int fileRow = fileRowStart; fileRow <= fileRowEnd; ++fileRow) {
        int got = bmp.read(rowBuf, srcRowStride);
        if (got < srcRowStride) {
            int clearFrom = got > 0 ? got : 0;
            memset(rowBuf + clearFrom, 0, srcRowStride - clearFrom);
        }
        ++rowsRead;

        int srcY = topDown ? fileRow : (srcH - 1 - fileRow);
        long dyStart = (long)srcY * dstH / srcH;
        long dyEnd = (((long)(srcY + 1) * dstH) - 1) / srcH;
        if (dyStart < 0) dyStart = 0;
        if (dyEnd >= dstH) dyEnd = dstH - 1;
        if (dyStart > dyEnd) continue;
        int overlapTop = (int)max((long)dstY, dyStart);
        int overlapBottom = (int)min((long)regionBottom, dyEnd);
        if (overlapTop > overlapBottom) continue;

        int dstHeight = overlapBottom - overlapTop + 1;

        if (dstHeight == 1) {
            for (int dx = 0; dx < regionW; ++dx) {
                int sx = wallpaperMapX[dstX + dx];
                int i = sx * 3;
                bool drawPixel = true;
                uint32_t color32 = wallpaperColorFromBGR(rowBuf[i + 0], rowBuf[i + 1], rowBuf[i + 2],
                                                         &drawPixel,
                                                         wpWhiteThresh, wpBlackThresh,
                                                         skipWPWhite, skipWPBlack,
                                                         whiteBlendQ, blackBlendQ);
                if (drawPixel) {
                    videodisplay.dotFast(dstX + dx, overlapTop, color32);
                }
            }
            if (yieldEveryNRows > 0 && ((srcY & (yieldEveryNRows - 1)) == 0)) yield();
            continue;
        }

        int runStart = -1;
        uint32_t runColor = 0;
        int runLen = 0;
 
        for (int dx = 0; dx < regionW; ++dx) {
            int sx = wallpaperMapX[dstX + dx];
            int i = sx * 3;
            bool drawPixel = true;
            uint32_t color32 = wallpaperColorFromBGR(rowBuf[i + 0], rowBuf[i + 1], rowBuf[i + 2],
                                                     &drawPixel,
                                                     wpWhiteThresh, wpBlackThresh,
                                                     skipWPWhite, skipWPBlack,
                                                     whiteBlendQ, blackBlendQ);
            if (!drawPixel) {
                if (runStart != -1 && runLen > 0) {
                    videodisplay.fillRect(dstX + runStart, overlapTop, runLen, dstHeight, runColor);
                    runStart = -1; runLen = 0;
                }
                continue;
            }
 
            if (runStart == -1) {
                runStart = dx; runColor = color32; runLen = 1;
            } else {
                if (color32 == runColor) {
                    runLen++;
                } else {
                    videodisplay.fillRect(dstX + runStart, overlapTop, runLen, dstHeight, runColor);
                    runStart = dx; runColor = color32; runLen = 1;
                }
            }
        }
        if (runStart != -1 && runLen > 0) {
            videodisplay.fillRect(dstX + runStart, overlapTop, runLen, dstHeight, runColor);
        }
 
        if (yieldEveryNRows > 0 && ((srcY & (yieldEveryNRows - 1)) == 0)) yield();
    }
    releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
    bmp.close();

    uint32_t regionElapsedMs = millis() - regionStartMs;
    if (regionElapsedMs > 50) {
        Serial.printf("Wallpaper region %dx%d @ %d,%d rendered in %lu ms (%d rows).\n",
                      regionW, regionH, dstX, dstY,
                      (unsigned long)regionElapsedMs,
                      rowsRead);
    }
}
 
void renderBMPDotByDotFromSPIFFS(const char* spiffsPath, int dstW /*= WALLPAPER_W*/, int dstH /*= WALLPAPER_H*/) {
    DisplayPolarityScope rawBitmapDraw(false);
    if (!WallpaperToggle) return;
    if (FastBoot) return;
    if (dstW <= 0 || dstH <= 0) return;
    cleanupWallpaperArtifactsForRender(spiffsPath);
    if (!SPIFFS.exists(spiffsPath)) {
        Serial.printf("No BMP in SPIFFS: %s\n", spiffsPath);
        return;
    }
 
    mirrorBMP(spiffsPath, 0, 0, 0, dstW, dstH, 0, 0, dstW, dstH,
              skipWallpaperBrightPixels, skipWallpaperDarkPixels);
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
    uint32_t renderStartMs = millis();
    Serial.printf("FreeHeap before open: %u\n", (unsigned)ESP.getFreeHeap());
 
    File bmp = SPIFFS.open(spiffsPath, FILE_READ);
    if (!bmp) { Serial.println("Failed to open wallpaper in SPIFFS."); return; }
 
    if (!loadWallpaperBMPInfo(bmp, spiffsPath, true)) { bmp.close(); return; }
    uint32_t pixelDataOffset = wallpaperBMPPixelOffset;
    int srcW = wallpaperBMPSrcW;
    int srcH = wallpaperBMPSrcH;
    bool topDown = wallpaperBMPTopDown;
    int srcRowStride = wallpaperBMPRowStride;
 
    bool rowBufHeapAllocated = false;
    uint8_t *rowBuf = acquireWallpaperRowBuffer((size_t)srcRowStride, &rowBufHeapAllocated);
 
    if (!rowBuf) {
        Serial.println("Out of memory for rowBuf.");
        bmp.close();
        return;
    }
    Serial.printf("Using %s wallpaper rowBuf: %d bytes\n",
                  rowBufHeapAllocated ? "heap" : "static",
                  srcRowStride);
 
    Serial.printf("FreeHeap after rowBuf alloc: %u\n", (unsigned)ESP.getFreeHeap());
 
    if (!ensureWallpaperMapX(srcW, dstW)) {
        Serial.println("Out of memory for wallpaper X map.");
        releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
        bmp.close();
        return;
    }

    ensureWallpaperToneLUT();
    const int wpWhiteThresh = wallpaperWhiteAlphaThreshold;
    const int wpBlackThresh = wallpaperBlackAlphaThreshold;
    const bool skipWPWhite = skipWallpaperBrightPixels;
    const bool skipWPBlack = skipWallpaperDarkPixels;
    const int whiteBlendQ = floatToQ256(whiteBlend);
    const int blackBlendQ = floatToQ256(blackBlend);
 
    if (!bmp.seek(pixelDataOffset)) {
        Serial.println("Failed to seek to pixel data.");
        releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
        bmp.close();
        return;
    }
 
    // Main loop: read each source row, compute corresponding dst Y range, and render columns.
    for (int fileRow = 0; fileRow < srcH; ++fileRow) {
        int got = bmp.read(rowBuf, srcRowStride);
        if (got < srcRowStride) {
            int clearFrom = got > 0 ? got : 0;
            memset(rowBuf + clearFrom, 0, srcRowStride - clearFrom);
        }
 
        int srcY = topDown ? fileRow : (srcH - 1 - fileRow);
 
        long dyStart = (long)srcY * dstH / srcH;
        long dyEnd = (((long)(srcY + 1) * dstH) - 1) / srcH;
        if (dyStart < 0) dyStart = 0;
        if (dyEnd >= dstH) dyEnd = dstH - 1;
        if (dyStart > dyEnd) continue;
 
        int dstHeight = (int)(dyEnd - dyStart + 1);

        if (dstHeight == 1) {
            for (int dx = 0; dx < dstW; ++dx) {
                int sx = wallpaperMapX[dx];
                int i = sx * 3;
                bool drawPixel = true;
                uint32_t color32 = wallpaperColorFromBGR(rowBuf[i + 0], rowBuf[i + 1], rowBuf[i + 2],
                                                         &drawPixel,
                                                         wpWhiteThresh, wpBlackThresh,
                                                         skipWPWhite, skipWPBlack,
                                                         whiteBlendQ, blackBlendQ);
                if (drawPixel) {
                    videodisplay.dotFast(dx, (int)dyStart, color32);
                }
            }
            if (yieldEveryNRows > 0 && ((fileRow & (yieldEveryNRows - 1)) == 0)) yield();
            continue;
        }
 
        int runStart = -1;
        uint32_t runColor = 0;
        int runLen = 0;
 
        // For each destination column compute source X (sx) on the fly — no mapX allocation.
        for (int dx = 0; dx < dstW; ++dx) {
            int sx = wallpaperMapX[dx];
            int i = sx * 3;
            bool drawPixel = true;
            uint32_t color32 = wallpaperColorFromBGR(rowBuf[i + 0], rowBuf[i + 1], rowBuf[i + 2],
                                                     &drawPixel,
                                                     wpWhiteThresh, wpBlackThresh,
                                                     skipWPWhite, skipWPBlack,
                                                     whiteBlendQ, blackBlendQ);

            if (!drawPixel) {
                if (runStart != -1 && runLen > 0) {
                    videodisplay.fillRect(runStart, (int)dyStart, runLen, dstHeight, runColor);
                    runStart = -1; runLen = 0;
                }
                continue;
            }
 
            if (runStart == -1) {
                runStart = dx; runColor = color32; runLen = 1;
            } else {
                if (color32 == runColor) {
                    runLen++;
                } else {
                    videodisplay.fillRect(runStart, (int)dyStart, runLen, dstHeight, runColor);
                    runStart = dx; runColor = color32; runLen = 1;
                }
            }
        }
 
        if (runStart != -1 && runLen > 0) {
            videodisplay.fillRect(runStart, (int)dyStart, runLen, dstHeight, runColor);
        }
 
        if (yieldEveryNRows > 0 && ((fileRow & (yieldEveryNRows - 1)) == 0)) yield();
    }
 
    releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
    bmp.close();
    Serial.printf("Wallpaper rendered (hybrid fast) in %lu ms. FreeHeap after: %u\n",
                  (unsigned long)(millis() - renderStartMs),
                  (unsigned)ESP.getFreeHeap());
}

static bool renderBMPScaledRectFromSPIFFS(const char* spiffsPath, int outX, int outY, int outW, int outH) {
    DisplayPolarityScope rawBitmapDraw(false);
    if (outW <= 0 || outH <= 0) return false;
    cleanupWallpaperArtifactsForRender(spiffsPath);
    if (!SPIFFS.exists(spiffsPath)) return false;
    mirrorBMP(spiffsPath, 0, outX, outY, outW, outH, 0, 0, outW, outH, false, false);
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);

    File bmp = SPIFFS.open(spiffsPath, FILE_READ);
    if (!bmp) return false;

    if (!loadWallpaperBMPInfo(bmp, spiffsPath, false)) {
        bmp.close();
        return false;
    }

    uint32_t pixelDataOffset = wallpaperBMPPixelOffset;
    int srcW = wallpaperBMPSrcW;
    int srcH = wallpaperBMPSrcH;
    bool topDown = wallpaperBMPTopDown;
    int srcRowStride = wallpaperBMPRowStride;

    if (!ensureWallpaperMapX(srcW, outW)) {
        bmp.close();
        return false;
    }

    bool rowBufHeapAllocated = false;
    uint8_t *rowBuf = acquireWallpaperRowBuffer((size_t)srcRowStride, &rowBufHeapAllocated);
    if (!rowBuf) {
        bmp.close();
        return false;
    }

    ensureWallpaperToneLUT();
    const int wpWhiteThresh = wallpaperWhiteAlphaThreshold;
    const int wpBlackThresh = wallpaperBlackAlphaThreshold;
    const bool skipWPWhite = false;
    const bool skipWPBlack = false;
    const int whiteBlendQ = floatToQ256(whiteBlend);
    const int blackBlendQ = floatToQ256(blackBlend);

    if (!bmp.seek(pixelDataOffset)) {
        releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
        bmp.close();
        return false;
    }

    for (int fileRow = 0; fileRow < srcH; ++fileRow) {
        int got = bmp.read(rowBuf, srcRowStride);
        if (got < srcRowStride) {
            int clearFrom = got > 0 ? got : 0;
            memset(rowBuf + clearFrom, 0, srcRowStride - clearFrom);
        }

        int srcY = topDown ? fileRow : (srcH - 1 - fileRow);
        long dyStart = (long)srcY * outH / srcH;
        long dyEnd = (((long)(srcY + 1) * outH) - 1) / srcH;
        if (dyStart < 0) dyStart = 0;
        if (dyEnd >= outH) dyEnd = outH - 1;
        if (dyStart > dyEnd) continue;

        int dstHeight = (int)(dyEnd - dyStart + 1);
        int runStart = -1;
        uint32_t runColor = 0;
        int runLen = 0;

        for (int dx = 0; dx < outW; ++dx) {
            int sx = wallpaperMapX[dx];
            int i = sx * 3;
            bool drawPixel = true;
            uint32_t color32 = wallpaperColorFromBGR(rowBuf[i + 0], rowBuf[i + 1], rowBuf[i + 2],
                                                     &drawPixel,
                                                     wpWhiteThresh, wpBlackThresh,
                                                     skipWPWhite, skipWPBlack,
                                                     whiteBlendQ, blackBlendQ);
            if (!drawPixel) color32 = 0;

            if (runStart == -1) {
                runStart = dx;
                runColor = color32;
                runLen = 1;
            } else if (color32 == runColor) {
                runLen++;
            } else {
                videodisplay.fillRect(outX + runStart, outY + (int)dyStart, runLen, dstHeight, runColor);
                runStart = dx;
                runColor = color32;
                runLen = 1;
            }
        }

        if (runStart != -1 && runLen > 0) {
            videodisplay.fillRect(outX + runStart, outY + (int)dyStart, runLen, dstHeight, runColor);
        }
        if ((fileRow & 31) == 0) yield();
    }

    releaseWallpaperRowBuffer(rowBuf, rowBufHeapAllocated);
    bmp.close();
    return true;
}
 
 
#define ICON_COUNT 6
 
const char* ICON_SD_PATHS[ICON_COUNT] = {
    "/UserData/Icons/Utilities.bmp",
    "/UserData/Icons/Files.bmp",
    "/UserData/Icons/Restart.bmp",
    "/UserData/Icons/Configure.bmp",
    "/UserData/Icons/Programmer.bmp",
    "/UserData/Icons/Terminal.bmp"
};
 
const char* ICON_SPIFFS_PATHS[ICON_COUNT] = {
    "/icons/Utilities.bmp",
    "/icons/Files.bmp",
    "/icons/Restart.bmp",
    "/icons/Configure.bmp",
    "/icons/Programmer.bmp",
    "/icons/Terminal.bmp"
};
 
int iconW[ICON_COUNT] = {0};
int iconH[ICON_COUNT] = {0};
uint32_t iconPixelOffset[ICON_COUNT] = {0};
uint16_t iconBpp[ICON_COUNT] = {0};
bool iconAvailableInSPIFFS[ICON_COUNT] = { false };
 
bool readIconHeaderFromSPIFFS(int idx) {
    if (idx < 0 || idx >= ICON_COUNT) return false;
    const char* path = ICON_SPIFFS_PATHS[idx];
    if (!SPIFFS.exists(path)) {
        iconAvailableInSPIFFS[idx] = false;
        return false;
    }
 
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        iconAvailableInSPIFFS[idx] = false;
        return false;
    }
 
    uint8_t header[54];
    if ((int)f.read(header, 54) != 54) {
        f.close();
        iconAvailableInSPIFFS[idx] = false;
        return false;
    }
 
    if (header[0] != 'B' || header[1] != 'M') {
        f.close();
        iconAvailableInSPIFFS[idx] = false;
        return false;
    }
 
    iconPixelOffset[idx] = readLE32(header, 10);
    iconW[idx] = (int32_t)readLE32(header, 18);
    iconH[idx] = (int32_t)readLE32(header, 22);
    iconBpp[idx] = header[28] | (header[29] << 8);
 
    if (iconW[idx] <= 0 || iconH[idx] <= 0 || iconBpp[idx] != 24) {
        f.close();
        iconAvailableInSPIFFS[idx] = false;
        return false;
    }
 
    iconAvailableInSPIFFS[idx] = true;
    f.close();
    Serial.printf("Icon in SPIFFS: %s (%dx%d bpp=%d offset=%d)\n", path, iconW[idx], iconH[idx], iconBpp[idx], (int)iconPixelOffset[idx]);
    return true;
}
 
void copyIconsFromSDToSPIFFSAndReadHeaders() {
    if (!SPIFFS.exists("/icons")) {
        SPIFFS.mkdir("/icons");
    }
 
    for (int i = 0; i < ICON_COUNT; ++i) {
        String sdPath = ICON_SD_PATHS[i];
        String spPath = ICON_SPIFFS_PATHS[i];
 
        if (SD.exists(sdPath)) {
            if (SPIFFS.exists(spPath) && filesMatchSDvsSPIFFS(sdPath, spPath)) {
                Serial.printf("SPIFFS icon up-to-date, skipping copy: %s\n", spPath);
            } else {
                Serial.printf("Copying icon %d from SD to SPIFFS: %s -> %s\n", i, sdPath, spPath);
                bool copied = copyFileSDToSPIFFS(sdPath, spPath);
                if (!copied) {
                    Serial.printf("Failed to copy icon from SD: %s\n", sdPath);
                }
            }
        } else {
            Serial.printf("No SD icon at: %s (will look for existing SPIFFS copy)\n", sdPath);
        }
 
        if (SPIFFS.exists(spPath)) {
            if (!readIconHeaderFromSPIFFS(i)) {
                Serial.printf("SPIFFS icon header invalid: %s\n", spPath);
            }
        } else {
            iconAvailableInSPIFFS[i] = false;
        }
    }
}
 
void drawIconFromSPIFFS(int iconIndex, int dstX, int dstY, int dstW, int dstH) {
    DisplayPolarityScope rawBitmapDraw(false);
    if (iconIndex < 0 || iconIndex >= ICON_COUNT) return;
    if (!iconAvailableInSPIFFS[iconIndex]) {
        videodisplay.line(dstX, dstY, dstX + dstW - 1, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        videodisplay.line(dstX + dstW - 1, dstY, dstX, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        return;
    }
 
    const char* path = ICON_SPIFFS_PATHS[iconIndex];
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        videodisplay.line(dstX, dstY, dstX + dstW - 1, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        videodisplay.line(dstX + dstW - 1, dstY, dstX, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        return;
    }
 
    int srcW = iconW[iconIndex];
    int srcH = iconH[iconIndex];
    uint32_t pixelDataOffset = iconPixelOffset[iconIndex];
    uint16_t bpp = iconBpp[iconIndex];
 
    if (bpp != 24 || srcW <= 0 || srcH <= 0) {
        f.close();
        videodisplay.line(dstX, dstY, dstX + dstW - 1, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        videodisplay.line(dstX + dstW - 1, dstY, dstX, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        return;
    }
 
    int srcRowBytes = srcW * 3;
    int padding = (4 - (srcRowBytes % 4)) % 4;
    int srcRowStride = srcRowBytes + padding;
 
    uint8_t* rowBuf = (uint8_t*)malloc(srcRowStride);
    if (!rowBuf) {
        Serial.println("Out of memory for icon row");
        f.close();
        return;
    }
 
    float brightnessMul = ((float)bmpBrightnessPercent) / 100.0f;

    mirrorBMP(path, 1, dstX, dstY, dstW, dstH, 0, 0, dstW, dstH, true, true);
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
 
    for (int dy = 0; dy < dstH; ++dy) {
        int srcY = constrain((int)((long long)dy * srcH / dstH), 0, srcH - 1);
 
        uint32_t rowOffset = pixelDataOffset + (uint32_t)((srcH - 1 - srcY) * srcRowStride);
        f.seek(rowOffset);
        int got = f.read(rowBuf, srcRowStride);
        if (got <= 0) {
            memset(rowBuf, 0, srcRowStride);
        }
 
        for (int dx = 0; dx < dstW; ++dx) {
            int srcX = constrain((int)((long long)dx * srcW / dstW), 0, srcW - 1);
            int i = srcX * 3;
            uint8_t B = rowBuf[i + 0];
            uint8_t G = rowBuf[i + 1];
            uint8_t R = rowBuf[i + 2];
 
            float r_s = (float)R * brightnessMul;
            float g_s = (float)G * brightnessMul;
            float b_s = (float)B * brightnessMul;
 
            float luminance = 0.299f * r_s + 0.587f * g_s + 0.114f * b_s;
            luminance = constrain(luminance, 0.0f, 255.0f);
 
            if (luminance <= (float)iconBlackAlphaThreshold ||
                (iconWhiteAlphaThreshold <= 255 && luminance >= (float)iconWhiteAlphaThreshold)) {
                continue;
            }
 
            G = B;
            if (!redAvailable) R = 0;
 
            int rAdj = constrain((int)( (float)R * brightnessMul + 0.5f), 0, 255);
            int gAdj = constrain((int)( (float)G * brightnessMul + 0.5f), 0, 255);
            int bAdj = constrain((int)( (float)B * brightnessMul + 0.5f), 0, 255);
 
            uint32_t color = videodisplay.RGB(rAdj, gAdj, bAdj);
            videodisplay.fillRect(dstX + dx, dstY + dy, 1, 1, color);
        }
 
        if ((dy & 0x3F) == 0) yield();
    }
 
    free(rowBuf);
    f.close();
}
String basenameNoExt(const String &fn) {
    String name = fn;
    int slash = name.lastIndexOf('/');
    if (slash != -1) name = name.substring(slash + 1);
    int dot = name.lastIndexOf('.');
    if (dot != -1) name = name.substring(0, dot);
    return name;
}
 
void copyOneAppIconFromSDToSPIFFS(const String &appfn) {
    if (appfn.length() == 0) return;
    if (appfn.startsWith("listed")) return;
    if (!appEntryIsFirmware(appfn)) return;

    String base = basenameNoExt(leafNameFromPath(appEntryPayload(appfn)));
    String sdPath = "/UserData/Icons/AppIcons/" + base + ".bmp";
    String spPath = "/icons/" + base + ".bmp";

    if (SD.exists(sdPath)) {
        if (!(SPIFFS.exists(spPath) && filesMatchSDvsSPIFFS(sdPath, spPath))) {
            copyFileSDToSPIFFS(sdPath, spPath);
            Serial.println("Found spPath matchup diskbarn: " + sdPath);
        }
    }
}

void copyAppIconsFromSDToSPIFFS() {
    if (!SPIFFS.exists("/icons")) SPIFFS.mkdir("/icons");
    Serial.println("Looking for appIcons");
    Serial.print("App File Count: ");
    Serial.println(appFileCount);

    if (SPIFFS.exists(APP_NAME_CACHE_PATH)) {
        File cache = SPIFFS.open(APP_NAME_CACHE_PATH, FILE_READ);
        while (cache && cache.available()) {
            String appfn = cache.readStringUntil('\n');
            appfn.trim();
            copyOneAppIconFromSDToSPIFFS(appfn);
        }
        if (cache) cache.close();
        return;
    }

    for (int i = 0; i < appFileCount; ++i) {
        String appEntry;
        if (readCachedAppEntry(i, appEntry)) copyOneAppIconFromSDToSPIFFS(appEntry);
    }
}
 
void drawIconFromSPIFFSPath(const char* spPath, int dstX, int dstY, int dstW, int dstH) {
    DisplayPolarityScope rawBitmapDraw(false);
    if (!SPIFFS.exists(spPath)) {
        videodisplay.line(dstX, dstY, dstX + dstW - 1, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        videodisplay.line(dstX + dstW - 1, dstY, dstX, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        return;
    }
 
    if (!gammaLUT_built) buildGammaLUT();
 
    File f = SPIFFS.open(spPath, FILE_READ);
    if (!f) {
        videodisplay.line(dstX, dstY, dstX + dstW - 1, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        videodisplay.line(dstX + dstW - 1, dstY, dstX, dstY + dstH - 1, videodisplay.RGB(0,0,0));
        return;
    }
 
    uint8_t header[54];
    if ((int)f.read(header, 54) != 54) { f.close(); return; }
    if (header[0] != 'B' || header[1] != 'M') { f.close(); return; }
 
    uint32_t pixelDataOffset = readLE32(header, 10);
    int srcW = (int32_t)readLE32(header, 18);
    int srcH_raw = (int32_t)readLE32(header, 22);
    uint16_t bpp = header[28] | (header[29] << 8);
    if (bpp != 24 || srcW <= 0 || srcH_raw == 0) { f.close(); return; }
 
    int srcH = srcH_raw > 0 ? srcH_raw : -srcH_raw;
    int srcRowBytes = srcW * 3;
    int padding = (4 - (srcRowBytes % 4)) % 4;
    int srcRowStride = srcRowBytes + padding;
    uint8_t* rowBuf = (uint8_t*)malloc(srcRowStride);
    if (!rowBuf) { f.close(); return; }
 
    float scaleW = (float)APP_ICON_MAX_SIZE / (float)srcW;
    float scaleH = (float)APP_ICON_MAX_SIZE / (float)srcH;
    float scaleMax = min(scaleW, scaleH);
    if (scaleMax > 1.0f) scaleMax = 1.0f;
 
    float scaleToDstW = (float)dstW / (float)srcW;
    float scaleToDstH = (float)dstH / (float)srcH;
    float scaleDst = min(scaleToDstW, scaleToDstH);
    float finalScale = min(scaleMax, scaleDst);
    if (finalScale <= 0.0f) finalScale = 1.0f;
 
    int tgtW = max(1, (int)floor(srcW * finalScale + 0.5f));
    int tgtH = max(1, (int)floor(srcH * finalScale + 0.5f));
 
    int drawX = dstX + (dstW - tgtW) / 2;
    int drawY = dstY + (dstH - tgtH) / 2;

    mirrorBMP(spPath, 2, drawX, drawY, tgtW, tgtH, 0, 0, tgtW, tgtH, true, true);
    CitCompositeColorDAC::MirrorSilence mirrorSilence(videodisplay);
 
    uint8_t *lut = gammaLUT;
    const float blackThreshold = (float)iconBlackAlphaThreshold;
    const float whiteThreshold = (float)iconWhiteAlphaThreshold;
 
    for (int dy = 0; dy < tgtH; ++dy) {
        int srcY = constrain((int)((long long)dy * srcH / tgtH), 0, srcH - 1);
        uint32_t rowOffset = pixelDataOffset + (uint32_t)((srcH - 1 - srcY) * srcRowStride);
        f.seek(rowOffset);
        int got = f.read(rowBuf, srcRowStride);
        if (got <= 0) memset(rowBuf, 0, srcRowStride);
 
        for (int dx = 0; dx < tgtW; ++dx) {
            int srcX = constrain((int)((long long)dx * srcW / tgtW), 0, srcW - 1);
            int i = srcX * 3;
            uint8_t B = rowBuf[i + 0];
            uint8_t G = rowBuf[i + 1];
            uint8_t R = rowBuf[i + 2];
 
            int rAdj = lut[R];
            int gAdj = lut[G];
            int bAdj = lut[B];
 
            float lum = 0.299f * (float)rAdj + 0.587f * (float)gAdj + 0.114f * (float)bAdj;
            if (lum <= blackThreshold || (iconWhiteAlphaThreshold <= 255 && lum >= whiteThreshold)) {
                continue;
            }
            gAdj = bAdj;
            if (!redAvailable) rAdj = 0;
 
            rAdj = constrain(rAdj, 0, 255);
            gAdj = constrain(gAdj, 0, 255);
            bAdj = constrain(bAdj, 0, 255);
 
            uint32_t color = videodisplay.RGB(rAdj, gAdj, bAdj);
            videodisplay.fillRect(drawX + dx, drawY + dy, 1, 1, color);
        }
 
        if ((dy & 0x3F) == 0) yield();
    }
 
    free(rowBuf);
    f.close();
}
 
void drawIconBuffer(int iconIndex, int dstX, int dstY, int dstW, int dstH) {
    drawIconFromSPIFFS(iconIndex, dstX, dstY, dstW, dstH);
}
 
void requestElevation(String a, int b){
    kernelCursorRestore();
    emBreakAction = b;
    emBreak = true;
    videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
    videodisplay.setFont(Font8x8);
    videodisplay.fillRect(60,60,255,150,videodisplay.RGB(255,255,255));
    videodisplay.fillRect(65,180,50,20,0);
    videodisplay.setCursor(83,182);
    videodisplay.println("Y");
    videodisplay.fillRect(140,180,50,20,0);
    videodisplay.setCursor(158,182);
    videodisplay.println("N");
    videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
    videodisplay.setCursor(65, 70);
    videodisplay.println(a.c_str());
    kernelCursorRefreshAfterRedraw();
}

static void closeElevationOverlay() {
    kernelCursorRestore();
    restoreDesktopRect(60, 60, 255, 150);
    emBreak = false;
    kernelCursorRefreshAfterRedraw();
}
 
 
void consoleOut(String a);
void execute(String a);

static int terminalConsoleX() { return kernelWindowClientX() + 7; }
static int terminalConsoleY() { return kernelWindowClientY() + 7; }
static int terminalConsoleW() { return max(20, kernelWindowClientW() - 14); }
static int terminalInputY() { return kernelWindow.y + kernelWindow.h - 25; }
static int terminalConsoleH() { return max(16, terminalInputY() - terminalConsoleY() - 7); }
static int terminalVisibleLines() { return max(1, (terminalConsoleH() - 6) / consoleLineHeight); }

static void drawTerminalInput() {
    if (!kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_TERMINAL) return;
    int x = terminalConsoleX();
    int y = terminalInputY();
    int w = terminalConsoleW();
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    uint32_t accent = videodisplay.RGB(54, 214, 132);
    videodisplay.fillRect(x, y, w, 17, black);
    videodisplay.rect(x, y, w, 17, accent);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(accent, black);
    videodisplay.setCursor(x + 4, y + 5);
    videodisplay.print(">");

    const int maxChars = max(1, (w - 24) / 6);
    int viewStart = max(0, shellCursorPosition - maxChars + 1);
    if (viewStart + maxChars > (int)executable.length()) {
        viewStart = max(0, (int)executable.length() - maxChars);
    }
    String visible = executable.substring(viewStart, min((int)executable.length(), viewStart + maxChars));
    videodisplay.setTextColor(white, black);
    videodisplay.setCursor(x + 14, y + 5);
    videodisplay.print(visible.c_str());
    int cursorColumn = constrain(shellCursorPosition - viewStart, 0, maxChars);
    videodisplay.fillRect(x + 14 + cursorColumn * 6, y + 4, 1, 9, white);
}

void redrawConsole() {
    if (!showLoggerOnScreen || !kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_TERMINAL) return;
    int x = terminalConsoleX();
    int y = terminalConsoleY();
    int w = terminalConsoleW();
    int h = terminalConsoleH();
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    uint32_t dim = videodisplay.RGB(72, 126, 106);
    videodisplay.fillRect(x, y, w, h, black);
    videodisplay.rect(x, y, w, h, dim);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, black);

    int capacity = terminalVisibleLines();
    int visible = min(consoleCount, capacity);
    int bottomIndex = (consoleHead - 1 - consoleScroll + CONSOLE_LINES) % CONSOLE_LINES;
    int topIndex = (bottomIndex - (visible - 1) + CONSOLE_LINES) % CONSOLE_LINES;
    int drawY = y + 4;
    int maxChars = max(1, (w - 8) / 6);
    for (int i = 0; i < visible; ++i) {
        int idx = (topIndex + i) % CONSOLE_LINES;
        videodisplay.setCursor(x + 4, drawY);
        videodisplay.print(consoleBuf[idx].substring(0, maxChars).c_str());
        drawY += consoleLineHeight;
    }
}

static void drawTerminalWindowContent() {
    uint32_t panel = videodisplay.RGB(10, 13, 14);
    videodisplay.fillRect(kernelWindowClientX(), kernelWindowClientY(),
                          kernelWindowClientW(), kernelWindowClientH(), panel);
    redrawConsole();
    drawTerminalInput();
}

void CitCommandPPT(){
    shell = true;
    applaunched = true;
    util = false;
    fE = false;
    logLine = 1;
    executable = "";
    shellCursorPosition = 0;
    shellHistoryPosition = -1;
    openKernelWindow(KERNEL_WINDOW_TERMINAL, "Terminal", 342, 238, drawTerminalWindowContent);
    if (consoleCount == 0) {
        consoleOut("Citadela Terminal ready. Type help for commands.");
    }
}

void buildShell(String text) {
    if (text.length() == 0) {
        drawTerminalInput();
        return;
    }
    if (executable.length() + text.length() > 96) return;
    executable = executable.substring(0, shellCursorPosition) + text + executable.substring(shellCursorPosition);
    shellCursorPosition += text.length();
    drawTerminalInput();
}
void consoleScrollUp() {
    int capacity = terminalVisibleLines();
    if (consoleCount <= capacity) return;
    int maxScroll = consoleCount - capacity;
    if (consoleScroll < maxScroll) {
        consoleScroll++;
        redrawConsole();
    }
}
 
void consoleScrollDown() {
    if (consoleScroll > 0) {
        consoleScroll--;
        redrawConsole();
    }
}
void consoleOut(String a) {
    Serial.println(a);
 
    int start = 0;
    while (start < a.length()) {
        int idx = a.indexOf('\n', start);
        String line;
        if (idx == -1) {
            line = a.substring(start);
            start = a.length();
        } else {
            line = a.substring(start, idx);
            start = idx + 1;
        }
        if (line.endsWith("\r")) {
            line = line.substring(0, line.length() - 1);
        }
 
        consoleBuf[consoleHead] = line;
        consoleHead = (consoleHead + 1) % CONSOLE_LINES;
        if (consoleCount < CONSOLE_LINES) consoleCount++;
 
        if (consoleScroll != 0) {
            int maxScroll = max(0, consoleCount - terminalVisibleLines());
            if (consoleScroll > maxScroll) consoleScroll = maxScroll;
        }
    }
 
    if (consoleBatchDepth == 0) redrawConsole();
}

static void beginConsoleBatch() {
    consoleBatchDepth++;
}

static void endConsoleBatch() {
    if (consoleBatchDepth > 0) consoleBatchDepth--;
    if (consoleBatchDepth == 0) redrawConsole();
}

static void shellRememberCommand(const String &command) {
    if (!command.length()) return;
    if (shellHistorySize > 0 && shellHistory[shellHistorySize - 1] == command) return;
    if (shellHistorySize < SHELL_HISTORY_COUNT) {
        shellHistory[shellHistorySize++] = command;
    } else {
        for (int i = 1; i < SHELL_HISTORY_COUNT; ++i) shellHistory[i - 1] = shellHistory[i];
        shellHistory[SHELL_HISTORY_COUNT - 1] = command;
    }
}

static void shellUseHistory(int direction) {
    if (shellHistorySize <= 0) return;
    if (shellHistoryPosition < 0) shellHistoryPosition = shellHistorySize;
    shellHistoryPosition = constrain(shellHistoryPosition + direction, 0, shellHistorySize);
    executable = shellHistoryPosition == shellHistorySize ? "" : shellHistory[shellHistoryPosition];
    shellCursorPosition = executable.length();
    drawTerminalInput();
}

static void shellBackspace() {
    if (shellCursorPosition <= 0 || executable.length() == 0) return;
    executable.remove(shellCursorPosition - 1, 1);
    shellCursorPosition--;
    drawTerminalInput();
}

static void shellDelete() {
    if (shellCursorPosition >= executable.length()) return;
    executable.remove(shellCursorPosition, 1);
    drawTerminalInput();
}

static void handleShellInput(String input) {
    input.trim();
    if (!input.length() || input.indexOf("rlsd") != -1) return;
    if (input == "Enter") {
        String command = executable;
        command.trim();
        if (command.length()) {
            consoleOut(String("> ") + command);
            shellRememberCommand(command);
        }
        executable = "";
        shellCursorPosition = 0;
        shellHistoryPosition = -1;
        drawTerminalInput();
        if (command.length()) execute(command);
        return;
    }
    if (input == "Escape") {
        executable = "";
        shellCursorPosition = 0;
        restoreAndCloseKernelWindow();
        return;
    }
    if (input == "Backspace") { shellBackspace(); return; }
    if (input == "Delete") { shellDelete(); return; }
    if (input == "LeftArrow") {
        shellCursorPosition = max(0, shellCursorPosition - 1);
        drawTerminalInput();
        return;
    }
    if (input == "RightArrow") {
        shellCursorPosition = min((int)executable.length(), shellCursorPosition + 1);
        drawTerminalInput();
        return;
    }
    if (input == "UpArrow") { shellUseHistory(-1); return; }
    if (input == "DownArrow") { shellUseHistory(1); return; }
    if (input == "PageUp") { consoleScrollUp(); return; }
    if (input == "PageDown") { consoleScrollDown(); return; }
    if (input == "Tab") { buildShell("  "); return; }
    if (input == "Space") { buildShell(" "); return; }

    int modifier = input.lastIndexOf(" + ");
    if (modifier >= 0) input = input.substring(modifier + 3);
    if (input.length() == 1 && input.charAt(0) >= 32 && input.charAt(0) <= 126) {
        buildShell(input);
    }
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
void configUpdate() {
    File file = SPIFFS.open("/systemConf.txt", FILE_WRITE);
    if (file) {
        file.println(boolRes);
        file.close();
        Serial.printf("Config updated! %s\n");
    } else {
        Serial.println("Failed to editConfig!");
    }
}
void reRememberApp() {
    File file = SPIFFS.open("/remApp.txt", FILE_WRITE);
    if (file) {
        file.seek(0);
        file.print("");
        file.println(appString);
        file.close();
        Serial.printf("String updated to %s\n", appString.c_str());
    } else {
        Serial.println("Failed to update appString!");
    }
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
void drawLambda(int cx, int cy, float s, uint32_t fillColor, uint32_t strokeColor) {
  auto scaled = [&](int off) -> int {
    return (int)roundf(off * s);
  };
 
  int r = scaled(20);
  videodisplay.fillCircle(cx, cy, r, videodisplay.RGB(255,255,255));
 
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
 
String configThrow(int id) {
    switch(id) {
        case 0: return "Wallpaper Tog";
        case 1: return "Tooltips Tog";
        case 2: return "Verbose UART";
        case 3: return "Display Colour";
        case 4: return "WiFi Core Tog";
        case 5: return "Telemetry Data";
        case 6: return "FastBoot";
        case 7: return "Audio Driver";
        case 8: return "Invert Colours";
        case 9: return "Set Time/Date";
        case 10: return "Black Alpha";
        case 11: return "White Alpha";
        case 12: return "Mouse Outline";
        case 13: return "Wallpaper Menu";
        case 14: return "UART Upload";
        case 15: return "Serial Display";
        case 16: return "Bluetooth Connect";
        case 17: return "WiFi Connection";
        default: return " --- ";
    }
}

static const int CONFIG_ITEM_COUNT = 18;
static const int CONFIG_PAGE_ROWS = 9;

void configureMenu(int snapDrag);
void timeClock(String time, String date);
void home();
void controller();
void drawButtons(int selected);
void visualheap();
static void drawMissingIconMark(int x, int y, int w, int h);
void openIconThresholdMenu(bool whiteMode);
void handleIconThresholdInput(String input);
void openWallpaperPickerMenu();
void handleWallpaperPickerInput(String input);
void openUARTUploadMenu();
void handleUARTUploadInput(String input);
static void handleUARTUploadBeginCommand(const String &command);
void openBluetoothConnectionMenu();
void openWiFiConnectionMenu();
static void handleConnectionInput(String input);
static bool handleConnectionControllerLine(String input);
static void serviceConnectionWindow();
int configurationswitch(int val);

static bool manualTimeLeapYear(int y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int manualTimeDaysInMonth(int y, int m) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && manualTimeLeapYear(y)) return 29;
    if (m < 1 || m > 12) return 31;
    return days[m - 1];
}

static String twoDigits(int v) {
    if (v < 10) return String("0") + String(v);
    return String(v);
}

static String fourDigits(int v) {
    if (v < 10) return String("000") + String(v);
    if (v < 100) return String("00") + String(v);
    if (v < 1000) return String("0") + String(v);
    return String(v);
}

static void clampManualTime() {
    manualYear = constrain(manualYear, 2020, 2099);
    manualMonth = constrain(manualMonth, 1, 12);
    manualDay = constrain(manualDay, 1, manualTimeDaysInMonth(manualYear, manualMonth));
    manualHour = constrain(manualHour, 0, 23);
    manualMinute = constrain(manualMinute, 0, 59);
    manualSecond = constrain(manualSecond, 0, 59);
}

static String manualDateString() {
    clampManualTime();
    return fourDigits(manualYear) + "-" + twoDigits(manualMonth) + "-" + twoDigits(manualDay);
}

static String manualTimeString() {
    clampManualTime();
    return twoDigits(manualHour) + ":" + twoDigits(manualMinute) + ":" + twoDigits(manualSecond);
}

static bool parseClockIntoManual() {
    if (currentClockDate.length() >= 10) {
        manualYear = currentClockDate.substring(0, 4).toInt();
        manualMonth = currentClockDate.substring(5, 7).toInt();
        manualDay = currentClockDate.substring(8, 10).toInt();
    }
    if (currentClockTime.length() >= 8) {
        manualHour = currentClockTime.substring(0, 2).toInt();
        manualMinute = currentClockTime.substring(3, 5).toInt();
        manualSecond = currentClockTime.substring(6, 8).toInt();
    }
    clampManualTime();
    return true;
}

static void drawManualTimeField(int id, int x, int y, int w, const char *label, const String &value) {
    bool selected = (manualTimeField == id);
    uint32_t border = videodisplay.RGB(255,255,255);
    uint32_t fill = selected ? videodisplay.RGB(255,255,255) : 0;
    uint32_t text = selected ? 0 : videodisplay.RGB(255,255,255);
    videodisplay.rect(x, y, w, 22, border);
    videodisplay.fillRect(x + 1, y + 1, w - 2, 20, fill);
    videodisplay.setTextColor(text, fill);
    videodisplay.setCursor(x + 4, y + 3);
    videodisplay.print(label);
    videodisplay.setCursor(x + 4, y + 12);
    videodisplay.print(value.c_str());
}

static void drawManualTimeAction(int id, int x, int y, int w, const char *label) {
    bool selected = (manualTimeField == id);
    uint32_t fill = selected ? videodisplay.RGB(255,255,255) : 0;
    uint32_t text = selected ? 0 : videodisplay.RGB(255,255,255);
    videodisplay.rect(x, y, w, 18, videodisplay.RGB(255,255,255));
    videodisplay.fillRect(x + 1, y + 1, w - 2, 16, fill);
    videodisplay.setTextColor(text, fill);
    videodisplay.setCursor(x + 12, y + 5);
    videodisplay.print(label);
}

static void drawManualTimeItem(int id) {
    videodisplay.setFont(Font6x8);
    switch (id) {
        case 0: drawManualTimeField(0, 72, 90, 62, "Year", String(manualYear)); break;
        case 1: drawManualTimeField(1, 144, 90, 48, "Month", twoDigits(manualMonth)); break;
        case 2: drawManualTimeField(2, 202, 90, 48, "Day", twoDigits(manualDay)); break;
        case 3: drawManualTimeField(3, 72, 140, 48, "Hour", twoDigits(manualHour)); break;
        case 4: drawManualTimeField(4, 132, 140, 48, "Min", twoDigits(manualMinute)); break;
        case 5: drawManualTimeField(5, 192, 140, 48, "Sec", twoDigits(manualSecond)); break;
        case 6: drawManualTimeAction(6, 80, 202, 74, "Save"); break;
        case 7: drawManualTimeAction(7, 174, 202, 82, "Cancel"); break;
    }
}

void drawManualTimeMenu() {
    kernelCursorRestore();
    clampManualTime();
    videodisplay.fillRect(48, 42, 284, 202, 0);
    videodisplay.rect(48, 42, 284, 202, videodisplay.RGB(255,255,255));
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), 0);
    videodisplay.setCursor(104, 54);
    videodisplay.print("Set Time & Date");

    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(72, 78);
    videodisplay.print("Date");
    drawManualTimeField(0, 72, 90, 62, "Year", String(manualYear));
    drawManualTimeField(1, 144, 90, 48, "Month", twoDigits(manualMonth));
    drawManualTimeField(2, 202, 90, 48, "Day", twoDigits(manualDay));

    videodisplay.setTextColor(videodisplay.RGB(255,255,255), 0);
    videodisplay.setCursor(72, 128);
    videodisplay.print("Time");
    drawManualTimeField(3, 72, 140, 48, "Hour", twoDigits(manualHour));
    drawManualTimeField(4, 132, 140, 48, "Min", twoDigits(manualMinute));
    drawManualTimeField(5, 192, 140, 48, "Sec", twoDigits(manualSecond));

    drawManualTimeAction(6, 80, 202, 74, "Save");
    drawManualTimeAction(7, 174, 202, 82, "Cancel");
    kernelCursorRefreshAfterRedraw();
}

void openManualTimeMenu() {
    if (kernelWindow.active) restoreAndCloseKernelWindow();
    configurationPending = false;
    manualTimePending = true;
    applaunched = true;
    manualTimeField = 0;
    parseClockIntoManual();
    drawManualTimeMenu();
}

static void closeManualTimeMenu(bool saveTime) {
    if (saveTime) {
        String date = manualDateString();
        String time = manualTimeString();
        String cmd = String("CBSETTIME ") + date + " " + time;
        Serial.println(cmd);
        Serial1.println(cmd);
        timeClock(time, date);
    }
    manualTimePending = false;
    kernelCursorRestore();
    restoreDesktopRect(48, 42, 284, 202);
    configureMenu(10);
}

static void changeManualTimeValue(int delta) {
    if (manualTimeField == 0) manualYear += delta;
    else if (manualTimeField == 1) manualMonth += delta;
    else if (manualTimeField == 2) manualDay += delta;
    else if (manualTimeField == 3) manualHour += delta;
    else if (manualTimeField == 4) manualMinute += delta;
    else if (manualTimeField == 5) manualSecond += delta;
    else if (manualTimeField == 6 || manualTimeField == 7) manualTimeField = (manualTimeField == 6) ? 7 : 6;

    if (manualMonth < 1) manualMonth = 12;
    if (manualMonth > 12) manualMonth = 1;
    int dim = manualTimeDaysInMonth(manualYear, manualMonth);
    if (manualDay < 1) manualDay = dim;
    if (manualDay > dim) manualDay = 1;
    if (manualHour < 0) manualHour = 23;
    if (manualHour > 23) manualHour = 0;
    if (manualMinute < 0) manualMinute = 59;
    if (manualMinute > 59) manualMinute = 0;
    if (manualSecond < 0) manualSecond = 59;
    if (manualSecond > 59) manualSecond = 0;
    clampManualTime();
}

void handleManualTimeInput(String input) {
    input.trim();
    if (input.length() == 0 || input == "rlsd") return;
    KernelCursorDrawGuard cursorGuard;

    int oldField = manualTimeField;
    int oldYear = manualYear;
    int oldMonth = manualMonth;
    int oldDay = manualDay;
    int oldHour = manualHour;
    int oldMinute = manualMinute;
    int oldSecond = manualSecond;
    bool handled = true;

    if (input == "Escape") {
        closeManualTimeMenu(false);
        return;
    }
    if (input == "LeftArrow") {
        manualTimeField = (manualTimeField + 7) % 8;
    } else if (input == "RightArrow") {
        manualTimeField = (manualTimeField + 1) % 8;
    } else if (input == "UpArrow") {
        changeManualTimeValue(1);
    } else if (input == "DownArrow") {
        changeManualTimeValue(-1);
    } else if (input == "Enter") {
        if (manualTimeField == 6) {
            closeManualTimeMenu(true);
            return;
        }
        if (manualTimeField == 7) {
            closeManualTimeMenu(false);
            return;
        }
        manualTimeField = (manualTimeField + 1) % 8;
    } else {
        handled = false;
    }

    if (!handled) return;

    if (oldField != manualTimeField) {
        drawManualTimeItem(oldField);
        drawManualTimeItem(manualTimeField);
    }

    if (oldYear != manualYear) drawManualTimeItem(0);
    if (oldMonth != manualMonth) drawManualTimeItem(1);
    if (oldDay != manualDay) drawManualTimeItem(2);
    if (oldHour != manualHour) drawManualTimeItem(3);
    if (oldMinute != manualMinute) drawManualTimeItem(4);
    if (oldSecond != manualSecond) drawManualTimeItem(5);
}

static const int ICON_THRESHOLD_WIN_W = 220;
static const int ICON_THRESHOLD_WIN_H = 84;
static const int ICON_THRESHOLD_WIN_X = (SCREEN_WIDTH - ICON_THRESHOLD_WIN_W) / 2;
static const int ICON_THRESHOLD_WIN_Y = (SCREEN_HEIGHT - ICON_THRESHOLD_WIN_H) / 2;
static const int ICON_THRESHOLD_SLIDER_X = ICON_THRESHOLD_WIN_X + 24;
static const int ICON_THRESHOLD_SLIDER_Y = ICON_THRESHOLD_WIN_Y + 52;
static const int ICON_THRESHOLD_SLIDER_W = ICON_THRESHOLD_WIN_W - 48;

static void saveIconThresholdIfDirty() {
    if (!iconThresholdDirty) return;
    writeSystemConfigToSPIFFS();
    iconThresholdDirty = false;
}

static void redrawHomeIconsForThreshold() {
    drawButtons(0);
}

static int currentIconThresholdValue() {
    return iconThresholdWhiteMode ? iconWhiteAlphaThreshold : iconBlackAlphaThreshold;
}

static int currentIconThresholdMax() {
    return iconThresholdWhiteMode ? 256 : 255;
}

static void setCurrentIconThresholdValue(int value) {
    if (iconThresholdWhiteMode) {
        iconWhiteAlphaThreshold = constrain(value, 0, 256);
    } else {
        iconBlackAlphaThreshold = constrain(value, 0, 255);
    }
}

static void drawIconThresholdSlider() {
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    int threshold = currentIconThresholdValue();
    int maxThreshold = currentIconThresholdMax();
    int panelX = ICON_THRESHOLD_WIN_X + 12;
    int panelY = ICON_THRESHOLD_WIN_Y + 28;
    int panelW = ICON_THRESHOLD_WIN_W - 24;
    int panelH = 44;

    videodisplay.fillRect(panelX, panelY, panelW, panelH, black);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, black);
    videodisplay.setCursor(panelX, panelY + 2);
    videodisplay.print("Threshold ");
    if (iconThresholdWhiteMode && threshold >= 256) {
        videodisplay.print("OFF");
    } else {
        videodisplay.print(threshold);
    }

    videodisplay.rect(ICON_THRESHOLD_SLIDER_X, ICON_THRESHOLD_SLIDER_Y - 5,
                      ICON_THRESHOLD_SLIDER_W, 12, white);
    videodisplay.fillRect(ICON_THRESHOLD_SLIDER_X + 2, ICON_THRESHOLD_SLIDER_Y,
                          ICON_THRESHOLD_SLIDER_W - 4, 2, white);

    int knobX = ICON_THRESHOLD_SLIDER_X + map(threshold, 0, maxThreshold, 0, ICON_THRESHOLD_SLIDER_W - 1);
    videodisplay.fillRect(knobX - 2, ICON_THRESHOLD_SLIDER_Y - 9, 5, 20, white);
}

static void drawIconThresholdWindow() {
    kernelCursorRestore();
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(ICON_THRESHOLD_WIN_X, ICON_THRESHOLD_WIN_Y,
                          ICON_THRESHOLD_WIN_W, ICON_THRESHOLD_WIN_H, black);
    videodisplay.rect(ICON_THRESHOLD_WIN_X, ICON_THRESHOLD_WIN_Y,
                      ICON_THRESHOLD_WIN_W, ICON_THRESHOLD_WIN_H, white);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(white, black);
    videodisplay.setCursor(ICON_THRESHOLD_WIN_X + 54, ICON_THRESHOLD_WIN_Y + 10);
    videodisplay.print(iconThresholdWhiteMode ? "White Alpha" : "Black Alpha");
    drawIconThresholdSlider();
    kernelCursorRefreshAfterRedraw();
}

static void changeIconThresholdValue(int delta) {
    int oldValue = currentIconThresholdValue();
    setCurrentIconThresholdValue(oldValue + delta);
    if (currentIconThresholdValue() == oldValue) return;
    iconThresholdDirty = true;
    kernelCursorRestore();
    drawIconThresholdSlider();
    redrawHomeIconsForThreshold();
    kernelCursorRefreshAfterRedraw();
}

void openIconThresholdMenu(bool whiteMode) {
    if (kernelWindow.active) restoreAndCloseKernelWindow();
    configurationPending = false;
    manualTimePending = false;
    iconThresholdPending = false;
    iconThresholdDirty = false;
    iconThresholdSliding = false;
    iconThresholdWhiteMode = whiteMode;
    iconThresholdPending = true;
    applaunched = true;
    drawIconThresholdWindow();
}

static void closeIconThresholdMenu() {
    saveIconThresholdIfDirty();
    iconThresholdPending = false;
    iconThresholdSliding = false;
    kernelCursorRestore();
    restoreDesktopRect(ICON_THRESHOLD_WIN_X, ICON_THRESHOLD_WIN_Y,
                       ICON_THRESHOLD_WIN_W, ICON_THRESHOLD_WIN_H);
    configureMenu(iconThresholdWhiteMode ? 12 : 11);
}

void handleIconThresholdInput(String input) {
    input.trim();
    if (input.length() == 0) return;

    if (input.indexOf("rlsd") != -1) {
        iconThresholdSliding = false;
        saveIconThresholdIfDirty();
        return;
    }

    if (input == "LeftArrow") {
        iconThresholdSliding = true;
        changeIconThresholdValue(-5);
        return;
    }

    if (input == "RightArrow") {
        iconThresholdSliding = true;
        changeIconThresholdValue(5);
        return;
    }

    if (input == "UpArrow") {
        changeIconThresholdValue(1);
        return;
    }

    if (input == "DownArrow") {
        changeIconThresholdValue(-1);
        return;
    }

    if (input == "Enter" || input == "Escape") {
        closeIconThresholdMenu();
    }
}

static const int WALLPAPER_PICKER_X = 34;
static const int WALLPAPER_PICKER_Y = 34;
static const int WALLPAPER_PICKER_W = 312;
static const int WALLPAPER_PICKER_H = 214;
static const int WALLPAPER_PREVIEW_X = WALLPAPER_PICKER_X + 14;
static const int WALLPAPER_PREVIEW_Y = WALLPAPER_PICKER_Y + 34;
static const int WALLPAPER_PREVIEW_W = 162;
static const int WALLPAPER_PREVIEW_H = 116;
static const int WALLPAPER_LIST_X = WALLPAPER_PICKER_X + 190;
static const int WALLPAPER_LIST_Y = WALLPAPER_PICKER_Y + 34;
static const int WALLPAPER_LIST_W = 106;
static const int WALLPAPER_LIST_ROW_H = 16;
static const int WALLPAPER_COLOR_X = WALLPAPER_PICKER_X + 14;
static const int WALLPAPER_ACTION_Y = WALLPAPER_PICKER_Y + 168;
static const int WALLPAPER_SAVE_X = WALLPAPER_PICKER_X + 128;
static const int WALLPAPER_CANCEL_X = WALLPAPER_PICKER_X + 218;
static const int WALLPAPER_ACTION_W = 76;
static const int WALLPAPER_ACTION_H = 20;

static String wallpaperClip(const String &value, int chars) {
    if (value.length() <= chars) return value;
    if (chars <= 1) return value.substring(0, chars);
    return value.substring(0, chars - 1) + "~";
}

static size_t spiffsFreeBytes() {
    size_t total = SPIFFS.totalBytes();
    size_t used = SPIFFS.usedBytes();
    return total > used ? total - used : 0;
}

static String wallpaperCachePathForIndex(int idx) {
    return String(WALLPAPER_CACHE_PREFIX) + String(idx) + WALLPAPER_CACHE_EXT;
}

static bool wallpaperPathKept(const char *path, const char *keepPrimary, const char *keepSecondary) {
    if (!path || !path[0]) return false;
    return (keepPrimary && strcmp(path, keepPrimary) == 0) ||
           (keepSecondary && strcmp(path, keepSecondary) == 0);
}

static void markWallpaperArtifactsDirty() {
    wallpaperArtifactsDirty = true;
}

static void cleanupWallpaperArtifacts(const char *keepPrimary, const char *keepSecondary) {
    if (!wallpaperArtifactsDirty) return;

    int removed = 0;
    const char *knownArtifacts[] = {
        WALLPAPER_SPIFFS_PATH,
        WALLPAPER_PREVIEW_SPIFFS_PATH,
        WALLPAPER_APPLY_TEMP_PATH,
        WALLPAPER_BACKUP_PATH
    };
    for (const char *path : knownArtifacts) {
        if (!wallpaperPathKept(path, keepPrimary, keepSecondary) && SPIFFS.exists(path)) {
            if (SPIFFS.remove(path)) removed++;
        }
    }
    for (int i = 0; i < WALLPAPER_MAX_ENTRIES; ++i) {
        String cachePath = wallpaperCachePathForIndex(i);
        if (!wallpaperPathKept(cachePath.c_str(), keepPrimary, keepSecondary) && SPIFFS.exists(cachePath)) {
            if (SPIFFS.remove(cachePath)) removed++;
        }
    }

    wallpaperArtifactsDirty = false;
    if (removed > 0) {
        Serial.printf("Wallpaper SPIFFS cleanup removed %d stale file(s).\n", removed);
    }
}

static void cleanupWallpaperArtifactsForRender(const char *renderPath) {
    if (!renderPath || !renderPath[0]) return;
    const char *rollbackPath = strcmp(renderPath, WALLPAPER_SPIFFS_PATH) == 0
        ? nullptr
        : WALLPAPER_SPIFFS_PATH;
    cleanupWallpaperArtifacts(renderPath, rollbackPath);
}

static void removeWallpaperBmpCaches() {
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
}

static void writeWallpaperCacheEntry(File &cache, const String &sdPath) {
    if (!cache || sdPath.length() == 0) return;
    char clippedSD[FE_PATH_MAX];
    strncpy(clippedSD, sdPath.c_str(), FE_PATH_MAX - 1);
    clippedSD[FE_PATH_MAX - 1] = '\0';
    cache.println(clippedSD);
}

static void queueWallpaperPickerPreview(uint16_t delayMs = 80) {
    wallpaperPickerPreviewQueued = true;
    wallpaperPickerPreviewDueMs = millis() + delayMs;
}

static void wallpaperClearEntries() {
    for (int i = 0; i < WALLPAPER_MAX_ENTRIES; ++i) {
        wallpaperEntries[i].name[0] = '\0';
        wallpaperEntries[i].path[0] = '\0';
        wallpaperEntries[i].cachePath[0] = '\0';
    }
    wallpaperEntryCount = 0;
    wallpaperSelected = 0;
    wallpaperScroll = 0;
}

static void wallpaperAddEntry(const String &path, const String &cachePath) {
    if (wallpaperEntryCount >= WALLPAPER_MAX_ENTRIES || path.length() == 0) return;
    String leaf = leafNameFromPath(path);
    strncpy(wallpaperEntries[wallpaperEntryCount].path, path.c_str(), FE_PATH_MAX - 1);
    wallpaperEntries[wallpaperEntryCount].path[FE_PATH_MAX - 1] = '\0';
    strncpy(wallpaperEntries[wallpaperEntryCount].cachePath, cachePath.c_str(), FE_PATH_MAX - 1);
    wallpaperEntries[wallpaperEntryCount].cachePath[FE_PATH_MAX - 1] = '\0';
    strncpy(wallpaperEntries[wallpaperEntryCount].name, leaf.c_str(), WALLPAPER_NAME_MAX - 1);
    wallpaperEntries[wallpaperEntryCount].name[WALLPAPER_NAME_MAX - 1] = '\0';
    wallpaperEntryCount++;
}

static void wallpaperAddEntry(const String &path) {
    String cachePath = (path == WALLPAPER_SD_PATH) ? String(WALLPAPER_SPIFFS_PATH) : "";
    wallpaperAddEntry(path, cachePath);
}

static void loadWallpaperEntriesFromCache() {
    wallpaperClearEntries();
    bool currentFound = false;
    if (SPIFFS.exists(WALLPAPER_NAME_CACHE_PATH)) {
        File cache = SPIFFS.open(WALLPAPER_NAME_CACHE_PATH, FILE_READ);
        if (cache) {
            while (cache.available() && wallpaperEntryCount < WALLPAPER_MAX_ENTRIES) {
                String line = cache.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;

                String cachePath = "";
                String path = line;
                int split = line.indexOf('|');
                if (split >= 0) {
                    cachePath = line.substring(0, split);
                    path = line.substring(split + 1);
                    cachePath.trim();
                    path.trim();
                } else if (path == WALLPAPER_SD_PATH) {
                    cachePath = WALLPAPER_SPIFFS_PATH;
                }

                if (path.length() > 0 && isBMPName(path)) {
                    if (path == WALLPAPER_SD_PATH && SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                        cachePath = WALLPAPER_SPIFFS_PATH;
                        currentFound = true;
                    } else {
                        cachePath = "";
                    }
                    wallpaperAddEntry(path, cachePath);
                }
            }
            cache.close();
        }
    }
    if (!currentFound && SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
        wallpaperAddEntry(WALLPAPER_SD_PATH, WALLPAPER_SPIFFS_PATH);
    }
    for (int i = 0; i < wallpaperEntryCount; ++i) {
        if (String(wallpaperEntries[i].path) == WALLPAPER_SD_PATH) {
            wallpaperSelected = i;
            break;
        }
    }
    wallpaperScroll = constrain(wallpaperSelected - 2, 0, max(0, wallpaperEntryCount - WALLPAPER_LIST_VISIBLE));
}

static void resetWallpaperEntryPreviewPaths() {
    for (int i = 0; i < wallpaperEntryCount; ++i) {
        if (String(wallpaperEntries[i].path) == WALLPAPER_SD_PATH && SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
            strncpy(wallpaperEntries[i].cachePath, WALLPAPER_SPIFFS_PATH, FE_PATH_MAX - 1);
            wallpaperEntries[i].cachePath[FE_PATH_MAX - 1] = '\0';
        } else {
            wallpaperEntries[i].cachePath[0] = '\0';
        }
    }
}

static bool waitForWallpaperControllerFlag(bool &flag, uint32_t timeoutMs) {
    uint32_t started = millis();
    while (!flag && millis() - started < timeoutMs) {
        controller();
        if (wallpaperFetchControllerFailed) return false;
        delay(1);
    }
    return flag;
}

static bool prepareWallpaperFetchVideo() {
    // Discard acknowledgements left from the previous handoff before starting
    // a new transaction. VDFETCHPREP is idempotent on the controller, so it
    // also recovers an interrupted fetch without an extra END round trip.
    while (serialDisplay.available()) controller();
    wallpaperFetchControllerReady = false;
    wallpaperFetchControllerOff = false;
    wallpaperFetchControllerFailed = false;

    for (int attempt = 1; attempt <= 3; ++attempt) {
        wallpaperFetchControllerReady = false;
        wallpaperFetchControllerFailed = false;
        Serial.printf("Preparing SerialController fetch screen, attempt %d/3.\n", attempt);
        bootVideoSerial.fetchFilesPrepare();
        if (waitForWallpaperControllerFlag(wallpaperFetchControllerReady, 4000)) return true;
        if (wallpaperFetchControllerFailed) {
            Serial.println("SerialController reported fetch-screen initialization failure.");
        } else {
            Serial.println("SerialController fetch-screen acknowledgement timed out.");
        }
        bootVideoSerial.fetchFilesEnd();
        delay(80);
    }

    Serial.println("Wallpaper fetch aborted: SerialController did not prepare CVBS after retries.");
    return false;
}

static bool stageWallpaperPreviewFromSD(const char *sdPath) {
    const uint32_t mountFrequencies[] = { 4000000, 2000000, 1000000 };
    const size_t expectedPreviewSize = 54U +
        (size_t)(((WALLPAPER_PREVIEW_CACHE_W * 3) + 3) & ~3) * WALLPAPER_PREVIEW_CACHE_H;
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    SDInUse = true;

    for (int transaction = 0; transaction < 2; ++transaction) {
        markWallpaperArtifactsDirty();
        cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
        Citadela::SDStats sdStats;
        Serial.printf("Wallpaper preview SD transaction %d/2.\n", transaction + 1);
        bool mounted = Citadela::Storage::beginBoardSDWithRetry(
            SD, SPI,
            mountFrequencies,
            sizeof(mountFrequencies) / sizeof(mountFrequencies[0]),
            &sdStats,
            &Serial);
        if (!mounted) {
            Serial.println("Wallpaper preview failed at mount.");
            delay(25);
            continue;
        }

        if (spiffsFreeBytes() < expectedPreviewSize + WALLPAPER_CACHE_MIN_FREE) {
            Serial.printf("Wallpaper preview failed: need %u SPIFFS bytes, have %u.\n",
                          (unsigned)(expectedPreviewSize + WALLPAPER_CACHE_MIN_FREE),
                          (unsigned)spiffsFreeBytes());
            Citadela::Storage::resetBoardSDBus(SD, SPI);
            SDInUse = false;
            return false;
        }

        bool generated = Citadela::WallpaperPreviewCache::create24BitBMP(
            SD,
            sdPath,
            SPIFFS,
            WALLPAPER_PREVIEW_SPIFFS_PATH,
            WALLPAPER_PREVIEW_CACHE_W,
            WALLPAPER_PREVIEW_CACHE_H,
            &Serial);
        Citadela::Storage::resetBoardSDBus(SD, SPI);

        File preview = SPIFFS.open(WALLPAPER_PREVIEW_SPIFFS_PATH, FILE_READ);
        size_t previewSize = preview ? preview.size() : 0;
        if (preview) preview.close();
        bool verified = generated && previewSize == expectedPreviewSize;
        Serial.printf("Wallpaper preview verification: stored=%u expected=%u.\n",
                      (unsigned)previewSize, (unsigned)expectedPreviewSize);
        if (verified) {
            markWallpaperArtifactsDirty();
            cleanupWallpaperArtifacts(WALLPAPER_PREVIEW_SPIFFS_PATH, WALLPAPER_SPIFFS_PATH);
            SDInUse = false;
            return true;
        }

        if (transaction == 0) {
            size_t freeBeforeGC = spiffsFreeBytes();
            esp_err_t gcResult = esp_spiffs_gc(nullptr, expectedPreviewSize + 4096U);
            Serial.printf("Wallpaper preview SPIFFS GC: result=%d free=%u->%u.\n",
                          (int)gcResult,
                          (unsigned)freeBeforeGC,
                          (unsigned)spiffsFreeBytes());
        }
        delay(25);
    }

    SDInUse = false;
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    Serial.println("Wallpaper preview exhausted both full SD transactions.");
    return false;
}

static bool stageFullWallpaperFromSD(const char *sdPath) {
    const uint32_t mountFrequencies[] = { 4000000, 2000000, 1000000 };
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    SDInUse = true;
    for (int transaction = 0; transaction < 2; ++transaction) {
        Citadela::SDStats sdStats;
        Serial.printf("Wallpaper SD transaction %d/2.\n", transaction + 1);
        bool mounted = Citadela::Storage::beginBoardSDWithRetry(
            SD, SPI,
            mountFrequencies,
            sizeof(mountFrequencies) / sizeof(mountFrequencies[0]),
            &sdStats,
            &Serial);
        if (!mounted) {
            Serial.println("Wallpaper fetch failed at mount: card did not answer after bus reset and speed fallback.");
            delay(25);
            continue;
        }

        File source = SD.open(sdPath, FILE_READ);
        size_t sourceSize = source ? source.size() : 0;
        uint8_t signature[2] = {0, 0};
        bool sourceValid = source && sourceSize >= 54 &&
                           source.read(signature, sizeof(signature)) == sizeof(signature) &&
                           signature[0] == 'B' && signature[1] == 'M';
        if (source) source.close();
        if (!sourceValid) {
            Serial.print("Wallpaper fetch failed at source validation: ");
            Serial.println(sdPath);
            Citadela::Storage::resetBoardSDBus(SD, SPI);
            delay(25);
            continue;
        }

        if (spiffsFreeBytes() < sourceSize + WALLPAPER_STAGING_HEADROOM) {
            Serial.println("Releasing wallpaper raw cache to give SPIFFS staging/GC headroom.");
            removeWallpaperRawCache();
            delay(20);
        }
        if (spiffsFreeBytes() < sourceSize + WALLPAPER_CACHE_MIN_FREE) {
            Serial.printf("Wallpaper fetch failed: need %u SPIFFS bytes, have %u.\n",
                          (unsigned)(sourceSize + WALLPAPER_CACHE_MIN_FREE),
                          (unsigned)spiffsFreeBytes());
            Citadela::Storage::resetBoardSDBus(SD, SPI);
            SDInUse = false;
            return false;
        }

        Serial.printf("Wallpaper staging source=%u bytes, SPIFFS free=%u bytes.\n",
                      (unsigned)sourceSize, (unsigned)spiffsFreeBytes());

        SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
        bool copied = Citadela::Storage::copyFile(
            SD, sdPath, SPIFFS, WALLPAPER_APPLY_TEMP_PATH, 8192, &Serial);
        Citadela::Storage::resetBoardSDBus(SD, SPI);
        if (!copied) {
            Serial.printf("Wallpaper fetch failed at copy: SPIFFS free now=%u bytes.\n",
                          (unsigned)spiffsFreeBytes());
            SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
            delay(25);
            continue;
        }

        File staged = SPIFFS.open(WALLPAPER_APPLY_TEMP_PATH, FILE_READ);
        size_t stagedSize = staged ? staged.size() : 0;
        bool verified = staged && stagedSize == sourceSize;
        if (staged) staged.close();
        Serial.printf("Wallpaper staging verification: stored=%u expected=%u.\n",
                      (unsigned)stagedSize, (unsigned)sourceSize);
        if (!verified) {
            SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
            Serial.print("Wallpaper fetch failed at staged-file verification for ");
            Serial.println(sdPath);
            delay(25);
            continue;
        }

        SDInUse = false;
        markWallpaperArtifactsDirty();
        cleanupWallpaperArtifacts(WALLPAPER_APPLY_TEMP_PATH, WALLPAPER_SPIFFS_PATH);
        Serial.printf("Wallpaper fetched from SD to SPIFFS staging: %s (%u bytes)\n",
                      sdPath, (unsigned)sourceSize);
        return true;
    }

    SDInUse = false;
    SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    Serial.println("Wallpaper fetch exhausted both full SD transactions.");
    return false;
}

static bool resumeKernelVideoAfterWallpaperFetch() {
    wallpaperFetchControllerOff = false;
    bootVideoSerial.fetchFilesEnd();
    if (!waitForWallpaperControllerFlag(wallpaperFetchControllerOff, 1500)) {
        Serial.println("SerialController handoff acknowledgement timed out; resuming kernel video.");
    }

    Serial.printf("DMA heap before wallpaper video resume: free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    bool videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
    initVd = videoReady;
    if (!videoReady) {
        Serial.println("Wallpaper fetch failed: kernel video could not be reinitialized.");
        bootVideoSerial.fetchFilesStart();
        return false;
    }

    CitaCursor.Begin(videodisplay, SCREEN_WIDTH, SCREEN_HEIGHT, &mouseCursorOutlined);
    CitaCursor.SetCallbacks(kernelCursorUpdateHover, kernelCursorActivateClick);
    CitaCursor.MoveMouseTo(wallpaperFetchCursorX, wallpaperFetchCursorY, wallpaperFetchCursorButtons);
    bootVideoSerial.fetchFilesKernelReady();
    Serial.printf("DMA heap after wallpaper video resume: free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return true;
}

static bool fetchWallpaperWithVideoHandoff(const char *selectedPath, bool previewOnly) {
    wallpaperFetchDisplayReinitialized = false;
    if (!selectedPath || !selectedPath[0]) return false;
    if (!prepareWallpaperFetchVideo()) return false;

    kernelCursorRestore();
    wallpaperFetchCursorX = CitaCursor.X();
    wallpaperFetchCursorY = CitaCursor.Y();
    wallpaperFetchCursorButtons = CitaCursor.Buttons();
    CitaCursor.Enable(false);
    bool resumeMirror = serialDisplay.enabled();
    serialDisplay.flush();
    serialDisplay.disable();
    Serial.printf("DMA heap before wallpaper video release: free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    videodisplay.releaseVideoMemory();
    initVd = false;
    pinMode(25, INPUT_PULLDOWN);
    Serial.printf("DMA heap after wallpaper video release: free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

    bootVideoSerial.fetchFilesStart();
    delay(60);
    bool fetched = previewOnly
        ? stageWallpaperPreviewFromSD(selectedPath)
        : stageFullWallpaperFromSD(selectedPath);
    bool videoReady = resumeKernelVideoAfterWallpaperFetch();
    if (!videoReady) return false;
    if (resumeMirror) {
        serialDisplay.enable(false);
        serialDisplayScenePending = serialDisplayEnabled && serialDisplayConnected;
    }
    wallpaperFetchDisplayReinitialized = true;

    return fetched;
}

static bool fetchSelectedWallpaperForPreview() {
    if (wallpaperEntryCount <= 0) return false;
    const char *selectedPath = wallpaperEntries[wallpaperSelected].path;
    bool fetched = fetchWallpaperWithVideoHandoff(selectedPath, true);

    resetWallpaperEntryPreviewPaths();
    if (fetched) {
        strncpy(wallpaperEntries[wallpaperSelected].cachePath,
                WALLPAPER_PREVIEW_SPIFFS_PATH,
                FE_PATH_MAX - 1);
        wallpaperEntries[wallpaperSelected].cachePath[FE_PATH_MAX - 1] = '\0';
    }
    return fetched;
}

static bool fetchSelectedWallpaperForApply() {
    if (wallpaperEntryCount <= 0) return false;
    const char *selectedPath = wallpaperEntries[wallpaperSelected].path;
    bool fetched = fetchWallpaperWithVideoHandoff(selectedPath, false);
    resetWallpaperEntryPreviewPaths();
    return fetched;
}

static bool promoteStagedWallpaper() {
    if (!SPIFFS.exists(WALLPAPER_APPLY_TEMP_PATH)) return false;
    SPIFFS.remove(WALLPAPER_BACKUP_PATH);

    bool hadActive = SPIFFS.exists(WALLPAPER_SPIFFS_PATH);
    if (hadActive && !SPIFFS.rename(WALLPAPER_SPIFFS_PATH, WALLPAPER_BACKUP_PATH)) {
        Serial.println("Could not preserve the active wallpaper before applying the new one.");
        markWallpaperArtifactsDirty();
        cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
        return false;
    }
    if (!SPIFFS.rename(WALLPAPER_APPLY_TEMP_PATH, WALLPAPER_SPIFFS_PATH)) {
        if (hadActive) SPIFFS.rename(WALLPAPER_BACKUP_PATH, WALLPAPER_SPIFFS_PATH);
        Serial.println("Could not promote the staged wallpaper.");
        markWallpaperArtifactsDirty();
        cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
        return false;
    }

    SPIFFS.remove(WALLPAPER_BACKUP_PATH);
    invalidateWallpaperRenderCache();
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    return true;
}

static const char *wallpaperPreviewPathForSelected() {
    if (wallpaperEntryCount <= 0) return nullptr;
    const char *cachePath = wallpaperEntries[wallpaperSelected].cachePath;
    if (cachePath && cachePath[0] && SPIFFS.exists(cachePath)) {
        wallpaperPickerPreviewReady = true;
        return cachePath;
    }
    wallpaperPickerPreviewReady = false;
    return nullptr;
}

static void drawWallpaperPickerList() {
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(WALLPAPER_LIST_X, WALLPAPER_LIST_Y, WALLPAPER_LIST_W, WALLPAPER_LIST_VISIBLE * WALLPAPER_LIST_ROW_H, black);
    videodisplay.rect(WALLPAPER_LIST_X - 1, WALLPAPER_LIST_Y - 1,
                      WALLPAPER_LIST_W + 2, WALLPAPER_LIST_VISIBLE * WALLPAPER_LIST_ROW_H + 2, white);
    videodisplay.setFont(Font6x8);
    for (int row = 0; row < WALLPAPER_LIST_VISIBLE; ++row) {
        int idx = wallpaperScroll + row;
        int y = WALLPAPER_LIST_Y + row * WALLPAPER_LIST_ROW_H;
        bool selected = idx == wallpaperSelected;
        uint32_t bg = selected ? white : black;
        uint32_t fg = selected ? black : white;
        videodisplay.fillRect(WALLPAPER_LIST_X, y, WALLPAPER_LIST_W, WALLPAPER_LIST_ROW_H - 1, bg);
        if (idx < wallpaperEntryCount) {
            String label = wallpaperClip(basenameNoExt(String(wallpaperEntries[idx].name)), 15);
            videodisplay.setTextColor(fg, bg);
            videodisplay.setCursor(WALLPAPER_LIST_X + 4, y + 4);
            videodisplay.print(label.c_str());
        }
    }
}

static void drawWallpaperPickerActions() {
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    auto drawAction = [&](int x, int w, const char *label, bool active) {
        uint32_t bg = active ? white : black;
        uint32_t fg = active ? black : white;
        videodisplay.fillRect(x, WALLPAPER_ACTION_Y, w, WALLPAPER_ACTION_H, bg);
        videodisplay.rect(x, WALLPAPER_ACTION_Y, w, WALLPAPER_ACTION_H, white);
        videodisplay.setTextColor(fg, bg);
        videodisplay.setCursor(x + 6, WALLPAPER_ACTION_Y + 6);
        videodisplay.print(label);
    };
    drawAction(WALLPAPER_COLOR_X, 96, wallpaperPickerColorEnabled ? "Color ON" : "Color OFF", wallpaperPickerColorEnabled);
    drawAction(WALLPAPER_SAVE_X, WALLPAPER_ACTION_W, "Save", false);
    drawAction(WALLPAPER_CANCEL_X, WALLPAPER_ACTION_W, "Cancel", false);
}

static void drawWallpaperPickerPreviewMessage(const char *line1, const char *line2 = nullptr) {
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(WALLPAPER_PREVIEW_X, WALLPAPER_PREVIEW_Y, WALLPAPER_PREVIEW_W, WALLPAPER_PREVIEW_H, black);
    videodisplay.rect(WALLPAPER_PREVIEW_X - 1, WALLPAPER_PREVIEW_Y - 1, WALLPAPER_PREVIEW_W + 2, WALLPAPER_PREVIEW_H + 2, white);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, black);
    if (line1 && line1[0]) {
        videodisplay.setCursor(WALLPAPER_PREVIEW_X + 16, WALLPAPER_PREVIEW_Y + 50);
        videodisplay.print(line1);
    }
    if (line2 && line2[0]) {
        videodisplay.setCursor(WALLPAPER_PREVIEW_X + 16, WALLPAPER_PREVIEW_Y + 62);
        videodisplay.print(line2);
    }
}

static void drawWallpaperPickerPreview() {
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(WALLPAPER_PREVIEW_X, WALLPAPER_PREVIEW_Y, WALLPAPER_PREVIEW_W, WALLPAPER_PREVIEW_H, black);
    videodisplay.rect(WALLPAPER_PREVIEW_X - 1, WALLPAPER_PREVIEW_Y - 1, WALLPAPER_PREVIEW_W + 2, WALLPAPER_PREVIEW_H + 2, white);

    if (wallpaperEntryCount <= 0) {
        drawWallpaperPickerPreviewMessage("No wallpapers");
        return;
    }

    bool oldMono = bmpMonochrome;
    bmpMonochrome = !wallpaperPickerColorEnabled;
    const char *previewPath = wallpaperPreviewPathForSelected();
    bool rendered = previewPath && renderBMPScaledRectFromSPIFFS(previewPath,
                                                                 WALLPAPER_PREVIEW_X + 1,
                                                                 WALLPAPER_PREVIEW_Y + 1,
                                                                 WALLPAPER_PREVIEW_W - 2,
                                                                 WALLPAPER_PREVIEW_H - 2);
    bmpMonochrome = !wallpaperPickerColorEnabled;
    if (!rendered) {
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(white, black);
        videodisplay.setCursor(WALLPAPER_PREVIEW_X + 16, WALLPAPER_PREVIEW_Y + 46);
        videodisplay.print("Preview failed");
        videodisplay.setCursor(WALLPAPER_PREVIEW_X + 16, WALLPAPER_PREVIEW_Y + 58);
        videodisplay.print("Not cached");
    }
    bmpMonochrome = oldMono;
}

static void serviceWallpaperPickerPreview() {
    if (!wallpaperPickerPending || !wallpaperPickerPreviewQueued) return;
    if ((int32_t)(millis() - wallpaperPickerPreviewDueMs) < 0) return;
    wallpaperPickerPreviewQueued = false;
    KernelCursorDrawGuard cursorGuard;
    drawWallpaperPickerPreview();
}

static void drawWallpaperPickerWindow() {
    kernelCursorRestore();
    uint32_t black = videodisplay.RGB(0, 0, 0);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(WALLPAPER_PICKER_X, WALLPAPER_PICKER_Y, WALLPAPER_PICKER_W, WALLPAPER_PICKER_H, black);
    videodisplay.rect(WALLPAPER_PICKER_X, WALLPAPER_PICKER_Y, WALLPAPER_PICKER_W, WALLPAPER_PICKER_H, white);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(white, black);
    videodisplay.setCursor(WALLPAPER_PICKER_X + 92, WALLPAPER_PICKER_Y + 12);
    videodisplay.print("Wallpaper");
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(WALLPAPER_PREVIEW_X, WALLPAPER_PICKER_Y + 24);
    videodisplay.print("Preview");
    videodisplay.setCursor(WALLPAPER_LIST_X, WALLPAPER_PICKER_Y + 24);
    videodisplay.print("Wallpapers");
    drawWallpaperPickerPreviewMessage("Loading preview");
    drawWallpaperPickerList();
    drawWallpaperPickerActions();
    queueWallpaperPickerPreview(10);
    kernelCursorRefreshAfterRedraw();
}

static void wallpaperPickerSelect(int idx) {
    if (wallpaperEntryCount <= 0) return;
    idx = constrain(idx, 0, wallpaperEntryCount - 1);
    if (idx == wallpaperSelected) return;
    wallpaperSelected = idx;
    if (wallpaperSelected < wallpaperScroll) wallpaperScroll = wallpaperSelected;
    if (wallpaperSelected >= wallpaperScroll + WALLPAPER_LIST_VISIBLE) {
        wallpaperScroll = wallpaperSelected - WALLPAPER_LIST_VISIBLE + 1;
    }
    wallpaperScroll = constrain(wallpaperScroll, 0, max(0, wallpaperEntryCount - WALLPAPER_LIST_VISIBLE));
    wallpaperPickerDirty = true;
    KernelCursorDrawGuard cursorGuard;
    drawWallpaperPickerList();
    if (wallpaperPreviewPathForSelected()) {
        drawWallpaperPickerPreviewMessage("Loading preview");
        queueWallpaperPickerPreview();
        return;
    }

    drawWallpaperPickerPreviewMessage("Fetching files");
    wallpaperFetchInProgress = true;
    bool fetched = fetchSelectedWallpaperForPreview();
    wallpaperFetchInProgress = false;

    if (wallpaperFetchDisplayReinitialized && initVd) {
        videodisplay.clear(0);
        applaunched = true;
        wallpaperPickerPending = true;
        drawWallpaperPickerWindow();
        wallpaperPickerPreviewQueued = false;
        KernelCursorDrawGuard previewCursorGuard;
        if (fetched) drawWallpaperPickerPreview();
        else drawWallpaperPickerPreviewMessage("Fetch failed", "Check SD card");
    } else if (!fetched && initVd) {
        drawWallpaperPickerPreviewMessage("Controller", "not ready");
    }
}

static void wallpaperPickerMove(int delta) {
    if (wallpaperEntryCount <= 0) return;
    int idx = wallpaperSelected + delta;
    if (idx < 0) idx = wallpaperEntryCount - 1;
    if (idx >= wallpaperEntryCount) idx = 0;
    wallpaperPickerSelect(idx);
}

static void wallpaperPickerToggleColor() {
    wallpaperPickerColorEnabled = !wallpaperPickerColorEnabled;
    wallpaperPickerDirty = true;
    KernelCursorDrawGuard cursorGuard;
    drawWallpaperPickerActions();
    drawWallpaperPickerPreviewMessage("Loading preview");
    queueWallpaperPickerPreview();
}

static void closeWallpaperPickerMenu(bool saveSelection) {
    if (saveSelection && wallpaperEntryCount > 0) {
        String selectedPath = wallpaperEntries[wallpaperSelected].path;
        bool wallpaperChanged = selectedPath != WALLPAPER_SD_PATH;
        if (wallpaperChanged) {
            drawWallpaperPickerPreviewMessage("Saving wallpaper", "Fetching full file");
            wallpaperFetchInProgress = true;
            bool fetched = fetchSelectedWallpaperForApply();
            wallpaperFetchInProgress = false;

            if (!fetched) {
                if (wallpaperFetchDisplayReinitialized && initVd) {
                    videodisplay.clear(0);
                    applaunched = true;
                    wallpaperPickerPending = true;
                    drawWallpaperPickerWindow();
                    wallpaperPickerPreviewQueued = false;
                    drawWallpaperPickerPreviewMessage("Full fetch failed", "Wallpaper kept");
                } else if (initVd) {
                    drawWallpaperPickerPreviewMessage("Controller", "not ready");
                }
                return;
            }
            if (!promoteStagedWallpaper()) {
                if (wallpaperFetchDisplayReinitialized && initVd) {
                    videodisplay.clear(0);
                    drawWallpaperPickerWindow();
                    wallpaperPickerPreviewQueued = false;
                }
                drawWallpaperPickerPreviewMessage("Apply failed", "Wallpaper kept");
                return;
            }
        }

        WALLPAPER_SD_PATH = selectedPath;
        bmpMonochrome = !wallpaperPickerColorEnabled;
        invalidateWallpaperRenderCache();
        writeSystemConfigToSPIFFS();
        configurationDirty = false;
        wallpaperPickerPending = false;
        wallpaperPickerDirty = false;
        wallpaperPickerPreviewQueued = false;
        SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
        SPIFFS.remove(WALLPAPER_BACKUP_PATH);
        markWallpaperArtifactsDirty();
        cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
        resetWallpaperEntryPreviewPaths();

        home();
        configureMenu(14);
        return;
    }

    WALLPAPER_SD_PATH = wallpaperPickerOriginalPath.length() ? wallpaperPickerOriginalPath : WALLPAPER_SD_PATH;
    bmpMonochrome = wallpaperPickerOriginalMonochrome;
    wallpaperPickerPending = false;
    wallpaperPickerDirty = false;
    wallpaperPickerPreviewQueued = false;
    SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
    SPIFFS.remove(WALLPAPER_BACKUP_PATH);
    markWallpaperArtifactsDirty();
    cleanupWallpaperArtifacts(WALLPAPER_SPIFFS_PATH);
    kernelCursorRestore();
    if (!SPIFFS.exists(WALLPAPER_RAW_CACHE_PATH)) {
        applaunched = false;
        home();
    } else {
        restoreDesktopRect(WALLPAPER_PICKER_X, WALLPAPER_PICKER_Y,
                           WALLPAPER_PICKER_W, WALLPAPER_PICKER_H);
    }
    configureMenu(14);
}

void openWallpaperPickerMenu() {
    if (kernelWindow.active) restoreAndCloseKernelWindow();
    configurationPending = false;
    manualTimePending = false;
    iconThresholdPending = false;
    wallpaperPickerPending = false;
    wallpaperPickerDirty = false;
    wallpaperPickerPreviewQueued = false;
    wallpaperPickerOriginalPath = WALLPAPER_SD_PATH;
    wallpaperPickerOriginalMonochrome = bmpMonochrome;
    wallpaperPickerColorEnabled = !bmpMonochrome;
    SPIFFS.remove(WALLPAPER_APPLY_TEMP_PATH);
    SPIFFS.remove(WALLPAPER_BACKUP_PATH);
    selection = false;
    loadWallpaperEntriesFromCache();
    wallpaperPickerPending = true;
    applaunched = true;
    drawWallpaperPickerWindow();
}

void handleWallpaperPickerInput(String input) {
    input.trim();
    if (wallpaperFetchInProgress) return;
    if (input.length() == 0 || input.indexOf("rlsd") != -1) return;
    if (input == "Escape") {
        closeWallpaperPickerMenu(false);
    } else if (input == "UpArrow") {
        wallpaperPickerMove(-1);
    } else if (input == "DownArrow") {
        wallpaperPickerMove(1);
    } else if (input == "PageUp") {
        wallpaperPickerMove(-WALLPAPER_LIST_VISIBLE);
    } else if (input == "PageDown") {
        wallpaperPickerMove(WALLPAPER_LIST_VISIBLE);
    } else if (input == "LeftArrow" || input == "RightArrow" || input == "c" || input == "C") {
        wallpaperPickerToggleColor();
    } else if (input == "Enter") {
        closeWallpaperPickerMenu(true);
    }
}

static void redrawConfigurationChange(int item);

static void activateConfigurationItem(int item) {
    switch (configurationswitch(item)) {
        case 1: WallpaperToggle = !WallpaperToggle; configurationDirty = true; redrawConfigurationChange(1); break;
        case 2: TooltipsToggle = !TooltipsToggle; configurationDirty = true; redrawConfigurationChange(2); break;
        case 3: VerboseUART = !VerboseUART; configurationDirty = true; redrawConfigurationChange(3); break;
        case 4: DisplayColour = !DisplayColour; configurationDirty = true; redrawConfigurationChange(4); break;
        case 5: WifiCoreToggle = !WifiCoreToggle; configurationDirty = true; redrawConfigurationChange(5); break;
        case 6: TelemetryData = !TelemetryData; configurationDirty = true; redrawConfigurationChange(6); break;
        case 7: FastBoot = !FastBoot; configurationDirty = true; redrawConfigurationChange(7); break;
        case 8: AudioDriver = !AudioDriver; configurationDirty = true; redrawConfigurationChange(8); break;
        case 9: setInvertColors(!invertColors); configurationDirty = true; redrawConfigurationChange(9); break;
        case 10: openManualTimeMenu(); break;
        case 11: openIconThresholdMenu(false); break;
        case 12: openIconThresholdMenu(true); break;
        case 13: mouseCursorOutlined = !mouseCursorOutlined; configurationDirty = true; redrawConfigurationChange(13); break;
        case 14: openWallpaperPickerMenu(); break;
        case 15: openUARTUploadMenu(); break;
        case 16:
            serialDisplayEnabled = !serialDisplayEnabled;
            writeSystemConfigToSPIFFS();
            if (!serialDisplayEnabled) {
                serialDisplay.disable();
            } else if (serialDisplayConnected) serialDisplayScenePending = true;
            redrawConfigurationChange(16);
            if (!serialDisplayEnabled) Serial1.println("VDM DISABLED");
            break;
        case 17: openBluetoothConnectionMenu(); break;
        case 18: openWiFiConnectionMenu(); break;
    }
}

static void closeConfigurationPanelFromInput() {
    if (configurationDirty) {
        restoreAndCloseKernelWindow();
        requestElevation("Save changes and restart?", 2);
        fE = false;
        util = false;
        applaunched = false;
        configurationPending = false;
    } else {
        fE = false;
        util = false;
        applaunched = false;
        configurationPending = false;
        if (!restoreAndCloseKernelWindow()) home();
    }
}

static void handleConfigurationInput(String input) {
    input.trim();
    if (input.length() == 0 || input.indexOf("rlsd") != -1) return;
    if (uartUploadPending) {
        handleUARTUploadInput(input);
        return;
    }

    if (input == "Escape") {
        closeConfigurationPanelFromInput();
    } else if (input == "UpArrow") {
        if (dragvalve > 1) dragvalve--;
    } else if (input == "DownArrow") {
        if (dragvalve < CONFIG_ITEM_COUNT) dragvalve++;
    } else if (input == "PageUp") {
        dragvalve = max(1, dragvalve - CONFIG_PAGE_ROWS);
    } else if (input == "PageDown") {
        dragvalve = min(CONFIG_ITEM_COUNT, dragvalve + CONFIG_PAGE_ROWS);
    } else if (input == "Enter") {
        activateConfigurationItem(dragvalve);
    }
}

static bool configItemGeometry(int item, int &x, int &y, int &w, int &h) {
    if (!kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_CONFIG ||
        item < 1 || item > CONFIG_ITEM_COUNT) return false;
    const int columns = 2;
    const int rows = CONFIG_PAGE_ROWS;
    const int gapX = 8;
    const int gapY = 3;
    const int marginX = 10;
    const int marginY = 7;
    w = (kernelWindowClientW() - marginX * 2 - gapX) / columns;
    h = 19;
    int column = (item - 1) / rows;
    int row = (item - 1) % rows;
    x = kernelWindowClientX() + marginX + column * (w + gapX);
    y = kernelWindowClientY() + marginY + row * (h + gapY);
    return true;
}

static void drawConfigurationItem(int item) {
    int x = 0, y = 0, w = 0, h = 0;
    if (!configItemGeometry(item, x, y, w, h)) return;
    bool selected = item == dragvalve;
    int configId = item - 1;
    bool toggleItem = configId <= 8 || configId == 12 || configId == 15;
    uint32_t bg = selected ? videodisplay.RGB(32, 113, 145) : videodisplay.RGB(31, 42, 48);
    uint32_t fg = videodisplay.RGB(245, 245, 245);
    uint32_t edge = selected ? videodisplay.RGB(86, 224, 211) : videodisplay.RGB(88, 116, 126);
    videodisplay.fillRect(x, y, w, h, bg);
    videodisplay.rect(x, y, w, h, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(fg, bg);
    videodisplay.setCursor(x + 5, y + 6);
    videodisplay.print(configThrow(configId).c_str());
    if (toggleItem) {
        bool enabled = configEnabled(configId);
        const char *state = enabled ? "ON" : "OFF";
        uint32_t stateBg = enabled ? videodisplay.RGB(31, 151, 91) : videodisplay.RGB(111, 72, 78);
        int stateW = enabled ? 20 : 26;
        videodisplay.fillRect(x + w - stateW - 3, y + 3, stateW, 13, stateBg);
        videodisplay.setTextColor(videodisplay.RGB(255, 255, 255), stateBg);
        videodisplay.setCursor(x + w - stateW, y + 6);
        videodisplay.print(state);
    } else {
        videodisplay.setTextColor(videodisplay.RGB(248, 193, 72), bg);
        videodisplay.setCursor(x + w - 10, y + 6);
        videodisplay.print(">");
    }
}

static void drawConfigurationWindowContent() {
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t dim = videodisplay.RGB(132, 146, 146);
    videodisplay.fillRect(kernelWindowClientX(), kernelWindowClientY(),
                          kernelWindowClientW(), kernelWindowClientH(), panel);
    for (int item = 1; item <= CONFIG_ITEM_COUNT; ++item) drawConfigurationItem(item);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(kernelWindowClientX() + 12, kernelWindow.y + kernelWindow.h - 14);
    String footer = uartUploadStatus.length()
        ? uartUploadStatus
        : (configurationDirty ? "Unsaved changes" : "System preferences");
    if (footer.length() > 45) footer = footer.substring(0, 45);
    videodisplay.print(footer.c_str());
}

static void redrawConfigurationChange(int item) {
    KernelCursorDrawGuard cursorGuard;
    drawConfigurationItem(item);
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t dim = videodisplay.RGB(132, 146, 146);
    int footerY = kernelWindow.y + kernelWindow.h - 16;
    videodisplay.fillRect(kernelWindowClientX() + 8, footerY,
                          kernelWindowClientW() - 16, 12, panel);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(kernelWindowClientX() + 12, footerY + 2);
    videodisplay.print("Unsaved changes");
}

void configureMenu(int snapDrag = 1) {
    configurationPending = true;
    applaunched = true;
    util = false;
    fE = false;
    shell = false;
    dragvalve = snapDrag;
    previousDragValve = -1;
    openKernelWindow(KERNEL_WINDOW_CONFIG, "Configuration", 312, 244, drawConfigurationWindowContent);
}

static int connectionListX() { return kernelWindowClientX() + 10; }
static int connectionListY() { return kernelWindowClientY() + 28; }
static int connectionListW() { return kernelWindowClientW() - 20; }
static const int CONNECTION_ROW_H = 18;
static int connectionActionY() { return kernelWindow.y + kernelWindow.h - 27; }

static void connectionCopyName(char *destination, const String &source) {
    if (!destination) return;
    int out = 0;
    for (int i = 0; i < source.length() && out < CONNECTION_NAME_MAX - 1; ++i) {
        char c = source.charAt(i);
        destination[out++] = (c >= 32 && c <= 126) ? c : '?';
    }
    destination[out] = '\0';
}

static void drawConnectionStatus() {
    if (!kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_CONNECTION) return;
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t dim = videodisplay.RGB(151, 177, 184);
    uint32_t accent = videodisplay.RGB(72, 219, 202);
    int x = kernelWindowClientX() + 10;
    int y = connectionStage == CONNECTION_STAGE_LIST
        ? connectionListY() + CONNECTION_VISIBLE_ROWS * CONNECTION_ROW_H + 3
        : kernelWindowClientY() + 130;
    int w = kernelWindowClientW() - 20;
    videodisplay.fillRect(x, y, w, 12, panel);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(connectionScanning ? accent : dim, panel);
    videodisplay.setCursor(x + 2, y + 2);
    String status = connectionStatus;
    int maxChars = max(1, (w - 4) / 6);
    if (status.length() > maxChars) status = status.substring(0, maxChars);
    videodisplay.print(status.c_str());
}

static void drawConnectionButton(int x, int y, int w, const char *label, bool enabled = true) {
    uint32_t bg = enabled ? videodisplay.RGB(35, 105, 133) : videodisplay.RGB(42, 50, 54);
    uint32_t edge = enabled ? videodisplay.RGB(80, 223, 207) : videodisplay.RGB(90, 102, 107);
    uint32_t fg = enabled ? videodisplay.RGB(255, 255, 255) : videodisplay.RGB(139, 149, 153);
    videodisplay.fillRect(x, y, w, 18, bg);
    videodisplay.rect(x, y, w, 18, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(fg, bg);
    int textW = strlen(label) * 6;
    videodisplay.setCursor(x + max(3, (w - textW) / 2), y + 5);
    videodisplay.print(label);
}

static void drawConnectionActions() {
    int x = kernelWindowClientX() + 10;
    int y = connectionActionY();
    int w = kernelWindowClientW() - 20;
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    videodisplay.fillRect(x, y, w, 19, panel);
    if (connectionStage == CONNECTION_STAGE_LIST) {
        drawConnectionButton(x, y, 66, "Rescan", !connectionScanning);
        drawConnectionButton(x + w - 72, y, 72, "Connect", connectionEntryCount > 0);
    } else {
        drawConnectionButton(x, y, 58, "Back", connectionStage != CONNECTION_STAGE_CONNECTING);
        drawConnectionButton(x + w - 72, y, 72, "Connect", connectionStage != CONNECTION_STAGE_CONNECTING);
    }
}

static void drawConnectionRow(int row) {
    if (row < 0 || row >= CONNECTION_VISIBLE_ROWS) return;
    int idx = connectionScroll + row;
    int x = connectionListX() + 1;
    int y = connectionListY() + row * CONNECTION_ROW_H + 1;
    int w = connectionListW() - 2;
    uint32_t empty = videodisplay.RGB(19, 26, 30);
    if (idx < 0 || idx >= connectionEntryCount) {
        videodisplay.fillRect(x, y, w, CONNECTION_ROW_H - 1, empty);
        return;
    }

    bool selected = idx == connectionSelected;
    uint32_t bg = selected ? videodisplay.RGB(31, 112, 143) : empty;
    uint32_t fg = videodisplay.RGB(246, 248, 248);
    uint32_t indexColor = selected ? videodisplay.RGB(119, 246, 230) : videodisplay.RGB(96, 155, 166);
    videodisplay.fillRect(x, y, w, CONNECTION_ROW_H - 1, bg);
    if (selected) videodisplay.fillRect(x, y, 3, CONNECTION_ROW_H - 1, videodisplay.RGB(83, 232, 210));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(indexColor, bg);
    videodisplay.setCursor(x + 7, y + 5);
    videodisplay.print(String(idx + 1).c_str());

    String name = connectionEntries[idx].name;
    int reserved = wifiConnectionPending ? 74 : 34;
    int maxChars = max(1, (w - reserved) / 6);
    if (name.length() > maxChars) name = name.substring(0, maxChars - 1) + "~";
    videodisplay.setTextColor(fg, bg);
    videodisplay.setCursor(x + 27, y + 5);
    videodisplay.print(name.c_str());
    if (wifiConnectionPending) {
        String signal = String(connectionEntries[idx].rssi) + " dBm";
        videodisplay.setTextColor(indexColor, bg);
        videodisplay.setCursor(x + w - signal.length() * 6 - 7, y + 5);
        videodisplay.print(signal.c_str());
    }
}

static void drawConnectionList() {
    int x = connectionListX();
    int y = connectionListY();
    int w = connectionListW();
    int h = CONNECTION_VISIBLE_ROWS * CONNECTION_ROW_H;
    uint32_t bg = videodisplay.RGB(19, 26, 30);
    uint32_t edge = videodisplay.RGB(76, 116, 128);
    videodisplay.fillRect(x, y, w, h, bg);
    videodisplay.rect(x, y, w, h, edge);
    for (int row = 0; row < CONNECTION_VISIBLE_ROWS; ++row) drawConnectionRow(row);
    if (connectionEntryCount == 0 && !connectionScanning) {
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(videodisplay.RGB(151, 177, 184), bg);
        videodisplay.setCursor(x + 12, y + 12);
        videodisplay.print(wifiConnectionPending ? "No WiFi networks found" : "No Bluetooth devices found");
    }
}

static void drawWiFiPasswordField() {
    int x = kernelWindowClientX() + 12;
    int y = kernelWindowClientY() + 66;
    int w = kernelWindowClientW() - 24;
    uint32_t bg = videodisplay.RGB(7, 10, 12);
    uint32_t edge = videodisplay.RGB(80, 223, 207);
    uint32_t fg = videodisplay.RGB(248, 248, 248);
    videodisplay.fillRect(x, y, w, 22, bg);
    videodisplay.rect(x, y, w, 22, edge);

    String shown = wifiPasswordVisible ? wifiPassword : String();
    if (!wifiPasswordVisible) {
        shown.reserve(wifiPassword.length());
        for (int i = 0; i < wifiPassword.length(); ++i) shown += '*';
    }
    int maxChars = max(1, (w - 14) / 6);
    int viewStart = max(0, wifiPasswordCursor - maxChars + 1);
    if (viewStart + maxChars > (int)shown.length()) viewStart = max(0, (int)shown.length() - maxChars);
    String visible = shown.substring(viewStart, min((int)shown.length(), viewStart + maxChars));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(fg, bg);
    videodisplay.setCursor(x + 6, y + 7);
    videodisplay.print(visible.c_str());
    int cursorColumn = constrain(wifiPasswordCursor - viewStart, 0, maxChars);
    videodisplay.fillRect(x + 6 + cursorColumn * 6, y + 5, 1, 12, fg);
}

static void drawWiFiPasswordScreen() {
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t white = videodisplay.RGB(248, 248, 248);
    uint32_t dim = videodisplay.RGB(151, 177, 184);
    int x = kernelWindowClientX();
    int y = kernelWindowClientY();
    videodisplay.fillRect(x, y, kernelWindowClientW(), kernelWindowClientH(), panel);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(x + 12, y + 15);
    videodisplay.print("Network");
    videodisplay.setTextColor(white, panel);
    videodisplay.setCursor(x + 68, y + 15);
    String ssid = connectionEntryCount > 0 ? String(connectionEntries[connectionSelected].name) : String("<none>");
    if (ssid.length() > 34) ssid = ssid.substring(0, 33) + "~";
    videodisplay.print(ssid.c_str());
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(x + 12, y + 51);
    videodisplay.print("Password");
    drawWiFiPasswordField();
    drawConnectionButton(x + 12, y + 98, 58, wifiPasswordVisible ? "Hide" : "Show", true);
    drawConnectionStatus();
    drawConnectionActions();
}

static void drawConnectionWindowContent() {
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t white = videodisplay.RGB(248, 248, 248);
    uint32_t dim = videodisplay.RGB(151, 177, 184);
    videodisplay.fillRect(kernelWindowClientX(), kernelWindowClientY(),
                          kernelWindowClientW(), kernelWindowClientH(), panel);
    if (wifiConnectionPending && connectionStage != CONNECTION_STAGE_LIST) {
        drawWiFiPasswordScreen();
        return;
    }
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, panel);
    videodisplay.setCursor(kernelWindowClientX() + 12, kernelWindowClientY() + 10);
    videodisplay.print(wifiConnectionPending ? "Available WiFi networks" : "Available Bluetooth devices");
    videodisplay.setTextColor(dim, panel);
    drawConnectionList();
    drawConnectionStatus();
    drawConnectionActions();
}

static void refreshConnectionWindow() {
    KernelCursorDrawGuard cursorGuard;
    drawConnectionWindowContent();
}

static void resetConnectionEntries() {
    connectionEntryCount = 0;
    connectionSelected = 0;
    connectionScroll = 0;
    for (int i = 0; i < CONNECTION_MAX_ENTRIES; ++i) {
        connectionEntries[i].name[0] = '\0';
        connectionEntries[i].controllerIndex = 0;
        connectionEntries[i].rssi = 0;
    }
}

static void startConnectionScan() {
    resetConnectionEntries();
    connectionStage = CONNECTION_STAGE_LIST;
    connectionScanning = true;
    connectionStatus = wifiConnectionPending ? "Scanning WiFi networks..." : "Scanning Bluetooth devices...";
    connectionScanDeadlineMs = millis() + (wifiConnectionPending ? 20000UL : 5000UL);
    refreshConnectionWindow();
    Serial1.println(wifiConnectionPending ? "CBWSC" : "BLE00");
}

static void openConnectionMenu(bool wifi) {
    if (kernelWindow.active) restoreAndCloseKernelWindow();
    configurationPending = false;
    bluetoothConnectionPending = !wifi;
    wifiConnectionPending = wifi;
    applaunched = true;
    util = false;
    fE = false;
    shell = false;
    connectionStage = CONNECTION_STAGE_LIST;
    wifiPassword = "";
    wifiPasswordCursor = 0;
    wifiPasswordVisible = false;
    connectionStatus = "Preparing scan...";
    resetConnectionEntries();
    openKernelWindow(KERNEL_WINDOW_CONNECTION,
                     wifi ? "WiFi Connection" : "Bluetooth Connection",
                     330, 236, drawConnectionWindowContent);
    startConnectionScan();
}

void openBluetoothConnectionMenu() { openConnectionMenu(false); }
void openWiFiConnectionMenu() { openConnectionMenu(true); }

static void closeConnectionMenu() {
    int returnItem = wifiConnectionPending ? 18 : 17;
    bluetoothConnectionPending = false;
    wifiConnectionPending = false;
    connectionScanning = false;
    wifiPassword = "";
    restoreAndCloseKernelWindow();
    configureMenu(returnItem);
}

static void setConnectionSelection(int selected) {
    if (connectionEntryCount <= 0) return;
    int oldSelected = connectionSelected;
    int oldScroll = connectionScroll;
    connectionSelected = constrain(selected, 0, connectionEntryCount - 1);
    if (connectionSelected < connectionScroll) connectionScroll = connectionSelected;
    if (connectionSelected >= connectionScroll + CONNECTION_VISIBLE_ROWS)
        connectionScroll = connectionSelected - CONNECTION_VISIBLE_ROWS + 1;
    KernelCursorDrawGuard cursorGuard;
    if (oldScroll != connectionScroll) {
        drawConnectionList();
    } else if (oldSelected != connectionSelected) {
        drawConnectionRow(oldSelected - connectionScroll);
        drawConnectionRow(connectionSelected - connectionScroll);
    }
}

static int findConnectionEntryByControllerIndex(int controllerIndex) {
    for (int i = 0; i < connectionEntryCount; ++i) {
        if (connectionEntries[i].controllerIndex == controllerIndex) return i;
    }
    return -1;
}

static int findConnectionEntryByName(const String &name) {
    for (int i = 0; i < connectionEntryCount; ++i) {
        if (name.equals(connectionEntries[i].name)) return i;
    }
    return -1;
}

static void addConnectionEntry(const String &name, int controllerIndex, int rssi, bool dedupeByName) {
    if (!name.length()) return;
    int existing = dedupeByName ? findConnectionEntryByName(name)
                                : findConnectionEntryByControllerIndex(controllerIndex);
    if (existing >= 0) {
        if (rssi != 0 && (connectionEntries[existing].rssi == 0 || rssi > connectionEntries[existing].rssi))
            connectionEntries[existing].rssi = rssi;
        return;
    }
    if (connectionEntryCount >= CONNECTION_MAX_ENTRIES) return;
    int idx = connectionEntryCount++;
    connectionCopyName(connectionEntries[idx].name, name);
    connectionEntries[idx].controllerIndex = controllerIndex;
    connectionEntries[idx].rssi = rssi;
    connectionStatus = String(connectionEntryCount) + (wifiConnectionPending ? " network(s) found" : " device(s) found");
    KernelCursorDrawGuard cursorGuard;
    int row = idx - connectionScroll;
    if (row >= 0 && row < CONNECTION_VISIBLE_ROWS) drawConnectionRow(row);
    drawConnectionStatus();
    drawConnectionActions();
}

static bool parseWiFiScanResult(const String &line, int &controllerIndex, String &ssid, int &rssi) {
    int colon = line.indexOf(':');
    int suffix = line.lastIndexOf(" (");
    if (colon <= 0 || suffix <= colon || !line.endsWith(")")) return false;
    for (int i = 0; i < colon; ++i) if (!isDigit(line.charAt(i))) return false;
    controllerIndex = line.substring(0, colon).toInt();
    ssid = line.substring(colon + 1, suffix);
    ssid.trim();
    rssi = line.substring(suffix + 2, line.length() - 1).toInt();
    return controllerIndex > 0 && ssid.length() > 0;
}

static bool handleConnectionControllerLine(String input) {
    if (!bluetoothConnectionPending && !wifiConnectionPending) return false;
    input.trim();
    if (!input.length()) return false;

    if (bluetoothConnectionPending) {
        if (input == "BL5X") {
            connectionScanning = true;
            connectionStatus = "Scanning Bluetooth devices...";
        } else if (input.startsWith("dev")) {
            int space = input.indexOf(' ');
            if (space <= 3) return false;
            int controllerIndex = input.substring(3, space).toInt();
            String name = input.substring(space + 1);
            name.trim();
            addConnectionEntry(name, controllerIndex, 0, false);
            return true;
        } else if (input == "BL1X") {
            connectionScanning = false;
            connectionStatus = "Bluetooth connected";
        } else if (input == "BL0X") {
            connectionScanning = false;
            connectionStatus = "Bluetooth connection failed";
        } else if (input == "BLEX") {
            connectionScanning = false;
            connectionStatus = "No Bluetooth device available";
        } else {
            return false;
        }
        KernelCursorDrawGuard cursorGuard;
        drawConnectionStatus();
        drawConnectionActions();
        return true;
    }

    if (input == "CBWSC OK") {
        connectionScanning = false;
        connectionStatus = connectionEntryCount > 0
            ? String(connectionEntryCount) + " network(s) found"
            : String("No WiFi networks found");
        KernelCursorDrawGuard cursorGuard;
        drawConnectionList();
        drawConnectionStatus();
        drawConnectionActions();
        return true;
    }
    if (input.startsWith("CBWSC FAIL")) {
        connectionScanning = false;
        connectionStatus = "WiFi scan failed - retry";
        KernelCursorDrawGuard cursorGuard;
        drawConnectionList();
        drawConnectionStatus();
        drawConnectionActions();
        return true;
    }
    if (input == "CBW0 OK" || input == "CBW1 OK") return true;
    if (input.startsWith("CBW2 OK")) {
        connectionScanning = false;
        connectionStage = CONNECTION_STAGE_LIST;
        wifiPassword = "";
        wifiPasswordCursor = 0;
        int ipAt = input.indexOf(" IP ");
        connectionStatus = ipAt >= 0 ? String("Connected - ") + input.substring(ipAt + 4)
                                     : String("WiFi connected");
        refreshConnectionWindow();
        return true;
    }
    if (input == "CBW2 FAIL" || input == "CBW2 NO_SSID") {
        connectionScanning = false;
        connectionStage = CONNECTION_STAGE_PASSWORD;
        connectionStatus = "Connection failed - check password";
        refreshConnectionWindow();
        return true;
    }

    int controllerIndex = 0;
    int rssi = 0;
    String ssid;
    if (parseWiFiScanResult(input, controllerIndex, ssid, rssi)) {
        addConnectionEntry(ssid, controllerIndex, rssi, true);
        return true;
    }
    return false;
}

static void beginSelectedConnection() {
    if (connectionEntryCount <= 0) return;
    connectionScanning = false;
    if (bluetoothConnectionPending) {
        connectionStatus = String("Connecting to ") + connectionEntries[connectionSelected].name + "...";
        {
            KernelCursorDrawGuard cursorGuard;
            drawConnectionStatus();
            drawConnectionActions();
        }
        Serial1.println(String("BLE0") + String(connectionEntries[connectionSelected].controllerIndex));
        return;
    }

    connectionStage = CONNECTION_STAGE_PASSWORD;
    connectionStatus = "Enter the network password";
    wifiPassword = "";
    wifiPasswordCursor = 0;
    wifiPasswordVisible = false;
    refreshConnectionWindow();
}

static void submitWiFiConnection() {
    if (!wifiConnectionPending || connectionEntryCount <= 0 || connectionStage == CONNECTION_STAGE_CONNECTING) return;
    connectionStage = CONNECTION_STAGE_CONNECTING;
    connectionStatus = String("Connecting to ") + connectionEntries[connectionSelected].name + "...";
    refreshConnectionWindow();
    Serial1.println(String("CBW0 ") + connectionEntries[connectionSelected].name);
    if (wifiPassword.length()) Serial1.println(String("CBW1 ") + wifiPassword);
    else Serial1.println("CBW1C");
    Serial1.println("CBW2");
}

static bool connectionInputCharacter(String input, char &output) {
    input.trim();
    bool shift = input.indexOf("Shift +") >= 0;
    int modifier = input.lastIndexOf(" + ");
    if (modifier >= 0) input = input.substring(modifier + 3);
    if (input == "Space") { output = ' '; return true; }
    if (input.length() != 1) return false;
    char c = input.charAt(0);
    if (shift && c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    else if (shift) {
        switch (c) {
            case '1': c = '!'; break; case '2': c = '@'; break; case '3': c = '#'; break;
            case '4': c = '$'; break; case '5': c = '%'; break; case '6': c = '^'; break;
            case '7': c = '&'; break; case '8': c = '*'; break; case '9': c = '('; break;
            case '0': c = ')'; break; case '-': c = '_'; break; case '=': c = '+'; break;
            case ',': c = '<'; break; case '.': c = '>'; break; case '/': c = '?'; break;
            case ';': c = ':'; break; case '\'': c = '"'; break; case '\\': c = '|'; break;
        }
    }
    if (c < 32 || c > 126) return false;
    output = c;
    return true;
}

static void handleConnectionInput(String input) {
    input.trim();
    if (!input.length() || input.indexOf("rlsd") != -1) return;

    if (wifiConnectionPending && connectionStage != CONNECTION_STAGE_LIST) {
        if (connectionStage == CONNECTION_STAGE_CONNECTING) {
            return;
        }
        if (input == "Escape") {
            connectionStage = CONNECTION_STAGE_LIST;
            connectionStatus = String(connectionEntryCount) + " network(s) found";
            refreshConnectionWindow();
            return;
        }
        if (input == "Enter") { submitWiFiConnection(); return; }
        if (input == "Backspace") {
            if (wifiPasswordCursor > 0) {
                wifiPassword.remove(wifiPasswordCursor - 1, 1);
                wifiPasswordCursor--;
            }
        } else if (input == "Delete") {
            if (wifiPasswordCursor < wifiPassword.length()) wifiPassword.remove(wifiPasswordCursor, 1);
        } else if (input == "LeftArrow") {
            wifiPasswordCursor = max(0, wifiPasswordCursor - 1);
        } else if (input == "RightArrow") {
            wifiPasswordCursor = min((int)wifiPassword.length(), wifiPasswordCursor + 1);
        } else if (input == "Tab") {
            wifiPasswordVisible = !wifiPasswordVisible;
            refreshConnectionWindow();
            return;
        } else {
            char c = 0;
            if (!connectionInputCharacter(input, c) || wifiPassword.length() >= WIFI_PASSWORD_MAX) return;
            wifiPassword = wifiPassword.substring(0, wifiPasswordCursor) + String(c) + wifiPassword.substring(wifiPasswordCursor);
            wifiPasswordCursor++;
        }
        KernelCursorDrawGuard cursorGuard;
        drawWiFiPasswordField();
        return;
    }

    if (input == "Escape") closeConnectionMenu();
    else if (input == "UpArrow") setConnectionSelection(connectionSelected - 1);
    else if (input == "DownArrow") setConnectionSelection(connectionSelected + 1);
    else if (input == "PageUp") setConnectionSelection(connectionSelected - CONNECTION_VISIBLE_ROWS);
    else if (input == "PageDown") setConnectionSelection(connectionSelected + CONNECTION_VISIBLE_ROWS);
    else if (input == "Enter") beginSelectedConnection();
    else if (input == "r" || input == "R") startConnectionScan();
}

static void serviceConnectionWindow() {
    if ((!bluetoothConnectionPending && !wifiConnectionPending) || !connectionScanning) return;
    if ((int32_t)(millis() - connectionScanDeadlineMs) < 0) return;
    connectionScanning = false;
    connectionStatus = connectionEntryCount > 0
        ? String(connectionEntryCount) + (wifiConnectionPending ? " network(s) found" : " device(s) found")
        : String(wifiConnectionPending ? "No WiFi networks found" : "No Bluetooth devices found");
    KernelCursorDrawGuard cursorGuard;
    drawConnectionList();
    drawConnectionStatus();
    drawConnectionActions();
}

static int cursorConnectionRowAt(int x, int y) {
    if (!Citadela::UI::pointInRect(x, y, connectionListX(), connectionListY(),
                                   connectionListW(), CONNECTION_VISIBLE_ROWS * CONNECTION_ROW_H)) return -1;
    int row = (y - connectionListY()) / CONNECTION_ROW_H;
    int idx = connectionScroll + row;
    return idx >= 0 && idx < connectionEntryCount ? idx : -1;
}

static int cursorConnectionActionAt(int x, int y) {
    int left = kernelWindowClientX() + 10;
    int width = kernelWindowClientW() - 20;
    if (connectionStage == CONNECTION_STAGE_LIST) {
        if (Citadela::UI::pointInRect(x, y, left, connectionActionY(), 66, 18)) return 1;
        if (Citadela::UI::pointInRect(x, y, left + width - 72, connectionActionY(), 72, 18)) return 2;
    } else {
        if (Citadela::UI::pointInRect(x, y, kernelWindowClientX() + 12, kernelWindowClientY() + 98, 58, 18)) return 3;
        if (Citadela::UI::pointInRect(x, y, left, connectionActionY(), 58, 18)) return 4;
        if (Citadela::UI::pointInRect(x, y, left + width - 72, connectionActionY(), 72, 18)) return 2;
    }
    return 0;
}

static void uartUploadPanelGeometry(int &x, int &y, int &w, int &h) {
    x = kernelWindowClientX() + 18;
    y = kernelWindowClientY() + 18;
    w = kernelWindowClientW() - 36;
    h = kernelWindowClientH() - 36;
}

static bool uartUploadCancelAt(int x, int y) {
    int panelX = 0, panelY = 0, panelW = 0, panelH = 0;
    uartUploadPanelGeometry(panelX, panelY, panelW, panelH);
    return Citadela::UI::pointInRect(x, y, panelX + panelW - 68, panelY + panelH - 25, 58, 17);
}

static void drawUARTUploadPanel() {
    int x = 0, y = 0, w = 0, h = 0;
    uartUploadPanelGeometry(x, y, w, h);
    uint32_t backdrop = videodisplay.RGB(13, 16, 18);
    uint32_t panel = videodisplay.RGB(24, 34, 40);
    uint32_t edge = videodisplay.RGB(76, 210, 197);
    uint32_t white = videodisplay.RGB(248, 248, 248);
    uint32_t dim = videodisplay.RGB(151, 177, 184);
    uint32_t action = videodisplay.RGB(39, 105, 133);

    videodisplay.fillRect(kernelWindowClientX(), kernelWindowClientY(),
                          kernelWindowClientW(), kernelWindowClientH(), backdrop);
    videodisplay.fillRect(x, y, w, h, panel);
    videodisplay.rect(x, y, w, h, edge);

    int iconX = x + 14;
    int iconY = y + 18;
    videodisplay.rect(iconX, iconY, 34, 42, dim);
    videodisplay.fillRect(iconX + 8, iconY + 9, 18, 2, white);
    videodisplay.fillRect(iconX + 16, iconY + 4, 2, 24, white);
    videodisplay.line(iconX + 11, iconY + 20, iconX + 17, iconY + 27, white);
    videodisplay.line(iconX + 23, iconY + 20, iconX + 17, iconY + 27, white);

    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(white, panel);
    videodisplay.setCursor(x + 60, y + 18);
    videodisplay.print("UART Package Upload");
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(x + 60, y + 38);
    videodisplay.print("USB UART ready");
    videodisplay.setCursor(x + 60, y + 51);
    videodisplay.print("Waiting for uploader...");

    videodisplay.fillRect(x + 12, y + 73, w - 24, 48, videodisplay.RGB(17, 24, 29));
    videodisplay.setTextColor(videodisplay.RGB(107, 230, 214), videodisplay.RGB(17, 24, 29));
    videodisplay.setCursor(x + 20, y + 80);
    videodisplay.print("APP: .bin + .ino");
    videodisplay.setCursor(x + 20, y + 94);
    videodisplay.print("KERNEL: system image + source");
    videodisplay.setCursor(x + 20, y + 108);
    videodisplay.print("BOOTLOADER: system app + source");

    videodisplay.fillRect(x + w - 68, y + h - 25, 58, 17, action);
    videodisplay.rect(x + w - 68, y + h - 25, 58, 17, dim);
    videodisplay.setTextColor(white, action);
    videodisplay.setCursor(x + w - 56, y + h - 19);
    videodisplay.print("Cancel");
}

void openUARTUploadMenu() {
    if (!kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_CONFIG) return;
    uartUploadStatus = "";
    uartUploadPending = true;
    uartUploadInProgress = false;
    KernelCursorDrawGuard cursorGuard;
    drawUARTUploadPanel();
    Serial.println("CITUART ARMED");
}

void handleUARTUploadInput(String input) {
    input.trim();
    if (uartUploadInProgress || input.length() == 0 || input.indexOf("rlsd") != -1) return;
    if (input == "Escape") {
        uartUploadPending = false;
        uartUploadStatus = "Upload cancelled";
        KernelCursorDrawGuard cursorGuard;
        drawConfigurationWindowContent();
    }
}

static bool updateUploadedAppCache(const String &appName) {
    String entry = String("A|") + appName + ".bin";
    const char *tempPath = "/app_names.uart";
    const char *backupPath = "/app_names.prev";
    SPIFFS.remove(tempPath);
    File output = SPIFFS.open(tempPath, FILE_WRITE);
    if (!output) return false;
    writeCachedName(output, entry);

    int copied = 1;
    File current = SPIFFS.open(APP_NAME_CACHE_PATH, FILE_READ);
    while (current && current.available() && copied < MAX_FILES) {
        String existing = current.readStringUntil('\n');
        existing.trim();
        if (existing.length() == 0) continue;
        String payload = appEntryPayload(existing);
        if (payload.equalsIgnoreCase(appName + ".bin")) continue;
        if (!appEntryIsFirmware(existing)) continue;
        writeCachedName(output, existing);
        copied++;
    }
    if (current) current.close();
    output.flush();
    output.close();

    SPIFFS.remove(backupPath);
    bool hadCurrent = SPIFFS.exists(APP_NAME_CACHE_PATH);
    if (hadCurrent && !SPIFFS.rename(APP_NAME_CACHE_PATH, backupPath)) {
        SPIFFS.remove(tempPath);
        return false;
    }
    if (!SPIFFS.rename(tempPath, APP_NAME_CACHE_PATH)) {
        if (hadCurrent) SPIFFS.rename(backupPath, APP_NAME_CACHE_PATH);
        SPIFFS.remove(tempPath);
        return false;
    }
    SPIFFS.remove(backupPath);
    writeAppCacheSchema();

    File refresh = SPIFFS.open(FILE_TREE_REFRESH_FLAG_PATH, FILE_WRITE);
    if (refresh) {
        refresh.print('1');
        refresh.close();
    }
    SPIFFS.remove(FILE_TREE_SCHEMA_PATH);
    return loadFNBufferFromSPIFFS();
}

static void markUARTFileTreeForRefresh() {
    File refresh = SPIFFS.open(FILE_TREE_REFRESH_FLAG_PATH, FILE_WRITE);
    if (refresh) {
        refresh.print('1');
        refresh.close();
    }
    SPIFFS.remove(FILE_TREE_SCHEMA_PATH);
}

static bool ensureUARTKernelPendingMarker() {
    if (SPIFFS.exists(UART_KERNEL_PENDING_PATH)) return true;
    File marker = SPIFFS.open(UART_KERNEL_PENDING_PATH, FILE_WRITE);
    if (!marker) return false;
    marker.println("pending-uart-kernel");
    marker.close();
    return true;
}

static void uartUploadVideoProgress(int percent, const char *label) {
    fallbackVideoProgress(percent, label);
}

static void restoreKernelUIAfterUARTUpload(bool videoReady) {
    wallpaperFetchInProgress = false;
    uartUploadInProgress = false;
    uartUploadPending = false;
    if (!videoReady) return;

    bootVideoRelease();
    fE = false;
    util = false;
    applaunched = false;
    configurationPending = false;
    manualTimePending = false;
    iconThresholdPending = false;
    wallpaperPickerPending = false;
    home();
    configureMenu(15);
}

static void handleUARTUploadBeginCommand(const String &command) {
    Citadela::UARTUploadRequest request;
    String parseError;
    if (!Citadela::UARTUploadReceiver::parseBegin(command, request, parseError)) {
        Serial.print("CITUART ERROR ");
        Serial.println(parseError);
        return;
    }
    if (!uartUploadPending || uartUploadInProgress) {
        Serial.println(uartUploadInProgress
            ? "CITUART ERROR BUSY"
            : "CITUART ERROR NOT_ARMED");
        return;
    }

    uartUploadInProgress = true;
    wallpaperFetchInProgress = true;
    Serial.println("CITUART ACCEPTED");
    // USB UART remains usable when the separate video controller is absent.
    // Release the local framebuffer for SD access and restore it afterwards.
    bool controllerVideoReady = prepareWallpaperFetchVideo();
    if (!controllerVideoReady) Serial.println("CITUART NOTICE CONTROLLER_VIDEO_UNAVAILABLE");

    kernelCursorRestore();
    wallpaperFetchCursorX = CitaCursor.X();
    wallpaperFetchCursorY = CitaCursor.Y();
    wallpaperFetchCursorButtons = CitaCursor.Buttons();
    CitaCursor.Enable(false);
    videodisplay.releaseVideoMemory();
    initVd = false;
    pinMode(25, INPUT_PULLDOWN);
    if (controllerVideoReady) bootVideoSerial.fetchFilesStart();
    fallbackVideoProgress(0, "UART package upload");
    delay(60);

    const uint32_t mountFrequencies[] = {4000000, 2000000, 1000000};
    Citadela::SDStats stats;
    SDInUse = true;
    bool mounted = Citadela::Storage::beginBoardSDWithRetry(
        SD, SPI,
        mountFrequencies,
        sizeof(mountFrequencies) / sizeof(mountFrequencies[0]),
        &stats,
        &Serial);
    if (!mounted) {
        SDInUse = false;
        Serial.println("CITUART ERROR SD_MOUNT_FAILED");
        uartUploadStatus = "Upload failed: SD mount";
        bool videoReady = resumeKernelVideoAfterWallpaperFetch();
        restoreKernelUIAfterUARTUpload(videoReady);
        return;
    }
    warning = false;
    cardSize = stats.cardSize;
    usedBytes = stats.usedBytes;
    freeBytes = stats.freeBytes;

    bool markerWasPresent = SPIFFS.exists(UART_KERNEL_PENDING_PATH);
    if (request.target == Citadela::UARTUploadTarget::Kernel && !ensureUARTKernelPendingMarker()) {
        Citadela::Storage::resetBoardSDBus(SD, SPI);
        SDInUse = false;
        Serial.println("CITUART ERROR KERNEL_MARKER_FAILED");
        uartUploadStatus = "Upload failed: safety marker";
        bool videoReady = resumeKernelVideoAfterWallpaperFetch();
        restoreKernelUIAfterUARTUpload(videoReady);
        return;
    }

    Serial.println("CITUART BAUD 921600");
    Serial.flush();
    delay(60);
    Serial.updateBaudRate(921600);
    delay(60);

    Citadela::UARTUploadResult result = Citadela::UARTUploadReceiver::receive(
        Serial, SD, request, uartUploadVideoProgress);

    bool cacheReady = true;
    if (result.ok && request.target == Citadela::UARTUploadTarget::App) {
        cacheReady = updateUploadedAppCache(request.appName);
        if (!cacheReady) Serial.println("CITUART NOTICE CACHE_REFRESH_PENDING");
    }
    if (result.ok && request.target != Citadela::UARTUploadTarget::App) {
        markUARTFileTreeForRefresh();
    }
    if (!result.ok && request.target == Citadela::UARTUploadTarget::Kernel && !markerWasPresent) {
        SPIFFS.remove(UART_KERNEL_PENDING_PATH);
    }

    if (result.ok) {
        Serial.print("CITUART DONE ");
        Serial.print(request.target == Citadela::UARTUploadTarget::Kernel ? "KERNEL" :
                     request.target == Citadela::UARTUploadTarget::Bootloader ? "BOOTLOADER" : "APP");
        Serial.print(' ');
        Serial.print(request.appName);
        Serial.print(' ');
        Serial.print(result.binaryBytes);
        Serial.print(' ');
        Serial.println(result.sourceBytes);
        uartUploadStatus = request.target == Citadela::UARTUploadTarget::Kernel
            ? "Kernel package updated"
            : request.target == Citadela::UARTUploadTarget::Bootloader
                ? "Bootloader package updated"
                : (cacheReady ? String("Uploaded ") + request.appName : String("Uploaded ") + request.appName + " (rescan pending)");
    } else {
        uartUploadStatus = String("Upload failed: ") + result.error;
    }

    Serial.flush();
    delay(80);
    Serial.updateBaudRate(115200);
    delay(30);
    Citadela::Storage::resetBoardSDBus(SD, SPI);
    SDInUse = false;

    bool videoReady = resumeKernelVideoAfterWallpaperFetch();
    restoreKernelUIAfterUARTUpload(videoReady);
}
void handleBootloaderFlash() {
    bootVideoStart("Flashing bootloader");
    String imagePath = appString.length() ? appString : "/System/bootloader.bin";
    boolRes = "false";
    boolUpdate();
    appString = "";
    reRememberApp();
    File bootloaderFile = SD.open(imagePath.c_str());
    if (!bootloaderFile) {Serial.println("file not found!");return;}
    size_t bootloaderSize = bootloaderFile.size();
    Serial.printf("size: %d bytes\n", bootloaderSize);
    if (!Update.begin(bootloaderSize)) {
        Serial.printf("Not enough space! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), bootloaderSize);
        Serial.printf("Error: update failed: %s\n", Update.errorString());
        bootloaderFile.close();
        return;
    }
    uint8_t buffer[2048];
    size_t flashedBytes = 0;
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
        flashedBytes += written;
        bootVideoProgress(10 + (int)((flashedBytes * 88ULL) / bootloaderSize), "Flashing bootloader");
    }
    if (Update.end(true)) {
        Serial.println("updated successfully. Restarting...");
        bootVideoProgress(100, "Restarting");
        Serial1.flush();
        delay(25);
        ESP.restart();
    } else {
        Serial.printf("update failed: %s\n", Update.errorString());
    }
}
void handleCProgFlash() {
    bootVideoStart("Flashing CProg");
    boolRes = "false";
    boolUpdate();
    appString = "";
    reRememberApp();
    File bootloaderFile = SD.open("/apps/CProg.bin");
    if (!bootloaderFile) {Serial.println("file not found!");return;}
    size_t bootloaderSize = bootloaderFile.size();
    Serial.printf("size: %d bytes\n", bootloaderSize);
    if (!Update.begin(bootloaderSize)) {
        Serial.printf("Not enough space! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), bootloaderSize);
        Serial.printf("Error: update failed: %s\n", Update.errorString());
        bootloaderFile.close();
        return;
    }
    uint8_t buffer[2048];
    size_t flashedBytes = 0;
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
        flashedBytes += written;
        bootVideoProgress(10 + (int)((flashedBytes * 88ULL) / bootloaderSize), "Flashing CProg");
    }
    if (Update.end(true)) {
        Serial.println("updated successfully. Restarting...");
        bootVideoProgress(100, "Restarting");
        Serial1.flush();
        delay(25);
        ESP.restart();
    } else {
        Serial.printf("update failed: %s\n", Update.errorString());
    }
}
void kernelFunction() {
    startup = false;
    if (warning){warningDrum();} else {home();}
}
void warningDrum() {
  videodisplay.fillRect(60,60,255,150,videodisplay.RGB(255,255,255));
  videodisplay.setCursor(164, 70);
  videodisplay.print("WARNING");
  videodisplay.setCursor(70, 90);
  videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
  videodisplay.println(warningMessage.c_str());
  videodisplay.setFont(Font6x8);
  videodisplay.println("-Check if component is present");
  videodisplay.println("-Restart your machine");
  videodisplay.println("-Connect to a stable power supply");
  videodisplay.fillRect(170,180,40,10,0);
  videodisplay.setCursor(173,181);
  videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
  videodisplay.print("OK");
  videodisplay.setCursor(255+60,255+10);
  videodisplay.println("Citadela");
}
 
void showTooltipForButton(int btnIndex, const char* text) {
    if (!TooltipsToggle) return;
    KernelCursorDrawGuard cursorGuard;
    int x = 15;
    int startY = 18;
    int size = 20;
    int spacing = 10;
    int y = startY + (btnIndex) * (size + spacing);
    strncpy(tooltipText, text, sizeof(tooltipText)-1);
    tooltipText[sizeof(tooltipText)-1] = '\0';
    int padding = 5;
    int charW = 6;
    int textLen = strlen(tooltipText);
    int tw = textLen * charW + padding * 2;
    int th = 10 + padding * 2;
    int tx = x + size + 6;
    int ty = y;
    if (tx + tw > WALLPAPER_W) tx = x - tw - 6;
    if (tx < 0) tx = 0;
    if (ty + th > WALLPAPER_H) ty = WALLPAPER_H - th;
    videodisplay.fillRect(tx, ty, tw, th, videodisplay.RGB(255,255,255));
    videodisplay.rect(tx, ty, tw, th, videodisplay.RGB(101, 150, 159));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(0,videodisplay.RGB(255,255,255));
    videodisplay.setCursor(tx + padding, ty + padding);
    videodisplay.print(tooltipText);
    tooltipIndex = btnIndex;
    tooltipX = tx;
    tooltipY = ty;
    tooltipW = tw;
    tooltipH = th;
    tooltipShown = true;
}
 
void hideTooltip() {
    if (!tooltipShown) return;
    KernelCursorDrawGuard cursorGuard;
    renderBMPRegionFromSPIFFS(WALLPAPER_SPIFFS_PATH, tooltipX, tooltipY, tooltipW, tooltipH, WALLPAPER_W, WALLPAPER_H);
    tooltipShown = false;
    tooltipIndex = -1;
    tooltipX = tooltipY = tooltipW = tooltipH = 0;
    tooltipText[0] = '\0';
}

static bool restoreWallpaperRawCacheRegion(int x, int y, int w, int h) {
    cleanupWallpaperArtifactsForRender(WALLPAPER_SPIFFS_PATH);
    if (wallpaperRawCacheInvalidated || !SPIFFS.exists(WALLPAPER_RAW_CACHE_PATH)) return false;
    File cache = SPIFFS.open(WALLPAPER_RAW_CACHE_PATH, FILE_READ);
    if (!cache) return false;

    uint16_t cacheW = 0;
    uint16_t cacheH = 0;
    uint16_t pixelSize = 0;
    uint32_t signature = 0;
    bool valid = wallpaperRawCacheHeader(cache, cacheW, cacheH, pixelSize, signature) &&
                 pixelSize == 1 &&
                 signature == wallpaperRawCacheSignature() &&
                 cacheW <= SCREEN_WIDTH;
    if (!valid) {
        cache.close();
        return false;
    }

    int rx = constrain(x, 0, (int)cacheW);
    int ry = constrain(y, 0, (int)cacheH);
    int rw = min(w, (int)cacheW - rx);
    int rh = min(h, (int)cacheH - ry);
    if (rw <= 0 || rh <= 0) {
        cache.close();
        return false;
    }

    const size_t headerBytes = WALLPAPER_RAW_CACHE_HEADER_BYTES;
    const size_t pixelBytes = 1;
    uint8_t *signalRow = reinterpret_cast<uint8_t*>(wallpaperRawLineScratch);
    size_t rowBytes = (size_t)cacheW * pixelBytes;
    bool ok = cache.seek(headerBytes + (size_t)ry * rowBytes);
    for (int row = 0; ok && row < rh; ++row) {
        ok = fileReadExact(cache, signalRow, rowBytes);
        for (int col = 0; ok && col < rw; ++col) {
            videodisplay.setRawPixelFast(rx + col, ry + row,
                (CitCompositeColorDAC::RawPixel)signalRow[rx + col] << 8);
        }
    }
    cache.close();
    return ok;
}

static void restoreDesktopBaseRect(int x, int y, int w, int h) {
    int rx = max(0, x);
    int ry = max(0, y);
    int rw = min(w, SCREEN_WIDTH - rx);
    int rh = min(h, SCREEN_HEIGHT - ry);
    if (rw <= 0 || rh <= 0) return;
    bool wallpaperActive = WallpaperToggle && !FastBoot && SPIFFS.exists(WALLPAPER_SPIFFS_PATH);
    if (wallpaperActive && !serialDisplay.enabled() && restoreWallpaperRawCacheRegion(rx, ry, rw, rh)) return;
    if (wallpaperActive) {
        renderBMPRegionFromSPIFFS(WALLPAPER_SPIFFS_PATH, rx, ry, rw, rh, WALLPAPER_W, WALLPAPER_H);
    } else {
        videodisplay.fillRect(rx, ry, rw, rh, 0);
    }
}

static bool desktopRectsIntersect(int ax, int ay, int aw, int ah,
                                  int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void restoreDesktopRect(int x, int y, int w, int h) {
    int rx = max(0, x);
    int ry = max(0, y);
    int rw = min(w, SCREEN_WIDTH - rx);
    int rh = min(h, SCREEN_HEIGHT - ry);
    if (rw <= 0 || rh <= 0) return;
    restoreDesktopBaseRect(rx, ry, rw, rh);

    if (desktopRectsIntersect(rx, ry, rw, rh, 8, 12, 36, 190)) {
        for (int i = 0; i < 6; ++i) {
            int iconY = 18 + i * 30;
            if (!desktopRectsIntersect(rx, ry, rw, rh, 15, iconY, 20, 20)) continue;
            if (iconAvailableInSPIFFS[i]) drawIconBuffer(i, 15, iconY, 20, 20);
            else drawMissingIconMark(15, iconY, 20, 20);
        }
        int selectedY = 18 + (constrain(homeSelectionIndex, 1, 6) - 1) * 30;
        videodisplay.fillRect(11, selectedY, 2, 20, videodisplay.RGB(255, 255, 255));
        videodisplay.fillRect(37, selectedY, 2, 20, videodisplay.RGB(255, 255, 255));
    }

    if (desktopRectsIntersect(rx, ry, rw, rh, 300, 10, 80, 175)) visualheap();

    if (desktopRectsIntersect(rx, ry, rw, rh, 0, 270, SCREEN_WIDTH, 15)) {
        uint32_t taskbar = videodisplay.RGB(218, 235, 242);
        videodisplay.fillRect(0, 270, SCREEN_WIDTH, 15, taskbar);
        videodisplay.setFont(Font8x8);
        videodisplay.setTextColor(videodisplay.RGB(70,105,111), taskbar);
        videodisplay.setCursor(CLOCK_POS_X, CLOCK_POS_Y);
        videodisplay.print(currentClockTime.c_str());
        videodisplay.setFont(Font6x8);
        videodisplay.setCursor(CLOCK_POS_X - 65, CLOCK_POS_Y + 1);
        videodisplay.print(currentClockDate.c_str());
    }
}

static void drawMissingIconMark(int x, int y, int w, int h) {
    uint32_t markColor = videodisplay.RGB(255, 255, 255);
    videodisplay.line(x, y, x + w - 1, y + h - 1, markColor);
    videodisplay.line(x + w - 1, y, x, y + h - 1, markColor);
}
 
void drawButtons(int selected){
    int btnNumber = 6;
    int x = 15;
    int startY = 18;
    int size = 20;
    int spacing = 10;
 
    for (int i = 0; i < btnNumber; i++) {
        int y = startY + i * (size + spacing);
        restoreDesktopBaseRect(x, y, size, size);
 
        int idx = i;
        if (iconAvailableInSPIFFS[idx]) {
            drawIconBuffer(idx, x, y, size, size);
        } else {
            drawMissingIconMark(x, y, size, size);
        }
    }
}

static void drawUtilityEntryFallback(char type, int x, int y, int w, int h) {
    uint32_t white = videodisplay.RGB(255, 255, 255);
    int ix = x + max(1, (w - 18) / 2);
    int iy = y + max(1, (h - 14) / 2);
    if (type == 'D') {
        videodisplay.rect(ix + 1, iy + 4, 16, 10, white);
        videodisplay.fillRect(ix + 2, iy + 2, 6, 3, white);
        videodisplay.line(ix + 8, iy + 4, ix + 16, iy + 4, white);
        return;
    }
    if (type == 'A') {
        videodisplay.line(ix + 5, iy + 2, ix + 1, iy + 7, white);
        videodisplay.line(ix + 1, iy + 7, ix + 5, iy + 12, white);
        videodisplay.line(ix + 13, iy + 2, ix + 17, iy + 7, white);
        videodisplay.line(ix + 17, iy + 7, ix + 13, iy + 12, white);
        videodisplay.line(ix + 11, iy + 2, ix + 7, iy + 12, white);
        return;
    }
    videodisplay.rect(ix + 4, iy + 1, 11, 14, white);
    videodisplay.line(ix + 11, iy + 1, ix + 15, iy + 5, white);
    videodisplay.line(ix + 6, iy + 8, ix + 13, iy + 8, white);
    videodisplay.line(ix + 6, iy + 11, ix + 13, iy + 11, white);
}

static bool utilityEntryGeometry(int index, int &x, int &y, int &w, int &h) {
    if (index < 0 || index >= appFileCount) return false;
    if (!kernelWindow.active || kernelWindow.kind != KERNEL_WINDOW_UTILITIES) return false;
    const int boxSizeX = 62;
    const int boxSizeY = 34;
    const int spacing = 5;
    const int columns = 5;
    const int visibleRows = 5;
    int absoluteRow = index / columns;
    int row = absoluteRow - utilityScrollRow;
    if (row < 0 || row >= visibleRows) return false;
    int col = index % columns;
    x = kernelWindowClientX() + 9 + col * (boxSizeX + spacing);
    y = kernelWindowClientY() + 10 + row * (boxSizeY + spacing);
    w = boxSizeX;
    h = boxSizeY;
    return true;
}

static void drawUtilityEntryValueAt(int index, const String &entry, bool isSelected) {
    int x = 0, y = 0, boxSizeX = 0, boxSizeY = 0;
    if (!utilityEntryGeometry(index, x, y, boxSizeX, boxSizeY)) return;

    if (!appEntryIsFirmware(entry)) return;
    String filename = appEntryPayload(entry);
    String base = basenameNoExt(leafNameFromPath(filename));
    String spIconPath = "/icons/" + base + ".bmp";

    uint32_t bg = isSelected ? videodisplay.RGB(24, 91, 116) : videodisplay.RGB(22, 29, 34);
    uint32_t border = isSelected ? videodisplay.RGB(65, 226, 207) : videodisplay.RGB(92, 121, 132);
    videodisplay.fillRect(x, y, boxSizeX, boxSizeY, bg);
    videodisplay.rect(x, y, boxSizeX, boxSizeY, border);

    int iconPad = 4;
    int iconWdst = boxSizeX - iconPad * 2;
    int iconHdst = boxSizeY - 12;
    int iconX = x + iconPad;
    int iconY = y + iconPad;

    if (SPIFFS.exists(spIconPath)) {
        drawIconFromSPIFFSPath(spIconPath.c_str(), iconX, iconY, iconWdst, iconHdst);
    } else {
        drawUtilityEntryFallback('A', iconX, iconY, iconWdst, iconHdst);
    }

    String label = basenameNoExt(leafNameFromPath(filename));
    if (label.length() > 10) label = label.substring(0, 10);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), bg);
    videodisplay.setCursor(x + 2, y + boxSizeY - 9);
    videodisplay.print(label.c_str());
}

static void drawUtilityEntryAt(int index, bool isSelected) {
    String entry;
    if (readCachedAppEntry(index, entry)) drawUtilityEntryValueAt(index, entry, isSelected);
}
 
void home() {
    kernelCursorRestore();
    restoreKernelWindowOutline();
    kernelWindow = KernelWindowState();
    dragvalve = 1;
    homeSelectionIndex = 1;
    previousDragValve = -1;
    tooltipShown = false;
    tooltipIndex = -1;
    hoverStartMillis = 0;
 
    bool wallpaperWillRender = WallpaperToggle && !FastBoot && SPIFFS.exists(WALLPAPER_SPIFFS_PATH);
    if (!wallpaperWillRender || skipWallpaperBrightPixels || skipWallpaperDarkPixels) {
        videodisplay.clear();
    }
    if (wallpaperWillRender) {
        if (serialDisplay.enabled() || !restoreWallpaperRawCache(SCREEN_WIDTH, SCREEN_HEIGHT)) {
            Serial.println("Rendering wallpaper from SPIFFS...");
            renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, SCREEN_WIDTH, SCREEN_HEIGHT);
            if (wallpaperRawCacheSaveEnabled) saveWallpaperRawCache(SCREEN_WIDTH, SCREEN_HEIGHT);
        }
    }
    videodisplay.fillRect(0, 270, SCREEN_WIDTH, 15, videodisplay.RGB(218, 235, 242));
    videodisplay.fillRect(11, 18, 2, 20, videodisplay.RGB(255, 255, 255));
    videodisplay.fillRect(37, 18, 2, 20, videodisplay.RGB(255, 255, 255));
    updateDisplay();
    logLine = 1;
    Serial.println("Reading DiskBarn, seeking SDF");
    Serial.println(String(cardSize + usedBytes + freeBytes));
    Serial.println("Stage 2 Log: DEF Drivers init");
    videodisplay.setFont(Font8x8);
    drawButtons(0);
    videodisplay.setTextColor(videodisplay.RGB(255,255,255),0);
    videodisplay.setFont(Font8x8);
    videodisplay.setCursor(255+70, 255 +8);
    Serial.println("UI Loaded properly");
    videodisplay.setCursor(CLOCK_POS_X, CLOCK_POS_Y);
    videodisplay.setTextColor(0, videodisplay.RGB(218, 235, 242));
    videodisplay.println("00:00:00");
    videodisplay.setCursor(CLOCK_POS_X - 65, CLOCK_POS_Y + 1);
    videodisplay.setFont(Font6x8);
    videodisplay.println("2000-01-01");
    videodisplay.setFont(Font8x8);
    visualheap();
    kernelCursorRefreshAfterRedraw();
}
 
void utilitiesMenu() {
    applaunched = true;
    util = true;
    fE = false;
    shell = false;
    dragvalve = 0;
    previousDragValve = -1;
    utilityScrollRow = 0;
    openKernelWindow(KERNEL_WINDOW_UTILITIES, "Utilities", 356, 238, listAppFiles);
    previousDragValve = dragvalve;
}
 
void listAppFiles() {
    uint32_t panel = videodisplay.RGB(13, 16, 18);
    uint32_t dim = videodisplay.RGB(132, 146, 146);
    uint32_t white = videodisplay.RGB(255, 255, 255);
    videodisplay.fillRect(kernelWindowClientX(), kernelWindowClientY(),
                          kernelWindowClientW(), kernelWindowClientH(), panel);
    if (appFileCount <= 0) {
        videodisplay.setFont(Font8x8);
        videodisplay.setTextColor(white, panel);
        videodisplay.setCursor(kernelWindowClientX() + 18, kernelWindowClientY() + 28);
        videodisplay.print("No flashable apps found");
        return;
    }

    File appCache = SPIFFS.open(APP_NAME_CACHE_PATH, FILE_READ);
    int appIndex = 0;
    while (appCache && appCache.available() && appIndex < appFileCount) {
        String entry = appCache.readStringUntil('\n');
        entry.trim();
        if (!appEntryIsFirmware(entry)) continue;
        drawUtilityEntryValueAt(appIndex, entry, appIndex == dragvalve);
        appIndex++;
    }
    if (appCache) appCache.close();

    int statusY = kernelWindow.y + kernelWindow.h - 13;
    videodisplay.fillRect(kernelWindowClientX() + 6, statusY - 2, kernelWindowClientW() - 12, 11, panel);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, panel);
    videodisplay.setCursor(kernelWindowClientX() + 9, statusY);
    videodisplay.print((String(appFileCount) + " flashable applications").c_str());
    if (appFileCount > 25) {
        int totalRows = (appFileCount + 4) / 5;
        videodisplay.setCursor(kernelWindow.x + kernelWindow.w - 72, statusY);
        videodisplay.print((String(utilityScrollRow + 1) + "/" + String(max(1, totalRows - 4))).c_str());
    }
}
 
 
void flashApp() {
    bootVideoStart("Flashing app");
    String structure = String("/apps/" + boolRes);
    File appFile = SD.open(appString);
    boolRes = "false";
    boolUpdate();
    appString = "";
    reRememberApp();
    if (!appFile) {
        Serial.println("file not found!");
        return;
    }
    size_t appSize = appFile.size();
    Serial.printf("Firmware file size: %d bytes\n", appSize);
    if (appSize == 0) {
        Serial.println("App file is empty. Aborting update.");
        appFile.close();
        return;
    }
    size_t freeSpace = ESP.getFreeSketchSpace();
    Serial.printf("Available space: %d bytes\n", freeSpace);
 
    if (appSize > freeSpace) {
        Serial.printf("Not enough space! Available: %d bytes, Needed: %d bytes\n", freeSpace, appSize);
        appFile.close();
        return;
    }
    if (!Update.begin(appSize)) {
        Serial.printf("Error: update failed at begin: %s\n", Update.errorString());
        appFile.close();
        return;
    }
    Serial1.print("VDAPPSTART ");
    Serial1.print((unsigned long)appSize);
    Serial1.println(" Flashing app");
    Serial1.flush();
    uint8_t buffer[2048];
    size_t flashedBytes = 0;
    while (appFile.available()) {
        int len = appFile.read(buffer, sizeof(buffer));
        int written = Update.write(buffer, len);
        Serial.printf("Read %d bytes, Wrote %d bytes\n", len, written);
        if (written != len) {
            Serial.printf("Write failed! Aborting update. Written: %d, Expected: %d\n", written, len);
            Update.abort();
            appFile.close();
            return;
        }
        flashedBytes += written;
        Serial1.print("VDAPPWRITE ");
        Serial1.println((unsigned long)flashedBytes);
        bootVideoProgress(10 + (int)((flashedBytes * 88ULL) / appSize), "Flashing app");
    }
    if (Update.end(true)) {
        Serial.println("Firmware update successful. Restarting...");
        Serial1.print("VDAPPWRITE ");
        Serial1.println((unsigned long)appSize);
        bootVideoProgress(100, "Restarting");
        Serial1.flush();
        delay(25);
        ESP.restart();
    } else {
        Serial.printf("update failed: %s\n", Update.errorString());
    }
    appFile.close();
}
 
void rememberApp(){
    appString = "/apps/" + appName;
    Serial.printf("App string: %s, appName: %s\n", appString.c_str(), appName.c_str());
}
 
void handleNavigation() {
    if (util) {
        if (dragvalve != previousDragValve) {
            KernelCursorDrawGuard cursorGuard;
            int oldIndex = previousDragValve;
            int oldScrollRow = utilityScrollRow;
            int selectedRow = max(0, dragvalve / 5);
            if (selectedRow < utilityScrollRow) utilityScrollRow = selectedRow;
            if (selectedRow >= utilityScrollRow + 5) utilityScrollRow = selectedRow - 4;
            previousDragValve = dragvalve;
            if (oldScrollRow != utilityScrollRow) {
                listAppFiles();
            } else {
                if (oldIndex >= 0 && oldIndex < appFileCount) drawUtilityEntryAt(oldIndex, false);
                if (dragvalve >= 0 && dragvalve < appFileCount) drawUtilityEntryAt(dragvalve, true);
            }
        }
    }
}
 
void handleSelection() {
    if (appFileCount > 0) {
        if (util) {
            String entry;
            if (!readCachedAppEntry(dragvalve, entry) || !appEntryIsFirmware(entry)) {
                Serial.printf("Utilities entry %d could not be read from the app cache.\n", dragvalve);
                return;
            }
            appName = appEntryPayload(entry);
            boolRes = "/apps/" + appName;
            boolUpdate();
            restartWithFallbackVideo("Preparing app flash");
        }
    } else {
        utilitiesMenu();
    }
}
 
static void feCopyString(char *dst, int cap, const String &src) {
    if (!dst || cap <= 0) return;
    strncpy(dst, src.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

static String feLower(String value) {
    value.toLowerCase();
    return value;
}

static int feCaseCompare(const char *a, const char *b) {
    while (*a && *b) {
        char ca = tolower((unsigned char)*a);
        char cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool ensureFileExplorerEntries() {
    return true;
}

static void feResetEntryWindow() {
    feEntryCount = 0;
    feTotalCount = 0;
    feWindowStart = max(0, feScroll);
}

static FileExplorerEntry *feLoadedEntry(int absoluteIndex) {
    int localIndex = absoluteIndex - feWindowStart;
    if (localIndex < 0 || localIndex >= feEntryCount) return nullptr;
    return &feEntries[localIndex];
}

static bool feCopyLoadedEntry(int absoluteIndex, FileExplorerEntry &out) {
    FileExplorerEntry *entry = feLoadedEntry(absoluteIndex);
    if (!entry) return false;
    out = *entry;
    return true;
}

static bool feVisibleRangeLoaded() {
    if (feTotalCount <= 0) return true;
    int lastVisible = min(feTotalCount, feScroll + FE_VISIBLE_ROWS) - 1;
    return feScroll >= feWindowStart && lastVisible < feWindowStart + feEntryCount;
}

static String feClip(String value, int maxChars) {
    if (maxChars <= 0) return "";
    if (value.length() <= maxChars) return value;
    if (maxChars <= 1) return value.substring(0, maxChars);
    return value.substring(0, maxChars - 1) + "~";
}

static String feNormalizePath(String path) {
    path.replace('\\', '/');
    path.trim();
    if (path.length() == 0) return "/";
    if (!path.startsWith("/")) path = "/" + path;
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    return path;
}

static String feParentPath(String path) {
    path = feNormalizePath(path);
    if (path == "/") return "/";
    int slash = path.lastIndexOf('/');
    if (slash <= 0) return "/";
    return path.substring(0, slash);
}

static String feLeafName(String path) {
    path = feNormalizePath(path);
    if (path == "/") return "/";
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

static uint32_t feHashPath(const String &path) {
    uint32_t hash = 2166136261UL;
    for (int i = 0; i < path.length(); ++i) {
        hash ^= (uint8_t)path[i];
        hash *= 16777619UL;
    }
    return hash;
}

static String feHex8(uint32_t value) {
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", (unsigned long)value);
    return String(buf);
}

static String fileExplorerTextCachePath(const String &path) {
    return String("/fe_txt_") + feHex8(feHashPath(feNormalizePath(path))) + ".txt";
}

static bool feParseTreeRow(String line, char &type, uint32_t &size, bool &textCached, String &path) {
    line.trim();
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    if (p1 <= 0 || p2 <= p1 || p3 <= p2) return false;
    type = line.charAt(0);
    size = (uint32_t)line.substring(p1 + 1, p2).toInt();
    textCached = line.substring(p2 + 1, p3).toInt() != 0;
    path = feNormalizePath(line.substring(p3 + 1));
    return (type == 'D' || type == 'F') && path.length() > 0;
}

static bool feImmediateChildName(const String &path, const String &dir, String &name) {
    String normalizedPath = feNormalizePath(path);
    String normalizedDir = feNormalizePath(dir);
    if (normalizedPath == normalizedDir) return false;

    if (normalizedDir == "/") {
        if (!normalizedPath.startsWith("/") || normalizedPath.length() <= 1) return false;
        String rest = normalizedPath.substring(1);
        if (rest.indexOf('/') != -1) return false;
        name = rest;
        return true;
    }

    String prefix = normalizedDir + "/";
    if (!normalizedPath.startsWith(prefix)) return false;
    String rest = normalizedPath.substring(prefix.length());
    if (rest.length() == 0 || rest.indexOf('/') != -1) return false;
    name = rest;
    return true;
}

static bool feAddEntry(const String &name, const String &path, bool isDir, uint32_t size, bool textCached) {
    if (!ensureFileExplorerEntries()) return false;
    if (feEntryCount >= FE_MAX_ENTRIES) return false;
    feCopyString(feEntries[feEntryCount].name, FE_NAME_MAX, name);
    feCopyString(feEntries[feEntryCount].path, FE_PATH_MAX, feNormalizePath(path));
    feEntries[feEntryCount].isDir = isDir;
    feEntries[feEntryCount].textCached = textCached;
    feEntries[feEntryCount].size = size;
    feEntryCount++;
    return true;
}

static void feConsiderEntry(const String &name, const String &path, bool isDir, uint32_t size, bool textCached) {
    int absoluteIndex = feTotalCount++;
    if (absoluteIndex < feWindowStart || feEntryCount >= FE_MAX_ENTRIES) return;
    feAddEntry(name, path, isDir, size, textCached);
}

static bool feEntryLess(const FileExplorerEntry &a, const FileExplorerEntry &b) {
    if (a.isDir != b.isDir) return a.isDir;
    return feCaseCompare(a.name, b.name) < 0;
}

static void feSortEntries() {
    int start = (feEntryCount > 0 && strcmp(feEntries[0].name, "..") == 0) ? 1 : 0;
    for (int i = start; i < feEntryCount - 1; ++i) {
        for (int j = i + 1; j < feEntryCount; ++j) {
            if (feEntryLess(feEntries[j], feEntries[i])) {
                FileExplorerEntry tmp = feEntries[i];
                feEntries[i] = feEntries[j];
                feEntries[j] = tmp;
            }
        }
    }
}

static void feClampSelection() {
    if (feTotalCount <= 0) {
        feSelected = 0;
        feScroll = 0;
        return;
    }
    if (feSelected < 0) feSelected = 0;
    if (feSelected >= feTotalCount) feSelected = feTotalCount - 1;
    if (feScroll > feSelected) feScroll = feSelected;
    if (feSelected >= feScroll + FE_VISIBLE_ROWS) feScroll = feSelected - FE_VISIBLE_ROWS + 1;
    int maxScroll = max(0, feTotalCount - FE_VISIBLE_ROWS);
    if (feScroll > maxScroll) feScroll = maxScroll;
    if (feScroll < 0) feScroll = 0;
}

static void feLoadDirectory() {
    feResetEntryWindow();
    String current = feNormalizePath(String(feCurrentPath));
    if (current != "/") {
        feConsiderEntry("..", feParentPath(current), true, 0, false);
    }

    File cache = SPIFFS.open(FILE_TREE_CACHE_PATH, FILE_READ);
    if (!cache) {
        return;
    }

    while (cache.available()) {
        String line = cache.readStringUntil('\n');
        char type = 0;
        uint32_t size = 0;
        bool textCached = false;
        String path;
        if (!feParseTreeRow(line, type, size, textCached, path)) continue;
        String name;
        if (!feImmediateChildName(path, current, name)) continue;
        feConsiderEntry(name, path, type == 'D', size, textCached);
    }
    cache.close();
}

static void feLoadSearch() {
    feResetEntryWindow();
    String query = feLower(String(feSearchQuery));
    if (query.length() == 0) {
        feLoadDirectory();
        return;
    }

    File cache = SPIFFS.open(FILE_TREE_CACHE_PATH, FILE_READ);
    if (!cache) {
        return;
    }

    while (cache.available()) {
        String line = cache.readStringUntil('\n');
        char type = 0;
        uint32_t size = 0;
        bool textCached = false;
        String path;
        if (!feParseTreeRow(line, type, size, textCached, path)) continue;
        String lowerPath = feLower(path);
        if (lowerPath.indexOf(query) == -1) continue;
        feConsiderEntry(feLeafName(path), path, type == 'D', size, textCached);
    }
    cache.close();
}

static void feLoadCurrentEntries() {
    if (feSearchActive) feLoadSearch();
    else feLoadDirectory();
}

static void feRefreshEntries(bool keepSelection) {
    int oldSelected = feSelected;
    int oldScroll = feScroll;
    if (!keepSelection) {
        feSelected = 0;
        feScroll = 0;
    }
    feLoadCurrentEntries();
    if (keepSelection) {
        feSelected = oldSelected;
        feScroll = oldScroll;
    }
    int loadedForScroll = feWindowStart;
    feClampSelection();
    if (feScroll != loadedForScroll || !feVisibleRangeLoaded() || (feTotalCount > 0 && !feLoadedEntry(feSelected))) {
        feLoadCurrentEntries();
        feClampSelection();
    }
    Serial.printf("[FE] %s entries=%d selected=%d path=%s\n",
                  feSearchActive ? "search" : "dir",
                  feTotalCount,
                  feSelected,
                  feSearchActive ? feSearchQuery : feCurrentPath);
    Serial.printf("[FE] window start=%d loaded=%d\n", feWindowStart, feEntryCount);
}

static void feSetPath(const String &path) {
    feCopyString(feCurrentPath, FE_PATH_MAX, feNormalizePath(path));
    Serial.printf("[FE] path %s\n", feCurrentPath);
    feSearchActive = false;
    feSearchQuery[0] = '\0';
    feMode = FE_MODE_BROWSE;
    feRefreshEntries(false);
}

static void feDrawFileIcon(bool isDir, bool textCached, int x, int y, uint32_t color) {
    if (isDir) {
        videodisplay.rect(x, y + 3, 12, 8, color);
        videodisplay.fillRect(x + 1, y + 1, 5, 3, color);
        videodisplay.line(x + 6, y + 3, x + 12, y + 3, color);
        return;
    }
    videodisplay.rect(x + 2, y, 9, 11, color);
    videodisplay.line(x + 8, y, x + 11, y + 3, color);
    if (textCached) {
        videodisplay.line(x + 4, y + 5, x + 9, y + 5, color);
        videodisplay.line(x + 4, y + 8, x + 9, y + 8, color);
    }
}

static int feClientX() { return kernelWindowClientX(); }
static int feClientY() { return kernelWindowClientY(); }
static int feClientW() { return kernelWindowClientW(); }
static int feListX() { return feClientX() + FE_LIST_X; }
static int feListY() { return feClientY() + FE_LIST_Y; }
static int fePreviewX() { return feClientX() + FE_PREVIEW_X; }

static void feDrawHeader() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t cyan = videodisplay.RGB(36, 210, 193);
    uint32_t dark = videodisplay.RGB(22, 55, 74);

    int ox = feClientX();
    int oy = feClientY();
    videodisplay.fillRect(ox, oy, feClientW(), 27, dark);
    videodisplay.fillRect(ox, oy + 26, feClientW(), 1, cyan);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, dark);
    videodisplay.setCursor(ox + 8, oy + 9);
    videodisplay.print("SD");
    videodisplay.rect(ox + 38, oy + 6, 246, 14, white);
    videodisplay.setCursor(ox + 43, oy + 10);
    videodisplay.print(feClip(String(feCurrentPath), 34).c_str());

    videodisplay.rect(ox + 290, oy + 6, 76, 14, white);
    videodisplay.setCursor(ox + 295, oy + 10);
    if (feSearchActive) {
        videodisplay.print(feClip(String("Find ") + String(feSearchQuery), 11).c_str());
    } else {
        videodisplay.print("Find");
    }
}

static void feDrawSidebar() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t dim = videodisplay.RGB(120,120,120);
    int ox = feClientX();
    int oy = feClientY();
    videodisplay.rect(ox + 6, oy + 32, 66, FE_PANEL_H, white);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, 0);
    videodisplay.setCursor(ox + 12, oy + 42); videodisplay.print("SD Card");
    videodisplay.line(ox + 11, oy + 55, ox + 66, oy + 55, dim);
    videodisplay.setCursor(ox + 12, oy + 65); videodisplay.print("1 Root");
    videodisplay.setCursor(ox + 12, oy + 79); videodisplay.print("2 Apps");
    videodisplay.setCursor(ox + 12, oy + 93); videodisplay.print("3 UserData");
    videodisplay.setCursor(ox + 12, oy + 107); videodisplay.print("4 System");
    videodisplay.setCursor(ox + 12, oy + 133); videodisplay.print("Browse");
    videodisplay.setCursor(ox + 12, oy + 147); videodisplay.print("Search");
    videodisplay.setCursor(ox + 12, oy + 161); videodisplay.print("Preview");
}

static void feDrawListFrame() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t dim = videodisplay.RGB(110,110,110);
    int x = feListX();
    int oy = feClientY();
    videodisplay.rect(x - 4, oy + 32, FE_LIST_W + 8, FE_PANEL_H, white);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, 0);
    videodisplay.setCursor(x, oy + 36);
    videodisplay.print(feSearchActive ? "Search results" : "Name");
    videodisplay.setCursor(x + 126, oy + 36);
    videodisplay.print("Size");
    videodisplay.line(x - 2, oy + 44, x + FE_LIST_W + 2, oy + 44, dim);
}

static void feDrawEntryRow(int row) {
    int idx = feScroll + row;
    int x = feListX();
    int y = feListY() + row * FE_ROW_H;
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t selectedBg = videodisplay.RGB(30, 103, 139);
    uint32_t regularBg = videodisplay.RGB(8, 13, 16);
    uint32_t fg = white;
    uint32_t bg = (idx == feSelected) ? selectedBg : regularBg;

    videodisplay.fillRect(x - 2, y - 1, FE_LIST_W + 4, FE_ROW_H, bg);
    if (idx >= feTotalCount) return;

    FileExplorerEntry *entry = feLoadedEntry(idx);
    if (!entry) return;
    uint32_t iconColor = entry->isDir ? videodisplay.RGB(247, 183, 67) :
                         (entry->textCached ? videodisplay.RGB(62, 211, 190) : videodisplay.RGB(108, 166, 224));
    if (idx == feSelected) iconColor = white;
    feDrawFileIcon(entry->isDir, entry->textCached, x + 2, y, iconColor);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(fg, bg);
    videodisplay.setCursor(x + 18, y + 2);
    videodisplay.print(feClip(String(entry->name), 16).c_str());
    videodisplay.setCursor(x + 128, y + 2);
    if (entry->isDir) {
        videodisplay.print("<DIR>");
    } else if (entry->size < 1024) {
        videodisplay.print(String(entry->size).c_str());
    } else {
        videodisplay.print((String(entry->size / 1024) + "K").c_str());
    }
}

static void feDrawPreviewLines(const String &path, int x, int y, int maxLines, int skipLines) {
    File preview = SPIFFS.open(fileExplorerTextCachePath(path), FILE_READ);
    if (!preview) return;

    int skipped = 0;
    while (preview.available() && skipped < skipLines) {
        preview.readStringUntil('\n');
        skipped++;
    }

    uint32_t white = videodisplay.RGB(255,255,255);
    videodisplay.setTextColor(white, 0);
    videodisplay.setFont(Font6x8);
    for (int line = 0; line < maxLines && preview.available(); ++line) {
        String text = preview.readStringUntil('\n');
        text.replace("\r", "");
        videodisplay.setCursor(x, y + line * 9);
        videodisplay.print(feClip(text, 15).c_str());
    }
    preview.close();
}

static int feCountCachedTextLines(const String &path) {
    File preview = SPIFFS.open(fileExplorerTextCachePath(path), FILE_READ);
    if (!preview) return 0;
    int lines = 0;
    while (preview.available()) {
        preview.readStringUntil('\n');
        lines++;
    }
    preview.close();
    return lines;
}

static void feDrawPreview() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t dim = videodisplay.RGB(120,120,120);
    int x = fePreviewX();
    int oy = feClientY();
    videodisplay.fillRect(x - 4, oy + 32, FE_PREVIEW_W + 8, FE_PANEL_H, 0);
    videodisplay.rect(x - 4, oy + 32, FE_PREVIEW_W + 8, FE_PANEL_H, white);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(dim, 0);
    videodisplay.setCursor(x, oy + 36);
    videodisplay.print("Details");
    videodisplay.line(x - 2, oy + 44, x + FE_PREVIEW_W + 2, oy + 44, dim);

    if (feTotalCount <= 0) {
        videodisplay.setTextColor(white, 0);
        videodisplay.setCursor(x, oy + 56);
        videodisplay.print("Empty");
        return;
    }

    FileExplorerEntry *entry = feLoadedEntry(feSelected);
    if (!entry) {
        videodisplay.setTextColor(white, 0);
        videodisplay.setCursor(x, oy + 56);
        videodisplay.print("Loading");
        return;
    }

    feDrawFileIcon(entry->isDir, entry->textCached, x, oy + 56, white);
    videodisplay.setTextColor(white, 0);
    videodisplay.setCursor(x + 18, oy + 56);
    videodisplay.print(feClip(String(entry->name), 12).c_str());
    videodisplay.setCursor(x, oy + 74);
    videodisplay.print(entry->isDir ? "Folder" : "File");
    videodisplay.setCursor(x, oy + 88);
    videodisplay.print(entry->isDir ? "" : (String(entry->size) + " bytes").c_str());
    videodisplay.setCursor(x, oy + 106);
    videodisplay.print(feClip(String(entry->path), 15).c_str());

    if (!entry->isDir) {
        videodisplay.setCursor(x, oy + 128);
        videodisplay.print(entry->textCached ? "Text preview" : "No text view");
        if (entry->textCached) {
            feDrawPreviewLines(String(entry->path), x, oy + 142, 6, 0);
        }
    }
}

static void feDrawStatusBar() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t dark = videodisplay.RGB(20,20,20);
    int x = feClientX();
    int y = kernelWindow.y + kernelWindow.h - 31;
    videodisplay.fillRect(x, y, feClientW(), 30, dark);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(white, dark);
    videodisplay.setCursor(x + 8, y + 6);
    String status = String(feTotalCount) + " item";
    if (feTotalCount != 1) status += "s";
    if (feSearchActive) status += " found";
    if (feTotalCount > 0) {
        status += "  ";
        status += String(feSelected + 1);
        status += "/";
        status += String(feTotalCount);
    }
    videodisplay.print(status.c_str());
    videodisplay.setCursor(x + 8, y + 18);
    videodisplay.print("Enter Open  Left Back  S Find  R Refresh");
}

static void feDrawBrowseScreen() {
    videodisplay.fillRect(feClientX(), feClientY(), feClientW(), kernelWindowClientH(), 0);
    feDrawHeader();
    feDrawSidebar();
    feDrawListFrame();
    for (int row = 0; row < FE_VISIBLE_ROWS; ++row) feDrawEntryRow(row);
    feDrawPreview();
    feDrawStatusBar();
}

static void feDrawListAndPreview() {
    videodisplay.fillRect(feListX() - 2, feListY() - 1, FE_LIST_W + 4, FE_VISIBLE_ROWS * FE_ROW_H + 2, 0);
    for (int row = 0; row < FE_VISIBLE_ROWS; ++row) feDrawEntryRow(row);
    feDrawPreview();
    feDrawStatusBar();
}

static void feDrawViewer() {
    uint32_t white = videodisplay.RGB(255,255,255);
    uint32_t dim = videodisplay.RGB(120,120,120);
    uint32_t dark = videodisplay.RGB(20,20,20);
    int x = feClientX();
    int y = feClientY();
    int w = feClientW();
    videodisplay.fillRect(x, y, w, kernelWindowClientH(), 0);
    videodisplay.fillRect(x, y, w, 26, dark);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(white, dark);
    videodisplay.setCursor(x + 8, y + 8);
    videodisplay.print(feClip(String(feViewerTitle), 28).c_str());
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(x + w - 78, y + 10);
    videodisplay.print((String(feViewerSize) + " B").c_str());
    videodisplay.rect(x + 6, y + 32, w - 12, 177, white);
    videodisplay.setTextColor(dim, 0);
    videodisplay.setCursor(x + 12, y + 36);
    videodisplay.print(feClip(String(feViewerPath), 58).c_str());
    videodisplay.line(x + 10, y + 47, x + w - 10, y + 47, dim);

    videodisplay.setTextColor(white, 0);
    if (!feViewerTextCached) {
        videodisplay.setCursor(x + 18, y + 68);
        videodisplay.print("No cached text preview for this file.");
    } else {
        feDrawPreviewLines(String(feViewerPath), x + 14, y + 55, 17, feViewerScrollLine);
    }

    int statusY = kernelWindow.y + kernelWindow.h - 31;
    videodisplay.fillRect(x, statusY, w, 30, dark);
    videodisplay.setTextColor(white, dark);
    videodisplay.setCursor(x + 8, statusY + 6);
    videodisplay.print((String("Line ") + String(feViewerScrollLine + 1) + "/" + String(max(1, feViewerLineCount))).c_str());
    videodisplay.setCursor(x + 8, statusY + 18);
    videodisplay.print("Up/Down Scroll  Left Back  Esc Close");
}

static void feOpenViewer(const FileExplorerEntry &entry) {
    feMode = FE_MODE_VIEW;
    feCopyString(feViewerPath, FE_PATH_MAX, String(entry.path));
    feCopyString(feViewerTitle, FE_NAME_MAX, String(entry.name));
    feViewerSize = entry.size;
    feViewerTextCached = entry.textCached;
    feViewerScrollLine = 0;
    feViewerLineCount = entry.textCached ? feCountCachedTextLines(String(entry.path)) : 0;
    Serial.printf("[FE] view %s text=%d lines=%d\n",
                  feViewerPath,
                  feViewerTextCached ? 1 : 0,
                  feViewerLineCount);
    feDrawViewer();
}

static void feMoveSelection(int delta) {
    if (feTotalCount <= 0) return;
    int oldSelected = feSelected;
    int oldScroll = feScroll;
    feSelected += delta;
    if (feSelected < 0) feSelected = feTotalCount - 1;
    if (feSelected >= feTotalCount) feSelected = 0;
    feClampSelection();
    if (!feVisibleRangeLoaded() || !feLoadedEntry(feSelected)) {
        feLoadCurrentEntries();
        feClampSelection();
    }
    if (oldSelected != feSelected || oldScroll != feScroll) feDrawListAndPreview();
    FileExplorerEntry *entry = feLoadedEntry(feSelected);
    if (entry) {
        Serial.printf("[FE] select %d/%d %s\n",
                      feSelected + 1,
                      feTotalCount,
                      entry->path);
    }
}

static void feOpenSelected() {
    if (feTotalCount <= 0) return;
    FileExplorerEntry entry;
    if (!feCopyLoadedEntry(feSelected, entry)) {
        feRefreshEntries(true);
        if (!feCopyLoadedEntry(feSelected, entry)) return;
    }
    if (entry.isDir) {
        feSetPath(String(entry.path));
        feDrawBrowseScreen();
    } else {
        feOpenViewer(entry);
    }
}

static void feAppendSearchChar(const String &key) {
    String value = key;
    if (value == "Space") value = " ";
    if (value.length() != 1) return;
    char ch = value.charAt(0);
    if ((ch < 32 || ch > 126) || strlen(feSearchQuery) >= FE_SEARCH_MAX - 1) return;
    int len = strlen(feSearchQuery);
    feSearchQuery[len] = ch;
    feSearchQuery[len + 1] = '\0';
}

static void requestFileExplorerTreeRefresh() {
    File flag = SPIFFS.open(FILE_TREE_REFRESH_FLAG_PATH, FILE_WRITE);
    if (flag) {
        flag.print("1");
        flag.close();
    }
    Serial.println("Refreshing file explorer cache on restart.");
    ESP.restart();
}

static void handleFileExplorerInput(String input) {
    input.trim();
    if (input.length() == 0 || input.indexOf("rlsd") != -1) return;
    KernelCursorDrawGuard cursorGuard;
    Serial.println(input);

    if (feMode == FE_MODE_VIEW) {
        if (input == "Escape" || input == "LeftArrow" || input == "Backspace") {
            feMode = FE_MODE_BROWSE;
            feDrawBrowseScreen();
        } else if (input == "UpArrow") {
            if (feViewerScrollLine > 0) feViewerScrollLine--;
            feDrawViewer();
        } else if (input == "DownArrow") {
            int maxScroll = max(0, feViewerLineCount - 18);
            if (feViewerScrollLine < maxScroll) feViewerScrollLine++;
            feDrawViewer();
        } else if (input == "PageUp") {
            feViewerScrollLine = max(0, feViewerScrollLine - 12);
            feDrawViewer();
        } else if (input == "PageDown") {
            int maxScroll = max(0, feViewerLineCount - 18);
            feViewerScrollLine = min(maxScroll, feViewerScrollLine + 12);
            feDrawViewer();
        }
        return;
    }

    if (feSearchActive) {
        if (input == "Escape") {
            feSearchActive = false;
            feSearchQuery[0] = '\0';
            feRefreshEntries(false);
            feDrawBrowseScreen();
        } else if (input == "Backspace") {
            int len = strlen(feSearchQuery);
            if (len > 0) feSearchQuery[len - 1] = '\0';
            else feSearchActive = false;
            feRefreshEntries(false);
            feDrawBrowseScreen();
        } else if (input == "Enter" || input == "RightArrow") {
            feOpenSelected();
        } else if (input == "UpArrow") {
            feMoveSelection(-1);
        } else if (input == "DownArrow") {
            feMoveSelection(1);
        } else if (input == "PageUp") {
            feMoveSelection(-FE_VISIBLE_ROWS);
        } else if (input == "PageDown") {
            feMoveSelection(FE_VISIBLE_ROWS);
        } else {
            feAppendSearchChar(input);
            feRefreshEntries(false);
            feDrawBrowseScreen();
        }
        return;
    }

    if (input == "Escape") {
        if (!restoreAndCloseKernelWindow()) home();
    } else if (input == "UpArrow") {
        feMoveSelection(-1);
    } else if (input == "DownArrow") {
        feMoveSelection(1);
    } else if (input == "PageUp") {
        feMoveSelection(-FE_VISIBLE_ROWS);
    } else if (input == "PageDown") {
        feMoveSelection(FE_VISIBLE_ROWS);
    } else if (input == "Enter" || input == "RightArrow") {
        feOpenSelected();
    } else if (input == "LeftArrow" || input == "Backspace") {
        feSetPath(feParentPath(String(feCurrentPath)));
        feDrawBrowseScreen();
    } else if (input == "s" || input == "S" || input == "/" || input == "Slash") {
        feSearchActive = true;
        feSearchQuery[0] = '\0';
        feRefreshEntries(false);
        feDrawBrowseScreen();
    } else if (input == "r" || input == "R") {
        requestFileExplorerTreeRefresh();
    } else if (input == "1") {
        feSetPath("/");
        feDrawBrowseScreen();
    } else if (input == "2") {
        feSetPath("/apps");
        feDrawBrowseScreen();
    } else if (input == "3") {
        feSetPath("/UserData");
        feDrawBrowseScreen();
    } else if (input == "4") {
        feSetPath("/System");
        feDrawBrowseScreen();
    }
}

void listSavedFiles() {
    feDrawBrowseScreen();
}

static void drawFileExplorerWindowContent() {
    if (feMode == FE_MODE_VIEW) feDrawViewer();
    else feDrawBrowseScreen();
}
 
void fileExplorer(){
  applaunched = true;
  fE = true;
  util = false;
  shell = false;
  configurationPending = false;
  selection = false;
  dragvalve = 1;
  previousDragValve = -1;
  feSetPath("/");
  openKernelWindow(KERNEL_WINDOW_FILES, "Files", 374, 270, drawFileExplorerWindowContent);
}

static bool cursorPointInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return Citadela::UI::pointInRect(x, y, rx, ry, rw, rh);
}

static int cursorHomeButtonAt(int x, int y) {
    const int bx = 15;
    const int by = 18;
    const int size = 20;
    const int spacing = 10;
    for (int i = 0; i < 6; ++i) {
        int iy = by + i * (size + spacing);
        if (cursorPointInRect(x, y, bx - 4, iy - 3, size + 28, size + 6)) return i + 1;
    }
    return 0;
}

static int cursorConfigItemAt(int x, int y) {
    for (int item = 1; item <= CONFIG_ITEM_COUNT; ++item) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (configItemGeometry(item, rx, ry, rw, rh) && cursorPointInRect(x, y, rx, ry, rw, rh)) {
            return item;
        }
    }
    return 0;
}

static int cursorManualTimeFieldAt(int x, int y) {
    if (cursorPointInRect(x, y, 72, 90, 62, 22)) return 0;
    if (cursorPointInRect(x, y, 144, 90, 48, 22)) return 1;
    if (cursorPointInRect(x, y, 202, 90, 48, 22)) return 2;
    if (cursorPointInRect(x, y, 72, 140, 48, 22)) return 3;
    if (cursorPointInRect(x, y, 132, 140, 48, 22)) return 4;
    if (cursorPointInRect(x, y, 192, 140, 48, 22)) return 5;
    if (cursorPointInRect(x, y, 80, 202, 74, 18)) return 6;
    if (cursorPointInRect(x, y, 174, 202, 82, 18)) return 7;
    return -1;
}

static int cursorWallpaperRowAt(int x, int y) {
    if (!cursorPointInRect(x, y, WALLPAPER_LIST_X, WALLPAPER_LIST_Y,
                           WALLPAPER_LIST_W, WALLPAPER_LIST_VISIBLE * WALLPAPER_LIST_ROW_H)) {
        return -1;
    }
    int row = (y - WALLPAPER_LIST_Y) / WALLPAPER_LIST_ROW_H;
    int idx = wallpaperScroll + row;
    return (idx >= 0 && idx < wallpaperEntryCount) ? idx : -1;
}

static int cursorWallpaperActionAt(int x, int y) {
    if (cursorPointInRect(x, y, WALLPAPER_COLOR_X, WALLPAPER_ACTION_Y, 96, WALLPAPER_ACTION_H)) return 1;
    if (cursorPointInRect(x, y, WALLPAPER_SAVE_X, WALLPAPER_ACTION_Y, WALLPAPER_ACTION_W, WALLPAPER_ACTION_H)) return 2;
    if (cursorPointInRect(x, y, WALLPAPER_CANCEL_X, WALLPAPER_ACTION_Y, WALLPAPER_ACTION_W, WALLPAPER_ACTION_H)) return 3;
    return 0;
}

static int cursorUtilityEntryAt(int x, int y) {
    for (int i = 0; i < appFileCount; ++i) {
        int rx = 0, ry = 0, rw = 0, rh = 0;
        if (utilityEntryGeometry(i, rx, ry, rw, rh) && cursorPointInRect(x, y, rx, ry, rw, rh)) return i;
    }
    return -1;
}

static int cursorFileExplorerRowAt(int x, int y) {
    if (!cursorPointInRect(x, y, feListX() - 4, feListY() - 2,
                           FE_LIST_W + 8, FE_VISIBLE_ROWS * FE_ROW_H + 4)) return -1;
    int row = (y - feListY()) / FE_ROW_H;
    if (row < 0 || row >= FE_VISIBLE_ROWS) return -1;
    int idx = feScroll + row;
    return (idx >= 0 && idx < feTotalCount) ? idx : -1;
}

static void activateHomeItem(int item) {
    homeSelectionIndex = constrain(item, 1, 6);
    switch (item) {
        case 1: Serial.println("Loading DiskDrum"); utilitiesMenu(); break;
        case 2: Serial.println("Loading DiskDrum"); fileExplorer(); break;
        case 3: boolRes = "trueBootloader"; boolUpdate(); restartWithFallbackVideo("Preparing bootloader flash"); break;
        case 4: Serial.println("Configure System"); configurationDirty = false; configureMenu(); break;
        case 5: boolRes = "trueCprog"; boolUpdate(); restartWithFallbackVideo("Preparing CProg flash"); break;
        case 6: CitCommandPPT(); break;
    }
}

static void cursorSelectFileExplorerIndex(int idx, bool openItem) {
    if (idx < 0 || idx >= feTotalCount) return;
    int oldSelected = feSelected;
    int oldScroll = feScroll;
    feSelected = idx;
    feClampSelection();
    if (!feVisibleRangeLoaded() || !feLoadedEntry(feSelected)) {
        feRefreshEntries(true);
        feSelected = constrain(idx, 0, max(0, feTotalCount - 1));
        feClampSelection();
    }
    if (oldSelected != feSelected || oldScroll != feScroll) feDrawListAndPreview();
    if (openItem) feOpenSelected();
}

static void cursorSetIconThresholdFromX(int x) {
    int maxThreshold = currentIconThresholdMax();
    int relative = constrain(x - ICON_THRESHOLD_SLIDER_X, 0, ICON_THRESHOLD_SLIDER_W - 1);
    int value = map(relative, 0, ICON_THRESHOLD_SLIDER_W - 1, 0, maxThreshold);
    int oldValue = currentIconThresholdValue();
    setCurrentIconThresholdValue(value);
    if (currentIconThresholdValue() == oldValue) return;
    iconThresholdDirty = true;
    KernelCursorDrawGuard cursorGuard;
    drawIconThresholdSlider();
    redrawHomeIconsForThreshold();
}

static void kernelCursorUpdateHover(int x, int y, bool leftDown) {
    bool windowModalOpen = manualTimePending || iconThresholdPending || wallpaperPickerPending || uartUploadPending || emBreak;
    if (kernelWindow.active && kernelWindow.dragging && !windowModalOpen) {
        moveKernelWindowToMouse(x, y, false);
        return;
    }

    if (wallpaperPickerPending) {
        int idx = cursorWallpaperRowAt(x, y);
        if (leftDown && idx >= 0 && idx != wallpaperSelected) wallpaperPickerSelect(idx);
        return;
    }

    if (uartUploadPending) return;

    if (iconThresholdPending) {
        if (leftDown && cursorPointInRect(x, y, ICON_THRESHOLD_SLIDER_X - 4, ICON_THRESHOLD_SLIDER_Y - 12,
                                          ICON_THRESHOLD_SLIDER_W + 8, 28)) {
            cursorSetIconThresholdFromX(x);
        }
        return;
    }

    if (manualTimePending) {
        int field = cursorManualTimeFieldAt(x, y);
        if (field >= 0 && field != manualTimeField) {
            KernelCursorDrawGuard cursorGuard;
            int oldField = manualTimeField;
            manualTimeField = field;
            drawManualTimeItem(oldField);
            drawManualTimeItem(manualTimeField);
        }
        return;
    }

    if (bluetoothConnectionPending || wifiConnectionPending) {
        if (connectionStage == CONNECTION_STAGE_LIST) {
            int idx = cursorConnectionRowAt(x, y);
            if (idx >= 0 && idx != connectionSelected) setConnectionSelection(idx);
        }
        return;
    }

    if (configurationPending) {
        int item = cursorConfigItemAt(x, y);
        if (item > 0 && item != dragvalve) {
            dragvalve = item;
        }
        return;
    }

    if (fE && feMode == FE_MODE_BROWSE) {
        int idx = cursorFileExplorerRowAt(x, y);
        if (idx >= 0 && idx != feSelected) {
            KernelCursorDrawGuard cursorGuard;
            cursorSelectFileExplorerIndex(idx, false);
        }
        return;
    }

    if (util) {
        int idx = cursorUtilityEntryAt(x, y);
        if (idx >= 0 && idx != dragvalve) {
            dragvalve = idx;
            handleNavigation();
        }
        return;
    }

    if (!applaunched && !shell && !emBreak && !warning) {
        int item = cursorHomeButtonAt(x, y);
        if (item > 0 && item != dragvalve) {
            dragvalve = item;
        }
    }
}

static void kernelCursorActivateClick(int x, int y) {
    bool windowModalOpen = manualTimePending || iconThresholdPending || wallpaperPickerPending || uartUploadPending || emBreak;
    if (kernelWindow.active && kernelWindow.dragging && !windowModalOpen) {
        moveKernelWindowToMouse(x, y, true);
        return;
    }
    if (kernelWindow.active && !windowModalOpen) {
        if (kernelWindowCloseButtonAt(x, y)) {
            if (kernelWindow.kind == KERNEL_WINDOW_CONFIG) {
                closeConfigurationPanelFromInput();
            } else if (kernelWindow.kind == KERNEL_WINDOW_CONNECTION) {
                closeConnectionMenu();
            } else if (!restoreAndCloseKernelWindow()) {
                home();
            }
            return;
        }
        if (kernelWindowTitleAt(x, y)) {
            beginKernelWindowDrag(x, y);
            return;
        }
    }

    if (wallpaperPickerPending) {
        int idx = cursorWallpaperRowAt(x, y);
        if (idx >= 0) {
            wallpaperPickerSelect(idx);
            return;
        }
        int action = cursorWallpaperActionAt(x, y);
        if (action == 1) wallpaperPickerToggleColor();
        else if (action == 2) closeWallpaperPickerMenu(true);
        else if (action == 3) closeWallpaperPickerMenu(false);
        return;
    }

    if (uartUploadPending) {
        if (!uartUploadInProgress && uartUploadCancelAt(x, y)) handleUARTUploadInput("Escape");
        return;
    }

    if (iconThresholdPending) {
        if (cursorPointInRect(x, y, ICON_THRESHOLD_SLIDER_X - 4, ICON_THRESHOLD_SLIDER_Y - 12,
                              ICON_THRESHOLD_SLIDER_W + 8, 28)) {
            cursorSetIconThresholdFromX(x);
        }
        return;
    }

    if (manualTimePending) {
        int field = cursorManualTimeFieldAt(x, y);
        if (field < 0) return;
        if (field == 6) {
            closeManualTimeMenu(true);
        } else if (field == 7) {
            closeManualTimeMenu(false);
        } else {
            KernelCursorDrawGuard cursorGuard;
            int oldField = manualTimeField;
            manualTimeField = field;
            drawManualTimeItem(oldField);
            drawManualTimeItem(manualTimeField);
        }
        return;
    }

    if (bluetoothConnectionPending || wifiConnectionPending) {
        if (connectionStage == CONNECTION_STAGE_LIST) {
            int idx = cursorConnectionRowAt(x, y);
            if (idx >= 0) setConnectionSelection(idx);
        }
        int action = cursorConnectionActionAt(x, y);
        if (action == 1) startConnectionScan();
        else if (action == 2) {
            if (connectionStage == CONNECTION_STAGE_LIST) beginSelectedConnection();
            else submitWiFiConnection();
        } else if (action == 3 && wifiConnectionPending) {
            wifiPasswordVisible = !wifiPasswordVisible;
            refreshConnectionWindow();
        } else if (action == 4 && wifiConnectionPending && connectionStage != CONNECTION_STAGE_CONNECTING) {
            connectionStage = CONNECTION_STAGE_LIST;
            connectionStatus = String(connectionEntryCount) + " network(s) found";
            refreshConnectionWindow();
        }
        return;
    }

    if (configurationPending) {
        int item = cursorConfigItemAt(x, y);
        if (item > 0) {
            dragvalve = item;
            activateConfigurationItem(item);
        }
        return;
    }

    if (fE && feMode == FE_MODE_BROWSE) {
        int idx = cursorFileExplorerRowAt(x, y);
        if (idx >= 0) {
            KernelCursorDrawGuard cursorGuard;
            cursorSelectFileExplorerIndex(idx, true);
        }
        return;
    }

    if (util) {
        int idx = cursorUtilityEntryAt(x, y);
        if (idx >= 0) {
            dragvalve = idx;
            handleNavigation();
            handleSelection();
        }
        return;
    }

    if (!applaunched && !shell && !emBreak) {
        int item = cursorHomeButtonAt(x, y);
        if (item > 0) {
            dragvalve = item;
            activateHomeItem(item);
        }
    }
}

static String serialControlKeyName(String cmd) {
    cmd.trim();
    String low = cmd;
    low.toLowerCase();
    if (low == "up" || low == "uparrow") return "UpArrow";
    if (low == "down" || low == "downarrow") return "DownArrow";
    if (low == "left" || low == "leftarrow") return "LeftArrow";
    if (low == "right" || low == "rightarrow") return "RightArrow";
    if (low == "enter" || low == "open") return "Enter";
    if (low == "esc" || low == "escape" || low == "close") return "Escape";
    if (low == "back" || low == "backspace") return "Backspace";
    if (low == "pgup" || low == "pageup") return "PageUp";
    if (low == "pgdn" || low == "pagedown") return "PageDown";
    if (low == "space") return "Space";
    if (low == "search") return "S";
    if (low == "refresh") return "R";
    return cmd;
}

static void serialControlDispatchKey(const String &key) {
    if (key.length() == 0) return;
    Serial.printf("[SC] key %s\n", key.c_str());

    if (shell) {
        handleShellInput(key);
        return;
    }

    if (wallpaperPickerPending) {
        handleWallpaperPickerInput(key);
        return;
    }
    if (uartUploadPending) {
        handleUARTUploadInput(key);
        return;
    }
    if (iconThresholdPending) {
        handleIconThresholdInput(key);
        return;
    }
    if (manualTimePending) {
        handleManualTimeInput(key);
        return;
    }
    if (bluetoothConnectionPending || wifiConnectionPending) {
        handleConnectionInput(key);
        return;
    }
    if (configurationPending) {
        handleConfigurationInput(key);
        return;
    }
    if (fE) {
        handleFileExplorerInput(key);
        return;
    }

    const int UTIL_COLUMNS = 5;
    if (key == "UpArrow") {
        if (util && appFileCount > 0) {
            int cols = UTIL_COLUMNS;
            int rows = (appFileCount + cols - 1) / cols;
            int col = dragvalve % cols;
            int row = dragvalve / cols;
            if (row > 0) row--; else row = rows - 1;
            int newIdx = row * cols + col;
            while (newIdx >= appFileCount && col > 0) { col--; newIdx = row * cols + col; }
            dragvalve = constrain(newIdx, 0, appFileCount - 1);
            handleNavigation();
        } else if (dragvalve > 1) {
            dragvalve--;
        }
    } else if (key == "DownArrow") {
        if (util && appFileCount > 0) {
            int cols = UTIL_COLUMNS;
            int rows = (appFileCount + cols - 1) / cols;
            int col = dragvalve % cols;
            int row = dragvalve / cols;
            if (row < rows - 1) row++; else row = 0;
            int newIdx = row * cols + col;
            while (newIdx >= appFileCount && col > 0) { col--; newIdx = row * cols + col; }
            dragvalve = constrain(newIdx, 0, appFileCount - 1);
            handleNavigation();
        } else if (dragvalve < 6) {
            dragvalve++;
        }
    } else if (key == "LeftArrow") {
        if (util && appFileCount > 0) {
            if ((dragvalve % UTIL_COLUMNS) > 0) dragvalve--;
            else {
                int row = dragvalve / UTIL_COLUMNS;
                int prevCol = UTIL_COLUMNS - 1;
                int newIdx = row * UTIL_COLUMNS + prevCol;
                while (newIdx >= appFileCount && prevCol > 0) { prevCol--; newIdx = row * UTIL_COLUMNS + prevCol; }
                dragvalve = constrain(newIdx, 0, appFileCount - 1);
            }
            handleNavigation();
        } else if (dragvalve > 1) {
            dragvalve--;
        }
    } else if (key == "RightArrow") {
        if (util && appFileCount > 0) {
            if ((dragvalve % UTIL_COLUMNS) < (UTIL_COLUMNS - 1)) dragvalve++;
            else {
                int row = dragvalve / UTIL_COLUMNS;
                int newIdx = row * UTIL_COLUMNS;
                if (newIdx >= appFileCount) newIdx = 0;
                dragvalve = constrain(newIdx, 0, appFileCount - 1);
            }
            handleNavigation();
        } else if (dragvalve < 6) {
            dragvalve++;
        }
    } else if (key == "Enter") {
        if (util) handleSelection();
        else selection = true;
    } else if (key == "Escape") {
        if (applaunched) {
            if (!restoreAndCloseKernelWindow()) home();
        } else {
            requestElevation("Return to Bootstrap?", 1);
        }
    }
}

static void serialControlHandleCommand(String command) {
    command.trim();
    if (command.length() == 0) return;
    if (handleDisplayMirrorCommand(command)) return;

    String low = command;
    low.toLowerCase();
    if (low == "cituart ping") {
        Serial.println(uartUploadPending && !uartUploadInProgress
            ? "CITUART ARMED"
            : (uartUploadInProgress ? "CITUART BUSY" : "CITUART NOT_ARMED"));
        return;
    }
    if (low.startsWith("cituart begin ")) {
        handleUARTUploadBeginCommand(command);
        return;
    }

    KernelCursorDrawGuard cursorGuard;
    if (low.startsWith("sc ")) {
        command = command.substring(3);
        command.trim();
        low = command;
        low.toLowerCase();
    }

    if (low == "help") {
        Serial.println("[SC] commands: apps, files, utilities, config, bluetooth, wifi, connection status, terminal, wallpaper, home, path <dir>, find <text>, type <text>, mouse <x> <y> <buttons>, up/down/left/right/enter/esc/backspace/pageup/pagedown");
        return;
    }
    if (low == "connection status" || low == "connections") {
        const char *mode = wifiConnectionPending ? "wifi" : (bluetoothConnectionPending ? "bluetooth" : "closed");
        Serial.printf("[SC] connection mode=%s stage=%d scanning=%d count=%d selected=%d status=%s\n",
                      mode, (int)connectionStage, connectionScanning ? 1 : 0,
                      connectionEntryCount, connectionSelected, connectionStatus.c_str());
        for (int i = 0; i < connectionEntryCount; ++i) {
            Serial.printf("[SC] connection[%d] slot=%d rssi=%d name=%s\n",
                          i, connectionEntries[i].controllerIndex,
                          connectionEntries[i].rssi, connectionEntries[i].name);
        }
        return;
    }
    if (low == "apps" || low == "appcache") {
        Serial.printf("[SC] cached apps=%d\n", appFileCount);
        for (int i = 0; i < appFileCount; ++i) {
            String entry;
            Serial.printf("[SC] app[%d]=%s\n", i,
                          readCachedAppEntry(i, entry) ? entry.c_str() : "<cache read failed>");
        }
        return;
    }
    if (low == "terminal" || low == "shell") {
        Serial.println("[SC] open terminal");
        CitCommandPPT();
        return;
    }
    if (low.startsWith("type ") && shell) {
        buildShell(command.substring(5));
        return;
    }
    if (low.startsWith("mouse ")) {
        int x = 0, y = 0, buttons = 0;
        if (sscanf(command.c_str(), "mouse %d %d %d", &x, &y, &buttons) == 3) {
            handleKernelMouseReport(String("MOUSE ") + String(x) + " " + String(y) + " " + String(buttons) + " 0 0 0");
        } else {
            Serial.println("[SC] usage: mouse <x> <y> <buttons>");
        }
        return;
    }
    if (low == "files" || low == "fileexplorer") {
        Serial.println("[SC] open files");
        fileExplorer();
        return;
    }
    if (low == "utilities") {
        Serial.println("[SC] open utilities");
        utilitiesMenu();
        return;
    }
    if (low == "config" || low == "settings") {
        Serial.println("[SC] open config");
        configurationDirty = false;
        configureMenu();
        return;
    }
    if (low == "bluetooth" || low == "ble") {
        Serial.println("[SC] open Bluetooth Connection");
        openBluetoothConnectionMenu();
        return;
    }
    if (low == "wifi" || low == "wireless") {
        Serial.println("[SC] open WiFi Connection");
        openWiFiConnectionMenu();
        return;
    }
    if (low == "wallpaper" || low == "wp") {
        Serial.println("[SC] open wallpaper");
        openWallpaperPickerMenu();
        return;
    }
    if (low == "home") {
        Serial.println("[SC] home");
        fE = false;
        util = false;
        applaunched = false;
        configurationPending = false;
        manualTimePending = false;
        iconThresholdPending = false;
        wallpaperPickerPending = false;
        wallpaperPickerPreviewQueued = false;
        uartUploadPending = false;
        uartUploadInProgress = false;
        bluetoothConnectionPending = false;
        wifiConnectionPending = false;
        selection = false;
        home();
        return;
    }
    if (low == "serialdisplay on" || low == "serialdisplay off") {
        serialDisplayEnabled = low.endsWith(" on");
        writeSystemConfigToSPIFFS();
        serialDisplayScenePending = serialDisplayEnabled && serialDisplayConnected;
        if (!serialDisplayEnabled) {
            serialDisplay.disable();
            Serial1.println("VDM DISABLED");
        }
        Serial.printf("[SC] Serial Display %s\n", serialDisplayEnabled ? "ON" : "OFF");
        return;
    }
    if (low.startsWith("path ")) {
        String path = command.substring(5);
        path.trim();
        if (!fE) fileExplorer();
        feSetPath(path);
        feDrawBrowseScreen();
        return;
    }
    if (low.startsWith("find ")) {
        String query = command.substring(5);
        query.trim();
        if (!fE) fileExplorer();
        feSearchActive = true;
        feCopyString(feSearchQuery, FE_SEARCH_MAX, query);
        Serial.printf("[SC] find %s\n", feSearchQuery);
        feRefreshEntries(false);
        feDrawBrowseScreen();
        return;
    }
    if (low.startsWith("key ")) {
        command = command.substring(4);
        command.trim();
    }

    serialControlDispatchKey(serialControlKeyName(command));
}

static bool handleDisplayMirrorCommand(const String &command) {
    if (!command.startsWith("VDM ")) return false;
    if (serialDisplay.receive(command.c_str())) return true;
    if (command == "VDM HELLO") {
        Serial.println("VDM KERNEL 1");
    } else if (command == "VDM ON" || command == "VDM SNAP") {
        serialDisplayConnected = true;
        if (serialDisplayEnabled) serialDisplayScenePending = true;
        else Serial1.println("VDM DISABLED");
    } else if (command == "VDM OFF") {
        serialDisplayConnected = false;
        serialDisplayScenePending = false;
        serialDisplay.disable();
    } else if (command.startsWith("VDM CHECK ") && serialDisplay.enabled()) {
        int x, y, w, h;
        if (sscanf(command.c_str(), "VDM CHECK %d %d %d %d", &x, &y, &w, &h) == 4 &&
            x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= videodisplay.xres && y + h <= videodisplay.yres) {
            uint32_t hash = 2166136261UL;
            for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) {
                uint32_t rgb = videodisplay.buftocol(videodisplay.rawPixelSignal(videodisplay.getRawPixelFast(xx, yy)));
                for (int i = 0; i < 3; ++i) { hash ^= (rgb >> (i * 8)) & 255; hash *= 16777619UL; }
            }
            serialDisplay.command("CHECK %d %d %d %d %08lX", x, y, w, h, (unsigned long)hash);
        }
    } else if (command.startsWith("VDM INPUT ") && initVd) {
        serialControlHandleCommand(String("SC ") + command.substring(10));
    }
    return true;
}

void SerialControl() {
    static String pcInput = "";
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialControlHandleCommand(pcInput);
            pcInput = "";
        } else if (pcInput.length() < 80) {
            pcInput += c;
        }
    }
}

static void clearFileExplorerPreviewCache() {
    File root = SPIFFS.open("/");
    if (!root) return;
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        String leaf = leafNameFromPath(String(entry.name()));
        entry.close();
        if (leaf.startsWith("fe_txt_")) {
            SPIFFS.remove(String("/") + leaf);
        }
    }
    root.close();
}

static bool fileExplorerKnownTextPath(String path) {
    path.toLowerCase();
    const char *exts[] = {
        ".txt", ".log", ".conf", ".cfg", ".ini", ".json", ".csv", ".tsv",
        ".md", ".xml", ".html", ".htm", ".css", ".js", ".ino", ".c",
        ".cpp", ".h", ".hpp", ".py", ".sh", ".bat", ".ps1", ".yaml",
        ".yml", ".textclipping", ".rtf"
    };
    for (int i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])); ++i) {
        if (path.endsWith(exts[i])) return true;
    }
    return false;
}

static bool fileExplorerLooksText(File &entry) {
    if (!entry) return false;
    if (entry.size() == 0) return true;
    if (!entry.seek(0)) return false;
    int total = 0;
    int printable = 0;
    while (entry.available() && total < 256) {
        int b = entry.read();
        if (b < 0) break;
        if (b == 0) {
            entry.seek(0);
            return false;
        }
        if (b == '\n' || b == '\r' || b == '\t' || (b >= 32 && b <= 126)) printable++;
        total++;
    }
    entry.seek(0);
    if (total == 0) return true;
    return (printable * 100 / total) >= 85;
}

static bool cacheFileExplorerTextPreview(File &entry, const String &path) {
    if (!entry || feTextCachedBytes >= FE_TEXT_CACHE_TOTAL_LIMIT) return false;
    bool textLike = fileExplorerKnownTextPath(path) || fileExplorerLooksText(entry);
    if (!textLike) return false;

    String cachePath = fileExplorerTextCachePath(path);
    SPIFFS.remove(cachePath);
    File preview = SPIFFS.open(cachePath, FILE_WRITE);
    if (!preview) return false;
    if (!entry.seek(0)) {
        preview.close();
        return false;
    }

    int remainingTotal = FE_TEXT_CACHE_TOTAL_LIMIT - feTextCachedBytes;
    int limit = min(FE_TEXT_CACHE_BYTES_PER_FILE, remainingTotal);
    int written = 0;
    uint8_t buf[64];
    while (entry.available() && written < limit) {
        int want = min((int)sizeof(buf), limit - written);
        int got = entry.read(buf, want);
        if (got <= 0) break;
        for (int i = 0; i < got && written < limit; ++i) {
            uint8_t b = buf[i];
            if (b == '\r') continue;
            if (b == '\n' || b == '\t' || (b >= 32 && b <= 126)) {
                preview.write(b);
            } else {
                preview.write('.');
            }
            written++;
        }
    }
    if (entry.available() && written + 22 < limit) {
        preview.print("\n[preview truncated]\n");
        written += 21;
    }
    preview.close();
    entry.seek(0);
    feTextCachedBytes += written;
    return written > 0 || entry.size() == 0;
}

static void cacheFileExplorerDirectory(File &treeCache, const String &dirPath, int depth) {
    if (depth > 5 || feTreeCachedCount >= FE_TREE_CACHE_LIMIT) return;

    File dir = SD.open(dirPath);
    if (!dir) {
        Serial.printf("Failed to open file explorer directory: %s\n", dirPath.c_str());
        return;
    }

    while (feTreeCachedCount < FE_TREE_CACHE_LIMIT) {
        File entry = dir.openNextFile();
        if (!entry) break;

        String leaf = leafNameFromPath(String(entry.name()));
        if (leaf.length() == 0 || leaf.startsWith("._")) {
            entry.close();
            continue;
        }

        String fullPath = dirPath;
        if (!fullPath.endsWith("/")) fullPath += "/";
        if (fullPath == "//") fullPath = "/";
        fullPath += leaf;
        fullPath = feNormalizePath(fullPath);

        bool isDir = entry.isDirectory();
        if (isDir) {
            treeCache.printf("D|0|0|%s\n", fullPath.c_str());
            feTreeCachedCount++;
            cacheFileExplorerDirectory(treeCache, fullPath, depth + 1);
        } else {
            bool textCached = cacheFileExplorerTextPreview(entry, fullPath);
            treeCache.printf("F|%lu|%d|%s\n",
                             (unsigned long)entry.size(),
                             textCached ? 1 : 0,
                             fullPath.c_str());
            feTreeCachedCount++;
        }
        entry.close();
    }

    dir.close();
}

static void cacheAppDirectory(File &appCache, const String &dirPath, const String &relPrefix, int depth) {
    if (depth > 3 || appFileCount >= MAX_FILES) return;

    File dir = SD.open(dirPath);
    if (!dir) {
        Serial.printf("Failed to open app directory: %s\n", dirPath.c_str());
        return;
    }

    while (appFileCount < MAX_FILES) {
        File entry = dir.openNextFile();
        if (!entry) break;

        String leaf = leafNameFromPath(String(entry.name()));
        if (leaf.length() == 0 || leaf.startsWith("._")) {
            entry.close();
            continue;
        }

        bool isDir = entry.isDirectory();
        String relPath = relPrefix.length() ? (relPrefix + "/" + leaf) : leaf;

        if (isDir) {
            String childPath = dirPath;
            if (!childPath.endsWith("/")) childPath += "/";
            childPath += leaf;
            cacheAppDirectory(appCache, childPath, relPath, depth + 1);
            entry.close();
            continue;
        }

        String lowerLeaf = leaf;
        lowerLeaf.toLowerCase();
        if (!lowerLeaf.endsWith(".bin")) {
            entry.close();
            continue;
        }

        writeCachedName(appCache, String("A|") + relPath);
        appFileCount++;
        Serial.print("[APP] ");
        Serial.println(relPath);

        entry.close();
    }

    dir.close();
}

static bool refreshAppCacheFromSD() {
    File appDirectory = SD.open("/apps");
    bool directoryReady = appDirectory && appDirectory.isDirectory();
    if (appDirectory) appDirectory.close();
    if (!directoryReady) {
        Serial.println("App cache refresh skipped: /apps is unavailable; retaining previous cache.");
        return false;
    }

    const char *temporaryPath = "/app_names.scan";
    const char *backupPath = "/app_names.backup";
    SPIFFS.remove(temporaryPath);
    File cache = SPIFFS.open(temporaryPath, FILE_WRITE);
    if (!cache) {
        Serial.println("App cache refresh failed: temporary cache could not be created.");
        return false;
    }

    int previousCount = appFileCount;
    appFileCount = 0;
    cacheAppDirectory(cache, "/apps", "", 0);
    cache.flush();
    cache.close();

    File verification = SPIFFS.open(temporaryPath, FILE_READ);
    bool cacheReady = verification && (appFileCount == 0 || verification.size() > 0);
    if (verification) verification.close();
    if (!cacheReady) {
        SPIFFS.remove(temporaryPath);
        appFileCount = previousCount;
        Serial.println("App cache refresh failed verification; retaining previous cache.");
        return false;
    }

    SPIFFS.remove(backupPath);
    bool hadCache = SPIFFS.exists(APP_NAME_CACHE_PATH);
    if (hadCache && !SPIFFS.rename(APP_NAME_CACHE_PATH, backupPath)) {
        SPIFFS.remove(temporaryPath);
        appFileCount = previousCount;
        Serial.println("App cache refresh failed while preserving the previous cache.");
        return false;
    }
    if (!SPIFFS.rename(temporaryPath, APP_NAME_CACHE_PATH)) {
        if (hadCache) SPIFFS.rename(backupPath, APP_NAME_CACHE_PATH);
        SPIFFS.remove(temporaryPath);
        appFileCount = previousCount;
        Serial.println("App cache refresh failed during final rename.");
        return false;
    }

    SPIFFS.remove(backupPath);
    writeAppCacheSchema();
    Serial.printf("App cache refreshed from SD: %d flashable binaries.\n", appFileCount);
    return true;
}

static bool cacheWallpaperPreviewFromSD(const String &sdPath, const String &previewPath) {
    File src = SD.open(sdPath, FILE_READ);
    if (!src) {
        Serial.print("[WALLPAPER PREVIEW SKIP] Open failed: ");
        Serial.println(sdPath);
        return false;
    }

    uint8_t header[54];
    if (!src.seek(0) || src.read(header, 54) != 54 || header[0] != 'B' || header[1] != 'M') {
        src.close();
        Serial.print("[WALLPAPER PREVIEW SKIP] Invalid BMP: ");
        Serial.println(sdPath);
        return false;
    }

    uint32_t pixelDataOffset = readLE32(header, 10);
    int32_t srcW = (int32_t)readLE32(header, 18);
    int32_t srcHRaw = (int32_t)readLE32(header, 22);
    uint16_t planes = header[26] | (header[27] << 8);
    uint16_t bpp = header[28] | (header[29] << 8);
    uint32_t compression = readLE32(header, 30);
    if (planes != 1 || bpp != 24 || compression != 0 || srcW <= 0 || srcHRaw == 0) {
        src.close();
        Serial.print("[WALLPAPER PREVIEW SKIP] Unsupported BMP: ");
        Serial.println(sdPath);
        return false;
    }

    bool topDown = srcHRaw < 0;
    int srcH = topDown ? -srcHRaw : srcHRaw;
    int srcStride = (srcW * 3 + 3) & ~3;
    int outW = WALLPAPER_PREVIEW_CACHE_W;
    int outH = WALLPAPER_PREVIEW_CACHE_H;
    int outStride = (outW * 3 + 3) & ~3;
    size_t previewBytes = 54UL + (size_t)outStride * (size_t)outH;
    if (spiffsFreeBytes() < previewBytes + WALLPAPER_CACHE_MIN_FREE) {
        src.close();
        Serial.print("[WALLPAPER PREVIEW SKIP] Not enough SPIFFS space: ");
        Serial.println(sdPath);
        return false;
    }

    SPIFFS.remove(previewPath);
    File dst = SPIFFS.open(previewPath, FILE_WRITE);
    if (!dst) {
        src.close();
        Serial.print("[WALLPAPER PREVIEW SKIP] SPIFFS create failed: ");
        Serial.println(previewPath);
        return false;
    }

    uint8_t outHeader[54] = {0};
    outHeader[0] = 'B';
    outHeader[1] = 'M';
    putLE32(outHeader, 2, (uint32_t)previewBytes);
    putLE32(outHeader, 10, 54);
    putLE32(outHeader, 14, 40);
    putLE32(outHeader, 18, (uint32_t)outW);
    putLE32(outHeader, 22, (uint32_t)outH);
    putLE16(outHeader, 26, 1);
    putLE16(outHeader, 28, 24);
    putLE32(outHeader, 34, (uint32_t)(outStride * outH));
    putLE32(outHeader, 38, 2835);
    putLE32(outHeader, 42, 2835);
    if (dst.write(outHeader, sizeof(outHeader)) != sizeof(outHeader)) {
        src.close();
        dst.close();
        SPIFFS.remove(previewPath);
        return false;
    }

    bool rowBufHeapAllocated = false;
    uint8_t *srcRow = acquireWallpaperRowBuffer((size_t)srcStride, &rowBufHeapAllocated);
    uint8_t outRow[(WALLPAPER_PREVIEW_CACHE_W * 3) + 4];
    if (!srcRow) {
        src.close();
        dst.close();
        SPIFFS.remove(previewPath);
        return false;
    }

    memset(outRow, 0, sizeof(outRow));
    bool ok = true;
    for (int outFileRow = 0; outFileRow < outH && ok; ++outFileRow) {
        int dstY = outH - 1 - outFileRow;
        int srcY = (int)((uint32_t)dstY * (uint32_t)srcH / (uint32_t)outH);
        if (srcY >= srcH) srcY = srcH - 1;
        int fileRow = topDown ? srcY : (srcH - 1 - srcY);
        uint32_t rowOffset = pixelDataOffset + (uint32_t)fileRow * (uint32_t)srcStride;
        if (!src.seek(rowOffset)) {
            ok = false;
            break;
        }
        int got = src.read(srcRow, srcStride);
        if (got < srcStride) {
            int clearFrom = got > 0 ? got : 0;
            memset(srcRow + clearFrom, 0, srcStride - clearFrom);
        }

        for (int dx = 0; dx < outW; ++dx) {
            int sx = (int)((uint32_t)dx * (uint32_t)srcW / (uint32_t)outW);
            if (sx >= srcW) sx = srcW - 1;
            int si = sx * 3;
            int di = dx * 3;
            outRow[di + 0] = srcRow[si + 0];
            outRow[di + 1] = srcRow[si + 1];
            outRow[di + 2] = srcRow[si + 2];
        }
        for (int pad = outW * 3; pad < outStride; ++pad) outRow[pad] = 0;
        if (dst.write(outRow, outStride) != (size_t)outStride) ok = false;
        if ((outFileRow & 0x0F) == 0) yield();
    }

    releaseWallpaperRowBuffer(srcRow, rowBufHeapAllocated);
    src.close();
    dst.close();

    if (!ok) {
        SPIFFS.remove(previewPath);
        Serial.print("[WALLPAPER PREVIEW SKIP] Write failed: ");
        Serial.println(sdPath);
        return false;
    }

    File verify = SPIFFS.open(previewPath, FILE_READ);
    bool valid = verify && verify.size() == previewBytes;
    if (verify) verify.close();
    if (!valid) {
        SPIFFS.remove(previewPath);
        Serial.print("[WALLPAPER PREVIEW SKIP] Verify failed: ");
        Serial.println(sdPath);
        return false;
    }
    return true;
}

static void cacheWallpaperDirectory(File &wallpaperCache) {
    wallpaperEntryCount = 0;
    removeWallpaperBmpCaches();

    File dir = SD.open(WALLPAPER_SD_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("Wallpaper directory not found on SD.");
        if (dir) dir.close();
        return;
    }

    while (wallpaperEntryCount < WALLPAPER_MAX_ENTRIES) {
        File entry = dir.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String leaf = leafNameFromPath(String(entry.name()));
            if (leaf.length() > 0 && !leaf.startsWith("._") && isBMPName(leaf)) {
                String fullPath = String(WALLPAPER_SD_DIR) + "/" + leaf;
                entry.close();

                writeWallpaperCacheEntry(wallpaperCache, fullPath);
                wallpaperEntryCount++;
                Serial.print("[WALLPAPER] ");
                Serial.println(fullPath);
                continue;
            }
        }
        entry.close();
    }

    dir.close();
}
 
void saveFNBuffer() {
    SDInUse = true;
    appFileCount = 0;
    FileCount = 0;
    bool rebuildFileTree = !fileTreeCacheSchemaCurrent() ||
                           !SPIFFS.exists(FILE_TREE_CACHE_PATH) ||
                           SPIFFS.exists(FILE_TREE_REFRESH_FLAG_PATH);

    SPIFFS.remove(APP_CACHE_SCHEMA_PATH);
    SPIFFS.remove(WALLPAPER_CACHE_SCHEMA_PATH);
    SPIFFS.remove(APP_NAME_CACHE_PATH);
    SPIFFS.remove(FILE_NAME_CACHE_PATH);
    SPIFFS.remove(WALLPAPER_NAME_CACHE_PATH);
    if (rebuildFileTree) {
        SPIFFS.remove(FILE_TREE_REFRESH_FLAG_PATH);
        SPIFFS.remove(FILE_TREE_SCHEMA_PATH);
        SPIFFS.remove(FILE_TREE_CACHE_PATH);
        clearFileExplorerPreviewCache();
    }
    File appCache = SPIFFS.open(APP_NAME_CACHE_PATH, FILE_WRITE);
    File fileCache = SPIFFS.open(FILE_NAME_CACHE_PATH, FILE_WRITE);
    File wallpaperCache = SPIFFS.open(WALLPAPER_NAME_CACHE_PATH, FILE_WRITE);
    File treeCache;
    if (rebuildFileTree) treeCache = SPIFFS.open(FILE_TREE_CACHE_PATH, FILE_WRITE);
    if (!appCache || !fileCache || !wallpaperCache || (rebuildFileTree && !treeCache)) {
        Serial.println("Failed to create filename caches in SPIFFS");
        if (appCache) appCache.close();
        if (fileCache) fileCache.close();
        if (wallpaperCache) wallpaperCache.close();
        if (rebuildFileTree && treeCache) treeCache.close();
        SDInUse = false;
        return;
    }
 
    Serial.println("Files in /apps/:");
    bootVideoProgress(36, "Scanning apps");
    cacheAppDirectory(appCache, "/apps", "", 0);
    appCache.close();
    writeAppCacheSchema();
    Serial.print("\nStored App Filenames: ");
    Serial.println(appFileCount);

    bootVideoProgress(40, "Scanning wallpapers");
    cacheWallpaperDirectory(wallpaperCache);
    wallpaperCache.close();
    writeWallpaperCacheSchema();
    Serial.print("Stored Wallpapers: ");
    Serial.println(wallpaperEntryCount);

    if (rebuildFileTree) {
        feTreeCachedCount = 0;
        feTextCachedBytes = 0;
        Serial.println("Building file explorer tree:");
        bootVideoProgress(44, "Building file tree");
        cacheFileExplorerDirectory(treeCache, "/", 0);
        treeCache.close();
        writeFileTreeCacheSchema();
        Serial.print("Stored File Explorer Entries: ");
        Serial.println(feTreeCachedCount);
        Serial.print("Cached Text Preview Bytes: ");
        Serial.println(feTextCachedBytes);
    } else {
        Serial.println("File explorer tree cache is current. Reusing SPIFFS cache.");
    }

    bootVideoProgress(52, "Caching root files");
    File dirA = SD.open("/");
    if (!dirA) {
        fileCache.close();
        SDInUse = false;
        return;
    }
    while (true) {
        File entry = dirA.openNextFile();
        if (!entry || FileCount >= MAX_FILES) break;
        writeCachedName(fileCache, entry.name());
        FileCount++;
        entry.close();
    }
    dirA.close();
    fileCache.close();
    Serial.print("Stored Root Filenames: ");
    Serial.println(FileCount);
    SDInUse = false;
}
 
void structImagePath(String straightPath , String curlPath = WALLPAPER_SD_PATH){
    String actualPath = String(curlPath.substring(0, String(WALLPAPER_SD_PATH).length()-11) + straightPath +".bmp");
    Serial.println(actualPath);
    consoleOut("New sd path declared: " + actualPath);
    consoleOut("Old sd decay path: " + curlPath);
    WALLPAPER_SD_PATH = actualPath;
    Serial.println(WALLPAPER_SD_PATH);
}
 
int verde(String a) {
    if (a == "reboot") return 1;
    if (a == "pue core") return 2;
    if (a == "help") return 3;
    if (a == "reboot full") return 4;
    if (a.startsWith("loadapp")) return 5;
    if (a.startsWith("reverie dacman")) return 6;
    if (a == "videodisplay mode quality") return 7;
    if (a == "videodisplay mode performance") return 8;
    if (a.startsWith("bmpbright")) return 9;
    if (a.startsWith("bmpmono")) return 10;
    if (a.startsWith("bmpwhite")) return 11;
    if (a.startsWith("bmpblack")) return 12;
    if (a.startsWith("bmpinvert")) return 13;
    if (a.startsWith("logshow")) return 14;
    if (a.startsWith("bmpwhitealpha")) return 15;
    if (a.startsWith("bmpblackalpha")) return 16;
    if (a.startsWith("corelambda")){
        String b = a.substring(String("corelambda").length() + 1);
        Serial.println(b);
        if (b == "penconf"){
            return 17;  
        } else {
            return 0;
        }
    }
    if (a.startsWith("coreimage")){
        String imgName = a.substring(String("coreimage").length() + 1);
        structImagePath(imgName);
        return 18;
    }
    return 0;
}
 
void execute(String a) {
    a.trim();
    String command = a;
    command.toLowerCase();

    if (command == "clear" || command == "cls") {
        for (int i = 0; i < CONSOLE_LINES; ++i) consoleBuf[i] = "";
        consoleHead = 0;
        consoleCount = 0;
        consoleScroll = 0;
        redrawConsole();
        return;
    }
    if (command.startsWith("echo ")) {
        consoleOut(a.substring(5));
        return;
    }
    if (command == "heap" || command == "status") {
        beginConsoleBatch();
        consoleOut(String("Heap free: ") + String(ESP.getFreeHeap()) + " bytes");
        consoleOut(String("DMA free: ") + String(heap_caps_get_free_size(MALLOC_CAP_DMA)) + " bytes");
        consoleOut(String("Apps: ") + String(appFileCount) + "  Cached files: " + String(FileCount));
        consoleOut(String("Display: ") + (initVd ? "online" : "offline"));
        endConsoleBatch();
        return;
    }
    if (command == "time") {
        consoleOut(currentClockTime);
        return;
    }
    if (command == "date") {
        consoleOut(currentClockDate);
        return;
    }
    if (command == "pwd") {
        consoleOut(feCurrentPath);
        return;
    }
    if (command == "history") {
        beginConsoleBatch();
        for (int i = 0; i < shellHistorySize; ++i) {
            consoleOut(String(i + 1) + "  " + shellHistory[i]);
        }
        endConsoleBatch();
        return;
    }
    if (command == "apps" || command == "ls apps") {
        beginConsoleBatch();
        if (appFileCount <= 0) consoleOut("No cached applications.");
        for (int i = 0; i < appFileCount; ++i) {
            String entry;
            if (readCachedAppEntry(i, entry)) consoleOut(appEntryPayload(entry));
        }
        endConsoleBatch();
        return;
    }
    if (command == "ls") {
        consoleOut(String(FileCount) + " cached filesystem entries. Open Files to browse them.");
        return;
    }
    if (command == "files") { fileExplorer(); return; }
    if (command == "utilities" || command == "apps open") { utilitiesMenu(); return; }
    if (command == "config" || command == "settings") {
        configurationDirty = false;
        configureMenu();
        return;
    }
    if (command.startsWith("open ")) {
        String path = a.substring(5);
        path.trim();
        fileExplorer();
        feSetPath(path);
        feDrawBrowseScreen();
        return;
    }
    if (command == "home" || command == "exit") {
        if (!restoreAndCloseKernelWindow()) home();
        return;
    }
    if (command == "version" || command == "about") {
        consoleOut("CitadelaOS kernel");
        consoleOut(String("Build ") + __DATE__ + " " + __TIME__);
        return;
    }

    switch (verde(a)) {
        case 0: consoleOut(String("Unknown Command : " + a)); break;
        case 1: ESP.restart(); break;
        case 2: boolRes = "fsd"; boolUpdate(); ESP.restart(); break;
        case 3:
            beginConsoleBatch();
            consoleOut("clear, status, heap, time, date, history");
            consoleOut("files, utilities, config, home, open <path>");
            consoleOut("ls, apps, pwd, echo <text>, version");
            consoleOut("reboot - restart system");
            consoleOut("reboot full - bootstrap entry");
            consoleOut("reverie dacman (argF) (argD) - speaker test");
            consoleOut("videodisplay mode (argD) - display mode");
            consoleOut("pue core - clean file system");
            consoleOut("loadapp (argSTR) - appB");
            consoleOut("bmpbright <percent> - set wallpaper brightness (0..200)");
            consoleOut("bmpmono <on|off|toggle> - set/toggle monochrome mode");
            consoleOut("bmpwhite <threshold> <blend> - set white regulation");
            consoleOut("bmpblack <threshold> <blend> - set black regulation");
            consoleOut("bmpinvert <on|off|toggle> - invert wallpaper colors; swap UI black/white");
            consoleOut("logshow <on|off|toggle> - show/hide on-screen logger");
            consoleOut("bmpwhitealpha <threshold|on|off|toggle>");
            consoleOut("bmpblackalpha <threshold|on|off|toggle>");
            consoleOut("corelambda <DEFINE BOOLRES> - Reloads Diskbarn");
            consoleOut("coreimage <DEFINE IMAGE> - Sets a desired wallpaper (.bmp)");
            endConsoleBatch();
            break;
        case 4: boolRes = "trueBootloader"; boolUpdate(); ESP.restart(); break;
        case 5: consoleOut("Loading app"); break;
        case 6: consoleOut("Dacman reverie"); break;
        case 7: consoleOut("Switching to quality display mode"); break;
        case 8: consoleOut("Switching to resolution display mode"); break;
 
        case 9: {
            int idx = a.indexOf(' ');
            int v = 100;
            if (idx != -1) v = a.substring(idx + 1).toInt();
            setBMPBrightness(v);
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 10: {
            int idx = a.indexOf(' ');
            String arg = "";
            if (idx != -1) arg = a.substring(idx + 1);
            arg.toLowerCase();
            if (arg == "on" || arg == "1" || arg == "true") {
                setBMPMonochrome(true);
            } else if (arg == "off" || arg == "0" || arg == "false") {
                setBMPMonochrome(false);
            } else {
                setBMPMonochrome(!bmpMonochrome);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 11: {
            int idx = a.indexOf(' ');
            if (idx != -1) {
                String rest = a.substring(idx + 1);
                rest.trim();
                int idx2 = rest.indexOf(' ');
                if (idx2 == -1) {
                    int t = rest.toInt();
                    setWhiteThreshold(t);
                } else {
                    int t = rest.substring(0, idx2).toInt();
                    float b = rest.substring(idx2 + 1).toFloat();
                    setWhiteThreshold(t);
                    setWhiteBlend(b);
                }
            } else {
                Serial.printf("Current whiteThreshold=%d, whiteBlend=%.2f\n", whiteThreshold, whiteBlend);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 12: {
            int idx = a.indexOf(' ');
            if (idx != -1) {
                String rest = a.substring(idx + 1);
                rest.trim();
                int idx2 = rest.indexOf(' ');
                if (idx2 == -1) {
                    int t = rest.toInt();
                    setBlackThreshold(t);
                } else {
                    int t = rest.substring(0, idx2).toInt();
                    float b = rest.substring(idx2 + 1).toFloat();
                    setBlackThreshold(t);
                    setBlackBlend(b);
                }
            } else {
                Serial.printf("Current blackThreshold=%d, blackBlend=%.2f\n", blackThreshold, blackBlend);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 13: {
            int idx = a.indexOf(' ');
            String arg = "";
            if (idx != -1) arg = a.substring(idx + 1);
            arg.toLowerCase();
            if (arg == "on" || arg == "1" || arg == "true") {
                setInvertColors(true);
            } else if (arg == "off" || arg == "0" || arg == "false") {
                setInvertColors(false);
            } else {
                setInvertColors(!invertColors);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 14: {
            int idx = a.indexOf(' ');
            String arg = "";
            if (idx != -1) arg = a.substring(idx + 1);
            arg.toLowerCase();
            if (arg == "on" || arg == "1" || arg == "true") {
                setShowLogger(true);
            } else if (arg == "off" || arg == "0" || arg == "false") {
                setShowLogger(false);
            } else {
                setShowLogger(!showLoggerOnScreen);
            }
            break;
        }
 
        case 15: {
            int idx = a.indexOf(' ');
            String arg = "";
            if (idx != -1) arg = a.substring(idx + 1);
            arg.trim();
            if (arg.length() == 0) {
                Serial.printf("Wallpaper white-alpha threshold = %d, mode = %s\n", wallpaperWhiteAlphaThreshold, skipWallpaperBrightPixels ? "ON" : "OFF");
            } else {
                setWallpaperWhiteAlphaMode(arg);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
 
        case 16: {
            int idx = a.indexOf(' ');
            String arg = "";
            if (idx != -1) arg = a.substring(idx + 1);
            arg.trim();
            if (arg.length() == 0) {
                Serial.printf("Wallpaper black-alpha threshold = %d, mode = %s\n", wallpaperBlackAlphaThreshold, skipWallpaperDarkPixels ? "ON" : "OFF");
            } else {
                setWallpaperBlackAlphaMode(arg);
            }
            if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                renderBMPDotByDotFromSPIFFS(WALLPAPER_SPIFFS_PATH, WALLPAPER_W, WALLPAPER_H);
            }
            break;
        }
        case 17: {
            Serial.println("Preparing pendingConfiguration: write config to SPIFFS and restart to copy to SD");
            writeSystemConfigToSPIFFS();
            File flag = SPIFFS.open("/pending_conf.flag", FILE_WRITE);
            if (flag) {
                flag.print("1");
                flag.close();
            } else {
                Serial.println("Warning: couldn't create pending_conf.flag");
            }
 
            boolRes = "false";
            boolUpdate();
 
            ESP.restart();
            break;
        }
        case 18: {consoleOut("Wallpaper pends update string.");}
 
    }
}
 
void processHoverAndTooltip() {
  if (util || fE || applaunched || emBreak || configurationPending || manualTimePending || iconThresholdPending || uartUploadPending || bluetoothConnectionPending || wifiConnectionPending || shell) {
        if (tooltipShown) {
            tooltipShown = false;
            tooltipIndex = -1;
            tooltipX = 0;
            tooltipY = 0;
            tooltipW = 0;
            tooltipH = 0;
            tooltipText[0] = '\0';
        }
 
        hoverStartMillis = 0;
        return;
    }
 
    if (dragvalve != previousDragValve) {
        hoverStartMillis = 0;
        if (tooltipShown) hideTooltip();
        return;
    }
 
    if (dragvalve == previousDragValve && dragvalve >= 1) {
        if (hoverStartMillis == 0) {
            hoverStartMillis = millis();
            return;
        }
        unsigned long elapsed = millis() - hoverStartMillis;
        if (elapsed >= HOVER_DELAY_MS && !tooltipShown) {
            int idx = dragvalve - 1;
            const char* labels[6] = { "Utilities", "Files", "Restart", "Configure", "Programmer", "Terminal" };
            showTooltipForButton(idx, labels[idx]);
        }
    } else {
        hoverStartMillis = 0;
    }
}
 
void handleBootloaderFlash();
void handleCProgFlash();
 
int configurationswitch(int val) {
    if (val == 1) return 1;
    if (val == 2) return 2;
    if (val == 3) return 3;
    if (val == 4) return 4;
    if (val == 5) return 5;
    if (val == 6) return 6;
    if (val == 7) return 7;
    if (val == 8) return 8;
    if (val == 9) return 9;
    if (val == 10) return 10;
    if (val == 11) return 11;
    if (val == 12) return 12;
    if (val == 13) return 13;
    if (val == 14) return 14;
    if (val == 15) return 15;
    if (val == 16) return 16;
    if (val == 17) return 17;
    if (val == 18) return 18;
    return 0;
}
void timeClock(String time = "", String date = "") {
    if (time.length() == 0 &&
        !fE && !applaunched && !shell && !emBreak && !util && !configurationPending && !manualTimePending && !iconThresholdPending && !wallpaperPickerPending && !uartUploadPending && !bluetoothConnectionPending && !wifiConnectionPending) {
        Serial1.println("CBTIME");
        return;
    }
    if (time.length() == 0 || date.length() == 0) return;
    // Draw every local tick, but send only the latest one after a queued scene.
    struct ClockMirrorGuard {
        bool paused = serialDisplay.hasBacklog();
        ClockMirrorGuard() { if (paused) videodisplay.beginSerialPixels(); }
        ~ClockMirrorGuard() { if (paused) videodisplay.endSerialPixels(); }
    } mirrorGuard;
    if (mirrorGuard.paused) serialDisplayClockDeferred = true;
    kernelCursorRestore();
    currentClockTime = time;
    currentClockDate = date;
    videodisplay.setFont(Font8x8);
    videodisplay.setCursor(CLOCK_POS_X, CLOCK_POS_Y);
    videodisplay.setTextColor(videodisplay.RGB(70,105,111), videodisplay.RGB(218, 235, 242));
    videodisplay.println(time.c_str());
 
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(CLOCK_POS_X - 65, CLOCK_POS_Y + 1);
    videodisplay.setTextColor(videodisplay.RGB(70,105,111), videodisplay.RGB(218, 235, 242));
    videodisplay.println(date.c_str());
    kernelCursorRefreshAfterRedraw();
}
void controller() {
    static String ctrlInput = "";
    static String ctrlInput1 = "";
    const int UTIL_COLUMNS = 5;
 
    while (serialDisplay.available()) {
        char c = serialDisplay.read();
        if (c == '\n') {
            ctrlInput1.trim();
            if (handleDisplayMirrorCommand(ctrlInput1)) {
                ctrlInput1 = "";
                continue;
            }
            if (wallpaperFetchInProgress && ctrlInput1.length()) {
                Serial.print("[WPFETCH RX] ");
                Serial.println(ctrlInput1);
            }
            if (ctrlInput1.indexOf("VDFETCHREADY") >= 0) {
                wallpaperFetchControllerReady = true;
                ctrlInput1 = "";
                continue;
            }
            if (ctrlInput1.indexOf("VDFETCHOFF") >= 0) {
                wallpaperFetchControllerOff = true;
                ctrlInput1 = "";
                continue;
            }
            if (ctrlInput1.indexOf("VDFETCHFAILED") >= 0) {
                wallpaperFetchControllerFailed = true;
                ctrlInput1 = "";
                continue;
            }
            if (wallpaperFetchInProgress && ctrlInput1.startsWith("MOUSE")) {
                ctrlInput1 = "";
                continue;
            }
            if (handleKernelMouseReport(ctrlInput1)) {
                ctrlInput1 = "";
                continue;
            }
            if (handleConnectionControllerLine(ctrlInput1)) {
                ctrlInput1 = "";
                continue;
            }
            if (!emBreak) {
                if (!shell) {
                    if (wallpaperPickerPending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleWallpaperPickerInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (iconThresholdPending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleIconThresholdInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (manualTimePending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleManualTimeInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (uartUploadPending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleUARTUploadInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (bluetoothConnectionPending || wifiConnectionPending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleConnectionInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (configurationPending) {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
                        handleConfigurationInput(ctrlInput1);
                        ctrlInput1 = "";
                    }
                    else if (fE) {
                        ctrlInput1.trim();
                        if (ctrlInput1 == "RightGUI (Win) +") {
                            restartWithFallbackVideo("Restarting");
                        } else {
                            handleFileExplorerInput(ctrlInput1);
                        }
                        ctrlInput1 = "";
                    }
                    else {
                        ctrlInput1.trim();
                        Serial.println(ctrlInput1);
 
                        if (ctrlInput1 == "UpArrow") {
                            if (util && appFileCount > 0) {
                                int cols = UTIL_COLUMNS;
                                int rows = (appFileCount + cols - 1) / cols;
                                int col = dragvalve % cols;
                                int row = dragvalve / cols;
                                if (row > 0) row--; else row = rows - 1;
                                int newIdx = row * cols + col;
                                while (newIdx >= appFileCount && col > 0) { col--; newIdx = row * cols + col; }
                                dragvalve = constrain(newIdx, 0, appFileCount - 1);
                                handleNavigation();
                            } else {
                                if (dragvalve > 1) dragvalve--;
                            }
                        } else if (ctrlInput1 == "DownArrow") {
                            if (util && appFileCount > 0) {
                                int cols = UTIL_COLUMNS;
                                int rows = (appFileCount + cols - 1) / cols;
                                int col = dragvalve % cols;
                                int row = dragvalve / cols;
                                if (row < rows - 1) row++; else row = 0;
                                int newIdx = row * cols + col;
                                while (newIdx >= appFileCount && col > 0) { col--; newIdx = row * cols + col; }
                                dragvalve = constrain(newIdx, 0, appFileCount - 1);
                                handleNavigation();
                            } else {
                                if (dragvalve < 6) dragvalve++;
                            }
                        } else if (ctrlInput1 == "LeftArrow") {
                            if (util && appFileCount > 0) {
                                if ((dragvalve % UTIL_COLUMNS) > 0) dragvalve--;
                                else {
                                    // wrap to previous column in same row or wrap to end
                                    int row = dragvalve / UTIL_COLUMNS;
                                    int prevCol = UTIL_COLUMNS - 1;
                                    int newIdx = row * UTIL_COLUMNS + prevCol;
                                    while (newIdx >= appFileCount && prevCol > 0) { prevCol--; newIdx = row * UTIL_COLUMNS + prevCol; }
                                    dragvalve = constrain(newIdx, 0, appFileCount - 1);
                                }
                                handleNavigation();
                            } else {
                                if (dragvalve > 1) dragvalve--;
                            }
                        } else if (ctrlInput1 == "RightArrow") {
                            if (util && appFileCount > 0) {
                                if ((dragvalve % UTIL_COLUMNS) < (UTIL_COLUMNS - 1)) dragvalve++;
                                else {
                                    int row = dragvalve / UTIL_COLUMNS;
                                    int newIdx = row * UTIL_COLUMNS;
                                    // ensure newIdx is valid
                                    if (newIdx >= appFileCount) newIdx = 0;
                                    dragvalve = constrain(newIdx, 0, appFileCount - 1);
                                }
                                handleNavigation();
                            } else {
                                if (dragvalve < 6) dragvalve++;
                            }
                        } else if (ctrlInput1 == "Enter") {
                            if (!warning) {
                                if (util) { handleSelection(); }
                                else { selection = true; }
                            } else { warning = false; home(); }
                        } else if (applaunched && ctrlInput1 == "Escape") {
                            if (!restoreAndCloseKernelWindow()) home();
                        } else if (!applaunched && ctrlInput1 == "Escape") {
                            requestElevation("Return to Bootstrap?", 1);
                        } else if (ctrlInput1 == "RightGUI (Win) +") {
                            restartWithFallbackVideo("Restarting");
                        } else if (ctrlInput1 == "BL1X") {
                            Serial.println("BTE Connection Established");
                            Serial.println("Handshake Successful");
                        } else if (ctrlInput1.startsWith("CBTIME ") && !FastBoot) {
                            String time = ctrlInput1.substring(18, 26);
                            String date = ctrlInput1.substring(7, 17);
                            timeClock(time, date);
                        }
                        ctrlInput1 = "";
                    }
                } else {
                    ctrlInput1.trim();
                    Serial.println(String("From Terminal: ") + ctrlInput1);
                    handleShellInput(ctrlInput1);
                    ctrlInput1 = "";
                }
            } else {
                // emBreak input handling (unchanged)
                ctrlInput1.trim();
                String low = ctrlInput1;
                low.toLowerCase();
                Serial.println(low);
 
                if (low == "escape") {
                    closeElevationOverlay();
                } else if (low == "y") {
                    if (emBreakAction == 1) {
                        boolRes = "trueBootloader";
                        boolUpdate();
                        ESP.restart();
                    } else if (emBreakAction == 2) {
                        writeSystemConfigToSPIFFS();
                        File flag = SPIFFS.open("/pending_conf.flag", FILE_WRITE);
                        if (flag) { flag.print("1"); flag.close(); }
                        boolRes = "false";
                        boolUpdate();
                        configurationDirty = false;
                        ESP.restart();
                    } else {
                        closeElevationOverlay();
                    }
                } else if (low == "n") {
                    if (emBreakAction == 2) {
                        readSystemConfigFromSPIFFS();
                        configurationPending = false;
                        configurationDirty = false;
                        applaunched = false;
                        closeElevationOverlay();
                    } else {
                        closeElevationOverlay();
                    }
                }
                ctrlInput1 = "";
            }
        } else {
            // accumulate characters
            ctrlInput1 += c;
        }
    }
}
 
String generateRandomString(int length) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    String result = "";
 
    for (int i = 0; i < length; i++) {
        int index = random(0, sizeof(charset) - 1);
        result += charset[index];
    }
 
    return result;
}
void storeInstructions(String writeF) {
    File file = SPIFFS.open("/instructions.cins", FILE_WRITE);
    if (file) {
        file.print(writeF);
        file.close();
    } else {
        Serial.println("Failed to open file for writing.");
    }
    ESP.restart();
}
 
void readInstructions() {
    File file = SPIFFS.open("/instructions.cins", FILE_READ);
    if (file) {
        String a = file.readStringUntil('\n');
        a.trim();
        Serial.println(a);
        if (a.startsWith("mkdir")) {
            Serial.println("DIR Creation " + a);
            SD.mkdir(String("/" + a.substring(6)));
        } else if (a.startsWith("mkfile")){
            Serial.println("FILE Creation " + a);
            File newFile = SD.open(String("/UserData/" + a.substring(6) +".cins"), FILE_WRITE);
            if (newFile) newFile.close();
        }
        file.close();
        SPIFFS.remove("/instructions.cins");
        Serial.println("Instruction consumed.");
    } else {
        Serial.println("Failed to open file for reading.");
    }
}
 
void processSelection() {
  if (selection) {
    selection = false;
    if (fE || configurationPending || manualTimePending || iconThresholdPending || wallpaperPickerPending || uartUploadPending || bluetoothConnectionPending || wifiConnectionPending) {
        return;
    }
    if (!applaunched){
        activateHomeItem(dragvalve);
    } else {
        switch (dragvalve) {
            case 1: {
                Serial.println("mkdir");
                String rnStr = generateRandomString(5);
                storeInstructions("mkdir " + rnStr);
                break;
            }
            case 2: {
                Serial.println("mkfile");
                String rnStr1 = generateRandomString(5);
                storeInstructions("mkfile " + rnStr1);
                break;
            }
            case 3:
                Serial.println("delete");
                break;
            case 4:
                Serial.println("format");
                break;
            default:
                Serial.println("Unknown selection");
                break;
        }
    }
  }
}
void updateDisplay() {
    processHoverAndTooltip();
    if (manualTimePending || iconThresholdPending || wallpaperPickerPending || uartUploadPending || bluetoothConnectionPending || wifiConnectionPending) return;
    auto restoreWallpaperRect = [&](int x, int y, int w, int h) {
        if (!SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) return;
        int rx = max(0, x - 1);
        int ry = max(0, y - 1);
        int rw = w + 2;
        int rh = h + 2;
        if (rx + rw > WALLPAPER_W) rw = WALLPAPER_W - rx;
        if (ry + rh > WALLPAPER_H) rh = WALLPAPER_H - ry;
        if (rw > 0 && rh > 0) {
            renderBMPRegionFromSPIFFS(WALLPAPER_SPIFFS_PATH, rx, ry, rw, rh, WALLPAPER_W, WALLPAPER_H);
        }
    };
 
    if (configurationPending){
        if (dragvalve != previousDragValve) {
            KernelCursorDrawGuard cursorGuard;
            int oldItem = previousDragValve;
            previousDragValve = dragvalve;
            if (oldItem >= 1 && oldItem <= CONFIG_ITEM_COUNT) drawConfigurationItem(oldItem);
            drawConfigurationItem(dragvalve);
        } 
    }
 
    if (fE) return;

    if (!util) {
            if (dragvalve != previousDragValve) {
                KernelCursorDrawGuard cursorGuard;
                if (tooltipShown) hideTooltip();
                if (previousDragValve >= 1 && previousDragValve <= 6) {
                    int oldYOffset = 18 + (previousDragValve - 1) * 30;
                    if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH)) {
                        restoreWallpaperRect(11, oldYOffset, 2, 20);
                        restoreWallpaperRect(37, oldYOffset, 2, 20);
                    } else {
                        videodisplay.fillRect(11, oldYOffset, 2, 20, 0);
                        videodisplay.fillRect(37, oldYOffset, 2, 20, 0);
                    }
                }
 
                previousDragValve = dragvalve;
                int yOffset = 18 + (dragvalve - 1) * 30;
                videodisplay.fillRect(11, yOffset, 2, 20, videodisplay.RGB(255, 255, 255));
                videodisplay.fillRect(37, yOffset, 2, 20, videodisplay.RGB(255, 255, 255));
            }
 
    }
}
int getBoolResCode(String value) {
    if (value == "false") return 0;
    if (value == "trueKernel") return 1;
    if (value == "trueBootloader") return 2;
    if (value == "trueWifiFlash") return 3;
    if (value == "trueCprog") return 4;
    if (value.startsWith("/apps")) return 5;
    if (value == "fsd") return 6;
    if (value == "pendingConfiguration") return 7;
    return -1;
}
 
void setup() {
    Serial.setRxBufferSize(1024);
    Serial1.setRxBufferSize(129);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    serialDisplay.begin(Serial1);
    videodisplay.bindSerialDisplay(serialDisplay);
    lastTimeClockMs = millis();
    bootVideoStart("Starting Citadela");
 
    freeSketch = ESP.getFlashChipSize();
    bootVideoProgress(4, "Mounting SPIFFS");
    if (!Citadela::Storage::beginSPIFFS(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    bootVideoProgress(12, "Mounting SD card");
    const uint32_t bootSDMountFrequencies[] = {4000000, 2000000, 1000000, 400000};
    Citadela::SDStats sdStats;
    if(!Citadela::Storage::beginBoardSDWithRetry(
        SD,
        SPI,
        bootSDMountFrequencies,
        sizeof(bootSDMountFrequencies) / sizeof(bootSDMountFrequencies[0]),
        &sdStats,
        &Serial)){
      Serial.println("SD Failed to init");
      warning = true;
    } else {
      warning = false;
      Serial.println("EUREKA");
      cardSize = sdStats.cardSize;
      usedBytes = sdStats.usedBytes;
      freeBytes = sdStats.freeBytes;
      Serial.println(String(cardSize + usedBytes + freeBytes));
    }
    bootVideoProgress(20, "Checking settings");
    if (SPIFFS.exists("/pending_conf.flag")) {
        Serial.println("pending_conf.flag found — copying systemConfiguration.conf from SPIFFS to SD...");
        if (warning) {
            Serial.println("SD not available: cannot copy configuration now.");
        } else {
            if (!SD.exists("/UserData")) SD.mkdir("/UserData");
            if (!SD.exists("/UserData/Configurations")) SD.mkdir("/UserData/Configurations");
            bool ok = copyFileSPIFFSToSD("/systemConfiguration.conf", "/UserData/Configurations/systemConfiguration.conf");
            if (ok) {
                Serial.println("Configuration copied to SD successfully. Removing flag and rebooting...");
                SPIFFS.remove("/pending_conf.flag");
                ESP.restart();
            } else {
                Serial.println("Failed to copy configuration to SD. Marker left for retry on next boot.");
            }
        }
    }
    readSystemConfigFromSPIFFS();
    if (!warning && !preservePendingUARTKernelImage()) syncRunningKernelImageToSD();
    bootVideoProgress(24, "Preparing config");
    String booleanConfig;
    booleanConfig.reserve(280);
 
    booleanConfig += "WallpaperToggle="; booleanConfig += (WallpaperToggle ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "TooltipsToggle=";  booleanConfig += (TooltipsToggle ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "VerboseUART=";      booleanConfig += (VerboseUART ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "DisplayColour=";    booleanConfig += (DisplayColour ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "WifiCoreToggle=";   booleanConfig += (WifiCoreToggle ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "TelemetryData=";    booleanConfig += (TelemetryData ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "FastBoot=";         booleanConfig += (FastBoot ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "AudioDriver=";      booleanConfig += (AudioDriver ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "InvertColors=";     booleanConfig += (invertColors ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "WallpaperColor=";   booleanConfig += (!bmpMonochrome ? "true" : "false"); booleanConfig += "\n";
    booleanConfig += "WallpaperPath=";    booleanConfig += WALLPAPER_SD_PATH; booleanConfig += "\n";
    booleanConfig += "IconBlackAlphaThreshold="; booleanConfig += String(iconBlackAlphaThreshold); booleanConfig += "\n";
    booleanConfig += "IconWhiteAlphaThreshold="; booleanConfig += String(iconWhiteAlphaThreshold);
 
    Serial.print(booleanConfig);
 
    if(AudioDriver){
        pinMode(SPEAKER_PIN, 0);
        startBootTone(1000);
    }
    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == ""){
        boolRes = "false";
        boolUpdate();
    }
    switch (getBoolResCode(boolRes)) {
        case 0: {
            bool fullCacheRefresh = !warning &&
                (!FastBoot || !appCacheSchemaCurrent() || !fileTreeCacheSchemaCurrent() ||
                 !wallpaperCacheSchemaCurrent() || !SPIFFS.exists(WALLPAPER_NAME_CACHE_PATH));
            if (fullCacheRefresh) {
                if (!FastBoot) {
                    bootVideoProgress(28, "Reading instructions");
                    readInstructions();
                } else {
                    Serial.println("Cache schema changed; refreshing SD caches once.");
                }
                bootVideoProgress(32, "Scanning files");
                saveFNBuffer();
                bootVideoProgress(58, "Copying app icons");
                copyAppIconsFromSDToSPIFFS(); 
            } else if (!warning) {
                bootVideoProgress(34, "Refreshing apps");
                if (refreshAppCacheFromSD()) {
                    bootVideoProgress(42, "Refreshing app icons");
                    copyAppIconsFromSDToSPIFFS();
                }
            }
            if (!warning && SD.exists(WALLPAPER_SD_PATH)) {
                if (SPIFFS.exists(WALLPAPER_SPIFFS_PATH) && filesMatchSDvsSPIFFS(WALLPAPER_SD_PATH, WALLPAPER_SPIFFS_PATH)) {
                    Serial.println("Wallpaper in SPIFFS is up-to-date. Skipping copy.");
                } else {
                    Serial.println("Found wallpaper on SD — copying to SPIFFS...");
                    bootVideoProgress(68, "Copying wallpaper");
                    if (!copyFileSDToSPIFFS(WALLPAPER_SD_PATH, WALLPAPER_SPIFFS_PATH)) {
                        Serial.println("Wallpaper copy failed or not present.");
                    }
                }
            }
            if (!warning && !FastBoot) {
                bootVideoProgress(78, "Copying icons");
                copyIconsFromSDToSPIFFSAndReadHeaders();
            } else {
                for (int i = 0; i < ICON_COUNT; ++i) {
                    if(!FastBoot){
                        readIconHeaderFromSPIFFS(i);
                    }
                }
            }
            bootVideoProgress(90, "Releasing SD card");
            stopBootTone();
            releaseFileNameBuffers();
            if (!warning) {
                SD.end();
                Serial.println("SD dismounted before video init.");
            }

            bootVideoProgress(96, "Starting display");
            Serial.printf("DMA heap before video init: free=%u largest=%u\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            bool videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
            Serial.printf("DMA heap after video init: free=%u largest=%u\n",
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            if (!videoReady) {
                Serial.println("Video init failed; restarting for a clean DMA heap.");
                bootVideoRelease();
                delay(250);
                ESP.restart();
                return;
            }
            bootVideoRelease();
            initVd = true;
            loadFNBufferFromSPIFFS();
    CitaCursor.Begin(videodisplay, SCREEN_WIDTH, SCREEN_HEIGHT, &mouseCursorOutlined);
    CitaCursor.SetCallbacks(kernelCursorUpdateHover, kernelCursorActivateClick);
 
            kernelFunction();
            break;
        }
        case 1: // "trueKernel"
            stopBootTone();
            boolRes = "false";
            boolUpdate();
            break;
        case 2: // "trueBootloader"
            stopBootTone();
            appString = "/System/bootloader.bin";
            bootVideoProgress(8, "Preparing bootloader");
            handleBootloaderFlash();
            break;
        case 3: // "trueWifiEditor"
            stopBootTone();
            appString = "/System/WifiEditor.bin";
            bootVideoProgress(8, "Preparing WiFi editor");
            handleBootloaderFlash();
            break;
        case 4: // "trueCprog"
            stopBootTone();
            appString = "/apps/CProg.bin";
            bootVideoProgress(8, "Preparing CProg");
            handleCProgFlash();
            break;
        case 5: // "trueAPPFlash"
            stopBootTone();
            appString = boolRes;
            bootVideoProgress(8, "Preparing application");
            flashApp();
            break;
        case 7: // "pendingConfiguration"
            stopBootTone();
            Serial.println("Saving Configuration and Restarting.");
            boolRes = "false";
            boolUpdate();
            ESP.restart();
            break;
    }
}
 
static void serviceSerialDisplayScene() {
    if (!serialDisplayScenePending || !initVd || !videodisplay.hasVideoMemory()) return;
    serialDisplayScenePending = false;
    serialDisplay.disable();
    kernelCursorRestore();
    serialDisplay.enable();
    if (!serialDisplay.enabled()) return;
    videodisplay.serialDisplayBeginScene();
    restoreDesktopRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (kernelWindow.active) {
        if (kernelWindow.dragging) captureAndDrawKernelWindowOutline(kernelWindow.outlineX, kernelWindow.outlineY);
        else redrawKernelWindow();
    }
    if (manualTimePending) drawManualTimeMenu();
    else if (iconThresholdPending) drawIconThresholdWindow();
    else if (wallpaperPickerPending) { drawWallpaperPickerWindow(); drawWallpaperPickerPreview(); }
    kernelCursorRefreshAfterRedraw();
    serialDisplay.command("E");
    serialDisplay.flush();
}

void loop() {
    unsigned long renderStart = millis();
    controller();
    SerialControl();
    processSelection();
    serviceConnectionWindow();
    updateDisplay();
    serviceWallpaperPickerPreview();
    if (serialDisplay.takeResyncRequest() && serialDisplayEnabled && serialDisplayConnected)
        serialDisplayScenePending = true;
    bool sceneRendered = serialDisplayScenePending;
    serviceSerialDisplayScene();
    unsigned long now = millis();
    if (now - lastTimeClockMs >= TIME_INTERVAL_MS) {
        unsigned long ticks = (now - lastTimeClockMs) / TIME_INTERVAL_MS;
        lastTimeClockMs += ticks * TIME_INTERVAL_MS;
        timeClock();
    }
    bool frameRendered = serialDisplay.endFrame();
    if (serialDisplay.takeResyncRequest() && serialDisplayEnabled && serialDisplayConnected)
        serialDisplayScenePending = true;
    unsigned long renderMs = millis() - renderStart;
    if (VerboseUART && (frameRendered || sceneRendered) && renderMs > 20)
        Serial.printf("[VDM] local render %lu ms, queued %u bytes\n", renderMs, serialDisplay.queuedBytes());
    if (!serialDisplayScenePending) serialDisplay.service();
    if (!serialDisplay.enabled()) serialDisplayClockDeferred = false;
    else if (serialDisplayClockDeferred && !serialDisplay.hasBacklog() && !serialDisplayScenePending) {
        serialDisplayClockDeferred = false;
        timeClock(currentClockTime, currentClockDate);
    }
}
 
void visualheap() {
  if (startup == false) {
    int heap = ESP.getFreeHeap();
    int startX = 255+65;
    float heapKB = heap / 1024.0;
 
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(190,190,190));
 
    videodisplay.circle(startX-10 , 23, 5, videodisplay.RGB(255,255,255));
    videodisplay.circle(startX-10 , 47, 5, videodisplay.RGB(255,255,255));
    videodisplay.fillCircle(startX-10 , 71, 5, videodisplay.RGB(255,255,255));
    videodisplay.fillCircle(startX-10 , 95, 5, videodisplay.RGB(255,255,255));
 
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(startX, 18);
 
    videodisplay.println("HeapRAM");
    String heapStr = String(heapKB, 2) + "KB";
    videodisplay.println(heapStr.c_str());
    videodisplay.println();
 
    videodisplay.println("Total SD");
    String TSDStr = String((double)cardSize / (1024.0 * 1024.0 * 1024.0), 2) + "GB";
    videodisplay.println(TSDStr.c_str());
    videodisplay.println();
 
    videodisplay.println("Free SD");
    String FoSDStr = String((double)freeBytes / (1024.0 * 1024.0 * 1024.0), 2) + "GB";
    videodisplay.println(FoSDStr.c_str());
    videodisplay.println();
 
    videodisplay.println("HeapROM");
    String SSStr = String((double)freeSketch / (1024.0 * 1024.0), 2) + "MB";
    videodisplay.println(SSStr.c_str());
    videodisplay.println();
 
    int cx1 = 255 + 60;
    int cy1 = 120;
    int radius1 = 10;
 
    float heapUsage = 1.0 - (heapKB / 320);
    int endAngle1 = heapUsage * 360;
    videodisplay.setCursor(cx1 + 15,cy1-7);
    videodisplay.println("RAM");
    videodisplay.println("USAGE");
    videodisplay.circle(cx1, cy1, radius1, videodisplay.RGB(255, 255, 255));
    for (int angle = 0; angle <= endAngle1; angle += 4) {
        float rad = angle * 3.1415926 / 180.0;
        int x = cx1 + cos(rad) * radius1;
        int y = cy1 + sin(rad) * radius1;
        videodisplay.line(cx1, cy1, x, y, videodisplay.RGB(255, 255, 255));
    }
    videodisplay.fillCircle(cx1, cy1, radius1 / 2, videodisplay.RGB(70,105,111));
    drawLambda(cx1, cy1 + 28, 0.5f, videodisplay.RGB(255,255,255), videodisplay.RGB(70,105,111));
    videodisplay.setCursor(cx1+15, cy1 + 21);
    videodisplay.println("CORE:\nLAMBDA");
 
  } else {
    return;
  }
}
