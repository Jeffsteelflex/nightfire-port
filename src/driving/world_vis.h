#pragma once
// A visibilidade do mundo do DRIVING.ELF (docs/driving.md seção 15): o que o RRenderWorldCamera faz antes de desenhar a
// pista. Portado do MIPS do PS2 (a verdade); o Xbox e o nightfire-cxbx (NightfireResearch, src/driving/world/Render.cpp,
// VisCurtain.cpp, render/WorldCulling.h) só como guia de leitura.
//
//  1. RPlayerViewCamera::ConfigureView monta o fgWorldCulling (RRenderWorldCulling, 0xa0 bytes em 0x334160): um
//     "frustum" em 2D no plano xz, com cinco planos: as duas bordas pelo ponto mais longe (o alcance, CullDistance =
//     400 m, x 1,1 com o campo de visão abaixo de 32 graus), os dois lados e o de perto pelo olho (recuado quando a
//     câmera olha para cima ou para baixo); os quatro primeiros transpostos (a coluna k é o plano k).
//  2. WRender::GenerateCurtainsAndNodes desce a árvore de cenas do 'Wmap' (CARP::MapNode, um quadrado por nó com os
//     quatro filhos de 'wn') contra esse frustum (QuadtreeFrustrumCheck2d: fora, dentro, cruzando) e guarda os nós
//     visíveis na lista do scratchpad (CachedDrawInfo em 0x70000000); as cortinas ('VisC', WVisCurtain: paredes
//     verticais) dos nós visíveis que caem no frustum viram cortinas ativas (AddCurtain, no microprograma VU0_culling:
//     driving/vu0_culling.h), salvo as que outra cortina já esconde.
//  3. RRenderWorldCamera::CullModule anda pelas instâncias dos nós visíveis (WRender::GetNextPoint: a esfera da
//     instância pelas medidas empacotadas, o "corte no longe" pelas bandeiras 8 e 0x40) e guarda (SetCull) as que
//     ficam no frustum (IsInFrustum2d) e que nenhuma cortina esconde (IsVisibleAgainstCurtains, VU0_culling 0x92).
//     Depois os objetos de cena (RSceneObj: os postes, as placas, o que o carro derruba; o WWorld::Open os cria das
//     instâncias com a bandeira 0x10 e os pendura no nó do WTree::FindNode): o mesmo CullModule com o
//     GetNextSceneObjCullInfo e o SetSceneObjectCull/SetNowVisible, para as listas de visíveis do RSceneObj.
//  4. DrawWorld desenha o céu (RSky) e as passadas 3 e 2 ordenadas pela instância; DoRender copia as passadas 0 e 1
//     (CopyDrawPasses), desenha os objetos de cena (RenderAllNormal), os carros (com os RenderAllDrawLast) e as passadas
//     1 e 0, do longe para o perto (o std::sort do GCC 2.95 de cada DrawPass).
//
// As estruturas têm o leiaute do PS2 (os ponteiros como palavras p32: nf::p32_ptr/p32_u), para o nf_driving_check
// (parte 11) comparar o porte com o código original no interpretador MIPS byte a byte. Os globais do EE (fgRender,
// CurrentList, o cursor, numActiveCurtains, fgWorldCulling, o mapa do WWorld) são globais aqui também.
//
// Divergências (todas só do port, documentadas nas funções):
//  - o atan2f do newlib (Setup2dFrustrum) é o nf::fm::atan2: o nf_driving_check o põe no lugar do original nos dois
//    lados, como o sinf/cosf da colisão;
//  - os nós da árvore não são contados (o PS2 não confere o limite de 0x300 da lista): aqui um nó além do limite não
//    entra na lista e conta em world_vis_guards() (memória inválida no PS2);
//  - o desenho (QuickDrawInstance) vira uma lista de instâncias na ordem do jogo (DrawPass_order), que o nf_driving
//    desenha; os objetos de cena (a bandeira 1) ficam de fora dela, como no jogo;
//  - do RSceneObj só o que o corte usa foi portado (o construtor, o nó, o corte): o nf_driving desenha a instância de
//    origem no lugar do modelo próprio do UseArticle; o RPathEngine::GetPathHandle dá 0; um objeto com física (não há
//    no port) leria a posição do corpo rígido;
//  - o PerformCulling não corta os atores (ActActorDatabase, nulo nas missões de carro do port) nem a galeria de efeitos
//    (GFXGallery).

