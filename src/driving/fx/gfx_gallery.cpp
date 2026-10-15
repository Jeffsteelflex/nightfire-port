#include "driving/fx/gfx_gallery.h"

#include <cstring>

#include "driving/real/vu0_math.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

using namespace ps2::vu;
namespace ee = ps2::ee;

namespace {

GfxGallery* g_gallery = nullptr;
GfxEnv g_env;
GfxStats g_stats;
uint32_t g_last_added = 0;  // 0x33aad8 o último tipo que o PARTICLE_Add pôs na biblioteca

constexpr uint32_t kTagFlgs = 0x466c6773, kTagNump = 0x4e554d50, kTagLife = 0x4c494645, kTagName = 0x4e616d65;
constexpr uint32_t kTagKey = 0x676b3000;  // 'gk' | GFXGallery::DataIndex(3, 0)

inline float fb(uint32_t w) { return ps2::ffloat(w); }
inline uint32_t bf(float f) { return ps2::fbits(f); }

UGroup* ug(uint32_t p) { return nf::p32_ptr<UGroup>(p); }

// O primeiro word de um dado do grupo (DataLocateTag, sem conferir o fim: o original lê o que estiver lá).
const uint8_t* data_of(uint32_t group, uint32_t tag) {
    UGroup* g = ug(group);
    UData* d = UGroup_DataLocateTag(g, tag);
    if (d == UGroup_DataEnd(g)) {
        ++g_stats.guards;
        static const uint8_t zero[16] = {};
        return zero;
    }
    return UData_Data(d);
}

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool same_nocase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (x == 0) return true;
    }
}

// m x n pela VU0 (0x002cbb88, o mesmo do VU0_MATRIX4_mult do port).
void mat_mul(const float a[4][4], const float b[4][4], float out[4][4]) { VU0_MATRIX4_mult(a, b, out); }

}  // namespace

GfxGallery* gfx_gallery() { return g_gallery; }
GfxEmitterAlloc& gfx_emitter_alloc() {
    static GfxEmitterAlloc a;
    return a;
}

namespace {
ParticleEmitter* emitters_new(uint32_t n) {
    if (gfx_emitter_alloc().alloc) return gfx_emitter_alloc().alloc(n);
    return new ParticleEmitter[n]();
}
void emitters_delete(ParticleEmitter* p) {
    if (gfx_emitter_alloc().free) gfx_emitter_alloc().free(p);
    else delete[] p;
}
}  // namespace
GfxEnv& gfx_env() { return g_env; }
GfxStats& gfx_stats() { return g_stats; }

// --------------------------------------------------------------------------------------------- a VU0 dos quatérnios

// [driving] 0x002cbc00 VU0_v3quatrotate
void VU0_v3quatrotate(const float q[4], const float v[4], float out[4]) {
    uint32_t vq[4], vv[4], acc[4], t[4], v3[4], v6[4], v4[4], v8[4];
    load(vq, q, XYZW);
    load(vv, v, XYZW);
    load(v8, out, XYZW);
    opmula(acc, vq, vv);
    opmsub(t, acc, vv, vq);         // q x v
    mulbc(v3, vv, vq, BW, XYZ);     // v * q.w
    add(t, t, v3, XYZ);
    opmula(acc, t, vq);
    opmsub(v6, acc, vq, t);         // t x q
    mul(v3, vq, vv, XYZ);
    addbc(v3, v3, v3, BY, X);
    addbc(v3, v3, v3, BZ, X);       // q . v
    mulbc(v4, vq, v3, BX, XYZ);
    mulbc(v3, t, vq, BW, XYZ);
    add(v4, v3, v4, XYZ);
    sub(v8, v4, v6, XYZ);
    store(out, v8, XYZW);
}

