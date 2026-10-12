// Torretas (módulo GT). Ver turret.h.
//
// Divergências do original (só no port):
// - GT_Track e GT_Update com uma torreta sem o cano registrado (o script do corpo sem o stream 1) leriam a matriz de um
//   objeto nulo: o port usa a do próprio objeto da torreta.
// - GT_DeployMiniGun monta a torreta com o nível da pilha zerado fora dos parâmetros 0-4 que o original escreve (o
//   parâmetro 5, o canal que a desliga, era lixo da pilha).
// - Os acumuladores vetoriais com lixo da pilha na coordenada w (o cano em GT_Update, o alvo em GT_RandMove) ficam com o
//   w dos vetores de verdade.
// - O giro do cano (spin_barrel) monta a rotação numa matriz zerada; o original usa a pilha que sobrou, e a translação
//   dela entrava na posição do cano.

#include "engine/turret.h"

#include <cmath>
#include <cstring>

#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/copter.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/emitter.h"
#include "engine/explode.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sort.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"

TurretStats g_turret_stats;

namespace {

constexpr float kPi = 3.1415927f;
constexpr float kTwoPi = 6.2831855f;

// TargetList.144 (0x00317008): 64 candidatos {objeto, distância ao quadrado}, ordenados por QuickSort (COMP_TARGETOBJ).
struct TargetEntry {
    P32<obj_tag> obj;
    float dist;
};
static_assert(sizeof(TargetEntry) == 8);
TargetEntry TargetList[64];

GUNTURRET* gt_of(obj_tag* obj) { return static_cast<GUNTURRET*>(obj->data); }

bool multiplayer() {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    return mp != 0;
}

_VECTOR row3(const MATRIX& m) { return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]}; }
_VECTOR sub4_vec(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
void set_row3(MATRIX* m, const _VECTOR& v) {
    m->m[3][0] = v.x;
    m->m[3][1] = v.y;
    m->m[3][2] = v.z;
    m->m[3][3] = v.w;
}

MATRIX& barrel_matrix(obj_tag* obj, GUNTURRET* gt) { return gt->barrel != nullptr ? gt->barrel->matrix : obj->matrix; }

// A boca do cano: a posição do cano mais a frente dele vezes GUNTURRET::muzzle.
_VECTOR muzzle_of(obj_tag* obj, GUNTURRET* gt, _VECTOR* dir) {
    MATRIX& m = barrel_matrix(obj, gt);
    Mat_GetDir(dir, &m);
    _VECTOR at = row3(m);
    at.x += dir->x * gt->muzzle;
    at.y += dir->y * gt->muzzle;
    at.z += dir->z * gt->muzzle;
    return at;
}

// Slerp de 4 x REC_FRAME_RATE da orientação "from" para "to"; a translação fica a de "from".
void turn_towards(const MATRIX& from, const MATRIX& to, MATRIX* out) {
    quaternion_tag a, b, q;
    Quat_MatToQuat(&a, &from);
    Quat_MatToQuat(&b, &to);
    Quat_Slerp_Acc(REC_FRAME_RATE * 4.0f, &a, &b, &q);
    Quat_Normalise(&q);
    Quat_QuatToMat(&q, out);
    set_row3(out, row3(from));
}

// O corpo vai para "m" (e com ele o clarão, o cano e a terceira peça).
void pose(GUNTURRET* gt, const MATRIX& m) {
    Mat_Copy(&m, &gt->aim);
    if (gt->barrel != nullptr) Mat_Copy(&m, &gt->barrel->matrix);
    if (gt->part != nullptr) Mat_Copy(&m, &gt->part->matrix);
    if (gt->flash != nullptr) Mat_Copy(&m, &gt->flash->matrix);
}

// O cano gira em z ("spin" acumula "step"), normalizado em ±180 graus.
// Divergência: RotMatrixZ só escreve a rotação, e o original monta a matriz numa área da pilha que sobrou de antes
// (Concat soma a linha 3 dela, girada, à posição do cano). Aqui ela começa zerada: o cano gira no lugar. Com lixo, o
// cano e o clarão pulavam de lugar conforme o que estava na pilha (visto na versão de 64 bits: 10 m, 100 m).
void spin_barrel(GUNTURRET* gt, float step) {
    if (gt->barrel == nullptr) return;
    float a = gt->spin;
    if (kPi <= std::fabs(a)) {
        const float r = std::fmod(a, kTwoPi);
        a = r;
        if (kPi <= std::fabs(r)) a = 0.0f <= gt->spin ? r - kTwoPi : r + kTwoPi;
    }
    gt->spin = a;
    MATRIX z{};
    RotMatrixZ(a, &z);
    gt->spin = gt->spin + step;
    Concat(&gt->barrel->matrix, &z);
    Mat_Copy(&z, &gt->barrel->matrix);
}

// Um tiro da arma 0x60 pela boca do cano, o clarão e, a cada dois, a fumaça. "player" >= 0 conta nas estatísticas.
void fire(obj_tag* obj, GUNTURRET* gt, uint32_t sound, obj_tag* from, int32_t player) {
    _VECTOR dir;
    _VECTOR at = muzzle_of(obj, gt, &dir);
    Sound_Play3D(sound, &at, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Bullet_init(-1, from, gt->barrel, weapon_def(0x60), &at, &dir, 0xffff);
    ++g_turret_stats.shots;
    // A fumaça gira ao acaso: o z da direção vira o giro (a direção já foi para a bala).
    dir.z = Rand_FRand(kTwoPi);
    if (player >= 0) PlrStat_LogShotFired(uint32_t(player));
    obj_tag* flash = gt->flash;
    if (flash == nullptr) return;
    const uint16_t toggle = uint16_t(flash->unknown_f6 ^ 1);
    flash->state = 2;
    flash->flags &= 0xffffffefu;
    flash->unknown_f6 = toggle;
    if (toggle != 0) Gas_Init(obj->cel, &at, &dir, nullptr, 0.05f, 0.1f, 0x2000039, 7, 0x48403080, 1, 0x80);
}

// Dano dos tiros nas peças de um script (o que Collide_GetDamageNObjects soma na lista de acertos de cada uma) e, se
// uma peça for ela mesma um projétil do taser, atordoa (o original confere o tipo do objeto do stream, não do projétil
// que o acertou: na prática nunca).
void take_damage(GUNTURRET* gt, SCRIPTINFO* si, bool* stunned) {
    for (uint16_t i = 0; si != nullptr && i < si->stream_count; ++i) {
        obj_tag* o = Script_GetObj(si, i);
        if (o == nullptr) continue;
        gt->health -= Collide_GetDamageNObjects(o->hits, nullptr, nullptr, 0);
        if (o->type == 5) {
            const weapon_definition_tag* w;
            w = nf::p32_load<const weapon_definition_tag>(static_cast<const uint8_t*>(o->data) + 0x44);
            if (w->id == 0x4a || w->id == 0x4c) *stunned = true;
        }
    }
}

}  // namespace

// [decomp] 0x00181560 GT_DeployMiniGun
// A metralhadora 0x52 montada onde o jogador (Player_WeaponInitBullet, um metro à frente) ou um bot atira com ela: se
// cabe no chão (Collide_PlaceObject, raio 0,5, meia largura 0,6, com uma cópia da matriz), uma torreta do tipo 0 (a
// que se usa: parâmetros 0 tipo, 1 = 2, 2/3 os scripts 0x6000045 e 0x6000044 do corpo e da base, 4 sem canal) com o
// modelo 0x2000359, virada para a frente da matriz alinhada ao chão; o quatérnio vem da matriz original. Devolve o
// objeto (0 se não coube: o jogador recebe a bala de volta).
uint32_t GT_DeployMiniGun(_VECTOR* pos, MATRIX* m, obj_tag* player) {
    MATRIX placed;
    Mat_Copy(m, &placed);
    if (!Collide_PlaceObject(pos, &placed, 0.5f, 0.6f, player)) return 0;
    alignas(16) _VECTOR dir, rot;
    Mat_GetDir(&dir, &placed);
    Vec_DirToRot(&dir, &rot);
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    level.params[0] = 0;
    level.params[1] = 2;
    level.params[2] = 0x6000045;
    level.params[3] = 0x6000044;
    level.params[4] = 0;
    alignas(16) quaternion_tag q;
    Quat_MatToQuat(&q, m);
    celglist_tag* glist = hashtable_hashcode_to_celglist(0x2000359);
    return nf::p32_u(GT_Create(pos, &rot, &q, &level, glist, player));
}

// [decomp] 0x0017ffa0 GT_Create
// Torreta (tipo 0x36, 0x100 bytes): sem os dois scripts (parâmetros 2 e 3) não cria. Os scripts tocam já e uma vez;
// os streams 0-2 do corpo se registram (GT_Register). A boca do cano fica a 1,5 x o z máximo das caixas dos modelos
// do corpo. No multijogador, uma metralhadora nova do mesmo dono explode as anteriores.
obj_tag* GT_Create(_VECTOR* pos, _VECTOR* rot, quaternion_tag*, level_tag* level, celglist_tag* glist, obj_tag* owner) {
    if (level->params[2] == 0 || level->params[3] == 0) return nullptr;
    obj_tag* obj = Control_CreateObjEx(0x100, pos, rot, nullptr, glist, nullptr, 1, 4, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;
    GUNTURRET* gt = gt_of(obj);
    std::memset(gt, 0, sizeof(*gt));
    obj->type = 0x36;
    obj->flags |= 0x810;
    gt->kind = int16_t(level->params[0]);
    gt->health = 100.0f;
    gt->param1 = uint16_t(level->params[1]);
    gt->enable = uint16_t(level->params[4]);
    gt->owner = owner != nullptr ? owner : obj;
    gt->disable = uint16_t(level->params[5]);
    gt->ammo = gt->kind == 0 ? int16_t(200) : int16_t(-1);
    gt->controlled = 0;
    gt->unknown_e4 = 0;
    Control_ObjectReferenced(obj, 1);
    gt->body = Script_Load(level->params[2], pos, rot, nullptr, nullptr, GT_Register, gt);
    gt->base = Script_Load(level->params[3], pos, rot, nullptr, nullptr, GT_Register, gt);
    if (gt->body == nullptr || gt->base == nullptr) {
        obj->life |= 1;
        return nullptr;
    }
    Script_Play(gt->body, 1);
    Script_Play(gt->base, 1);
    Script_Update(gt->body);
    Script_Update(gt->base);
    obj->state = 0;
    _VECTOR lo{9999999.0f, 9999999.0f, 9999999.0f, 0.0f};
    _VECTOR hi{-9999999.0f, -9999999.0f, -9999999.0f, 0.0f};
    for (uint16_t i = 0; gt->body != nullptr && i < gt->body->stream_count; ++i) {
        obj_tag* o = Script_GetObj(gt->body, i);
        if (o == nullptr || o->glist == nullptr) continue;
        Vec_Min(&lo, &o->glist->box_min, &lo);
        Vec_Max(&hi, &o->glist->box_max, &hi);
    }
    gt->muzzle = hi.z * 1.5f;
    if (gt->kind == 0) {
        Mat_GetDir(&gt->facing, &obj->matrix);
    } else {
        Mat_GetUp(&gt->facing, &obj->matrix);
        Vec_Negate(&gt->facing, &gt->facing);
    }
    if (multiplayer() && gt->kind == 0) {
        for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o == obj || o->type != 0x36 || gt_of(o)->owner != gt->owner) continue;
            o->life |= 1;
            Explode_Create(o, &o->pos, &CONSTANT_UP1, 5.0f, 5.0f, 0x6000052, 50.0f, 0xff, 0xff, 'd', nullptr, 0);
        }
    }
    ++g_turret_stats.created;
    return obj;
}