#include <cstdint>
#include <vector>

namespace drv {

struct UData;
using UGroup = UData;

// ---------------------------------------------------------------------------------------------------------- o frustum

// RRenderWorldCulling (0xa0 bytes; fgWorldCulling em 0x334160).
struct RRenderWorldCulling {
    float planes[5][4];      // +0x00 (x, 0, z, distância); 0-3 transpostos (a coluna k é o plano k) no fim do Setup
    float eye[4];            // +0x50 a posição da câmera, recuada
    float edge_length;       // +0x60 o comprimento da borda (InitializePlaneInfo)
    float fov;               // +0x64 em graus (metade do campo horizontal)
    float range;             // +0x68 o alcance
    float cosine;            // +0x6c VU0_Cos(fov / 360)
    float centre[3];         // +0x70 o meio do alcance, girado pela direção, no olho
    float edge_z;            // +0x7c a direção da borda, z
    float centre_local[3];   // +0x80 (0, 0, alcance / 2)
    float sine;              // +0x8c
    float edge_x;            // +0x90 a direção da borda, x
    uint32_t unknown94[2];   // +0x94 (nunca escritos)
    float eye_y;             // +0x9c a altura da câmera
};
static_assert(sizeof(RRenderWorldCulling) == 0xa0, "RRenderWorldCulling");

RRenderWorldCulling& fgWorldCulling();
// RViewCamera::fCulling... O alcance do corte (CullDistance, 0x333ef8: 400 no .data; ESetCullDistanceFactor muda).
float& CullDistance();

// [driving] 0x001c6588 RRenderWorldCulling::RRenderWorldCulling(float, float)
void RRenderWorldCulling_Construct(RRenderWorldCulling& c, float fov, float range);
// [driving] 0x001c6b70 RRenderWorldCulling::InitializePlaneInfo(float, float)
void InitializePlaneInfo(RRenderWorldCulling& c, float fov, float range);
// [driving] 0x001c6858 RRenderWorldCulling::Setup2dFrustrum(COORD4 &, MATRIX4 &, float, float)
void Setup2dFrustrum(RRenderWorldCulling& c, const float position[4], const float matrix[16], float fov, float range);
// [driving] 0x001c65b0 RRenderWorldCulling::IsInFrustum2d(COORD4 &, float, bool, float, float *)
bool IsInFrustum2d(const RRenderWorldCulling& c, const float sphere[4], float radius, bool check_far, float far_scale,
                   float* distance);
// [driving] 0x001c66c8 RRenderWorldCulling::QuadtreeFrustrumCheck2d(COORD4 &, float): 0 fora, 1 dentro, 2 cruzando.
int QuadtreeFrustrumCheck2d(const RRenderWorldCulling& c, const float box[4], float radius);

// [driving] 0x001d44c8 RViewCamera::RefreshLODMultiplier: fov / 33 / 400 x (x1 - x0) (o +0x30 da vista; as bordas da
// janela de vista em +0x18 e +0x24).
float RefreshLODMultiplier(float fov, float x0, float x1);
// [driving] 0x001beba0 RPlayerViewCamera::ConfigureView: o fgWorldCulling da câmera (RCamera: a matriz em +0, o fov em
// +0xa4). `widescreen` é o +0x5c do fgRenderer (fov x 1,25).
void RPlayerViewCamera_ConfigureView(const float camera_matrix[16], float fov, bool widescreen);

// ---------------------------------------------------------------------------------------------------------- as cortinas

// WVisCurtain (0x20 bytes): o pé e o topo da parede; o w do começo é a metade do comprimento no plano xz.
struct WVisCurtain {
    float start[4];
    float end[4];
};
static_assert(sizeof(WVisCurtain) == 0x20, "WVisCurtain");

// [driving] 0x0022f660 WVisCurtain::WVisCurtain(COORD4 &, COORD4 &)
void WVisCurtain_Construct(WVisCurtain& c, const float from[4], const float to[4]);
// [driving] 0x0022f428 WVisCurtain::CalcRadius(void)
void CalcRadius(WVisCurtain& c);
// numActiveCurtains (0x33aa9c).
int32_t& numActiveCurtains();
// [driving] 0x0022f408 InitActiveCurtainList(void)
void InitActiveCurtainList();
// [driving] 0x0022f418 GetCurtainCount(void)
int GetCurtainCount();
// [driving] 0x0022f4a0 AddCurtain(WVisCurtain &, COORD4 &)
void AddCurtain(const WVisCurtain& curtain, const float eye[4]);
// [driving] 0x0022f5a8 IsVisibleAgainstCurtains(COORD4 &, float)
bool IsVisibleAgainstCurtains(const float sphere[4], float margin);
// [driving] 0x0022f5e8 CheckPositionAgainstActiveCurtains(COORD4 &)
bool CheckPositionAgainstActiveCurtains(const float position[4]);

// ---------------------------------------------------------------------------------------------------------- a árvore

// CARP::MapNode (0x10 bytes; os 'wn' são grupos de quatro filhos).
struct MapNode {
    uint32_t children;        // +0x00 (p32) os quatro filhos, ou 0
    uint32_t scene_objs;      // +0x04 (p32) os RSceneObj do nó (SetMapNode); zerado pelo WTree::InitializeTree
    uint16_t list_offset;     // +0x08 a lista de índices (u16), em bytes a partir do nó: as instâncias e as cortinas
    uint16_t instance_count;  // +0x0a
    uint8_t curtain_count;    // +0x0c
    uint8_t depth, x, z;      // +0x0d (WTree::InitializeTree)
};
static_assert(sizeof(MapNode) == 0x10, "MapNode");
inline const uint16_t* MapNode_IndexList(const MapNode* n) {
    return reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(n) + n->list_offset);
}

