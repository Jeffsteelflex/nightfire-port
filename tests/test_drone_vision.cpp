// Testes da visão e percepção da IA (engine/drone_vision.cpp) sem dados do jogo: personagens e salas
// sintéticos, propriedades de comportamento montadas nas tabelas bitDescs/bitMasks.

#include <cmath>
#include <cstring>

#include "engine/ainetwork.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "platform/psi.h"
#include "test_framework.h"
#include "platform/fmath.h"

namespace {

alignas(16) Drone_tag g_d[3];
alignas(16) obj_tag g_o[6];
alignas(16) BLData g_bloke;

// Piso de 20 x 20 em y = 0 com normal +y (como em test_collide.cpp): bloqueia raios de cima para baixo.
struct FloorRoom {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2]{3, 3};
    colldata_tag coll{};
    celglist_tag glist{};
    cel_tag* cel = nullptr;

    FloorRoom() {
        const int16_t v[4][3] = {{-1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}, {1000, 0, -1000}};
        std::memcpy(head, v, sizeof(v));
        const int16_t n[3] = {0, 16383, 0};
        std::memcpy(head + 24, n, sizeof(n));
        const float d = 0.0f;
        std::memcpy(head + 30, &d, 4);
        const uint16_t t[2][4] = {{0, 3, 6, 12}, {0, 6, 9, 12}};
        std::memcpy(tris, t, sizeof(t));
        box = {{-10, -1, -10}, int16_t(-0x7fff), 0, {10, 1, 10}, 2, 0, {0, 0, 0}, 0.01f};
        coll.boxes = reinterpret_cast<uint8_t*>(&box);
        coll.list = reinterpret_cast<uint8_t*>(tris);
        coll.rest = surface;
        coll.head = head;
        glist.coll = &coll;
        glist.sphere = {0, 0, 0, 15};
        cel = build_alloc_cel(glb_world->first);
        glb_world->first = cel;
        cel->glist = &glist;
        cel->type = 0xc023 | 0x8000000;
        cel->pos = {0, 0, 0, 0};
        cel->rot = {0, 0, 0, 0};
        cel->glist_flags = 0;
    }
};

uint8_t g_saved_descs[sizeof(bitDescs)];
uint32_t g_saved_masks[35];
bool g_saved = false;

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    glb_world = build_alloc_world();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    Mission_SetStatus(kMissionRunning);  // missão em andamento: PlrStat_OkToUpdate deixa ver o jogador
    GS_SetRefreshRate(60, 60);
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    LList_Init(Drone_List(), 0xd20, 0);
    std::memset(&AIBoxList, 0, sizeof(AIBoxList));
    AINetwork.path_count = 0;
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    switch_BLIND_DRONES = 0;
    glb_players[0] = nullptr;
    if (!g_saved) {
        std::memcpy(g_saved_descs, bitDescs, sizeof(bitDescs));
        std::memcpy(g_saved_masks, bitMasks, sizeof(bitMasks));
        g_saved = true;
    }
    std::memset(bitDescs, 0, sizeof(bitDescs));
    std::memset(bitMasks, 0, sizeof(bitMasks));
    // Propriedades de 1 bit: a propriedade p é o bit (p & 31) da palavra p / 32 do comportamento.
    for (int p = 0; p < 91; ++p) {
        bitDescs[p * 2] = uint8_t((p / 32) | ((p & 31) << 3));
        bitDescs[p * 2 + 1] = uint8_t(p % 35);
    }
}

void restore_tables() {
    std::memcpy(bitDescs, g_saved_descs, sizeof(bitDescs));
    std::memcpy(bitMasks, g_saved_masks, sizeof(bitMasks));
    g_saved = false;
}

// Liga a propriedade p (1 bit) no comportamento do personagem.
void set_prop(Drone_tag* d, int p) {
    bitMasks[p % 35] = 1u << (p & 31);
    d->behaviour[p / 32] |= 1u << (p & 31);
}

