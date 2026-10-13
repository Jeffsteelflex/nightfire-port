// Controles do PC pelo XInput 1.4 (xinput.lib): a fonte do sistema no Windows (system_pad_source). Ver gamepad.h.

#include "platform/gamepad.h"

#include <windows.h>
#include <Xinput.h>

namespace nf {

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

}  // namespace

PadSource& system_pad_source() { return g_xinput; }

}  // namespace nf
