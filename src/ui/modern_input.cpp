// A entrada dos menus Modern dos menus do jogo. Ver modern_input.h.

#include "ui/modern_input.h"

#include <algorithm>
#include <cmath>

#include <windows.h>

#include "engine/input.h"
#include "platform/gamepad.h"
#include "platform/input_bindings.h"
#include "render/ui_hd.h"

namespace nf::ui {

namespace {
float stick(int16_t v) { return std::clamp(float(v) / 32767.0f, -1.0f, 1.0f); }
}  // namespace

void NavState::reset() {
    swallow = true;
    for (bool& h : held) h = false;
    for (float& r : repeat) r = 0.0f;
    prev_lmb = prev_back = true;
    clear_fired();
}

void NavState::clear_fired() {
    for (bool& f : fired) f = false;
    moved = pressed = back = false;
    wheel = 0.0f;
}

void NavState::read(const UiInput& in, const Canvas& cv, float dt, const Tokens& tk, bool enter_selects) {
    clear_fired();
    device = in.device;
    // O mouse.
    inside = in.mouse_inside;
    const float x = canvas_x(cv, in.mouse_x), y = canvas_y(cv, in.mouse_y);
    moved = inside && (std::fabs(x - last_mx) > 0.5f || std::fabs(y - last_my) > 0.5f);
    if (inside) {
        mx = last_mx = x;
        my = last_my = y;
    }
    lmb = in.lmb;
    pressed = in.lmb && !prev_lmb;
    back = in.back_button && !prev_back;
    prev_lmb = in.lmb;
    prev_back = in.back_button;
    wheel = in.wheel;
    // O teclado e o controle: os comandos do menu da tabela de teclas, Enter e Esc fixos, os botões do controle.
    nf::InputFrame f;
    f.keys = in.keys;
    f.pad = in.pad;
    f.pad_deadzone = 0.5f;
    const nf::Bindings& b = nf::controls().bindings;
    const auto cmd = [&](const char* key) {
        const int c = nf::find_bind_command(key);
        return c >= 0 && nf::command_value(b, c, f) > 0.5f;
    };
    const auto key = [&](int vk) { return in.keys != nullptr && in.keys[vk]; };
    const nf::PadState* pd = in.pad != nullptr && in.pad->connected ? in.pad : nullptr;
    const auto btn = [&](uint16_t mask) { return pd != nullptr && (pd->buttons & mask) != 0; };
    const float lx = pd != nullptr ? stick(pd->lx) : 0.0f, ly = pd != nullptr ? stick(pd->ly) : 0.0f;
    bool now[kNavCount];
    now[int(Nav::Up)] = cmd("menu_up") || btn(nf::kPadDpadUp) || ly > 0.55f;
    now[int(Nav::Down)] = cmd("menu_down") || btn(nf::kPadDpadDown) || ly < -0.55f;
    now[int(Nav::Left)] = cmd("menu_left") || btn(nf::kPadDpadLeft) || lx < -0.55f;
    now[int(Nav::Right)] = cmd("menu_right") || btn(nf::kPadDpadRight) || lx > 0.55f;
    const bool enter = key(VK_RETURN);
    // O "pause" (Enter por padrão), sem as entradas dele que também são do menu (Esc: aqui só volta).
    bool pause_cmd = false;
    {
        nf::Bindings only = b;
        nf::InputCode shared[nf::kMaxBinds];
        const int ns = nf::pause_menu_shared(b, shared);
        for (int i = 0; i < ns; ++i) {
            for (nf::InputCode& c : only.in[nf::pause_command()]) {
                if (c == shared[i]) c = nf::InputCode{};
            }
        }
        pause_cmd = nf::command_value(only, nf::pause_command(), f) > 0.5f;
    }
    now[int(Nav::Select)] = cmd("menu_select") || btn(nf::kPadA) || (enter_selects && enter);
    now[int(Nav::Back)] = cmd("menu_back") || key(VK_ESCAPE) || btn(nf::kPadB);
    now[int(Nav::TabPrev)] = cmd("menu_tab_prev");
    now[int(Nav::TabNext)] = cmd("menu_tab_next");
    // O Start: o do controle e a tecla da pausa (menos o Enter, quando ele escolhe).
    now[int(Nav::Start)] = btn(nf::kPadStart) || (pause_cmd && !(enter_selects && enter));
    if (swallow) {
        bool any = in.lmb || in.back_button;
        for (bool h : now) any = any || h;
        if (!any) swallow = false;
        for (int i = 0; i < kNavCount; ++i) held[i] = now[i];
        pressed = back = false;
        wheel = 0.0f;
        return;
    }
    for (int i = 0; i < kNavCount; ++i) {
        bool fire = now[i] && !held[i];
        if (i < 4) {
            // As setas seguradas repetem.
            if (!now[i]) {
                repeat[i] = 0.0f;
            } else if (fire) {
                repeat[i] = tk.motion.repeat_delay;
            } else {
                repeat[i] -= dt;
                if (repeat[i] <= 0.0f) {
                    fire = true;
                    repeat[i] += tk.motion.repeat_rate;
                }
            }
        }
        held[i] = now[i];
        fired[i] = fire;
        if (fire) mouse_active = false;
    }
    if (moved || pressed || back || wheel != 0.0f) mouse_active = true;
}

std::string xbox_glyph(const nf::InputCode& c) {
    if (!c.is_pad()) return {};
    switch (c.code) {
        case nf::kPadInA: return "xbox_a";
        case nf::kPadInB: return "xbox_b";
        case nf::kPadInX: return "xbox_x";
        case nf::kPadInY: return "xbox_y";
        case nf::kPadInLB: return "xbox_lb";
        case nf::kPadInRB: return "xbox_rb";
        case nf::kPadInLT: return "xbox_lt";
        case nf::kPadInRT: return "xbox_rt";
        case nf::kPadInLThumb: return "xbox_ls";
        case nf::kPadInRThumb: return "xbox_rs";
        case nf::kPadInStart: return "xbox_menu";
        case nf::kPadInBack: return "xbox_view";
        case nf::kPadInDpadUp:
        case nf::kPadInDpadDown: return "xbox_dpad_v";
        case nf::kPadInDpadLeft:
        case nf::kPadInDpadRight: return "xbox_dpad_h";
        default: return {};
    }
}

std::string symbol_glyph(char symbol, bool triggers_r1) {
    const char* name = nf::ui_hd_xbox_name(uint8_t(symbol), triggers_r1);
    if (name == nullptr) return {};
    const std::string n = name;
    if (n == "DpadV") return "xbox_dpad_v";
    if (n == "DpadH") return "xbox_dpad_h";
    std::string s = "xbox_";
    for (const char c : n) s += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return s;
}

std::wstring symbol_keys(char symbol, uint16_t style) {
    const nf::ControlsConfig& c = nf::controls();
    return nf::ui_hd_prompt_keys(uint8_t(symbol), true, c.bindings, style, c.pad_triggers);
}

std::wstring bind_key(const char* command) {
    const int c = nf::find_bind_command(command);
    nf::InputCode in[nf::kMaxBinds];
    if (c >= 0 && nf::controls().bindings.list(c, false, in, nf::kMaxBinds) > 0) return nf::ui_hd_key_label(in[0]);
    return std::wstring();
}

std::string bind_glyph(const char* command, const char* fallback) {
    const int c = nf::find_bind_command(command);
    nf::InputCode in[nf::kMaxBinds];
    if (c >= 0 && nf::controls().bindings.list(c, true, in, nf::kMaxBinds) > 0) {
        const std::string g = xbox_glyph(in[0]);
        if (!g.empty()) return g;
    }
    return fallback != nullptr ? fallback : "";
}

}  // namespace nf::ui
