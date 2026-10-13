// O decodificador de vídeo MPEG-2 do port (ISO/IEC 13818-2). Ver mpeg2_decoder.h. As seções citadas são as da norma.

#include "platform/mpeg2_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// SSE2 (todo x86-64, e o x86 de 32 bits compilado com SSE2, o padrão do MSVC): a previsão e a soma dos blocos em
// vetores, com as mesmas contas inteiras do C++ puro (o resultado é o mesmo bit a bit). Nas outras CPUs, o C++ puro.
// -DNF_MPEG2_SSE2=0 força o C++ puro também no x86 (para conferir que as duas versões dão o mesmo).
#ifndef NF_MPEG2_SSE2
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define NF_MPEG2_SSE2 1
#else
#define NF_MPEG2_SSE2 0
#endif
#endif
#if NF_MPEG2_SSE2
#include <emmintrin.h>
#endif

namespace nf {
namespace mpeg2 {

// Figura 7-2: a varredura em zigue-zague (posição na varredura -> u + 8 v).
const uint8_t kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// Figura 7-3: a varredura alternada.
const uint8_t kAlternateScan[64] = {
    0,  8,  16, 24, 1,  9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49,
    41, 33, 26, 18, 3,  11, 4,  12, 19, 27, 34, 42, 50, 58, 35, 43,
    51, 59, 20, 28, 5,  13, 6,  14, 21, 29, 36, 44, 52, 60, 37, 45,
    53, 61, 22, 30, 7,  15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63,
};

// 6.3.11: a matriz intra padrão, em ordem de linhas (a não intra padrão é toda 16).
const uint8_t kDefaultIntraMatrix[64] = {
    8,  16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83,
};

// Tabela 7-6: quantiser_scale com q_scale_type 1.
const uint8_t kNonLinearQuantiserScale[32] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12, 14, 16, 18, 20,  22,
    24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112,
};

int quantiser_scale(int code, bool q_scale_type) {
    code &= 31;
    return q_scale_type ? kNonLinearQuantiserScale[code] : code * 2;
}

namespace {

// ---------------------------------------------------------------- as tabelas do Anexo B, como estão na norma

struct Code {
    const char* bits;  // '0' e '1' (espaços ignorados), sem o bit de sinal
    int value;
};

// B.1 macroblock_address_increment.
constexpr Code kMbaCodes[] = {
    {"1", 1},              {"011", 2},            {"010", 3},            {"0011", 4},
    {"0010", 5},           {"0001 1", 6},         {"0001 0", 7},         {"0000 111", 8},
    {"0000 110", 9},       {"0000 1011", 10},     {"0000 1010", 11},     {"0000 1001", 12},
    {"0000 1000", 13},     {"0000 0111", 14},     {"0000 0110", 15},     {"0000 0101 11", 16},
    {"0000 0101 10", 17},  {"0000 0101 01", 18},  {"0000 0101 00", 19},  {"0000 0100 11", 20},
    {"0000 0100 10", 21},  {"0000 0100 011", 22}, {"0000 0100 010", 23}, {"0000 0100 001", 24},
    {"0000 0100 000", 25}, {"0000 0011 111", 26}, {"0000 0011 110", 27}, {"0000 0011 101", 28},
    {"0000 0011 100", 29}, {"0000 0011 011", 30}, {"0000 0011 010", 31}, {"0000 0011 001", 32},
    {"0000 0011 000", 33}, {"0000 0001 000", kMbEscape}, {"0000 0001 111", kMbStuffing},
};

// B.2 a B.4 macroblock_type (imagens I, P e B).
constexpr Code kMbTypeI[] = {
    {"1", kMbIntra},
    {"01", kMbQuant | kMbIntra},
};
constexpr Code kMbTypeP[] = {
    {"1", kMbForward | kMbPattern},
    {"01", kMbPattern},
    {"001", kMbForward},
    {"0001 1", kMbIntra},
    {"0001 0", kMbQuant | kMbForward | kMbPattern},
    {"0000 1", kMbQuant | kMbPattern},
    {"0000 01", kMbQuant | kMbIntra},
};
constexpr Code kMbTypeB[] = {
    {"10", kMbForward | kMbBackward},
    {"11", kMbForward | kMbBackward | kMbPattern},
    {"010", kMbBackward},
    {"011", kMbBackward | kMbPattern},
    {"0010", kMbForward},
    {"0011", kMbForward | kMbPattern},
    {"0001 1", kMbIntra},
    {"0001 0", kMbQuant | kMbForward | kMbBackward | kMbPattern},
    {"0000 11", kMbQuant | kMbForward | kMbPattern},
    {"0000 10", kMbQuant | kMbBackward | kMbPattern},
    {"0000 01", kMbQuant | kMbIntra},
};
// D (MPEG-1): só intra.
constexpr Code kMbTypeD[] = {
    {"1", kMbIntra},
};

// B.9 coded_block_pattern.
constexpr Code kCbpCodes[] = {
    {"111", 60},        {"1101", 4},        {"1100", 8},        {"1011", 16},       {"1010", 32},
    {"1001 1", 12},     {"1001 0", 48},     {"1000 1", 20},     {"1000 0", 40},     {"0111 1", 28},
    {"0111 0", 44},     {"0110 1", 52},     {"0110 0", 56},     {"0101 1", 1},      {"0101 0", 61},
    {"0100 1", 2},      {"0100 0", 62},     {"0011 11", 24},    {"0011 10", 36},    {"0011 01", 3},
    {"0011 00", 63},    {"0010 111", 5},    {"0010 110", 9},    {"0010 101", 17},   {"0010 100", 33},
    {"0010 011", 6},    {"0010 010", 10},   {"0010 001", 18},   {"0010 000", 34},   {"0001 1111", 7},
    {"0001 1110", 11},  {"0001 1101", 19},  {"0001 1100", 35},  {"0001 1011", 13},  {"0001 1010", 49},
    {"0001 1001", 21},  {"0001 1000", 41},  {"0001 0111", 14},  {"0001 0110", 50},  {"0001 0101", 22},
    {"0001 0100", 42},  {"0001 0011", 15},  {"0001 0010", 51},  {"0001 0001", 23},  {"0001 0000", 43},
    {"0000 1111", 25},  {"0000 1110", 37},  {"0000 1101", 26},  {"0000 1100", 38},  {"0000 1011", 29},
    {"0000 1010", 45},  {"0000 1001", 53},  {"0000 1000", 57},  {"0000 0111", 30},  {"0000 0110", 46},
    {"0000 0101", 54},  {"0000 0100", 58},  {"0000 0011 1", 31}, {"0000 0011 0", 47}, {"0000 0010 1", 55},
    {"0000 0010 0", 59}, {"0000 0001 1", 27}, {"0000 0001 0", 39}, {"0000 0000 1", 0},
};

// B.10 motion_code (com o bit de sinal: o último).
constexpr Code kMotionCodes[] = {
    {"0000 0011 001", -16}, {"0000 0011 011", -15}, {"0000 0011 101", -14}, {"0000 0011 111", -13},
    {"0000 0100 001", -12}, {"0000 0100 011", -11}, {"0000 0100 11", -10},  {"0000 0101 01", -9},
    {"0000 0101 11", -8},   {"0000 0111", -7},      {"0000 1001", -6},      {"0000 1011", -5},
    {"0000 111", -4},       {"0001 1", -3},         {"0011", -2},           {"011", -1},
    {"1", 0},
    {"010", 1},             {"0010", 2},            {"0001 0", 3},          {"0000 110", 4},
    {"0000 1010", 5},       {"0000 1000", 6},       {"0000 0110", 7},       {"0000 0101 10", 8},
    {"0000 0101 00", 9},    {"0000 0100 10", 10},   {"0000 0100 010", 11},  {"0000 0100 000", 12},
    {"0000 0011 110", 13},  {"0000 0011 100", 14},  {"0000 0011 010", 15},  {"0000 0011 000", 16},
};

// B.11 dmvector.
constexpr Code kDmvCodes[] = {
    {"11", -1},
    {"0", 0},
    {"10", 1},
};

// B.12 e B.13 dct_dc_size_luminance / dct_dc_size_chrominance.
constexpr Code kDcLumaCodes[] = {
    {"100", 0},      {"00", 1},        {"01", 2},         {"101", 3},         {"110", 4},          {"1110", 5},
    {"1111 0", 6},   {"1111 10", 7},   {"1111 110", 8},   {"1111 1110", 9},   {"1111 1111 0", 10}, {"1111 1111 1", 11},
};
constexpr Code kDcChromaCodes[] = {
    {"00", 0},        {"01", 1},         {"10", 2},          {"110", 3},           {"1110", 4},
    {"1111 0", 5},    {"1111 10", 6},    {"1111 110", 7},    {"1111 1110", 8},     {"1111 1111 0", 9},
    {"1111 1111 10", 10}, {"1111 1111 11", 11},
};

// B.14 e B.15: os coeficientes da DCT (sem o bit de sinal). run -1 = fim do bloco, -2 = escape.
struct DctCode {
    const char* bits;
    int run;
    int level;
};
constexpr int kEob = -1, kEsc = -2;

// B.14 (table zero). O '1s' do primeiro coeficiente de um bloco não intra fica fora (lido à parte).
constexpr DctCode kDctTable0[] = {
    {"10", kEob, 0},
    {"11", 0, 1},
    {"011", 1, 1},
    {"0100", 0, 2},
    {"0101", 2, 1},
    {"0010 1", 0, 3},
    {"0011 1", 3, 1},
    {"0011 0", 4, 1},
    {"0001 10", 1, 2},
    {"0001 11", 5, 1},
    {"0001 01", 6, 1},
    {"0001 00", 7, 1},
    {"0000 110", 0, 4},
    {"0000 100", 2, 2},
    {"0000 111", 8, 1},
    {"0000 101", 9, 1},
    {"0000 01", kEsc, 0},
    {"0010 0110", 0, 5},
    {"0010 0001", 0, 6},
    {"0010 0101", 1, 3},
    {"0010 0100", 3, 2},
    {"0010 0111", 10, 1},
    {"0010 0011", 11, 1},
    {"0010 0010", 12, 1},
    {"0010 0000", 13, 1},
    {"0000 0010 10", 0, 7},
    {"0000 0011 00", 1, 4},
    {"0000 0010 11", 2, 3},
    {"0000 0011 11", 4, 2},
    {"0000 0010 01", 5, 2},
    {"0000 0011 10", 14, 1},
    {"0000 0011 01", 15, 1},
    {"0000 0010 00", 16, 1},
    {"0000 0001 1101", 0, 8},
    {"0000 0001 1000", 0, 9},
    {"0000 0001 0011", 0, 10},
    {"0000 0001 0000", 0, 11},
    {"0000 0001 1011", 1, 5},
    {"0000 0001 0100", 2, 4},
    {"0000 0001 1100", 3, 3},
    {"0000 0001 0010", 4, 3},
    {"0000 0001 1110", 6, 2},
    {"0000 0001 0101", 7, 2},
    {"0000 0001 0001", 8, 2},
    {"0000 0001 1111", 17, 1},
    {"0000 0001 1010", 18, 1},
    {"0000 0001 1001", 19, 1},
    {"0000 0001 0111", 20, 1},
    {"0000 0001 0110", 21, 1},
    {"0000 0000 1101 0", 0, 12},
    {"0000 0000 1100 1", 0, 13},
    {"0000 0000 1100 0", 0, 14},
    {"0000 0000 1011 1", 0, 15},
    {"0000 0000 1011 0", 1, 6},
    {"0000 0000 1010 1", 1, 7},
    {"0000 0000 1010 0", 2, 5},
    {"0000 0000 1001 1", 3, 4},
    {"0000 0000 1001 0", 5, 3},
    {"0000 0000 1000 1", 9, 2},
    {"0000 0000 1000 0", 10, 2},
    {"0000 0000 1111 1", 22, 1},
    {"0000 0000 1111 0", 23, 1},
    {"0000 0000 1110 1", 24, 1},
    {"0000 0000 1110 0", 25, 1},
    {"0000 0000 1101 1", 26, 1},
    // Daqui em diante (14 a 16 bits) as duas tabelas são iguais.
    {"0000 0000 0111 11", 0, 16},
    {"0000 0000 0111 10", 0, 17},
    {"0000 0000 0111 01", 0, 18},
    {"0000 0000 0111 00", 0, 19},
    {"0000 0000 0110 11", 0, 20},
    {"0000 0000 0110 10", 0, 21},
    {"0000 0000 0110 01", 0, 22},
    {"0000 0000 0110 00", 0, 23},
    {"0000 0000 0101 11", 0, 24},
    {"0000 0000 0101 10", 0, 25},
    {"0000 0000 0101 01", 0, 26},
    {"0000 0000 0101 00", 0, 27},
    {"0000 0000 0100 11", 0, 28},
    {"0000 0000 0100 10", 0, 29},
    {"0000 0000 0100 01", 0, 30},
    {"0000 0000 0100 00", 0, 31},
    {"0000 0000 0011 000", 0, 32},
    {"0000 0000 0010 111", 0, 33},
    {"0000 0000 0010 110", 0, 34},
    {"0000 0000 0010 101", 0, 35},
    {"0000 0000 0010 100", 0, 36},
    {"0000 0000 0010 011", 0, 37},
    {"0000 0000 0010 010", 0, 38},
    {"0000 0000 0010 001", 0, 39},
    {"0000 0000 0010 000", 0, 40},
    {"0000 0000 0011 111", 1, 8},
    {"0000 0000 0011 110", 1, 9},
    {"0000 0000 0011 101", 1, 10},
    {"0000 0000 0011 100", 1, 11},
    {"0000 0000 0011 011", 1, 12},
    {"0000 0000 0011 010", 1, 13},
    {"0000 0000 0011 001", 1, 14},
    {"0000 0000 0001 0011", 1, 15},
    {"0000 0000 0001 0010", 1, 16},
    {"0000 0000 0001 0001", 1, 17},
    {"0000 0000 0001 0000", 1, 18},
    {"0000 0000 0001 0100", 6, 3},
    {"0000 0000 0001 1010", 11, 2},
    {"0000 0000 0001 1001", 12, 2},
    {"0000 0000 0001 1000", 13, 2},
    {"0000 0000 0001 0111", 14, 2},
    {"0000 0000 0001 0110", 15, 2},
    {"0000 0000 0001 0101", 16, 2},
    {"0000 0000 0001 1111", 27, 1},
    {"0000 0000 0001 1110", 28, 1},
    {"0000 0000 0001 1101", 29, 1},
    {"0000 0000 0001 1100", 30, 1},
    {"0000 0000 0001 1011", 31, 1},
};
constexpr int kDctTable0Shared = 65;  // o índice do primeiro código de 14 bits (os comuns às duas tabelas)

// B.15 (table one), até 13 bits; os de 14 a 16 bits vêm da B.14.
constexpr DctCode kDctTable1[] = {
    {"0110", kEob, 0},
    {"10", 0, 1},
    {"010", 1, 1},
    {"110", 0, 2},
    {"0010 1", 2, 1},
    {"0111", 0, 3},
    {"0011 1", 3, 1},
    {"0001 10", 4, 1},
    {"0011 0", 1, 2},
    {"0001 11", 5, 1},
    {"0000 110", 6, 1},
    {"0000 100", 7, 1},
    {"1110 0", 0, 4},
    {"0000 111", 2, 2},
    {"0000 101", 8, 1},
    {"1111 000", 9, 1},
    {"0000 01", kEsc, 0},
    {"1110 1", 0, 5},
    {"0001 01", 0, 6},
    {"1111 001", 1, 3},
    {"0010 0110", 3, 2},
    {"1111 010", 10, 1},
    {"0010 0001", 11, 1},
    {"0010 0101", 12, 1},
    {"0010 0100", 13, 1},
    {"0001 00", 0, 7},
    {"0010 0111", 1, 4},
    {"1111 1100", 2, 3},
    {"1111 1101", 4, 2},
    {"0000 0010 0", 5, 2},
    {"0000 0010 1", 14, 1},
    {"0000 0011 1", 15, 1},
    {"0000 0011 01", 16, 1},
    {"1111 011", 0, 8},
    {"1111 100", 0, 9},
    {"0010 0011", 0, 10},
    {"0010 0010", 0, 11},
    {"0010 0000", 1, 5},
    {"0000 0011 00", 2, 4},
    {"0000 0001 1100", 3, 3},
    {"0000 0001 0010", 4, 3},
    {"0000 0001 1110", 6, 2},
    {"0000 0001 0101", 7, 2},
    {"0000 0001 0001", 8, 2},
    {"0000 0001 1111", 17, 1},
    {"0000 0001 1010", 18, 1},
    {"0000 0001 1001", 19, 1},
    {"0000 0001 0111", 20, 1},
    {"0000 0001 0110", 21, 1},
    {"1111 1010", 0, 12},
    {"1111 1011", 0, 13},
    {"1111 1110", 0, 14},
    {"1111 1111", 0, 15},
    {"0000 0000 1011 0", 1, 6},
    {"0000 0000 1010 1", 1, 7},
    {"0000 0000 1010 0", 2, 5},
    {"0000 0000 1001 1", 3, 4},
    {"0000 0000 1001 0", 5, 3},
    {"0000 0000 1000 1", 9, 2},
    {"0000 0000 1000 0", 10, 2},
    {"0000 0000 1111 1", 22, 1},
    {"0000 0000 1111 0", 23, 1},
    {"0000 0000 1110 1", 24, 1},
    {"0000 0000 1110 0", 25, 1},
    {"0000 0000 1101 1", 26, 1},
};

// ---------------------------------------------------------------- as tabelas de consulta

struct Vlc {
    int16_t value = 0;
    uint8_t len = 0;  // 0: código que não existe
};

template <int Bits>
struct VlcTable {
    static constexpr int kBits = Bits;
    Vlc e[1 << Bits];
};

// A entrada de um coeficiente: run 0..63, ou um dos especiais.
constexpr uint8_t kDctRunEob = 64, kDctRunEsc = 65, kDctRunBad = 66, kDctRunLong = 67;
struct DctEntry {
    uint8_t run = kDctRunBad;
    uint8_t len = 0;
    int16_t level = 0;
};

uint32_t parse_code(const char* s, int* len) {
    uint32_t v = 0;
    int n = 0;
    for (; *s != 0; ++s) {
        if (*s == ' ') continue;
        v = (v << 1) | uint32_t(*s == '1');
        ++n;
    }
    *len = n;
    return v;
}

template <int Bits, size_t N>
void fill(VlcTable<Bits>& t, const Code (&codes)[N]) {
    for (const Code& c : codes) {
        int len = 0;
        const uint32_t v = parse_code(c.bits, &len);
        const int shift = Bits - len;
        for (uint32_t i = 0; i < (1u << shift); ++i) t.e[(v << shift) | i] = Vlc{int16_t(c.value), uint8_t(len)};
    }
}

struct Tables {
    VlcTable<11> mba;
    VlcTable<6> mb_type[5];  // pelo picture_coding_type (0 sem uso)
    VlcTable<9> cbp;
    VlcTable<11> motion;
    VlcTable<2> dmv;
    VlcTable<9> dc_luma;
    VlcTable<10> dc_chroma;
    // Os coeficientes: os códigos de até 8 bits pelos 8 primeiros bits; os mais longos (todos começam com seis zeros)
    // pelos 10 bits seguintes.
    DctEntry dct_short[2][256];
    DctEntry dct_long[2][1024];

