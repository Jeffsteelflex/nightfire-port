#include "drv_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "drv_renderer.h"
#include "platform/crt_compat.h"
#include "driving/bigf.h"
#include "driving/carp_runtime.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_state.h"
#include "driving/fx/bullet_streak.h"
#include "driving/fx/fx_verify.h"
#include "driving/fx/glare.h"
#include "driving/ai/pedestrian.h"
#include "driving/fx/muzzle_flash.h"
#include "driving/fx/scene_fx.h"
#include "driving/physics/simulation.h"
#include "driving/fx/tuning_db.h"
#include "driving/game/shells.h"
#include "driving/track.h"
#include "driving/fx/decals.h"
#include "driving/fx/fx_events.h"
#include "driving/fx/fx_lights.h"
#include "driving/fx/fx_render.h"
#include "driving/fx/gfx_gallery.h"
#include "driving/fx/particles.h"
#include "driving/game/explosion.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving/light_manager.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/world_vis.h"
#include "driving/render_method.h"
#include "driving/shps.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"

namespace {

bool read_from(drv::Bigf* arc, const std::string& key, std::vector<uint8_t>& out) {
    if (!arc) return false;
    const int i = arc->find(key);
    return i >= 0 && arc->read(i, out) && !out.empty();
}

bool sticky_pos(int32_t index, float out[4]) {
    const drv::GfxGallery* g = drv::gfx_gallery();
    if (!g) return false;
    return drv::GFXGallery_GET_StickyEffectPos(*g, uint32_t(index), out);
}

// Simulation::GetRigidBody(int) +0x10: a posição do corpo (os riscos grudados num carro).
bool rigid_pos(int32_t slot, float out[4]) {
    const drv::RigidBody* b = drv::GetRigidBody(drv::simulation(), slot);
    if (!b) return false;
    std::memcpy(out, b->pos, 16);
    return true;
}

// O destrutor do ESpawnExplosion: RScorchWorld::ScorchGround com um WWorldPos novo (0x00232180: zerado).
void scorch_ground(const float pos[4], int32_t type, float size) {
    drv::WWorldPos wp{};
    if (drv::decal_state().ready) drv::RScorchWorld_ScorchGround(drv::decal_state(), drv::world(), wp, pos, type, size);
}

// O destrutor do ESpawnExplosion: RHighLevelLightManager::AddExplosion (com o RTimeData::fTime).
void explosion_light(const float pos[4], float radius) {
    drv::RHighLevelLightManager_AddExplosion(drv::light_manager_positional().hl, pos, radius, drv::rtime_data().time);
}

// RSceneObj::GetVelocity (0x001c9c78): a velocidade do corpo do objeto de física (o Smackable do port: o corpo rígido,
// ou o simples com a bandeira 1), +0x20; o caminho (+0x2c, o RPathEngine) não foi portado: 0.
uint32_t scene_velocity(uint32_t scene) {
    if (!scene) return 0;
    const drv::RSceneObj* o = nf::p32_ptr<const drv::RSceneObj>(scene);
    if (!o->physics) return 0;
    const drv::Smackable* s = nf::p32_ptr<const drv::Smackable>(o->physics);
    if (s->d.flags & 1) {
        const drv::SimpleRigidBody* b = drv::GetSimpleRigidBody(drv::simulation(), s->d.slot);
        return b ? nf::p32_u(b->vel) : 0;
    }
    const drv::RigidBody* b = s->body();
    return b ? nf::p32_u(b) + 0x20 : 0;
}

}  // namespace

// Seção 29: os modelos dos destroços do DRIVING.ELF do jogador (os segmentos de programa: o endereço virtual no arquivo).
// Seção 32: também o modelo, os UV e os nomes das texturas dos clarões dos canos (RMuzzleFlash).
bool load_debris_from_elf(const nf::DiscSource& disc, std::vector<drv::DebrisGeometry>& out) {
    std::vector<uint8_t> elf;
    if (!disc.read("DRIVING.ELF", elf) || elf.size() < 0x34) {
        drv::RMuzzleFlash_StaticInit(drv::muzzle_flash(), nullptr);
        drv::RMuzzleFlash_Init(drv::muzzle_flash());
        return false;
    }
    auto u32 = [&](size_t o) {
        uint32_t v = 0;
        if (o + 4 <= elf.size()) std::memcpy(&v, elf.data() + o, 4);
        return v;
    };
    const uint32_t phoff = u32(0x1c);
    uint16_t phnum = 0;
    std::memcpy(&phnum, elf.data() + 0x2c, 2);
    auto read = [&](uint32_t addr, void* dst, uint32_t n) {
        for (uint32_t i = 0; i < phnum; ++i) {
            const size_t p = size_t(phoff) + size_t(i) * 32;
            const uint32_t off = u32(p + 4), va = u32(p + 8), fs = u32(p + 16);
            if (addr >= va && uint64_t(addr) + n <= uint64_t(va) + fs && uint64_t(off) + (addr - va) + n <= elf.size()) {
                std::memcpy(dst, elf.data() + off + (addr - va), n);
                return true;
            }
        }
        return false;
    };
    drv::RMuzzleFlash_StaticInit(drv::muzzle_flash(), read);
    drv::RMuzzleFlash_Init(drv::muzzle_flash());
    return drv::debris_load_geometry(read, out);
}

namespace {

// Um desenho do UVolatileMaterial em triângulos no mundo (o de faixa, prim 5, aberto; com a matriz do modelo).
void push_volatile_draw(drv::DrvScene& scene, const drv::VolatileDraw& v, uint64_t& batches, uint64_t& triangles) {
    drv::DrvFxBatch d;
    d.texture = v.texture >= 0 ? v.texture : drv::kFxTexStreak0 + v.shape;
    d.world = true;
    d.alpha = v.alpha;
    d.test = v.test;
    // O 0x002913f8(fgRenderer + 0x78, 0) antes do UVolatileMaterial::Draw (e o 1 depois) é o SetZWritesEnable do
    // contexto (o ZMSK pelo 0x0028c318), não o perfil que a seção 29 leu: os riscos das balas e os clarões dos canos não
    // escrevem o Z (seção 32).
    d.zwrite = false;
    // As cinco texturas do RMuzzleFlash::Draw têm os bits de baixo do +0x10 apagados pelo Init (o CLAMP do EAGL: REPEAT
    // nos dois eixos); os UV do modelo vão de 0,9 a 2.
    if (v.texture >= drv::kFxTexMuzzle0 && v.texture < drv::kFxTexMuzzlePov0) d.clamp = 0;
    std::vector<drv::DrvFxVertex> src(size_t(v.count));
    for (int i = 0; i < v.count; ++i) {
        drv::DrvFxVertex& o = src[size_t(i)];
        const float* c = &v.coords[size_t(i) * 4];
        if (v.has_matrix) {
            for (int a = 0; a < 4; ++a)
                o.pos[a] = c[0] * v.matrix[0][a] + c[1] * v.matrix[1][a] + c[2] * v.matrix[2][a] + c[3] * v.matrix[3][a];
        } else {
            std::memcpy(o.pos, c, 16);
        }
        o.uv[0] = v.uv[size_t(i) * 2];
        o.uv[1] = v.uv[size_t(i) * 2 + 1];
        o.colour = v.colours[size_t(i)];
    }
    if (v.prim == 5) {
        for (int k = 0; k + 2 < v.count; ++k)
            for (int j = 0; j < 3; ++j) d.verts.push_back(src[size_t(k + j)]);
    } else {
        d.verts = std::move(src);
    }
    if (d.verts.empty()) return;
    triangles += d.verts.size() / 3;
    scene.fx.push_back(std::move(d));
    ++batches;
}

}  // namespace

