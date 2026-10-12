// Port de psiCreateMapTextures, psiCreateEntityGfx, psiResetResources e auxiliares. Ver psi_gfx.h.

#include "platform/psi_gfx.h"

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "engine/memory.h"
#include "engine/parsemap.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_loading.h"

PS2TEX_tag Tex[kMaxPS2Textures];
int32_t NumPS2TexturesLoaded = 0;
int32_t nTexLoad = 0;
int32_t TotalSize = 0;
int32_t PS2LSActive = 0;
int8_t PS2FirstGameRun = 1;
int32_t TextureBufBase = 0;
PsiGfxBackend g_psi_gfx_backend;
PsiGfxGuards g_psi_gfx_guards;

namespace {

int32_t TotalCacheStart = 0, TotalCacheSize = 0, CacheStart = 0, CacheSize = 0, iLock = 0, CacheFrame = 0;

// Só no port: o que o backend criou, por estrutura do heap (zerado em psiResetResources).
std::unordered_map<const celglist_tag*, int> g_entity_mesh;
std::unordered_map<const map_tag*, std::vector<int>> g_map_textures;

// crc_32_tab (0x00264bd8): a tabela padrão do CRC-32 (polinômio 0xedb88320), conferida contra o
// executável.
const std::array<uint32_t, 256> crc_32_tab = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}();

uint32_t block_payload_bytes(const uint8_t* payload) {
    uint32_t header;
    std::memcpy(&header, payload - 4, 4);
    return (header & 0xffffff) - 4;
}

}  // namespace

int psi_gfx_entity_mesh(const celglist_tag* glist) {
    const auto it = g_entity_mesh.find(glist);
    return it == g_entity_mesh.end() ? -1 : it->second;
}

int psi_gfx_map_texture(const map_tag* map, int item_texture) {
    const auto it = g_map_textures.find(map);
    if (it == g_map_textures.end() || item_texture < 0 || item_texture >= int(it->second.size())) return -1;
    return it->second[size_t(item_texture)];
}

void psi_gfx_debug_power_on() {
    std::memset(Tex, 0, sizeof(Tex));
    NumPS2TexturesLoaded = nTexLoad = TotalSize = 0;
    PS2LSActive = 0;
    PS2FirstGameRun = 1;
    g_entity_mesh.clear();
    g_map_textures.clear();
    g_psi_gfx_guards = {};
}

// [decomp] 0x00120670 crc32buf
uint32_t crc32buf(const char* data, uint32_t size) {
    uint32_t crc = 0xffffffff;
    for (; size != 0; --size) crc = crc_32_tab[(crc ^ uint8_t(*data++)) & 0xff] ^ crc >> 8;
    return ~crc;
}

