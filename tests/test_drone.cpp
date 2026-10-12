// Testes do núcleo dos personagens da IA (engine/drone*.cpp) sem dados do jogo: tabelas sintéticas
// e funções de estado de teste no lugar das do ELF.

#include <cstring>
#include <string>
#include <vector>

#include "engine/drone.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "test_framework.h"

namespace {

alignas(16) Drone_tag g_drones[3];
alignas(16) obj_tag g_objs[3];
std::vector<std::string> g_log;

void fresh() {
    std::memset(&GameState, 0, sizeof(GameState));
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    LList_Init(Drone_List(), 0xd20, 0);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    g_log.clear();
}

Drone_tag* make_drone(int i, uint16_t state) {
    std::memset(&g_drones[i], 0, sizeof(Drone_tag));
    std::memset(&g_objs[i], 0, sizeof(obj_tag));
    g_objs[i].type = 2;
    g_objs[i].data = &g_drones[i];
    g_objs[i].state = state;
    g_drones[i].obj = &g_objs[i];
    LList_Add(Drone_List(), &g_drones[i].node);
    Drone_SM_InitObject(&g_objs[i]);
    return &g_drones[i];
}

void note(const char* who, const MsgObject* m) { g_log.push_back(std::string(who) + ":" + std::to_string(m->msg)); }

uint32_t state_global(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* m) {
    note("G", m);
    return 0;
}
// Estado 5: trata o tick (3); a mensagem 7 pede o estado 6.
uint32_t state_5(DCVars_tag* dcv, Drone_tag*, obj_tag*, MsgObject* m) {
    note("5", m);
    if (m->msg == 7) Drone_SM_SetState(dcv->sm, 6, 0);
    return m->msg == 3 ? 1 : 0;
}
uint32_t state_6(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* m) {
    note("6", m);
    return 1;
}

}  // namespace

TEST(drone_behaviour_properties_round_trip) {
    uint8_t descs[sizeof(bitDescs)];
    uint32_t masks[35];
    std::memcpy(descs, bitDescs, sizeof(descs));
    std::memcpy(masks, bitMasks, sizeof(masks));
    // Propriedade 0: palavra 1, 3 bits a partir do bit 4; propriedade 1: palavra 0, bit 31.
    bitDescs[0] = uint8_t(1 | (4 << 3));
    bitDescs[1] = 2;
    bitMasks[2] = 0x70;
    bitDescs[2] = uint8_t(0 | (31 << 3));
    bitDescs[3] = 3;
    bitMasks[3] = 0x80000000u;
    uint32_t words[3] = {0x1234, 0xffffffffu, 0};
    behaviour_util_setProperty(0, words, 5);
    CHECK(words[1] == 0xffffffdfu && behaviour_util_getProperty(0, words) == 5);
    behaviour_util_setProperty(1, words, 1);
    CHECK(words[0] == 0x80001234u && behaviour_util_getProperty(1, words) == 1);
    behaviour_util_setProperty(0, words, 0);
    CHECK(behaviour_util_getProperty(0, words) == 0 && words[1] == 0xffffff8fu && words[2] == 0);
    std::memcpy(bitDescs, descs, sizeof(descs));
    std::memcpy(bitMasks, masks, sizeof(masks));
}

TEST(drone_behaviour_struct_reads_words_and_stats) {
    // Tipo 2, ataque 1, uma palavra (0x20 bits), amigo 3, uma palavra de ataque, e as estatísticas
    // (marca 0x8001xxxx, 6 / 3 = 2 palavras por conjunto, campos de 8,3,8,8,5,8,8,1 bits).
    const uint32_t set0 = 10u | (5u << 8) | (200u << 11) | (7u << 19) | (31u << 27);
    const uint32_t set0b = 1u | (2u << 8) | (1u << 16);
    uint32_t data[] = {2, 1, 1 | (0x20u << 16), 0xdeadbeefu, 3, 1 | (0x10u << 16), 0x1234, 0x80010006u,
                       set0, set0b, 0, 0, 0, 0};
    uint32_t a[3] = {}, b[3] = {};
    _BehaviourStruct bs{};
    bs.props = a;
    bs.props2 = b;
    std::memset(drone_stats, 0, sizeof(drone_stats));
    CHECK(behaviour_util_get(&bs, data) == 1);
    CHECK(bs.type == 2 && bs.attack == 1 && bs.friend_type == 3 && bs.bits == 0x20 && bs.words2 == 1);
    CHECK(a[0] == 0xdeadbeefu && a[1] == 0 && b[0] == 0x1234);
    const uint32_t want[8] = {10, 5, 200, 7, 31, 1, 2, 1};
    CHECK(std::memcmp(drone_stats[2][0], want, sizeof(want)) == 0);
    CHECK(behaviour_util_getStats(2, 0) == drone_stats[2][0]);
    // Inválida (tipo 0x11): zera e devolve 0.
    data[0] = 0x11;
    a[0] = 7;
    CHECK(behaviour_util_get(&bs, data) == 0 && bs.type == 0 && bs.words == 3 && bs.bits == 0x5b && a[0] == 0);
}

