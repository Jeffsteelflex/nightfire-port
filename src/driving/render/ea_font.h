#pragma once
// A biblioteca de fontes da EA (FONT_*, do librealfilez do DRIVING.ELF) e o driver de fontes do EAGL que ela usa nas
// missões de carro (docs/driving.md seção 28): o texto das legendas dos filmes (GSubtitles, a fonte
// data\loading\loading.sfn) e o do HUD (as galerias).
//
// Uma fonte é o arquivo "FNTS" inteiro na memória (FONT_create devolve o próprio arquivo): o cabeçalho tem os campos que
// o jogo escreve antes de desenhar (a cor +0x20, o Z +0x28, o deslocamento do UV +0x30/+0x34, a escala +0x38/+0x3c, a
// mistura +0x48, o teste de Z +0x4c, a textura do EAGL +0x7c); a tabela dos caracteres (+0x14: 12 bytes por caractere,
// ou 16 com o bit 0x40000 das bandeiras +0xc) e a de kerning (+0x18). FONT_drawtextx anda o texto: o caractere pela
// posição (código - 0x20) ou pela busca binária, a troca de caixa e o 0x7f quando não há o caractere, o kerning, o
// avanço, a quebra de linha (o ascendente + o descendente vezes a escala, truncado); cada caractere vai ao driver.
//
// O driver do EAGL (a tabela 0x00348be0: o caractere 0x002a1e50, o começo 0x002a20d0, o fim 0x002a25c8, a criação
// 0x002a31d0 e a destruição 0x002a3228) escreve um pacote do GIF com pares A+D direto para o GS: a textura da fonte
// (os 12 qwords do EAGL::Texture), o TEST_1, o ALPHA_1, o PRMODE, o FRAME_1, o ZBUF_1, o FBA_1, a cor (RGBAQ) e, por
// caractere, um SPRITE (UV e XYZ3, UV e XYZ2) em coordenadas do GS (x 16, mais o XYOFFSET). O port escreve as mesmas
// palavras de 64 bits (GsPacket) e o PC as interpreta (driving/render/gs2d.h); a parte 24 do nf_driving_check compara
// as palavras com as do original.
//
// Divergências (só do port): a textura da fonte não é um EAGL::Texture na VRAM: os 12 qwords dela são os do port
// (ea_font_texture_regs: o TEX0 leva o número da imagem do port no TBP0), e o FRAME_1, o ZBUF_1 e o FBA_1 do contexto
// do EAGL são 0 (o PC não os usa); o EAGLMalloc da criação não existe (a textura é um número).
// Um caractere abaixo de 0x20 cuja entrada direta cairia antes do começo da fonte conta como não encontrado (o PS2
// leria a memória antes dela).

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

// As palavras de 64 bits de um pacote do GIF para o GS (o DmaMgr::gpCurVifAcc do EAGL): o primeiro qword é a tag DMA
// que o fim do driver escreve.
struct GsPacket {
    std::vector<uint64_t> words;
};

// O estado do driver do EAGL (os globais 0x00348b90..0x00348bb4).
struct EaFontDriverState {
    GsPacket* packet = nullptr;  // 0x00348b94 o acumulador (o port: um vetor)
    int32_t tag_at = 0;          // 0x00348b98 o índice da tag do GIF das configurações
    int32_t tag2_at = 0;         // 0x00348ba0 o da tag dos caracteres
    int32_t end_at = 0;          // 0x00348b9c
    int32_t offset_x = 0;        // 0x00348ba4 o XYOFFSET x do contexto (x 16)
    int32_t offset_y = 0;        // 0x00348ba8
    int32_t uv_x = 0;            // 0x00348bac o deslocamento do UV (x 16)
    int32_t uv_y = 0;            // 0x00348bb0
    int32_t z = 0;               // 0x00348bb4
    uint64_t zbuf = 0;           // 0x0041a4e0 o ZBUF_1 de antes (o fim o põe de volta)
};
EaFontDriverState& ea_font_driver();
// O XYOFFSET do contexto corrente do EAGL (o RenderContext +0x75c/+0x760; 0 no PC: as coordenadas ficam as da tela).
void ea_font_set_offset(int32_t x16, int32_t y16);
// Os 12 qwords da textura de uma imagem do port (o lugar do EAGL::Texture do FUN_0027e388): TEX0_1 com o número da
// imagem no TBP0, o TEX1_1, o CLAMP_1, o TEXFLUSH...
void ea_font_texture_regs(uint32_t image, uint64_t out[12]);

// Uma fonte (o FNTS na memória: o port guarda os bytes e o número da imagem decodificada).
struct EaFont {
    std::vector<uint8_t> file;   // o FNTS (o jogo escreve no cabeçalho)
    uint32_t image = 0;          // o número da imagem do port (o +0x7c do PS2: a EAGL::Texture)
    uint8_t* ptr() { return file.data(); }
    const uint8_t* ptr() const { return file.data(); }
};

// [driving] 0x00257f18 FONT_create (com o driver do EAGL: 0x002a31d0 cria a textura; o port recebe o número)
bool FONT_create(EaFont& f, std::vector<uint8_t> file, uint32_t image);
// [driving] 0x00259140 FONT_getkern
int32_t FONT_getkern(const uint8_t* font, const uint8_t* glyph, uint32_t prev);
// [driving] 0x00258100 FONT_drawtextx<unsigned char> (pelo FONT_drawtexta 0x00258020 e o FONT_drawtextfa 0x00259040:
// o port recebe o texto já formatado)
void FONT_drawtexta(float x, float y, EaFont& f, const char* text, GsPacket& out);
// [driving] 0x00259040 FONT_drawtextfa(fonte, x, y, formato, ...): o vsprintf da newlib (0x002ae188) num buffer de 8 KB e
// o FONT_drawtexta. Quem desenha os textos do jogo (GGallery::FONT_Draw, GSubtitles::DrawFont) passa o texto como o
// formato e nenhum argumento: aqui o texto formatado assim. Um '%' seguido de uma letra que não é conversão sai como a
// letra ("50% HEALTH" vira "50HEALTH": o ' ' é um flag), "%%" como '%', e um '%' no fim acaba o texto; uma conversão de
// verdade (%d, %s...) leria um argumento que não há: o port não escreve nada e conta em `guards`.
std::string FONT_format_noargs(const char* fmt, int32_t* guards = nullptr);
void FONT_drawtextfa(float x, float y, EaFont& f, const char* fmt, GsPacket& out, int32_t* guards = nullptr);
// [driving] 0x00258880 FONT_getrectx<unsigned char> (o FONT_getrecta): o retângulo do texto (cada ponteiro pode ser nulo)
void FONT_getrecta(const uint8_t* font, const char* text, float* x, float* y, float* w, float* h);
// O caractere do texto (a posição, a busca binária 0x00259498, a troca de caixa 0x00257fb0, o 0x7f): nulo sem desenho.
const uint8_t* ea_font_find_glyph(const uint8_t* font, uint8_t c);

}  // namespace drv
