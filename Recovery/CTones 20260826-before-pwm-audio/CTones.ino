#include <Arduino.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Update.h>
#include <Preferences.h>
#include <pgmspace.h>
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaMouseCursor.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"

static const int SCREEN_W = 376;
static const int SCREEN_H = 288;
static const int VIDEO_PIN = 25;
static const int SPEAKER_PIN = 12;
static const int SD_CS = 5;

static const int NOTE_G3 = 196;
static const int NOTE_A3 = 220;
static const int NOTE_B3 = 247;
static const int NOTE_C4 = 262;
static const int NOTE_CS4 = 277;
static const int NOTE_D4 = 294;
static const int NOTE_DS4 = 311;
static const int NOTE_E4 = 330;
static const int NOTE_F4 = 349;
static const int NOTE_FS4 = 370;
static const int NOTE_G4 = 392;
static const int NOTE_GS4 = 415;
static const int NOTE_A4 = 440;
static const int NOTE_B4 = 494;
static const int NOTE_C5 = 523;
static const int NOTE_CS5 = 554;
static const int NOTE_D5 = 587;
static const int NOTE_DS5 = 622;
static const int NOTE_E5 = 659;
static const int NOTE_F5 = 698;
static const int NOTE_FS5 = 740;
static const int NOTE_G5 = 784;
static const int NOTE_GS5 = 831;
static const int NOTE_A5 = 880;

struct NoteEvent {
    uint16_t frequency;
    uint8_t sixteenths;
    uint8_t gatePercent;
};

// A full statement and reprise of the public-domain Beethoven theme.
static const NoteEvent ODE_TO_JOY[] PROGMEM = {
    {NOTE_E4,4,91},{NOTE_E4,4,91},{NOTE_F4,4,91},{NOTE_G4,4,91},
    {NOTE_G4,4,91},{NOTE_F4,4,91},{NOTE_E4,4,91},{NOTE_D4,4,91},
    {NOTE_C4,4,91},{NOTE_C4,4,91},{NOTE_D4,4,91},{NOTE_E4,4,91},
    {NOTE_E4,6,94},{NOTE_D4,2,86},{NOTE_D4,8,96},{0,2,0},
    {NOTE_E4,4,91},{NOTE_E4,4,91},{NOTE_F4,4,91},{NOTE_G4,4,91},
    {NOTE_G4,4,91},{NOTE_F4,4,91},{NOTE_E4,4,91},{NOTE_D4,4,91},
    {NOTE_C4,4,91},{NOTE_C4,4,91},{NOTE_D4,4,91},{NOTE_E4,4,91},
    {NOTE_D4,6,94},{NOTE_C4,2,86},{NOTE_C4,8,96},{0,2,0},
    {NOTE_D4,4,90},{NOTE_D4,4,90},{NOTE_E4,4,90},{NOTE_C4,4,90},
    {NOTE_D4,4,90},{NOTE_E4,2,88},{NOTE_F4,2,88},{NOTE_E4,4,90},{NOTE_C4,4,90},
    {NOTE_D4,4,90},{NOTE_E4,2,88},{NOTE_F4,2,88},{NOTE_E4,4,90},{NOTE_D4,4,90},
    {NOTE_C4,4,90},{NOTE_D4,4,90},{NOTE_G3,8,96},{0,2,0},
    {NOTE_E4,4,91},{NOTE_E4,4,91},{NOTE_F4,4,91},{NOTE_G4,4,91},
    {NOTE_G4,4,91},{NOTE_F4,4,91},{NOTE_E4,4,91},{NOTE_D4,4,91},
    {NOTE_C4,4,91},{NOTE_C4,4,91},{NOTE_D4,4,91},{NOTE_E4,4,91},
    {NOTE_D4,6,94},{NOTE_C4,2,86},{NOTE_C4,8,97},
    {NOTE_G3,2,82},{NOTE_C4,2,86},{NOTE_E4,2,86},{NOTE_G4,2,86},
    {NOTE_C5,8,98}
};