Drone_tag* make_drone(int i, _VECTOR pos, cel_tag* cel, uint8_t side) {
    std::memset(&g_d[i], 0, sizeof(Drone_tag));
    std::memset(&g_o[i], 0, sizeof(obj_tag));
    obj_tag* o = &g_o[i];
    Drone_tag* d = &g_d[i];
    o->type = 2;
    o->data = d;
    o->pos = pos;
    o->cel = cel;
    d->obj = o;
    d->side = side;
    d->flags = 0x100;
    d->health = 1.0f;
    d->behaviour = d->behaviour_a;
    d->head_pos = {pos.x, pos.y + 0.7f, pos.z, 0.0f};
    d->view_angle = 1.5707964f;
    d->range_e8 = 20.0f;
    d->opponent_player = 0xff;
    LList_Add(Drone_List(), &d->node);
    Drone_SM_InitObject(o);
    return d;
}

// Objeto qualquer (não personagem) na sala.
obj_tag* make_obj(int i, uint8_t type, _VECTOR pos, cel_tag* cel) {
    obj_tag* o = &g_o[i];
    std::memset(o, 0, sizeof(obj_tag));
    o->type = type;
    o->pos = pos;
    o->cel = cel;
    return o;
}

}  // namespace

TEST(drone_vision_clamp_to_collision_cylinder) {
    obj_tag o{};
    o.pos = {10, 5, -3, 0};
    // A 3 m no plano (e 2 m acima): fica a 0,4 m na mesma direção, sem mudar a altura.
    _VECTOR p{13, 7, -3, 0};
    DroneFunc_ClampPosToCollisionCylinder(&o, &p);
    CHECK_NEAR(p.x, 10.4, 1e-5);
    CHECK_NEAR(p.z, -3.0, 1e-5);
    CHECK(p.y == 7.0f);
    _VECTOR q{10 + 3, 0, -3 + 4, 0};  // 5 m na diagonal 3-4-5
    DroneFunc_ClampPosToCollisionCylinder(&o, &q);
    CHECK_NEAR(std::hypot(q.x - 10.0, q.z + 3.0), 0.4, 1e-5);
    CHECK_NEAR((q.x - 10.0) / (q.z + 3.0), 0.75, 1e-4);
    // Dentro do cilindro (mesmo bem acima): não muda.
    _VECTOR r{10.2f, 50, -3.1f, 0};
    DroneFunc_ClampPosToCollisionCylinder(&o, &r);
    CHECK(r.x == 10.2f && r.y == 50.0f && r.z == -3.1f);
}

TEST(drone_vision_opponent_visibility_rules) {
    fresh();
    Drone_tag* d = make_drone(0, {0, 1, 0, 0}, nullptr, 1);
    obj_tag* opp = make_obj(3, 0x1c, {0, 1, 10, 0}, nullptr);
    d->opponent = opp;
    d->view_angle = 0.0f;  // vê para todo lado
    d->range_e8 = 10.0f;
    d->visibility_mul = 1.0f;
    // Sem alerta: 0,7 exatamente no alcance, mais perto mais, e nada a partir de 1,7 alcance.
    d->opp_dist = 10.0f;
    CHECK_NEAR(DroneVision_OpponentVisibility(d), 0.7, 1e-6);
    CHECK(d->visibility == DroneVision_OpponentVisibility(d));
    d->opp_dist = 5.0f;
    CHECK(DroneVision_OpponentVisibility(d) > 0.7f);
    d->opp_dist = 17.5f;
    CHECK(DroneVision_OpponentVisibility(d) == 0.0f);
    // Alerta 0,5 dobra o alcance.
    d->alert = 0.5f;
    d->opp_dist = 20.0f;
    CHECK_NEAR(DroneVision_OpponentVisibility(d), 0.7, 1e-6);
    // Os volumes de IA do oponente multiplicam (exceto com algo sabido e alerta total).
    d->alert = 0.0f;
    d->opp_dist = 10.0f;
    d->visibility_mul = 0.5f;
    CHECK_NEAR(DroneVision_OpponentVisibility(d), 0.35, 1e-6);
    d->flags2 = 2;
    d->alert = 1.0f;
    d->opp_dist = 30.0f;  // alcance 30 com alerta total
    CHECK_NEAR(DroneVision_OpponentVisibility(d), 0.7, 1e-6);
    d->flags2 = 0;
    d->alert = 0.0f;
    d->visibility_mul = 1.0f;
    // Campo de visão de 90 graus (meio ângulo): de costas não vê; de frente, a borda reduz.
    d->view_angle = 1.5707964f;
    d->head_yaw = 0.0f;
    d->opp_yaw = 3.1415927f;
    d->opp_dist = 5.0f;
    CHECK(DroneVision_OpponentVisibility(d) == 0.0f);
    d->opp_yaw = 0.0f;
    const float ahead = DroneVision_OpponentVisibility(d);
    d->opp_yaw = 1.6f;  // além do meio ângulo, mas dentro da folga de 22,5 graus
    const float edge = DroneVision_OpponentVisibility(d);
    CHECK(ahead > edge && edge > 0.0f);
    // Sem oponente: 0.
    d->opponent = nullptr;
    CHECK(DroneVision_OpponentVisibility(d) == 0.0f && d->visibility == 0.0f);
    restore_tables();
}

