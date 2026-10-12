// Testes do corpo dos personagens da IA (engine/drone_move.cpp e drone_damage.cpp) sem dados do jogo:
// movimento e desvio, cápsula e gravidade, olhar e mira, dano e o caminho de um tiro até a morte.

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "test_framework.h"

namespace {

alignas(16) Drone_tag g_drones[3];
alignas(16) obj_tag g_objs[3];
alignas(16) ANIMOBJ_tag g_anims[3];
std::vector<uint32_t> g_msgs;

void identity(MATRIX& m) {
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
}

void fresh() {
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.level_file = 0x7000001;
    GameState.difficulty = 2;
    std::memset(MPSettings, 0, sizeof(MPSettings));
    for (obj_tag*& p : glb_players) p = nullptr;
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    LList_Init(Drone_List(), 0xd20, 0);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    std::memset(Drone_SfxInfo, 0, sizeof(Drone_SfxInfo));
    g_msgs.clear();
    REC_FRAME_RATE = 1.0f / 60.0f;
    FRAME_RATE_DIV = 1.0f;
    FRAME_RATE_MUL = 1.0f;
    FRAME_RATE_INT = 60;
    WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
    DroneDamage_Easy = 2.0f;
    DroneDamage_Normal = 1.5f;
    DroneDamage_Hard = 0.5f;
    DroneDamage_Head = 100.0f;
    DroneDamage_Legs = 0.75f;
    DroneDamage_Arms = 1.0f;
    DroneDamage_Torso = 1.0f;
    DroneArmour_Helmet = 0.5f;
    DroneArmour_Combat = 0.25f;
    DroneArmour_Jacket = 0.5f;
    DroneArmour_Vest = 0.75f;
}

// Um personagem ativo e vivo, de pé em pos, olhando para +z (rot.y = 0).
Drone_tag* make_drone(int i, _VECTOR pos, uint16_t state = 5) {
    std::memset(&g_drones[i], 0, sizeof(Drone_tag));
    std::memset(&g_objs[i], 0, sizeof(obj_tag));
    std::memset(&g_anims[i], 0, sizeof(ANIMOBJ_tag));
    obj_tag* o = &g_objs[i];
    Drone_tag* d = &g_drones[i];
    o->type = 2;
    o->data = d;
    o->state = state;
    o->pos = pos;
    o->old_pos = pos;
    o->sphere_src = reinterpret_cast<uint8_t*>(&g_anims[i]);
    identity(o->matrix);
    o->matrix.m[3][0] = pos.x;
    o->matrix.m[3][1] = pos.y;
    o->matrix.m[3][2] = pos.z;
    d->obj = o;
    d->flags = 0x100;
    d->health = 10.0f;
    d->max_health = 10.0f;
    d->vulnerable = 1;
    d->side = 1;
    d->root_y = 1.0f;
    d->hit_damage_mul = 1.0f;
    d->behaviour = d->behaviour_a;
    d->unknown_4a0 = 1.0f;
    LList_Add(Drone_List(), &d->node);
    Drone_SM_InitObject(o);
    return d;
}

DCVars_tag dcv_of(Drone_tag* d) {
    DCVars_tag dcv{};
    Drone_DCVfromOBJ(d->obj, &dcv);
    return dcv;
}

// Estado 5 de teste: guarda as mensagens e trata os impactos como um estado do jogo.
uint32_t state_5(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* m) {
    g_msgs.push_back(m->msg);
    if (m->msg == 8) return DroneFunc_HandleImpact(dcv, m, 0, 0);
    return 1;
}

}  // namespace

// ---- desvio de obstáculos ----

