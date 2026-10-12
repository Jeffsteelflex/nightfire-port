// Testes do port de psiCreateMapTextures, psiCreateEntityGfx e auxiliares (src/platform/psi_gfx.cpp).

#include <cstring>
#include <vector>

#include "engine/memory.h"
#include "platform/psi.h"
#include "platform/psi_gfx.h"
#include "test_framework.h"

namespace {

// Conteúdo de bloco precedido do cabeçalho {id << 24 | tamanho}, como dentro de um item de mapa.
struct BlockBuf {
    std::vector<uint8_t> bytes;
    BlockBuf(uint8_t id, std::vector<uint8_t> payload) {
        const uint32_t header = uint32_t(id) << 24 | uint32_t(payload.size() + 4);
        bytes.resize(4);
        std::memcpy(bytes.data(), &header, 4);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    uint8_t* payload() { return bytes.data() + 4; }
};

struct Recorder {
    std::vector<int> textures;
    int entities = 0;
    uint32_t first_word_at_call = 0;
};

void fresh_heap() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    psi_gfx_debug_power_on();
}

// Quatro texturas 4x4 de 4 bits: 0 e 1 iguais, 2 diferente (30 quadros de vídeo por quadro),
// 3 com largura 1.
struct FourTextures {
    BlockBuf px_a{0x16, std::vector<uint8_t>(8, 0x11)};
    BlockBuf px_b{0x16, std::vector<uint8_t>(8, 0x11)};
    BlockBuf px_c{0x16, std::vector<uint8_t>(8, 0x23)};
    BlockBuf px_d{0x16, std::vector<uint8_t>(2, 0x00)};
    BlockBuf pal_a{0x12, std::vector<uint8_t>(64, 0x40)};
    BlockBuf pal_b{0x12, std::vector<uint8_t>(64, 0x40)};
    BlockBuf pal_c{0x12, std::vector<uint8_t>(64, 0x7f)};
    BlockBuf pal_d{0x12, std::vector<uint8_t>(64, 0x00)};
    bmpheader_tag bmp[4]{};
    pallheader_tag pall[4]{};
    map_tag map{};

    FourTextures() {
        BlockBuf* px[4] = {&px_a, &px_b, &px_c, &px_d};
        BlockBuf* pal[4] = {&pal_a, &pal_b, &pal_c, &pal_d};
        for (int i = 0; i < 4; ++i) {
            bmp[i].width1 = i == 3 ? 0 : 3;
            bmp[i].height1 = 3;
            bmp[i].frames = 1;
            bmp[i].data = px[i]->payload();
            pall[i].data = pal[i]->payload();
        }
        bmp[2].frames = 1 | (30 << 8);
        map.pall = pall;
        map.pall_count = 4;
        map.bmp = bmp;
        map.bmp_count = 4;
        map.tex = static_cast<maptex_tag*>(Mem_Malloc(4 * 8, 0xb08, 0));
    }
};

}  // namespace

TEST(psi_gfx_helpers) {
    CHECK(crc32buf("123456789", 9) == 0xcbf43926u);  // valor de referência do CRC-32
    CHECK(GetBitPos(48) == 6 && GetBitPos(64) == 6 && GetBitPos(65) == 7 && GetBitPos(2000) == 0);
    CHECK(GetMinWidth(64) == 1 && GetMinWidth(65) == 2 && GetMinWidth(256) == 4 && GetMinWidth(1024) == 16);
    const uint8_t a[3] = {1, 2, 3}, b[3] = {1, 2, 4};
    CHECK(ByteCompare(a, a, 3) == 1 && ByteCompare(a, b, 3) == 0 && ByteCompare(a, b, 2) == 1);
}

TEST(psi_create_map_textures_dedups_and_builds_records) {
    fresh_heap();
    FourTextures t;
    Recorder rec;
    g_psi_gfx_backend.user = &rec;
    g_psi_gfx_backend.create_texture = [](void* user, int index, const bmpheader_tag&, const uint8_t*, uint32_t,
                                          const uint8_t*, uint32_t) {
        static_cast<Recorder*>(user)->textures.push_back(index);
    };
    psiCreateMapTextures(&t.map);
    g_psi_gfx_backend = {};

    CHECK(NumPS2TexturesLoaded == 2);  // a 1 reaproveita a 0 e a 3 não vira textura
    CHECK(rec.textures.size() == 2 && rec.textures[0] == 0 && rec.textures[1] == 1);
    const pstex_tag* r0 = reinterpret_cast<const pstex_tag*>(nf::raw(t.bmp[0].data));
    const pstex_tag* r1 = reinterpret_cast<const pstex_tag*>(nf::raw(t.bmp[1].data));
    const pstex_tag* r2 = reinterpret_cast<const pstex_tag*>(nf::raw(t.bmp[2].data));
    CHECK(t.map.tex[0].rec == r0 && t.map.tex[1].rec == r1 && r0 != r1);
    CHECK(r0->tex_index == 0 && r1->tex_index == 0 && r2->tex_index == 1);
    CHECK(r1->pixels == r0->pixels);  // a cópia do registro aponta para os mesmos dados
    CHECK(Tex[0].shared == 1 && Tex[0].rec == r0 && Tex[0].dma != nullptr);
    CHECK(r0->pixels != t.px_a.payload() && std::memcmp(r0->pixels, t.px_a.payload(), 8) == 0);
    CHECK(r0->width == 4 && r0->height == 8 && r0->colors == 16 && r0->min_width == 2 && r0->unknown_1f == 2);
    CHECK(r0->frame_rate == 1 && r2->frame_rate == 2);  // 60 / 30
    CHECK(Tex[0].vram_width == 128 && Tex[0].vram_height == 128 && Tex[0].width == 4 && Tex[0].height == 4);
    CHECK(t.bmp[3].data == t.px_d.payload());  // largura 1: bmp continua apontando para os pixels
    CHECK(psi_gfx_map_texture(&t.map, 0) == 0 && psi_gfx_map_texture(&t.map, 1) == 0);
    CHECK(psi_gfx_map_texture(&t.map, 2) == 1 && psi_gfx_map_texture(&t.map, 3) == -1);
    // Efeito colateral do original: o modo de alocação fica em MALLOC_TOP.
    CHECK(Mem_SetMallocMethod(MALLOC_BEST) == MALLOC_TOP);
}

