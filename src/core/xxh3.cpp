// XXH3 de 64 bits. Ver xxh3.h.

#include "core/xxh3.h"

#include <cstring>

namespace nf {

namespace {

constexpr uint64_t kP32_1 = 0x9E3779B1u, kP32_2 = 0x85EBCA77u, kP32_3 = 0xC2B2AE3Du;
constexpr uint64_t kP64_1 = 0x9E3779B185EBCA87ull, kP64_2 = 0xC2B2AE3D27D4EB4Full, kP64_3 = 0x165667B19E3779F9ull;
constexpr uint64_t kP64_4 = 0x85EBCA77C2B2AE63ull, kP64_5 = 0x27D4EB2F165667C5ull;
constexpr uint64_t kMx1 = 0x165667919E3779F9ull, kMx2 = 0x9FB21C651E98DF25ull;

constexpr uint8_t kSecret[192] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

uint64_t r64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

uint32_t r32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

uint64_t rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

// Produto de 64 x 64 bits em 128, dobrado (parte baixa XOR parte alta).
uint64_t mul128_fold(uint64_t a, uint64_t b) {
    const uint64_t lo_lo = (a & 0xFFFFFFFFu) * (b & 0xFFFFFFFFu);
    const uint64_t hi_lo = (a >> 32) * (b & 0xFFFFFFFFu);
    const uint64_t lo_hi = (a & 0xFFFFFFFFu) * (b >> 32);
    const uint64_t hi_hi = (a >> 32) * (b >> 32);
    const uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFFu) + lo_hi;
    const uint64_t upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;
    const uint64_t lower = (cross << 32) | (lo_lo & 0xFFFFFFFFu);
    return lower ^ upper;
}

uint64_t avalanche3(uint64_t h) {
    h ^= h >> 37;
    h *= kMx1;
    return h ^ (h >> 32);
}

uint64_t avalanche64(uint64_t h) {
    h ^= h >> 33;
    h *= kP64_2;
    h ^= h >> 29;
    h *= kP64_3;
    return h ^ (h >> 32);
}

uint64_t rrmxmx(uint64_t h, uint64_t len) {
    h ^= rotl(h, 49) ^ rotl(h, 24);
    h *= kMx2;
    h ^= (h >> 35) + len;
    h *= kMx2;
    return h ^ (h >> 28);
}

uint64_t mix16(const uint8_t* in, const uint8_t* secret) {
    return mul128_fold(r64(in) ^ r64(secret), r64(in + 8) ^ r64(secret + 8));
}

uint64_t swap64(uint64_t x) {
    x = ((x & 0x00FF00FF00FF00FFull) << 8) | ((x >> 8) & 0x00FF00FF00FF00FFull);
    x = ((x & 0x0000FFFF0000FFFFull) << 16) | ((x >> 16) & 0x0000FFFF0000FFFFull);
    return (x << 32) | (x >> 32);
}

// Uma faixa de 64 bytes nos 8 acumuladores.
void accumulate(uint64_t* acc, const uint8_t* in, const uint8_t* secret) {
    for (int lane = 0; lane < 8; ++lane) {
        const uint64_t v = r64(in + 8 * lane);
        const uint64_t k = v ^ r64(secret + 8 * lane);
        acc[lane ^ 1] += v;
        acc[lane] += (k & 0xFFFFFFFFu) * (k >> 32);
    }
}

uint64_t hash_long(const uint8_t* in, size_t len) {
    uint64_t acc[8] = {kP32_3, kP64_1, kP64_2, kP64_3, kP64_4, kP32_2, kP64_5, kP32_1};
    constexpr size_t kStripes = (sizeof(kSecret) - 64) / 8;  // faixas por bloco
    constexpr size_t kBlock = 64 * kStripes;
    const size_t blocks = (len - 1) / kBlock;
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t s = 0; s < kStripes; ++s) accumulate(acc, in + b * kBlock + 64 * s, kSecret + 8 * s);
        for (int lane = 0; lane < 8; ++lane) {
            uint64_t a = acc[lane];
            a ^= a >> 47;
            a ^= r64(kSecret + sizeof(kSecret) - 64 + 8 * lane);
            acc[lane] = a * kP32_1;
        }
    }
    const size_t stripes = ((len - 1) - kBlock * blocks) / 64;
    for (size_t s = 0; s < stripes; ++s) accumulate(acc, in + blocks * kBlock + 64 * s, kSecret + 8 * s);
    accumulate(acc, in + len - 64, kSecret + sizeof(kSecret) - 64 - 7);
    uint64_t result = uint64_t(len) * kP64_1;
    for (int i = 0; i < 4; ++i) {
        const uint8_t* s = kSecret + 11 + 16 * i;
        result += mul128_fold(acc[2 * i] ^ r64(s), acc[2 * i + 1] ^ r64(s + 8));
    }
    return avalanche3(result);
}

}  // namespace

uint64_t xxh3_64(const void* data, size_t len) {
    const auto* in = static_cast<const uint8_t*>(data);
    if (len == 0) return avalanche64(r64(kSecret + 56) ^ r64(kSecret + 64));
    if (len <= 3) {
        const uint32_t c = (uint32_t(in[0]) << 16) | (uint32_t(in[len >> 1]) << 24) | uint32_t(in[len - 1]) |
                           (uint32_t(len) << 8);
        return avalanche64(uint64_t(c) ^ uint64_t(r32(kSecret) ^ r32(kSecret + 4)));
    }
    if (len <= 8) {
        const uint64_t v = uint64_t(r32(in + len - 4)) + (uint64_t(r32(in)) << 32);
        return rrmxmx(v ^ (r64(kSecret + 8) ^ r64(kSecret + 16)), len);
    }
    if (len <= 16) {
        const uint64_t lo = r64(in) ^ (r64(kSecret + 24) ^ r64(kSecret + 32));
        const uint64_t hi = r64(in + len - 8) ^ (r64(kSecret + 40) ^ r64(kSecret + 48));
        return avalanche3(len + swap64(lo) + hi + mul128_fold(lo, hi));
    }
    if (len <= 128) {
        uint64_t acc = len * kP64_1;
        if (len > 32) {
            if (len > 64) {
                if (len > 96) acc += mix16(in + 48, kSecret + 96) + mix16(in + len - 64, kSecret + 112);
                acc += mix16(in + 32, kSecret + 64) + mix16(in + len - 48, kSecret + 80);
            }
            acc += mix16(in + 16, kSecret + 32) + mix16(in + len - 32, kSecret + 48);
        }
        acc += mix16(in, kSecret) + mix16(in + len - 16, kSecret + 16);
        return avalanche3(acc);
    }
    if (len <= 240) {
        uint64_t acc = len * kP64_1;
        for (int i = 0; i < 8; ++i) acc += mix16(in + 16 * i, kSecret + 16 * i);
        uint64_t acc_end = mix16(in + len - 16, kSecret + 136 - 17);
        acc = avalanche3(acc);
        const size_t rounds = len / 16;
        for (size_t i = 8; i < rounds; ++i) acc_end += mix16(in + 16 * i, kSecret + 16 * (i - 8) + 3);
        return avalanche3(acc + acc_end);
    }
    return hash_long(in, len);
}

}  // namespace nf
