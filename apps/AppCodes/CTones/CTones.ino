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
#include "CTonesAudio.h"
#include "../../../System/Libraries/CitadelaDisplay.h"
#include "../../../System/Libraries/CitadelaMouseCursor.h"
#include "../../../System/Libraries/CitadelaSerialCommands.h"

static const int SCREEN_W = 376;
static const int SCREEN_H = 192;
static const int VIDEO_PIN = 25;
static const int SPEAKER_PIN = 12;
static const int SD_CS = 5;

static const int NOTE_C3 = 131;
static const int NOTE_D3 = 147;
static const int NOTE_E3 = 165;
static const int NOTE_F3 = 175;
static const int NOTE_FS3 = 185;
static const int NOTE_G3 = 196;
static const int NOTE_A3 = 220;
static const int NOTE_AS3 = 233;
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
static const int NOTE_AS4 = 466;
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
static const int NOTE_B5 = 988;
static const int NOTE_C6 = 1047;

struct NoteEvent {
    uint16_t frequency;
    uint8_t sixteenths;
    uint8_t gatePercent;
};

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
    {NOTE_D4,4,90},{NOTE_E4,2,88},{NOTE_F4,2,88},{NOTE_E4,4,90},
    {NOTE_C4,4,90},{NOTE_D4,4,90},{NOTE_E4,2,88},{NOTE_F4,2,88},
    {NOTE_E4,4,90},{NOTE_D4,4,90},{NOTE_C4,4,90},{NOTE_D4,4,90},
    {NOTE_G3,8,96},{0,2,0},{NOTE_E4,4,91},{NOTE_E4,4,91},
    {NOTE_F4,4,91},{NOTE_G4,4,91},{NOTE_G4,4,91},{NOTE_F4,4,91},
    {NOTE_E4,4,91},{NOTE_D4,4,91},{NOTE_C4,4,91},{NOTE_C4,4,91},
    {NOTE_D4,4,91},{NOTE_E4,4,91},{NOTE_D4,6,94},{NOTE_C4,2,86},
    {NOTE_C4,8,97},{NOTE_G3,2,82},{NOTE_C4,2,86},{NOTE_E4,2,86},
    {NOTE_G4,2,86},{NOTE_C5,8,98}
};

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

static const NoteEvent BRAHMS_LULLABY[] PROGMEM = {
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_B4,4,94},{NOTE_G4,4,94},
    {NOTE_G4,2,88},{NOTE_B4,2,88},{NOTE_E5,8,97},{0,2,0},
    {NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_B4,4,94},{NOTE_G4,4,94},
    {NOTE_B4,2,88},{NOTE_E5,2,88},{NOTE_D5,8,97},{0,2,0},
    {NOTE_F4,2,88},{NOTE_F4,2,88},{NOTE_A4,4,94},{NOTE_F4,4,94},
    {NOTE_F4,2,88},{NOTE_A4,2,88},{NOTE_D5,6,96},{NOTE_C5,2,90},
    {NOTE_B4,4,94},{NOTE_G4,4,94},{NOTE_A4,4,94},{NOTE_G4,8,98},
    {0,2,0},{NOTE_G4,2,88},{NOTE_G4,2,88},{NOTE_C5,4,94},
    {NOTE_B4,4,94},{NOTE_A4,2,88},{NOTE_A4,2,88},{NOTE_G4,8,97},
    {0,2,0},{NOTE_F4,2,88},{NOTE_G4,2,88},{NOTE_A4,4,94},
    {NOTE_D4,4,94},{NOTE_D4,2,88},{NOTE_G4,2,88},{NOTE_B4,6,96},
    {NOTE_A4,2,90},{NOTE_G4,4,94},{NOTE_D4,4,94},{NOTE_G4,4,94},
    {NOTE_B4,8,97},{0,2,0},{NOTE_G4,2,88},{NOTE_G4,2,88},
    {NOTE_B4,4,94},{NOTE_G4,4,94},{NOTE_B4,2,88},{NOTE_E5,2,88},
    {NOTE_D5,6,96},{NOTE_C5,2,90},{NOTE_B4,4,94},{NOTE_A4,4,94},
    {NOTE_G4,4,94},{NOTE_G4,12,99}
};

static const NoteEvent TWINKLE[] PROGMEM = {
    {NOTE_C4,4,92},{NOTE_C4,4,92},{NOTE_G4,4,92},{NOTE_G4,4,92},
    {NOTE_A4,4,92},{NOTE_A4,4,92},{NOTE_G4,8,97},
    {NOTE_F4,4,92},{NOTE_F4,4,92},{NOTE_E4,4,92},{NOTE_E4,4,92},
    {NOTE_D4,4,92},{NOTE_D4,4,92},{NOTE_C4,8,97},{0,2,0},
    {NOTE_G4,4,92},{NOTE_G4,4,92},{NOTE_F4,4,92},{NOTE_F4,4,92},
    {NOTE_E4,4,92},{NOTE_E4,4,92},{NOTE_D4,8,97},
    {NOTE_G4,4,92},{NOTE_G4,4,92},{NOTE_F4,4,92},{NOTE_F4,4,92},
    {NOTE_E4,4,92},{NOTE_E4,4,92},{NOTE_D4,8,97},{0,2,0},
    {NOTE_C4,4,92},{NOTE_C4,4,92},{NOTE_G4,4,92},{NOTE_G4,4,92},
    {NOTE_A4,4,92},{NOTE_A4,4,92},{NOTE_G4,8,97},
    {NOTE_F4,4,92},{NOTE_F4,4,92},{NOTE_E4,4,92},{NOTE_E4,4,92},
    {NOTE_D4,4,92},{NOTE_D4,4,92},{NOTE_C4,12,99}
};

