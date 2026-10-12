#pragma once
// Estruturas de tempo de execução que o carregador de fases (parsemap_*) monta no heap.
//
// Nomes de tipo originais (vêm do mangling, ex.: parsemap_create_dynamic_objects recebe
// _TARG23_PLACEMENT*, level_tag*, celglist_tag*, framelist_tag*). Nomes de campo são do port; os
// desconhecidos ficam como unknown_<deslocamento>. Os deslocamentos foram lidos no código que usa
// cada campo e são conferidos por static_assert. Ponteiros têm 4 bytes (o port compila em 32 bits),
// então os campos que apontam para dentro do arquivo carregado são ponteiros de verdade.

#include <cstddef>
#include <cstdint>

#include "engine/ps2types.h"
#include "platform/ptr32.h"

struct cel_tag;
struct celglist_tag;
struct map_tag;
struct obj_tag;
struct ATTACH_tag;  // anexo desenhado junto com uma célula ou objeto (engine/view.h)

// Quadro de um caminho (bloco 0x19; level_tag::path aponta para o primeiro de path_count quadros):
// posição e rotação em quatérnio (x, y, z, w). Lido por Spline_Interp e KeyFrame_Interp.
struct framelist_tag {
    float pos[3];
    float quat[4];
};
static_assert(sizeof(framelist_tag) == 0x1c);

// Plano ax + by + cz + d = 0 (Plane_PlaneEq).
struct alignas(16) plane_equ_tag {
    float a, b, c, d;
};

// Cabeçalho de bloco já separado: DynamicBH guarda o tamanho no word baixo e o id no alto.
struct block_header_tag {
    uint32_t size;
    uint32_t id;
};

// Entrada da tabela de texturas (bloco 0x10, 12 bytes cada).
struct bmpheader_tag {
    uint16_t flags;   // bit 0: 8 bits por pixel
    uint16_t width1;  // largura - 1
    uint16_t height1; // altura - 1
    uint16_t frames;  // byte baixo: quadros de animação
    union {
        uint32_t hash;  // no arquivo (e para GameCube/Xbox)
        P32<uint8_t> data;  // depois do bloco de pixels: aponta para eles
    };
};
static_assert(sizeof(bmpheader_tag) == 12);

// Entrada da tabela de paletas (bloco 0x0f, 8 bytes cada).
struct pallheader_tag {
    uint32_t unknown_00;
    union {
        uint32_t hash;  // hashcode da textura no arquivo
        P32<uint8_t> data;  // depois do bloco de paleta: aponta para ela
    };
};
static_assert(sizeof(pallheader_tag) == 8);

// Registro de textura criado por psiCreateMapTextures (0x20 bytes). Depois dele,
// bmpheader_tag::data e maptex_tag::rec apontam para cá.
struct pstex_tag {
    P32<uint8_t> pixels;     // 0x00 cópia dos pixels no heap (todos os quadros)
    P32<uint8_t> palette;    // 0x04 cópia da paleta no heap
    uint32_t unknown_08;     // 0
    uint32_t unknown_0c;     // 0
    uint16_t min_width;      // 0x10 GetMinWidth(largura), no mínimo 2
    uint16_t width;          // 0x12
    uint16_t height;         // 0x14 altura, no mínimo 8
    uint16_t colors;         // 0x16 16 ou 256
    uint16_t unknown_18;
    uint16_t tex_index;      // 0x1a índice na tabela global Tex
    uint8_t frames;          // 0x1c
    uint8_t frame_rate;      // 0x1d quadros de vídeo por quadro de animação
    uint8_t unknown_1e;
    uint8_t unknown_1f;      // 1, ou 2 quando min_width < 2
};
static_assert(sizeof(pstex_tag) == 0x20);

// Textura registrada na tabela de hashcodes (8 bytes).
struct maptex_tag {
    P32<pstex_tag> rec;  // preenchido por psiCreateMapTextures
    int16_t width;
    int16_t height;
};
static_assert(sizeof(maptex_tag) == 8);

