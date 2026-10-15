// O mixador do SND no EE (driving/sound/snd_mix.h): MIX_*, MIXI_*, SNDMIX*, mixc e os filtros dos fluxos, portados do
// MIPS do DRIVING.ELF. Divergências e guardas no cabeçalho.

#include "driving/sound/snd_mix.h"

#include <cstring>
#include <new>

#include "driving/sound/eaxa.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snd_stream.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

// O estado global do mixador (o .bss de 0x0035e400 a 0x0035e660).
struct MixGlobals {
    SndMixCreate create;            // 0x0035e400 (+0x08 o voice_free)
    uint16_t slice_bytes = 0;       // 0x0035e40c o passo das saídas por fatia (0x400)
    uint8_t quality = 0;            // 0x0035e40e
    uint8_t reverb[1] = {};         // 0x0035e40f o estado do reverb por barramento (0: nenhum)
    uint32_t reverb_fn = 0;         // 0x0035e414 (nenhum: o reverb do EE não está portado)
    uint8_t* wet_ext = nullptr;     // 0x0035e418 (o MIX_setwetbuffer)
    uint32_t unpack_init[0x21] = {};  // 0x0035e41c a função de iniciação de cada formato
    uint32_t unpack_size[0x21] = {};  // 0x0035e4a0 o tamanho do objeto
    uint8_t* buf_raw[2] = {};       // 0x0035e524
    float* buf[2] = {};             // 0x0035e52c os buffers A e B (alinhados a 64)
    uint8_t* out_raw[6] = {};       // 0x0035e534
    float* outbuf[6] = {};          // 0x0035e54c as somas float de cada saída
    float pending[6] = {};          // 0x0035e564 o estalo de fim por saída
    float pending_wet[1] = {};      // 0x0035e57c
    nf::P32<SFilter> out_head[6] = {};  // 0x0035e580
    nf::P32<SFilter> out_filter[6] = {};  // 0x0035e598 (nenhum: o SNDMIX_setmasterlowpass é vazio)
    SFilter ft16[6] = {};           // 0x0035e5b0 (0x1c cada)
    MixVoice* voices = nullptr;     // 0x0035e658
    uint32_t mixfn = 0;             // 0x0035e65c (o mixc)
    int32_t dts_left = 0;           // 0x0035e660
    float wet[0x601] = {};          // 0x0035ec00 o buffer molhado do barramento 0 (+ o 0x00360400)
    uint64_t guards = 0;
};
MixGlobals g;

uint8_t n_out() { return g.create.outputs; }
uint8_t n_voices() { return g.create.voices; }
MixVoice& voice(int32_t mv) { return g.voices[mv]; }

void guard() { ++g.guards; }

// [driving] 0x00317b78 mixc(int, float *, float *, float): out[i] += in[i] x ganho, 16 de cada vez (vmulx/vadd).
void mixc(float gain, int32_t count, const float* in, float* out) {
    if (count <= 0 || (count & 15) != 0) {
        // O original só para quando o ponteiro bate no fim exato (a divergência do cabeçalho).
        guard();
        count = (count + 15) & ~15;
        if (count <= 0) count = 16;
    }
    const uint32_t g32 = ps2::fbits(gain);
    for (int32_t i = 0; i < count; ++i)
        out[i] = ps2::ffloat(ps2::add(ps2::fbits(out[i]), ps2::mul(ps2::fbits(in[i]), g32)));
}

void mix(uint32_t fn, float gain, int32_t count, const float* in, float* out) {
    (void)fn;  // só o mixc (MIX_reset)
    mixc(gain, count, in, out);
}

// [driving] 0x003123f0 MIXI_interpolateto0(float *, float *): o valor que sobrou levado a zero em 16 amostras.
void MIXI_interpolateto0(float* v, float* out) {
    static const uint32_t k[16] = {0x3f70f0f1, 0x3f61e1e2, 0x3f52d2d3, 0x3f43c3c4, 0x3f34b4b5, 0x3f25a5a6,
                                   0x3f169697, 0x3f078788, 0x3ef0f0f1, 0x3ed2d2d3, 0x3eb4b4b5, 0x3e969697,
                                   0x3e70f0f1, 0x3e34b4b5, 0x3df0f0f1, 0x3d70f0f1};
    for (int i = 0; i < 16; ++i)
        out[i] = ps2::ffloat(ps2::add(ps2::fbits(out[i]), ps2::mul(ps2::fbits(*v), k[i])));
    *v = 0.0f;
}

