// Testes do decodificador de MPEG-2 do port (platform/mpeg2_decoder.h), sem dados do jogo:
//   - os VLC do Anexo B: cada código digitado de novo aqui (outra forma, da norma) e lido pelo decodificador;
//   - a IDCT pelo procedimento da IEEE 1180 (o que o Anexo A da 13818-2 pede): blocos aleatórios do gerador da norma,
//     a DCT direta e a IDCT de referência em double, os limites de pico, de erro quadrático e de erro médio;
//   - a quantização inversa (7.4) em casos feitos à mão: intra, não intra, as duas escalas, a saturação, o controle de
//     descasamento, o ajuste para ímpar do MPEG-1;
//   - a previsão com meio pixel (7.6.4) contra a fórmula;
//   - um codificador mínimo feito aqui (escrito à parte do decodificador: as tabelas, a DCT, a quantização, a previsão e
//     a reconstrução de novo) que gera sequências pequenas I/P/B com o que a norma permite (macroblocos pulados, sem
//     vetor, intra em P, vetores com meio pixel e negativos, previsão e DCT de campo, as duas varreduras, os dois VLC de
//     intra, mudança de quantização, fatias no meio da linha, macroblock_escape, vetores de ocultação, DC de 10 bits,
//     MPEG-1 com escape de 8/16 bits e full_pel): o decodificador tem de dar a reconstrução do codificador (a menos dos
//     empates da IDCT) na ordem de exibição, e a imagem tem de ficar perto da original (o erro da quantização);
//   - a fachada Mpeg2Decoder com o decodificador do port (NV12) e a escolha [video] movie_decoder;
//   - um fluxo estragado não derruba nada.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/xxh3.h"
#include "platform/movie_decoder.h"
#include "platform/mpeg2_decoder.h"
#include "test_framework.h"

namespace {

using nf::mpeg2::BitReader;

// ---------------------------------------------------------------- escrita de bits

class BitWriter {
public:
    void bit(int b) {
        cur_ = uint8_t((cur_ << 1) | (b & 1));
        if (++n_ == 8) {
            out_.push_back(cur_);
            cur_ = 0;
            n_ = 0;
        }
    }
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) bit(int((v >> i) & 1));
    }
    void code(const char* s) {
        for (; *s != 0; ++s) {
            if (*s == '0' || *s == '1') bit(*s == '1');
        }
    }
    void align() {
        while (n_ != 0) bit(0);
    }
    void start_code(uint8_t c) {
        align();
        out_.push_back(0);
        out_.push_back(0);
        out_.push_back(1);
        out_.push_back(c);
    }
    const std::vector<uint8_t>& bytes() {
        align();
        return out_;
    }
    size_t size() const { return out_.size(); }

private:
    std::vector<uint8_t> out_;
    uint8_t cur_ = 0;
    int n_ = 0;
};

// Um leitor sobre uma cópia com folga de zeros (o BitReader lê 4 bytes adiante).
struct Bits {
    std::vector<uint8_t> data;
    BitReader br;
    explicit Bits(BitWriter& w) : data(w.bytes()) {
        data.resize(data.size() + 8, 0);
        br = BitReader(data.data(), data.size() - 8);
    }
};

// ---------------------------------------------------------------- as tabelas da norma, digitadas de novo

struct TCode {
    int value;
    const char* bits;
};

// B.1: o incremento de 1 a 33 e o escape.
const TCode kTMba[] = {
    {1, "1"},           {2, "011"},         {3, "010"},          {4, "0011"},         {5, "0010"},
    {6, "00011"},       {7, "00010"},       {8, "0000111"},      {9, "0000110"},      {10, "00001011"},
    {11, "00001010"},   {12, "00001001"},   {13, "00001000"},    {14, "00000111"},    {15, "00000110"},
    {16, "0000010111"}, {17, "0000010110"}, {18, "0000010101"},  {19, "0000010100"},  {20, "0000010011"},
    {21, "0000010010"}, {22, "00000100011"}, {23, "00000100010"}, {24, "00000100001"}, {25, "00000100000"},
    {26, "00000011111"}, {27, "00000011110"}, {28, "00000011101"}, {29, "00000011100"}, {30, "00000011011"},
    {31, "00000011010"}, {32, "00000011001"}, {33, "00000011000"},
};
const char* const kTMbaEscape = "00000001000";
const char* const kTMbStuffing = "00000001111";

enum : int { Q = nf::mpeg2::kMbQuant, F = nf::mpeg2::kMbForward, B = nf::mpeg2::kMbBackward,
             P = nf::mpeg2::kMbPattern, I = nf::mpeg2::kMbIntra };

// B.2, B.3, B.4.
const TCode kTTypeI[] = {{I, "1"}, {Q | I, "01"}};
const TCode kTTypeP[] = {{F | P, "1"}, {P, "01"}, {F, "001"}, {I, "00011"}, {Q | F | P, "00010"}, {Q | P, "00001"},
                         {Q | I, "000001"}};
const TCode kTTypeB[] = {{F | B, "10"}, {F | B | P, "11"}, {B, "010"}, {B | P, "011"}, {F, "0010"},
                         {F | P, "0011"}, {I, "00011"}, {Q | F | B | P, "00010"}, {Q | F | P, "000011"},
                         {Q | B | P, "000010"}, {Q | I, "000001"}};

// B.9, uma parte: os padrões que o codificador de teste usa (os outros viram 63).
const TCode kTCbp[] = {{60, "111"},  {4, "1101"},   {8, "1100"},   {16, "1011"},  {32, "1010"},  {12, "10011"},
                       {48, "10010"}, {20, "10001"}, {40, "10000"}, {28, "01111"}, {1, "01011"}, {61, "01010"},
                       {2, "01001"},  {62, "01000"}, {3, "001101"}, {63, "001100"}, {5, "0010111"}, {7, "00011111"},
                       {31, "000000111"}, {27, "000000011"}, {39, "000000010"}};

// B.10 sem o sinal: |motion_code| de 0 a 16 (depois o bit de sinal, 1 = negativo, menos no 0).
const char* const kTMotion[17] = {"1",          "01",         "001",        "0001",       "000011",
                                  "0000101",    "0000100",    "0000011",    "000001011",  "000001010",
                                  "000001001",  "0000010001", "0000010000", "0000001111", "0000001110",
                                  "0000001101", "0000001100"};

// B.11.
const TCode kTDmv[] = {{0, "0"}, {1, "10"}, {-1, "11"}};

// B.12, B.13.
const char* const kTDcLuma[12] = {"100",     "00",       "01",        "101",       "110",       "1110",
                                  "11110",   "111110",   "1111110",   "11111110",  "111111110", "111111111"};
const char* const kTDcChroma[12] = {"00",       "01",        "10",         "110",        "1110",       "11110",
                                    "111110",   "1111110",   "11111110",   "111111110",  "1111111110", "1111111111"};

// B.14 e B.15, uma parte (sem o sinal). O resto vai por escape.
struct TDct {
    int run, level;
    const char* bits;
};
const TDct kTDct0[] = {
    {0, 1, "11"},           {1, 1, "011"},          {0, 2, "0100"},         {2, 1, "0101"},
    {0, 3, "00101"},        {3, 1, "00111"},        {4, 1, "00110"},        {1, 2, "000110"},
    {5, 1, "000111"},       {6, 1, "000101"},       {7, 1, "000100"},       {0, 4, "0000110"},
    {2, 2, "0000100"},      {8, 1, "0000111"},      {9, 1, "0000101"},      {0, 5, "00100110"},
    {0, 6, "00100001"},     {1, 3, "00100101"},     {3, 2, "00100100"},     {10, 1, "00100111"},
    {11, 1, "00100011"},    {12, 1, "00100010"},    {13, 1, "00100000"},    {0, 7, "0000001010"},
    {1, 4, "0000001100"},   {2, 3, "0000001011"},   {4, 2, "0000001111"},   {5, 2, "0000001001"},
    {14, 1, "0000001110"},  {15, 1, "0000001101"},  {16, 1, "0000001000"},  {0, 8, "000000011101"},
    {0, 9, "000000011000"}, {0, 10, "000000010011"}, {0, 11, "000000010000"}, {1, 5, "000000011011"},
    {2, 4, "000000010100"}, {3, 3, "000000011100"}, {17, 1, "000000011111"}, {21, 1, "000000010110"},
    {0, 12, "0000000011010"}, {0, 15, "0000000010111"}, {1, 6, "0000000010110"}, {10, 2, "0000000010000"},
    {26, 1, "0000000011011"}, {0, 16, "00000000011111"}, {0, 31, "00000000010000"},
    {0, 32, "000000000011000"}, {0, 40, "000000000010000"}, {1, 8, "000000000011111"}, {1, 14, "000000000011001"},
    {1, 15, "0000000000010011"}, {1, 18, "0000000000010000"}, {6, 3, "0000000000010100"},
    {16, 2, "0000000000010101"}, {31, 1, "0000000000011011"},
};
const TDct kTDct1[] = {
    {0, 1, "10"},          {1, 1, "010"},         {0, 2, "110"},          {2, 1, "00101"},
    {0, 3, "0111"},        {3, 1, "00111"},       {4, 1, "000110"},       {1, 2, "00110"},
    {5, 1, "000111"},      {6, 1, "0000110"},     {7, 1, "0000100"},      {0, 4, "11100"},
    {2, 2, "0000111"},     {8, 1, "0000101"},     {9, 1, "1111000"},      {0, 5, "11101"},
    {0, 6, "000101"},      {1, 3, "1111001"},     {3, 2, "00100110"},     {10, 1, "1111010"},
    {11, 1, "00100001"},   {12, 1, "00100101"},   {13, 1, "00100100"},    {0, 7, "000100"},
    {1, 4, "00100111"},    {2, 3, "11111100"},    {4, 2, "11111101"},     {5, 2, "000000100"},
    {14, 1, "000000101"},  {15, 1, "000000111"},  {16, 1, "0000001101"},  {0, 8, "1111011"},
    {0, 9, "1111100"},     {0, 10, "00100011"},   {0, 11, "00100010"},    {1, 5, "00100000"},
    {2, 4, "0000001100"},  {3, 3, "000000011100"}, {4, 3, "000000010010"}, {21, 1, "000000010110"},
    {0, 12, "11111010"},   {0, 13, "11111011"},   {0, 14, "11111110"},    {0, 15, "11111111"},
    {1, 6, "0000000010110"}, {10, 2, "0000000010000"}, {26, 1, "0000000011011"}, {0, 16, "00000000011111"},
    {0, 40, "000000000010000"}, {31, 1, "0000000000011011"},
};
const char* const kTEob[2] = {"10", "0110"};
const char* const kTEscape = "000001";

