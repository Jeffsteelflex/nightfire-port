// Teclas e botões dos comandos do jogador. Ver input_bindings.h.

#include "platform/input_bindings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/game_options.h"
#include "platform/gamepad.h"
#include "platform/psi_input.h"
#include "platform/settings.h"

namespace nf {

// As ações de engine/input.h (números, para a plataforma não depender dos nomes da engine); a partir de
// kPortActionBase, as só do port (PortAction).
constexpr int8_t kPortReloadAction = int8_t(kPortActionBase + kPortReload);
constexpr int8_t kPortGadgetAction = int8_t(kPortActionBase + kPortGadget1);
const BindCommand kBindCommands[kBindCommandCount] = {
    {"forward", BindGroup::Game, false, {{2, 1}, {-1, 0}}, "Andar para a frente", "Move forward", "w"},
    {"back", BindGroup::Game, false, {{2, -1}, {-1, 0}}, "Andar para trás", "Move back", "s"},
    {"strafe_left", BindGroup::Game, false, {{1, 1}, {-1, 0}}, "Passo à esquerda", "Strafe left", "a"},
    {"strafe_right", BindGroup::Game, false, {{1, -1}, {-1, 0}}, "Passo à direita", "Strafe right", "d"},
    {"turn_left", BindGroup::Game, false, {{0, -1}, {3, -1}}, "Virar à esquerda", "Turn left", "left"},
    {"turn_right", BindGroup::Game, false, {{0, 1}, {3, 1}}, "Virar à direita", "Turn right", "right"},
    {"look_up", BindGroup::Game, false, {{5, 1}, {4, 1}}, "Olhar para cima", "Look up", "up"},
    {"look_down", BindGroup::Game, false, {{5, -1}, {4, -1}}, "Olhar para baixo", "Look down", "down"},
    {"jump", BindGroup::Game, false, {{7, 1}, {-1, 0}}, "Pular", "Jump", "space"},
    {"crouch", BindGroup::Game, false, {{8, 1}, {-1, 0}}, "Agachar", "Crouch", "c, ctrl"},
    {"fire", BindGroup::Game, false, {{9, 1}, {-1, 0}}, "Atirar", "Fire", "mouse1"},
    {"aim", BindGroup::Game, false, {{19, 1}, {-1, 0}}, "Mirar", "Aim", "mouse2"},
    // Usar continua "usar ou recarregar", como o botão do PS2; recarregar só recarrega (só do port).
    {"use", BindGroup::Game, false, {{14, 1}, {-1, 0}}, "Usar / recarregar", "Use / reload", "e"},
    {"reload", BindGroup::Game, false, {{kPortReloadAction, 1}, {-1, 0}}, "Recarregar", "Reload", "r"},
    {"fire_mode", BindGroup::Game, false, {{12, 1}, {-1, 0}}, "Modo de tiro / acessório", "Fire mode / attachment",
     "mouse3, x"},
    {"vision", BindGroup::Game, false, {{13, 1}, {-1, 0}}, "Visão noturna / raio X", "Vision mode", "n"},
    // A roda: para baixo a próxima arma, para cima a anterior (mirando com zoom, a roda dá zoom: psiInput_SetWheelZoom).
    {"weapon_next", BindGroup::Game, false, {{16, 1}, {-1, 0}}, "Próxima arma", "Next weapon", "wheel_down, z"},
    {"weapon_prev", BindGroup::Game, false, {{15, 1}, {-1, 0}}, "Arma anterior", "Previous weapon", "wheel_up, q"},
    // O ciclo dos aparelhos do jogo (e o zoom do direcional, mirando): G o próximo; o anterior sem tecla (1 a 5 escolhem).
    {"gadget_next", BindGroup::Game, false, {{11, 1}, {6, -1}}, "Próximo aparelho / zoom +", "Next gadget / zoom in", "g"},
    {"gadget_prev", BindGroup::Game, false, {{10, 1}, {6, 1}}, "Aparelho anterior / zoom -", "Previous gadget / zoom out",
     ""},
    {"gadget_1", BindGroup::Game, false, {{kPortGadgetAction, 1}, {-1, 0}}, "Aparelho 1", "Gadget 1", "1"},
    {"gadget_2", BindGroup::Game, false, {{int8_t(kPortGadgetAction + 1), 1}, {-1, 0}}, "Aparelho 2", "Gadget 2", "2"},
    {"gadget_3", BindGroup::Game, false, {{int8_t(kPortGadgetAction + 2), 1}, {-1, 0}}, "Aparelho 3", "Gadget 3", "3"},
    {"gadget_4", BindGroup::Game, false, {{int8_t(kPortGadgetAction + 3), 1}, {-1, 0}}, "Aparelho 4", "Gadget 4", "4"},
    {"gadget_5", BindGroup::Game, false, {{int8_t(kPortGadgetAction + 4), 1}, {-1, 0}}, "Aparelho 5", "Gadget 5", "5"},
    // Esc: a pausa no jogo e o voltar nos menus (pause_menu_shared).
    {"pause", BindGroup::Game, true, {{30, 1}, {25, 1}}, "Pausa (Start)", "Pause (Start)", "enter, escape"},
    {"menu_up", BindGroup::Menu, true, {{26, 1}, {-1, 0}}, "Menu: cima", "Menu: up", "up, w"},
    {"menu_down", BindGroup::Menu, true, {{27, 1}, {-1, 0}}, "Menu: baixo", "Menu: down", "down, s"},
    {"menu_left", BindGroup::Menu, false, {{28, 1}, {-1, 0}}, "Menu: esquerda", "Menu: left", "left, a"},
    {"menu_right", BindGroup::Menu, false, {{29, 1}, {-1, 0}}, "Menu: direita", "Menu: right", "right, d"},
    {"menu_select", BindGroup::Menu, true, {{31, 1}, {25, 1}}, "Menu: escolher (X)", "Menu: select (X)", "space"},
    {"menu_circle", BindGroup::Menu, false, {{33, 1}, {-1, 0}}, "Menu: círculo", "Menu: circle", "tab"},
    {"menu_back", BindGroup::Menu, true, {{34, 1}, {-1, 0}}, "Menu: voltar (triângulo)", "Menu: back (triangle)",
     "escape, backspace"},
    // O quadrado (o dossiê no mapa das missões e no resultado, "Create" nos codinomes): E, a tecla da aba DOSSIER do mapa
    // Modern (menu_tab_next), para o dossiê abrir com a mesma tecla nos dois estilos. Os dois comandos nunca valem ao
    // mesmo tempo (os menus Modern não leem as ações do jogo, as páginas do jogo não leem menu_tab_next), e o quadrado
    // (ação 32) só é lido pelos controles dos menus: no jogo o E continua só "usar".
    {"menu_square", BindGroup::Menu, false, {{32, 1}, {-1, 0}}, "Menu: quadrado", "Menu: square", "e"},
    // Só do estilo de menu Modern (src/ui): sem ação do jogo, lidos pelos menus do port (command_value).
    {"menu_tab_prev", BindGroup::Menu, false, {{-1, 0}, {-1, 0}}, "Menu: aba anterior", "Menu: previous tab", "q, pad_lb"},
    {"menu_tab_next", BindGroup::Menu, false, {{-1, 0}, {-1, 0}}, "Menu: próxima aba", "Menu: next tab", "e, pad_rb"},
    {"menu_defaults", BindGroup::Menu, false, {{-1, 0}, {-1, 0}}, "Menu: restaurar padrões", "Menu: restore defaults",
     "r, pad_x"},
    // Só do port: a roda de armas (engine/weapon_wheel.h), segurada: Tab, e no controle L3 (pad_ls), o único botão que
    // nenhum dos oito estilos do jogo usa (o R3 prenderia o analógico direito, que aponta o espaço da roda).
    {"weapon_wheel", BindGroup::Game, false, {{int8_t(kPortActionBase + kPortWeaponWheel), 1}, {-1, 0}}, "Roda de armas",
     "Weapon wheel", "tab, pad_ls"},
};

const char* const kPadStyleNames[8] = {"NightFire", "Moonraker", "Octopussy", "Goldfinger",
                                       "Dr. No",    "Thunderball", "GoldenEye", "Classic Bond"};

bool g_controls_persist = false;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

struct KeyName {
    uint8_t vk;
    const char* name;
    const char* pt;
    const char* en;
};

// Teclas com nome (letras, dígitos, F1-F24 e o teclado numérico vêm de regras).
const KeyName kKeys[] = {
    {VK_LBUTTON, "mouse1", "Mouse 1", "Mouse 1"},
    {VK_RBUTTON, "mouse2", "Mouse 2", "Mouse 2"},
    {VK_MBUTTON, "mouse3", "Mouse 3", "Mouse 3"},
    {VK_XBUTTON1, "mouse4", "Mouse 4", "Mouse 4"},
    {VK_XBUTTON2, "mouse5", "Mouse 5", "Mouse 5"},
    {VK_SPACE, "space", "Espaço", "Space"},
    {VK_RETURN, "enter", "Enter", "Enter"},
    {VK_TAB, "tab", "Tab", "Tab"},
    {VK_BACK, "backspace", "Backspace", "Backspace"},
    {VK_ESCAPE, "escape", "Esc", "Esc"},
    {VK_SHIFT, "shift", "Shift", "Shift"},
    {VK_LSHIFT, "lshift", "Shift esq.", "Left Shift"},
    {VK_RSHIFT, "rshift", "Shift dir.", "Right Shift"},
    {VK_CONTROL, "ctrl", "Ctrl", "Ctrl"},
    {VK_LCONTROL, "lctrl", "Ctrl esq.", "Left Ctrl"},
    {VK_RCONTROL, "rctrl", "Ctrl dir.", "Right Ctrl"},
    {VK_MENU, "alt", "Alt", "Alt"},
    {VK_LMENU, "lalt", "Alt esq.", "Left Alt"},
    {VK_RMENU, "ralt", "Alt dir.", "Right Alt"},
    {VK_UP, "up", "Seta cima", "Up"},
    {VK_DOWN, "down", "Seta baixo", "Down"},
    {VK_LEFT, "left", "Seta esq.", "Left"},
    {VK_RIGHT, "right", "Seta dir.", "Right"},
    {VK_INSERT, "insert", "Insert", "Insert"},
    {VK_DELETE, "delete", "Delete", "Delete"},
    {VK_HOME, "home", "Home", "Home"},
    {VK_END, "end", "End", "End"},
    {VK_PRIOR, "pageup", "Page Up", "Page Up"},
    {VK_NEXT, "pagedown", "Page Down", "Page Down"},
    {VK_CAPITAL, "capslock", "Caps Lock", "Caps Lock"},
    {VK_MULTIPLY, "num*", "Num *", "Num *"},
    {VK_ADD, "num+", "Num +", "Num +"},
    {VK_SUBTRACT, "num-", "Num -", "Num -"},
    {VK_DECIMAL, "num.", "Num .", "Num ."},
    {VK_DIVIDE, "num/", "Num /", "Num /"},
    {VK_OEM_1, "semicolon", ";", ";"},
    {VK_OEM_PLUS, "equals", "=", "="},
    {VK_OEM_COMMA, "comma", ",", ","},
    {VK_OEM_MINUS, "minus", "-", "-"},
    {VK_OEM_PERIOD, "period", ".", "."},
    {VK_OEM_2, "slash", "/", "/"},
    {VK_OEM_3, "grave", "`", "`"},
    {VK_OEM_4, "lbracket", "[", "["},
    {VK_OEM_5, "backslash", "\\", "\\"},
    {VK_OEM_6, "rbracket", "]", "]"},
    {VK_OEM_7, "apostrophe", "'", "'"},
    {VK_OEM_102, "oem102", "\\", "\\"},
    {0xc1, "abnt_c1", "/", "/"},  // VK_ABNT_C1: a tecla / do teclado ABNT2
};

const char* const kPadNames[kPadInCount] = {
    "pad_up", "pad_down", "pad_left", "pad_right", "pad_start", "pad_back", "pad_ls", "pad_rs", "pad_lb",
    "pad_rb", nullptr,    nullptr,    "pad_a",     "pad_b",     "pad_x",    "pad_y",  "pad_lt", "pad_rt",
    "pad_lx+", "pad_lx-", "pad_ly+",  "pad_ly-",   "pad_rx+",   "pad_rx-",  "pad_ry+", "pad_ry-",
};
const char* const kPadLabels[kPadInCount] = {
    "D-pad ^", "D-pad v", "D-pad <", "D-pad >", "Start", "Back", "LS", "RS",  "LB",  "RB",  nullptr, nullptr, "A",
    "B",       "X",       "Y",       "LT",      "RT",    "LX+",  "LX-", "LY+", "LY-", "RX+", "RX-",   "RY+",   "RY-",
};

bool is_key_class(InputCode c) { return c.kind == InputKind::Key || c.kind == InputKind::Wheel; }
bool same_class(InputCode a, InputCode b) { return a.is_pad() == b.is_pad(); }

// Metade de um analógico (-32768..32767) com a zona morta, de 0 a 1.
float stick_half(int16_t v, bool positive, float deadzone) {
    float f = float(v) / 32767.0f;
    if (!positive) f = -f;
    if (f <= deadzone) return 0.0f;
    const float r = (f - deadzone) / std::max(1e-4f, 1.0f - deadzone);
    return r < 1.0f ? r : 1.0f;
}

void remove_at(InputCode* list, int i) {
    for (int k = i; k + 1 < kMaxBinds; ++k) list[k] = list[k + 1];
    list[kMaxBinds - 1] = InputCode{};
}

int find_in(const InputCode* list, InputCode c) {
    for (int i = 0; i < kMaxBinds; ++i) {
        if (list[i] == c) return i;
    }
    return -1;
}

int count_class(const InputCode* list, bool pad) {
    int n = 0;
    for (int i = 0; i < kMaxBinds; ++i) n += list[i].valid() && list[i].is_pad() == pad;
    return n;
}

// Põe a entrada na lista do comando: teclado/mouse antes do controle, a nova primeiro na classe dela.
// Devolve a que saiu para caber (ou inválida).
InputCode insert_first(InputCode* list, InputCode c) {
    InputCode dropped;
    const bool pad = c.is_pad();
    if (pad) {
        // Uma só do controle: a anterior sai.
        for (int i = 0; i < kMaxBinds; ++i) {
            if (list[i].valid() && list[i].is_pad()) {
                dropped = list[i];
                remove_at(list, i);
                break;
            }
        }
    } else if (count_class(list, false) >= kMaxKeyBinds) {
        for (int i = kMaxBinds - 1; i >= 0; --i) {
            if (list[i].valid() && !list[i].is_pad()) {
                dropped = list[i];
                remove_at(list, i);
                break;
            }
        }
    }
    InputCode tmp[kMaxBinds];
    int n = 0;
    if (!pad) tmp[n++] = c;
    for (int i = 0; i < kMaxBinds && n < kMaxBinds; ++i) {
        if (list[i].valid() && !list[i].is_pad()) tmp[n++] = list[i];
    }
    if (pad && n < kMaxBinds) tmp[n++] = c;
    for (int i = 0; i < kMaxBinds && n < kMaxBinds; ++i) {
        if (list[i].valid() && list[i].is_pad()) tmp[n++] = list[i];
    }
    for (int i = 0; i < kMaxBinds; ++i) list[i] = i < n ? tmp[i] : InputCode{};
    return dropped;
}

}  // namespace

