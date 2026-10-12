// O controle do PS2 no PC: os slots da libpad preenchidos pelo XInput, o mapeamento dos estilos de
// botões e a vibração. Ver psi_pad.h; psiInput_PollDevices e psiInput_MapInputs estão em psi_input.cpp.

#include "platform/psi_pad.h"

#include <algorithm>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/input.h"
#include "platform/input_bindings.h"

PsiPadSlot tSlot[kPsiPadSlots];
uint16_t AllButtonState = 0;
int32_t MultitapPresent = 0;
int32_t psiPadDeadZone = 40;
uint32_t psiPadMask = 0;
bool psiPadTriggersR1 = true;
nf::PadState psiPadStates[nf::kMaxPads];

namespace {

float stick(uint8_t v) { return (float(v) - 127.0f) * 0.0078125f; }
float bits(uint16_t b, uint16_t mask) { return float(b & mask); }

// Os botões do PC e os do PS2 que eles fazem (os ombros dependem de psiPadTriggersR1).
struct ButtonPair {
    uint8_t pc;   // kPadIn*
    uint16_t ps2;
};
constexpr ButtonPair kFixedButtons[] = {
    {nf::kPadInA, kPs2Cross},        {nf::kPadInB, kPs2Circle},         {nf::kPadInX, kPs2Square},
    {nf::kPadInY, kPs2Triangle},     {nf::kPadInBack, kPs2Select},      {nf::kPadInStart, kPs2Start},
    {nf::kPadInLThumb, kPs2L3},      {nf::kPadInRThumb, kPs2R3},        {nf::kPadInDpadUp, kPs2Up},
    {nf::kPadInDpadDown, kPs2Down},  {nf::kPadInDpadLeft, kPs2Left},    {nf::kPadInDpadRight, kPs2Right},
};

// Byte do analógico do PS2 (centro 127, 0 a 255) a partir do XInput (y do XInput para cima: o do PS2
// cresce para baixo).
uint8_t stick_byte(int16_t v, bool flip) {
    float f = float(v) * (128.0f / 32767.0f);
    if (flip) f = -f;
    const int32_t b = 127 + int32_t(f + (f < 0.0f ? -0.5f : 0.5f));
    return uint8_t(std::clamp(b, 0, 255));
}

}  // namespace

// scePadSetActDirect do PC: manda os motores ao XInput quando mudaram (o grande no motor de baixa
// frequência, o pequeno, que no DualShock só liga e desliga, no outro).
void psi_pad_set_act(PsiPadSlot& s) {
    if (s.xinput < 0) return;
    if (s.motor_small == s.sent_small && s.motor_big == s.sent_big) return;
    s.sent_small = s.motor_small;
    s.sent_big = s.motor_big;
    nf::pad_source().vibrate(s.xinput, uint16_t(s.motor_big * 257u), s.motor_small != 0 ? 0xffff : 0);
}

void psi_pad_convert(const nf::PadState& pad, uint32_t mask, bool triggers_r1, uint16_t* buttons, uint8_t sticks[4]) {
    uint16_t b = 0;
    const auto on = [&](uint8_t code) { return (mask & (1u << code)) == 0; };
    for (const ButtonPair& p : kFixedButtons) {
        if ((pad.buttons & (1u << p.pc)) != 0 && on(p.pc)) b |= p.ps2;
    }
    const bool rt = pad.rt > nf::kPadTriggerThreshold && on(nf::kPadInRT);
    const bool lt = pad.lt > nf::kPadTriggerThreshold && on(nf::kPadInLT);
    const bool rb = (pad.buttons & nf::kPadRB) != 0 && on(nf::kPadInRB);
    const bool lb = (pad.buttons & nf::kPadLB) != 0 && on(nf::kPadInLB);
    if (rt) b |= triggers_r1 ? kPs2R1 : kPs2R2;
    if (lt) b |= triggers_r1 ? kPs2L1 : kPs2L2;
    if (rb) b |= triggers_r1 ? kPs2R2 : kPs2R1;
    if (lb) b |= triggers_r1 ? kPs2L2 : kPs2L1;
    *buttons = b;
    // Uma metade religada na tabela fica no centro.
    const auto half = [&](int16_t v, uint8_t pos, uint8_t neg) -> int16_t {
        if (v > 0 && !on(pos)) return 0;
        if (v < 0 && !on(neg)) return 0;
        return v;
    };
    sticks[0] = stick_byte(half(pad.rx, nf::kPadInRXPos, nf::kPadInRXNeg), false);
    sticks[1] = stick_byte(half(pad.ry, nf::kPadInRYPos, nf::kPadInRYNeg), true);
    sticks[2] = stick_byte(half(pad.lx, nf::kPadInLXPos, nf::kPadInLXNeg), false);
    sticks[3] = stick_byte(half(pad.ly, nf::kPadInLYPos, nf::kPadInLYNeg), true);
}

