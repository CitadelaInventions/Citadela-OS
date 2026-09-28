#include <Arduino.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include <math.h>
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaMouseCursor.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"

static const int SCREEN_W = 376;
static const int SCREEN_H = 192;
static const int VIDEO_PIN = 25;
static const int SD_CS = 5;
static const int SPEAKER_PIN = 12;

static const int CANVAS_X = 5;
static const int CANVAS_Y = 24;
static const int CANVAS_W = 268;
static const int CANVAS_H = 163;
static const int CANVAS_PIXELS = CANVAS_W * CANVAS_H;

static const int PANEL_X = 278;
static const int TOOL_BUTTON_W = 42;
static const int TOOL_BUTTON_H = 15;
static const int COLOR_X = 284;
static const int COLOR_Y = 80;
static const int COLOR_W = 86;
static const int COLOR_H = 42;
static const int HUE_X = 284;
static const int HUE_Y = 127;
static const int HUE_W = 86;
static const int HUE_H = 7;

static const int MAX_UNDO_PIXELS = 4096;
static const int MAX_PREVIEW_PIXELS = 1400;
static const int FILL_STACK_SIZE = 1536;
static const uint32_t KEY_STEP_MS = 22;

enum DrawTool : uint8_t {
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECTANGLE,
    TOOL_CIRCLE,
    TOOL_FILL,
    TOOL_COUNT
};

enum PointerDrag : uint8_t {
    DRAG_NONE,
    DRAG_CANVAS,
    DRAG_COLOR,
    DRAG_HUE
};

static const char *TOOL_LABELS[TOOL_COUNT] = {
    "Brush", "Erase", "Line", "Rect", "Circle", "Fill"
};

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::MouseCursor<Citadela::CitCompositeColorDAC, 81> appCursor;
static Citadela::LineReader controllerReader(192);
static Citadela::LineReader usbReader(192);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);

typedef Citadela::CitCompositeColorDAC::RawPixel RawPixel;

static bool videoReady = false;
static bool sdReady = false;
static bool cursorOutlined = false;
static DrawTool activeTool = TOOL_BRUSH;
static PointerDrag pointerDrag = DRAG_NONE;
static uint8_t brushSize = 3;

static uint16_t selectedHue = 172;
static uint8_t selectedSaturation = 210;
static uint8_t selectedValue = 220;
static uint8_t selectedRed = 38;
static uint8_t selectedGreen = 211;
static uint8_t selectedBlue = 190;

static uint16_t undoPositions[MAX_UNDO_PIXELS];
static RawPixel undoPixels[MAX_UNDO_PIXELS];
static uint8_t undoSeen[(CANVAS_PIXELS + 7) / 8];
static uint16_t undoCount = 0;
static bool undoAvailable = false;
static bool undoActionOpen = false;
static bool undoActionChanged = false;
static bool undoOverflow = false;

static uint16_t previewPositions[MAX_PREVIEW_PIXELS];
static RawPixel previewPixels[MAX_PREVIEW_PIXELS];
static uint8_t previewSeen[(CANVAS_PIXELS + 7) / 8];
static uint16_t previewCount = 0;

static uint16_t fillStack[FILL_STACK_SIZE];
static uint16_t fillStackCount = 0;
static bool fillStackOverflow = false;

static bool mouseWasDown = false;
static bool gestureActive = false;
static int gestureStartX = 0;
static int gestureStartY = 0;
static int gestureLastX = 0;
static int gestureLastY = 0;

static bool keyUp = false;
static bool keyDown = false;
static bool keyLeft = false;
static bool keyRight = false;
static bool keyboardDrawing = false;
static uint32_t lastKeyStepMs = 0;

static char statusText[28] = "Ready";

static void drawHeader();
static void drawToolButton(DrawTool tool);
static void drawColorSquare();
static void drawHueStrip();
static void drawSelectedColor();
static void drawActionButtons();
static void saveDrawingBMP();
static void clearCanvas();
static void undoLastAction();
static void onMouseHover(int x, int y, bool leftDown);

static inline uint32_t rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return videodisplay.RGB(red, green, blue);
}

