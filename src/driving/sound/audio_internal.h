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

// ASound (0x120 bytes, audio_sound.cpp): um timbre que segue a posição que o dono escreve (o voo do míssil, os sons
// 'gs' da galeria).
class ASound : public ABaseSound {
public:
    AVoice voice;  // +0xb0
    // [driving] 0x002ed3d0 ASound::ASound(int banco, int timbre, char *mistura)
    ASound(const char* mix_name, int32_t bank, int32_t patch);
    // [driving] 0x002ed260 ASound::ASound(char *mistura): sem timbre, volume 0
    explicit ASound(const char* mix_name);
    ~ASound() override;
    void Play(APath& path) override;
};

// [driving] 0x002f1f40 APath::Build(ABaseSound &)
void APath_Build(APath& path, const ABaseSound& s);

// ---- TIMER, random ----
// [driving] 0x00261038 TIMER_gettick: o relógio de 100 Hz do EA (a interrupção do contador 1).
int32_t TIMER_gettick();
// [driving] 0x002d0aa8 rrandom: [-0,5, 0,5) (o random de seis palavras; o jogo o quer em [0, 1): ver audio.cpp).
float rrandom();
// [driving] 0x0025a5ac random (o da libc do jogo: as seis palavras; a buzina o usa com o %).
uint32_t random_word();

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

// AHorn (0x7c, fora da lista dos sons): a buzina dos carros do trânsito (audio_vehicle.cpp).
struct AHorn {
    float volume = 0;           // +0x00
    float pitch = 0;            // +0x04
    AMix* mix = nullptr;        // +0x08 "Horns"
    AVoice voice;               // +0x0c
    int32_t honks = 0;          // +0x70 as buzinadas que faltam
    int32_t ticks = 0;          // +0x74 os quadros desta buzinada
    int32_t wait = 0;           // +0x78 até a próxima
    AHorn(float vol, float pitch, const char* name);
    void Play(int32_t request, APath& path);
};

// ---- ASceneObj e os sons das armas e dos objetos (audio_scene.cpp, seção 31) ----

// ASceneObj (0xd0): um som do mundo que o dono alimenta (os veículos, os objetos derrubáveis, as torres). Os campos
// das armas (+0xb0..) vêm do PBondCar::Simulate; as entradas virtuais da tabela (o +0xac do PS2: o PlayLanding +0x2c,
// o PlayCollision +0x34, o IsStillActive +0x3c, o IsTracked +0x44) são as do port.
class ASceneObj : public ABaseSound {
public:
    int32_t gun_active = 0;     // +0xb0 (o servo da torre; a arma ligada)
    int32_t gun_firing = 0;     // +0xb4 atirando (o AGun)
    int32_t unknown_b8 = 0;     // +0xb8 a torre girando de lado (o ATurret)
    int32_t unknown_bc = 0;     // +0xbc a torre girando para cima ou para baixo
    int32_t aiming = 0;         // +0xc0 o zoom da mira mudou (o GHud::DrawCrossHairs grava 1/0)
    int32_t unknown_c4 = 1;     // +0xc4 o tiro do AGun em laço (0: um AOneShotSound por tiro)
    // [driving] 0x002f0480 ASceneObj::ASceneObj(char *)
    explicit ASceneObj(const char* mix_name) : ABaseSound(mix_name, 4) {}
    // [driving] 0x002f05c0 ASceneObj::~ASceneObj
    ~ASceneObj() override = default;
    // [driving] 0x002f05a8 ASceneObj::PlayLanding e 0x002f05b0 ASceneObj::PlayCollision: vazios.
    virtual void PlayLanding(float, const float*, const float*) {}
    virtual void PlayCollision(float, const float*, const float*, int32_t) {}
    // [driving] 0x002f0730 ASceneObj::IsStillActive e 0x002f0738 ASceneObj::IsTracked: 0.
    virtual bool IsStillActive() const { return false; }
    virtual bool IsTracked() const { return false; }
};

// AGun (0x34): o som de uma metralhadora (o laço sustentado enquanto atira; ou um AOneShotSound por tiro).
struct AGun {
    ASceneObj* owner = nullptr;  // +0x00
    ABank* bank = nullptr;      // +0x04 ABank::fgBanks[4] (Weapons)
    AMix* mix = nullptr;        // +0x08
    AVoice* voice = nullptr;    // +0x0c
    float fire_timer = 5.0f;    // +0x10 os passos desde o último tiro
    float sustain = 0.0f;       // +0x14 os passos que o laço ainda toca
    float unknown18 = 0.0f;     // +0x18 (não escrito pelo construtor)
    float fire_interval = 6.0f; // +0x1c
    float unknown20 = 0.0f;     // +0x20
    std::string fire_patch;     // +0x24 "SFX_HeliMachGun"
    std::string shell_patch;    // +0x28 "SFX_ShellCasing"
    std::string mix_name;       // +0x2c
    int32_t shells = 0;         // +0x30 os cartuchos (nada no jogo o liga)
    AGun(ASceneObj* owner, const char* mix_name);
    ~AGun();
    void SetFirePatch(const std::string& patch);
    void Play(float frames, int32_t firing, APath& path);
};

