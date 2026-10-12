// Port de parsemap_* e parseentity_*. Ver parsemap.h.
//
// Divergências (todas onde o original leria ou escreveria num endereço inválido; cada uma conta em
// g_parsemap_guards para o nf_loader_check mostrar se os dados chegam a passar por ali):
// - bloco de parâmetros sem dicionário de geometria antes: o original grava em gfx nulo;
// - posicionamento estático cuja entidade não existe: o original lê celglist nulo;
// - portal com célula não encontrada: o original grava em célula nula;
// - chave de parâmetro >= 33: o original escreve além do level_tag, na pilha;
// - item sem bloco 0x1a ou sem bloco 0x1c: o original percorre lixo ou copia para endereço nulo;
// - id de bloco >= 49: o original soma fora de FileBlockSize.

#include "engine/lights.h"
#include "engine/parsemap.h"

#include <cstdio>
#include <cstring>

#include "engine/build.h"
#include "engine/hashtable.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/vecmath.h"
#include "platform/psi.h"
#include "platform/psi_file.h"
#include "platform/psi_gfx.h"

uint32_t* FileBase = nullptr;
uint8_t* FileNextBlock = nullptr;
uint8_t* FileLastBlock = nullptr;
uint8_t* FileDiscard = nullptr;
uint32_t* pFileDiscardHeader = nullptr;
uint32_t* pBinFileHeader = nullptr;
uint32_t BinFileVersion = 0;
uint32_t MapHashCode = 0;
uint32_t ParseMap_State = 0;
uint8_t StartingNewMap = 0;
block_header_tag DynamicBH;
uint8_t* DynamicPtr = nullptr;
uint32_t FileBlockSize[49];
uint32_t bh = 0;
uint32_t block_id = 0;
uint32_t OldCollisionSize = 0;

celglist_tag* pCelList = nullptr;
celglist_tag* pCurrCelList = nullptr;
uint32_t MapEntityCount = 0;
uint32_t* pBMH = nullptr;
map_tag* m_pmap = nullptr;
uint8_t** m_paths = nullptr;
uint8_t** m_ppaths = nullptr;
world_tag* glb_world = nullptr;

ParsemapGuards g_parsemap_guards;

namespace {

bmpheader_tag* pBmpHeader = nullptr;
pallheader_tag* pPallHeader = nullptr;
maptex_tag* pTexData = nullptr;
uint8_t* prev_block = nullptr;
char filename[64];

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint16_t rd16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
uint8_t* payload() { return FileNextBlock + 4; }

// Dicionários de geometria da entidade atual, alocados pelo primeiro bloco que precisar deles.
entgfx_tag* current_gfx() {
    if (pCurrCelList->gfx == nullptr) {
        pCurrCelList->gfx = static_cast<entgfx_tag*>(Mem_Malloc(0x28, 0x904, 0));
        std::memset(pCurrCelList->gfx, 0, 0x28);
    }
    return pCurrCelList->gfx;
}

// Pares {chave, valor} depois do posicionamento vão para level->params[chave].
void read_params(const _TARG23_PLACEMENT* pl, level_tag* level) {
    const uint8_t* kv = reinterpret_cast<const uint8_t*>(pl) + sizeof(_TARG23_PLACEMENT);
    for (uint32_t i = 0; i < pl->param_count; ++i, kv += 8) {
        const uint32_t key = rd32(kv);
        if (key < 33) {
            level->params[key] = rd32(kv + 4);
        } else {
            ++g_parsemap_guards.param_key_out_of_range;
        }
    }
}

// Texturas e paletas: registra a textura e avança as três tabelas juntas.
void register_texture(uint32_t hash, uint32_t& data_field) {
    if (hash != 0xffffffff) hashtable_additem(hash, pTexData);
    pTexData->width = static_cast<int16_t>(pBmpHeader->width1 + 1);
    pTexData->height = static_cast<int16_t>(pBmpHeader->height1 + 1);
    data_field = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload()));
    ++pBmpHeader;
    ++pPallHeader;
    ++pTexData;
}

}  // namespace

