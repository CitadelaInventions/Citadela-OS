#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <esp_ota_ops.h>
#include "esp_partition.h"
#include "FS.h"
#include "SPIFFS.h"

#define SPEAKER_PIN 12

String boolRes = "";
String lastChar = "Input Char";

bool BTAV = false;
bool allowRV = false;

CompositeColorDAC videodisplay;

static const int APP_CURSOR_SCREEN_W = 376;
static const int APP_CURSOR_SCREEN_H = 192;
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

#line 58 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorLoadConfigOnce();
#line 79 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static AppCursorRawPixel appCursorGetRawPixel(int x, int y);
#line 84 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorSetRawPixel(int x, int y, AppCursorRawPixel raw);
#line 89 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool appCursorInnerPoint(int dx, int dy);
#line 94 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool appCursorOutlinePoint(int dx, int dy);
#line 99 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool appCursorCapturePoint(int dx, int dy);
#line 103 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool appCursorBackgroundLooksBright(AppCursorRawPixel raw);
#line 109 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorRestore();
#line 120 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorSetPosition(int x, int y, int buttons);
#line 127 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorCaptureAndDraw();
#line 159 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void appCursorRefreshAfterRedraw();
#line 169 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool appMouseRead(String input, AppMouseReport &report);
#line 190 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void restartWithFallbackVideo(const char *label);
#line 209 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void releaseSerialControllerVideo();
#line 269 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void drawCube();
#line 326 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void boolUpdate();
#line 338 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void boolTru();
#line 348 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static bool handleUtilityMouse(String input);
#line 367 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void controller();
#line 405 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 416 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 421 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 430 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void handleKernelFlash();
#line 488 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void visuals();
#line 551 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void repeatedVisuals();
#line 587 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void setup();
#line 620 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
void loop();
#line 58 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CUtility\\CUtility.ino"
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

// ─── NEW: 3D CUBE SUPPORT ─────────────────────────────────────────────────────

static void releaseSerialControllerVideo() {
  for (uint8_t i = 0; i < 12; ++i) {
    Serial1.println("CVBSOFF");
    Serial1.println("VDAPPVIDEO");
    Serial1.flush();
    delay(80);
  }
}

struct Vec3 {
  float x, y, z;
};

Vec3 rotateXY(const Vec3 &v, float ax, float ay);

// cube position & size (you can tweak these)
int cubeCX   = 230;
int cubeCY   =  50;
int cubeSize =  20;

unsigned long lastFrameMicros = 0;
float        currentFPS       = 0;

// current rotation angles
float angleX = 0;
float angleY = 0;

// 6 faces (each is 4 vertex indices into the cube’s 8 corners)
const int faces[6][4] = {
  {0,1,2,3},  // back
  {4,5,6,7},  // front
  {0,1,5,4},  // bottom
  {3,2,6,7},  // top
  {1,2,6,5},  // right
  {0,3,7,4}   // left
};

// normals for unrotated faces
const Vec3 faceNormals[6] = {
  { 0,  0, -1},
  { 0,  0,  1},
  { 0, -1,  0},
  { 0,  1,  0},
  { 1,  0,  0},
  {-1,  0,  0}
};

// rotate a point around X then Y
Vec3 rotateXY(const Vec3 &v, float ax, float ay) {
  // rotate X
  float cosy = cos(ax), siny = sin(ax);
  float y1 = v.y * cosy - v.z * siny;
  float z1 = v.y * siny + v.z * cosy;
  // rotate Y
  float cosx = cos(ay), sinx = sin(ay);
  float x2 = v.x * cosx + z1 * sinx;
  float z2 = -v.x * sinx + z1 * cosx;
  return { x2, y1, z2 };
}

void drawCube() {
  // 1) clear previous cube area
  int r = cubeSize + 2;
  for (int yy = cubeCY - r; yy <= cubeCY + r; yy++) {
    for (int xx = cubeCX - r; xx <= cubeCX + r; xx++) {
      videodisplay.dot(xx, yy, 0);
    }
  }

  // 2) define the 8 cube corners
  Vec3 base[8] = {
    {-cubeSize, -cubeSize, -cubeSize},
    { cubeSize, -cubeSize, -cubeSize},
    { cubeSize,  cubeSize, -cubeSize},
    {-cubeSize,  cubeSize, -cubeSize},
    {-cubeSize, -cubeSize,  cubeSize},
    { cubeSize, -cubeSize,  cubeSize},
    { cubeSize,  cubeSize,  cubeSize},
    {-cubeSize,  cubeSize,  cubeSize}
  };

  // 3) rotate & project each corner
  struct P { int x,y; float z; } proj[8];
  const float fov = 128.0;
  for (int i = 0; i < 8; i++) {
    Vec3 r = rotateXY(base[i], angleX, angleY);
    float depth = r.z + cubeSize*3;            // shift z so everything is positive
    float scale = fov / (fov + depth);
    proj[i].x = (int)(r.x * scale) + cubeCX;
    proj[i].y = (int)(r.y * scale) + cubeCY;
    proj[i].z = r.z;                           // keep for depth sorting if desired
  }

  // 4) draw each face’s edges in grayscale based on its normal.z
  for (int f = 0; f < 6; f++) {
    // rotate face normal
    Vec3 rn = rotateXY(faceNormals[f], angleX, angleY);
    // simple back‑face cull: skip faces pointing away
    if (rn.z <= 0) continue;
    // new: map rn.z (0.0–1.0) into 150–200
    int shade = 150 + (int)(rn.z * 50);
    shade = constrain(shade, 200, 255);  // just in case

    uint16_t col = videodisplay.RGB(shade, shade, shade);
    // draw the quad as four lines
    for (int e = 0; e < 4; e++) {
      int i0 = faces[f][e];
      int i1 = faces[f][(e+1)%4];
      videodisplay.line(proj[i0].x, proj[i0].y,
                        proj[i1].x, proj[i1].y,
                        col);
    }
  }
}

