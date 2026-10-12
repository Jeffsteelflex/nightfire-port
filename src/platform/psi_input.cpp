// Entrada da plataforma no PC. Ver psi_input.h.
//
// psiInput_PollDevices e psiInput_MapInputs são os do PS2 (os slots da libpad, a zona morta, os oito
// estilos de botões e a vibração, em psi_pad.cpp) com o teclado e o mouse do PC somados no fim: a
// tabela de teclas (input_bindings.h, com os padrões dos jogos de PC: andar WASD, virar e mirar com as setas,
// pular Espaço, agachar C ou Ctrl, atirar botão esquerdo, mirar botão direito, usar/recarregar E, recarregar R,
// modo de tiro botão do meio ou X, visão N, armas na roda e Z/Q, aparelhos 1 a 5 e G, Start Enter ou Esc; nos
// menus setas e WASD, escolher Espaço, círculo Tab, voltar Esc ou Backspace) e as ações só do port
// (psiInput_PortPressed). Esc: ver psi_input.h. A roda mirando com zoom: psiInput_SetWheelZoom.
//
// Só no PC também: o mouse nos menus (PsiMenuMouse, calculado em psiInput_PollDevices para
// engine/menu_mouse.h). Com um menu aberto, os botões do mouse e a roda são só dele (não chegam à tabela de
// teclas: nada de tiro, mira ou troca de arma); quando o menu fecha, os botões ainda segurados ficam de fora
// até serem soltos.
//
// Divergências: a vibração conta ticks com PS2FramesToSkip, que o port deixa 0 (vale 1); o multitap não
// existe (MultitapPresent = 0, sem sceMtapGetConnection a cada 60 chamadas).

#include "platform/psi_input.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/menu.h"
#include "platform/game_options.h"
#include "platform/input_bindings.h"
#include "platform/psi_2d.h"
#include "platform/psi_pad.h"

PsiKeyboardMouse g_psi_keyboard_mouse{};
float psiMouseSensitivity = 0.0025f;

extern uint32_t PS2FramesToSkip;  // engine/menu.cpp

