// Port do núcleo dos personagens da IA: criação (Drone_Create), carga (Drone_PreLoad_Init,
// Drone_LevelReset, Drone_PostLoad_Init), inicialização pelos parâmetros do posicionamento
// (NDrone2_DefaultInit), controle por tick (Drone_Control) e remoção (Drone_Delete). Ver drone.h.
//
// Divergências intencionais:
// - NDrone2_DefaultInit lê a arma da animação sem conferir o objeto de animação (que é nulo quando a
//   pele não carregou): aqui a leitura é protegida.
// - Drone_PostLoad_Init cria o objeto 0x30 com posição e rotação zeradas; o original deixa o w das
//   duas no que estiver na pilha.
// - NDrone2_ControlSTANDARD: o w da posição e da direção do bafo (Env_Breath) vem de registradores
//   não inicializados no original; aqui fica o w da posição do osso e 0.

#include "engine/drone.h"

#include <cmath>
#include <cstring>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/npc.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/psi.h"

uint8_t Drone_DummyPlayer = 0;
uint8_t Drone_DebugSpr = 0;

namespace {

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

template <class T>
T& at(void* base, uint32_t offset) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

Drone_tag* drone_of(obj_tag* obj) { return static_cast<Drone_tag*>(obj->data); }

}  // namespace

LLISTINFO_tag* Drone_List() { return &(&NPCGlobals)->drones; }

// ---- criação ----

// [decomp] 0x00136be0 Drone_Create
// Só cria na dificuldade mínima do posicionamento (parâmetro 2) ou acima; guarda os parâmetros no
// DIVars_tag do personagem para NDrone2_DefaultInit, chamado depois da carga.
obj_tag* Drone_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    if (Drone_bDisableSystem != 0 || GameState.difficulty < level->params[2]) return nullptr;
    DIVars_tag vars;
    std::memset(&vars, 0, sizeof(vars));
    vars.pos = *pos;
    vars.rot = *rot;
    amemcpy(vars.params, level->params, 0x84);
    vars.owner = nullptr;
    vars.obj = nullptr;
    obj_tag* obj = NDrone2_CreateObj(&vars);
    if (obj == nullptr) return nullptr;
    Drone_tag* d = drone_of(obj);
    vars.obj = obj;
    obj->created = Rand_Rand(10000);
    d->divars.obj = obj;
    d->divars.pos = vars.pos;
    d->divars.rot = vars.rot;
    amemcpy(d->divars.params, vars.params, 0x84);
    d->group_id = vars.params[4];
    d->param7 = vars.params[7];
    return obj;
}

// [decomp] 0x0014d7c8 NDrone2_CreateObj
// Peculiaridade mantida: com dono, o original passa o dono no argumento do modelo de
// Control_CreateObjEx e deixa o dono nulo (nenhum caminho portado cria personagens com dono).
obj_tag* NDrone2_CreateObj(DIVars_tag* vars) {
    obj_tag* obj;
    if (vars->owner == nullptr) {
        obj = control_create_object(0xd20, nullptr, nullptr, nullptr);
        if (obj == nullptr) return nullptr;
        obj->type = 2;
    } else {
        obj = Control_CreateObjEx(0xd20, &vars->pos, &vars->rot, nullptr, reinterpret_cast<celglist_tag*>(nf::raw(vars->owner)),
                                  nullptr, 1, 0x44, 1.0f, 0, 0xff, 0xff, 0xff);
        if (obj == nullptr) return nullptr;
        obj->type = 0x4d;
    }
    Drone_tag* d = drone_of(obj);
    if (d == nullptr) return nullptr;
    d->obj = obj;
    LList_Add(Drone_List(), &d->node);
    return obj;
}

// ---- carga ----

// [decomp] 0x00136888 Drone_PreLoad_Init
void Drone_PreLoad_Init() {
    NPCGLOBALS_tag* n = (&NPCGlobals);
    LList_Init(&n->drones, 0xd20, 0);
    LList_Init(&AIBoxList, 0x120, 0);
    n->cover_nodes = CoverNodes;
    n->ai_points = AIPoints;
    n->alert.cel = nullptr;
    n->emitter.dist = nullptr;
    n->cover_next = 0;
    n->cover_count = 0;
    n->ai_point_count = 0;
    n->unknown_1c68 = 0;
    n->alert.frame = 0;
    n->alert.param[2] = 0.0f;
    for (int i = 0; i < 31; ++i) {
        n->spawners[i] = nullptr;
        n->groups[i] = nullptr;
        n->group_counts[i] = 0;
    }
}

// [decomp] 0x0013a5a8 Drone_LevelReset
void Drone_LevelReset() {
    std::memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    if (Drone_bDisableSystem != 0) return;
    NPCGLOBALS_tag* n = (&NPCGlobals);
    n->corpse_time = FRAME_RATE_INT * 10;
    n->enabled = 1;
    n->unknown_1c70 = 0.5f;
    n->unknown_14c = 3;
    n->unknown_1c74 = 0;
    n->alarm_said = 0;
    n->sm_next_id = 0;
    Drone_SM_Init();
}

// [decomp] 0x00136938 Drone_PostLoad_Init
// Depois da carga: ajustes da rede de navegação, caminhos dos pontos de cobertura e de IA, o objeto
// 0x30 do "jogador falso", os grupos e a inicialização de cada personagem.
void Drone_PostLoad_Init() {
    if (Drone_bDisableSystem != 0) return;
    NPCGLOBALS_tag* n = (&NPCGlobals);
    n->postload_frame = GameState.frame;
    n->player_target.frame = 0;
    n->alert_target.frame = 0;
    if (GameState.level_file == 0x7000016) switch_channels[0x96] = 0;
    const bool l08 = GameState.level_file == 0x7000008;
    AINetwork.max_slope = l08 ? 0.7853982f : 0.8726646f;  // 0x3f490fdb / 0x3f5f66f4
    AINetwork.slope_height = l08 ? 1.5f : 2.0f;
    AINetwork.max_height = 4.0f;
    AINetwork.unknown_10 = 30.0f;
    AINetwork.max_dist_sq = 900.0f;
    std::memset(&n->emitter, 0, sizeof(n->emitter));
    if (AINetwork.max_nodes != 0) AINetwork_AllocEmitter(&n->emitter, AINetwork.max_nodes);
    for (uint32_t i = 0; i < n->cover_count; ++i) AINetwork_EmitPath(&n->cover_nodes[i].emitter, 0.0f);
    for (uint32_t i = 0; i < n->ai_point_count; ++i) AINetwork_EmitPath(&n->ai_points[i].emitter, 0.0f);
    Drone_DummyPlayer = 0;
    obj_tag* dummy = control_create_object(0x3c, nullptr, nullptr, nullptr);
    if (dummy != nullptr && dummy->data != nullptr) {
        control_init_object(dummy);
        _VECTOR zero{0.0f, 0.0f, 0.0f, 0.0f};
        Vec_Copy2(&zero, &dummy->pos, &dummy->old_pos);
        Vec_Copy2(&zero, &dummy->rot, &dummy->old_rot);
        dummy->type = 0x30;
        dummy->cel = build_FindCel(&dummy->pos, glb_world);
        n->dummy_player_unknown = 0;
        n->dummy_player = dummy;
    }
    NDrone2_SetupGroups();
    for (LLNODE_tag* node = n->drones.first; node != nullptr; node = node->next) {
        NDrone2_PostLoad_Init(reinterpret_cast<Drone_tag*>(node)->obj);
    }
    NDrone_InitDoorNodes();
    NDrone_InitKickNodes();
    AINetwork_InitPassableBoundries();
}

