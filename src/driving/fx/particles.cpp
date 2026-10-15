#include "driving/fx/particles.h"

#include <cstring>

#include "driving/ai/sim_random.h"
#include "driving/light_manager.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

RParticleSystemManager* g_manager = nullptr;
uint32_t g_density = 0x100;  // 0x425148 (ver particle_density)
int32_t g_toggle = 0;        // 0x3390b0
float g_view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
StickyPosFn g_sticky = nullptr;
RigidPosFn g_rigid = nullptr;
ParticleStats g_stats;

inline float fb(uint32_t w) { return ps2::ffloat(w); }
inline uint32_t bf(float f) { return ps2::fbits(f); }

// cvt.s.W de um inteiro sem sinal como o GCC o faz (o bit 31: (v >> 1 | v & 1) x 2).
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(h, h);
}

// [driving] 0x0024e340 __fixunssfsi (fptoui): os valores até 2^31 por cvt.w.s; acima, menos 2^31 e o bit de volta.
uint32_t fptoui(float f) {
    if (ee::lt(f, 2147483648.0f)) return uint32_t(ee::cvt_w(f));
    return uint32_t(ee::cvt_w(ee::sub(f, 2147483648.0f))) ^ 0x80000000u;
}

// O 'dflt' do .data (0x3351c0), palavra a palavra.
const uint32_t kDefaultWords[44] = {
    0x00000000, 0x3d8f5c29, 0x00000000, 0x000000c8, 0x3a83126f, 0x3a03126f, 0x3a83126f, 0x00000008,
    0x3f800000, 0x736d6b35, 0x00000000, 0x00000000, 0x00000000, 0x3e99999a, 0x3e99999a, 0x00000050,
    0x00000050, 0x00000000, 0x00000000, 0x40000000, 0x00000000, 0x43340000, 0x3f000000, 0x00000000,
    0x3f007ffe, 0x3f207ffe, 0x3df80022, 0x00000000, 0x437f0000, 0x437f0000, 0x437f0000, 0x437f0000,
    0x43000000, 0x437f0000, 0x437f0000, 0x437f0000, 0x41800000, 0x437f0000, 0x437f0000, 0x437f0000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

// O produto do RRandom inline dos usuários: a semente fica com os 16 bits de baixo e o número são os bits 8-23.
uint32_t rrandom() {
    RRandom& r = render_random();
    const uint32_t p = r.seed * r.multiplier;
    r.seed = p & 0xffffu;
    return (p & 0xffff00u) >> 8;
}

// O quatérnio-vetor de 4 palavras de 3 floats (o w da pilha não importa: as contas são .xyz).
void load3(uint32_t* d, const float* s) {
    d[0] = bf(s[0]);
    d[1] = bf(s[1]);
    d[2] = bf(s[2]);
}

}  // namespace

RParticleSystemManager* particle_manager() { return g_manager; }
uint32_t& particle_density() { return g_density; }
int32_t& particle_render_toggle() { return g_toggle; }
void particles_set_view_matrix(const float m[16]) { std::memcpy(g_view, m, sizeof(g_view)); }
const float* particles_view_matrix() { return g_view; }
StickyPosFn& particles_sticky_pos() { return g_sticky; }
RigidPosFn& particles_rigid_pos() { return g_rigid; }
ParticleStats& particle_stats() { return g_stats; }

const ParticleData& particle_default_data() {
    static ParticleData d = [] {
        ParticleData x;
        std::memcpy(&x, kDefaultWords, sizeof(x));
        return x;
    }();
    return d;
}

void ParticleDrawLists::clear() {
    for (int b = 0; b < 2; ++b) {
        normal[b] = Sprites{};
        additive[b] = Sprites{};
    }
    for (int b = 0; b < 2; ++b) {
        tri_coords[b].clear();
        tri_colours[b].clear();
    }
}

// ---------------------------------------------------------------------------------------------------- a biblioteca

// [driving] 0x001f3188 RParticleLibrary::MungeDataForPlatform
void RParticleLibrary_MungeDataForPlatform(const ParticleData& in, ParticleData& out) {
    if (&in != &out) out = in;
    // O maior tamanho: o +0x38 se o +0x34 for menor.
    out.max_size = ee::lt(out.size, out.size_change) ? out.size_change : out.size;
    if (out.flags & 2) out.off_time = 99999;
    if (out.life >= 0x100) out.life = 0xff;  // (o printf "ERROR IN PARTICLE DATA")
    out.inv_life = ee::div(1.0f, u2f(out.life));
    out.count = fptoui(ee::mul(u2f(out.count), 0.5f));
    if ((out.flags & 0x20) == 0) {
        const float k = fb(0x3fb50481);  // 1,4142
        const float a = ee::mul(out.size, k), b = ee::mul(out.size_change, k);
        out.size = b;
        out.size_change = ee::sub(a, b);
    }
    if (!ee::eq(out.spin, 0.0f)) out.flags |= 8;
    if (out.flags & 0x10) out.flags |= 8;
    out.spin = ee::mul(out.spin, fb(0x3c8efa35));  // graus para radianos
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 4; ++k) out.colour[c][k] = ee::mul(out.colour[c][k], fb(0x3b000000));  // 1/512
    out.colour[2][0] = ee::mul(out.colour[2][0], 0.5f);
    out.colour[0][0] = ee::mul(out.colour[0][0], 0.5f);
    out.colour[1][0] = ee::mul(out.colour[1][0], 0.5f);
    float t[4];
    std::memcpy(t, out.colour[2], 16);
    std::memcpy(out.colour[2], out.colour[0], 16);
    std::memcpy(out.colour[0], t, 16);
}

