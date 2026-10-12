// Testes de break.cpp e psi_shard.cpp: quebráveis, estilhaços, destrutíveis e zonas que machucam,
// com modelos, cadeias de DMA e colisão sintéticos.

#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

#include "engine/break.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_gfx.h"
#include "platform/psi_shard.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kChip = 0x2000404;
celglist_tag g_chip_model{};

void fresh_world() {
    psiMemHeapSize = 2u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    view_debug_power_on();
    effect_debug_power_on();
    break_debug_power_on();
    explode_debug_power_on();
    psi_draw_debug_power_on();
    psi_shard_debug_power_on();
    hashtable_debug_power_on();
    hashtable_initialise();
    hashtable_additem(kChip, &g_chip_model);
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    std::memset(&GameState, 0, sizeof(GameState));
    std::memset(MPSettings, 0, sizeof(MPSettings));
    std::memset(switch_channels, 0, sizeof(switch_channels));
    GS_SetRefreshRate(60, 60);
    WldGravity = {0.0f, -9.8f, 0.0f, 0.0f};
    for (obj_tag*& p : glb_players) p = nullptr;
    std::memset(EffectInfo, 0, sizeof(EffectInfo));
    EffectInfo[0x10].debris = kChip;
    EffectInfo[0x10].debris_speed = 1.0f;
    EffectInfo[0x10].debris_life = 20;
    InitShards();
}

// Colisão de um piso 20 x 20 em y = 0 (dois triângulos numa folha, como em test_collide.cpp).
struct FloorColl {
    alignas(16) uint8_t head[0x40]{};
    COLLBOX_tag box{};
    uint16_t tris[2][4]{};
    uint8_t surface[2]{0, 0};
    colldata_tag coll{};
    FloorColl() {
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
        coll.box_count = 1;
    }
};

// Projéteis (tipo 5), cada um de uma arma com "stats" e classe dadas.
struct Shooter {
    std::deque<weapon_definition_tag> weapons;
    obj_tag* from = nullptr;
    obj_tag* bullet(uint16_t stats, int16_t klass = 2) {
        weapon_definition_tag& weapon = weapons.emplace_back();
        weapon.stats = stats;
        weapon.klass = klass;
        obj_tag* b = control_create_object(sizeof(BU_tag), nullptr, nullptr, nullptr);
        b->type = 5;
        bullet_of(b)->weapon = &weapon;
        bullet_of(b)->from = from;
        return b;
    }
};

// Um acerto de "by" na lista de "obj" com o dano (HITDATA_tag::unknown_08), como Collide_Update faz.
HITDATA_tag* add_hit(obj_tag* obj, obj_tag* by, float damage, float dist = 1.0f) {
    HITDATA_tag* h = Coll_AddHitToList(&obj->hits, dist, nullptr, by);
    std::memcpy(&h->unknown_08, &damage, 4);
    h->dir = {0.0f, 0.0f, 1.0f, 0.0f};
    return h;
}

level_tag params(std::initializer_list<uint32_t> values) {
    level_tag l{};
    int k = 0;
    for (uint32_t v : values) l.params[k++] = v;
    return l;
}

float tri_area(const float* a, const float* b, const float* c) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float x = u[1] * v[2] - u[2] * v[1], y = u[2] * v[0] - u[0] * v[2], z = u[0] * v[1] - u[1] * v[0];
    return 0.5f * std::sqrt(x * x + y * y + z * z);
}

float word_float(uint32_t w) {
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

SHATTER_VERT vert(float x, float y, float z, float u, float v) {
    SHATTER_VERT s{};
    s.pos = {x, y, z, 0.0f};
    s.uv[0] = u;
    s.uv[1] = v;
    const float c[4] = {100.0f, 50.0f, 25.0f, 64.0f};
    std::memcpy(s.rgba, c, sizeof(c));
    return s;
}

std::vector<SHARD_tag*> shards() {
    std::vector<SHARD_tag*> out;
    for (LLNODE_tag* n = ShardList.first; n != nullptr; n = n->next) out.push_back(reinterpret_cast<SHARD_tag*>(n));
    return out;
}

}  // namespace