TEST(drone_sphere_sweep_finds_first_contact) {
    // Raios 0,5 + 0,5; a primeira esfera anda 1 por passo em x na direção da segunda, parada a 3.
    const _VECTOR p1{0, 0, 0, 0}, p1e{1, 0, 0, 0}, p2{3, 0, 0, 0};
    float t0 = -1.0f, t1 = -1.0f;
    CHECK(SphereSphereSweep(0.5f, p1, p1e, 0.5f, p2, p2, t0, t1) == 1);
    // No instante t0 a distância entre os centros é a soma dos raios.
    CHECK_NEAR(t0, 2.0f, 1e-5);
    CHECK_NEAR(std::fabs(p2.x - (p1.x + t0 * (p1e.x - p1.x))), 1.0f, 1e-5);
    // Já encostadas: os dois instantes são 0.
    const _VECTOR near{0.8f, 0, 0, 0};
    CHECK(SphereSphereSweep(0.5f, p1, p1e, 0.5f, near, near, t0, t1) == 1 && t0 == 0.0f && t1 == 0.0f);
    // Passando longe (a 3 em z, com raios somando 1): não se encontram.
    const _VECTOR far{0, 0, 3, 0};
    CHECK(SphereSphereSweep(0.5f, p1, p1e, 0.5f, far, far, t0, t1) == 0);
    // Peculiaridade preservada: sem movimento relativo o discriminante é 0 e o "contato" é em t = 0.
    const _VECTOR q{0, 0, 3, 0}, qe{1, 0, 3, 0};
    CHECK(SphereSphereSweep(0.5f, p1, p1e, 0.5f, q, qe, t0, t1) == 1 && t0 == 0.0f);
}

TEST(drone_steer_turns_away_from_obstacle) {
    fresh();
    // O personagem 0 anda para +z (0,1 por tick); o 1 está parado logo à frente, um pouco à esquerda (+x).
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    Drone_tag* other = make_drone(1, {0.3f, 0, 1.2f, 0});
    me->move_speed = 1.0f;
    me->moved = 0.1f;
    DCVars_tag dcv = dcv_of(me);
    NDrone2_Steer(&dcv, 0.0f);
    CHECK(me->move_rot.y < 0.0f);  // gira para a direita, para longe
    CHECK(me->nearest == other && std::fabs(me->nearest_dist - std::sqrt(0.09f + 1.44f)) < 1e-5f);
    // Do outro lado, gira para o outro lado.
    me->move_rot.y = 0.0f;
    other->obj->pos.x = -0.3f;
    NDrone2_Steer(&dcv, 0.0f);
    CHECK(me->move_rot.y > 0.0f);
    // Longe (mais de 3 m): só o giro pedido, à taxa unknown_4a0.
    me->move_rot.y = 0.0f;
    other->obj->pos = {0.3f, 0, 5.0f, 0};
    NDrone2_Steer(&dcv, 0.2f);
    CHECK_NEAR(me->move_rot.y, 0.2f, 1e-6);
    // Um morto não é obstáculo.
    me->move_rot.y = 0.0f;
    other->obj->pos = {0.3f, 0, 1.2f, 0};
    other->flags |= 0x200;
    NDrone2_Steer(&dcv, 0.0f);
    CHECK(me->move_rot.y == 0.0f && me->nearest == nullptr);
}

TEST(drone_no_bunching_reports_side_of_nearest) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    Drone_tag* other = make_drone(1, {0.5f, 0, 1.0f, 0});
    DCVars_tag dcv = dcv_of(me);
    me->nearest = other;
    CHECK(DroneMove_NoBunching(&dcv) == 1);
    CHECK(me->bunch_flags == (8u | 2u));  // à esquerda (+x) e à frente
    me->bunch_flags = 0;
    other->obj->pos = {-0.5f, 0, -1.0f, 0};
    CHECK(DroneMove_NoBunching(&dcv) == 1 && me->bunch_flags == (0x10u | 4u));  // à direita, atrás
    me->nearest = nullptr;
    CHECK(DroneMove_NoBunching(&dcv) == 0);
}

TEST(drone_move_applies_and_decays_push) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    me->control_flags = 0x8000000;  // sem girar
    me->push_offset = {0.1f, 0, 0, 0};
    DCVars_tag dcv = dcv_of(me);
    NDrone2_Move(&dcv);
    CHECK_NEAR(me->obj->pos.x, 0.1f, 1e-6);
    CHECK_NEAR(me->push_offset.x, 0.095f, 1e-6);
    NDrone2_Move(&dcv);
    // A posição segue o empurrão atual (o do tick anterior sai), sem acumular.
    CHECK_NEAR(me->obj->pos.x, 0.095f, 1e-6);
}

