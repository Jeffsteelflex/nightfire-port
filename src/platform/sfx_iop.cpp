// O driver de som do IOP (SFX.IRX) no PC: ver sfx_iop.h e docs/formats.md, "Áudio".
//
// Endereços: "// [irx] 0x..." é o endereço da função no .text do SFX.IRX (python tools/sound.py irxsyms SFX.IRX).
// As globais têm os nomes da .symtab; as que o código lê por gp (gp = 0x3b880) foram identificadas pelo .sdata.

#include "platform/sfx_iop.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "platform/audio_out.h"
#include "platform/crt_compat.h"

namespace nf {

SfxIopStats g_sfx_iop_stats;

namespace {

// ---- estruturas do driver (STABS do SFX.IRX) ----

struct SFXPoint3 {
    int32_t x, y, z;  // milímetros
};

struct SFXHeader {
    int32_t HashCode;
    int32_t Offset;
};

struct SFXParameters {  // 68 bytes; ver docs/formats.md
    int32_t ReverbSend, TrackingType, InnerRadiusReal, OuterRadiusReal, MaxVoices, Priority, Group;
    int16_t MaxReject, Action2, IgnoreAge, Ducker;
    int32_t DuckerLength, MasterVolume, MinDelay, MaxDelay;
    int16_t MultiSample, RandomPick, Shuffled, Loop, Polyphonic;
    int8_t Outdoors, PauseInNis;
    int32_t SampleCount;
};
static_assert(sizeof(SFXParameters) == 68);

struct SFXSamplePoolFiles {  // 28 bytes
    int32_t FileRef, PitchOffset, RandomPitchOffset, BaseVolume, RandomVolumeOffset, Pan, RandomPan;
};
static_assert(sizeof(SFXSamplePoolFiles) == 28);

struct SampleHeaderData {  // 36 bytes
    int32_t Flags;
    int32_t Address;  // no arquivo: deslocamento no .SBF; carregado: endereço na RAM de som
    int32_t Size, Frequency, RealSize, NumberOfChannels, BitsPerSample, psi_SampleHeader, LoopOffset;
};
static_assert(sizeof(SampleHeaderData) == 36);

struct SoundBankHeader {
    std::vector<uint8_t> shf;      // SampleHeaderBuffer
    int32_t SampleCount = 0;
    SampleHeaderData* SampleHeader = nullptr;
    std::vector<uint8_t> sfx;      // SFXBuffer
    int32_t SFXCount = 0;
    SFXHeader* SFXHeaderData = nullptr;
    int32_t Loaded = 0;
    int32_t HashCode = 0;
};

struct psiSFXSpecials {
    int32_t Volume3D, Pan1, Pan2, Pan3;
};

struct SFXItem {  // 704 bytes no IOP
    SFXItem* Next;
    SFXItem* Last;
    int32_t HashCode;
    int32_t Counter;
    int32_t StreamHandle;
    int32_t VoiceHandle[10];
    int32_t BaseVolume[10];
    int32_t Volume3D;
    int32_t Pitch[10];
    int32_t Pan[10];
    int32_t CuePos;
    int32_t CueSampleWaiting[50];
    SFXSamplePoolFiles* CuedSamples[50];
    SFXParameters* SFXData;
    int32_t SoundBankSlot;
    int32_t UpdatedCounter;
    SFXPoint3 RealPos;
    SFXPoint3 pos;
    SFXPoint3 vel;
    uint32_t Handle;  // o DYNAMICSOUNDS* do EE
    int32_t VolumeOverRide;
    int32_t OuterRadius;
    int32_t InnerRadius;
    int32_t DeleteMe;
    int32_t Paused;
    int32_t StartedWhenPaused;
    int32_t StartedWhenNisPlaying;
    int32_t FadeAndKill;
    int32_t FadeAndKillVolume;
    psiSFXSpecials Specials;
    int32_t KeyOnFrameDelay;
    int32_t FirstTransferComplete;
    int32_t IsStreamed;
};

struct psiSFX {
    int32_t Handle, InnerRadius, OuterRadius, Volume, Frequency, Is3d, Is3dTracked, IsStreamed, Pan;
    SFXPoint3* pos;
    SFXPoint3* vel;
    psiSFXSpecials* Specials;
    int32_t TrackingType;
};

struct ES_VoiceHandleDetails {
    int32_t IsStreamed;
    int32_t Handle;
    int32_t Status;
};

struct VoiceHandle {  // VoiceHandleData (0x14 bytes no IOP)
    int32_t locked;
    int32_t rear;      // tem a voz de trás (som 3D)
    int32_t front_voice;
    int32_t rear_voice;
    int32_t frequency;
};

// Os três canais de stream (StreamData, 1020 bytes no IOP): 0 a música, 1 e 2 as falas. Só os campos que o PC usa.
struct StreamData {
    int32_t IdNumber = 0;
    int32_t HashCode = 0;        // FileRef (fala, < 0) ou faixa (música)
    int32_t Status = 0;          // 0 livre, 1 esperando, 2 tocando, 10 acabou
    int32_t StreamHandle = 0;    // o buffer de reprodução (StreamPlaybackBuffers)
    int32_t Channels = 1;
    int32_t GamePausedMe = 0;
    int32_t BaseVolume = 100;
    int32_t MusicStream = 0;
    int32_t Frequency = 0;
    int32_t FinishedInitialise = 0;
    int32_t StartedPlaying = 0;
    int32_t AllowStartedPlaying = 0;
    int32_t KeyOn = 0;
    int32_t ReverbDepth = 0;
    int32_t Is3d = 0;
    int32_t ShutDownVolume = 100;
    std::vector<uint8_t> data;   // só no PC: a fala inteira
};

struct StreamPlaybackBuffer {
    int32_t channels;
    int32_t voice_handle[2];
    int32_t spu_addr[2];
    int32_t paused;
};

constexpr int kItems = 40;
constexpr int kBankSlots = 0x96;
constexpr int kEsVoiceHandles = 100;
constexpr int kSpuVoices = Spu2::kVoices;

// ---- globais (os nomes da .symtab do SFX.IRX) ----

Spu2 g_spu;
// O disco (só do port: a pasta, a imagem ISO, o leitor ou a cópia pessoal; nulo: nada carrega).
std::shared_ptr<const DiscSource> g_disc;

SoundBankHeader SoundBankData[kBankSlots];
int32_t SoundBankInfo[kBankSlots];
int32_t MusicTrackInfo[kBankSlots];
SFXItem SFXActive[kItems];
SFXItem* BaseSFXActive = nullptr;
SFXItem* BaseSFXFree = nullptr;
ES_VoiceHandleDetails ES_VoiceHandleData[kEsVoiceHandles];
VoiceHandle VoiceHandleData[kSpuVoices];
uint8_t VoiceLooping[kSpuVoices];
uint8_t VoiceLocked[kSpuVoices];
int32_t PS2_PausedSampleFrequency[kSpuVoices];
StreamData Streams[3];
// O .data do IRX: a música com dois canais, as falas com um (e a voz de trás).
StreamPlaybackBuffer StreamPlaybackBuffers[3] = {{2, {-1, -1}, {0, 0}, 0}, {1, {-1, -1}, {0, 0}, 0}, {1, {-1, -1}, {0, 0}, 0}};
std::vector<uint32_t> StreamLookup;  // STREAMS.LUT (4 palavras por fala)

// SFXActiveData (devolvido ao EE): {Count, 40 x {HashCode, Handle}, MusicLastJump[2], MusicSectionNowPlaying[2]}.
int32_t SFXActiveData[0x154 / 4];

SFXPoint3 ListenerPos, ListenerVel, ListenerNorm, ListenerDir, ListenerUp;
SFXPoint3 ListenerPositions[4];
constexpr SFXPoint3 ZeroPos = {0, 0, 0};
constexpr SFXPoint3 NormVector = {1000, 0, 0};
constexpr SFXPoint3 DirVector = {0, 1000, 0};
constexpr SFXPoint3 UpVector = {0, 0, 1000};
constexpr int32_t BonbUnderWaterSFX[23] = {335, 331, 332, 333, 334, 330, 279, 278, 277, 253, 1303, 137,
                                           1492, 1491, 136, 514, 10, 40, 33, 310, 311, 39, 9};
constexpr const char* kLanguageFolders[5] = {"ENGLISH", "FRENCH", "GERMAN", "SPANISH", "JAPANESE"};

int32_t ES_MuteMusic = 1;
int32_t ES_MuteSFX = 1;
int32_t SFXLanguageNumber = 0;
int32_t SFXModeNumber = 1;
int32_t SFXStartedCount = 0;
int32_t MusicMasterVolume = 70;
int32_t DuckerDuration = 1;
int32_t DuckerVolume = 100;
int32_t DuckerCurrentVolume = 100;
uint32_t DuckerStartTime = 0;
int32_t AllStreamsPaused = 0;
int32_t SfxMasterVolume = 100;
int32_t FadeVolume = 10000;
int32_t FadeMusicVolume = 10000;
int32_t FadeStatus = 0;
int32_t FadeRate = 30;
int32_t FadeMusic = 1;
int32_t SFXNewReverb = 0;
int32_t SFXReverb = 0;
int32_t SFXNewIndoorsVolume = 100;
int32_t SFXIndoorsVolume = 100;
int32_t SFXUnderWater = 0;
int32_t FadeStartTimer = 0;
uint32_t randomseed1 = 12341234;
uint32_t randomseed2 = 13123123;
int32_t SFXFileAccessSuspended = 0;
int32_t SFXFileAccessSuspendedToggle = 0;
int32_t SFXListenerCount = 1;
int32_t SFXKeyOnFrameDelay = 0;
int32_t SFXMode = 4;  // psiSFXSetMode: 0 mono (sem a fase invertida da voz de trás)
int32_t SoundRAMUsed = 0;
uint32_t PlayingCoreValue[2] = {0xffffffff, 0xffffffff};
uint32_t KeyOn[2], DelayKeyOn[2], KeyOff[2], LastKeyOn[2];
uint32_t Reverb[2];
int32_t NisPlaying = 0;
int32_t NisQuitten = 0;
int32_t SFXPaused = 0;
int32_t HasInitialised = 0;
int32_t HaveCalledOnceMxi = 0;
int32_t InitialiseStatus = 0;
int32_t MusicStartSection = 0;
char SFXLanguage[20] = "ENGLISH";
int32_t MusicJumpRequest = -1;    // gp-0x7f3c: o trecho pedido por SFXJumpToMusicMarker (-1 nenhum)
int32_t MusicJumpInstant = 0;     // gp-0x7f38: o pedido é imediato
int32_t LastMusicJumpHashCode = -1;
int32_t MusicLastJump = 0;          // SFXActiveData +0x144 (DAT_000160c4): o último trecho pedido ao canal
int32_t MusicSectionNowPlaying = 0; // SFXActiveData +0x14c (DAT_000160cc): o trecho tocando

// O relógio do driver (TimerThread soma 1 a cada 100 ms; psiSFXGetTimer devolve milissegundos). No PC: os VBlanks
// que o EE já mandou (audio_out conta), 6 por 100 ms.
uint32_t psi_timer_ticks() { return uint32_t(audio_out_vblanks() / 6); }

// ---- registro de depuração (só do port): NF_SFX_LOG=<arquivo> grava cada pedido e decisão do driver ----

FILE* sfx_log_file() {
    static FILE* f = [] {
        FILE* out = nullptr;
        std::string path;
        if (nf::get_env_narrow("NF_SFX_LOG", &path)) out = nf::fopen_narrow(path.c_str(), "w");
        return out;
    }();
    return f;
}

void sfx_log(const char* fmt, ...) {
    FILE* f = sfx_log_file();
    if (f == nullptr) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fflush(f);
}

// ---- arquivos ----

// Um arquivo do disco inteiro (caminho do disco, "PS2/SBINFO.SBI"); sem disco nada é achado.
bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    if (g_disc == nullptr) return false;
    return g_disc->read(path, out);
}

// "\PS2\" (SFXFilePath) no disco.
std::string sfx_file_path() { return "PS2"; }
std::string language_path() { return sfx_file_path() + "/" + SFXLanguage; }

// ---- PRNG ----

// [irx] 0x00002efc SFXrnd__Fi
int32_t SFXrnd(int32_t n) {
    if (n < 1) return 0;
    randomseed1 += randomseed2;
    randomseed2 += uint32_t(int32_t(randomseed1) >> 31) + randomseed1;
    return int32_t((randomseed1 >> 16) % uint32_t(n));
}

// [irx] 0x00002fb0 SFXfrnd__Fv
int32_t SFXfrnd() { return SFXrnd(0x401); }

// [irx] 0x00002ff0 SFXsfrnd__Fv
int32_t SFXsfrnd() { return SFXrnd(0x801) - 0x400; }

// ---- matemática inteira do 3D ----

// Somas de quadrados como no IOP (inteiros de 32 bits que dão a volta; em C++ o estouro com sinal seria indefinido).
int32_t wrap_sq3(int32_t a, int32_t b, int32_t c) {
    return int32_t(uint32_t(a) * uint32_t(a) + uint32_t(b) * uint32_t(b) + uint32_t(c) * uint32_t(c));
}

// [irx] 0x00010250 PS2_SquareRoot
// Newton com 10 passos a partir de 1 e divisão inteira de 64 bits (__divdi3): só converge para valores pequenos;
// acima de ~1e5 (31 m em decímetros) o resultado sai maior que a raiz. Preservado: o 3D e o corte por distância
// dependem dele.
uint32_t PS2_SquareRoot(uint32_t n) {
    if (n == 0) return 0;
    int64_t x = 1;
    for (int i = 0; i < 10; ++i) {
        const int64_t num = x * x - int64_t(int32_t(n));
        const int64_t den = x * 2;
        if (den == 0) break;  // não acontece (x cresce a partir de 1); evita a exceção da divisão
        x = x - num / den;
    }
    return uint32_t(x);
}

// [irx] 0x000103bc PS2_DotProduct__FP9SFXPoint3T0
int32_t PS2_DotProduct(const SFXPoint3* a, const SFXPoint3* b) { return (a->x * b->x + a->y * b->y + a->z * b->z) / 1000; }

// [irx] 0x00010470 PS2_NormaliseVector__FP9SFXPoint3
void PS2_NormaliseVector(SFXPoint3* v) {
    const int32_t len = int32_t(PS2_SquareRoot(uint32_t(wrap_sq3(v->x, v->y, v->z))));
    if (len != 0) {
        v->x = v->x * 1000 / len;
        v->y = v->y * 1000 / len;
        v->z = v->z * 1000 / len;
    }
}

// ---- vozes da SPU (a camada psi do driver) ----

// [irx] 0x0000e2fc PS2_GetSampleVolume__Fi
int32_t PS2_GetSampleVolume(int32_t v) { return std::clamp(v * 0x3fff / 100, 0, 0x3fff); }

int core_of(int voice) { return voice > 0x17 ? 1 : 0; }
uint32_t bit_of(int voice) { return 1u << ((voice > 0x17 ? voice - 0x18 : voice) & 0x1f); }

// [irx] 0x0000da1c PS2_CheckKeyOn2__Fi
bool PS2_CheckKeyOn2(int voice) {
    const int c = core_of(voice);
    const uint32_t b = bit_of(voice);
    if ((PlayingCoreValue[c] & b) == 0) return true;
    if ((KeyOn[c] & b) != 0) return true;
    if ((DelayKeyOn[c] & b) != 0) return true;
    if ((LastKeyOn[c] & b) != 0) return true;
    return VoiceLooping[voice] != 0;
}

// [irx] 0x0000dc68 PS2_CheckKeyOn__Fi
bool PS2_CheckKeyOn(int h) {
    if (VoiceHandleData[h].front_voice != -1 && PS2_CheckKeyOn2(VoiceHandleData[h].front_voice)) return true;
    return VoiceHandleData[h].rear_voice != -1 && PS2_CheckKeyOn2(VoiceHandleData[h].rear_voice);
}

// [irx] 0x0000d178 psiLockVoiceHandle2__Fi
void psiLockVoiceHandle2(int voice) { VoiceLocked[voice] = 1; }
// [irx] 0x0000d140 psiUnLockVoiceHandle2__Fi
void psiUnLockVoiceHandle2(int voice) { VoiceLocked[voice] = 0; }

// [irx] 0x0000d368 psiLockVoiceHandle
void psiLockVoiceHandle(int h) {
    VoiceHandleData[h].locked = 1;
    if (VoiceHandleData[h].front_voice != -1) psiLockVoiceHandle2(VoiceHandleData[h].front_voice);
    if (VoiceHandleData[h].rear_voice != -1) psiLockVoiceHandle2(VoiceHandleData[h].rear_voice);
}

// [irx] 0x0000d1f8 psiUnLockVoiceHandle
void psiUnLockVoiceHandle(int h) {
    VoiceHandleData[h].locked = 0;
    if (VoiceHandleData[h].front_voice != -1) psiUnLockVoiceHandle2(VoiceHandleData[h].front_voice);
    if (VoiceHandleData[h].rear_voice != -1) psiUnLockVoiceHandle2(VoiceHandleData[h].rear_voice);
    VoiceHandleData[h].front_voice = -1;
    VoiceHandleData[h].rear_voice = -1;
}

// [irx] 0x0000e420 psiRequestVoiceHandle2__FP16SampleHeaderDatai
// A primeira voz da SPU calada e destravada; o ADSR (0x000f, 0x1fc0) é o fixo da Spu2.
int psiRequestVoiceHandle2(const SampleHeaderData* hdr) {
    int v = 0;
    for (; v < kSpuVoices; ++v) {
        if (!PS2_CheckKeyOn2(v) && VoiceLocked[v] == 0) break;
    }
    if (v >= kSpuVoices) return -1;
    psiLockVoiceHandle2(v);
    g_spu.set_start(v, uint32_t(hdr->Address));
    VoiceLooping[v] = (hdr->Flags & 1) != 0 ? 1 : 0;
    return v;
}

// [irx] 0x0000e648 psiRequestVoiceHandle
// Divergência: sem alça livre o original segue com o índice 48 e escreve fora de VoiceHandleData; aqui devolve -1.
int psiRequestVoiceHandle(const SampleHeaderData* hdr, int32_t rear) {
    int h = 0;
    while (h < kSpuVoices && (PS2_CheckKeyOn(h) || VoiceHandleData[h].locked != 0)) ++h;
    if (h >= kSpuVoices) return -1;
    VoiceHandleData[h].front_voice = psiRequestVoiceHandle2(hdr);
    if (VoiceHandleData[h].front_voice == -1) return -1;
    psiLockVoiceHandle(h);
    VoiceHandleData[h].rear = rear;
    if (rear != 0) {
        VoiceHandleData[h].rear_voice = psiRequestVoiceHandle2(hdr);
        if (VoiceHandleData[h].rear_voice == -1) {
            psiUnLockVoiceHandle(h);
            return -1;
        }
    }
    return h;
}

// [irx] 0x0000e854 PS2_SampleSdSetSwitch__Fii
// Preservado: o key off limpa DelayKeyOn com "& -bit" (zera os bits abaixo da voz, não só o dela).
void PS2_SampleSdSetSwitch(int voice, int what) {
    const int c = core_of(voice);
    const uint32_t b = bit_of(voice);
    if (what == 0) {
        KeyOn[c] &= ~b;
        DelayKeyOn[c] &= uint32_t(-int32_t(b));
        KeyOff[c] |= b;
    }
    if (what == 1) KeyOn[c] |= b;
    if (what == 2) DelayKeyOn[c] |= b;
}

// [irx] 0x0000ebcc PS2_VoiceSetReverb__Fii
void PS2_VoiceSetReverb(int voice, int32_t on) {
    const int c = core_of(voice);
    if (on == 0) {
        Reverb[c] &= ~bit_of(voice);
    } else {
        Reverb[c] |= bit_of(voice);
    }
}

// [irx] 0x0000ece4 psiSampleKeyOn2__Fii
void psiSampleKeyOn2(int voice, int32_t reverb) {
    PS2_VoiceSetReverb(voice, reverb);
    PS2_SampleSdSetSwitch(voice, 1);
}

// [irx] 0x0000ed38 psiSampleKeyOnDelayed2__Fii
void psiSampleKeyOnDelayed2(int voice, int32_t reverb) {
    PS2_VoiceSetReverb(voice, reverb);
    PS2_SampleSdSetSwitch(voice, 2);
}

// [irx] 0x0000ed8c psiSampleKeyOn
void psiSampleKeyOn(int h, int32_t reverb) {
    if (VoiceHandleData[h].front_voice != -1) psiSampleKeyOn2(VoiceHandleData[h].front_voice, reverb);
    if (VoiceHandleData[h].rear_voice != -1) psiSampleKeyOnDelayed2(VoiceHandleData[h].rear_voice, reverb);
}

// [irx] 0x0000ea7c psiSampleKeyOff2__Fi
void psiSampleKeyOff2(int voice) {
    VoiceLooping[voice] = 0;
    PS2_SampleSdSetSwitch(voice, 0);
}

// [irx] 0x0000ead4 psiSampleKeyOff
void psiSampleKeyOff(int h) {
    if (VoiceHandleData[h].front_voice != -1) psiSampleKeyOff2(VoiceHandleData[h].front_voice);
    if (VoiceHandleData[h].rear_voice != -1) psiSampleKeyOff2(VoiceHandleData[h].rear_voice);
}

// [irx] 0x0000e1d4 psiIsSamplePlaying
bool psiIsSamplePlaying(int h) { return PS2_CheckKeyOn(h); }

// [irx] 0x0000ee90 psiSetSampleFrequency2__Fii
void psiSetSampleFrequency2(int voice, int32_t f) { g_spu.set_pitch(voice, uint16_t(f)); }

// [irx] 0x0000eeec psiSetSampleFrequency
void psiSetSampleFrequency(int h, int32_t f) {
    if (VoiceHandleData[h].front_voice != -1) psiSetSampleFrequency2(VoiceHandleData[h].front_voice, f);
    if (VoiceHandleData[h].rear_voice != -1) psiSetSampleFrequency2(VoiceHandleData[h].rear_voice, f);
    VoiceHandleData[h].frequency = f;
}

// [irx] 0x0000f418 PS2_GetSampleFrequency__Fi
int32_t PS2_GetSampleFrequency(int h) { return VoiceHandleData[h].frequency; }

// [irx] 0x0000f144 psiSetSampleVolumeLeft2__Fii
void psiSetSampleVolumeLeft2(int voice, int32_t v) { g_spu.set_volume_left(voice, uint16_t(PS2_GetSampleVolume(v))); }
// [irx] 0x0000f238 psiSetSampleVolumeRight2__Fii
void psiSetSampleVolumeRight2(int voice, int32_t v) { g_spu.set_volume_right(voice, uint16_t(PS2_GetSampleVolume(v))); }

// [irx] 0x0000f1a4 psiSetSampleVolumeLeft
void psiSetSampleVolumeLeft(int h, int32_t v) {
    if (VoiceHandleData[h].front_voice != -1) psiSetSampleVolumeLeft2(VoiceHandleData[h].front_voice, v);
}

// [irx] 0x0000f2a4 psiSetSampleVolumeRight
void psiSetSampleVolumeRight(int h, int32_t v) {
    if (VoiceHandleData[h].front_voice != -1) psiSetSampleVolumeRight2(VoiceHandleData[h].front_voice, v);
}

// [irx] 0x0000f384 psiSetSampleVolume
void psiSetSampleVolume(int h, int32_t v) {
    if (VoiceHandleData[h].front_voice != -1) {
        psiSetSampleVolumeLeft2(VoiceHandleData[h].front_voice, v);
        psiSetSampleVolumeRight2(VoiceHandleData[h].front_voice, v);
    }
}

// [irx] 0x0000f018 psiSetSampleVolumeRear__Fii
// A voz de trás: L = v, R = 0x7fff - v (a mesma amplitude com a fase invertida) fora do modo mono.
void psiSetSampleVolumeRear(int h, int32_t v) {
    const int voice = VoiceHandleData[h].rear_voice;
    if (voice == -1) return;
    const uint16_t s = uint16_t(PS2_GetSampleVolume(v));
    g_spu.set_volume_left(voice, s);
    g_spu.set_volume_right(voice, SFXMode == 0 ? s : uint16_t(0x7fff - s));
}

// [irx] 0x0000f46c psiSamplePause
void psiSamplePause(int h) {
    PS2_PausedSampleFrequency[h] = PS2_GetSampleFrequency(h);
    psiSetSampleFrequency(h, 0);
}

// [irx] 0x0000f4cc psiSampleUnPause
// Preservado: as alças 0 e 1 (as da música) voltam sempre a 0xaaa (32 kHz).
void psiSampleUnPause(int h) {
    int32_t f = PS2_PausedSampleFrequency[h];
    if (h == 0 || h == 1) f = 0xaaa;
    psiSetSampleFrequency(h, f);
}

// [irx] 0x0000cba0 PS2_SetMasterVolume__Fi
void PS2_SetMasterVolume(int32_t v) { g_spu.set_master_volume(uint16_t(v * 0x3fff / 100)); }

// [irx] 0x0000c840 psiSetReverb
void psiSetReverb(int32_t r) { g_spu.set_effect_volume(uint16_t((r * 0x50 / 100) * 0x3fff / 100)); }

// [irx] 0x0000c944 PS2_SetupReverb__Fv
// Modo 4 (studio C) com profundidade 0 nos dois núcleos; o volume de efeito fica com psiSetReverb.
void PS2_SetupReverb() { psiSetReverb(0); }

// [irx] 0x0000caac psiSFXSetMode
void psiSFXSetMode(int32_t mode) { SFXMode = mode; }

// [irx] 0x0000d510 PS2_ResetKeyONKeyOff__Fv
void PS2_ResetKeyONKeyOff() {
    KeyOn[0] = KeyOn[1] = 0;
    DelayKeyOn[0] = DelayKeyOn[1] = 0;
    KeyOff[0] = KeyOff[1] = 0;
}

// [irx] 0x0000d544 PS2_SendKeyONKeyOff__Fv
// O fim de cada lote: lê o ENVX de todas as vozes (PlayingCoreValue: bit 1 = calada), manda os envios ao reverb e
// liga/desliga as vozes. A voz de trás liga 500 us depois (DelayThread(500)): 24 amostras de 48 kHz.
void PS2_SendKeyONKeyOff() {
    PlayingCoreValue[0] = PlayingCoreValue[1] = 0;
    for (int v = 0; v < kSpuVoices; ++v) {
        if (g_spu.envx(v) == 0) PlayingCoreValue[core_of(v)] |= bit_of(v);
    }
    for (int v = 0; v < kSpuVoices; ++v) g_spu.set_reverb_send(v, (Reverb[core_of(v)] & bit_of(v)) != 0);
    for (int c = 0; c < 2; ++c) {
        if (SFXMode == 0) KeyOn[c] |= DelayKeyOn[c];
        DelayKeyOn[c] &= ~KeyOff[c];
        KeyOn[c] &= ~KeyOff[c];
        LastKeyOn[c] = KeyOn[c] | DelayKeyOn[c];
    }
    for (int v = 0; v < kSpuVoices; ++v) {
        if (KeyOn[core_of(v)] & bit_of(v)) g_spu.key_on(v);
        if (KeyOff[core_of(v)] & bit_of(v)) g_spu.key_off(v);
    }
    if (SFXMode != 0) {
        for (int v = 0; v < kSpuVoices; ++v) {
            if (DelayKeyOn[core_of(v)] & bit_of(v)) g_spu.key_on(v, 24);
        }
    }
}

// ---- streams (PC: a fala inteira na memória; ver sfx_iop.h) ----

// [irx] 0x00007c68 ES_IsStreamInitialised__FP10StreamData
bool ES_IsStreamInitialised(const StreamData* s) {
    if (s->Status == 0 || s->Status == 1 || s->Status == 10) return false;
    return s->FinishedInitialise != 0;
}

// As alças de voz de um buffer de stream (-1 antes de PS2_CreateStreamVoiceHandles: nada a fazer).
bool stream_ready(int h) {
    if (h < 0 || h > 2) return false;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) {
        if (StreamPlaybackBuffers[h].voice_handle[i] < 0) return false;
    }
    return true;
}

