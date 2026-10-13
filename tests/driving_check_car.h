#pragma once
// nf_driving_check, parte 6: o carro (PBondCar: ProcessPhysics com AddWheelForces, ImproveLanding e TwoWheelStunt,
// CalculateRPM, GetControllerInput) do código ORIGINAL do EE no interpretador MIPS com a VU0, sobre o chão de verdade de
// Paris (a cópia da parte 5) e o manejo do Vanquish do disco, lado a lado com o porte (tests/driving_check_car.cpp).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo bit a bit).
int run_car_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                   int cases, uint32_t seed);

}  // namespace drvcheck