TEST(break_bullets_wear_down_and_debris_from_collision) {
    fresh_world();
    FloorColl floor;
    celglist_tag glist{};
    glist.coll = &floor.coll;
    glist.sphere = {0, 0, 0, 2};
    _VECTOR pos{5, 0, 0, 0}, rot{0, 0, 0, 0};
    level_tag level = params({1, 10, 0});
    obj_tag* obj = Break_Create(&pos, &rot, &glist, &level);
    BREAK_tag* b = static_cast<BREAK_tag*>(obj->data);
    CHECK(obj->type == 0x20 && b->kind == 1 && b->strength == 10.0f && b->mask == 0);
    Shooter s;
    add_hit(obj, s.bullet(0x8), 4.0f);
    Break_Update(obj);
    CHECK(b->strength == 6.0f && (obj->life & 1) == 0);
    // As classes 0x106 nunca ferem um quebrável (nem as da máscara).
    Collide_FreeHitList(&obj->hits);
    add_hit(obj, s.bullet(0x2), 50.0f);
    add_hit(obj, s.bullet(0x100), 50.0f);
    Break_Update(obj);
    CHECK(b->strength == 6.0f && obj->hits == nullptr);
    // 7 de dano: abaixo de zero, Break_Kill solta um detrito do material 0x10 (material 0 do acerto) a
    // cada três triângulos de colisão, no centro do triângulo mais a posição do objeto.
    Collide_FreeHitList(&obj->hits);
    HITDATA_tag* last = add_hit(obj, s.bullet(0x8), 7.0f);
    Break_Update(obj);
    CHECK((obj->life & 1) != 0 && g_break_stats.broken == 1);
    CHECK(last->surface == 0x10);
    CHECK_NEAR(last->pos.x, 5.0 - 10.0 / 3.0, 1e-4);
    CHECK_NEAR(last->pos.z, 10.0 / 3.0, 1e-4);
    CHECK_NEAR(last->plane.b, 1.0, 1e-4);
    int chips = 0;
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) chips += o->type == 0x10;
    CHECK(chips == 1);
    // Já marcado para remoção: não é atualizado de novo.
    Break_Update(obj);
    CHECK(g_break_stats.broken == 1);
}

TEST(break_mask_from_parameter_filters_bullet_classes) {
    fresh_world();
    celglist_tag glist{};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    level_tag level = params({1, 10, 0xfff7});  // máscara = ~0xfff7 = 8
    obj_tag* obj = Break_Create(&pos, &rot, &glist, &level);
    CHECK(static_cast<BREAK_tag*>(obj->data)->mask == 8);
    Shooter s;
    add_hit(obj, s.bullet(0x8), 4.0f);
    add_hit(obj, s.bullet(0x10), 3.0f);
    Break_Update(obj);
    CHECK(static_cast<BREAK_tag*>(obj->data)->strength == 7.0f);
}

TEST(collide_filter_bullets_and_damage_sum) {
    fresh_world();
    _VECTOR pos{0, 0, 0, 0};
    obj_tag* target = control_create_object(0x40, &pos, nullptr, nullptr);
    obj_tag* other = control_create_object(0x40, &pos, nullptr, nullptr);
    Shooter s;
    add_hit(target, s.bullet(0x1), 1.0f, 3.0f);
    add_hit(target, other, 2.0f, 1.0f);
    add_hit(target, s.bullet(0x4), 4.0f, 2.0f);
    add_hit(target, s.bullet(0x8), 8.0f, 0.5f);
    obj_tag* hit_objs[2];
    uint16_t n = 99;
    CHECK(Collide_GetDamageNObjects(target->hits, hit_objs, &n, 2) == 15.0f && n == 2);
    // Sai o de classe 4; os outros ficam em ordem de distância, encadeados nos dois sentidos.
    Collide_FilterBullets(&target->hits, 0x4);
    std::vector<float> d;
    for (HITDATA_tag* h = target->hits; h != nullptr; h = h->next) {
        d.push_back(h->dist);
        if (h->next != nullptr) CHECK(h->next->prev == h);
    }
    CHECK(d.size() == 3 && d[0] == 0.5f && d[1] == 1.0f && d[2] == 3.0f);
    CHECK(target->hits->prev == nullptr);
    CHECK(Collide_GetDamageNObjects(target->hits, nullptr, nullptr, 0) == 11.0f);
}

