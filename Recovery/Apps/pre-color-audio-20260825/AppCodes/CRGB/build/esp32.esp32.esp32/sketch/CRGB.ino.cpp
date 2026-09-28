#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
#include <Arduino.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <Update.h>
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaMouseCursor.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"

static const int SCREEN_W = 376;
static const int SCREEN_H = 285;
static const int SD_CS = 5;
static const int VIDEO_PIN = 25;
static const int SPEAKER_PIN = 12;

static const int TAB_Y = 32;
static const int TAB_H = 18;
static const int LIST_X = 8;
static const int LIST_Y = 57;
static const int LIST_W = 212;
static const int ROW_H = 20;
static const int TRACK_X = LIST_X + 100;
static const int TRACK_W = 68;
static const int PREVIEW_X = 228;
static const int PREVIEW_Y = LIST_Y;
static const int PREVIEW_W = 140;
static const int PREVIEW_H = 162;
static const int FOOTER_Y = 227;
static const int ACTION_Y = 239;
static const int ACTION_H = 20;

enum CalibrationField {
    FIELD_NONE,
    FIELD_BRIGHTNESS,
    FIELD_CONTRAST,
    FIELD_SATURATION,
    FIELD_HUE,
    FIELD_RED_GAIN,
    FIELD_GREEN_GAIN,
    FIELD_BLUE_GAIN,
    FIELD_GAMMA,
    FIELD_SYNC_LEVEL,
    FIELD_BLANKING_LEVEL,
    FIELD_BLACK_LEVEL,
    FIELD_WHITE_LEVEL,
    FIELD_CLIP_MIN,
    FIELD_CLIP_MAX,
    FIELD_BURST_AMPLITUDE,
    FIELD_BURST_PHASE,
    FIELD_H_FRONT,
    FIELD_H_SYNC,
    FIELD_H_BACK,
    FIELD_BURST_START,
    FIELD_BURST_LENGTH,
    FIELD_ACTIVE_START,
    FIELD_PIXEL_CLOCK,
    FIELD_COLOR_CLOCK,
    FIELD_V_FRONT,
    FIELD_V_PRE_EQ,
    FIELD_V_SYNC,
    FIELD_V_POST_EQ,
    FIELD_V_BACK,
    FIELD_ACTIVE_PHASE,
    FIELD_LINE_BUFFERS
};

struct Parameter {
    const char *label;
    CalibrationField field;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
};

static const char *PAGE_NAMES[] = {"Picture", "Levels", "Timing", "Vertical"};
static const uint8_t PAGE_COUNTS[] = {8, 8, 8, 6};
static const Parameter PARAMETERS[4][8] = {
    {
        {"Brightness", FIELD_BRIGHTNESS, -128, 127, 2},
        {"Contrast", FIELD_CONTRAST, 0, 300, 5},
        {"Saturation", FIELD_SATURATION, 0, 300, 5},
        {"Hue", FIELD_HUE, -180, 180, 3},
        {"Red gain", FIELD_RED_GAIN, 0, 200, 2},
        {"Green gain", FIELD_GREEN_GAIN, 0, 200, 2},
        {"Blue gain", FIELD_BLUE_GAIN, 0, 200, 2},
        {"Gamma", FIELD_GAMMA, 50, 250, 5}
    },
    {
        {"Sync DAC", FIELD_SYNC_LEVEL, 0, 255, 1},
        {"Blanking DAC", FIELD_BLANKING_LEVEL, 0, 255, 1},
        {"Black DAC", FIELD_BLACK_LEVEL, 0, 255, 1},
        {"White DAC", FIELD_WHITE_LEVEL, 0, 255, 1},
        {"Clip minimum", FIELD_CLIP_MIN, 0, 255, 1},
        {"Clip maximum", FIELD_CLIP_MAX, 0, 255, 1},
        {"Burst amp", FIELD_BURST_AMPLITUDE, 0, 127, 1},
        {"Burst phase", FIELD_BURST_PHASE, -180, 180, 3}
    },
    {
        {"H front", FIELD_H_FRONT, -24, 24, 1},
        {"H sync", FIELD_H_SYNC, -24, 24, 1},
        {"H back", FIELD_H_BACK, -24, 24, 1},
        {"Burst start", FIELD_BURST_START, -24, 24, 1},
        {"Burst length", FIELD_BURST_LENGTH, -24, 24, 1},
        {"Active start", FIELD_ACTIVE_START, -24, 24, 1},
        {"Pixel clock", FIELD_PIXEL_CLOCK, -50000, 50000, 250},
        {"Colour clock", FIELD_COLOR_CLOCK, -10000, 10000, 50}
    },
    {
        {"V front", FIELD_V_FRONT, -4, 4, 1},
        {"Pre equalize", FIELD_V_PRE_EQ, -4, 4, 1},
        {"V sync", FIELD_V_SYNC, -4, 4, 1},
        {"Post equalize", FIELD_V_POST_EQ, -4, 4, 1},
        {"V back", FIELD_V_BACK, -8, 8, 1},
        {"Active phase", FIELD_ACTIVE_PHASE, -180, 180, 3},
        {"DMA lines", FIELD_LINE_BUFFERS, 1, 16, 1},
        {"", FIELD_NONE, 0, 0, 0}
    }
};

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::MouseCursor<Citadela::CitCompositeColorDAC, 81> appCursor;
static Citadela::LineReader controllerReader(160);
static Citadela::LineReader usbReader(160);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);

