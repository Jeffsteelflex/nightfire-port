// O mixador do IOP do SNDDRV.IRX (driving/sound/iop_mix.h), do MIPS do módulo (as decompilações em
// out/agent_drv_sound/decomp_irx do checkout principal). Os campos dos filtros e das vozes guardam os deslocamentos do
// módulo nos comentários; os objetos são do port (a memória do SNDMEMI não é reproduzida).

#include "driving/sound/iop_mix.h"

#include <cstring>
#include <memory>
#include <vector>

namespace drv {

namespace {

IopMixStats g_stats;

// [snddrv] 0x0000b470 xafp (.data): os coeficientes (k0, k1) de cada filtro do EA-XA, em 1/256.
const int16_t kXafp[4][2] = {{0, 0}, {240, 0}, {460, -208}, {392, -220}};

// A função de cada filtro (+0x00): o reamostrador e os três desempacotadores do EA-XA.
enum FilterKind { kRs, kXaf, kXalf, kXalfSection };

// Um SFILTER do mixador (0x40 bytes o reamostrador, 0xb0 o xaf, 0x138 o xalf).
struct Filter {
    FilterKind kind = kRs;        // +0x00
    Filter* next = nullptr;       // +0x08
    Filter* prev = nullptr;       // +0x10
    uint16_t order = 0;           // +0x18
    uint8_t chan = 0;             // +0x1a
    // Os desempacotadores: o pedido do decxa16c (+0x1c..+0x2c) e o estado.
    int32_t count = 0;            // +0x1c
    int32_t s1 = 0, s2 = 0;       // +0x20, +0x24
    const uint8_t* in = nullptr;  // +0x28
    int32_t* out = nullptr;       // +0x2c
    const uint8_t* data = nullptr;  // +0x30
    int32_t frames = 0;           // +0x34
    int32_t pos = 0;              // +0x38
    int32_t a3c = 0;              // +0x3c (xaf: as amostras que sobraram; xalf: o começo do laço)
    int32_t loop_end = 0;         // +0x40 (xalf: o fim do laço, incluído)
    int32_t resid = 0;            // +0x44 (xalf: as amostras que sobraram)
    int32_t save1 = 0, save2 = 0; // +0x48, +0x4c (xalf: o estado no bloco do começo do laço)
    int32_t buf[56] = {};         // +0x40 (xaf) / +0x58 (xalf): as amostras que sobraram
    const uint8_t* end = nullptr; // port: o fim da memória do IOP (a guarda das leituras)
    // O reamostrador.
    int32_t pitch[4] = {};        // +0x20..+0x2c (o passo, x2, x3, x4)
    uint32_t phase = 0;           // +0x30 (16.16)
    uint8_t primed = 0;           // +0x34
    int8_t delay = 0;             // +0x35
    int8_t taps = 0;              // +0x36
    bool linear = false;          // +0x1c (SFILTERI_rs16linear ou SFILTERI_rs16nearest)
    int32_t hist[2] = {};         // +0x38
};

struct Voice {                    // 0x50 bytes
    uint8_t state = 0;            // +0x00 (1 iniciada, 2 tocando)
    uint8_t dirty = 0;            // +0x01 um ganho mudou (o SNDMIXI_volramp na próxima fatia)
    int32_t cur[4] = {};          // +0x04 o ganho seco de agora, por saída
    int32_t target[4] = {};       // +0x1c o pedido
    int32_t wet_cur = 0;          // +0x34
    int32_t wet_target = 0;       // +0x38
    int32_t last = 0;             // +0x3c a última amostra (o estalo do fim)
    Filter* chain = nullptr;      // +0x40
    bool getframe = false;        // +0x44 (nulo = SNDI_getb, no endereço 0)
    Filter* unpack = nullptr;     // +0x48
    Filter* rs = nullptr;         // +0x4c
    std::vector<std::unique_ptr<Filter>> owned;  // port: os blocos do SNDMEMI
};

struct Mixer {
    uint32_t rate = 0;            // sndmix
    int voices = 0, outputs = 0;  // 0x00011dc4 (bytes 0 e 1)
    uint8_t quality = 0;          // 0x00011dce
    uint8_t fx = 0;               // 0x00011dcf o reverb do mixador (o MIX_initreverb: 1 ou 2; 0 desligado)
    void (*voice_free)(int32_t) = nullptr;  // 0x00011dc8
    std::vector<Voice> v;         // 0x00011f54
    std::vector<int32_t> a, b;    // 0x00011ee8, 0x00011eec (0x20bc bytes cada, alinhados em 64)
    int32_t last[4] = {};         // 0x00011f08 o estalo do fim por saída
    int32_t wet_last = 0;         // 0x00011f20
    int32_t* out[4] = {};         // 0x00011ef0
};
Mixer g_mix;

// [snddrv] 0x00003de0 mixiop: soma amostra x (ganho >> 8) (desenrolado no original: o mesmo resultado).
void mixiop(int n, int32_t gain, const int32_t* src, int32_t* dst) {
    const uint32_t g = uint32_t(gain >> 8);
    for (int i = 0; i < n; ++i) dst[i] = int32_t(uint32_t(dst[i]) + uint32_t(src[i]) * g);
}

// [snddrv] 0x00000ba8 MIXI_interpolatemix: as 16 primeiras com o ganho em passos de (para - de) / 17.
void MIXI_interpolatemix(int32_t from, int32_t to, const int32_t* src, int32_t* dst) {
    const int32_t step = (to - from) / 0x11;
    int32_t g = from;
    for (int i = 0; i < 16; ++i) {
        g += step;
        dst[i] = int32_t(uint32_t(dst[i]) + uint32_t(src[i]) * uint32_t(g >> 8));
    }
}

// [snddrv] 0x00000970 MIXI_interpolateto0: o último valor de uma voz que parou até zero em 16 amostras.
void MIXI_interpolateto0(int32_t* last, int32_t* dst) {
    static const int32_t kRamp[16] = {0x77, 0x70, 0x68, 0x61, 0x59, 0x52, 0x4a, 0x43,
                                      0x3b, 0x34, 0x2c, 0x25, 0x1d, 0x16, 0xe, 7};
    for (int i = 0; i < 16; ++i) dst[i] = int32_t(uint32_t(dst[i]) + uint32_t(*last) * uint32_t(kRamp[i]));
    *last = 0;
}

// [snddrv] 0x00002240 SFILTER_add: antes do primeiro filtro de ordem (+0x18) maior ou igual.
void SFILTER_add(Filter** head, Filter* f) {
    Filter* p = *head;
    Filter* before = nullptr;
    while (p != nullptr && p->order < f->order) {
        before = p;
        p = p->next;
    }
    f->next = p;
    if (p != nullptr) p->prev = f;
    if (before == nullptr) {
        *head = f;
        return;
    }
    before->next = f;
    f->prev = before;
}

// [snddrv] 0x0000a390 decxa16c: blocos de 15 bytes (o filtro e o deslocamento no primeiro, 28 amostras de 4 bits) até
// o pedido (+0x1c) ficar <= 0; o resto negativo diz quantas amostras passaram do pedido.
void decxa16c(Filter& f) {
    while (f.count > 0) {
        int32_t* o = f.out;
        f.count -= 0x1c;
        if (f.end != nullptr && (f.in > f.end || f.end - f.in < 15)) {
            // port: um bloco além da memória do IOP (dados inválidos): zeros
            ++g_stats.guards;
            for (int i = 0; i < 0x1c; ++i) o[i] = 0;
            f.out = o + 0x1c;
            f.in += 15;
            continue;
        }
        const uint8_t h = *f.in;
        const int k = h >> 4;
        const int shift = (h & 0xf) + 8;
        int32_t k0 = 0, k1 = 0;
        if (k < 4) {
            k0 = kXafp[k][0];
            k1 = kXafp[k][1];
        } else {
            ++g_stats.guards;  // o IOP leria depois da tabela (a .sbss, zerada no carregamento): zeros
        }
        ++f.in;
        int32_t* stop = o + 0x1c;
        while (o < stop) {
            const uint8_t byte = *f.in;
            int32_t v = int32_t(uint32_t(int32_t(uint32_t(byte >> 4) << 28) >> shift) + uint32_t(f.s1) * uint32_t(k0) +
                                uint32_t(f.s2) * uint32_t(k1)) >> 8;
            o[0] = v;
            f.s2 = v;
            v = int32_t(uint32_t(int32_t(uint32_t(byte) << 28) >> shift) + uint32_t(v) * uint32_t(k0) +
                        uint32_t(f.s1) * uint32_t(k1)) >> 8;
            o[1] = v;
            f.s1 = v;
            ++f.in;
            o += 2;
            f.out = o;
        }
    }
}

const uint8_t* block_at(const Filter& f, int32_t pos) { return f.data + (pos * 0xf) / 0x1c; }

// [snddrv] 0x00009b80 SFILTER_unpackxaf: o EA-XA sem laço; -1 depois do fim.
int unpackxaf(Filter& f, int n, int32_t* out) {
    if (!(f.pos < f.frames)) return -1;
    f.out = out;
    if (f.a3c < n) {
        for (int i = 0; i < f.a3c; ++i) *f.out++ = f.buf[i];
        int avail = f.frames - f.pos;
        n -= f.a3c;
        if (avail < 0) avail = 0;
        const int take = n < avail ? n : avail;
        f.count = take;
        f.in = block_at(f, f.pos);
        decxa16c(f);
        f.pos = (f.pos + take) - f.count;
        if (avail == take) {
            // o fim: o resto em zeros (as que sobraram do bloco não são guardadas, nem o +0x3c muda)
            int k = (n - take) - (f.pos - f.frames);
            while (k-- > 0) *f.out++ = 0;
            return 1;
        }
        int j = 0;
        for (int32_t i = f.count; i < 0; ++i) f.buf[j++] = f.out[i];
        f.a3c = j;
    } else {
        int i = 0;
        for (; i < n; ++i) *f.out++ = f.buf[i];
        int j = 0;
        for (; i < f.a3c; ++i) f.buf[j++] = f.buf[i];
        f.a3c = j;
    }
    return 1;
}

// [snddrv] 0x0000a4d0 unpackxafresidual: até `want` das `have` guardadas; as outras vão ao começo.
int unpackxafresidual(int have, int32_t* buf, int want, int32_t** out) {
    int32_t* o = *out;
    if (have < want) want = have;
    int i = 0;
    for (; i < want; ++i) *o++ = buf[i];
    int j = 0;
    for (; i < have; ++i) buf[j++] = buf[i];
    *out = o;
    return want;
}

// [snddrv] 0x00009e10 SFILTER_unpackxalfsection: o laço; no bloco do fim volta ao bloco do começo com o estado guardado.
int unpackxalfsection(Filter& f, int n, int32_t* out) {
    f.out = out;
    while (n > 0) {
        if (f.resid == 0) {
            if (f.pos + 0x1b < f.loop_end) {
                f.count = 0x1c;
                f.in = block_at(f, f.pos);
                decxa16c(f);
                n -= 0x1c;
                f.pos += 0x1c;
            } else {
                int32_t* keep = f.out;
                f.count = 0x1c;
                f.out = f.buf;
                f.in = block_at(f, f.pos);
                decxa16c(f);
                const int32_t start = f.a3c;
                f.pos += 0x1c;
                f.resid = 0x1d - (f.pos - f.loop_end);
                f.s1 = f.save1;
                f.s2 = f.save2;
                f.pos = (start / 0x1c) * 0x1c;
                if (start != f.pos) {
                    int32_t tmp[0x1e];  // auStack_98: duas palavras e o bloco
                    f.count = 0x1c;
                    f.out = tmp + 2;
                    f.in = block_at(f, f.pos);
                    decxa16c(f);
                    f.pos += 0x1c;
                    const int tail = f.pos - start;
                    const int32_t* src = tmp + (0x1e - tail);
                    for (int i = 0; i < tail; ++i) f.buf[f.resid++] = src[i];
                }
                f.out = keep;
            }
        } else {
            const int got = unpackxafresidual(f.resid, f.buf, n, &f.out);
            n -= got;
            f.resid -= got;
        }
    }
    f.out += n;
    for (; n < 0; ++n) f.buf[f.resid++] = *f.out++;
    return 1;
}

// [snddrv] 0x0000a0ec SFILTER_unpackxalf: o EA-XA com laço, até o bloco do começo do laço; dali o unpackxalfsection.
int unpackxalf(Filter& f, int n, int32_t* out) {
    f.out = out;
    if (f.resid < n) {
        for (int i = 0; i < f.resid; ++i) *f.out++ = f.buf[i];
        n -= f.resid;
        int take = (f.a3c - f.pos) - 0x1b;
        if (n < take) take = n;
        if (take < 0) take = 0;
        f.count = take;
        f.in = block_at(f, f.pos);
        decxa16c(f);
        const int32_t rem = f.count;
        f.pos = (f.pos + take) - rem;
        int j = 0;
        for (int32_t i = rem; i < 0; ++i) f.buf[j++] = f.out[i];
        f.resid = j;
        f.out -= j;
        if (f.a3c - f.pos < 0x1c) {
            f.kind = kXalfSection;
            f.save1 = f.s1;
            f.save2 = f.s2;
            if (0 < n - take) unpackxalfsection(f, n - take, f.out);
        }
    } else {
        int i = 0;
        for (; i < n; ++i) *f.out++ = f.buf[i];
        int j = 0;
        for (; i < f.resid; ++i) f.buf[j++] = f.buf[i];
        f.resid = j;
    }
    return 1;
}

// [snddrv] 0x00003830 SFILTERI_rs16linear / 0x000037a0 SFILTERI_rs16nearest: de quatro em quatro amostras.
void rs16(Filter& f, int n, const int32_t* src, int32_t* dst) {
    if (f.linear) {
        for (int i = 0; i < n; i += 4) {
            for (int k = 0; k < 4; ++k) {
                const uint32_t ph = f.phase + uint32_t(k == 0 ? 0 : f.pitch[k - 1]);
                const int32_t* p = src + (ph >> 16);
                dst[k] = (int32_t(uint32_t(p[1] - p[0]) * (ph & 0xffff)) >> 16) + p[0];
            }
            f.phase += uint32_t(f.pitch[3]);
            dst += 4;
        }
    } else {
        uint32_t ph = f.phase;
        const int32_t* end = dst + n;
        do {
            dst[0] = src[ph >> 16];
            dst[1] = src[(ph + uint32_t(f.pitch[0])) >> 16];
            dst[2] = src[(ph + uint32_t(f.pitch[1])) >> 16];
            dst[3] = src[(ph + uint32_t(f.pitch[2])) >> 16];
            ph += uint32_t(f.pitch[3]);
            dst += 4;
        } while (dst < end);
        f.phase = ph;
    }
}

int run(Filter* f, int n, int32_t* a, int32_t* b);

// [snddrv] 0x00003990 SFILTER_rs: o reamostrador; puxa do filtro seguinte as amostras de que precisa (mais as do
// histórico) e devolve n.
int rs(Filter& f, int n, int32_t* a, int32_t* b) {
    int r = 1;
    const int keep = f.taps - f.delay;
    if (f.pitch[0] == 0x10000) {
        int want;
        if (f.primed == 0) {
            want = n + keep;
        } else {
            for (int i = 0; i < keep; ++i) b[i] = f.hist[i + f.delay];
            b += keep;
            want = n;
        }
        if (f.next) r = run(f.next, want, a, b);
        if (r < 1) {
            f.primed = 1;
            for (int i = 0; i < f.taps; ++i) f.hist[i] = 0;
            return r;
        }
        if (f.primed == 0) {
            f.primed = 1;
            for (int i = 0; i < f.taps; ++i) f.hist[i] = b[n + i - f.delay];
        } else {
            for (int i = 0; i < f.taps; ++i) f.hist[i] = b[n + i - f.taps];
        }
        return n;
    }
    int need = int((uint32_t(f.pitch[0]) * uint32_t(n) + f.phase) >> 16);
    if (f.primed == 0) {
        need += keep;
        for (int i = 0; i < f.delay; ++i) *a++ = 0;
    } else {
        for (int i = 0; i < f.taps; ++i) a[i] = f.hist[i];
        a += f.taps;
    }
    if (0 < need && f.next) {
        r = run(f.next, need, b, a);
        if (r < 1) {
            f.primed = 1;
            for (int i = 0; i < f.taps; ++i) f.hist[i] = 0;
            return r;
        }
    }
    if (f.primed == 0) f.primed = 1;
    else a -= keep;
    rs16(f, n, a, b);
    for (int i = 0; i < f.taps; ++i) f.hist[i] = a[int(f.phase >> 16) + i - f.delay];
    f.phase &= 0xffff;
    return n;
}

int run(Filter* f, int n, int32_t* a, int32_t* b) {
    switch (f->kind) {
        case kRs: return rs(*f, n, a, b);
        case kXaf: return unpackxaf(*f, n, b);
        case kXalf: return unpackxalf(*f, n, b);
        case kXalfSection: return unpackxalfsection(*f, n, b);
    }
    return -1;
}

Voice* voice_at(int v) { return v >= 0 && v < int(g_mix.v.size()) ? &g_mix.v[size_t(v)] : nullptr; }

// A cadeia da voz. port: uma voz sem cadeia (o PS2 pularia para o endereço lido em 0) ou sem desempacotador (um
// formato não portado: o PS2 reamostraria o que houvesse no buffer A) termina.
int run_voice(Voice& v, int n) {
    if (v.chain == nullptr || v.unpack == nullptr) {
        ++g_stats.guards;
        return -1;
    }
    return run(v.chain, n, g_mix.a.data(), g_mix.b.data());
}

// [snddrv] 0x00000dcc SNDMIXI_volramp: as 16 primeiras amostras com o ganho em rampa até o pedido.
int SNDMIXI_volramp(Voice& v) {
    const int r = run_voice(v, 0x10);
    const int32_t* b = g_mix.b.data();
    if (r < 1) {
        v.wet_cur = v.wet_target;
        for (int k = 0; k < g_mix.outputs; ++k) v.cur[k] = v.target[k];
        return 0;
    }
    v.last = b[15];
    ++g_stats.ramps;
    if (g_mix.fx != 0) {
        // (o reverb do mixador: desligado no jogo, ver o cabeçalho)
        if (v.wet_cur != v.wet_target) v.wet_cur = v.wet_target;
    }
    for (int k = 0; k < g_mix.outputs; ++k) {
        if (v.cur[k] == v.target[k]) {
            if (v.cur[k] != 0) mixiop(0x10, v.cur[k], b, g_mix.out[k]);
        } else {
            MIXI_interpolatemix(v.cur[k], v.target[k], b, g_mix.out[k]);
            v.cur[k] = v.target[k];
        }
    }
    return 0x10;
}

void free_chain(Voice& v) {
    v.chain = nullptr;
    v.unpack = nullptr;
    v.rs = nullptr;
    v.getframe = false;
    v.owned.clear();
}

}  // namespace

IopMixStats& iop_mix_stats() { return g_stats; }

int32_t iop_decxa16c(int32_t count, int32_t& s1, int32_t& s2, const uint8_t*& in, int32_t*& out) {
    Filter f;
    f.count = count;
    f.s1 = s1;
    f.s2 = s2;
    f.in = in;
    f.out = out;
    decxa16c(f);
    s1 = f.s1;
    s2 = f.s2;
    in = f.in;
    out = f.out;
    return f.count;
}

// [snddrv] 0x00000fc8 MIX_create e 0x0000115c MIX_reset (o mixiop e a fatia de 0x400).
void IopMix_create(uint32_t rate, int voices, int outputs, void (*voice_free)(int32_t)) {
    g_mix = Mixer();
    g_mix.rate = rate;
    g_mix.voices = voices;
    g_mix.outputs = outputs > 4 ? 4 : outputs;
    g_mix.voice_free = voice_free;
    g_mix.v.resize(size_t(voices));
    g_mix.a.assign(0x20bc / 4, 0);
    g_mix.b.assign(0x20bc / 4, 0);
    g_stats = IopMixStats();
}

// [snddrv] 0x0000117c MIX_setresamplequality
void IopMix_setresamplequality(int quality) { g_mix.quality = uint8_t(quality); }

// [snddrv] 0x0000118c MIX_playinit (o SNDIOP_updateiopvoices passa 1 canal, a contagem, o começo e o fim do laço)
void IopMix_playinit(int voice, int format, int channels, const uint8_t* data, const uint8_t* data_end, int32_t frames,
                     int32_t loop_start, int32_t loop_end, int chan) {
    Voice* v = voice_at(voice);
    if (!v) return;
    free_chain(*v);
    // A tabela de 0x00011dd8 por (formato x 3) + 1 (um canal com laço) ou + 2 (canais 0); o MIXI_initunpackxa preenche
    // as entradas 6 (0x00009dc8 SFILTER_unpackxafinit) e 7 (0x0000a338 SFILTER_unpackxalfinit).
    int index = 0;
    if (format == 8 || format == 7) index = 3;
    else if (format == 10) index = 6;
    else if (format == 4) index = 9;
    else if (format == 0xe) index = 0xc;
    else if (format == 0xf) index = 0xf;
    else if (format == 0x10) index = 0x12;
    else if (format == 0x40) index = 0x15;
    if (channels == 1) {
        if (0 < loop_end) ++index;
    } else if (channels == 0) {
        index += 2;
    }
    if (index == 6 || index == 7) {
        auto f = std::make_unique<Filter>();
        f->order = 0xf0;
        f->chan = uint8_t(chan);
        f->data = data;
        f->end = data_end;
        f->frames = frames;
        f->pos = 0;
        f->s1 = f->s2 = 0;
        if (index == 6) {
            f->kind = kXaf;
            f->a3c = 0;
        } else {
            f->kind = kXalf;
            f->a3c = loop_start;
            f->loop_end = loop_end;
            f->resid = 0;
        }
        v->unpack = f.get();
        v->getframe = true;
        SFILTER_add(&v->chain, f.get());
        v->owned.push_back(std::move(f));
        ++g_stats.starts;
    } else {
        ++g_stats.guards;  // port: os desempacotadores de PCM (MIXI_initunpack8/16) e os outros formatos
    }
    v->state = 1;
}

// [snddrv] 0x00001388 MIX_play
void IopMix_play(int voice) {
    Voice* v = voice_at(voice);
    if (!v) return;
    v->last = 0;
    v->dirty = 0;
    v->wet_cur = v->wet_target;
    for (int k = 0; k < g_mix.outputs; ++k) v->cur[k] = v->target[k];
    v->state = 2;
}

// [snddrv] 0x00001444 MIX_stop: o último valor da voz vai ao estalo do fim de cada saída.
void IopMix_stop(int voice) {
    Voice* v = voice_at(voice);
    if (!v) return;
    g_mix.wet_last += ((v->wet_cur >> 8) * v->last) >> 7;
    for (int k = 0; k < g_mix.outputs; ++k) g_mix.last[k] += ((v->cur[k] >> 8) * v->last) >> 7;
    if (v->chain == nullptr) ++g_stats.guards;  // port: o PS2 leria o endereço 4
    free_chain(*v);
    v->state = 0;
    ++g_stats.stops;
}

// [snddrv] 0x00002160 MIX_setpitch: o reamostrador na primeira vez (0x00003d5c SFILTER_rsinit, linear se a qualidade
// passa de 0x18), depois 0x00003970 SFILTER_rssetpitch.
void IopMix_setpitch(int voice, int32_t pitch) {
    Voice* v = voice_at(voice);
    if (!v) return;
    if (0x40000 < pitch) pitch = 0x40000;
    if (v->rs == nullptr) {
        auto f = std::make_unique<Filter>();
        f->kind = kRs;
        f->order = 0xa0;
        f->linear = 0x18 < g_mix.quality;
        f->taps = f->linear ? 2 : 1;
        f->delay = 0;
        f->primed = 0;
        f->phase = 0;
        v->rs = f.get();
        SFILTER_add(&v->chain, f.get());
        v->owned.push_back(std::move(f));
    }
    v->rs->pitch[0] = pitch;
    v->rs->pitch[1] = pitch * 2;
    v->rs->pitch[2] = pitch * 3;
    v->rs->pitch[3] = int32_t(uint32_t(pitch) << 2);
}

// [snddrv] 0x00002100 MIX_getframe (0x00009dbc / 0x0000a32c: o +0x38 do desempacotador)
int32_t IopMix_getframe(int voice) {
    Voice* v = voice_at(voice);
    if (!v || !v->getframe || !v->unpack) return -1;
    return v->unpack->pos;
}

// [snddrv] 0x00001564 SNDMIX_setdrygain
void IopMix_setdrygain(int voice, int output, int32_t gain) {
    Voice* v = voice_at(voice);
    if (!v || output < 0 || output >= 4) return;
    v->target[output] = gain;
    v->dirty = 1;
}

// [snddrv] 0x000015a4 SNDMIX_setwetgain (x 1,5 com o reverb do modo 2)
void IopMix_setwetgain(int voice, int fx, int32_t gain) {
    Voice* v = voice_at(voice);
    if (!v || fx != 0) return;
    v->wet_target = g_mix.fx == 2 ? (gain * 3) >> 1 : gain;
    v->dirty = 1;
}

bool IopMix_playing(int voice) {
    const Voice* v = voice_at(voice);
    return v && v->state == 2;
}

// [snddrv] 0x00001634 MIX_audioslice
void IopMix_audioslice(int32_t* const* outputs, int n) {
    ++g_stats.slices;
    for (int k = 0; k < g_mix.outputs; ++k) g_mix.out[k] = outputs[k];
    for (int k = 0; k < g_mix.outputs; ++k) {
        std::memset(g_mix.out[k], 0, size_t(n) * 4);
        if (g_mix.last[k] != 0) MIXI_interpolateto0(&g_mix.last[k], g_mix.out[k]);
    }
    bool active = false;
    for (int i = 0; i < int(g_mix.v.size()); ++i) {
        Voice& v = g_mix.v[size_t(i)];
        if (v.state != 2) continue;
        int pre = 0, left = n;
        if (v.dirty) {
            pre = SNDMIXI_volramp(v);
            v.dirty = 0;
            left = n - pre;
        }
        if (left == 0) continue;
        ++g_stats.voice_slices;
        const int r = run_voice(v, left);
        if (r < 0) {
            // [snddrv] 0x0000140c MIXI_stop: o MIX_stop e o aviso ao driver (o SNDIOP_mixvoicefree)
            IopMix_stop(i);
            ++g_stats.ended;
            if (g_mix.voice_free) g_mix.voice_free(i);
        } else if (r != 0) {
            const int32_t* b = g_mix.b.data();
            v.last = b[r - 1];
            g_stats.samples += uint64_t(r);
            for (int k = 0; k < g_mix.outputs; ++k)
                if (v.target[k] != 0) mixiop(r, v.target[k], b, g_mix.out[k] + pre);
        }
        active = true;
    }
    if (active) {
        // port: o relatório (o pico na escala que o SNDIOP_ee48_iop48 soma, >> 7)
        if (g_stats.active_slices++ == 0) g_stats.first_active = g_stats.slices;
        g_stats.last_active = g_stats.slices;
        for (int k = 0; k < g_mix.outputs; ++k)
            for (int i = 0; i < n; ++i) {
                const int32_t x = g_mix.out[k][i] >> 7;
                const int32_t m = x < 0 ? -x : x;
                if (g_stats.peak < m) g_stats.peak = m;
            }
    }
}

}  // namespace drv
