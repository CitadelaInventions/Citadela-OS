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

#define SCREEN_WIDTH  380
#define SCREEN_HEIGHT 285
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
  clearTextAreaAtOffset(contentOffsetCurrent, videodisplay.RGB(255,255,255));
}
void handleKernelFlash() {
    File kernelFile1 = SD.open("/System/kernel.bin");
    if (!kernelFile1) {
        Serial.println("Kernel file not found!");
        return;
    }

    size_t kernelSize1 = kernelFile1.size();
    Serial.printf("Kernel size: %u bytes\n", (unsigned)kernelSize1);

    if (!Update.begin(kernelSize1)) {
        Serial.printf("Not enough space for the kernel! Available: %u bytes, Needed: %u bytes\n", (unsigned)ESP.getFreeSketchSpace(), (unsigned)kernelSize1);
        Serial.printf("Update Error (%d): %s\n", Update.getError(), Update.errorString());
        kernelFile1.close();
        return;
    }

    Serial.printf("Free Sketch Space: %u bytes\n", (unsigned)ESP.getFreeSketchSpace());
    uint8_t buffer1[2048];
    while (kernelFile1.available()) {
        int len1 = kernelFile1.read(buffer1, sizeof(buffer1));
        int written1 = Update.write(buffer1, len1);
        Serial.printf("Read %d bytes, Attempting to write %d bytes... Wrote %d bytes\n", len1, len1, written1);

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

void updateLayoutMetrics() {
  int rectX = 6;
  int rectW = SCREEN_WIDTH - rectX * 2;
  int usable = rectW - 8;
  int cp = usable / max(1, R_CHAR_W);
  if (cp < 8) cp = 8;
  if (cp > LINE_SIZE - 4) cp = LINE_SIZE - 4;
  CHARS_PER_LINE = cp;
  Serial.printf("Layout metrics updated: R_CHAR_W=%d, CHARS_PER_LINE=%d\n", R_CHAR_W, CHARS_PER_LINE);
  return;
}

void reflowAllLines() {
  String full = "";
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

  clearTextAreaAtOffset(contentOffsetAfterClear, videodisplay.RGB(0,0,0));

  videodisplay.setFont(Font8x8);
  videodisplay.setCursor((SCREEN_WIDTH/2)-76, 55);
  videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  videodisplay.print("CITADELA AI BROWSER");
  videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
  // restore font based on current R_CHAR_W/H
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
}

void drawLambda(int cx, int cy, float s, uint32_t fillColor, uint32_t strokeColor) {
  auto scaled = [&](int off) -> int {
    return (int)roundf(off * s);
  };

  int r = scaled(20);
  videodisplay.fillCircle(cx, cy, r, videodisplay.RGB(220,220,220));

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
  while (pageLineCount >= MAX_PAGE_LINES) {
    for (int i = 1; i < pageLineCount; ++i) {
      strncpy(pageLines[i - 1], pageLines[i], LINE_SIZE);
      pageLines[i - 1][LINE_SIZE - 1] = 0;
    }
    pageLineCount--;
    if (pageWindowStart > 0) pageWindowStart--;
  }
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

  uint32_t bgRgb = (offset == contentOffsetAfterClear) ? videodisplay.RGB(0,0,0) : videodisplay.RGB(255,255,255);
  clearTextAreaAtOffset(offset, bgRgb);

  int rectX = 6;
  int rectY = offset + 3;
  int rectW = SCREEN_WIDTH - rectX * 2;
  if (offset == contentOffsetAfterClear) {
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  } else {
    videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
  }
  int baseY = rectY + 3;
  int y = baseY;
  applyFontForOffset(offset);

  for (int i = start; i < pageLineCount && i < start + linesPerScreen; i++) {
    videodisplay.setCursor(rectX + 4, y);
    videodisplay.print(pageLines[i]);
    y += R_CHAR_H + 2;
  }
  videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
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

  clearTextAreaAtOffset(contentOffsetDefault, videodisplay.RGB(255,255,255));

  appendWord(msg);
  pushCurrentLine();
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
bool viewToggleStates[VIEW_TOGGLES] = { false, false, false, false, false };

// pending toggles used while View window is open (edits go here)
bool viewPendingStates[VIEW_TOGGLES] = { false, false, false, false, false };
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
  updateLayoutMetrics();
  Serial.println("Loaded view config");
}
// commit pending view edits to live state and persist; re-render UI accordingly
void commitViewPending() {
  if (!viewPendingActive) return;
  bool prevLarger = viewToggleStates[0];
  for (int i = 0; i < VIEW_TOGGLES; ++i) viewToggleStates[i] = viewPendingStates[i];
  viewPendingActive = false;
  // apply Larger Text (only now)
  if (viewToggleStates[0] && !prevLarger) {
    R_CHAR_W = 8; R_CHAR_H = 8;
    videodisplay.setFont(Font8x8);
    updateLayoutMetrics();
    reflowAllLines(); // reflow (wrap) page content to new CHARS_PER_LINE
  } else if (!viewToggleStates[0] && prevLarger) {
    R_CHAR_W = 6; R_CHAR_H = 8;
    videodisplay.setFont(Font6x8);
    updateLayoutMetrics();
    reflowAllLines();
  }
  saveViewConfig();
  // apply re-render after committing
  if (viewMode == VM_ANSWER) displayPage(pageWindowStart, contentOffsetAfterClear);
  else displayPage(pageWindowStart, contentOffsetCurrent);
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

void showSingleTopLeftCompose() {
  int rectX = 100;
  int rectY = contentOffsetDefault - 20;
  int rectW = SCREEN_WIDTH - rectX * 2;
  int rectH = R_CHAR_H + 6;

  // top area: draw menu bar (white) or default top region
  drawMenuBar();

  // area below menu
  videodisplay.fillRect(0, 25, SCREEN_WIDTH, 80, videodisplay.RGB(255,255,255));
  videodisplay.fillRect(rectX, rectY, rectW, rectH, videodisplay.RGB(0,0,0));
  // title uses 8x8
  videodisplay.setFont(Font8x8);
  videodisplay.setCursor((SCREEN_WIDTH/2)-68, rectY - 10);
  videodisplay.setTextColor(videodisplay.RGB(255,255,255),videodisplay.RGB(0,0,0));
  videodisplay.println("Enter your prompt");
  // compose uses the current R_CHAR font
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
  videodisplay.setCursor(rectX + 2, rectY + 3);

  if (composeBuffer.length() == 0) {
    // nothing to show
  } else {
      // compute compose area max chars by rect width and R_CHAR_W
      int maxChars = max(8, (rectW - 8) / max(1, R_CHAR_W));
      int start = 0;
      int len = composeBuffer.length();
      if (len > maxChars) {
        start = len - maxChars;
      }
      String tail = composeBuffer.substring(start);
      videodisplay.print(tail.c_str());
  }
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

  displayPage(pageWindowStart, offset);

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
    uint32_t borderRgb = (offset == contentOffsetAfterClear) ? videodisplay.RGB(255,255,255) : videodisplay.RGB(0,0,0);
    // draw border (thin)
    videodisplay.rect(rectX, rectY, rectW, rectH, borderRgb);
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
  clearTextAreaAtOffset(contentOffsetDefault, videodisplay.RGB(255,255,255));
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
  displayShort(String("Prompt: ") + (composeBuffer.length() > 40 ? composeBuffer.substring(0,40) + "..." : composeBuffer));
  composeBuffer = "";
}


void drawMenuBar() {
  int barH = 25;
  videodisplay.fillRect(0, 10, SCREEN_WIDTH, barH, videodisplay.RGB(255,255,255)); // white bar

  // buttons
  int btnW = SCREEN_WIDTH / MENU_BUTTONS;
  // label font uses current char width
  if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);

  for (int i = 0; i < MENU_BUTTONS; ++i) {
    int x = i * btnW;
    int w = btnW;
    int y = 7;
    int h = barH - 7;

    bool highlight = false;
    if (menuMode && i == menuIndex) highlight = true;
    else if (windowMode && i == activeWindow) highlight = true; // highlight active window while in window mode

    if (highlight) {
      videodisplay.fillRect(x + 2, y, w - 4, h, videodisplay.RGB(0,0,0));
      videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
    } else {
      videodisplay.fillRect(x + 2, y, w - 4, h, videodisplay.RGB(255,255,255));
      videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
    }

    // center label
    int labelW = menuLabels[i].length() * R_CHAR_W;
    int cx = x + (w - labelW) / 2;
    int cy = y + (h - R_CHAR_H) / 2;
    videodisplay.setCursor(cx, cy+2);
    videodisplay.print(menuLabels[i].c_str());
  }

  // thin separator line under bar
  videodisplay.line(0, 25 - 1, SCREEN_WIDTH, 25 - 1, videodisplay.RGB(0,0,0));
}

void drawCenteredWindowBase(const String &title, int winW, int winH) {
  int wx = (SCREEN_WIDTH - winW) / 2;
  int wy = (SCREEN_HEIGHT - winH) / 2;

  videodisplay.fillRect(0,25,SCREEN_WIDTH, SCREEN_HEIGHT, videodisplay.RGB(0,255,255));
  videodisplay.fillRect(wx, wy, winW, winH, videodisplay.RGB(230,230,230));
  // border
  videodisplay.rect(wx, wy, winW, winH, videodisplay.RGB(255,255,255));

  // title bar
  videodisplay.fillRect(wx, wy, winW, R_CHAR_H + 6, videodisplay.RGB(0,0,0));
  videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  videodisplay.setFont(Font8x8);
  int titleW = title.length() * (R_CHAR_W + 1);
  videodisplay.setCursor(wx + (winW - titleW) / 2, wy + 2);
  videodisplay.print(title.c_str());
  videodisplay.setFont(Font6x8);
  // restore default text color for body
  videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(230,230,230));
}

// helper to produce a short truncated display string
String shortForDisplay(const String &s, int maxChars) {
  if (s.length() == 0) return String("<not set>");
  if (s.length() <= maxChars) return s;
  return s.substring(0, maxChars - 3) + "...";
}

// per-window draw functions (stubs) — fill these with your real UI logic
void drawConfigWindow() {
  drawCenteredWindowBase("Config", 260, 140);
  int wx = (SCREEN_WIDTH - 260) / 2;
  int wy = (SCREEN_HEIGHT - 140) / 2;
  videodisplay.setCursor(wx + 8, wy + 24);
  videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
  String r0 = String("Wifi SSID: " + wifiSSID + "\n");
  String r1 = String("Wifi Pass: " + wifiPass + "\n");
  String r2 = String("Wifi IP ADDR: " + wifiIP + "\n");
  videodisplay.println(r0.c_str());
  videodisplay.println(r1.c_str());
  videodisplay.println(r2.c_str());
}

void drawParamWindow() {
  drawCenteredWindowBase("Param", 260, 140);
  int wx = (SCREEN_WIDTH - 260) / 2;
  int wy = (SCREEN_HEIGHT - 140) / 2;
  videodisplay.setCursor(wx + 8, wy + 24);
  videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(255,255,255));
  String r0 = String("Token: \n" + hfToken + "\n");
  String r1 = String("Model: \n" + hfModel + "\n");
  String r2 = String("Space: \n" + hfSpace + "\n");
  videodisplay.println(r0.c_str());
  videodisplay.println(r1.c_str());
  videodisplay.println(r2.c_str());

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
      videodisplay.fillRect(x - 6, itemY - 2, 260, VIEW_ROW_H - 2, videodisplay.RGB(0,0,0));
      videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
    } else {
      videodisplay.fillRect(x - 6, itemY - 2, 260, VIEW_ROW_H - 2, videodisplay.RGB(230,230,230));
      videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(230,230,230));
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
  videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, videodisplay.RGB(0,0,0));
  videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  videodisplay.setFont(Font6x8);
  int labelW = String("Enable (Enter)").length() * R_CHAR_W;
  videodisplay.setCursor(enableX + (enableW - labelW) / 2, enableY + 3);
  videodisplay.print("Enable (Enter)");
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
    videodisplay.fillRect(x - 6, itemY - 2, rowW, rowH, videodisplay.RGB(0,0,0));
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  } else {
    videodisplay.fillRect(x - 6, itemY - 2, rowW, rowH, videodisplay.RGB(230,230,230));
    videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(230,230,230));
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
    videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, videodisplay.RGB(0,0,0));
    videodisplay.setTextColor(videodisplay.RGB(255,255,255), videodisplay.RGB(0,0,0));
  } else {
    videodisplay.fillRect(enableX, enableY, enableW, VIEW_ROW_H, videodisplay.RGB(230,230,230));
    videodisplay.setTextColor(videodisplay.RGB(0,0,0), videodisplay.RGB(230,230,230));
  }
  videodisplay.setFont(Font6x8);
  int labelW = String("Enable (Enter)").length() * R_CHAR_W;
  videodisplay.setCursor(enableX + (enableW - labelW) / 2, enableY + 3);
  videodisplay.print("Enable (Enter)");
}

