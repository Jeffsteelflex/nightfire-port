// Testes do script player (src/engine/script.cpp, script_player.cpp, script_support.cpp e
// drone_script.cpp) sem dados do jogo: scripts montados à mão no formato do disco (docs/formats.md,
// "Scripts de cena").

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/lights.h"
#include "engine/npc.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

void fresh() {
    psiMemHeapSize = 1u << 21;
    PtrHeap = nullptr;
    Mem_Init();
    hashtable_debug_power_on();
    hashtable_initialise();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    std::memset(glb_viewer, 0, sizeof(glb_viewer));
    std::memset(glb_players, 0, sizeof(glb_players));
    script_debug_power_on();
    Script_Init();
    Sensor_Init();
    std::memset(switch_channels, 0, sizeof(switch_channels));
    std::memset(switch_channels_time, 0, sizeof(switch_channels_time));
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(&NPCGlobals, 0, sizeof(NPCGlobals));
    lights_debug_power_on();
    Light_Init();
    GS_SetRefreshRate(60, 60);
    pending_reset();
}

struct Key {
    float pos[3];
    float quat[4];  // x, y, z, w
    float scale;
    float alpha;
    float frame;
};

// Monta um script no formato do disco (cabeçalho, streams com tamanho, chaves alinhadas a 16).
struct ScriptBuilder {
    struct Stream {
        uint16_t first = 0, last = 0;
        std::vector<uint8_t> cmds;
    };
    uint16_t camera = 0, loop_start = 0, end = 0;
    std::vector<Stream> streams;
    std::vector<Key> keys;

    Stream& add(uint16_t first, uint16_t last) {
        streams.push_back({first, last, {}});
        return streams.back();
    }
    static void u8(Stream& s, uint8_t v) { s.cmds.push_back(v); }
    static void u16(Stream& s, uint16_t v) {
        s.cmds.push_back(uint8_t(v));
        s.cmds.push_back(uint8_t(v >> 8));
    }
    static void u32(Stream& s, uint32_t v) {
        for (int i = 0; i < 4; ++i) s.cmds.push_back(uint8_t(v >> (8 * i)));
    }
    // Comandos (o byte de fecho é o próprio código; 5 e 6 trocados).
    static void wait(Stream& s, uint16_t frame) {
        u8(s, 5);
        u16(s, frame);
        u8(s, 6);
        u8(s, 6);
        u16(s, frame);
        u8(s, 5);
    }
    static void event(Stream& s, uint8_t ev, std::initializer_list<uint32_t> args) {
        u8(s, 0x12);
        u8(s, uint8_t(args.size()));
        u8(s, ev);
        for (uint32_t a : args) u32(s, a);
        u8(s, 0x12);
    }
    static void entity(Stream& s, uint32_t hash, uint8_t type, int parent = -1) {
        u8(s, 7);
        u32(s, hash);
        u8(s, parent < 0 ? 0 : 1);
        u8(s, parent < 0 ? 0xff : uint8_t(parent));
        u8(s, 0);
        u8(s, type);
        u8(s, 0);
        u8(s, 7);
    }
    static void camera_cmd(Stream& s, uint16_t first, uint16_t last_plus_one) {
        u8(s, 0xd);
        u8(s, 0);
        u16(s, first);
        u16(s, last_plus_one);
        u8(s, 0xd);
    }
    static void stream_end(Stream& s, uint16_t frame) {
        u8(s, 4);
        u16(s, frame);
        u8(s, 4);
    }

    // Buffer alinhado a 16 (como os itens do pacote, alinhados a 32).
    uint8_t* build(std::vector<uint8_t>& storage) const {
        std::vector<uint8_t> b;
        const auto w16 = [&](uint16_t v) {
            b.push_back(uint8_t(v));
            b.push_back(uint8_t(v >> 8));
        };
        const auto w32 = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
        };
        w16(0x1c);
        w16(camera);
        w16(uint16_t(streams.size()));
        w16(loop_start);
        w16(end);
        for (const Stream& s : streams) {
            w16(s.first);
            w16(s.last);
            w32(uint32_t(s.cmds.size() + 4));
            b.insert(b.end(), s.cmds.begin(), s.cmds.end());
        }
        w32(uint32_t(keys.size()));
        const size_t after_pad = b.size() + 4;
        const uint32_t pad = uint32_t((16 - after_pad % 16) % 16);
        w32(pad);
        for (uint32_t i = 0; i < pad; ++i) b.push_back(0);
        for (const Key& k : keys) {
            float f[12] = {k.pos[0], k.pos[1], k.pos[2], 1.0f, k.quat[0], k.quat[1], k.quat[2], k.quat[3],
                           k.scale, k.alpha, 1.0f, k.frame};
            const auto* p = reinterpret_cast<const uint8_t*>(f);
            b.insert(b.end(), p, p + sizeof(f));
        }
        storage.assign(b.size() + 32, 0);
        auto addr = reinterpret_cast<uintptr_t>(storage.data());
        uint8_t* aligned = storage.data() + ((32 - addr % 32) % 32);
        std::memcpy(aligned, b.data(), b.size());
        return aligned;
    }
};

