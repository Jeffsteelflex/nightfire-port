#pragma once
// O mixador do SND no EE (MIX_*, MIXI_*, SNDMIX*, e os filtros SFILTER_* que os fluxos usam), portado do MIPS do
// DRIVING.ELF (docs/driving.md seções 17 e 27) e conferido contra o original na parte 23 do nf_driving_check.
//
// O mixador do EE toca as vozes de modo de render 4: no jogo, só os fluxos (AStream: a música dos .MUS e as falas e os
// ambientes dos .SPE), que chegam em pacotes de EA-XA pelo SNDPKTPLAY (driving/sound/snd_stream.h). Cada voz (0x60
// bytes, no heap do SND) tem uma cadeia de filtros (SFILTER, os objetos também no heap do SND, com o leiaute do PS2) da
// saída para a fonte, em ordem crescente do campo +0x18: o passa-baixa RC (0x28, o SNDSTRM_lowpass), o reamostrador
// (0xa0, sempre: o MIX_setpitch o cria) e o desempacotador de EA-XA (0xf0). A cada fatia de até 512 amostras o
// MIX_audioslice puxa a cadeia de cada voz tocando para o buffer B, rampa os ganhos nas 16 primeiras amostras quando
// mudaram (SNDMIXI_volramp, MIXI_interpolatemix: passos de 1/17), soma no buffer float de cada saída com o ganho seco
// (mixc, a VU0 em modo macro) e converte cada saída para 16 bits na tagbuf (SFILTER_ft16: satura em +-32767, trunca).
// Uma voz que para deixa o seu último valor x o ganho para o começo da fatia seguinte, que o leva a zero em 16
// amostras (MIXI_interpolateto0: o estalo do fim).
//
// As contas são as da FPU do EE e da VU0 (platform/ps2_float.h), na ordem do original. Os ponteiros de função dos
// filtros ficam no objeto como o endereço do PS2 (kFn*), e o porte despacha por ele.
//
// Divergências (contadas em mix_guards()):
//  - o mixc e o SFILTER_ft16 só param quando o ponteiro bate no fim exato (blocos de 16 amostras): com outra contagem o
//    PS2 andaria pela memória sem fim; o porte arredonda para cima e conta;
//  - os filtros que nenhum fluxo usa (os desempacotadores de 16 bits, de EA-XA de banco e de MicroTalk, o passa-alta,
//    a distorção, a esticada de tempo, o reverb do mixador do EE) não estão portados: pedi-los conta e não faz nada.
//    Nenhum banco do disco toca no mixador do EE, e o AFX do jogo só liga o reverb da SPU2 (o barramento 0x40);
//  - o reamostrador com pitch diferente de 1,0 é portado com o erro do original: a fase fracionária não anda entre as
//    chamadas (o SFILTER_rsf grava de volta o valor de entrada & 0xffff) e a história guardada são as 4 PRIMEIRAS
//    amostras do bloco de entrada. Os fluxos tocam a 48 kHz num mixador de 48 kHz (pitch 0x10000: só cópia).

#include <cstdint>

#include "platform/ptr32.h"

