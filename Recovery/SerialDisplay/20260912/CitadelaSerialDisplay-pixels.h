#pragma once

#include <Arduino.h>
#include <stdarg.h>

namespace Citadela {

// Optional command recorder. Small buffers are allocated only after video init.
class SerialDisplay {
  public:
    void begin(HardwareSerial &uart) { port = &uart; }
    bool enabled() const { return active; }
    bool failed() const { return fault; }
    void enable() {
        fault = false;
        if (!payload) payload = (char *)malloc(160);
        if (!pixels) pixels = (uint8_t *)malloc(48);
        if (!pending) pending = (char *)malloc(512);
        if (!incoming) incoming = (char *)malloc(128);
        active = port && payload && pixels && pending && incoming;
        if (!active) { stopOnError(); return; }
        used = pixelCount = 0;
        sequence = 0;
    }
    void disable() {
        active = false;
        used = pixelCount = 0;
        free(payload); payload = nullptr;
        free(pixels); pixels = nullptr;
        releaseIdleInput();
    }

    // Deferred HID/time lines received while awaiting transport acknowledgement
    // are consumed by the kernel's ordinary controller parser, in order.
    int available() { return pendingCount + (!active ? incomingSize : 0) + (port ? port->available() : 0); }
    int read() {
        if (pendingCount) {
            int result = pending[pendingRead];
            pendingRead = (pendingRead + 1) % 512;
            --pendingCount;
            releaseIdleInput();
            return result;
        }
        if (!active && incomingSize) {
            int result = incoming[0];
            memmove(incoming, incoming + 1, --incomingSize);
            releaseIdleInput();
            return result;
        }
        return port ? port->read() : -1;
    }

    void command(const char *format, ...) {
        if (!active) return;
        flushPixels();
        char record[160];
        va_list args;
        va_start(args, format);
        int size = vsnprintf(record, sizeof(record), format, args);
        va_end(args);
        if (size <= 0 || size >= (int)sizeof(record)) { stopOnError(); return; }
        append(record, size);
    }

    void pixel(int x, int y, uint8_t index) {
        if (!active) return;
        if (pixelCount && (y != pixelY || x != pixelX + pixelCount || pixelCount == 48))
            flushPixels();
        if (!active) return;
        if (!pixelCount) { pixelX = x; pixelY = y; }
        pixels[pixelCount++] = index;
    }

    void flush() {
        flushPixels();
        sendPacket();
    }

    static uint16_t checksum(const char *data, size_t count) {
        uint16_t crc = 0xffff;
        while (count--) {
            crc ^= (uint8_t)*data++ << 8;
            for (int i = 0; i < 8; ++i)
                crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
        return crc;
    }

  private:
    HardwareSerial *port = nullptr;
    bool active = false;
    bool fault = false;
    uint32_t sequence = 0;
    char *payload = nullptr;
    uint16_t used = 0;
    uint8_t *pixels = nullptr;
    uint8_t pixelCount = 0;
    int16_t pixelX = 0, pixelY = 0;
    char *pending = nullptr;
    uint16_t pendingRead = 0, pendingCount = 0;
    char *incoming = nullptr;
    uint8_t incomingSize = 0;

    void releaseIdleInput() {
        if (!active && !pendingCount && !incomingSize) {
            free(pending); pending = nullptr;
            free(incoming); incoming = nullptr;
            pendingRead = 0;
        }
    }

    void stopOnError() {
        disable();
        fault = true;
    }

    void append(const char *record, int length) {
        if (!active) return;
        if (used + length + 1 >= 160) sendPacket();
        if (!active) return;
        memcpy(payload + used, record, length);
        used += length;
        payload[used++] = ';';
        payload[used] = 0;
    }

    void flushPixels() {
        if (!active || !pixelCount) return;
        char record[128];
        int length = snprintf(record, sizeof(record), "B %d %d ", pixelX, pixelY);
        bool solid = true;
        for (int i = 1; i < pixelCount; ++i) solid &= pixels[i] == pixels[0];
        if (solid) {
            length = snprintf(record, sizeof(record), "R %d %d %u %u",
                              pixelX, pixelY, pixelCount, pixels[0]);
        } else {
            const char *hex = "0123456789ABCDEF";
            int runs = 1;
            for (int i = 1; i < pixelCount; ++i) if (pixels[i] != pixels[i - 1]) ++runs;
            if (runs * 4 < pixelCount * 2) {
                record[0] = 'D';
                for (int i = 0; i < pixelCount;) {
                    int count = 1;
                    while (i + count < pixelCount && pixels[i + count] == pixels[i]) ++count;
                    record[length++] = hex[count >> 4];
                    record[length++] = hex[count & 15];
                    record[length++] = hex[pixels[i] >> 4];
                    record[length++] = hex[pixels[i] & 15];
                    i += count;
                }
            } else {
                for (int i = 0; i < pixelCount; ++i) {
                    record[length++] = hex[pixels[i] >> 4];
                    record[length++] = hex[pixels[i] & 15];
                }
            }
            record[length] = 0;
        }
        pixelCount = 0;
        append(record, length);
    }

    bool awaitAck(uint32_t wanted) {
        uint32_t start = millis();
        while (millis() - start < 8000) {
            while (port->available()) {
                // Leave normal input in the UART when the deferred queue is full.
                if (512 - pendingCount < 128) return false;
                char c = (char)port->read();
                if (c == '\r') continue;
                if (c == '\n') {
                    incoming[incomingSize] = 0;
                    if (strcmp(incoming, "VDM OFF") == 0) {
                        incomingSize = 0;
                        disable();
                        return false;
                    }
                    if (strcmp(incoming, "VDM RETRY") == 0) {
                        incomingSize = 0;
                        return false;
                    }
                    unsigned long received = 0;
                    bool ack = sscanf(incoming, "VDM ACK %lu", &received) == 1;
                    if (!ack && incomingSize) {
                        for (int i = 0; i <= incomingSize; ++i) {
                            pending[(pendingRead + pendingCount) % 512] =
                                i == incomingSize ? '\n' : incoming[i];
                            ++pendingCount;
                        }
                    }
                    incomingSize = 0;
                    if (ack && received == wanted) return true;
                } else if (incomingSize < 127) {
                    incoming[incomingSize++] = c;
                } else {
                    // Preserve an unusually long input line instead of truncating it.
                    for (int i = 0; i < incomingSize; ++i) {
                        pending[(pendingRead + pendingCount) % 512] = incoming[i];
                        ++pendingCount;
                    }
                    incomingSize = 0;
                    incoming[incomingSize++] = c;
                }
            }
            delay(1);
        }
        return false;
    }

    void sendPacket() {
        if (!active || !used) return;
        char packet[192];
        ++sequence;
        int size = snprintf(packet, sizeof(packet), "VDM1 %lu %04X %s\n",
                            (unsigned long)sequence, checksum(payload, used), payload);
        if (size >= (int)sizeof(packet)) { stopOnError(); return; }
        bool acknowledged = false;
        for (int attempt = 0; attempt < 5 && active && !acknowledged; ++attempt) {
            port->write((const uint8_t *)packet, size);
            acknowledged = awaitAck(sequence);
        }
        used = 0;
        if (!acknowledged && active) stopOnError();
    }
};

} // namespace Citadela
