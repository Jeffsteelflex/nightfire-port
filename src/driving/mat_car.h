#pragma once
// A família de render methods texturizados dos carros, lida no microprograma da VU1
// (`python tools/vudis.py --eagl bondrm.o MatCarTexturePacked`; docs/driving.md seção 10):
//  - MatCarTexturePacked e MatCarChrome: o mesmo microprograma, byte a byte; entradas "Packed" (u16/u8) e três
//    passadas (base, especular, reflexo);
//  - MatCarTextureDullPacked: o mesmo código com outro leiaute e só a passada base;
//  - MatCarDash: o mesmo código sem o passo 1 (as entradas já vêm em float, UNPACK V4-32), base e especular.
// No Vanquish são 360 dos 389 lotes; o vidro (MatCarGlassPacked, abaixo) mais 5 e as luzes (MatCarTextureSwitchPacked,
// abaixo) mais 19; o resto vai ao substituto (MatCarGouraudDullPacked; BondCarShadow).
//
// O que o microprograma faz com cada lote (até 44 vértices) que o VIF põe no buffer TOP:
//  0. preenche a cor de trabalho (TOP+cor) com (127, 127, 127, 128) inteiros: sem cor por vértice;
//  1. (só "Packed") converte os fluxos: STQ s,t = u16 x I1 - 32 (I1 = 0x3a7ffff7, um pouco menos que 1/1024), z = 1;
//     normal = u8 x I2 - 1 (I2 = 0x3c008080, pouco menos que 2/255); vetor de dano = u16 x I1 - 32 (4 componentes);
//     normal de dano = u16 x I1 - 32 (xyz; o w fica o inteiro do UNPACK: o índice da zona de dano);
//  2. dano: z = DamageZones[zona] (VU 0x56 + zona); posição += vetor.xyz x z.x; normal += normal_de_dano x z.x;
//     cor.rgb = int(max(0, 127 x (1 - vetor.w x z.y))); alfa_f = max(0, 128 x (1 - vetor.w x z.z)) guardado em
//     float no w do vetor de dano (o carro inteiro tem z = 0: nada muda);
//  3. três luzes direcionais: as linhas 0-2 da WorldMatrix normalizadas (ERLENG), A_k = L0 r_k.x + L1 r_k.y + L2 r_k.z
//     com L = VU 0x49-0x4b (as direções transpostas), d = max(0, A0 n.x + A1 n.y + A2 n.z); cor.rgb =
//     min(255, cor x (C0 x props.y x d.x + C1 d.y + C2 d.z + Amb x props.x)), C = VU 0x4c-0x4e, Amb = 0x4f;
//  4. quatro luzes pontuais (sem a normal): p = posição no mundo, att = max(0, 255 - K x |P - p|^2) com P e K em
//     VU 0x40-0x43 (SoA: x, y, z, K), cor.rgb = min(255, cor + C0 att.x + ... + C3 att.w), C = 0x44-0x47;
//  5. transformação: clip = p x MVP (as linhas TOP+1..4), Q = 1/clip.w, tela = VU 0 + clip.xyz x VU 1 x Q, névoa
//     F = 255 - VU48.w x (clamp(clip.w, VU48.x, VU48.z) - VU48.x); STQ = (s Q, t Q, Q); XYZF = FTOI4(tela, F); o ADC
//     (bit 15 do w) liga em triângulos com os três vértices fora do mesmo plano ou além de +z (o recorte) e nos dois
//     primeiros vértices de cada faixa (as 12 entradas "SplitReg": o vértice x 3 de cada recomeço);
//     passada 1: XGKICK do State, do TAR da textura e dos vértices;
//  6. (TexturePacked/Chrome/Dash) passada 2, o especular: E = WorldMatrix x SpecularMat (VU 0x50-0x53), linhas x
//     ReflectionInfo2.z (VU 0x55); ST = 0.5 + (E0 n.x + E1 n.y + E2 n.z).xy, Q = 1; cor = int(props.z x alfa_f);
//     XGKICK do EnvMapState (no Dash, SpecMapState), do TAR SpecularMap e dos vértices (o mesmo XYZF);
//  7. (TexturePacked/Chrome) passada 3, o reflexo: v = (posição x VU55.x + normal), a = (W0 v.x + W1 v.y + W2 v.z) x
//     VU55.y (W = WorldMatrix sem a translação); ST = (0.5 + a.x, 0.5 + a.z), Q = 1;
//     cor = int(min(props.w x alfa_f, 100000 x max(a.y, 0))); XGKICK do EnvMapState, do TAR ReflectionMap2 e dos
//     vértices.
//
// MatCarGlassPacked (o vidro, `vudis.py --eagl bondrm.o MatCarGlassPacked`): os passos 0-5 iguais (outro leiaute, até 40
// vértices), com três diferenças e quatro passadas:
//  - o alfa da cor de trabalho é a palavra w da zona de dano do vértice, crua (LQ.w + SQI.w): o RVehicle::ComputeDamage
//    (0x001d3138) a põe em 127 (o inteiro, 1.77965e-43 em float) quando o dano da zona passa do limite (o vidro
//    quebrado), senão 0;
//  - depois da transformação, a face de trás: n' e p' pela ModelView (TOP+0x1f..0x22: n' = M0 n.x + M1 n.y + M2 n.z,
//    p' = M0 p.x + M1 p.y + M2 p.z + M3), d = ((n'.z p'.z + n'.x p'.x) + n'.y p'.y); com d >= 0 (o vértice de costas
//    para a câmera: o sinal do MAC do z apagado) o w do XYZF vira 0x8000 (o ADC, a névoa zerada): o triângulo que esse
//    vértice fecha não é desenhado. Só então o AND 0xbffe dos SplitReg;
//  - passada 1: o GlassDmgState (TOP+0xfd; o State em 0xf5 não é chutado) e o TAR do DmgTexture (0x23), o alfa w;
//    passada 2: o mesmo estado, o TAR do Texture (0x0a), o alfa int(127 - w) (ITOF0, x -1, + 127, FTOI0), o STQ da
//    passada 1; passadas 3 e 4: o especular e o reflexo da família (EnvMapState 0x105, SpecularMap 0x11,
//    ReflectionMap2 0x18) com o alfa 127 - w. Sem dano: o DmgTexture com alfa 0 e o Texture com 127; quebrado, o inverso.
//    (Um laço entre as passadas 2 e 3 faz min(rgb, alfa) com o xyz de registradores velhos (ITOF0.w só no w); o xyz que
//    ele grava é sobrescrito pelo especular antes de qualquer XGKICK: sem efeito.)
//
// MatCarTextureSwitchPacked (as luzes do carro): o MatCarTextureDullPacked com outro leiaute e um passo entre o 4 e o 5:
// com s = SwitchData (TOP+0x11: a cópia do GAME::SwitchData[n] do lote; o RVehicle::SetPerObjectBuffers (0x001d2b38)
// grava o w de cada um a cada quadro, a partir do RVehicle +0x268) e k = SwitchParam (TOP+0xf1),
// alvo = (s.z, s.z, s.z, s.y) x 255 x k.x e cor = int(min(255, cor x (1 - s.w) + alvo x s.w)), nas quatro componentes
// (o alfa de partida 128 também: com s.w = 0, a lâmpada apagada, nada muda).
// MatCarGouraudDullPacked (as peças pintadas por vértice, sem textura: `vudis.py --eagl bondrm.o
// MatCarGouraudDullPacked`, docs/driving.md seção 16): o MatCarTextureDullPacked sem o STQ, com a cor de cada vértice de
// um ColourSet e a luz guardada à parte. O leiaute: Coordinates 0x0d, Normals 0x39, DmgVec 0x65, DmgNrm 0x91,
// ColourSlot 0xbd (a palavra x), ColourSet 0xbe (8 qwords de inteiros u8, a V4-8 do VIF), State 0xc6, a área de luz
// 0xce, as cores de trabalho 0xfa, a etiqueta GIF 0x126 e a saída 0x127 (STQ, RGBAQ, XYZF2, como na família). Passos:
//  1. a conversão dos fluxos da família, com o STQ lido de 0xce (.xy: o método não tem STQ; o que estiver ali vira
//     (x I1 - 32, y I1 - 32, 1, 0) e é gravado de volta em 0xce, para os `count` vértices);
//  2. a cor: um LQI só de 0xbe + slot (o ColourSlot escolhe uma das 8 cores do ColourSet: as variações de cor do carro)
//     e o laço 0x34-0x36 grava esse qword `count` vezes em 0xfa; o lote inteiro tem uma cor. Com um slot de 16 ou mais a
//     leitura cairia na área de luz convertida no passo 1 (bits de float lidos como inteiros); os dados do disco têm
//     slots de 0 a 7;
//  3. o dano da família sobre a cor copiada: rgb = FTOI0(max(0, ITOF0(c.rgb) x s_rgb)), alfa_f = max(0, ITOF0(c.w) x
//     s_a) gravado no w do DmgVec; o w da cor fica o cru;
//  4. 0xce recebe FTOI0(vf00 + 127) = (127, 127, 127, 128) e a luz direcional da família é multiplicada por esse 127
//     (não pela cor): luz = FTOI0(min(255, l x 127)) em 0xce (o w 128);
//  5. as luzes pontuais somadas à luz: v = min(255, luz + C0 att.x + ... + C3 att.w); cor.rgb = FTOI0((v x
//     ITOF0(cor.rgb)) x 1/128) em 0xfa;
//  6. a transformação da família; o RGBAQ é a cor de 0xfa (o alfa, o w cru do ColourSet) e o STQ é (0xce) x Q: os
//     inteiros da luz lidos como float são denormais, que a VU lê como zero (STQ = 0; o State não liga a textura).
// O trecho 0x109..0x126 do microprograma (a cor sem as luzes pontuais) não é alcançado: nenhum desvio leva a ele.
//
// BondCarShadow (a sombra do carro: `vudis.py --eagl bondrm.o BondCarShadow`): posição x MyMatrix, a cor do
// CarShadowColour (0xa8) em todos os vértices, XYZF2 = FTOI4(tela, w), w = 0 ou 2047 (quando algum dos três últimos
// CLIP deu fora: o FCAND 0x3ffff), os SplitReg (vértice x 2: a saída tem dois qwords por vértice, RGBAQ e XYZF2) gravam
// 0x8000 no w. Leiaute: SplitReg 5..7, Coordinates 8 (até 160), CarShadowColour 0xa8, State 0xac, etiqueta 0xb4, saída
// 0xb5. O jogo nunca o desenha: o RVehicle::RenderShadowGeometry e o RenderShadowVolume (o modelo 'as' do tipo 3 e 4)
// não têm quem os chame no DRIVING.ELF do PS2; a sombra que o PS2 desenha é o quadrado do RShadowMap::DrawSimpleShadow
// (driving/render/shadow.h). A referência existe para a conferência.
//
// As constantes I são as do microcódigo (o I de uma instrução com o bit I é o carregado na anterior: a instrução de cima
// executa antes do LOI do mesmo par). A referência abaixo faz as contas em float do IEEE na mesma ordem; a VU não tem
// denormais nem infinitos e arredonda para zero (diferenças no último bit, que os testes evitam com valores exatos). A
// cor do Gouraud usa a aritmética do PS2 (platform/ps2_float.h): os vértices de cor lixo levam inteiros enormes por ITOF0,
// MUL e FTOI0, onde o arredondamento do PC mudaria os bits que o GS lê.