const TDct* find_dct(int table, int run, int level) {
    const TDct* t = table == 0 ? kTDct0 : kTDct1;
    const size_t n = table == 0 ? sizeof(kTDct0) / sizeof(kTDct0[0]) : sizeof(kTDct1) / sizeof(kTDct1[0]);
    for (size_t i = 0; i < n; ++i) {
        if (t[i].run == run && t[i].level == level) return &t[i];
    }
    return nullptr;
}

const TCode* find_code(const TCode* t, size_t n, int value) {
    for (size_t i = 0; i < n; ++i) {
        if (t[i].value == value) return &t[i];
    }
    return nullptr;
}

// As varreduras: o zigue-zague pela regra das diagonais; a alternada da figura 7-3 digitada de novo.
std::vector<int> zigzag() {
    std::vector<int> s;
    for (int d = 0; d < 15; ++d) {
        for (int i = 0; i <= d; ++i) {
            const int a = d % 2 == 0 ? d - i : i;  // a linha: diagonais pares sobem, ímpares descem
            const int b = d - a;
            if (a < 8 && b < 8) s.push_back(a * 8 + b);
        }
    }
    return s;
}
const int kTAlternate[64] = {0,  8,  16, 24, 1,  9,  2,  10, 17, 25, 32, 40, 48, 56, 57, 49,
                             41, 33, 26, 18, 3,  11, 4,  12, 19, 27, 34, 42, 50, 58, 35, 43,
                             51, 59, 20, 28, 5,  13, 6,  14, 21, 29, 36, 44, 52, 60, 37, 45,
                             53, 61, 22, 30, 7,  15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63};

// ---------------------------------------------------------------- referências em double

// cos(m pi / 16) para m de 0 a 8, como literais: o std::cos muda no último bit de uma libm para outra, e o codificador de
// teste tem de gerar o mesmo fluxo em toda plataforma (as contas em double depois disso são as mesmas no IEEE).
const double kCos16[9] = {1.0,
                          0.9807852804032304,
                          0.9238795325112867,
                          0.8314696123025452,
                          0.7071067811865476,
                          0.5555702330196023,
                          0.38268343236508984,
                          0.19509032201612833,
                          0.0};

double cos16(int m) {  // cos(m pi / 16) para qualquer m >= 0
    m %= 32;
    if (m > 16) m = 32 - m;
    return m <= 8 ? kCos16[m] : -kCos16[16 - m];
}

// c[x][u] = C(u)/2 cos((2x+1) u pi / 16): a DCT 8x8 ortonormal (7.5).
struct Cosines {
    double c[8][8];
    Cosines() {
        for (int x = 0; x < 8; ++x) {
            for (int u = 0; u < 8; ++u) c[x][u] = (u == 0 ? kCos16[4] : 1.0) * 0.5 * cos16((2 * x + 1) * u);
        }
    }
};
const Cosines& cosines() {
    static const Cosines c;
    return c;
}

void ref_fdct(const double in[64], double out[64]) {
    const auto& c = cosines().c;
    double t[64];
    for (int y = 0; y < 8; ++y) {
        for (int u = 0; u < 8; ++u) {
            double s = 0.0;
            for (int x = 0; x < 8; ++x) s += c[x][u] * in[y * 8 + x];
            t[y * 8 + u] = s;
        }
    }
    for (int u = 0; u < 8; ++u) {
        for (int v = 0; v < 8; ++v) {
            double s = 0.0;
            for (int y = 0; y < 8; ++y) s += c[y][v] * t[y * 8 + u];
            out[v * 8 + u] = s;
        }
    }
}

void ref_idct_double(const int16_t in[64], double out[64]) {
    const auto& c = cosines().c;
    double t[64];
    for (int v = 0; v < 8; ++v) {
        for (int x = 0; x < 8; ++x) {
            double s = 0.0;
            for (int u = 0; u < 8; ++u) s += c[x][u] * in[v * 8 + u];
            t[v * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            double s = 0.0;
            for (int v = 0; v < 8; ++v) s += c[y][v] * t[v * 8 + x];
            out[y * 8 + x] = s;
        }
    }
}

// A IDCT de referência da IEEE 1180: arredondada ao inteiro mais próximo e saturada em [-256, 255].
void ref_idct(const int16_t in[64], int out[64]) {
    double d[64];
    ref_idct_double(in, d);
    for (int i = 0; i < 64; ++i) out[i] = std::clamp(int(std::floor(d[i] + 0.5)), -256, 255);
}

// 7.4.2 e 7.4.4, como a norma escreve (e o 2.4.4 do MPEG-1).
int t_dequant(int qf, int w, int qs, bool intra, bool mpeg1) {
    if (qf == 0) return 0;
    const int k = intra ? 0 : (qf > 0 ? 1 : -1);
    int v = ((2 * qf + k) * w * qs) / 32;  // / trunca para zero
    if (mpeg1 && (v % 2) == 0) v -= (v > 0) - (v < 0);
    return std::clamp(v, -2048, 2047);
}

void t_mismatch(int f[64]) {
    int sum = 0;
    for (int i = 0; i < 64; ++i) sum += f[i];
    if (sum % 2 == 0) f[63] += (f[63] % 2 != 0) ? -1 : 1;
}

// Um pixel da previsão com meio pixel (7.6.4): p[] no plano, (x2, y2) em meios pixels.
int t_pred(const uint8_t* p, int stride, int pw, int ph, int x2, int y2) {
    const int x = x2 >> 1, y = y2 >> 1, hx = x2 & 1, hy = y2 & 1;
    CHECK(x >= 0 && y >= 0 && x + hx < pw && y + hy < ph);
    auto at = [&](int xx, int yy) { return int(p[yy * stride + xx]); };
    if (!hx && !hy) return at(x, y);
    if (hx && !hy) return (at(x, y) + at(x + 1, y) + 1) / 2;
    if (!hx && hy) return (at(x, y) + at(x, y + 1) + 1) / 2;
    return (at(x, y) + at(x + 1, y) + at(x, y + 1) + at(x + 1, y + 1) + 2) / 4;
}

// ---------------------------------------------------------------- um codificador mínimo

struct Frame {
    int w = 0, h = 0;
    std::vector<uint8_t> p[3];
    void alloc(int W, int H) {
        w = W;
        h = H;
        p[0].assign(size_t(W * H), 0);
        p[1].assign(size_t(W * H / 4), 0);
        p[2].assign(size_t(W * H / 4), 0);
    }
    int pw(int c) const { return c == 0 ? w : w / 2; }
    int ph(int c) const { return c == 0 ? h : h / 2; }
};

// Uma onda triangular em [-amp, amp] de período period (só contas inteiras: a mesma em qualquer plataforma).
int tri(int v, int period, int amp) {
    v = ((v % period) + period) % period;
    const int half = period / 2;
    return (v < half ? v : period - v) * 2 * amp / half - amp;
}

// Uma imagem de teste: ondas que andam com t, mais um ruído fixo (para os coeficientes altos) e bordas fortes.
Frame make_source(int W, int H, int t) {
    Frame f;
    f.alloc(W, H);
    uint32_t r = 12345;
    for (int c = 0; c < 3; ++c) {
        const int w = f.pw(c), h = f.ph(c);
        const int s = c == 0 ? 2 : 1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                r = r * 1664525u + 1013904223u;
                int v = 128 + tri(x * s * 4 + t * 6, 120, 70) * tri(y * s * 3 - t * 4, 200, 100) / 100 +
                        (c == 0 ? tri(x + y + 3 * t, 14, 25) : 0) + int(r >> 27) - 16;
                if (c == 0 && ((x / 9 + y / 7 + t) % 5) == 0) v = 230;
                f.p[c][size_t(y * w + x)] = uint8_t(std::clamp(v, 0, 255));
            }
        }
    }
    return f;
}

struct EncParams {
    int width = 64, height = 48;
    bool mpeg1 = false;
    bool full_pel = false;           // MPEG-1
    int intra_dc_precision = 0;
    bool q_scale_type = false;
    bool intra_vlc_format = false;
    bool alternate_scan = false;
    bool frame_pred_frame_dct = true;
    bool concealment = false;
    bool custom_matrices = false;
    int f_code = 2;
    double skip_prob = 0.15;
    bool split_slices = true;
    bool stuffing = false;           // MPEG-1: macroblock_stuffing antes de alguns incrementos
    // Exercitar a sintaxe: quantizador ao acaso (inclusive o maior), coeficientes grandes ao acaso (escapes,
    // saturação). Desligado: um codificador comportado, quantizador de 2 a max_qcode, para medir a qualidade.
    bool noise = true;
    int max_qcode = 31;
};

class TestEncoder {
public:
    TestEncoder(const EncParams& p, uint32_t seed) : p_(p), rng_(seed) {
        const std::vector<int> zz = zigzag();
        for (int i = 0; i < 64; ++i) {
            zigzag_[i] = zz[size_t(i)];
            // A matriz intra padrão (6.3.11), digitada de novo.
            static const int kIntra[64] = {8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37,
                                           19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
                                           22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
                                           26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};
            intra_q_[i] = kIntra[i];
            nonintra_q_[i] = 16;
        }
        if (p_.custom_matrices) {
            for (int i = 0; i < 64; ++i) {
                intra_q_[i] = i == 0 ? 8 : 8 + (i * 7) % 60;
                nonintra_q_[i] = 10 + (i * 5) % 40;
            }
        }
    }

    void sequence_header() {
        bw_.start_code(0xb3);
        bw_.put(uint32_t(p_.width), 12);
        bw_.put(uint32_t(p_.height), 12);
        bw_.put(2, 4);       // 4:3
        bw_.put(5, 4);       // 30 fps
        bw_.put(0x3ffff, 18);
        bw_.put(1, 1);
        bw_.put(112, 10);
        bw_.put(0, 1);
        if (p_.custom_matrices) {
            bw_.put(1, 1);
            for (int i = 0; i < 64; ++i) bw_.put(uint32_t(intra_q_[zigzag_[i]]), 8);
            bw_.put(1, 1);
            for (int i = 0; i < 64; ++i) bw_.put(uint32_t(nonintra_q_[zigzag_[i]]), 8);
        } else {
            bw_.put(0, 1);
            bw_.put(0, 1);
        }
        if (!p_.mpeg1) {
            bw_.start_code(0xb5);
            bw_.put(1, 4);      // sequence_extension
            bw_.put(0x48, 8);   // MP@ML
            bw_.put(1, 1);      // progressive_sequence
            bw_.put(1, 2);      // 4:2:0
            bw_.put(0, 2);
            bw_.put(0, 2);
            bw_.put(0, 12);
            bw_.put(1, 1);
            bw_.put(0, 8);
            bw_.put(0, 1);
            bw_.put(0, 2);
            bw_.put(0, 5);
        }
    }

    void gop(bool closed) {
        bw_.start_code(0xb8);
        bw_.put(0, 25);
        bw_.put(closed ? 1 : 0, 1);
        bw_.put(0, 1);
    }

