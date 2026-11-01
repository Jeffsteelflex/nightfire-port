#pragma once
// Os clarões dos canos (docs/driving.md seção 32): o RMuzzleFlash (RDebris.obj do DRIVING.ELF), o
// ActWeapon::StartMuzzleFlash e a parte do ActWeapon::ManualRender que desenha os clarões das armas dos atores, o
// RVehicle::StartMuzzleFlash e a parte do RVehicle::RenderFX que desenha os dos carros.
//
// Como o PS2 faz: o EActorMuzzleFlash (o evento 39, criado pela chave "Fire" da animação de uma arma: os capangas nas
// janelas de Paris, as mãos de Bond) chama o ActWeapon::StartMuzzleFlash com o ponto do efeito no mundo: a primeira das
// oito vagas da arma (+0x84, 0x14 bytes) que já acabou recebe o ponto, o fim (o passo da simulação + 4) e a força
// (0,75 + um número do RRandom x 0,25 / 65536 + a distância ao carro do jogador / 400, até 0,5); a linha 2 da matriz da
// arma vai ao +0x124 e o passo + 7 ao +0x134. A cada quadro o ManualRender desenha cada vaga viva: a força x 0,75^(passo
// + 4 - fim) x 0,4 com o RMuzzleFlash::Draw (o modelo de 26 vértices em faixa, no referencial da arma, de frente para
// +z), ou, na arma do jogador (o bit 1 de +0x138), com o DrawPOV (um quadrado de frente para a câmera, com o tamanho e a
// textura pela última arma disparada do SWeaponManager e o giro do UV e a textura sorteados pelo RRandom fora da pausa).
// O dos carros: o RVehicle::TriggerFX 8 (o EFireWeapon das armas do carro) chama o RVehicle::StartMuzzleFlash (as duas
// vagas do +0x1e0: o ponto, a direção, o fim de 5 quadros, o tipo e a força pela arma e pelo HAS_TURRET) e o
// RVehicle::RenderFX os desenha. O Draw e o DrawPOV montam a matriz e mandam ao UVolatileMaterial::Draw (o
// MatVolatileTexture, com a matriz do modelo), sem escrever o Z, a mistura Cs + Cd (0x48).
//
// O RMuzzleFlash::Init (do Render_InitLibRender): o InitPOVLayer (o quadrado, os quatro giros do UV, o material, as 24
// texturas dos oito tipos), as cinco texturas e materiais dos tipos do Draw e o modelo do .data (0x334e40, girado um
// quarto de volta em y: (x, z) vira (z, -x)) com a cor 0x687f7f7f.
//
// Portado do MIPS do PS2; conferido na parte 28 do nf_driving_check (tests/driving_check_fx_ped.cpp).
// Divergências (só do port): as texturas do PS2 (os ponteiros do RTextureContext) são índices do port (o tipo, ou o tipo
// x 3 + k no POV); o modelo do .data vem do DRIVING.ELF do jogador (`read`); o RRandom é o do port (render_random()).

#include <cstdint>
#include <functional>
#include <vector>

#include "driving/fx/bullet_streak.h"