static CompositeColorCalibration stagedProfile;
static CompositeColorCalibration appliedProfile;
static bool cursorOutlined = false;
static bool videoReady = false;
static uint8_t selectedPage = 0;
static uint8_t selectedRow = 0;
static int dragRow = -1;
static uint32_t lastPreviewMs = 0;
static char statusText[52] = "Live preview; Apply restarts signal";

static void onMouseHover(int x, int y, bool leftDown);
static void onMouseClick(int x, int y);

#line 142 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
#line 146 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static int32_t fieldValue(const CompositeColorCalibration &profile, CalibrationField field);
#line 183 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void setFieldValue(CompositeColorCalibration &profile, CalibrationField field, int32_t value);
#line 221 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool fieldIsPercent(CalibrationField field);
#line 227 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool fieldIsDegrees(CalibrationField field);
#line 231 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool fieldIsClock(CalibrationField field);
#line 235 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void formatValue(const Parameter &parameter, int32_t value, char *output, size_t outputSize);
#line 243 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void setStatus(const char *message);
#line 247 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawHeader();
#line 264 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawTabs();
#line 281 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawParameterRow(uint8_t row);
#line 310 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawParameterList();
#line 316 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawPreview();
#line 365 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawStatusOnly();
#line 374 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawFooter();
#line 390 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void drawApplication();
#line 399 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void refreshAfterValueChange(uint8_t row, bool forcePreview);
#line 413 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void selectPage(uint8_t page);
#line 424 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void selectParameter(uint8_t row);
#line 433 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void setParameterFromX(uint8_t row, int x, bool forcePreview);
#line 450 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void adjustSelected(int direction);
#line 460 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool writeProfileText();
#line 480 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void printProfile(Stream &output);
#line 495 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool reinitializeVideoWithProfile();
#line 532 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void resetStagedProfile();
#line 540 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void undoStagedProfile();
#line 548 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void saveStagedProfile();
#line 564 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void activateAction(int action);
#line 571 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static int tabAt(int x, int y);
#line 580 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static int rowAt(int x, int y);
#line 586 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static int actionAt(int x, int y);
#line 622 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void loadCursorConfig();
#line 636 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static String readBootState();
#line 645 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool writeBootState(const char *value);
#line 653 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool mountSD();
#line 666 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void restartWithFallbackVideo(const char *label);
#line 681 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void flashKernel();
#line 720 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static String normalizedKey(String key);
#line 728 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool setParameterByName(String name, int32_t value);
#line 752 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static bool handleCalibrationCommand(const String &input);
#line 794 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static void handleControllerLine(String line);
#line 837 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
void setup();
#line 881 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
void loop();
#line 142 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CRGB\\CRGB.ino"
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return videodisplay.RGB(r, g, b);
}

static int32_t fieldValue(const CompositeColorCalibration &profile, CalibrationField field) {
    switch (field) {
        case FIELD_BRIGHTNESS: return profile.brightness;
        case FIELD_CONTRAST: return profile.contrast;
        case FIELD_SATURATION: return profile.saturation;
        case FIELD_HUE: return profile.hueDegrees;
        case FIELD_RED_GAIN: return profile.redGain;
        case FIELD_GREEN_GAIN: return profile.greenGain;
        case FIELD_BLUE_GAIN: return profile.blueGain;
        case FIELD_GAMMA: return profile.gammaX100;
        case FIELD_SYNC_LEVEL: return profile.syncLevel;
        case FIELD_BLANKING_LEVEL: return profile.blankingLevel;
        case FIELD_BLACK_LEVEL: return profile.blackLevel;
        case FIELD_WHITE_LEVEL: return profile.whiteLevel;
        case FIELD_CLIP_MIN: return profile.clipMin;
        case FIELD_CLIP_MAX: return profile.clipMax;
        case FIELD_BURST_AMPLITUDE: return profile.burstAmplitude;
        case FIELD_BURST_PHASE: return profile.burstPhaseDegrees;
        case FIELD_H_FRONT: return profile.hFrontAdjust;
        case FIELD_H_SYNC: return profile.hSyncAdjust;
        case FIELD_H_BACK: return profile.hBackAdjust;
        case FIELD_BURST_START: return profile.burstStartAdjust;
        case FIELD_BURST_LENGTH: return profile.burstLengthAdjust;
        case FIELD_ACTIVE_START: return profile.activeStartSamples;
        case FIELD_PIXEL_CLOCK: return profile.pixelClockPpm;
        case FIELD_COLOR_CLOCK: return profile.colorClockPpm;
        case FIELD_V_FRONT: return profile.vFrontAdjust;
        case FIELD_V_PRE_EQ: return profile.vPreEqualizingAdjust;
        case FIELD_V_SYNC: return profile.vSyncAdjust;
        case FIELD_V_POST_EQ: return profile.vPostEqualizingAdjust;
        case FIELD_V_BACK: return profile.vBackAdjust;
        case FIELD_ACTIVE_PHASE: return profile.activePhaseDegrees;
        case FIELD_LINE_BUFFERS: return profile.lineBufferCount;
        default: return 0;
    }
}