void psi_pad_poll() {
    nf::pad_source().poll(psiPadStates);
    // Slot 0: o primeiro controle conectado; slot 4 (a porta 2): o segundo.
    int8_t first = -1, second = -1;
    for (int8_t i = 0; i < nf::kMaxPads; ++i) {
        if (!psiPadStates[i].connected) continue;
        if (first < 0) {
            first = i;
        } else if (second < 0) {
            second = i;
        }
    }
    for (int i = 0; i < kPsiPadSlots; ++i) {
        PsiPadSlot& s = tSlot[i];
        const int8_t pad = i == 0 ? first : i == 4 ? second : int8_t(-1);
        if (s.xinput != pad) {
            // Outro controle (ou nenhum) no slot: os motores dele começam parados.
            s.sent_small = 0;
            s.sent_big = 0;
        }
        s.xinput = pad;
    }
}

void psi_pad_read_slot(int32_t slot) {
    PsiPadSlot& s = tSlot[slot];
    if (s.xinput < 0 || !psiPadStates[s.xinput].connected) {
        s.state = 0;
        return;
    }
    s.state = 99;
    s.status = 0;
    uint16_t b = 0;
    psi_pad_convert(psiPadStates[s.xinput], psiPadMask, psiPadTriggersR1, &b, s.sticks);
    s.pressed = uint16_t(b & ~s.buttons);
    s.buttons = b;
}

// A zona morta de psiInput_PollDevices: dentro de ±dz o centro; fora, o resto esticado até a ponta
// (127/87 = 1,4597701 no PS2, com dz = 40), e o byte truncado como o original ((char)(int)v + 0x7f).
uint8_t psi_pad_deadzone(uint8_t raw, int32_t deadzone) {
    deadzone = std::clamp(deadzone, 0, 126);
    const float scale = deadzone == 40 ? 1.4597701f : 127.0f / float(127 - deadzone);
    float v = float(int32_t(raw) - 0x7f);
    if (0.0f < v) {
        v = 0.0f <= v - float(deadzone) ? (v - float(deadzone)) * scale : 0.0f;
    } else if (v < 0.0f) {
        v = 0.0f < v + float(deadzone) ? 0.0f : (v + float(deadzone)) * scale;
    }
    return uint8_t(int8_t(int32_t(v)) + 0x7f);
}

// [decomp] 0x0010cc48 MapPlayerToSlot
// O slot da libpad de cada jogador: 0 na porta 1; o 1 no multitap (slot 1) ou na porta 2 (slot 4); 2 e
// 3 só com o multitap e os anteriores presentes. -1: sem controle.
int32_t MapPlayerToSlot(int32_t player) {
    const auto ready = [](int32_t slot) { return tSlot[slot].state == 99; };
    if (player == 1) {
        if (!ready(0)) return -1;
        if (MultitapPresent == 1) return ready(1) ? 1 : -1;
        return ready(4) ? 4 : -1;
    }
    if (player < 2) return player == 0 && ready(0) ? 0 : -1;
    if (player == 2) {
        if (MultitapPresent != 1 || !ready(0) || !ready(1)) return -1;
        return ready(2) ? 2 : -1;
    }
    if (player == 3 && MultitapPresent == 1) {
        if (!ready(0) || !ready(1) || !ready(2)) return -1;
        return ready(3) ? 3 : -1;
    }
    return -1;
}

