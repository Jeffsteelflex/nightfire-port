// A pausa do som do jogo de carro (driving/sound/audio_pause.h; docs/driving.md seção 28).
#include "driving/sound/audio_pause.h"

#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_stream.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {
float g_music = 1.0f, g_sfx = 1.0f;  // ASoundManager::fMusicVolume, fSFXVolume
AudioPauseStats g_stats;
}  // namespace

AudioPauseStats& audio_pause_stats() { return g_stats; }

void ASoundManager_SetVolumes(float music, float sfx) {
    g_music = music;
    g_sfx = sfx;
}

void ASoundManager_Pause() {
    ++g_stats.pauses;
    // A lista pode perder sons enquanto anda (um AOneShotSound se apaga no Play): o próximo é lido antes, como o PS2.
    std::list<ABaseSound*>& list = sound_list();
    for (auto it = list.begin(); it != list.end();) {
        ABaseSound* s = *it;
        ++it;
        for (int32_t view = 0; view < 3; ++view) {
            if ((s->views & (1u << uint32_t(view))) == 0) continue;
            if (!ee::lt(0.0f, s->last_volume[view])) continue;
            AListener l;  // AListener::AListener(view) (0x002f4d78)
            l.view = view;
            APath p;
            p.volume = s->last_volume[view];
            p.pitch = 0.0f;
            p.azimuth = 0.0f;
            p.facing = 0.0f;
            p.listener = &l;  // o +0x10 e o +0x14 do PS2 são o que a pilha tiver (o port: 0)
            s->Play(p);
            ++g_stats.sounds_paused;
        }
        s->disabled = 1;
    }
    AVoice_PlayVoices();
    ++g_stats.afx_calls;  // AFX::Pause (0x002f8248): não portado
    AStreamGlobalsView g = astream_globals();
    g.is_paused = 1;
    astream_set_globals(g);
}

void ASoundManager_Resume() {
    ++g_stats.resumes;
    for (ABaseSound* s : sound_list()) s->disabled = 0;
    ++g_stats.afx_calls;  // AFX::Resume: não portado
    AStreamGlobalsView g = astream_globals();
    g.is_paused = 0;
    astream_set_globals(g);
    if (AMix* m = AMix_Get("1:Music")) {
        m->saved = m->volume;
        m->volume = g_music;
    }
    if (AMix* m = AMix_Get("1:Effects")) {
        const float old = m->volume;
        m->volume = g_sfx;
        m->saved = old;
    }
}

}  // namespace drv
