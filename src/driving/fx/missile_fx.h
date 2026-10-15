#pragma once
// O rastro e a luz dos mísseis (docs/driving.md seção 25): o RMissileStreak (RDebris.obj) e a parte de efeitos do
// Missile::Render (Missile.obj, 0x00181960), que o RRenderWorldCamera::DrawBulletStreaks chama para cada míssil vivo a
// cada câmera desenhada (atualizando só na câmera 0).
//
// Como o PS2 faz:
//  - RMissileStreak::Init (0x001e3e28, do RRenderHigh) monta três DrawArray de 40 vértices (um por tipo de rastro: 0 o
//    comum, 1 o do 0x19, 2 o dos torpedos), em faixa de triângulos (o GeoPrimState 0x3de080: Gouraud e textura, mistura
//    normal), com a textura 'trai' do common.ssh, as cores 0x3de100 (cinza 0x80 com o alfa subindo como (i / 12)² x 30
//    nos 13 primeiros e 30 depois: a cauda some) e o STQ 0x3de1b0 (s alterna 1, 0; t muda a cada dois vértices);
//  - o Missile::Render pega a posição do míssil (o corpo simples, ou o LocateFX do RSceneObj, que o port não tem); o
//    EMP (arma 0xe) só acende uma luz azul (RLightManager::AddPositionalLight direto, K 4, ou K 1 morrendo) depois do
//    tempo em linha reta; os outros, atualizando, pedem a luz do míssil (RHighLevelLightManager::AddMissile: o tipo 1
//    na arma 1, 2 na 0x19, 0 nas outras; o valor (2 x (q - idade))² x passo nos primeiros quartos de segundo, depois a
//    idade em segundos; nem morrendo nem na arma 0x15) e movem o rastro (Update(COORD4 &), ou Update(void) morrendo;
//    nada com a simulação parada); por fim desenham o rastro (salvo as armas 2 e 0x1e);
//  - RMissileStreak::Update(COORD4 &) (0x001e4640): até 96 pontos, um par novo a cada dois Update: a posição com um
//    tremor do RRandom (± largura / 2 em cada eixo; a largura 0x334e30 pelo tipo) e o par aberto para os lados pela
//    linha 0 da matriz da primeira câmera (o "direita") vezes o deslocamento 0x334e20 do tipo;
//  - RMissileStreak::Draw (0x001e4258): os últimos 40 pontos até o par do tique ((tique + 1) / 2 x 2, no máximo os
//    pontos), com quatro ou mais; o STQ começa no 0 ou no 2 (o bit 1 do primeiro ponto).
//
// Divergências (só do port): as tabelas globais (cores, STQ) são do port; o LocateFX (a posição pelo RSceneObj do
// míssil) não foi portado: com RSceneObj o port usa o corpo e conta em missile_fx_stats().guards; o desenho do PC
// (drv_fx.cpp) transforma a faixa em triângulos do Direct3D (o MatDebugTexture, com o recorte de sempre).

#include <cstdint>
#include <vector>

#include "driving/fx/fx_lights.h"
#include "driving/game/missile.h"

namespace drv {

struct Simulation;

// As tabelas globais do RMissileStreak::Init.
struct MissileStreakTables {
    uint32_t colours[40];  // 0x3de100 (RGBA do GS)
    float stq[42][4];      // 0x3de1b0
    bool ready = false;
};
MissileStreakTables& missile_streak_tables();

// [driving] 0x001e3e28 RMissileStreak::Init (as tabelas; os DrawArray e as texturas ficam com o desenho do PC)
void RMissileStreak_Init(MissileStreakTables& t);
// [driving] 0x001e4640 RMissileStreak::Update(COORD4 &): `right` a linha 0 da matriz da primeira câmera
void RMissileStreak_Update(RMissileStreakData& s, const float pos[4], const float right[3]);
// [driving] 0x001e43e0 RMissileStreak::Update(void): só o tique (morrendo)
void RMissileStreak_UpdateDying(RMissileStreakData& s);
// O que o RMissileStreak::Draw (0x001e4258) desenha: `count` pontos a partir de `first`, o STQ a partir de `stq_first`.
struct StreakDrawRange {
    int32_t first = 0, count = 0, stq_first = 0, kind = 0;
};
// [driving] 0x001e4258 RMissileStreak::Draw: falso sem o que desenhar (menos de quatro pontos).
bool RMissileStreak_Draw(const RMissileStreakData& s, StreakDrawRange& out);

// Um rastro para o desenho do PC: os pontos (posição, cor RGBA do GS, STQ) em faixa de triângulos e o tipo.
struct StreakStrip {
    int kind = 0;
    std::vector<float> pos;        // 4 por ponto
    std::vector<uint32_t> colour;  // 1 por ponto
    std::vector<float> stq;        // 4 por ponto
};

// [driving] 0x00181960 Missile::Render(bool): a luz e o rastro. `body_pos` a posição do corpo simples (nulo: sem corpo,
// nada); `right` a linha 0 da câmera; `strip` recebe o rastro desenhado (nulo: não desenha).
void Missile_RenderEffects(MissileData& d, const PhysicsObjectWords& po, const float* body_pos, bool update,
                           const Simulation& sim, const float right[3], RLightManagerPositional& lm,
                           StreakStrip* strip);

// O que o port conta.
struct MissileFxStats {
    uint64_t renders = 0, emp_lights = 0, lights = 0, updates = 0, dying_updates = 0, draws = 0, guards = 0;
};
MissileFxStats& missile_fx_stats();

}  // namespace drv
