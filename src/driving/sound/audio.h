#pragma once
// A camada de som do jogo de carro (o BondAudioPS2.lib do DRIVING.ELF: ASystem, ASoundManager, ABank, AMix, AVoice,
// AVehicle...) sobre o SND da EA (driving/sound/snd.h). Portado do MIPS do DRIVING.ELF; docs/driving.md seção 17.
//
// O que o port tem: o sistema (as opções do SND, o heap), os volumes (AMix e o data/audio/<pista>.ini), os bancos das
// categorias do data/audio/banks.ini com os nomes dos timbres (os .h), as vozes (AVoice: o caminho até o SND pelo
// PlayVoices), o ouvinte e o caminho de cada som (AListener, APath: distância, azimute, Doppler), o reverb do ambiente
// (AFX) e o carro do jogador (APlayerVehicle sobre AVehicle: o motor ARaceEngine/AEngine pelas rotações e pela carga,
// o ruído dos pneus por superfície, as derrapagens, os raspões, os solavancos dos paralelepípedos e trilhos, o vento,
// as aterrissagens e os choques), alimentado pelo fim do PBondCar::Simulate e pelo ECollision.
// O som dos personagens (ACharacter: os gritos e quedas dos capangas, seção 29) e o mixador do IOP (as vozes de render
// 0x100: driving/sound/iop_mix.h) também.
// Fica de fora (contado ou anotado em cada lugar): as falas e a música (AStream: os fluxos EA-XA pelo mixador do EE),
// AFader, as armas do carro (AGun,
// ATurret, AZoomObj), a buzina e o alarme, os outros veículos (ATrafficVehicle...), os objetos do mundo (WSoundGroup,
// ASmackable).

#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace drv {

// ---- ASystem (o dono do SND) ---------------------------------------------------------------------------------------

// [driving] 0x002e6c88 ASystem::SetOpts(ASystem::Mode): as opções do SND para o modo de saída do jogo (0 mono,
// 1 estéreo, 2..4 Pro Logic / 5.1; o jogo passa o "sound mode" do bloco da UberBond).
void ASystem_SetOpts(uint32_t mode);
// [driving] 0x002e6db8 ASystem::ASystem(void): o heap do SND (0x10000 bytes), SNDSYSI_init e o volume mestre 0x40.
// Devolve o resultado do SNDSYSI_init.
int32_t ASystem_Construct();
// [driving] 0x002e6ee0 ASystem::Shutdown (o destrutor: SNDSYS_restore).
void ASystem_Shutdown();
// A memória do heap do SND (nula antes do ASystem_Construct).
uint8_t* ASystem_heap();

// ---- o som do jogo no port (o que o nf_driving chama) ---------------------------------------------------------------

// Os arquivos do som da missão: o caminho do jogo ("data/audio/banks.ini", "data/audio/pars/g1_paris.bnk"...) para os
// bytes (vazio: sem o arquivo). O nf_driving lê do .VIV da missão.
using AudioFileLoader = std::function<std::vector<uint8_t>(const std::string& path)>;

class Bigf;
struct AudioInit {
    std::string track;            // a seção do banks.ini e o nome do .ini dos volumes ("paris_mis01")
    // Os fluxos (driving/sound/audio_stream.h): o nome do .SPE da missão sem a extensão ("MIS01EN"; a música é ele sem
    // os dois últimos caracteres, "MIS01") e quem abre os BIG do disco ("driving/MIS01.mus"). Vazio: sem fluxos.
    std::string stream_file;
    std::function<std::unique_ptr<Bigf>(const std::string& path)> big_open;
    uint32_t sound_mode = 1;      // UberBond +0xa28 (1: estéreo)
    float music_volume = 1.0f;    // UberBond +0x968 / 100
    float sfx_volume = 1.0f;      // UberBond +0x964 / 100
};

// [driving] 0x002eb7a8 ASoundManager::Init (a parte do port: os volumes, o AFX, o ASystem, os bancos). false se o
// SND não iniciou. Liga também o IOP (driving/sound/snddrv_iop.h) à SPU2 do port (driving/sound/snd_spu.h).
bool audio_init(const AudioInit& init, const AudioFileLoader& load);
// Desliga tudo (os sons, os bancos, o SND).
void audio_shutdown();
bool audio_ready();

