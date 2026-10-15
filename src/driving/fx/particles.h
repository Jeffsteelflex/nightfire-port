#pragma once
// O sistema de partículas do jogo de carro (RDebris.obj e RVehicleParticle.obj do DRIVING.ELF; docs/driving.md seção 25):
//  - a biblioteca dos tipos (RParticleLibrary: até 256 GenericParticleData de 0xb0 bytes e o mapa da etiqueta para o
//    tipo; o Reload deixa só o 'dflt' do .data, 0x3351c0; a galeria dos efeitos e o RVehicleParticle somam os deles com
//    o AddSystem, que "munge" a cópia: as cores em 1/512 e na ordem inversa, o tamanho x 1,4142, a vida em 1/vida);
//  - os sistemas (RParticleSystem, 0x70 bytes, e RMovableParticleSystem, 0xb0, com a cópia do emissor): cada um segue
//    um emissor (o "CARP::ParticleData" de 0x40 bytes que a galeria ou o carro mantêm: a posição, o eixo lateral, a
//    etiqueta do tipo, a direção, o índice "grudado") e cria partículas pela taxa do tipo (CalcEmitVal), no ciclo
//    ligado/desligado dele;
//  - o gerente (RParticleSystemManager, um só: o fManager 0x3d82f0): o mapa dos sistemas vivos pela chave de quem pediu
//    (AddOrRefresh), o passo de cada um (UpdateSpawnAllSystems, até 3 tiques por quadro desenhado; um sistema que
//    ninguém renova em 30 passos é apagado) e o cache das partículas;
//  - o cache (RParticleParticleCache): dois quadros de 1024 partículas de 32 bytes, em dois blocos de 512 que o PS2
//    sobe ao scratchpad; o passo (UpdateBlock: posição += velocidade, velocidade x arrasto, gravidade, a vida em tiques)
//    copia as vivas para o outro quadro; o desenho (RenderBlock) as empacota em três listas para o EAGL::DrawArray: os
//    retângulos girados de mistura normal e os aditivos (o render method RenderSpritesRot, fx_render.h) e os riscos de
//    três vértices (o bit 0x20 do tipo: o MatDebugTexture).
//
// A partícula (32 bytes): +0x00 a posição, +0x0c a vida (tiques), +0x0d as bandeiras do tipo (byte de baixo; 0x40 o
// alfa do byte +0x1c), +0x0e a gravidade (com sinal, x -1/4096 por tique), +0x0f o índice do tipo, +0x10 a velocidade
// (por tique), +0x1c os nibbles de alfa (alto) e tamanho (baixo), +0x1d o arrasto x 255, +0x1e o índice "grudado" da
// galeria (-1 nenhum), +0x1f o giro (com sinal, radianos x 40,7).
//
// Portado do MIPS do PS2 (a verdade); os nomes de campos e a leitura geral são do nightfire-cxbx (NightfireResearch, a
// versão de Xbox, render/Particles.h), conferidos no PS2 (o leiaute dos sistemas e o "munge" são outros no Xbox).
// Conferido na parte 21 do nf_driving_check (tests/driving_check_fx.cpp).
//
// Divergências (só do port, documentadas nas funções):
//  - o mapa dos sistemas é um std::map pela chave (no PS2 a árvore de 0x18 bytes por nó da STL da GCC 2.95, com a mesma
//    ordem: as chaves em ordem crescente); as chaves são os endereços dos emissores, que diferem dos do PS2, então a
//    ordem em que os sistemas criam partículas (e gastam o RRandom) pode diferir da do PS2 com vários sistemas vivos;
//  - o scratchpad: o PS2 sobe cada bloco ao scratchpad (Upload, DMA) e anda as partículas lá; o port anda no próprio
//    cache (o mesmo resultado);
//  - a matriz da câmera que o giro alinhado à câmera (bit 0x10) lê (o RRenderer::fgRenderer +0x78, 0x291840 e
//    0x297480): o port a recebe do desenho (particles_set_view_matrix).

#include <cstdint>
#include <map>
#include <vector>

#include "platform/ptr32.h"

