#include "driving/fx/scene_fx.h"

#include <cmath>
#include <cstring>

#include "driving/carp_runtime.h"
#include "driving/fx/fx_lights.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/glare.h"
#include "driving/fx/muzzle_flash.h"
#include "driving/light_manager.h"
#include "driving/render/anim_engine.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

SceneFxEnv g_env;
SceneFxStats g_stats;

const float kZero3[4] = {0.0f, 0.0f, 0.0f, 0.0f};      // 0x355b80 kZeroCoord3
const float kIdentityQ[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x355ba0

template <typename T>
T rd(uint32_t p, uint32_t off) {
    T v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, sizeof v);
    return v;
}
template <typename T>
void wr(uint32_t p, uint32_t off, T v) {
    std::memcpy(nf::p32_ptr<uint8_t>(p) + off, &v, sizeof v);
}

// O Handle do objeto (+0x28) e as máscaras de 64 bits dele.
uint32_t handle_of(uint32_t scene) { return rd<uint32_t>(scene, 0x28); }
uint64_t mask64(uint32_t h, uint32_t off) { return rd<uint64_t>(h, off); }
bool bit(uint64_t m, uint32_t i) { return ((m >> (i & 63)) & 1) != 0; }

// O fim do TriggerEffects e do DamageEffects: com algum efeito ligado, o prazo de 600 quadros; sem, zero.
void set_decay(uint32_t scene, uint32_t h) {
    wr<uint32_t>(scene, 0x20, mask64(h, 0x20) != 0 ? rtime_data().time + 600 : 0u);
}

// A seleção de um efeito pela máscara: sem a metade alta, os efeitos sem zonas; com ela, os que têm uma zona em comum.
bool selected(uint16_t zones, uint32_t mask) {
    const uint32_t hi = mask & 0xffff0000u;
    const uint32_t z = uint32_t(zones) << 16;
    if (hi == 0 && z == 0) return true;
    return (z & hi) != 0;
}

// RAnimEngine::Handle::FindEffectByID (0x001a2750): a cópia do efeito com o identificador (o byte depois dos u16), ou
// 0. O identificador comparado é a palavra inteira (uma máscara acima de 255 não acha nada).
uint32_t find_effect_by_id(uint32_t h, uint32_t id) {
    const uint8_t n = rd<uint8_t>(h, 0x1f);
    const uint32_t ids = rd<uint32_t>(h, 0x18) + uint32_t(n) * 2;
    for (uint32_t i = 0; i < n; ++i)
        if (uint32_t(rd<uint8_t>(ids, i)) == id) return rd<uint32_t>(h, 0x14) + i * 0x40;
    return 0;
}

void suppress(int32_t canvas) {
    if (g_env.gfx_suppress) g_env.gfx_suppress(canvas);
    else GFX_SuppressEffect(canvas);
}

}  // namespace

SceneFxEnv& scene_fx_env() { return g_env; }
SceneFxStats& scene_fx_stats() { return g_stats; }

// [driving] 0x00233160 GFX::Purge, 0x002356b0 GFXGallery::CANVAS_Purge
void GFX_Purge(int32_t handle) {
    GfxGallery* g = gfx_gallery();
    ++g_stats.purges;
    if (!g) return;
    if (handle - 1 >= 0) GFXGallery_CANVAS_PurgeSceneObj(*g, handle - 1);
}

