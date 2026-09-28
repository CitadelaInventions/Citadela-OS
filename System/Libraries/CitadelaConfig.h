#pragma once

#include <Arduino.h>
#include <FS.h>

namespace Citadela {

class ConfigFile {
  public:
    static bool boolValue(const String &value) {
        String v = value;
        v.trim();
        v.toLowerCase();
        return v == "1" || v == "true" || v == "on" || v == "yes";
    }

    static int intValue(const String &value, int minValue, int maxValue) {
        return constrain(value.toInt(), minValue, maxValue);
    }

    template <typename Handler>
    static bool read(fs::FS &fs, const char *path, Handler handler) {
        if (!fs.exists(path)) return false;

        File file = fs.open(path, FILE_READ);
        if (!file) return false;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line.charAt(0) == '#') continue;

            int eq = line.indexOf('=');
            if (eq < 1) continue;

            String key = line.substring(0, eq);
            String value = line.substring(eq + 1);
            key.trim();
            value.trim();
            handler(key, value);
        }

        file.close();
        return true;
    }
};

class ConfigWriter {
  public:
    bool begin(fs::FS &fs, const char *path) {
        file = fs.open(path, FILE_WRITE);
        return file;
    }

    bool Begin(fs::FS &fs, const char *path) {
        return begin(fs, path);
    }

    void boolValue(const char *key, bool value) {
        if (file) file.printf("%s=%d\n", key, value ? 1 : 0);
    }

    void Bool(const char *key, bool value) {
        boolValue(key, value);
    }

    void intValue(const char *key, int value) {
        if (file) file.printf("%s=%d\n", key, value);
    }

    void Int(const char *key, int value) {
        intValue(key, value);
    }

    void stringValue(const char *key, const String &value) {
        if (!file) return;
        String clean = value;
        clean.replace("\r", "");
        clean.replace("\n", "");
        file.printf("%s=%s\n", key, clean.c_str());
    }

    void StringValue(const char *key, const String &value) {
        stringValue(key, value);
    }

    void end() {
        if (file) file.close();
    }

    void End() {
        end();
    }

  private:
    File file;
};

}