bool DrvFxApp::init(drv::Bigf* misc, drv::Bigf* mission,
                    bool (*instance_position)(uint32_t scene, int32_t instance, float m[4][4])) {
    // [driving] 0x001e6e88 RParticleSystemManager::Init("partic01") e 0x00232ce0 GFX::Init (o GameLoop_StartUp).
    drv::RParticleSystemManager_Init();
    std::vector<uint8_t> gal;
    const std::string gal_name = "data\\gfx\\data\\gall1.gal";  // GSystem::GetGalleryName fora das ilhas
    if (!read_from(mission, gal_name, gal)) read_from(misc, gal_name, gal);
    have_gallery = !gal.empty();
    drv::GFX_Init(std::move(gal));
    drv::gfx_stats() = drv::GfxStats();
    drv::particle_stats() = drv::ParticleStats();
    drv::EffectsEnv& fe = drv::effects_env();
    fe.gfx_lookup = drv::GFX_LookupReference;
    fe.gfx_trigger = drv::GFX_Trigger;
    // NF_DRV_NO_IMPACT_FX=1 (depuração): os efeitos dos impactos e dos vidros ficam só anotados, como antes da seção 26
    // (as partículas deles sorteiam o RRandom do render, que também decide o jogo).
    std::string no_impact;
    fe.gfx_trigger_incidental = nf::get_env_narrow("NF_DRV_NO_IMPACT_FX", &no_impact) && no_impact == "1"
                                    ? nullptr
                                    : drv::GFX_TriggerIncidental;
    drv::gfx_env().instance_position = instance_position;
    drv::particles_sticky_pos() = sticky_pos;
    drv::particles_rigid_pos() = rigid_pos;
    // As luzes pontuais, as marcas (o decal.dat da missão; o MISC.VIV não tem) e as tabelas do rastro dos mísseis.
    drv::RLightManager_ResetPositional(drv::light_manager_positional());
    std::vector<uint8_t> dat;
    if (!read_from(mission, "data\\render\\decal.dat", dat)) read_from(misc, "data\\render\\decal.dat", dat);
    decal_types = dat.size() >= 0x280 ? 8 : 0;
    drv::RDecalManager_Construct(drv::decal_state(), dat);
    drv::decal_stats() = drv::DecalStats();
    drv::RMissileStreak_Init(drv::missile_streak_tables());
    drv::missile_fx_stats() = drv::MissileFxStats();
    drv::fx_event_stats() = drv::FxEventStats();
    fe.scorch_ground = scorch_ground;
    fe.explosion_light = explosion_light;
    drv::fx_events_env().scene_velocity = scene_velocity;
    // Seção 29: os riscos das balas (o "Render:FX" da pista: a tabela dos tipos, que também dá a velocidade das balas
    // lentas com rastro e o BulletStreakCount do EFireWeapon) e os clarões (o "Render:Glare"; o gerador do tremor pelo
    // GSystem::CURATOR_Init, antes do GFX::Init no PS2).
    auto read_tun = [&](const std::string& name, std::string& text) {
        std::vector<uint8_t> b;
        if (!read_from(mission, name, b) && !read_from(misc, name, b)) return false;
        text.assign(b.begin(), b.end());
        return true;
    };
    drv::GSystem_RANDOM_Startup(0x642f2b);
    drv::BulletStreakState& bs = drv::bullet_streak();
    bs = drv::BulletStreakState();
    drv::RBulletStreak_StaticInit(bs);
    const drv::TuningReader fx_tun = drv::tuning_load(read_tun, "Render", "FX", track);
    have_fx_tun = !fx_tun.text.empty();
    drv::RBulletStreak_Init(bs, fx_tun);
    drv::bullet_streak_stats() = drv::BulletStreakStats();
    drv::shells_env().streak_speed = [](int32_t type, float& speed) {
        if (type < 0 || type >= drv::kBulletStreakTypes) return false;
        speed = drv::bullet_streak().types[type].speed;
        return true;
    };
    fe.streak_count = [](int32_t type) {
        return type >= 0 && type < drv::kBulletStreakTypes ? drv::bullet_streak().types[type].count : 0;
    };
    const drv::TuningReader glare_tun = drv::tuning_load(read_tun, "Render", "Glare", track);
    have_glare_tun = !glare_tun.text.empty();
    drv::RGlareManager_Construct(drv::glare_manager(), glare_tun);
    drv::glare_stats() = drv::GlareStats();
    // Os destroços (o RDebris do Render_InitLibRender; os modelos do DRIVING.ELF).
    debris_geo.clear();
    if (disc) load_debris_from_elf(*disc, debris_geo);
    drv::RDebris_Construct(drv::debris());
    drv::debris_stats() = drv::DebrisStats();
    ready = true;
    return have_gallery;
}

void DrvFxApp::textures(drv::Bigf* misc, drv::Bigf* mission, drv::DrvRenderer& renderer) {
    auto load = [&](const std::string& file, const char* name, int slot) {
        std::vector<uint8_t> b;
        if (!read_from(mission, file, b) && !read_from(misc, file, b)) return;
        drv::ShpsFile f;
        if (!drv::shps_parse(b.data(), b.size(), f)) return;
        for (const drv::ShpsImage& im : f.images)
            if (im.name == name && renderer.set_fx_texture(slot, im.width, im.height, im.rgba.data())) ++texture_count;
    };
    texture_count = 0;
    load("data\\render\\partic01.ssh", "MAIN", drv::kFxTexMain);
    load("data\\render\\common.ssh", "sprk", drv::kFxTexSpark);
    load("data\\render\\common.ssh", "decl", drv::kFxTexDecal);
    load("data\\render\\common.ssh", "trai", drv::kFxTexTrail);
    // Seção 29: as quatro formas dos riscos das balas e o atlas dos clarões (o contexto 0).
    load("data\\render\\common.ssh", "wtrc", drv::kFxTexStreak0 + 0);
    load("data\\render\\common.ssh", "ytrc", drv::kFxTexStreak0 + 1);
    load("data\\render\\common.ssh", "otrc", drv::kFxTexStreak0 + 2);
    load("data\\render\\common.ssh", "btrc", drv::kFxTexStreak0 + 3);
    load("data\\render\\common.ssh", "flar", drv::kFxTexGlare);
    // Seção 32: as cinco texturas dos clarões dos canos e as 24 do POV (os nomes de 4 letras do .data), no contexto 0.
    auto load_tag = [&](uint32_t t, int slot) {
        const char name[5] = {char(t & 0xff), char(t >> 8 & 0xff), char(t >> 16 & 0xff), char(t >> 24 & 0xff), 0};
        if (t != 0) load("data\\render\\common.ssh", name, slot);
    };
    const drv::MuzzleFlashState& mf = drv::muzzle_flash();
    for (int i = 0; i < drv::kMuzzleTypes; ++i) load_tag(mf.texture_names[i], drv::muzzle_texture_slot(mf.texture[i]));
    for (int t = 0; t < drv::kMuzzlePovTypes; ++t)
        for (int k = 0; k < 3; ++k) load_tag(mf.pov[t].names[k], drv::muzzle_pov_texture_slot(mf.pov[t].textures[k]));
    // As texturas dos 23 tipos de destroço (a etiqueta de cada um, no contexto 0).
    for (size_t i = 0; i < debris_geo.size(); ++i) {
        const uint32_t t = debris_geo[i].tag;
        const char name[5] = {char(t & 0xff), char(t >> 8 & 0xff), char(t >> 16 & 0xff), char(t >> 24 & 0xff), 0};
        load("data\\render\\common.ssh", name, drv::kFxTexDebris0 + int(i));
    }
}