// [driving] 0x001e80b0 RParticleLibrary::MungeData
void RParticleLibrary_MungeData(const ParticleData& in, ParticleData& out) {
    if (out.life >= 0x100) out.life = 0xff;  // (o printf)
    if (out.initial != 0) out.initial = fptoui(ee::mul(u2f(out.life), 0.75f));
    RParticleLibrary_MungeDataForPlatform(in, out);
}

// [driving] 0x001e7f60 RParticleLibrary::Reload
void RParticleLibrary_Reload(RParticleLibrary& lib) {
    lib.count = 0;
    lib.tags.clear();
    ParticleData d = particle_default_data();
    RParticleLibrary_AddSystem(lib, d, 0x64666c74u);  // 'dflt'
}

// [driving] 0x001e8040 RParticleLibrary::AddSystem (0x001e7cf0 RPartLibraryData::AddSystem e o MungeData da cópia)
int32_t RParticleLibrary_AddSystem(RParticleLibrary& lib, ParticleData& data, uint32_t tag) {
    if (lib.count >= 0x100) return -1;
    const uint32_t i = uint32_t(lib.count);
    data.index = i;  // o original grava o índice no dado de quem chama
    lib.systems[i] = data;
    lib.tags.emplace(tag, i);  // _Rb_tree::insert_unique: uma etiqueta repetida fica com o primeiro
    ++lib.count;
    ParticleData& d = lib.systems[i];
    RParticleLibrary_MungeData(d, d);
    return int32_t(i);
}

// [driving] 0x001e8008 RParticleLibrary::FindSystemViaTag (0x001e7db0): sem a etiqueta, o de menor etiqueta (o
// leftmost do cabeçalho da árvore).
ParticleData* RParticleLibrary_FindSystemViaTag(RParticleLibrary& lib, uint32_t tag) {
    auto it = lib.tags.find(tag);
    if (it == lib.tags.end()) {
        if (lib.tags.empty()) {
            ++g_stats.guards;  // o PS2 leria o +0x14 do próprio cabeçalho
            return &lib.systems[0];
        }
        it = lib.tags.begin();
    }
    return &lib.systems[it->second];
}

// [driving] 0x001e8028 RParticleLibrary::FindSystemViaIndex
ParticleData* RParticleLibrary_FindSystemViaIndex(RParticleLibrary& lib, uint32_t index) {
    if (index >= 256) {
        ++g_stats.guards;
        return &lib.systems[0];
    }
    return &lib.systems[index];
}

// ---------------------------------------------------------------------------------------------------- os sistemas

// [driving] 0x001e5cd8 CalcEmitVal
float CalcEmitVal(int32_t count, int32_t life, int32_t on, int32_t off) {
    if (off != 0 && on <= life) {
        const int32_t cycle = on + off;
        if (life <= cycle) return ee::div(ee::cvt_s(count), ee::cvt_s(on));
        const float r = ee::mul(ee::div(ee::cvt_s(life), ee::cvt_s(cycle)), ee::cvt_s(on));
        return ee::div(ee::cvt_s(count), r);
    }
    return ee::div(ee::cvt_s(count), ee::cvt_s(life));
}

// [driving] 0x001e5d78 RParticleSystem::InitData
void RParticleSystem_InitData(RParticleSystem& s) {
    ParticleEmitter* e = nf::raw(s.emitter);
    s.source = nf::p32_u(e);
    s.last[0] = e->pos[0];
    s.last[1] = e->pos[1];
    s.last[2] = e->pos[2];
    uint32_t side[4], dir[4], acc[4], ax[4];
    load(side, e->side, XYZW);
    load(dir, e->dir, XYZW);
    load(ax, s.axis, XYZW);
    opmula(acc, side, dir);
    opmsub(ax, acc, dir, side);
    store(s.axis, ax, XYZ);
    const ParticleData* d = nf::raw(s.data);
    const float r = ee::mul(s.rate, s.rate_scale);
    s.spawns = ee::add(s.spawns, ee::mul(r, u2f(d->initial)));
}

