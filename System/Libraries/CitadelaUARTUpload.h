#pragma once

#include <Arduino.h>
#include <FS.h>

namespace Citadela {

enum class UARTUploadTarget : uint8_t {
    App,
    Kernel
};

struct UARTUploadRequest {
    UARTUploadTarget target = UARTUploadTarget::App;
    String appName;
};

struct UARTUploadResult {
    bool ok = false;
    String error;
    uint32_t binaryBytes = 0;
    uint32_t sourceBytes = 0;
};

class UARTUploadReceiver {
  public:
    typedef void (*ProgressCallback)(int percent, const char *label);

    static const uint16_t ChunkBytes = 512;
    static const uint32_t BinaryLimit = 4UL * 1024UL * 1024UL;
    static const uint32_t SourceLimit = 2UL * 1024UL * 1024UL;

    static bool parseBegin(const String &input, UARTUploadRequest &request, String &error) {
        String line = input;
        line.trim();
        if (!line.startsWith("CITUART BEGIN ")) {
            error = "BAD_BEGIN";
            return false;
        }

        String payload = line.substring(14);
        payload.trim();
        if (payload.equalsIgnoreCase("KERNEL")) {
            request.target = UARTUploadTarget::Kernel;
            request.appName = "kernel";
            return true;
        }

        if (!payload.startsWith("APP ")) {
            error = "BAD_TARGET";
            return false;
        }
        String name = payload.substring(4);
        name.trim();
        if (!safeAppName(name)) {
            error = "BAD_APP_NAME";
            return false;
        }

        request.target = UARTUploadTarget::App;
        request.appName = name;
        return true;
    }

    static UARTUploadResult receive(Stream &uart,
                                    fs::FS &storage,
                                    const UARTUploadRequest &request,
                                    ProgressCallback progress = nullptr) {
        UARTUploadResult result;
        String binaryTemp;
        String sourceTemp;
        String mirrorTemp;

        if (!preparePaths(storage, request, binaryTemp, sourceTemp, mirrorTemp, result.error)) {
            sendError(uart, result.error);
            return result;
        }

        uart.print("CITUART READY ");
        uart.print(request.target == UARTUploadTarget::Kernel ? "KERNEL" : "APP");
        uart.print(' ');
        uart.println(request.appName);

        if (progress) progress(2, "Receiving binary");
        if (!receiveFile(uart, storage, "BIN", binaryTemp, BinaryLimit,
                         2, 70, result.binaryBytes, result.error, progress)) {
            cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
            sendError(uart, result.error);
            return result;
        }
        if (!validESPImage(storage, binaryTemp)) {
            cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
            result.error = "INVALID_ESP_IMAGE";
            sendError(uart, result.error);
            return result;
        }

        if (progress) progress(70, "Receiving source");
        if (!receiveFile(uart, storage, "INO", sourceTemp, SourceLimit,
                         70, 94, result.sourceBytes, result.error, progress)) {
            cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
            sendError(uart, result.error);
            return result;
        }

        String command;
        if (!readLine(uart, command, 30000) || command != "CITUART COMMIT") {
            cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
            result.error = command == "CITUART CANCEL" ? "CANCELLED" : "COMMIT_TIMEOUT";
            sendError(uart, result.error);
            return result;
        }

        if (progress) progress(96, "Installing package");
        if (!commit(storage, request, binaryTemp, sourceTemp, mirrorTemp, result.error)) {
            cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
            sendError(uart, result.error);
            return result;
        }

        result.ok = true;
        if (progress) progress(100, "Upload complete");
        uart.print("CITUART COMMITTED ");
        uart.print(request.target == UARTUploadTarget::Kernel ? "KERNEL" : "APP");
        uart.print(' ');
        uart.println(request.appName);
        return result;
    }

  private:
    static bool safeAppName(const String &name) {
        if (name.length() == 0 || name.length() > 40) return false;
        for (size_t i = 0; i < name.length(); ++i) {
            char c = name.charAt(i);
            if (!isAlphaNumeric(c) && c != '_' && c != '-') return false;
        }
        return true;
    }