    void sequence_end() { bw_.start_code(0xb7); }

    // Codifica src como uma imagem do tipo type (1 I, 2 P, 3 B) prevista de fwd/bwd (as reconstruções), e devolve a
    // reconstrução (o que um decodificador tem de dar). unit_start: onde a unidade desta imagem começa nos bytes.
    Frame picture(int type, int temporal_reference, const Frame& src, const Frame* fwd, const Frame* bwd) {
        type_ = type;
        fwd_ = fwd;
        bwd_ = bwd;
        src_ = &src;
        rec_.alloc(p_.width, p_.height);
        bw_.start_code(0x00);
        bw_.put(uint32_t(temporal_reference), 10);
        bw_.put(uint32_t(type), 3);
        bw_.put(0xffff, 16);
        if (type == 2 || type == 3) {
            bw_.put(p_.mpeg1 && p_.full_pel ? 1 : 0, 1);
            bw_.put(p_.mpeg1 ? uint32_t(p_.f_code) : 7, 3);
        }
        if (type == 3) {
            bw_.put(p_.mpeg1 && p_.full_pel ? 1 : 0, 1);
            bw_.put(p_.mpeg1 ? uint32_t(p_.f_code) : 7, 3);
        }
        bw_.put(0, 1);  // extra_bit_picture
        if (!p_.mpeg1) {
            bw_.start_code(0xb5);
            bw_.put(8, 4);
            // f_code[s][t]: 15 onde não se usa (as de trás numa P; numa I as de frente só com os vetores de ocultação).
            for (int i = 0; i < 4; ++i) {
                const bool used = type == 3 || (i < 2 && (type == 2 || p_.concealment));
                bw_.put(used ? uint32_t(p_.f_code) : 15, 4);
            }
            bw_.put(uint32_t(p_.intra_dc_precision), 2);
            bw_.put(3, 2);  // imagem de quadro
            bw_.put(0, 1);  // top_field_first
            bw_.put(p_.frame_pred_frame_dct ? 1 : 0, 1);
            bw_.put(p_.concealment ? 1 : 0, 1);
            bw_.put(p_.q_scale_type ? 1 : 0, 1);
            bw_.put(p_.intra_vlc_format ? 1 : 0, 1);
            bw_.put(p_.alternate_scan ? 1 : 0, 1);
            bw_.put(0, 1);  // repeat_first_field
            bw_.put(1, 1);  // chroma_420_type
            bw_.put(1, 1);  // progressive_frame
            bw_.put(0, 1);  // composite_display_flag
        }
        const int mbw = p_.width / 16, mbh = p_.height / 16;
        for (int row = 0; row < mbh; ++row) {
            int split = mbw;
            if (p_.split_slices && mbw > 2 && rnd(3) == 0) split = 1 + rnd(mbw - 1);
            slice(row, 0, split);
            if (split < mbw) slice(row, split, mbw);
        }
        return rec_;
    }

    const std::vector<uint8_t>& bytes() { return bw_.bytes(); }
    // Os bytes até aqui, com o último completo (a próxima imagem começa num byte novo).
    size_t aligned_size() {
        bw_.align();
        return bw_.size();
    }
    int skipped() const { return skipped_; }
    int field_mbs() const { return field_mbs_; }
    int escapes() const { return escapes_; }

private:
    int rnd(int n) {
        rng_ = rng_ * 1103515245u + 12345u;
        return int((rng_ >> 8) % uint32_t(n));
    }
    double frand() { return double(rnd(1 << 16)) / 65536.0; }