// [driving] 0x001e5e10 RParticleSystem::RParticleSystem
void RParticleSystem_Construct(RParticleSystem& s, float lod, ParticleEmitter* emitter, bool init_data) {
    RParticleSystemManager* m = g_manager;
    s.lod = lod;
    s.axis[3] = 5.0f;
    s.keep_alive = 0x1e;
    s.vtable = kParticleSystemVtable;
    s.time = 0;
    s.next_spawn = 1.0f;
    s.spawns = 0.0f;
    ParticleData* d = RParticleLibrary_FindSystemViaTag(m->library, emitter->tag);
    s.index = 0;
    s.emitter = emitter;
    s.active = 1;
    s.alpha_bias = 1.0f;
    s.data = d;
    s.flags = 0;
    s.velocity = 0;
    s.rate_scale = 1.0f;
    s.unknown54 = 0.0f;
    s.index = d->index;
    s.scaled_count = (d->count * g_density) >> 8;
    s.rate = CalcEmitVal(int32_t(d->count), int32_t(d->life), int32_t(d->on_time), int32_t(d->off_time));
    if (init_data) RParticleSystem_InitData(s);
    if (ee::eq(nf::raw(s.data)->size, 0.0f) && ee::eq(nf::raw(s.data)->size_change, 0.0f)) s.flags |= 2;
    if (nf::raw(s.data)->flags & 2) s.active = 0;
    ++g_stats.systems_created;
}

// [driving] 0x001e60c8 RMovableParticleSystem::RMovableParticleSystem: a cópia do emissor em +0x70 é o emissor.
void RMovableParticleSystem_Construct(RParticleSystem& s, float lod, const ParticleEmitter& emitter) {
    // O construtor de base com o emissor de quem chamou e sem o InitData (o PS2 passa param_2 e 0).
    RParticleSystem_Construct(s, lod, const_cast<ParticleEmitter*>(&emitter), false);
    s.vtable = kMovableParticleSystemVtable;
    s.copy = emitter;
    s.source = nf::p32_u(&s.copy);
    s.emitter = &s.copy;
    RParticleSystem_InitData(s);
}

// [driving] 0x001e5f60 RParticleSystem::~RParticleSystem (0x001e61a0 o do móvel, que volta a tabela e chama este)
void RParticleSystem_Destruct(RParticleSystem& s) {
    s.vtable = kParticleSystemVtable;
    if (s.emitter) nf::raw(s.emitter)->system = nullptr;
    ++g_stats.systems_deleted;
}

// [driving] 0x001e5fa0 RParticleSystem::SetSystem
void RParticleSystem_SetSystem(RParticleSystem& s, uint32_t tag) {
    ParticleEmitter* e = nf::raw(s.emitter);
    if (e->tag == tag) return;
    e->tag = tag;
    ParticleData* d = RParticleLibrary_FindSystemViaTag(g_manager->library, tag);
    const uint32_t scaled = (d->count * g_density) >> 8;
    s.scaled_count = scaled;
    s.rate = CalcEmitVal(int32_t(scaled), int32_t(d->life), int32_t(d->on_time), int32_t(d->off_time));
    s.data = d;
    s.index = d->index;
}

// [driving] 0x001e65c0 RParticleSystem::Update
void RParticleSystem_Update(RParticleSystem& s, int32_t ticks) {
    if (s.active == 0 || !s.emitter) return;
    const float* src = (s.flags & 1) ? nf::raw(s.emitter)->pos : nf::p32_ptr<const float>(s.source);
    // A diferença (de onde criou por último) - (agora), na pilha: o w é o lixo da pilha (só o xyz é lido).
    float delta[3];
    delta[0] = ee::sub(s.last[0], src[0]);
    delta[1] = ee::sub(s.last[1], src[1]);
    delta[2] = ee::sub(s.last[2], src[2]);
    s.last[0] = src[0];
    s.last[1] = src[1];
    s.last[2] = src[2];
    const ParticleData* d = nf::raw(s.data);
    if (d->off_time != 0 && d->on_time < s.time) {
        const uint32_t cycle = d->on_time + d->off_time;
        if (cycle < s.time) {
            s.time = s.time - cycle;
            if (d->flags & 2) s.active = 0;
            return;
        }
        s.time += uint32_t(ticks);
        return;
    }
    ParticleData* type = RParticleLibrary_FindSystemViaIndex(g_manager->library, s.index);
    const float fticks = ee::cvt_s(ticks);
    const float rate = ee::mul(s.rate, s.rate_scale);
    s.spawns = ee::add(s.spawns, ee::mul(rate, fticks));
    if (ee::lt(0.0f, s.lod) && ee::lt(ee::div(type->max_size, s.lod), fb(0x3b83126f))) {  // 0,004
        s.next_spawn = s.spawns;
        s.time += uint32_t(ticks);
        return;
    }
    while (ee::lt(s.next_spawn, s.spawns)) {
        Particle* p = RParticleParticleCache_Spawn(g_manager->cache);
        if (p) {
            const float a = ee::div(ee::div(ee::sub(s.spawns, s.next_spawn), rate), fticks);
            ResetParticle(a, fticks, *p, src, *type, *nf::raw(s.emitter), s.axis,
                          s.velocity ? nf::p32_ptr<const float>(s.velocity) : nullptr, delta);
        }
        s.next_spawn = ee::add(s.next_spawn, 1.0f);
    }
    s.time += uint32_t(ticks);
}

