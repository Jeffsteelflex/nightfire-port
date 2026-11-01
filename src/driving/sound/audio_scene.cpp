// Os sons das armas, da mira e dos objetos do mundo de carro (driving/sound/audio_internal.h, seção 31 do
// docs/driving.md): ASceneObj, AGun (a metralhadora), ATurret (os motores de uma torre), AZoomObj (a respiração de Bond na
// luneta, com as mixagens abaixadas), ACarAlarm, ATargeting (os bipes da trava), AMenuSound (os sons sem posição do HUD,
// das armas e da missão), APlayerHeli (o "helicopter" do jogador em Paris), ASmackable (os objetos derrubados) e o
// AVehicle::Activate*. Portado do MIPS do DRIVING.ELF (SupportPS2.lib UDevCons.obj e BondAudioPS2.lib AMix.obj no SYM) e
// conferido contra o original no interpretador (a parte 27 do nf_driving_check, `--so som_hud`). O nightfire-cxbx
// (NightfireResearch, src/driving/game/WeaponSound.cpp, VehicleSoundKinds.cpp) serviu de leitura; o PS2 é a verdade.
//
// Divergências (port), cada uma também na função:
//  - o ACarAlarm::ACarAlarm constrói a voz com a mixagem do +0x64 antes de escrevê-lo (lixo do __builtin_new no PS2):
//    o port usa a "Horns" (o AVehicle::StartAlarm não tem quem o chame no DRIVING.ELF);
//  - as alocações do UMemory (FastAlloc) e o __builtin_new são new/delete; os basic_string do AGun são std::string;
//  - o rrandom do AGun é comparado em double (fptodp + dpcmp), como no PS2.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>

#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_stream.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

int32_t g_next_hard = 0;  // 0x00355ea0
SceneAudioStats g_stats;

std::set<ASmackable*>& live_smackables() {
    static std::set<ASmackable*> s;
    return s;
}

int32_t lookup(ABank* b, const char* name) { return b->index.Lookup(name); }

// O UMemory::FastAlloc(100, "AVoice") + AVoice::AVoice(mixagem, banco, timbre).
AVoice* new_voice(AMix* mix, ABank* b, const char* name) { return new AVoice(mix, b->snd_bank, lookup(b, name)); }
AVoice* new_voice(AMix* mix, int32_t id) { return new AVoice(mix, id >> 7, id & 0x7f); }
// O AVoice::View::~View das três vistas e o FastFree.
void free_voice(AVoice*& v) {
    delete v;
    v = nullptr;
}
// Cada vista toca uma vez só (o laço de -1 em +0x4c, de trás para a frente).
void once(AVoice* v) {
    for (int i = 2; i >= 0; --i) v->views[i].loop = 0;
}

// |v| pela VU0 (vmul.xyz; vmulax.x, vmadday.x, vmaddz.x com 1,0; vsqrt; vaddq.x com o vf0).
float vu_length(const float v[3]) {
    uint32_t acc = ps2::mul(ps2::fbits(v[0]), ps2::fbits(v[0]));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(v[1], v[1])));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(v[2], v[2])));
    return ps2::ffloat(ps2::add(0, ps2::sqrt(acc)));
}

}  // namespace

SceneAudioStats& scene_audio_stats() { return g_stats; }
int32_t& asmackable_next_hard() { return g_next_hard; }

// ---- o registro da conferência -------------------------------------------------------------------------------------

AudioTrace*& audio_trace() {
    static AudioTrace* t = nullptr;
    return t;
}

int AudioTrace::id(const void* p) {
    auto it = ids.find(p);
    if (it != ids.end()) return it->second;
    ids[p] = next;
    return next++;
}

void AudioTrace::line(const char* fmt, ...) {
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    log.emplace_back(b);
}

std::string audio_mix_name(const AMix* m) { return m ? std::string(m->name) : std::string("?"); }

