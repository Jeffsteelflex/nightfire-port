// nf_driving_check, parte 5 (tests/driving_check_world.h): o chão das pistas. Cada pista do disco é carregada pelo
// porte (drv::Track, o CARP resolvido como o jogo) e o seu mundo de colisão montado por drv::TrackWorld (o 'CDat':
// WCollisionMgr::Init e WGrid::Init portados); uma cópia do que as consultas leem vai para a RAM emulada (as
// instâncias 'ci', os artigos 'ca', os nós 'cn' e a tabela de nós da grade, com os ponteiros trocados pelos endereços
// emulados), e o código ORIGINAL do DRIVING.ELF roda no interpretador MIPS ao lado do porte, sobre os mesmos pontos:
//  - WWorldPos::FindClosestFace(COORD3 &, bool) (0x2322c8): pontos soltos na grade e perto das instâncias, com um
//    WWorldPos novo ou guardado de uma consulta anterior (andando pela pista: o caminho que guarda a face), com o
//    artigo trocado (a face velha), e a máscara de barreiras do gerente variando (0xf0 do construtor, 0x20 e 0x10 de
//    TempGetHeightInformation, 0); compara o retorno e o WWorldPos inteiro;
//  - WCollisionMgr::GetWorldHeightAtPoint (0x2151b8): o retorno e a altura;
//  - WWorldPos::MakeFaceAtPoint (0x232228);
//  - WCollisionMgr::SurfaceBumpHeight (0x213618) pelos 12 primeiros tipos de piso;
//  - no fim, a contagem de consultas do gerente (+0x04) e a marca de cada instância (+0x10): as mesmas instâncias
//    visitadas, na mesma ordem de consultas.
//
// O que não é comparado, e por quê:
//  - o w de v0 do WWorldPos (+0x0c): FindClosestFace copia 0x30 bytes da face candidata, que fica na pilha e cujo +0x0c
//    nunca é escrito (lixo da pilha no PS2, zero no port);
//  - os ponteiros (WWorldPos +0x34 a instância, +0x38 o artigo) são comparados pelo que apontam: o índice da instância
//    e o artigo correspondente da cópia;
//  - o sinf/cosf do newlib (SurfaceBumpHeight): os dois lados usam nf::fm::sin/cos (o original fica por um gancho; ver
//    world_collision.h).
// Fora do código original (ganchos nativos): memset, memmove, UMemory::FastAlloc/FastFree, sinf e cosf.

#include "driving_check_world.h"

#include <algorithm>
#include <cinttypes>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/physics/world.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kCollMgrPtr = 0x33a540;  // WCollisionMgr::fgCollisionMgr
constexpr uint32_t kGridPtr = 0x33a5c8;     // WGrid::fgGrid
constexpr uint32_t kSimSteps = 0x339f54;    // Simulation +0xdc4 (o passo que WWorldPos +0x3c guarda)

constexpr uint32_t kFnFindClosestFace = 0x2322c8;
constexpr uint32_t kFnMakeFaceAtPoint = 0x232228;
constexpr uint32_t kFnSurfaceBumpHeight = 0x213618;
constexpr uint32_t kFnGetWorldHeightAtPoint = 0x2151b8;

// A RAM emulada.
constexpr uint32_t kMgr = 0x01000000;
constexpr uint32_t kGrid = 0x01000100;
constexpr uint32_t kWp = 0x01000200;
constexpr uint32_t kPoint = 0x01000300;
constexpr uint32_t kOut = 0x01000340;
constexpr uint32_t kSurface = 0x01000380;
constexpr uint32_t kHeapBegin = 0x01010000, kHeapEnd = 0x01100000;
constexpr uint32_t kWorldBegin = 0x01100000, kWorldEnd = MipsMachine::kStackTop - 0x40000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    float uni(float a, float b) { return a + (b - a) * float(next() & 0xffffff) / float(0x1000000); }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", v);
    return b;
}

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

