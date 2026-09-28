#include "CTonesAudio.h"

#include <SD.h>
#include <math.h>
#include <soc/ledc_struct.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace CTonesAudio {
namespace {

constexpr uint32_t OUTPUT_SAMPLE_RATE = 22050;
constexpr uint32_t PWM_CARRIER_RATE = 312500;
constexpr uint8_t PWM_RESOLUTION = 8;
constexpr uint8_t PWM_CHANNEL = 7;
constexpr uint16_t RING_SIZE = 4096;
constexpr uint16_t RING_MASK = RING_SIZE - 1;
constexpr uint16_t FILE_PREFILL_SAMPLES = 640;
constexpr uint16_t REVERB_SAMPLES = 1537;
constexpr size_t WAV_BUFFER_BYTES = 1536;
constexpr size_t MP3_INPUT_BYTES = 4096;
constexpr size_t FILE_PATH_BYTES = 128;
// minimp3 creates an approximately 15 KB scratch decoder on the caller's
// stack for every frame. Leave enough room for that, SD/File, and FreeRTOS.
constexpr uint32_t DECODER_TASK_STACK_BYTES = 24576;
constexpr uint32_t DECODER_STOP_TIMEOUT_MS = 2000;

static_assert((RING_SIZE & (RING_SIZE - 1)) == 0,
              "Audio ring size must be a power of two");
static_assert(sizeof(mp3dec_scratch_t) + 6144U <= DECODER_TASK_STACK_BYTES,
              "CTones decoder task stack is too small for minimp3");

enum class Source : uint8_t {
    IDLE = 0,
    SYNTH,
    FILE_AUDIO
};

static uint8_t outputPin = 0;
static bool outputReady = false;
static hw_timer_t *sampleTimer = nullptr;
static TaskHandle_t decoderTaskHandle = nullptr;

static volatile Source source = Source::IDLE;
static volatile bool paused = false;
static volatile bool stopRequested = false;
static volatile bool decoderBusy = false;
static volatile bool fileOutputStarted = false;
static volatile Result result = Result::IDLE;

static volatile uint32_t bytesProcessed = 0;
static volatile uint32_t totalBytes = 0;
static volatile uint32_t sourceSampleRate = 0;
static volatile uint16_t underruns = 0;
static volatile uint8_t sourceChannels = 0;
static volatile uint8_t sourceBits = 0;

static volatile uint16_t volumeGainQ15 = 22932;
static volatile bool reverbEnabled = false;
static volatile uint32_t synthPhaseIncrement = 0;
static volatile int32_t synthTargetQ15 = 0;
static int32_t synthEnvelopeQ15 = 0;
static uint32_t synthPhase = 0;

static int16_t sineTable[256];
static int16_t reverbBuffer[REVERB_SAMPLES];
static uint16_t reverbPosition = 0;

static int16_t sampleRing[RING_SIZE];
static volatile uint32_t ringRead = 0;
static volatile uint32_t ringWrite = 0;

static char requestedPath[FILE_PATH_BYTES];
static FileType requestedType = FileType::WAV;

static uint8_t wavBuffer[WAV_BUFFER_BYTES];
static uint8_t mp3Input[MP3_INPUT_BYTES];
static mp3d_sample_t mp3Pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static mp3dec_t mp3Decoder;

static uint32_t resampleAccumulator = 0;
static uint32_t resampleInputRate = OUTPUT_SAMPLE_RATE;
static int32_t resampleFilter = 0;

inline int16_t clampSample(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

inline uint32_t ringCount() {
    return ringWrite - ringRead;
}

void resetRing() {
    ringRead = 0;
    ringWrite = 0;
}

void activateFileOutputIfReady(bool force = false) {
    if (!fileOutputStarted && (force || ringCount() >= FILE_PREFILL_SAMPLES)) {
        fileOutputStarted = true;
        source = Source::FILE_AUDIO;
        result = Result::PLAYING;
    }
}

bool pushSample(int16_t sample) {
    while (ringCount() >= RING_SIZE - 1) {
        if (stopRequested) return false;
        activateFileOutputIfReady();
        vTaskDelay(1);
    }
    uint32_t writeIndex = ringWrite;
    sampleRing[writeIndex & RING_MASK] = sample;
    __sync_synchronize();
    ringWrite = writeIndex + 1;
    activateFileOutputIfReady();
    return !stopRequested;
}

void resetResampler(uint32_t inputRate) {
    resampleAccumulator = 0;
    resampleInputRate = inputRate ? inputRate : OUTPUT_SAMPLE_RATE;
    resampleFilter = 0;
}

bool feedSourceSample(int16_t sample) {
    int32_t filtered = sample;
    if (resampleInputRate > OUTPUT_SAMPLE_RATE) {
        resampleFilter += ((int32_t)sample - resampleFilter) >> 1;
        filtered = resampleFilter;
    } else {
        resampleFilter = sample;
    }

    resampleAccumulator += OUTPUT_SAMPLE_RATE;
    while (resampleAccumulator >= resampleInputRate) {
        if (!pushSample((int16_t)filtered)) return false;
        resampleAccumulator -= resampleInputRate;
    }
    return true;
}

void ARDUINO_ISR_ATTR writePwmDuty(uint8_t duty) {
    LEDC.channel_group[0].channel[PWM_CHANNEL].duty.duty =
        ((uint32_t)duty) << 4;
    LEDC.channel_group[0].channel[PWM_CHANNEL].conf1.duty_start = 1;
}

void ARDUINO_ISR_ATTR onAudioSample() {
    if (paused) {
        writePwmDuty(128);
        return;
    }

    int32_t sample = 0;
    Source activeSource = source;
    if (activeSource == Source::FILE_AUDIO) {
        uint32_t readIndex = ringRead;
        if (readIndex != ringWrite) {
            sample = sampleRing[readIndex & RING_MASK];
            ringRead = readIndex + 1;
        } else if (decoderBusy) {
            ++underruns;
        }
    } else if (activeSource == Source::SYNTH) {
        int32_t target = synthTargetQ15;
        if (synthEnvelopeQ15 < target) {
            synthEnvelopeQ15 += 1024;
            if (synthEnvelopeQ15 > target) synthEnvelopeQ15 = target;
        } else if (synthEnvelopeQ15 > target) {
            synthEnvelopeQ15 -= 256;
            if (synthEnvelopeQ15 < target) synthEnvelopeQ15 = target;
        }
        synthPhase += synthPhaseIncrement;
        sample = ((int32_t)sineTable[synthPhase >> 24] * synthEnvelopeQ15) >> 15;
    } else if (synthEnvelopeQ15 > 0) {
        synthEnvelopeQ15 -= 384;
        if (synthEnvelopeQ15 < 0) synthEnvelopeQ15 = 0;
    }

    int32_t delayed = reverbBuffer[reverbPosition];
    if (reverbEnabled) {
        int32_t mixed = sample + ((delayed * 3) >> 3);
        int32_t feedback = sample + ((delayed * 5) >> 4);
        reverbBuffer[reverbPosition] = clampSample(feedback);
        sample = clampSample(mixed);
    } else {
        reverbBuffer[reverbPosition] = 0;
    }
    if (++reverbPosition >= REVERB_SAMPLES) reverbPosition = 0;

    int32_t scaled = (sample * (int32_t)volumeGainQ15) >> 15;
    int32_t duty = 128 + (scaled >> 8);
    if (duty < 8) duty = 8;
    if (duty > 247) duty = 247;
    writePwmDuty((uint8_t)duty);
}

uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t readLE32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

bool readExact(File &file, uint8_t *destination, size_t length) {
    return file.read(destination, length) == (int)length;
}

int16_t decodePcmSample(const uint8_t *data, uint8_t bits) {
    if (bits == 8) return (int16_t)(((int)data[0] - 128) << 8);
    if (bits == 16) return (int16_t)readLE16(data);
    if (bits == 24) {
        int32_t value = (int32_t)data[0] |
                        ((int32_t)data[1] << 8) |
                        ((int32_t)data[2] << 16);
        if (value & 0x00800000L) value |= 0xFF000000L;
        return (int16_t)(value >> 8);
    }
    if (bits == 32) return (int16_t)((int32_t)readLE32(data) >> 16);
    return 0;
}

bool decodeWav(File &file) {
    uint8_t header[12];
    if (!readExact(file, header, sizeof(header)) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        result = Result::UNSUPPORTED_FORMAT;
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t blockAlign = 0;
    uint32_t sampleRate = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
    bool foundFormat = false;

    while (file.available()) {
        uint8_t chunkHeader[8];
        if (!readExact(file, chunkHeader, sizeof(chunkHeader))) break;
        uint32_t chunkSize = readLE32(chunkHeader + 4);
        uint32_t chunkData = file.position();
        uint32_t nextChunk = chunkData + chunkSize + (chunkSize & 1U);

        if (memcmp(chunkHeader, "fmt ", 4) == 0) {
            uint8_t formatData[40] = {};
            size_t formatBytes = min((uint32_t)sizeof(formatData), chunkSize);
            if (formatBytes < 16 || !readExact(file, formatData, formatBytes)) {
                result = Result::UNSUPPORTED_FORMAT;
                return false;
            }
            format = readLE16(formatData);
            channels = readLE16(formatData + 2);
            sampleRate = readLE32(formatData + 4);
            blockAlign = readLE16(formatData + 12);
            bits = readLE16(formatData + 14);
            if (format == 0xFFFE && formatBytes >= 26) format = readLE16(formatData + 24);
            foundFormat = true;
        } else if (memcmp(chunkHeader, "data", 4) == 0) {
            dataOffset = chunkData;
            dataSize = chunkSize;
        }

        if (foundFormat && dataOffset) break;
        if (!file.seek(nextChunk)) break;
    }

    uint8_t bytesPerSample = bits / 8;
    if (!foundFormat || !dataOffset || format != 1 ||
        channels < 1 || channels > 2 ||
        (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        blockAlign < channels * bytesPerSample || sampleRate < 4000) {
        result = Result::UNSUPPORTED_FORMAT;
        return false;
    }

    if (!file.seek(dataOffset)) {
        result = Result::DECODE_FAILED;
        return false;
    }

    sourceSampleRate = sampleRate;
    sourceChannels = (uint8_t)channels;
    sourceBits = (uint8_t)bits;
    totalBytes = dataSize;
    bytesProcessed = 0;
    resetResampler(sampleRate);

    uint32_t remaining = dataSize;
    while (remaining && !stopRequested) {
        size_t wanted = min((uint32_t)sizeof(wavBuffer), remaining);
        wanted -= wanted % blockAlign;
        if (!wanted) break;
        int received = file.read(wavBuffer, wanted);
        if (received <= 0) break;
        received -= received % blockAlign;

        for (int offset = 0; offset < received; offset += blockAlign) {
            int32_t sample = decodePcmSample(wavBuffer + offset, bits);
            if (channels == 2) {
                sample += decodePcmSample(wavBuffer + offset + bytesPerSample, bits);
                sample >>= 1;
            }
            if (!feedSourceSample((int16_t)sample)) return false;
        }
        remaining -= received;
        bytesProcessed = dataSize - remaining;
    }

    if (stopRequested) return false;
    return bytesProcessed > 0;
}

bool decodeMp3(File &file) {
    mp3dec_init(&mp3Decoder);
    size_t buffered = 0;
    bool reachedEnd = false;
    bool decodedFrame = false;
    uint32_t consumedTotal = 0;
    uint32_t decodeIterations = 0;

    totalBytes = file.size();
    bytesProcessed = 0;
    sourceSampleRate = 0;
    sourceChannels = 0;
    sourceBits = 16;

    while (!stopRequested) {
        if (!reachedEnd && buffered < MP3_INPUT_BYTES) {
            int received = file.read(mp3Input + buffered, MP3_INPUT_BYTES - buffered);
            if (received > 0) buffered += received;
            else reachedEnd = true;
        }
        if (!buffered) break;

        mp3dec_frame_info_t frameInfo = {};
        int samplesPerChannel = mp3dec_decode_frame(
            &mp3Decoder, mp3Input, (int)buffered, mp3Pcm, &frameInfo);

        // Invalid tags and damaged streams can contain long areas without a
        // frame. Yield while scanning so the decoder cannot starve core 0.
        if ((++decodeIterations & 15U) == 0U) vTaskDelay(1);

        size_t consumed = frameInfo.frame_bytes > 0
            ? min((size_t)frameInfo.frame_bytes, buffered)
            : 0;

        if (samplesPerChannel > 0 && frameInfo.channels >= 1 && frameInfo.hz >= 4000) {
            if (!decodedFrame || sourceSampleRate != (uint32_t)frameInfo.hz) {
                resetResampler((uint32_t)frameInfo.hz);
            }
            decodedFrame = true;
            sourceSampleRate = frameInfo.hz;
            sourceChannels = min(2, frameInfo.channels);
            sourceBits = 16;

            for (int index = 0; index < samplesPerChannel; ++index) {
                int32_t sample = mp3Pcm[index * frameInfo.channels];
                if (frameInfo.channels > 1) {
                    sample += mp3Pcm[index * frameInfo.channels + 1];
                    sample >>= 1;
                }
                if (!feedSourceSample((int16_t)sample)) return false;
            }
        }

        if (!consumed) {
            if (!reachedEnd && buffered < MP3_INPUT_BYTES) continue;
            consumed = 1;
        }
        buffered -= consumed;
        consumedTotal += consumed;
        if (buffered) memmove(mp3Input, mp3Input + consumed, buffered);
        bytesProcessed = min(consumedTotal, (uint32_t)totalBytes);
    }

    if (stopRequested) return false;
    if (!decodedFrame) {
        result = Result::DECODE_FAILED;
        return false;
    }
    return true;
}

void decoderTask(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        decoderBusy = true;
        fileOutputStarted = false;
        result = Result::BUFFERING;

        File file = SD.open(requestedPath, FILE_READ);
        if (!file || file.isDirectory()) {
            if (file) file.close();
            result = Result::OPEN_FAILED;
            decoderBusy = false;
            continue;
        }

        bool decoded = requestedType == FileType::WAV
            ? decodeWav(file)
            : decodeMp3(file);
        file.close();

        if (stopRequested) {
            source = Source::IDLE;
            result = Result::STOPPED;
            decoderBusy = false;
            continue;
        }

        if (!decoded) {
            source = Source::IDLE;
            if (result == Result::BUFFERING || result == Result::PLAYING) {
                result = Result::DECODE_FAILED;
            }
            decoderBusy = false;
            continue;
        }

        activateFileOutputIfReady(true);
        while (ringRead != ringWrite && !stopRequested) vTaskDelay(2);
        source = Source::IDLE;
        result = stopRequested ? Result::STOPPED : Result::FINISHED;
        decoderBusy = false;
    }
}

}  // namespace

bool Begin(uint8_t speakerPin) {
    if (outputReady) return true;
    outputPin = speakerPin;
    for (uint16_t index = 0; index < 256; ++index) {
        sineTable[index] = (int16_t)lroundf(
            sinf((2.0f * PI * index) / 256.0f) * 32767.0f);
    }
    memset(reverbBuffer, 0, sizeof(reverbBuffer));
    resetRing();

    if (!ledcAttachChannel(outputPin, PWM_CARRIER_RATE,
                           PWM_RESOLUTION, PWM_CHANNEL)) {
        return false;
    }
    ledcWriteChannel(PWM_CHANNEL, 128);

    sampleTimer = timerBegin(OUTPUT_SAMPLE_RATE);
    if (!sampleTimer) {
        ledcDetach(outputPin);
        return false;
    }
    timerAttachInterrupt(sampleTimer, onAudioSample);
    timerAlarm(sampleTimer, 1, true, 0);

    BaseType_t created = xTaskCreatePinnedToCore(
        decoderTask, "CTonesDecoder", DECODER_TASK_STACK_BYTES, nullptr, 2,
        &decoderTaskHandle, 0);
    if (created != pdPASS) {
        timerEnd(sampleTimer);
        sampleTimer = nullptr;
        ledcDetach(outputPin);
        return false;
    }

    outputReady = true;
    result = Result::IDLE;
    return true;
}

void End() {
    if (!outputReady) return;
    Stop();
    if (sampleTimer) {
        timerEnd(sampleTimer);
        sampleTimer = nullptr;
    }
    ledcWriteChannel(PWM_CHANNEL, 128);
    ledcDetach(outputPin);
    outputReady = false;
}

void BeginSynth() {
    Stop();
    paused = false;
    synthPhase = 0;
    synthEnvelopeQ15 = 0;
    synthTargetQ15 = 0;
    source = Source::SYNTH;
    result = Result::PLAYING;
}

void Tone(uint16_t frequency, uint8_t levelPercent) {
    if (!outputReady || !frequency) {
        Silence();
        return;
    }
    levelPercent = min((int)levelPercent, 100);
    synthPhaseIncrement = (uint32_t)(
        ((uint64_t)frequency << 32) / OUTPUT_SAMPLE_RATE);
    synthTargetQ15 = (32767L * levelPercent) / 100L;
}

void Silence() {
    synthTargetQ15 = 0;
}

bool StartFile(const char *path, FileType type) {
    if (!outputReady || !decoderTaskHandle || !path || !path[0]) return false;
    Stop();
    if (decoderBusy) return false;

    snprintf(requestedPath, sizeof(requestedPath), "%s", path);
    requestedType = type;
    stopRequested = false;
    paused = false;
    bytesProcessed = 0;
    totalBytes = 0;
    sourceSampleRate = 0;
    sourceChannels = 0;
    sourceBits = 0;
    underruns = 0;
    fileOutputStarted = false;
    result = Result::BUFFERING;
    resetRing();
    // Reserve the persistent decoder before it is scheduled so a rapid
    // second click cannot overwrite the pending path or queue another stream.
    decoderBusy = true;
    xTaskNotifyGive(decoderTaskHandle);
    return true;
}

void Stop() {
    source = Source::IDLE;
    synthTargetQ15 = 0;
    paused = false;
    stopRequested = true;
    uint32_t started = millis();
    while (decoderBusy && millis() - started < DECODER_STOP_TIMEOUT_MS) delay(2);
    resetRing();
    if (!decoderBusy && result != Result::IDLE) result = Result::STOPPED;
}

void SetPaused(bool value) {
    paused = value;
}

void SetVolume(uint8_t percent) {
    percent = min((int)percent, 100);
    // Leave headroom for the reverb mix and for direct speaker drive.
    volumeGainQ15 = (uint16_t)((uint32_t)percent * 294U);
}

void SetReverb(bool enabled) {
    reverbEnabled = enabled;
}

Snapshot GetSnapshot() {
    Snapshot snapshot;
    snapshot.result = result;
    snapshot.decoderBusy = decoderBusy;
    snapshot.outputActive = source != Source::IDLE;
    snapshot.paused = paused;
    snapshot.bytesProcessed = bytesProcessed;
    snapshot.totalBytes = totalBytes;
    snapshot.sourceSampleRate = sourceSampleRate;
    snapshot.underruns = underruns;
    snapshot.channels = sourceChannels;
    snapshot.bitsPerSample = sourceBits;
    return snapshot;
}

uint32_t SampleRate() {
    return OUTPUT_SAMPLE_RATE;
}

uint32_t CarrierRate() {
    return PWM_CARRIER_RATE;
}

}  // namespace CTonesAudio
