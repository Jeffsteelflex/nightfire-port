// Testes da apresentação da campanha: a tela de carga (platform/psi_loading.cpp, ResetMap_LevelCode2Img em
// engine/resetmap.cpp), o tempo (engine/weather.cpp: gotas, caixas de chuva, raios, nuvens) e os enfeites
// (engine/scenery.cpp: ondulações, hélices, folhas, paredes de um lado, bafo).

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/scenery.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weather.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_gfx.h"
#include "platform/psi_loading.h"
#include "test_framework.h"

namespace {

// Quantos Rand_Random uma chamada gastou: refaz a sequência a partir das sementes de antes.
uint32_t rand_calls_between(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    const uint32_t sx = SEED_X, sy = SEED_Y;
    SEED_X = x0;
    SEED_Y = y0;
    uint32_t n = 0;
    while ((SEED_X != x1 || SEED_Y != y1) && n < 64) {
        Rand_Random();
        ++n;
    }
    SEED_X = sx;
    SEED_Y = sy;
    return n;
}

}  // namespace

TEST(loading_blobs_ping_pong_every_11_vblanks) {
    // A VBlankInt com PS2LSActive 2: BlobTime (com sinal) começa em 0, então o primeiro VBlank já anda; depois um passo
    // a cada 11 VBlanks, de 0 a 6 e de volta.
    psi_loading_debug_power_on();
    const int32_t saved = PS2LSActive;
    PS2LSActive = 2;
    ResetLoadingBlobs();
    CHECK(psi_loading_vblank() && BlobToDraw == 1);
    int changes = 0;
    for (int v = 0; v < 10; ++v) changes += psi_loading_vblank() ? 1 : 0;
    CHECK(changes == 0 && BlobToDraw == 1);
    CHECK(psi_loading_vblank() && BlobToDraw == 2);
    for (int step = 3; step <= 6; ++step) {
        for (int v = 0; v < 11; ++v) psi_loading_vblank();
        CHECK(BlobToDraw == step);
    }
    CHECK(BlobDir == -1);
    for (int v = 0; v < 11; ++v) psi_loading_vblank();
    CHECK(BlobToDraw == 5);
    // Fora da tela de carga nada anda.
    PS2LSActive = 0;
    CHECK(!psi_loading_vblank() && BlobToDraw == 5);
    PS2LSActive = saved;
    psi_loading_debug_power_on();
}

TEST(loading_blob_cells) {
    // CreateLoadingBlob(n): seis quadrados de 16 x 16 a partir de (55, 309), os n primeiros acesos (0x8029a0ee).
    psi_loading_debug_power_on();
    BlobTexture = 7;
    BlobTextWidth = 32;
    BlobTextHeight = 16;
    Psi2DQuad q[kLoadingBlobCells];
    psi_loading_blob(2, q);
    for (int i = 0; i < kLoadingBlobCells; ++i) {
        CHECK(q[i].x0 == 55.0f + 16.0f * float(i) && q[i].x1 - q[i].x0 == 16.0f);
        CHECK(q[i].y0 == 309.0f && q[i].y1 == 325.0f);
        CHECK(q[i].texture == 7 && !q[i].linear && q[i].alpha == (0x64u | (uint64_t(0x80) << 32)));
        CHECK(q[i].u1 == 0.5f && q[i].v1 == 1.0f);  // 16 texels da textura
        const bool lit = i < 2;
        CHECK(q[i].rgba[0] == (lit ? 0xee : 0x5c) && q[i].rgba[1] == (lit ? 0xa0 : 0x31));
        CHECK(q[i].rgba[2] == (lit ? 0x29 : 0x0c) && q[i].rgba[3] == 0x80);
    }
    psi_loading_debug_power_on();
}

