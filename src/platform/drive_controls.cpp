// Os controles de PC das missões de carro (só do port; ver drive_controls.h e docs/driving.md seção 33).

#include "platform/drive_controls.h"

#include <cstdlib>
#include <cstring>

#include "platform/gamepad.h"
#include "platform/keys.h"
#include "platform/settings.h"

namespace nf {

namespace {
using P = Ps2In;
constexpr P N = P::None;
}  // namespace

// Os alvos por modo: Drive (drivecfg.def), Aim e AimZoom (povcfg0.def), Menu (FrontEnd.def). Ver o cabeçalho e a tabela
// da seção 33 do docs/driving.md.
const DriveCommand kDriveCommands[kDriveCommandCount] = {
    // Só no menu: o X aceita (FRONTENDACTION_ACCEPT), o triângulo volta (FRONTENDACTION_CANCEL).
    {"menu_accept", {N, N, N, P::Cross}, "Aceitar (menu)", "Accept (menu)", "enter, space"},
    {"menu_back", {N, N, N, P::Triangle}, "Voltar (menu)", "Back (menu)", "backspace"},
    // Só com a luneta fechada (povcfg0: GAMEACTION_CAMZOOMIN/OUT no D-pad para cima e para baixo).
    {"zoom_in", {N, N, P::Up, N}, "Aproximar (luneta)", "Zoom in (scope)", "wheel_up"},
    {"zoom_out", {N, N, P::Down, N}, "Afastar (luneta)", "Zoom out (scope)", "wheel_down"},
    // Dirigindo, o X e o quadrado (GAS, BRAKE); mirando, o direcional esquerdo em y (CAMROTATEY); no menu, o D-pad.
    {"accelerate", {P::Cross, P::LYNeg, P::LYNeg, P::Up}, "Acelerar / mirar para cima", "Accelerate / aim up", "w, up"},
    {"brake", {P::Square, P::LYPos, P::LYPos, P::Down}, "Frear e ré / mirar para baixo", "Brake and reverse / aim down",
     "s, down"},
    {"steer_left", {P::LXNeg, P::LXNeg, P::LXNeg, P::Left}, "Virar / mirar à esquerda", "Steer / aim left", "a, left"},
    {"steer_right", {P::LXPos, P::LXPos, P::LXPos, P::Right}, "Virar / mirar à direita", "Steer / aim right",
     "d, right"},
    {"handbrake", {P::Circle, N, N, N}, "Freio de mão", "Handbrake", "space"},
    // R1: a arma secundária (FIRESECONDARY) dirigindo e o tiro do fuzil mirando.
    {"fire", {P::R1, P::R1, P::R1, N}, "Atirar", "Fire", "mouse1, ctrl"},
    // L1: o aparelho (FIREGADGET: o Q-Pulse do Vanquish) dirigindo, a luneta (AIMZOOMRIGHT/AIMRELEASE) mirando.
    {"special", {P::L1, P::L1, P::L1, N}, "Aparelho / luneta", "Gadget / scope", "mouse2, shift"},
    // A troca da arma secundária (TOGGLESECONDARYDOWN/UP no D-pad à direita e à esquerda, nas duas configurações).
    {"weapon_next", {P::Right, P::Right, P::Right, N}, "Próxima arma", "Next weapon", "wheel_down, e"},
    {"weapon_prev", {P::Left, P::Left, P::Left, N}, "Arma anterior", "Previous weapon", "wheel_up, q"},
    // POVRELOAD (o quadrado no povcfg0).
    {"reload", {N, P::Square, P::Square, N}, "Recarregar (mirando)", "Reload (aiming)", "r"},
    // O triângulo: CHANGECAMERA dirigindo, CAMSPIN mirando (só na pista snow1a_mis3).
    {"change_camera", {P::Triangle, P::Triangle, P::Triangle, N}, "Trocar câmera", "Change camera", "f"},
    {"look_back", {P::L2, N, N, N}, "Olhar para trás", "Look back", "c"},
    // O Start: a pausa do HUD (e o pulo das cenas, como no PS2).
    {"pause", {P::Start, P::Start, P::Start, P::Start}, "Pausa", "Pause", "escape, enter"},
};

int find_drive_command(const std::string& key) {
    for (int i = 0; i < kDriveCommandCount; ++i)
        if (key == kDriveCommands[i].key) return i;
    return -1;
}

int ps2_button_bit(Ps2In in) {
    const int v = int(in);
    return v >= int(Ps2In::L2) && v <= int(Ps2In::Left) ? v - int(Ps2In::L2) : -1;
}

int ps2_pressure_index(Ps2In in) {
    switch (in) {
        case Ps2In::Right: return 0;
        case Ps2In::Left: return 1;
        case Ps2In::Up: return 2;
        case Ps2In::Down: return 3;
        case Ps2In::Triangle: return 4;
        case Ps2In::Circle: return 5;
        case Ps2In::Cross: return 6;
        case Ps2In::Square: return 7;
        case Ps2In::L1: return 8;
        case Ps2In::R1: return 9;
        case Ps2In::L2: return 10;
        case Ps2In::R2: return 11;
        default: return -1;
    }
}

void ps2_press(Ps2PadFrame& p, Ps2In in) {
    const int bit = ps2_button_bit(in);
    if (bit >= 0) {
        p.buttons |= uint16_t(1u << bit);
        const int pr = ps2_pressure_index(in);
        if (pr >= 0) p.pressure[pr] = 255;
        return;
    }
    switch (in) {
        case Ps2In::LXNeg: p.lx = 0; break;
        case Ps2In::LXPos: p.lx = 255; break;
        case Ps2In::LYNeg: p.ly = 0; break;
        case Ps2In::LYPos: p.ly = 255; break;
        case Ps2In::RXNeg: p.rx = 0; break;
        case Ps2In::RXPos: p.rx = 255; break;
        case Ps2In::RYNeg: p.ry = 0; break;
        case Ps2In::RYPos: p.ry = 255; break;
        default: break;
    }
}

const char* ps2_input_name(Ps2In in) {
    static const char* const names[] = {"-",  "L2", "R2", "L1",    "R1",   "triangulo", "bola", "X",   "quadrado",
                                        "Select", "L3", "R3", "Start", "cima", "direita",  "baixo", "esquerda", "LX-",
                                        "LX+", "LY-", "LY+", "RX-",   "RX+",  "RY-",       "RY+"};
    const int v = int(in);
    return v >= 0 && v < int(sizeof names / sizeof names[0]) ? names[v] : "?";
}

const char* drive_mode_name(DriveMode m) {
    switch (m) {
        case DriveMode::Drive: return "conducao";
        case DriveMode::Aim: return "mira";
        case DriveMode::AimZoom: return "mira_zoom";
        case DriveMode::Menu: return "menu";
    }
    return "?";
}

bool DriveBindings::has(int command, InputCode c) const {
    if (command < 0 || command >= kDriveCommandCount) return false;
    for (const InputCode& x : in[command])
        if (x.valid() && x == c) return true;
    return false;
}

namespace {
// Só teclado, mouse e roda (as do controle não entram: ele vai pela posição dos botões).
int parse_drive_inputs(const std::string& text, InputCode* out) {
    InputCode tmp[kMaxBinds];
    parse_inputs(text, tmp, kMaxBinds);
    int n = 0;
    for (int i = 0; i < kMaxKeyBinds; ++i) out[i] = InputCode{};
    for (const InputCode& c : tmp)
        if (c.valid() && !c.is_pad() && n < kMaxKeyBinds) out[n++] = c;
    return n;
}
}  // namespace

DriveBindings default_drive_bindings() {
    DriveBindings b;
    for (int i = 0; i < kDriveCommandCount; ++i) parse_drive_inputs(kDriveCommands[i].defaults, b.in[i]);
    return b;
}

void load_drive_bindings(const Settings& s, DriveBindings* b) {
    *b = default_drive_bindings();
    for (int i = 0; i < kDriveCommandCount; ++i)
        if (s.has("driving_bindings", kDriveCommands[i].key))
            parse_drive_inputs(s.get("driving_bindings", kDriveCommands[i].key), b->in[i]);
}

DriveMouseConfig load_drive_mouse(const Settings& s) {
    DriveMouseConfig m;
    m.sensitivity = s.get_float("controls", "mouse_sensitivity", m.sensitivity);
    m.sensitivity = s.get_float("driving", "mouse_sensitivity", m.sensitivity);
    if (!(m.sensitivity > 0.0f) || m.sensitivity > 1.0f) m.sensitivity = kDefaultMouseSensitivity;
    m.invert_y = s.get_bool("controls", "mouse_invert_y", m.invert_y);
    m.invert_y = s.get_bool("driving", "mouse_invert_y", m.invert_y);
    return m;
}

void pc_pad_to_ps2(const PadState& pc, Ps2PadFrame& p, bool sticks) {
    if (!pc.connected) return;
    const uint16_t b = pc.buttons;
    if (b & kPadA) ps2_press(p, Ps2In::Cross);
    if (b & kPadB) ps2_press(p, Ps2In::Circle);
    if (b & kPadX) ps2_press(p, Ps2In::Square);
    if (b & kPadY) ps2_press(p, Ps2In::Triangle);
    if (b & kPadLB) ps2_press(p, Ps2In::L1);
    if (b & kPadRB) ps2_press(p, Ps2In::R1);
    if (pc.lt > kPadTriggerThreshold) ps2_press(p, Ps2In::L2);
    if (pc.rt > kPadTriggerThreshold) ps2_press(p, Ps2In::R2);
    if (b & kPadBack) ps2_press(p, Ps2In::Select);
    if (b & kPadLThumb) ps2_press(p, Ps2In::L3);
    if (b & kPadRThumb) ps2_press(p, Ps2In::R3);
    if (b & kPadStart) ps2_press(p, Ps2In::Start);
    if (b & kPadDpadUp) ps2_press(p, Ps2In::Up);
    if (b & kPadDpadRight) ps2_press(p, Ps2In::Right);
    if (b & kPadDpadDown) ps2_press(p, Ps2In::Down);
    if (b & kPadDpadLeft) ps2_press(p, Ps2In::Left);
    if (sticks) {
        // -32768..32767 para 0..255; o y do PS2 para baixo.
        auto byte = [](int v) { return uint8_t((v + 32768) >> 8); };
        p.lx = byte(pc.lx);
        p.ly = uint8_t(255 - byte(pc.ly));
        p.rx = byte(pc.rx);
        p.ry = uint8_t(255 - byte(pc.ry));
    }
}

Ps2PadFrame ps2_merge(const Ps2PadFrame& a, const Ps2PadFrame& b) {
    Ps2PadFrame r = a;
    r.buttons = uint16_t(a.buttons | b.buttons);
    for (int i = 0; i < 12; ++i) r.pressure[i] = a.pressure[i] > b.pressure[i] ? a.pressure[i] : b.pressure[i];
    if (b.lx != 128) r.lx = b.lx;
    if (b.ly != 128) r.ly = b.ly;
    if (b.rx != 128) r.rx = b.rx;
    if (b.ry != 128) r.ry = b.ry;
    return r;
}

namespace {
bool is_mouse_button(InputCode c) {
    return c.kind == InputKind::Key &&
           (c.code == key::LButton || c.code == key::RButton || c.code == key::MButton || c.code == key::XButton1 ||
            c.code == key::XButton2);
}
int mouse_slot(int code) {
    switch (code) {
        case key::LButton: return 0;
        case key::RButton: return 1;
        case key::MButton: return 2;
        case key::XButton1: return 3;
        case key::XButton2: return 4;
        default: return -1;
    }
}
constexpr int kMouseCodes[5] = {key::LButton, key::RButton, key::MButton, key::XButton1, key::XButton2};
}  // namespace

Ps2PadFrame DriveController::update(const DriveFrame& f, DriveMode mode) {
    ++frames;
    const int m = int(mode);
    auto raw_down = [&](int code) { return f.key_down ? f.key_down(code) : false; };
    // Os botões do mouse: só capturado; presos ao capturar ou ao sair do menu, fora até soltar.
    const bool leaving_menu = last_mode == DriveMode::Menu && mode != DriveMode::Menu;
    const bool capturing = f.mouse_captured && !prev_captured_;
    for (int i = 0; i < 5; ++i) {
        const bool held = raw_down(kMouseCodes[i]);
        if ((capturing || leaving_menu) && held) mouse_block_[i] = true;
        if (!held) mouse_block_[i] = false;
    }
    prev_captured_ = f.mouse_captured;
    last_mode = mode;
    auto down = [&](InputCode c) {
        if (c.kind != InputKind::Key) return false;
        if (is_mouse_button(c)) {
            const int s = mouse_slot(c.code);
            if (!f.mouse_captured || (s >= 0 && mouse_block_[s])) return false;
        }
        return raw_down(c.code);
    };

    // As teclas: cada entrada vale para o primeiro comando da tabela com alvo neste modo.
    Ps2PadFrame keys;
    bool kbm_used = false;
    InputCode claimed[kDriveCommandCount * kMaxKeyBinds];
    int nclaimed = 0;
    auto is_claimed = [&](InputCode c) {
        for (int i = 0; i < nclaimed; ++i)
            if (claimed[i] == c) return true;
        return false;
    };
    bool stick_neg[4] = {}, stick_pos[4] = {};  // lx, ly, rx, ry
    for (int c = 0; c < kDriveCommandCount; ++c) {
        active_[c] = false;
        const Ps2In t = kDriveCommands[c].target[m];
        for (int k = 0; k < kMaxKeyBinds; ++k) {
            const InputCode in = bindings.in[c][k];
            const bool d = in.kind == InputKind::Key && down(in);
            const bool edge = d && !prev_key_[c][k];
            prev_key_[c][k] = d;
            if (t == Ps2In::None || !d || is_claimed(in)) continue;
            claimed[nclaimed++] = in;
            active_[c] = true;
            if (edge) kbm_used = true;
        }
        if (!active_[c]) continue;
        switch (t) {
            case Ps2In::LXNeg: stick_neg[0] = true; break;
            case Ps2In::LXPos: stick_pos[0] = true; break;
            case Ps2In::LYNeg: stick_neg[1] = true; break;
            case Ps2In::LYPos: stick_pos[1] = true; break;
            case Ps2In::RXNeg: stick_neg[2] = true; break;
            case Ps2In::RXPos: stick_pos[2] = true; break;
            case Ps2In::RYNeg: stick_neg[3] = true; break;
            case Ps2In::RYPos: stick_pos[3] = true; break;
            default: ps2_press(keys, t); break;
        }
    }
    // As duas metades juntas se anulam (o centro).
    uint8_t* axes[4] = {&keys.lx, &keys.ly, &keys.rx, &keys.ry};
    for (int i = 0; i < 4; ++i)
        if (stick_neg[i] != stick_pos[i]) *axes[i] = stick_neg[i] ? 0 : 255;

    // A roda: cada entalhe um toque do alvo do primeiro comando com ela neste modo (segurado kDriveWheelPulseTicks).
    if (f.wheel != 0) {
        kbm_used = true;
        const InputCode w = wheel_input(f.wheel < 0);
        const int n = std::abs(f.wheel);
        Ps2In t = Ps2In::None;
        for (int c = 0; c < kDriveCommandCount && t == Ps2In::None; ++c)
            if (kDriveCommands[c].target[m] != Ps2In::None && bindings.has(c, w)) t = kDriveCommands[c].target[m];
        for (int i = 0; i < n; ++i) {
            ++wheel_notches;
            if (t != Ps2In::None && queued_ < int(sizeof queue_ / sizeof queue_[0])) queue_[queued_++] = t;
        }
    }
    if (pulse_ != Ps2In::None && f.clock >= pulse_until) {
        pulse_ = Ps2In::None;
        pulse_gap_ = true;  // um quadro solto: o próximo toque é um aperto novo
    } else if (pulse_ == Ps2In::None && !pulse_gap_ && queued_ > 0) {
        pulse_ = queue_[0];
        for (int i = 1; i < queued_; ++i) queue_[i - 1] = queue_[i];
        --queued_;
        pulse_until = f.clock + kDriveWheelPulseTicks;
        ++pulses;
    } else {
        pulse_gap_ = false;
    }
    if (pulse_ != Ps2In::None) ps2_press(keys, pulse_);

    // O mouse.
    aim_yaw = aim_pitch = 0.0f;
    if (f.mouse_captured && (f.mouse_dx != 0.0f || f.mouse_dy != 0.0f)) {
        kbm_used = true;
        ++mouse_frames;
        mouse_dx_total += f.mouse_dx;
        mouse_dy_total += f.mouse_dy;
        if (mode == DriveMode::Aim || mode == DriveMode::AimZoom) {
            aim_yaw = f.mouse_dx * mouse.sensitivity;
            aim_pitch = -f.mouse_dy * mouse.sensitivity * (mouse.invert_y ? -1.0f : 1.0f);
            aim_yaw_total += aim_yaw;
            aim_pitch_total += aim_pitch;
        }
    }

    // O controle do PC: um botão, um gatilho ou um direcional novo é uso.
    Ps2PadFrame pad;
    bool pad_used = false;
    const bool pad_on = f.pad && f.pad->connected;
    if (pad_on) {
        const PadState& pc = *f.pad;
        if ((pc.buttons & ~prev_pad_buttons_) != 0) pad_used = true;
        const bool lt = pc.lt > kPadTriggerThreshold, rt = pc.rt > kPadTriggerThreshold;
        if ((lt && !prev_pad_lt_) || (rt && !prev_pad_rt_)) pad_used = true;
        const int st[4] = {pc.lx, pc.ly, pc.rx, pc.ry};
        for (int i = 0; i < 4; ++i) {
            const bool out = std::abs(st[i]) > kDrivePadStickUse;
            if (out && !prev_pad_stick_[i]) pad_used = true;
            prev_pad_stick_[i] = out;
        }
        prev_pad_buttons_ = pc.buttons;
        prev_pad_lt_ = lt;
        prev_pad_rt_ = rt;
        pc_pad_to_ps2(pc, pad, true);
    } else {
        prev_pad_buttons_ = 0;
        prev_pad_lt_ = prev_pad_rt_ = false;
        for (bool& b : prev_pad_stick_) b = false;
    }
    const DriveDevice before = device_;
    if (pad_used) device_ = DriveDevice::Pad;
    if (kbm_used) device_ = DriveDevice::KeyboardMouse;
    if (!pad_on && device_ == DriveDevice::Pad) device_ = DriveDevice::KeyboardMouse;
    if (device_ != before) ++device_switches;

    // Os botões somam; os direcionais são os do último aparelho usado.
    Ps2PadFrame out;
    out.buttons = uint16_t(keys.buttons | pad.buttons);
    for (int i = 0; i < 12; ++i) out.pressure[i] = keys.pressure[i] > pad.pressure[i] ? keys.pressure[i] : pad.pressure[i];
    const Ps2PadFrame& sticks = device_ == DriveDevice::Pad ? pad : keys;
    out.lx = sticks.lx;
    out.ly = sticks.ly;
    out.rx = sticks.rx;
    out.ry = sticks.ry;
    return out;
}

}  // namespace nf