// [driving] 0x00312618 MIXI_interpolatemix(float, float, float *, float *): o ganho de from a to em 16 passos de 1/17.
void MIXI_interpolatemix(float from, float to, const float* in, float* out) {
    const uint32_t f = ps2::fbits(from);
    const uint32_t d = ps2::mul(ps2::sub(ps2::fbits(to), f), 0x3d70f0f1u);
    uint32_t gk[16];
    gk[0] = ps2::add(f, d);
    gk[1] = ps2::add(f, ps2::add(d, d));
    for (int k = 3; k <= 16; ++k) gk[k - 1] = ps2::add(f, ps2::mul(d, ps2::from_int(k)));
    for (int i = 0; i < 16; ++i)
        out[i] = ps2::ffloat(ps2::add(ps2::fbits(out[i]), ps2::mul(ps2::fbits(in[i]), gk[i])));
}

int32_t process(SFilter* f, int32_t frames, float* a2, float* a3, int32_t t0);

// [driving] 0x00315fe8 SFILTER_unpackxapf: decodifica o canal do pacote em a3, pedindo o pacote seguinte ao
// SNDPKTPLAY quando acaba; sem pacote, completa com zeros se já tinha decodificado algo deste pacote.
int32_t SFILTER_unpackxapf(SFilterXapf* f, int32_t frames, float* out) {
    int32_t done = 0;
    if (f->pending != 0) {
        SNDPKTPLAYI_freeframes(f->player, f->chan, f->pending);
        f->pending = 0;
    }
    if (frames <= 0) return 0;
    float* cur = out;
    while (true) {
        float* outs[1] = {cur};
        const int32_t got = CEAXABLKDecf_Decode(*nf::raw(f->dec), outs, frames);
        cur += got;
        done += got;
        f->decoded += got;
        f->pending += got;
        if (got < frames) {
            frames -= got;
            int32_t cont = 0;
            const int16_t* p = SNDPKTPLAYI_get(f->player, f->chan, &f->frames, &cont);
            f->pkt = p;
            if (p == nullptr) {
                if (frames > 0) {
                    if (f->pending != 0) {
                        std::memset(cur, 0, size_t(frames) * 4);
                        done += frames;
                    }
                }
                frames = 0;
            } else {
                CEAXABLKDecf_SetState(*nf::raw(f->dec), ee::cvt_s(p[0]), ee::cvt_s(p[1]));
                f->pkt = p + 2;
                f->decoded = 0;
                CEAXABLKDecf_Feed(*nf::raw(f->dec), reinterpret_cast<const uint8_t*>(nf::raw(f->pkt)), f->frames << 2,
                                  f->frames);
            }
        } else {
            frames -= got;
        }
        if (frames < 1) break;
    }
    return done;
}

// [driving] 0x00316158 SFILTER_unpackxapfrestore
void SFILTER_unpackxapfrestore(SFilterXapf* f) {
    if (f->dec != nullptr) {
        nf::raw(f->dec)->~CEAXABLKDecf();
        SNDMEMI_free(nf::raw(f->dec));  // o operator delete pelo CODA (o SNDI_Delete)
    }
}