// ─── END 3D CUBE SUPPORT ──────────────────────────────────────────────────────

void boolUpdate() {
  File file = SPIFFS.open("/evil.txt", FILE_WRITE);
  if (file) {
    file.seek(0);
    file.print("");
    file.println(boolRes);
    file.close();
    Serial.printf("Bool updated to %s\n", boolRes);
  } else {
    Serial.println("Failed to update bool!");
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
}

static bool handleUtilityMouse(String input) {
  AppMouseReport mouse;
  if (!appMouseRead(input, mouse)) return false;
  appCursorRestore();
  appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
  if (mouse.dx != 0 || mouse.dy != 0) {
    angleY += (float)mouse.dx * 0.01f;
    angleX += (float)mouse.dy * 0.01f;
  }
  if (mouse.leftPressed) {
    if (appCursorY >= 78 && appCursorY <= 96) {
      if (appCursorX < 318) videodisplay.startTX();
      else videodisplay.startRX();
    }
  }
  appCursorCaptureAndDraw();
  return true;
}

void controller() {
  static String ctrlInput1 = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      ctrlInput1.trim();
      Serial.println(ctrlInput1);
      if (handleUtilityMouse(ctrlInput1)) {
        ctrlInput1 = "";
        continue;
      }
      AppCursorDrawGuard cursorGuard;
      if (ctrlInput1 == "Escape") {
        boolRes = "trueKernel";
        boolUpdate();
        restartWithFallbackVideo("Returning home");
      } else if (ctrlInput1 == "RightGUI (Win) +"){
        restartWithFallbackVideo("Restarting");
      } else if (ctrlInput1 == "BL1X"){
        BTAV = true;
        visuals();
      } else if (ctrlInput1 == "b"){
        videodisplay.reset();
      } else if (ctrlInput1 == "r"){
        videodisplay.startRX();
      } else if (ctrlInput1 == "t"){
        videodisplay.startTX();
      }
      if(ctrlInput1 != "rlsd"){lastChar = String(ctrlInput1.substring(0,1) + " - Last Character Input");}
      ctrlInput1 = "";
    } else {
      ctrlInput1 += c;
    }
  }
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
  Serial.printf("Kernel size: %d bytes\n", kernelSize1);
  kernelFlashVideoStart(kernelSize1, "Flashing kernel");

  if (!Update.begin(kernelSize1)) {
    Serial.printf("Not enough space for the kernel! Available: %d bytes, Needed: %d bytes\n",
                  ESP.getFreeSketchSpace(), kernelSize1);
    Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
    kernelFlashVideoProgress(100, "Kernel flash failed");
    Serial1.flush();
    kernelFile1.close();
    return;
  }

  Serial.printf("Free Sketch Space: %d bytes\n", ESP.getFreeSketchSpace());
  uint8_t buffer1[2048];
  size_t flashedKernelBytes = 0;
  while (kernelFile1.available()) {
    int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
    int written1 = Update.write(buffer1, len1);
    Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n",
                  len1, len1, written1);
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
void visuals() {
  /*
  videodisplay.fillCircle(177,60,20,videodisplay.RGB(255,255,255));
  videodisplay.line(165,50,170,50,0);
  videodisplay.line(170,50,185,70,0);
  videodisplay.line(185,70,190,70,0);
  videodisplay.line(175,60,165,70,0);
  videodisplay.line(165,49,170,49,0);
  videodisplay.line(171,49,186,69,0);
  videodisplay.line(186,69,191,69,0);
  videodisplay.line(175,59,166,69,0);
  */
  videodisplay.setFont(Font6x8);
  videodisplay.setCursor(0,10);
  int heap = ESP.getFreeHeap();
  int theap = ESP.getHeapSize();
  int CSize = ESP.getFlashChipSize();
  int CSpd = ESP.getFlashChipSpeed();
  int REV = ESP.getChipRevision();
  uint64_t chipID = ESP.getEfuseMac();
  unsigned long CCount = ESP.getCycleCount();
  int CFM = ESP.getCpuFreqMHz();
  int verified = 0;
  String a = String(heap / 1024) + "KB / " + String(theap / 1024) + " RAM";
  String b = String(CSize / 1024) + "KB Flash Storage";
  String c = String(CSpd / 1000000) + " Flash Frequency";
  String d = "Chip ID: " + String(chipID);
  String e = String(REV) + " Chip Revision";
  String f = String(CCount) + " Cycles Till Log";
  String g = String(CFM) + " MHz CPU Frequency";
  videodisplay.println(String(ESP.getChipModel()).c_str());
  videodisplay.println(a.c_str());
  videodisplay.println(e.c_str());
  videodisplay.println(String(ESP.getSdkVersion()).c_str());
  videodisplay.println(b.c_str());
  videodisplay.println(c.c_str());
  videodisplay.println(d.c_str());
  videodisplay.println(f.c_str());
  videodisplay.println(g.c_str());
  if(BTAV){videodisplay.println("BTE Available");} else {videodisplay.println("BTE Unavailable");}
  videodisplay.println("------------------------------");
  File rootr = SD.open("/System/kernel.bin");
  if (rootr){videodisplay.println("Kernel File Integrity Verified");verified++;
  } else {videodisplay.println("Kernel File Integrity Failed to Verify");}
  rootr.close();
  File root = SD.open("/System/bootloader.bin");
  if (root){videodisplay.println("Bootloader File Integrity Verified");verified++;
  } else {videodisplay.println("Bootloader File Integrity Failed to Verify");}
  root.close();
  if (verified == 2){videodisplay.println("Primary File System Verified Successfully");} else {
    String h = "System verification failed with " + String(verified) + " out of (2)";
    videodisplay.println(h.c_str());
  }
  videodisplay.println("-----------------------------------------");
  int indf  = heap_caps_check_integrity(4096, true);
  if(indf == 1){videodisplay.println("Memory Corruption : Test Passed / 0xffffff <-> 0x000000");} else {videodisplay.println("Memory Corruption : Test Failed !Corrupted Hash Found!");}
  videodisplay.rect(202, 10, 290, 95, videodisplay.RGB(255,255,255));
  videodisplay.println("displayMode1:CompMode:MODEPAL576Idiv3,25(DAC),true(VoltDiv)");
  videodisplay.println("displayMode2:CompMode:MODEPALColor288Pmid,25(DAC),true(VoltDiv)");
  videodisplay.println("t - Invoke VD(TX) | r - Invoke VD(RX)");
  allowRV = true;
}

