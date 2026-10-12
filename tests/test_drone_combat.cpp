// Testes dos estados de combate dos personagens da IA (engine/drone_combat.cpp) sem dados do jogo:
// tabelas sintéticas, estados de teste no lugar dos que não interessam e propriedades de um bit.

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ainetwork.h"
#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/map_types.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "test_framework.h"

namespace {

alignas(16) Drone_tag g_drones[3];
alignas(16) obj_tag g_objs[4];
alignas(16) ANIMOBJ_tag g_anims[3];
std::vector<std::string> g_log;
uint8_t g_saved_descs[sizeof(bitDescs)];
uint32_t g_saved_masks[35];

void identity(MATRIX& m) {
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
}

// Cada propriedade de comportamento p vira um bit: palavra p / 32, bit p % 32.
void one_bit_properties() {
    std::memcpy(g_saved_descs, bitDescs, sizeof(bitDescs));
    std::memcpy(g_saved_masks, bitMasks, sizeof(bitMasks));
    std::memset(bitMasks, 0, sizeof(bitMasks));
    bitMasks[1] = 1;  // usada só pelas propriedades que não interessam (todas zero)
    for (int p = 0; p < 91; ++p) {
        bitDescs[p * 2] = uint8_t((p >> 5) | ((p & 31) << 3));
        bitDescs[p * 2 + 1] = uint8_t(2 + (p & 31));
    }
    for (int b = 0; b < 32 && 2 + b < 35; ++b) bitMasks[2 + b] = 1u << b;
}

void restore_properties() {
    std::memcpy(bitDescs, g_saved_descs, sizeof(bitDescs));
    std::memcpy(bitMasks, g_saved_masks, sizeof(bitMasks));
}

void set_prop(Drone_tag* d, int p) { d->behaviour[p >> 5] |= 1u << (p & 31); }

void fresh() {
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.level_file = 0x7000001;
    GameState.frame = 1000;
    std::memset(MPSettings, 0, sizeof(MPSettings));
    for (obj_tag*& p : glb_players) p = nullptr;
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    LList_Init(Drone_List(), 0xd20, 0);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    FRAME_RATE_INT = 60;
    FRAME_RATE_DIV = 1.0f;
    g_log.clear();
}

// Personagem em pos olhando para +z, armado com a arma `weapon` (0 desarmado), sem máquina de estados.
Drone_tag* make_drone(int i, _VECTOR pos, int8_t weapon) {
    std::memset(&g_drones[i], 0, sizeof(Drone_tag));
    std::memset(&g_objs[i], 0, sizeof(obj_tag));
    std::memset(&g_anims[i], 0, sizeof(ANIMOBJ_tag));
    obj_tag* o = &g_objs[i];
    Drone_tag* d = &g_drones[i];
    o->type = 2;
    o->data = d;
    o->pos = pos;
    o->sphere_src = reinterpret_cast<uint8_t*>(&g_anims[i]);
    identity(o->matrix);
    g_anims[i].weapon = weapon;
    d->obj = o;
    d->behaviour = d->behaviour_a;
    d->health = 10.0f;
    d->flags = 0x100;
    d->type = 5;
    LList_Add(Drone_List(), &d->node);
    return d;
}

uint32_t state_recorder(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* m) {
    g_log.push_back("R:" + std::to_string(m->msg));
    return 1;
}

}  // namespace

TEST(drone_combat_move_quad_by_angle) {
    // De frente é 0, de costas 2; ângulo positivo (oponente à esquerda do passo) é 3, negativo 1.
    for (uint16_t prev = 0; prev < 4; ++prev) {
        CHECK(DroneAnim_GetMoveQuad(0.0f, prev) == 0);
        CHECK(DroneAnim_GetMoveQuad(3.0f, prev) == 2 && DroneAnim_GetMoveQuad(-3.0f, prev) == 2);
        CHECK(DroneAnim_GetMoveQuad(1.5f, prev) == 3 && DroneAnim_GetMoveQuad(-1.5f, prev) == 1);
    }
    // Vindo de um lado, o lado vale até mais para trás que vindo de frente.
    CHECK(DroneAnim_GetMoveQuad(2.4f, 0) == 2 && DroneAnim_GetMoveQuad(2.4f, 3) == 3);
    CHECK(DroneAnim_GetMoveQuad(-2.4f, 0) == 2 && DroneAnim_GetMoveQuad(-2.4f, 1) == 1);
    // Um quadrante anterior desconhecido não dá movimento de lado.
    CHECK(DroneAnim_GetMoveQuad(1.5f, 7) == 0);
}