// [irx] 0x00013718 psiStreamPlay
void psiStreamPlay(int h, int32_t reverb) {
    if (!stream_ready(h)) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSampleKeyOn(StreamPlaybackBuffers[h].voice_handle[i], reverb);
}

// [irx] 0x000137e8 psiStreamStop
void psiStreamStop(int h) {
    if (!stream_ready(h)) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSampleKeyOff(StreamPlaybackBuffers[h].voice_handle[i]);
}

// [irx] 0x000138b0 psiStreamPause
void psiStreamPause(int h) {
    if (!stream_ready(h)) return;
    StreamPlaybackBuffers[h].paused = 1;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSamplePause(StreamPlaybackBuffers[h].voice_handle[i]);
}

// [irx] 0x000139a4 psiStreamUnPause
void psiStreamUnPause(int h) {
    if (!stream_ready(h)) return;
    StreamPlaybackBuffers[h].paused = 0;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSampleUnPause(StreamPlaybackBuffers[h].voice_handle[i]);
}

// [irx] 0x00013a94 psiSetStreamFrequency
void psiSetStreamFrequency(int h, int32_t f) {
    if (!stream_ready(h)) return;
    if (StreamPlaybackBuffers[h].paused != 0) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSetSampleFrequency(StreamPlaybackBuffers[h].voice_handle[i], f);
}

