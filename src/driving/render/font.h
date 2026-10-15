#pragma once
// As fontes "FNTS" da EA (.sfn; docs/driving.md seção 19): data\render\debug.sfn do MISC.VIV, a fonte de depuração do
// EAGL, que o port usa para desenhar o HUD mínimo da missão (o GHud do PS2 desenha com as fontes das galerias,
// hudNTSC.gal, que o port não lê).
//
// Leitor só do port. O formato, lido no debug.sfn (14.582 bytes):
//  - "FNTS", u32 tamanho, u16 +0x8 (0x130), u16 +0xa o número de caracteres, u32 +0x14 a posição da tabela dos
//    caracteres, u32 +0x1c a posição da imagem, u32 +0x24 a altura da linha; floats +0x40 e +0x44 = 1 / largura e
//    1 / altura da imagem;
//  - a tabela: 12 bytes por caractere {u16 código, u8 largura, u8 altura, u16 x, u16 y, s8 avanço, s8 deslocamento x,
//    s8 deslocamento y (do alto da linha), u8 0};
//  - a imagem: uma cadeia de blocos de imagem do SHPS (driving/shps.h: 8 bits por pixel com a paleta CSM1 de 32 bits),
//    lida com shps_parse num cabeçalho SHPS montado em volta dela.

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

struct FontGlyph {
    uint16_t code = 0;
    int width = 0, height = 0;
    int x = 0, y = 0;   // na imagem
    int advance = 0;
    int offset_x = 0, offset_y = 0;
};

struct FontFile {
    int line_height = 0;
    std::vector<FontGlyph> glyphs;
    int image_width = 0, image_height = 0;
    std::vector<uint8_t> rgba;  // a imagem decodificada (alfa como está no arquivo)
    const FontGlyph* find(uint16_t code) const;
    // A largura do texto (a soma dos avanços; um caractere sem desenho usa o espaço).
    int width(const std::string& text) const;
};

// Decodifica o arquivo. false se não é FNTS ou se a tabela ou a imagem saem do arquivo.
bool font_parse(const uint8_t* data, size_t size, FontFile& out);

// Um caractere posto na tela: o retângulo (em pixels da fonte a partir de (0, 0), o alto da linha) e o da imagem.
struct FontQuad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
};
// Os caracteres do texto a partir de (x, y) (o alto da linha), na escala dada.
std::vector<FontQuad> font_layout(const FontFile& f, const std::string& text, float x, float y, float scale);

}  // namespace drv
