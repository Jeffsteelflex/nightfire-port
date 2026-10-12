#pragma once
// O controle do PS2 visto pelo jogo (psiInput_* do ACTION.ELF, 0x0010c418-0x0010e588): a libpad lê
// cada porta num TSLOTDATA (tSlot, 8 de 0x180 bytes: 2 portas x 4 do multitap), ps2_handle_pad deixa
// os botões em +0x122 (SCE_PAD*: 1 = apertado) e os analógicos em +0x128 (direito x, y, esquerdo x, y;
// 0 a 255, centro 127, y para baixo); psiInput_PollDevices aplica a zona morta e psiInput_MapInputs
// transforma o controle de cada jogador nas ações conforme o estilo de botões (PlayerSetting::style).
//
// No PC o "slot" guarda só os campos que o jogo usa (PsiPadSlot) e é preenchido por um controle do
// XInput (platform/gamepad.h; psi_pad_read_slot no lugar de ps2_handle_pad): A = X, B = círculo, X =
// quadrado, Y = triângulo, Back = Select, Start, LS/RS = L3/R3, direcional; com `pad_triggers` (padrão)
// RT/LT = R1/L1 e RB/LB = R2/L2 (o tiro e a mira do estilo 0 nos gatilhos), sem ele RB/LB = R1/L1. O
// slot 0 é o primeiro controle conectado (jogador 0) e o 4 o segundo (a porta 2, sem multitap).
//
// Divergências (comentadas nas funções): a zona morta de psiInput_PollDevices é configurável (40 de
// 127 no PS2, o padrão; [controls] pad_deadzone); a vibração conta ticks com PS2FramesToSkip, que o port
// deixa 0 (vale 1: a lógica roda sempre em ticks de 60 Hz); o multitap não existe no PC.

#include <cstdint>

#include "platform/gamepad.h"

struct PlayerInput_tag;

// Botões do PS2 como ps2_handle_pad os deixa (libpad: SCE_PADL2... com 1 = apertado).
enum : uint16_t {
    kPs2L2 = 0x0001,
    kPs2R2 = 0x0002,
    kPs2L1 = 0x0004,
    kPs2R1 = 0x0008,
    kPs2Triangle = 0x0010,
    kPs2Circle = 0x0020,
    kPs2Cross = 0x0040,
    kPs2Square = 0x0080,
    kPs2Select = 0x0100,
    kPs2L3 = 0x0200,
    kPs2R3 = 0x0400,
    kPs2Start = 0x0800,
    kPs2Up = 0x1000,
    kPs2Right = 0x2000,
    kPs2Down = 0x4000,
    kPs2Left = 0x8000,
};

// Os campos de TSLOTDATA que o jogo usa (offsets do PS2).
struct PsiPadSlot {
    int32_t state = 0;            // +0x114 99: pronto (o resto: passos da inicialização da libpad)
    int32_t rumble_time = 0;      // +0x118 ticks de vibração restantes (< 0: parada)
    int32_t rumble_strength = 0;  // +0x11c
    uint16_t pressed = 0;         // +0x120 botões que desceram nesta leitura
    uint16_t buttons = 0;         // +0x122 botões seguros
    uint8_t status = 0;           // +0x124 o primeiro byte da leitura (0: leitura boa)
    uint8_t sticks[4] = {0x7f, 0x7f, 0x7f, 0x7f};  // +0x128 direito x, y, esquerdo x, y
    uint8_t mapped[4] = {0x7f, 0x7f, 0x7f, 0x7f};  // +0x148 a cópia de psiInput_PollDevices
    uint8_t motor_small = 0;      // +0x164 liga/desliga
    uint8_t motor_big = 0;        // +0x165 0 a 255
    // Só no PC: o controle do XInput deste slot (-1: nenhum) e os motores enviados por último.
    int8_t xinput = -1;
    uint8_t sent_small = 0;
    uint8_t sent_big = 0;
};

constexpr int kPsiPadSlots = 8;
extern PsiPadSlot tSlot[kPsiPadSlots];
extern uint16_t AllButtonState;   // OU dos botões de todos os slots (psiInput_PollDevices)
extern int32_t MultitapPresent;   // sempre 0 no PC

// Funções portadas da plataforma do PS2.
int32_t MapPlayerToSlot(int32_t player);
void psiInput_RumbleStart(uint16_t pad, int32_t frames, int32_t strength);
void psiInput_RumbleStop(uint16_t pad);
void psiStopAllRumble();
void psiFlushAllInput();
void psiInputReady();

// ---- só no PC ----
// A zona morta de psiInput_PollDevices em contagens (0 a 126; 40 no PS2).
extern int32_t psiPadDeadZone;
// Entradas do controle que não vão para o controle do PS2 (bits 1 << kPadIn* de input_bindings.h: as
// religadas na tabela de teclas).
extern uint32_t psiPadMask;
// Os gatilhos fazem R1/L1 (padrão) ou R2/L2.
extern bool psiPadTriggersR1;
// O estado lido do XInput neste tick (psi_pad_poll) e o slot de cada controle.
extern nf::PadState psiPadStates[nf::kMaxPads];
// Lê os controles (uma vez por tick) e distribui os conectados nos slots 0 e 4.
void psi_pad_poll();
// scePadSetActDirect do PC: manda os motores do slot ao XInput (se mudaram).
void psi_pad_set_act(PsiPadSlot& s);
// ps2_handle_pad do PC: copia o controle do slot para os campos do PS2.
void psi_pad_read_slot(int32_t slot);
// Converte um controle do PC no do PS2 (botões e analógicos), sem as entradas de `mask`.
void psi_pad_convert(const nf::PadState& pad, uint32_t mask, bool triggers_r1, uint16_t* buttons, uint8_t sticks[4]);
// A zona morta de psiInput_PollDevices num byte do analógico.
uint8_t psi_pad_deadzone(uint8_t raw, int32_t deadzone);
// O mapeamento do estilo (o switch de psiInput_MapInputs) e as ações comuns de um jogador.
void psi_pad_map_player(PlayerInput_tag& p, const PsiPadSlot& s, int32_t player);
// Os botões do PC (bits 1 << kPadIn*) que fazem a ação `action` com o sinal `sign` no estilo, para a
// página de teclas (simula cada botão do PS2 pelo mapeamento acima).
uint32_t psi_pad_style_inputs(uint16_t style, int32_t action, int32_t sign, bool triggers_r1);