// [irx] 0x00013ba0 psiSetStreamVolumeLeft
void psiSetStreamVolumeLeft(int h, int32_t v) {
    if (!stream_ready(h)) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSetSampleVolumeLeft(StreamPlaybackBuffers[h].voice_handle[i], v);
}

// [irx] 0x00013c70 psiSetStreamVolumeRight
void psiSetStreamVolumeRight(int h, int32_t v) {
    if (!stream_ready(h)) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSetSampleVolumeRight(StreamPlaybackBuffers[h].voice_handle[i], v);
}

// [irx] 0x00013d40 psiSetStreamVolumeRear__Fii
void psiSetStreamVolumeRear(int h, int32_t v) {
    if (!stream_ready(h)) return;
    for (int i = 0; i < StreamPlaybackBuffers[h].channels; ++i) psiSetSampleVolumeRear(StreamPlaybackBuffers[h].voice_handle[i], v);
}

// [irx] 0x00013364 psiStreamSetVolume
void psiStreamSetVolume(int h, int32_t v) {
    if (!stream_ready(h)) return;
    const StreamPlaybackBuffer& b = StreamPlaybackBuffers[h];
    if (b.channels == 1) {
        psiSetSampleVolume(b.voice_handle[0], v);
    } else if (SFXMode == 0) {
        psiSetSampleVolume(b.voice_handle[0], v / 2);
        psiSetSampleVolume(b.voice_handle[1], v / 2);
    } else {
        psiSetSampleVolumeLeft(b.voice_handle[0], v);
        psiSetSampleVolumeLeft(b.voice_handle[1], 0);
        psiSetSampleVolumeRight(b.voice_handle[0], 0);
        psiSetSampleVolumeRight(b.voice_handle[1], v);
    }
}

void ES_UnPauseStream(StreamData* s);
void ES_MusicSetVolume();

// ---- a música (o canal 0: MFX_n.SMF e MFX_n.SSD) ----
//
// No PS2 o driver lê a faixa do CD em pedaços que terminam nos marcadores (UpdateMusicMarkers) e os copia para os
// dois buffers da RAM de som das vozes esquerda e direita (psiStreamMemCpy separa cada 0x100 bytes do .SSD em 0x80 da
// esquerda e 0x80 da direita). No PC a faixa é lida do disco aos poucos e cada voz pede os blocos à MusicStream
// (Spu2BlockSource), que segue a mesma linha do tempo de marcadores de UpdateMusicMarkers, unidade de 0x100 bytes por
// unidade:
//   - tipo 9 (fim) e 5 (pausa): a leitura para nesse marcador (o 5 espera um salto);
//   - um salto pedido (SFXJumpToMusicMarker) acontece no próximo marcador que não seja 7: a leitura passa ao marcador
//     inicial do trecho pedido (o MarkerPos dele); os trechos com IsInstant (ou o pedido imediato) saltam já;
//   - tipos 6 e 7 (laço): a leitura volta a LoopStart e o marcador atual passa a ser LoopMarkerCount.
// Os cortes caem em StreamAlign (múltiplos de 0x100), como no PS2. Divergências: o PS2 lê adiantado (até ~2 s em 8
// buffers), então um salto não imediato espera o primeiro marcador depois do que já foi lido; aqui, o primeiro depois
// do que já tocou. O estado "acabou de saltar" (HasJustJumped, que segura pedidos até passar o marcador seguinte) não
// existe: todo pedido novo vale.
class MusicStream : public Spu2BlockSource {
public:
    struct Marker {  // MusicMarkerData, 32 bytes
        int32_t Name, Pos, MType, Flags, Extra, LoopStart, MarkerCount, LoopMarkerCount;
    };
    struct Start {  // MusicMarkerStartData, 52 bytes
        Marker m;
        int32_t MarkerPos, IsInstant, InstantBuffer, State[2];
    };
    static_assert(sizeof(Marker) == 32 && sizeof(Start) == 52);

    bool open(const std::string& smf, const std::string& ssd) {
        close();
        std::vector<uint8_t> d;
        if (!load_file(smf, d) || d.size() < 20) return false;
        int32_t h[5];
        std::memcpy(h, d.data(), sizeof(h));
        base_volume_ = h[4];
        if (h[0] < 0 || h[1] < 0 || size_t(h[2]) + size_t(h[0]) * 52 > d.size() || size_t(h[3]) + size_t(h[1]) * 32 > d.size()) {
            return false;
        }
        starts_.resize(size_t(h[0]));
        markers_.resize(size_t(h[1]));
        if (!starts_.empty()) std::memcpy(starts_.data(), d.data() + h[2], starts_.size() * sizeof(Start));
        if (!markers_.empty()) std::memcpy(markers_.data(), d.data() + h[3], markers_.size() * sizeof(Marker));
        if (g_disc == nullptr) return false;
        ssd_ = g_disc->open(ssd);
        if (!ssd_.is_open()) return false;
        ssd_size_ = uint32_t(ssd_.size());
        open_ = true;
        return true;
    }
    void close() {
        ssd_.close();
        open_ = false;
        starts_.clear();
        markers_.clear();
        units_.clear();
        cache_.clear();
    }
    bool is_open() const { return open_; }
    int32_t base_volume() const { return base_volume_; }
    int32_t start_count() const { return int32_t(starts_.size()); }
    bool start_is_instant(int32_t section) const {
        return section >= 0 && section < int32_t(starts_.size()) && starts_[size_t(section)].IsInstant != 0;
    }
    int32_t voice_left = -1;   // as vozes da SPU que pedem os blocos (esquerda, direita)
    int32_t voice_right = -1;
    int32_t jump_to = -1;      // StreamJumpTo (+1000): salto pendente para o próximo marcador
    bool ended = false;

    // Começa no marcador inicial do trecho (SFXInitialiseStreamUpdate da música).
    void start(int32_t section) {
        units_.clear();
        base_unit_ = 0;
        consumed_[0] = consumed_[1] = 0;
        ended = false;
        jump_to = -1;
        go_to_section(section);
    }

    // Salto imediato (SFXCheckStreamJumpInstant): a próxima unidade já é do trecho novo.
    void jump_now(int32_t section) {
        go_to_section(section);
        MusicLastJump = section;
        MusicSectionNowPlaying = section;
        // As unidades já geradas e ainda não tocadas pelas duas vozes ficam (no máximo uma: 8 blocos).
    }

    const uint8_t* next_block(int voice) override {
        const int ch = voice == voice_right ? 1 : 0;
        const uint64_t b = consumed_[ch]++;
        const uint64_t unit = b / 8;
        while (base_unit_ + units_.size() <= unit) {
            const int64_t off = generate();
            if (off < 0) return nullptr;
            units_.push_back(uint32_t(off));
        }
        const uint32_t file = units_[size_t(unit - base_unit_)] + (ch != 0 ? 0x80u : 0u) + uint32_t(b % 8) * 16;
        const uint8_t* src = read(file);
        if (src == nullptr) return nullptr;
        std::memcpy(block_[ch], src, 16);
        block_[ch][1] = 0;  // o laço e o fim dos buffers do PS2 não valem aqui: quem decide são os marcadores
        const uint64_t done = std::min(consumed_[0], consumed_[1]) / 8;
        while (base_unit_ < done && !units_.empty()) {
            units_.pop_front();
            ++base_unit_;
        }
        return block_[ch];
    }

private:
    void go_to_section(int32_t section) {
        if (section < 0 || section >= int32_t(starts_.size())) {
            ended = true;
            return;
        }
        st_ = section;
        mk_ = starts_[size_t(section)].MarkerPos;
        const int32_t pos = mk_ >= 0 && mk_ < int32_t(markers_.size()) ? markers_[size_t(mk_)].Pos : starts_[size_t(section)].m.Pos;
        read_pos_ = align(pos);
    }
    static int32_t align(int32_t v) { return (v < 0 ? v + 0xff : v) >> 8 << 8; }  // StreamAlign

    // A próxima unidade de 0x100 bytes da linha do tempo (o deslocamento no .SSD) ou -1 (parou).
    int64_t generate() {
        for (int guard = 0; guard < 4096; ++guard) {
            if (ended) return -1;
            while (mk_ >= 0 && mk_ < int32_t(markers_.size()) && markers_[size_t(mk_)].Pos < read_pos_) ++mk_;
            if (mk_ >= 0 && mk_ < int32_t(markers_.size()) && markers_[size_t(mk_)].Pos < read_pos_ + 0x100) {
                const Marker& m = markers_[size_t(mk_)];
                if (m.MType == 9 || m.MType == 5) {
                    ended = true;
                    return -1;
                }
                if (jump_to >= 0 && m.MType != 7) {
                    const int32_t to = jump_to;
                    jump_to = -1;
                    go_to_section(to);
                    MusicLastJump = to;
                    MusicSectionNowPlaying = to;
                    continue;
                }
                if (m.MType == 6 || m.MType == 7) {
                    read_pos_ = align(m.LoopStart);
                    mk_ = m.LoopMarkerCount;
                    if (mk_ >= 0 && mk_ < int32_t(markers_.size())) st_ = markers_[size_t(mk_)].Name;
                    continue;
                }
                ++mk_;
                continue;
            }
            if (uint32_t(read_pos_) + 0x100 > ssd_size_) {
                ended = true;
                return -1;
            }
            const int32_t unit = read_pos_;
            read_pos_ += 0x100;
            return unit;
        }
        ended = true;  // marcadores em laço sem dados entre eles
        return -1;
    }

    const uint8_t* read(uint32_t off) {
        if (cache_.empty() || off < cache_off_ || off + 16 > cache_off_ + cache_.size()) {
            constexpr uint32_t kChunk = 0x10000;
            cache_off_ = off & ~(kChunk - 1);
            const uint32_t n = std::min(kChunk * 2, ssd_size_ - cache_off_);
            cache_.assign(n, 0);
            if (ssd_.read_at(cache_off_, cache_.data(), n) != n) {
                cache_.clear();
                return nullptr;
            }
        }
        return cache_.data() + (off - cache_off_);
    }

    bool open_ = false;
    std::vector<Start> starts_;
    std::vector<Marker> markers_;
    int32_t base_volume_ = 100;
    DiscFile ssd_;
    uint32_t ssd_size_ = 0;
    std::vector<uint8_t> cache_;
    uint32_t cache_off_ = 0;
    std::deque<uint32_t> units_;
    uint64_t base_unit_ = 0;
    uint64_t consumed_[2] = {0, 0};
    int32_t read_pos_ = 0;
    int32_t mk_ = 0;
    int32_t st_ = 0;
    uint8_t block_[2][16] = {};
};

MusicStream g_music;

// [irx] 0x00006288 FindMusicTrackNumber__Fi
int32_t FindMusicTrackNumber(int32_t hash) {
    for (int i = 0; i < kBankSlots; ++i) {
        if (MusicTrackInfo[i] == hash) return i + 1;
    }
    return -1;
}

// A música no canal 0 (a parte de SFXInitialiseStreamUpdate que vale para ela): a faixa do hash, os marcadores e o
// trecho inicial (MusicStartSection), as duas vozes ligadas à MusicStream, KeyOn já pedido.
int32_t music_initialise(StreamData& s, int32_t hash) {
    const int32_t track = FindMusicTrackNumber(hash);
    if (track < 0 || g_disc == nullptr) return -1;
    const std::string dir = sfx_file_path() + "/MUSIC/MFX_" + std::to_string(track >> 4);
    const std::string name = "MFX_" + std::to_string(track);
    if (!g_music.open(dir + "/" + name + ".SMF", dir + "/" + name + ".SSD")) return -1;
    sfx_log("musica 0x%x -> MFX_%d trecho %d (%d trechos)\n", hash, track, MusicStartSection, g_music.start_count());
    const StreamPlaybackBuffer& b = StreamPlaybackBuffers[0];
    if (b.voice_handle[0] < 0 || b.voice_handle[1] < 0) return -1;
    g_music.voice_left = VoiceHandleData[b.voice_handle[0]].front_voice;
    g_music.voice_right = VoiceHandleData[b.voice_handle[1]].front_voice;
    g_music.start(MusicStartSection);
    if (g_music.voice_left >= 0) g_spu.set_source(g_music.voice_left, &g_music);
    if (g_music.voice_right >= 0) g_spu.set_source(g_music.voice_right, &g_music);
    s.IdNumber = 0;
    s.HashCode = track;
    s.Status = 2;
    s.StreamHandle = 0;
    s.Channels = 2;
    s.Frequency = 32000;
    s.MusicStream = 1;
    s.BaseVolume = g_music.base_volume();
    s.ShutDownVolume = 100;
    s.Is3d = 0;
    s.FinishedInitialise = 1;
    s.AllowStartedPlaying = 1;
    s.StartedPlaying = 0;
    s.KeyOn = 1;
    s.ReverbDepth = 0;
    s.GamePausedMe = 0;
    return 0;
}

// [irx] 0x000075fc SFXJumpToMusicMarkerUpdate__Fv
// Um pedido novo de trecho: igual ao último pedido ou ao que toca, nada; senão vira o salto pendente
// (StreamJumpTo), imediato se o trecho tem IsInstant ou o pedido é imediato (SFXCheckStreamJumpInstant).
void SFXJumpToMusicMarkerUpdate() {
    StreamData& s = Streams[0];
    if (s.MusicStream == 0 || MusicJumpRequest == -1 || !g_music.is_open()) return;
    LastMusicJumpHashCode = MusicJumpRequest;
    if (MusicLastJump == MusicJumpRequest || MusicSectionNowPlaying == MusicJumpRequest) return;
    if (StreamPlaybackBuffers[0].paused != 0 && s.GamePausedMe == 0) {
        // Pausado por um marcador do tipo 5: o salto tira da pausa e é imediato.
        ES_UnPauseStream(&s);
    }
    if (MusicJumpRequest < 0 || MusicJumpRequest >= g_music.start_count()) {
        // Divergência: o IRX não confere o número e leria um trecho além da tabela do .SMF (o jogo pode pedir assim
        // um trecho de outra faixa: as funções UpdateMFX_* continuam rodando no menu principal, que toca a faixa dos
        // créditos); aqui o pedido é esquecido.
        sfx_log("musica salto para o trecho %d ignorado (%d trechos)\n", MusicJumpRequest, g_music.start_count());
        MusicJumpRequest = -1;
        return;
    }
    sfx_log("musica salto para o trecho %d (imediato %d, IsInstant %d)\n", MusicJumpRequest, MusicJumpInstant,
            int(g_music.start_is_instant(MusicJumpRequest)));
    if (g_music.start_is_instant(MusicJumpRequest) || MusicJumpInstant != 0) {
        g_music.jump_now(MusicJumpRequest);
        g_music.jump_to = -1;
    } else {
        g_music.jump_to = MusicJumpRequest;
        MusicLastJump = MusicJumpRequest;
    }
}