static const NoteEvent GREENSLEEVES[] PROGMEM = {
    {NOTE_A4,4,92},{NOTE_C5,6,95},{NOTE_D5,2,88},{NOTE_E5,4,93},
    {NOTE_F5,2,88},{NOTE_E5,2,88},{NOTE_D5,6,95},{NOTE_B4,2,88},
    {NOTE_G4,4,92},{NOTE_A4,2,88},{NOTE_B4,2,88},{NOTE_C5,6,95},
    {NOTE_A4,2,88},{NOTE_A4,4,93},{NOTE_GS4,2,88},{NOTE_A4,2,88},
    {NOTE_B4,6,96},{NOTE_GS4,2,88},{NOTE_E4,8,97},{0,2,0},
    {NOTE_A4,4,92},{NOTE_C5,6,95},{NOTE_D5,2,88},{NOTE_E5,4,93},
    {NOTE_F5,2,88},{NOTE_E5,2,88},{NOTE_D5,6,95},{NOTE_B4,2,88},
    {NOTE_G4,4,92},{NOTE_A4,2,88},{NOTE_B4,2,88},{NOTE_C5,4,93},
    {NOTE_B4,2,88},{NOTE_A4,2,88},{NOTE_GS4,4,93},{NOTE_FS4,2,88},
    {NOTE_GS4,2,88},{NOTE_A4,8,98},{0,2,0},
    {NOTE_G5,8,97},{NOTE_G5,4,93},{NOTE_FS5,2,88},{NOTE_E5,2,88},
    {NOTE_D5,6,95},{NOTE_B4,2,88},{NOTE_G4,4,92},{NOTE_A4,2,88},
    {NOTE_B4,2,88},{NOTE_C5,6,95},{NOTE_A4,2,88},{NOTE_A4,4,93},
    {NOTE_GS4,2,88},{NOTE_A4,2,88},{NOTE_B4,8,97},{NOTE_E4,8,97},
    {NOTE_A4,12,99}
};

static const NoteEvent CANON_ARPEGGIO[] PROGMEM = {
    {NOTE_D4,2,88},{NOTE_A4,2,88},{NOTE_FS4,2,88},{NOTE_D5,2,88},
    {NOTE_A3,2,88},{NOTE_E4,2,88},{NOTE_CS4,2,88},{NOTE_A4,2,88},
    {NOTE_B3,2,88},{NOTE_FS4,2,88},{NOTE_D4,2,88},{NOTE_B4,2,88},
    {NOTE_FS3,2,88},{NOTE_CS4,2,88},{NOTE_A3,2,88},{NOTE_FS4,2,88},
    {NOTE_G3,2,88},{NOTE_D4,2,88},{NOTE_B3,2,88},{NOTE_G4,2,88},
    {NOTE_D4,2,88},{NOTE_A4,2,88},{NOTE_FS4,2,88},{NOTE_D5,2,88},
    {NOTE_G3,2,88},{NOTE_D4,2,88},{NOTE_B3,2,88},{NOTE_G4,2,88},
    {NOTE_A3,2,88},{NOTE_E4,2,88},{NOTE_CS4,2,88},{NOTE_A4,2,88},
    {NOTE_FS4,1,82},{NOTE_G4,1,82},{NOTE_A4,1,82},{NOTE_B4,1,82},
    {NOTE_CS5,1,82},{NOTE_D5,1,82},{NOTE_E5,1,82},{NOTE_FS5,1,82},
    {NOTE_G5,2,88},{NOTE_FS5,2,88},{NOTE_E5,2,88},{NOTE_D5,2,88},
    {NOTE_CS5,2,88},{NOTE_B4,2,88},{NOTE_A4,2,88},{NOTE_G4,2,88},
    {NOTE_FS4,1,82},{NOTE_A4,1,82},{NOTE_D5,1,82},{NOTE_A4,1,82},
    {NOTE_E4,1,82},{NOTE_A4,1,82},{NOTE_CS5,1,82},{NOTE_A4,1,82},
    {NOTE_FS4,1,82},{NOTE_B4,1,82},{NOTE_D5,1,82},{NOTE_B4,1,82},
    {NOTE_E4,1,82},{NOTE_G4,1,82},{NOTE_B4,1,82},{NOTE_G4,1,82},
    {NOTE_D4,2,88},{NOTE_FS4,2,88},{NOTE_A4,2,88},{NOTE_D5,2,88},
    {NOTE_CS5,2,88},{NOTE_A4,2,88},{NOTE_E4,2,88},{NOTE_CS4,2,88},
    {NOTE_D4,2,88},{NOTE_FS4,2,88},{NOTE_B4,2,88},{NOTE_D5,2,88},
    {NOTE_CS5,2,88},{NOTE_A4,2,88},{NOTE_E4,2,88},{NOTE_A3,2,88},
    {NOTE_D4,12,99}
};

static const NoteEvent BACH_PRELUDE[] PROGMEM = {
    {NOTE_C4,1,84},{NOTE_E4,1,84},{NOTE_G4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},
    {NOTE_G4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},{NOTE_C4,1,84},{NOTE_E4,1,84},
    {NOTE_G4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},{NOTE_G4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},
    {NOTE_D4,1,84},{NOTE_F4,1,84},{NOTE_A4,1,84},{NOTE_D5,1,84},{NOTE_F5,1,84},
    {NOTE_A4,1,84},{NOTE_D5,1,84},{NOTE_F5,1,84},{NOTE_D4,1,84},{NOTE_F4,1,84},
    {NOTE_A4,1,84},{NOTE_D5,1,84},{NOTE_F5,1,84},{NOTE_A4,1,84},{NOTE_D5,1,84},{NOTE_F5,1,84},
    {NOTE_B3,1,84},{NOTE_D4,1,84},{NOTE_G4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},
    {NOTE_G4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},{NOTE_B3,1,84},{NOTE_D4,1,84},
    {NOTE_G4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},{NOTE_G4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},
    {NOTE_C4,1,84},{NOTE_E4,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},{NOTE_C4,1,84},{NOTE_E4,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_E5,1,84},
    {NOTE_C4,1,84},{NOTE_F4,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_F5,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_F5,1,84},{NOTE_C4,1,84},{NOTE_F4,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_F5,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_F5,1,84},
    {NOTE_C4,1,84},{NOTE_FS4,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_FS5,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_FS5,1,84},{NOTE_C4,1,84},{NOTE_FS4,1,84},
    {NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_FS5,1,84},{NOTE_A4,1,84},{NOTE_C5,1,84},{NOTE_FS5,1,84},
    {NOTE_B3,1,84},{NOTE_G4,1,84},{NOTE_B4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},
    {NOTE_B4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},{NOTE_B3,1,84},{NOTE_G4,1,84},
    {NOTE_B4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},{NOTE_B4,1,84},{NOTE_D5,1,84},{NOTE_G5,1,84},
    {NOTE_C4,2,88},{NOTE_E4,2,88},{NOTE_G4,2,88},{NOTE_C5,2,88},{NOTE_E5,8,98}
};