// [driving] 0x002cbcf8 VU0_v4quatrotate_xlate
void VU0_v4quatrotate_xlate(const float q[4], const float v[4], const float tr[4], float out[4]) {
    uint32_t vq[4], vv[4], acc[4], t[4], v3[4], v6[4], v4[4], v8[4], v9[4];
    load(vq, q, XYZW);
    load(vv, v, XYZW);
    opmula(acc, vq, vv);
    opmsub(t, acc, vv, vq);
    mulbc(v3, vv, vq, BW, XYZ);
    add(t, t, v3, XYZ);
    opmula(acc, t, vq);
    opmsub(v6, acc, vq, t);
    mul(v3, vq, vv, XYZ);
    load(v8, out, XYZW);
    addbc(v3, v3, v3, BY, X);
    load(v9, tr, XYZW);
    addbc(v3, v3, v3, BZ, X);
    mulbc(v4, vq, v3, BX, XYZ);
    mulbc(v3, t, vq, BW, XYZ);
    add(v4, v3, v4, XYZ);
    sub(v8, v4, v6, XYZ);
    add(v8, v8, v9, XYZ);
    store(out, v8, XYZW);
}

// O produto inline (0x00233460 e PARTICLE_Draw): w = q.w l.w - q.x l.x - q.y l.y - q.z l.z; xyz = q x l + q.xyz l.w +
// l.xyz q.w, somados lane a lane na ordem da VU0.
void gfx_quat_mul(const float q[4], const float l[4], float out[4]) {
    uint32_t v1[4], v2[4], v3[4], v4[4], acc[4];
    load(v1, q, XYZW);
    load(v2, l, XYZW);
    mul(v3, v1, v2, XYZW);
    subbc(v3, v3, v3, BX, W);
    subbc(v3, v3, v3, BY, W);
    subbc(v3, v3, v3, BZ, W);
    opmula(acc, v1, v2);
    opmsub(v3, acc, v2, v1);
    mulbc(v4, v1, v2, BW, XYZ);
    add(v3, v3, v4, X);
    add(v3, v3, v4, Y);
    add(v3, v3, v4, Z);
    mulbc(v4, v2, v1, BW, XYZ);
    add(v3, v3, v4, X);
    add(v3, v3, v4, Y);
    add(v3, v3, v4, Z);
    store(out, v3, XYZW);
}

// ----------------------------------------------------------------------------------------------------------- GFX::

void GFX_Init(std::vector<uint8_t> file) {
    // [driving] 0x00233618 GFXGallery::GFXGallery: o bloco das trilhas (0x800) e o das telas (0x12000) zerados, o
    // arquivo lido e desserializado, as 512 telas livres e os grudados soltos.
    if (!g_gallery) g_gallery = new GfxGallery();
    GfxGallery& g = *g_gallery;
    g.root = 0;
    g.file = 0;
    std::memset(g.cache, 0, sizeof(g.cache));
    std::memset(g.canvases, 0, sizeof(g.canvases));
    g.cache_count = 0;
    g.active_count = 0;
    g.free_count = 0x200;
    g.cull_cursor = 0;
    g.drawn = 0;
    g.culled = 0;
    g.data = std::move(file);
    if (g.data.size() >= 16) {
        UGroup* root = reinterpret_cast<UGroup*>(g.data.data());
        UGroup_Deserialize(root);
        g.root = nf::p32_u(root);
        g.file = nf::p32_u(g.data.data());
    }
    for (int i = 0; i < 0x200; ++i) g.free_list[0x1ff - i] = i;
    GFXGallery_STICKY_ReleaseAllIndexes(g);
}

void GFX_Shutdown() {
    delete g_gallery;
    g_gallery = nullptr;
}

// [driving] 0x00232f48 GFX::LookupReference
uint32_t GFX_LookupReference(const char* name) {
    ++g_stats.lookups;
    GfxGallery* g = g_gallery;
    if (!g || !g->root || !name) {
        ++g_stats.lookups_failed;
        return 0;
    }
    // [driving] 0x002339e8 GFXGallery::GET_Canvas: o primeiro GANM com o nome (strcasecmp).
    UGroup* root = ug(g->root);
    UData* a = UGroup_GetArray(root);
    for (uint32_t i = 0; i < UGroup_GroupCount(root); ++i) {
        UData* n = UGroup_DataLocateTag(a + i, kTagName);
        if (n == UGroup_DataEnd(a + i)) continue;
        if (same_nocase(reinterpret_cast<const char*>(UData_Data(n)), name)) return nf::p32_u(a + i);
    }
    ++g_stats.lookups_failed;  // (o GHud::dbMessage "GFX: no effect %s")
    return 0;
}

