// A travessia do jogador: escadas, gancho, cordas, tirolesas, paredes de esgueirar, os ícones de toque e a câmera de
// terceira pessoa que acompanha a corda e a parede. Ver player_traverse.h.
//
// Divergências do original (só no port):
// - Player_CollLadder, Player_SSGrapple: o original grava a coordenada w da posição do jogador com lixo da pilha
//   (o quarto float do vetor montado na pilha); o port mantém o w que estava.
// - ThirdCam_Create: o índice (parâmetro 0) fora das 16 entradas de ThirdPersonCameras escreveria além da tabela; o
//   port ignora e conta em g_traverse_stats.guards. camera_tracking com uma câmera de terceira pessoa que nenhum
//   posicionamento criou leria um objeto nulo: o port usa a posição calculada.
// - Grapple_Create, CreepWall_Create, Wire_Create, ThirdIcon_Create, CamSubject_Create sem modelo leriam as flags de
//   um ponteiro nulo: o port usa 0.
// - CamSubject_Update: o canal (um short) indexa switch_channels só pelos 8 bits de baixo (o original escreveria fora
//   da tabela com um canal acima de 255).

#include "engine/player_traverse.h"

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/spline.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/fmath.h"

obj_tag* ThirdPersonCameras[kThirdPersonCameras];
uint32_t oldhash = 0;
TraverseStats g_traverse_stats;

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kHalfPi = 1.5707964f;

bool multiplayer() {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    return mp != 0;
}

uint32_t glist_flags(const celglist_tag* glist) { return glist != nullptr ? glist->flags : 0; }

// A câmera de acompanhamento (modo 0xb) usa os floats 0x1e4-0x1fc do viewer_tag (viewer_tag::unknown_1e0[1..7]):
// elevação, giro, a distância usada, a distância, e a elevação, o giro e a distância guardados pela parede.
enum TrackField : int {
    kTrackPitch = 1,      // 0x1e4
    kTrackYaw = 2,        // 0x1e8
    kTrackDistUsed = 3,   // 0x1ec
    kTrackDist = 4,       // 0x1f0
    kTrackSavedPitch = 5, // 0x1f4
    kTrackSavedYaw = 6,   // 0x1f8
    kTrackSavedDist = 7,  // 0x1fc
};

float track(const viewer_tag* v, TrackField f) {
    float x;
    std::memcpy(&x, &v->unknown_1e0[f], 4);
    return x;
}

void set_track(viewer_tag* v, TrackField f, float x) { std::memcpy(&v->unknown_1e0[f], &x, 4); }

_VECTOR sub4(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
_VECTOR row3(const MATRIX& m) { return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]}; }

// As pontas de um fio (ou parede): o centro da esfera mais e menos o vetor de comprimento "len" na direção do giro +
// 90 graus (e da inclinação "tilt"); só x, y e z andam (w fica o raio, como no original).
void ends(const obj_tag* o, float len, float tilt, _VECTOR* e1, _VECTOR* e2) {
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, len, o->rot.y + kHalfPi, tilt);
    *e1 = o->sphere;
    *e2 = o->sphere;
    e1->x += off.x;
    e1->y += off.y;
    e1->z += off.z;
    e2->x -= off.x;
    e2->y -= off.y;
    e2->z -= off.z;
}

// O fim comum de sair da corda, da tirolesa e da parede: a arma volta.
void restore_weapon(obj_tag* obj) {
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->next_weapon = anim->prev_weapon;
}

// O passo de mão da corda: o ponto do fio recuado "k" na direção do giro do jogador (PS2Sinf2 de 90 graus - giro e
// de -giro) e o corpo 1,0207 m abaixo.
void hang_at(obj_tag* obj, _VECTOR* at, float yaw, float k) {
    float c, s;
    PS2Sinf2(-yaw + kHalfPi, -yaw, &c, &s);
    at->x -= c * k;
    at->z -= s * k;
    obj->pos.x = at->x;
    obj->pos.z = at->z;
    obj->pos.y = at->y - 1.0207f;
}

}  // namespace

// ---- escada ----

// [decomp] 0x001966c0 Ladder_Create
// Escada (tipo 0x2d): o modelo é a colisão em que o jogador encosta. Sem as flags do modelo, como no original.
obj_tag* Ladder_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(4, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* d = static_cast<LADDER_tag*>(obj->data);
    d->kind = 0;
    d->param1 = int16_t(level->params[1]);
    obj->type = 0x2d;
    obj->glist = glist;
    build_LinkToRoom(obj, 0, glb_world);
    Controls_StraddleTest(obj);
    return obj;
}