    void add_dct(int table, const DctCode& c) {
        int len = 0;
        const uint32_t v = parse_code(c.bits, &len);
        DctEntry e;
        e.run = c.run == kEob ? kDctRunEob : c.run == kEsc ? kDctRunEsc : uint8_t(c.run);
        e.len = uint8_t(len);
        e.level = int16_t(c.level);
        if (len <= 8) {
            const int shift = 8 - len;
            for (uint32_t i = 0; i < (1u << shift); ++i) dct_short[table][(v << shift) | i] = e;
        } else {
            // Os seis primeiros bits são zero; o resto (len - 6 bits) indexa a segunda tabela.
            const int rest = len - 6;
            const int shift = 10 - rest;
            const uint32_t low = v & ((1u << rest) - 1);
            for (uint32_t i = 0; i < (1u << shift); ++i) dct_long[table][(low << shift) | i] = e;
        }
    }

    Tables() {
        fill(mba, kMbaCodes);
        fill(mb_type[1], kMbTypeI);
        fill(mb_type[2], kMbTypeP);
        fill(mb_type[3], kMbTypeB);
        fill(mb_type[4], kMbTypeD);
        fill(cbp, kCbpCodes);
        fill(motion, kMotionCodes);
        fill(dmv, kDmvCodes);
        fill(dc_luma, kDcLumaCodes);
        fill(dc_chroma, kDcChromaCodes);
        for (const DctCode& c : kDctTable0) add_dct(0, c);
        for (const DctCode& c : kDctTable1) add_dct(1, c);
        for (size_t i = kDctTable0Shared; i < sizeof(kDctTable0) / sizeof(kDctTable0[0]); ++i) add_dct(1, kDctTable0[i]);
        // Os seis zeros de começo levam à segunda tabela.
        for (int t = 0; t < 2; ++t) {
            for (int i = 0; i < 4; ++i) dct_short[t][i].run = kDctRunLong;  // 000000xx
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

template <int Bits>
inline int vlc_read(BitReader& br, const VlcTable<Bits>& t) {
    const Vlc& e = t.e[br.peek(Bits)];
    if (e.len == 0) return kInvalid;
    br.skip(e.len);
    return e.value;
}

// Um coeficiente (sem o caso do primeiro de um bloco não intra).
inline int dct_coefficient(BitReader& br, const Tables& T, int table, bool mpeg1, int& run, int& level) {
    const uint32_t bits = br.peek(16);
    const DctEntry* e = &T.dct_short[table][bits >> 8];
    if (e->run >= kDctRunEob) {
        if (e->run == kDctRunLong) {
            e = &T.dct_long[table][bits & 0x3ff];
            if (e->run >= kDctRunEob) return kInvalid;  // um buraco da B.15, ou 16 zeros
        } else if (e->run == kDctRunEob) {
            br.skip(e->len);
            return 0;
        } else if (e->run == kDctRunEsc) {
            br.skip(6);
            run = int(br.read(6));
            if (mpeg1) {
                // 11172-2 2.4.3.7: 8 bits com sinal; 0x00 e 0x80 trazem mais 8 bits (128..255 e -256..-129).
                const int l = int(br.read(8));
                if (l == 0) level = int(br.read(8));
                else if (l == 128) level = int(br.read(8)) - 256;
                else level = l > 128 ? l - 256 : l;
            } else {
                const int l = int(br.read(12));
                level = l >= 2048 ? l - 4096 : l;
            }
            return 1;
        } else {
            return kInvalid;
        }
    }
    br.skip(e->len);
    run = e->run;
    level = br.read(1) ? -e->level : e->level;
    return 1;
}

// ---------------------------------------------------------------- a quantização inversa (7.4)

// Um coeficiente AC (ou de um bloco não intra): (2 QF + k) W qs / 32, truncado para zero, saturado; no MPEG-1 também
// o ajuste para ímpar (11172-2 2.4.4.1/2).
inline int dequant_coefficient(int qf, int w, int qs, bool intra, bool mpeg1) {
    if (qf == 0) return 0;
    const int mag = qf < 0 ? -qf : qf;
    int v = ((2 * mag + (intra ? 0 : 1)) * w * qs) >> 5;
    if (mpeg1 && (v & 1) == 0 && v != 0) v -= 1;
    v = qf < 0 ? -v : v;
    return v < -2048 ? -2048 : v > 2047 ? 2047 : v;
}

// ---------------------------------------------------------------- a IDCT

// x[n] = 1/2 sum C(k) X[k] cos((2n+1) k pi / 16) nas linhas e nas colunas (7.5; C(0) = 1/sqrt(2)). A parte par (X0, X2,
// X4, X6) e a ímpar (X1, X3, X5, X7) dão x[n] = e[n] + o[n] e x[7-n] = e[n] - o[n]. As linhas em inteiros de 32 bits,
// com cos(k pi / 16) x 2^16 e sem arredondar (a linha x 2^17); as colunas com cos x 2^20 (a coluna x 2^38) e um só
// arredondamento no fim, floor(x + 1/2). As colunas vão em double só porque o x86 de 32 bits não tem multiplicação de
// 64 bits rápida: todos os valores são inteiros abaixo de 2^53, então as contas em double são exatas (as mesmas de
// inteiros de 64 bits, em qualquer CPU e modo de arredondamento). O erro contra a fórmula exata fica abaixo de 0.002 nos
// blocos dos filmes: o resultado é o da fórmula exata a menos dos empates.
constexpr int32_t kR1 = 64277, kR2 = 60547, kR3 = 54491, kR4 = 46341, kR5 = 36410, kR6 = 25080, kR7 = 12785;
constexpr double kK1 = 1028428, kK2 = 968758, kK3 = 871859, kK4 = 741455, kK5 = 582558, kK6 = 401273, kK7 = 204567;
constexpr double kIdctScale = 1.0 / 274877906944.0;  // 2^-38

inline int clamp_idct(int v) { return v < -256 ? -256 : v > 255 ? 255 : v; }
[[maybe_unused]] inline uint8_t clamp_pixel(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }  // sem SSE2

// Uma linha (|coeficiente| <= 2048) para 8 valores x 2^17 (|valor| < 2^30). Devolve 0 se a linha é toda zero, 1 se só
// tem o X0 (os 8 valores iguais), 2 no resto. Os atalhos dão o mesmo que as contas inteiras: os termos que faltam são
// zero.
inline int idct_row(const int16_t* in, int32_t* out) {
    const int32_t x0 = in[0], x1 = in[1], x2 = in[2], x3 = in[3], x4 = in[4], x5 = in[5], x6 = in[6], x7 = in[7];
    int32_t e0, e1, e2, e3, o0, o1, o2, o3;
    if ((x4 | x5 | x6 | x7) == 0) {
        if ((x1 | x2 | x3) == 0) {
            const int32_t v = x0 * kR4;
            for (int i = 0; i < 8; ++i) out[i] = v;
            return x0 != 0 ? 1 : 0;
        }
        const int32_t t0 = x0 * kR4, t2 = x2 * kR2, t3 = x2 * kR6;
        e0 = t0 + t2;
        e3 = t0 - t2;
        e1 = t0 + t3;
        e2 = t0 - t3;
        o0 = x1 * kR1 + x3 * kR3;
        o1 = x1 * kR3 - x3 * kR7;
        o2 = x1 * kR5 - x3 * kR1;
        o3 = x1 * kR7 - x3 * kR5;
    } else {
        const int32_t t0 = (x0 + x4) * kR4, t1 = (x0 - x4) * kR4;
        const int32_t t2 = x2 * kR2 + x6 * kR6, t3 = x2 * kR6 - x6 * kR2;
        e0 = t0 + t2;
        e3 = t0 - t2;
        e1 = t1 + t3;
        e2 = t1 - t3;
        o0 = x1 * kR1 + x3 * kR3 + x5 * kR5 + x7 * kR7;
        o1 = x1 * kR3 - x3 * kR7 - x5 * kR1 - x7 * kR5;
        o2 = x1 * kR5 - x3 * kR1 + x5 * kR7 + x7 * kR3;
        o3 = x1 * kR7 - x3 * kR5 + x5 * kR3 - x7 * kR1;
    }
    out[0] = e0 + o0;
    out[7] = e0 - o0;
    out[1] = e1 + o1;
    out[6] = e1 - o1;
    out[2] = e2 + o2;
    out[5] = e2 - o2;
    out[3] = e3 + o3;
    out[4] = e3 - o3;
    return 2;
}

// floor(acc / 2^38 + 1/2), saturado em [-256, 255]. acc x 2^-38 é exato (uma potência de 2) e + 1024.5 também (no
// máximo 15 bits inteiros e 38 de fração: cabe nos 53 do double); a truncagem de um positivo é o floor, e um valor
// abaixo de -1024.5 sai abaixo de -256 de qualquer jeito.
inline int16_t idct_final(double acc) { return int16_t(clamp_idct(int(acc * kIdctScale + 1024.5) - 1024)); }

// Uma coluna dos valores das linhas para 8 pixels (passo 8 em out). rows: até onde as linhas têm algo (1: só a 0; 4: só
// as 0-3; 8: todas).
inline void idct_col(const int32_t* in, int16_t* out, int rows) {
    const double x0 = in[0];
    if (rows == 1) {
        const int16_t v = idct_final(x0 * kK4);
        for (int i = 0; i < 8; ++i) out[8 * i] = v;
        return;
    }
    const double x1 = in[8], x2 = in[16], x3 = in[24];
    double e0, e1, e2, e3, o0, o1, o2, o3;
    if (rows <= 4) {
        const double t0 = x0 * kK4, t2 = x2 * kK2, t3 = x2 * kK6;
        e0 = t0 + t2;
        e3 = t0 - t2;
        e1 = t0 + t3;
        e2 = t0 - t3;
        o0 = x1 * kK1 + x3 * kK3;
        o1 = x1 * kK3 - x3 * kK7;
        o2 = x1 * kK5 - x3 * kK1;
        o3 = x1 * kK7 - x3 * kK5;
    } else {
        const double x4 = in[32], x5 = in[40], x6 = in[48], x7 = in[56];
        const double t0 = (x0 + x4) * kK4, t1 = (x0 - x4) * kK4;
        const double t2 = x2 * kK2 + x6 * kK6, t3 = x2 * kK6 - x6 * kK2;
        e0 = t0 + t2;
        e3 = t0 - t2;
        e1 = t1 + t3;
        e2 = t1 - t3;
        o0 = x1 * kK1 + x3 * kK3 + x5 * kK5 + x7 * kK7;
        o1 = x1 * kK3 - x3 * kK7 - x5 * kK1 - x7 * kK5;
        o2 = x1 * kK5 - x3 * kK1 + x5 * kK7 + x7 * kK3;
        o3 = x1 * kK7 - x3 * kK5 + x5 * kK3 - x7 * kK1;
    }
    out[0] = idct_final(e0 + o0);
    out[56] = idct_final(e0 - o0);
    out[8] = idct_final(e1 + o1);
    out[48] = idct_final(e1 - o1);
    out[16] = idct_final(e2 + o2);
    out[40] = idct_final(e2 - o2);
    out[24] = idct_final(e3 + o3);
    out[32] = idct_final(e3 - o3);
}

#if NF_MPEG2_SSE2
// Duas colunas de uma vez (as colunas c e c + 1 são vizinhas em tmp): as mesmas contas exatas em double, então o mesmo
// resultado de idct_col.
inline __m128d idct_load2(const int32_t* p) {
    return _mm_cvtepi32_pd(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)));
}

inline void idct_store2(int16_t* p, __m128d acc) {
    const __m128i v = _mm_sub_epi32(
        _mm_cvttpd_epi32(_mm_add_pd(_mm_mul_pd(acc, _mm_set1_pd(kIdctScale)), _mm_set1_pd(1024.5))),
        _mm_set1_epi32(1024));
    __m128i s = _mm_packs_epi32(v, v);
    s = _mm_min_epi16(_mm_max_epi16(s, _mm_set1_epi16(-256)), _mm_set1_epi16(255));
    const int32_t two = _mm_cvtsi128_si32(s);
    std::memcpy(p, &two, sizeof(two));
}

inline void idct_col2(const int32_t* in, int16_t* out, int rows) {
    const __m128d k1 = _mm_set1_pd(kK1), k2 = _mm_set1_pd(kK2), k3 = _mm_set1_pd(kK3), k4 = _mm_set1_pd(kK4);
    const __m128d k5 = _mm_set1_pd(kK5), k6 = _mm_set1_pd(kK6), k7 = _mm_set1_pd(kK7);
    const __m128d x0 = idct_load2(in);
    if (rows == 1) {
        const __m128d a = _mm_mul_pd(x0, k4);
        for (int i = 0; i < 8; ++i) idct_store2(out + 8 * i, a);
        return;
    }
    const __m128d x1 = idct_load2(in + 8), x2 = idct_load2(in + 16), x3 = idct_load2(in + 24);
    __m128d e0, e1, e2, e3, o0, o1, o2, o3;
    if (rows <= 4) {
        const __m128d t0 = _mm_mul_pd(x0, k4), t2 = _mm_mul_pd(x2, k2), t3 = _mm_mul_pd(x2, k6);
        e0 = _mm_add_pd(t0, t2);
        e3 = _mm_sub_pd(t0, t2);
        e1 = _mm_add_pd(t0, t3);
        e2 = _mm_sub_pd(t0, t3);
        o0 = _mm_add_pd(_mm_mul_pd(x1, k1), _mm_mul_pd(x3, k3));
        o1 = _mm_sub_pd(_mm_mul_pd(x1, k3), _mm_mul_pd(x3, k7));
        o2 = _mm_sub_pd(_mm_mul_pd(x1, k5), _mm_mul_pd(x3, k1));
        o3 = _mm_sub_pd(_mm_mul_pd(x1, k7), _mm_mul_pd(x3, k5));
    } else {
        const __m128d x4 = idct_load2(in + 32), x5 = idct_load2(in + 40), x6 = idct_load2(in + 48),
                      x7 = idct_load2(in + 56);
        const __m128d t0 = _mm_mul_pd(_mm_add_pd(x0, x4), k4), t1 = _mm_mul_pd(_mm_sub_pd(x0, x4), k4);
        const __m128d t2 = _mm_add_pd(_mm_mul_pd(x2, k2), _mm_mul_pd(x6, k6));
        const __m128d t3 = _mm_sub_pd(_mm_mul_pd(x2, k6), _mm_mul_pd(x6, k2));
        e0 = _mm_add_pd(t0, t2);
        e3 = _mm_sub_pd(t0, t2);
        e1 = _mm_add_pd(t1, t3);
        e2 = _mm_sub_pd(t1, t3);
        o0 = _mm_add_pd(_mm_add_pd(_mm_mul_pd(x1, k1), _mm_mul_pd(x3, k3)),
                        _mm_add_pd(_mm_mul_pd(x5, k5), _mm_mul_pd(x7, k7)));
        o1 = _mm_sub_pd(_mm_sub_pd(_mm_mul_pd(x1, k3), _mm_mul_pd(x3, k7)),
                        _mm_add_pd(_mm_mul_pd(x5, k1), _mm_mul_pd(x7, k5)));
        o2 = _mm_add_pd(_mm_sub_pd(_mm_mul_pd(x1, k5), _mm_mul_pd(x3, k1)),
                        _mm_add_pd(_mm_mul_pd(x5, k7), _mm_mul_pd(x7, k3)));
        o3 = _mm_sub_pd(_mm_add_pd(_mm_sub_pd(_mm_mul_pd(x1, k7), _mm_mul_pd(x3, k5)), _mm_mul_pd(x5, k3)),
                        _mm_mul_pd(x7, k1));
    }
    idct_store2(out + 0, _mm_add_pd(e0, o0));
    idct_store2(out + 56, _mm_sub_pd(e0, o0));
    idct_store2(out + 8, _mm_add_pd(e1, o1));
    idct_store2(out + 48, _mm_sub_pd(e1, o1));
    idct_store2(out + 16, _mm_add_pd(e2, o2));
    idct_store2(out + 40, _mm_sub_pd(e2, o2));
    idct_store2(out + 24, _mm_add_pd(e3, o3));
    idct_store2(out + 32, _mm_sub_pd(e3, o3));
}
#endif

// A IDCT sem o atalho do bloco só com DC. portable: as colunas em C++ puro também onde há SSE2 (os testes).
void idct_int(int16_t* blk, bool portable = false) {
    int32_t tmp[64];
    int last = 0;
    bool flat = true;  // todas as linhas com algo têm só o X0: as 8 colunas são iguais
    for (int r = 0; r < 8; ++r) {
        const int k = idct_row(blk + 8 * r, tmp + 8 * r);
        if (k != 0) last = r;
        if (k == 2) flat = false;
    }
    const int rows = last == 0 ? 1 : last < 4 ? 4 : 8;
    if (flat) {
        idct_col(tmp, blk, rows);
        for (int r = 0; r < 8; ++r) {
            for (int c = 1; c < 8; ++c) blk[8 * r + c] = blk[8 * r];
        }
        return;
    }
#if NF_MPEG2_SSE2
    if (!portable) {
        for (int c = 0; c < 8; c += 2) idct_col2(tmp + c, blk + c, rows);
        return;
    }
#else
    (void)portable;
#endif
    for (int c = 0; c < 8; ++c) idct_col(tmp + c, blk + c, rows);
}

// A IDCT de referência (a fórmula de 7.5 em double, arredondada ao mais próximo): só para depuração.
void idct_double(int16_t* blk) {
    static double cosines[8][8];
    static bool init = false;
    if (!init) {
        for (int x = 0; x < 8; ++x) {
            for (int u = 0; u < 8; ++u) {
                const double cu = u == 0 ? std::sqrt(0.5) : 1.0;
                cosines[x][u] = 0.5 * cu * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0);
            }
        }
        init = true;
    }
    double tmp[64];
    for (int v = 0; v < 8; ++v) {
        for (int x = 0; x < 8; ++x) {
            double s = 0.0;
            for (int u = 0; u < 8; ++u) s += cosines[x][u] * blk[8 * v + u];
            tmp[8 * v + x] = s;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            double s = 0.0;
            for (int v = 0; v < 8; ++v) s += cosines[y][v] * tmp[8 * v + x];
            blk[8 * y + x] = int16_t(clamp_idct(int(std::floor(s + 0.5))));
        }
    }
}

// ---------------------------------------------------------------- a previsão (7.6.4)

// Os pixels: p = (a + b + 1) >> 1 com meio pixel numa direção, (a + b + c + d + 2) >> 2 nas duas; a média de duas
// previsões (a bidirecional) também (d + p + 1) >> 1. A versão SSE2 faz as mesmas contas (pavgb é exatamente
// (a + b + 1) >> 1; a de quatro vai em 16 bits), então o resultado é o mesmo bit a bit.
template <int W, int HX, int HY, bool Avg>
void mc_block(const uint8_t* s, int ss, uint8_t* d, int ds, int h) {
    for (int y = 0; y < h; ++y, s += ss, d += ds) {
        for (int x = 0; x < W; ++x) {
            int p;
            if (HX && HY) p = (s[x] + s[x + 1] + s[x + ss] + s[x + ss + 1] + 2) >> 2;
            else if (HX) p = (s[x] + s[x + 1] + 1) >> 1;
            else if (HY) p = (s[x] + s[x + ss] + 1) >> 1;
            else p = s[x];
            d[x] = Avg ? uint8_t((d[x] + p + 1) >> 1) : uint8_t(p);
        }
    }
}

#if NF_MPEG2_SSE2
template <int W>
inline __m128i load_px(const uint8_t* p) {
    if constexpr (W == 16) return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    else return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
}
template <int W>
inline void store_px(uint8_t* p, __m128i v) {
    if constexpr (W == 16) _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v);
    else _mm_storel_epi64(reinterpret_cast<__m128i*>(p), v);
}

template <int W, int HX, int HY, bool Avg>
void mc_block_sse2(const uint8_t* s, int ss, uint8_t* d, int ds, int h) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i two = _mm_set1_epi16(2);
    for (int y = 0; y < h; ++y, s += ss, d += ds) {
        const __m128i a = load_px<W>(s);
        __m128i p;
        if constexpr (HX && HY) {
            const __m128i b = load_px<W>(s + 1), c = load_px<W>(s + ss), e = load_px<W>(s + ss + 1);
            __m128i lo = _mm_add_epi16(_mm_add_epi16(_mm_unpacklo_epi8(a, zero), _mm_unpacklo_epi8(b, zero)),
                                       _mm_add_epi16(_mm_unpacklo_epi8(c, zero), _mm_unpacklo_epi8(e, zero)));
            lo = _mm_srli_epi16(_mm_add_epi16(lo, two), 2);
            if constexpr (W == 16) {
                __m128i hi = _mm_add_epi16(_mm_add_epi16(_mm_unpackhi_epi8(a, zero), _mm_unpackhi_epi8(b, zero)),
                                           _mm_add_epi16(_mm_unpackhi_epi8(c, zero), _mm_unpackhi_epi8(e, zero)));
                hi = _mm_srli_epi16(_mm_add_epi16(hi, two), 2);
                p = _mm_packus_epi16(lo, hi);
            } else {
                p = _mm_packus_epi16(lo, lo);
            }
        } else if constexpr (HX) {
            p = _mm_avg_epu8(a, load_px<W>(s + 1));
        } else if constexpr (HY) {
            p = _mm_avg_epu8(a, load_px<W>(s + ss));
        } else {
            p = a;
        }
        if constexpr (Avg) p = _mm_avg_epu8(p, load_px<W>(d));
        store_px<W>(d, p);
    }
}
#define NF_MC_BLOCK mc_block_sse2
#else
#define NF_MC_BLOCK mc_block
#endif

