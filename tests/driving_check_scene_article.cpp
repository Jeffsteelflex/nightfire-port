// nf_driving_check, parte 29 (tests/driving_check_scene_article.h; docs/driving.md seção 34): o modelo próprio dos objetos
// de cena de Paris. O CARP de Paris (carregado e resolvido pelo porte, com os objetos de cena do WWorld::Open) vai para a
// RAM emulada com as palavras que o código segue trocadas pelos endereços emulados (a instância do mundo e o artigo dela,
// o 'pD' e o grupo do modelo, o vetor do grupo e os dados de cada entrada, as instâncias do 'in' e os artigos delas, as
// animações, as chaves e as listas 'el'). Em cada caso, um objeto de cena de Paris (os 557, mais vezes os que têm lista
// de instâncias ou animações), a variante (a do 'ps  ' do jogo ou sorteada), a bandeira 8 da instância e o tempo
// sorteados, e um objeto novo dos dois lados com as bandeiras sorteadas:
//  a) o RSceneObj::UseArticle (0x001c7658) ORIGINAL, com o 0x001c6fb8, o UseInstanceList (0x001c7940), o Close, o
//     RAnimEngine::Handle::Create, o InitEffects (0x001ca048, com o CalcDamageZone), e às vezes o UseInstanceList direto
//     (a lista do grupo ou a instância do mundo, o argumento do InitEffects sorteado), contra o porte: o objeto, o bloco
//     do Handle, os identificadores gravados nos artigos, a lista do motor, as chamadas que saem (o KillEffects, o
//     ToggleEffect, o SetMapNode da raiz, o ComputeBoundingRadius);
//  b) depois, operações sorteadas no Handle (os estímulos em todos os sistemas, o 8 da rampa do guindaste mais vezes, e
//     o RAnimEngine::Update com o tempo andando: os eventos das chaves, como o ESetCollisionGeometry e o EPlayEffect da
//     rampa) e o RSceneObj::Render (0x001c85f8) ORIGINAL com o DrawInstanceList e o DrawInstance (o GetInstanceMatrix pelo
//     gProcAnimFunc, o EvaluateInstance e o produto pela VU0): cada cópia desenhada (o artigo e a matriz, agora ou na
//     fila da passada) contra o RSceneObj_Render do porte.
// Fora do código original (ganchos nativos): a memória (UMemory::FastAlloc, MEM_fill, MEM_copy, __builtin_delete), as
// criadoras dos eventos (a LookupEvent anota o número e os dados), o que o desenho chama (GetArticleViewDistance devolve
// uma distância, o DrawArticleModel, o DrawGroupDrawEffects, a fila das passadas e o RenderEffects anotam), o sinf e o
// cosf do newlib (os do port: a divergência do BuildRotate, como na parte 12) e o que o
// UseArticle chama fora do objeto (o GFXGallery::CANVAS_PurgeSceneObjEffects do KillEffects, o ToggleEffect, o
// SetMapNode, o ComputeBoundingRadius e o SetDimensions anotam).

#include "driving_check_scene_article.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/carp_runtime.h"
#include "driving/game/events.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/render/anim_engine.h"
#include "driving/scene_article.h"
#include "driving/track.h"
#include "driving/world_vis.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

constexpr uint32_t kFnUseArticle = 0x1c7658, kFnUseList = 0x1c7940, kFnRender = 0x1c85f8;
constexpr uint32_t kFnStimuliAll = 0x1a2868, kFnUpdate = 0x1a1a10;
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnMemFill = 0x260300, kFnMemCopy = 0x2601f0, kFnLookupEvent = 0x1757b8;
constexpr uint32_t kFnDelete = 0x2d09a8, kFnPurge = 0x232dc8, kFnToggle = 0x1ca198, kFnSetMapNode = 0x1c8cf8;
constexpr uint32_t kFnRadius = 0x1c98a0, kFnSetDims = 0x2dac58;
constexpr uint32_t kFnViewDist = 0x1ad0b8, kFnDrawModel = 0x1ad2e0, kFnDrawEffects = 0x1ad1a0, kFnQueue = 0x1ab050;
constexpr uint32_t kFnRenderEffects = 0x1ca930;