// [driving] 0x0022c730 WTree::InitializeTree(CARP::MapNode &, int, unsigned char, unsigned char): a profundidade e a
// posição de cada nó (o WWorld::Open a chama na raiz do 'Wmap'); zera o +4.
void WTree_InitializeTree(MapNode* node, int depth, uint8_t x, uint8_t z);

constexpr int kMaxVisibleNodes = 0x300;
constexpr int kMaxDraws = 0x400;

struct InstanceAndDistance {
    uint32_t instance;  // (p32) o CARP::Instance
    float distance;
};
// CachedDrawInfo (0x2c08 bytes; a lista do quadro fica no scratchpad, 0x70000000).
struct CachedDrawInfo {
    int32_t node_count;                     // +0x000
    int32_t draw_count;                     // +0x004
    uint32_t nodes[kMaxVisibleNodes];       // +0x008 (p32) os nós visíveis
    InstanceAndDistance draws[kMaxDraws];   // +0xc08
};
static_assert(sizeof(CachedDrawInfo) == 0x2c08, "CachedDrawInfo");

// WRender (0xe0 bytes; fgRender em 0x33a5d4).
struct WRender {
    uint32_t instances;       // +0x00 (p32) as instâncias de render da pista ('in  ')
    uint32_t pass_first[5];   // +0x04 (p32) a primeira instância de cada passada
    int32_t pass_count[5];    // +0x18
    uint32_t proc_anims;      // +0x2c (p32) 'ps  ', ou 0
    uint32_t curtain_count;   // +0x30 'VisC'
    uint32_t curtains;        // +0x34 (p32) as WVisCurtain, ou 0
    uint8_t unknown38[0x9c];
    uint32_t camera;          // +0xd4 (p32) a do GenerateCurtainsAndNodes
    float unknown_d8;         // +0xd8 o float do GenerateCurtainsAndNodes (o RRenderWorldCamera passa 120)
    uint32_t unknown_dc;
};
static_assert(sizeof(WRender) == 0xe0, "WRender");

