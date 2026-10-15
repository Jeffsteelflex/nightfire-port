#pragma once
// nf_driving_check, parte 14: o mundo vivo (tests/driving_check_traffic.cpp; docs/driving.md seção 18). O código ORIGINAL
// do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte: a rede de estradas de Paris e as
// consultas dela (driving/ai/road_network.h), o navegador (road_nav.h), o nascimento do tráfego (road_spawn.h), o passo
// do controle de um carro do tráfego (ground_vehicle.h) e os objetos de cena que o carro derruba (o Smackable).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: os casos sorteados de cada tipo.
int run_traffic_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed);
// Parte 17 (tests/driving_check_ai.inc): a IA dos elementos de Paris e o dano do carro, na mesma máquina da parte 14.
int run_ai_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                  int cases, uint32_t seed);

}  // namespace drvcheck
