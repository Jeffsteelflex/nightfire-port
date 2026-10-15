#pragma once
// A família dos render methods de pele dos personagens, lida no microprograma da VU1 (`python tools/vudis.py --eagl
// bondrm.o BondLitTextureAndShadow_Skin`; docs/driving.md seção 20): BondLitTextureAndShadow_Skin (100 lotes nos
// personagens dos nove arquivos) e BondLitTextureAndShadowSpecular_Skin (22; com o especular e o reflexo do carro).
//
// A memória da VU1: os pesos ("weights") e a paleta ("PalletMatrices") ficam em endereços absolutos, fora do buffer
// duplo (os pesos a partir de 4, a paleta a partir de 0x22: as matrizes que a operação 0xb do GeoPrim::Draw manda,
// o EAGLAnimationBuffer de cada osso vezes a escala do modelo); o resto é relativo ao TOP, que o microprograma só
// conhece em dois valores (0xf4 e 0x27a: o código está escrito duas vezes, com os endereços absolutos de cada metade).
//
// O que o microprograma faz com cada lote:
//  1. a paleta misturada, uma vez por paleta (o w do qword absoluto 4 é a bandeira: zero a cada envio dos pesos): copia
//     as `z` matrizes da paleta (Parameters.z) para o rascunho em TOP + saída, e para cada uma das `w` entradas dos pesos
//     (Parameters.w) faz M = P[a] x pa + P[b] x pb + P[c] x pc (as quatro linhas, MULAx/MADDAy/MADDz), com os ossos
//     a, b, c nos 8 bits de baixo das palavras x, y, z da entrada (o deslocamento em qwords: 4 x a posição na paleta) e
//     os pesos nas mesmas palavras como float, limpas pelo truque do + 768 - 768 (ADDi/SUBi: o ulp de 768 é 2^-14).
//     A matriz i fica em 0x22 + 7i (por cima da paleta, que já está no rascunho); no fim a bandeira recebe `w`;
//  2. os vértices únicos (Parameters.y): a posição (as Coordinates: floats em [1, 2) com 16 bits de mantissa; a
//     palavra w traz nos 16 bits de baixo o deslocamento 7i da matriz misturada, negativo = a mesma do vértice
//     anterior), a normal (Normals) e o STQ (s e t inteiros com 12 bits de fração; z e w os dois lugares de saída, em
//     qwords, 3 x o índice) passam pela matriz (a normal só pelas três primeiras linhas; o w da posição sai da linha 3)
//     e vão para os dois lugares no rascunho (STQ, normal, posição; ITOF12 do STQ, z = 1, w = 0); depois os
//     Parameters.x vértices de saída voltam às áreas de entrada (posição, normal, STQ);
//  3. a cor: se o CharacterOptions.z (o ActManager + 0x48: a sombra) não é zero, (0, 0, 0, alfa) e só a passada base;
//     senão (127, 127, 127, alfa) iluminada como a família MatCar (as três luzes direcionais do GAME::ActorLightSources
//     pelas linhas normalizadas da WorldMatrix, a cor 1 x difusa e o ambiente x prop. ambiente, vezes a cor, MINI 255,
//     FTOI0 nas quatro pistas) e as quatro luzes pontuais (GAME::PositionalLights, a posição pela WorldMatrix inteira),
//     com o alfa o w do CharacterOptions (ITOF0 e FTOI0 de volta);
//  4. a transformação como no BondCarShadow: clip = posição x MyMatrix (o w da posição entra), Q = 1/w, a tela, o STQ x
//     Q, XYZF = FTOI4 com o w 0, ou 2049 (vf00.w + vf31.w, este 1 + 2047) quando o FCAND 0x3ffff dos três últimos
//     CLIPw dá algo: 0x8010, com o ADC, e o GS não desenha o triângulo fora do volume que esse vértice fecha (o
//     recorte do microprograma é esse); os SplitReg gravam 0x8000 no w do XYZF do vértice da entrada e do seguinte (o
//     recomeço da faixa); XGKICK do State, do TAR do Texture e dos vértices;
//  5. (só o Specular, sem a sombra) a passada 2, o especular: E = WorldMatrix x SpecularMat (as quatro linhas, guardadas
//     em TOP + 0xc7), as linhas 0-2 normalizadas (ERLENG) e vezes o ReflectionInfo2.z; STQ = (0,5 + E0 n.x + E1 n.y +
//     E2 n.z).xy com z = w = 1, cor.xyz = FTOI0(128 x props.z); cópia do STQ e da cor para a saída (o XYZF fica o da
//     passada 1) e XGKICK do EnvMapState, do TAR do SpecularMap e dos vértices; a passada 3, o reflexo: as linhas 0-2 da
//     WorldMatrix normalizadas e vezes o ReflectionInfo2.y, a = W0 n.x + W1 n.y + W2 n.z, STQ = (0,5 + a.x, 0,5 + a.z,
//     1, 1), cor.xyz = FTOI0(min(128 x props.w, 100000 x max(a.y, 0))); XGKICK do EnvMapState, do ReflectionMap2 e dos
//     vértices.
//
// A referência abaixo refaz tudo isso numa cópia da memória da VU com a aritmética da VU (platform/ps2_float.h), na ordem
// do código, gravando nos mesmos endereços (os casos estranhos dos dados, como um lugar de saída que nenhum vértice
// único escreve, saem iguais). Divergências documentadas: os laços do microprograma que contam até zero (do ... while)
// rodariam 65.536 vezes com uma contagem 0 e escreveriam a memória toda; a referência não roda esses laços e conta
// (`guards`). A versão do desenho no PC (char_decode_batch e o vertex shader do nf_driving) refaz os passos 1-5 por
// vértice com as matrizes do quadro (o esqueleto posado), sem o rascunho.