Key key(float x, float y, float z, float frame, float scale = 1.0f, float alpha = 1.0f) {
    return Key{{x, y, z}, {0.0f, 0.0f, 0.0f, 1.0f}, scale, alpha, frame};
}

_VECTOR* zero() { return const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR); }

SCRIPTINFO* load(uint32_t hash, const ScriptBuilder& sb, std::vector<uint8_t>& storage, void* owner = nullptr) {
    uint8_t* data = sb.build(storage);
    CHECK(Script_Load(hash, zero(), zero(), data, nullptr, nullptr, nullptr) == nullptr);
    return Script_Load(hash, zero(), zero(), nullptr, owner, nullptr, nullptr);
}

}  // namespace

TEST(script_spline_weights_are_catmull_rom) {
    KEYED_POSROT keys[4]{};
    for (int i = 0; i < 4; ++i) keys[i].frame = float(i * 10);
    int32_t k[4];
    float w[4];
    // No meio de um trecho: os quatro pesos somam 1 e são simétricos em t = 0,5.
    GetSplineWeights(k, w, 15.0f, keys, 3);
    CHECK(k[0] == 0 && k[1] == 1 && k[2] == 2 && k[3] == 3);
    CHECK_NEAR(w[0] + w[1] + w[2] + w[3], 1.0f, 1e-6f);
    CHECK_NEAR(w[1], w[2], 1e-6f);
    CHECK_NEAR(w[0], w[3], 1e-6f);
    // Num quadro-chave exato, só ele pesa (a fração é 0).
    GetSplineWeights(k, w, 10.0f, keys, 3);
    CHECK(k[1] == 1 && w[1] == 1.0f && w[0] == 0.0f && w[2] == 0.0f && w[3] == 0.0f);
    // A fração vem do quadro inteiro (o tempo é truncado): 15,9 pesa como 15.
    float w15[4], w2[4];
    GetSplineWeights(k, w15, 15.0f, keys, 3);
    GetSplineWeights(k, w2, 15.9f, keys, 3);
    CHECK(std::memcmp(w15, w2, sizeof(w2)) == 0);
    // Depois do último quadro: tudo no último, peso 1.
    GetSplineWeights(k, w, 99.0f, keys, 3);
    CHECK(k[0] == 3 && k[1] == 3 && k[2] == 3 && k[3] == 3 && w[1] == 1.0f && w[0] == 0.0f);
}