// [decomp] 0x0014d890 NDrone2_PostLoad_Init
void NDrone2_PostLoad_Init(obj_tag* obj) {
    Drone_tag* d = drone_of(obj);
    d->created = GameState.frame;
    NDrone2_DefaultInit(&d->divars);
    NDrone2_FindOpponent(d);
    Drone_GetOpponentInfo(d, obj);
    d->unknown_2d = 0;
    d->head_target = d->opponent;
    d->route.path = AINetwork_NavPathForPosition(&obj->pos, obj->cel, 0);
    Drone_SM_InitObject(obj);
    if (bots_on()) BOTWEAP_changeWeapon(d, static_cast<uint8_t*>(d->bot)[0x768]);
}

// [decomp] 0x00149ca0 NDrone2_SetupGroups
uint32_t NDrone2_SetupGroups() {
    NPCGLOBALS_tag* n = (&NPCGlobals);
    for (LLNODE_tag* node = n->drones.first; node != nullptr; node = node->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(node);
        if (d->group_id != 0) NDrone2_AddToGroup(d, d->group_id);
    }
    for (int i = 0; i < 31; ++i) {
        if (n->spawners[i] != nullptr && n->group_counts[i] != 0) DroneSpawner_Init(n->spawners[i]);
    }
    return 1;
}

// [decomp] 0x001412e0 NDrone2_AddToGroup
// Sem conferir o índice (o original também não): grupos acima de 30 escreveriam fora da tabela.
uint32_t NDrone2_AddToGroup(Drone_tag* drone, uint32_t group) {
    NPCGLOBALS_tag* n = (&NPCGlobals);
    if (n->groups[group] != nullptr) {
        drone->group_next = n->groups[group];
        n->groups[group] = drone;
        ++n->group_counts[group];
        drone->group = group;
        return 1;
    }
    n->groups[group] = drone;
    ++n->group_counts[group];
    drone->group = group;
    drone->group_next = nullptr;
    return 1;
}

// [decomp] 0x00149dd0 NDrone2_Enable
void NDrone2_Enable(int8_t on, Drone_tag* drone) {
    if (on == 0) {
        if (drone->script != nullptr) drone->script->speed = 0.0f;
        drone->obj->flags |= 0x10;
        drone->obj->flags |= 0x20;
        drone->flags &= 0xffe7fe5f;
    } else if ((drone->flags & 0x200) == 0) {
        if (drone->script != nullptr) drone->script->speed = 1.0f;
        drone->obj->flags &= ~0x10u;
        drone->obj->flags &= ~0x20u;
        drone->flags |= 0x801a0;
    }
}

// [decomp] 0x0014d928 DroneInit_TakeXHits
// Vida para morrer com "hits" tiros da arma (dano em weapon_data + 0xc).
float DroneInit_TakeXHits(uint32_t weapon, float hits) {
    if (weapon < 0x48) {
        float damage;
        std::memcpy(&damage, weapon_data + weapon * 0x10c + 0xc, 4);
        return damage * hits;
    }
    return hits;
}

// [decomp] 0x0014b1e8 DroneInit_Collision
// Põe o personagem 1 m acima do chão que um raio de 1 m acima a 25 m abaixo encontrar.
void DroneInit_Collision(Drone_tag* drone) {
    obj_tag* obj = drone->obj;
    cel_tag* cel = obj->cel;
    if (bots_on() || drone->enter_script != 0 || drone->zero_g != 0 || drone->klass == 0x13 || drone->type == 0x17) {
        return;
    }
    drone->flags |= 0x80000000u;
    _VECTOR from = obj->pos;
    from.y = obj->pos.y + 1.0f;
    _VECTOR to = obj->pos;
    to.y = obj->pos.y - 25.0f;
    P32<HITDATA_tag> hits = nullptr;
    if (Collide_RayIntersect(&from, &to, cel, nullptr, nullptr, &hits, 0, 0x104, 0)) {
        to = hits->pos;
        anim_of(obj)->flags |= 8;
        obj->pos.y = to.y + 1.0f;
        Collide_FreeHitList(&hits);
    }
}

// [decomp] 0x0014aed8 DroneFunc_SetAsAttacking
void DroneFunc_SetAsAttacking(Drone_tag* drone) {
    drone->channel_state = 0;
    drone->flags |= 0x2000000;
    drone->channel_b = 0;
    if (drone->attack_time == 0) drone->attack_time = GameState.frame;
    drone->flags |= 0x4000000;
    drone->alert = 1.0f;
    // Nas fases 07000009 e 0700000a o alcance não sobe.
    const uint32_t level = GameState.level_file;
    if (level < 0x700000b && 0x7000008 < level) return;
    if (drone->range_e8 < drone->range_fc) drone->range_e8 = drone->range_fc;
    drone->view_angle = 0.0f;
}

