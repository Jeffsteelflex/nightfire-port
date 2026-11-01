#pragma once
// nf_driving_check, parte 29 (`--so objetos_cena`): o modelo próprio dos objetos de cena do mundo (o RSceneObj::UseArticle
// e o Render; docs/driving.md seção 34), o código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro)
// lado a lado com o porte (driving/scene_article.h).

#include <cstdint>

namespace nf {
class Elf;
class DiscSource;
}  // namespace nf

namespace drvcheck {

// `vu0`: o programa da VU0 do .vutext (o de número 1, como as outras partes). Devolve as diferenças.
int run_scene_article_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size,
                             int cases, uint32_t seed);

}  // namespace drvcheck
