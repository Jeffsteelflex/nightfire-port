#pragma once
// A pausa do som do jogo de carro (ASoundManager::Pause e Resume do DRIVING.ELF; docs/driving.md seção 28): o filme da
// missão (o ESetVideo, os filmes de abertura e de fim do RunTheGame) cala os sons do jogo enquanto toca e os devolve
// depois.
//
// Pause: cada som da lista (ASoundManager::fgSoundList) toca de novo, em cada vista em uso com volume no quadro anterior,
// com o volume de antes e o pitch 0 (as vozes param), e fica marcado (+0x80); o AVoice::PlayVoices leva isso ao SND; o
// AFX::Pause; o fgIsPaused. Resume: as marcas saem, o AFX::Resume, o fgIsPaused a 0 e os volumes da música e dos efeitos
// do ASoundManager de volta nas mixagens "1:Music" e "1:Effects" (o anterior vai para o +8 delas).
//
// Divergências (só do port): o caminho que o PS2 passa ao Play de cada som é um APath na pilha com o volume, o pitch, o
// azimute e a direção escritos e o resto (o +0x10 em diante: o atraso, a distância e o ouvinte) com o que a pilha
// tiver; o port põe 0 no atraso e na distância e o ouvinte da vista do laço (o ouvinte decide a vista em que o som
// toca). O AFX::Pause/Resume (o reverb do EE) não estão portados: contados.

#include <cstdint>

namespace drv {

// Os volumes que o ASoundManager::Init guardou (fMusicVolume, fSFXVolume: o UberBond +0x968/+0x964 / 100).
void ASoundManager_SetVolumes(float music, float sfx);
// [driving] 0x002ec568 ASoundManager::Pause
void ASoundManager_Pause();
// [driving] 0x002ec6d8 ASoundManager::Resume
void ASoundManager_Resume();

struct AudioPauseStats {
    uint64_t pauses = 0, resumes = 0, sounds_paused = 0, afx_calls = 0;
};
AudioPauseStats& audio_pause_stats();

}  // namespace drv