// O fim de uma voz de uma vez só nas vistas em uso (a vista 2 acabou; com DAT_00355cd0 sem o 4, as vistas 0 e 1 da
// máscara também).
bool AVoice_Done(const AVoice& v, uint32_t mask) {
    if (v.views[2].over == 0) return false;
    if ((mask & 4u) != 0) return true;
    for (uint32_t i = 0; i < 2; ++i)
        if ((mask & (1u << i)) != 0 && v.views[i].over == 0) return false;
    return true;
}

// ---- AMenuSound ----------------------------------------------------------------------------------------------------

// [driving] 0x002f4780 AMenuSoundPriv::AMenuSoundPriv(int, int, char *, AView): o som sem posição (o máximo 0) com as
// vistas de uma vez só.
AMenuSoundPriv::AMenuSoundPriv(int32_t bank, int32_t patch, const char* mix_name, int32_t view)
    : ABaseSound(mix_name, view), voice(mix, bank, patch) {
    max_distance = 0.0f;
    volume = 1.0f;
    max_distance2 = 0.0f;
    for (int i = 2; i >= 0; --i) voice.views[i].loop = 0;
    ++g_stats.menu_sounds;
}

// [driving] 0x002f4918 AMenuSoundPriv::Play(APath &): toca com o volume da mixagem e se apaga quando acaba.
void AMenuSoundPriv::Play(APath& path) {
    const int32_t view = path.listener->view;
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f, &voice, view);
    if (AVoice_Done(voice)) destroy();
}

// [driving] 0x002f4a20 AMenuSound::Trigger(int, int, char *, AView): no jogo de um jogador a vista 2.
void AMenuSound_Trigger(int32_t bank, int32_t patch, const char* mix, int32_t view) {
    if (AudioTrace* t = audio_trace()) {
        // (com o registro, só o pedido: a conferência confere o AMenuSoundPriv à parte)
        t->line("menu %d %d %s %d", int(bank), int(patch), mix, int(view));
        return;
    }
    if (!audio_ready()) return;
    AMenuSound_Make(bank, patch, mix, view);
}

AMenuSoundPriv* AMenuSound_Make(int32_t bank, int32_t patch, const char* mix, int32_t view) {
    // ABaseSound::operator new(0x120, "AMenuSoundPriv"); (DAT_00355cd0 & 4) ? 2 : a vista pedida (no jogo de um jogador
    // a máscara tem o 4)
    (void)view;
    return new AMenuSoundPriv(bank, patch, mix, 2);
}

// [driving] 0x002f4aa0 AMenuSound::Trigger(char *banco, char *timbre, char *, AView)
void AMenuSound_TriggerBank(const char* bank, const char* patch, const char* mix, int32_t view) {
    ABank* b = ABank_Get(bank);
    const int32_t p = lookup(b, patch);
    if (-1 < p) AMenuSound_Trigger(b->snd_bank, p, mix, view);
}

// [driving] 0x002f4b10 AMenuSound::Trigger(int categoria, char *timbre, char *, AView)
void AMenuSound_TriggerCategory(int32_t category, const char* patch, const char* mix, int32_t view) {
    ABank* b = ABank_Category(category);
    if (-1 < lookup(b, patch)) AMenuSound_Trigger(b->snd_bank, lookup(b, patch), mix, view);
}

// ---- AGun ----------------------------------------------------------------------------------------------------------

// [driving] 0x002f6e60 AGun::AGun(ASceneObj &, char *)
AGun::AGun(ASceneObj* o, const char* mix_name_) : owner(o), bank(ABank_Category(4)), mix(AMix_Get(mix_name_)) {
    fire_patch = "SFX_HeliMachGun";
    shell_patch = "SFX_ShellCasing";
    mix_name = mix_name_;
    voice = new AVoice(mix, bank->snd_bank, lookup(bank, fire_patch.c_str()));
}

// [driving] 0x002f7078 AGun::~AGun
AGun::~AGun() { free_voice(voice); }

// [driving] 0x002f7188 AGun::SetFirePatch(basic_string)
void AGun::SetFirePatch(const std::string& patch) {
    fire_patch = patch;
    free_voice(voice);
    voice = new AVoice(mix, bank->snd_bank, lookup(bank, fire_patch.c_str()));
}