// [driving] 0x00317510 SFILTER_rsf: o reamostrador linear (MMI e VU0); com o pitch 0x10000, cópia com 4 amostras lidas
// à frente.
int32_t SFILTER_rsf(SFilterRsf* f, int32_t frames, float* a2, float* a3) {
    SFilter* up = nf::raw(f->h.next);
    if (f->pitch == 0x10000) {
        float* out = a3;
        int32_t need;
        const int16_t primed = f->primed;
        if (primed == 0) {
            need = frames + 4 - f->delay;
        } else {
            need = frames;
            const int16_t d = f->delay;
            for (int32_t i = 0; i < 4 - d; ++i) *out++ = f->hist[d + i];
        }
        int32_t r = frames;
        if (up != nullptr) r = process(up, need, a2, out, f->h.chan);
        if (r < 1) {
            if (f->primed == 0) f->primed = 1;
            f->hist[3] = 0.0f;
            f->hist[0] = 0.0f;
            f->hist[1] = 0.0f;
            f->hist[2] = 0.0f;
            return r;
        }
        const float* h;
        if (f->primed == 0) {
            f->primed = 1;
            h = out + (frames - f->delay);
        } else {
            h = out + frames - 4;
        }
        std::memcpy(f->hist, h, 16);
        return frames;
    }
    // O caminho interpolado (com o erro do original: ver o cabeçalho).
    const uint32_t frac0 = f->frac;
    uint32_t n = (uint32_t(f->pitch) * uint32_t(frames) + frac0) >> 16;
    float* in = a2;
    int32_t need = int32_t(n);
    if (f->primed == 0) {
        need = int32_t(n + 4 - uint32_t(int32_t(f->delay)));
        for (int32_t k = f->delay; k > 0; --k) *in++ = 0.0f;
    } else {
        std::memcpy(in, f->hist, 16);
        in += 4;
    }
    uint32_t frac = frac0;
    if (need > 0 && up != nullptr) {
        const int32_t r = process(up, need, a3, in, f->h.chan);
        if (r < 1) {
            if (f->primed == 0) f->primed = 1;
            f->hist[3] = 0.0f;
            f->hist[0] = 0.0f;
            f->hist[1] = 0.0f;
            f->hist[2] = 0.0f;
            return r;
        }
        frac = f->frac;
    }
    int16_t delay;
    if (f->primed != 0) {
        delay = f->delay;
        in -= (4 - f->delay);
    } else {
        delay = f->delay;
        f->primed = 1;
    }
    if (frames <= 0 || (frames & 15) != 0) guard();  // o laço só para no fim exato (o cabeçalho)
    const int32_t p = f->pitch;
    uint32_t pos = f->frac;
    const int32_t blocks = frames > 0 ? (frames + 15) / 16 : 1;
    float* out = a3;
    for (int32_t b = 0; b < blocks; ++b) {
        for (int32_t k = 0; k < 16; ++k) {
            const uint32_t q = pos + uint32_t(p) * uint32_t(k);
            const uint32_t fr = ps2::mul(ps2::from_int(int32_t(q & 0xffff)), 0x37800000u);
            const float* s = in + (q >> 16);
            const uint32_t s0 = ps2::fbits(s[0]), s1 = ps2::fbits(s[1]);
            out[k] = ps2::ffloat(ps2::add(ps2::mul(ps2::sub(s1, s0), fr), s0));
        }
        pos += uint32_t(p) * 16u;
        out += 16;
    }
    f->frac = frac & 0xffff;
    const float* h = in + (int32_t(frac >> 16) - delay);
    std::memcpy(f->hist, h, 16);
    return frames;
}

// [driving] 0x003166e0 SFILTER_lpfRC: y = y x a + b x x.
int32_t SFILTER_lpfRC(SFilterLpfRc* f, int32_t frames, float* a2, float* a3) {
    SFilter* up = nf::raw(f->h.next);
    if (up != nullptr) {
        const int32_t r = process(up, frames, a3, a2, f->h.chan);
        if (r <= 0) return r;
    }
    for (int32_t i = 0; i < frames; ++i) {
        const uint32_t y = ps2::add(ps2::mul(ps2::fbits(f->y), ps2::fbits(f->a)), ps2::mul(ps2::fbits(f->b),
                                                                                             ps2::fbits(a2[i])));
        f->y = ps2::ffloat(y);
        a3[i] = f->y;
    }
    return frames;
}

// [driving] 0x003165d0 SFILTER_ft16: satura em [-32768, 32767] (vmax/vmini), trunca (vftoi0) e empacota (ppach).
int32_t SFILTER_ft16(SFilter* f, int32_t frames, const float* in, int16_t* out) {
    SFilter* up = nf::raw(f->next);
    if (up != nullptr) {
        // O original chama a entrada sem argumentos (nenhuma saída tem entrada no jogo).
        guard();
        return 0;
    }
    if (frames <= 0 || (frames & 15) != 0) guard();
    const int32_t n = frames > 0 ? (frames + 15) & ~15 : 16;
    const uint32_t lo = ps2::from_int(-32768), hi = ps2::from_int(32767);
    for (int32_t i = 0; i < n; ++i) {
        uint32_t v = ps2::max(ps2::fbits(in[i]), lo);
        v = ps2::min(v, hi);
        out[i] = int16_t(ps2::to_int(v));
    }
    return frames;
}