TEST(drone_combat_corner_cover_position) {
    CoverNode_tag node{};
    node.pos = {10.0f, 2.0f, 20.0f, 1.0f};
    _VECTOR pos{}, rot{};
    // Sem lado utilizável, nada.
    CHECK(NDrone2_GetPosForCornerCover(&node, 0, &pos, &rot, 0) == 0);
    // Cobertura baixa (bit 8): 0,3 m atrás dela (1,1 m para sair), em qualquer direção.
    node.usable[2] = 8;
    for (float yaw : {0.0f, 1.0f, -2.5f}) {
        node.rot = {0.0f, yaw, 0.0f, 0.0f};
        CHECK(NDrone2_GetPosForCornerCover(&node, 2, &pos, &rot, 0) == 1);
        CHECK(std::fabs(Vec_Dist2D(&pos, &node.pos) - 0.3f) < 1e-3f && pos.y == 2.0f && rot.y == yaw);
        CHECK(NDrone2_GetPosForCornerCover(&node, 2, &pos, nullptr, 1) == 1);
        CHECK(std::fabs(Vec_Dist2D(&pos, &node.pos) - 1.1f) < 1e-3f);
    }
    // Olhando para +z (yaw 0), "atrás" é -z.
    node.rot = {};
    NDrone2_GetPosForCornerCover(&node, 2, &pos, nullptr, 0);
    CHECK(std::fabs(pos.x - 10.0f) < 1e-5f && std::fabs(pos.z - 19.7f) < 1e-4f);
    // Quina pelo lado 2 (bit 2): de lado pelo parâmetro da quina, e a rotação gira 90 graus.
    node.usable[0] = 2;
    node.corner_a = 5;
    node.rot = {0.0f, 0.5f, 0.0f, 0.0f};
    CHECK(NDrone2_GetPosForCornerCover(&node, 0, &pos, &rot, 0) == 1);
    CHECK(std::fabs(rot.y - (0.5f - 1.5707964f)) < 1e-5f);
    CHECK(std::fabs(Vec_Dist2D(&pos, &node.pos) - std::sqrt(0.5f * 0.5f + 0.1f * 0.1f)) < 1e-3f);
}

TEST(drone_combat_no_weapon_reaction) {
    fresh();
    Drone_tag* d = make_drone(0, {}, 0);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    // Desarmado: foge com medo, ou saca a arma secundária se tiver uma.
    CHECK(DroneFunc_AttackNoWeapon(&dcv) == 0x12);
    d->weapon2 = 5;
    CHECK(DroneFunc_AttackNoWeapon(&dcv) == 0x66);
    // Armado: ataca; um civil armado passa a inimigo.
    g_anims[0].weapon = 3;
    d->side = 3;
    CHECK(DroneFunc_AttackNoWeapon(&dcv) == 0 && d->side == 1);
}

TEST(drone_combat_attack_enters_combat) {
    fresh();
    NDrone2_StateFuncs[0x56] = NDrone2_DSTATE_Attack;
    for (uint32_t s : {0x12u, 0x2au, 0x58u, 0x67u}) NDrone2_StateFuncs[s] = state_recorder;
    // Já atacando (sem primeira reação) e armado: combate.
    Drone_tag* d = make_drone(0, {}, 3);
    d->flags |= 0x2000000;
    g_objs[0].state = 0x56;
    Drone_SM_InitObject(&g_objs[0]);
    // (0x10000 é ligado aqui, mas a troca de estado o desliga até o novo estado ligar de novo.)
    CHECK(d->sm.state == 0x58 && (d->flags & 0x400000) != 0 && d->alert == 1.0f);
    // Atirador de elite (tipo 2): mira de longe.
    fresh();
    NDrone2_StateFuncs[0x56] = NDrone2_DSTATE_Attack;
    for (uint32_t s : {0x12u, 0x2au, 0x58u, 0x67u}) NDrone2_StateFuncs[s] = state_recorder;
    d = make_drone(0, {}, 3);
    d->flags |= 0x2000000;
    d->type = 2;
    g_objs[0].state = 0x56;
    Drone_SM_InitObject(&g_objs[0]);
    CHECK(d->sm.state == 0x2a);
    // Desarmado: foge.
    fresh();
    NDrone2_StateFuncs[0x56] = NDrone2_DSTATE_Attack;
    for (uint32_t s : {0x12u, 0x2au, 0x58u, 0x67u}) NDrone2_StateFuncs[s] = state_recorder;
    d = make_drone(0, {}, 0);
    d->flags |= 0x2000000;
    g_objs[0].state = 0x56;
    Drone_SM_InitObject(&g_objs[0]);
    CHECK(d->sm.state == 0x12);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
}

