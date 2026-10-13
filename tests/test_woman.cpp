// Testes do fundo animado da pausa (platform/psi_woman.h: PS2SetUpWoman, psiDecompressWoman e o preparo da textura no
// psiPreGameRun) com um RLE feito à mão e um fogo sintético (nada do jogo); e do decode_texture_frame (data/map_item.h)
// que o leva para RGBA.

#include <cstring>
#include <memory>
#include <vector>

#include "data/map_item.h"
#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "engine/memory.h"
#include "platform/psi.h"
#include "platform/psi_gfx.h"
#include "platform/psi_woman.h"
#include "test_framework.h"

extern uint32_t PS2FramesToSkip;  // engine/menu.cpp

namespace {

constexpr int kFireBytes = 128 * 256;

// O fogo é uma rampa (o valor diz de onde veio o byte) e a paleta dele tem cada cor diferente; a 0x030000db começa com a
// imagem e a paleta "guardadas" (0x55 e 0x33), que o preparo da carga troca.
uint8_t fire_value(uint32_t i) { return uint8_t((i * 7u + 3u) & 0xff); }

struct Recorder {
    int calls = 0;
    int index = -1;
    std::vector<uint8_t> pixels, palette;
};

struct WomanWorld {
    std::vector<uint8_t> fire_px = std::vector<uint8_t>(kFireBytes);
    std::vector<uint8_t> fire_pal = std::vector<uint8_t>(0x400);
    std::vector<uint8_t> woman_px = std::vector<uint8_t>(kWomanBytes, 0x55);
    std::vector<uint8_t> woman_pal = std::vector<uint8_t>(0x400, 0x33);
    std::unique_ptr<pstex_tag> fire_rec = std::make_unique<pstex_tag>();
    std::unique_ptr<pstex_tag> woman_rec = std::make_unique<pstex_tag>();
    std::unique_ptr<maptex_tag> fire_tex = std::make_unique<maptex_tag>();
    std::unique_ptr<maptex_tag> woman_tex = std::make_unique<maptex_tag>();
    std::vector<uint8_t> rle;
    Recorder rec;

    WomanWorld() {
        psiMemHeapSize = 1u << 20;
        PtrHeap = nullptr;
        Mem_Init();
        hashtable_debug_power_on();
        hashtable_initialise();
        psi_woman_debug_power_on();
        PS2FramesToSkip = 0;
        for (uint32_t i = 0; i < uint32_t(kFireBytes); ++i) fire_px[i] = fire_value(i);
        for (uint32_t c = 0; c < 256; ++c) {
            fire_pal[c * 4 + 0] = uint8_t(c);
            fire_pal[c * 4 + 1] = uint8_t(255 - c);
            fire_pal[c * 4 + 2] = uint8_t(c ^ 0x5a);
            fire_pal[c * 4 + 3] = 0x80;
        }
        setup(*fire_rec, fire_px.data(), fire_pal.data(), 128, 256, 3);
        setup(*woman_rec, woman_px.data(), woman_pal.data(), 128, 128, 4);
        *fire_tex = maptex_tag{fire_rec.get(), 128, 256};
        *woman_tex = maptex_tag{woman_rec.get(), 128, 128};
        hashtable_additem(kWomanFireTexture, fire_tex.get());
        hashtable_additem(kWomanTexture, woman_tex.get());
        g_psi_gfx_backend = {};
        g_psi_gfx_backend.user = &rec;
        g_psi_gfx_backend.update_texture = [](void* user, int index, const bmpheader_tag& h, const uint8_t* px,
                                              uint32_t px_bytes, const uint8_t* pal, uint32_t pal_bytes) {
            auto* r = static_cast<Recorder*>(user);
            ++r->calls;
            r->index = index;
            CHECK(h.flags == 1 && h.width1 == 127 && h.height1 == 127 && (h.frames & 0xff) == 1);
            r->pixels.assign(px, px + px_bytes);
            r->palette.assign(pal, pal + pal_bytes);
        };
    }
    ~WomanWorld() {
        g_psi_gfx_backend = {};
        PS2FramesToSkip = 0;
    }
    static void setup(pstex_tag& r, uint8_t* px, uint8_t* pal, int w, int h, int index) {
        r = pstex_tag{};
        r.pixels = px;
        r.palette = pal;
        r.width = uint16_t(w);
        r.height = uint16_t(h);
        r.colors = 0x100;
        r.frames = 1;
        r.tex_index = uint16_t(index);
    }
    // O RLE: um trecho zerado, depois um bloco de trechos (cópia, zero, cópia...), alternando até o fim do quadro.
    void zeros(uint32_t n) {
        rle.push_back(uint8_t(n >> 8));
        rle.push_back(uint8_t(n));
    }
    void block(std::initializer_list<uint8_t> runs) {
        zeros(uint32_t(runs.size()));
        rle.insert(rle.end(), runs.begin(), runs.end());
    }
    void load() {
        PS2SetUpWoman(rle.data());
        psi_woman_set_end(rle.data() + rle.size());
    }
    // A fonte do quadro f no fogo (a conta do PS2).
    static uint32_t source(uint32_t f) { return (f & 0x3f) * 0x100 + (0x80 - (f & 0x7f)); }
};

}  // namespace

