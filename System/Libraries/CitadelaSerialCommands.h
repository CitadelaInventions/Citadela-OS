#pragma once

#include <Arduino.h>

namespace Citadela {

class VideoProgressSerial {
  public:
    VideoProgressSerial() = default;
    explicit VideoProgressSerial(HardwareSerial &serialPort, uint16_t throttleMs = 250)
        : port(&serialPort), throttle(throttleMs) {}

    void begin(HardwareSerial &serialPort, uint16_t throttleMs = 250) {
        port = &serialPort;
        throttle = throttleMs;
        active = false;
        lastPercent = -1;
        lastEmitMs = 0;
    }

    bool isActive() const {
        return active;
    }

    int currentPercent() const {
        return lastPercent;
    }

    void markActive() {
        active = true;
        lastPercent = -1;
        lastEmitMs = 0;
    }

    void start(const char *label = "Preparing Citadela") {
        if (!port) return;
        if (!active) {
            markActive();
            port->println("VDINIT");
            port->flush();
        }
        progress(lastPercent >= 0 ? lastPercent : 0, label);
    }

    void prepare(const char *label = "Preparing Citadela", int percent = 0) {
        if (!port) return;
        active = true;
        lastPercent = -1;
        port->print("VDPREP ");
        port->print(constrain(percent, 0, 100));
        port->print(' ');
        port->println(label ? label : "Preparing Citadela");
        port->flush();
    }

    void progress(int percent, const char *label = "Preparing Citadela") {
        if (!active || !port) return;
        emitProgress(percent, label);
    }

    void forceProgress(int percent, const char *label = "Preparing Citadela") {
        if (!port) return;
        if (!active) markActive();
        emitProgress(percent, label);
    }

    void releaseKernel() {
        if (!active || !port) return;
        port->println("VDKERNEL");
        port->println("VDDONE");
        port->flush();
        active = false;
    }

    void appVideoActive(uint8_t repeats = 8, uint16_t gapMs = 60) {
        if (!port) return;
        for (uint8_t i = 0; i < repeats; ++i) {
            port->println("CVBSOFF");
            port->println("VDAPPVIDEO");
            port->flush();
            delay(gapMs);
        }
    }

    void appFlashStart(uint32_t totalBytes, const char *label = "Flashing app") {
        if (!port) return;
        active = true;
        lastPercent = -1;
        port->print("VDAPPSTART ");
        port->print((unsigned long)totalBytes);
        port->print(' ');
        port->println(label ? label : "Flashing app");
        port->flush();
    }

    void appFlashWrite(uint32_t writtenBytes) {
        if (!port) return;
        port->print("VDAPPWRITE ");
        port->println((unsigned long)writtenBytes);
    }

    void fetchFilesPrepare() {
        sendImmediate("VDFETCHPREP");
    }

    void fetchFilesStart() {
        sendImmediate("VDFETCHSTART");
    }

    void fetchFilesEnd() {
        sendImmediate("VDFETCHEND");
    }

    void fetchFilesKernelReady() {
        sendImmediate("VDFETCHKERNEL");
    }

  private:
    HardwareSerial *port = nullptr;
    bool active = false;
    int lastPercent = -1;
    unsigned long lastEmitMs = 0;
    uint16_t throttle = 250;

    void sendImmediate(const char *command) {
        if (!port || !command) return;
        port->println(command);
        port->flush();
    }

    void emitProgress(int percent, const char *label) {
        percent = constrain(percent, 0, 100);
        unsigned long now = millis();
        if (percent == lastPercent && now - lastEmitMs < throttle) return;
        lastPercent = percent;
        lastEmitMs = now;

        port->print("VDPROG ");
        port->print(percent);
        port->print(' ');
        port->println(label ? label : "Preparing Citadela");
    }
};

class LineReader {
  public:
    explicit LineReader(size_t maxLen = 192) : limit(maxLen) {}

    bool poll(Stream &stream, String &lineOut) {
        while (stream.available()) {
            char c = (char)stream.read();
            if (c == '\n') {
                lineOut = line;
                line = "";
                return true;
            }
            if (c == '\r') continue;
            if (line.length() < limit) {
                line += c;
            } else {
                line = "";
            }
        }
        return false;
    }

    void reset() {
        line = "";
    }

  private:
    String line;
    size_t limit;
};

}