// --------------------------------------------------------------------------------------------------------- o gerente

void RParticleSystemManager_Init() {
    // [driving] 0x001e6e88 RParticleSystemManager::Init (o contexto de texturas "data\render\partic01" fica com o
    // desenho) e 0x001e6f78 o construtor: o cache (0x001f34d8: as listas do EAGL::DrawArray ficam com o desenho), o
    // mapa vazio e o Reload da biblioteca.
    if (!g_manager) g_manager = new RParticleSystemManager();
    RParticleSystemManager* m = g_manager;
    m->last_tick = 0;
    m->systems.clear();
    m->cache = RParticleParticleCache{};
    m->lists.clear();
    RParticleLibrary_Reload(m->library);
}

void RParticleSystemManager_Shutdown() {
    if (!g_manager) return;
    for (auto& kv : g_manager->systems) {
        RParticleSystem_Destruct(*kv.second);
        delete kv.second;
    }
    delete g_manager;
    g_manager = nullptr;
}

// [driving] 0x001e76c8 RParticleSystemManager::Reset
void RParticleSystemManager_Reset(RParticleSystemManager& m) {
    m.cache.count = 0;
    m.cache.spawned = 0;
    for (auto& kv : m.systems) {
        RParticleSystem_Destruct(*kv.second);
        delete kv.second;
    }
    m.systems.clear();
}

// [driving] 0x001e61f8 RParticleSystemManager::AddOrRefresh
RParticleSystem* RParticleSystemManager_AddOrRefresh(RParticleSystemManager& m, uint32_t key, ParticleEmitter* emitter,
                                                     ParticleCreator creator, float lod, bool* created) {
    RParticleSystem* s;
    auto it = m.systems.find(key);
    const bool found = it != m.systems.end();
    if (found) {
        s = it->second;
    } else {
        s = new RParticleSystem();
        if (creator == ParticleCreator::kMovable) {
            // [driving] 0x001e6078 RMovableParticleSystem::RMovableParticleCreator::Create
            RMovableParticleSystem_Construct(*s, lod, *emitter);
        } else {
            // [driving] 0x001e6028 RParticleSystem::RParticleCreator::Create
            RParticleSystem_Construct(*s, lod, emitter, true);
        }
        m.systems.emplace(key, s);
    }
    if (created) *created = !found;
    s->lod = lod;
    s->keep_alive = 0x1e;
    return s;
}

// [driving] 0x001e62e0 RParticleSystemManager::GetSystem
RParticleSystem* RParticleSystemManager_GetSystem(RParticleSystemManager& m, uint32_t key) {
    auto it = m.systems.find(key);
    return it == m.systems.end() ? nullptr : it->second;
}

// [driving] 0x001e67e0 RParticleSystemManager::UpdateSpawnAllSystems
void RParticleSystemManager_UpdateSpawnAllSystems(RParticleSystemManager& m) {
    const uint32_t now = rtime_data().time;
    uint32_t ticks = 3;
    if (now - m.last_tick < 4) ticks = now - m.last_tick;
    if (ticks == 0) return;
    m.last_tick = now;
    for (auto it = m.systems.begin(); it != m.systems.end();) {
        RParticleSystem* s = it->second;
        if (--s->keep_alive < 1) {
            // A entrada +0xc da tabela virtual (o destrutor) com 3 e o nó apagado da árvore.
            RParticleSystem_Destruct(*s);
            delete s;
            it = m.systems.erase(it);
            continue;
        }
        RParticleSystem_Update(*s, int32_t(ticks));  // a entrada +0x14
        ++it;
    }
}

// [driving] 0x001e6330 RParticleSystemManager::CreateParticles
void RParticleSystemManager_CreateParticles(RParticleSystemManager& m, ParticleData& data, const float pos[3],
                                            const float dir[3], int32_t count) {
    float d[3] = {dir[0], dir[1], dir[2]};
    float up[3] = {0.0f, 1.0f, 0.0f};  // 0x3908c0
    const float k = fb(0x3dcccccd);    // 0,1
    bool swap = false;
    if (ee::lt(ee::abs(d[0]), k) || ee::lt(1.0f, ee::abs(ee::div(d[1], d[0])))) {
        if (ee::lt(ee::abs(d[2]), k) || ee::lt(1.0f, ee::abs(ee::div(d[1], d[2])))) swap = true;
    }
    if (swap) {
        up[0] = 1.0f;
        up[1] = 0.0f;
        up[2] = 0.0f;
        if (ee::lt(d[1], k)) {
            d[0] = 0.0f;
            d[1] = 0.0f;
            d[2] = 1.0f;
        }
    }
    // O lado = |d x up|, o eixo = |lado x d| (VOPMULA/VOPMSUB e o VRSQRT).
    auto cross_norm = [](const float a[3], const float b[3], float out[3]) {
        uint32_t va[4] = {}, vb[4] = {}, acc[4] = {}, c[4] = {}, sq[4] = {}, one[4] = {bf(1.0f), 0, 0, 0};
        load3(va, a);
        load3(vb, b);
        opmula(acc, va, vb);
        opmsub(c, acc, vb, va);
        mul(sq, c, c, XYZ);
        uint32_t acc2[4] = {};
        mulabc(acc2, c, c, BX, X);
        maddabc(acc2, one, sq, BY, X);
        uint32_t len2[4] = {};
        maddbc(len2, acc2, one, sq, BZ, X);
        const uint32_t q = ps2::rsqrt(bf(1.0f), len2[0]);
        out[0] = fb(ps2::mul(c[0], q));
        out[1] = fb(ps2::mul(c[1], q));
        out[2] = fb(ps2::mul(c[2], q));
    };
    ParticleEmitter e{};  // na pilha: só o lado (+0x20), a direção (+0x30) e o -1 de +0x3c são lidos
    float axis[3];
    cross_norm(d, up, e.side);
    cross_norm(e.side, d, axis);
    e.dir[0] = dir[0];
    e.dir[1] = dir[1];
    e.dir[2] = dir[2];
    e.sticky = -1;
    const float zero[3] = {0.0f, 0.0f, 0.0f};  // 0x335270
    for (int32_t i = count; i > 0; --i) {
        Particle* p = RParticleParticleCache_Spawn(m.cache);
        if (p) ResetParticle(0.0f, 0.0f, *p, pos, data, e, axis, dir, zero);
    }
}

