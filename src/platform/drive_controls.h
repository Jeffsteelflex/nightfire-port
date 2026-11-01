#pragma once
// Os controles de PC das missões de carro (só do port; docs/driving.md seção 33). O DRIVING.ELF só lê o controle do PS2
// (o PS2PadDevice: 16 botões com pressão e os dois direcionais); aqui o teclado, o mouse e o controle do PC viram esse
// controle a cada quadro, como o controle virtual do jogo a pé (platform/psi_pad.h), e o mouse também gira a mira da
// seção do helicóptero direto, em radianos (a câmera do AutoDrive, RPlayerCamera; driving/camera/player_camera.h).
//
// Neutro e sem nada do Nightfire além do controle do PS2: o nf_driving o usa hoje, o nightfire.exe quando as missões de
// carro entrarem no jogo.
//
// A tabela (kDriveCommands): cada comando de PC tem até kMaxKeyBinds entradas do teclado, do mouse ou da roda (os nomes de
// nf::input_name) e um alvo no controle do PS2 por modo: dirigindo (Drive: a configuração do drivecfg.def), mirando
// (Aim: a do povcfg0.def, a da direção automática), mirando com a luneta fechada (AimZoom) e no menu de pausa (Menu: o
// FrontEnd.def). Uma entrada que está em vários comandos vale, em cada modo, para o primeiro da tabela com alvo nesse
// modo (Enter é o aceitar no menu e o Start fora dele; a roda é o zoom com a luneta e a troca de arma sem ela).
//
// Aparelho: os direcionais vêm do último aparelho usado (uma tecla ou botão do mouse ligado, o mouse mexido, a roda;
// ou um botão, um gatilho ou um direcional novo do controle), os botões dos dois somam (OU), como psiInput_ActiveDevice
// do jogo a pé. O controle do PC entra pela posição dos botões (A = X, B = bola, X = quadrado, Y = triângulo, LB/RB =
// L1/R1, LT/RT = L2/R2, Back = Select, os direcionais e o D-pad).
//
// nightfire.ini (ou o arquivo de --config): [driving_bindings] uma chave por comando (`fire = mouse1, ctrl`; vazio:
// nenhuma; ausente: o padrão; entradas do controle são ignoradas) e [driving] mouse_sensitivity (radianos por contagem;
// sem ela, a [controls] mouse_sensitivity do jogo a pé; sem as duas, kDefaultMouseSensitivity) e mouse_invert_y (sem
// ela, a [controls] mouse_invert_y).

#include <cstdint>
#include <functional>
#include <string>

#include "platform/input_bindings.h"

