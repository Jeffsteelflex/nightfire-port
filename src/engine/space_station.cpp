// O laser e os mísseis da estação espacial. Ver space_station.h.
//
// Divergências do original (só no port; também nas funções):
// - Create_SpaceLaser e Create_SpaceMissile sem memória devolvem nulo (o original escreveria no objeto nulo).
// - SpaceLaser_Update sem jogador no começo de uma carga mantém o alvo anterior (o original leria o objeto nulo); conta
//   em SpaceStats::guards. Sem script de carga (o hash não registrado), a carga termina na hora.
// - Os vetores com lixo da pilha na coordenada w (o fim do raio do laser) ficam com o w da boca.
// - O script da carga que terminou não é liberado (o original também não: ele só esquece o ponteiro).
// - MissileDeploy[número - 1] só é escrito com o número de 1 a 8 (o original escreveria fora dos 8 bytes com 0 ou
//   mais de 8); os canais passam por & 0xff ao indexar switch_channels.

#include "engine/space_station.h"

#include <cstring>

#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/emitter.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/parsemap.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"

uint8_t MissileDeploy[8];
SpaceStats g_space_stats;

namespace {

SPACELASER_tag* laser_of(obj_tag* obj) { return static_cast<SPACELASER_tag*>(obj->data); }
SPACEMISSILE_tag* missile_of(obj_tag* obj) { return static_cast<SPACEMISSILE_tag*>(obj->data); }

bool channel_on(uint16_t c) { return c == 0 || switch_channels[uint8_t(c)] != 0; }

void stop_sound(SPACELASER_tag* l) {
    if (l->sound == 0) return;
    Sound_Stop(l->sound);
    l->sound = 0;
}

void set_channel(uint16_t c) {
    switch_channels[uint8_t(c)] = 1;
    switch_channels_time[uint8_t(c)] = GameState.frame;
}

SCRIPTINFO* play(uint32_t hash, _VECTOR* pos, _VECTOR* rot, uint8_t loop,
                 void (*callback)(obj_tag*, uint16_t, void*) = nullptr, void* user = nullptr) {
    SCRIPTINFO* si = Script_Load(hash, pos, rot, nullptr, nullptr, callback, user);
    Script_Play(si, loop);
    return si;
}

}  // namespace

// [decomp] 0x001aa9e8 SpaceLaser_Register
// O evento 15 do script de carga: a boca do laser é a posição do objeto do stream.
void SpaceLaser_Register(obj_tag* obj, uint16_t, void* user) {
    if (user == nullptr) return;
    std::memcpy(user, &obj->matrix.m[3][0], sizeof(_VECTOR));
}