InputCode key_input(uint8_t vk) { return InputCode{InputKind::Key, vk}; }
InputCode wheel_input(bool down) { return InputCode{InputKind::Wheel, uint8_t(down ? 1 : 0)}; }
InputCode pad_input(uint8_t code) { return InputCode{InputKind::Pad, code}; }

std::string input_name(InputCode c) {
    char buf[16];
    switch (c.kind) {
        case InputKind::Key:
            for (const KeyName& k : kKeys) {
                if (k.vk == c.code) return k.name;
            }
            if ((c.code >= 'A' && c.code <= 'Z') || (c.code >= '0' && c.code <= '9')) {
                return std::string(1, char(std::tolower(c.code)));
            }
            if (c.code >= VK_F1 && c.code <= VK_F24) {
                std::snprintf(buf, sizeof(buf), "f%d", c.code - VK_F1 + 1);
                return buf;
            }
            if (c.code >= VK_NUMPAD0 && c.code <= VK_NUMPAD9) {
                std::snprintf(buf, sizeof(buf), "num%d", c.code - VK_NUMPAD0);
                return buf;
            }
            std::snprintf(buf, sizeof(buf), "vk%02x", c.code);
            return buf;
        case InputKind::Wheel:
            return c.code == 0 ? "wheel_up" : "wheel_down";
        case InputKind::Pad:
            if (c.code < kPadInCount && kPadNames[c.code] != nullptr) return kPadNames[c.code];
            return "";
        default:
            return "";
    }
}

