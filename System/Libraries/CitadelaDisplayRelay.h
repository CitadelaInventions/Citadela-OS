#pragma once

#include "CitadelaSerialDisplay.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Citadela {

class DisplayRelay {
  public:
    class Input : public Stream {
      public:
        int available() override {
            portENTER_CRITICAL(&mux); int n = count; portEXIT_CRITICAL(&mux); return n;
        }
        int read() override {
            portENTER_CRITICAL(&mux);
            int value = -1;
            if (count) { value = bytes[head]; head = (head + 1) % sizeof(bytes); --count; }
            portEXIT_CRITICAL(&mux);
            return value;
        }
        int peek() override {
            portENTER_CRITICAL(&mux); int v = count ? bytes[head] : -1; portEXIT_CRITICAL(&mux); return v;
        }
        void flush() override {}
        size_t write(uint8_t) override { return 0; }
        bool push(const char *line, size_t length) {
            portENTER_CRITICAL(&mux);
            bool ok = count + length + 1 <= sizeof(bytes);
            if (ok) {
                for (size_t i = 0; i <= length; ++i) {
                    bytes[(head + count) % sizeof(bytes)] = i == length ? '\n' : line[i];
                    ++count;
                }
            }
            portEXIT_CRITICAL(&mux);
            return ok;
        }
      private:
        uint8_t bytes[4096];
        uint16_t head = 0, count = 0;
        portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    } pcInput, peerInput;

    bool begin() {
        return xTaskCreatePinnedToCore(task, "DisplayRelay", 4096, this, 2, &taskHandle, 1) == pdPASS;
    }
    bool running() const { return taskHandle != nullptr; }

  private:
    TaskHandle_t taskHandle = nullptr;
    bool requested = false;
    uint32_t lastPcSeen = 0;
    uint32_t lastOverflowLog = 0;
    uint32_t droppedInputLines = 0;
    struct Line { char data[601]; uint16_t length = 0; bool overflow = false; } pc, peer;

    static void task(void *context) {
        auto &relay = *(DisplayRelay *)context;
        while (true) {
            relay.pump(Serial1, relay.peer, true);
            relay.pump(Serial, relay.pc, false);
            if (relay.requested && millis() - relay.lastPcSeen > 10000) {
                relay.requested = false;
                writePeer("VDM OFF");
            }
            vTaskDelay(1);
        }
    }

    static void writePeer(const char *line) {
        char message[194];
        int length = snprintf(message, sizeof(message), "%s\n", line);
        if (length > 0 && length < (int)sizeof(message))
            Serial1.write((const uint8_t *)message, length);
    }

    void receive(const char *line, bool fromPeer) {
        if (fromPeer && strncmp(line, "VDM1 ", 5) == 0) {
            unsigned long sequence = 0;
            unsigned crc = 0;
            int offset = 0;
            if (sscanf(line, "VDM1 %lu %x %n", &sequence, &crc, &offset) != 2 || offset <= 0) return;
            const char *payload = line + offset;
            if (SerialDisplay::checksum(payload, strlen(payload)) != crc) { writePeer("VDM RETRY"); return; }
            char packet[604];
            int length = snprintf(packet, sizeof(packet), "\n%s\n", line);
            Serial.write((const uint8_t *)packet, length);
            return;
        }
        if (!fromPeer && strncmp(line, "VDM ", 4) == 0) {
            lastPcSeen = millis();
            if (strcmp(line, "VDM HELLO") == 0) {
                Serial.println("VDM CONTROLLER 1");
            } else if (strcmp(line, "VDM ON") == 0 || strcmp(line, "VDM OFF") == 0 ||
                       strcmp(line, "VDM SNAP") == 0 || strncmp(line, "VDM INPUT ", 10) == 0 ||
                       strncmp(line, "VDM ACK ", 8) == 0 || strcmp(line, "VDM RETRY") == 0 ||
                       strncmp(line, "VDM CHECK ", 10) == 0) {
                if (strcmp(line, "VDM ON") == 0 || strcmp(line, "VDM SNAP") == 0) requested = true;
                if (strcmp(line, "VDM OFF") == 0) requested = false;
                writePeer(line);
            }
            return;
        }
        if (fromPeer && requested && strcmp(line, "VDKERNEL") == 0)
            Serial.println("VDM SOURCE_READY");
        if (fromPeer && strncmp(line, "VDM ", 4) == 0) { Serial.println(line); return; }
        Input &destination = fromPeer ? peerInput : pcInput;
        if (!destination.push(line, strlen(line))) {
            ++droppedInputLines;
            uint32_t now = millis();
            if (now - lastOverflowLog >= 1000) {
                Serial.printf("VDM RELAY_INPUT_OVERFLOW dropped=%lu\n",
                              (unsigned long)droppedInputLines);
                droppedInputLines = 0;
                lastOverflowLog = now;
            }
        }
    }

    void pump(HardwareSerial &uart, Line &line, bool fromPeer) {
        // Bound each pass so a chatty port cannot starve the other direction.
        for (int budget = 0; budget < 512 && uart.available(); ++budget) {
            char c = (char)uart.read();
            if (c == '\r') continue;
            if (c == '\n') {
                line.data[line.length] = 0;
                if (!line.overflow && line.length) receive(line.data, fromPeer);
                line.length = 0;
                line.overflow = false;
            } else if (line.length < sizeof(line.data) - 1) {
                line.data[line.length++] = c;
            } else line.overflow = true;
        }
    }
};

}
