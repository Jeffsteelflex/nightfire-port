// Testes de emitter.cpp (definiÃ§Ãµes do bloco 0x30, nascimento, movimento, vida, cor, rajada, canais e
// desenho) e de psiDrawParticleList, com definiÃ§Ãµes sintÃ©ticas.

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/emitter.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "test_framework.h"

namespace {

constexpr uint32_t kDef = 0x0c000042;

// Escreve um bloco 0x30 com as definiÃ§Ãµes dadas.
struct DefWriter {
    std::vector<uint8_t> bytes;
    void u32(uint32_t v) {
        const size_t at = bytes.size();
        bytes.resize(at + 4);
        std::memcpy(&bytes[at], &v, 4);
    }
    void u16(uint16_t v) {
        const size_t at = bytes.size();
        bytes.resize(at + 2);
        std::memcpy(&bytes[at], &v, 2);
    }
    void f32(float v) {
        const size_t at = bytes.size();
        bytes.resize(at + 4);
        std::memcpy(&bytes[at], &v, 4);
    }
    struct Def {
        uint32_t hash = kDef;
        uint16_t flags = 0;
        uint32_t tex = 0xffffffff, glist = 0xffffffff;
        uint16_t count = 8, rate = 2;
        float size = 0.5f;
        float life = 1.0f, life_rand = 0.0f, gravity = 0.0f, speed = 3.0f, speed_rand = 0.0f;
        float polar = 0.0f, polar_rand = 0.0f, azimuth = 0.0f, azimuth_rand = 0.0f;
        std::vector<KEYED_CLR> keys{{200.0f, 100.0f, 50.0f, 255.0f, 0.0f}, {0.0f, 100.0f, 250.0f, 0.0f, 1.0f}};
    };
    void def(const Def& d) {
        u32(4u | uint32_t(d.flags) << 8);
        u32(d.hash);
        u32(d.tex);
        u32(d.glist);
        u16(d.count);
        u16(d.rate);
        f32(d.size);
        for (float f : {d.life, d.life_rand, d.gravity, d.speed, d.speed_rand, d.polar, d.polar_rand, d.azimuth,
                        d.azimuth_rand}) {
            f32(f);
        }
        u32(uint32_t(d.keys.size()));
        for (const KEYED_CLR& k : d.keys) {
            f32(k.r);
            f32(k.g);
            f32(k.b);
            f32(k.a);
            f32(k.time);
        }
    }
};

struct EmitWorld {
    EmitWorld() {
        psiMemHeapSize = 2u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        object_debug_power_on();
        view_debug_power_on();
        hashtable_debug_power_on();
        hashtable_initialise();
        control_init_object_lists();
        collide_debug_power_on();
        gbl_room_cels = nullptr;
        glb_world = build_alloc_world();
        std::memset(&GameState, 0, sizeof(GameState));
        std::memset(switch_channels, 0, sizeof(switch_channels));
        GS_SetRefreshRate(60, 60);
        psi_draw_debug_power_on();
    }
    // Carrega um bloco com uma definiÃ§Ã£o.
    EMITTERDEF_tag* load(const DefWriter::Def& d) {
        DefWriter w;
        w.u32(1);
        w.def(d);
        block = w.bytes;
        Emitter_LoadDefs(block.data());
        return static_cast<EMITTERDEF_tag*>(hashtable_getitem(d.hash));
    }
    std::vector<uint8_t> block;
};

EMITTER_tag* data_of(obj_tag* o) { return static_cast<EMITTER_tag*>(o->data); }

uint32_t alive(const EMITTER_tag* e) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < e->def->count; ++i) n += e->age[i] < e->life[i] ? 1u : 0u;
    return n;
}

}  // namespace