void parsemap_debug_power_on() {
    FileBase = nullptr;
    FileNextBlock = FileLastBlock = FileDiscard = nullptr;
    pFileDiscardHeader = pBinFileHeader = nullptr;
    BinFileVersion = MapHashCode = ParseMap_State = 0;
    StartingNewMap = 0;
    DynamicBH = {};
    DynamicPtr = nullptr;
    std::memset(FileBlockSize, 0, sizeof(FileBlockSize));
    bh = block_id = OldCollisionSize = 0;
    pCelList = pCurrCelList = nullptr;
    MapEntityCount = 0;
    pBMH = nullptr;
    m_pmap = nullptr;
    m_paths = m_ppaths = nullptr;
    glb_world = nullptr;
    pBmpHeader = nullptr;
    pPallHeader = nullptr;
    pTexData = nullptr;
    prev_block = nullptr;
    g_parsemap_guards = {};
}

// [decomp] 0x001d1cf0 parsemap_parsemap
// Quatro chamadas por arquivo: abre, lê todos os blocos, cria os objetos dinâmicos (itens que não
// são a fase principal) e descarta o fim do arquivo. Devolve 0 na última.
uint32_t parsemap_parsemap(uint32_t hash, int8_t main_map) {
    switch (ParseMap_State) {
        case 0: {
            std::snprintf(filename, sizeof(filename), "%s%8.8X.bin", "", hash);
            uint32_t size = 0;
            FileBase = static_cast<uint32_t*>(psiFileLoad(filename, 0x1704, &size));
            FileNextBlock = reinterpret_cast<uint8_t*>(FileBase + 1);
            FileLastBlock = reinterpret_cast<uint8_t*>(FileBase) + size;
            MapHashCode = hash;
            ParseMap_State = 1;
            DynamicBH.size = 0;
            DynamicPtr = nullptr;
            StartingNewMap = 1;
            BinFileVersion = FileBase[0];
            pBinFileHeader = FileBase + 1;
            return 1;
        }
        case 1:
            while (parsemap_parsenextblock(main_map) == 0) {
            }
            ParseMap_State = 2;
            return 1;
        case 2:
            if (main_map == 0 && MP_NeedFile(MapHashCode)) parsemap_block_map_data_dynamic(DynamicBH, DynamicPtr, 0);
            ParseMap_State = 3;
            return 1;
        case 3:
            if (main_map == 0) {
                if (FileDiscard >= reinterpret_cast<uint8_t*>(FileBase) && FileDiscard <= FileLastBlock) {
                    void* discard = FileDiscard;
                    LoaderShrink(&discard, uint32_t(FileLastBlock - FileDiscard));
                    FileDiscard = static_cast<uint8_t*>(discard);
                } else {
                    ++g_parsemap_guards.discard_outside_file;
                }
            }
            ParseMap_State = 0;
            return 0;
        default:
            return 0;
    }
}

// [decomp] 0x001d12e8 parsemap_parsenextblock
// Devolve 1 no bloco de fim (0x1d).
uint32_t parsemap_parsenextblock(int8_t main_map) {
    const uint32_t hash = MapHashCode;
    if (BinFileVersion == 1) FileNextBlock = reinterpret_cast<uint8_t*>(FileBase) + *pBinFileHeader + 4;
    const uint32_t header = rd32(FileNextBlock);
    block_id = header >> 24;
    bh = header & 0xffffff;
    if (block_id < 49) {
        FileBlockSize[block_id] += bh;
    } else {
        ++g_parsemap_guards.block_id_out_of_range;
    }
    if (!MP_NeedFile(hash)) {
        if (block_id == 0x1c) {
            FileDiscard = reinterpret_cast<uint8_t*>(FileBase);
            pFileDiscardHeader = pBinFileHeader;
        } else if (block_id == 0x1d) {
            return 1;
        }
    } else if (parsemap_handle_block_id(main_map) != 0) {
        return 1;
    }
    uint8_t* next;
    if (BinFileVersion == 1) {
        next = reinterpret_cast<uint8_t*>(FileBase) + pBinFileHeader[1] + 4;
        ++pBinFileHeader;
    } else {
        next = FileNextBlock + bh;
    }
    prev_block = FileNextBlock;
    FileNextBlock = next;
    return 0;
}