// [decomp] 0x00196118 Player_CollLadder
// O jogador encostou numa escada (Player_DealWithObjHit). Fora da escada e do gancho, sem a espera de soltar, vivo e
// de frente para ela (o giro da escada aponta para fora): a altura vai no mínimo a 2 cm acima da base (mesmo sem
// prender) e, entre a base e o topo, ele prende: meio metro à frente do centro da escada, subestado 1, sem arma.
void Player_CollLadder(obj_tag* obj, BLData* b, obj_tag* ladder) {
    const int16_t s = int16_t(obj->unknown_f6);
    if (s == kPlrClimb || s == kPlrGrapple) return;
    if (b->traverse_wait != 0) return;
    if (b->health <= 0.0f) return;
    const float d = Vec_AngleDifference(obj->rot.y, ladder->rot.y);
    if (!(kHalfPi < std::fabs(d))) return;
    b->ladder_top = ladder->sphere.y + ladder->sphere.w + 0.85f;
    b->ladder_bottom = (ladder->sphere.y - ladder->sphere.w) + 1.0f;
    const float low = b->ladder_bottom + 0.02f;
    const float y = low < obj->pos.y ? obj->pos.y : low;
    obj->pos.y = y;
    obj->old_pos.y = y;
    if (!(b->ladder_bottom < y)) return;
    if (!(y < b->ladder_top)) return;
    const float a = ladder->rot.y + kPi;
    const float sx = PS2Sinf(a) * 0.5f;
    const float sz = PS2Sinf(a + kHalfPi) * 0.5f;
    obj->rot.z = 0.0f;
    obj->rot.x = 0.0f;
    obj->pos.x = ladder->sphere.x - sx;
    obj->pos.z = ladder->sphere.z - sz;
    b->climb_yaw = obj->rot.y;
    Player_ClearInertia(obj);
    Player_ChangeSubState(obj, kPlrClimb);
    b->traverse_step = 1;
    b->look_state = 2;
    Player_WeaponNone(obj);
    b->ladder = ladder;
    ++g_traverse_stats.ladders;
}

// [decomp] 0x00196310 Player_Climb
// Subestado 1. O giro fica a até 90 graus de olhar para a escada (e, sem mirar, volta 20% por tick para ela); o
// jogador acompanha a escada que se mexe. Pular sem o analógico para a frente solta para trás; acima do topo sai
// para a frente; entre a base e o topo o analógico sobe e desce (no teto não sobe; no chão, descer solta); abaixo
// da base solta. Ao soltar: um segundo sem prender de novo (traverse_wait), o subestado de andar e a arma de volta.
void Player_Climb(BLData* b, obj_tag* obj) {
    obj_tag* ladder = b->ladder;
    const float d = Vec_AngleDifference(obj->rot.y, ladder->rot.y + kPi);
    if (kHalfPi < d) obj->rot.y = (ladder->rot.y + kPi) - kHalfPi;
    if (d < -kHalfPi) obj->rot.y = ladder->rot.y + kPi + kHalfPi;
    ANIMOBJ_tag* anim = anim_of(obj);
    if ((anim->flags & 1) == 0) obj->rot.y = obj->rot.y + d * 0.2f;
    Player_Move(b, obj, 0.0f);
    Vec_Zero(&b->fall);
    obj->pos = sub4(add4(obj->pos, row3(ladder->matrix)), ladder->old_pos);
    const int8_t player = b->player;
    const auto leave = [&]() {
        b->traverse_wait = int8_t(int32_t(FRAME_RATE));
        b->vel.y = 0.0f;
        b->prev_substate = int16_t(obj->unknown_f6);
        anim->walk_pos = 0.0f;
    };
    if (Input_Action(player, kActJump, kActionDown) != 0 && Input_Actionf(player, kActForward, kActionDown) < 0.05f) {
        // Pulou: sai para trás.
        b->vel.z = b->vel.z + -(0.2f / FRAME_RATE_DIV) * 0.25f;
        leave();
    } else {
        ladder = b->ladder;
        b->ladder_top = ladder->sphere.y + ladder->sphere.w + 0.85f;
        b->ladder_bottom = (ladder->sphere.y - ladder->sphere.w) + 1.0f;
        if (b->ladder_top < obj->pos.y) {
            // Passou do topo: sai para a frente.
            b->vel.z = b->vel.z + (0.2f / FRAME_RATE_DIV) * 3.0f;
            leave();
        } else if (obj->pos.y < b->ladder_bottom) {
            leave();
        } else {
            float f = Input_Actionf(player, kActForward, kActionDown);
            if ((anim->flags & 0x20) != 0 && 0.0f < f) f = 0.0f;
            if ((anim->flags & 8) == 0 || !(f < 0.0f)) {
                b->vel.y = b->vel.y + f * (FRAME_RATE_MUL * 0.02f);
                return;
            }
            // No chão descendo: sai.
            leave();
        }
    }
    Player_ChangeSubState(obj, kPlrWalk);
    AnimObjectUpdate(obj);
    if (anim->weapon == kWeaponNone) anim->next_weapon = anim->prev_weapon;
    ++g_traverse_stats.ladder_exits;
}

// ---- gancho ----

// [decomp] 0x00198298 Grapple_Create
// Ponto de gancho (tipo 0x51): só o modelo, onde o projétil do gancho prende (Bullet_Delete).
obj_tag* Grapple_Create(_VECTOR* pos, _VECTOR* rot, level_tag*, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    obj->type = 0x51;
    obj->flags = glist_flags(glist);
    build_LinkToRoom(obj, 0, glb_world);
    Controls_StraddleTest(obj);
    return obj;
}

// [decomp] 0x00195d10 Player_InitGrapple
// Zera o gancho (0x140-0x16f, também BLData::move_locked) e cria a corda: a entidade 0x2000138, escondida (0x10),
// sem colisão de tiro (0x20), escalada no comprimento (Player_GrappleSetRope).
void Player_InitGrapple(BLData* b, obj_tag* obj) {
    std::memset(&b->grapple_point, 0, 0x30);
    celglist_tag* glist = hashtable_hashcode_to_celglist(0x2000138);
    b->rope = Control_CreateObjEx(0, nullptr, nullptr, nullptr, glist, obj, 0, 5, 1.0f, 0x30, 0xff, 0xff, 0xff);
}