// [decomp] 0x0014b300 NDrone2_DefaultInit
// Aplica os parâmetros do posicionamento: classe e subclasse pela pele, arma, aparência, alcances,
// canais, modo (tabela antiga ou estrutura de comportamento), capitão pela dificuldade, animação
// inicial, rotas, vida e o chão.
obj_tag* NDrone2_DefaultInit(DIVars_tag* vars) {
    obj_tag* obj = vars->obj;
    Drone_tag* d = drone_of(obj);
    control_init_object(obj);
    Vec_Copy2(&vars->pos, &obj->pos, &obj->old_pos);
    Vec_Copy2(&vars->rot, &obj->rot, &obj->old_rot);
    obj->hit_test = &d->hit_test;
    obj->flags &= ~0x20u;
    control_init_collision(obj);
    Vec_Zero(&d->fall);
    build_LinkToRoom(obj, 0, glb_world);
    const uint32_t nodes = AINetwork.max_nodes;
    if (nodes != 0) {
        d->route.list = static_cast<uint16_t*>(Mem_Malloc(nodes << 1, 0x1508, 0));
        d->route2.list = static_cast<uint16_t*>(Mem_Malloc(nodes << 1, 0x4d08, 0));
        std::memset(d->route.list, 0, nodes << 1);
        std::memset(d->route2.list, 0, nodes << 1);
    }
    d->unknown_bd0 = 0.5f;
    d->root_y = 1.0f;
    d->hit_damage_mul = 1.0f;
    d->damage_mul = 1.0f;
    d->prev_root_y = 1.0f;
    d->health = 10.0f;
    d->vulnerable = 1;
    d->ignore_impacts = 0;
    d->ninja_eyes = 0;
    d->root_moved = 0;
    obj->unknown_f6 = 1;
    obj->state = 0;
    d->control_flags = 0;
    d->flags = (d->flags & 0xffea05e2) | 0x801e2;
    std::memset(&d->opponent, 0, 0x140);
    Vec_Zero(&d->fall);
    Vec_Zero(&d->vel);
    d->move_target.pos = obj->pos;
    d->move_target.cel = obj->cel;
    d->sound = 0;
    d->move_angles = obj->rot;
    d->unknown_4ce = 0;
    d->unknown_4a0 = 0.1f / FRAME_RATE_DIV;

    // Classe e subclasse pela pele.
    if (GameState.level_file == 0x7000014 && vars->params[0] == 0x50000b1) vars->params[0] = 0x500008c;
    uint16_t weapon = 1;
    d->skin = vars->params[0];
    const auto set = [&](uint16_t klass, uint16_t sub) {
        d->klass = klass;
        d->subclass = sub;
    };
    switch (d->skin) {
        case 0x5000004: case 0x5000006: case 0x5000008: case 0x500004c: set(3, 0xc); break;
        default: set(0, 0xc); break;
        case 0x500000e: case 0x500000f: set(0x15, 2); d->unknown_15 = 1; break;
        case 0x5000010: case 0x5000011: case 0x5000052: set(0x16, 2); d->unknown_15 = 1; break;
        case 0x5000012: case 0x5000013: case 0x5000014: set(0x11, 10); break;
        case 0x5000015: case 0x5000020: case 0x5000022: case 0x5000023: case 0x5000024:
            set(0xd, 0xe);
            d->unknown_15 = 1;
            break;
        case 0x5000016: case 0x500008f: case 0x50000ac: set(0x14, 0xc); break;
        case 0x5000017: case 0x5000018: set(0x12, 0xe); d->unknown_15 = 1; break;
        case 0x5000019: case 0x5000021: set(0xe, 0xc); break;
        case 0x500001b: case 0x500004f: set(9, 3); break;
        case 0x500001e: set(0xb, 7); weapon = 0; break;
        case 0x5000025: set(5, 0xc); break;
        case 0x5000026: case 0x5000066: vars->params[10] = 6; set(0x10, 0xc); break;
        case 0x500002b: case 0x500002d: case 0x5000034: set(8, 1); d->unknown_15 = 1; break;
        case 0x500002e: case 0x500002f: case 0x5000030: set(2, 0xc); break;
        case 0x5000035: set(0xf, 0xc); d->type = 0x17; break;
        case 0x5000036: case 0x5000037: case 0x5000038: case 0x5000039: case 0x500003a: set(1, 0xc); break;
        case 0x500003b: case 0x500003c: set(0xc, 0xe); d->unknown_15 = 1; d->vulnerable = 0; break;
        case 0x5000053:
            d->vulnerable = 0;
            d->ignore_impacts = 1;
            [[fallthrough]];
        case 0x5000027: case 0x5000028: case 0x5000029: case 0x500002a: case 0x5000031: case 0x5000032:
        case 0x5000033:
            set(0x17, 0xb);
            break;
        case 0x5000054: case 0x500006d: case 0x500006e: case 0x500007e: set(4, 0xc); break;
        case 0x500005e: set(0x13, 0xd); d->unknown_15 = 1; break;
        case 0x5000078: case 0x5000094: case 0x50000ba: d->zero_g = 1; set(0x18, 0x10); break;
        case 0x5000087: case 0x5000090: case 0x5000093: set(6, 0xc); break;
        case 0x500008c: case 0x50000b1: case 0x50000b2: set(7, 0xc); break;
    }

    // Arma (parâmetro 9; 1 se zero).
    if (bots_on()) BOT_setGender(d);
    if (vars->params[9] != 0) weapon = static_cast<uint16_t>(vars->params[9]);
    d->weapon2 = static_cast<uint8_t>(vars->params[10]);
    switch (weapon) {
        default:
            weapon = 1;
            d->clip_size = 0;
            d->clip = 0;
            break;
        case 2: case 6: case 10: case 0xb: case 0xe: case 0x10: case 0x3b: case 0x3c: case 0x3d: case 0x3e:
        case 0x3f: case 0x40:
            d->subclass = d->klass == 0xc ? 8 : 4;
            d->clip = 0xc;
            d->damage_mul = 2.0f;
            d->clip_size = 0xc;
            break;
        case 0x12: case 0x14: case 0x15:
            if (d->klass == 0xb) {
                weapon = 0x14;
            } else {
                d->subclass = 6;
            }
            d->clip = d->clip_size = 0x40;
            break;
        case 0x16: case 0x18: case 0x19: case 0x1a: case 0x1b:
            d->subclass = 5;
            d->clip = d->clip_size = 0x40;
            break;
        case 0x1c: case 0x1d: case 0x2a: case 0x2b: case 0x2c: case 0x2d:
            d->subclass = 9;
            d->clip = d->clip_size = 8;
            break;
        case 0x1e: case 0x1f: case 0x24: case 0x25:
            d->subclass = 0x13;
            d->clip = d->clip_size = 5;
            break;
        case 0x30: case 0x32: case 0x33: case 0x6a: case 0x6e:
            if (d->klass != 0x18) d->subclass = 6;
            d->clip = d->clip_size = 10000;
            break;
    }

    // Aparência (parâmetro 11: tipo << 4 | variante).
    const uint32_t look = vars->params[11];
    d->look = static_cast<uint16_t>(look >> 4);
    d->look_variant = static_cast<uint16_t>(look) & 0xf;
    switch ((look >> 4) & 0xffff) {
        default:
            d->look_variant = 0;
            d->look = 0;
            break;
        case 1: d->look = 0x36; break;
        case 2: d->look = 0x35; break;
        case 4: d->look = 0x34; break;
        case 5: {
            const uint16_t v = d->look_variant;
            d->look_variant = 1;
            d->look = v + 0x3a;
            break;
        }
        case 6:
            d->look = 0x6c;
            d->look_variant = 1;
            break;
    }
    if (GameState.level_file == 0x7000014 && d->look < 0x37 && 0x34 < d->look) d->look = 0x34;

    // Alcances (parâmetro 14).
    d->unknown_ec = 4.0f;
    d->unknown_f8 = 2.0f;
    float a = 0.0f, b = 0.0f;
    bool set_fc = true;
    switch (vars->params[14]) {
        default:
            d->range_f0 = Rand_FRand(4.0f) + 12.0f;
            b = Rand_FRand(4.0f);
            a = d->range_f0 + 1.0f;
            d->range_e8 = 24.0f;
            break;
        case 1:
            d->range_f0 = Rand_FRand(3.0f) + 12.0f;
            b = Rand_FRand(3.0f);
            d->range_e8 = 12.0f;
            a = d->range_f0 + 1.0f;
            break;
        case 2:
            d->range_f0 = Rand_FRand(3.0f) + 12.0f;
            b = Rand_FRand(3.0f);
            a = d->range_f0 + 1.0f;
            d->range_e8 = 16.0f;
            break;
        case 3:
            d->range_f0 = Rand_FRand(6.0f) + 25.0f;
            a = Rand_FRand(6.0f);
            d->range_e8 = 40.0f;
            d->range_fc = a + 100.0f;
            set_fc = false;
            break;
        case 4:
            a = Rand_FRand(8.0f);
            d->range_e8 = 1000.0f;
            d->range_fc = 1000.0f;
            d->range_f0 = a + 75.0f;
            set_fc = false;
            break;
        case 5:
            d->range_f0 = Rand_FRand(4.0f) + 12.0f;
            b = Rand_FRand(4.0f);
            d->range_e8 = 0.0f;
            a = d->range_f0 + 1.0f;
            d->view_angle = 0.0f;
            break;
    }
    if (set_fc) d->range_fc = a + b;
    d->unknown_d0 = 0xf;
    d->view_angle = 1.5707964f;
    d->unknown_d4 = 0;
    if (GameState.level_file == 0x7000011) {
        if (d->subclass != 9) {
            d->range_fc = 9.0f;
            d->range_f0 = 6.0f;
        } else {
            d->range_fc = 8.0f;
            d->range_f0 = 5.0f;
        }
    } else if (GameState.level_file == 0x7000014) {
        if (d->subclass != 5) d->range_f0 = d->subclass == 9 ? 7.0f : 10.0f;
    }
    d->unknown_f4 = d->unknown_ec + Rand_FRand(0.0f);

    // Canais, grupo e script de entrada.
    d->channel_a = static_cast<uint8_t>(vars->params[3]);
    d->channel_b = static_cast<uint8_t>(vars->params[6]);
    d->param12 = static_cast<uint8_t>(vars->params[12]);
    d->channel_a_state = switch_channels[d->channel_a];
    d->channel_b_state = switch_channels[d->channel_b];
    d->mode = static_cast<uint16_t>(vars->params[5]);
    d->channel_state = static_cast<uint16_t>(vars->params[8]);
    d->group_id = vars->params[4];
    d->param7 = vars->params[7];
    d->enter_script = vars->params[13];
    if (d->enter_script != 0) {
        d->script_rot = obj->rot;
        d->script_pos = obj->pos;
        if (d->enter_script != 0x600021f) d->flags |= 0x10;
    }

    // Modo e capitão.
    const uint32_t mode = vars->params[5];
    if (mode < 0x24) {
        NDrone2_DoModeSettingsOLD(d, mode, &vars->params[15], vars);
    } else {
        NDrone2_DoModeSettingsNEW(d, mode, &vars->params[15], vars);
    }
    d->captain = 0;
    if (behaviour_util_getProperty(9, d->behaviour) != 0 && GameState.difficulty == 1) d->captain = 1;
    if (behaviour_util_getProperty(5, d->behaviour) != 0 && GameState.difficulty == 2) d->captain = 1;
    if (behaviour_util_getProperty(8, d->behaviour) != 0 && GameState.difficulty == 3) d->captain = 1;
    if (d->captain != 0) {
        switch (d->klass) {
            case 1: d->skin = 0x5000038; break;
            case 2:
                d->ninja_eyes = 1;
                d->skin = 0x500002f;
                break;
            case 3: d->skin = 0x5000006; break;
            default: d->captain = 0; break;
            case 6: d->skin = 0x5000093; break;
            case 7: d->skin = 0x50000b2; break;
        }
        if (d->captain != 0) {
            const float inv_accuracy = 1.0f / DroneCaptain_Mod_BulletAccuracy;
            d->damage_mul = DroneCaptain_Mod_BulletDamage;
            d->health = d->health * DroneCaptain_Mod_Health;
            d->stats[0] = static_cast<uint8_t>(static_cast<int>(float(d->stats[0]) * inv_accuracy));
        }
    }
    AnimObjectNew(obj, d->skin);
    d->armour = 0;
    if (d->behaviour_type == 0xc) {
        behaviour_util_setProperty(0x14, d->behaviour, 0);
        behaviour_util_setProperty(0x15, d->behaviour, 0);
        behaviour_util_setProperty(0x16, d->behaviour, 0);
        if (d->klass == 9) behaviour_util_setProperty(0x4c, d->behaviour, 1);
    }

    // Rotas.
    d->active_route = nullptr;
    d->hit_test.type = 0x800;
    AINetwork_ResetRoute(&d->route, obj, nullptr);
    d->route.path = nullptr;
    d->route.arrive = 2.0f;
    d->route.creep_max = 2.0f;
    if (d->zero_g != 0) {
        d->route.flags |= 0x80;
        d->route.creep_max = 4.0f;
    }
    AINetwork_ResetRoute(&d->route2, obj, nullptr);
    d->route2.arrive = 0.5f;
    d->route2.creep_max = 2.0f;
    d->route2.path = nullptr;
    if (d->zero_g != 0) {
        d->route2.flags |= 0x80;
        d->route2.creep_max = 4.0f;
    }
    for (uint32_t off : {0xac0u, 0xab0u, 0xaa0u, 0xa80u, 0xad4u, 0xa84u}) at<uint32_t>(d, off) = 0;
    at<uint16_t>(d, 0xad0) = 0;
    at<uint16_t>(d, 0xad2) = 0;
    for (uint32_t off : {0xa98u, 0xa94u, 0xa90u, 0xab8u, 0xab4u}) at<uint32_t>(d, off) = 0;
    obj->flags &= ~0x30u;
    d->flags |= 0x1a0;
    if (d->side == 1) PlrStat_LogEnemySpawned();

    // Script de entrada (parâmetro 1) e animação inicial.
    d->entry_script = 0;
    const uint32_t script = vars->params[1];
    if (script != 0x6000000) {
        d->entry_script = script;
        if (script == 0x6000892 || script == 0x600089c) {
            d->type = script == 0x6000892 ? 0x1c : 0x1b;
            d->saved_subclass = d->subclass;
            d->subclass = 0xf;
            d->entry_script = 0;
        }
    }
    d->danim = 0xffff;
    bool anim_set = false;
    if (d->klass == 0xb) {
        DroneAnim_SetAnimD(0x1a5, 0, d);
        anim_set = true;
    } else if (d->klass == 0x18) {
        DroneAnim_SetAnimD(0x82, 0, d);
        anim_set = true;
    }
    if (d->type == 2) {
        if (d->subclass == 5) {
            d->subclass = 0x12;
            DroneAnim_SetAnimD(0x7d, 0, d);
            anim_set = true;
        }
    } else if (d->type == 0x19 && d->subclass == 5) {
        d->subclass = 0x12;
        DroneAnim_SetAnimD(0x32, 0, d);
        anim_set = true;
    }
    if (!anim_set) DroneAnim_SetAnimD(0x11a, 0, d);
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim != nullptr) {
        anim->root_motion = DroneAnim_PreTransCallback;
        anim->weapon = static_cast<int8_t>(weapon);
    }
    if (d->mode_old != 0 && vars->obj != nullptr) {
        vars->obj->state = 0;
        NDrone2_DoTypeSettingsOLD(d, d->type);
    }
    if (obj->cel == nullptr) {
        obj->life |= 1;
        return nullptr;
    }
    obj->flags &= ~0x400u;
    if (d->initial_state == 0x56 || d->initial_state == 0x12) {
        DroneFunc_SetAsAttacking(d);
        d->flags |= 0x44000000;
        d->flags2 |= 0x10;
    }

    // Ajustes por classe e tipo.
    if (d->klass == 0x13) {
        if (GameState.level_file == 0x7000014) {
            d->unknown_d0 = 0x3c;
            d->view_angle = 0.7853982f;
        }
        if (GameState.level_file == 0x7000016) d->ignore_impacts = 1;
    } else if (d->klass == 0x10) {
        if (GameState.level_file == 0x7000014) d->health = d->health * 3.0f;
    } else if (d->klass == 0x18) {
        d->type = 0x1d;
        d->initial_state = 0xbd;
        d->base_type = 0x1d;
        d->attack_type = 0;
        d->attack_state = 0;
        d->health = DroneInit_TakeXHits(0x33, GameState.difficulty != 1 ? 2.0f : 1.0f);
        if (d->skin == 0x50000ba) d->health = d->health * 3.0f;
    }
    if (d->type == 0xd) {
        behaviour_util_setProperty(0x3c, d->behaviour, 1);
        behaviour_util_setProperty(0x36, d->behaviour, 1);
        behaviour_util_setProperty(0x35, d->behaviour, 1);
        d->health = 100.0f;
        d->route.creep_max = 8.0f;
        d->unknown_ec = 2.0f;
        d->stats[0] = 0;
    } else if (d->type == 0x1b || d->type == 0x1c) {
        d->zero_g = 1;
        d->initial_state = 0x8e;
    }
    d->float_to_target = 1;
    if (d->unknown_50 == 0.0f) d->unknown_50 = 0.075f;
    DroneInit_Collision(d);
    d->max_health = d->health;
    if (!bots_on()) {
        behaviour_util_setProperty(0x17, d->behaviour, 0);
        d->aim_from_events = 1;
        d->accuracy = 1.0f;
        switch (d->type) {
            case 0xc:
                behaviour_util_setProperty(0x34, d->behaviour, 0);
                behaviour_util_setProperty(0x36, d->behaviour, 0);
                behaviour_util_setProperty(0x3b, d->behaviour, 0);
                behaviour_util_setProperty(0x3a, d->behaviour, 0);
                break;
            case 0xd: case 0x1b: case 0x1c: case 0x1d:
                d->aim_from_events = 0;
                break;
            case 0x11:
                behaviour_util_setProperty(0x3b, d->behaviour, 0);
                behaviour_util_setProperty(0x3a, d->behaviour, 0);
                break;
            default:
                break;
        }
        // Port: o original lê a arma sem conferir o objeto de animação.
        const int8_t w = anim != nullptr ? anim->weapon : 0;
        d->anim_fire = (0x1c <= w && w < 0x1e) ? 1 : 0;
        if (GameState.level_file - 0x7000005u < 2 && vars->params[9] == 0x16) {
            behaviour_util_setProperty(0, d->behaviour, 1);
            behaviour_util_setProperty(1, d->behaviour, 1);
            behaviour_util_setProperty(6, d->behaviour, 1);
            behaviour_util_setProperty(0x13, d->behaviour, 0);
            behaviour_util_setProperty(0x41, d->behaviour, 1);
            behaviour_util_setProperty(0x42, d->behaviour, 1);
            d->range_f0 = 10.0f;
        }
    } else {
        BOT_postLoadInit(d);
    }
    d->unknown_ba0 = 0x80;
    d->bounds_skip = 0x1c00;
    if (behaviour_util_getProperty(0x34, d->behaviour) != 0 && behaviour_util_getProperty(0x36, d->behaviour) == 0) {
        d->unknown_ba0 |= 0x200;
    }
    if (behaviour_util_getProperty(0x3b, d->behaviour) != 0 || behaviour_util_getProperty(0x3a, d->behaviour) != 0) {
        d->unknown_ba0 |= 0x100;
    }
    d->body_glow = static_cast<obj_tag*>(BodyGlow_Create(vars->obj));
    return vars->obj;
}

