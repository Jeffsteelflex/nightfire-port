// O SND da EA no EE (driving/sound/snd.h): o sistema, o heap, o servidor de 100 Hz, o gerente das vozes, a API, os
// bancos e as etiquetas PT. Portado do MIPS do DRIVING.ELF (as desmontagens em out/agent_drv_sound/ do checkout
// principal); a plataforma do PS2 fica em snd_ps2.cpp.
//
// Divergências (port), além das do cabeçalho:
//  - SNDBANKI_playpatch guarda até 12 handles na pilha (0xc0..0xf0) e um 13º timbre tocado escreveria por cima das
//    variáveis dela; aqui para em 12 e conta em snd_guards();
//  - SNDVOICEI_free com dois timbres do mesmo som e nenhum marcado como o último lê a "voz -1" (antes do vetor, fora do
//    heap); aqui conta como livre e conta em snd_guards(); SNDBANKI_playpatch sem nenhuma voz válida escreveria no
//    endereço 0x27; aqui não escreve e conta.

#include "driving/sound/snd.h"

#include <cstring>

#include "driving/sound/snd_state.h"
#include "driving/sound/snd_mix.h"
#include "driving/sound/snd_stream.h"
#include "platform/ps2_float.h"

namespace drv {

namespace {

SndState g_snd;
uint64_t g_guards = 0;

// As tabelas do .data: 2^(i/256) - 1 em 256 avos (0x00357510), o envelope padrão (0x00357610: para sempre no 127),
// o pan -> azimute (0x00357c20, um seno de um quarto de volta à esquerda a um à direita) e as sementes (0x00357d20).
constexpr uint8_t kOctaveFraction[256] = {
    0,   1,   2,   2,   3,   4,   4,   5,   6,   7,   7,   8,   9,   9,   10,  11,  12,  12,  13,  14,  14,  15,
    16,  17,  17,  18,  19,  20,  20,  21,  22,  23,  23,  24,  25,  26,  26,  27,  28,  29,  30,  30,  31,  32,
    33,  33,  34,  35,  36,  37,  37,  38,  39,  40,  41,  41,  42,  43,  44,  45,  45,  46,  47,  48,  49,  50,
    50,  51,  52,  53,  54,  55,  55,  56,  57,  58,  59,  60,  61,  61,  62,  63,  64,  65,  66,  67,  67,  68,
    69,  70,  71,  72,  73,  74,  75,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  86,  87,  88,
    89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 108, 109,
    110, 111, 112, 113, 114, 115, 116, 117, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
    133, 134, 135, 136, 137, 138, 139, 140, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 154, 155, 156,
    157, 158, 159, 160, 161, 163, 164, 165, 166, 167, 168, 169, 171, 172, 173, 174, 175, 176, 178, 179, 180, 181,
    182, 183, 185, 186, 187, 188, 189, 191, 192, 193, 194, 196, 197, 198, 199, 200, 202, 203, 204, 205, 207, 208,
    209, 210, 212, 213, 214, 216, 217, 218, 219, 221, 222, 223, 225, 226, 227, 229, 230, 231, 232, 234, 235, 236,
    238, 239, 240, 242, 243, 245, 246, 247, 249, 250, 251, 253, 254, 255,
};
// O envelope padrão: um trecho, para sempre (0x7fffffff), no nível 127.
SndEnvSegment kDefaultEnvelope[2] = {{0x7fffffff, 127}, {0, 0}};
constexpr uint16_t kPanAzimuth[128] = {
    0xc000, 0xc007, 0xc016, 0xc02f, 0xc052, 0xc07e, 0xc0b5, 0xc0f5, 0xc13f, 0xc192, 0xc1ef, 0xc256, 0xc2c6,
    0xc340, 0xc3c3, 0xc44f, 0xc4e4, 0xc583, 0xc62a, 0xc6db, 0xc794, 0xc856, 0xc921, 0xc9f4, 0xcacf, 0xcbb3,
    0xcc9e, 0xcd92, 0xce8d, 0xcf90, 0xd09a, 0xd1ac, 0xd2c5, 0xd3e4, 0xd50b, 0xd638, 0xd76c, 0xd8a6, 0xd9e5,
    0xdb2b, 0xdc77, 0xddc8, 0xdf1e, 0xe079, 0xe1d9, 0xe33e, 0xe4a7, 0xe615, 0xe786, 0xe8fb, 0xea74, 0xebf0,
    0xed6f, 0xeef1, 0xf076, 0xf1fd, 0xf386, 0xf511, 0xf69e, 0xf82c, 0xf9bb, 0xfb4c, 0xfcdd, 0xfe6f, 0x0000,
    0x0198, 0x0330, 0x04c7, 0x065e, 0x07f4, 0x0988, 0x0b1b, 0x0cac, 0x0e3b, 0x0fc8, 0x1152, 0x12da, 0x145f,
    0x15e0, 0x175e, 0x18d8, 0x1a4e, 0x1bc0, 0x1d2e, 0x1e97, 0x1ffb, 0x215a, 0x22b4, 0x2408, 0x2557, 0x269f,
    0x27e2, 0x291e, 0x2a53, 0x2b82, 0x2caa, 0x2dcb, 0x2ee4, 0x2ff7, 0x3101, 0x3204, 0x32fe, 0x33f1, 0x34db,
    0x35bd, 0x3697, 0x3767, 0x382f, 0x38ee, 0x39a4, 0x3a51, 0x3af4, 0x3b8e, 0x3c1f, 0x3ca6, 0x3d24, 0x3d97,
    0x3e01, 0x3e61, 0x3eb7, 0x3f04, 0x3f46, 0x3f7e, 0x3fac, 0x3fd0, 0x3fea, 0x3ff9, 0x3fff,
};
constexpr uint32_t kSeeds[6] = {0xf22d0e56, 0x883126e9, 0xc624dd2f, 0x0702c49c, 0x9e353f7d, 0x6fdf3b64};

// Os campos das opções.
uint8_t& o8(int off) { return g_snd.opts[off]; }
uint16_t o16(int off) {
    uint16_t v;
    std::memcpy(&v, &g_snd.opts[off], 2);
    return v;
}
void o16(int off, uint16_t v) { std::memcpy(&g_snd.opts[off], &v, 2); }
uint32_t o32(int off) {
    uint32_t v;
    std::memcpy(&v, &g_snd.opts[off], 4);
    return v;
}

SndVoice& voice(int32_t v) { return g_snd.voices[v]; }

int32_t mul32(int32_t a, int32_t b) { return int32_t(uint32_t(a) * uint32_t(b)); }

}  // namespace

SndState& snd_state() { return g_snd; }
uint64_t snd_guards() { return g_guards; }
void snd_guard() { ++g_guards; }
const SndEnvSegment* snd_default_envelope() { return kDefaultEnvelope; }

void snd_power_on() {
    g_snd = SndState();
    g_guards = 0;
    g_snd.tagbuf.assign(0x2c00, 0);
    snd_stream_power_on();
    mix_power_on();
}

// ---- as opções ----------------------------------------------------------------------------------------------------

// [driving] 0x00307108 SNDSYS_getopts
int32_t SNDSYS_getopts(SndSysOpts* opts) {
    if (g_snd.opts_got == 0) {
        g_snd.caps_result = SNDPLATFORM_outputcaps();
        g_snd.opts_got = 1;
        o16(sndopt::kMaxBanks, 0x10);
        o8(0x4c) = 0x5a;
        o8(sndopt::kStealEqual) = 1;
        std::memcpy(g_snd.saved, g_snd.opts + 0x20, 0xe8);
    }
    std::memcpy(opts->raw, g_snd.opts, 0x120);
    return g_snd.caps_result;
}

// [driving] 0x003072e8 SNDSYS_setopts
int32_t SNDSYS_setopts(const SndSysOpts* opts) {
    std::memcpy(g_snd.opts + 0x20, opts->raw + 0x20, 0xe8);
    std::memcpy(g_snd.opts + 0x108, opts->raw + 0x108, 0x18);
    SNDPLATFORM_outputset();
    std::memcpy(g_snd.saved, g_snd.opts + 0x20, 0xe8);
    return 0;
}

// ---- o heap (SNDMEMI) ---------------------------------------------------------------------------------------------

// [driving] 0x00307c30 SNDMEMI_init(void *, int)
void SNDMEMI_init(uint8_t* mem, int32_t size) {
    SndMemHeap* h = reinterpret_cast<SndMemHeap*>(mem);
    h->size = size;
    h->low_water = size - 0xf;
    g_snd.heap = h;
    h->table = reinterpret_cast<int32_t*>(mem + (size - 8));
    h->base = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(mem) + 0x27) & ~uintptr_t(15));
    h->limit = size - 0x2f;
    g_snd.heap_mem = mem;
    g_snd.heap_size = size;
}