// ATurret (0x1c): os motores de uma torre (de lado, para cima e para baixo, o servo).
struct ATurret {
    AMix* mix = nullptr;          // +0x00 "Weapons Init"
    AVoice* servo_voice = nullptr;    // +0x04
    AVoice* sideways_voice = nullptr; // +0x08
    AVoice* updown_voice = nullptr;   // +0x0c
    int32_t servo = 0;            // +0x10
    int32_t sideways = 0;         // +0x14
    int32_t updown = 0;           // +0x18
    ATurret();
    ~ATurret();
    void Play(int32_t sideways_on, int32_t updown_on, int32_t servo_on, APath& path);
};

// AZoomObj (0x10): a respiração de Bond na luneta e as mixagens abaixadas enquanto ela está "na zona".
struct AZoomObj {
    AMix* mix = nullptr;          // +0x00 "Weapons Init"
    AVoice* voice = nullptr;      // +0x04 "SFX_breath" do banco Misc (ABank::fgBanks[6])
    ASceneObj* owner = nullptr;   // +0x08
    int32_t in_zone = 0;          // +0x0c
    explicit AZoomObj(ASceneObj* owner);
    ~AZoomObj();
    void InTheZone(int32_t on);
    void Play(APath& path);
};

// ACarAlarm (0x68): o alarme ("SFX_CarAlarm"). Nada no DRIVING.ELF chama o AVehicle::StartAlarm.
struct ACarAlarm {
    AVoice voice;                 // +0x00
    AMix* mix = nullptr;          // +0x64 "Horns"
    ACarAlarm();
    void Play(APath& path);
};

// AMenuSoundPriv (0x120): um timbre de uma vez só, sem posição (o máximo 0: o APath sem distância), que se apaga.
class AMenuSoundPriv : public ABaseSound {
public:
    AVoice voice;                 // +0xb0
    AMenuSoundPriv(int32_t bank, int32_t patch, const char* mix_name, int32_t view);
    void Play(APath& path) override;
};

class AVehicle : public ASceneObj {
public:
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
    AGun* gun = nullptr;        // +0x1a4 (ActivateGun)
    ATurret* turret = nullptr;  // +0x1a8 (ActivateTurret)
    AZoomObj* zoom = nullptr;   // +0x1ac (ActivateZoom)
    ABank* collisions = nullptr;  // +0x1b0
    AVoice* road[3] = {};       // +0x1b4 OffRN, GravelRN, CobbleRN
    AVoice* skid[2] = {};       // +0x1c0 frente, trás
    AVoice* grass_skids[2] = {};  // +0x1c8
    AVoice* offroad_skids[2] = {};  // +0x1d0
    AVoice* ice_skids[2] = {};  // +0x1d8
    AVoice* scrapes[3] = {};    // +0x1e0 metal, rocha, areia
    ACarAlarm* alarm = nullptr; // +0x1f0 (StartAlarm)
    AVehicleWind* wind = nullptr;  // +0x1f4
    AHorn* horn_obj = nullptr;  // +0x1f8 AHorn (os carros do trânsito)
    AMix* mix_asphalt = nullptr;  // +0x1fc
    AMix* mix_offroad = nullptr;  // +0x200
    AMix* mix_skids = nullptr;  // +0x204
    AMix* mix_scrapes = nullptr;  // +0x208

    AVehicle(const char* mix_name, int32_t surface);
    ~AVehicle() override;
    void Play(APath& path) override;
    void PlayLanding(float force, const float point[3], const float vel[3]) override;
    void PlayCollision(float force, const float point[3], const float vel[3], int32_t surface) override;
    // [driving] 0x002df650 AVehicle::ActivateGun(char *), 0x002df6a0 ActivateTurret, 0x002df6e8 ActivateZoom e
    // 0x002df780 StartAlarm: o objeto, se ainda não há.
    void ActivateGun(const char* mix_name);
    void ActivateTurret();
    void ActivateZoom();
    void StartAlarm();
    void Reset();
    void ActivateWind();
    int32_t GetAirborneWheels() const;
    void CalculateSpeed();
};