// [driving] 0x002f7300 AGun::Play(bool, APath &, float): atirando, o tempo anda pelos passos e a cada intervalo um tiro
// (um AOneShotSound se o dono tem lagartas ou não sustenta o laço; os cartuchos com o rrandom); o laço toca por 10
// passos depois do último pedido.
void AGun::Play(float frames, int32_t firing, APath& path) {
    const int32_t view = path.listener->view;
    if (firing == 1) {
        const float t = ee::add(fire_timer, frames);
        fire_timer = t;
        if (ee::le(fire_interval, t)) {
            if (owner->IsTracked() || owner->unknown_c4 == 0) {
                // O AOneShotSound inline (0x120): a mixagem do nome guardado, o timbre do tiro, na posição e com a
                // velocidade do dono; 30..500 m.
                AOneShotSound* s = new AOneShotSound(mix_name.c_str(), bank->snd_bank, lookup(bank, fire_patch.c_str()),
                                                     1.0f);
                std::memcpy(s->pos, owner->pos, 12);
                std::memcpy(s->vel, owner->vel, 12);
                s->max_distance2 = 250000.0f;
                s->min_distance = 30.0f;
                s->max_distance = 500.0f;
                ++g_stats.gun_shots;
            }
            if (shells != 0 && double(rrandom()) < 0.1) {
                AOneShotSound* s = new AOneShotSound("Shell Casings", bank->snd_bank,
                                                     lookup(bank, shell_patch.c_str()), 1.0f);
                std::memcpy(s->pos, owner->pos, 12);
                std::memcpy(s->vel, owner->vel, 12);
            }
            fire_timer = ee::sub(fire_timer, fire_interval);
        }
        sustain = 10.0f;
    } else {
        fire_timer = fire_interval;
    }
    if (ee::lt(0.0f, sustain) && owner->unknown_c4 != 0) {
        sustain = ee::sub(sustain, frames);
        const float mv = AMix_GetVolume(mix);
        AVoice_Play(ee::mul(mv, path.volume), path.pitch, path.azimuth, 0.0f, 1.0f, voice, view);
        return;
    }
    AVoice_Play(0.0f, path.pitch, path.azimuth, 0.0f, 0.0f, voice, view);
}

// ---- ATurret -------------------------------------------------------------------------------------------------------

// [driving] 0x002e4930 ATurret::ATurret
ATurret::ATurret() : mix(AMix_Get("Weapons Init")) {}

// [driving] 0x002e5148 ATurret::~ATurret
ATurret::~ATurret() {
    free_voice(sideways_voice);
    free_voice(updown_voice);
    free_voice(servo_voice);
}

namespace {

// Um motor: ligando, a voz em laço; desligando, a parada de uma vez só; toca e solta a voz que acabou.
void turret_motor(ATurret& t, int32_t on, int32_t& state, AVoice*& v, ABank* b, const char* run, const char* stop,
                  float scale, APath& path) {
    if (on == 0) {
        if (state != 0) {
            free_voice(v);
            v = new_voice(t.mix, b, stop);
            once(v);
            state = 0;
        }
    } else if (state == 0) {
        free_voice(v);
        v = new_voice(t.mix, b, run);
        state = 1;
    }
    if (v == nullptr) return;
    const float mv = AMix_GetVolume(t.mix);
    AVoice_Play(scale == 1.0f ? ee::mul(mv, path.volume) : ee::mul(ee::mul(mv, scale), path.volume), path.pitch,
                path.azimuth, 0.0f, 0.0f, v, path.listener->view);
    if (AVoice_Done(*v)) free_voice(v);
}

}  // namespace

// [driving] 0x002e4980 ATurret::Play(bool de lado, bool para cima, bool servo, APath &)
void ATurret::Play(int32_t sideways_on, int32_t updown_on, int32_t servo_on, APath& path) {
    ABank* generic = ABank_Category(0);
    turret_motor(*this, sideways_on, sideways, sideways_voice, generic, "SFX_TurretLR", "SFX_TurretLRStop", 0.5f, path);
    turret_motor(*this, updown_on, updown, updown_voice, generic, "SFX_TurretUD", "SFX_TurretUDStop", 0.5f, path);
    ABank* weapons = ABank_Category(4);
    if (servo_on != 0 && servo == 0) {
        // O "Kachung" de quando o servo liga (AMenuSound::Trigger(banco, timbre, "Weapons Init", 2)).
        AMenuSound_Trigger(weapons->snd_bank, lookup(weapons, "SFX_Kachung"), "Weapons Init", 2);
    }
    turret_motor(*this, servo_on, servo, servo_voice, weapons, "SFX_servo", "SFX_servodown", 1.0f, path);
}