// [decomp] 0x001d1020 parsemap_handle_block_id
uint32_t parsemap_handle_block_id(int8_t main_map) {
    switch (block_id) {
        case 0x00:
        case 0x08: parsemap_block_dict_xyz(); break;
        case 0x01: parsemap_block_dict_uv(); break;
        case 0x02: parsemap_block_dict_rgba(); break;
        case 0x03:
        case 0x0a:
        case 0x0b: parsemap_block_dict_comlist(); break;
        case 0x04:
        case 0x1f: parsemap_block_entity_params(); break;
        case 0x05: AIPath_Parse(FileNextBlock); break;
        case 0x0e: parsemap_block_map_header(); break;
        case 0x0f: parsemap_block_palette_header(); break;
        case 0x10: parsemap_block_texture_header(); break;
        case 0x11: parsemap_block_palette_data_pc(); break;
        case 0x12: parsemap_block_palette_data_psx(); break;
        case 0x15:
        case 0x16: parsemap_block_texture_data_pc(); break;
        case 0x17: parsemap_block_texture_data_GameCube(); break;
        case 0x18: parsemap_block_texture_data_Xbox(); break;
        case 0x19:
            if (main_map != 0) parsemap_block_path_data();
            break;
        case 0x1a:
            DynamicBH.size = bh;
            DynamicBH.id = block_id;
            DynamicPtr = FileNextBlock;
            parsemap_block_map_data_static();
            break;
        case 0x1c: parsemap_block_memory_discard(); break;
        case 0x1d: return 1;
        case 0x20: parsemap_block_dict_norm(); break;
        case 0x21: parsemap_block_portal_data(); break;
        case 0x22:
        case 0x23: parsemap_block_coll_data(); break;
        case 0x24: current_gfx()->block_24 = payload(); break;
        case 0x25: parsemap_block_datums(); break;
        case 0x26:
            if (main_map != 0) Light_SetAmbientRadiators(payload());
            break;
        case 0x27: parsemap_block_LOD(); break;
        case 0x28:
            if (main_map != 0) Sound_LoadMapSounds(payload());
            break;
        case 0x29: parsemap_block_texture_data_pcdx(); break;
        case 0x2b: parsemap_block_morph_data(); break;
        case 0x2c: parsemap_block_hashlist(); break;
        case 0x2d: parsemap_block_PS2_GFX(); break;
        case 0x2e:
        case 0x2f: parsemap_block_Coll_Data_New(); break;
        case 0x30: parsemap_block_particles(); break;
        default: break;
    }
    return 0;
}

// [decomp] 0x001d1430 parsemap_block_map_header
// Sem entidades, nada é alocado e pCelList continua apontando para o item anterior.
void parsemap_block_map_header() {
    pBMH = reinterpret_cast<uint32_t*>(payload());
    MapEntityCount = pBMH[1];
    if (MapEntityCount == 0) return;
    pCelList = static_cast<celglist_tag*>(Mem_Malloc(MapEntityCount * 0x50, 0x904, 0));
    std::memset(pCelList, 0, pBMH[1] * 0x50);
    pCurrCelList = pCelList;
    pCelList->coll = nullptr;
    m_paths = pBMH[2] == 0 ? nullptr : static_cast<uint8_t**>(Mem_Malloc(pBMH[2] << 2, 0x1804, 0));
    if (m_paths != nullptr) m_ppaths = m_paths;
    m_pmap = static_cast<map_tag*>(Mem_Malloc(0x14, 0xa04, 0));
    std::memset(m_pmap, 0, 0x14);
}

