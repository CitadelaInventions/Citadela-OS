#pragma once

#include <Arduino.h>

namespace Citadela {

template <typename Display, int MaxPixels = 81>
class MouseCursor {
  public:
    typedef typename Display::RawPixel RawPixel;
    typedef void (*HoverCallback)(int x, int y, bool leftDown);
    typedef void (*ClickCallback)(int x, int y);

    struct Report {
        int x = 0;
        int y = 0;
        int buttons = 0;
        int dx = 0;
        int dy = 0;
        int wheel = 0;
        bool leftDown = false;
        bool leftPressed = false;
    };

    void begin(Display &targetDisplay, int width, int height, bool *outlineMode = nullptr) {
        display = &targetDisplay;
        screenW = width;
        screenH = height;
        outlineModeState = outlineMode;
        x = width / 2;
        y = height / 2;
    }

    void Begin(Display &targetDisplay, int width, int height, bool *outlineMode = nullptr) {
        begin(targetDisplay, width, height, outlineMode);
    }

    void setCallbacks(HoverCallback hover, ClickCallback click) {
        hoverCallback = hover;
        clickCallback = click;
    }

    void SetCallbacks(HoverCallback hover, ClickCallback click) {
        setCallbacks(hover, click);
    }

    void enable(bool enabled = true) {
        if (!enabled) restore();
        cursorEnabled = enabled;
    }

    void Enable(bool enabled = true) {
        enable(enabled);
    }

    void restore() {
        if (!display || !drawn) return;
        for (int i = 0; i < savedCount; ++i) {
            display->setRawPixelFast(x + saved[i].dx, y + saved[i].dy, saved[i].raw);
        }
        drawn = false;
        savedCount = 0;
    }

    void Restore() {
        restore();
    }

    void redraw() {
        if (!display || !cursorEnabled || drawn) return;
        savedCount = 0;
        int radius = outlined() ? 4 : 3;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (!capturePoint(dx, dy)) continue;
                int px = x + dx;
                int py = y + dy;
                if ((unsigned int)px >= (unsigned int)screenW || (unsigned int)py >= (unsigned int)screenH) continue;
                if (savedCount >= MaxPixels) continue;
                saved[savedCount].dx = dx;
                saved[savedCount].dy = dy;
                saved[savedCount].raw = display->getRawPixelFast(px, py);
                savedCount++;
            }
        }

        uint32_t white = display->RGB(255, 255, 255);
        uint32_t black = display->RGB(0, 0, 0);
        for (int i = 0; i < savedCount; ++i) {
            bool outline = outlinePoint(saved[i].dx, saved[i].dy);
            uint32_t color = white;
            if (outlined()) {
                color = outline ? white : black;
            } else {
                color = backgroundLooksBright(saved[i].raw) ? black : white;
            }
            display->dotFast(x + saved[i].dx, y + saved[i].dy, color);
        }
        drawn = true;
    }

    void Redraw() {
        redraw();
    }

    void refreshAfterRedraw() {
        if (cursorEnabled && !drawn) redraw();
    }

    void RefreshAfterRedraw() {
        refreshAfterRedraw();
    }

    void moveTo(int nx, int ny, int newButtons) {
        if (!display || screenW <= 0 || screenH <= 0) return;
        nx = constrain(nx, 0, screenW - 1);
        ny = constrain(ny, 0, screenH - 1);
        if (cursorEnabled && drawn && nx == x && ny == y) {
            buttons = newButtons;
            return;
        }
        restore();
        x = nx;
        y = ny;
        buttons = newButtons;
        cursorEnabled = true;
        redraw();
    }

    void MoveMouseTo(int nx, int ny, int newButtons = 0) {
        moveTo(nx, ny, newButtons);
    }

    bool parseReport(const String &input, Report &report) const {
        String line = input;
        line.trim();
        if (!line.startsWith("MOUSE")) return false;
        report.x = x;
        report.y = y;
        report.buttons = buttons;
        int parsed = sscanf(line.c_str(),
                            "MOUSE %d %d %d %d %d %d",
                            &report.x,
                            &report.y,
                            &report.buttons,
                            &report.dx,
                            &report.dy,
                            &report.wheel);
        if (parsed < 3) return false;
        report.leftDown = (report.buttons & 0x01) != 0;
        report.leftPressed = report.leftDown && !wasLeftDown;
        return true;
    }

    bool handleReport(const String &input) {
        if (!display) return false;
        Report report;
        if (!parseReport(input, report)) return false;
        moveTo(report.x, report.y, report.buttons);
        if (hoverCallback) hoverCallback(x, y, report.leftDown);
        if (report.leftPressed && clickCallback) clickCallback(x, y);
        wasLeftDown = report.leftDown;
        return true;
    }

    bool HandleMouseReport(const String &input) {
        return handleReport(input);
    }

    int currentX() const { return x; }
    int currentY() const { return y; }
    int currentButtons() const { return buttons; }
    bool isDrawn() const { return drawn; }

    int X() const { return currentX(); }
    int Y() const { return currentY(); }
    int Buttons() const { return currentButtons(); }
    bool IsDrawn() const { return isDrawn(); }

  private:
    struct SavedPixel {
        int8_t dx;
        int8_t dy;
        RawPixel raw;
    };

    Display *display = nullptr;
    SavedPixel saved[MaxPixels];
    int savedCount = 0;
    int screenW = 0;
    int screenH = 0;
    int x = 0;
    int y = 0;
    int buttons = 0;
    bool cursorEnabled = false;
    bool drawn = false;
    bool wasLeftDown = false;
    bool *outlineModeState = nullptr;
    HoverCallback hoverCallback = nullptr;
    ClickCallback clickCallback = nullptr;

    bool outlined() const {
        return outlineModeState && *outlineModeState;
    }

    static bool innerPoint(int dx, int dy) {
        int d2 = dx * dx + dy * dy;
        return d2 >= 5 && d2 <= 10;
    }

    static bool outlinePoint(int dx, int dy) {
        int d2 = dx * dx + dy * dy;
        return d2 > 10 && d2 <= 18;
    }

    bool capturePoint(int dx, int dy) const {
        return innerPoint(dx, dy) || (outlined() && outlinePoint(dx, dy));
    }

    bool backgroundLooksBright(RawPixel raw) const {
        return display->rawPixelBrightness(raw) >= 128;
    }
};

template <typename Cursor>
class CursorDrawGuard {
  public:
    explicit CursorDrawGuard(Cursor &target) : cursor(target) {
        cursor.restore();
    }

    ~CursorDrawGuard() {
        cursor.refreshAfterRedraw();
    }

  private:
    Cursor &cursor;
};

}  // namespace Citadela