// [decomp] 0x001aa8b8 Create_SpaceLaser
// O laser (sem modelo): os três scripts, os segundos da contagem e do fim, os canais e 10 s até a primeira carga.
obj_tag* Create_SpaceLaser(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    obj_tag* obj = Control_CreateObjEx(0x50, pos, rot, nullptr, nullptr, nullptr, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;
    ++g_space_stats.lasers;
    obj->type = 0x50;
    SPACELASER_tag* l = laser_of(obj);
    l->charge[0] = level->params[0];
    l->charge[1] = level->params[1];
    l->final = level->params[2];
    l->countdown = float(level->params[3]);
    l->after = float(level->params[4]);
    l->done = uint16_t(level->params[5]);
    l->stop = uint16_t(level->params[6]);
    l->wait = 10.0f;
    l->enable = uint16_t(level->params[7]);
    obj->state = 0;
    return obj;
}

// [decomp] 0x001aa3f8 SpaceLaser_Update
// Estados (obj_tag::state): 0 esperando (com o canal de ligar: a contagem acabada começa o fim, estado 3; os 10 s
// acabados começam uma carga, estado 1), 1 carregando (o script; o som da metade em diante; acabado, estado 2), 2 o tiro
// (ver space_station.h; volta ao 0), 3 o fim (o script em laço; o canal depois do parâmetro 4), 4 apagado (pelo canal
// do parâmetro 6).
void SpaceLaser_Update(obj_tag* obj) {
    SPACELASER_tag* l = laser_of(obj);
    if (channel_on(l->enable)) l->countdown = l->countdown - REC_FRAME_RATE;
    if (obj->state != 4 && l->stop != 0 && switch_channels[uint8_t(l->stop)] != 0) obj->state = 4;
    _VECTOR zero = CONSTANT_ZERO_VECTOR;
    switch (int16_t(obj->state)) {
        case 0: {
            if (!channel_on(l->enable)) break;
            stop_sound(l);
            l->wait = l->wait - REC_FRAME_RATE;
            if (l->countdown < 0.0f) {
                l->script = play(l->final, &zero, &zero, 1);
                Script_Update(l->script);
                obj->state = 3;
            } else if (l->wait < 0.0f) {
                const uint32_t r = Rand_Random();
                l->script = play(l->charge[r & 1], &zero, &zero, 0, SpaceLaser_Register, l);
                Script_Update(l->script);
                if (obj_tag* p = glb_players[0]; p != nullptr) {
                    l->target = p->pos;
                } else {
                    ++g_space_stats.guards;
                }
                ++g_space_stats.charges;
                obj->state = 1;
            }
            break;
        }
        case 1: {
            Script_Update(l->script);
            if (l->script != nullptr && float(uint32_t(l->script->end >> 1)) < l->script->time && l->sound == 0) {
                l->sound = Sound_Play(0x5de, 100.0f, 0, 0);
            }
            if (Script_IsPlaying(l->script) == 0) {
                obj->state = 2;
                l->script = nullptr;
            }
            break;
        }
        case 2: {
            _VECTOR dir{l->target.x - l->origin.x, l->target.y - l->origin.y, l->target.z - l->origin.z,
                        l->target.w - l->origin.w};
            Vec_Normalise(&dir, &dir);
            const _VECTOR end{l->origin.x + dir.x * 1000.0f, l->origin.y + dir.y * 1000.0f,
                              l->origin.z + dir.z * 1000.0f, l->origin.w};
            HITDATA_tag* hits = nullptr;
            if (Collide_RayIntersect(&l->origin, &end, obj->cel, obj, nullptr, &hits, 0, 8, 0)) l->target = hits->pos;
            Collide_FreeHitList(&hits);
            Sound_Play3D(0x46a, &l->origin, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            Bullet_init(-1, obj, nullptr, weapon_def(0x6b), &l->origin, &dir, 0xffff);
            ++g_space_stats.shots;
            if (obj_tag* beam = control_create_object(0x40, &l->origin, nullptr, nullptr); beam != nullptr) {
                auto* d = static_cast<uint8_t*>(beam->data);
                beam->type = 0x10;
                const uint16_t life = 5;
                std::memcpy(d + 0x30, &life, 2);
                d[0x32] = 0;
                hashtable_set_object_to_entity_gfx(beam, 0x20005cd);
                beam->unknown_103[3] = uint8_t(Rand_Rand(0x80) + 0x7f);
                control_link_object_to_cel(beam, build_FindCel(&l->origin, glb_world));
                PositionBeam(beam, &l->origin, &l->target);
            }
            Emitter_Create(0xc000010, &l->target, const_cast<_VECTOR*>(&CONSTANT_UP1), 0.0f, 0, 0, 0, 0.0f);
            l->wait = 10.0f;
            obj->state = 0;
            stop_sound(l);
            break;
        }
        case 3:
            Script_Update(l->script);
            l->after = l->after - REC_FRAME_RATE;
            if (l->after < 0.0f && l->done != 0) set_channel(l->done);
            stop_sound(l);
            break;
        case 4:
            stop_sound(l);
            obj->life |= 1;
            break;
        default:
            break;
    }
}

// [decomp] 0x001a9ef0 Create_SpaceMissile
// O míssil (sem modelo): os scripts (o do silo, parâmetro 0, toca já; os de acompanhamento, 8-11, também), os canais e
// a contagem; zera MissileDeploy.
obj_tag* Create_SpaceMissile(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    obj_tag* obj = Control_CreateObjEx(0x28, pos, rot, nullptr, nullptr, nullptr, 1, 0, 1.0f, 0, 0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;
    ++g_space_stats.missiles;
    SPACEMISSILE_tag* m = missile_of(obj);
    obj->type = 0x4f;
    m->launch = level->params[1];
    m->sabotage = level->params[2];
    m->delay = float(level->params[3]);
    m->index = uint16_t(level->params[4]);
    m->launched = uint16_t(level->params[5]);
    m->sabotaged = uint16_t(level->params[6]);
    m->enable = uint16_t(level->params[7]);
    m->script = play(level->params[0], pos, rot, 0);
    for (int k = 0; k < 4; ++k) {
        if (level->params[8 + k] != 0) m->extra[k] = play(level->params[8 + k], pos, rot, 0);
    }
    obj->state = 0;
    Script_Update(m->script);
    std::memset(MissileDeploy, 0, sizeof(MissileDeploy));
    return obj;
}

// [decomp] 0x001aa118 SpaceMissile_Update
// Estados (obj_tag::state, os casos da tabela de saltos 0x002fcc30): 0 esperando (com o canal de ligar, a contagem;
// acabada, armado), 1 armado (o script do silo até a pausa dele), 2 a decisão (sabotado ou lançado; obj_tag::unknown_f6
// 0 sabotado, 1 lançado), 3 o desfecho (o script até o fim; então o canal), 4 acabado. Nos estados 1-3 os scripts de
// acompanhamento andam também. Sem script nada acontece.
void SpaceMissile_Update(obj_tag* obj) {
    SPACEMISSILE_tag* m = missile_of(obj);
    if (m->script == nullptr) return;
    const int16_t state = int16_t(obj->state);
    if (state != 0 && state != 4) {
        for (SCRIPTINFO* s : m->extra) {
            if (s != nullptr) Script_Update(s);
        }
    }
    const int16_t index = int16_t(m->index - 1);
    switch (int16_t(obj->state)) {
        case 0:
            if (m->enable != 0 && switch_channels[uint8_t(m->enable)] == 0) return;
            m->delay = m->delay - REC_FRAME_RATE;
            if (m->delay < 0.0f) {
                obj->state = 1;
                if (index >= 0 && index < 8) MissileDeploy[index] = 1;
                ++g_space_stats.armed;
            }
            break;
        case 1:
            Script_Update(m->script);
            if (m->script->wait != 0) obj->state = 2;
            break;
        case 2: {
            const bool sabotaged = switch_channels[uint8_t(m->index)] != 0;
            Script_Free(m->script);
            m->script = play(sabotaged ? m->sabotage : m->launch, &obj->pos, &obj->rot, 0);
            Script_Update(m->script);
            obj->unknown_f6 = sabotaged ? 0 : 1;
            obj->state = 3;
            if (index >= 0 && index < 8) MissileDeploy[index] = sabotaged ? 2 : 3;
            ++(sabotaged ? g_space_stats.sabotaged : g_space_stats.launched);
            break;
        }
        case 3:
            Script_Update(m->script);
            if (Script_IsPlaying(m->script) != 0) return;
            if (obj->unknown_f6 == 0) {
                if (m->sabotaged != 0) set_channel(m->sabotaged);
            } else if (m->launched != 0) {
                set_channel(m->launched);
            }
            obj->state = 4;
            m->script = nullptr;
            break;
        default:
            break;
    }
}

void space_station_debug_power_on() {
    std::memset(MissileDeploy, 0, sizeof(MissileDeploy));
    g_space_stats = {};
}
