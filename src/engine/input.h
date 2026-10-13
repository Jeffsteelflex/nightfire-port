#pragma once
// Entrada do jogo (módulo Input, 0x0017edd8-0x0017f9e0 no ACTION.ELF): os controles viram 40
// "ações" por jogador, cada uma um valor de -1 a 1 com um estado (ligada, acabou de ligar,
// repetição). A plataforma (psiInput_MapInputs) preenche os valores; Input_Update compensa os
// analógicos, limita os valores e calcula os estados; o jogo consulta com Input_Action/Actionf.

#include <cstdint>

// Ações (GameActions_tag). Nomes pelo uso no código portado e pelo mapeamento padrão do controle
// do PS2 em psiInput_MapInputs (configuração 0).
enum : uint32_t {
    kActTurn = 0,         // analógico direito, horizontal: virar
    kActStrafe = 1,       // analógico esquerdo, horizontal (negado): andar de lado
    kActForward = 2,      // analógico esquerdo, vertical (negado): andar
    kActAimX = 3,         // analógico direito, horizontal: mira
    kActAimY = 4,         // analógico direito, vertical (negado): mira
    kActLookY = 5,        // analógico direito, vertical (negado): olhar para cima e para baixo
    kActDpadY = 6,        // direcional: -1 para cima, 1 para baixo
    kActJump = 7,         // triângulo
    kActCrouch = 8,       // X
    kActFire = 9,         // R1
    kActUse = 14,         // quadrado ou R2: usar (Player_Activate) ou recarregar
    kActAimMode = 19,     // L1
    kActMenuStart = 25,   // jogador 0: Start ou X
    kActMenuUp = 26,
    kActMenuDown = 27,
    kActMenuLeft = 28,
    kActMenuRight = 29,
    kActPause = 30,       // Start
    kActScanMode = 39,    // depuração (jogador 3)
};
constexpr int kActionCount = 40;

// Estado de uma ação (PlayerInput_tag::state, máscara de Input_Action).
enum : uint8_t {
    kActionDown = 1,     // ligada
    kActionPressed = 4,  // acabou de ligar
    kActionRepeat = 8,   // repetição: depois de 46 ticks ligada, a cada 8 ticks
};

// Entrada e opções de controle de um jogador (0x158 bytes; PlayerSetting tem 4).
struct PlayerInput_tag {
    uint8_t invert_look;     // 0x00 nega as ações 4 e 5
    uint8_t autoaim;         // 0x01 mira automática (modo história; Player_AutoAim)
    uint8_t autoaim_mp;      // 0x02 mira automática no multijogador
    uint8_t aim_toggle;      // 0x03 0: mira enquanto segura a ação 19; 1: a ação alterna (Player_Weapon)
    uint8_t crouch_toggle;   // 0x04 1: agachar alterna (Player_SSCrouch); 0: só enquanto segura
    uint8_t unknown_05;      // 1
    uint8_t keep_zoom;       // 0x06 1: cada arma lembra o seu zoom (Player_Weapon)
    uint8_t auto_center;     // 0x07 1: andar recentraliza a visão (Player_SSWalk)
    uint8_t unknown_08;      // 0x08 1: a mira aparece sem mirar (opção "Crosshairs" do menu, HUD)
    uint8_t unknown_09;      // 0x09 vibração ("Vibration", Input_RumbleStop)
    uint8_t auto_switch;     // 0x0a 1: pegar uma arma melhor já a põe na mão (Player_EquipWeapon)
    uint8_t unknown_0b;      // 0 (Player_Update, campo 0x8bc do jogador); menu "HUD Always On": sim = 0, não = 1
    uint8_t unknown_0c;      // 1; menu "Flashing Objects": sim = 2, não = 0 (o brilho dos itens)
    uint8_t unknown_0d;
    uint16_t style;          // 0x0e configuração dos botões (psiInput_MapInputs)
    uint16_t unknown_10;
    uint16_t unknown_12;
    float actions[kActionCount];  // 0x14 valores, zerados a cada Input_Update
    uint16_t hold[kActionCount];  // 0xb4 ticks seguidos com a ação ligada (até 0x7fff)
    uint8_t state[kActionCount];  // 0x104 kActionDown | kActionPressed | kActionRepeat
    // 0x12c bit 0x10: liberada (o estado segue a ação). Sem ele, a ação fica travada enquanto
    // estiver ligada (bit 1) ou acabar de ligar (bit 4); depois o bit 0x10 volta.
    uint8_t lock[kActionCount];
    uint8_t any;             // 0x154 alguma ação ligada neste tick
    uint8_t connected;       // 0x155 controle presente (psiInput_MapInputs)
    uint8_t index;           // 0x156
    uint8_t unknown_157;
};
static_assert(sizeof(PlayerInput_tag) == 0x158);

extern PlayerInput_tag PlayerSetting[4];
extern uint8_t gAnalogStickMappingFunction[129];
// As combinações de botões (0x00316ff0; DList de 10 nós de 0x18 bytes, refeita por Input_Ready a cada carga; só
// Input_Check4Combo, ainda não portada, a leria).
struct DLISTINFO_tag;
extern DLISTINFO_tag ComboKeysList;

void Input_Init();
// Descongela o jogo, zera as ações e refaz ComboKeysList (no heap do jogo: precisa de Mem_Init antes).
void Input_Ready();
void Input_Update();
uint16_t Input_Action(int16_t player, uint32_t action, uint16_t mask);
float Input_Actionf(int16_t player, uint32_t action, uint16_t mask);
void Input_ClearAction(int16_t player, uint32_t action);
void Input_SetAction(int16_t player, uint32_t action, uint8_t state);
void StickCompensation2(float* x, float* y);
void StickCompensation3(float* x, float* y);
float MapAnalogStick(float value);
// Vibração (o jogador precisa ter ligado a opção, PlayerInput_tag::unknown_09) e o estilo de botões.
void Input_RumbleStart(uint16_t player, int32_t frames, int32_t strength);
void Input_RumbleStop(uint16_t player);
uint32_t Input_ChangeControllerStyle(uint16_t player, uint32_t style);
