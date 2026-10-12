#pragma once
// A medida das miras das armas na mão (só do port, nf_loader_check --miras): para cada arma com pele carregada na fase,
// a pose do primeiro quadro da animação de espera no espaço do objeto da arma (Player_SetWeaponAnim, AnimScriptAdd,
// AnimObjectUpdate, psiBuildMatrixPalette) e os vértices da arma levados por essa paleta; daí a linha de mira (a alça e
// a massa, ou o alto do ferrolho ou do cano) por engine/player_ads.h. A saída são as linhas da tabela kAdsSights.

#include <cstdint>

struct celglist_tag;
namespace nf {
struct EntityMesh;
}

namespace sights {
// Liga a medida (guarda as malhas com skinning decodificadas na carga).
void enable();
bool enabled();
// Chamado pelo backend de gráficos do nf_loader_check com cada malha com skinning decodificada.
void note_mesh(const celglist_tag* glist, const nf::EntityMesh& mesh);
// Antes de cada fase: esquece as malhas da anterior.
void begin_level();
// Depois da carga: mede as armas da fase.
void measure_level(uint32_t pack);
// No fim: a tabela.
void print_table();
}  // namespace sights