// [driving] 0x00307c00 SNDMEMI_constrain(unsigned int *, int *)
static void SNDMEMI_constrain(int32_t* offset, int32_t* size) {
    if (uint32_t(g_snd.heap->limit) < uint32_t(*offset + *size)) *size = g_snd.heap->limit - *offset;
}

// [driving] 0x00307c90 SNDMEMI_allocz(int): o bloco não é zerado (o nome engana; o SNDSYSI_init zera o heap todo).
void* SNDMEMI_allocz(int32_t request) {
    SndMemHeap* h = g_snd.heap;
    const int32_t size = (request + 0xf) & ~0xf;
    int32_t* table = nf::raw(h->table);
    int32_t slot = 0, offset = 0, room = 0;
    if (h->count == 0) {
        room = h->limit;
        offset = 0;
        SNDMEMI_constrain(&offset, &room);
        if (room < size) return nullptr;
        slot = 0;
    } else {
        int32_t i = 0;
        bool found = false;
        if (h->count < 0) {
            do {
                int32_t* rec = table + i * 2;
                if (i == 0) {
                    offset = 0;
                    room = rec[0];
                } else {
                    int32_t* prev = table + i * 2 + 2;
                    offset = prev[0] + prev[1];
                    room = rec[0] - offset;
                }
                SNDMEMI_constrain(&offset, &room);
                if (size <= room) {
                    for (int32_t k = h->count; k < i; ++k) std::memcpy(table + k * 2, table + k * 2 + 2, 8);
                    found = true;
                    break;
                }
                --i;
            } while (h->count < i);
        }
        slot = i;
        if (!found) {
            int32_t* prev = table + i * 2 + 2;
            offset = prev[0] + prev[1];
            room = h->limit - offset;
            SNDMEMI_constrain(&offset, &room);
            if (room < size) return nullptr;
        }
    }
    int32_t* rec = table + slot * 2;
    rec[0] = offset;
    rec[1] = size;
    h->count -= 1;
    h->limit -= 8;
    const int32_t left = h->limit - (offset + size);
    if (left < h->low_water) h->low_water = left;
    return nf::raw(h->base) + offset;
}

// [driving] 0x00307eb8 SNDMEMI_free(void *)
void SNDMEMI_free(void* p) {
    SndMemHeap* h = g_snd.heap;
    int32_t* table = nf::raw(h->table);
    const int32_t off = int32_t(static_cast<uint8_t*>(p) - nf::raw(h->base));
    int32_t i = 0;
    if (h->count >= 0) return;
    while (table[i * 2] != off) {
        --i;
        if (i <= h->count) return;
    }
    h->limit += 8;
    h->count += 1;
    while (h->count < i) {
        std::memcpy(table + i * 2, table + i * 2 - 2, 8);
        --i;
    }
}

// ---- o sistema ----------------------------------------------------------------------------------------------------

// [driving] 0x0030c990 SNDI_randomseed(unsigned int)
void SNDI_randomseed(uint32_t seed) {
    for (int i = 0; i < 6; ++i) g_snd.rnd[i] = kSeeds[i] + seed;
}

// [driving] 0x0030c9d0 iSNDrandom(void): seis palavras somadas em cascata com o vai-um.
uint32_t iSNDrandom() {
    uint32_t* r = g_snd.rnd;  // r[0] = 0x00357d38 ... r[5] = 0x00357d4c
    const uint32_t s5 = r[5], s4 = r[4], s3 = r[3], s2 = r[2], s1 = r[1];
    const uint32_t u1 = s5 + s4;
    const uint32_t c1 = (u1 < s5 || u1 < s4) ? 1 : 0;
    const uint32_t u2 = u1 + s3 + c1;
    const uint32_t u3 = u2 + s2 + (u2 < s3 ? 1 : 0);
    r[5] = s5 + 1;
    const uint32_t u4 = u3 + s1 + (u3 < s2 ? 1 : 0);
    r[0] = u4 + r[0] + (u4 < s1 ? 1 : 0);
    r[1] = u4;
    r[2] = u3;
    r[3] = u2;
    r[4] = u1;
    if (r[5] == 0) {
        r[4] = u1 + 1;
        if (r[4] == 0) {
            r[3] = u2 + 1;
            if (r[3] == 0) {
                r[2] = u3 + 1;
                if (r[2] == 0) {
                    r[1] = u4 + 1;
                    if (r[1] == 0) r[0] += 1;
                }
            }
        }
    }
    return r[0];
}

// [driving] 0x00312218 randrange
int32_t randrange(int32_t range) {
    if (range > 0x10000) range = 0x10000;
    else if (range < 0) range = 0;
    const uint32_t r = iSNDrandom();
    return mul32(int32_t(r & 0x7fff) - 0x4000, range) >> 14;
}

// [driving] 0x003074c8 SNDSYSI_init
int32_t SNDSYSI_init(uint8_t* mem, int32_t size) {
    if (g_snd.inited != 0) return 0;
    std::memset(mem, 0, size_t(size));
    SNDMEMI_init(mem, size);
    if (g_snd.num_voices == 0) {
        SndSysOpts o;
        const int32_t r = SNDSYS_getopts(&o);
        if (r < 0) return r;
        SNDSYS_setopts(&o);
    }
    SNDI_randomseed(o32(sndopt::kSeed));
    SNDSYS_entercritical();  // (o SNDI_mutexalloc antes: no PC não faz nada)
    g_snd.voices = static_cast<SndVoice*>(SNDMEMI_allocz(g_snd.num_voices * 0x88));
    g_snd.banks = static_cast<SndBankSlot*>(SNDMEMI_allocz(int32_t(o16(sndopt::kMaxBanks)) << 4));
    SNDSYS_leavecritical();
    g_snd.timer_count = 0;
    g_snd.master_vol = 0x7f;
    g_snd.tick = 0;
    g_snd.n_user_clients = 0;
    g_snd.n_timer_clients = 0;
    g_snd.n_server_clients = 0;
    const int32_t r = SNDPLATFORM_init();
    if (r < 0) {
        SNDPLATFORM_restore();
        return r;
    }
    g_snd.inited = 1;
    if (o8(sndopt::kOutputMode) == 2) {
        g_snd.chan_az[1][0] = 0xc000;
        g_snd.chan_az[1][1] = 0x4000;
    } else {
        g_snd.chan_az[1][0] = 0xe000;
        g_snd.chan_az[1][1] = 0x2000;
    }
    g_snd.chan_az[2][0] = o16(0x94);
    g_snd.chan_az[2][1] = o16(0x90);
    g_snd.chan_az[2][2] = o16(0x92);
    g_snd.chan_az[5][0] = 0xe000;
    g_snd.chan_az[5][2] = 0x2000;
    g_snd.chan_az[5][3] = 0xa000;
    g_snd.chan_az[5][4] = 0x6000;
    g_snd.chan_az[5][5] = 0;
    g_snd.chan_az[3][0] = 0xe000;
    g_snd.chan_az[3][1] = 0x2000;
    g_snd.chan_az[3][2] = 0xa000;
    g_snd.chan_az[3][3] = 0x6000;
    g_snd.chan_az[4][0] = 0xe000;
    g_snd.chan_az[4][1] = 0;
    g_snd.chan_az[4][2] = 0x2000;
    g_snd.chan_az[4][3] = 0xa000;
    g_snd.chan_az[4][4] = 0x6000;
    g_snd.chan_az[5][1] = 0;
    SNDI_precalcaztospkrvol();
    return 0;
}

// [driving] 0x003077b0 SNDSYS_inited
uint8_t SNDSYS_inited() { return g_snd.inited; }