// O corpo de psiInput_MapInputs para um jogador com controle: o estilo de botões (PlayerSetting::style;
// os botões valem o próprio bit, como (float)(botões & máscara), e Input_Update limita a 1) e depois as
// ações comuns (Start, os menus, o direcional e o analógico esquerdo como direcional).
void psi_pad_map_player(PlayerInput_tag& p, const PsiPadSlot& s, int32_t player) {
    float* a = p.actions;
    const uint16_t b = s.buttons;
    const uint8_t* raw = s.sticks;    // +0x128: direito x, y, esquerdo x, y
    const uint8_t* map = s.mapped;    // +0x148
    const auto dpad_y = [&](uint16_t up, uint16_t down) {
        if ((b & up) != 0) a[kActDpadY] = -1.0f;
        if ((b & down) != 0) a[kActDpadY] = 1.0f;
    };
    switch (p.style) {
        case 0:  // NightFire
            a[2] = -stick(raw[3]);
            a[1] = -stick(raw[2]);
            a[5] = -stick(map[1]);
            a[0] = stick(map[0]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[1]);
            a[3] = stick(map[0]);
            dpad_y(kPs2Up, kPs2Down);
            a[7] = bits(b, kPs2Triangle);
            a[8] = bits(b, kPs2Cross);
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2Square | kPs2R2);
            a[12] = bits(b, kPs2Circle);
            a[16] = bits(b, kPs2Right | kPs2L2);
            a[15] = bits(b, kPs2Left);
            a[11] = bits(b, kPs2Up);
            a[10] = bits(b, kPs2Down);
            a[13] = bits(b, kPs2Select);
            break;
        case 1:  // Moonraker
        case 2:  // Octopussy: o analógico esquerdo vira e anda; o direito anda de lado (com L1, o esquerdo)
            a[2] = -stick(raw[3]);
            if (p.style == 1) {
                a[1] = -stick(raw[2]);
                a[0] = stick(map[0]);
            } else {
                a[1] = -stick((b & kPs2L1) == 0 ? raw[0] : raw[2]);
                a[0] = stick(map[2]);
            }
            a[5] = -stick(map[1]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[1]);
            a[3] = stick(map[0]);
            dpad_y(kPs2Up, kPs2Down);
            a[7] = bits(b, kPs2Triangle);
            a[8] = bits(b, kPs2L2);
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2Cross);
            a[12] = bits(b, kPs2Square);
            a[16] = bits(b, kPs2Right | kPs2R2);
            a[15] = bits(b, kPs2Left);
            a[11] = bits(b, kPs2Up);
            a[10] = bits(b, kPs2Down);
            a[13] = bits(b, kPs2Circle);
            break;
        case 3:  // Goldfinger
            a[2] = -stick(raw[3]);
            a[1] = -stick(raw[2]);
            a[5] = -stick(map[1]);
            a[0] = stick(map[0]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[1]);
            a[3] = stick(map[0]);
            dpad_y(kPs2Up, kPs2Down);
            a[7] = bits(b, kPs2Triangle);
            a[8] = bits(b, kPs2Cross);
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2L2);
            a[12] = bits(b, kPs2R2);
            a[16] = bits(b, kPs2Right);
            a[15] = bits(b, kPs2Left);
            a[11] = bits(b, kPs2Up);
            a[10] = bits(b, kPs2Down);
            a[13] = bits(b, kPs2Square | kPs2Circle);
            break;
        case 4:  // Dr. No: os analógicos trocados (o direito anda, o esquerdo olha)
            a[2] = -stick(raw[1]);
            a[1] = -stick(raw[0]);
            a[5] = -stick(map[3]);
            a[0] = stick(map[2]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[3]);
            a[3] = stick(map[2]);
            dpad_y(kPs2Triangle, kPs2Cross);
            a[7] = bits(b, kPs2Up);
            a[8] = bits(b, kPs2Down);
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2L2);
            a[12] = bits(b, kPs2R2);
            a[16] = bits(b, kPs2Circle);
            a[15] = bits(b, kPs2Square);
            a[11] = bits(b, kPs2Triangle);
            a[10] = bits(b, kPs2Cross);
            a[13] = bits(b, kPs2Right | kPs2Left);
            break;
        case 5:  // Thunderball: o esquerdo vira e anda, o direito anda de lado e olha; L1 tira o pulo e o agachar
            a[2] = -stick(raw[3]);
            a[1] = -stick(raw[0]);
            a[5] = -stick(map[1]);
            a[0] = stick(map[2]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[3]);
            a[3] = stick(map[2]);
            dpad_y(kPs2Triangle, kPs2Cross);
            if ((b & kPs2L1) == 0) {
                a[7] = bits(b, kPs2Triangle);
                a[8] = bits(b, kPs2Cross);
            }
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2L2);
            a[12] = bits(b, kPs2R2);
            a[16] = bits(b, kPs2Right);
            a[15] = bits(b, kPs2Left);
            a[11] = bits(b, kPs2Up);
            a[10] = bits(b, kPs2Down);
            a[13] = bits(b, kPs2Square | kPs2Circle);
            break;
        case 6: {  // GoldenEye: L1/R1 andam de lado, X atira, R2 mira
            a[2] = -stick(raw[3]);
            const float strafe = -stick(raw[0]);
            a[1] = strafe;
            if ((b & kPs2L1) != 0) a[1] = strafe + 1.0f;
            if ((b & kPs2R1) != 0) a[1] = a[1] - 1.0f;
            a[5] = -stick(map[1]);
            a[0] = stick(map[2]);
            a[19] = bits(b, kPs2R2);
            a[4] = -stick(map[3]);
            a[3] = stick(map[2]);
            dpad_y(kPs2Up, kPs2Down);
            a[7] = bits(b, kPs2Triangle);
            a[8] = bits(b, kPs2L2);
            a[9] = bits(b, kPs2Cross);
            a[14] = bits(b, kPs2Square);
            a[12] = bits(b, kPs2Circle);
            a[16] = bits(b, kPs2Down);
            a[15] = bits(b, kPs2Up);
            a[11] = bits(b, kPs2Right);
            a[10] = bits(b, kPs2Left);
            a[13] = bits(b, kPs2Select);
            break;
        }
        case 7:  // Classic Bond
            a[2] = -stick(raw[3]);
            a[1] = -stick(raw[0]);
            a[5] = -stick(map[1]);
            a[0] = stick(map[2]);
            a[19] = bits(b, kPs2L1);
            a[4] = -stick(map[3]);
            a[3] = stick(map[2]);
            dpad_y(kPs2Up, kPs2Down);
            a[7] = bits(b, kPs2Triangle);
            a[8] = bits(b, kPs2L2);
            a[9] = bits(b, kPs2R1);
            a[14] = bits(b, kPs2Cross);
            a[12] = bits(b, kPs2Square);
            a[16] = bits(b, kPs2Down | kPs2R2);
            a[15] = bits(b, kPs2Up);
            a[11] = bits(b, kPs2Right | kPs2Circle);
            a[10] = bits(b, kPs2Left);
            a[13] = bits(b, kPs2Select);
            break;
        default:
            break;
    }
    // Comuns a todos os estilos.
    a[kActPause] = bits(b, kPs2Start);
    a[kActMenuUp] = bits(b, kPs2Up);
    a[kActMenuDown] = bits(b, kPs2Down);
    a[kActMenuLeft] = bits(b, kPs2Left);
    a[kActMenuRight] = bits(b, kPs2Right);
    if (stick(raw[2]) < -0.5f) a[kActMenuLeft] = 1.0f;
    if (0.5f < stick(raw[2])) a[kActMenuRight] = 1.0f;
    if (stick(raw[3]) < -0.5f) a[kActMenuUp] = 1.0f;
    if (0.5f < stick(raw[3])) a[kActMenuDown] = 1.0f;
    a[31] = bits(b, kPs2Cross);
    a[32] = bits(b, kPs2Square);
    a[33] = bits(b, kPs2Circle);
    a[kActPause] = bits(b, kPs2Start);
    a[34] = bits(b, kPs2Triangle);
    if (player == 0) a[kActMenuStart] = bits(b, kPs2Start | kPs2Cross);
    // Combinação de depuração: analógico esquerdo à esquerda, direito à direita e o direcional para cima.
    if (stick(raw[2]) < -0.8f && 0.8f < stick(raw[0]) && (b & kPs2Up) != 0) a[38] = 1.0f;
}

