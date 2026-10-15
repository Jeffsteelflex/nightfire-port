// As vozes da camada de som do jogo de carro (driving/sound/audio_internal.h): AVoice e as suas vistas, o PlayVoices
// (o que vai ao SND a cada quadro), ABaseSound e a lista do ASoundManager, AOneShotSound e o APath::Build. Portado do
// MIPS do DRIVING.ELF (SupportPS2.lib, BondAudioPS2.lib).
//
// Divergências (port):
//  - a lista ativa das vistas (0x00421c08) e a dos sons (ASoundManager::fgSoundList) são std::list; as alocações do
//    UMemory (FastAlloc) são new/delete;
//  - o atan2f do newlib (o azimute do APath::Build) é o nf::fm::atan2, como no resto do port do carro;
//  - as contas da VU0 em modo macro (o ponto no referencial do ouvinte, os produtos escalares, vsqrt/vrsqrt) usam as
//    operações da FPU do EE na mesma ordem (platform/ps2_float.h).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "driving/sound/audio_internal.h"
#include "driving/sound/snd.h"
#include "platform/crt_compat.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

std::list<AVoiceView*>& active() {
    static std::list<AVoiceView*> l;  // 0x00421c08
    return l;
}
int32_t g_last_tick = 0;  // UDevCons_gMap (o tick do PlayVoices anterior)

float madd3(float ax, float ay, float az, float bx, float by, float bz) {
    // vmul + vaddy.x + vaddz.x: (ax bx + ay by) + az bz
    return ee::add(ee::add(ee::mul(ax, bx), ee::mul(ay, by)), ee::mul(az, bz));
}

float length2(float x, float y, float z) {
    // vmul; vmulax.x ACC; vmadday.x; vmaddz.x (com 1,0 no outro lado)
    uint32_t acc = ps2::mul(ps2::fbits(x), ps2::fbits(x));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(y, y)));
    acc = ps2::madd(acc, ps2::fbits(1.0f), ps2::fbits(ee::mul(z, z)));
    return ps2::ffloat(acc);
}

int8_t round_byte(float x) { return int8_t(ee::cvt_w(ee::add(ee::lt(x, 0.0f) ? -0.5f : 0.5f, x))); }

// NF_DRV_SND_LOG=<arquivo> (depuração do port): cada chamada do PlayVoices ao SND.
FILE* snd_log() {
    static FILE* f = [] {
        std::string path;
        return nf::get_env_narrow("NF_DRV_SND_LOG", &path) && !path.empty() ? nf::fopen_narrow(path.c_str(), "w") : nullptr;
    }();
    return f;
}

}  // namespace

std::list<AVoiceView*>& voice_active_list() { return active(); }

// ---- AVoice --------------------------------------------------------------------------------------------------------

// [driving] 0x002de060 AVoice::View::Restart
static void AVoiceView_Restart(AVoiceView* v) {
    if (v->handle == -1) return;
    SNDSYS_entercritical();
    SNDstop(v->handle);
    SNDSYS_leavecritical();
    v->handle = -1;
}

// [driving] 0x002dd790 AVoice::AVoice(AMix &, int, int)
AVoice::AVoice(AMix* m, int32_t bank, int32_t patch) : mix(m) { Set(bank, patch); }

// [driving] 0x002dd698 AVoice::Set(int, int): a vista recomeça quando o timbre muda; sem timbre (-1) acaba.
void AVoice::Set(int32_t bank, int32_t patch) {
    for (AVoiceView& v : views) {
        if (patch == -1) {
            v.over = 1;
            continue;
        }
        if (v.bank == bank && v.patch == patch) continue;
        v.bank = bank;
        v.patch = patch;
        AVoiceView_Restart(&v);
    }
}

// [driving] 0x002de0b8 AVoice::View::~View (para as três vistas)
AVoice::~AVoice() {
    for (int i = 2; i >= 0; --i) {
        AVoiceView& v = views[i];
        if (v.listed != 0) active().remove(&v);
        if (v.handle != -1) {
            SNDSYS_entercritical();
            SNDstop(v.handle);
            SNDSYS_leavecritical();
        }
    }
}

