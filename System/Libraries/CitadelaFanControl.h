#pragma once

#include <Arduino.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

namespace Citadela {

class FanControl {
  public:
    explicit FanControl(uint8_t pin, bool activeHigh = true)
        : pin_(pin), activeHigh_(activeHigh) {}

    void begin(uint8_t percent = 100) {
        pinMode(pin_, OUTPUT);
        started_ = true;
        setPercent(percent);
    }

    void setPercent(uint8_t percent) {
        percent_ = constrain(percent, (uint8_t)0, (uint8_t)100);
        if (!started_) return;

        if (percent_ == 0 || percent_ == 100) {
            detachPWM();
            pinMode(pin_, OUTPUT);
            bool on = percent_ == 100;
            digitalWrite(pin_, (on == activeHigh_) ? HIGH : LOW);
            return;
        }

        attachPWM();
        uint8_t duty = (uint8_t)((percent_ * 255U + 50U) / 100U);
        if (!activeHigh_) duty = 255U - duty;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(pin_, duty);
#else
        ledcWrite(PwmChannel, duty);
#endif
    }

    uint8_t percent() const { return percent_; }

  private:
    static constexpr uint8_t PwmChannel = 7;
    static constexpr uint32_t PwmFrequency = 25000;
    static constexpr uint8_t PwmResolution = 8;

    uint8_t pin_;
    uint8_t percent_ = 100;
    bool activeHigh_ = true;
    bool started_ = false;
    bool pwmAttached_ = false;

    void attachPWM() {
        if (pwmAttached_) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        pwmAttached_ = ledcAttach(pin_, PwmFrequency, PwmResolution);
#else
        ledcSetup(PwmChannel, PwmFrequency, PwmResolution);
        ledcAttachPin(pin_, PwmChannel);
        pwmAttached_ = true;
#endif
    }

    void detachPWM() {
        if (!pwmAttached_) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcDetach(pin_);
#else
        ledcDetachPin(pin_);
#endif
        pwmAttached_ = false;
    }
};

}
