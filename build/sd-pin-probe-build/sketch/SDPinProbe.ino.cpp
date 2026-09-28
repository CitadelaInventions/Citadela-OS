#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

struct SDLayout {
    const char *name;
    uint8_t miso;
    uint8_t mosi;
    uint8_t cs;
};

static const uint8_t SD_SCK = 18;
static const SDLayout layouts[] = {
    {"schematic", 19, 23, 5},
    {"old-bootloader", 5, 19, 23},
    {"perm-1", 19, 5, 23},
    {"perm-2", 23, 19, 5},
    {"perm-3", 5, 23, 19},
    {"perm-4", 23, 5, 19},
};

#line 22 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
static void releaseBus();
#line 31 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
static bool tryLayout(const SDLayout &layout, uint32_t frequency);
#line 57 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
void setup();
#line 79 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
void loop();
#line 22 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDPinProbe\\SDPinProbe.ino"
static void releaseBus() {
    SD.end();
    SPI.end();
    for (uint8_t pin : {uint8_t(5), uint8_t(18), uint8_t(19), uint8_t(23)}) {
        pinMode(pin, INPUT);
    }
    delay(30);
}

static bool tryLayout(const SDLayout &layout, uint32_t frequency) {
    releaseBus();
    pinMode(layout.cs, OUTPUT);
    digitalWrite(layout.cs, HIGH);
    SPI.begin(SD_SCK, layout.miso, layout.mosi, layout.cs);
    delay(20);

    Serial.printf("TRY %-14s SCK=%u MISO=%u MOSI=%u CS=%u @ %lu Hz: ",
                  layout.name, SD_SCK, layout.miso, layout.mosi, layout.cs,
                  (unsigned long)frequency);
    bool mounted = SD.begin(layout.cs, SPI, frequency);
    if (!mounted) {
        Serial.println("NO RESPONSE");
        return false;
    }

    uint8_t type = SD.cardType();
    Serial.printf("MOUNTED type=%u size=%llu MB\n",
                  (unsigned)type,
                  (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
    File root = SD.open("/");
    Serial.println(root ? "ROOT OPEN OK" : "ROOT OPEN FAILED");
    if (root) root.close();
    return type != CARD_NONE;
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.printf("SD pin probe. Free heap=%u\n", (unsigned)ESP.getFreeHeap());

    for (uint8_t pin : {uint8_t(5), uint8_t(18), uint8_t(19), uint8_t(23)}) {
        pinMode(pin, INPUT);
        delay(1);
        Serial.printf("IDLE GPIO%u=%d\n", pin, digitalRead(pin));
    }

    bool found = false;
    for (const SDLayout &layout : layouts) {
        if (tryLayout(layout, 400000)) {
            Serial.printf("FOUND %s\n", layout.name);
            found = true;
            break;
        }
    }
    if (!found) Serial.println("NO PIN LAYOUT RESPONDED");
}

void loop() {
    delay(1000);
}