using McFn = void (*)(const uint8_t*, int, uint8_t*, int, int);

template <int W>
McFn mc_fn(int hx, int hy, bool avg) {
    static const McFn table[8] = {
        NF_MC_BLOCK<W, 0, 0, false>, NF_MC_BLOCK<W, 1, 0, false>, NF_MC_BLOCK<W, 0, 1, false>,
        NF_MC_BLOCK<W, 1, 1, false>, NF_MC_BLOCK<W, 0, 0, true>,  NF_MC_BLOCK<W, 1, 0, true>,
        NF_MC_BLOCK<W, 0, 1, true>,  NF_MC_BLOCK<W, 1, 1, true>,
    };
    return table[(avg ? 4 : 0) + hx + 2 * hy];
}

// A versão em C++ puro (a das outras CPUs), para os testes compararem com a SSE2.
McFn mc_fn_scalar(int w, int hx, int hy, bool avg) {
    static const McFn t16[8] = {mc_block<16, 0, 0, false>, mc_block<16, 1, 0, false>, mc_block<16, 0, 1, false>,
                                mc_block<16, 1, 1, false>, mc_block<16, 0, 0, true>,  mc_block<16, 1, 0, true>,
                                mc_block<16, 0, 1, true>,  mc_block<16, 1, 1, true>};
    static const McFn t8[8] = {mc_block<8, 0, 0, false>, mc_block<8, 1, 0, false>, mc_block<8, 0, 1, false>,
                               mc_block<8, 1, 1, false>, mc_block<8, 0, 0, true>,  mc_block<8, 1, 0, true>,
                               mc_block<8, 0, 1, true>,  mc_block<8, 1, 1, true>};
    return (w == 16 ? t16 : t8)[(avg ? 4 : 0) + hx + 2 * hy];
}

