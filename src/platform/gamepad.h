#pragma once
// Controles do PC (só do port): o estado de até 4 controles no formato do XInput e a vibração, atrás de
// uma interface (PadSource) para os testes e as capturas (--input com pad_*) injetarem estados sem um
// controle de verdade.
//
// A fonte real é a do sistema (system_pad_source): no Windows, o XInput 1.4 (xinput.lib, gamepad_xinput.cpp; o estado
// no formato dele é o deste cabeçalho, e um backend de outro sistema converte o seu). XInputGetState num índice vazio
// demora (o driver procura o aparelho), então um índice sem controle só é consultado de novo a cada 60 leituras; os
// conectados são lidos a cada tick de lógica (psiInput_PollDevices).
//
// O que o jogo vê é o controle do PS2: platform/psi_pad.h converte este estado no "slot" da libpad
// (botões SCE_PAD*, analógicos de 0 a 255) e daí as funções portadas fazem o resto.

#include <cstdint>
#include <vector>

namespace nf {

constexpr int kMaxPads = 4;

// Botões (os bits de XINPUT_GAMEPAD::wButtons).
enum : uint16_t {
    kPadDpadUp = 0x0001,
    kPadDpadDown = 0x0002,
    kPadDpadLeft = 0x0004,
    kPadDpadRight = 0x0008,
    kPadStart = 0x0010,
    kPadBack = 0x0020,
    kPadLThumb = 0x0040,
    kPadRThumb = 0x0080,
    kPadLB = 0x0100,
    kPadRB = 0x0200,
    kPadA = 0x1000,
    kPadB = 0x2000,
    kPadX = 0x4000,
    kPadY = 0x8000,
};

// Gatilho apertado (XINPUT_GAMEPAD_TRIGGER_THRESHOLD).
constexpr uint8_t kPadTriggerThreshold = 30;

struct PadState {
    bool connected = false;
    uint16_t buttons = 0;
    uint8_t lt = 0;
    uint8_t rt = 0;
    int16_t lx = 0;  // -32768..32767; y positivo para cima, como no XInput
    int16_t ly = 0;
    int16_t rx = 0;
    int16_t ry = 0;
};

class PadSource {
public:
    virtual ~PadSource() = default;
    // Lê os controles (uma vez por tick de lógica).
    virtual void poll(PadState out[kMaxPads]) = 0;
    // Os motores: low é o grande (baixa frequência, o esquerdo do XInput), high o pequeno; 0 a 65535.
    virtual void vibrate(int pad, uint16_t low, uint16_t high) = 0;
};

// A fonte do sistema (o backend da plataforma: gamepad_xinput.cpp no Windows; no macOS, o GameController, etapa 3).
PadSource& system_pad_source();
// A fonte em uso: a do sistema, a não ser que set_pad_source tenha posto outra (nulo volta à do sistema).
PadSource& pad_source();
void set_pad_source(PadSource* source);
// Falso (main.cpp: janela sem foco): os controles continuam conectados, mas parados, e a vibração para.
extern bool g_pad_enabled;

// Fonte fixa (testes e capturas): devolve `state` e guarda os pedidos de vibração.
class ScriptedPadSource : public PadSource {
public:
    struct Vibration {
        int pad;
        uint16_t low;
        uint16_t high;
    };
    PadState state[kMaxPads];
    std::vector<Vibration> vibrations;
    void poll(PadState out[kMaxPads]) override;
    void vibrate(int pad, uint16_t low, uint16_t high) override;
};

}  // namespace nf
