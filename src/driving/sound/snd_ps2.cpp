// A plataforma do PS2 do SND no EE (SNDPLATFORM_*, SNDDRV_*): as vozes da SPU2 na tagbuf, as do IOP, as do mixador do
// EE, a fila de transferências das amostras e o alocador da RAM da SPU2. Portado do MIPS do DRIVING.ELF; o IOP do outro
// lado é o driving/sound/snddrv_iop.h. docs/driving.md seção 17.
//
// Divergências (port), além das do cabeçalho de snd.h:
//  - SNDPLATFORM_init não carrega os IRX, não cria a thread nem o semáforo e não liga o RPC (o PS2: sceSifLoadModule,
//    CreateThread do SNDDRV_thread, sceSifBindRpc 'SND'); o bloco de iniciação vai direto ao snddrv_iop_rpc;
//  - a memória do IOP é a do IOP portado (snddrv_iop_ram, alocada pelo modelo do kernel do IOP, snddrv_iop_alloc); o
//    DMA do SIF para ela (SNDDRVI_dmeetoiopxfer) é um memcpy;
//  - só o caminho sem DTS (o bit 1 de kOutputFlags; a saída estéreo do jogo não o usa) está portado; pedido, conta em
//    snd_guards().

#include <cstring>

#include "driving/sound/snd.h"
#include "driving/sound/snd_mix.h"
#include "driving/sound/snd_stream.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/snddrv_iop.h"
#include "driving/sound/snd_spu.h"
#include "platform/ps2_float.h"

