// O som dos outros carros (driving/sound/audio.h): ATrafficVehicle (o trânsito e os inimigos: o motor em duas vozes,
// LdEn e LdEx, afinadas pelas rotações suavizadas e misturadas pelo lado que o carro mostra; o ruído da rua pela
// velocidade), a buzina AHorn (as buzinadas em rajadas quando a IA vê o caminho fechado) e o ABasic (a sirene). Portado
// do MIPS do DRIVING.ELF (SupportPS2.lib UDevCons.obj e BondAudioPS2.lib AMix.obj no SYM); docs/driving.md seção 29.
//
// Como o PS2 faz: o PBondCar::InitAudioObject (driving/game/bond_car.h) cria, para as classes 1 e 3, um
// ATrafficVehicle na mixagem "Traffic Cars" com a buzina do SFX_HORN (alcance 10..200 m); para a 2 e a 4 um na
// "Opponents" (ou "Friendly": a 4 sem os motores "750" e "Nsub") com a afinação sorteada (0..0,2) e a sirene com o
// HAS_SIREN (alcance 30..400 m, ou 100..600 m com IS_HELI ou IS_FLYING). O fim do PBondCar::Simulate alimenta o objeto
// como o do jogador; a IA liga a buzina (o +0x154, AIGroundVehicle::CheckTrafficCollision) e a sirene (o +0x244,
// SetAttackMode 13) e o zera ao tirar o carro da rua (AVehicle::Reset, AIGroundVehicle::UnSpawn).
//
// Divergências (port, contadas em traffic_audio_stats().guards):
//  - o AHelicopter (a classe 2 com IS_HELI) e o PlaySnowmobileSpecifics (o tipo 5 dos trenós) não estão portados:
//    o helicóptero fica sem som, o trenó sem a parte dele; nenhum dos dois existe em Paris;
//  - o IsTracked (o tipo 5) no CalculateRpm usa o logf do port (nf::fm::log) no lugar do da libm do PS2;
//  - os objetos ficam num registro: o audio_shutdown apaga todos os sons, e o carro que ainda o aponte o vê como nulo.

#include <cstdio>
#include <cstring>
#include <set>

#include "driving/camera/dafi.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "platform/crt_compat.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

TrafficAudioStats g_stats;

std::set<ATrafficVehicle*>& live() {
    static std::set<ATrafficVehicle*> s;
    return s;
}

ATrafficVehicle* from(uint32_t h) {
    if (h == 0) return nullptr;
    ATrafficVehicle* v = nf::p32_ptr<ATrafficVehicle>(h);
    return live().count(v) != 0 ? v : nullptr;
}

int32_t generic_patch(const char* name) { return ABank_Category(0)->index.Lookup(name); }

}  // namespace

TrafficAudioStats& traffic_audio_stats() { return g_stats; }

// ---- AHorn ---------------------------------------------------------------------------------------------------------

// [driving] 0x002de9b8 AHorn::AHorn(char *, float, float): o timbre do banco Generic na mixagem "Horns".
AHorn::AHorn(float vol, float p, const char* name)
    : volume(vol), pitch(p), mix(AMix_Add("Horns")), voice(mix, ABank_Category(0)->snd_bank, generic_patch(name)) {}

// [driving] 0x002dea48 AHorn::Play(bool, APath &): com o pedido, uma rajada de 3 a 5 buzinadas de 5 a 64 quadros com
// 5 a 34 de intervalo, e depois 120 a 359 quadros de descanso (o random da libc, o resto sem sinal).
void AHorn::Play(int32_t request, APath& path) {
    AHorn_Step(honks, ticks, wait, request);
    const int32_t view = path.listener->view;
    const float vol = ticks == 0 ? 0.0f : ee::mul(AMix_GetVolume(mix), volume);
    AVoice_Play(ee::mul(vol, path.volume), ee::mul(pitch, path.pitch), path.azimuth, 0.0f, 0.0f, &voice, view);
}

void AHorn_Step(int32_t& honks, int32_t& ticks, int32_t& wait, int32_t request) {
    if (ticks == 0) {
        if (request != 0) {
            if (honks == 0) honks = int32_t(random_word() % 3u) + 3;  // (divu: o resto sem sinal)
            else honks -= 1;
            if (wait == 0) ticks = int32_t(random_word() % 0x3cu) + 5;
            else wait -= 1;
        }
    } else {
        ticks -= 1;
        if (ticks == 0) {
            if (honks == 0) wait = int32_t(random_word() % 0xf0u) + 0x78;
            else wait = int32_t(random_word() % 0x1eu) + 5;
        }
    }
}

