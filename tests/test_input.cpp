// Testes da entrada do PC (platform/input_bindings, psi_pad, psi_input, gamepad) e das funções do jogo
// portadas com ela (Input_RumbleStart, Input_ChangeControllerStyle, Menu_DisplayControllerStyleList), e
// das páginas de controles do port (engine/menu_pc_controls). O controle é um ScriptedPadSource: nenhum
// teste depende de um controle de verdade.


#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/font.h"
#include "platform/keys.h"
#include "engine/gamestate.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/menu.h"
#include "engine/menu_mouse.h"
#include "engine/menu_pc_controls.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/pc_text.h"
#include "engine/resetmap.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/gamepad.h"
#include "platform/input_bindings.h"
#include "platform/psi.h"
#include "platform/psi_2d.h"
#include "platform/psi_input.h"
#include "platform/psi_pad.h"
#include "platform/settings.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

// O controle simulado instalado e a entrada do jogo zerada, sem configuração salva.
struct InputWorld {
    nf::ScriptedPadSource pad;
    InputWorld() {
        nf::controls() = nf::ControlsConfig{};
        nf::set_pad_source(&pad);
        nf::g_pad_enabled = true;
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        g_psi_keyboard_mouse.active = true;
        std::memset(&GameState, 0, sizeof(GameState));
        Input_Init();
        Input_Update();  // a primeira atualização só libera as travas
    }
    ~InputWorld() {
        nf::set_pad_source(nullptr);
        nf::controls() = nf::ControlsConfig{};
        std::memset(&g_psi_keyboard_mouse, 0, sizeof(g_psi_keyboard_mouse));
        for (PsiPadSlot& s : tSlot) s = PsiPadSlot{};
        GameState.paused = 0;
    }
    void keys(std::initializer_list<int> vks) {
        std::memset(g_psi_keyboard_mouse.down, 0, sizeof(g_psi_keyboard_mouse.down));
        for (int vk : vks) g_psi_keyboard_mouse.down[vk] = true;
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) Input_Update();
    }
    float a(int action) const { return PlayerSetting[0].actions[action]; }
};

// Os padrões do teclado e do mouse no costume dos jogos de PC: cada entrada (tecla, botão do mouse ou dente da roda)
// e as ações do jogo que ela liga, com o sinal, e a ação só do port (PortAction, -1 nenhuma). Escrito à parte, como
// especificação; a configuração 0 do PS2 que o port usava antes mudou em R (o modo de tiro, agora no botão do meio e no
// X: R recarrega), Tab (a visão, agora no N: Tab livre no jogo), 1/2 (os aparelhos em ciclo e o zoom: agora 1 a 5 são os
// espaços dos aparelhos e G o próximo), a roda (as armas) e o Esc (a pausa no jogo, voltar nos menus).
struct SpecKey {
    int vk;  // código virtual; kWheelUp/kWheelDown para a roda
    std::vector<std::pair<int, float>> actions;
    int port = -1;
};
constexpr int kWheelUp = 0x1000, kWheelDown = 0x1001;
const std::vector<SpecKey>& pc_mapping() {
    static const std::vector<SpecKey> m = {
        {'W', {{2, 1.0f}, {26, 1.0f}}},
        {'S', {{2, -1.0f}, {27, 1.0f}}},
        {'A', {{1, 1.0f}, {28, 1.0f}}},
        {'D', {{1, -1.0f}, {29, 1.0f}}},
        {nf::key::Right, {{0, 1.0f}, {3, 1.0f}, {29, 1.0f}}},
        {nf::key::Left, {{0, -1.0f}, {3, -1.0f}, {28, 1.0f}}},
        {nf::key::Up, {{4, 1.0f}, {5, 1.0f}, {26, 1.0f}}},
        {nf::key::Down, {{4, -1.0f}, {5, -1.0f}, {27, 1.0f}}},
        {nf::key::Space, {{7, 1.0f}, {25, 1.0f}, {31, 1.0f}}},
        {'C', {{8, 1.0f}}},
        {nf::key::Control, {{8, 1.0f}}},
        {nf::key::LButton, {{9, 1.0f}}},
        {nf::key::RButton, {{19, 1.0f}}},
        {nf::key::MButton, {{12, 1.0f}}},  // modo de tiro / acessório
        {'X', {{12, 1.0f}}},
        {'N', {{13, 1.0f}}},         // visão noturna / raio X
        {nf::key::Tab, {{33, 1.0f}}, nf::kPortWeaponWheel},  // a roda de armas no jogo (segurada) e o círculo dos menus
        {'E', {{14, 1.0f}, {32, 1.0f}}},  // usar/recarregar e o quadrado dos menus (menu_square)
        {'R', {}, nf::kPortReload},       // recarregar (o restaurar padrões dos menus Modern não tem ação do jogo)
        {kWheelDown, {{16, 1.0f}}},  // a próxima arma
        {kWheelUp, {{15, 1.0f}}},    // a anterior
        {'Z', {{16, 1.0f}}},
        {'Q', {{15, 1.0f}}},
        {'G', {{6, -1.0f}, {11, 1.0f}}},  // o próximo aparelho (e o zoom do direcional, mirando)
        {'1', {}, nf::kPortGadget1},
        {'2', {}, nf::kPortGadget1 + 1},
        {'3', {}, nf::kPortGadget1 + 2},
        {'4', {}, nf::kPortGadget1 + 3},
        {'5', {}, nf::kPortGadget1 + 4},
        {nf::key::Return, {{25, 1.0f}, {30, 1.0f}}},
        {nf::key::Escape, {{25, 1.0f}, {30, 1.0f}, {34, 1.0f}}},  // a pausa (Start) e o voltar dos menus
        {nf::key::Back, {{34, 1.0f}}},
        {'F', {}},
        {'T', {}},
        {'V', {}},
    };
    return m;
}

void keyboard_actions(const nf::Bindings& b, std::initializer_list<int> vks, float out[40], float* port = nullptr) {
    bool down[256] = {};
    nf::InputFrame f;
    for (int vk : vks) {
        if (vk == kWheelUp) f.wheel_up = true;
        else if (vk == kWheelDown) f.wheel_down = true;
        else down[vk] = true;
    }
    f.keys = down;
    std::memset(out, 0, sizeof(float) * 40);
    if (port != nullptr) std::memset(port, 0, sizeof(float) * nf::kPortActionCount);
    nf::bindings_to_actions(b, f, false, out, port);
}

int find_cmd(const char* key) { return nf::find_bind_command(key); }

}  // namespace