// O cursor do GetNextPoint (0x33a5dc o nó, 0x33a5e0 a instância, 0x33a5e4 a lista).
struct WCullCursor {
    int32_t node = 0;
    int32_t item = -1;
    CachedDrawInfo* list = nullptr;
};

WRender*& fgRender();                 // 0x33a5d4
uint32_t& fgWorldMap();               // WWorld::fgWorld + 0x1c (p32): os dados do 'Wmap' (a raiz em +0, o canto em +0x1c, o lado em +0x2c)
CachedDrawInfo& scratchpad_list();    // 0x70000000
CachedDrawInfo*& CurrentList();       // 0x33a5e8
WCullCursor& Cursor();
// Os nós que não couberam na lista (no PS2 a escrita passaria do fim do scratchpad).
uint64_t& world_vis_guards();

// [driving] 0x0021eae8 WRender::WRender(void): as passadas ('in  ', do fim para o começo), o 'ps  ' e as cortinas
// ('VisC': `curtains` recebe as WVisCurtain feitas pelo construtor, no lugar do operator new do PS2). O 'Shad' (as
// sombras do RInstanceRender) fica de fora.
void WRender_Construct(WRender& r, UGroup* map_group, std::vector<WVisCurtain>& curtains);
// [driving] 0x0021ef68 WRender::FindVisibleTreeNodesAndCurtains(CARP::MapNode &, COORD4 &, bool, int)
void FindVisibleTreeNodesAndCurtains(WRender& r, const MapNode* node, const float box[4], bool clip, int depth);
// [driving] 0x0021f1d8 WRender::GenerateCurtainsAndNodes(RCamera &, float): `camera` é o RCamera (a matriz em +0).
CachedDrawInfo* GenerateCurtainsAndNodes(WRender& r, const float* camera, float unknown);
// [driving] 0x0021f288 WRender::PrepareForCull(CachedDrawInfo &) (no SYM, GetNextPoint)
void PrepareForCull(CachedDrawInfo* list);
// [driving] 0x0021f2b8 WRender::GetNextPoint(COORD4 &, float &, bool &, float &): a instância (p32) ou -1.
int32_t GetNextPoint(float sphere[4], float* height, int32_t* check_far, float* far_scale);
// [driving] 0x0021f570 WRender::SetCull(int, bool, float)
void SetCull(int32_t item, int32_t culled, float distance);
// [driving] 0x0021f5b8 WRender::CopyDrawPasses(CachedDrawInfo &, CachedDrawInfo &, unsigned int, unsigned int)
void CopyDrawPasses(const WRender& r, const CachedDrawInfo& from, CachedDrawInfo& to, int first_pass, int last_pass);

using CullNextFn = int32_t (*)(float sphere[4], float* height, int32_t* check_far, float* far_scale);
using CullSetFn = void (*)(int32_t item, int32_t culled, float distance);
// [driving] 0x001c54a0 RRenderWorldCamera::CullModule(int (*)(...), void (*)(...), bool)
void RRenderWorldCamera_CullModule(CullNextFn next, CullSetFn set);
// [driving] 0x001c5780 RRenderWorldCamera::PerformCulling(void): o mundo e os objetos de cena (os atores e a galeria de
// efeitos não foram portados: o ActActorDatabase é nulo nas missões de carro do port e o GFXGallery não existe).
CachedDrawInfo* RRenderWorldCamera_PerformCulling(WRender& r, const float* camera);

