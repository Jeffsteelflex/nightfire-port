#pragma once
// A vibração do controle nas missões de carro: o IFeedback do DRIVING.ELF (actionqueuemgr.obj). Portado do MIPS e
// conferido contra o original no nf_driving_check (parte 12, `--so carro`). docs/driving.md seção 16.
//
// Quatro portas (IFeedbackPort, 0x34 bytes cada no .data 0x32c468); um IFeedback (FastAlloc(4)) é só o ponteiro para a
// porta do controle, contado em +0x00. O carro do jogador ganha o seu no construtor do PBondCar (a porta do bloco da
// troca, UberBond +0x990) e os eventos mexem nele:
//  - ApplyJolt(f): um tranco: f > 0,25 dá o nível 255 por round(32 f) quadros (até 24), senão o nível round(1020 f) por 8;
//    só substitui o tranco em curso se for mais forte ou mais longo (o meio da duração em +0x18);
//  - AddCollisionInfo(COLLISION_INFO): o tranco da força do choque (|+0x44|, x 0,2 se um dos lados é do tipo 0x0e); o
//    ~ECollision o chama para cada lado do choque cujo dono tem um IFeedback;
//  - SetRumble(a, t) e SetVibrate(a, t): um nível extra (round(255 a)) por round(60 t) quadros, no motor grande e no
//    pequeno; os tiros (EFireWeapon) e as explosões (EShake, ESpawnExplosion), não portados, os chamam;
//  - Update (uma vez por tick do relógio do IFeedback::Timer; aqui por passo da simulação): o motor pequeno (o 0 do
//    DualShock: liga ou desliga) por um pontilhado com a tabela 0x3738d0 (a inversão dos bits do contador) contra o nível
//    (a base +0x2c x 100, a vibração, o tranco que diminui), o grande (o 1: 0..255) pela base +0x30 x 125, o tranco e o
//    SetRumble; com a vibração desligada (o .data 0x32c460, o UberBond +0x994) ou em pausa (0x32e690), tudo parado.
// PAD_setactuator (librealinputz.a) vai para fora pelo `actuator` (o nf_driving o liga ao controle do PC como o jogo a
// pé faz com o seu: o motor grande no de baixa frequência do XInput, o pequeno ligado em 0xffff; platform/gamepad.h).

#include <cstdint>
#include <functional>

namespace drv {

struct CollisionInfo;

// Uma porta (o IFeedbackPort do .data, 0x34 bytes).
struct IFeedbackPortData {
    int32_t refs;          // +0x00 os IFeedback desta porta
    int32_t port;          // +0x04
    int32_t small_level;   // +0x08 o nível do motor pequeno (Update: a base x 100 mais a vibração, até 255)
    int32_t big_level;     // +0x0c a base do motor grande (x 125)
    int32_t jolt_level;    // +0x10
    int32_t jolt_count;    // +0x14 quadros de tranco restantes
    int32_t jolt_half;     // +0x18 metade da duração
    int32_t rumble;        // +0x1c SetRumble
    int32_t rumble_time;   // +0x20
    int32_t vibrate;       // +0x24 SetVibrate
    int32_t vibrate_time;  // +0x28
    float small_base;      // +0x2c
    float big_base;        // +0x30
};
static_assert(sizeof(IFeedbackPortData) == 0x34);

struct FeedbackState {
    IFeedbackPortData ports[4] = {};  // 0x32c468
    int32_t threads = 0;              // 0x32c538 os IFeedback vivos (o primeiro liga o temporizador)
    int32_t enabled = 1;              // 0x32c460 (o .data: ligada)
    int32_t uber_vibration = 1;       // UberBond +0x994 (a opção do jogo a pé; 0 desliga)
    int32_t paused = 0;               // 0x32e690
    uint32_t counter = 0;             // 0x3c8770 (o pontilhado)
    // PAD_setactuator(porta, motor, valor): motor 1 o grande (0..255), 0 o pequeno (0/1).
    std::function<void(int32_t port, int32_t motor, uint8_t value)> actuator;
    uint64_t guards = 0;              // divisões por zero que o PS2 pararia (break 7)
};
FeedbackState& feedback_state();
void feedback_reset();

// Um IFeedback (o ponteiro para a porta; -1 sem).
struct IFeedback {
    int32_t port = -1;
};

// [driving] 0x00165b90 IFeedbackPort::Reset
void IFeedbackPort_Reset(IFeedbackPortData& p, int32_t port);
// [driving] 0x00165ed0 IFeedback::IFeedback(int)
void IFeedback_Construct(IFeedback& f, int32_t port);
// [driving] 0x00166260 IFeedback::~IFeedback
void IFeedback_Destruct(IFeedback& f);
// [driving] 0x00165bc8 IFeedback::Update
void IFeedback_Update();
// [driving] 0x00165fb0 IFeedback::ApplyJolt(float)
void IFeedback_ApplyJolt(const IFeedback& f, float strength);
// [driving] 0x001660a0 IFeedback::AddCollisionInfo(COLLISION_INFO &)
void IFeedback_AddCollisionInfo(const IFeedback& f, const CollisionInfo& info);
// [driving] 0x001660f0 IFeedback::SetRumble(float, float)
void IFeedback_SetRumble(const IFeedback& f, float amount, float seconds);
// [driving] 0x00166180 IFeedback::SetVibrate(float, float)
void IFeedback_SetVibrate(const IFeedback& f, float amount, float seconds);
// [driving] 0x00166210 IFeedback::Pause
void IFeedback_Pause();

// A tabela do pontilhado (0x3738d0): o byte com os bits invertidos, menos a última entrada (254 em vez de 255).
uint8_t feedback_dither(uint32_t index);

// [driving] 0x00149ed8 ECollision::~ECollision, a parte da vibração (o trabalho do evento fica no destrutor, como os
// outros): cada lado do choque (o tipo +0x48 com a vaga +0x49; o +0x4c com a +0x4d) abaixo de 6, com a vaga ocupada e um
// dono com IFeedback, recebe AddCollisionInfo. `feedback_of` dá o IFeedback do dono do corpo da vaga (nulo: vaga livre ou
// sem IFeedback). O resto do destrutor (o som, as faíscas, o estímulo do render, o SetVibrate dos raspões contra os
// objetos do tipo 1, a zona da IA, os eventos da missão) não foi portado.
void ECollision_Feedback(const CollisionInfo& info, const std::function<const IFeedback*(int32_t slot)>& feedback_of);

}  // namespace drv
