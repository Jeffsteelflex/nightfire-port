// Funções de outros módulos do ACTION.ELF que o script player usa e que ainda não estavam portadas:
// pilha de estados das câmeras (Stack_*, Camera_PushStates/PopStates), animação (AnimGetFirstTrans,
// AnimGetNDiscreteObj), jogador (Player_Disable/Enable/Cam2Mode), IA (Drone_EnableAll,
// Drone_CoderCreate), entrada (Input_RumblePause, Input_ClearAllActions) e quadro
// (PS2_ResetFrameCount). Ver script.h.

#include <cstring>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/parsemap.h"
#include "engine/input.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sort.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"

uint32_t PS2SkipCount = 0;
uint32_t GameCount = 0;

// [decomp] 0x001cceb0 Stack_IsEmpty
uint32_t Stack_IsEmpty(STACKINFO* stack) {
    if (stack == nullptr) return 0;
    return stack->count == 0 ? 1 : 0;
}

// [decomp] 0x001cced0 Stack_Top
// Vazia, devolve o primeiro elemento.
int32_t Stack_Top(STACKINFO* stack) {
    if (stack == nullptr) return 0;
    if (stack->count != 0) return stack->data[stack->count - 1];
    return stack->data[0];
}

// [decomp] 0x001ccf18 Stack_Push
// Cabem size - 1 elementos.
uint32_t Stack_Push(STACKINFO* stack, int32_t value) {
    if (stack == nullptr) return 0;
    const uint16_t count = stack->count;
    if (int32_t(count) < int32_t(stack->size) - 1) {
        stack->count = uint16_t(count + 1);
        stack->data[count] = value;
        return 1;
    }
    return 0;
}

// [decomp] 0x001ccf68 Stack_Pop
// Vazia, devolve o primeiro elemento sem mexer.
int32_t Stack_Pop(STACKINFO* stack) {
    if (stack == nullptr) return 0;
    if (stack->count != 0) {
        --stack->count;
        return stack->data[stack->count];
    }
    return stack->data[0];
}

// [decomp] 0x001bbc60 Camera_PushStates
// Guarda na pilha de cada câmera se ela está ligada (bit 0) e se mostra sprites (bit 8).
void Camera_PushStates() {
    for (uint16_t i = 0; i < 11; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v != nullptr) Stack_Push(&v->stack, int32_t(uint32_t(v->sprites) << 8 | v->active));
    }
}

// [decomp] 0x001bbcd0 Camera_PopStates
void Camera_PopStates() {
    for (uint16_t i = 0; i < 11; ++i) {
        viewer_tag* v = glb_viewer[i];
        if (v == nullptr || Stack_IsEmpty(&v->stack) != 0) continue;
        const int32_t s = Stack_Pop(&v->stack);
        Camera_Enable(i, int8_t(s & 1), int8_t((s >> 8) & 1), nullptr);
    }
}

// [decomp] 0x001c7348 AnimGetFirstTrans
// Translação da raiz no quadro atual do script (só scripts com o bit 0x800000).
uint32_t AnimGetFirstTrans(ANIMSCRIPT_tag* s, _VECTOR* out) {
    if ((s->flags & 0x800000) == 0) return 0;
    ANIMSEQ_tag* seq = s->seq[0];
    if (AnimSeqGetInfo(seq->hash, seq, nullptr) == nullptr) return 0;
    alignas(16) uint8_t frame[0xc00];
    AnimFrameCopy(frame, seq, nullptr, s->frame);
    std::memcpy(out, frame, sizeof(_VECTOR));
    return 1;
}

// [decomp] 0x001c73d0 AnimGetFrameTransRaw
// Translação da raiz no quadro "frame" da sequência "hash" (lida direto do arquivo, sem objeto). Só no port: sem a
// sequência carregada devolve zero (o original decodificaria a partir de um ponteiro nulo).
void AnimGetFrameTransRaw(uint32_t hash, int32_t frame, _VECTOR* out) {
    alignas(16) ANIMSEQ_tag seq;
    std::memset(&seq, 0, sizeof(seq));
    if (AnimSeqGetInfo(hash, &seq, nullptr) == nullptr) {
        *out = _VECTOR{0.0f, 0.0f, 0.0f, 0.0f};
        return;
    }
    seq.first_frame = 1;
    seq.last_frame = seq.frame_count;
    alignas(16) uint8_t buf[0xc60];
    AnimFrameCopy(buf, &seq, nullptr, float(frame));
    std::memcpy(out, buf, sizeof(_VECTOR));
}

// [decomp] 0x001c66b8 AnimGetNDiscreteObj
// O objeto rígido "index" preso a um osso de um objeto animado.
obj_tag* AnimGetNDiscreteObj(obj_tag* obj, uint32_t index) {
    if (obj == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim == nullptr || !(index < anim->skin->n_discrete)) return nullptr;
    return anim->attached[index];
}

// [decomp] 0x00195f70 Player_Disable
// Cena: o jogador para de atualizar e some (com a arma), sem colisão; freeze só o congela.
void Player_Disable(obj_tag* obj, int8_t freeze) {
    if (obj == nullptr) return;
    BLData* b = bloke_of(obj);
    if (freeze != 0) {
        b->frozen = 1;
        return;
    }
    b->active = 0;
    obj->flags |= 0x10;
    b->hit_test.type = 0;
    if (b->gun != nullptr) b->gun->flags |= 0x10;
}