// [irx] 0x0000789c SFXStopStream__FP10StreamData
// O buffer da fala fica (as vozes ainda soltam por 2 amostras); um novo começo no mesmo canal o troca.
void SFXStopStream(StreamData* s) {
    if (!ES_IsStreamInitialised(s)) {
        if (s->Status == 3) s->Status = 10;
        return;
    }
    psiStreamUnPause(s->StreamHandle);
    psiStreamSetVolume(s->StreamHandle, 0);
    psiStreamStop(s->StreamHandle);
    s->Status = 10;
}

// [irx] 0x00007a28 ES_StopStream__Fi
void ES_StopStream(int slot) { SFXStopStream(&Streams[slot]); }
// [irx] 0x00007a7c ES_FindpsiStreamHandle__Fi
int32_t ES_FindpsiStreamHandle(int slot) { return Streams[slot].StreamHandle; }
// [irx] 0x00007ad0 ES_FindStreamShutdownVolume__Fi
int32_t ES_FindStreamShutdownVolume(int slot) { return Streams[slot].ShutDownVolume; }
// [irx] 0x00007b24 ES_HasStreamEnded__Fi
bool ES_HasStreamEnded(int slot) { return Streams[slot].Status == 10; }
// [irx] 0x00007b90 ES_IsStreamPlaying__Fi
bool ES_IsStreamPlaying(int slot) { return ES_IsStreamInitialised(&Streams[slot]); }
// [irx] 0x00007c04 ES_UnlockStream__Fi
void ES_UnlockStream(int slot) { Streams[slot].Status = 0; }

// [irx] 0x00007e84 ES_StreamKeyOn__Fii
// (no PC a fala nunca é roubada por outra durante a carga: BeenStolen é sempre 0)
void ES_StreamKeyOn(int slot, int32_t reverb) {
    Streams[slot].KeyOn = 1;
    Streams[slot].ReverbDepth = reverb;
}

// [irx] 0x00007e0c ES_PauseStream__FP10StreamData
void ES_PauseStream(StreamData* s) { psiStreamPause(s->StreamHandle); }
// [irx] 0x00007e48 ES_UnPauseStream__FP10StreamData
void ES_UnPauseStream(StreamData* s) { psiStreamUnPause(s->StreamHandle); }

// [irx] 0x00009030 SFXInitialiseStream
// No PS2 o canal entra numa fila (StreamWaitingIntialise) e SFXInitialiseStreamUpdate lê os marcadores e começa a
// trazer os dados do CD; aqui a fala inteira é lida na hora (STREAMS.BIN, entrada -1 - FileRef da LUT) e as duas
// vozes do canal passam a tocá-la. A música (o hash da faixa, canal 0) vai para a MusicStream (acima).
int32_t SFXInitialiseStream(int32_t file_ref, int32_t is3d) {
    int slot = file_ref < 0 ? 1 : 0;
    while (slot <= 2 && Streams[slot].Status != 0) ++slot;
    if (slot > 2) {
        sfx_log("  canais de stream ocupados: 1 estado %d fala %d, 2 estado %d fala %d\n", Streams[1].Status,
                -1 - Streams[1].HashCode, Streams[2].Status, -1 - Streams[2].HashCode);
        return -1;
    }
    // A música só no canal 0 (o EE sempre para a anterior antes: SFXStopMusic; o IRX aceitaria um canal de fala, de
    // uma voz só).
    if (file_ref >= 0) return slot == 0 && music_initialise(Streams[0], file_ref) == 0 ? 0 : -1;
    StreamData& s = Streams[slot];
    const uint32_t index = uint32_t(-1 - file_ref);
    if (index * 4 + 3 >= StreamLookup.size()) return -1;
    const uint32_t data_off = StreamLookup[index * 4 + 2];
    const uint32_t data_size = StreamLookup[index * 4 + 3];
    const uint32_t mark_off = StreamLookup[index * 4 + 0];
    if (g_disc == nullptr) return -1;
    const DiscFile f = g_disc->open(language_path() + "/STREAMS/STREAMS.BIN");
    if (!f.is_open()) return -1;
    int32_t marker_header[5] = {};
    f.read_at(mark_off, marker_header, sizeof(marker_header));
    g_spu.detach(s.data.data());  // as vozes ainda podem estar soltando a fala anterior
    s.data.assign(data_size, 0);
    if (f.read_at(data_off, s.data.data(), data_size) != data_size) return -1;
    s.IdNumber = slot;
    s.HashCode = file_ref;
    s.Status = 2;
    s.StreamHandle = slot;
    s.Channels = 1;
    s.Frequency = 0x5622;
    s.MusicStream = 0;
    s.BaseVolume = marker_header[4];
    s.ShutDownVolume = 100;
    s.Is3d = is3d;
    s.FinishedInitialise = 1;
    s.AllowStartedPlaying = 1;
    s.StartedPlaying = 0;
    s.KeyOn = 0;
    s.GamePausedMe = 0;
    const StreamPlaybackBuffer& b = StreamPlaybackBuffers[slot];
    for (int i = 0; i < b.channels; ++i) {
        if (b.voice_handle[i] < 0) continue;
        const VoiceHandle& vh = VoiceHandleData[b.voice_handle[i]];
        if (vh.front_voice != -1) g_spu.set_stream(vh.front_voice, s.data.data(), uint32_t(s.data.size()));
        if (vh.rear_voice != -1) g_spu.set_stream(vh.rear_voice, s.data.data(), uint32_t(s.data.size()));
    }
    ++g_sfx_iop_stats.speech;
    return slot;
}

// [irx] 0x0000a1a0 SFXUpdateStreams
// O PC só faz o que vale para a fala inteira na memória: liga as vozes quando o item pediu (KeyOn) e a fala está
// pronta, e para o canal quando a voz chega ao fim do ADPCM (o PS2 para no marcador de fim: "No Data Stop Mono
// Stream!").
void SFXUpdateStreams() {
    SFXJumpToMusicMarkerUpdate();
    ES_MusicSetVolume();
    for (int i = 0; i < 3; ++i) {
        StreamData& s = Streams[i];
        if (!ES_IsStreamInitialised(&s)) continue;
        if (!stream_ready(s.StreamHandle)) continue;
        if (s.StartedPlaying != 0) {
            const VoiceHandle& vh = VoiceHandleData[StreamPlaybackBuffers[s.StreamHandle].voice_handle[0]];
            if (vh.front_voice != -1 && g_spu.end_flag(vh.front_voice) && g_spu.envx(vh.front_voice) == 0 &&
                StreamPlaybackBuffers[s.StreamHandle].paused == 0) {
                SFXStopStream(&s);
                continue;
            }
        }
        if (s.AllowStartedPlaying != 0 && s.StartedPlaying == 0 && s.KeyOn != 0) {
            // ES_InitialStreamBuffer/psiStreamCreatePlaybackBuffer: o pitch do canal (32 kHz a música, 22050 Hz as falas).
            if (s.MusicStream != 0) psiSetStreamFrequency(s.StreamHandle, (s.Frequency << 12) / 48000);
            psiStreamPlay(s.StreamHandle, s.ReverbDepth);
            s.StartedPlaying = 1;
        }
    }
}

// [irx] 0x00007d10 SFXStopAllStreams
// Preservado: só o canal 0 (a música); as falas param com os seus itens.
void SFXStopAllStreams() {
    if (ES_IsStreamInitialised(&Streams[0])) SFXStopStream(&Streams[0]);
    Streams[0].Status = 0;
}

// [irx] 0x00012b74 PS2_CreateStreamVoiceHandles__Fv
// Reserva as vozes das streams: a música com duas alças (esquerda, direita, sem voz de trás), cada fala com uma
// alça de duas vozes (frente e trás). Os endereços na RAM de som (0x1e0000 + 0x3c00 por buffer) não são usados no
// PC (as vozes tocam o buffer da fala).
bool PS2_CreateStreamVoiceHandles() {
    int32_t addr = 0x1e0000;
    for (int s = 0; s < 3; ++s) {
        for (int c = 0; c < StreamPlaybackBuffers[s].channels; ++c) {
            SampleHeaderData hdr{};
            hdr.Flags = 1;
            hdr.Address = addr;
            hdr.Size = 0x3c00;
            hdr.Frequency = 0xeb3;
            hdr.RealSize = 0x3c00;
            hdr.NumberOfChannels = 1;
            hdr.BitsPerSample = 0x10;
            const int h = psiRequestVoiceHandle(&hdr, StreamPlaybackBuffers[s].channels == 1 ? 1 : 0);
            StreamPlaybackBuffers[s].voice_handle[c] = h;
            StreamPlaybackBuffers[s].spu_addr[c] = addr;
            if (h == -1) return false;
            addr += 0x3c00;
        }
    }
    return true;
}

// [irx] 0x0000ced0 psiInitialiseSoundRam
void psiInitialiseSoundRam() {
    PS2_SetMasterVolume(0);
    PlayingCoreValue[0] = PlayingCoreValue[1] = 0xffffffff;
    SoundRAMUsed = 0;
    for (int v = 0; v < kSpuVoices; ++v) psiSetSampleFrequency2(v, 0xeb3);
    for (int v = 0; v < kSpuVoices; ++v) g_spu.key_off(v);
    for (int i = 0; i < kSpuVoices; ++i) {
        VoiceLooping[i] = 0;
        VoiceLocked[i] = 0;
        VoiceHandleData[i] = VoiceHandle{0, 0, -1, -1, 0};
    }
    PS2_CreateStreamVoiceHandles();
    PS2_SetMasterVolume(100);
    PS2_SetupReverb();
}

// [irx] 0x00006338 SFXReleaseAudioStreamSystem__Fv
void SFXReleaseAudioStreamSystem() {
    if (g_music.voice_left >= 0) g_spu.set_source(g_music.voice_left, nullptr);
    if (g_music.voice_right >= 0) g_spu.set_source(g_music.voice_right, nullptr);
    g_music.close();
    for (StreamData& s : Streams) {
        g_spu.detach(s.data.data());
        s.data.clear();
        s.data.shrink_to_fit();
    }
}

// [irx] 0x000065d8 SFXInitialiseAudioStreamSystem
void SFXInitialiseAudioStreamSystem() {
    if (HaveCalledOnceMxi == 0) {
        std::vector<uint8_t> mxi;
        if (!load_file(sfx_file_path() + "/MFXINFO.MXI", mxi) || mxi.size() < sizeof(MusicTrackInfo)) return;
        std::memcpy(MusicTrackInfo, mxi.data(), sizeof(MusicTrackInfo));
        HaveCalledOnceMxi = 1;
    }
    std::vector<uint8_t> lut;
    StreamLookup.clear();
    if (load_file(language_path() + "/STREAMS/STREAMS.LUT", lut)) {
        // O driver copia 3000 entradas (a LUT do disco tem 1681).
        StreamLookup.assign(3000 * 4, 0);
        std::memcpy(StreamLookup.data(), lut.data(), std::min(lut.size(), StreamLookup.size() * 4));
    }
    AllStreamsPaused = 0;
    InitialiseStatus = 2;
    for (int i = 0; i < 3; ++i) {
        Streams[i].Status = 0;
        Streams[i].IdNumber = i;
        Streams[i].StreamHandle = i;
    }
}

// ---- alças de voz do driver (ES_*) ----

// [irx] 0x00000000 ES_InitialiseVoiceHandles__Fv
void ES_InitialiseVoiceHandles() {
    for (ES_VoiceHandleDetails& d : ES_VoiceHandleData) d.Status = 0;
}

// [irx] 0x00000088 ES_FindFreeVoiceHandle__Fv
int ES_FindFreeVoiceHandle() {
    for (int i = 0; i < kEsVoiceHandles; ++i) {
        if (ES_VoiceHandleData[i].Status == 0) {
            ES_VoiceHandleData[i].Status = 1;
            return i;
        }
    }
    return -1;
}

// [irx] 0x00000160 ES_psiFindVoiceHandle__Fi
int32_t ES_psiFindVoiceHandle(int h) {
    if (ES_VoiceHandleData[h].IsStreamed == 0) return ES_VoiceHandleData[h].Handle;
    return ES_FindpsiStreamHandle(ES_VoiceHandleData[h].Handle);
}

// [irx] 0x00000244 ES_IsVoiceHandleStreamed__Fi
int32_t ES_IsVoiceHandleStreamed(int h) { return ES_VoiceHandleData[h].IsStreamed; }

// [irx] 0x000002a8 ES_GetStreamShutdownVolume__Fi
int32_t ES_GetStreamShutdownVolume(int h) { return ES_FindStreamShutdownVolume(ES_VoiceHandleData[h].Handle); }

// [irx] 0x0000031c ES_RequestVoiceHandle__FP16SampleHeaderDataiii
int ES_RequestVoiceHandle(SampleHeaderData* headers, int32_t file_ref, int32_t is3d) {
    int h = -1;
    if (file_ref < 0) {
        const int32_t slot = SFXInitialiseStream(file_ref, is3d);
        if (slot != -1 && (h = ES_FindFreeVoiceHandle()) != -1) {
            ES_VoiceHandleData[h].IsStreamed = 1;
            ES_VoiceHandleData[h].Handle = slot;
        }
    } else {
        const int psi = psiRequestVoiceHandle(&headers[file_ref], is3d);
        if (psi != -1 && (h = ES_FindFreeVoiceHandle()) != -1) {
            ES_VoiceHandleData[h].IsStreamed = 0;
            ES_VoiceHandleData[h].Handle = psi;
        }
    }
    return h;
}

// [irx] 0x000004e4 ES_UnLockVoiceHandle__Fi
void ES_UnLockVoiceHandle(int h) {
    if (h == -1) return;
    if (ES_VoiceHandleData[h].IsStreamed == 0) {
        psiUnLockVoiceHandle(ES_VoiceHandleData[h].Handle);
    } else {
        ES_UnlockStream(ES_VoiceHandleData[h].Handle);
    }
    ES_VoiceHandleData[h].Status = 0;
}

// [irx] 0x000005d0 IsSamplePlaying__Fi
bool IsSamplePlaying(int h) {
    if (h == -1) return false;
    if (ES_VoiceHandleData[h].IsStreamed == 0) return psiIsSamplePlaying(ES_VoiceHandleData[h].Handle);
    return ES_IsStreamPlaying(ES_VoiceHandleData[h].Handle);
}

// [irx] 0x000006ac HasSampleEnded__FiP7SFXItem
bool HasSampleEnded(int h, const SFXItem* item) {
    if (h == -1) return true;
    if (item->KeyOnFrameDelay > 0) return false;
    if (ES_VoiceHandleData[h].IsStreamed == 0) return !psiIsSamplePlaying(ES_VoiceHandleData[h].Handle);
    return ES_HasStreamEnded(ES_VoiceHandleData[h].Handle);
}