TEST(drone_vision_object_cone) {
    fresh();
    Drone_tag* d = make_drone(0, {0, 0, 0, 0}, nullptr, 1);
    d->head_yaw = 0.0f;  // olha para +z
    d->view_angle = 0.5f;
    d->range_e8 = 20.0f;
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, 10, 0}, nullptr)) == 1.0f);   // à frente
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, -10, 0}, nullptr)) == 0.0f);  // atrás
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, -1, 0}, nullptr)) == 1.0f);   // < 2 m
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, 25, 0}, nullptr)) == 0.0f);   // além do alcance
    d->range_e8 = 100.0f;
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, 60, 0}, nullptr)) == 0.0f);   // além de 50 m
    d->view_angle = 0.0f;  // todo lado
    CHECK(DroneVision_ObjectVisibility(d, make_obj(3, 0x1c, {0, 0, -10, 0}, nullptr)) == 1.0f);
    CHECK(DroneVision_ObjectVisibility(nullptr, &g_o[3]) == 0.0f);
    restore_tables();
}

TEST(drone_vision_target_ids_and_set_opponent) {
    fresh();
    Drone_tag* e1 = make_drone(0, {0, 0, 0, 0}, nullptr, 1);
    Drone_tag* e2 = make_drone(1, {1, 0, 0, 0}, nullptr, 1);
    Drone_tag* ally = make_drone(2, {2, 0, 0, 0}, nullptr, 2);
    obj_tag* a = make_obj(3, 0x1c, {5, 1, 5, 0}, nullptr);
    obj_tag* b = make_obj(4, 0x1c, {-5, 1, 5, 0}, nullptr);
    // O mesmo alvo tem o mesmo lugar; outro alvo, outro lugar; aliados não usam a tabela.
    const uint8_t ia = DroneFunc_AllocateTargetID(e1, a);
    CHECK(ia == 0 && DroneFunc_AllocateTargetID(e2, a) == ia);
    const uint8_t ib = DroneFunc_AllocateTargetID(e2, b);
    CHECK(ib != ia && ib < 8);
    CHECK(DroneFunc_AllocateTargetID(ally, a) == 0xff);
    // Cheia: 0xff.
    for (int i = 0; i < 8; ++i) NPCGlobals.opponents[i].flags |= 1;
    obj_tag* c = make_obj(5, 0x1c, {0, 0, 0, 0}, nullptr);
    CHECK(DroneFunc_AllocateTargetID(e1, c) == 0xff);

    fresh();
    e1 = make_drone(0, {0, 0, 0, 0}, nullptr, 1);
    a = make_obj(3, 0x1c, {5, 1, 5, 0}, nullptr);
    GameState.frame = 1234;
    NDrone2_SetOpponent(e1, a);
    CHECK(e1->opponent == a && e1->opponent_player != 0xff);
    // Um objeto comum: sem mira, o alvo é a posição dele; a tabela anota o quadro e a posição.
    CHECK(e1->aim_offset.x == 0.0f && e1->aim_offset.y == 0.0f && e1->aim_offset.z == 0.0f);
    CHECK(e1->target_pos.x == 5.0f && e1->target_pos.y == 1.0f && e1->target_pos.z == 5.0f);
    const NPCOpponent_tag& slot = NPCGlobals.opponents[e1->opponent_player];
    CHECK(slot.obj == a && (slot.flags & 3) == 3 && slot.frame == 1234 && slot.pos.x == 5.0f);
    // Visto em outro lugar: DroneFunc_TargetSeen atualiza o lugar.
    _VECTOR seen{7, 8, 9, 0};
    GameState.frame = 2000;
    DroneFunc_TargetSeen(e1, &seen);
    CHECK(slot.frame == 2000 && slot.pos.x == 7.0f && slot.pos.z == 9.0f);
    // Sem oponente: sem lugar.
    NDrone2_SetOpponent(e1, nullptr);
    CHECK(e1->opponent == nullptr && e1->opponent_player == 0xff);
    restore_tables();
}