TEST(input_bindings_defaults_follow_the_pc_spec) {
    const nf::Bindings b = nf::default_bindings();
    for (const SpecKey& k : pc_mapping()) {
        float a[40];
        float port[nf::kPortActionCount];
        keyboard_actions(b, {k.vk}, a, port);
        float expected[40] = {};
        for (const auto& [action, value] : k.actions) expected[action] = value;
        for (int i = 0; i < 40; ++i) {
            if (a[i] != expected[i]) {
                char msg[96];
                std::snprintf(msg, sizeof(msg), "tecla %02x ação %d: %g, esperado %g", k.vk, i, double(a[i]),
                              double(expected[i]));
                nftest::fail(__FILE__, __LINE__, msg);
            }
        }
        for (int i = 0; i < nf::kPortActionCount; ++i) {
            if (port[i] != (i == k.port ? 1.0f : 0.0f)) {
                char msg[96];
                std::snprintf(msg, sizeof(msg), "tecla %02x ação do port %d: %g", k.vk, i, double(port[i]));
                nftest::fail(__FILE__, __LINE__, msg);
            }
        }
    }
    // Nenhuma entrada de teclado/mouse fica em dois comandos do jogo (o grupo do menu repete o E, como antes: o
    // quadrado e a aba DOSSIER do mapa Modern), e nenhuma tecla de depuração do main.cpp (V, H, P, T, F2-F4, F9, F12,
    // Page Up/Down) é padrão de um comando.
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        for (int k = 0; k < nf::kMaxBinds; ++k) {
            const nf::InputCode c = b.in[i][k];
            if (!c.valid()) continue;
            for (const int vk : {int('V'), int('H'), int('P'), int('T'), nf::key::F2, nf::key::F3, nf::key::F4, nf::key::F9, nf::key::F12, nf::key::Prior, nf::key::Next}) {
                CHECK(c != nf::key_input(uint8_t(vk)));
            }
            if (nf::kBindCommands[i].group != nf::BindGroup::Game) continue;
            for (int j = i + 1; j < nf::kBindCommandCount; ++j) {
                if (nf::kBindCommands[j].group == nf::BindGroup::Game) CHECK(!b.has(j, c));
            }
        }
    }
    // Tab no jogo: a roda de armas (segurada; no controle L3, o botão que nenhum estilo usa); no menu, o círculo.
    CHECK(!b.has(find_cmd("vision"), nf::key_input(nf::key::Tab)) && b.has(find_cmd("menu_circle"), nf::key_input(nf::key::Tab)));
    CHECK(b.in[find_cmd("weapon_wheel")][0] == nf::key_input(nf::key::Tab) &&
          b.has(find_cmd("weapon_wheel"), nf::pad_input(nf::kPadInLThumb)));
    CHECK(nf::kBindCommands[find_cmd("weapon_wheel")].group == nf::BindGroup::Game);
    // L3 não está em nenhum dos oito estilos do controle do PS2 (nada some do controle por estar na roda).
    for (uint16_t style = 0; style < 8; ++style) {
        for (int act = 0; act < 40; ++act) {
            CHECK((psi_pad_style_inputs(style, act, 1, true) & (1u << nf::kPadInLThumb)) == 0);
            CHECK((psi_pad_style_inputs(style, act, -1, true) & (1u << nf::kPadInLThumb)) == 0);
        }
    }
    // O Esc está na pausa e no voltar; o Enter continua na pausa (e é a primeira dela: os símbolos de Start mostram Enter).
    CHECK(b.in[find_cmd("pause")][0] == nf::key_input(nf::key::Return) && b.has(find_cmd("pause"), nf::key_input(nf::key::Escape)));
    CHECK(b.in[find_cmd("menu_back")][0] == nf::key_input(nf::key::Escape));
    nf::InputCode shared[nf::kMaxBinds];
    CHECK(nf::pause_menu_shared(b, shared) == 1 && shared[0] == nf::key_input(nf::key::Escape));
    // As ações do port mascaram um botão do controle religado nelas (como as do jogo).
    nf::Bindings r = b;
    nf::bind(r, find_cmd("reload"), nf::pad_input(nf::kPadInX));
    // (L3 é da roda de armas, uma ação do port também.)
    CHECK(nf::bindings_pad_mask(r) == (1u << nf::kPadInX | 1u << nf::kPadInLThumb));
    // A coluna do controle: recarregar é o usar do estilo (o mesmo botão no PS2); os espaços dos aparelhos, nada.
    CHECK(nf::pad_style_action(find_cmd("reload")) == 14 && nf::pad_style_action(find_cmd("gadget_1")) == -1);
    CHECK(nf::pad_style_action(find_cmd("fire")) == 9 && nf::pad_style_action(find_cmd("menu_tab_prev")) == -1);
    // Andar na diagonal: 1/sqrt(2) em cada eixo, como o analógico.
    float a[40];
    keyboard_actions(b, {'W', 'A'}, a);
    CHECK_NEAR(a[2], 0.70710677, 1e-6);
    CHECK_NEAR(a[1], 0.70710677, 1e-6);
    // Teclas opostas se anulam; o quadrado (32) é o E, a mesma tecla da aba DOSSIER do mapa Modern (menu_tab_next).
    keyboard_actions(b, {nf::key::Left, nf::key::Right}, a);
    CHECK(a[0] == 0.0f && a[3] == 0.0f);
    for (const nf::BindCommand& c : nf::kBindCommands) {
        if (std::strcmp(c.key, "menu_square") == 0) CHECK(std::strcmp(c.defaults, "e") == 0);
    }
    CHECK(b.has(find_cmd("menu_square"), nf::key_input('E')) && b.has(find_cmd("menu_tab_next"), nf::key_input('E')));
}

TEST(input_bindings_pipeline_matches_keyboard) {
    // Pela entrada do jogo inteira (psiInput_MapInputs + Input_Update), sem controle: o mesmo de antes.
    InputWorld w;
    w.keys({'W', nf::key::LButton});
    w.tick();
    CHECK(w.a(kActForward) == 1.0f);
    CHECK(Input_Action(0, kActFire, kActionPressed) != 0);
    CHECK(PlayerSetting[0].connected == 1);
    w.keys({});
    w.tick();
    CHECK(w.a(kActForward) == 0.0f && Input_Action(0, kActFire, kActionDown) == 0);
    // Sem o mouse capturado (active falso), nada.
    g_psi_keyboard_mouse.active = false;
    w.keys({'W'});
    w.tick();
    CHECK(w.a(kActForward) == 0.0f);
}

TEST(input_names_round_trip) {
    int nameable = 0;
    for (int vk = 1; vk < 255; ++vk) {
        const nf::InputCode c = nf::key_input(uint8_t(vk));
        const std::string name = nf::input_name(c);
        nf::InputCode back;
        CHECK(nf::parse_input_name(name, &back) && back == c);
        if (name.compare(0, 2, "vk") != 0) ++nameable;
    }
    CHECK(nameable > 100);
    for (uint8_t code = 0; code < nf::kPadInCount; ++code) {
        if (code == 10 || code == 11) continue;  // bits sem botão no XInput
        nf::InputCode back;
        CHECK(nf::parse_input_name(nf::input_name(nf::pad_input(code)), &back) && back == nf::pad_input(code));
    }
    nf::InputCode c;
    CHECK(nf::parse_input_name("Wheel_Down", &c) && c == nf::wheel_input(true));
    CHECK(nf::parse_input_name(" F12 ", &c) && c == nf::key_input(nf::key::F12));
    CHECK(nf::parse_input_name("num7", &c) && c == nf::key_input(nf::key::Numpad7));
    CHECK(!nf::parse_input_name("f25", &c) && !nf::parse_input_name("pad_q", &c) && !nf::parse_input_name("", &c));
}

TEST(input_bindings_parse_and_format_round_trip) {
    // Os padrões: formatar e ler de volta dá a mesma lista.
    const nf::Bindings d = nf::default_bindings();
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        nf::InputCode back[nf::kMaxBinds];
        nf::parse_inputs(nf::format_inputs(d.in[i], nf::kMaxBinds), back, nf::kMaxBinds);
        for (int k = 0; k < nf::kMaxBinds; ++k) CHECK(back[k] == d.in[i][k]);
    }
    CHECK(nf::format_inputs(d.in[find_cmd("crouch")], nf::kMaxBinds) == "c, ctrl");
    // Maiúsculas, espaços, nomes desconhecidos e repetidos; até 2 do teclado/mouse e 1 do controle, o
    // controle depois.
    nf::InputCode in[nf::kMaxBinds];
    CHECK(nf::parse_inputs("pad_a, MOUSE1 , xyz, mouse1, f, g, pad_b", in, nf::kMaxBinds) == 3);
    CHECK(in[0] == nf::key_input(nf::key::LButton) && in[1] == nf::key_input('F') && in[2] == nf::pad_input(nf::kPadInA));
    CHECK(!in[3].valid());
    CHECK(nf::parse_inputs("", in, nf::kMaxBinds) == 0 && !in[0].valid());
}