// [irx] 0x000008a8 SampleKeyOn__Fii
void SampleKeyOn(int h, int32_t reverb) {
    if (h == -1) return;
    if (ES_VoiceHandleData[h].IsStreamed == 0) {
        psiSampleKeyOn(ES_VoiceHandleData[h].Handle, reverb);
    } else {
        ES_StreamKeyOn(ES_VoiceHandleData[h].Handle, reverb);
    }
}

// [irx] 0x00000978 SampleKeyOff__Fi
void SampleKeyOff(int h) {
    if (h == -1) return;
    if (ES_VoiceHandleData[h].IsStreamed == 0) {
        psiSampleKeyOff(ES_VoiceHandleData[h].Handle);
    } else {
        ES_StopStream(ES_VoiceHandleData[h].Handle);
    }
}

// [irx] 0x00000a3c SamplePause__Fi
void SamplePause(int h) {
    if (h == -1 || !IsSamplePlaying(h)) return;
    if (ES_VoiceHandleData[h].IsStreamed == 0) {
        psiSamplePause(ES_VoiceHandleData[h].Handle);
    } else {
        ES_PauseStream(&Streams[ES_VoiceHandleData[h].Handle]);
    }
}

// [irx] 0x00000b24 SampleUnPause__Fi
void SampleUnPause(int h) {
    if (h == -1 || !IsSamplePlaying(h)) return;
    if (ES_VoiceHandleData[h].IsStreamed == 0) {
        psiSampleUnPause(ES_VoiceHandleData[h].Handle);
    } else {
        ES_UnPauseStream(&Streams[ES_VoiceHandleData[h].Handle]);
    }
}

// ---- 3D e 2D (a camada psi) ----

// [irx] 0x0001060c PS2_SFXCalculate3D__FP6psiSFX
// Volume pela distância (em decímetros, ao quadrado) e a divisão entre esquerda, direita e trás pelos produtos
// escalares com o vetor da direita do ouvinte (ListenerNorm), o oposto e o de trás (-ListenerDir). A posição seguinte
// (pos + vel) é calculada e descartada: sem Doppler.
void PS2_SFXCalculate3D(psiSFX* p) {
    // ListenerNorm é a linha 0 da câmera, que aponta para a esquerda (no jogo -x é a direita): o produto com ela
    // dá o volume do canal esquerdo, o com o oposto o do direito.
    const SFXPoint3 norm = ListenerNorm;
    const SFXPoint3 neg_norm = {-ListenerNorm.x, -ListenerNorm.y, -ListenerNorm.z};
    const SFXPoint3 back = {-ListenerDir.x, -ListenerDir.y, -ListenerDir.z};
    const int32_t dx = p->pos->x - ListenerPos.x;
    const int32_t dy = p->pos->y - ListenerPos.y;
    const int32_t dz = p->pos->z - ListenerPos.z;
    int32_t d = int32_t(PS2_SquareRoot(uint32_t(wrap_sq3(dx / 100, dy / 100, dz / 100))));
    d += p->InnerRadius * -10;
    if (d < 0) d = 0;
    int32_t range = (p->OuterRadius - p->InnerRadius) * 10;
    if (range < 10) range = 10;
    int32_t v = 1000 - d * 1000 / range;
    if (v < 0) v = 0;
    v /= 10;
    if (v > 100) v = 100;
    const int32_t vol = v * v / 100;
    SFXPoint3 dir = {dx / 100, dy / 100, dz / 100};
    PS2_NormaliseVector(&dir);
    const int32_t dot_n = PS2_DotProduct(&norm, &dir);
    const int32_t dot_m = PS2_DotProduct(&neg_norm, &dir);
    const int32_t dot_b = PS2_DotProduct(&back, &dir);
    const int32_t rear = (dot_b + 1000) * vol / 2000;
    const int32_t front = 100 - rear;
    int32_t l = ((dot_n + 1000) * vol / 2000) * front / 100;
    int32_t r = ((dot_m + 1000) * vol / 2000) * front / 100;
    int32_t b = rear * 0x4b / 100;
    if (SFXMode == 0) {
        l = (l + r + b) / 3;
        r = l;
        b = l;
    }
    p->Specials->Pan1 = std::clamp(l, 0, 100);
    p->Specials->Pan2 = std::clamp(r, 0, 100);
    p->Specials->Pan3 = std::clamp(b, 0, 100);
}

// [irx] 0x00010eb8 PS2_SFXSetup3D__FP6psiSFX
void PS2_SFXSetup3D(const psiSFX* p) {
    const int32_t l = p->Specials->Pan1, r = p->Specials->Pan2, b = p->Specials->Pan3;
    if (p->TrackingType == 3) {
        if (p->IsStreamed == 0) {
            psiSetSampleFrequency(p->Handle, p->Frequency);
            psiSetSampleVolumeLeft(p->Handle, p->Volume / 2);
            psiSetSampleVolumeRight(p->Handle, p->Volume / 2);
            psiSetSampleVolumeRear(p->Handle, p->Volume * 0x1e / 100);
        } else {
            psiSetStreamFrequency(p->Handle, p->Frequency);
            psiSetStreamVolumeLeft(p->Handle, p->Volume / 2);
            psiSetStreamVolumeRight(p->Handle, p->Volume / 2);
            psiSetStreamVolumeRear(p->Handle, p->Volume * 0x1e / 100);
        }
    } else if (p->IsStreamed == 0) {
        psiSetSampleFrequency(p->Handle, p->Frequency);
        psiSetSampleVolumeLeft(p->Handle, l * p->Volume / 100);
        psiSetSampleVolumeRight(p->Handle, r * p->Volume / 100);
        psiSetSampleVolumeRear(p->Handle, b * p->Volume / 100);
    } else {
        psiSetStreamFrequency(p->Handle, p->Frequency);
        psiSetStreamVolumeLeft(p->Handle, l * p->Volume / 100);
        psiSetStreamVolumeRight(p->Handle, r * p->Volume / 100);
        psiSetStreamVolumeRear(p->Handle, b * p->Volume / 100);
    }
}

// [irx] 0x00011328 PS2_SFXSetup2D__FP6psiSFX
void PS2_SFXSetup2D(const psiSFX* p) {
    int32_t l = (-((p->Pan - 100) / 2) * p->Volume) / 100;
    int32_t r = (((p->Pan + 100) / 2) * p->Volume) / 100;
    l = std::clamp(l, 0, 100);
    r = std::clamp(r, 0, 100);
    if (SFXMode == 0) {
        l = (l + r) / 2;
        r = l;
    }
    if (p->IsStreamed == 0) {
        psiSetSampleVolumeLeft(p->Handle, l);
        psiSetSampleVolumeRight(p->Handle, r);
        psiSetSampleFrequency(p->Handle, p->Frequency);
    } else {
        psiSetStreamVolumeLeft(p->Handle, l);
        psiSetStreamVolumeRight(p->Handle, r);
        psiSetStreamFrequency(p->Handle, p->Frequency);
    }
}

// [irx] 0x00011580 psiInitialiseSound
void psiInitialiseSound(psiSFX* p) {
    if (p->Is3d == 0) {
        PS2_SFXSetup2D(p);
    } else {
        PS2_SFXCalculate3D(p);
        PS2_SFXSetup3D(p);
    }
}

// [irx] 0x000115fc psiUpdateSound
void psiUpdateSound(psiSFX* p) {
    if (p->Is3d == 0) {
        PS2_SFXSetup2D(p);
    } else {
        if (p->Is3dTracked != 0) PS2_SFXCalculate3D(p);
        PS2_SFXSetup3D(p);
    }
}

// ---- ambiente, fade, ducker ----

// [irx] 0x00000d68 SFXResetEnvironment__Fv
void SFXResetEnvironment() {
    SFXNewReverb = 0;
    SFXReverb = 0;
    psiSetReverb(SFXReverb);
    SFXNewIndoorsVolume = 100;
    SFXIndoorsVolume = 100;
    SFXUnderWater = 0;
}

// [irx] 0x00000db4 SFXSetEnvironment
void SFXSetEnvironment(int32_t reverb, int32_t outdoors) {
    int32_t r;
    if (reverb == -0x6768) {
        SFXUnderWater = 1;
        r = 0;
    } else {
        SFXUnderWater = 0;
        r = std::clamp(reverb, 0, 100);
    }
    SFXNewReverb = r;
    SFXNewIndoorsVolume = outdoors == 0 ? 100 : 0x32;
}

// [irx] 0x00000e60 SFXUpdateEnvironment__Fv
void SFXUpdateEnvironment() {
    SFXReverb += std::clamp(SFXNewReverb - SFXReverb, -4, 0x10);
    psiSetReverb(SFXReverb);
    SFXIndoorsVolume += std::clamp(SFXNewIndoorsVolume - SFXIndoorsVolume, -4, 4);
}

// [irx] 0x00000f50 ES_FadeReset__Fv
void ES_FadeReset() {
    FadeVolume = 10000;
    FadeMusicVolume = FadeVolume;
    FadeStatus = 0;
    FadeRate = 0x1e;
}

// [irx] 0x00014660 psiSFXGetTimer
int32_t psiSFXGetTimer() { return int32_t(psi_timer_ticks() * 100); }

// [irx] 0x00000f8c SFXFadeUpdate__Fv
void SFXFadeUpdate() {
    if (FadeStatus == 0) return;
    int32_t step = 10000 / (FadeStatus * FadeRate);
    if (step == 0) step = FadeStatus;
    FadeVolume += step;
    if (FadeStatus == -1) {
        FadeVolume = (psiSFXGetTimer() - FadeStartTimer) * -5 + 10000;
    } else {
        FadeVolume = (psiSFXGetTimer() - FadeStartTimer) * 5;
    }
    if (FadeVolume < 0) {
        FadeVolume = 0;
        FadeStatus = 0;
    }
    if (FadeVolume > 10000) {
        FadeVolume = 10000;
        FadeStatus = 0;
    }
    if (FadeMusic != 0) FadeMusicVolume = FadeVolume;
}

// [irx] 0x000010e0 SFXFadeDown
void SFXFadeDown(int32_t with_music) {
    if (FadeStatus != -1 && FadeVolume != 0) {
        FadeStatus = -1;
        FadeMusic = with_music;
        FadeStartTimer = psiSFXGetTimer();
    }
}

// [irx] 0x00001158 SFXFadeUp
void SFXFadeUp(int32_t with_music) {
    if (FadeStatus != 1 && FadeVolume != 10000) {
        FadeStatus = 1;
        FadeMusic = with_music;
        FadeStartTimer = psiSFXGetTimer();
    }
}

// [irx] 0x0000c228 ES_HasTimerPassed__FUii
bool ES_HasTimerPassed(uint32_t start, int32_t length) {
    const int32_t d = psiSFXGetTimer() - int32_t(start);
    if (d < -0x40000000) return false;
    return length < d;
}

// [irx] 0x00008218 ES_MusicSetVolume__Fv
void ES_MusicSetVolume() {
    if (!ES_IsStreamInitialised(&Streams[0])) return;
    int32_t v = (((MusicMasterVolume * DuckerCurrentVolume * Streams[0].BaseVolume) / 10000) * FadeMusicVolume / 10000) *
                Streams[0].ShutDownVolume / 100;
    psiStreamSetVolume(Streams[0].StreamHandle, v * v / 100);
}

// [irx] 0x000083c8 ES_StartDucker__Fii
void ES_StartDucker(int32_t volume, int32_t duration) {
    DuckerStartTime = uint32_t(psiSFXGetTimer());
    DuckerDuration = duration;
    DuckerVolume = volume;
}

// [irx] 0x00008418 ES_StopDucker__Fv
void ES_StopDucker() { DuckerDuration = 0; }

// [irx] 0x00008438 ES_UpdateDucker__Fv
void ES_UpdateDucker() {
    const bool passed = ES_HasTimerPassed(DuckerStartTime, DuckerDuration);
    int32_t step = (DuckerVolume - DuckerCurrentVolume) * 0x400;
    if (step != 0) {
        const int32_t sign = step < 0 ? -1 : 1;
        step = passed ? step / 0xf : step / 6;
        step >>= 10;
        if (step == 0) step = sign;
        DuckerCurrentVolume += step;
    }
    if (step != 0) ES_MusicSetVolume();
    if (passed) DuckerVolume = 100;
}

// ---- volume de um item ----

// [irx] 0x00001258 IsUnderWaterSFX__FP7SFXItem
bool IsUnderWaterSFX(const SFXItem* item) {
    for (int32_t id : BonbUnderWaterSFX) {
        if (item->HashCode == id) return true;
    }
    return false;
}

// [irx] 0x00001300 ES_CombineVolumes__FiP7SFXItem
int32_t ES_CombineVolumes(int32_t base, const SFXItem* item) {
    int32_t v;
    if (item->HashCode == 0x1d7 || item->HashCode == 0x470) {
        v = base * MusicMasterVolume * ES_MuteMusic / 100;
    } else {
        v = ((base * SfxMasterVolume * ES_MuteSFX) / 100) * item->VolumeOverRide / 100;
        if (item->SFXData->Outdoors != 0) v = v * SFXIndoorsVolume / 100;
        if (SFXUnderWater != 0 && !IsUnderWaterSFX(item)) v /= 2;
    }
    v = ((((v * FadeVolume) / 10000) * item->FadeAndKillVolume) / 100) * item->SFXData->MasterVolume / 100;
    if (item->SFXData->Ducker == 0) v = v * (100 - (100 - DuckerCurrentVolume) / 3) / 100;
    return v * v / 100;
}

// ---- itens ----

// [irx] 0x00003034 InitialiseSFXList__Fv
void InitialiseSFXList() {
    BaseSFXActive = nullptr;
    BaseSFXFree = &SFXActive[0];
    for (int i = 0; i < kItems; ++i) {
        SFXActive[i].Next = i == kItems - 1 ? nullptr : &SFXActive[i + 1];
        SFXActive[i].Last = i < 1 ? nullptr : &SFXActive[i - 1];
    }
}

// [irx] 0x00003118 NewSFX__Fv
SFXItem* NewSFX() {
    SFXItem* item = BaseSFXFree;
    if (item == nullptr) return nullptr;
    if (BaseSFXActive != nullptr) BaseSFXActive->Last = item;
    BaseSFXFree = item->Next;
    item->Last = nullptr;
    item->Next = BaseSFXActive;
    BaseSFXActive = item;
    return item;
}

// [irx] 0x000031c4 DeleteSFX__FP7SFXItem
void DeleteSFX(SFXItem* item) {
    SFXItem* next = item->Next;
    if (item->Last == nullptr) {
        BaseSFXActive = item->Next;
    } else {
        item->Last->Next = item->Next;
    }
    if (next != nullptr) next->Last = item->Last;
    item->Next = BaseSFXFree;
    item->Last = nullptr;
    BaseSFXFree = item;
}

