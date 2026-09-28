#pragma once

#include <Arduino.h>

namespace Citadela {

class DS1302RTC {
  public:
    struct DateTime {
        uint16_t year = 2000;
        uint8_t month = 1;
        uint8_t day = 1;
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        uint8_t weekday = 1;
    };

    DS1302RTC(uint8_t resetPin, uint8_t dataPin, uint8_t clockPin)
        : resetPin_(resetPin), dataPin_(dataPin), clockPin_(clockPin) {}

    void begin() {
        pinMode(resetPin_, OUTPUT);
        pinMode(clockPin_, OUTPUT);
        pinMode(dataPin_, INPUT);
        digitalWrite(resetPin_, LOW);
        digitalWrite(clockPin_, LOW);
    }

    bool read(DateTime &dateTime) {
        uint8_t raw[8] = {};
        beginTransaction();
        writeByte(0xBF);
        pinMode(dataPin_, INPUT);
        for (uint8_t i = 0; i < sizeof(raw); ++i) raw[i] = readByte();
        endTransaction();

        if ((raw[0] & 0x80U) != 0 || raw[0] == 0xFFU) return false;

        dateTime.second = fromBCD(raw[0] & 0x7FU);
        dateTime.minute = fromBCD(raw[1] & 0x7FU);
        if ((raw[2] & 0x80U) != 0) {
            uint8_t hour12 = fromBCD(raw[2] & 0x1FU);
            bool pm = (raw[2] & 0x20U) != 0;
            dateTime.hour = hour12 % 12U + (pm ? 12U : 0U);
        } else {
            dateTime.hour = fromBCD(raw[2] & 0x3FU);
        }
        dateTime.day = fromBCD(raw[3] & 0x3FU);
        dateTime.month = fromBCD(raw[4] & 0x1FU);
        dateTime.weekday = fromBCD(raw[5] & 0x07U);
        dateTime.year = 2000U + fromBCD(raw[6]);
        return valid(dateTime);
    }

    bool write(const DateTime &dateTime) {
        if (!valid(dateTime)) return false;

        writeRegister(0x8E, 0x00);
        beginTransaction();
        writeByte(0xBE);  // Clock burst write.
        writeByte(toBCD(dateTime.second) & 0x7FU);
        writeByte(toBCD(dateTime.minute));
        writeByte(toBCD(dateTime.hour));
        writeByte(toBCD(dateTime.day));
        writeByte(toBCD(dateTime.month));
        writeByte(toBCD(dateTime.weekday));
        writeByte(toBCD((uint8_t)(dateTime.year - 2000U)));
        writeByte(0x00);
        endTransaction();
        writeRegister(0x8E, 0x80);

        DateTime check;
        return read(check);
    }

    bool stop() {
        uint8_t seconds = readRegister(0x81);
        if (seconds == 0xFFU) return false;
        writeRegister(0x8E, 0x00);
        writeRegister(0x80, (seconds & 0x7FU) | 0x80U);
        writeRegister(0x8E, 0x80);
        return true;
    }

    static bool valid(const DateTime &dateTime) {
        if (dateTime.year < 2020 || dateTime.year > 2099) return false;
        if (dateTime.month < 1 || dateTime.month > 12) return false;
        if (dateTime.day < 1 || dateTime.day > daysInMonth(dateTime.year, dateTime.month)) return false;
        if (dateTime.hour > 23 || dateTime.minute > 59 || dateTime.second > 59) return false;
        return dateTime.weekday >= 1 && dateTime.weekday <= 7;
    }

  private:
    uint8_t resetPin_;
    uint8_t dataPin_;
    uint8_t clockPin_;

    static uint8_t toBCD(uint8_t value) {
        return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
    }

    static uint8_t fromBCD(uint8_t value) {
        return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
    }

    static bool leapYear(uint16_t year) {
        return ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
    }

    static uint8_t daysInMonth(uint16_t year, uint8_t month) {
        static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && leapYear(year)) return 29;
        return days[month - 1U];
    }

    void beginTransaction() {
        digitalWrite(clockPin_, LOW);
        pinMode(dataPin_, OUTPUT);
        digitalWrite(resetPin_, HIGH);
        delayMicroseconds(4);
    }

    void endTransaction() {
        digitalWrite(resetPin_, LOW);
        digitalWrite(clockPin_, LOW);
        pinMode(dataPin_, INPUT);
        delayMicroseconds(4);
    }

    void writeByte(uint8_t value) {
        pinMode(dataPin_, OUTPUT);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            digitalWrite(dataPin_, (value >> bit) & 0x01U);
            delayMicroseconds(1);
            digitalWrite(clockPin_, HIGH);
            delayMicroseconds(1);
            digitalWrite(clockPin_, LOW);
        }
    }

    uint8_t readByte() {
        uint8_t value = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (digitalRead(dataPin_)) value |= (uint8_t)(1U << bit);
            digitalWrite(clockPin_, HIGH);
            delayMicroseconds(1);
            digitalWrite(clockPin_, LOW);
            delayMicroseconds(1);
        }
        return value;
    }

    uint8_t readRegister(uint8_t command) {
        beginTransaction();
        writeByte(command | 0x01U);
        pinMode(dataPin_, INPUT);
        uint8_t value = readByte();
        endTransaction();
        return value;
    }

    void writeRegister(uint8_t command, uint8_t value) {
        beginTransaction();
        writeByte(command & 0xFEU);
        writeByte(value);
        endTransaction();
    }
};

}
