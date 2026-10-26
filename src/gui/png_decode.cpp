// CPCSyntaxError GUI — a PNG reader. See png_decode.h.
#include "png_decode.h"

#include <cstring>

namespace cpcse {

namespace {

// ---------------------------------------------------------------- inflate (RFC 1951)

struct BitReader {
    const uint8_t* data;
    size_t size, pos = 0;
    uint32_t bits = 0;
    int count = 0;
    bool overrun = false;
    int bit() {
        if (count == 0) {
            if (pos >= size) { overrun = true; return 0; }
            bits = data[pos++];
            count = 8;
        }
        const int b = bits & 1;
        bits >>= 1;
        count--;
        return b;
    }
    int read(int n) {                       // n bits, least significant first
        int v = 0;
        for (int i = 0; i < n; i++) v |= bit() << i;
        return v;
    }
};

// A canonical Huffman code (RFC 1951 3.2.2), decoded a bit at a time.
struct Huffman {
    int counts[16] = {};
    int symbols[320] = {};
    bool build(const int* lengths, int n) {
        std::memset(counts, 0, sizeof counts);
        for (int i = 0; i < n; i++) counts[lengths[i]]++;
        counts[0] = 0;
        int offsets[16] = {};
        for (int i = 1; i < 16; i++) offsets[i] = offsets[i - 1] + counts[i - 1];
        for (int i = 0; i < n; i++) if (lengths[i]) symbols[offsets[lengths[i]]++] = i;
        return true;
    }
    int decode(BitReader& in) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            code |= in.bit();
            const int count = counts[len];
            if (code - count < first) return symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            if (in.overrun) return -1;
        }
        return -1;
    }
};

const int LENGTH_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const int LENGTH_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const int DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const int DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool inflateBlockData(BitReader& in, const Huffman& lit, const Huffman& dist, std::vector<uint8_t>& out, size_t limit) {
    for (;;) {
        int sym = lit.decode(in);
        if (sym < 0) return false;
        if (sym < 256) { out.push_back((uint8_t)sym); if (out.size() > limit) return false; continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        const int len = LENGTH_BASE[sym] + in.read(LENGTH_EXTRA[sym]);
        const int ds = dist.decode(in);
        if (ds < 0 || ds >= 30) return false;
        const size_t d = (size_t)(DIST_BASE[ds] + in.read(DIST_EXTRA[ds]));
        if (d > out.size() || in.overrun) return false;
        for (int i = 0; i < len; i++) out.push_back(out[out.size() - d]);
        if (out.size() > limit) return false;
    }
}

// zlib (RFC 1950): the 2-byte header, the deflate stream; the Adler-32 is not checked.
bool zlibInflate(const std::vector<uint8_t>& z, std::vector<uint8_t>& out, size_t limit, std::string& why) {
    if (z.size() < 2 || (z[0] & 0x0f) != 8 || ((z[0] << 8) | z[1]) % 31 != 0 || (z[1] & 0x20)) { why = "not a zlib stream"; return false; }
    BitReader in { z.data() + 2, z.size() - 2 };
    int last = 0;
    do {
        last = in.bit();
        const int type = in.read(2);
        if (type == 0) {                                     // stored
            in.count = 0;                                     // to the byte boundary
            if (in.pos + 4 > in.size) { why = "truncated stored block"; return false; }
            const int len = in.data[in.pos] | in.data[in.pos + 1] << 8;
            in.pos += 4;
            if (in.pos + (size_t)len > in.size || out.size() + (size_t)len > limit) { why = "bad stored block"; return false; }
            out.insert(out.end(), in.data + in.pos, in.data + in.pos + len);
            in.pos += (size_t)len;
        } else if (type == 1) {                              // fixed Huffman
            int lengths[320];
            for (int i = 0; i < 144; i++) lengths[i] = 8;
            for (int i = 144; i < 256; i++) lengths[i] = 9;
            for (int i = 256; i < 280; i++) lengths[i] = 7;
            for (int i = 280; i < 288; i++) lengths[i] = 8;
            Huffman lit, dist;
            lit.build(lengths, 288);
            for (int i = 0; i < 30; i++) lengths[i] = 5;
            dist.build(lengths, 30);
            if (!inflateBlockData(in, lit, dist, out, limit)) { why = "bad fixed block"; return false; }
        } else if (type == 2) {                              // dynamic Huffman
            const int hlit = in.read(5) + 257, hdist = in.read(5) + 1, hclen = in.read(4) + 4;
            static const int ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            int codeLengths[19] = {};
            for (int i = 0; i < hclen; i++) codeLengths[ORDER[i]] = in.read(3);
            Huffman cl;
            cl.build(codeLengths, 19);
            int lengths[320] = {};
            int n = 0;
            while (n < hlit + hdist) {
                const int sym = cl.decode(in);
                if (sym < 0) { why = "bad code lengths"; return false; }
                if (sym < 16) { lengths[n++] = sym; continue; }
                int repeat = 0, value = 0;
                if (sym == 16) { if (n == 0) { why = "bad repeat"; return false; } value = lengths[n - 1]; repeat = 3 + in.read(2); }
                else if (sym == 17) repeat = 3 + in.read(3);
                else repeat = 11 + in.read(7);
                if (n + repeat > hlit + hdist) { why = "bad repeat"; return false; }
                while (repeat--) lengths[n++] = value;
            }
            Huffman lit, dist;
            lit.build(lengths, hlit);
            dist.build(lengths + hlit, hdist);
            if (!inflateBlockData(in, lit, dist, out, limit)) { why = "bad dynamic block"; return false; }
        } else { why = "bad block type"; return false; }
        if (in.overrun) { why = "truncated stream"; return false; }
    } while (!last);
    return true;
}

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
} // namespace