// [driving] 0x00307688 SNDSYS_restore
int32_t SNDSYS_restore() {
    if (SNDSYS_inited() == 0) return -14;
    SNDfxinitbus(0, 0, 0, -1, -1);
    // Os ganchos de saída na ordem do original: +0, +4, +8, +0x14, +0x18, +0x10, (SNDstopall), +0xc(-1).
    for (int k : {0, 1, 2, 5, 6, 4})
        if (g_snd.exit_hooks[k] != nullptr) g_snd.exit_hooks[k](0);
    SNDstopall();
    if (g_snd.exit_hooks[3] != nullptr) g_snd.exit_hooks[3](-1);
    SNDPLATFORM_restore();
    SNDSYS_entercritical();
    SNDMEMI_free(g_snd.voices);
    SNDMEMI_free(g_snd.banks);
    SNDSYS_leavecritical();
    // SNDMEMI_restore: a porcentagem usada do heap (só devolvida).
    const int32_t used = ((g_snd.heap->size - g_snd.heap->low_water) * 100) / g_snd.heap->size;
    g_snd.inited = 0;
    return used;
}

// [driving] 0x00307bb0 SNDSYS_entercritical (o SNDI_mutexlock antes: no PC não faz nada)
void SNDSYS_entercritical() { g_snd.critical = int8_t(g_snd.critical + 1); }
// [driving] 0x00307be0 SNDSYS_leavecritical
void SNDSYS_leavecritical() { g_snd.critical = int8_t(g_snd.critical - 1); }

// [driving] 0x00307850 SNDSYSI_timerservice(int): o servidor de 100 Hz alcança o relógio (rate chamadas por segundo).
void SNDSYSI_timerservice(uint32_t rate) {
    if (g_snd.inited == 0 || g_snd.in_timer != 0) return;
    if (int32_t(rate) != int32_t(g_snd.timer_rate)) {
        g_snd.timer_rate = int16_t(rate);
        g_snd.tick = 0;
        g_snd.timer_count = 0;
    }
    if (g_snd.critical != 0) {
        g_snd.timer_missed = int16_t(g_snd.timer_missed + 1);
        return;
    }
    g_snd.in_timer = 1;
    g_snd.timer_count += 1;
    const uint32_t target = (g_snd.timer_count * 100) / rate;
    while (!(target < g_snd.tick)) SNDSYSI_100hzserver();
    g_snd.in_timer = 0;
}

// [driving] 0x00307940 SNDSYSI_100hzserver(void)
void SNDSYSI_100hzserver() {
    g_snd.tick += 1;
    // iSNDserve (0x0030e5d0): vazio no PS2 (o IOP diz quais vozes acabaram: SNDDRV_updatevoices).
    for (int i = 0; i < g_snd.n_timer_clients; ++i) g_snd.timer_clients[i]();
    for (int32_t v = 0; v < g_snd.num_voices; ++v) {
        SndVoice& s = voice(v);
        if (int8_t(s.in_use) != 1 || s.handle < 0) continue;
        if (s.pitch_lfo != nullptr) {
            s.pitch_lfo_pos = uint8_t(s.pitch_lfo_pos + 1);
            if (!(s.pitch_lfo_pos < s.pitch_lfo_len)) s.pitch_lfo_pos = 0;
            s.detune_linear = 0;
            iSNDcalcpitch(v);
            SNDPLATFORM_setpitch(v);
        }
        bool changed = false;
        if (s.vol_lfo != nullptr) {
            changed = true;
            s.vol_lfo_pos = uint8_t(s.vol_lfo_pos + 1);
            if (!(s.vol_lfo_pos < s.vol_lfo_len)) s.vol_lfo_pos = 0;
        }
        bool stop = false;
        if (s.fade_step != 0) {
            changed = true;
            s.fade += s.fade_step;
            const bool going = s.fade_step >= 0 ? s.fade < s.fade_target : s.fade_target < s.fade;
            if (!going) {
                s.fade = s.fade_target;
                s.fade_step = 0;
            }
            if (s.fade < 0) stop = true;
        }
        if (!stop) {
            s.env_ticks -= 1;
            if (s.env_step != 0) {
                s.env += s.env_step;
                changed = true;
            }
            if (s.env_ticks == 0) {
                s.env_current = int8_t(s.env_current + 1);
                if (!(s.env_current < int8_t(s.env_count))) {
                    stop = true;
                } else {
                    const SndEnvSegment& seg = nf::raw(s.env_table)[s.env_current];
                    s.env_ticks = seg.ticks;
                    if (seg.ticks < 0) s.env_ticks = 0x7fffffff;
                    s.env_step = (int32_t(uint32_t(seg.level) << 16) - s.env) / s.env_ticks;
                }
            }
        }
        if (stop) {
            SNDstop(s.handle);
            continue;
        }
        if (changed) {
            iSNDcalcvol(v);
            if (s.handle >= 0) SNDPLATFORM_setvol(v);
        }
    }
}

// [driving] 0x00311078 iSNDserveraddclient(void (*)(void))
void iSNDserveraddclient(SndClient fn) {
    g_snd.server_clients[g_snd.n_server_clients] = fn;
    g_snd.n_server_clients = int8_t(g_snd.n_server_clients + 1);
}

// [driving] 0x003110a0 iSNDserverremoveclient(void (*)(void))
void iSNDserverremoveclient(SndClient fn) {
    int i = 0;
    while (i < g_snd.n_server_clients && g_snd.server_clients[i] != fn) ++i;
    if (i >= g_snd.n_server_clients) return;
    g_snd.n_server_clients = int8_t(g_snd.n_server_clients - 1);
    for (; i < g_snd.n_server_clients; ++i) g_snd.server_clients[i] = g_snd.server_clients[i + 1];
}

// [driving] 0x00311140 SNDSYS_service
int32_t SNDSYS_service() {
    if (g_snd.inited != 0)
        for (int i = 0; i < g_snd.n_server_clients; ++i) g_snd.server_clients[i]();
    return 0;
}

// [driving] 0x00309f30 SNDmastervol
int32_t SNDmastervol(int32_t vol) {
    SNDSYS_entercritical();
    g_snd.master_vol = int8_t(vol);
    for (int32_t v = 0; v < g_snd.num_voices; ++v) {
        if (voice(v).in_use != 1 || voice(v).handle < 0) continue;
        iSNDcalcvol(v);
        SNDPLATFORM_setvol(v);
    }
    SNDSYS_leavecritical();
    return 0;
}

// ---- as vozes -----------------------------------------------------------------------------------------------------

// [driving] 0x0030a940 SNDVOICEI_isreserved(int, int)
static int32_t SNDVOICEI_isreserved(int32_t v, int32_t count) {
    for (int32_t i = 0; i < count; ++i)
        if (g_snd.alloc_scratch[i] == int16_t(v)) return 1;
    return 0;
}

// [driving] 0x0030a988 SNDVOICEI_alloc(int, int, int *, int, int): n vozes entre first e end (livres, as mais velhas
// primeiro; senão roubadas das de prioridade menor, ou igual com o kStealEqual, as mais velhas primeiro). Devolve a
// mestre (a menor) ou -9.
int32_t SNDVOICEI_alloc(int32_t n, int32_t priority, int32_t* handle, int32_t first, int32_t end) {
    int32_t result = -9;
    for (int32_t k = 0; k < n; ++k) g_snd.alloc_scratch[k] = -1;
    g_snd.handle_gen += 0x100;
    if (g_snd.handle_gen < 0) g_snd.handle_gen = 0;
    int32_t got = 0;
    for (int32_t pass = 0; pass < n; ++pass) {
        int32_t best = -1;
        uint32_t best_tick = 0xffffffffu;
        for (int32_t v = first; v < end; ++v) {
            if (voice(v).in_use != 0) continue;
            if (SNDVOICEI_isreserved(v, got) != 0) continue;
            if (voice(v).tick < best_tick) {
                best_tick = voice(v).tick;
                best = v;
            }
        }
        if (best >= 0) g_snd.alloc_scratch[got++] = int16_t(best);
    }
    if (got < n) {
        int32_t pass = got;
        do {
            int32_t limit = priority - 1;
            if (o8(sndopt::kStealEqual) != 0) limit = priority;
            uint32_t best_tick = 0xffffffffu;
            int32_t best = -1;
            for (int32_t v = first; v < end; ++v) {
                if (SNDVOICEI_isreserved(v, got) != 0) continue;
                const int32_t pr = voice(v).priority;
                if (!(pr < 0x65)) continue;
                if (pr < limit) {
                    limit = pr;
                    best_tick = voice(v).tick;
                    best = v;
                } else if (pr == limit && voice(v).tick < best_tick) {
                    best_tick = voice(v).tick;
                    best = v;
                }
            }
            if (best >= 0) {
                g_snd.alloc_scratch[got++] = int16_t(best);
                if (!(got < n)) break;
            }
            ++pass;
        } while (pass < n);
    }
    if (got != n) return result;
    bool sorted;
    do {
        sorted = true;
        for (int32_t i = 0; i < got - 1; ++i) {
            const int16_t a = g_snd.alloc_scratch[i], b = g_snd.alloc_scratch[i + 1];
            if (b < a) {
                g_snd.alloc_scratch[i] = b;
                g_snd.alloc_scratch[i + 1] = a;
                sorted = false;
            }
        }
    } while (!sorted);
    result = g_snd.alloc_scratch[0];
    *handle = g_snd.handle_gen | int32_t(g_snd.alloc_scratch[0]);
    for (int32_t k = 0; k < got; ++k) {
        SndVoice& s = voice(g_snd.alloc_scratch[k]);
        if (s.in_use != 0) {
            int32_t h = s.handle;
            if (h < 0) h = voice(s.master).handle;
            SNDstop(h);
        }
        s.in_use = 1;
        s.tick = g_snd.tick;
        s.priority = uint8_t(priority);
    }
    SndVoice& m = voice(g_snd.alloc_scratch[0]);
    m.channels_v[0] = g_snd.alloc_scratch[0];
    m.handle = *handle;
    m.master = -1;
    for (int32_t k = 1; k < got; ++k) {
        m.channels_v[k] = g_snd.alloc_scratch[k];
        SndVoice& s = voice(g_snd.alloc_scratch[k]);
        s.master = g_snd.alloc_scratch[0];
        s.handle = -1;
    }
    return result;
}