TEST(colour_get_value_interpolates_between_keys) {
    KEYED_CLR keys[3] = {{0.0f, 10.0f, 100.0f, 255.0f, 0.0f},
                         {100.0f, 30.0f, 0.0f, 55.0f, 0.5f},
                         {50.0f, 30.0f, 0.0f, 0.0f, 1.0f}};
    KEYED_CLR out{};
    // Antes da primeira chave e nela: a primeira chave.
    ColourGetValue(&out, -0.5f, keys, 3);
    CHECK_NEAR(out.r, 0.0, 1e-6);
    CHECK_NEAR(out.a, 255.0, 1e-6);
    // No meio do primeiro trecho (t + 0.001 = 0.25): metade do caminho.
    ColourGetValue(&out, 0.249f, keys, 3);
    CHECK_NEAR(out.r, 50.0, 1e-3);
    CHECK_NEAR(out.g, 20.0, 1e-3);
    CHECK_NEAR(out.b, 50.0, 1e-3);
    CHECK_NEAR(out.a, 155.0, 1e-3);
    // No segundo trecho, a trÃªs quartos (t + 0.001 = 0.875).
    ColourGetValue(&out, 0.874f, keys, 3);
    CHECK_NEAR(out.r, 62.5, 1e-3);
    CHECK_NEAR(out.a, 13.75, 1e-3);
    // Na Ãºltima chave a cor Ã© a dela (o original leria uma chave alÃ©m do fim da tabela).
    ColourGetValue(&out, 0.9995f, keys, 3);
    CHECK_NEAR(out.r, 50.0, 1e-6);
    CHECK_NEAR(out.a, 0.0, 1e-6);
    // Uma chave sÃ³.
    ColourGetValue(&out, 0.3f, keys + 1, 1);
    CHECK_NEAR(out.r, 100.0, 1e-6);
}

TEST(emitter_load_defs_reads_the_particles_block) {
    EmitWorld w;
    DefWriter::Def d;
    d.flags = 1;
    d.count = 12;
    d.rate = 3;
    d.life = 0.75f;
    EMITTERDEF_tag* def = w.load(d);
    CHECK(def != nullptr);
    if (def == nullptr) return;
    CHECK(def->count == 12 && def->rate == 3 && def->flags == 1);
    CHECK(def->texture == nullptr && def->glist == nullptr);
    CHECK_NEAR(def->size, 0.5, 0);
    CHECK_NEAR(def->life, 0.75, 0);
    CHECK_NEAR(def->speed, 3.0, 0);
    CHECK(def->key_count == 2);
    // As chaves ficam logo depois da definiÃ§Ã£o; r, g e b pela metade (escala do GS), alfa igual.
    CHECK(reinterpret_cast<uint8_t*>(def->keys) == reinterpret_cast<uint8_t*>(def) + 0x40);
    CHECK_NEAR(def->keys[0].r, 100.0, 0);
    CHECK_NEAR(def->keys[0].g, 50.0, 0);
    CHECK_NEAR(def->keys[0].b, 25.0, 0);
    CHECK_NEAR(def->keys[0].a, 255.0, 0);
    CHECK_NEAR(def->keys[1].b, 125.0, 0);
    CHECK_NEAR(def->keys[1].time, 1.0, 0);
}

TEST(emitter_load_defs_stops_at_a_record_of_another_type) {
    EmitWorld w;
    DefWriter wr;
    wr.u32(2);
    wr.u32(5);  // tipo errado: nada Ã© lido
    DefWriter::Def d;
    wr.def(d);
    Emitter_LoadDefs(wr.bytes.data());
    CHECK(hashtable_getitem(kDef) == nullptr);
}

TEST(emitter_texture_is_the_record_of_the_texture_item) {
    EmitWorld w;
    pstex_tag rec{};
    rec.tex_index = 77;
    maptex_tag tex{&rec, 16, 16};
    hashtable_additem(0x03000080, &tex);
    celglist_tag model{};
    hashtable_additem(0x020001fe, &model);
    DefWriter::Def d;
    d.tex = 0x03000080;
    d.glist = 0x020001fe;
    EMITTERDEF_tag* def = w.load(d);
    CHECK(def != nullptr && def->texture == &rec && def->glist == &model);
}

