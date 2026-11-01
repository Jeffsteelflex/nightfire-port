// Os clarões (driving/fx/glare.h).

#include "driving/fx/glare.h"

#include <cmath>
#include <cstring>

#include "driving/eagl_state.h"
#include "driving/game/target_picker.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace {

using namespace ps2::vu;
namespace ee = ps2::ee;
constexpr uint32_t kOne = 0x3f800000u;

GlareStats g_stats;

// O u32 em float como o GCC do PS2 o converte (cvt.s.w; se negativo, (v >> 1 | v & 1) e o dobro).
float u32_to_float(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t(v >> 1 | (v & 1)));
    return ee::add(h, h);
}
// __fixunssfsi (0x0024e340): o float sem sinal em u32 (truncando).
uint32_t f_to_u32(float f) {
    if (ee::lt(f, 2147483648.0f)) return uint32_t(ee::cvt_w(f));
    return uint32_t(ee::cvt_w(ee::sub(f, 2147483648.0f))) ^ 0x80000000u;
}
// vmul.xyz; vmulax.x; vmadday.x; vmaddz.x: |v|²
uint32_t len2(const Reg v) {
    Reg t{}, acc{};
    const Reg one = {kOne, 0, 0, 0};
    mul(t, v, v, XYZ);
    mulabc(acc, v, v, BX, X);
    maddabc(acc, one, t, BY, X);
    maddbc(t, acc, one, t, BZ, X);
    return t[0];
}

}  // namespace

GlareGlobals& glare_globals() {
    static GlareGlobals g;
    return g;
}
RGlareManager& glare_manager() {
    static RGlareManager m{};
    return m;
}
GlareStats& glare_stats() { return g_stats; }
GlareJitter& glare_jitter() {
    static GlareJitter j;
    return j;
}

// [driving] 0x0024b328 GSystem::RANDOM_Startup
void GSystem_RANDOM_Startup(uint32_t n) {
    GlareJitter& j = glare_jitter();
    j.mul = 0x075bcd15u;
    j.seed = 0x3ade68b1u;
    uint32_t k = n % 500u;
    if (k == 0) return;
    uint32_t v = 0x3ade68b1u;
    while (k--) v = (v * 0x075bcd15u) & 0xffffu;
    j.seed = v;
}

// [driving] 0x001f14b0 RGlareManager::RGlareManager
void RGlareManager_Construct(RGlareManager& m, const TuningReader& tun) {
    std::memset(&m, 0, sizeof m);
    m.count = 0;
    m.vtable = 0x394f68;
    m.enabled = 1;
    // RTextureContext::FindOrCreateTexture(contexto 0, 'flar') (0x339000; o port: a textura do PC) e o GeoPrimState
    // 0x339010 (o construtor estático 0x001f26a8 o constrói): lista de triângulos, Cs As + Cd, Gouraud + textura +
    // mistura, o teste de alfa 0x70.
    GlareGlobals& gg = glare_globals();
    eagl::GeoPrimState_Init(gg.state);
    eagl::GeoPrimState_SetPrimType(gg.state, 4);
    const uint64_t alpha = 0x8000000048ull;
    std::memcpy(gg.state + 0x40, &alpha, 8);
    uint64_t prim;
    std::memcpy(&prim, gg.state + 0x10, 8);
    prim = (prim & 0xffffffffffffffa7ull) | 0x58;
    std::memcpy(gg.state + 0x10, &prim, 8);
    // [driving] 0x001a0f30 Draw::SetAlphaTest(GeoPrimState *, ref): ATE 1, ATST 6 (GEQUAL), AREF, AFAIL 3 (só RGB).
    uint64_t test;
    std::memcpy(&test, gg.state + 0x30, 8);
    test = (test & ~uint64_t(1)) | 1;
    test = (test & ~uint64_t(0xff0)) | (uint64_t(0x70) << 4);
    test = (test & ~uint64_t(0xe)) | 0xc;
    test = (test & ~uint64_t(0x3000)) | 0x3000;
    std::memcpy(gg.state + 0x30, &test, 8);
    for (int i = 0; i < 4; ++i) m.parameters[i] = 0;
    // (os dois EAGL::DrawArray 0x3dfeb8: SetGeoPrim, SetPrimitiveType 4 e os parâmetros: o port monta a lista no
    // DrawGlares)
    for (int i = 0x4f; i >= 0; --i) m.types[i].zbias = 2.0f;
    m.types[0x34].zbias = ps2::ffloat(0x3e99999a);
    m.types[0x21].zbias = ps2::ffloat(0x3e99999a);
    m.types[0x22].zbias = ps2::ffloat(0x3e99999a);
    // DTuningDBMgr::LoadDatabase("Render:Glare", ...) (o `tun`), dbindex("Glare ID", 0x339094, 0, 0x4f) e os atributos
    // com o passo 0x40.
    TuningIndex ix;
    ix.lo = 0;
    ix.hi = 0x4f;
    uint8_t* t = reinterpret_cast<uint8_t*>(m.types);
    dbattrib_int(tun, &ix, "Halo Tex", t + 0x30, 0x40);
    dbattrib_float(tun, &ix, "Halo Spin", t + 0x0c, 0x40);
    dbattrib_colour(tun, &ix, "Halo Col", t + 0x20, 0x40);
    dbattrib_float(tun, &ix, "Halo Size", t + 0x28, 0x40);
    dbattrib_int(tun, &ix, "Spike Tex", t + 0x34, 0x40);
    dbattrib_float(tun, &ix, "Spike Spin", t + 0x1c, 0x40);
    dbattrib_colour(tun, &ix, "Spike Col", t + 0x24, 0x40);
    dbattrib_float(tun, &ix, "Spike Size", t + 0x2c, 0x40);
    dbattrib_float(tun, &ix, "Z Bias", t + 0x38, 0x40);
    // (DTuningDBMgr::CloseCurrent)
    RGlareManager_CreateUVsFromTexIDs(m);
}