namespace {

float tick_look_dx = 0.0f;
float tick_look_dy = 0.0f;
// A roda: dentes pendentes, e cada dente vira um tick apertado seguido de um solto.
float wheel_pending[2] = {};
bool wheel_now[2] = {};
bool wheel_was[2] = {};
// Bordas: as teclas e o controle no tick anterior.
bool prev_keys[256] = {};
uint16_t prev_pad_buttons = 0;
uint8_t prev_lt = 0, prev_rt = 0;
float prev_stick[4] = {};
int active_device = 0;  // 0 teclado e mouse, 1 controle
// Captura (página de teclas).
PsiCapture capture_state = PsiCapture::Idle;
bool capture_armed = false;
int capture_ticks = 0;
nf::InputCode capture_code;
bool swallow = false;  // até tudo ser solto depois de uma captura
bool modal = false;    // um menu do port por cima do jogo (psiInput_SetModal)
// As opções do jogo aplicadas (para gravar quando a pausa as muda).
// O mouse nos menus (psiInput_MenuMouse).
PsiMenuMouse menu_mouse;
float menu_wheel = 0.0f;        // dentes da roda ainda não entregues ao menu
bool menu_prev_lmb = false;
bool menu_prev_back = false;
bool menu_was_open = false;
uint8_t mouse_swallow = 0;       // botões do mouse ignorados até serem soltos (bits de kMouseButtons)
constexpr int kMouseButtons[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
// As entradas do "pause" que também são do menu (Esc), apertadas com um menu aberto: ficam fora do "pause" até
// serem soltas (pause_menu_shared).
nf::InputCode pause_blocked[nf::kMaxBinds];
// As ações só do port do jogador 0 (nf::PortAction): o valor deste tick e o do anterior.
float port_now[nf::kPortActionCount] = {};
bool wheel_open = false;      // a roda de armas aberta (psiInput_SetWheelOpen)
bool wheel_was_open = false;
float port_prev[nf::kPortActionCount] = {};
// A roda mirando com zoom: o pedido do jogador para o próximo tick, o modo deste tick e os dentes guardados.
bool wheel_zoom_request = false;
bool wheel_zoom_mode = false;
float wheel_zoom_steps = 0.0f;

void clear_port_actions() {
    std::memcpy(port_prev, port_now, sizeof(port_prev));
    for (float& v : port_now) v = 0.0f;
}

bool pause_is_blocked(nf::InputCode c) {
    for (const nf::InputCode& b : pause_blocked) {
        if (b == c) return true;
    }
    return false;
}

void pause_block(nf::InputCode c, bool on) {
    for (nf::InputCode& b : pause_blocked) {
        if (on && !b.valid()) {
            b = c;
            return;
        }
        if (!on && b == c) b = nf::InputCode{};
    }
}

bool is_mouse_button(int vk) {
    for (const int b : kMouseButtons) {
        if (vk == b) return true;
    }
    return false;
}

// O estado do mouse nos menus deste tick: a posição na tela do PS2 (psi_2d_viewport ao contrário), os botões
// e a roda. Com o menu aberto a roda vai toda para ele; o mouse fica "ativo" quando mexe ou clica.
void update_menu_mouse(bool menu_open, float wheel) {
    const PsiKeyboardMouse& km = g_psi_keyboard_mouse;
    PsiMenuMouse& mm = menu_mouse;
    const float old_x = mm.x, old_y = mm.y;
    const bool was_inside = mm.inside;
    mm.inside = km.active && km.cursor_inside && km.view_width > 0 && km.view_height > 0;
    if (mm.inside) {
        const Psi2DViewport vp = psi_2d_viewport(km.view_width, km.view_height);
        mm.x = (km.cursor_x - vp.left) / vp.scale_x;
        mm.y = (km.cursor_y - vp.top) / vp.scale_y;
    }
    mm.moved = mm.inside && (!was_inside || std::fabs(mm.x - old_x) > 0.01f || std::fabs(mm.y - old_y) > 0.01f);
    const bool lmb = km.active && km.down[VK_LBUTTON];
    const bool back = km.active && (km.down[VK_RBUTTON] || km.down[VK_XBUTTON1]);
    mm.held = lmb;
    mm.pressed = lmb && !menu_prev_lmb;
    mm.back = back && !menu_prev_back;
    menu_prev_lmb = lmb;
    menu_prev_back = back;
    mm.wheel = 0;
    if (modal) {
        // O menu do port (estilo Modern) lê o mouse sozinho: os menus do jogo não recebem nada.
        mm.moved = mm.pressed = mm.back = false;
        menu_wheel = 0.0f;
        return;
    }
    if (menu_open) {
        menu_wheel += wheel;
        mm.wheel = int(menu_wheel);
        menu_wheel -= float(mm.wheel);
    } else {
        menu_wheel = 0.0f;
    }
    if (!menu_open) {
        mm.pressed = false;
        mm.back = false;
    }
    if (mm.moved || mm.pressed || mm.back || mm.wheel != 0) mm.active = true;
}

// Teclas que a captura não aceita: Esc cancela, Delete limpa; as de depuração do main.cpp (F2-F4, F12,
// Page Up/Down) e as do Windows.
bool reserved_key(int vk) {
    switch (vk) {
        case VK_ESCAPE:
        case VK_DELETE:
        case VK_F2:
        case VK_F3:
        case VK_F4:
        case VK_F12:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:
            return true;
        default:
            return false;
    }
}

bool nameable_key(int vk) {
    const std::string n = nf::input_name(nf::key_input(uint8_t(vk)));
    return !(n.size() == 4 && n.compare(0, 2, "vk") == 0);
}

float stick_value(int16_t v) { return std::clamp(float(v) / 32767.0f, -1.0f, 1.0f); }

const nf::PadState* player_pad() {
    const PsiPadSlot& s = tSlot[0];
    if (s.xinput < 0 || !psiPadStates[s.xinput].connected) return nullptr;
    return &psiPadStates[s.xinput];
}

// Alguma entrada segurada (teclas, botões do mouse, o controle além da metade).
bool anything_held(const nf::PadState* pad) {
    for (int vk = 1; vk < 256; ++vk) {
        if (g_psi_keyboard_mouse.down[vk] && nameable_key(vk)) return true;
    }
    if (pad != nullptr) {
        if (pad->buttons != 0 || pad->lt > nf::kPadTriggerThreshold || pad->rt > nf::kPadTriggerThreshold) return true;
        for (const int16_t v : {pad->lx, pad->ly, pad->rx, pad->ry}) {
            if (std::fabs(stick_value(v)) > 0.5f) return true;
        }
    }
    return false;
}

// A captura de uma entrada nova (as bordas usam prev_keys e o controle do tick anterior).
void run_capture(const nf::PadState* pad) {
    ++capture_ticks;
    if (!capture_armed) {
        if (!anything_held(pad)) capture_armed = true;
        return;
    }
    const bool* down = g_psi_keyboard_mouse.down;
    if (down[VK_ESCAPE] || capture_ticks > 600) {
        capture_state = PsiCapture::Cancelled;
        swallow = true;
        return;
    }
    if (down[VK_DELETE]) {
        capture_state = PsiCapture::Clear;
        swallow = true;
        return;
    }
    nf::InputCode got;
    for (int vk = 1; vk < 256 && !got.valid(); ++vk) {
        if (down[vk] && !prev_keys[vk] && !reserved_key(vk) && nameable_key(vk)) got = nf::key_input(uint8_t(vk));
    }
    if (!got.valid() && wheel_now[0]) got = nf::wheel_input(false);
    if (!got.valid() && wheel_now[1]) got = nf::wheel_input(true);
    if (!got.valid() && pad != nullptr) {
        const uint16_t pressed = uint16_t(pad->buttons & ~prev_pad_buttons);
        for (uint8_t bit = 0; bit < 16 && !got.valid(); ++bit) {
            if ((pressed & (1u << bit)) != 0) got = nf::pad_input(bit);
        }
        if (!got.valid() && pad->rt > nf::kPadTriggerThreshold && prev_rt <= nf::kPadTriggerThreshold) {
            got = nf::pad_input(nf::kPadInRT);
        }
        if (!got.valid() && pad->lt > nf::kPadTriggerThreshold && prev_lt <= nf::kPadTriggerThreshold) {
            got = nf::pad_input(nf::kPadInLT);
        }
        const int16_t axes[4] = {pad->lx, pad->ly, pad->rx, pad->ry};
        for (int k = 0; k < 4 && !got.valid(); ++k) {
            const float v = stick_value(axes[k]);
            if (std::fabs(v) > 0.5f && std::fabs(prev_stick[k]) <= 0.5f) {
                got = nf::pad_input(uint8_t(nf::kPadInLXPos + k * 2 + (v < 0.0f ? 1 : 0)));
            }
        }
    }
    if (got.valid()) {
        capture_code = got;
        capture_state = PsiCapture::Got;
        swallow = true;
    }
}

// Detecção do aparelho em uso: uma tecla ou botão novo, o mouse andando ou a roda; no controle, um botão
// novo, um gatilho apertado ou um analógico mexendo fora da zona morta. Devolve se o controle foi usado.
bool detect_device(const nf::PadState* pad, const bool* keys) {
    bool kbm = tick_look_dx != 0.0f || tick_look_dy != 0.0f || wheel_now[0] || wheel_now[1];
    if (keys != nullptr) {
        for (int vk = 1; vk < 256 && !kbm; ++vk) kbm = keys[vk] && !prev_keys[vk];
    }
    bool gamepad = false;
    if (pad != nullptr) {
        gamepad = (pad->buttons & ~prev_pad_buttons) != 0 ||
                  (pad->lt > nf::kPadTriggerThreshold && prev_lt <= nf::kPadTriggerThreshold) ||
                  (pad->rt > nf::kPadTriggerThreshold && prev_rt <= nf::kPadTriggerThreshold);
        const int16_t axes[4] = {pad->lx, pad->ly, pad->rx, pad->ry};
        const float dz = std::max(0.25f, nf::controls().pad_deadzone);
        for (int k = 0; k < 4 && !gamepad; ++k) {
            const float v = stick_value(axes[k]);
            gamepad = std::fabs(v) > dz && std::fabs(v - prev_stick[k]) > 0.05f;
        }
    }
    if (kbm) {
        active_device = 0;
    } else if (gamepad) {
        active_device = 1;
    }
    return gamepad;
}

void remember_edges(const nf::PadState* pad) {
    std::memcpy(prev_keys, g_psi_keyboard_mouse.down, sizeof(prev_keys));
    if (pad != nullptr) {
        prev_pad_buttons = pad->buttons;
        prev_lt = pad->lt;
        prev_rt = pad->rt;
        const int16_t axes[4] = {pad->lx, pad->ly, pad->rx, pad->ry};
        for (int k = 0; k < 4; ++k) prev_stick[k] = stick_value(axes[k]);
    } else {
        prev_pad_buttons = 0;
        prev_lt = prev_rt = 0;
        for (float& v : prev_stick) v = 0.0f;
    }
}

// Só no PC: o teclado e o mouse do jogador 0 somados ao controle (ver psi_input.h) e a captura da
// página de teclas. As opções do jogo que os menus mudam vão para o nightfire.ini em psiInput_MapInputs
// (platform/game_options.h).
void map_pc_inputs(PlayerInput_tag* players, uint16_t count) {
    if (count == 0) return;
    PlayerInput_tag& p = players[0];
    const nf::PadState* pad = player_pad();
    const bool* keys = g_psi_keyboard_mouse.active ? g_psi_keyboard_mouse.down : nullptr;
    // O mouse nos menus: uma tecla nova (não do mouse) ou o controle tiram o mouse do comando. Com o menu
    // aberto, os botões do mouse são só do menu; quando ele fecha, os que ainda estão segurados ficam de fora
    // até serem soltos.
    const bool menu_open = MenuManager_GetStatus() != 0;
    if (keys != nullptr) {
        for (int vk = 1; vk < 256; ++vk) {
            if (keys[vk] && !prev_keys[vk] && !is_mouse_button(vk)) menu_mouse.active = false;
        }
    }
    if (menu_was_open && !menu_open) {
        for (int i = 0; i < 5; ++i) {
            if (g_psi_keyboard_mouse.down[kMouseButtons[i]]) mouse_swallow = uint8_t(mouse_swallow | (1u << i));
        }
        tick_look_dx = tick_look_dy = 0.0f;
    }
    menu_was_open = menu_open;
    // A roda de armas fechou: um botão do mouse apertado com ela aberta (um clique para escolher) não vira tiro.
    if (wheel_was_open && !wheel_open) {
        for (int i = 0; i < 5; ++i) {
            if (g_psi_keyboard_mouse.down[kMouseButtons[i]]) mouse_swallow = uint8_t(mouse_swallow | (1u << i));
        }
    }
    wheel_was_open = wheel_open;
    bool masked[256];
    if (keys != nullptr && (menu_open || mouse_swallow != 0)) {
        std::memcpy(masked, keys, sizeof(masked));
        for (int i = 0; i < 5; ++i) {
            const int vk = kMouseButtons[i];
            if (!keys[vk]) mouse_swallow = uint8_t(mouse_swallow & ~(1u << i));
            if (menu_open || (mouse_swallow & (1u << i)) != 0) masked[vk] = false;
        }
        keys = masked;
    }
    if (detect_device(pad, keys)) menu_mouse.active = false;
    const bool pad_ready = MapPlayerToSlot(0) != -1;
    p.connected = pad_ready || active_device == 0 ? 1 : 0;

    if (capture_state == PsiCapture::Waiting) run_capture(pad);
    if (capture_state == PsiCapture::Waiting || swallow || modal) {
        if (swallow && capture_state != PsiCapture::Waiting && !anything_held(pad)) swallow = false;
        std::memset(p.actions, 0, sizeof(p.actions));
        clear_port_actions();
        tick_look_dx = tick_look_dy = 0.0f;
        remember_edges(pad);
        return;
    }

    const nf::ControlsConfig& c = nf::controls();
    float kb[kActionCount] = {};
    float pb[kActionCount] = {};
    nf::InputFrame f;
    f.keys = keys;
    f.wheel_up = wheel_now[0];
    f.wheel_down = wheel_now[1];
    f.pad = pad;
    f.pad_deadzone = c.pad_deadzone;
    // Esc (pausa no jogo, voltar nos menus): com um menu aberto, as entradas do "pause" que também são do menu valem
    // só no menu (Start e triângulo juntos se anulam em Menu_InputAction); fechado o menu, a que continua apertada
    // fica fora do "pause" até ser solta (senão reabriria a pausa).
    const nf::Bindings* bindings = &c.bindings;
    nf::Bindings without_shared;
    nf::InputCode shared[nf::kMaxBinds];
    const int shared_count = nf::pause_menu_shared(c.bindings, shared);
    for (nf::InputCode& b : pause_blocked) {
        if (b.valid() && nf::input_value(b, f) == 0.0f) b = nf::InputCode{};
    }
    for (int i = 0; i < shared_count; ++i) {
        const bool held = nf::input_value(shared[i], f) > 0.0f;
        if (menu_open && held && !pause_is_blocked(shared[i])) pause_block(shared[i], true);
        if (!menu_open && !pause_is_blocked(shared[i])) continue;
        if (bindings == &c.bindings) {
            without_shared = c.bindings;
            bindings = &without_shared;
        }
        nf::InputCode* list = without_shared.in[nf::pause_command()];
        for (int k = 0; k < nf::kMaxBinds; ++k) {
            if (list[k] == shared[i]) list[k] = nf::InputCode{};
        }
    }
    float port[nf::kPortActionCount] = {};
    nf::bindings_to_actions(*bindings, f, false, kb, port);
    if (pad != nullptr) nf::bindings_to_actions(*bindings, f, true, pb, port);
    std::memcpy(port_prev, port_now, sizeof(port_prev));
    std::memcpy(port_now, port, sizeof(port_now));
    float* a = p.actions;
    for (int i = 0; i < kActionCount; ++i) {
        float v = a[i] + pb[i];  // o estilo (controle do PS2) e as entradas do controle religadas
        if (i == kActTurn || i == kActAimX || i == kActAimY || i == kActLookY) v *= c.pad_sensitivity;
        if (i <= kActLookY) {
            a[i] = active_device == 1 ? v : kb[i];
        } else {
            a[i] = std::fabs(kb[i]) > std::fabs(v) ? kb[i] : v;
        }
    }
    // A roda de armas aberta (só do PC): o mouse e o analógico direito são dela; o jogador não gira, não mira, não
    // atira, não usa nem troca de arma, aparelho, modo ou visão (andar, pular e agachar continuam).
    if (wheel_open) {
        for (const int i : {int(kActTurn), int(kActAimX), int(kActAimY), int(kActLookY), int(kActDpadY), int(kActFire), 10,
                            11, 12, 13, 14, 15, 16, int(kActAimMode)}) {
            a[i] = 0.0f;
        }
        for (int k = 0; k < nf::kPortActionCount; ++k) {
            if (k != nf::kPortWeaponWheel) port_now[k] = 0.0f;
        }
        tick_look_dx = tick_look_dy = 0.0f;
    }
    remember_edges(pad);
}

}  // namespace

// [decomp] 0x0010e3a0 psiInput_Init
// Vazia no original. Só no PC: zera o mouse, a captura e as bordas, e aplica ao jogador 0 as opções
// salvas que são do jogo (o estilo de botões, a inversão do olhar e a vibração; Input_Init acabou de pôr
// os padrões do PS2).
void psiInput_Init() {
    tick_look_dx = 0.0f;
    tick_look_dy = 0.0f;
    capture_state = PsiCapture::Idle;
    swallow = false;
    modal = false;
    active_device = 0;
    remember_edges(nullptr);
    for (int k = 0; k < 2; ++k) {
        wheel_pending[k] = 0.0f;
        wheel_now[k] = false;
        wheel_was[k] = false;
    }
    menu_mouse = PsiMenuMouse{};
    menu_wheel = 0.0f;
    menu_prev_lmb = false;
    menu_prev_back = false;
    menu_was_open = false;
    mouse_swallow = 0;
    for (nf::InputCode& b : pause_blocked) b = nf::InputCode{};
    for (int k = 0; k < nf::kPortActionCount; ++k) port_now[k] = port_prev[k] = 0.0f;
    wheel_open = wheel_was_open = false;
    wheel_zoom_request = wheel_zoom_mode = false;
    wheel_zoom_steps = 0.0f;
    PlayerInput_tag& p = PlayerSetting[0];
    const nf::ControlsConfig& c = nf::controls();
    p.style = c.pad_style;
    p.invert_look = c.pad_invert_y ? 1 : 0;
    p.unknown_09 = c.vibration ? 1 : 0;
    // As outras opções do jogo que o .ini tem (legendas, mira automática, volumes...; platform/game_options.h), e o
    // estado de agora como referência para as mudanças que os menus fizerem.
    nf::game_options_apply_ini();
}

// [decomp] 0x0010ca78 psiInput_PollDevices
// Lê os 8 slots (ps2_handle_pad; no PC psi_pad_read_slot, do XInput); num slot pronto soma os botões em
// AllButtonState e passa os quatro analógicos pela zona morta (psi_pad_deadzone), guardando o valor no
// byte lido e na cópia de +0x148 (o original escreve antes na cópia a curva gAnalogStickMappingFunction
// / 2, que a linha seguinte apaga); num slot sem controle zera os botões e centra os analógicos.
// Só no PC, antes: o movimento do mouse e a roda deste tick (o acumulado desde o tick anterior; o que
// ninguém usou no tick anterior se perde) e a leitura do XInput com as opções de [controls].
void psiInput_PollDevices() {
    PsiKeyboardMouse& km = g_psi_keyboard_mouse;
    tick_look_dx = km.active ? km.look_dx : 0.0f;
    tick_look_dy = km.active ? km.look_dy : 0.0f;
    km.look_dx = 0.0f;
    km.look_dy = 0.0f;
    float wheel = km.active ? km.wheel : 0.0f;
    km.wheel = 0.0f;
    // Com um menu aberto a roda é do menu (menu_mouse.h), a não ser na captura de uma tecla, que a aceita.
    const bool menu_open = MenuManager_GetStatus() != 0 && capture_state != PsiCapture::Waiting;
    update_menu_mouse(menu_open, wheel);
    if (menu_open) wheel = 0.0f;
    // Mirando com zoom (o pedido de Player_Zoom no tick anterior): a roda vai toda para o zoom, sem os pulsos da tabela.
    wheel_zoom_mode = wheel_zoom_request && !menu_open && !modal && capture_state != PsiCapture::Waiting;
    wheel_zoom_request = false;
    if (wheel_zoom_mode) {
        wheel_zoom_steps += wheel;
        wheel = 0.0f;
        wheel_pending[0] = wheel_pending[1] = 0.0f;
    } else {
        wheel_zoom_steps = 0.0f;
    }
    if (wheel > 0.0f) wheel_pending[0] = std::min(4.0f, wheel_pending[0] + wheel);
    if (wheel < 0.0f) wheel_pending[1] = std::min(4.0f, wheel_pending[1] - wheel);
    for (int k = 0; k < 2; ++k) {
        wheel_now[k] = false;
        if (wheel_was[k]) {
            wheel_was[k] = false;
        } else if (wheel_pending[k] >= 1.0f) {
            wheel_pending[k] -= 1.0f;
            wheel_now[k] = true;
            wheel_was[k] = true;
        }
    }
    const nf::ControlsConfig& c = nf::controls();
    psiPadDeadZone = int32_t(c.pad_deadzone * 127.0f + 0.5f);
    psiPadMask = nf::bindings_pad_mask(c.bindings);
    psiPadTriggersR1 = c.pad_triggers;
    psi_pad_poll();

    AllButtonState = 0;
    for (int32_t i = 0; i < kPsiPadSlots; ++i) {
        PsiPadSlot& s = tSlot[i];
        psi_pad_read_slot(i);
        if (s.state == 99) {
            AllButtonState |= s.buttons;
            for (int k = 0; k < 4; ++k) {
                const uint8_t v = psi_pad_deadzone(s.sticks[k], psiPadDeadZone);
                s.sticks[k] = v;
                s.mapped[k] = v;
            }
        } else {
            s.pressed = 0;
            s.buttons = 0;
            s.sticks[2] = 0x7f;
            s.sticks[3] = 0x7f;
            s.sticks[0] = 0x7f;
            s.sticks[1] = 0x7f;
        }
    }
}

// [decomp] 0x0010cd80 psiInput_MapInputs
// A vibração com tempo (cada slot conta os ticks; ao acabar, desliga os motores) e, com o jogo pausado,
// todos os motores desligados; depois cada jogador: o índice, o slot (MapPlayerToSlot; sem controle:
// desconectado) e as ações do estilo de botões (psi_pad_map_player). Fora do laço, a ação 37 do
// jogador 3 é o X do slot 4. Só no PC, no fim: o teclado e o mouse do jogador 0 (map_pc_inputs); no começo, as
// opções do jogo que um menu mudou no tick anterior vão para o nightfire.ini (nf::game_options_sync).
void psiInput_MapInputs(PlayerInput_tag* players, uint16_t count) {
    nf::game_options_sync();
    const int32_t frames = PS2FramesToSkip != 0 ? int32_t(PS2FramesToSkip) : 1;
    for (PsiPadSlot& s : tSlot) {
        if (s.rumble_time < 0) continue;
        s.rumble_time -= frames;
        if (s.rumble_time < 0 && s.state == 99 && s.status == 0) {
            s.motor_small = 0;
            s.motor_big = 0;
            psi_pad_set_act(s);
        }
    }
    if (GameState.paused != 0) {
        for (PsiPadSlot& s : tSlot) {
            if (s.state != 99 || s.status != 0) continue;
            s.motor_small = 0;
            s.motor_big = 0;
            psi_pad_set_act(s);
        }
    }
    for (uint16_t i = 0; i < count; ++i) {
        PlayerInput_tag& p = players[i];
        p.index = uint8_t(i);
        const int32_t slot = MapPlayerToSlot(i);
        if (slot == -1) {
            p.connected = 0;
            continue;
        }
        p.connected = 1;
        psi_pad_map_player(p, tSlot[slot], i);
    }
    players[3].actions[37] = float(tSlot[4].buttons & kPs2Cross);
    map_pc_inputs(players, count);
}

void psiInput_TakeMouseLook(int player, float* yaw, float* pitch, bool aiming, float zoom) {
    if (player != 0) {
        *yaw = 0.0f;
        *pitch = 0.0f;
        return;
    }
    const nf::ControlsConfig& c = nf::controls();
    float s = psiMouseSensitivity;
    if (aiming) s *= c.mouse_aim_scale / std::max(1.0f, zoom);
    *yaw = -tick_look_dx * s;
    *pitch = -tick_look_dy * s * (c.mouse_invert_y ? -1.0f : 1.0f);
    tick_look_dx = 0.0f;
    tick_look_dy = 0.0f;
}

void psiInput_PeekMouseLook(int player, float* yaw, float* pitch, bool aiming, float zoom) {
    const PsiKeyboardMouse& km = g_psi_keyboard_mouse;
    if (player != 0 || !km.active || modal || capture_state == PsiCapture::Waiting || swallow) {
        *yaw = 0.0f;
        *pitch = 0.0f;
        return;
    }
    const nf::ControlsConfig& c = nf::controls();
    float s = psiMouseSensitivity;
    if (aiming) s *= c.mouse_aim_scale / std::max(1.0f, zoom);
    *yaw = -km.look_dx * s;
    *pitch = -km.look_dy * s * (c.mouse_invert_y ? -1.0f : 1.0f);
}

bool psiInput_PortPressed(int player, int action) {
    if (player != 0 || action < 0 || action >= nf::kPortActionCount) return false;
    return port_now[action] > 0.0f && port_prev[action] == 0.0f;
}

void psiInput_SetWheelOpen(bool on) { wheel_open = on; }

bool psiInput_WheelOpen() { return wheel_open; }

void psiInput_SetWheelZoom(int player, bool on) {
    if (player == 0) wheel_zoom_request = on;
}

float psiInput_TakeWheelZoom(int player) {
    if (player != 0 || !wheel_zoom_mode) return 0.0f;
    const float n = wheel_zoom_steps;
    wheel_zoom_steps = 0.0f;
    return n;
}

int psiInput_ActiveDevice() { return active_device; }

const PsiMenuMouse& psiInput_MenuMouse() { return menu_mouse; }

int psiInput_PadIndex() { return tSlot[0].xinput; }

void psiInput_BeginCapture() {
    capture_state = PsiCapture::Waiting;
    capture_armed = false;
    capture_ticks = 0;
    capture_code = nf::InputCode{};
}

void psiInput_EndCapture() {
    if (capture_state == PsiCapture::Waiting) swallow = true;
    capture_state = PsiCapture::Idle;
}

PsiCapture psiInput_CaptureState() { return capture_state; }

void psiInput_SetModal(bool on) {
    if (modal && !on) swallow = true;  // o que fechou o menu (e o resto ainda apertado) fica de fora
    modal = on;
}

bool psiInput_Modal() { return modal; }

const nf::PadState* psiInput_PlayerPad() { return player_pad(); }

bool psiInput_CaptureResult(nf::InputCode* out) {
    if (capture_state != PsiCapture::Got) return false;
    *out = capture_code;
    return true;
}