bool parse_input_name(const std::string& name_in, InputCode* out) {
    const std::string name = lower(trim(name_in));
    if (name.empty()) return false;
    for (const KeyName& k : kKeys) {
        if (name == k.name) {
            *out = key_input(k.vk);
            return true;
        }
    }
    if (name.size() == 1 && (std::isalpha(uint8_t(name[0])) || std::isdigit(uint8_t(name[0])))) {
        *out = key_input(uint8_t(std::toupper(uint8_t(name[0]))));
        return true;
    }
    if (name == "wheel_up" || name == "wheel_down") {
        *out = wheel_input(name == "wheel_down");
        return true;
    }
    for (uint8_t i = 0; i < kPadInCount; ++i) {
        if (kPadNames[i] != nullptr && name == kPadNames[i]) {
            *out = pad_input(i);
            return true;
        }
    }
    int n = 0;
    if (name.size() >= 2 && name[0] == 'f' && (n = std::atoi(name.c_str() + 1)) != 0 && n >= 1 && n <= 24 &&
        name == "f" + std::to_string(n)) {
        *out = key_input(uint8_t(VK_F1 + n - 1));
        return true;
    }
    if (name.size() == 4 && name.compare(0, 3, "num") == 0 && std::isdigit(uint8_t(name[3]))) {
        *out = key_input(uint8_t(VK_NUMPAD0 + (name[3] - '0')));
        return true;
    }
    if (name.size() == 4 && name.compare(0, 2, "vk") == 0 && std::isxdigit(uint8_t(name[2])) &&
        std::isxdigit(uint8_t(name[3]))) {
        const unsigned v = unsigned(std::strtoul(name.c_str() + 2, nullptr, 16));
        if (v > 0 && v < 0xff) {
            *out = key_input(uint8_t(v));
            return true;
        }
    }
    return false;
}