TEST(emitter_spawns_at_the_rate_and_moves_along_its_direction) {
    EmitWorld w;
    DefWriter::Def d;
    d.count = 8;
    d.rate = 2;
    w.load(d);
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f};
    _VECTOR dir{0.0f, 0.0f, 1.0f, 0.0f};
    obj_tag* o = Emitter_Create(kDef, &pos, &dir, -1.0f, 0, 0, 0, 0.0f);
    CHECK(o != nullptr);
    if (o == nullptr) return;
    EMITTER_tag* e = data_of(o);
    CHECK(o->type == kEmitterObjectType && (o->mode & 0x104) == 0x104 && (o->flags & 0x21) == 0x21);
    CHECK(alive(e) == 0);
    // Alcance: velocidade mÃ¡xima vezes vida mÃ¡xima.
    CHECK_NEAR(o->sphere.w, 3.0, 1e-6);
    for (int tick = 1; tick <= 4; ++tick) {
        Emitter_Update(o);
        CHECK(alive(e) == uint32_t(2 * tick));
    }
    Emitter_Update(o);
    CHECK(alive(e) == 8);  // sem vagas
    // A primeira partÃ­cula nasceu no tick 1 e andou 4 ticks a 3 m/s ao longo de dir (o eixo y local).
    CHECK_NEAR(e->pos[0].x, 1.0, 1e-5);
    CHECK_NEAR(e->pos[0].y, 2.0, 1e-5);
    CHECK_NEAR(e->pos[0].z, 3.0 + 4.0 * 3.0 / 60.0, 1e-4);
    CHECK_NEAR(e->vel[0].z, 3.0, 1e-5);
    // As que nasceram no tick 4 andaram um tick.
    CHECK_NEAR(e->pos[6].z, 3.0 + 3.0 / 60.0, 1e-4);
}

TEST(emitter_particles_age_fall_and_take_the_key_colour) {
    EmitWorld w;
    DefWriter::Def d;
    d.count = 4;
    d.rate = 4;
    d.life = 0.105f;  // 7 ticks: viva atÃ© a idade 6/60
    d.gravity = 1.0f;
    d.speed = 0.0f;
    w.load(d);
    _VECTOR pos{0.0f, 10.0f, 0.0f, 1.0f};
    _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    obj_tag* o = Emitter_Create(kDef, &pos, &up, -1.0f, 0, 0, 0, 0.0f);
    if (o == nullptr) return;
    EMITTER_tag* e = data_of(o);
    Emitter_Update(o);
    // RecÃ©m-nascida: cor da primeira chave (r, g, b jÃ¡ pela metade).
    CHECK(e->colour[0] == 100 && e->colour[1] == 50 && e->colour[2] == 25 && e->colour[3] == 255);
    for (int k = 0; k < 3; ++k) Emitter_Update(o);
    // TrÃªs ticks vivos: idade 3/60; a cor do instante foi calculada com a idade 2/60 (antes de envelhecer).
    CHECK_NEAR(e->age[0], 3.0 / 60.0, 1e-6);
    const float t = (2.0f / 60.0f) / 0.105f + 0.001f;
    CHECK(int(e->colour[0]) == int(100.0f * (1.0f - t)));
    // Cai: vel.y = -9.8 * gravity * ticks; a posiÃ§Ã£o usa a velocidade do tick anterior.
    CHECK_NEAR(e->vel[0].y, -9.8 * 3.0 / 60.0, 1e-4);
    CHECK_NEAR(e->pos[0].y, 10.0 - 9.8 * (0.0 + 1.0 + 2.0) / 3600.0, 1e-4);
    for (int k = 0; k < 3; ++k) Emitter_Update(o);
    CHECK(alive(e) == 4);
    CHECK_NEAR(e->age[0], 6.0 / 60.0, 1e-5);
    // No oitavo tick passam da vida (7/60 > 0.105) e, como o emissor Ã© contÃ­nuo, renascem no mesmo
    // tick: idade 0, de volta Ã  posiÃ§Ã£o do emissor, com a cor da primeira chave.
    Emitter_Update(o);
    CHECK(alive(e) == 4);
    CHECK_NEAR(e->age[0], 0.0, 0);
    CHECK_NEAR(e->pos[0].y, 10.0, 1e-6);
    CHECK(e->colour[0] == 100 && e->colour[3] == 255);
}