namespace drv {

struct CEAXABLKDecf;

// MIXCREATEPARAMS (o que o SNDPLATFORM_init monta).
struct SndMixCreate {
    uint32_t rate = 0;          // +0x00
    uint8_t voices = 0;         // +0x04
    uint8_t outputs = 0;        // +0x05
    uint8_t quality = 0;        // +0x06
    void (*voice_free)(int32_t) = nullptr;  // +0x08
};

// Os endereços do PS2 das funções que os objetos guardam (o despacho do porte).
enum : uint32_t {
    kFnUnpackXapf = 0x00315fe8,
    kFnUnpackXapfRestore = 0x00316158,
    kFnRsf = 0x00317510,
    kFnLpfRc = 0x003166e0,
    kFnFt16 = 0x003165d0,
    kFnMixc = 0x00317b78,
};

// SFILTER: o cabeçalho comum (0x1c bytes).
struct SFilter {
    uint32_t process;           // +0x00 a função (kFn*)
    uint32_t restore;           // +0x04 a de desmontar (0: nenhuma)
    nf::P32<SFilter> next;      // +0x08 de onde este puxa (o próximo para a fonte)
    nf::P32<SFilter> in2;       // +0x0c a segunda entrada (o SFILTER_cleanup a segue)
    nf::P32<SFilter> prev;      // +0x10
    uint32_t w14;               // +0x14
    uint16_t order;             // +0x18 a posição na cadeia (crescente da saída para a fonte)
    uint8_t chan;               // +0x1a o canal (o desempacotador; o resto passa adiante)
    uint8_t cleaned;            // +0x1b
};
static_assert(sizeof(SFilter) == 0x1c);

// O desempacotador de EA-XA em pacotes (SFILTER_unpackxapf, 0x38 bytes).
struct SFilterXapf {
    SFilter h;
    nf::P32<CEAXABLKDecf> dec;  // +0x1c (0xa8 bytes do heap do SND)
    nf::P32<const int16_t> pkt; // +0x20 o pacote do canal (dois s16 de estado e os blocos)
    int32_t frames;             // +0x24 as amostras do pacote
    int32_t decoded;            // +0x28 as decodificadas do pacote
    int32_t player;             // +0x2c o SNDPKTPLAY
    int32_t pending;            // +0x30 as decodificadas desde o último SNDPKTPLAYI_freeframes
    uint8_t chan;               // +0x34
    uint8_t pad35[3];
};
static_assert(sizeof(SFilterXapf) == 0x38);

// O reamostrador linear (SFILTER_rsf, 0x38 bytes).
struct SFilterRsf {
    SFilter h;
    int32_t pitch;              // +0x1c 16.16 (0x10000: cópia)
    uint32_t frac;              // +0x20
    int16_t primed;             // +0x24
    int16_t delay;              // +0x26
    float hist[4];              // +0x28 as 4 amostras lidas à frente
};
static_assert(sizeof(SFilterRsf) == 0x38);

// O passa-baixa RC (SFILTER_lpfRC, 0x28 bytes).
struct SFilterLpfRc {
    SFilter h;
    float y;                    // +0x1c
    float a;                    // +0x20 o peso do anterior
    float b;                    // +0x24 o da entrada
};
static_assert(sizeof(SFilterLpfRc) == 0x28);

// Uma voz do mixador (0x60 bytes, no heap do SND a partir de 0x0035e658).
struct MixVoice {
    uint8_t state;              // +0x00 0 livre, 1 iniciada, 2 tocando
    uint8_t ramp;               // +0x01 os ganhos mudaram (o SNDMIXI_volramp)
    uint8_t pad02[2];
    float dry[6];               // +0x04 os ganhos secos em uso, por saída
    float dry_target[6];        // +0x1c os pedidos
    float wet[1];               // +0x34 o molhado em uso (o barramento 0)
    float wet_target[1];        // +0x38
    float last;                 // +0x3c a última amostra que saiu (o estalo do fim)
    nf::P32<SFilter> chain;     // +0x40 a cabeça da cadeia (a saída)
    int32_t w44;                // +0x44 (o +0x24 dos parâmetros do desempacotador)
    nf::P32<SFilter> unpack;    // +0x48
    nf::P32<SFilter> stretch;   // +0x4c
    nf::P32<SFilter> rsf;       // +0x50
    nf::P32<SFilter> distort;   // +0x54
    nf::P32<SFilter> lowpass;   // +0x58
    nf::P32<SFilter> highpass;  // +0x5c
};
static_assert(sizeof(MixVoice) == 0x60);

// [driving] 0x00312b08 MIX_create
int32_t MIX_create(const SndMixCreate& p);
// [driving] 0x00312e10 MIX_destroy
void MIX_destroy();
// [driving] 0x003137b8 MIX_audio: frames amostras de cada saída em out[i] (16 bits).
void MIX_audio(int16_t* const* out, int32_t frames);
// [driving] 0x00312f40 MIX_playinit: os 13 argumentos do PS2 (a0..t3 e os cinco da pilha); stretch é o t1 (os dados da
// esticada de tempo do canal: 0 nenhum).
void MIX_playinit(int32_t mv, uint8_t rep, int32_t mode, const uint8_t* data, int32_t t0, uint32_t stretch,
                  int32_t channels, int32_t frames, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12,
                  uint8_t chan);
// [driving] 0x00313178 MIX_play
void MIX_play(int32_t mv);
// [driving] 0x00313248 MIX_stop
void MIX_stop(int32_t mv);
// [driving] 0x00313f90 MIX_setpitch
void MIX_setpitch(int32_t mv, uint32_t pitch);
// [driving] 0x00313350 SNDMIX_setdrygain
void SNDMIX_setdrygain(float gain, int32_t mv, int32_t output);
// [driving] 0x00313380 SNDMIX_setwetgain
void SNDMIX_setwetgain(float gain, int32_t mv, int32_t bus);
// [driving] 0x00314038 MIX_settimemult
void MIX_settimemult(int32_t mv, int32_t mult);
// [driving] 0x00314068 MIX_setdistortlevel
void MIX_setdistortlevel(int32_t mv, int32_t level);
// [driving] 0x003141e8 MIX_setlowpass
void MIX_setlowpass(float cutoff, int32_t mv);
// [driving] 0x00314120 MIX_sethighpass
void MIX_sethighpass(int32_t mv, int32_t cutoff);
// [driving] 0x00313f70 MIX_setwetbuffer
void MIX_setwetbuffer(uint8_t* buf);
// [driving] 0x00312f30 MIX_setresamplequality
void MIX_setresamplequality(uint8_t q);
// [driving] 0x00318520 MIX_initreverb
void MIX_initreverb(uint32_t rate, const uint8_t* desc);
// [driving] 0x003187b8 MIX_restorereverb
void MIX_restorereverb();

// Vozes do mixador tocando (para os relatórios).
int32_t mix_voices_playing();
// As guardas do cabeçalho.
uint64_t mix_guards();
// O snd_power_on: zera as guardas (o estado do mixador o MIX_create refaz).
void mix_power_on();

// Para a conferência (tests/driving_check_streams.cpp): o estado global do mixador.
struct MixInternals {
    const SndMixCreate* create;     // 0x0035e400
    uint8_t reverb;                 // 0x0035e40f
    uint8_t quality;                // 0x0035e40e
    float* buf_a;                   // 0x0035e52c
    float* buf_b;                   // 0x0035e530
    float* const* outbuf;           // 0x0035e54c
    const float* pending;           // 0x0035e564
    const float* pending_wet;       // 0x0035e57c
    MixVoice* voices;               // 0x0035e658
};
MixInternals mix_internals();

}  // namespace drv