static const NoteEvent TURKISH_MARCH[] PROGMEM = {
    {NOTE_B4,2,88},{NOTE_A4,2,88},{NOTE_GS4,2,88},{NOTE_A4,2,88},
    {NOTE_C5,4,94},{0,1,0},{NOTE_D5,2,88},{NOTE_C5,2,88},
    {NOTE_B4,2,88},{NOTE_C5,2,88},{NOTE_E5,4,94},{0,1,0},
    {NOTE_F5,2,88},{NOTE_E5,2,88},{NOTE_DS5,2,88},{NOTE_E5,2,88},
    {NOTE_B5,2,88},{NOTE_A5,2,88},{NOTE_GS5,2,88},{NOTE_A5,2,88},
    {NOTE_B5,2,88},{NOTE_A5,2,88},{NOTE_GS5,2,88},{NOTE_A5,2,88},
    {NOTE_C6,6,96},{NOTE_A5,2,88},{NOTE_C6,2,88},{NOTE_B5,2,88},
    {NOTE_A5,2,88},{NOTE_G5,2,88},{NOTE_FS5,2,88},{NOTE_E5,2,88},
    {NOTE_FS5,2,88},{NOTE_G5,2,88},{NOTE_A5,2,88},{NOTE_G5,2,88},
    {NOTE_FS5,2,88},{NOTE_E5,2,88},{NOTE_D5,2,88},{NOTE_CS5,2,88},
    {NOTE_B4,2,88},{NOTE_A4,2,88},{NOTE_GS4,2,88},{NOTE_A4,2,88},
    {NOTE_C5,4,94},{0,1,0},{NOTE_D5,2,88},{NOTE_C5,2,88},
    {NOTE_B4,2,88},{NOTE_C5,2,88},{NOTE_E5,4,94},{0,1,0},
    {NOTE_F5,2,88},{NOTE_E5,2,88},{NOTE_DS5,2,88},{NOTE_E5,2,88},
    {NOTE_A5,2,88},{NOTE_G5,2,88},{NOTE_FS5,2,88},{NOTE_E5,2,88},
    {NOTE_D5,2,88},{NOTE_CS5,2,88},{NOTE_B4,2,88},{NOTE_A4,2,88},
    {NOTE_A4,8,98}
};

struct Song {
    const char *title;
    const char *composer;
    const NoteEvent *events;
    uint16_t eventCount;
    uint16_t defaultBpm;
};

#define SONG_ENTRY(titleValue, composerValue, eventsValue, bpmValue) \
    {titleValue, composerValue, eventsValue, \
     (uint16_t)(sizeof(eventsValue) / sizeof(eventsValue[0])), bpmValue}

static const Song SONGS[] = {
    SONG_ENTRY("Ode to Joy", "Beethoven", ODE_TO_JOY, 118),
    SONG_ENTRY("Fur Elise Theme", "Beethoven", FUR_ELISE, 126),
    SONG_ENTRY("Minuet in G", "Petzold", MINUET_IN_G, 112),
    SONG_ENTRY("Brahms Lullaby", "Brahms", BRAHMS_LULLABY, 78),
    SONG_ENTRY("Twinkle Little Star", "Traditional", TWINKLE, 104),
    SONG_ENTRY("Greensleeves", "Traditional", GREENSLEEVES, 96),
    SONG_ENTRY("Canon Arpeggio", "Pachelbel", CANON_ARPEGGIO, 142),
    SONG_ENTRY("Prelude in C Run", "J. S. Bach", BACH_PRELUDE, 164),
    SONG_ENTRY("Turkish March", "W. A. Mozart", TURKISH_MARCH, 148)
};

static const uint8_t SONG_COUNT = sizeof(SONGS) / sizeof(SONGS[0]);
static const uint8_t VISIBLE_ROWS = 5;
static const uint8_t MAX_MEDIA_FILES = 24;

struct MediaEntry {
    char name[48];
    char path[96];
    uint32_t size;
    CTonesAudio::FileType type;
};

static MediaEntry mediaFiles[MAX_MEDIA_FILES];
static uint8_t mediaCount = 0;
static uint8_t selectedMedia = 0;
static uint8_t mediaScroll = 0;

static Citadela::CitCompositeColorDAC videodisplay;
static Citadela::MouseCursor<Citadela::CitCompositeColorDAC, 81> appCursor;
static Citadela::LineReader controllerReader(128);
static Citadela::LineReader usbReader(128);
static Citadela::VideoProgressSerial controllerVideo(Serial1, 120);

enum PlayerPage : uint8_t {
    PAGE_LIBRARY = 0,
    PAGE_SD_AUDIO = 1,
    PAGE_OPTIONS = 2
};

enum PlaybackKind : uint8_t {
    PLAYBACK_NONE = 0,
    PLAYBACK_SONG,
    PLAYBACK_FILE
};

static bool videoReady = false;
static bool cursorOutlined = false;
static bool audioReady = false;
static bool sdReady = false;
static uint8_t currentPage = PAGE_LIBRARY;
static uint8_t selectedOption = 0;
static uint8_t selectedSong = 0;
static uint8_t songScroll = 0;
static uint16_t bpm = 118;
static uint8_t volumePercent = 78;
static bool reverbEnabled = false;
static bool optionsDirty = false;
static uint32_t optionsDirtySince = 0;