// [decomp] 0x00195da0 Player_SetGrapplePoint
// O gancho prendeu em "target" no ponto "point": subestado 2, puxando (estado 1), 15 ticks antes de o chão soltar.
void Player_SetGrapplePoint(BLData* b, _VECTOR* point, obj_tag* target) {
    obj_tag* obj = glb_players[b->player];
    b->grapple_obj = target;
    b->grapple_point = *point;
    Player_ChangeSubState(obj, kPlrGrapple);
    b->unknown_15c = 1;
    b->grapple_wait = 0xf;
    b->grapple_speed = 0.0f;
    ++g_traverse_stats.grapples;
}

// [decomp] 0x00192860 Player_SSGrapple
// Subestado 2. Puxando (1): até 2 m do ponto o jogador anda na direção dele, cada vez mais rápido (0,05 por tick
// até 1,5 m por tick; 0,5 no multijogador, onde perder a linha de visão solta); no chão (passada a espera) solta.
// Chegou (2): fica enquanto o gatilho estiver apertado; soltou (4): volta a andar. O ponto acompanha o objeto do
// gancho (que pode se mexer) e o jogador sobe com ele. A velocidade de andar do tick vira a anterior e zera.
void Player_SSGrapple(BLData* b, obj_tag* obj) {
    _VECTOR* point = &b->grapple_point;
    Player_Move(b, obj, 1.0f);
    if (b->grapple_wait != 0) --b->grapple_wait;
    const uint16_t state = uint16_t(b->unknown_15c);
    if (state == 2) {
        if (Input_Action(b->player, kActFire, kActionDown) == 0) b->unknown_15c = 4;
    } else if (state == 4) {
        Player_ChangeSubState(obj, kPlrWalk);
        b->unknown_15c = 0;
        return;
    } else if (state == 1) {
        const bool lost =
            multiplayer() && !Collide_LineOfSight(&obj->pos, point, obj->cel, obj, b->grapple_obj, 8);
        if (lost || ((anim_of(obj)->flags & 8) != 0 && b->grapple_wait == 0)) {
            b->unknown_15c = 4;
        } else if (Vec_Dist3D(&obj->pos, point) < 2.0f) {
            b->unknown_15c = 2;
        } else {
            const float top = multiplayer() ? 0.5f : 1.5f;
            const float v = b->grapple_speed + 0.05f;
            b->grapple_speed = v < top ? v : top;
            _VECTOR dir = sub4(*point, obj->pos);
            Vec_Normalise(&dir, &dir);
            obj->pos.x += dir.x * b->grapple_speed;
            obj->pos.y += dir.y * b->grapple_speed;
            obj->pos.z += dir.z * b->grapple_speed;
        }
    }
    b->prev_vel = b->vel;
    Vec_Zero(&b->vel);
    obj_tag* g = b->grapple_obj;
    if (g != nullptr) {
        *point = sub4(add4(*point, row3(g->matrix)), g->old_pos);
        obj->pos.y = (obj->pos.y + g->matrix.m[3][1]) - g->old_pos.y;
        if ((g->life & 1) != 0) b->grapple_obj = nullptr;
    }
}

// [decomp] 0x00192728 Player_GrappleSetRope
// A corda do gancho, a cada tick: escondida, menos com o gancho na mão (armas 0x50 e 0x51) puxando ou com o
// projétil voando (BLData::move_locked): então vai do cano ao ponto (alinhada, escalada no comprimento).
void Player_GrappleSetRope(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    obj_tag* rope = b->rope;
    if (rope == nullptr) return;
    rope->flags |= 0x10;
    if (!(uint8_t(uint8_t(anim_of(obj)->weapon) - 0x50) < 2)) return;
    if (b->unknown_15c != 1 && b->move_locked == 0) return;
    rope->flags &= ~0x10u;
    rope->flags |= 0x8000;
    rope->mode |= 0x20;
    rope->matrix.m[3][0] = b->grapple_point.x;
    rope->matrix.m[3][1] = b->grapple_point.y;
    rope->matrix.m[3][2] = b->grapple_point.z;
    rope->matrix.m[3][3] = b->grapple_point.w;
    rope->pos = b->grapple_point;
    _VECTOR muzzle{};
    AnimGetBoneWorldTrans(b->gun, 0, 0, &muzzle, nullptr);
    _VECTOR dir = sub4(b->grapple_point, muzzle);
    Vec_Normalise(&dir, &dir);
    Mat_Align2Dir(&rope->matrix, &dir, &CONSTANT_UP1, &CONSTANT_UP2);
    rope->scale = Vec_Dist3D(&muzzle, &b->grapple_point);
}

// ---- ícones de toque ----

// [decomp] 0x00197070 ThirdIcon_Create
// Ícone de toque (tipo 0x42): tipo (parâmetro 0), canal que liga (1; 0 ligado já) e canal que apaga (2). O modelo é o
// volume; a flag 0x1000 deixa as balas passarem.
obj_tag* ThirdIcon_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0xc, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* d = static_cast<THIRDICON_tag*>(obj->data);
    d->kind = level->params[0];
    d->enable = uint16_t(level->params[1]);
    d->wait = 0;
    d->kill = uint16_t(level->params[2]);
    obj->state = d->enable == 0 ? 1 : 0;
    obj->glist = glist;
    obj->type = 0x42;
    obj->flags = glist_flags(glist) | 0x1000;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x00196e80 ThirdIcon_Update
