#pragma once
// O SND da EA no EE (libsndps2z.a / a parte de som do librealsystemz.a: "SNDAUTHOR Blair Bitonti", versão 7.42.0),
// portado do MIPS do DRIVING.ELF e conferido contra o original no nf_driving_check (parte 13, `--so som`).
// docs/driving.md seção 17. Os nomes vêm do DRIVING.SYM, alinhados pelos tamanhos (a biblioteca foi ligada pronta nas
// duas compilações: a ordem e os tamanhos das funções de cada objeto batem), e a leitura do nightfire-cxbx
// (NightfireResearch: o mesmo SND na versão de Xbox, src/driving/sound/snd/) ajudou a entender as camadas de cima.
//
// As camadas, de cima para baixo (o jogo chama a API: driving/sound/audio.h):
//  - a API (SNDBANK_play, SNDvol, SND3dpos, SNDpitchmult, SNDstop, SNDautovol, SNDfxinitbus...) e o gerente das vozes
//    lógicas (SNDVOICEI_*: a alocação com roubo por prioridade, o volume e o pitch em ponto fixo, o servidor de 100 Hz
//    que anda os envelopes, os LFOs e os desvanecimentos);
//  - os bancos (BNKl versão 5: SNDbankadd manda as amostras para a RAM da SPU2 e troca o deslocamento de cada timbre
//    pelo endereço lá) e as etiquetas PT (SNDI_gettag, SNDI_parsetimbre);
//  - o PS2 (SNDPLATFORM_*, SNDDRV_*: driving/sound/snd_ps2.cpp): três "lugares" de voz pelo modo de render do timbre:
//    8 = uma voz da SPU2 (as amostras PS-ADPCM dos bancos), 0x100 = o mixador do IOP, 4 = o mixador do EE (os fluxos
//    EA-XA). O estado de cada voz da SPU2 vai ao IOP numa "tagbuf" (0x2c00 bytes, montada aqui e lida pelo
//    SNDDRV.IRX portado em driving/sound/snddrv_iop.h) a cada tick do driver (512 amostras de 48 kHz).
//
// Divergências (port):
//  - o PS2 roda o tick do driver numa thread do EE acordada pelo IOP (SNDDRV_thread) e protege a API com um mutex e o
//    contador de seção crítica; aqui tudo roda na thread do jogo (snd_tick, chamado no ritmo do áudio) e os mutexes
//    não fazem nada (o contador de seção crítica continua contando, como no PS2);
//  - o SIF (RPC, comandos, DMA do EE ao IOP) vira chamadas diretas ao IOP portado (driving/sound/snddrv_iop.h), na
//    mesma ordem; as transferências de amostras completam na hora (o PS2 espera a interrupção do IOP);
//  - os ponteiros de função das listas de clientes e dos ganchos de saída são nativos.

#include <cstdint>

#include "platform/ptr32.h"

