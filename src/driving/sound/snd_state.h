#pragma once
// O estado global do SND no EE (as variáveis do .data/.bss do DRIVING.ELF de 0x003571e0 a 0x00359148), para o porte
// (driving/sound/snd*.cpp) e a conferência (tests/driving_check_sound.cpp). Os campos levam o endereço do PS2.

#include <cstdint>
#include <vector>

#include "driving/sound/snd.h"

namespace drv {

// Os deslocamentos dentro das opções (SNDSYSOPTS a 0x003571e0) que o código usa.
namespace sndopt {
constexpr int kCapsMinRate = 0x00;     // u16 24000
constexpr int kCapsMaxRate = 0x02;     // u16 48000
constexpr int kCapsModeMin = 0x04;     // u8 1
constexpr int kCapsModeMax = 0x05;     // u8 5
constexpr int kCapsMixVoices = 0x06;   // u8 0x20 (EE)
constexpr int kCapsIopVoices = 0x07;   // u8 0x10
constexpr int kCapsSpuVoices = 0x08;   // u8 0x30
constexpr int kSeed = 0x24;            // u32 a semente do iSNDrandom (0x00357204)
constexpr int kMaxBanks = 0x28;        // u16 (0x00357208)
constexpr int kMixRate = 0x2a;         // u16 a taxa do mixador do EE (0x0035720a)
constexpr int kIopRate = 0x2c;         // u16 a do IOP (0x0035720c)
constexpr int kMixVoices = 0x2e;       // u8 (0x0035720e)
constexpr int kIopVoices = 0x2f;       // u8 (0x0035720f)
constexpr int kSpuVoices = 0x30;       // u8 (0x00357210)
constexpr int kStealEqual = 0x3c;      // u8 rouba vozes de prioridade igual (0x0035721c)
constexpr int kLoadModules = 0x3f;     // u8 carregar os IRX (0x0035721f)
constexpr int kResample = 0x41;        // u8 (0x00357221)
constexpr int kFxLevel = 0x42;         // u8 (0x00357222)
constexpr int kIopFx = 0x43;           // u8 o efeito do IOP no mixador (0x00357223)
constexpr int kDtsChannels = 0x44;     // u8 (0x00357224)
constexpr int kOutputMode = 0x48;      // u8 1 mono, 2 estéreo, 3..5 (0x00357228)
constexpr int kExtraOutputs = 0x49;    // u8 (0x00357229)
constexpr int kOutputFlags = 0x4a;     // u8 bit 1: DTS (0x0035722a)
constexpr int kDtsParam = 0x4b;        // u8 (0x0035722b)
constexpr int kModeCount = 0x4d;       // u8 os modos de render (0x0035722d)
constexpr int kModes = 0x50;           // u16[6] (0x00357230)
constexpr int kDtsBuf = 0x64;          // u32 (0x00357244)
constexpr int kDtsSize = 0x68;         // u32 (0x00357248)
constexpr int kModulePath = 0x6c;      // u32 o caminho dos IRX (0x0035724c)
constexpr int kSpeakers = 0x6c;        // u16[mode][6], a linha "mode" em +0x6c + mode x 0xc (0x0035724c)
}  // namespace sndopt

// Uma voz da plataforma (0x14 bytes a 0x003588e0): os ganhos por alto-falante (do SNDI_aztospkrvol), o endereço da
// amostra na SPU2 e os sinais anteriores do Dolby.
struct SndPlatformVoice {
    int16_t gain[6];            // +0x00
    uint32_t spu_addr;          // +0x0c
    int16_t dolby_prev[2];      // +0x10
};
static_assert(sizeof(SndPlatformVoice) == 0x14);

// Um pedido de transferência ao IOP (0x10 bytes a 0x00358710).
struct SndDmaRequest {
    uint32_t id;                // +0x00 (0: livre)
    const uint8_t* src = nullptr;  // +0x04 a origem no EE (nativo aqui)
    uint32_t dst;               // +0x08 o destino na RAM da SPU2 ou do IOP
    uint16_t size;              // +0x0c
    uint8_t priority;           // +0x0e
    uint8_t pad;
};

struct SndState {
    // ---- o sistema (0x003571e0..0x00357d84)
    uint8_t opts[0x120] = {};           // 0x003571e0
    uint8_t saved[0xe8] = {};           // 0x00357300 a cópia do que se pode mudar
    uint8_t opts_got = 0;               // 0x00357154
    int32_t caps_result = 0;            // 0x00357158
    uint8_t inited = 0;                 // 0x003573e8
    int8_t master_vol = 0;              // 0x003573e9
    uint8_t in_timer = 0;               // 0x003573ea
    int8_t critical = 0;                // 0x003573eb
    int8_t n_timer_clients = 0;         // 0x003573ec
    int8_t n_server_clients = 0;        // 0x003573ed
    int8_t n_user_clients = 0;          // 0x003573ee
    int16_t num_voices = 0;             // 0x003573f0
    uint32_t tick = 0;                  // 0x003573f4 o relógio do servidor de 100 Hz
    SndClient timer_clients[6] = {};    // 0x003573f8
    SndClient server_clients[6] = {};   // 0x00357410
    void (*user_clients[4])(void*) = {};  // 0x00357428
    int32_t (*exit_hooks[7])(int32_t) = {};  // 0x00357438..0x00357450 (o +0x0c: SNDbankremove)
    SndVoice* voices = nullptr;         // 0x00357454
    SndBankSlot* banks = nullptr;       // 0x00357458
    SndFxBus bus_ee[1] = {};            // 0x0035745c
    SndFxBus bus_iop[1] = {};           // 0x00357470
    SndFxBus bus_spu[1] = {};           // 0x00357484
    SndMemHeap* heap = nullptr;         // 0x00357498
    uint16_t chan_az[6][6] = {};        // 0x0035749c o azimute padrão do canal c de um som de n canais: [n - 1][c]
    void (*pre_frame)() = nullptr;      // 0x003574e4
    void (*post_frame)() = nullptr;     // 0x003574e8
    uint32_t timer_count = 0;           // 0x003574ec
    int16_t timer_rate = 0;             // 0x003574f0
    int16_t timer_missed = 0;           // 0x003574f2
    int32_t handle_gen = 0;             // 0x003574f8
    int16_t alloc_scratch[8] = {};      // 0x00357500
    int8_t next_key = 0;                // 0x00357618
    uint8_t gains[256 * 6] = {};        // 0x00357620 ganho por (azimute / 256, alto-falante)
    uint32_t rnd[6] = {};               // 0x00357d38
    uint8_t iop_up = 0;                 // 0x00357d84