// [driving] 0x001e6e48 RParticleSystemManager::UpdateAndRenderAllSystems (0x001f47f8 RParticleParticleCache::
// UpdateAndRender: até 3 JustUpdate pelos tiques do RTimeData, depois o JustRender)
void RParticleSystemManager_UpdateAndRenderAllSystems(RParticleSystemManager& m) {
    RParticleParticleCache& c = m.cache;
    const uint32_t d = rtime_data().time - c.last_time;
    uint32_t n = 3;
    if (d < 4) n = d;
    for (; int32_t(n) > 0; --n) RParticleParticleCache_JustUpdate(c);
    RParticleParticleCache_JustRender(m);
    c.last_time = rtime_data().time;
}

// ----------------------------------------------------------------------------------------------------- a partícula

// [driving] 0x001f29d8 RParticleSystem::RParticle::ResetParticle (a ordem das contas é a do MIPS)
void ResetParticle(float a, float ticks, Particle& p, const float source[3], const ParticleData& data,
                   const ParticleEmitter& emitter, const float axis[3], const float* velocity, const float delta[3]) {
    const float k16 = fb(0x37800000);  // 1/65536
    const float at = ee::mul(a, ticks);  // f22
    p.pos[0] = source[0];
    p.pos[1] = source[1];
    p.pos[2] = source[2];
    // O espalhamento em x e z.
    {
        const float r1 = ee::cvt_s(int32_t(rrandom()));
        const float r2 = ee::cvt_s(int32_t(rrandom()));
        const float r3 = ee::cvt_s(int32_t(rrandom()));
        float t = ee::mul(ee::mul(r1, data.spread_x), k16);
        p.pos[0] = ee::add(p.pos[0], ee::add(ee::neg(data.spread_x), ee::add(t, t)));
        t = ee::mul(ee::mul(r2, data.spread_z), k16);
        p.pos[2] = ee::add(p.pos[2], ee::add(ee::neg(data.spread_z), ee::add(t, t)));
        // A velocidade ao longo da direção do emissor (vmulx.xyz: o w da partícula, +0x1c..+0x1f, fica).
        t = ee::mul(ee::mul(r3, data.speed_spread), k16);
        const float sp = ee::add(ee::sub(data.speed, data.speed_spread), ee::add(t, t));
        p.vel[0] = ee::mul(emitter.dir[0], sp);
        p.vel[1] = ee::mul(emitter.dir[1], sp);
        p.vel[2] = ee::mul(emitter.dir[2], sp);
    }
    {
        const float r = ee::cvt_s(int32_t(rrandom()));
        float t = ee::mul(ee::mul(r, data.side_speed), k16);
        const float s = ee::add(ee::neg(data.side_speed), ee::add(t, t));
        p.vel[0] = ee::add(p.vel[0], ee::mul(emitter.side[0], s));
        p.vel[1] = ee::add(p.vel[1], ee::mul(emitter.side[1], s));
        p.vel[2] = ee::add(p.vel[2], ee::mul(emitter.side[2], s));
    }
    {
        const float r = ee::cvt_s(int32_t(rrandom()));
        float t = ee::mul(ee::mul(r, data.axis_speed), k16);
        const float s = ee::add(ee::neg(data.axis_speed), ee::add(t, t));
        p.vel[0] = ee::add(p.vel[0], ee::mul(axis[0], s));
        p.vel[1] = ee::add(p.vel[1], ee::mul(axis[1], s));
        p.vel[2] = ee::add(p.vel[2], ee::mul(axis[2], s));
    }
    if (velocity) {
        p.vel[0] = ee::add(p.vel[0], ee::div(velocity[0], 60.0f));
        p.vel[1] = ee::add(p.vel[1], ee::div(velocity[1], 60.0f));
        p.vel[2] = ee::add(p.vel[2], ee::div(velocity[2], 60.0f));
    }
    {
        const float h = ee::lt(at, 0.0f) ? -0.5f : 0.5f;
        const int32_t n = ee::cvt_w(ee::add(h, at));
        uint8_t life = uint8_t(uint32_t(data.life & 0xff) - uint32_t(n));
        p.life = life == 0 ? 1 : life;
    }
    {
        const float r = ee::cvt_s(int32_t(rrandom()));
        float t = ee::mul(ee::mul(r, data.gravity_spread), k16);
        const float g = ee::add(ee::sub(data.gravity, data.gravity_spread), ee::add(t, t));
        p.gravity = int8_t(ee::cvt_w(g));
    }
    {
        const float r = ee::cvt_s(int32_t(rrandom()));
        float t = ee::mul(ee::mul(r, data.spin), k16);
        const float s = ee::add(ee::neg(data.spin), ee::add(t, t));
        p.spin = int8_t(ee::cvt_w(ee::mul(s, fb(0x4222cccd))));  // x 40,7
    }
    p.type = uint8_t(data.index);
    p.sticky = int8_t(uint8_t(emitter.sticky));
    p.nibbles = data.alpha_byte;
    p.drag = uint8_t(ee::cvt_w(ee::mul(data.drag, 255.0f)));
    p.flags = uint8_t(data.flags);
    if ((data.flags & 0x100) && data.alpha_byte != 0) p.flags = uint8_t(data.flags) | 0x40;
    else p.flags = p.flags & 0xbf;
    if (data.flags & 0x10) {
        // O giro pela câmera: a velocidade na câmera (o RRenderer +0x78, a matriz de 0x297480) e o atan2f dela.
        const float in[4] = {p.vel[0], p.vel[1], p.vel[2], 0.0f};
        float out[4];
        TransformPoint(g_view, in, out);
        float t = ee::sub(ee::mul(nf::fm::atan2(out[0], out[1]), fb(0x3e22f983)), 0.125f);
        if (ee::lt(t, -0.5f)) t = ee::add(t, 1.0f);
        p.spin = int8_t(ee::cvt_w(ee::mul(ee::mul(t, fb(0x40c90fdb)), fb(0x4222cccd))));
    }
    if (ee::lt(0.0f, data.alpha_spread)) {
        p.flags &= 0xbf;
        float lo = data.colour[2][0];
        const float c0 = data.colour[0][0];
        int32_t span = ee::cvt_w(ee::sub(c0, lo));
        if (span < 0) span = -span;
        if (ee::lt(c0, lo)) lo = c0;
        const uint32_t r = (render_random().seed * render_random().multiplier);
        render_random().seed = r & 0xffffu;
        const float rn = ee::cvt_s(int32_t((r >> 8) & 0xffffu));
        const float f = ee::sub(1.0f, ee::mul(ee::mul(rn, data.alpha_spread), fb(0x37800000)));
        const float v = ee::mul(ee::add(lo, ee::mul(ee::cvt_s(span), f)), fb(0x426b4b4c));  // x 58,823532
        p.nibbles = uint8_t(ee::cvt_w(v) << 4);
        p.flags &= 0xbf;
    }
    if (ee::lt(0.0f, data.size_spread)) {
        p.flags &= 0xbf;
        const uint32_t r = (render_random().seed * render_random().multiplier);
        int32_t span = ee::cvt_w(data.size_change);
        const float base = ee::add(data.size, data.size_change);
        if (span < 0) span = -span;
        render_random().seed = r & 0xffffu;
        const float rn = ee::cvt_s(int32_t((r >> 8) & 0xffffu));
        const float f = ee::sub(1.0f, ee::mul(ee::mul(rn, data.size_spread), fb(0x37800000)));
        const float v = ee::mul(ee::add(base, ee::mul(ee::cvt_s(span), f)), 1.5f);
        const uint8_t b = uint8_t(p.nibbles | (ee::cvt_w(v) & 0xf));
        p.nibbles = b <= 1 ? 1 : b;
        p.flags &= 0xbf;
    }
    // A posição no instante a do passo.
    const float am1 = ee::sub(a, 1.0f);
    p.pos[0] = ee::add(ee::add(p.pos[0], ee::mul(delta[0], a)), ee::mul(ee::mul(p.vel[0], ticks), am1));
    const float g = ee::mul(ee::mul(ee::mul(ee::cvt_s(p.gravity), fb(0xb9800000)), at), at);
    p.pos[1] = ee::add(ee::add(ee::add(p.pos[1], ee::mul(delta[1], a)), ee::mul(ee::mul(p.vel[1], ticks), am1)), g);
    p.pos[2] = ee::add(ee::add(p.pos[2], ee::mul(delta[2], a)), ee::mul(ee::mul(p.vel[2], ticks), am1));
    ++g_stats.spawned;
}