TEST(drone_combat_first_attack_starts_the_attack) {
    fresh();
    NDrone2_StateFuncs[0x56] = NDrone2_DSTATE_Attack;
    NDrone2_StateFuncs[0x67] = state_recorder;
    // Primeira vez: liga as flags de ataque, alerta máximo, alcance de visão ampliado e todo lado, e
    // segue o estado de ataque do tipo (que é consumido).
    Drone_tag* d = make_drone(0, {}, 3);
    d->health = 0.0f;  // sem fala
    d->attack_state = 0x67;
    d->range_e8 = 10.0f;
    d->range_fc = 30.0f;
    d->view_angle = 1.5f;
    g_objs[0].state = 0x56;
    Drone_SM_InitObject(&g_objs[0]);
    CHECK(d->sm.state == 0x67 && (d->flags & 0x6000000) == 0x6000000 && d->alert == 1.0f);
    CHECK(d->range_e8 == 30.0f && d->view_angle == 0.0f && d->attack_time == GameState.frame);
    CHECK(d->initial_state == 0x56 && d->attack_state == 0 && d->unknown_28 == 1);
    // Em 07000009 o alcance fica.
    fresh();
    GameState.level_file = 0x7000009;
    NDrone2_StateFuncs[0x56] = NDrone2_DSTATE_Attack;
    NDrone2_StateFuncs[0x67] = state_recorder;
    d = make_drone(0, {}, 3);
    d->health = 0.0f;
    d->attack_state = 0x67;
    d->range_e8 = 10.0f;
    d->range_fc = 30.0f;
    d->view_angle = 1.5f;
    g_objs[0].state = 0x56;
    Drone_SM_InitObject(&g_objs[0]);
    CHECK(d->sm.state == 0x67 && d->range_e8 == 10.0f && d->view_angle == 1.5f);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
}

TEST(drone_combat_state_without_opponent) {
    fresh();
    Drone_tag* d = make_drone(0, {}, 3);
    d->sm.state = 0x58;
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    // Sem oponente nem visto há pouco: sem oponente (0x60).
    CHECK(DroneFunc_CombatState(&dcv) == 0x60);
    d->seen_time = GameState.frame - 40;
    CHECK(DroneFunc_CombatState(&dcv) == 0x60);
    // Visto há 10 quadros: continua; sem poder andar (0x10), combate parado (0x59).
    d->seen_time = GameState.frame - 10;
    d->flags |= 0x10;
    CHECK(DroneFunc_CombatState(&dcv) == 0x59);
    // Numa transição de animação, fica.
    d->anim_kind = 5;
    CHECK(DroneFunc_CombatState(&dcv) == 0);
}

TEST(drone_combat_choose_move_without_options) {
    fresh();
    one_bit_properties();
    Drone_tag* d = make_drone(0, {}, 3);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    // Sem propriedades de movimento só sobra mirar parado (0x67) ou ficar; atirando parado (0x68),
    // mirar parado não é uma troca.
    bool saw_aim = false;
    for (int i = 0; i < 200; ++i) {
        d->sm.state = 0x58;
        const uint32_t r = NDrone2_ChooseCombatMove(&dcv);
        CHECK(r == 0 || r == 0x67);
        saw_aim = saw_aim || r == 0x67;
        d->sm.state = 0x68;
        CHECK(NDrone2_ChooseCombatMove(&dcv) == 0);
    }
    CHECK(saw_aim);
    // Sem poder sair do lugar, nunca.
    d->flags |= 0x10;
    d->sm.state = 0x58;
    CHECK(NDrone2_ChooseCombatMove(&dcv) == 0);
    restore_properties();
}

TEST(drone_combat_cover_node_ok) {
    fresh();
    one_bit_properties();
    Drone_tag* d = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    Drone_tag* other = make_drone(1, {50.0f, 0.0f, 50.0f, 1.0f}, 3);
    std::memset(&g_objs[3], 0, sizeof(obj_tag));
    g_objs[3].pos = {0.0f, 0.0f, 20.0f, 1.0f};
    d->opponent = &g_objs[3];
    d->opponent_player = 1;
    d->unknown_f8 = 2.0f;
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    CoverNode_tag node{};
    node.type = 1;
    node.pos = {0.0f, 0.0f, 10.0f, 1.0f};
    // Cobertura baixa contra o lado 1 só com a propriedade 0xf.
    node.usable[1] = 8;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 0);
    set_prop(d, 0xf);
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 3);
    // Não serve contra outro lado, nem sem oponente, nem perto demais dele.
    node.usable[1] = 2;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 0);
    node.usable[1] = 8;
    d->unknown_f8 = 15.0f;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 0);
    d->unknown_f8 = 2.0f;
    // Com `force` exige os bits 0x70.
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 1) == 0);
    node.usable[1] = 0x18;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 1) == 3);
    // Outro personagem vivo a menos de 1,5 m a ocupa (a não ser para quem já tem cobertura, 0x1000).
    g_objs[1].pos = {0.0f, 0.0f, 11.0f, 1.0f};
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 0);
    d->flags |= 0x1000;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 3);
    d->flags &= ~0x1000u;
    other->flags |= 0x200;  // morto não ocupa
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 3);
    d->opponent = nullptr;
    CHECK(NDrone2_CoverNodeOK(&dcv, &node, 0, 0) == 0);
    restore_properties();
}