namespace drv {

// GenericParticleData / MungedGenericParticleData (0xb0 bytes; o mesmo leiaute antes e depois do "munge").
struct ParticleData {
    float spread_x;          // +0x00 a partícula nasce até isto para cada lado em x
    float speed;             // +0x04 ao longo da direção do emissor
    float spread_z;          // +0x08
    uint32_t count;          // +0x0c partículas por ciclo (o munge do PS2 o divide por 2)
    float side_speed;        // +0x10 até isto para cada lado ao longo do eixo lateral do emissor
    float speed_spread;      // +0x14
    float axis_speed;        // +0x18 ao longo do eixo do sistema (lado x direção)
    uint32_t flags;          // +0x1c 2 uma vez só, 8 gira, 0x10 giro pela câmera, 0x20 risco, 0x40 alfa por partícula,
                             //       0x80 tamanho por partícula, 0x100 alfa no byte +0x1c, 0x200 aditivo
    float drag;              // +0x20 a velocidade x isto a cada tique
    uint32_t tag;            // +0x24 (a etiqueta do arquivo)
    uint32_t unknown28;
    uint8_t alpha_byte;      // +0x2c o byte +0x1c da partícula
    uint8_t unknown2d[3];    // +0x2d (o PARTICLE_Add da galeria grava o último tipo no +0x2d)
    float inv_life;          // +0x30 1 / vida (o munge)
    float size;              // +0x34 o tamanho ao nascer (o munge: 1,4142 x o do fim)
    float size_change;       // +0x38 (o munge: 1,4142 x (nascer - fim))
    uint32_t life;           // +0x3c em tiques (no máximo 255)
    uint32_t on_time;        // +0x40 tiques do ciclo em que cria ...
    uint32_t off_time;       // +0x44 ... e em que não cria; 0 sem ciclo (o munge põe 99999 nos de uma vez só)
    uint32_t initial;        // +0x48 x a taxa: as partículas devidas ao nascer (o munge: vida x 0,75)
    float gravity;           // +0x4c (a galeria: x 127)
    float gravity_spread;    // +0x50 (a galeria: x 127)
    float spin;              // +0x54 graus (a galeria x 360); o munge: radianos
    float unknown58;
    float max_size;          // +0x5c o maior dos dois tamanhos (antes do munge)
    float uv[4];             // +0x60 u, v, o tamanho do quadrado na textura, (w: o giro, no desenho)
    float colour[3][4];      // +0x70 ARGB 0..255 ao nascer, no meio, no fim (o munge: x 1/512, o alfa do 0 e do 2 / 2,
                             //       e o 0 trocado com o 2)
    float size_spread;       // +0xa0
    float alpha_spread;      // +0xa4
    uint32_t unknowna8;
    uint32_t index;          // +0xac o lugar na biblioteca
};
static_assert(sizeof(ParticleData) == 0xb0);

// O emissor ("CARP::ParticleData", 0x40 bytes) que a galeria ou o carro mantêm e o sistema segue.
struct ParticleEmitter {
    float pos[4];            // +0x00
    uint32_t unknown10[2];
    P32<struct RParticleSystem> system;  // +0x18 o sistema dele (o destrutor do sistema o zera)
    uint32_t unknown1c;
    float side[3];           // +0x20
    uint32_t tag;            // +0x2c a etiqueta do tipo na biblioteca (-1 ainda sem tipo)
    float dir[3];            // +0x30
    int32_t sticky;          // +0x3c o índice "grudado" da galeria (0xff nenhum; o byte vai para a partícula)
};
static_assert(sizeof(ParticleEmitter) == 0x40);

// RParticleSystem (0x70 bytes; a tabela virtual em +0x60) e o RMovableParticleSystem (0xb0: a cópia do emissor em +0x70).
struct RParticleSystem {
    float axis[4];           // +0x00 lado x direção do emissor (InitData); w 5
    float last[3];           // +0x10 de onde criou no último passo
    int32_t keep_alive;      // +0x1c passos até o gerente o apagar (o AddOrRefresh põe 30)
    uint32_t time;           // +0x20 tiques no ciclo ligado/desligado
    float lod;               // +0x24 a distância à câmera (sem criar se max_size / isto < 0,004)
    float next_spawn;        // +0x28 a contagem em que a próxima partícula nasce
    float spawns;            // +0x2c as devidas (somadas pela taxa)
    P32<ParticleData> data;  // +0x30 o tipo
    P32<ParticleEmitter> emitter;  // +0x34
    int32_t active;          // +0x38
    uint16_t flags;          // +0x3c 1 segue o emissor, 2 sem tamanho, 4 posição de outra fonte
    uint16_t pad3e;
    uint32_t source;         // +0x40 (p32 de 3 floats) de onde as partículas nascem sem o bit 1
    uint32_t velocity;       // +0x44 (p32 de 3 floats, por segundo) somada à velocidade das partículas; 0 nenhuma
    float rate;              // +0x48 partículas por tique (CalcEmitVal)
    float rate_scale;        // +0x4c
    float alpha_bias;        // +0x50
    float unknown54;
    uint32_t scaled_count;   // +0x58 count x ParticleDensity / 256
    uint32_t index;          // +0x5c o índice do tipo
    uint32_t vtable;         // +0x60 0x3909a8 (RParticleSystem) ou 0x390968 (o móvel)
    uint32_t unknown64[3];
    ParticleEmitter copy;    // +0x70 só no móvel
};
static_assert(sizeof(RParticleSystem) == 0xb0);
constexpr uint32_t kParticleSystemVtable = 0x003909a8, kMovableParticleSystemVtable = 0x00390968;

// Uma partícula (32 bytes).
struct Particle {
    float pos[3];            // +0x00
    uint8_t life;            // +0x0c
    uint8_t flags;           // +0x0d
    int8_t gravity;          // +0x0e
    uint8_t type;            // +0x0f
    float vel[3];            // +0x10
    uint8_t nibbles;         // +0x1c alfa (alto) e tamanho (baixo)
    uint8_t drag;            // +0x1d
    int8_t sticky;           // +0x1e
    int8_t spin;             // +0x1f
};
static_assert(sizeof(Particle) == 0x20);

// RParticleParticleCache (no PS2 o gerente +0x40).
struct RParticleParticleCache {
    int32_t w00 = -1;        // +0x00
    int32_t count = 0;       // +0x04 as partículas do quadro corrente
    int32_t uploaded = -1;   // +0x08 o bloco no scratchpad
    uint32_t last_time = 0;  // +0x0c o RTimeData::fTime do último UpdateAndRender
    uint32_t unknown10[12] = {};
    Particle particles[2][1024];  // +0x40 os dois quadros
    int32_t current = 0;     // +0x10040 o quadro corrente
    int32_t spawned = 0;     // +0x10044 as do próximo quadro
};

// A biblioteca (RParticleLibrary::RPartLibraryData, 0xb010 bytes no PS2).
struct RParticleLibrary {
    ParticleData systems[256];
    uint64_t count = 0;                 // +0xb000
    std::map<uint32_t, uint32_t> tags;  // +0xb008 (no PS2 a árvore da STL) a etiqueta -> o índice
};

// As listas que o RenderBlock enche (os vetores do EAGL::DrawArray, globais no PS2): por bloco (dois de 512) e pelo
// quadro duplo (o 0x3390b0 alterna 0/1).
struct ParticleDrawLists {
    // Os retângulos (RenderSpritesRot): Coordinates (x, y, z, tamanho), UVUVs (u, v, lado, giro), Colours (RGBA, 8 bits).
    struct Sprites {
        std::vector<float> coords;    // 4 por retângulo
        std::vector<float> uvuv;      // 4 por retângulo
        std::vector<uint32_t> colours;
    };
    Sprites normal[2];       // por bloco: 0x3e9db0, 0x3f1db0, 0x3e3ff0 (o GeoPrimState 0x3e3f70: mistura normal)
    Sprites additive[2];     // o bit 0x200: 0x3f9db0, 0x401db0, 0x3e6070 (0x3e5ff0: Cs x As + Cd)
    // Os riscos (MatDebugTexture): três vértices por partícula; o STQ é o fixo do construtor (0x40fdb0); por bloco (o
    // PS2 recomeça a lista a cada bloco e a desenha no fim dele).
    std::vector<float> tri_coords[2];      // 4 por vértice (0x409db0)
    std::vector<uint32_t> tri_colours[2];  // (0x3e80f0)
    void clear();
};

// O gerente (RParticleSystemManager, 0x100c0 bytes no PS2).
struct RParticleSystemManager {
    uint32_t last_tick = 0;                 // +0x00
    std::map<uint32_t, RParticleSystem*> systems;  // +0x04 (a árvore da STL) pela chave de quem pediu
    RParticleLibrary library;               // +0x08
    RParticleParticleCache cache;           // +0x40
    ParticleDrawLists lists;                // as listas do último RenderBlock (globais no PS2)
};

// O gerente (fManager); nulo antes do Init.
RParticleSystemManager* particle_manager();
// [driving] 0x001e6e88 RParticleSystemManager::Init(char *): o contexto de texturas "data\render\<nome>" (o port o
// carrega no desenho) e o gerente.
void RParticleSystemManager_Init();
// [driving] 0x001e6f40 RParticleSystemManager::Shutdown
void RParticleSystemManager_Shutdown();
// [driving] 0x001e76c8 RParticleSystemManager::Reset: as partículas apagadas e todos os sistemas destruídos.
void RParticleSystemManager_Reset(RParticleSystemManager& m);
// O quadro duplo das listas (0x3390b0: o JustRender o troca; global, o Init do gerente não o zera).
int32_t& particle_render_toggle();
// O ParticleDensity (0x425148, 256 = 1,0).
uint32_t& particle_density();

// [driving] 0x001e7f60 RParticleLibrary::Reload: vazia, só o 'dflt' (0x3351c0).
void RParticleLibrary_Reload(RParticleLibrary& lib);
// [driving] 0x001e8040 RParticleLibrary::AddSystem(GenericParticleData &, unsigned int): a cópia e o munge; o índice ou -1.
int32_t RParticleLibrary_AddSystem(RParticleLibrary& lib, ParticleData& data, uint32_t tag);
// [driving] 0x001e8008 RParticleLibrary::FindSystemViaTag (0x001e7db0): o tipo da etiqueta, ou o de menor etiqueta.
ParticleData* RParticleLibrary_FindSystemViaTag(RParticleLibrary& lib, uint32_t tag);
// [driving] 0x001e8028 RParticleLibrary::FindSystemViaIndex
ParticleData* RParticleLibrary_FindSystemViaIndex(RParticleLibrary& lib, uint32_t index);
// [driving] 0x001e80b0 RParticleLibrary::MungeData(GenericParticleData &, MungedGenericParticleData &)
void RParticleLibrary_MungeData(const ParticleData& in, ParticleData& out);
// [driving] 0x001f3188 RParticleLibrary::MungeDataForPlatform
void RParticleLibrary_MungeDataForPlatform(const ParticleData& in, ParticleData& out);
// O 'dflt' do .data (0x3351c0, 0xb0 bytes) antes do munge.
const ParticleData& particle_default_data();

// [driving] 0x001e5cd8 CalcEmitVal(int contagem, int vida, int ligado, int desligado)
float CalcEmitVal(int32_t count, int32_t life, int32_t on, int32_t off);
// [driving] 0x001e5e10 RParticleSystem::RParticleSystem(float, CARP::ParticleData &, bool initData)
void RParticleSystem_Construct(RParticleSystem& s, float lod, ParticleEmitter* emitter, bool init_data);
// [driving] 0x001e60c8 RMovableParticleSystem::RMovableParticleSystem(float, CARP::ParticleData &)
void RMovableParticleSystem_Construct(RParticleSystem& s, float lod, const ParticleEmitter& emitter);
// [driving] 0x001e5f60 RParticleSystem::~RParticleSystem (0x001e61a0 o do móvel): o emissor perde o sistema.
void RParticleSystem_Destruct(RParticleSystem& s);
// [driving] 0x001e5d78 RParticleSystem::InitData
void RParticleSystem_InitData(RParticleSystem& s);
// [driving] 0x001e5fa0 RParticleSystem::SetSystem(unsigned int)
void RParticleSystem_SetSystem(RParticleSystem& s, uint32_t tag);
// [driving] 0x001e65c0 RParticleSystem::Update(int tiques) (a entrada +0x14 da tabela virtual; o móvel usa a mesma)
void RParticleSystem_Update(RParticleSystem& s, int32_t ticks);
// [driving] 0x001e8f30 (sem nome; o PARTICLE_Draw da galeria): o ativo.
inline void RParticleSystem_SetActive(RParticleSystem& s, int32_t v) { s.active = v; }

// Os criadores do AddOrRefresh (o RAbstractParticleCreator: a entrada +0xc).
enum class ParticleCreator { kSystem, kMovable };  // 0x3909d0 RParticleCreator, 0x390990 RMovableParticleCreator
// [driving] 0x001e61f8 RParticleSystemManager::AddOrRefresh(unsigned int chave, void *emissor, RAbstractParticleCreator &,
// float): o sistema da chave (criado se não há), com a distância e 30 passos. No PS2 devolve um par estático
// (0x3de910: o sistema e "criado agora").
RParticleSystem* RParticleSystemManager_AddOrRefresh(RParticleSystemManager& m, uint32_t key, ParticleEmitter* emitter,
                                                     ParticleCreator creator, float lod, bool* created = nullptr);
// [driving] 0x001e62e0 RParticleSystemManager::GetSystem(void *)
RParticleSystem* RParticleSystemManager_GetSystem(RParticleSystemManager& m, uint32_t key);
// [driving] 0x001e67e0 RParticleSystemManager::UpdateSpawnAllSystems
void RParticleSystemManager_UpdateSpawnAllSystems(RParticleSystemManager& m);
// [driving] 0x001e6330 RParticleSystemManager::CreateParticles(MungedGenericParticleData &, COORD3 &posição, COORD3
// &direção, int)
void RParticleSystemManager_CreateParticles(RParticleSystemManager& m, ParticleData& data, const float pos[3],
                                            const float dir[3], int32_t count);
// [driving] 0x001e6e48 RParticleSystemManager::UpdateAndRenderAllSystems (o cache: 0x001f47f8 UpdateAndRender)
void RParticleSystemManager_UpdateAndRenderAllSystems(RParticleSystemManager& m);

// [driving] 0x001f29d8 RParticleSystem::RParticle::ResetParticle(float a, float tiques, ...): a partícula nova.
void ResetParticle(float a, float ticks, Particle& p, const float source[3], const ParticleData& data,
                   const ParticleEmitter& emitter, const float axis[3], const float* velocity, const float delta[3]);
// [driving] 0x001f3bb8 RParticleParticleCache::Spawn: a vaga no próximo quadro (nulo com 1024).
Particle* RParticleParticleCache_Spawn(RParticleParticleCache& c);
// [driving] 0x001f3c20 RParticleParticleCache::SaveForNextFrame
void RParticleParticleCache_SaveForNextFrame(RParticleParticleCache& c, const Particle& p);
// [driving] 0x001f3ca8 RParticleParticleCache::UpdateBlock(int bloco) (sobre o bloco do quadro corrente, no lugar)
void RParticleParticleCache_UpdateBlock(RParticleParticleCache& c, int32_t block);
// [driving] 0x001f4878 RParticleParticleCache::JustUpdate
void RParticleParticleCache_JustUpdate(RParticleParticleCache& c);
// [driving] 0x001f3dc8 RParticleParticleCache::RenderBlock(int bloco, int quadro): empacota as listas.
void RParticleParticleCache_RenderBlock(RParticleSystemManager& m, int32_t block, int32_t frame);
// [driving] 0x001f4920 RParticleParticleCache::JustRender (o desenho das listas fica com quem desenha)
void RParticleParticleCache_JustRender(RParticleSystemManager& m);

// Só do port: o que o desenho dá às partículas (a matriz do mundo para a câmera do giro alinhado; o RRenderer).
void particles_set_view_matrix(const float m[16]);
const float* particles_view_matrix();
// Só do port: a posição grudada da galeria (GFXGallery::GET_StickyEffectPos; driving/fx/gfx_gallery.h a liga).
using StickyPosFn = bool (*)(int32_t index, float out[4]);
StickyPosFn& particles_sticky_pos();
// Só do port: a posição do corpo rígido de um índice (Simulation::GetRigidBody +0x10) para os riscos grudados.
using RigidPosFn = bool (*)(int32_t slot, float out[4]);
RigidPosFn& particles_rigid_pos();

struct ParticleStats {
    uint64_t systems_created = 0, systems_deleted = 0, spawned = 0, refused = 0, guards = 0;
    uint64_t sprites = 0, additive = 0, streaks = 0;
};
ParticleStats& particle_stats();

}  // namespace drv