// O carro do jogador: o objeto de som que o PBondCar::InitAudioObject cria (as chaves SFX_* da coleção de atributos).
struct AudioCarAttributes {
    std::string engine;           // SFX_ENGINE
    float volume = 1.0f;          // SFX_VOLUME
    float skid_multiple = 0.0f;   // SFX_SKIDMULTIPLE
    std::string type;             // o tipo do carro (o +0x2cc: "helicopter" dá o APlayerHeli; vazio, o APlayerVehicle)
};
// [driving] 0x00186878 PBondCar::InitAudioObject (o carro do jogador, classe 0: o APlayerHeli do "helicopter" e do
// "helivanquish", o APlayerVehicle dos outros; os do tanque, do submarino, da moto de neve, do ultraleve, da base do
// Fingo e do caminhão da selva não estão portados: um APlayerVehicle, contado em scene_audio_stats().guards).
void audio_create_player_car(const AudioCarAttributes& a);
// A parte do som do PBondCar::ChangeCarType (0x00185e50): o objeto antigo apagado e um APlayerVehicle do SFX_ENGINE novo,
// com o SFX_VOLUME e 15..500 m.
void audio_change_player_car(const std::string& engine, float volume);

// O que o fim do PBondCar::Simulate escreve no objeto de som a cada passo (driving/sound/audio_vehicle.cpp).
struct AudioCarFeed {
    float pos[3] = {};            // o corpo rígido +0x10
    float vel[3] = {};            // +0x20
    float orient[3][3] = {};      // as linhas da orientação (RigidBodyInfo +0x00/+0x10/+0x20)
    float aim[3] = {};            // RPlayerCamera::GetForwardAimVec4
    float rpm = 0;                // PBondCar::CalculateRPM
    float gas = 0;                // o controle (o acelerador)
    float front_slip = 0, back_slip = 0;  // a média do GetCarWheelSlip das rodas da frente e de trás
    int32_t surface[4] = {};      // a superfície de cada roda no chão (0 no ar)
    int32_t alive = 1;            // a vida acima de 0
    int32_t gun_active = 1, gun_firing = 0;
    float car_speed = 0;          // +0x288
    float control_gas = 0, control_brake = 0;  // +0x240, +0x244
    float landing = 0;            // +0x2ec
    float compression[4] = {};    // +0x210
    float gas_input = 0, brake_input = 0;  // +0x278, +0x27c
};
void audio_feed_player_car(const AudioCarFeed& f);

// Um choque (o ECollision): a força, o ponto, a velocidade e a superfície (o número da face) de um lado que é o carro
// do jogador; landing: o PlayLanding em vez do PlayCollision.
void audio_player_car_collision(float force, const float point[3], const float vel[3], int32_t surface, bool landing);

// O fim do PBondCar::Simulate (o nf_driving o liga ao bond_car_audio_hook): o carro em AudioCarFeed.
class BondCar;
void PBondCar_AudioFeed(BondCar& car, float rpm);
// O +0xb0 (a arma ligada) e o +0xb4 (atirando) que o fim do PBondCar::Simulate grava no objeto de som.
void PBondCar_GunFeed(BondCar& car, int32_t& active, int32_t& firing);
// [driving] 0x00149ed8 ECollision::~ECollision, a parte do som (o lado que é o carro do jogador, a vaga player_slot).
struct CollisionInfo;
void ECollision_Audio(const CollisionInfo& c, int32_t player_slot);

// [driving] 0x00159d10 ESimFrameUpdate::~ESimFrameUpdate (o ASoundManager::fgCount += 1 de cada passo).
void audio_sim_step();

// O ouvinte (o EAudioUpdate: a câmera do jogador, as linhas da matriz e a velocidade) e o carro (a posição).
struct AudioListenerInput {
    float matrix[4][4] = {};      // as linhas (direita, cima, frente, posição) da câmera
    float vel[3] = {};
    float car_pos[3] = {};
    // O EnviroDesc sob o carro do jogador (WWorld::GetEnviroDesc, driving/sound/audio_stream.h; nulo sem face).
    const uint8_t* enviro = nullptr;
};
// [driving] 0x00147e30 EAudioUpdate::~EAudioUpdate: o som de um quadro (os caminhos, os volumes, o PlayVoices).
void audio_update(const AudioListenerInput& l);

