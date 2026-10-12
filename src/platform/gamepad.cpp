// Controles do PC pelo XInput. Ver gamepad.h.

#include "platform/gamepad.h"

#include <windows.h>
#include <Xinput.h>

namespace nf {

bool g_pad_enabled = true;

namespace {

class XInputPadSource : public PadSource {
public:
    void poll(PadState out[kMaxPads]) override {
        for (int i = 0; i < kMaxPads; ++i) {
            out[i] = PadState{};
            if (!g_pad_enabled) {
                out[i].connected = connected_[i];  // sem foco: conectado, mas parado
                continue;
            }
            // Índice vazio: só a cada 60 leituras (XInputGetState demora sem o aparelho).
            if (!connected_[i] && retry_[i]-- > 0) continue;
            XINPUT_STATE st{};
            if (XInputGetState(DWORD(i), &st) != ERROR_SUCCESS) {
                connected_[i] = false;
                retry_[i] = 60;
                continue;
            }
            connected_[i] = true;
            const XINPUT_GAMEPAD& g = st.Gamepad;
            out[i].connected = true;
            out[i].buttons = g.wButtons;
            out[i].lt = g.bLeftTrigger;
            out[i].rt = g.bRightTrigger;
            out[i].lx = g.sThumbLX;
            out[i].ly = g.sThumbLY;
            out[i].rx = g.sThumbRX;
            out[i].ry = g.sThumbRY;
        }
    }
    void vibrate(int pad, uint16_t low, uint16_t high) override {
        if (pad < 0 || pad >= kMaxPads || !connected_[pad]) return;
        if (!g_pad_enabled) low = high = 0;
        XINPUT_VIBRATION v{low, high};
        XInputSetState(DWORD(pad), &v);
    }

private:
    bool connected_[kMaxPads]{};
    int retry_[kMaxPads]{};
};

XInputPadSource g_xinput;
PadSource* g_source = nullptr;

}  // namespace

PadSource& pad_source() { return g_source != nullptr ? *g_source : g_xinput; }

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