TEST(woman_pack_takes_the_fire_palette_and_clears_the_image) {
    WomanWorld w;
    psi_woman_pack_textures();
    CHECK(w.woman_pal == w.fire_pal);  // a paleta do fogo, os bytes como estão (o arranjo CSM1 vai junto)
    CHECK(std::vector<uint8_t>(kWomanBytes, 0) == w.woman_px);  // o desenho guardado nunca aparece
    CHECK(w.rec.calls == 1 && w.rec.index == 4 && w.rec.palette == w.fire_pal && w.rec.pixels == w.woman_px);
    CHECK(g_psi_woman.packs == 1 && g_psi_woman.texture == 4 && g_psi_woman.guards == 0);
    // Sem a 0x030000db não há o que preparar (o PS2 não faz nada); com ela e sem o fogo, o PS2 leria a paleta em 0.
    hashtable_deleteitem(kWomanFireTexture);
    psi_woman_pack_textures();
    CHECK(g_psi_woman.guards == 1 && w.rec.calls == 1);
    hashtable_deleteitem(kWomanTexture);
    psi_woman_pack_textures();
    CHECK(g_psi_woman.guards == 1 && w.rec.calls == 1);
}

TEST(woman_decodes_a_hand_built_frame_every_other_tick) {
    WomanWorld w;
    // Quadro 0: 100 zeros, um bloco (5 do fogo, 2 zeros, 3 do fogo), o resto zerado. Quadro 1: nenhum zero, um bloco de
    // um trecho de 255 do fogo, o resto zerado.
    w.zeros(100);
    w.block({5, 2, 3});
    w.zeros(uint32_t(kWomanBytes) - 110);
    w.zeros(0);
    w.block({255});
    w.zeros(uint32_t(kWomanBytes) - 255);
    w.load();
    psi_woman_pack_textures();
    const int after_pack = w.rec.calls;

    psiDecompressWoman();  // skipit.288: 2 - 1 = 1, ainda não
    CHECK(g_psi_woman.decoded == 0 && w.rec.calls == after_pack && g_psi_woman.last_frame == -1);
    psiDecompressWoman();  // 0: o quadro 0
    CHECK(g_psi_woman.decoded == 1 && WomanFrame == 1 && g_psi_woman.last_frame == 0 && w.rec.calls == after_pack + 1);
    const uint32_t s0 = WomanWorld::source(0);
    CHECK(s0 == 0x80);
    bool ok = true;
    for (int i = 0; i < kWomanBytes; ++i) {
        uint8_t want = 0;
        if ((i >= 100 && i < 105) || (i >= 107 && i < 110)) want = fire_value(s0 + uint32_t(i));
        ok = ok && w.woman_px[size_t(i)] == want;
    }
    CHECK(ok);
    CHECK(w.rec.pixels == w.woman_px && w.rec.index == 4);
    CHECK(pWoman == w.rle.data() + 2 + 2 + 3 + 2);

    psiDecompressWoman();  // de novo 2 - 1: nada
    CHECK(g_psi_woman.decoded == 1);
    psiDecompressWoman();  // o quadro 1: a fonte sobe duas linhas e anda uma coluna
    const uint32_t s1 = WomanWorld::source(1);
    CHECK(s1 == 0x100 + 0x7f && WomanFrame == 2 && g_psi_woman.decoded == 2);
    ok = true;
    for (int i = 0; i < kWomanBytes; ++i) ok = ok && w.woman_px[size_t(i)] == (i < 255 ? fire_value(s1 + uint32_t(i)) : 0);
    CHECK(ok);
    CHECK(g_psi_woman.guards == 0);

    // A 30 quadros (PS2FramesToSkip = 2), um quadro a cada tick.
    PS2FramesToSkip = 2;
    const uint32_t before = g_psi_woman.decoded;
    PS2SetUpWoman(w.rle.data());
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.decoded == before + 2);
}