namespace drv {

namespace {

SndState& S() { return snd_state(); }
uint8_t& o8(int off) { return S().opts[off]; }
uint16_t o16(int off) {
    uint16_t v;
    std::memcpy(&v, &S().opts[off], 2);
    return v;
}
void o16(int off, uint16_t v) { std::memcpy(&S().opts[off], &v, 2); }
void o32(int off, uint32_t v) { std::memcpy(&S().opts[off], &v, 4); }

SndVoice& voice(int32_t v) { return S().voices[v]; }
uint8_t* tb() { return S().tagbuf.data(); }
uint32_t tbr32(uint32_t off) {
    uint32_t v;
    std::memcpy(&v, tb() + off, 4);
    return v;
}
void tbw32(uint32_t off, uint32_t v) { std::memcpy(tb() + off, &v, 4); }
void tbw16(uint32_t off, uint16_t v) { std::memcpy(tb() + off, &v, 2); }
uint64_t tbr64(uint32_t off) {
    uint64_t v;
    std::memcpy(&v, tb() + off, 8);
    return v;
}
void tbw64(uint32_t off, uint64_t v) { std::memcpy(tb() + off, &v, 8); }

// As vozes na tagbuf: as da SPU2 em +0x20 (8 bytes: endereço | geração, pitch, volume esquerdo e direito), as do IOP
// em +0x1a0 (0x10 bytes), a partir dos dados das vozes (S().tag_voices).
uint32_t spu_entry(int32_t v) { return S().tag_voices + 0x20 + uint32_t(v) * 8; }
uint32_t iop_entry(int32_t v) { return S().tag_voices + 0x1a0 + uint32_t(v - o8(sndopt::kSpuVoices)) * 0x10; }
// O índice da voz no mixador do EE.
int32_t mix_index(int32_t v) { return v - (int32_t(o8(sndopt::kSpuVoices)) + o8(sndopt::kIopVoices)); }


// [driving] 0x0035df98 a área de trabalho de cada tipo de reverb da SPU2 (bytes), lida pelo SNDDRV_setspufx.
constexpr uint32_t kSpuFxArea[10] = {0x10,    0x226c0, 0x21f40, 0x24840, 0x26fe0,
                                     0x2ade0, 0x2f6c0, 0x38040, 0x38040, 0x23c00};

}  // namespace

// ---- as opções ----------------------------------------------------------------------------------------------------

// [driving] 0x0030cf90 SNDPLATFORM_outputcaps(void)
int32_t SNDPLATFORM_outputcaps() {
    o16(0x00, 24000);
    o8(0x06) = 0x20;
    o8(0x07) = 0x10;
    o8(0x05) = 5;
    o8(0x13) = 6;
    o16(0x1a, 0x48);
    o16(sndopt::kModes + 0, 0x28);
    o16(sndopt::kModes + 2, 0x24);
    o16(sndopt::kModes + 4, 0x120);
    o8(sndopt::kSpuVoices) = 0x30;
    o8(sndopt::kOutputMode) = 2;
    o8(0x08) = 0x30;
    o16(0x14, 0x28);
    o16(0x1c, 0x44);
    o16(0x1e, 0x140);
    o8(sndopt::kModeCount) = 3;
    o8(0x47) = 8;
    o16(sndopt::kIopRate, 48000);
    o8(sndopt::kLoadModules) = 1;
    // O caminho dos IRX: o endereço da cadeia "host0:" no PS2 (0x003c7418, lido de 0x00357d80); o port não carrega
    // módulos e nunca o lê, mas as opções ficam iguais às do original.
    o32(sndopt::kModulePath, 0x003c7418);
    o8(0x0c) = 0;
    o16(0x02, 48000);
    o8(0x04) = 1;
    o8(sndopt::kOutputFlags) = 1;
    o16(0x16, 0x24);
    o16(0x18, 0x120);
    o8(0x40) = 0;
    o8(0x3e) = 0;
    o8(sndopt::kMixVoices) = 8;
    o8(sndopt::kIopVoices) = 8;
    o16(sndopt::kMixRate, 48000);
    o8(sndopt::kIopFx) = 0;
    o8(sndopt::kResample) = 0x19;
    o16(0xaa, 0x2000);
    o16(0xac, 0x6000);
    o16(0xae, 0xa000);
    o16(0x9c, 0x2000);
    o16(0x9e, 0x6000);
    o16(0xa0, 0xa000);
    o16(0xb0, 0xe000);
    o8(sndopt::kFxLevel) = 0x19;
    o16(0x84, 0x4000);
    o16(0x86, 0xc000);
    o16(0x90, 0x2aaa);
    o16(0x92, 0x8000);
    o16(0x94, 0xd555);
    o16(0x78, 0);
    o16(0xa2, 0xe000);
    o16(0xa8, 0);
    return 0;
}

// [driving] 0x0030d120 SNDPLATFORM_outputset(void): ajusta o que se pode mudar às capacidades.
int32_t SNDPLATFORM_outputset() {
    uint8_t set[0xe8];
    std::memcpy(set, S().opts + 0x20, 0xe8);
    // Os campos de "set" pelo deslocamento nas opções.
    auto s8 = [&](int off) -> uint8_t& { return set[off - 0x20]; };
    auto s16 = [&](int off) {
        uint16_t v;
        std::memcpy(&v, &set[off - 0x20], 2);
        return v;
    };
    auto s16w = [&](int off, uint16_t v) { std::memcpy(&set[off - 0x20], &v, 2); };
    if (S().inited != 0) {
        std::memcpy(S().opts + 0x20, S().saved, 0xe8);
        // Depois da iniciação, mudar o número de vozes não vale: só se as bandeiras de saída forem as mesmas.
        if (S().saved[sndopt::kOutputFlags - 0x20] != s8(sndopt::kOutputFlags)) return 0;
    }
    auto rate = [](uint16_t r) -> uint16_t { return r < 0x5dc1 ? 24000 : (r < 0x8ca1 ? 36000 : 48000); };
    s16w(sndopt::kMixRate, rate(s16(sndopt::kMixRate)));
    s16w(sndopt::kIopRate, rate(s16(sndopt::kIopRate)));
    if ((s8(sndopt::kOutputFlags) & 2) != 0) {
        s8(sndopt::kSpuVoices) = 0x18;
        if (s8(sndopt::kOutputMode) < 4) s8(sndopt::kOutputMode) = 4;
    }
    if ((s8(sndopt::kOutputFlags) & 1) != 0 && 2 < s8(sndopt::kOutputMode)) s8(sndopt::kOutputMode) = 2;
    if (100 < s8(sndopt::kDtsParam)) s8(sndopt::kDtsParam) = 100;
    uint8_t mode = o8(sndopt::kCapsModeMin);
    if (o8(sndopt::kCapsModeMin) <= s8(sndopt::kOutputMode)) {
        mode = o8(sndopt::kCapsModeMax);
        if (s8(sndopt::kOutputMode) <= o8(sndopt::kCapsModeMax)) mode = s8(sndopt::kOutputMode);
    }
    s8(sndopt::kOutputMode) = mode;
    s8(sndopt::kExtraOutputs) = mode == 5 ? 1 : 0;
    if (o8(sndopt::kCapsMixVoices) < s8(sndopt::kMixVoices)) s8(sndopt::kMixVoices) = o8(sndopt::kCapsMixVoices);
    if (o8(sndopt::kCapsIopVoices) < s8(sndopt::kIopVoices)) s8(sndopt::kIopVoices) = o8(sndopt::kCapsIopVoices);
    if (o8(sndopt::kCapsSpuVoices) < s8(sndopt::kSpuVoices)) s8(sndopt::kSpuVoices) = o8(sndopt::kCapsSpuVoices);
    if (0x20 < s8(0x47)) s8(0x47) = 0x20;
    MIX_setresamplequality(s8(sndopt::kResample));
    uint32_t dts_buf;
    std::memcpy(&dts_buf, S().opts + sndopt::kDtsBuf, 4);
    uint32_t sb, ss;
    std::memcpy(&sb, &set[sndopt::kDtsBuf - 0x20], 4);
    std::memcpy(&ss, &set[sndopt::kDtsSize - 0x20], 4);
    if (dts_buf != 0 && (sb & 0xf) != 0) {
        sb = (sb + 0xf) & ~0xfu;
        ss -= 0x10;
        std::memcpy(&set[sndopt::kDtsBuf - 0x20], &sb, 4);
        std::memcpy(&set[sndopt::kDtsSize - 0x20], &ss, 4);
    }
    if (S().inited == 0) {
        S().num_voices = int16_t(uint16_t(s8(sndopt::kSpuVoices)) + s8(sndopt::kMixVoices) + s8(sndopt::kIopVoices));
        std::memcpy(S().opts + 0x20, set, 0xe8);
    } else {
        o8(sndopt::kResample) = s8(sndopt::kResample);
        o8(sndopt::kFxLevel) = s8(sndopt::kFxLevel);
        tb()[S().tag_voices + 0x13] = s8(sndopt::kFxLevel);
        if (S().saved[sndopt::kOutputMode - 0x20] != s8(sndopt::kOutputMode)) {
            o8(sndopt::kOutputMode) = s8(sndopt::kOutputMode);
            for (int32_t v = 0; v < S().num_voices; ++v)
                if (voice(v).in_use == 1 && voice(v).master < 0) SNDPLATFORM_set3dpos(v);
        }
    }
    return 0;
}

// [driving] 0x0030cf18 SNDPLATFORM_getvoicerange(int, int *, int *)
void SNDPLATFORM_getvoicerange(uint32_t render, int32_t* first, int32_t* end) {
    const int32_t spu = o8(sndopt::kSpuVoices), iop = o8(sndopt::kIopVoices);
    if ((render & 8) != 0) {
        *first = 0;
        *end = spu;
    } else if ((render & 0x100) != 0) {
        *first = spu;
        *end = spu + iop;
    } else if ((render & 4) != 0) {
        *first = spu + iop;
        *end = S().num_voices;
    }
}

// ---- a tagbuf -----------------------------------------------------------------------------------------------------

// [driving] 0x0030d998 SNDDRV_resettagbuf(void)
static void SNDDRV_resettagbuf() {
    std::memset(tb(), 0, 0x2c00);
    const uint32_t frames = (uint32_t(o16(sndopt::kMixRate)) << 9) / 48000;
    tbw32(0x14, 0x2a0);
    S().tag_head = 4;
    tbw32(0x10, 3);
    S().tag_voices = 0x18;
    S().tag_len = 0x2b8;
    tbw32(0x00, 0);
    do {
        S().tag_len = uint16_t(S().tag_len + 1);
        tbw32(0x14, tbr32(0x14) + 1);
    } while ((S().tag_len & 0xf) != 0);
    if (o8(sndopt::kIopFx) != 0) {
        tbw32(S().tag_len, 2);
        S().tag_len = uint16_t(S().tag_len + 0x10);
        MIX_setwetbuffer(tb() + S().tag_len);
        S().tag_len = uint16_t(S().tag_len + frames * 4);
    }
    tbw32(S().tag_len, 1);
    S().tag_len = uint16_t(S().tag_len + 4);
    const uint32_t size_at = S().tag_len;
    S().tag_len = uint16_t(S().tag_len + 0xc);
    const uint32_t outputs = uint32_t(o8(sndopt::kOutputMode)) + o8(sndopt::kExtraOutputs);
    for (uint32_t i = 0, at = S().tag_len; i < outputs; ++i, at += frames * 2) S().mix_out[i] = at;
    uint16_t size = uint16_t(frames * outputs * 2);
    tbw16(size_at, size);
    if (o8(sndopt::kOutputMode) == 1) {
        size = uint16_t(size + frames * 2);
        tbw16(size_at, size);
        S().mix_out[1] = S().tag_len + frames * 2;
    }
    S().tag_len = uint16_t(S().tag_len + size);
    if ((o8(sndopt::kOutputFlags) & 2) != 0) {
        tbw32(S().tag_len, 4);
        S().tag_len = uint16_t(S().tag_len + 0x810);
    }
    tbw32(S().tag_len, 5);
    S().tag_len = uint16_t(S().tag_len + 4);
    S().tag_tail = S().tag_len;
    tbw32(S().tag_len + 0xc, 6);
    S().tag_len = uint16_t(S().tag_len + 0x1c);
    tbw32(S().tag_head + 4, S().tag_len);
}

// ---- as transferências ao IOP --------------------------------------------------------------------------------------

static void SNDDRV_dmservice(int32_t in_interrupt);

// [driving] 0x00310898 SNDDRVI_dmeetoiopxfer(unsigned int, unsigned int, int, int): o DMA do SIF para a memória do IOP.
static void SNDDRVI_dmeetoiopxfer(const uint8_t* src, uint32_t dst, uint32_t size) {
    uint8_t* d = snddrv_iop_ram(dst, size);
    if (d == nullptr) {
        snd_guard();
        return;
    }
    if (size != 0) std::memcpy(d, src, size);
}

// [driving] 0x003103a8 SNDDRV_dmtransfer(int): o pedido de maior prioridade (o mais velho no empate) vai ao IOP.
static void SNDDRV_dmtransfer(int32_t /*in_interrupt*/) {
    int32_t seen = 0;
    uint32_t best = 0xffffffffu;
    uint8_t best_prio = 0;
    for (int i = 0; i < 0x1c; ++i) {
        const SndDmaRequest& r = S().dma[i];
        if (r.id == 0) continue;
        ++seen;
        if (best_prio < r.priority) {
            best = r.id;
            best_prio = r.priority;
            S().dma_current = uint8_t(i);
        } else if (r.priority == best_prio && r.id < best) {
            best = r.id;
            S().dma_current = uint8_t(i);
        }
        if (!(seen < S().dma_count)) break;
    }
    const SndDmaRequest& r = S().dma[int8_t(S().dma_current)];
    SNDDRVI_dmeetoiopxfer(r.src, S().iop_block, r.size);
    S().dma_busy = 1;
    // sceSifSendCmd(0, {1, id, bloco do IOP, destino, tamanho, prioridade}): o SNDIOP_cmdhandler do outro lado.
    snddrv_iop_cmd(snddrv_iop_ram(S().iop_block, r.size), r.dst, r.size, r.priority, int32_t(r.id));
}

// [driving] 0x00310520 SNDDRV_dmservice(int)
static void SNDDRV_dmservice(int32_t in_interrupt) {
    if (S().dma_busy == 0 && 0 < S().dma_count) SNDDRV_dmtransfer(in_interrupt);
}

// [driving] 0x003105a8 SNDDRV_dmqueue
static int32_t SNDDRV_dmqueue(int32_t where, const uint8_t* src, uint32_t dst, uint32_t size, uint8_t priority) {
    if ((size & 0x3f) != 0) size = (size + 0x40) & ~0x3fu;
    S().dma_id += 1;
    if (S().dma_id == 0) S().dma_id = 1;
    if (where == 0x100) {
        // A memória do IOP (a parte do IOP de um banco): o DMA do SIF direto, sem a fila.
        SNDDRVI_dmeetoiopxfer(src, dst, size);
        return S().dma_id;
    }
    for (int i = 0; i < 0x1c; ++i) {
        SndDmaRequest& r = S().dma[i];
        if (r.id != 0) continue;
        S().dma_count = int8_t(S().dma_count + 1);
        r.size = uint16_t(size);
        r.priority = priority;
        r.id = uint32_t(S().dma_id);
        r.src = src;
        r.dst = dst;
        SNDDRV_dmservice(0);
        return S().dma_id;
    }
    return 0;
}

// [driving] 0x003106c8 SNDDRV_dmqueuesplit
static int32_t SNDDRV_dmqueuesplit(int32_t where, const uint8_t* src, uint32_t dst, int32_t size, uint8_t priority) {
    int32_t id = 0;
    while (0 < size) {
        const int32_t part = 0xfff < size ? 0x1000 : size;
        while (0x1c - S().dma_count < 5) SNDDRV_dmservice(0);
        id = SNDDRV_dmqueue(where, src, dst, uint32_t(part), priority);
        src += part;
        dst += uint32_t(part);
        size -= part;
    }
    return id;
}

// [driving] 0x003107e8 SNDDRV_dmiscomplete
static int32_t SNDDRV_dmiscomplete(int32_t id) {
    if (!(0 < S().dma_count)) return 1;
    for (int i = 0; i < 0x1c; ++i)
        if (int32_t(S().dma[i].id) == id) return 0;
    return 1;
}

// [driving] 0x00310838 SNDDRVI_dmcompleterequest: o IOP acabou a transferência id (o comando 1, tipo 2).
static void SNDDRVI_dmcompleterequest(int32_t id) {
    for (int i = 0; i < 0x1c; ++i) {
        if (int32_t(S().dma[i].id) != id) continue;
        S().dma_busy = 0;
        S().dma_count = int8_t(S().dma_count - 1);
        S().dma[i].id = 0;
        SNDDRV_dmservice(1);
        return;
    }
}

// [driving] 0x00310368 SNDPLATFORM_download(int, void *, void *, int)
int32_t SNDPLATFORM_download(int32_t where, const uint8_t* src, uint32_t dst, int32_t size) {
    return SNDDRV_dmqueuesplit(where, src, dst, size, 0);
}

// [driving] 0x00310388 SNDPLATFORM_downloadcomplete(int)
int32_t SNDPLATFORM_downloadcomplete(int32_t id) { return SNDDRV_dmiscomplete(id); }

// ---- a RAM da SPU2 ------------------------------------------------------------------------------------------------

// [driving] 0x00310a88 SNDDRV_memconstrain
static void SNDDRV_memconstrain(uint32_t* start, int32_t* size) {
    uint32_t s = *start;
    if (int32_t(s) < int32_t(S().spu_lo)) {
        *start = S().spu_lo;
        *size -= int32_t(S().spu_lo - s);
        s = *start;
    }
    if (int32_t(S().spu_hi) < int32_t(s + uint32_t(*size))) *size = int32_t(S().spu_hi - s);
    if (int32_t(S().spu_top) < int32_t(s + uint32_t(*size))) *size = int32_t(S().spu_top - s);
}

// [driving] 0x00310b00 SNDPLATFORM_memalloc(int, int): blocos de 64 bytes entre spu_lo e spu_hi (o primeiro vão que
// cabe, na ordem dos endereços); o endereço em bytes, ou 0.
uint32_t SNDPLATFORM_memalloc(int32_t where, int32_t size) {
    if (where == 0x100) return snddrv_iop_alloc(uint32_t(size + 0x40));  // sceSifAllocIopHeap
    if (!(S().spu_count < o16(sndopt::kMaxBanks))) return 0;
    const int32_t units = (size + 0x3f) >> 6;
    uint32_t start = 0;
    int32_t room = 0;
    uint32_t* t = S().spu_table;
    auto rec_start = [&](int i) { return uint32_t(t[i] & 0xffff); };
    auto rec_size = [&](int i) { return uint32_t(t[i] >> 16); };
    int32_t i = 0;
    if (S().spu_count == 0) {
        room = int32_t(S().spu_hi) - int32_t(S().spu_lo);
        start = S().spu_lo;
        SNDDRV_memconstrain(&start, &room);
        if (room < units) return 0;
    } else {
        bool found = false;
        for (; i < S().spu_count; ++i) {
            if (i == 0) {
                start = S().spu_lo;
                room = int32_t(rec_start(0)) - int32_t(start);
            } else {
                start = rec_start(i - 1) + rec_size(i - 1);
                room = int32_t(rec_start(i)) - int32_t(start);
            }
            SNDDRV_memconstrain(&start, &room);
            if (units <= room) {
                for (int k = S().spu_count; i < k; --k) t[k] = t[k - 1];
                found = true;
                break;
            }
        }
        if (!found) {
            start = rec_start(i - 1) + rec_size(i - 1);
            room = int32_t(S().spu_hi) - int32_t(start);
            SNDDRV_memconstrain(&start, &room);
            if (room < units) return 0;
        }
    }
    t[i] = (start & 0xffff) | (uint32_t(units & 0xffff) << 16);
    S().spu_count = uint16_t(S().spu_count + 1);
    return start << 6;
}

// [driving] 0x00310d10 SNDPLATFORM_memfree(int, unsigned int)
int32_t SNDPLATFORM_memfree(int32_t where, uint32_t addr) {
    if (where == 0x100) return snddrv_iop_free(addr);  // sceSifFreeIopHeap
    uint32_t* t = S().spu_table;
    for (int i = 0; i < S().spu_count; ++i) {
        if ((t[i] & 0xffff) != (addr >> 6)) continue;
        S().spu_count = uint16_t(S().spu_count - 1);
        for (; i < S().spu_count; ++i) t[i] = t[i + 1];
        return 0;
    }
    return -8;
}

// ---- iniciação ----------------------------------------------------------------------------------------------------

// [driving] 0x0030cef8 SNDDRV_mixvoicefree(int): o mixador do EE acabou uma voz (o fim da amostra).
static void SNDDRV_mixvoicefree(int32_t mv) {
    const int32_t v = mv + o8(sndopt::kSpuVoices) + o8(sndopt::kIopVoices);
    // O PS2 marca a voz para o SNDDRV_updatevoices (o status em 0x003585d0 vale só para as da SPU2): a voz do EE é
    // liberada pelo SNDPLATFORM_stop do servidor; aqui igual: nada (o original também só devolve).
    (void)v;
}

// [driving] 0x0030f950 SNDPLATFORM_fxoutputbalance(int, int)
void SNDPLATFORM_fxoutputbalance(int32_t /*bus*/, uint32_t path) {
    if (S().tagbuf.empty() || S().tag_voices == 0 || (path & 8) == 0) return;
    if (S().bus_spu[0].mode == 1) {
        tbw16(S().tag_voices + 0x16, 0);
        tbw16(S().tag_voices + 0x14, 0);
        return;
    }
    const float l = ps2::ee::mul(S().bus_spu[0].balance[0], 32767.0f);
    tbw16(S().tag_voices + 0x16, uint16_t(int16_t(ps2::ee::cvt_w(ps2::ee::mul(S().bus_spu[0].balance[1], 32767.0f)))));
    tbw16(S().tag_voices + 0x14, uint16_t(int16_t(ps2::ee::cvt_w(l))));
}

// [driving] 0x0030ddf8 SNDPLATFORM_init(void)
int32_t SNDPLATFORM_init() {
    S().dts_extra = 0;
    SNDSYS_entercritical();
    S().spu_table = static_cast<uint32_t*>(SNDMEMI_allocz(int32_t(o16(sndopt::kMaxBanks)) << 2));
    if (o16(sndopt::kMixRate) == 24000) S().frames_tick = 0x100;
    else S().frames_tick = o16(sndopt::kMixRate) == 36000 ? 0x180 : 0x200;
    SNDDRV_resettagbuf();
    tbw64(S().tag_voices + 8, 0xffffffffffffull);
    if ((o8(sndopt::kOutputFlags) & 2) != 0) snd_guard();  // DTS: não portado
    SNDPLATFORM_fxoutputbalance(0, 8);
    tb()[S().tag_voices + 0x13] = o8(sndopt::kFxLevel);
    S().spu_lo = 0x141;
    S().spu_hi = 0x8000;
    SNDSYS_leavecritical();
    // (o semáforo, o RPC, os IRX, o bloco do IOP e a thread do PS2)
    S().iop_block = snddrv_iop_alloc(0x1000);  // sceSifAllocIopHeap(0x1000): o bloco das transferências
    SNDDRV_setfx(8);
    SNDDRV_setfx(4);
    for (int i = 0; i < o8(sndopt::kModeCount); ++i) {
        if ((o16(sndopt::kModes + i * 2) & 4) == 0) continue;
        SndMixCreate p;
        p.outputs = uint8_t(o8(sndopt::kOutputMode) + o8(sndopt::kExtraOutputs));
        p.voices = o8(sndopt::kMixVoices);
        p.rate = o16(sndopt::kMixRate);
        p.voice_free = SNDDRV_mixvoicefree;
        p.quality = 0;
        MIX_create(p);
        break;
    }
    // O bloco de iniciação do IOP (0x14 bytes, SND 7.42.0).
    uint8_t block[0x14] = {};
    const uint32_t version = 0x72a00, status = 0x003585c0, dts = 0;
    std::memcpy(block + 0x00, &version, 4);
    std::memcpy(block + 0x04, &status, 4);
    std::memcpy(block + 0x08, &dts, 4);
    const uint16_t ee_rate = o16(sndopt::kMixRate), iop_rate = o16(sndopt::kIopRate);
    std::memcpy(block + 0x0c, &ee_rate, 2);
    std::memcpy(block + 0x0e, &iop_rate, 2);
    block[0x10] = o8(sndopt::kOutputMode);
    block[0x11] = o8(sndopt::kExtraOutputs);
    block[0x12] = o8(sndopt::kIopVoices);
    block[0x13] = o8(sndopt::kOutputMode) < 4 ? 0 : o8(sndopt::kDtsChannels);
    snddrv_set_dma_done(SNDDRVI_dmcompleterequest);
    snddrv_iop_rpc(0, block, 0x14);
    S().platform_up = 1;
    S().iop_up = 1;
    return 0;
}

// [driving] 0x0030e430 SNDPLATFORM_restore(void)
int32_t SNDPLATFORM_restore() {
    snddrv_iop_rpc(1, nullptr, 0);
    snddrv_iop_free(S().iop_block);  // sceSifFreeIopHeap
    MIX_setwetbuffer(nullptr);
    S().platform_up = 0;
    MIX_destroy();
    SNDMEMI_free(S().spu_table);
    return 0;
}

// ---- as vozes -----------------------------------------------------------------------------------------------------

// [driving] 0x0030e5d8 SNDPLATFORM_stop(int)
int32_t SNDPLATFORM_stop(int32_t v) {
    SndVoice& s = voice(v);
    const int n = s.channels;
    if ((s.render & 8) != 0) {
        for (int i = 0; i < n; ++i) {
            const int16_t c = s.channels_v[i];
            tbw32(spu_entry(c), 0);
            SNDVOICEI_free(c);
            if (o8(sndopt::kOutputFlags) == 2) tbw32(spu_entry(c + 0x18), 0);
        }
    } else if ((s.render & 0x100) != 0) {
        for (int i = 0; i < n; ++i) {
            tbw32(iop_entry(s.channels_v[i]), 0);
            SNDVOICEI_free(s.channels_v[i]);
        }
    } else {
        for (int i = 0; i < n; ++i) {
            const int16_t c = s.channels_v[i];
            MIX_stop(mix_index(c));
            SNDVOICEI_free(c);
        }
    }
    return 0;
}

// [driving] 0x0030e788 SNDPLATFORM_setpitch(int)
int32_t SNDPLATFORM_setpitch(int32_t v) {
    SndVoice& s = voice(v);
    if ((s.render & 8) != 0) {
        uint32_t p = (((uint32_t(s.sample_rate) << 12) / 48000) * s.pitch) >> 12;
        if (0x3fff < p) p = 0x3fff;
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            tbw16(spu_entry(c) + 4, uint16_t(p));
            if (o8(sndopt::kOutputFlags) == 2) tbw16(spu_entry(c + 0x18) + 4, uint16_t(p));
        }
    } else if ((s.render & 0x100) != 0) {
        const uint32_t rate = o16(sndopt::kIopRate);
        const uint32_t p = (((uint32_t(s.sample_rate) << 12) / rate) * s.pitch) >> 12;
        for (int i = 0; i < s.channels; ++i) tbw16(iop_entry(s.channels_v[i]) + 4, uint16_t(p));
    } else {
        const uint32_t rate = o16(sndopt::kMixRate);
        const uint32_t p = (((uint32_t(s.sample_rate) << 12) / rate) * s.pitch) >> 8;
        for (int i = 0; i < s.channels; ++i) MIX_setpitch(mix_index(s.channels_v[i]), p);
    }
    return 0;
}

