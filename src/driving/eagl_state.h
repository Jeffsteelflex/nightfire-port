#pragma once
// O estado do GS de uma primitiva do EAGL (EAGL::GeoPrimState) e as texturas (EAGL::TAR), portados do DRIVING.ELF
// (docs/driving.md seção 10).
//
// GeoPrimState: um pacote GIF de 8 qwords que o render method sobe para a VU1 e o microprograma chuta (XGKICK) antes
// dos vértices: qw0 GIF tag (PACKED, 1 registrador: PRIM), qw1 PRIM, qw2 GIF tag (A+D, 5 registradores, fim), qw3
// TEST_1, qw4 ALPHA_1, qw5 FRAME_1, qw6 ZBUF_1, qw7 FBA_1. O construtor padrão (0x00283568) liga Gouraud, o teste
// de Z GEQUAL (o Z do PS2 cresce para perto) e a mistura Cs*FIX; FRAME/ZBUF/FBA são preenchidos na hora do desenho.
// Os modelos dos carros pedem os estados por nome, "STATE::name=envmap;primtype=tristrip", que a função de resolução
// do jogo passa ao RStateManager (0x001ce158): o padrão do carro (triângulos, textura, névoa, mistura, teste de alfa
// GREATER 0xa0 que falha em "só RGB": o truque para não gravar Z em quem tem alfa < 0xa0, isto é, todo pixel com a
// escala de alfa do GS) e uma função por chave (modify, primtype, shading, cull, texture, texcoord, depthtest,
// alphatestmethod, alphabm, transparency, chroma, alphatest, texalpha, fog, antialias, name). As chaves cujo tratador
// devolve 1 na primeira passada (só modify=specmap) são aplicadas de novo no fim.
//
// TAR: 0x60 bytes no modelo (+0 os dados da imagem, +4 o nome da shape em 4 letras, +8..+0x14 a faixa de UV em
// float, +0x18 o modo da textura (1 DECAL), +0x1c/+0x20 repetir em U/V...). O construtor (0x0027c6a0) acha a shape
// "shape_<nome>", e reescreve +0x10..+0x18 como os campos do CLAMP_1 e as bandeiras (TFX nos bits 25-26). O pacote
// que o op 6 do GeoPrim sobe (0x0027d340): GIF tag A+D com 6 registradores, TEXA, CLAMP_1, TEX1_1, TEX0_1, MIPTBP1_1,
// MIPTBP2_1. Divergência (só do port): a VRAM não existe no PC; os "dados da imagem" que o TAR aponta são um bloco do
// port com os mesmos campos que o 0x0027d340 lê, e o TBP0 do TEX0 leva o índice da textura no renderizador do PC.

#include <cstdint>
#include <string>
#include <vector>

#include "driving/eagl_loader.h"

namespace drv::eagl {

constexpr uint32_t kGeoPrimStateSize = 0x80;

// EAGL::GeoPrimState::GeoPrimState (0x00283568), o construtor padrão.
void GeoPrimState_Init(uint8_t* state);
// EAGL::GeoPrimState::SetPrimType (0x00283fc8) (o tipo do EAGL: 0 pontos ... 4 trilist, 5 tristrip, 10 sprite)
bool GeoPrimState_SetPrimType(uint8_t* state, int type);
// EAGL::GeoPrimState::SetDepthTest (0x00284038) (0 never, 1 always, 6 gequal, 7 greater; os outros recusa)
bool GeoPrimState_SetDepthTest(uint8_t* state, int test);

// O RStateManager: o estado de uma especificação "chave=valor;..." (sem o "STATE::"), deduplicado.
// RStateManager::GetGeoPrimState (0x001ce158)
uint8_t* RStateManager_GetGeoPrimState(const char* spec);
// Esvazia os estados guardados (troca de modelo, testes).
void RStateManager_Reset();
int RStateManager_Count();

// Os dados de uma imagem que o TAR aponta (+0), com os campos nas posições que o 0x0027d340 lê.
struct TarImage {
    uint8_t raw[0xdc];
};
// Cria os dados de uma imagem para o TAR: o índice da textura do PC vai no TBP0 (+0x34), o PSM do GS (+0x3c), a
// largura e a altura (+0x40, e o log2 em +0x38), "tem paleta" (bit 3 de +0x10).
TarImage* tar_image_create(uint16_t texture_id, int width, int height, uint8_t gs_psm, bool palette);
void tar_images_reset();

// A busca da shape pelo nome (as 4 letras): o RegisterShapes do jogo registra "shape_<nome>"; o port pergunta a esta
// função, que o dono das texturas instala. Devolve nullptr se a shape não existe.
using ShapeLookup = TarImage* (*)(const char name[4], void* user);
void set_shape_lookup(ShapeLookup fn, void* user);

// EAGL::TAR::TAR (0x0027c6a0), o construtor de tipo registrado para "EAGL::TAR".
void TAR_Construct(uint32_t tar);
// O pacote de 7 qwords do TAR (0x0027d340); devolve o fim.
uint64_t* TAR_WritePacket(uint32_t tar_plus4, uint64_t* out);
// Um TAR montado pelo port (as texturas que o jogo cria em código: GAME::SpecularMap = 'carS' do common.ssh,
// GAME::ReflectionMap): 0x60 bytes zerados com o nome, a faixa [0,1], repetir em U e V, e construído.
uint32_t tar_create(const char name[4], bool repeat_u, bool repeat_v);

// O estado do GS que um pacote A+D (GeoPrimState, TAR) deixa: o que o renderizador do PC precisa.
struct GsRegs {
    bool has_prim = false;
    uint64_t prim = 0;
    bool has_test = false, has_alpha = false, has_zbuf = false, has_tex0 = false, has_clamp = false, has_tex1 = false;
    uint64_t test = 0, alpha = 0, zbuf = 0, tex0 = 0, clamp = 0, tex1 = 0, texa = 0;
};
// Lê um pacote GIF (tags PACKED com PRIM ou A+D) a partir de `qw` até o fim (EOP) ou `max_qw`.
void gs_read_packet(const uint8_t* qw, int max_qw, GsRegs& regs);

// Registra os tipos e os alocadores do EAGL que o port conhece (o EAGL::Init, 0x00286e18): "Model", "EAGL::TAR" e os
// RUNTIME_ALLOC de "EAGL::GeoPrimState" e "EAGL::TAR" (estes dois não portados: devolvem um estado padrão e contam
// em `runtime_alloc_unported`).
void register_eagl_types();
extern int runtime_alloc_unported;

}  // namespace drv::eagl
