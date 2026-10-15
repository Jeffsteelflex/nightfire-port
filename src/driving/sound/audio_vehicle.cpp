// O som do carro do jogador (driving/sound/audio.h): AVehicle, APlayerVehicle, AEngine e ARaceEngine, AVehicleWind,
// o PBondCar::InitAudioObject do carro do jogador, o fim do PBondCar::Simulate que alimenta o objeto de som e a parte de
// som do ECollision. Portado do MIPS do DRIVING.ELF (SupportPS2.lib, BondAudioPS2.lib); docs/driving.md seção 17.
//
// Divergências (port):
//  - as armas do carro (AGun, ATurret, AZoomObj), a buzina (AHorn) e o alarme (ACarAlarm) não estão portados: os
//    ponteiros ficam nulos e o AVehicle::Play os pula (o PS2 cria o AGun "Machine Gun" do jogador, mudo sem tiro);
//  - o PBondCar::Simulate grava em +0xb0/+0xb4 (as armas, pelo SMissionManager e pelo estado da animação) e em +0x50
//    (RPlayerCamera::GetForwardAimVec4): o port grava 1, 0 e zero (só as armas os leem);
//  - o submarino e os carros de IA (as outras classes) não estão portados: só o caminho do carro do jogador;
//  - as contas em double do AVehicleWind (o fptodp/dpmul/dpadd/dptofp da libgcc) são double do C++ (IEEE, o mesmo
//    arredondamento); o resto na FPU do EE (platform/ps2_float.h).

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#include "driving/camera/dafi.h"
#include "driving/game/bond_car.h"
#include "driving/physics/collision.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/world.h"
#include "driving/real/vu0_math.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/snd.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

// ---- AEngine, ARaceEngine, AVehicleWind (as classes em audio_internal.h) -------------------------------------------

namespace {

APlayerVehicle* g_player = nullptr;
std::map<std::string, std::unique_ptr<AEngine>, NoCaseLess> g_engines;  // URefCounter<AEngine>
uint32_t g_gate = 0;                 // 0x00355d0c
int32_t g_cooldown[12] = {};          // 0x00355cd8 o tick até o qual cada timbre do choque descansa

float approach(float cur, float target, float step) {
    const float d = ee::sub(target, cur);
    if (ee::le(d, ee::neg(step))) return ee::sub(cur, step);
    if (ee::le(step, d)) return ee::add(cur, step);
    return target;
}

int32_t patch_of(ABank* b, const char* name) { return b->index.Lookup(name); }

// O AOneShotSound montado no lugar (o AVehicle, o ARaceEngine): na posição e com a velocidade dadas.
void oneshot(const char* mix, ABank* bank, int32_t patch, float volume, const float pos[3], const float vel[3]) {
    AOneShotSound* s = new AOneShotSound(mix, bank->snd_bank, patch, volume);
    std::memcpy(s->pos, pos, 12);
    std::memcpy(s->vel, vel, 12);
}

// Os timbres dos choques por força (0x003c27d8, 0x003c2838, 0x003c28b8).
struct HitSound {
    float threshold;
    const char* name;
};
constexpr HitSound kHits12[12] = {{0.05f, "SFX_Cobble1"}, {0.1f, "SFX_Cobble2"}, {0.15f, "SFX_Light1"},
                                  {0.2f, "SFX_Light2"},   {0.25f, "SFX_Light3"}, {0.3f, "SFX_Light4"},
                                  {0.35f, "SFX_Med1"},    {0.4f, "SFX_Med2"},    {0.45f, "SFX_Med3"},
                                  {0.5f, "SFX_Med4"},     {0.55f, "SFX_Heavy1"}, {0.6f, "SFX_Heavy2"}};
constexpr HitSound kHits10[10] = {{0.05f, "SFX_Light1"}, {0.1f, "SFX_Light2"}, {0.2f, "SFX_Light3"},
                                  {0.25f, "SFX_Light4"}, {0.3f, "SFX_Med1"},   {0.35f, "SFX_Med2"},
                                  {0.4f, "SFX_Med3"},    {0.45f, "SFX_Med4"},  {0.5f, "SFX_Heavy1"},
                                  {0.55f, "SFX_Heavy2"}};
constexpr HitSound kHitsSand[3] = {{0.2f, "SFX_sandbump1"}, {0.35f, "SFX_sandbump2"}, {0.5f, "SFX_sandbump3"}};

}  // namespace