std::string input_label(InputCode c, bool english) {
    switch (c.kind) {
        case InputKind::Key: {
            for (const KeyName& k : kKeys) {
                if (k.vk != c.code) continue;
                // Teclas de pontuação: o caractere do layout do usuário (ABNT2: VK_OEM_1 é o Ç).
                if (c.code >= VK_OEM_1 && c.code != 0xc1) {
                    const UINT ch = MapVirtualKeyW(c.code, MAPVK_VK_TO_CHAR) & 0xffff;
                    if (ch >= 0x21 && ch < 0x100) {
                        char buf[4] = {};
                        if (ch < 0x80) {
                            buf[0] = char(ch);
                        } else {
                            buf[0] = char(0xc0 | (ch >> 6));
                            buf[1] = char(0x80 | (ch & 0x3f));
                        }
                        return buf;
                    }
                }
                return english ? k.en : k.pt;
            }
            if ((c.code >= 'A' && c.code <= 'Z') || (c.code >= '0' && c.code <= '9')) return std::string(1, char(c.code));
            char buf[24];
            if (c.code >= VK_F1 && c.code <= VK_F24) {
                std::snprintf(buf, sizeof(buf), "F%d", c.code - VK_F1 + 1);
            } else if (c.code >= VK_NUMPAD0 && c.code <= VK_NUMPAD9) {
                std::snprintf(buf, sizeof(buf), "Num %d", c.code - VK_NUMPAD0);
            } else {
                std::snprintf(buf, sizeof(buf), english ? "Key %02X" : "Tecla %02X", c.code);
            }
            return buf;
        }
        case InputKind::Wheel:
            return c.code == 0 ? (english ? "Wheel up" : "Roda p/ cima") : (english ? "Wheel down" : "Roda p/ baixo");
        case InputKind::Pad:
            if (c.code < kPadInCount && kPadLabels[c.code] != nullptr) return kPadLabels[c.code];
            return "?";
        default:
            return "";
    }
}