// [irx] 0x00001ea4 KillSFX__FP7SFXItem
void KillSFX(SFXItem* item) {
    for (int i = 0; i < 10; ++i) {
        if (item->VoiceHandle[i] != -1) {
            SampleKeyOff(item->VoiceHandle[i]);
            ES_UnLockVoiceHandle(item->VoiceHandle[i]);
        }
    }
    DeleteSFX(item);
}

// [irx] 0x00001f88 SFXRemove
void SFXRemove(uint32_t handle, int32_t hash) {
    SFXItem* next = BaseSFXActive;
    while (SFXItem* item = next) {
        next = item->Next;
        if ((item->Handle == handle || handle == 0) && (item->HashCode == hash || hash == -1)) KillSFX(item);
    }
}

// [irx] 0x00001648 ES_PauseSfx__FP7SFXItem
void ES_PauseSfx(SFXItem* item) {
    if (item->Paused != 0) return;
    for (int i = 0; i < 10; ++i) {
        if (item->VoiceHandle[i] != -1) SamplePause(item->VoiceHandle[i]);
    }
    item->Paused = 1;
}

// [irx] 0x00001794 ES_UnPauseSfx__FP7SFXItem
void ES_UnPauseSfx(SFXItem* item) {
    if (item->Paused == 0) return;
    for (int i = 0; i < 10; ++i) {
        if (item->VoiceHandle[i] != -1) SampleUnPause(item->VoiceHandle[i]);
    }
    item->Paused = 0;
}

// [irx] 0x0000171c SFXPause
void SFXPause() {
    SFXPaused = 1;
    for (SFXItem* item = BaseSFXActive; item != nullptr; item = item->Next) ES_PauseSfx(item);
}

// [irx] 0x00001868 SFXUnPause
void SFXUnPause() {
    SFXPaused = 0;
    for (SFXItem* item = BaseSFXActive; item != nullptr; item = item->Next) ES_UnPauseSfx(item);
}

// [irx] 0x000018dc ES_HandleNisPauseSfx__FP7SFXItem
void ES_HandleNisPauseSfx(SFXItem* item) {
    if (item->SFXData->PauseInNis == 0 || SFXPaused != 0) return;
    if (NisPlaying == 0 && SFXFileAccessSuspended == 0) {
        if (SFXFileAccessSuspendedToggle == 0) ES_UnPauseSfx(item);
    } else {
        ES_PauseSfx(item);
    }
}

// [irx] 0x00007fe0 SFXPauseAllStreams
void SFXPauseAllStreams() {
    AllStreamsPaused = 1;
    for (StreamData& s : Streams) {
        if (ES_IsStreamInitialised(&s) && s.StartedPlaying != 0) {
            ES_PauseStream(&s);
            s.GamePausedMe = 1;
        }
    }
}

// [irx] 0x00008100 SFXUnPauseAllStreams
void SFXUnPauseAllStreams() {
    AllStreamsPaused = 0;
    for (StreamData& s : Streams) {
        if (ES_IsStreamInitialised(&s) && s.GamePausedMe == 1) {
            ES_UnPauseStream(&s);
            s.GamePausedMe = 0;
        }
    }
}

// [irx] 0x00001994 SFXSetPositionAndVelocity
void SFXSetPositionAndVelocity(const SFXPoint3* pos, const SFXPoint3* vel, uint32_t handle, int32_t hash) {
    for (SFXItem* item = BaseSFXActive; item != nullptr; item = item->Next) {
        if ((item->Handle == handle || handle == 0) && (item->HashCode == hash || hash == -1)) {
            item->pos = *pos;
            item->RealPos = *pos;
            item->vel = *vel;
        }
    }
}

// [irx] 0x000057e8 ES_GetMyPos__FP7SFXItem
// TrackingType 3: um pouco à frente do ouvinte (raios 2 e 3); com mais de um ouvinte ou TrackingType 1: à distância
// do ouvinte mais próximo, na direção para a qual o ouvinte 0 olha.
void ES_GetMyPos(SFXItem* item) {
    if (item->SFXData->TrackingType == 3) {
        item->InnerRadius = 2;
        item->OuterRadius = 3;
        item->pos = {ListenerPos.x + ListenerDir.x, ListenerPos.y + ListenerDir.y, ListenerPos.z + ListenerDir.z};
        item->vel = ZeroPos;
    }
    if (SFXListenerCount > 1 || item->SFXData->TrackingType == 1) {
        int32_t best = 0;
        for (int i = 0; i < SFXListenerCount; ++i) {
            const int32_t dx = (ListenerPositions[i].x - item->RealPos.x) / 100;
            const int32_t dy = (ListenerPositions[i].y - item->RealPos.y) / 100;
            const int32_t dz = (ListenerPositions[i].z - item->RealPos.z) / 100;
            int32_t d = wrap_sq3(dx, dy, dz);
            if (i != 0 && best <= d) d = best;
            best = d;
        }
        const int32_t dist = int32_t(PS2_SquareRoot(uint32_t(best))) * 100;
        item->pos = {ListenerPos.x + dist * ListenerDir.x / 1000, ListenerPos.y + dist * ListenerDir.y / 1000,
                     ListenerPos.z + dist * ListenerDir.z / 1000};
    }
}

// [irx] 0x00005cb4 ES_GetDist__FP7SFXItem
// A distância (metros) ao ouvinte mais próximo.
int32_t ES_GetDist(const SFXItem* item) {
    int32_t best = 0;
    for (int i = 0; i < SFXListenerCount; ++i) {
        const int32_t dx = (ListenerPositions[i].x - item->RealPos.x) / 100;
        const int32_t dy = (ListenerPositions[i].y - item->RealPos.y) / 100;
        const int32_t dz = (ListenerPositions[i].z - item->RealPos.z) / 100;
        int32_t d = wrap_sq3(dx, dy, dz);
        if (i != 0 && best <= d) d = best;
        best = d;
    }
    return int32_t(PS2_SquareRoot(uint32_t(best))) / 10;
}

// [irx] 0x0000329c ES_FillpsiSFXData__FP6psiSFXP7SFXItemi
void ES_FillpsiSFXData(psiSFX* p, SFXItem* item, int i) {
    p->TrackingType = item->SFXData->TrackingType;
    p->Handle = ES_psiFindVoiceHandle(item->VoiceHandle[i]);
    p->InnerRadius = item->InnerRadius;
    p->OuterRadius = item->OuterRadius;
    p->Volume = ES_CombineVolumes(item->BaseVolume[i], item);
    p->Specials = &item->Specials;
    p->Frequency = item->Pitch[i];
    p->Pan = item->Pan[i];
    p->Is3d = p->Is3dTracked = item->SFXData->TrackingType != 0 ? 1 : 0;
    p->IsStreamed = ES_IsVoiceHandleStreamed(item->VoiceHandle[i]);
    if (p->IsStreamed != 0) p->Volume = ES_GetStreamShutdownVolume(item->VoiceHandle[i]) * p->Volume / 100;
    p->pos = &item->pos;
    p->vel = &item->vel;
}

// A amostra está nas definições do banco do item (SB_n.SFX, onde SFXSetup aponta CuedSamples)?
bool sample_in_bank(const SFXSamplePoolFiles* sample, int slot) {
    if (sample == nullptr || slot < 0 || slot >= kBankSlots) return false;
    const std::vector<uint8_t>& sfx = SoundBankData[slot].sfx;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(sample);
    return !sfx.empty() && sfx.data() <= p && p + sizeof(SFXSamplePoolFiles) <= sfx.data() + sfx.size();
}

// [irx] 0x00003524 StartSample__FP18SFXSamplePoolFilesiP7SFXItemi
// Divergência (guarda, g_sfx_iop_stats.guards): uma amostra fora das definições do banco do item não começa (a voz fica
// -1, como sem voz livre). Acontece com um item recusado por SFXSetup antes de preencher a fila (o trovão polifônico de
// 0700000a que rouba a si mesmo com as vozes cheias): ele fica na lista até o fim do SFXUpdate seguinte, que o trata
// como os outros, e num item nunca usado CuedSamples é nulo e CueSampleWaiting 0; num item reaproveitado a fila é a da
// vida anterior, talvez de um banco já descarregado. O IRX leria a amostra ali (o endereço 0 do IOP); o item morre no
// fim do mesmo update de qualquer jeito (DeleteMe), e uma amostra antiga do mesmo banco ainda começa, como no PS2.
bool StartSample(SFXSamplePoolFiles* sample, int slot, SFXItem* item, int i) {
    if (!sample_in_bank(sample, slot)) {
        ++g_sfx_iop_stats.guards;
        sfx_log("  guarda: amostra fora do banco (som 0x%x, canal %d)\n", item->HashCode, i);
        item->VoiceHandle[i] = -1;
        return false;
    }
    SampleHeaderData* headers = SoundBankData[slot].SampleHeader;
    item->VoiceHandle[i] = ES_RequestVoiceHandle(headers, sample->FileRef, item->SFXData->TrackingType != 0 ? 1 : 0);
    if (item->VoiceHandle[i] == -1) return false;
    int32_t base;
    if (sample->FileRef < 0) {
        base = 0x759;
        item->IsStreamed = 1;
    } else {
        base = headers[sample->FileRef].Frequency;
        item->IsStreamed = 0;
    }
    const int32_t pitch_offset = sample->PitchOffset;
    const int32_t rnd_pitch = SFXsfrnd();
    const int32_t random_pitch = sample->RandomPitchOffset;
    int32_t vol = sample->BaseVolume + ((SFXsfrnd() * sample->RandomVolumeOffset) >> 10);
    vol = std::clamp(vol, 0, 100);
    int32_t pan = sample->Pan + ((SFXsfrnd() * sample->RandomPan) >> 10);
    pan = std::clamp(pan, -100, 100);
    item->BaseVolume[i] = vol;
    item->Pitch[i] = base + (base * (pitch_offset + ((rnd_pitch * random_pitch) >> 10))) / 0x3000;
    item->Pan[i] = pan;
    item->pos = item->RealPos;
    ES_GetMyPos(item);
    psiSFX p{};
    ES_FillpsiSFXData(&p, item, i);
    psiInitialiseSound(&p);
    if (item->KeyOnFrameDelay == 0) SampleKeyOn(item->VoiceHandle[i], item->SFXData->ReverbSend);
    return true;
}

// [irx] 0x00003984 SetCueTimer__FP13SFXParameters
int32_t SetCueTimer(const SFXParameters* p) { return p->MinDelay + SFXrnd(p->MaxDelay - p->MinDelay); }

// [irx] 0x000041e0 SFXGetImportance__FP7SFXItem
int32_t SFXGetImportance(const SFXItem* item) { return item->SFXData->Priority; }

// [irx] 0x000040ec CountAllVoicesActive__FP7SFXItem
int32_t CountAllVoicesActive(const SFXItem* self) {
    int32_t n = 0;
    for (SFXItem* item = BaseSFXActive; item != nullptr; item = item->Next) {
        if (item == self) continue;
        for (int i = 0; i < 10; ++i) n += item->VoiceHandle[i] != -1 ? 1 : 0;
    }
    return n;
}

// [irx] 0x00004220 SFXSetup__FP7SFXItem
// Decide se o som começa (o limite de vozes, a prioridade dos sons 3D distantes, MaxVoices) e liga a primeira amostra
// (sorteada, a primeira de uma sequência ou todas as polifônicas).
bool SFXSetup(SFXItem* item) {
    SFXParameters* p = item->SFXData;
    SFXSamplePoolFiles* pool = reinterpret_cast<SFXSamplePoolFiles*>(reinterpret_cast<uint8_t*>(p) + sizeof(SFXParameters));
    for (int i = 0; i < 10; ++i) item->VoiceHandle[i] = -1;
    if (SFXStartedCount > 2000) SFXStartedCount = 2000;
    item->DeleteMe = 0;
    item->IsStreamed = 0;
    const int32_t budget = 0xf - SFXStartedCount / 400;
    int32_t active = CountAllVoicesActive(item);
    if (p->TrackingType == 2 && p->Priority < 100) {
        const int32_t dist = ES_GetDist(item);
        bool reject = item->OuterRadius < dist;
        if (!reject) {
            int32_t range = item->OuterRadius - item->InnerRadius;
            if (range < 1) range = 1;
            int32_t v = 100 - ((dist - item->InnerRadius) * 100) / range;
            v = std::clamp(v, 0, 100);
            if (v * p->Priority < (active * budget * 2000) / (budget * budget)) reject = true;
        }
        if (reject) {
            item->DeleteMe = 1;
            ++g_sfx_iop_stats.rejected;
            sfx_log("  recusado: distancia %d (externo %d) prioridade %d vozes %d limite %d\n", dist, item->OuterRadius,
                    p->Priority, active, budget);
            return false;
        }
    }
    if (budget < active) {
        active = CountAllVoicesActive(item);
        for (int k = 0; k < active - budget; ++k) {
            SFXItem* victim = nullptr;
            int32_t lowest = 10000;
            for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) {
                if (o->DeleteMe == 0 && o->IsStreamed == 0 && o->SFXData->TrackingType != 3) {
                    const int32_t imp = SFXGetImportance(o);
                    if (imp < lowest) {
                        victim = o;
                        lowest = imp;
                    }
                }
            }
            if (victim != nullptr) {
                victim->DeleteMe = 1;
                ++g_sfx_iop_stats.stolen;
            }
        }
        if (p->MaxReject != 0 || item->DeleteMe != 0) {
            item->DeleteMe = 1;
            ++g_sfx_iop_stats.rejected;
            sfx_log("  recusado: limite de vozes %d/%d\n", active, budget);
            return false;
        }
    }
    if (p->MaxVoices != 0) {
        int32_t same = 0;
        for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) {
            if (o != item && o->HashCode == item->HashCode) ++same;
        }
        if (p->MaxVoices <= same) {
            if (p->MaxReject != 0) {
                item->DeleteMe = 1;
                ++g_sfx_iop_stats.rejected;
                sfx_log("  recusado: MaxVoices %d\n", p->MaxVoices);
                return false;
            }
            SFXItem* oldest = nullptr;
            int32_t age = -1;
            for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) {
                if (o != item && o->HashCode == item->HashCode && age < o->Counter) {
                    age = o->Counter;
                    oldest = o;
                }
            }
            oldest->DeleteMe = 1;
            ++g_sfx_iop_stats.stolen;
        }
    }
    SFXStartedCount += 100;
    item->CuePos = 0;
    if (p->Ducker != 0 && (p->TrackingType == 0 || p->TrackingType == 3)) ES_StartDucker(100 - p->Ducker, p->DuckerLength);
    if (p->MultiSample == 0) {
        const int32_t pick = SFXrnd(p->SampleCount);
        if (!StartSample(&pool[pick], item->SoundBankSlot, item, 0)) {
            item->DeleteMe = 1;
            ++g_sfx_iop_stats.rejected;
            sfx_log("  recusado: sem voz (amostra %d)\n", pool[pick].FileRef);
            return false;
        }
        item->CueSampleWaiting[0] = SetCueTimer(p);
    } else if (p->Polyphonic == 0) {
        item->CuePos = p->SampleCount;
        item->CueSampleWaiting[0] = 0;
        if (p->Shuffled == 0) {
            SFXSamplePoolFiles* s = pool + (item->CuePos - 1);
            for (int i = 0; i < p->SampleCount; ++i) item->CuedSamples[i] = s--;
        } else {
            int32_t used[50] = {};
            int32_t remaining = p->SampleCount;
            for (int i = 0; i < p->SampleCount; ++i) {
                int32_t r = SFXrnd(remaining);
                int32_t k = 0;
                bool more;
                do {
                    if (r > 0) ++k;
                    while (used[k] != 0) ++k;
                    more = r > 0;
                    --r;
                } while (more);
                used[k] = 1;
                --remaining;
                item->CuedSamples[i] = pool + k;
            }
        }
        const int32_t cue = item->CuePos--;
        StartSample(item->CuedSamples[cue - 1], item->SoundBankSlot, item, 0);
        item->CueSampleWaiting[0] = SetCueTimer(p);
    } else {
        for (int i = 0; i < p->SampleCount; ++i) {
            item->CueSampleWaiting[i] = SetCueTimer(p);
            item->CuedSamples[i] = pool + i;
        }
    }
    return true;
}

