#pragma once
// A família MatBond dos render methods do mundo, lida no microprograma da VU1 (`python tools/vudis.py --eagl bondrm.o
// MatBondTexturePacked`; docs/driving.md seções 10, 13 e 15): o que as pistas usam. Em Paris (paris_mis01.crp, 7.541
// lotes) MatBondReflectTexturePacked são 4.494 lotes (64% dos triângulos) e MatBondTexturePacked 2.766 (32%);
// MatBondTexture (a mesma conta com as entradas em float) 107, MatDynamicTexturePacked 94, MatBondScrollTexture2 e
// MatBondScrollTexture2Packed 38 cada (o céu de nuvens é um deles) e MatBondFrameAnimPacked 4.
//
// O que o microprograma faz com cada lote (até 72 vértices; 60 no Reflect, no Dynamic e nos Scroll2):
//  1. (só "Packed") converte os fluxos: posição = u16 x (1/256) - 128 (ITOF0, MULAi, MADDw por -128), w = 1;
//     STQ s,t = u16 x (1/256) - 128, z = 1, w = 0 (a segunda STQ dos Scroll2Packed NÃO é convertida: fica a palavra
//     que o VIF deixou, lida como float);
//  2. (só o Reflect) o mapa de ambiente: c = CameraPos (TOP+0x17) - (0, 5, 0); d = (p x WorldMatrix com a linha 3
//     menos c), d *= ERLENG(d) (1 / |d|), d *= 0,4375; STQ do ambiente = (0,5 + d.x, 0,5 + d.z, 1);
//  2b. (só o Dynamic) a cor de partida (127, 127, 127, 128: vf00 + 127) e as três luzes do LightSources do lote (TOP+0x10..0x16: as
//     direções L transpostas nas linhas 0-2, as cores C0-C2 e o ambiente C3; GAME::LightSources, o bloco do
//     RLightManager): M = linhas de WorldMatrix (xyz) x L, d = max(0, n x M) com n a normal (TOP+0x92, float),
//     cor.rgb = int(min(255, 127 x (C0 d.x + C1 d.y + C2 d.z + C3))), alfa 128;
//  3. a cor: a do vértice (Colours: u8 x 4, inteiros; no Dynamic a do passo 2b) + as quatro luzes pontuais (VU
//     0x40-0x47, como a família MatCar: att = max(0, 255 - K |P - p|^2) com p = posição no mundo), limitada a 255 e
//     truncada (FTOI0); o alfa fica o inteiro;
//  3b. (Scroll2) a STQ += a fração de (ScrollUV x t) (TOP+0x11b, t = SimStep.x, TOP+0x17: MULx.xy, FTOI0, ITOF0, SUB);
//     (FrameAnim) o t da STQ += FrameAnim.y x int(frac(FrameAnim.x x (t - SimStep.w)) x FrameAnim.z) (TOP+0x10 o
//     SimStep, 0xf4 o FrameAnim: a tira de quadros da textura);
//  4. a transformação: clip = p x MyMatrix (TOP+1..4), Q = 1/clip.w, tela = VU 0 + clip.xyz x VU 1 x Q, névoa
//     F = 255 - VU48.w x (clamp(clip.w, VU48.x, VU48.z) - VU48.x); STQ x Q; XYZF = FTOI4(tela, F); o ADC dos recomeços
//     de faixa (SplitReg1-3, o vértice x 3; w = (w & 0xbffe) | 0x8000) e o do recorte (triângulos fora do volume:
//     o microprograma os recorta à parte e os redesenha; no PC o hardware recorta);
//  5. XGKICK: no Texture, Dynamic e FrameAnim, o State, o TAR da textura e os vértices; no Reflect, primeiro o
//     GlossMapState, o TAR do EnvironmentMap e os vértices com a STQ do ambiente, depois o State, o TAR da textura e os
//     vértices de novo com a STQ da textura (a mesma cor e a mesma posição); nos Scroll2, o State, o TAR do Texture e os
//     vértices, e de novo (passos 3b e 4 refeitos com a STQ1 e o ScrollUV1) o State (o State1 nunca é chutado), o TAR do
//     Texture1 e os vértices.
// Sem luz direcional nem normais (salvo o Dynamic): a luz do mundo vem cozida nas cores dos vértices.

#include <cstdint>
#include <vector>

#include "driving/eagl_state.h"
#include "driving/mat_car.h"
#include "driving/mat_car_verify.h"
#include "driving/vif.h"