static PlaybackKind playbackKind = PLAYBACK_NONE;
static bool playing = false;
static bool paused = false;
static bool releaseApplied = false;
static uint16_t eventIndex = 0;
static uint16_t currentFrequency = 0;
static uint32_t eventEndMs = 0;
static uint32_t toneOffMs = 0;
static uint32_t pausedAtMs = 0;
static uint32_t lastPlaybackDrawMs = 0;
static int lastProgressWidth = -1;
static CTonesAudio::Result lastFileResult = CTonesAudio::Result::IDLE;
static char statusText[58] = "Choose music and press Enter";

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return videodisplay.RGB(r, g, b);
}

static void setStatus(const char *text) {
    snprintf(statusText, sizeof(statusText), "%s", text ? text : "");
}

static void printClipped(const char *text, uint8_t maxCharacters) {
    if (!text) return;
    for (uint8_t index = 0; text[index] && index < maxCharacters; ++index) {
        videodisplay.print(text[index]);
    }
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
    CTonesAudio::Stop();
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
    CTonesAudio::End();
    appCursor.Restore();
    if (sdReady) {
        SD.end();
        sdReady = false;
    }
    SPI.end();
    Serial1.print("VDPREP 0 ");
    Serial1.println(label ? label : "Restarting");
    Serial1.flush();
    delay(140);
    if (videoReady) videodisplay.releaseVideoMemory();
    videoReady = false;
    pinMode(VIDEO_PIN, INPUT_PULLDOWN);
    Serial1.println("VDINIT");
    Serial1.flush();
    delay(45);
    ESP.restart();
}

static bool hasAudioExtension(const char *name, CTonesAudio::FileType &type) {
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    char extension[5] = {};
    for (uint8_t index = 0; dot[index] && index < 4; ++index) {
        extension[index] = (char)tolower((unsigned char)dot[index]);
    }
    if (strcmp(extension, ".wav") == 0) {
        type = CTonesAudio::FileType::WAV;
        return true;
    }
    if (strcmp(extension, ".mp3") == 0) {
        type = CTonesAudio::FileType::MP3;
        return true;
    }
    return false;
}

static void sortMediaFiles() {
    for (uint8_t index = 1; index < mediaCount; ++index) {
        MediaEntry value = mediaFiles[index];
        int position = index - 1;
        while (position >= 0 && strcasecmp(mediaFiles[position].name, value.name) > 0) {
            mediaFiles[position + 1] = mediaFiles[position];
            --position;
        }
        mediaFiles[position + 1] = value;
    }
}

static void scanMediaFiles() {
    mediaCount = 0;
    selectedMedia = 0;
    mediaScroll = 0;
    if (!sdReady) return;

    File directory = SD.open("/Music");
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        return;
    }

    File entry = directory.openNextFile();
    while (entry && mediaCount < MAX_MEDIA_FILES) {
        if (!entry.isDirectory()) {
            CTonesAudio::FileType type;
            const char *entryPath = entry.name();
            if (hasAudioExtension(entryPath, type)) {
                const char *baseName = strrchr(entryPath, '/');
                baseName = baseName ? baseName + 1 : entryPath;
                snprintf(mediaFiles[mediaCount].name,
                         sizeof(mediaFiles[mediaCount].name), "%s", baseName);
                if (entryPath[0] == '/') {
                    snprintf(mediaFiles[mediaCount].path,
                             sizeof(mediaFiles[mediaCount].path), "%s", entryPath);
                } else {
                    snprintf(mediaFiles[mediaCount].path,
                             sizeof(mediaFiles[mediaCount].path), "/Music/%s", entryPath);
                }
                mediaFiles[mediaCount].size = entry.size();
                mediaFiles[mediaCount].type = type;
                ++mediaCount;
            }
        }
        entry.close();
        entry = directory.openNextFile();
    }
    if (entry) entry.close();
    directory.close();
    sortMediaFiles();
    Serial.printf("CTones SD audio scan: %u file(s)\n", (unsigned)mediaCount);
}

static void ensureSongVisible() {
    if (selectedSong < songScroll) songScroll = selectedSong;
    if (selectedSong >= songScroll + VISIBLE_ROWS) {
        songScroll = selectedSong - VISIBLE_ROWS + 1;
    }
}

static void ensureMediaVisible() {
    if (selectedMedia < mediaScroll) mediaScroll = selectedMedia;
    if (selectedMedia >= mediaScroll + VISIBLE_ROWS) {
        mediaScroll = selectedMedia - VISIBLE_ROWS + 1;
    }
}

static void drawTabs() {
    static const char *TAB_NAMES[] = {"LIBRARY", "SD AUDIO", "OPTIONS"};
    uint32_t strip = rgb(22, 27, 31);
    videodisplay.fillRect(0, 24, SCREEN_W, 20, strip);
    for (uint8_t page = 0; page < 3; ++page) {
        int x = 12 + page * 96;
        bool selected = currentPage == page;
        uint32_t fill = selected ? rgb(43, 95, 103) : rgb(30, 37, 42);
        uint32_t edge = selected ? rgb(91, 231, 211) : rgb(80, 96, 102);
        videodisplay.fillRect(x, 26, 88, 16, fill);
        videodisplay.rect(x, 26, 88, 16, edge);
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(selected ? rgb(247, 251, 250) : rgb(169, 184, 188), fill);
        videodisplay.setCursor(x + (page == PAGE_SD_AUDIO ? 20 : 23), 31);
        videodisplay.print(TAB_NAMES[page]);
    }
}