// Colisão do ícone (control_funcs[0x42].collide): liga com o canal, some com o outro; ligado, cada jogador que encosta
// recebe o tipo do ícone em BLData::touch_type (o HUD mostra o ícone). A parede de esgueirar (6) avisa com uma
// mensagem a cada novo toque, fora da parede.
void ThirdIcon_Update(obj_tag* obj) {
    auto* d = static_cast<THIRDICON_tag*>(obj->data);
    if (0 < d->wait) --d->wait;
    if (switch_channels[d->enable] != 0) obj->state = 1;
    if (switch_channels[d->kill] != 0) obj->life |= 1;
    if (obj->state != 1) return;
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr || o->type != 3) continue;
        BLData* pb = bloke_of(o);
        if (d->wait == 0 && d->kind == 6 && o->unknown_f6 != kPlrCreep) {
            Text_AddMsg(0, 0, 1, Txt_BindLabel(0x1000291, 0), 0, 0xb4);
        }
        pb->touch_type = uint8_t(d->kind);
        d->wait = 4;
    }
}

// ---- fios: corda e tirolesa ----

// [decomp] 0x001981d8 Wire_Create
// Fio (tipo 0x2c). O giro z do posicionamento entra negado (é a inclinação do fio).
obj_tag* Wire_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    rot->z = -rot->z;
    obj_tag* obj = control_create_object(8, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* d = static_cast<WIRE_tag*>(obj->data);
    d->camera = uint16_t(level->params[0]);
    d->kind = uint16_t(level->params[1]);
    d->param2 = uint16_t(level->params[2]);
    d->channel = uint16_t(level->params[3]);
    obj->glist = glist;
    obj->type = 0x2c;
    obj->flags = glist_flags(glist);
    build_LinkToRoom(obj, 0, glb_world);
    Controls_StraddleTest(obj);
    return obj;
}

// [decomp] 0x00197130 Player_CollWire
// Pegar o fio (encostando: Player_DealWithObjHit; ou pulando perto com o ícone 3). Corda: subestado 6 pendurado
// pelas mãos (script 0x60005e5), de frente para o fio, o corpo recuado pelo quanto a raiz anda na animação de subir
// (0x4000536, quadros 0 e 0x170); tirolesa: subestado 0xf (script 0x6000095), virado para a ponta de baixo, e só na
// metade de cima. Os dois: câmera de acompanhamento (modo 0xb, 4 m) e sem arma.
void Player_CollWire(obj_tag* obj, BLData* b, obj_tag* wire) {
    if (b->traverse_wait != 0) return;
    if (b->health <= 0.0f) return;
    _VECTOR raw_a, raw_b;
    Vec_Zero(&raw_a);
    Vec_Zero(&raw_b);
    const auto* d = static_cast<const WIRE_tag*>(wire->data);
    const uint16_t kind = d->kind;
    if (kind == 1) return;
    const int16_t sub = kind == 2 ? int16_t(kPlrZipline) : int16_t(kPlrWire);
    if (int16_t(obj->unknown_f6) == sub) return;
    _VECTOR e1, e2;
    ends(wire, wire->sphere.w - 2.0f, wire->rot.z, &e1, &e2);
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    if (d->kind == 2) {
        const _VECTOR& low = e1.y < e2.y ? e1 : e2;
        if (Vec_Dist3D(&low, &at) < Vec_Dist3D(&e1, &e2) * 0.5f) return;
    }
    Player_ClearInertia(obj);
    Player_ChangeSubState(obj, sub);
    b->traverse_step = 6;
    b->look_state = 2;
    b->unknown_8cc = 0.0f;
    if (d->kind == 0) {
        b->traverse_step = 0xc;
        oldhash = 0x60005e5;
        AnimScriptAdd(obj, 0x60005e5);
        AnimGetFrameTransRaw(0x4000536, 0, &raw_a);
        AnimGetFrameTransRaw(0x4000536, 0x170, &raw_b);
    } else if (d->kind == 2) {
        oldhash = 0x6000095;
        AnimScriptAdd(obj, 0x6000095);
    }
    float yaw = obj->rot.y + kHalfPi;
    float face = wire->rot.y;
    if (d->kind == 1) {
        face -= kHalfPi;
        yaw -= kHalfPi;
        if (kHalfPi < std::fabs(Vec_AngleDifference(face, yaw))) face += kPi;
        at.y += 1.12353f;
    } else if (d->kind == 0) {
        face -= kHalfPi;
        yaw -= kHalfPi;
        if (kHalfPi < std::fabs(Vec_AngleDifference(face, yaw))) face += kPi;
        at.y -= 1.0207f;
    } else if (d->kind == 2) {
        if (e1.y < e2.y) {
            yaw += kHalfPi;
            face += kHalfPi;
        } else {
            yaw -= kHalfPi;
            face -= kHalfPi;
        }
        at.y -= 1.17363f;
    }
    obj->rot.y = face;
    obj->rot.x = 0.0f;
    obj->rot.z = 0.0f;
    obj->pos.x = at.x;
    obj->pos.y = at.y;
    obj->pos.z = at.z;
    Player_WeaponNone(obj);
    Player_SetCamMode(b, 0xb);
    viewer_tag* v = glb_viewer[b->player];
    set_track(v, kTrackDistUsed, 0.0f);
    set_track(v, kTrackDist, 4.0f);
    set_track(v, kTrackPitch, -b->pitch * kHalfPi);
    set_track(v, kTrackYaw, yaw);
    b->ladder = wire;
    // O quanto a raiz anda na animação de subir na corda, girado pelo jogador, sai da posição.
    _VECTOR delta = sub4(raw_b, raw_a);
    delta.x -= 0.082f;
    float c, s;
    PS2Sinf2(-obj->rot.y + kHalfPi, -obj->rot.y, &c, &s);
    const float x = delta.x;
    const float z = delta.z;
    delta.z = z * c + x * s;
    delta.x = x * c - z * s;
    obj->pos = sub4(obj->pos, delta);
    obj->old_pos = obj->pos;
    ++g_traverse_stats.wires;
}

