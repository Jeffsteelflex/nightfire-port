// As opções de jogabilidade só do PC. Ver pc_gameplay.h.

#include "platform/pc_gameplay.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "platform/input_bindings.h"
#include "platform/settings.h"

namespace nf {

namespace {
const char* const kStyleNames[3] = {"auto", "modern", "classic"};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}
}  // namespace

const float kWheelSlowmoValues[kWheelSlowmoChoices] = {0.0f, 0.1f, 0.25f, 0.5f, 1.0f};

int wheel_slowmo_index(float v) {
    int best = 0;
    for (int i = 1; i < kWheelSlowmoChoices; ++i) {
        if (std::fabs(kWheelSlowmoValues[i] - v) < std::fabs(kWheelSlowmoValues[best] - v)) best = i;
    }
    return best;
}

PcGameplay& pc_gameplay() {
    static PcGameplay g;
    return g;
}

void load_pc_gameplay(const Settings& s, PcGameplay* g) {
    if (s.has("game", "aim_style")) {
        const std::string v = lower(s.get("game", "aim_style"));
        for (uint8_t i = 0; i < 3; ++i) {
            if (v == kStyleNames[i]) g->aim_style = i;
        }
    }
    if (s.has("game", "wheel_slowmo")) {
        g->wheel_slowmo = std::clamp(s.get_float("game", "wheel_slowmo", kDefaultWheelSlowmo), 0.0f, 1.0f);
    }
}

void save_pc_gameplay(Settings& s, const PcGameplay& g) {
    if (s.has("game", "aim_style") || g.aim_style != 0) s.set("game", "aim_style", kStyleNames[std::min<int>(g.aim_style, 2)]);
    if (s.has("game", "wheel_slowmo") || std::fabs(g.wheel_slowmo - kDefaultWheelSlowmo) > 1e-4f) {
        s.set_float("game", "wheel_slowmo", g.wheel_slowmo);
    }
}

void persist_pc_gameplay() {
    if (!g_controls_persist) return;
    save_pc_gameplay(settings(), pc_gameplay());
    settings().save();
}

}  // namespace nf