// [driving] 0x0030adf0 SNDVOICEI_free(int)
void SNDVOICEI_free(int32_t v) {
    SndVoice& s = voice(v);
    const uint8_t key = s.key;
    if (key == 0) {
        s.in_use = 0;
    } else {
        int32_t last = -1, count = 0;
        for (int32_t i = 0; i < g_snd.num_voices; ++i) {
            const SndVoice& o = voice(i);
            if (o.key != key || o.handle < 0 || o.in_use == 0) continue;
            ++count;
            if (o.key_last != 0) last = i;
        }
        if (count == 1) {
            s.in_use = 0;
            s.key = 0;
            s.key_last = 0;
            s.tick = g_snd.tick;
            return;
        }
        int8_t last_use = 0;
        if (last >= 0) last_use = int8_t(voice(last).in_use);
        else ++g_guards;  // a "voz -1" (fora do heap): conta como livre
        if (last_use == 2) {
            if (v != last && count == 2) {
                s.in_use = 0;
                s.key = 0;
                s.key_last = 0;
                s.tick = g_snd.tick;
                SndVoice& l = voice(last);
                l.in_use = 0;
                l.key = 0;
                l.key_last = 0;
                l.tick = g_snd.tick;
                return;
            }
        }
        if (last_use == 1 && v == last) {
            voice(last).in_use = 2;
            return;
        }
        s.in_use = 0;
    }
    s.key = 0;
    s.key_last = 0;
    s.tick = g_snd.tick;
}

// [driving] 0x0030af70 SNDVOICEI_get(int): a voz do handle, ou -8.
int32_t SNDVOICEI_get(int32_t handle) {
    if (handle < 0) return -8;
    const int32_t v = handle & 0xff;
    if (!(v < g_snd.num_voices)) return -8;
    if (voice(v).in_use == 0 || voice(v).handle != handle) return -8;
    return v;
}

// [driving] 0x0030b268 iSNDpatchkey(int, int *): a próxima voz mestre de um som (os timbres com a mesma tecla).
int32_t iSNDpatchkey(int32_t v, int32_t* cursor) {
    const uint8_t key = voice(v).key;
    if (key == 0) {
        if (*cursor >= 0) return 0;
        *cursor = v;
        return 1;
    }
    *cursor += 1;
    if (!(*cursor < g_snd.num_voices)) return 0;
    for (;;) {
        const int32_t i = *cursor;
        const SndVoice& o = voice(i);
        if (o.key == key && int8_t(o.in_use) == 1 && o.handle >= 0) return 1;
        *cursor = i + 1;
        if (!(i + 1 < g_snd.num_voices)) return 0;
    }
}

// [driving] 0x0030b038 iSNDcalcvol(int)
void iSNDcalcvol(int32_t v) {
    SndVoice& s = voice(v);
    const int32_t a = mul32(s.builtin_vol, g_snd.master_vol);
    const int32_t b = mul32(int16_t(uint32_t(s.env) >> 16), int16_t(uint32_t(s.fade) >> 16));
    int32_t vol = mul32(a, b) / 0x1f417f;
    s.vol = int8_t(vol);
    if (s.vol_lfo != nullptr) {
        vol = mul32(int8_t(vol), nf::raw(s.vol_lfo)[s.vol_lfo_pos]) / 0x7f;
        s.vol = int8_t(vol);
    }
    if (s.vol_table != nullptr) s.vol = nf::raw(s.vol_table)[s.vol];
}

// [driving] 0x0030b0e8 iSNDdetunetolinear(int): centésimos para um multiplicador 4.12.
int32_t iSNDdetunetolinear(int32_t cents) {
    int32_t lin = 0x1000;
    while (!(cents < 0x4b0)) {
        cents -= 0x4b0;
        lin = int32_t(uint32_t(lin) << 1);
    }
    while (cents < -0x4af) {
        cents += 0x4b0;
        lin >>= 1;
    }
    int32_t f = mul32(cents, 0x369d) >> 16;
    if (!(-0x100 < f)) f = -0xff;
    if (f < 0) return mul32(lin, kOctaveFraction[256 + f] + 0x100) >> 9;
    return mul32(lin, kOctaveFraction[f] + 0x100) >> 8;
}

// [driving] 0x0030b1a0 iSNDcalcpitch(int)
void iSNDcalcpitch(int32_t v) {
    SndVoice& s = voice(v);
    if (s.detune_linear == 0) {
        int32_t cents = s.detune;
        if (s.bend_range != 0) {
            int8_t b = s.bend;
            if (s.bend_table != nullptr) b = nf::raw(s.bend_table)[b];
            cents = s.detune + (mul32(b - 0x40, s.bend_range) >> 6);
        }
        if (s.pitch_lfo != nullptr)
            cents += mul32(nf::raw(s.pitch_lfo)[s.pitch_lfo_pos] - 0x40, s.pitch_lfo_depth) >> 6;
        s.detune_linear = uint16_t(iSNDdetunetolinear(cents));
    }
    s.pitch = uint16_t(int32_t(uint32_t(s.detune_linear) * uint32_t(s.pitch_mult)) >> 12);
}

// [driving] 0x0030afc8 SNDI_calcfxlevel(int, int)
void SNDI_calcfxlevel(int32_t bus, int32_t v) {
    SndVoice& s = voice(v);
    const SndFxBus* b = SNDCTRLI_getfxbus(bus, s.render);
    const int8_t* builtin = &s.builtin_fx + bus;
    const int8_t* level = &s.fx_level + bus;
    (&s.fx_send)[bus] = int16_t(mul32(mul32(*builtin, *level), b->level) >> 6);
}

// [driving] 0x0030c640 SNDI_equalpower(int)
int32_t SNDI_equalpower(int32_t x) { return x * 2 - (x * x) / 0x7f; }

// [driving] 0x0030c668 SNDI_precalcaztospkrvol(void): o ganho de cada alto-falante por azimute (256 passos), pela
// potência igual entre os dois alto-falantes vizinhos (as posições na linha do modo de saída das opções).
void SNDI_precalcaztospkrvol() {
    const int n = o8(sndopt::kOutputMode);
    for (int i = 0; i < 256; ++i) {
        uint8_t* g = &g_snd.gains[i * 6];
        const int32_t az = i << 8;
        if (n == 1) {
            g[0] = 0x7f;
            continue;
        }
        for (int k = 0; k < n; ++k) g[k] = 0;
        const int row = sndopt::kSpeakers + n * 0xc;
        auto sp = [&](int k) { return int32_t(o16(row + k * 2)); };
        if (!(sp(0) < az)) {
            // antes do primeiro alto-falante: entre o último (dando a volta) e o primeiro
            const int32_t s2 = sp(0) - az;
            const int32_t a1 = az + 0x10000 - sp(n - 1);
            const int32_t s1 = s2 + a1;
            g[0] = uint8_t(SNDI_equalpower((a1 * 127) / s1));
            g[n - 1] = uint8_t(SNDI_equalpower((s2 * 127) / s1));
        } else if (!(az < sp(n - 1))) {
            // depois do último
            const int32_t a1 = az - sp(n - 1);
            const int32_t s2 = sp(0) + 0x10000 - az;
            const int32_t s1 = s2 + a1;
            g[0] = uint8_t(SNDI_equalpower((a1 * 127) / s1));
            g[n - 1] = uint8_t(SNDI_equalpower((s2 * 127) / s1));
        } else {
            for (int k = 0; k < n; ++k) {
                if (az < sp(k)) continue;
                const int32_t next = sp(k + 1);
                const int32_t s2 = az - sp(k);
                const int32_t a1 = next - az;
                const int32_t s1 = s2 + a1;
                if (next < az) continue;
                g[k] = uint8_t(SNDI_equalpower((a1 * 127) / s1));
                g[k + 1] = uint8_t(SNDI_equalpower((s2 * 127) / s1));
            }
        }
    }
}