static inline bool pointInRect(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

static inline bool insideCanvasScreen(int x, int y) {
    return pointInRect(x, y, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);
}

static inline uint16_t canvasPosition(int x, int y) {
    return (uint16_t)(y * CANVAS_W + x);
}

static inline int canvasPositionX(uint16_t position) {
    return position % CANVAS_W;
}

static inline int canvasPositionY(uint16_t position) {
    return position / CANVAS_W;
}

static inline bool bitIsSet(const uint8_t *bits, uint16_t position) {
    return (bits[position >> 3] & (1U << (position & 7))) != 0;
}

static inline void setBit(uint8_t *bits, uint16_t position) {
    bits[position >> 3] |= (uint8_t)(1U << (position & 7));
}

static void setStatus(const char *message) {
    snprintf(statusText, sizeof(statusText), "%s", message ? message : "");
}

static void hsvToRgb(uint16_t hue, uint8_t saturation, uint8_t value,
                     uint8_t &red, uint8_t &green, uint8_t &blue) {
    hue %= 360;
    if (saturation == 0) {
        red = green = blue = value;
        return;
    }

    uint8_t region = hue / 60;
    uint16_t remainder = (uint16_t)(hue - region * 60) * 255 / 60;
    uint8_t p = (uint8_t)((uint16_t)value * (255 - saturation) / 255);
    uint8_t q = (uint8_t)((uint16_t)value *
                          (255 - ((uint16_t)saturation * remainder / 255)) / 255);
    uint8_t t = (uint8_t)((uint16_t)value *
                          (255 - ((uint16_t)saturation * (255 - remainder) / 255)) / 255);

    switch (region) {
        case 0: red = value; green = t; blue = p; break;
        case 1: red = q; green = value; blue = p; break;
        case 2: red = p; green = value; blue = t; break;
        case 3: red = p; green = q; blue = value; break;
        case 4: red = t; green = p; blue = value; break;
        default: red = value; green = p; blue = q; break;
    }
}

static void rgbToHsv(uint8_t red, uint8_t green, uint8_t blue,
                     uint16_t &hue, uint8_t &saturation, uint8_t &value) {
    uint8_t maximum = max(red, max(green, blue));
    uint8_t minimum = min(red, min(green, blue));
    uint8_t delta = maximum - minimum;
    value = maximum;
    saturation = maximum == 0 ? 0 : (uint8_t)((uint16_t)delta * 255 / maximum);
    if (delta == 0) return;

    int calculatedHue;
    if (maximum == red) {
        calculatedHue = 60 * ((int)green - blue) / delta;
    } else if (maximum == green) {
        calculatedHue = 120 + 60 * ((int)blue - red) / delta;
    } else {
        calculatedHue = 240 + 60 * ((int)red - green) / delta;
    }
    if (calculatedHue < 0) calculatedHue += 360;
    hue = (uint16_t)calculatedHue;
}

static void updateSelectedRgb() {
    hsvToRgb(selectedHue, selectedSaturation, selectedValue,
             selectedRed, selectedGreen, selectedBlue);
}

static uint32_t selectedColor() {
    return rgb(selectedRed, selectedGreen, selectedBlue);
}

static uint32_t pickerColorAt(int localX, int localY) {
    localX = constrain(localX, 0, COLOR_W - 1);
    localY = constrain(localY, 0, COLOR_H - 1);
    uint8_t saturation = (uint8_t)((uint32_t)localX * 255 / (COLOR_W - 1));
    uint8_t value = (uint8_t)(255 - (uint32_t)localY * 255 / (COLOR_H - 1));
    uint8_t red, green, blue;
    hsvToRgb(selectedHue, saturation, value, red, green, blue);
    return rgb(red, green, blue);
}

static int pickerMarkerX() {
    return COLOR_X + 2 + (int)((uint32_t)selectedSaturation * (COLOR_W - 5) / 255);
}

static int pickerMarkerY() {
    return COLOR_Y + 2 + (int)((uint32_t)(255 - selectedValue) * (COLOR_H - 5) / 255);
}

static void redrawPickerArea(int centerX, int centerY) {
    for (int y = centerY - 3; y <= centerY + 3; ++y) {
        for (int x = centerX - 3; x <= centerX + 3; ++x) {
            if (!pointInRect(x, y, COLOR_X, COLOR_Y, COLOR_W, COLOR_H)) continue;
            videodisplay.dotFast(x, y, pickerColorAt(x - COLOR_X, y - COLOR_Y));
        }
    }
}

static void drawPickerMarker() {
    int x = pickerMarkerX();
    int y = pickerMarkerY();
    uint32_t edge = selectedValue > 145 ? rgb(0, 0, 0) : rgb(255, 255, 255);
    videodisplay.rect(x - 2, y - 2, 5, 5, edge);
    videodisplay.dotFast(x, y, selectedValue > 145 ? rgb(255, 255, 255) : rgb(0, 0, 0));
}

static void drawColorSquare() {
    for (int y = 0; y < COLOR_H; ++y) {
        for (int x = 0; x < COLOR_W; ++x) {
            videodisplay.dotFast(COLOR_X + x, COLOR_Y + y, pickerColorAt(x, y));
        }
    }
    videodisplay.rect(COLOR_X - 1, COLOR_Y - 1, COLOR_W + 2, COLOR_H + 2,
                      rgb(105, 125, 128));
    drawPickerMarker();
}

static void drawHueStrip() {
    for (int x = 0; x < HUE_W; ++x) {
        uint16_t hue = (uint16_t)((uint32_t)x * 359 / (HUE_W - 1));
        uint8_t red, green, blue;
        hsvToRgb(hue, 255, 255, red, green, blue);
        videodisplay.fillRect(HUE_X + x, HUE_Y, 1, HUE_H, rgb(red, green, blue));
    }
    videodisplay.rect(HUE_X - 1, HUE_Y - 1, HUE_W + 2, HUE_H + 2,
                      rgb(105, 125, 128));
    int marker = HUE_X + (int)((uint32_t)selectedHue * (HUE_W - 1) / 359);
    videodisplay.fillRect(marker - 1, HUE_Y - 2, 3, HUE_H + 4, rgb(255, 255, 255));
    videodisplay.fillRect(marker, HUE_Y - 1, 1, HUE_H + 2, rgb(0, 0, 0));
}

static void drawHeader() {
    uint32_t header = rgb(18, 39, 47);
    videodisplay.fillRect(0, 0, SCREEN_W, 20, header);
    videodisplay.fillRect(0, 19, SCREEN_W, 1, rgb(43, 211, 189));
    videodisplay.fillRect(5, 5, 3, 10, rgb(238, 75, 82));
    videodisplay.fillRect(9, 5, 3, 10, rgb(250, 198, 61));
    videodisplay.fillRect(13, 5, 3, 10, rgb(43, 211, 189));

    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(255, 255, 255), header);
    videodisplay.setCursor(21, 6);
    videodisplay.print("CDraw");

    char toolText[18];
    snprintf(toolText, sizeof(toolText), "%s %upx", TOOL_LABELS[activeTool], brushSize);
    videodisplay.setTextColor(rgb(184, 207, 211), header);
    videodisplay.setCursor(58, 6);
    videodisplay.print(toolText);

    char hexText[9];
    snprintf(hexText, sizeof(hexText), "#%02X%02X%02X",
             selectedRed, selectedGreen, selectedBlue);
    videodisplay.setTextColor(rgb(245, 249, 249), header);
    videodisplay.setCursor(130, 6);
    videodisplay.print(hexText);

    videodisplay.setTextColor(rgb(145, 184, 190), header);
    videodisplay.setCursor(184, 6);
    videodisplay.print(statusText);
    videodisplay.fillRect(356, 5, 14, 10, selectedColor());
    videodisplay.rect(355, 4, 16, 12, rgb(230, 240, 240));
}

