#pragma once
// As tabelas estáticas do HUD e da missão, copiadas do ACTION.ELF do usuário na partida (como
// weapon_data em game/static_init.h, mas com ponteiros entre as tabelas, que o TableCopy não cobre):
// - o construtor estático do HUD (_GLOBAL_$I$HUD_Init) roda no interpretador MIPS: ele monta no .bss
//   quatro panes de mensagem e os SpriteInfo delas;
// - PaneList e MPPaneList (22 ponteiros) levam às descrições das panes (HUDPANECREATE_tag), que vão
//   para HudPanePool com as funções de criação e atualização traduzidas por resolve_code (só funções
//   portadas: uma função do HUD não portada é erro) e os SpriteInfo copiados para HudSprInfoPool;
//   os formatos de fonte (SpriteInfo +0x20, strings curtas do .sdata) vão para HudFormatPool;
// - CrossHair, HUDCrossCoords, BulletImg.189 e TexUV.291 são copiados como estão;
// - MissionData (24 fases) e os objetivos que cada fase aponta (para MissionObjectives) e sp_level
//   (as missões do menu, para o substituto da escolha de missão).

#include <string>
#include <vector>

#include "game/static_init.h"

namespace nf {

class Elf;

bool init_hud_tables(const Elf& elf, CodeResolver resolve_code, std::string* error,
                     std::vector<std::string>* log = nullptr);

}  // namespace nf