TEST(script_load_registers_prototype_and_instance) {
    fresh();
    ScriptBuilder sb;
    sb.camera = 1;
    sb.loop_start = 2;
    sb.end = 9;
    ScriptBuilder::Stream& a = sb.add(0, 1);
    ScriptBuilder::stream_end(a, 9);
    ScriptBuilder::Stream& b = sb.add(2, 2);
    ScriptBuilder::wait(b, 3);
    ScriptBuilder::stream_end(b, 9);
    sb.keys = {key(0, 0, 0, 0), key(1, 0, 0, 5), key(2, 0, 0, 0)};
    std::vector<uint8_t> storage;
    uint8_t* data = sb.build(storage);
    CHECK(Script_Load(0x06000123, zero(), zero(), data, nullptr, nullptr, nullptr) == nullptr);
    const auto* def = static_cast<SCRIPTDEF_tag*>(hashtable_getitem(0x06000123));
    CHECK(def != nullptr && def->stream_count == 2 && def->key_count == 3 && def->end == 9 && def->loop_start == 2);
    CHECK(def->streams[1].first_key == 2 && def->streams[1].commands[0] == 5);
    CHECK((reinterpret_cast<uintptr_t>(def->keys) & 15) == 0 && def->keys[1].frame == 5.0f);
    // Instância no lugar pedido, com a câmera e os streams no começo.
    _VECTOR pos{10.0f, 20.0f, 30.0f, 0.0f};
    int owner = 0;
    SCRIPTINFO* si = Script_Load(0x06000123, &pos, zero(), nullptr, &owner, nullptr, nullptr);
    CHECK(si != nullptr && si->stream_count == 2 && si->owner == &owner && si->hash == 0x06000123);
    CHECK((si->flags & 0x40) != 0 && si->scale == 1.0f && si->play == 0);
    CHECK(si->base.m[3][0] == 10.0f && si->base.m[3][1] == 20.0f && si->base.m[3][2] == 30.0f);
    CHECK(si->streams[1].cursor == def->streams[1].commands && si->streams[1].parent == -1);
    // Outro formato: marcado como "não é script" e nunca instanciado.
    data[0] = 0x1b;
    CHECK(Script_Load(0x06000124, zero(), zero(), data, nullptr, nullptr, nullptr) == nullptr);
    CHECK(hashtable_getitem(0x06000124) == reinterpret_cast<void*>(uintptr_t(0xffffffffu)));
    CHECK(Script_Load(0x06000124, zero(), zero(), nullptr, nullptr, nullptr, nullptr) == nullptr);
    // Hash que não é de script.
    CHECK(Script_Load(0x02000123, zero(), zero(), data, nullptr, nullptr, nullptr) == nullptr);
}

TEST(script_waits_sets_channel_and_ends) {
    fresh();
    ScriptBuilder sb;
    sb.end = 5;
    ScriptBuilder::Stream& s = sb.add(0, 0);
    ScriptBuilder::wait(s, 3);
    ScriptBuilder::event(s, 13, {0x20, 1});
    ScriptBuilder::stream_end(s, 5);
    sb.keys = {key(0, 0, 0, 0)};
    std::vector<uint8_t> storage;
    SCRIPTINFO* si = load(0x06000200, sb, storage);
    CHECK(si != nullptr);
    Script_Play(si, 0);
    GameState.frame = 42;
    for (int i = 0; i < 3; ++i) Script_Update(si);
    CHECK(switch_channels[0x20] == 0);  // quadros 0, 1 e 2: ainda esperando
    Script_Update(si);
    CHECK(switch_channels[0x20] == 1 && switch_channels_time[0x20] == 42);  // quadro 3
    Script_Update(si);
    CHECK(Script_IsPlaying(si) != 0);
    Script_Update(si);  // quadro 5: fim do stream e do script
    CHECK(Script_IsPlaying(si) == 0 && (si->play & 2) != 0);
    CHECK(si->time == 6.0f);
    Script_Free(si);
}

TEST(script_loop_returns_to_loop_start) {
    fresh();
    ScriptBuilder sb;
    sb.end = 4;
    sb.loop_start = 1;
    ScriptBuilder::Stream& s = sb.add(0, 0);
    ScriptBuilder::wait(s, 2);
    ScriptBuilder::event(s, 13, {0x21, 1});
    ScriptBuilder::stream_end(s, 4);
    sb.keys = {key(0, 0, 0, 0)};
    std::vector<uint8_t> storage;
    SCRIPTINFO* si = load(0x06000201, sb, storage);
    Script_Play(si, 1);
    for (int i = 0; i < 5; ++i) Script_Update(si);  // quadros 0 a 4: no 4, volta para o quadro 1
    CHECK(Script_IsPlaying(si) != 0);
    CHECK(si->time == 2.0f && si->streams[0].time == 1.0f);
    switch_channels[0x21] = 0;
    Script_Update(si);  // quadro 1: esperando o 2
    CHECK(switch_channels[0x21] == 0);
    Script_Update(si);  // quadro 2 de novo: o evento roda outra vez
    CHECK(switch_channels[0x21] == 1);
}