// [decomp] 0x00181538 GT_Register
// Evento 15 dos scripts da torreta: o objeto do stream 0, 1 ou 2 vira o clarão, o cano ou a terceira peça.
void GT_Register(obj_tag* obj, uint16_t stream, void* user) {
    if (user == nullptr || stream >= 3) return;
    P32<obj_tag>* slots = &static_cast<GUNTURRET*>(user)->flash;
    slots[stream] = obj;
}

// [decomp] 0x0017f9c8 GT_Track
// O alvo: candidatos a até 50 m (no multijogador, 22 m: os jogadores de MPGame), do mais perto ao mais longe, na frente
// da torreta (GUNTURRET::facing) e à vista da boca do cano. No modo história a torreta de outro dono que não o jogador
// mira no jogador (vivo e ativo); a montada também nos personagens (vivos, ligados, não ocultos) e nos helicópteros. O
// alvo atual continua se for o segundo visível.
obj_tag* GT_Track(GUNTURRET* gt, obj_tag* obj) {
    if (gt->target != nullptr) {
        if ((gt->target->life & 1) != 0) gt->target = nullptr;
        obj_tag* t = gt->target;
        if (t != nullptr && t->type == 2) {
            const Drone_tag* d = static_cast<const Drone_tag*>(t->data);
            const bool alive = (d->flags & 0x600) == 0 && (d->flags & 0x100) != 0 && 0.0f < d->health &&
                               (t->life & 1) == 0;
            if (!alive) gt->target = nullptr;
        }
    }
    for (TargetEntry& e : TargetList) e.dist = 1000000.0f;
    uint32_t n = 0;
    _VECTOR diff{};
    if (!multiplayer()) {
        const float range = 2500.0f;
        if (gt->owner != glb_players[0] && glb_players[0] != nullptr && 0.0f < glb_blokes[0]->health) {
            diff = sub4_vec(glb_players[0]->pos, obj->pos);
            TargetList[0].dist = Vec_SqDist3D(&glb_players[0]->pos, &obj->pos);
            if (TargetList[0].dist < range && glb_blokes[0]->active != 0) {
                n = 1;
                TargetList[0].obj = glb_players[0];
            }
        }
        bool full = false;
        if (gt->kind == 0) {
            for (LLNODE_tag* node = Drone_List()->first; node != nullptr && !full; node = node->next) {
                obj_tag* o = reinterpret_cast<Drone_tag*>(node)->obj;
                if (o == nullptr) continue;
                const Drone_tag* d = static_cast<const Drone_tag*>(o->data);
                const bool ok = (d->flags & 0x600) == 0 && (d->flags & 0x100) != 0 && 0.0f < d->health &&
                                o->type != 0x11 && (o->life & 1) == 0;
                if (!ok || (o->flags & 0x10) != 0) continue;
                diff = sub4_vec(o->pos, obj->pos);
                const float d2 = Vec_SqMagnitude(&diff);
                TargetList[n].dist = d2;
                if (d2 < range) TargetList[n++].obj = o;
                if (0x3e < n) full = true;
            }
        }
        // Os helicópteros (CopterList). Como no original, a distância usa a diferença do último candidato.
        for (LLNODE_tag* c = CopterList.first; !full && c != nullptr && gt->kind == 0; c = c->next) {
            obj_tag* body = Copter_GetBody(c);
            if (body == nullptr) continue;
            const float d2 = Vec_SqMagnitude(&diff) * 0.25f;
            TargetList[n].dist = d2;
            if (d2 < range) TargetList[n++].obj = body;
            if (0x3e < n) break;
        }
    } else {
        const float range = 500.0f;
        for (uint32_t i = 0; i < 8; ++i) {
            obj_tag* o;
            o = nf::p32_load<obj_tag>(MPGame + 0x1c + i * 0x30);
            if (o == nullptr || gt->owner == o) continue;
            if (o->type == 2 && !(0.0f < static_cast<const Drone_tag*>(o->data)->health)) continue;
            if (o->type == 3 && bloke_of(o)->health <= 0.0f) continue;
            diff = sub4_vec(o->pos, obj->pos);
            const float d2 = Vec_SqMagnitude(&diff);
            TargetList[n].dist = d2;
            if (d2 < range) TargetList[n++].obj = o;
            if (0x3e < n) break;
        }
    }
    if (n == 0) return nullptr;
    QuickSort(TargetList, n, sizeof(TargetEntry), COMP_TARGETOBJ, nullptr);
    obj_tag* first = nullptr;
    obj_tag* second = nullptr;
    _VECTOR dir;
    _VECTOR muzzle = muzzle_of(obj, gt, &dir);
    for (uint32_t i = 0; i < n; ++i) {
        obj_tag* t = TargetList[i].obj;
        _VECTOR to = sub4_vec(t->sphere, row3(gt->body->base));
        Vec_Normalise(&to, &to);
        const float facing = gt->facing.x * to.x + gt->facing.y * to.y + gt->facing.z * to.z;
        if (!(0.01f <= facing)) continue;
        if (!Collide_LineOfSight(&muzzle, &t->sphere, obj->cel, obj, t, 0x72f)) continue;
        if (first != nullptr) {
            second = t;
            break;
        }
        first = t;
    }
    obj_tag* cur = gt->target;
    if (cur != nullptr && first != cur && second == cur) return cur;
    return first;
}