TEST(woman_cycle_returns_to_the_first_frame_after_348) {
    WomanWorld w;
    // O quadro k copia um byte do fogo na posição k (k < 348): dá para saber qual quadro está na textura.
    for (uint32_t k = 0; k < kWomanFrames; ++k) {
        w.zeros(k);
        w.block({1});
        w.zeros(uint32_t(kWomanBytes) - k - 1);
    }
    w.load();
    psi_woman_pack_textures();
    const auto only_pixel = [&](uint32_t k) {
        for (uint32_t i = 0; i < uint32_t(kWomanBytes); ++i) {
            const uint8_t want = i == k ? fire_value(WomanWorld::source(k) + k) : 0;
            if (w.woman_px[i] != want) return false;
        }
        return true;
    };
    bool ok = true;
    for (uint32_t k = 0; k < kWomanFrames; ++k) {
        psiDecompressWoman();
        psiDecompressWoman();
        ok = ok && g_psi_woman.last_frame == int32_t(k) && only_pixel(k);
    }
    CHECK(ok);
    CHECK(WomanFrame == 0 && pWoman == PS2Woman && g_psi_woman.decoded == kWomanFrames);
    CHECK(g_psi_woman.cycle_bytes == w.rle.size() && g_psi_woman.item_bytes == w.rle.size());
    psiDecompressWoman();
    psiDecompressWoman();  // o 349.º é o primeiro de novo (o PS2 não segura o último quadro)
    CHECK(g_psi_woman.last_frame == 0 && WomanFrame == 1 && only_pixel(0));
    CHECK(g_psi_woman.guards == 0);
    char line[256];
    psi_woman_describe(line, sizeof(line));
    CHECK(std::strstr(line, "fundo_pausa quadro 0 proximo 1 decodificados 349 ") == line);
}

TEST(woman_guards_where_the_ps2_would_read_out_of_bounds) {
    WomanWorld w;
    // Sem o item: nada (o PS2 leria o RLE de um ponteiro velho).
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.guards == 1 && g_psi_woman.decoded == 0);
    // Um trecho zerado maior que o quadro: para no fim do quadro.
    w.zeros(uint32_t(kWomanBytes) + 0x1000);
    w.load();
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.guards == 2 && g_psi_woman.decoded == 1);
    // Um item que acaba no meio do quadro: para no fim do item.
    w.rle.clear();
    w.zeros(10);
    w.zeros(3);
    w.rle.push_back(4);
    w.load();
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.guards >= 3 && pWoman <= w.rle.data() + w.rle.size());
    // Sem a 0x030000db (o PS2 não confere o nulo): nada.
    w.load();
    hashtable_deleteitem(kWomanTexture);
    const uint32_t g = g_psi_woman.guards, d = g_psi_woman.decoded;
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.guards == g + 1 && g_psi_woman.decoded == d);
    // Sem o fogo, nada e sem guarda (o PS2 confere esse nulo).
    hashtable_deleteitem(kWomanFireTexture);
    psiDecompressWoman();
    psiDecompressWoman();
    CHECK(g_psi_woman.guards == g + 1);
}

TEST(woman_frame_decodes_with_the_csm1_palette_like_the_load) {
    // Índices de 8 bits com a paleta CSM1: as faixas 8-15 e 16-23 de cada grupo de 32 trocadas (o índice 8 é a cor 16
    // da paleta crua, o 40 é a 48), como decode_texture faz na carga; e a memória do resultado fica.
    std::vector<uint8_t> px(16 * 16), pal(0x400);
    for (size_t i = 0; i < px.size(); ++i) px[i] = uint8_t(i);
    for (size_t c = 0; c < 256; ++c) {
        pal[c * 4] = uint8_t(c);
        pal[c * 4 + 1] = 1;
        pal[c * 4 + 2] = 2;
        pal[c * 4 + 3] = uint8_t(c < 128 ? 0x80 : 0x40);
    }
    nf::Texture frame, load;
    CHECK(nf::decode_texture_frame(16, 16, true, nf::ByteView(px.data(), px.size()), nf::ByteView(pal.data(), pal.size()),
                                   frame));
    CHECK(nf::decode_texture(16, 16, 1, true, nf::ByteView(px.data(), px.size()), nf::ByteView(pal.data(), pal.size()),
                             load));
    CHECK(frame.rgba == load.rgba && frame.has_alpha == load.has_alpha && frame.width == 16 && frame.height == 16);
    CHECK(frame.rgba[0] == 0 && frame.rgba[8 * 4] == 16 && frame.rgba[16 * 4] == 8 && frame.rgba[40 * 4] == 48 &&
          frame.rgba[7 * 4] == 7 && frame.rgba[24 * 4] == 24);
    const uint8_t* data = frame.rgba.data();
    px.assign(px.size(), 0);
    CHECK(nf::decode_texture_frame(16, 16, true, nf::ByteView(px.data(), px.size()), nf::ByteView(pal.data(), pal.size()),
                                   frame));
    CHECK(frame.rgba.data() == data && frame.rgba[8 * 4] == 0 && !frame.has_alpha);
    // Tamanho errado: nada.
    CHECK(!nf::decode_texture_frame(16, 8, true, nf::ByteView(px.data(), px.size()), nf::ByteView(pal.data(), pal.size()),
                                    frame));
}