// [driving] 0x0030e9a0 SNDPLATFORM_setfxlevel(int, int)
int32_t SNDPLATFORM_setfxlevel(int32_t v, int32_t bus) {
    SndVoice& s = voice(v);
    if ((s.render & 8) != 0) {
        const int16_t send = s.fx_send;
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            uint64_t bits = tbr64(S().tag_voices);
            if (send == 0) bits &= ~(1ull << c);
            else bits |= 1ull << c;
            tbw64(S().tag_voices, bits);
            if (o8(sndopt::kOutputFlags) == 2) {
                bits = tbr64(S().tag_voices);
                if (send == 0) bits &= ~(1ull << (c + 0x18));
                else bits |= 1ull << (c + 0x18);
                tbw64(S().tag_voices, bits);
            }
        }
        return 0;
    }
    const int8_t vol = s.vol;
    const int16_t send = (&s.fx_send)[bus];
    if ((s.render & 0x100) != 0) {
        const float g = ps2::ee::mul(ps2::ee::mul(ps2::ee::cvt_s(vol), ps2::ee::cvt_s(send)), 3.051851e-05f);
        for (int i = 0; i < s.channels; ++i)
            tb()[iop_entry(s.channels_v[i]) + 0xc + uint32_t(bus)] = uint8_t(ps2::ee::cvt_w(g));
    } else {
        const float g = ps2::ee::mul(ps2::ee::mul(ps2::ee::cvt_s(vol), ps2::ee::cvt_s(send)), 2.4030322e-07f);
        for (int i = 0; i < s.channels; ++i) SNDMIX_setwetgain(g, mix_index(s.channels_v[i]), bus);
    }
    return 0;
}