// [decomp] 0x00197658 Player_Wire
// Subestado 6, pendurado na corda. O canal do fio fica ligado; o jogador fica entre as pontas (meio metro antes de
// cada uma) e cansa (100 em 30 s, fora do subir inicial): pular ou cansar solta. O analógico de lado escolhe o
// sentido (7 ou 8; parado 6), que gira o corpo para ele e anda um passo de mão por animação (scripts 0x60005ea-ee,
// 0x60000b4/b5, alternando as mãos por oldhash; cada passo põe o corpo 5,42 cm para um lado ou outro do ponto do
// fio), a 1,2x.
void Player_Wire(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    ANIMSCRIPT_tag* added = nullptr;
    Vec_Zero(&b->fall);
    obj_tag* wire = b->ladder;
    const auto* d = static_cast<const WIRE_tag*>(wire->data);
    Music_Event(0xd, 1);
    if (d->channel != 0) switch_channels[d->channel] = 1;
    _VECTOR e1, e2;
    ends(wire, wire->sphere.w - 0.5f, 0.0f, &e1, &e2);
    const float span = Vec_SqDist2D(&e1, &e2);
    const float d1 = Vec_SqDist2D(&e1, &obj->pos);
    const float d2 = Vec_SqDist2D(&e2, &obj->pos);
    if (span < d1) {
        obj->pos.x = e2.x;
        obj->pos.z = e2.z;
    }
    if (span < d2) {
        obj->pos.x = e1.x;
        obj->pos.z = e1.z;
    }
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    const float n1 = Vec_SqDist2D(&e1, &obj->pos);
    const float n2 = Vec_SqDist2D(&e2, &obj->pos);
    if (b->traverse_step != 0xc) b->unknown_8cc += 100.0f / (FRAME_RATE * 30.0f);
    if (Input_Action(b->player, kActJump, kActionPressed) != 0 || 100.0f < b->unknown_8cc) {
        b->prev_substate = int16_t(obj->unknown_f6);
        Player_ChangeSubState(obj, kPlrWalk);
        Player_SetCamMode(b, 0);
        set_track(glb_viewer[b->player], kTrackDist, 0.0f);
        b->unknown_8cc = 0.0f;
        b->traverse_wait = 0x1e;
        restore_weapon(obj);
        if (d->channel != 0) switch_channels[d->channel] = 0;
        return;
    }
    float side = Input_Actionf(b->player, kActStrafe, kActionDown);
    if ((side < 0.0f && (n1 <= 0.5f || span < d2)) || (0.0f < side && (n2 <= 0.5f || span < d1))) side = 0.0f;
    uint8_t want = 7;
    if (side <= 0.0f) want = side < 0.0f ? 8 : 6;
    float target = obj->rot.y;
    if (want == 7) target = wire->rot.y + kPi + kHalfPi;
    if (want == 8) target = wire->rot.y + kHalfPi;
    if (b->traverse_step == 6) {
        if (want == 6) {
            // Parado: alterna o apoio das mãos.
            if (!AnimScriptIsStopped(obj)) return;
            uint32_t script;
            if (oldhash == 0x60005e5 || oldhash == 0x60005ec || oldhash == 0x60000b4 || oldhash == 0x60005ee) {
                oldhash = 0x60005ec;
                script = 0x60005ec;
                hang_at(obj, &at, obj->rot.y, -0.0542f);
            } else {
                oldhash = 0x60005ed;
                script = 0x60005ed;
                hang_at(obj, &at, obj->rot.y, 0.0542f);
            }
            AnimScriptAdd(obj, script);
            return;
        }
    } else if (!AnimScriptIsStopped(obj)) {
        return;
    }
    uint8_t step = want;
    if (want == 7 || want == 8) {
        uint32_t script;
        if (std::fabs(Vec_AngleDifference(target, obj->rot.y)) <= 0.0002f || oldhash == 0x60005ee ||
            oldhash == 0x60005ea) {
            const bool was_b5 = oldhash == 0x60000b5;
            obj->rot.y = target;
            if (was_b5 || oldhash == 0x60005ed) {
                oldhash = 0x60000b4;
                script = 0x60000b4;
                hang_at(obj, &at, obj->rot.y, 0.0542f);
            } else {
                script = 0x60000b5;
                hang_at(obj, &at, target, -0.0542f);
                oldhash = 0x60000b5;
            }
        } else if (oldhash == 0x60000b5 || oldhash == 0x60005ed) {
            // Virando: o primeiro meio passo.
            oldhash = 0x60005ea;
            script = 0x60005ea;
            hang_at(obj, &at, obj->rot.y, 0.0542f);
        } else {
            script = 0x60005ee;
            hang_at(obj, &at, obj->rot.y, -0.0542f);
            oldhash = 0x60005ee;
        }
        added = AnimScriptAdd(obj, script);
    } else {
        // Parou depois de andar (7 ou 8): vira de frente para o fio. O original põe 0xa no passo mesmo sem virar
        // (o li do delay slot).
        const uint8_t cur = b->traverse_step;
        if (6 < cur && cur < 9) {
            step = 0xa;
            if (oldhash == 0x60005ea || oldhash == 0x60005ee) {
                const float dd = Vec_AngleDifference(obj->rot.y, wire->rot.y + kHalfPi);
                const float f = std::fabs(dd) < 0.0002f ? wire->rot.y + kPi : wire->rot.y;
                obj->rot.y = f + kHalfPi;
            }
        }
    }
    b->traverse_step = step;
    if (added == nullptr) return;
    added->speed = added->speed < 0.0f ? -1.2f : 1.2f;
}