// ---- AZoomObj ------------------------------------------------------------------------------------------------------

// [driving] 0x002dc500 AZoomObj::AZoomObj(ASceneObj &) (o FileHandlerCallback do SYM)
AZoomObj::AZoomObj(ASceneObj* o) : mix(AMix_Get("Weapons Init")), owner(o) {
    ABank* misc = ABank_Category(6);
    voice = new AVoice(mix, misc->snd_bank, lookup(misc, "SFX_breath"));
}

// [driving] 0x002dcbb0 AZoomObj::~AZoomObj
AZoomObj::~AZoomObj() { free_voice(voice); }

// [driving] 0x002dc5a0 AZoomObj::InTheZone(bool): entrando, as mixagens vão (em 10 quadros) aos volumes da luneta (o
// vento, as armas e a Bond ficam; o resto cai); saindo, o AMix::Reset.
void AZoomObj::InTheZone(int32_t on) {
    if (on == 0) {
        if (in_zone != 0) {
            // printf("NOT IN THE ZONE!!!")
            AMix_Reset(10);
            ++g_stats.zone_out;
        }
        in_zone = 0;
        return;
    }
    if (in_zone == 0) {
        // printf("In THE ZONE!!!")
        static const struct {
            const char* name;
            uint32_t to;
        } kZone[] = {
            {"Ambience", 0},          {"Asphalt", 0},         {"Blowout", 0},          {"Booster", 0},
            {"Bullet Impact", 0},     {"Bullet Ricochet", 0}, {"Characters", 0},       {"Collisions", 0x3e19999a},
            {"Explosions", 0x3e4ccccd}, {"ExplosionBass", 0x3dcccccd}, {"Friendly", 0x3dcccccd}, {"Gadgets", 0},
            {"Gatling Gun", 0},       {"Giotto", 0x3e19999a}, {"Helicopter", 0},       {"Henchmen Fire", 0x3e19999a},
            {"Horns", 0x3dcccccd},    {"Landings", 0x3e800000}, {"Machine Gun", 0},    {"Object Sounds", 0},
            {"OffRoad", 0},           {"Opponents", 0x3dcccccd}, {"Player Car", 0x3e19999a},
            {"Player Grunts", 0x3e4ccccd}, {"Player Wind", 0x3f800000}, {"POV Weapons", 0x3f800000},
            {"Power Ups", 0},         {"Rail", 0},            {"Scrapes", 0},          {"Shell Casings", 0},
            {"Skids", 0},             {"Smackables", 0},      {"Tank Treads", 0},      {"Tracking System", 0},
            {"Traffic Cars", 0},      {"Weapons Fire", 0},    {"Weapons Init", 0x3f800000}, {"Weapons Reload", 0},
            {"Weapons Transit", 0},   {"Wood", 0},            {"World Sounds", 0x3e19999a}, {"Music", 0x3d4ccccd},
        };
        for (const auto& z : kZone) AMix_SetTransition(ps2::ffloat(z.to), AMix_Get(z.name), 10);
        ++g_stats.zone_in;
    }
    in_zone = on;
}

// [driving] 0x002dcb10 AZoomObj::Play(APath &): a respiração enquanto na zona.
void AZoomObj::Play(APath& path) {
    const int32_t view = path.listener->view;
    if (in_zone == 0) {
        AVoice_Play(0.0f, path.pitch, path.azimuth, 0.0f, 0.0f, voice, view);
        return;
    }
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(mv, path.volume), path.pitch, path.azimuth, 0.0f, 0.0f, voice, view);
}