    // ---- a plataforma (0x003585c0..0x00359148)
    uint32_t iop_status[0x44] = {};     // 0x003585c0 o "ioptoee" (0x110 bytes): +0x10 as 48 vozes, +0xd0 as do IOP
    std::vector<uint8_t> tagbuf;        // 0x0035b380 (0x2c00 bytes; o EE a lê de 0x2035b380, sem cache)
    uint32_t tag_voices = 0;            // 0x003586fc deslocamento dos dados das vozes na tagbuf (0x18)
    uint32_t tag_head = 0;              // 0x00358700 (+4)
    uint32_t tag_tail = 0;              // 0x00358704 (o rótulo 5)
    uint16_t tag_len = 0;               // 0x0035907a
    uint32_t mix_out[8] = {};           // 0x00359084 os canais do mixador do EE na tagbuf
    int32_t dma_id = 0;                 // 0x00358708
    int8_t dma_count = 0;               // 0x0035870c
    uint8_t dma_current = 0;            // 0x0035870d
    uint8_t dma_busy = 0;               // 0x0035870e
    SndDmaRequest dma[0x1c] = {};       // 0x00358710
    uint32_t iop_block = 0;             // 0x003588d4 o bloco das transferências na memória do IOP
    SndPlatformVoice pvoice[64] = {};   // 0x003588e0
    uint16_t spu_count = 0;             // 0x00359060 os blocos alocados na SPU2
    uint16_t spu_lo = 0;                // 0x00359062 (em unidades de 64 bytes)
    uint16_t spu_hi = 0;                // 0x00359064
    uint16_t spu_top = 0;               // 0x00359066 o fim antes da área do reverb
    uint32_t* spu_table = nullptr;      // 0x00359068 {u16 início, u16 tamanho} por bloco
    uint8_t platform_up = 0;            // 0x00359075
    uint16_t dts_extra = 0;             // 0x00359076
    uint16_t frames_tick = 0;           // 0x00359078 amostras do mixador por tick
    uint32_t spu_gen = 0;               // 0x00359080 a geração dos key-on (bits 23..31 do endereço)