void mc_dispatch(const uint8_t* s, int ss, uint8_t* d, int ds, int w, int h, int hx, int hy, bool avg) {
    if (w == 16) mc_fn<16>(hx, hy, avg)(s, ss, d, ds, h);
    else if (w == 8) mc_fn<8>(hx, hy, avg)(s, ss, d, ds, h);
    else {
        // Larguras de teste.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = s + y * ss + x;
                int v;
                if (hx && hy) v = (p[0] + p[1] + p[ss] + p[ss + 1] + 2) >> 2;
                else if (hx) v = (p[0] + p[1] + 1) >> 1;
                else if (hy) v = (p[0] + p[ss] + 1) >> 1;
                else v = p[0];
                uint8_t& o = d[y * ds + x];
                o = avg ? uint8_t((o + v + 1) >> 1) : uint8_t(v);
            }
        }
    }
}

// A previsão de um bloco de um plano (w x h no plano de plane_w x plane_h, passo stride). Um vetor que sai do plano
// (a norma não deixa; um fluxo com erro) lê as bordas repetidas.
void predict_plane(const uint8_t* plane, int stride, int plane_w, int plane_h, int x, int y, int hx, int hy, int w,
                   int h, uint8_t* dst, int ds, bool avg) {
    if (x >= 0 && y >= 0 && x + w + hx <= plane_w && y + h + hy <= plane_h) {
        mc_dispatch(plane + y * stride + x, stride, dst, ds, w, h, hx, hy, avg);
        return;
    }
    uint8_t tmp[17 * 17];
    for (int j = 0; j <= h; ++j) {
        const int sy = std::clamp(y + j, 0, plane_h - 1);
        for (int i = 0; i <= w; ++i) tmp[j * 17 + i] = plane[sy * stride + std::clamp(x + i, 0, plane_w - 1)];
    }
    mc_dispatch(tmp, 17, dst, ds, w, h, hx, hy, avg);
}

}  // namespace

