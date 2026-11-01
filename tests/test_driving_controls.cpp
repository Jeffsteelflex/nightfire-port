// Os controles de PC das missões de carro (docs/driving.md seção 33; platform/drive_controls.h): o teclado e o mouse no
// controle do PS2 por modo, a troca de aparelho, a roda, a mira pelo mouse (e presa aos limites do braço do AutoDrive),
// a leitura do .ini. Propriedades escritas à mão a partir da tabela da seção 33 e do libpad (os bits L2 0 ... esquerda
// 15), não da implementação.

#include <cmath>
#include <memory>
#include <set>

#include "driving/camera/camera_ini.h"
#include "driving/camera/player_camera.h"
#include "platform/drive_controls.h"
#include "platform/gamepad.h"
#include "platform/keys.h"
#include "platform/settings.h"
#include "test_framework.h"

using namespace nf;

namespace {

// Os bits do libpad (SCE_PAD*): L2 0, R2 1, L1 2, R1 3, triângulo 4, bola 5, X 6, quadrado 7, Select 8, L3 9, R3 10,
// Start 11, cima 12, direita 13, baixo 14, esquerda 15.
constexpr uint16_t kL2 = 1u << 0, kL1 = 1u << 2, kR1 = 1u << 3, kTri = 1u << 4, kCircle = 1u << 5, kCross = 1u << 6,
                   kSquare = 1u << 7, kStart = 1u << 11, kUp = 1u << 12, kRight = 1u << 13, kDown = 1u << 14,
                   kLeft = 1u << 15;

DriveFrame keys_frame(std::set<int> held, int32_t clock = 0) {
    DriveFrame f;
    auto keys = std::make_shared<std::set<int>>(std::move(held));
    f.key_down = [keys](int k) { return keys->count(k) != 0; };
    f.clock = clock;
    return f;
}

Ps2PadFrame press(DriveController& c, std::set<int> held, DriveMode m) { return c.update(keys_frame(std::move(held)), m); }

}  // namespace

TEST(drive_controls_keyboard_follows_the_mode) {
    DriveController c;
    // Dirigindo: W o X (o acelerador do drivecfg.def), com a pressão inteira; o direcional no centro.
    Ps2PadFrame p = press(c, {'W'}, DriveMode::Drive);
    CHECK(p.buttons == kCross);
    CHECK(p.pressure[6] == 255);
    CHECK(p.lx == 128 && p.ly == 128);
    // S o quadrado, espaço a bola, A o direcional esquerdo à esquerda, D à direita, os dois juntos no centro.
    CHECK(press(c, {'S'}, DriveMode::Drive).buttons == kSquare);
    CHECK(press(c, {key::Space}, DriveMode::Drive).buttons == kCircle);
    CHECK(press(c, {'A'}, DriveMode::Drive).lx == 0);
    CHECK(press(c, {'D'}, DriveMode::Drive).lx == 255);
    CHECK(press(c, {'A', 'D'}, DriveMode::Drive).lx == 128);
    CHECK(press(c, {'F'}, DriveMode::Drive).buttons == kTri);
    CHECK(press(c, {'C'}, DriveMode::Drive).buttons == kL2);
    // Mirando (o povcfg0.def): W e S o direcional esquerdo em y (para cima é 0), sem botão; A/D o x.
    p = press(c, {'W'}, DriveMode::Aim);
    CHECK(p.buttons == 0 && p.ly == 0 && p.lx == 128);
    CHECK(press(c, {'S'}, DriveMode::Aim).ly == 255);
    p = press(c, {'W', 'D'}, DriveMode::AimZoom);
    CHECK(p.ly == 0 && p.lx == 255 && p.buttons == 0);
    CHECK(press(c, {'R'}, DriveMode::Aim).buttons == kSquare);  // POVRELOAD
    CHECK(press(c, {'R'}, DriveMode::Drive).buttons == 0);
    CHECK(press(c, {key::Space}, DriveMode::Aim).buttons == 0);  // sem freio de mão na mira
    // No menu: WASD e as setas o D-pad, Enter e espaço o X (aceitar), Backspace o triângulo, Esc o Start.
    CHECK(press(c, {'W'}, DriveMode::Menu).buttons == kUp);
    CHECK(press(c, {key::Down}, DriveMode::Menu).buttons == kDown);
    CHECK(press(c, {'A'}, DriveMode::Menu).buttons == kLeft);
    CHECK(press(c, {key::Return}, DriveMode::Menu).buttons == kCross);
    CHECK(press(c, {key::Space}, DriveMode::Menu).buttons == kCross);
    CHECK(press(c, {key::Back}, DriveMode::Menu).buttons == kTri);
    CHECK(press(c, {key::Escape}, DriveMode::Menu).buttons == kStart);
    // Fora do menu, Enter e Esc são o Start (a pausa do HUD).
    CHECK(press(c, {key::Return}, DriveMode::Drive).buttons == kStart);
    CHECK(press(c, {key::Escape}, DriveMode::Aim).buttons == kStart);
}