// [driving] 0x00235520 GFXGallery::CANVAS_PurgeSceneObj(int): uma tela presa a um objeto passa a "uma vez só" e parada,
// com a velocidade do objeto guardada (GetVelocity) e a vida que lhe resta; os sistemas dela param de nascer.
void GFXGallery_CANVAS_PurgeSceneObj(GfxGallery& g, int32_t canvas) {
    if (canvas < 0 || canvas >= 0x200) {
        ++g_stats.guards;  // o PS2 escreveria fora das 512 telas
        return;
    }
    GfxCanvas& c = g.canvases[canvas];
    if ((c.flags & 0x800) == 0) {
        float v[3] = {0.0f, 0.0f, 0.0f};
        uint32_t vel = 0;
        if (c.scene != 0 && g_env.velocity) vel = g_env.velocity(c.scene);
        if (vel != 0) std::memcpy(v, nf::p32_ptr<const float>(vel), 12);
        std::memcpy(c.extra, v, 12);
        c.extra_ptr = 0;
        c.flags = uint16_t(c.flags | 0x1000);
        c.flags = uint16_t(c.flags | 0x800);
        c.frame = c.life;
        for (int32_t i = 0; i < int8_t(c.nump); ++i) {
            ParticleEmitter* e = nf::raw(c.emitters) ? &nf::raw(c.emitters)[i] : nullptr;
            if (!e) break;
            if (RParticleSystem* s = nf::raw(e->system)) s->active = 0;
        }
    }
    c.scene = 0;
}

// [driving] 0x002331c0 GFX::SuppressEffect, 0x00235720 GFXGallery::CANVAS_Suppress
void GFX_SuppressEffect(int32_t handle) {
    GfxGallery* g = gfx_gallery();
    ++g_stats.suppressed;
    if (g) GFXGallery_CANVAS_Suppress(*g, handle);
}
void GFXGallery_CANVAS_Suppress(GfxGallery& g, int32_t handle) {
    const int32_t i = handle - 1;
    if (i < 0 || i >= 0x200) {
        ++g_stats.guards;
        return;
    }
    g.canvases[i].flags = uint16_t(g.canvases[i].flags | 0x2000);
}

// [driving] 0x002331e8 GFX::SetIntensity, 0x00235750 GFXGallery::CANVAS_SetIntensity: a escala da taxa (+0x4c) de cada
// sistema da tela (os NUMP do efeito, lidos do 'NUMP' do UGroup: o mesmo que o +0x6e da tela).
void GFX_SetIntensity(int32_t handle, float intensity) {
    GfxGallery* g = gfx_gallery();
    ++g_stats.intensities;
    if (g) GFXGallery_CANVAS_SetIntensity(*g, handle, intensity);
}
void GFXGallery_CANVAS_SetIntensity(GfxGallery& g, int32_t handle, float intensity) {
    const int32_t i = handle - 1;
    if (i < 0 || i >= 0x200) {
        ++g_stats.guards;
        return;
    }
    GfxCanvas& c = g.canvases[i];
    // O 'NUMP' do efeito (UGroup::DataLocateTag(ref, 'NUMP'); o PS2 não confere o fim).
    UGroup* grp = c.ref ? nf::p32_ptr<UGroup>(c.ref) : nullptr;
    UData* d = grp ? UGroup_DataLocateTag(grp, 0x4e554d50u) : nullptr;
    if (!grp || d == UGroup_DataEnd(grp)) {
        ++g_stats.guards;
        return;
    }
    int32_t n;
    std::memcpy(&n, UData_Data(d), 4);
    for (int32_t k = 0; k < n; ++k) {
        ParticleEmitter* e = nf::raw(c.emitters) ? &nf::raw(c.emitters)[k] : nullptr;
        if (!e) break;
        if (RParticleSystem* s = nf::raw(e->system)) s->rate_scale = intensity;
    }
}

