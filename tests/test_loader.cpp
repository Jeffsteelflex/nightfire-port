// Testes do carregador portado (src/engine/parsemap.cpp, build.cpp, loader.cpp) com um item de mapa
// sintético, sem os dados do jogo.

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/hashtable.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_file.h"
#include "platform/psi_gfx.h"
#include "test_framework.h"

namespace {

struct Writer {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void u16(uint16_t v) { raw(&v, 2); }
    void f32(float v) { raw(&v, 4); }
    void raw(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        bytes.insert(bytes.end(), b, b + n);
    }
};

// Item de mapa versão 0: blocos {id << 24 | tamanho, conteúdo} em sequência.
struct MapBuilder {
    Writer w;
    std::vector<size_t> offsets;  // posição de cada bloco, para os testes acharem os conteúdos
    MapBuilder() { w.u32(0); }
    size_t block(uint8_t id, const Writer& payload) {
        const size_t at = w.bytes.size();
        w.u32(uint32_t(id) << 24 | uint32_t(payload.bytes.size() + 4));
        w.raw(payload.bytes.data(), payload.bytes.size());
        offsets.push_back(at);
        return at;
    }
};

Writer entity_params(uint32_t hash, uint32_t flags, const float sphere[4]) {
    Writer p;
    p.u32(hash);
    p.u32(flags);
    for (int i = 0; i < 4; ++i) p.f32(sphere[i]);
    for (int i = 0; i < 3; ++i) p.f32(-1.0f);  // box_min
    for (int i = 0; i < 3; ++i) p.f32(1.0f);   // box_max
    return p;
}

void placement(Writer& p, uint16_t entity, uint32_t hash, uint32_t flags, const float pos[3],
               std::vector<std::pair<uint32_t, uint32_t>> params) {
    p.u16(entity);
    p.u16(0);
    p.u32(hash);
    p.u32(flags);
    for (int i = 0; i < 3; ++i) p.f32(pos[i]);
    for (int i = 0; i < 3; ++i) p.f32(0.0f);   // rot
    for (int i = 0; i < 4; ++i) p.f32(0.0f);   // quat
    for (int i = 0; i < 3; ++i) p.f32(1.0f);   // escala
    p.u16(0);  // path_count
    p.u16(0);
    p.u16(0);
    p.u16(0);
    p.u32(uint32_t(params.size()));
    for (const auto& [k, v] : params) {
        p.u32(k);
        p.u32(v);
    }
}

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    hashtable_debug_power_on();
    hashtable_initialise();
    loader_debug_power_on();
    psi_gfx_debug_power_on();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    g_dynamic_object_calls = 0;
    pending_reset();
    glb_world = build_alloc_world();
}

}  // namespace

TEST(plane_eq_contains_points_and_rejects_degenerate) {
    const _VECTOR a{1, 2, 3, 0}, b{4, 2, 7, 0}, c{1, 5, 3, 0};
    plane_equ_tag pl;
    CHECK(Plane_PlaneEq(&pl, &a, &b, &c));
    CHECK_NEAR(pl.a * pl.a + pl.b * pl.b + pl.c * pl.c, 1.0, 1e-6);
    for (const _VECTOR* v : {&a, &b, &c}) CHECK_NEAR(pl.a * v->x + pl.b * v->y + pl.c * v->z + pl.d, 0.0, 1e-5);
    const _VECTOR d{2, 4, 6, 0};
    CHECK(!Plane_PlaneEq(&pl, &a, &d, &d));  // colineares
    CHECK(pl.a == 0 && pl.b == 0 && pl.c == 0 && pl.d == 0);
}

TEST(build_alloc_portal_center_and_padded_box) {
    fresh();
    portal_tag src;
    const float v[4][3] = {{5, 0, 0}, {5, 2, 0}, {5, 2, 4}, {5, 0, 4}};
    for (int i = 0; i < 4; ++i) src.verts[i] = {v[i][0], v[i][1], v[i][2], 0};
    src.plane = {1, 0, 0, -5};
    portal_tag* p = build_alloc_portal(&src, nullptr, nullptr, 1);
    CHECK(p->center.x == 5.0f && p->center.y == 1.0f && p->center.z == 2.0f);
    CHECK_NEAR(p->box_min.x, 4.9, 1e-6);
    CHECK_NEAR(p->box_max.z, 4.1, 1e-6);
    CHECK(p->flag == 1 && p->unknown_24 == 0xffff && p->plane.d == -5.0f);
}