// [driving] 0x002df538 AVehicle::ChooseHorn(char *, float, float): a buzina "SFX_<nome>" (a anterior apagada).
void AVehicle_ChooseHorn(float vol, float pitch, AVehicle& v, const char* name) {
    delete v.horn_obj;
    char full[64];
    std::snprintf(full, sizeof full, "SFX_%s", name ? name : "");  // (o "." do DAT_003c26a8: zero)
    v.horn_obj = new AHorn(vol, pitch, full);
}

// ---- ABasic --------------------------------------------------------------------------------------------------------

// [driving] 0x002fc5d0 ABasic::ABasic(char *, char *)
ABasic::ABasic(const char* patch, const char* mix_name) : ABaseSound(mix_name, 4) {
    std::snprintf(patch_name, sizeof patch_name, "%s", patch);
}

// [driving] 0x002fc848 ABasic::~ABasic
ABasic::~ABasic() { delete voice; }

// [driving] 0x002fc700 ABasic::Play(APath &): a voz do banco Generic na primeira vez.
void ABasic::Play(APath& path) {
    if (voice == nullptr) voice = new AVoice(mix, ABank_Category(0)->snd_bank, generic_patch(patch_name));
    const int32_t view = path.listener->view;
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), ee::mul(pitch, path.pitch), path.azimuth, 0.0f, 0.0f, voice,
                view);
}

// [driving] 0x002fc7c8 ABasic::Stop(APath &)
void ABasic::Stop() {
    delete voice;
    voice = nullptr;
}

// ---- ATrafficVehicle -----------------------------------------------------------------------------------------------

// [driving] 0x002e5340 ATrafficVehicle::ATrafficVehicle(char *, char *)
ATrafficVehicle::ATrafficVehicle(const char* engine, const char* mix_name) : AVehicle(mix_name, 0xe) {
    std::snprintf(engine_name, sizeof engine_name, "%s", engine);
    ABank* g = ABank_Category(0);
    char name[32];
    std::snprintf(name, sizeof name, "SFX_%sLdEn", engine);
    load_engine = new AVoice(mix, g->snd_bank, g->index.Lookup(name));  // UMemory::FastAlloc(100, "AVoice")
    std::snprintf(name, sizeof name, "SFX_%sLdEx", engine);
    load_exhaust = new AVoice(mix, g->snd_bank, g->index.Lookup(name));
    if (dafi_strcasecmp(engine, "tank") == 0)
        road_voice = new AVoice(AMix_Get("Tank Treads"), g->snd_bank, g->index.Lookup("SFX_tanktreads"));
    else
        road_voice = new AVoice(mix, g->snd_bank, g->index.Lookup("SFX_TrafficRN"));
    if (dafi_strcasecmp(engine, "supsled") == 0 || dafi_strcasecmp(engine, "oppsled") == 0) {
        vehicle_type = 5;  // (+0x254..+0x260: os números do trenó, só do PlaySnowmobileSpecifics)
        ++g_stats.guards;
    }
    live().insert(this);
    ++g_stats.created;
}

// [driving] 0x002e6640 ATrafficVehicle::~ATrafficVehicle
ATrafficVehicle::~ATrafficVehicle() {
    delete road_voice;
    delete load_exhaust;
    delete load_engine;
    if (siren) siren->destroy();
    delete horn_obj;  // (o ~AVehicle do PS2 apaga a buzina)
    horn_obj = nullptr;
    live().erase(this);
    ++g_stats.deleted;
}

// [driving] 0x002e58f8 ATrafficVehicle::ActivateSiren(char *)
void ATrafficVehicle::ActivateSiren(const char* patch) {
    if (siren == nullptr) siren = new ABasic(patch, "Opponents");  // ABaseSound::operator new(0xe0, "ABasic")
    else std::printf("WARNING: Siren already exists in ATrafficeVehicle\n");
}

