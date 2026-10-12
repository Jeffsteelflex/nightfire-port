#pragma once
// Preparação da plataforma antes do jogo rodar: copia o índice FileList do ACTION.ELF para o
// psi_file (no PS2 ele está no .data do executável) e abre o FILES.BIN; roda os construtores
// estáticos do ACTION.ELF, que montam tabelas como weapon_data (no PS2, o __main antes do main).

#include <string>
#include <vector>

#include "data/files_bin.h"

namespace nf {

bool install_files(const FilesBin& files, const std::wstring& data_dir);

// Lê o ACTION.ELF de data_dir e chama init_game_tables (game/static_init.h) e init_hud_tables
// (game/hud_tables.h) com os ponteiros de código resolvidos pelo motor (Engine_ResolveCode). log
// recebe as traduções e avisos.
bool install_game_tables(const std::wstring& data_dir, std::string* error, std::vector<std::string>* log = nullptr);

}  // namespace nf
