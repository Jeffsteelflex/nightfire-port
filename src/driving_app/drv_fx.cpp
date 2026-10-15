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
#include "driving/fx/fx_verify.h"
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
}

void DrvFxApp::update(uint32_t step, const float camera[16], const float view[16]) {
    if (!ready || step == last_step) return;
    last_step = step;
    const float* eye = camera + 12;
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
    drv::GFX_Update();
    if (drv::RParticleSystemManager* m = drv::particle_manager()) {
        drv::RParticleSystemManager_UpdateSpawnAllSystems(*m);
        drv::RParticleSystemManager_UpdateAndRenderAllSystems(*m);
    }
    // [driving] 0x001c3d58 (o fim do quadro desenhado): RLightManager::FinishLights publica as luzes pontuais pedidas
    // neste quadro e as explosões vivas (com o fTime deste quadro), para o desenho do quadro seguinte.
    drv::RLightManager_FinishLights(drv::light_manager_positional(), drv::rtime_data().time);
    ++updates;
}

void DrvFxApp::push(drv::DrvScene& scene, float fov, float aspect) {
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
    std::vector<uint64_t> sprites_code, tri_code;
    drv::FxVerifyStats vs, vt;
    bool elf_sprites = false, elf_tri = false;
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
        std::vector<uint8_t> elf;
        disc.read("DRIVING.ELF", elf);
        elf_sprites = in_elf(elf, sprites_code);
        elf_tri = in_elf(elf, tri_code);
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
        if (sprites_code.empty() || tri_code.empty() || vs.failed_runs || vt.failed_runs || vs.st_diff || vs.rgba_diff ||
            vs.xyz_diff || vt.st_diff || vt.rgba_diff || vt.xyz_diff)
            ++fail;
    }
    drv::GFX_Shutdown();
    drv::RParticleSystemManager_Shutdown();
    drv::gfx_env().instance_position = nullptr;
    drv::particles_sticky_pos() = nullptr;
    return fail;
}
