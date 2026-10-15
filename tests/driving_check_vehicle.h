#pragma once
// nf_driving_check, parte 12: o carro desenhado e o que o cerca (tests/driving_check_vehicle.cpp): o código ORIGINAL do
// DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte de driving/render/vehicle.h,
// render/shadow.h, game/feedback.h, game/input_config.h e as luzes do PBondCar (game/bond_car.h). docs/driving.md
// seção 16.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada função.
int run_vehicle_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);

}  // namespace drvcheck