// The opening theme and answering phrase of Beethoven's public-domain bagatelle.
static const NoteEvent FUR_ELISE[] PROGMEM = {
    {NOTE_E5,2,84},{NOTE_DS5,2,84},{NOTE_E5,2,84},{NOTE_DS5,2,84},
    {NOTE_E5,2,86},{NOTE_B4,2,86},{NOTE_D5,2,86},{NOTE_C5,2,86},
    {NOTE_A4,5,94},{0,1,0},{NOTE_C4,2,86},{NOTE_E4,2,86},
    {NOTE_A4,2,88},{NOTE_B4,5,94},{0,1,0},{NOTE_E4,2,86},
    {NOTE_GS4,2,86},{NOTE_B4,2,88},{NOTE_C5,5,94},{0,1,0},
    {NOTE_E4,2,86},{NOTE_E5,2,84},{NOTE_DS5,2,84},{NOTE_E5,2,84},
    {NOTE_DS5,2,84},{NOTE_E5,2,86},{NOTE_B4,2,86},{NOTE_D5,2,86},
    {NOTE_C5,2,86},{NOTE_A4,5,94},{0,1,0},{NOTE_C4,2,86},
    {NOTE_E4,2,86},{NOTE_A4,2,88},{NOTE_B4,5,94},{0,1,0},
    {NOTE_E4,2,86},{NOTE_C5,2,86},{NOTE_B4,2,86},{NOTE_A4,6,96},
    {0,2,0},{NOTE_B4,2,86},{NOTE_C5,2,86},{NOTE_D5,2,86},
    {NOTE_E5,5,94},{0,1,0},{NOTE_G4,2,86},{NOTE_F5,2,86},
    {NOTE_E5,2,86},{NOTE_D5,5,94},{0,1,0},{NOTE_F4,2,86},
    {NOTE_E5,2,86},{NOTE_D5,2,86},{NOTE_C5,5,94},{0,1,0},
    {NOTE_E4,2,86},{NOTE_D5,2,86},{NOTE_C5,2,86},{NOTE_B4,5,94},
    {0,1,0},{NOTE_E4,2,86},{NOTE_E5,2,84},{NOTE_DS5,2,84},
    {NOTE_E5,2,84},{NOTE_DS5,2,84},{NOTE_E5,2,86},{NOTE_B4,2,86},
    {NOTE_D5,2,86},{NOTE_C5,2,86},{NOTE_A4,5,94},{0,1,0},
    {NOTE_C4,2,86},{NOTE_E4,2,86},{NOTE_A4,2,88},{NOTE_B4,5,94},
    {0,1,0},{NOTE_E4,2,86},{NOTE_GS4,2,86},{NOTE_B4,2,88},
    {NOTE_C5,5,94},{0,1,0},{NOTE_E4,2,86},{NOTE_C5,2,86},
    {NOTE_B4,2,86},{NOTE_A4,8,97}
};

// Christian Petzold's public-domain Minuet in G, arranged in clear 3/4 phrases.
static const NoteEvent MINUET_IN_G[] PROGMEM = {
    {NOTE_D5,4,94},{NOTE_G4,2,90},{NOTE_A4,2,90},{NOTE_B4,2,90},{NOTE_C5,2,90},
    {NOTE_D5,4,94},{NOTE_G4,4,92},{NOTE_G4,4,96},
    {NOTE_E5,4,94},{NOTE_C5,2,90},{NOTE_D5,2,90},{NOTE_E5,2,90},{NOTE_FS5,2,90},
    {NOTE_G5,4,94},{NOTE_G4,4,92},{NOTE_G4,4,96},
    {NOTE_C5,4,94},{NOTE_D5,2,90},{NOTE_C5,2,90},{NOTE_B4,2,90},{NOTE_A4,2,90},
    {NOTE_B4,4,94},{NOTE_C5,2,90},{NOTE_B4,2,90},{NOTE_A4,2,90},{NOTE_G4,2,90},
    {NOTE_FS4,4,94},{NOTE_G4,2,90},{NOTE_A4,2,90},{NOTE_B4,2,90},{NOTE_G4,2,90},
    {NOTE_B4,4,94},{NOTE_A4,8,97},{0,2,0},
    {NOTE_D5,4,94},{NOTE_G4,2,90},{NOTE_A4,2,90},{NOTE_B4,2,90},{NOTE_C5,2,90},
    {NOTE_D5,4,94},{NOTE_G4,4,92},{NOTE_G4,4,96},
    {NOTE_E5,4,94},{NOTE_C5,2,90},{NOTE_D5,2,90},{NOTE_E5,2,90},{NOTE_FS5,2,90},
    {NOTE_G5,4,94},{NOTE_G4,4,92},{NOTE_G4,4,96},
    {NOTE_C5,4,94},{NOTE_D5,2,90},{NOTE_C5,2,90},{NOTE_B4,2,90},{NOTE_A4,2,90},
    {NOTE_B4,4,94},{NOTE_C5,2,90},{NOTE_B4,2,90},{NOTE_A4,2,90},{NOTE_G4,2,90},
    {NOTE_A4,4,94},{NOTE_B4,2,90},{NOTE_A4,2,90},{NOTE_G4,2,90},{NOTE_FS4,2,90},
    {NOTE_G4,8,98}
};