TEST(drone_anim_state_call_lookup) {
    // Estado de animação 3: chamada 4 com 2 variantes (DAnim 100), qualquer outra -> DAnim 200.
    static const uint8_t calls[] = {4, 2, 100, 0, 7, 1, 0, 1, 200, 0, 8, 0, 0xff};
    static const uint8_t no_default[] = {4, 1, 50, 0, 0, 0, 0xff};
    const DroneAnimState saved3 = DroneAnimStates[3], saved4 = DroneAnimStates[4];
    DroneAnimStates[3].calls = calls;
    DroneAnimStates[4].calls = no_default;
    uint16_t out4 = 0;
    int8_t out5 = 0;
    CHECK(DroneAnim_GetDAnimForAnimState(4, 3, 1, &out4, &out5) == 101 && out4 == 7 && out5 == 1);
    CHECK(DroneAnim_GetDAnimForAnimState(4, 3, 2, nullptr, nullptr) == 100);  // variante além das que há
    CHECK(DroneAnim_GetDAnimForAnimState(9, 3, 0, &out4, &out5) == 200 && out4 == 8 && out5 == 0);
    CHECK(DroneAnim_GetDAnimForAnimState(9, 4, 0, nullptr, nullptr) == -1);
    CHECK(DroneAnim_GetDAnimForAnimState(4, 4, 0, nullptr, nullptr) == 50);
    DroneAnimStates[3] = saved3;
    DroneAnimStates[4] = saved4;
}

TEST(drone_state_machine_enter_leave_and_global) {
    fresh();
    NDrone2_StateFuncs[0] = state_global;
    NDrone2_StateFuncs[5] = state_5;
    NDrone2_StateFuncs[6] = state_6;
    Drone_tag* d = make_drone(0, 5);
    CHECK(d->sm.id == 1 && d->sm.state == 5 && d->sm.handler == NDrone2_ProcessStateMachine);
    // "Entrar" (1) chega ao estado 5, que não trata: vai também ao estado 0.
    CHECK((g_log == std::vector<std::string>{"5:1", "G:1"}));
    g_log.clear();
    DCVars_tag dcv;
    CHECK(Drone_DCVfromOBJ(d->obj, &dcv) == 1 && dcv.sm == &d->sm);
    Drone_SM_SendMsgSelf(3, 0, 0, 0, &dcv);  // tratada: o estado 0 não vê
    CHECK((g_log == std::vector<std::string>{"5:3"}));
    g_log.clear();
    Drone_SM_SendMsgSelf(7, 0, 0, 0, &dcv);  // pede o estado 6: sai do 5, entra no 6
    CHECK((g_log == std::vector<std::string>{"5:7", "G:7", "5:2", "6:1"}));
    CHECK(d->sm.state == 6 && d->sm.prev_state == 5 && d->obj->state == 6 && d->sm.change == 0);
    // Mensagem que exige outro estado não é entregue.
    g_log.clear();
    Drone_SM_SendMsgSelf(3, 0, 0, 5, &dcv);
    CHECK(g_log.empty());
    // Um segundo personagem e uma mensagem a todos (destino 0).
    Drone_tag* e = make_drone(1, 6);
    g_log.clear();
    Drone_SM_SendMsg(9, 0, 0, 0);
    CHECK(g_log.size() == 2 && e->sm.id == 2);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
}

