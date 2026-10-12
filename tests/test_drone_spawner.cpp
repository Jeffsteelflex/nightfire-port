// Testes dos geradores de personagens (engine/drone_spawner.cpp) sem dados do jogo: uma sala sem
// geometria, personagens criados por Drone_Create (sem pele) num grupo e um gerador desse grupo.
// Conferem propriedades: os modelos saem do mapa, um personagem por tick, nunca mais vivos que o
// limite, cada modelo uma vez por leva, o total de levas, a distância do jogador, os canais de começo,
// parada, fim e remoção, e o que acontece quando um gerado morre ou é apagado.

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#include "engine/ainetwork.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kSkinParam = 0x6000123;  // parâmetro 1 (script de entrada): marca os modelos

void fresh() {
    psiMemHeapSize = 2u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    hashtable_debug_power_on();
    hashtable_initialise();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    std::memset(glb_viewer, 0, sizeof(glb_viewer));
    std::memset(glb_players, 0, sizeof(glb_players));
    pending_reset();
    GS_SetRefreshRate(60, 60);
    std::memset(&GameState, 0, sizeof(GameState));
    GameState.difficulty = 2;
    GameState.frame = 100;
    GameState.level_file = 0x7000001;
    std::memset(MPSettings, 0, sizeof(MPSettings));
    std::memset(switch_channels, 0, sizeof(switch_channels));
    std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
    std::memset(&AIBoxList, 0, sizeof(AIBoxList));
    AINetwork.path_count = 0;
    AINetwork.max_nodes = 0;
    Drone_bDisableSystem = 0;
    Drone_LevelReset();
    Drone_PreLoad_Init();
    std::memset(NDrone2_StateFuncs, 0, sizeof(NDrone2_StateFuncs));
    g_drone_spawner_stats = {};
    // A sala onde ficam os modelos (sem geometria: nada de chão nem paredes), de -50 a 50.
    cel_tag* cel = build_alloc_cel(glb_world->first);
    glb_world->first = cel;
    cel->type = 0x4c023 | 0x8000000;
    cel->box_min = {-50, -10, -50, 0};
    cel->box_max = {50, 50, 50, 0};
}

// Personagem da fase (posicionamento 0x0f) no grupo `group`, como parsemap_create_dynamic_objects.
obj_tag* place_drone(uint32_t group, _VECTOR pos) {
    level_tag level{};
    level.params[1] = kSkinParam;
    level.params[2] = 1;  // dificuldade mínima
    level.params[4] = group;
    level.params[8] = 0x65;
    _VECTOR rot{0, 0, 0, 0};
    return Drone_Create(&pos, &rot, &level);
}

// Gerador (posicionamento 0xf1): tipo, grupo, limite, levas, canais de começo/parada/fim/remoção e
// distância.
obj_tag* place_spawner(uint32_t kind, uint32_t group, uint32_t max_alive, uint32_t waves, uint32_t start = 0,
                       uint32_t stop = 0, uint32_t done = 0, uint32_t kill = 0, uint32_t distance = 0) {
    level_tag level{};
    level.params[0] = kind;
    level.params[1] = group;
    level.params[2] = max_alive;
    level.params[3] = waves;
    level.params[4] = start;
    level.params[5] = stop;
    level.params[6] = done;
    level.params[7] = kill;
    level.params[8] = distance;
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    const uint32_t before = g_drone_spawner_stats.created;
    DroneSpawner_Create(&pos, &rot, &level);
    if (g_drone_spawner_stats.created == before) return nullptr;
    return NPCGlobals.spawners[group];
}

DroneSpawner_tag* data_of(obj_tag* spawner) { return static_cast<DroneSpawner_tag*>(spawner->data); }

int drone_count() {
    int n = 0;
    for (const LLNODE_tag* node = Drone_List()->first; node != nullptr; node = node->next) ++n;
    return n;
}