static void setFieldValue(CompositeColorCalibration &profile, CalibrationField field, int32_t value) {
    switch (field) {
        case FIELD_BRIGHTNESS: profile.brightness = value; break;
        case FIELD_CONTRAST: profile.contrast = value; break;
        case FIELD_SATURATION: profile.saturation = value; break;
        case FIELD_HUE: profile.hueDegrees = value; break;
        case FIELD_RED_GAIN: profile.redGain = value; break;
        case FIELD_GREEN_GAIN: profile.greenGain = value; break;
        case FIELD_BLUE_GAIN: profile.blueGain = value; break;
        case FIELD_GAMMA: profile.gammaX100 = value; break;
        case FIELD_SYNC_LEVEL: profile.syncLevel = value; break;
        case FIELD_BLANKING_LEVEL: profile.blankingLevel = value; break;
        case FIELD_BLACK_LEVEL: profile.blackLevel = value; break;
        case FIELD_WHITE_LEVEL: profile.whiteLevel = value; break;
        case FIELD_CLIP_MIN: profile.clipMin = value; break;
        case FIELD_CLIP_MAX: profile.clipMax = value; break;
        case FIELD_BURST_AMPLITUDE: profile.burstAmplitude = value; break;
        case FIELD_BURST_PHASE: profile.burstPhaseDegrees = value; break;
        case FIELD_H_FRONT: profile.hFrontAdjust = value; break;
        case FIELD_H_SYNC: profile.hSyncAdjust = value; break;
        case FIELD_H_BACK: profile.hBackAdjust = value; break;
        case FIELD_BURST_START: profile.burstStartAdjust = value; break;
        case FIELD_BURST_LENGTH: profile.burstLengthAdjust = value; break;
        case FIELD_ACTIVE_START: profile.activeStartSamples = value; break;
        case FIELD_PIXEL_CLOCK: profile.pixelClockPpm = value; break;
        case FIELD_COLOR_CLOCK: profile.colorClockPpm = value; break;
        case FIELD_V_FRONT: profile.vFrontAdjust = value; break;
        case FIELD_V_PRE_EQ: profile.vPreEqualizingAdjust = value; break;
        case FIELD_V_SYNC: profile.vSyncAdjust = value; break;
        case FIELD_V_POST_EQ: profile.vPostEqualizingAdjust = value; break;
        case FIELD_V_BACK: profile.vBackAdjust = value; break;
        case FIELD_ACTIVE_PHASE: profile.activePhaseDegrees = value; break;
        case FIELD_LINE_BUFFERS: profile.lineBufferCount = value; break;
        default: break;
    }
    profile.sanitize();
}

static bool fieldIsPercent(CalibrationField field) {
    return field == FIELD_CONTRAST || field == FIELD_SATURATION ||
           field == FIELD_RED_GAIN || field == FIELD_GREEN_GAIN ||
           field == FIELD_BLUE_GAIN || field == FIELD_GAMMA;
}

static bool fieldIsDegrees(CalibrationField field) {
    return field == FIELD_HUE || field == FIELD_BURST_PHASE || field == FIELD_ACTIVE_PHASE;
}

static bool fieldIsClock(CalibrationField field) {
    return field == FIELD_PIXEL_CLOCK || field == FIELD_COLOR_CLOCK;
}

static void formatValue(const Parameter &parameter, int32_t value, char *output, size_t outputSize) {
    if (fieldIsPercent(parameter.field)) snprintf(output, outputSize, "%ld%%", (long)value);
    else if (fieldIsDegrees(parameter.field)) snprintf(output, outputSize, "%ldd", (long)value);
    else if (fieldIsClock(parameter.field)) snprintf(output, outputSize, "%+ld", (long)value);
    else if (parameter.minimum < 0) snprintf(output, outputSize, "%+ld", (long)value);
    else snprintf(output, outputSize, "%ld", (long)value);
}

