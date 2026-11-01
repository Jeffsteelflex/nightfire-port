#pragma once
// O mixador do IOP do SNDDRV.IRX (mix/smixer.c, smixptch.c, smixfram.c e os filtros do módulo; docs/driving.md seção 29),
// portado do MIPS do módulo (as decompilações em out/agent_drv_sound/decomp_irx do checkout principal; marcas
// `// [snddrv] 0x<endereço no IRX> <Nome>`).
//
// Como o PS2 faz: as vozes de modo de render 0x100 (os timbres de EA-XA da memória do IOP: os bancos h* dos personagens,
// os a* do ambiente e três de cada g*, como o SFX_OffRN e o SFX_OffRoadSkid do carro) são as 6 vozes do mixador do IOP.
// A cada tick o SNDIOP_updateiopvoices lê da tagbuf (+0x1a0, 0x10 bytes por voz: o endereço da amostra no IOP, o pitch,
// os volumes por saída e o do reverb) o que mudou: uma amostra nova para a voz (MIX_stop, MIX_playinit com o cabeçalho
// da amostra: o formato no byte +1 (0x0a: EA-XA), o laço em +4 e +8, as amostras em +0xc, os dados em +0x10; MIX_play),
// o pitch (MIX_setpitch, o do EE vezes 16), os ganhos (SNDMIX_setdrygain: o volume com sinal, x 0x102 ou << 8), e
// devolve a posição de cada voz (MIX_getframe). No rótulo 1 da tagbuf o SNDIOP_mix chama o MIX_audioslice: cada voz
// tocando puxa a sua cadeia de filtros (o reamostrador 0xa0: linear ou o mais perto, pela qualidade; o desempacotador
// de EA-XA 0xf0: o decxa16c, o ADPCM da EA em inteiros com a tabela xafp de 0x0000b470; em laço o unpackxalf e o
// unpackxalfsection, que volta ao bloco do começo do laço com o estado guardado), rampa os ganhos em 16 amostras quando
// mudam (SNDMIXI_volramp, MIXI_interpolatemix: passos de 1/17) e soma em inteiros (mixiop: amostra x (ganho >> 8)) nos
// acumuladores de cada saída, que o SNDIOP_ee48_iop48 junta às amostras do EE (>> 7) e satura. Uma voz que para leva o
// último valor a zero em 16 amostras (MIXI_interpolateto0).
//
// Divergências (port, contadas em iop_mix_stats().guards):
//  - o reverb do mixador do IOP (o sndfx: MIX_initreverb, SNDMIXI_fxinit/fxadd, SNDMIXI_initfx2) não está portado: o
//    jogo só o desliga (o barramento do AFX é o 0x40, da SPU2; o SNDfxinitbus(0, ...) do começo manda o modo 0, que o
//    RPC 2 de 4 bytes faz MIX_restorereverb), então os ganhos molhados ficam guardados e nunca são somados, como no PS2;
//  - os outros formatos (PCM de 8 e 16 bits, MicroTalk) não estão portados: a voz termina na primeira fatia (nenhum
//    timbre do disco no IOP os usa: os 249 são EA-XA); uma voz sem cadeia também (o PS2 pularia para o endereço 0);
//  - um bloco do EA-XA além da memória do IOP do port dá zeros;
//  - um filtro 4..15 do EA-XA lê no IOP além da tabela xafp (a .sbss, zerada no carregamento): o port usa zeros;
//  - os objetos (as vozes e os filtros) são do port, não os blocos do SNDMEMI; o IOP não roda no interpretador (a
//    conferência é o que chega a ele, a parte 13, e o que sai, tests/test_driving_sound.cpp).

#include <cstdint>

namespace drv {

// [snddrv] 0x00000fc8 MIX_create (os parâmetros do SNDIOP_init: a taxa, as vozes, as saídas, o aviso de voz livre) e
// 0x0000115c MIX_reset.
void IopMix_create(uint32_t rate, int voices, int outputs, void (*voice_free)(int32_t));
// [snddrv] 0x0000117c MIX_setresamplequality
void IopMix_setresamplequality(int quality);
// [snddrv] 0x0000118c MIX_playinit(voz, formato, canais, dados, ...): o desempacotador da voz pelo formato.
// `data_end` (port): o fim da memória do IOP, para as leituras de dados inválidos.
void IopMix_playinit(int voice, int format, int channels, const uint8_t* data, const uint8_t* data_end, int32_t frames,
                     int32_t loop_start, int32_t loop_end, int chan);
// [snddrv] 0x00001388 MIX_play
void IopMix_play(int voice);
// [snddrv] 0x00001444 MIX_stop
void IopMix_stop(int voice);
// [snddrv] 0x00002160 MIX_setpitch (16.16, até 4)
void IopMix_setpitch(int voice, int32_t pitch);
// [snddrv] 0x00002100 MIX_getframe (-1: a voz não tem desempacotador)
int32_t IopMix_getframe(int voice);
// [snddrv] 0x00001564 SNDMIX_setdrygain / 0x000015a4 SNDMIX_setwetgain
void IopMix_setdrygain(int voice, int output, int32_t gain);
void IopMix_setwetgain(int voice, int fx, int32_t gain);
// [snddrv] 0x00001634 MIX_audioslice: `outputs[k]` os acumuladores de cada saída (n amostras).
void IopMix_audioslice(int32_t* const* outputs, int n);
// A voz está tocando (o estado 2).
bool IopMix_playing(int voice);

struct IopMixStats {
    uint64_t starts = 0, stops = 0, slices = 0, voice_slices = 0, samples = 0, ramps = 0, ended = 0;
    uint64_t guards = 0;  // os formatos e o reverb não portados, os filtros do EA-XA além da tabela
    // port (o relatório): o pico das saídas (já >> 7, a escala das amostras de 16 bits) e as fatias com voz, a
    // primeira e a última.
    int32_t peak = 0;
    uint64_t active_slices = 0, first_active = 0, last_active = 0;
};
IopMixStats& iop_mix_stats();

// [snddrv] 0x0000a390 decxa16c (exposto para os testes): `count` amostras (múltiplo de 28) de blocos de 15 bytes, o
// estado s1/s2, a saída em int32. Devolve o resto negativo como o original (count - 28 x blocos).
int32_t iop_decxa16c(int32_t count, int32_t& s1, int32_t& s2, const uint8_t*& in, int32_t*& out);

}  // namespace drv