// The public-domain Brahms lullaby theme with a repeated, resolved ending.
static const NoteEvent BRAHMS_LULLABY[] PROGMEM = {
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_B4,4,94},{NOTE_G4,4,94},
    {NOTE_G4,2,88},{NOTE_B4,2,88},{NOTE_E5,8,97},{0,2,0},
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_B4,4,94},{NOTE_G4,4,94},
    {NOTE_B4,2,88},{NOTE_E5,2,88},{NOTE_D5,8,97},{0,2,0},
    {NOTE_F4,2,88},{NOTE_F4,2,88},{NOTE_A4,4,94},{NOTE_F4,4,94},
    {NOTE_F4,2,88},{NOTE_A4,2,88},{NOTE_D5,6,96},{NOTE_C5,2,90},
    {NOTE_B4,4,94},{NOTE_G4,4,94},{NOTE_A4,4,94},
    {NOTE_G4,8,98},{0,2,0},
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_C5,4,94},{NOTE_B4,4,94},
    {NOTE_A4,2,88},{NOTE_A4,2,88},{NOTE_G4,8,97},{0,2,0},
    {NOTE_F4,2,88},{NOTE_G4,2,88},{NOTE_A4,4,94},{NOTE_D4,4,94},
    {NOTE_D4,2,88},{NOTE_G4,2,88},{NOTE_B4,6,96},{NOTE_A4,2,90},
    {NOTE_G4,4,94},{NOTE_D4,4,94},{NOTE_G4,4,94},
    {NOTE_B4,8,97},{0,2,0},
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_B4,4,94},{NOTE_G4,4,94},
    {NOTE_B4,2,88},{NOTE_E5,2,88},{NOTE_D5,6,96},{NOTE_C5,2,90},
    {NOTE_B4,4,94},{NOTE_A4,4,94},{NOTE_G4,4,94},
    {NOTE_G4,12,99}
};

struct Song {
    const char *title;
    const char *composer;
    const NoteEvent *events;
    uint16_t eventCount;
    uint16_t defaultBpm;
};

static const Song SONGS[] = {
    {"Ode to Joy", "L. van Beethoven", ODE_TO_JOY,
     (uint16_t)(sizeof(ODE_TO_JOY) / sizeof(ODE_TO_JOY[0])), 118},
    {"Fur Elise Theme", "L. van Beethoven", FUR_ELISE,
     (uint16_t)(sizeof(FUR_ELISE) / sizeof(FUR_ELISE[0])), 126},
    {"Minuet in G", "C. Petzold", MINUET_IN_G,
     (uint16_t)(sizeof(MINUET_IN_G) / sizeof(MINUET_IN_G[0])), 112},
    {"Brahms Lullaby", "J. Brahms", BRAHMS_LULLABY,
     (uint16_t)(sizeof(BRAHMS_LULLABY) / sizeof(BRAHMS_LULLABY[0])), 78}
};

static const uint8_t SONG_COUNT = sizeof(SONGS) / sizeof(SONGS[0]);

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::MouseCursor<Citadela::CitCompositeColorDAC, 81> appCursor;
static Citadela::LineReader controllerReader(128);
static Citadela::LineReader usbReader(128);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);