// [driving] 0x001ca198 RSceneObj::ToggleEffect
void RSceneObj_ToggleEffect(float intensity, uint32_t scene, bool on, uint32_t index, uint32_t effect) {
    uint32_t h = handle_of(scene);
    if (!on) {
        const uint64_t active = mask64(h, 0x20);
        if (!bit(active, index)) return;
        wr<uint64_t>(h, 0x20, active & ~(1ull << (index & 63)));
        ++g_stats.toggled_off;
        h = handle_of(scene);
        if (!bit(mask64(h, 0x30), index)) return;
        const int32_t canvas = rd<int32_t>(effect, 0x1c);
        if (canvas == 0) return;
        if (g_env.gfx_purge) g_env.gfx_purge(canvas);
        else GFX_Purge(canvas);
        wr<int32_t>(effect, 0x1c, 0);
        return;
    }
    const uint64_t active = mask64(h, 0x20);
    if (bit(active, index)) return;
    if (!bit(mask64(h, 0x30), index)) {
        wr<uint64_t>(h, 0x20, active | (1ull << (index & 63)));
        ++g_stats.toggled_on;
        return;
    }
    if (rd<int32_t>(effect, 0x1c) == 0) {
        // A velocidade do objeto (a entrada +0x74) e a tela presa à instância (+0x15) na posição e no quatérnio do
        // efeito.
        const uint32_t vel = g_env.velocity ? g_env.velocity(scene) : 0;
        const uint32_t ref = rd<uint32_t>(effect, 0x18);
        float pos2[4], quat2[4];
        std::memcpy(pos2, nf::p32_ptr<const uint8_t>(effect), 12);
        pos2[3] = 0.0f;
        std::memcpy(quat2, nf::p32_ptr<const uint8_t>(effect) + 0x20, 16);
        const int32_t inst = int32_t(rd<uint8_t>(effect, 0x15));
        const int32_t canvas = g_env.gfx_trigger ? g_env.gfx_trigger(ref, kZero3, kIdentityQ, pos2, quat2, scene, inst, vel)
                                                 : GFX_Trigger(ref, kZero3, kIdentityQ, pos2, quat2, scene, inst, vel);
        ++g_stats.gfx_triggers;
        wr<int32_t>(effect, 0x1c, canvas);
        if (canvas != 0) {
            h = handle_of(scene);
            wr<uint64_t>(h, 0x20, mask64(h, 0x20) | (1ull << (index & 63)));
            ++g_stats.toggled_on;
        }
    }
    if (ee::le(0.0f, intensity)) {
        const int32_t canvas = rd<int32_t>(effect, 0x1c);
        if (canvas != 0) {
            if (g_env.gfx_intensity) g_env.gfx_intensity(canvas, intensity);
            else GFX_SetIntensity(canvas, intensity);
        }
    }
}

// [driving] 0x001ca4b0 RSceneObj::TriggerEffects
void RSceneObj_TriggerEffects(float intensity, uint32_t scene, bool on, bool by_id, uint32_t mask) {
    uint32_t h = handle_of(scene);
    if (mask64(h, 0x38) == 0) return;
    if (by_id) {
        const uint32_t e = find_effect_by_id(h, mask);
        if (e != 0) RSceneObj_ToggleEffect(-1.0f, scene, on, (e - rd<uint32_t>(h, 0x14)) >> 6, e);
    } else {
        const uint8_t n = rd<uint8_t>(h, 0x1f);
        uint32_t effect = rd<uint32_t>(h, 0x14);
        uint32_t zones = rd<uint32_t>(h, 0x18);
        for (uint32_t i = 0; i < n; ++i, effect += 0x40, zones += 2) {
            h = handle_of(scene);
            if (!bit(mask64(h, 0x38), i) || bit(mask64(h, 0x28), i)) continue;
            if (selected(rd<uint16_t>(zones, 0), mask)) RSceneObj_ToggleEffect(intensity, scene, on, i, effect);
        }
    }
    set_decay(scene, handle_of(scene));
}

