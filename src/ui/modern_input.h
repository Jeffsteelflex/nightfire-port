#pragma once
// A entrada dos menus Modern dos menus do jogo (a pausa, o fim de missão e o resultado; a tela de opções tem a dela,
// igual): os comandos de um quadro de vídeo a partir do teclado (os comandos do menu da tabela de teclas, mais
// Enter e Esc fixos), do controle (direcional, analógico esquerdo, A/B/LB/RB/Start) e do mouse (na tela virtual),
// com a repetição das setas seguradas e o "engolir até soltar" (o que abriu a tela, o fim de uma ação, só conta
// depois de solto). Ver docs/design.md, "Comandos e símbolos".

#include <string>

#include "ui/modern_kit.h"
#include "ui/modern_options.h"

namespace nf {
struct InputCode;
}

namespace nf::ui {

// Os comandos. Start: o comando "pause" da tabela de teclas (Enter por padrão; sem o Esc, que nos menus é só voltar:
// nf::pause_menu_shared) e o Start do controle; com
// enter_selects, Enter escolhe (as telas em que o Start do jogo também escolhe: o fim de missão e o resultado).
// Circle: o círculo do controle do PS2 (menu_circle, Tab; o Y do controle não: o resultado o usa como o B do jogo).
enum class Nav : uint8_t { Up, Down, Left, Right, Select, Back, TabPrev, TabNext, Start, Count };
constexpr int kNavCount = int(Nav::Count);

struct NavState {
    bool swallow = true;
    bool held[kNavCount] = {};
    float repeat[4] = {};
    bool prev_lmb = true, prev_back = true;
    float last_mx = -1.0f, last_my = -1.0f;
    bool mouse_active = false;
    int device = 0;  // 0 teclado e mouse, 1 controle (psiInput_ActiveDevice)
    // O quadro lido.
    bool fired[kNavCount] = {};
    bool inside = false;   // o mouse está na janela
    float mx = 0.0f, my = 0.0f;  // na tela virtual
    bool moved = false, pressed = false, back = false, lmb = false;
    float wheel = 0.0f;

    // A tela abriu (ou terminou uma ação): engole tudo até soltar.
    void reset();
    void read(const UiInput& in, const Canvas& cv, float dt, const Tokens& tk, bool enter_selects);
    bool fire(Nav n) const { return fired[int(n)]; }
    // Um comando dos testes, como uma tecla apertada neste quadro.
    void inject(Nav n) { fired[int(n)] = true; }
    void clear_fired();
};

// O glifo SVG (hd_ui/svg/xbox_*.svg) de uma entrada do controle; vazio se não houver.
std::string xbox_glyph(const nf::InputCode& c);
// O nome do glifo do botão do Xbox que faz um símbolo do PS2 dos textos do jogo ('A' cruz... 'T' Select), pelo
// esquema dos ombros (ui_hd_xbox_name); vazio nos que não são botões.
std::string symbol_glyph(char symbol, bool triggers_r1);
// As teclas que fazem um símbolo no mapa de botões do jogo (o comando do jogo do estilo, ui_hd_prompt_keys); vazio:
// nenhuma.
std::wstring symbol_keys(char symbol, uint16_t style);
// A primeira tecla (teclado/mouse) ligada a um comando da tabela, pelo nome curto ("Space", "Esc"...); vazio sem.
std::wstring bind_key(const char* command);
// O glifo do controle do primeiro botão ligado a um comando; `fallback` sem.
std::string bind_glyph(const char* command, const char* fallback);

}  // namespace nf::ui