// [driving] 0x0030ec40 SNDPLATFORM_setvol(int)
void SNDPLATFORM_setvol(int32_t v) {
    SndVoice& s = voice(v);
    if ((s.render & 8) != 0) {
        const int32_t vol = s.vol;
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            const SndPlatformVoice& p = S().pvoice[c];
            if (o8(sndopt::kOutputFlags) == 2) {
                if (o8(sndopt::kOutputMode) == 4) {
                    tb()[spu_entry(c) + 6] = uint8_t((p.gain[3] * vol) >> 15);
                    tb()[spu_entry(c) + 7] = uint8_t((p.gain[0] * vol) >> 15);
                    tb()[spu_entry(c + 0x18) + 6] = uint8_t((p.gain[2] * vol) >> 15);
                    tb()[spu_entry(c + 0x18) + 7] = uint8_t((p.gain[1] * vol) >> 15);
                } else {
                    tb()[spu_entry(c) + 6] = uint8_t((p.gain[4] * vol) >> 15);
                    tb()[spu_entry(c) + 7] = uint8_t((p.gain[1] * vol) >> 15);
                    tb()[spu_entry(c + 0x18) + 6] = uint8_t((p.gain[3] * vol) >> 15);
                    tb()[spu_entry(c + 0x18) + 7] = uint8_t((p.gain[2] * vol) >> 15);
                }
            } else {
                tb()[spu_entry(c) + 6] = uint8_t((p.gain[1] * vol) >> 15);
                tb()[spu_entry(c) + 7] = uint8_t((p.gain[0] * vol) >> 15);
            }
        }
        return;
    }
    const int32_t outputs = int32_t(o8(sndopt::kOutputMode)) + o8(sndopt::kExtraOutputs);
    const float base = ps2::ee::mul(ps2::ee::cvt_s(s.vol), ps2::ee::cvt_s(s.prog_vol));
    if ((s.render & 0x100) != 0) {
        const float k = ps2::ee::mul(base, 2.4030322e-07f);
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            for (int o = 0; o < outputs; ++o)
                tb()[iop_entry(c) + 6 + uint32_t(o)] =
                    uint8_t(ps2::ee::cvt_w(ps2::ee::mul(k, ps2::ee::cvt_s(S().pvoice[c].gain[o]))));
        }
    } else {
        const float k = ps2::ee::mul(base, 1.8921513e-09f);
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            for (int o = 0; o < outputs; ++o)
                SNDMIX_setdrygain(ps2::ee::mul(k, ps2::ee::cvt_s(S().pvoice[c].gain[o])), mix_index(c), o);
        }
    }
}