TEST(input_bindings_conflicts) {
    nf::Bindings b = nf::default_bindings();
    const int fire = find_cmd("fire"), jump = find_cmd("jump"), forward = find_cmd("forward");
    const int use = find_cmd("use"), pause = find_cmd("pause"), crouch = find_cmd("crouch");
    const int select = find_cmd("menu_select"), back = find_cmd("menu_back"), menu_up = find_cmd("menu_up");
    // F em atirar: vira a primeira, o mouse fica de segunda.
    nf::BindResult r = nf::bind(b, fire, nf::key_input('F'));
    CHECK(!r.refused && r.moved_from == -1);
    CHECK(b.in[fire][0] == nf::key_input('F') && b.in[fire][1] == nf::key_input(nf::key::LButton));
    // W em pular: sai de andar para a frente (mesmo grupo), que fica sem tecla; o menu continua com W.
    r = nf::bind(b, jump, nf::key_input('W'));
    CHECK(r.moved_from == forward && r.swapped_to == -1);
    CHECK(!b.in[forward][0].valid() && b.has(jump, nf::key_input('W')) && b.has(menu_up, nf::key_input('W')));
    // Uma terceira tecla de teclado tira a mais antiga.
    nf::bind(b, crouch, nf::key_input('G'));
    CHECK(b.in[crouch][0] == nf::key_input('G') && b.in[crouch][1] == nf::key_input('C'));
    CHECK(!b.has(crouch, nf::key_input(nf::key::Control)));
    // Enter em usar: sai da pausa, que é obrigatória mas continua com o Esc (não troca).
    r = nf::bind(b, use, nf::key_input(nf::key::Return));
    CHECK(!r.refused && r.moved_from == pause && r.swapped_to == -1);
    CHECK(b.in[use][0] == nf::key_input(nf::key::Return) && b.has(use, nf::key_input('E')));
    CHECK(b.in[pause][0] == nf::key_input(nf::key::Escape) && !b.has(pause, nf::key_input(nf::key::Return)));
    // Espaço em voltar: escolher (obrigatório, só com o Espaço) fica com o Esc, a primeira de voltar (troca).
    r = nf::bind(b, back, nf::key_input(nf::key::Space));
    CHECK(r.swapped_to == select && r.swapped == nf::key_input(nf::key::Escape));
    CHECK(b.in[select][0] == nf::key_input(nf::key::Escape) && b.in[back][0] == nf::key_input(nf::key::Space));
    CHECK(b.has(back, nf::key_input(nf::key::Back)) && !b.has(back, nf::key_input(nf::key::Escape)));
    // Um comando vazio não tem o que trocar: andar para a frente (sem tecla desde o W) não leva o Esc, a única tecla da
    // pausa (obrigatória): recusa e nada muda.
    nf::Bindings before = b;
    r = nf::bind(b, forward, nf::key_input(nf::key::Escape));
    CHECK(r.refused && r.moved_from == pause);
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        for (int k = 0; k < nf::kMaxBinds; ++k) CHECK(b.in[i][k] == before.in[i][k]);
    }
    // Limpar: não num obrigatório.
    CHECK(!nf::clear_binds(b, pause, false) && nf::clear_binds(b, fire, false) && !b.in[fire][0].valid());
    // Controle: um por comando, e também sai do outro comando.
    nf::bind(b, fire, nf::pad_input(nf::kPadInA));
    nf::bind(b, jump, nf::pad_input(nf::kPadInA));
    CHECK(!b.has(fire, nf::pad_input(nf::kPadInA)) && b.has(jump, nf::pad_input(nf::kPadInA)));
    nf::bind(b, jump, nf::pad_input(nf::kPadInB));
    CHECK(!b.has(jump, nf::pad_input(nf::kPadInA)) && b.has(jump, nf::pad_input(nf::kPadInB)));
    CHECK(nf::bindings_pad_mask(b) == (1u << nf::kPadInB | 1u << nf::kPadInLThumb));  // L3: a roda de armas
}

TEST(input_controls_ini_round_trip) {
    nf::ControlsConfig c;
    nf::bind(c.bindings, find_cmd("fire"), nf::key_input('F'));
    nf::bind(c.bindings, find_cmd("jump"), nf::pad_input(nf::kPadInRT));
    c.mouse_aim_scale = 0.5f;
    c.mouse_invert_y = true;
    c.pad_sensitivity = 2.0f;
    c.pad_deadzone = 0.125f;
    c.pad_invert_y = true;
    c.vibration = false;
    c.pad_style = 4;
    c.pad_triggers = false;
    nf::Settings s;
    nf::save_controls(s, c, 0.003f);
    const std::string text = s.text();
    CHECK(text.find("[bindings]") != std::string::npos && text.find("fire = f, mouse1") != std::string::npos);
    CHECK(text.find("jump = space, pad_rt") != std::string::npos);
    // Só o que não está no padrão vai para um arquivo novo: os comandos com as teclas padrão ficam de fora, e as três
    // opções do jogo (vibration, pad_style, pad_invert_y) não são do save_controls.
    CHECK(!s.has("bindings", "menu_square") && !s.has("bindings", "forward") && s.has("bindings", "jump"));
    CHECK(!s.has("controls", "pad_style") && !s.has("controls", "vibration") && !s.has("controls", "pad_invert_y"));
    // Elas vão pelo save_game_options, quando o .ini as conhece (platform/game_options.h).
    nf::GameOptionsIni g;
    g.known[int(nf::GameOption::PadStyle)] = true;
    g.value[int(nf::GameOption::PadStyle)] = 4;
    g.known[int(nf::GameOption::Vibration)] = true;
    g.value[int(nf::GameOption::Vibration)] = 0;
    g.known[int(nf::GameOption::InvertLook)] = true;
    g.value[int(nf::GameOption::InvertLook)] = 1;
    nf::save_game_options(s, g);
    nf::Settings t;
    t.parse(s.text());
    nf::ControlsConfig d;
    nf::load_controls(t, &d);
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        for (int k = 0; k < nf::kMaxBinds; ++k) CHECK(d.bindings.in[i][k] == c.bindings.in[i][k]);
    }
    CHECK(d.mouse_aim_scale == 0.5f && d.mouse_invert_y && d.pad_sensitivity == 2.0f && d.pad_deadzone == 0.125f);
    CHECK(d.pad_invert_y && !d.vibration && d.pad_style == 4 && !d.pad_triggers);
    CHECK_NEAR(t.get_float("controls", "mouse_sensitivity", 0.0f), 0.003, 1e-7);
    // Um arquivo que já tem a chave a recebe de novo, mesmo no padrão; as outras no padrão continuam de fora.
    nf::Settings v;
    v.parse("[controls]\nmouse_invert_y = 1\n[bindings]\nforward = up\n");
    nf::save_controls(v, nf::ControlsConfig{}, nf::kDefaultMouseSensitivity);
    CHECK(v.has("controls", "mouse_invert_y") && !v.get_bool("controls", "mouse_invert_y", true));
    CHECK(v.get("bindings", "forward") == "w" && !v.has("bindings", "back"));
    CHECK(!v.has("controls", "mouse_sensitivity") && !v.has("controls", "pad_deadzone") && !v.has("controls", "pad_shoulders"));
    // O estilo pelo nome do menu; comandos ausentes ficam no padrão.
    nf::Settings u;
    u.parse("[controls]\npad_style = GoldenEye\n[bindings]\nfire = \n");
    nf::ControlsConfig e;
    nf::load_controls(u, &e);
    CHECK(e.pad_style == 6 && !e.bindings.in[find_cmd("fire")][0].valid());
    CHECK(e.bindings.in[find_cmd("jump")][0] == nf::key_input(nf::key::Space));
}