TEST(drone_vision_reaction_time) {
    fresh();
    Drone_tag* d = make_drone(0, {0, 0, 0, 0}, nullptr, 1);
    GameState.level_file = 0x7000002;
    d->stats[4] = 50;
    d->alert = 0.0f;
    const uint32_t calm = DroneFunc_ReactionTime(d);
    d->alert = 0.5f;
    const uint32_t half = DroneFunc_ReactionTime(d);
    CHECK(calm > half && half > 0);
    CHECK(calm == 44 || calm == 45);  // 0,3 x (200 - 50), truncado
    d->stats[4] = 150;
    d->alert = 0.0f;
    CHECK(DroneFunc_ReactionTime(d) < calm);  // estatística maior, reação mais rápida
    d->alert = 1.0f;
    CHECK(DroneFunc_ReactionTime(d) == 0);
    d->alert = 0.0f;
    d->sight_flags = 8;  // já viu antes
    CHECK(DroneFunc_ReactionTime(d) == 0);
    // Fases sem a regra: 10 quadros a 60 Hz.
    GameState.level_file = 0x7000004;
    CHECK(DroneFunc_ReactionTime(d) == 10);
    restore_tables();
}

TEST(drone_vision_line_of_sight_through_floor) {
    fresh();
    FloorRoom room;
    Drone_tag* watcher = make_drone(0, {0, -2, 0, 0}, room.cel, 1);  // embaixo do piso
    Drone_tag* other = make_drone(1, {3, 2, 3, 0}, room.cel, 1);      // em cima
    const uint16_t checks = NPCGlobals.vision_checks;
    // O teste vai do alvo ao olho: de cima para baixo o piso bloqueia.
    CHECK(NDrone2_CanSeeObject(watcher, other->obj, 5, 0) == 0);
    CHECK(NPCGlobals.vision_checks == checks + 1);
    // Os dois em cima: vê.
    watcher->obj->pos = {0, 1, 0, 0};
    watcher->head_pos = {0, 1.7f, 0, 0};
    CHECK(NDrone2_CanSeeObject(watcher, other->obj, 5, 0) == 1);
    // Posição: de cima para um ponto embaixo do piso, bloqueado; para um ponto em cima, livre.
    _VECTOR below{2, -3, 2, 0}, above{2, 3, 2, 0};
    CHECK(DroneVision_CanSeePosition(watcher->obj, &below, room.cel, nullptr) == 0);
    CHECK(DroneVision_CanSeePosition(watcher->obj, &above, room.cel, nullptr) == 1);
    restore_tables();
}