namespace {

// [driving] 0x00232af8 GFX::Trigger (o comum do Trigger e do TriggerIncidental; `incidental` é o segundo argumento)
int32_t gfx_trigger_common(int32_t incidental, uint32_t ref, const float pos[3], const float quat[4],
                           const float pos2[3], const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra) {
    GfxGallery* g = g_gallery;
    if (!g || ref == 0) return 0;
    ++g_stats.triggers;
    UGroup* r = ug(ref);
    UData* f = UGroup_DataLocateTag(r, kTagFlgs);
    uint32_t flags = 0;
    if (f != UGroup_DataEnd(r)) flags = rd32(UData_Data(f));
    const float p4[4] = {pos[0], pos[1], pos[2], 1.0f};
    const float p24[4] = {pos2[0], pos2[1], pos2[2], 1.0f};
    const int32_t loop = int32_t(flags & 1), with_extra = int32_t((flags >> 4) & 1);
    const int32_t id =
        GFXGallery_CANVAS_Add(*g, ref, scene, instance, p4, quat, p24, quat2, extra, loop, with_extra, incidental ? loop : 0);
    return (flags & 1) ? 0 : id;
}

}  // namespace

// [driving] 0x002330b0 GFX::Trigger (o 0x00232af8 com a galeria e o 0 do segundo argumento)
int32_t GFX_Trigger(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3], const float quat2[4],
                    uint32_t scene, int32_t instance, uint32_t extra) {
    return gfx_trigger_common(0, ref, pos, quat, pos2, quat2, scene, instance, extra);
}

// [driving] 0x00233108 GFX::TriggerIncidental (o 0x00232af8 com o 1 do segundo argumento)
int32_t GFX_TriggerIncidental(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                              const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra) {
    ++g_stats.incidental;
    return gfx_trigger_common(1, ref, pos, quat, pos2, quat2, scene, instance, extra);
}

// [driving] 0x00232e80 GFX::Update
void GFX_Update() {
    if (g_gallery) GFXGallery_FRAME_Draw(*g_gallery);
}

// ------------------------------------------------------------------------------------------------------- os grudados

// [driving] 0x00234330 GFXGallery::STICKY_ReleaseAllIndexes
void GFXGallery_STICKY_ReleaseAllIndexes(GfxGallery& g) { g.sticky_used[0] = g.sticky_used[1] = 0; }

// [driving] 0x00234350 GFXGallery::STICKY_GetNewIndex
int32_t GFXGallery_STICKY_GetNewIndex(GfxGallery& g, int32_t canvas) {
    for (int w = 0; w < 2; ++w) {
        uint32_t bits = g.sticky_used[w];
        if (bits == 0xffffffffu) continue;
        for (int b = 0; b < 32; ++b, bits >>= 1) {
            if ((bits & 1) == 0) {
                g.sticky_used[w] |= 1u << b;
                g.sticky[w * 32 + b].canvas = canvas;
                return w * 32 + b;
            }
        }
    }
    return -1;
}

// [driving] 0x002343e0 GFXGallery::STICKY_UpdateAllIndexes
void GFXGallery_STICKY_UpdateAllIndexes(GfxGallery& g) {
    for (int w = 0; w < 2; ++w) {
        uint32_t bits = g.sticky_used[w];
        for (int b = 0; bits != 0 && b < 32; ++b, bits >>= 1) {
            if (bits & 1) {
                float pos[4], quat[4];
                GfxGallery::Sticky& s = g.sticky[w * 32 + b];
                if (uint32_t(s.canvas) < 0x200) {
                    // (0x00233460 direto, sem o limite do GET_EffectPosQuat)
                    GFXGallery_GET_EffectPosQuat(g, uint32_t(s.canvas), pos, quat);
                } else {
                    ++g_stats.guards;
                    pos[0] = pos[1] = pos[2] = 0.0f;
                }
                s.pos[0] = pos[0];
                s.pos[1] = pos[1];
                s.pos[2] = pos[2];
            }
        }
    }
}

