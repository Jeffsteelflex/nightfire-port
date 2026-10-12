// Port de parsemap_create_dynamic_objects e dos *_Create já portados. Ver objects.h.

#include "engine/lights.h"
#include "engine/objects.h"

#include <cstring>

#include "engine/ainetwork.h"
#include "engine/break.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/emitter.h"
#include "engine/gunimp.h"
#include "engine/hazard.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_traverse.h"
#include "engine/rigid_body.h"
#include "engine/scenery.h"
#include "engine/script.h"
#include "engine/searchlight.h"
#include "engine/sensor.h"
#include "engine/ssys.h"
#include "engine/turret.h"
#include "engine/copter.h"
#include "engine/simple_script.h"
#include "engine/space_station.h"
#include "engine/trigger.h"
#include "engine/weather.h"
#include "platform/pending.h"

ENV_tag* GlobalEnv = nullptr;
_VECTOR SunVector;
uint32_t g_dynamic_object_calls = 0;

namespace {

int16_t p16(const level_tag* level, int k) { return static_cast<int16_t>(level->params[k]); }
uint8_t p8(const level_tag* level, int k) { return static_cast<uint8_t>(level->params[k]); }

bool multiplayer() {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    return mp != 0;
}

}  // namespace

// [decomp] 0x0018dbf8 DynamicObject_Create
obj_tag* DynamicObject_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0xc, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    auto* data = static_cast<int16_t*>(obj->data);
    obj->type = 0x52;
    obj->flags = glist->flags;
    const int16_t first = p16(level, 0);
    data[0] = first;
    data[1] = p16(level, 1);
    std::memcpy(data + 2, &obj->flags, 4);
    data[4] = p16(level, 2);
    reinterpret_cast<uint8_t*>(data)[10] = p8(level, 3);
    if (first != 0) obj->flags |= 0x30;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x0012c230 Break_Create
// Objeto quebrável: parâmetros 0 (u32), 1 (convertido para float) e 2 (u16 negado, 0 se zero).
obj_tag* Break_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, level_tag* level) {
    obj_tag* obj = control_create_object(0xc, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->type = 0x20;
    auto* data = static_cast<uint32_t*>(obj->data);
    Control_SetGList(obj, glist);
    build_LinkToRoom(obj, 0, glb_world);
    data[0] = level->params[0];
    const float strength = float(level->params[1]);
    std::memcpy(&data[1], &strength, 4);
    const uint16_t mask = level->params[2] == 0 ? 0 : static_cast<uint16_t>(~level->params[2]);
    std::memcpy(&data[2], &mask, 2);
    return obj;
}

// [decomp] 0x0017b968 Env_Create
// Ambiente da sala que contém o ponto (uma vez por sala): chuva/neve, vento e a direção do sol.
// Não cria objeto: o modelo do posicionamento não é desenhado.
void Env_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag*, level_tag* level) {
    cel_tag* cel = build_FindCel(pos, glb_world);
    if (cel == nullptr || cel->env != nullptr) return;
    auto* env = static_cast<ENV_tag*>(Mem_Malloc(0x50, 0x104, 0));
    if (env == nullptr) return;
    env->kind = p8(level, 0);
    env->unknown_4c = env->kind == 0 ? p8(level, 1) : 0;
    env->unknown_4d = 1;
    env->unknown_4e = p8(level, 2);
    env->unknown_30 = float(level->params[3]) * 0.1f;
    env->unknown_38 = -1.0f;
    env->unknown_34 = float(level->params[3]) * 0.1f;
    env->unknown_48[0] = p8(level, 4);
    env->unknown_48[1] = p8(level, 5);
    env->unknown_48[2] = p8(level, 6);
    env->unknown_3c = float(level->params[7]);
    env->unknown_40 = float(level->params[8]);
    env->inv_radius = 1.0f / cel->glist->sphere.w;
    env->pos = *pos;
    cel->env = env;
    GlobalEnv = env;
    MATRIX m;
    RotMatrix(rot, &m);
    Mat_GetDir(&env->dir, &m);
    // dir x up com up = (0, 1, 0), no registro de dir (w = dir.w).
    const _VECTOR& d = env->dir;
    const _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    env->side = {d.y * up.z - up.y * d.z, d.z * up.x - up.z * d.x, d.x * up.y - up.x * d.y, d.w};
    InitDrops(env->kind);
    Vec_Zero(&SunVector);
    if (level->params[9] != 0) {
        Vec_Spherical_2_Cartesian(&SunVector, 1000.0f, float(level->params[10]) * 0.017453294f,
                                  -(float(level->params[11]) * 0.017453294f));
    }
}