    int qs_of(int code) const {
        static const int kNl[32] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12, 14, 16, 18, 20,  22,
                                    24, 28, 32, 36, 40, 44, 48, 52, 56, 64, 72, 80, 88, 96, 104, 112};
        return (!p_.mpeg1 && p_.q_scale_type) ? kNl[code] : 2 * code;
    }

    void slice(int row, int first, int end) {
        bw_.start_code(uint8_t(row + 1));
        qcode_ = p_.noise ? 1 + rnd(31) : 2 + rnd(std::max(1, p_.max_qcode - 1));
        if (p_.noise && rnd(4) == 0) qcode_ = 1;  // níveis grandes: escapes
        bw_.put(uint32_t(qcode_), 5);
        if (!p_.mpeg1 && rnd(3) == 0) {
            bw_.put(1, 1);  // intra_slice_flag
            bw_.put(0, 1);
            bw_.put(0, 7);
            bw_.put(1, 1);  // extra_bit_slice com um byte
            bw_.put(0xa5, 8);
        }
        bw_.put(0, 1);
        const int dc_reset = 1 << (7 + (p_.mpeg1 ? 0 : p_.intra_dc_precision));
        dc_[0] = dc_[1] = dc_[2] = dc_reset;
        std::memset(pmv_, 0, sizeof(pmv_));
        prev_flags_ = 0;
        int last_coded = -1;  // o incremento do primeiro é a coluna + 1 (o endereço conta do começo da linha)
        for (int col = first; col < end; ++col) {
            const bool edge = col == first || col == end - 1;
            if (!edge && type_ != 1 && frand() < p_.skip_prob && can_skip()) {
                skip_mb(col, row);
                continue;
            }
            int inc = col - last_coded;
            if (p_.stuffing && rnd(4) == 0) bw_.code(kTMbStuffing);
            while (inc > 33) {
                bw_.code(kTMbaEscape);
                inc -= 33;
                ++escapes_;
            }
            bw_.code(find_code(kTMba, 33, inc)->bits);
            coded_mb(col, row);
            last_coded = col;
        }
    }

    bool can_skip() const {
        if (type_ == 2) return true;
        return (prev_flags_ & (F | B)) != 0 && (prev_flags_ & I) == 0;
    }

    // ---- a previsão de um macrobloco numa direção, no buffer pred (16x16 + 2 x 8x8)
    void predict(const Frame& ref, int mx, int my, int s, bool field, uint8_t pred[3][256]) {
        for (int c = 0; c < 3; ++c) {
            const int n = c == 0 ? 16 : 8;
            const int pw = ref.pw(c), ph = ref.ph(c);
            for (int y = 0; y < n; ++y) {
                for (int x = 0; x < n; ++x) {
                    int v;
                    if (!field) {
                        int vx = vec_[0][s][0], vy = vec_[0][s][1];
                        if (p_.mpeg1 && p_.full_pel) {
                            vx *= 2;
                            vy *= 2;
                        }
                        if (c != 0) {
                            vx /= 2;
                            vy /= 2;
                        }
                        v = t_pred(ref.p[c].data(), pw, pw, ph, 2 * (mx * n + x) + vx, 2 * (my * n + y) + vy);
                    } else {
                        const int r = y & 1;  // a linha do campo de cima (0) ou de baixo (1)
                        int vx = vec_[r][s][0], vy = vec_[r][s][1];
                        if (c != 0) {
                            vx /= 2;
                            vy /= 2;
                        }
                        const int sel = sel_[r][s];
                        const int fy = (my * n + y) >> 1;
                        // O campo sel da referência: as linhas de paridade sel.
                        v = t_pred(ref.p[c].data() + sel * pw, 2 * pw, pw, ph / 2, 2 * (mx * n + x) + vx, 2 * fy + vy);
                    }
                    pred[c][y * n + x] = uint8_t(v);
                }
            }
        }
    }

    bool vector_ok(int mx, int my, bool field, int r, int s) const {
        // Todas as leituras dentro dos planos (a norma não deixa sair).
        for (int c = 0; c < 3; ++c) {
            const int n = c == 0 ? 16 : 8;
            const int pw = c == 0 ? p_.width : p_.width / 2, ph = c == 0 ? p_.height : p_.height / 2;
            int vx = vec_[r][s][0], vy = vec_[r][s][1];
            if (p_.mpeg1 && p_.full_pel) {
                vx *= 2;
                vy *= 2;
            }
            if (c != 0) {
                vx /= 2;
                vy /= 2;
            }
            const int x0 = ((2 * mx * n + vx) >> 1), x1 = ((2 * (mx * n + n - 1) + vx) >> 1) + (vx & 1);
            if (x0 < 0 || x1 >= pw) return false;
            if (!field) {
                const int y0 = ((2 * my * n + vy) >> 1), y1 = ((2 * (my * n + n - 1) + vy) >> 1) + (vy & 1);
                if (y0 < 0 || y1 >= ph) return false;
            } else {
                const int f0 = (my * n) >> 1, f1 = (my * n + n - 1) >> 1;
                const int y0 = ((2 * f0 + vy) >> 1), y1 = ((2 * f1 + vy) >> 1) + (vy & 1);
                if (y0 < 0 || y1 >= ph / 2) return false;
            }
        }
        return true;
    }

    void choose_vector(int mx, int my, bool field, int r, int s) {
        const int f = 1 << (p_.f_code - 1);
        for (int tries = 0; tries < 50; ++tries) {
            const int span = std::min(16 * f - 1, 20);
            vec_[r][s][0] = rnd(2 * span + 1) - span;
            vec_[r][s][1] = rnd(2 * span + 1) - span;
            if (field) vec_[r][s][1] = vec_[r][s][1] / 2;
            sel_[r][s] = rnd(2);
            if (vector_ok(mx, my, field, r, s)) return;
        }
        vec_[r][s][0] = vec_[r][s][1] = 0;
        sel_[r][s] = r;
    }

    // motion_vector(r, s): a diferença da previsão PMV, com o intervalo do f_code (7.6.3.1 ao contrário).
    void write_vector(int r, int s, bool field) {
        const int f = 1 << (p_.f_code - 1);
        for (int t = 0; t < 2; ++t) {
            const bool half = field && t == 1;
            const int pred = half ? (pmv_[r][s][t] >> 1) : pmv_[r][s][t];
            int delta = vec_[r][s][t] - pred;
            if (delta < -16 * f) delta += 32 * f;
            if (delta > 16 * f - 1) delta -= 32 * f;
            int code, residual = 0;
            if (f == 1 || delta == 0) {
                code = delta;
            } else {
                const int a = std::abs(delta);
                code = ((a - 1) / f + 1) * (delta < 0 ? -1 : 1);
                residual = (a - 1) % f;
            }
            bw_.code(kTMotion[std::abs(code)]);
            if (code != 0) bw_.put(code < 0 ? 1 : 0, 1);
            if (f > 1 && code != 0) bw_.put(uint32_t(residual), p_.f_code - 1);
            pmv_[r][s][t] = half ? vec_[r][s][t] * 2 : vec_[r][s][t];
        }
    }

    void write_vectors(int s, bool field) {
        if (field) {
            bw_.put(uint32_t(sel_[0][s]), 1);
            write_vector(0, s, true);
            bw_.put(uint32_t(sel_[1][s]), 1);
            write_vector(1, s, true);
        } else {
            write_vector(0, s, false);
            pmv_[1][s][0] = pmv_[0][s][0];
            pmv_[1][s][1] = pmv_[0][s][1];
        }
    }

    // A previsão do macrobloco pelas flags (média das duas direções).
    void form_prediction(int flags, bool field, int mx, int my, uint8_t out[3][256]) {
        uint8_t a[3][256], b[3][256];
        const bool f = (flags & F) != 0 || type_ == 2, bk = (flags & B) != 0;
        if (f) predict(type_ == 2 ? *fwd_ : *fwd_, mx, my, 0, field, a);
        if (bk) predict(*bwd_, mx, my, 1, field, b);
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < (c == 0 ? 256 : 64); ++i) {
                out[c][i] = f && bk ? uint8_t((a[c][i] + b[c][i] + 1) / 2) : f ? a[c][i] : b[c][i];
            }
        }
    }

    void store(int c, int mx, int my, const uint8_t* src) {
        const int n = c == 0 ? 16 : 8, w = rec_.pw(c);
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) rec_.p[c][size_t((my * n + y) * w + mx * n + x)] = src[y * n + x];
        }
    }

    void skip_mb(int mx, int my) {
        ++skipped_;
        const int dc_reset = 1 << (7 + (p_.mpeg1 ? 0 : p_.intra_dc_precision));
        dc_[0] = dc_[1] = dc_[2] = dc_reset;
        uint8_t pred[3][256];
        if (type_ == 2) {
            std::memset(pmv_, 0, sizeof(pmv_));
            std::memset(vec_, 0, sizeof(vec_));
            form_prediction(F, false, mx, my, pred);
            prev_flags_ = F;
            prev_field_ = false;
        } else {
            form_prediction(prev_flags_, prev_field_, mx, my, pred);
        }
        for (int c = 0; c < 3; ++c) store(c, mx, my, pred[c]);
    }

    // O bloco k (0-3 luma, 4 Cb, 5 Cr) do macrobloco em in[64] (o original ou o resíduo), com a DCT de campo.
    void gather(const uint8_t* const planes[3], const int stride[3], int k, int mx, int my, bool field_dct,
                const uint8_t pred[3][256], bool intra, double out[64]) const {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                int c, px, py, pi;
                if (k < 4) {
                    c = 0;
                    const int bx = (k & 1) * 8;
                    const int ry = field_dct ? (y * 2 + (k >> 1)) : ((k >> 1) * 8 + y);
                    px = mx * 16 + bx + x;
                    py = my * 16 + ry;
                    pi = ry * 16 + bx + x;
                } else {
                    c = k - 3;
                    px = mx * 8 + x;
                    py = my * 8 + y;
                    pi = y * 8 + x;
                }
                const int s = planes[c][size_t(py * stride[c] + px)];
                out[y * 8 + x] = intra ? double(s) : double(s - pred[c][pi]);
            }
        }
    }

    // Escreve os coeficientes de um bloco (QF em ordem de linhas). Para um intra o DC já está em qf[0].
    void write_block(const int qf[64], bool intra, int cc) {
        const int* scan = p_.alternate_scan && !p_.mpeg1 ? kTAlternate : zigzag_;
        int start = 0;
        if (intra) {
            const int diff = qf[0] - dc_[cc];
            dc_[cc] = qf[0];
            int size = 0;
            while ((1 << size) <= std::abs(diff)) ++size;
            bw_.code(cc == 0 ? kTDcLuma[size] : kTDcChroma[size]);
            if (size > 0) bw_.put(uint32_t(diff > 0 ? diff : diff + (1 << size) - 1), size);
            start = 1;
        }
        const int table = intra && p_.intra_vlc_format && !p_.mpeg1 ? 1 : 0;
        int run = 0;
        bool first = !intra;
        for (int i = start; i < 64; ++i) {
            const int l = qf[scan[i]];
            if (l == 0) {
                ++run;
                continue;
            }
            const int a = std::abs(l);
            if (first && run == 0 && a == 1) {
                bw_.code("1");
                bw_.put(l < 0 ? 1 : 0, 1);
            } else if (const TDct* d = find_dct(table, run, a)) {
                bw_.code(d->bits);
                bw_.put(l < 0 ? 1 : 0, 1);
            } else {
                ++escapes_;
                bw_.code(kTEscape);
                bw_.put(uint32_t(run), 6);
                if (p_.mpeg1) {
                    if (a < 128) bw_.put(uint32_t(l & 0xff), 8);
                    else if (l > 0) {
                        bw_.put(0, 8);
                        bw_.put(uint32_t(l), 8);
                    } else {
                        bw_.put(0x80, 8);
                        bw_.put(uint32_t(l + 256), 8);
                    }
                } else {
                    bw_.put(uint32_t(l & 0xfff), 12);
                }
            }
            first = false;
            run = 0;
        }
        bw_.code(kTEob[table]);
    }

    // Quantiza um bloco (qualquer QF vale: a reconstrução é a do que for escrito).
    void quantise(const double in[64], bool intra, int cc, int qs, int qf[64]) {
        double f[64];
        ref_fdct(in, f);
        const int* w = intra ? intra_q_ : nonintra_q_;
        const int lim = p_.mpeg1 ? 255 : 2047;
        for (int i = 0; i < 64; ++i) {
            if (intra && i == 0) {
                const int mult = 8 >> (p_.mpeg1 ? 0 : p_.intra_dc_precision);
                const int maxdc = (1 << (8 + (p_.mpeg1 ? 0 : p_.intra_dc_precision))) - 1;
                qf[0] = std::clamp(int(std::lround(f[0] / mult)), 0, maxdc);
                continue;
            }
            int q = int(std::lround(f[i] * 16.0 / (w[i] * qs)));
            // Um pouco de ruído nos coeficientes altos (exercita mais códigos e escapes).
            if (p_.noise && q == 0 && rnd(40) == 0) q = rnd(2) ? 1 : -1;
            if (p_.noise && rnd(200) == 0) q = (rnd(2) ? 1 : -1) * (40 + rnd(p_.mpeg1 ? 200 : 600));
            qf[i] = std::clamp(q, -lim, lim);
        }
        (void)cc;
    }

    // A reconstrução de um bloco: QF -> F (7.4) -> IDCT de referência -> + previsão.
    void reconstruct(const int qf[64], bool intra, int qs, int cc, int k, int mx, int my, bool field_dct,
                     const uint8_t pred[3][256]) {
        const int* w = intra ? intra_q_ : nonintra_q_;
        int f[64];
        for (int i = 0; i < 64; ++i) {
            if (intra && i == 0) f[0] = qf[0] * (8 >> (p_.mpeg1 ? 0 : p_.intra_dc_precision));
            else f[i] = t_dequant(qf[i], w[i], qs, intra, p_.mpeg1);
        }
        if (!p_.mpeg1) t_mismatch(f);
        int16_t f16[64];
        for (int i = 0; i < 64; ++i) f16[i] = int16_t(f[i]);
        int r[64];
        ref_idct(f16, r);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                int c, px, py, pi;
                if (k < 4) {
                    c = 0;
                    const int bx = (k & 1) * 8;
                    const int ry = field_dct ? (y * 2 + (k >> 1)) : ((k >> 1) * 8 + y);
                    px = mx * 16 + bx + x;
                    py = my * 16 + ry;
                    pi = ry * 16 + bx + x;
                } else {
                    c = k - 3;
                    px = mx * 8 + x;
                    py = my * 8 + y;
                    pi = y * 8 + x;
                }
                const int base = intra ? 0 : pred[c][pi];
                rec_.p[c][size_t(py * rec_.pw(c) + px)] = uint8_t(std::clamp(base + r[y * 8 + x], 0, 255));
            }
        }
        (void)cc;
    }

    void coded_mb(int mx, int my) {
        // O tipo.
        int flags;
        if (type_ == 1) flags = I;
        else if (type_ == 2) {
            const int pick = rnd(10);
            flags = pick < 5 ? (F | P) : pick < 7 ? F : pick < 9 ? P : I;
        } else {
            const int pick = rnd(12);
            flags = pick < 3 ? (F | B | P) : pick < 5 ? (F | P) : pick < 7 ? (B | P) : pick < 8 ? (F | B) : pick < 9 ? F
                    : pick < 10 ? B : pick < 11 ? (F | B | P) : I;
        }
        if (!p_.noise && (flags & I) == 0) flags |= P;  // comportado: sempre o resíduo
        if (rnd(4) == 0) flags |= Q;
        if ((flags & Q) && (flags & (I | P)) == 0) flags &= ~Q;  // quant só com intra ou padrão
        const bool fpfd = p_.frame_pred_frame_dct || p_.mpeg1;
        bool field = !fpfd && (flags & (F | B)) && rnd(2) == 0;
        bool field_dct = !fpfd && (flags & (I | P)) && rnd(2) == 0;
        int new_q = qcode_;
        if (flags & Q) new_q = p_.noise ? 1 + rnd(31) : 2 + rnd(std::max(1, p_.max_qcode - 1));

        // Os vetores.
        if (flags & F) choose_vector(mx, my, field, 0, 0);
        if (flags & F && field) choose_vector(mx, my, field, 1, 0);
        if (flags & B) choose_vector(mx, my, field, 0, 1);
        if (flags & B && field) choose_vector(mx, my, field, 1, 1);
        const bool conceal = (flags & I) && p_.concealment && !p_.mpeg1;
        if (conceal) {
            choose_vector(mx, my, false, 0, 0);
        }

        uint8_t pred[3][256] = {};
        const bool no_mc_p = type_ == 2 && (flags & (F | I)) == 0;
        if (no_mc_p) {
            std::memset(vec_, 0, sizeof(vec_));
            field = false;
        }
        if (!(flags & I)) form_prediction(flags, field, mx, my, pred);

        // Os coeficientes de cada bloco com o quantizador novo.
        const int qs = qs_of(new_q);
        int qf[6][64];
        int cbp = 0;
        const uint8_t* planes[3] = {src_->p[0].data(), src_->p[1].data(), src_->p[2].data()};
        const int stride[3] = {p_.width, p_.width / 2, p_.width / 2};
        for (int k = 0; k < 6; ++k) {
            double in[64];
            gather(planes, stride, k, mx, my, field_dct, pred, (flags & I) != 0, in);
            quantise(in, (flags & I) != 0, k < 4 ? 0 : k - 3, qs, qf[k]);
            bool any = false;
            for (int i = 0; i < 64; ++i) any = any || qf[k][i] != 0;
            if (any) cbp |= 32 >> k;
        }
        if (!(flags & I)) {
            if (flags & P) {
                if (cbp == 0) {
                    // Um bloco com padrão precisa de um coeficiente: um no primeiro bloco.
                    qf[0][0] = 1;
                    cbp = 32;
                }
                if (find_code(kTCbp, sizeof(kTCbp) / sizeof(kTCbp[0]), cbp) == nullptr) {
                    for (int k = 0; k < 6; ++k) {
                        if (cbp & (32 >> k)) continue;
                        qf[k][rnd(64)] = rnd(2) ? 2 : -1;
                    }
                    cbp = 63;
                }
            } else {
                cbp = 0;  // sem padrão: a previsão pura
                if (flags & Q) flags &= ~Q;
            }
        }
        if (!(flags & Q)) new_q = qcode_;
        const int qs_used = qs_of(new_q);
        if (qs_used != qs) {
            // A quantização foi feita com o quantizador que não ficou: refaz (os coeficientes não mudam de sentido).
            for (int k = 0; k < 6; ++k) {
                if (!(flags & I) && !(cbp & (32 >> k))) continue;
                double in[64];
                gather(planes, stride, k, mx, my, field_dct, pred, (flags & I) != 0, in);
                quantise(in, (flags & I) != 0, k < 4 ? 0 : k - 3, qs_used, qf[k]);
                if (!(flags & I)) {
                    bool any = false;
                    for (int i = 0; i < 64; ++i) any = any || qf[k][i] != 0;
                    if (!any) qf[k][0] = -1;
                }
            }
        }
        if (field || field_dct) ++field_mbs_;

        // O macroblock_type.
        const TCode* t = type_ == 1 ? find_code(kTTypeI, 2, flags)
                         : type_ == 2 ? find_code(kTTypeP, 7, flags)
                                      : find_code(kTTypeB, 11, flags);
        CHECK(t != nullptr);
        if (t == nullptr) return;
        bw_.code(t->bits);
        if (!fpfd) {
            if (flags & (F | B)) bw_.put(field ? 1 : 2, 2);
            if (flags & (I | P)) bw_.put(field_dct ? 1 : 0, 1);
        }
        if (flags & Q) {
            bw_.put(uint32_t(new_q), 5);
            qcode_ = new_q;
        }
        if (conceal) write_vectors(0, false);
        if (flags & F) write_vectors(0, field);
        if (flags & B) write_vectors(1, field);
        if (conceal) bw_.put(1, 1);  // marker_bit
        if (flags & P) bw_.code(find_code(kTCbp, sizeof(kTCbp) / sizeof(kTCbp[0]), cbp)->bits);

        const int dc_reset = 1 << (7 + (p_.mpeg1 ? 0 : p_.intra_dc_precision));
        if (flags & I) {
            if (!conceal) std::memset(pmv_, 0, sizeof(pmv_));
            for (int k = 0; k < 6; ++k) {
                write_block(qf[k], true, k < 4 ? 0 : k - 3);
                reconstruct(qf[k], true, qs_used, k < 4 ? 0 : k - 3, k, mx, my, field_dct, pred);
            }
        } else {
            dc_[0] = dc_[1] = dc_[2] = dc_reset;
            if (no_mc_p) std::memset(pmv_, 0, sizeof(pmv_));
            for (int c = 0; c < 3; ++c) store(c, mx, my, pred[c]);
            for (int k = 0; k < 6; ++k) {
                if (!(cbp & (32 >> k))) continue;
                write_block(qf[k], false, k < 4 ? 0 : k - 3);
                reconstruct(qf[k], false, qs_used, k < 4 ? 0 : k - 3, k, mx, my, field_dct, pred);
            }
        }
        prev_flags_ = no_mc_p ? F : flags;
        prev_field_ = field;
    }

    EncParams p_;
    uint32_t rng_;
    BitWriter bw_;
    int zigzag_[64];
    int intra_q_[64], nonintra_q_[64];
    int type_ = 1;
    const Frame* fwd_ = nullptr;
    const Frame* bwd_ = nullptr;
    const Frame* src_ = nullptr;
    Frame rec_;
    int qcode_ = 1;
    int dc_[3] = {};
    int pmv_[2][2][2] = {};
    int vec_[2][2][2] = {};
    int sel_[2][2] = {};
    int prev_flags_ = 0;
    bool prev_field_ = false;
    int skipped_ = 0, field_mbs_ = 0, escapes_ = 0;
};