// ABasic (0xe0): um timbre do banco Generic tocado no lugar de quem o chama (a sirene dos carros de polícia).
class ABasic : public ABaseSound {
public:
    AVoice* voice = nullptr;    // +0xb0 criada no primeiro Play
    char patch_name[0x2c] = {}; // +0xb4
    ABasic(const char* patch, const char* mix_name);
    ~ABasic() override;
    void Play(APath& path) override;
    void Stop();
};

// ATrafficVehicle (0x270): o carro do trânsito e dos inimigos (as vozes LdEn e LdEx do motor e o ruído da rua).
class ATrafficVehicle : public AVehicle {
public:
    char engine_name[16] = {};  // +0x210
    AVoice* load_engine = nullptr;   // +0x220 SFX_<motor>LdEn
    AVoice* load_exhaust = nullptr;  // +0x224 SFX_<motor>LdEx
    AVoice* road_voice = nullptr;    // +0x228 SFX_TrafficRN (o tanque: SFX_tanktreads)
    ABasic* siren = nullptr;         // +0x22c
    float smooth_rpm = 0;       // +0x230
    float load = 0;             // +0x234
    float pitch_offset = 0;     // +0x238 o sorteio do InitAudioObject (0..0,2)
    int32_t unknown_23c = 0;    // +0x23c
    int32_t unknown_240 = 0;    // +0x240
    int32_t siren_on = 0;       // +0x244 a sirene pedida (AIGroundVehicle::SetAttackMode 13)
    ATrafficVehicle(const char* engine, const char* mix_name);
    // [driving] 0x002e6a20 ATrafficVehicle::IsTracked: o tipo 5 (as lagartas).
    bool IsTracked() const override { return vehicle_type == 5; }
    ~ATrafficVehicle() override;
    void Play(APath& path) override;
    void ActivateSiren(const char* patch);
    float CalculateRpm() const;
};

// O ATrafficVehicle de um P32 (nulo se apagado: audio_traffic.cpp).
AVehicle* audio_car_object(uint32_t h);
// [driving] 0x002df538 AVehicle::ChooseHorn(char *, float, float)
void AVehicle_ChooseHorn(float vol, float pitch, AVehicle& v, const char* name);

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

// ---- o registro das chamadas (a parte 27 do nf_driving_check, seção 31) ----

// Com audio_trace() não nulo, as vozes (criadas, tocadas, apagadas), as mixagens pedidas e as transições, os sons
// apagados e os AMenuSound deixam uma linha cada, na ordem; a conferência compara com as do original no interpretador.
struct AudioTrace {
    std::vector<std::string> log;
    std::map<const void*, int> ids;
    int next = 0;
    // A seco (o padrão): o AVoice::Play só registra (não mexe nas vistas) e as vozes não falam com o SND, como os
    // ganchos do original (o AVoice::Play original é conferido na parte 13).
    bool dry = true;
    int id(const void* p);     // o número da voz (na ordem de criação)
    int fresh(const void* p) { return ids[p] = next++; }  // uma voz nova (o endereço pode ser o de uma solta)
    void line(const char* fmt, ...);
};
AudioTrace*& audio_trace();
std::string audio_mix_name(const AMix* m);

// O "acabou nas vistas em uso" das vozes de uma vez só: a vista 2 acabou e, com a máscara das vistas em uso
// (DAT_00355cd0) sem o 4, também as vistas 0 e 1 que ela marca (no jogo de um jogador a máscara é 4).
bool AVoice_Done(const AVoice& v, uint32_t mask = 4);
// O que o AMenuSound::Trigger(int, int, ...) cria (o operator new e o construtor) depois da vista escolhida.
AMenuSoundPriv* AMenuSound_Make(int32_t bank, int32_t patch, const char* mix, int32_t view);

// APlayerHeli (0x230): o carro do jogador quando é o helicóptero (o "helicopter" de Paris na seção do helicóptero):
// três vozes "SFX_helichange" e três "SFX_heli" (esquerda, direita, centro dentro do helicóptero), misturadas pela
// velocidade; a metralhadora, a luneta (AZoomObj) e o vento.
class APlayerHeli : public AVehicle {
public:
    AVoice* change[3] = {};     // +0x210 "SFX_helichange"
    AVoice* rotor[3] = {};      // +0x21c "SFX_heli"
    float blend = 0;            // +0x228 0 parado, 1 andando
    APlayerHeli();
    ~APlayerHeli() override;
    void Play(APath& path) override;
    void PlayOwn(APath& path);  // o que vem depois do AVehicle::Play
};