#include <cstdint>
#include <vector>

#include "driving/eagl_state.h"
#include "driving/vif.h"

namespace drv {

// As passadas, na ordem dos XGKICK do microprograma (bit p = a passada p). Na família: 0 a base, 1 o especular, 2 o
// reflexo. No vidro: 0 o DmgTexture, 1 o Texture, 2 o especular, 3 o reflexo.
enum CarPass { kPassBase = 1, kPassSpec = 2, kPassRefl = 4, kPassGlassRefl = 8 };
constexpr int kCarMaxPasses = 4;
enum CarKind { kCarStd = 0, kCarGlass = 1, kCarSwitch = 2, kCarGouraud = 3, kCarShadow = 4 };

struct CarMethodLayout {
    const char* name;
    int passes;        // máscara de CarPass
    bool packed;       // entradas u16/u8 (o passo 1); senão float (V4-32)
    int max_verts;
    // Endereços relativos ao TOP (qwords).
    int count, mvp, world, props, tar, tar_spec, tar_refl, split, pos, stq, normal, dmgvec, dmgnrm, colour, gif_tag,
        out, state, env_state;
    int kind = kCarStd;
    int tar_dmg = 0;    // o vidro: o TAR do DmgTexture
    int modelview = 0;  // o vidro: a ModelView (a face de trás)
    int switch_data = 0, switch_param = 0;  // as luzes: SwitchData e SwitchParam
    int colour_slot = 0, colour_set = 0;    // o Gouraud: ColourSlot e ColourSet (a área de luz fica no "stq")
    int out_qw = 3;                         // qwords por vértice na saída (a sombra: 2)
};
// A família pelo nome do render method (nullptr se não é dela).
const CarMethodLayout* car_method_layout(const char* method_name);
// O STQ de uma passada: 0 o da carga x Q (a base), 1 o especular, 2 o reflexo.
int car_pass_stq(const CarMethodLayout& l, int pass);
// O alfa dos vértices de uma passada: 0 o 128 da cor de partida, 1 o w da zona de dano, 2 int(127 - w).
int car_pass_alpha(const CarMethodLayout& l, int pass);

// As constantes globais da VU1 que o microprograma lê (endereços absolutos), postas pelos métodos MatBondPerViewPort
// (PositionalLightSources 0x40-0x47, FogData 0x48) e MatCarPerCar (LightSources 0x49-0x4f, SpecularMat 0x50-0x53,
// ReflectionInfo1/2 0x54/0x55, DamageZones 0x56-0x65) e pelo EAGL (a janela de vista em 0 e 1).
struct CarConstants {
    float vp_offset[4] = {2048, 2048, 0, 0};       // VU 0
    float vp_scale[4] = {320, -224, -16777215, 0};  // VU 1
    float point_pos[4][4] = {};                      // VU 0x40-0x43: x, y, z (4 luzes) e K
    float point_col[4][4] = {};                      // VU 0x44-0x47
    float fog[4] = {0, 0, 1e9f, 0};                  // VU 0x48: x = w perto, z = w longe, w = escala
    float light_dir_t[3][4] = {};                    // VU 0x49-0x4b: as direções das 3 luzes, transpostas
    float light_col[3][4] = {};                      // VU 0x4c-0x4e
    float ambient[4] = {};                           // VU 0x4f
    float spec_mat[4][4] = {};                       // VU 0x50-0x53
    float refl_info1[4] = {};                        // VU 0x54
    float refl_info2[4] = {};                        // VU 0x55: x, y, z (ver acima)
    float damage[16][4] = {};                        // VU 0x56-0x65
    void write(VuMemory& mem) const;
};

// Um vértice como o VIF o deixou (antes do passo 1): as palavras de cada fluxo como estão na memória da VU (inteiros
// das UNPACK u16/u8 com USN, ou os bits dos floats das V4-32).
struct CarVertexIn {
    uint32_t pos[3];
    uint32_t stq[3];
    uint32_t normal[3];
    uint32_t dmgvec[4];
    uint32_t dmgnrm[4];  // w = índice da zona de dano (inteiro nos dois formatos)
    uint32_t colour[4] = {0, 0, 0, 0};  // o Gouraud: a cor que a cópia do passo 2 lê (as palavras cruas)
};

// Um vértice depois do passo 1 (o que o vertex shader do PC recebe).
struct CarVertexWork {
    float pos[3];
    float stq[3];   // s, t e a base do Q (1)
    float normal[3];
    float dmgvec[4];
    float dmgnrm[3];
    uint32_t zone;
    uint32_t colour[4];  // o Gouraud: a cor crua do ColourSet (inteiros; lixo nos vértices além do ColourSet)
};
static_assert(sizeof(CarVertexWork) == 84);

// Um lote decodificado da memória da VU1 num MSCNT.
struct CarBatch {
    const CarMethodLayout* layout = nullptr;
    int count = 0;
    float mvp[4][4];      // linhas, como a VU as lê (p' = p x M)
    float world[4][4];
    float props[4];       // LightingProperties: (ambiente, difusa, especular, reflexo)
    float modelview[4][4] = {};  // o vidro: a ModelView
    float switch_data[4] = {}, switch_param[4] = {};  // as luzes
    uint32_t shadow_colour[4] = {};  // a sombra: o CarShadowColour (as palavras inteiras do qword 0xa8)
    eagl::GsRegs pass[kCarMaxPasses];  // o estado de cada passada (o GeoPrimState e o TAR que o microprograma chuta)
    bool strip = true;     // PRIM do State: 4 faixa, 3 lista
    int split[12];        // as entradas SplitReg (vértice x 3)
    std::vector<CarVertexIn> v;
};
bool car_decode_batch(const VuMemory& mem, uint32_t top, const CarMethodLayout& layout, CarBatch& out);
// O Gouraud: as cores que a cópia do passo 2 lê (CarVertexIn::colour), com o passo 1 aplicado à área de luz.
void car_gouraud_colours(const VuMemory& mem, uint32_t top, const CarMethodLayout& layout, CarBatch& b);
// O Gouraud: a cor de um vértice (passos 3-5, na aritmética do PS2): `colour` cru, a escala do dano s_rgb, as linhas
// A (a WorldMatrix normalizada vezes as direções), a normal com o dano, a posição no mundo; devolve RGBA como a VU grava
// (o w cru).
void car_gouraud_vertex(const uint32_t colour[4], float s_rgb, const float A[3][3], const float n[3], const float pw[3],
                        const float props[4], const CarConstants& k, int32_t rgba[4]);

// O passo 1 de um vértice (ou só a leitura dos floats, nos métodos sem "Packed").
CarVertexWork car_convert(const CarMethodLayout& layout, const CarVertexIn& v);

// A saída de um vértice numa passada, como o microprograma a grava no buffer de saída.
struct CarVertexOut {
    float stq[3];      // s, t, q
    int32_t rgba[4];   // inteiros (0x80 = 1,0 no GS)
    int32_t xyzf[4];   // FTOI4: x, y, z e a névoa F << 4; o bit 15 do w é o ADC (só o das faixas, ver acima)
    bool adc;          // o recomeço de faixa (SplitReg) ou, no vidro, a face de trás; o descarte por recorte não é
                       // reproduzido (ver car_reference)
};

// A referência na CPU do microprograma (passos 0-7) para um lote: out[passada][vértice] (vazio nas passadas que o
// método não tem). Divergência documentada: o descarte de triângulos por recorte (CLIPw e os testes de FCAND/FCOR) não é
// feito; no PC o hardware recorta. Os demais valores saem como o microprograma os grava.
void car_reference(const CarBatch& b, const CarConstants& k, std::vector<CarVertexOut> out[kCarMaxPasses]);

// As luzes: a cor (as quatro componentes, inteiras) depois do passo do SwitchData (ver acima).
void car_switch_colour(const float switch_data[4], const float switch_param[4], int32_t col[4]);

// O vidro: o vértice está de costas para a câmera da ModelView (o ADC que o microprograma põe; ver acima).
bool car_glass_backface(const float modelview[4][4], const float pos[3], const float normal[3]);

// Os triângulos de um lote (índices dentro do lote) pela regra do GS: numa faixa, cada vértice a partir do terceiro
// fecha um triângulo, menos os que têm ADC; numa lista, de três em três. Com `closing_first` o vértice que fecha vem
// primeiro (v, v - 2, v - 1: a mesma volta): o vértice provocador do Direct3D, que decide a face de trás do vidro.
void car_triangles(const CarBatch& b, std::vector<uint16_t>& tris, bool closing_first = false);

}  // namespace drv
