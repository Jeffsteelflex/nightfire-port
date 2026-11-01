#pragma once
// nf_driving_check, parte 24 (docs/driving.md seção 28): os filmes e o HUD de Paris contra o DRIVING.ELF.

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// Os filmes e as legendas (tests/driving_check_p24.cpp): a biblioteca FONT e o driver de fontes do EAGL (o pacote do
// GIF), o GSubtitles e o GSubtitleSplitter, o relógio do AV_PLAYER, o PlayMPC::ReadInput.
int run_p24_movie_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);
// O HUD (tests/driving_check_p24_hud.cpp).
int run_p24_hud_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);
// A pausa do HUD (parte 27, seção 31: tests/driving_check_p24_hud.cpp, com a emulação do HUD da parte 24).
int run_p27_pause_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);

}  // namespace drvcheck