namespace drv {

constexpr int kMuzzleTypes = 5, kMuzzleVerts = 26, kMuzzlePovTypes = 8;

// Os globais do RMuzzleFlash.
struct MuzzleFlashState {
    float positions[kMuzzleVerts][4] = {};     // 0x334e40 o modelo (do .data, girado pelo Init)
    float uv[kMuzzleVerts][2] = {};            // 0x334fe0
    uint32_t colours[kMuzzleVerts] = {};       // 0x3de460
    uint32_t texture_names[kMuzzleTypes] = {}; // 0x3350d8 ('fl3d', 'fl2d'...)
    uint32_t texture[kMuzzleTypes] = {};       // 0x3de830 (port: o tipo)
    uint32_t texture_flags[kMuzzleTypes] = {}; // o +0x10 de cada textura (& 0xfffffff0)
    uint8_t material[kMuzzleTypes][0x90] = {}; // 0x3de4d0 (UVolatileMaterial)
    uint8_t pov_material[0x90] = {};           // 0x3de7a0
    float pov_positions[4][4] = {};            // 0x3de850
    float pov_uv[4][4][2] = {};                // 0x3de890 os quatro giros
    struct PovType {
        uint32_t names[3];                     // 0x3350f0 + 0x18 x tipo
        uint32_t textures[3];                  // +0xc (port: o tipo x 3 + k)
    } pov[kMuzzlePovTypes] = {};
    bool ready = false;
};
MuzzleFlashState& muzzle_flash();

// O construtor estático dos seis UVolatileMaterial (o Construct de cada um) e os nomes das texturas do .data.
// `read(addr, out, n)` copia do DRIVING.ELF (falso fora dele; os nomes e o modelo ficam zero).
void RMuzzleFlash_StaticInit(MuzzleFlashState& s, const std::function<bool(uint32_t, void*, uint32_t)>& read);
// [driving] 0x001e4b48 RMuzzleFlash::InitPOVLayer
void RMuzzleFlash_InitPOVLayer(MuzzleFlashState& s);
// [driving] 0x001e5488 RMuzzleFlash::Init
void RMuzzleFlash_Init(MuzzleFlashState& s);
// [driving] 0x001e55f0 RMuzzleFlash::Draw(float tamanho, MATRIX4 &, COORD4 &posição, COORD4 &direção, int tipo)
void RMuzzleFlash_Draw(MuzzleFlashState& s, float size, const float transform[4][4], const float pos[4],
                       const float dir[4], int32_t type, std::vector<VolatileDraw>* out);
// [driving] 0x001e5808 RMuzzleFlash::DrawPOV(float tamanho, MATRIX4 &, COORD3 &posição, int tipo): `paused` é a
// Simulation +0xdb4 == 3 (sem sorteio).
void RMuzzleFlash_DrawPOV(MuzzleFlashState& s, float size, const float transform[4][4], const float pos[3],
                          int32_t type, bool paused, std::vector<VolatileDraw>* out);

// O que os clarões dos atores e dos carros leem de fora.
struct MuzzleFlashEnv {
    // PhysicsObject::GetPosition do carro do jogador (a Simulation +0xdcc[0]); nulo: a origem.
    void (*player_position)(float out[3]) = nullptr;
    // AttributeSet::LookupBool(dono + 0x5c, "HAS_TURRET") e o IS_SNOWMOBILE (o +0xc8 dos atributos de física) do dono
    // de um RVehicle (o +0x24); nulos: falso e 0.
    bool (*has_turret)(uint32_t owner) = nullptr;
    int32_t (*is_snowmobile)(uint32_t owner) = nullptr;
    // A última arma disparada do SWeaponManager (+0x18; o DrawPOV da arma do jogador).
    int32_t (*last_fired)() = nullptr;
    // RSceneObj::GetTransform da arma (nulo: a cópia da instância do +0x08, como o 0x001c9190).
    void (*transform)(uint32_t scene, float m[4][4]) = nullptr;
};
MuzzleFlashEnv& muzzle_flash_env();

// [driving] 0x00116720 ActWeapon::StartMuzzleFlash(COORD3 &): `weapon` é a imagem do ActWeapon (p32), `steps` o passo da
// simulação.
void ActWeapon_StartMuzzleFlash(uint32_t weapon, const float pos[3], int32_t steps);
// [driving] 0x001169c8 ActWeapon::ManualRender (a parte dos clarões; o SetNowVisible, o SendPerViewPort e o Render da
// arma são do desenho dos atores do port).
void ActWeapon_DrawMuzzleFlashes(MuzzleFlashState& s, uint32_t weapon, int32_t steps, bool paused,
                                 std::vector<VolatileDraw>* out);
// [driving] 0x001d38d8 RVehicle::StartMuzzleFlash(COORD3 &, COORD3 &, unsigned int vaga, unsigned int arma)
void RVehicle_StartMuzzleFlash(uint32_t vehicle, const float pos[3], const float dir[3], int32_t slot, int32_t weapon);
// [driving] 0x001d3278 RVehicle::RenderFX(MATRIX4 &) (a parte dos clarões; as partículas, o EMP e os efeitos das
// instâncias, RSceneObj::RenderEffects, à parte).
void RVehicle_DrawMuzzleFlashes(MuzzleFlashState& s, uint32_t vehicle, const float transform[4][4], uint32_t time,
                                bool paused, std::vector<VolatileDraw>* out);

// As texturas do port (o índice da textura do desenho, `VolatileDraw::texture`): as do Draw a partir de kTexMuzzle0, as
// do POV a partir de kTexMuzzlePov0 (tipo x 3 + k).
int muzzle_texture_slot(uint32_t texture);
int muzzle_pov_texture_slot(uint32_t texture);

struct MuzzleFlashStats {
    uint64_t actor_started = 0, actor_full = 0, actor_drawn = 0, pov_drawn = 0, vehicle_started = 0;
    uint64_t vehicle_busy = 0, vehicle_drawn = 0, draws = 0, guards = 0;
};
MuzzleFlashStats& muzzle_flash_stats();

}  // namespace drv