// ---- cápsula e gravidade ----

TEST(drone_capsule_spans_hip_to_feet) {
    fresh();
    Drone_tag* me = make_drone(0, {2, 5, 3, 0});
    me->flags |= 0x20;  // colide
    NDrone2_Collision(me->obj);
    const HITTEST_tag& t = me->hit_test;
    CHECK(t.type == 0x800 && t.radius == 0.4f && t.cel == me->obj->cel);
    // A esfera de baixo toca os pés (root_y abaixo da posição); a de cima fica um raio acima.
    CHECK_NEAR(t.end.y - t.radius, me->obj->pos.y - me->root_y, 1e-6);
    CHECK_NEAR(t.start.y, me->obj->pos.y + 0.4f, 1e-6);
    CHECK(t.start.x == 2.0f && t.end.z == 3.0f);
    // Sem a flag 0x20 a cápsula não age.
    me->flags &= ~0x20u;
    NDrone2_Collision(me->obj);
    CHECK(me->hit_test.type == 0);
    // Perto do jogador volta a colidir.
    alignas(16) static obj_tag player;
    glb_players[0] = &player;
    me->player_dist[0] = 5.0f;
    me->flags |= 0x20;
    me->anim_flags = 2;  // a DAnim anda sem colidir...
    NDrone2_Collision(me->obj);
    CHECK(me->hit_test.type == 0);
    me->player_dist[0] = 1.5f;  // ...a não ser com o jogador a menos de 2 m
    NDrone2_Collision(me->obj);
    CHECK(me->hit_test.type == 0x800);
    glb_players[0] = nullptr;
}

TEST(drone_gravity_integrates_fall) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 10, 0, 0});
    me->flags |= 0x80 | 4;  // gravidade sempre; sem 0x20, a cápsula não age
    const int ticks = 30;
    for (int i = 0; i < ticks; ++i) Drone_CollisionHandler(me->obj);
    const float dt = 1.0f / 60.0f;
    // Euler semi-implícito: v = n g dt, y = y0 + g dt² n (n + 1) / 2.
    CHECK_NEAR(me->fall.y, -9.8f * dt * ticks, 1e-4);
    CHECK_NEAR(me->obj->pos.y, 10.0f - 9.8f * dt * dt * ticks * (ticks + 1) / 2.0f, 1e-3);
    CHECK((me->obj->mode & 0x20) != 0);
    // Sem a flag 0x80 a queda vertical para.
    me->flags &= ~0x84u;
    const float y = me->obj->pos.y;
    Drone_CollisionHandler(me->obj);
    CHECK(me->fall.y == 0.0f && me->obj->pos.y == y);
}

TEST(drone_gravity_stops_in_water_and_system_off) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 10, 0, 0});
    me->flags |= 0x80 | 4;
    alignas(16) static cel_tag room;
    std::memset(&room, 0, sizeof(room));
    room.water_y = 20.0f;  // a água cobre o personagem
    me->obj->cel = &room;
    Drone_CollisionHandler(me->obj);
    CHECK(me->obj->pos.y == 10.0f && me->fall.y == 0.0f);
    room.water_y = -500.0f;
    NPCGlobals.enabled = 0;  // IA desligada: nada se move e a cápsula desliga
    me->hit_test.type = 0x800;
    Drone_CollisionHandler(me->obj);
    CHECK(me->obj->pos.y == 10.0f && me->hit_test.type == 0);
}

TEST(drone_zero_g_floats_and_damps) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    me->zero_g = 1;
    me->flags |= 0x40;  // amortecimento
    me->fall = {6.0f, 0, 0, 0};
    Drone_CollisionHandler(me->obj);
    CHECK_NEAR(me->obj->pos.x, 0.1f, 1e-6);
    CHECK_NEAR(me->fall.x, 6.0f * 0.98f, 1e-5);
}