// ------------------------------------------------------------------------------------------------ os objetos de cena

// O RSceneObj (0x40 bytes no WWorld::Open): só os campos que o construtor (0x001c7098) escreve e que o corte lê. O resto
// do RSceneObj (o modelo próprio do UseArticle, a física, os efeitos, o desenho do Render) não foi portado: o port
// desenha a instância de origem no lugar dele.
struct RSceneObj {
    uint32_t next;        // +0x00 (p32) o próximo na lista do nó (MapNode +4)
    uint32_t prev;        // +0x04 (p32)
    uint32_t instance;    // +0x08 (p32) o registro do 'in  '
    uint32_t unknown0c;   // +0x0c
    uint32_t carp;        // +0x10 o RCARPFile (Load)
    uint32_t map_node;    // +0x14 (p32) o nó (SetMapNode)
    uint32_t flags;       // +0x18 bit 24 pode ficar visível, 25 o longe x 0,3, 26 sem o corte no longe, 27 desenha por
                          //       último; o byte +0x1a = 100
    uint16_t unknown1c, unknown1e;
    uint32_t unknown20;
    uint32_t physics;     // +0x24 (p32) o PhysicsObject (SetPhysics), ou 0
    uint32_t article;     // +0x28 (UseArticle)
    uint32_t path;        // +0x2c RPathEngine::GetPathHandle
    uint32_t unknown30[3];
    uint32_t vtable;      // +0x3c
};
static_assert(sizeof(RSceneObj) == 0x40, "RSceneObj");

// [driving] 0x0022c1a8 WTree::FindNode(CARP::MapNode &, COORD4 &, COORD4 &): o nó mais fundo cujo quadrado (`box`: o
// centro em x e z, a metade do lado no w) contém o círculo da esfera inteiro.
MapNode* WTree_FindNode(MapNode* node, const float box[4], const float sphere[4]);
// [driving] 0x001c8cf8 RSceneObj::SetMapNode(CARP::MapNode *)
void RSceneObj_SetMapNode(RSceneObj& o, MapNode* node);
// [driving] 0x001c7098 RSceneObj::RSceneObj(CARP::Instance *): os campos, a esfera da instância (o raio das medidas
// empacotadas, 10 se zero) e o nó (WTree::FindNode a partir do quadrado do 'Wmap', fgWorldMap()). O byte +0x1b vem do
// FastAlloc no PS2 (os bits 29-31 de `flags` ficam como estavam): aqui 0; o RPathEngine não foi portado (`path` 0).
void RSceneObj_Construct(RSceneObj& o, uint32_t instance);
// A parte do WWorld::Open (0x0022faf8) que cria os objetos de cena: cada instância com a bandeira 0x10 cujo registro do
// 'ps  ' (`r.proc_anims`, 0x20 bytes pelo u16 da instância em +0xe) tem o byte +7 diferente de 0xff ganha a bandeira 1
// (o DrawPass não a desenha mais) e, com artigo (+0x1c), um RSceneObj, guardado no registro (+8). `objs` recebe os
// objetos (reservado antes: os ponteiros p32 apontam para ele). O UseArticle, o SetTransform, o EnableTarget e os
// ProcAnimInstanceAndPointer não foram portados; as instâncias sem a bandeira 0x10 (os efeitos do GFX::Trigger) também
// não. Devolve quantos objetos criou; sem o 'ps  ' (memória inválida no PS2) conta em world_vis_guards().
int WWorld_CreateSceneObjects(WRender& r, std::vector<RSceneObj>& objs);

