// Helicópteros (módulo Copter). Ver copter.h.
//
// Divergências do original (só no port):
// - Copter_FireGun sem o clarão registrado (a peça 6) ou sem jogador leria um objeto nulo: o port pula o clarão (ou o
//   tiro).
// - A fumaça do helicóptero avariado gira ao acaso só em z (o original monta o vetor com lixo da pilha em y e w).

#include "engine/copter.h"

#include <cmath>
#include <cstring>

#include "engine/bullet.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_stats.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/spline.h"
#include "engine/vecmath.h"
#include "engine/view.h"

COPTERBIT Bits[9];
LLISTINFO_tag CopterList;
LLISTINFO_tag CopterPaths;
float DiffMod = 1.0f;
float COPT_COS = 0.0f;
float COPT_SIN = 0.0f;
CopterStats g_copter_stats;

namespace {

// [decomp] 0x0017e468 __static_initialization_and_destruction_0
// O construtor estático do módulo (chamado por _GLOBAL_$I$Copter_Create com 1, 0xffff): o cone de 45 graus da procura
// do jogador, COPT_COS = PS2Sinf(3 pi / 4) e COPT_SIN = PS2Sinf(pi / 4) (os dois ~0,7071).
void __static_initialization_and_destruction_0(int32_t initialize, int32_t priority) {
    if (priority != 0xffff || initialize == 0) return;
    COPT_COS = PS2Sinf(2.3561945f);  // 0x4016cbe4
    COPT_SIN = PS2Sinf(0.7853982f);  // 0x3f490fdb
}

const bool kCopterConeBuilt = (__static_initialization_and_destruction_0(1, 0xffff), true);

uint32_t attackmode_159 = 0;

COPTER* copter_of(obj_tag* obj) { return static_cast<COPTER*>(obj->data); }

_VECTOR row3(const MATRIX& m) { return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]}; }

void set_channel(uint16_t ch) {
    if (ch == 0) return;
    switch_channels[ch] = 1;
    switch_channels_time[ch] = GameState.frame;
}

// O jogador está no cone de 45 graus à frente do helicóptero, a até 1 km, e à vista de um ponto 4 m à frente dele.
bool sees_player(obj_tag* obj, obj_tag* player) {
    if (player == nullptr) return false;
    _VECTOR at = row3(obj->matrix);
    if (1e+06f < Vec_SqDist3D(&player->pos, &at)) return false;
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->matrix);
    if (Intersect_ConeSphere(&at, &dir, COPT_COS, COPT_SIN, &player->sphere) == 0) return false;
    _VECTOR ahead{at.x + dir.x * 4.0f, at.y + dir.y * 4.0f, at.z + dir.z * 4.0f, at.w};
    cel_tag* cel = build_FindCel(&ahead, glb_world);
    return Collide_LineOfSight(&ahead, &player->sphere, cel, obj, player, 0x2e);
}

}  // namespace

// [decomp] 0x001ee100 Intersect_ConeSphere
// A esfera toca o cone (vértice "apex", eixo "dir" unitário, meia abertura de cosseno cos_a e seno sin_a)? Dentro da
// esfera, sim; senão pela projeção no eixo e a distância ao eixo.
uint32_t Intersect_ConeSphere(const _VECTOR* apex, const _VECTOR* dir, float cos_a, float sin_a, const _VECTOR* sphere) {
    const _VECTOR d{sphere->x - apex->x, sphere->y - apex->y, sphere->z - apex->z, sphere->w - apex->w};
    const float r2 = sphere->w * sphere->w;
    const float d2 = Vec_SqMagnitude(&d);
    if (!(r2 < d2)) return 1;
    const float along = d.x * dir->x + d.y * dir->y + d.z * dir->z;
    if (!(along * along < d2 * cos_a * cos_a) && 0.0f < along) return 1;
    const float k = cos_a * along + sin_a * std::sqrt(std::fabs(d2 - along * along));
    return ((k * k - d2) + r2 >= 0.0f && 0.0f <= k) ? 1 : 0;
}