static void drawSongRow(uint8_t songIndex, uint8_t row) {
    int y = 47 + row * 18;
    bool selected = selectedSong == songIndex;
    uint32_t fill = selected ? rgb(27, 82, 93) : rgb(24, 29, 33);
    uint32_t edge = selected ? rgb(91, 231, 211) : rgb(48, 58, 63);
    static const uint8_t accents[][3] = {
        {255,196,72},{235,104,121},{113,184,255},
        {192,139,247},{83,210,160},{255,151,89}
    };
    videodisplay.fillRect(12, y, 352, 16, fill);
    videodisplay.rect(12, y, 352, 16, edge);
    const uint8_t *accent = accents[songIndex % 6];
    videodisplay.fillRect(15, y + 3, 3, 10,
                          rgb(accent[0], accent[1], accent[2]));
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(238, 244, 244), fill);
    videodisplay.setCursor(22, y + 5);
    if (songIndex < 9) videodisplay.print('0');
    videodisplay.print(songIndex + 1);
    videodisplay.setCursor(43, y + 5);
    printClipped(SONGS[songIndex].title, 25);
    videodisplay.setTextColor(rgb(159, 181, 186), fill);
    videodisplay.setCursor(205, y + 5);
    printClipped(SONGS[songIndex].composer, 17);
    videodisplay.setCursor(337, y + 5);
    videodisplay.print(SONGS[songIndex].defaultBpm);
}

static void drawLibraryPage() {
    videodisplay.fillRect(0, 44, SCREEN_W, 94, rgb(12, 15, 18));
    ensureSongVisible();
    for (uint8_t row = 0; row < VISIBLE_ROWS; ++row) {
        uint8_t index = songScroll + row;
        if (index < SONG_COUNT) drawSongRow(index, row);
    }
    int barHeight = max(8, 88 * VISIBLE_ROWS / SONG_COUNT);
    int barY = 47 + (88 - barHeight) * songScroll /
        max(1, (int)SONG_COUNT - VISIBLE_ROWS);
    videodisplay.fillRect(369, 47, 2, 88, rgb(55, 66, 71));
    videodisplay.fillRect(369, barY, 2, barHeight, rgb(91, 231, 211));
}

static void drawMediaRow(uint8_t mediaIndex, uint8_t row) {
    int y = 47 + row * 18;
    bool selected = selectedMedia == mediaIndex;
    uint32_t fill = selected ? rgb(27, 82, 93) : rgb(24, 29, 33);
    uint32_t edge = selected ? rgb(91, 231, 211) : rgb(48, 58, 63);
    videodisplay.fillRect(12, y, 352, 16, fill);
    videodisplay.rect(12, y, 352, 16, edge);
    uint32_t badge = mediaFiles[mediaIndex].type == CTonesAudio::FileType::MP3
        ? rgb(235, 104, 121) : rgb(83, 210, 160);
    videodisplay.fillRect(17, y + 3, 24, 10, badge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(248, 250, 249), badge);
    videodisplay.setCursor(20, y + 5);
    videodisplay.print(mediaFiles[mediaIndex].type == CTonesAudio::FileType::MP3
        ? "MP3" : "WAV");
    videodisplay.setTextColor(rgb(238, 244, 244), fill);
    videodisplay.setCursor(48, y + 5);
    printClipped(mediaFiles[mediaIndex].name, 38);
    videodisplay.setTextColor(rgb(159, 181, 186), fill);
    videodisplay.setCursor(319, y + 5);
    videodisplay.print(mediaFiles[mediaIndex].size / 1024);
    videodisplay.print('K');
}

static void drawMediaPage() {
    videodisplay.fillRect(0, 44, SCREEN_W, 94, rgb(12, 15, 18));
    if (!sdReady || !mediaCount) {
        videodisplay.setFont(Font8x8);
        videodisplay.setTextColor(rgb(224, 231, 231), rgb(12, 15, 18));
        videodisplay.setCursor(28, 70);
        videodisplay.print(sdReady ? "NO WAV OR MP3 FILES IN /MUSIC" : "SD CARD NOT AVAILABLE");
        videodisplay.setFont(Font6x8);
        videodisplay.setTextColor(rgb(139, 161, 167), rgb(12, 15, 18));
        videodisplay.setCursor(28, 91);
        videodisplay.print("Press R to scan again");
        return;
    }
    ensureMediaVisible();
    for (uint8_t row = 0; row < VISIBLE_ROWS; ++row) {
        uint8_t index = mediaScroll + row;
        if (index < mediaCount) drawMediaRow(index, row);
    }
    int barHeight = min(88, max(8, 88 * VISIBLE_ROWS / mediaCount));
    int barY = 47 + (88 - barHeight) * mediaScroll /
        max(1, (int)mediaCount - VISIBLE_ROWS);
    videodisplay.fillRect(369, 47, 2, 88, rgb(55, 66, 71));
    videodisplay.fillRect(369, barY, 2, barHeight, rgb(91, 231, 211));
}

static void drawVolumeControl() {
    int y = 50;
    uint32_t fill = selectedOption == 0 ? rgb(31, 65, 72) : rgb(25, 31, 35);
    uint32_t edge = selectedOption == 0 ? rgb(91, 231, 211) : rgb(72, 87, 93);
    videodisplay.fillRect(24, y, 328, 31, fill);
    videodisplay.rect(24, y, 328, 31, edge);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(239, 245, 245), fill);
    videodisplay.setCursor(36, y + 11);
    videodisplay.print("VOLUME");
    int trackX = 134;
    int trackY = y + 12;
    int trackW = 174;
    int loaded = trackW * volumePercent / 100;
    videodisplay.fillRect(trackX, trackY, trackW, 7, rgb(67, 78, 83));
    if (loaded) videodisplay.fillRect(trackX, trackY, loaded, 7, rgb(79, 211, 181));
    int knobX = constrain(trackX + loaded - 2, trackX, trackX + trackW - 4);
    videodisplay.fillRect(knobX, trackY - 3, 4, 13, rgb(244, 247, 246));
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(316, y + 12);
    videodisplay.print(volumePercent);
    videodisplay.print('%');
}

static void drawReverbControl() {
    int y = 87;
    uint32_t fill = selectedOption == 1 ? rgb(31, 65, 72) : rgb(25, 31, 35);
    uint32_t edge = selectedOption == 1 ? rgb(91, 231, 211) : rgb(72, 87, 93);
    videodisplay.fillRect(24, y, 328, 27, fill);
    videodisplay.rect(24, y, 328, 27, edge);
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(239, 245, 245), fill);
    videodisplay.setCursor(36, y + 9);
    videodisplay.print("REVERB");
    uint32_t toggle = reverbEnabled ? rgb(67, 199, 164) : rgb(77, 88, 93);
    videodisplay.fillRect(278, y + 5, 58, 17, toggle);
    videodisplay.rect(278, y + 5, 58, 17, edge);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(249, 251, 250), toggle);
    videodisplay.setCursor(299, y + 10);
    videodisplay.print(reverbEnabled ? "ON" : "OFF");
}