static void toolButtonGeometry(DrawTool tool, int &x, int &y) {
    int index = (int)tool;
    x = PANEL_X + 5 + (index & 1) * 45;
    y = 24 + (index >> 1) * 18;
}

static void drawToolButton(DrawTool tool) {
    int x, y;
    toolButtonGeometry(tool, x, y);
    bool selected = tool == activeTool;
    uint32_t fill = selected ? rgb(27, 91, 87) : rgb(27, 34, 37);
    uint32_t edge = selected ? rgb(43, 211, 189) : rgb(76, 91, 94);
    videodisplay.fillRect(x, y, TOOL_BUTTON_W, TOOL_BUTTON_H, fill);
    videodisplay.rect(x, y, TOOL_BUTTON_W, TOOL_BUTTON_H, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(selected ? rgb(255, 255, 255) : rgb(190, 202, 204), fill);
    int textWidth = (int)strlen(TOOL_LABELS[tool]) * 6;
    videodisplay.setCursor(x + max(2, (TOOL_BUTTON_W - textWidth) / 2), y + 4);
    videodisplay.print(TOOL_LABELS[tool]);
}

static void drawSmallButton(int x, int y, int width, const char *label, bool accent = false) {
    uint32_t fill = accent ? rgb(28, 91, 87) : rgb(27, 34, 37);
    uint32_t edge = accent ? rgb(43, 211, 189) : rgb(76, 91, 94);
    videodisplay.fillRect(x, y, width, 15, fill);
    videodisplay.rect(x, y, width, 15, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(242, 247, 247), fill);
    int textWidth = (int)strlen(label) * 6;
    videodisplay.setCursor(x + max(2, (width - textWidth) / 2), y + 4);
    videodisplay.print(label);
}

static void drawSelectedColor() {
    uint32_t panel = rgb(16, 21, 23);
    videodisplay.fillRect(PANEL_X + 3, 137, 94, 15, panel);
    videodisplay.fillRect(284, 138, 18, 13, selectedColor());
    videodisplay.rect(283, 137, 20, 15, rgb(220, 232, 232));

    char text[8];
    snprintf(text, sizeof(text), "%02X%02X%02X", selectedRed, selectedGreen, selectedBlue);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(224, 234, 235), panel);
    videodisplay.setCursor(306, 141);
    videodisplay.print(text);
    drawSmallButton(349, 137, 11, "-");
    drawSmallButton(362, 137, 11, "+");
}

static void drawActionButtons() {
    drawSmallButton(283, 155, 42, "Undo");
    drawSmallButton(328, 155, 42, "Clear");
    drawSmallButton(283, 173, 87, "Save", true);
}

static void drawApplication() {
    videodisplay.clear(rgb(10, 13, 15));
    drawHeader();

    videodisplay.fillRect(CANVAS_X - 2, CANVAS_Y - 2,
                          CANVAS_W + 4, CANVAS_H + 4, rgb(67, 88, 92));
    videodisplay.fillRect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, rgb(0, 0, 0));

    videodisplay.fillRect(PANEL_X, 20, SCREEN_W - PANEL_X, SCREEN_H - 20,
                          rgb(16, 21, 23));
    videodisplay.fillRect(PANEL_X, 20, 1, SCREEN_H - 20, rgb(43, 211, 189));
    for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool)
        drawToolButton((DrawTool)tool);
    drawColorSquare();
    drawHueStrip();
    drawSelectedColor();
    drawActionButtons();
    videodisplay.show();
}

static void selectTool(DrawTool tool) {
    if (tool >= TOOL_COUNT || tool == activeTool) return;
    DrawTool previous = activeTool;
    activeTool = tool;
    keyboardDrawing = false;
    drawToolButton(previous);
    drawToolButton(activeTool);
    setStatus(TOOL_LABELS[activeTool]);
    drawHeader();
}

static void adjustBrushSize(int delta) {
    int next = constrain((int)brushSize + delta, 1, 11);
    if ((next & 1) == 0) next += delta >= 0 ? 1 : -1;
    next = constrain(next, 1, 11);
    if (next == brushSize) return;
    brushSize = (uint8_t)next;
    setStatus("Brush resized");
    drawHeader();
}

static void chooseColorFromSquare(int screenX, int screenY) {
    int oldMarkerX = pickerMarkerX();
    int oldMarkerY = pickerMarkerY();
    selectedSaturation = (uint8_t)((uint32_t)constrain(screenX - COLOR_X, 0, COLOR_W - 1) *
                                   255 / (COLOR_W - 1));
    selectedValue = (uint8_t)(255 -
                            (uint32_t)constrain(screenY - COLOR_Y, 0, COLOR_H - 1) *
                            255 / (COLOR_H - 1));
    updateSelectedRgb();
    redrawPickerArea(oldMarkerX, oldMarkerY);
    drawPickerMarker();
    drawSelectedColor();
    setStatus("Color selected");
    drawHeader();
}

static void chooseHue(int screenX) {
    uint16_t nextHue = (uint16_t)((uint32_t)constrain(screenX - HUE_X, 0, HUE_W - 1) *
                                  359 / (HUE_W - 1));
    if (nextHue == selectedHue) return;
    selectedHue = nextHue;
    updateSelectedRgb();
    drawColorSquare();
    drawHueStrip();
    drawSelectedColor();
    setStatus("Hue selected");
    drawHeader();
}

static void beginUndoAction() {
    undoActionOpen = true;
    undoActionChanged = false;
    undoOverflow = false;
}