// [driving] 0x002fa8f0 AEngine::Play(APath &): as rotações vão às do carro (menos o deslizamento de trás) ou, com uma
// roda de trás no ar, ao máximo acelerando; a carga ao acelerador.
void AEngine::Play(APath&) {
    const AVehicle& v = *vehicle;
    const float count = ee::cvt_s(sound_count());
    const float fall = ee::mul(count, 150.0f);
    float step = ee::mul(count, 0.05f);
    float target_load = v.gas;
    float target;
    if (v.surface[2] == 0 || v.surface[3] == 0) {
        target = ee::lt(0.0f, target_load) ? 10000.0f : 0.0f;
    } else {
        target = ee::sub(ee::add(v.rpm, v.rpm_wobble), ee::mul(v.back_skid, 1000.0f));
    }
    float cur = rpm;
    if (ee::le(ee::sub(target, cur), ee::neg(fall))) {
        step = ee::mul(step, 3.0f);
        target_load = ee::mul(target_load, 0.125f);
        cur = ee::sub(cur, ee::mul(fall, 0.5f));
    } else {
        cur = ee::add(cur, ee::mul(ee::sub(target, cur), 0.1f));
    }
    rpm = cur;
    load = approach(load, target_load, step);
    pitch = ee::mul(ee::add(rpm, 2000.0f), inv_max);
    load_amp = ee::sqrt(load);
    idle_amp = ee::sqrt(ee::sub(1.0f, load));
}

// [driving] 0x002ff040 ARaceEngine::ARaceEngine
ARaceEngine::ARaceEngine() {
    AMix* mix = AMix_Get("Player Car");
    ABank* b = ABank_Category(9);
    static const char* kNames[10] = {"SFX_IN-idle",   "SFX_OUT-idle",   "SFX_IN-lowload", "SFX_OUT-lowload",
                                     "SFX_IN-midload", "SFX_OUT-midload", "SFX_IN-hiload",  "SFX_OUT-hiload",
                                     "SFX_IN-cruz",   "SFX_OUT-cruz"};
    for (int i = 0; i < 10; ++i) voices[i] = new AVoice(mix, b->snd_bank, patch_of(b, kNames[i]));
}

// [driving] 0x002ff320 ARaceEngine::Play(APath &): os dez timbres (dentro do carro até 1 m da câmera, fora depois de
// 2 m, cruzados entre os dois), o "vaccuum" quando a carga passa de 0,2 e a oscilação dos primeiros 60 passos.
void ARaceEngine::Play(APath& path) {
    AEngine::Play(path);
    const float count = ee::cvt_s(sound_count());
    if (ee::lt(load, 0.2f)) {
        start = 0.0f;
    } else if (ee::lt(start, 60.0f)) {
        if (ee::eq(start, 0.0f)) {
            ABank* b = ABank_Category(9);
            oneshot("Player Car", b, patch_of(b, "SFX_vaccuum"), 0.1f, vehicle->pos, vehicle->vel);
        }
        const float t = start;
        start = ee::add(t, count);
        if (ee::lt(0.5f, load)) {
            float f = ee::div(start, 10.0f);
            while (ee::lt(1.0f, f)) f = ee::sub(f, 1.0f);
            const float s = VU0_Sin(f);
            pitch = ee::add(pitch, ee::mul(ee::mul(s, 0.3f), ee::div(2.0f, ee::add(start, 2.0f))));
        }
    }
    if (ee::lt(rpm, 2000.0f)) {
        float f = ee::mul(rpm, 0.00033333333f);
        if (ee::lt(f, 0.0f)) f = 0.0f;
        low = ee::sqrt(ee::sub(1.0f, f));
        high = ee::sqrt(f);
    } else {
        low = 0.0f;
        high = 1.0f;
    }
    const float d = path.distance;
    float in, out;
    if (ee::lt(2.0f, d)) {
        in = 0.0f;
        out = ee::mul(vehicle->volume, AMix_GetVolume(vehicle->mix));
    } else if (ee::lt(1.0f, d)) {
        const float x = ee::sub(d, 1.0f), y = ee::sub(1.0f, x);
        in = ee::mul(ee::mul(ee::sqrt(y), vehicle->volume), AMix_GetVolume(vehicle->mix));
        out = ee::mul(ee::mul(ee::sqrt(x), vehicle->volume), AMix_GetVolume(vehicle->mix));
    } else {
        in = ee::mul(vehicle->volume, AMix_GetVolume(vehicle->mix));
        out = 0.0f;
    }
    const int32_t view = path.listener->view;
    for (int k = 0; k < 2; ++k) {
        const float vol = k == 0 ? in : out;
        AVoice_Play(ee::mul(ee::mul(ee::mul(vol, low), idle_amp), path.volume), ee::mul(ee::mul(pitch, 1.1f), path.pitch),
                    path.azimuth, 0.0f, 0.0f, voices[0 + k], view);
        AVoice_Play(ee::mul(ee::mul(vol, load_amp), path.volume), ee::mul(pitch, path.pitch), path.azimuth, 0.0f, 0.0f,
                    voices[2 + k], view);
        AVoice_Play(ee::mul(ee::mul(ee::mul(vol, high), load_amp), path.volume), ee::mul(pitch, path.pitch),
                    path.azimuth, 0.0f, 0.0f, voices[4 + k], view);
        AVoice_Play(ee::mul(ee::mul(ee::mul(vol, high), load_amp), path.volume), ee::mul(pitch, path.pitch),
                    path.azimuth, 0.0f, 0.0f, voices[6 + k], view);
        AVoice_Play(ee::mul(ee::mul(ee::mul(vol, high), idle_amp), path.volume), ee::mul(pitch, path.pitch),
                    path.azimuth, 0.0f, 0.0f, voices[8 + k], view);
    }
    AudioStats& st = audio_stats_mut();
    st.rpm = rpm;
    st.engine_pitch = pitch;
    st.engine_load = load;
}

