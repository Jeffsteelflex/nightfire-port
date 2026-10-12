// As abas só do port de controles e de teclas da página de opções do PC. Ver menu_pc_controls.h.

#include "engine/menu_pc_controls.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/input.h"
#include "engine/menu.h"
#include "engine/pc_text.h"
#include "platform/input_bindings.h"
#include "platform/pc_gameplay.h"
#include "platform/psi_input.h"
#include "platform/psi_pad.h"

namespace {

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }

std::string g_status_utf8;
std::string g_device_utf8;
// O comando esperando a tecla (-1: nenhum).
int g_capture_command = -1;

const char* utf8(PcText id) { return PcText_Utf8(id); }
bool english() { return PcText_Language() == "en"; }

std::string fmt(PcText id, double v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), utf8(id), v);
    return buf;
}

std::string fmt_int(PcText id, int v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), utf8(id), v);
    return buf;
}

// ---- controles ----
// Barras: posição <-> valor.
// A sensibilidade do mouse: a barra logarítmica de nf::mouse_sens_to_pos (a mesma da tela Modern).
int32_t mouse_pos() { return nf::mouse_sens_to_pos(psiMouseSensitivity); }
int32_t aim_pos() { return std::clamp(int32_t(nf::controls().mouse_aim_scale * 20.0f + 0.5f), 2, 40); }
int32_t pad_pos() { return std::clamp(int32_t(nf::controls().pad_sensitivity * 10.0f + 0.5f), 1, 30); }
int32_t deadzone_pos() { return std::clamp(int32_t(nf::controls().pad_deadzone * 127.0f / 2.0f + 0.5f), 0, 30); }

bool toggle_value(uint32_t id) {
    const PlayerInput_tag& p = PlayerSetting[0];
    switch (id) {
        case kPcRowMouseInvert: return nf::controls().mouse_invert_y;
        case kPcRowPadInvert: return p.invert_look != 0;
        case kPcRowVibration: return p.unknown_09 != 0;
        case kPcRowTriggers: return nf::controls().pad_triggers;
        default: return false;
    }
}

std::string device_text() {
    const int pad = psiInput_PadIndex();
    if (pad < 0) return utf8(PcText::CtlKeyboardMouse);
    return fmt_int(psiInput_ActiveDevice() == 1 ? PcText::CtlPad : PcText::CtlKeyboardPad, pad + 1);
}

// Os nomes das barras levam o valor.
void slider_names(uint8_t index) {
    MenuPc_SetRowName(index, kPcRowMouseSens, fmt(PcText::CtlMouseSens, double(psiMouseSensitivity * 1000.0f)));
    MenuPc_SetRowName(index, kPcRowAimSens, fmt(PcText::CtlAimSens, double(nf::controls().mouse_aim_scale)));
    MenuPc_SetRowName(index, kPcRowPadSens, fmt(PcText::CtlPadSens, double(nf::controls().pad_sensitivity)));
    MenuPc_SetRowName(index, kPcRowDeadzone, fmt_int(PcText::CtlDeadzone, int(nf::controls().pad_deadzone * 100.0f + 0.5f)));
}

void controls_enter(uint8_t index) {
    g_device_utf8 = device_text();
    MenuPc_SetRowName(index, kPcRowDevice, utf8(PcText::CtlDevice));
    MenuPc_SetRowValue(index, kPcRowDevice, g_device_utf8);
    MenuPc_SetRowName(index, kPcRowMouseInvert, utf8(PcText::CtlMouseInvert));
    MenuPc_SetRowName(index, kPcRowPadInvert, utf8(PcText::CtlPadInvert));
    MenuPc_SetRowName(index, kPcRowVibration, utf8(PcText::CtlVibration));
    MenuPc_SetRowName(index, kPcRowTriggers, utf8(PcText::CtlTriggers));
    MenuPc_SetRowName(index, kPcRowAimStyle, utf8(PcText::MdAimStyle));
    MenuPc_SetChoices(index, kPcRowAimStyle,
                      {utf8(PcText::AimStyleAuto), utf8(PcText::AimStyleModern), utf8(PcText::AimStyleClassic)},
                      std::clamp(int32_t(nf::pc_gameplay().aim_style), 0, 2));
    slider_names(index);
    MenuPc_SetSlider(index, kPcRowMouseSens, mouse_pos());
    MenuPc_SetSlider(index, kPcRowAimSens, aim_pos());
    MenuPc_SetSlider(index, kPcRowPadSens, pad_pos());
    MenuPc_SetSlider(index, kPcRowDeadzone, deadzone_pos());
    for (const uint32_t id : {kPcRowMouseInvert, kPcRowPadInvert, kPcRowVibration, kPcRowTriggers}) {
        MenuPc_SetChoices(index, id, {utf8(PcText::Off), utf8(PcText::On)}, toggle_value(id) ? 1 : 0);
    }
}

