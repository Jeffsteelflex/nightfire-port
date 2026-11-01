// O modelo próprio dos objetos de cena do mundo (driving/scene_article.h; docs/driving.md seção 34): propriedades
// independentes sobre grupos, artigos e instâncias montados à mão (a variante achada pela etiqueta curta, a lista de
// instâncias virando um Handle com o 'pD', o nome, a variante e as bandeiras; a instância única com a identidade; as
// cópias desenhadas pela matriz do objeto; o gProcAnimFunc 0xf2 girando em y). A comparação com o código original fica
// na parte 29 do nf_driving_check.

#include <cmath>
#include <cstring>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/light_manager.h"
#include "driving/render/anim_engine.h"
#include "driving/render/vehicle.h"
#include "driving/scene_article.h"
#include "driving/world_vis.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// Um grupo sem subgrupos com as entradas dadas (endereços absolutos; as etiquetas em ordem crescente, como o CARP as
// guarda, para a busca binária do DataLocateFirst).
struct Entry {
    uint32_t tag, count, data;
};
struct Group {
    alignas(16) UData head{};
    alignas(16) UData items[8]{};
    void build(const std::vector<Entry>& es) {
        head = UData{0x41727469u, 0u, uint32_t(es.size()), nf::p32_u(items)};
        // O bit 0: a etiqueta curta (duas letras e um índice: 'in#0002').
        for (size_t i = 0; i < es.size(); ++i)
            items[i] = UData{es[i].tag, 0x100u | ((es[i].tag & 0xffff) < 0x1000 ? 1u : 0u), es[i].count, es[i].data};
    }
};

// Uma instância (CARP::Instance, 64 bytes): a posição, as bandeiras, a função e o artigo.
void make_instance(uint8_t* p, float x, float y, float z, uint8_t flags, uint8_t fn, uint32_t article) {
    std::memset(p, 0, 64);
    const float rows[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {x, y, z}};
    for (int r = 0; r < 4; ++r) std::memcpy(p + r * 16, rows[r], 12);
    p[0xc] = flags;
    p[0xd] = fn;
    std::memcpy(p + 0x1c, &article, 4);
    const uint32_t dims = 4;  // um raio de 1 m (4 x 0,25)
    std::memcpy(p + 0x3c, &dims, 4);
}

// Um artigo (0x28 bytes) com o 'pD', os efeitos, as animações e a passada.
struct Article {
    alignas(16) uint8_t bytes[0x30]{};
    void set(uint32_t desc, uint32_t effects, uint32_t anims, uint8_t pass) {
        std::memset(bytes, 0, sizeof bytes);
        std::memcpy(bytes + 0, &desc, 4);
        std::memcpy(bytes + 4, &effects, 4);
        std::memcpy(bytes + 8, &anims, 4);
        bytes[0x1d] = pass;
    }
    uint32_t addr() { return nf::p32_u(bytes); }
};

SceneArticleEnv g_saved;
int g_toggles = 0, g_roots = 0;
uint32_t g_toggle_index = 0xffffffffu;

struct EnvGuard {
    EnvGuard() {
        g_saved = scene_article_env();
        g_toggles = g_roots = 0;
        g_toggle_index = 0xffffffffu;
        scene_article_env().purge = nullptr;
        scene_article_env().toggle_effect = [](uint32_t, uint32_t index, uint32_t) {
            ++g_toggles;
            g_toggle_index = index;
        };
        scene_article_env().set_root_node = [](RSceneObj&) { ++g_roots; };
        anim_engine_reset(60.0f);
    }
    ~EnvGuard() {
        scene_article_env() = g_saved;
        anim_engine_reset(60.0f);
    }
};

}  // namespace