void repeatedVisuals() {
  unsigned long now   = micros();
  unsigned long delta = now - lastFrameMicros;
  if (delta > 0) {
    currentFPS = 1e6 / delta;
  }
  lastFrameMicros = now;
  drawCube();
  angleX += 0.02;
  angleY += 0.03;
  videodisplay.setCursor(204, 28);
  char buf[16];
  snprintf(buf, sizeof(buf), "FPS: %.1f", currentFPS);
  String fpsLine = String("3D Render : ") + buf;
  String fpsLine1 = buf + String(" MaxRender:60");
  videodisplay.setCursor(204, 12);
  unsigned long CCount = ESP.getCycleCount();
  String a = String(CCount) + " Cycles";
  videodisplay.println(a.c_str());
  videodisplay.setCursor(204, 20);
  videodisplay.print(lastChar.c_str());
  videodisplay.setCursor(260,28);
  videodisplay.println(fpsLine.c_str());
  videodisplay.setCursor(260,36);
  videodisplay.println(fpsLine1.c_str());
  videodisplay.setCursor(260,44);
  videodisplay.println("Shape : Box 4SQ");
  videodisplay.setCursor(260,52);
  videodisplay.println("Calc Mode : SIN/COS");
  videodisplay.setCursor(260,60);
  videodisplay.println("VEC3 X,Y,Z / GRAYSCALE");
  videodisplay.setCursor(260,68);
  videodisplay.println("VD.DOT Render Style");
}


void setup() {
  pinMode(SPEAKER_PIN, 0);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(256000,SERIAL_8N1, 16, 17);
  Serial1.println("res");
  if (!SPIFFS.begin(true)){
    return;
  }
  SPI.begin(18,19,23,5);
  if(!SD.begin(5)){
    Serial.println("SD Failed to init");
  } else {
    Serial.println("EUREKA");
  }
  boolTru();
  if (boolRes == ""){
      boolRes = "false";
      boolUpdate();
  }
  boolRes.trim();
  if (boolRes == "false"){
    tone(SPEAKER_PIN, 0);
    videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
    releaseSerialControllerVideo();
    visuals();
  } else if (boolRes == "trueKernel"){
    tone(SPEAKER_PIN, 0);
    boolRes = "false";
    boolUpdate();
    handleKernelFlash();
  }
}
void loop() {
  controller();
  if(allowRV){
    AppCursorDrawGuard cursorGuard;
    repeatedVisuals();
  }else{return;}
}

