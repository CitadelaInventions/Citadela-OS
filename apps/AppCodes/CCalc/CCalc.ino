#include <Wire.h>
#include <ESP32Video.h>
#include <Ressources/Font6x8.h>
#include <Ressources/CodePage437_9x16.h>
#include <Update.h>
#include "esp_partition.h"
#include "SD.h"
#include "SPI.h"
#include "SPIFFS.h"
#include "FS.h"

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

String boolRes = "";
String appName = "";
String appString = "";

#define SPEAKER_PIN 12
#define CALC_SCREEN_WIDTH 376
#define CALC_SCREEN_HEIGHT 192
#define CALC_KEY_COLUMNS 5
#define CALC_KEY_ROWS 5
#define CALC_KEY_COUNT (CALC_KEY_COLUMNS * CALC_KEY_ROWS)
#define CALC_KEY_X 8
#define CALC_KEY_Y 78
#define CALC_KEY_W 68
#define CALC_KEY_H 18
#define CALC_KEY_GAP_X 4
#define CALC_KEY_GAP_Y 3

struct CalculatorKey {
    const char *label;
    const char *action;
    uint8_t style;
};

static const CalculatorKey calculatorKeys[CALC_KEY_COUNT] = {
    {"C", "CLEAR", 2}, {"(", "(", 1}, {")", ")", 1}, {"BKSP", "BACK", 2}, {"/", "/", 1},
    {"7", "7", 0}, {"8", "8", 0}, {"9", "9", 0}, {"SQRT", "SQRT", 3}, {"*", "*", 1},
    {"4", "4", 0}, {"5", "5", 0}, {"6", "6", 0}, {"%", "%", 3}, {"-", "-", 1},
    {"1", "1", 0}, {"2", "2", 0}, {"3", "3", 0}, {"+/-", "SIGN", 3}, {"+", "+", 1},
    {"0", "0", 0}, {".", ".", 0}, {"ANS", "ANS", 3}, {"^", "^", 1}, {"=", "EQUALS", 4}
};

static String expression = "";
static String calculatorStatus = "READY";
static double lastResult = 0.0;
static bool hasLastResult = false;
static bool resultJustEvaluated = false;
static int selectedCalculatorKey = 20;

class CalculatorParser {
  public:
    CalculatorParser(const char *source) : text(source ? source : ""), position(0), valid(true) {}

    bool evaluate(double &result) {
        result = parseExpression();
        skipSpaces();
        if (text[position] != '\0' || !isfinite(result)) valid = false;
        return valid;
    }

  private:
    const char *text;
    int position;
    bool valid;

    void skipSpaces() {
        while (text[position] == ' ') ++position;
    }

    bool consume(char value) {
        skipSpaces();
        if (text[position] != value) return false;
        ++position;
        return true;
    }

    bool consumeWord(const char *word) {
        skipSpaces();
        int start = position;
        for (int i = 0; word[i]; ++i) {
            if (tolower(text[position++]) != tolower(word[i])) {
                position = start;
                return false;
            }
        }
        return true;
    }

    double parseExpression() {
        double value = parseTerm();
        while (valid) {
            if (consume('+')) value += parseTerm();
            else if (consume('-')) value -= parseTerm();
            else break;
        }
        return value;
    }

    double parseTerm() {
        double value = parsePower();
        while (valid) {
            if (consume('*')) value *= parsePower();
            else if (consume('/')) {
                double divisor = parsePower();
                if (fabs(divisor) < 1e-12) valid = false;
                else value /= divisor;
            } else if (consume('%')) {
                double divisor = parsePower();
                if (fabs(divisor) < 1e-12) valid = false;
                else value = fmod(value, divisor);
            } else break;
        }
        return value;
    }

    double parsePower() {
        double value = parseUnary();
        if (consume('^')) value = pow(value, parsePower());
        return value;
    }

    double parseUnary() {
        if (consume('+')) return parseUnary();
        if (consume('-')) return -parseUnary();
        if (consumeWord("sqrt")) {
            if (!consume('(')) {
                valid = false;
                return 0;
            }
            double value = parseExpression();
            if (!consume(')') || value < 0) {
                valid = false;
                return 0;
            }
            return sqrt(value);
        }
        return parsePrimary();
    }