TEST(drone_state_machine_delayed_messages_in_order) {
    fresh();
    NDrone2_StateFuncs[6] = state_6;
    Drone_tag* d = make_drone(0, 6);
    DCVars_tag dcv;
    Drone_DCVfromOBJ(d->obj, &dcv);
    g_log.clear();
    GameState.frame = 100;
    Drone_SM_SendMsgSelf(20, 0, 20, 0, &dcv);
    Drone_SM_SendMsgSelf(10, 0, 10, 0, &dcv);
    Drone_SM_SendMsgSelf(15, 0, 15, 0, &dcv);
    Drone_SM_SendMsgSelf(11, 0, 10, 0, &dcv);  // mesma hora que a 10: depois dela
    CHECK(g_log.empty());
    GameState.frame = 109;
    Drone_SM_SendDelayedMsgs();
    CHECK(g_log.empty());
    GameState.frame = 115;
    Drone_SM_SendDelayedMsgs();
    CHECK((g_log == std::vector<std::string>{"6:10", "6:11", "6:15"}));
    GameState.frame = 200;
    Drone_SM_SendDelayedMsgs();
    CHECK(g_log.size() == 4 && g_log.back() == "6:20");
    // As 0x3fe entradas voltaram todas para a lista livre.
    int free_items = 0;
    for (DroneSMItem* it = Drone_SM_LLDelayed.free->next; it != nullptr; it = it->next) ++free_items;
    CHECK(free_items == 0x3fe && Drone_SM_LLDelayed.pending->next == nullptr);
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
}

TEST(drone_dtype_from_behaviour) {
    // Sem propriedades (todas 0): os tipos dependem só do comportamento, do ataque e do alerta.
    alignas(16) static Drone_tag d;
    std::memset(&d, 0, sizeof(d));
    d.behaviour = d.behaviour_a;
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.level_file = 0x7000001;
    CHECK(NDrone2_GetDTYPENEW(&d, 0xf, 0) == 0xd);
    CHECK(NDrone2_GetDTYPENEW(&d, 0x10, 3) == 0x1e);
    CHECK(NDrone2_GetDTYPENEW(&d, 0, 4) == 4 && NDrone2_GetDTYPENEW(&d, 0, 5) == 0x15);
    CHECK(NDrone2_GetDTYPENEW(&d, 0, 2) == 2);
    d.alert = 1.0f;
    CHECK(NDrone2_GetDTYPENEW(&d, 0, 2) == 0x19 && NDrone2_GetDTYPENEW(&d, 10, 0) == 10);
    d.alert = 0.0f;
    CHECK(NDrone2_GetDTYPENEW(&d, 10, 0) == 9);
    d.unknown_15 = 1;
    CHECK(NDrone2_GetDTYPENEW(&d, 10, 0) == 0x12);
    d.klass = 0xc;
    CHECK(NDrone2_GetDTYPENEW(&d, 0xc, 0) == 0x11);
    // Na fase 07000007, comportamentos 0-9 sem ataque viram 0x13.
    GameState.level_file = 0x7000007;
    CHECK(NDrone2_GetDTYPENEW(&d, 3, 0) == 0x13);
}