// ---------------------------------------------------------------- as peças expostas aos testes

bool BitReader::next_start_code() {
    pos_ = (pos_ + 7) & ~size_t(7);
    while (pos_ + 24 <= size_bits_) {
        if (peek(24) == 1) return true;
        pos_ += 8;
    }
    return false;
}

int read_mb_address_increment(BitReader& br) { return vlc_read(br, tables().mba); }

int read_mb_type(BitReader& br, int picture_coding_type) {
    if (picture_coding_type < 1 || picture_coding_type > 4) return kInvalid;
    const int v = vlc_read(br, tables().mb_type[picture_coding_type]);
    return v == 0 ? kInvalid : v;
}

int read_coded_block_pattern(BitReader& br) { return vlc_read(br, tables().cbp); }
int read_motion_code(BitReader& br) { return vlc_read(br, tables().motion); }
int read_dmvector(BitReader& br) { return vlc_read(br, tables().dmv); }
int read_dc_size(BitReader& br, bool chroma) {
    return chroma ? vlc_read(br, tables().dc_chroma) : vlc_read(br, tables().dc_luma);
}

int read_dct_coefficient(BitReader& br, int table, bool first, bool mpeg1, int* run, int* level) {
    if (first && br.peek(1) == 1) {
        br.skip(1);
        *run = 0;
        *level = br.read(1) ? -1 : 1;
        return 1;
    }
    return dct_coefficient(br, tables(), table & 1, mpeg1, *run, *level);
}

void inverse_quantise(const int16_t qf[64], const uint8_t w[64], int qs, bool intra, int intra_dc_precision, bool mpeg1,
                      int16_t f[64]) {
    int sum = 0;
    for (int i = 0; i < 64; ++i) {
        int v;
        if (intra && i == 0) {
            v = qf[0] * (8 >> (mpeg1 ? 0 : (intra_dc_precision & 3)));
            v = v < -2048 ? -2048 : v > 2047 ? 2047 : v;
        } else {
            v = dequant_coefficient(qf[i], w[i], qs, intra, mpeg1);
        }
        f[i] = int16_t(v);
        sum += v;
    }
    if (!mpeg1 && (sum & 1) == 0) f[63] = int16_t(f[63] ^ 1);
}

void idct(int16_t block[64]) {
    bool dc_only = true;
    for (int i = 1; i < 64 && dc_only; ++i) dc_only = block[i] == 0;
    if (dc_only) {
        const int16_t v = int16_t(clamp_idct((block[0] + 4) >> 3));
        for (int i = 0; i < 64; ++i) block[i] = v;
        return;
    }
    idct_int(block);
}

void idct_portable(int16_t block[64]) {
    bool dc_only = true;
    for (int i = 1; i < 64 && dc_only; ++i) dc_only = block[i] == 0;
    if (dc_only) {
        const int16_t v = int16_t(clamp_idct((block[0] + 4) >> 3));
        for (int i = 0; i < 64; ++i) block[i] = v;
        return;
    }
    idct_int(block, true);
}

void predict(const uint8_t* ref, int ref_stride, int x, int y, int half_x, int half_y, int w, int h, uint8_t* dst,
             int dst_stride, bool average) {
    mc_dispatch(ref + y * ref_stride + x, ref_stride, dst, dst_stride, w, h, half_x & 1, half_y & 1, average);
}

void predict_portable(const uint8_t* ref, int ref_stride, int x, int y, int half_x, int half_y, int w, int h,
                      uint8_t* dst, int dst_stride, bool average) {
    mc_fn_scalar(w, half_x & 1, half_y & 1, average)(ref + y * ref_stride + x, ref_stride, dst, dst_stride, h);
}

}  // namespace mpeg2

// ================================================================ o decodificador

using namespace mpeg2;

namespace {

struct Frame {
    std::vector<uint8_t> buf;
    uint8_t* y = nullptr;
    uint8_t* cb = nullptr;
    uint8_t* cr = nullptr;
    int coding_type = 0;
    int temporal_reference = 0;
    uint64_t decode_index = 0;
    bool progressive_frame = true;
    bool top_field_first = false;
    int errors = 0;
};

// Os tipos de previsão de um macrobloco (frame_motion_type, 6.3.17.1) numa imagem de quadro.
constexpr int kMotionField = 1, kMotionFrame = 2, kMotionDualPrime = 3;

const uint8_t* find_start_code(const uint8_t* p, const uint8_t* end) {
    while (p + 3 <= end) {
        if (p[2] > 1) {
            p += 3;
        } else if (p[2] == 1 && p[0] == 0 && p[1] == 0) {
            return p;
        } else {
            ++p;
        }
    }
    return end;
}

}  // namespace

struct Mpeg2VideoDecoder::State {
    const Tables* T = &tables();
    Output out;
    Mpeg2Stats st;
    std::string err;
    bool ref_idct = false;

    // ---- a sequência
    bool have_seq = false;
    bool mpeg1 = true;
    int horizontal_size = 0, vertical_size = 0;
    int aspect = 0, frame_rate_code = 0, frame_rate_n = 0, frame_rate_d = 0;
    int chroma_format = 1;
    bool progressive_sequence = true;
    uint8_t intra_q[64], nonintra_q[64], chroma_intra_q[64], chroma_nonintra_q[64];  // em ordem de linhas

    // ---- os buffers
    int mb_width = 0, mb_height = 0;
    int coded_w = 0, coded_h = 0, chroma_w = 0, chroma_h = 0;
    Frame frames[3];
    Frame* fwd = nullptr;  // a referência mais antiga (a de frente das imagens B)
    Frame* bwd = nullptr;  // a mais nova (a de frente das P, a de trás das B)
    bool bwd_pending = false;  // a mais nova ainda não saiu
    Frame* cur = nullptr;
    std::vector<uint8_t> mb_done;
    uint64_t decode_count = 0, display_count = 0;

    // ---- a imagem
    bool pic_header = false;   // um cabeçalho de imagem visto, a imagem ainda não acabou
    bool pic_started = false;  // a primeira fatia começou a imagem
    bool pic_ok = false;       // a imagem está sendo decodificada (não pulada)
    int pic_type = 0, temporal_reference = 0;
    int f_code[2][2] = {{15, 15}, {15, 15}};
    bool full_pel[2] = {false, false};
    int intra_dc_precision = 0, picture_structure = 3;
    bool top_field_first = false, frame_pred_frame_dct = true, concealment_mv = false, q_scale_type = false;
    bool intra_vlc_format = false, alternate_scan = false, repeat_first_field = false, progressive_frame = true;
    int pic_errors = 0;

    // ---- o macrobloco
    int qscale = 2;
    int dc_pred[3] = {128, 128, 128};
    int pmv[2][2][2] = {};          // [r][s][t]
    int vec[2][2][2] = {};          // os vetores do macrobloco (meio pixel de luma; o vertical de campo em linhas de campo)
    int field_sel[2][2] = {};       // [r][s]
    int mb_flags = 0, motion_type = kMotionFrame;  // os do último macrobloco (os pulados de B repetem)
    int16_t blk[64];

    std::vector<uint8_t> buf;  // a entrada com folga de zeros

    // ---------------------------------------------------------------- cabeçalhos
    void reset_matrices_default() {
        std::memcpy(intra_q, kDefaultIntraMatrix, 64);
        std::memset(nonintra_q, 16, 64);
    }
    void load_matrix(BitReader& br, uint8_t* m) {
        for (int i = 0; i < 64; ++i) m[kZigzag[i]] = uint8_t(br.read(8));
    }

