#pragma once
// nf_driving_check, parte 7: os choques (RigidBody::CollideWithWorld com o WCollider, as barreiras, os cilindros e as
// caixas dos objetos do mundo, ResolveWorldOBBCollision e CalculateAndApplyWorldDamage; RigidBody::CollideWithObject
// com as caixas orientadas e ResolveCollision) do código ORIGINAL do EE no interpretador MIPS com a VU0, sobre o
// mundo de colisão de verdade de Paris, lado a lado com o porte (tests/driving_check_collide.cpp).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo bit a bit).
int run_collide_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);

}  // namespace drvcheck
