// A interface do jogo com o driver de som do IOP (SFX*): ver psi_sound.h.

#include "platform/psi_sound.h"

#include <cstring>

#include "platform/audio_out.h"
#include "platform/psi.h"
#include "platform/sfx_iop.h"

uint32_t MusicMasterVolume = 0x46;
uint32_t SfxMasterVolume = 100;
uint32_t SFXModeNumber = 1;
uint32_t switch_CONFIG_SOUND = 1;
uint32_t SFXLanguageNumber = 0;
int32_t NisPlaying = 0;
int32_t NisQuitten = 0;
int32_t SFXPaused = 0;
int32_t SfxNisMusicTrigger = 0;
uint32_t VBlankCount2 = 0;
int32_t SFXBatchPos = 0;
int32_t SFXBatchCommands[0x800];
int32_t SFXActiveData[0x55];
int32_t SFXRequestedActiveData[0x79];

namespace {

int32_t SFXBatchPos2 = 0;
int32_t SFXBatchCommands2[0x800];
int32_t LastNisPlaying = 0;
int32_t LastNisQuitten = 0;
int32_t AllowSkip = 0;
int32_t SlowCommand = 0;
int32_t LastSlowCommand = 0;
int32_t LimitSFXSetupListenerCount = 0;
int32_t HaveCalledOnce_296 = 0;
int32_t SFXFileAccessSuspended = 0;
constexpr float Zero3Dpos[3] = {0.0f, 0.0f, 0.0f};          // 0x0024ff40
constexpr float NormalisedVector_336[3] = {1.0f, 0.0f, 0.0f};  // 0x002500d8

// Um pedido recente (SFXRequestedActiveData +4: 40 x {som, handle, vida em VBlanks}).
int32_t& req_sound(int i) { return SFXRequestedActiveData[1 + i * 3]; }
int32_t& req_handle(int i) { return SFXRequestedActiveData[2 + i * 3]; }
int32_t& req_life(int i) { return SFXRequestedActiveData[3 + i * 3]; }

void push(int32_t v) { SFXBatchCommands[SFXBatchPos++] = v; }
void push2(int32_t v) { SFXBatchCommands2[SFXBatchPos2++] = v; }
int32_t mm(float v) { return int32_t(v * 1000.0f); }  // metros -> milímetros (cvt.w.s trunca)

// O RPC: no PS2 sceSifCallRpc(0x40013575), aqui o driver do PC; antes a SPU anda até o VBlank atual.
void call_iop(const int32_t* commands, int32_t count) {
    nf::audio_out_catch_up(VBlankCount);
    nf::sfx_iop_rpc(commands, count, reinterpret_cast<uint8_t*>(SFXActiveData));
}

// O lote cheio: força um SFXUpdate na próxima chamada.
void batch_full() {
    AllowSkip = 0;
    VBlankCount2 = VBlankCount2 - 9999;
    SFXUpdate();
}

// O lote lento (SFXBatchCommands2), mandado na hora.
void send_slow() {
    if (switch_CONFIG_SOUND == 0) return;
    push2(5);
    call_iop(SFXBatchCommands2, SFXBatchPos2);
    if (switch_CONFIG_SOUND != 0) SFXBatchPos2 = 0;
}

// Guarda um pedido em SFXRequestedActiveData (vida de 5 VBlanks), para SFXIsSFXPlaying responder antes do IOP.
void remember_request(int32_t sound, uint32_t handle) {
    for (int i = 0; i < 0x28; ++i) {
        if (req_life(i) == 0) {
            req_life(i) = 5;
            req_sound(i) = sound;
            req_handle(i) = int32_t(handle);
            return;
        }
    }
}

// Os comandos de cena (0x19) antes de um começo de som, se NisPlaying/NisQuitten mudaram.
void nis_state() {
    if (LastNisPlaying != NisPlaying || LastNisQuitten != NisQuitten) {
        push(0x19);
        push(NisPlaying);
        push(NisQuitten);
    }
}

}  // namespace