TEST(shatter_preserves_area_and_texture_mapping) {
    fresh_world();
    _VECTOR pos{0, 0, 0, 0};
    obj_tag* obj = control_create_object(0x40, &pos, nullptr, nullptr);
    obj->pos = {1.0f, 2.0f, 0.0f, 0.0f};
    HITDATA_tag hit{};
    hit.pos = {1.0f, 2.0f, 0.0f, 0.0f};
    hit.dir = {0.0f, 0.0f, 1.0f, 0.0f};
    // Triângulo de área 6 no espaço do objeto; UV: u = x / 4 - y / 6 + 0.5, v = y / 3.
    _SHATTER_VERT_LIST list{{vert(-2, 0, 0, 0, 0), vert(2, 0, 0, 1, 0), vert(0, 3, 0, 0, 1)}};
    ShatterPolyRecurse(0, &list, obj, &hit, 0x1234);
    const std::vector<SHARD_tag*> all = shards();
    CHECK(all.size() > 4 && all.size() == g_break_stats.shards);
    float area = 0.0f;
    for (SHARD_tag* s : all) {
        const auto* w = reinterpret_cast<const uint32_t*>(s->chain);
        CHECK(w[0] == 0x30000004 && w[1] == 0x1234 && s->texture == 0x1234);
        float p[3][3];
        float centre[3] = {};
        for (int k = 0; k < 3; ++k) {
            for (int c = 0; c < 3; ++c) {
                p[k][c] = word_float(w[0x12 + k * 12 + c]);
                centre[c] += p[k][c] / 3.0f;
            }
            CHECK(w[0x12 + k * 12 + 4] == 100 && w[0x12 + k * 12 + 5] == 50 && w[0x12 + k * 12 + 7] == 64);
            // UV pela posição no espaço do objeto (centro do estilhaço - obj->pos + vértice local).
            const float x = s->pos.x - obj->pos.x + p[k][0], y = s->pos.y - obj->pos.y + p[k][1];
            CHECK_NEAR(word_float(w[0x37 + k * 4]), x / 4.0 - y / 6.0 + 0.5, 1e-4);
            CHECK_NEAR(word_float(w[0x37 + k * 4 + 1]), y / 3.0, 1e-4);
        }
        for (float c : centre) CHECK_NEAR(c, 0.0, 1e-4);  // vértices relativos ao centro
        area += tri_area(p[0], p[1], p[2]);
        // A aresta mais longa (ao quadrado) não passa de metade do quadrado da distância ao tiro,
        // entre 0,5 e 100.
        const float dx = s->pos.x - hit.pos.x, dy = s->pos.y - hit.pos.y, dz = s->pos.z - hit.pos.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        const float limit = std::fmin(std::fmax(d2 * 0.5f, 0.5f), 100.0f);
        for (int k = 0; k < 3; ++k) {
            const float* a = p[k];
            const float* b = p[(k + 1) % 3];
            const float e2 = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
            CHECK(e2 <= limit * 1.0001f);
        }
        // Espera pela distância, voa na direção do tiro a min(0.125 / d, 0.1) por tick, 25 ticks.
        const float dist = std::sqrt(d2);
        CHECK_NEAR(s->delay, dist * 15.0 / 60.0, 1e-5);
        CHECK_NEAR(s->vel.z, std::fmin(0.125f / dist, 0.1f), 1e-5);
        CHECK(s->vel.x == 0.0f && s->vel.y == 0.0f);
        CHECK_NEAR(s->life, 25.0 / 60.0, 1e-6);
    }
    CHECK_NEAR(area, 6.0, 1e-3);
}

