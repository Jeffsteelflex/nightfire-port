#pragma once
// As texturas "shape" da EA para PS2 (.ssh, "SHPS"; docs/driving.md seção 5 e seção 10) decodificadas para RGBA.
//
// Leitor só do port: no PS2 o EAGL sobe a imagem como está para a VRAM (EAGL::DynamicLoader::RegisterShapes registra
// cada imagem como "shape_<nome>" e o TAR a usa; a gerência de VRAM, `EAGLInternal::TexAlloc`, não foi portada).
// O formato, conferido nas 3.209 imagens dos 23 arquivos do disco:
//  - "SHPS", u32 tamanho, u32 número de imagens, 4 letras de versão (G354, G352, G344, G310); diretório de
//    {4 letras de nome, u32 posição};
//  - cada imagem é uma cadeia de blocos {u8 tipo, u24 até o próximo (0 = último), u16 largura, u16 altura,
//    u16 centro x, u16 centro y, u16 x | bandeiras << 12, u16 y | mipmaps << 12} seguidos dos dados:
//    0x01 4 bits por pixel, 0x02 8 bits, 0x04 RGB 24, 0x05 RGBA 32; paletas 0x21 (32 bits; também 0x20, 32 imagens
//    no disco, lida igual), 0x22 (24 bits), 0x23 (16 bits 5551); 0x69 o "metal bin" do EAGL (dados da VRAM em tempo
//    de execução), 0x6f comentário, 0x70 o nome longo, 0x7c região de pixels;
//  - bandeira 0x2000 no campo x (bit 13): na imagem, os pixels estão no "swizzle" do GS (PSMT8 gravado como
//    PSMCT32, para subir mais rápido: ext.ssh `cref`, `wref`); na paleta, a ordem CSM1 (as entradas 8-15 e 16-23 de
//    cada grupo de 32 trocadas);
//  - o alfa fica na escala do GS (0x80 = opaco) na maioria dos arquivos; alguns (ext.ssh, versão G310) gravam até
//    0xff. O valor sai como está: quem desenha decide a escala (o GS lê 0x80 como 1,0).

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

struct ShpsImage {
    std::string name;       // as 4 letras do diretório ("$$00", "side")
    std::string long_name;  // bloco 0x70, se houver
    int width = 0, height = 0;
    int type = 0;            // tipo do bloco da imagem (0x01, 0x02, 0x04, 0x05)
    int palette_type = 0;    // 0 sem paleta
    int mipmaps = 0;         // os bits 12-15 do campo y (só o nível 0 é decodificado)
    bool swizzled = false;   // pixels no swizzle do GS (bandeira 0x2000 da imagem)
    bool csm1 = false;       // paleta na ordem CSM1 (bandeira 0x2000 da paleta)
    uint8_t gs_psm = 0;      // o PSM do GS que a imagem teria na VRAM (0x13 PSMT8, 0x14 PSMT4, 0 PSMCT32, 1 PSMCT24)
    std::vector<uint8_t> rgba;  // width * height * 4, alfa como está no arquivo
};

struct ShpsFile {
    std::string version;
    std::vector<ShpsImage> images;
    std::vector<std::string> warnings;  // formatos não decodificados
    const ShpsImage* find(const std::string& name) const;
};

// Decodifica o arquivo inteiro. false se não é SHPS ou se o diretório está fora do bloco.
bool shps_parse(const uint8_t* data, size_t size, ShpsFile& out);

// O índice do pixel (x, y) de uma imagem PSMT8 gravada no swizzle do GS (16x16 por bloco), em bytes.
int shps_psmt8_swizzled_index(int x, int y, int width);
// A entrada da paleta CSM1 para o índice i (troca os bits 3 e 4).
inline int shps_csm1_index(int i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); }

}  // namespace drv
