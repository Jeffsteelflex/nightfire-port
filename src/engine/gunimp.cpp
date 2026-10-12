// O canhão montado. Ver gunimp.h.
//
// Divergências do original (só no port; também nas funções):
// - GunImp_Create sem memória para o cano devolve o canhão sem cano (o original escreveria no objeto nulo) e
//   GunImp_Update de um canhão sem cano não faz nada (conta em GunImpStats::guards); sem modelo, o assento do jogador
//   fica na base (o original leria o endereço 0x28).
// - GunImp_Deactivate de um canhão sem jogador (estado 0 e GunImp_PlayerHasDied não o chamam assim) não mexe no
//   jogador nulo.
// - Os vetores com lixo da pilha na coordenada w (a boca do cano, a fumaça) ficam com o w da matriz do cano.

#include "engine/gunimp.h"

#include <cstring>

#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/lights.h"
#include "engine/modules.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"

TURRETGUN TurretGuns[12];
GunImpStats g_gunimp_stats;

namespace {

GUNIMP_tag* gi_of(obj_tag* obj) { return static_cast<GUNIMP_tag*>(obj->data); }

bool multiplayer() {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    return mp != 0;
}

_VECTOR row3(const MATRIX& m) { return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]}; }

// v (xyz) pela parte 3x3 de m (vmulax/vmadday/vmaddz.xyz), mais a linha 3 (vadd.xyzw: o w é o da linha 3 mais o do
// vetor girado, que fica o de v).
_VECTOR transform(const MATRIX& m, const _VECTOR& v) {
    return {m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0],
            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1],
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2], m.m[3][3] + v.w};
}

// Um efeito de um tick (objeto 0x10, que Debris_Update apaga): o modelo, o alfa ao acaso, a escala e o giro em z.
obj_tag* flash(_VECTOR* at, uint32_t entity, uint16_t life, bool sized, cel_tag* cel, float* scale_used) {
    obj_tag* o = control_create_object(0x40, at, nullptr, nullptr);
    if (o == nullptr) return nullptr;
    auto* d = static_cast<uint8_t*>(o->data);
    o->type = 0x10;
    std::memcpy(d + 0x30, &life, 2);
    d[0x32] = 0;
    hashtable_set_object_to_entity_gfx(o, entity);
    o->unknown_103[3] = uint8_t(Rand_Rand(0x80) + 0x7f);
    if (sized) {
        o->scale = *scale_used;
        o->rot.z = Rand_FRand(6.2831855f);
    }
    control_link_object_to_cel(o, cel);
    return o;
}

}  // namespace

// [decomp] 0x00131f20 GunImp_Create
// O canhão (a base com o modelo do posicionamento, matriz do quatérnio dele) e o cano (o modelo da arma de TurretGuns),
// os dois sem colisão de esfera (flag 0x1000). No multijogador sem canhões (MPSettings +0x1b8) devolve nulo.
obj_tag* GunImp_Create(_VECTOR* pos, quaternion_tag* quat, celglist_tag* glist, level_tag*) {
    int32_t kind;
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp == 0) {
        kind = 0xb;
    } else {
        uint32_t allowed;
        std::memcpy(&allowed, MPSettings + 0x1b8, 4);
        if (allowed == 0) return nullptr;
        int16_t pick;
        std::memcpy(&pick, MPSettings + 0x1b4, 2);
        kind = pick == 10 ? int16_t(Rand_Rand(10)) : pick;
    }
    MATRIX m;
    Quat_QuatToMat(quat, &m);
    Mat_SetTrans(pos, &m);
    obj_tag* obj = Control_CreateObjEx(0xa0, pos, nullptr, &m, glist, nullptr, 1, 4, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;
    ++g_gunimp_stats.created;
    obj->type = 0x47;
    obj->flags |= 0x1000;
    GUNIMP_tag* g = gi_of(obj);
    g->seat.y = 0.0f;
    g->seat.x = 0.0f;
    g->kind = uint8_t(kind);
    // Só no port: sem modelo, o assento fica na base (o original leria o endereço 0x28).
    g->seat.z = glist != nullptr ? glist->box_min.z * 0.75f : 0.0f;
    celglist_tag* barrel = hashtable_hashcode_to_celglist(TurretGuns[uint8_t(kind)].entity);
    g->gun = Control_CreateObjEx(0xa0, pos, nullptr, &m, barrel, nullptr, 1, 4, 1.0f, 0, 0xff, 0xff, 0xff);
    if (g->gun != nullptr) {
        g->gun->flags |= 0x1000;
    } else {
        ++g_gunimp_stats.guards;
    }
    Mat_Copy(&obj->matrix, &g->base);
    g->overheated = 0;
    g->heat = 0.0f;
    return obj;
}

