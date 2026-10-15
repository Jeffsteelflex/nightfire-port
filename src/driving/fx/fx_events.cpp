#include "driving/fx/fx_events.h"

#include <cstring>

#include "driving/fx/gfx_gallery.h"
#include "driving/game/explosion.h"
#include "driving/game/triggers.h"
#include "driving/game/weapons.h"
#include "driving/real/vu0_math.h"
#include "driving/world_vis.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

FxEventsEnv g_env;
FxEventStats g_stats;

const float kZero3[3] = {0.0f, 0.0f, 0.0f};        // 0x355b80
const float kIdentity[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x355ba0

int32_t gfx_trigger(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3], const float quat2[4],
                    uint32_t scene, int32_t instance, uint32_t extra) {
    const EffectsEnv& fe = effects_env();
    return fe.gfx_trigger ? fe.gfx_trigger(ref, pos, quat, pos2, quat2, scene, instance, extra) : 0;
}

// [driving] 0x00153f80 EPlayEffect::~EPlayEffect
void play_effect(const PendingEvent& e) {
    uint32_t w[6];
    std::memcpy(w, e.obj, sizeof w);
    // O objeto de cena: o do evento, ou o +8 do registro da instância (com matriz e instância).
    if (w[4] == 0 && w[2] != 0 && w[3] != 0) w[4] = nf::p32_ptr<const uint32_t>(w[3])[2];
    const uint8_t* r = w[1] ? nf::p32_ptr<const uint8_t>(w[1]) : nullptr;
    if (!r || r[0x14] != 6) return;
    uint32_t gfx, flags;
    std::memcpy(&gfx, r + 0x18, 4);
    std::memcpy(&flags, r + 0x10, 4);
    if (gfx == 0) return;
    if (flags & 0x82) {
        // A entrada +0x84 do RSceneObj (TriggerFX(-1, 1, byte +0x17, 0, zero, zero)): não portada.
        if (w[4] != 0) ++g_stats.trigger_fx;
        return;
    }
    alignas(16) float q[4], p[4];
    std::memcpy(q, r + 0x20, 16);
    std::memcpy(p, r, 12);
    p[3] = 0.0f;  // (o PS2 deixa o lixo da pilha; nunca lido)
    if (w[4] == 0) {
        // Sem objeto: a matriz do evento (ou a identidade), o quatérnio dela vezes o do efeito, a translação somada.
        alignas(16) float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        if (w[2] != 0) {
            std::memcpy(m, nf::p32_ptr<const float>(w[2]), 64);
            m[0][3] = m[1][3] = m[2][3] = 0.0f;
            m[3][3] = 1.0f;
        }
        alignas(16) float qm[4], qo[4];
        VU0_m4toquat(m, qm);
        gfx_quat_mul(qm, q, qo);
        for (int k = 0; k < 3; ++k) p[k] = ee::add(p[k], m[3][k]);
        gfx_trigger(gfx, p, qo, kZero3, kIdentity, 0, 0, 0);
        ++g_stats.play_effect;
        return;
    }
    if ((flags & 0x200) != 0 && w[5] != uint32_t(r[0x15])) {
        // Outra instância: o efeito espelhado em x na translação dela.
        alignas(16) float m[4][4] = {};
        const EffectsEnv& fe = effects_env();
        if (!fe.instance_position || !fe.instance_position(w[4], int32_t(w[5]), m)) ++g_stats.guards;
        p[0] = ee::add(ee::add(ee::neg(p[0]), m[3][0]), m[3][0]);
        q[1] = ee::neg(q[1]);
        q[2] = ee::neg(q[2]);
        ++g_stats.play_effect_mirror;
    }
    const uint32_t velocity = g_env.scene_velocity ? g_env.scene_velocity(w[4]) : 0;
    gfx_trigger(gfx, kZero3, kIdentity, p, q, w[4], int32_t(r[0x15]), velocity);
    ++g_stats.play_effect_scene;
}

// [driving] 0x001543e8 EPlayGiottoEffect::~EPlayGiottoEffect e 0x00232f70 GFX::Trigger(char *, ...)
void play_giotto(const PendingEvent& e) {
    const uint8_t* r = e.obj[1] ? nf::p32_ptr<const uint8_t>(e.obj[1]) : nullptr;
    if (!r) {
        ++g_stats.guards;  // o PS2 leria o endereço 0
        return;
    }
    alignas(16) float p[4], q[4];
    std::memcpy(p, r, 16);
    p[3] = 1.0f;
    std::memcpy(q, r + 0x30, 16);
    q[3] = 1.0f;
    const char* name = e.obj[2] ? nf::p32_ptr<const char>(e.obj[2]) : nullptr;
    const EffectsEnv& fe = effects_env();
    const uint32_t ref = (fe.gfx_lookup && name) ? fe.gfx_lookup(name) : 0;
    ++g_stats.giotto;
    if (!ref) {
        ++g_stats.giotto_missing;
        return;  // (o GFX::Trigger da galeria recusa a referência 0)
    }
    gfx_trigger(ref, p, q, kZero3, kIdentity, 0, 0, 0);
}

// [driving] 0x00151408 ENuclearBlast::~ENuclearBlast
void nuclear(const PendingEvent& e) {
    float scale = ps2::ffloat(e.obj[1]);
    if (ee::lt(scale, 0.0f)) scale = 1.0f;
    float pos[3] = {};
    if (e.obj[2]) std::memcpy(pos, nf::p32_ptr<const uint8_t>(e.obj[2]), 12);
    else ++g_stats.guards;
    ++g_stats.nuclear;
    if (g_env.start_nuclear) {
        g_env.start_nuclear(pos, scale);
    } else if (SWeaponManagerData* w = weapon_manager()) {
        SWeaponManager_StartNuclearSequence(*w, pos, scale);
    }
}

}  // namespace

FxEventsEnv& fx_events_env() { return g_env; }
FxEventStats& fx_event_stats() { return g_stats; }

bool fx_event_execute(const PendingEvent& e) {
    switch (e.type) {
    case 9:
        play_effect(e);
        return true;
    case 196:
        nuclear(e);
        return true;
    case 250:
        play_giotto(e);
        return true;
    case 257:
        // [driving] 0x00157ec0 ESetCullDistanceFactor::~ESetCullDistanceFactor
        CullDistance() = ee::mul(ps2::ffloat(e.obj[1]), 400.0f);
        ++g_stats.cull_factor;
        return true;
    case 36:
    case 130:
    case 173:
    case 252:
    case 253:
    case 279:
        ++g_stats.counted;
        return false;
    default:
        return false;
    }
}

}  // namespace drv