// [driving] 0x002faae8 AEngine::Get(char *, AVehicle &, float)
static AEngine* AEngine_Get(float max_rpm, const char* name, AVehicle* vehicle) {
    auto it = g_engines.find(name);
    if (it == g_engines.end()) it = g_engines.emplace(name, std::make_unique<ARaceEngine>()).first;
    AEngine* e = it->second.get();
    e->vehicle = vehicle;
    e->rpm = 0.0f;
    e->load = 0.0f;
    e->start = 0.0f;
    e->inv_max = ee::div(1.0f, max_rpm);
    return e;
}

// [driving] 0x002fe9c0 AVehicleWind::AVehicleWind(AVehicle &)
AVehicleWind::AVehicleWind(AVehicle* v) : vehicle(v), mix(AMix_Get("Player Wind")) {
    ABank* b = ABank_Category(4);
    voice = new AVoice(mix, b->snd_bank, patch_of(b, "SFX_windy"));
}

// [driving] 0x002feaa0 AVehicleWind::Play(APath &, float): o nível e o pitch passeiam entre limites, com passos
// sorteados (rrandom em double), e o volume sobe com a velocidade acima de 5.
void AVehicleWind::Play(float count, APath& path) {
    level = ee::add(level, ee::mul(level_step, count));
    pitch = ee::add(pitch, ee::mul(pitch_step, count));
    if (ee::lt(1.0f, level)) level_step = float(double(rrandom()) * -0.02 - 0.002);
    if (ee::lt(level, 0.4f)) level_step = float(double(rrandom()) * 0.02 + 0.002);
    if (ee::lt(2.8f, pitch)) pitch_step = float(double(rrandom()) * -0.01 - 0.001);
    if (ee::lt(pitch, 1.0f)) pitch_step = float(double(rrandom()) * 0.03 + 0.001);
    float speed = 0.0f;
    if (ee::lt(5.0f, vehicle->speed)) {
        speed = ee::mul(ee::sub(vehicle->speed, 5.0f), 0.2f);
        if (ee::lt(1.0f, speed)) speed = 1.0f;
    }
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(ee::mul(ee::mul(level, speed), mv), volume), path.volume), ee::mul(pitch, path.pitch),
                azimuth, 0.0f, 0.0f, voice, path.listener->view);
}

// [driving] 0x002fed98 AVehicleWind::Stop(APath &)
void AVehicleWind::Stop(APath& path) {
    AVoice_Play(ee::mul(path.volume, 0.0f), path.pitch, azimuth, 0.0f, 0.0f, voice, path.listener->view);
}

// ---- AVehicle ------------------------------------------------------------------------------------------------------

// [driving] 0x002defd0 AVehicle::AVehicle(char *, ASceneObj::SurfaceType)
AVehicle::AVehicle(const char* mix_name, int32_t surface) : ABaseSound(mix_name, 4) {
    // ASceneObj::ASceneObj (0x002f0480): o mesmo que o ABaseSound, +0xc4 = 1.
    ABank* generic = ABank_Category(0);
    collisions = ABank_Category(1);
    mix_asphalt = AMix_Get("Asphalt");
    mix_offroad = AMix_Get("OffRoad");
    mix_skids = AMix_Get("Skids");
    mix_scrapes = AMix_Get("Scrapes");
    Reset();
    const int32_t g = generic->snd_bank, c = collisions->snd_bank;
    road[0] = new AVoice(mix_offroad, g, patch_of(generic, "SFX_OffRN"));
    road[1] = new AVoice(mix_offroad, g, patch_of(generic, "SFX_GravelRN"));
    road[2] = new AVoice(mix_offroad, g, patch_of(generic, "SFX_CobbleRN"));
    grass_skids[0] = new AVoice(mix_skids, g, patch_of(generic, "SFX_GrassSkid"));
    grass_skids[1] = new AVoice(mix_skids, g, patch_of(generic, "SFX_GrassSkid"));
    offroad_skids[0] = new AVoice(mix_skids, g, patch_of(generic, "SFX_OffRoadSkid"));
    offroad_skids[1] = new AVoice(mix_skids, g, patch_of(generic, "SFX_OffRoadSkid"));
    ice_skids[0] = new AVoice(mix_skids, g, patch_of(generic, "SFX_iceskidfront"));
    ice_skids[1] = new AVoice(mix_skids, g, patch_of(generic, "SFX_iceskidback"));
    scrapes[0] = new AVoice(mix_scrapes, c, patch_of(collisions, "SFX_ScrapeMetal"));
    scrapes[1] = new AVoice(mix_scrapes, c, patch_of(collisions, "SFX_ScrapeRock"));
    scrapes[2] = new AVoice(mix_scrapes, c, patch_of(collisions, "SFX_scrapesand"));
    cobble_patch[0] = patch_of(collisions, "SFX_Cobble1");
    cobble_patch[1] = patch_of(collisions, "SFX_Cobble2");
    rail_patch[0] = patch_of(collisions, "SFX_Railbump1");
    rail_patch[1] = patch_of(collisions, "SFX_Railbump2");
    if (surface == 0xe) {
        squeal = 0;
        skid[0] = new AVoice(mix_skids, g, patch_of(generic, "SFX_SkidFront1"));
        skid[1] = new AVoice(mix_skids, g, patch_of(generic, "SFX_SkidBack1"));
    } else {
        squeal = 1;
        skid[0] = new AVoice(mix_skids, c, patch_of(collisions, "SFX_SquealFront1"));
        skid[1] = new AVoice(mix_skids, c, patch_of(collisions, "SFX_SquealBack1"));
    }
    for (int i = 0; i < 4; ++i) surface_type = 0, this->surface[i] = 1, prev_surface[i] = 1;
}