TEST(script_entity_follows_keys_and_parent) {
    fresh();
    ScriptBuilder sb;
    sb.end = 20;
    ScriptBuilder::Stream& parent = sb.add(0, 1);
    ScriptBuilder::entity(parent, 0, 0x31);
    ScriptBuilder::wait(parent, 20);
    ScriptBuilder::stream_end(parent, 20);
    ScriptBuilder::Stream& child = sb.add(2, 2);
    ScriptBuilder::entity(child, 0, 0x31, 0);
    ScriptBuilder::wait(child, 20);
    ScriptBuilder::stream_end(child, 20);
    // O pai anda de x = 0 a x = 10 entre os quadros 0 e 10, com alfa de 1 a 0; o filho fica 1 m acima.
    sb.keys = {key(0, 0, 0, 0, 1.0f, 1.0f), key(10, 0, 0, 10, 1.0f, 0.0f), key(0, 1, 0, 0)};
    std::vector<uint8_t> storage;
    int owner = 0;
    SCRIPTINFO* si = load(0x06000202, sb, storage, &owner);
    _VECTOR at{100.0f, 0.0f, 0.0f, 0.0f};
    MATRIX m;
    View_RotTransMatrix(zero(), &at, &m);
    Script_SetPosRot(si, &m);  // o script fica em x = 100
    Script_Play(si, 0);
    for (int i = 0; i < 5; ++i) Script_Update(si);
    obj_tag* p = Script_GetObj(si, 0);
    obj_tag* c = Script_GetObj(si, 1);
    CHECK(p != nullptr && c != nullptr && p != c);
    Script_Update(si);  // quadro 5
    CHECK_NEAR(p->matrix.m[3][0], 105.0f, 1e-4f);
    CHECK(p->script == &owner && (p->mode & 4) != 0);
    CHECK(p->unknown_103[3] == 128);  // alfa 0,5 -> 127,5 + 0,5
    CHECK_NEAR(c->matrix.m[3][0], 105.0f, 1e-4f);
    CHECK_NEAR(c->matrix.m[3][1], 1.0f, 1e-4f);
    // Script_RemoveObj desfaz o stream da entidade (o objeto vai ser apagado por quem chamou).
    Script_RemoveObj(c, si);
    CHECK(Script_GetObj(si, 1) == nullptr && c->script == nullptr && (c->life & 1) != 0);
}

TEST(script_pause_and_unpause_through_player) {
    fresh();
    ScriptBuilder sb;
    sb.end = 6;
    ScriptBuilder::Stream& s = sb.add(0, 0);
    ScriptBuilder::event(s, 3, {0});  // pausa até SP_UnPause
    ScriptBuilder::wait(s, 2);
    ScriptBuilder::event(s, 13, {0x22, 1});
    ScriptBuilder::stream_end(s, 6);
    sb.keys = {key(0, 0, 0, 0)};
    std::vector<uint8_t> storage;
    uint8_t* data = sb.build(storage);
    Script_Load(0x06000203, zero(), zero(), data, nullptr, nullptr, nullptr);
    // Modo 1: toca já (estado 2), gatilho 4 (usar).
    obj_tag* obj = SP_Create(zero(), zero(), 0x06000203, 0, 1, 1, nullptr, nullptr, nullptr, 4);
    CHECK(obj != nullptr && obj->type == 0x1b);
    auto* sp = static_cast<SCRIPTPLAYER_tag*>(obj->data);
    CHECK(sp->state == 7 && sp->scripts[0] == nullptr);  // modo 1: carrega no primeiro tick
    SP_Update(obj);
    CHECK(sp->state == 2 && sp->scripts[0] != nullptr);
    SCRIPTINFO* si = sp->scripts[0];
    CHECK(si->wait == -1 && SP_GetState(obj) == 0);  // parado na pausa
    const float t = si->time;
    for (int i = 0; i < 10; ++i) SP_Update(obj);
    CHECK(si->time == t && switch_channels[0x22] == 0);
    SP_UnPause(obj);
    CHECK(SP_GetState(obj) == 1);
    for (int i = 0; i < 4; ++i) SP_Update(obj);
    CHECK(switch_channels[0x22] == 1);
    for (int i = 0; i < 6; ++i) SP_Update(obj);
    // Acabou e o player é "para no fim": estado 5, o objeto pede para ser apagado.
    CHECK(sp->state == 5);
    SP_Update(obj);
    CHECK((obj->life & 1) != 0);
}