// [driving] 0x002ddc80 AVoice::View::Stop
void AVoiceView_Stop(AVoiceView* v) {
    v->vol = 0;
    if (v->listed != 0) active().remove(v);
    if (v->handle != -1) {
        SNDSYS_entercritical();
        SNDstop(v->handle);
        SNDSYS_leavecritical();
        ++audio_stats_mut().stops;
        if (FILE* f = snd_log())
            std::fprintf(f, "%d stop %d:%d h %d\n", int(TIMER_gettick()), int(v->bank), int(v->patch), int(v->handle));
    }
    if (v->loop == 0) v->over = 1;
    v->listed = v->vol;
}

// [driving] 0x002ddd20 AVoice::View::Play(float, float, float, float, float): o volume 0..127, o pitch 4.12 (até 4),
// o azimute em 1/65536 de volta, o atraso em ticks do TIMER (só quando não há um contando) e o efeito 0..127.
static void AVoiceView_Play(float vol, float pitch, float azimuth, float delay, float fx, AVoiceView* v) {
    if (!ee::lt(0.0f, vol)) {
        AVoiceView_Stop(v);
        return;
    }
    if (v->patch < 0 || v->bank < 0) return;
    const int8_t v8 = round_byte(ee::mul(vol, 127.0f));
    v->vol = v8;
    if (ee::eq(ee::cvt_s(v->listed), 0.0f)) {
        v->listed = v8;
        active().push_back(v);  // AVoice::View::Push
    }
    if (ee::lt(pitch, 0.0f)) pitch = ee::neg(pitch);
    if (ee::lt(4.0f, pitch)) pitch = 4.0f;
    v->pitch = uint16_t(int16_t(ee::cvt_w(ee::add(ee::lt(ee::mul(pitch, 4096.0f), 0.0f) ? -0.5f : 0.5f,
                                                 ee::mul(pitch, 4096.0f)))));
    const float az = ee::mul(azimuth, 65536.0f);
    v->azimuth = uint16_t(int16_t(ee::cvt_w(ee::add(ee::lt(az, 0.0f) ? -0.5f : 0.5f, az))));
    if (ee::lt(ee::cvt_s(v->delay), 0.0f)) {
        if (ee::lt(delay, 0.0f)) delay = 0.0f;
        const float rate = ee::cvt_s(100);  // DAT_003c80d0: a frequência do TIMER
        const float limit = ee::mul(ee::div(1.0f, rate), 32767.0f);
        if (ee::lt(limit, delay)) delay = limit;
        delay = ee::mul(delay, rate);
        v->delay = int16_t(ee::cvt_w(ee::add(ee::lt(delay, 0.0f) ? -0.5f : 0.5f, delay)));
    }
    if (ee::lt(fx, 0.0f)) fx = 0.0f;
    if (ee::lt(1.0f, fx)) fx = 1.0f;
    v->fx = uint8_t(round_byte(ee::mul(fx, 127.0f)));
}

// [driving] 0x002ddfd0 AVoice::Play(AView, float, float, float, float, float)
void AVoice_Play(float vol, float pitch, float azimuth, float delay, float fx, AVoice* v, int32_t view) {
    if (v == nullptr) return;
    if (ee::lt(0.007874016f, vol)) {
        if (ee::lt(1.0f, vol)) vol = 1.0f;
        v->mix->count += 1;
        v->mix->sum = ee::add(v->mix->sum, vol);
        vol = ee::mul(vol, v->mix->gain);
    } else {
        vol = 0.0f;
    }
    AVoiceView_Play(vol, pitch, azimuth, delay, fx, &v->views[view]);
}