// [driving] 0x001ca640 RSceneObj::SuppressEffects
void RSceneObj_SuppressEffects(uint32_t scene, bool by_id, uint32_t mask) {
    uint32_t h = handle_of(scene);
    if (mask64(h, 0x38) == 0) return;
    if (by_id) {
        const uint32_t e = find_effect_by_id(h, mask);
        if (e == 0 || rd<uint8_t>(e, 0x14) != 6) return;
        const int32_t canvas = rd<int32_t>(e, 0x1c);
        if (canvas != 0) suppress(canvas);
        return;
    }
    const uint8_t n = rd<uint8_t>(h, 0x1f);
    uint32_t effect = rd<uint32_t>(h, 0x14);
    uint32_t zones = rd<uint32_t>(h, 0x18);
    for (uint32_t i = 0; i < n; ++i, effect += 0x40, zones += 2) {
        h = handle_of(scene);
        if (!bit(mask64(h, 0x38), i) || bit(mask64(h, 0x28), i) || !bit(mask64(h, 0x30), i)) continue;
        if (!selected(rd<uint16_t>(zones, 0), mask)) continue;
        const int32_t canvas = rd<int32_t>(effect, 0x1c);
        if (canvas != 0) suppress(canvas);
    }
}

// [driving] 0x001ca7a0 RSceneObj::DamageEffects: cada efeito de dano liga se a zona dele está entre as batidas (e
// desliga se não); um da galeria que não ganhou tela fica marcado como ligado.
void RSceneObj_DamageEffects(uint32_t scene, uint32_t zones_hit) {
    uint32_t h = handle_of(scene);
    if (mask64(h, 0x28) == 0) return;
    ++g_stats.damage;
    const uint8_t n = rd<uint8_t>(h, 0x1f);
    uint32_t effect = rd<uint32_t>(h, 0x14);
    uint32_t zones = rd<uint32_t>(h, 0x18);
    for (uint32_t i = 0; i < n; ++i, effect += 0x40, zones += 2) {
        h = handle_of(scene);
        if (!bit(mask64(h, 0x28), i)) continue;
        const bool on = (rd<uint16_t>(zones, 0) & zones_hit & 0xffff) != 0;
        RSceneObj_ToggleEffect(-1.0f, scene, on, i, effect);
        if (!on) continue;
        h = handle_of(scene);
        if (!bit(mask64(h, 0x30), i)) continue;
        if (rd<int32_t>(effect, 0x1c) != 0) continue;
        wr<uint64_t>(h, 0x20, mask64(h, 0x20) | (1ull << (i & 63)));
    }
    set_decay(scene, handle_of(scene));
}

// [driving] 0x001caba8 RSceneObj::TriggerFX
void RSceneObj_TriggerFX(float intensity, uint32_t scene, uint32_t type, uint32_t mask) {
    ++g_stats.trigger_fx;
    if (handle_of(scene) == 0) {
        ++g_stats.guards;  // um objeto sem RAnimEngine::Handle (os carros do port sem as cópias): o PS2 sempre o tem
        return;
    }
    if (type < 2) {
        RSceneObj_TriggerEffects(intensity, scene, true, type == 1, mask);
    } else if (type - 2 < 2) {
        RSceneObj_TriggerEffects(intensity, scene, false, type == 3, mask);
    } else if (type - 4 < 2) {
        RSceneObj_SuppressEffects(scene, type == 5, mask);
    } else if (type == 10) {
        const uint16_t old = rd<uint16_t>(scene, 0x1c);
        const uint32_t fresh = ~uint32_t(old) & mask;
        if (fresh == 0) return;
        ++g_stats.stimuli_zones;
        if (g_env.stimuli_zones) g_env.stimuli_zones(handle_of(scene), 0x12, fresh, rtime_data().time, 2);
        wr<uint16_t>(scene, 0x1c, uint16_t(mask));
    } else if (type == 9) {
        const uint16_t old = rd<uint16_t>(scene, 0x1e);
        if ((~uint32_t(old) & mask) == 0) return;
        RSceneObj_DamageEffects(scene, uint32_t(old) | mask);
        wr<uint16_t>(scene, 0x1e, uint16_t(rd<uint16_t>(scene, 0x1e) | mask));
    }
}