// [decomp] 0x001d1508 parsemap_block_entity_params
void parsemap_block_entity_params() {
    celglist_tag* g = pCurrCelList;
    const uint8_t* b = FileNextBlock;
    const uint32_t hash = rd32(b + 4);
    g->flags = rd32(b + 8);
    std::memcpy(&g->sphere, b + 0x0c, 0x1c);  // esfera e box_min.xyz
    std::memcpy(&g->box_max, b + 0x28, 0x0c);
    g->params = FileNextBlock + 0x34;
    if (hash != 0xffffffff) hashtable_additem(hash, g);
    if (g->gfx != nullptr) {
        g->gfx->map = m_pmap;
    } else {
        ++g_parsemap_guards.entity_without_gfx;
    }
    if (StartingNewMap != 0) {
        psiCreateMapTextures(m_pmap);
        StartingNewMap = 0;
    }
    psiCreateEntityGfx(pCurrCelList, m_pmap, MemType);
    ++pCurrCelList;
}

// [decomp] 0x001d15f8 parsemap_block_portal_data
// Registro de 0x38 bytes: 4 vértices xyz, índices das duas entidades (u16) e flags (u16). Cada
// portal vira dois, um em cada célula, com os vértices em ordem inversa no segundo. As células
// encontradas ficam para o próximo portal se ele não achar as suas (como no original).
void parsemap_block_portal_data() {
    const uint32_t count = rd32(FileNextBlock + 4);
    const uint8_t* p = FileNextBlock + 8;
    cel_tag* cel_a = nullptr;
    cel_tag* cel_b = nullptr;
    portal_tag tmp_a, tmp_b;
    for (uint16_t i = 0; i < count; ++i) {
        for (int k = 0; k < 4; ++k, p += 12) {
            float xyz[3];
            std::memcpy(xyz, p, 12);
            tmp_a.verts[k].x = tmp_b.verts[3 - k].x = xyz[0];
            tmp_a.verts[k].y = tmp_b.verts[3 - k].y = xyz[1];
            tmp_a.verts[k].z = tmp_b.verts[3 - k].z = xyz[2];
        }
        Plane_PlaneEq(&tmp_a.plane, &tmp_a.verts[0], &tmp_a.verts[1], &tmp_a.verts[2]);
        Plane_PlaneEq(&tmp_b.plane, &tmp_b.verts[0], &tmp_b.verts[1], &tmp_b.verts[2]);
        const uint16_t index_a = rd16(p), index_b = rd16(p + 2), flags = rd16(p + 4);
        p += 8;
        for (cel_tag* c = glb_world->first; c != nullptr; c = c->next) {
            if (c->glist == pCelList + index_a) cel_a = c;
            if (c->glist == pCelList + index_b) cel_b = c;
        }
        if (cel_a == nullptr || cel_b == nullptr) {
            ++g_parsemap_guards.portal_without_cel;
            continue;
        }
        portal_tag* to_a = build_alloc_portal(&tmp_a, cel_b->portals, cel_a, (flags & 2) == 0);
        cel_b->portals = to_a;
        portal_tag* to_b = build_alloc_portal(&tmp_b, cel_a->portals, cel_b, static_cast<uint8_t>((flags & 1) ^ 1));
        cel_a->portals = to_b;
        to_a->opposite = to_b;
        to_b->opposite = to_a;
    }
}

