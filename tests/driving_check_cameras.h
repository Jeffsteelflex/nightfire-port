#pragma once
// nf_driving_check, parte 26 (tests/driving_check_cameras.cpp; docs/driving.md seção 30): a câmera e os gatilhos que
// faltavam a Paris, o código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o
// porte: as âncoras do RWorldCamera (o GetAnchorLinearVelocity com o carro no trilho, os corpos simples), os modos
// Bumper, Dashboard, Tumble e Ellipse, a entrada do carro, as animações relativas ao carro (o 97 EPlayCarCameraAnim e os
// 'Cams' do vanquish no RAnimEngine), o SetOldCameraMode com transição, os corpos simples perto da câmera, os gatilhos
// tocados pelos corpos simples e pelas instâncias dos trilhos e o RSniperZoom. `--so cameras` roda só ela.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo; `frames`: os quadros das
// passadas da câmera.
int run_cameras_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, int frames, uint32_t seed);

}  // namespace drvcheck