TEST(driving_scene_article_locate_variant) {
    alignas(16) static uint8_t d[4][16];
    static Group g;
    g.build({{0x4e616d65u, 1, nf::p32_u(d[0])},
             {0x696e0000u, 1, nf::p32_u(d[1])},
             {0x696e0002u, 1, nf::p32_u(d[2])},
             {0x70730002u, 1, nf::p32_u(d[3])}});
    const UGroup* grp = &g.head;
    // A variante pedida; sem ela, a 0 (só se a pedida não era a 0); 0xffffffff: a etiqueta inteira.
    CHECK(RSceneObj_LocateVariant(grp, 0x696e2020u, 2) == &g.items[2]);
    CHECK(RSceneObj_LocateVariant(grp, 0x696e2020u, 1) == &g.items[1]);
    CHECK(RSceneObj_LocateVariant(grp, 0x696e2020u, 0) == &g.items[1]);
    CHECK(RSceneObj_LocateVariant(grp, 0x70732020u, 2) == &g.items[3]);
    CHECK(RSceneObj_LocateVariant(grp, 0x70732020u, 1) == nullptr);
    CHECK(RSceneObj_LocateVariant(grp, 0x70732020u, 0) == nullptr);
    CHECK(RSceneObj_LocateVariant(grp, 0x4e616d65u, 0xffffffffu) == &g.items[0]);
}

TEST(driving_scene_article_instance_list) {
    EnvGuard env;
    // O artigo das instâncias: uma animação (o estímulo 8 do estado 1 ao 2, como a rampa do guindaste) e um efeito que
    // nasce ligado (a bandeira 0x10).
    alignas(16) static uint8_t keys[2][0x20];
    alignas(16) static CarpAnimInfo anims[2];
    alignas(16) static uint8_t effects[2][0x40];
    std::memset(keys, 0, sizeof keys);
    std::memset(anims, 0, sizeof anims);
    std::memset(effects, 0, sizeof effects);
    anims[0] = CarpAnimInfo{nf::p32_u(keys), 0, 0, 2, 0, 8, 1, 2, 1};
    const uint32_t eflags = 0x10;
    std::memcpy(effects[0] + 0x10, &eflags, 4);
    effects[0][0x14] = 8;  // o tipo (nada a desenhar); o segundo, 0, fecha a lista
    alignas(16) static uint32_t desc[24];
    std::memset(desc, 0, sizeof desc);
    static Article sub;
    sub.set(nf::p32_u(desc), nf::p32_u(effects), nf::p32_u(anims), 2);
    alignas(16) static uint8_t list[2][64];
    make_instance(list[0], 1, 2, 3, 0x01, 0xff, sub.addr());
    make_instance(list[1], -1, 0, 0, 0x40, 0xff, sub.addr());
    static const char name[] = "crane_ramp";
    static Group g;
    g.build({{0x4e616d65u, 11, nf::p32_u(name)}, {0x696e0002u, 2, nf::p32_u(list)}});
    // A instância do mundo (a bandeira 8: o nó da raiz) e o objeto como o construtor o deixa.
    alignas(16) static uint8_t world[64];
    make_instance(world, 10, 0, 5, 0x18, 0xff, sub.addr());
    alignas(16) static RSceneObj o;
    o = RSceneObj{};
    o.instance = nf::p32_u(world);
    o.flags = 0x01640000u;
    CHECK(RSceneObj_UseArticle(o, &g.head, 2));
    CHECK(o.article != 0);
    const AnimHandleData& h = *nf::p32_ptr<const AnimHandleData>(o.article);
    CHECK(h.n_instances == 2 && h.n_systems == 1 && h.n_effects == 2);  // o efeito do artigo em cada instância
    CHECK(o.unknown0c == nf::p32_u(desc));             // o 'pD' do artigo da primeira instância
    CHECK(o.unknown30[2] == nf::p32_u(name));         // o nome
    CHECK(((o.flags >> 8) & 0xff) == 2);              // a variante
    CHECK((o.flags & 0x02000000u) != 0);              // as cópias têm 0x01 ou 0x40
    CHECK((o.flags & 0x04000000u) != 0 && g_roots == 1);  // a bandeira 8 da instância do mundo
    CHECK(g_toggles == 2 && g_toggle_index == 1);     // o efeito que nasce ligado, nas duas cópias
    CHECK(AnimHandle_GetSystemState(o.article, 0) == 1);
    // O estímulo 8 toca a animação (a rampa desce).
    AnimHandle_ProcessStimuliAll(o.article, 8, 0, 2);
    CHECK(AnimHandle_IsSystemPlaying(o.article, 0));

    // Uma cópia sem 0x41: o bit 25 cai; sem a bandeira 8 nada vai à raiz; sem 'Name', o "<<unknown>>".
    make_instance(list[1], -1, 0, 0, 0x00, 0xff, sub.addr());
    world[0xc] = 0x10;
    g.build({{0x696e0000u, 2, nf::p32_u(list)}});
    alignas(16) static RSceneObj o2;
    o2 = RSceneObj{};
    o2.instance = nf::p32_u(world);
    o2.flags = 0x01640000u;
    CHECK(RSceneObj_UseArticle(o2, &g.head, 5));  // a variante 5 não existe: a 0
    CHECK((o2.flags & 0x02000000u) == 0);
    CHECK((o2.flags & 0x04000000u) == 0 && g_roots == 1);
    CHECK(std::strcmp(nf::p32_ptr<const char>(o2.unknown30[2]), "<<unknown>>") == 0);
}