// --------------------------------------------------------------------------------------------------------- o cache

// [driving] 0x001f3bb8 RParticleParticleCache::Spawn
Particle* RParticleParticleCache_Spawn(RParticleParticleCache& c) {
    const int32_t i = c.spawned;
    if (c.count + i < 0x400) {
        c.spawned = i + 1;
        return &c.particles[1 - c.current][i];
    }
    ++g_stats.refused;
    return nullptr;
}

// [driving] 0x001f3c20 RParticleParticleCache::SaveForNextFrame (sem conferir o limite, como o original: o
// UpdateBlock só guarda as que já cabiam)
void RParticleParticleCache_SaveForNextFrame(RParticleParticleCache& c, const Particle& p) {
    const int32_t i = c.spawned;
    if (i < 0 || i >= 0x400) {
        ++g_stats.guards;
        return;
    }
    c.particles[1 - c.current][i] = p;
    c.spawned = i + 1;
}

// [driving] 0x001f3ca8 RParticleParticleCache::UpdateBlock: sobre o scratchpad que o Upload (0x001f4760, um DMA dos 512
// lugares do bloco) encheu; o port copia o bloco para um scratchpad seu (o quadro corrente fica como estava, como no PS2).
void RParticleParticleCache_UpdateBlock(RParticleParticleCache& c, int32_t block) {
    static Particle scratch[0x200];
    std::memcpy(scratch, &c.particles[c.current][block * 0x200], sizeof scratch);
    int32_t n = 0x200;
    if ((c.count >> 9) <= block) n = ((c.count - 1) & 0x1ff) + 1;
    Particle* p = scratch;
    for (; n != 0; --n, ++p) {
        const float g = ee::mul(ee::cvt_s(p->gravity), fb(0xb9800000));
        const ParticleData* d = RParticleLibrary_FindSystemViaIndex(g_manager->library, p->type);
        const float drag = d->drag;  // 0x3e3f50
        p->pos[0] = ee::add(p->pos[0], p->vel[0]);
        p->pos[1] = ee::add(p->pos[1], p->vel[1]);
        p->pos[2] = ee::add(p->pos[2], p->vel[2]);
        p->vel[0] = ee::mul(p->vel[0], drag);
        p->vel[1] = ee::mul(p->vel[1], drag);
        p->vel[2] = ee::mul(p->vel[2], drag);
        p->vel[1] = ee::add(p->vel[1], g);
        p->life = uint8_t(p->life - 1);
        if (p->life != 0) RParticleParticleCache_SaveForNextFrame(c, *p);
    }
}