// [driving] 0x001f19f0 RGlareManager::CreateUVsFromTexIDs
void RGlareManager_CreateUVsFromTexIDs(RGlareManager& m) {
    for (int i = 0; i < 0x50; ++i) {
        GlareType& ty = m.types[i];
        for (int k = 0; k < 2; ++k) {
            float* uv = k == 0 ? ty.halo_uv : ty.spike_uv;
            const uint32_t tex = uint32_t(k == 0 ? ty.halo_tex : ty.spike_tex);
            if (tex < 2) {
                uv[0] = ee::mul(ee::cvt_s(int32_t(tex & 1)), 0.5f);
                uv[1] = 0.0f;
                uv[2] = 0.5f;
            } else {
                uv[0] = ee::mul(ee::cvt_s(int32_t((tex - 2) & 3)), 0.25f);
                uv[1] = ee::add(ee::mul(ee::cvt_s(int32_t(((tex - 2) & 0xc) >> 2)), 0.25f), 0.5f);
                uv[2] = 0.25f;
            }
        }
    }
}

// [driving] 0x001f1f88 RGlareManager::AddGlare
void RGlareManager_AddGlare(RGlareManager& m, const GlareData& g) {
    if (m.count >= 0x80 || m.enabled == 0) {
        if (m.count >= 0x80) ++g_stats.full;
        return;
    }
    m.glares[m.count] = g;
    m.glares[m.count].pos[3] = 1.0f;
    ++m.count;
    ++g_stats.added;
}

// [driving] 0x001f1b60 GlareBlinkBrightness
float GlareBlinkBrightness(const GlareData& g, uint32_t time) {
    float f = ee::mul(u32_to_float(time - g.blink_start), g.blink_rate);
    // floorf (0x002bfb50, do newlib: exato como o do PC)
    f = ee::sub(f, std::floor(f));
    if (!ee::le(f, g.blink_duty)) return 0.0f;
    if (g.flags & 0x800) {
        if ((g.flags & 0x4000) == 0) {
            f = ee::add(f, f);
            if (ee::lt(1.0f, f)) f = ee::sub(2.0f, f);
        } else if (ee::lt(ps2::ffloat(0x3ecccccd), f)) {
            if (ee::lt(f, ps2::ffloat(0x3f19999a))) {
                const float tenth = ps2::ffloat(0x3dcccccd);
                if (ee::lt(f, 0.5f)) {
                    const float a = ee::add(f, tenth);
                    f = ee::sub(2.0f, ee::add(a, a));
                } else {
                    const float a = ee::sub(f, tenth);
                    f = ee::add(a, a);
                }
            } else {
                f = ee::sub(1.0f, ee::mul(ee::sub(f, ps2::ffloat(0x3f19999a)), 2.5f));
            }
        } else {
            f = ee::mul(f, 2.5f);
        }
    }
    return ee::add(g.blink_base, ee::mul(f, g.blink_amp));
}

