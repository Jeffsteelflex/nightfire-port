// Entrada do jogo. Ver input.h.
//
// gAnalogStickMappingFunction é a curva de resposta do analógico (zona morta e trechos lineares)
// do .data do ACTION.ELF, montada aqui pelos mesmos trechos.

#include "engine/input.h"

#include <cmath>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "platform/pending.h"
#include "platform/psi_input.h"
#include "platform/psi_pad.h"

PlayerInput_tag PlayerSetting[4];

namespace {
struct StickCurve {
    uint8_t v[129];
    constexpr StickCurve() : v{} {
        // 0-36 zona morta; 37-40 = 10; depois inclinação 1, 2, 4 e 10; 126-128 = 255.
        for (int i = 0; i < 129; ++i) {
            int y = 0;
            if (i >= 126) y = 255;
            else if (i > 115) y = 150 + 10 * (i - 115);
            else if (i > 100) y = 90 + 4 * (i - 100);
            else if (i > 80) y = 50 + 2 * (i - 80);
            else if (i > 40) y = i - 30;
            else if (i >= 37) y = 10;
            v[i] = uint8_t(y);
        }
    }
};
constexpr StickCurve kStickCurve;

void Input_Check4Combo() { pending_call("Input_Check4Combo"); }
}  // namespace

uint8_t gAnalogStickMappingFunction[129];
namespace {
[[maybe_unused]] const bool kCurveReady = [] {
    static_assert(sizeof(gAnalogStickMappingFunction) == sizeof(kStickCurve.v));
    std::memcpy(gAnalogStickMappingFunction, kStickCurve.v, sizeof(gAnalogStickMappingFunction));
    return true;
}();
}  // namespace

// [decomp] 0x0017edd8 Input_Init
void Input_Init() {
    for (PlayerInput_tag& p : PlayerSetting) {
        std::memset(&p, 0, sizeof(p));
        p.unknown_0c = 1;
        p.autoaim = 1;
        p.aim_toggle = 0;
        p.crouch_toggle = 1;
        p.unknown_05 = 1;
        p.unknown_08 = 1;
        p.auto_center = 1;
        p.keep_zoom = 1;
        p.style = 0;
        p.unknown_10 = 0;
        p.auto_switch = 1;
        p.unknown_0b = 0;
    }
    psiInput_Init();
}

// [decomp] 0x0017ee88 Input_Update
// Zera os valores, pede à plataforma os controles e as ações, compensa os analógicos (a
// configuração 2 e 5 usa o analógico esquerdo de outro jeito), inverte a visão se pedido e calcula
// o estado de cada ação.
void Input_Update() {
    for (PlayerInput_tag& p : PlayerSetting) std::memset(p.actions, 0, sizeof(p.actions));
    psiInput_PollDevices();
    uint16_t players;
    std::memcpy(&players, MPSettings + 0x1ac, 2);
    if (GameState.level_file == 0x7000048) players = 4;
    psiInput_MapInputs(PlayerSetting, players);
    for (PlayerInput_tag& p : PlayerSetting) {
        float* a = p.actions;
        if (p.style == 2 || p.style == 5) {
            StickCompensation2(&a[kActAimX], &a[kActAimY]);
            StickCompensation3(&a[kActTurn], &a[kActForward]);
        } else {
            StickCompensation2(&a[kActAimX], &a[kActAimY]);
            StickCompensation2(&a[kActTurn], &a[kActLookY]);
            StickCompensation3(&a[kActStrafe], &a[kActForward]);
        }
        if (p.invert_look != 0) {
            a[kActAimY] = -a[kActAimY];
            a[kActLookY] = -a[kActLookY];
        }
        p.any = 0;
        for (int i = 0; i < kActionCount; ++i) {
            const float v = a[i];
            a[i] = -1.0f <= v ? (v < 1.0f ? v : 1.0f) : -1.0f;
            uint8_t flags;
            if (a[i] == 0.0f) {
                flags = 0;
                p.hold[i] = 0;
            } else {
                flags = p.state[i] != 0 ? kActionDown : kActionDown | kActionPressed;
                const uint32_t h = uint32_t(p.hold[i]) + 1;
                p.hold[i] = uint16_t(h > 0x7fff ? 0x7fff : h);
                if (p.hold[i] >= 0x2e) {
                    if ((p.hold[i] & 7) != 0) flags &= 0xf7;
                    else flags |= kActionRepeat;
                }
                p.any = 1;
            }
            const uint8_t lock = p.lock[i];
            if ((lock & 0x10) != 0) {
                p.state[i] = flags;
            } else if (!((flags & kActionDown) != 0 && (lock & 1) != 0) &&
                       !((flags & kActionPressed) != 0 && (lock & 4) != 0)) {
                p.lock[i] = lock | 0x10;
            }
        }
    }
    Input_Check4Combo();
}