// [decomp] 0x00180348 GT_RandMove
// Atordoada: o corpo vira para uma direção ao acaso (elevação ±90 e giro ±180 graus da frente), com o slerp de sempre.
void GT_RandMove(obj_tag*, GUNTURRET* gt) {
    _VECTOR e{Rand_FRandHalf(kPi), Rand_FRandHalf(kTwoPi), 0.0f, 0.0f};
    MATRIX r;
    RotMatrix(&e, &r);
    const _VECTOR f = gt->facing;
    const _VECTOR at{r.m[0][0] * f.x + r.m[1][0] * f.y + r.m[2][0] * f.z + gt->aim.m[3][0],
                     r.m[0][1] * f.x + r.m[1][1] * f.y + r.m[2][1] * f.z + gt->aim.m[3][1],
                     r.m[0][2] * f.x + r.m[1][2] * f.y + r.m[2][2] * f.z + gt->aim.m[3][2], f.w + gt->aim.m[3][3]};
    _VECTOR up;
    Mat_GetUp(&up, &gt->aim);
    MATRIX look;
    const _VECTOR eye = row3(gt->aim);
    Mat_LookAt(&look, &eye, &at, &up, &CONSTANT_UP2);
    MATRIX m;
    turn_towards(gt->aim, look, &m);
    pose(gt, m);
}

