#pragma once
// nf_driving_check, parte 8: a câmera do jogador (tests/driving_check_camera.cpp): o código ORIGINAL do EE no
// interpretador MIPS (com a VU0) lado a lado com o porte (driving/camera).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo bit a bit). `frames`: quadros de câmera por carro.
int run_camera_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                      int frames, uint32_t seed);

}  // namespace drvcheck