// [decomp] 0x0017f208 Input_Action
// O valor da ação vezes 100 se o estado tiver algum bit da máscara; com player -1, o OU dos quatro.
uint16_t Input_Action(int16_t player, uint32_t action, uint16_t mask) {
    if (player == -1) {
        uint16_t r = 0;
        for (int16_t i = 0; i < 4; ++i) r |= Input_Action(i, action, mask);
        return r;
    }
    if (player < 4 && (PlayerSetting[player].state[action] & mask) != 0) {
        return uint16_t(int32_t(PlayerSetting[player].actions[action] * 100.0f));
    }
    return 0;
}

// [decomp] 0x0017f300 Input_Actionf
// Como Input_Action, mas o próprio valor; com player -1, o maior dos quatro (a partir de 0).
float Input_Actionf(int16_t player, uint32_t action, uint16_t mask) {
    if (player == -1) {
        float r = 0.0f;
        for (int16_t i = 0; i < 4; ++i) {
            if (r <= Input_Actionf(i, action, mask)) r = Input_Actionf(i, action, mask);
        }
        return r;
    }
    if (player < 4 && (PlayerSetting[player].state[action] & mask) != 0) return PlayerSetting[player].actions[action];
    return 0.0f;
}

// [decomp] 0x0017f400 Input_ClearAction
void Input_ClearAction(int16_t player, uint32_t action) {
    if (player == -1) {
        for (int16_t i = 0; i < 4; ++i) Input_ClearAction(i, action);
    } else if (player < 4) {
        PlayerSetting[player].actions[action] = 0.0f;
        PlayerSetting[player].state[action] = 0;
    }
}

// [decomp] 0x0017f4a0 Input_SetAction
void Input_SetAction(int16_t player, uint32_t action, uint8_t state) {
    if (player == -1) {
        for (int16_t i = 0; i < 4; ++i) Input_SetAction(i, action, state);
    } else if (player < 4) {
        PlayerSetting[player].actions[action] = 1.0f;
        PlayerSetting[player].state[action] = state;
    }
}

// [decomp] 0x0017f798 StickCompensation2
// Passa o comprimento do vetor (x, y) pela curva do analógico, mantendo a direção.
void StickCompensation2(float* x, float* y) {
    const float fx = *x, fy = *y;
    const float len = std::sqrt(fx * fx + fy * fy);
    if (0.0002f < len || len < -0.0002f) {
        const float m = MapAnalogStick(len);
        *x = fx * (m / len);
        *y = fy * (m / len);
    }
}

// [decomp] 0x0017f850 StickCompensation3
// Com um eixo perto do centro (|v| <= 0,707107), o outro além de 0,707107 vai ao máximo.
void StickCompensation3(float* x, float* y) {
    const float fx = *x, fy = *y;
    if (-0.707107f <= fx && fx <= 0.707107f) {
        if (fy <= -0.707107f) *y = -1.0f;
        else if (0.707107f <= fy) *y = 1.0f;
    }
    if (-0.707107f <= fy && fy <= 0.707107f) {
        if (fx <= -0.707107f) {
            *x = -1.0f;
            return;
        }
        if (0.707107f <= fx) *x = 1.0f;
    }
}

// [decomp] 0x0017f958 MapAnalogStick
float MapAnalogStick(float value) {
    const int32_t v = int32_t(value * 127.0f);
    int32_t a = v < 0 ? -v : v;
    if (a > 0x80) a = 0x80;
    int32_t m = gAnalogStickMappingFunction[a];
    if (v < 0) m = -m;
    return float(m) * 0.003921569f;
}

// [decomp] 0x0017f748 Input_RumbleStop
// Para a vibração do controle do jogador (se ele a ligou nas opções, +9).
void Input_RumbleStop(uint16_t player) {
    const PlayerInput_tag& p = PlayerSetting[player];
    if (p.unknown_09 != 0 && p.index < 4) psiInput_RumbleStop(p.index);
}

// [decomp] 0x0017f6b8 Input_RumbleStart
// Vibra o controle do jogador por `frames` ticks com a força dada, se ele ligou a vibração nas opções
// (+9) e o jogo está rodando (GameState +0x51, ligado por ResetMap_GameInit; o recomeço pela pausa o
// zera).
void Input_RumbleStart(uint16_t player, int32_t frames, int32_t strength) {
    const PlayerInput_tag& p = PlayerSetting[player];
    if (p.unknown_09 != 0 && GameState.unknown_51 != 0 && p.index < 4) psiInput_RumbleStart(p.index, frames, strength);
}

// [decomp] 0x0017f5f0 Input_ChangeControllerStyle
// Troca o estilo de botões do jogador; mudando, zera as ações dele (o teste `player == -1` do original
// nunca vale: o jogador é sem sinal e já passou por < 4). Devolve 0 com um jogador inválido.
uint32_t Input_ChangeControllerStyle(uint16_t player, uint32_t style) {
    if (player >= 4) return 0;
    PlayerInput_tag& p = PlayerSetting[player];
    if (p.style != uint16_t(style)) {
        p.style = uint16_t(style);
        psiFlushAllInput();
        for (uint16_t i = 0; i < 4; ++i) {
            if (int16_t(player) == -1 || player == i) std::memset(PlayerSetting[i].actions, 0, sizeof(PlayerSetting[i].actions));
        }
    }
    return 1;
}