constexpr uint32_t kCount = 0x333600, kLastTick = 0x333604, kList = 0x3d1518, kFrequency = 0x333ff4;
constexpr uint32_t kEventData = 0x32e708, kTime = 0x334010, kTimeData = 0x334000, kReflection = 0x333e68, kQueueHolder = 0x333760;
constexpr uint32_t kUnknownName = 0x386a30, kSceneVtable = 0x386a88;

constexpr uint32_t kScene = 0x00900000, kFakeRefl = 0x00900100, kFakeQueue = 0x00900200, kCreatorStub = 0x00901000;
constexpr uint32_t kWorld = 0x33aac8, kFakeWorld = 0x00900300, kFakeWmap = 0x00900400, kFakeRoot = 0x00900500;
constexpr uint32_t kHeap = 0x00a00000, kHeapEnd = 0x00e00000;
constexpr uint32_t kCarp = 0x00e00000, kCarpEnd = 0x01f00000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2929) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

uint32_t rd(uint32_t p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, 4);
    return v;
}

// Um desenho anotado do Render original: o artigo, a matriz e, na fila, a cópia.
struct EmuDraw {
    bool queued = false;
    uint32_t article = 0, copy = 0;
    uint8_t m[64] = {};
};

struct Ctx {
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    int32_t pending_type = -1;
    struct Event {
        int32_t type;
        uint32_t data;
    };
    std::vector<Event> events;
    // O que sai do UseArticle e do Render.
    uint32_t purges = 0, radius = 0;
    std::vector<uint32_t> toggles;     // o índice e a cópia do efeito (relativa ao Handle) em pares
    std::vector<uint32_t> map_nodes;   // o nó do SetMapNode
    std::vector<EmuDraw> draws;
    uint32_t effects_calls = 0, render_effects = 0;
    std::string error;
    uint64_t errors = 0;

    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
        }
    }
    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(kFnFastAlloc, "UMemory::FastAlloc",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(kFnMemFill, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(d, n), int(mm.gpr(R::a1) & 0xff), n);
        });
        m.hook(kFnMemCopy, "MEM_copy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), s = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(d, n), mm.mem(s, n), n);
        });
        m.hook(kFnDelete, "__builtin_delete", [](MipsMachine&) {});
        // O sinf/cosf do newlib (o BuildRotate do gProcAnimFunc 0xf2): os do port, como na parte 12 (a divergência do
        // BuildRotate, driving/light_manager.cpp).
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(kFnLookupEvent, "RegisterEvent::LookupEvent", [this](MipsMachine& mm) {
            const int32_t t = int32_t(mm.gpr(R::a0));
            pending_type = t;
            mm.set_gpr(R::v0, drv::event_has_creator(t) ? kCreatorStub : 0);
        });
        m.hook(kCreatorStub, "a criadora", [this](MipsMachine& mm) {
            events.push_back({pending_type, uint32_t(mm.gpr(R::a0))});
        });
        m.hook(kFnPurge, "GFX (o KillEffects: CANVAS_PurgeSceneObjEffects)", [this](MipsMachine&) { ++purges; });
        m.hook(kFnToggle, "RSceneObj::ToggleEffect", [this](MipsMachine& mm) {
            toggles.push_back(uint32_t(mm.gpr(R::a2)));
            toggles.push_back(uint32_t(mm.gpr(R::a3)));
        });
        m.hook(kFnSetMapNode, "RSceneObj::SetMapNode", [this](MipsMachine& mm) {
            map_nodes.push_back(uint32_t(mm.gpr(R::a1)));
        });
        m.hook(kFnRadius, "RSceneObj::ComputeBoundingRadius", [this](MipsMachine& mm) {
            ++radius;
            mm.set_fpr(0, 1.0f);
        });
        m.hook(kFnSetDims, "CARP::Instance::SetDimensions", [](MipsMachine&) {});
        m.hook(kFnViewDist, "GetArticleViewDistance", [](MipsMachine& mm) { mm.set_fpr(0, 5.0f); });
        m.hook(kFnDrawModel, "DrawArticleModel", [this](MipsMachine& mm) {
            EmuDraw d;
            d.article = uint32_t(mm.gpr(R::a0));
            std::memcpy(d.m, mm.mem(uint32_t(mm.gpr(R::a1)), 64), 64);
            draws.push_back(d);
        });
        m.hook(kFnDrawEffects, "DrawGroupDrawEffects", [this](MipsMachine&) { ++effects_calls; });
        m.hook(kFnQueue, "a fila da passada (0x1ab050)", [this](MipsMachine& mm) {
            const uint32_t a = uint32_t(mm.gpr(R::a1));
            EmuDraw d;
            d.queued = true;
            std::memcpy(d.m, mm.mem(a, 64), 64);
            d.copy = mm.read32(a + 0x40);
            d.article = mm.read32(d.copy + 0x1c);
            draws.push_back(d);
        });
        m.hook(kFnRenderEffects, "RSceneObj::RenderEffects", [this](MipsMachine&) { ++render_effects; });
        m.write32(kReflection, kFakeRefl);
        m.write32(kFakeRefl + 0x5c, 0);  // o alcance do reflexo 0: nenhum objeto vai ao RReflection
        m.write32(kQueueHolder, kFakeQueue);
        // O WWorld (fgWorld): o +0x1c é o 'Wmap', cuja primeira palavra é a raiz da árvore (o SetMapNode da bandeira 8).
        m.write32(kWorld, kFakeWorld);
        m.write32(kFakeWorld + 0x1c, kFakeWmap);
        m.write32(kFakeWmap, kFakeRoot);
    }
};