// [driving] 0x0030f0c8 SNDPLATFORM_setdrylevel(int)
void SNDPLATFORM_setdrylevel(int32_t v) {
    SndVoice& s = voice(v);
    if ((s.render & 8) == 0) {
        SNDPLATFORM_setvol(v);
        return;
    }
    for (int i = 0; i < s.channels; ++i) {
        const int16_t c = s.channels_v[i];
        uint64_t bits = tbr64(S().tag_voices + 8);
        if (s.prog_vol == 0) bits &= ~(1ull << c);
        else bits |= 1ull << c;
        tbw64(S().tag_voices + 8, bits);
        if (o8(sndopt::kOutputFlags) == 2) {
            bits = tbr64(S().tag_voices + 8);
            if (s.prog_vol == 0) bits &= ~(1ull << (c + 0x18));
            else bits |= 1ull << (c + 0x18);
            tbw64(S().tag_voices + 8, bits);
        }
    }
}

// [driving] 0x0030f9c0 SNDI_dolby(int, short *, short *, int, int)
static void SNDI_dolby(int32_t azimuth, int16_t* a, int16_t* b, int32_t prev_a, int32_t prev_b) {
    uint32_t q;
    if (prev_a < 0) {
        if (prev_b < 0) {
            q = prev_a < prev_b ? 1 : 2;
        } else {
            const int32_t m = prev_a < 0 ? -prev_a : prev_a;
            q = m <= prev_b ? 3 : 4;
        }
    } else if (prev_b < 0) {
        const int32_t m = prev_a;
        q = m <= -prev_b ? 3 : 4;
    } else {
        q = prev_a < prev_b ? 2 : 1;
    }
    int32_t flip = 0;
    const int16_t va = *a;
    if (uint32_t(azimuth - 0x4000) < 0x8000) {
        const int32_t sect = *a < *b ? 3 : 4;
        if (2 < q || sect != 3) goto done;
        q ^= 2;
    } else {
        const int32_t sect = *a < *b ? 2 : 1;
        if (q < 3) goto done;
        q ^= 3;
        if (sect != 2) {
            flip = 2;
            goto done;
        }
    }
    flip = q == 0 ? 1 : 2;
done:
    if (prev_a < 0) *a = int16_t(-va);
    if (prev_b < 0) *b = int16_t(-*b);
    if (flip == 1) *a = int16_t(-*a);
    else if (flip == 2) *b = int16_t(-*b);
}

