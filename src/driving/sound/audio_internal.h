#pragma once
// As classes da camada de som do jogo de carro (BondAudioPS2.lib do DRIVING.ELF), entre os .cpp de driving/sound/audio*.
// Portadas do MIPS; os campos levam o deslocamento do PS2 nos comentários (o port não guarda os objetos no layout do
// PS2: os ponteiros são nativos). docs/driving.md seção 17.

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "driving/sound/audio.h"

namespace drv {

// strcasecmp como comparação (os mapas do jogo: MapKey e AIndexCharPtr comparam sem diferenciar maiúsculas).
struct NoCaseLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

// ---- AMix (0x50): um volume com um mestre, uma transição e as somas do quadro ----
struct AMix {
    AMix* master = nullptr;     // +0x00
    float volume = 0;           // +0x04
    float saved = 0;            // +0x08 (o anterior: Revert)
    float preset = -1;          // +0x0c
    float initial = -1;         // +0x10 o do .ini (Reset)
    float target = 0;           // +0x14
    float step = 0;             // +0x18
    int32_t transition = 0;     // +0x1c
    int32_t priority = 0;       // +0x20
    int32_t count = 0;          // +0x24 os AVoice::Play deste quadro
    float sum = 0;              // +0x28
    float gain = 1;             // +0x2c
    char name[32] = {};         // +0x30
    int32_t refs = 0;           // (URefCounter)
};
AMix* AMix_Add(const char* name);
AMix* AMix_Get(const char* name);
void AMix_Remove(AMix* m);
float AMix_GetVolume(AMix* m);
void AMix_SetMaster(AMix* m, const char* name);
void AMix_SetTransition(float to, AMix* m, int32_t ticks);
void AMix_Reset(int32_t ticks);
float AMix_GetPresetVolume(AMix* m);
void AMix_Load(const std::vector<uint8_t>& ini);

// ---- AIndex: os nomes dos timbres de um banco (o .h ao lado do .bnk) ----
struct AIndex {
    std::map<int32_t, std::string> by_number;
    std::map<std::string, int32_t, NoCaseLess> by_name;
    int32_t Lookup(const char* name) const;     // -1 sem o nome
    const char* Lookup(int32_t number) const;   // "" sem o número
};

// ---- ABank (0x18) ----
struct ABank {
    std::vector<uint8_t> data;  // +0x00 o arquivo (depois do download, só o cabeçalho: SNDbankheadercopy)
    int32_t category = -1;      // +0x04
    int32_t snd_bank = -1;      // +0x08
    int32_t status = -1;        // +0x0c o resultado do SNDbankadd (-0x13 sem o arquivo)
    AIndex index;               // +0x10
    std::string name;
    int32_t refs = 0;
};
// ABank::fgBanks: as dez categorias (0 Generic, 1 Collisions, 2 Smackables, 3 CarWeapons, 4 Weapons, 5 Ambience,
// 6 Misc, 7 Actors, 8 g_common, 9 Engine); o banco vazio quando a categoria não tem banco.
ABank* ABank_Category(int32_t category);
ABank* ABank_Get(const char* name);

// ---- AVoice (100 bytes): um som em até três vistas (a tela do jogador é a vista 2) ----
struct AVoiceView {            // 0x20
    int32_t patch = -1;         // +0x00
    int32_t bank = -1;          // +0x04
    int32_t loop = 1;           // +0x08 0: toca uma vez (ao acabar marca +0x1c)
    int32_t handle = -1;        // +0x0c
    uint16_t pitch = 0x1000;    // +0x10 4.12
    uint16_t azimuth = 0;       // +0x12
    int16_t delay = -1;         // +0x14 ticks do TIMER (100 Hz) até o SNDBANK_play
    int8_t vol = 0;             // +0x16
    int8_t listed = 0;          // +0x17 o volume com que entrou na lista ativa (0: fora)
    uint8_t fx = 0;             // +0x18
    int32_t over = 0;           // +0x1c
};
struct AVoice {
    AMix* mix = nullptr;        // +0x00
    AVoiceView views[3];        // +0x04
    AVoice(AMix* m, int32_t bank, int32_t patch);
    ~AVoice();
    void Set(int32_t bank, int32_t patch);
};
// [driving] 0x002ddfd0 AVoice::Play(AView, float, float, float, float, float)
void AVoice_Play(float vol, float pitch, float azimuth, float delay, float fx, AVoice* v, int32_t view);
void AVoiceView_Stop(AVoiceView* v);

// ---- APath (0x1c): o caminho de um som até o ouvinte ----
struct AListener;
struct APath {
    float volume = 0;           // +0x00
    float pitch = 0;            // +0x04 o Doppler
    float azimuth = 0;          // +0x08 em voltas (-0,5..0,5)
    float facing = 0;           // +0x0c
    float delay = 0;            // +0x10 (a distância - 25) / 400: segundos do som
    float distance = 0;         // +0x14
    AListener* listener = nullptr;  // +0x18
};

// ---- AListener (0x80) ----
struct AListener {
    float rot[3][4] = {};       // +0x00 as linhas da rotação transposta (os eixos do ouvinte)
    float trans[4] = {};        // +0x30
    float pos[4] = {};          // +0x40
    float vel[3] = {};          // +0x50
    int32_t view = 2;           // +0x5c
    float car[4] = {};          // +0x60 a posição do carro do jogador (EAudioUpdate)
    float band_top = -100000.0f;  // +0x70 a faixa de altura do ambiente (WWorld::GetEnviroDesc)
    float band_height = 0;      // +0x74
};

// ---- ABaseSound (0xb0): tudo o que o ASoundManager percorre ----
class ABaseSound {
public:
    float pos[4] = {};          // +0x00
    float vel[4] = {};          // +0x10
    float dir[4] = {0, 0, 1, 0};  // +0x20 (a frente do carro: a linha 2 da orientação)
    float axis1[4] = {0, 0, 1, 0};  // +0x30 (a linha 0)
    float axis2[4] = {0, 0, 1, 0};  // +0x40 (a linha 1)
    float aim[4] = {};          // +0x50
    float min_distance = 3.0f;  // +0x5c
    float max_distance = 350.0f;  // +0x60
    float max_distance2 = 122500.0f;  // +0x64
    float falloff = 0;          // +0x68
    float volume = 1;           // +0x6c
    float pitch = 1;            // +0x70
    float last_volume[3] = {1, 1, 1};  // +0x74 o volume do caminho no quadro anterior, por vista
    int32_t disabled = 0;       // +0x80
    AMix* mix = nullptr;        // +0x84
    uint32_t views = 0;         // +0x88 a máscara das vistas
    char name[16] = {};         // +0x8c
    float fade = 0;             // +0x9c
    int32_t fade_t = 0;         // +0xa0
    int32_t fade_len = 0;       // +0xa4
    int32_t fading = 0;         // +0xa8

