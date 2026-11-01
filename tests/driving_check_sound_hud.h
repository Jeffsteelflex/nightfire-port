#pragma once
// nf_driving_check, parte 27 (`--so som_hud`): os sons das armas, da mira e dos objetos do mundo e o resto do HUD de
// Paris (docs/driving.md seção 31), o código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0) lado a lado com o
// porte.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// `vu0`: o programa da VU0 do .vutext (o de número 1, como as outras partes). Devolve as diferenças.
int run_sound_hud_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size,
                         int cases, uint32_t seed);

}  // namespace drvcheck
