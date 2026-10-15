#pragma once
// As luzes pontuais dos efeitos (docs/driving.md seção 25): o RHighLevelLightManager (RDirectorQueue.obj; o
// RLightManager + 0x2e0) e a parte do RLightManager que monta o bloco das luzes pontuais (o GAME::PositionalLights que
// os métodos do carro, do mundo dinâmico e dos personagens leem na VU 0x40-0x47: x, y, z e K das quatro luzes em
// colunas, depois as quatro cores).
//
// Como o PS2 faz:
//  - uma explosão (o destrutor do ESpawnExplosion) chama AddExplosion(pos, dano): o tipo pelo dano (>800: 4, >400: 3,
//    >200: 2, >100: 1, senão 0) numa das quatro vagas (a livre, ou a mais velha), com o RTimeData::fTime;
//  - um míssil (Missile::Render, do RRenderWorldCamera::DrawBulletStreaks, só na câmera 0) chama AddMissile(pos, tipo,
//    valor) numa das duas vagas (com as duas cheias, a de valor maior, se maior que 0);
//  - no fim de cada quadro desenhado (0x001c3d58, depois do DrawEffects) o RLightManager::FinishLights chama o
//    RHighLevelLightManager::Process: as luzes pedidas pelo AddPositionalLight (até quatro, por prioridade), cada
//    explosão viva com a cor da tabela 0x333878 (0x18 bytes por tipo: a cor, o K e a intensidade) vezes
//    min(intensidade x (1 / (idade x 0,5) - 0,02), 16) e o K do tipo (a explosão some quando o fator chega a 0), e cada
//    míssil com a cor da tabela 0x3338f0 e o K do tipo vezes max(valor, 1); cada uma vai ao bloco em construção
//    (RLightManager::AddPositionalLight: as quatro primeiras; com o RColorize no modo 1 o vermelho fica 0 e o azul pela
//    metade); o FinishLights copia o bloco em construção para o publicado (+0x2a4), zera o em construção e as contagens.
//    O desenho do quadro seguinte lê o publicado.
//
// Divergências (só do port): o RColorize (0x3349e0, o modo da visão colorida) não foi portado: `colorize_mode` fica 0
// (o modo 1 só existe com ele); o bloco em construção e o publicado são do port (no PS2, dois blocos de 0x80 bytes do
// heap apontados por +0x2a0 e +0x2a4).

#include <cstdint>

namespace drv {

// Uma luz pedida (0x20 bytes): a posição (w = K), a cor e a prioridade.
struct HighLevelLight {
    float pos[4];      // +0x00
    float colour[3];   // +0x10
    int32_t priority;  // +0x1c
};
static_assert(sizeof(HighLevelLight) == 0x20);

// RHighLevelLightManager (0x118 bytes, o RLightManager + 0x2e0).
struct RHighLevelLightManager {
    HighLevelLight lights[4];   // +0x00 as do AddPositionalLight
    int32_t count;              // +0x80
    float expl_pos[4][4];       // +0x84 as explosões (o w recebe o K do tipo no Process)
    int32_t expl_type[4];       // +0xc4
    uint32_t expl_time[4];      // +0xd4 o fTime do AddExplosion (0: vaga livre)
    int32_t missile_count;      // +0xe4
    float missile_pos[2][4];    // +0xe8
    int32_t missile_type[2];    // +0x108
    float missile_value[2];     // +0x110
};
static_assert(sizeof(RHighLevelLightManager) == 0x118);

// O bloco das luzes pontuais (0x80 bytes, o leiaute da VU): as linhas x, y, z e K (uma coluna por luz) e as cores.
struct PositionalLightBlock {
    float pos[4][4];  // +0x00
    float col[4][4];  // +0x40 (r, g, b de cada luz; o w nunca é escrito)
};
static_assert(sizeof(PositionalLightBlock) == 0x80);

// A parte do RLightManager que o port tem para as luzes pontuais.
struct RLightManagerPositional {
    PositionalLightBlock building{};   // *(+0x2a0)
    PositionalLightBlock published{};  // *(+0x2a4) o GAME::PositionalLights
    int32_t unknown60 = 0;             // +0x60
    int32_t count = 0;                 // +0x64 as luzes do bloco em construção
    int32_t last_count = 0;            // +0x68 as do último bloco publicado
    int32_t colorize_mode = 0;         // RColorize::fgThis +0x30 (port: sempre 0)
    RHighLevelLightManager hl{};       // +0x2e0
};
// O do jogo (RLightManager::fgThis_RLightManager, 0x333830).
RLightManagerPositional& light_manager_positional();

// As tabelas do .data (0x333878: cinco tipos de explosão; 0x3338f0: três tipos de míssil), 0x18 bytes por tipo.
struct LightTypeEntry {
    float colour[4];
    float k;
    float intensity;
};
const LightTypeEntry* explosion_light_types();  // [5]
const LightTypeEntry* missile_light_types();    // [3] (o Process lê além delas com um tipo maior: não acontece)

// [driving] 0x001afa88 RHighLevelLightManager::AddExplosion(COORD4 &, float)
void RHighLevelLightManager_AddExplosion(RHighLevelLightManager& m, const float pos[4], float radius, uint32_t time);
// [driving] 0x001afba8 RHighLevelLightManager::AddMissile(COORD4 &, int tipo, float)
void RHighLevelLightManager_AddMissile(RHighLevelLightManager& m, const float pos[4], int32_t type, float value);
// [driving] 0x001afc58 RHighLevelLightManager::AddPositionalLight(COORD4 &, COORD4 &, int prioridade)
void RHighLevelLightManager_AddPositionalLight(RHighLevelLightManager& m, const float pos[4], const float colour[3],
                                               int32_t priority);
// [driving] 0x001afd50 RHighLevelLightManager::Process (o `lm` recebe as luzes; `time` o RTimeData::fTime)
void RHighLevelLightManager_Process(RLightManagerPositional& lm, uint32_t time);
// [driving] 0x001af520 RLightManager::AddPositionalLight(COORD4 &, COORD4 &)
void RLightManager_AddPositionalLight(RLightManagerPositional& lm, const float pos[4], const float colour[3]);
// [driving] 0x001aede8 RLightManager::FinishLights (com o Process do +0x2e0 e o CopyPositionalLightsToPlatformSpecific
// 0x001aedc8, vazio no PS2)
void RLightManager_FinishLights(RLightManagerPositional& lm, uint32_t time);
// Port: tudo zerado (o começo de uma missão).
void RLightManager_ResetPositional(RLightManagerPositional& lm);

// O que o port conta.
struct FxLightStats {
    uint64_t explosions = 0, missiles = 0, positional = 0, finishes = 0, published_lights = 0, refused = 0;
};
FxLightStats& fx_light_stats();

}  // namespace drv