TEST(driving_scene_article_single_instance) {
    EnvGuard env;
    alignas(16) static uint32_t desc[24];
    static Article art, as_art;
    art.set(nf::p32_u(desc), 0, 0, 2);
    as_art.set(nf::p32_u(desc), 0, 0, 1);
    static Group g;
    g.build({});
    // Sem 'in': a própria instância do mundo, com a rotação e a posição da identidade, a função 0xff e as bandeiras 0x11
    // apagadas.
    alignas(16) static uint8_t world[64];
    make_instance(world, 7, 8, 9, 0x31, 0x05, art.addr());
    alignas(16) static RSceneObj o;
    o = RSceneObj{};
    o.instance = nf::p32_u(world);
    CHECK(RSceneObj_UseArticle(o, &g.head, 0));
    const uint8_t* copy = nf::p32_ptr<const uint8_t>(o.article + 0x40);
    float m[4][4];
    std::memcpy(m, copy, 64);
    CHECK(m[0][0] == 1 && m[1][1] == 1 && m[2][2] == 1 && m[3][0] == 0 && m[3][1] == 0 && m[3][2] == 0);
    CHECK(copy[0xc] == 0x20 && copy[0xd] == 0xff);
    uint32_t a;
    std::memcpy(&a, copy + 0x1c, 4);
    CHECK(a == art.addr());
    // Sem artigo: o primeiro 'as  ' do grupo; sem ele, nada (nenhum Handle).
    make_instance(world, 7, 8, 9, 0x10, 0xff, 0);
    g.build({{0x61730000u, 1, as_art.addr()}});
    alignas(16) static RSceneObj o2;
    o2 = RSceneObj{};
    o2.instance = nf::p32_u(world);
    CHECK(RSceneObj_UseArticle(o2, &g.head, 0));
    std::memcpy(&a, nf::p32_ptr<const uint8_t>(o2.article + 0x40) + 0x1c, 4);
    CHECK(a == as_art.addr());
    g.build({});
    alignas(16) static RSceneObj o3;
    o3 = RSceneObj{};
    o3.instance = nf::p32_u(world);
    CHECK(!RSceneObj_UseArticle(o3, &g.head, 0));
    CHECK(o3.article == 0);
}