// ---- ACarAlarm -----------------------------------------------------------------------------------------------------

// [driving] 0x002debc8 ACarAlarm::ACarAlarm. Divergência: o PS2 passa ao AVoice::AVoice o +0x64 ainda não escrito (o
// lixo do __builtin_new) e só depois grava a "Horns" nele; o port constrói a voz sem mixagem e depois lhe dá a "Horns".
ACarAlarm::ACarAlarm()
    : voice(nullptr, ABank_Category(0)->snd_bank, lookup(ABank_Category(0), "SFX_CarAlarm")), mix(AMix_Get("Horns")) {
    voice.mix = mix;
}

// [driving] 0x002dec38 ACarAlarm::Play(APath &): o volume do caminho, sem o da mixagem.
void ACarAlarm::Play(APath& path) {
    AVoice_Play(path.volume, path.pitch, path.azimuth, 0.0f, 0.0f, &voice, path.listener->view);
}

// ---- AVehicle::Activate* -------------------------------------------------------------------------------------------

void AVehicle::ActivateGun(const char* mix_name) {
    if (gun == nullptr) gun = new AGun(this, mix_name);
}
void AVehicle::ActivateTurret() {
    if (turret == nullptr) turret = new ATurret();
}
void AVehicle::ActivateZoom() {
    if (zoom == nullptr) zoom = new AZoomObj(this);
}
void AVehicle::StartAlarm() {
    if (alarm == nullptr) alarm = new ACarAlarm();
}

// ---- APlayerHeli ---------------------------------------------------------------------------------------------------

// [driving] 0x002f1428 APlayerHeli::APlayerHeli: seis vozes do banco Generic na "Player Car", a metralhadora, a
// luneta e o vento; o tipo 2.
APlayerHeli::APlayerHeli() : AVehicle("Player Car", 0xd) {
    ABank* g = ABank_Category(0);
    for (AVoice*& v : change) v = new AVoice(AMix_Get("Player Car"), g->snd_bank, lookup(g, "SFX_helichange"));
    for (AVoice*& v : rotor) v = new AVoice(AMix_Get("Player Car"), g->snd_bank, lookup(g, "SFX_heli"));
    max_distance = 0.0f;
    max_distance2 = 0.0f;
    ActivateGun("Machine Gun");
    ActivateZoom();
    ActivateWind();
    vehicle_type = 2;
}

// [driving] 0x002f1ae8 APlayerHeli::~APlayerHeli
APlayerHeli::~APlayerHeli() {
    for (AVoice*& v : change) free_voice(v);
    for (AVoice*& v : rotor) free_voice(v);
}

// [driving] 0x002f16b8 APlayerHeli::Play(APath &): o AVehicle; a mistura anda 10% por quadro para 0 (parado) ou 1
// (acima de 1 m/s); perto da câmera (menos de 2 m: dentro) as três vozes de cada timbre à esquerda, à direita e no
// centro, sem o volume do caminho; longe, uma voz de cada no caminho.
void APlayerHeli::Play(APath& path) {
    AVehicle::Play(path);
    PlayOwn(path);
}