// Uma sequência curta: I0 P3 B1 B2 P6 B4 B5 (ordem de codificação), num GOP fechado; devolve as reconstruções na
// ordem de exibição.
struct Encoded {
    std::vector<uint8_t> stream;
    std::vector<size_t> unit_starts;  // onde cada imagem começa (com os cabeçalhos antes dela)
    std::vector<Frame> recon;         // pela ordem de exibição
    std::vector<Frame> source;
    int skipped = 0, field_mbs = 0, escapes = 0;
};

Encoded encode_sequence(const EncParams& p, uint32_t seed, int frames = 7) {
    TestEncoder enc(p, seed);
    Encoded e;
    for (int t = 0; t < frames; ++t) e.source.push_back(make_source(p.width, p.height, t));
    e.recon.resize(size_t(frames));
    // Ordem de codificação: I0, depois cada P de 3 em 3 e os B antes dele.
    std::vector<std::pair<int, int>> order;  // (quadro, tipo)
    order.push_back({0, 1});
    for (int anchor = 3; anchor - 2 < frames; anchor += 3) {
        const int a = std::min(anchor, frames - 1);
        const int prev = anchor - 3;
        if (a <= prev) break;
        order.push_back({a, 2});
        for (int b = prev + 1; b < a; ++b) order.push_back({b, 3});
        if (a != anchor) break;
    }
    int last_ref = -1, cur_ref = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        const int fr = order[i].first, type = order[i].second;
        e.unit_starts.push_back(enc.aligned_size());
        if (i == 0) {
            enc.sequence_header();
            enc.gop(true);
        }
        if (type == 1) {
            e.recon[size_t(fr)] = enc.picture(1, fr, e.source[size_t(fr)], nullptr, nullptr);
            cur_ref = fr;
        } else if (type == 2) {
            e.recon[size_t(fr)] = enc.picture(2, fr, e.source[size_t(fr)], &e.recon[size_t(cur_ref)], nullptr);
            last_ref = cur_ref;
            cur_ref = fr;
        } else {
            e.recon[size_t(fr)] =
                enc.picture(3, fr, e.source[size_t(fr)], &e.recon[size_t(last_ref)], &e.recon[size_t(cur_ref)]);
        }
    }
    enc.sequence_end();
    e.stream = enc.bytes();
    e.skipped = enc.skipped();
    e.field_mbs = enc.field_mbs();
    e.escapes = enc.escapes();
    return e;
}

struct Decoded {
    std::vector<Frame> frames;
    std::vector<int> types, trs;
    nf::Mpeg2Stats stats;
    std::string error;
};

// Decodifica numa chamada só (units vazio) ou imagem por imagem.
Decoded decode_stream(const Encoded& e, bool by_units) {
    Decoded d;
    nf::Mpeg2VideoDecoder dec([&](const nf::Mpeg2Picture& p) {
        Frame f;
        f.alloc(p.width, p.height);
        for (int y = 0; y < p.height; ++y) std::memcpy(&f.p[0][size_t(y * p.width)], p.y + y * p.y_stride, size_t(p.width));
        for (int y = 0; y < p.chroma_height; ++y) {
            std::memcpy(&f.p[1][size_t(y * p.chroma_width)], p.cb + y * p.c_stride, size_t(p.chroma_width));
            std::memcpy(&f.p[2][size_t(y * p.chroma_width)], p.cr + y * p.c_stride, size_t(p.chroma_width));
        }
        d.frames.push_back(std::move(f));
        d.types.push_back(p.coding_type);
        d.trs.push_back(p.temporal_reference);
    });
    if (!by_units) {
        dec.decode(e.stream.data(), e.stream.size());
    } else {
        for (size_t i = 0; i < e.unit_starts.size(); ++i) {
            const size_t a = e.unit_starts[i];
            const size_t b = i + 1 < e.unit_starts.size() ? e.unit_starts[i + 1] : e.stream.size();
            dec.decode(e.stream.data() + a, b - a);
        }
    }
    dec.finish();
    d.stats = dec.stats();
    d.error = dec.last_error();
    return d;
}

struct Diff {
    int max = 0;
    long differing = 0;
    long pixels = 0;
};

Diff compare(const Frame& a, const Frame& b) {
    Diff d;
    for (int c = 0; c < 3; ++c) {
        for (size_t i = 0; i < a.p[c].size() && i < b.p[c].size(); ++i) {
            const int x = std::abs(int(a.p[c][i]) - int(b.p[c][i]));
            d.max = std::max(d.max, x);
            d.differing += x != 0;
            ++d.pixels;
        }
    }
    return d;
}