// [decomp] 0x00111b90 SFXIsSFXPlaying
// O som (handle e número; 0 e -1 valem qualquer um) foi pedido nos últimos 5 VBlanks ou está vivo no IOP.
uint32_t SFXIsSFXPlaying(uint32_t handle, int32_t sound) {
    for (int i = 0; i < 0x28; ++i) {
        if (req_life(i) <= 0) continue;
        if (uint32_t(req_handle(i)) != handle && handle != 0) continue;
        if (req_sound(i) == sound || sound == -1) return 1;
    }
    if (SFXActiveData[0] == 0) return 0;
    if (handle == 0 && sound == -1) return 1;
    for (int i = 0; i < SFXActiveData[0]; ++i) {
        const int32_t s = SFXActiveData[1 + i * 2];
        const uint32_t h = uint32_t(SFXActiveData[2 + i * 2]);
        if (h != handle && handle != 0) continue;
        if (s == sound || sound == -1) return 1;
    }
    return 0;
}

// [decomp] 0x001155d0 PS2_SFX_SendBatchCommandsToIOP
// O lote normal com o 5 do fim. No PS2 espera o RPC anterior (pulando com AllowSkip depois de 11 ciclos do T0); no
// PC o RPC é síncrono e nunca está ocupado.
void PS2_SFX_SendBatchCommandsToIOP() {
    if (switch_CONFIG_SOUND == 0) return;
    AllowSkip = 1;
    LastSlowCommand = SlowCommand;
    SlowCommand = 0;
    push(5);
    call_iop(SFXBatchCommands, SFXBatchPos);
    if (switch_CONFIG_SOUND != 0) SFXBatchPos = 0;
    LimitSFXSetupListenerCount = 0;
}

// [decomp] 0x001156c0 SFXUpdate
// Uma vez por VBlank: fecha o lote com o estado das cenas (0x19) e o update do IOP (4), manda, envelhece os pedidos
// lembrados e roda a música (UpdateMusicalEvents).
void SFXUpdate() {
    if (VBlankCount == VBlankCount2) return;
    VBlankCount2 = VBlankCount;
    if (switch_CONFIG_SOUND == 0) return;
    push(0x19);
    push(NisPlaying);
    push(NisQuitten);
    push(4);
    PS2_SFX_SendBatchCommandsToIOP();
    for (int i = 0; i < 0x28; ++i) {
        if (req_life(i) > 0) --req_life(i);
    }
    UpdateMusicalEvents();
}

// [decomp] 0x001157b0 SFXInitialise
// Uma vez por sessão: língua, modo, volumes e o comando 0 (o IOP lê SBINFO.SBI e reserva as vozes das streams).
void SFXInitialise(const char* path) {
    if (switch_CONFIG_SOUND == 0 || HaveCalledOnce_296 != 0) return;
    SFXBatchPos = 0;
    SFXBatchPos2 = 0;
    SFXSetLanguage(SFXLanguageNumber);
    SFXSetMode(int32_t(SFXModeNumber));
    SFXSetVolume(SfxMasterVolume);
    SFXMusicSetVolume(MusicMasterVolume);
    if (path != nullptr) SFXSetFilePath(reinterpret_cast<const uint32_t*>(path));
    push(0);
    for (int i = 0; i < 0x28; ++i) req_life(i) = 0;
    AllowSkip = 0;
    SlowCommand = 1;
    HaveCalledOnce_296 = 1;
}

// [decomp] 0x00115938 SFXLoadSoundBank
// Comando lento: reset (SFXReset2) e o banco, mandados na hora.
void SFXLoadSoundBank(uint32_t bank) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    SFXReset2();
    push2(1);
    push2(int32_t(bank));
    SlowCommand = 1;
    AllowSkip = 0;
    send_slow();
}

// [decomp] 0x00115a68 SFXStart
// O som 0x172 não toca: é o gatilho de música das cenas (SfxNisMusicTrigger).
uint32_t SFXStart(uint32_t sound, uint32_t handle) {
    if (switch_CONFIG_SOUND == 0) return 1;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return 1;
    }
    if (sound == 0x172) {
        SfxNisMusicTrigger = 1;
        return 1;
    }
    nis_state();
    LastNisQuitten = NisQuitten;
    LastNisPlaying = NisPlaying;
    push(3);
    push(int32_t(sound));
    push(int32_t(handle));
    if (handle != 0 || int32_t(sound) != -1) remember_request(int32_t(sound), handle);
    return 0;
}