TEST(drive_controls_mouse_buttons_need_the_capture) {
    DriveController c;
    DriveFrame f = keys_frame({key::LButton});
    // Sem o mouse capturado, os botões não valem.
    f.mouse_captured = false;
    CHECK(c.update(f, DriveMode::Drive).buttons == 0);
    // O clique que captura fica fora até ser solto.
    f.mouse_captured = true;
    CHECK(c.update(f, DriveMode::Drive).buttons == 0);
    CHECK(c.update(keys_frame({}), DriveMode::Drive).buttons == 0);
    // Depois: o esquerdo o R1 (a arma), o direito o L1 (o aparelho / a luneta), dirigindo e mirando; no menu nada.
    CHECK(press(c, {key::LButton}, DriveMode::Drive).buttons == kR1);
    CHECK(press(c, {key::RButton}, DriveMode::Drive).buttons == kL1);
    CHECK(press(c, {key::LButton}, DriveMode::Aim).buttons == kR1);
    CHECK(press(c, {key::RButton}, DriveMode::AimZoom).buttons == kL1);
    c.update(keys_frame({}), DriveMode::Drive);
    CHECK(press(c, {key::LButton}, DriveMode::Menu).buttons == 0);
    // Saindo do menu com o botão seguro: fora até soltar (o clique do menu não atira).
    CHECK(press(c, {key::LButton}, DriveMode::Drive).buttons == 0);
    c.update(keys_frame({}), DriveMode::Drive);
    CHECK(press(c, {key::LButton}, DriveMode::Drive).buttons == kR1);
}

TEST(drive_controls_wheel_notches_are_separate_presses) {
    DriveController c;
    // Mirando sem a luneta: para baixo a próxima arma (o D-pad à direita), dois entalhes, dois apertos com um quadro
    // solto entre eles, cada um de kDriveWheelPulseTicks ticks.
    DriveFrame f = keys_frame({}, 100);
    f.wheel = -2;
    std::vector<uint16_t> seen;
    for (int32_t t = 100; t < 100 + 3 * kDriveWheelPulseTicks + 4; ++t) {
        DriveFrame g = keys_frame({}, t);
        if (t == 100) g.wheel = -2;
        seen.push_back(c.update(g, DriveMode::Aim).buttons);
    }
    int presses = 0, held = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i] == 0 || seen[i] == kRight);
        if (seen[i] == kRight) ++held;
        if (seen[i] == kRight && (i == 0 || seen[i - 1] == 0)) ++presses;
    }
    CHECK(presses == 2);
    CHECK(held == 2 * kDriveWheelPulseTicks);
    CHECK(c.wheel_notches == 2);
    // Com a luneta fechada a roda aproxima (para cima: o D-pad para cima) e afasta.
    DriveController z;
    DriveFrame u = keys_frame({}, 0);
    u.wheel = 1;
    CHECK(z.update(u, DriveMode::AimZoom).buttons == kUp);
    DriveController d;
    DriveFrame w = keys_frame({}, 0);
    w.wheel = 1;
    CHECK(d.update(w, DriveMode::Drive).buttons == kLeft);  // dirigindo: a arma anterior
}

