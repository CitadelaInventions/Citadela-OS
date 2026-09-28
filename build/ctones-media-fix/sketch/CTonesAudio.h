#line 1 "C:\\Users\\ADMIN CONF\\Documents\\CitContents\\apps\\AppCodes\\CTones\\CTonesAudio.h"
#pragma once

#include <Arduino.h>

namespace CTonesAudio {

enum class FileType : uint8_t {
    WAV = 0,
    MP3 = 1
};

enum class Result : uint8_t {
    IDLE = 0,
    BUFFERING,
    PLAYING,
    FINISHED,
    STOPPED,
    OPEN_FAILED,
    UNSUPPORTED_FORMAT,
    DECODE_FAILED
};

struct Snapshot {
    Result result;
    bool decoderBusy;
    bool outputActive;
    bool paused;
    uint32_t bytesProcessed;
    uint32_t totalBytes;
    uint32_t sourceSampleRate;
    uint16_t underruns;
    uint8_t channels;
    uint8_t bitsPerSample;
};

bool Begin(uint8_t speakerPin);
void End();

void BeginSynth();
void Tone(uint16_t frequency, uint8_t levelPercent = 100);
void Silence();

bool StartFile(const char *path, FileType type);
void Stop();
void SetPaused(bool paused);
void SetVolume(uint8_t percent);
void SetReverb(bool enabled);

Snapshot GetSnapshot();
uint32_t SampleRate();
uint32_t CarrierRate();

}  // namespace CTonesAudio