// O relógio de 100 Hz do EA (TIMER_gettick): o nf_driving o anda pelo relógio do jogo.
void audio_set_timer(int32_t ticks);

// Para os relatórios.
struct AudioStats {
    int32_t banks = 0, banks_failed = 0;
    int32_t sounds = 0;           // os ABaseSound vivos
    int32_t voices_active = 0;    // as vistas na lista ativa do PlayVoices
    uint64_t plays = 0, updates = 0, stops = 0, oneshots = 0, collisions = 0, landings = 0;
    float rpm = 0, engine_pitch = 0, engine_load = 0;
    int32_t afx_mode = -1;
    float afx_level = 0;
    uint64_t guards = 0;
};
AudioStats audio_stats();

// ---- os outros carros (driving/sound/audio_traffic.cpp) -----------------------------------------------------------

// O que o PBondCar::InitAudioObject lê da coleção de atributos para as classes 1 a 4 (e a afinação que ele sorteia).
struct AudioCarObjectParams {
    int32_t car_class = 3;
    std::string engine;           // SFX_ENGINE
    float volume = 0;             // SFX_VOLUME
    float skid_multiple = 0;      // SFX_SKIDMULTIPLE
    bool has_siren = false, is_heli = false, is_flying = false;
    std::string horn;             // SFX_HORN
    float horn_pitch = 0;         // SFX_HORNPITCH
    float pitch_offset = 0;       // o SimRandom das classes 2 e 4 (0..0,2)
};
// O que o InitAudioObject decide: a mixagem, o AHelicopter (não portado), a sirene, a buzina e o alcance.
struct AudioCarObjectPlan {
    const char* mix = "";
    bool heli = false, siren = false, horn = false;
    float min = 0, max = 0, max2 = 0;
};
AudioCarObjectPlan audio_car_object_plan(const AudioCarObjectParams& p);
// O ATrafficVehicle de um carro (o P32 do objeto, o +0x388 do PBondCar; 0 sem o som ligado ou no AHelicopter).
uint32_t audio_create_car_object(const AudioCarObjectParams& p);
void audio_delete_car_object(uint32_t h);
// A IA: a buzina (+0x154), a sirene (+0x244) e o AVehicle::Reset do carro tirado da rua.
void audio_car_horn(uint32_t h);
void audio_car_siren(uint32_t h);
void audio_car_reset(uint32_t h);
// As contas que a parte 25 do nf_driving_check confere contra o original: o AVehicle::CalculateRpm (0x002df8a0), a
// parte própria do ATrafficVehicle::Play (0x002e5598) e o passo da buzina do AHorn::Play (0x002dea48; o random da libc
// do jogo, as seis palavras do audio_internals()).
float AVehicle_CalculateRpmValues(bool tracked, bool has_horn, float rpm, float wobble, float speed);
struct TrafficPlayFields {
    int32_t surface2 = 0, surface3 = 0, alive = 0;
    float gas = 0, volume = 0, speed = 0, smooth_rpm = 0, load = 0, pitch_offset = 0;
};
struct TrafficPlayVoices {
    float vol[3] = {}, pitch[3] = {};  // LdEn, LdEx, o ruído da rua
};
struct APath;
void ATrafficVehicle_PlayOwn(TrafficPlayFields& f, float countf, float rpm, float mix_volume, const APath& path,
                             TrafficPlayVoices& out);
void AHorn_Step(int32_t& honks, int32_t& ticks, int32_t& wait, int32_t request);
struct TrafficAudioStats {
    uint64_t created = 0, deleted = 0, plays = 0, guards = 0;
};
TrafficAudioStats& traffic_audio_stats();

// ---- ACharacter (o som dos personagens: driving/sound/audio_character.cpp) -----------------------------------------