TEST(drive_controls_mouse_aims_only_while_aiming) {
    DriveController c;
    c.mouse.sensitivity = 0.002f;
    DriveFrame f = keys_frame({});
    f.mouse_dx = 100.0f;
    f.mouse_dy = -50.0f;  // o mouse para cima (o y da tela para baixo)
    c.update(f, DriveMode::Aim);
    CHECK_NEAR(c.aim_yaw, 0.2f, 1e-6f);
    CHECK_NEAR(c.aim_pitch, 0.1f, 1e-6f);  // para cima é positivo
    c.mouse.invert_y = true;
    c.update(f, DriveMode::AimZoom);
    CHECK_NEAR(c.aim_pitch, -0.1f, 1e-6f);
    c.update(f, DriveMode::Drive);
    CHECK(c.aim_yaw == 0.0f && c.aim_pitch == 0.0f);
    f.mouse_captured = false;
    c.update(f, DriveMode::Aim);
    CHECK(c.aim_yaw == 0.0f && c.aim_pitch == 0.0f);
}

TEST(drive_controls_sticks_follow_the_last_device_and_buttons_add_up) {
    DriveController c;
    PadState pad;
    pad.connected = true;
    // O controle mexido: o aparelho é ele, e os direcionais são os dele.
    pad.lx = 32767;
    DriveFrame f = keys_frame({});
    f.pad = &pad;
    Ps2PadFrame p = c.update(f, DriveMode::Drive);
    CHECK(c.device() == DriveDevice::Pad);
    CHECK(p.lx == 255);
    // Uma tecla nova: o teclado volta a mandar nos direcionais (A: à esquerda), o direcional do controle fica de fora.
    DriveFrame g = keys_frame({'A'});
    g.pad = &pad;
    p = c.update(g, DriveMode::Drive);
    CHECK(c.device() == DriveDevice::KeyboardMouse);
    CHECK(p.lx == 0);
    // Os botões dos dois somam: o A do controle (o X do PS2) com o botão esquerdo (o R1) dirigindo.
    pad.lx = 0;
    pad.buttons = kPadA;
    DriveFrame h = keys_frame({key::LButton});
    h.pad = &pad;
    c.update(keys_frame({}), DriveMode::Drive);
    p = c.update(h, DriveMode::Drive);
    CHECK((p.buttons & (kCross | kR1)) == (kCross | kR1));
    CHECK(p.pressure[6] == 255 && p.pressure[9] == 255);
    // O controle desligado: volta ao teclado.
    PadState off;
    DriveFrame k = keys_frame({});
    k.pad = &off;
    c.update(k, DriveMode::Drive);
    CHECK(c.device() == DriveDevice::KeyboardMouse);
    // Um controle ligado com o jogo aberto passa a valer no primeiro botão.
    pad.buttons = kPadB;
    DriveFrame l = keys_frame({});
    l.pad = &pad;
    p = c.update(l, DriveMode::Drive);
    CHECK(c.device() == DriveDevice::Pad);
    CHECK(p.buttons == kCircle);
}