// O aparelho em uso muda com o jogador mexendo no teclado ou no controle.
void controls_tick(uint8_t index) {
    const std::string device = device_text();
    if (device == g_device_utf8) return;
    g_device_utf8 = device;
    MenuPc_SetRowValue(index, kPcRowDevice, device);
}

void build_controls(uint8_t, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Controls, kPcRowDevice, PcRowKind::Info);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowMouseSens, PcRowKind::Slider, nf::kMouseSensPosMin, nf::kMouseSensPosMax);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowAimSens, PcRowKind::Slider, 2, 40);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowMouseInvert, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowPadSens, PcRowKind::Slider, 1, 30);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowDeadzone, PcRowKind::Slider, 0, 30);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowPadInvert, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowVibration, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowTriggers, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Controls, kPcRowAimStyle, PcRowKind::Choice);
}

// As linhas: as barras (0x49) mudam a opção; os rádios Off/On (0x49), também; escolher num rádio troca.
int32_t C_PCCONTROLS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t, int32_t) {
    if (c == nullptr) return 0;
    nf::ControlsConfig& cfg = nf::controls();
    PlayerInput_tag& p = PlayerSetting[0];
    if (message == 0x4b && c->type == MCT_RADIO) {
        M_RADIO* r = reinterpret_cast<M_RADIO*>(c);
        // Escolher avança (Off/On alterna; o estilo de mira dá a volta nos três).
        const int32_t n = id == kPcRowAimStyle ? 3 : 2;
        Radio_SendMessage(r, 0x1d, (MenuPc_Choice(index, id) + 1) % n, 0);
        return 1;
    }
    if (message != 0x49) return 0;
    if (c->type == MCT_SCROLL) {
        const int32_t pos = MenuPc_Slider(index, id);
        switch (id) {
            case kPcRowMouseSens: psiMouseSensitivity = nf::mouse_sens_from_pos(pos); break;
            case kPcRowAimSens: cfg.mouse_aim_scale = float(pos) * 0.05f; break;
            case kPcRowPadSens: cfg.pad_sensitivity = float(pos) * 0.1f; break;
            case kPcRowDeadzone: cfg.pad_deadzone = float(pos * 2) / 127.0f; break;
            default: return 0;
        }
        slider_names(index);
    } else if (c->type == MCT_RADIO) {
        const bool on = MenuPc_Choice(index, id) == 1;
        switch (id) {
            case kPcRowMouseInvert: cfg.mouse_invert_y = on; break;
            case kPcRowPadInvert:
                p.invert_look = on ? 1 : 0;
                cfg.pad_invert_y = on;
                break;
            case kPcRowVibration:
                p.unknown_09 = on ? 1 : 0;
                cfg.vibration = on;
                if (!on) Input_RumbleStop(0);
                break;
            case kPcRowTriggers: cfg.pad_triggers = on; break;
            case kPcRowAimStyle:
                // Só do PC: vai para [game] (platform/pc_gameplay.h), não para [controls].
                nf::pc_gameplay().aim_style = uint8_t(std::clamp(MenuPc_Choice(index, id), 0, 2));
                nf::persist_pc_gameplay();
                return 1;
            default: return 0;
        }
    } else {
        return 0;
    }
    nf::persist_controls();
    return 1;
}

