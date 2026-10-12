// Testes de core/xxh3.cpp e data/texture_pack.cpp (hashes do PCSX2 para texturas de substituição).

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#include "core/xxh3.h"
#include "data/texture_pack.h"
#include "test_framework.h"

using namespace nf;

TEST(xxh3_official_vectors) {
    // Vetores de tests/sanity_test_vectors.h do xxHash (semente 0), sobre o buffer pseudoaleatório
    // de cli/xsum_sanity_check.c. Cobrem todos os caminhos: 0-16, 17-128, 129-240 e longo.
    std::vector<uint8_t> buf(4096);
    uint64_t gen = 2654435761u;
    for (uint8_t& b : buf) {
        b = uint8_t(gen >> 56);
        gen *= 11400714785074694797ull;
    }
    const struct {
        size_t len;
        uint64_t hash;
    } v[] = {{0, 0x2D06800538D394C2ull},    {3, 0x54247382A8D6B94Dull},    {8, 0x24CCC9ACAA9F65E4ull},
             {16, 0x981B17D36C7498C9ull},   {17, 0x796F5ACD3A60F862ull},   {100, 0x93CD95432B7D483Full},
             {128, 0xFCFF24126754D861ull},  {129, 0x98F1B0A679A2CA29ull},  {200, 0xBDDCA58935D7C038ull},
             {240, 0x81C3C2B67F568CCFull},  {241, 0xC5A639ECD2030E5Eull},  {1024, 0xDD85C9B5C1109C5Cull},
             {1025, 0xD870C0FA13211C6Aull}, {2048, 0xDD59E2C3A5F038E0ull}, {2367, 0xCB37AEB9E5D361EDull},
             {4096, 0xE91206429D1F48F9ull}};
    for (const auto& t : v) CHECK(xxh3_64(buf.data(), t.len) == t.hash);
}

TEST(gs_block_layouts) {
    // Cada texel do bloco ocupa uma posição diferente (as funções são bijeções).
    std::set<uint32_t> seen8, seen4;
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) seen8.insert(gs_block_offset8(x, y));
        for (uint32_t x = 0; x < 32; ++x) seen4.insert(gs_block_offset4(x, y));
    }
    CHECK(seen8.size() == 256 && *seen8.rbegin() == 255);
    CHECK(seen4.size() == 512 && *seen4.rbegin() == 511);
    // Algumas posições das tabelas de colunas do manual do GS (PSMT8 e PSMT4).
    CHECK(gs_block_offset8(1, 0) == 4 && gs_block_offset8(8, 0) == 2 && gs_block_offset8(0, 2) == 33);
    CHECK(gs_block_offset8(4, 2) == 1 && gs_block_offset8(0, 4) == 96 && gs_block_offset8(15, 15) == 255);
    CHECK(gs_block_offset4(1, 0) == 8 && gs_block_offset4(8, 0) == 2 && gs_block_offset4(0, 2) == 65);
    CHECK(gs_block_offset4(0, 4) == 192 && gs_block_offset4(31, 15) == 511);
}

TEST(gs_texture_hashes) {
    // 32x16 em 8 bits: dois blocos. O hash por blocos é o da sequência dos dois blocos
    // embaralhados; o linear, o dos índices em ordem.
    std::vector<uint8_t> tex(32 * 16);
    for (size_t i = 0; i < tex.size(); ++i) tex[i] = uint8_t(i * 7 + 3);
    std::vector<uint8_t> blocks(512);
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 32; ++x) blocks[(x / 16) * 256 + gs_block_offset8(x % 16, y)] = tex[y * 32 + x];
    }
    CHECK(gs_texture_hash_blocks(tex.data(), 32, 16, true) == xxh3_64(blocks.data(), blocks.size()));
    CHECK(gs_texture_hash_linear(tex.data(), 32, 16) == xxh3_64(tex.data(), tex.size()));
    // Menor que um bloco ou fora da grade de blocos: só o linear vale.
    CHECK(gs_texture_hash_blocks(tex.data(), 8, 16, true) == 0);
    CHECK(gs_texture_hash_blocks(tex.data(), 16, 16, false) == 0);  // PSMT4 precisa de 32 de largura
}

TEST(csm1_palette_order) {
    uint8_t in[1024], out[1024], back[1024];
    for (int i = 0; i < 256; ++i) std::memset(in + i * 4, i, 4);
    unswizzle_csm1(in, out);
    CHECK(out[8 * 4] == 16 && out[16 * 4] == 8 && out[23 * 4] == 15 && out[7 * 4] == 7 && out[24 * 4] == 24);
    CHECK(out[(32 + 9) * 4] == 32 + 17);
    unswizzle_csm1(out, back);  // a troca é a própria inversa
    CHECK(std::memcmp(in, back, sizeof(in)) == 0);
}

TEST(texture_pack_names_and_lookup) {
    uint64_t t = 0, c = 0;
    CHECK(TexturePack::parse_name(L"9b4e3c5ad73b4900-b5ce85bee872011d-00001dd3.dds", &t, &c));
    CHECK(t == 0x9b4e3c5ad73b4900ull && c == 0xb5ce85bee872011dull);
    CHECK(TexturePack::parse_name(L"1050a458e3cb7bd-c057fb0ff2caacf2-r0x64-00001994.dds", &t, &c));
    CHECK(t == 0x1050a458e3cb7bdull && c == 0xc057fb0ff2caacf2ull);
    CHECK(TexturePack::parse_name(L"1038f1a7bebf3fd2-4bb56b0eee0eb348-r80006000400020-00005554.dds", &t, &c));
    CHECK(c == 0x4bb56b0eee0eb348ull);
    CHECK(TexturePack::parse_name(L"00000000000000ff-00001153.dds", &t, &c) && t == 0xff && c == 0);
    CHECK(!TexturePack::parse_name(L"leia-me.txt", &t, &c));
    CHECK(!TexturePack::parse_name(L"xyz-00001153.dds", &t, &c));

    // Pasta com um arquivo nomeado pelos hashes de uma textura de 4 bits sintética.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "nf_texture_pack_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::vector<uint8_t> pixels(64 * 32 / 2), indices(64 * 32);
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = uint8_t((i * 5) & 0xF);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(indices[2 * i] | (indices[2 * i + 1] << 4));
    uint8_t palette[64];
    for (int i = 0; i < 64; ++i) palette[i] = uint8_t(i * 3);
    const uint64_t tex = gs_texture_hash_blocks(indices.data(), 64, 32, false);
    const uint64_t clut = gs_clut_hash(palette, 16);
    wchar_t name[96];
    swprintf(name, 96, L"%llx-%llx-00001596.dds", static_cast<unsigned long long>(tex),
             static_cast<unsigned long long>(clut));
    std::ofstream(dir / name).put('x');
    std::ofstream(dir / L"0123-00001596.png").put('x');  // PNG ainda não entra
    TexturePack pack;
    CHECK(pack.open(dir.wstring()) && pack.files() == 1);
    CHECK(pack.find(64, 32, false, pixels.data(), palette) == (dir / name).wstring());
    palette[0] ^= 1;  // outra paleta: sem substituta
    CHECK(pack.find(64, 32, false, pixels.data(), palette).empty());
    fs::remove_all(dir);
    CHECK(!pack.open((dir / "nao_existe").wstring()));
}