TEST(input_old_ini_keeps_its_bindings_over_new_defaults) {
    // Um nightfire.ini de antes dos padrões de PC: o que ele escreveu continua valendo; os padrões novos que usariam a
    // mesma entrada no mesmo grupo a perdem (nada faz duas coisas).
    nf::Settings s;
    s.parse("[bindings]\nfire_mode = r\nvision = tab\ngadget_next = 1\n");
    nf::ControlsConfig c;
    nf::load_controls(s, &c);
    const nf::Bindings& b = c.bindings;
    CHECK(b.in[find_cmd("fire_mode")][0] == nf::key_input('R') && !b.in[find_cmd("fire_mode")][1].valid());
    CHECK(!b.has(find_cmd("reload"), nf::key_input('R')) && !b.in[find_cmd("reload")][0].valid());
    CHECK(b.in[find_cmd("vision")][0] == nf::key_input(nf::key::Tab));
    CHECK(b.has(find_cmd("menu_circle"), nf::key_input(nf::key::Tab)));  // outro grupo: o menu continua com o Tab
    CHECK(b.in[find_cmd("gadget_next")][0] == nf::key_input('1') && !b.in[find_cmd("gadget_1")][0].valid());
    CHECK(b.in[find_cmd("gadget_2")][0] == nf::key_input('2'));
    // O R do menu (restaurar padrões, outro grupo) e os padrões novos sem conflito ficam.
    CHECK(b.has(find_cmd("menu_defaults"), nf::key_input('R')) && b.has(find_cmd("weapon_next"), nf::wheel_input(true)));
    // Gravar de novo escreve o comando que perdeu a entrada (vazio), para o arquivo dizer o que vale.
    nf::save_controls(s, c, nf::kDefaultMouseSensitivity);
    CHECK(s.has("bindings", "reload") && s.get("bindings", "reload").empty() && s.get("bindings", "fire_mode") == "r");
    CHECK(!s.has("bindings", "gadget_2") && !s.has("bindings", "pause"));
    // Um obrigatório que ficaria sem tecla fica com os padrões (o conflito, como antes).
    nf::Settings t;
    t.parse("[bindings]\nuse = enter\njump = escape\n");
    nf::ControlsConfig d;
    nf::load_controls(t, &d);
    CHECK(d.bindings.has(find_cmd("pause"), nf::key_input(nf::key::Return)) && d.bindings.has(find_cmd("pause"), nf::key_input(nf::key::Escape)));
    // Sem [bindings], os padrões novos.
    nf::Settings u;
    nf::ControlsConfig e;
    nf::load_controls(u, &e);
    CHECK(e.bindings.in[find_cmd("reload")][0] == nf::key_input('R') && e.bindings.in[find_cmd("vision")][0] == nf::key_input('N'));
}

TEST(input_mouse_sensitivity_scale) {
    // A barra: o padrão na posição dele; 8 passos dobram; as pontas cobrem 400 a 3200 DPI de 5 a 100 cm por volta.
    CHECK(nf::mouse_sens_to_pos(nf::kDefaultMouseSensitivity) == nf::kMouseSensDefaultPos);
    CHECK(nf::mouse_sens_from_pos(nf::kMouseSensDefaultPos) == nf::kDefaultMouseSensitivity);
    for (int p = nf::kMouseSensPosMin; p + 8 <= nf::kMouseSensPosMax; ++p) {
        CHECK_NEAR(nf::mouse_sens_from_pos(p + 8) / nf::mouse_sens_from_pos(p), 2.0, 1e-5);
        CHECK(nf::mouse_sens_to_pos(nf::mouse_sens_from_pos(p)) == p);
    }
    CHECK(nf::mouse_sens_to_pos(1.0f) == nf::kMouseSensPosMax && nf::mouse_sens_to_pos(0.0f) == nf::kMouseSensPosMin);
    // Os centímetros: contagens por volta (2 pi / sensibilidade) sobre os DPI, em polegadas de 2,54 cm.
    const float s = nf::kDefaultMouseSensitivity;
    CHECK_NEAR(nf::mouse_cm_per_360(s, 800.0f) * 800.0f / 2.54f * s, 2.0 * 3.14159265, 1e-4);
    CHECK_NEAR(nf::mouse_cm_per_360(s, 1600.0f) * 2.0f, nf::mouse_cm_per_360(s, 800.0f), 1e-4);
    const float fastest = nf::mouse_sens_from_pos(nf::kMouseSensPosMax);
    const float slowest = nf::mouse_sens_from_pos(nf::kMouseSensPosMin);
    CHECK(nf::mouse_cm_per_360(fastest, 400.0f) < 5.0f);   // 400 DPI: até menos de 5 cm por volta
    CHECK(nf::mouse_cm_per_360(slowest, 3200.0f) > 100.0f);  // 3200 DPI: até mais de 100 cm
}

TEST(input_escape_pauses_in_play_and_backs_in_menus) {
    InputWorld w;
    // Jogando: Esc é o Start (pausa) e o 25 (pular a cena, a cortina de sangue), como o Enter.
    w.keys({nf::key::Escape});
    w.tick();
    CHECK(Input_Action(0, kActPause, kActionPressed) != 0 && w.a(kActMenuStart) == 1.0f);
    w.keys({});
    w.tick();
    // R e 1 a 5: as ações do port acabam de ligar só no primeiro tick.
    w.keys({'R'});
    w.tick();
    CHECK(psiInput_PortPressed(0, nf::kPortReload) && !psiInput_PortPressed(1, nf::kPortReload));
    w.tick();
    CHECK(!psiInput_PortPressed(0, nf::kPortReload));
    w.keys({'3'});
    w.tick();
    CHECK(psiInput_PortPressed(0, nf::kPortGadget1 + 2) && !psiInput_PortPressed(0, nf::kPortGadget1));
    w.keys({});
    w.tick();
}

TEST(input_wheel_changes_weapons_or_zooms) {
    InputWorld w;
    // A roda: para baixo a próxima arma (16), para cima a anterior (15); um dente é um tick.
    g_psi_keyboard_mouse.wheel = -1.0f;
    w.tick();
    CHECK(w.a(16) == 1.0f && w.a(15) == 0.0f);
    w.tick();
    g_psi_keyboard_mouse.wheel = 1.0f;
    w.tick();
    CHECK(w.a(15) == 1.0f && w.a(16) == 0.0f);
    w.tick(2);
    // Mirando com zoom (o pedido de Player_Zoom vale para o tick seguinte): a roda inteira vai para o zoom.
    psiInput_SetWheelZoom(0, true);
    g_psi_keyboard_mouse.wheel = 2.0f;
    w.tick();
    CHECK(w.a(15) == 0.0f && w.a(16) == 0.0f);
    CHECK(psiInput_TakeWheelZoom(0) == 2.0f && psiInput_TakeWheelZoom(0) == 0.0f);
    // Sem o pedido no tick seguinte, a roda volta às armas.
    g_psi_keyboard_mouse.wheel = -1.0f;
    w.tick();
    CHECK(w.a(16) == 1.0f && psiInput_TakeWheelZoom(0) == 0.0f);
}

TEST(input_pad_style0_moves_and_fires) {
    InputWorld w;
    w.tick();
    // O controle conectado ocupa o slot 0 (jogador 0).
    w.pad.state[0].connected = true;
    w.pad.state[0].ly = 32767;   // analógico esquerdo para cima: andar
    w.pad.state[0].rx = 32767;   // direito para a direita: virar
    w.pad.state[0].rt = 255;     // RT = R1 (gatilhos): atirar
    w.tick();
    CHECK(MapPlayerToSlot(0) == 0 && PlayerSetting[0].connected == 1);
    CHECK(w.a(kActForward) > 0.9f);
    CHECK(w.a(kActTurn) > 0.9f && w.a(kActAimX) > 0.9f);
    CHECK(Input_Action(0, kActFire, kActionPressed) != 0);
    CHECK(psiInput_ActiveDevice() == 1);
    // A = X do PS2: agachar; Y = triângulo: pular; LT = L1: mirar; os menus: X escolhe, triângulo volta.
    w.pad.state[0] = nf::PadState{};
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadA | nf::kPadY;
    w.pad.state[0].lt = 200;
    w.tick();
    CHECK(w.a(kActCrouch) == 1.0f && w.a(kActJump) == 1.0f && w.a(kActAimMode) == 1.0f);
    CHECK(w.a(31) == 1.0f && w.a(34) == 1.0f && w.a(kActMenuStart) == 1.0f);
    CHECK(w.a(kActFire) == 0.0f && w.a(kActForward) == 0.0f);
    // Com os ombros do PS2 (pad_shoulders = bumpers), RB é que atira.
    nf::controls().pad_triggers = false;
    w.pad.state[0] = nf::PadState{};
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadRB;
    w.tick();
    CHECK(w.a(kActFire) == 1.0f);
}