    // 6.2.2.1 sequence_header.
    void sequence_header(BitReader& br) {
        horizontal_size = int(br.read(12));
        vertical_size = int(br.read(12));
        aspect = int(br.read(4));
        frame_rate_code = int(br.read(4));
        br.skip(18 + 1 + 10 + 1);  // bit_rate_value, marker, vbv_buffer_size_value, constrained_parameters_flag
        reset_matrices_default();
        if (br.read(1)) load_matrix(br, intra_q);
        if (br.read(1)) load_matrix(br, nonintra_q);
        std::memcpy(chroma_intra_q, intra_q, 64);
        std::memcpy(chroma_nonintra_q, nonintra_q, 64);
        have_seq = horizontal_size > 0 && vertical_size > 0;
        mpeg1 = true;  // até a extensão de sequência aparecer
        chroma_format = 1;
        progressive_sequence = true;
        frame_rate_n = frame_rate_d = 0;
    }

    // 6.2.2.3 sequence_extension e as outras extensões (6.2.3.x).
    void extension(BitReader& br) {
        const int id = int(br.read(4));
        if (id == 1) {  // sequence_extension
            br.skip(8);  // profile_and_level_indication
            progressive_sequence = br.read(1) != 0;
            chroma_format = int(br.read(2));
            horizontal_size = (horizontal_size & 0xfff) | int(br.read(2) << 12);
            vertical_size = (vertical_size & 0xfff) | int(br.read(2) << 12);
            br.skip(12 + 1 + 8 + 1);  // bit_rate_extension, marker, vbv_buffer_size_extension, low_delay
            frame_rate_n = int(br.read(2));
            frame_rate_d = int(br.read(5));
            mpeg1 = false;
        } else if (id == 3) {  // quant_matrix_extension
            if (br.read(1)) {
                load_matrix(br, intra_q);
                std::memcpy(chroma_intra_q, intra_q, 64);
            }
            if (br.read(1)) {
                load_matrix(br, nonintra_q);
                std::memcpy(chroma_nonintra_q, nonintra_q, 64);
            }
            if (br.read(1)) load_matrix(br, chroma_intra_q);
            if (br.read(1)) load_matrix(br, chroma_nonintra_q);
        } else if (id == 8) {  // picture_coding_extension
            f_code[0][0] = int(br.read(4));
            f_code[0][1] = int(br.read(4));
            f_code[1][0] = int(br.read(4));
            f_code[1][1] = int(br.read(4));
            intra_dc_precision = int(br.read(2));
            picture_structure = int(br.read(2));
            top_field_first = br.read(1) != 0;
            frame_pred_frame_dct = br.read(1) != 0;
            concealment_mv = br.read(1) != 0;
            q_scale_type = br.read(1) != 0;
            intra_vlc_format = br.read(1) != 0;
            alternate_scan = br.read(1) != 0;
            repeat_first_field = br.read(1) != 0;
            br.skip(1);  // chroma_420_type
            progressive_frame = br.read(1) != 0;
        }
        // 2 sequence_display, 5 sequence_scalable, 7 picture_display, 9 picture_spatial_scalable, 10
        // picture_temporal_scalable: nada a fazer aqui.
    }

    // 6.2.3 picture_header.
    void picture_header(BitReader& br) {
        temporal_reference = int(br.read(10));
        pic_type = int(br.read(3));
        br.skip(16);  // vbv_delay
        full_pel[0] = full_pel[1] = false;
        f_code[0][0] = f_code[0][1] = f_code[1][0] = f_code[1][1] = 15;
        if (pic_type == 2 || pic_type == 3) {
            full_pel[0] = br.read(1) != 0;
            const int f = int(br.read(3));
            f_code[0][0] = f_code[0][1] = f;
        }
        if (pic_type == 3) {
            full_pel[1] = br.read(1) != 0;
            const int f = int(br.read(3));
            f_code[1][0] = f_code[1][1] = f;
        }
        // Os valores de uma imagem MPEG-1 (sem picture_coding_extension); no MPEG-2 a extensão os troca.
        intra_dc_precision = 0;
        picture_structure = 3;
        top_field_first = false;
        frame_pred_frame_dct = true;
        concealment_mv = false;
        q_scale_type = false;
        intra_vlc_format = false;
        alternate_scan = false;
        repeat_first_field = false;
        progressive_frame = true;
        if (!mpeg1) full_pel[0] = full_pel[1] = false;  // no MPEG-2 os bits valem 0 e o f_code 7: a extensão manda
    }

    // ---------------------------------------------------------------- a imagem
    bool alloc_frames() {
        const int mbw = (horizontal_size + 15) / 16;
        const int mbh = (mpeg1 || progressive_sequence) ? (vertical_size + 15) / 16 : 2 * ((vertical_size + 31) / 32);
        if (mbw == mb_width && mbh == mb_height && !frames[0].buf.empty()) return true;
        if (mbw <= 0 || mbh <= 0 || mbw > 256 || mbh > 256) return false;
        mb_width = mbw;
        mb_height = mbh;
        coded_w = mbw * 16;
        coded_h = mbh * 16;
        chroma_w = coded_w / 2;
        chroma_h = coded_h / 2;
        const size_t ysz = size_t(coded_w) * size_t(coded_h), csz = size_t(chroma_w) * size_t(chroma_h);
        for (Frame& f : frames) {
            f.buf.assign(ysz + 2 * csz, 128);
            f.y = f.buf.data();
            f.cb = f.y + ysz;
            f.cr = f.cb + csz;
        }
        fwd = bwd = nullptr;
        bwd_pending = false;
        return true;
    }

    void emit(const Frame& f) {
        ++st.output;
        if (!out) {
            ++display_count;
            return;
        }
        Mpeg2Picture p;
        p.width = horizontal_size;
        p.height = vertical_size;
        p.chroma_width = (horizontal_size + 1) / 2;
        p.chroma_height = (vertical_size + 1) / 2;
        p.y = f.y;
        p.cb = f.cb;
        p.cr = f.cr;
        p.y_stride = coded_w;
        p.c_stride = chroma_w;
        p.coding_type = f.coding_type;
        p.temporal_reference = f.temporal_reference;
        p.decode_index = f.decode_index;
        p.display_index = display_count++;
        p.progressive_frame = f.progressive_frame;
        p.top_field_first = f.top_field_first;
        p.errors = f.errors;
        out(p);
    }

    void start_picture() {
        pic_started = true;
        pic_ok = false;
        if (!have_seq) {
            err = "imagem sem cabeçalho de sequência";
            ++st.errors;
            return;
        }
        if (picture_structure != 3 || chroma_format != 1 || pic_type < 1 || pic_type > 3) {
            err = picture_structure != 3 ? "imagem de campo (sem suporte)"
                  : chroma_format != 1   ? "croma que não é 4:2:0 (sem suporte)"
                                         : "tipo de imagem sem suporte (D)";
            ++st.unsupported;
            return;
        }
        if (!alloc_frames()) {
            err = "tamanho de imagem inválido";
            ++st.errors;
            return;
        }
        if ((pic_type == 2 && bwd == nullptr) || (pic_type == 3 && (fwd == nullptr || bwd == nullptr))) {
            ++st.dropped;  // um GOP aberto sem as referências de antes
            return;
        }
        if (pic_type == 3) {
            cur = nullptr;
            for (Frame& f : frames) {
                if (&f != fwd && &f != bwd) cur = &f;
            }
        } else {
            if (bwd != nullptr && bwd_pending) {
                emit(*bwd);
                bwd_pending = false;
            }
            cur = nullptr;
            for (Frame& f : frames) {
                if (&f != fwd && &f != bwd) cur = &f;
            }
        }
        cur->coding_type = pic_type;
        cur->temporal_reference = temporal_reference;
        cur->decode_index = decode_count++;
        cur->progressive_frame = progressive_frame;
        cur->top_field_first = top_field_first;
        cur->errors = 0;
        pic_errors = 0;
        mb_done.assign(size_t(mb_width) * size_t(mb_height), 0);
        pic_ok = true;
#ifdef NF_MPEG2_PICTURE_HOOK
        NF_MPEG2_PICTURE_HOOK(*this);
#endif
    }

    // Os macroblocos que nenhuma fatia trouxe: copiados da referência mais recente (cinza sem nenhuma).
    void conceal() {
        const Frame* src = pic_type == 3 ? bwd : bwd;
        for (int addr = 0; addr < mb_width * mb_height; ++addr) {
            if (mb_done[size_t(addr)]) continue;
            ++st.concealed;
            const int mx = addr % mb_width, my = addr / mb_width;
            for (int y = 0; y < 16; ++y) {
                uint8_t* d = cur->y + (my * 16 + y) * coded_w + mx * 16;
                if (src != nullptr) std::memcpy(d, src->y + (d - cur->y), 16);
                else std::memset(d, 128, 16);
            }
            for (int y = 0; y < 8; ++y) {
                const size_t off = size_t(my * 8 + y) * size_t(chroma_w) + size_t(mx * 8);
                if (src != nullptr) {
                    std::memcpy(cur->cb + off, src->cb + off, 8);
                    std::memcpy(cur->cr + off, src->cr + off, 8);
                } else {
                    std::memset(cur->cb + off, 128, 8);
                    std::memset(cur->cr + off, 128, 8);
                }
            }
        }
    }

    void finish_picture() {
        if (!pic_header) return;
        pic_header = false;
        if (!pic_started) return;
        pic_started = false;
        if (!pic_ok) return;
        pic_ok = false;
        conceal();
        cur->errors = pic_errors;
        ++st.pictures;
        if (pic_type == 3) {
            emit(*cur);
        } else {
            fwd = bwd;
            bwd = cur;
            bwd_pending = true;
        }
        cur = nullptr;
    }

    void end_of_sequence() {
        finish_picture();
        if (bwd != nullptr && bwd_pending) {
            emit(*bwd);
            bwd_pending = false;
        }
    }

    // ---------------------------------------------------------------- os vetores (7.6.3)
    // Um vetor (r, s): os dois componentes, com a previsão de PMV e o intervalo do f_code. field_frame: um vetor de
    // campo numa imagem de quadro (o vertical em linhas de campo: a previsão PMV / 2 e o PMV guardado x 2).
    bool motion_vector(BitReader& br, int r, int s, bool field_frame, bool dmv) {
        for (int t = 0; t < 2; ++t) {
            const int code = vlc_read(br, T->motion);
            if (code == kInvalid) return false;
            const int r_size = std::max(0, f_code[s][t] - 1);
            int residual = 0;
            if (r_size > 0 && code != 0) residual = int(br.read(r_size));
            if (dmv && vlc_read(br, T->dmv) == kInvalid) return false;
            const int f = 1 << r_size;
            const int high = 16 * f - 1, low = -16 * f, range = 32 * f;
            int delta;
            if (f == 1 || code == 0) delta = code;
            else {
                delta = (std::abs(code) - 1) * f + residual + 1;
                if (code < 0) delta = -delta;
            }
            const bool half = field_frame && t == 1;
            int v = (half ? (pmv[r][s][t] >> 1) : pmv[r][s][t]) + delta;
            if (v < low) v += range;
            if (v > high) v -= range;
            pmv[r][s][t] = half ? v * 2 : v;
            vec[r][s][t] = v;
        }
        return true;
    }

