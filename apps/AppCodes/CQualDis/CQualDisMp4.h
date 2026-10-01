#pragma once

#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#ifdef ARDUINO
#include <FS.h>
#endif

// A constant-memory reader for ordinary (non-fragmented) MP4 files whose video
// samples are complete JPEG images. The input File is owned by the caller. The
// parser seeks into MP4 sample tables on demand rather than copying them to RAM.
// H.264/AVC and ordinary MPEG-4 Part 2 video are deliberately rejected.
namespace CQualDisMp4 {

enum class Error : uint8_t {
    None,
    Io,
    InvalidContainer,
    NoVideoTrack,
    UnsupportedCodec,
    UnsupportedLayout,
    InvalidSample,
    EndOfVideo
};

struct Metadata {
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t timescale = 0;
    uint32_t frameCount = 0;
    uint32_t durationMs = 0;
    float fps = 0.0f;
};

struct Sample {
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t ptsMs = 0;
};

class Reader {
public:
    bool open(File &file, Metadata &out) {
        clear();
        out = Metadata{};
        const uint64_t length = static_cast<uint64_t>(file.size());
        // ESP32 File::seek uses a 32-bit position. Reject files it cannot seek.
        if (length < 8) return fail(Error::InvalidContainer);
        if (length > UINT32_MAX) return fail(Error::UnsupportedLayout);
        fileSize_ = static_cast<uint32_t>(length);

        Box moov{};
        bool haveMoov = false;
        bool haveMdat = false;
        for (uint32_t pos = 0; pos < fileSize_;) {
            Box box{};
            if (!readBox(file, pos, fileSize_, box)) return false;
            if (box.type == tag('m','o','o','v')) {
                if (haveMoov) return fail(Error::UnsupportedLayout);
                moov = box;
                haveMoov = true;
            } else if (box.type == tag('m','d','a','t')) {
                haveMdat = true;
                if (!firstMdatEnd_) {
                    firstMdatBegin_ = box.payload;
                    firstMdatEnd_ = box.end;
                }
            }
            pos = box.end;
        }
        if (!haveMoov || !haveMdat) return fail(Error::InvalidContainer);

        bool foundVideo = false;
        bool foundUnsupportedVideo = false;
        for (uint32_t pos = moov.payload; pos < moov.end;) {
            Box box{};
            if (!readBox(file, pos, moov.end, box)) return false;
            if (box.type == tag('t','r','a','k')) {
                Track candidate{};
                if (!parseTrack(file, box, candidate)) return false;
                if (candidate.video) {
                    foundVideo = true;
                    if (candidate.jpegCodec) {
                        if (!validateTrack(file, candidate)) return false;
                        track_ = candidate;
                        metadata_.width = candidate.width;
                        metadata_.height = candidate.height;
                        metadata_.timescale = candidate.timescale;
                        metadata_.frameCount = candidate.sampleCount;
                        metadata_.durationMs = static_cast<uint32_t>(
                            (candidate.durationUnits * 1000ULL) / candidate.timescale);
                        metadata_.fps = static_cast<float>(
                            (static_cast<double>(candidate.sampleCount) *
                             candidate.timescale) / candidate.durationUnits);
                        rewind();
                        error_ = Error::None;
                        out = metadata_;
                        return true;
                    }
                    foundUnsupportedVideo = true;
                }
            }
            pos = box.end;
        }
        if (foundUnsupportedVideo) return fail(Error::UnsupportedCodec);
        return fail(foundVideo ? Error::UnsupportedLayout : Error::NoVideoTrack);
    }