    ABaseSound(const char* mix_name, int32_t view);
    virtual ~ABaseSound();
    virtual void Play(APath& path) = 0;
    // ABaseSound::operator delete + o destrutor (a entrada +0xc da tabela com 3).
    void destroy();
};
// ASoundManager::fgSoundList (0x00355dc0; o operator new põe cada som novo no começo).
std::list<ABaseSound*>& sound_list();

// AOneShotSound (0x120): um timbre que toca e se apaga.
class AOneShotSound : public ABaseSound {
public:
    AVoice voice;               // +0xb0
    float fx = 0;               // +0x114
    float lifetime = 0;         // +0x118 segundos (0: até acabar)
    AOneShotSound(const char* mix_name, int32_t bank, int32_t patch, float volume);
    void Play(APath& path) override;
};

// [driving] 0x002f1f40 APath::Build(ABaseSound &)
void APath_Build(APath& path, const ABaseSound& s);

// ---- TIMER, random ----
// [driving] 0x00261038 TIMER_gettick: o relógio de 100 Hz do EA (a interrupção do contador 1).
int32_t TIMER_gettick();
// [driving] 0x002d0aa8 rrandom: [-0,5, 0,5) (o random de seis palavras; o jogo o quer em [0, 1): ver audio.cpp).
float rrandom();

// ---- AFX ----
void AFX_Init();
void AFX_SetMode(int32_t mode, int32_t slot, int32_t priority);
void AFX_Update();
int32_t AFX_IsAllVoices();
int32_t AFX_CurrentMode();

// ASoundManager::fgCount: os passos da simulação desde o último EAudioUpdate.
int32_t& sound_count();
// [driving] 0x002dda50 AVoice::PlayVoices: a lista ativa ao SND.
void AVoice_PlayVoices();
AudioStats& audio_stats_mut();

// ---- os fluxos (audio_stream.cpp) ----
void audio_streams_create(const std::string& stream_file);
void audio_streams_destroy();
void audio_streams_enviro(const uint8_t* desc, AListener& l);
void audio_streams_fader_update();

// ---- os veículos (audio_vehicle.cpp) ----
class AVehicleWind;
class AEngine;

class AVehicle : public ABaseSound {
public:
    // ASceneObj (+0xb0..)
    int32_t gun_active = 0;     // +0xb0
    int32_t gun_firing = 0;     // +0xb4
    int32_t unknown_b8 = 0;     // +0xb8
    int32_t unknown_bc = 0;     // +0xbc
    int32_t surface_type = 0;   // +0xc0
    int32_t unknown_c4 = 1;     // +0xc4
    // AVehicle
    int32_t vehicle_type = 0;   // +0xd0
    int32_t surface[4] = {1, 1, 1, 1};       // +0xd4 a superfície de cada roda (0 no ar)
    int32_t prev_surface[4] = {1, 1, 1, 1};  // +0xe4
    float compression[4] = {};  // +0xf4
    float front_slip = 0;       // +0x104 o deslizamento das rodas da frente (0..1)
    float back_slip = 0;        // +0x108 o das de trás
    float front_skid = 0;       // +0x10c o da frente suavizado
    float back_skid = 0;        // +0x110 o de trás suavizado (o AEngine o tira das rotações)
    float skid_multiple = 1;    // +0x114 SFX_SKIDMULTIPLE
    float gas = 0;              // +0x118
    float rpm = 0;              // +0x11c
    float rpm_wobble = 0;       // +0x120 a oscilação das rotações (±150)
    float wobble_dir = 1;       // +0x124
    float offroad_level = 0;    // +0x128
    float gravel_level = 0;     // +0x12c
    float cobble_level = 0;     // +0x130
    float cobble_dist = 0;      // +0x134 até o próximo solavanco dos paralelepípedos (150)
    float rail_dist = 0;        // +0x138 até o próximo do trilho (250)
    float speed = 0;            // +0x13c
    float car_speed = 0;        // +0x140
    float scrape[3] = {};       // +0x144 rocha, metal, areia
    float landing_force = 0;    // +0x150
    int32_t horn = 0;           // +0x154
    int32_t cobble_toggle = 0;  // +0x158
    int32_t rail_toggle = 0;    // +0x15c
    int32_t boost = 0;          // +0x160
    int32_t alive = 0;          // +0x164
    int32_t squeal = 0;         // +0x168 0: os timbres de derrapagem do asfalto, 1: os chiados
    int32_t cobble_patch[2] = {-1, -1};  // +0x16c
    int32_t rail_patch[2] = {-1, -1};    // +0x174
    int32_t scrape_timer[3] = {};        // +0x17c rocha, metal, areia
    int16_t unknown_188 = 0;    // +0x188
    int32_t gas_button = 0;     // +0x18c
    int32_t brake_button = 0;   // +0x190
    int32_t xy_trigger = 0;     // +0x194
    int32_t xy_release = 0;     // +0x198
    float steering = 0;         // +0x19c
    float steering_vertical = 0;  // +0x1a0
    ABank* collisions = nullptr;  // +0x1b0
    AVoice* road[3] = {};       // +0x1b4 OffRN, GravelRN, CobbleRN
    AVoice* skid[2] = {};       // +0x1c0 frente, trás
    AVoice* grass_skids[2] = {};  // +0x1c8
    AVoice* offroad_skids[2] = {};  // +0x1d0
    AVoice* ice_skids[2] = {};  // +0x1d8
    AVoice* scrapes[3] = {};    // +0x1e0 metal, rocha, areia
    AVehicleWind* wind = nullptr;  // +0x1f4
    void* horn_obj = nullptr;   // +0x1f8 AHorn (os carros do trânsito; não portado: nulo)
    AMix* mix_asphalt = nullptr;  // +0x1fc
    AMix* mix_offroad = nullptr;  // +0x200
    AMix* mix_skids = nullptr;  // +0x204
    AMix* mix_scrapes = nullptr;  // +0x208