int32_t process(SFilter* f, int32_t frames, float* a2, float* a3, int32_t t0) {
    (void)t0;
    switch (f->process) {
    case kFnUnpackXapf: return SFILTER_unpackxapf(reinterpret_cast<SFilterXapf*>(f), frames, a3);
    case kFnRsf: return SFILTER_rsf(reinterpret_cast<SFilterRsf*>(f), frames, a2, a3);
    case kFnLpfRc: return SFILTER_lpfRC(reinterpret_cast<SFilterLpfRc*>(f), frames, a2, a3);
    default: guard(); return 0;
    }
}

void restore(SFilter* f) {
    switch (f->restore) {
    case kFnUnpackXapfRestore: SFILTER_unpackxapfrestore(reinterpret_cast<SFilterXapf*>(f)); break;
    case 0: break;
    default: guard(); break;
    }
}

// [driving] 0x003143f8 SFILTER_add(SFILTER **, SFILTER *): insere em ordem crescente do +0x18.
void SFILTER_add(nf::P32<SFilter>* list, SFilter* f) {
    SFilter* cur = nf::raw(*list);
    SFilter* before = nullptr;
    if (cur != nullptr) {
        uint16_t o = cur->order;
        SFilter* it = cur;
        while (cur = it, o < f->order) {
            it = nf::raw(cur->next);
            before = cur;
            if (it == nullptr) {
                f->next = nullptr;
                goto link;
            }
            o = it->order;
        }
    }
    f->next = cur;
    if (cur != nullptr) cur->prev = f;
link:
    if (before != nullptr) {
        before->next = f;
        f->prev = before;
        return;
    }
    *list = f;
}

// [driving] 0x00314460 SFILTER_remove(SFILTER **, SFILTER *)
void SFILTER_remove(nf::P32<SFilter>* list, SFilter* f) {
    SFilter* cur = nf::raw(*list);
    SFilter* it = nf::raw(cur->next);
    if (f == cur) {
        *list = cur->next;
        return;
    }
    while (true) {
        if (it == nullptr) return;
        if (it == f) break;
        cur = it;
        it = nf::raw(it->next);
    }
    SFilter* nx;
    if (it->next == nullptr) {
        nx = nf::raw(it->next);
    } else {
        nf::raw(it->next)->prev = cur;
        nx = nf::raw(nf::raw(cur->next)->next);
    }
    cur->next = nx;
}

// [driving] 0x00312f10 MIX_reset
void MIX_reset() {
    g.mixfn = kFnMixc;
    g.slice_bytes = 0x400;
}

// [driving] 0x003128a0 SNDMIXI_volramp(MIXVOICE *): as 16 primeiras amostras da fatia com o ganho rampado.
int32_t SNDMIXI_volramp(MixVoice& v) {
    const int32_t got = process(nf::raw(v.chain), 0x10, g.buf[0], g.buf[1], 0);
    if (got < 1) {
        v.wet[0] = v.wet_target[0];
        for (int i = 0; i < n_out(); ++i) v.dry[i] = v.dry_target[i];
        return 0;
    }
    v.last = g.buf[1][15];
    for (int b = 0; b < 1; ++b) {
        if (g.reverb[b] == 0) continue;
        if (ee::eq(v.wet[b], v.wet_target[b])) {
            if (!ee::eq(v.wet_target[b], 0.0f)) mix(g.mixfn, v.wet_target[b], 0x10, g.buf[1], g.wet);
        } else {
            MIXI_interpolatemix(v.wet[b], v.wet_target[b], g.buf[1], g.wet);
            v.wet[b] = v.wet_target[b];
        }
    }
    for (int i = 0; i < n_out(); ++i) {
        if (ee::eq(v.dry[i], v.dry_target[i])) {
            if (!ee::eq(v.dry_target[i], 0.0f)) mix(g.mixfn, v.dry_target[i], 0x10, g.buf[1], g.outbuf[i]);
        } else {
            MIXI_interpolatemix(v.dry[i], v.dry_target[i], g.buf[1], g.outbuf[i]);
            v.dry[i] = v.dry_target[i];
        }
    }
    return 0x10;
}

// [driving] 0x00313210 MIXI_stop
void MIXI_stop(int32_t mv) {
    MIX_stop(mv);
    if (g.create.voice_free != nullptr) g.create.voice_free(mv);
}

