#include <Arduino.h>
#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
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

#define SPEAKER_PIN    12
#define MAX_POINTS   1000
#define STEP_INTERVAL 50

int CIRCLE_R = 3;

const bool USE_32BIT = true;

struct Point { uint16_t x, y; uint8_t g; };

String boolRes = "";
bool screenFling = false;

bool upPressed    = false;
bool downPressed  = false;
bool leftPressed  = false;
bool rightPressed = false;

Point  points[MAX_POINTS];
uint16_t pointCount = 0;

int cursorX = 125;
int cursorY =  95;
int oldX = -100, oldY = -100;

unsigned long lastStepTime = 0;
CompositeColorDAC videodisplay;

#line 40 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static uint32_t grayColor(uint8_t value);
#line 80 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void restartWithFallbackVideo(const char *label);
#line 97 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void releaseSerialControllerVideo();
#line 110 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void boolUpdate();
#line 116 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void boolTru();
#line 124 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void drawPoint(uint16_t x, uint16_t y, uint8_t g);
#line 128 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void addDot();
#line 138 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void eraseDotsAt(int cx, int cy);
#line 160 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void eraseOldCursor();
#line 181 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void drawCursor();
#line 187 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void stepAndDraw();
#line 208 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void staticUi();
#line 258 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void grayscalepick(int picked);
#line 275 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void changeGrayscale(int delta);
#line 282 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void handleDrawMouse(String input);
#line 323 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void controller();
#line 386 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void kernelFlashVideoProgress(int percent, const char *label);
#line 397 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void kernelFlashVideoBytes(size_t writtenBytes);
#line 402 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static void kernelFlashVideoStart(size_t totalBytes, const char *label);
#line 411 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void handleKernelFlash();
#line 452 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void writeLE16(File &f, uint16_t v);
#line 456 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void writeLE32(File &f, uint32_t v);
#line 463 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
String makeNextFilename();
#line 474 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void saveDrawingBMP();
#line 559 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void setup();
#line 593 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
void loop();
#line 40 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CDraw\\CDraw.ino"
static inline uint32_t grayColor(uint8_t value) {
  return videodisplay.RGB(value, value, value);
}

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

static bool readDrawMouseReport(String input, AppMouseReport &report);

static bool drawMouseWasLeftDown = false;