TEST(input_pad_style4_swaps_the_sticks) {
    InputWorld w;
    PlayerSetting[0].style = 4;  // Dr. No: o direito anda, o esquerdo olha; triângulo/X no zoom
    w.pad.state[0].connected = true;
    w.pad.state[0].ry = 32767;
    w.pad.state[0].lx = -32767;
    w.pad.state[0].buttons = nf::kPadY;
    w.tick();
    CHECK(w.a(kActForward) > 0.9f && w.a(kActTurn) < -0.9f);
    CHECK(w.a(kActDpadY) == -1.0f && w.a(11) == 1.0f);
    CHECK(w.a(kActJump) == 0.0f);  // no Dr. No pular é o direcional para cima
    w.pad.state[0] = nf::PadState{};
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadDpadUp;
    w.tick();
    CHECK(w.a(kActJump) == 1.0f);
    // GoldenEye (6): X do PS2 (A) atira, R2 mira (RB com os gatilhos como R1/L1), L1/R1 andam de lado.
    PlayerSetting[0].style = 6;
    w.pad.state[0].buttons = nf::kPadA | nf::kPadRB;
    w.pad.state[0].lt = 255;
    w.tick();
    CHECK(w.a(kActFire) == 1.0f && w.a(kActAimMode) == 1.0f && w.a(kActStrafe) > 0.9f);
}

TEST(input_pad_rebound_button_leaves_the_style) {
    InputWorld w;
    // A religado para atirar: deixa de agachar (estilo 0) e atira.
    nf::bind(nf::controls().bindings, find_cmd("fire"), nf::pad_input(nf::kPadInA));
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadA;
    w.tick();
    CHECK(w.a(kActFire) == 1.0f && w.a(kActCrouch) == 0.0f && w.a(31) == 0.0f);
    // A página de teclas mostra o que o estilo faz sem religar: atirar é R1 (RT com os gatilhos).
    const uint32_t fire0 = psi_pad_style_inputs(0, kActFire, 1, true);
    CHECK(fire0 == 1u << nf::kPadInRT);
    CHECK(psi_pad_style_inputs(0, kActForward, 1, true) == 1u << nf::kPadInLYPos);
    CHECK(psi_pad_style_inputs(4, kActForward, 1, true) == 1u << nf::kPadInRYPos);
    CHECK(psi_pad_style_inputs(6, kActFire, 1, true) == 1u << nf::kPadInA);
}

TEST(input_pad_deadzone_sensitivity_invert) {
    // A zona morta de psiInput_PollDevices: 40 de 127 no PS2.
    CHECK(psi_pad_deadzone(127, 40) == 127 && psi_pad_deadzone(127 + 39, 40) == 127 && psi_pad_deadzone(127 - 39, 40) == 127);
    CHECK(psi_pad_deadzone(255, 40) == 255);
    CHECK(psi_pad_deadzone(127 + 60, 40) > 127 && psi_pad_deadzone(127 + 60, 40) < psi_pad_deadzone(127 + 90, 40));
    CHECK(psi_pad_deadzone(200, 0) == 200);  // sem zona morta: o mesmo valor
    InputWorld w;
    // O controle em uso (L3 não faz nada no estilo 0).
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadLThumb;
    w.tick();
    w.pad.state[0].buttons = 0;
    // 25% para a frente: dentro da zona morta padrão (31%); com 10%, anda.
    w.pad.state[0].ly = int16_t(0.25f * 32767.0f);
    w.tick();
    CHECK(w.a(kActForward) == 0.0f);
    nf::controls().pad_deadzone = 0.1f;
    w.tick();
    CHECK(w.a(kActForward) > 0.1f);
    // Sensibilidade do controle: o olhar (meia volta do analógico) cresce com ela; a 0,5 não chega ao máximo.
    nf::controls().pad_deadzone = 40.0f / 127.0f;
    w.pad.state[0] = nf::PadState{};
    w.pad.state[0].connected = true;
    w.pad.state[0].rx = int16_t(0.8f * 32767.0f);
    w.tick();
    const float normal = w.a(kActTurn);
    nf::controls().pad_sensitivity = 2.0f;
    w.tick();
    const float fast = w.a(kActTurn);
    nf::controls().pad_sensitivity = 0.5f;
    w.pad.state[0].rx = 32767;
    w.tick();
    CHECK(normal > 0.0f && fast > normal && w.a(kActTurn) < 0.9f);
    // Inverter Y do controle é a opção do jogo (PlayerSetting::invert_look).
    nf::controls().pad_sensitivity = 1.0f;
    w.pad.state[0] = nf::PadState{};
    w.pad.state[0].connected = true;
    w.pad.state[0].ry = 32767;  // para cima: olhar para cima
    w.tick();
    const float up = w.a(kActLookY);
    PlayerSetting[0].invert_look = 1;
    w.tick();
    CHECK(up > 0.9f && w.a(kActLookY) < -0.9f);
    // O mouse: sensibilidade, a da mira (dividida pelo zoom) e a inversão.
    psiMouseSensitivity = 0.002f;
    float yaw = 0, pitch = 0;
    g_psi_keyboard_mouse.look_dx = 100.0f;
    g_psi_keyboard_mouse.look_dy = 50.0f;
    w.tick();
    psiInput_TakeMouseLook(0, &yaw, &pitch);
    CHECK_NEAR(yaw, -0.2, 1e-6);
    CHECK_NEAR(pitch, -0.1, 1e-6);
    nf::controls().mouse_aim_scale = 0.5f;
    nf::controls().mouse_invert_y = true;
    g_psi_keyboard_mouse.look_dx = 100.0f;
    g_psi_keyboard_mouse.look_dy = 50.0f;
    w.tick();
    psiInput_TakeMouseLook(0, &yaw, &pitch, true, 2.0f);
    CHECK_NEAR(yaw, -0.05, 1e-6);
    CHECK_NEAR(pitch, 0.025, 1e-6);
    psiMouseSensitivity = 0.0025f;
}

TEST(input_last_device_wins_for_sticks) {
    InputWorld w;
    w.pad.state[0].connected = true;
    w.pad.state[0].ly = 32767;
    // Teclado para trás e controle para a frente no mesmo tick: o teclado vence.
    w.keys({'S', 'E'});
    w.tick();
    CHECK(w.a(kActForward) == -1.0f && w.a(kActUse) == 1.0f && psiInput_ActiveDevice() == 0);
    // O controle mexe de novo (e aperta A): ele vence nos analógicos; os botões somam (E, ainda
    // seguro no teclado, continua usando; A agacha).
    w.pad.state[0].ly = 20000;
    w.pad.state[0].buttons = nf::kPadA;
    w.tick();
    CHECK(psiInput_ActiveDevice() == 1 && w.a(kActForward) > 0.0f);
    CHECK(w.a(kActUse) == 1.0f && w.a(kActCrouch) == 1.0f);
}