int find_bind_command(const std::string& key) {
    const std::string k = lower(trim(key));
    for (int i = 0; i < kBindCommandCount; ++i) {
        if (k == kBindCommands[i].key) return i;
    }
    return -1;
}

int pause_command() {
    static const int pause = find_bind_command("pause");
    return pause;
}

int pad_style_action(int command) {
    if (command < 0 || command >= kBindCommandCount) return -1;
    const int action = kBindCommands[command].targets[0].action;
    if (action == kPortReloadAction) return 14;  // no PS2 o botão de usar recarrega
    return action >= 0 && action < kPortActionBase ? action : -1;
}

void Bindings::reset() { *this = default_bindings(); }

bool Bindings::has(int command, InputCode c) const { return find_in(in[command], c) >= 0; }

int Bindings::list(int command, bool pad, InputCode* out, int max) const {
    int n = 0;
    for (int i = 0; i < kMaxBinds && n < max; ++i) {
        if (in[command][i].valid() && in[command][i].is_pad() == pad) out[n++] = in[command][i];
    }
    return n;
}

Bindings default_bindings() {
    Bindings b{};
    for (int i = 0; i < kBindCommandCount; ++i) parse_inputs(kBindCommands[i].defaults, b.in[i], kMaxBinds);
    return b;
}

BindResult bind(Bindings& b, int command, InputCode c) {
    BindResult r;
    if (command < 0 || command >= kBindCommandCount || !c.valid()) {
        r.refused = true;
        return r;
    }
    Bindings nb = b;  // só vale se der certo
    InputCode* list = nb.in[command];
    // Já está no comando: só vira a primeira da classe.
    if (const int at = find_in(list, c); at >= 0) {
        remove_at(list, at);
        insert_first(list, c);
        b = nb;
        return r;
    }
    // A primeira da classe antes da troca (a que um comando obrigatório receberia).
    InputCode previous;
    for (int i = 0; i < kMaxBinds; ++i) {
        if (list[i].valid() && same_class(list[i], c)) {
            previous = list[i];
            break;
        }
    }
    const BindGroup group = kBindCommands[command].group;
    for (int other = 0; other < kBindCommandCount; ++other) {
        if (other == command || kBindCommands[other].group != group) continue;
        const int at = find_in(nb.in[other], c);
        if (at < 0) continue;
        const bool left_empty = count_class(nb.in[other], c.is_pad()) == 1;
        if (kBindCommands[other].required && left_empty && !c.is_pad()) {
            const int pi = previous.valid() ? find_in(list, previous) : -1;
            if (pi < 0) {
                r = BindResult{};
                r.refused = true;
                r.moved_from = other;
                return r;
            }
            // Troca: o obrigatório fica com a tecla que este tinha.
            remove_at(list, pi);
            nb.in[other][at] = previous;
            r.swapped_to = other;
            r.swapped = previous;
        } else {
            remove_at(nb.in[other], at);
        }
        r.moved_from = other;
    }
    insert_first(list, c);
    b = nb;
    return r;
}

