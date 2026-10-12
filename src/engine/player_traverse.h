#pragma once
// A travessia do jogador (módulo Player do ACTION.ELF, 0x00192728-0x00198318, e a câmera que a acompanha): escadas,
// o gancho, cordas e tirolesas, paredes de esgueirar e os ícones de toque que liberam esses movimentos. Ver
// docs/engine.md, "Travessia".
//
// - Escada (posicionamento 0x22 ou entidade de script do tipo 0x22, objeto 0x2d; Ladder_Create): o jogador encosta
//   de frente (Player_DealWithObjHit -> Player_CollLadder) entre a base e o topo da esfera da escada e entra no
//   subestado 1 (Player_Climb): sem arma, a meio metro da escada, o analógico para a frente sobe e para trás desce
//   (0,02 m por tick por unidade), pular sem andar solta para trás; passa do topo (centro + raio + 0,85) e sai para
//   a frente; desce até o chão ou abaixo da base (centro - raio + 1) e sai. Ao sair, a arma volta e por um segundo
//   (BLData::traverse_wait) nada prende o jogador de novo.
// - Gancho (posicionamento 0x3a, objeto 0x51; Grapple_Create): com um ícone de toque do tipo 2 (ThirdIcon) a arma
//   0x50 atira o gancho; o projétil que para num ponto 0x51 chama Player_SetGrapplePoint (Bullet_Delete) e o
//   subestado 2 (Player_SSGrapple) puxa o jogador até 2 m do ponto (velocidade crescendo 0,05 por tick até 1,5 m
//   por tick), onde ele fica pendurado enquanto segura o gatilho; o chão (depois de 15 ticks) ou soltar o gatilho
//   desfazem. Player_GrappleSetRope estica a corda (entidade 0x2000138) da arma ao ponto.
// - Fio (posicionamento 0x3e, objeto 0x2c; Wire_Create): o parâmetro 1 é o tipo: 0 corda (subestado 6, Player_Wire:
//   pendurado pelas mãos, anda de lado com o analógico, cansa em 30 s), 2 tirolesa (subestado 0xf, Player_Zipline:
//   desce pelo fio a 0,15 m por tick e solta perto da ponta de baixo; só pega na metade de cima), 1 desligado. Pega
//   ao encostar (Player_CollWire) ou pulando com o ícone do tipo 3 perto. A câmera passa ao modo 0xb (camera_tracking,
//   terceira pessoa) e o canal do parâmetro 3 fica ligado enquanto o jogador está no fio.
// - Parede de esgueirar (posicionamento 0x3f, objeto 0x2b; CreepWall_Create): usar com o ícone do tipo 6 encosta o
//   jogador de costas (Player_CollCreepWall; subestado 7, Player_Creep), que anda de lado até a ponta e sai.
// - Ícone de toque (posicionamento 0xfa, objeto 0x42; ThirdIcon_Create): um volume que, encostado pelo jogador com o
//   canal do parâmetro 1 ligado, põe em BLData::touch_type o tipo do parâmetro 0 (o HUD mostra o ícone).
// - Câmera de terceira pessoa (posicionamento 0xe7; ThirdCam_Create): um caminho que camera_tracking segue em vez da
//   posição calculada, pela fração do fio ou da parede em que o jogador está (o parâmetro 0 é o índice em
//   ThirdPersonCameras, que o parâmetro 0 do fio e o 1 da parede escolhem).

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

struct BLData;
struct viewer_tag;

// Dados da escada (4 bytes em obj->data, tipo 0x2d).
struct LADDER_tag {
    int16_t kind;    // 0x0 0 (Ladder_Create): com 0 o jogador pode mirar na escada (Player_Weapon)
    int16_t param1;  // 0x2 parâmetro 1 (ninguém lê)
};
static_assert(sizeof(LADDER_tag) == 4);