// O construtor inline do AICharacter::GetACharacter (0x00118988): um ACharacter novo (o P32 do objeto; o AICharacter
// o guarda no +0x94).
uint32_t ACharacter_New();
// [driving] 0x002fbe00 ACharacter::Say(char *): a fala ("HDeath", "HGrunt", "HFall", "HYell", "HScream"; "M..." e os
// outros os pedestres), se nenhuma voz toca ainda.
void ACharacter_Say(uint32_t handle, const char* what);
// A voz acabou (o +0xb0 nulo; um som que o audio_shutdown apagou também conta como acabado).
bool ACharacter_Done(uint32_t handle);
// O fim do AICharacter::Execute: o som na posição do personagem.
void ACharacter_SetPos(uint32_t handle, const float pos[3]);
// A entrada +0xc da tabela virtual com 3 (o ~ACharacter e o operator delete).
void ACharacter_Delete(uint32_t handle);

// As partes do ACharacter::Say e do Play que a parte 25 do nf_driving_check confere contra o original.
struct ACharacterSayState {   // os contadores das rodas no .data
    int32_t fall = 0;         // 0x00356134
    int32_t yell = 0;         // 0x00356144
    int32_t death = 0;        // 0x00356174
    int32_t scream = 0;       // 0x00356188
    int32_t grunt = 0;        // 0x003561b0
    int32_t mped = 0;         // 0x003561e0
    int32_t mped_wrapped = 0; // 0x003561e4
    int32_t fped = 0;         // 0x003561fc
    int32_t fped_wrapped = 0; // 0x00356200 (nada o liga)
};
ACharacterSayState& acharacter_say_state();
// O nome escolhido (nulo: uma entrada nula da roda) e o alcance (+0x5c, +0x60, +0x64); `present` é o
// ABank::IsPatchPresent do nome (as rodas dos pedestres procuram um timbre presente).
const char* ACharacter_PickName(const char* what, const std::function<int32_t(const char*)>& present, float range[3]);
// O volume do ACharacter::Play: o do som x o da mixagem x o do caminho.
float ACharacter_PlayVolume(float volume, float mix_volume, float path_volume);
// O fim da voz no ACharacter::Play (`over`: o +0x1c de cada vista; `mask`: as vistas em uso, 0x00355cd0).
bool ACharacter_VoiceDone(const int32_t over[3], uint32_t mask);

// ---- os sons 'gs' da galeria do Giotto (driving/sound/audio_sound.cpp) ---------------------------------------------

// O ASound("Object Sounds") de uma trilha 'gs' (o CANVAS_Draw: o operator new e o 0x002ed260; `one_shot`, a bandeira
// 0x800 da tela, tira o laço das vistas). O P32 do objeto (a tela o guarda no +0x70).
uint32_t GFXGallery_NewSound(bool one_shot);
// [driving] 0x002363b0 GFXGallery::SOUND_Play: a posição e a velocidade; com a chave (`desc`, a da criação) a mixagem
// "Giotto", o timbre, o volume, o pitch e o alcance.
void GFXGallery_SOUND_Play(uint32_t handle, const uint8_t* desc, const float pos[3], const float vel[3]);
// Os números que o SOUND_Play tira da chave: o volume, o pitch, o alcance mínimo, o máximo e o quadrado dele.
void GFXGallery_SOUND_Fields(const uint8_t* desc, float out[5]);
// O CANVAS_HasStopped: a voz acabou nas vistas em uso (um som apagado pelo audio_shutdown também conta).
bool GFXGallery_SoundDone(uint32_t handle);
// A entrada +0xc da tabela virtual com 3.
void GFXGallery_DeleteSound(uint32_t handle);
struct GallerySoundStats {
    uint64_t created = 0, started = 0, silent = 0, deleted = 0, guards = 0;
};
GallerySoundStats& gallery_sound_stats();

// ---- os sons das armas, da mira e dos objetos (driving/sound/audio_scene.cpp, audio_world.cpp; seção 31) -----------