// Entrada da tabela global de texturas carregadas (Tex, 0x00254ae0: 1024 entradas de 0x20 bytes).
// Texturas animadas ocupam uma entrada por quadro.
struct PS2TEX_tag {
    P32<pstex_tag> rec;     // 0x00
    uint32_t crc;           // 0x04 crc32buf dos pixels
    P32<uint32_t> dma;      // 0x08 pacote de registradores do GS (FillTextureDMA), só no quadro 0
    int16_t width;          // 0x0c
    int16_t height;         // 0x0e
    uint16_t vram_width;    // 0x10 largura arredondada para 128
    uint16_t vram_height;   // 0x12 altura arredondada (128 em 4 bits, 64 em 8 bits)
    uint16_t unknown_14;    // 0xffff
    uint16_t unknown_16;    // 0
    uint16_t unknown_18;    // 0
    uint16_t map_hash;      // 0x1a: 16 bits baixos de MapHashCode
    uint16_t colors;        // 0x1c
    uint8_t shared;         // 0x1e: quantas vezes foi reaproveitada por outro item
    uint8_t frame;          // 0x1f
};
static_assert(sizeof(PS2TEX_tag) == 0x20);

// m_pmap: texturas e paletas de um item de mapa (0x14 bytes).
struct map_tag {
    P32<pallheader_tag> pall;  // 0x00
    uint32_t pall_count;   // 0x04
    P32<bmpheader_tag> bmp;  // 0x08
    uint32_t bmp_count;    // 0x0c
    P32<maptex_tag> tex;   // 0x10: pall_count entradas
};
static_assert(sizeof(map_tag) == 0x14);

// Dicionários de geometria de uma entidade (0x28 bytes, celglist_tag::gfx).
struct entgfx_tag {
    P32<map_tag> map;    // 0x00
    P32<uint8_t> norm;   // 0x04 bloco 0x20
    P32<uint8_t> ps2_gfx;  // 0x08 bloco 0x2d (aponta para o cabeçalho do bloco, não para o conteúdo)
    P32<uint8_t> block_24;  // 0x0c bloco 0x24
    P32<uint8_t> xyz;    // 0x10 blocos 0x00 e 0x08
    P32<uint8_t> morph;  // 0x14 bloco 0x2b
    P32<uint8_t> rgba;   // 0x18 bloco 0x02
    P32<uint8_t> uv;     // 0x1c bloco 0x01
    P32<uint8_t> comlist;  // 0x20 blocos 0x03, 0x0a e 0x0b
    uint16_t xyz_count;  // 0x24
    uint16_t unknown_26;
};
static_assert(sizeof(entgfx_tag) == 0x28);

// Colisão de uma entidade (0x18 bytes, bloco 0x2e/0x2f).
struct colldata_tag {
    P32<uint8_t> boxes;   // 0x00: box_count registros de 0x30 bytes
    P32<uint8_t> list;    // 0x04: list_count registros de 8 bytes
    P32<uint8_t> rest;    // 0x08
    P32<uint8_t> head;    // 0x0c: início dos dados
    uint16_t head_bytes;  // 0x10
    uint16_t list_count;  // 0x12
    uint16_t box_count;   // 0x14
    uint16_t unknown_16;
};
static_assert(sizeof(colldata_tag) == 0x18);

// Entidade de mapa (0x50 bytes; pCelList é um array delas, uma por bloco de parâmetros).
struct alignas(16) celglist_tag {
    P32<uint8_t> params;  // 0x00: bloco de parâmetros + 0x34
    P32<entgfx_tag> gfx;  // 0x04
    P32<colldata_tag> coll;  // 0x08
    uint32_t unknown_0c;
    _VECTOR sphere;       // 0x10: centro e raio
    _VECTOR box_min;      // 0x20 (w não é escrito)
    _VECTOR box_max;      // 0x30 (w não é escrito)
    P32<celglist_tag> lod;  // 0x40 (bloco 0x27)
    uint32_t flags;       // 0x44
    uint32_t unknown_48[2];
};
static_assert(sizeof(celglist_tag) == 0x50);
static_assert(offsetof(celglist_tag, lod) == 0x40);

// Portal entre duas células (0xa0 bytes, build_alloc_portal). parsemap_block_portal_data monta
// dois temporários na pilha com o mesmo layout (plano e vértices).
struct alignas(16) portal_tag {
    P32<portal_tag> next;  // 0x00: lista da célula
    P32<portal_tag> opposite;  // 0x04: o mesmo portal visto da outra célula
    P32<cel_tag> cel;      // 0x08: célula do outro lado
    uint32_t unknown_0c;
    plane_equ_tag plane;   // 0x10
    uint32_t stamp;        // 0x20 GameState.stamp da última visita (Collide_StraddleCels)
    uint16_t unknown_24;   // 0xffff
    uint8_t unknown_26;
    uint8_t flag;          // 0x27
    uint32_t unknown_28[2];
    _VECTOR verts[4];      // 0x30
    _VECTOR center;        // 0x70 (w não é escrito)
    _VECTOR box_min;       // 0x80 (w não é escrito)
    _VECTOR box_max;       // 0x90 (w não é escrito)
};
static_assert(sizeof(portal_tag) == 0xa0);
static_assert(offsetof(portal_tag, verts) == 0x30);