    // A sample is a complete JPEG bitstream. The caller may seek to offset and
    // read size bytes, then call nextSample again. Each call verifies JPEG SOI.
    bool nextSample(File &file, Sample &out) {
        out = Sample{};
        if (error_ != Error::None && error_ != Error::EndOfVideo) return false;
        if (nextSampleIndex_ >= track_.sampleCount) return fail(Error::EndOfVideo);
        if (!track_.timescale) return fail(Error::UnsupportedLayout);

        if (!samplesLeftInChunk_) {
            if (++chunkIndex_ > track_.chunkCount)
                return fail(Error::InvalidSample);
            if (!loadChunk(file)) return false;
        }

        uint32_t size = track_.fixedSampleSize;
        if (!size && !readU32(file,
                              track_.sampleSizeTable + nextSampleIndex_ * 4ULL,
                              size)) return false;
        if (size < 4 || sampleOffset_ > UINT32_MAX ||
            static_cast<uint64_t>(size) > fileSize_ - sampleOffset_)
            return fail(Error::InvalidSample);
        if (!sampleInMdat(file, static_cast<uint32_t>(sampleOffset_), size))
            return fail(error_ == Error::None ? Error::InvalidSample : error_);

        uint8_t soi[2];
        if (!readAt(file, sampleOffset_, soi, sizeof(soi))) return false;
        if (soi[0] != 0xff || soi[1] != 0xd8)
            return fail(Error::InvalidSample);

        if (!sttsLeft_) {
            if (sttsIndex_ >= track_.sttsCount) return fail(Error::InvalidSample);
            const uint64_t entry = track_.sttsTable + sttsIndex_ * 8ULL;
            if (!readU32(file, entry, sttsLeft_) ||
                !readU32(file, entry + 4, sttsDelta_)) return false;
            if (!sttsLeft_ || !sttsDelta_) return fail(Error::InvalidSample);
            ++sttsIndex_;
        }

        const uint64_t ptsMs = (ptsUnits_ * 1000ULL) / track_.timescale;
        if (ptsMs > UINT32_MAX) return fail(Error::UnsupportedLayout);
        out.offset = static_cast<uint32_t>(sampleOffset_);
        out.size = size;
        out.ptsMs = static_cast<uint32_t>(ptsMs);

        sampleOffset_ += size;
        --samplesLeftInChunk_;
        --sttsLeft_;
        ptsUnits_ += sttsDelta_;
        ++nextSampleIndex_;
        error_ = Error::None;
        return true;
    }

    void rewind() {
        nextSampleIndex_ = 0;
        chunkIndex_ = 0;
        samplesLeftInChunk_ = 0;
        sampleOffset_ = 0;
        stscIndex_ = 0;
        sttsIndex_ = 0;
        sttsLeft_ = 0;
        sttsDelta_ = 0;
        ptsUnits_ = 0;
        error_ = Error::None;
    }

    Error lastError() const { return error_; }
    const Metadata &metadata() const { return metadata_; }
    const char *errorText() const {
        switch (error_) {
            case Error::None: return "OK";
            case Error::Io: return "SD read failed";
            case Error::InvalidContainer: return "Invalid MP4 file";
            case Error::NoVideoTrack: return "No video track";
            case Error::UnsupportedCodec: return "Convert H.264 to Citadela MJPEG";
            case Error::UnsupportedLayout: return "Unsupported MP4 layout";
            case Error::InvalidSample: return "Invalid JPEG video frame";
            case Error::EndOfVideo: return "End of video";
        }
        return "Unknown MP4 error";
    }

private:
    struct Box {
        uint32_t begin = 0;
        uint32_t payload = 0;
        uint32_t end = 0;
        uint32_t type = 0;
    };
    struct Track {
        bool video = false;
        bool jpegCodec = false;
        bool hasCompositionOffsets = false;
        uint16_t width = 0;
        uint16_t height = 0;
        uint32_t timescale = 0;
        uint64_t durationUnits = 0;
        uint32_t fixedSampleSize = 0;
        uint32_t sampleCount = 0;
        uint32_t sampleSizeTable = 0;
        uint32_t chunkTable = 0;
        uint32_t chunkCount = 0;
        uint8_t chunkEntryBytes = 0;
        uint32_t stscTable = 0;
        uint32_t stscCount = 0;
        uint32_t sttsTable = 0;
        uint32_t sttsCount = 0;
        bool haveStsd = false;
        bool haveStsz = false;
        bool haveStco = false;
        bool haveStsc = false;
        bool haveStts = false;
    };