TEST(drive_controls_read_the_ini) {
    Settings s;
    s.parse(
        "[driving_bindings]\n"
        "fire = mouse2, pad_a\n"
        "handbrake =\n"
        "pause = p\n"
        "[controls]\n"
        "mouse_sensitivity = 0.003\n"
        "mouse_invert_y = 1\n");
    DriveBindings b;
    load_drive_bindings(s, &b);
    InputCode rmb, lmb, space, w, p, esc;
    CHECK(parse_input_name("mouse2", &rmb) && parse_input_name("mouse1", &lmb) && parse_input_name("space", &space));
    CHECK(parse_input_name("w", &w) && parse_input_name("p", &p) && parse_input_name("escape", &esc));
    CHECK(b.has(kDrvFire, rmb) && !b.has(kDrvFire, lmb));  // a do controle fica de fora
    CHECK(!b.in[kDrvFire][1].valid());
    CHECK(!b.has(kDrvHandbrake, space) && !b.in[kDrvHandbrake][0].valid());
    CHECK(b.has(kDrvAccelerate, w));  // ausente: o padrão
    CHECK(b.has(kDrvPause, p) && !b.has(kDrvPause, esc));
    DriveMouseConfig m = load_drive_mouse(s);
    CHECK_NEAR(m.sensitivity, 0.003f, 1e-7f);  // o do jogo a pé, sem [driving]
    CHECK(m.invert_y);
    s.parse("[driving]\nmouse_sensitivity = 0.001\nmouse_invert_y = 0\n[controls]\nmouse_sensitivity = 0.003\n");
    m = load_drive_mouse(s);
    CHECK_NEAR(m.sensitivity, 0.001f, 1e-7f);
    CHECK(!m.invert_y);
    // Os padrões: cada comando com alguma tecla, e o tiro no botão esquerdo.
    const DriveBindings d = default_drive_bindings();
    for (int i = 0; i < kDriveCommandCount; ++i) CHECK(d.in[i][0].valid());
    CHECK(d.has(kDrvFire, lmb));
    // Com a tabela mudada, o botão direito atira e o freio de mão some.
    DriveController c;
    c.bindings = b;
    c.update(keys_frame({}), DriveMode::Drive);
    CHECK(press(c, {key::RButton}, DriveMode::Drive).buttons == kR1);
    CHECK(press(c, {key::Space}, DriveMode::Drive).buttons == 0);
}

TEST(drive_mouse_aim_turns_the_autodrive_aim_within_the_arm_limits) {
    using drv::AutoDriveArmInfo;
    using drv::PlayerCamera;
    auto cam = std::make_unique<PlayerCamera>();
    AutoDriveArmInfo arm{};
    // Um braço com um trecho: rumo de -0,1 a 0,1 volta, inclinação pelo seno de -0,5 a 0,5 (30 graus).
    arm.min_yaw[0] = -0.1f;
    arm.max_yaw[0] = 0.1f;
    arm.min_pitch[0] = -0.5f;
    arm.max_pitch[0] = 0.5f;
    cam->d.fov = 33.0f;
    // Sem pedido, nada muda.
    cam->d.ad_yaw = 0.01f;
    cam->d.ad_pitch = 0.02f;
    cam->ApplyMouseAim(arm, 33.0f);
    CHECK(cam->d.ad_yaw == 0.01f && cam->d.ad_pitch == 0.02f && cam->mouse_aim_stats.applied == 0);
    // Um pedido pequeno anda o rumo e diminui a inclinação (para cima), vezes o fov sobre o padrão.
    cam->d.ad_yaw = 0.0f;
    cam->d.ad_pitch = 0.0f;
    cam->AddMouseAim(0.01f, 0.02f);
    cam->ApplyMouseAim(arm, 33.0f);
    CHECK_NEAR(cam->d.ad_yaw, 0.01f, 1e-6f);
    CHECK_NEAR(cam->d.ad_pitch, -0.02f, 1e-6f);
    cam->d.fov = 16.5f;  // a luneta fechada: a metade
    cam->AddMouseAim(0.01f, 0.0f);
    cam->ApplyMouseAim(arm, 33.0f);
    CHECK_NEAR(cam->d.ad_yaw, 0.015f, 1e-6f);
    // Pedidos grandes param nos limites do braço.
    const float pitch_lim = std::asin(0.5f) / 6.2831853f;
    for (int i = 0; i < 4; ++i) {
        const float dy = (i & 1) ? -1.0f : 1.0f, dp = (i & 2) ? -1.0f : 1.0f;
        cam->AddMouseAim(dy, dp);
        cam->ApplyMouseAim(arm, 33.0f);
        CHECK(cam->d.ad_yaw >= -0.1f - 1e-5f && cam->d.ad_yaw <= 0.1f + 1e-5f);
        CHECK(cam->d.ad_pitch >= -pitch_lim - 1e-4f && cam->d.ad_pitch <= pitch_lim + 1e-4f);
        CHECK_NEAR(std::fabs(cam->d.ad_yaw), 0.1f, 1e-4f);
    }
    CHECK(cam->mouse_aim_stats.limited >= 4);
}