// [driving] 0x002df8a0 AVehicle::CalculateRpm (com o IsTracked 0x002e6a20 do ATrafficVehicle: o tipo 5)
float ATrafficVehicle::CalculateRpm() const {
    if (vehicle_type == 5 && ee::lt(1.0f, speed)) ++g_stats.guards;  // (o logf da libm do PS2)
    return AVehicle_CalculateRpmValues(vehicle_type == 5, horn_obj != nullptr, rpm, rpm_wobble, speed);
}

float AVehicle_CalculateRpmValues(bool tracked, bool has_horn, float rpm, float wobble, float speed) {
    if (!tracked) {
        if (!has_horn) return ee::add(rpm, wobble);
        const float s = speed;
        float g = 15.0f;
        if (ee::le(15.0f, s)) {
            int32_t i = 2;
            do {
                if (9 < i) break;
                g = ee::mul(ee::cvt_s(i), 15.0f);
                ++i;
            } while (ee::le(g, s));
        }
        return ee::add(ee::div(ee::mul(s, 5000.0f), g), wobble);
    }
    if (ee::le(speed, 1.0f)) return 0.0f;
    return ee::mul(nf::fm::log(speed), 1000.0f);
}

// [driving] 0x002e5598 ATrafficVehicle::Play(APath &)
void ATrafficVehicle::Play(APath& path) {
    const float countf = ee::cvt_s(sound_count());
    AVehicle::Play(path);
    TrafficPlayFields f;
    f.surface2 = surface[2];
    f.surface3 = surface[3];
    f.alive = alive;
    f.gas = gas;
    f.volume = volume;
    f.speed = speed;
    f.smooth_rpm = smooth_rpm;
    f.load = load;
    f.pitch_offset = pitch_offset;
    TrafficPlayVoices out;
    ATrafficVehicle_PlayOwn(f, countf, CalculateRpm(), AMix_GetVolume(mix), path, out);
    smooth_rpm = f.smooth_rpm;
    load = f.load;
    const int32_t view = path.listener->view;
    AVoice* voices[3] = {load_engine, load_exhaust, road_voice};
    for (int i = 0; i < 3; ++i)
        AVoice_Play(out.vol[i], out.pitch[i], path.azimuth, 0.0f, 0.0f, voices[i], view);
    if (vehicle_type == 5) ++g_stats.guards;  // PlaySnowmobileSpecifics (0x002e5968): não portado
    if (siren != nullptr) {
        if (siren_on == 0) siren->Stop();  // (o +0x244: a IA o liga no modo de ataque 13)
        else siren->Play(path);
    }
    ++g_stats.plays;
}

// A conta do ATrafficVehicle::Play depois do AVehicle::Play: as rotações suavizadas (200 por passo; caindo, metade e a
// carga em 1/8), a carga (16/127 por passo), o pitch (rpm + 4000) / 7200 mais a afinação, as duas vozes do motor
// (0,66 x (1 -/+ facing / 2)) e o ruído da rua ((velocidade - 2) / 15 / 2, até 0,5). `mix_volume` é o AMix::GetVolume
// (o mesmo nas duas leituras).
void ATrafficVehicle_PlayOwn(TrafficPlayFields& f, float countf, float rpm, float mix_volume, const APath& path,
                             TrafficPlayVoices& out) {
    float r = rpm;
    float target = 1.0f;
    if (f.surface2 == 0 || f.surface3 == 0) {
        r = 0.0f;
        if (ee::lt(0.0f, f.gas)) r = 10000.0f;
    }
    const float cur = f.smooth_rpm, step = ee::mul(countf, 200.0f);
    const float d = ee::sub(r, cur);
    if (ee::le(d, ee::neg(step))) {
        target = 0.125f;
        f.smooth_rpm = ee::sub(cur, ee::mul(step, 0.5f));
    } else if (ee::le(step, d)) {
        f.smooth_rpm = ee::add(cur, step);
    } else {
        f.smooth_rpm = r;
        target = 1.0f;
    }
    const float lstep = ee::mul(countf, ps2::ffloat(0x3e010204u));  // 0,12598425 (16/127)
    const float dl = ee::sub(target, f.load);
    if (ee::le(dl, ee::neg(lstep))) f.load = ee::sub(f.load, lstep);
    else if (ee::le(lstep, dl)) f.load = ee::add(f.load, lstep);
    else f.load = target;
    const float pitch_now = ee::add(ee::div(ee::add(f.smooth_rpm, 4000.0f), 7200.0f), f.pitch_offset);
    float vol = 0.0f;
    if (f.alive != 0) vol = ee::mul(f.load, ee::mul(f.volume, mix_volume));
    const float k066 = ps2::ffloat(0x3f28f5c3u);  // 0,66
    out.vol[0] = ee::mul(ee::mul(ee::mul(ee::sub(1.0f, ee::mul(path.facing, 0.5f)), k066), vol), path.volume);
    out.vol[1] = ee::mul(ee::mul(ee::mul(ee::add(ee::mul(path.facing, 0.5f), 1.0f), k066), vol), path.volume);
    out.pitch[0] = out.pitch[1] = ee::mul(pitch_now, path.pitch);
    float road = 0.0f;
    if (ee::lt(2.0f, f.speed))
        road = ee::mul(ee::mul(ee::mul(ee::mul(f.volume, mix_volume), ee::sub(f.speed, 2.0f)), ps2::ffloat(0x3d888889u)),
                       0.5f);
    if (ee::lt(0.5f, road)) road = 0.5f;
    out.vol[2] = ee::mul(road, path.volume);
    out.pitch[2] = path.pitch;
}