// [driving] 0x00313400 MIX_audioslice
void MIX_audioslice(int16_t** outs, int32_t frames) {
    for (int b = 0; b < 1; ++b)
        if (g.reverb[b] != 0 && !ee::eq(g.pending_wet[b], 0.0f)) MIXI_interpolateto0(&g.pending_wet[b], g.wet);
    for (int i = 0; i < n_out(); ++i) {
        std::memset(g.outbuf[i], 0, size_t(frames) << 2);
        if (!ee::eq(g.pending[i], 0.0f)) MIXI_interpolateto0(&g.pending[i], g.outbuf[i]);
    }
    for (int mv = 0; mv < n_voices(); ++mv) {
        MixVoice& v = voice(mv);
        if (int8_t(v.state) != 2) continue;
        int32_t ramp = 0;
        int32_t rem = frames;
        if (v.ramp != 0) {
            ramp = SNDMIXI_volramp(v);
            v.ramp = 0;
            rem = frames - ramp;
        }
        if (rem == 0) continue;
        const int32_t got = process(nf::raw(v.chain), rem, g.buf[0], g.buf[1], 0);
        if (got < 0) {
            MIXI_stop(mv);
            continue;
        }
        if (got == 0) continue;
        v.last = g.buf[1][got - 1];
        for (int i = 0; i < n_out(); ++i)
            if (!ee::eq(v.dry_target[i], 0.0f)) mix(g.mixfn, v.dry_target[i], got, g.buf[1], g.outbuf[i] + ramp);
        for (int b = 0; b < 1; ++b) {
            if (g.reverb[b] == 0 || ee::eq(v.wet_target[b], 0.0f)) continue;
            mix(g.mixfn, v.wet_target[b], got, g.buf[1], g.wet + ramp);
            g.wet[0x600] = 0.0f;  // (o original zera 0x00360400, logo depois do buffer molhado)
        }
    }
    if (g.reverb_fn != 0) guard();
    for (int i = 0; i < n_out(); ++i) {
        if (g.out_filter[i] == nullptr) {
            SFILTER_ft16(nf::raw(g.out_head[i]), frames, g.outbuf[i], outs[i]);
        } else {
            guard();
        }
    }
}

// O alinhamento a 64 bytes do MIX_create, contado a partir da base do heap do SND (no PS2 o heap do ASystem começa
// alinhado a 64: o porte usa o deslocamento, para o leiaute ser o mesmo).
uint8_t* align64(uint8_t* raw) {
    if (raw == nullptr) return nullptr;
    const SndState& s = snd_state();
    uint32_t off = uint32_t(raw - s.heap_mem);
    while ((off & 0x3f) != 0) off += 4;
    return s.heap_mem + off;
}

}  // namespace

// [driving] 0x00312b08 MIX_create
int32_t MIX_create(const SndMixCreate& p) {
    SndState& s = snd_state();
    g.reverb[0] = 0;
    g.create = p;
    SNDSYS_entercritical();
    int32_t dts_size = 0, dts_buf = 0;
    std::memcpy(&dts_size, s.opts + sndopt::kDtsSize, 4);
    std::memcpy(&dts_buf, s.opts + sndopt::kDtsBuf, 4);
    g.dts_left = dts_size;
    if (dts_size >= 0x2058) guard();  // os buffers no buffer do DTS: o jogo não liga o DTS
    for (int i = 0; i < 2; ++i) {
        uint8_t* raw = static_cast<uint8_t*>(SNDMEMI_allocz(0x2058));
        g.buf_raw[i] = raw;
        g.buf[i] = reinterpret_cast<float*>(align64(raw));
    }
    for (int i = 0; i < n_out(); ++i) {
        uint8_t* raw = static_cast<uint8_t*>(SNDMEMI_allocz(0x840));
        g.out_raw[i] = raw;
        g.outbuf[i] = reinterpret_cast<float*>(align64(raw));
    }
    if (n_voices() != 0) {
        g.voices = static_cast<MixVoice*>(SNDMEMI_allocz(int32_t(n_voices()) * 0x60));
        std::memset(static_cast<void*>(g.voices), 0, size_t(n_voices()) * 0x60);
    }
    SNDSYS_leavecritical();  // (o SNDMEM_report do nome é o 0x00307be0)
    // (o SNDMEM_report; o MIXI_initunpack16, o MIXI_initunpackxa e o MIXI_initunpackmt montam a tabela dos
    // desempacotadores: o porte só tem o de EA-XA em pacotes, o índice 8)
    std::memset(g.unpack_init, 0, sizeof g.unpack_init);
    std::memset(g.unpack_size, 0, sizeof g.unpack_size);
    g.unpack_init[3] = 0x00314c28;  // SFILTER_unpackfinit
    g.unpack_size[3] = 0x2c;
    g.unpack_init[4] = 0x00314d20;  // SFILTER_unpacklfinit
    g.unpack_size[4] = 0x30;
    g.unpack_init[5] = 0x00314e80;  // SFILTER_unpackpfinit
    g.unpack_size[5] = 0x34;
    g.unpack_init[6] = 0x00315c58;  // SFILTER_unpackxafinit
    g.unpack_size[6] = 0x2c;
    g.unpack_init[7] = 0x00315f38;  // SFILTER_unpackxalfinit
    g.unpack_size[7] = 0x44;
    g.unpack_init[8] = 0x00316178;  // SFILTER_unpackxapfinit
    g.unpack_size[8] = 0x38;
    g.unpack_init[9] = 0x00314900;  // SFILTER_unpackmtfinit
    g.unpack_size[9] = 0xd68;
    g.unpack_init[11] = 0x00314ae8;  // SFILTER_unpackmtpfinit
    g.unpack_size[11] = 0xd74;
    MIX_reset();
    if (n_out() != 0) {
        g.out_head[0] = nullptr;
        for (int i = 0; i < n_out(); ++i) {
            SFilter& f = g.ft16[i];
            f.restore = 0;
            f.process = kFnFt16;  // SFILTER_ft16init
            f.order = 0;
            SFILTER_add(&g.out_head[i], &f);
            std::memset(g.outbuf[i], 0, 0x800);
            if (i + 1 < n_out()) g.out_head[i + 1] = nullptr;
        }
    }
    return 0;
}

