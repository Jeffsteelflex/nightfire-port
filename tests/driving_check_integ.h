#pragma once
// nf_driving_check, parte 20 (`--so integracao`): a ligação das armas com a IA de Paris (docs/driving.md seção 24). O
// código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte, em três
// pedaços, cada um no seu arquivo (tests/driving_check_integ_*.cpp, que entram sozinhos no CMake):
//  - tiros (driving_check_integ_shells.cpp): as balas (RayShell, Shell), as janelas (CheckHitWindow), o dano no mundo
//    (Shell::ApplyWorldDamage) e o EFireWeapon das armas dos personagens e da IA;
//  - atores (driving_check_integ_actors.cpp): os atores (ActActor, ActCharacter, ActWeapon, as animações e os eventos
//    delas) e os personagens da IA (AICharacter*, Human, as mãos de Bond);
//  - ia (driving_check_integ_ai.cpp): as armas dos veículos da IA (FireBullets, FireRockets, FireMissiles, TurnTurret),
//    o DoAutoDriveUpdate, o WTargetPicker com os alvos da IA e o Q-Pulse.
// `--so integracao.<pedaço>` roda só um deles.

#include <cstdint>
#include <string>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `only`: vazio (todos) ou o nome de um pedaço (tiros, atores, ia).
int run_integ_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                     int cases, uint32_t seed, const std::string& only);

// Os pedaços (cada um no seu arquivo).
int run_integ_shell_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                           uint32_t vu0_size, int cases, uint32_t seed);
int run_integ_actor_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                           uint32_t vu0_size, int cases, uint32_t seed);
int run_integ_ai_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                        int cases, uint32_t seed);
// As armas e a direção automática dos veículos da IA (tests/driving_check_integ_ai_vehicle.inc, incluído no
// driving_check_traffic.cpp para usar a máquina da parte 14; chamada pelo pedaço da IA).
int run_integ_ai_vehicle_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                                uint32_t vu0_size, int cases, uint32_t seed);

}  // namespace drvcheck