static void recordUndoPixel(int x, int y, RawPixel oldPixel) {
    if (!undoActionOpen) return;
    uint16_t position = canvasPosition(x, y);
    if (!undoActionChanged) {
        memset(undoSeen, 0, sizeof(undoSeen));
        undoCount = 0;
        undoAvailable = false;
        undoActionChanged = true;
    }
    if (bitIsSet(undoSeen, position)) return;
    if (undoCount >= MAX_UNDO_PIXELS) {
        undoOverflow = true;
        return;
    }
    setBit(undoSeen, position);
    undoPositions[undoCount] = position;
    undoPixels[undoCount] = oldPixel;
    undoCount++;
}

static void finishUndoAction() {
    if (!undoActionOpen) return;
    undoActionOpen = false;
    if (!undoActionChanged) return;
    if (undoOverflow) {
        undoCount = 0;
        undoAvailable = false;
    } else {
        undoAvailable = undoCount > 0;
    }
}

static inline RawPixel canvasRawPixel(int x, int y) {
    return videodisplay.getRawPixelFast(CANVAS_X + x, CANVAS_Y + y);
}

static inline uint8_t canvasSignal(int x, int y) {
    return videodisplay.rawPixelSignal(canvasRawPixel(x, y));
}

static void setCanvasPixel(int x, int y, uint32_t color) {
    if ((unsigned)x >= CANVAS_W || (unsigned)y >= CANVAS_H) return;
    RawPixel oldPixel = canvasRawPixel(x, y);
    uint8_t newSignal = videodisplay.indexedPixel(color);
    if (videodisplay.rawPixelSignal(oldPixel) == newSignal) return;
    recordUndoPixel(x, y, oldPixel);
    videodisplay.dotFast(CANVAS_X + x, CANVAS_Y + y, color);
}

static void stampCanvas(int x, int y, uint32_t color) {
    int radius = brushSize / 2;
    if (radius == 0) {
        setCanvasPixel(x, y, color);
        return;
    }
    int radiusSquared = radius * radius + radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radiusSquared)
                setCanvasPixel(x + dx, y + dy, color);
        }
    }
}

static void previewPixel(int x, int y, uint32_t color) {
    if ((unsigned)x >= CANVAS_W || (unsigned)y >= CANVAS_H) return;
    uint16_t position = canvasPosition(x, y);
    if (bitIsSet(previewSeen, position)) return;
    if (previewCount >= MAX_PREVIEW_PIXELS) return;
    setBit(previewSeen, position);
    previewPositions[previewCount] = position;
    previewPixels[previewCount] = canvasRawPixel(x, y);
    previewCount++;
    videodisplay.dotFast(CANVAS_X + x, CANVAS_Y + y, color);
}

static void clearShapePreview() {
    for (uint16_t i = 0; i < previewCount; ++i) {
        int x = canvasPositionX(previewPositions[i]);
        int y = canvasPositionY(previewPositions[i]);
        videodisplay.setRawPixelFast(CANVAS_X + x, CANVAS_Y + y, previewPixels[i]);
    }
    previewCount = 0;
    memset(previewSeen, 0, sizeof(previewSeen));
}

static void emitShapePoint(int x, int y, bool preview, uint32_t color) {
    if (preview) previewPixel(x, y, color);
    else stampCanvas(x, y, color);
}

static void drawLineShape(int x0, int y0, int x1, int y1,
                          bool preview, uint32_t color) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        emitShapePoint(x0, y0, preview, color);
        if (x0 == x1 && y0 == y1) break;
        int doubled = error * 2;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

static void drawRectangleShape(int x0, int y0, int x1, int y1,
                               bool preview, uint32_t color) {
    int left = min(x0, x1);
    int right = max(x0, x1);
    int top = min(y0, y1);
    int bottom = max(y0, y1);
    drawLineShape(left, top, right, top, preview, color);
    drawLineShape(right, top, right, bottom, preview, color);
    drawLineShape(right, bottom, left, bottom, preview, color);
    drawLineShape(left, bottom, left, top, preview, color);
}

static void drawCircleShape(int centerX, int centerY, int edgeX, int edgeY,
                            bool preview, uint32_t color) {
    int deltaX = edgeX - centerX;
    int deltaY = edgeY - centerY;
    int radius = (int)(sqrtf((float)(deltaX * deltaX + deltaY * deltaY)) + 0.5f);
    int x = radius;
    int y = 0;
    int error = 1 - radius;
    while (x >= y) {
        emitShapePoint(centerX + x, centerY + y, preview, color);
        emitShapePoint(centerX + y, centerY + x, preview, color);
        emitShapePoint(centerX - y, centerY + x, preview, color);
        emitShapePoint(centerX - x, centerY + y, preview, color);
        emitShapePoint(centerX - x, centerY - y, preview, color);
        emitShapePoint(centerX - y, centerY - x, preview, color);
        emitShapePoint(centerX + y, centerY - x, preview, color);
        emitShapePoint(centerX + x, centerY - y, preview, color);
        y++;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x) + 1;
        }
    }
}

static void drawActiveShape(int x0, int y0, int x1, int y1, bool preview) {
    uint32_t color = selectedColor();
    if (activeTool == TOOL_LINE)
        drawLineShape(x0, y0, x1, y1, preview, color);
    else if (activeTool == TOOL_RECTANGLE)
        drawRectangleShape(x0, y0, x1, y1, preview, color);
    else if (activeTool == TOOL_CIRCLE)
        drawCircleShape(x0, y0, x1, y1, preview, color);
}

static bool pushFillSeed(int x, int y) {
    if ((unsigned)x >= CANVAS_W || (unsigned)y >= CANVAS_H) return false;
    if (fillStackCount >= FILL_STACK_SIZE) {
        fillStackOverflow = true;
        return false;
    }
    fillStack[fillStackCount++] = canvasPosition(x, y);
    return true;
}

