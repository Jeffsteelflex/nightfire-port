#pragma once
// A família MatBond dos render methods do mundo, lida no microprograma da VU1 (`python tools/vudis.py --eagl bondrm.o
// MatBondTexturePacked`; docs/driving.md seção 10): o que as pistas usam. Em Paris (paris_mis01.crp, 7.541 lotes)
// MatBondReflectTexturePacked são 4.494 lotes (64% dos triângulos) e MatBondTexturePacked 2.766 (32%); MatBondTexture
// (a mesma conta com as entradas em float) 107.
//
// O que o microprograma faz com cada lote (até 72 vértices; 60 no Reflect):
//  1. (só "Packed") converte os fluxos: posição = u16 x (1/256) - 128 (ITOF0, MULAi, MADDw por -128), w = 1;
//     STQ s,t = u16 x (1/256) - 128, z = 1, w = 0;
//  2. (só o Reflect) o mapa de ambiente: c = CameraPos (TOP+0x17) - (0, 5, 0); d = (p x WorldMatrix com a linha 3
//     menos c), d *= ERLENG(d) (1 / |d|), d *= 0,4375; STQ do ambiente = (0,5 + d.x, 0,5 + d.z, 1);
//  3. a cor: a do vértice (Colours: u8 x 4, inteiros) + as quatro luzes pontuais (VU 0x40-0x47, como a família
//     MatCar: att = max(0, 255 - K |P - p|^2) com p = posição no mundo), limitada a 255 e truncada (FTOI0); o alfa
//     fica o inteiro do arquivo;
//  4. a transformação: clip = p x MyMatrix (TOP+1..4), Q = 1/clip.w, tela = VU 0 + clip.xyz x VU 1 x Q, névoa
//     F = 255 - VU48.w x (clamp(clip.w, VU48.x, VU48.z) - VU48.x); STQ x Q; XYZF = FTOI4(tela, F); o ADC dos recomeços
//     de faixa (SplitReg1-3, o vértice x 3; w = (w & 0xbffe) | 0x8000) e o do recorte (triângulos fora do volume:
//     o microprograma os recorta à parte e os redesenha; no PC o hardware recorta);
//  5. XGKICK: no Texture, o State, o TAR da textura e os vértices; no Reflect, primeiro o GlossMapState, o TAR do
//     EnvironmentMap e os vértices com a STQ do ambiente, depois o State, o TAR da textura e os vértices de novo com a
//     STQ da textura (a mesma cor e a mesma posição).
// Sem luz direcional nem normais: a luz do mundo vem cozida nas cores dos vértices.

#include <cstdint>
#include <vector>

#include "driving/eagl_state.h"
#include "driving/mat_car.h"
#include "driving/mat_car_verify.h"
#include "driving/vif.h"

namespace drv {

enum BondPass { kBondBase = 1, kBondEnv = 2 };

struct BondMethodLayout {
    const char* name;
    int passes;     // máscara de BondPass
    bool packed;    // entradas u16 (o passo 1); senão float
    int max_verts;
    // Endereços relativos ao TOP (qwords); 0 onde o método não tem.
    int count, mvp, world, tar, tar_env, camera, split, pos, stq, colour, state, gloss_state, env_stq, gif_tag, out;
};
// A família pelo nome do render method (nullptr se não é dela).
const BondMethodLayout* bond_method_layout(const char* method_name);

// Um vértice como o VIF o deixou (as palavras de cada fluxo na memória da VU).
struct BondVertexIn {
    uint32_t pos[4];
    uint32_t stq[3];
    uint32_t col[4];
};

// Um vértice depois do passo 1 (o que o vertex shader do PC recebe).
struct BondVertexWork {
    float pos[4];  // w: 1 nos "Packed", o do arquivo nos outros
    float stq[3];  // s, t e a base do Q (1 nos "Packed")
    float col[4];  // os inteiros da cor (0..255; o alfa na escala do GS, 0x80 = 1)
};
static_assert(sizeof(BondVertexWork) == 44);

struct BondBatch {
    const BondMethodLayout* layout = nullptr;
    int count = 0;
    float mvp[4][4];
    float world[4][4];
    float camera[4];        // CameraPos (só o Reflect)
    eagl::GsRegs pass[2];   // [0] a base (State + Texture), [1] o ambiente (GlossMapState + EnvironmentMap)
    bool strip = true;
    int split[12];
    std::vector<BondVertexIn> v;
};
bool bond_decode_batch(const VuMemory& mem, uint32_t top, const BondMethodLayout& layout, BondBatch& out);
BondVertexWork bond_convert(const BondMethodLayout& layout, const BondVertexIn& v);

// A referência na CPU do microprograma (passos 1-4) para um lote: out[0] a passada da textura, out[1] a do ambiente
// (vazia sem ela). Divergência documentada: o ADC do recorte não é posto (o PC recorta); o da faixa é.
void bond_reference(const BondBatch& b, const CarConstants& k, std::vector<CarVertexOut> out[2]);
// A STQ do ambiente (passo 2) de um vértice já convertido, no referencial do lote (world, camera).
void bond_env_stq(const float world[4][4], const float camera[4], const float pos[4], float out[3]);
// Os triângulos de um lote (a regra do GS, como car_triangles).
void bond_triangles(const BondBatch& b, std::vector<uint16_t>& tris);

// A conferência contra o microprograma original (como mat_car_verify.h): o código roda no interpretador da VU1 a
// partir do par 2, e cada pacote de vértices chutado é comparado com bond_reference.
void bond_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const BondBatch& b,
                       CarVerifyStats& s);

}  // namespace drv
