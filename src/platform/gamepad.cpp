// Controles do PC: a fonte em uso e a fonte fixa dos testes e das capturas. Ver gamepad.h. A fonte do sistema é a do
// backend da plataforma (gamepad_xinput.cpp: o XInput 1.4 do Windows).

#include "platform/gamepad.h"

namespace nf {

bool g_pad_enabled = true;

namespace {
PadSource* g_source = nullptr;
}  // namespace

PadSource& pad_source() { return g_source != nullptr ? *g_source : system_pad_source(); }

void set_pad_source(PadSource* source) { g_source = source; }

void ScriptedPadSource::poll(PadState out[kMaxPads]) {
    for (int i = 0; i < kMaxPads; ++i) {
        out[i] = state[i];
        if (!g_pad_enabled) {
            out[i] = PadState{};
            out[i].connected = state[i].connected;
        }
    }
}

void ScriptedPadSource::vibrate(int pad, uint16_t low, uint16_t high) { vibrations.push_back({pad, low, high}); }

}  // namespace nf
