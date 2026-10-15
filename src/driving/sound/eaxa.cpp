// O decodificador de EA-XA do SND (driving/sound/eaxa.h), portado do MIPS do DRIVING.ELF (as desmontagens em
// out/agent_drv_sound/ do checkout principal). As contas em float seguem a FPU do EE (platform/ps2_float.h).

#include "driving/sound/eaxa.h"

#include "platform/ps2_float.h"

namespace drv {

namespace {

// As tabelas do .data (0x00360bb8 e 0x00360bc8): os coeficientes de s1 e de s2 por filtro.
constexpr float kK0[4] = {0.0f, 0.9375f, 1.796875f, 1.53125f};
constexpr float kK1[4] = {0.0f, 0.0f, -0.8125f, -0.859375f};

uint64_t g_guards = 0;

// ADDA.S n, 0; MADDA.S a, k0; MADD.S b, k1: ((n + 0) + a k0) + b k1, cada passo truncado.
inline uint32_t step(uint32_t n, uint32_t a, uint32_t k0, uint32_t b, uint32_t k1) {
    uint32_t acc = ps2::add(n, 0);
    acc = ps2::madd(acc, a, k0);
    return ps2::madd(acc, b, k1);
}

}  // namespace

uint64_t eaxa_guards() { return g_guards; }
void eaxa_reset_guards() { g_guards = 0; }

// [driving] 0x00318900 SND::decodexac(SND::MXAPACKETF *)
void SND_decodexac(MxaPacketF& p) {
    // f10 = a última amostra, f11 = a de antes; cada amostra nova entra no lugar da mais velha e os papéis trocam.
    uint32_t f10 = ps2::fbits(p.s1), f11 = ps2::fbits(p.s2);
    int32_t count = p.count;
    const uint8_t* in = p.in;
    float* out = p.out;
    while (count > 0) {
        count -= 28;
        const uint8_t head = in[0];
        const uint32_t filter = head >> 4;
        const int shift = 12 - (head & 0xf);   // sllv: só os 5 bits de baixo contam
        const int rshift = (head & 0xf) + 16;  // srav
        uint32_t k0 = 0, k1 = 0;
        if (filter < 4) {
            k0 = ps2::fbits(kK0[filter]);
            k1 = ps2::fbits(kK1[filter]);
        } else {
            ++g_guards;
        }
        for (int i = 0; i < 14; ++i) {
            const int32_t b = int8_t(in[1 + i]);
            const int32_t hi = int32_t(uint32_t(b >> 4) << (uint32_t(shift) & 31));
            const int32_t lo = int32_t(uint32_t(b) << 28) >> (uint32_t(rshift) & 31);
            f11 = step(ps2::from_int(hi), f10, k0, f11, k1);
            out[2 * i] = ps2::ffloat(f11);
            f10 = step(ps2::from_int(lo), f11, k0, f10, k1);
            out[2 * i + 1] = ps2::ffloat(f10);
        }
        in += 15;
        out += 28;
    }
    p.count = count;
    p.s1 = ps2::ffloat(f10);
    p.s2 = ps2::ffloat(f11);
    p.out = out;
    // O original não grava a entrada de volta (o Decode a anda à mão: +0xa0 += 15 por bloco).
}

// [driving] 0x00318d90 SND::CEAXABLKDecf::CEAXABLKDecf(void)
void CEAXABLKDecf_Construct(CEAXABLKDecf& d) {
    d.bytes = 0;
    d.frames = 0;
    d.left = 0;
    d.pkt.s1 = 0.0f;
    d.pkt.s2 = 0.0f;
}

// [driving] 0x00318db0 SND::CEAXABLKDecf::Feed(void *, int, int)
int32_t CEAXABLKDecf_Feed(CEAXABLKDecf& d, const uint8_t* data, int32_t bytes, int32_t frames) {
    if (data != nullptr && d.frames == 0) {
        d.pkt.in = data;
        d.frames = frames;
        d.bytes = bytes;
        return 0;
    }
    return -1;
}

// [driving] 0x00318dd8 SND::CEAXABLKDecf::Decode(float **, int)
int32_t CEAXABLKDecf_Decode(CEAXABLKDecf& d, float* const* out, int32_t n) {
    int32_t done = 0;
    const int32_t frames = d.frames;
    d.pkt.out = *out;
    if (frames == 0) return 0;
    if (frames <= n) n = frames;  // movz: n = min(n, frames)
    if (d.left != 0) {
        const int32_t take = n < d.left ? n : d.left;
        for (int32_t i = 0; i < take; ++i) *d.pkt.out++ = *d.next++;
        n -= take;
        d.frames = frames - take;
        d.left -= take;
        done = take;
    }
    const int32_t whole = (n / 28) * 28;
    const int32_t rest = n - whole;
    d.pkt.count = whole;
    if (whole > 0) {
        done += whole;
        SND_decodexac(d.pkt);
        d.frames -= whole;
        d.pkt.in += (whole / 28) * 15;
    }
    if (rest > 0) {
        float* const dest = d.pkt.out;
        d.pkt.count = rest;
        d.pkt.out = d.buffer;
        SND_decodexac(d.pkt);  // um bloco inteiro no buffer; count fica rest - 28
        float* const end = d.pkt.out;
        d.pkt.in += 15;
        d.next = end + d.pkt.count;  // o buffer + rest
        d.left = -d.pkt.count;       // 28 - rest
        d.frames -= rest;
        d.pkt.out = end - 28;        // o começo do buffer
        for (int32_t i = 0; i < rest; ++i) dest[i] = d.pkt.out[i];
        done += rest;
    }
    if (d.frames <= 0) d.left = 0;
    return done;
}

// [driving] 0x00319018 SND::CEAXABLKDecf::SetState(SND::XAFSTATE *)
void CEAXABLKDecf_SetState(CEAXABLKDecf& d, float s1, float s2) {
    d.pkt.s1 = s1;
    d.pkt.s2 = s2;
}

}  // namespace drv
