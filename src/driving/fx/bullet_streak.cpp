// Os riscos das balas e os clarões dos tiros (driving/fx/bullet_streak.h).

#include "driving/fx/bullet_streak.h"

#include <cstring>

#include "driving/eagl_state.h"
#include "driving/fx/fx_lights.h"
#include "driving/game/ai_character.h"
#include "driving/game/explosion.h"
#include "driving/game/shells.h"
#include "driving/physics/simple_body.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace {

using namespace ps2::vu;
constexpr uint32_t kOne = 0x3f800000u;

BulletStreakStats g_stats;

// O índice "BulletStreakType" (0x3349b0: os sete nomes).
const char* const kTypeNames[kBulletStreakTypes] = {"Henchmen", "Vanquish", "SuperSnowmobile", "SUV",
                                                     "Captain",  "Ultralight", "Sentinel"};

// vmul.xyz t, v, v; vmulax.x ACC, v, v; vmadday.x ACC, one, t; vmaddz.x t, one, t: |v|² no x.
uint32_t len2(const Reg v) {
    Reg t{}, acc{};
    const Reg one = {kOne, 0, 0, 0};
    mul(t, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, t, BY, X);
    maddbc(t, acc, one, t, BZ, X);
    return t[0];
}
// ... vrsqrt Q, vf0w, t; vwaitq; vmulq.xyz d, v, Q (o w de d fica).
void unit(Reg d, const Reg v) {
    const uint32_t q = ps2::rsqrt(kOne, len2(v));
    muls(d, v, q, XYZ);
}

}  // namespace

BulletStreakState& bullet_streak() {
    static BulletStreakState s;
    return s;
}
BulletStreakStats& bullet_streak_stats() { return g_stats; }

// [driving] 0x002cb860 UVolatileMaterial::UVolatileMaterial
void UVolatileMaterial_Construct(uint8_t* m) {
    eagl::GeoPrimState_Init(m);
    uint64_t v;
    std::memcpy(&v, m + 0x30, 8);
    v &= ~uint64_t(1);  // o TEST_1 sem o teste de alfa
    std::memcpy(m + 0x30, &v, 8);
    std::memcpy(&v, m + 0x10, 8);
    v = (v & 0xffffffffffffffa7ull) | 0x58;  // o PRIM: Gouraud, textura, mistura
    std::memcpy(m + 0x10, &v, 8);
    v = 0x8000000044ull;  // o ALPHA_1: (Cs - Cd) As + Cd
    std::memcpy(m + 0x40, &v, 8);
    std::memset(m + 0x80, 0, 0x10);
}

// [driving] 0x001d82e0 (o construtor estático: o caminho de criação, 1 e 0xffff)
void RBulletStreak_StaticInit(BulletStreakState& s) {
    // (o PS2 também põe 0x3d76f8 = 0x800000 e 0x3d76fc = 0x7e800000: o mínimo e o máximo do float, não lidos aqui)
    for (int i = 0; i < kStreakShapes; ++i) UVolatileMaterial_Construct(s.material[i]);
    std::memset(s.colours, 0, sizeof s.colours);
}

