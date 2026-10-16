// Minimal PNG decoder for texture loading: zlib/inflate (stored, fixed and dynamic Huffman blocks), the five scanline
// filters, 8 and 16 bit samples, grey / grey+alpha / RGB / RGBA / palette. No interlacing (Adam7), no gamma or colour-profile
// handling (the caller decides what the numbers mean: sRGB for colours, linear for data). Output is always 8 bit RGB.
// Self-made like the JSON parser, so the renderer still depends on SDL only for its window.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace png {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;   // w * h * 3, row 0 = top
};

// ---- inflate (RFC 1951) ----
struct BitReader {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t buf = 0;
    int n = 0;
    uint32_t bits(int k) {
        while (n < k) {
            if (p >= end) throw std::runtime_error("png: unexpected end of compressed data");
            buf |= (uint32_t)*p++ << n;
            n += 8;
        }
        uint32_t v = buf & ((1u << k) - 1);
        buf >>= k; n -= k;
        return v;
    }
    void alignByte() { buf = 0; n = 0; }
};

struct Huffman {
    uint16_t count[16] = {0};
    uint16_t symbol[320] = {0};
    void build(const uint8_t* lengths, int num) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < num; i++) count[lengths[i]]++;
        count[0] = 0;
        uint16_t offs[16];
        offs[1] = 0;
        for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + count[i];
        for (int i = 0; i < num; i++) if (lengths[i]) symbol[offs[lengths[i]]++] = (uint16_t)i;
    }
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= (int)br.bits(1);
            int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
        }
        throw std::runtime_error("png: bad Huffman code");
    }
};