// [decomp] 0x00180498 GT_Update
void GT_Update(obj_tag* obj) {
    GUNTURRET* gt = gt_of(obj);
    obj_tag* flash = gt->flash;
    obj_tag* barrel = gt->barrel;
    Script_Update(gt->body);
    Script_Update(gt->base);
    if (flash != nullptr) {
        const uint16_t s = flash->state;
        if (s == 0) {
            flash->flags |= 0x10;
        } else {
            flash->state = uint16_t(s - 1);
            if (s == 1) flash->flags |= 0x10;
        }
    }
    bool stunned = false;
    take_damage(gt, gt->body, &stunned);
    if (obj->state == 4) take_damage(gt, gt->base, &stunned);
    if (stunned) GT_Disable(obj);
    if (gt->health < 0.0f || gt->ammo == 0) {
        if (obj->state != 6) {
            if (obj->state == 7) GT_LoseControl(obj);
            obj->state = 6;
            gt->timer = 0.0f;
            MATRIX& bm = barrel_matrix(obj, gt);
            _VECTOR up;
            Mat_GetUp(&up, &bm);
            _VECTOR at = row3(bm);
            Emitter_Create(0xc000004, &at, &up, 10.0f, 0, 0, 0, 0.0f);
            ++g_turret_stats.destroyed;
        }
    }
    switch (obj->state) {
        case 0:
            if (gt->kind == 1) {
                if (gt->enable != 0 && switch_channels[gt->enable] == 0) return;
                if (gt->disable != 0 && switch_channels[gt->disable] != 0) return;
                if (GT_Track(gt, obj) == nullptr) return;
            }
            if (gt->base != nullptr) gt->base->wait = 0;
            if (gt->body != nullptr) gt->body->wait = 0;
            gt->timer = 0.0f;
            obj->state = 1;
            return;
        case 1:
            if (gt->base->wait != 0 && gt->body->wait != 0) {
                Mat_Copy(&gt->body->base, &gt->aim);
                Mat_Copy(&gt->body->base, &gt->rest);
                obj->state = 4;
                ++g_turret_stats.opened;
            }
            return;
        case 2: {
            MATRIX m;
            turn_towards(gt->body->base, gt->rest, &m);
            gt->timer = gt->timer + REC_FRAME_RATE;
            if (1.2f < gt->timer) {
                gt->timer = 0.0f;
                if (gt->base != nullptr) gt->base->wait = 0;
                if (gt->body != nullptr) gt->body->wait = 0;
                obj->state = 3;
            }
            Script_SetPosRot(gt->body, &m);
            return;
        }
        case 3:
            if (gt->base->wait != 0 && gt->body->wait != 0) {
                if (gt->controlled != 0) GT_LoseControl(obj);
                if (gt->kind == 0) {
                    // A metralhadora recolhida vira o item da arma 0x52.
                    _VECTOR at = row3(obj->matrix);
                    Pickup_Create(&at, nullptr, &obj->matrix, hashtable_hashcode_to_celglist(0x2000366), 0, 0x52, 1, 0,
                                  0xf5, 1, 0, 0, 0);
                    obj->life |= 1;
                } else {
                    obj->state = 0;
                }
            }
            return;
        case 4: {
            gt->target = GT_Track(gt, obj);
            if (gt->target != nullptr) {
                Music_Event(2, 1);
                gt->timer = 0.0f;
                _VECTOR up;
                Mat_GetUp(&up, &gt->aim);
                MATRIX look;
                const _VECTOR eye = row3(gt->aim);
                Mat_LookAt(&look, &eye, &gt->target->pos, &up, &CONSTANT_UP2);
                MATRIX m;
                turn_towards(gt->aim, look, &m);
                pose(gt, m);
                gt->fire_timer = gt->fire_timer + REC_FRAME_RATE;
                if (0.1f < gt->fire_timer) {
                    gt->fire_timer = 0.0f;
                    if (0 < gt->ammo) --gt->ammo;
                    fire(obj, gt, gt->kind == 1 ? 0x4e7u : 0x10au, gt->owner != nullptr ? nf::raw(gt->owner) : obj, -1);
                }
                if (barrel == nullptr) return;
                spin_barrel(gt, REC_FRAME_RATE * kTwoPi + REC_FRAME_RATE * kTwoPi);
                return;
            }
            // Sem alvo: volta devagar à orientação de mira guardada; a automática desligada pelos canais recolhe.
            MATRIX m;
            turn_towards(gt->body->base, gt->aim, &m);
            Script_SetPosRot(gt->body, &m);
            if (gt->kind != 1) return;
            if (gt->enable != 0 && switch_channels[gt->enable] != 0) {
                if (gt->disable == 0 || switch_channels[gt->disable] == 0) return;
            }
            gt->timer = gt->timer + REC_FRAME_RATE;
            if (0.5f < gt->timer) {
                obj->state = 2;
                gt->timer = 0.0f;
            }
            return;
        }
        case 5:
            gt->timer = gt->timer + REC_FRAME_RATE;
            GT_RandMove(obj, gt);
            if (3.0f < gt->timer) obj->state = obj->unknown_f6;
            return;
        case 6:
            if (10.0f <= gt->timer) {
                if (multiplayer() && gt->kind != 1) {
                    obj->life |= 1;
                    Explode_Create(obj, &obj->pos, &CONSTANT_UP1, 5.0f, 5.0f, 0x6000052, 50.0f, 0xff, 0xff, 'd',
                                   nullptr, 0);
                }
            } else {
                gt->timer = gt->timer + REC_FRAME_RATE;
                if (int32_t(GameState.frame) % 10 == 0) {
                    _VECTOR rot{0.0f, 0.0f, Rand_FRand(kTwoPi), 0.0f};
                    Gas_Init(obj->cel, &obj->pos, &rot, nullptr, 0.05f, 0.25f, 0x2000039, 7, 0x48403080, 1, 0x3c);
                }
            }
            return;
        case 7: {
            // Controlada pelo jogador (multijogador).
            BLData* pb = bloke_of(gt->owner);
            const int8_t player = pb->player;
            gt->control.y += -Input_Actionf(player, 3, kActionDown) * 0.024543693f * FRAME_RATE_MUL;
            gt->control.x += -Input_Actionf(player, 4, kActionDown) * 0.024543693f * FRAME_RATE_MUL;
            gt->control.x = gt->control.x < -0.5f ? -0.5f : (0.5f <= gt->control.x ? 0.5f : gt->control.x);
            gt->control.y = gt->control.y < -1.5707964f ? -1.5707964f
                                                         : (1.5707964f <= gt->control.y ? 1.5707964f : gt->control.y);
            MATRIX my, mx;
            RotMatrixY(gt->control.y, &my);
            RotMatrixX(gt->control.x, &mx);
            Concat(&obj->matrix, &my);
            Concat(&my, &mx);
            MATRIX m;
            turn_towards(gt->body->base, mx, &m);
            Script_SetPosRot(gt->body, &m);
            gt->fire_timer = gt->fire_timer + REC_FRAME_RATE;
            gt->spin_speed = gt->spin_speed * 0.9f;
            if (Input_Action(player, kActFire, kActionDown) != 0) {
                if (0.1f < gt->fire_timer) {
                    gt->fire_timer = 0.0f;
                    if (0 < gt->ammo) --gt->ammo;
                    fire(obj, gt, 0x10a, gt->owner, player);
                }
                gt->spin_speed = REC_FRAME_RATE * kTwoPi + REC_FRAME_RATE * kTwoPi;
            }
            if (barrel != nullptr && gt->spin_speed != 0.0f) spin_barrel(gt, gt->spin_speed);
            if (Input_Action(player, kActUse, kActionPressed) != 0) GT_LoseControl(obj);
            return;
        }
        default:
            return;
    }
}