// [driving] 0x002344d0 GFXGallery::STICKY_ReleaseIndex
void GFXGallery_STICKY_ReleaseIndex(GfxGallery& g, uint32_t index) {
    if (index < 0x40) g.sticky_used[index >> 5] &= ~(1u << (index & 0x1f));
}

// [driving] 0x00234508 GFXGallery::GET_StickyEffectPos: (x, y, z, 0), ou o kZeroCoord4 (0, 0, 0, 1) sem ela.
bool GFXGallery_GET_StickyEffectPos(const GfxGallery& g, uint32_t index, float out[4]) {
    if (index < 0x40 && (g.sticky_used[index >> 5] & (1u << (index & 0x1f)))) {
        out[0] = g.sticky[index].pos[0];
        out[1] = g.sticky[index].pos[1];
        out[2] = g.sticky[index].pos[2];
        out[3] = 0.0f;
        return true;
    }
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    return false;
}

// ------------------------------------------------------------------------------------------------------- as telas

// [driving] 0x00233f88 GFXGallery::GET_EffectPosQuat (e 0x00233460)
bool GFXGallery_GET_EffectPosQuat(GfxGallery& g, uint32_t index, float pos[4], float quat[4]) {
    if (index >= 0x200) {
        pos[0] = pos[1] = pos[2] = 0.0f;  // kZeroCoord4
        pos[3] = 1.0f;
        quat[0] = quat[1] = quat[2] = 0.0f;  // kZeroCoord3 e 1
        quat[3] = 1.0f;
        return false;
    }
    const GfxCanvas& c = g.canvases[index];
    if (c.scene == 0) {
        std::memcpy(quat, c.quat, 16);
        uint32_t a[4], b[4], o[4];
        load(a, c.pos, XYZ);
        load(b, c.pos2, XYZ);
        load(o, pos, XYZW);
        add(o, a, b, XYZ);
        store(pos, o, XYZ);
        pos[3] = 1.0f;
        return true;
    }
    alignas(16) float m[4][4] = {};
    if (!g_env.instance_position || !g_env.instance_position(c.scene, c.instance, m)) ++g_stats.guards;
    float q[4];
    VU0_m4toquat(m, q);
    gfx_quat_mul(q, c.quat2, quat);
    pos[0] = c.pos2[0];
    pos[1] = c.pos2[1];
    pos[2] = c.pos2[2];
    pos[3] = 1.0f;
    // (x, y, z, 1) x m: vmulax/vmadday/vmaddaz/vmaddw.
    uint32_t v[4], r0[4], r1[4], r2[4], r3[4], acc[4], o[4];
    load(v, pos, XYZW);
    load(r0, m[0], XYZW);
    load(r1, m[1], XYZW);
    load(r2, m[2], XYZW);
    load(r3, m[3], XYZW);
    mulabc(acc, r0, v, BX, XYZW);
    maddabc(acc, r1, v, BY, XYZW);
    maddabc(acc, r2, v, BZ, XYZW);
    maddbc(o, acc, r3, v, BW, XYZW);
    store(pos, o, XYZW);
    return true;
}