// Os globais do RSceneObj para o corte: fgCachedDrawInfo (0x333f34, o nó), kIdentityInstance (0x333f30, a lista),
// fgCurrentlyLoadingObj (0x333f3c, o objeto), fgNumVisibleObjs (0x3c8320), fgNumVisibleDrawLastObjs (0x3c8324),
// fgMaxVisibleObjects (0x3c8328) e as duas listas de visíveis (0x424840 e 0x425040, 0x200 cada).
struct SceneObjCullState {
    int32_t node = -1;
    CachedDrawInfo* list = nullptr;
    RSceneObj* current = nullptr;
    int32_t num_visible = 0, num_draw_last = 0, max_visible = 0;
    uint32_t visible[0x200] = {}, draw_last[0x200] = {};  // (p32)
};
SceneObjCullState& scene_obj_cull();
// Port: a posição do corpo rígido de um objeto de cena com física (o RSceneObj +0x24), que o GetNextSceneObjCullInfo
// usa no lugar da da instância; driving/game/smackable.h a instala (o PhysicsObject do port não é visível daqui). Falso:
// sem corpo (conta como guarda).
using SceneObjBodyPos = bool (*)(uint32_t physics, float pos[3]);
SceneObjBodyPos& scene_obj_body_pos();
// [driving] 0x001c80f0 RSceneObj::PrepareSceneObjsForCulling(CachedDrawInfo &)
void PrepareSceneObjsForCulling(CachedDrawInfo* list);
// [driving] 0x001c8130 RSceneObj::GetNextSceneObjCullInfo(COORD4 &, float &, bool &, float &): o objeto (p32) ou -1.
// Um objeto com física leria a posição do corpo rígido (Simulation::GetRigidBody/GetSimpleRigidBody, não portados): aqui
// a da instância, contado em world_vis_guards() (os objetos do port não têm física).
int32_t GetNextSceneObjCullInfo(float sphere[4], float* height, int32_t* check_far, float* far_scale);
// [driving] 0x001c8490 RSceneObj::SetSceneObjectCull(int, bool, float)
void SetSceneObjectCull(int32_t item, int32_t culled, float distance);
// [driving] 0x001c8c70 RSceneObj::SetNowVisible(void). A lista dos desenhados por último não confere o próprio limite no
// PS2: além de 0x200 a escrita seria memória inválida; aqui não entra e conta em world_vis_guards().
void RSceneObj_SetNowVisible(RSceneObj& o);

// ---------------------------------------------------------------------------------------------------------- o desenho

// O std::sort do GCC 2.95 que o DrawPass compila (__introsort_loop 0x002207c0, __final_insertion_sort 0x00220b10 e os
// ajudantes do SGI STL) com os dois comparadores: 1 InstanceAndDistance::lessThanByInstance (0x00220bc0, a instância sem
// sinal) e 2 greaterThanByDistance (0x00220bd0: b.distância < a.distância, o do longe primeiro).
void introsort_loop(InstanceAndDistance* first, InstanceAndDistance* last, int depth_limit, int sort);
void final_insertion_sort(InstanceAndDistance* first, InstanceAndDistance* last, int sort);
// O começo do DrawPass: a ordenação de toda a lista (sort 1 ou 2; outro valor: nada).
void DrawPass_sort(CachedDrawInfo& list, int sort);
// [driving] 0x0021f670 WRender::DrawPass(CachedDrawInfo &, unsigned int, unsigned int, SortBy): a ordenação e as
// instâncias de cada passada de `last` a `first`, na ordem da lista, que o QuickDrawInstance (0x001ad618) desenharia: as
// sem a bandeira 1 (objeto de cena) e com artigo. Em `out` as entradas da lista (o p32 da instância e a distância).
void DrawPass_order(const WRender& r, CachedDrawInfo& list, int first_pass, int last_pass, int sort,
                    std::vector<InstanceAndDistance>& out);
// [driving] 0x001ece18 RSky::Draw(CARP::Instance *, bool): as instâncias do começo com a bandeira 4 (o céu), na ordem
// (a distância 0).
void RSky_order(const WRender& r, std::vector<InstanceAndDistance>& out);

}  // namespace drv
