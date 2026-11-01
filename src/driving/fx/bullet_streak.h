#pragma once
// Os riscos das balas e os clarões dos tiros (docs/driving.md seção 29): o RBulletStreak (RBulletStreak.obj), o
// UVolatileMaterial (librealmema.a), o RayShell::DrawTracers e o RayShell::DrawFlashes (PVehicle.obj) e a parte das balas
// lentas do RRenderWorldCamera::DrawBulletStreaks.
//
// Como o PS2 faz: o RBulletStreak::Init lê do "Render:FX" (data\tuning\Render\FX\<pista>.tun) a tabela dos sete tipos de
// risco (o índice "BulletStreakType": Henchmen, Vanquish, SuperSnowmobile, SUV, Captain, Ultralight, Sentinel; 0x18
// bytes cada em 0x423f38: a cor, a forma, a largura, o comprimento, a velocidade e o BulletStreakCount) e prepara as
// quatro formas (as texturas 'wtrc', 'ytrc', 'otrc' e 'btrc' do contexto 0, cada uma com 76 vértices: os UV de dois
// triângulos por risco e o w = 1; o material: Gouraud, textura e a mistura aditiva Cs As + Cd). A cada quadro desenhado o
// DrawBulletStreaks põe um risco por bala lenta viva com tipo (o Shell, na direção da velocidade), o DrawTracers anda os
// rastros dos tiros instantâneos (o RayShell: cada um avança da boca, pela idade vezes a velocidade do tipo mais o
// comprimento, até passar da ponta; os rastros com mais de 60 passos se apagam) e o DrawFlashes acende uma luz pontual
// (0,2, raio 6) no primeiro clarão guardado e o tira da fila. O RBulletStreak::Add monta o risco de frente para a
// câmera: o lado = normalize(normalize(olho - p) x d) x largura, o rabo = d x comprimento, dois triângulos (p + lado,
// p - lado, rabo - lado; rabo - lado, rabo + lado, p + lado) com a cor do tipo pela metade (a escala do GS), até 70
// vértices por forma. O RBulletStreak::Draw manda cada forma com vértices ao UVolatileMaterial::Draw (uma lista de
// triângulos do EAGL::DynamicModel com o render method MatVolatileTexture, o mesmo microprograma do MatDebugTexture
// com o UV no lugar do STQ: o VIF desempacota o UV com a máscara 0x50 e a linha (0, 0, 1, 1), isto é, Q = 1) e zera as
// contagens.
//
// Divergências (só do port, cada uma também na função): o UVolatileMaterial::Draw não monta a lista do EAGL: grava o
// desenho (as cópias dos vértices) para o PC; a textura do PS2 (o ponteiro do RTextureContext) é o índice da forma; o
// PS2 lê o +0x164 das mãos de Bond (AICharacterHands, 0x327670) sem conferir o ponteiro no DrawTracers: o port conta
// (guards) e trata como zero.

#include <cstdint>
#include <vector>

#include "driving/fx/tuning_db.h"