bool clear_binds(Bindings& b, int command, bool pad) {
    if (command < 0 || command >= kBindCommandCount) return false;
    if (!pad && kBindCommands[command].required) return false;
    InputCode* list = b.in[command];
    for (int i = kMaxBinds - 1; i >= 0; --i) {
        if (list[i].valid() && list[i].is_pad() == pad) remove_at(list, i);
    }
    return true;
}

bool command_has_action(int command) {
    if (command < 0 || command >= kBindCommandCount) return false;
    for (const BindTarget& t : kBindCommands[command].targets) {
        if (t.action >= 0) return true;
    }
    return false;
}

int pause_menu_shared(const Bindings& b, InputCode* out) {
    const int pause = pause_command();
    int n = 0;
    for (int k = 0; k < kMaxBinds; ++k) {
        const InputCode c = b.in[pause][k];
        if (!c.valid()) continue;
        for (int i = 0; i < kBindCommandCount; ++i) {
            if (kBindCommands[i].group == BindGroup::Menu && b.has(i, c)) {
                out[n++] = c;
                break;
            }
        }
    }
    return n;
}

float command_value(const Bindings& b, int command, const InputFrame& f, int which) {
    if (command < 0 || command >= kBindCommandCount) return 0.0f;
    float v = 0.0f;
    for (int k = 0; k < kMaxBinds; ++k) {
        const InputCode c = b.in[command][k];
        if (!c.valid() || (which == 0 && c.is_pad()) || (which == 1 && !c.is_pad())) continue;
        v = std::max(v, input_value(c, f));
    }
    return v;
}

BindResult bind_slot(Bindings& b, int command, int slot, InputCode c) {
    if (slot <= 0 || c.is_pad() || command < 0 || command >= kBindCommandCount) return bind(b, command, c);
    // A segunda tecla: a primeira de antes continua primeira (se ela ainda estiver lá depois dos conflitos).
    InputCode first[kMaxKeyBinds];
    const int n = b.list(command, false, first, kMaxKeyBinds);
    const BindResult r = bind(b, command, c);
    if (r.refused || n == 0 || first[0] == c) return r;
    InputCode* list = b.in[command];
    const int at = find_in(list, first[0]);
    if (at < 0) return r;
    remove_at(list, at);
    // c já é a primeira da classe: a anterior volta para a frente dela.
    InputCode keep[kMaxBinds];
    int k = 0;
    keep[k++] = first[0];
    for (int i = 0; i < kMaxBinds && k < kMaxBinds; ++i) {
        if (list[i].valid()) keep[k++] = list[i];
    }
    for (int i = 0; i < kMaxBinds; ++i) list[i] = i < k ? keep[i] : InputCode{};
    return r;
}

bool clear_bind_slot(Bindings& b, int command, int slot) {
    if (command < 0 || command >= kBindCommandCount) return false;
    if (slot >= kMaxKeyBinds) return clear_binds(b, command, true);
    InputCode keys[kMaxKeyBinds];
    const int n = b.list(command, false, keys, kMaxKeyBinds);
    if (slot >= n) return true;
    if (n == 1 && kBindCommands[command].required) return false;
    remove_at(b.in[command], find_in(b.in[command], keys[slot]));
    return true;
}

float input_value(InputCode c, const InputFrame& f) {
    switch (c.kind) {
        case InputKind::Key:
            return f.keys != nullptr && f.keys[c.code] ? 1.0f : 0.0f;
        case InputKind::Wheel:
            return (c.code == 0 ? f.wheel_up : f.wheel_down) ? 1.0f : 0.0f;
        case InputKind::Pad: {
            if (f.pad == nullptr || !f.pad->connected) return 0.0f;
            const PadState& p = *f.pad;
            if (c.code < 16) return (p.buttons & (1u << c.code)) != 0 ? 1.0f : 0.0f;
            switch (c.code) {
                case kPadInLT:
                    return p.lt > kPadTriggerThreshold ? float(p.lt) / 255.0f : 0.0f;
                case kPadInRT:
                    return p.rt > kPadTriggerThreshold ? float(p.rt) / 255.0f : 0.0f;
                case kPadInLXPos: return stick_half(p.lx, true, f.pad_deadzone);
                case kPadInLXNeg: return stick_half(p.lx, false, f.pad_deadzone);
                case kPadInLYPos: return stick_half(p.ly, true, f.pad_deadzone);
                case kPadInLYNeg: return stick_half(p.ly, false, f.pad_deadzone);
                case kPadInRXPos: return stick_half(p.rx, true, f.pad_deadzone);
                case kPadInRXNeg: return stick_half(p.rx, false, f.pad_deadzone);
                case kPadInRYPos: return stick_half(p.ry, true, f.pad_deadzone);
                case kPadInRYNeg: return stick_half(p.ry, false, f.pad_deadzone);
                default: return 0.0f;
            }
        }
        default:
            return 0.0f;
    }
}