#include <cstdint>
#include <vector>

#include "driving/eagl_state.h"
#include "driving/mat_car.h"
#include "driving/mat_car_verify.h"
#include "driving/vif.h"

namespace drv {

enum CharKind { kCharLit = 0, kCharLitSpec = 1 };
enum CharPass { kCharPassBase = 1, kCharPassSpec = 2, kCharPassRefl = 4 };
constexpr int kCharMaxPasses = 3;

struct CharMethodLayout {
    const char* name;
    int kind;
    int passes;     // máscara de CharPass (sem a sombra)
    int max_verts;
    // Relativos ao TOP (qwords); 0 onde o método não tem.
    int params, model, mvp, world, modelview, state, env_state, lights, point_lights, options, tar, tar_spec, tar_refl,
        props, refl_info, spec_mat, split, pos, stq, normal, e_scratch, colour, gif_tag, out;
    int top0 = 0xf4, top1 = 0x27a;  // os dois TOPs que o código conhece (os endereços absolutos de cada cópia)
    int weights = 4, palette = 0x22;  // absolutos
};
// A família pelo nome do render method (nullptr se não é dela).
const CharMethodLayout* char_method_layout(const char* method_name);

// A referência na CPU (passos 1-5) para o lote no TOP `top` da memória `mem`: out[passada][vértice] (só a base com a
// sombra ou no método sem especular). `guards` conta os laços com contagem zero que não rodaram.
void char_reference(const CharMethodLayout& l, const VuMemory& mem, uint32_t top, std::vector<CarVertexOut> out[kCharMaxPasses],
                    int* guards = nullptr);

// A conferência contra o microprograma original (como mat_car_verify.h): o código roda no interpretador da VU1 a partir
// do par 2 sobre a memória do lote, e cada pacote de vértices chutado é comparado com char_reference. O lote roda de novo
// com outros valores na cópia da memória (`variantes`): uma câmera em perspectiva, uma WorldMatrix girada, as luzes
// pontuais acesas e a sombra (CharacterOptions.z = 1).
void char_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CharMethodLayout& l,
                       CarVerifyStats& s);

// Um vértice de saída como o vertex shader do PC o recebe: a posição e a normal antes da paleta, o ST em float, os três
// ossos do esqueleto (o EAGLAnimationBuffer; -1 nenhum) e os três pesos limpos.
struct CharVertex {
    float pos[4];      // a posição de entrada (o w é 1 no desenho: o do arquivo carrega o índice da matriz)
    float normal[3];
    float st[2];
    int32_t bone[3];
    float weight[3];
};
static_assert(sizeof(CharVertex) == 60);

struct CharBatch {
    const CharMethodLayout* layout = nullptr;
    int count = 0;
    bool strip = true;
    int split[12] = {};
    float props[4] = {}, refl_info[4] = {};
    uint32_t options[4] = {};          // o CharacterOptions (o alfa no w)
    float lights[7][4] = {};           // o GAME::ActorLightSources do lote
    float point_lights[8][4] = {};     // o GAME::PositionalLights
    eagl::GsRegs pass[kCharMaxPasses];
    std::vector<CharVertex> v;
    int guards = 0;                    // lugares de saída sem vértice, índices de matriz fora da tabela
};
// Decodifica o lote para o desenho do PC: `bones` = os ossos da paleta da VU na ordem (a lista da operação 0xb).
bool char_decode_batch(const VuMemory& mem, uint32_t top, const CharMethodLayout& l, const std::vector<int32_t>& bones,
                       CharBatch& out);
// Os triângulos de um lote (a regra do GS, como car_triangles).
void char_triangles(const CharBatch& b, std::vector<uint16_t>& tris);

}  // namespace drv
