#pragma once
// nf_driving_check, parte 19: a seção do helicóptero de Paris (tests/driving_check_heli.cpp): o código ORIGINAL do
// DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte de driving/render/path_engine.h (o
// CARP::PathInfo, o RPathEngine), driving/game/rail.h (o trilho: o AISplinePath, o AIGroundVehicle no estado 3, o
// PBondCar::ProcessSplinePhysics, os eventos do trilho) e a câmera da seção (driving/camera/player_camera_modes.cpp,
// camera_math.cpp), com os caminhos e os trilhos de Paris e valores sorteados. docs/driving.md seção 23.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Devolve o número de comparações que falharam. `cases`: o tamanho das conferências (casos por função).
int run_heli_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                    int cases, uint32_t seed);
// A câmera da seção (tests/driving_check_heli_cam.cpp; chamada por run_heli_checks): `frames` quadros de UpdateCamera.
int run_heli_camera_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                           uint32_t vu0_size, int frames, uint32_t seed);
// O trilho do carro do jogador (tests/driving_check_heli_rail.cpp; chamada por run_heli_checks): `scenes` cenas com os
// eventos do trilho, o RPathEngine, o DoSplinePathUpdate e o ProcessSplinePhysics.
int run_heli_rail_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                         int scenes, uint32_t seed);

}  // namespace drvcheck