    // motion_vectors(s) de uma imagem de quadro (6.2.5.2), pelo tipo de previsão do macrobloco.
    bool motion_vectors(BitReader& br, int s, int mt) {
        if (mt == kMotionField) {
            field_sel[0][s] = int(br.read(1));
            if (!motion_vector(br, 0, s, true, false)) return false;
            field_sel[1][s] = int(br.read(1));
            return motion_vector(br, 1, s, true, false);
        }
        if (mt == kMotionDualPrime) {
            return motion_vector(br, 0, s, true, true);
        }
        if (!motion_vector(br, 0, s, false, false)) return false;
        pmv[1][s][0] = pmv[0][s][0];
        pmv[1][s][1] = pmv[0][s][1];
        return true;
    }

    // No MPEG-1 os vetores podem vir em pixels inteiros (full_pel): o vetor da previsão é em meio pixel.
    int mc_vector(int r, int s, int t) const { return full_pel[s] ? vec[r][s][t] * 2 : vec[r][s][t]; }

    // A previsão de um macrobloco numa direção (s 0 de frente, 1 de trás) a partir de ref.
    void predict_mb(const Frame& ref, int s, int mt, int mx, int my, bool avg) {
        uint8_t* dy = cur->y + (my * 16) * coded_w + mx * 16;
        const size_t coff = size_t(my * 8) * size_t(chroma_w) + size_t(mx * 8);
        if (mt == kMotionFrame) {
            const int vx = mc_vector(0, s, 0), vy = mc_vector(0, s, 1);
            predict_plane(ref.y, coded_w, coded_w, coded_h, mx * 16 + (vx >> 1), my * 16 + (vy >> 1), vx & 1, vy & 1,
                          16, 16, dy, coded_w, avg);
            const int cx = vx / 2, cy = vy / 2;  // 7.6.3.7: "/" trunca para zero
            predict_plane(ref.cb, chroma_w, chroma_w, chroma_h, mx * 8 + (cx >> 1), my * 8 + (cy >> 1), cx & 1, cy & 1,
                          8, 8, cur->cb + coff, chroma_w, avg);
            predict_plane(ref.cr, chroma_w, chroma_w, chroma_h, mx * 8 + (cx >> 1), my * 8 + (cy >> 1), cx & 1, cy & 1,
                          8, 8, cur->cr + coff, chroma_w, avg);
            return;
        }
        // Previsão de campo numa imagem de quadro (7.6.4): o campo de cima do macrobloco (as linhas pares)
        // pelo vetor 0 a partir do campo field_sel[0][s] da referência, o de baixo pelo vetor 1.
        for (int r = 0; r < 2; ++r) {
            const int sel = field_sel[r][s];
            const int vx = vec[r][s][0], vy = vec[r][s][1];
            predict_plane(ref.y + sel * coded_w, coded_w * 2, coded_w, coded_h / 2, mx * 16 + (vx >> 1),
                          my * 8 + (vy >> 1), vx & 1, vy & 1, 16, 8, dy + r * coded_w, coded_w * 2, avg);
            const int cx = vx / 2, cy = vy / 2;
            predict_plane(ref.cb + sel * chroma_w, chroma_w * 2, chroma_w, chroma_h / 2, mx * 8 + (cx >> 1),
                          my * 4 + (cy >> 1), cx & 1, cy & 1, 8, 4, cur->cb + coff + size_t(r * chroma_w),
                          chroma_w * 2, avg);
            predict_plane(ref.cr + sel * chroma_w, chroma_w * 2, chroma_w, chroma_h / 2, mx * 8 + (cx >> 1),
                          my * 4 + (cy >> 1), cx & 1, cy & 1, 8, 4, cur->cr + coff + size_t(r * chroma_w),
                          chroma_w * 2, avg);
        }
    }

    // A previsão do macrobloco inteiro pelas flags (de frente e/ou de trás; a média das duas).
    void predict_macroblock(int flags, int mt, int mx, int my) {
        if (pic_type == 2) {
            predict_mb(*bwd, 0, mt, mx, my, false);
            return;
        }
        const bool f = (flags & kMbForward) != 0, b = (flags & kMbBackward) != 0;
        if (f) predict_mb(*fwd, 0, mt, mx, my, false);
        if (b) predict_mb(*bwd, 1, mt, mx, my, f);
    }

    // ---------------------------------------------------------------- os blocos (7.2, 7.4, 7.5)
    bool intra_block(BitReader& br, int cc, bool& dc_only) {
        int16_t* b = blk;
        std::memset(b, 0, sizeof(blk));
        const int size = cc == 0 ? vlc_read(br, T->dc_luma) : vlc_read(br, T->dc_chroma);
        if (size == kInvalid) return false;
        int diff = 0;
        if (size > 0) {
            const int v = int(br.read(size));
            diff = (v >> (size - 1)) != 0 ? v : v - (1 << size) + 1;
        }
        dc_pred[cc] += diff;
        const int prec = mpeg1 ? 0 : intra_dc_precision;
        int dc = dc_pred[cc] * (8 >> prec);
        dc = dc < -2048 ? -2048 : dc > 2047 ? 2047 : dc;
        b[0] = int16_t(dc);
        int sum = dc;
        const uint8_t* w = cc == 0 ? intra_q : chroma_intra_q;
        const uint8_t* scan = alternate_scan ? kAlternateScan : kZigzag;
        const int table = (!mpeg1 && intra_vlc_format) ? 1 : 0;
        const int qs = qscale;
        int i = 0;
        bool any_ac = false;
        for (;;) {
            int run = 0, level = 0;
            const int r = dct_coefficient(br, *T, table, mpeg1, run, level);
            if (r == 0) break;
            if (r < 0) return false;
            i += run + 1;
            if (i > 63) return false;
            const int pos = scan[i];
            const int v = dequant_coefficient(level, w[pos], qs, true, mpeg1);
            b[pos] = int16_t(v);
            sum += v;
            any_ac = true;
        }
        if (!mpeg1 && (sum & 1) == 0) {
            b[63] = int16_t(b[63] ^ 1);
            any_ac = true;
        }
        dc_only = !any_ac;
        return true;
    }

    bool nonintra_block(BitReader& br, int cc, bool& dc_only) {
        int16_t* b = blk;
        std::memset(b, 0, sizeof(blk));
        const uint8_t* w = cc == 0 ? nonintra_q : chroma_nonintra_q;
        const uint8_t* scan = alternate_scan ? kAlternateScan : kZigzag;
        const int qs = qscale;
        int i = -1;
        int sum = 0;
        bool only_dc = true;
        bool first = true;
        for (;;) {
            int run = 0, level = 0;
            if (first && br.peek(1) == 1) {
                br.skip(1);
                level = br.read(1) ? -1 : 1;
            } else {
                const int r = dct_coefficient(br, *T, 0, mpeg1, run, level);
                if (r == 0) {
                    if (first) return false;  // um bloco codificado tem pelo menos um coeficiente
                    break;
                }
                if (r < 0) return false;
            }
            first = false;
            i += run + 1;
            if (i > 63) return false;
            const int pos = scan[i];
            const int v = dequant_coefficient(level, w[pos], qs, false, mpeg1);
            b[pos] = int16_t(v);
            sum += v;
            if (pos != 0) only_dc = false;
        }
        if (!mpeg1 && (sum & 1) == 0) {
            b[63] = int16_t(b[63] ^ 1);
            only_dc = false;
        }
        dc_only = only_dc;
        return true;
    }

    void do_idct(bool dc_only) {
        if (ref_idct) {
            idct_double(blk);
            return;
        }
        if (dc_only) {
            const int16_t v = int16_t(clamp_idct((blk[0] + 4) >> 3));
            for (int i = 0; i < 64; ++i) blk[i] = v;
            return;
        }
        idct_int(blk);
    }

    // O bloco da IDCT ([-256, 255]) no destino, saturado em [0, 255] (intra), ou somado à previsão e saturado. O
    // packus do SSE2 satura do mesmo jeito.
    void put_block(uint8_t* d, int stride) {
        const int16_t* b = blk;
#if NF_MPEG2_SSE2
        for (int y = 0; y < 8; ++y, d += stride, b += 8) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
            _mm_storel_epi64(reinterpret_cast<__m128i*>(d), _mm_packus_epi16(v, v));
        }
#else
        for (int y = 0; y < 8; ++y, d += stride, b += 8) {
            for (int x = 0; x < 8; ++x) d[x] = clamp_pixel(b[x]);
        }
#endif
    }
    void add_block(uint8_t* d, int stride) {
        const int16_t* b = blk;
#if NF_MPEG2_SSE2
        const __m128i zero = _mm_setzero_si128();
        for (int y = 0; y < 8; ++y, d += stride, b += 8) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b));
            const __m128i p = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(d)), zero);
            const __m128i s = _mm_add_epi16(p, v);
            _mm_storel_epi64(reinterpret_cast<__m128i*>(d), _mm_packus_epi16(s, s));
        }
#else
        for (int y = 0; y < 8; ++y, d += stride, b += 8) {
            for (int x = 0; x < 8; ++x) d[x] = clamp_pixel(d[x] + b[x]);
        }