// ---- teclas ----
const char* command_label(int command) {
    return english() ? nf::kBindCommands[command].label_en : nf::kBindCommands[command].label_pt;
}

std::string pad_text(int command) {
    std::string s;
    nf::InputCode pad[nf::kMaxBinds];
    const int n = nf::controls().bindings.list(command, true, pad, nf::kMaxBinds);
    for (int i = 0; i < n; ++i) {
        if (!s.empty()) s += " ";
        s += nf::input_label(pad[i], english());
    }
    if (!s.empty()) return s;
    // Sem entrada na tabela: o que o estilo de botões do jogo faz (entre parênteses).
    const int action = nf::pad_style_action(command);
    if (action < 0) return "-";
    const uint32_t bits = psi_pad_style_inputs(PlayerSetting[0].style, action, nf::kBindCommands[command].targets[0].sign,
                                               nf::controls().pad_triggers);
    for (uint8_t code = 0; code < nf::kPadInCount; ++code) {
        if ((bits & (1u << code)) == 0) continue;
        if (!s.empty()) s += " ";
        s += nf::input_label(nf::pad_input(code), english());
    }
    return s.empty() ? "-" : "(" + s + ")";
}

std::string keys_text(int command) {
    std::string s;
    nf::InputCode keys[nf::kMaxBinds];
    const int n = nf::controls().bindings.list(command, false, keys, nf::kMaxBinds);
    for (int i = 0; i < n; ++i) {
        if (!s.empty()) s += ", ";
        s += nf::input_label(keys[i], english());
    }
    return s.empty() ? "-" : s;
}

M_CONTROL* keys_list(uint8_t index) { return MenuPc_Control(index, kPcKeysList); }

// Uma célula da lista (a lista copia o texto).
void cell(M_CONTROL* list, int row, int col, const std::string& text) {
    char buf[0x80];
    std::snprintf(buf, sizeof(buf), "%s", PcText_ToGame(text.c_str()).c_str());
    __Menu_SendMessage(list, 0x19, int32_t(uint32_t(row) << 16 | uint32_t(col)), val(buf));
}

// As linhas (na primeira vez, montadas: três colunas, o nome, o teclado/mouse e o controle; senão, os textos).
void fill_keys_list(M_CONTROL* list) {
    if (list == nullptr) return;
    const bool fresh = __Menu_SendMessage(list, 0x33, 0, 0) != nf::kBindCommandCount + 1;
    if (fresh) {
        __Menu_SendMessage(list, 0x17, 0, 0);
        __Menu_SendMessage(list, 0x28, 0, 44);
        __Menu_SendMessage(list, 0xe, 32, 1);
        __Menu_SendMessage(list, 0xe, 24, 1);
    }
    char buf[0x80];
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        std::snprintf(buf, sizeof(buf), "%s", PcText_ToGame(command_label(i)).c_str());
        if (fresh) {
            __Menu_SendMessage(list, 0x10, val(buf), i);
        } else {
            __Menu_SendMessage(list, 0x19, int32_t(uint32_t(i) << 16), val(buf));
        }
        cell(list, i, 1, keys_text(i));
        cell(list, i, 2, pad_text(i));
    }
    std::snprintf(buf, sizeof(buf), "%s", PcText_ToGame(utf8(PcText::KeysRestore)).c_str());
    if (fresh) {
        __Menu_SendMessage(list, 0x10, val(buf), kPcKeysRestoreRow);
    } else {
        __Menu_SendMessage(list, 0x19, int32_t(uint32_t(nf::kBindCommandCount) << 16), val(buf));
    }
}

void set_status(uint8_t index, const std::string& text) {
    g_status_utf8 = text;
    MenuPc_SetRowValue(index, kPcKeysStatus, text);
}

std::string fmt(PcText id, const std::string& a, const std::string& b = {}, const std::string& c = {},
                const std::string& d = {}) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), utf8(id), a.c_str(), b.c_str(), c.c_str(), d.c_str());
    return buf;
}