    double parsePrimary() {
        if (consume('(')) {
            double value = parseExpression();
            if (!consume(')')) valid = false;
            return value;
        }
        skipSpaces();
        char *end = nullptr;
        double value = strtod(text + position, &end);
        if (end == text + position) {
            valid = false;
            return 0;
        }
        position = (int)(end - text);
        return value;
    }
};

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
    SPIFFS.remove("/evil.txt");
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (file) {
        file.println(boolRes);
        file.close();
        Serial.printf("Bool updated to %s\n", boolRes.c_str());
    } else {
        Serial.println("Failed to update bool!");
    }
}

static String formatCalculatorNumber(double value) {
    if (!isfinite(value)) return "ERROR";
    char buffer[32];
    double magnitude = fabs(value);
    if ((magnitude >= 1000000000.0) || (magnitude > 0 && magnitude < 0.000001)) {
        snprintf(buffer, sizeof(buffer), "%.8g", value);
    } else {
        snprintf(buffer, sizeof(buffer), "%.8f", value);
        int end = strlen(buffer) - 1;
        while (end > 0 && buffer[end] == '0') buffer[end--] = '\0';
        if (end > 0 && buffer[end] == '.') buffer[end] = '\0';
    }
    return String(buffer);
}

static void drawCalculatorDisplay() {
    uint32_t displayFill = videodisplay.RGB(11, 18, 22);
    uint32_t edge = videodisplay.RGB(65, 214, 191);
    uint32_t text = videodisplay.RGB(239, 247, 246);
    uint32_t muted = videodisplay.RGB(137, 166, 172);
    videodisplay.fillRect(8, 25, 360, 45, displayFill);
    videodisplay.rect(8, 25, 360, 45, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(muted, displayFill);
    videodisplay.setCursor(15, 31);
    String visible = expression.length() > 56 ? expression.substring(expression.length() - 56) : expression;
    videodisplay.print(visible.length() ? visible.c_str() : "0");
    videodisplay.setTextColor(text, displayFill);
    String resultText = hasLastResult ? formatCalculatorNumber(lastResult) : calculatorStatus;
    int resultX = max(15, 360 - (int)resultText.length() * 6);
    videodisplay.setCursor(resultX, 50);
    videodisplay.print(resultText.c_str());
    videodisplay.setTextColor(muted, displayFill);
    videodisplay.setCursor(15, 60);
    videodisplay.print(calculatorStatus.c_str());
}

static void drawCalculatorKey(int index, bool selected) {
    if (index < 0 || index >= CALC_KEY_COUNT) return;
    int row = index / CALC_KEY_COLUMNS;
    int column = index % CALC_KEY_COLUMNS;
    int x = CALC_KEY_X + column * (CALC_KEY_W + CALC_KEY_GAP_X);
    int y = CALC_KEY_Y + row * (CALC_KEY_H + CALC_KEY_GAP_Y);
    uint32_t fills[] = {
        videodisplay.RGB(25, 35, 42), videodisplay.RGB(31, 66, 73),
        videodisplay.RGB(83, 42, 47), videodisplay.RGB(50, 48, 76),
        videodisplay.RGB(28, 119, 101)
    };
    uint32_t fill = fills[calculatorKeys[index].style];
    uint32_t border = selected ? videodisplay.RGB(255, 255, 255)
                               : videodisplay.RGB(73, 96, 103);
    videodisplay.fillRect(x, y, CALC_KEY_W, CALC_KEY_H, fill);
    videodisplay.rect(x, y, CALC_KEY_W, CALC_KEY_H, border);
    videodisplay.setTextColor(videodisplay.RGB(245, 248, 248), fill);
    int labelWidth = strlen(calculatorKeys[index].label) * 6;
    videodisplay.setCursor(x + (CALC_KEY_W - labelWidth) / 2, y + 5);
    videodisplay.print(calculatorKeys[index].label);
}

void drawCalculatorUI() {
    AppCursorDrawGuard cursorGuard;
    uint32_t background = videodisplay.RGB(7, 11, 15);
    uint32_t header = videodisplay.RGB(16, 26, 32);
    uint32_t accent = videodisplay.RGB(65, 214, 191);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, CALC_SCREEN_WIDTH, 20, header);
    videodisplay.fillRect(0, 19, CALC_SCREEN_WIDTH, 1, accent);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(videodisplay.RGB(245, 248, 248), header);
    videodisplay.setCursor(8, 6);
    videodisplay.print("CCALC");
    videodisplay.setTextColor(videodisplay.RGB(150, 177, 183), header);
    videodisplay.setCursor(52, 6);
    videodisplay.print("STANDARD / PRECEDENCE MODE");
    videodisplay.setCursor(318, 6);
    videodisplay.print("ESC HOME");
    drawCalculatorDisplay();
    for (int index = 0; index < CALC_KEY_COUNT; ++index) {
        drawCalculatorKey(index, index == selectedCalculatorKey);
    }
    videodisplay.show();
}