// [driving] 0x0030c908 SNDI_aztospkrvol(int, short *): o ganho (byte com sinal x 0x102) de cada alto-falante.
void SNDI_aztospkrvol(uint32_t azimuth, int16_t* gains) {
    const int n = o8(sndopt::kOutputMode);
    for (int k = 0; k < n; ++k) {
        const int idx = int32_t((azimuth & 0xffff) >> 8) * 6 + k;
        gains[k] = int16_t(int8_t(g_snd.gains[idx]) * 0x102);
    }
}

// [driving] 0x0030c978 SNDI_pantoazimuth(int)
uint16_t SNDI_pantoazimuth(int32_t pan) { return kPanAzimuth[pan]; }

// ---- a API ---------------------------------------------------------------------------------------------------------

// [driving] 0x0030a8e8 SNDplaysetdef
int32_t SNDplaysetdef(SndPlayOpts* o) {
    o->key = 0x3c;
    o->tempo_mult = 0x1000;
    o->lowpass = 0xffff;
    o->bend = 0x40;
    o->fx_level = 0x7f;
    o->elevation = 0;
    o->pitch_mult = 0x1000;
    o->time_mult = 0x1000;
    o->distort = 0;
    o->highpass = 0;
    o->velocity = 0x7f;
    o->vol = 0x7f;
    o->prog_vol = 0x7f;
    o->azimuth = 0;
    return 0;
}

// [driving] 0x0030a660 SNDstop
int32_t SNDstop(int32_t handle) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) SNDPLATFORM_stop(c);
    }
    return v;
}

// [driving] 0x0030a6b8 SNDstopall
void SNDstopall() {
    SNDSYS_entercritical();
    for (int32_t v = 0; v < g_snd.num_voices; ++v) SNDstop(voice(v).handle);
    SNDSYS_leavecritical();
}

// [driving] 0x0030a730 SNDover
int32_t SNDover(int32_t handle) { return int32_t(uint32_t(SNDVOICEI_get(handle)) >> 31); }

// [driving] 0x0030a3d0 SNDvol
int32_t SNDvol(int32_t handle, int32_t vol) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) {
            SndVoice& s = voice(c);
            s.fade_step = 0;
            if (s.fade == int32_t(uint32_t(vol) << 16)) return 0;
            s.fade = int32_t(uint32_t(vol) << 16);
            iSNDcalcvol(c);
            SNDPLATFORM_setvol(c);
            if (s.fx_send > 0) SNDPLATFORM_setfxlevel(s.channels_v[0], 0);
        }
    }
    return v;
}

// [driving] 0x0030a310 SNDautovol
int32_t SNDautovol(int32_t handle, int32_t ticks, int32_t vol) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        if (ticks < 1) ticks = 1;
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) {
            SndVoice& s = voice(c);
            s.fade_target = int32_t(uint32_t(vol) << 16);
            s.fade_step = (int32_t(uint32_t(vol) << 16) - s.fade) / ticks;
        }
    }
    return v;
}

// [driving] 0x0030a4a0 SNDpitchmult
int32_t SNDpitchmult(int32_t handle, uint32_t mult) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) {
            SndVoice& s = voice(c);
            if (s.pitch_mult == mult) return 0;
            s.pitch_mult = uint16_t(mult);
            iSNDcalcpitch(c);
            SNDPLATFORM_setpitch(c);
        }
    }
    return v;
}

// [driving] 0x0030a750 SND3dpos
int32_t SND3dpos(int32_t handle, int32_t azimuth, int32_t elevation) {
    if (!(elevation < 0x4000)) elevation = 0x3fff;
    else if (elevation < -0x4000) elevation = -0x4000;
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) {
            SndVoice& s = voice(c);
            s.elevation = int16_t(elevation);
            s.azimuth = uint16_t(s.builtin_azimuth + azimuth);
            SNDPLATFORM_set3dpos(c);
        }
    }
    return v;
}

// [driving] 0x0030a548 SNDfxlevel
int32_t SNDfxlevel(int32_t handle, int32_t bus, int32_t level) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0 && bus != 0) {
        // O barramento 1 leria o nível de fora da tabela dos barramentos (o ponteiro do heap do SND): o jogo só usa o
        // 0; conta e não muda nada.
        snd_guard();
        return v;
    }
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) {
            (&voice(c).fx_level)[bus] = int8_t(level);
            SNDI_calcfxlevel(bus, c);
            SNDPLATFORM_setfxlevel(c, bus);
        }
    }
    return v;
}

// [driving] 0x0030a5f8 SNDCTRL_lowpass
int32_t SNDCTRL_lowpass(int32_t handle, int32_t cutoff) {
    const int32_t v = SNDVOICEI_get(handle);
    if (v >= 0) {
        int32_t c = -1;
        while (iSNDpatchkey(v, &c) != 0) SNDPLATFORM_lowpass(c, cutoff);
    }
    return v;
}

// [driving] 0x00309fe8 SNDCTRLI_getfxbus(int, int)
SndFxBus* SNDCTRLI_getfxbus(int32_t bus, uint32_t render) {
    const uint32_t where = render & 0x71c;
    if (where == 8) return &g_snd.bus_spu[bus];
    if (where == 4) return &g_snd.bus_ee[bus];
    if (where == 0x100) return &g_snd.bus_iop[bus];
    return nullptr;
}

// [driving] 0x0030a060 SNDCTRLI_fxbustoplayloc(int)
static uint32_t SNDCTRLI_fxbustoplayloc(uint32_t bus) {
    uint32_t r = (bus >> 3) & 8;
    if ((bus & 0x10) != 0) r |= 4;
    if ((bus & 0x20) != 0) r |= 0x100;
    return r;
}

// [driving] 0x0030a088 SNDfxinitbus
int32_t SNDfxinitbus(uint32_t bus, int32_t level, int32_t mode, int32_t delay, int32_t feedback) {
    if ((bus & ~0x1f8u) != 0) {
        // Só há o barramento 0 (o original escreveria nas variáveis depois dele; o jogo só usa o 0).
        snd_guard();
        return 0;
    }
    if ((bus & 0x1f8) == 0) bus |= 0x1f8;
    uint32_t paths = SNDCTRLI_fxbustoplayloc(bus);
    if (mode == 0) level = 0;
    for (uint32_t path = 4; paths != 0; path <<= 1) {
        if ((path & paths) == 0) continue;
        SndFxBus* b = SNDCTRLI_getfxbus(int32_t(bus & ~0x1f8u), path);
        b->feedback = feedback;
        b->mode = uint16_t(mode);
        b->delay = delay;
        SNDPLATFORM_fxinit(int32_t(bus & ~0x1f8u), path);
        paths &= ~path;
    }
    SNDfxmasterlevel(bus, level);
    return 0;
}

// [driving] 0x0030a178 SNDfxmasterlevel
int32_t SNDfxmasterlevel(uint32_t bus, int32_t level) {
    const int32_t index = int32_t(bus & ~0x1f8u);
    if (index != 0) {
        snd_guard();  // (como no SNDfxinitbus)
        return 0;
    }
    if ((bus & 0x1f8) == 0) bus |= 0x1f8;
    if ((bus & 0x10) != 0) g_snd.bus_ee[index].level = int8_t(level);
    if ((bus & 0x20) != 0) g_snd.bus_iop[index].level = int8_t(level);
    if ((bus & 0x40) != 0) g_snd.bus_spu[index].level = int8_t(level);
    SNDSYS_entercritical();
    if ((bus & 0x40) != 0) {
        // SNDCTRL_fxoutputbalance(bus, 8, 0, {l, l}): o nível em 0..1 nos dois lados.
        const float f = ps2::ee::mul(ps2::ee::cvt_s(level), 0.007874016f);
        g_snd.bus_spu[index].balance[0] = f;
        g_snd.bus_spu[index].balance[1] = f;
        SNDPLATFORM_fxoutputbalance(index, 8);
    }
    for (int32_t v = 0; v < g_snd.num_voices; ++v)
        SNDfxlevel(voice(v).handle, index, (&voice(v).fx_level)[index]);
    SNDSYS_leavecritical();
    return 0;
}

