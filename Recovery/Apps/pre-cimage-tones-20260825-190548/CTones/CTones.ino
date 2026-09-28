#include <Arduino.h>
#include <ESP32Video.h>
#include <Ressources/Font8x8.h>
#include <Ressources/Font6x8.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Update.h>
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
     (uint16_t)(sizeof(FUR_ELISE) / sizeof(FUR_ELISE[0])), 126}
};

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::MouseCursor<Citadela::CitCompositeColorDAC, 81> appCursor;
static Citadela::LineReader controllerReader(128);
static Citadela::LineReader usbReader(128);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);

static bool videoReady = false;
static bool cursorOutlined = false;
static bool audioReady = false;
static uint8_t selectedSong = 0;
static uint16_t bpm = 118;
static bool playing = false;
static bool paused = false;
static bool toneActive = false;
static uint16_t eventIndex = 0;
static uint16_t currentFrequency = 0;
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

static void speakerOff() {
    if (audioReady) ledcWriteTone(SPEAKER_PIN, 0);
    toneActive = false;
    currentFrequency = 0;
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
    int x = index == 0 ? 12 : 194;
    int y = 48;
    int w = 170;
    int h = 76;
    bool selected = selectedSong == index;
    uint32_t background = selected ? rgb(29, 98, 116) : rgb(31, 38, 43);
    uint32_t edge = selected ? rgb(91, 231, 211) : rgb(91, 111, 119);
    uint32_t accent = index == 0 ? rgb(255, 196, 72) : rgb(235, 104, 121);
    videodisplay.fillRect(x, y, w, h, background);
    videodisplay.rect(x, y, w, h, edge);
    videodisplay.fillRect(x + 8, y + 10, 5, 46, accent);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(246, 248, 247), background);
    videodisplay.setCursor(x + 21, y + 13);
    videodisplay.println(SONGS[index].title);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(178, 195, 199), background);
    videodisplay.setCursor(x + 21, y + 34);
    videodisplay.println(SONGS[index].composer);
    videodisplay.setCursor(x + 21, y + 51);
    videodisplay.print(SONGS[index].eventCount);
    videodisplay.println(" scored events");
}

static void resetProgressBar() {
    videodisplay.fillRect(24, 169, 328, 12, rgb(17, 21, 24));
    videodisplay.rect(24, 169, 328, 12, rgb(103, 126, 134));
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
        videodisplay.fillRect(25 + lastProgressWidth, 170,
                              progressWidth - lastProgressWidth, 10,
                              rgb(74, 215, 181));
        lastProgressWidth = progressWidth;
    }
    videodisplay.fillRect(24, 190, 328, 45, background);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(244, 246, 245), background);
    videodisplay.setCursor(24, 193);
    videodisplay.print(paused ? "PAUSED" : (playing ? "PLAYING" : "READY"));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(167, 184, 189), background);
    videodisplay.setCursor(24, 211);
    videodisplay.print(statusText);
    videodisplay.setCursor(267, 193);
    videodisplay.print(bpm);
    videodisplay.print(" BPM");
    videodisplay.setCursor(267, 211);
    if (currentFrequency) {
        videodisplay.print(currentFrequency);
        videodisplay.print(" Hz");
    } else {
        videodisplay.print("rest");
    }
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
    drawSongCard(0);
    drawSongCard(1);
    videodisplay.setTextColor(rgb(185, 198, 202), background);
    videodisplay.setCursor(24, 143);
    videodisplay.print("Enter play   Space pause   S stop   Up/Down tempo");
    resetProgressBar();
    drawPlaybackStatus(false);
    videodisplay.fillRect(0, 258, SCREEN_W, 30, rgb(29, 35, 39));
    videodisplay.setTextColor(rgb(218, 225, 226), rgb(29, 35, 39));
    videodisplay.setCursor(12, 269);
    videodisplay.print("Hardware-timed square wave   Esc returns home");
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
    currentFrequency = event.frequency;
    if (event.frequency && audioReady) {
        ledcWriteTone(SPEAKER_PIN, event.frequency);
        toneActive = true;
    } else {
        speakerOff();
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
    index = constrain(index, 0, 1);
    if (selectedSong == index) return;
    if (playing || paused) stopPlayback("Song changed");
    uint8_t previous = selectedSong;
    selectedSong = index;
    bpm = SONGS[selectedSong].defaultBpm;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    drawSongCard(previous);
    drawSongCard(selectedSong);
    setStatus("Press Enter to play");
    drawPlaybackStatus(true);
}

static void updatePlayback() {
    if (!playing || paused) return;
    uint32_t now = millis();
    if (toneActive && (int32_t)(now - toneOffMs) >= 0) speakerOff();
    if ((int32_t)(now - eventEndMs) >= 0) {
        ++eventIndex;
        beginCurrentEvent(now);
    }
}

static void onMouseHover(int x, int y, bool leftDown) {
    (void)x;
    (void)y;
    (void)leftDown;
}

static void onMouseClick(int x, int y) {
    if (y >= 48 && y < 124) {
        uint8_t song = x < 188 ? 0 : 1;
        if (song == selectedSong) startPlayback();
        else selectSong(song);
    } else if (y >= 160 && y < 238) {
        togglePause();
    }
}

static void handleInput(String line) {
    line.trim();
    if (!line.length() || line == "rlsd") return;
    if (appCursor.HandleMouseReport(line)) return;
    if (line == "Escape") {
        writeBootState("trueKernel");
        restartWithFallbackVideo("Returning home");
    } else if (line == "RightGUI (Win) +") {
        restartWithFallbackVideo("Restarting");
    } else if (line == "LeftArrow") {
        selectSong(0);
    } else if (line == "RightArrow") {
        selectSong(1);
    } else if (line == "Enter") {
        startPlayback();
    } else if (line == "Space") {
        togglePause();
    } else if (line == "s" || line == "S" || line == "Backspace") {
        stopPlayback();
    } else if (line == "UpArrow") {
        bpm = min(220, (int)bpm + 4);
        setStatus("Tempo increased");
        drawPlaybackStatus(false);
    } else if (line == "DownArrow") {
        bpm = max(50, (int)bpm - 4);
        setStatus("Tempo decreased");
        drawPlaybackStatus(false);
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
                  SPEAKER_PIN, (unsigned)(sizeof(SONGS) / sizeof(SONGS[0])),
                  audioReady ? "LEDC 10-bit" : "failed");
}

void loop() {
    String line;
    while (controllerReader.poll(Serial1, line)) handleInput(line);
    while (usbReader.poll(Serial, line)) handleInput(line);
    updatePlayback();
    delay(1);
}