bool decodePng(const std::vector<uint8_t>& file, int& width, int& height, std::vector<uint8_t>& rgba, std::string& why) {
    static const uint8_t SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (file.size() < 8 || std::memcmp(file.data(), SIG, 8) != 0) { why = "not a PNG file"; return false; }
    int depth = 0, type = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    bool header = false;
    for (size_t at = 8; at + 12 <= file.size();) {
        const uint32_t len = be32(&file[at]);
        if (len > file.size() - at - 12) { why = "truncated chunk"; return false; }
        const char* t = (const char*)&file[at + 4];
        const uint8_t* d = &file[at + 8];
        if (!std::memcmp(t, "IHDR", 4) && len >= 13) {
            width = (int)be32(d); height = (int)be32(d + 4);
            depth = d[8]; type = d[9]; interlace = d[12];
            header = true;
        } else if (!std::memcmp(t, "PLTE", 4)) palette.assign(d, d + len);
        else if (!std::memcmp(t, "tRNS", 4)) trns.assign(d, d + len);
        else if (!std::memcmp(t, "IDAT", 4)) idat.insert(idat.end(), d, d + len);
        else if (!std::memcmp(t, "IEND", 4)) break;
        at += 12 + len;
    }
    if (!header || width <= 0 || height <= 0 || width > 16384 || height > 16384) { why = "no usable IHDR"; return false; }
    const int channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    const bool depthOk = type == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                       : type == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8) : (depth == 8 || depth == 16);
    if (!channels || !depthOk) { why = "unsupported colour type / depth"; return false; }
    if (type == 3 && palette.empty()) { why = "palette image with no PLTE"; return false; }
    const int bitsPerPixel = channels * depth;
    const int bytesPerPixel = (bitsPerPixel + 7) / 8;

    // the passes: Adam7's seven, or one
    struct Pass { int x0, y0, dx, dy; };
    static const Pass ADAM7[7] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
    static const Pass ONE[1] = { { 0, 0, 1, 1 } };
    const Pass* passes = interlace ? ADAM7 : ONE;
    const int passCount = interlace ? 7 : 1;
    size_t expected = 0;
    for (int p = 0; p < passCount; p++) {
        const int pw = (width - passes[p].x0 + passes[p].dx - 1) / passes[p].dx, ph = (height - passes[p].y0 + passes[p].dy - 1) / passes[p].dy;
        if (pw > 0 && ph > 0) expected += (size_t)ph * (1 + ((size_t)pw * bitsPerPixel + 7) / 8);
    }
    std::vector<uint8_t> raw;
    if (!zlibInflate(idat, raw, expected + 16, why)) return false;
    if (raw.size() < expected) { why = "image data too short"; return false; }

    rgba.assign((size_t)width * height * 4, 0);
    size_t pos = 0;
    for (int p = 0; p < passCount; p++) {
        const Pass& ps = passes[p];
        const int pw = (width - ps.x0 + ps.dx - 1) / ps.dx, ph = (height - ps.y0 + ps.dy - 1) / ps.dy;
        if (pw <= 0 || ph <= 0) continue;
        const size_t stride = ((size_t)pw * bitsPerPixel + 7) / 8;
        std::vector<uint8_t> prev(stride, 0), cur(stride);
        for (int y = 0; y < ph; y++) {
            const int filter = raw[pos++];
            for (size_t i = 0; i < stride; i++) {
                const int x = raw[pos + i];
                const int a = i >= (size_t)bytesPerPixel ? cur[i - bytesPerPixel] : 0;
                const int b = prev[i];
                const int c = i >= (size_t)bytesPerPixel ? prev[i - bytesPerPixel] : 0;
                int v = x;
                if (filter == 1) v = x + a;
                else if (filter == 2) v = x + b;
                else if (filter == 3) v = x + ((a + b) >> 1);
                else if (filter == 4) v = x + paeth(a, b, c);
                else if (filter != 0) { why = "bad row filter"; return false; }
                cur[i] = (uint8_t)v;
            }
            pos += stride;
            for (int x = 0; x < pw; x++) {
                auto sample = [&](int ch) -> int {             // one sample, scaled to 8 bits
                    if (depth == 8) return cur[(size_t)x * channels + ch];
                    if (depth == 16) return cur[((size_t)x * channels + ch) * 2];
                    const int bitPos = x * depth;
                    const int v = (cur[(size_t)bitPos / 8] >> (8 - depth - bitPos % 8)) & ((1 << depth) - 1);
                    return type == 3 ? v : v * 255 / ((1 << depth) - 1);
                };
                auto rawSample = [&](int ch) -> int {          // the sample as stored (tRNS compares those)
                    if (depth == 16) return cur[((size_t)x * channels + ch) * 2] << 8 | cur[((size_t)x * channels + ch) * 2 + 1];
                    if (depth == 8) return cur[(size_t)x * channels + ch];
                    const int bitPos = x * depth;
                    return (cur[(size_t)bitPos / 8] >> (8 - depth - bitPos % 8)) & ((1 << depth) - 1);
                };
                uint8_t* o = &rgba[((size_t)(ps.y0 + y * ps.dy) * width + (ps.x0 + x * ps.dx)) * 4];
                if (type == 3) {
                    const int idx = sample(0);
                    if ((size_t)idx * 3 + 2 < palette.size()) { o[0] = palette[idx * 3]; o[1] = palette[idx * 3 + 1]; o[2] = palette[idx * 3 + 2]; }
                    o[3] = (size_t)idx < trns.size() ? trns[idx] : 255;
                } else if (type == 0 || type == 4) {
                    o[0] = o[1] = o[2] = (uint8_t)sample(0);
                    o[3] = type == 4 ? (uint8_t)sample(1) : 255;
                    if (type == 0 && trns.size() >= 2 && rawSample(0) == (trns[0] << 8 | trns[1])) o[3] = 0;
                } else {
                    o[0] = (uint8_t)sample(0); o[1] = (uint8_t)sample(1); o[2] = (uint8_t)sample(2);
                    o[3] = type == 6 ? (uint8_t)sample(3) : 255;
                    if (type == 2 && trns.size() >= 6 && rawSample(0) == (trns[0] << 8 | trns[1]) &&
                        rawSample(1) == (trns[2] << 8 | trns[3]) && rawSample(2) == (trns[4] << 8 | trns[5])) o[3] = 0;
                }
            }
            prev.swap(cur);
        }
    }
    return true;
}

} // namespace cpcse
