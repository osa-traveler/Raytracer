// Minimal image writers (no external libraries).
//   PPM  - 8 bit, what the renderer has always written
//   PNG  - 8 bit, uncompressed (stored deflate blocks): big but trivially correct, opens everywhere
//   PFM  - 32 bit float RGB, the linear film itself (lossless; for later image comparison)
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "raytracer.h"

inline std::vector<uint8_t> filmToRgb8(const Film& film, const Display& disp) {
    std::vector<uint8_t> rgb((size_t)film.w * film.h * 3);
    for (int y = 0; y < film.h; y++)
        for (int x = 0; x < film.w; x++) {
            Vec3 c = film.pixel(x, y);
            uint8_t* p = &rgb[((size_t)y * film.w + x) * 3];
            p[0] = disp.encode(c.x); p[1] = disp.encode(c.y); p[2] = disp.encode(c.z);
        }
    return rgb;
}

inline bool writePPM(const std::string& path, const Film& film, const Display& disp) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", film.w, film.h);
    auto rgb = filmToRgb8(film, disp);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    return true;
}

inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline void put32be(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

inline void pngChunk(std::FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> head;
    put32be(head, (uint32_t)data.size());
    std::fwrite(head.data(), 1, 4, f);
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    std::fwrite(body.data(), 1, body.size(), f);
    std::vector<uint8_t> crc;
    put32be(crc, crc32(body.data(), body.size()));
    std::fwrite(crc.data(), 1, 4, f);
}

inline bool writePNG(const std::string& path, const Film& film, const Display& disp) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    put32be(ihdr, film.w); put32be(ihdr, film.h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);  // 8bit RGB
    pngChunk(f, "IHDR", ihdr);

    auto rgb = filmToRgb8(film, disp);
    std::vector<uint8_t> raw;                       // each scanline starts with filter type 0
    for (int y = 0; y < film.h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * film.w * 3, rgb.begin() + (size_t)(y + 1) * film.w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};          // zlib header, then stored (uncompressed) deflate blocks
    for (size_t pos = 0; pos < raw.size(); pos += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8); z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    put32be(z, (b << 16) | a);
    pngChunk(f, "IDAT", z);
    pngChunk(f, "IEND", {});
    std::fclose(f);
    return true;
}

// PFM: "PF", width height, scale (negative = little endian), rows stored bottom to top.
inline bool writePFM(const std::string& path, const Film& film) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "PF\n%d %d\n-1.0\n", film.w, film.h);
    for (int y = film.h - 1; y >= 0; y--)
        for (int x = 0; x < film.w; x++) {
            Vec3 c = film.pixel(x, y);
            float px[3] = {c.x, c.y, c.z};
            std::fwrite(px, sizeof(float), 3, f);
        }
    std::fclose(f);
    return true;
}

// Reads a PFM written by writePFM (little endian RGB float). Returns false on any mismatch.
inline bool readPFM(const std::string& path, int& w, int& h, std::vector<float>& data) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[3] = {0};
    float scale = 0;
    bool ok = std::fscanf(f, "%2s %d %d %f", magic, &w, &h, &scale) == 4 && magic[0] == 'P' && magic[1] == 'F' && scale < 0;
    if (ok) {
        std::fgetc(f);   // the single whitespace after the header
        data.resize((size_t)w * h * 3);
        ok = std::fread(data.data(), sizeof(float), data.size(), f) == data.size();
    }
    std::fclose(f);
    return ok;
}

// Root-mean-square difference between two linear images (all channels together).
inline bool rmsePFM(const std::string& a, const std::string& b, double& rmse) {
    int w1, h1, w2, h2;
    std::vector<float> d1, d2;
    if (!readPFM(a, w1, h1, d1) || !readPFM(b, w2, h2, d2) || w1 != w2 || h1 != h2) return false;
    double acc = 0;
    for (size_t i = 0; i < d1.size(); i++) acc += ((double)d1[i] - d2[i]) * ((double)d1[i] - d2[i]);
    rmse = std::sqrt(acc / d1.size());
    return true;
}