// ---- controle por tick ----

// [decomp] 0x0013a0a0 Drone_DCVfromOBJ
uint32_t Drone_DCVfromOBJ(obj_tag* obj, DCVars_tag* dcv) {
    if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) {
        Drone_tag* d = drone_of(obj);
        dcv->obj = obj;
        dcv->drone = d;
        dcv->sm = &d->sm;
        dcv->cel = obj->cel;
        return 1;
    }
    return 0;
}

// [decomp] 0x00138f68 Drone_GetOpponentInfo
// Copia a posição e a rotação do oponente e calcula a distância, a direção até ele e os ângulos
// entre as duas frentes. Com um jogador como oponente, guarda onde ele foi visto por último.
void Drone_GetOpponentInfo(Drone_tag* drone, obj_tag* obj) {
    if ((drone->flags & 0x100) == 0) return;
    obj_tag* opp = drone->opponent;
    if (opp != nullptr) {
        const uint8_t player = drone->opponent_player;
        drone->opp_pos = opp->pos;
        drone->opp_rot = opp->rot;
        if (player != 0xff) {
            NPCOpponent_tag& info = NPCGlobals.opponents[player];
            if ((info.flags & 1) != 0 && info.frame < drone->seen_time) {
                info.frame = drone->seen_time;
                info.pos = drone->seen_opp_pos;
                drone->seen_pos = obj->pos;
            }
        }
    }
    drone->opp_delta = {drone->opp_pos.x - obj->pos.x, drone->opp_pos.y - obj->pos.y, drone->opp_pos.z - obj->pos.z,
                        drone->opp_pos.w - obj->pos.w};
    drone->opp_dist = Vec_Magnitude(&drone->opp_delta);
    drone->unknown_1c0 = 0.0f;
    const float yaw = ATAN2_APPROX(drone->opp_delta.x, drone->opp_delta.z);
    drone->unknown_1c8 = 0.0f;
    drone->opp_yaw = yaw;
    drone->opp_facing = Vec_AngleDifference(drone->opp_rot.y, yaw + 3.1415927f);
    const uint32_t flags = drone->anim_flags;
    float facing = obj->rot.y;
    if ((flags & 8) != 0) facing = facing + 1.5707964f;
    if ((flags & 0x10) != 0) facing = facing + 3.1415927f;
    if ((flags & 0x20) != 0) facing = facing + -1.5707964f;
    drone->opp_bearing = Vec_AngleDifference(facing, drone->opp_yaw);
}