// [decomp] 0x00181408 GT_LoseControl
// O jogador solta a metralhadora: a câmera e o subestado de antes, a arma 0x53 guardada (a animação de jogar), a
// ação de usar consumida e o giro a partir da direção da matriz.
void GT_LoseControl(obj_tag* obj) {
    GUNTURRET* gt = gt_of(obj);
    obj_tag* p = gt->owner;
    BLData* pb = bloke_of(p);
    pb->ride = nullptr;
    Player_SetCamMode(pb, 0);
    glb_viewer[pb->player]->cel = nullptr;
    Player_ChangeSubState(p, pb->prev_substate);
    Player_Enable(p, nullptr, 0);
    ANIMOBJ_tag* anim = anim_of(p);
    anim->weapon = 0x53;
    anim->next_weapon = 0x53;
    pb->gun->state = 0xc;
    AnimScriptAdd(pb->gun, weapon_def(0x53)->anim_fire_alt);
    pb->weapons[0x53].have = 0;
    Input_SetAction(pb->player, kActUse, 1);
    p->mode &= 0xffbb;
    _VECTOR dir;
    Mat_GetDir(&dir, &p->matrix);
    vecutil_cartesian_to_spherical_acc(&p->rot, dir.x, dir.y, dir.z);
    p->rot.x = 0.0f;
    p->rot.z = 0.0f;
    if (obj->state == 7) obj->state = 4;
    gt->controlled = 0;
}