// [driving] 0x001f1d18 RGlareManager::AddModelGlare
void RGlareManager_AddModelGlare(RGlareManager& m, float dist2, const GlareData& g, const float matrix[4][4],
                                 const float camera_fwd[4], uint32_t time) {
    if (m.count >= 0x80 || m.enabled == 0) {
        if (m.count >= 0x80) ++g_stats.full;
        return;
    }
    float blink = 1.0f;
    if (g.flags & 0x400) {
        blink = GlareBlinkBrightness(g, time);
        if (ee::lt(blink, ps2::ffloat(0x38d1b717))) return;  // 0,0001
    }
    const float bright = ee::mul(g.intensity, blink);
    if (ee::lt(ps2::ffloat(0x3fcccccd), ee::div(dist2, bright))) {  // 1,6
        ++g_stats.model_far;
        return;
    }
    GlareData& s = m.glares[m.count];
    s = g;
    s.pos[3] = 1.0f;
    s.intensity = bright;
    // [driving] 0x002d18e8 TransformPoint(matriz, o registro, o lugar): o xyz pela matriz (o w fica 1).
    float in[4], out[4];
    std::memcpy(in, g.pos, 16);
    std::memcpy(out, s.pos, 16);
    TransformPoint3(&matrix[0][0], in, out);
    std::memcpy(s.pos, out, 16);
    if (g.flags & 0x20) {
        // [driving] 0x002d1950 TransformVector(MATRIX4 &, COORD3 &, COORD3 &): vmulax.xyz, vmadday.xyz, vmaddz.xyz (o w
        // do destino fica: o +0x2c, o começo do pisca).
        Reg r0, r1, r2, v, acc{}, o;
        load(r0, matrix[0]);
        load(r1, matrix[1]);
        load(r2, matrix[2]);
        load(v, g.dir, XYZ);
        load(o, &s.dir[0], XYZ);
        o[3] = s.blink_start;
        mulabc(acc, r0, v, BX, XYZ);
        maddabc(acc, r1, v, BY, XYZ);
        maddbc(o, acc, r2, v, BZ, XYZ);
        std::memcpy(s.dir, o, 12);
        s.blink_start = o[3];
        // [driving] 0x0025a9d0 VEC3_Dot: (x x + y y) + z z
        const float d = ee::add(ee::add(ee::mul(camera_fwd[0], s.dir[0]), ee::mul(camera_fwd[1], s.dir[1])),
                                ee::mul(camera_fwd[2], s.dir[2]));
        s.intensity = ee::mul(s.intensity, ee::neg(d));
        s.intensity = ee::sub(s.intensity, 0.5f);
        if (!ee::lt(0.0f, s.intensity)) {
            ++g_stats.model_back;
            return;  // (o lugar fica escrito, a contagem não anda)
        }
        s.intensity = ee::add(s.intensity, s.intensity);
    }
    ++m.count;
    ++g_stats.model;
}

// [driving] 0x001f2058 RGlareManager::DrawGlares
void RGlareManager_DrawGlares(RGlareManager& m, const float eye[4], float control_y, GlareDrawList* out) {
    if (m.count == 0) return;
    GlareGlobals& gg = glare_globals();
    gg.control[1] = control_y;
    gg.control[2] = ps2::ffloat(0xccbebc20u);
    int n = 0;
    Reg e;
    load(e, eye);
    const float two_pi = ps2::ffloat(0x40c90fda), off = ps2::ffloat(0x407b851f), k14 = ps2::ffloat(0x3fb33333);
    for (int i = 0; i < m.count; ++i) {
        GlareData& g = m.glares[i];
        Reg p, d{}, u{};
        load(p, g.pos);
        sub(d, p, e, XYZ);
        // vsqrt Q, |d|²; vaddq.x vf0: a distância
        const float dist = ps2::ffloat(ps2::add(0, ps2::sqrt(len2(d))));
        const GlareType& ty = m.types[g.type];
        uint32_t alpha = 0x20000000u;
        if (g.flags & 0x20) alpha = f_to_u32(ee::mul(g.intensity, 32.0f)) << 24;
        // u = normalize(p - olho) (vrsqrt; vmulq.xyz)
        sub(u, p, e, XYZ);
        muls(u, u, ps2::rsqrt(kOne, len2(u)), XYZ);
        for (int k = 0; k < 2; ++k) {
            const float size = ee::mul(k == 0 ? ty.halo_size : ty.spike_size, g.intensity);
            const float pull = ee::mul(ee::neg(ty.zbias), size);
            float* c = gg.coords[gg.buffer][n];
            Reg cr, kk = {ps2::fbits(pull), 0, 0, 0}, t{};
            load(cr, c);
            mulbc(t, u, kk, BX, XYZ);
            add(cr, p, t, XYZ);
            store(c, cr);
            c[3] = ee::mul(size, k14);
            gg.colours[gg.buffer][n] = ((k == 0 ? ty.halo_col : ty.spike_col) & 0xffffffu) | alpha;
            float* uv = gg.uvuv[gg.buffer][n];
            std::memcpy(uv, k == 0 ? ty.halo_uv : ty.spike_uv, 16);
            float r = ee::div(ee::mul(uv[3], dist), two_pi);
            r = ee::sub(r, ee::cvt_s(ee::cvt_w(r)));
            uv[3] = ee::sub(ee::mul(r, two_pi), off);
            ++n;
        }
    }
    ++g_stats.drawn;
    g_stats.sprites += uint64_t(n);
    // (0x0028c318 com 1: o Z sem escrita; EAGL::DrawArray::Draw(0x3dfeb8 + metade x 0x40, n); 0x0028c318 com 0)
    if (out && n > 0) {
        out->coords.insert(out->coords.end(), &gg.coords[gg.buffer][0][0], &gg.coords[gg.buffer][0][0] + n * 4);
        out->uvuv.insert(out->uvuv.end(), &gg.uvuv[gg.buffer][0][0], &gg.uvuv[gg.buffer][0][0] + n * 4);
        out->colours.insert(out->colours.end(), &gg.colours[gg.buffer][0], &gg.colours[gg.buffer][0] + n);
        std::memcpy(&out->alpha, gg.state + 0x40, 8);
        std::memcpy(&out->test, gg.state + 0x30, 8);
        std::memcpy(out->control, gg.control, 16);
    }
    m.count = 0;
    gg.buffer = 1 - gg.buffer;
}

