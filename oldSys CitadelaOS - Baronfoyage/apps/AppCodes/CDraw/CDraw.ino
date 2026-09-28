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
  videodisplay.fillRect(x, y, 1, 1, g);
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
  videodisplay.circle(cursorX, cursorY, CIRCLE_R, 255);
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
    videodisplay.line(250 + gx, 150, 250 + gx, 180, i);
  }
  int markX = 250 + map(grayscaleSelected, 0, 255, 0, 149);
  int markerW = 3;
  int markerH = 32;
  int mx = markX - markerW/2;
  int my = 150 - 2;
  if (mx < 250) mx = 250;
  int inverted = 255 - grayscaleSelected;
  videodisplay.fillRect(mx, my, 1, markerH, inverted);
  videodisplay.fillRect(mx+1, my, 1, markerH, grayscaleSelected);
  videodisplay.fillRect(mx+2, my, 1, markerH, inverted);
  videodisplay.rect(250, 149, 150, 32, 255);
  videodisplay.rect(250, 130, 150, 11, 255);
  videodisplay.rect(250, 140, 150, 11, 255);
  videodisplay.rect(250, 180, 150, 10, 255);
  videodisplay.rect(250, 120, 150, 11, 255);
  videodisplay.rect(250, 110, 150, 11, 255);
  videodisplay.rect(250, 100, 150, 11, 255);
  videodisplay.rect(250, 7, 148, 183, 255);
  videodisplay.rect(0, 7, 248, 183, 255);

  videodisplay.setCursor(252, 131);
  videodisplay.println("   Tab to save");
  videodisplay.setCursor(252, 141);
  videodisplay.println("+/- to adjust grayscale");
  videodisplay.fillCircle(257, 135, 3, 50);
  videodisplay.setCursor(252, 101);
  videodisplay.println("i/o to reshape cursor");
  videodisplay.setCursor(252, 111);
  videodisplay.print("   Draw: ");
  videodisplay.fillCircle(257, 115, 3, 255);
  videodisplay.fillCircle(257, 135, 3, 50);
  if (screenFling) videodisplay.println("ON ");
  else videodisplay.println("OFF");
  videodisplay.setCursor(252, 121);
  videodisplay.print("   Eraser: ");
  videodisplay.circle(257, 125, 3, 255);
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

void controller() {
  static String buf;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      buf.trim();
      Serial.println(buf);
      if (buf == "Escape") {
        Serial1.println("VDCLoadNSS");
        boolRes = "trueKernel"; boolUpdate();
        ESP.restart();
      } else if (buf == "Tab") {
        saveDrawingBMP();
      } else if (buf == "RightGUI (Win) +") {
        ESP.restart();
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

void handleKernelFlash() {
  File k = SD.open("/System/kernel.bin");
  if (!k) return;
  size_t sz = k.size();
  if (!Update.begin(sz)) { k.close(); return; }
  uint8_t b[2048];
  while (k.available()) {
    int r = k.read(b, sizeof(b));
    Update.write(b, r);
  }
  k.close();
  if (Update.end()) ESP.restart();
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
  videodisplay.init(CompMode::MODEPAL576Idiv3, 25, false);
  videodisplay.fillRect(0, 0, 720, 576, 0);
  videodisplay.show();
  if (boolRes == "false") {
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
