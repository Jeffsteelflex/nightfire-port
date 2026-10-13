// Os controles no macOS: o backend de platform/gamepad.h com o GameController (GCController, o perfil
// GCExtendedGamepad; docs/port.md, "macOS: os backends"). Compilado com ARC (-fobjc-arc).
//
// O GameController reconhece sozinho os controles do Xbox (One, Series), o DualShock 4, o DualSense e o Pro Controller
// do Switch, por cabo ou Bluetooth, e dá todos no mesmo perfil: os botões pela posição (buttonA é o de baixo, o X do
// DualShock), os ombros, os gatilhos analógicos (0 a 1), os sticks (-1 a 1, y para cima, como o XInput), o direcional,
// Menu (Start, Options do DualShock), Options (Back, Share/Create; nem todos têm) e os cliques dos sticks. Este arquivo
// converte isso no PadState do XInput (os bits de XINPUT_GAMEPAD, os analógicos em -32768..32767, os gatilhos em 0..255),
// e daí o psi_pad.h faz o resto como no Windows: o primeiro controle no slot 0 do PS2, o segundo no 4.
//
// Os índices: o XInput dá a cada controle um índice fixo (0 a 3) enquanto ele está ligado; aqui o primeiro controle visto
// fica com o 0, o segundo com o 1 e assim por diante, e um índice só é reaproveitado depois que o controle dele sai (o
// playerIndex do GCController acompanha: a luz do controle, nos que têm).
//
// A leitura: na thread principal (run_on_main), onde o GameController atualiza os controles e avisa das conexões; fora
// do programa de janela (o nf_tests) na própria thread. Com o jogo sem o foco o GameController não manda nada (o padrão
// dele: os controles só valem para o aplicativo da frente), como o g_pad_enabled do Windows.
//
// A vibração fica de fora nesta etapa: no GameController ela é o Core Haptics (GCController.haptics, um CHHapticEngine
// por lado do controle, padrões contínuos com a intensidade mudada por parâmetros dinâmicos), não um "motor grande e
// motor pequeno" como no XInput; sem um controle para conferir, vibrate() não faz nada (docs/port.md).

#include "platform/gamepad.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "platform/main_thread_macos.h"

namespace nf {

namespace {

int16_t axis(float v) {
    const float c = std::clamp(v, -1.0f, 1.0f);
    return int16_t(std::lround(c * 32767.0f));
}

uint8_t trigger(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

bool pressed(GCControllerButtonInput* b) { return b != nil && b.isPressed; }

class GameControllerPads final : public PadSource {
public:
    void poll(PadState out[kMaxPads]) override {
        for (int i = 0; i < kMaxPads; ++i) out[i] = PadState{};
        PadState read[kMaxPads];
        macos::run_on_main([this, &read] { read_main(read); });
        for (int i = 0; i < kMaxPads; ++i) out[i] = read[i];
    }

    void vibrate(int, uint16_t, uint16_t) override {}

private:
    void read_main(PadState out[kMaxPads]) {
        @autoreleasepool {
            NSArray<GCController*>* list = [GCController controllers];
            // Os que saíram liberam o índice.
            for (int i = 0; i < kMaxPads; ++i) {
                if (slots_[size_t(i)] != nil && ![list containsObject:slots_[size_t(i)]]) slots_[size_t(i)] = nil;
            }
            // Os novos com o perfil estendido no primeiro índice livre.
            for (GCController* c in list) {
                if (c.extendedGamepad == nil) continue;
                bool known = false;
                for (int i = 0; i < kMaxPads; ++i) known = known || slots_[size_t(i)] == c;
                if (known) continue;
                for (int i = 0; i < kMaxPads; ++i) {
                    if (slots_[size_t(i)] == nil) {
                        slots_[size_t(i)] = c;
                        c.playerIndex = GCControllerPlayerIndex(i);
                        break;
                    }
                }
            }
            for (int i = 0; i < kMaxPads; ++i) {
                GCController* c = slots_[size_t(i)];
                GCExtendedGamepad* g = c != nil ? c.extendedGamepad : nil;
                if (g == nil) continue;
                PadState& s = out[i];
                s.connected = true;
                uint16_t b = 0;
                if (g.dpad.up.isPressed) b |= kPadDpadUp;
                if (g.dpad.down.isPressed) b |= kPadDpadDown;
                if (g.dpad.left.isPressed) b |= kPadDpadLeft;
                if (g.dpad.right.isPressed) b |= kPadDpadRight;
                if (pressed(g.buttonMenu)) b |= kPadStart;
                if (pressed(g.buttonOptions)) b |= kPadBack;
                if (pressed(g.leftThumbstickButton)) b |= kPadLThumb;
                if (pressed(g.rightThumbstickButton)) b |= kPadRThumb;
                if (g.leftShoulder.isPressed) b |= kPadLB;
                if (g.rightShoulder.isPressed) b |= kPadRB;
                if (g.buttonA.isPressed) b |= kPadA;
                if (g.buttonB.isPressed) b |= kPadB;
                if (g.buttonX.isPressed) b |= kPadX;
                if (g.buttonY.isPressed) b |= kPadY;
                s.buttons = b;
                s.lt = trigger(g.leftTrigger.value);
                s.rt = trigger(g.rightTrigger.value);
                s.lx = axis(g.leftThumbstick.xAxis.value);
                s.ly = axis(g.leftThumbstick.yAxis.value);
                s.rx = axis(g.rightThumbstick.xAxis.value);
                s.ry = axis(g.rightThumbstick.yAxis.value);
            }
        }
    }

    std::array<GCController*, kMaxPads> slots_{};
};

}  // namespace

PadSource& system_pad_source() {
    static GameControllerPads pads;
    return pads;
}

}  // namespace nf