// [driving] 0x002f4a20 AMenuSound::Trigger(int banco, int timbre, char *mixagem, AView): um som sem posição, de uma vez
// só (o AMenuSoundPriv). Nada sem o som ligado.
void AMenuSound_Trigger(int32_t bank, int32_t patch, const char* mix, int32_t view);
// [driving] 0x002f4aa0 AMenuSound::Trigger(char *banco, char *timbre, char *mixagem, AView)
void AMenuSound_TriggerBank(const char* bank, const char* patch, const char* mix, int32_t view);
// [driving] 0x002f4b10 AMenuSound::Trigger(int categoria, char *timbre, char *mixagem, AView): o banco da categoria
// (ABank::fgBanks: 0 Generic ... 8 g_common).
void AMenuSound_TriggerCategory(int32_t category, const char* patch, const char* mix, int32_t view);

// ATargeting (0xc bytes, o leiaute do PS2): os bipes da trava do WTargetPicker (+0x7c) e do GHud (+0x39c).
struct ATargetingData {
    int32_t state = 0;      // +0x00 0 nada, 1 seguindo, 2 travado
    int32_t unknown04 = 0;  // +0x04
    uint32_t mix = 0;       // +0x08 (p32 AMix) "Tracking System"
};
static_assert(sizeof(ATargetingData) == 0xc);
// [driving] 0x002e6a88 ATargeting::ATargeting
void ATargeting_Construct(ATargetingData& t);
// [driving] 0x002e6ac8 ATargeting::SetState(int)
void ATargeting_SetState(ATargetingData& t, int32_t state);

// O AZoomObj do carro do jogador (o +0x1ac do APlayerHeli: a respiração da luneta), pelo HUD
// (GHud::DrawCrossHairs) e pelo ESetAutoDriveCamOffMovie: AZoomObj::InTheZone. Nada sem o objeto.
void audio_player_zoom(int32_t on);
// O +0xc0 do objeto do carro do jogador (o GHud::DrawCrossHairs grava 1/0 quando o zoom muda).
void audio_player_scene_c0(int32_t v);

// O ASmackable de um Smackable (o P32 do objeto; 0 sem o som ligado).
uint32_t ASmackable_New(int32_t lo, int32_t med, int32_t hi, int32_t roll);
// [driving] 0x002efc80 ASmackable::Detatch
void ASmackable_Detatch(uint32_t h);
// O que o Smackable::Simulate escreve no objeto (a posição e a velocidade do corpo) e o IsStillActive (a voz do choque).
void ASmackable_Feed(uint32_t h, const float pos[3], const float vel[3]);
bool ASmackable_IsStillActive(uint32_t h);

// Os sons do mapa (o WSoundGroup do WWorld, +0x38): os registros 'Audi' da pista (0x60 bytes) no WWorld::Open e o
// WSoundGroup::Update do EAudioUpdate. Um caminho animado cujo objeto tem um DEFAULTSOUND também entra (o +0x10 da
// "PhysicsData"; WARNINGSOUND o +0x14), na posição do RPathHandle.
struct WorldPathSound {
    float pos[3] = {};
    uint32_t handle = 0;          // p32 RPathHandle
    int32_t sound = -1, warning = -1;
};
void world_sounds_open(const uint8_t* audi, int32_t count, const std::vector<WorldPathSound>& paths);
void world_sounds_close();
int32_t world_sounds_count();

// AMix::Get(nome) e o volume trocado (o anterior no +0x8): o "0:Fader0" a 0,5 do EPlayerLose. Nada sem o som ligado.
void audio_mix_force(const char* name, float volume);

// O id de um timbre pelo nome no banco da categoria `first`, ou no de `second` (banco << 7 | timbre; -1 se nenhum): o
// leitor dos campos de som da "PhysicsData" (0x00194d48). Antes do ASoundManager::Init os bancos são o vazio (-1).
int32_t audio_patch_id(const char* name, int32_t first, int32_t second);

struct SceneAudioStats {
    uint64_t menu_sounds = 0, gun_shots = 0, zone_in = 0, zone_out = 0, targeting = 0, targeting_warnings = 0;
    uint64_t smackables = 0, smack_collisions = 0, smack_detached = 0, world_sounds = 0, collisions_other = 0;
    uint64_t guards = 0;
};
SceneAudioStats& scene_audio_stats();

struct ACharacterStats {
    uint64_t created = 0, deleted = 0, says = 0, silent = 0, voices_done = 0, guards = 0;
};
ACharacterStats& acharacter_stats();

}  // namespace drv