// [driving] 0x001d3c28 RVehicle::TriggerFX
void RVehicle_TriggerFXFull(float intensity, uint32_t vehicle, int32_t type, uint32_t mask, uint32_t extra,
                            const float* p0, const float* p1) {
    const float* a = p0 ? p0 : kZero3;
    const float* b = p1 ? p1 : kZero3;
    if (type == 7) {
        ++g_stats.dissolves;  // RVehicle::StartColourDissolve (0x001d3b10): não portado
        return;
    }
    if (type == 6 && rd<uint32_t>(vehicle, 0x358) != 0) {
        ++g_stats.sparks;  // RVehicleParticle::TriggerSparks (0x001eed18)
        if (g_env.sparks) g_env.sparks(vehicle, mask, a, b, 1.0f);
        return;
    }
    if (type == 8) {
        ++g_stats.muzzle;
        RVehicle_StartMuzzleFlash(vehicle, a, b, int32_t(mask), int32_t(extra));
        return;
    }
    if (type == 0xb || type == 0xc) {
        ++g_stats.lamps;
        // (o PS2 grava em +0x268 + 4 x lâmpada sem conferir)
        if (mask > 15) {
            ++g_stats.guards;
            return;
        }
        wr<float>(vehicle, 0x268 + mask * 4, type == 0xb ? 1.0f : 0.0f);
        return;
    }
    RSceneObj_TriggerFX(intensity, vehicle, uint32_t(type), mask);
}

void SceneObj_TriggerFX(float intensity, uint32_t scene, int32_t type, uint32_t mask, uint32_t extra,
                        const float* p0, const float* p1) {
    if (scene == 0) {
        ++g_stats.guards;  // o PS2 leria a tabela virtual do endereço 0
        return;
    }
    if (rd<uint32_t>(scene, 0x3c) == kVtRVehicle) {
        RVehicle_TriggerFXFull(intensity, scene, type, mask, extra, p0, p1);
        return;
    }
    RSceneObj_TriggerFX(intensity, scene, uint32_t(type), mask);
}

// [driving] 0x001ca340 RSceneObj::DecayEffects
void RSceneObj_DecayEffects(uint32_t scene) {
    ++g_stats.decays;
    float m[4][4];
    if (g_env.transform) {
        g_env.transform(scene, m);
    } else if (rd<uint32_t>(scene, 0x08) == 0) {
        ++g_stats.guards;  // um objeto sem a cópia da instância (o PS2 leria o endereço 0): a origem
        std::memset(m, 0, sizeof m);
        m[3][3] = 1.0f;
    } else {
        std::memcpy(m, nf::p32_ptr<const uint8_t>(rd<uint32_t>(scene, 0x08)), 64);
        m[0][3] = m[1][3] = m[2][3] = 0.0f;
        m[3][3] = 1.0f;
    }
    // vsub.xyz; vmul.xyz; vmulax.x; vmadday.x; vmaddz.x
    uint32_t d[3];
    for (int i = 0; i < 3; ++i) d[i] = ps2::sub(ps2::fbits(m[3][i]), ps2::fbits(g_env.eye[i]));
    uint32_t acc = ps2::mul(d[0], d[0]);
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(d[1], d[1]));
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(d[2], d[2]));
    if (ps2::less(acc, ps2::fbits(28900.0f))) {
        wr<uint32_t>(scene, 0x20, rtime_data().time + 600);
        return;
    }
    uint32_t h = handle_of(scene);
    uint64_t active = mask64(h, 0x20);
    if (mask64(h, 0x30) != 0) {
        if (active != 0) {
            const uint8_t n = rd<uint8_t>(h, 0x1f);
            uint32_t effect = rd<uint32_t>(h, 0x14);
            for (uint32_t i = 0; i < n; ++i, effect += 0x40) {
                if (bit(mask64(h, 0x20), i) && bit(mask64(h, 0x30), i) && (rd<uint32_t>(effect, 0x10) & 0x14) == 0)
                    RSceneObj_ToggleEffect(-1.0f, scene, false, i, effect);
                h = handle_of(scene);
            }
        }
        active = mask64(h, 0x20);
    }
    if (active == 0) wr<uint32_t>(scene, 0x20, 0u);
}