// [driving] 0x001f4878 RParticleParticleCache::JustUpdate (o Upload 0x001f4760 e o Download vazio 0x001f47f0)
void RParticleParticleCache_JustUpdate(RParticleParticleCache& c) {
    int32_t block = 0;
    for (int32_t left = c.count; left > 0; left -= 0x200) {
        c.uploaded = block;
        RParticleParticleCache_UpdateBlock(c, block);
        ++block;
        c.uploaded = -1;
    }
    c.count = c.spawned;
    c.spawned = 0;
    c.current = 1 - c.current;
}

namespace {

// A cor de 4 floats (ARGB, 0..1) x 255 em RGBA de 8 bits (vmulx, vftoi0 e o ppach/prevh/pexeh/ppacb).
uint32_t pack_colour(const uint32_t c[4]) {
    uint32_t v[4], i[4];
    const uint32_t k[4] = {bf(255.0f), 0, 0, 0};
    mulbc(v, c, k, BX, XYZW);
    for (int l = 0; l < 4; ++l) i[l] = uint32_t(ps2::to_int(v[l]));
    // x = A, y = R, z = G, w = B: o byte 0 sai R, o 1 G, o 2 B, o 3 A.
    return (i[1] & 0xff) | (i[2] & 0xff) << 8 | (i[3] & 0xff) << 16 | (i[0] & 0xff) << 24;
}

}  // namespace

