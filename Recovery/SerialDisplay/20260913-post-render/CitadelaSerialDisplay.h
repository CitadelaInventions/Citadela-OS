#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <FS.h>
#include <mbedtls/base64.h>

namespace Citadela {

// Optional command recorder. Small buffers are allocated only after video init.
class SerialDisplay {
  public:
    void begin(HardwareSerial &uart) { port = &uart; }
    bool enabled() const { return active; }
    bool failed() const { return fault; }
    void enable(bool resetSequence = true) {
        fault = false;
        if (!payload) payload = (char *)malloc(560);
        if (!pixels) pixels = (uint8_t *)malloc(48);
        if (!pending) pending = (char *)malloc(512);
        if (!incoming) incoming = (char *)malloc(128);
        if (!assets) assets = (Asset *)calloc(24, sizeof(Asset));
        if (!tableHashes) tableHashes = (uint32_t *)calloc(6, sizeof(uint32_t));
        active = port && payload && pixels && pending && incoming && assets && tableHashes;
        if (!active) { stopOnError(); return; }
        used = pixelCount = 0;
        if (resetSequence) sequence = 0;
        assetCount = 0;
        memset(tableHashes, 0, 6 * sizeof(uint32_t));
    }
    void disable() {
        active = false;
        used = pixelCount = 0;
        free(payload); payload = nullptr;
        free(pixels); pixels = nullptr;
        free(assets); assets = nullptr; assetCount = 0;
        free(tableHashes); tableHashes = nullptr;
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
        if (size <= 0 || size >= (int)sizeof(record) - 1) { stopOnError(); return; }
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

    void table(int slot, const uint8_t *values) {
        if (!active) return;
        if (slot < 1 || slot > 5) return;
        uint32_t signature = hash(values, 256);
        if (tableHashes[slot] == signature) return;
        tableHashes[slot] = signature;
        for (int offset = 0; offset < 256; offset += 48) {
            char hex[97];
            int count = min(48, 256 - offset);
            for (int i = 0; i < count; ++i) snprintf(hex + i * 2, 3, "%02X", values[offset + i]);
            command("LUT %d %d %s", slot, offset, hex);
        }
    }

    // Transfer the original BMP once per content hash; redraws reference its ID.
    uint32_t asset(fs::FS &fs, const char *path) {
        if (!active) return 0;
        uint32_t pathHash = hash((const uint8_t *)path, strlen(path));
        File file = fs.open(path, FILE_READ);
        if (!file) return 0;
        size_t size = file.size();
        for (int i = 0; i < assetCount; ++i)
            if (assets[i].path == pathHash && assets[i].size == size) return assets[i].id;
        uint8_t bytes[384];
        uint32_t id = 2166136261UL;
        while (file.available()) {
            int got = file.read(bytes, sizeof(bytes));
            if (got <= 0) return 0;
            id = hash(bytes, got, id);
            yield();
        }
        cachedAsset = 0;
        command("ASSET %08lX %u", (unsigned long)id, (unsigned)size);
        flush();
        if (active && cachedAsset != id) {
            file.seek(0);
            size_t offset = 0;
            while (active && offset < size) {
                int got = file.read(bytes, sizeof(bytes));
                if (got <= 0) { stopOnError(); return 0; }
                char record[560];
                int prefix = snprintf(record, sizeof(record), "DATA %08lX %u ", (unsigned long)id, (unsigned)offset);
                size_t encoded = 0;
                mbedtls_base64_encode((unsigned char *)record + prefix, sizeof(record) - prefix - 2,
                                     &encoded, bytes, got);
                record[prefix + encoded] = ';';
                record[prefix + encoded + 1] = 0;
                transmit(record, prefix + encoded + 1);
                offset += got;
            }
            command("ASSETEND %08lX", (unsigned long)id);
            flush();
        }
        if (active && assetCount < 24) assets[assetCount++] = {pathHash, (uint32_t)size, id};
        return active ? id : 0;
    }

    void invalidateAssets() { assetCount = 0; }
    void endFrame() {
        if (active && (used || pixelCount)) { command("FRAME"); flush(); }
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
    struct Asset { uint32_t path, size, id; };
    Asset *assets = nullptr;
    uint8_t assetCount = 0;
    uint32_t cachedAsset = 0;
    uint32_t *tableHashes = nullptr;

    static uint32_t hash(const uint8_t *data, size_t count, uint32_t result = 2166136261UL) {
        while (count--) { result ^= *data++; result *= 16777619UL; }
        return result;
    }

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
        if (used + length + 1 >= 560) sendPacket();
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
                    unsigned long cached = 0;
                    int fields = sscanf(incoming, "VDM ACK %lu %lx", &received, &cached);
                    bool ack = fields >= 1;
                    if (ack && received == wanted && fields == 2) cachedAsset = cached;
                    if (!ack && incomingSize) {
                        // Avoid queuing stale seconds during a first-time file transfer.
                        if (strncmp(incoming, "CBTIME ", 7) == 0) { incomingSize = 0; continue; }
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
        transmit(payload, used);
        used = 0;
    }

    void transmit(const char *data, size_t count) {
        if (!active) return;
        char packet[600];
        ++sequence;
        int size = snprintf(packet, sizeof(packet), "VDM1 %lu %04X %s\n",
                            (unsigned long)sequence, checksum(data, count), data);
        if (size >= (int)sizeof(packet)) { stopOnError(); return; }
        bool acknowledged = false;
        for (int attempt = 0; attempt < 5 && active && !acknowledged; ++attempt) {
            port->write((const uint8_t *)packet, size);
            acknowledged = awaitAck(sequence);
        }
        if (!acknowledged && active) stopOnError();
    }
};

} // namespace Citadela