static bool videoReady = false;
static bool cursorOutlined = false;
static bool audioReady = false;
enum PlayerPage : uint8_t { PAGE_SONGS = 0, PAGE_OPTIONS = 1 };
static uint8_t currentPage = PAGE_SONGS;
static uint8_t selectedOption = 0;
static uint8_t selectedSong = 0;
static uint16_t bpm = 118;
static uint8_t volumePercent = 78;
static bool reverbEnabled = false;
static bool optionsDirty = false;
static uint32_t optionsDirtySince = 0;
static bool playing = false;
static bool paused = false;
static bool toneActive = false;
static bool releaseApplied = false;
static uint16_t eventIndex = 0;
static uint16_t currentFrequency = 0;
static uint16_t outputFrequency = 0;
static uint16_t lastPitchedFrequency = 0;
static uint8_t outputScalePercent = 0;
static uint32_t eventEndMs = 0;
static uint32_t toneOffMs = 0;
static int lastProgressWidth = -1;
static char statusText[48] = "Choose a song and press Enter";

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return videodisplay.RGB(r, g, b);
}

static void setStatus(const char *text) {
    snprintf(statusText, sizeof(statusText), "%s", text ? text : "");
}

static uint16_t volumeDuty(uint8_t scalePercent) {
    uint32_t effective = (uint32_t)volumePercent * scalePercent / 100U;
    if (!effective) return 0;
    uint32_t duty = effective * effective * 511U / 10000U;
    return (uint16_t)max(6UL, duty);
}

static void setSpeakerLevel(uint8_t scalePercent, uint16_t fadeMs) {
    if (!audioReady || !outputFrequency) return;
    uint16_t target = volumeDuty(scalePercent);
    uint16_t current = (uint16_t)min(511UL, ledcRead(SPEAKER_PIN));
    if (fadeMs) ledcFade(SPEAKER_PIN, current, target, fadeMs);
    else ledcWrite(SPEAKER_PIN, target);
    outputScalePercent = scalePercent;
    toneActive = target != 0;
}

static void speakerTone(uint16_t frequency, uint8_t scalePercent = 100) {
    if (!audioReady || !frequency || !volumePercent) {
        if (audioReady) ledcWrite(SPEAKER_PIN, 0);
        toneActive = false;
        outputFrequency = 0;
        outputScalePercent = 0;
        return;
    }
    uint16_t startDuty = toneActive ? (uint16_t)min(511UL, ledcRead(SPEAKER_PIN)) : 0;
    if (!toneActive || outputFrequency != frequency) {
        ledcChangeFrequency(SPEAKER_PIN, frequency, 10);
    }
    outputFrequency = frequency;
    uint16_t target = volumeDuty(scalePercent);
    ledcFade(SPEAKER_PIN, startDuty, target, 4);
    outputScalePercent = scalePercent;
    toneActive = target != 0;
}

static void speakerOff() {
    if (audioReady) ledcWrite(SPEAKER_PIN, 0);
    toneActive = false;
    outputFrequency = 0;
    outputScalePercent = 0;
    currentFrequency = 0;
}

static void loadAudioOptions() {
    Preferences options;
    if (!options.begin("cit_tones", true)) return;
    volumePercent = constrain((int)options.getUChar("volume", 78), 0, 100);
    reverbEnabled = options.getBool("reverb", false);
    options.end();
}

static void markAudioOptionsDirty() {
    optionsDirty = true;
    optionsDirtySince = millis();
}