// [driving] 0x001f3dc8 RParticleParticleCache::RenderBlock: as três listas do bloco (no PS2 sobre o scratchpad: as
// gravações na partícula, o -1 do grudado que sumiu, ficam lá e se perdem; o port trabalha numa cópia).
void RParticleParticleCache_RenderBlock(RParticleSystemManager& m, int32_t block, int32_t /*frame*/) {
    RParticleParticleCache& c = m.cache;
    if (!(block << 9 < c.count)) return;
    int32_t n = 0x200;
    if ((c.count >> 9) <= block) n = ((c.count - 1) & 0x1ff) + 1;
    ParticleDrawLists::Sprites& nl = m.lists.normal[block];
    ParticleDrawLists::Sprites& al = m.lists.additive[block];
    nl = ParticleDrawLists::Sprites{};
    al = ParticleDrawLists::Sprites{};
    m.lists.tri_coords[block].clear();
    m.lists.tri_colours[block].clear();
    for (int32_t i = 0; i < n; ++i) {
        Particle p = c.particles[c.current][block * 0x200 + i];
        const ParticleData* d = RParticleLibrary_FindSystemViaIndex(m.library, p.type);
        const float t = ee::mul(ee::cvt_s(int32_t(p.life)), d->inv_life);  // f20
        float t2 = ee::add(t, t);
        int k = 0;
        if (ee::lt(1.0f, t2)) {
            t2 = ee::sub(t2, 1.0f);
            k = 1;
        }
        uint32_t c0[4], c1[4], diff[4], col[4];
        load(c0, d->colour[k], XYZW);
        load(c1, d->colour[k + 1], XYZW);
        sub(diff, c1, c0, XYZW);
        const uint32_t tt[4] = {bf(t2), 0, 0, 0};
        mulbc(diff, diff, tt, BX, XYZW);
        add(col, c0, diff, XYZW);
        if (d->flags & 0x40) col[0] = bf(ee::mul(ee::cvt_s(int32_t(p.nibbles >> 4)), fb(0x3d800000)));
        const uint32_t rgba = pack_colour(col);
        if (d->flags & 0x20) {
            // O risco: três vértices (a posição andada pela velocidade x o +0x38, com o +0x34 em y; a mesma; a
            // posição), w 1; com o grudado (aqui um corpo rígido), a posição do corpo somada aos três.
            float moved[3];
            moved[0] = ee::add(p.pos[0], ee::mul(p.vel[0], d->size_change));
            moved[1] = ee::add(p.pos[1], ee::mul(p.vel[1], d->size_change));
            moved[2] = ee::add(p.pos[2], ee::mul(p.vel[2], d->size_change));
            std::vector<float>& tc = m.lists.tri_coords[block];
            const size_t base = tc.size();
            tc.insert(tc.end(), {moved[0], ee::add(moved[1], d->size), moved[2], 1.0f});
            tc.insert(tc.end(), {moved[0], moved[1], moved[2], 1.0f});
            tc.insert(tc.end(), {p.pos[0], p.pos[1], p.pos[2], 1.0f});
            for (int v = 0; v < 3; ++v) m.lists.tri_colours[block].push_back(rgba);
            if (uint8_t(p.sticky) != 0xff) {
                float body[4];
                if (g_rigid && g_rigid(uint8_t(p.sticky), body)) {
                    for (int v = 0; v < 3; ++v)
                        for (int l = 0; l < 3; ++l) tc[base + v * 4 + l] = ee::add(tc[base + v * 4 + l], body[l]);
                } else {
                    ++g_stats.guards;
                }
            }
            ++g_stats.streaks;
            continue;
        }
        ParticleDrawLists::Sprites& l = (d->flags & 0x200) ? al : nl;
        l.colours.push_back(rgba);
        float uv[4] = {d->uv[0], d->uv[1], d->uv[2], d->uv[3]};
        if (p.flags & 8) {
            if (p.flags & 0x10) uv[3] = ee::div(ee::cvt_s(p.spin), fb(0x4222cccd));
            else uv[3] = ee::div(ee::mul(t, ee::cvt_s(p.spin)), fb(0x4222cccd));
        } else {
            uv[3] = fb(0xbf490fdb);  // -pi/4
        }
        l.uvuv.insert(l.uvuv.end(), uv, uv + 4);
        // A posição: o aditivo com w 1, o normal com o w dos bytes +0xc..+0xf (os dois trocados pelo tamanho depois).
        uint32_t pos[4];
        load3(pos, p.pos);
        if (d->flags & 0x200) {
            pos[3] = bf(1.0f);
        } else {
            std::memcpy(&pos[3], reinterpret_cast<const uint8_t*>(&p) + 0xc, 4);
        }
        if (uint8_t(p.sticky) != 0xff) {
            float s[4];
            if (g_sticky && g_sticky(uint8_t(p.sticky), s)) {
                uint32_t sv[4];
                load(sv, s, XYZW);
                add(pos, pos, sv, XYZW);
            }
            // (sem a posição: o PS2 grava 0xff no +0x1e da cópia no scratchpad, que se perde)
        }
        float size;
        if (d->flags & 0x80) size = ee::div(ee::cvt_s(int32_t(p.nibbles & 0xf)), 1.5f);
        else size = ee::add(d->size, ee::mul(t, d->size_change));
        pos[3] = bf(size);
        float f[4];
        store(f, pos, XYZW);
        l.coords.insert(l.coords.end(), f, f + 4);
        ++((d->flags & 0x200) ? g_stats.additive : g_stats.sprites);
    }
}

// [driving] 0x001f4920 RParticleParticleCache::JustRender: o Control do RenderSpritesRot (66 / (o +0xa4 da câmera x
// o +0x50 da vista), e x 1,4e7) fica com o desenho; aqui os blocos e a troca do quadro duplo.
void RParticleParticleCache_JustRender(RParticleSystemManager& m) {
    RParticleParticleCache& c = m.cache;
    m.lists.clear();
    int32_t block = 0;
    for (int32_t left = c.count; left > 0; left -= 0x200) {
        c.uploaded = block;
        RParticleParticleCache_RenderBlock(m, block, g_toggle);
        c.uploaded = -1;
        ++block;
    }
    g_toggle = 1 - g_toggle;
}

}  // namespace drv