// A cópia do mundo na RAM emulada.
class WorldEmu {
public:
    MipsMachine m;
    uint32_t heap = kHeapBegin;

    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.hook(0x2a8038, "memset", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2a7f34, "memmove", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2cfda8, "UMemory::FastAlloc", [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("FastAlloc: a área acabou", heap, n);
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        });
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
    }

    void w32(uint32_t a, uint32_t v) { m.write32(a, v); }
    uint32_t r32(uint32_t a) { return m.read32(a); }
    void put(uint32_t a, const void* src, uint32_t n) { std::memcpy(m.mem(a, n), src, n); }
    void get(uint32_t a, void* dst, uint32_t n) { std::memcpy(dst, m.mem(a, n), n); }

    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        heap = kHeapBegin;
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 50'000'000);
    }
};

// O tamanho que as consultas leem de um artigo: o cabeçalho, as esferas das faixas, os vértices de cada faixa e as
// barreiras (a colisão com os prédios, parte 7).
uint32_t article_extent(const uint8_t* art) {
    const auto* a = reinterpret_cast<const drv::CollisionArticle*>(art);
    const auto* st = reinterpret_cast<const drv::CollisionStrip*>(art + 0x20);
    uint32_t end = 0x20 + 16u * a->strip_count;
    end = std::max(end, 0x20u + a->barrier_offset + 0x20u * a->barrier_count);
    for (int i = 0; i < a->strip_count; ++i) {
        const auto* v = reinterpret_cast<const drv::StripVertex*>(art + 0x20 + st[i].vertex_offset);
        end = std::max(end, 0x20u + st[i].vertex_offset + 16u * v[0].w);
    }
    return end;
}

uint32_t node_extent(const drv::WGridNode* n) {
    uint32_t end = 16;
    for (int t = 0; t < 4; ++t) end = std::max(end, 16u + n->static_offset[t] + 2u * n->static_count[t]);
    return end;
}

// Compara dois WWorldPos (o +0x0c fica de fora); devolve o primeiro campo diferente, ou vazio.
std::string diff_wp(const drv::WWorldPos& a, const drv::WWorldPos& b) {
    uint32_t wa[16], wb[16];
    std::memcpy(wa, &a, 0x40);
    std::memcpy(wb, &b, 0x40);
    for (int i = 0; i < 16; ++i) {
        if (i == 3) continue;
        if (wa[i] != wb[i]) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "WWorldPos +0x%02x: original %08x, porte %08x", 4 * i, wa[i], wb[i]);
            return buf;
        }
    }
    return {};
}

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad,
                t.bad ? ": " : "", t.bad ? t.first.c_str() : "");
    if (t.bad) ++failures;
}

const char* const kTracks[] = {"paris_mis01",    "uw_mis11",    "junglea_mis13a", "jungleb_mis13b",
                               "snow1a_mis3",    "snow2a_mis4", "junglec_mis13c", "snow2a_race"};

}  // namespace

void put(nf::MipsMachine& m, uint32_t addr, const void* src, uint32_t n) { std::memcpy(m.mem(addr, n), src, n); }