static void completeOverflowedFill(uint8_t targetSignal, uint8_t replacementSignal,
                                   uint32_t replacementColor) {
    bool changed;
    do {
        changed = false;
        for (int y = 0; y < CANVAS_H; ++y) {
            for (int x = 0; x < CANVAS_W; ++x) {
                if (canvasSignal(x, y) != targetSignal) continue;
                bool adjacent = (x > 0 && canvasSignal(x - 1, y) == replacementSignal) ||
                                (y > 0 && canvasSignal(x, y - 1) == replacementSignal) ||
                                (x + 1 < CANVAS_W && canvasSignal(x + 1, y) == replacementSignal) ||
                                (y + 1 < CANVAS_H && canvasSignal(x, y + 1) == replacementSignal);
                if (adjacent) {
                    setCanvasPixel(x, y, replacementColor);
                    changed = true;
                }
            }
        }
        for (int y = CANVAS_H - 1; y >= 0; --y) {
            for (int x = CANVAS_W - 1; x >= 0; --x) {
                if (canvasSignal(x, y) != targetSignal) continue;
                bool adjacent = (x > 0 && canvasSignal(x - 1, y) == replacementSignal) ||
                                (y > 0 && canvasSignal(x, y - 1) == replacementSignal) ||
                                (x + 1 < CANVAS_W && canvasSignal(x + 1, y) == replacementSignal) ||
                                (y + 1 < CANVAS_H && canvasSignal(x, y + 1) == replacementSignal);
                if (adjacent) {
                    setCanvasPixel(x, y, replacementColor);
                    changed = true;
                }
            }
        }
    } while (changed);
}

static void floodFillCanvas(int seedX, int seedY) {
    if ((unsigned)seedX >= CANVAS_W || (unsigned)seedY >= CANVAS_H) return;
    uint32_t replacementColor = selectedColor();
    uint8_t targetSignal = canvasSignal(seedX, seedY);
    uint8_t replacementSignal = videodisplay.indexedPixel(replacementColor);
    if (targetSignal == replacementSignal) {
        setStatus("Same fill color");
        drawHeader();
        return;
    }

    beginUndoAction();
    fillStackCount = 0;
    fillStackOverflow = false;
    pushFillSeed(seedX, seedY);

    while (fillStackCount) {
        uint16_t seed = fillStack[--fillStackCount];
        int y = canvasPositionY(seed);
        int x = canvasPositionX(seed);
        if (canvasSignal(x, y) != targetSignal) continue;

        int left = x;
        while (left > 0 && canvasSignal(left - 1, y) == targetSignal) left--;
        int right = left;
        while (right < CANVAS_W && canvasSignal(right, y) == targetSignal) right++;

        bool spanAbove = false;
        bool spanBelow = false;
        for (int scanX = left; scanX < right; ++scanX) {
            setCanvasPixel(scanX, y, replacementColor);
            if (y > 0) {
                bool target = canvasSignal(scanX, y - 1) == targetSignal;
                if (target && !spanAbove) pushFillSeed(scanX, y - 1);
                spanAbove = target;
            }
            if (y + 1 < CANVAS_H) {
                bool target = canvasSignal(scanX, y + 1) == targetSignal;
                if (target && !spanBelow) pushFillSeed(scanX, y + 1);
                spanBelow = target;
            }
        }
    }

    if (fillStackOverflow)
        completeOverflowedFill(targetSignal, replacementSignal, replacementColor);
    finishUndoAction();
    setStatus(undoOverflow ? "Filled; no undo" : "Area filled");
    drawHeader();
}

static void beginCanvasGesture(int x, int y) {
    gestureActive = true;
    gestureStartX = gestureLastX = constrain(x, 0, CANVAS_W - 1);
    gestureStartY = gestureLastY = constrain(y, 0, CANVAS_H - 1);
    beginUndoAction();

    if (activeTool == TOOL_BRUSH || activeTool == TOOL_ERASER) {
        uint32_t color = activeTool == TOOL_ERASER ? rgb(0, 0, 0) : selectedColor();
        stampCanvas(gestureLastX, gestureLastY, color);
    } else {
        drawActiveShape(gestureStartX, gestureStartY,
                        gestureLastX, gestureLastY, true);
    }
}

static void updateCanvasGesture(int x, int y) {
    if (!gestureActive) return;
    x = constrain(x, 0, CANVAS_W - 1);
    y = constrain(y, 0, CANVAS_H - 1);
    if (x == gestureLastX && y == gestureLastY) return;

    if (activeTool == TOOL_BRUSH || activeTool == TOOL_ERASER) {
        uint32_t color = activeTool == TOOL_ERASER ? rgb(0, 0, 0) : selectedColor();
        drawLineShape(gestureLastX, gestureLastY, x, y, false, color);
    } else {
        clearShapePreview();
        drawActiveShape(gestureStartX, gestureStartY, x, y, true);
    }
    gestureLastX = x;
    gestureLastY = y;
}

static void endCanvasGesture(int x, int y) {
    if (!gestureActive) return;
    updateCanvasGesture(x, y);
    if (activeTool == TOOL_LINE || activeTool == TOOL_RECTANGLE ||
        activeTool == TOOL_CIRCLE) {
        clearShapePreview();
        drawActiveShape(gestureStartX, gestureStartY,
                        gestureLastX, gestureLastY, false);
    }
    finishUndoAction();
    gestureActive = false;
    setStatus(undoOverflow ? "Drawn; no undo" : "Drawn");
    drawHeader();
}