// [driving] 0x001ca8e0 RSceneObj::AdjustEffectPosition
void RSceneObj_AdjustEffectPosition(uint32_t effect, float m[4][4]) {
    if ((rd<uint32_t>(effect, 0x10) & 0x100) == 0) return;
    float y = m[3][1];
    if (g_env.world_height && g_env.world_height(m[3], &y)) m[3][1] = y;
    else m[3][1] = 0.0f;
}

// [driving] 0x001af708 (o brilho da luz dinâmica)
float DynamicLight_Brightness(uint32_t effect, uint32_t time) {
    const uint32_t start = rd<uint32_t>(effect, 0x20);
    const uint32_t dt = time - start;
    // O inteiro sem sinal em float (CVT.S.W; acima de 2^31 pela metade, com o bit de baixo, somada a si mesma).
    float t;
    if (int32_t(dt) >= 0) {
        t = ee::cvt_s(int32_t(dt));
    } else {
        t = ee::cvt_s(int32_t((dt & 1) | (dt >> 1)));
        t = ee::add(t, t);
    }
    float f = ee::mul(t, rd<float>(effect, 0x24));
    f = ee::sub(f, std::floor(f));
    if (!ee::le(f, rd<float>(effect, 0x28))) return 0.0f;
    const uint32_t flags = rd<uint32_t>(effect, 0x10);
    if (flags & 0x800) {
        if ((flags & 0x4000) == 0) {
            f = ee::add(f, f);
            if (ee::lt(1.0f, f)) f = ee::sub(2.0f, f);
        } else if (ee::lt(0.4f, f)) {
            if (ee::lt(f, 0.6f)) {
                if (ee::lt(f, 0.5f)) {
                    float g = ee::add(f, 0.1f);
                    g = ee::add(g, g);
                    f = ee::sub(2.0f, g);
                } else {
                    const float g = ee::sub(f, 0.1f);
                    f = ee::add(g, g);
                }
            } else {
                const float g = ee::sub(f, 0.6f);
                f = ee::sub(1.0f, ee::mul(g, 2.5f));
            }
        } else {
            f = ee::mul(f, 2.5f);
        }
    }
    return ee::add(rd<float>(effect, 0x2c), ee::mul(f, rd<float>(effect, 0x30)));
}

// [driving] 0x001af8c0 RHighLevelLightManager::AddDynamicLightEffect
void RHighLevelLightManager_AddDynamicLightEffect(RHighLevelLightManager& m, uint32_t effect, const float mat[4][4],
                                                  uint32_t time) {
    const float k = DynamicLight_Brightness(effect, time);
    if (!ee::lt(0.0f, k)) return;
    // vmulax.xyzw; vmadday.xyzw; vmaddaz.xyzw; vmaddw.xyzw (a posição com o w do efeito)
    const uint32_t* v = nf::p32_ptr<const uint32_t>(effect);
    alignas(16) float pos[4];
    for (int c = 0; c < 4; ++c) {
        uint32_t acc = ps2::mul(ps2::fbits(mat[0][c]), v[0]);
        acc = ps2::madd(acc, ps2::fbits(mat[1][c]), v[1]);
        acc = ps2::madd(acc, ps2::fbits(mat[2][c]), v[2]);
        acc = ps2::madd(acc, ps2::fbits(mat[3][c]), v[3]);
        pos[c] = ps2::ffloat(acc);
    }
    pos[3] = k;
    const uint8_t* e = nf::p32_ptr<const uint8_t>(effect);
    const uint32_t inv255 = 0x3b808081u;
    float colour[4];
    const uint8_t bytes[4] = {e[0x1f], e[0x1e], e[0x1d], e[0x1c]};
    for (int c = 0; c < 4; ++c) colour[c] = ps2::ffloat(ps2::mul(ps2::fbits(ee::cvt_s(bytes[c])), inv255));
    RHighLevelLightManager_AddPositionalLight(m, pos, colour, rd<int32_t>(effect, 0x18));
    ++g_stats.lights;
}