TEST(driving_scene_article_render_copies) {
    EnvGuard env;
    alignas(16) static uint32_t desc[24];
    static Article art;
    art.set(nf::p32_u(desc), 0, 0, 2);
    alignas(16) static uint8_t list[2][64];
    make_instance(list[0], 1, 2, 3, 0x00, 0xff, art.addr());
    make_instance(list[1], 4, 4, 4, 0x01, 0xff, art.addr());  // escondida: o DrawInstance a pula
    static Group g;
    g.build({{0x696e0000u, 2, nf::p32_u(list)}});
    // O objeto girado 90 graus em y e posto em (10, 0, 5): a cópia em (1, 2, 3) vai para (10 + 3, 2, 5 - 1).
    alignas(16) static uint8_t world[64];
    make_instance(world, 10, 0, 5, 0x10, 0xff, art.addr());
    const float rows[3][3] = {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
    for (int r = 0; r < 3; ++r) std::memcpy(world + r * 16, rows[r], 12);
    alignas(16) static RSceneObj o;
    o = RSceneObj{};
    o.instance = nf::p32_u(world);
    CHECK(RSceneObj_UseArticle(o, &g.head, 0));
    std::vector<SceneObjDraw> d;
    RSceneObj_Render(o, d);
    CHECK(d.size() == 1);
    if (d.size() == 1) {
        CHECK(d[0].article == art.addr() && d[0].pass == 2);
        CHECK_NEAR(d[0].world[3][0], 13.0f, 1e-5f);
        CHECK_NEAR(d[0].world[3][1], 2.0f, 1e-5f);
        CHECK_NEAR(d[0].world[3][2], 4.0f, 1e-5f);
        CHECK_NEAR(d[0].world[0][2], -1.0f, 1e-6f);
    }
    // Sem Handle, nada.
    alignas(16) static RSceneObj none;
    none = RSceneObj{};
    d.clear();
    RSceneObj_Render(none, d);
    CHECK(d.empty());
}

TEST(driving_proc_anim_spin_f2) {
    // O gProcAnimFunc 0xf2: o giro em y de (velocidade / 256) x fTimeData.x graus, a velocidade no s16 +0xe (ou, com a
    // bandeira 0x10, no s16 +2 do estado); a posição fica.
    const RTimeData saved = rtime_data();
    rtime_data().time_data[0] = 2.0f;
    alignas(16) uint8_t inst[64];
    make_instance(inst, 5, 6, 7, 0x00, 0xf2, 0);
    const int16_t speed = 45 * 256;  // 45 graus por segundo: 90 graus em 2 s
    std::memcpy(inst + 0xe, &speed, 2);
    uint32_t raw[16];
    std::memcpy(raw, inst, 64);
    ProcAnimContext ctx;
    float out[4][4];
    CHECK(GetInstanceMatrix(raw, ctx, out));
    // O eixo x do objeto gira para -z (90 graus em y), o z para +x; a posição e o y ficam.
    CHECK_NEAR(out[0][0], 0.0f, 1e-5f);
    CHECK_NEAR(out[0][2], -1.0f, 1e-5f);
    CHECK_NEAR(out[2][0], 1.0f, 1e-5f);
    CHECK_NEAR(out[1][1], 1.0f, 1e-6f);
    CHECK(out[3][0] == 5.0f && out[3][1] == 6.0f && out[3][2] == 7.0f && out[3][3] == 1.0f);
    // Com a bandeira 0x10: a velocidade do estado (o u16 +0xe é o índice dele); 0 não gira.
    alignas(16) uint8_t states[2][0x20] = {};
    const int16_t zero = 0;
    std::memcpy(states[1] + 2, &zero, 2);
    inst[0xc] = 0x10;
    const uint16_t idx = 1;
    std::memcpy(inst + 0xe, &idx, 2);
    std::memcpy(raw, inst, 64);
    ctx.states = &states[0][0];
    CHECK(GetInstanceMatrix(raw, ctx, out));
    CHECK(out[0][0] == 1.0f && out[0][2] == 0.0f);
    rtime_data() = saved;
}