// [irx] 0x00003aac SFXStart3D
// Preservado: o volume pedido pelo jogo (5º argumento) é sobrescrito com 100 logo no começo; VolumeOverRide é sempre
// 100.
int32_t SFXStart3D(int32_t hash, const SFXPoint3* pos, const SFXPoint3* vel, uint32_t handle, int32_t /*volume*/,
                   int32_t inner, int32_t outer) {
    const int32_t delay = SFXKeyOnFrameDelay;
    SFXKeyOnFrameDelay = 0;
    const int32_t volume = 100;
    ++g_sfx_iop_stats.requested;
    sfx_log("t %llu som 0x%x handle %08x pos %d,%d,%d raios %d/%d atraso %d ouvinte %d,%d,%d\n",
            static_cast<unsigned long long>(audio_out_vblanks()), hash, handle, pos != nullptr ? pos->x : 0,
            pos != nullptr ? pos->y : 0, pos != nullptr ? pos->z : 0, inner, outer, delay, ListenerPos.x, ListenerPos.y,
            ListenerPos.z);
    for (int slot = 0; slot < kBankSlots; ++slot) {
        SoundBankHeader& bank = SoundBankData[slot];
        if (bank.Loaded == 0) continue;
        for (int k = 0; k < bank.SFXCount; ++k) {
            if (bank.SFXHeaderData[k].HashCode != hash) continue;
            SFXParameters* p = reinterpret_cast<SFXParameters*>(bank.sfx.data() + bank.SFXHeaderData[k].Offset);
            const SFXSamplePoolFiles* pool =
                reinterpret_cast<const SFXSamplePoolFiles*>(reinterpret_cast<uint8_t*>(p) + sizeof(SFXParameters));
            if (p->SampleCount == 1) {
                if (pool[0].FileRef < 0) {
                    // Uma fala pré-carregada (SFXPreCache) com o mesmo som espera só o key on.
                    if (delay == 0) {
                        for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) {
                            if (o->HashCode == hash && o->KeyOnFrameDelay != 0) {
                                o->KeyOnFrameDelay = 0;
                                if (o->VoiceHandle[0] != -1) {
                                    o->StartedWhenNisPlaying = NisPlaying;
                                    o->StartedWhenPaused = SFXPaused;
                                    SampleKeyOn(o->VoiceHandle[0], o->SFXData->ReverbSend);
                                    return 0;
                                }
                                o->DeleteMe = 1;
                            }
                        }
                    } else {
                        for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) {
                            if (o->HashCode == hash && o->KeyOnFrameDelay != 0) return 0;
                        }
                    }
                } else if (delay != 0) {
                    return 0;
                }
            }
            SFXItem* item = NewSFX();
            if (item == nullptr) {
                ++g_sfx_iop_stats.rejected;
                return 2;
            }
            item->StartedWhenNisPlaying = NisPlaying;
            item->StartedWhenPaused = SFXPaused;
            item->FadeAndKillVolume = 100;
            item->FadeAndKill = 0;
            item->Handle = handle;
            if (pos != nullptr) {
                item->pos = *pos;
                item->RealPos = *pos;
            }
            if (vel != nullptr) item->vel = *vel;
            item->VolumeOverRide = volume == -1 ? 100 : volume;
            item->InnerRadius = inner == -1 ? p->InnerRadiusReal : inner;
            item->OuterRadius = outer == -1 ? p->OuterRadiusReal : outer;
            item->SFXData = p;
            item->HashCode = hash;
            item->SoundBankSlot = slot;
            item->Counter = 0;
            item->Paused = 0;
            item->KeyOnFrameDelay = delay;
            if (SFXSetup(item)) {
                ++g_sfx_iop_stats.started;
                return 0;
            }
            return 1;
        }
    }
    ++g_sfx_iop_stats.not_found;
    return 1;
}

// [irx] 0x00003a3c SFXStart
int32_t SFXStart(int32_t hash, uint32_t handle) { return SFXStart3D(hash, &ZeroPos, &ZeroPos, handle, -1, -1, -1); }

// [irx] 0x000039f4 SFXPreCache
void SFXPreCache(int32_t hash, int32_t frames) {
    SFXKeyOnFrameDelay = frames + 5;
    SFXStart(hash, 0);
}

// [irx] 0x00004e20 SFXUpdateEnding__FP7SFXItemPP7SFXItem
// No fim das amostras: sem Loop o item morre; com Loop espera o intervalo e recomeça (e, se recomeçou neste
// update, é processado de novo).
void SFXUpdateEnding(SFXItem* item, SFXItem** next) {
    if (item->SFXData->Loop == 0) {
        item->DeleteMe = 1;
        return;
    }
    const int32_t wait = item->CueSampleWaiting[0]--;
    if (wait < 1 && SFXSetup(item) && item->UpdatedCounter < 2) *next = item;
}

// [irx] 0x00004ee4 SFXUpdate
void SFXUpdate() {
    if (SFXStartedCount > 0) {
        SFXStartedCount = SFXStartedCount * 9 / 10;
        if (SFXStartedCount < 0) SFXStartedCount = 0;
    }
    if (SFXFileAccessSuspended != 0) SFXFileAccessSuspendedToggle = 1 - SFXFileAccessSuspendedToggle;
    SFXUpdateEnvironment();
    ES_UpdateDucker();
    SFXFadeUpdate();
    if (NisQuitten != 0) ES_StopDucker();
    for (SFXItem* item = BaseSFXActive; item != nullptr; item = item->Next) {
        if (NisQuitten != 0 && item->StartedWhenNisPlaying != 0) item->FadeAndKill = 1;
        item->UpdatedCounter = 0;
    }
    SFXUpdateStreams();
    SFXItem* item = BaseSFXActive;
    while (item != nullptr) {
        SFXItem* next = item->Next;
        if (item->Paused == 0) {
            if (item->FadeAndKill != 0) {
                item->FadeAndKillVolume -= 5;
                if (item->FadeAndKillVolume < 0) {
                    item->FadeAndKillVolume = 0;
                    item->DeleteMe = 1;
                }
            }
            ++item->UpdatedCounter;
            ++item->Counter;
            if (item->KeyOnFrameDelay != 0 && --item->KeyOnFrameDelay == 0) item->DeleteMe = 1;
            const SFXParameters* p = item->SFXData;
            if (p->MultiSample == 0 || p->Polyphonic == 0) {
                if (p->MultiSample == 0) {
                    if (HasSampleEnded(item->VoiceHandle[0], item)) {
                        ES_UnLockVoiceHandle(item->VoiceHandle[0]);
                        item->VoiceHandle[0] = -1;
                        SFXUpdateEnding(item, &next);
                    }
                } else if (HasSampleEnded(item->VoiceHandle[0], item)) {
                    ES_UnLockVoiceHandle(item->VoiceHandle[0]);
                    item->VoiceHandle[0] = -1;
                    if (item->CuePos == 0) {
                        SFXUpdateEnding(item, &next);
                    } else {
                        const int32_t wait = item->CueSampleWaiting[0]--;
                        if (wait < 1) {
                            const int32_t cue = item->CuePos--;
                            StartSample(item->CuedSamples[cue - 1], item->SoundBankSlot, item, 0);
                            item->CueSampleWaiting[0] = SetCueTimer(item->SFXData);
                        }
                    }
                }
            } else {
                bool all_ended = true;
                for (int i = 0; i < p->SampleCount; ++i) {
                    if (item->CueSampleWaiting[i] == 0) {
                        StartSample(item->CuedSamples[i], item->SoundBankSlot, item, i);
                        item->CueSampleWaiting[i] = -1;
                        all_ended = false;
                    } else if (item->CueSampleWaiting[i] < 1) {
                        if (!HasSampleEnded(item->VoiceHandle[i], item)) {
                            all_ended = false;
                        } else {
                            ES_UnLockVoiceHandle(item->VoiceHandle[i]);
                            item->VoiceHandle[i] = -1;
                        }
                    } else {
                        all_ended = false;
                        --item->CueSampleWaiting[i];
                    }
                }
                if (all_ended) SFXUpdateEnding(item, &next);
            }
            item->pos = item->RealPos;
            ES_GetMyPos(item);
            for (int i = 0; i < 10; ++i) {
                if (item->VoiceHandle[i] != -1) {
                    psiSFX ps{};
                    ES_FillpsiSFXData(&ps, item, i);
                    psiUpdateSound(&ps);
                }
            }
        }
        ES_HandleNisPauseSfx(item);
        item = next;
    }
    item = BaseSFXActive;
    while (item != nullptr) {
        SFXItem* next = item->Next;
        if (item->DeleteMe != 0) KillSFX(item);
        item = next;
    }
    SFXActiveData[0x51] = MusicLastJump;
    SFXActiveData[0x53] = MusicSectionNowPlaying;
    SFXActiveData[0] = 0;
    for (SFXItem* o = BaseSFXActive; o != nullptr && SFXActiveData[0] < kItems; o = o->Next) {
        SFXActiveData[1 + SFXActiveData[0] * 2] = o->HashCode;
        SFXActiveData[2 + SFXActiveData[0] * 2] = int32_t(o->Handle);
        ++SFXActiveData[0];
    }
}

// ---- ouvinte ----

// [irx] 0x00005efc SFXSetupListener
void SFXSetupListener(const SFXPoint3* pos, const SFXPoint3* vel, const SFXPoint3* dir, const SFXPoint3* up,
                      const SFXPoint3* norm, int32_t index) {
    if (SFXListenerCount == 1) {
        ListenerPos = *pos;
        ListenerVel = *vel;
        ListenerNorm = *norm;
        ListenerDir = *dir;
        ListenerUp = *up;
        ListenerPositions[0] = *pos;
    } else {
        ListenerNorm = NormVector;
        ListenerDir = DirVector;
        ListenerUp = UpVector;
        if (index >= 0 && index < 4) ListenerPositions[index] = *pos;
    }
}

// [irx] 0x00006200 SFXSetNumberOfListeners
void SFXSetNumberOfListeners(int32_t n) { SFXListenerCount = n; }

// ---- bancos ----

// [irx] 0x00002070 FindSoundBankSlot__Fi
int FindSoundBankSlot(int32_t hash) {
    for (int i = 0; i < kBankSlots; ++i) {
        if (SoundBankInfo[i] == hash) return i;
    }
    return -1;
}

// [irx] 0x00002164 SFXDeLoadSoundBank
void SFXDeLoadSoundBank(int32_t hash) {
    for (int slot = 0; slot < kBankSlots; ++slot) {
        if (SoundBankInfo[slot] != hash) continue;
        SoundBankHeader& bank = SoundBankData[slot];
        if (bank.Loaded == 0) return;
        SFXItem* next = BaseSFXActive;
        while (SFXItem* item = next) {
            next = item->Next;
            if (item->SoundBankSlot == slot) KillSFX(item);
        }
        bank.sfx.clear();
        bank.shf.clear();
        bank.SampleHeader = nullptr;
        bank.SFXHeaderData = nullptr;
        bank.Loaded = 0;
        return;
    }
}

// [irx] 0x0000243c SFXDeLoadAllSoundBanks__Fv
void SFXDeLoadAllSoundBanks() {
    for (int slot = 0; slot < kBankSlots; ++slot) {
        if (SoundBankData[slot].Loaded != 0) SFXDeLoadSoundBank(SoundBankData[slot].HashCode);
    }
}

// [irx] 0x0000de98 psiLoadSoundBank
// Copia o .SBF para a RAM de som a partir de 0x5010 + o já usado; devolve o endereço inicial (0: falhou).
int32_t psiLoadSoundBank(const std::string& path) {
    std::vector<uint8_t> sbf;
    if (!load_file(path, sbf)) return 0;
    const uint32_t start = 0x5010 + uint32_t(SoundRAMUsed);
    if (start + sbf.size() > Spu2::kRamBytes) return 0;
    std::memcpy(g_spu.ram.data() + start, sbf.data(), sbf.size());
    SoundRAMUsed = int32_t(start + sbf.size() - 0x5010);
    return int32_t(start);
}

// [irx] 0x000025a4 SFXLoadSoundBank
void SFXLoadSoundBank(int32_t hash) {
    const int slot = FindSoundBankSlot(hash);
    if (slot == -1) return;
    SoundBankHeader& bank = SoundBankData[slot];
    if (bank.Loaded != 0) return;
    if (g_disc == nullptr) return;
    const std::string dir = language_path() + "/SB_" + std::to_string(slot >> 3);
    const std::string name = "SB_" + std::to_string(slot);
    if (!load_file(dir + "/" + name + ".SFX", bank.sfx) || bank.sfx.size() < 4) return;
    bank.SFXCount = *reinterpret_cast<int32_t*>(bank.sfx.data());
    bank.SFXHeaderData = reinterpret_cast<SFXHeader*>(bank.sfx.data() + 4);
    if (!load_file(dir + "/" + name + ".SHF", bank.shf) || bank.shf.size() < 4) {
        bank.sfx.clear();
        return;
    }
    bank.SampleCount = *reinterpret_cast<int32_t*>(bank.shf.data());
    bank.SampleHeader = reinterpret_cast<SampleHeaderData*>(bank.shf.data() + 4);
    if (bank.SampleCount != 0) {
        int32_t addr = psiLoadSoundBank(dir + "/" + name + ".SBF");
        if (addr == 0) {
            bank.sfx.clear();
            bank.shf.clear();
            return;
        }
        for (int i = 0; i < bank.SampleCount; ++i) {
            bank.SampleHeader[i].Address = addr;
            addr += bank.SampleHeader[i].Size;
        }
    }
    bank.Loaded = 1;
    bank.HashCode = hash;
    ++g_sfx_iop_stats.banks_loaded;
    g_sfx_iop_stats.last_bank = uint32_t(hash);
}