TEST(drone_combat_slide_to_position) {
    fresh();
    Drone_tag* d = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    _VECTOR to{10.0f, 4.0f, -10.0f, 1.0f};
    // Uma fração do caminho no plano; a altura só com with_y.
    NDrone2_SlideToPosition(&to, 0.1f, 0, &dcv);
    CHECK(std::fabs(g_objs[0].pos.x - 1.0f) < 1e-5f && std::fabs(g_objs[0].pos.z + 1.0f) < 1e-5f);
    CHECK(g_objs[0].pos.y == 0.0f);
    NDrone2_SlideToPosition(&to, 0.5f, 1, &dcv);
    CHECK(std::fabs(g_objs[0].pos.y - 2.0f) < 1e-5f);
    // A menos de 1 cm, encosta de vez.
    g_objs[0].pos = {9.995f, 4.0f, -10.0f, 0.0f};
    NDrone2_SlideToPosition(&to, 0.1f, 0, &dcv);
    CHECK(g_objs[0].pos.x == 10.0f && g_objs[0].pos.w == 1.0f);
}

TEST(drone_combat_angle_to_cover) {
    fresh();
    Drone_tag* d = make_drone(0, {}, 3);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    std::memset(&g_objs[3], 0, sizeof(obj_tag));
    g_objs[3].pos = {10.0f, 0.0f, 0.0f, 1.0f};
    CoverNode_tag node{};
    node.rot = {0.0f, 1.0f, 0.0f, 0.0f};
    // Sem cobertura ou sem oponente, nada.
    CHECK(NDrone2_SetAngleToCover(&dcv, 0.0f) == 0);
    d->cover = &node;
    CHECK(NDrone2_SetAngleToCover(&dcv, 0.0f) == 0);
    d->opponent = &g_objs[3];
    // Quina pelo lado 2: a direção da cobertura; pelo lado 1, 90 graus para a direita dela.
    d->cover_kind = 2;
    CHECK(NDrone2_SetAngleToCover(&dcv, 0.0f) == 1 && d->move_angles.y == 1.0f);
    d->cover_kind = 1;
    CHECK(NDrone2_SetAngleToCover(&dcv, 0.0f) == 1 && std::fabs(d->move_angles.y - (1.0f - 1.5707964f)) < 1e-5f);
    // Cobertura baixa: para o oponente (+x).
    node.type = 1;
    CHECK(NDrone2_SetAngleToCover(&dcv, 0.0f) == 0);
    CHECK(std::fabs(d->move_angles.y - ATAN2_APPROX(10.0f, 0.0f)) < 1e-5f);
}

TEST(drone_combat_door_is_open_and_route_door_nodes) {
    fresh();
    NDrone2_StateFuncs[0x81] = state_recorder;
    NDrone2_StateFuncs[5] = state_recorder;
    Drone_tag* d = make_drone(0, {}, 3);
    g_objs[0].state = 5;
    Drone_SM_InitObject(&g_objs[0]);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    // Sem rota (ou sem porta), a porta conta como aberta.
    CHECK(NDrone2_DoorIsOpen(&dcv) == 1);
    // Uma rota de três nós: 0 comum, 1 e 2 de porta (a porta fica no nó 1).
    alignas(16) MAPNODE_tag map[3] = {};
    NODE_tag nodes[3] = {};
    for (int i = 0; i < 3; ++i) nodes[i].node = &map[i];
    map[1].flags = 2;
    map[2].flags = 2;
    std::memset(&g_objs[3], 0, sizeof(obj_tag));
    g_objs[3].type = 0x1c;
    g_objs[3].state = 2;
    nodes[1].door = &g_objs[3];
    AIPath_tag path{};
    path.nodes = nodes;
    path.node_count = 3;
    uint16_t list[3] = {0, 1, 2};
    AIRoute_tag& route = d->route;
    route.list = list;
    route.path = &path;
    route.count = 3;
    // Chegando ao nó 1, com o seguinte também de porta: vai abrir (0x81) e lembra o estado de antes.
    route.current = 1;
    NDrone2_ReachedDestNode(&route, &dcv);
    CHECK(d->sm.next_state == 0x81 && d->sm.change == 1 && d->door == &g_objs[3] && d->node_return_state == 5 && d->action_node == 1);
    // A porta da rota fechada não está aberta; aberta (estado 4), está.
    d->active_route = &route;
    CHECK(NDrone2_DoorIsOpen(&dcv) == 0);
    g_objs[3].state = 4;
    CHECK(NDrone2_DoorIsOpen(&dcv) == 1);
    // No nó 2 (último de porta), com a porta aberta, esquece a porta.
    route.current = 2;
    NDrone2_ReachedDestNode(&route, &dcv);
    CHECK(d->door == nullptr);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
}