uint32_t psi_pad_style_inputs(uint16_t style, int32_t action, int32_t sign, bool triggers_r1) {
    if (action < 0 || action >= kActionCount) return 0;  // as ações só do port (input_bindings.h) não têm botão
    uint32_t out = 0;
    PlayerInput_tag p{};
    p.style = style;
    const auto hit = [&](const PsiPadSlot& s) {
        std::memset(p.actions, 0, sizeof(p.actions));
        psi_pad_map_player(p, s, 0);
        return float(sign) * p.actions[action] > 0.0f;
    };
    // Os botões do PC que fazem cada botão do PS2.
    for (uint8_t pc = 0; pc < nf::kPadInLXPos; ++pc) {
        nf::PadState pad;
        pad.connected = true;
        if (pc < 16) {
            pad.buttons = uint16_t(1u << pc);
        } else {
            (pc == nf::kPadInLT ? pad.lt : pad.rt) = 255;
        }
        PsiPadSlot s;
        psi_pad_convert(pad, 0, triggers_r1, &s.buttons, s.sticks);
        if (s.buttons == 0) continue;
        std::memcpy(s.mapped, s.sticks, 4);
        if (hit(s)) out |= 1u << pc;
    }
    // As metades dos analógicos.
    static const struct {
        uint8_t pc;
        int index;
        uint8_t value;
    } kHalves[] = {{nf::kPadInRXPos, 0, 255}, {nf::kPadInRXNeg, 0, 0}, {nf::kPadInRYPos, 1, 0}, {nf::kPadInRYNeg, 1, 255},
                   {nf::kPadInLXPos, 2, 255}, {nf::kPadInLXNeg, 2, 0}, {nf::kPadInLYPos, 3, 0}, {nf::kPadInLYNeg, 3, 255}};
    for (const auto& h : kHalves) {
        PsiPadSlot s;
        s.sticks[h.index] = h.value;
        s.mapped[h.index] = h.value;
        if (hit(s)) out |= 1u << h.pc;
    }
    return out;
}