// [decomp] 0x00139268 Drone_Control
// Tick de um personagem: antes (distâncias, canais, temporizadores), o controle do tipo (ou o padrão),
// depois (morte), a animação (ou o avanço sem animar de Drone_AnimInfo +4) e as chamadas de animação.
void Drone_Control(obj_tag* obj) {
    if ((&NPCGlobals)->enabled == 0 || Drone_bDisableSystem != 0) return;
    if (bots_on() && (obj->type == 0x11 || Control_Plr2Ind(obj) < 0)) return;
    Drone_tag* d = drone_of(obj);
    DCVars_tag dcv{obj, d, obj->cel, &d->sm};
    if (NDrone2_PreDroneControl(&dcv) != 0) {
        if (bots_on()) {
            if (obj->type == 0x11) return;
            if (Control_Plr2Ind(obj) < 0) return;
        }
        uint32_t (*control)(DCVars_tag*) = DroneTypeSettings[dcv.drone->type].control;
        if (control == nullptr) {
            NDrone2_ControlSTANDARD(&dcv);
        } else {
            control(&dcv);
        }
    }
    NDrone2_PostDroneControl(&dcv);
    DroneAnim_SnapRotate(d);
    if ((d->flags & 0x80000) != 0) {
        if (NDrone2_DoAnimation(d) == 0 && d->anim_start != GameState.frame) {
            float step = d->move_speed;
            const uint32_t flags = d->flags;
            d->flags = flags & 0xffefffff;
            if (step != 0.0f) {
                if ((flags & 1) != 0 && d->door == nullptr) {
                    if ((d->boundary_flags & 2) != 0 && 0.0f < step) step = 0.0f;
                    if ((d->boundary_flags & 4) != 0 && step < 0.0f) step = 0.0f;
                }
                dcv.obj->pos.x = dcv.obj->pos.x + PS2Sinf(dcv.obj->rot.y) * step;
                dcv.obj->pos.z = dcv.obj->pos.z + PS2Sinf(dcv.obj->rot.y + 1.5707964f) * step;
            }
        } else {
            AnimObjectUpdate(obj);
            DroneAnim_SnapRotate2(d);
            d->flags |= 0x100000;
        }
        if (d->anim_func != nullptr) d->anim_func(d, 1);
        if (d->state_func != nullptr) d->state_func(d, 1);
    }
    DroneAnim_CallHandler(&dcv);
    const float dx = obj->pos.x - obj->old_pos.x;
    const float dz = obj->pos.z - obj->old_pos.z;
    d->moved = std::sqrt(dx * dx + dz * dz);
    d->speed = Vec_Magnitude(&d->vel);
}