TEST(input_pad_disconnect_shows_the_game_warning) {
    InputWorld w;
    w.pad.state[0].connected = true;
    w.pad.state[0].buttons = nf::kPadA;
    w.tick();
    CHECK(PlayerSetting[0].connected == 1 && psiInput_ActiveDevice() == 1);
    // O controle em uso some: o jogador fica desconectado (o MenuManager_Monitor mostra o aviso).
    w.pad.state[0] = nf::PadState{};
    w.tick();
    CHECK(PlayerSetting[0].connected == 0 && MapPlayerToSlot(0) == -1);
    // Uma tecla devolve o jogador ao teclado.
    w.keys({'W'});
    w.tick();
    CHECK(PlayerSetting[0].connected == 1 && psiInput_ActiveDevice() == 0);
}

TEST(input_rumble_reaches_the_pad) {
    InputWorld w;
    w.pad.state[0].connected = true;
    w.tick();
    w.pad.vibrations.clear();
    GameState.unknown_51 = 1;
    PlayerSetting[0].unknown_09 = 1;
    Input_RumbleStart(0, 5, 40);
    CHECK(w.pad.vibrations.size() == 1);
    CHECK(w.pad.vibrations[0].pad == 0 && w.pad.vibrations[0].low == 100 * 257 && w.pad.vibrations[0].high == 0xffff);
    // A duração em ticks: desliga sozinha depois de 5.
    w.tick(5);
    CHECK(w.pad.vibrations.size() == 1);
    w.tick();
    CHECK(w.pad.vibrations.size() == 2 && w.pad.vibrations[1].low == 0 && w.pad.vibrations[1].high == 0);
    // Sem a opção de vibração, nada; força 0, nada; o jogo pausado desliga.
    PlayerSetting[0].unknown_09 = 0;
    Input_RumbleStart(0, 5, 40);
    PlayerSetting[0].unknown_09 = 1;
    Input_RumbleStart(0, 5, 0);
    CHECK(w.pad.vibrations.size() == 2);
    Input_RumbleStart(0, 60, 10);
    CHECK(w.pad.vibrations.size() == 3 && w.pad.vibrations[2].low == 25 * 257);
    GameState.paused = 1;
    w.tick();
    CHECK(w.pad.vibrations.size() == 4 && w.pad.vibrations[3].low == 0);
    // Input_RumbleStop pelo jogador.
    GameState.paused = 0;
    Input_RumbleStart(0, 60, 10);
    Input_RumbleStop(0);
    CHECK(w.pad.vibrations.back().low == 0 && w.pad.vibrations.back().high == 0);
}

TEST(input_change_controller_style) {
    InputWorld w;
    PlayerSetting[1].actions[3] = 0.5f;
    PlayerSetting[0].actions[3] = 0.5f;
    CHECK(Input_ChangeControllerStyle(0, 3) == 1 && PlayerSetting[0].style == 3);
    CHECK(PlayerSetting[0].actions[3] == 0.0f && PlayerSetting[1].actions[3] == 0.5f);
    PlayerSetting[0].actions[3] = 0.5f;
    CHECK(Input_ChangeControllerStyle(0, 3) == 1 && PlayerSetting[0].actions[3] == 0.5f);  // igual: não zera
    CHECK(Input_ChangeControllerStyle(4, 1) == 0);
    PlayerSetting[1].actions[3] = 0.0f;
}

TEST(input_capture_takes_the_next_new_key) {
    InputWorld w;
    w.keys({nf::key::Space});  // a tecla que escolheu a linha ainda está apertada
    w.tick();
    psiInput_BeginCapture();
    w.tick(3);
    CHECK(psiInput_CaptureState() == PsiCapture::Waiting);
    w.keys({});
    w.tick();
    w.keys({'F'});
    w.tick();
    nf::InputCode c;
    CHECK(psiInput_CaptureState() == PsiCapture::Got && psiInput_CaptureResult(&c) && c == nf::key_input('F'));
    // Enquanto F está apertada as ações ficam zeradas (não atira, não escolhe no menu).
    psiInput_EndCapture();
    w.tick();
    CHECK(w.a(kActFire) == 0.0f);
    w.keys({});
    w.tick(2);
    // Botão do controle e Esc.
    w.pad.state[0].connected = true;
    psiInput_BeginCapture();
    w.tick();
    w.pad.state[0].buttons = nf::kPadX;
    w.tick();
    CHECK(psiInput_CaptureResult(&c) && c == nf::pad_input(nf::kPadInX));
    psiInput_EndCapture();
    w.pad.state[0].buttons = 0;
    w.tick(2);
    psiInput_BeginCapture();
    w.tick();
    w.keys({nf::key::Escape});
    w.tick();
    CHECK(psiInput_CaptureState() == PsiCapture::Cancelled);
    psiInput_EndCapture();
}

TEST(input_command_labels_fit_the_game_fonts) {
    // Os nomes dos comandos e das entradas passam pela regra de pc_text: sem glifo, a letra sem acento
    // (nunca '?'); os nomes são únicos nos dois idiomas.
    const auto only_ascii = [](uint8_t c) { return c < 0x80; };
    for (int i = 0; i < nf::kBindCommandCount; ++i) {
        CHECK(PcText_ToGame(nf::kBindCommands[i].label_pt, only_ascii).find('?') == std::string::npos);
        CHECK(PcText_ToGame(nf::kBindCommands[i].label_en, only_ascii).find('?') == std::string::npos);
        for (int k = i + 1; k < nf::kBindCommandCount; ++k) {
            CHECK(std::strcmp(nf::kBindCommands[i].label_pt, nf::kBindCommands[k].label_pt) != 0);
            CHECK(std::strcmp(nf::kBindCommands[i].key, nf::kBindCommands[k].key) != 0);
        }
    }
    CHECK(PcText_ToGame("Passo à esquerda", only_ascii) == "Passo a esquerda");
    for (uint8_t code = 0; code < nf::kPadInCount; ++code) {
        if (code == 10 || code == 11) continue;
        CHECK(PcText_ToGame(nf::input_label(nf::pad_input(code), false).c_str(), only_ascii).find('?') == std::string::npos);
    }
}

// ---- menus ----
namespace {

// Um arquivo de menu mínimo (docs/formats.md, "Menus (item tipo 8)"): uma página com a lista 10 do painel
// da pausa (id 0x10000028), no menu 0x80000002.
struct MiniMenu {
    std::vector<uint8_t> b;
    void dw(uint32_t v) {
        for (int k = 0; k < 4; ++k) b.push_back(uint8_t(v >> (8 * k)));
    }
    void w(uint16_t v) {
        b.push_back(uint8_t(v));
        b.push_back(uint8_t(v >> 8));
    }
    void control(uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t ww, int16_t h, uint32_t tag, uint32_t a = 0) {
        dw(0xfffffff9);
        dw(id);
        b.push_back(type);
        b.push_back(0);
        for (int16_t v : {x, y, ww, h}) w(uint16_t(v));
        w(20);
        dw(tag);
        w(1);
        dw(a);
        dw(0);
    }
    MiniMenu() {
        dw(0xfffffffa);
        dw(0xfffffff0);
        dw(0x4000004b);
        dw(0x80000002);
        dw(0);
        b.push_back(0);
        const size_t at = b.size();
        dw(0);
        control(0x4000004b, MCT_WINDOW, 0, 0, 640, 480, 0xf);
        control(0x10000028, MCT_LIST, 94, 185, 465, 153, 10, 1);
        const uint32_t size = uint32_t(b.size() - at - 4);
        std::memcpy(b.data() + at, &size, 4);
        dw(0xfffffff2);
        dw(0);
    }
};

struct MenuInputWorld : InputWorld {
    MiniMenu file;
    MenuInputWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        txt_debug_power_on();
        sprite_debug_power_on();
        Sprite_InitLists();
        text_debug_power_on();
        Text_Init(0);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        menu_pc_debug_power_on();
        std::memset(MPSettings, 0, sizeof(MPSettings));
        const uint32_t one_player = 1;
        std::memcpy(MPSettings + 0x1ac, &one_player, 4);
        std::memset(MPGame, 0, sizeof(MPGame));
        for (obj_tag*& p : glb_players) p = nullptr;
        GameState.level_file = 0x7000012;
        MenuManager_Init();
        MenuManager_Load(0x8000001, file.b.data());
    }
    ~MenuInputWorld() {
        for (uint8_t i = 0; i < kMenuManagers; ++i) MenuManager_Delete(i);
        menu_debug_power_on();
        menu_handlers_debug_power_on();
        menu_pc_debug_power_on();
        text_debug_power_on();
        sprite_debug_power_on();
        txt_debug_power_on();
        GameState.paused = 0;
        while (GameFlow_Depth() > 0) GameFlow_PopState();
    }
    void menu_tick(std::initializer_list<int> vks = {}, int n = 1) {
        for (int i = 0; i < n; ++i) {
            keys(vks);
            Input_Update();
            MenuManager_Update();
            MenuManager_Monitor();
        }
    }
    M_LISTBOX* list10() {
        M_PAGE* p = menu_manager[0].page;
        for (M_CONTROL* c = static_cast<M_CONTROL*>(ll_first(p->controls)); c != nullptr; c = c->next) {
            if (c->id == 0x10000028 && c->tag == 10) return reinterpret_cast<M_LISTBOX*>(c);
        }
        return nullptr;
    }
};

}  // namespace