// [decomp] 0x0010e4a8 psiInput_RumbleStart
// Liga a vibração do controle do jogador: o motor pequeno, o grande com a força x 2,5 (o byte truncado
// como no original) e a duração em ticks (a maior entre a atual e a pedida). Força 0 não faz nada.
void psiInput_RumbleStart(uint16_t pad, int32_t frames, int32_t strength) {
    const int32_t slot = MapPlayerToSlot(pad);
    if (strength == 0 || slot == -1) return;
    PsiPadSlot& s = tSlot[slot];
    if (s.state != 99 || s.status != 0) return;
    s.motor_small = 1;
    s.rumble_time = std::max(s.rumble_time, frames);
    s.rumble_strength = strength;
    s.motor_big = uint8_t(int32_t(float(strength) * 2.5f));
    psi_pad_set_act(s);
}

// [decomp] 0x0010e3a8 psiInput_RumbleStop
void psiInput_RumbleStop(uint16_t pad) {
    const int32_t slot = MapPlayerToSlot(pad);
    if (slot == -1) return;
    PsiPadSlot& s = tSlot[slot];
    if (s.state != 99 || s.status != 0) return;
    s.motor_small = 0;
    s.motor_big = 0;
    psi_pad_set_act(s);
}

// [decomp] 0x0010e418 psiStopAllRumble
void psiStopAllRumble() {
    for (PsiPadSlot& s : tSlot) {
        if (s.state != 99 || s.status != 0) continue;
        s.motor_small = 0;
        s.motor_big = 0;
        psi_pad_set_act(s);
    }
}

// [decomp] 0x0010e580 psiFlushAllInput
// Vazia no original.
void psiFlushAllInput() {}

// [decomp] 0x0010e578 psiInputReady
// Vazia no original.
void psiInputReady() {}