double psnr_luma(const Frame& a, const Frame& b) {
    double se = 0.0;
    for (size_t i = 0; i < a.p[0].size(); ++i) {
        const double x = double(a.p[0][i]) - double(b.p[0][i]);
        se += x * x;
    }
    const double mse = se / double(a.p[0].size());
    return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// O decodificador tem de dar a reconstrução do codificador: todos os quadros, na ordem de exibição, iguais a menos dos
// empates da IDCT (|d| <= 1 em no máximo 0,5% dos pixels: a IDCT inteira erra até 0,002 e a de referência do teste
// arredonda os valores a essa distância de k + 1/2 do outro lado), e, sem o ruído (p.noise), perto do original.
void check_roundtrip(const EncParams& p, uint32_t seed, double min_psnr, int frames = 7) {
    const Encoded e = encode_sequence(p, seed, frames);
    for (int pass = 0; pass < 2; ++pass) {
        const Decoded d = decode_stream(e, pass == 1);
        CHECK(d.stats.errors == 0);
        CHECK(d.stats.unsupported == 0);
        CHECK(d.stats.concealed == 0);
        if (d.stats.errors != 0) std::printf("    erro: %s\n", d.error.c_str());
        CHECK(d.frames.size() == e.recon.size());
        for (size_t i = 0; i < d.frames.size() && i < e.recon.size(); ++i) {
            CHECK(d.trs[i] == int(i));  // a ordem de exibição
            const Diff df = compare(d.frames[i], e.recon[i]);
            CHECK(df.max <= 1);
            CHECK(df.differing * 200 <= df.pixels);
            if (df.max > 1 || df.differing * 200 > df.pixels) {
                std::printf("    quadro %zu tipo %d: max %d, %ld de %ld pixels diferentes\n", i, d.types[i], df.max,
                            df.differing, df.pixels);
            }
            const double ps = psnr_luma(d.frames[i], e.source[i]);
            CHECK(ps >= min_psnr);
            if (ps < min_psnr) std::printf("    quadro %zu: PSNR %.2f dB\n", i, ps);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- os VLC

TEST(mpeg2_vlc_tables_decode_the_standard_codes) {
    using namespace nf::mpeg2;
    for (const TCode& c : kTMba) {
        BitWriter w;
        w.code(c.bits);
        w.code("1");
        Bits b(w);
        CHECK(read_mb_address_increment(b.br) == c.value);
        CHECK(b.br.pos() == std::strlen(c.bits));
    }
    {
        BitWriter w;
        w.code(kTMbaEscape);
        w.code(kTMbStuffing);
        Bits b(w);
        CHECK(read_mb_address_increment(b.br) == kMbEscape);
        CHECK(read_mb_address_increment(b.br) == kMbStuffing);
    }
    const struct {
        int type;
        const TCode* codes;
        size_t n;
    } types[] = {{1, kTTypeI, 2}, {2, kTTypeP, 7}, {3, kTTypeB, 11}};
    for (const auto& t : types) {
        for (size_t i = 0; i < t.n; ++i) {
            BitWriter w;
            w.code(t.codes[i].bits);
            Bits b(w);
            CHECK(read_mb_type(b.br, t.type) == t.codes[i].value);
        }
    }
    for (const TCode& c : kTCbp) {
        BitWriter w;
        w.code(c.bits);
        Bits b(w);
        CHECK(read_coded_block_pattern(b.br) == c.value);
        CHECK(b.br.pos() == std::strlen(c.bits));
    }
    for (int m = -16; m <= 16; ++m) {
        BitWriter w;
        w.code(kTMotion[std::abs(m)]);
        if (m != 0) w.put(m < 0 ? 1 : 0, 1);
        Bits b(w);
        CHECK(read_motion_code(b.br) == m);
    }
    for (const TCode& c : kTDmv) {
        BitWriter w;
        w.code(c.bits);
        Bits b(w);
        CHECK(read_dmvector(b.br) == c.value);
    }
    for (int s = 0; s < 12; ++s) {
        BitWriter w;
        w.code(kTDcLuma[s]);
        w.code(kTDcChroma[s]);
        Bits b(w);
        CHECK(read_dc_size(b.br, false) == s);
        CHECK(read_dc_size(b.br, true) == s);
    }
    // Os coeficientes, com os dois sinais; o fim de bloco; o '1s' do primeiro de um bloco não intra; o escape.
    for (int table = 0; table < 2; ++table) {
        const TDct* t = table == 0 ? kTDct0 : kTDct1;
        const size_t n = table == 0 ? sizeof(kTDct0) / sizeof(kTDct0[0]) : sizeof(kTDct1) / sizeof(kTDct1[0]);
        for (size_t i = 0; i < n; ++i) {
            for (int sign = 0; sign < 2; ++sign) {
                BitWriter w;
                w.code(t[i].bits);
                w.put(uint32_t(sign), 1);
                w.code(kTEob[table]);
                Bits b(w);
                int run = -1, level = 0;
                CHECK(read_dct_coefficient(b.br, table, false, false, &run, &level) == 1);
                CHECK(run == t[i].run && level == (sign ? -t[i].level : t[i].level));
                CHECK(read_dct_coefficient(b.br, table, false, false, &run, &level) == 0);
            }
        }
    }
    {
        BitWriter w;
        w.code("11");  // primeiro de um bloco não intra: '1s' = (0, -1)
        w.code(kTEscape);
        w.put(17, 6);
        w.put(uint32_t(-1000 & 0xfff), 12);
        w.code(kTEscape);  // MPEG-1: 0x80 e mais um byte = -200
        w.put(3, 6);
        w.put(0x80, 8);
        w.put(56, 8);
        w.code(kTEscape);  // MPEG-1: 0x00 e mais um byte = 200
        w.put(0, 6);
        w.put(0x00, 8);
        w.put(200, 8);
        w.code(kTEscape);  // MPEG-1: 8 bits = -5
        w.put(63, 6);
        w.put(0xfb, 8);
        Bits b(w);
        int run = 0, level = 0;
        CHECK(read_dct_coefficient(b.br, 0, true, false, &run, &level) == 1 && run == 0 && level == -1);
        CHECK(read_dct_coefficient(b.br, 0, false, false, &run, &level) == 1 && run == 17 && level == -1000);
        CHECK(read_dct_coefficient(b.br, 0, false, true, &run, &level) == 1 && run == 3 && level == -200);
        CHECK(read_dct_coefficient(b.br, 0, false, true, &run, &level) == 1 && run == 0 && level == 200);
        CHECK(read_dct_coefficient(b.br, 0, false, true, &run, &level) == 1 && run == 63 && level == -5);
    }
    // Um código que não existe: 16 zeros; e um buraco da B.15 (0000 0001 1101, o (0, 8) da B.14).
    {
        BitWriter w;
        w.put(0, 16);
        w.put(1, 8);
        Bits b(w);
        int run = 0, level = 0;
        CHECK(read_dct_coefficient(b.br, 0, false, false, &run, &level) == kInvalid);
        BitWriter w2;
        w2.code("0000000111010");
        Bits b2(w2);
        CHECK(read_dct_coefficient(b2.br, 1, false, false, &run, &level) == kInvalid);
        CHECK(read_dct_coefficient(b2.br, 0, false, false, &run, &level) == 1 && run == 0 && level == 8);
    }
}

TEST(mpeg2_scans_and_tables_match_the_standard) {
    const std::vector<int> zz = zigzag();
    CHECK(zz.size() == 64);
    bool seen[64] = {};
    for (int i = 0; i < 64; ++i) {
        CHECK(nf::mpeg2::kZigzag[i] == zz[size_t(i)]);
        CHECK(nf::mpeg2::kAlternateScan[i] == kTAlternate[i]);
        seen[kTAlternate[i]] = true;
    }
    for (bool s : seen) CHECK(s);
    CHECK(nf::mpeg2::kDefaultIntraMatrix[0] == 8 && nf::mpeg2::kDefaultIntraMatrix[63] == 83);
    CHECK(nf::mpeg2::kDefaultIntraMatrix[7] == 34 && nf::mpeg2::kDefaultIntraMatrix[56] == 27);
    // A escala: linear = 2 x código; não linear da tabela 7-6.
    CHECK(nf::mpeg2::quantiser_scale(1, false) == 2 && nf::mpeg2::quantiser_scale(31, false) == 62);
    const int nl[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52, 56,
                      64, 72, 80, 88, 96, 104, 112};
    for (int c = 1; c <= 31; ++c) CHECK(nf::mpeg2::quantiser_scale(c, true) == nl[c - 1]);
}

// ---------------------------------------------------------------- a IDCT (IEEE 1180)

namespace {

// O gerador da IEEE 1180 (com o estouro de 32 bits do original).
struct Ieee1180Rand {
    uint32_t x = 1;
    int next(int L, int H) {
        x = x * 1103515245u + 12345u;
        const uint32_t i = x & 0x7ffffffeu;
        double v = double(i) / double(0x7fffffff);
        v *= double(L + H + 1);
        const int j = int(v);
        return j - L;
    }
};

struct IeeeResult {
    int peak = 0;
    double worst_pixel_mse = 0.0, overall_mse = 0.0, worst_pixel_mean = 0.0, overall_mean = 0.0;
};

IeeeResult ieee1180(int L, int H, int sign, int blocks, bool portable) {
    Ieee1180Rand rnd;
    long sum_err[64] = {}, sum_sq[64] = {};
    IeeeResult r;
    for (int b = 0; b < blocks; ++b) {
        double in[64];
        for (int i = 0; i < 64; ++i) in[i] = double(rnd.next(L, H) * sign);
        double f[64];
        ref_fdct(in, f);
        int16_t coef[64];
        for (int i = 0; i < 64; ++i) coef[i] = int16_t(std::clamp(int(std::floor(f[i] + 0.5)), -2048, 2047));
        int ref[64];
        ref_idct(coef, ref);
        int16_t test[64];
        std::memcpy(test, coef, sizeof(test));
        if (portable) nf::mpeg2::idct_portable(test);
        else nf::mpeg2::idct(test);
        for (int i = 0; i < 64; ++i) {
            const int e = test[i] - ref[i];
            r.peak = std::max(r.peak, std::abs(e));
            sum_err[i] += e;
            sum_sq[i] += e * e;
        }
    }
    long total_err = 0, total_sq = 0;
    for (int i = 0; i < 64; ++i) {
        r.worst_pixel_mse = std::max(r.worst_pixel_mse, double(sum_sq[i]) / blocks);
        r.worst_pixel_mean = std::max(r.worst_pixel_mean, std::fabs(double(sum_err[i]) / blocks));
        total_err += sum_err[i];
        total_sq += sum_sq[i];
    }
    r.overall_mse = double(total_sq) / (64.0 * blocks);
    r.overall_mean = std::fabs(double(total_err) / (64.0 * blocks));
    return r;
}

}  // namespace

TEST(mpeg2_idct_meets_ieee_1180) {
    // As faixas da norma (L, H) e as duas pontas do Anexo A (até os coeficientes de 12 bits), com os dois sinais.
    const int ranges[][2] = {{256, 255}, {5, 5}, {300, 300}, {384, 383}};
    for (const auto& lh : ranges) {
        for (int k = 0; k < 4; ++k) {
            const int sign = (k & 1) ? -1 : 1;
            const bool portable = k >= 2;  // as duas versões (a vetorial onde houver SSE2 e a de C++ puro)
            const IeeeResult r = ieee1180(lh[0], lh[1], sign, 10000, portable);
            CHECK(r.peak <= 1);
            CHECK(r.worst_pixel_mse <= 0.06);
            CHECK(r.overall_mse <= 0.02);
            CHECK(r.worst_pixel_mean <= 0.015);
            CHECK(r.overall_mean <= 0.0015);
            if (r.peak > 1 || r.worst_pixel_mse > 0.06 || r.overall_mse > 0.02 || r.worst_pixel_mean > 0.015 ||
                r.overall_mean > 0.0015) {
                std::printf("    L %d H %d sinal %d: pico %d emq pixel %.4f emq %.5f media pixel %.4f media %.5f\n",
                            lh[0], lh[1], sign, r.peak, r.worst_pixel_mse, r.overall_mse, r.worst_pixel_mean,
                            r.overall_mean);
            }
        }
    }
    // Zero entra, zero sai; um só DC dá DC / 8 arredondado em todo o bloco; o maior DC satura.
    int16_t z[64] = {};
    nf::mpeg2::idct(z);
    for (int v : z) CHECK(v == 0);
    for (int dc : {-2048, -100, -4, -3, 4, 12, 1021, 2047}) {
        int16_t b[64] = {};
        b[0] = int16_t(dc);
        nf::mpeg2::idct(b);
        const int expect = std::clamp(int(std::floor(dc / 8.0 + 0.5)), -256, 255);
        for (int v : b) CHECK(v == expect);
    }
    // As duas versões dão o mesmo bit a bit, em blocos esparsos (os atalhos de linha e de coluna) e cheios.
    uint32_t seed = 3;
    bool same = true;
    for (int b = 0; b < 20000; ++b) {
        int16_t x[64] = {}, y[64];
        const int n = 1 + b % 12;
        const int span = (b % 3 == 0) ? 64 : (b % 3 == 1) ? 32 : 8;  // tudo, as 4 primeiras linhas, só a primeira
        for (int i = 0; i < n; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const int pos = int((seed >> 8) % uint32_t(span));
            seed = seed * 1664525u + 1013904223u;
            x[pos] = int16_t(int((seed >> 16) % 4096u) - 2048);
        }
        std::memcpy(y, x, sizeof(y));
        nf::mpeg2::idct(x);
        nf::mpeg2::idct_portable(y);
        same = same && std::memcmp(x, y, sizeof(x)) == 0;
    }
    CHECK(same);
    // Coeficientes no extremo (o pior caso das contas internas) contra a referência.
    for (int pattern = 0; pattern < 4; ++pattern) {
        int16_t b[64], c[64];
        for (int i = 0; i < 64; ++i) b[i] = int16_t(((i * 7 + pattern) % 3 == 0) ? -2048 : 2047);
        std::memcpy(c, b, sizeof(c));
        int ref[64];
        ref_idct(c, ref);
        nf::mpeg2::idct(b);
        for (int i = 0; i < 64; ++i) CHECK(std::abs(b[i] - ref[i]) <= 1);
    }
}

// ---------------------------------------------------------------- a quantização inversa

TEST(mpeg2_inverse_quantisation_by_hand) {
    uint8_t w16[64];
    std::memset(w16, 16, sizeof(w16));
    int16_t qf[64] = {}, f[64];
    // Intra: DC x 8 (precisão 0) e x 2 (precisão 2); AC = 2 QF W qs / 32 truncado para zero.
    qf[0] = 100;
    qf[1] = 3;
    qf[2] = -3;
    nf::mpeg2::inverse_quantise(qf, w16, 5, true, 0, false, f);
    CHECK(f[0] == 800);
    CHECK(f[1] == (2 * 3 * 16 * 5) / 32);   // 15
    CHECK(f[2] == -15);
    // Soma 800 + 15 - 15 = 800, par: o último coeficiente vira 1 (controle de descasamento).
    CHECK(f[63] == 1);
    nf::mpeg2::inverse_quantise(qf, w16, 5, true, 2, false, f);
    CHECK(f[0] == 200);
    // Não intra: (2 QF + sinal) W qs / 32; -1 com W 16 e qs 3: -3 x 48 / 32 = -4.5 -> -4.
    int16_t q2[64] = {};
    q2[0] = -1;
    q2[10] = 7;
    nf::mpeg2::inverse_quantise(q2, w16, 3, false, 0, false, f);
    CHECK(f[0] == -4);
    CHECK(f[10] == (15 * 16 * 3) / 32);  // 22
    // Soma -4 + 22 = 18, par: F[7][7] = 0 vira 1.
    CHECK(f[63] == 1);
    // Soma ímpar: nada muda.
    q2[10] = 8;  // (17 x 48) / 32 = 25; -4 + 25 = 21
    nf::mpeg2::inverse_quantise(q2, w16, 3, false, 0, false, f);
    CHECK(f[10] == 25 && f[63] == 0);
    // F[7][7] ímpar com soma par: perde 1.
    int16_t q3[64] = {};
    q3[63] = 1;
    q3[1] = 1;  // não intra, W 16, qs 2: 3 x 32 / 32 = 3 em cada: soma 6
    nf::mpeg2::inverse_quantise(q3, w16, 2, false, 0, false, f);
    CHECK(f[1] == 3 && f[63] == 2);
    // A saturação em [-2048, 2047], antes do controle de descasamento.
    int16_t q4[64] = {};
    q4[5] = 2047;
    q4[6] = -2047;
    uint8_t w255[64];
    std::memset(w255, 255, sizeof(w255));
    nf::mpeg2::inverse_quantise(q4, w255, 112, false, 0, false, f);
    CHECK(f[5] == 2047 && f[6] == -2048);
    CHECK(f[63] == 0);  // soma -1: ímpar
    // A escala não linear: o código 31 é 112.
    CHECK(nf::mpeg2::quantiser_scale(31, true) == 112);
    // MPEG-1: o valor par vai um para perto do zero (ajuste para ímpar), sem o controle de descasamento.
    int16_t q5[64] = {};
    q5[1] = 2;   // intra: 2 x 2 x 16 x 4 / 32 = 8 -> 7
    q5[2] = -2;  // -8 -> -7
    q5[3] = 1;   // 4 -> 3
    nf::mpeg2::inverse_quantise(q5, w16, 4, true, 0, true, f);
    CHECK(f[1] == 7 && f[2] == -7 && f[3] == 3 && f[63] == 0);
}

// ---------------------------------------------------------------- a previsão

TEST(mpeg2_half_pel_prediction_follows_the_formula) {
    uint8_t ref[24 * 20];
    uint32_t r = 7;
    for (uint8_t& v : ref) {
        r = r * 1664525u + 1013904223u;
        v = uint8_t(r >> 24);
    }
    for (int hy = 0; hy < 2; ++hy) {
        for (int hx = 0; hx < 2; ++hx) {
            for (int avg = 0; avg < 2; ++avg) {
                for (int w : {16, 8}) {
                    // As duas versões (a vetorial onde houver SSE2, e a de C++ puro das outras CPUs).
                    for (int portable = 0; portable < 2; ++portable) {
                        uint8_t dst[16 * 16];
                        for (int i = 0; i < 256; ++i) dst[i] = uint8_t(i * 3);
                        uint8_t before[16 * 16];
                        std::memcpy(before, dst, sizeof(dst));
                        const int x = 3, y = 2, h = w == 16 ? 16 : 4;
                        if (portable) nf::mpeg2::predict_portable(ref, 24, x, y, hx, hy, w, h, dst, 16, avg != 0);
                        else nf::mpeg2::predict(ref, 24, x, y, hx, hy, w, h, dst, 16, avg != 0);
                        for (int yy = 0; yy < 16; ++yy) {
                            for (int xx = 0; xx < 16; ++xx) {
                                int e = before[yy * 16 + xx];  // fora do bloco nada muda
                                if (yy < h && xx < w) {
                                    const int p = t_pred(ref, 24, 24, 20, 2 * (x + xx) + hx, 2 * (y + yy) + hy);
                                    e = avg ? (before[yy * 16 + xx] + p + 1) / 2 : p;
                                }
                                CHECK(dst[yy * 16 + xx] == e);
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- fluxos feitos pelo codificador de teste

// Com o ruído (o padrão de EncParams) os fluxos exercitam a sintaxe e a imagem não importa (min_psnr 0); os testes
// "comportados" medem a imagem contra a original.

TEST(mpeg2_roundtrip_intra_and_motion_like_the_movies) {
    // O que os filmes usam: intra_vlc_format 1, q_scale_type 1, matriz não intra própria, zigue-zague, previsão de
    // quadro.
    EncParams p;
    p.intra_vlc_format = true;
    p.q_scale_type = true;
    p.custom_matrices = true;
    check_roundtrip(p, 1, 0.0);
}

TEST(mpeg2_roundtrip_table_zero_linear_scale_alternate_scan) {
    EncParams p;
    p.alternate_scan = true;
    p.f_code = 1;
    p.width = 80;
    p.height = 64;
    check_roundtrip(p, 2, 0.0);
    p.f_code = 3;
    p.intra_dc_precision = 2;  // DC de 10 bits
    check_roundtrip(p, 3, 0.0);
}

TEST(mpeg2_roundtrip_field_prediction_and_field_dct) {
    EncParams p;
    p.frame_pred_frame_dct = false;
    p.intra_vlc_format = true;
    p.width = 64;
    p.height = 64;
    const Encoded e = encode_sequence(p, 4);
    CHECK(e.field_mbs > 10);
    check_roundtrip(p, 4, 0.0);
}

TEST(mpeg2_roundtrip_concealment_vectors_and_long_skips) {
    EncParams p;
    p.concealment = true;
    p.f_code = 2;
    check_roundtrip(p, 5, 0.0);
    // Uma imagem larga (50 macroblocos) quase toda pulada: incrementos acima de 33 (macroblock_escape).
    EncParams w;
    w.width = 800;
    w.height = 32;
    w.skip_prob = 0.97;
    w.split_slices = false;
    const Encoded e = encode_sequence(w, 6, 4);
    CHECK(e.escapes > 0 && e.skipped > 100);
    check_roundtrip(w, 6, 0.0, 4);
}

TEST(mpeg2_roundtrip_mpeg1) {
    EncParams p;
    p.mpeg1 = true;
    p.stuffing = true;
    p.f_code = 2;
    check_roundtrip(p, 7, 0.0);
    p.full_pel = true;
    check_roundtrip(p, 8, 0.0);
    const Encoded e = encode_sequence(p, 8);
    nf::Mpeg2VideoDecoder dec;
    dec.decode(e.stream.data(), e.stream.size());
    CHECK(dec.mpeg1());
    CHECK(dec.width() == 64 && dec.height() == 48);
    CHECK(std::fabs(dec.frame_rate() - 30.0) < 1e-9);
}

TEST(mpeg2_roundtrip_known_image_within_quantisation_error) {
    // Um codificador comportado (sem ruído, sem pulados, sempre com o resíduo) com o quantizador fino: o decodificado
    // fica perto do original (o erro é o da quantização), em I, P e B, nos dois jeitos de quantizar e no MPEG-1.
    EncParams p;
    p.noise = false;
    p.skip_prob = 0.0;
    p.max_qcode = 3;
    p.intra_vlc_format = true;
    p.q_scale_type = true;
    check_roundtrip(p, 21, 38.0);
    p.q_scale_type = false;
    p.alternate_scan = true;
    p.frame_pred_frame_dct = false;
    check_roundtrip(p, 22, 34.0);
    EncParams m;
    m.mpeg1 = true;
    m.noise = false;
    m.skip_prob = 0.0;
    m.max_qcode = 3;
    check_roundtrip(m, 23, 34.0);
    // Mais grosso, menos qualidade (a medida acompanha o quantizador).
    p.max_qcode = 12;
    check_roundtrip(p, 24, 26.0);
}

TEST(mpeg2_digest_is_the_same_on_every_platform) {
    // O codificador de teste só faz contas exatas ou do IEEE sem libm, e o decodificador só contas exatas: os fluxos e os
    // quadros decodificados têm de ser os mesmos em qualquer CPU e compilador (o SSE2 do x86 e o C++ puro do ARM da CI).
    // Os resumos de referência saíram do nf_tests do Windows (x86 e x64); para trocá-los, rode "nf_tests mpeg2_digest".
    std::vector<uint8_t> streams, frames;
    for (int k = 0; k < 4; ++k) {
        EncParams p;
        p.intra_vlc_format = k != 1;
        p.q_scale_type = k == 0;
        p.custom_matrices = k == 0;
        p.frame_pred_frame_dct = k != 1;
        p.alternate_scan = k == 1;
        p.intra_dc_precision = k == 1 ? 2 : 0;
        p.concealment = k == 2;
        p.mpeg1 = k == 3;
        p.full_pel = k == 3;
        p.width = k == 1 ? 64 : 80;
        p.height = 64;
        const Encoded e = encode_sequence(p, 100 + uint32_t(k), 10);
        streams.insert(streams.end(), e.stream.begin(), e.stream.end());
        const Decoded d = decode_stream(e, true);
        CHECK(d.frames.size() == 10);
        for (const Frame& f : d.frames) {
            for (int c = 0; c < 3; ++c) frames.insert(frames.end(), f.p[c].begin(), f.p[c].end());
        }
    }
    const uint64_t s = nf::xxh3_64(streams.data(), streams.size());
    const uint64_t f = nf::xxh3_64(frames.data(), frames.size());
    std::printf("    resumos: fluxos %016llx quadros %016llx\n", static_cast<unsigned long long>(s),
                static_cast<unsigned long long>(f));
    CHECK(s == 0xd7a4351a0e2d695full);
    CHECK(f == 0x919ce3711417c6caull);
}

TEST(mpeg2_output_order_and_reset) {
    EncParams p;
    p.intra_vlc_format = true;
    const Encoded e = encode_sequence(p, 9, 10);
    // Imagem por imagem: cada I ou P sai quando o próximo chega; os B logo; a última no fim.
    std::vector<int> trs;
    nf::Mpeg2VideoDecoder dec([&](const nf::Mpeg2Picture& pic) { trs.push_back(pic.temporal_reference); });
    dec.decode(e.stream.data() + e.unit_starts[0], e.unit_starts[1] - e.unit_starts[0]);  // I0
    CHECK(trs.empty());
    dec.decode(e.stream.data() + e.unit_starts[1], e.unit_starts[2] - e.unit_starts[1]);  // P3: sai o I0
    CHECK(trs.size() == 1 && trs[0] == 0);
    dec.decode(e.stream.data() + e.unit_starts[2], e.unit_starts[3] - e.unit_starts[2]);  // B1 sai logo
    CHECK(trs.size() == 2 && trs[1] == 1);
    // reset: nada sai, e o mesmo fluxo de novo do começo dá tudo.
    dec.reset();
    trs.clear();
    dec.decode(e.stream.data(), e.stream.size());
    dec.finish();
    CHECK(trs.size() == 10);
    for (size_t i = 0; i < trs.size(); ++i) CHECK(trs[i] == int(i));
    // Um B sem as referências (um GOP aberto pego no meio) não sai.
    nf::Mpeg2VideoDecoder dec2([&](const nf::Mpeg2Picture&) {});
    dec2.decode(e.stream.data(), e.unit_starts[1]);  // só o I: sem par de referências para o B1
    dec2.decode(e.stream.data() + e.unit_starts[2], e.unit_starts[3] - e.unit_starts[2]);
    CHECK(dec2.stats().dropped == 1);
}

TEST(mpeg2_damaged_stream_conceals_and_survives) {
    EncParams p;
    p.intra_vlc_format = true;
    const Encoded e = encode_sequence(p, 10, 7);
    // Os dados das fatias (depois dos 2 primeiros bytes de cada uma, até o próximo código de início).
    std::vector<size_t> slice_bytes;
    {
        const std::vector<uint8_t>& s = e.stream;
        size_t i = 0;
        while (i + 4 <= s.size()) {
            if (s[i] == 0 && s[i + 1] == 0 && s[i + 2] == 1 && s[i + 3] >= 1 && s[i + 3] <= 0xaf) {
                size_t j = i + 6;
                while (j + 3 <= s.size() && !(s[j] == 0 && s[j + 1] == 0 && s[j + 2] == 1)) {
                    if (s[j] > 1 && s[j - 1] != 0) slice_bytes.push_back(j);
                    ++j;
                }
                i = j;
            } else {
                ++i;
            }
        }
    }
    CHECK(slice_bytes.size() > 100);
    uint32_t r = 99;
    long errors = 0;
    for (int trial = 0; trial < 60; ++trial) {
        std::vector<uint8_t> s = e.stream;
        // Estraga bytes no meio das fatias (nunca um 0 ou 1: nada de código de início novo).
        for (int k = 0; k < 6; ++k) {
            r = r * 1664525u + 1013904223u;
            const size_t at = slice_bytes[size_t(r >> 8) % slice_bytes.size()];
            s[at] = uint8_t(s[at] ^ (1u << ((r >> 4) % 8)));
            if (s[at] <= 1) s[at] = 0x55;
        }
        int out = 0;
        nf::Mpeg2VideoDecoder dec([&](const nf::Mpeg2Picture& pic) {
            ++out;
            CHECK(pic.width == 64 && pic.height == 48);
        });
        dec.decode(s.data(), s.size());
        dec.finish();
        errors += long(dec.stats().errors);
        CHECK(out == 7);
    }
    CHECK(errors > 0);
    // Lixo puro e um fluxo cortado não derrubam.
    std::vector<uint8_t> junk(5000);
    for (uint8_t& b : junk) {
        r = r * 1664525u + 1013904223u;
        b = uint8_t(r >> 24);
    }
    nf::Mpeg2VideoDecoder dec;
    dec.decode(junk.data(), junk.size());
    dec.decode(e.stream.data(), e.stream.size() / 2);
    dec.finish();
    dec.decode(nullptr, 0);
    CHECK(true);
}

// ---------------------------------------------------------------- a fachada

TEST(mpeg2_facade_own_decoder_gives_nv12_in_display_order) {
    EncParams p;
    p.intra_vlc_format = true;
    p.q_scale_type = true;
    const Encoded e = encode_sequence(p, 11, 7);
    nf::Mpeg2Decoder d;
    CHECK(d.open(64, 48, 30.0, nf::MovieDecoderChoice::Own));
    CHECK(d.ok() && d.own());
    CHECK(d.name() == nf::kOwnMpeg2DecoderName);
    std::vector<nf::MovieFrameNV12> out;
    nf::MovieFrameNV12 f;
    for (size_t i = 0; i < e.unit_starts.size(); ++i) {
        const size_t a = e.unit_starts[i];
        const size_t b = i + 1 < e.unit_starts.size() ? e.unit_starts[i + 1] : e.stream.size();
        CHECK(d.feed(e.stream.data() + a, b - a));
        while (d.take(f)) out.push_back(f);
    }
    d.drain();
    while (d.take(f)) out.push_back(f);
    CHECK(out.size() == 7);
    const Decoded planar = decode_stream(e, false);
    for (size_t i = 0; i < out.size() && i < planar.frames.size(); ++i) {
        const nf::MovieFrameNV12& n = out[i];
        CHECK(n.width == 64 && n.height == 48 && n.data.size() == size_t(64 * 48 * 3 / 2));
        CHECK(std::memcmp(n.data.data(), planar.frames[i].p[0].data(), 64 * 48) == 0);
        bool uv_ok = true;
        for (int k = 0; k < 32 * 24; ++k) {
            uv_ok = uv_ok && n.data[size_t(64 * 48 + 2 * k)] == planar.frames[i].p[1][size_t(k)] &&
                    n.data[size_t(64 * 48 + 2 * k + 1)] == planar.frames[i].p[2][size_t(k)];
        }
        CHECK(uv_ok);
    }
    // flush: o que estava dentro some; o filme de novo dá os mesmos quadros.
    d.flush();
    CHECK(d.frames_ready() == 0);
    d.feed(e.stream.data(), e.stream.size());
    d.drain();
    int again = 0;
    while (d.take(f)) ++again;
    CHECK(again == 7);
}

TEST(movie_nv12_to_bgra_matches_the_per_pixel_formula) {
    // A conversão (vetorial onde houver SSE2) contra yuv_to_bgr pixel a pixel, com uma largura que não é múltipla de 16
    // (o resto vai pelo C++ puro) e todos os extremos de Y e de croma.
    const int w = 50, h = 6;
    std::vector<uint8_t> nv12(size_t(w * h * 3 / 2));
    uint32_t r = 5;
    for (size_t i = 0; i < nv12.size(); ++i) {
        r = r * 1664525u + 1013904223u;
        nv12[i] = uint8_t(r >> 24);
    }
    nv12[0] = 0;
    nv12[1] = 255;
    nv12[size_t(w * h)] = 0;
    nv12[size_t(w * h + 1)] = 255;
    nv12[size_t(w * h + 2)] = 255;
    nv12[size_t(w * h + 3)] = 0;
    std::vector<uint8_t> bgra(size_t(w * h * 4));
    nf::nv12_to_bgra(nv12.data(), w, h, bgra.data(), 0x80);
    bool ok = true;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t* uv = nv12.data() + w * h + (y / 2) * w + (x & ~1);
            uint8_t e[3];
            nf::yuv_to_bgr(nv12[size_t(y * w + x)], uv[0], uv[1], e);
            const uint8_t* o = bgra.data() + (y * w + x) * 4;
            ok = ok && o[0] == e[0] && o[1] == e[1] && o[2] == e[2] && o[3] == 0x80;
        }
    }
    CHECK(ok);
}

TEST(mpeg2_decoder_choice_parses_the_ini_values) {
    nf::MovieDecoderChoice c = nf::MovieDecoderChoice::Own;
    CHECK(nf::parse_movie_decoder_choice("auto", &c) && c == nf::MovieDecoderChoice::Auto);
    CHECK(nf::parse_movie_decoder_choice("MF", &c) && c == nf::MovieDecoderChoice::System);
    CHECK(nf::parse_movie_decoder_choice("system", &c) && c == nf::MovieDecoderChoice::System);
    CHECK(nf::parse_movie_decoder_choice(" own ", &c) && c == nf::MovieDecoderChoice::Own);
    CHECK(nf::parse_movie_decoder_choice("port", &c) && c == nf::MovieDecoderChoice::Own);
    CHECK(!nf::parse_movie_decoder_choice("ffmpeg", &c) && c == nf::MovieDecoderChoice::Own);
    CHECK(std::string(nf::movie_decoder_choice_name(nf::MovieDecoderChoice::Auto)) == "auto");
    CHECK(std::string(nf::movie_decoder_choice_name(nf::MovieDecoderChoice::System)) == "mf");
    CHECK(std::string(nf::movie_decoder_choice_name(nf::MovieDecoderChoice::Own)) == "own");
    // own sempre abre; auto sempre abre (o do sistema ou o do port).
    nf::Mpeg2Decoder d;
    CHECK(d.open(512, 448, 30.0, nf::MovieDecoderChoice::Own) && d.own());
    CHECK(d.open(512, 448, 30.0, nf::MovieDecoderChoice::Auto) && d.ok());
    if (d.own()) CHECK(!d.failure().empty());  // auto caiu no do port: o motivo do sistema fica
}