// [driving] 0x001d7620 RBulletStreak::Init
void RBulletStreak_Init(BulletStreakState& s, const TuningReader& fx) {
    for (int i = 0; i < kStreakShapes; ++i) {
        // RTextureContext::FindOrCreateTexture(contexto 0, 'wtrc'/'ytrc'/'otrc'/'btrc'): o port guarda a forma.
        s.texture[i] = uint32_t(i);
        s.texture_flags[i] &= 0xf9ffffffu;
        s.texture_flags[i] = (s.texture_flags[i] & 0xfffffff0u) | 5u;
        for (int j = 0; j < kStreakVerts; ++j) {
            s.colours[i][j] = 0x80808080u;
            float u = 0.0f, v = 0.0f;
            switch (j % 6) {
                case 0:
                case 5: u = 0.0f, v = 1.0f; break;
                case 1: u = 0.0f, v = 0.0f; break;
                case 2:
                case 3: u = 1.0f, v = ps2::ffloat(0x3dcccccd); break;
                case 4: u = 1.0f, v = 1.0f; break;
            }
            s.uv[i][j][0] = u;
            s.uv[i][j][1] = v;
            s.coords[i][j][3] = 1.0f;
        }
        uint64_t prim;
        std::memcpy(&prim, s.material[i] + 0x10, 8);
        prim |= 8;
        s.count[i] = 0;
        prim |= 0x10;
        const uint64_t alpha = 0x8000000048ull;  // Cs As + Cd
        std::memcpy(s.material[i] + 0x40, &alpha, 8);
        prim |= 0x40;
        std::memcpy(s.material[i] + 0x10, &prim, 8);
    }
    // dbindex("BulletStreakType", &índice, 0, 6, os nomes) e os seis atributos com o passo 0x18.
    TuningIndex ix;
    ix.lo = 0;
    ix.hi = kBulletStreakTypes - 1;
    ix.names = kTypeNames;
    uint8_t* t = reinterpret_cast<uint8_t*>(s.types);
    dbattrib_colour(fx, &ix, "BulletStreakColour", t + 0x00, 0x18);
    dbattrib_int(fx, &ix, "BulletStreakTexture", t + 0x04, 0x18);
    dbattrib_float(fx, &ix, "BulletStreakWidth", t + 0x08, 0x18);
    dbattrib_float(fx, &ix, "BulletStreakLength", t + 0x0c, 0x18);
    dbattrib_float(fx, &ix, "BulletStreakSpeed", t + 0x10, 0x18);
    dbattrib_int(fx, &ix, "BulletStreakCount", t + 0x14, 0x18);
    s.ready = true;
}

// [driving] 0x001d7d78 RBulletStreak::Add(COORD4 &, COORD4 &, StreakType)
void RBulletStreak_Add(BulletStreakState& s, const float eye[4], const float p4[4], const float d4[4], int32_t type) {
    const BulletStreakType& ty = s.types[type];
    const int32_t shape = ty.shape;
    int32_t& n = s.count[shape];
    if (n >= 0x46) {
        ++g_stats.full;
        return;
    }
    ++g_stats.added;
    Reg e, p, d, a{}, side{}, tail{};
    load(e, eye);
    load(p, p4);
    load(d, d4);
    sub(a, e, p, XYZ);  // o olho - p
    unit(a, a);
    Reg acc{}, c{};
    opmula(acc, a, d);
    opmsub(c, acc, d, a);  // a x d
    unit(c, c);
    const Reg w = {ps2::fbits(ty.width), 0, 0, 0};
    mulbc(side, c, w, BX, XYZ);
    const Reg l = {ps2::fbits(ty.length), 0, 0, 0};
    mulbc(tail, d, l, BX, XYZ);
    const uint32_t col = (ty.colour >> 1) & 0x7f7f7f7fu;
    float (*v)[4] = s.coords[shape];
    auto put = [&](int k, const Reg r) {
        Reg o;
        load(o, v[n + k]);
        move(o, r, XYZ);  // o w do vértice fica (1, do Init)
        store(v[n + k], o);
        s.colours[shape][n + k] = col;
    };
    Reg r{};
    add(r, p, side, XYZ);
    put(0, r);
    sub(r, p, side, XYZ);
    put(1, r);
    Reg q;
    move(q, p);
    sub(q, p, tail, XYZ);  // o rabo (o w de p)
    sub(r, q, side, XYZ);
    put(2, r);
    put(3, r);
    add(r, q, side, XYZ);
    put(4, r);
    add(q, q, tail, XYZ);  // de volta à ponta (p mais o erro do arredondamento)
    add(r, q, side, XYZ);
    put(5, r);
    n += 6;
}