static void undoLastAction() {
    clearShapePreview();
    if (!undoAvailable || undoCount == 0) {
        setStatus("Nothing to undo");
        drawHeader();
        return;
    }
    for (uint16_t i = 0; i < undoCount; ++i) {
        int x = canvasPositionX(undoPositions[i]);
        int y = canvasPositionY(undoPositions[i]);
        videodisplay.setRawPixelFast(CANVAS_X + x, CANVAS_Y + y, undoPixels[i]);
    }
    undoAvailable = false;
    undoCount = 0;
    setStatus("Undo complete");
    drawHeader();
}

static void clearCanvas() {
    clearShapePreview();
    beginUndoAction();
    uint8_t blackSignal = videodisplay.indexedPixel(rgb(0, 0, 0));
    for (int y = 0; y < CANVAS_H; ++y) {
        for (int x = 0; x < CANVAS_W; ++x) {
            RawPixel oldPixel = canvasRawPixel(x, y);
            if (videodisplay.rawPixelSignal(oldPixel) != blackSignal)
                recordUndoPixel(x, y, oldPixel);
        }
    }
    videodisplay.fillRect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, rgb(0, 0, 0));
    finishUndoAction();
    setStatus("Canvas cleared");
    drawHeader();
}

static int toolAt(int x, int y) {
    for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
        int bx, by;
        toolButtonGeometry((DrawTool)tool, bx, by);
        if (pointInRect(x, y, bx, by, TOOL_BUTTON_W, TOOL_BUTTON_H)) return tool;
    }
    return -1;
}

static void handleUiClick(int x, int y) {
    int tool = toolAt(x, y);
    if (tool >= 0) {
        selectTool((DrawTool)tool);
        return;
    }
    if (pointInRect(x, y, 349, 137, 11, 15)) {
        adjustBrushSize(-2);
    } else if (pointInRect(x, y, 362, 137, 11, 15)) {
        adjustBrushSize(2);
    } else if (pointInRect(x, y, 283, 155, 42, 15)) {
        undoLastAction();
    } else if (pointInRect(x, y, 328, 155, 42, 15)) {
        clearCanvas();
    } else if (pointInRect(x, y, 283, 173, 87, 15)) {
        saveDrawingBMP();
    }
}

static void onMouseHover(int x, int y, bool leftDown) {
    bool pressed = leftDown && !mouseWasDown;
    bool released = !leftDown && mouseWasDown;
    if (!pressed && !released && !leftDown) {
        mouseWasDown = false;
        return;
    }

    appCursor.Restore();
    if (pressed) {
        if (insideCanvasScreen(x, y)) {
            int localX = x - CANVAS_X;
            int localY = y - CANVAS_Y;
            if (activeTool == TOOL_FILL) {
                floodFillCanvas(localX, localY);
            } else {
                pointerDrag = DRAG_CANVAS;
                beginCanvasGesture(localX, localY);
            }
        } else if (pointInRect(x, y, COLOR_X, COLOR_Y, COLOR_W, COLOR_H)) {
            pointerDrag = DRAG_COLOR;
            chooseColorFromSquare(x, y);
        } else if (pointInRect(x, y, HUE_X, HUE_Y - 2, HUE_W, HUE_H + 4)) {
            pointerDrag = DRAG_HUE;
            chooseHue(x);
        } else {
            handleUiClick(x, y);
        }
    } else if (leftDown) {
        if (pointerDrag == DRAG_CANVAS) {
            updateCanvasGesture(x - CANVAS_X, y - CANVAS_Y);
        } else if (pointerDrag == DRAG_COLOR) {
            chooseColorFromSquare(x, y);
        } else if (pointerDrag == DRAG_HUE) {
            chooseHue(x);
        }
    }

    if (released) {
        if (pointerDrag == DRAG_CANVAS)
            endCanvasGesture(x - CANVAS_X, y - CANVAS_Y);
        pointerDrag = DRAG_NONE;
    }
    mouseWasDown = leftDown;
    appCursor.Redraw();
}

static void loadCursorConfig() {
    File file = SPIFFS.open("/systemConfiguration.conf", FILE_READ);
    if (!file) return;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("MouseCursorOutlined=") ||
            line.startsWith("CursorOutlineMode=")) {
            int equals = line.indexOf('=');
            cursorOutlined = equals >= 0 && line.substring(equals + 1).toInt() != 0;
        }
    }
    file.close();
}

static String readBootState() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    if (!file) return "false";
    String state = file.readString();
    file.close();
    state.trim();
    return state.length() ? state : "false";
}

static bool writeBootState(const char *state) {
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (!file) return false;
    bool ok = file.println(state ? state : "false") > 0;
    file.close();
    return ok;
}

static bool mountSD() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(18, 19, 23, SD_CS);
    const uint32_t frequencies[] = {4000000, 2000000, 1000000};
    for (uint8_t attempt = 0; attempt < 6; ++attempt) {
        if (SD.begin(SD_CS, SPI, frequencies[attempt % 3])) return true;
        SD.end();
        delay(30);
    }
    return false;
}

static void restartWithFallbackVideo(const char *label) {
    appCursor.Restore();
    controllerVideo.prepare(label ? label : "Restarting", 0);
    delay(160);
    if (videoReady) videodisplay.releaseVideoMemory();
    videoReady = false;
    pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(60);
    ESP.restart();
}

static void flashKernel() {
    File kernel = SD.open("/System/kernel.bin", FILE_READ);
    if (!kernel) {
        controllerVideo.forceProgress(100, "Kernel file missing");
        return;
    }
    size_t total = kernel.size();
    controllerVideo.appFlashStart(total, "Flashing kernel");
    if (total == 0 || !Update.begin(total)) {
        kernel.close();
        controllerVideo.forceProgress(100, "Kernel flash failed");
        return;
    }

    uint8_t buffer[2048];
    size_t writtenTotal = 0;
    while (kernel.available()) {
        int count = kernel.read(buffer, sizeof(buffer));
        int written = count > 0 ? Update.write(buffer, count) : 0;
        if (written != count) {
            Update.abort();
            kernel.close();
            controllerVideo.forceProgress(100, "Kernel flash failed");
            return;
        }
        writtenTotal += written;
        controllerVideo.appFlashWrite(writtenTotal);
    }
    kernel.close();
    if (!Update.end(true)) {
        controllerVideo.forceProgress(100, "Kernel flash failed");
        return;
    }
    controllerVideo.appFlashWrite(total);
    Serial1.println("VDSTOP");
    Serial1.flush();
    delay(25);
    ESP.restart();
}