// [decomp] 0x00115bf0 SFXStart3D
uint32_t SFXStart3D(uint32_t sound, const float* pos, const float* vel, uint32_t handle, int32_t volume, int32_t inner,
                    int32_t outer) {
    if (switch_CONFIG_SOUND == 0) return 1;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return 1;
    }
    if (sound == 0x172) {
        SfxNisMusicTrigger = 1;
        return 1;
    }
    nis_state();
    LastNisPlaying = NisPlaying;
    LastNisQuitten = NisQuitten;
    if (pos == nullptr) pos = Zero3Dpos;
    if (vel == nullptr) vel = Zero3Dpos;
    push(0xd);
    push(int32_t(sound));
    push(mm(pos[0]));
    push(mm(pos[1]));
    push(mm(pos[2]));
    push(mm(vel[0]));
    push(mm(vel[1]));
    push(mm(vel[2]));
    push(int32_t(handle));
    push(volume);
    push(inner);
    push(outer);
    if (handle != 0 || int32_t(sound) != -1) remember_request(int32_t(sound), handle);
    return 0;
}

// [decomp] 0x00115ec8 SFXPauseAllStreams
void SFXPauseAllStreams() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push2(0x16);
    SlowCommand = 1;
    AllowSkip = 0;
    send_slow();
}

// [decomp] 0x00115ff0 SFXUnPauseAllStreams
void SFXUnPauseAllStreams() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push2(0x17);
    SlowCommand = 1;
    AllowSkip = 0;
    send_slow();
}

// [decomp] 0x00116118 SFXSetPositionAndVelocity
void SFXSetPositionAndVelocity(const float* pos, const float* vel, uint32_t handle, int32_t sound) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    if (pos == nullptr) pos = Zero3Dpos;
    if (vel == nullptr) vel = Zero3Dpos;
    push(0xb);
    push(mm(pos[0]));
    push(mm(pos[1]));
    push(mm(pos[2]));
    push(mm(vel[0]));
    push(mm(vel[1]));
    push(mm(vel[2]));
    push(int32_t(handle));
    push(sound);
}

// [decomp] 0x00116298 SFXSetupListener
// Só o primeiro ouvinte de cada lote; a velocidade do chamador é zerada antes de ir.
void SFXSetupListener(const float* pos, float* vel, const float* dir, const float* up, const float* norm, int32_t index) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    const bool first = LimitSFXSetupListenerCount < 1;
    ++LimitSFXSetupListenerCount;
    if (!first) return;
    if (vel != nullptr) std::memcpy(vel, Zero3Dpos, sizeof(Zero3Dpos));
    const float* v = vel != nullptr ? vel : Zero3Dpos;
    if (pos == nullptr) pos = Zero3Dpos;
    if (norm == nullptr) norm = NormalisedVector_336;
    push(0xe);
    push(mm(pos[0]));
    push(mm(pos[1]));
    push(mm(pos[2]));
    push(mm(v[0]));
    push(mm(v[1]));
    push(mm(v[2]));
    push(mm(dir[0]));
    push(mm(dir[1]));
    push(mm(dir[2]));
    push(mm(up[0]));
    push(mm(up[1]));
    push(mm(up[2]));
    push(mm(norm[0]));
    push(mm(norm[1]));
    push(mm(norm[2]));
    push(index);
}

// [decomp] 0x001165d0 SFXSuspendFileAccess
void SFXSuspendFileAccess() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push2(0x24);
    SlowCommand = 1;
    SFXFileAccessSuspended = 1;
    AllowSkip = 0;
    send_slow();
}

// [decomp] 0x001166f8 SFXUnSuspendFileAccess
void SFXUnSuspendFileAccess() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push2(0x25);
    SlowCommand = 1;
    SFXFileAccessSuspended = 0;
    AllowSkip = 0;
    send_slow();
}

// [decomp] 0x00116858 SFXStartMusic
void SFXStartMusic(uint32_t music, int32_t marker) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x11);
    push(int32_t(music));
    push(marker);
}

// [decomp] 0x001168e8 SFXStopMusic
void SFXStopMusic() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x12);
}

// [decomp] 0x00116950 SFXJumpToMusicMarker
void SFXJumpToMusicMarker(int32_t marker, int32_t instant) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x13);
    push(marker);
    push(instant);
}

// [decomp] 0x001169e0 PS2_SFXWaitTillFinished
// Espera o RPC: no PC ele já terminou.
void PS2_SFXWaitTillFinished() {}

// [decomp] 0x00116a18 SFXReset
void SFXReset() { SFXReset2(); }

// [decomp] 0x00116a38 SFXReset2
// Divergência (ver psi_sound.h): no mesmo VBlank do último SFXUpdate o lote com o 0xf é mandado na hora.
void SFXReset2() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0xf);
    SlowCommand = 1;
    AllowSkip = 0;
    SFXUpdate();
    if (SFXBatchPos != 0) {
        VBlankCount2 = VBlankCount2 - 9999;
        SFXUpdate();
    }
}