TEST(drone_joint_boxes_catch_rays) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    // Esqueleto sintético: o osso i em (i, 2i, 0), sem rotação; o pai de i é i - 1.
    constexpr int kBones = 0x40;
    alignas(16) static MATRIX bones[kBones];
    static uint8_t parents[kBones];
    for (int i = 0; i < kBones; ++i) {
        identity(bones[i]);
        bones[i].m[3][0] = float(i);
        bones[i].m[3][1] = float(2 * i);
        parents[i] = uint8_t(0x80 | (i == 0 ? 0x7f : i - 1));
    }
    alignas(16) static ANIMSKIN_tag skin;
    std::memset(&skin, 0, sizeof(skin));
    skin.hash = 0x5000001;
    skin.parents = parents;
    skin.scale[0] = skin.scale[1] = skin.scale[2] = 2.0f;
    alignas(16) static uint8_t skel_bytes[0x20];
    std::memset(skel_bytes, 0, sizeof(skel_bytes));
    ANIMSKEL_tag* skel = reinterpret_cast<ANIMSKEL_tag*>(skel_bytes);
    skel->bone_count = kBones;
    ANIMSKEL_tag* saved_skel = pSkeletons[0];
    pSkeletons[0] = skel;
    ANIMOBJ_tag* a = anim_of(me->obj);
    a->skin = &skin;
    a->bones = bones;
    a->anim_flags = 1 | 2 | 4;  // personagem com as matrizes prontas
    const AnimCollBone saved[3] = {HumanCollisionBones[0], HumanCollisionBones[1], HumanCollisionBones[2]};
    HumanCollisionBones[0] = {5, -1, 0.0f, {0.1f, 0.1f, 0.1f}};      // cabeça: o outro é o pai (4)
    HumanCollisionBones[1] = {0x31, 0x30, 0.0f, {0.1f, 0.2f, 0.3f}};  // perna
    HumanCollisionBones[2] = {-1, -1, 0.0f, {0, 0, 0}};
    uint16_t n = 0;
    COLLJOINT_tag* j = AnimGetCollData(me->obj, &n);
    CHECK(n == 2 && j == HitList2);
    // A cabeça fica acima do osso 5 pela diferença de altura até o pai; as outras, entre os dois ossos.
    CHECK(j[0].joint == 5 && j[0].m.m[3][0] == 5.0f && j[0].m.m[3][1] == 12.0f);
    CHECK(j[1].joint == 0x31 && j[1].m.m[3][0] == 48.5f && j[1].m.m[3][1] == 97.0f);
    CHECK(j[1].half.x == 0.2f && j[1].half.y == 0.4f && j[1].half.z == 0.6f);  // vezes a escala da pele
    // Um raio pela caixa da cabeça acerta a junta 5 (Collide_Jointy).
    HITTEST_tag test{};
    test.type = 0x201;
    test.start = {5.0f, 12.0f, -5.0f, 0.0f};
    test.end = {5.0f, 12.0f, 5.0f, 0.0f};
    test.dir = {0.0f, 0.0f, 10.0f, 0.0f};
    test.radius = 10.0f;
    alignas(16) HITDATA_tag hit{};
    hit.obj = me->obj;
    CHECK(Collide_Jointy(&test, &hit) == 1 && hit.unknown_52 == 5);
    CHECK_NEAR(hit.pos.z, -0.2f, 1e-5);  // a face da caixa (meia medida 0,1 x escala 2)
    // Sem anim_flags 2 (não é personagem), nenhuma caixa.
    a->anim_flags = 1 | 4;
    CHECK(AnimGetCollData(me->obj, &n) == nullptr && n == 0);
    for (int i = 0; i < 3; ++i) HumanCollisionBones[i] = saved[i];
    pSkeletons[0] = saved_skel;
}

// ---- olhar e mira ----