// [driving] 0x00234d80 GFXGallery::CANVAS_Add
int32_t GFXGallery_CANVAS_Add(GfxGallery& g, uint32_t ref, uint32_t scene, int32_t instance, const float pos[4],
                              const float quat[4], const float pos2[4], const float quat2[4], uint32_t extra,
                              int32_t loop, int32_t with_extra, int32_t reserve) {
    if (reserve == 0 ? g.free_count <= 0 : g.free_count <= 0x40) {
        ++g_stats.refused;  // (o GHud::dbMessage "GFX: no free canvas")
        return 0;
    }
    const int32_t idx = g.free_list[--g.free_count];
    g.active[g.active_count] = idx;
    float ex[3] = {0.0f, 0.0f, 0.0f};
    if (extra && with_extra) std::memcpy(ex, nf::p32_ptr<const float>(extra), 12);
    const uint32_t flags = rd32(data_of(ref, kTagFlgs));
    const int32_t nump = int32_t(rd32(data_of(ref, kTagNump)));
    uint8_t sticky = 0xff;
    ParticleEmitter* emitters = nullptr;
    if (nump > 0) {
        emitters = emitters_new(uint32_t(nump));  // UMemory::FastAlloc(NUMP << 6)
        if (scene && (flags & 8)) sticky = uint8_t(GFXGallery_STICKY_GetNewIndex(g, idx));
        for (int32_t i = 0; i < nump; ++i) {
            emitters[i].sticky = int32_t(sticky);
            emitters[i].tag = 0xffffffffu;
            emitters[i].system = nullptr;
        }
    }
    float p[4], q[4], q2[4];
    std::memcpy(p, pos, 16);
    std::memcpy(q, quat, 16);
    std::memcpy(q2, quat2, 16);
    if (((loop && !(flags & 8)) || with_extra) && scene) {
        alignas(16) float m[4][4] = {};
        VU0_quattom4(q2, m);
        m[3][0] = pos2[0];
        m[3][1] = pos2[1];
        m[3][2] = pos2[2];
        m[3][3] = 1.0f;
        // [driving] 0x001c95f0 RSceneObj::TransformMatrixByInstancePosition(int, MATRIX4 &, bool): m x a matriz da
        // instância no mundo.
        alignas(16) float inst[4][4] = {};
        if (!g_env.instance_position || !g_env.instance_position(scene, instance, inst)) ++g_stats.guards;
        alignas(16) float out[4][4];
        mat_mul(m, inst, out);
        std::memcpy(m, out, sizeof(m));
        VU0_m4toquat(m, q);
        p[0] = m[3][0];
        p[1] = m[3][1];
        p[2] = m[3][2];
        p[3] = m[3][3];
    }
    const int32_t life = int32_t(rd32(data_of(ref, kTagLife)));
    GfxCanvas& c = g.canvases[idx];
    std::memcpy(c.quat2, q2, 16);
    std::memcpy(c.pos2, pos2, 12);
    c.ref = ref;
    std::memcpy(c.quat, q, 16);
    std::memcpy(c.pos, p, 12);
    c.life = life;
    std::memcpy(c.extra, ex, 12);
    c.age = 0;
    c.scene = scene;
    c.extra_ptr = extra;
    c.emitters = emitters;
    c.frame = 0;
    c.instance = instance;
    c.unknown64 = 0;
    c.unknown60 = 0.0f;
    // Os bits 10-15 de +0x6c: só o 11 (uma vez só) e o 15 (reserva); os outros bits ficam.
    c.flags = uint16_t((c.flags & 0x03ff) | (loop ? 0x800 : 0) | (reserve ? 0x8000 : 0));
    c.nump = uint8_t(nump);
    c.sticky = sticky;
    for (uint32_t& s : c.sounds) s = 0;
    ++g.active_count;
    return idx + 1;
}

// [driving] 0x002345d8 GFXGallery::FRAME_Draw
void GFXGallery_FRAME_Draw(GfxGallery& g) {
    GFXGallery_STICKY_UpdateAllIndexes(g);
    g.drawn = 0;
    ++g_stats.frames;
    float top = 0.0f;  // f20: o maior +0x60 das telas de uma vez só (ninguém lê)
    int32_t i = 0;
    while (i < g.active_count) {
        GfxCanvas& c = g.canvases[g.active[i]];
        if ((c.flags & 0x800) == 0) {
            c.frame = 0;
        } else if (ee::lt(top, c.unknown60)) {
            top = c.unknown60;
        }
        if (!(c.life < c.frame)) {
            const bool past = !(c.frame < c.life);
            c.flags = uint16_t((c.flags & ~0x1000) | ((((c.flags >> 12) & 1) | (past ? 1 : 0)) << 12));
            float pos[4], quat[4];
            GFXGallery_GET_EffectPosQuat(g, uint32_t(g.active[i]), pos, quat);
            std::memcpy(c.pos, pos, 12);
            if (c.flags & 0x2000) c.flags &= ~0x2000;
            else GFXGallery_CANVAS_Draw(g, c, pos, quat);
            ++c.frame;
            ++g.drawn;
        }
        ++c.age;
        if ((c.flags & 0x1000) && GFXGallery_CANVAS_HasStopped(g, c)) {
            GFXGallery_STICKY_ReleaseIndex(g, c.sticky);
            g.free_list[g.free_count] = g.active[i];
            g.active[i] = g.active[g.active_count - 1];
            ++g.free_count;
            --g.active_count;
            ++g_stats.stopped;
            continue;
        }
        ++i;
    }
}