static void writeLE16(File &file, uint16_t value) {
    file.write((uint8_t)(value & 0xff));
    file.write((uint8_t)((value >> 8) & 0xff));
}

static void writeLE32(File &file, uint32_t value) {
    file.write((uint8_t)(value & 0xff));
    file.write((uint8_t)((value >> 8) & 0xff));
    file.write((uint8_t)((value >> 16) & 0xff));
    file.write((uint8_t)((value >> 24) & 0xff));
}

static String nextDrawingFilename() {
    SD.mkdir("/Images");
    char path[32];
    for (int index = 0; index < 10000; ++index) {
        snprintf(path, sizeof(path), "/Images/CDraw%04d.bmp", index);
        if (!SD.exists(path)) return String(path);
    }
    return String("/Images/CDraw9999.bmp");
}

static void saveDrawingBMP() {
    setStatus("Saving...");
    drawHeader();
    if (!sdReady) sdReady = mountSD();
    if (!sdReady) {
        setStatus("SD unavailable");
        drawHeader();
        Serial.println("CDraw save failed: SD unavailable");
        return;
    }

    String filename = nextDrawingFilename();
    File file = SD.open(filename.c_str(), FILE_WRITE);
    if (!file) {
        setStatus("File create failed");
        drawHeader();
        Serial.println("CDraw save failed: open");
        return;
    }

    const uint32_t rowBytes = ((CANVAS_W * 3U + 3U) / 4U) * 4U;
    const uint32_t dataSize = rowBytes * CANVAS_H;
    const uint32_t headerSize = 14 + 40;
    file.write('B');
    file.write('M');
    writeLE32(file, headerSize + dataSize);
    writeLE16(file, 0);
    writeLE16(file, 0);
    writeLE32(file, headerSize);
    writeLE32(file, 40);
    writeLE32(file, CANVAS_W);
    writeLE32(file, CANVAS_H);
    writeLE16(file, 1);
    writeLE16(file, 24);
    writeLE32(file, 0);
    writeLE32(file, dataSize);
    writeLE32(file, 2835);
    writeLE32(file, 2835);
    writeLE32(file, 0);
    writeLE32(file, 0);

    uint8_t *row = (uint8_t *)malloc(rowBytes);
    if (!row) {
        file.close();
        SD.remove(filename.c_str());
        setStatus("Save memory failed");
        drawHeader();
        return;
    }

    bool writeOk = true;
    for (int y = CANVAS_H - 1; y >= 0 && writeOk; --y) {
        memset(row, 0, rowBytes);
        for (int x = 0; x < CANVAS_W; ++x) {
            uint32_t color = videodisplay.getFast(CANVAS_X + x, CANVAS_Y + y);
            size_t offset = (size_t)x * 3;
            row[offset + 0] = (uint8_t)((color >> 16) & 0xff);
            row[offset + 1] = (uint8_t)((color >> 8) & 0xff);
            row[offset + 2] = (uint8_t)(color & 0xff);
        }
        writeOk = file.write(row, rowBytes) == rowBytes;
    }
    free(row);
    file.close();

    if (!writeOk) {
        SD.remove(filename.c_str());
        setStatus("SD write failed");
        Serial.println("CDraw save failed: short write");
    } else {
        const char *base = strrchr(filename.c_str(), '/');
        setStatus(base ? base + 1 : filename.c_str());
        Serial.printf("CDraw saved %s (%dx%d, 24-bit)\n",
                      filename.c_str(), CANVAS_W, CANVAS_H);
        tone(SPEAKER_PIN, 1480, 65);
    }
    drawHeader();
}

static void setSelectedRgb(uint8_t red, uint8_t green, uint8_t blue) {
    int oldX = pickerMarkerX();
    int oldY = pickerMarkerY();
    selectedRed = red;
    selectedGreen = green;
    selectedBlue = blue;
    rgbToHsv(red, green, blue, selectedHue, selectedSaturation, selectedValue);
    redrawPickerArea(oldX, oldY);
    drawColorSquare();
    drawHueStrip();
    drawSelectedColor();
    setStatus("RGB color set");
    drawHeader();
}