TEST(drone_aiming_flags_round_trip) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    obj_tag* obj = me->obj;
    CHECK(!AnimObjectIsAiming(obj) && !AnimObjectIsHeadTracking(obj));
    _VECTOR target{};
    AnimObjectSetAiming(obj, &target, 10.0f, 0.2f, 1, 7);
    ANIMOBJ_tag* a = anim_of(obj);
    CHECK(AnimObjectIsAiming(obj) && a->aim_target == &target && a->aim_dist_sq == 100.0f && a->aim_bone == 7);
    CHECK((a->anim_flags & 0x200) != 0);
    AnimObjectSetAiming(obj, &target, 10.0f, 0.2f, 0, 0);
    CHECK((a->anim_flags & 0x200) == 0 && AnimObjectIsAiming(obj));
    AnimObjectSetAiming(obj, nullptr, 10.0f, 0.2f, 1, 5);
    CHECK(!AnimObjectIsAiming(obj) && a->aim_target == nullptr);
    AnimObjectSetHeadTracking(obj, &target, 3.0f, 0.14f, 0, 0, 5);
    CHECK(AnimObjectIsHeadTracking(obj));
    CHECK(!AnimObjectIsAiming(nullptr) && !AnimObjectIsHeadTracking(nullptr));
}

TEST(drone_aim_test_follows_anim_kind) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    DCVars_tag dcv = dcv_of(me);
    alignas(16) static ANIMSCRIPT_tag script;
    std::memset(&script, 0, sizeof(script));
    CHECK(NDrone2_AimTest(&dcv) == 0);  // sem script, não mira
    me->script = &script;
    me->anim_info_f = 2;
    CHECK(NDrone2_AimTest(&dcv) == 1);
    me->anim_info_f = 3;  // mira depois da metade do script...
    script.frame_count = 20;
    script.frame = 5.0f;
    CHECK(NDrone2_AimTest(&dcv) == 0);
    script.frame = 11.0f;
    CHECK(NDrone2_AimTest(&dcv) == 1);
    me->aim_from_events = 1;  // ...ou pelo evento da animação
    me->aim_event = 0;
    CHECK(NDrone2_AimTest(&dcv) == 0);
    me->anim_info_f = 0;
    CHECK(NDrone2_AimTest(&dcv) == 0);
}

// ---- dano ----

TEST(drone_hurt_object_accessors) {
    alignas(16) static obj_tag hurt;
    alignas(16) static uint8_t data[0x20];
    std::memset(&hurt, 0, sizeof(hurt));
    const float damage = 7.5f;
    std::memcpy(data + 0x10, &damage, 4);
    data[0x14] = 3;
    hurt.data = data;
    hurt.type = 0x27;
    CHECK(Hurt_GetDamage(&hurt) == 7.5f && Hurt_GetType(&hurt) == 3);
    hurt.type = 5;
    CHECK(Hurt_GetDamage(&hurt) == 0.0f && Hurt_GetType(&hurt) == 0);
    CHECK(Hurt_GetDamage(nullptr) == 0.0f);
}

TEST(drone_hit_damage_by_location_armour_and_difficulty) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    DCVars_tag dcv = dcv_of(me);
    const auto lost = [&](float damage, int32_t loc, int32_t weapon) {
        me->health = 1000.0f;
        NDrone2_HitDamage(&dcv, damage, loc, weapon, 0);
        return 1000.0f - me->health;
    };
    const float torso = lost(2.0f, 1, 2);
    CHECK(torso > 0.0f && me->last_damage == torso && me->head_shot == 0);
    // Cabeça: Head/Torso vezes o tronco, e marca o tiro na cabeça.
    CHECK_NEAR(lost(2.0f, 5, 2) / torso, DroneDamage_Head / DroneDamage_Torso, 1e-4);
    CHECK(me->head_shot == 1);
    // Um soco (arma 1) na cabeça conta como no tronco.
    CHECK_NEAR(lost(2.0f, 5, 1), torso, 1e-5);
    CHECK_NEAR(lost(2.0f, 0x33, 2) / torso, DroneDamage_Legs / DroneDamage_Torso, 1e-5);
    CHECK_NEAR(lost(2.0f, 0x15, 2) / torso, DroneDamage_Arms / DroneDamage_Torso, 1e-5);
    // Proteções: o capacete só protege a cabeça; o colete, o tronco.
    me->armour = 4;
    CHECK_NEAR(lost(2.0f, 5, 2) / lost(2.0f, 1, 2), DroneDamage_Head * DroneArmour_Helmet, 1e-3);
    me->armour = 1;
    CHECK_NEAR(lost(2.0f, 1, 2) / torso, DroneArmour_Vest, 1e-5);
    CHECK_NEAR(lost(2.0f, 0x33, 2) / torso, DroneDamage_Legs, 1e-5);
    me->armour = 0;
    // Dificuldade: fácil e difícil mudam o dano na proporção dos multiplicadores.
    GameState.difficulty = 1;
    const float easy = lost(2.0f, 1, 2);
    GameState.difficulty = 3;
    const float hard = lost(2.0f, 1, 2);
    CHECK_NEAR(easy / hard, DroneDamage_Easy / DroneDamage_Hard, 1e-5);
    // Quem não é vulnerável não perde vida, mas o dano fica guardado.
    me->vulnerable = 0;
    CHECK(lost(2.0f, 1, 2) == 0.0f && me->last_damage == 2.0f);
}