// [driving] 0x00312e10 MIX_destroy
void MIX_destroy() {
    SNDSYS_entercritical();
    if (g.voices != nullptr) {
        SNDMEMI_free(g.voices);
        g.voices = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if (g.buf_raw[i] != nullptr) SNDMEMI_free(g.buf_raw[i]);
        g.buf_raw[i] = nullptr;
    }
    for (int i = 0; i < n_out(); ++i) {
        if (g.out_raw[i] != nullptr) SNDMEMI_free(g.out_raw[i]);
        g.out_raw[i] = nullptr;
    }
    // (o SNDMIX_setmasterlowpass(0x40000000), vazio)
    SNDSYS_leavecritical();
}

// [driving] 0x003137b8 MIX_audio
void MIX_audio(int16_t* const* out, int32_t frames) {
    int16_t* o[8] = {};
    for (int i = 0; i < n_out(); ++i) o[i] = out[i];
    while (frames > 0) {
        const int32_t n = frames < 0x201 ? frames : 0x200;
        MIX_audioslice(o, n);
        for (int i = 0; i < n_out(); ++i) o[i] = reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(o[i]) +
                                                                            g.slice_bytes);
        frames -= 0x200;
    }
}

// [driving] 0x00312f40 MIX_playinit
void MIX_playinit(int32_t mv, uint8_t rep, int32_t mode, const uint8_t* data, int32_t t0, uint32_t stretch,
                  int32_t channels, int32_t frames, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12,
                  uint8_t chan) {
    (void)t0;
    (void)arg9;
    (void)arg11;
    (void)arg12;
    (void)channels;
    (void)data;
    MixVoice& v = voice(mv);
    v.chain = nullptr;
    v.unpack = nullptr;
    v.rsf = nullptr;
    v.lowpass = nullptr;
    v.highpass = nullptr;
    v.stretch = nullptr;
    int32_t idx = 0;
    if (rep == 8 || rep == 7) idx = 3;
    else if (rep == 9) idx = 0;  // (o +0x18 dos parâmetros vira 0)
    else if (rep == 10) idx = 6;
    else if (rep == 4) idx = 9;
    else if (rep == 0xe) idx = 0xc;
    else if (rep == 0xf) idx = 0xf;
    else if (rep == 0x10) idx = 0x12;
    else idx = rep == 0x40 ? 0x15 : 0;
    if (mode == 1) idx += arg10 > 0 ? 1 : 0;
    else if (mode == 0) idx += 2;
    if (g.unpack_init[idx] != 0) {
        uint8_t* obj = static_cast<uint8_t*>(SNDMEMI_allocz(int32_t(g.unpack_size[idx])));
        SFilter* f = reinterpret_cast<SFilter*>(obj);
        f->chan = chan;
        f->order = 0xf0;
        f->restore = 0;
        int32_t w24 = 0;  // o +0x24 dos parâmetros (o desempacotador de EA-XA não o escreve)
        if (g.unpack_init[idx] == 0x00316178) {
            // [driving] 0x00316178 SFILTER_unpackxapfinit
            SFilterXapf* x = reinterpret_cast<SFilterXapf*>(f);
            x->h.process = kFnUnpackXapf;
            x->h.restore = kFnUnpackXapfRestore;
            const int32_t master = SNDDRV_getmastervoice(mv);
            x->player = SNDPKTPLAYI_voicetopackethandle(master);
            x->chan = uint8_t(SNDDRV_getsamplechan(mv));
            x->pkt = nullptr;
            x->frames = frames;
            x->decoded = 0;
            x->pending = 0;
            void* mem = SNDMEMI_allocz(0xa8);  // o operator new do CODA (o SNDI_New)
            static_assert(sizeof(CEAXABLKDecf) <= 0xa8);
            CEAXABLKDecf* d = ::new (mem) CEAXABLKDecf;
            CEAXABLKDecf_Construct(*d);
            x->dec = d;
        } else {
            guard();  // os outros desempacotadores (o cabeçalho)
            f->process = 0;
        }
        v.w44 = w24;
        v.unpack = f;
        SFILTER_add(&v.chain, nf::raw(v.unpack));
    }
    if (stretch != 0) guard();  // a esticada de tempo (SFILTER_timestretchinit): nenhum fluxo a usa
    v.state = 1;
}