// O fim de uma captura (no tick da aba).
void finish_capture(uint8_t index) {
    const PsiCapture state = psiInput_CaptureState();
    if (state == PsiCapture::Waiting || state == PsiCapture::Idle || g_capture_command < 0) return;
    const int command = g_capture_command;
    const std::string name = command_label(command);
    nf::ControlsConfig& cfg = nf::controls();
    nf::InputCode code;
    if (state == PsiCapture::Cancelled) {
        set_status(index, utf8(PcText::KeysCancelled));
    } else if (state == PsiCapture::Clear) {
        if (nf::clear_binds(cfg.bindings, command, false)) {
            set_status(index, fmt(PcText::KeysCleared, name));
            nf::persist_controls();
        } else {
            set_status(index, fmt(PcText::KeysRequired, name));
        }
    } else if (psiInput_CaptureResult(&code)) {
        const std::string key = nf::input_label(code, english());
        const nf::BindResult r = nf::bind(cfg.bindings, command, code);
        if (r.refused) {
            set_status(index, fmt(PcText::KeysRefused, key, r.moved_from >= 0 ? command_label(r.moved_from) : name));
        } else {
            if (r.swapped_to >= 0) {
                set_status(index, fmt(PcText::KeysSwapped, key, name, command_label(r.swapped_to),
                                      nf::input_label(r.swapped, english())));
            } else if (r.moved_from >= 0) {
                set_status(index, fmt(PcText::KeysMoved, key, name, command_label(r.moved_from)));
            } else {
                set_status(index, fmt(PcText::KeysBound, key, name));
            }
            nf::persist_controls();
        }
    }
    psiInput_EndCapture();
    g_capture_command = -1;
    fill_keys_list(keys_list(index));
}

void keys_enter(uint8_t index) {
    fill_keys_list(keys_list(index));
    set_status(index, g_status_utf8);
}

void keys_tick(uint8_t index) { finish_capture(index); }

void keys_leave(uint8_t) {
    if (g_capture_command >= 0) psiInput_EndCapture();
    g_capture_command = -1;
}

void build_keys(uint8_t, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Keys, kPcKeysList, PcRowKind::List);
    MenuPc_AddRow(m, PcTab::Keys, kPcKeysStatus, PcRowKind::Status);
}

int32_t C_PCKEYS_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x4b || c == nullptr || c->type != MCT_LIST || g_capture_command >= 0) return 0;
    const int32_t data = __Menu_SendMessage(c, 0x35, 0, 0);
    if (data == kPcKeysRestoreRow) {
        nf::controls().bindings.reset();
        nf::persist_controls();
        fill_keys_list(c);
        set_status(index, utf8(PcText::KeysRestored));
        return 1;
    }
    if (data < 0 || data >= nf::kBindCommandCount) return 0;
    g_capture_command = data;
    psiInput_BeginCapture();
    set_status(index, fmt(PcText::KeysPress, command_label(data)));
    return 1;
}

}  // namespace

void MenuPcControls_Register() {
    MenuPcTab controls;
    controls.build = build_controls;
    controls.enter = controls_enter;
    controls.tick = controls_tick;
    controls.help = PcText::CtlHelp;
    MenuPc_RegisterTab(PcTab::Controls, controls);
    MenuPcTab keys;
    keys.build = build_keys;
    keys.enter = keys_enter;
    keys.tick = keys_tick;
    keys.leave = keys_leave;
    keys.help = PcText::KeysHelp;
    MenuPc_RegisterTab(PcTab::Keys, keys);
    for (uint32_t id = kPcRowMouseSens; id <= kPcRowAimStyle; ++id) {
        Menu_PortRegisterHandler(id, "C_PCCONTROLS_Handler", C_PCCONTROLS_Handler);
    }
    Menu_PortRegisterHandler(kPcKeysList, "C_PCKEYS_Handler", C_PCKEYS_Handler);
    g_capture_command = -1;
    g_status_utf8.clear();
    g_device_utf8.clear();
}

const char* MenuPcControls_Status() { return g_status_utf8.c_str(); }