// [driving] 0x0030fb18 SNDPLATFORM_set3dpos(int): os ganhos por alto-falante de cada canal.
void SNDPLATFORM_set3dpos(int32_t v) {
    SndVoice& s = voice(v);
    const int mode = o8(sndopt::kOutputMode);
    if (mode == 1) {
        for (int i = 0; i < s.channels; ++i) {
            S().pvoice[s.channels_v[i]].gain[0] = 0x6000;
            S().pvoice[s.channels_v[i]].gain[1] = 0x6000;
        }
        if (s.channels == 6) {
            S().pvoice[s.channels_v[5]].gain[0] = 0;
            S().pvoice[s.channels_v[5]].gain[1] = 0;
        }
        SNDPLATFORM_setvol(v);
        return;
    }
    SndPlatformVoice& pv = S().pvoice[v];
    if (s.channels == 1) {
        if (o8(sndopt::kExtraOutputs) != 0) pv.gain[mode] = 0;
        SNDI_aztospkrvol(s.azimuth, pv.gain);
        if ((s.render & 0x40) == 0) {
            if (mode == 5 && (s.render & 8) != 0) {
                int16_t back[6];
                SNDI_aztospkrvol(uint32_t(int16_t(s.azimuth) - 0x8000), back);
                pv.gain[4] = back[2];
                pv.gain[1] = back[3];
            }
        } else {
            SNDI_dolby(int16_t(s.azimuth), &pv.gain[1], &pv.gain[0], pv.dolby_prev[0], pv.dolby_prev[1]);
            pv.dolby_prev[0] = pv.gain[1];
            pv.dolby_prev[1] = pv.gain[0];
        }
        SNDPLATFORM_setvol(v);
        return;
    }
    // Vários canais: o azimute de cada um mais o deslocamento da linha da tabela (azimute, ganho), escalado pelo ganho.
    // As linhas do .data: 0x0035dfc0 (o padrão), 0x0035dfd8 e 0x0035dff0 (seis canais, sem e com o bit 0x40),
    // 0x0035e008 e 0x0035e018 (quatro canais).
    static const int16_t kRows[5][6][2] = {
        {{0, 32767}, {0, 32767}, {0, 32767}, {0, 32767}, {0, 32767}, {0, 32767}},
        {{-8192, 32767}, {0, 32767}, {8192, 32767}, {8192, 32767}, {-8192, 32767}, {0, 0}},
        {{-8192, 32767}, {0, 32767}, {8192, 32767}, {-8192, 32767}, {8192, 32767}, {0, 0}},
        {{-8192, 32767}, {8192, 32767}, {8192, 32767}, {-8192, 32767}, {0, 0}, {0, 0}},
        {{-8192, 32767}, {8192, 32767}, {-8192, 32767}, {8192, 32767}, {0, 0}, {0, 0}},
    };
    int row = 0;
    if (mode == 2) {
        const bool dolby = (s.render & 0x40) != 0;
        if (s.channels == 6) row = dolby ? 2 : 1;
        else if (s.channels == 4) row = dolby ? 4 : 3;
    }
    const int16_t (*t)[2] = kRows[row];
    for (int i = 0; i < s.channels;) {
        int next;
        SndPlatformVoice& cv = S().pvoice[s.channels_v[i]];
        if (i == 5 && o8(sndopt::kExtraOutputs) != 0) {
            int k = 0;
            for (; k < mode; ++k) cv.gain[k] = 0;
            cv.gain[k] = 0x7fff;
            next = 6;
        } else {
            SNDI_aztospkrvol(uint32_t(s.channel_azimuth[i]) + uint32_t(int32_t(t[i][0])), cv.gain);
            next = i + 1;
        }
        const int outputs = mode + o8(sndopt::kExtraOutputs);
        for (int k = 0; k < outputs; ++k) cv.gain[k] = int16_t((int32_t(cv.gain[k]) * t[i][1]) >> 15);
        if ((s.render & 0x40) != 0 &&
            uint32_t(uint32_t(s.channel_azimuth[i]) + uint32_t(int32_t(t[i][0])) - 0x4001u) < 0x7fffu)
            cv.gain[0] = int16_t(-cv.gain[0]);
        i = next;
    }
    SNDPLATFORM_setvol(v);
}