// [driving] 0x00233f18 GFXGallery::GET_FirstIdleKey
const uint8_t* GFXGallery_GET_FirstIdleKey(uint32_t track) { return data_of(track, kTagKey); }

// [driving] 0x00234818 GFXGallery::CANVAS_Draw
void GFXGallery_CANVAS_Draw(GfxGallery& g, GfxCanvas& c, const float pos[4], const float quat[4]) {
    ++g_stats.canvas_draws;
    uint32_t all_one_shot = 1;  // s8
    int32_t emitter = 0, sound = 0;
    UGroup* r = ug(c.ref);
    UData* t = UGroup_GetArray(r);
    UData* end = t + UGroup_GroupCount(r);
    static const float kZero4[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x39c940
    for (; t != end; ++t) {
        const uint32_t tag = UData_MatchTag(t);
        if (tag == 0x67702020) {  // 'gp '
            const uint8_t* key = GFXGallery_GET_FirstIdleKey(nf::p32_u(t));
            if (key == nullptr) continue;
            ParticleEmitter* e = nf::raw(c.emitters);
            if (!e || emitter >= c.nump) {
                ++g_stats.guards;  // o PS2 escreveria além do bloco dos emissores
                ++emitter;
                continue;
            }
            ParticleEmitter& em = e[emitter++];
            em.sticky = c.sticky;
            const float* p = c.sticky != 0xff ? kZero4 : pos;
            const uint32_t r1 = GFXGallery_PARTICLE_Draw(g, nf::p32_u(t), em, key, p, quat, c.extra, c.frame,
                                                         (c.flags >> 12) & 1, (c.flags >> 10) & 1,
                                                         (c.flags >> 15) & 1);
            all_one_shot &= (r1 != 0) ? 1u : 0u;
        } else if (tag == 0x67732020) {  // 'gs ' o som
            all_one_shot = 1;
            if (sound < 8) {
                ++sound;
                if ((c.flags & 0x1000) == 0) ++g_stats.sounds;  // (o ASound e o SOUND_Play: não portados)
            }
        } else if (tag == 0x67672020) {  // 'gg ' o clarão
            all_one_shot = 0;
            if ((c.flags & 0x1400) == 0) ++g_stats.glares;  // (GLARE_Interpolate e GLARE_Draw: não portados)
        } else if (tag == 0x67642020 && (c.flags & 0x1400) == 0) {  // 'gd ' os destroços
            if ((c.flags & 0x4000) == 0 || c.frame == 0) {
                ++g_stats.debris;  // (DEBRIS_SetState e DEBRIS_Draw: não portados)
                c.flags |= 0x4000;
            } else {
                all_one_shot = 0;
            }
        }
    }
    if (c.flags & 0x800) c.flags = uint16_t((c.flags & ~0x1000) | ((((c.flags >> 12) & 1) | all_one_shot) << 12));
}

// [driving] 0x00235ed0 GFXGallery::PARTICLE_Add
uint32_t GFXGallery_PARTICLE_Add(GfxGallery& g, uint32_t track, const uint8_t* src) {
    for (int32_t i = 0; i < g.cache_count; ++i)
        if (g.cache[i].track == track) return g.cache[i].tag;
    ParticleData d;
    std::memcpy(&d, src, sizeof(d));
    if (int8_t(d.unknown2d[1]) > 0) d.unknown2d[0] = uint8_t(g_last_added);  // o +0x2d com o último tipo
    d.gravity_spread = ee::mul(d.gravity_spread, 127.0f);
    d.spin = ee::mul(d.spin, 360.0f);
    d.gravity = ee::mul(d.gravity, 127.0f);
    const uint32_t tag = uint32_t(g.cache_count + 5);
    d.index = tag;
    RParticleSystemManager* m = particle_manager();
    g_last_added = m ? uint32_t(RParticleLibrary_AddSystem(m->library, d, tag)) : 0xffffffffu;
    ++g_stats.particle_types;
    if (g.cache_count >= 256) {
        ++g_stats.guards;  // o PS2 escreveria além do bloco de 0x800 bytes
        return tag;
    }
    g.cache[g.cache_count].track = track;
    g.cache[g.cache_count].tag = tag;
    ++g.cache_count;
    return tag;
}

// [driving] 0x002360c0 GFXGallery::PARTICLE_Draw
uint32_t GFXGallery_PARTICLE_Draw(GfxGallery& g, uint32_t track, ParticleEmitter& e, const uint8_t* key,
                                  const float pos[4], const float quat[4], const float* velocity, int32_t frame,
                                  int32_t stopping, int32_t culled, int32_t keep) {
    ++g_stats.particle_draws;
    const uint8_t* data = key + 0x30;
    const int32_t start = int32_t(rd32(data + 0xa8)), repeat = int32_t(rd32(data + 0xac));
    if (repeat == 0 && frame < start) return 0;
    if (e.tag == 0xffffffffu) e.tag = GFXGallery_PARTICLE_Add(g, track, data);
    RParticleSystemManager* m = particle_manager();
    RParticleSystem* s = nf::raw(e.system);
    if (!culled) {
        float kp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        std::memcpy(kp, key, 12);
        kp[3] = 1.0f;
        VU0_v4quatrotate_xlate(quat, kp, pos, e.pos);
        e.pos[3] = 1.0f;
        float sp[4];
        GFXGallery_GET_StickyEffectPos(g, uint32_t(e.sticky), sp);
        uint32_t a[4], b[4], c[4];
        load(a, sp, XYZW);
        load(b, e.pos, XYZW);
        add(a, a, b, XYZW);
        // A distância à câmera (o RRenderer +0x40): vsub.xyz, x^2 + y^2 + z^2 pelo ACC e o VSQRT.
        load(b, g_env.camera, XYZW);
        sub(c, a, b, XYZ);
        uint32_t sq[4], acc[4], len2[4];
        const uint32_t one[4] = {bf(1.0f), 0, 0, 0};
        mul(sq, c, c, XYZ);
        mulabc(acc, c, c, BX, X);
        maddabc(acc, one, sq, BY, X);
        maddbc(len2, acc, one, sq, BZ, X);
        const float dist = fb(ps2::add(0, ps2::sqrt(len2[0])));
        float kq[4], wq[4];
        std::memcpy(kq, key + 0x10, 16);
        gfx_quat_mul(quat, kq, wq);
        static const float kUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};    // 0x39c9d0
        static const float kSide[4] = {1.0f, 0.0f, 0.0f, 0.0f};  // 0x39c9e0
        float dir[4], side[4];
        std::memcpy(dir, e.dir, 12);
        dir[3] = fb(e.sticky);  // o w do +0x30 é o +0x3c
        VU0_v3quatrotate(wq, kUp, dir);
        std::memcpy(e.dir, dir, 12);
        std::memcpy(side, e.side, 12);
        side[3] = fb(e.tag);
        VU0_v3quatrotate(wq, kSide, side);
        std::memcpy(e.side, side, 12);
        if (s == nullptr && m) {
            // O criador 0x3909d0 (RParticleSystem::RParticleCreator) e a chave = o emissor.
            s = RParticleSystemManager_AddOrRefresh(*m, nf::p32_u(&e), &e, ParticleCreator::kSystem, dist);
            e.system = s;
        }
        if (s) {
            s->lod = dist;
            s->keep_alive = 0x1e;
        }
    }
    if (s == nullptr) return keep != 0 ? 1u : 0u;
    const ParticleData* sd = reinterpret_cast<const ParticleData*>(data);
    if (frame == 0 || frame == start) {
        if (sd->flags & 2) {
            RParticleSystem_SetActive(*s, 1);
            s->time = 0;
        }
        s->velocity = nf::p32_u(velocity);
    }
    if (stopping && ((nf::raw(s->data)->flags >> 1) & 1) == 0) s->active = 0;
    return (nf::raw(s->data)->flags >> 1) & 1;
}

