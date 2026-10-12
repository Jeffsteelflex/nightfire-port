#pragma once
// Pacotes de texturas de substituição no formato do PCSX2 (pasta "replacements" de um jogo).
//
// O PCSX2 dá a cada textura o nome "<hash da textura>-<hash da paleta>[-r<região>]-<bits>.dds",
// com hashes XXH3 de 64 bits do que está na memória do GS quando o jogo usa a textura:
// - textura: os índices de 1 byte por texel, linha a linha, quando ela é uma região de uma área
//   maior (o jogo empacota as texturas em "espaços" e usa CLAMP REGION_REPEAT) ou é menor que um
//   bloco; senão os blocos de 256 bytes como o GS os guarda (PSMT8 16x16, PSMT4 32x16, com o
//   embaralhamento de colunas), em ordem de linhas de blocos;
// - paleta: as 16 ou 256 cores de 32 bits em ordem lógica (a de 256 cores já sem a troca CSM1).
// A posição da textura no espaço só é conhecida em tempo de jogo, então os dois hashes da textura
// são tentados. Os texels e a paleta vêm do item de mapa como o jogo os carrega.
//
// As imagens do pacote guardam o alfa como o GS (0x80 = opaco), igual às texturas decodificadas
// por decode_texture.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nf {

// Posição (em bytes, ou em meio-bytes para PSMT4) do texel (x, y) dentro de um bloco do GS.
uint32_t gs_block_offset8(uint32_t x, uint32_t y);   // PSMT8: bloco 16x16, 256 bytes
uint32_t gs_block_offset4(uint32_t x, uint32_t y);   // PSMT4: bloco 32x16, 512 meio-bytes

// Hashes como o PCSX2 os calcula. "indices": um byte por texel (largura x altura).
uint64_t gs_texture_hash_linear(const uint8_t* indices, int width, int height);
// 0 quando a textura não cobre blocos inteiros (o PCSX2 usa o hash linear nesse caso).
uint64_t gs_texture_hash_blocks(const uint8_t* indices, int width, int height, bool bpp8);
// Paleta RGBA de 32 bits por cor, em ordem lógica.
uint64_t gs_clut_hash(const uint8_t* palette, int colors);
// Paleta de 256 cores como o jogo a sobe (CSM1) para a ordem lógica: troca as cores 8-15 com as
// 16-23 de cada grupo de 32.
void unswizzle_csm1(const uint8_t* in, uint8_t* out);

class TexturePack {
public:
    // Indexa os nomes da pasta; devolve false se ela não existir.
    bool open(const std::wstring& dir);
    size_t files() const { return files_; }
    bool empty() const { return files_ == 0; }

    // Arquivo que substitui a textura (largura x altura, 8 ou 4 bits por texel, pixels e paleta
    // como no item de mapa, primeiro quadro), ou vazio. Só aceita o par exato de textura e paleta.
    std::wstring find(int width, int height, bool bpp8, const uint8_t* pixels, const uint8_t* palette) const;

    // Interpreta um nome de arquivo do PCSX2; false se não for do formato.
    static bool parse_name(const std::wstring& name, uint64_t* texture, uint64_t* clut);

private:
    struct Entry {
        uint64_t clut;
        std::wstring file;
    };
    std::wstring dir_;
    std::unordered_map<uint64_t, std::vector<Entry>> by_texture_;
    size_t files_ = 0;
};

}  // namespace nf