void bindings_to_actions(const Bindings& b, const InputFrame& f, bool pad, float actions[40], float* port_actions) {
    for (int i = 0; i < kBindCommandCount; ++i) {
        float v = 0.0f;
        for (int k = 0; k < kMaxBinds; ++k) {
            const InputCode c = b.in[i][k];
            if (!c.valid() || c.is_pad() != pad) continue;
            v = std::max(v, input_value(c, f));
        }
        if (v == 0.0f) continue;
        for (const BindTarget& t : kBindCommands[i].targets) {
            if (t.action < 0) continue;
            float* a = nullptr;
            if (t.action < kPortActionBase) {
                a = &actions[t.action];
            } else if (port_actions != nullptr && is_port_action(t.action)) {
                a = &port_actions[t.action - kPortActionBase];
            }
            if (a != nullptr) *a = std::clamp(*a + v * float(t.sign), -1.0f, 1.0f);
        }
    }
    // Como o analógico na diagonal (o mapeamento do teclado que o port usava).
    if (!pad && actions[1] != 0.0f && actions[2] != 0.0f) {
        actions[1] *= 0.70710677f;
        actions[2] *= 0.70710677f;
    }
}

uint32_t bindings_pad_mask(const Bindings& b) {
    uint32_t mask = 0;
    for (int i = 0; i < kBindCommandCount; ++i) {
        // Os comandos sem ação do jogo (os do menu Modern) não tiram o botão do controle do PS2.
        if (!command_has_action(i)) continue;
        for (int k = 0; k < kMaxBinds; ++k) {
            if (b.in[i][k].is_pad()) mask |= 1u << b.in[i][k].code;
        }
    }
    return mask;
}

std::string format_inputs(const InputCode* list, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (!list[i].valid()) continue;
        if (!s.empty()) s += ", ";
        s += input_name(list[i]);
    }
    return s;
}

int parse_inputs(const std::string& text, InputCode* out, int max) {
    InputCode tmp[kMaxBinds];
    int n = 0;
    size_t pos = 0;
    while (pos <= text.size() && n < kMaxBinds) {
        size_t end = text.find(',', pos);
        if (end == std::string::npos) end = text.size();
        InputCode c;
        if (parse_input_name(text.substr(pos, end - pos), &c) && find_in(tmp, c) < 0) {
            // Teclado/mouse até kMaxKeyBinds; controle, um.
            if (count_class(tmp, c.is_pad()) < (c.is_pad() ? 1 : kMaxKeyBinds)) tmp[n++] = c;
        }
        pos = end + 1;
    }
    // Teclado/mouse antes do controle.
    std::stable_partition(tmp, tmp + n, [](InputCode c) { return is_key_class(c); });
    n = std::min(n, max);
    for (int i = 0; i < max; ++i) out[i] = i < n ? tmp[i] : InputCode{};
    return n;
}

void load_bindings(const Settings& s, Bindings* b) {
    *b = default_bindings();
    bool written[kBindCommandCount] = {};
    for (int i = 0; i < kBindCommandCount; ++i) {
        written[i] = s.has("bindings", kBindCommands[i].key);
        if (written[i]) parse_inputs(s.get("bindings", kBindCommands[i].key), b->in[i], kMaxBinds);
    }
    // Os padrões mudam entre versões (R era o modo de tiro e passou a recarregar; Tab era a visão): a entrada que um
    // comando escrito usa sai do padrão de outro comando do mesmo grupo, para nada fazer duas coisas.
    for (int i = 0; i < kBindCommandCount; ++i) {
        if (written[i]) continue;
        InputCode kept[kMaxBinds];
        std::memcpy(kept, b->in[i], sizeof(kept));
        for (int k = kMaxBinds - 1; k >= 0; --k) {
            const InputCode c = b->in[i][k];
            if (!c.valid()) continue;
            for (int j = 0; j < kBindCommandCount; ++j) {
                if (j == i || !written[j] || kBindCommands[j].group != kBindCommands[i].group || !b->has(j, c)) continue;
                remove_at(b->in[i], k);
                break;
            }
        }
        // Um obrigatório sem teclado fica com os padrões (o conflito, como antes desta regra).
        if (kBindCommands[i].required && count_class(b->in[i], false) == 0) std::memcpy(b->in[i], kept, sizeof(kept));
    }
}

void save_bindings(Settings& s, const Bindings& b) {
    // Um comando vai para o arquivo se ele já o tem ou se as teclas dele não são as padrão: religar um comando não
    // escreve os outros 30 (os padrões continuam vindo do port, também se um dia mudarem).
    const Bindings d = default_bindings();
    for (int i = 0; i < kBindCommandCount; ++i) {
        bool same = true;
        for (int k = 0; k < kMaxBinds; ++k) same = same && b.in[i][k] == d.in[i][k];
        if (same && !s.has("bindings", kBindCommands[i].key)) continue;
        s.set("bindings", kBindCommands[i].key, format_inputs(b.in[i], kMaxBinds));
    }
}