static void setStatus(const char *message) {
    snprintf(statusText, sizeof(statusText), "%s", message ? message : "");
}

static void drawHeader() {
    uint32_t header = rgb(22, 54, 72);
    videodisplay.fillRect(0, 0, SCREEN_W, 28, header);
    videodisplay.fillRect(0, 27, SCREEN_W, 1, rgb(38, 211, 190));
    videodisplay.fillRect(12, 7, 5, 14, rgb(236, 67, 77));
    videodisplay.fillRect(18, 7, 5, 14, rgb(52, 205, 120));
    videodisplay.fillRect(24, 7, 5, 14, rgb(73, 145, 236));
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(255, 255, 255), header);
    videodisplay.setCursor(38, 10);
    videodisplay.print("Composite Calibration");
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(165, 205, 215), header);
    videodisplay.setCursor(294, 10);
    videodisplay.print(videodisplay.hasStoredCalibration() ? "SAVED" : "DEFAULT");
}

static void drawTabs() {
    uint32_t background = rgb(10, 15, 18);
    uint32_t selected = rgb(35, 91, 107);
    uint32_t edge = rgb(68, 116, 126);
    for (int page = 0; page < 4; ++page) {
        int x = 8 + page * 91;
        uint32_t fill = page == selectedPage ? selected : rgb(20, 29, 34);
        videodisplay.fillRect(x, TAB_Y, 85, TAB_H, fill);
        videodisplay.rect(x, TAB_Y, 85, TAB_H, edge);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(page == selectedPage ? rgb(255, 255, 255) : rgb(157, 180, 187), fill);
        videodisplay.setCursor(x + 9, TAB_Y + 5);
        videodisplay.print(PAGE_NAMES[page]);
    }
    videodisplay.fillRect(0, TAB_Y + TAB_H + 1, SCREEN_W, 2, background);
}

static void drawParameterRow(uint8_t row) {
    if (row >= PAGE_COUNTS[selectedPage]) return;
    const Parameter &parameter = PARAMETERS[selectedPage][row];
    int y = LIST_Y + row * ROW_H;
    bool selected = row == selectedRow;
    uint32_t fill = selected ? rgb(26, 54, 63) : rgb(17, 24, 28);
    uint32_t track = rgb(52, 68, 73);
    uint32_t accent = rgb(38, 211, 190);
    videodisplay.fillRect(LIST_X, y, LIST_W, ROW_H - 1, fill);
    if (selected) videodisplay.fillRect(LIST_X, y, 3, ROW_H - 1, accent);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(selected ? rgb(255, 255, 255) : rgb(183, 199, 203), fill);
    videodisplay.setCursor(LIST_X + 8, y + 6);
    videodisplay.print(parameter.label);

    int32_t value = fieldValue(stagedProfile, parameter.field);
    int knob = TRACK_X + (int)(((int64_t)(value - parameter.minimum) * (TRACK_W - 1)) /
                               (parameter.maximum - parameter.minimum));
    videodisplay.fillRect(TRACK_X, y + 8, TRACK_W, 4, track);
    videodisplay.fillRect(TRACK_X, y + 8, max(1, knob - TRACK_X + 1), 4, accent);
    videodisplay.fillRect(knob - 1, y + 5, 3, 10, rgb(245, 250, 250));

    char valueText[16];
    formatValue(parameter, value, valueText, sizeof(valueText));
    videodisplay.setTextColor(rgb(222, 231, 232), fill);
    videodisplay.setCursor(LIST_X + 172, y + 6);
    videodisplay.print(valueText);
}

static void drawParameterList() {
    videodisplay.fillRect(LIST_X, LIST_Y, LIST_W, ROW_H * 8, rgb(17, 24, 28));
    videodisplay.rect(LIST_X, LIST_Y, LIST_W, ROW_H * 8, rgb(60, 83, 89));
    for (uint8_t row = 0; row < PAGE_COUNTS[selectedPage]; ++row) drawParameterRow(row);
}