// [driving] 0x00235270 GFXGallery::CANVAS_HasStopped (sem os sons: o port não os cria)
bool GFXGallery_CANVAS_HasStopped(GfxGallery& /*g*/, GfxCanvas& c) {
    ParticleEmitter* e = nf::raw(c.emitters);
    if (e) {
        for (int32_t i = 0; i < int8_t(c.nump); ++i) {
            RParticleSystem* s = nf::raw(e[i].system);
            if (s && s->active != 0) return false;
        }
    }
    // (os oito sons: os que não acabaram seguram a tela; os outros são destruídos)
    if (e) {
        for (int32_t i = 0; i < int8_t(c.nump); ++i) {
            RParticleSystem* s = nf::raw(e[i].system);
            if (s) {
                // RParticleSystem::Shutdown inline: sem emissor e sem passos; o gerente o apaga no próximo passo.
                s->emitter = nullptr;
                s->keep_alive = 0;
                e[i].system = nullptr;
            }
        }
        emitters_delete(e);  // UMemory::FastFree(NUMP << 6)
        c.emitters = nullptr;
    }
    return true;
}

// [driving] 0x00234198 GFXGallery::CULL_Start
void GFXGallery_CULL_Start(GfxGallery& g) {
    g.culled = 0;
    g.cull_cursor = 0;
}