// [decomp] 0x00116ae0 SFXRemove
void SFXRemove(uint32_t handle, uint32_t sound) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0xc);
    push(int32_t(handle));
    push(int32_t(sound));
}

// [decomp] 0x00116b70 SFXPause
void SFXPause() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    SFXPaused = 1;
    push(9);
}

// [decomp] 0x00116be0 SFXUnPause
void SFXUnPause() {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(10);
    SFXPaused = 0;
}

// [decomp] 0x00116c50 SFXSetFilePath
// 50 palavras do caminho (o jogo não chama; o driver do PC usa a raiz dos dados).
void SFXSetFilePath(const uint32_t* path) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x14);
    for (int i = 0; i < 0x32; ++i) push(int32_t(path[i]));
}

// [decomp] 0x00116cf8 SFXMusicSetVolume
void SFXMusicSetVolume(uint32_t volume) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x15);
    push(int32_t(volume));
    MusicMasterVolume = volume;
}

// [decomp] 0x00116d78 SFXMusicGetVolume
uint32_t SFXMusicGetVolume() { return switch_CONFIG_SOUND == 0 ? 1 : MusicMasterVolume; }

// [decomp] 0x00116d98 SFXSetVolume
void SFXSetVolume(uint32_t volume) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(8);
    push(int32_t(volume));
    SfxMasterVolume = volume;
}

// [decomp] 0x00116e18 SFXGetVolume
uint32_t SFXGetVolume() { return switch_CONFIG_SOUND == 0 ? 1 : SfxMasterVolume; }

// [decomp] 0x00116e38 SFXFadeDown
void SFXFadeDown(int32_t with_music) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(6);
    push(with_music);
}

// [decomp] 0x00116ec0 SFXSetNumberOfListeners
void SFXSetNumberOfListeners(uint32_t n) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x18);
    push(int32_t(n));
}

// [decomp] 0x00116f48 SFXSetLanguage
void SFXSetLanguage(uint32_t language) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x1b);
    push(int32_t(language));
    SFXLanguageNumber = language;
}

// [decomp] 0x00116fc8 SFXSetEnvironment
void SFXSetEnvironment(int32_t reverb, int32_t outdoors) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x1d);
    push(reverb);
    push(outdoors);
}

// [decomp] 0x00117058 SFXGetLanguage
uint32_t SFXGetLanguage() { return switch_CONFIG_SOUND == 0 ? 1 : SFXLanguageNumber; }

// [decomp] 0x00117078 SFXSetMode
void SFXSetMode(int32_t mode) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x1c);
    push(mode);
    SFXModeNumber = uint32_t(mode);
}

// [decomp] 0x001170f8 SFXGetMode
uint32_t SFXGetMode() { return switch_CONFIG_SOUND == 0 ? 1 : SFXModeNumber; }

// [decomp] 0x00117118 SFXPreCache
// Uma fala pré-carregada: começa calada e espera frames VBlanks (+5) por um SFXStart do mesmo som.
void SFXPreCache(uint32_t sound, int32_t frames) {
    if (switch_CONFIG_SOUND == 0) return;
    if (SFXBatchPos >= 0x79d) {
        batch_full();
        return;
    }
    push(0x26);
    push(int32_t(sound));
    push(frames);
}

// [decomp] 0x001171a8 PS2Sound_Init
void PS2Sound_Init() {}

void psi_sound_debug_power_on() {
    MusicMasterVolume = 0x46;
    SfxMasterVolume = 100;
    SFXModeNumber = 1;
    switch_CONFIG_SOUND = 1;
    SFXLanguageNumber = 0;
    NisPlaying = NisQuitten = SFXPaused = SfxNisMusicTrigger = 0;
    VBlankCount2 = 0;
    SFXBatchPos = SFXBatchPos2 = 0;
    std::memset(SFXBatchCommands, 0, sizeof(SFXBatchCommands));
    std::memset(SFXBatchCommands2, 0, sizeof(SFXBatchCommands2));
    std::memset(SFXActiveData, 0, sizeof(SFXActiveData));
    std::memset(SFXRequestedActiveData, 0, sizeof(SFXRequestedActiveData));
    LastNisPlaying = LastNisQuitten = AllowSkip = SlowCommand = LastSlowCommand = 0;
    LimitSFXSetupListenerCount = HaveCalledOnce_296 = SFXFileAccessSuspended = 0;
}