// ---- as etiquetas PT -----------------------------------------------------------------------------------------------

// [driving] 0x00312270 SNDI_getb(void *, int): big-endian, com sinal em 1 a 3 bytes.
int32_t SNDI_getb(const uint8_t* data, int32_t length) {
    int32_t v = 0;
    for (int32_t n = length; n != 0; --n) v = v * 0x100 + *data++;
    if (length == 1 && v > 0x7f) v -= 0x100;
    else if (length == 2 && v > 0x7fff) v -= 0x10000;
    else if (length == 3 && v > 0x7fffff) v -= 0x1000000;
    return v;
}

// [driving] 0x00312300 SNDI_gettag(SNDTAGINFO *)
int32_t SNDI_gettag(SndTagReader& r) {
    const uint8_t* p = r.cursor;
    while (*p == 0xfc) {
        ++p;
        r.cursor = p;
    }
    r.tag = *p;
    if (r.tag == 0xff) return 0;
    r.cursor = p + 1;
    if (r.tag == 0xfd || r.tag == 0xfe) return 1;
    r.length = p[1];
    if (r.length == 0xff) {
        r.length = uint32_t(SNDI_getb(p + 2, 4));
        r.cursor += 4;
    }
    r.data = r.cursor + 1;
    r.cursor += 1;
    if (r.length < 5) r.value = SNDI_getb(r.data, int32_t(r.length));
    r.cursor += r.length;
    return 1;
}

// [driving] 0x0030b318 SNDI_parsetimbre(void **, SNDIPATCHHEADER *)
int32_t SNDI_parsetimbre(const uint8_t** cursor, SndPatchHeader* h) {
    int32_t low[0x26] = {};
    const uint8_t* low_data[0x26] = {};
    int32_t smp[0x28] = {};
    const uint8_t* smp_data[0x28] = {};
    int32_t az[6] = {};
    SndTagReader r;
    r.cursor = *cursor;
    h->start = const_cast<uint8_t*>(*cursor);
    for (int i = 0; i < 6; ++i) {
        h->stretch[i] = nullptr;
        h->stretch_size[i] = 0;
    }
    low[0x07] = 0x3c;
    low[0x0c] = 0x40;
    low[0x0e] = 0x7f;
    low[0x02] = 0x7f;
    low[0x04] = 0x7f;
    low[0x08] = -1;
    low[0x09] = 1;
    h->user_count = 0;
    low[0x1a] = -1;
    low[0x1c] = 0x7f;
    low[0x25] = 1;
    smp[0x02] = 1;            // 0x82 os canais
    smp[0x04] = 22050;        // 0x84 a taxa
    smp[0x07] = -1;           // 0x87
    smp[0x06] = -1;           // 0x86
    smp[0x20] = 5;            // 0xa0 PS-ADPCM
    smp[0x0c] = 8;            // 0x8c a SPU2
    smp[0x21] = 1;            // 0xa1
    int32_t more = 0;
    for (;;) {
        if (SNDI_gettag(r) == 0) break;
        const int32_t t = r.tag;
        if (t <= 0x25) {
            low[t] = r.value;
            low_data[t] = r.data;
            if (t == 0x14) {
                h->user_data[h->user_count] = const_cast<uint8_t*>(r.data);
                h->user_size[h->user_count] = int32_t(r.length);
                h->user_count = int8_t(h->user_count + 1);
            }
        } else if (uint32_t(t - 0x80) <= 0x27) {
            smp[t - 0x80] = r.value;
            smp_data[t - 0x80] = r.data;
            int ch = -1;
            switch (t) {
                case 0x98: ch = 0; break;
                case 0x99: ch = 1; break;
                case 0x9a: ch = 2; break;
                case 0x9b: ch = 3; break;
                case 0xa4: ch = 4; break;
                case 0xa5: ch = 5; break;
                default: break;
            }
            if (ch >= 0) {
                h->stretch[ch] = const_cast<uint8_t*>(r.data);
                h->stretch_size[ch] = uint16_t(r.length);
            }
        } else if (t == 0xfe) {
            more = 1;
            break;
        }
    }
    auto at = [](const uint8_t* data, int32_t off) { return const_cast<uint8_t*>(data) + off; };
    h->velocity_low = int8_t(low[0x01]);
    h->velocity_high = int8_t(low[0x02]);
    h->key_low = int8_t(low[0x03]);
    h->key_high = int8_t(low[0x04]);
    h->priority = int8_t(low[0x06]);
    h->root_key = int8_t(low[0x07]);
    h->env_release = uint8_t(low[0x08]);
    h->env_count = uint8_t(low[0x09]);
    h->bend_range = int8_t(low[0x0a]);
    h->pan = int8_t(low[0x0c]);
    h->pan_random = int8_t(low[0x0d]);
    h->volume = int8_t(low[0x0e]);
    h->volume_random = int8_t(low[0x0f]);
    h->tune = uint16_t(low[0x10]);
    h->tune_random = uint16_t(low[0x11]);
    h->vol_table = reinterpret_cast<int8_t*>(at(low_data[0x12], low[0x12]));
    h->fx_level = int8_t(low[0x13]);
    h->bend_table = reinterpret_cast<int8_t*>(at(low_data[0x17], low[0x17]));
    // O envelope: sem a etiqueta, o padrão (o valor é o endereço 0x00357610 e o dado nulo).
    if (low_data[0x19] == nullptr && low[0x19] == 0) h->env_table = kDefaultEnvelope;
    else h->env_table = reinterpret_cast<SndEnvSegment*>(at(low_data[0x19], low[0x19]));
    h->tag1a = low[0x1a];
    h->env_start = int8_t(low[0x1c]);
    h->vol_lfo = reinterpret_cast<int8_t*>(at(low_data[0x1d], low[0x1d]));
    h->vol_lfo_len = uint8_t(low[0x1e]);
    h->vol_lfo_random = int8_t(low[0x1f]);
    h->pitch_lfo = reinterpret_cast<int8_t*>(at(low_data[0x20], low[0x20]));
    h->pitch_lfo_len = uint8_t(low[0x21]);
    h->pitch_lfo_depth = uint16_t(low[0x22]);
    h->pitch_lfo_random = uint8_t(low[0x23]);
    h->detune_random = uint16_t(low[0x24]);
    h->pan_mult = uint8_t(low[0x25]);
    h->platform_version = uint8_t(smp[0x00]);
    h->channels = int8_t(smp[0x02]);
    h->sample_rep = uint8_t(smp[0x20]);
    h->sample_rate = uint16_t(smp[0x04]);
    h->frames = smp[0x05];
    h->loop_start = smp[0x06];
    h->loop_end = smp[0x07];
    h->render = uint16_t(smp[0x0c]);
    h->sample_data = reinterpret_cast<int32_t*>(const_cast<uint8_t*>(smp_data[0x0a]));
    h->sample_offset[0] = smp[0x08];
    h->sample_offset[1] = smp[0x09];
    h->sample_offset[2] = smp[0x14];
    h->sample_offset[3] = smp[0x15];
    h->sample_offset[4] = smp[0x22];
    h->sample_offset[5] = smp[0x23];
    az[0] = smp[0x1c];
    az[1] = smp[0x1d];
    az[2] = smp[0x1e];
    az[3] = smp[0x1f];
    az[4] = smp[0x26];
    az[5] = smp[0x27];
    for (int i = 0; i < h->channels; ++i) {
        if (o8(sndopt::kOutputMode) == 2 && uint8_t(h->channels) == 2) az[i] = 0;
        h->azimuth[i] = uint16_t(g_snd.chan_az[h->channels - 1][i] + az[i]);
    }
    const int32_t a1 = smp[0x21];
    if (a1 != 1 && h->render == 8) {
        uint16_t r16 = 0;
        if ((a1 & 1) != 0) r16 = 8;
        if ((a1 & 2) != 0) r16 |= 4;
        if ((a1 & 0x80) != 0) r16 |= 0x20;
        if ((a1 & 0x40) != 0) r16 |= 0x40;
        h->render = r16;
    }
    *cursor = r.cursor;
    return more;
}