// [driving] 0x002dda50 AVoice::PlayVoices: cada vista ativa ao SND (SNDBANK_play quando o atraso acaba; senão volume,
// pitch, azimute e efeito, ou o fim do som).
void AVoice_PlayVoices() {
    const int32_t tick = TIMER_gettick();
    int32_t dt = tick - g_last_tick;
    if (8 < dt) dt = 1;
    g_last_tick = tick;
    auto& list = active();
    audio_stats_mut().voices_active = int32_t(list.size());
    if (list.empty()) return;
    const int32_t mode = AFX_CurrentMode();
    const int32_t all = AFX_IsAllVoices();
    SNDSYS_entercritical();
    for (auto it = list.begin(); it != list.end();) {
        auto next = std::next(it);
        AVoiceView* v = *it;
        const int8_t vol = v->vol;
        const uint16_t pitch = v->pitch, az = v->azimuth;
        const uint8_t fx = mode == -1 ? 0 : (all == 0 ? v->fx : 0x7f);
        if (v->handle == -1) {
            if (0 < vol) {
                // delay -= dt; toca quando ((delay - dt) << 16) < 1 (o resultado de 16 bits com sinal até 0)
                const int16_t left = int16_t(uint16_t(uint16_t(v->delay) - uint32_t(dt)));
                v->delay = left;
                if (left <= 0) {
                    SndPlayOpts o{};
                    SNDplaysetdef(&o);
                    o.vol = pitch == 0 ? 0 : vol;
                    o.elevation = 0;
                    o.fx_level = int8_t(fx);
                    o.azimuth = az;
                    o.pitch_mult = pitch;
                    v->handle = SNDBANK_play(v->bank, v->patch, &o);
                    ++audio_stats_mut().plays;
                    if (FILE* f = snd_log())
                        std::fprintf(f, "%d play %d:%d vol %d pitch %u az %u fx %u -> %d\n", int(tick), int(v->bank),
                                     int(v->patch), int(o.vol), unsigned(pitch), unsigned(az), unsigned(fx), int(v->handle));
                }
            }
        } else if (0 < vol) {
            if (SNDover(v->handle) == 0) {
                if (FILE* f = snd_log())
                    std::fprintf(f, "%d set %d:%d h %d vol %d pitch %u az %u fx %u\n", int(tick), int(v->bank),
                                 int(v->patch), int(v->handle), int(vol), unsigned(pitch), unsigned(az), unsigned(fx));
                SNDvol(v->handle, pitch != 0 ? vol : 0);
                SNDpitchmult(v->handle, pitch);
                SND3dpos(v->handle, az, 0);
                SNDfxlevel(v->handle, 0, fx);
            } else if (v->loop == 0) {
                v->over = 1;
            } else {
                if (FILE* f = snd_log())
                    std::fprintf(f, "%d over %d:%d h %d\n", int(tick), int(v->bank), int(v->patch), int(v->handle));
                SNDstop(v->handle);
                v->handle = -1;
            }
        }
        it = next;
    }
    SNDSYS_leavecritical();
}

// ---- ABaseSound ----------------------------------------------------------------------------------------------------

// [driving] 0x002fca38 ABaseSound::operator new + [driving] 0x002fcdb0 ABaseSound::ABaseSound(char *, AView). O
// operator new põe o som no COMEÇO do fgSoundList (insere antes do begin(): o mais novo é o primeiro que o BuildPaths
// percorre).
ABaseSound::ABaseSound(const char* mix_name, int32_t view) {
    sound_list().push_front(this);
    mix = AMix_Add(mix_name);
    views |= view != 4 ? (1u << (uint32_t(view) & 31)) : 4u;  // DAT_00355cd0: as vistas em uso (4: a 2)
}

ABaseSound::~ABaseSound() {
    AMix_Remove(mix);
    sound_list().remove(this);
}

void ABaseSound::destroy() { delete this; }

// ---- AOneShotSound -------------------------------------------------------------------------------------------------

// O AOneShotSound que o AVehicle e o ARaceEngine montam no lugar (o construtor inline: o volume, as três vistas de uma
// vez só, efeito 0, sem tempo de vida).
AOneShotSound::AOneShotSound(const char* mix_name, int32_t bank, int32_t patch, float vol)
    : ABaseSound(mix_name, 4), voice(mix, bank, patch) {
    volume = vol;
    for (AVoiceView& v : voice.views) v.loop = 0;
    ++audio_stats_mut().oneshots;
}

// [driving] 0x002f2428 AOneShotSound::Play(APath &): toca e se apaga quando a voz das vistas em uso acabou (ou o tempo
// de vida passou).
void AOneShotSound::Play(APath& path) {
    const int32_t view = path.listener->view;
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), ee::mul(pitch, path.pitch), path.azimuth, path.delay, fx,
                &voice, view);
    if (!ee::le(0.0f, lifetime)) return;
    bool done;
    if (voice.views[2].over == 0) {
        done = false;
    } else {
        // DAT_00355cd0 & 4: a vista 2 em uso (o jogo de um jogador) basta
        done = true;
    }
    if (done) {
        destroy();
        return;
    }
    if (ee::lt(0.0f, lifetime)) {
        lifetime = ee::sub(lifetime, ee::mul(ee::cvt_s(sound_count()), ps2::ffloat(0x3c888889u)));
        if (ee::le(lifetime, 0.0f)) destroy();
    }
}

// ---- APath ---------------------------------------------------------------------------------------------------------