struct CarpCopy {
    uint32_t port = 0, size = 0;
    uint32_t to_emu(uint32_t p) const { return in(p) ? kCarp + (p - port) : p; }
    bool in(uint32_t p) const { return p >= port && p < port + size; }
};

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& why) {
        ++bad;
        if (first.empty()) first = why;
    }
};

void report(const char* name, const Tally& t, int& failures) {
    std::printf("  %-62s %6" PRIu64 " casos, %" PRIu64 " diferentes\n", name, t.cases, t.bad);
    if (!t.first.empty()) std::printf("    primeira diferença: %s\n", t.first.c_str());
    failures += int(t.bad);
}

}  // namespace

int run_scene_article_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size,
                             int cases, uint32_t seed) {
    std::printf("\nparte 29: o modelo próprio dos objetos de cena do mundo (o RSceneObj::UseArticle e o Render)\n");
    int failures = 0;
    drv::anim_engine_reset(60.0f);
    drv::Track track;
    std::string err;
    if (!track.load(disc, "paris_mis01", "", err)) {
        std::printf("  Paris não abriu: %s\n", err.c_str());
        return 1;
    }
    const std::vector<drv::RSceneObj>& objs = track.scene_objects();
    static Ctx c;
    c.setup(elf, vu0, vu0_size);
    CarpCopy cc;
    cc.port = nf::p32_u(track.models().carp_data());
    cc.size = uint32_t(track.models().carp_size());
    if (kCarp + cc.size > kCarpEnd) {
        std::printf("  o CARP de Paris (%u bytes) não cabe na RAM emulada\n", cc.size);
        return 1;
    }
    // Os grupos dos modelos e as palavras seguidas (como na parte 18), uma vez cada.
    std::vector<uint8_t> buf(track.models().carp_data(), track.models().carp_data() + cc.size);
    std::set<uint32_t> done;
    auto reloc = [&](uint32_t port_addr) {
        if (!cc.in(port_addr) || !done.insert(port_addr).second) return;
        uint32_t v;
        std::memcpy(&v, buf.data() + (port_addr - cc.port), 4);
        v = cc.to_emu(v);
        std::memcpy(buf.data() + (port_addr - cc.port), &v, 4);
    };
    std::set<uint32_t> articles, seen_articles;
    auto reloc_article = [&](uint32_t art) {
        if (!art || !cc.in(art) || !seen_articles.insert(art).second) return;
        articles.insert(art);
        reloc(art + 0);
        reloc(art + 4);
        reloc(art + 8);
        const uint32_t anims = rd(art, 8);
        for (uint32_t a = anims; anims && nf::p32_ptr<const uint8_t>(a)[0xf] != 0; a += 0x10) {
            reloc(a);
            const uint32_t keys = rd(a, 0);
            uint16_t frames;
            std::memcpy(&frames, nf::p32_ptr<const uint8_t>(a) + 8, 2);
            for (uint32_t k = 0; keys && k < frames; ++k) reloc(keys + k * 0x20 + 0x1c);
        }
    };
    std::set<uint32_t> groups;
    for (const drv::RSceneObj& o : objs) {
        reloc(o.instance + 0x1c);
        const uint32_t art = rd(o.instance, 0x1c);
        reloc_article(art);
        const uint32_t desc = art ? rd(art, 0) : 0;
        if (!desc) continue;
        reloc(desc);
        const uint32_t g = rd(desc, 0);
        if (!g || !groups.insert(g).second) continue;
        reloc(g + 0xc);
        const drv::UGroup* grp = nf::p32_ptr<const drv::UGroup>(g);
        drv::UData* arr = drv::UGroup_GetArray(grp);
        const uint32_t total = drv::UGroup_GroupCount(grp) + grp->count;
        for (uint32_t i = 0; i < total; ++i) {
            const drv::UData* d = arr + i;
            reloc(nf::p32_u(d) + 0xc);
            if (i < drv::UGroup_GroupCount(grp)) continue;
            const uint32_t tag = d->tag & 0xffff0000u;
            const uint32_t data = nf::p32_u(drv::UData_Data(d));
            if (tag == 0x696e0000u) {  // 'in': as instâncias e os artigos delas
                for (uint32_t k = 0; k < d->count; ++k) {
                    reloc(data + k * 0x40 + 0x1c);
                    reloc_article(rd(data + k * 0x40, 0x1c));
                }
            } else if (tag == 0x61730000u) {  // 'as': os artigos do grupo (o ramo de uma instância sem artigo)
                reloc_article(data);
            }
        }
    }
    std::memcpy(c.m.mem(kCarp, cc.size), buf.data(), cc.size);

    // Os objetos escolhidos mais vezes: os com lista de instâncias ou animações.
    std::vector<size_t> rich;
    for (size_t i = 0; i < objs.size(); ++i)
        if (objs[i].article && (nf::p32_ptr<const drv::AnimHandleData>(objs[i].article)->n_instances > 1 ||
                                nf::p32_ptr<const drv::AnimHandleData>(objs[i].article)->n_systems > 0))
            rich.push_back(i);
    std::printf("  Paris: %zu objetos de cena, %zu com lista de instâncias ou animações; CARP %u bytes, %zu grupos de "
                "modelo, %zu artigos\n",
                objs.size(), rich.size(), cc.size, groups.size(), articles.size());

    // O ambiente do porte durante a parte: o que sai do UseArticle anotado (devolvido no fim).
    drv::SceneArticleEnv saved_env = drv::scene_article_env();
    const drv::RTimeData saved_time = drv::rtime_data();
    static uint32_t p_purges = 0;
    static std::vector<uint32_t> p_toggles, p_nodes;
    drv::scene_article_env().purge = [](uint32_t) { ++p_purges; };
    drv::scene_article_env().toggle_effect = [](uint32_t, uint32_t index, uint32_t effect) {
        p_toggles.push_back(index);
        p_toggles.push_back(effect);
    };
    drv::scene_article_env().set_root_node = [](drv::RSceneObj&) { p_nodes.push_back(1); };

    Rng r(seed ^ 0x29);
    Tally t_use, t_list, t_ops, t_render;
    uint64_t with_list = 0, single = 0, toggled = 0, rooted = 0, stim8 = 0, updates = 0, events = 0, drawn = 0,
             queued = 0, systems_played = 0;
    alignas(16) static drv::RSceneObj po;
    uint32_t steps = 1000;
    for (int k = 0; k < cases; ++k) {
        const size_t idx = !rich.empty() && r.chance(0.5) ? rich[r.next() % rich.size()] : size_t(r.next() % objs.size());
        const drv::RSceneObj& src = objs[idx];
        const uint32_t art = rd(src.instance, 0x1c);
        const uint32_t desc = art ? rd(art, 0) : 0;
        const uint32_t group = desc ? rd(desc, 0) : 0;
        if (!group) continue;
        // A variante: a do 'ps  ' do jogo (o +0x19 que o UseArticle gravou no objeto da pista), ou sorteada.
        uint32_t variant = (src.flags >> 8) & 0xff;
        if (r.chance(0.25)) variant = uint32_t(r.range(0, 3));
        // A bandeira 8 da instância (o nó da raiz) às vezes trocada nos dois lados (devolvida no fim do caso).
        uint8_t* pinst = nf::p32_ptr<uint8_t>(src.instance);
        const uint8_t old_flags = pinst[0xc];
        if (r.chance(0.3)) pinst[0xc] ^= 8;
        c.m.mem(cc.to_emu(src.instance), 0x40)[0xc] = pinst[0xc];
        // O tempo, o motor zerado, os objetos novos (as bandeiras sorteadas, o resto como o construtor deixa).
        steps += uint32_t(r.range(0, 500));
        drv::rtime_data().time = steps;
        c.m.write32(kTime, steps);
        // O fTimeData.x (os segundos: o giro do gProcAnimFunc 0xf2).
        drv::rtime_data().time_data[0] = float(r.range(0, 100000)) / 64.0f;
        c.m.write32(kTimeData, ps2::fbits(drv::rtime_data().time_data[0]));
        drv::anim_engine_reset(60.0f);
        c.m.write32(kFrequency, ps2::fbits(drv::anim_engine().frequency));
        c.m.write32(kCount, 0);
        c.m.write32(kLastTick, 0);
        c.heap = kHeap;
        drv::trigger_manager().pending.clear();
        std::memset(&drv::trigger_manager().event_data, 0, sizeof(drv::EventDynamicData));
        std::memset(c.m.mem(kEventData, 0x64), 0, 0x64);
        po = drv::RSceneObj{};
        po.instance = src.instance;
        po.flags = r.next();
        po.unknown1c = uint16_t(r.next());
        po.unknown20 = r.next();
        drv::RSceneObj eo = po;
        eo.instance = cc.to_emu(src.instance);
        eo.vtable = kSceneVtable;
        std::memcpy(c.m.mem(kScene, 0x40), &eo, 0x40);
        c.purges = c.radius = 0;
        c.toggles.clear();
        c.map_nodes.clear();
        p_purges = 0;
        p_toggles.clear();
        p_nodes.clear();
        c.events.clear();
        // a) o UseArticle, ou às vezes o UseInstanceList direto.
        bool ok = true;
        std::string why;
        const bool direct = r.chance(0.25);
        Tally& t = direct ? t_list : t_use;
        ++t.cases;
        if (!direct) {
            c.call(kFnUseArticle, {kScene, cc.to_emu(group), variant});
            const bool er = (c.m.gpr(R::v0) & 1) != 0;
            const bool pr = drv::RSceneObj_UseArticle(po, nf::p32_ptr<const drv::UGroup>(group), variant);
            if (er != pr) {
                ok = false;
                why = "o resultado";
            }
        } else {
            // A lista do 'in' do grupo (a 0) ou a instância do mundo, o argumento do InitEffects sorteado.
            uint32_t list = src.instance, n = 1, states = 0;
            if (drv::UData* in = drv::RSceneObj_LocateVariant(nf::p32_ptr<const drv::UGroup>(group), 0x696e2020u, 0)) {
                list = nf::p32_u(drv::UData_Data(in));
                n = in->count;
            }
            if (drv::UData* ps = drv::RSceneObj_LocateVariant(nf::p32_ptr<const drv::UGroup>(group), 0x70732020u, 0))
                if (r.chance(0.5)) states = nf::p32_u(drv::UData_Data(ps));
            const uint32_t flag = r.chance(0.5) ? 1u : 0u;
            const uint32_t name = r.chance(0.5) ? 0u : desc;
            c.call(kFnUseList, {kScene, cc.to_emu(list), n, cc.to_emu(states), flag, cc.to_emu(name)});
            drv::RSceneObj_UseInstanceList(po, list, n, states, flag != 0, name);
        }
        const uint32_t ph = po.article;
        const uint32_t eh = c.m.read32(kScene + 0x28);
        auto compare = [&](std::string& w) -> bool {
            char b[200];
            // O objeto.
            drv::RSceneObj eobj;
            std::memcpy(&eobj, c.m.mem(kScene, 0x40), 0x40);
            drv::RSceneObj pobj = po;
            pobj.instance = cc.to_emu(pobj.instance);
            pobj.unknown0c = cc.to_emu(pobj.unknown0c);
            pobj.article = pobj.article ? eh + (pobj.article - ph) : 0;
            if (pobj.unknown30[2] != 0 && !cc.in(pobj.unknown30[2])) pobj.unknown30[2] = kUnknownName;
            else pobj.unknown30[2] = cc.to_emu(pobj.unknown30[2]);
            pobj.vtable = kSceneVtable;
            const uint8_t* ea = reinterpret_cast<const uint8_t*>(&eobj);
            const uint8_t* pa = reinterpret_cast<const uint8_t*>(&pobj);
            for (uint32_t off = 0; off < 0x40; off += 4) {
                uint32_t ev, pv;
                std::memcpy(&ev, ea + off, 4);
                std::memcpy(&pv, pa + off, 4);
                if (ev != pv) {
                    std::snprintf(b, sizeof b, "o objeto +0x%x: porte %08x original %08x", off, pv, ev);
                    w = b;
                    return false;
                }
            }
            if (!ph) return eh == 0 || (w = "o Handle: só o original", false);
            // O bloco do Handle (os ponteiros para dentro dele e do CARP relativos).
            const drv::AnimHandleData& h = *nf::p32_ptr<drv::AnimHandleData>(ph);
            const uint32_t total = 0x40 + h.size;
            const uint8_t* p = nf::p32_ptr<uint8_t>(ph);
            for (uint32_t off = 0; off + 4 <= total; off += 4) {
                uint32_t pv, ev = c.m.read32(eh + off);
                std::memcpy(&pv, p + off, 4);
                const bool header_ptr = off >= 8 && off <= 0x18;
                const bool system_ptr = off >= h.systems - ph && off < h.systems - ph + uint32_t(h.n_systems) * 0x40 &&
                                        (off - (h.systems - ph)) % 0x40 == 0;
                const bool inst_art =
                    off >= 0x40 && off < 0x40 + uint32_t(h.n_instances) * 0x40 && (off - 0x40) % 0x40 == 0x1c;
                if (header_ptr || system_ptr) pv = pv ? eh + (pv - ph) : 0;
                else if (inst_art) pv = cc.to_emu(pv);
                else if (off == 4) pv = pv == nf::p32_u(&po) ? kScene : pv;
                if (pv != ev) {
                    std::snprintf(b, sizeof b, "Handle +0x%x: porte %08x original %08x", off, pv, ev);
                    w = b;
                    return false;
                }
            }
            for (uint32_t i = 0; i < h.n_instances; ++i) {
                const uint32_t a = rd(ph + 0x40 + i * 0x40, 0x1c);
                if (a && rd(a, 0x18) != c.m.read32(cc.to_emu(a) + 0x18)) {
                    w = "o artigo +0x18 (os identificadores do sistema)";
                    return false;
                }
            }
            // A lista do motor.
            const drv::AnimEngineGlobals& g = drv::anim_engine();
            if (g.count != c.m.read32(kCount) || g.last_tick != c.m.read32(kLastTick)) {
                std::snprintf(b, sizeof b, "o motor: porte %u/%u original %u/%u", g.count, g.last_tick, c.m.read32(kCount),
                              c.m.read32(kLastTick));
                w = b;
                return false;
            }
            for (uint32_t i = 0; i < g.count; ++i)
                if (eh + (g.active[i] - ph) != c.m.read32(kList + i * 4)) {
                    w = "a lista do motor";
                    return false;
                }
            // Os eventos (o número e os dados) e o EventDynamicData.
            const std::vector<drv::PendingEvent>& pe = drv::trigger_manager().pending;
            if (pe.size() != c.events.size()) {
                std::snprintf(b, sizeof b, "eventos: porte %zu original %zu", pe.size(), c.events.size());
                w = b;
                return false;
            }
            for (size_t i = 0; i < pe.size(); ++i)
                if (pe[i].type != c.events[i].type || cc.to_emu(pe[i].data) != c.events[i].data) {
                    std::snprintf(b, sizeof b, "evento %zu: porte %d original %d", i, pe[i].type, c.events[i].type);
                    w = b;
                    return false;
                }
            const uint8_t* dp = reinterpret_cast<const uint8_t*>(&drv::trigger_manager().event_data);
            for (uint32_t off = 0; off < 0x40; off += 4) {
                uint32_t pv, ev = c.m.read32(kEventData + off);
                std::memcpy(&pv, dp + off, 4);
                if (off == 0 && pv != 0) pv = eh + (pv - ph);
                else if (off == 8) pv = pv == nf::p32_u(&po) ? kScene : pv;
                if (pv != ev) {
                    std::snprintf(b, sizeof b, "EventDynamicData +0x%x: porte %08x original %08x", off, pv, ev);
                    w = b;
                    return false;
                }
            }
            return true;
        };
        if (ok) ok = compare(why);
        if (ok) {
            // As chamadas que saem: o KillEffects, os ToggleEffect (o índice e a cópia), o SetMapNode, o raio.
            std::vector<uint32_t> pt = p_toggles;
            for (size_t i = 1; i < pt.size(); i += 2) pt[i] = eh + (pt[i] - ph);
            if (c.purges != p_purges) ok = false, why = "o KillEffects";
            else if (pt != c.toggles) ok = false, why = "os ToggleEffect";
            else if (c.map_nodes.size() != p_nodes.size()) ok = false, why = "o SetMapNode";
            else if (c.radius != uint32_t(po.instance && (rd(po.instance, 0x3c) & 0x3ff) == 0 && ph)) ok = false,
                                                                                                why = "o raio";
            for (uint32_t node : c.map_nodes)
                if (node != kFakeRoot) ok = false, why = "o nó do SetMapNode (a raiz do 'Wmap')";
        }
        if (!ok) t.fail("caso " + std::to_string(k) + ": " + why);
        toggled += c.toggles.size() / 2;
        rooted += c.map_nodes.size();
        if (ph) (nf::p32_ptr<const drv::AnimHandleData>(ph)->n_instances > 1 ? with_list : single) += 1;

        // b) operações no Handle e o Render.
        if (ok && ph) {
            const int ops = r.range(0, 6);
            for (int o = 0; o < ops && ok; ++o) {
                ++t_ops.cases;
                drv::trigger_manager().pending.clear();
                c.events.clear();
                if (r.chance(0.45)) {
                    const uint32_t stim = r.chance(0.5) ? 8u : uint32_t(r.range(1, 0x16));
                    stim8 += stim == 8;
                    const uint32_t mode = uint32_t(r.range(0, 2));
                    c.call(kFnStimuliAll, {eh, stim, steps, mode});
                    drv::AnimHandle_ProcessStimuliAll(ph, stim, steps, mode);
                } else {
                    steps += uint32_t(r.range(0, 4));
                    ++updates;
                    c.call(kFnUpdate, {steps});
                    drv::RAnimEngine_Update(steps);
                }
                events += c.events.size();
                systems_played += drv::anim_engine().count;
                if (!compare(why)) {
                    ok = false;
                    t_ops.fail("caso " + std::to_string(k) + ": " + why);
                }
            }
            if (ok) {
                ++t_render.cases;
                c.draws.clear();
                c.call(kFnRender, {kScene});
                std::vector<drv::SceneObjDraw> pd;
                drv::RSceneObj_Render(po, pd);
                char b[200];
                if (pd.size() != c.draws.size()) {
                    const drv::AnimHandleData& hd = *nf::p32_ptr<const drv::AnimHandleData>(ph);
                    std::string fns;
                    for (uint32_t i = 0; i < hd.n_instances; ++i) {
                        char x[24];
                        const uint8_t* cp = nf::p32_ptr<const uint8_t>(ph + 0x40 + i * 0x40);
                        std::snprintf(x, sizeof x, " %02x/%02x", cp[0xc], cp[0xd]);
                        fns += x;
                    }
                    std::snprintf(b, sizeof b, "caso %d: desenhos porte %zu original %zu (cópias%s)", k, pd.size(),
                                  c.draws.size(), fns.c_str());
                    t_render.fail(b);
                } else {
                    for (size_t i = 0; i < pd.size(); ++i) {
                        const EmuDraw& ed = c.draws[i];
                        const bool pq = pd[i].pass != 2;
                        if (pq != ed.queued || cc.to_emu(pd[i].article) != ed.article ||
                            std::memcmp(pd[i].world, ed.m, 64) != 0 || (ed.queued && eh + (pd[i].copy - ph) != ed.copy)) {
                            float em[16];
                            std::memcpy(em, ed.m, 64);
                            const float* pm = &pd[i].world[0][0];
                            int wi = 0;
                            while (wi < 15 && ps2::fbits(pm[wi]) == ps2::fbits(em[wi])) ++wi;
                            std::snprintf(b, sizeof b, "caso %d: o desenho %zu (%s; gProcAnimFunc %02x, [%d] porte %08x original %08x)",
                                          k, i, std::memcmp(pd[i].world, ed.m, 64) != 0 ? "a matriz" : "o artigo ou a fila",
                                          nf::p32_ptr<const uint8_t>(pd[i].copy)[0xd], wi, ps2::fbits(pm[wi]),
                                          ps2::fbits(em[wi]));
                            t_render.fail(b);
                            break;
                        }
                        drawn += !ed.queued;
                        queued += ed.queued;
                    }
                }
            }
            drv::AnimHandle_Destroy(ph);
            po.article = 0;
        }
        pinst[0xc] = old_flags;
        c.m.mem(cc.to_emu(src.instance), 0x40)[0xc] = old_flags;
    }
    drv::scene_article_env() = saved_env;
    drv::rtime_data() = saved_time;
    drv::anim_engine_reset(60.0f);
    report("RSceneObj::UseArticle (0x1c6fb8, UseInstanceList, Handle::Create, InitEffects)", t_use, failures);
    report("RSceneObj::UseInstanceList direto (o argumento do InitEffects)", t_list, failures);
    report("os estímulos e o RAnimEngine::Update no Handle do objeto", t_ops, failures);
    report("RSceneObj::Render (DrawInstanceList, DrawInstance, GetInstanceMatrix)", t_render, failures);
    std::printf("    objetos com lista %" PRIu64 ", de uma instância %" PRIu64 "; ToggleEffect %" PRIu64
                ", nós da raiz %" PRIu64 "; estímulos 8 %" PRIu64 ", Updates %" PRIu64 ", eventos %" PRIu64
                ", sistemas tocando (soma) %" PRIu64 "; desenhos agora %" PRIu64 ", na fila %" PRIu64 "\n",
                with_list, single, toggled, rooted, stim8, updates, events, systems_played, drawn, queued);
    if (c.errors) {
        std::printf("    o interpretador falhou %" PRIu64 " vezes: %s\n", c.errors, c.error.c_str());
        failures += int(c.errors);
    }
    return failures;
}

}  // namespace drvcheck