// [decomp] 0x001d1810 parsemap_block_Coll_Data_New
// Peculiaridade mantida: o laço que alarga as caixas em 0,01 nunca avança o ponteiro, então só a
// primeira caixa cresce (box_count vezes).
void parsemap_block_Coll_Data_New() {
    const uint8_t* b = FileNextBlock;
    uint8_t* data = FileNextBlock + 0x10;
    const uint16_t box_count = rd16(b + 8);
    const uint16_t list_count = rd16(b + 10);
    const uint32_t head_bytes = uint32_t(rd16(b + 0xc)) * 0x40;
    const uint32_t version = rd32(b + 4);
    if (version != 4 && version == 5) {
        data = FileNextBlock + 0x20;
        OldCollisionSize += rd32(b + 0x10);
    }
    if (pCurrCelList->coll == nullptr) {
        if (box_count == 0) {
            pCurrCelList->coll = nullptr;
            return;
        }
        pCurrCelList->coll = static_cast<colldata_tag*>(Mem_Malloc(0x18, 0x1a04, 0));
    }
    colldata_tag* c = pCurrCelList->coll;
    c->head = data;
    data += head_bytes;
    c->box_count = box_count;
    c->boxes = data;
    data += uint32_t(box_count) * 0x30;
    c->list_count = list_count;
    c->list = data;
    c->head_bytes = static_cast<uint16_t>(head_bytes);
    c->rest = data + uint32_t(list_count) * 8;
    float* box = reinterpret_cast<float*>(c->boxes);
    for (uint32_t n = box_count; n != 0; --n) {
        box[4] += 0.01f;
        box[5] += 0.01f;
        box[6] += 0.01f;
        box[0] -= 0.01f;
        box[1] -= 0.01f;
        box[2] -= 0.01f;
    }
    MemStats[0] += bh - 4;
}

// [decomp] 0x001d1998 parsemap_block_map_data_static
// Cria uma célula para cada posicionamento com o bit 0x8000. Peculiaridade mantida: aqui o índice
// de entidade é aceito até MapEntityCount inclusive (o bloco dinâmico usa "<").
void parsemap_block_map_data_static() {
    uint8_t* p = FileNextBlock + 4;
    int32_t left = int32_t(bh) - 4;
    while (left != 0) {
        _TARG23_PLACEMENT* pl = reinterpret_cast<_TARG23_PLACEMENT*>(p);
        level_tag level;
        std::memset(&level, 0, sizeof(level));
        read_params(pl, &level);
        bool resolved = true;
        celglist_tag* g = nullptr;
        if (pl->hash == 0xffffffff) {
            if (pl->entity <= MapEntityCount) {
                g = pCelList + pl->entity;
            } else {
                resolved = false;
            }
        } else {
            g = static_cast<celglist_tag*>(hashtable_getitem(pl->hash));
        }
        if (resolved && (pl->flags & 0x8000) != 0) {
            uint32_t glist_flags = 0;
            cel_tag* cel = build_alloc_cel(glb_world->first);
            glb_world->first = cel;
            cel->glist = g;
            if (g != nullptr) amemcpy(&cel->box_min, &g->box_min, 0x20);
            if (g == nullptr) ++g_parsemap_guards.cel_without_glist;
            cel->type = pl->flags & 0xffff;
            cel->glist_flags = g != nullptr ? g->flags : 0;
            cel->pos.x = pl->pos[0];
            cel->pos.y = pl->pos[1];
            cel->pos.z = pl->pos[2];
            cel->rot.x = pl->rot[0];
            cel->rot.y = pl->rot[1];
            cel->rot.z = pl->rot[2];
            parseentity_fixup_entity(cel, &glist_flags, &level);
            if (cel->glist != nullptr) cel->sphere = cel->glist->sphere;
            parseentity_transform_bounding_box(cel);
        }
        const uint32_t step = sizeof(_TARG23_PLACEMENT) + pl->param_count * 8;
        left -= int32_t(step);
        p += step;
    }
}