TEST(menu_controller_style_list_fills_the_pause_tab) {
    MenuInputWorld w;
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    M_LISTBOX* l = w.list10();
    CHECK(l != nullptr);
    Menu_DisplayControllerStyleList(0, 0);
    // Seis linhas de dois botões e uma de um; duas colunas de 52%; os símbolos da fonte na frente (sem o
    // arquivo de texto, os rótulos ficam vazios).
    CHECK(List_SendMessage(l, 0x33, 0, 0) == 7 && l->columns == 2 && l->widths[0] == 0x34 && l->widths[1] == 0x34);
    LIST_ROW* r = static_cast<LIST_ROW*>(ll_first(l->rows));
    CHECK(std::strcmp(r->cells[0].text, "~A ") == 0 && std::strcmp(r->cells[1].text, "~B ") == 0);
    LIST_ROW* last = static_cast<LIST_ROW*>(List_SetIndex(l, &l->rows, 6));
    CHECK(last != nullptr && std::strcmp(last->cells[0].text, "~T ") == 0 && last->cells[1].text[0] == 0);
    // Outro estilo refaz a lista; um estilo inválido só a limpa.
    Menu_DisplayControllerStyleList(0, 7);
    CHECK(List_SendMessage(l, 0x33, 0, 0) == 7);
    Menu_DisplayControllerStyleList(0, 8);
    CHECK(List_SendMessage(l, 0x33, 0, 0) == 0);
}

namespace {
// Uma fonte só com ASCII nas três entradas de FontTable (como em test_pc_options.cpp): a lista de teclas mede
// as linhas com ela (sem fonte, a altura da linha seria 0).
struct AsciiFont {
    CHARDATA_tag chars[3];
    FONT_tag font;
    pstex_tag rec;
    maptex_tag tex;
    AsciiFont() {
        font_debug_power_on();
        std::memset(FontTable, 0, sizeof(FontTable));
        const uint16_t codes[3] = {'!', 'A', 'a'};
        for (int i = 0; i < 3; ++i) {
            chars[i] = CHARDATA_tag{};
            chars[i].code = codes[i];
            chars[i].u = uint16_t(i * 10);
            chars[i].w = 8;
            chars[i].h = 10;
            chars[i].post = 1;
        }
        font = FONT_tag{0, nullptr, 0x21, 0xff, 3, chars, 6.0f, 4.0f};
        for (int k = 0; k < 3; ++k) FontTable[k] = {0x03000998, &font};
        rec = pstex_tag{};
        tex = maptex_tag{&rec, 256, 64};
        hashtable_additem(0x03000998, &tex);
    }
    ~AsciiFont() {
        font_debug_power_on();
        std::memset(FontTable, 0, sizeof(FontTable));
    }
};
}  // namespace

TEST(menu_pc_controls_tabs_and_rebinding) {
    MenuInputWorld w;
    AsciiFont ascii;
    MenuPc_Install();
    MenuPcControls_Register();
    psiMouseSensitivity = 0.0022f;
    PcText_SetLanguage("en");  // o padrão: o texto dentro do jogo é em inglês
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    // A página de opções direto na aba CONTROLES (a de --menu-tab), com o foco na aba.
    CHECK(MenuPc_ShowTab(0, PcTab::Controls) && Menu_CurrentPage(0) == kPcOptionsPage);
    CHECK(MenuPc_CurrentTab() == PcTab::Controls);
    w.menu_tick();
    uint32_t id = 0, tag = 0;
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcTabFirst + uint32_t(PcTab::Controls));
    // Baixo: a primeira linha com foco (o aparelho é só informação): a sensibilidade do mouse.
    w.menu_tick({nf::key::Down});
    w.menu_tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcRowMouseSens);
    // A seta para a direita aumenta a sensibilidade do mouse (um passo da barra logarítmica: x 2^(1/8)); o nome da linha
    // mostra o valor e os centímetros por volta a 800 DPI.
    w.menu_tick({nf::key::Right});
    w.menu_tick();
    CHECK_NEAR(psiMouseSensitivity, 0.0022 * nf::fm::pow(2.0, 1.0 / 8.0), 1e-7);
    CHECK(MenuPc_Slider(0, kPcRowMouseSens) == nf::kMouseSensDefaultPos + 1);
    // Para baixo até a inversão do Y do controle e Espaço troca (o PlayerSetting do jogo).
    for (int i = 0; i < 5; ++i) {
        w.menu_tick({nf::key::Down});
        w.menu_tick();
    }
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcRowPadInvert);
    w.menu_tick({nf::key::Space});
    w.menu_tick();
    CHECK(PlayerSetting[0].invert_look == 1 && nf::controls().pad_invert_y);
    // Nove linhas: as de baixo rolam para a área (o aparelho, a primeira, some).
    M_CONTROL* device = MenuPc_Control(0, kPcRowDevice);
    CHECK(device != nullptr && device->state == 2);
    w.menu_tick({nf::key::Down});
    w.menu_tick();
    w.menu_tick({nf::key::Down});
    w.menu_tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcRowTriggers && (device->state & 1) != 0);
    // TECLAS: a lista dos comandos com o teclado/mouse e o controle, e a linha de restaurar.
    CHECK(MenuPc_ShowTab(0, PcTab::Keys) && MenuPc_CurrentTab() == PcTab::Keys);
    w.menu_tick();
    M_LISTBOX* list = reinterpret_cast<M_LISTBOX*>(MenuPc_Control(0, kPcKeysList));
    CHECK(list != nullptr && list->base.state == 0 && List_SendMessage(list, 0x33, 0, 0) == nf::kBindCommandCount + 1);
    CHECK((device->state & 1) != 0);  // as linhas de CONTROLES somem
    const int fire = nf::find_bind_command("fire");
    LIST_ROW* row = List_SetIndex(list, &list->rows, fire);
    CHECK(row != nullptr && std::strcmp(row->cells[1].text, "Mouse 1") == 0);
    LIST_ROW* first = List_SetIndex(list, &list->rows, 0);
    CHECK(first != nullptr && std::strcmp(first->cells[0].text, "Move forward") == 0);
    // Baixo desce para a lista; a roda do mouse sobre ela anda até atirar.
    w.menu_tick({nf::key::Down});
    w.menu_tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcKeysList);
    const Psi2DViewport vp = psi_2d_viewport(640, 480);
    const auto mouse = [&](float x, float y) {
        g_psi_keyboard_mouse.view_width = 640;
        g_psi_keyboard_mouse.view_height = 480;
        g_psi_keyboard_mouse.cursor_inside = true;
        g_psi_keyboard_mouse.cursor_x = vp.left + x * vp.scale_x;
        g_psi_keyboard_mouse.cursor_y = vp.top + y * vp.scale_y;
    };
    const float lx = float(list->base.x) + 40.0f;
    mouse(lx, float(list->base.y) + list->row_height * 0.5f);
    w.menu_tick();  // o mouse entra (passa por cima da primeira linha)
    CHECK(List_SendMessage(list, 0x34, 0, 0) == 0);
    g_psi_keyboard_mouse.wheel = -float(fire);
    w.menu_tick();
    CHECK(List_SendMessage(list, 0x35, 0, 0) == fire);
    // Clicar na linha escolhida espera a tecla; soltar, apertar F: atirar fica com F e o mouse.
    const float row_y = float(list->base.y) + (float(fire - list->top) + 0.5f) * list->row_height;
    mouse(lx, row_y);
    w.menu_tick({nf::key::LButton});
    CHECK(psiInput_CaptureState() == PsiCapture::Waiting);
    CHECK(std::string(MenuPcControls_Status()) == "Fire: press a key or button (Esc cancels)");
    w.menu_tick({}, 2);
    w.menu_tick({'F'});
    w.menu_tick({}, 2);
    CHECK(nf::controls().bindings.in[fire][0] == nf::key_input('F'));
    CHECK(std::strcmp(row->cells[1].text, "F, Mouse 1") == 0);
    CHECK(std::string(MenuPcControls_Status()) == "F -> Fire");
    // Em português (language_pc = pt), os nomes trocam.
    PcText_SetLanguage("pt");
    w.menu_tick({}, 2);
    CHECK(std::strcmp(first->cells[0].text, "Andar para a frente") == 0);
    PcText_SetLanguage("en");
    w.menu_tick({}, 2);
    // F não escolheu nada no menu nem fechou (ainda na aba de teclas).
    CHECK(Menu_CurrentPage(0) == kPcOptionsPage && MenuPc_CurrentTab() == PcTab::Keys);
    // De cima da primeira linha, sobe para a aba; voltar (Backspace) volta à pausa.
    List_SendMessage(list, 0x1d, 0, 0);
    w.menu_tick({nf::key::Up});
    w.menu_tick();
    w.menu_tick({nf::key::Up});
    w.menu_tick();
    CHECK(Menu_Focus(0, &id, &tag) && id == kPcTabFirst + uint32_t(PcTab::Keys));
    w.menu_tick({nf::key::Back});
    w.menu_tick({}, 3);
    CHECK(Menu_CurrentPage(0) == 0x4000004b);
    psiMouseSensitivity = 0.0025f;
}