TEST(emitter_burst_spawns_once_and_removes_itself) {
    EmitWorld w;
    DefWriter::Def d;
    d.count = 4;
    d.rate = 4;
    d.life = 0.04f;
    w.load(d);
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
    _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    obj_tag* o = Emitter_Create(kDef, &pos, &up, 0.0f, 0, 0, 0, 0.0f);
    if (o == nullptr) return;
    EMITTER_tag* e = data_of(o);
    CHECK(e->one_shot == 1);
    Emitter_Update(o);  // nascem as quatro
    CHECK(alive(e) == 4 && e->done == 0);
    Emitter_Update(o);
    Emitter_Update(o);
    CHECK(alive(e) == 4 && e->dying == 0);
    Emitter_Update(o);  // tick 4: idade 3/60 > 0.04; numa rajada nÃ£o renascem
    CHECK(alive(e) == 0 && e->done == 1 && e->dying == 1);
    CHECK((o->life & 1) == 0);
    Emitter_Update(o);
    CHECK(e->dying == 2 && (o->life & 1) == 0);
    Emitter_Update(o);
    CHECK(e->dying == 3 && (o->life & 1) == 1);
    // Terminado: nada a desenhar.
    psiPreDraw();
    Emitter_Draw(o, nullptr);
    CHECK(g_psi_frame.draws.empty());
}

TEST(emitter_duration_period_and_switch_channels) {
    EmitWorld w;
    DefWriter::Def d;
    d.count = 64;
    d.rate = 1;
    d.life = 100.0f;
    w.load(d);
    _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
    _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    // Liga pelo canal 5 e marca o canal 7 enquanto houver partÃ­cula viva.
    obj_tag* o = Emitter_Create(kDef, &pos, &up, -1.0f, 5, 0, 7, 0.0f);
    if (o == nullptr) return;
    EMITTER_tag* e = data_of(o);
    Emitter_Update(o);
    CHECK(alive(e) == 0 && switch_channels[7] == 0);
    switch_channels[5] = 1;
    GameState.frame = 1234;
    Emitter_Update(o);
    CHECK(alive(e) == 1);
    Emitter_Update(o);
    CHECK(alive(e) == 2 && switch_channels[7] == 1 && switch_channels_time[7] == 1234);

    // Pisca de 0.2 s: emite 0.1 s (ligado = metade do perÃ­odo) e para 0.2 s.
    w = EmitWorld();
    w.load(d);
    o = Emitter_Create(kDef, &pos, &up, -1.0f, 0, 0, 0, 0.2f);
    e = data_of(o);
    bool born[40]{};
    for (int tick = 1; tick < 40; ++tick) {
        const uint32_t before = alive(e);
        Emitter_Update(o);
        born[tick] = alive(e) != before;
    }
    int first_off = 1;
    while (first_off < 40 && born[first_off]) ++first_off;
    CHECK(first_off >= 6 && first_off <= 7);  // cerca de 0.1 s emitindo
    bool gap = true;
    for (int k = first_off; k < first_off + 11; ++k) gap = gap && !born[k];
    CHECK(gap);  // cerca de 0.2 s parado
    bool resumed = false;
    for (int k = first_off + 11; k < first_off + 15; ++k) resumed = resumed || born[k];
    CHECK(resumed);

    // DuraÃ§Ã£o de 3 ticks: depois vira rajada (nÃ£o renasce) e nÃ£o emite mais nada alÃ©m das vagas.
    w = EmitWorld();
    d.life = 0.02f;
    w.load(d);
    o = Emitter_Create(kDef, &pos, &up, 3.0f / 60.0f + 0.001f, 0, 0, 0, 0.0f);
    e = data_of(o);
    for (int k = 0; k < 3; ++k) Emitter_Update(o);
    CHECK(e->one_shot == 0);
    Emitter_Update(o);
    CHECK(e->one_shot == 1);
}