// Os personagens que o gerador mantém nas vagas.
std::vector<obj_tag*> slots(obj_tag* spawner) {
    std::vector<obj_tag*> out;
    const DroneSpawner_tag* s = data_of(spawner);
    for (uint16_t i = 0; i < s->max_alive; ++i) {
        if (s->spawned[i] != nullptr) out.push_back(s->spawned[i]);
    }
    return out;
}

void kill(obj_tag* drone) { static_cast<Drone_tag*>(drone->data)->health = 0.0f; }

// Um tick do gerador; devolve quantos personagens nasceram nele.
uint32_t tick(obj_tag* spawner) {
    const uint32_t before = g_drone_spawner_stats.spawned;
    ++GameState.frame;
    DroneSpawner_Control(spawner);
    return g_drone_spawner_stats.spawned - before;
}

// Ticks até o gerador ter n gerados nas vagas (no máximo 100), conferindo um por tick: o sorteio de
// DroneSpawner_SpawnDrone pode cair além dos modelos livres e não gerar naquele tick.
void fill(obj_tag* spawner, size_t n) {
    for (int t = 0; t < 100 && slots(spawner).size() < n; ++t) CHECK(tick(spawner) <= 1);
    CHECK(slots(spawner).size() == n);
}

// Três modelos espalhados no grupo 4 e um gerador; NDrone2_SetupGroups (Drone_PostLoad_Init) o inicia.
obj_tag* setup(uint32_t kind, uint32_t max_alive, uint32_t waves, uint32_t start = 0, uint32_t stop = 0,
               uint32_t done = 0, uint32_t kill_channel = 0, uint32_t distance = 0) {
    place_drone(4, {-10, 0, 0, 0});
    place_drone(4, {0, 0, 10, 0});
    place_drone(4, {10, 0, 0, 0});
    obj_tag* sp = place_spawner(kind, 4, max_alive, waves, start, stop, done, kill_channel, distance);
    NDrone2_SetupGroups();
    return sp;
}

}  // namespace

TEST(drone_spawner_create_reads_the_placement) {
    fresh();
    obj_tag* sp = place_spawner(3, 7, 2, 0, 0x11, 0x12, 0x13, 0x14, 0);
    CHECK(sp != nullptr && sp->type == 0x30 && sp->glist == nullptr && sp->cel == glb_world->first);
    const DroneSpawner_tag* s = data_of(sp);
    CHECK(s->kind == 3 && s->group == 7 && s->max_alive == 2);
    CHECK(s->waves == 32000 && s->distance == 10.0f);  // 0 levas: "sem fim"; distância 0: 10 m
    CHECK(s->channel_start == 0x11 && s->channel_stop == 0x12 && s->channel_done == 0x13 && s->channel_kill == 0x14);
    CHECK(s->ready == 0 && s->spawned == nullptr && s->entries == nullptr);
    // Tipo menor que 2 não cria gerador.
    CHECK(place_spawner(1, 8, 2, 1) == nullptr && NPCGlobals.spawners[8] == nullptr);
    CHECK(place_spawner(2, 9, 2, 1, 0, 0, 0, 0, 25) != nullptr && data_of(NPCGlobals.spawners[9])->distance == 25.0f);
}