// [decomp] 0x00197f50 Player_Zipline
// Subestado 0xf, descendo a tirolesa: o canal do fio ligado, a animação de pendurar (0x6000096) em laço, o corpo
// 1,17 m abaixo do ponto do fio mais perto e 0,15 m por tick para a frente; a 1 m da ponta de baixo, a animação de
// soltar (0x6000094, passo 9), e no fim dela volta a andar.
void Player_Zipline(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    obj_tag* wire = b->ladder;
    const auto* d = static_cast<const WIRE_tag*>(wire->data);
    if (b->traverse_step == 9) {
        if (!AnimScriptIsStopped(obj)) return;
        b->prev_substate = int16_t(obj->unknown_f6);
        Player_ChangeSubState(obj, kPlrWalk);
        Player_SetCamMode(b, 0);
        set_track(glb_viewer[b->player], kTrackDist, 0.0f);
        b->traverse_wait = int8_t(int32_t(FRAME_RATE));
        restore_weapon(obj);
        if (d->channel != 0) switch_channels[d->channel] = 0;
        return;
    }
    if (d->channel != 0) switch_channels[d->channel] = 1;
    if (AnimScriptIsStopped(obj)) {
        oldhash = 0x6000096;
        AnimScriptAdd(obj, 0x6000096);
    }
    _VECTOR e1, e2;
    ends(wire, wire->sphere.w, wire->rot.z, &e1, &e2);
    obj->pos.y += 1.17363f;
    const _VECTOR& low = e2.y <= e1.y ? e2 : e1;
    if (Vec_Dist3D(&obj->pos, &low) < 1.0f) {
        b->traverse_step = 9;
        oldhash = 0x6000094;
        AnimScriptAdd(obj, 0x6000094);
    }
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    obj->pos.z = at.z;
    obj->pos.x = at.x;
    obj->pos.y = at.y - 1.17363f;
    b->vel.z = b->vel.z + FRAME_RATE_MUL * 0.15f;
}

// ---- paredes de esgueirar ----