TEST(emitter_create_plist_reads_the_placement_params) {
    EmitWorld w;
    DefWriter::Def d;
    w.load(d);
    MATRIX m{};
    Mat_Identity(&m);
    m.m[3][0] = 4.0f;
    m.m[3][1] = 5.0f;
    m.m[3][2] = 6.0f;
    m.m[3][3] = 1.0f;
    level_tag level{};
    level.params[0] = kDef;
    level.params[1] = 120;  // 2 s
    level.params[2] = 3;
    level.params[3] = 4;
    level.params[4] = 9;
    level.params[5] = 30;  // pisca de 0.5 s
    const uint32_t before = g_emitters_created;
    Emitter_CreatePlist(&m, &level);
    level.params[1] = 0;  // sempre
    Emitter_CreatePlist(&m, &level);
    level.params[1] = 1;  // rajada
    Emitter_CreatePlist(&m, &level);
    CHECK(g_emitters_created == before + 3);
    int timed = 0, forever = 0, burst = 0;
    for (obj_tag* o = Control_ReturnNextObjectOfType(kEmitterObjectType, nullptr); o != nullptr;
         o = Control_ReturnNextObjectOfType(kEmitterObjectType, o)) {
        const EMITTER_tag* e = data_of(o);
        CHECK_NEAR(e->period, 0.5, 1e-6);
        CHECK(e->switch_on == 3 && e->switch_off == 4 && e->switch_alive == 9);
        CHECK_NEAR(o->matrix.m[3][0], 4.0, 0);
        CHECK_NEAR(o->matrix.m[1][1], 1.0, 1e-6);  // emite para cima (linha 1 de m)
        if (std::fabs(e->duration - 2.0f) < 1e-5f && e->one_shot == 0) ++timed;
        if (e->duration < 0.0f && e->one_shot == 0) ++forever;
        if (e->duration == 0.0f && e->one_shot == 1) ++burst;
    }
    CHECK(timed == 1 && forever == 1 && burst == 1);
}

TEST(emitter_draw_records_a_sprite_list) {
    EmitWorld w;
    DefWriter::Def d;
    d.count = 10;  // desenha 8 (mÃºltiplo de 4)
    d.rate = 10;
    d.size = 1.0f;
    w.load(d);
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f};
    _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    obj_tag* o = Emitter_Create(kDef, &pos, &up, -1.0f, 0, 0, 0, 0.0f);
    if (o == nullptr) return;
    Emitter_Update(o);
    Emitter_Update(o);
    psiPreDraw();
    Emitter_Draw(o, nullptr);
    CHECK(g_psi_frame.particle_lists.size() == 1 && g_psi_frame.draws.size() == 1);
    if (g_psi_frame.particle_lists.empty() || g_psi_frame.draws.empty()) return;
    const PsiParticleList& l = g_psi_frame.particle_lists[0];
    const PsiDrawCall& c = g_psi_frame.draws[0];
    CHECK(l.count == 8 && l.texture == -1 && c.particles == 0 && c.glist == nullptr);
    CHECK(c.blend == 0);  // flags 0: aditiva
    CHECK((c.gs_has & 6) == 6 && c.gs_test == 0x5000d && ((c.gs_zbuf >> 32) & 1) == 1);
    CHECK_NEAR(l.half_size, psi_particle_half_size(1.0f, true), 1e-6);
    const EMITTER_tag* e = data_of(o);
    for (uint32_t i = 0; i < l.count; ++i) {
        const PsiParticle& p = g_psi_frame.particles[l.first + i];
        CHECK(p.pos[0] == e->pos[i].x && p.pos[1] == e->pos[i].y && p.pos[2] == e->pos[i].z);
        CHECK(std::memcmp(p.rgba, e->colour + i * 4, 4) == 0);
    }
    // Nada a desenhar com o emissor terminado.
    data_of(o)->done = 1;
    psiPreDraw();
    Emitter_Draw(o, nullptr);
    CHECK(g_psi_frame.draws.empty());
}

