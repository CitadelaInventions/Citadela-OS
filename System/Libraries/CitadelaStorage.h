#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include "CitadelaBoardPins.h"

namespace Citadela {

struct SDStats {
    uint64_t cardSize = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
};

class Storage {
  public:
    static bool beginSPIFFS(bool formatOnFail = true) {
        if (SPIFFS.begin(true)) return true;
        if (!formatOnFail) return false;
        SPIFFS.format();
        return SPIFFS.begin(true);
    }

    static bool beginSD(SDFS &sd,
                        SPIClass &spi,
                        uint8_t csPin,
                        uint32_t frequency,
                        SDStats *stats = nullptr) {
        if (!sd.begin(csPin, spi, frequency)) return false;
        if (stats) {
            stats->cardSize = sd.cardSize();
            stats->usedBytes = sd.usedBytes();
            stats->freeBytes = stats->cardSize - stats->usedBytes;
        }
        return true;
    }

    static bool beginBoardSD(SDFS &sd,
                             SPIClass &spi,
                             uint32_t frequency,
                             SDStats *stats = nullptr) {
        BoardPins::beginSDCardSPI(spi);
        return beginSD(sd, spi, BoardPins::SdChipSelect, frequency, stats);
    }

    static void resetSDBus(SDFS &sd,
                           SPIClass &spi,
                           uint8_t sckPin,
                           uint8_t misoPin,
                           uint8_t mosiPin,
                           uint8_t csPin,
                           uint32_t settleMs = 8) {
        sd.end();
        pinMode(csPin, OUTPUT);
        digitalWrite(csPin, HIGH);
        delay(2);
        spi.end();
        delay(2);
        spi.begin(sckPin, misoPin, mosiPin, csPin);
        digitalWrite(csPin, HIGH);
        delay(settleMs);
    }

    static void resetBoardSDBus(SDFS &sd, SPIClass &spi, uint32_t settleMs = 8) {
        resetSDBus(sd, spi,
                   BoardPins::SdClock,
                   BoardPins::SdMiso,
                   BoardPins::SdMosi,
                   BoardPins::SdChipSelect,
                   settleMs);
    }

    static bool beginSDWithRetry(SDFS &sd,
                                 SPIClass &spi,
                                 uint8_t sckPin,
                                 uint8_t misoPin,
                                 uint8_t mosiPin,
                                 uint8_t csPin,
                                 const uint32_t *frequencies,
                                 size_t frequencyCount,
                                 SDStats *stats = nullptr,
                                 Stream *log = nullptr) {
        if (!frequencies || frequencyCount == 0) return false;
        for (size_t attempt = 0; attempt < frequencyCount; ++attempt) {
            resetSDBus(sd, spi, sckPin, misoPin, mosiPin, csPin,
                       8 + (uint32_t)attempt * 8);
            if (beginSD(sd, spi, csPin, frequencies[attempt], stats)) {
                if (log) {
                    log->printf("SD mounted at %lu Hz on attempt %u.\n",
                                (unsigned long)frequencies[attempt],
                                (unsigned)(attempt + 1));
                }
                return true;
            }
            if (log) {
                log->printf("SD mount attempt %u failed at %lu Hz.\n",
                            (unsigned)(attempt + 1),
                            (unsigned long)frequencies[attempt]);
            }
        }
        resetSDBus(sd, spi, sckPin, misoPin, mosiPin, csPin);
        return false;
    }

    static bool beginBoardSDWithRetry(SDFS &sd,
                                      SPIClass &spi,
                                      const uint32_t *frequencies,
                                      size_t frequencyCount,
                                      SDStats *stats = nullptr,
                                      Stream *log = nullptr) {
        return beginSDWithRetry(sd, spi,
                                BoardPins::SdClock,
                                BoardPins::SdMiso,
                                BoardPins::SdMosi,
                                BoardPins::SdChipSelect,
                                frequencies,
                                frequencyCount,
                                stats,
                                log);
    }

    static uint32_t fnv1a(File &file) {
        const size_t bufferSize = 1024;
        uint8_t buffer[bufferSize];
        uint32_t hash = 2166136261u;
        file.seek(0);
        while (true) {
            int readCount = file.read(buffer, bufferSize);
            if (readCount <= 0) break;
            for (int i = 0; i < readCount; ++i) {
                hash ^= (uint8_t)buffer[i];
                hash *= 16777619u;
            }
        }
        file.seek(0);
        return hash;
    }

    static bool filesMatch(fs::FS &leftFs, const char *leftPath, fs::FS &rightFs, const char *rightPath) {
        if (!leftFs.exists(leftPath) || !rightFs.exists(rightPath)) return false;
        File left = leftFs.open(leftPath, FILE_READ);
        if (!left) return false;
        File right = rightFs.open(rightPath, FILE_READ);
        if (!right) {
            left.close();
            return false;
        }
        bool same = left.size() == right.size();
        if (same) {
            uint32_t leftHash = fnv1a(left);
            uint32_t rightHash = fnv1a(right);
            same = leftHash != 0 && leftHash == rightHash;
        }
        left.close();
        right.close();
        return same;
    }