static void drawOptionsPage() {
    videodisplay.fillRect(0, 44, SCREEN_W, 94, rgb(12, 15, 18));
    drawVolumeControl();
    drawReverbControl();
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(142, 169, 174), rgb(12, 15, 18));
    videodisplay.setCursor(25, 125);
    videodisplay.print("22.05 kHz audio  |  312.5 kHz PWM carrier  |  GPIO12");
}

static void drawPage() {
    if (currentPage == PAGE_LIBRARY) drawLibraryPage();
    else if (currentPage == PAGE_SD_AUDIO) drawMediaPage();
    else drawOptionsPage();
}

static void switchPage(uint8_t page) {
    page = constrain((int)page, (int)PAGE_LIBRARY, (int)PAGE_OPTIONS);
    if (currentPage == page) return;
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    currentPage = page;
    drawTabs();
    drawPage();
}

static int playbackProgressWidth() {
    if (playbackKind == PLAYBACK_SONG) {
        uint16_t count = SONGS[selectedSong].eventCount;
        if (!count) return 0;
        return constrain((int)((uint32_t)eventIndex * 350U / count), 0, 350);
    }
    if (playbackKind == PLAYBACK_FILE) {
        CTonesAudio::Snapshot snapshot = CTonesAudio::GetSnapshot();
        if (!snapshot.totalBytes) return 0;
        return constrain((int)((uint64_t)snapshot.bytesProcessed * 350ULL /
                               snapshot.totalBytes), 0, 350);
    }
    return 0;
}

static void drawPlaybackStatus(bool force = false) {
    int progressWidth = playbackProgressWidth();
    if (!force && progressWidth == lastProgressWidth &&
        millis() - lastPlaybackDrawMs < 350) return;

    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    uint32_t panel = rgb(17, 21, 24);
    videodisplay.fillRect(0, 138, SCREEN_W, 38, panel);
    videodisplay.fillRect(12, 141, 352, 7, rgb(50, 61, 66));
    if (progressWidth) {
        videodisplay.fillRect(13, 142, progressWidth, 5, rgb(74, 215, 181));
    }
    videodisplay.rect(12, 141, 352, 7, rgb(103, 126, 134));

    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(240, 245, 244), panel);
    videodisplay.setCursor(12, 153);
    videodisplay.print(paused ? "PAUSED" : (playing ? "PLAYING" : "READY"));
    videodisplay.setCursor(64, 153);
    printClipped(statusText, 36);

    videodisplay.setTextColor(rgb(153, 178, 183), panel);
    videodisplay.setCursor(12, 165);
    if (playbackKind == PLAYBACK_SONG) {
        videodisplay.print(bpm);
        videodisplay.print(" BPM   ");
        if (currentFrequency) {
            videodisplay.print(currentFrequency);
            videodisplay.print(" Hz");
        } else {
            videodisplay.print("rest");
        }
    } else if (playbackKind == PLAYBACK_FILE) {
        CTonesAudio::Snapshot snapshot = CTonesAudio::GetSnapshot();
        if (snapshot.sourceSampleRate) {
            videodisplay.print(snapshot.sourceSampleRate);
            videodisplay.print(" Hz   ");
            videodisplay.print(snapshot.channels);
            videodisplay.print(" ch / ");
            videodisplay.print(snapshot.bitsPerSample);
            videodisplay.print(" bit");
        } else {
            videodisplay.print("Reading stream headers...");
        }
        if (snapshot.underruns) {
            videodisplay.setCursor(303, 165);
            videodisplay.print("U:");
            videodisplay.print(snapshot.underruns);
        }
    } else {
        videodisplay.print("Sine synthesis and SD audio ready");
    }
    lastProgressWidth = progressWidth;
    lastPlaybackDrawMs = millis();
}

static void drawFooter() {
    uint32_t footer = rgb(29, 35, 39);
    videodisplay.fillRect(0, 176, SCREEN_W, 16, footer);
    videodisplay.setFont(Font6x8);
    videodisplay.setTextColor(rgb(211, 223, 224), footer);
    videodisplay.setCursor(10, 181);
    videodisplay.print("Arrows select  Enter play  Space pause    VOL ");
    videodisplay.print(volumePercent);
    videodisplay.print("  R:");
    videodisplay.print(reverbEnabled ? "ON" : "OFF");
}

static void drawApplication() {
    uint32_t background = rgb(12, 15, 18);
    videodisplay.clear(background);
    videodisplay.fillRect(0, 0, SCREEN_W, 24, rgb(225, 235, 236));
    videodisplay.fillRect(0, 22, SCREEN_W, 2, rgb(62, 199, 181));
    videodisplay.setFont(Font8x8);
    videodisplay.setTextColor(rgb(19, 26, 29), rgb(225, 235, 236));
    videodisplay.setCursor(12, 8);
    videodisplay.print("CITADELA AUDIO PLAYER");
    videodisplay.setFont(Font6x8);
    videodisplay.setCursor(300, 9);
    videodisplay.print("PWM / SD");
    drawTabs();
    drawPage();
    drawPlaybackStatus(true);
    drawFooter();
}

static NoteEvent readEvent(uint16_t index) {
    NoteEvent event;
    memcpy_P(&event, SONGS[selectedSong].events + index, sizeof(event));
    return event;
}

static uint32_t eventDurationMs(const NoteEvent &event) {
    uint32_t quarterMs = 60000UL / max(40, (int)bpm);
    return max(12UL, (quarterMs * event.sixteenths + 2UL) / 4UL);
}