// [decomp] 0x00196fc8 CreepWall_Create
// Parede de esgueirar (tipo 0x2b): parâmetros 0-3 (o 1 escolhe a câmera de terceira pessoa, o 3 é o canal ligado
// enquanto o jogador está nela).
obj_tag* CreepWall_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(8, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* d = static_cast<WIRE_tag*>(obj->data);
    d->kind = uint16_t(level->params[0]);
    d->camera = uint16_t(level->params[1]);
    d->param2 = uint16_t(level->params[2]);
    d->channel = uint16_t(level->params[3]);
    obj->glist = glist;
    obj->type = 0x2b;
    obj->flags = glist_flags(glist);
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x00196758 Player_CollCreepWall
// Usar com o ícone 6 (Player_Activate): o jogador vai ao ponto da parede mais perto, de costas para ela, na altura
// do chão (um raio de 5 m acima a 5 m abaixo, + 1 m), sem arma, subestado 7, a câmera de acompanhamento (guardando
// a anterior) atrás dele a 4 m, e a animação de encostar (0x6000476).
void Player_CollCreepWall(obj_tag* obj, BLData* b, obj_tag* wall) {
    if (b->traverse_wait != 0) return;
    if (!(0.0f < b->health)) return;
    if (!(1u < uint16_t(obj->unknown_f6 - 6))) return;
    const float yaw = wall->rot.y;
    viewer_tag* v = glb_viewer[b->player];
    _VECTOR e1, e2;
    ends(wall, wall->sphere.w, 0.0f, &e1, &e2);
    e1.y = wall->sphere.y;
    e2.y = wall->sphere.y;
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    const float old_yaw = obj->rot.y;
    _VECTOR from = at;
    _VECTOR to = at;
    from.y = at.y + 5.0f;
    to.y = at.y - 5.0f;
    obj->rot.y = yaw + kHalfPi + kHalfPi;
    obj->rot.z = 0.0f;
    obj->rot.x = 0.0f;
    obj->pos.x = at.x;
    obj->pos.z = at.z;
    P32<HITDATA_tag> hits = nullptr;
    if (Collide_RayIntersect(&from, &to, obj->cel, obj, wall, &hits, 0, 0x62d, 0)) obj->pos.y = hits->pos.y + 1.0f;
    Collide_FreeHitList(&hits);
    Player_WeaponNone(obj);
    Player_ClearInertia(obj);
    Player_ChangeSubState(obj, kPlrCreep);
    b->traverse_step = 0;
    b->look_state = 2;
    Player_SetCamMode(b, 0xb);
    set_track(v, kTrackSavedYaw, track(v, kTrackYaw));
    set_track(v, kTrackSavedDist, track(v, kTrackDist));
    set_track(v, kTrackSavedPitch, track(v, kTrackPitch));
    set_track(v, kTrackDist, 4.0f);
    set_track(v, kTrackYaw, old_yaw + kPi);
    set_track(v, kTrackPitch, -b->pitch * kHalfPi);
    AnimScriptAdd(obj, 0x6000476);
    b->ladder = wall;
    ++g_traverse_stats.creeps;
}

// [decomp] 0x001969d0 Player_Creep
// Subestado 7, encostado na parede: o canal da parede ligado, o jogador no ponto da parede mais perto, 27,5 cm à
// frente dela, de costas. O analógico de lado (meia volta no mínimo) anda (scripts 0x600046d/0x6000471 para um lado,
// 0x600046f/0x6000475 para o outro, a 1,5x); parado, a animação de parar (0x600046c, 0x600046e). A menos de 1 m
// de uma ponta, andar para ela solta: volta a andar, virado para o lado da ponta mais perto, um pouco para a frente,
// e 2 s sem pegar a parede de novo.
void Player_Creep(BLData* b, obj_tag* obj) {
    ANIMSCRIPT_tag* added = nullptr;
    obj_tag* wall = b->ladder;
    const auto* d = static_cast<const WIRE_tag*>(wall->data);
    viewer_tag* v = glb_viewer[b->player];
    Music_Event(0xd, 1);
    if (d->channel != 0) switch_channels[d->channel] = 1;
    _VECTOR e1, e2;
    ends(wall, wall->sphere.w, 0.0f, &e1, &e2);
    e1.y = wall->sphere.y;
    e2.y = wall->sphere.y;
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    const float d1 = Vec_Dist2D(&e1, &at);
    const float d2 = Vec_Dist2D(&e2, &at);
    uint8_t at_end = d1 < 1.0f ? 1 : 0;
    if (d2 < 1.0f) at_end = 2;
    const int nearer = d1 < d2 ? 1 : 2;
    const float yaw = wall->rot.y + kPi;
    obj->pos.z = at.z;
    obj->pos.x = at.x;
    obj->rot.y = yaw;
    obj->pos.x += PS2Sinf(yaw) * 0.275f;
    obj->pos.z += PS2Sinf(obj->rot.y + kHalfPi) * 0.275f;
    float side = Input_Actionf(b->player, kActStrafe, kActionDown);
    if (std::fabs(side) < 0.5f) side = 0.0f;
    uint8_t want = 2;
    if (side <= 0.0f) want = side < 0.0f ? 1 : 0;
    if ((at_end == 1 && want == 2) || (at_end == 2 && want == 1)) {
        b->prev_substate = int16_t(obj->unknown_f6);
        Player_ChangeSubState(obj, kPlrWalk);
        Player_SetCamMode(b, 0);
        set_track(v, kTrackDist, 0.0f);
        set_track(v, kTrackYaw, track(v, kTrackYaw) - kPi);
        obj->rot.y = nearer == 1 ? obj->rot.y - kHalfPi : obj->rot.y + kHalfPi;
        b->traverse_wait = 0x78;
        restore_weapon(obj);
        b->vel.z = b->vel.z + 0.05f;
        if (d->channel != 0) switch_channels[d->channel] = 0;
        return;
    }
    const uint8_t step = b->traverse_step;
    if (step == 1 && want == 2) want = 0;
    if (step == 2 && want == 1) want = 0;
    if (!AnimScriptIsStopped(obj) && want != 0) return;
    bool set_step = true;
    if (want == 1) {
        uint32_t script = 0;
        if (step == 2 || step == 0) {
            script = 0x600046d;
        } else if (AnimScriptIsStopped(obj)) {
            script = 0x6000471;
        }
        if (script != 0) added = AnimScriptAdd(obj, script);
    } else if (want == 2) {
        uint32_t script = 0;
        if (step < 2) {
            script = 0x600046f;
        } else if (AnimScriptIsStopped(obj)) {
            script = 0x6000475;
        }
        if (script != 0) added = AnimScriptAdd(obj, script);
    } else if (step == 1) {
        AnimScriptAppendBlend(obj, 0x600046c);
    } else if (step == 2) {
        AnimScriptAppendBlend(obj, 0x600046e);
    }
    if (set_step) b->traverse_step = want;
    if (added != nullptr) {
        added->flags &= 0x7fffffff;
        added->speed *= 1.5f;
    }
}

// ---- câmera de terceira pessoa ----

// [decomp] 0x001bc510 ThirdCam_Create
// Caminho de câmera (posicionamento 0xe7): ThirdPersonCameras[parâmetro 0] (o objeto não tem tipo nem modelo).
void ThirdCam_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag*) {
    obj_tag* obj = control_create_object(0xc, pos, rot, nullptr);
    if (obj == nullptr) return;  // o original não testa
    const uint16_t index = uint16_t(level->params[0]);
    auto* d = static_cast<THIRDCAM_tag*>(obj->data);
    d->index = index;
    d->frames = uint16_t(level->path_count - 1);
    d->path = level->path;
    d->aim = uint8_t(level->params[1]);
    if (index < kThirdPersonCameras) {
        ThirdPersonCameras[index] = obj;
    } else {
        ++g_traverse_stats.guards;
    }
}

// [decomp] 0x001bc590 ThirdCam_GetPos
// A matriz da câmera no ponto t (0 a 1) do caminho.
void ThirdCam_GetPos(obj_tag* cam, float t, MATRIX* m) {
    const auto* d = static_cast<const THIRDCAM_tag*>(cam->data);
    Spline_Interp(d->path, 0, d->frames, float(d->frames) * t, m, nullptr, 2);
}

