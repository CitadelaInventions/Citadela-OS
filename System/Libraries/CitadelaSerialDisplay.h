#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <FS.h>
#include <mbedtls/base64.h>
#include <new>
#include <esp_heap_caps.h>

namespace Citadela {
class SerialDisplay {
  public:
    void begin(HardwareSerial &uart) { port = &uart; }
    bool enabled() const { return state != nullptr; }
    bool failed() const { return fault; }
    void enable(bool resetSequence = true) {
        disable();
        fault = false;
        state = new (std::nothrow) State();
        if (state) {
            state->packet = (char *)malloc(PacketSize);
            state->payload = (char *)malloc(PayloadSize);
        }
        if (!state || !state->packet || !state->payload || !port) {
            Serial.printf("[VDM] transport allocation failed: heap=%u largest=%u requested=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)sizeof(State));
            stopOnError();
            return;
        }
        if (resetSequence) sequence = 0;
    }
    void disable() {
        if (!state) return;
        while (state->head) {
            Block *next = state->head->next;
            heap_caps_free(state->head);
            state->head = next;
        }
        for (int i = 0; i < state->assetCount; ++i) heap_caps_free(state->assets[i]);
        state->file.close();
        free(state->payload);
        free(state->packet);
        delete state;
        state = nullptr;
    }
    int available() { return port ? port->available() : 0; }
    int read() { return port ? port->read() : -1; }
    unsigned queuedBytes() const { return state ? state->queued : 0; }
    bool hasBacklog() const { return state && (state->queued || state->packetReady || state->phase != Idle); }
    bool takeResyncRequest() { bool value = resyncPending; resyncPending = false; return value; }

    void command(const char *format, ...) {
        if (!state || state->overflow) return;
        flushPixels();
        char record[160];
        va_list args;
        va_start(args, format);
        int length = vsnprintf(record, sizeof(record), format, args);
        va_end(args);
        if (length <= 0 || length >= (int)sizeof(record) - 1) { state->overflow = true; return; }
        append(record, length);
    }
    void pixel(int x, int y, uint8_t index) {
        if (!state || state->overflow) return;
        if (state->pixelCount && (y != state->pixelY || x != state->pixelX + state->pixelCount || state->pixelCount == 48))
            flushPixels();
        if (!state->pixelCount) { state->pixelX = x; state->pixelY = y; }
        state->pixels[state->pixelCount++] = index;
    }
    // Commit without transmitting. These bytes become eligible on service().
    void flush() {
        if (!state) return;
        flushPixels();
        if (state->overflow) { disable(); resyncPending = true; return; }
        state->committed = state->queued;
        state->dirty = false;
    }
    bool endFrame() {
        if (!state) return false;
        if (state->overflow) { flush(); return false; }
        if (!state->dirty) return false;
        command("FRAME");
        flush();
        return true;
    }
    void table(int slot, const uint8_t *values) {
        if (!state || slot < 1 || slot > 5) return;
        uint32_t signature = hash(values, 256);
        if (state->tableHashes[slot] == signature) return;
        state->tableHashes[slot] = signature;
        for (int offset = 0; offset < 256; offset += 48) {
            char hex[97];
            int count = min(48, 256 - offset);
            for (int i = 0; i < count; ++i) snprintf(hex + i * 2, 3, "%02X", values[offset + i]);
            command("LUT %d %d %s", slot, offset, hex);
        }
    }
    // A path alias avoids opening, hashing or transmitting the BMP during drawing.
    uint32_t asset(fs::FS &fs, const char *path) {
        if (!state || state->overflow) return 0;
        for (int i = 0; i < state->assetCount; ++i)
            if (state->assets[i]->fs == &fs && pathEquals(state->assets[i]->path, path))
                return state->assets[i]->alias;
        if (state->assetCount == 24 || strlen(path) >= PathSize) {
            state->overflow = true;
            return 0;
        }
        Asset *allocated = allocateWords<Asset>();
        if (!allocated) { state->overflow = true; return 0; }
        int slot = state->assetCount++;
        state->assets[slot] = allocated;
        Asset &entry = *allocated;
        entry.fs = &fs;
        for (unsigned i = 0; i <= strlen(path); ++i) putByte(entry.path, i, path[i]);
        entry.alias = hash((const uint8_t *)path, strlen(path));
        command("@ %d", slot);
        return entry.alias;
    }
    void invalidateAssets() { if (state) resyncPending = true; }

    bool receive(const char *line) {
        unsigned long received = 0, cached = 0;
        int fields = sscanf(line, "VDM ACK %lu %lx", &received, &cached);
        if (fields >= 1) {
            if (!state || !state->packetReady || received != sequence || !state->attempts) return true;
            Action action = state->action;
            state->packetReady = state->waiting = false;
            if (action == Offer) {
                if (fields == 2 && cached == state->contentId) {
                    state->file.close();
                    state->phase = Alias;
                } else {
                    if (!state->file.seek(0)) { stopOnError(); return true; }
                    state->offset = 0;
                    state->phase = Data;
                }
            } else if (action == DataChunk) {
                state->offset += state->chunkSize;
                if (state->offset >= state->fileSize) state->phase = End;
            } else if (action == Finish) {
                state->file.close();
                state->phase = Alias;
            }
            else if (action == Link) state->phase = Idle;
            return true;
        }
        if (strcmp(line, "VDM RETRY") == 0) {
            if (state && state->packetReady) state->waiting = false;
            return true;
        }
        return false;
    }

    void service() {
        if (!state || state->overflow || resyncPending) return;
        if (state->packetReady) {
            if (state->waiting && millis() - state->sentAt < 1000) return;
            if (state->attempts >= 5) { stopOnError(); return; }
            if (port->availableForWrite() < 32) return;
            // One complete line keeps ordinary controller commands from interleaving.
            // With the existing 256000-baud FIFO this is bounded to about 20 ms.
            port->write((const uint8_t *)state->packet, state->packetSize);
            ++state->attempts;
            state->waiting = true;
            state->sentAt = millis();
            return;
        }
        if (state->phase != Idle) { serviceAsset(); return; }
        int used = 0;
        while (state->head && state->committed) {
            Block &block = *state->head;
            int length = 0;
            while (byteAt(block.data, block.read + length) != ';') ++length;
            ++length;
            if (byteAt(block.data, block.read) == '@') {
                if (used) break;
                int slot = 0;
                for (int i = 2; i < length - 1; ++i)
                    slot = slot * 10 + byteAt(block.data, block.read + i) - '0';
                consume(length);
                state->assetSlot = slot;
                Asset &entry = *state->assets[slot];
                char path[PathSize];
                for (unsigned i = 0; i < PathSize; ++i) {
                    path[i] = byteAt(entry.path, i);
                    if (!path[i]) break;
                }
                state->file = entry.fs->open(path, FILE_READ);
                if (!state->file) { stopOnError(); return; }
                state->fileSize = state->file.size();
                state->contentId = 2166136261UL;
                state->offset = 0;
                state->phase = Hashing;
                return;
            }
            if (used + length >= PayloadSize) break;
            for (int i = 0; i < length; ++i)
                state->payload[used + i] = byteAt(block.data, block.read + i);
            used += length;
            consume(length);
        }
        if (used) { state->payload[used] = 0; prepare(Commands); }
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
    enum Phase { Idle, Hashing, Data, End, Alias };
    enum Action { Commands, Offer, DataChunk, Finish, Link };
    static constexpr unsigned ReserveHeap = 12 * 1024;
    static constexpr unsigned WordCaps = MALLOC_CAP_EXEC | MALLOC_CAP_32BIT;
    static constexpr unsigned PayloadSize = 560, PacketSize = 600, PathSize = 96;
    // IRAM heap is word-access-only: never memcpy or dereference bytes here.
    struct Block {
        Block *volatile next = nullptr;
        volatile uint32_t read = 0, used = 0;
        volatile uint32_t data[256];
    };
    struct Asset { fs::FS *volatile fs = nullptr; volatile uint32_t alias = 0; volatile uint32_t path[PathSize / 4]; };
    struct State {
        Block *head = nullptr, *tail = nullptr;
        unsigned queued = 0, committed = 0, blocks = 0;
        bool dirty = false, overflow = false;
        Asset *assets[24] = {};
        uint8_t assetCount = 0, assetSlot = 0;
        uint32_t tableHashes[6] = {};
        uint8_t pixels[48], pixelCount = 0;
        int16_t pixelX = 0, pixelY = 0;
        char *payload = nullptr, *packet = nullptr;
        uint16_t packetSize = 0, chunkSize = 0;
        uint8_t attempts = 0;
        bool packetReady = false, waiting = false;
        uint32_t sentAt = 0, contentId = 0, offset = 0, fileSize = 0;
        Action action = Commands;
        Phase phase = Idle;
        File file;
    };
    HardwareSerial *port = nullptr;
    State *state = nullptr;
    uint32_t sequence = 0;
    bool fault = false, resyncPending = false;
    static uint8_t byteAt(const volatile uint32_t *words, unsigned offset) {
        return (words[offset >> 2] >> ((offset & 3) * 8)) & 255;
    }
    static void putByte(volatile uint32_t *words, unsigned offset, uint8_t value) {
        unsigned shift = (offset & 3) * 8;
        if (!shift) words[offset >> 2] = value;
        else words[offset >> 2] = (words[offset >> 2] & ~(255UL << shift)) | ((uint32_t)value << shift);
    }
    static bool pathEquals(const volatile uint32_t *words, const char *path) {
        for (unsigned i = 0; i < PathSize; ++i) {
            if (byteAt(words, i) != (uint8_t)path[i]) return false;
            if (!path[i]) return true;
        }
        return false;
    }
    template<typename T> static T *allocateWords() {
        if (heap_caps_get_free_size(WordCaps) < ReserveHeap + sizeof(T)) return nullptr;
        void *memory = heap_caps_malloc(sizeof(T), WordCaps);
        return memory ? new (memory) T : nullptr;
    }
    static uint32_t hash(const uint8_t *data, size_t count, uint32_t result = 2166136261UL) {
        while (count--) { result ^= *data++; result *= 16777619UL; }
        return result;
    }
    void stopOnError() { disable(); fault = true; }
    void append(const char *record, int length) {
        if (!state || state->overflow) return;
        if (!state->tail || state->tail->used + length + 1 > sizeof(Block::data)) {
            if (state->blocks >= 40) {
                state->overflow = true;
                return;
            }
            Block *block = allocateWords<Block>();
            if (!block) { state->overflow = true; return; }
            if (state->tail) state->tail->next = block;
            else state->head = block;
            state->tail = block;
            ++state->blocks;
        }
        Block &block = *state->tail;
        for (int i = 0; i < length; ++i) putByte(block.data, block.used + i, record[i]);
        putByte(block.data, block.used + length, ';');
        block.used += length + 1;
        state->queued += length + 1;
        state->dirty = true;
    }
    void consume(int length) {
        state->head->read += length;
        state->queued -= length;
        state->committed -= length;
        if (state->head->read == state->head->used) {
            Block *old = state->head;
            state->head = old->next;
            if (!state->head) state->tail = nullptr;
            heap_caps_free(old);
            --state->blocks;
        }
    }
    void flushPixels() {
        if (!state || !state->pixelCount || state->overflow) return;
        char record[128];
        int length = snprintf(record, sizeof(record), "B %d %d ", state->pixelX, state->pixelY);
        for (int i = 0; i < state->pixelCount; ++i) {
            snprintf(record + length, 3, "%02X", state->pixels[i]);
            length += 2;
        }
        state->pixelCount = 0;
        append(record, length);
    }
    void prepare(Action action) {
        ++sequence;
        int length = snprintf(state->packet, PacketSize, "VDM1 %lu %04X %s\n",
            (unsigned long)sequence, checksum(state->payload, strlen(state->payload)), state->payload);
        if (length <= 0 || length >= PacketSize) { stopOnError(); return; }
        state->packetSize = length;
        state->action = action;
        state->packetReady = true;
        state->waiting = false;
        state->attempts = 0;
    }
    void serviceAsset() {
        uint8_t bytes[384];
        if (state->phase == Hashing) {
            if (state->offset < state->fileSize) {
                int got = state->file.read(bytes, sizeof(bytes));
                if (got <= 0) { stopOnError(); return; }
                state->contentId = hash(bytes, got, state->contentId);
                state->offset += got;
                return;
            }
            snprintf(state->payload, PayloadSize, "ASSET %08lX %u;",
                (unsigned long)state->contentId, (unsigned)state->fileSize);
            prepare(Offer);
        } else if (state->phase == Data) {
            int got = state->file.read(bytes, sizeof(bytes));
            if (got <= 0) { stopOnError(); return; }
            int prefix = snprintf(state->payload, PayloadSize, "DATA %08lX %u ",
                (unsigned long)state->contentId, (unsigned)state->offset);
            size_t encoded = 0;
            int result = mbedtls_base64_encode((unsigned char *)state->payload + prefix,
                PayloadSize - prefix - 2, &encoded, bytes, got);
            if (result != 0) { stopOnError(); return; }
            state->payload[prefix + encoded] = ';';
            state->payload[prefix + encoded + 1] = 0;
            state->chunkSize = got;
            prepare(DataChunk);
        } else if (state->phase == End) {
            snprintf(state->payload, PayloadSize, "ASSETEND %08lX;", (unsigned long)state->contentId);
            prepare(Finish);
        } else if (state->phase == Alias) {
            snprintf(state->payload, PayloadSize, "ASSETREF %08lX %08lX;",
                (unsigned long)state->assets[state->assetSlot]->alias, (unsigned long)state->contentId);
            prepare(Link);
        }
    }
};

}
