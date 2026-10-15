#pragma once
// As cortinas de visibilidade na VU0: o microprograma VU0_culling do DRIVING.ELF (o terceiro dos três programas do
// .vutext, 189 instruções, enviado por UploadCullingMicrocode a cada GenerateCurtainsAndNodes; docs/driving.md seção 15)
// portado à mão para C++, como o VU0_physics em physics/vu0_physics.h.
//
// O programa tem três pontos de entrada (instruções; o DRIVING.SYM dá os rótulos em bytes):
//   0x000 (sem rótulo; o AddCurtain o chama): a cortina vf01 (começo) e vf02 (fim) vista do olho vf03. Se o olho está
//         atrás do plano da parede, vi02 = 0 e nada é gravado; senão grava em 4 x vi01 .. 4 x vi01 + 3 os quatro planos
//         transpostos (a coluna k é o plano k: a normal em x, y, z e a distância na linha 3): a parede, o plano do olho
//         pelo começo, o do olho pelo fim e o de trás pelo fim, e vi02 = 1;
//   0x092 CheckAgainstVisCurtains: a esfera vf01 (o raio no w) e a margem vf02.x contra as vi01 cortinas da memória:
//         vi02 = 0 se para alguma delas as três primeiras distâncias + raio e a quarta + margem são negativas (os quatro
//         bits de sinal do MAC: FMAND 0xf0), senão 1;
//   0x0a8 CheckPositionAgainstVisCurtains: o mesmo para o ponto vf01, sem raio nem margem.
// Os portes leem e escrevem o mesmo estado da VU0 que o microcódigo (os registradores, os temporários, ACC, Q, os VI e a
// memória de dados), na mesma ordem e com a mesma aritmética (platform/vu0_macro.h); os indicadores (MAC, estado) não
// fazem parte do contrato, salvo o que o FMAND põe em vi13/vi14. O nf_driving_check (parte 11) confere tudo contra o
// microcódigo original no interpretador da VU.
//
// O PS2 divide a VU0 entre a física e as cortinas (UploadCullingMicrocode troca o programa a cada quadro de desenho e o
// SimulateGame põe o da física de volta): aqui também, as duas usam drv::vu0() e o mesmo modo (Native ou Interpreted,
// set_vu0_mode).

#include <cstdint>
#include <vector>

#include "platform/vu_interp.h"

namespace drv {

// O microcódigo do VU0_culling (os pares de 64 bits a partir da instrução 0), para o modo Interpreted.
void set_vu0_culling_program(std::vector<uint8_t> code);
bool has_vu0_culling_program();
// [driving] 0x0022f3e0 UploadCullingMicrocode(bool): VU0_UploadMicrocode(VU0_culling_dma_start, ...); no modo
// Interpreted copia o microcódigo para a memória de microprograma de vu0().
void UploadCullingMicrocode(bool wait);
// vcallms do programa das cortinas (o porte em C++ ou o interpretador, pelo modo).
void vu0_culling_callms(uint32_t instr);

// Os portes à mão.
void VU0MICRO_AddCurtain(ps2::VuMachine& vu);                       // 0x000
void VU0MICRO_CheckAgainstVisCurtains(ps2::VuMachine& vu);          // 0x092
void VU0MICRO_CheckPositionAgainstVisCurtains(ps2::VuMachine& vu);  // 0x0a8
// Roda o porte do ponto de entrada (0x000, 0x092 ou 0x0a8); false se não for um deles.
bool vu0_culling_native(ps2::VuMachine& vu, uint32_t instr);

}  // namespace drv