namespace drv {

struct RLightManagerPositional;

// Um tipo de risco (0x18 bytes, a tabela 0x423f38).
struct BulletStreakType {
    uint32_t colour;  // +0x00 BulletStreakColour (R, G, B, A na memória)
    int32_t shape;    // +0x04 BulletStreakTexture (a forma, 0..3)
    float width;      // +0x08 BulletStreakWidth
    float length;     // +0x0c BulletStreakLength
    float speed;      // +0x10 BulletStreakSpeed (também a velocidade das balas lentas com rastro: Shell::Shell)
    int32_t count;    // +0x14 BulletStreakCount (a cada quantos tiros o EFireWeapon guarda um rastro)
};
static_assert(sizeof(BulletStreakType) == 0x18);

constexpr int kBulletStreakTypes = 7, kStreakShapes = 4, kStreakVerts = 0x4c;

// Os globais do RBulletStreak.obj e dos quatro UVolatileMaterial.
struct BulletStreakState {
    BulletStreakType types[kBulletStreakTypes] = {};  // 0x423f38
    int32_t count[kStreakShapes] = {};                // 0x425178 os vértices de cada forma
    float coords[kStreakShapes][kStreakVerts][4] = {};  // 0x425190 (0x4c0 bytes por forma)
    float uv[kStreakShapes][kStreakVerts][2] = {};      // 0x426490 (0x260)
    uint32_t colours[kStreakShapes][kStreakVerts] = {}; // fgColours (0x130)
    uint32_t texture[kStreakShapes] = {};             // fTexture 0x334990 (port: a forma; o PS2 o ponteiro)
    uint32_t texture_flags[kStreakShapes] = {};       // o +0x10 de cada textura (o TFX e o filtro: & 0xf9ffffff | 5)
    uint8_t material[kStreakShapes][0x90] = {};       // fgMaterial 0x334290 (o UVolatileMaterial: GeoPrimState + 0x80)
    int32_t index = 0;                                // 0x3349d0 a variável do índice "BulletStreakType"
    bool ready = false;
};
BulletStreakState& bullet_streak();

// Um desenho do UVolatileMaterial (o que o DynamicModel mandaria à VU1), para o PC.
struct VolatileDraw {
    int shape = 0;      // a forma (a textura)
    int prim = 4;       // o tipo do EAGL (4: lista de triângulos)
    int count = 0;
    uint64_t alpha = 0; // o ALPHA_1 do material (+0x40)
    uint64_t test = 0;  // o TEST_1 do material (+0x30)
    std::vector<float> coords;    // count x 4
    std::vector<float> uv;        // count x 2
    std::vector<uint32_t> colours;
    // Seção 32: a matriz do modelo (o EAGL::DynamicModel::SetModelMatrix do UVolatileMaterial::Draw com matriz; os
    // clarões dos canos) e a textura do port (-1: a forma dos riscos, kFxTexStreak0 + shape).
    bool has_matrix = false;
    float matrix[4][4] = {};
    int texture = -1;
};

// [driving] 0x001d82e0 (o construtor estático dos fgMaterial: os quatro UVolatileMaterial e as cores zeradas).
void RBulletStreak_StaticInit(BulletStreakState& s);
// [driving] 0x002cb860 UVolatileMaterial::UVolatileMaterial(void)
void UVolatileMaterial_Construct(uint8_t* m);
// [driving] 0x001d7620 RBulletStreak::Init(void): as texturas (`slot` = a forma), as formas e a tabela do "Render:FX".
void RBulletStreak_Init(BulletStreakState& s, const TuningReader& fx);
// [driving] 0x001d7d78 RBulletStreak::Add(COORD4 &p, COORD4 &d, StreakType) (`eye`: o +0x30 do RCamera da câmera do
// jogador, 0x333ed8 +4).
void RBulletStreak_Add(BulletStreakState& s, const float eye[4], const float p[4], const float d[4], int32_t type);
// [driving] 0x001d8048 RBulletStreak::Add(COORD3 &, COORD3 &, StreakType): o de cima com w = 1.
void RBulletStreak_Add3(BulletStreakState& s, const float eye[4], const float p[3], const float d[3], int32_t type);
// [driving] 0x001d7c18 RBulletStreak::Draw(void): cada forma com vértices vai ao UVolatileMaterial::Draw; zera as
// contagens.
void RBulletStreak_Draw(BulletStreakState& s, std::vector<VolatileDraw>* out);
// [driving] 0x001d7d50 RBulletStreak::Frame(void): zera as contagens.
void RBulletStreak_Frame(BulletStreakState& s);
// [driving] 0x002cb938 UVolatileMaterial::Draw(EAGL::PrimitiveType, unsigned int, MATRIX4 *): o material grava a
// contagem e o tipo; o port grava o desenho em `out` (o DynamicModel do PS2) com as cópias das três listas.
void UVolatileMaterial_Draw(uint8_t* material, int prim, int count, const float (*coords)[4], const float (*uv)[2],
                            const uint32_t* colours, int shape, std::vector<VolatileDraw>* out,
                            const float (*matrix)[4] = nullptr, int texture = -1);

// [driving] 0x001993e8 RayShell::DrawTracers(void): os rastros das balas instantâneas (rayshell_globals()).
void RayShell_DrawTracers(BulletStreakState& s, const float eye[4]);
// [driving] 0x00199678 RayShell::DrawFlashes(void): a luz do primeiro clarão (RLightManager::AddPositionalLight).
void RayShell_DrawFlashes(RLightManagerPositional& lm);
// [driving] 0x001c5aa8 RRenderWorldCamera::DrawBulletStreaks (a parte das balas lentas, o laço do Simulation +0xe20:
// o risco de cada Shell com tipo, na direção da velocidade do corpo simples).
void DrawBulletStreaks_Shells(BulletStreakState& s, const float eye[4]);

struct BulletStreakStats {
    uint64_t added = 0, full = 0, drawn = 0, vertices = 0, tracers = 0, tracers_done = 0, flashes = 0, shells = 0;
    uint64_t guards = 0;
};
BulletStreakStats& bullet_streak_stats();

}  // namespace drv
