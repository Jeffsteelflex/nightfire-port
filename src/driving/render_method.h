#pragma once
// Os render methods do EAGL (eaglrm.o, 53, e bondrm.o, 62, no MISC.VIV; docs/driving.md seção 10): o recenseamento.
//
// Cada render method é um símbolo do objeto (o nome do método) que aponta uma estrutura: +0 o molde da lista de
// comandos (o que vira, compilado no modelo, a lista do GeoPrim), +0xc o máximo de vértices por lote, +0x10 a lista de
// parâmetros {nome, tamanho em palavras} terminada em zero ("Parameters", "MyMatrix", "WorldMatrix", "LightSources",
// "Texture", "Coordinates", "STQ", "Normals", "Colours", "weights", "PalletMatrices"...), +0x14 o nome. O microprograma
// é o símbolo "<nome>0__EAGLMicroCode": +0 o número de instruções, +0xc o pacote de partida, +0x10 a cadeia de DMA que o
// envia (CNT com o MPG e REF com o código, 256 instruções por vez).
// Daqui sai o tamanho do trabalho do render: quantos microprogramas distintos há, quais leem luzes, névoa, ossos, quais
// recortam, quantas passadas (XGKICK) cada um faz.

#include <cstdint>
#include <string>
#include <vector>

#include "driving/eagl_loader.h"

namespace drv {

struct RenderMethodInfo {
    std::string name;
    std::string object;          // "eaglrm" ou "bondrm"
    uint32_t method = 0;         // o endereço da estrutura
    uint32_t microcode = 0;      // o endereço do "<nome>0__EAGLMicroCode"
    int instructions = 0;
    int max_verts = 0;
    std::vector<std::string> params;
    std::vector<uint64_t> code;  // as instruções (inferior | superior << 32)
    uint64_t code_hash = 0;      // FNV-1a das instruções: métodos com o mesmo microprograma
    int xgkicks = 0;             // XGKICK no código (chutes de pacotes GIF: estados, texturas, vértices)
    bool clip = false;           // usa CLIP (recorte)
    bool efu = false;            // usa a EFU (ERLENG, ESIN, EATAN...)
    bool skin = false;           // tem PalletMatrices (pele com vários ossos)
    std::vector<int> abs_reads;  // endereços absolutos lidos (LQ/ILW em vi00, IADDIU de vi00): as constantes globais
    std::string family;          // a família (car_family)
};

// Os render methods de um objeto carregado.
std::vector<RenderMethodInfo> render_methods_census(const eagl::DynamicLoader& obj, const std::string& object_name);
// A família de um método pelo nome e pelos parâmetros (para dimensionar o trabalho; ver docs/driving.md seção 10).
std::string render_method_family(const RenderMethodInfo& m);

}  // namespace drv