static void persistAudioOptionsIfDue(bool force = false) {
    if (!optionsDirty || (!force && millis() - optionsDirtySince < 600)) return;
    Preferences options;
    if (options.begin("cit_tones", false)) {
        options.putUChar("volume", volumePercent);
        options.putBool("reverb", reverbEnabled);
        options.end();
        optionsDirty = false;
    }
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
    SPIFFS.remove("/evil.txt");
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

static void flashKernel() {
    speakerOff();
    File kernel = SD.open("/System/kernel.bin", FILE_READ);
    if (!kernel) {
        controllerVideo.forceProgress(100, "Kernel file missing");
        return;
    }
    size_t total = kernel.size();
    controllerVideo.appFlashStart(total, "Flashing kernel");
    if (!total || !Update.begin(total)) {
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

static void restartWithFallbackVideo(const char *label) {
    speakerOff();
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

static void drawSongCard(uint8_t index) {
    int x = (index & 1) ? 194 : 12;
    int y = 55 + (index >> 1) * 54;
    int w = 170;
    int h = 49;
    bool selected = selectedSong == index;
    uint32_t background = selected ? rgb(29, 98, 116) : rgb(31, 38, 43);
    uint32_t edge = selected ? rgb(91, 231, 211) : rgb(91, 111, 119);
    const uint32_t accents[] = {
        rgb(255, 196, 72), rgb(235, 104, 121),
        rgb(113, 184, 255), rgb(192, 139, 247)
    };
    videodisplay.fillRect(x, y, w, h, background);
    videodisplay.rect(x, y, w, h, edge);
    videodisplay.fillRect(x + 7, y + 7, 4, h - 14, accents[index]);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(246, 248, 247), background);
    videodisplay.setCursor(x + 18, y + 7);
    videodisplay.println(SONGS[index].title);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(178, 195, 199), background);
    videodisplay.setCursor(x + 18, y + 24);
    videodisplay.print(SONGS[index].composer);
    videodisplay.setCursor(x + 116, y + 36);
    videodisplay.print(SONGS[index].eventCount);
    videodisplay.print(" notes");
}

static void drawTabs() {
    uint32_t strip = rgb(22, 27, 31);
    videodisplay.fillRect(0, 31, SCREEN_W, 21, strip);
    for (uint8_t page = 0; page < 2; ++page) {
        int x = 12 + page * 88;
        bool selected = currentPage == page;
        uint32_t fill = selected ? rgb(52, 91, 101) : rgb(30, 37, 42);
        uint32_t edge = selected ? rgb(91, 231, 211) : rgb(80, 96, 102);
        videodisplay.fillRect(x, 33, 80, 17, fill);
        videodisplay.rect(x, 33, 80, 17, edge);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(selected ? rgb(247, 251, 250) : rgb(169, 184, 188), fill);
        videodisplay.setCursor(x + 18, 38);
        videodisplay.print(page == PAGE_SONGS ? "SONGS" : "OPTIONS");
    }
}

static void drawSongsPage() {
    videodisplay.fillRect(0, 52, SCREEN_W, 116, rgb(12, 15, 18));
    for (uint8_t index = 0; index < SONG_COUNT; ++index) drawSongCard(index);
}

static void drawVolumeControl() {
    int x = 24;
    int y = 68;
    int w = 328;
    int h = 48;
    uint32_t fill = selectedOption == 0 ? rgb(31, 58, 65) : rgb(26, 32, 36);
    uint32_t edge = selectedOption == 0 ? rgb(91, 231, 211) : rgb(80, 96, 102);
    videodisplay.fillRect(x, y, w, h, fill);
    videodisplay.rect(x, y, w, h, edge);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(239, 245, 245), fill);
    videodisplay.setCursor(x + 12, y + 8);
    videodisplay.print("VOLUME");
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(x + 274, y + 9);
    videodisplay.print(volumePercent);
    videodisplay.print('%');
    int trackX = x + 96;
    int trackY = y + 30;
    int trackW = 204;
    int loaded = trackW * volumePercent / 100;
    videodisplay.fillRect(trackX, trackY, trackW, 8, rgb(67, 78, 83));
    if (loaded) videodisplay.fillRect(trackX, trackY, loaded, 8, rgb(79, 211, 181));
    int knobX = constrain(trackX + loaded - 2, trackX, trackX + trackW - 4);
    videodisplay.fillRect(knobX, trackY - 3, 4, 14, rgb(244, 247, 246));
}

static void drawReverbControl() {
    int x = 24;
    int y = 126;
    int w = 328;
    int h = 38;
    uint32_t fill = selectedOption == 1 ? rgb(31, 58, 65) : rgb(26, 32, 36);
    uint32_t edge = selectedOption == 1 ? rgb(91, 231, 211) : rgb(80, 96, 102);
    videodisplay.fillRect(x, y, w, h, fill);
    videodisplay.rect(x, y, w, h, edge);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(239, 245, 245), fill);
    videodisplay.setCursor(x + 12, y + 11);
    videodisplay.print("REVERB");
    int toggleX = x + 244;
    uint32_t toggle = reverbEnabled ? rgb(67, 199, 164) : rgb(77, 88, 93);
    videodisplay.fillRect(toggleX, y + 9, 64, 20, toggle);
    videodisplay.rect(toggleX, y + 9, 64, 20, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(249, 251, 250), toggle);
    videodisplay.setCursor(toggleX + (reverbEnabled ? 22 : 20), y + 15);
    videodisplay.print(reverbEnabled ? "ON" : "OFF");
}