TEST(menu_mouse_clicks_stay_out_of_the_game) {
    MenuInputWorld w;
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    w.menu_tick();
    // Com o menu aberto, os botões do mouse são só do menu: nada de atirar (ação 9) nem mirar (19).
    w.menu_tick({nf::key::LButton, nf::key::RButton});
    CHECK(w.a(9) == 0.0f && w.a(19) == 0.0f);
    // O menu fecha com o botão ainda segurado: ele fica de fora até ser solto.
    MenuManager_Delete(0);
    w.menu_tick({nf::key::LButton});
    CHECK(w.a(9) == 0.0f);
    w.menu_tick({nf::key::LButton});
    CHECK(w.a(9) == 0.0f);
    w.menu_tick({});
    w.menu_tick({nf::key::LButton});
    CHECK(w.a(9) == 1.0f);
    // A roda num menu também é só dele: com a próxima arma na roda, ela não troca de arma; fora do menu, troca.
    const int next = find_cmd("weapon_next");
    nf::bind(nf::controls().bindings, next, nf::wheel_input(false));
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    w.menu_tick({});
    g_psi_keyboard_mouse.wheel = 1.0f;
    w.menu_tick({});
    CHECK(w.a(16) == 0.0f);
    MenuManager_Delete(0);
    w.menu_tick({});
    g_psi_keyboard_mouse.wheel = 1.0f;
    w.menu_tick({});
    CHECK(w.a(16) == 1.0f);
}

TEST(menu_escape_backs_and_closes_the_pause_without_reopening) {
    MenuInputWorld w;
    MenuPc_Install();  // o P_PAUSE_Handler do PC: voltar no topo fecha a pausa
    g_menu_pc_stats = MenuPcStats{};
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    GS_PauseGame(1);
    w.menu_tick({}, 2);
    // Com o menu aberto o Esc é só o voltar (o triângulo, 34): o Start (30) e o 25 não ligam junto (Menu_InputAction
    // anularia o voltar com o Start).
    w.keys({nf::key::Escape});
    Input_Update();
    CHECK(w.a(34) == 1.0f && w.a(kActPause) == 0.0f && w.a(kActMenuStart) == 0.0f);
    // No topo da pausa, voltar fecha a pausa como o Start (só do PC; no PS2 nada acontece).
    MenuManager_Update();
    MenuManager_Monitor();
    CHECK(MenuManager_Exists(0) == 0 && GameState.paused == 0 && g_menu_pc_stats.back_closes == 1);
    // O Esc ainda apertado não reabre a pausa: fica fora do Start até ser solto.
    w.menu_tick({nf::key::Escape}, 3);
    CHECK(w.a(kActPause) == 0.0f && w.a(34) == 1.0f);
    w.menu_tick({});
    w.menu_tick({nf::key::Escape});
    CHECK(Input_Action(0, kActPause, kActionPressed) != 0);
    w.menu_tick({});
    // O Backspace (também voltar) fecha do mesmo jeito; o Enter (Start) fecha pelo MenuManager_Monitor, como no jogo.
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    w.menu_tick({}, 2);
    w.menu_tick({nf::key::Back});
    CHECK(MenuManager_Exists(0) == 0 && g_menu_pc_stats.back_closes == 2);
    w.menu_tick({});
    CHECK(MenuManager_Create(0x80000002u, 0x4000004b, 0, 0, 0x1e, 0, 3) == 0);
    w.menu_tick({}, 2);
    w.menu_tick({nf::key::Return});
    CHECK(MenuManager_Exists(0) == 0 && g_menu_pc_stats.back_closes == 2);
    w.menu_tick({});
}

TEST(input_ready_unfreezes_clears_and_makes_the_combo_list) {
    // Input_Ready (a cada carga, depois do Mem_Init): descongela, zera as ações de todos e refaz a lista das
    // combinações (10 nós zerados de 0x18 bytes no heap, nenhum em uso).
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    FreezeGame = 1;
    for (PlayerInput_tag& p : PlayerSetting) p.actions[3] = 1.0f;
    Input_Ready();
    CHECK(FreezeGame == 0);
    for (const PlayerInput_tag& p : PlayerSetting) CHECK(p.actions[3] == 0.0f);
    CHECK(ComboKeysList.free.count == 10 && ComboKeysList.free.node_size == 0x18);
    CHECK(ComboKeysList.used.count == 0 && ComboKeysList.used.first == nullptr && ComboKeysList.used.node_size == 0x18);
    int nodes = 0;
    bool zeroed = true;
    for (LLNODE_tag* n = ComboKeysList.free.first; n != nullptr; n = n->next) {
        ++nodes;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(n);
        for (int i = 8; i < 0x18; ++i) zeroed = zeroed && b[i] == 0;  // depois dos ponteiros do nó
    }
    CHECK(nodes == 10 && zeroed);
    // Uma segunda carga (o heap reiniciado de novo) refaz a lista do zero.
    void* taken = DList_MoveFromFree2InUse(&ComboKeysList);
    CHECK(taken != nullptr && ComboKeysList.used.count == 1);
    Mem_Init();
    Input_Ready();
    CHECK(ComboKeysList.free.count == 10 && ComboKeysList.used.count == 0);
}