static void drawPreview() {
    uint32_t panel = rgb(14, 20, 23);
    uint32_t edge = rgb(69, 100, 107);
    videodisplay.fillRect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, panel);
    videodisplay.rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, edge);

    static const uint8_t bars[8][3] = {
        {255, 255, 255}, {255, 235, 30}, {20, 230, 230}, {30, 210, 65},
        {225, 40, 210}, {235, 40, 45}, {35, 75, 235}, {0, 0, 0}
    };
    const int barX = PREVIEW_X + 6;
    const int barY = PREVIEW_Y + 7;
    const int barW = 16;
    for (int i = 0; i < 8; ++i)
        videodisplay.fillRect(barX + i * barW, barY, barW, 35, rgb(bars[i][0], bars[i][1], bars[i][2]));
    videodisplay.rect(barX - 1, barY - 1, barW * 8 + 2, 37, rgb(220, 230, 230));

    for (int i = 0; i < 8; ++i) {
        uint8_t level = i * 255 / 7;
        videodisplay.fillRect(barX + i * barW, PREVIEW_Y + 49, barW, 17, rgb(level, level, level));
    }

    int plugeY = PREVIEW_Y + 73;
    videodisplay.fillRect(barX, plugeY, 128, 28, rgb(8, 8, 8));
    const uint8_t pluge[] = {0, 4, 8, 12, 16, 24, 32, 48};
    for (int i = 0; i < 8; ++i)
        videodisplay.fillRect(barX + i * barW + 3, plugeY + 4, 10, 20, rgb(pluge[i], pluge[i], pluge[i]));

    videodisplay.fillRect(barX, PREVIEW_Y + 108, 128, 1, rgb(255, 255, 255));
    videodisplay.fillRect(barX, PREVIEW_Y + 115, 128, 1, rgb(120, 120, 120));
    for (int x = 0; x < 128; x += 8)
        videodisplay.fillRect(barX + x, PREVIEW_Y + 122, 4, 8, rgb(255, 255, 255));

    ModeComposite mode = videodisplay.calibratedMode(CompMode::MODEPALColor288Pmid);
    char timing[24];
    snprintf(timing, sizeof(timing), "H %d  V %d", mode.pixelsPerLine(), mode.linesPerFrame);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(165, 190, 197), panel);
    videodisplay.setCursor(PREVIEW_X + 7, PREVIEW_Y + 137);
    videodisplay.print(timing);
    snprintf(timing, sizeof(timing), "P %.3fMHz", (double)mode.pixelClock / 1000000.0);
    videodisplay.setCursor(PREVIEW_X + 7, PREVIEW_Y + 149);
    videodisplay.print(timing);
}

static const int ACTION_X[] = {8, 70, 132, 194};
static const int ACTION_W = 56;
static const char *ACTION_LABEL[] = {"Reset", "Undo", "Apply", "Save"};

static void drawStatusOnly() {
    uint32_t footer = rgb(13, 20, 23);
    videodisplay.fillRect(8, 265, SCREEN_W - 16, 12, footer);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(160, 188, 194), footer);
    videodisplay.setCursor(9, 267);
    videodisplay.print(statusText);
}

static void drawFooter() {
    uint32_t footer = rgb(13, 20, 23);
    videodisplay.fillRect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, footer);
    videodisplay.fillRect(0, FOOTER_Y, SCREEN_W, 1, rgb(38, 211, 190));
    for (int i = 0; i < 4; ++i) {
        uint32_t fill = i == 3 ? rgb(28, 90, 87) : rgb(25, 35, 40);
        videodisplay.fillRect(ACTION_X[i], ACTION_Y, ACTION_W, ACTION_H, fill);
        videodisplay.rect(ACTION_X[i], ACTION_Y, ACTION_W, ACTION_H, rgb(85, 118, 124));
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(rgb(245, 249, 249), fill);
        videodisplay.setCursor(ACTION_X[i] + 10, ACTION_Y + 6);
        videodisplay.print(ACTION_LABEL[i]);
    }
    drawStatusOnly();
}

static void drawApplication() {
    videodisplay.clear(rgb(10, 15, 18));
    drawHeader();
    drawTabs();
    drawParameterList();
    drawPreview();
    drawFooter();
}

static void refreshAfterValueChange(uint8_t row, bool forcePreview) {
    videodisplay.setCalibration(stagedProfile);
    drawParameterRow(row);
    uint32_t now = millis();
    if (forcePreview || now - lastPreviewMs >= 35) {
        drawPreview();
        lastPreviewMs = now;
    }
    setStatus(Citadela::CitCompositeColorDAC::requiresSignalRestart(appliedProfile, stagedProfile)
                  ? "Signal changed; Apply before Save"
                  : "Picture preview is live; Apply to commit");
    drawStatusOnly();
}

static void selectPage(uint8_t page) {
    page %= 4;
    if (page == selectedPage) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    selectedPage = page;
    if (selectedRow >= PAGE_COUNTS[selectedPage]) selectedRow = PAGE_COUNTS[selectedPage] - 1;
    drawTabs();
    drawParameterList();
    drawPreview();
}

static void selectParameter(uint8_t row) {
    if (row >= PAGE_COUNTS[selectedPage] || row == selectedRow) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    uint8_t old = selectedRow;
    selectedRow = row;
    drawParameterRow(old);
    drawParameterRow(selectedRow);
}

