#pragma once
// Tabelas do menu tiradas do ACTION.ELF do usuário na partida, rodando o próprio código do jogo no interpretador MIPS
// (como as de game/static_init.h e game/hud_tables.h; nada delas fica no repositório):
// - os créditos: Menu_SetupCredits (0x001f9a88) monta as 578 linhas com código (cada uma um rótulo do *Txt.dat ou uma
//   string do .rodata, as fontes e se é uma linha só) e as copia para o heap do menu. Txt_BindLabel, Menu_Malloc,
//   amemcpy e memset são nativos; o rótulo volta marcado (0x80000000 | rótulo) para ser lido na hora pelo
//   Menu_SetupCredits portado. O resultado vai para CreditsSource (engine/menu_frontend.h).
// - as listas da página de depuração (engine/menu_tweaks.h): o que C_NIS_Handler e C_LBPMMAP_Handler acrescentam ao
//   serem criados (mensagem 0x51), com __Menu_SendMessage nativo guardando cada item (o texto copiado e o valor).
//
// E uma conferência para o nf_loader_check: check_special_codenames roda o Menu_SpecialCodenameCheck do ELF para
// cada código de "Secret Unlocks" (e um que não vale) com alguns estados de `bonus` e das missões liberadas, com
// Menu_UpgradeCheat chamando o portado, e compara a resposta, os bônus gravados (Menu_SetBonus) e as missões com o
// Menu_SpecialCodenameCheck portado (engine/menu_secret.cpp).

#include <string>
#include <vector>

namespace nf {

class Elf;

bool init_menu_tables(const Elf& elf, std::string* error, std::vector<std::string>* log = nullptr);

// Devolve quantos códigos (vezes estados) conferem; `report` recebe as diferenças (vazio se nenhuma) ou o erro.
int check_special_codenames(const Elf& elf, std::string* report);

}  // namespace nf
