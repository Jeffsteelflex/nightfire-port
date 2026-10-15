#pragma once
// O "hardware" do som das missões de carro (só do port): a SPU2 do PC (platform/spu2.h, uma instância só daqui, não a
// do jogo a pé) atrás das funções do LIBSD.IRX que o SNDDRV.IRX portado chama (driving/sound/snddrv_iop.h), e o relógio:
// snd_render anda as amostras e, a cada 512 (um tick do driver: 48000 / 512 = 93,75 por segundo, a metade do buffer
// em laço do IOP), roda o IOP e o EE (driving/sound/snd_ps2.cpp: snd_driver_tick) antes de a SPU2 tocar o pedaço.
//
// O que o IOP escreve na metade do buffer (as amostras do mixador do EE e do IOP) entra na entrada de dados do núcleo 0
// (Spu2::input_push) com um tick de atraso, como no PS2 (a SPU2 toca uma metade enquanto o IOP enche a outra).

#include <cstdint>

#include "platform/spu2.h"

namespace drv {

class SndLibsd;

nf::Spu2& snd_spu();
SndLibsd* snd_spu_libsd();
// Volta ao estado do boot (a SPU2 zerada, a fila da entrada vazia, o relógio no começo de um tick).
void snd_spu_power_on();
// Anda frames amostras de 48 kHz (estéreo intercalado em out; nulo: só anda), rodando os ticks do driver no caminho.
void snd_render(int16_t* out, int frames);
// Os ticks do driver desde o boot.
uint64_t snd_ticks();

// [driving] 0x0030dc58 SNDDRV_thread (uma volta) com a volta do IOP antes (driving/sound/snd_ps2.cpp).
void snd_driver_tick();

}  // namespace drv