static void setParameterFromX(uint8_t row, int x, bool forcePreview) {
    if (row >= PAGE_COUNTS[selectedPage]) return;
    const Parameter &parameter = PARAMETERS[selectedPage][row];
    x = constrain(x, TRACK_X, TRACK_X + TRACK_W - 1);
    int64_t span = parameter.maximum - parameter.minimum;
    int32_t value = parameter.minimum + (int32_t)(((int64_t)(x - TRACK_X) * span) / (TRACK_W - 1));
    if (parameter.step > 1) {
        int32_t relative = value - parameter.minimum;
        value = parameter.minimum + ((relative + parameter.step / 2) / parameter.step) * parameter.step;
    }
    value = constrain(value, parameter.minimum, parameter.maximum);
    if (fieldValue(stagedProfile, parameter.field) == value) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    setFieldValue(stagedProfile, parameter.field, value);
    refreshAfterValueChange(row, forcePreview);
}

static void adjustSelected(int direction) {
    const Parameter &parameter = PARAMETERS[selectedPage][selectedRow];
    int32_t value = fieldValue(stagedProfile, parameter.field) + direction * parameter.step;
    value = constrain(value, parameter.minimum, parameter.maximum);
    if (fieldValue(stagedProfile, parameter.field) == value) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    setFieldValue(stagedProfile, parameter.field, value);
    refreshAfterValueChange(selectedRow, true);
}

static bool writeProfileText() {
    SPIFFS.remove("/videoCalibration.conf");
    File file = SPIFFS.open("/videoCalibration.conf", FILE_WRITE);
    if (!file) return false;
    for (int page = 0; page < 4; ++page) {
        for (int row = 0; row < PAGE_COUNTS[page]; ++row) {
            const Parameter &parameter = PARAMETERS[page][row];
            String key(parameter.label);
            key.replace(" ", "_");
            file.print(key);
            file.print('=');
            file.println((long)fieldValue(stagedProfile, parameter.field));
        }
    }
    file.print("Signature=");
    file.println((unsigned long)stagedProfile.signature(), HEX);
    file.close();
    return true;
}

static void printProfile(Stream &output) {
    output.println("CAL PROFILE BEGIN");
    for (int page = 0; page < 4; ++page) {
        for (int row = 0; row < PAGE_COUNTS[page]; ++row) {
            const Parameter &parameter = PARAMETERS[page][row];
            output.print(parameter.label);
            output.print('=');
            output.println((long)fieldValue(stagedProfile, parameter.field));
        }
    }
    output.print("Signature=");
    output.println((unsigned long)stagedProfile.signature(), HEX);
    output.println("CAL PROFILE END");
}

static bool reinitializeVideoWithProfile() {
    CompositeColorCalibration fallback = appliedProfile;
    int cursorX = appCursor.X();
    int cursorY = appCursor.Y();
    appCursor.Restore();
    Serial1.println("VDPREP 0 Applying video calibration");
    Serial1.flush();
    delay(60);

    if (videoReady) videodisplay.releaseVideoMemory();
    videoReady = false;
    videodisplay.setCalibration(stagedProfile);
    videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, VIDEO_PIN, true);
    if (!videoReady) {
        Serial.println("CAL apply allocation failed; restoring previous profile");
        videodisplay.setCalibration(fallback);
        videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, VIDEO_PIN, true);
        stagedProfile = fallback;
        if (!videoReady) {
            Serial.println("CAL recovery video initialization failed");
            return false;
        }
        setStatus("Apply failed; previous profile restored");
    } else {
        stagedProfile = videodisplay.getCalibration();
        appliedProfile = stagedProfile;
        setStatus("Profile applied; Save makes it persistent");
    }

    controllerVideo.appVideoActive(4, 20);
    drawApplication();
    appCursor.Begin(videodisplay, SCREEN_W, SCREEN_H, &cursorOutlined);
    appCursor.SetCallbacks(onMouseHover, onMouseClick);
    appCursor.MoveMouseTo(cursorX, cursorY, 0);
    return true;
}

static void resetStagedProfile() {
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    stagedProfile = CompositeColorCalibration::defaults(true);
    videodisplay.setCalibration(stagedProfile);
    setStatus("Bitluni PAL divider defaults staged");
    drawApplication();
}

static void undoStagedProfile() {
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    stagedProfile = appliedProfile;
    videodisplay.setCalibration(stagedProfile);
    setStatus("Returned to last applied profile");
    drawApplication();
}