static void drawOptionsPage() {
    videodisplay.fillRect(0, 52, SCREEN_W, 116, rgb(12, 15, 18));
    drawVolumeControl();
    drawReverbControl();
}

static void drawPage() {
    if (currentPage == PAGE_SONGS) drawSongsPage();
    else drawOptionsPage();
}

static void switchPage(uint8_t page) {
    page = constrain((int)page, (int)PAGE_SONGS, (int)PAGE_OPTIONS);
    if (currentPage == page) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    currentPage = page;
    drawTabs();
    drawPage();
}

static void resetProgressBar() {
    videodisplay.fillRect(24, 175, 328, 12, rgb(17, 21, 24));
    videodisplay.rect(24, 175, 328, 12, rgb(103, 126, 134));
    lastProgressWidth = 0;
}

static void drawPlaybackStatus(bool resetBar) {
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    uint32_t background = rgb(12, 15, 18);
    if (resetBar) resetProgressBar();
    int progressWidth = playing
        ? (int)((uint32_t)eventIndex * 326U / SONGS[selectedSong].eventCount)
        : (eventIndex >= SONGS[selectedSong].eventCount ? 326 : 0);
    progressWidth = constrain(progressWidth, 0, 326);
    if (progressWidth > lastProgressWidth) {
        videodisplay.fillRect(25 + lastProgressWidth, 176,
                              progressWidth - lastProgressWidth, 10,
                              rgb(74, 215, 181));
        lastProgressWidth = progressWidth;
    }
    videodisplay.fillRect(24, 194, 328, 45, background);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(244, 246, 245), background);
    videodisplay.setCursor(24, 197);
    videodisplay.print(paused ? "PAUSED" : (playing ? "PLAYING" : "READY"));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(167, 184, 189), background);
    videodisplay.setCursor(24, 215);
    videodisplay.print(statusText);
    videodisplay.setCursor(267, 197);
    videodisplay.print(bpm);
    videodisplay.print(" BPM");
    videodisplay.setCursor(267, 215);
    if (currentFrequency) {
        videodisplay.print(currentFrequency);
        videodisplay.print(" Hz");
    } else {
        videodisplay.print("rest");
    }
}

static void drawFooterStatus() {
    uint32_t footer = rgb(29, 35, 39);
    videodisplay.fillRect(0, 258, SCREEN_W, 30, footer);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(218, 225, 226), footer);
    videodisplay.setCursor(12, 269);
    videodisplay.print("VOL ");
    videodisplay.print(volumePercent);
    videodisplay.print("%     REVERB ");
    videodisplay.print(reverbEnabled ? "ON" : "OFF");
}

static void drawApplication() {
    uint32_t background = rgb(12, 15, 18);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_W, 30, rgb(225, 235, 236));
    videodisplay.fillRect(0, 29, SCREEN_W, 2, rgb(62, 199, 181));
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(19, 26, 29), rgb(225, 235, 236));
    videodisplay.setCursor(12, 10);
    videodisplay.print("CITADELA TONE PLAYER");
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(290, 11);
    videodisplay.print("GPIO12");
    drawTabs();
    drawPage();
    resetProgressBar();
    drawPlaybackStatus(false);
    drawFooterStatus();
}

static NoteEvent readEvent(uint16_t index) {
    NoteEvent event;
    memcpy_P(&event, SONGS[selectedSong].events + index, sizeof(event));
    return event;
}

static uint32_t eventDurationMs(const NoteEvent &event) {
    uint32_t quarterMs = 60000UL / max(40, (int)bpm);
    return max(18UL, (quarterMs * event.sixteenths + 2UL) / 4UL);
}

static void beginCurrentEvent(uint32_t now) {
    if (eventIndex >= SONGS[selectedSong].eventCount) {
        speakerOff();
        playing = false;
        paused = false;
        setStatus("Arrangement complete");
        eventIndex = SONGS[selectedSong].eventCount;
        drawPlaybackStatus(false);
        return;
    }
    NoteEvent event = readEvent(eventIndex);
    uint32_t duration = eventDurationMs(event);
    eventEndMs = now + duration;
    toneOffMs = now + (duration * event.gatePercent) / 100U;
    releaseApplied = false;
    currentFrequency = event.frequency;
    if (event.frequency && audioReady) {
        lastPitchedFrequency = event.frequency;
        speakerTone(event.frequency, 100);
    } else if (reverbEnabled && lastPitchedFrequency && audioReady && volumePercent) {
        currentFrequency = 0;
        speakerTone(lastPitchedFrequency, 26);
        toneOffMs = now + (duration * 68U) / 100U;
    } else {
        speakerOff();
        releaseApplied = true;
    }
    setStatus(SONGS[selectedSong].title);
    drawPlaybackStatus(false);
}

