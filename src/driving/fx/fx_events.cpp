#include "driving/fx/fx_events.h"

#include <cstring>

#include "driving/ai/element_controller.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/muzzle_flash.h"
#include "driving/fx/scene_fx.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/render/anim_engine.h"
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
uint32_t g_reflective = 0;  // a Simulation +0xdc8

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
        // A entrada +0x84 do RSceneObj: TriggerFX(-1, 1, byte +0x17, 0, zero, zero) (seção 32).
        if (w[4] != 0) {
            ++g_stats.trigger_fx;
            SceneObj_TriggerFX(-1.0f, w[4], 1, r[0x17], 0, kZero3, kZero3);
        }
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

// O objeto de cena de um evento (o 36 e o 173): o do evento (a palavra `scene_at`); sem ele, o +8 do registro da
// instância (+0x10) quando há o +0xc e o +0x10; senão o RSceneObj (+0x48) do dono da vaga da Simulation (+0x18; +0x14
// os corpos simples), guardado na palavra.
uint32_t event_scene(PendingEvent& e, int scene_at) {
    uint32_t& scene = e.obj[scene_at];
    if (scene != 0) return scene;
    if (e.obj[3] != 0 && e.obj[4] != 0) {
        scene = nf::p32_ptr<const uint32_t>(e.obj[4])[2];
        return scene;
    }
    const int32_t slot = int32_t(e.obj[6]);
    if (slot == -1) return scene;
    const bool simple = e.obj[5] != 0;
    if (slot < 0 || slot >= (simple ? kSimpleBodySlots : kRigidBodySlots)) {
        ++g_stats.guards;  // o PS2 leria fora das tabelas da Simulation
        return scene;
    }
    const uint32_t s = g_env.physics_scene ? g_env.physics_scene(simple ? 1 : 0, slot) : 0;
    if (s != 0) scene = s;
    return scene;
}

// [driving] 0x00154ad8 EPlaySystemAnim::~EPlaySystemAnim
void play_system_anim(PendingEvent e) {
    const uint32_t scene = event_scene(e, 7);
    if (scene == 0) {
        ++g_stats.system_anim_none;
        return;
    }
    const uint32_t h = nf::p32_ptr<const uint32_t>(scene)[0x28 / 4];
    if (h == 0) {
        ++g_stats.guards;  // um objeto sem Handle: o PS2 leria o endereço 0
        return;
    }
    ++g_stats.system_anim;
    const uint32_t t = rtime_data().time;
    if (g_env.stimuli) g_env.stimuli(h, e.obj[1], e.obj[2], t, 2);
    else AnimHandle_ProcessStimuli(h, e.obj[1], e.obj[2], t, 2);
}

// [driving] 0x001479c0 EActorMuzzleFlash::~EActorMuzzleFlash (o TransformPoint 0x002d18e8: o ponto vezes a matriz)
void actor_muzzle_flash(const PendingEvent& e) {
    const uint32_t effect = e.obj[1], weapon = e.obj[2], scene = e.obj[3];
    if (effect == 0 || scene == 0) {
        ++g_stats.actor_flash_none;
        return;
    }
    alignas(16) float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const int32_t inst = nf::p32_ptr<const uint8_t>(effect)[0x15];
    if (g_env.instance_position_local) g_env.instance_position_local(scene, inst, m);
    // TransformPoint(MATRIX4 &, COORD3 &, COORD3 &): vmulax, vmadday, vmaddaz, vmaddw (o w = 1 do vf0).
    const float* p = nf::p32_ptr<const float>(effect);
    float out[3];
    for (int c = 0; c < 3; ++c) {
        uint32_t acc = ps2::mul(ps2::fbits(m[0][c]), ps2::fbits(p[0]));
        acc = ps2::madd(acc, ps2::fbits(m[1][c]), ps2::fbits(p[1]));
        acc = ps2::madd(acc, ps2::fbits(m[2][c]), ps2::fbits(p[2]));
        acc = ps2::madd(acc, ps2::fbits(m[3][c]), 0x3f800000u);
        out[c] = ps2::ffloat(acc);
    }
    if (weapon == 0) {
        ++g_stats.guards;  // o PS2 escreveria nas vagas a partir do endereço 0
        return;
    }
    ++g_stats.actor_flash;
    ActWeapon_StartMuzzleFlash(weapon, out, g_env.steps ? g_env.steps() : simulation().steps);
}