// Instância estática no mundo (0xb0 bytes, build_alloc_cel): uma por posicionamento com o bit
// 0x8000. As células formam uma lista ligada a partir de world_tag::first.
struct alignas(16) cel_tag {
    uint32_t unknown_00[2];
    P32<obj_tag> objects;   // 0x08 objetos na sala (control_link_object_to_cel)
    uint32_t unknown_0c;
    P32<ATTACH_tag> attach;  // 0x10 anexos desenhados junto (View_DrawCelGlist)
    P32<cel_tag> next;      // 0x14
    P32<cel_tag> children;  // 0x18 sala: primeira célula dentro dela; célula: a próxima na mesma sala
    P32<cel_tag> parent;    // 0x1c sala da célula (build_link_objects_to_rooms)
    P32<cel_tag> vis_next;  // 0x20 próxima na lista de células visíveis do quadro
    P32<struct NODE_tag> ai_nodes;  // 0x24 nós da rede da IA na sala (AIPath_Link2Cel)
    P32<portal_tag> portals;  // 0x28
    P32<void> env;          // 0x2c ambiente da sala (Env_Create: chuva, neve, sol)
    P32<struct BNODE_tag> ai_bnodes;  // 0x30 nós de limite da IA na sala (AIBounds_Link2Cel)
    P32<struct BLINK_tag> ai_blinks;  // 0x34 segmentos de limite com uma ponta na sala (AIBounds_Prepare)
    P32<celglist_tag> glist;  // 0x38
    uint32_t type;          // 0x3c: tipo do posicionamento (parseentity_fixup_entity); 0x40000 = sala
    _VECTOR box_min;        // 0x40
    _VECTOR box_max;        // 0x50
    _VECTOR pos;            // 0x60 (w não é escrito)
    _VECTOR rot;            // 0x70 ângulos de Euler (w não é escrito)
    _VECTOR sphere;         // 0x80: esfera da entidade transformada para o mundo
    float water_y;          // 0x90 altura da água na sala (-500 sem água; uma célula 0x8017 dentro dela a define)
    uint32_t glist_flags;   // 0x94: celglist_tag::flags
    uint8_t misc[12];       // 0x98..0xa3 (0x98 é lido também como u16 e como u64)
    uint32_t unknown_a4[3];
};
static_assert(sizeof(cel_tag) == 0xb0);
static_assert(offsetof(cel_tag, glist) == 0x38 && offsetof(cel_tag, sphere) == 0x80);
static_assert(offsetof(cel_tag, ai_nodes) == 0x24 && offsetof(cel_tag, ai_blinks) == 0x34);

struct world_tag {
    P32<cel_tag> first;
};
static_assert(sizeof(world_tag) == 4, "world_tag: 4 bytes");

// Parâmetros de um posicionamento (0xb0 bytes, zerados para cada um). Os pares chave/valor do
// arquivo vão para params[chave].
struct level_tag {
    uint16_t unknown_00;
    int16_t path_count;          // 0x02
    uint16_t path_unknown;       // 0x04
    uint16_t unknown_06;
    P32<framelist_tag> path;     // 0x08
    uint32_t unknown_0c[8];
    uint32_t params[33];         // 0x2c
};
static_assert(sizeof(level_tag) == 0xb0);
static_assert(offsetof(level_tag, params) == 0x2c);

// Posicionamento no bloco 0x1a (0x4c bytes + param_count pares {chave, valor}).
struct _TARG23_PLACEMENT {
    uint16_t entity;         // 0x00: índice em pCelList (com hash = 0xffffffff)
    uint16_t unknown_02;
    uint32_t hash;           // 0x04: hashcode da entidade, ou 0xffffffff
    uint32_t flags;          // 0x08: 0x8000 = célula estática; 0xa000 = sem objeto dinâmico
    float pos[3];            // 0x0c
    float rot[3];            // 0x18
    float quat[4];           // 0x24 (conjugado e às vezes desatualizado; o jogo usa rot)
    float scale[3];          // 0x34
    int16_t path_count;      // 0x40
    uint16_t path_unknown;   // 0x42
    uint16_t path_index;     // 0x44: índice em m_paths
    uint16_t unknown_46;
    uint32_t param_count;    // 0x48
};
static_assert(sizeof(_TARG23_PLACEMENT) == 0x4c);