void APlayerHeli::PlayOwn(APath& path) {
    const float s = ee::sqrt(ee::add(ee::add(ee::mul(vel[0], vel[0]), ee::mul(vel[1], vel[1])), ee::mul(vel[2], vel[2])));
    const float k = ps2::ffloat(0x3f666666u);  // 0,9
    if (ee::lt(s, 1.0f)) blend = ee::mul(blend, k);
    else blend = ee::add(ee::mul(blend, k), ps2::ffloat(0x3dcccccdu));
    const int32_t view = path.listener->view;
    const float pitch_ = ee::mul(pitch, path.pitch);
    if (ee::lt(path.distance, 2.0f)) {
        const float az[3] = {0.25f, 0.75f, 0.0f};
        for (int i = 0; i < 3; ++i) {
            const float mv = AMix_GetVolume(mix);
            AVoice_Play(ee::mul(ee::mul(volume, mv), ee::sub(1.0f, blend)), pitch_, az[i], 0.0f, 0.0f, change[i], view);
        }
        for (int i = 0; i < 3; ++i) {
            const float mv = AMix_GetVolume(mix);
            AVoice_Play(ee::mul(ee::mul(volume, mv), blend), pitch_, az[i], 0.0f, 0.0f, rotor[i], view);
        }
        return;
    }
    {
        const float mv = AMix_GetVolume(mix);
        AVoice_Play(ee::mul(ee::mul(ee::mul(volume, mv), ee::sub(1.0f, blend)), path.volume), pitch_, path.azimuth,
                    0.0f, 0.0f, change[0], view);
    }
    AVoice_Play(0.0f, pitch_, path.azimuth, 0.0f, 0.0f, change[1], view);
    AVoice_Play(0.0f, pitch_, path.azimuth, 0.0f, 0.0f, change[2], view);
    {
        const float mv = AMix_GetVolume(mix);
        AVoice_Play(ee::mul(ee::mul(ee::mul(volume, mv), blend), path.volume), pitch_, path.azimuth, 0.0f, 0.0f,
                    rotor[0], view);
    }
    AVoice_Play(0.0f, pitch_, path.azimuth, 0.0f, 0.0f, rotor[1], view);
    AVoice_Play(0.0f, pitch_, path.azimuth, 0.0f, 0.0f, rotor[2], view);
}

// ---- ATargeting ----------------------------------------------------------------------------------------------------

// [driving] 0x002e6a88 ATargeting::ATargeting
void ATargeting_Construct(ATargetingData& t) {
    t.state = 0;
    t.unknown04 = 0;
    t.mix = nf::p32_u(AMix_Get("Tracking System"));
}

// [driving] 0x002e6ac8 ATargeting::SetState(int): o bipe da trava (2, de 0 ou 1) e o de soltar (0, de 1 ou 2); nada
// com o som pausado; outro estado avisa (printf).
void ATargeting_SetState(ATargetingData& t, int32_t s) {
    if (astream_globals().is_paused != 0) return;
    if (s == t.state) return;
    if (s == 0) {
        if (uint32_t(t.state - 1) < 2u) AMenuSound_TriggerCategory(0, "SFX_targetunlock", "Tracking System", 0);
    } else if (s == 2) {
        if (uint32_t(t.state) < 2u) AMenuSound_TriggerCategory(0, "SFX_targetlock", "Tracking System", 0);
    } else if (s != 1) {
        ++g_stats.targeting_warnings;  // "WARNING: Unknown HUD state updating targeting audio!"
    }
    t.state = s;
    ++g_stats.targeting;
}

// ---- ASmackable ----------------------------------------------------------------------------------------------------

// [driving] 0x002ef6f0 ASmackable::ASmackable(int, int, int, int, char *): os três timbres do choque e a voz do rolar
// (com o rolar, 5 m de mínimo); 75 m de alcance.
ASmackable::ASmackable(int32_t lo, int32_t med, int32_t hi, int32_t roll, const char* mix_name) : ASceneObj(mix_name) {
    ids[0] = lo;
    ids[1] = med;
    ids[2] = hi;
    if (-1 < roll) {
        roll_voice = new_voice(mix, roll);
        min_distance = 5.0f;
    }
    max_distance = 75.0f;
    max_distance2 = 5625.0f;
    live_smackables().insert(this);
    ++g_stats.smackables;
}

// [driving] 0x002ef8d0 ASmackable::~ASmackable
ASmackable::~ASmackable() {
    free_voice(collision_voice);
    free_voice(roll_voice);
    live_smackables().erase(this);
}

// [driving] 0x002ef7d0 ASmackable::PlayLanding: o rolar conta os passos no chão (até 10).
void ASmackable::PlayLanding(float, const float*, const float*) {
    if (roll_frames < 10) roll_frames += sound_count();
}