static void evaluateCalculatorExpression() {
    if (expression.length() == 0) return;
    CalculatorParser parser(expression.c_str());
    double result = 0;
    if (!parser.evaluate(result)) {
        calculatorStatus = "INVALID EXPRESSION";
        resultJustEvaluated = false;
    } else {
        lastResult = result;
        hasLastResult = true;
        calculatorStatus = "RESULT";
        resultJustEvaluated = true;
    }
    drawCalculatorDisplay();
}

static void appendCalculatorToken(const String &token) {
    if (expression.length() + token.length() > 96) {
        calculatorStatus = "EXPRESSION LIMIT";
        drawCalculatorDisplay();
        return;
    }
    bool numericStart = token.length() == 1 && (isdigit(token[0]) || token[0] == '.');
    if (resultJustEvaluated && numericStart) expression = "";
    resultJustEvaluated = false;
    expression += token;
    calculatorStatus = "EDITING";
    drawCalculatorDisplay();
}

static void activateCalculatorAction(const char *action) {
    if (!action) return;
    if (strcmp(action, "CLEAR") == 0) {
        expression = "";
        hasLastResult = false;
        resultJustEvaluated = false;
        calculatorStatus = "READY";
    } else if (strcmp(action, "BACK") == 0) {
        if (expression.length()) expression.remove(expression.length() - 1);
        resultJustEvaluated = false;
        calculatorStatus = expression.length() ? "EDITING" : "READY";
    } else if (strcmp(action, "EQUALS") == 0) {
        evaluateCalculatorExpression();
        return;
    } else if (strcmp(action, "SQRT") == 0) {
        appendCalculatorToken("sqrt(");
        return;
    } else if (strcmp(action, "SIGN") == 0) {
        if (expression.startsWith("-(") && expression.endsWith(")")) {
            expression = expression.substring(2, expression.length() - 1);
        } else if (expression.length()) {
            expression = "-(" + expression + ")";
        } else {
            expression = "-";
        }
        resultJustEvaluated = false;
        calculatorStatus = "EDITING";
    } else if (strcmp(action, "ANS") == 0) {
        if (hasLastResult) appendCalculatorToken(formatCalculatorNumber(lastResult));
        return;
    } else {
        appendCalculatorToken(String(action));
        return;
    }
    drawCalculatorDisplay();
}

static int calculatorMouseKeyAt(int x, int y) {
    if (x < CALC_KEY_X || y < CALC_KEY_Y) return -1;
    int column = (x - CALC_KEY_X) / (CALC_KEY_W + CALC_KEY_GAP_X);
    int row = (y - CALC_KEY_Y) / (CALC_KEY_H + CALC_KEY_GAP_Y);
    if (column < 0 || column >= CALC_KEY_COLUMNS || row < 0 || row >= CALC_KEY_ROWS) return -1;
    int keyX = CALC_KEY_X + column * (CALC_KEY_W + CALC_KEY_GAP_X);
    int keyY = CALC_KEY_Y + row * (CALC_KEY_H + CALC_KEY_GAP_Y);
    if (x >= keyX + CALC_KEY_W || y >= keyY + CALC_KEY_H) return -1;
    return row * CALC_KEY_COLUMNS + column;
}

static bool handleCalculatorMouse(String input) {
    AppMouseReport mouse;
    if (!appMouseRead(input, mouse)) return false;
    appCursorRestore();
    appCursorSetPosition(mouse.x, mouse.y, mouse.buttons);
    if (mouse.leftPressed) {
        int key = calculatorMouseKeyAt(mouse.x, mouse.y);
        if (key >= 0) {
            int previous = selectedCalculatorKey;
            selectedCalculatorKey = key;
            drawCalculatorKey(previous, false);
            drawCalculatorKey(selectedCalculatorKey, true);
            activateCalculatorAction(calculatorKeys[key].action);
        }
    }
    appCursorCaptureAndDraw();
    return true;
}