static void beginCurrentEvent(uint32_t now) {
    if (eventIndex >= SONGS[selectedSong].eventCount) {
        CTonesAudio::Stop();
        playing = false;
        paused = false;
        currentFrequency = 0;
        setStatus("Arrangement complete");
        eventIndex = SONGS[selectedSong].eventCount;
        drawPlaybackStatus(true);
        return;
    }

    NoteEvent event = readEvent(eventIndex);
    uint32_t duration = eventDurationMs(event);
    eventEndMs = now + duration;
    toneOffMs = now + (duration * event.gatePercent) / 100U;
    releaseApplied = false;
    currentFrequency = event.frequency;
    if (event.frequency) CTonesAudio::Tone(event.frequency, 100);
    else {
        CTonesAudio::Silence();
        releaseApplied = true;
    }
}

static void stopPlayback(const char *message = "Stopped") {
    CTonesAudio::Stop();
    playing = false;
    paused = false;
    eventIndex = 0;
    currentFrequency = 0;
    playbackKind = PLAYBACK_NONE;
    setStatus(message);
    drawPlaybackStatus(true);
}

static void startSongPlayback() {
    CTonesAudio::BeginSynth();
    playbackKind = PLAYBACK_SONG;
    playing = true;
    paused = false;
    eventIndex = 0;
    currentFrequency = 0;
    bpm = SONGS[selectedSong].defaultBpm;
    setStatus(SONGS[selectedSong].title);
    lastProgressWidth = -1;
    beginCurrentEvent(millis());
    drawPlaybackStatus(true);
}

static void startMediaPlayback() {
    if (!sdReady || !mediaCount) {
        setStatus(sdReady ? "No audio files found" : "SD card unavailable");
        drawPlaybackStatus(true);
        return;
    }
    stopPlayback("Preparing SD audio");
    if (!CTonesAudio::StartFile(mediaFiles[selectedMedia].path,
                                mediaFiles[selectedMedia].type)) {
        setStatus("Audio decoder is busy");
        drawPlaybackStatus(true);
        return;
    }
    playbackKind = PLAYBACK_FILE;
    playing = true;
    paused = false;
    lastFileResult = CTonesAudio::Result::BUFFERING;
    setStatus(mediaFiles[selectedMedia].name);
    drawPlaybackStatus(true);
}

static void togglePause() {
    if (!playing) {
        if (currentPage == PAGE_SD_AUDIO) startMediaPlayback();
        else startSongPlayback();
        return;
    }
    paused = !paused;
    CTonesAudio::SetPaused(paused);
    uint32_t now = millis();
    if (paused) {
        pausedAtMs = now;
        setStatus("Paused");
    } else {
        if (playbackKind == PLAYBACK_SONG) {
            uint32_t pauseDuration = now - pausedAtMs;
            eventEndMs += pauseDuration;
            toneOffMs += pauseDuration;
            setStatus(SONGS[selectedSong].title);
        } else {
            setStatus(mediaFiles[selectedMedia].name);
        }
    }
    drawPlaybackStatus(true);
}

static void selectSong(uint8_t index) {
    index = constrain((int)index, 0, (int)SONG_COUNT - 1);
    if (selectedSong == index) return;
    if (playing) stopPlayback("Selection changed");
    uint8_t previous = selectedSong;
    uint8_t previousScroll = songScroll;
    selectedSong = index;
    bpm = SONGS[selectedSong].defaultBpm;
    ensureSongVisible();
    setStatus("Press Enter to play");
    {
        Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
        if (currentPage == PAGE_LIBRARY) {
            if (previousScroll != songScroll) drawLibraryPage();
            else {
                drawSongRow(previous, previous - songScroll);
                drawSongRow(selectedSong, selectedSong - songScroll);
            }
        }
    }
    drawPlaybackStatus(true);
}

static void selectMedia(uint8_t index) {
    if (!mediaCount) return;
    index = constrain((int)index, 0, (int)mediaCount - 1);
    if (selectedMedia == index) return;
    if (playing) stopPlayback("Selection changed");
    uint8_t previous = selectedMedia;
    uint8_t previousScroll = mediaScroll;
    selectedMedia = index;
    ensureMediaVisible();
    setStatus("Press Enter to play from SD");
    {
        Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
        if (currentPage == PAGE_SD_AUDIO) {
            if (previousScroll != mediaScroll) drawMediaPage();
            else {
                drawMediaRow(previous, previous - mediaScroll);
                drawMediaRow(selectedMedia, selectedMedia - mediaScroll);
            }
        }
    }
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
    CTonesAudio::SetVolume(volumePercent);
    markAudioOptionsDirty();
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    if (currentPage == PAGE_OPTIONS) drawVolumeControl();
    drawFooter();
}

static void toggleReverbOption() {
    reverbEnabled = !reverbEnabled;
    CTonesAudio::SetReverb(reverbEnabled);
    markAudioOptionsDirty();
    Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
    if (currentPage == PAGE_OPTIONS) drawReverbControl();
    drawFooter();
}

static void updateSongPlayback() {
    if (!playing || paused || playbackKind != PLAYBACK_SONG) return;
    uint32_t now = millis();
    if (!releaseApplied && (int32_t)(now - toneOffMs) >= 0) {
        releaseApplied = true;
        CTonesAudio::Silence();
    }
    if ((int32_t)(now - eventEndMs) >= 0) {
        ++eventIndex;
        beginCurrentEvent(now);
    }
}

static void updateFilePlayback() {
    if (playbackKind != PLAYBACK_FILE) return;
    CTonesAudio::Snapshot snapshot = CTonesAudio::GetSnapshot();
    if (snapshot.result != lastFileResult) {
        lastFileResult = snapshot.result;
        if (snapshot.result == CTonesAudio::Result::BUFFERING) {
            setStatus("Buffering SD audio...");
        } else if (snapshot.result == CTonesAudio::Result::PLAYING) {
            setStatus(mediaFiles[selectedMedia].name);
        } else if (snapshot.result == CTonesAudio::Result::FINISHED) {
            playing = false;
            paused = false;
            setStatus("Playback complete");
        } else if (snapshot.result == CTonesAudio::Result::OPEN_FAILED) {
            playing = false;
            setStatus("Could not open audio file");
        } else if (snapshot.result == CTonesAudio::Result::UNSUPPORTED_FORMAT) {
            playing = false;
            setStatus(mediaFiles[selectedMedia].type == CTonesAudio::FileType::MP3
                ? "Unsupported MP3 stream" : "Unsupported WAV encoding");
        } else if (snapshot.result == CTonesAudio::Result::DECODE_FAILED) {
            playing = false;
            setStatus("Audio decode failed");
        }
        drawPlaybackStatus(true);
    }
}

