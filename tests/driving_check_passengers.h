#pragma once
// nf_driving_check, parte 22 (`--so passageiros`): os passageiros dos carros da IA de Paris (docs/driving.md seção 26).
// O código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte
// (driving/game/ai_passengers.h, driving/anim/actor.h, driving/game/bond_car.h), com estados sorteados.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam.
int run_passenger_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                         int cases, uint32_t seed);

}  // namespace drvcheck