TEST(psi_create_entity_gfx_relocates_block_and_texture_refs) {
    fresh_heap();
    FourTextures t;
    psiCreateMapTextures(&t.map);

    // Bloco 0x2d: cabeçalho em 0x20 {1 caixa em 0x30, sem extra}; a caixa aponta para uma cadeia de
    // DMA em 0x70 e para a lista de texturas em 0xa0, que dá as etiquetas REF nos deslocamentos 0,
    // 0x10 e 0x20 da cadeia (texturas 0, 1 e 2 do item no endereço de cada etiqueta).
    std::vector<uint32_t> w(0xc0 / 4, 0);
    w[0] = 0x2d000000 | 0xc0;
    w[1] = 0x20;
    w[0x20 / 4] = 1;
    w[0x24 / 4] = 0x30;
    w[(0x30 + 0x20) / 4] = 0x70;
    w[(0x30 + 0x24) / 4] = 0xa0;
    w[0x74 / 4] = 0;
    w[0x84 / 4] = 1;
    w[0x94 / 4] = 2;
    w[0xa0 / 4] = 0x00;
    w[0xa4 / 4] = 0x10;
    w[0xa8 / 4] = 0x20;
    w[0xac / 4] = 0xffffffff;
    uint8_t* blk = reinterpret_cast<uint8_t*>(w.data());

    entgfx_tag gfx{};
    gfx.map = &t.map;
    gfx.ps2_gfx = blk;
    celglist_tag glist{};
    glist.gfx = &gfx;

    Recorder rec;
    g_psi_gfx_backend.user = &rec;
    g_psi_gfx_backend.create_entity = [](void* user, const celglist_tag* g, const map_tag*) {
        auto* r = static_cast<Recorder*>(user);
        ++r->entities;
        std::memcpy(&r->first_word_at_call, g->gfx->ps2_gfx + 4, 4);
        return 7;
    };
    psiCreateEntityGfx(&glist, &t.map, 0);
    g_psi_gfx_backend = {};

    CHECK(rec.entities == 1 && rec.first_word_at_call == 0x20);  // o backend vê o bloco cru
    CHECK(psi_gfx_entity_mesh(&glist) == 7);
    const uint32_t* hdr = reinterpret_cast<const uint32_t*>(nf::raw(gfx.ps2_gfx));
    CHECK(gfx.ps2_gfx == blk + 0x20);
    CHECK(hdr[1] == nf::p32_u(blk + 0x30) && hdr[2] == 0);
    const uint32_t* box = reinterpret_cast<const uint32_t*>(blk + 0x30);
    CHECK(box[0x20 / 4] == nf::p32_u(blk + 0x70));
    CHECK(box[0x24 / 4] == nf::p32_u(blk + 0xa0));
    // Referências: índice de textura do item -> pacote do GS da entrada de Tex.
    CHECK(w[0x74 / 4] == nf::p32_u(Tex[0].dma));
    CHECK(w[0x84 / 4] == nf::p32_u(Tex[0].dma));  // a 1 reaproveita a 0
    CHECK(w[0x94 / 4] == nf::p32_u(Tex[1].dma));
    CHECK(w[0xa0 / 4] == 0 && w[0xa4 / 4] == 1 && w[0xa8 / 4] == 0xffffffff);  // índices de Tex, sem repetição

    // Bloco vazio (cabeçalho em 0x10): zera o cabeçalho.
    std::vector<uint32_t> e(8, 0xaaaaaaaa);
    e[1] = 0x10;
    gfx.ps2_gfx = reinterpret_cast<uint8_t*>(e.data());
    psiCreateEntityGfx(&glist, &t.map, 0);
    CHECK(e[4] == 0 && e[5] == 0 && e[6] == 0);
    Mem_SetMallocMethod(MALLOC_BEST);
}