// [decomp] 0x00132f00 GunImp_Activate
// Usar o canhão livre (não agachado): o jogador monta (ver gunimp.h).
void GunImp_Activate(obj_tag* obj, obj_tag* by) {
    if (obj->state != 0) return;
    BLData* b = bloke_of(by);
    GUNIMP_tag* g = gi_of(obj);
    if (by->unknown_f6 == 4) return;
    const int16_t old = Player_ChangeSubState(by, 0xc);
    b->ride = obj;
    b->prev_substate = old;
    Player_SetCamMode(b, 0xe);
    obj->state = 1;
    obj->unknown_f6 = uint16_t(int16_t(b->player));
    Player_Disable(by, 1);
    g->player = by;
    Input_SetAction(int16_t(obj->unknown_f6), 0xe, 1);
    g->player->mode |= 0x44;
    Player_WeaponNone(by);
    ++g_gunimp_stats.mounted;
    Camera_CalcViewAngles(obj->unknown_f6, multiplayer() ? 1.0471976f : 0.5235988f);
}

// [decomp] 0x00132c18 GunImp_Deactivate
// Desmonta: o jogador volta ao subestado de antes, à câmera em primeira pessoa e à arma de antes (a pedida vira a
// guardada), sem queda nem velocidade, virado para onde o canhão apontava; o som do feixe para.
void GunImp_Deactivate(obj_tag* obj) {
    const int16_t index = int16_t(obj->unknown_f6);
    if (obj->state == 1) {
        BLData* b = glb_blokes[index];
        obj_tag* p = glb_players[index];
        b->ride = nullptr;
        Player_SetCamMode(b, 0);
        Player_ChangeSubState(p, b->prev_substate);
        Player_Enable(p, nullptr, 0);
        p->sphere_src[0x63] = p->sphere_src[0x64];
        Camera_CalcViewAngles(uint16_t(index), 1.0471976f);
        b->fall_time = 0.0f;
        Vec_Zero(&b->fall);
        Vec_Zero(&b->vel);
    }
    Input_SetAction(index, 0xe, 1);
    GUNIMP_tag* g = gi_of(obj);
    if (obj_tag* p = g->player; p != nullptr) {
        p->mode &= 0xffbb;
        _VECTOR dir;
        Mat_GetDir(&dir, &p->matrix);
        vecutil_cartesian_to_spherical_acc(&p->rot, dir.x, dir.y, dir.z);
        p->rot.x = 0.0f;
        p->rot.z = 0.0f;
    } else {
        ++g_gunimp_stats.guards;
    }
    g->player = nullptr;
    obj->state = 0;
    Sound_Stop(g->loop);
    g->loop = 0;
}

// [decomp] 0x00133008 GunImp_PlayerHasDied
// O canhão em que o jogador morto estava o solta.
void GunImp_PlayerHasDied(obj_tag* player) {
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 0x47 || gi_of(o)->player != player) continue;
        GunImp_Deactivate(o);
        return;
    }
}