// [decomp] 0x001206c0 ByteCompare
// Devolve 1 quando os blocos são iguais.
uint32_t ByteCompare(const uint8_t* a, const uint8_t* b, int32_t size) {
    for (int32_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

// [decomp] 0x0011f808 GetBitPos
// Expoente da potência de 2 que cobre o valor (0 acima de 1024).
int32_t GetBitPos(int32_t v) {
    if (v < 2) return 0;
    if (v < 3) return 1;
    if (v < 5) return 2;
    if (v < 9) return 3;
    if (v < 0x11) return 4;
    if (v < 0x21) return 5;
    if (v < 0x41) return 6;
    if (v < 0x81) return 7;
    if (v < 0x101) return 8;
    if (v < 0x201) return 9;
    if (v < 0x401) return 10;
    return 0;
}

// [decomp] 0x001205c8 GetMinWidth
// Largura em unidades de 64 pixels (TBW do GS), no mínimo 1.
int32_t GetMinWidth(int32_t width) {
    if (width < 0x41) return 1;
    return (1 << GetBitPos(width)) / 64;
}

// [decomp] 0x00109688 initcache
// Cache de texturas da VRAM do PS2; no PC só guarda os valores.
void initcache(int32_t start, int32_t size) {
    TotalCacheStart = start;
    TotalCacheSize = size;
    iLock = 0;
    CacheFrame = 1;
    CacheStart = start + size / 2;
    CacheSize = size / 2;
}

// [decomp] 0x0011d110 FillTextureDMA
// Monta o pacote de registradores do GS para usar a textura (GIFtag + TEX0/TEX1/CLAMP). No PC o
// pacote não é usado, mas continua alocado no heap como no original.
void FillTextureDMA(int index) {
    uint32_t* p = static_cast<uint32_t*>(Mem_Malloc(0x40, 0x1180, 0));
    Tex[NumPS2TexturesLoaded].dma = p;
    p = Tex[index].dma;
    const uint32_t packet[16] = {0, 0, 0x10000000, 0x50000003, 0x8002, 0x10000000, 0xe, 0,
                                 0, 0, 6, 0, 0, 0, 8, 0};
    std::memcpy(p, packet, sizeof(packet));
}

// [decomp] 0x0011d200 psiCreateMapTextures
// Um registro pstex_tag por textura do item. Texturas iguais (mesmo CRC dos pixels e mesmos bytes
// de pixels e paleta) já carregadas por outro item reaproveitam a entrada de Tex. Pixels e paleta
// são copiados para o heap (os originais ficam na parte descartada do arquivo) e bmp[i].data passa
// a apontar para o registro. Texturas com largura ou altura 1 ficam sem registro completo.
void psiCreateMapTextures(map_tag* map) {
    maptex_tag* tex = map->tex;
    std::vector<int>& indices = g_map_textures[map];
    indices.assign(map->bmp_count, -1);
    for (int32_t i = 0; i < int32_t(map->bmp_count); ++i) {
        if (uint32_t(i) >= map->pall_count) {
            // Divergência: o original escreveria além de map->tex.
            ++g_psi_gfx_guards.texture_without_palette;
            continue;
        }
        pstex_tag* rec = static_cast<pstex_tag*>(Mem_Malloc(0x20, 0x904, 0));
        tex[i].rec = rec;
        rec->unknown_08 = 0;
        bmpheader_tag& bmp = map->bmp[i];
        const uint16_t height1 = bmp.height1;
        const uint32_t width = uint32_t(bmp.width1) + 1;
        const uint32_t height = uint32_t(height1) + 1;
        const uint8_t frames = uint8_t(bmp.frames & 0xff);
        uint32_t pixel_bytes = width * height * frames;
        const int32_t colors = (bmp.flags & 1) ? 0x100 : 0x10;
        if (colors == 0x10) pixel_bytes >>= 1;
        uint8_t* pixels = bmp.data;
        uint8_t* palette = map->pall[i].data;
        const uint32_t crc = crc32buf(reinterpret_cast<const char*>(pixels), pixel_bytes);
        int32_t found = -1;
        for (int32_t k = 0; k < NumPS2TexturesLoaded; ++k) {
            if (Tex[k].crc == crc) {
                found = k;
                break;
            }
        }
        bool reuse = false;
        if (found != -1) {
            const pstex_tag* old = Tex[found].rec;
            reuse = ByteCompare(palette, old->palette, colors << 2) != 0 &&
                    ByteCompare(pixels, old->pixels, int32_t(pixel_bytes)) != 0;
            if (reuse) {
                amemcpy(rec, old, 0x20);
                ++Tex[old->tex_index].shared;
            }
        }
        if (!reuse) {
            rec->pixels = nullptr;
            rec->palette = nullptr;
            rec->frames = frames;
            const uint8_t rate = uint8_t(bmp.frames >> 8);
            rec->frame_rate = uint8_t(rate - 1) < 0x3b ? uint8_t(0x3c / rate) : 1;
            if (width < 2 || height < 2) continue;
            const int32_t min_width = GetMinWidth(int32_t(width));
            const uint32_t padded_height = height < 8 ? 8 : height;
            const int16_t w16 = int16_t(width);
            for (int32_t frame = 0; frame < frames; ++frame) {
                if (NumPS2TexturesLoaded >= kMaxPS2Textures) {
                    // Divergência: o original escreveria além da tabela.
                    ++g_psi_gfx_guards.table_full;
                    break;
                }
                PS2TEX_tag& t = Tex[NumPS2TexturesLoaded];
                t.crc = crc;
                t.width = w16;
                t.unknown_14 = 0xffff;
                t.height = int16_t(height);
                t.map_hash = uint16_t(MapHashCode);
                t.unknown_16 = 0;
                t.colors = uint16_t(colors);
                t.unknown_18 = 0;
                t.frame = uint8_t(frame);
                t.shared = 0;
                t.vram_width = uint16_t((w16 + 0x7f) & 0xff80);
                t.vram_height = colors == 0x10 ? uint16_t((height1 + 0x80) & 0xff80) : uint16_t((height1 + 0x40) & 0xffc0);
                t.rec = rec;
                if (frame == 0) {
                    rec->tex_index = uint16_t(NumPS2TexturesLoaded);
                    const tMallocMethod old = Mem_SetMallocMethod(MALLOC_TOP);
                    FillTextureDMA(NumPS2TexturesLoaded);
                    Mem_SetMallocMethod(old);
                    // Só no port: no PS2 a textura sobe para a VRAM mais tarde; aqui o backend a cria.
                    if (g_psi_gfx_backend.create_texture) {
                        g_psi_gfx_backend.create_texture(g_psi_gfx_backend.user, NumPS2TexturesLoaded, bmp, pixels,
                                                         block_payload_bytes(pixels), palette,
                                                         block_payload_bytes(palette));
                    }
                }
                ++NumPS2TexturesLoaded;
            }
            Mem_SetMallocMethod(MALLOC_TOP);
            uint8_t* palette_copy = static_cast<uint8_t*>(Mem_Malloc(uint32_t(colors) << 2, 0x2c10, 0));
            amemcpy(palette_copy, palette, size_t(colors) << 2);
            const tMallocMethod old = Mem_SetMallocMethod(MALLOC_BOTTOM);
            uint8_t* pixel_copy = static_cast<uint8_t*>(Mem_Malloc(pixel_bytes, 0x2b10, 0));
            amemcpy(pixel_copy, pixels, pixel_bytes);
            Mem_SetMallocMethod(old);
            ++nTexLoad;
            rec->pixels = pixel_copy;
            rec->palette = palette_copy;
            rec->width = uint16_t(width);
            rec->unknown_0c = 0;
            rec->height = uint16_t(padded_height);
            rec->min_width = uint16_t(min_width);
            rec->colors = uint16_t(colors);
            rec->unknown_1f = 1;
            if (min_width < 2) {
                rec->unknown_1f = 2;
                rec->min_width = 2;
            }
            if (colors == 0x10) {
                TotalSize += int32_t(padded_height << (rec->min_width & 0x1f)) / 2;
            } else {
                TotalSize += int32_t(padded_height << (rec->min_width & 0x1f));
            }
        }
        bmp.data = reinterpret_cast<uint8_t*>(rec);
        // Sem quadros, tex_index nunca é escrito (fica o que estava no heap).
        indices[size_t(i)] = rec->frames != 0 ? rec->tex_index : -1;
    }
}

// [decomp] 0x001171b0 psiCreateEntityGfx
// Converte os deslocamentos do bloco 0x2d em ponteiros: o cabeçalho ({quantidade, caixas, extra}),
// as cadeias de DMA e as listas de texturas de cada caixa (0x38 bytes) e os ponteiros do skinning.
// As referências de textura na cadeia de DMA passam a apontar para o pacote de Tex, e a lista de
// texturas da caixa vira a lista dos índices de Tex usados, sem repetição e terminada em -1.
uint32_t psiCreateEntityGfx(celglist_tag* glist, map_tag*, uint32_t) {
    entgfx_tag* gfx = glist->gfx;
    if (gfx == nullptr || gfx->ps2_gfx == nullptr) {
        // Divergência: o original leria o cabeçalho no endereço 4.
        ++g_psi_gfx_guards.entity_without_gfx;
        return 0;
    }
    // Só no port: o backend decodifica a geometria antes de os deslocamentos virarem ponteiros.
    if (g_psi_gfx_backend.create_entity) {
        const int mesh = g_psi_gfx_backend.create_entity(g_psi_gfx_backend.user, glist, gfx->map);
        if (mesh >= 0) g_entity_mesh[glist] = mesh;
    }
    uint8_t* blk = gfx->ps2_gfx;
    const auto at = [blk](uint32_t offset) { return blk + (offset & ~3u); };
    int32_t header_offset;
    std::memcpy(&header_offset, blk + 4, 4);
    uint32_t* hdr = reinterpret_cast<uint32_t*>(blk + (header_offset / 4) * 4);
    gfx->ps2_gfx = reinterpret_cast<uint8_t*>(hdr);
    if (header_offset == 0x10) {
        hdr[1] = 0;
        hdr[0] = 0;
        hdr[2] = 0;
        return 0;
    }
    uint8_t* boxes = at(hdr[1]);
    hdr[1] = nf::p32_u(boxes);
    const map_tag* map = gfx->map;
    for (int32_t i = 0; i < int32_t(hdr[0]); ++i) {
        uint32_t* box = reinterpret_cast<uint32_t*>(boxes + i * 0x38);
        uint32_t& dma = box[0x20 / 4];
        uint32_t& list_field = box[0x24 / 4];
        if (dma != 0) dma = nf::p32_u(at(dma));
        if (list_field == 0) continue;
        uint32_t* list = reinterpret_cast<uint32_t*>(at(list_field));
        list_field = nf::p32_u(list);
        if (dma == 0) continue;
        int32_t n = 0;
        if (list[0] != 0xffffffff) {
            uint32_t value = list[0];
            uint32_t* out = list;
            uint32_t* next = list;
            for (;;) {
                ++next;
                // value: deslocamento da etiqueta REF na cadeia; a palavra seguinte é o endereço,
                // que no arquivo guarda o índice da textura no item.
                uint32_t* ref = nf::p32_ptr<uint32_t>(dma) + (int32_t(value) / 4 + 1);
                const uint32_t item_texture = *ref;
                uint32_t tex_index = 0xffffffff;
                if (map != nullptr && item_texture < map->bmp_count) {
                    uint16_t idx;
                    std::memcpy(&idx, map->bmp[item_texture].data + 0x1a, 2);
                    tex_index = idx;
                }
                if (tex_index < uint32_t(kMaxPS2Textures)) {
                    *ref = nf::p32_u(Tex[tex_index].dma);
                } else {
                    // Divergência: o original leria fora de map->bmp ou de Tex.
                    ++g_psi_gfx_guards.bad_texture_ref;
                    *ref = 0;
                }
                int32_t k = 0;
                if (n > 0 && list[0] != tex_index) {
                    k = 1;
                    while (k < n && list[k] != tex_index) ++k;
                }
                if (k == n) {
                    *out++ = tex_index;
                    ++n;
                }
                if (*next == 0xffffffff) break;
                value = *next;
            }
        }
        list[n] = 0xffffffff;
    }
    if (hdr[2] != 0) {
        uint32_t* extra = reinterpret_cast<uint32_t*>(at(hdr[2]));
        hdr[2] = nf::p32_u(extra);
        if (extra[0] != 0) extra[0] = nf::p32_u(at(extra[0]));
        if (extra[2] != 0) extra[2] = nf::p32_u(at(extra[2]));
        if (extra[3] != 0) extra[3] = nf::p32_u(at(extra[3]));
    }
    return 0;
}

// PS2SafeCleanup (0x00121770) espera o GS terminar e libera os pacotes de DMA dos dois buffers de
// desenho. O PC não tem esses buffers.
void PS2SafeCleanup() {}

// PS2WipeVram (0x0011cab0) apaga a VRAM do GS com uploads de zeros (também os buffers de quadro: a tela fica preta).
// No PC o backend descarta as texturas em psiResetResources e o gancho da tela de carga (platform/psi_loading.h) pode
// mostrar a tela preta.
void PS2WipeVram() {
    ++g_psi_loading_stats.wipes;
    if (g_psi_loading_hook != nullptr) g_psi_loading_hook(PsiLoadingEvent::Wipe);
}

// [decomp] 0x0011f390 psiResetResources
void psiResetResources() {
    psiStopAllRumble();
    psiStopBackgroundMovie();
    PS2SafeCleanup();
    NumPS2TexturesLoaded = 0;
    initcache(TextureBufBase, 0x4000 - TextureBufBase);
    std::memset(FileBlockSize, 0, 0xc4);
    if (PS2LSActive == 0 && PS2FirstGameRun == 0) PS2WipeVram();
    // Só no port: a tabela recomeçou, então os recursos do backend e os registros do port também.
    g_entity_mesh.clear();
    g_map_textures.clear();
    if (g_psi_gfx_backend.reset) g_psi_gfx_backend.reset(g_psi_gfx_backend.user);
}
