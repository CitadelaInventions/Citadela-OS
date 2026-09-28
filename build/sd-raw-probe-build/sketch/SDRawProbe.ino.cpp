#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDRawProbe\\SDRawProbe.ino"
#include <Arduino.h>
#include <SPI.h>

struct SDLayout {
    const char *name;
    uint8_t miso;
    uint8_t mosi;
    uint8_t cs;
};

static const SDLayout layouts[] = {
    {"schematic", 19, 23, 5},
    {"old-bootloader", 5, 19, 23},
};

#line 16 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDRawProbe\\SDRawProbe.ino"
static void rawCMD0(const SDLayout &layout);
#line 38 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDRawProbe\\SDRawProbe.ino"
void setup();
#line 45 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDRawProbe\\SDRawProbe.ino"
void loop();
#line 16 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\build\\SDRawProbe\\SDRawProbe.ino"
static void rawCMD0(const SDLayout &layout) {
    SPI.end();
    for (uint8_t pin : {uint8_t(5), uint8_t(18), uint8_t(19), uint8_t(23)}) pinMode(pin, INPUT);
    delay(20);
    pinMode(layout.cs, OUTPUT);
    digitalWrite(layout.cs, HIGH);
    SPI.begin(18, layout.miso, layout.mosi, layout.cs);
    SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));

    for (int i = 0; i < 12; ++i) SPI.transfer(0xFF);
    digitalWrite(layout.cs, LOW);
    const uint8_t command[] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    for (uint8_t value : command) SPI.transfer(value);

    Serial.printf("RAW %-14s:", layout.name);
    for (int i = 0; i < 24; ++i) Serial.printf(" %02X", SPI.transfer(0xFF));
    Serial.println();
    digitalWrite(layout.cs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
}

void setup() {
    Serial.begin(115200);
    delay(800);
    for (const SDLayout &layout : layouts) rawCMD0(layout);
    Serial.println("RAW PROBE COMPLETE");
}

void loop() {
    delay(1000);
}