void drawProtocolWindow() {
  drawCenteredWindowBase("Protocol", 300, 160);
  int wx = (SCREEN_WIDTH - 300) / 2;
  int wy = (SCREEN_HEIGHT - 160) / 2;
  videodisplay.setCursor(wx + 8, wy + 24);
  String protocol =
    "CB {MAIN ARG}        CBW {WIFI ARG}\n"
    "CBHF {AI ARG}        ---\n"
    "\n"
    "CBW0 {WIFI_SSID}     CBW1 {WIFI_PASS}\n"
    "CBW2 {CONNECT}       CBWSC {SCAN_SSIDs}\n"
    "CBWR0 {GET_SSID}     CBWR1 {GET_PASS}\n"
    "CBWR2 {GET_IP}       CBWS {STATUS}\n"
    "\n"
    "CBHFA {PROMPT}       CBHFTS {SET_TOKEN}\n"
    "CBHFM {SET_MODEL}    CBHFS {SET_SPACE}\n"
    "CBHFR {START_TX}     CBHFT {END_TX}\n"
    "CBHFRT {GET_TOKEN}   CBHFRM {GET_MODEL}\n"
    "CBHFRS {GET_SPACE}   ---\n";
  videodisplay.println(protocol.c_str());
}
  
void drawToolsWindow() {
  drawCenteredWindowBase("Tools", 260, 140);
  int wx = (SCREEN_WIDTH - 260) / 2;
  int wy = (SCREEN_HEIGHT - 140) / 2;
  videodisplay.setCursor(wx + 8, wy + 24);
}