TEST(drone_bullet_from_player_kills_through_state) {
    fresh();
    NDrone2_StateFuncs[5] = state_5;
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    alignas(16) static obj_tag player;
    alignas(16) static obj_tag bullet_obj;
    alignas(16) static BU_tag bullet;
    static weapon_definition_tag weapon;
    std::memset(&player, 0, sizeof(player));
    std::memset(&bullet_obj, 0, sizeof(bullet_obj));
    std::memset(&bullet, 0, sizeof(bullet));
    std::memset(&weapon, 0, sizeof(weapon));
    player.type = 3;
    weapon.id = 2;
    weapon.damage = 4.0f;
    bullet.from = &player;
    bullet.weapon = &weapon;
    bullet_obj.type = 5;
    bullet_obj.data = &bullet;
    alignas(16) static HITDATA_tag hit;
    std::memset(&hit, 0, sizeof(hit));
    hit.obj = &bullet_obj;
    hit.dir = {0, 0, 1, 0};
    hit.unknown_52 = 1;  // tronco
    const float damage = 4.0f;
    std::memcpy(&hit.unknown_08, &damage, 4);
    g_msgs.clear();
    NDrone2_DealWithObjHit(me->obj, me, &hit);
    // A mensagem 8 chegou ao estado, que tratou o impacto: vida a menos, alerta, quem atirou, empurrão.
    CHECK((g_msgs == std::vector<uint32_t>{8}));
    CHECK_NEAR(me->health, 10.0f - 4.0f * DroneDamage_Torso * DroneDamage_Normal, 1e-5);
    CHECK(me->alert == 1.0f && me->alert_status == 2 && me->attacker == &player && me->hits_taken == 1);
    CHECK(me->last_hit == &hit);
    CHECK_NEAR(me->push_offset.z, 0.175f, 1e-6);
    // Mais tiros: o empurrão não passa de 0,175 m; sem vida, vai ao estado de morte 0x44.
    NDrone2_DealWithObjHit(me->obj, me, &hit);
    CHECK_NEAR(std::sqrt(me->push_offset.x * me->push_offset.x + me->push_offset.z * me->push_offset.z), 0.175f, 1e-6);
    CHECK(me->health <= 0.0f && me->sm.state == 0x44 && me->sm.prev_state == 5);
}

TEST(drone_bullet_friendly_fire_is_ignored) {
    fresh();
    NDrone2_StateFuncs[5] = state_5;
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    Drone_tag* mate = make_drone(1, {3, 0, 0, 0});
    alignas(16) static obj_tag bullet_obj;
    alignas(16) static BU_tag bullet;
    static weapon_definition_tag weapon;
    std::memset(&bullet_obj, 0, sizeof(bullet_obj));
    std::memset(&bullet, 0, sizeof(bullet));
    std::memset(&weapon, 0, sizeof(weapon));
    weapon.id = 2;
    bullet.from = mate->obj;  // outro inimigo (lado 1)
    bullet.weapon = &weapon;
    bullet_obj.type = 5;
    bullet_obj.data = &bullet;
    alignas(16) static HITDATA_tag hit;
    std::memset(&hit, 0, sizeof(hit));
    hit.obj = &bullet_obj;
    const float damage = 4.0f;
    std::memcpy(&hit.unknown_08, &damage, 4);
    g_msgs.clear();
    NDrone2_DealWithObjHit(me->obj, me, &hit);
    CHECK(g_msgs.empty() && me->health == 10.0f && me->hits_taken == 0);
    // Um aliado (lado 2) atirando num inimigo fere.
    mate->side = 2;
    NDrone2_DealWithObjHit(me->obj, me, &hit);
    CHECK((g_msgs == std::vector<uint32_t>{8}) && me->health < 10.0f);
}