// [decomp] 0x001ba5e0 camera_tracking
// Modo de câmera 0xb (Player_PositionCamera): na corda, na tirolesa e na parede. A câmera fica a track_dist do ponto
// do fio mais perto do jogador (as pontas 1 m abaixo; na parede e no fio desligado, 1 m acima), com a elevação pela
// fração do fio (180 graus - fração x 30; fixa na parede) e o giro do fio (45 graus para o lado em que ele anda); com
// uma câmera de terceira pessoa (o parâmetro do fio ou da parede), a posição vem do caminho dela. Depois aponta para o
// ponto (menos se a câmera de terceira pessoa não aponta).
void camera_tracking(obj_tag* obj, viewer_tag* v) {
    BLData* b = glb_blokes[v->index];
    obj_tag* wire = b->ladder;
    if (wire == nullptr) {
        v->unknown_24 = 0;
        return;
    }
    const bool wall = wire->type == 0x2b;
    const auto* d = static_cast<const WIRE_tag*>(wire->data);
    _VECTOR e1 = wire->pos;
    _VECTOR e2 = wire->pos;
    _VECTOR off;
    Vec_Spherical_2_Cartesian(&off, wire->sphere.w, wire->rot.y + kHalfPi, wire->rot.z);
    e1.x += off.x;
    e1.z += off.z;
    e2.x -= off.x;
    e2.z -= off.z;
    if (wall || d->kind == 1) {
        e2.y = (e2.y - off.y) + 1.0f;
        e1.y = e1.y + off.y + 1.0f;
    } else {
        e2.y = (e2.y - off.y) - 1.0f;
        e1.y = (e1.y + off.y) - 1.0f;
    }
    _VECTOR at;
    vecutil_calculate_closest_point_on_line(&at, &e1, &e2, &obj->pos);
    const float frac = Vec_Dist2D(&e1, &at) / Vec_Dist2D(&e1, &e2);
    if (wall) {
        set_track(v, kTrackPitch, 2.748894f);
    } else {
        set_track(v, kTrackPitch, kPi - frac * 0.5235988f);
    }
    float yaw = wire->rot.y;
    if (wall) yaw += kPi;
    const uint8_t step = b->traverse_step;
    if (wall) {
        if (step == 7) yaw -= 0.7853982f;
        if (step == 8) yaw += 0.7853982f;
    } else {
        if (step == 7) yaw += 0.7853982f;
        if (step == 8) yaw -= 0.7853982f;
    }
    const float pitch = track(v, kTrackPitch);
    set_track(v, kTrackYaw, yaw);
    set_track(v, kTrackDistUsed, track(v, kTrackDist));
    uint8_t aim = 1;
    _VECTOR sph;
    Vec_Spherical_2_Cartesian(&sph, track(v, kTrackDist), yaw, -pitch);
    v->pos.x = at.x - sph.x;
    v->pos.z = at.z - sph.z;
    v->pos.y = at.y - sph.y;
    if ((wall || wire->type == 0x2c) && d->camera != 0) {
        obj_tag* cam = d->camera < kThirdPersonCameras ? ThirdPersonCameras[d->camera] : nullptr;
        if (cam == nullptr) {
            ++g_traverse_stats.guards;
        } else {
            aim = static_cast<const THIRDCAM_tag*>(cam->data)->aim;
            ThirdCam_GetPos(cam, frac, &v->camera);
            v->pos = row3(v->camera);
        }
    }
    if (aim == 0) return;
    const float ty = nf::fm::atan2(at.x - v->pos.x, at.z - v->pos.z);
    v->rot.y = v->rot.y + (ty - v->rot.y);
    View_RotTransMatrix(&v->rot, &v->pos, &v->camera);
    v->updated = 0;
    const float dx = at.x - v->pos.x;
    const float dz = at.z - v->pos.z;
    const float tx = nf::fm::atan2(at.y - v->pos.y, std::sqrt(dx * dx + dz * dz));
    v->rot.x = v->rot.x + (-tx - v->rot.x);
    View_RotTransMatrix(&v->rot, &v->pos, &v->camera);
    v->updated = 0;
    View_RotTransMatrix(&v->rot, &v->pos, &v->camera);
    v->updated = 0;
}

// [decomp] 0x001bc5d8 CamSubject_Create
// Alvo da câmera (posicionamento 0xfd, objeto 0x48): o modelo e o canal (parâmetro 0) em obj_tag::unknown_f6.
obj_tag* CamSubject_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    obj->type = 0x48;
    obj->flags = glist_flags(glist);
    obj->unknown_f6 = uint16_t(level->params[0]);
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001bc668 CamSubject_Update
// Fotografado: um projétil da câmera (classe 0x54) o atingiu; liga o canal e some.
void CamSubject_Update(obj_tag* obj) {
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        const obj_tag* o = h->obj;
        if (o == nullptr || o->type != 5) continue;
        const weapon_definition_tag* w;
        w = nf::p32_load<const weapon_definition_tag>(static_cast<const uint8_t*>(o->data) + 0x44);
        if (w->klass != 0x54) continue;
        switch_channels[int16_t(obj->unknown_f6) & 0xff] = 1;
        obj->life |= 1;
        ++g_traverse_stats.photos;
    }
}

void traverse_debug_power_on() {
    std::memset(ThirdPersonCameras, 0, sizeof(ThirdPersonCameras));
    oldhash = 0;
    g_traverse_stats = TraverseStats{};
}