// [driving] 0x003111b8 SNDI_validrendermode(int *, SNDIPATCHHEADER *): o próximo modo configurado que serve ao timbre
// (o lugar, & 0x71c, e o tipo, & 0xe0; um tipo 0x80 só para um canal).
uint32_t SNDI_validrendermode(int32_t* index, const SndPatchHeader* h) {
    int32_t i = *index;
    const uint16_t want = h->render;
    while (i < o8(sndopt::kModeCount)) {
        const uint16_t mode = o16(sndopt::kModes + i * 2);
        uint32_t where = mode & 0x71c;
        uint32_t type = mode & 0xe0;
        bool ok = true;
        if ((want & 0x71c) != 0) {
            where = want & where;
            if (where == 0) ok = false;
        }
        if (ok && (want & 0xe0) != 0) {
            type = want & type;
            if (type == 0) ok = false;
        }
        if (ok && (h->channels == 1 || (type & 0x80) == 0) && (where | type) != 0) {
            *index = i + 1;
            return where | type;
        }
        ++i;
        *index = i;
    }
    return 0;
}

// ---- os bancos ----------------------------------------------------------------------------------------------------

// [driving] 0x00311570 SNDBANKI_alloc(void)
static int32_t SNDBANKI_alloc() {
    for (int32_t i = 0; i < int32_t(o16(sndopt::kMaxBanks)); ++i)
        if (g_snd.banks[i].header == nullptr) return i;
    return -9;
}

// [driving] 0x003115b0 SNDBANKI_getppatch(BANKVER5 *, int)
uint8_t* SNDBANKI_getppatch(uint8_t* bank, int32_t patch) {
    uint16_t count;
    std::memcpy(&count, bank + 6, 2);
    if (patch < count) {
        int32_t off;
        std::memcpy(&off, bank + 0x14 + patch * 4, 4);
        if (off != 0) return bank + 0x14 + patch * 4 + off;
    }
    return nullptr;
}

// [driving] 0x003084e0 SNDBANKI_valid(int): 0, -0x12 (carregando) ou -8.
static int32_t SNDBANKI_valid(int32_t bank) {
    if (bank < 0 || !(bank < int32_t(o16(sndopt::kMaxBanks))) || g_snd.banks[bank].header == nullptr) return -8;
    return g_snd.banks[bank].flag == 0 ? 0 : -0x12;
}

// [driving] 0x00307fa0 SNDBANKI_userdatacallback(SNDIPATCHHEADER *, int)
static void SNDBANKI_userdatacallback(SndPatchHeader* h, int32_t op) {
    while (h->user_count > 0) {
        h->user_count = int8_t(h->user_count - 1);
        struct {
            int32_t op;
            uint8_t* data;
            int32_t size;
        } info{op, nf::raw(h->user_data[h->user_count]), h->user_size[h->user_count]};
        for (int i = 0; i < g_snd.n_user_clients; ++i) g_snd.user_clients[i](&info);
    }
}

// [driving] 0x00308630 SNDBANKI_asyncresolvepatch(int, TAGGEDPATCH *, char *, int *)
static void SNDBANKI_asyncresolvepatch(int32_t where, uint8_t* patch, uint32_t base, int32_t* first) {
    const uint8_t* c = patch + 4;
    int32_t more;
    do {
        SndPatchHeader h{};
        more = SNDI_parsetimbre(&c, &h);
        if ((h.render & where) != 0) {
            SNDPLATFORM_asyncresolvetimbre(&h, base, first);
            SNDBANKI_userdatacallback(&h, 0);
        }
    } while (more != 0);
}

// [driving] 0x00308088 SNDbankadd
int32_t SNDbankadd(int32_t* bank, uint8_t* data) {
    int32_t iop_size = 0;
    g_snd.exit_hooks[3] = SNDbankremove;
    const int32_t b = SNDBANKI_alloc();
    *bank = b;
    SndBankSlot& slot = g_snd.banks[b];
    slot.flag = 1;
    slot.header = data;
    slot.iop_mem = 0;
    int32_t header_size, spu_size;
    std::memcpy(&header_size, data + 8, 4);
    std::memcpy(&spu_size, data + 0xc, 4);
    uint16_t count;
    std::memcpy(&count, data + 6, 2);
    if (data[4] == 5) std::memcpy(&iop_size, data + 0x10, 4);
    auto resolve_all = [&](int32_t where, uint32_t base) {
        int32_t first = 0;
        for (int32_t i = 0; i < count; ++i) {
            int32_t off;
            std::memcpy(&off, data + 0x14 + i * 4, 4);
            if (off != 0) SNDBANKI_asyncresolvepatch(where, data + 0x14 + i * 4 + off, base, &first);
        }
    };
    if (iop_size != 0) {
        slot.iop_mem = SNDPLATFORM_memalloc(0x100, iop_size + 0x40);
        if (slot.iop_mem == 0) {
            slot.flag = 0;
            slot.header = nullptr;
            return -6;
        }
        for (int32_t done = 0; done < iop_size; done += 0x1000) {
            const int32_t part = iop_size - done < 0x1001 ? iop_size - done : 0x1000;
            const int32_t id = SNDPLATFORM_download(0x100, data + header_size + done, slot.iop_mem + uint32_t(done), part);
            while (SNDPLATFORM_downloadcomplete(id) == 0) {
            }
        }
        resolve_all(0x100, slot.iop_mem);
    }
    slot.spu_mem = 0;
    if (spu_size != 0) {
        slot.spu_mem = SNDPLATFORM_memalloc(8, spu_size + 0x30);
        if (slot.spu_mem == 0) {
            slot.flag = 0;
            slot.header = nullptr;
            return -6;
        }
        for (int32_t done = 0; done < spu_size; done += 0x1000) {
            const int32_t part = spu_size - done < 0x1001 ? spu_size - done : 0x1000;
            const int32_t id =
                SNDPLATFORM_download(8, data + done + iop_size + header_size, slot.spu_mem + uint32_t(done), part);
            while (SNDPLATFORM_downloadcomplete(id) == 0) {
            }
        }
        resolve_all(8, slot.spu_mem);
    }
    slot.flag = 0;
    return (spu_size == 0 && iop_size == 0) ? 8 : 7;
}

// [driving] 0x00308300 SNDbankremove
int32_t SNDbankremove(int32_t bank) {
    if (bank == -1) {
        for (int32_t i = 0; i < int32_t(o16(sndopt::kMaxBanks)); ++i) SNDbankremove(i);
        return 0;
    }
    if (SNDBANKI_valid(bank) != 0) return -8;
    SNDSYS_entercritical();
    uint8_t* data = nf::raw(g_snd.banks[bank].header);
    for (int32_t v = 0; v < g_snd.num_voices; ++v)
        if (voice(v).bank == bank) SNDstop(voice(v).handle);
    uint16_t count;
    std::memcpy(&count, data + 6, 2);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* patch = SNDBANKI_getppatch(data, i);
        if (patch == nullptr) continue;
        const uint8_t* c = patch + 4;
        int32_t more;
        do {
            SndPatchHeader h{};
            more = SNDI_parsetimbre(&c, &h);
            SNDBANKI_userdatacallback(&h, 2);
        } while (more != 0);
    }
    if (g_snd.banks[bank].iop_mem != 0) SNDPLATFORM_memfree(0x100, g_snd.banks[bank].iop_mem);
    if (g_snd.banks[bank].spu_mem != 0) SNDPLATFORM_memfree(8, g_snd.banks[bank].spu_mem);
    g_snd.banks[bank].flag = 0;
    g_snd.banks[bank].header = nullptr;
    SNDSYS_leavecritical();
    return 0;
}

// [driving] 0x00308528 SNDbankheadersize
int32_t SNDbankheadersize(int32_t bank) {
    int32_t size;
    std::memcpy(&size, nf::raw(g_snd.banks[bank].header) + 8, 4);
    return size;
}

// [driving] 0x00308548 SNDbankheadercopy
int32_t SNDbankheadercopy(uint8_t* dest, int32_t bank) {
    const int32_t size = SNDbankheadersize(bank);
    if (size < 0) return size;
    std::memmove(dest, nf::raw(g_snd.banks[bank].header), size_t(size));
    g_snd.banks[bank].header = dest;
    return 0;
}