static void saveStagedProfile() {
    if (stagedProfile.signature() != appliedProfile.signature() && !reinitializeVideoWithProfile()) return;
    stagedProfile.flags |= CompositeColorCalibration::FLAG_VOLTAGE_DIVIDER;
    videodisplay.setCalibration(stagedProfile);
    bool nvsSaved = videodisplay.saveCalibration();
    bool textSaved = writeProfileText();
    stagedProfile = videodisplay.getCalibration();
    appliedProfile = stagedProfile;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    setStatus(nvsSaved && textSaved ? "Saved globally to NVS and SPIFFS" : "Save incomplete; check serial log");
    drawHeader();
    drawStatusOnly();
    Serial.printf("CAL save NVS=%d SPIFFS=%d signature=%08lX\n",
                  nvsSaved, textSaved, (unsigned long)stagedProfile.signature());
}

static void activateAction(int action) {
    if (action == 0) resetStagedProfile();
    else if (action == 1) undoStagedProfile();
    else if (action == 2) reinitializeVideoWithProfile();
    else if (action == 3) saveStagedProfile();
}

static int tabAt(int x, int y) {
    if (y < TAB_Y || y >= TAB_Y + TAB_H) return -1;
    for (int page = 0; page < 4; ++page) {
        int tabX = 8 + page * 91;
        if (x >= tabX && x < tabX + 85) return page;
    }
    return -1;
}

static int rowAt(int x, int y) {
    if (x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y) return -1;
    int row = (y - LIST_Y) / ROW_H;
    return row >= 0 && row < PAGE_COUNTS[selectedPage] ? row : -1;
}

static int actionAt(int x, int y) {
    if (y < ACTION_Y || y >= ACTION_Y + ACTION_H) return -1;
    for (int action = 0; action < 4; ++action)
        if (x >= ACTION_X[action] && x < ACTION_X[action] + ACTION_W) return action;
    return -1;
}

static void onMouseHover(int x, int y, bool leftDown) {
    if (leftDown) {
        if (dragRow < 0) {
            int row = rowAt(x, y);
            if (row >= 0 && x >= TRACK_X - 4 && x <= TRACK_X + TRACK_W + 4) dragRow = row;
        }
        if (dragRow >= 0) setParameterFromX(dragRow, x, false);
    } else if (dragRow >= 0) {
        setParameterFromX(dragRow, x, true);
        dragRow = -1;
    }
}

static void onMouseClick(int x, int y) {
    int page = tabAt(x, y);
    if (page >= 0) {
        selectPage(page);
        return;
    }
    int row = rowAt(x, y);
    if (row >= 0) {
        selectParameter(row);
        if (x >= TRACK_X - 4) setParameterFromX(row, x, true);
        return;
    }
    int action = actionAt(x, y);
    if (action >= 0) activateAction(action);
}

static void loadCursorConfig() {
    File file = SPIFFS.open("/systemConfiguration.conf", FILE_READ);
    if (!file) return;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.startsWith("MouseCursorOutlined=") || line.startsWith("CursorOutlineMode=")) {
            int equals = line.indexOf('=');
            cursorOutlined = equals >= 0 && line.substring(equals + 1).toInt() != 0;
        }
    }
    file.close();
}

static String readBootState() {
    File file = SPIFFS.open("/evil.txt", FILE_READ);
    if (!file) return "false";
    String value = file.readString();
    file.close();
    value.trim();
    return value.length() ? value : "false";
}