// [driving] 0x001cab38 RSceneObj::RenderEffect
bool RSceneObj_RenderEffect(uint32_t effect, const float m[4][4]) {
    const uint8_t type = rd<uint8_t>(effect, 0x14);
    if (type == 2) {
        ++g_stats.glares;
        if (g_env.model_glare) {
            g_env.model_glare(effect, m);
        } else {
            RGlareManager_AddModelGlare(glare_manager(), 0.0f, *nf::p32_ptr<const GlareData>(effect), m,
                                        g_env.forward, rtime_data().time);
        }
        return true;
    }
    if (type == 5) {
        if (g_env.dynamic_light) g_env.dynamic_light(effect, m);
        else RHighLevelLightManager_AddDynamicLightEffect(light_manager_positional().hl, effect, m, rtime_data().time);
        return true;
    }
    return type == 6 || type == 8;
}

// [driving] 0x001ca930 RSceneObj::RenderEffects
void RSceneObj_RenderEffects(uint32_t scene, bool glares_only) {
    uint32_t h = handle_of(scene);
    if (h == 0) {
        ++g_stats.guards;  // (como no TriggerFX)
        return;
    }
    if (mask64(h, 0x20) == 0) return;
    ++g_stats.renders;
    const uint32_t decay = rd<uint32_t>(scene, 0x20);
    if (decay != 0) {
        const uint32_t now = rtime_data().time;
        if (decay <= now) RSceneObj_DecayEffects(scene);
        else wr<uint32_t>(scene, 0x20, now + 600);
    }
    h = handle_of(scene);
    bool have_matrix = false;
    uint32_t last_instance = 0xffffffffu;
    bool playing = false;
    alignas(16) float m[4][4] = {};
    const uint8_t n = rd<uint8_t>(h, 0x1f);
    uint32_t effect = rd<uint32_t>(h, 0x14);
    for (uint32_t i = 0; i < n; ++i, effect += 0x40) {
        h = handle_of(scene);
        if (!bit(mask64(h, 0x20), i)) continue;
        if (glares_only && rd<uint8_t>(effect, 0x14) != 2) continue;
        const uint32_t flags = rd<uint32_t>(effect, 0x10);
        const bool needs_system = ((flags >> 7) & 1) != 0;
        const uint16_t hit = rd<uint16_t>(scene, 0x1c);
        const uint8_t zone = rd<uint8_t>(effect, 0x16);
        const bool hidden = ((uint32_t(hit) >> (zone & 31)) & 1) != 0 && (flags & 0x2000) == 0;
        const uint32_t instance = rd<uint8_t>(effect, 0x15);
        if (instance != last_instance) {
            have_matrix = false;
            last_instance = instance;
            if (needs_system) {
                playing = g_env.system_playing
                              ? g_env.system_playing(h, instance)
                              : AnimHandle_IsSystemPlaying(h, AnimHandle_GetInstanceSystemID(h, instance));
            }
        }
        if (needs_system && !playing) {
            RSceneObj_ToggleEffect(-1.0f, scene, false, i, effect);
            continue;
        }
        if (hidden) continue;
        if (!have_matrix) {
            std::memset(m, 0, sizeof m);
            if (g_env.instance_position) g_env.instance_position(scene, int32_t(instance), m);
            have_matrix = true;
            RSceneObj_AdjustEffectPosition(effect, m);
        }
        if (!RSceneObj_RenderEffect(effect, m)) {
            h = handle_of(scene);
            wr<uint64_t>(h, 0x20, mask64(h, 0x20) & ~(1ull << (i & 63)));
        }
    }
}

}  // namespace drv