// [driving] 0x0030f380 SNDPLATFORM_lowpass(int, int)
void SNDPLATFORM_lowpass(int32_t v, int32_t cutoff) {
    SndVoice& s = voice(v);
    if ((s.render & 4) == 0) return;
    const float half = ps2::ee::mul(ps2::ee::cvt_s(int32_t(o16(sndopt::kMixRate))), 0.5f);
    const float k = ps2::ee::mul(ps2::ee::cvt_s(cutoff), ps2::ee::div(1.0f, half));
    for (int i = 0; i < s.channels; ++i) MIX_setlowpass(k, mix_index(s.channels_v[i]));
}

// [driving] 0x0030f218 SNDPLATFORM_timemult(int, int)
static void SNDPLATFORM_timemult(int32_t v, int32_t mult) {
    SndVoice& s = voice(v);
    if ((s.render & 4) == 0) return;
    for (int i = 0; i < s.channels; ++i) MIX_settimemult(mix_index(s.channels_v[i]), mult);
}

// [driving] 0x0030f2d0 SNDPLATFORM_setdistortlevel(int, int)
static void SNDPLATFORM_setdistortlevel(int32_t v, int32_t level) {
    SndVoice& s = voice(v);
    if ((s.render & 4) == 0) return;
    for (int i = 0; i < s.channels; ++i) MIX_setdistortlevel(mix_index(s.channels_v[i]), level);
}

// [driving] 0x0030f460 SNDPLATFORM_highpass(int, int)
static void SNDPLATFORM_highpass(int32_t v, int32_t cutoff) {
    SndVoice& s = voice(v);
    if ((s.render & 4) == 0) return;
    for (int i = 0; i < s.channels; ++i) MIX_sethighpass(mix_index(s.channels_v[i]), cutoff);
}

// [driving] 0x00319100 SNDPLATFORM_packetplay(int, int, int, int, int, int, SNDSAMPLEFORMAT *, int *): a voz de um
// tocador de pacotes no mixador do EE (o desempacotador de pacotes de cada canal; a SPU2 e o IOP não tocam pacotes).
int32_t SNDPLATFORM_packetplay(int32_t /*player*/, int32_t v, int32_t time_mult, int32_t distort, int32_t lowpass,
                               int32_t highpass, const SndSampleFormat* fmt, const nf::P32<uint8_t>* stretch) {
    SndVoice& s = voice(v);
    if ((s.render & 8) != 0 || (s.render & 4) == 0) return 0;
    for (int i = 0; i < s.channels; ++i)
        MIX_playinit(mix_index(s.channels_v[i]), fmt->codec, 0, nullptr, -1, nf::p32_u(nf::raw(stretch[i])),
                     s.channels, s.frames, -1, -1, 0, 0, uint8_t(i));
    SNDPLATFORM_setpitch(s.channels_v[0]);
    SNDPLATFORM_set3dpos(s.channels_v[0]);
    SNDPLATFORM_setfxlevel(s.channels_v[0], 0);
    SNDPLATFORM_timemult(s.channels_v[0], time_mult);
    SNDPLATFORM_setdistortlevel(s.channels_v[0], distort);
    SNDPLATFORM_lowpass(s.channels_v[0], lowpass);
    SNDPLATFORM_highpass(s.channels_v[0], highpass);
    for (int i = 0; i < s.channels; ++i) MIX_play(mix_index(s.channels_v[i]));
    return 0;
}

// [driving] 0x0030ff40 SNDPLATFORM_asyncresolvetimbre(SNDIPATCHHEADER *, char *, int *): troca o dado da etiqueta 0x8a
// do timbre (no banco) pelo endereço da amostra na SPU2.
int32_t SNDPLATFORM_asyncresolvetimbre(SndPatchHeader* h, uint32_t spu_base, int32_t* first) {
    if (*first == 0) *first = h->sample_offset[0];
    const int32_t a = int32_t(spu_base) + (h->sample_offset[0] - *first);
    std::memcpy(nf::raw(h->sample_data), &a, 4);
    return 0;
}

// [driving] 0x0030ff78 SNDPLATFORM_playtimbre(SNDIPATCHHEADER *, void *, int, int, int, int, int)
int32_t SNDPLATFORM_playtimbre(SndPatchHeader* h, uint8_t* bank, int32_t v, int32_t time_mult, int32_t distort,
                               int32_t lowpass, int32_t highpass) {
    SndVoice& s = voice(v);
    if ((s.render & 8) != 0) {
        SNDPLATFORM_setpitch(v);
        SNDPLATFORM_set3dpos(v);
        SNDPLATFORM_setdrylevel(v);
        SNDPLATFORM_setfxlevel(v, 0);
        int32_t base;
        std::memcpy(&base, nf::raw(h->sample_data), 4);
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            SndPlatformVoice& pv = S().pvoice[c];
            pv.spu_addr = uint32_t(base);
            if (i == 1) S().pvoice[s.channels_v[1]].spu_addr += uint32_t((s.frames << 4) / 0x1c);
            S().spu_gen += 0x800000;
            tbw32(spu_entry(c), pv.spu_addr | S().spu_gen);
            S().iop_status[4 + c] = pv.spu_addr;
            if (o8(sndopt::kOutputFlags) == 2) {
                tbw32(spu_entry(c + 0x18), pv.spu_addr | S().spu_gen);
                S().iop_status[4 + c + 0x18] = pv.spu_addr;
            }
        }
    } else if ((s.render & 0x100) != 0) {
        SNDPLATFORM_setpitch(v);
        SNDPLATFORM_set3dpos(v);
        SNDPLATFORM_setfxlevel(v, 0);
        int32_t base;
        std::memcpy(&base, nf::raw(h->sample_data), 4);
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            S().pvoice[c].spu_addr = uint32_t(base + (h->sample_offset[i] - h->sample_offset[0]));
            S().spu_gen += 0x800000;
            tbw32(iop_entry(c), S().pvoice[c].spu_addr | S().spu_gen);
            S().iop_status[0x34 + (c - o8(sndopt::kSpuVoices))] = 0;
        }
    } else {
        for (int i = 0; i < s.channels; ++i)
            MIX_playinit(mix_index(s.channels_v[i]), h->sample_rep, 1, bank + h->sample_offset[i], h->tag1a,
                         nf::p32_u(h->stretch[i]), s.channels, s.frames, 0, 0, 0, 0, uint8_t(i));
        SNDPLATFORM_setpitch(v);
        SNDPLATFORM_set3dpos(v);
        SNDPLATFORM_setfxlevel(v, 0);
        SNDPLATFORM_timemult(v, time_mult);
        SNDPLATFORM_setdistortlevel(v, distort);
        SNDPLATFORM_lowpass(v, lowpass);
        SNDPLATFORM_highpass(v, highpass);
        for (int i = 0; i < s.channels; ++i) MIX_play(mix_index(s.channels_v[i]));
    }
    return 0;
}