// [decomp] 0x001d1b68 parsemap_block_map_data_dynamic
// Percorre de novo o bloco 0x1a e cria os objetos dos posicionamentos sem os bits 0xa000.
void parsemap_block_map_data_dynamic(block_header_tag& header, uint8_t* block, uint8_t main_map) {
    if (block == nullptr) {
        // Divergência: sem bloco 0x1a o original percorre memória a partir do endereço 4.
        ++g_parsemap_guards.dynamic_without_block;
        return;
    }
    uint8_t* p = block + 4;
    for (int32_t left = int32_t(header.size) - 4; left != 0;) {
        _TARG23_PLACEMENT* pl = reinterpret_cast<_TARG23_PLACEMENT*>(p);
        celglist_tag* next_glist = pCelList + pl->entity + 1;
        level_tag level;
        std::memset(&level, 0, sizeof(level));
        read_params(pl, &level);
        celglist_tag* g;
        if (pl->hash == 0xffffffff) {
            g = pl->entity < MapEntityCount ? pCelList + pl->entity : nullptr;
        } else {
            g = static_cast<celglist_tag*>(hashtable_getitem(pl->hash));
        }
        if (pl->path_count != 0) {
            level.path = reinterpret_cast<framelist_tag*>(m_paths[pl->path_index]);
            level.path_unknown = pl->path_unknown;
            level.path_count = pl->path_count;
        }
        if ((pl->flags & 0xa000) == 0 && g != nullptr) {
            parsemap_create_dynamic_objects(pl, &level, pl->flags, g, level.path, next_glist, main_map);
        }
        const uint32_t step = sizeof(_TARG23_PLACEMENT) + pl->param_count * 8;
        left -= int32_t(step);
        p += step;
    }
}

// [decomp] 0x001d1e78 parsemap_block_hashlist
// Lista de hashcodes que o item usa. O resultado da verificação não é usado (era só depuração).
void parsemap_block_hashlist() {
    uint8_t* p = payload();
    const uint32_t count = (bh - 4) >> 2;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t hash = BIN_GetDWord(&p);
        if (hash != 0x6000000 && hashtable_getitem(hash) == nullptr) isLoadable(hash);
    }
}

// [decomp] 0x001d1f10 parsemap_block_LOD
// Um par de u32 por entidade: o primeiro é o índice da entidade de menor detalhe, ou -1.
void parsemap_block_LOD() {
    const int32_t* p = reinterpret_cast<const int32_t*>(payload());
    for (uint16_t i = 0; i < pBMH[1]; ++i, p += 2) {
        if (*p != -1) pCelList[i].lod = pCelList + *p;
    }
}

// [decomp] 0x001d1f80 parsemap_block_texture_header
void parsemap_block_texture_header() {
    pBmpHeader = reinterpret_cast<bmpheader_tag*>(payload());
    m_pmap->bmp = pBmpHeader;
    m_pmap->bmp_count = uint32_t((int32_t(bh) - 4) / 0xc);
}

// [decomp] 0x001d1fc0 parsemap_block_palette_header
void parsemap_block_palette_header() {
    m_pmap->pall = reinterpret_cast<pallheader_tag*>(payload());
    m_pmap->tex = nullptr;
    const uint32_t count = (bh - 4) >> 3;
    m_pmap->pall_count = count;
    if (count != 0) m_pmap->tex = static_cast<maptex_tag*>(Mem_Malloc(count << 3, 0xb08, 0));
    pPallHeader = reinterpret_cast<pallheader_tag*>(payload());
    pTexData = m_pmap->tex;
}

// [decomp] 0x001d2048 parsemap_block_texture_data_pc
void parsemap_block_texture_data_pc() { pBmpHeader->data = payload(); }

// [decomp] 0x001d2060 parsemap_block_particles
void parsemap_block_particles() { Emitter_LoadDefs(payload()); }

// [decomp] 0x001d2080 parsemap_block_texture_data_pcdx
void parsemap_block_texture_data_pcdx() {
    pBmpHeader->data = payload();
    parsemap_block_palette_data_pc();
}

// [decomp] 0x001d20b0 parsemap_block_palette_data_psx
void parsemap_block_palette_data_psx() { register_texture(pPallHeader->hash, pPallHeader->hash); }