#endif
    }

    // O destino do bloco k (0-3 luma, 4 Cb, 5 Cr) do macrobloco, com a DCT de campo (dct_type) nos de luma.
    uint8_t* block_dest(int k, int mx, int my, bool field_dct, int* stride) {
        if (k < 4) {
            uint8_t* base = cur->y + (my * 16) * coded_w + mx * 16 + (k & 1) * 8;
            if (field_dct) {
                *stride = coded_w * 2;
                return base + (k >> 1) * coded_w;
            }
            *stride = coded_w;
            return base + (k >> 1) * 8 * coded_w;
        }
        *stride = chroma_w;
        uint8_t* plane = k == 4 ? cur->cb : cur->cr;
        return plane + (my * 8) * chroma_w + mx * 8;
    }

    // ---------------------------------------------------------------- os macroblocos (6.2.5, 7.6.6)
    void skipped_macroblock(int addr) {
        const int mx = addr % mb_width, my = addr / mb_width;
        dc_pred[0] = dc_pred[1] = dc_pred[2] = 1 << (7 + (mpeg1 ? 0 : intra_dc_precision));
        ++st.skipped;
        ++st.macroblocks;
        if (pic_type == 2) {
            // P: previsão de quadro com o vetor zero; os PMV voltam a zero (7.6.6.2).
            std::memset(pmv, 0, sizeof(pmv));
            std::memset(vec, 0, sizeof(vec));
            mb_flags = kMbForward;
            motion_type = kMotionFrame;
            predict_macroblock(kMbForward, kMotionFrame, mx, my);
        } else if (pic_type == 3) {
            // B: o tipo e os vetores do macrobloco anterior (7.6.6.3); os PMV ficam.
            if ((mb_flags & (kMbForward | kMbBackward)) == 0) {
                mb_flags = kMbForward;  // depois de um intra (a norma não deixa): o vetor que houver
            }
            predict_macroblock(mb_flags, motion_type, mx, my);
        } else {
            return;  // numa imagem I não há pulados: fica para a ocultação
        }
        mb_done[size_t(addr)] = 1;
    }

    bool macroblock(BitReader& br, int addr) {
        const int mx = addr % mb_width, my = addr / mb_width;
        int flags = read_mb_type_fast(br);
        if (flags == kInvalid) return fail("macroblock_type inválido");
        int mt = kMotionFrame;
        bool field_dct = false;
        if (!frame_pred_frame_dct) {
            if (flags & (kMbForward | kMbBackward)) {
                mt = int(br.read(2));
                if (mt == 0) return fail("frame_motion_type reservado");
            }
            if (flags & (kMbIntra | kMbPattern)) field_dct = br.read(1) != 0;
        }
        if (flags & kMbQuant) qscale = quantiser_scale(int(br.read(5)), q_scale_type);
        if (flags & kMbIntra) {
            if (concealment_mv) {
                // Os vetores de ocultação: de quadro numa imagem de quadro; só atualizam os PMV.
                if (!motion_vectors(br, 0, kMotionFrame)) return fail("vetor de ocultação inválido");
                br.skip(1);  // marker_bit
            } else {
                std::memset(pmv, 0, sizeof(pmv));
            }
            if (mt == kMotionDualPrime) mt = kMotionFrame;
            ++st.macroblocks;
            for (int k = 0; k < 6; ++k) {
                bool dc_only = false;
                if (!intra_block(br, k < 4 ? 0 : k - 3, dc_only)) return fail("coeficiente intra inválido");
#ifdef NF_MPEG2_BLOCK_HOOK
                int16_t coeffs[64];
                std::memcpy(coeffs, blk, sizeof(coeffs));
#endif
                do_idct(dc_only);
                int stride = 0;
                uint8_t* d = block_dest(k, mx, my, field_dct, &stride);
                put_block(d, stride);
#ifdef NF_MPEG2_BLOCK_HOOK
                NF_MPEG2_BLOCK_HOOK(cur->decode_index, pic_type, k, mx, my, true, coeffs, d, stride);
#endif
            }
            st.intra_blocks += 6;
            mb_flags = kMbIntra;
            mb_done[size_t(addr)] = 1;
            return true;
        }
        if (flags & kMbForward) {
            if (!motion_vectors(br, 0, mt)) return fail("vetor de movimento inválido");
        }
        if (flags & kMbBackward) {
            if (!motion_vectors(br, 1, mt)) return fail("vetor de movimento inválido");
        }
        if (mt == kMotionDualPrime) {
            ++st.unsupported;
            return fail("dual prime (sem suporte)");
        }
        int cbp = 0;
        if (flags & kMbPattern) {
            cbp = vlc_read(br, T->cbp);
            if (cbp == kInvalid) return fail("coded_block_pattern inválido");
        }
        dc_pred[0] = dc_pred[1] = dc_pred[2] = 1 << (7 + (mpeg1 ? 0 : intra_dc_precision));
        if (pic_type == 2 && (flags & kMbForward) == 0) {
            // P sem vetor ("No MC"): previsão de quadro com o vetor zero; os PMV voltam a zero (7.6.3.4).
            std::memset(pmv, 0, sizeof(pmv));
            std::memset(vec, 0, sizeof(vec));
            mt = kMotionFrame;
            flags |= kMbForward;
        }
        predict_macroblock(flags, mt, mx, my);
        for (int k = 0; k < 6; ++k) {
            if ((cbp & (32 >> k)) == 0) continue;
            bool dc_only = false;
            if (!nonintra_block(br, k < 4 ? 0 : k - 3, dc_only)) return fail("coeficiente inválido");
#ifdef NF_MPEG2_BLOCK_HOOK
            int16_t coeffs[64];
            std::memcpy(coeffs, blk, sizeof(coeffs));
#endif
            do_idct(dc_only);
            int stride = 0;
            uint8_t* d = block_dest(k, mx, my, field_dct, &stride);
            add_block(d, stride);
#ifdef NF_MPEG2_BLOCK_HOOK
            NF_MPEG2_BLOCK_HOOK(cur->decode_index, pic_type, k, mx, my, false, coeffs, d, stride);
#endif
            ++st.coded_blocks;
        }
        mb_flags = flags;
        motion_type = mt;
        ++st.macroblocks;
        mb_done[size_t(addr)] = 1;
        return true;
    }

    int read_mb_type_fast(BitReader& br) {
        const int v = vlc_read(br, T->mb_type[pic_type]);
        return v == 0 ? kInvalid : v;
    }

    bool fail(const char* why) {
        err = why;
        return false;
    }

    // 6.2.4 slice e os macroblocos dela, até o próximo código de início.
    void slice(int code, const uint8_t* p, const uint8_t* end) {
        ++st.slices;
        BitReader br(p, size_t(end - p));
        int row = code - 1;
        if (!mpeg1 && vertical_size > 2800) row += int(br.read(3)) << 7;
        if (row >= mb_height) {
            slice_error("fatia fora da imagem");
            return;
        }
        qscale = quantiser_scale(int(br.read(5)), mpeg1 ? false : q_scale_type);
        if (!mpeg1 && br.read(1)) {
            br.skip(1 + 7);  // intra_slice, reserved_bits
            while (br.read(1)) br.skip(8);
        } else if (mpeg1) {
            while (br.read(1)) br.skip(8);
        }
        dc_pred[0] = dc_pred[1] = dc_pred[2] = 1 << (7 + (mpeg1 ? 0 : intra_dc_precision));
        std::memset(pmv, 0, sizeof(pmv));
        std::memset(vec, 0, sizeof(vec));
        mb_flags = 0;
        motion_type = kMotionFrame;
        const int total = mb_width * mb_height;
        int addr = row * mb_width - 1;
        bool first = true;
        for (;;) {
            int inc = 0;
            for (;;) {
                const int v = vlc_read(br, T->mba);
                if (v == kMbEscape) {
                    inc += 33;
                    continue;
                }
                if (v == kMbStuffing) continue;
                if (v == kInvalid) {
                    slice_error("macroblock_address_increment inválido");
                    return;
                }
                inc += v;
                break;
            }
            if (first) {
                addr += inc;
                first = false;
            } else {
                for (int k = 1; k < inc && addr + k < total; ++k) skipped_macroblock(addr + k);
                addr += inc;
            }
            if (addr >= total) {
                slice_error("macrobloco fora da imagem");
                return;
            }
            if (!macroblock(br, addr)) {
                slice_error(nullptr);
                return;
            }
            if (br.overrun()) {
                slice_error("a fatia acabou no meio de um macrobloco");
                return;
            }
            // O fim da fatia: 23 zeros adiante (o enchimento e o próximo código de início; depois do fim da entrada,
            // a folga de zeros).
            if (br.peek(23) == 0) break;
        }
    }

    void slice_error(const char* why) {
        if (why != nullptr) err = why;
        ++st.errors;
        ++pic_errors;
    }

    void handle(uint8_t code, const uint8_t* body, const uint8_t* next) {
        BitReader br(body, size_t(next - body));
        if (code >= 0x01 && code <= 0xaf) {
            if (!pic_header) return;
            if (!pic_started) start_picture();
            if (pic_ok) slice(code, body, next);
            return;
        }
        switch (code) {
            case 0x00:
                finish_picture();
                picture_header(br);
                pic_header = true;
                pic_started = false;
                break;
            case 0xb3:
                finish_picture();
                sequence_header(br);
                break;
            case 0xb5:
                extension(br);
                break;
            case 0xb8:
                finish_picture();
                break;
            case 0xb7:
                end_of_sequence();
                break;
            default:
                break;  // user data e o resto
        }
    }
};

Mpeg2VideoDecoder::Mpeg2VideoDecoder() : s_(new State) {}
Mpeg2VideoDecoder::Mpeg2VideoDecoder(Output out) : s_(new State) { s_->out = std::move(out); }
Mpeg2VideoDecoder::~Mpeg2VideoDecoder() { delete s_; }

void Mpeg2VideoDecoder::set_output(Output out) { s_->out = std::move(out); }

void Mpeg2VideoDecoder::decode(const uint8_t* data, size_t size) {
    State& s = *s_;
    s.buf.assign(data, data + size);
    s.buf.resize(size + 8, 0);
    const uint8_t* b = s.buf.data();
    const uint8_t* end = b + size;
    const uint8_t* p = find_start_code(b, end);
    while (p < end) {
        const uint8_t* body = p + 4;
        if (body > end) break;
        const uint8_t* next = find_start_code(body, end);
        s.handle(p[3], body, next);
        p = next;
    }
    s.finish_picture();
}

void Mpeg2VideoDecoder::finish() {
    s_->end_of_sequence();
}

void Mpeg2VideoDecoder::reset() {
    State& s = *s_;
    s.fwd = s.bwd = s.cur = nullptr;
    s.bwd_pending = false;
    s.pic_header = s.pic_started = s.pic_ok = false;
    s.decode_count = s.display_count = 0;
}

int Mpeg2VideoDecoder::width() const { return s_->horizontal_size; }
int Mpeg2VideoDecoder::height() const { return s_->vertical_size; }

double Mpeg2VideoDecoder::frame_rate() const {
    static const double kRates[16] = {0.0, 24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 50.0,
                                      60000.0 / 1001.0, 60.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const State& s = *s_;
    const double base = kRates[s.frame_rate_code & 15];
    return base * double(s.frame_rate_n + 1) / double(s.frame_rate_d + 1);
}

bool Mpeg2VideoDecoder::mpeg1() const { return s_->mpeg1; }
const Mpeg2Stats& Mpeg2VideoDecoder::stats() const { return s_->st; }
const std::string& Mpeg2VideoDecoder::last_error() const { return s_->err; }
void Mpeg2VideoDecoder::set_reference_idct(bool on) { s_->ref_idct = on; }

}  // namespace nf
