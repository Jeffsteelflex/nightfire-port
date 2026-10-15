#pragma once
// As marcas no chão (docs/driving.md seção 25): o RDecalManager (RDebris.obj; 0x298 bytes, RDecalManager::fgThis
// 0x334bc0) e o RScorchWorld::ScorchGround, que a explosão (o destrutor do ESpawnExplosion: tipo 5, tamanho 8) e o radar
// do SWeaponManager chamam.
//
// Como o PS2 faz: o construtor (0x001da030, do RRenderHigh) lê o data\render\decal.dat da missão (0x280 bytes: oito
// tipos de 0x50, cada um com quatro coordenadas de textura (s, t, q, 1) do 'decl' do common.ssh, o atraso em quadros
// (+0x44) e o alfa (+0x48)) e monta oito DrawArray de 72 vértices (12 marcas cada) sobre três vetores globais: as
// posições (0x3da420), o STQ (0x3d8020) e as cores (0x3d7720), com o GeoPrimState 0x334bd0 (lista de triângulos,
// Gouraud e textura, mistura normal: ALPHA 0x44, o Z escrito) e o render method MatDebugTexture. O AddDecal (0x001da6c8)
// escreve a marca seguinte do anel de 96: dois triângulos (seis vértices) num quadrado de lados w e h centrado na
// posição, no plano perpendicular a `dir` x `normal` (o ScorchGround passa dir = (0, 0, 1) e a normal da face: o quadrado
// fica no chão), a cor (0x7f, 0x7f, 0x7f, 0) e o atraso do tipo; a cada quadro desenhado o Draw (0x001da978, do
// RRenderWorldCamera::DrawEffects) conta o atraso de cada marca e, ao chegar a 0, põe o alfa do tipo nos seis vértices.
//
// O ScorchGround (0x001eb078) acha a face sob o ponto (WWorldPos::FindClosestFace), recusa as faces de bandeira 2 no
// subtipo, de tipo 6, ou de instância cujo artigo tem o byte +0xc, mede a altura do ponto acima da face, tira a normal
// da face (a do HeightAtPoint: (v1 - v0) x (v0 - v2) normalizada, para cima, y no máximo 0,9999) e recusa se y < 0,95;
// fora do tipo 4, confere quatro pontos a meio lado (tamanho / 2, no máximo 6) em +x, -x, +z e -z: cada um com face
// achada e altura a menos de 0,15 da do centro; recusa o tipo de face 9. Com o tamanho que sobra (meio lado menos metade
// da altura) positivo, a marca vai 0,025 acima da face com lado 2 x esse tamanho.
//
// Divergências (só do port): os vetores globais são do port (DecalBuffers); o desenho do PC (drv_fx.cpp) faz os
// triângulos do MatDebugTexture no Direct3D (o recorte da VU1, a divergência de sempre).

#include <cstdint>
#include <vector>

#include "driving/physics/world.h"

namespace drv {

// Um tipo do decal.dat (0x50 bytes).
struct DecalType {
    float stq[4][4];     // +0x00 os quatro cantos (s, t, q, w)
    uint32_t unknown40;  // +0x40
    int32_t delay;       // +0x44 os quadros desenhados antes do alfa (0: 1)
    uint32_t alpha;      // +0x48 o byte de baixo: o alfa dos vértices
    uint32_t index;      // +0x4c
};
static_assert(sizeof(DecalType) == 0x50);

// RDecalManager (0x298 bytes no PS2).
struct RDecalManagerData {
    uint32_t vtable;         // +0x00 0x38b840
    uint32_t unknown04[3];
    DecalType types[8];      // +0x10 o decal.dat (RDecalLibrary::LoadData)
    uint32_t unknown290;
    int32_t next;            // +0x294 a próxima marca do anel (0..95)
};
static_assert(sizeof(RDecalManagerData) == 0x298);

constexpr int kDecalCount = 96;
// Os vetores globais do PS2 (o que os oito DrawArray desenham).
struct DecalBuffers {
    float coords[kDecalCount * 6][4];      // 0x3da420
    float stq[kDecalCount * 6][4];         // 0x3d8020
    uint32_t colours[kDecalCount * 6];     // 0x3d7720 (RGBA do GS)
    uint8_t delay[kDecalCount];            // 0x3dc820
    uint8_t alpha[kDecalCount];            // 0x3dc880
};

// O do jogo (RDecalManager::fgThis 0x334bc0) e os vetores; `ready` depois do construtor.
struct DecalState {
    RDecalManagerData m{};
    DecalBuffers b{};
    bool ready = false;
};
DecalState& decal_state();

// [driving] 0x001da030 RDecalManager::RDecalManager (com o RDecalLibrary::LoadData 0x001da3b0: `decal_dat` o arquivo,
// 0x280 bytes; vazio ou menor: os tipos zerados, como o PS2 sem o arquivo)
void RDecalManager_Construct(DecalState& s, const std::vector<uint8_t>& decal_dat);
// [driving] 0x001da6c8 RDecalManager::AddDecal(COORD4 &pos, COORD4 &normal, COORD4 &dir, DecalType, float, float)
void RDecalManager_AddDecal(DecalState& s, const float pos[4], const float normal[4], const float dir[4], int32_t type,
                            float w, float h);
// [driving] 0x001da978 RDecalManager::Draw: os atrasos (o desenho dos oito DrawArray fica com o nf_driving)
void RDecalManager_Draw(DecalState& s);
// [driving] 0x001eb078 RScorchWorld::ScorchGround(WWorldPos &, COORD4 &, float, DecalType): a marca no chão sob `pos`;
// `wc` o mundo (nulo: nenhuma face, nada). Devolve se pôs a marca.
bool RScorchWorld_ScorchGround(DecalState& s, WorldCollision* wc, WWorldPos& wp, const float pos[4], int32_t type,
                               float size);

// O que o port conta.
struct DecalStats {
    uint64_t added = 0, scorches = 0, refused_face = 0, refused_slope = 0, refused_edges = 0, refused_size = 0;
    uint64_t draws = 0, guards = 0;
};
DecalStats& decal_stats();

}  // namespace drv