// [decomp] 0x001320d8 GunImp_Update
// O calor e a espera do tiro caem; montado (estado 1): usar desmonta; os analógicos miram (o canhão segue 15% por
// tick), o jogador vai para trás da base; superaquecido solta fumaça; o tiro (ver gunimp.h).
void GunImp_Update(obj_tag* obj) {
    GUNIMP_tag* g = gi_of(obj);
    if (0.0f < g->heat) {
        g->heat = g->heat - FRAME_RATE_MUL;
    } else {
        g->heat = 0.0f;
        g->overheated = 0;
    }
    if (0.0f < g->fire_wait) {
        g->fire_wait = g->fire_wait - FRAME_RATE_MUL;
    } else {
        g->fire_wait = 0.0f;
    }
    if (obj->state != 1) return;
    const int16_t player = int16_t(obj->unknown_f6);
    if (Input_Action(player, 0xe, 4) != 0) {
        GunImp_Deactivate(obj);
        return;
    }
    if (g->gun == nullptr || g->player == nullptr) {
        ++g_gunimp_stats.guards;
        return;
    }
    g->yaw_target = g->yaw_target + -Input_Actionf(player, 3, 1) * (FRAME_RATE_MUL * 0.015f);
    const float look = Input_Actionf(player, 4, 1);
    g->unknown_78 = 0.0f;
    const float wanted = g->pitch_target + -look * (FRAME_RATE_MUL * 0.015f);
    g->pitch_target = wanted;
    float clamped = -1.5707964f;
    if (-1.5707964f <= wanted) clamped = wanted < 0.7853982f ? wanted : 0.7853982f;  // min.S
    g->pitch_target = clamped;
    // 15% por tick, sem passar do alvo.
    const auto follow = [](float now, float target) {
        const float d = target - now;
        float next = now + d * 0.15f;
        if (0.0f <= d ? target < next : next < target) next = target;
        return next;
    };
    g->pitch = follow(g->pitch, clamped);
    g->yaw = follow(g->yaw, g->yaw_target);
    MATRIX ry, rx;
    Mat_IdentityT(&ry);
    Mat_IdentityT(&rx);
    RotMatrixY(g->yaw, &ry);
    RotMatrixX(g->pitch, &rx);
    Mat_Copy(&g->base, &obj->matrix);
    Concat(&obj->matrix, &ry);
    Mat_CopyRot(&ry, &obj->matrix);
    obj_tag* gun = g->gun;
    Mat_Copy(&obj->matrix, &gun->matrix);
    Concat(&gun->matrix, &rx);
    Mat_CopyRot(&rx, &gun->matrix);
    obj->mode |= 0x20;
    gun->mode |= 0x20;
    obj_tag* p = g->player;
    Mat_Copy(&obj->matrix, &p->matrix);
    const _VECTOR seat = transform(obj->matrix, {g->seat.x, g->seat.y, g->seat.z, g->seat.w});
    p->matrix.m[3][0] = seat.x;
    p->matrix.m[3][1] = seat.y;
    p->matrix.m[3][2] = seat.z;
    p->matrix.m[3][3] = seat.w;
    p->pos = seat;
    p->mode |= 0x20;
    bloke_of(p)->pitch = -g->pitch * 0.5f;
    const TURRETGUN& t = TurretGuns[g->kind];
    _VECTOR muzzle = transform(gun->matrix, {t.muzzle[0], t.muzzle[1], t.muzzle[2], 0.0f});
    if (g->overheated != 0 && GameState.frame % 6 == 0) {
        Control_BuildWorldSph(gun);
        _VECTOR dir;
        Mat_GetDir(&dir, &gun->matrix);
        const float r = -gun->sphere.w * Rand_FRand(1.0f);
        _VECTOR at{muzzle.x + dir.x * r, muzzle.y + dir.y * r, muzzle.z + dir.z * r, muzzle.w};
        _VECTOR spin{0.0f, 0.0f, 0.0f, 0.0f};
        spin.z = Rand_FRand(6.2831855f);
        Gas_Init(obj->cel, &at, &spin, nullptr, 0.05f, 0.25f, 0x2000039, 7, 0x484030e4, 1, 0x80);
    }
    if (Input_Action(player, 9, 1) == 0 || 0.0f < g->fire_wait || g->overheated != 0) {
        // Soltou o gatilho (ou não pode atirar): o som contínuo para; segurando, só para superaquecido.
        if (Input_Action(player, 9, 1) != 0 && g->overheated == 0) return;
        if (g->loop != 0) {
            if (TurretGuns[g->kind].weapon == 0x61) Sound_Play3D(0x5e6, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            Sound_Stop(g->loop);
            g->loop = 0;
        }
        return;
    }
    const uint32_t weapon = t.weapon;
    uint32_t sound;
    if (weapon == 0x67 || weapon == 0x70) {
        sound = 0xffff;
        if (g->loop == 0) g->loop = Sound_Play3D(t.sound, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        g->heat = g->heat + 2.0f;
        if (200.0f < g->heat) {
            if (g->overheated == 0) ++g_gunimp_stats.overheats;
            g->overheated = 1;
        }
    } else if (weapon == 0x61) {
        sound = t.sound;
        if (g->loop == 0) {
            Sound_Play3D(0x5e4, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            g->loop = Sound_Play3D(0x5e5, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
        g->heat = g->heat + 8.0f;
        if (200.0f < g->heat) {
            if (g->overheated == 0) ++g_gunimp_stats.overheats;
            g->overheated = 1;
        }
    } else {
        sound = t.sound;
    }
    g->fire_wait = t.delay;
    _VECTOR dir;
    Mat_GetDir(&dir, &gun->matrix);
    _VECTOR at = muzzle;
    Bullet_init(player, glb_players[player], gun, weapon_def(int32_t(weapon)), &at, &dir, 0xffff);
    ++g_gunimp_stats.shots;
    if (sound != 0xffff) Sound_Play3D(sound, &at, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    float ten = 10.0f;
    if (weapon != 0x67 && weapon != 0x70) {
        flash(&muzzle, 0x2000a92, 1, true, obj->cel, &ten);
        Light_Create(&at, 0xff, 0x60, 0, 9.0f, 0, 0, 1, Rand_FRand(1.0f) + 1.5f, 0, 0, 0, 0xffff);
        return;
    }
    // O feixe: o clarão na boca, a luz, o raio de 1 km e o feixe até onde ele bate, mais o clarão do impacto.
    flash(&at, 0x200026e, 1, true, obj->cel, &ten);
    Light_Create(&at, 0, 0xff, 0xff, 9.0f, 0, 0, 1, Rand_FRand(1.0f) + 1.5f, 0, 0, 0, 0xffff);
    _VECTOR end{at.x + dir.x * 1000.0f, at.y + dir.y * 1000.0f, at.z + dir.z * 1000.0f, at.w};
    P32<HITDATA_tag> hits = nullptr;
    if (Collide_RayIntersect(&at, &end, obj->cel, obj, nullptr, &hits, 0, 8, 0)) end = hits->pos;
    if (obj_tag* beam = flash(&at, 0x20005cd, 0, false, obj->cel, &ten); beam != nullptr) {
        beam->owner = obj;
        PositionBeam(beam, &at, &end);
    }
    if (hits != nullptr) {
        _VECTOR spot{hits->pos.x - dir.x * 0.2f, hits->pos.y - dir.y * 0.2f, hits->pos.z - dir.z * 0.2f, hits->pos.w};
        obj_tag* hit_flash = flash(&spot, 0x200026e, 0, true, obj->cel, &ten);
        if (weapon == 0x67 && hit_flash != nullptr) Explode_Propagate(&hits->pos, 4.0f, hit_flash->cel, 5.0f, obj);
    }
    Collide_FreeHitList(&hits);
}

void gunimp_debug_power_on() {
    g_gunimp_stats = {};
}