TEST(shards_wait_fly_fall_and_go) {
    fresh_world();
    _VECTOR pos{0, 0, 0, 0};
    obj_tag* obj = control_create_object(0x40, &pos, nullptr, nullptr);
    HITDATA_tag hit{};
    hit.pos = {0.0f, 0.0f, -1.0f, 0.0f};  // a 1 m: espera 15 ticks
    hit.dir = {0.0f, 0.0f, 1.0f, 0.0f};
    _SHATTER_VERT_LIST list{{vert(-0.1f, 0, 0, 0, 0), vert(0.1f, 0, 0, 1, 0), vert(0, 0.1f, 0, 0, 1)}};
    ShatterPolyRecurse(0, &list, obj, &hit, 0);
    CHECK(shards().size() == 1);
    SHARD_tag* s = shards()[0];
    const _VECTOR start = s->pos;
    int waited = 0;
    while (0.0f < s->delay) {
        UpdateAllShards();
        ++waited;
        CHECK(s->pos.z == start.z);
    }
    CHECK(waited >= 14 && waited <= 16);
    const float vy0 = s->vel.y;
    UpdateAllShards();
    CHECK_NEAR(s->pos.z - start.z, 0.1, 1e-6);  // min(0.125 / 1, 0.1)
    CHECK_NEAR(s->vel.y - vy0, -9.8 * 4.0 / 3600.0, 1e-7);
    // Desenho: um triângulo no mundo com a cor da cadeia, no estado dos estilhaços.
    psiPreDraw();
    psiSetShardRenderStates();
    DrawAllShards();
    CHECK(g_psi_frame.tri_lists.size() == 1 && g_psi_frame.tri_vertices.size() == 3);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].triangles == 0 && g_psi_frame.draws[0].blend == 1);
    CHECK(g_psi_frame.draws[0].gs_test == 0x5000d && ((g_psi_frame.draws[0].gs_zbuf >> 32) & 1) == 1);
    float c[3] = {};
    for (const PsiTriVertex& v : g_psi_frame.tri_vertices) {
        for (int k = 0; k < 3; ++k) c[k] += v.pos[k] / 3.0f;
        CHECK(v.rgba[0] == 100 && v.rgba[1] == 50 && v.rgba[2] == 25 && v.rgba[3] == 64);
    }
    CHECK_NEAR(c[0], s->pos.x, 1e-4);
    CHECK_NEAR(c[1], s->pos.y, 1e-4);
    CHECK_NEAR(c[2], s->pos.z, 1e-4);
    // Some depois de 25 ticks voando.
    for (int i = 0; i < 30; ++i) UpdateAllShards();
    CHECK(ShardList.first == nullptr && ShardList.count == 0);
}