void drawHelpWindow() {
  drawCenteredWindowBase("Help", 300, 140);
  int wx = (SCREEN_WIDTH - 300) / 2;
  int wy = (SCREEN_HEIGHT - 140) / 2;
  videodisplay.setCursor(wx + 8, wy + 24);
  videodisplay.print("Help: press Escape to close the window.");
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
    drawMenuBar();
    showSingleTopLeftCompose();
    if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
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
    if (viewMode == VM_ANSWER) drawPendingRectIfVisibleAndReset(contentOffsetAfterClear);
    else drawPendingRectIfVisibleAndReset(contentOffsetCurrent);
  }

  delay(1);
}

void setup() {
  pinMode(SPEAKER_PIN, 0);
  tone(SPEAKER_PIN, 1000);
  Serial.begin(115200);
  Serial1.begin(SERIAL1_BAUD, SERIAL_8N1, SERIAL1_RX_PIN, SERIAL1_TX_PIN);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed — continuing without FS");
  }
  if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        SPIFFS.format();
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS Mount Failed Again");
            return;
        }
    }
    SPI.begin(18,19,23,5);
    if(!SD.begin(5)){
      Serial.println("SD Failed to init");
    } else {
      Serial.println("EUREKA");
    }

    loadViewConfig();
    updateLayoutMetrics();

    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == "") {
        boolRes = "false";
        boolUpdate();
    }
    started = false;
    if (boolRes == "false") {
      SD.end();
      Serial1.println("res");
      videodisplay.init(CompMode::MODEPALColor288Pmid, 25, true);
      if (R_CHAR_W == 8) videodisplay.setFont(Font8x8); else videodisplay.setFont(Font6x8);
      videodisplay.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, videodisplay.RGB(255,255,255));
      contentOffsetCurrent = contentOffsetDefault;
      nextAnswerUsesClearOffset = false;
      ignoreList.push_back("rlsd");
      pageLineCount = 0;
      displayShort("Waiting for peer...");
      delay(1000);
      displayShort("");
      videodisplay.setCursor((SCREEN_WIDTH/2)-30, SCREEN_HEIGHT/2);
      videodisplay.setFont(Font8x8);
      videodisplay.println("CITADELA AI BROWSER");
      videodisplay.setFont(Font6x8);
      videodisplay.println("Type in your prompt to start");
      drawLambda((SCREEN_WIDTH/2)-100, (SCREEN_HEIGHT/2)+10, 2.5f, videodisplay.RGB(255,255,255), 0);
      menuMode = false;
      windowMode = false;
      drawMenuBar();
      Serial.println("Display unit ready.");
      getParam();
      getConfig();
      delay(1000);
      Serial.println("Type 'help' for commands. Peer messages will appear on screen.");
      tone(SPEAKER_PIN,0);
    } else if (boolRes == "trueKernel") {
        tone(SPEAKER_PIN, 0);
        boolRes = "false";
        boolUpdate();
        handleKernelFlash();
    } else if (boolRes.startsWith("task")){
      tone(SPEAKER_PIN, 0);
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

void handlePeerLine(const String &lineRaw) {
  String tmp = lineRaw;
  tmp.trim();
  String tmpLower = tmp;
  tmpLower.toLowerCase();

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
      if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
      showSingleTopLeftCompose();
    }
    return;
  }

  if (tmpLower.startsWith("leftctrl") && !tmp.endsWith("+")) {
    if (menuMode) {
      menuMode = false;
      Serial.println("Menu mode: EXIT (plain LeftCtrl)");
      drawMenuBar();
      if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
      showSingleTopLeftCompose();
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
    Serial1.println("res");
    ESP.restart();
  }
  if (tmp == "Escape") {
    Serial1.println("VDCLoadNSS");
    boolRes = "trueKernel";
    boolUpdate();
    ESP.restart();
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
    Serial.println("CBHFA received: start collecting answer lines (buffering).");
    return;
  }
  if (tmp == "CBHFAR" || tmp == "CBHFR") {
    collectingAnswer = false;
    printingAnswers = true;
    Serial.println("CBHFAR received: accepting buffered answer and displaying it at answer area.");
    if (answerBuffer.length()) {
      clearTextAreaAtOffset(contentOffsetAfterClear, videodisplay.RGB(0,0,0));
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
    answerBuffer = "";
    Serial.println("CBHFT received: stop printing answers; resume top-left single-line mode.");
    return;
  }

  if (collectingAnswer) {
    String line = sanitizeLine(tmp);
    if (line.length()) {
      answerBuffer += line;
      answerBuffer += '\n';
      Serial.println(String("CBHFA-buffered: ") + line);
    }
    return;
  }

  if (printingAnswers) {
    String line = sanitizeLine(tmp);
    if (line.length() == 0) return;
    Serial.println(String("<- peer (answer area): ") + line);
    clearTextAreaAtOffset(contentOffsetAfterClear, videodisplay.RGB(0,0,0));
    int s, e;
    appendPlainTextRange(line, s, e);
    if (pendingRectStart == -1) pendingRectStart = s;
    pendingRectEnd = e;
    int linesPerScreen = max(1, (SCREEN_HEIGHT - contentOffsetAfterClear - 10) / (R_CHAR_H + 2));
    if (pageLineCount > linesPerScreen) pageWindowStart = pageLineCount - linesPerScreen;
    else pageWindowStart = 0;
    displayPage(pageWindowStart, contentOffsetAfterClear);
    drawPendingRectIfVisibleAndReset(contentOffsetAfterClear);
    return;
  }

  if (tmp.startsWith("CBWR0")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiSSID = arg;
    Serial.println(String("CBWR0 -> WiFi SSID set to: ") + wifiSSID);
    if (windowMode && activeWindow == 0) drawConfigWindow();
    return;
  }
  if (tmp.startsWith("CBWR1")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiPass = arg;
    Serial.println(String("CBWR1 -> WiFi PASS set to: ") + wifiPass);
    if (windowMode && activeWindow == 0) drawConfigWindow();
    return;
  }
  if (tmp.startsWith("CBWR2")) {
    String arg = "";
    if (tmp.length() > 5) { arg = tmp.substring(5); arg.trim(); }
    wifiIP = arg;
    Serial.println(String("CBWR2 -> WiFi IP set to: ") + wifiIP);
    if (windowMode && activeWindow == 0) drawConfigWindow();
    return;
  }

  if (tmp.startsWith("CBHFRM")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfModel = arg;
    Serial.println(String("CBHFRM -> HF Model set to: ") + hfModel);
    if (windowMode && activeWindow == 1) drawParamWindow();
    return;
  }
  if (tmp.startsWith("CBHFRT")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfToken = arg;
    Serial.println(String("CBHFRT -> HF Token set to: ") + hfToken);
    if (windowMode && activeWindow == 1) drawParamWindow();
    return;
  }
  if (tmp.startsWith("CBHFRS")) {
    String arg = "";
    if (tmp.length() > 7) { arg = tmp.substring(7); arg.trim(); }
    hfSpace = arg;
    Serial.println(String("CBHFRS -> HF Space set to: ") + hfSpace);
    if (windowMode && activeWindow == 1) drawParamWindow();
    return;
  }

  {
    String line = sanitizeLine(tmp);
    if (line.length() == 0) return;
    Serial.println(String("<- peer (compose-mode): ") + line);
    appendToComposeBufferFromLine(line);
    showSingleTopLeftCompose();
    if (pageLineCount > 0) displayPage(pageWindowStart, contentOffsetAfterClear);
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