TEST(drone_combat_validate_cover_anim) {
    fresh();
    Drone_tag* d = make_drone(0, {}, 3);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    const DroneAnimState saved5 = DroneAnimStates[5], saved7 = DroneAnimStates[7], saved9 = DroneAnimStates[9];
    DroneAnimTableEntry saved_rows[3][kDroneAnimSubclasses];
    std::memcpy(saved_rows, Drone_AnimTables[400], sizeof(saved_rows));
    std::memset(Drone_AnimTables[400], 0, sizeof(saved_rows));
    DroneAnimStates[5].danim = 400;
    DroneAnimStates[7].danim = 401;
    DroneAnimStates[9].danim = 402;
    // Animação que existe volta igual; 0 também.
    Drone_AnimTables[402][0].script = 1;
    CHECK(DroneAnim_ValidateCoverAnim(&dcv, 9) == 9 && DroneAnim_ValidateCoverAnim(&dcv, 0) == 0);
    // Agachar mirando (5) sem animação vira 7 se existe, senão 10.
    CHECK(DroneAnim_ValidateCoverAnim(&dcv, 5) == 10);
    Drone_AnimTables[401][0].script = 1;
    CHECK(DroneAnim_ValidateCoverAnim(&dcv, 5) == 7);
    // Outras inexistentes voltam como estão.
    Drone_AnimTables[402][0].script = 0;
    CHECK(DroneAnim_ValidateCoverAnim(&dcv, 9) == 9 && DroneAnim_ValidateCoverAnim(&dcv, 0x80) == 0x80);
    std::memcpy(Drone_AnimTables[400], saved_rows, sizeof(saved_rows));
    DroneAnimStates[5] = saved5;
    DroneAnimStates[7] = saved7;
    DroneAnimStates[9] = saved9;
}

TEST(drone_combat_plane_from_two_vectors) {
    // A normal é perpendicular às duas direções e o ponto está no plano.
    const _VECTOR a{1.0f, 2.0f, 0.5f, 0.0f}, b{-0.5f, 1.0f, 3.0f, 0.0f}, p{4.0f, -2.0f, 7.0f, 1.0f};
    _VECTOR pl{};
    plane_from_two_vectors_and_point(&pl, &a, &b, &p);
    CHECK(std::fabs(pl.x * a.x + pl.y * a.y + pl.z * a.z) < 1e-5f);
    CHECK(std::fabs(pl.x * b.x + pl.y * b.y + pl.z * b.z) < 1e-5f);
    CHECK(std::fabs(pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w) < 1e-4f);
}