TEST(fixup_entity_type_table) {
    fresh();
    level_tag level;
    std::memset(&level, 0, sizeof(level));
    uint32_t flags = 0;
    const auto type_after = [&](uint32_t type) {
        cel_tag* cel = build_alloc_cel(nullptr);
        cel->type = type;
        parseentity_fixup_entity(cel, &flags, &level);
        return cel;
    };
    CHECK(type_after(0xd8)->type == 0x8019);
    CHECK(type_after(0xf3)->type == 0x8019);
    CHECK(type_after(0x803d)->type == 0x28019);
    CHECK(type_after(0xdd)->type == 0xdd);  // reescrito com o mesmo valor
    CHECK(type_after(0x1234)->type == 0x1234);
    // 0x8019 na origem ganha o bit 0x8000000 e as cores dos parâmetros 0 e 1.
    level.params[0] = 5;
    level.params[1] = 2;
    cel_tag* c = type_after(0x8019);
    CHECK(c->type == (0x8019 | 0x8000000) && c->misc[0xa3 - 0x98] == 0x25);
    c = build_alloc_cel(nullptr);
    c->type = 0x8018;
    c->pos.x = 1.0f;
    parseentity_fixup_entity(c, &flags, &level);
    CHECK(c->type == 0x8018);  // fora da origem
    // 0xc047 sem cor nos parâmetros 0-2 usa 0x3c.
    std::memset(&level, 0, sizeof(level));
    level.params[3] = 1;
    c = type_after(0xc047);
    CHECK(c->type == 0x1804c047 && c->water_y == -500.0f);
    CHECK(c->misc[2] == 0x3c && c->misc[4] == 0x3c && c->misc[6] == 0x3c);
}