TEST(drone_anim_call_requests) {
    // Estado de animação 5 com DAnim base 10: só a subclasse 0 tem script; o 6 (DAnim 11) nenhuma.
    const DroneAnimState saved5 = DroneAnimStates[5], saved6 = DroneAnimStates[6], saved24 = DroneAnimStates[0x24];
    DroneAnimTableEntry saved10[kDroneAnimSubclasses], saved11[kDroneAnimSubclasses];
    std::memcpy(saved10, Drone_AnimTables[10], sizeof(saved10));
    std::memcpy(saved11, Drone_AnimTables[11], sizeof(saved11));
    std::memset(Drone_AnimTables[10], 0, sizeof(saved10));
    std::memset(Drone_AnimTables[11], 0, sizeof(saved11));
    DroneAnimStates[5].danim = 10;
    DroneAnimStates[5].kind = 7;
    DroneAnimStates[6].danim = 11;
    Drone_AnimTables[10][0].script = 0x123;
    alignas(16) static Drone_tag d;
    alignas(16) static obj_tag o;
    std::memset(&d, 0, sizeof(d));
    std::memset(&o, 0, sizeof(o));
    d.obj = &o;
    d.subclass = 4;
    DCVars_tag dcv{&o, &d, nullptr, &d.sm};
    CHECK(DroneAnim_CanDoAnimState(&dcv, 0, 0) && DroneAnim_CanDoAnimState(&dcv, 5, 0));  // pela subclasse 0
    CHECK(!DroneAnim_CanDoAnimState(&dcv, 6, 0) && !DroneAnim_CanDoAnimState(&dcv, 0x77, 0));
    // Pedido guardado; repetir o mesmo pedido não o renova.
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.frame = 50;
    d.anim_state = 2;
    CHECK(DroneAnim_CallAnim(1.5f, 5, 1, 7, 0, 0, &dcv) == 1);
    CHECK(d.call_state == 5 && d.call_variant == 1 && d.call_new == 1 && d.call_time == 50 && d.end_state == 7);
    CHECK(d.call_from_state == 2 && d.call_state_kind == 7 && d.call_param == 1.5f);
    GameState.frame = 60;
    DroneAnim_CallAnim(1.5f, 5, 1, 7, 0, 0, &dcv);
    CHECK(d.call_time == 50);
    // Já no estado pedido e sem estado ou mensagem de fim: o pedido é apagado.
    d.anim_state = 5;
    DroneAnim_CallAnim(0.0f, 5, 0, 0, 0, 0, &dcv);
    CHECK(d.call_state == 0 && d.call_new == 0);
    DroneAnimStates[5] = saved5;
    DroneAnimStates[6] = saved6;
    DroneAnimStates[0x24] = saved24;
    std::memcpy(Drone_AnimTables[10], saved10, sizeof(saved10));
    std::memcpy(Drone_AnimTables[11], saved11, sizeof(saved11));
}

namespace {
uint32_t state_recorder(DCVars_tag*, Drone_tag*, obj_tag*, MsgObject* m) {
    note("R", m);
    return 1;
}
}  // namespace

TEST(drone_states_wait_switch_to_idle) {
    fresh();
    NDrone2_StateFuncs[0] = NDrone2_DSTATE_Global;
    NDrone2_StateFuncs[1] = NDrone2_DSTATE_WaitSwitch;
    NDrone2_StateFuncs[4] = state_recorder;
    NDrone2_StateFuncs[6] = NDrone2_DSTATE_InitPatrol;
    std::memset(switch_channels, 0, sizeof(switch_channels));
    Drone_tag* d = &g_drones[0];
    std::memset(d, 0, sizeof(Drone_tag));
    std::memset(&g_objs[0], 0, sizeof(obj_tag));
    g_objs[0].type = 2;
    g_objs[0].data = d;
    d->obj = &g_objs[0];
    d->behaviour = d->behaviour_a;  // sem propriedades: sem patrulha
    d->channel_a = 0x30;
    d->initial_state = 6;
    d->flags = 0x801a0;
    LList_Add(Drone_List(), &d->node);
    Drone_SM_InitObject(&g_objs[0]);
    // O estado 0 manda esperar o canal; esperando, o personagem fica desligado (0x100) e escondido.
    CHECK(d->sm.state == 1 && (d->flags & 0x100) == 0 && (g_objs[0].flags & 0x10) != 0);
    DCVars_tag dcv;
    Drone_DCVfromOBJ(d->obj, &dcv);
    Drone_SM_SendMsgSelf(3, 0, 0, 0, &dcv);
    CHECK(d->sm.state == 1);
    // Canal ligado: liga de novo e vai ao estado inicial (6, patrulha), que sem patrulha leva ao 4.
    switch_channels[0x30] = 1;
    g_log.clear();
    Drone_SM_SendMsgSelf(3, 0, 0, 0, &dcv);
    CHECK(d->sm.state == 4 && (d->flags & 0x100) != 0 && (g_objs[0].flags & 0x10) == 0);
    CHECK(!g_log.empty() && g_log.back() == "R:1");
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    std::memset(switch_channels, 0, sizeof(switch_channels));
}