// [decomp] 0x0017d340 Copter_Create
// O caminho sempre; o helicóptero só com o parâmetro 0 em zero (ver copter.h).
obj_tag* Copter_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    auto* path = static_cast<COPTERPATH*>(Mem_Malloc(0x1c, 0x3304, 0));
    if (path == nullptr) return nullptr;
    std::memset(path, 0, sizeof(*path));
    uint32_t duration = level->params[10];
    if (duration == 0) duration = level->path_unknown;
    path->kind = uint16_t(level->params[0]);
    path->group = uint16_t(level->params[1]);
    path->path = level->path;
    const uint16_t frames = level->path_count == 0 ? uint16_t(0) : uint16_t(level->path_count - 1);
    path->frames = frames;
    path->start = uint16_t(level->params[3] < frames ? level->params[3] : frames);
    path->index = uint16_t(level->params[2]);
    path->speed = float(frames) * REC_FRAME_RATE;
    path->speed = path->speed * (FRAME_RATE / float(duration));
    LList_Add(&CopterPaths, &path->node);
    if (level->params[0] != 0) return nullptr;
    obj_tag* obj = control_create_object(0x7c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    COPTER* c = copter_of(obj);
    c->path = path;
    c->obj = obj;
    c->t = 0.0f;
    c->enable = uint16_t(level->params[6]);
    c->done = uint16_t(level->params[7]);
    c->channel = uint16_t(level->params[8]);
    c->paths = 0;
    c->delay = uint16_t(level->params[9]);
    std::memset(c->parts, 0, sizeof(c->parts));
    obj->type = 0x3b;
    obj->state = 0;
    obj->glist = nullptr;
    obj->mode |= 0x44;
    build_LinkToRoom(obj, 0, glb_world);
    c->sp = SP_Create(pos, rot, level->params[4], level->params[5], 8, 0, nullptr, Copter_Register, c, 0);
    LList_Add(&CopterList, &c->node);
    if (c->enable != 0 && switch_channels[c->enable] == 0) SP_HideObj(c->sp, 1);
    switch (GameState.difficulty) {
        case 2: DiffMod = 1.5f; break;
        case 3: DiffMod = 2.0f; break;
        case 4: DiffMod = 4.0f; break;
        default: DiffMod = 1.0f; break;
    }
    c->damage = 0.0f;
    c->max_damage = DiffMod * 299.0f;
    ++g_copter_stats.created;
    return obj;
}

// [decomp] 0x0017e4f0 Copter_Init
void Copter_Init() {
    LList_Init(&CopterList, 0x7c, 0);
    LList_Init(&CopterPaths, 0x1c, 0);
}

// [decomp] 0x0017e530 Copter_Register
// Evento 15 do script das peças: a peça "part" (0-8); todas menos o corpo (2) deixam os tiros passar (flag 0x20).
void Copter_Register(obj_tag* obj, uint16_t part, void* user) {
    if (user == nullptr || part >= 9) return;
    static_cast<COPTER*>(user)->parts[part] = obj;
    if (part != 2) obj->flags |= 0x20;
}

