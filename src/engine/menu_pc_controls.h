#pragma once
// As abas só do port CONTROLES e TECLAS da página de opções do PC (0x40000060, engine/menu_pc_options.h), com
// as linhas e as peças das páginas de opções do jogo (engine/menu.h, "Páginas só do port"):
//
// - CONTROLES: o aparelho em uso (informação), a sensibilidade do mouse e a da mira, a sensibilidade e a zona
//   morta do controle (as barras de volume do "AV Options"), e inverter o Y do mouse, inverter o Y do controle
//   (PlayerSetting::invert_look), vibração (PlayerSetting +0x09) e os gatilhos como R1/L1 (os rádios Off/On).
//   Nove linhas: as que não cabem rolam.
// - TECLAS: a lista dos comandos (platform/input_bindings.h) com o teclado/mouse e o controle de cada um
//   (entre parênteses, o que o estilo de botões do jogo faz quando a tabela não religa nada) e a linha de
//   restaurar os padrões, na lista com moldura e setas do jogo; escolher uma linha (ou clicar nela) espera a
//   tecla, o botão do mouse, a roda ou o botão do controle novo (psiInput_BeginCapture; Esc cancela, Delete
//   limpa o teclado), com os conflitos resolvidos por nf::bind(); embaixo, o que aconteceu.
//
// As duas são registradas com MenuPc_RegisterTab. Os controles usam os ids 0x10000400-0x100004ff. As mudanças
// vão para o nightfire.ini na hora (nf::persist_controls). Os textos (em inglês por padrão, como os do jogo;
// português com [ui] language_pc = pt) são de engine/pc_text.h (os nomes dos comandos, de kBindCommands),
// com a mesma regra de acentos: na fonte do jogo, as letras sem glifo perdem o acento; na camada HD, não.

#include <cstdint>

#include "engine/menu_pc_options.h"

// As linhas da aba de controles e a lista de teclas.
constexpr uint32_t kPcRowDevice = 0x10000400;
constexpr uint32_t kPcRowMouseSens = 0x10000401;
constexpr uint32_t kPcRowAimSens = 0x10000402;
constexpr uint32_t kPcRowMouseInvert = 0x10000403;
constexpr uint32_t kPcRowPadSens = 0x10000404;
constexpr uint32_t kPcRowDeadzone = 0x10000405;
constexpr uint32_t kPcRowPadInvert = 0x10000406;
constexpr uint32_t kPcRowVibration = 0x10000407;
constexpr uint32_t kPcRowTriggers = 0x10000408;
// Só do PC: o estilo de mira (engine/player_ads.h; [game] aim_style), Automático/Moderno/Clássico.
constexpr uint32_t kPcRowAimStyle = 0x10000409;
constexpr uint32_t kPcKeysList = 0x10000430;
constexpr uint32_t kPcKeysStatus = 0x10000431;
// O valor da linha "restaurar padrões" na lista de teclas.
constexpr int32_t kPcKeysRestoreRow = 100;

// Registra as duas abas e os tratadores (o main, depois de MenuPc_Install; os testes, depois
// de menu_handlers_debug_power_on e menu_pc_debug_power_on).
void MenuPcControls_Register();
// A última mensagem da aba de teclas (UTF-8; relatório do --screenshot e testes).
const char* MenuPcControls_Status();