TEST(script_switch_flips_channel_and_animates) {
    fresh();
    ScriptBuilder sb;
    sb.end = 8;
    ScriptBuilder::Stream& s = sb.add(0, 0);
    ScriptBuilder::event(s, 3, {0});
    ScriptBuilder::wait(s, 2);
    ScriptBuilder::event(s, 3, {0});
    ScriptBuilder::wait(s, 4);
    ScriptBuilder::stream_end(s, 8);
    sb.keys = {key(0, 0, 0, 0)};
    std::vector<uint8_t> storage;
    uint8_t* data = sb.build(storage);
    Script_Load(0x06000204, zero(), zero(), data, nullptr, nullptr, nullptr);
    level_tag level{};
    level.params[0] = 0x30;        // canal
    level.params[1] = 0x06000204;  // script
    celglist_tag glist{};
    _VECTOR pos{5.0f, 0.0f, 0.0f, 0.0f};
    obj_tag* sw = Switch_Create(&pos, zero(), &level, &glist);
    CHECK(sw != nullptr && sw->type == 0x39 && sw->state == 0);
    CHECK(Switch_GetSwitchChannel(sw) == 0x30 && Switch_GetNearest(&pos) == sw);
    auto* data_sw = static_cast<SWITCH_tag*>(sw->data);
    obj_tag* player = data_sw->player;
    CHECK(player != nullptr && SP_GetState(player) == 0);  // a alavanca está parada na primeira pausa
    obj_tag by{};
    by.type = 3;
    Switch_Activate(sw, &by);
    CHECK(switch_channels[0x30] == 1);
    // O switch acompanha o canal e solta a pausa: a alavanca anda até a próxima pausa (quadro 2).
    Switch_Update(sw);
    CHECK(sw->state == 1 && data_sw->uses == 1 && SP_GetState(player) == 1);
    for (int i = 0; i < 5; ++i) SP_Update(player);
    CHECK(SP_GetState(player) == 0);
    // A pausa veio no quadro 2; o tick em que ela chegou ainda anda o tempo.
    CHECK(static_cast<SCRIPTPLAYER_tag*>(player->data)->scripts[0]->streams[0].time == 3.0f);
    // Com o canal de trava ligado, não vira.
    data_sw->lock = 0x31;
    switch_channels[0x31] = 1;
    Switch_Activate(sw, &by);
    CHECK(switch_channels[0x30] == 1);
}

TEST(script_camera_takes_over_and_gives_back) {
    fresh();
    Camera_CreateCameras();
    Camera_Enable(0, 1, 1, nullptr);
    ScriptBuilder sb;
    sb.camera = 1;
    sb.end = 10;
    ScriptBuilder::Stream& s = sb.add(0, 0);
    ScriptBuilder::camera_cmd(s, 0, 2);
    ScriptBuilder::wait(s, 4);
    ScriptBuilder::u8(s, 0xf);  // fim da câmera
    ScriptBuilder::u8(s, 0xf);
    ScriptBuilder::stream_end(s, 10);
    sb.keys = {key(0, 2, 0, 0, 0.5f), key(8, 2, 0, 8, 0.5f)};
    std::vector<uint8_t> storage;
    SCRIPTINFO* si = load(0x06000205, sb, storage);
    Script_Play(si, 0);
    CHECK(ScriptCam == 0x06000205);
    for (int i = 0; i < 3; ++i) Script_Update(si);  // quadros 0 a 2
    viewer_tag* v4 = glb_viewer[4];
    CHECK(v4->active == 1 && glb_viewer[0]->active == 0);
    CHECK(Script_ActiveCamera() == v4);
    // A câmera de script foi para o quadro 2 (Script_CameraStart pôs o lugar do script na origem).
    CHECK_NEAR(v4->camera.m[3][0], 2.0f, 1e-4f);
    CHECK_NEAR(v4->camera.m[3][1], 2.0f, 1e-4f);
    CHECK_NEAR(v4->fov, 0.5f * v4->aspect, 1e-5f);
    for (int i = 0; i < 3; ++i) Script_Update(si);  // quadro 4: fim da câmera
    CHECK(glb_viewer[0]->active == 1 && v4->active == 0);
    CHECK(Script_ActiveCamera() == nullptr);
}