TEST(drone_combat_door_guard_zones) {
    fresh();
    // Dois guardas (tipo 0x16) de cada lado de uma porta em x = -1 e x = 1, olhando para +z.
    Drone_tag* a = make_drone(0, {-1.0f, 0.0f, 0.0f, 1.0f}, 0);
    Drone_tag* b = make_drone(1, {1.0f, 0.0f, 0.0f, 1.0f}, 0);
    a->type = b->type = 0x16;
    std::memset(&g_objs[3], 0, sizeof(obj_tag));
    a->opponent = b->opponent = &g_objs[3];
    DCVars_tag da{a->obj, a, nullptr, &a->sm}, db{b->obj, b, nullptr, &b->sm};
    CHECK(NDrone2_SetDronePartner(0x16, &da) == 1);
    CHECK(a->unknown_2bc == b->obj && b->unknown_2bc == a->obj);
    // Um dos dois guarda os planos; o outro lê a zona do parceiro.
    CHECK((a->guard_zone == 0xff) != (b->guard_zone == 0xff));
    DCVars_tag* owner = a->guard_zone == 0xff ? &db : &da;
    DCVars_tag* other = owner == &da ? &db : &da;
    const auto zone_at = [&](float x, float z) {
        g_objs[3].pos = {x, 0.0f, z, 1.0f};
        const uint32_t zone = NDrone2_DoorGuardHandler(owner);
        CHECK(NDrone2_DoorGuardHandler(other) == zone);
        return zone;
    };
    // Na frente da porta: chegando (2 a 6 m), perto (até 2 m); passando (atrás); longe ou de lado, nada.
    CHECK(zone_at(0.0f, 4.0f) == 1);
    CHECK(zone_at(0.5f, 1.0f) == 2);
    CHECK(zone_at(0.0f, -1.0f) == 3);
    CHECK(zone_at(0.0f, 10.0f) == 0);
    CHECK(zone_at(5.0f, 1.0f) == 0);
    // A mais de 2 m de altura, nada (sem gravar a zona).
    g_objs[3].pos = {0.0f, 3.0f, 1.0f, 1.0f};
    CHECK(NDrone2_DoorGuardHandler(owner) == 0);
}

TEST(drone_combat_second_behaviour_only_in_mode_100) {
    fresh();
    Drone_tag* d = make_drone(0, {}, 3);
    CHECK(DroneFunc_Set2ndBehaviour(d) == 0 && d->behaviour == d->behaviour_a);
    d->divars.params[5] = 100;
    CHECK(DroneFunc_Set2ndBehaviour(d) == 1 && d->behaviour == d->behaviour_b && DroneFunc_Using2ndBehaviour(d) == 1);
    CHECK(DroneFunc_Set1stBehaviour(d) == 1 && d->behaviour == d->behaviour_a && DroneFunc_Using2ndBehaviour(d) == 0);
}

TEST(drone_combat_interrogation) {
    fresh();
    // Um inimigo armado diante de um oponente desarmado a 3 m.
    Drone_tag* d = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    d->side = 1;
    make_drone(1, {0.0f, 0.0f, 3.0f, 1.0f}, 0);
    d->opponent = &g_objs[1];
    d->opp_dist = 3.0f;
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    MsgObject m{};
    const auto send = [&](uint32_t msg) {
        m.msg = msg;
        return NDrone2_DSTATE_Interogate(&dcv, d, d->obj, &m);
    };
    const auto changed_to = [&](uint32_t state) {
        const bool ok = d->sm.change == 1 && d->sm.next_state == state;
        d->sm.change = 0;
        d->sm.next_state = 0;
        return ok;
    };
    // Entrar: começa o interrogatório (sem trocar de estado), guarda distância, posição e quadro, e
    // marca o oponente como desarmado.
    CHECK(send(1) == 1 && d->sm.change == 0);
    CHECK((d->flags2 & 0x20) != 0 && NPCGlobals.interrogating == 1);
    CHECK(d->interrogate_dist == 3.0f && d->interrogate_min == 3.0f && d->interrogate_time == 1000);
    CHECK(d->interrogate_pos.z == 3.0f && (d->sight_flags & 0x10) == 0);
    CHECK(d->timer1_on == 1 && d->timer1_time == 1060 && d->state_a == 0);
    // Chegar mais perto baixa o mínimo; afastar-se até 4 m além dele não muda nada; mais, ataca.
    d->opp_dist = 2.0f;
    CHECK(send(3) == 1 && d->interrogate_min == 2.0f && d->sm.change == 0);
    d->opp_dist = 5.5f;
    CHECK(send(3) == 1 && d->interrogate_min == 2.0f && d->sm.change == 0);
    d->opp_dist = 6.5f;
    send(3);
    CHECK(changed_to(0x56));
    // O oponente desarmado que saca a arma leva ao ataque.
    d->opp_dist = 3.0f;
    g_anims[1].weapon = 3;
    send(3);
    CHECK(changed_to(0x56));
    // Armado desde o começo (bit 0x10), continuar armado não é motivo.
    send(1);
    CHECK((d->sight_flags & 0x10) != 0);
    send(3);
    CHECK(d->sm.change == 0);
    // O roteiro (fase comum): três passos pelo temporizador (3 s, 3 s, 1 s) e então o ataque.
    GameState.frame = 2000;
    send(0xc);
    CHECK(d->state_a == 1 && d->timer1_time == 2180 && d->sm.change == 0);
    send(0xc);
    CHECK(d->state_a == 2 && d->timer1_time == 2180);
    send(0xc);
    CHECK(d->state_a == 3 && d->timer1_time == 2060 && d->sm.change == 0);
    send(0xc);
    CHECK(changed_to(0x56));
    // Sair encerra o interrogatório.
    send(2);
    CHECK((d->flags2 & 0x20) == 0 && (d->flags2 & 0x10) != 0 && NPCGlobals.interrogating == 0);
    // Civil, desarmado ou sem oponente: não interroga.
    d->side = 3;
    send(1);
    CHECK(changed_to(0x56));
    d->side = 1;
    g_anims[0].weapon = 0;
    send(1);
    CHECK(changed_to(0x56));
    d->opponent = nullptr;
    send(1);
    CHECK(changed_to(0x60));
    // Andando até ele (0x3d), a menos de 4 m passa a interrogar.
    d->opponent = &g_objs[1];
    d->interrogate_min = 10.0f;
    d->opp_dist = 3.5f;
    m.msg = 3;
    NDrone2_DSTATE_InterogateWalk(&dcv, d, d->obj, &m);
    CHECK(changed_to(0x3c));
}