static void stopPlayback(const char *status = "Stopped") {
    speakerOff();
    playing = false;
    paused = false;
    eventIndex = 0;
    setStatus(status);
    drawPlaybackStatus(true);
}

static void startPlayback() {
    speakerOff();
    playing = true;
    paused = false;
    eventIndex = 0;
    lastPitchedFrequency = 0;
    bpm = SONGS[selectedSong].defaultBpm;
    setStatus(SONGS[selectedSong].title);
    drawPlaybackStatus(true);
    beginCurrentEvent(millis());
}

static void togglePause() {
    if (!playing) {
        startPlayback();
        return;
    }
    paused = !paused;
    speakerOff();
    if (paused) {
        setStatus("Paused");
        drawPlaybackStatus(false);
    } else {
        setStatus(SONGS[selectedSong].title);
        beginCurrentEvent(millis());
    }
}

static void selectSong(uint8_t index) {
    index = constrain((int)index, 0, (int)SONG_COUNT - 1);
    if (selectedSong == index) return;
    if (playing || paused) stopPlayback("Song changed");
    uint8_t previous = selectedSong;
    selectedSong = index;
    bpm = SONGS[selectedSong].defaultBpm;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    if (currentPage == PAGE_SONGS) {
        drawSongCard(previous);
        drawSongCard(selectedSong);
    }
    setStatus("Press Enter to play");
    drawPlaybackStatus(true);
}

static void setSelectedOption(uint8_t option) {
    option = constrain((int)option, 0, 1);
    if (selectedOption == option) return;
    selectedOption = option;
    if (currentPage == PAGE_OPTIONS) {
        Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
        drawOptionsPage();
    }
}

static void setVolumePercent(int value) {
    uint8_t adjusted = (uint8_t)constrain(value, 0, 100);
    if (volumePercent == adjusted) return;
    volumePercent = adjusted;
    if (outputFrequency && outputScalePercent) setSpeakerLevel(outputScalePercent, 8);
    markAudioOptionsDirty();
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    if (currentPage == PAGE_OPTIONS) drawVolumeControl();
    drawFooterStatus();
}

static void toggleReverbOption() {
    reverbEnabled = !reverbEnabled;
    if (!reverbEnabled && outputFrequency && outputScalePercent < 100) {
        setSpeakerLevel(0, 10);
    }
    markAudioOptionsDirty();
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    if (currentPage == PAGE_OPTIONS) drawReverbControl();
    drawFooterStatus();
}

static void updatePlayback() {
    if (!playing || paused) return;
    uint32_t now = millis();
    if (!releaseApplied && (int32_t)(now - toneOffMs) >= 0) {
        releaseApplied = true;
        if (reverbEnabled && currentFrequency) setSpeakerLevel(28, 16);
        else setSpeakerLevel(0, 6);
    }
    if ((int32_t)(now - eventEndMs) >= 0) {
        ++eventIndex;
        beginCurrentEvent(now);
    }
}

static void onMouseHover(int x, int y, bool leftDown) {
    if (currentPage == PAGE_OPTIONS && leftDown && y >= 82 && y < 116 && x >= 120 && x <= 324) {
        setSelectedOption(0);
        setVolumePercent((x - 120) * 100 / 204);
    }
}