TEST(emitter_draw_with_a_model_tints_each_live_particle) {
    EmitWorld w;
    uint8_t ps2_gfx[16]{};
    entgfx_tag gfx{};
    gfx.ps2_gfx = ps2_gfx;
    celglist_tag model{};
    model.gfx = &gfx;
    hashtable_additem(0x02000500, &model);
    DefWriter::Def d;
    d.glist = 0x02000500;
    d.count = 3;
    d.rate = 2;
    w.load(d);
    _VECTOR pos{1.0f, 2.0f, 3.0f, 1.0f};
    _VECTOR up{0.0f, 1.0f, 0.0f, 0.0f};
    obj_tag* o = Emitter_Create(kDef, &pos, &up, -1.0f, 0, 0, 0, 0.0f);
    if (o == nullptr) return;
    Emitter_Update(o);  // duas vivas, a terceira com alfa 0
    psiPreDraw();
    Emitter_Draw(o, nullptr);
    CHECK(g_psi_frame.draws.size() == 2);
    for (const PsiDrawCall& c : g_psi_frame.draws) {
        CHECK(c.glist == &model);
        CHECK(c.tweak[0] == 255 && c.tweak[1] == 100 && c.tweak[2] == 50 && c.tweak[3] == 25);
        CHECK_NEAR(c.world.m[3][0], 1.0, 1e-6);
        CHECK_NEAR(c.world.m[3][1], 2.0, 1e-6);
    }
    // A rotaÃ§Ã£o cresce REC_FRAME_RATE em x e y a cada modelo desenhado.
    CHECK_NEAR(data_of(o)->rot.x, 2.0 / 60.0, 1e-6);
    CHECK_NEAR(data_of(o)->rot.y, 2.0 / 60.0, 1e-6);
    CHECK_NEAR(data_of(o)->rot.z, 0.0, 0);
}

TEST(psi_draw_particle_list_records_what_it_is_given) {
    psi_draw_debug_power_on();
    psiPreDraw();
    _VECTOR4 pos[7];
    for (int i = 0; i < 7; ++i) pos[i] = {float(i), float(2 * i), float(3 * i), 1.0f};
    pstex_tag tex{};
    tex.tex_index = 12;
    psiDrawParticleList(pos, 7, 0xffffffff, uint32_t(reinterpret_cast<uintptr_t>(&tex)), nullptr, 0, 3.0f, nullptr);
    CHECK(g_psi_frame.particle_lists.size() == 1);
    if (g_psi_frame.particle_lists.empty()) return;
    const PsiParticleList& l = g_psi_frame.particle_lists[0];
    CHECK(l.count == 4 && l.texture == 12);
    // Sem lista de cores: a cor fixa do microprograma e o tamanho da neve (nÃ£o depende de size).
    const PsiParticle& p = g_psi_frame.particles[3];
    CHECK(p.pos[0] == 3.0f && p.pos[1] == 6.0f && p.pos[2] == 9.0f);
    CHECK(p.rgba[0] == 0x3f && p.rgba[1] == 0x3f && p.rgba[2] == 0x3f && p.rgba[3] == 0x7f);
    CHECK_NEAR(l.half_size, psi_particle_half_size(0.0f, false), 1e-6);
    // TransparÃªncia (additive = 0) e a mistura fica em vigor para os prÃ³ximos desenhos.
    CHECK(g_psi_frame.draws[0].blend == 1);
    // Menos de 4 partÃ­culas: nada.
    psiDrawParticleList(pos, 3, 0, 0, nullptr, 1, 1.0f, nullptr);
    CHECK(g_psi_frame.particle_lists.size() == 2 && g_psi_frame.particle_lists[1].count == 0);
    CHECK(g_psi_frame.particle_lists[1].texture == -1 && g_psi_frame.draws[1].blend == 0);
}

TEST(psi_particle_half_size_matches_the_ps2_projection) {
    // Com cores: o sprite tem (Ps2Near2 / 0.002386635) * 4 * size / z pixels de meia altura numa
    // tela de 448 linhas, o tamanho de um objeto de 0.432 * size m a 60 graus.
    CHECK_NEAR(psi_particle_half_size(1.0f, true), 0.432, 0.001);
    CHECK_NEAR(psi_particle_half_size(0.25f, true), 0.108, 0.001);
    // Sem cores (neve): 1 / PS2SnowSize por Z, cerca de 10.8 cm com os valores padrÃ£o.
    CHECK_NEAR(psi_particle_half_size(123.0f, false), 0.108, 0.001);
}