// [driving] 0x001d8048 RBulletStreak::Add(COORD3 &, COORD3 &, StreakType)
void RBulletStreak_Add3(BulletStreakState& s, const float eye[4], const float p[3], const float d[3], int32_t type) {
    const float p4[4] = {p[0], p[1], p[2], 1.0f}, d4[4] = {d[0], d[1], d[2], 1.0f};
    RBulletStreak_Add(s, eye, p4, d4, type);
}

// [driving] 0x002cb938 UVolatileMaterial::Draw
void UVolatileMaterial_Draw(uint8_t* material, int prim, int count, const float (*coords)[4], const float (*uv)[2],
                            const uint32_t* colours, int shape, std::vector<VolatileDraw>* out,
                            const float (*matrix)[4], int texture) {
    const uint32_t c = uint32_t(count);
    std::memcpy(material + 0x80, &c, 4);
    eagl::GeoPrimState_SetPrimType(material, prim);
    // O fgLayerData[fgCur] recebe as contagens e o material; o EAGL::DynamicModel::Draw (sem matriz) manda à VU1.
    if (!out) return;
    VolatileDraw dr;
    dr.shape = shape;
    dr.prim = prim;
    dr.count = count;
    std::memcpy(&dr.alpha, material + 0x40, 8);
    std::memcpy(&dr.test, material + 0x30, 8);
    dr.coords.assign(&coords[0][0], &coords[0][0] + size_t(count) * 4);
    dr.uv.assign(&uv[0][0], &uv[0][0] + size_t(count) * 2);
    dr.colours.assign(colours, colours + count);
    // Com matriz: o SetModelMatrix antes do Draw (e a identidade de volta depois).
    if (matrix) {
        dr.has_matrix = true;
        std::memcpy(dr.matrix, matrix, 64);
    }
    dr.texture = texture;
    out->push_back(std::move(dr));
}

// [driving] 0x001d7c18 RBulletStreak::Draw
void RBulletStreak_Draw(BulletStreakState& s, std::vector<VolatileDraw>* out) {
    // (TIMER_removetask(fgRenderer + 0x78, 0) e (…, 1): o perfil do PS2)
    for (int i = 0; i < kStreakShapes; ++i) {
        if (s.count[i] > 0) {
            ++g_stats.drawn;
            g_stats.vertices += uint64_t(s.count[i]);
            UVolatileMaterial_Draw(s.material[i], 4, s.count[i], s.coords[i], s.uv[i], s.colours[i], int(s.texture[i]),
                                   out);
        }
        s.count[i] = 0;
    }
}

// [driving] 0x001d7d50 RBulletStreak::Frame
void RBulletStreak_Frame(BulletStreakState& s) {
    for (int i = 3; i >= 0; --i) s.count[i] = 0;
}