TEST(drone_vision_alert_spreads_to_who_sees) {
    fresh();
    FloorRoom room;
    // a sabe que viu o oponente (flags2 2); b pode ser alertado (0x2000) por quem viu (propriedade 0x27).
    Drone_tag* a = make_drone(0, {0, 1, 10, 0}, room.cel, 1);
    Drone_tag* b = make_drone(1, {0, 1, 0, 0}, room.cel, 1);
    a->flags2 = 2;
    b->flags |= 0x2000;
    set_prop(b, 0x27);
    b->head_yaw = 0.0f;  // olha para a (+z)
    GameState.frame = 1000;
    CHECK(DroneVision_DetermineAlertCheck(a, b) == 1);  // no cone de visão
    CHECK((b->flags & 0x20000) != 0 && b->alerted_by == a && (b->flags2 & 0x20000) != 0);
    // Já alertado: não de novo; os dois sabendo: nada.
    CHECK(DroneVision_DetermineAlertCheck(b, a) == 0);

    // De costas não vê: não é alertado.
    fresh();
    FloorRoom room2;
    a = make_drone(0, {0, 1, 10, 0}, room2.cel, 1);
    b = make_drone(1, {0, 1, 0, 0}, room2.cel, 1);
    a->flags2 = 2;
    b->flags |= 0x2000;
    set_prop(b, 0x27);
    b->head_yaw = 3.1415927f;
    GameState.frame = 1000;
    CHECK(DroneVision_DetermineAlertCheck(b, a) == 0 && (b->flags & 0x20000) == 0);
    // Sem a propriedade que aceita esse tipo de alerta: nada, mesmo olhando.
    b->head_yaw = 0.0f;
    b->behaviour_a[0x27 / 32] = 0;
    CHECK(DroneVision_DetermineAlertCheck(a, b) == 0 && (b->flags & 0x20000) == 0);
    // Com a propriedade, mas o outro atacou há menos de um segundo: espera.
    set_prop(b, 0x27);
    a->attack_time = 990;
    CHECK(DroneVision_DetermineAlertCheck(a, b) == 0 && (b->flags & 0x20000) == 0);
    a->attack_time = 0;
    // Aliados não são alertados.
    b->side = 2;
    CHECK(DroneVision_DetermineAlertCheck(a, b) == 0 && (b->flags & 0x20000) == 0);
    restore_tables();
}

TEST(drone_vision_find_opponent_enemies_target_the_player) {
    fresh();
    Drone_tag* e = make_drone(0, {0, 0, 0, 0}, nullptr, 1);
    obj_tag* p = make_obj(3, 3, {4, 0, 4, 0}, nullptr);
    std::memset(&g_bloke, 0, sizeof(g_bloke));
    p->data = &g_bloke;
    g_bloke.health = 1.0f;
    glb_players[0] = p;
    NDrone2_FindOpponent(e);
    CHECK(e->opponent == p && e->opponent_player == 0);
    CHECK(e->target_pos.x == 4.0f && e->target_pos.z == 4.0f);  // sem animação: a posição
    // Jogador morto: sem oponente.
    g_bloke.health = 0.0f;
    NDrone2_FindOpponent(e);
    CHECK(e->opponent == nullptr && e->opponent_player == 0xff);
    glb_players[0] = nullptr;
    restore_tables();
}

TEST(drone_vision_find_opponent_allies_pick_visible_enemies) {
    fresh();
    FloorRoom room;
    // O aliado e um inimigo embaixo do piso; outro inimigo em cima (o piso o esconde do aliado).
    Drone_tag* ally = make_drone(0, {0, -3, 0, 0}, room.cel, 2);
    Drone_tag* hidden = make_drone(1, {3, 2, 3, 0}, room.cel, 1);
    Drone_tag* visible = make_drone(2, {0, -3, 8, 0}, room.cel, 1);
    (void)hidden;
    // Cada chamada para no primeiro teste de visão que falha e retoma do seguinte na próxima.
    CHECK(NDrone2_FindOpponent(ally) == 0 && ally->opponent == nullptr && ally->unknown_2c0 == hidden);
    NDrone2_FindOpponent(ally);
    CHECK(ally->opponent == visible->obj && ally->unknown_2c0 == visible);
    CHECK(ally->opponent_player == 0xff);  // aliados não ocupam a tabela de alvos
    // Mantém o oponente enquanto o vê; perdido de vista, larga.
    NDrone2_FindOpponent(ally);
    CHECK(ally->opponent == visible->obj);
    visible->obj->pos = {0, 3, 8, 0};
    NDrone2_FindOpponent(ally);
    CHECK(ally->opponent != visible->obj);
    restore_tables();
}