TEST(drone_spawner_init_turns_the_group_into_templates) {
    fresh();
    place_drone(2, {30, 0, 30, 0});  // outro grupo, sem gerador: fica
    const int objects_before = DynamicObjCount;
    obj_tag* sp = setup(2, 0, 1);
    // Os três do grupo 4 saíram do mapa (e da lista dos personagens); o do grupo 2 ficou.
    CHECK(drone_count() == 1 && g_drone_spawner_stats.templates == 3);
    CHECK(DynamicObjCount == objects_before + 1);  // + 3 modelos + o gerador - 3 modelos
    const DroneSpawner_tag* s = data_of(sp);
    CHECK(s->ready == 1 && s->running == 1 && s->anywhere == 1);  // sem canal de começo: já roda
    CHECK(s->entry_count == 3 && s->entry_count2 == 3 && s->max_alive == 3);  // limite 0: o grupo
    CHECK(NPCGlobals.group_counts[4] == 0xffff && NPCGlobals.group_counts[2] == 1);
    std::set<float> xs;
    for (uint16_t i = 0; i < s->entry_count; ++i) {
        CHECK(s->entries[i].spawned == 0 && s->entries[i].vars.params[1] == kSkinParam);
        CHECK(s->entries[i].vars.params[4] == 4);
        xs.insert(s->entries[i].vars.pos.x);
    }
    CHECK((xs == std::set<float>{-10.0f, 0.0f, 10.0f}));
    for (uint16_t i = 0; i < s->max_alive; ++i) CHECK(s->spawned[i] == nullptr);
    // Limite maior que o grupo também vira o tamanho do grupo; grupo sem ninguém: o gerador some.
    fresh();
    place_drone(5, {0, 0, 0, 0});
    obj_tag* big = place_spawner(2, 5, 9, 1);
    obj_tag* empty = place_spawner(2, 6, 1, 1);
    NDrone2_SetupGroups();
    CHECK(data_of(big)->max_alive == 1);
    CHECK(data_of(empty)->ready == 0 && NPCGlobals.spawners[6] == empty);  // SetupGroups não o chama
    DroneSpawner_Init(empty);
    CHECK((empty->life & 1) != 0 && NPCGlobals.spawners[6] == nullptr);
}

TEST(drone_spawner_spawns_one_per_tick_through_the_load_path) {
    fresh();
    obj_tag* sp = setup(2, 2, 1);
    CHECK(tick(sp) == 1);
    std::vector<obj_tag*> got = slots(sp);
    CHECK(got.size() == 1);
    obj_tag* o = got[0];
    const Drone_tag* d = static_cast<const Drone_tag*>(o->data);
    // Um personagem como os da fase: tipo 2, na lista, posto na sala por NDrone2_DefaultInit, com os
    // parâmetros do modelo e a máquina de estados registrada (Drone_SM_InitObject).
    CHECK(o->type == 2 && drone_count() == 1 && o->cel == glb_world->first);
    CHECK(d->obj == o && d->divars.obj == o && d->group_id == 4 && d->divars.params[1] == kSkinParam);
    CHECK(d->sm.id != 0 && d->sm.handler == NDrone2_ProcessStateMachine);
    CHECK(d->health > 0.0f && (d->flags & 0x100) != 0);
    CHECK(d->obj->pos.x == d->divars.pos.x && d->obj->pos.z == d->divars.pos.z);
    // Um por tick até o limite (2), cada um de um modelo diferente.
    fill(sp, 2);
    const DroneSpawner_tag* s = data_of(sp);
    uint32_t used = 0;
    for (uint16_t i = 0; i < s->entry_count; ++i) {
        CHECK(s->entries[i].spawned <= 1);
        used += s->entries[i].spawned;
    }
    CHECK(used == 2 && s->wave_spawned == 2);
}

TEST(drone_spawner_never_exceeds_the_limit_or_the_waves) {
    fresh();
    obj_tag* sp = setup(2, 2, 3);
    const DroneSpawner_tag* s = data_of(sp);
    uint32_t total = 0;
    for (int t = 0; t < 200 && (sp->life & 1) == 0; ++t) {
        const uint32_t born = tick(sp);
        CHECK(born <= 1);
        total += born;
        CHECK(slots(sp).size() <= 2);
        CHECK(s->alive <= s->max_alive);
        for (uint16_t i = 0; i < s->entry_count; ++i) CHECK(s->entries[i].spawned <= 1);
        // Mata quem nasceu há pouco: o tipo 2 repõe até acabarem as levas.
        if (t % 3 == 2) {
            for (obj_tag* o : slots(sp)) kill(o);
        }
    }
    // 3 levas de 2: 6 personagens, e o gerador some depois da última.
    CHECK(total == 6 && g_drone_spawner_stats.spawned == 6);
    CHECK((sp->life & 1) != 0 && s->ready == 0 && s->wave == 3 && g_drone_spawner_stats.waves == 3);
}