TEST(breakable_data_reads_strips_from_the_dma_chain) {
    fresh_world();
    // Lote 1: 4 vértices de tira (os 2 primeiros com ADC) depois da textura A; lote 2: 3 vértices com
    // a textura B.
    std::vector<uint32_t> c;
    const auto f = [](float v) {
        uint32_t w;
        std::memcpy(&w, &v, 4);
        return w;
    };
    const auto batch = [&](uint32_t texture, int n, float base) {
        c.insert(c.end(), {0x30000004u, texture, 0u, 0u});
        const size_t tag = c.size();
        c.insert(c.end(), {0x10000000u, 0u, 0x01000404u, 0x6c018000u, 0x8000u | uint32_t(n), 0u, 0x412u, 8u});
        c.push_back(0x64008000u | uint32_t(n) << 16);
        for (int k = 0; k < n; ++k) c.insert(c.end(), {f(base + k * 0.25f), f(0.5f)});
        c.push_back(0x68008000u | uint32_t(n) << 16);
        for (int k = 0; k < n; ++k) c.insert(c.end(), {f(base + k), f(1.0f), f(2.0f)});
        c.push_back(0x6e000000u | uint32_t(n) << 16);
        for (int k = 0; k < n; ++k) c.push_back(k < 2 ? 0x80000000u : 0u);
        c.push_back(0x6e004000u | uint32_t(n) << 16);
        for (int k = 0; k < n; ++k) c.push_back(0x40302010u + uint32_t(k));
        c.push_back(0x17000000u);
        while (c.size() % 4 != 0) c.push_back(0);
        c[tag] |= uint32_t((c.size() - tag) / 4 - 1);
    };
    batch(0xaaaa0, 4, 0.0f);
    batch(0xbbbb0, 3, 10.0f);
    c.insert(c.end(), {0x60000000u, 0u, 0u, 0u});
    alignas(16) static uint8_t box[0x38];
    std::memset(box, 0, sizeof(box));
    const uint32_t dma = uint32_t(reinterpret_cast<uintptr_t>(c.data()));
    const uint32_t verts = 7, tris = 3;
    std::memcpy(box + 0x20, &dma, 4);
    std::memcpy(box + 0x2c, &verts, 4);
    std::memcpy(box + 0x30, &tris, 4);
    uint32_t hdr[3] = {1, uint32_t(reinterpret_cast<uintptr_t>(box)), 0};
    entgfx_tag gfx{};
    gfx.ps2_gfx = reinterpret_cast<uint8_t*>(hdr);
    celglist_tag glist{};
    glist.gfx = &gfx;
    psiCreateBreakableData(&glist);
    const auto* data = reinterpret_cast<const BREAKGFX_tag*>(uintptr_t(hdr[2]));
    CHECK(data != nullptr && data->tris != nullptr && data->verts != nullptr);
    const BREAKTRI_tag* t = data->tris;
    CHECK(t[0].texture == 0xaaaa0 && t[0].vert[0] == 0 && t[0].vert[1] == 1 && t[0].vert[2] == 2);
    CHECK(t[1].texture == 0xaaaa0 && t[1].vert[0] == 1 && t[1].vert[2] == 3);
    // Como no original, os índices do segundo lote contam do começo do lote.
    CHECK(t[2].texture == 0xbbbb0 && t[2].vert[0] == 0 && t[2].vert[2] == 2);
    const SHATTER_VERT* v = data->verts;
    CHECK(v[5].pos.x == 11.0f && v[5].pos.y == 1.0f && v[5].pos.z == 2.0f);
    CHECK(v[5].uv[0] == 10.25f && v[5].uv[1] == 0.5f);
    CHECK(v[5].rgba[0] == 0x11 && v[5].rgba[1] == 0x20 && v[5].rgba[2] == 0x30 && v[5].rgba[3] == 0x40);
    CHECK(g_shard_guards.unknown_code == 0 && g_shard_guards.tri_overflow == 0 && g_shard_guards.vert_overflow == 0);
    // Uma vez por modelo; psiGetTriList devolve as quantidades da caixa.
    const BREAKTRI_tag* first = data->tris;
    psiCreateBreakableData(&glist);
    CHECK(data->tris == first);
    obj_tag holder{};
    holder.glist = &glist;
    TRILIST_tag* list = psiGetTriList(&holder);
    CHECK(list->tri_count == 3 && list->vert_count == 7 && list->tris == first && list->verts == v);
    // Caixa que reserva menos: o que sobra não é gravado e conta como guarda.
    hdr[2] = 0;
    const uint32_t two = 2;
    std::memcpy(box + 0x30, &two, 4);
    psiCreateBreakableData(&glist);
    CHECK(g_shard_guards.tri_overflow == 1);
}

TEST(shard_texture_comes_from_the_tex_table) {
    fresh_world();
    psi_gfx_debug_power_on();
    alignas(16) static uint32_t packet[16];
    pstex_tag rec{};
    rec.tex_index = 7;
    Tex[0].dma = packet;
    Tex[0].rec = &rec;
    NumPS2TexturesLoaded = 1;
    const uint32_t texture = uint32_t(reinterpret_cast<uintptr_t>(packet));
    CHECK(GetTextureFromDMA(reinterpret_cast<int32_t*>(packet)) == uint32_t(reinterpret_cast<uintptr_t>(&rec)));
    CHECK(GetTextureFromDMA(nullptr) == 0);
    _SHATTER_VERT_LIST list{{vert(0, 0, 0, 0, 0), vert(1, 0, 0, 1, 0), vert(0, 1, 0, 0, 1)}};
    list.vert[0].texture = texture;
    int32_t* chain = psiCreateShard(&list);
    MATRIX m{};
    for (int k = 0; k < 4; ++k) m.m[k][k] = 1.0f;
    m.m[3][0] = 5.0f;
    m.m[0][3] = 9.0f;  // a coluna w vira (0, 0, 0, 1)
    psiPreDraw();
    psiSetShardRenderStates();
    psiDrawShard(chain, &m, texture);
    CHECK(m.m[0][3] == 0.0f && m.m[3][3] == 1.0f && ShardTexture == texture);
    CHECK(g_psi_frame.tri_lists.size() == 1 && g_psi_frame.tri_lists[0].texture == 7);
    CHECK(g_psi_frame.tri_vertices[1].pos[0] == 6.0f && g_psi_frame.tri_vertices[2].uv[1] == 1.0f);
    // A textura só é procurada quando muda; uma que não está em Tex deixa a anterior.
    rec.tex_index = 9;
    psiDrawShard(chain, &m, texture);
    CHECK(g_psi_frame.tri_lists[1].texture == 7);
    psiDrawShard(chain, &m, 0x1230);
    CHECK(g_psi_frame.tri_lists[2].texture == 7 && ShardTexture == 0x1230);
    Tex[0] = {};
    NumPS2TexturesLoaded = 0;
}