TEST(parsemap_builds_runtime_structures_from_synthetic_item) {
    fresh();
    MapBuilder m;
    Writer header;
    header.u32(0);
    header.u32(2);  // entidades
    header.u32(0);  // caminhos
    m.block(0x0e, header);
    Writer xyz;
    xyz.f32(1);
    xyz.f32(2);
    xyz.f32(3);
    const size_t xyz_at = m.block(0x00, xyz);
    const float sphere0[4] = {1, 2, 3, 5};
    m.block(0x04, entity_params(0x02000001, 0x11, sphere0));
    Writer gfx;  // bloco 0x2d vazio: cabeçalho no deslocamento 0x10
    gfx.u32(0x10);
    for (int i = 0; i < 5; ++i) gfx.u32(0);
    const size_t gfx_at = m.block(0x2d, gfx);
    Writer coll;
    coll.u32(4);   // versão
    coll.u16(2);   // caixas
    coll.u16(1);   // lista
    coll.u16(0);   // cabeçalho (x 0x40)
    coll.u16(0);
    for (int box = 0; box < 2; ++box) {
        for (int i = 0; i < 4; ++i) coll.f32(0.0f);  // min
        for (int i = 0; i < 4; ++i) coll.f32(1.0f);  // max
        for (int i = 0; i < 4; ++i) coll.f32(0.0f);
    }
    coll.u32(0);
    coll.u32(0);
    const size_t coll_at = m.block(0x2e, coll);
    const float sphere1[4] = {0, 0, 0, 1};
    m.block(0x04, entity_params(0xffffffff, 0x22, sphere1));
    Writer lod;
    lod.i32(1);
    lod.u32(0);
    lod.i32(-1);
    lod.u32(0);
    m.block(0x27, lod);
    Writer pl;
    const float at_x10[3] = {10, 0, 0}, origin[3] = {0, 0, 0};
    // O tipo da célula é flags & 0xffff, com o bit 0x8000: 0x8019 aqui.
    placement(pl, 0, 0xffffffff, 0x8000 | 0x19, at_x10, {{0, 3}});
    placement(pl, 1, 0xffffffff, 0x8000 | 0x8019, origin, {{0, 5}, {1, 2}});
    placement(pl, 0, 0x02000001, 0x0001, origin, {});  // dinâmico
    m.block(0x1a, pl);
    Writer portals;
    portals.u32(1);
    const float quad[4][3] = {{5, 0, 0}, {5, 1, 0}, {5, 1, 1}, {5, 0, 1}};
    for (const auto& v : quad) for (float f : v) portals.f32(f);
    portals.u16(0);
    portals.u16(1);
    portals.u16(0);
    portals.u16(0);
    m.block(0x21, portals);
    const size_t discard_at = m.block(0x1c, Writer{});
    m.block(0x1d, Writer{});

    // O arquivo fica no heap, como depois do LoaderLoad, em modo de arquivo único.
    const uint32_t len = uint32_t(m.w.bytes.size());
    uint8_t* file = static_cast<uint8_t*>(Mem_Malloc(len, 0x2980, 0));
    std::memcpy(file, m.w.bytes.data(), len);
    DirFileBuf = file;
    DirFileLen = len;
    LoadBufNext = LoadBufLast = reinterpret_cast<char*>(file) + len;
    psiFileSetSingleFileMode(1);

    CHECK(parsemap_parsemap(0x01000001, 0) == 1);  // abre
    CHECK(parsemap_parsemap(0x01000001, 0) == 1);  // blocos
    CHECK(parsemap_parsemap(0x01000001, 0) == 1);  // objetos dinâmicos
    CHECK(parsemap_parsemap(0x01000001, 0) == 0);  // descarte
    psiFileSetSingleFileMode(0);

    CHECK(MapEntityCount == 2);
    celglist_tag* g0 = pCelList;
    celglist_tag* g1 = pCelList + 1;
    CHECK(g0->flags == 0x11 && g1->flags == 0x22);
    CHECK(g0->sphere.w == 5.0f && g0->box_max.z == 1.0f);
    CHECK(g0->gfx->xyz == file + xyz_at + 4 && g0->gfx->xyz_count == 1);
    // parsemap guarda o cabeçalho do bloco; psiCreateEntityGfx troca pelo cabeçalho da geometria.
    CHECK(g1->gfx->ps2_gfx == file + gfx_at + 0x10);
    CHECK(g0->gfx->map == m_pmap && g1->gfx->map == m_pmap);
    CHECK(hashtable_getitem(0x02000001) == g0);
    CHECK(g0->lod == g1 && g1->lod == nullptr);

    // Colisão: só a primeira caixa cresce, uma vez por caixa (2 x 0,01).
    const float* boxes = reinterpret_cast<const float*>(g1->coll->boxes);
    CHECK(g1->coll->box_count == 2 && g1->coll->boxes == file + coll_at + 0x10);
    CHECK_NEAR(boxes[0], -0.02, 1e-6);
    CHECK_NEAR(boxes[4], 1.02, 1e-6);
    CHECK(boxes[12] == 0.0f && boxes[16] == 1.0f);
    CHECK(MemStats[0] == uint32_t(coll.bytes.size()));

    // Células: a última criada fica no início da lista.
    cel_tag* c1 = glb_world->first;
    cel_tag* c0 = c1->next;
    CHECK(c0 != nullptr && c0->next == nullptr);
    CHECK(c1->glist == g1 && c0->glist == g0);
    CHECK(c0->type == 0x8019 && c0->misc[0xa3 - 0x98] == 0x03);  // fora da origem: sem 0x8000000
    CHECK(c1->type == (0x8019 | 0x8000000) && c1->misc[0xa3 - 0x98] == 0x25);
    CHECK_NEAR(c0->sphere.x, 11.0, 1e-5);  // centro (1, 2, 3) levado para pos (10, 0, 0)
    CHECK_NEAR(c0->sphere.y, 2.0, 1e-5);
    CHECK_NEAR(c0->sphere.z, 3.0, 1e-5);
    CHECK(c0->sphere.w == 5.0f);

    // Portal: cada célula recebe o lado que leva à outra, e os dois se conhecem.
    CHECK(c0->portals != nullptr && c1->portals != nullptr);
    CHECK(c1->portals->cel == c0 && c0->portals->cel == c1);
    CHECK(c0->portals->opposite == c1->portals && c1->portals->opposite == c0->portals);
    CHECK_NEAR(std::fabs(c1->portals->plane.a), 1.0, 1e-6);
    CHECK_NEAR(c1->portals->plane.a, -c0->portals->plane.a, 1e-6);  // vértices em ordem inversa

    // Plataforma e objetos: texturas uma vez por item, geometria por entidade, um objeto dinâmico.
    // A entidade 0 não tem bloco 0x2d (o port pula; o original leria o endereço 4).
    CHECK(g_psi_gfx_guards.entity_without_gfx == 1);
    // Um posicionamento dinâmico chega ao despacho; fora da fase principal, o tipo 1 não cria nada.
    CHECK(g_dynamic_object_calls == 1 && DynamicObjCount == 0);

    // Descarte: o arquivo termina no bloco 0x1c e o bloco do heap encolheu.
    CHECK(DirFileLen == discard_at);
    CHECK(FileDiscard == nullptr);
}