TEST(drone_combat_ally_waits_for_channel_and_hides) {
    fresh();
    Drone_tag* d = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    d->side = 2;
    // Uma sala vazia (sem nós nem portais), para que nada procure a sala pela posição.
    static cel_tag room;
    std::memset(&room, 0, sizeof(room));
    cel_tag* some_room = &room;
    d->route2.from.pos = {0.0f, 0.0f, 5.0f, 1.0f};  // sem nós, o próximo ponto é a origem
    d->route2.from.cel = some_room;
    d->route2.current = -1;
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    MsgObject m{};
    const auto changed_to = [&](uint32_t state) {
        const bool ok = d->sm.change == 1 && d->sm.next_state == state;
        d->sm.change = 0;
        d->sm.next_state = 0;
        return ok;
    };
    // Esperando a missão: o canal vem da troca de estado (0x99 sem ele); guia de novo quando liga.
    std::memset(switch_channels, 0, sizeof(switch_channels));
    d->sm.change_param = 0x30;
    m.msg = 1;
    CHECK(NDrone2_DSTATE_AllyLeadMissionWait(&dcv, d, d->obj, &m) == 1);
    CHECK(d->state_a == 0x30 && d->active_route == &d->route2 && d->move_target.pos.z == 5.0f);
    m.msg = 3;
    CHECK(NDrone2_DSTATE_AllyLeadMissionWait(&dcv, d, d->obj, &m) == 1 && d->sm.change == 0);
    switch_channels[0x30] = 1;
    NDrone2_DSTATE_AllyLeadMissionWait(&dcv, d, d->obj, &m);
    CHECK(changed_to(0x1d));
    d->sm.change_param = 0;
    m.msg = 1;
    NDrone2_DSTATE_AllyLeadMissionWait(&dcv, d, d->obj, &m);
    CHECK(d->state_a == 0x99);
    std::memset(switch_channels, 0, sizeof(switch_channels));
    // Escondido: sem inimigo armado perto, espera 2 s e procura a rota de missão de novo. Sem nós na
    // rota, o alvo vira o jogador (raio 2) e ele continua escondido.
    make_drone(1, {3.0f, 0.0f, 4.0f, 1.0f}, 0);
    g_objs[1].type = 3;
    g_objs[1].cel = some_room;
    glb_players[0] = &g_objs[1];
    m.msg = 1;
    NDrone2_DSTATE_AllyLeadHide(&dcv, d, d->obj, &m);
    CHECK(d->state_a == 0);
    m.msg = 3;
    NDrone2_DSTATE_AllyLeadHide(&dcv, d, d->obj, &m);
    CHECK(d->state_a == 1 && d->timer1_on == 1 && d->timer1_time == 1120);
    m.msg = 0xc;
    CHECK(NDrone2_DSTATE_AllyLeadHide(&dcv, d, d->obj, &m) == 1 && d->sm.change == 0);
    CHECK(d->goal_point.pos.pos.x == 3.0f && d->goal_point.pos.pos.z == 4.0f && d->goal_point.radius == 2.0f);
    glb_players[0] = nullptr;
}