// ASmackable (0xf0): o som de um objeto derrubável: o choque (três timbres pela força) e o rolar.
class ASmackable : public ASceneObj {
public:
    AVoice* collision_voice = nullptr;  // +0xd0
    AVoice* roll_voice = nullptr;       // +0xd4
    int32_t ids[3] = {};        // +0xd8 os timbres do choque (banco << 7 | timbre; -1 nenhum): fraco, médio, forte
    int32_t roll_frames = 0;    // +0xe4
    float roll_volume = 0;      // +0xe8
    int32_t detached = 0;       // +0xec o Smackable acabou e a voz do choque ainda toca
    ASmackable(int32_t lo, int32_t med, int32_t hi, int32_t roll, const char* mix_name);
    ~ASmackable() override;
    void PlayLanding(float force, const float* point, const float* vel) override;
    void PlayCollision(float force, const float* point, const float* vel, int32_t surface) override;
    bool IsStillActive() const override { return collision_voice != nullptr; }
    void Play(APath& path) override;
    void Detatch();
};
// [driving] 0x00355ea0 o choque forte que vem (alterna o médio e o forte).
int32_t& asmackable_next_hard();
// O ASmackable de um P32 (nulo se apagado: o audio_shutdown apaga todos os sons).
ASmackable* asmackable_from(uint32_t h);
// O objeto de som do dono de uma vaga de corpo rígido (o ECollision): o carro, os outros carros, os objetos derrubados.
ASceneObj* audio_object_of_slot(int32_t slot);


// AWorldSound (0x140): um som parado no mundo, que se repete com intervalos e vezes sorteados; com a voz de aviso, um
// segundo timbre quando algo vai passar perto do carro do jogador.
class AWorldSound : public ABaseSound {
public:
    AVoice voice;               // +0xb0
    float min_delay = 0;        // +0x114
    float max_delay = 0;        // +0x118
    float min_repeat = 0;       // +0x11c
    float max_repeat = 0;       // +0x120
    int32_t pan_mode = 0;       // +0x124 1: sem azimute nem Doppler
    int32_t added = 1;          // +0x128 (WSoundGroup: visto no último Open)
    float next_time = 0;        // +0x12c
    float repeats = 0;          // +0x130
    int32_t waiting = 1;        // +0x134
    AVoice* warning = nullptr;  // +0x138
    int32_t pass_frames = 0;    // +0x13c
    AWorldSound(int32_t bank, int32_t patch, const char* mix_name);
    ~AWorldSound() override;
    void Play(APath& path) override;
    void SetWarningVoice(int32_t id);
    void SetTiming(float min_delay, float max_delay);
    void SetRepeat(float lo, float hi) {
        min_repeat = lo;  // [driving] 0x002dd650 AWorldSound::SetRepeat
        max_repeat = hi;
    }
};
// WSound (0x150): o AWorldSound do mapa (+0x140: o RPathHandle do caminho que o pôs).
class WSound : public AWorldSound {
public:
    uint32_t path_handle = 0;   // +0x140 (p32 RPathHandle)
    WSound(int32_t bank, int32_t patch, const char* mix_name) : AWorldSound(bank, patch, mix_name) {}
};
// WSoundGroup (0x10): os sons do mapa por número (o map<int, WSound *> do PS2).
struct WSoundGroup {
    std::map<int32_t, WSound*> sounds;  // +0x0c
    void Start();
    WSound* Add(int32_t id, int32_t bank, int32_t patch);
    WSound* Add(int32_t id, const char* bank_and_patch);
    WSound* Add(int32_t id, int32_t packed) { return Add(id, packed >> 7, packed & 0x7f); }
    void End();
    void Update();
    void Clear();
};
// ASoundManager::fgTime (os segundos da simulação, 1/60 por passo).
float& sound_time();
// O WSoundGroup do WWorld (+0x38) e as vozes de aviso que o ~AWorldSound do PS2 deixa vivas (o port as solta no
// audio_shutdown).
WSoundGroup& world_sound_group();
std::vector<AVoice*>& world_leaked_voices();

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
    std::map<std::string, std::unique_ptr<ABank>, NoCaseLess>* banks = nullptr;  // URefCounter<ABank> (ABank::Get)
    ABank* empty = nullptr;               // ABank::fgEmpty (0x00356288)
};
AudioInternals audio_internals();
std::list<AVoiceView*>& voice_active_list();
void audio_vehicle_internals(AudioInternals& out);

}  // namespace drv