TEST(destroy_smashes_by_damage_and_channels) {
    fresh_world();
    celglist_tag glist{}, smashed{};
    glist.lod = &smashed;
    glist.sphere = {0, 0, 0, 1};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    // Canal de saída 5, de entrada 6, sem explosão, resistência 10, raio do dano 2, sem atraso.
    level_tag level = params({5, 6, 0, 10, 0, 200, 0});
    obj_tag* obj = Destroy_Create(&pos, &rot, &level, &glist, nullptr);
    DESTROY_tag* d = static_cast<DESTROY_tag*>(obj->data);
    CHECK(obj->type == 0x21 && d->smashed == &smashed && d->channel_out == 5 && d->channel_in == 6);
    CHECK(d->strength == 10.0f && d->mask == 0 && d->delay == 0);
    CHECK_NEAR(d->blast, 2.0, 1e-6);
    Shooter s;
    add_hit(obj, s.bullet(0x8), 4.0f);
    Destroy_CollisionHandler(obj);
    CHECK(d->strength == 6.0f && obj->glist == &glist && d->smashed_flag == 0);
    Collide_FreeHitList(&obj->hits);
    add_hit(obj, s.bullet(0x8), 7.0f);
    Destroy_CollisionHandler(obj);
    CHECK(d->smashed_flag == 1 && obj->glist == &smashed && switch_channels[5] == 1 && obj->type == 1);
    CHECK(g_break_stats.smashed == 1 && g_explode_stats.created == 0);
    Destroy_Smash(obj, nullptr, 0);
    CHECK(g_break_stats.smashed == 1);
}

TEST(destroy_delay_counts_after_the_channel_smash) {
    fresh_world();
    celglist_tag glist{}, next{};
    glist.sphere = {0, 0, 0, 1};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    // Atraso de 1 s (60 ticks) e explosão do tipo 3 (script 0x6000052, raio x 3, dano 30).
    level_tag level = params({5, 6, 3, 10, 0, 200, 1});
    obj_tag* obj = Destroy_Create(&pos, &rot, &level, &glist, &next);
    DESTROY_tag* d = static_cast<DESTROY_tag*>(obj->data);
    CHECK(d->smashed == &next && d->delay == -60);
    Destroy_Update(obj);
    CHECK(d->smashed_flag == 0);
    switch_channels[6] = 1;
    Destroy_Update(obj);
    CHECK(d->smashed_flag == 1 && d->channel_in == 0 && d->delay == 60 && obj->type == 0x21);
    CHECK(switch_channels[5] == 0 && g_explode_stats.created == 1);
    const obj_tag* boom = nullptr;
    for (const obj_tag* o = DynamicObjList.next; o != nullptr; o = o->next) {
        if (o->type == 0xf) boom = o;
    }
    CHECK(boom != nullptr);
    if (boom != nullptr) {
        const EXPLODE_tag* x = static_cast<const EXPLODE_tag*>(boom->data);
        CHECK_NEAR(x->radius, 2.0, 1e-6);
        CHECK(x->damage == 30.0f && x->duration == 15);
    }
    for (int i = 0; i < 59; ++i) Destroy_Update(obj);
    CHECK(switch_channels[5] == 0 && obj->type == 0x21);
    Destroy_Update(obj);
    CHECK(switch_channels[5] == 1 && obj->type == 1);
}

TEST(destroy_class_0x67_heats_the_tint) {
    fresh_world();
    celglist_tag glist{};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    level_tag level = params({0, 0, 0, 100});
    obj_tag* obj = Destroy_Create(&pos, &rot, &level, &glist, nullptr);
    CHECK(obj->unknown_103[1] == 0xff && obj->unknown_103[2] == 0xff);
    Shooter s;
    add_hit(obj, s.bullet(0x8, 0x67), 1.0f);
    Destroy_CollisionHandler(obj);
    CHECK(obj->unknown_103[1] == 0xfa && obj->unknown_103[2] == 0xfa && obj->unknown_103[0] == 0xff);
    Collide_FreeHitList(&obj->hits);
    Destroy_CollisionHandler(obj);
    CHECK(obj->unknown_103[1] == 0xfb && obj->unknown_103[2] == 0xfb);
}