    static bool readLine(Stream &uart, String &line, uint32_t timeoutMs) {
        line = "";
        uint32_t lastData = millis();
        while (millis() - lastData < timeoutMs) {
            while (uart.available()) {
                int value = uart.read();
                if (value < 0) break;
                lastData = millis();
                char c = (char)value;
                if (c == '\r') continue;
                if (c == '\n') {
                    line.trim();
                    return line.length() > 0;
                }
                if (line.length() >= 120) return false;
                line += c;
            }
            delay(1);
        }
        return false;
    }

    static bool readExact(Stream &uart, uint8_t *buffer, size_t bytes, uint32_t timeoutMs) {
        size_t received = 0;
        uint32_t lastData = millis();
        while (received < bytes && millis() - lastData < timeoutMs) {
            int available = uart.available();
            if (available <= 0) {
                delay(1);
                continue;
            }
            size_t request = min((size_t)available, bytes - received);
            size_t count = uart.readBytes((char*)buffer + received, request);
            if (count > 0) {
                received += count;
                lastData = millis();
            }
        }
        return received == bytes;
    }

    static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t bytes) {
        for (size_t i = 0; i < bytes; ++i) {
            crc ^= data[i];
            for (uint8_t bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
            }
        }
        return crc;
    }

    static bool parseFileHeader(const String &line,
                                const char *expectedKind,
                                uint32_t limit,
                                uint32_t &size,
                                uint32_t &crc) {
        if (!line.startsWith("CITUART FILE ")) return false;
        int kindStart = 13;
        int kindEnd = line.indexOf(' ', kindStart);
        if (kindEnd < 0 || line.substring(kindStart, kindEnd) != expectedKind) return false;
        int sizeEnd = line.indexOf(' ', kindEnd + 1);
        if (sizeEnd < 0) return false;

        String sizeText = line.substring(kindEnd + 1, sizeEnd);
        String crcText = line.substring(sizeEnd + 1);
        sizeText.trim();
        crcText.trim();
        if (sizeText.length() == 0 || crcText.length() != 8) return false;

        char *end = nullptr;
        unsigned long parsedSize = strtoul(sizeText.c_str(), &end, 10);
        if (!end || *end != '\0' || parsedSize == 0 || parsedSize > limit) return false;
        end = nullptr;
        unsigned long parsedCrc = strtoul(crcText.c_str(), &end, 16);
        if (!end || *end != '\0') return false;
        size = (uint32_t)parsedSize;
        crc = (uint32_t)parsedCrc;
        return true;
    }

    static bool writeExact(File &file, const uint8_t *data, size_t bytes) {
        size_t written = 0;
        uint8_t retries = 0;
        while (written < bytes) {
            size_t count = file.write(data + written, bytes - written);
            if (count > 0) {
                written += count;
                retries = 0;
            } else if (++retries >= 3) {
                return false;
            } else {
                file.flush();
                delay(2);
            }
        }
        return true;
    }

    static bool validESPImage(fs::FS &storage, const String &path) {
        File image = storage.open(path.c_str(), FILE_READ);
        if (!image) return false;
        size_t size = image.size();
        int magic = image.read();
        image.close();
        return size >= 32768 && magic == 0xE9;
    }