// [decomp] 0x00181740 GT_TakeControl
// A arma 0x53: o jogador assume a metralhadora ativa (estado 4): subestado 0x10, câmera 0xf, sem arma.
uint32_t GT_TakeControl(obj_tag* gun, obj_tag* player) {
    const uint16_t s = gun->state;
    if (s != 4) return 0;
    BLData* pb = bloke_of(player);
    GUNTURRET* gt = gt_of(gun);
    const int16_t old = Player_ChangeSubState(player, 0x10);
    pb->ride = gun;
    pb->prev_substate = old;
    Player_SetCamMode(pb, 0xf);
    glb_viewer[pb->player]->cel = nullptr;
    gun->state = 7;
    Input_SetAction(int16_t(gun->unknown_f6), kActUse, 1);
    Player_WeaponNone(player);
    Vec_Zero(&gt->control);
    gt->controlled = 1;
    return 1;
}

// [decomp] 0x00181678 GT_Delete
void GT_Delete(obj_tag* obj) {
    GUNTURRET* gt = gt_of(obj);
    Script_Free(gt->body);
    Script_Free(gt->base);
}

// [decomp] 0x001816a8 GT_Disable
// O taser (Bullet_CollisionHandler) ou uma explosão: ativa, fica atordoada (estado 5) e depois volta.
void GT_Disable(obj_tag* obj) {
    if (obj->state != 4) return;
    obj->unknown_f6 = obj->state;
    obj->state = 5;
    gt_of(obj)->timer = 0.0f;
    ++g_turret_stats.stunned;
}

// [decomp] 0x001816f0 GT_Activate
// Usar a metralhadora montada (tipo 0) aberta: recolhe (vira item).
void GT_Activate(obj_tag* obj, obj_tag*) {
    switch (obj->state) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 6:
            return;
        default:
            if (gt_of(obj)->kind != 0) return;
            gt_of(obj)->timer = 0.0f;
            obj->state = 2;
            return;
    }
}

// [decomp] 0x00181808 GT_PlayerHasDied
// O jogador morreu controlando uma metralhadora: solta.
void GT_PlayerHasDied(obj_tag* player) {
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 0x36 || o->state != 7 || gt_of(o)->owner != player) continue;
        GT_LoseControl(o);
        return;
    }
}

void turret_debug_power_on() {
    std::memset(TargetList, 0, sizeof(TargetList));
    g_turret_stats = TurretStats{};
}
