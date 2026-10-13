#pragma once
// A tela de entrada (o launcher) no nightfire.exe (só do port; docs/port.md, "Instalação fácil"): o laço que a mostra
// antes do jogo (a cada partida normal, com o Shift, sem um disco que sirva, ou com --setup: ui/modern_setup.h,
// launcher_reason), com a janela e o renderizador do jogo. A tela é ui/modern_setup.h (sem GPU); aqui fica o laço dela,
// sem nada do sistema:
//  - a escolha de arquivo (platform/file_dialog.h: no Windows, o IFileOpenDialog numa thread STA própria; o laço continua
//    desenhando e bombeando a janela);
//  - a conferência de um disco escolhido (platform/disc_source.h: probe_disc) e o [game] disc do nightfire.ini (store);
//  - os leitores de DVD, procurados de novo a cada 2 s numa thread (um disco que entra aparece sozinho);
//  - a cópia pessoal (platform/disc_pack.h) numa thread, com o andamento na caixa da tela;
//  - as opções da tela (a resolução, o modo de tela, o VSync, o estilo dos menus, a camada 2D em HD, "Show at startup"):
//    o PLAY as devolve em SetupResult::settings, e quem chamou grava e abre o jogo com elas.
// Nas capturas (--screenshot) a tela desenha fora da tela, no relógio fixo de 60 quadros por segundo, só com o roteiro
// de --setup-input; --setup-pick <caminho> é a resposta da janela de escolha. Se o --wait passa com a tela aberta, grava
// a imagem e o relatório (<png>.txt: a linha "configuracao") e o programa sai.

#include <functional>
#include <memory>
#include <string>

#include "platform/disc_source.h"
#include "platform/gamepad.h"
#include "ui/modern_setup.h"

namespace nf {

class Renderer;
class Window;

// A entrada de um quadro de vídeo (a janela, ou o roteiro das capturas).
struct SetupFrameInput {
    bool keys[256] = {};
    bool mouse_inside = false;
    float mouse_x = 0.0f, mouse_y = 0.0f;  // pixels da janela
    float wheel = 0.0f;
    PadState pad;
};

struct SetupOptions {
    Window* window = nullptr;
    Renderer* renderer = nullptr;
    std::wstring hd_ui_dir;            // as fontes, os glifos e o design/tokens.json do estilo Modern
    std::string build;                 // a versão na barra de baixo
    const DiscTitle* title = nullptr;
    DiscFound found;                   // o que a procura achou (os recusados viram o estado da tela)
    // As capturas.
    bool capture = false;
    std::wstring screenshot;
    float wait = 1.0f;
    std::function<void(float t, SetupFrameInput&)> script;
    std::wstring pick;                 // --setup-pick
    // O [game] disc no nightfire.ini (falso: não gravou; nas capturas sem --config não grava).
    std::function<bool(const std::wstring& path)> store;
    std::wstring exe;                  // o executável em uso (a base da cópia pessoal)
    bool pack_movies = true;           // a cópia leva os filmes
    // As opções que a tela mostra (as do nightfire.ini e da linha de comando) e por que ela apareceu (o relatório).
    ui::LauncherSettings settings;
    ui::LauncherReason reason = ui::LauncherReason::Forced;
};

struct SetupResult {
    DiscFound found;        // o disco escolhido (ok) para o jogo
    bool quit = false;      // QUIT ou a janela fechada
    bool captured = false;  // a captura da tela foi gravada (o programa sai)
    ui::LauncherSettings settings;   // PLAY: as opções da tela (para gravar e abrir o jogo com elas)
    bool settings_changed = false;   // mudaram desde que a tela abriu
};

SetupResult run_setup(const SetupOptions& o);

}  // namespace nf