    static bool ensureDirPath(fs::FS &fs, const char *dirPath) {
        String path(dirPath);
        path.trim();
        if (!path.length() || path == "/") return true;
        if (fs.exists(path.c_str())) return true;

        int start = path.charAt(0) == '/' ? 1 : 0;
        for (int i = start; i < path.length(); ++i) {
            if (path.charAt(i) != '/') continue;
            String part = path.substring(0, i);
            if (part.length() && !fs.exists(part.c_str()) && !fs.mkdir(part.c_str())) return false;
        }

        return fs.exists(path.c_str()) || fs.mkdir(path.c_str());
    }

    static bool ensureParentDir(fs::FS &fs, const char *path) {
        String full(path);
        int slash = full.lastIndexOf('/');
        if (slash <= 0) return true;
        String parent = full.substring(0, slash);
        return ensureDirPath(fs, parent.c_str());
    }

    static bool copyFile(fs::FS &srcFs,
                         const char *srcPath,
                         fs::FS &dstFs,
                         const char *dstPath,
                         size_t bufferSize = 4096,
                         Stream *log = nullptr) {
        if (!srcFs.exists(srcPath)) return false;

        File sourceInfo = srcFs.open(srcPath, FILE_READ);
        if (!sourceInfo) return false;
        const size_t expectedBytes = sourceInfo.size();
        sourceInfo.close();
        if (expectedBytes == 0) return false;

        ensureParentDir(dstFs, dstPath);
        if (dstFs.exists(dstPath)) dstFs.remove(dstPath);

        uint8_t *buffer = (uint8_t*)malloc(bufferSize);
        if (!buffer) {
            bufferSize = 512;
            buffer = (uint8_t*)malloc(bufferSize);
        }
        if (!buffer) {
            return false;
        }

        size_t committedBytes = 0;
        bool ok = false;
        const int maxSessions = 6;
        for (int session = 0; session < maxSessions && committedBytes < expectedBytes; ++session) {
            File src = srcFs.open(srcPath, FILE_READ);
            if (!src || !src.seek(committedBytes)) {
                if (src) src.close();
                break;
            }

            File dst = dstFs.open(dstPath, session == 0 ? FILE_WRITE : FILE_APPEND);
            if (!dst) {
                src.close();
                break;
            }

            size_t streamedBytes = committedBytes;
            bool sessionHealthy = true;
            while (sessionHealthy && streamedBytes < expectedBytes) {
                size_t request = min(bufferSize, expectedBytes - streamedBytes);
                size_t readCount = src.read(buffer, request);
                if (readCount == 0) {
                    if (log) log->printf("Copy read stopped at %u/%u bytes.\n",
                                         (unsigned)streamedBytes,
                                         (unsigned)expectedBytes);
                    sessionHealthy = false;
                    break;
                }

                size_t blockWritten = 0;
                int zeroWriteRetries = 0;
                while (blockWritten < readCount) {
                    size_t written = dst.write(buffer + blockWritten, readCount - blockWritten);
                    if (written > 0) {
                        blockWritten += written;
                        zeroWriteRetries = 0;
                        continue;
                    }
                    dst.flush();
                    yield();
                    if (++zeroWriteRetries >= 3) {
                        sessionHealthy = false;
                        break;
                    }
                    delay(2);
                }
                streamedBytes += blockWritten;
                if ((streamedBytes & 0x3fff) == 0) yield();
            }

            dst.flush();
            src.close();
            dst.close();

            File verify = dstFs.open(dstPath, FILE_READ);
            size_t storedBytes = verify ? verify.size() : 0;
            if (verify) verify.close();
            if (storedBytes > expectedBytes || storedBytes < committedBytes) break;

            if (log) {
                log->printf("Copy session %d: streamed=%u stored=%u expected=%u bytes.\n",
                            session + 1,
                            (unsigned)streamedBytes,
                            (unsigned)storedBytes,
                            (unsigned)expectedBytes);
            }
            if (storedBytes == expectedBytes) {
                committedBytes = storedBytes;
                ok = true;
                break;
            }
            if (storedBytes == committedBytes) {
                delay(10);
            }
            committedBytes = storedBytes;
            delay(5);
        }

        free(buffer);
        if (log) {
            log->printf("Copy %s: stored=%u expected=%u bytes.\n",
                        ok ? "complete" : "failed",
                        (unsigned)committedBytes,
                        (unsigned)expectedBytes);
        }
        return ok;
    }
};

}