namespace nf {

class Settings;
struct PadState;

// O controle do PS2 de um quadro, como o PS2PadDevice o lê (o drv::PadData): os bits do libpad, as pressões (0..255) e os
// direcionais (0..255, 128 no centro, o y para baixo).
struct Ps2PadFrame {
    uint16_t buttons = 0;
    uint8_t rx = 128, ry = 128, lx = 128, ly = 128;
    uint8_t pressure[12] = {};
};

// As entradas do controle do PS2. Os botões na ordem dos bits do libpad (L2 = bit 0 ... esquerda = bit 15), depois as
// metades dos direcionais (LYNeg: o esquerdo para cima).
enum class Ps2In : uint8_t {
    None = 0,
    L2, R2, L1, R1, Triangle, Circle, Cross, Square, Select, L3, R3, Start, Up, Right, Down, Left,
    LXNeg, LXPos, LYNeg, LYPos, RXNeg, RXPos, RYNeg, RYPos,
};
// O bit do botão (-1: um direcional ou nenhum).
int ps2_button_bit(Ps2In in);
// O índice da pressão do botão (direita 0, esquerda 1, cima 2, baixo 3, triângulo 4, bola 5, X 6, quadrado 7, L1 8,
// R1 9, L2 10, R2 11; -1: sem pressão).
int ps2_pressure_index(Ps2In in);
// Aperta o botão (pressão 255) ou põe a metade do direcional no extremo.
void ps2_press(Ps2PadFrame& p, Ps2In in);
const char* ps2_input_name(Ps2In in);

// O modo da entrada (o nf_driving o escolhe a cada quadro: Menu com a pausa do HUD aberta, Aim/AimZoom com a configuração
// do POV (a direção automática) e a luneta solta/fechada, Drive no resto).
enum class DriveMode : uint8_t { Drive = 0, Aim = 1, AimZoom = 2, Menu = 3 };
constexpr int kDriveModeCount = 4;
const char* drive_mode_name(DriveMode m);  // "conducao", "mira", "mira_zoom", "menu"

struct DriveCommand {
    const char* key;  // a chave em [driving_bindings]
    Ps2In target[kDriveModeCount];
    const char* label_pt;
    const char* label_en;
    const char* defaults;  // como no .ini
};
enum DriveCommandId : int {
    kDrvMenuAccept = 0,
    kDrvMenuBack,
    kDrvZoomIn,
    kDrvZoomOut,
    kDrvAccelerate,
    kDrvBrake,
    kDrvSteerLeft,
    kDrvSteerRight,
    kDrvHandbrake,
    kDrvFire,
    kDrvSpecial,
    kDrvWeaponNext,
    kDrvWeaponPrev,
    kDrvReload,
    kDrvChangeCamera,
    kDrvLookBack,
    kDrvPause,
    kDriveCommandCount,
};
extern const DriveCommand kDriveCommands[kDriveCommandCount];
int find_drive_command(const std::string& key);  // -1 se não existe

struct DriveBindings {
    InputCode in[kDriveCommandCount][kMaxKeyBinds];
    bool has(int command, InputCode c) const;
};
DriveBindings default_drive_bindings();
void load_drive_bindings(const Settings& s, DriveBindings* b);

struct DriveMouseConfig {
    float sensitivity = kDefaultMouseSensitivity;  // radianos por contagem
    bool invert_y = false;
};
DriveMouseConfig load_drive_mouse(const Settings& s);

// O que o PC deu num quadro.
struct DriveFrame {
    std::function<bool(int key)> key_down;  // platform/keys.h (também os botões do mouse); nulo: nada seguro
    bool mouse_captured = true;             // os botões e o movimento do mouse só valem com o mouse capturado
    float mouse_dx = 0.0f, mouse_dy = 0.0f;  // contagens do sensor (y para baixo)
    int wheel = 0;                           // entalhes da roda (positivo: para cima)
    const PadState* pad = nullptr;           // o controle do PC (nulo ou desconectado: nenhum)
    int32_t clock = 0;                       // o relógio do jogo em ticks (a duração dos toques da roda)
};

enum class DriveDevice : uint8_t { KeyboardMouse = 0, Pad = 1 };

// A metade de um direcional do controle do PC que conta como uso (o do jogo a pé: 40 de 127 no PS2).
constexpr int kDrivePadStickUse = 10000;
// Quantos ticks um entalhe da roda segura o alvo (depois, um quadro solto antes do próximo).
constexpr int32_t kDriveWheelPulseTicks = 4;

class DriveController {
public:
    DriveBindings bindings = default_drive_bindings();
    DriveMouseConfig mouse;

    // Um quadro: o controle do PS2 que o PC faz neste modo; aim_yaw/aim_pitch o giro da mira pelo mouse (só em Aim e
    // AimZoom; radianos, yaw positivo para a direita, pitch positivo para cima).
    Ps2PadFrame update(const DriveFrame& f, DriveMode mode);
    DriveDevice device() const { return device_; }
    bool command_active(int command) const { return command >= 0 && command < kDriveCommandCount && active_[command]; }
    float aim_yaw = 0.0f, aim_pitch = 0.0f;

    // Contadores (o relatório).
    uint64_t frames = 0, device_switches = 0, mouse_frames = 0, wheel_notches = 0, pulses = 0;
    double mouse_dx_total = 0.0, mouse_dy_total = 0.0, aim_yaw_total = 0.0, aim_pitch_total = 0.0;
    DriveMode last_mode = DriveMode::Drive;

private:
    DriveDevice device_ = DriveDevice::KeyboardMouse;
    bool active_[kDriveCommandCount] = {};
    bool prev_key_[kDriveCommandCount][kMaxKeyBinds] = {};
    uint16_t prev_pad_buttons_ = 0;
    bool prev_pad_lt_ = false, prev_pad_rt_ = false;
    bool prev_pad_stick_[4] = {};  // lx, ly, rx, ry fora do kDrivePadStickUse
    bool prev_captured_ = true;
    bool mouse_block_[8] = {};      // botões do mouse fora até soltar (presos ao capturar e ao sair do menu)
    // Os toques da roda: o alvo segurado até `pulse_until` e os que esperam.
    Ps2In pulse_ = Ps2In::None;
    int32_t pulse_until = 0;
    bool pulse_gap_ = false;
    Ps2In queue_[8] = {};
    int queued_ = 0;
};

// O controle do PC no controle do PS2 (pela posição dos botões; os direcionais só com `sticks`).
void pc_pad_to_ps2(const PadState& pc, Ps2PadFrame& p, bool sticks);
// A soma de dois quadros: os botões em OU, as pressões no maior, cada direcional o de `b` se fora do centro, senão o de
// `a`.
Ps2PadFrame ps2_merge(const Ps2PadFrame& a, const Ps2PadFrame& b);

}  // namespace nf