// [driving] 0x00313178 MIX_play
void MIX_play(int32_t mv) {
    MixVoice& v = voice(mv);
    v.last = 0.0f;
    v.ramp = 0;
    v.wet[0] = v.wet_target[0];
    for (int i = 0; i < n_out(); ++i) v.dry[i] = v.dry_target[i];
    v.state = 2;
}

// [driving] 0x00313248 MIX_stop
void MIX_stop(int32_t mv) {
    MixVoice& v = voice(mv);
    SFilter* f = nf::raw(v.chain);
    if (f == nullptr) guard();  // o original leria o +4 de um ponteiro nulo (nunca: o MIX_setpitch sempre cria o rsf)
    g.pending_wet[0] = ee::add(g.pending_wet[0], ee::mul(v.wet[0], v.last));
    for (int i = 0; i < n_out(); ++i) g.pending[i] = ee::add(g.pending[i], ee::mul(v.dry[i], v.last));
    while (f != nullptr) {
        SFilter* nx;
        if (f->restore == 0) {
            nx = nf::raw(f->next);
        } else {
            restore(f);
            f = nf::raw(v.chain);
            nx = nf::raw(f->next);
        }
        SNDMEMI_free(f);
        v.chain = nx;
        f = nx;
    }
    v.state = 0;
}

// [driving] 0x00313f90 MIX_setpitch
void MIX_setpitch(int32_t mv, uint32_t pitch) {
    int32_t p = int32_t(pitch) < 0x20001 ? int32_t(pitch) : 0x20000;
    MixVoice& v = voice(mv);
    if (v.rsf == nullptr) {
        SFilterRsf* f = static_cast<SFilterRsf*>(SNDMEMI_allocz(0x38));
        v.rsf = &f->h;
        f->h.order = 0xa0;
        f->h.restore = 0;
        // [driving] 0x00317b28 SFILTER_rsfinit (os outros dois argumentos não são lidos)
        f->h.restore = 0;
        f->frac = 0;
        f->h.process = kFnRsf;
        f->primed = 0;
        f->delay = 0;
        for (int i = 0; i < 4; ++i) f->hist[i] = 0.0f;
        SFILTER_add(&v.chain, nf::raw(v.rsf));
    }
    reinterpret_cast<SFilterRsf*>(nf::raw(v.rsf))->pitch = p;
}

// [driving] 0x00313350 SNDMIX_setdrygain
void SNDMIX_setdrygain(float gain, int32_t mv, int32_t output) {
    MixVoice& v = voice(mv);
    v.dry_target[output] = gain;
    v.ramp = 1;
}

// [driving] 0x00313380 SNDMIX_setwetgain
void SNDMIX_setwetgain(float gain, int32_t mv, int32_t bus) {
    MixVoice& v = voice(mv);
    v.wet_target[bus] = g.reverb[bus] == 2 ? ee::mul(gain, 1.5f) : gain;
    v.ramp = 1;
}