// [decomp] 0x001d2148 parsemap_block_texture_data_GameCube
void parsemap_block_texture_data_GameCube() { register_texture(pBmpHeader->hash, pBmpHeader->hash); }

// [decomp] 0x001d21e0 parsemap_block_texture_data_Xbox
void parsemap_block_texture_data_Xbox() { register_texture(pBmpHeader->hash, pBmpHeader->hash); }

// [decomp] 0x001d2278 parsemap_block_palette_data_pc
void parsemap_block_palette_data_pc() { register_texture(pPallHeader->hash, pPallHeader->hash); }

// [decomp] 0x001d2310 parsemap_block_dict_xyz
void parsemap_block_dict_xyz() {
    entgfx_tag* gfx = current_gfx();
    gfx->xyz = payload();
    gfx->xyz_count = static_cast<uint16_t>((int32_t(bh) - 4) / 0xc);
}

// [decomp] 0x001d23a0 parsemap_block_dict_uv
void parsemap_block_dict_uv() { pCurrCelList->gfx->uv = payload(); }

// [decomp] 0x001d23b8 parsemap_block_dict_rgba
void parsemap_block_dict_rgba() { pCurrCelList->gfx->rgba = payload(); }

// [decomp] 0x001d23d0 parsemap_block_dict_norm
void parsemap_block_dict_norm() { pCurrCelList->gfx->norm = payload(); }

// [decomp] 0x001d23e8 parsemap_block_morph_data
void parsemap_block_morph_data() { current_gfx()->morph = payload(); }

// [decomp] 0x001d2450 parsemap_block_PS2_GFX
// Guarda o cabeçalho do bloco, não o conteúdo (psiCreateEntityGfx lê o tamanho dali).
void parsemap_block_PS2_GFX() { current_gfx()->ps2_gfx = FileNextBlock; }

// [decomp] 0x001d24b0 parsemap_block_dict_comlist
void parsemap_block_dict_comlist() { pCurrCelList->gfx->comlist = payload(); }

// [decomp] 0x001d24c8 parsemap_block_coll_data
void parsemap_block_coll_data() {}

// [decomp] 0x001d24d0 parsemap_block_path_data
void parsemap_block_path_data() { *m_ppaths++ = payload(); }

// [decomp] 0x001d24f8 parsemap_block_datums
// Só sobrou o laço vazio da versão de depuração.
void parsemap_block_datums() {}

// [decomp] 0x001d2538 parsemap_block_memory_discard
void parsemap_block_memory_discard() {
    FileDiscard = FileNextBlock;
    pFileDiscardHeader = pBinFileHeader;
}

// [decomp] 0x001d2550 parsemap_reset_to_block_memory_discard
void parsemap_reset_to_block_memory_discard() {
    if (BinFileVersion == 1) {
        pBinFileHeader = pFileDiscardHeader;
        FileNextBlock = reinterpret_cast<uint8_t*>(FileBase) + *pFileDiscardHeader + 4;
        return;
    }
    FileNextBlock = FileDiscard;
}