TEST(drone_vision_aware_points_from_projectiles_and_smoke) {
    fresh();
    alignas(16) static BU_tag bu[2];
    alignas(16) static weapon_definition_tag defs[2];
    std::memset(bu, 0, sizeof(bu));
    std::memset(defs, 0, sizeof(defs));
    obj_tag* smoke = make_obj(3, 0x2a, {1, 0, 0, 0}, nullptr);
    obj_tag* grenade = make_obj(4, 5, {2, 0, 0, 0}, nullptr);
    obj_tag* other = make_obj(5, 5, {3, 0, 0, 0}, nullptr);
    defs[0].id = 0x2c;  // granada: perigo, raio 12
    defs[1].id = 0x10;  // bala comum: nada
    bu[0].weapon = &defs[0];
    bu[1].weapon = &defs[1];
    grenade->data = &bu[0];
    other->data = &bu[1];
    DynamicObjList.next = smoke;
    smoke->next = grenade;
    grenade->next = other;
    other->next = nullptr;
    Drone_BuildDynamicAwarePoints();
    const NPCGLOBALS_tag& n = NPCGlobals;
    CHECK(n.aware_count == 3);
    CHECK(n.aware_points[0].obj == smoke && n.aware_points[0].kind == 0 && n.aware_points[0].radius == 3.0f);
    CHECK(n.aware_points[1].obj == smoke && n.aware_points[1].kind == 2);
    CHECK(n.aware_points[2].obj == grenade && n.aware_points[2].kind == 2 && n.aware_points[2].radius == 12.0f);
    CHECK(n.aware_points[2].pos.x == 2.0f);
    // No multijogador não monta.
    uint32_t on = 1;
    std::memcpy(MPSettings + 0x184, &on, 4);
    NPCGlobals.aware_count = 7;
    Drone_BuildDynamicAwarePoints();
    CHECK(NPCGlobals.aware_count == 7);
    DynamicObjList.next = nullptr;
    restore_tables();
}

TEST(drone_vision_volume_factor_multiplies) {
    fresh();
    alignas(16) static AIVolume_tag vols[3];
    alignas(16) static obj_tag owners[3];
    std::memset(vols, 0, sizeof(vols));
    std::memset(owners, 0, sizeof(owners));
    const float values[3] = {0.5f, 0.5f, 0.25f};
    const uint16_t kinds[3] = {0, 0, 2};
    for (int i = 0; i < 3; ++i) {
        for (int r = 0; r < 4; ++r) owners[i].matrix.m[r][r] = 1.0f;
        owners[i].matrix.m[3][0] = float(i);  // caixas de meia medida 2 centradas em x = 0, 1, 2
        vols[i].obj = &owners[i];
        vols[i].half = {2, 2, 2, 0};
        vols[i].value = values[i];
        vols[i].kind = kinds[i];
        LList_Add(&AIBoxList, &vols[i].node);
    }
    _VECTOR in_both{0.5f, 0, 0, 0}, in_first{-1.5f, 0, 0, 0}, outside{10, 0, 0, 0};
    CHECK(Drone_VisibilityForPosition(&in_both, 0) == 0.25f);
    CHECK(Drone_VisibilityForPosition(&in_first, 0) == 0.5f);
    CHECK(Drone_VisibilityForPosition(&outside, 0) == 1.0f);
    CHECK(Drone_VisibilityForPosition(&in_both, 2) == 0.25f);  // só o do tipo pedido
    std::memset(&AIBoxList, 0, sizeof(AIBoxList));
    restore_tables();
}

TEST(drone_vision_peek_sfx_skips_commands) {
    hashtable_initialise();
    // Cabeçalho de 8 bytes (+6: comandos), depois comandos: {tipo | comprimento << 8, ...}. O tipo 0
    // ocupa comprimento + 4 palavras, os tipos 2 a 4 comprimento + 3; o 1 é o som (palavra 2).
    alignas(4) static uint16_t script[] = {0, 0, 0, 3,
                                           0x0100, 1, 2, 3, 4,  // tipo 0, comprimento 1: 5 palavras
                                           0x0202, 1, 2, 3, 4,  // tipo 2, comprimento 2: 5 palavras
                                           0x0001, 0, 0x1234};
    hashtable_additem(0x6000123, script);
    CHECK(AnimScriptPeekSfx(0x6000123) == 0x1234);
    alignas(4) static uint16_t unknown[] = {0, 0, 0, 2, 0x0005, 0, 0x0001, 0, 0x55};
    hashtable_additem(0x6000124, unknown);
    CHECK(AnimScriptPeekSfx(0x6000124) == 0);  // comando desconhecido antes do som
    CHECK(AnimScriptPeekSfx(0x6000125) == 0);  // sem script
}