// [driving] 0x003085d0 SNDbankpatpresent
int32_t SNDbankpatpresent(int32_t bank, int32_t patch) {
    if (SNDBANKI_valid(bank) < 0) return -8;
    return SNDBANKI_getppatch(nf::raw(g_snd.banks[bank].header), patch) != nullptr ? 1 : 0;
}

// [driving] 0x0030b980 SNDBANKI_findfreekey(void)
static int8_t SNDBANKI_findfreekey() {
    for (;;) {
        int8_t k = int8_t(g_snd.next_key + 1);
        if (k == 0) k = int8_t(g_snd.next_key + 2);
        g_snd.next_key = k;
        bool used = false;
        for (int32_t v = 0; v < g_snd.num_voices; ++v)
            if (voice(v).in_use != 0 && int8_t(voice(v).key) == k) {
                used = true;
                break;
            }
        if (!used) return k;
    }
}

// [driving] 0x0030b9f0 SNDBANKI_playtimbre(int, int, void *, SNDPLAYOPTS *, SNDIPATCHHEADER *, int, int, int)
static int32_t SNDBANKI_playtimbre(int32_t bank, int32_t patch, uint8_t* bank_data, const SndPlayOpts* o,
                                   SndPatchHeader* h, int32_t key, int32_t velocity, int32_t detune_random) {
    const int32_t vol = o->vol, bend = o->bend, fx = o->fx_level;
    int32_t handle = -9;
    int32_t mode_index = 0;
    for (;;) {
        uint32_t mode;
        int32_t v;
        do {
            mode = SNDI_validrendermode(&mode_index, h);
            if (mode == 0) return handle;
            int32_t first = 0, end = 0;
            SNDPLATFORM_getvoicerange(mode, &first, &end);
            v = SNDVOICEI_alloc(h->channels, h->priority, &handle, first, end);
        } while (v < 0);
        SndVoice& s = voice(v);
        int32_t pan = h->pan;
        if (h->pan_random != 0) {
            pan = h->pan + randrange(h->pan_random);
            if (pan < 0) pan = 0;
            else if (pan > 0x7f) pan = 0x7f;
        }
        s.builtin_azimuth = SNDI_pantoazimuth(pan);
        s.detune = int16_t(h->tune);
        s.detune = int16_t(s.detune + (h->root_key - key) * -100);
        if (detune_random != 0) s.detune = int16_t(s.detune + detune_random);
        if (h->tune_random != 0) s.detune = int16_t(s.detune + randrange(int16_t(h->tune_random)));
        s.fade_step = 0;
        s.vol_table = nf::raw(h->vol_table);
        s.bend_table = nf::raw(h->bend_table);
        s.fade = int32_t(uint32_t(vol) << 16);
        int32_t level = h->volume;
        if (h->volume_random != 0) level = h->volume + randrange(h->volume_random);
        if (level > 0x7f) level = 0x7f;
        if (level < 0) level = 0;
        s.builtin_vol = int8_t((level * velocity) / 0x7f);
        s.bend = int8_t(bend);
        s.env_table = nf::raw(h->env_table);
        s.bend_range = int16_t(h->bend_range * 100);
        s.env_current = 0;
        s.env = int32_t(uint32_t(int32_t(h->env_start)) << 16);
        s.env_count = h->env_count;
        s.env_release = h->env_release;
        const SndEnvSegment* env = nf::raw(s.env_table);
        s.env_ticks = env[0].ticks;
        if (env[0].ticks < 0) s.env_ticks = 0x7fffffff;
        s.vol_lfo = nf::raw(h->vol_lfo);
        s.pitch_lfo = nf::raw(h->pitch_lfo);
        s.env_step = (int32_t(uint32_t(env[0].level) << 16) - s.env) / s.env_ticks;
        s.vol_lfo_len = h->vol_lfo_len;
        s.pitch_lfo_len = h->pitch_lfo_len;
        s.pitch_lfo_depth = int16_t(h->pitch_lfo_depth);
        if (h->vol_lfo_random == 0) s.vol_lfo_pos = 0;
        else s.vol_lfo_pos = uint8_t(int32_t(iSNDrandom()) % int32_t(h->vol_lfo_random));
        if (h->pitch_lfo_random == 0) s.pitch_lfo_pos = 0;
        else s.pitch_lfo_pos = uint8_t(int32_t(iSNDrandom()) % int32_t(h->pitch_lfo_len));
        s.time_mult = o->time_mult;
        s.detune_linear = 0;
        s.pitch_mult = o->pitch_mult;
        iSNDcalcpitch(v);
        s.bank = int16_t(bank);
        s.loop_end = h->loop_end;
        s.patch = int16_t(patch);
        s.azimuth = uint16_t(s.builtin_azimuth + o->azimuth);
        s.frames = h->frames;
        s.elevation = int16_t(o->elevation);
        s.sample_rate = h->sample_rate;
        s.sample_rep = h->sample_rep;
        s.render = uint16_t(mode);
        s.channels = uint8_t(h->channels);
        for (int i = 0; i < h->channels; ++i) s.channel_azimuth[i] = h->azimuth[i];
        s.prog_vol = o->prog_vol;
        s.fx_level = int8_t(fx);
        s.builtin_fx = h->fx_level;
        SNDI_calcfxlevel(0, v);
        iSNDcalcvol(v);
        if (SNDPLATFORM_playtimbre(h, bank_data, v, o->time_mult, o->distort, o->lowpass, o->highpass) >= 0) break;
        for (int i = 0; i < h->channels; ++i) SNDVOICEI_free(voice(v).channels_v[i]);
        handle = -9;
    }
    return handle;
}

// [driving] 0x0030be40 SNDBANKI_playpatch(void *, TAGGEDPATCH *, int, int, SNDPLAYOPTS *)
static int32_t SNDBANKI_playpatch(uint8_t* bank_data, uint8_t* patch, int32_t bank, int32_t patch_index,
                                  const SndPlayOpts* o) {
    int32_t result = -9;
    if (patch == nullptr) return -8;
    if (bank_data == nullptr) bank_data = patch;
    const uint8_t* c = patch + 4;
    const int32_t key = o->key, velocity = o->velocity;
    const int8_t sound_key = SNDBANKI_findfreekey();
    int32_t handles[12];
    int32_t count = 0, detune_random = 0, randomised = 0;
    int32_t more;
    do {
        SndPatchHeader h{};
        more = SNDI_parsetimbre(&c, &h);
        if (randomised == 0) {
            if (int16_t(h.detune_random) != 0) detune_random = randrange(int16_t(h.detune_random));
            randomised = 1;
        }
        if (velocity < h.velocity_low || h.velocity_high < velocity || key < h.key_low || h.key_high < key) continue;
        result = SNDBANKI_playtimbre(bank, patch_index, bank_data, o, &h, key, velocity, detune_random);
        if (result < 0) {
            for (int32_t i = 0; i < count; ++i) SNDstop(handles[i]);
            return result;
        }
        if (count < 12) handles[count++] = result;
        else ++g_guards;
        SNDBANKI_userdatacallback(&h, 1);
    } while (more != 0);
    int32_t live = 0;
    for (int32_t i = 0; i < count; ++i)
        if (SNDover(handles[i]) == 0) handles[live++] = handles[i];
    if (live == 0) return result;
    if (live == 1) {
        const int32_t v = SNDVOICEI_get(handles[0]);
        if (v >= 0) {
            voice(v).key_last = 0;
            voice(v).key = 0;
        }
        return result;
    }
    SndVoice* last = nullptr;
    for (int32_t i = 0; i < live; ++i) {
        const int32_t v = SNDVOICEI_get(handles[i]);
        if (v < 0) continue;
        last = &voice(v);
        last->key_last = 0;
        last->key = uint8_t(sound_key);
    }
    if (last != nullptr) last->key_last = 1;
    else ++g_guards;
    return result;
}

// [driving] 0x003086c8 SNDBANK_play
int32_t SNDBANK_play(int32_t bank, int32_t patch, const SndPlayOpts* opts) {
    if (SNDBANKI_valid(bank) < 0) return -8;
    uint8_t* data = nf::raw(g_snd.banks[bank].header);
    uint16_t count;
    std::memcpy(&count, data + 6, 2);
    if (patch < 0 || !(patch < count)) return -8;
    return SNDBANKI_playpatch(data, SNDBANKI_getppatch(data, patch), bank, patch, opts);
}

}  // namespace drv