TEST(loading_screen_starts_blobs_on_mem_init) {
    // psiStartLoadingScreen: PS2LSActive 1; o psiAllocateLoadingBlobs do Mem_Init seguinte liga as bolinhas (2);
    // psiEndLoadingScreen desliga.
    psi_loading_debug_power_on();
    const int32_t saved = PS2LSActive;
    PS2LSActive = 0;
    psiAllocateLoadingBlobs();
    CHECK(PS2LSActive == 0);
    psiStartLoadingScreen();
    CHECK(PS2LSActive == 1 && BlobToDraw == 0 && BlobDir == 1 && g_psi_loading_stats.screens == 1);
    psiAllocateLoadingBlobs();
    CHECK(PS2LSActive == 2 && g_psi_loading_stats.blobs == 1);
    psiEndLoadingScreen();
    CHECK(PS2LSActive == 0);
    PS2LSActive = saved;
    psi_loading_debug_power_on();
}

TEST(loading_level_images_and_draws) {
    // ResetMap_LevelCode2Img: a imagem de cada fase e quantos sorteios gasta (o jogo usa Rand_Random como o PS2). Sem
    // os textos pedidos (nulos), as fases com tabela própria só sorteiam no fim (a troca pela genérica: 1); as de dica
    // fixa e as do multijogador não sorteiam.
    struct Case {
        uint32_t level, image, draws;
    };
    const Case cases[] = {
        {0x7000001, 0x30000d2, 1}, {0x7000004, 0x30000d2, 1}, {0x7000005, 0x30000d0, 0}, {0x7000006, 0x30000d0, 1},
        {0x7000008, 0x30000d0, 1}, {0x7000009, 0x30000d5, 1}, {0x700000b, 0x30000d5, 1}, {0x700000c, 0x30000d3, 1},
        {0x700000d, 0x30000d3, 1}, {0x7000011, 0x30000d6, 1}, {0x7000013, 0x30000d6, 1}, {0x7000014, 0x30000d1, 1},
        {0x7000016, 0x30000d1, 1}, {0x700001b, 0x30000d4, 1}, {0x7000023, 0x300016e, 0}, {0x700004a, 0x30000d6, 0},
        {0x7000048, 0x30000d7, 0}, {0x7000500, 0x30000d7, 0},
    };
    for (const Case& c : cases) {
        const uint32_t x0 = SEED_X, y0 = SEED_Y;
        CHECK(ResetMap_LevelCode2Img(c.level, nullptr, nullptr) == c.image);
        CHECK(rand_calls_between(x0, y0, SEED_X, SEED_Y) == c.draws);
    }
}

// ------------------------------------------------------------------ o tempo e os enfeites

namespace {

// Um mundo vazio (sem salas) com o heap, as listas de objetos, a câmera 0 e um jogador simples.
struct WeatherWorld {
    alignas(16) obj_tag player{};
    alignas(16) viewer_tag viewer{};
    viewer_tag* saved[kViewerCount];
    obj_tag* saved_players[4];
    WeatherWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        collide_debug_power_on();
        control_init_object_lists();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        hashtable_debug_power_on();
        hashtable_initialise();
        effect_debug_power_on();
        weather_debug_power_on();
        scenery_debug_power_on();
        psi_draw_debug_power_on();
        std::memset(switch_channels, 0, sizeof(switch_channels));
        std::memset(&GameState, 0, sizeof(GameState));
        GS_SetRefreshRate(60, 60);
        std::memcpy(saved, glb_viewer, sizeof(saved));
        std::memset(glb_viewer, 0, sizeof(glb_viewer));
        viewer.sky_visible = 1;
        viewer.pos = {0.0f, 5.0f, 0.0f, 1.0f};
        glb_viewer[0] = &viewer;
        std::memcpy(saved_players, glb_players, sizeof(saved_players));
        std::memset(glb_players, 0, sizeof(glb_players));
        player.type = 3;
        player.pos = {0.0f, 5.0f, 0.0f, 1.0f};
        glb_players[0] = &player;
        GlobalEnv = nullptr;
        SuperHappyLightningFlag = 0;
        Reflicker = 0;
    }
    ~WeatherWorld() {
        std::memcpy(glb_viewer, saved, sizeof(saved));
        std::memcpy(glb_players, saved_players, sizeof(saved_players));
        weather_debug_power_on();
        GlobalEnv = nullptr;
        SuperHappyLightningFlag = 0;
        Reflicker = 0;
    }
};