// ---- o reverb -----------------------------------------------------------------------------------------------------

// [driving] 0x0030f760 SNDDRV_setspufx(int): o tipo de reverb da SPU2 (o modo do barramento) e a área dele.
static void SNDDRV_setspufx(uint32_t mode) {
    uint32_t t;
    if (mode < 2) t = 0;
    else if (mode == 5) t = 1;
    else if (mode == 10) t = 2;
    else if (mode == 0x14) t = 3;
    else if (mode == 0x1e) t = 4;
    else if (mode == 0x28) t = 5;
    else if (mode == 0x32) t = 6;
    else if (mode == 100) t = 9;
    else if (mode == 0x6e) t = 8;
    else if (mode == 0x78) t = 7;
    else t = mode;
    const uint32_t area = t < 10 ? kSpuFxArea[t] : 0;
    if (!(t < 10)) snd_guard();
    S().spu_top = uint16_t((0x200000 - area) >> 6);
    tb()[S().tag_voices + 0x10] = uint8_t(t);
    tb()[S().tag_voices + 0x11] = uint8_t(S().bus_spu[0].delay);
    tb()[S().tag_voices + 0x12] = uint8_t(S().bus_spu[0].feedback);
}

// [driving] 0x0035df80 o descritor do reverb do mixador do IOP (o .data), que o SNDDRV_setiopfx manda pelo RPC 2.
constexpr uint8_t kIopReverb[0x18] = {70, 88, 0,  0,  4, 0, 0,  250, 0, 100, 64, 0,
                                      0,  70, 64, 95, 0, 49, 76, 0, 0,   34, 76, 95};

// [driving] 0x0030f650 SNDDRV_setiopfx(int): o reverb do mixador do IOP (o modo do barramento do IOP).
static void SNDDRV_setiopfx(uint32_t mode) {
    if (o8(sndopt::kIopFx) != 0) snd_guard();  // o efeito do IOP dentro do mixador do EE: nenhum modo do jogo o liga
    if (mode == 0) {
        snddrv_iop_rpc(2, kIopReverb, 4);
        return;
    }
    if (mode == 1) {
        // O modo 1 lê um descritor do usuário pelo ponteiro guardado no atraso do barramento (+0x04): o jogo não usa.
        snd_guard();
        return;
    }
    const uint8_t* d = kIopReverb;
    int32_t size = 0;
    if (d[2] == 0) size = (int32_t(d[4]) + d[5] + d[6]) * 4 + 8;
    else if (d[2] == 10) size = SNDI_getb(d + 4, 4);
    snddrv_iop_rpc(2, d, size);
}

// [driving] 0x0030f858 SNDDRV_setfx
void SNDDRV_setfx(uint32_t path) {
    SNDSYS_entercritical();
    if (path == 8) {
        SNDDRV_setspufx(S().bus_spu[0].mode);
    } else if (path == 0x100) {
        SNDDRV_setiopfx(S().bus_iop[0].mode);
    } else if (path == 4) {
        if (S().bus_ee[0].mode == 0 || o8(sndopt::kIopFx) != 0) {
            MIX_restorereverb();
        } else {
            MIX_initreverb(o16(sndopt::kMixRate), nullptr);
        }
    }
    SNDSYS_leavecritical();
}

// [driving] 0x0030f920 SNDPLATFORM_fxinit(int, int)
int32_t SNDPLATFORM_fxinit(int32_t /*bus*/, uint32_t path) {
    if (S().inited != 0) SNDDRV_setfx(path);
    return 0;
}

// ---- o tick -------------------------------------------------------------------------------------------------------

// [driving] 0x0030d610 SNDDRV_updatevoices(void): libera as vozes que o IOP disse que acabaram (o status de cada voz
// da SPU2 zerado; a do IOP -1), cinco ticks depois do key on.
void SNDDRV_updatevoices() {
    const int spu = o8(sndopt::kSpuVoices);
    for (int v = 0; v < spu; ++v) {
        if (tbr32(spu_entry(v)) == 0) continue;
        SndVoice& s = voice(v);
        if (s.master != -1 || S().iop_status[4 + v] != 0) continue;
        if (!(s.tick + 5 < S().tick) || s.channels == 0) continue;
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            tbw32(spu_entry(c), 0);
            SNDVOICEI_free(c);
            if (o8(sndopt::kOutputFlags) == 2) tbw32(spu_entry(c + 0x18), 0);
        }
    }
    const int iop = o8(sndopt::kIopVoices);  // (as do mixador do IOP)
    for (int k = 0; k < iop; ++k) {
        const int v = spu + k;
        if (tbr32(iop_entry(v)) == 0) continue;
        SndVoice& s = voice(v);
        if (s.master != -1 || S().iop_status[0x34 + k] != 0xffffffffu) continue;
        if (!(s.tick + 5 < S().tick) || s.channels == 0) continue;
        for (int i = 0; i < s.channels; ++i) {
            const int16_t c = s.channels_v[i];
            tbw32(iop_entry(c), 0);
            SNDVOICEI_free(c);
        }
    }
}

// [driving] 0x0030dc58 SNDDRV_thread (uma volta): o IOP processa a tagbuf do tick anterior e devolve o status; o EE
// anda o relógio, libera as vozes que acabaram, mistura as do EE e fecha a tagbuf seguinte.
void snd_driver_tick() {
    SndState& s = snd_state();
    if (s.platform_up == 0) return;
    snddrv_iop_tick(s.tagbuf.data(), s.iop_status);
    if (s.pre_frame != nullptr) s.pre_frame();
    SNDSYSI_timerservice(0x5e);
    SNDDRV_updatevoices();
    int16_t* outs[8] = {};
    for (int i = 0; i < 8; ++i)
        outs[i] = s.mix_out[i] != 0 ? reinterpret_cast<int16_t*>(s.tagbuf.data() + s.mix_out[i]) : nullptr;
    MIX_audio(outs, s.frames_tick);
    if ((s.opts[sndopt::kOutputFlags] & 2) == 0 && s.opts[sndopt::kOutputMode] == 1)
        std::memcpy(s.tagbuf.data() + s.mix_out[1], s.tagbuf.data() + s.mix_out[0], size_t(s.frames_tick) * 2);
    SNDPKTPLAYI_flushcallbackdata();
    std::memcpy(s.tagbuf.data() + s.tag_head, &s.timer_count, 4);
    std::memcpy(s.tagbuf.data() + s.tag_tail, &s.timer_count, 4);
    if (s.post_frame != nullptr) s.post_frame();
}

}  // namespace drv