// [decomp] 0x00195498 Player_Enable
// Volta do Player_Disable, opcionalmente na matriz m (posição e giro pela linha 2). Com check, procura
// um fio ou parede de esgueirar encostada (raio 2) e gruda nela.
void Player_Enable(obj_tag* obj, MATRIX* m, uint32_t check) {
    if (obj == nullptr) return;
    BLData* b = bloke_of(obj);
    b->active = 2;
    b->frozen = 0;
    obj->flags &= 0xffffffefu;
    if (m != nullptr) {
        _VECTOR dir;
        Mat_GetDir(&dir, m);
        std::memcpy(&obj->pos, &m->m[3][0], sizeof(_VECTOR));
        vecutil_cartesian_to_spherical_acc(&obj->rot, dir.x, dir.y, dir.z);
        obj->old_pos = obj->pos;
        obj->old_rot = obj->rot;
    }
    b->hit_test.type = 0x800;
    if (b->gun != nullptr) b->gun->flags &= 0xffffffefu;
    b->jump = 0;
    b->jump_wait = 0;
    Vec_Zero(&b->fall);
    BLData* d = bloke_of(obj);
    Vec_Zero(&d->prev_vel);
    Vec_Zero(&d->prev_rot_vel);
    Vec_Zero(&d->vel);
    Vec_Zero(&d->rot_vel);
    Input_ClearAllActions(-1);
    control_unlink_object(obj);
    build_LinkToRoom(obj, 1, glb_world);
    if (check != 0) {
        P32<HITDATA_tag> hits = nullptr;
        Collide_SphereIntersect(&obj->pos, 2.0f, obj->cel, obj, nullptr, &hits, 0, 1, 0x8c0, 4);
        for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
            obj_tag* o = h->obj;
            if (o == nullptr) continue;
            if (o->type == 0x2c) {
                Player_CollWire(obj, b, o);
                break;
            }
            if (o->type == 0x2b) {
                Player_CollCreepWall(obj, b, o);
                break;
            }
        }
        Collide_FreeHitList(&hits);
    }
    control_unlink_object(obj);
    build_LinkToRoom(obj, 1, glb_world);
}

// [decomp] 0x00195fc8 Player_Cam2Mode
// Modo de câmera do jogador: 3 terceira pessoa (modo 2); outros, primeira pessoa olhando para a frente.
void Player_Cam2Mode(uint16_t player, uint32_t mode) {
    BLData* b = glb_blokes[player];
    if (b == nullptr) return;
    if (mode == 3) {
        Player_SetCamMode(b, 2);
        return;
    }
    b->pitch = 0.0f;
    Player_SetCamMode(glb_blokes[player], 0);
    Player_ChangeSubState(glb_players[player], 0);
}

// [decomp] 0x001383e8 Drone_EnableAll
// Liga ou desliga todos os personagens (os ligados de novo só se não estão esperando, estado 1). Ao
// ligar, alguns scripts mandam mensagens: 0600001b chama todos (10) e muda a música, 0600002e acorda
// (0xe) os que esperam o canal 10.
void Drone_EnableAll(int8_t on, uint32_t script) {
    if (Drone_bDisableSystem != 0) return;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (on != 0 && d->obj->state == 1) continue;
        NDrone2_Enable(on, d);
    }
    NPCGlobals.enabled = uint8_t(on);
    if (on == 0) return;
    NPCGlobals.unknown_1ac = GameState.frame;
    if (script == 0x600001b) {
        Drone_Message(nullptr, 10, 0, FRAME_RATE_INT << 3);
        Music_Event(0xf, 1);
    } else if (script == 0x600002e) {
        for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
            Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
            obj_tag* o = d->obj;
            if (o == nullptr || (o->type != 2 && o->type != 0x11)) continue;
            if (o->state == 1 && d->channel_a == 10) Drone_Message(o, 0xe, 0, 0);
        }
    }
}

// [decomp] 0x0013a108 Drone_CoderCreate
// Personagem criado por um script (evento 8): posicionamento falso com a pele/tipo (p0, p1) e a arma
// (p9); o estado inicial vem do script. p2 não é usado.
obj_tag* Drone_CoderCreate(_VECTOR* pos, _VECTOR* rot, uint32_t p0, uint32_t p1, uint32_t, uint32_t p9,
                           uint16_t state) {
    if (Drone_bDisableSystem != 0) return nullptr;
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    level.params[0] = p0;
    level.params[1] = p1;
    level.params[9] = p9;
    level.params[14] = 0;
    level.params[5] = 0;
    level.params[8] = 0;
    obj_tag* obj = Drone_Create(pos, rot, &level);
    if (obj == nullptr) return nullptr;
    Drone_tag* d = static_cast<Drone_tag*>(obj->data);
    d->initial_state = state;
    NDrone2_PostLoad_Init(d->obj);
    return obj;
}

// [decomp] 0x0017f710 Input_RumblePause
void Input_RumblePause() {
    for (uint16_t pad = 0; pad < 4; ++pad) psiInput_RumbleStop(pad);
}

// [decomp] 0x0017f558 Input_ClearAllActions
// Zera os valores das 40 ações de um jogador (-1: todos).
void Input_ClearAllActions(int16_t player) {
    psiFlushAllInput();
    for (uint16_t i = 0; i < 4; ++i) {
        if (player == -1 || player == int16_t(i)) std::memset(PlayerSetting[i].actions, 0, 0xa0);
    }
}

// [decomp] 0x00109450 PS2_ResetFrameCount
// Depois de uma carga no meio do jogo, o laço principal não tenta recuperar os quadros perdidos.
void PS2_ResetFrameCount() {
    PS2SkipCount = 0;
    GameCount = VBlankCount;
}