static void moveCalculatorSelection(int dx, int dy) {
    int previous = selectedCalculatorKey;
    int row = previous / CALC_KEY_COLUMNS;
    int column = previous % CALC_KEY_COLUMNS;
    row = (row + dy + CALC_KEY_ROWS) % CALC_KEY_ROWS;
    column = (column + dx + CALC_KEY_COLUMNS) % CALC_KEY_COLUMNS;
    selectedCalculatorKey = row * CALC_KEY_COLUMNS + column;
    drawCalculatorKey(previous, false);
    drawCalculatorKey(selectedCalculatorKey, true);
}

static void processSerialCommand(String command) {
    command.trim();
    if (command.length() == 0 || command == "rlsd") return;
    if (command == "STATUS") {
        Serial.print("CCALC expression=");
        Serial.print(expression);
        Serial.print(" result=");
        Serial.print(hasLastResult ? formatCalculatorNumber(lastResult) : "none");
        Serial.print(" state=");
        Serial.println(calculatorStatus);
        return;
    }
    if (handleCalculatorMouse(command)) return;
    AppCursorDrawGuard cursorGuard;
    if (command == "Escape") {
        boolRes = "trueKernel";
        boolUpdate();
        restartWithFallbackVideo("Returning home");
    } else if (command == "Enter") {
        activateCalculatorAction(calculatorKeys[selectedCalculatorKey].action);
    } else if (command == "LeftArrow") {
        moveCalculatorSelection(-1, 0);
    } else if (command == "RightArrow") {
        moveCalculatorSelection(1, 0);
    } else if (command == "UpArrow") {
        moveCalculatorSelection(0, -1);
    } else if (command == "DownArrow") {
        moveCalculatorSelection(0, 1);
    } else if (command == "Backspace") {
        activateCalculatorAction("BACK");
    } else if (command == "Delete") {
        activateCalculatorAction("CLEAR");
    } else if (command == "LeftShift + 9") {
        appendCalculatorToken("(");
    } else if (command == "LeftShift + 10") {
        appendCalculatorToken(")");
    } else if (command == "LeftShift + 8") {
        appendCalculatorToken("*");
    } else if (command == "LeftShift + 5") {
        appendCalculatorToken("%");
    } else if (command == "LeftShift + =") {
        appendCalculatorToken("+");
    } else if (command.length() == 1) {
        char c = command[0];
        if (c == 'x' || c == 'X') c = '*';
        if (isdigit(c) || c == '.' || c == '+' || c == '-' || c == '*' ||
            c == '/' || c == '%' || c == '^' || c == '(' || c == ')') {
            appendCalculatorToken(String(c));
        } else if (c == '=') {
            evaluateCalculatorExpression();
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
        Serial.printf("Not enough space for the kernel! Available: %d bytes, Needed: %d bytes\n", ESP.getFreeSketchSpace(), kernelSize1);
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
    if (Update.end(true)) {
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

void setup() {
    pinMode(SPEAKER_PIN, 0);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    Serial.setTimeout(40);
    Serial1.setTimeout(40);
    Serial1.println("res");
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
    boolTru();
    boolRes.trim();
    Serial.println("boolRes: " + boolRes);
    if (boolRes == ""){
        boolRes = "false";
        boolUpdate();
    }
    if (boolRes == "false"){
      SD.end();
      bool videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, 25, true);
      Serial.printf("CCalc video init: %s\n", videoReady ? "ready" : "failed");
      if (!videoReady) restartWithFallbackVideo("CCalc video failed");
      releaseSerialControllerVideo();
      tone(SPEAKER_PIN, 0);
      videodisplay.setFont(Font6x8);
      videodisplay.clear();
      drawCalculatorUI();
    } else if (boolRes == "trueKernel") {
      tone(SPEAKER_PIN, 0);
      boolRes = "false";
      boolUpdate();
      handleKernelFlash();
    }
}

static void pollCalculatorPort(HardwareSerial &port, String &buffer) {
    while (port.available()) {
        char c = port.read();
        if (c == '\r') continue;
        if (c == '\n') {
            processSerialCommand(buffer);
            buffer = "";
        } else if (buffer.length() < 96) {
            buffer += c;
        } else {
            buffer = "";
        }
    }
}

void loop() {
    static String controllerCommand = "";
    static String usbCommand = "";
    pollCalculatorPort(Serial1, controllerCommand);
    pollCalculatorPort(Serial, usbCommand);
    delay(2);
}