TEST(script_stack_and_camera_states) {
    int32_t data[3];
    STACKINFO st;
    Stack_Init(&st, 3, data);
    CHECK(Stack_IsEmpty(&st) == 1);
    CHECK(Stack_Push(&st, 7) == 1 && Stack_Push(&st, 9) == 1);
    CHECK(Stack_Push(&st, 11) == 0);  // cabem size - 1
    CHECK(Stack_Top(&st) == 9 && Stack_Pop(&st) == 9 && Stack_Pop(&st) == 7);
    CHECK(Stack_IsEmpty(&st) == 1 && Stack_Pop(&st) == 7);  // vazia: devolve o primeiro
    fresh();
    Camera_CreateCameras();
    Camera_Enable(0, 1, 1, nullptr);
    uint8_t before[11], sprites[11];
    for (int i = 0; i < 11; ++i) {
        before[i] = glb_viewer[i] != nullptr ? glb_viewer[i]->active : 0;
        sprites[i] = glb_viewer[i] != nullptr ? glb_viewer[i]->sprites : 0;
    }
    Camera_PushStates();
    for (uint32_t i = 0; i < 7; ++i) Camera_Enable(i, 0, 0, nullptr);
    Camera_PopStates();
    for (int i = 0; i < 11; ++i) {
        if (glb_viewer[i] == nullptr) continue;
        CHECK(glb_viewer[i]->active == before[i] && glb_viewer[i]->sprites == sprites[i]);
    }
}

TEST(script_hit_damage_and_bullet_filter) {
    fresh();
    alignas(16) static HITDATA_tag hits[3];
    alignas(16) static obj_tag bullets[2], wall;
    alignas(16) static BU_tag data[2];
    static weapon_definition_tag weapons[2];
    std::memset(hits, 0, sizeof(hits));
    std::memset(bullets, 0, sizeof(bullets));
    std::memset(data, 0, sizeof(data));
    std::memset(weapons, 0, sizeof(weapons));
    std::memset(&wall, 0, sizeof(wall));
    weapons[0].stats = 0x100;  // filtrada pela máscara 0x106
    weapons[1].stats = 0x001;
    for (int i = 0; i < 2; ++i) {
        bullets[i].type = 5;
        bullets[i].data = &data[i];
        data[i].weapon = &weapons[i];
    }
    const float damage[3] = {2.5f, 4.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        hits[i].dist = float(3 - i);  // em ordem decrescente: o filtro reordena
        std::memcpy(&hits[i].unknown_08, &damage[i], 4);
        hits[i].next = i < 2 ? &hits[i + 1] : nullptr;
        hits[i].prev = i > 0 ? &hits[i - 1] : nullptr;
    }
    hits[0].obj = &bullets[0];
    hits[1].obj = &bullets[1];
    hits[2].obj = &wall;
    obj_tag* objs[4];
    uint16_t n = 0;
    CHECK(Collide_GetDamageNObjects(&hits[0], objs, &n, 2) == 7.5f);
    CHECK(n == 2 && objs[0] == &bullets[0] && objs[1] == &bullets[1]);
    HITDATA_tag* list = &hits[0];
    Collide_FilterBullets(&list, 0x106);
    // O tiro da arma filtrada sai; os outros ficam ordenados pela distância.
    CHECK(list == &hits[2] && list->next == &hits[1] && hits[1].next == nullptr && list->prev == nullptr);
    CHECK(Collide_GetDamageNObjects(list, nullptr, &n, 4) == 5.0f && n == 0);
    // Sem nada filtrado a lista fica como estava.
    HITDATA_tag* same = list;
    Collide_FilterBullets(&same, 0x8000);
    CHECK(same == list);
}

TEST(script_playscript_state_messages) {
    fresh();
    alignas(16) static Drone_tag drone;
    alignas(16) static obj_tag obj;
    std::memset(&drone, 0, sizeof(drone));
    std::memset(&obj, 0, sizeof(obj));
    drone.obj = &obj;
    obj.data = &drone;
    DCVars_tag dcv{&obj, &drone, nullptr, &drone.sm};
    MsgObject msg{};
    msg.msg = 0;
    CHECK(NDrone2_DSTATE_PlayScript(&dcv, &drone, &obj, &msg) == 1);
    msg.msg = 2;  // sair: não tratada (vai para o estado global)
    CHECK(NDrone2_DSTATE_PlayScript(&dcv, &drone, &obj, &msg) == 0);
    // O guarda do castelo (DTYPE 0x18) troca para o estado 0x36 ao entrar.
    drone.type = 0x18;
    msg.msg = 1;
    CHECK(NDrone2_DSTATE_PlayScript(&dcv, &drone, &obj, &msg) == 1);
    CHECK(drone.sm.change == 1 && drone.sm.next_state == 0x36);
    CHECK((drone.flags & 0x12000) == 0x12000);
}