namespace drv {

using nf::P32;

// Um trecho do envelope de um timbre (no banco): ticks do servidor de 100 Hz (negativo: para sempre) e o nível 0..127.
struct SndEnvSegment {
    int32_t ticks;
    int32_t level;
};
static_assert(sizeof(SndEnvSegment) == 8);

// SNDVOICEI: uma voz lógica (0x88 bytes, o vetor no heap do SND, 0x00357454).
struct SndVoice {
    int32_t handle;             // +0x00 índice | geração (0x003574f8, += 0x100 a cada alocação); -1 numa escrava
    int16_t channels_v[6];      // +0x04 as vozes de cada canal do som (a mestre guarda todas)
    int16_t patch;              // +0x10
    int16_t bank;               // +0x12
    int32_t frames;             // +0x14
    int32_t loop_end;           // +0x18
    uint16_t azimuth;           // +0x1c o embutido + o das opções
    int16_t elevation;          // +0x1e
    uint16_t sample_rate;       // +0x20
    uint8_t sample_rep;         // +0x22 5 = PS-ADPCM (o padrão do PS2), 10 = EA-XA
    uint8_t channels;           // +0x23
    uint16_t render;            // +0x24 o modo de render escolhido (0x28: SPU2, 0x24: EE, 0x120: IOP)
    uint8_t key;                // +0x26 a "tecla" do som de vários timbres (0: um timbre)
    uint8_t key_last;           // +0x27 o último timbre do som
    int16_t master;             // +0x28 a mestre numa escrava, -1 na mestre
    uint8_t priority;           // +0x2a 0..100 (101: fluxos)
    uint8_t pad2b;
    uint32_t tick;              // +0x2c o tick da alocação (a ordem do roubo)
    int32_t fade_step;          // +0x30 16.16 por tick
    int32_t fade_target;        // +0x34
    int32_t fade;               // +0x38 16.16
    int32_t env_step;           // +0x3c
    int32_t env;                // +0x40 16.16
    int32_t env_ticks;          // +0x44 ticks que faltam no trecho
    int8_t builtin_vol;         // +0x48 o volume do timbre x a velocidade / 127
    int8_t vol;                 // +0x49 o final, 0..127
    uint16_t builtin_azimuth;   // +0x4a
    uint16_t channel_azimuth[6];  // +0x4c
    uint8_t env_count;          // +0x58
    int8_t env_current;         // +0x59
    uint8_t env_release;        // +0x5a
    int8_t prog_vol;            // +0x5b o "dry level" das opções
    int8_t builtin_fx;          // +0x5c [barramento] (o barramento 1 lê os campos seguintes, como o original)
    int8_t fx_level;            // +0x5d [barramento]
    int16_t fx_send;            // +0x5e [barramento]
    int8_t bend;                // +0x60
    uint8_t vol_lfo_len;        // +0x61
    uint8_t pitch_lfo_len;      // +0x62
    uint8_t vol_lfo_pos;        // +0x63
    uint8_t pitch_lfo_pos;      // +0x64
    uint8_t in_use;             // +0x65 1 tocando, 2 o último timbre de um som (libera depois)
    uint16_t time_mult;         // +0x66
    P32<SndEnvSegment> env_table;  // +0x68 no banco
    P32<int8_t> vol_table;      // +0x6c
    P32<int8_t> bend_table;     // +0x70
    P32<int8_t> vol_lfo;        // +0x74
    P32<int8_t> pitch_lfo;      // +0x78
    int16_t pitch_lfo_depth;    // +0x7c
    int16_t bend_range;         // +0x7e x 100
    int16_t detune;             // +0x80 centésimos
    uint16_t detune_linear;     // +0x82 4.12 (0 = recalcular)
    uint16_t pitch_mult;        // +0x84 4.12 das opções
    uint16_t pitch;             // +0x86 o final
};
static_assert(sizeof(SndVoice) == 0x88);

// SNDIPATCHHEADER: um timbre lido pelo SNDI_parsetimbre (0xb8 bytes na pilha). [etiqueta]
struct SndPatchHeader {
    uint16_t detune_random;     // +0x00 [0x24]
    uint8_t platform_version;   // +0x02 [0x80] (2)
    int8_t channels;            // +0x03 [0x82] (1)
    uint16_t tune;              // +0x04 [0x10]
    uint16_t tune_random;       // +0x06 [0x11]
    int8_t velocity_low;        // +0x08 [0x01]
    int8_t velocity_high;       // +0x09 [0x02] (0x7f)
    int8_t key_low;             // +0x0a [0x03]
    int8_t key_high;            // +0x0b [0x04] (0x7f)
    int8_t priority;            // +0x0c [0x06]
    int8_t root_key;            // +0x0d [0x07] (0x3c)
    uint8_t env_release;        // +0x0e [0x08] (0xff)
    uint8_t env_count;          // +0x0f [0x09] (1)
    int8_t bend_range;          // +0x10 [0x0a]
    int8_t pan;                 // +0x11 [0x0c] (0x40)
    int8_t pan_random;          // +0x12 [0x0d]
    uint8_t sample_rep;         // +0x13 [0xa0] (5: PS-ADPCM)
    int8_t volume;              // +0x14 [0x0e] (0x7f)
    int8_t volume_random;       // +0x15 [0x0f]
    int8_t fx_level;            // +0x16 [0x13]
    int8_t user_count;          // +0x17 [0x14] quantas
    uint16_t render;            // +0x18 [0x8c] (8: a SPU2)
    int8_t env_start;           // +0x1a [0x1c] (0x7f)
    uint8_t vol_lfo_len;        // +0x1b [0x1e]
    int8_t vol_lfo_random;      // +0x1c [0x1f]
    uint8_t pitch_lfo_len;      // +0x1d [0x21]
    uint8_t pitch_lfo_random;   // +0x1e [0x23]
    uint8_t pan_mult;           // +0x1f [0x25] (1)
    P32<int8_t> vol_table;      // +0x20 [0x12] dado + valor
    P32<int8_t> bend_table;     // +0x24 [0x17]
    P32<SndEnvSegment> env_table;  // +0x28 [0x19] (o padrão: 0x00357610 + valor)
    P32<int8_t> vol_lfo;        // +0x2c [0x1d]
    P32<int8_t> pitch_lfo;      // +0x30 [0x20]
    P32<uint8_t> user_data[4];  // +0x34 [0x14]
    int32_t user_size[4];       // +0x44
    uint16_t pitch_lfo_depth;   // +0x54 [0x22]
    uint16_t sample_rate;       // +0x56 [0x84] (22050)
    int32_t frames;             // +0x58 [0x85]
    int32_t loop_start;         // +0x5c [0x86] (-1)
    int32_t loop_end;           // +0x60 [0x87] (-1)
    P32<int32_t> sample_data;   // +0x64 [0x8a] os dados da etiqueta (o SNDbankadd grava ali o endereço na SPU2)
    int32_t sample_offset[6];   // +0x68 [0x88] [0x89] [0x94] [0x95] [0xa2] [0xa3]
    int32_t tag1a;              // +0x80 [0x1a] (-1)
    P32<uint8_t> stretch[6];    // +0x84 [0x98] [0x99] [0x9a] [0x9b] [0xa4] [0xa5]
    uint16_t stretch_size[6];   // +0x9c
    uint16_t azimuth[6];        // +0xa8 o padrão do canal + [0x9c] [0x9d] [0x9e] [0x9f] [0xa6] [0xa7]
    P32<uint8_t> start;         // +0xb4 onde as etiquetas começaram
};
static_assert(sizeof(SndPatchHeader) == 0xb8);

// SNDPLAYOPTS (0x18; o padrão do SNDplaysetdef).
struct SndPlayOpts {
    int8_t vol;                 // +0x00 0x7f
    int8_t bend;                // +0x01 0x40
    int8_t key;                 // +0x02 0x3c
    int8_t velocity;            // +0x03 0x7f
    int8_t prog_vol;            // +0x04 0x7f
    int8_t fx_level;            // +0x05 0x7f
    uint8_t pad06[2];
    uint16_t azimuth;           // +0x08
    uint16_t elevation;         // +0x0a
    uint16_t pitch_mult;        // +0x0c 0x1000
    uint16_t time_mult;         // +0x0e 0x1000
    uint16_t tempo_mult;        // +0x10 0x1000
    uint16_t distort;           // +0x12
    uint16_t lowpass;           // +0x14 0xffff
    uint16_t highpass;          // +0x16
};
static_assert(sizeof(SndPlayOpts) == 0x18);

// Um barramento de efeito (0x14): o do EE (0x0035745c), o do IOP (0x00357470), o da SPU2 (0x00357484).
struct SndFxBus {
    uint16_t mode;              // +0x00
    int8_t level;               // +0x02
    uint8_t pad03;
    int32_t delay;              // +0x04
    int32_t feedback;           // +0x08
    float balance[2];           // +0x0c o "output balance" (só o da SPU2 é lido: 0x00357490)
};
static_assert(sizeof(SndFxBus) == 0x14);

// Um banco (0x10, o vetor no heap do SND, 0x00357458).
struct SndBankSlot {
    P32<uint8_t> header;        // +0x00 o BNKl
    uint32_t iop_mem;           // +0x04 a memória no IOP (os dados de +0x10 do BNKl v5)
    uint32_t spu_mem;           // +0x08 o endereço na RAM da SPU2 das amostras
    uint8_t flag;               // +0x0c 1 enquanto carrega
    uint8_t pad[3];
};
static_assert(sizeof(SndBankSlot) == 0x10);

// O cabeçalho do heap do SND (SNDMEMI), no começo da memória que o SNDSYSI_init recebe (0x18 bytes).
struct SndMemHeap {
    P32<uint8_t> base;          // +0x00 a primeira fronteira de 16 depois do cabeçalho
    P32<int32_t> table;         // +0x04 o registro 0 (os registros crescem para baixo: {deslocamento, tamanho})
    int32_t size;               // +0x08
    int32_t limit;              // +0x0c bytes que os blocos podem usar (diminui 8 por registro)
    int32_t low_water;          // +0x10
    int32_t count;              // +0x14 menos o número de registros
};
static_assert(sizeof(SndMemHeap) == 0x18);

// ---- a API --------------------------------------------------------------------------------------------------------

// As opções (SNDSYSOPTS, 0x120 bytes: as capacidades +0, o que se pode mudar +0x20, os vetores +0x108).
struct SndSysOpts {
    uint8_t raw[0x120];
};

// [driving] 0x00307108 SNDSYS_getopts
int32_t SNDSYS_getopts(SndSysOpts* opts);
// [driving] 0x003072e8 SNDSYS_setopts
int32_t SNDSYS_setopts(const SndSysOpts* opts);
// [driving] 0x003074c8 SNDSYSI_init: mem/size do heap (o ASystem dá 0x10000 bytes).
int32_t SNDSYSI_init(uint8_t* mem, int32_t size);
// [driving] 0x00307688 SNDSYS_restore
int32_t SNDSYS_restore();
// [driving] 0x003077b0 SNDSYS_inited
uint8_t SNDSYS_inited();
// [driving] 0x00307850 SNDSYSI_timerservice(int)
void SNDSYSI_timerservice(uint32_t rate);
// [driving] 0x00307940 SNDSYSI_100hzserver(void)
void SNDSYSI_100hzserver();
// [driving] 0x00307bb0 SNDSYS_entercritical
void SNDSYS_entercritical();
// [driving] 0x00307be0 SNDSYS_leavecritical
void SNDSYS_leavecritical();
// [driving] 0x00311140 SNDSYS_service
int32_t SNDSYS_service();
// [driving] 0x00309f30 SNDmastervol (o volume mestre 0..127, todas as vozes de novo)
int32_t SNDmastervol(int32_t vol);

// [driving] 0x0030a8e8 SNDplaysetdef
int32_t SNDplaysetdef(SndPlayOpts* opts);
// [driving] 0x003086c8 SNDBANK_play: o handle, ou um erro negativo.
int32_t SNDBANK_play(int32_t bank, int32_t patch, const SndPlayOpts* opts);
// [driving] 0x0030a660 SNDstop
int32_t SNDstop(int32_t handle);
// [driving] 0x0030a6b8 SNDstopall
void SNDstopall();
// [driving] 0x0030a730 SNDover: 1 se o som acabou.
int32_t SNDover(int32_t handle);
// [driving] 0x0030a3d0 SNDvol
int32_t SNDvol(int32_t handle, int32_t vol);
// [driving] 0x0030a310 SNDautovol: um desvanecimento até vol em ticks do servidor de 100 Hz.
int32_t SNDautovol(int32_t handle, int32_t ticks, int32_t vol);
// [driving] 0x0030a4a0 SNDpitchmult
int32_t SNDpitchmult(int32_t handle, uint32_t mult);
// [driving] 0x0030a750 SND3dpos
int32_t SND3dpos(int32_t handle, int32_t azimuth, int32_t elevation);
// [driving] 0x0030a548 SNDfxlevel
int32_t SNDfxlevel(int32_t handle, int32_t bus, int32_t level);
// [driving] 0x0030a5f8 SNDCTRL_lowpass
int32_t SNDCTRL_lowpass(int32_t handle, int32_t cutoff);
// [driving] 0x0030a088 SNDfxinitbus
int32_t SNDfxinitbus(uint32_t bus, int32_t level, int32_t mode, int32_t delay, int32_t feedback);
// [driving] 0x0030a178 SNDfxmasterlevel
int32_t SNDfxmasterlevel(uint32_t bus, int32_t level);

// [driving] 0x00308088 SNDbankadd: *bank recebe o índice; 7 (amostras na SPU2: o jogo pode ficar só com o cabeçalho),
// 8 (sem amostras) ou um erro negativo.
int32_t SNDbankadd(int32_t* bank, uint8_t* data);
// [driving] 0x00308300 SNDbankremove (-1: todos)
int32_t SNDbankremove(int32_t bank);
// [driving] 0x00308528 SNDbankheadersize
int32_t SNDbankheadersize(int32_t bank);
// [driving] 0x00308548 SNDbankheadercopy
int32_t SNDbankheadercopy(uint8_t* dest, int32_t bank);
// [driving] 0x003085d0 SNDbankpatpresent
int32_t SNDbankpatpresent(int32_t bank, int32_t patch);

// ---- o que os testes e a conferência leem -------------------------------------------------------------------------

// As etiquetas: o leitor do SNDI_gettag (SNDTAGINFO).
struct SndTagReader {
    const uint8_t* cursor = nullptr;  // +0x00
    int32_t tag = 0;                  // +0x04
    int32_t value = 0;                // +0x08
    const uint8_t* data = nullptr;    // +0x0c
    uint32_t length = 0;              // +0x10
};
// [driving] 0x00312270 SNDI_getb(void *, int)
int32_t SNDI_getb(const uint8_t* data, int32_t length);
// [driving] 0x00312300 SNDI_gettag(SNDTAGINFO *)
int32_t SNDI_gettag(SndTagReader& r);
// [driving] 0x0030b318 SNDI_parsetimbre(void **, SNDIPATCHHEADER *): 1 se outro timbre vem depois.
int32_t SNDI_parsetimbre(const uint8_t** cursor, SndPatchHeader* h);
// [driving] 0x003115b0 SNDBANKI_getppatch(BANKVER5 *, int)
uint8_t* SNDBANKI_getppatch(uint8_t* bank, int32_t patch);

// Clientes do servidor do 100 Hz e do serviço (as listas do SND; ninguém do jogo as usa).
using SndClient = void (*)();
void iSNDserveraddclient(SndClient fn);
void iSNDserverremoveclient(SndClient fn);

}  // namespace drv