static bool writeBootState(const char *value) {
    File file = SPIFFS.open("/evil.txt", FILE_WRITE);
    if (!file) return false;
    bool ok = file.println(value ? value : "false") > 0;
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
    Serial1.print("VDPREP 0 ");
    Serial1.println(label ? label : "Restarting");
    Serial1.flush();
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

static String normalizedKey(String key) {
    key.trim();
    key.toLowerCase();
    key.replace("_", " ");
    key.replace("colour", "color");
    return key;
}

static bool setParameterByName(String name, int32_t value) {
    name = normalizedKey(name);
    for (int page = 0; page < 4; ++page) {
        for (int row = 0; row < PAGE_COUNTS[page]; ++row) {
            String candidate = normalizedKey(String(PARAMETERS[page][row].label));
            if (candidate != name) continue;
            const Parameter &parameter = PARAMETERS[page][row];
            value = constrain(value, parameter.minimum, parameter.maximum);
            setFieldValue(stagedProfile, parameter.field, value);
            videodisplay.setCalibration(stagedProfile);
            selectedPage = page;
            selectedRow = row;
            if (videoReady) {
                Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
                drawTabs();
                drawParameterList();
                drawPreview();
            }
            return true;
        }
    }
    return false;
}

static bool handleCalibrationCommand(const String &input) {
    String line = input;
    line.trim();
    String upper = line;
    upper.toUpperCase();
    if (!upper.startsWith("CAL")) return false;

    if (upper == "CAL" || upper == "CAL HELP") {
        Serial.println("CAL GET | CAL SET <name> <value> | CAL APPLY | CAL SAVE | CAL RESET | CAL UNDO");
    } else if (upper == "CAL GET") {
        printProfile(Serial);
    } else if (upper == "CAL APPLY") {
        Serial.println(reinitializeVideoWithProfile() ? "CAL APPLY OK" : "CAL APPLY FAILED");
    } else if (upper == "CAL SAVE") {
        saveStagedProfile();
        Serial.println("CAL SAVE DONE");
    } else if (upper == "CAL RESET") {
        stagedProfile = CompositeColorCalibration::defaults(true);
        videodisplay.setCalibration(stagedProfile);
        Serial.println("CAL RESET STAGED; use CAL APPLY");
        if (videoReady) drawApplication();
    } else if (upper == "CAL UNDO") {
        stagedProfile = appliedProfile;
        videodisplay.setCalibration(stagedProfile);
        Serial.println("CAL UNDO OK");
        if (videoReady) drawApplication();
    } else if (upper.startsWith("CAL SET ")) {
        String arguments = line.substring(8);
        int split = arguments.lastIndexOf(' ');
        if (split <= 0) {
            Serial.println("CAL SET requires a name and integer value");
        } else {
            String name = arguments.substring(0, split);
            int32_t value = arguments.substring(split + 1).toInt();
            Serial.println(setParameterByName(name, value) ? "CAL SET OK" : "CAL SET UNKNOWN FIELD");
        }
    } else {
        Serial.println("CAL UNKNOWN COMMAND");
    }
    return true;
}

static void handleControllerLine(String line) {
    line.trim();
    if (!line.length()) return;
    if (handleCalibrationCommand(line)) return;
    if (appCursor.HandleMouseReport(line)) return;
    if (line == "Escape") {
        writeBootState("trueKernel");
        restartWithFallbackVideo("Returning home");
    } else if (line == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting");
    } else if (line == "LeftArrow") {
        adjustSelected(-1);
    } else if (line == "RightArrow") {
        adjustSelected(1);
    } else if (line == "UpArrow") {
        selectParameter(selectedRow == 0 ? PAGE_COUNTS[selectedPage] - 1 : selectedRow - 1);
    } else if (line == "DownArrow") {
        selectParameter((selectedRow + 1) % PAGE_COUNTS[selectedPage]);
    } else if (line == "Tab" || line == "PageDown") {
        selectPage((selectedPage + 1) % 4);
    } else if (line == "PageUp") {
        selectPage((selectedPage + 3) % 4);
    } else if (line == "F1") {
        selectPage(0);
    } else if (line == "F2") {
        selectPage(1);
    } else if (line == "F3") {
        selectPage(2);
    } else if (line == "F4") {
        selectPage(3);
    } else if (line == "Enter") {
        reinitializeVideoWithProfile();
    } else if (line == "s" || line == "S") {
        saveStagedProfile();
    } else if (line == "r" || line == "R") {
        resetStagedProfile();
    } else if (line == "u" || line == "U") {
        undoStagedProfile();
    } else if (line == "rlsd") {
        dragRow = -1;
    }
}

void setup() {
    pinMode(SPEAKER_PIN, OUTPUT);
    tone(SPEAKER_PIN, 1000);
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    if (!SPIFFS.begin(true)) {
        noTone(SPEAKER_PIN);
        return;
    }

    String bootState = readBootState();
    if (bootState == "trueKernel") {
        writeBootState("false");
        if (mountSD()) flashKernel();
        else controllerVideo.forceProgress(100, "SD mount failed");
        noTone(SPEAKER_PIN);
        return;
    }

    loadCursorConfig();
    if (mountSD()) SD.end();
    SPI.end();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, VIDEO_PIN, true);
    noTone(SPEAKER_PIN);
    if (!videoReady) {
        Serial.println("CAL initial video allocation failed");
        return;
    }

    stagedProfile = videodisplay.getCalibration();
    appliedProfile = stagedProfile;
    controllerVideo.appVideoActive(8, 35);
    drawApplication();
    appCursor.Begin(videodisplay, SCREEN_W, SCREEN_H, &cursorOutlined);
    appCursor.SetCallbacks(onMouseHover, onMouseClick);
    appCursor.MoveMouseTo(SCREEN_W / 2, SCREEN_H / 2, 0);
    Serial.printf("CAL ready signature=%08lX source=%s\n",
                  (unsigned long)stagedProfile.signature(),
                  videodisplay.hasStoredCalibration() ? "NVS" : "defaults");
}

void loop() {
    String line;
    while (controllerReader.poll(Serial1, line)) handleControllerLine(line);
    while (usbReader.poll(Serial, line)) handleControllerLine(line);
    delay(1);
}

