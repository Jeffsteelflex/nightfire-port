#pragma once
// Preparação da plataforma antes do jogo rodar: copia o índice FileList do ACTION.ELF para o
// psi_file (no PS2 ele está no .data do executável) e abre o FILES.BIN; roda os construtores
// estáticos do ACTION.ELF, que montam tabelas como weapon_data (no PS2, o __main antes do main).
// Tudo lido do disco do jogador (platform/disc_source.h).

#include <string>
#include <vector>

#include "data/files_bin.h"
#include "platform/disc_source.h"

namespace nf {

// O disco que o port espera: 007: Nightfire, PS2, americano (SLUS-20579). Os arquivos sem os quais ele não roda, e o
// que entra na cópia pessoal (platform/disc_pack.h): ACTION.ELF, SYSTEM.CNF, FILES.BIN e PS2\; os filmes (MOVIES\) são
// opcionais; o DRIVING\ fica de fora até as missões de carro rodarem.
const DiscTitle& nightfire_disc();

// Copia o FileList e abre o FILES.BIN do disco de `files` (psiFileSetup).
bool install_files(const FilesBin& files);

// Lê o ACTION.ELF do disco e chama init_game_tables (game/static_init.h) e init_hud_tables
// (game/hud_tables.h) com os ponteiros de código resolvidos pelo motor (Engine_ResolveCode). log
// recebe as traduções e avisos.
bool install_game_tables(const DiscSource& disc, std::string* error, std::vector<std::string>* log = nullptr);
// O mesmo de um caminho (open_disc: uma pasta, uma imagem .iso, uma unidade).
bool install_game_tables(const std::wstring& data_dir, std::string* error, std::vector<std::string>* log = nullptr);

}  // namespace nf
