#pragma once

#include <Arduino.h>
#include <SPI.h>

namespace Citadela {
namespace BoardPins {

static constexpr uint8_t SdClock = 18;
static constexpr uint8_t SdMiso = 19;
static constexpr uint8_t SdMosi = 23;
static constexpr uint8_t SdChipSelect = 5;

static constexpr uint8_t RtcReset = 27;
static constexpr uint8_t RtcData = 13;
static constexpr uint8_t RtcClock = 14;
static constexpr uint8_t FanControl = 33;

inline void beginSDCardSPI(SPIClass &spi) {
    pinMode(SdChipSelect, OUTPUT);
    digitalWrite(SdChipSelect, HIGH);
    spi.begin(SdClock, SdMiso, SdMosi, SdChipSelect);
}

}
}