static void handleInput(String line) {
    line.trim();
    if (!line.length()) return;

    if (line.startsWith("MOUSE")) {
        int x = 0, y = 0, buttons = 0, dx = 0, dy = 0, wheel = 0;
        int parsed = sscanf(line.c_str(), "MOUSE %d %d %d %d %d %d",
                            &x, &y, &buttons, &dx, &dy, &wheel);
        if (appCursor.HandleMouseReport(line)) {
            if (parsed >= 6 && wheel != 0) {
                appCursor.Restore();
                adjustBrushSize(wheel > 0 ? 2 : -2);
                appCursor.Redraw();
            }
            return;
        }
    }

    if (line == "rlsd") {
        keyUp = keyDown = keyLeft = keyRight = false;
        return;
    }
    if (line == "Escape") {
        writeBootState("trueKernel");
        restartWithFallbackVideo("Returning home");
        return;
    }
    if (line == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting");
        return;
    }

    appCursor.Restore();
    if (line == "Tab" || line == "s" || line == "S") {
        saveDrawingBMP();
    } else if (line == "b" || line == "B") {
        selectTool(TOOL_BRUSH);
    } else if (line == "e" || line == "E" || line == "LeftShift +") {
        selectTool(TOOL_ERASER);
    } else if (line == "l" || line == "L") {
        selectTool(TOOL_LINE);
    } else if (line == "r" || line == "R") {
        selectTool(TOOL_RECTANGLE);
    } else if (line == "c" || line == "C") {
        selectTool(TOOL_CIRCLE);
    } else if (line == "f" || line == "F") {
        selectTool(TOOL_FILL);
    } else if (line == "+" || line == "=" || line == "]") {
        adjustBrushSize(2);
    } else if (line == "-" || line == "[") {
        adjustBrushSize(-2);
    } else if (line == "z" || line == "Z" || line == "LeftControl + z" ||
               line == "RightControl + z") {
        undoLastAction();
    } else if (line == "Delete" || line == "Backspace" || line == "n" || line == "N") {
        clearCanvas();
    } else if (line == "Enter") {
        if (activeTool == TOOL_FILL && insideCanvasScreen(appCursor.X(), appCursor.Y())) {
            floodFillCanvas(appCursor.X() - CANVAS_X, appCursor.Y() - CANVAS_Y);
        } else if (activeTool == TOOL_BRUSH || activeTool == TOOL_ERASER) {
            keyboardDrawing = !keyboardDrawing;
            setStatus(keyboardDrawing ? "Keyboard draw on" : "Keyboard draw off");
            drawHeader();
        } else {
            setStatus("Drag shape on canvas");
            drawHeader();
        }
    } else if (line == "UpArrow") {
        keyUp = true;
    } else if (line == "DownArrow") {
        keyDown = true;
    } else if (line == "LeftArrow") {
        keyLeft = true;
    } else if (line == "RightArrow") {
        keyRight = true;
    } else if (line.startsWith("CDRAW COLOR ")) {
        int red, green, blue;
        if (sscanf(line.c_str(), "CDRAW COLOR %d %d %d", &red, &green, &blue) == 3)
            setSelectedRgb((uint8_t)constrain(red, 0, 255),
                           (uint8_t)constrain(green, 0, 255),
                           (uint8_t)constrain(blue, 0, 255));
    } else if (line == "CDRAW STATUS" || line == "STATUS") {
        Serial.printf("CDRAW READY tool=%s size=%u color=#%02X%02X%02X undo=%u heap=%u max=%u\n",
                      TOOL_LABELS[activeTool], brushSize,
                      selectedRed, selectedGreen, selectedBlue,
                      undoAvailable ? 1U : 0U,
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
    appCursor.Redraw();
}

static void stepKeyboardCursor() {
    int dx = (keyRight ? 1 : 0) - (keyLeft ? 1 : 0);
    int dy = (keyDown ? 1 : 0) - (keyUp ? 1 : 0);
    if (dx == 0 && dy == 0) return;

    int oldX = constrain(appCursor.X(), CANVAS_X, CANVAS_X + CANVAS_W - 1);
    int oldY = constrain(appCursor.Y(), CANVAS_Y, CANVAS_Y + CANVAS_H - 1);
    int nextX = constrain(oldX + dx, CANVAS_X, CANVAS_X + CANVAS_W - 1);
    int nextY = constrain(oldY + dy, CANVAS_Y, CANVAS_Y + CANVAS_H - 1);
    appCursor.Restore();
    if (keyboardDrawing && (activeTool == TOOL_BRUSH || activeTool == TOOL_ERASER)) {
        beginUndoAction();
        uint32_t color = activeTool == TOOL_ERASER ? rgb(0, 0, 0) : selectedColor();
        drawLineShape(oldX - CANVAS_X, oldY - CANVAS_Y,
                      nextX - CANVAS_X, nextY - CANVAS_Y, false, color);
        finishUndoAction();
    }
    appCursor.MoveMouseTo(nextX, nextY, 0);
}

void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 980);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);

    if (!SPIFFS.begin(true)) {
        noTone(SPEAKER_PIN);
        return;
    }
    String bootState = readBootState();
    if (bootState == "trueKernel") {
        writeBootState("false");
        sdReady = mountSD();
        if (sdReady) flashKernel();
        else controllerVideo.forceProgress(100, "SD mount failed");
        noTone(SPEAKER_PIN);
        return;
    }

    loadCursorConfig();
    sdReady = mountSD();
    videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, VIDEO_PIN, true);
    if (!videoReady && sdReady) {
        SD.end();
        SPI.end();
        sdReady = false;
        delay(20);
        videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, VIDEO_PIN, true);
    }
    noTone(SPEAKER_PIN);
    if (!videoReady) {
        Serial.println("CDraw video allocation failed");
        return;
    }

    updateSelectedRgb();
    controllerVideo.appVideoActive(8, 35);
    drawApplication();
    appCursor.Begin(videodisplay, SCREEN_W, SCREEN_H, &cursorOutlined);
    appCursor.SetCallbacks(onMouseHover, nullptr);
    appCursor.MoveMouseTo(CANVAS_X + CANVAS_W / 2,
                          CANVAS_Y + CANVAS_H / 2, 0);
    lastKeyStepMs = millis();
    Serial.printf("CDRAW READY %dx%d canvas=%dx%d color=#%02X%02X%02X heap=%u max=%u sd=%u\n",
                  SCREEN_W, SCREEN_H, CANVAS_W, CANVAS_H,
                  selectedRed, selectedGreen, selectedBlue,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap(), sdReady ? 1U : 0U);
}

void loop() {
    String line;
    while (controllerReader.poll(Serial1, line)) handleInput(line);
    while (usbReader.poll(Serial, line)) handleInput(line);

    uint32_t now = millis();
    if ((keyUp || keyDown || keyLeft || keyRight) &&
        now - lastKeyStepMs >= KEY_STEP_MS) {
        lastKeyStepMs = now;
        stepKeyboardCursor();
    }
    delay(1);
}