    static bool receiveFile(Stream &uart,
                            fs::FS &storage,
                            const char *kind,
                            const String &tempPath,
                            uint32_t limit,
                            int progressStart,
                            int progressEnd,
                            uint32_t &receivedBytes,
                            String &error,
                            ProgressCallback progress) {
        String header;
        if (!readLine(uart, header, 30000)) {
            error = String(kind) + "_HEADER_TIMEOUT";
            return false;
        }
        if (header == "CITUART CANCEL") {
            error = "CANCELLED";
            return false;
        }

        uint32_t expectedBytes = 0;
        uint32_t expectedCrc = 0;
        if (!parseFileHeader(header, kind, limit, expectedBytes, expectedCrc)) {
            error = String("BAD_") + kind + "_HEADER";
            return false;
        }

        storage.remove(tempPath.c_str());
        File output = storage.open(tempPath.c_str(), FILE_WRITE);
        if (!output) {
            error = String("CREATE_") + kind + "_FAILED";
            return false;
        }

        uart.print("CITUART FILE_READY ");
        uart.print(kind);
        uart.print(' ');
        uart.println(ChunkBytes);

        uint8_t buffer[ChunkBytes];
        uint32_t crc = 0xFFFFFFFFUL;
        receivedBytes = 0;
        bool ok = true;
        while (receivedBytes < expectedBytes) {
            size_t chunk = min((uint32_t)ChunkBytes, expectedBytes - receivedBytes);
            if (!readExact(uart, buffer, chunk, 15000)) {
                error = String(kind) + "_DATA_TIMEOUT";
                ok = false;
                break;
            }
            if (!writeExact(output, buffer, chunk)) {
                error = String(kind) + "_WRITE_FAILED";
                ok = false;
                break;
            }
            crc = crc32Update(crc, buffer, chunk);
            receivedBytes += chunk;

            uart.print("CITUART ACK ");
            uart.print(kind);
            uart.print(' ');
            uart.println(receivedBytes);

            if (progress) {
                int percent = progressStart + (int)((uint64_t)(progressEnd - progressStart) * receivedBytes / expectedBytes);
                progress(percent, strcmp(kind, "BIN") == 0 ? "Receiving binary" : "Receiving source");
            }
            yield();
        }

        output.flush();
        output.close();
        crc ^= 0xFFFFFFFFUL;
        if (!ok) {
            storage.remove(tempPath.c_str());
            return false;
        }

        File verification = storage.open(tempPath.c_str(), FILE_READ);
        uint32_t storedBytes = verification ? (uint32_t)verification.size() : 0;
        if (verification) verification.close();
        if (storedBytes != expectedBytes) {
            storage.remove(tempPath.c_str());
            error = String(kind) + "_SIZE_MISMATCH";
            return false;
        }
        if (crc != expectedCrc) {
            storage.remove(tempPath.c_str());
            error = String(kind) + "_CRC_MISMATCH";
            return false;
        }

        uart.print("CITUART FILE_OK ");
        uart.print(kind);
        uart.print(' ');
        uart.print(storedBytes);
        uart.print(' ');
        char crcText[9];
        snprintf(crcText, sizeof(crcText), "%08lX", (unsigned long)crc);
        uart.println(crcText);
        return true;
    }

    static bool preparePaths(fs::FS &storage,
                             const UARTUploadRequest &request,
                             String &binaryTemp,
                             String &sourceTemp,
                             String &mirrorTemp,
                             String &error) {
        if (request.target == UARTUploadTarget::App) {
            if (!safeAppName(request.appName) ||
                !ensureDirectory(storage, "/apps") ||
                !ensureDirectory(storage, "/apps/AppCodes") ||
                !ensureDirectory(storage, String("/apps/AppCodes/") + request.appName)) {
                error = "APP_DIRECTORY_FAILED";
                return false;
            }
            binaryTemp = String("/apps/.") + request.appName + ".bin.uartpart";
            sourceTemp = String("/apps/AppCodes/") + request.appName + "/." + request.appName + ".ino.uartpart";
            mirrorTemp = "";
        } else {
            if (!ensureDirectory(storage, "/System") ||
                !ensureDirectory(storage, "/System/kernel")) {
                error = "KERNEL_DIRECTORY_FAILED";
                return false;
            }
            binaryTemp = "/System/.kernel.bin.uartpart";
            sourceTemp = "/System/kernel/.kernel.ino.uartpart";
            mirrorTemp = "/System/kernel/.kernel.bin.uartpart";
        }
        cleanupTemps(storage, binaryTemp, sourceTemp, mirrorTemp);
        return true;
    }

    static bool ensureDirectory(fs::FS &storage, const String &path) {
        if (storage.exists(path.c_str())) {
            File existing = storage.open(path.c_str());
            bool directory = existing && existing.isDirectory();
            if (existing) existing.close();
            return directory;
        }
        return storage.mkdir(path.c_str());
    }