// [driving] 0x002f1f40 APath::Build(ABaseSound &)
void APath_Build(APath& p, const ABaseSound& s) {
    const AListener& l = *p.listener;
    const float maxd = s.max_distance;
    auto side_azimuth = [&]() { return l.view == 2 ? 0.0f : ((l.view & 1) == 0 ? -0.25f : 0.25f); };
    if (ee::eq(maxd, 0.0f)) {
        p.pitch = 1.0f;
        p.volume = 1.0f;
        p.azimuth = side_azimuth();
        p.facing = 0.0f;
        p.delay = 0.0f;
        return;
    }
    // A faixa de altura do ambiente (o som e o ouvinte de lados diferentes dela).
    const float top = l.band_top, h = l.band_height;
    float band = 1.0f;
    if (ee::lt(top, s.pos[1]) && ee::lt(l.pos[1], top)) band = ee::mul(ee::add(l.pos[1], h), ee::div(1.0f, h));
    const float bottom = ee::sub(top, h);
    if (ee::lt(s.pos[1], bottom) && ee::lt(bottom, l.pos[1])) band = ee::mul(ee::neg(l.pos[1]), ee::div(1.0f, h));
    if (ee::lt(band, 0.001f)) {
        p.volume = 0.0f;
        return;
    }
    // O som no referencial do ouvinte (vmulax/vmadday/vmaddaz/vmaddw com w = 1).
    float rel[4];
    for (int c = 0; c < 4; ++c) {
        uint32_t acc = ps2::mul(ps2::fbits(l.rot[0][c]), ps2::fbits(s.pos[0]));
        acc = ps2::madd(acc, ps2::fbits(l.rot[1][c]), ps2::fbits(s.pos[1]));
        acc = ps2::madd(acc, ps2::fbits(l.rot[2][c]), ps2::fbits(s.pos[2]));
        acc = ps2::madd(acc, ps2::fbits(l.trans[c]), ps2::fbits(1.0f));
        rel[c] = ps2::ffloat(acc);
    }
    const float dist = ee::sqrt(length2(rel[0], rel[1], rel[2]));
    p.distance = dist;
    const float mind = s.min_distance;
    if (ee::lt(maxd, dist)) {
        p.volume = 0.0f;
        return;
    }
    if (ee::le(mind, dist))
        p.volume = ee::mul(ee::div(ee::sub(maxd, dist), ee::add(ee::sub(maxd, mind), ee::mul(ee::sub(dist, mind), 8.0f))),
                           band);
    else
        p.volume = band;
    if (!ee::lt(0.0f, dist)) {
        p.pitch = 1.0f;
        p.azimuth = side_azimuth();
        p.facing = 0.0f;
        p.delay = 0.0f;
        return;
    }
    if (ee::lt(dist, ee::mul(mind, 0.5f))) {
        p.azimuth = 0.0f;
    } else {
        float a = ee::mul(nf::fm::atan2(rel[0], rel[2]), 0.15915494f);
        if (l.view != 2) a = (l.view & 1) != 0 ? 0.25f : -0.25f;
        p.azimuth = a;
        if (ee::lt(dist, mind)) p.azimuth = ee::mul(a, ee::div(ee::sub(ee::add(dist, dist), mind), mind));
    }
    // A direção do ouvinte ao som, normalizada (vsub com w = 1 - 1; vrsqrt Q = 1 / |d|; vmulq).
    const float d[3] = {ee::sub(s.pos[0], l.pos[0]), ee::sub(s.pos[1], l.pos[1]), ee::sub(s.pos[2], l.pos[2])};
    const float q = ee::rsqrt(1.0f, length2(d[0], d[1], d[2]));
    const float n[3] = {ee::mul(d[0], q), ee::mul(d[1], q), ee::mul(d[2], q)};
    p.facing = madd3(n[0], n[1], n[2], s.dir[0], s.dir[1], s.dir[2]);
    const float vs = madd3(n[0], n[1], n[2], s.vel[0], s.vel[1], s.vel[2]);
    const float vl = madd3(n[0], n[1], n[2], l.vel[0], l.vel[1], l.vel[2]);
    p.pitch = ee::div(400.0f, ee::sub(ee::add(vs, 400.0f), vl));
    p.delay = ee::le(25.0f, dist) ? ee::div(ee::sub(dist, 25.0f), 400.0f) : 0.0f;
}

}  // namespace drv
