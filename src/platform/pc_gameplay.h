#pragma once
// As opções de jogabilidade só do PC (só do port; docs/port.md "Mira moderna e roda de armas"), em [game] do
// nightfire.ini ao lado das opções do jogo (platform/game_options.h), mas sem lugar no jogo nem no perfil:
//   aim_style    = auto | modern | classic   o estilo de mira (engine/player_ads.h); auto: moderno com o teclado e o
//                                            mouse (o último aparelho usado), o do jogo com o controle
//   wheel_slowmo = 0.25                      a lógica com a roda de armas aberta (engine/weapon_wheel.h): 1 tempo
//                                            real, 0 parada, 0.25 um quarto dos ticks
// Como as outras opções do PC, o arquivo só recebe as chaves que já tem ou que saíram do padrão.

#include <cstdint>

namespace nf {

class Settings;

struct PcGameplay {
    uint8_t aim_style = 0;       // 0 auto, 1 modern, 2 classic (AimStyle de engine/player_ads.h)
    float wheel_slowmo = 0.25f;  // 0 a 1
};
constexpr float kDefaultWheelSlowmo = 0.25f;

PcGameplay& pc_gameplay();
void load_pc_gameplay(const Settings& s, PcGameplay* g);
void save_pc_gameplay(Settings& s, const PcGameplay& g);
// Grava nightfire.ini com as opções atuais (com g_controls_persist, como nf::persist_controls).
void persist_pc_gameplay();

// Os valores da roda nos menus (0, 0.1, 0.25, 0.5, 1) e o mais perto de um valor.
constexpr int kWheelSlowmoChoices = 5;
extern const float kWheelSlowmoValues[kWheelSlowmoChoices];
int wheel_slowmo_index(float v);

}  // namespace nf