// ---- língua, modo, volumes ----

// [irx] 0x00001cbc ES_SetLanguageFolder__Fv
void ES_SetLanguageFolder() {
    if (SFXLanguageNumber >= 0 && SFXLanguageNumber < 5) std::snprintf(SFXLanguage, sizeof(SFXLanguage), "%s", kLanguageFolders[SFXLanguageNumber]);
}

// [irx] 0x00001dfc SFXSetLanguage
void SFXSetLanguage(int32_t n) {
    SFXLanguageNumber = n;
    ES_SetLanguageFolder();
}

// [irx] 0x00001e38 SFXSetMode
void SFXSetMode(int32_t mode) {
    SFXModeNumber = mode;
    psiSFXSetMode(SFXModeNumber);
}

// [irx] 0x000011d0 SFXSetVolume
void SFXSetVolume(int32_t v) { SfxMasterVolume = std::clamp(v, 0, 100); }

// [irx] 0x00008360 SFXMusicSetVolume
void SFXMusicSetVolume(int32_t v) {
    MusicMasterVolume = v;
    ES_MusicSetVolume();
}

// [irx] 0x00001bcc SFXMuteMusic
void SFXMuteMusic() { ES_MuteMusic = 0; }
// [irx] 0x00001bec SFXMuteSFX
void SFXMuteSFX() { ES_MuteSFX = 0; }
// [irx] 0x00001c40 SFXUnMuteMusic
void SFXUnMuteMusic() { ES_MuteMusic = 1; }
// [irx] 0x00001c64 SFXUnMuteSFX
void SFXUnMuteSFX() { ES_MuteSFX = 1; }

// [irx] 0x00009f94 SFXSuspendFileAccess
void SFXSuspendFileAccess() { SFXFileAccessSuspended = 1; }
// [irx] 0x00009fc8 SFXUnSuspendFileAccess
void SFXUnSuspendFileAccess() { SFXFileAccessSuspended = 0; }

// [irx] 0x00006eb4 SFXStartMusic
void SFXStartMusic(int32_t track, int32_t marker) {
    MusicStartSection = marker;
    MusicLastJump = marker;
    MusicSectionNowPlaying = marker;
    SFXInitialiseStream(track, 0);
    MusicJumpRequest = -1;
    MusicJumpInstant = 0;
}

// [irx] 0x000079ec SFXStopMusic
void SFXStopMusic() { SFXStopAllStreams(); }

// [irx] 0x000075c0 SFXJumpToMusicMarker
void SFXJumpToMusicMarker(int32_t marker, int32_t instant) {
    MusicJumpRequest = marker;
    MusicJumpInstant = instant;
}

// [irx] 0x00002500 SFXReset
void SFXReset() {
    SFXRemove(0, -1);
    SFXDeLoadAllSoundBanks();
    SFXStopAllStreams();
    SFXReleaseAudioStreamSystem();
    psiInitialiseSoundRam();
    SFXInitialiseAudioStreamSystem();
    DuckerDuration = 1;
    DuckerVolume = 100;
    DuckerCurrentVolume = 100;
    DuckerStartTime = 0;
    SFXStartedCount = 0;
    SFXFileAccessSuspendedToggle = 0;
    ES_FadeReset();
    SFXPaused = 0;
    ES_InitialiseVoiceHandles();
    SFXUnMuteMusic();
    SFXUnMuteSFX();
    SFXResetEnvironment();
}

// [irx] 0x00002d24 SFXInitialise
void SFXInitialise() {
    if (HasInitialised != 0) return;
    ES_SetLanguageFolder();
    psiSFXSetMode(SFXModeNumber);
    HasInitialised = 1;
    psiInitialiseSoundRam();
    std::vector<uint8_t> sbi;
    if (!load_file(sfx_file_path() + "/SBINFO.SBI", sbi) || sbi.size() < sizeof(SoundBankInfo)) return;
    for (int i = 0; i < kBankSlots; ++i) SoundBankData[i].Loaded = 0;
    std::memcpy(SoundBankInfo, sbi.data(), sizeof(SoundBankInfo));
    InitialiseSFXList();
    SFXReset();
}

SFXPoint3 point_at(const int32_t* w) { return SFXPoint3{w[0], w[1], w[2]}; }

}  // namespace

Spu2& spu() { return g_spu; }

void sfx_iop_set_disc(std::shared_ptr<const DiscSource> disc) { g_disc = std::move(disc); }
const DiscSource* sfx_iop_disc() { return g_disc.get(); }
void sfx_iop_set_root(const std::filesystem::path& root) {
    g_disc = root.empty() ? nullptr : open_disc_folder(root.wstring());
}

// [irx] 0x0000f600 sifrpc_server__FUiPvi
// Os comandos do lote, até o 5 (fim: liga e desliga as vozes e devolve SFXActiveData) ou o 0x10. Os comandos 1,
// 0xf, 0x16 e 0x17 no começo do lote (os "lentos") não mexem nas máscaras de key on/off.
void sfx_iop_rpc(const int32_t* cmd, int32_t count, uint8_t* active_out) {
    if (count <= 0 || cmd == nullptr) return;
    const bool slow = cmd[0] == 1 || cmd[0] == 0xf || cmd[0] == 0x16 || cmd[0] == 0x17;
    if (!slow) PS2_ResetKeyONKeyOff();
    const int32_t* end = cmd + count;
    while (cmd < end) {
        switch (*cmd) {
            case 0: SFXInitialise(); break;
            case 1: SFXLoadSoundBank(cmd[1]); cmd += 1; break;
            case 2: SFXDeLoadSoundBank(cmd[1]); cmd += 1; break;
            case 3: SFXStart(cmd[1], uint32_t(cmd[2])); cmd += 2; break;
            case 4: SFXUpdate(); break;
            case 5:
                if (!slow) PS2_SendKeyONKeyOff();
                if (active_out != nullptr) std::memcpy(active_out, SFXActiveData, sizeof(SFXActiveData));
                return;
            case 6: SFXFadeDown(cmd[1]); cmd += 1; break;
            case 7: SFXFadeUp(cmd[1]); cmd += 1; break;
            case 8: SFXSetVolume(cmd[1]); cmd += 1; break;
            case 9: SFXPause(); break;
            case 10: SFXUnPause(); break;
            case 0xb: {
                const SFXPoint3 pos = point_at(cmd + 1), vel = point_at(cmd + 4);
                SFXSetPositionAndVelocity(&pos, &vel, uint32_t(cmd[7]), cmd[8]);
                cmd += 8;
                break;
            }
            case 0xc: SFXRemove(uint32_t(cmd[1]), cmd[2]); cmd += 2; break;
            case 0xd: {
                const SFXPoint3 pos = point_at(cmd + 2), vel = point_at(cmd + 5);
                SFXStart3D(cmd[1], &pos, &vel, uint32_t(cmd[8]), cmd[9], cmd[10], cmd[11]);
                cmd += 11;
                break;
            }
            case 0xe: {
                const SFXPoint3 pos = point_at(cmd + 1), vel = point_at(cmd + 4), dir = point_at(cmd + 7),
                                up = point_at(cmd + 10), norm = point_at(cmd + 13);
                SFXSetupListener(&pos, &vel, &dir, &up, &norm, cmd[16]);
                cmd += 16;
                break;
            }
            case 0xf: SFXReset(); break;
            case 0x10:
                if (active_out != nullptr) std::memcpy(active_out, SFXActiveData, sizeof(SFXActiveData));
                return;
            case 0x11: SFXStartMusic(cmd[1], cmd[2]); cmd += 2; break;
            case 0x12: SFXStopMusic(); break;
            case 0x13: SFXJumpToMusicMarker(cmd[1], cmd[2]); cmd += 2; break;
            case 0x14: cmd += 0x32; break;  // SFXSetFilePath: no PC o caminho é a raiz dos dados
            case 0x15: SFXMusicSetVolume(cmd[1]); cmd += 1; break;
            case 0x16: SFXPauseAllStreams(); break;
            case 0x17: SFXUnPauseAllStreams(); break;
            case 0x18: SFXSetNumberOfListeners(cmd[1]); cmd += 1; break;
            case 0x19: NisPlaying = cmd[1]; NisQuitten = cmd[2]; cmd += 2; break;
            case 0x1a: if (cmd[3] == 99) SFXSetMode(cmd[1]); cmd += 3; break;
            case 0x1b: SFXSetLanguage(cmd[1]); cmd += 1; break;
            case 0x1c: SFXSetMode(cmd[1]); cmd += 1; break;
            case 0x1d: SFXSetEnvironment(cmd[1], cmd[2]); cmd += 2; break;
            case 0x1e: SFXMuteMusic(); break;
            case 0x1f: SFXMuteSFX(); break;
            case 0x20: SFXMuteMusic(); SFXMuteSFX(); break;
            case 0x21: SFXUnMuteMusic(); break;
            case 0x22: SFXUnMuteSFX(); break;
            case 0x23: SFXUnMuteMusic(); SFXUnMuteSFX(); break;
            case 0x24: SFXSuspendFileAccess(); break;
            case 0x25: SFXUnSuspendFileAccess(); break;
            case 0x26: SFXPreCache(cmd[1], cmd[2]); cmd += 2; break;
            default:
                // "Illegal function No.%d from EE"
                if (!slow) PS2_SendKeyONKeyOff();
                if (active_out != nullptr) std::memcpy(active_out, SFXActiveData, sizeof(SFXActiveData));
                return;
        }
        ++cmd;
    }
}

void sfx_iop_debug_power_on() {
    for (SoundBankHeader& b : SoundBankData) b = SoundBankHeader{};
    std::memset(SoundBankInfo, 0xff, sizeof(SoundBankInfo));
    std::memset(MusicTrackInfo, 0xff, sizeof(MusicTrackInfo));
    std::memset(SFXActive, 0, sizeof(SFXActive));
    BaseSFXActive = BaseSFXFree = nullptr;
    std::memset(ES_VoiceHandleData, 0, sizeof(ES_VoiceHandleData));
    for (VoiceHandle& v : VoiceHandleData) v = VoiceHandle{0, 0, -1, -1, 0};
    std::memset(VoiceLooping, 0, sizeof(VoiceLooping));
    std::memset(VoiceLocked, 0, sizeof(VoiceLocked));
    std::memset(PS2_PausedSampleFrequency, 0, sizeof(PS2_PausedSampleFrequency));
    for (StreamData& s : Streams) s = StreamData{};
    StreamPlaybackBuffers[0] = StreamPlaybackBuffer{2, {-1, -1}, {0, 0}, 0};
    StreamPlaybackBuffers[1] = StreamPlaybackBuffer{1, {-1, -1}, {0, 0}, 0};
    StreamPlaybackBuffers[2] = StreamPlaybackBuffer{1, {-1, -1}, {0, 0}, 0};
    StreamLookup.clear();
    std::memset(SFXActiveData, 0, sizeof(SFXActiveData));
    ListenerPos = ListenerVel = ZeroPos;
    ListenerNorm = NormVector;
    ListenerDir = DirVector;
    ListenerUp = UpVector;
    for (SFXPoint3& p : ListenerPositions) p = ZeroPos;
    ES_MuteMusic = ES_MuteSFX = 1;
    SFXLanguageNumber = 0;
    SFXModeNumber = 1;
    SFXStartedCount = 0;
    MusicMasterVolume = 70;
    DuckerDuration = 1;
    DuckerVolume = DuckerCurrentVolume = 100;
    DuckerStartTime = 0;
    AllStreamsPaused = 0;
    SfxMasterVolume = 100;
    FadeVolume = FadeMusicVolume = 10000;
    FadeStatus = 0;
    FadeRate = 30;
    FadeMusic = 1;
    SFXNewReverb = SFXReverb = 0;
    SFXNewIndoorsVolume = SFXIndoorsVolume = 100;
    SFXUnderWater = 0;
    FadeStartTimer = 0;
    randomseed1 = 12341234;
    randomseed2 = 13123123;
    SFXFileAccessSuspended = SFXFileAccessSuspendedToggle = 0;
    SFXListenerCount = 1;
    SFXKeyOnFrameDelay = 0;
    SFXMode = 4;
    SoundRAMUsed = 0;
    PlayingCoreValue[0] = PlayingCoreValue[1] = 0xffffffff;
    KeyOn[0] = KeyOn[1] = DelayKeyOn[0] = DelayKeyOn[1] = KeyOff[0] = KeyOff[1] = LastKeyOn[0] = LastKeyOn[1] = 0;
    Reverb[0] = Reverb[1] = 0;
    NisPlaying = NisQuitten = SFXPaused = 0;
    HasInitialised = HaveCalledOnceMxi = InitialiseStatus = MusicStartSection = 0;
    std::snprintf(SFXLanguage, sizeof(SFXLanguage), "ENGLISH");
    MusicJumpRequest = -1;
    MusicJumpInstant = 0;
    LastMusicJumpHashCode = -1;
    MusicLastJump = MusicSectionNowPlaying = 0;
    g_music.close();
    g_music.voice_left = g_music.voice_right = -1;
    g_spu.reset();
    std::fill(g_spu.ram.begin(), g_spu.ram.end(), uint8_t(0));
    sfx_iop_reset_stats();
}

void sfx_iop_reset_stats() { g_sfx_iop_stats = SfxIopStats{}; }

uint32_t sfx_iop_square_root(uint32_t n) { return PS2_SquareRoot(n); }

void sfx_debug_log(const char* fmt, ...) {
    FILE* f = sfx_log_file();
    if (f == nullptr) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fflush(f);
}

SfxIopState sfx_iop_state() {
    SfxIopState st;
    for (SFXItem* o = BaseSFXActive; o != nullptr; o = o->Next) ++st.items;
    st.voices = g_spu.voices_sounding();
    for (int i = 1; i < 3; ++i) st.speech_playing += (Streams[i].Status == 2 && Streams[i].StartedPlaying != 0) ? 1 : 0;
    for (int i = 0; i < kBankSlots; ++i) {
        if (SoundBankData[i].Loaded != 0) st.bank_slot = i;
    }
    st.reverb = SFXReverb;
    st.paused = SFXPaused;
    st.nis_playing = NisPlaying;
    return st;
}

}  // namespace nf