    uint32_t fileSize_ = 0;
    uint32_t firstMdatBegin_ = 0;
    uint32_t firstMdatEnd_ = 0;
    Track track_{};
    Metadata metadata_{};
    Error error_ = Error::None;
    uint32_t nextSampleIndex_ = 0;
    uint32_t chunkIndex_ = 0;
    uint32_t samplesLeftInChunk_ = 0;
    uint64_t sampleOffset_ = 0;
    uint32_t stscIndex_ = 0;
    uint32_t sttsIndex_ = 0;
    uint32_t sttsLeft_ = 0;
    uint32_t sttsDelta_ = 0;
    uint64_t ptsUnits_ = 0;

    static constexpr uint32_t tag(char a, char b, char c, char d) {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
    }
    bool fail(Error e) { error_ = e; return false; }
    void clear() {
        fileSize_ = 0;
        firstMdatBegin_ = firstMdatEnd_ = 0;
        track_ = Track{};
        metadata_ = Metadata{};
        rewind();
    }
    bool readAt(File &file, uint64_t offset, void *data, size_t size) {
        if (offset > fileSize_ || size > fileSize_ - offset)
            return fail(Error::InvalidContainer);
        if (!file.seek(static_cast<uint32_t>(offset)) ||
            file.read(static_cast<uint8_t *>(data), size) != size)
            return fail(Error::Io);
        return true;
    }
    bool readU16(File &file, uint64_t offset, uint16_t &out) {
        uint8_t b[2];
        if (!readAt(file, offset, b, sizeof(b))) return false;
        out = (static_cast<uint16_t>(b[0]) << 8) | b[1];
        return true;
    }
    bool readU32(File &file, uint64_t offset, uint32_t &out) {
        uint8_t b[4];
        if (!readAt(file, offset, b, sizeof(b))) return false;
        out = (static_cast<uint32_t>(b[0]) << 24) |
              (static_cast<uint32_t>(b[1]) << 16) |
              (static_cast<uint32_t>(b[2]) << 8) | b[3];
        return true;
    }
    bool readU64(File &file, uint64_t offset, uint64_t &out) {
        uint32_t hi, lo;
        if (!readU32(file, offset, hi) || !readU32(file, offset + 4, lo))
            return false;
        out = (static_cast<uint64_t>(hi) << 32) | lo;
        return true;
    }
    bool readBox(File &file, uint32_t pos, uint32_t limit, Box &out) {
        if (pos > limit || limit - pos < 8) return fail(Error::InvalidContainer);
        uint32_t size, type;
        if (!readU32(file, pos, size) || !readU32(file, pos + 4, type))
            return false;
        uint32_t header = 8;
        uint64_t fullSize = size;
        if (size == 1) {
            if (limit - pos < 16 || !readU64(file, pos + 8, fullSize))
                return fail(Error::InvalidContainer);
            header = 16;
        } else if (size == 0) {
            fullSize = limit - pos;
        }
        if (fullSize < header || fullSize > limit - pos)
            return fail(Error::InvalidContainer);
        out.begin = pos;
        out.payload = pos + header;
        out.end = pos + static_cast<uint32_t>(fullSize);
        out.type = type;
        return true;
    }
    bool table(File &file, const Box &box, uint32_t fixedBytes,
               uint32_t entryBytes, uint32_t &count, uint32_t &tableStart) {
        if (box.end - box.payload < fixedBytes) return fail(Error::InvalidContainer);
        if (!readU32(file, box.payload + fixedBytes - 4, count)) return false;
        tableStart = box.payload + fixedBytes;
        if (static_cast<uint64_t>(count) * entryBytes > box.end - tableStart)
            return fail(Error::InvalidContainer);
        return true;
    }
    bool parseTrack(File &file, const Box &trak, Track &out) {
        for (uint32_t pos = trak.payload; pos < trak.end;) {
            Box box{};
            if (!readBox(file, pos, trak.end, box)) return false;
            if (box.type == tag('m','d','i','a') && !parseMdia(file, box, out))
                return false;
            pos = box.end;
        }
        return true;
    }
    bool parseMdia(File &file, const Box &mdia, Track &out) {
        Box minf{};
        for (uint32_t pos = mdia.payload; pos < mdia.end;) {
            Box box{};
            if (!readBox(file, pos, mdia.end, box)) return false;
            if (box.type == tag('m','d','h','d')) {
                if (box.end - box.payload < 4) return fail(Error::InvalidContainer);
                uint8_t version;
                if (!readAt(file, box.payload, &version, 1)) return false;
                if (version == 0) {
                    if (box.end - box.payload < 20 ||
                        !readU32(file, box.payload + 12, out.timescale))
                        return fail(Error::InvalidContainer);
                } else if (version == 1) {
                    if (box.end - box.payload < 32 ||
                        !readU32(file, box.payload + 20, out.timescale))
                        return fail(Error::InvalidContainer);
                } else return fail(Error::UnsupportedLayout);
            } else if (box.type == tag('h','d','l','r')) {
                uint32_t handler;
                if (box.end - box.payload < 12 ||
                    !readU32(file, box.payload + 8, handler))
                    return fail(Error::InvalidContainer);
                out.video = handler == tag('v','i','d','e');
            } else if (box.type == tag('m','i','n','f')) {
                minf = box;
            }
            pos = box.end;
        }
        // MP4 may place audio tracks before video. Their stsd entries have a
        // different shape, so only parse the selected video sample tables.
        if (out.video && minf.end) {
            for (uint32_t q = minf.payload; q < minf.end;) {
                Box child{};
                if (!readBox(file, q, minf.end, child)) return false;
                if (child.type == tag('s','t','b','l') &&
                    !parseStbl(file, child, out)) return false;
                q = child.end;
            }
        }
        return true;
    }
    bool parseStbl(File &file, const Box &stbl, Track &out) {
        for (uint32_t pos = stbl.payload; pos < stbl.end;) {
            Box box{};
            if (!readBox(file, pos, stbl.end, box)) return false;
            if (box.type == tag('s','t','s','d')) {
                if (out.haveStsd || !parseStsd(file, box, out))
                    return fail(out.haveStsd ? Error::UnsupportedLayout : error_);
                out.haveStsd = true;
            } else if (box.type == tag('s','t','t','s')) {
                if (out.haveStts || !table(file, box, 8, 8,
                                           out.sttsCount, out.sttsTable))
                    return fail(out.haveStts ? Error::UnsupportedLayout : error_);
                out.haveStts = true;
            } else if (box.type == tag('s','t','s','c')) {
                if (out.haveStsc || !table(file, box, 8, 12,
                                           out.stscCount, out.stscTable))
                    return fail(out.haveStsc ? Error::UnsupportedLayout : error_);
                out.haveStsc = true;
            } else if (box.type == tag('s','t','s','z')) {
                if (out.haveStsz || box.end - box.payload < 12)
                    return fail(Error::InvalidContainer);
                if (!readU32(file, box.payload + 4, out.fixedSampleSize) ||
                    !readU32(file, box.payload + 8, out.sampleCount)) return false;
                out.sampleSizeTable = box.payload + 12;
                if (!out.fixedSampleSize &&
                    static_cast<uint64_t>(out.sampleCount) * 4 >
                    box.end - out.sampleSizeTable)
                    return fail(Error::InvalidContainer);
                out.haveStsz = true;
            } else if (box.type == tag('s','t','c','o') ||
                       box.type == tag('c','o','6','4')) {
                if (out.haveStco) return fail(Error::UnsupportedLayout);
                out.chunkEntryBytes = box.type == tag('s','t','c','o') ? 4 : 8;
                if (!table(file, box, 8, out.chunkEntryBytes,
                           out.chunkCount, out.chunkTable)) return false;
                out.haveStco = true;
            } else if (box.type == tag('c','t','t','s')) {
                out.hasCompositionOffsets = true;
            }
            pos = box.end;
        }
        return true;
    }
    bool parseStsd(File &file, const Box &stsd, Track &out) {
        uint32_t count;
        if (stsd.end - stsd.payload < 8 ||
            !readU32(file, stsd.payload + 4, count)) return fail(Error::InvalidContainer);
        if (count != 1) return fail(Error::UnsupportedLayout);
        Box entry{};
        if (!readBox(file, stsd.payload + 8, stsd.end, entry)) return false;
        if (entry.end != stsd.end || entry.end - entry.begin < 86)
            return fail(Error::InvalidContainer);
        if (!readU16(file, entry.begin + 32, out.width) ||
            !readU16(file, entry.begin + 34, out.height)) return false;
        if (entry.type == tag('j','p','e','g') ||
            entry.type == tag('m','j','p','g') ||
            entry.type == tag('M','J','P','G')) {
            out.jpegCodec = true;
            return true;
        }
        if (entry.type != tag('m','p','4','v')) return true;

        // VisualSampleEntry occupies 78 bytes after its own box header.
        for (uint32_t pos = entry.begin + 86; pos < entry.end;) {
            Box child{};
            if (!readBox(file, pos, entry.end, child)) return false;
            if (child.type == tag('e','s','d','s')) {
                uint8_t oti = 0;
                if (!readEsdsObjectType(file, child, oti)) return false;
                out.jpegCodec = oti == 0x6c; // ISO/IEC 10918-1 JPEG
            }
            pos = child.end;
        }
        return true;
    }
    bool descriptor(File &file, uint32_t pos, uint32_t limit,
                    uint8_t &kind, uint32_t &payload, uint32_t &end) {
        if (pos >= limit) return fail(Error::InvalidContainer);
        if (!readAt(file, pos++, &kind, 1)) return false;
        uint32_t len = 0;
        bool complete = false;
        for (int i = 0; i < 4; ++i) {
            if (pos >= limit) return fail(Error::InvalidContainer);
            uint8_t b;
            if (!readAt(file, pos++, &b, 1)) return false;
            len = (len << 7) | (b & 0x7f);
            if (!(b & 0x80)) { complete = true; break; }
        }
        if (!complete || len > limit - pos) return fail(Error::InvalidContainer);
        payload = pos;
        end = pos + len;
        return true;
    }
    bool readEsdsObjectType(File &file, const Box &esds, uint8_t &oti) {
        if (esds.end - esds.payload < 6)
            return fail(Error::InvalidContainer);
        uint8_t kind;
        uint32_t data, end;
        if (!descriptor(file, esds.payload + 4, esds.end,
                        kind, data, end)) return false;
        if (kind != 0x03 || end - data < 3)
            return fail(Error::UnsupportedLayout);
        uint8_t flags;
        if (!readAt(file, data + 2, &flags, 1)) return false;
        uint32_t pos = data + 3;
        if (flags & 0x80) pos += 2;
        if (flags & 0x40) {
            uint8_t urlBytes;
            if (pos >= end || !readAt(file, pos++, &urlBytes, 1))
                return fail(Error::InvalidContainer);
            pos += urlBytes;
        }
        if (flags & 0x20) pos += 2;
        if (pos > end) return fail(Error::InvalidContainer);
        while (pos < end) {
            if (!descriptor(file, pos, end, kind, data, pos)) return false;
            if (kind == 0x04) {
                if (data >= pos || !readAt(file, data, &oti, 1))
                    return fail(Error::InvalidContainer);
                return true;
            }
        }
        return fail(Error::UnsupportedLayout);
    }
    bool stscEntry(File &file, const Track &t, uint32_t index,
                   uint32_t &first, uint32_t &perChunk,
                   uint32_t &description) {
        if (index >= t.stscCount) return fail(Error::InvalidContainer);
        const uint64_t pos = t.stscTable + index * 12ULL;
        return readU32(file, pos, first) &&
               readU32(file, pos + 4, perChunk) &&
               readU32(file, pos + 8, description);
    }
    bool validateTrack(File &file, Track &t) {
        if (!t.video || !t.jpegCodec || !t.haveStsd || !t.haveStsz ||
            !t.haveStco || !t.haveStsc || !t.haveStts ||
            t.hasCompositionOffsets || !t.timescale || !t.width || !t.height ||
            !t.sampleCount || !t.chunkCount || !t.stscCount || !t.sttsCount)
            return fail(Error::UnsupportedLayout);

        uint64_t samplesMapped = 0;
        uint32_t previousFirst = 0;
        uint32_t previousPerChunk = 0;
        for (uint32_t i = 0; i < t.stscCount; ++i) {
            uint32_t first, perChunk, description;
            if (!stscEntry(file, t, i, first, perChunk, description)) return false;
            if (!first || first <= previousFirst || first > t.chunkCount ||
                !perChunk || description != 1) return fail(Error::UnsupportedLayout);
            if (i) samplesMapped +=
                static_cast<uint64_t>(first - previousFirst) * previousPerChunk;
            previousFirst = first;
            previousPerChunk = perChunk;
        }
        // The first stsc run must begin with the first chunk. The table loop
        // above checks every later first_chunk is strictly increasing.
        uint32_t first, perChunk, description;
        if (!stscEntry(file, t, 0, first, perChunk, description)) return false;
        if (first != 1) return fail(Error::UnsupportedLayout);
        samplesMapped += static_cast<uint64_t>(t.chunkCount + 1ULL -
                                               previousFirst) * previousPerChunk;
        if (samplesMapped != t.sampleCount) return fail(Error::UnsupportedLayout);

        uint64_t timedSamples = 0;
        uint64_t duration = 0;
        for (uint32_t i = 0; i < t.sttsCount; ++i) {
            uint32_t count, delta;
            const uint64_t pos = t.sttsTable + i * 8ULL;
            if (!readU32(file, pos, count) || !readU32(file, pos + 4, delta))
                return false;
            if (!count || !delta || timedSamples + count > t.sampleCount)
                return fail(Error::UnsupportedLayout);
            timedSamples += count;
            const uint64_t runDuration = static_cast<uint64_t>(count) * delta;
            if (runDuration > UINT64_MAX - duration)
                return fail(Error::UnsupportedLayout);
            duration += runDuration;
        }
        if (timedSamples != t.sampleCount || !duration ||
            duration > UINT64_MAX / 1000ULL ||
            (duration * 1000ULL) / t.timescale > UINT32_MAX)
            return fail(Error::UnsupportedLayout);
        t.durationUnits = duration;
        return true;
    }
    bool loadChunk(File &file) {
        uint32_t first, perChunk, description;
        if (!stscEntry(file, track_, stscIndex_, first,
                       perChunk, description)) return false;
        while (stscIndex_ + 1 < track_.stscCount) {
            uint32_t nextFirst, nextPerChunk, nextDescription;
            if (!stscEntry(file, track_, stscIndex_ + 1, nextFirst,
                           nextPerChunk, nextDescription)) return false;
            if (nextFirst > chunkIndex_) break;
            ++stscIndex_;
            perChunk = nextPerChunk;
        }
        const uint64_t tablePos = track_.chunkTable +
                                  (chunkIndex_ - 1ULL) * track_.chunkEntryBytes;
        uint64_t offset;
        if (track_.chunkEntryBytes == 4) {
            uint32_t v;
            if (!readU32(file, tablePos, v)) return false;
            offset = v;
        } else if (!readU64(file, tablePos, offset)) return false;
        if (offset >= fileSize_) return fail(Error::InvalidSample);
        sampleOffset_ = offset;
        samplesLeftInChunk_ = perChunk;
        return true;
    }
    bool sampleInMdat(File &file, uint32_t offset, uint32_t size) {
        if (offset >= firstMdatBegin_ && offset <= firstMdatEnd_ &&
            size <= firstMdatEnd_ - offset) return true;
        // Rare MP4 files have several mdat boxes. Find the owning box without
        // retaining an array of intervals or making the common case slower.
        for (uint32_t pos = 0; pos < fileSize_;) {
            Box box{};
            if (!readBox(file, pos, fileSize_, box)) return false;
            if (box.type == tag('m','d','a','t') &&
                offset >= box.payload && offset <= box.end &&
                size <= box.end - offset) return true;
            pos = box.end;
        }
        return false;
    }
};

} // namespace CQualDisMp4