// [decomp] 0x00148db0 NDrone2_PreDroneControl
// Zera o estado do tick, mede a distância a cada jogador, trata o canal de ataque (parâmetro 6) e
// entrega as mensagens com hora marcada (msg_time e os dois temporizadores).
uint32_t NDrone2_PreDroneControl(DCVars_tag* dcv) {
    obj_tag* obj = dcv->obj;
    Drone_tag* d = dcv->drone;
    obj->mode &= 0xffdf;
    d->last_hit = nullptr;
    d->no_move = 0;
    d->prev_alert = d->alert;
    d->yaw_offset = 0.0f;
    if (float(d->unknown_2c4) + FRAME_RATE_DIV * 30.0f < float(GameState.frame)) d->in_sights = 0;
    if (d->in_sights == 0) {
        d->unknown_2c8 = 0;
    } else {
        ++d->unknown_2c8;
    }
    for (int i = 0; i < 4; ++i) {
        d->player_dist[i] = glb_players[i] == nullptr ? 100000.0f : Vec_Dist3D(&obj->pos, &glb_players[i]->pos);
    }
    const uint32_t now = GameState.frame;
    if (d->channel_b != 0 && switch_channels[d->channel_b] != 0) {
        if (d->channel_state == 0xc) {
            dcv->obj->life |= 1;
            d->channel_b = 0;
            d->channel_state = 0;
            return 1;
        }
        if (d->channel_state != 0) {
            int32_t state = d->attack_state;
            d->channel_state = 0;
            d->channel_b = 0;
            if (state != 0) {
                if (d->attack_type != 0) d->type = d->attack_type;
                if (d->type == 2) {
                    d->type = 0x19;
                    if (DroneTypeSettings[0x19].init != nullptr) DroneTypeSettings[0x19].init(d);
                    state = 0x56;
                }
                d->attack_type = 0;
                d->attack_state = 0;
                if (d->divars.params[5] == 100) {
                    d->behaviour = d->behaviour_b;
                    d->unknown_29 = 1;
                }
                d->unknown_28 = 1;
            }
            return Drone_SM_SetState(dcv->sm, static_cast<uint32_t>(state), 0);
        }
    }
    if (d->msg_time != 0 && d->msg_time <= GameState.frame) {
        d->msg_time = 0;
        Drone_SM_SendMsgSelf(4, 0, 0, d->sm.state, dcv);
    }
    if (d->timer1_on != 0 && d->timer1_time <= now) {
        const uint32_t data = d->timer1_data;
        d->timer1_on = 0;
        d->timer1_data = 0;
        Drone_SM_SendMsgSelf(0xc, data, 0, 0, dcv);
    }
    if (d->timer2_on != 0 && d->timer2_time <= now) {
        const uint32_t data = d->timer2_data;
        d->timer2_on = 0;
        d->timer2_data = 0;
        Drone_SM_SendMsgSelf(0xd, data, 0, 0, dcv);
    }
    if (d->ninja_eyes != 0) NDrone2_CreateNinjaEyes(dcv->obj);
    return 1;
}

