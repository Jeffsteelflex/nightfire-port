#pragma once
// A interface do jogo com o driver de som do IOP: SFX* (0x00111b90-0x001171b0 no ACTION.ELF). Ver docs/formats.md,
// "Áudio", e docs/engine.md, "Som".
//
// Cada função põe um comando no lote SFXBatchCommands (até 0x800 palavras; 0x79d é o limite antes de forçar o
// envio); SFXUpdate (de psiPreGame_Run/psiPostGame_Run, uma vez por VBlank) fecha o lote com 0x19 (NisPlaying,
// NisQuitten), 4 (SFXUpdate do IOP) e 5 e o manda (PS2_SFX_SendBatchCommandsToIOP); a resposta é SFXActiveData, os
// sons vivos no IOP, que SFXIsSFXPlaying consulta junto com os pedidos dos últimos 5 VBlanks (SFXRequestedActiveData).
// Os comandos "lentos" (carregar banco, pausar as streams, suspender o disco) vão num segundo lote
// (SFXBatchCommands2) mandado na hora. No PC o RPC é nf::sfx_iop_rpc (platform/sfx_iop.h), que antes mistura a SPU
// até o VBlank atual (platform/audio_out.h).
//
// Os volumes ficam guardados aqui também (os Get devolvem o valor guardado, ou 1 com o som desligado,
// switch_CONFIG_SOUND): o perfil do codinome o grava no bloco GSET, as páginas do menu o mostram, nightfire.ini o
// lembra (platform/game_options.h). Com o lote cheio o original força um SFXUpdate e perde o valor (preservado).
//
// Divergência: SFXReset2 espera o IOP terminar o reset; no PS2 isso leva vários VBlanks (o IOP relê STREAMS.LUT do
// CD), então o SFXUpdate seguinte sempre manda o lote. No PC o reset é imediato: se o 0xf ficou no lote (mesmo
// VBlank), SFXReset2 força o envio; senão o reset chegaria depois do banco carregado por SFXLoadSoundBank e o
// descarregaria.

#include <cstdint>

extern uint32_t MusicMasterVolume;    // 0x0030c958 (70 no disco)
extern uint32_t SfxMasterVolume;      // 0x0030c95c (100)
extern uint32_t SFXModeNumber;        // 0x0030c964 (1, estéreo)
extern uint32_t switch_CONFIG_SOUND;  // 0x0030ceac (1)
extern uint32_t SFXLanguageNumber;    // 0x0030c960
extern int32_t NisPlaying;            // 0x0030c8b8 uma cena (NIS) começou e não acabou (UpdateMusicalEvents)
extern int32_t NisQuitten;            // 0x0030c8bc VBlanks desde o fim de uma cena (5 a 0)
extern int32_t SFXPaused;             // 0x0030c92c SFXPause/SFXUnPause
extern int32_t SfxNisMusicTrigger;    // 0x0030c8b4 o som 0x172 (o gatilho de música das cenas) foi pedido
extern uint32_t VBlankCount2;         // 0x0030c94c o VBlank do último SFXUpdate
extern int32_t SFXBatchPos;           // 0x0030c934
extern int32_t SFXBatchCommands[0x800];   // 0x00250340
extern int32_t SFXActiveData[0x55];       // 0x0024ff80 a resposta do IOP: {n, 40 x {som, handle}, ...}
extern int32_t SFXRequestedActiveData[0x79];  // 0x0024f230 {n, 40 x {som, handle, vida}}

void SFXMusicSetVolume(uint32_t volume);
uint32_t SFXMusicGetVolume();
void SFXSetVolume(uint32_t volume);
uint32_t SFXGetVolume();
void SFXSetMode(int32_t mode);
uint32_t SFXGetMode();

uint32_t SFXIsSFXPlaying(uint32_t handle, int32_t sound);
void PS2_SFX_SendBatchCommandsToIOP();
void SFXUpdate();
void SFXInitialise(const char* path);
void SFXLoadSoundBank(uint32_t bank);
uint32_t SFXStart(uint32_t sound, uint32_t handle);
uint32_t SFXStart3D(uint32_t sound, const float* pos, const float* vel, uint32_t handle, int32_t volume, int32_t inner,
                    int32_t outer);
void SFXPauseAllStreams();
void SFXUnPauseAllStreams();
void SFXSetPositionAndVelocity(const float* pos, const float* vel, uint32_t handle, int32_t sound);
void SFXSetupListener(const float* pos, float* vel, const float* dir, const float* up, const float* norm, int32_t index);
void SFXSuspendFileAccess();
void SFXUnSuspendFileAccess();
void SFXStartMusic(uint32_t music, int32_t marker);
void SFXStopMusic();
void SFXJumpToMusicMarker(int32_t marker, int32_t instant);
void PS2_SFXWaitTillFinished();
void SFXReset();
void SFXReset2();
void SFXRemove(uint32_t handle, uint32_t sound);
void SFXPause();
void SFXUnPause();
void SFXSetFilePath(const uint32_t* path);
void SFXFadeDown(int32_t with_music);
void SFXSetNumberOfListeners(uint32_t n);
void SFXSetLanguage(uint32_t language);
void SFXSetEnvironment(int32_t reverb, int32_t outdoors);
uint32_t SFXGetLanguage();
void SFXPreCache(uint32_t sound, int32_t frames);
void PS2Sound_Init();

// A lógica da música (engine/sound.cpp), chamada no fim de cada SFXUpdate que mandou o lote.
void UpdateMusicalEvents();

// Só no port: os valores do disco (testes).
void psi_sound_debug_power_on();
