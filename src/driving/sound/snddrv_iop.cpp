// O SNDDRV.IRX portado (driving/sound/snddrv_iop.h), do MIPS do módulo (MODULES/SNDDRV.IRX, base 0; as desmontagens em
// out/agent_drv_sound/ do checkout principal).
//
// Divergências (port), além das do cabeçalho:
//  - o mixador do IOP (MIX_*/SFILTER_* do módulo: as vozes de modo de render 0x100, amostras EA-XA na memória do IOP)
//    não está portado: ele não mistura voz nenhuma e devolve silêncio, como o original sem vozes, e cada tick com uma voz
//    do IOP pedida conta em snddrv_guards(). No disco são 249 timbres (os bancos h* dos personagens: os gritos e quedas
//    dos capangas e do Bond; os a* do ambiente e 3 de cada g*: na Paris o SFX_OffRN e o SFX_OffRoadSkid do carro, mudos
//    fora do asfalto); os outros 1.219 vão à SPU2;
//  - a memória do IOP (o SNDMEMI do módulo) são vetores daqui.

#include "driving/sound/snddrv_iop.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace drv {

namespace {

// Os números do libsd (o núcleo no bit 0, a voz x 2 nos bits 1..5).
constexpr uint16_t SD_VP_VOLL = 0x0000, SD_VP_VOLR = 0x0100, SD_VP_PITCH = 0x0200, SD_VP_ADSR1 = 0x0300,
                   SD_VP_ADSR2 = 0x0400, SD_VP_ENVX = 0x0500, SD_VP_VOLXL = 0x0600, SD_VP_VOLXR = 0x0700;
constexpr uint16_t SD_P_MMIX = 0x0800, SD_P_MVOLL = 0x0980, SD_P_MVOLR = 0x0a80, SD_P_EVOLL = 0x0b80,
                   SD_P_EVOLR = 0x0c80, SD_P_BVOLL = 0x0f80, SD_P_BVOLR = 0x1080;
constexpr uint16_t SD_S_KON = 0x1500, SD_S_KOFF = 0x1600, SD_S_VMIXL = 0x1800, SD_S_VMIXEL = 0x1900,
                   SD_S_VMIXR = 0x1a00, SD_S_VMIXER = 0x1b00;
constexpr uint16_t SD_A_ESA = 0x1c00, SD_A_EEA = 0x1d00, SD_VA_SSA = 0x2040, SD_VA_LSAX = 0x2140, SD_VA_NAX = 0x2240;
constexpr uint16_t SD_C_SPDIF = 0x000a;  // SetCoreAttr 10 (o modo do caminho da entrada)

// A tabela da rampa de volume (.data 0x0000b398): a taxa da varredura da SPU2 por |diferença| / 2 (128 entradas).
constexpr int8_t kVolRamp[128] = {
    0,  55, 51, 49, 47, 46, 45, 44, 43, 43, 42, 42, 41, 40, 40, 40, 39, 39, 39, 39, 37, 37, 37, 37, 37, 37,
    37, 37, 36, 36, 36, 36, 35, 35, 35, 35, 35, 35, 35, 35, 34, 34, 34, 34, 34, 34, 34, 34, 33, 33, 33, 33,
    33, 33, 33, 33, 32, 32, 32, 32, 32, 32, 32, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
    31, 31, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 29, 29, 29, 29, 29, 29, 29, 29,
    29, 29, 29, 29, 29, 29, 29, 29, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
};
// O laço silencioso (.data 0x0000b418): quatro blocos de PS-ADPCM com as bandeiras 7 (início, repete, fim), que o
// driver põe em 0x5000 da RAM da SPU2 e usa como o endereço de laço de todas as vozes.
uint8_t g_safe_loop[0x40] = {0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t g_zero_buf[0x1000] = {};  // sndiopzerobuf

struct DmaSlot {
    int32_t id = 0;          // +0x00
    int32_t ee_id = 0;       // +0x04
    const uint8_t* src = nullptr;  // +0x08
    uint32_t dst = 0;        // +0x0c
    uint16_t size = 0;       // +0x10
    int16_t priority = 0;    // +0x12
    uint8_t mode = 0;        // +0x14
};

struct IopVoice {            // o estado anterior de uma voz da SPU2 (0x0000b5a8, 8 bytes)
    uint32_t addr = 0;
    uint16_t pitch = 0;
    int8_t vol[2] = {};
};

struct Iop {
    SndLibsd* sd = nullptr;
    SndIopDmaDone dma_done = nullptr;
    // ioptoee (0x110 bytes): +0 a carga do IOP, +0x10 o NAX das 48 vozes (0x0000b838), +0xd0 as do mixador.
    uint32_t ioptoee[0x44] = {};
    uint8_t running = 0;          // 0x0000b97c
    uint8_t fx_state = 0;         // 0x0000b97d
    int8_t outputs = 0;           // 0x0000b97e (o modo de saída: 2)
    uint8_t dts = 0;              // 0x0000b97f
    int8_t extra = 0;             // 0x0000b980
    uint8_t iop_voices = 0;       // 0x0000b981
    uint8_t half = 0;             // 0x0000b982 a metade do buffer que a SPU2 toca
    int32_t out_size = 0;         // 0x0000b984
    uint16_t ee_rate = 0, iop_rate = 0;    // 0x0000b9f8, 0x0000b9fa
    uint16_t ee_frames = 0, iop_frames = 0;  // 0x0000b9fc, 0x0000b9fe
    uint32_t updates = 0;         // 0x0000b954
    uint32_t dry[2] = {}, wet[2] = {};       // 0x0000b590, 0x0000b588 os bits anteriores
    int8_t fx_type = 0;           // 0x0000b598
    uint8_t quality = 0;          // 0x0000b59b
    int16_t evol[2] = {};         // 0x0000b59c, 0x0000b59e
    uint32_t clear_top[2] = {};   // 0x0000b93c, 0x0000b940
    int32_t clear_id = 0;         // sndiop (a última transferência de zeros)
    IopVoice prev[48];            // 0x0000b5a8
    uint8_t ramping[96] = {};     // 0x0000b998
    uint8_t mix_on[16] = {};      // 0x0000b988
    std::vector<int16_t> out;     // o buffer da transferência em laço (0x0000ba20)
    std::vector<int32_t> mix;     // as saídas do mixador do IOP (0x0000ba18: 0x800 bytes cada)
    std::vector<uint8_t> tag;     // a cópia da tagbuf (0x0000e624)
    // A fila das transferências (0x00011250..)
    int32_t dma_id = 0;
    int8_t dma_count = 0;
    int8_t dma_current = 0;
    uint8_t dma_busy = 0;
    DmaSlot dma[0x20];
    uint64_t guards = 0;
    // A memória do IOP (o port: 2 MB) e o seu alocador.
    std::vector<uint8_t> ram;
    IopHeapModel heap;
    uint32_t rpc_count[3] = {};
    uint32_t rpc_hash = 0x811c9dc5u;
};
Iop g_iop;

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
int16_t rd16s(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint16_t rd16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

uint16_t vent(int v) { return uint16_t(((v % 24) * 2) | (v < 24 ? 0 : 1)); }

void SNDIOP_dmtransfer();

// [snddrv] 0x00005344 SNDIOP_dmcallback: a transferência acabou (a interrupção do libsd).
void SNDIOP_dmcallback() {
    DmaSlot& s = g_iop.dma[g_iop.dma_current];
    if (s.id != 0) {
        s.id = 0;
        g_iop.dma_count = int8_t(g_iop.dma_count - 1);
        if (s.ee_id != 0) {
            const int32_t ee = s.ee_id;
            s.ee_id = 0;
            if (g_iop.dma_done != nullptr) g_iop.dma_done(ee);  // sceSifSendCmd(1, {tipo 2, ee_id})
        }
        if (!(0 < g_iop.dma_count)) {
            g_iop.dma_busy = 0;
            return;
        }
    }
    SNDIOP_dmtransfer();
}

// [snddrv] 0x00005420 SNDIOP_dmtransfer
void SNDIOP_dmtransfer() {
    uint32_t best = 0xffffffffu;
    int16_t prio = 0;
    for (int i = 0; i < 0x20; ++i) {
        const DmaSlot& s = g_iop.dma[i];
        if (s.id == 0) continue;
        if (prio < s.priority) {
            best = uint32_t(s.id);
            prio = s.priority;
            g_iop.dma_current = int8_t(i);
        } else if (s.priority == prio && uint32_t(s.id) < best) {
            best = uint32_t(s.id);
            g_iop.dma_current = int8_t(i);
        }
    }
    g_iop.dma_busy = 1;
    const DmaSlot& s = g_iop.dma[g_iop.dma_current];
    g_iop.sd->VoiceTrans(1, s.mode != 0, s.src, s.dst, s.size);
    SNDIOP_dmcallback();  // no PC a transferência acaba na hora
}

// [snddrv] 0x00005558 SNDIOP_dmservice
void SNDIOP_dmservice() {
    if (g_iop.dma_busy == 0 && g_iop.dma_count != 0) SNDIOP_dmtransfer();
}

// [snddrv] 0x000055b4 SNDIOP_dmqueue
int32_t SNDIOP_dmqueue(int32_t ee_id, const uint8_t* src, uint32_t dst, uint16_t size, int16_t priority, uint8_t mode) {
    if ((size & 0x3f) != 0) size = uint16_t((size + 0x40) & 0xffc0);
    g_iop.dma_id += 1;
    if (g_iop.dma_id == 0) g_iop.dma_id = 1;
    for (int i = 0; i < 0x20; ++i) {
        DmaSlot& s = g_iop.dma[i];
        if (s.id != 0) continue;
        g_iop.dma_count = int8_t(g_iop.dma_count + 1);
        s.id = g_iop.dma_id;
        s.ee_id = ee_id;
        s.src = src;
        s.dst = dst;
        s.size = size;
        s.priority = priority;
        s.mode = mode;
        SNDIOP_dmservice();
        return g_iop.dma_id;
    }
    return 0;
}

// [snddrv] 0x000056e4 SNDIOP_dmcomplete
int32_t SNDIOP_dmcomplete(int32_t id) {
    if (!(0 < g_iop.dma_count)) return 1;
    for (int i = 0; i < 0x20; ++i)
        if (g_iop.dma[i].id == id) return 0;
    return 1;
}

// [snddrv] 0x00007a64 SNDIOP_keyon
void SNDIOP_keyon(uint32_t lo, uint32_t hi) {
    if ((lo & 0xffffff) != 0) g_iop.sd->SetSwitch(SD_S_KON, lo & 0xffffff);
    const uint32_t c1 = lo >> 24 | hi << 8;
    if (c1 != 0) g_iop.sd->SetSwitch(SD_S_KON | 1, c1);
}

// [snddrv] 0x00007ac8 SNDIOP_keyoff
void SNDIOP_keyoff(uint32_t lo, uint32_t hi) {
    if ((lo & 0xffffff) != 0) g_iop.sd->SetSwitch(SD_S_KOFF, lo & 0xffffff);
    const uint32_t c1 = lo >> 24 | hi << 8;
    if (c1 != 0) g_iop.sd->SetSwitch(SD_S_KOFF | 1, c1);
}

// [snddrv] 0x00007b2c SNDIOP_setdrybusbits
void SNDIOP_setdrybusbits(uint32_t lo, uint32_t hi) {
    g_iop.sd->SetSwitch(SD_S_VMIXL, lo & 0xffffff);
    g_iop.sd->SetSwitch(SD_S_VMIXR, lo & 0xffffff);
    const uint32_t c1 = lo >> 24 | hi << 8;
    g_iop.sd->SetSwitch(SD_S_VMIXL | 1, c1);
    g_iop.sd->SetSwitch(SD_S_VMIXR | 1, c1);
}

// [snddrv] 0x00007bac SNDIOP_setwetbusbits
void SNDIOP_setwetbusbits(uint32_t lo, uint32_t hi) {
    g_iop.sd->SetSwitch(SD_S_VMIXEL, lo & 0xffffff);
    g_iop.sd->SetSwitch(SD_S_VMIXER, lo & 0xffffff);
    const uint32_t c1 = lo >> 24 | hi << 8;
    g_iop.sd->SetSwitch(SD_S_VMIXEL | 1, c1);
    g_iop.sd->SetSwitch(SD_S_VMIXER | 1, c1);
}

// O volume de 8 bits do EE no registrador de volume fixo de 15 bits.
uint16_t fixed_vol(uint8_t b) { return int8_t(b) < 0 ? uint16_t(uint32_t(b) << 7) : uint16_t((int8_t(b) * 0x81) & 0xffff); }

// [snddrv] 0x00007fe8 SNDIOP_spusetvol: perto do alvo, o volume fixo; longe, a rampa da SPU2 na direção dele.
void SNDIOP_spusetvol(const uint8_t* entry, int v, int side) {
    const uint16_t base = vent(v);
    const uint16_t cur = g_iop.sd->GetParam(uint16_t((side == 0 ? SD_VP_VOLXL : SD_VP_VOLXR) | base));
    const int32_t cur8 = int32_t(uint32_t(cur) << 16) >> 24;
    g_iop.prev[v].vol[side] = int8_t(cur >> 8);
    const int32_t want = int8_t(entry[6 + side]);
    int32_t diff = want - cur8;
    if (want <= cur8) diff = cur8 - want;
    const uint16_t reg = side == 0 ? SD_VP_VOLL : SD_VP_VOLR;
    if ((diff >> 1) < 2) {
        g_iop.sd->SetParam(uint16_t(reg | base), fixed_vol(entry[6 + side]));
        g_iop.prev[v].vol[side] = int8_t(entry[6 + side]);
        g_iop.ramping[side + v * 2] = 0;
        return;
    }
    const int32_t c16 = int16_t(cur);
    uint16_t sweep = uint16_t(uint8_t(kVolRamp[diff >> 1]));
    const int32_t w16 = int32_t(uint32_t(entry[6 + side]) << 24) >> 16;
    if (c16 < w16) {
        if (c16 < 0) {
            sweep = uint16_t(sweep | 0xb000);
            if (-1 < w16) sweep = 0xb01b;
        } else {
            sweep = uint16_t(sweep | 0x8000);
        }
    } else if (c16 < 1) {
        sweep = uint16_t(sweep | 0x9000);
    } else {
        sweep = uint16_t(sweep | 0xa000);
        if (w16 < 1) sweep = 0xa01b;
    }
    g_iop.sd->SetParam(uint16_t(reg | base), sweep);
    g_iop.ramping[side + v * 2] = 1;
}

// [snddrv] 0x000081e8 SNDIOP_checkspueffect: troca de reverb (o volume do efeito cai a zero, o novo tipo, a área
// zerada) e o volume dele andando 0x800 por tick até o pedido.
void SNDIOP_checkspueffect(const uint8_t* p) {
    if (int8_t(p[0x10]) != g_iop.fx_type) {
        g_iop.fx_state = 1;
        g_iop.fx_type = int8_t(p[0x10]);
    }
    if (g_iop.fx_state == 0) {
        for (int side = 0; side < 2; ++side) {
            const int16_t want = rd16s(p + 0x14 + side * 2);
            if (want == g_iop.evol[side]) continue;
            const uint16_t reg = side == 0 ? SD_P_EVOLL : SD_P_EVOLR;
            const int32_t cur = int16_t(g_iop.sd->GetParam(reg));
            int32_t next;
            bool far;
            if (cur < want) {
                next = cur + 0x800;
                far = next < want;
            } else {
                next = cur - 0x800;
                far = want < next;
            }
            if (!far) {
                next = want;
                g_iop.evol[side] = want;
            }
            g_iop.sd->SetParam(reg, uint16_t(next));
            g_iop.sd->SetParam(uint16_t(reg | 1), uint16_t(next));
        }
        return;
    }
    if (g_iop.fx_state == 1) {
        int32_t e = int32_t(g_iop.sd->GetParam(SD_P_EVOLL)) - 0x800;
        if (e < 1) e = 0;
        g_iop.sd->SetParam(SD_P_EVOLL, uint16_t(e));
        g_iop.sd->SetParam(SD_P_EVOLL | 1, uint16_t(e));
        g_iop.sd->SetParam(SD_P_EVOLR, uint16_t(e));
        g_iop.sd->SetParam(SD_P_EVOLR | 1, uint16_t(e));
        if (e == 0) {
            g_iop.fx_state = 2;
            g_iop.evol[0] = 0;
            g_iop.evol[1] = 0;
            g_iop.sd->SetAddr(SD_A_EEA | 1, p[0x10] == 0 ? 0x1e0000 : 0x1c0000);
            SndEffectAttr a;
            a.mode = g_iop.fx_type;
            a.delay = int8_t(p[0x11]);
            a.feedback = int8_t(p[0x12]);
            a.core = 0;
            g_iop.sd->SetEffectAttr(0, a);
            a.core = 1;
            g_iop.sd->SetEffectAttr(1, a);
            g_iop.clear_top[0] = 0x200000;
            g_iop.clear_top[1] = 0x1e0000;
        }
    }
    if (g_iop.fx_state == 2 && SNDIOP_dmcomplete(g_iop.clear_id) != 0) {
        for (int core = 0; core < 2; ++core) {
            const uint32_t esa = g_iop.sd->GetAddr(uint16_t(SD_A_ESA | core));
            const uint32_t top = g_iop.clear_top[core];
            if (esa < top) {
                uint32_t n = top - esa;
                if (0x1000 < n) n = 0x1000;
                g_iop.clear_top[core] = top - n;
                g_iop.clear_id = SNDIOP_dmqueue(0, g_zero_buf, top - n, uint16_t(n), 0x14, 0);
            } else if (SNDIOP_dmcomplete(g_iop.clear_id) != 0) {
                g_iop.fx_state = 0;
            }
        }
    }
}

// [snddrv] 0x00007c2c SNDIOP_setiopvoicevol / 0x00007cf4 SNDIOP_updateiopvoices: as vozes do mixador do IOP.
void SNDIOP_updateiopvoices(const uint8_t* p) {
    if (p[0x13] != g_iop.quality) g_iop.quality = p[0x13];  // MIX_setresamplequality
    for (int k = 0; k < g_iop.iop_voices; ++k) {
        const uint8_t* e = p + 0x1a0 + k * 0x10;
        const uint32_t addr = rd32(e) & 0x7fffff;
        if (addr != 0 && g_iop.mix_on[k] == 0) ++g_iop.guards;  // uma voz do IOP pedida: não portada
        g_iop.ioptoee[0x34 + k] = g_iop.mix_on[k] != 0 ? 0 : 0xffffffffu;
    }
}

// [snddrv] 0x0000858c SNDIOP_updatevoices: compara cada voz da SPU2 com o tick anterior e manda o que mudou.
void SNDIOP_updatevoices(const uint8_t* p) {
    uint32_t on_lo = 0, on_hi = 0, off_lo = 0, off_hi = 0;
    g_iop.updates += 1;
    if (g_iop.updates == 3) {
        g_iop.sd->SetParam(SD_P_BVOLL, 0x7fff);
        g_iop.sd->SetParam(SD_P_BVOLR, 0x7fff);
    }
    SNDIOP_checkspueffect(p);
    if (rd32(p + 8) != g_iop.dry[0] || rd32(p + 0xc) != g_iop.dry[1]) {
        SNDIOP_setdrybusbits(rd32(p + 8), rd32(p + 0xc));
        g_iop.dry[0] = rd32(p + 8);
        g_iop.dry[1] = rd32(p + 0xc);
    }
    if (rd32(p + 0) != g_iop.wet[0] || rd32(p + 4) != g_iop.wet[1]) {
        SNDIOP_setwetbusbits(rd32(p + 0), rd32(p + 4));
        g_iop.wet[0] = rd32(p + 0);
        g_iop.wet[1] = rd32(p + 4);
    }
    for (int v = 0; v < 48; ++v) {
        const uint8_t* e = p + 0x20 + v * 8;
        IopVoice& pr = g_iop.prev[v];
        const uint16_t base = vent(v);
        bool keyed = false;
        uint32_t start = 0;
        if (rd16(e + 4) != pr.pitch) {
            g_iop.sd->SetParam(uint16_t(SD_VP_PITCH | base), rd16(e + 4));
            pr.pitch = rd16(e + 4);
        }
        if (int8_t(e[6]) != pr.vol[0] || g_iop.ramping[v * 2] != 0) SNDIOP_spusetvol(e, v, 0);
        if (int8_t(e[7]) != pr.vol[1] || g_iop.ramping[v * 2 + 1] != 0) SNDIOP_spusetvol(e, v, 1);
        const uint32_t word = rd32(e);
        const uint32_t bit_lo = v < 32 ? 1u << v : 0, bit_hi = v < 32 ? 0 : 1u << (v & 31);
        if (word != pr.addr) {
            start = word & 0x7fffff;
            if (start == 0) {
                off_lo |= bit_lo;
                off_hi |= bit_hi;
                pr.addr = word;
            } else if (g_iop.sd->GetParam(uint16_t(SD_VP_ENVX | base)) == 0) {
                g_iop.sd->SetAddr(uint16_t(SD_VA_SSA | base), start);
                g_iop.sd->SetAddr(uint16_t(SD_VA_LSAX | base), 0x5000);
                g_iop.sd->SetParam(uint16_t(SD_VP_ADSR1 | base), 0xf);
                g_iop.sd->SetParam(uint16_t(SD_VP_ADSR2 | base), 5);
                g_iop.sd->SetParam(uint16_t(SD_VP_VOLL | base), fixed_vol(e[6]));
                pr.vol[0] = int8_t(e[6]);
                g_iop.ramping[v * 2] = 0;
                keyed = true;
                g_iop.sd->SetParam(uint16_t(SD_VP_VOLR | base), fixed_vol(e[7]));
                on_lo |= bit_lo;
                on_hi |= bit_hi;
                pr.vol[1] = int8_t(e[7]);
                g_iop.ramping[v * 2 + 1] = 0;
                pr.addr = word;
            } else {
                off_lo |= bit_lo;
                off_hi |= bit_hi;
                keyed = true;
            }
        }
        const uint32_t nax = g_iop.sd->GetAddr(uint16_t(SD_VA_NAX | base));
        g_iop.ioptoee[4 + v] = nax;
        if (keyed) g_iop.ioptoee[4 + v] = start;
        else if (nax < 0x5040) g_iop.ioptoee[4 + v] = 0;
    }
    SNDIOP_updateiopvoices(p);
    if (on_hi != 0 || on_lo != 0) SNDIOP_keyon(on_lo, on_hi);
    if (off_hi != 0 || off_lo != 0) SNDIOP_keyoff(off_lo, off_hi);
}

// [snddrv] 0x000061ec SNDIOP_ee48_iop48: as amostras do EE (16 bits) mais as do mixador do IOP (>> 7), saturadas, nos
// blocos da entrada da SPU2 (256 amostras de um lado, 256 do outro; o primeiro é o canal 1 do EE: o esquerdo).
void SNDIOP_ee48_iop48(const uint8_t* ee, int16_t* out) {
    static const int16_t kEe[4] = {512, 0, 768, 256};  // .rodata 0x0000abc4
    static const int16_t kIop[4] = {1, 0, 1, 0};       // .rodata 0x0000abb4
    for (int b = 0; b < 2; ++b) {
        for (int ch = 0; ch < 2; ++ch) {
            const uint8_t* src = ee + kEe[ch + b * 2] * 2;
            int32_t* acc = &g_iop.mix[size_t(kIop[ch + b * 2]) * 0x200 + size_t(b) * 0x100];
            for (int i = 0; i < 256; ++i) {
                const int32_t x = int32_t(rd16s(src + i * 2)) + (acc[i] >> 7);
                out[i] = int16_t(x < -0x8000 ? -0x8000 : (x < 0x8000 ? x : 0x7fff));
                acc[i] = 0;
            }
            out += 0x100;
        }
    }
}

// [snddrv] 0x000074b0 SNDIOP_mix: o rótulo 1 (as amostras do EE).
void SNDIOP_mix(const uint8_t* tag, uint32_t* at) {
    const uint32_t size_at = *at;
    *at += 0xc;
    int16_t* half = g_iop.out.data() + (1 - g_iop.half) * (g_iop.out_size >> 2) / 2;
    // MIX_audioslice do mixador do IOP: sem vozes, os acumuladores ficam em zero.
    if (g_iop.ee_rate == 48000 && g_iop.iop_rate == 48000 && g_iop.outputs < 4) {
        SNDIOP_ee48_iop48(tag + *at, half);
    } else {
        ++g_iop.guards;  // outras taxas (24 e 36 kHz) e o DTS: não portados
    }
    *at += rd16(tag + size_at);
}

// [snddrv] 0x00007914 SNDIOP_processtagbuf
void SNDIOP_processtagbuf(uint8_t* tag) {
    uint32_t at = 0;
    uint32_t word = rd32(tag);
    while (word != 6) {
        const uint32_t here = at;
        at = here + 4;
        switch (rd32(tag + here)) {
            case 0:
            case 5: at = here + 0x10; break;
            case 1: SNDIOP_mix(tag, &at); break;
            case 2: at += 0xc + g_iop.ee_frames * 4u; ++g_iop.guards; break;  // SNDIOP_maincpufx: não portado
            case 3: {
                const uint32_t size = rd32(tag + at);
                at = here + 8;
                SNDIOP_updatevoices(tag + here + 8);
                at += size;
                break;
            }
            case 4: at += 0xc + 0x800; ++g_iop.guards; break;  // SNDIOP_parsedts
            default: return;
        }
        word = rd32(tag + at);
    }
    std::memcpy(tag, &word, 4);
}

// [snddrv] 0x00008b3c SNDIOP_init
void SNDIOP_init() {
    g_iop.updates = 0;
    g_iop.out_size = g_iop.outputs > 3 ? 0x1000 : 0x2000;
    g_iop.out.assign(size_t(g_iop.out_size) / 2, 0);
    g_iop.mix.assign(size_t(g_iop.outputs + g_iop.extra + (g_iop.outputs == 1 ? 1 : 0)) * 0x200, 0);
    for (int i = 0; i < 0x10; ++i) {
        g_iop.ioptoee[0x34 + i] = 0xffffffffu;
        g_iop.mix_on[i] = 0;
    }
    SndLibsd* sd = g_iop.sd;
    sd->Init(0);
    for (int core = 0; core < 2; ++core) {
        sd->SetParam(uint16_t(SD_P_MVOLL | core), 0);
        sd->SetParam(uint16_t(SD_P_MVOLR | core), 0);
    }
    sd->SetParam(SD_P_MMIX, 0xfc0);
    sd->SetParam(SD_P_MMIX | 1, 0xfcc);
    g_iop.evol[0] = 0;
    g_iop.evol[1] = 0;
    sd->SetParam(SD_P_EVOLL, 0);
    sd->SetParam(SD_P_EVOLL | 1, 0);
    sd->SetParam(SD_P_EVOLR, 0);
    sd->SetParam(SD_P_EVOLR | 1, 0);
    sd->SetAddr(SD_A_EEA, 0x1e0000);
    sd->SetAddr(SD_A_EEA | 1, 0x1e0000);
    SNDIOP_setdrybusbits(0xffffffffu, 0xffff);
    g_iop.dry[0] = 0;
    g_iop.dry[1] = 0;
    SNDIOP_setwetbusbits(0, 0);
    g_iop.wet[0] = 0;
    g_iop.wet[1] = 0;
    g_iop.fx_type = 0;
    SndEffectAttr a;
    for (int core = 0; core < 2; ++core) {
        a.core = core;
        sd->SetEffectAttr(core, a);
        sd->SetCoreAttr(uint16_t(core | 2), 1);  // SD_C_EFFECT_ENABLE
    }
    const int32_t id = SNDIOP_dmqueue(0, g_safe_loop, 0x5000, 0x40, 100, 0);
    while (SNDIOP_dmcomplete(id) == 0) SNDIOP_dmservice();
    for (int v = 0; v < 48; ++v) {
        const uint16_t base = vent(v);
        sd->SetParam(uint16_t(SD_VP_PITCH | base), 0);
        g_iop.prev[v].pitch = 0;
        sd->SetAddr(uint16_t(SD_VA_SSA | base), 0x5000);
        g_iop.prev[v].addr = 0;
        g_iop.ioptoee[4 + v] = 0;
        sd->SetAddr(uint16_t(SD_VA_LSAX | base), 0x5000);
        sd->SetParam(uint16_t(SD_VP_ADSR1 | base), 0xf);
        sd->SetParam(uint16_t(SD_VP_ADSR2 | base), 5);
        sd->SetParam(uint16_t(SD_VP_VOLL | base), 0);
        g_iop.prev[v].vol[0] = 0;
        sd->SetParam(uint16_t(SD_VP_VOLR | base), 0);
        g_iop.prev[v].vol[1] = 0;
        g_iop.ramping[v * 2] = 0;
        g_iop.ramping[v * 2 + 1] = 0;
    }
    SNDIOP_keyon(0xffffffffu, 0xffff);
    sd->SetParam(SD_P_BVOLL, 0);
    sd->SetParam(SD_P_BVOLR, 0);
    if (g_iop.outputs < 4) {
        sd->SetCoreAttr(SD_C_SPDIF, 0);
        sd->BlockTrans(0, 0x10, g_iop.out.data(), uint32_t(g_iop.out_size >> 1));
        for (int core = 0; core < 2; ++core) {
            sd->SetParam(uint16_t(SD_P_MVOLL | core), 0x3fff);
            sd->SetParam(uint16_t(SD_P_MVOLR | core), 0x3fff);
        }
    } else {
        ++g_iop.guards;  // DTS
    }
    g_iop.running = 1;
}

}  // namespace

// [snddrv] 0x00009a3c start
void snddrv_iop_start(SndLibsd* sd) {
    SndIopDmaDone done = g_iop.dma_done;
    std::vector<uint8_t> ram = std::move(g_iop.ram);
    g_iop = Iop();
    // A memória do IOP: zerada, sem blocos.
    if (ram.empty()) ram.assign(2u << 20, 0);
    else std::fill(ram.begin(), ram.end(), uint8_t(0));
    g_iop.ram = std::move(ram);
    g_iop.sd = sd;
    g_iop.dma_done = done;
    g_iop.tag.assign(0x2c00, 0);
    g_iop.fx_type = -1;  // 0x0000b598 = 0xff (threadmain)
}

void snddrv_set_dma_done(SndIopDmaDone fn) { g_iop.dma_done = fn; }

uint32_t IopHeapModel::alloc(uint32_t size) {
    const uint32_t need = (size + 0xff) & ~0xffu;
    if (need == 0 || count_ >= 256) return 0;
    // Os blocos ficam em ordem de endereço; o primeiro vão que cabe.
    uint32_t at = kIopHeapBase;
    int i = 0;
    for (; i < count_; ++i) {
        if (blocks_[i].addr - at >= need) break;
        at = blocks_[i].addr + blocks_[i].size;
    }
    if (i == count_ && (at > kIopHeapEnd || kIopHeapEnd - at < need)) return 0;
    for (int k = count_; k > i; --k) blocks_[k] = blocks_[k - 1];
    blocks_[i] = {at, need};
    ++count_;
    return at;
}

int32_t IopHeapModel::free(uint32_t addr) {
    for (int i = 0; i < count_; ++i) {
        if (blocks_[i].addr != addr) continue;
        for (--count_; i < count_; ++i) blocks_[i] = blocks_[i + 1];
        return 0;
    }
    return -1;
}

uint32_t snddrv_iop_alloc(uint32_t size) { return g_iop.heap.alloc(size); }
int32_t snddrv_iop_free(uint32_t addr) { return g_iop.heap.free(addr); }
uint8_t* snddrv_iop_ram(uint32_t addr, uint32_t size) {
    if (g_iop.ram.empty()) g_iop.ram.assign(2u << 20, 0);
    if (addr > g_iop.ram.size() || size > g_iop.ram.size() - addr) return nullptr;
    return g_iop.ram.data() + addr;
}
uint64_t snddrv_guards() { return g_iop.guards; }

// [snddrv] 0x00009440 SNDIOP_dispatchrpc
void snddrv_iop_rpc(int32_t fn, const uint8_t* data, int32_t size) {
    // O registro das chamadas (a conferência compara com o que o EE original manda).
    if (fn >= 0 && fn < 3) {
        ++g_iop.rpc_count[fn];
        for (int32_t i = 0; i < size; ++i) g_iop.rpc_hash = (g_iop.rpc_hash ^ data[i]) * 0x01000193u;
    }
    if (fn == 1) {
        // SNDIOP_restore (0x00009340)
        g_iop.running = 0;
        g_iop.sd->SetParam(SD_P_BVOLL, 0);
        g_iop.sd->SetParam(SD_P_BVOLR, 0);
        g_iop.sd->BlockTrans(0, 2, nullptr, 0);
        return;
    }
    if (fn == 0) {
        if (rd32(data) != 0x72a00) {
            ++g_iop.guards;  // a versão errada: o PS2 para (trap)
            return;
        }
        g_iop.outputs = int8_t(data[0x10]);
        g_iop.extra = int8_t(data[0x11]);
        g_iop.iop_voices = data[0x12];
        g_iop.ee_rate = rd16(data + 0xc);
        g_iop.iop_rate = rd16(data + 0xe);
        g_iop.dts = data[0x13];
        g_iop.ee_frames = uint16_t((uint32_t(g_iop.ee_rate) << 9) / 48000);
        g_iop.iop_frames = uint16_t((uint32_t(g_iop.iop_rate) << 9) / 48000);
        SNDIOP_init();
        return;
    }
    // fn 2: o reverb do mixador do IOP (SNDMIXI_initfx2: o descritor que o SNDDRV_setiopfx manda). O mixador do IOP
    // não está portado (o cabeçalho), então o reverb dele também não: só fica registrado.
}

// [snddrv] 0x000093f0 SNDIOP_cmdhandler
void snddrv_iop_cmd(const uint8_t* iop_src, uint32_t dst, uint16_t size, uint16_t priority, int32_t ee_id) {
    SNDIOP_dmqueue(ee_id, iop_src, dst, size, int16_t(priority), 0);
}

// [snddrv] 0x000097dc threadmain (uma volta)
void snddrv_iop_tick(const uint8_t* tagbuf, uint32_t* ioptoee) {
    if (g_iop.running == 0) return;
    // SNDIOP_copytagbuf (0x0000788c): o EE gravou a tagbuf inteira antes; a cópia sai coerente na primeira vez.
    if (rd32(tagbuf) == 0) std::memcpy(g_iop.tag.data(), tagbuf, rd32(tagbuf + 8));
    else if (rd32(tagbuf) == 6) std::memset(g_iop.tag.data(), 0, 4), g_iop.tag[0] = 6;
    SNDIOP_processtagbuf(g_iop.tag.data());
    // O PS2 roda a thread da DMA a cada metade: a SPU2 passa a tocar a outra.
    g_iop.half ^= 1;
    std::memcpy(ioptoee, g_iop.ioptoee, sizeof(g_iop.ioptoee));
}

// A metade do buffer de saída que o IOP acabou de escrever (o port a entrega à SPU2: driving/sound/snd_spu.h).
const int16_t* snddrv_iop_output(int32_t* frames) {
    *frames = 512;
    return g_iop.out.data() + (g_iop.half) * (g_iop.out_size >> 2) / 2;
}

SndIopInfo snddrv_iop_info() {
    SndIopInfo i;
    i.running = g_iop.running;
    i.outputs = g_iop.outputs;
    i.extra = g_iop.extra;
    i.iop_voices = g_iop.iop_voices;
    i.dts = g_iop.dts;
    i.ee_rate = g_iop.ee_rate;
    i.iop_rate = g_iop.iop_rate;
    i.ee_frames = g_iop.ee_frames;
    i.iop_frames = g_iop.iop_frames;
    i.updates = g_iop.updates;
    for (int k = 0; k < 3; ++k) i.rpc_count[k] = g_iop.rpc_count[k];
    i.rpc_hash = g_iop.rpc_hash;
    return i;
}

}  // namespace drv
