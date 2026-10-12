#pragma once
// Imagens 2D (texturas 03xxxxxx) de um pacote de fase lidas sem carregar a fase (só do port): a roda de armas mostra as
// imagens do dossiê, que só existem no pacote do menu principal (07000048), numa fase qualquer.
//
// O hash de cada textura de um item de mapa está no cabeçalho das paletas (bloco 0x0F: um registro de 8 bytes por
// textura, {info, hash}; parsemap_block_palette_data_pc o registra na hashtable), na mesma ordem dos cabeçalhos das
// texturas (bloco 0x10) e dos blocos de pixels e de paleta (docs/formats.md, "Texturas"). Só as texturas pedidas são
// decodificadas (decode_texture: o primeiro quadro, RGBA com o alfa do GS).

#include <cstdint>
#include <map>
#include <vector>

#include "core/binary.h"
#include "data/map_item.h"

namespace nf {

// As texturas de `want` achadas nos itens de mapa do pacote (a primeira de cada hash). Itens que não se leem ficam de
// fora (sem exceção).
std::map<uint32_t, Texture> decode_pack_images(ByteView pak, const std::vector<uint32_t>& want);

}  // namespace nf