    AVehicle(const char* mix_name, int32_t surface);
    ~AVehicle() override;
    void Play(APath& path) override;
    virtual void PlayLanding(float force, const float point[3], const float vel[3]);
    virtual void PlayCollision(float force, const float point[3], const float vel[3], int32_t surface);
    void Reset();
    void ActivateWind();
    int32_t GetAirborneWheels() const;
    void CalculateSpeed();
};

class APlayerVehicle : public AVehicle {
public:
    char engine_name[16] = {};  // +0x210
    AEngine* engine = nullptr;  // +0x220
    AVoice* road_noise = nullptr;  // +0x224
    int32_t road_surface[4] = {};  // +0x22c
    explicit APlayerVehicle(const char* engine);
    ~APlayerVehicle() override;
    void Play(APath& path) override;
};

// AEngine (0x20, a tabela virtual em +0x20 no PS2): as rotações e a carga suavizadas.
class AEngine {
public:
    float pitch = 0;            // +0x00 (rpm + 2000) / o máximo
    float load_amp = 0;         // +0x04 sqrt(carga)
    float idle_amp = 0;         // +0x08 sqrt(1 - carga)
    float start = 0;            // +0x0c passos desde que a carga passou de 0,2 (o "vaccuum")
    float rpm = 0;              // +0x10 suavizada
    float inv_max = 0;          // +0x14 1 / o máximo
    float load = 0;             // +0x18 suavizada
    AVehicle* vehicle = nullptr;  // +0x1c
    virtual ~AEngine() = default;
    virtual void Play(APath& path);
};

// ARaceEngine (0x54): os dez timbres do motor do carro do jogador.
class ARaceEngine : public AEngine {
public:
    AVoice* voices[10] = {};    // +0x24 dentro/fora: idle, lowload, midload, hiload, cruz
    float low = 0;              // +0x4c
    float high = 0;             // +0x50
    ARaceEngine();
    ~ARaceEngine() override {
        for (AVoice*& v : voices) delete v;
    }
    void Play(APath& path) override;
};

// AVehicleWind (0x24): o vento do carro.
class AVehicleWind {
public:
    AVehicle* vehicle;          // +0x00
    AMix* mix;                  // +0x04
    AVoice* voice = nullptr;    // +0x08
    float level = 0.4f;         // +0x0c
    float level_step = -0.1f;   // +0x10
    float pitch = 1.0f;         // +0x14
    float pitch_step = -0.1f;   // +0x18
    float volume = 1.0f;        // +0x1c
    float azimuth = 0.5f;       // +0x20
    explicit AVehicleWind(AVehicle* v);
    ~AVehicleWind() { delete voice; }
    void Play(float count, APath& path);
    void Stop(APath& path);
};

// ---- para o nf_driving_check (parte 13: a camada do jogo contra o original no interpretador) ----
struct AudioInternals {
    uint32_t* random = nullptr;           // as seis palavras do random (0x00343810)
    std::list<AVoiceView*>* active = nullptr;  // a lista ativa das vistas (0x00421c08)
    std::map<std::string, std::unique_ptr<AMix>, NoCaseLess>* mixes = nullptr;
    std::list<AMix*>* mix_list = nullptr;  // AMix::gList (as do .ini, na ordem: o AMix::Reset)
    ABank** categories = nullptr;         // ABank::fgBanks (10)
    APlayerVehicle* player = nullptr;     // o carro do jogador (nulo sem o audio_create_player_car)
    int32_t* cooldown = nullptr;          // 0x00355cd8: o descanso dos 12 timbres dos choques
    uint32_t* gate = nullptr;             // 0x00355d0c
};
AudioInternals audio_internals();
std::list<AVoiceView*>& voice_active_list();
void audio_vehicle_internals(AudioInternals& out);

}  // namespace drv