// Uma caixa de chuva com o modelo "box" (a caixa do celglist_tag) no lugar "at".
obj_tag* rain_box(celglist_tag& box, _VECTOR at) {
    level_tag level{};
    return RainBox_Create(&at, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), &level, &box);
}

uint32_t count_type(uint8_t type) {
    uint32_t n = 0;
    for (const obj_tag* o = control_first_object(); o != nullptr; o = o->next) n += o->type == type ? 1 : 0;
    return n;
}

_VECTOR* zero() { return const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR); }

}  // namespace

TEST(weather_init_drops_by_kind_and_level) {
    // InitDrops: o máximo e o vento pelo tipo, as velocidades sorteadas pela fase, as gotas mortas e o InitBodge
    // (07000005-06).
    WeatherWorld w;
    GameState.level_file = 0x7000005;
    InitDrops(0);
    CHECK(DropType == 0 && CurrentMax == 0x1000 && WindVector.y == 0.0f && InitBodge == 1);
    bool ranges = true;
    for (const _VECTOR& r : RandTable1) {
        ranges = ranges && std::fabs(r.x) <= 0.02f && std::fabs(r.z) <= 0.02f && r.y <= -0.025f && -0.1f <= r.y;
    }
    CHECK(ranges);
    CHECK(DropVector[0].z == -1024.0f && DropVector[kMaxDrops - 1].x == 0.0f && DropState[17] == 0);
    CHECK(PrevPos.x == -999.0f && PrevPos.z == -999.0f);
    GameState.level_file = 0x7000007;  // as fases 07000007-08: x e z dez vezes menores
    InitDrops(0);
    ranges = true;
    for (const _VECTOR& r : RandTable1) ranges = ranges && std::fabs(r.x) <= 0.002f && std::fabs(r.z) <= 0.002f;
    CHECK(ranges && InitBodge == 0);
    GameState.level_file = 0x700000a;
    InitDrops(1);
    CHECK(CurrentMax == 0x80 && WindVector.y == -0.5f);
    GameState.level_file = 0x7000001;
    InitDrops(1);
    CHECK(CurrentMax == 0x40);
    InitDrops(2);  // sobem: um quarto da velocidade, para cima
    ranges = true;
    for (const _VECTOR& r : RandTable1) ranges = ranges && 0.00625f <= r.y && r.y <= 0.025f && std::fabs(r.x) <= 0.005f;
    CHECK(CurrentMax == 0x800 && ranges);
}