// [driving] 0x002341a8 GFXGallery::CULL_GetNextCanvas: a esfera da tela (a posição, raio 5), altura 5, sem corte no
// longe com 0,75.
int32_t GFXGallery_CULL_GetNextCanvas(GfxGallery& g, float sphere[4], float* height, int32_t* check_far,
                                      float* far_scale) {
    if (!(g.cull_cursor < g.active_count)) return -1;
    float quat[4];
    GFXGallery_GET_EffectPosQuat(g, uint32_t(g.active[g.cull_cursor]), sphere, quat);
    sphere[3] = 5.0f;
    *height = 5.0f;
    *check_far = 1;
    *far_scale = 0.75f;
    return g.cull_cursor++;
}

// [driving] 0x00234258 GFXGallery::CULL_SetCanvas
void GFXGallery_CULL_SetCanvas(GfxGallery& g, int32_t item, int32_t culled) {
    GfxCanvas& c = g.canvases[g.active[item]];
    const uint16_t old = c.flags;
    uint16_t f = uint16_t((old & ~0x400) | ((culled & 1) ? 0x400 : 0));
    c.flags = f;
    if (culled != 0) {
        if (old & 0x8000) {
            c.flags = f | 0x1000;
        } else if (old & 0x800) {
            c.flags = uint16_t((f & ~0x1000) | ((((f >> 12) & 1) | (c.scene == 0 ? 1 : 0)) << 12));
        }
        ++g.culled;
    }
}

// [driving] 0x002337f8 GFXGallery::PurgeAllEffects: as telas ativas sem efeito, os sistemas soltos e os emissores
// liberados; depois todas as telas zeradas (efeito, objeto, quadro, emissores, sons) e as listas de volta.
void GFXGallery_PurgeAllEffects(GfxGallery& g) {
    for (int32_t i = 0; i < g.active_count; ++i) {
        GfxCanvas& c = g.canvases[g.active[i]];
        c.ref = 0;
        ParticleEmitter* e = nf::raw(c.emitters);
        if (e) {
            for (int32_t k = 0; k < int8_t(c.nump); ++k) {
                RParticleSystem* s = nf::raw(e[k].system);
                if (s) {
                    s->emitter = nullptr;
                    s->keep_alive = 0;
                    e[k].system = nullptr;
                }
            }
            emitters_delete(e);
            c.emitters = nullptr;
        }
        for (uint32_t& s : c.sounds) s = 0;  // (o destrutor de cada som: o port não os cria)
    }
    for (int i = 0; i < 0x200; ++i) {
        GfxCanvas& c = g.canvases[i];
        c.ref = 0;
        c.scene = 0;
        c.frame = 0;
        c.emitters = nullptr;
        for (uint32_t& s : c.sounds) s = 0;
        g.free_list[i] = 0x1ff - i;
    }
    g.active_count = 0;
    g.free_count = 0x200;
    GFXGallery_STICKY_ReleaseAllIndexes(g);
}

}  // namespace drv