// [driving] 0x00235ea0 GFXGallery::GLARE_Interpolate
void GFXGallery_GLARE_Interpolate(float t, const uint32_t a[3], const uint32_t b[3], uint32_t out[3]) {
    const float x = ps2::ffloat(a[1]);
    out[1] = ps2::fbits(ee::abs(ee::add(x, ee::mul(ee::sub(ps2::ffloat(b[1]), x), t))));
    out[0] = a[0];
    out[2] = a[2];
}

// [driving] 0x00235d50 GFXGallery::GLARE_Draw
void GFXGallery_GLARE_Draw(const uint32_t glare[3], const float pos[4]) {
    GlareData g{};
    if (glare[2] != 0) {
        GlareJitter& j = glare_jitter();
        const uint32_t a = j.mul;
        const uint32_t v0 = j.seed * a;
        const uint32_t v1 = (v0 & 0xffff) * a;
        const uint32_t a1 = (v1 & 0xffff) * a;
        const float k = ps2::ffloat(0x37800000);  // 2^-16
        auto off = [&](uint32_t v) {
            const float f = ee::cvt_s(int32_t((v & 0xffff00u) >> 8));
            return ee::mul(ee::add(f, f), k);
        };
        g.pos[0] = ee::add(ee::sub(pos[0], 1.0f), off(v0));
        g.pos[1] = ee::add(ee::sub(pos[1], 1.0f), off(v1));
        g.pos[2] = ee::add(ee::sub(pos[2], 1.0f), off(a1));
        g.pos[3] = pos[3];
        j.seed = a1 & 0xffff;
    } else {
        std::memcpy(g.pos, pos, 16);
    }
    // O registro na pilha: +0x10 0, +0x14 o tipo 2, +0x15 0, +0x18 o tipo do clarão, +0x1c a intensidade (o resto do
    // registro é o lixo da pilha no PS2: zeros aqui; o DrawGlares não o lê).
    g.flags = 0;
    g.kind = 2;
    g.type = int32_t(glare[0]);
    g.intensity = ps2::ffloat(glare[1]);
    ++g_stats.gallery;
    RGlareManager_AddGlare(glare_manager(), g);
}

// [driving] 0x001ad1a0 DrawGroupDrawEffects
void DrawGroupDrawEffects(float dist, const GlareData* fx, const float matrix[4][4], const float camera_fwd[4],
                          uint32_t time) {
    if (!fx || fx->kind == 0) return;
    for (;;) {
        ++g_stats.world_effects;
        if (fx->flags & 1) {
            if (fx->kind == 2) {
                if (glare_globals().enabled != 0)
                    RGlareManager_AddModelGlare(glare_manager(), ee::mul(dist, dist), *fx, matrix, camera_fwd, time);
            } else if (fx->kind == 6) {
                // ExtractQuatTrans(matriz) e GFX::Draw(o efeito do +0x1c, quatérnio, posição): o GFX::Draw do PS2
                // (0x00233188) só volta (o porte conta).
                uint32_t effect;
                std::memcpy(&effect, &fx->intensity, 4);
                if (effect != 0) ++g_stats.world_gfx;
            } else {
                ++g_stats.unknown_effects;  // printf("Unhandled effect type: %d\n")
            }
        }
        if (fx[1].kind == 0) return;
        ++fx;
    }
}

}  // namespace drv