    // Port: os blocos de memória do SND fora do heap (o SNDMEMI devolve ponteiros nativos dentro de heap_mem).
    uint8_t* heap_mem = nullptr;
    int32_t heap_size = 0;
};

SndState& snd_state();
// As guardas do porte (os casos em que o PS2 leria ou escreveria fora do lugar; ver os cabeçalhos dos .cpp).
void snd_guard();
uint64_t snd_guards();
// O envelope padrão dos timbres (0x00357610), para a conferência normalizar os ponteiros.
const SndEnvSegment* snd_default_envelope();
// Volta ao estado do boot (o .data do ELF); a memória do heap fica com o chamador.
void snd_power_on();

// Internos usados entre os arquivos do SND.
void* SNDMEMI_allocz(int32_t size);
void SNDMEMI_free(void* p);
void SNDMEMI_init(uint8_t* mem, int32_t size);
int32_t SNDVOICEI_get(int32_t handle);
void SNDVOICEI_free(int32_t voice);
int32_t SNDVOICEI_alloc(int32_t n, int32_t priority, int32_t* handle, int32_t first, int32_t end);
int32_t iSNDpatchkey(int32_t voice, int32_t* cursor);
void iSNDcalcvol(int32_t voice);
void iSNDcalcpitch(int32_t voice);
int32_t iSNDdetunetolinear(int32_t cents);
void SNDI_calcfxlevel(int32_t bus, int32_t voice);
void SNDI_aztospkrvol(uint32_t azimuth, int16_t* gains);
void SNDI_precalcaztospkrvol();
int32_t SNDI_equalpower(int32_t x);
uint16_t SNDI_pantoazimuth(int32_t pan);
void SNDI_randomseed(uint32_t seed);
uint32_t iSNDrandom();
int32_t randrange(int32_t range);
SndFxBus* SNDCTRLI_getfxbus(int32_t bus, uint32_t render);
uint32_t SNDI_validrendermode(int32_t* index, const SndPatchHeader* h);

// A plataforma do PS2 (driving/sound/snd_ps2.cpp).
int32_t SNDPLATFORM_outputcaps();
int32_t SNDPLATFORM_outputset();
void SNDPLATFORM_getvoicerange(uint32_t render, int32_t* first, int32_t* end);
int32_t SNDPLATFORM_init();
int32_t SNDPLATFORM_restore();
int32_t SNDPLATFORM_stop(int32_t voice);
int32_t SNDPLATFORM_setpitch(int32_t voice);
int32_t SNDPLATFORM_setfxlevel(int32_t voice, int32_t bus);
void SNDPLATFORM_setvol(int32_t voice);
void SNDPLATFORM_setdrylevel(int32_t voice);
void SNDPLATFORM_set3dpos(int32_t voice);
void SNDPLATFORM_lowpass(int32_t voice, int32_t cutoff);
int32_t SNDPLATFORM_fxinit(int32_t bus, uint32_t path);
void SNDPLATFORM_fxoutputbalance(int32_t bus, uint32_t path);
int32_t SNDPLATFORM_playtimbre(SndPatchHeader* h, uint8_t* bank, int32_t voice, int32_t time_mult, int32_t distort,
                               int32_t lowpass, int32_t highpass);
int32_t SNDPLATFORM_asyncresolvetimbre(SndPatchHeader* h, uint32_t spu_base, int32_t* first);
uint32_t SNDPLATFORM_memalloc(int32_t where, int32_t size);
int32_t SNDPLATFORM_memfree(int32_t where, uint32_t addr);
int32_t SNDPLATFORM_download(int32_t where, const uint8_t* src, uint32_t dst, int32_t size);
int32_t SNDPLATFORM_downloadcomplete(int32_t id);
void SNDDRV_updatevoices();
void SNDDRV_setfx(uint32_t path);

}  // namespace drv