static bool readDrawMouseReport(String input, AppMouseReport &report) {
  input.trim();
  if (!input.startsWith("MOUSE")) return false;
  report.x = cursorX;
  report.y = cursorY;
  report.buttons = 0;
  report.dx = 0;
  report.dy = 0;
  report.wheel = 0;
  int parsed = sscanf(input.c_str(), "MOUSE %d %d %d %d %d %d", &report.x, &report.y, &report.buttons, &report.dx, &report.dy, &report.wheel);
  if (parsed >= 3) {
    report.leftDown = (report.buttons & 0x01) != 0;
    report.leftPressed = report.leftDown && !drawMouseWasLeftDown;
    drawMouseWasLeftDown = report.leftDown;
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

int grayscaleSelected = 128;

bool eraserMode = false;

void boolUpdate() {
  File f = SPIFFS.open("/evil.txt", FILE_WRITE);
  if (!f) return;
  f.seek(0); f.print(""); f.println(boolRes);
  f.close();
}
void boolTru() {
  File f = SPIFFS.open("/evil.txt", FILE_READ);
  if (!f) return;
  boolRes = "";
  while (f.available()) boolRes += char(f.read());
  f.close();
}

inline void drawPoint(uint16_t x, uint16_t y, uint8_t g) {
  videodisplay.fillRect(x, y, 1, 1, grayColor(g));
}

void addDot() {
  if (pointCount < MAX_POINTS) {
    points[pointCount++] = { (uint16_t)cursorX,
                             (uint16_t)cursorY,
                             (uint8_t)grayscaleSelected };
  }
  drawPoint(cursorX, cursorY, (uint8_t)grayscaleSelected);
  videodisplay.show();
}

void eraseDotsAt(int cx, int cy) {
  const int radius = CIRCLE_R;
  const int rr = radius * radius;
  uint16_t writeIndex = 0;
  bool removed = false;
  for (uint16_t i = 0; i < pointCount; ++i) {
    int dx = (int)points[i].x - cx;
    int dy = (int)points[i].y - cy;
    if (dx*dx + dy*dy > rr) {
      // keep
      points[writeIndex++] = points[i];
    } else {
      removed = true;
    }
  }
  if (removed) {
    pointCount = writeIndex;
  }
}



void eraseOldCursor() {
  int diam = CIRCLE_R * 2 + 1;
  int xx0 = oldX - CIRCLE_R;
  int yy0 = oldY - CIRCLE_R;

  if (xx0 < 0) xx0 = 0;
  if (yy0 < 0) yy0 = 0;
  int w = diam;
  int h = diam;
  if (xx0 + w > 720) w = 720 - xx0;
  if (yy0 + h > 576) h = 576 - yy0;
  if (w <= 0 || h <= 0) return;
  videodisplay.fillRect(xx0, yy0, w, h, 0);
  for (uint16_t i = 0; i < pointCount; ++i) {
    auto &p = points[i];
    if (p.x >= xx0 && p.x < xx0 + w &&
        p.y >= yy0 && p.y < yy0 + h) {
      drawPoint(p.x, p.y, p.g);
    }
  }
}
void drawCursor() {
  videodisplay.circle(cursorX, cursorY, CIRCLE_R, grayColor(255));
  videodisplay.show();
  oldX = cursorX;
  oldY = cursorY;
}
void stepAndDraw() {
  bool moved = false;
  if (upPressed)    { cursorY = max(11,   cursorY - 1); moved = true; }
  if (downPressed)  { cursorY = min(184, cursorY + 1); moved = true; }
  if (leftPressed)  { cursorX = max(5,   cursorX - 1); moved = true; }
  if (rightPressed) { cursorX = min(244, cursorX + 1); moved = true; }
  if (!moved) return;
  if (cursorX < 245 && cursorY < 185) {
    if (eraserMode) {
      eraseOldCursor();
      eraseDotsAt(cursorX, cursorY);
      drawCursor();
    } else if (screenFling) {
      addDot();
    } else {
      eraseOldCursor();
      drawCursor();
    }
  }
}

void staticUi(){
  videodisplay.setCursor(252, 181);
  videodisplay.setFont(Font6x8);
  videodisplay.println("Enter to toggle draw");
  for (int i = 0; i <= 255; ++i) {
    int gx = map(i, 0, 255, 0, 149);
    videodisplay.line(250 + gx, 150, 250 + gx, 180, grayColor(i));
  }
  int markX = 250 + map(grayscaleSelected, 0, 255, 0, 149);
  int markerW = 3;
  int markerH = 32;
  int mx = markX - markerW/2;
  int my = 150 - 2;
  if (mx < 250) mx = 250;
  int inverted = 255 - grayscaleSelected;
  videodisplay.fillRect(mx, my, 1, markerH, grayColor(inverted));
  videodisplay.fillRect(mx+1, my, 1, markerH, grayColor(grayscaleSelected));
  videodisplay.fillRect(mx+2, my, 1, markerH, grayColor(inverted));
  uint32_t white = grayColor(255);
  videodisplay.rect(250, 149, 150, 32, white);
  videodisplay.rect(250, 130, 150, 11, white);
  videodisplay.rect(250, 140, 150, 11, white);
  videodisplay.rect(250, 180, 150, 10, white);
  videodisplay.rect(250, 120, 150, 11, white);
  videodisplay.rect(250, 110, 150, 11, white);
  videodisplay.rect(250, 100, 150, 11, white);
  videodisplay.rect(250, 7, 148, 183, white);
  videodisplay.rect(0, 7, 248, 183, white);

  videodisplay.setCursor(252, 131);
  videodisplay.println("   Tab to save");
  videodisplay.setCursor(252, 141);
  videodisplay.println("+/- to adjust grayscale");
  videodisplay.fillCircle(257, 135, 3, grayColor(50));
  videodisplay.setCursor(252, 101);
  videodisplay.println("i/o to reshape cursor");
  videodisplay.setCursor(252, 111);
  videodisplay.print("   Draw: ");
  videodisplay.fillCircle(257, 115, 3, white);
  videodisplay.fillCircle(257, 135, 3, grayColor(50));
  if (screenFling) videodisplay.println("ON ");
  else videodisplay.println("OFF");
  videodisplay.setCursor(252, 121);
  videodisplay.print("   Eraser: ");
  videodisplay.circle(257, 125, 3, white);
  if (eraserMode) videodisplay.println("ON ");
  else videodisplay.println("OFF");
  videodisplay.show();
}

void grayscalepick(int picked){
  int gx;
  if (picked >= 250 && picked < 250 + 150) {
    gx = picked - 250;
  } else if (picked >= 0 && picked < 150) {
    gx = picked;
  } else if (picked >= 250) {
    gx = picked - 250;
  } else {
    gx = 0;
  }
  if (gx < 0) gx = 0;
  if (gx > 149) gx = 149;
  grayscaleSelected = map(gx, 0, 149, 0, 255);
  staticUi();
}

void changeGrayscale(int delta) {
  grayscaleSelected += delta;
  if (grayscaleSelected < 0) grayscaleSelected = 0;
  if (grayscaleSelected > 255) grayscaleSelected = 255;
  staticUi();
}

static void handleDrawMouse(String input) {
  AppMouseReport mouse;
  if (!readDrawMouseReport(input, mouse)) return;

  if (mouse.leftPressed && mouse.x >= 250 && mouse.x < 400) {
    if (mouse.y >= 110 && mouse.y < 121) {
      screenFling = !screenFling;
      if (screenFling) eraserMode = false;
      staticUi();
    } else if (mouse.y >= 120 && mouse.y < 131) {
      eraserMode = !eraserMode;
      if (eraserMode) screenFling = false;
      staticUi();
    } else if (mouse.y >= 130 && mouse.y < 141) {
      saveDrawingBMP();
    } else if (mouse.y >= 148 && mouse.y < 182) {
      grayscalepick(mouse.x);
    }
  }

  int nextX = constrain(mouse.x, 5, 244);
  int nextY = constrain(mouse.y, 11, 184);
  bool moved = (nextX != cursorX || nextY != cursorY);
  if (moved || mouse.leftDown || mouse.leftPressed) {
    eraseOldCursor();
    cursorX = nextX;
    cursorY = nextY;
    if (mouse.leftDown && cursorX < 245 && cursorY < 185) {
      if (eraserMode) {
        eraseDotsAt(cursorX, cursorY);
      } else {
        if (pointCount < MAX_POINTS) {
          points[pointCount++] = { (uint16_t)cursorX, (uint16_t)cursorY, (uint8_t)grayscaleSelected };
        }
        drawPoint(cursorX, cursorY, (uint8_t)grayscaleSelected);
      }
    }
    drawCursor();
  }
}

void controller() {
  static String buf;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      buf.trim();
      Serial.println(buf);
      if (buf.startsWith("MOUSE")) {
        handleDrawMouse(buf);
      } else if (buf == "Escape") {
        boolRes = "trueKernel"; boolUpdate();
        restartWithFallbackVideo("Returning home");
      } else if (buf == "Tab") {
        saveDrawingBMP();
      } else if (buf == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting");
      } else if (buf == "Enter") {
        screenFling = !screenFling;
        if (screenFling) {
          eraserMode = false;
          eraseOldCursor();
          videodisplay.show();
        } else {
          drawCursor();
        }
        staticUi();

      } else if (buf == "-") {
        changeGrayscale(-5);
      } else if (buf == "+" || buf == "=") {
        changeGrayscale(+5);
      } else if (buf == "LeftShift +") {
        eraserMode = !eraserMode;
        if (eraserMode) {
          screenFling = false;
        }
        staticUi();
        eraseOldCursor();
        drawCursor();
      } else if (buf == "UpArrow") {
        upPressed = true;
      } else if (buf == "DownArrow") {
        downPressed = true;
      } else if (buf == "LeftArrow") {
        leftPressed = true;
      } else if (buf == "RightArrow") {
        rightPressed = true;
      } else if (buf == "rlsd") {
        upPressed = downPressed = leftPressed = rightPressed = false;
      } else if (buf == "i") {
        if(CIRCLE_R<20){eraseOldCursor();CIRCLE_R +=1;drawCursor();}
      } else if (buf == "o") {
        if(CIRCLE_R>0){eraseOldCursor();CIRCLE_R -=1;drawCursor();}
      }
      buf = "";
    } else {
      buf += c;
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
  File k = SD.open("/System/kernel.bin");
  if (!k) return;
  size_t sz = k.size();
  kernelFlashVideoStart(sz, "Flashing kernel");
  if (!Update.begin(sz)) {
    kernelFlashVideoProgress(100, "Kernel flash failed");
    Serial1.flush();
    k.close();
    return;
  }
  uint8_t b[2048];
  size_t flashedKernelBytes = 0;
  while (k.available()) {
    int r = k.read(b, sizeof(b));
    int w = Update.write(b, r);
    if (w > 0) {
      flashedKernelBytes += w;
      kernelFlashVideoBytes(flashedKernelBytes);
    }
    if (w != r) {
      Update.abort();
      kernelFlashVideoProgress(100, "Kernel flash failed");
      Serial1.flush();
      k.close();
      return;
    }
  }
  k.close();
  if (Update.end()) {
    kernelFlashVideoBytes(sz);
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
  } else {
    kernelFlashVideoProgress(100, "Kernel flash failed");
    Serial1.flush();
  }
}

void writeLE16(File &f, uint16_t v) {
  f.write((uint8_t)(v & 0xFF));
  f.write((uint8_t)((v >> 8) & 0xFF));
}
void writeLE32(File &f, uint32_t v) {
  f.write((uint8_t)(v & 0xFF));
  f.write((uint8_t)((v >> 8) & 0xFF));
  f.write((uint8_t)((v >> 16) & 0xFF));
  f.write((uint8_t)((v >> 24) & 0xFF));
}

String makeNextFilename() {
  SD.mkdir("/Images");
  char buf[32];
  for (int i = 0; i < 10000; ++i) {
    sprintf(buf, "/Images/drawing%04d.bmp", i);
    if (!SD.exists(buf)) {
      return String(buf);
    }
  }
  return String("/Images/drawing9999.bmp");
}
void saveDrawingBMP() {
  const int imgW = 245;
  const int imgH = 185;
  if (!SD.begin(5)) {
    Serial.println("BMP save: SD not initialized or missing");
  }
  SD.mkdir("/Images");
  String fname = makeNextFilename();
  Serial.print("Saving BMP -> ");
  Serial.println(fname);
  File f = SD.open(fname.c_str(), FILE_WRITE);
  if (!f) {
    Serial.println("BMP save: failed to open file");
    return;
  }
  const int bpp = USE_32BIT ? 32 : 24;
  const int bytesPerPixel = (bpp / 8);
  int rowBytesRaw = imgW * bytesPerPixel;
  int pad = (4 - (rowBytesRaw % 4)) % 4;
  uint32_t rowBytesWithPad = (uint32_t)rowBytesRaw + pad;
  uint32_t dataSize = rowBytesWithPad * (uint32_t)imgH;
  const uint32_t bfOffBits = 14 + 40;
  uint32_t bfSize = bfOffBits + dataSize;
  f.write('B');
  f.write('M');
  writeLE32(f, bfSize);        // bfSize
  writeLE16(f, 0);             // bfReserved1
  writeLE16(f, 0);             // bfReserved2
  writeLE32(f, bfOffBits);     // bfOffBits

  // ---- BITMAPINFOHEADER (40 bytes) ----
  writeLE32(f, 40);            // biSize
  writeLE32(f, imgW);          // biWidth
  writeLE32(f, imgH);          // biHeight (positive -> bottom-up)
  writeLE16(f, 1);             // biPlanes
  writeLE16(f, bpp);           // biBitCount (24 or 32)
  writeLE32(f, 0);             // biCompression (BI_RGB)
  writeLE32(f, dataSize);      // biSizeImage
  writeLE32(f, 2835);          // biXPelsPerMeter (72 DPI ~ 2835)
  writeLE32(f, 2835);          // biYPelsPerMeter
  writeLE32(f, 0);             // biClrUsed (0 for truecolor)
  writeLE32(f, 0);             // biClrImportant
  size_t rowBufSize = (size_t)rowBytesRaw + pad;
  uint8_t *rowBuf = (uint8_t*)malloc(rowBufSize);
  if (!rowBuf) {
    Serial.println("BMP save: row buffer OOM");
    f.close();
    return;
  }
  if (pad) {
    for (int i = 0; i < pad; ++i) rowBuf[rowBytesRaw + i] = 0;
  }
  for (int y = imgH - 1; y >= 0; --y) {
    memset(rowBuf, 0, rowBufSize);
    for (uint16_t i = 0; i < pointCount; ++i) {
      const Point &p = points[i];
      if ((int)p.y == y && p.x < (uint16_t)imgW) {
        uint8_t v = p.g; // grayscale
        size_t idx = (size_t)p.x * bytesPerPixel;
        rowBuf[idx + 0] = v; // B
        rowBuf[idx + 1] = v; // G
        rowBuf[idx + 2] = v; // R
        if (bytesPerPixel == 4) {
          rowBuf[idx + 3] = 0xFF;
        }
      }
    }
    size_t toWrite = (size_t)rowBytesRaw + pad;
    size_t written = f.write(rowBuf, toWrite);
    if (written != toWrite) {
      Serial.printf("BMP save: write error (row %d) wrote %u of %u\n", y, (unsigned)written, (unsigned)toWrite);
      break;
    }
  }
  free(rowBuf);
  f.close();

  Serial.print("BMP saved as ");
  Serial.print(bpp);
  Serial.println("-bit.");
  tone(SPEAKER_PIN, 1500, 80);
  delay(90);
  tone(SPEAKER_PIN, 0);
}

void setup() {
  pinMode(SPEAKER_PIN, OUTPUT);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(256000, SERIAL_8N1, 16, 17);
  Serial1.println("res");
  SPIFFS.begin(true);
  SPI.begin(18,19,23,5);
  if (!SD.begin(5)) {
    Serial.println("Warning: SD.begin failed in setup. Make sure CS pin and wiring are correct.");
  }
  boolTru();
  if (boolRes == "") {
    boolRes = "false";
    boolUpdate();
  }
  boolRes.trim();
  tone(SPEAKER_PIN, 0);
  videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
  videodisplay.fillRect(0, 0, 720, 576, 0);
  videodisplay.show();
  if (boolRes == "false") {
    releaseSerialControllerVideo();
    staticUi();
  } else if (boolRes == "trueKernel") {
    boolRes = "false"; boolUpdate();
    handleKernelFlash();
  }
  oldX = cursorX;
  oldY = cursorY;
  drawCursor();
  lastStepTime = millis();
}

void loop() {
  controller();
  unsigned long now = millis();
  if ((upPressed || downPressed || leftPressed || rightPressed)
      && (now - lastStepTime >= STEP_INTERVAL)) {
    lastStepTime = now;
    stepAndDraw();
  }
}