TEST(drone_spawner_replaces_the_dead) {
    fresh();
    obj_tag* sp = setup(2, 2, 32000);
    fill(sp, 2);
    std::vector<obj_tag*> first = slots(sp);
    CHECK(first.size() == 2);
    tick(sp);  // fecha a leva (gerou 2): nenhum nasce
    CHECK(tick(sp) == 0 && data_of(sp)->wave == 1);
    // Um morre: a vaga solta no tick seguinte e um novo nasce no lugar.
    kill(first[0]);
    CHECK(tick(sp) == 1);
    std::vector<obj_tag*> now = slots(sp);
    CHECK(now.size() == 2 && data_of(sp)->alive == 1);
    CHECK(std::find(now.begin(), now.end(), first[0]) == now.end());
    // Um morto que ainda não foi apagado continua na lista dos personagens (o estado da morte o apaga).
    CHECK(drone_count() == 3);
    // Apagar um gerado vivo (Drone_Delete) solta a vaga por DroneSpawner_DroneDelete.
    obj_tag* alive = first[1];
    control_delete_object(alive);
    CHECK(g_drone_spawner_stats.released == 1);
    now = slots(sp);
    CHECK(std::find(now.begin(), now.end(), alive) == now.end() && now.size() == 1);
    fill(sp, 2);
}

TEST(drone_spawner_waves_wait_for_the_whole_wave_to_die) {
    fresh();
    obj_tag* sp = setup(3, 2, 2, 0, 0, 0x40);
    fill(sp, 2);
    CHECK(tick(sp) == 0 && data_of(sp)->wave == 1);  // fim da primeira leva
    std::vector<obj_tag*> wave1 = slots(sp);
    CHECK(wave1.size() == 2);
    // Com um vivo, a próxima leva não começa.
    kill(wave1[0]);
    for (int t = 0; t < 5; ++t) CHECK(tick(sp) == 0);
    kill(wave1[1]);
    CHECK(tick(sp) == 1);  // a leva nova: todos os modelos livres, o sorteio sempre acerta
    fill(sp, 2);
    CHECK(tick(sp) == 0 && data_of(sp)->wave == 2);
    // Depois da última leva, o canal de fim só liga quando todos morreram, e o gerador some.
    CHECK(tick(sp) == 0 && (sp->life & 1) == 0 && switch_channels[0x40] == 0);
    for (obj_tag* o : slots(sp)) kill(o);
    tick(sp);
    CHECK(switch_channels[0x40] == 1 && switch_channels_time[0x40] == GameState.frame && (sp->life & 1) != 0);
}

TEST(drone_spawner_start_and_stop_channels) {
    fresh();
    obj_tag* sp = setup(2, 1, 5, 0x21, 0x22);
    DroneSpawner_tag* s = data_of(sp);
    CHECK(s->running == 0 && s->anywhere == 0);  // o canal de começo estava desligado na carga
    for (int t = 0; t < 5; ++t) CHECK(tick(sp) == 0);
    switch_channels[0x21] = 1;
    CHECK(tick(sp) == 1 && s->running == 1 && s->channel_start == 0);
    // Desligar o canal de começo depois não para o gerador; o de parada para e o tira do mapa.
    switch_channels[0x21] = 0;
    kill(slots(sp)[0]);
    tick(sp);  // fecha a leva
    CHECK(tick(sp) == 1);
    switch_channels[0x22] = 1;
    CHECK(tick(sp) == 0 && (sp->life & 1) != 0 && s->running == 0);
    // Já ligado na carga: a primeira leva nasce sem olhar a distância.
    fresh();
    switch_channels[0x21] = 1;
    obj_tag* on = setup(2, 1, 1, 0x21);
    CHECK(data_of(on)->anywhere == 1 && data_of(on)->running == 0);
    CHECK(tick(on) == 1);
}