bool copy_world(nf::MipsMachine& m, drv::TrackWorld& tw, WorldCopy& wc, uint32_t mgr_addr, uint32_t grid_addr,
                uint32_t begin, uint32_t end, std::string& error) {
    uint32_t at = begin;
    auto alloc = [&](uint32_t n) -> uint32_t {
        const uint32_t a = at;
        at = (at + n + 15) & ~15u;
        if (at > end) return 0;
        return a;
    };
    wc.count = tw.instance_count();
    wc.insts = alloc(0x40 * wc.count);
    if (!wc.insts) {
        error = "as instâncias não cabem na RAM emulada";
        return false;
    }
    const drv::WCollisionInstance* insts = tw.instances();
    for (uint32_t i = 0; i < wc.count; ++i) {
        drv::WCollisionInstance ci = insts[i];
        if (ci.article) {
            auto it = wc.art_emu.find(ci.article);
            if (it == wc.art_emu.end()) {
                const uint8_t* art = nf::p32_ptr<uint8_t>(ci.article);
                const uint32_t n = article_extent(art);
                const uint32_t a = alloc(n);
                if (!a) {
                    error = "os artigos não cabem na RAM emulada";
                    return false;
                }
                put(m, a, art, n);
                it = wc.art_emu.emplace(ci.article, a).first;
                wc.art_port.emplace(a, ci.article);
            }
            ci.article = it->second;
        }
        put(m, wc.insts + 0x40 * i, &ci, 0x40);
    }
    drv::WGrid g = tw.grid();
    const uint32_t cells = g.rows * g.columns;
    const uint32_t table = alloc(4 * cells);
    if (!table) {
        error = "a tabela da grade não cabe na RAM emulada";
        return false;
    }
    for (uint32_t c = 0; c < cells; ++c) {
        const drv::WGridNode* node = tw.node(c);
        uint32_t a = 0;
        if (node) {
            const uint32_t n = node_extent(node);
            a = alloc(n);
            if (!a) {
                error = "os nós não cabem na RAM emulada";
                return false;
            }
            put(m, a, node, n);
        }
        m.write32(table + 4 * c, a);
    }
    g.nodes = table;
    put(m, grid_addr, &g, sizeof(g));
    // Os objetos de colisão ('co') e as instâncias de render do mundo (o 'in  ' do 'Map ', WWorld +0x20), para a colisão
    // com os prédios (parte 7).
    wc.objs = 0;
    if (tw.object_count()) {
        wc.objs = alloc(0x30 * tw.object_count());
        if (!wc.objs) {
            error = "os objetos não cabem na RAM emulada";
            return false;
        }
        put(m, wc.objs, tw.objects(), 0x30 * tw.object_count());
    }
    wc.render = 0;
    if (tw.render_instance_count()) {
        wc.render = alloc(64 * tw.render_instance_count());
        if (!wc.render) {
            error = "as instâncias de render não cabem na RAM emulada";
            return false;
        }
        put(m, wc.render, tw.render_instances(), 64 * tw.render_instance_count());
    }
    drv::WCollisionMgr mgr = tw.mgr();
    mgr.instances = wc.insts;
    mgr.objects = wc.objs;
    put(m, mgr_addr, &mgr, sizeof(mgr));
    m.write32(kCollMgrPtr, mgr_addr);
    m.write32(kGridPtr, grid_addr);
    wc.mgr = mgr_addr;
    wc.grid = grid_addr;
    wc.bytes = at - begin;
    return true;
}

// O WWorldPos do port na RAM emulada (os ponteiros trocados) e o da RAM emulada no espaço do port.
void wp_to_emu(const drv::WWorldPos& wp, const drv::TrackWorld& tw, const WorldCopy& wc, uint8_t out[0x40]) {
    drv::WWorldPos c = wp;
    if (c.instance) c.instance = wc.insts + (c.instance - nf::p32_u(tw.instances()));
    if (c.article) {
        auto it = wc.art_emu.find(c.article);
        c.article = it == wc.art_emu.end() ? 0xdead0000u : it->second;
    }
    std::memcpy(out, &c, 0x40);
}
drv::WWorldPos wp_from_emu(const uint8_t in[0x40], const drv::TrackWorld& tw, const WorldCopy& wc) {
    drv::WWorldPos c;
    std::memcpy(&c, in, 0x40);
    if (c.instance) c.instance = nf::p32_u(tw.instances()) + (c.instance - wc.insts);
    if (c.article) {
        auto it = wc.art_port.find(c.article);
        c.article = it == wc.art_port.end() ? 0xdead0000u : it->second;
    }
    return c;
}

