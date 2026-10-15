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
// Fica de fora (contado ou anotado em cada lugar): as falas e a música (AStream: os fluxos EA-XA pelo mixador do EE),
// o mixador do IOP (as vozes de render 0x100: o ambiente da Paris, os capangas), AFader, as armas do carro (AGun,
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
};
// [driving] 0x00186878 PBondCar::InitAudioObject (o carro do jogador, classe 0; um APlayerVehicle).
void audio_create_player_car(const AudioCarAttributes& a);

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

}  // namespace drv