// [decomp] 0x001490f0 NDrone2_ControlSTANDARD
// O controle padrão: alerta, tick da máquina de estados (mensagem 3), fala e rosto, oponente, olhar,
// bafo no frio, movimento, colisão, tiro, portas e explosivos.
uint32_t NDrone2_ControlSTANDARD(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (1.0f < d->alert) d->alert = 1.0f;
    if (d->alert_base < d->alert && d->alert < 1.0f) d->alert = d->alert - 1.0f / (FRAME_RATE * 50.0f);
    if (d->alert < d->alert_base) d->alert = d->alert_base;
    Drone_SM_SendMsgSelf(3, 0, 0, 0, dcv);
    if (dcv->drone->sound != 0 && Sound_IsFinnished(dcv->drone->sound) != 0) dcv->drone->sound = 0;
    Drone_tag* dd = dcv->drone;
    if (dd->face_playing != 0) {
        if (dd->face_done != 0 || dd->face_script == nullptr || (dd->face_script->flags & 0x10000000) != 0) {
            dd->face_playing = 0;
        }
    }
    if (d->talk_script == 0) d->talk_pending = 0;
    if (d->talk_pending != 0 && d->talk_time < GameState.frame) {
        Drone_tag* t = dcv->drone;
        ANIMSCRIPT_tag* face = AnimFacialAppend(dcv->obj, d->talk_script);
        t->face_script = face;
        if (face != nullptr) {
            t->face_done = 0;
            t->face_playing = 1;
            face->done_flag = &t->face_done;
            t->face_script->owner = &t->face_script;
            t->face_script->flags &= 0x7fffffff;
            t->face_script->end_mode = 3;
        }
        t->talk_pending = 0;
    }
    if ((dcv->drone->flags & 0x100) == 0) {
        NDrone2_DoTracking(0, dcv);
        return 1;
    }
    NPCGLOBALS_tag* n = (&NPCGlobals);
    switch (d->side) {
        case 1: ++n->side_counts[0]; break;
        case 2: ++n->side_counts[1]; break;
        case 3: ++n->side_counts[2]; break;
        default: break;
    }
    if (dcv->drone->type == 0x1e) BOT_setOtherPlayerInfo(dcv);
    NDrone2_FindOpponent(dcv->drone);
    if (obj->unknown_fc == 0 || d->health <= 0.0f) {
        NDrone2_DoTracking(0, dcv);
    } else {
        // Bafo: 0,12 m à frente da cabeça (osso 5), na direção do eixo z do osso.
        _VECTOR head;
        MATRIX m;
        AnimGetBoneWorldTrans(obj, 0x80000005, 0, &head, &m);
        _VECTOR dir{m.m[2][0], m.m[2][1], m.m[2][2], 0.0f};
        _VECTOR pos{head.x + dir.x * 0.12f, head.y + dir.y * 0.12f, head.z + dir.z * 0.12f, head.w};
        Env_Breath(&pos, &dir, obj->cel,
                   obj->created + (nf::p32_u(obj) & 0xfff) * 0x80);
        NDrone2_DoTracking(1, dcv);
    }
    NDrone2_HandleTalking(dcv);
    if (d->zero_g == 0) {
        d->bunch_flags = 0;
        d->nearest = nullptr;
        d->nearest_dist = 0.0f;
        DroneMove_SetBoundryFlags(dcv);
        d->move_rot = obj->rot;
        d->move_rot.y = d->move_rot.y + d->yaw_offset;
        NDrone2_Move(dcv);
        d->move_rot.y = d->move_rot.y - d->yaw_offset;
        obj->rot = d->move_rot;
        d->no_bunching = DroneMove_NoBunching(dcv);
    } else {
        NDrone2_Move(dcv);
    }
    NDrone2_Collision(dcv->obj);
    DroneWeap_HandleFiring(dcv);
    Drone_tag* e = dcv->drone;
    if (e->door != nullptr && e->door->type == 0x1c &&
        float(e->door_time) + FRAME_RATE_DIV * 60.0f < float(GameState.frame)) {
        if (2.0f < Vec_Dist3D(&dcv->obj->pos, &e->door->pos)) {
            e->door = nullptr;
        } else if (NDrone2_DoorIsOpen(dcv) == 0) {
            NDrone2_OpenDoor(dcv);
        }
    }
    DroneFunc_HandleExplosives(dcv);
    return 1;
}

// [decomp] 0x0014aff8 NDrone2_PostDroneControl
// Personagem ativo sem vida e ainda não morto: marca "morreu agora" (0x400).
uint32_t NDrone2_PostDroneControl(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if ((d->flags & 0x100) != 0) {
        dcv->obj->mode |= 0x20;
        if (d->health <= 0.0f && (d->flags & 0x200) == 0) dcv->drone->flags |= 0x400;
    }
    return 1;
}

// [decomp] 0x00149f88 NDrone2_DoAnimation
// Se a animação deve tocar neste tick: sem a flag 0x80000 não; quem tem altura de raiz e está no
// chão (obj +0xfc zero) só anima com um script saindo ou fora do modo "anda sem animar" (4).
uint32_t NDrone2_DoAnimation(Drone_tag* drone) {
    if ((drone->flags & 0x80000) == 0) return 0;
    if (drone->klass != 0xc && anim_of(drone->obj)->root_height != 0.0f && drone->obj->unknown_fc == 0) {
        if (drone->blend_script != nullptr) return 1;
        return (drone->anim_flags & 4) == 0 ? 1 : 0;
    }
    return 1;
}

// ---- remoção ----

// [decomp] 0x00136ce8 Drone_Delete
// Solta o personagem de tudo que o referencia: pontos de cobertura, outros personagens (oponente,
// alvos, cabeça), objetos 0xd/5 que o seguem, o gerador do grupo e explosivos presos a ele.
void Drone_Delete(obj_tag* obj) {
    Drone_tag* d = drone_of(obj);
    if (d->body_glow != nullptr) {
        BodyGlow_Delete(d->body_glow);
        d->body_glow = nullptr;
    }
    NPCGLOBALS_tag* n = (&NPCGlobals);
    for (uint32_t i = 0; i < n->cover_count; ++i) {
        CoverNode_tag& node = n->cover_nodes[i];
        if (node.user == obj) {
            node.user = nullptr;
            node.flags &= ~0x10u;
        }
    }
    if (d->route.list != nullptr) Mem_Free(&d->route.list);
    if (d->route2.list != nullptr) Mem_Free(&d->route2.list);
    for (obj_tag* o = control_first_object(); o != nullptr;) {
        obj_tag* next = o->next;
        switch (o->type) {
            case 0xd:
                if (at<P32<obj_tag>>(o->data, 0x28) == obj) at<P32<obj_tag>>(o->data, 0x28) = nullptr;
                break;
            case 2: {
                Drone_tag* other = drone_of(o);
                if (other->opponent == obj) other->opponent = nullptr;
                if (other->unknown_2c0 == d) other->unknown_2c0 = nullptr;
                if (other->nearest == d) other->nearest = nullptr;
                if (other->unknown_318 == d) other->unknown_318 = nullptr;
                if (other->unknown_2b0 == obj) other->unknown_2b0 = nullptr;
                if (other->attacker == obj) other->attacker = nullptr;
                if (other->unknown_2bc == obj) other->unknown_2bc = nullptr;
                if (other->head_target == obj) {
                    AnimObjectSetHeadTracking(o, nullptr, 3.0f, 0.14f, 1, 1, 5);
                    other->head_tracking = 0;
                    other->head_target = nullptr;
                }
                break;
            }
            case 5:
                if (at<P32<obj_tag>>(o->data, 0x30) == obj) at<P32<obj_tag>>(o->data, 0x30) = nullptr;
                break;
            case 0x30:
                if (d->group_id == static_cast<DroneSpawner_tag*>(o->data)->group) DroneSpawner_DroneDelete(o, obj);
                break;
            case 0x36:
                if (o->data != nullptr && obj == at<P32<obj_tag>>(o->data, 0xb8)) {
                    o->life |= 1;
                    static const _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
                    Explode_Create(o, &o->pos, &up, 5.0f, 5.0f, 0x6000052, 50.0f, 0xff, 0xff, 100, nullptr, 0);
                }
                break;
            default:
                break;
        }
        o = next;
    }
    AnimObjectSetHeadTracking(obj, nullptr, 3.0f, 0.14f, 1, 1, 5);
    d->head_target = nullptr;
    d->head_tracking = 0;
    LList_Remove(Drone_List(), &d->node);
}