static void onMouseClick(int x, int y) {
    if (y >= 33 && y < 51) {
        if (x >= 12 && x < 92) switchPage(PAGE_SONGS);
        else if (x >= 100 && x < 180) switchPage(PAGE_OPTIONS);
    } else if (currentPage == PAGE_SONGS && y >= 55 && y < 158) {
        uint8_t column = x >= 188 ? 1 : 0;
        uint8_t row = y >= 109 ? 1 : 0;
        uint8_t song = row * 2 + column;
        if (song == selectedSong) startPlayback();
        else selectSong(song);
    } else if (currentPage == PAGE_OPTIONS && y >= 68 && y < 116) {
        setSelectedOption(0);
        if (x >= 120 && x <= 324) setVolumePercent((x - 120) * 100 / 204);
    } else if (currentPage == PAGE_OPTIONS && y >= 126 && y < 164) {
        setSelectedOption(1);
        toggleReverbOption();
    } else if (y >= 175 && y < 239) {
        togglePause();
    }
}

static void handleInput(String line) {
    line.trim();
    if (!line.length() || line == "rlsd") return;
    if (appCursor.HandleMouseReport(line)) return;
    if (line == "Escape") {
        persistAudioOptionsIfDue(true);
        writeBootState("trueKernel");
        restartWithFallbackVideo("Returning home");
    } else if (line == "RightGUI (Win) +") {
        persistAudioOptionsIfDue(true);
        restartWithFallbackVideo("Restarting");
    } else if (line == "Tab") {
        switchPage(currentPage == PAGE_SONGS ? PAGE_OPTIONS : PAGE_SONGS);
    } else if (line == "Space") {
        togglePause();
    } else if (line == "s" || line == "S" || line == "Backspace") {
        stopPlayback();
    } else if (currentPage == PAGE_SONGS) {
        if (line == "LeftArrow") {
            selectSong((selectedSong + SONG_COUNT - 1) % SONG_COUNT);
        } else if (line == "RightArrow") {
            selectSong((selectedSong + 1) % SONG_COUNT);
        } else if (line == "Enter") {
            startPlayback();
        } else if (line == "UpArrow") {
            bpm = min(220, (int)bpm + 4);
            setStatus("Tempo increased");
            drawPlaybackStatus(false);
        } else if (line == "DownArrow") {
            bpm = max(50, (int)bpm - 4);
            setStatus("Tempo decreased");
            drawPlaybackStatus(false);
        }
    } else {
        if (line == "UpArrow") {
            setSelectedOption(selectedOption ? selectedOption - 1 : 1);
        } else if (line == "DownArrow") {
            setSelectedOption((selectedOption + 1) % 2);
        } else if (line == "LeftArrow") {
            if (selectedOption == 0) setVolumePercent((int)volumePercent - 5);
            else toggleReverbOption();
        } else if (line == "RightArrow") {
            if (selectedOption == 0) setVolumePercent((int)volumePercent + 5);
            else toggleReverbOption();
        } else if (line == "Enter" && selectedOption == 1) {
            toggleReverbOption();
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(256000, SERIAL_8N1, 16, 17);
    if (!SPIFFS.begin(true)) return;

    String bootState = readBootState();
    if (bootState == "trueKernel") {
        writeBootState("false");
        if (mountSD()) flashKernel();
        else controllerVideo.forceProgress(100, "SD mount failed");
        return;
    }

    loadCursorConfig();
    loadAudioOptions();
    if (mountSD()) SD.end();
    SPI.end();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    videoReady = videodisplay.init(CompMode::MODEPALColor288Pmid, VIDEO_PIN, true);
    if (!videoReady) {
        Serial.println("Tone Player video allocation failed");
        return;
    }
    controllerVideo.appVideoActive(8, 35);

    audioReady = ledcAttach(SPEAKER_PIN, 440, 10);
    speakerOff();
    if (!audioReady) setStatus("Speaker PWM initialization failed");

    bpm = SONGS[selectedSong].defaultBpm;
    drawApplication();
    appCursor.Begin(videodisplay, SCREEN_W, SCREEN_H, &cursorOutlined);
    appCursor.SetCallbacks(onMouseHover, onMouseClick);
    appCursor.MoveMouseTo(SCREEN_W / 2, SCREEN_H / 2, 0);
    Serial.printf("Tone Player ready: GPIO%d, songs=%u, audio=%s\n",
                  SPEAKER_PIN, (unsigned)SONG_COUNT,
                  audioReady ? "LEDC 10-bit" : "failed");
}

void loop() {
    String line;
    while (controllerReader.poll(Serial1, line)) handleInput(line);
    while (usbReader.poll(Serial, line)) handleInput(line);
    updatePlayback();
    persistAudioOptionsIfDue();
    delay(1);
}
