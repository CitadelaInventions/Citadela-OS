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

// ─── NEW: 3D CUBE SUPPORT ─────────────────────────────────────────────────────

struct Vec3 {
  float x, y, z;
};

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
void controller() {
  static String ctrlInput1 = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      ctrlInput1.trim();
      Serial.println(ctrlInput1);
      if (ctrlInput1 == "Escape") {
        Serial1.println("VDCLoadNSS");
        boolRes = "trueKernel";
        boolUpdate();
        ESP.restart();
      } else if (ctrlInput1 == "RightGUI (Win) +"){
        ESP.restart();
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

void handleKernelFlash() {
  File kernelFile1 = SD.open("/System/kernel.bin");
  if (!kernelFile1) {
    Serial.println("Kernel file not found!");
    return;
  }

  size_t kernelSize1 = kernelFile1.size();
  Serial.printf("Kernel size: %d bytes\n", kernelSize1);

  if (!Update.begin(kernelSize1)) {
    Serial.printf("Not enough space for the kernel! Available: %d bytes, Needed: %d bytes\n",
                  ESP.getFreeSketchSpace(), kernelSize1);
    Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
    kernelFile1.close();
    return;
  }

  Serial.printf("Free Sketch Space: %d bytes\n", ESP.getFreeSketchSpace());
  uint8_t buffer1[2048];
  while (kernelFile1.available()) {
    int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
    int written1 = Update.write(buffer1, len1);
    Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n",
                  len1, len1, written1);

    if (written1 != len1) {
      Serial.printf("Write failed! Expected: %d, Wrote: %d\n", len1, written1);
      Update.abort();
      kernelFile1.close();
      return;
    }
  }

  kernelFile1.close();
  if (Update.end()) {
    Serial.println("Kernel written to flash successfully.");
    ESP.restart();
  } else {
    Serial.printf("Update failed: %s\n", Update.errorString());
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
  videodisplay.rect(202, 10, 290, 95, 255);
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
  if(allowRV){repeatedVisuals();}else{return;}
}