// [decomp] 0x0017d628 Copter_Move
// Anda no caminho (a matriz inteira, ou só a posição com pos_only); no fim dele escolhe outro caminho de ronda do grupo
// (mais avariado, mais longe na lista; acima da metade do dano, ao acaso) e devolve 1. Sem caminho, 1.
uint32_t Copter_Move(obj_tag* obj, COPTER* c, int8_t pos_only) {
    COPTERPATH* p = c->path;
    if (p == nullptr) return 1;
    if (p->frames == 0) return 0;
    uint32_t done = 0;
    MATRIX m;
    quaternion_tag q;
    std::memcpy(&q, c->quat, sizeof(c->quat));
    Spline_Interp(p->path, 0, p->frames, c->t, &m, &q, 2);
    std::memcpy(c->quat, &q, sizeof(c->quat));
    c->t = c->t + p->speed;
    if (pos_only == 0) {
        Mat_Copy(&m, &obj->matrix);
    } else {
        obj->matrix.m[3][0] = m.m[3][0];
        obj->matrix.m[3][1] = m.m[3][1];
        obj->matrix.m[3][2] = m.m[3][2];
        obj->matrix.m[3][3] = m.m[3][3];
    }
    if (float(p->frames) < c->t) {
        if (c->channel == 100) obj->life |= 1;
        const float frac = c->damage / (c->max_damage - c->max_damage * 0.1f);
        uint32_t next;
        if (0.5f <= frac) {
            next = Rand_Rand(c->paths);
        } else {
            next = uint32_t(float(c->paths) * frac + 0.5f);
        }
        if (c->paths < next) next = c->paths;
        for (LLNODE_tag* n = CopterPaths.first; n != nullptr; n = n->next) {
            auto* q = reinterpret_cast<COPTERPATH*>(n);
            if (q->group != c->path->group || q->index != next || q->kind == 2) continue;
            c->path = q;
            break;
        }
        done = 1;
        c->t = float(c->path->start);
    }
    SP_SetPosRot(c->sp, &obj->matrix);
    return done;
}

