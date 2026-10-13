#pragma once
// Recursos gráficos criados durante a carga (psiCreateMapTextures, psiCreateEntityGfx,
// psiResetResources e auxiliares).
//
// A parte que mexe nas estruturas do jogo é portada como no PS2: o registro de cada textura
// (pstex_tag), a tabela global Tex com reaproveitamento por CRC, as cópias de pixels e paletas no
// heap e as correções de ponteiro do bloco 0x2d. O que no PS2 vai para a VRAM e para as cadeias de
// DMA da VU1 aqui vai para um "backend" (o renderizador Direct3D 11), por ganchos opcionais: sem
// backend (testes, nf_loader_check) só as estruturas são montadas.

#include <cstdint>

#include "engine/map_types.h"

constexpr int kMaxPS2Textures = 1024;
extern PS2TEX_tag Tex[kMaxPS2Textures];
extern int32_t NumPS2TexturesLoaded;
extern int32_t nTexLoad;
extern int32_t TotalSize;
extern int32_t PS2LSActive;
extern int8_t PS2FirstGameRun;
extern int32_t TextureBufBase;

struct PsiGfxBackend {
    void* user = nullptr;
    // Textura nova na entrada Tex[index] (quadro 0; "pixels" traz todos os quadros empilhados).
    // pixels_bytes e palette_bytes são os tamanhos dos blocos de origem, para validar.
    void (*create_texture)(void* user, int index, const bmpheader_tag& header, const uint8_t* pixels,
                           uint32_t pixels_bytes, const uint8_t* palette, uint32_t palette_bytes) = nullptr;
    // Geometria de uma entidade. Chamado antes das correções de ponteiro, com o bloco 0x2d ainda
    // como está no arquivo. Devolve um id de malha ou -1.
    int (*create_entity)(void* user, const celglist_tag* glist, const map_tag* map) = nullptr;
    // A tabela de texturas recomeçou e o heap vai ser reiniciado: descartar tudo.
    void (*reset)(void* user) = nullptr;
    // Só do port: o jogo reescreveu os pixels (e a paleta) da textura Tex[index] durante a partida (o fundo animado da
    // pausa, platform/psi_woman.h): um quadro, nos formatos do create_texture. O backend troca o conteúdo da textura.
    void (*update_texture)(void* user, int index, const bmpheader_tag& header, const uint8_t* pixels,
                           uint32_t pixels_bytes, const uint8_t* palette, uint32_t palette_bytes) = nullptr;
};
extern PsiGfxBackend g_psi_gfx_backend;

// Só no port: malha criada pelo backend para a entidade (-1 se nenhuma) e entrada de Tex usada pela
// textura "item_texture" (índice no bloco 0x10 do item) de um map_tag (-1 se não virou textura).
int psi_gfx_entity_mesh(const celglist_tag* glist);
int psi_gfx_map_texture(const map_tag* map, int item_texture);

// Situações em que o original leria fora das tabelas; o port pula e conta.
struct PsiGfxGuards {
    uint32_t texture_without_palette = 0;  // bmp_count maior que pall_count
    uint32_t entity_without_gfx = 0;       // entidade sem bloco 0x2d
    uint32_t bad_texture_ref = 0;          // referência de textura fora das tabelas
    uint32_t table_full = 0;               // mais de 1024 entradas em Tex
};
extern PsiGfxGuards g_psi_gfx_guards;

void psiCreateMapTextures(map_tag* map);
uint32_t psiCreateEntityGfx(celglist_tag* glist, map_tag* map, uint32_t mem_type);
void psiResetResources();
void FillTextureDMA(int index);
uint32_t crc32buf(const char* data, uint32_t size);
uint32_t ByteCompare(const uint8_t* a, const uint8_t* b, int32_t size);
int32_t GetBitPos(int32_t value);
int32_t GetMinWidth(int32_t width);
void initcache(int32_t start, int32_t size);
void PS2WipeVram();
void PS2SafeCleanup();

// Só para testes: zera a tabela e os registros do port.
void psi_gfx_debug_power_on();