TEST(weather_rain_box_clip_and_add_drop) {
    // RainBox_Clip recorta cada caixa pelo cubo de 30 m em volta de CubeCentre; AddDrop só faz nascer dentro de uma
    // caixa ativa, com o fim no chão dela (ou a câmera - 7,5 m, o que for mais alto); fora, desce 1054 m.
    WeatherWorld w;
    alignas(16) celglist_tag box{};
    box.box_min = {-40.0f, -1.0f, -40.0f, 0.0f};
    box.box_max = {40.0f, 10.0f, 40.0f, 0.0f};
    obj_tag* b = rain_box(box, {0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(b != nullptr && b->type == 1 && RainBoxRoot == b && g_weather_stats.rainboxes == 1);
    if (b == nullptr) return;
    CubeCentre = {0.0f, 5.0f, 0.0f, 1.0f};
    CHECK(RainBox_Clip() == 1);
    const auto* r = static_cast<const RAINBOX_tag*>(b->data);
    CHECK(r->min[0] == -15.0f && r->min[1] == -1.0f && r->max[1] == 10.0f && r->max[2] == 15.0f && r->active == 1);
    DropType = 0;
    WindVector = _VECTOR{};
    Added = 0;
    AddDrop(3, 0, DropState, DropVector);
    CHECK(Added == 1 && DropState[3] == 1);
    CHECK(DropVector[3].y >= 12.5f && DropVector[3].y <= 20.0f && DropVector[3].w == -1.0f);
    // Longe da caixa: nenhuma ativa, e a gota não nasce.
    CubeCentre = {200.0f, 5.0f, 200.0f, 1.0f};
    CHECK(RainBox_Clip() == 0 && r->active == 0);
    DropState[4] = 0;
    AddDrop(4, 0, DropState, DropVector);
    CHECK(DropState[4] == 0 && DropVector[4].y < -1000.0f);
}

TEST(weather_drops_fall_die_and_need_the_sky) {
    // UpdateDrops: até 21 gotas novas por tick, as vivas andam e morrem ao passar do fim; sem céu visível, Added = 999
    // e DrawDrops não desenha nada.
    WeatherWorld w;
    alignas(16) celglist_tag box{};
    box.box_min = {-40.0f, -1.0f, -40.0f, 0.0f};
    box.box_max = {40.0f, 30.0f, 40.0f, 0.0f};
    rain_box(box, {0.0f, 0.0f, 0.0f, 0.0f});
    GameState.level_file = 0x7000008;
    InitDrops(0);
    InitBodge = 0;
    UpdateDrops();
    CHECK(Added <= 0x15 && Weather_LiveDrops() > 0 && Weather_LiveDrops() <= 0x15);
    for (int t = 0; t < 600; ++t) UpdateDrops();
    bool above = true;
    for (int i = 0; i < CurrentMax; ++i) {
        if (DropState[i] == 1) above = above && DropVector[i].w <= DropVector[i].y;
    }
    CHECK(above && Weather_LiveDrops() > 100);
    CHECK(std::fabs(WindVector.x) <= 0.01f && std::fabs(WindVector.z) <= 0.01f);
    // Uma gota no fim do caminho morre e desce 1024 m.
    int live = -1;
    for (int i = 0; i < CurrentMax && live < 0; ++i) {
        if (DropState[i] == 1) live = i;
    }
    CHECK(live >= 0);
    if (live >= 0) {
        DropVector[live].y = DropVector[live].w + 0.001f;
        const float y = DropVector[live].y;
        UpdateDrops();
        CHECK(DropState[live] == 0 && DropVector[live].y < y - 1000.0f);
    }
    w.viewer.sky_visible = 0;
    UpdateDrops();
    CHECK(Added == 999);
    psiPreDraw();
    const size_t before = g_psi_frame.draws.size();
    DrawDrops();
    CHECK(g_psi_frame.particle_lists.empty() && g_psi_frame.draws.size() == before);
}

TEST(weather_lightning_pairs_and_fractal) {
    // Dois raios com o mesmo parâmetro 1: o segundo forma o par (oito segmentos, o primeiro vira o último, tipo 1).
    // Com o relâmpago com repique no céu os segmentos vão de um ponto ao outro, os do meio sorteados por FractalThing (7
    // pontos, 3 sorteios cada); sem ele ficam escondidos.
    WeatherWorld w;
    level_tag level{};
    level.params[1] = 7;
    _VECTOR top{0.0f, 20.0f, 0.0f, 1.0f}, bottom{4.0f, 0.0f, 0.0f, 1.0f};
    obj_tag* a = Lightning_Create(&top, zero(), &level, nullptr);
    CHECK(a != nullptr && static_cast<LIGHTNING_tag*>(a->data)->seg[0] == nullptr);
    obj_tag* b = Lightning_Create(&bottom, zero(), &level, nullptr);
    CHECK(b != nullptr);
    if (a == nullptr || b == nullptr) return;
    const auto* l = static_cast<const LIGHTNING_tag*>(b->data);
    CHECK(l->seg[0] == b && l->seg[7] == a && a->type == 1 && b->type == 0x33 && (b->flags & 0x8000) != 0);
    bool segs = true;
    for (int k = 1; k < 7; ++k) {
        segs = segs && l->seg[k] != nullptr && l->seg[k]->type == 1 && (l->seg[k]->flags & 0x8000) != 0;
    }
    CHECK(segs && g_weather_stats.pairs == 1 && l->start.x == 4.0f && l->end.y == 20.0f);
    if (!segs) return;
    // Sem relâmpago: escondidos em todas as câmeras.
    Lightning_Update(b);
    CHECK(l->seg[3]->unknown_f8 == 0);
    SuperHappyLightningFlag = 1;
    Reflicker = 1;
    const uint32_t x0 = SEED_X, y0 = SEED_Y;
    Lightning_Update(b);
    CHECK(Reflicker == 0 && rand_calls_between(x0, y0, SEED_X, SEED_Y) == 21);
    bool placed = true;
    for (int k = 1; k < 8; ++k) {
        const _VECTOR& p = l->seg[k]->pos;
        // O meio de cada trecho mais até metade de (a distância dele / 2): dentro da caixa dos dois pontos alargada.
        placed = placed && -15.0f < p.x && p.x < 19.0f && -10.0f < p.y && p.y < 30.0f;
        placed = placed && l->seg[k]->unknown_f8 == 0x1f && (l->seg[k]->mode & 0x21) == 0x21;
    }
    CHECK(placed && l->seg[0]->pos.y == 0.0f);
    // Cada segmento vai até o seguinte (PositionBeam: escala = a distância); o último, até o ponto do par.
    CHECK(std::fabs(l->seg[0]->scale - Vec_Dist3D(&l->seg[0]->pos, &l->seg[1]->pos)) < 1e-4f);
    _VECTOR end = l->end;
    CHECK(std::fabs(l->seg[7]->scale - Vec_Dist3D(&l->seg[7]->pos, &end)) < 1e-4f);
}

TEST(weather_cloud_follows_the_player) {
    // A nuvem: um elemento do céu (camada 1) com o objeto, escondida como objeto; anda a velocidade x FRAME_RATE_MUL,
    // dando a volta, em volta do jogador, na altura dele mais a dela.
    WeatherWorld w;
    level_tag level{};
    level.params[0] = 50;    // altura
    level.params[1] = 1000;  // x 0,001 = 1 m por tick
    level.params[3] = 10;    // a volta
    level.params[4] = 3;     // o elemento do céu
    alignas(16) celglist_tag model{};
    obj_tag* c = Cloud_Create(zero(), zero(), &level, &model);
    CHECK(c != nullptr && c->type == 0x24 && c->unknown_f8 == 0 && GfxList.sky[3].obj == c && GfxList.sky[3].layer == 1);
    if (c == nullptr) return;
    w.player.pos = {1.0f, 2.0f, 3.0f, 1.0f};
    cloud_update(c);
    CHECK(c->pos.x == 2.0f && c->pos.y == 52.0f && c->pos.z == 3.0f);
    for (int t = 0; t < 9; ++t) cloud_update(c);
    CHECK(std::fabs(c->pos.x - 1.0f) < 1e-4f);  // 10 m deram a volta
    std::memset(GfxList.sky, 0, sizeof(GfxList.sky));
}

TEST(scenery_ripples_rotor_leaves_and_one_sided) {
    WeatherWorld w;
    alignas(16) celglist_tag model{};
    model.flags = 0x123;
    level_tag level{};
    // Ondulações sem parâmetros: raio 1 a cada 30 ticks; com raio e sem frequência, ficam assim (um anel por tick).
    obj_tag* r = Ripples_Create(zero(), zero(), &level, &model);
    const auto* rp = static_cast<const RIPPLES_tag*>(r->data);
    CHECK(r->type == 0x34 && rp->radius == 1 && rp->rate == 0x1e && r->flags == 0x123 && r->glist == &model);
    level.params[0] = 3;
    obj_tag* r2 = Ripples_Create(zero(), zero(), &level, &model);
    CHECK(static_cast<const RIPPLES_tag*>(r2->data)->radius == 3 && static_cast<const RIPPLES_tag*>(r2->data)->rate == 0);
    // A hélice gira a rotação do posicionamento por tick enquanto o canal está desligado.
    _VECTOR spin{0.0f, 0.1f, 0.0f, 0.0f};
    obj_tag* h = rotor_init(zero(), &spin, &model, 0, 0, 5);
    CHECK(h->type == 7 && h->rot.y == 0.1f);
    rotor_update(h);
    CHECK(std::fabs(h->rot.y - 0.2f) < 1e-6f && (h->mode & 0x20) != 0);
    switch_channels[5] = 1;
    rotor_update(h);
    CHECK(std::fabs(h->rot.y - 0.2f) < 1e-6f);
    switch_channels[5] = 0;
    // As folhas: vivem 256 ticks a 60 Hz e somem.
    level.params[0] = 0;
    obj_tag* gen = LeafGen_Create(zero(), zero(), &level, &model);
    gen->sphere.w = 2.0f;
    Leaf_Create(gen, 2);
    CHECK(gen->type == 0x31 && count_type(0x2e) == 2 && g_scenery_stats.leaves == 2);
    obj_tag* leaf = nullptr;
    for (obj_tag* o = control_first_object(); o != nullptr && leaf == nullptr; o = o->next) {
        if (o->type == 0x2e) leaf = o;
    }
    CHECK(leaf != nullptr);
    if (leaf == nullptr) return;
    CHECK(static_cast<const LEAF_tag*>(leaf->data)->life == 256 && leaf->unknown_103[3] == 0xff);
    for (int t = 0; t < 256; ++t) Leaf_Update(leaf);
    CHECK((leaf->life & 1) == 0);
    Leaf_Update(leaf);
    CHECK((leaf->life & 1) != 0);
    // A parede de um lado só some com a câmera 0 atrás dela (a frente é a linha 2 da matriz: +z).
    obj_tag* wall = OneSided_Create(zero(), zero(), &model);
    w.viewer.pos = {0.0f, 0.0f, 5.0f, 1.0f};
    OneSided_Update(wall);
    CHECK((wall->flags & 0x10) != 0);
    w.viewer.pos = {0.0f, 0.0f, -5.0f, 1.0f};
    OneSided_Update(wall);
    CHECK((wall->flags & 0x10) == 0);
}

TEST(scenery_breath_timing) {
    // O bafo: numa sala já vista, a cada décimo de segundo no primeiro terço de cada 2 s; nuvem visível com a chuva de
    // partículas no ambiente (tipo 0), senão só no modo de visão 2 (com a flag 0x400).
    WeatherWorld w;
    cel_tag* cel = build_alloc_cel(nullptr);
    cel->misc[0xa0 - 0x98] = 1;
    alignas(16) ENV_tag env{};
    env.kind = 0;
    GlobalEnv = &env;
    _VECTOR mouth{0.0f, 1.6f, 0.0f, 1.0f}, dir{0.0f, 0.0f, 1.0f, 0.0f};
    GameState.frame = 100;
    Env_Breath(&mouth, &dir, cel, 100);  // t = 0
    CHECK(count_type(0xe) == 1 && g_scenery_stats.breaths == 1);
    Env_Breath(&mouth, &dir, cel, 97);  // t = 3: fora do passo de 6
    Env_Breath(&mouth, &dir, cel, 58);  // t = 42: depois do primeiro terço (40)
    CHECK(count_type(0xe) == 1);
    GlobalEnv = nullptr;
    Env_Breath(&mouth, &dir, cel, 100);  // sem ambiente e sem o modo 2: nada
    CHECK(count_type(0xe) == 1);
    w.viewer.mode = 2;
    Env_Breath(&mouth, &dir, cel, 100);
    uint32_t ir = 0;
    for (const obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        ir += o->type == 0xe && (o->flags & 0x400) != 0 ? 1 : 0;
    }
    CHECK(count_type(0xe) == 2 && ir == 1);
    cel->misc[0xa0 - 0x98] = 0;  // sala ainda não vista
    Env_Breath(&mouth, &dir, cel, 100);
    CHECK(count_type(0xe) == 2);
}