    static bool copyFile(fs::FS &storage, const String &sourcePath, const String &targetPath) {
        File source = storage.open(sourcePath.c_str(), FILE_READ);
        if (!source) return false;
        storage.remove(targetPath.c_str());
        File target = storage.open(targetPath.c_str(), FILE_WRITE);
        if (!target) {
            source.close();
            return false;
        }

        uint8_t buffer[1024];
        bool ok = true;
        while (source.available()) {
            int count = source.read(buffer, sizeof(buffer));
            if (count <= 0 || !writeExact(target, buffer, (size_t)count)) {
                ok = false;
                break;
            }
            yield();
        }
        target.flush();
        uint32_t sourceSize = (uint32_t)source.size();
        source.close();
        target.close();

        File verification = storage.open(targetPath.c_str(), FILE_READ);
        uint32_t targetSize = verification ? (uint32_t)verification.size() : 0;
        if (verification) verification.close();
        if (!ok || sourceSize == 0 || sourceSize != targetSize) {
            storage.remove(targetPath.c_str());
            return false;
        }
        return true;
    }

    static bool commit(fs::FS &storage,
                       const UARTUploadRequest &request,
                       const String &binaryTemp,
                       const String &sourceTemp,
                       const String &mirrorTemp,
                       String &error) {
        String temps[3];
        String finals[3];
        size_t count = 0;
        if (request.target == UARTUploadTarget::App) {
            temps[0] = binaryTemp;
            finals[0] = String("/apps/") + request.appName + ".bin";
            temps[1] = sourceTemp;
            finals[1] = String("/apps/AppCodes/") + request.appName + "/" + request.appName + ".ino";
            count = 2;
        } else {
            if (!copyFile(storage, binaryTemp, mirrorTemp)) {
                error = "KERNEL_MIRROR_FAILED";
                return false;
            }
            temps[0] = binaryTemp;
            finals[0] = "/System/kernel.bin";
            temps[1] = mirrorTemp;
            finals[1] = "/System/kernel/kernel.bin";
            temps[2] = sourceTemp;
            finals[2] = "/System/kernel/kernel.ino";
            count = 3;
        }

        String backups[3];
        bool hadOriginal[3] = {false, false, false};
        size_t backedUp = 0;
        for (size_t i = 0; i < count; ++i) {
            backups[i] = finals[i] + ".uartbak";
            storage.remove(backups[i].c_str());
            hadOriginal[i] = storage.exists(finals[i].c_str());
            if (hadOriginal[i] && !storage.rename(finals[i].c_str(), backups[i].c_str())) {
                for (size_t rollback = 0; rollback < backedUp; ++rollback) {
                    if (hadOriginal[rollback]) storage.rename(backups[rollback].c_str(), finals[rollback].c_str());
                }
                error = "BACKUP_FAILED";
                return false;
            }
            backedUp++;
        }

        size_t installed = 0;
        for (; installed < count; ++installed) {
            if (!storage.rename(temps[installed].c_str(), finals[installed].c_str())) break;
        }
        if (installed != count) {
            for (size_t i = 0; i < installed; ++i) storage.remove(finals[i].c_str());
            for (size_t i = 0; i < count; ++i) {
                if (hadOriginal[i]) storage.rename(backups[i].c_str(), finals[i].c_str());
            }
            error = "INSTALL_RENAME_FAILED";
            return false;
        }

        for (size_t i = 0; i < count; ++i) storage.remove(backups[i].c_str());
        return true;
    }

    static void cleanupTemps(fs::FS &storage,
                             const String &binaryTemp,
                             const String &sourceTemp,
                             const String &mirrorTemp) {
        if (binaryTemp.length()) storage.remove(binaryTemp.c_str());
        if (sourceTemp.length()) storage.remove(sourceTemp.c_str());
        if (mirrorTemp.length()) storage.remove(mirrorTemp.c_str());
    }

    static void sendError(Stream &uart, const String &error) {
        uart.print("CITUART ERROR ");
        uart.println(error.length() ? error : "UNKNOWN");
    }
};

}