AVehicle::~AVehicle() {
    for (AVoice* v : road) delete v;
    for (AVoice* v : skid) delete v;
    for (AVoice* v : grass_skids) delete v;
    for (AVoice* v : offroad_skids) delete v;
    for (AVoice* v : ice_skids) delete v;
    for (AVoice* v : scrapes) delete v;
    delete wind;
}

// [driving] 0x002def58 AVehicle::Reset
void AVehicle::Reset() {
    unknown_188 = 0;
    wobble_dir = 1.0f;
    gas = rpm = speed = rpm_wobble = 0.0f;
    front_skid = back_skid = front_slip = back_slip = 0.0f;
    offroad_level = gravel_level = cobble_level = cobble_dist = rail_dist = 0.0f;
    cobble_toggle = rail_toggle = 0;
    landing_force = 0.0f;
    boost = alive = 0;
    for (int i = 0; i < 3; ++i) scrape_timer[i] = 0, scrape[i] = 0.0f;
}

// [driving] 0x002df738 AVehicle::ActivateWind
void AVehicle::ActivateWind() {
    if (wind == nullptr) wind = new AVehicleWind(this);
}

// [driving] 0x002df9b0 AVehicle::GetAirborneWheels: as rodas no ar no passo anterior.
int32_t AVehicle::GetAirborneWheels() const {
    int32_t n = 0;
    for (int i = 0; i < 4; ++i)
        if (prev_surface[i] == 0) ++n;
    return n;
}

// [driving] 0x002df7c0 AVehicle::CalculateSpeed: |velocidade| (no trânsito suavizada, 14 entre 5 e 14).
void AVehicle::CalculateSpeed() {
    const float l2 = ps2::ffloat(ps2::madd(ps2::madd(ps2::mul(ps2::fbits(vel[0]), ps2::fbits(vel[0])), ps2::fbits(1.0f),
                                                     ps2::fbits(ee::mul(vel[1], vel[1]))),
                                          ps2::fbits(1.0f), ps2::fbits(ee::mul(vel[2], vel[2]))));
    float v = ee::sqrt(l2);
    if (horn_obj != nullptr) {
        if (ee::lt(5.0f, v) && ee::lt(v, 14.0f)) v = 14.0f;
        const float step = ee::mul(ee::cvt_s(sound_count()), 0.1f);
        speed = approach(speed, v, step);
        return;
    }
    speed = v;
}

