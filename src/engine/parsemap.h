#pragma once
// Leitura de itens de mapa (parsemap_* e parseentity_*, 0x001d00c0-0x001d2590 no ACTION.ELF).
//
// Um item de mapa (01xxxxxx) é uma lista de blocos {u32 id << 24 | tamanho, conteúdo}; na versão 1
// uma tabela de deslocamentos no início dá a posição de cada bloco. parsemap_parsemap percorre os
// blocos e cada um vira estrutura de tempo de execução no heap (map_types.h) ou ponteiro para dentro
// do próprio arquivo, que fica carregado. A seção depois do bloco 0x1c ("memory discard") é
// descartada no fim; os posicionamentos dinâmicos (bloco 0x1a) viram objetos antes disso.

#include <cstdint>

#include "engine/map_types.h"

extern uint32_t* FileBase;
extern uint8_t* FileNextBlock;
extern uint8_t* FileLastBlock;
extern uint8_t* FileDiscard;
extern uint32_t* pFileDiscardHeader;
extern uint32_t* pBinFileHeader;
extern uint32_t BinFileVersion;
extern uint32_t MapHashCode;
extern uint32_t ParseMap_State;
extern uint8_t StartingNewMap;
extern block_header_tag DynamicBH;
extern uint8_t* DynamicPtr;
extern uint32_t FileBlockSize[49];  // bytes lidos por id de bloco (estatística)
extern uint32_t bh;                 // tamanho do bloco atual, com o cabeçalho
extern uint32_t block_id;           // id do bloco atual (0x0030d814, sem símbolo)
extern uint32_t OldCollisionSize;

extern celglist_tag* pCelList;
extern celglist_tag* pCurrCelList;
extern uint32_t MapEntityCount;
extern uint32_t* pBMH;              // conteúdo do bloco 0x0e
extern map_tag* m_pmap;
extern uint8_t** m_paths;
extern uint8_t** m_ppaths;
extern world_tag* glb_world;

uint32_t parsemap_parsemap(uint32_t hash, int8_t main_map);
uint32_t parsemap_parsenextblock(int8_t main_map);
uint32_t parsemap_handle_block_id(int8_t main_map);
void parsemap_block_map_header();
void parsemap_block_entity_params();
void parsemap_block_portal_data();
void parsemap_block_Coll_Data_New();
void parsemap_block_map_data_static();
void parsemap_block_map_data_dynamic(block_header_tag& header, uint8_t* block, uint8_t main_map);
void parsemap_block_hashlist();
void parsemap_block_LOD();
void parsemap_block_texture_header();
void parsemap_block_palette_header();
void parsemap_block_texture_data_pc();
void parsemap_block_particles();
void parsemap_block_texture_data_pcdx();
void parsemap_block_palette_data_psx();
void parsemap_block_texture_data_GameCube();
void parsemap_block_texture_data_Xbox();
void parsemap_block_palette_data_pc();
void parsemap_block_dict_xyz();
void parsemap_block_dict_uv();
void parsemap_block_dict_rgba();
void parsemap_block_dict_norm();
void parsemap_block_morph_data();
void parsemap_block_PS2_GFX();
void parsemap_block_dict_comlist();
void parsemap_block_coll_data();
void parsemap_block_path_data();
void parsemap_block_datums();
void parsemap_block_memory_discard();
void parsemap_reset_to_block_memory_discard();

// Cria os objetos do jogo (portas, armas, inimigos...) a partir dos posicionamentos (objects.cpp).
void parsemap_create_dynamic_objects(_TARG23_PLACEMENT* placement, level_tag* level, uint32_t flags,
                                     celglist_tag* glist, framelist_tag* path, celglist_tag* next_glist,
                                     uint8_t main_map);

void parseentity_fixup_entity(cel_tag* cel, uint32_t* glist_flags, level_tag* level);
void parseentity_transform_bounding_box(cel_tag* cel);

// Só para o port: estado inicial do executável.
void parsemap_debug_power_on();

// Contadores de situações em que o original leria ou escreveria em endereço inválido e o port
// pula a operação (ver os comentários de divergência em parsemap.cpp). Usados pelo nf_loader_check.
struct ParsemapGuards {
    uint32_t entity_without_gfx = 0;
    uint32_t cel_without_glist = 0;
    uint32_t portal_without_cel = 0;
    uint32_t param_key_out_of_range = 0;
    uint32_t dynamic_without_block = 0;
    uint32_t discard_outside_file = 0;
    uint32_t block_id_out_of_range = 0;
};
extern ParsemapGuards g_parsemap_guards;