// [decomp] 0x001d00c0 parseentity_fixup_entity
// Ajusta o tipo do posicionamento estático. Vários tipos viram 0x8019; 0x8017-0x8019 ganham o bit
// 0x8000000 quando estão na origem sem rotação; 0xc023 e 0xc047 copiam parâmetros para misc.
void parseentity_fixup_entity(cel_tag* cel, uint32_t* glist_flags, level_tag* level) {
    *glist_flags = cel->glist != nullptr ? cel->glist->flags : 0;
    const uint32_t type = cel->type & 0xffff;
    uint8_t* misc = cel->misc;  // misc[i] = cel + 0x98 + i
    const auto at_origin = [cel] {
        return Vec_IsEqual(&cel->pos, &CONSTANT_ZERO_VECTOR, 0.0f) && Vec_IsEqual(&cel->rot, &CONSTANT_ZERO_VECTOR, 0.0f);
    };
    switch (type) {
        case 0xf:
        case 0x20:
        case 0x4e:
        case 0xdd:
            cel->type = type;
            return;
        case 0x34:
        case 0xd8:
        case 0xd9:
        case 0xda:
        case 0xdf:
        case 0xe0:
        case 0xe1:
        case 0xe2:
        case 0xe3:
        case 0xe4:
        case 0xf2:
        case 0xf3:
            cel->type = 0x8019;
            return;
        case 0x803d:
            cel->type = 0x28019;
            return;
        case 0x8017:
        case 0x8018:
            cel->type = type;
            if (at_origin()) cel->type |= 0x8000000;
            return;
        case 0x8019:
            cel->type = 0x8019;
            if (at_origin()) cel->type |= 0x8000000;
            misc[0xa3 - 0x98] = static_cast<uint8_t>(uint8_t(level->params[0]) | uint8_t(level->params[1]) << 4);
            return;
        case 0xc023:
        case 0xc047: {
            const bool is_c023 = type == 0xc023;
            cel->type = is_c023 ? 0x804c023 : 0x804c047;
            if (level->params[3] != 0) cel->type |= 0x10000000;
            cel->pos.x = 0.0f;
            if (is_c023) {
                std::memcpy(&cel->water_y, &cel->box_max.y, 4);
                cel->pos.y = cel->pos.z = 0.0f;
                misc[0] = misc[1] = 0x98;  // u16 0x9898
                misc[0xa1 - 0x98] = uint8_t(level->params[4]);
                misc[0x9a - 0x98] = uint8_t(level->params[0]);
                misc[0x9c - 0x98] = uint8_t(level->params[1]);
                misc[0x9e - 0x98] = uint8_t(level->params[2]);
            } else {
                cel->pos.z = cel->pos.y = 0.0f;
                misc[0xa1 - 0x98] = uint8_t(level->params[4]);
                misc[0xa2 - 0x98] = uint8_t(level->params[5]);
                misc[0x9a - 0x98] = uint8_t(level->params[0]);
                misc[0x9c - 0x98] = uint8_t(level->params[1]);
                misc[0x9e - 0x98] = uint8_t(level->params[2]);
                misc[0x9b - 0x98] = uint8_t(level->params[6]);
                misc[0x9d - 0x98] = uint8_t(level->params[7]);
                misc[0x9f - 0x98] = uint8_t(level->params[8]);
                cel->water_y = -500.0f;
                const uint16_t w = static_cast<uint16_t>(level->params[9]);
                std::memcpy(misc, &w, 2);
            }
            // Sem cor nos bytes 0x9a, 0x9c e 0x9e (máscara 0x00ff00ff00ff0000 do u64 em 0x98), usa 0x3c.
            if (misc[2] == 0 && misc[4] == 0 && misc[6] == 0) misc[2] = misc[6] = misc[4] = 0x3c;
            return;
        }
        default:
            return;
    }
}

// [decomp] 0x001d0fb0 parseentity_transform_bounding_box
// Leva o centro da esfera da entidade para o mundo (RotMatrix(rot) com translação pos). O raio
// (w) fica o mesmo: as instruções da VU0 escrevem só xyz.
void parseentity_transform_bounding_box(cel_tag* cel) {
    MATRIX m;
    RotMatrix(&cel->rot, &m);
    Mat_SetTrans(&cel->pos, &m);
    const celglist_tag* g = cel->glist;
    if (g == nullptr) return;
    const _VECTOR& s = g->sphere;
    cel->sphere.x = m.m[0][0] * s.x + m.m[1][0] * s.y + m.m[2][0] * s.z + m.m[3][0];
    cel->sphere.y = m.m[0][1] * s.x + m.m[1][1] * s.y + m.m[2][1] * s.z + m.m[3][1];
    cel->sphere.z = m.m[0][2] * s.x + m.m[1][2] * s.y + m.m[2][2] * s.z + m.m[3][2];
    cel->sphere.w = s.w;
}