// [driving] 0x002ef7f8 ASmackable::PlayCollision: acima de 0,75 o médio e o forte alternam (o 0x00355ea0 muda mesmo
// sem voz); a voz do choque, de uma vez só, se ainda não há uma.
void ASmackable::PlayCollision(float force, const float*, const float*, int32_t) {
    int32_t which = 0;
    if (ee::lt(0.75f, force)) {
        which = g_next_hard + 1;
        g_next_hard = g_next_hard == 0 ? 1 : 0;
    }
    const int32_t id = ids[which];
    if (-1 < id && collision_voice == nullptr) {
        collision_voice = new_voice(mix, id);
        once(collision_voice);
        ++g_stats.smack_collisions;
    }
}

// [driving] 0x002ef9d0 ASmackable::Play(APath &): o rolar sobe dois passos e desce um (0,04 por passo do som) para a
// velocidade / 10; o choque toca até acabar; sem ele, o objeto solto se apaga.
void ASmackable::Play(APath& path) {
    const int32_t view = path.listener->view;
    if (roll_voice != nullptr) {
        const float step = ee::mul(ee::cvt_s(sound_count()), 0.04f);
        float target = 0.0f;
        if (0 < roll_frames) {
            roll_frames -= sound_count();
            if (roll_frames < 0) roll_frames = 0;
            if (2 < roll_frames) {
                const float sp = vu_length(vel);
                target = ee::lt(sp, 10.0f) ? ee::mul(sp, ps2::ffloat(0x3dcccccdu)) : 1.0f;
            }
        }
        const float cur = roll_volume;
        const float d = ee::sub(target, cur);
        if (ee::le(d, ee::neg(step))) roll_volume = ee::sub(cur, step);
        else if (ee::le(step, d)) roll_volume = ee::add(cur, ee::add(step, step));
        else roll_volume = target;
        const float mv = AMix_GetVolume(mix);
        AVoice_Play(ee::mul(ee::mul(ee::mul(volume, mv), roll_volume), path.volume), path.pitch, path.azimuth, 0.0f,
                    0.0f, roll_voice, view);
    }
    if (collision_voice == nullptr) {
        if (detached != 0) destroy();
        return;
    }
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f, collision_voice, view);
    if (AVoice_Done(*collision_voice)) free_voice(collision_voice);
}

// [driving] 0x002efc80 ASmackable::Detatch: com a voz do choque ainda tocando, fica até ela acabar; senão se apaga.
void ASmackable::Detatch() {
    if (collision_voice != nullptr && !AVoice_Done(*collision_voice)) {
        detached = 1;
        ++g_stats.smack_detached;
        return;
    }
    destroy();
}

ASmackable* asmackable_from(uint32_t h) {
    if (h == 0) return nullptr;
    ASmackable* a = nf::p32_ptr<ASmackable>(h);
    return live_smackables().count(a) != 0 ? a : nullptr;
}

// O new do Smackable::Smackable (ABaseSound::operator new(0xf0, "ASmackable") e o construtor) com a mixagem
// "Smackables".
uint32_t ASmackable_New(int32_t lo, int32_t med, int32_t hi, int32_t roll) {
    if (!audio_ready()) return 0;
    return nf::p32_u(new ASmackable(lo, med, hi, roll, "Smackables"));
}

void ASmackable_Detatch(uint32_t h) {
    if (ASmackable* a = asmackable_from(h)) a->Detatch();
}

void ASmackable_Feed(uint32_t h, const float p[3], const float v[3]) {
    if (ASmackable* a = asmackable_from(h)) {
        std::memcpy(a->pos, p, 12);
        std::memcpy(a->vel, v, 12);
    }
}

bool ASmackable_IsStillActive(uint32_t h) {
    ASmackable* a = asmackable_from(h);
    return a != nullptr && a->IsStillActive();
}

// [driving] 0x00194d48 (o leitor dos campos de som da "PhysicsData" do PhysicsNamespace): o índice do nome no banco da
// primeira categoria, ou no da segunda; o banco << 7 | o índice, ou -1.
int32_t audio_patch_id(const char* name, int32_t first, int32_t second) {
    ABank* b = ABank_Category(first);
    int32_t i = b->index.Lookup(name);
    if (i < 0) {
        b = ABank_Category(second);
        i = b->index.Lookup(name);
        if (i < 0) return i;
    }
    return i | (b->snd_bank << 7);
}

}  // namespace drv