inline void inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint16_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint16_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    BitReader br{src, src + n};
    for (bool last = false; !last;) {
        last = br.bits(1) != 0;
        uint32_t type = br.bits(2);
        if (type == 0) {
            br.alignByte();
            if (br.p + 4 > br.end) throw std::runtime_error("png: truncated stored block");
            uint32_t len = br.p[0] | (br.p[1] << 8);
            br.p += 4;
            if (br.p + len > br.end) throw std::runtime_error("png: truncated stored block");
            out.insert(out.end(), br.p, br.p + len);
            br.p += len;
        } else if (type == 1 || type == 2) {
            Huffman lit, dist;
            if (type == 1) {
                uint8_t l[288];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                lit.build(l, 288);
                uint8_t d[30];
                for (int i = 0; i < 30; i++) d[i] = 5;
                dist.build(d, 30);
            } else {
                int nlen = (int)br.bits(5) + 257, ndist = (int)br.bits(5) + 1, ncode = (int)br.bits(4) + 4;
                static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl[19] = {0};
                for (int i = 0; i < ncode; i++) cl[order[i]] = (uint8_t)br.bits(3);
                Huffman clh; clh.build(cl, 19);
                uint8_t lens[320] = {0};
                for (int i = 0; i < nlen + ndist;) {
                    int sym = clh.decode(br);
                    if (sym < 16) lens[i++] = (uint8_t)sym;
                    else {
                        int rep, val = 0;
                        if (sym == 16) { if (i == 0) throw std::runtime_error("png: bad repeat"); val = lens[i - 1]; rep = 3 + (int)br.bits(2); }
                        else if (sym == 17) rep = 3 + (int)br.bits(3);
                        else rep = 11 + (int)br.bits(7);
                        if (i + rep > nlen + ndist) throw std::runtime_error("png: bad code lengths");
                        while (rep--) lens[i++] = (uint8_t)val;
                    }
                }
                lit.build(lens, nlen);
                dist.build(lens + nlen, ndist);
            }
            for (;;) {
                int sym = lit.decode(br);
                if (sym < 256) out.push_back((uint8_t)sym);
                else if (sym == 256) break;
                else {
                    sym -= 257;
                    if (sym >= 29) throw std::runtime_error("png: bad length symbol");
                    int len = lbase[sym] + (int)br.bits(lext[sym]);
                    int ds = dist.decode(br);
                    if (ds >= 30) throw std::runtime_error("png: bad distance symbol");
                    size_t d = dbase[ds] + br.bits(dext[ds]);
                    if (d > out.size()) throw std::runtime_error("png: distance too far back");
                    size_t from = out.size() - d;
                    for (int k = 0; k < len; k++) out.push_back(out[from + k]);   // may overlap: copy byte by byte
                }
            }
        } else throw std::runtime_error("png: bad block type");
    }
}

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Decodes a PNG file held in memory. Throws std::runtime_error with a message on anything unsupported or damaged.
inline Image decode(const uint8_t* d, size_t n) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8 || std::memcmp(d, sig, 8) != 0) throw std::runtime_error("png: not a PNG file (only PNG textures are supported)");
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, palette;
    for (size_t off = 8; off + 12 <= n;) {
        uint32_t len = be32(d + off);
        if (off + 12 + len > n) throw std::runtime_error("png: truncated chunk");
        const uint8_t* body = d + off + 8;
        if (!std::memcmp(d + off + 4, "IHDR", 4)) {
            w = (int)be32(body); h = (int)be32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12];
        } else if (!std::memcmp(d + off + 4, "PLTE", 4)) palette.assign(body, body + len);
        else if (!std::memcmp(d + off + 4, "IDAT", 4)) idat.insert(idat.end(), body, body + len);
        else if (!std::memcmp(d + off + 4, "IEND", 4)) break;
        off += 12 + len;
    }
    if (w <= 0 || h <= 0) throw std::runtime_error("png: missing header");
    if (interlace) throw std::runtime_error("png: interlaced PNG is not supported");
    if (depth != 8 && depth != 16) throw std::runtime_error("png: only 8 and 16 bit samples are supported");
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels) throw std::runtime_error("png: bad colour type");
    if (ctype == 3 && depth != 8) throw std::runtime_error("png: palette images must be 8 bit");
    if (idat.size() < 6) throw std::runtime_error("png: no image data");
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * ((size_t)w * channels * (depth / 8) + 1));
    inflate(idat.data() + 2, idat.size() - 2, raw);                  // skip the 2-byte zlib header; the Adler-32 at the end is not checked
    const int bpp = channels * (depth / 8);
    const size_t stride = (size_t)w * bpp;
    if (raw.size() < (stride + 1) * (size_t)h) throw std::runtime_error("png: image data too short");
    std::vector<uint8_t> prev(stride, 0), cur(stride);
    Image img; img.w = w; img.h = h; img.rgb.resize((size_t)w * h * 3);
    for (int y = 0; y < h; y++) {
        const uint8_t* line = &raw[(size_t)y * (stride + 1)];
        int ft = line[0];
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = line[1 + i], v;
            switch (ft) {
                case 0: v = x; break;
                case 1: v = x + a; break;
                case 2: v = x + b; break;
                case 3: v = x + ((a + b) >> 1); break;
                case 4: {
                    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    v = x + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c));
                    break;
                }
                default: throw std::runtime_error("png: bad filter type");
            }
            cur[i] = (uint8_t)v;
        }
        for (int x = 0; x < w; x++) {
            const uint8_t* s = &cur[(size_t)x * bpp];
            uint8_t* o = &img.rgb[((size_t)y * w + x) * 3];
            int step = depth / 8;                                   // 16 bit: keep the high byte
            if (ctype == 3) {
                size_t pi = s[0];
                if (pi * 3 + 2 >= palette.size()) throw std::runtime_error("png: palette index out of range");
                o[0] = palette[pi * 3]; o[1] = palette[pi * 3 + 1]; o[2] = palette[pi * 3 + 2];
            } else if (channels < 3) { o[0] = o[1] = o[2] = s[0]; }
            else { o[0] = s[0]; o[1] = s[step]; o[2] = s[2 * step]; }
        }
        prev.swap(cur);
    }
    return img;
}

}  // namespace png
