#pragma once

// The FFmpeg API mod writes the AAC audio track without its decoder config
// (AudioSpecificConfig), so most players (Windows, Discord, browsers) can't play
// the sound. This inserts the missing config into the MP4's esds box.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fw::mp4 {

inline uint32_t rd32(std::vector<uint8_t> const& d, size_t p) {
    return (uint32_t(d[p]) << 24) | (uint32_t(d[p + 1]) << 16) | (uint32_t(d[p + 2]) << 8) | uint32_t(d[p + 3]);
}
inline void wr32(std::vector<uint8_t>& d, size_t p, uint32_t v) {
    d[p] = uint8_t(v >> 24); d[p + 1] = uint8_t(v >> 16); d[p + 2] = uint8_t(v >> 8); d[p + 3] = uint8_t(v);
}

struct Box { size_t pos; size_t size; std::string type; size_t header; };

inline bool readBox(std::vector<uint8_t> const& d, size_t pos, size_t end, Box& out) {
    if (pos + 8 > end) return false;
    uint64_t size = rd32(d, pos);
    size_t header = 8;
    if (size == 1) {
        if (pos + 16 > end) return false;
        size = (uint64_t(rd32(d, pos + 8)) << 32) | rd32(d, pos + 12);
        header = 16;
    }
    else if (size == 0) {
        size = end - pos;
    }
    if (size < header || pos + size > end) return false;
    out = {pos, static_cast<size_t>(size), std::string(reinterpret_cast<char const*>(&d[pos + 4]), 4), header};
    return true;
}

// MPEG-4 descriptor: tag, then a length in 1-4 bytes (7 bits each)
struct Desc { size_t pos; uint8_t tag; size_t lenPos; size_t lenBytes; size_t len; size_t body; };

inline bool readDesc(std::vector<uint8_t> const& d, size_t pos, size_t end, Desc& out) {
    if (pos + 2 > end) return false;
    out.pos = pos;
    out.tag = d[pos];
    out.lenPos = pos + 1;
    size_t len = 0, i = 0;
    for (; i < 4 && pos + 1 + i < end; ++i) {
        uint8_t b = d[pos + 1 + i];
        len = (len << 7) | (b & 0x7f);
        if (!(b & 0x80)) { ++i; break; }
    }
    out.lenBytes = i;
    out.len = len;
    out.body = pos + 1 + i;
    return out.body + len <= end;
}

inline void writeLen(std::vector<uint8_t>& d, Desc const& ds, size_t newLen) {
    // keep the same number of length bytes
    for (size_t i = 0; i < ds.lenBytes; ++i) {
        size_t shift = 7 * (ds.lenBytes - 1 - i);
        uint8_t b = uint8_t((newLen >> shift) & 0x7f);
        if (i + 1 < ds.lenBytes) b |= 0x80;
        d[ds.lenPos + i] = b;
    }
}

inline int freqIndex(int rate) {
    static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    for (int i = 0; i < 13; ++i) if (rates[i] == rate) return i;
    return 4;
}

// returns true if the file was changed
inline bool fixAacEsds(std::vector<uint8_t>& d, int sampleRate = 44100, int channels = 2) {
    std::vector<Box> chain;        // ancestors of the esds box
    Box esds{};
    bool found = false;
    size_t mdatPos = SIZE_MAX, moovPos = SIZE_MAX;

    // depth-first search for moov/trak/mdia/minf/stbl/stsd/mp4a/esds
    auto search = [&](auto&& self, size_t start, size_t end) -> void {
        size_t pos = start;
        Box b;
        while (!found && readBox(d, pos, end, b)) {
            if (b.type == "mdat" && mdatPos == SIZE_MAX) mdatPos = b.pos;
            if (b.type == "moov") moovPos = b.pos;
            size_t body = b.pos + b.header;
            size_t bend = b.pos + b.size;
            if (b.type == "moov" || b.type == "trak" || b.type == "mdia" || b.type == "minf" || b.type == "stbl") {
                chain.push_back(b);
                self(self, body, bend);
                if (!found) chain.pop_back();
            }
            else if (b.type == "stsd") {
                chain.push_back(b);
                self(self, body + 8, bend); // version/flags + entry count
                if (!found) chain.pop_back();
            }
            else if (b.type == "mp4a") {
                chain.push_back(b);
                self(self, body + 28, bend); // audio sample entry fields
                if (!found) chain.pop_back();
            }
            else if (b.type == "esds") {
                esds = b;
                found = true;
                return;
            }
            pos = bend;
        }
    };
    search(search, 0, d.size());
    if (!found) return false;

    // esds: version/flags, then ES_Descriptor(03) > DecoderConfigDescriptor(04) [> DecoderSpecificInfo(05)]
    size_t esdsBody = esds.pos + esds.header + 4;
    size_t esdsEnd = esds.pos + esds.size;
    Desc es{};
    if (!readDesc(d, esdsBody, esdsEnd, es) || es.tag != 0x03) return false;
    size_t p = es.body + 3; // ES_ID + flags
    uint8_t flags = d[es.body + 2];
    if (flags & 0x80) p += 2;
    if (flags & 0x40) p += 1 + d[p];
    if (flags & 0x20) p += 2;
    Desc dc{};
    if (!readDesc(d, p, es.body + es.len, dc) || dc.tag != 0x04) return false;
    // already has a DecoderSpecificInfo?
    size_t q = dc.body + 13;
    Desc sub{};
    while (q < dc.body + dc.len && readDesc(d, q, dc.body + dc.len, sub)) {
        if (sub.tag == 0x05) return false;
        q = sub.body + sub.len;
    }

    // AAC-LC AudioSpecificConfig
    uint16_t asc = uint16_t((2 << 11) | (freqIndex(sampleRate) << 7) | ((channels & 0xf) << 3));
    std::vector<uint8_t> ins = {0x05, 0x80, 0x80, 0x80, 0x02, uint8_t(asc >> 8), uint8_t(asc & 0xff)};
    size_t at = dc.body + dc.len; // end of the decoder config
    size_t delta = ins.size();

    writeLen(d, dc, dc.len + delta);
    writeLen(d, es, es.len + delta);
    d.insert(d.begin() + at, ins.begin(), ins.end());
    wr32(d, esds.pos, uint32_t(esds.size + delta));
    for (auto const& b : chain) {
        if (b.header == 8) wr32(d, b.pos, uint32_t(b.size + delta));
    }

    // if the movie header comes before the media data, chunk offsets move too
    if (moovPos != SIZE_MAX && mdatPos != SIZE_MAX && moovPos < mdatPos) {
        auto fixOffsets = [&](auto&& self, size_t start, size_t end) -> void {
            Box b;
            size_t pos = start;
            while (readBox(d, pos, end, b)) {
                size_t body = b.pos + b.header;
                if (b.type == "moov" || b.type == "trak" || b.type == "mdia" || b.type == "minf" || b.type == "stbl") {
                    self(self, body, b.pos + b.size);
                }
                else if (b.type == "stco") {
                    uint32_t n = rd32(d, body + 4);
                    for (uint32_t i = 0; i < n; ++i) wr32(d, body + 8 + 4 * i, rd32(d, body + 8 + 4 * i) + uint32_t(delta));
                }
                pos = b.pos + b.size;
            }
        };
        fixOffsets(fixOffsets, 0, d.size());
    }
    return true;
}

} // namespace fw::mp4