namespace drv {

enum BondPass { kBondBase = 1, kBondEnv = 2 };  // a passada 2: o ambiente (Reflect) ou o Texture1 (Scroll2)
enum BondKind { kBondTexture = 0, kBondReflect = 1, kBondDynamic = 2, kBondScroll2 = 3, kBondFrameAnim = 4 };

struct BondMethodLayout {
    const char* name;
    int passes;     // máscara de BondPass
    bool packed;    // entradas u16 (o passo 1); senão float
    int max_verts;
    // Endereços relativos ao TOP (qwords); 0 onde o método não tem.
    int count, mvp, world, tar, tar_env, camera, split, pos, stq, colour, state, gloss_state, env_stq, gif_tag, out;
    int kind = kBondTexture;
    int lights = 0;      // Dynamic: o LightSources (7 qwords)
    int normal = 0;      // Dynamic: as normais (float)
    int sim_step = 0;    // Scroll2, FrameAnim: o SimStep
    int stq1 = 0;        // Scroll2: a segunda STQ
    int scroll = 0;      // Scroll2: ScrollUV (e ScrollUV1 no qword seguinte)
    int frame_anim = 0;  // FrameAnim
};
// A família pelo nome do render method (nullptr se não é dela).
const BondMethodLayout* bond_method_layout(const char* method_name);

// Um vértice como o VIF o deixou (as palavras de cada fluxo na memória da VU).
struct BondVertexIn {
    uint32_t pos[4];
    uint32_t stq[3];
    uint32_t col[4];
    uint32_t normal[3] = {0, 0, 0};       // Dynamic
    uint32_t stq1[3] = {0, 0, 0};         // Scroll2 (cru)
};

// Um vértice depois do passo 1 (o que o vertex shader do PC recebe).
struct BondVertexWork {
    float pos[4];   // w: 1 nos "Packed", o do arquivo nos outros
    float stq[3];   // s, t e a base do Q (1 nos "Packed")
    float col[4];   // os inteiros da cor (0..255; o alfa na escala do GS, 0x80 = 1)
    float stq1[3];  // Scroll2: a segunda STQ (as palavras cruas como float)
    float nrm[3];   // Dynamic: a normal
};
static_assert(sizeof(BondVertexWork) == 68);

struct BondBatch {
    const BondMethodLayout* layout = nullptr;
    int count = 0;
    float mvp[4][4];
    float world[4][4];
    float camera[4];        // CameraPos (só o Reflect)
    eagl::GsRegs pass[2];   // [0] a base (State + Texture), [1] o ambiente (GlossMapState + EnvironmentMap) ou o Texture1
    bool strip = true;
    int split[12];
    float lights[7][4] = {};     // Dynamic: o LightSources
    float sim_step[4] = {};      // Scroll2, FrameAnim
    float scroll[2][2] = {};     // Scroll2: ScrollUV e ScrollUV1 (xy)
    float frame_anim[4] = {};    // FrameAnim
    std::vector<BondVertexIn> v;
};
bool bond_decode_batch(const VuMemory& mem, uint32_t top, const BondMethodLayout& layout, BondBatch& out);
BondVertexWork bond_convert(const BondMethodLayout& layout, const BondVertexIn& v);

// A referência na CPU do microprograma (passos 1-4) para um lote: out[0] a passada da textura, out[1] a segunda (o
// ambiente ou o Texture1; vazia sem ela). Divergência documentada: o ADC do recorte não é posto (o PC recorta); o da
// faixa é.
void bond_reference(const BondBatch& b, const CarConstants& k, std::vector<CarVertexOut> out[2]);
// A STQ do ambiente (passo 2) de um vértice já convertido, no referencial do lote (world, camera).
void bond_env_stq(const float world[4][4], const float camera[4], const float pos[4], float out[3]);
// O passo 3b, com a aritmética da VU: o deslocamento da STQ dos Scroll2 (a fração de ScrollUV x t, xy) e o do t da
// STQ do FrameAnim. O renderizador do PC os calcula por desenho a cada quadro.
void bond_scroll_offset(const float scroll_uv[2], float t, float out[2]);
float bond_frame_offset(const float frame_anim[4], const float sim_step[4]);
// O passo 2b: a cor (inteiros, o alfa 128) de um vértice do Dynamic.
void bond_dynamic_colour(const float lights[7][4], const float world[4][4], const float normal[3], int32_t out[4]);
// Os triângulos de um lote (a regra do GS, como car_triangles).
void bond_triangles(const BondBatch& b, std::vector<uint16_t>& tris);

// A conferência contra o microprograma original (como mat_car_verify.h): o código roda no interpretador da VU1 a
// partir do par 2, e cada pacote de vértices chutado é comparado com bond_reference. O Dynamic, os Scroll2 e o
// FrameAnim rodam de novo com outros valores na cópia da memória (`variantes`: outras luzes; o SimStep de um jogo
// longo), para a conferência passar pelas contas que o valor da carga (o quadro 0) deixa triviais.
void bond_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const BondBatch& b,
                       CarVerifyStats& s);

}  // namespace drv