// [driving] 0x001993e8 RayShell::DrawTracers
void RayShell_DrawTracers(BulletStreakState& s, const float eye[4]) {
    RayShellGlobals& g = rayshell_globals();
    Simulation& sim = simulation();
    const uint32_t steps = uint32_t(sim.steps);
    RayShell_ClearTracers(steps - 0x3c);
    for (int32_t i = 0; i < g.num_tracers; ++i) {
        RayTracer& t = g.tracers[i];
        const int32_t age_i = int32_t(steps - uint32_t(t.step));
        float age;
        if (age_i >= 0) {
            age = ps2::ee::cvt_s(age_i);
        } else {  // cvt.s.w de (v >> 1 | v & 1) e o dobro (o u32 em float)
            const uint32_t u = uint32_t(age_i);
            age = ps2::ee::add(ps2::ee::cvt_s(int32_t(u >> 1 | (u & 1))), ps2::ee::cvt_s(int32_t(u >> 1 | (u & 1))));
        }
        float f = ps2::ee::mul(age, sim.dt);
        if (g.tracer_side[i][0] != 0) {
            const AICharacterHandsData* hands = nf::p32_ptr<AICharacterHandsData>(hands_globals().hands);
            int32_t visible = 0;
            if (hands) visible = hands->visible;
            else ++g_stats.guards;  // o PS2 lê o +0x164 do endereço nulo
            if (visible != 0) {
                f = ps2::ee::mul(f, f);
                // RSceneObj::LocateFX(objeto, efeito, MATRIX4 &, 1): a boca é a linha 3.
                float m[4][4] = {};
                const ShellsEnv& env = shells_env();
                if (env.locate_fx && env.locate_fx(g.tracer_side[i][0], g.tracer_side[i][1], m)) {
                    t.from[0] = m[3][0];
                    t.from[1] = m[3][1];
                    t.from[2] = m[3][2];
                } else {
                    ++g_stats.guards;  // o PS2 leria a pilha sem o efeito: o port deixa a boca
                }
            }
        }
        Reg to{}, from{}, dv{}, u{};
        load(to, t.to, XYZ);
        load(from, t.from, XYZ);
        sub(dv, to, from, XYZ);
        const uint32_t d2 = len2(dv);
        unit(u, dv);
        const BulletStreakType& ty = s.types[t.type];
        const float k = ps2::ee::add(ps2::ee::mul(f, ty.speed), ty.length);
        const Reg kk = {ps2::fbits(k), 0, 0, 0};
        Reg p{}, e{};
        mulbc(p, u, kk, BX, XYZ);
        add(p, from, p, XYZ);
        sub(e, from, p, XYZ);
        const uint32_t e2 = len2(e);
        if (ps2::less(d2, e2)) {  // c.lt.s: a ponta passou do alvo
            RayShell_DeleteTracer(i);
            --i;
            ++g_stats.tracers_done;
        } else {
            ++g_stats.tracers;
            const float pp[3] = {ps2::ffloat(p[0]), ps2::ffloat(p[1]), ps2::ffloat(p[2])};
            const float uu[3] = {ps2::ffloat(u[0]), ps2::ffloat(u[1]), ps2::ffloat(u[2])};
            RBulletStreak_Add3(s, eye, pp, uu, t.type);
        }
    }
}

// [driving] 0x00199678 RayShell::DrawFlashes
void RayShell_DrawFlashes(RLightManagerPositional& lm) {
    RayShellGlobals& g = rayshell_globals();
    if (g.num_flashes <= 0) return;
    const float pos[4] = {g.flashes[0].pos[0], g.flashes[0].pos[1], g.flashes[0].pos[2], 6.0f};
    static const float kColour[4] = {0.2f, 0.2f, 0.2f, 0.2f};  // 0x37bf70 (0x3e4ccccd)
    RLightManager_AddPositionalLight(lm, pos, kColour);
    ++g_stats.flashes;
    for (int32_t i = 1; i < g.num_flashes; ++i) g.flashes[i - 1] = g.flashes[i];
    --g.num_flashes;
}

// [driving] 0x001c5aa8 RRenderWorldCamera::DrawBulletStreaks (o laço das balas lentas)
void DrawBulletStreaks_Shells(BulletStreakState& s, const float eye[4]) {
    Simulation& sim = simulation();
    for (PhysicsObject* po : sim.shells) {
        Shell* sh = dynamic_cast<Shell*>(po);
        if (!sh) continue;
        SimpleRigidBody* b = GetSimpleRigidBody(sim, sh->po.slot);
        if (!b) {
            ++g_stats.guards;
            continue;
        }
        Reg v{}, u{};
        load(v, b->vel, XYZ);
        unit(u, v);
        if (sh->d.streak < 0) continue;
        ++g_stats.shells;
        const float uu[3] = {ps2::ffloat(u[0]), ps2::ffloat(u[1]), ps2::ffloat(u[2])};
        RBulletStreak_Add3(s, eye, b->pos, uu, sh->d.streak);
    }
}

}  // namespace drv