// ---- a ponte com o jogo ---------------------------------------------------------------------------------------------

// O que o PBondCar::InitAudioObject (0x00186878) faz com as classes 1 a 4.
AudioCarObjectPlan audio_car_object_plan(const AudioCarObjectParams& p) {
    AudioCarObjectPlan plan;
    if (p.car_class == 2) {
        plan.mix = "Opponents";
        plan.heli = p.is_heli;
        plan.siren = !p.is_heli && p.has_siren;
        if (!p.is_heli && !p.is_flying) plan.min = 30.0f, plan.max = 400.0f, plan.max2 = 160000.0f;
        else plan.min = 100.0f, plan.max = 600.0f, plan.max2 = 360000.0f;
    } else if (p.car_class == 4) {
        const bool opp = dafi_strcasecmp(p.engine.c_str(), "750") == 0 || dafi_strcasecmp(p.engine.c_str(), "Nsub") == 0;
        plan.mix = opp ? "Opponents" : "Friendly";
        plan.siren = p.has_siren;
        plan.min = 30.0f, plan.max = 400.0f, plan.max2 = 160000.0f;
    } else {
        plan.mix = "Traffic Cars";
        plan.horn = true;
        plan.min = 10.0f, plan.max = 200.0f, plan.max2 = 40000.0f;
    }
    return plan;
}

// A parte do PBondCar::InitAudioObject (0x00186878) das classes 1 a 4: o objeto e os números (a classe 0, o
// APlayerVehicle, é o audio_create_player_car).
uint32_t audio_create_car_object(const AudioCarObjectParams& p) {
    if (!audio_ready()) return 0;
    const AudioCarObjectPlan plan = audio_car_object_plan(p);
    if (plan.heli) {
        ++g_stats.guards;  // o AHelicopter (0x350 bytes): não portado
        return 0;
    }
    ATrafficVehicle* v = new ATrafficVehicle(p.engine.c_str(), plan.mix);
    v->volume = p.volume;
    if (p.car_class == 2 || p.car_class == 4) v->pitch_offset = p.pitch_offset;
    if (plan.siren) v->ActivateSiren("SFX_copsiren");
    if (plan.horn) AVehicle_ChooseHorn(1.0f, p.horn_pitch, *v, p.horn.c_str());
    v->min_distance = plan.min;
    v->max_distance2 = plan.max2;
    v->max_distance = plan.max;
    if (ee::le(1.0f, p.skid_multiple)) v->skid_multiple = p.skid_multiple;
    return nf::p32_u(v);
}

void audio_delete_car_object(uint32_t h) {
    if (ATrafficVehicle* v = from(h)) v->destroy();
}

void audio_car_horn(uint32_t h) {
    if (ATrafficVehicle* v = from(h)) v->horn = 1;  // o +0x154
}

void audio_car_siren(uint32_t h) {
    if (ATrafficVehicle* v = from(h)) v->siren_on = 1;  // o +0x244
}

void audio_car_reset(uint32_t h) {
    if (ATrafficVehicle* v = from(h)) v->Reset();
}

AVehicle* audio_car_object(uint32_t h) { return from(h); }

}  // namespace drv