// ---- processamento global ----

// [decomp] 0x0013a710 Drone_PauseNoDraw
// Liga (1) ou desliga a pausa da IA: os personagens param de pensar e de ser desenhados (flag 0x10).
void Drone_PauseNoDraw(int8_t on) {
    Drone_bDisableSystem = static_cast<uint8_t>(on);
    Drone_bDontDraw = static_cast<uint8_t>(on);
    for (LLNODE_tag* n = Drone_List()->first; n != nullptr; n = n->next) {
        obj_tag* obj = reinterpret_cast<Drone_tag*>(n)->obj;
        if (on == 0) {
            obj->flags &= ~0x10u;
        } else {
            obj->flags |= 0x10;
        }
    }
}

// [decomp] 0x00139520 Drone_InitComms
// Uma vez por tick, antes dos objetos: troca do "jogador falso", visão e oponentes de todos, a música
// de combate (ninguém alerta e perto) e os canais 0x97/0x98 de alarme, a contagem de caídos, o aviso
// do alarme, as mensagens atrasadas e as contagens por lado do tick anterior.
void Drone_InitComms() {
    if (switch_NO_DRONES != 0) {
        if (Drone_bDontDraw == 0 || Drone_bDisableSystem == 0) Drone_PauseNoDraw(1);
        return;
    }
    if (Drone_bDontDraw != 0 || Drone_bDisableSystem != 0) Drone_PauseNoDraw(0);
    NPCGLOBALS_tag* n = (&NPCGlobals);
    const uint8_t dummy = Drone_DummyPlayer;
    obj_tag* player = glb_players[0];
    obj_tag* fake = n->dummy_player;
    if (dummy != n->dummy_player_unknown && fake != nullptr) {
        if (dummy == 0) {
            for (LLNODE_tag* l = n->drones.first; l != nullptr; l = l->next) {
                Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
                if (d->opponent == fake) NDrone2_SetOpponent(d, player);
            }
        } else {
            Vec_Copy2(&player->pos, &fake->pos, &fake->old_pos);
            Vec_Copy2(&player->rot, &fake->rot, &fake->old_rot);
            fake->cel = player->cel;
            for (LLNODE_tag* l = n->drones.first; l != nullptr; l = l->next) {
                Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
                if (d->opponent == player) NDrone2_SetOpponent(d, n->dummy_player);
            }
        }
        n->dummy_player_unknown = dummy;
    }
    if (Drone_bDisableSystem != 0 || n->enabled == 0 || n->drones.first == nullptr) return;
    AINetwork_ClearLinkFlags(0x310);
    Drone_BuildDynamicAwarePoints();
    for (NPCOpponent_tag& info : n->opponents) {
        if ((info.flags & 1) == 0) continue;
        if (info.obj == nullptr) {
            info.flags = 0;
        } else {
            info.pos = info.obj->pos;
            info.frame = GameState.frame;
        }
    }
    Drone_ProcessCoverNodes();
    Drone_ProcessOpponents();
    for (NPCOpponent_tag& info : n->opponents) {
        info.flags = 0;
        info.obj = nullptr;
        info.frame = 0;
    }
    DroneVision_ProcessDroneSight();
    // Música calma se nenhum inimigo ativo; alarme se alguém não aliado está em alerta a menos de 30 m.
    bool calm = true, close_alert = false;
    for (LLNODE_tag* l = n->drones.first; l != nullptr; l = l->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
        if ((d->flags & 0x600) != 0) continue;
        if ((d->flags & 0x100) == 0) {
            if (d->side == 1) calm = false;
        } else if (1.0f <= d->alert && d->opp_dist < 30.0f && d->side != 2) {
            close_alert = true;
            calm = false;
            break;
        }
    }
    if (calm) Music_Event(0xe, 1);
    if (close_alert) switch_channels[0x97] = 1;
    switch_channels[0x98] = close_alert ? 1 : 0;
    NDrone2_NavNodeCache();
    // Caídos (estados 0xf, 0x47, 0x48, 0xbf) e o mais antigo deles.
    n->down_count = 0;
    n->oldest_down_time = GameState.frame;
    n->down_or_49_count = 0;
    n->oldest_down_id = 0;
    uint32_t since = 0;
    DCVars_tag dcv{};
    for (LLNODE_tag* l = n->drones.first; l != nullptr; l = l->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(l);
        obj_tag* obj = d->obj;
        // Como o original, um objeto que não é personagem reaproveita o DCVars anterior.
        if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) {
            dcv = {obj, static_cast<Drone_tag*>(obj->data), obj->cel, &static_cast<Drone_tag*>(obj->data)->sm};
        }
        DroneFunc_HandleSoundAlerts(&dcv);
        const uint32_t st = d->sm.state;
        bool down = false;
        if (st == 0x48) {
            down = true;
            since = d->sm.entered + n->corpse_time;
        } else if (st == 0xf || st == 0x47 || st == 0xbf) {
            down = true;
            since = d->sm.entered;
        } else if (st == 0x49) {
            ++n->down_or_49_count;
        }
        if (down) {
            ++n->down_count;
            if (since < n->oldest_down_time) {
                n->oldest_down_id = d->sm.id;
                n->oldest_down_time = since;
            }
        }
    }
    n->down_or_49_count += n->down_count;
    Sound_ZeroAlertness();
    if (switch_channels[0x96] != 0 && n->alarm_time + 0x1eu < GameState.frame &&
        (GameState.level_file != 0x7000014 || n->alarm_said == 0)) {
        n->alarm_said = 1;
        Drone_Message(nullptr, 0x1e, 0, 0);
        n->alarm_time = GameState.frame;
    }
    Drone_SM_SendDelayedMsgs();
    DroneFunc_CheckAlarmRaised();
    for (int i = 0; i < 4; ++i) n->prev_counts[3 + i] = n->unknown_188[i];
    n->prev_counts[0] = n->side_counts[0];
    n->prev_counts[1] = n->side_counts[2];
    n->prev_counts[2] = n->side_counts[1];
    n->side_counts[1] = 0;
    for (uint32_t& c : n->unknown_188) c = 0;
    n->side_counts[0] = 0;
    n->side_counts[2] = 0;
    uint16_t music_2, music_e;
    std::memcpy(&music_2, MusicVars + 2, 2);
    std::memcpy(&music_e, MusicVars + 0xe, 2);
    if (music_e <= music_2) std::memset(MusicVars + 0xc, 0, 2);
    std::memset(MusicVars + 0x20, 0, 4);
    const float thousand = 1000.0f;
    std::memcpy(MusicVars + 0x18, &thousand, 4);
    std::memset(MusicVars + 0xe, 0, 2);
    std::memcpy(MusicVars + 0x14, &thousand, 4);
}