// Dados do fio (8 bytes, tipo 0x2c) e da parede de esgueirar (8 bytes, tipo 0x2b).
struct WIRE_tag {
    uint16_t kind;     // 0x0 fio: parâmetro 1 (0 corda, 1 desligado, 2 tirolesa); parede: parâmetro 0
    uint16_t camera;   // 0x2 fio: parâmetro 0; parede: parâmetro 1. Índice em ThirdPersonCameras (0: nenhuma)
    uint16_t param2;   // 0x4 parâmetro 2
    uint16_t channel;  // 0x6 parâmetro 3: ligado enquanto o jogador está no fio ou na parede
};
static_assert(sizeof(WIRE_tag) == 8);

// Dados do ícone de toque (0xc bytes, tipo 0x42).
struct THIRDICON_tag {
    uint32_t kind;     // 0x0 parâmetro 0: o BLData::touch_type que o ícone dá (2 gancho, 3 corda, 6 parede)
    uint16_t enable;   // 0x4 parâmetro 1: liga o ícone (0: ligado desde a carga)
    uint16_t kill;     // 0x6 parâmetro 2: apaga o ícone
    int8_t wait;       // 0x8 ticks até a próxima mensagem (4 a cada toque)
    uint8_t pad[3];
};
static_assert(sizeof(THIRDICON_tag) == 0xc);

// Dados da câmera de terceira pessoa (0xc bytes; ThirdCam_Create).
struct THIRDCAM_tag {
    uint16_t index;          // 0x0 parâmetro 0
    uint16_t pad_2;
    framelist_tag* path;     // 0x4
    uint16_t frames;         // 0x8 quadros do caminho - 1
    uint8_t aim;             // 0xa parâmetro 1: 0 a câmera não gira para o jogador
    uint8_t pad_b;
};
static_assert(sizeof(THIRDCAM_tag) == 0xc);

constexpr int kThirdPersonCameras = 16;
extern obj_tag* ThirdPersonCameras[kThirdPersonCameras];
extern uint32_t oldhash;  // o último script de animação que a corda, a tirolesa ou o fio puseram no jogador

obj_tag* Ladder_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Player_CollLadder(obj_tag* obj, BLData* b, obj_tag* ladder);
void Player_Climb(BLData* b, obj_tag* obj);

obj_tag* Grapple_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Player_InitGrapple(BLData* b, obj_tag* obj);
void Player_SetGrapplePoint(BLData* b, _VECTOR* point, obj_tag* target);
void Player_SSGrapple(BLData* b, obj_tag* obj);
void Player_GrappleSetRope(obj_tag* obj);

obj_tag* ThirdIcon_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void ThirdIcon_Update(obj_tag* obj);

obj_tag* Wire_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Player_CollWire(obj_tag* obj, BLData* b, obj_tag* wire);
void Player_Wire(obj_tag* obj);
void Player_Zipline(obj_tag* obj);

obj_tag* CreepWall_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Player_CollCreepWall(obj_tag* obj, BLData* b, obj_tag* wall);
void Player_Creep(BLData* b, obj_tag* obj);

// O alvo da câmera (posicionamento 0xfd, objeto 0x48): fotografado (um projétil da câmera, classe 0x54, o atinge),
// liga o canal do parâmetro 0 e some.
obj_tag* CamSubject_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void CamSubject_Update(obj_tag* obj);

void ThirdCam_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void ThirdCam_GetPos(obj_tag* cam, float t, MATRIX* m);
void camera_tracking(obj_tag* obj, viewer_tag* v);

// Só no port: as guardas (o original leria um endereço inválido) e o que a travessia fez desde a partida.
struct TraverseStats {
    uint32_t ladders = 0;        // Player_CollLadder prendeu o jogador numa escada
    uint32_t ladder_exits = 0;   // Player_Climb soltou
    uint32_t wires = 0;          // Player_CollWire (corda ou tirolesa)
    uint32_t grapples = 0;       // Player_SetGrapplePoint
    uint32_t creeps = 0;         // Player_CollCreepWall
    uint32_t photos = 0;         // CamSubject_Update: alvos fotografados
    uint32_t guards = 0;         // câmera de terceira pessoa ausente, índice fora da tabela, modelo nulo
};
extern TraverseStats g_traverse_stats;

// Estado inicial das globais (testes).
void traverse_debug_power_on();