TEST(drone_punch_and_shock_messages_by_weapon) {
    fresh();
    NDrone2_StateFuncs[5] = state_5;
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    alignas(16) static obj_tag player;
    alignas(16) static obj_tag bullet_obj;
    alignas(16) static BU_tag bullet;
    static weapon_definition_tag weapon;
    std::memset(&player, 0, sizeof(player));
    std::memset(&bullet, 0, sizeof(bullet));
    std::memset(&weapon, 0, sizeof(weapon));
    player.type = 3;
    bullet.from = &player;
    bullet.weapon = &weapon;
    bullet_obj.type = 5;
    bullet_obj.data = &bullet;
    alignas(16) static HITDATA_tag hit;
    std::memset(&hit, 0, sizeof(hit));
    hit.obj = &bullet_obj;
    struct Case {
        int16_t weapon;
        uint32_t msg;
    };
    const Case cases[] = {{1, 6}, {0x4a, 7}, {0x4d, 6}, {0x43, 0x19}};
    for (const Case& c : cases) {
        g_msgs.clear();
        weapon.id = c.weapon;
        NDrone2_DealWithObjHit(me->obj, me, &hit);
        CHECK((g_msgs == std::vector<uint32_t>{c.msg}));
    }
    // Armas que o personagem ignora (0x1b) não mandam nada.
    g_msgs.clear();
    weapon.id = 0x1b;
    NDrone2_DealWithObjHit(me->obj, me, &hit);
    CHECK(g_msgs.empty());
}

TEST(drone_explosion_side_picks_impact_variant) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    DCVars_tag dcv = dcv_of(me);
    DroneImpact_tag blast{};
    blast.damage = 1.0f;
    const auto variant_for = [&](float x, float z) {
        me->health = 1000.0f;
        me->call_state = 0;
        blast.pos = {x, 0, z, 0};
        NDrone2_ExplosiveImpact(&dcv, &blast, 0, 1);
        return me->call_variant;
    };
    CHECK(variant_for(0.0f, 5.0f) == 1);   // de frente
    CHECK(variant_for(0.0f, -5.0f) == 0);  // de trás
    // Dos lados: 2 e 3, um para cada lado.
    const uint16_t left = variant_for(5.0f, 0.0f), right = variant_for(-5.0f, 0.0f);
    CHECK(left != right && (left == 2 || left == 3) && (right == 2 || right == 3));
    CHECK(me->health < 1000.0f && me->alert == 1.0f);
    // A explosão do próprio 0x50000ba não o fere.
    me->skin = 0x50000ba;
    blast.source = me->obj;
    me->health = 5.0f;
    NDrone2_ExplosiveImpact(&dcv, &blast, 0, 1);
    CHECK(me->health == 5.0f);
}

TEST(drone_stun_accumulates_until_twice_health) {
    fresh();
    Drone_tag* me = make_drone(0, {0, 0, 0, 0});
    DCVars_tag dcv = dcv_of(me);
    me->health = 3.0f;
    MsgObject msg{7, 0, 0, 0, 0, 0, 0};
    // Cada choque soma um tick; até 2 x vida (6) só conta.
    for (int i = 0; i < 6; ++i) DroneFunc_HandleImpact(&dcv, &msg, 0, 0);
    CHECK(me->stun_time == 6 && me->sm.next_state != 0x4a);
    DroneFunc_HandleImpact(&dcv, &msg, 0, 0);
    CHECK(me->stun_time == 7 && me->sm.change == 1 && me->sm.next_state == 0x4a);
    // Com ignore_impacts, nada acontece.
    me->ignore_impacts = 1;
    DroneFunc_HandleImpact(&dcv, &msg, 0, 0);
    CHECK(me->stun_time == 7);
}
