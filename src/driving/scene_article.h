#pragma once
// O modelo próprio dos objetos de cena do mundo (RSceneObj.obj do DRIVING.ELF; docs/driving.md seção 34): o
// RSceneObj::UseArticle que o WWorld::Open chama para cada objeto de cena da pista e o desenho dele (RSceneObj::Render).
//
// Como o PS2 faz: o WWorld::Open (0x0022faf8) cria um RSceneObj para cada instância da pista com a bandeira 0x10 (os
// postes, as placas, a rampa do guindaste da obra de Paris...) e chama o UseArticle(grupo do modelo do 'pD' do artigo
// dela, o byte +7 do registro do 'ps  ', a variante). O UseArticle procura no grupo a lista de instâncias da variante
// ('in' + a variante, ou a 0): com ela o objeto é feito dessas instâncias (a rampa do guindaste tem 16: a rampa, o
// gancho e os elos das duas correntes), com os estados ('ps' + a variante); sem ela, de uma só, a própria instância do
// mundo com a matriz identidade (e o primeiro 'as  ' do grupo se ela não tem artigo). O UseInstanceList cria o
// RAnimEngine::Handle dessas instâncias (+0x28; driving/render/anim_engine.h), grava o 'pD' (+0xc) e o nome (+0x38),
// liga os efeitos que nascem ligados (InitEffects) e, com a bandeira 8 da instância, põe o objeto no nó da raiz da árvore.
// O Render (0x001c85f8) desenha as cópias das instâncias do Handle (DrawInstanceList 0x001ad6b8, DrawInstance 0x001ad498:
// a escondida, bit 0 do +0xc, não; a matriz da cópia pelo gProcAnimFunc dela, GetInstanceMatrix, vezes a do objeto, o
// GetTransform; o artigo da passada 2 agora, os outros na fila da passada deles) e os efeitos (RenderEffects). Assim um
// estímulo no Handle (o EPlaySystemAnim de um gatilho, o tiro no Shell::ApplyWorldDamage) toca a animação das cópias: a
// rampa do guindaste desce quando o tiro acerta o gancho (o gatilho 711 de Paris).
//
// Portado do MIPS do PS2 (a verdade); conferido na parte 29 do nf_driving_check (tests/driving_check_scene_article.cpp).
//
// Divergências (cada uma também na função):
//  - o Close (0x001c8070) de um objeto novo: o KillEffects limpa as telas da galeria do objeto (GFXGallery::
//    CANVAS_PurgeSceneObjEffects, pelo scene_article_env().purge; nulo: nada, um objeto novo não tem telas) e o Handle
//    antigo (nenhum); o ramo do Close(true) (o URefCounter do RCARPFile) não é alcançado pelo WWorld::Open: contado;
//  - o ComputeBoundingRadius com o SetDimensions das instâncias sem medidas (nenhuma em Paris): contado e não gravado;
//  - a entrada +0x94 da tabela virtual (vazia no RSceneObj, 0x001c7f40) e o SetProcAnimState do Render (+0x5c, vazio,
//    0x001c9308) não fazem nada; no PS2 as cópias com o gProcAnimFunc 0xfe/0xef leriam então os ossos e os parâmetros do
//    último RSkeletalObj desenhado: aqui os zerados, contados (nenhuma em Paris);
//  - o desenho: o port não tem a fila das passadas do DrawInstance (0x001ab050): a passada vai com cada desenho, e quem
//    desenha (o nf_driving) a põe na lista da passada dela.

#include <cstdint>
#include <vector>

#include "driving/carp_runtime.h"

namespace drv {

struct RSceneObj;

// O que vem de fora (o nf_driving liga; a parte 29 grava).
struct SceneArticleEnv {
    // GFXGallery::CANVAS_PurgeSceneObjEffects(objeto) (0x00235490 pelo 0x00232dc8; o KillEffects do Close); nulo: nada.
    void (*purge)(uint32_t scene) = nullptr;
    // RSceneObj::ToggleEffect(-1, liga, índice, efeito) do InitEffects; nulo: o do port (driving/fx/scene_fx.h).
    void (*toggle_effect)(uint32_t scene, uint32_t index, uint32_t effect) = nullptr;
    // RSceneObj::SetMapNode(raiz) (a bandeira 8 da instância); nulo: o do port (driving/world_vis.h) com o nó da raiz do
    // 'Wmap' (fgWorldMap()).
    void (*set_root_node)(RSceneObj& o) = nullptr;
};
SceneArticleEnv& scene_article_env();

struct SceneArticleStats {
    uint64_t articles = 0, lists = 0, single = 0, without = 0, handles = 0, instances = 0, effects_on = 0;
    uint64_t root_nodes = 0, draw_last = 0, no_radius = 0, close_true = 0, draws = 0, hidden = 0, proc_other = 0;
    uint64_t guards = 0;
};
SceneArticleStats& scene_article_stats();

// [driving] 0x001c6fb8 (UGroup: o dado com a etiqueta e a variante, o índice da família curta; sem ele, o da variante 0,
// se a variante não era 0; 0xffffffff: a etiqueta inteira): o dado, ou nulo.
UData* RSceneObj_LocateVariant(const UGroup* g, uint32_t tag, uint32_t variant);
// [driving] 0x001c7658 RSceneObj::UseArticle(UGroup *, unsigned int): verdadeiro se o objeto ganhou instâncias.
bool RSceneObj_UseArticle(RSceneObj& o, const UGroup* group, uint32_t variant);
// [driving] 0x001c7940 RSceneObj::UseInstanceList(CARP::Instance *, unsigned int, CARP::ProcAnimState *, bool, char *)
void RSceneObj_UseInstanceList(RSceneObj& o, uint32_t instances, uint32_t n, uint32_t states, bool flag, uint32_t name);
// [driving] 0x001ca048 RSceneObj::InitEffects(bool)
void RSceneObj_InitEffects(RSceneObj& o, bool flag);
// [driving] 0x001c8070 RSceneObj::Close(bool) (o KillEffects 0x001ca178 e o ~Handle)
void RSceneObj_Close(RSceneObj& o, bool flag);

// Um desenho do Render de um objeto de cena (0x001c85f8, DrawInstance 0x001ad498): a cópia da instância no Handle (p32),
// o artigo dela (p32, o +0x1c), a passada do artigo (+0x1d) e a matriz no mundo.
struct SceneObjDraw {
    uint32_t copy = 0;
    uint32_t article = 0;
    int pass = 0;
    float world[4][4] = {};
};
// As cópias desenhadas de um objeto com Handle (nenhuma sem ele: quem desenha usa a instância de origem).
void RSceneObj_Render(const RSceneObj& o, std::vector<SceneObjDraw>& out);

}  // namespace drv