TEST(drone_spawner_keeps_its_distance_from_the_player) {
    alignas(16) static obj_tag player;
    alignas(16) static BLData bloke;
    const auto place_player = [](_VECTOR pos) {
        std::memset(&player, 0, sizeof(player));
        std::memset(&bloke, 0, sizeof(bloke));
        player.pos = pos;
        player.data = &bloke;
        glb_players[0] = &player;
    };
    // O jogador fica no modelo de x = -10; com 6 m de distância mínima e o canal de começo ligado só
    // depois da carga, esse modelo nunca é escolhido (os outros estão a 14 e 20 m). Sem câmera, nada
    // está à vista (Drone_PositionVisible): só a distância conta.
    fresh();
    obj_tag* sp = setup(2, 3, 32000, 0x30, 0, 0, 0, 6);
    place_player({-10, 0, 0, 0});
    switch_channels[0x30] = 1;
    std::set<float> xs;
    for (int t = 0; t < 30; ++t) {
        tick(sp);
        for (obj_tag* o : slots(sp)) {
            xs.insert(static_cast<Drone_tag*>(o->data)->divars.pos.x);
            kill(o);
        }
    }
    CHECK((xs == std::set<float>{0.0f, 10.0f}));
    // Peculiaridade: o modelo perto nunca gera, a leva nunca chega a 3 e não recomeça; o gerador para.
    CHECK(data_of(sp)->wave == 0 && data_of(sp)->wave_spawned == 2);
    // Com todos os modelos a menos de 40 m, ninguém nasce.
    fresh();
    obj_tag* near = setup(2, 3, 32000, 0x30, 0, 0, 0, 40);
    place_player({0, 0, 0, 0});
    switch_channels[0x30] = 1;
    for (int t = 0; t < 30; ++t) CHECK(tick(near) == 0);
    glb_players[0] = nullptr;
}

TEST(drone_spawner_kill_channel_removes_the_spawned) {
    fresh();
    obj_tag* sp = setup(2, 2, 32000, 0, 0, 0, 0x50);
    fill(sp, 2);
    std::vector<obj_tag*> got = slots(sp);
    switch_channels[0x50] = 1;
    CHECK(tick(sp) == 0);
    for (obj_tag* o : got) CHECK((o->life & 1) != 0);  // marcados para remoção
    CHECK(slots(sp).empty() && (sp->life & 1) != 0);
    // Em 07000016 os que estão à vista (desenhados há pouco) ficam, e o gerador espera por eles.
    fresh();
    GameState.level_file = 0x7000016;
    obj_tag* sp16 = setup(2, 2, 32000, 0, 0, 0, 0x50);
    fill(sp16, 2);
    got = slots(sp16);
    got[0]->unknown_fc = 2;
    got[1]->unknown_fc = 0;
    switch_channels[0x50] = 1;
    tick(sp16);
    CHECK((got[0]->life & 1) == 0 && (got[1]->life & 1) != 0);
    CHECK(slots(sp16).size() == 1 && (sp16->life & 1) == 0);
    got[0]->unknown_fc = 0;
    tick(sp16);
    CHECK((got[0]->life & 1) != 0 && slots(sp16).empty() && (sp16->life & 1) != 0);
}

TEST(drone_spawner_delete_releases_its_lists) {
    fresh();
    obj_tag* sp = setup(2, 1, 1);
    CHECK(NPCGlobals.spawners[4] == sp && data_of(sp)->spawned != nullptr);
    tick(sp);
    obj_tag* drone = slots(sp)[0];
    control_delete_object(sp);
    CHECK(NPCGlobals.spawners[4] == nullptr);
    // O gerado continua no mapa; apagá-lo depois não toca o gerador (que já não existe).
    CHECK(drone_count() == 1);
    control_delete_object(drone);
    CHECK(drone_count() == 0 && g_drone_spawner_stats.released == 0);
}