int run_world_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n== o chão das pistas: o código original do EE x o porte (WCollisionMgr, WGrid, WWorldPos) ==\n");
    int failures = 0;
    WorldEmu* e = new WorldEmu();  // 32 MB de RAM
    try {
        e->setup(elf);
        const std::vector<uint8_t> clean(e->m.ram());
        Rng rng(seed ^ 0x5eed);
        for (const char* name : kTracks) {
            std::memcpy(e->m.ram().data(), clean.data(), clean.size());
            drv::Track track;
            std::string error;
            if (!track.load(disc, name, "", error)) {
                std::printf("  %s: não carregou (%s)\n", name, error.c_str());
                ++failures;
                continue;
            }
            drv::TrackWorld tw;
            if (!tw.init(track.root(), &error)) {
                std::printf("  %s: o mundo de colisão não montou (%s)\n", name, error.c_str());
                ++failures;
                continue;
            }
            WorldCopy wc;
            if (!copy_world(e->m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
                std::printf("  %s: %s\n", name, error.c_str());
                ++failures;
                continue;
            }
            const drv::WGrid& g = tw.grid();
            uint32_t nodes = 0;
            for (uint32_t c = 0; c < g.rows * g.columns; ++c) nodes += tw.node(c) != nullptr;
            uint32_t tilted = 0, above = 0, strips = 0;
            for (uint32_t i = 0; i < wc.count; ++i) {
                tilted += (tw.instances()[i].flags & 3) != 0;
                above += (tw.instances()[i].flags & 1) != 0;
            }
            for (const auto& a : wc.art_emu)
                strips += reinterpret_cast<const drv::CollisionArticle*>(nf::p32_ptr<uint8_t>(a.first))->strip_count;
            std::printf("  %s: %u instâncias (%u inclinadas, %u com a face acima), %zu artigos com %u faixas, grade %ux%u"
                        " de %.1f m (%u nós), %.1f MB copiados\n",
                        name, wc.count, tilted, above, wc.art_emu.size(), strips, g.columns, g.rows,
                        double(g.cell_size), nodes, double(wc.bytes) / 1048576.0);

            // Os pontos: soltos na grade, ou perto de uma instância com artigo (onde há faces).
            std::vector<uint32_t> with_article;
            for (uint32_t i = 0; i < wc.count; ++i)
                if (tw.instances()[i].article) with_article.push_back(i);
            auto random_point = [&](float p[3]) {
                if (with_article.empty() || rng.chance(0.25)) {
                    p[0] = g.origin[0] + rng.uni(-0.05f, 1.05f) * g.cell_size * float(g.columns);
                    p[2] = g.origin[2] + rng.uni(-0.05f, 1.05f) * g.cell_size * float(g.rows);
                    p[1] = rng.uni(-50.0f, 150.0f);
                    return;
                }
                const drv::WCollisionInstance& ci = tw.instances()[with_article[rng.next() % with_article.size()]];
                float c[3];
                drv::WCollisionInstance_CalcPosition(tw.mgr(), ci, c);
                const float r = std::max(1.0f, ci.radius);
                p[0] = c[0] + rng.uni(-r, r);
                p[2] = c[2] + rng.uni(-r, r);
                p[1] = c[1] + rng.uni(-ci.half_height - 2.0f, ci.half_height + 6.0f);
            };
            const uint32_t masks[4] = {0xf0, 0x20, 0x10, 0};

            Tally t_find, t_walk, t_stale, t_height, t_make, t_bump, t_stamps;
            uint64_t found = 0, kept = 0, heights = 0;
            drv::WWorldPos walk{};  // o WWorldPos que anda (os dois lados partem do mesmo)
            float wpos[3];
            random_point(wpos);
            for (int c = 0; c < cases; ++c) {
                const uint32_t mask = masks[rng.range(0, 3)];
                const int32_t step = int32_t(rng.next() & 0xffff);
                tw.set_barrier_mask(mask);
                drv::simulation().steps = step;
                e->w32(kMgr + 0x2c, mask);
                e->w32(kSimSteps, uint32_t(step));

                // 1. Um WWorldPos novo (zerado), keep 0 ou 1.
                {
                    float p[3];
                    random_point(p);
                    const bool keep = rng.chance(0.5);
                    drv::WWorldPos wp{};
                    uint8_t img[0x40];
                    wp_to_emu(wp, tw, wc, img);
                    e->put(kWp, img, 0x40);
                    e->put(kPoint, p, 12);
                    e->call(kFnFindClosestFace, {kWp, kPoint, keep ? 1u : 0u});
                    const uint32_t r_emu = uint32_t(e->m.gpr(R::v0));
                    const bool r_port = tw.find_closest_face(wp, p, keep);
                    e->get(kWp, img, 0x40);
                    const drv::WWorldPos o = wp_from_emu(img, tw, wc);
                    ++t_find.cases;
                    std::string d = diff_wp(o, wp);
                    if (d.empty() && r_emu != (r_port ? 1u : 0u)) d = "retorno " + hex(r_emu);
                    if (!d.empty()) t_find.fail(d);
                    found += wp.valid != 0;
                }
                // 2. O WWorldPos que anda pela pista com keep (a face guardada), às vezes saltando.
                {
                    if (rng.chance(0.1)) random_point(wpos);
                    else
                        for (int k : {0, 2}) wpos[k] += rng.uni(-1.5f, 1.5f);
                    const bool keep = rng.chance(0.85);
                    uint8_t img[0x40];
                    wp_to_emu(walk, tw, wc, img);
                    e->put(kWp, img, 0x40);
                    e->put(kPoint, wpos, 12);
                    e->call(kFnFindClosestFace, {kWp, kPoint, keep ? 1u : 0u});
                    const uint32_t r_emu = uint32_t(e->m.gpr(R::v0));
                    const bool r_port = tw.find_closest_face(walk, wpos, keep);
                    e->get(kWp, img, 0x40);
                    const drv::WWorldPos o = wp_from_emu(img, tw, wc);
                    ++t_walk.cases;
                    std::string d = diff_wp(o, walk);
                    if (d.empty() && r_emu != (r_port ? 1u : 0u)) d = "retorno " + hex(r_emu);
                    if (!d.empty()) t_walk.fail(d);
                    kept += keep && !r_port;
                    // Se divergiu, o próximo passo parte do lado do port nos dois.
                }
                // 3. A face velha: válida, mas com o artigo trocado ou a instância nula.
                if (walk.valid && rng.chance(0.3)) {
                    drv::WWorldPos wp = walk;
                    if (rng.chance(0.5)) wp.instance = 0;
                    else {
                        auto it = wc.art_emu.begin();
                        std::advance(it, rng.next() % wc.art_emu.size());
                        wp.article = it->first;
                    }
                    float p[3] = {wpos[0] + rng.uni(-0.3f, 0.3f), wpos[1], wpos[2] + rng.uni(-0.3f, 0.3f)};
                    uint8_t img[0x40];
                    wp_to_emu(wp, tw, wc, img);
                    e->put(kWp, img, 0x40);
                    e->put(kPoint, p, 12);
                    e->call(kFnFindClosestFace, {kWp, kPoint, 1u});
                    const uint32_t r_emu = uint32_t(e->m.gpr(R::v0));
                    const bool r_port = tw.find_closest_face(wp, p, true);
                    e->get(kWp, img, 0x40);
                    const drv::WWorldPos o = wp_from_emu(img, tw, wc);
                    ++t_stale.cases;
                    std::string d = diff_wp(o, wp);
                    if (d.empty() && r_emu != (r_port ? 1u : 0u)) d = "retorno " + hex(r_emu);
                    if (!d.empty()) t_stale.fail(d);
                }
                // 4. GetWorldHeightAtPoint.
                {
                    float p[3];
                    random_point(p);
                    e->put(kPoint, p, 12);
                    const float sentinel = 12345.0f;
                    e->put(kOut, &sentinel, 4);
                    e->call(kFnGetWorldHeightAtPoint, {kMgr, kPoint, kOut});
                    const uint32_t r_emu = uint32_t(e->m.gpr(R::v0));
                    float h_emu;
                    e->get(kOut, &h_emu, 4);
                    float h = sentinel;
                    const bool r_port = tw.world_height_at_point(p, h);
                    ++t_height.cases;
                    if (r_emu != (r_port ? 1u : 0u)) t_height.fail("retorno " + hex(r_emu));
                    else if (ps2::fbits(h_emu) != ps2::fbits(h))
                        t_height.fail("altura: original " + hex(ps2::fbits(h_emu)) + ", porte " + hex(ps2::fbits(h)));
                    heights += r_port;
                }
                // 5. MakeFaceAtPoint sobre um WWorldPos com lixo.
                {
                    float p[3];
                    random_point(p);
                    drv::WWorldPos wp{};
                    uint32_t junk[16];
                    for (uint32_t& j : junk) j = rng.next();
                    std::memcpy(&wp, junk, 0x40);
                    wp.instance = 0;
                    wp.article = 0;
                    uint8_t img[0x40];
                    wp_to_emu(wp, tw, wc, img);
                    e->put(kWp, img, 0x40);
                    e->put(kPoint, p, 12);
                    e->call(kFnMakeFaceAtPoint, {kWp, kPoint});
                    tw.make_face_at_point(wp, p);
                    e->get(kWp, img, 0x40);
                    const drv::WWorldPos o = wp_from_emu(img, tw, wc);
                    ++t_make.cases;
                    uint32_t wa[16], wb[16];
                    std::memcpy(wa, &o, 0x40);
                    std::memcpy(wb, &wp, 0x40);
                    for (int i = 0; i < 16; ++i)
                        if (wa[i] != wb[i]) {
                            t_make.fail("+0x" + hex(4u * uint32_t(i)) + ": original " + hex(wa[i]) + ", porte " +
                                        hex(wb[i]));
                            break;
                        }
                }
                // 6. SurfaceBumpHeight.
                {
                    float p[3];
                    random_point(p);
                    uint8_t surf[4] = {uint8_t(rng.range(0, 11)), uint8_t(rng.next()), uint8_t(rng.next()),
                                       uint8_t(rng.next())};
                    e->put(kPoint, p, 12);
                    e->put(kSurface, surf, 4);
                    e->call(kFnSurfaceBumpHeight, {kMgr, kPoint, kSurface});
                    const float b_emu = e->m.fpr(0);
                    const float b = tw.surface_bump_height(p, surf);
                    ++t_bump.cases;
                    if (ps2::fbits(b_emu) != ps2::fbits(b))
                        t_bump.fail("tipo " + std::to_string(surf[0]) + ": original " + hex(ps2::fbits(b_emu)) +
                                    ", porte " + hex(ps2::fbits(b)));
                }
            }
            // 7. As marcas das consultas.
            {
                ++t_stamps.cases;
                const uint32_t q_emu = e->r32(kMgr + 4);
                if (q_emu != tw.mgr().query_stamp)
                    t_stamps.fail("consultas do gerente: original " + std::to_string(q_emu) + ", porte " +
                                  std::to_string(tw.mgr().query_stamp));
                for (uint32_t i = 0; i < wc.count && t_stamps.bad == 0; ++i) {
                    const uint32_t s = e->r32(wc.insts + 0x40 * i + 0x10);
                    if (s != tw.instances()[i].query_stamp)
                        t_stamps.fail("instância " + std::to_string(i) + ": marca original " + std::to_string(s) +
                                      ", porte " + std::to_string(tw.instances()[i].query_stamp));
                }
            }
            std::printf("    faces achadas %" PRIu64 " de %d, guardadas %" PRIu64 ", alturas %" PRIu64
                        ", guardas %" PRIu64 "\n",
                        found, cases, kept, heights, drv::g_world_collision_guards);
            report("FindClosestFace (WWorldPos novo)", t_find, failures);
            report("FindClosestFace (andando, a face guardada)", t_walk, failures);
            report("FindClosestFace (a face velha)", t_stale, failures);
            report("GetWorldHeightAtPoint", t_height, failures);
            report("MakeFaceAtPoint", t_make, failures);
            report("SurfaceBumpHeight", t_bump, failures);
            report("as marcas de consulta (gerente e instâncias)", t_stamps, failures);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    delete e;
    return failures;
}

}  // namespace drvcheck