static void rescanMedia() {
    if (playing) stopPlayback("Scanning /Music");
    if (!sdReady) sdReady = mountSD();
    scanMediaFiles();
    setStatus(mediaCount ? "SD audio library refreshed" : "No SD audio files found");
    {
        Citadela::CursorDrawGuard<decltype(appCursor)> guard(appCursor);
        if (currentPage == PAGE_SD_AUDIO) drawMediaPage();
    }
    drawPlaybackStatus(true);
}

static void onMouseHover(int x, int y, bool leftDown) {
    if (currentPage == PAGE_OPTIONS && leftDown &&
        y >= 50 && y < 81 && x >= 134 && x <= 308) {
        setSelectedOption(0);
        setVolumePercent((x - 134) * 100 / 174);
    }
}

static void onMouseClick(int x, int y) {
    if (y >= 26 && y < 43) {
        for (uint8_t page = 0; page < 3; ++page) {
            int tabX = 12 + page * 96;
            if (x >= tabX && x < tabX + 88) switchPage(page);
        }
        return;
    }

    if (y >= 47 && y < 137) {
        uint8_t row = (y - 47) / 18;
        if (currentPage == PAGE_LIBRARY) {
            uint8_t index = songScroll + row;
            if (index < SONG_COUNT) {
                if (index == selectedSong) startSongPlayback();
                else selectSong(index);
            }
        } else if (currentPage == PAGE_SD_AUDIO) {
            uint8_t index = mediaScroll + row;
            if (index < mediaCount) {
                if (index == selectedMedia) startMediaPlayback();
                else selectMedia(index);
            }
        }
        return;
    }

    if (currentPage == PAGE_OPTIONS && y >= 50 && y < 81) {
        setSelectedOption(0);
        if (x >= 134 && x <= 308) setVolumePercent((x - 134) * 100 / 174);
    } else if (currentPage == PAGE_OPTIONS && y >= 87 && y < 114) {
        setSelectedOption(1);
        toggleReverbOption();
    } else if (y >= 138 && y < 176) {
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
        switchPage((currentPage + 1) % 3);
    } else if (line == "Space") {
        togglePause();
    } else if (line == "s" || line == "S" || line == "Backspace") {
        stopPlayback();
    } else if ((line == "r" || line == "R") && currentPage == PAGE_SD_AUDIO) {
        rescanMedia();
    } else if (currentPage == PAGE_LIBRARY) {
        if (line == "UpArrow") {
            selectSong((selectedSong + SONG_COUNT - 1) % SONG_COUNT);
        } else if (line == "DownArrow") {
            selectSong((selectedSong + 1) % SONG_COUNT);
        } else if (line == "LeftArrow") {
            bpm = max(50, (int)bpm - 4);
            setStatus("Tempo decreased");
            drawPlaybackStatus(true);
        } else if (line == "RightArrow") {
            bpm = min(220, (int)bpm + 4);
            setStatus("Tempo increased");
            drawPlaybackStatus(true);
        } else if (line == "Enter") {
            startSongPlayback();
        }
    } else if (currentPage == PAGE_SD_AUDIO) {
        if (line == "UpArrow" && mediaCount) {
            selectMedia((selectedMedia + mediaCount - 1) % mediaCount);
        } else if (line == "DownArrow" && mediaCount) {
            selectMedia((selectedMedia + 1) % mediaCount);
        } else if (line == "Enter") {
            startMediaPlayback();
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

    videoReady = videodisplay.init(CompMode::MODEPAL576Idiv3, VIDEO_PIN, true);
    if (!videoReady) {
        Serial.printf("CTones video allocation failed, heap=%u largest=%u\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        return;
    }
    controllerVideo.appVideoActive(8, 35);

    // Reserve the decoder stack before SD creates its working buffers. This
    // keeps MP3 playback reliable even when CVBS has already claimed DMA RAM.
    audioReady = CTonesAudio::Begin(SPEAKER_PIN);
    CTonesAudio::SetVolume(volumePercent);
    CTonesAudio::SetReverb(reverbEnabled);
    if (!audioReady) setStatus("Speaker PWM initialization failed");

    sdReady = mountSD();
    if (sdReady) scanMediaFiles();

    bpm = SONGS[selectedSong].defaultBpm;
    drawApplication();
    appCursor.Begin(videodisplay, SCREEN_W, SCREEN_H, &cursorOutlined);
    appCursor.SetCallbacks(onMouseHover, onMouseClick);
    appCursor.MoveMouseTo(SCREEN_W / 2, SCREEN_H / 2, 0);

    Serial.printf(
        "CTones ready: mode=PAL576Idiv3 %dx%d, heap=%u, largest=%u, "
        "audio=%s %luHz/%luHz, SD=%s, songs=%u, media=%u\n",
        SCREEN_W, SCREEN_H, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
        audioReady ? "PWM" : "failed",
        (unsigned long)CTonesAudio::SampleRate(),
        (unsigned long)CTonesAudio::CarrierRate(),
        sdReady ? "mounted" : "failed", (unsigned)SONG_COUNT,
        (unsigned)mediaCount);
}

void loop() {
    String line;
    while (controllerReader.poll(Serial1, line)) handleInput(line);
    while (usbReader.poll(Serial, line)) handleInput(line);
    updateSongPlayback();
    updateFilePlayback();
    if (playing && millis() - lastPlaybackDrawMs >= 250) drawPlaybackStatus();
    persistAudioOptionsIfDue();
    delay(1);
}