// [decomp] 0x0017d828 Copter_FireGun
// A metralhadora (peça 5) e o clarão (6) apontam para o jogador (0,5 m acima do quadril); a cada 6 ticks, uma bala.
uint32_t Copter_FireGun(obj_tag* obj, COPTER* c) {
    obj_tag* flash = c->parts[6];
    obj_tag* gun = c->parts[5];
    obj_tag* player = glb_players[0];
    if (gun == nullptr || player == nullptr) return 0;
    const _VECTOR target{player->pos.x, player->pos.y + 0.5f, player->pos.z, player->pos.w};
    _VECTOR at = row3(gun->matrix);
    Mat_LookAt(&gun->matrix, &at, &target, &CONSTANT_UP1, &CONSTANT_UP2);
    if (flash != nullptr) {
        _VECTOR fat = row3(flash->matrix);
        Mat_LookAt(&flash->matrix, &fat, &target, &CONSTANT_UP1, &CONSTANT_UP2);
    }
    const uint16_t s = gun->state;
    gun->state = uint16_t(s + 1);
    if (int16_t(s) <= 4) return 0;
    _VECTOR dir{target.x - at.x, target.y - at.y, target.z - at.z, target.w - at.w};
    Vec_Normalise(&dir, &dir);
    const float reach = gun->glist != nullptr ? gun->glist->box_max.z : 0.0f;
    _VECTOR muzzle{gun->pos.x + dir.x * reach, gun->pos.y + dir.y * reach, gun->pos.z + dir.z * reach, gun->pos.w};
    Sound_Play3D(0x5df, &muzzle, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    const int32_t weapon = GameState.level_file == 0x7000008 ? 0x61 : 0x71;
    Bullet_init(-1, obj, gun, weapon_def(weapon), &muzzle, &dir, 0xffff);
    ++g_copter_stats.shots;
    gun->state = 0;
    if (flash != nullptr) {
        flash->state = 2;
        flash->flags &= 0xffffffefu;
    }
    return 1;
}

// [decomp] 0x0017d9f0 Copter_FireMissile
// Um míssil de um dos lançadores (7 ou 8, ao acaso) a cada FRAME_RATE_INT x DiffMod ticks, para a frente; em 07000008,
// às vezes (1 em 8 / DiffMod) direto no jogador.
uint32_t Copter_FireMissile(obj_tag* obj, COPTER* c) {
    const uint32_t r = Rand_Rand(0x86e6);
    const float mod = DiffMod;
    obj_tag* launcher = (r & 1) != 0 ? c->parts[7] : c->parts[8];
    if (launcher == nullptr) return 0;
    const uint32_t every = uint32_t(float(FRAME_RATE_INT) * DiffMod);
    if (every == 0 || int32_t(GameState.frame) % int32_t(every) != 0) return 0;
    _VECTOR dir;
    bool aimed = false;
    if (GameState.level_file == 0x7000008 && Rand_Rand(uint32_t(8.0f / mod)) == 0 && glb_players[0] != nullptr) {
        const _VECTOR at = row3(launcher->matrix);
        dir = {glb_players[0]->pos.x - at.x, glb_players[0]->pos.y - at.y, glb_players[0]->pos.z - at.z,
               glb_players[0]->pos.w - at.w};
        Vec_Normalise(&dir, &dir);
        aimed = true;
    }
    if (!aimed) Mat_GetDir(&dir, &launcher->matrix);
    const float reach = (launcher->glist != nullptr ? launcher->glist->box_max.z : 0.0f) * 1.5f;
    _VECTOR at{launcher->sphere.x + dir.x * reach, launcher->sphere.y + dir.y * reach,
               launcher->sphere.z + dir.z * reach, launcher->sphere.w};
    const int32_t weapon = GameState.level_file == 0x7000008 ? 0x62 : 0x6d;
    Bullet_init(-1, obj, launcher, weapon_def(weapon), &at, &dir, 0xffff);
    ++g_copter_stats.shots;
    return 1;
}

// [decomp] 0x0017e4b8 Copter_GetBody
// O corpo (peça 2) de um helicóptero ligado (o canal do parâmetro 6, se houver).
obj_tag* Copter_GetBody(void* copter) {
    const auto* c = static_cast<const COPTER*>(copter);
    if (c->enable != 0 && switch_channels[c->enable] == 0) return nullptr;
    return c->parts[2];
}

// [decomp] 0x0017e568 Copter_Delete
void Copter_Delete(obj_tag* obj) {
    COPTER* c = copter_of(obj);
    SP_Delete(c->sp);
    LList_Remove(&CopterList, &c->node);
    std::memset(c, 0, sizeof(*c));
}

// [decomp] 0x0017dbb8 Copter_Update
void Copter_Update(obj_tag* obj) {
    obj_tag* player = glb_players[0];
    COPTER* c = copter_of(obj);
    obj_tag* flash = c->parts[6];
    if (c->enable != 0) {
        if (switch_channels[c->enable] == 0) return;
        SP_HideObj(c->sp, 0);
        c->enable = 0;
    }
    if (c->delay != 0) {
        --c->delay;
        if (c->delay == 0 && c->channel != 0) set_channel(c->channel);
    }
    if (flash != nullptr) {
        const uint16_t s = flash->state;
        if (s == 0) {
            flash->flags |= 0x10;
        } else {
            flash->state = uint16_t(s - 1);
            if (s == 1) flash->flags |= 0x10;
        }
    }
    switch (obj->state) {
        case 0:
            for (LLNODE_tag* n = CopterPaths.first; n != nullptr; n = n->next) {
                auto* q = reinterpret_cast<COPTERPATH*>(n);
                if (q->group != c->path->group) continue;
                if (q->kind == 2) {
                    c->exit = q;
                } else {
                    ++c->paths;
                }
            }
            obj->state = 1;
            break;
        case 1: {
            Copter_Move(obj, c, 0);
            if (c->channel == 100) break;
            const int16_t wait = int16_t(obj->unknown_f6 + 1);
            obj->unknown_f6 = uint16_t(wait);
            const float every = float(int32_t(int16_t(FRAME_RATE_INT)) << 2) / DiffMod;
            if (!(every <= float(wait))) break;
            if (sees_player(obj, player)) {
                obj->unknown_f6 = 0;
                obj->state = 2;
                c->attack_damage = 0.0f;
                attackmode_159 = Rand_Rand(10);
                ++g_copter_stats.attacks;
            }
            break;
        }
        case 2: {
            Music_Event(2, 1);
            Copter_Move(obj, c, 0);
            obj->unknown_f6 = uint16_t(obj->unknown_f6 + 1);
            if ((attackmode_159 & 1) == 0) {
                Copter_FireMissile(obj, c);
            } else {
                Copter_FireGun(obj, c);
            }
            const float limit = float(int16_t(FRAME_RATE_INT) * 5) * DiffMod;
            bool stay = false;
            if (player != nullptr && !(limit < float(int32_t(int16_t(obj->unknown_f6)))) && c->attack_damage <= 100.0f) {
                stay = sees_player(obj, player);
            }
            if (stay) break;
            obj->unknown_f6 = 0;
            obj->state = 1;
            c->attack_damage = 0.0f;
            break;
        }
        case 4:
            if (Copter_Move(obj, c, 0) == 0) break;
            c->path = c->exit;
            c->t = 0.0f;
            set_channel(c->done);
            obj->state = 5;
            break;
        case 5:
            if (Copter_Move(obj, c, 0) == 0) return;
            set_channel(c->done);
            obj->life |= 1;
            return;
        case 6:
            set_channel(c->done);
            Explode_Create(obj, &obj->pos, &CONSTANT_UP1, 10.0f, 10.0f, 0x6000052, 67.4f, 0xff, 0xff, 'd', nullptr, 6);
            obj->life |= 1;
            return;
        default:
            break;
    }
    // O dano dos tiros nas peças (Bits: as classes de tiro ignoradas e o multiplicador de cada uma).
    for (uint32_t i = 0; i < 9; ++i) {
        obj_tag* part = c->parts[i];
        if (part == nullptr) continue;
        if (Bits[i].mask != 0) Collide_FilterBullets(&part->hits, Bits[i].mask);
        bool by_player = false;
        for (HITDATA_tag* h = part->hits; h != nullptr; h = h->next) {
            float d;
            std::memcpy(&d, &h->unknown_08, 4);
            if (!(0.0f < d)) continue;
            obj_tag* o = h->obj;
            if (o != nullptr && o->type == 5) {
                obj_tag* from;
                std::memcpy(&from, static_cast<const uint8_t*>(o->data) + 0x30, 4);
                if (from != nullptr && from->type == 3) by_player = true;
            }
            c->damage += d * Bits[i].mul;
            c->attack_damage += d * Bits[i].mul;
        }
        if (by_player) PlrStat_LogShotHitEnemy(0);
    }
    if (c->max_damage * 0.5f < c->damage && c->parts[3] != nullptr && 8 <= Rand_Rand(10)) {
        obj_tag* smoke = c->parts[3];
        const uint32_t grey = uint32_t((1.0f - c->damage / c->max_damage) * 127.0f);
        const uint32_t g = grey & 0xff;
        _VECTOR rot{0.0f, 0.0f, Rand_FRand(6.2831855f), 0.0f};
        _VECTOR vel{0.0f, 0.0f, 0.0f, 0.0f};
        const float size = Rand_FRand(2.0f) + 3.0f;
        const uint32_t colour = grey << 24 | g << 16 | g << 8;
        Gas_Init(smoke->cel, &smoke->sphere, &rot, &vel, 2.0f, size, 0x200001f, 3, colour | ((0xbf - g) & 0xff), colour,
                 0x1a4);
    }
    if (1u < uint32_t(obj->state - 4) && c->max_damage < c->damage) {
        obj->state = 4;
        ++g_copter_stats.fled;
        if (GameState.level_file == 0x7000012 || GameState.level_file == 0x700000b) {
            obj->state = 6;
            ++g_copter_stats.exploded;
        }
        set_channel(c->done);
    }
}

void copter_debug_power_on() {
    std::memset(&CopterList, 0, sizeof(CopterList));
    std::memset(&CopterPaths, 0, sizeof(CopterPaths));
    DiffMod = 1.0f;
    attackmode_159 = 0;
    g_copter_stats = CopterStats{};
}