TEST(hurt_zone_follows_its_path_when_touched) {
    fresh_world();
    // Caminho de 3 quadros ao longo de x (0, 5, 10), duração de 4 ticks: meio quadro por tick.
    static framelist_tag path[3] = {{{0, 0, 0}, {0, 0, 0, 1}}, {{5, 0, 0}, {0, 0, 0, 1}}, {{10, 0, 0}, {0, 0, 0, 1}}};
    level_tag level = params({3, 25, 0, 0, 8, 1});
    level.path = path;
    level.path_count = 3;
    level.path_unknown = 4;
    celglist_tag glist{};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* obj = Hurt_Create(&pos, &rot, &level, &glist);
    const HURT_tag* h = static_cast<const HURT_tag*>(obj->data);
    CHECK(obj->type == 0x27 && (obj->mode & 0x44) == 0x44 && h->frames == 2 && h->touch == 8 && h->once == 1);
    CHECK(Hurt_GetDamage(obj) == 25.0f && Hurt_GetType(obj) == 3);
    CHECK_NEAR(h->speed, 0.5, 1e-6);
    // Sem toque do jogador (um drone na máscara 8 não conta), fica parada.
    obj_tag* drone = control_create_object(0x40, &pos, nullptr, nullptr);
    drone->type = 2;
    Coll_AddHitToList(&obj->hits, 1.0f, nullptr, drone);
    Hurt_Update(obj);
    CHECK(obj->state == 0);
    obj_tag* player = control_create_object(0x40, &pos, nullptr, nullptr);
    player->type = 3;
    Coll_AddHitToList(&obj->hits, 1.0f, nullptr, player);
    Hurt_Update(obj);
    CHECK(obj->state == 1);
    Collide_FreeHitList(&obj->hits);
    std::vector<float> xs;
    for (int i = 0; i < 6 && obj->state == 1; ++i) {
        Hurt_Update(obj);
        xs.push_back(obj->matrix.m[3][0]);
    }
    // Quadros 0, 0.5, 1, 1.5, 2 (x = 0, 2.5, 5, 7.5, 10) e no fim volta ao começo e some (once).
    CHECK(xs.size() == 5);
    if (xs.size() == 5) {
        CHECK_NEAR(xs[0], 0.0, 1e-4);
        CHECK_NEAR(xs[2], 5.0, 1e-4);
        CHECK_NEAR(xs[4], 10.0, 1e-4);
        CHECK(xs[1] > xs[0] && xs[3] > xs[2]);
    }
    CHECK(obj->state == 2 && h->t == 0.0f);
    Hurt_Update(obj);
    CHECK(obj->type == 1);
}

TEST(hurt_zone_with_channel_hides_and_waits) {
    fresh_world();
    static framelist_tag path[2] = {{{0, 0, 0}, {0, 0, 0, 1}}, {{1, 0, 0}, {0, 0, 0, 1}}};
    level_tag level = params({0, 5, 0x40, 0, 0, 0});
    level.path = path;
    level.path_count = 2;
    level.path_unknown = 60;
    celglist_tag glist{};
    _VECTOR pos{0, 0, 0, 0}, rot{0, 0, 0, 0};
    obj_tag* obj = Hurt_Create(&pos, &rot, &level, &glist);
    CHECK((obj->flags & 0x20) != 0);
    Hurt_Update(obj);
    CHECK(obj->state == 0 && (obj->flags & 0x20) != 0);
    switch_channels[0x40] = 1;
    Hurt_Update(obj);
    CHECK(obj->state == 1 && (obj->flags & 0x20) == 0);
    Hurt_Update(obj);
    switch_channels[0x40] = 0;
    Hurt_Update(obj);  // ainda anda neste tick, mas volta ao estado 0 escondida
    CHECK(obj->state == 0 && (obj->flags & 0x20) != 0);
}