// [driving] 0x00314038 MIX_settimemult
void MIX_settimemult(int32_t mv, int32_t /*mult*/) {
    if (voice(mv).stretch != nullptr) guard();
}

// [driving] 0x00314068 MIX_setdistortlevel
void MIX_setdistortlevel(int32_t mv, int32_t level) {
    if (level > 0 || voice(mv).distort != nullptr) guard();
}

// [driving] 0x003141e8 MIX_setlowpass
void MIX_setlowpass(float cutoff, int32_t mv) {
    MixVoice& v = voice(mv);
    const uint16_t rate = uint16_t(snd_state().opts[sndopt::kMixRate] | (snd_state().opts[sndopt::kMixRate + 1] << 8));
    if (ee::lt(cutoff, 1.0f)) {
        if (v.lowpass == nullptr) {
            SFilterLpfRc* f = static_cast<SFilterLpfRc*>(SNDMEMI_allocz(0x28));
            f->h.order = 0x28;
            v.lowpass = &f->h;
            f->h.restore = 0;
            // [driving] 0x00316798 SFILTER_createLPFRC
            f->y = 0.0f;
            f->h.next = nullptr;
            f->h.process = kFnLpfRc;
            f->h.in2 = nullptr;
            f->h.prev = nullptr;
            f->h.w14 = 0;
            f->h.cleaned = 0;
            f->h.restore = 0;
            SFILTER_add(&v.chain, nf::raw(v.lowpass));
        }
        // [driving] 0x003167c8 SFILTER_modifyLPFRC
        const int32_t p0 = ee::cvt_w(ee::mul(cutoff, ee::cvt_s(int32_t(rate)))) << 7;
        const int32_t p1 = int32_t(rate) << 8;
        const int32_t p2 = 0x100;
        float f0 = ee::cvt_s(p0);
        const float f4 = ee::cvt_s(p1);
        float f1 = ee::cvt_s(p2);
        f0 = ee::add(f0, f0);
        f1 = ee::mul(f1, ps2::ffloat(0x3b800000u));
        f0 = ee::div(f0, f4);
        float f2 = ee::sub(1.0f, f0);
        f0 = ee::mul(f0, f1);
        f2 = ee::mul(f2, f1);
        SFilterLpfRc* f = reinterpret_cast<SFilterLpfRc*>(nf::raw(v.lowpass));
        f->b = f0;
        f->a = f2;
    } else if (v.lowpass != nullptr) {
        SFILTER_remove(&v.chain, nf::raw(v.lowpass));
        SNDMEMI_free(nf::raw(v.lowpass));
        v.lowpass = nullptr;
    }
}

// [driving] 0x00314120 MIX_sethighpass
void MIX_sethighpass(int32_t mv, int32_t cutoff) {
    if (cutoff > 0 || voice(mv).highpass != nullptr) guard();
}

// [driving] 0x00313f70 MIX_setwetbuffer
void MIX_setwetbuffer(uint8_t* buf) { g.wet_ext = buf; }

// [driving] 0x00312f30 MIX_setresamplequality
void MIX_setresamplequality(uint8_t q) { g.quality = q; }

// [driving] 0x00318520 MIX_initreverb: o reverb do mixador do EE (SNDMIXI_initfx2) não está portado (o cabeçalho).
void MIX_initreverb(uint32_t, const uint8_t*) { guard(); }

// [driving] 0x003187b8 MIX_restorereverb (sem o reverb do EE: só os estados)
void MIX_restorereverb() {
    g.reverb_fn = 0;
    g.reverb[0] = 0;
}

int32_t mix_voices_playing() {
    int32_t n = 0;
    if (g.voices != nullptr)
        for (int i = 0; i < n_voices(); ++i) n += g.voices[i].state == 2 ? 1 : 0;
    return n;
}

uint64_t mix_guards() { return g.guards; }
void mix_power_on() { g.guards = 0; }

MixInternals mix_internals() {
    MixInternals r;
    r.create = &g.create;
    r.reverb = g.reverb[0];
    r.quality = g.quality;
    r.buf_a = g.buf[0];
    r.buf_b = g.buf[1];
    r.outbuf = g.outbuf;
    r.pending = g.pending;
    r.pending_wet = g.pending_wet;
    r.voices = g.voices;
    return r;
}

}  // namespace drv