TEST(drone_combat_hostage_killer_takes_nearest_hostage) {
    fresh();
    // Sequestrador na origem; reféns (tipo 5) a 4 m e 3 m.
    Drone_tag* k = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    k->type = 4;
    Drone_tag* h4 = make_drone(1, {0.0f, 0.0f, 4.0f, 1.0f}, 0);
    Drone_tag* h3 = make_drone(2, {3.0f, 0.0f, 0.0f, 1.0f}, 0);
    for (Drone_tag* h : {h4, h3}) h->type = 5;
    DCVars_tag dcv{k->obj, k, nullptr, &k->sm};
    CHECK(NDrone2_SetupHostageKiller(&dcv) == 1);
    CHECK(k->unknown_2b0 == h3->obj && h3->unknown_2b0 == k->obj && h4->unknown_2b0 == nullptr);
    // Os dois se olham: move_target no outro e o giro na direção dele (+x é giro pi/2).
    CHECK(k->move_target.pos.x == 3.0f && h3->move_target.pos.x == 0.0f);
    CHECK(std::fabs(k->move_angles.y - 1.5707964f) < 0.01f);
    CHECK(std::fabs(std::fabs(h3->move_angles.y) - 1.5707964f) < 0.01f && h3->move_angles.y < 0.0f);
    // Já com par, nada muda.
    CHECK(NDrone2_SetupHostageKiller(&dcv) == 1 && k->unknown_2b0 == h3->obj);
    // Um refém além de 5 m não serve.
    h4->obj->pos = {0.0f, 0.0f, 6.0f, 1.0f};
    k->unknown_2b0 = nullptr;
    h3->unknown_2b0 = k->obj;  // o de 3 m continua tomado
    CHECK(NDrone2_SetupHostageKiller(&dcv) == 0 && k->unknown_2b0 == nullptr);
    // Salvar um refém conta e liga o canal do parâmetro 6, uma vez.
    std::memset(switch_channels, 0, sizeof(switch_channels));
    NPCGlobals.unknown_19c = 0;
    h3->channel_b = 0x40;
    DCVars_tag dh{h3->obj, h3, nullptr, &h3->sm};
    DroneFunc_HostageSaved(&dh);
    CHECK(NPCGlobals.unknown_19c == 1 && switch_channels[0x40] == 1 && switch_channels_time[0x40] == 1000);
    DroneFunc_HostageSaved(&dh);
    CHECK(NPCGlobals.unknown_19c == 1);
    std::memset(switch_channels, 0, sizeof(switch_channels));
}

TEST(drone_combat_ambush_cover) {
    fresh();
    one_bit_properties();
    static cel_tag room;
    std::memset(&room, 0, sizeof(room));
    Drone_tag* d = make_drone(0, {0.0f, 0.0f, 0.0f, 1.0f}, 3);
    DCVars_tag dcv{d->obj, d, nullptr, &d->sm};
    // Três coberturas baixas olhando para +z: a 1 m (ocupada), a 1,5 m e a 3 m; mais a entrada depois
    // da última, cujo tipo decide quina ou baixa.
    static CoverNode_tag nodes[4];
    std::memset(nodes, 0, sizeof(nodes));
    const float zs[3] = {1.0f, 1.5f, 3.0f};
    for (int i = 0; i < 3; ++i) {
        nodes[i].type = 1;
        nodes[i].pos = {0.0f, 0.0f, zs[i], 1.0f};
        nodes[i].cel = &room;
        nodes[i].usable[0] = 8;
    }
    nodes[0].flags = 0x10;
    nodes[3].type = 1;
    CoverNode_tag* saved_nodes = NPCGlobals.cover_nodes;
    const uint32_t saved_count = NPCGlobals.cover_count;
    NPCGlobals.cover_nodes = nodes;
    NPCGlobals.cover_count = 3;
    d->opponent_player = 0;
    // Baixa só com a propriedade 0xf.
    CHECK(NDrone2_FindAmbushCover(&dcv) == 0);
    set_prop(d, 0xf);
    CHECK(NDrone2_FindAmbushCover(&dcv) == 1);
    CHECK(d->cover == &nodes[1] && nodes[1].user == d->obj && (nodes[1].flags & 0x10) != 0);
    CHECK((d->cover_kind & 3) == 3 && (d->flags & 0x1800) == 0x1800);
    // O alvo fica 0,3 m atrás dela (-z), com raio 0,5.
    CHECK(std::fabs(d->goal_point.pos.pos.z - 1.2f) < 1e-4f && d->goal_point.radius == 0.5f);
    // Peculiaridade: com a entrada depois da tabela do tipo 0, a mesma cobertura baixa é tratada como
    // quina (lado 1 ou 2).
    nodes[1].flags = 0;
    nodes[1].user = nullptr;
    d->cover = nullptr;
    nodes[3].type = 0;
    CHECK(NDrone2_FindAmbushCover(&dcv) == 1 && d->cover == &nodes[1]);
    CHECK((d->cover_kind & 3) == 1 || (d->cover_kind & 3) == 2);
    NPCGlobals.cover_nodes = saved_nodes;
    NPCGlobals.cover_count = saved_count;
    restore_properties();
}