ControlsConfig& controls() {
    static ControlsConfig c;
    return c;
}

float mouse_sens_from_pos(int pos) {
    pos = std::clamp(pos, kMouseSensPosMin, kMouseSensPosMax);
    return kDefaultMouseSensitivity * std::exp2(float(pos - kMouseSensDefaultPos) / float(kMouseSensStepsPerOctave));
}

int mouse_sens_to_pos(float sens) {
    if (!(sens > 0.0f)) return kMouseSensPosMin;
    const float p = float(kMouseSensDefaultPos) + float(kMouseSensStepsPerOctave) * std::log2(sens / kDefaultMouseSensitivity);
    return std::clamp(int(std::lround(p)), kMouseSensPosMin, kMouseSensPosMax);
}

float mouse_cm_per_360(float sens, float dpi) {
    if (!(sens > 0.0f) || !(dpi > 0.0f)) return 0.0f;
    const float counts = 6.2831853f / sens;  // contagens por volta
    return counts / dpi * 2.54f;
}

void load_controls(const Settings& s, ControlsConfig* c) {
    load_bindings(s, &c->bindings);
    c->mouse_aim_scale = std::clamp(s.get_float("controls", "mouse_aim_sensitivity", c->mouse_aim_scale), 0.05f, 4.0f);
    c->mouse_invert_y = s.get_bool("controls", "mouse_invert_y", c->mouse_invert_y);
    c->pad_sensitivity = std::clamp(s.get_float("controls", "pad_sensitivity", c->pad_sensitivity), 0.1f, 4.0f);
    c->pad_deadzone = std::clamp(s.get_float("controls", "pad_deadzone", c->pad_deadzone), 0.0f, 0.9f);
    c->pad_invert_y = s.get_bool("controls", "pad_invert_y", c->pad_invert_y);
    c->vibration = s.get_bool("controls", "vibration", c->vibration);
    const std::string shoulders = lower(s.get("controls", "pad_shoulders", c->pad_triggers ? "triggers" : "bumpers"));
    c->pad_triggers = shoulders != "bumpers";
    // O estilo pelo número (0-7) ou pelo nome do menu.
    const std::string style = lower(trim(s.get("controls", "pad_style", std::to_string(c->pad_style))));
    for (uint16_t i = 0; i < 8; ++i) {
        if (style == lower(kPadStyleNames[i]) || style == std::to_string(i)) c->pad_style = i;
    }
}

void save_controls(Settings& s, const ControlsConfig& c, float mouse_sensitivity) {
    // Cada chave vai para o arquivo se ele já a tem ou se o valor não é o padrão: mudar uma opção não escreve as
    // outras com o padrão em vigor. As três do jogo (vibration, pad_style, pad_invert_y) não são daqui: o
    // save_game_options grava as que o .ini conhece (platform/game_options.h; um pad_style = 0 escrito sempre fazia
    // os perfis padrão perderem o estilo 7 de def_codename).
    const ControlsConfig d{};
    const auto differs = [](float a, float b) { return std::fabs(a - b) > 1e-6f + 1e-4f * std::fabs(b); };
    const auto put_float = [&](const char* key, float v, float def) {
        if (differs(v, def) || s.has("controls", key)) s.set_float("controls", key, v);
    };
    const auto put_bool = [&](const char* key, bool v, bool def) {
        if (v != def || s.has("controls", key)) s.set_bool("controls", key, v);
    };
    put_float("mouse_sensitivity", mouse_sensitivity, kDefaultMouseSensitivity);
    put_float("mouse_aim_sensitivity", c.mouse_aim_scale, d.mouse_aim_scale);
    put_bool("mouse_invert_y", c.mouse_invert_y, d.mouse_invert_y);
    put_float("pad_sensitivity", c.pad_sensitivity, d.pad_sensitivity);
    put_float("pad_deadzone", c.pad_deadzone, d.pad_deadzone);
    if (c.pad_triggers != d.pad_triggers || s.has("controls", "pad_shoulders")) {
        s.set("controls", "pad_shoulders", c.pad_triggers ? "triggers" : "bumpers");
    }
    save_bindings(s, c.bindings);
}

void persist_controls() {
    // Uma opção do jogo mudada junto (a vibração ou a inversão da página de controles) passa a ser do .ini antes (sem
    // g_controls_persist só a referência das mudanças anda, como no game_options_sync de cada tick).
    game_options_note_changes();
    if (!g_controls_persist) return;
    save_controls(settings(), controls(), psiMouseSensitivity);
    save_game_options(settings(), game_options_ini());  // [controls] do jogo, [ui] subtitles e [game] (game_options.h)
    settings().save();
}

}  // namespace nf