// [driving] 0x00157be0 ESetCollisionGeometry::~ESetCollisionGeometry
void set_collision_geometry(PendingEvent e) {
    uint32_t scene = e.obj[3];
    if (scene == 0) {
        scene = g_env.scene_from_instance ? g_env.scene_from_instance(e.obj[1]) : 0;
        e.obj[3] = scene;
        if (scene == 0) {
            ++g_stats.collision_none;
            return;
        }
    }
    ++g_stats.collision_geometry;
    const uint32_t instance = nf::p32_ptr<const uint32_t>(scene)[2];
    if (g_env.set_collision_article) g_env.set_collision_article(instance, int32_t(e.obj[2]));
}

// [driving] 0x0015d320 ESuppressEffect::~ESuppressEffect
void suppress_effect(PendingEvent e) {
    uint32_t type, mask;
    if (e.obj[1] != 0) {
        type = 5;
        mask = nf::p32_ptr<const uint8_t>(e.obj[1])[0x17];
    } else {
        type = 4;
        mask = 1u << ((e.obj[7] + 0x10) & 31);
    }
    const uint32_t scene = event_scene(e, 2);
    if (scene == 0) {
        ++g_stats.suppress_none;
        return;
    }
    ++g_stats.suppress;
    if (g_env.trigger_fx) g_env.trigger_fx(scene, int32_t(type), mask);
    else SceneObj_TriggerFX(-1.0f, scene, int32_t(type), mask, 0, kZero3, kZero3);
}

// [driving] 0x0015d538 ESwitchEffectOff::~ESwitchEffectOff e 0x0015d650 ESwitchEffectOn::~ESwitchEffectOn (com
// 0x001984b8 PVehicle::MissionEditorSwitch: ligado, o +0x6c ganha o bit e o +0x70 zera; desligado, o contrário)
void switch_effect(const PendingEvent& e, bool on) {
    if (e.obj[1] == 0) {
        ++g_stats.switch_none;
        return;
    }
    AIElement* el = g_env.redirect_element ? nf::p32_ptr<AIElement>(g_env.redirect_element(e.obj[1]))
                                           : AIElementController_RedirectCarpElement(e.obj[1]);
    if (!el) {
        ++g_stats.guards;  // sem controlador (o PS2 leria o controlador nulo)
        return;
    }
    if (el->type != 1) {
        ++g_stats.switch_none;
        return;
    }
    uint8_t* car = el->physics ? nf::p32_ptr<uint8_t>(el->physics) : nullptr;
    if (!car) {
        ++g_stats.guards;  // o PS2 escreveria a partir do endereço 0
        return;
    }
    ++g_stats.switch_effect;
    uint32_t on_mask, off_mask;
    std::memcpy(&on_mask, car + 0x6c, 4);
    std::memcpy(&off_mask, car + 0x70, 4);
    const uint32_t b = 1u << (e.obj[2] & 31);
    if (on) {
        off_mask = 0;
        on_mask |= b;
    } else {
        on_mask = 0;
        off_mask |= b;
    }
    std::memcpy(car + 0x6c, &on_mask, 4);
    std::memcpy(car + 0x70, &off_mask, 4);
}

// [driving] 0x001589f0 ESetReflectiveObject::~ESetReflectiveObject e 0x00201b68 Simulation::SetReflectiveObject
void set_reflective_object(const PendingEvent& e) {
    uint32_t sig = 0;
    if (e.obj[1] == 0) {
        sig = g_env.player_sig ? g_env.player_sig() : 0u;
    } else {
        const uint8_t* el = nf::p32_ptr<const uint8_t>(e.obj[1]);
        int32_t type;
        uint32_t obj;
        std::memcpy(&type, el + 0x44, 4);
        if (type == 1) {
            std::memcpy(&obj, el + 0xfc, 4);
            if (obj == 0) ++g_stats.reflective_none;  // ("ESetReflectiveObject: null vehicle", 0x36fa68)
            else sig = g_env.object_sig ? g_env.object_sig(obj) : 0u;
        } else if (type == 0) {
            std::memcpy(&obj, el + 0x54, 4);
            if (obj == 0) ++g_stats.guards;  // o PS2 chamaria o GetSig de um objeto nulo
            else sig = g_env.object_sig ? g_env.object_sig(obj) : 0u;
        } else {
            ++g_stats.reflective_none;
        }
    }
    g_reflective = sig;
    ++g_stats.reflective;
}

}  // namespace

uint32_t& sim_reflective_object() { return g_reflective; }

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
        play_system_anim(e);
        return true;
    case 39:
        actor_muzzle_flash(e);
        return true;
    case 130:
        set_collision_geometry(e);
        return true;
    case 173:
        suppress_effect(e);
        return true;
    case 252:
    case 253:
        switch_effect(e, e.type == 253);
        return true;
    case 279:
        set_reflective_object(e);
        return true;
    default:
        return false;
    }
}

}  // namespace drv