// [decomp] 0x001d04f0 parsemap_create_dynamic_objects
// Cria o objeto de um posicionamento dinâmico pelo tipo (flags). Quase todos só existem na fase
// principal (main_map); os tipos ainda não portados passam por pending_create.
void parsemap_create_dynamic_objects(_TARG23_PLACEMENT* pl, level_tag* level, uint32_t type, celglist_tag* glist,
                                     framelist_tag*, celglist_tag* next_glist, uint8_t main_map) {
    ++g_dynamic_object_calls;
    _VECTOR pos{pl->pos[0], pl->pos[1], pl->pos[2], 0.0f};
    _VECTOR rot{pl->rot[0], pl->rot[1], pl->rot[2], 0.0f};
    const auto pending = [&](const char* name) { pending_create(name, pos, rot, glist, type, level); };
    // Só do port: --no-env (depuração) deixa o tempo e os enfeites provisórios, como antes do porte deles (sem os
    // sorteios que mudam as capturas e as provas do nf_loader_check).
    const bool old_scenery = g_port_no_env != 0;
    if (g_record_placements) {
        // Só no port (auditoria da campanha, nf_loader_check).
        PendingPlacement all{};
        all.type = type;
        all.pos = pos;
        std::memcpy(all.params, level->params, sizeof(all.params));
        g_all_placements.push_back(all);
    }
    switch (type) {
        case 0x0f: if (main_map) Drone_Create(&pos, &rot, level); break;
        case 0x20:
            if (level->params[0] == 2) psiCreateBreakableData(glist);
            if (main_map) Break_Create(&pos, &rot, glist, level);
            break;
        case 0x21:
            if (main_map) {
                if (old_scenery) pending("Ripples_Create"); else Ripples_Create(&pos, &rot, level, glist);
            }
            break;
        case 0x22: if (main_map) Ladder_Create(&pos, &rot, level, glist); break;
        case 0x24:
            if (main_map && !multiplayer()) Player_AddNewStartPos(&pos, &rot, 1, level);
            break;
        case 0x25:
            if (main_map && multiplayer()) pending("MP_RegisterSpawnPoint");
            break;
        case 0x26:
            if (main_map && multiplayer()) pending("MP_RegisterMPObject");
            break;
        case 0x27: if (main_map) GT_Create(&pos, &rot, nullptr, level, glist, nullptr); break;
        case 0x28: if (main_map) Sensor_Create(&pos, &rot, level, glist); break;
        case 0x29: if (main_map) Switch_Create(&pos, &rot, level, glist); break;
        case 0x2a:
            if (main_map) {
                View_AddSkyObj(uint16_t(level->params[0]), glist, &pos, &rot, nullptr, int8_t(level->params[1]),
                               uint16_t(level->params[2]), uint16_t(level->params[3]), int8_t(level->params[4]));
            }
            break;
        case 0x2b:
            if (main_map) {
                if (old_scenery) pending("LeafGen_Create"); else LeafGen_Create(&pos, &rot, level, glist);
            }
            break;
        case 0x2c:
            if (main_map) {
                if (old_scenery) pending("Lightning_Create"); else Lightning_Create(&pos, &rot, level, glist);
            }
            break;
        case 0x2d:
            if (main_map && !multiplayer()) Player_AddNewStartPos(&pos, &rot, 0, level);
            break;
        case 0x2e: if (main_map) Lock_Create(&pos, &rot, level, glist); break;
        case 0x2f: if (main_map) Copter_Create(&pos, &rot, level); break;
        case 0x30: if (main_map) Monitor_Create(&pos, &rot, level, glist, next_glist); break;
        case 0x31: if (main_map) FuseBox_Create(&pos, &rot, level, glist); break;
        case 0x32:
            if (main_map) {
                if (old_scenery) pending("RainBox_Create"); else RainBox_Create(&pos, &rot, level, glist);
            }
            break;
        case 0x33: if (main_map) Hint_Create(&pos, &rot, level, glist); break;
        case 0x34:
            if (main_map) {
                quaternion_tag q;
                std::memcpy(&q, pl->quat, sizeof(q));
                GunImp_Create(&pos, &q, glist, level);
            }
            break;
        case 0x35: if (main_map) PCQWorm_Create(&pos, &rot, level, glist); break;
        case 0x36: if (main_map) pending("Sub_Create"); break;
        case 0x37: if (main_map) pending("MiniSub_Create"); break;
        case 0x38: if (main_map) Create_SpaceLaser(&pos, &rot, level); break;
        case 0x39: if (main_map) Create_SpaceMissile(&pos, &rot, level); break;
        case 0x3a: if (main_map) Grapple_Create(&pos, &rot, level, glist); break;
        case 0x3b: if (main_map) DynamicObject_Create(&pos, &rot, level, glist); break;
        case 0x3e: if (main_map) Wire_Create(&pos, &rot, level, glist); break;
        case 0x3f: if (main_map) CreepWall_Create(&pos, &rot, level, glist); break;
        case 0x41: if (main_map) InverterTrigger_Create(level); break;
        case 0x4a: if (main_map) Apocalypse_Create(&pos, &rot, level, glist); break;
        case 0x4e:
            // Luz da fase: raio (inteiro), cor, canal, pisca-pisca e som nos parâmetros.
            if (main_map) {
                Light_Create(&pos, uint8_t(level->params[1]), uint8_t(level->params[2]), uint8_t(level->params[3]),
                             float(level->params[0]), uint16_t(level->params[4]), -1, 0, 1.0f,
                             uint16_t(level->params[5]), uint16_t(level->params[6]), uint16_t(level->params[7]),
                             level->params[8]);
            }
            break;
        case 0x71:
            if (main_map) {
                if (old_scenery) pending("rotor_init"); else rotor_init(&pos, &rot, glist, 0, uint8_t(level->params[0]), uint8_t(level->params[1]));
            }
            break;
        case 0xd2: if (main_map) Shooter_Create(&pos, &rot, level, glist); break;
        case 0xd4:
            if (main_map) {
                if (old_scenery) pending("Cloud_Create"); else Cloud_Create(&pos, &rot, level, glist);
            }
            break;
        case 0xd5:
            if (main_map) {
                if (old_scenery) pending("OneSided_Create"); else OneSided_Create(&pos, &rot, glist);
            }
            break;
        case 0xd8: if (main_map) RB_Create(&pos, &rot, glist, level); break;
        case 0xd9: if (main_map) SP_CreateScriptPlayer(&pos, &rot, level, nullptr, nullptr, nullptr); break;
        case 0xda: if (main_map) pending("Car_Create"); break;
        case 0xdb: if (main_map) Door_Create(&pos, &rot, level, glist); break;
        case 0xdc: if (main_map) Trigger_Create(&pos, &rot, level, glist); break;
        case 0xde: Searchlight_Create(&pos, &rot, level, glist); break;  // também fora da fase principal
        case 0xdf: if (main_map) Env_Create(&pos, &rot, glist, level); break;
        case 0xe0: if (main_map) pending("Creature_Create"); break;
        case 0xe1: if (main_map) Destroy_Create(&pos, &rot, level, glist, next_glist); break;
        case 0xe2: if (main_map) SS_Create(&pos, &rot, level, glist); break;
        case 0xe3: if (main_map) Flicker_Create(&pos, &rot, level, glist, nullptr); break;
        case 0xe4: if (main_map) Hurt_Create(&pos, &rot, level, glist); break;
        case 0xe5: if (main_map) Drone_CoverCornerNode(&pos, &rot, level); break;
        case 0xe6: if (main_map) Drone_CoverLowNode(&pos, &rot, level); break;
        case 0xe7: if (main_map) ThirdCam_Create(&pos, &rot, level, glist); break;
        case 0xe8: if (main_map) Trigger_LoadLevelCreate(&pos, &rot, level, glist); break;
        case 0xe9: if (main_map) Drone_AIPoint(&pos, &rot, level); break;
        case 0xea: if (main_map) Trigger_TouchOnce(&pos, &rot, level, glist); break;
        case 0xeb: if (main_map) Trigger_Touch(&pos, &rot, level, glist); break;
        case 0xec: if (main_map) Trigger_MultiplexIn(&pos, &rot, level, glist); break;
        case 0xed: if (main_map) Trigger_MultiplexSIn(&pos, &rot, level, glist); break;
        case 0xee: if (main_map) Trigger_MultiplexOut(&pos, &rot, level, glist); break;
        case 0xef: if (main_map) Trigger_MultiplexOrIn(&pos, &rot, level, glist); break;
        case 0xf0:
            // Item: tipo, item, quantidade, canal, som, reaparecimento e texto nos parâmetros 0-6
            // (posicionado: não cai; sem vida útil).
            if (main_map) {
                Pickup_Create(&pos, &rot, nullptr, glist, uint16_t(level->params[0]), uint16_t(level->params[1]),
                              level->params[2], level->params[3], level->params[4], 1, uint16_t(level->params[5]),
                              0, level->params[6]);
            }
            break;
        case 0xf1: if (main_map) DroneSpawner_Create(&pos, &rot, level); break;
        case 0xf2:
            if (main_map) {
                // A matriz vem do quatérnio do posicionamento (não de rot), na posição dele; a w
                // da translação é 1 (lixo da pilha no original, ver emitter.cpp).
                quaternion_tag q;
                std::memcpy(&q, pl->quat, sizeof(q));
                MATRIX m{};
                Quat_QuatToMat(&q, &m);
                m.m[3][0] = pl->pos[0];
                m.m[3][1] = pl->pos[1];
                m.m[3][2] = pl->pos[2];
                m.m[3][3] = 1.0f;
                Emitter_CreatePlist(&m, level);
            }
            break;
        case 0xf3:
            if (main_map) {
                if (old_scenery) pending("Env_Tree_Create"); else Env_Tree_Create(&pos, &rot, level);
            }
            break;
        case 0xf4: if (main_map) Trigger_MoviePlayer(&pos, &rot, level, glist); break;
        case 0xf5:
        case 0xf8:
            if (main_map) {
                _VECTOR scale{pl->scale[0], pl->scale[1], pl->scale[2], 0.0f};
                Drone_AIVolume_Create(&pos, &rot, &scale, level, glist);
            }
            break;
        case 0xf9: if (main_map) SoundTrigger_Create(&pos, &rot, level, glist); break;
        case 0xfa: if (main_map) ThirdIcon_Create(&pos, &rot, level, glist); break;
        case 0xfb: if (main_map) MusicTrigger_Create(&pos, &rot, level, glist); break;
        case 0xfc: if (main_map) Corona_Create(&pos, &rot, level, glist); break;
        case 0xfd: if (main_map) CamSubject_Create(&pos, &rot, level, glist); break;
        case 0xfe: if (main_map) Mine_Create(&pos, &rot, level, glist); break;
        default: break;
    }
}