void DrvFxApp::update(uint32_t step, const float camera[16], const float view[16]) {
    if (!ready || step == last_step) return;
    last_step = step;
    const float* eye = camera + 12;
    const float eye4[4] = {eye[0], eye[1], eye[2], 1.0f};
    for (const std::string& d : debug_effects) {
        const size_t a1 = d.find('@');
        if (a1 == std::string::npos) continue;
        const size_t a2 = d.find('@', a1 + 1);
        const std::string name = d.substr(0, a1);
        const float at = std::strtof(d.c_str() + a1 + 1, nullptr);
        const float dist = a2 == std::string::npos ? 15.0f : std::strtof(d.c_str() + a2 + 1, nullptr);
        if (uint32_t(at * 60.0f + 0.5f) != step) continue;
        const uint32_t ref = drv::GFX_LookupReference(name.c_str());
        if (!ref) {
            ++debug_missing;
            continue;
        }
        float fx = camera[8], fz = camera[10];
        const float l = std::sqrt(fx * fx + fz * fz);
        if (l > 1e-6f) {
            fx /= l;
            fz /= l;
        }
        const float pos[3] = {eye[0] + fx * dist, eye[1] - 1.5f, eye[2] + fz * dist};
        static const float kZero3[3] = {0, 0, 0}, kId[4] = {0, 0, 0, 1};
        drv::GFX_Trigger(ref, pos, kId, kZero3, kId, 0, 0, 0);
        ++debug_fired;
    }
    for (const std::string& d : debug_explosions) {
        const size_t a1 = d.find('@');
        const float at = std::strtof(d.c_str(), nullptr);
        float dist = 15.0f, damage = 500.0f;
        if (a1 != std::string::npos) {
            dist = std::strtof(d.c_str() + a1 + 1, nullptr);
            const size_t a2 = d.find('@', a1 + 1);
            if (a2 != std::string::npos) damage = std::strtof(d.c_str() + a2 + 1, nullptr);
        }
        if (uint32_t(at * 60.0f + 0.5f) != step) continue;
        float fx = camera[8], fz = camera[10];
        const float l = std::sqrt(fx * fx + fz * fz);
        if (l > 1e-6f) {
            fx /= l;
            fz /= l;
        }
        // O evento 33 como o ESpawnExplosionStatic o monta (driving/game/explosion.cpp): a posição, a direção (0, 0, 1),
        // o tipo 0, a arma 2 (o míssil do Vanquish), o dano, a face 0 e sem nome (a galeria escolhe o "Ex_*").
        const float pos[4] = {eye[0] + fx * dist, eye[1] - 1.5f, eye[2] + fz * dist, 1.0f};
        const float dir[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        drv::PendingEvent x{33, 0, 0.0f, 0, 0, 0};
        x.size = 0x50;
        x.written = 0xfffff;
        x.obj[0] = 0x00370498;
        std::memcpy(&x.obj[1], pos, 16);
        std::memcpy(&x.obj[5], dir, 16);
        x.obj[11] = 0xffffffffu;
        x.obj[13] = 0;
        x.obj[14] = 2;
        std::memcpy(&x.obj[15], &damage, 4);
        drv::trigger_manager().pending.push_back(x);
        ++debug_fired;
    }
    drv::GfxEnv& ge = drv::gfx_env();
    ge.camera[0] = eye[0];
    ge.camera[1] = eye[1];
    ge.camera[2] = eye[2];
    ge.camera[3] = 1.0f;
    drv::particles_set_view_matrix(view);
    // O que este quadro desenha: as luzes publicadas no fim do anterior.
    frame_lights = drv::light_manager_positional().published;
    // [driving] 0x001d1ef8 RTimeData::Update: o relógio que o UpdateSpawnAllSystems e o UpdateAndRender comparam.
    drv::RTimeData_Update(int32_t(step));
    // [driving] 0x001c5928 RRenderWorldCamera::DrawEffects (as partes portadas, na ordem): 0x001c5aa8 DrawBulletStreaks
    // (o Missile::Render de cada míssil, atualizando: a câmera 0), 0x001da978 RDecalManager::Draw, GFX::Update...
    streaks.clear();
    // Seção 29: o começo do DrawBulletStreaks (as balas lentas, os rastros e o clarão dos tiros instantâneos).
    drv::DrawBulletStreaks_Shells(drv::bullet_streak(), eye4);
    drv::RayShell_DrawTracers(drv::bullet_streak(), eye4);
    drv::RayShell_DrawFlashes(drv::light_manager_positional());
    for (drv::PhysicsObject* po : drv::simulation().missiles) {
        drv::Missile* msl = dynamic_cast<drv::Missile*>(po);
        if (!msl) continue;
        const drv::SimpleRigidBody* b = msl->body();
        drv::StreakStrip strip;
        drv::Missile_RenderEffects(msl->d, msl->po, b ? b->pos : nullptr, true, drv::simulation(), camera,
                                   drv::light_manager_positional(), &strip);
        if (!strip.pos.empty()) streaks.push_back(std::move(strip));
    }
    if (drv::decal_state().ready) drv::RDecalManager_Draw(drv::decal_state());
    volatile_draws.clear();
    drv::RBulletStreak_Draw(drv::bullet_streak(), &volatile_draws);
    drv::GFX_Update();
    // RDebris::Draw (os destroços: o EE tira os velhos e baixa o alfa; o BondDebris do PC está no push).
    debris_draws.clear();
    drv::RDebris_Draw(drv::debris(), drv::rtime_data().time_data, &debris_draws);
    // RGlareManager::DrawGlares(1) (os clarões da galeria deste passo; os do mundo vêm no push, do quadro desenhado).
    glare_list = drv::GlareDrawList();
    drv::RGlareManager_DrawGlares(drv::glare_manager(), eye4, glare_control_y, &glare_list);
    if (drv::RParticleSystemManager* m = drv::particle_manager()) {
        drv::RParticleSystemManager_UpdateSpawnAllSystems(*m);
        drv::RParticleSystemManager_UpdateAndRenderAllSystems(*m);
    }
    // [driving] 0x001c3d58 (o fim do quadro desenhado): RLightManager::FinishLights publica as luzes pontuais pedidas
    // neste quadro e as explosões vivas (com o fTime deste quadro), para o desenho do quadro seguinte.
    drv::RLightManager_FinishLights(drv::light_manager_positional(), drv::rtime_data().time);
    ++updates;
}

void DrvFxApp::push(drv::DrvScene& scene, float fov, float aspect, const drv::Track* trk,
                    const std::vector<drv::Track::Draw>* draws, const float camera[16]) {
    drv::RParticleSystemManager* m = drv::particle_manager();
    if (!ready || !m) return;
    std::vector<drv::FxBatch> b;
    drv::fx_particle_batches(m->lists, scene.view, scene.proj, drv::fx_sprites_control_y(fov, 4.0f), aspect, b);
    for (drv::FxBatch& x : b) {
        drv::DrvFxBatch d;
        d.texture = x.texture;
        d.world = x.world;
        d.alpha = x.alpha;
        d.test = x.test;
        d.zwrite = x.zwrite;
        d.verts.resize(x.verts.size());
        for (size_t i = 0; i < x.verts.size(); ++i) {
            std::memcpy(d.verts[i].pos, x.verts[i].pos, 16);
            std::memcpy(d.verts[i].uv, x.verts[i].uv, 8);
            d.verts[i].colour = x.verts[i].colour;
        }
        triangles += d.verts.size() / 3;
        scene.fx.push_back(std::move(d));
        ++batches;
    }
    // As marcas (os oito DrawArray do RDecalManager: o GeoPrimState 0x334bd0, lista de triângulos, mistura normal, o Z
    // escrito; o 'decl'): as 96 do anel já escritas, com o alfa que o Draw pôs (0 durante o atraso).
    const drv::DecalState& ds = drv::decal_state();
    const int used = int(std::min<uint64_t>(drv::decal_stats().added, uint64_t(drv::kDecalCount)));
    if (ds.ready && used > 0) {
        drv::DrvFxBatch d;
        d.texture = drv::kFxTexDecal;
        d.world = true;
        d.alpha = 0x8000000044ull;
        d.test = 0x50002ull;
        d.zwrite = true;
        d.verts.resize(size_t(used) * 6);
        for (int v = 0; v < used * 6; ++v) {
            drv::DrvFxVertex& o = d.verts[size_t(v)];
            std::memcpy(o.pos, ds.b.coords[v], 16);
            const float q = ds.b.stq[v][2] != 0.0f ? ds.b.stq[v][2] : 1.0f;
            o.uv[0] = ds.b.stq[v][0] / q;
            o.uv[1] = ds.b.stq[v][1] / q;
            o.colour = ds.b.colours[v];
        }
        triangles += d.verts.size() / 3;
        scene.fx.push_back(std::move(d));
        ++batches;
    }
    // Os rastros dos mísseis (os três DrawArray do RMissileStreak: faixa de triângulos, o GeoPrimState 0x3de080 com o
    // AREF 4 sem teste de alfa, mistura normal; o 'trai'), em triângulos.
    for (const drv::StreakStrip& st : streaks) {
        const size_t n = st.colour.size();
        if (n < 3) continue;
        drv::DrvFxBatch d;
        d.texture = drv::kFxTexTrail;
        d.world = true;
        d.alpha = 0x8000000044ull;
        d.test = 0x50042ull;
        d.zwrite = true;
        for (size_t k = 0; k + 2 < n; ++k) {
            for (size_t j = 0; j < 3; ++j) {
                const size_t i = k + j;
                drv::DrvFxVertex o;
                std::memcpy(o.pos, &st.pos[i * 4], 16);
                o.pos[3] = 1.0f;
                const float q = st.stq[i * 4 + 2] != 0.0f ? st.stq[i * 4 + 2] : 1.0f;
                o.uv[0] = st.stq[i * 4] / q;
                o.uv[1] = st.stq[i * 4 + 1] / q;
                o.colour = st.colour[i];
                d.verts.push_back(o);
            }
        }
        triangles += d.verts.size() / 3;
        scene.fx.push_back(std::move(d));
        ++batches;
    }
    // Seção 29: os riscos das balas (o UVolatileMaterial de cada forma: lista de triângulos no mundo, o MatVolatileTexture
    // com Q = 1, Cs As + Cd, o Z escrito).
    for (const drv::VolatileDraw& v : volatile_draws) push_volatile_draw(scene, v, batches, triangles);
    // Seção 32: o RVehicle::RenderFX dos carros do quadro (os clarões dos canos e os efeitos das instâncias: os clarões das
    // lâmpadas vão ao RGlareManager, desenhados com os do mundo abaixo, e as luzes dinâmicas ao RHighLevelLightManager,
    // publicadas no fim do passo) e os clarões das armas dos atores.
    {
        drv::SceneFxEnv& se = drv::scene_fx_env();
        if (camera) {
            std::memcpy(se.forward, camera + 8, 16);
            se.eye[0] = camera[12];
            se.eye[1] = camera[13];
            se.eye[2] = camera[14];
            se.eye[3] = 1.0f;
        }
        const bool paused = drv::simulation().state == 3;
        for (const FrameCar& fc : frame_cars) {
            std::vector<drv::VolatileDraw> fl;
            drv::RVehicle_DrawMuzzleFlashes(drv::muzzle_flash(), fc.vehicle, fc.transform, drv::rtime_data().time, paused,
                                            &fl);
            for (const drv::VolatileDraw& v : fl) frame_volatile.push_back(v);
            drv::RSceneObj_RenderEffects(fc.vehicle, false);
            ++car_fx_frames;
        }
        frame_cars.clear();
        for (uint32_t so : frame_scene_objs) {
            drv::RSceneObj_RenderEffects(so, false);
            ++scene_obj_fx_frames;
        }
        frame_scene_objs.clear();
        for (const drv::VolatileDraw& v : frame_volatile) {
            push_volatile_draw(scene, v, batches, triangles);
            ++flash_draws;
        }
        frame_volatile.clear();
    }
    // Os destroços (o BondDebris: cada pedaço com a matriz que a VU1 monta, o modelo do tipo em faixa de triângulos, a
    // cor do pedaço, (Cs - Cd) As + Cd, o Z escrito).
    for (const drv::DebrisDraw& dd : debris_draws) {
        if (dd.type < 0 || size_t(dd.type) >= debris_geo.size()) continue;
        const drv::DebrisGeometry& geo = debris_geo[size_t(dd.type)];
        if (geo.count < 3) continue;
        drv::DrvFxBatch d;
        d.texture = drv::kFxTexDebris0 + dd.type;
        d.world = true;
        d.alpha = dd.alpha;
        d.test = dd.test;
        d.zwrite = true;
        for (int i = 0; i < dd.count; ++i) {
            float pm[4][4];
            drv::bond_debris_instance_matrix(dd.simtick, &dd.offsets[size_t(i) * 4], &dd.velocity[size_t(i) * 4],
                                             &dd.quats[size_t(i) * 4], pm);
            std::vector<drv::DrvFxVertex> strip(size_t(geo.count));
            for (int k = 0; k < geo.count; ++k) {
                const float* c = &geo.coords[size_t(k) * 4];
                drv::DrvFxVertex& o = strip[size_t(k)];
                for (int a = 0; a < 4; ++a) o.pos[a] = c[0] * pm[0][a] + c[1] * pm[1][a] + c[2] * pm[2][a] + c[3] * pm[3][a];
                o.uv[0] = geo.uv[size_t(k) * 2];
                o.uv[1] = geo.uv[size_t(k) * 2 + 1];
                o.colour = dd.colours[size_t(i)];
            }
            for (int k = 0; k + 2 < geo.count; ++k)
                for (int j = 0; j < 3; ++j) d.verts.push_back(strip[size_t(k + j)]);
        }
        if (d.verts.empty()) continue;
        triangles += d.verts.size() / 3;
        scene.fx.push_back(std::move(d));
        ++batches;
    }
    // Os clarões: os da galeria do último passo e os das lâmpadas do mundo deste quadro (o DrawGroupDrawEffects de cada
    // instância desenhada, com a distância de vista dela, e um DrawGlares só deles).
    glare_control_y = drv::fx_sprites_control_y(fov, 4.0f);
    world_glare_list = drv::GlareDrawList();
    if (trk && draws && camera) {
        const float eye[3] = {camera[12], camera[13], camera[14]};
        const float fwd[4] = {camera[8], camera[9], camera[10], camera[11]};
        const std::vector<drv::TrackInstance>& insts = trk->instances();
        const std::vector<drv::TrackArticle>& arts = trk->articles();
        for (const drv::Track::Draw& d : *draws) {
            if (d.instance < 0 || size_t(d.instance) >= insts.size()) continue;
            const drv::TrackInstance& inst = insts[size_t(d.instance)];
            uint32_t art_addr = d.own ? d.article : 0u;
            if (!d.own) {
                if (inst.article < 0 || size_t(inst.article) >= arts.size()) continue;
                art_addr = arts[size_t(inst.article)].address;
            }
            if (art_addr == 0) continue;
            // [driving] 0x001ad618 QuickDrawInstance (e o 0x001ad1a0 do DrawInstance, seção 34: a cópia do Handle de um
            // objeto de cena com a matriz dela): com o +4 do artigo (o DrawGroup) não nulo, os efeitos.
            uint32_t fx_list;
            std::memcpy(&fx_list, nf::p32_ptr<uint8_t>(art_addr) + 4, 4);
            if (fx_list == 0) continue;
            ++world_glare_instances;
            float im[4][4];
            std::memcpy(im, d.own ? d.world : inst.matrix, 64);
            const float dist = drv::Track::view_distance(inst, eye, 0.0f);  // o multiplicador de LOD do jogo (0)
            drv::DrawGroupDrawEffects(dist, nf::p32_ptr<const drv::GlareData>(fx_list), im, fwd,
                                      drv::rtime_data().time);
        }
        const float eye4[4] = {eye[0], eye[1], eye[2], 1.0f};
        drv::RGlareManager_DrawGlares(drv::glare_manager(), eye4, glare_control_y, &world_glare_list);
    }
    for (const drv::GlareDrawList* gl : {&glare_list, &world_glare_list}) {
        if (gl->colours.empty()) continue;
        drv::ParticleDrawLists::Sprites sp;
        sp.coords = gl->coords;
        sp.uvuv = gl->uvuv;
        sp.colours = gl->colours;
        drv::FxBatch fb;
        fb.texture = drv::kFxTexGlare;
        drv::fx_sprite_batch(sp, scene.view, scene.proj, glare_control_y, aspect, gl->alpha, fb);
        if (fb.verts.empty()) continue;
        drv::DrvFxBatch d;
        d.texture = fb.texture;
        d.world = false;
        d.alpha = gl->alpha;
        d.test = gl->test;
        d.zwrite = false;  // (0x0028c318 com 1: o ZMSK do ZBUF_1)
        d.verts.resize(fb.verts.size());
        for (size_t i = 0; i < fb.verts.size(); ++i) {
            std::memcpy(d.verts[i].pos, fb.verts[i].pos, 16);
            std::memcpy(d.verts[i].uv, fb.verts[i].uv, 8);
            d.verts[i].colour = fb.verts[i].colour;
        }
        triangles += d.verts.size() / 3;
        scene.fx.push_back(std::move(d));
        ++batches;
    }
    // As luzes pontuais publicadas (o GAME::PositionalLights: a VU 0x40-0x47 do carro, do mundo dinâmico e dos
    // personagens).
    const drv::PositionalLightBlock& pl = frame_lights;
    std::memcpy(scene.k.point_pos, pl.pos, sizeof pl.pos);
    std::memcpy(scene.k.point_col, pl.col, sizeof pl.col);
}

// As marcas, as luzes, os mísseis e os eventos (a segunda metade da linha do relatório).
std::string DrvFxApp::extra_report() const {
    const drv::DecalStats& ds = drv::decal_stats();
    const drv::FxLightStats& ls = drv::fx_light_stats();
    const drv::MissileFxStats& ms = drv::missile_fx_stats();
    const drv::FxEventStats& es = drv::fx_event_stats();
    const drv::RLightManagerPositional& lm = drv::light_manager_positional();
    int live = 0;
    for (uint32_t t : lm.hl.expl_time) live += t != 0;
    char buf[640];
    std::snprintf(buf, sizeof buf,
                  "marcas tipos %d pedidas %llu postas %llu recusadas %llu/%llu/%llu/%llu | luzes explosoes %llu vivas %d "
                  "misseis %llu publicadas %d (%.3f,%.3f,%.3f K %.3f) | rastros %zu atualizados %llu desenhados %llu emp "
                  "%llu | eventos efeito %llu objeto %llu espelhado %llu triggerfx %llu giotto %llu/%llu nuclear %llu "
                  "corte %llu contados %llu | guardas %llu/%llu/%llu",
                  decal_types, (unsigned long long)ds.scorches, (unsigned long long)ds.added,
                  (unsigned long long)ds.refused_face, (unsigned long long)ds.refused_slope,
                  (unsigned long long)ds.refused_edges, (unsigned long long)ds.refused_size,
                  (unsigned long long)ls.explosions, live, (unsigned long long)ls.missiles, lm.last_count,
                  lm.published.col[0][0], lm.published.col[0][1], lm.published.col[0][2], lm.published.pos[3][0],
                  streaks.size(), (unsigned long long)ms.updates, (unsigned long long)ms.draws,
                  (unsigned long long)ms.emp_lights, (unsigned long long)es.play_effect,
                  (unsigned long long)es.play_effect_scene, (unsigned long long)es.play_effect_mirror,
                  (unsigned long long)es.trigger_fx, (unsigned long long)es.giotto, (unsigned long long)es.giotto_missing,
                  (unsigned long long)es.nuclear, (unsigned long long)es.cull_factor, (unsigned long long)es.counted,
                  (unsigned long long)ds.guards, (unsigned long long)ms.guards, (unsigned long long)es.guards);
    return buf;
}

std::string DrvFxApp::report() const {
    const drv::GfxStats& g = drv::gfx_stats();
    const drv::ParticleStats& p = drv::particle_stats();
    const drv::GfxGallery* gal = drv::gfx_gallery();
    const drv::RParticleSystemManager* m = drv::particle_manager();
    char buf[1400];
    std::snprintf(buf, sizeof buf,
                  "efeitos_desenho galeria %d texturas %d telas %d disparos %llu recusados %llu quadros %llu tipos "
                  "%llu sistemas %zu criados %llu apagados %llu particulas %d nascidas %llu cheias %llu retangulos "
                  "%llu aditivos %llu riscos %llu | sons %llu claroes %llu destrocos %llu | lotes %llu triangulos "
                  "%llu guardas %llu | depuracao %llu sem_efeito %llu | %s",
                  have_gallery ? 1 : 0, texture_count, gal ? gal->active_count : 0, (unsigned long long)g.triggers,
                  (unsigned long long)g.refused, (unsigned long long)g.frames, (unsigned long long)g.particle_types,
                  m ? m->systems.size() : size_t(0), (unsigned long long)p.systems_created,
                  (unsigned long long)p.systems_deleted, m ? m->cache.count : 0, (unsigned long long)p.spawned,
                  (unsigned long long)p.refused, (unsigned long long)p.sprites, (unsigned long long)p.additive,
                  (unsigned long long)p.streaks, (unsigned long long)g.sounds, (unsigned long long)g.glares,
                  (unsigned long long)g.debris, (unsigned long long)batches, (unsigned long long)triangles,
                  (unsigned long long)(g.guards + p.guards), (unsigned long long)debug_fired,
                  (unsigned long long)debug_missing, extra_report().c_str());
    return std::string(buf) + '\n' + rest_report() + '\n' + section32_report();
}

// Seção 32: os efeitos das instâncias, os clarões dos canos, as marcas dos tiros e os eventos que faltavam.
std::string DrvFxApp::section32_report() const {
    const drv::SceneFxStats& f = drv::scene_fx_stats();
    const drv::MuzzleFlashStats& m = drv::muzzle_flash_stats();
    const drv::DecalStats& d = drv::decal_stats();
    const drv::FxEventStats& e = drv::fx_event_stats();
    char buf[1600];
    std::snprintf(buf, sizeof buf,
                  "efeitos_cena triggerfx %llu ligados %llu desligados %llu telas %llu limpas %llu suprimidas %llu "
                  "intensidades %llu dano %llu decaimentos %llu quadros %llu claroes %llu luzes %llu estimulos_zonas %llu "
                  "faiscas %llu dissolver %llu lampadas %llu guardas %llu | canos atores %llu/%llu cheios %llu pov %llu "
                  "carros %llu/%llu ocupados %llu desenhos %llu guardas %llu | marcas_tiros %llu chao %llu | eventos "
                  "sistema %llu/%llu cano %llu/%llu colisao %llu/%llu suprimir %llu/%llu interruptor %llu/%llu reflexo "
                  "%llu/%llu | carros_quadro %llu desenhos_quadro %llu | pedestres ligado %d conjunto %d tipos %d pedestres %d "
                  "especiais %d forcado %d passos %llu sem_porte %llu",
                  (unsigned long long)f.trigger_fx, (unsigned long long)f.toggled_on, (unsigned long long)f.toggled_off,
                  (unsigned long long)f.gfx_triggers, (unsigned long long)f.purges, (unsigned long long)f.suppressed,
                  (unsigned long long)f.intensities, (unsigned long long)f.damage, (unsigned long long)f.decays,
                  (unsigned long long)f.renders, (unsigned long long)f.glares, (unsigned long long)f.lights,
                  (unsigned long long)f.stimuli_zones, (unsigned long long)f.sparks, (unsigned long long)f.dissolves,
                  (unsigned long long)f.lamps, (unsigned long long)f.guards, (unsigned long long)m.actor_started,
                  (unsigned long long)m.actor_drawn, (unsigned long long)m.actor_full, (unsigned long long)m.pov_drawn,
                  (unsigned long long)m.vehicle_started, (unsigned long long)m.vehicle_drawn,
                  (unsigned long long)m.vehicle_busy, (unsigned long long)m.draws, (unsigned long long)m.guards,
                  (unsigned long long)d.world_scorches, (unsigned long long)d.world_feet,
                  (unsigned long long)e.system_anim, (unsigned long long)e.system_anim_none,
                  (unsigned long long)e.actor_flash, (unsigned long long)e.actor_flash_none,
                  (unsigned long long)e.collision_geometry, (unsigned long long)e.collision_none,
                  (unsigned long long)e.suppress, (unsigned long long)e.suppress_none,
                  (unsigned long long)e.switch_effect, (unsigned long long)e.switch_none,
                  (unsigned long long)e.reflective, (unsigned long long)e.reflective_none,
                  (unsigned long long)car_fx_frames, (unsigned long long)flash_draws, drv::pedestrian_globals().enabled,
                  drv::pedestrian_globals().ped_base,
                  drv::pedestrian_globals().set_count[drv::pedestrian_globals().ped_base % 7],
                  drv::pedestrian_globals().num_peds, drv::pedestrian_globals().num_special,
                  drv::pedestrian_globals().force_special,
                  (unsigned long long)drv::pedestrian_globals().manage_steps,
                  (unsigned long long)drv::pedestrian_globals().unported);
    return buf;
}

// Seção 29: os riscos das balas, os clarões dos tiros, os clarões, os destroços e os sons dos efeitos.
std::string DrvFxApp::rest_report() const {
    const drv::BulletStreakStats& b = drv::bullet_streak_stats();
    const drv::GlareStats& g = drv::glare_stats();
    char buf[1100];
    std::snprintf(buf, sizeof buf,
                  "efeitos_restos ajustes fx %d glare %d | riscos postos %llu cheios %llu desenhos %llu vertices %llu "
                  "rastros %llu acabados %llu balas_lentas %llu claroes_tiros %llu guardas %llu | claroes galeria %llu "
                  "postos %llu modelo %llu longe %llu costas %llu cheios %llu desenhos %llu retangulos %llu mundo "
                  "instancias %llu efeitos %llu galeria %llu desconhecidos %llu | quadro riscos %zu claroes %zu/%zu | "
                  "destrocos modelos %zu lancados %llu cheios %llu expirados %llu desenhos %llu pecas %llu galeria %llu "
                  "cartuchos %llu quadro %zu",
                  have_fx_tun ? 1 : 0, have_glare_tun ? 1 : 0, (unsigned long long)b.added, (unsigned long long)b.full,
                  (unsigned long long)b.drawn, (unsigned long long)b.vertices, (unsigned long long)b.tracers,
                  (unsigned long long)b.tracers_done, (unsigned long long)b.shells, (unsigned long long)b.flashes,
                  (unsigned long long)b.guards, (unsigned long long)g.gallery, (unsigned long long)g.added,
                  (unsigned long long)g.model, (unsigned long long)g.model_far, (unsigned long long)g.model_back,
                  (unsigned long long)g.full, (unsigned long long)g.drawn, (unsigned long long)g.sprites,
                  (unsigned long long)world_glare_instances, (unsigned long long)g.world_effects,
                  (unsigned long long)g.world_gfx, (unsigned long long)g.unknown_effects, volatile_draws.size(),
                  glare_list.colours.size(), world_glare_list.colours.size(), debris_geo.size(),
                  (unsigned long long)drv::debris_stats().added, (unsigned long long)drv::debris_stats().full,
                  (unsigned long long)drv::debris_stats().expired, (unsigned long long)drv::debris_stats().drawn,
                  (unsigned long long)drv::debris_stats().pieces, (unsigned long long)drv::debris_stats().gallery,
                  (unsigned long long)drv::debris_stats().gallery_single, debris_draws.size());
    return buf;
}

// ------------------------------------------------------------------------------------------------- o --check

namespace {

// Uma matriz de recorte do PS2 (vetores linha): a câmera na origem olhando para +z, fov vertical 60, perto 0,5, longe
// 2000 (z e x, y entre -w e w).
void check_mvp(float m[4][4]) {
    std::memset(m, 0, 64);
    const float f = 1.7320508f, a = 4.0f / 3.0f, n = 0.5f, far = 2000.0f;
    m[0][0] = f / a;
    m[1][1] = f;
    m[2][2] = (far + n) / (far - n);
    m[2][3] = 1.0f;
    m[3][2] = -2.0f * far * n / (far - n);
}

// As instruções de um render method (o census do objeto).
bool method_code(const std::vector<drv::RenderMethodInfo>& all, const char* name, std::vector<uint64_t>& out) {
    for (const drv::RenderMethodInfo& m : all)
        if (m.name == name) {
            out = m.code;
            return !out.empty();
        }
    return false;
}

// A cópia do microprograma no DRIVING.ELF: os mesmos bytes no arquivo.
bool in_elf(const std::vector<uint8_t>& elf, const std::vector<uint64_t>& code) {
    if (code.empty() || elf.size() < code.size() * 8) return false;
    const uint8_t* c = reinterpret_cast<const uint8_t*>(code.data());
    const size_t n = code.size() * 8;
    return std::search(elf.begin(), elf.end(), c, c + n) != elf.end();
}

std::string stats_line(const drv::FxVerifyStats& s) {
    char b[320];
    std::snprintf(b, sizeof b,
                  "execucoes %d falhas %d retangulos %d triangulos %d vertices %d diferentes st %d rgba %d xyz %d | "
                  "retangulos_com_adc %d%s%s",
                  s.runs, s.failed_runs, s.sprites, s.triangles, s.vertices, s.st_diff, s.rgba_diff, s.xyz_diff,
                  s.adc_sprites, s.first_error.empty() ? "" : " | ", s.first_error.c_str());
    return b;
}

}  // namespace

int drv_fx_check(const nf::DiscSource& disc, bool verify) {
    int fail = 0;
    drv::Bigf misc;
    if (!misc.open(disc, "DRIVING/MISC.VIV")) {
        std::printf("efeitos: sem o MISC.VIV\n");
        return 1;
    }
    std::vector<uint8_t> gal;
    read_from(&misc, "data\\gfx\\data\\gall1.gal", gal);
    drv::RParticleSystemManager_Init();
    drv::GFX_Init(gal);  // (uma cópia: cada grupo de efeitos recomeça dela)
    drv::gfx_stats() = drv::GfxStats();
    drv::particle_stats() = drv::ParticleStats();
    // As instâncias da cena na origem (os efeitos presos a uma instância), os grudados na galeria, nenhum corpo.
    drv::gfx_env().instance_position = [](uint32_t, int32_t, float m[4][4]) {
        std::memset(m, 0, 64);
        for (int i = 0; i < 4; ++i) m[i][i] = 1.0f;
        return true;
    };
    drv::particles_sticky_pos() = sticky_pos;
    drv::particles_rigid_pos() = nullptr;
    drv::GfxGallery& g = *drv::gfx_gallery();
    // Os efeitos da galeria: todos disparados diante da câmera, um a cada quadro, e 240 quadros de passos.
    std::vector<std::string> names;
    {
        drv::UGroup* root = g.root ? nf::p32_ptr<drv::UGroup>(g.root) : nullptr;
        drv::UData* a = root ? drv::UGroup_GetArray(root) : nullptr;
        for (uint32_t i = 0; root && i < drv::UGroup_GroupCount(root); ++i) {
            drv::UData* n = drv::UGroup_DataLocateTag(a + i, 0x4e616d65);
            if (n != drv::UGroup_DataEnd(a + i)) names.push_back(reinterpret_cast<const char*>(drv::UData_Data(n)));
        }
    }
    std::vector<uint64_t> sprites_code, tri_code, debris_code, dbg_code;
    drv::FxVerifyStats vs, vt, vdbg, vh;
    bool elf_sprites = false, elf_tri = false, elf_dbg = false;
    std::vector<std::unique_ptr<drv::eagl::DynamicLoader>> loaders;
    std::vector<std::vector<uint8_t>> keep;
    if (verify) {
        drv::eagl::register_eagl_types();
        std::vector<drv::RenderMethodInfo> all;
        for (const char* obj : {"eaglrm", "bondrm"}) {
            std::vector<uint8_t> b;
            if (!read_from(&misc, std::string("data\\render\\") + obj + ".o", b)) continue;
            keep.push_back(std::vector<uint8_t>(b.size() + 16));
            uint8_t* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(keep.back().data()) + 15) & ~uintptr_t(15));
            std::memcpy(p, b.data(), b.size());
            loaders.push_back(std::make_unique<drv::eagl::DynamicLoader>(p, uint32_t(b.size()), nullptr));
            auto c = drv::render_methods_census(*loaders.back(), obj);
            all.insert(all.end(), c.begin(), c.end());
        }
        method_code(all, "RenderSpritesRot", sprites_code);
        method_code(all, "MatDebugTexture", tri_code);
        method_code(all, "MatDebug", dbg_code);
        method_code(all, "BondDebris", debris_code);
        std::vector<uint8_t> elf;
        disc.read("DRIVING.ELF", elf);
        elf_sprites = in_elf(elf, sprites_code);
        elf_tri = in_elf(elf, tri_code);
        elf_dbg = in_elf(elf, dbg_code);
    }
    float mvp[4][4];
    check_mvp(mvp);
    const drv::FxVuGlobals vg;
    const float control[4] = {0.0f, drv::fx_sprites_control_y(45.0f, 4.0f), 0.0f, 0.0f};
    float control_z[4];
    std::memcpy(control_z, control, 16);
    control_z[2] = ps2::ee::mul(control[1], 1.4e7f);  // o Control.z do JustRender
    float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    drv::particles_set_view_matrix(view);
    uint32_t rng = 0x5eed1234u;
    auto rnd = [&rng](float a, float b) {
        rng = rng * 1664525u + 1013904223u;
        return a + (b - a) * float(rng >> 8) / float(1u << 24);
    };
    // Seção 29: os clarões das trilhas 'gg' (o RGlareManager com o "Render:Glare" do MIS01.VIV), desenhados a cada
    // quadro; uma amostra dos retângulos vai ao RenderSpritesRot com o Control dos clarões (z = 0xccbebc20).
    drv::Bigf mis_tun;
    const bool have_mis = mis_tun.open(disc, "DRIVING/MIS01.VIV");
    auto read_tun = [&](const std::string& name, std::string& text) {
        std::vector<uint8_t> b;
        if (!have_mis || !read_from(&mis_tun, name, b)) return false;
        text.assign(b.begin(), b.end());
        return true;
    };
    drv::GSystem_RANDOM_Startup(0x642f2b);
    drv::RGlareManager_Construct(drv::glare_manager(), drv::tuning_load(read_tun, "Render", "Glare", "paris_mis01"));
    drv::glare_stats() = drv::GlareStats();
    drv::FxVerifyStats vg_glare;
    float glare_control[4] = {0.0f, control[1], ps2::ffloat(0xccbebc20u), 0.0f};
    // Os destroços das trilhas 'gd' (os modelos do DRIVING.ELF), desenhados a cada quadro; uma amostra dos pedaços vai
    // ao BondDebris com a viewProj da conferência.
    std::vector<drv::DebrisGeometry> geo;
    const bool have_geo = load_debris_from_elf(disc, geo);
    drv::RDebris_Construct(drv::debris());
    drv::debris_stats() = drv::DebrisStats();
    drv::FxVerifyStats vd;
    uint64_t debris_seen = 0;
    const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int fired = 0, frames = 0;
    const int sprite_cap = 6000, tri_cap = 2000;
    uint64_t sprite_seen = 0;
    // Em grupos de 16 efeitos, cada um com a galeria e o gerente recomeçados (como a cada missão): o cache de tipos da
    // galeria tem 256 lugares (0x800 bytes) e os 106 efeitos juntos pedem mais (o PS2 escreveria além dele).
    const int group = 16, group_frames = 150;
    for (int f = 0; f < group_frames * ((int(names.size()) + group - 1) / group); ++f) {
        const int gi = f / group_frames, gf = f % group_frames;
        if (gf == 0 && gi > 0) {
            drv::GFX_Shutdown();
            drv::RParticleSystemManager_Shutdown();
            drv::RParticleSystemManager_Init();
            drv::GFX_Init(gal);
        }
        const size_t e = size_t(gi * group + gf / 2);
        if ((gf & 1) == 0 && gf / 2 < group && e < names.size()) {
            const uint32_t ref = drv::GFX_LookupReference(names[e].c_str());
            if (ref) {
                const float pos[3] = {rnd(-6, 6), rnd(-3, 2), rnd(12, 40)}, q[4] = {0, 0, 0, 1}, z3[3] = {0, 0, 0};
                drv::GFX_Trigger(ref, pos, q, z3, q, 0, 0, 0);
                ++fired;
            }
        }
        drv::RTimeData_Update(f + 1);
        drv::GFX_Update();
        std::vector<drv::DebrisDraw> dds;
        drv::RDebris_Draw(drv::debris(), drv::rtime_data().time_data, &dds);
        if (verify && have_geo) {
            for (const drv::DebrisDraw& dd : dds)
                for (int i = 0; i < dd.count && vd.triangles < 1500; ++i) {
                    if (debris_seen++ % 7) continue;
                    const drv::DebrisGeometry& gm = geo[size_t(dd.type)];
                    uint32_t col[4];
                    for (int k = 0; k < 4; ++k) col[k] = (dd.colours[size_t(i)] >> (8 * k)) & 0xff;
                    drv::fx_verify_debris(debris_code, vg, mvp, dd.simtick, &dd.offsets[size_t(i) * 4],
                                          &dd.velocity[size_t(i) * 4], &dd.quats[size_t(i) * 4], col, gm.coords.data(),
                                          gm.uv.data(), gm.count, vd);
                }
        }
        drv::GlareDrawList glares;
        drv::RGlareManager_DrawGlares(drv::glare_manager(), origin, control[1], &glares);
        if (verify) {
            for (size_t i = 0; i < glares.colours.size() && vg_glare.sprites < 2000; ++i) {
                uint32_t col[4];
                for (int k = 0; k < 4; ++k) col[k] = (glares.colours[i] >> (8 * k)) & 0xff;
                drv::fx_verify_sprite(sprites_code, vg, mvp, glare_control, &glares.coords[i * 4], &glares.uvuv[i * 4],
                                      col, vg_glare);
            }
        }
        drv::RParticleSystemManager& m = *drv::particle_manager();
        drv::RParticleSystemManager_UpdateSpawnAllSystems(m);
        drv::RParticleSystemManager_UpdateAndRenderAllSystems(m);
        ++frames;
        if (!verify) continue;
        // Os retângulos e os riscos deste quadro no microprograma.
        for (int b = 0; b < 2; ++b) {
            for (const drv::ParticleDrawLists::Sprites* l : {&m.lists.normal[b], &m.lists.additive[b]}) {
                for (size_t i = 0; i < l->colours.size() && vs.sprites < sprite_cap; ++i) {
                    if (sprite_seen++ % 31) continue;  // uma amostra espalhada por todos os efeitos
                    uint32_t col[4];
                    for (int k = 0; k < 4; ++k) col[k] = (l->colours[i] >> (8 * k)) & 0xff;
                    drv::fx_verify_sprite(sprites_code, vg, mvp, (sprite_seen & 2) ? control_z : control, &l->coords[i * 4],
                                          &l->uvuv[i * 4], col, vs);
                }
            }
            const std::vector<float>& tc = m.lists.tri_coords[b];
            static const float kStq[3][4] = {{0, 0, 1, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}};
            for (size_t t = 0; t + 2 < m.lists.tri_colours[b].size() && vt.triangles < tri_cap; t += 3) {
                float pos[3][4];
                uint32_t col[3][4];
                for (int v = 0; v < 3; ++v) {
                    std::memcpy(pos[v], &tc[(t + size_t(v)) * 4], 16);
                    for (int k = 0; k < 4; ++k) col[v][k] = (m.lists.tri_colours[b][t + size_t(v)] >> (8 * k)) & 0xff;
                }
                if (!drv::fx_triangle_inside(mvp, pos)) continue;
                drv::fx_verify_triangle(tri_code, vg, mvp, pos, kStq, col, vt);
            }
        }
    }
    // As marcas de verdade (o decal.dat do MIS01.VIV, 96 marcas no chão diante da câmera, depois do atraso) e os
    // rastros (RMissileStreak::Update de mísseis sorteados, o que o Draw desenha), triângulo a triângulo no
    // MatDebugTexture.
    int decal_tris = 0, streak_tris = 0;
    if (verify) {
        drv::Bigf mis;
        std::vector<uint8_t> dat;
        if (mis.open(disc, "DRIVING/MIS01.VIV")) read_from(&mis, "data\\render\\decal.dat", dat);
        static drv::DecalState ds;
        drv::RDecalManager_Construct(ds, dat);
        for (int i = 0; i < drv::kDecalCount; ++i) {
            const float pos[4] = {rnd(-15, 15), rnd(-6, -1), rnd(8, 120), 1.0f};
            float n[4] = {rnd(-0.2f, 0.2f), 1.0f, rnd(-0.2f, 0.2f), 0.0f};
            const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int k = 0; k < 3; ++k) n[k] /= l;
            const float dir[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            const float side = rnd(0.5f, 6.0f);
            drv::RDecalManager_AddDecal(ds, pos, n, dir, int32_t(i % 8), side, side);
        }
        for (int f = 0; f < 6; ++f) drv::RDecalManager_Draw(ds);
        for (int t = 0; t < drv::kDecalCount * 2; ++t) {
            float pos[3][4], stq[3][4];
            uint32_t col[3][4];
            for (int v = 0; v < 3; ++v) {
                const int k = t * 3 + v;
                std::memcpy(pos[v], ds.b.coords[k], 16);
                std::memcpy(stq[v], ds.b.stq[k], 16);
                for (int b = 0; b < 4; ++b) col[v][b] = (ds.b.colours[k] >> (8 * b)) & 0xff;
            }
            if (!drv::fx_triangle_inside(mvp, pos)) continue;
            drv::fx_verify_triangle(tri_code, vg, mvp, pos, stq, col, vt);
            ++decal_tris;
        }
        static drv::MissileStreakTables st;
        drv::RMissileStreak_Init(st);
        for (int m = 0; m < 24; ++m) {
            static drv::RMissileStreakData sd;
            sd = drv::RMissileStreakData();
            sd.kind = m % 3;
            float p[4] = {rnd(-10, 10), rnd(-4, 4), rnd(10, 40), 1.0f};
            const float v[3] = {rnd(-0.3f, 0.3f), rnd(-0.2f, 0.2f), rnd(0.5f, 2.0f)};
            const float right[3] = {1.0f, 0.0f, 0.0f};
            const int updates = int(rnd(8, 120));
            for (int u = 0; u < updates; ++u) {
                drv::RMissileStreak_Update(sd, p, right);
                for (int k = 0; k < 3; ++k) p[k] += v[k];
            }
            drv::StreakDrawRange dr;
            if (!drv::RMissileStreak_Draw(sd, dr)) continue;
            for (int k = 0; k + 2 < dr.count; ++k) {
                float pos[3][4], stq[3][4];
                uint32_t col[3][4];
                for (int j = 0; j < 3; ++j) {
                    std::memcpy(pos[j], sd.verts[dr.first + k + j], 16);
                    pos[j][3] = 1.0f;
                    std::memcpy(stq[j], st.stq[dr.stq_first + k + j], 16);
                    for (int b = 0; b < 4; ++b) col[j][b] = (st.colours[k + j] >> (8 * b)) & 0xff;
                }
                if (!drv::fx_triangle_inside(mvp, pos)) continue;
                drv::fx_verify_triangle(tri_code, vg, mvp, pos, stq, col, vt);
                ++streak_tris;
            }
        }
    }
    // Retângulos e triângulos sorteados (o microprograma não sabe de onde vêm): perto da câmera e fora da tela, o
    // caminho do ADC dos retângulos; quaisquer STQ e cores nos triângulos.
    const int extra_sprites = verify ? 2000 : 0, extra_tris = verify ? 2000 : 0;
    for (int i = 0; i < extra_sprites; ++i) {
        const float coord[4] = {rnd(-30, 30), rnd(-20, 20), i % 3 == 0 ? rnd(0.6f, 6.0f) : rnd(2.0f, 300.0f),
                                rnd(0.02f, 8.0f)};
        const float uvuv[4] = {rnd(0, 1), rnd(0, 1), rnd(0.01f, 1.0f), rnd(-12.0f, 12.0f)};
        uint32_t col[4];
        for (uint32_t& c : col) c = uint32_t(rnd(0, 255.99f));
        drv::fx_verify_sprite(sprites_code, vg, mvp, (i & 1) ? control_z : control, coord, uvuv, col, vs);
    }
    for (int i = 0, tries = 0; i < extra_tris && tries < extra_tris * 20; ++tries) {
        float pos[3][4], stq[3][4];
        uint32_t col[3][4];
        const float cz = rnd(2.0f, 400.0f);
        for (int v = 0; v < 3; ++v) {
            pos[v][0] = rnd(-0.6f, 0.6f) * cz;
            pos[v][1] = rnd(-0.45f, 0.45f) * cz;
            pos[v][2] = cz + rnd(-1.0f, 1.0f);
            pos[v][3] = 1.0f;
            stq[v][0] = rnd(-2, 2);
            stq[v][1] = rnd(-2, 2);
            stq[v][2] = rnd(0.25f, 4.0f);
            stq[v][3] = 1.0f;
            for (uint32_t& c : col[v]) c = uint32_t(rnd(0, 255.99f));
        }
        if (!drv::fx_triangle_inside(mvp, pos)) continue;
        drv::fx_verify_triangle(tri_code, vg, mvp, pos, stq, col, vt);
        ++i;
    }
    // Seção 29: os riscos das balas (a tabela do "Render:FX" de Paris; riscos sorteados diante da câmera, o
    // RBulletStreak::Draw) no MatVolatileTexture, o mesmo microprograma do MatDebugTexture com o UV desempacotado com a
    // linha (0, 0, 1, 1): o STQ = (u, v, 1, 1).
    drv::FxVerifyStats vv;
    std::vector<uint64_t> vol_code;
    bool vol_same = false;
    int streak_draws = 0;
    if (verify) {
        // (os métodos de novo: os carregadores de cima continuam vivos)
        std::vector<drv::RenderMethodInfo> all;
        for (size_t k = 0; k < loaders.size(); ++k) {
            auto c = drv::render_methods_census(*loaders[k], k == 0 ? "eaglrm" : "bondrm");
            all.insert(all.end(), c.begin(), c.end());
        }
        method_code(all, "MatVolatileTexture", vol_code);
        vol_same = !vol_code.empty() && vol_code == tri_code;
        static drv::BulletStreakState bs;
        bs = drv::BulletStreakState();
        drv::RBulletStreak_StaticInit(bs);
        drv::RBulletStreak_Init(bs, drv::tuning_load(read_tun, "Render", "FX", "paris_mis01"));
        std::vector<drv::VolatileDraw> draws;
        for (int round = 0; round < 40 && vv.triangles < 1500; ++round) {
            for (int k = 0; k < 40; ++k) {
                const float p[4] = {rnd(-5, 5), rnd(-3, 2), rnd(6, 40), 1.0f};
                float d[4] = {rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), 1.0f};
                const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (l < 0.1f) continue;
                for (int c = 0; c < 3; ++c) d[c] /= l;
                drv::RBulletStreak_Add(bs, origin, p, d, int32_t(rnd(0, 6.99f)));
            }
            draws.clear();
            drv::RBulletStreak_Draw(bs, &draws);
            for (const drv::VolatileDraw& v : draws) {
                ++streak_draws;
                for (int t = 0; t + 2 < v.count; t += 3) {
                    float pos[3][4], stq[3][4];
                    uint32_t col[3][4];
                    for (int j = 0; j < 3; ++j) {
                        std::memcpy(pos[j], &v.coords[size_t(t + j) * 4], 16);
                        stq[j][0] = v.uv[size_t(t + j) * 2];
                        stq[j][1] = v.uv[size_t(t + j) * 2 + 1];
                        stq[j][2] = 1.0f;
                        stq[j][3] = 1.0f;
                        for (int k = 0; k < 4; ++k) col[j][k] = (v.colours[size_t(t + j)] >> (8 * k)) & 0xff;
                    }
                    if (!drv::fx_triangle_inside(mvp, pos)) continue;
                    drv::fx_verify_triangle(vol_code, vg, mvp, pos, stq, col, vv);
                }
            }
        }
    }
    // Os desenhos do HUD (seção 28): o MatDebug (os quadrados sem textura) e o MatDebugTexture com as faixas (PRIM 4) e os
    // SPRITE (6) além da lista (3), n vértices; a câmera sorteada e a do HUD (a ortográfica que leva as coordenadas da
    // tela do PS2, 640 x 448, aos pixels: a que o port supõe no desenho do HUD).
    int hud_ortho = 0;
    if (verify && !dbg_code.empty()) {
        float ortho[4][4] = {{1.0f / 320.0f, 0, 0, 0}, {0, -1.0f / 224.0f, 0, 0}, {0, 0, 1e-6f, 0}, {-1.0f, 1.0f, 0, 1.0f}};
        const int kPrims[3] = {3, 4, 6};
        for (int i = 0; i < extra_tris; ++i) {
            const bool hud_cam = (i & 3) == 0;
            const float(*m)[4] = hud_cam ? ortho : mvp;
            const int prim = kPrims[i % 3];
            const bool textured = (i & 1) != 0;
            const int n = prim == 6 ? 2 * (1 + i % 8) : 3 + i % 12;
            float pos[40][4], stq[40][4];
            uint32_t col[40][4];
            bool inside = true;
            for (int v = 0; v < n; ++v) {
                if (hud_cam) {
                    pos[v][0] = rnd(40, 600);
                    pos[v][1] = rnd(30, 420);
                    pos[v][2] = rnd(0.5f, 12);
                } else {
                    const float cz = rnd(2.0f, 400.0f);
                    pos[v][0] = rnd(-0.5f, 0.5f) * cz;
                    pos[v][1] = rnd(-0.4f, 0.4f) * cz;
                    pos[v][2] = cz;
                }
                pos[v][3] = 1.0f;
                stq[v][0] = rnd(-2, 2);
                stq[v][1] = rnd(-2, 2);
                stq[v][2] = rnd(0.25f, 4.0f);
                stq[v][3] = 1.0f;
                for (uint32_t& c : col[v]) c = uint32_t(rnd(0, 255.99f));
            }
            for (int v = 0; v + 2 < n || (v == 0 && n < 3); v += 3) {
                float tri[3][4];
                for (int k = 0; k < 3; ++k) std::memcpy(tri[k], pos[std::min(n - 1, v + k)], 16);
                if (!drv::fx_triangle_inside(m, tri)) inside = false;
            }
            if (!inside) continue;
            if (hud_cam) ++hud_ortho;
            drv::fx_verify_list(textured ? tri_code : dbg_code, textured, vg, m, prim, n, pos, stq, col,
                                textured ? vh : vdbg);
        }
    }
    const drv::GfxStats& gs = drv::gfx_stats();
    const drv::ParticleStats& ps = drv::particle_stats();
    std::printf("efeitos: galeria %s efeitos %zu disparados %d quadros %d tipos %llu sistemas criados %llu particulas %llu "
                "retangulos %llu aditivos %llu riscos %llu | sons %llu claroes %llu destrocos %llu | guardas %llu/%llu\n",
                gal.empty() ? "ausente" : "gall1.gal", names.size(), fired, frames,
                (unsigned long long)gs.particle_types, (unsigned long long)ps.systems_created,
                (unsigned long long)ps.spawned, (unsigned long long)ps.sprites, (unsigned long long)ps.additive,
                (unsigned long long)ps.streaks, (unsigned long long)gs.sounds, (unsigned long long)gs.glares,
                (unsigned long long)gs.debris, (unsigned long long)gs.guards, (unsigned long long)ps.guards);
    if (gal.empty() || gs.guards + ps.guards) ++fail;
    if (verify) {
        std::printf("efeitos, RenderSpritesRot (copia no DRIVING.ELF %s) %s\n", elf_sprites ? "igual" : "AUSENTE",
                    stats_line(vs).c_str());
        std::printf("efeitos, MatDebugTexture (copia no DRIVING.ELF %s; marcas %d, rastros %d, sorteados %d) %s\n",
                    elf_tri ? "igual" : "AUSENTE", decal_tris, streak_tris, vt.triangles - decal_tris - streak_tris,
                    stats_line(vt).c_str());
        std::printf("hud, MatDebug (copia no DRIVING.ELF %s) %s\n", elf_dbg ? "igual" : "AUSENTE", stats_line(vdbg).c_str());
        std::printf("hud, MatDebugTexture com faixas e SPRITE (camera do HUD %d) %s\n", hud_ortho, stats_line(vh).c_str());
        if (sprites_code.empty() || tri_code.empty() || vs.failed_runs || vt.failed_runs || vs.st_diff || vs.rgba_diff ||
            vs.xyz_diff || vt.st_diff || vt.rgba_diff || vt.xyz_diff)
            ++fail;
        if (dbg_code.empty() || vdbg.failed_runs || vh.failed_runs || vdbg.rgba_diff || vdbg.xyz_diff || vh.st_diff ||
            vh.rgba_diff || vh.xyz_diff)
            ++fail;
        // Seção 29.
        const drv::GlareStats& gl = drv::glare_stats();
        std::printf("efeitos, claroes (RGlareManager: galeria %llu desenhos %llu retangulos %llu) no RenderSpritesRot %s\n",
                    (unsigned long long)gl.gallery, (unsigned long long)gl.drawn, (unsigned long long)gl.sprites,
                    stats_line(vg_glare).c_str());
        std::printf("efeitos, MatVolatileTexture (o microprograma do MatDebugTexture: %s; riscos das balas, %d desenhos) %s\n",
                    vol_same ? "igual" : "DIFERENTE", streak_draws, stats_line(vv).c_str());
        const drv::DebrisStats& db = drv::debris_stats();
        std::printf("efeitos, BondDebris (destrocos: modelos do DRIVING.ELF %s, lancados %llu, pecas desenhadas %llu; pecas "
                    "inteiras no recorte, uma em 7) %s\n",
                    have_geo ? "lidos" : "AUSENTES", (unsigned long long)db.added, (unsigned long long)db.pieces,
                    stats_line(vd).c_str());
        if (!have_geo || debris_code.empty() || vd.triangles == 0 || vd.failed_runs || vd.st_diff || vd.rgba_diff ||
            vd.xyz_diff)
            ++fail;
        if (!vol_same || vg_glare.sprites == 0 || vv.triangles == 0 || vg_glare.failed_runs || vv.failed_runs ||
            vg_glare.st_diff || vg_glare.rgba_diff || vg_glare.xyz_diff || vv.st_diff || vv.rgba_diff || vv.xyz_diff)
            ++fail;
    }
    drv::GFX_Shutdown();
    drv::RParticleSystemManager_Shutdown();
    drv::gfx_env().instance_position = nullptr;
    drv::particles_sticky_pos() = nullptr;
    return fail;
}
