#pragma once
// A tela de configuração da primeira partida no nightfire.exe (só do port; docs/port.md, "Instalação fácil"): o laço
// que a mostra antes do jogo quando nenhum disco serve (ou com --setup), com a janela e o renderizador do jogo. A tela
// é ui/modern_setup.h (sem GPU); aqui ficam o que é do Windows e do disco:
//  - a escolha de arquivo (IFileOpenDialog numa thread STA própria; o laço continua desenhando e bombeando a janela);
//  - a conferência de um disco escolhido (platform/disc_source.h: probe_disc) e o [game] disc do nightfire.ini (store);
//  - os leitores de DVD, procurados de novo a cada 2 s numa thread (um disco que entra aparece sozinho);
//  - a cópia pessoal (platform/disc_pack.h) numa thread, com o andamento na caixa da tela.
// Nas capturas (--screenshot) a tela desenha fora da tela, no relógio fixo de 60 quadros por segundo, só com o roteiro
// de --setup-input; --setup-pick <caminho> é a resposta da janela de escolha. Se o --wait passa com a tela aberta, grava
// a imagem e o relatório (<png>.txt: a linha "configuracao") e o programa sai.

#include <functional>
#include <memory>
#include <string>

#include "platform/disc_source.h"
#include "platform/gamepad.h"

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
};

struct SetupResult {
    DiscFound found;        // o disco escolhido (ok) para o jogo
    bool quit = false;      // QUIT ou a janela fechada
    bool captured = false;  // a captura da tela foi gravada (o programa sai)
};

SetupResult run_setup(const SetupOptions& o);

}  // namespace nf