// [driving] 0x002e1768 AVehicle::Play(APath &)
void AVehicle::Play(APath& path) {
    // (ASceneObj 0x002f05b8: vazio)
    CalculateSpeed();
    const int32_t count = sound_count();
    const float countf = ee::cvt_s(count);
    const float step = ee::mul(countf, 0.04f);
    const int32_t view = path.listener->view;
    rpm_wobble = ee::add(rpm_wobble, ee::mul(ee::mul(countf, 5.0f), wobble_dir));
    if (ee::lt(150.0f, rpm_wobble)) wobble_dir = -1.0f;
    else if (ee::lt(rpm_wobble, -150.0f)) wobble_dir = 1.0f;
    // Os raspões: rocha (+0x17c, +0x144), metal (+0x180, +0x148), areia (+0x184, +0x14c).
    static const int kScrapeVoice[3] = {1, 0, 2};
    for (int k = 0; k < 3; ++k) {
        float target = 0.0f;
        if (0 < scrape_timer[k]) {
            scrape_timer[k] -= count;
            if (scrape_timer[k] < 0) scrape_timer[k] = 0;
            if (3 < scrape_timer[k]) target = ee::lt(speed, 15.0f) ? ee::mul(speed, 0.06666667f) : 1.0f;
        }
        scrape[k] = approach(scrape[k], target, step);
        const float mv = AMix_GetVolume(mix_scrapes);
        AVoice_Play(ee::mul(ee::mul(scrape[k], mv), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f,
                    scrapes[kScrapeVoice[k]], view);
    }
    // As derrapagens.
    front_skid = approach(front_skid, front_slip, step);
    float front = AMix_GetVolume(mix_skids);
    front = ee::mul(ee::mul(front_skid, front), skid_multiple);
    back_skid = approach(back_skid, back_slip, step);
    float back = AMix_GetVolume(mix_skids);
    back = ee::mul(ee::mul(back_skid, back), skid_multiple);
    const int32_t* s = surface;
    auto front_on = [&](int a, int b) { return s[0] == a || s[1] == a || s[0] == b || s[1] == b; };
    auto back_on = [&](int a, int b) { return s[2] == a || s[3] == a || s[2] == b || s[3] == b; };
    auto play_or_mute = [&](bool on, float vol, float voice_pitch, AVoice* v) {
        if (on) AVoice_Play(ee::mul(vol, path.volume), voice_pitch, path.azimuth, 0.0f, 0.0f, v, view);
        else AVoice_Play(0.0f, path.pitch, path.azimuth, 0.0f, 0.0f, v, view);
    };
    const float slow = ee::mul(path.pitch, 0.75f);
    play_or_mute(front_on(1, 4), front, path.pitch, skid[0]);
    play_or_mute(back_on(1, 4), back, path.pitch, skid[1]);
    play_or_mute(front_on(3, 10), front, path.pitch, grass_skids[0]);
    play_or_mute(back_on(3, 10), back, slow, grass_skids[1]);
    play_or_mute(front_on(5, 2) || s[0] == 0xc || s[1] == 0xc, front, path.pitch, offroad_skids[0]);
    play_or_mute(back_on(5, 2) || s[2] == 0xc || s[3] == 0xc, back, slow, offroad_skids[1]);
    play_or_mute(front_on(8, 9), front, path.pitch, ice_skids[0]);
    play_or_mute(back_on(8, 9), back, path.pitch, ice_skids[1]);
    // O ruído do chão de cada superfície.
    float gravel = 0.0f, offroad = 0.0f, cobble = 0.0f;
    bool bumps = false, rails = false;
    for (int i = 0; i < 4; ++i) {
        switch (s[i]) {
            case 7: bumps = true; break;
            case 0xc: gravel = ee::add(gravel, 0.15f), rails = true; break;
            case 5: gravel = ee::add(gravel, 0.15f); break;
            case 2: gravel = ee::add(gravel, 0.25f); break;
            case 4: cobble = ee::add(cobble, 0.25f); break;
            case 3: offroad = ee::add(offroad, 0.15f); break;
            case 10: offroad = ee::add(offroad, 0.25f); break;
            default: break;
        }
    }
    auto road_noise = [&](float& level, float target, AVoice* v) {
        level = approach(level, target, step);
        float vol = level;
        const float mv = AMix_GetVolume(mix_offroad);
        if (ee::lt(speed, 5.0f)) vol = ee::mul(ee::mul(vol, speed), 0.2f);
        AVoice_Play(ee::mul(ee::mul(vol, mv), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f, v, view);
    };
    road_noise(offroad_level, offroad, road[0]);
    road_noise(gravel_level, gravel, road[1]);
    road_noise(cobble_level, cobble, road[2]);
    // Os solavancos dos paralelepípedos (a cada 150 de velocidade x passos) e dos trilhos (250), alternando os dois
    // timbres de cada.
    const float over5 = ee::sub(speed, 5.0f);
    if (bumps && ee::lt(0.0f, over5)) {
        cobble_dist = ee::add(cobble_dist, ee::mul(speed, countf));
        if (ee::lt(150.0f, cobble_dist)) {
            float vol;
            int32_t patch;
            if (cobble_toggle == 0) {
                vol = ee::lt(over5, 5.0f) ? ee::mul(over5, 0.2f) : 1.0f;
                patch = cobble_patch[0];
            } else {
                vol = ee::lt(over5, 20.0f) ? ee::mul(over5, 0.05f) : 1.0f;
                patch = cobble_patch[1];
            }
            oneshot("Wood", collisions, patch, vol, pos, vel);
            cobble_toggle ^= 1;
            cobble_dist = ee::sub(cobble_dist, 150.0f);
        }
    }
    if (rails && ee::lt(0.0f, over5)) {
        rail_dist = ee::add(rail_dist, ee::mul(speed, countf));
        if (ee::lt(250.0f, rail_dist)) {
            float vol;
            int32_t patch;
            if (rail_toggle == 0) {
                vol = ee::lt(over5, 5.0f) ? ee::mul(over5, 0.2f) : 1.0f;
                patch = rail_patch[0];
            } else {
                vol = ee::lt(over5, 20.0f) ? ee::mul(over5, 0.05f) : 1.0f;
                patch = rail_patch[1];
            }
            oneshot("Rail", collisions, patch, vol, pos, vel);
            rail_toggle ^= 1;
            rail_dist = ee::sub(rail_dist, 250.0f);
        }
    }
    // O vento: no ar, com a câmera a 1-2 m do carro ou no veículo do tipo 5.
    if (wind != nullptr) {
        const bool airborne = s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0;
        bool play = airborne;
        if (!airborne) {
            if (ee::le(1.0f, path.distance) && ee::le(path.distance, 2.0f)) play = true;
            else play = vehicle_type == 5;
        }
        if (play) wind->Play(countf, path);
        else wind->Stop(path);
    }
    // (AHorn, AGun, ATurret, AZoomObj, ACarAlarm: ver o cabeçalho)
}

// [driving] 0x002dfa00 AVehicle::PlayLanding(float, COORD3 &, COORD3 &): os pneus batendo (e uma pancada com duas ou
// quatro rodas) pela força da aterrissagem e pela superfície.
void AVehicle::PlayLanding(float, const float point[3], const float*) {
    ABank* b = ABank_Category(1);
    bool asphalt = false, wood = false, snow = false;
    for (int i = 0; i < 4; ++i) {
        const int32_t t = surface[i];
        if (t == 7) wood = true;
        else if (t == 1) asphalt = true;
        else if (uint32_t(t - 3) > 1 && uint32_t(t - 8) < 2) snow = true;
    }
    float force = ee::add(landing_force, 0.1f);
    if (ee::lt(1.0f, force)) force = 1.0f;
    if (GetAirborneWheels() < 1) return;
    ++audio_stats_mut().landings;
    if (ee::le(0.5f, landing_force)) {
        switch (GetAirborneWheels()) {
            case 0:
            case 1: oneshot("Landings", b, patch_of(b, "SFX_TiresHeavy"), force, point, vel); break;
            case 2:
            case 3:
                oneshot("Landings", b, patch_of(b, "SFX_TiresHeavy"), force, point, vel);
                oneshot("Landings", b, patch_of(b, "SFX_Light4"), force, point, vel);
                break;
            case 4:
                oneshot("Landings", b, patch_of(b, "SFX_TiresHeavy"), force, point, vel);
                oneshot("Landings", b, patch_of(b, "SFX_Med3"), force, point, vel);
                break;
            default: break;
        }
        return;
    }
    const char* tires = asphalt ? "SFX_TiresHeavy" : wood ? "SFX_TiresWood" : snow ? "SFX_TiresSnow" : "SFX_TiresHeavy";
    oneshot("Landings", b, patch_of(b, tires), force, point, vel);
}

// [driving] 0x002e0890 AVehicle::PlayCollision(float, COORD3 &, COORD3 &, ASceneObj::SurfaceType): o raspão contínuo
// (os contadores de rocha, metal e areia), os pneus no chão e a pancada pela força nas tabelas, cada timbre com 5 ticks
// do TIMER de descanso.
void AVehicle::PlayCollision(float force, const float point[3], const float hit_vel[3], int32_t surf) {
    int32_t* timer = surf == 0xd ? &scrape_timer[1] : surf == 5 ? &scrape_timer[2] : &scrape_timer[0];
    if (*timer < 10) *timer += 3;
    if (3 < scrape_timer[1] || 3 < scrape_timer[0] || 3 < scrape_timer[2]) return;
    ++audio_stats_mut().collisions;
    if (surf == 1) {
        const int32_t* s = surface;
        const bool tires = s[0] != 0 || (s[1] != 0 && s[2] != 0) || s[3] != 0;
        if (tires) {
            oneshot("Landings", collisions, patch_of(collisions, "SFX_TiresHeavy"), 1.0f, point, vel);
            return;
        }
    }
    if (surf == 0xd && vehicle_type != 4) force = ee::add(force, 0.35f);
    if (!(ee::le(0.05f, force) || g_cooldown[0] < 1)) return;
    if (squeal != 0) {
        ABank* smack = ABank_Category(2);
        const bool odd = (g_gate & 1) != 0;
        ++g_gate;
        const float vol = ee::lt(speed, 5.0f) ? ee::mul(speed, 0.2f) : 1.0f;
        oneshot("Smackables", smack, patch_of(smack, odd ? "SFX_MetalGate3" : "SFX_MetalGate2"), vol, point, hit_vel);
    }
    const HitSound* table;
    int32_t n;
    if (vehicle_type == 4) {
        if (surf == 5) table = kHitsSand, n = 3;
        else table = kHits10, n = 10;
    } else if (vehicle_type != 5) {
        table = kHits12, n = 12;
    } else {
        return;
    }
    int32_t i = 0;
    if (ee::lt(table[0].threshold, force)) {
        i = 1;
        while (i < n - 1 && ee::lt(table[i].threshold, force)) ++i;
    }
    // Os timbres ainda descansando: o seguinte (o original não para no fim da tabela dos descansos; aqui sim).
    while (i < 12 && TIMER_gettick() < g_cooldown[i]) ++i;
    if (!(i < n) || !ee::lt(table[i].threshold, ee::add(force, 0.3f))) return;
    g_cooldown[i] = TIMER_gettick() + 5;
    const float vol = ee::lt(speed, 2.0f) ? ee::mul(speed, 0.5f) : 1.0f;
    const char* hit_mix = vehicle_type == 4 ? "Collisions" : (squeal == 0 ? "Collisions" : "Smackables");
    oneshot(hit_mix, collisions, patch_of(collisions, table[i].name), vol, point, hit_vel);
}

// ---- APlayerVehicle ------------------------------------------------------------------------------------------------

// [driving] 0x002f07c8 APlayerVehicle::APlayerVehicle(char *)
APlayerVehicle::APlayerVehicle(const char* engine_name_) : AVehicle("Player Car", 0xe) {
    engine = AEngine_Get(4500.0f, engine_name_, this);
    std::snprintf(engine_name, sizeof engine_name, "%s", engine_name_);
    // ActivateGun("Machine Gun"): o AGun não está portado (o cabeçalho).
    ActivateWind();
    ABank* generic = ABank_Category(0);
    road_noise = new AVoice(mix_asphalt, generic->snd_bank, patch_of(generic, "SFX_PavedRN"));
    for (int i = 0; i < 4; ++i) road_surface[i] = surface[i];
    if (dafi_strcasecmp(engine_name_, "suv") == 0) vehicle_type = 7;
}

APlayerVehicle::~APlayerVehicle() { delete road_noise; }

// [driving] 0x002f0900 APlayerVehicle::Play(APath &): o AVehicle, o motor e o ruído do asfalto acima de 15, trocado
// pela superfície da roda que mudou.
void APlayerVehicle::Play(APath& path) {
    if (ee::lt(ee::add(rpm, rpm_wobble), 500.0f)) rpm = 500.0f;
    AVehicle::Play(path);
    engine->Play(path);
    if (road_noise != nullptr) {
        float v;
        if (ee::lt(speed, 15.0f)) v = 0.0f;
        else if (ee::lt(speed, 50.0f)) v = ee::mul(ee::sub(speed, 15.0f), 0.028571429f);
        else v = 1.0f;
        const float mv = AMix_GetVolume(mix_asphalt);
        AVoice_Play(ee::mul(ee::mul(v, mv), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f, road_noise,
                    path.listener->view);
    }
    ABank* generic = ABank_Category(0);
    for (int i = 0; i < 4; ++i) {
        if (surface[i] == road_surface[i]) continue;
        road_surface[i] = surface[i];
        delete road_noise;
        road_noise = nullptr;
        const char* patch = nullptr;
        switch (road_surface[i]) {
            case 1: patch = "SFX_PavedRN"; break;
            case 2: patch = "SFX_GravelRN"; break;
            case 4: patch = "SFX_CobbleRN"; break;
            case 8: patch = "SFX_IceRN"; break;
            case 9: patch = "SFX_SnowRN"; break;
            case 10:
            case 0xf: patch = "SFX_OffRN"; break;
            default: break;
        }
        if (patch != nullptr) road_noise = new AVoice(mix_asphalt, generic->snd_bank, patch_of(generic, patch));
    }
}

// ---- o carro do jogador no port ------------------------------------------------------------------------------------

// [driving] 0x00186878 PBondCar::InitAudioObject (a classe 0 sem um dos nomes especiais: APlayerVehicle)
void audio_create_player_car(const AudioCarAttributes& a) {
    if (!audio_ready()) return;
    delete g_player;
    APlayerVehicle* v = new APlayerVehicle(a.engine.c_str());
    v->volume = a.volume;
    v->min_distance = 30.0f;
    v->max_distance2 = 160000.0f;
    v->max_distance = 400.0f;
    if (ee::le(1.0f, a.skid_multiple)) v->skid_multiple = a.skid_multiple;
    g_player = v;
}

// O fim do PBondCar::Simulate (0x001891c4..0x00189390): o objeto de som do carro a cada passo.
void audio_feed_player_car(const AudioCarFeed& f) {
    APlayerVehicle* v = g_player;
    if (v == nullptr) return;
    v->gas = f.gas;
    v->alive = f.alive;
    v->rpm = f.rpm;
    v->front_slip = ee::lt(f.front_slip, 1.0f) ? f.front_slip : 1.0f;
    v->back_slip = ee::lt(f.back_slip, 1.0f) ? f.back_slip : 1.0f;
    for (int i = 0; i < 4; ++i) {
        v->prev_surface[i] = v->surface[i];
        v->surface[i] = f.surface[i];
    }
    v->gun_active = f.gun_active;
    v->gun_firing = f.gun_firing;
    std::memcpy(v->pos, f.pos, 12);
    std::memcpy(v->vel, f.vel, 12);
    std::memcpy(v->axis1, f.orient[0], 12);
    std::memcpy(v->axis2, f.orient[1], 12);
    std::memcpy(v->dir, f.orient[2], 12);
    std::memcpy(v->aim, f.aim, 12);
    v->car_speed = f.car_speed;
    v->steering = f.control_gas;
    v->steering_vertical = f.control_brake;
    v->landing_force = f.landing;
    std::memcpy(v->compression, f.compression, sizeof v->compression);
    v->gas_button = ee::lt(0.0f, f.gas_input) ? 1 : 0;
    v->brake_button = ee::lt(0.0f, f.brake_input) ? 1 : 0;
}

void audio_player_car_collision(float force, const float point[3], const float vel[3], int32_t surface, bool landing) {
    if (g_player == nullptr) return;
    if (landing) g_player->PlayLanding(force, point, vel);
    else g_player->PlayCollision(force, point, vel, surface);
}

// O fim do PBondCar::Simulate (0x00188200, a marca em bond_car.cpp; 0x00188c64..0x00189390): o que vai ao objeto de som (GetAudio) a
// cada passo, para o carro do jogador (a classe 0; o submarino e as classes de IA não estão portados).
void PBondCar_AudioFeed(BondCar& car, float rpm) {
    if (g_player == nullptr) return;
    const PBondCarData& c = car.d;
    if (c.car_class != 0) return;
    RigidBody* rb = car.body();
    AudioCarFeed f;
    // As derrapagens: a média do GetCarWheelSlip das rodas da frente e das de trás (f23, f22).
    f.front_slip = ee::mul(ee::add(c.wheel_slip[0], c.wheel_slip[1]), 0.5f);
    f.back_slip = ee::mul(ee::add(c.wheel_slip[2], c.wheel_slip[3]), 0.5f);
    f.gas = c.control.gas;  // GetCarControl: o +0x10 da cópia
    f.rpm = rpm;
    f.alive = ee::le(car.hit_points(), 0.0f) ? 0 : 1;
    // A superfície de cada roda no chão (IsWheelOnGround: a compressão não zero).
    for (int i = 0; i < 4; ++i) f.surface[i] = ee::eq(c.suspension[i], 0.0f) ? 0 : c.wheels[i].surface[0];
    f.gun_active = 1;
    f.gun_firing = 0;
    std::memcpy(f.pos, rb->pos, 12);
    std::memcpy(f.vel, rb->vel, 12);
    for (int r = 0; r < 3; ++r) std::memcpy(f.orient[r], rb->info->orient[r], 12);
    f.car_speed = c.car_speed;
    f.control_gas = c.control.steering;
    f.control_brake = c.control.steering_vertical;
    f.landing = c.landing_impact;
    std::memcpy(f.compression, c.suspension, sizeof f.compression);
    f.gas_input = c.gas_input;
    f.brake_input = c.brake_input;
    audio_feed_player_car(f);
}

// A parte do som do ECollision::~ECollision (0x00149ed8, a marca em game/feedback.cpp): cada lado do choque cujo dono tem objeto de som (no
// port, o carro do jogador) recebe o PlayLanding ou o PlayCollision com a superfície do outro lado.
void ECollision_Audio(const CollisionInfo& c, int32_t player_slot) {
    if (g_player == nullptr) return;
    const float force = ee::abs(c.strength);
    if (c.kind < 6 && c.slot == player_slot) {
        if (c.flags == 0xe) {
            g_player->PlayLanding(force, c.point, c.velocity);
        } else if (c.kind == 4) {
            if (c.type == 7) g_player->PlayLanding(force, c.point, c.velocity);
        } else if (c.kind < 4 && c.type != 4) {
            int32_t surf;
            switch (int16_t(c.tag - 1)) {
                case 0: surf = 1; break;
                case 1: surf = 2; break;
                case 2: surf = 3; break;
                case 3: surf = 4; break;
                case 4:
                case 0x4f: surf = 5; break;
                case 9: surf = 10; break;
                case 0xb: surf = 0xc; break;
                case 0xc:
                case 0xf: surf = 0xd; break;
                case 0x2f: surf = 9; break;
                case 0x3f: surf = 8; break;
                case 0x5f: surf = 0xf; break;
                default: surf = 0; break;
            }
            g_player->PlayCollision(force, c.point, c.velocity, surf);
        }
    }
    if (c.type < 6 && int8_t(c.unknown_4d) == player_slot) {
        if (c.tag == 0xe) {
            g_player->PlayLanding(force, c.point, c.velocity);
        } else if (c.type == 4) {
            if (c.kind == 7) g_player->PlayLanding(force, c.point, c.velocity);
        } else if (c.type < 4 && c.kind != 4) {
            const int32_t surf = c.kind == 7 ? 1 : (c.flags == 0x10 || c.flags == 0xd) ? 0xd : 0xf;
            g_player->PlayCollision(force, c.point, c.velocity, surf);
        }
    }
}

void audio_vehicle_internals(AudioInternals& out) {
    out.player = g_player;
    out.cooldown = g_cooldown;
    out.gate = &g_gate;
}

// O que o port precisa limpar junto com o resto (audio_shutdown apaga os sons da lista).
void audio_vehicle_forget() {
    g_player = nullptr;
    g_engines.clear();
    g_gate = 0;
    for (int32_t& c : g_cooldown) c = 0;
}

}  // namespace drv
