#pragma once
// O chão da pista: o sistema de colisão do mundo do DRIVING.ELF (WCollisionMgr, WGrid, WWorldPos; os registros do
// grupo 'CDat' do CARP), portado do MIPS e conferido contra o original no nf_driving_check (docs/driving.md seção 13).
// Leiautes do PS2 byte a byte (static_assert): o nf_driving_check monta as mesmas estruturas na RAM emulada.
//
// O que é cada coisa (lido no PS2; o nightfire-cxbx, NightfireResearch, src/driving/world/CollisionTypes.h e Grid.h,
// deu os nomes dos campos):
//  - WGrid ('CGrd'): uma grade de células quadradas no plano xz (linhas ao longo de z, colunas ao longo de x); cada
//    célula ocupada tem um nó ('cn', a contagem do registro é o número da célula) com as listas estáticas por tipo
//    (0 instância de colisão, 1 gatilho, 2 objeto, 3 trecho de estrada) e a lista dinâmica (o que se move; nada em
//    Paris);
//  - WCollisionInstance ('ci', 0x40 bytes): uma peça de geometria de colisão posta no mundo: os eixos direito e
//    frente com as meias-medidas da caixa nos w, a posição do mundo no referencial dela (a linha 3 de MakeMatrix), o
//    raio, e o artigo ('ca': faixas de triângulos com uma esfera cada, barreiras, janelas);
//  - WWorldPos (0x40 bytes): a face sob um ponto, guardada para a próxima consulta.
// O gerente (WCollisionMgr) é um objeto só (fgCollisionMgr, 0x33a540); a grade também (fgGrid, 0x33a5c8).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/physics/world.h"
#include "platform/ptr32.h"

namespace drv {

// WCollisionInstance ('ci', 0x40 bytes).
struct WCollisionInstance {
    float right[3];          // +0x00
    float half_x;            // +0x0c
    uint32_t query_stamp;    // +0x10 a última consulta que a pegou (WCollisionMgr +0x04)
    float half_height;       // +0x14
    uint8_t flags;           // +0x18 1: faces acima do ponto; 3: inclinada (o "para cima" é frente x direito)
    uint8_t unknown19;
    uint16_t render_index;   // +0x1a
    uint32_t article;        // +0x1c antes do Init: o grupo do artigo (p32); depois: os dados do 'ca' (p32)
    float forward[3];        // +0x20
    float half_z;            // +0x2c
    float position[3];       // +0x30 a origem do mundo no referencial dela
    float radius;            // +0x3c
};
static_assert(sizeof(WCollisionInstance) == 0x40);

// O cabeçalho de um artigo de colisão ('ca', 0x20 bytes): as faixas (as esferas a partir de +0x20), as barreiras e
// as janelas.
struct CollisionArticle {
    uint16_t strip_count;     // +0x00
    uint16_t barrier_offset;  // +0x02 a partir de +0x20
    uint16_t barrier_count;   // +0x04
    uint16_t window_offset;   // +0x06
    uint16_t window_groups;   // +0x08
    uint8_t unknown0a[0x16];
};
static_assert(sizeof(CollisionArticle) == 0x20);

// A esfera de uma faixa (16 bytes): o centro (no referencial da instância), o raio em 1/16 e onde ficam os vértices
// (a partir de +0x20 do artigo).
struct CollisionStrip {
    float centre[3];
    uint16_t radius;
    uint16_t vertex_offset;
};
static_assert(sizeof(CollisionStrip) == 16);

// Um vértice de faixa (16 bytes): o w é a contagem no primeiro, as bandeiras no segundo (1 começa no sentido
// horário, 2 qualquer sentido) e, nos outros, a etiqueta da face que ele fecha (tipo, subtipo, id u16).
struct StripVertex {
    float x, y, z;
    uint32_t w;
};
static_assert(sizeof(StripVertex) == 16);

// O nó de uma célula ('cn'): a lista dinâmica, e as estáticas por tipo (as contagens e onde começam, em bytes
// depois dos 16 do cabeçalho; índices u16).
struct WGridNode {
    uint32_t dynamic;         // +0x00 (p32) nulo até algo se mover para a célula
    uint8_t static_count[4];  // +0x04
    uint16_t static_offset[4];  // +0x08
};
static_assert(sizeof(WGridNode) == 16);

// WGrid (0x24 bytes).
struct WGrid {
    float origin[4];          // +0x00 o canto de x e z menores
    float cell_size;          // +0x10
    float inv_cell;           // +0x14
    uint32_t rows;            // +0x18 ao longo de z
    uint32_t columns;         // +0x1c ao longo de x
    uint32_t nodes;           // +0x20 (p32) o vetor de ponteiros (p32) dos nós, por célula
};
static_assert(sizeof(WGrid) == 0x24);

// WCollisionMgr (0x34 bytes; só os campos que o chão usa têm nome).
struct WCollisionMgr {
    uint32_t unknown00;
    uint32_t query_stamp;     // +0x04 conta as consultas de GetInstanceListGuts
    uint32_t instances;       // +0x08 (p32) os 'ci'
    uint32_t instance_count;  // +0x0c
    uint32_t objects;         // +0x10 (p32) os 'co'
    uint32_t unknown14[6];    // +0x14 os mapas de janelas quebradas e o que mais o construtor monta
    uint32_t barrier_mask;    // +0x2c as faces cujo subtipo tem um bit em comum não contam (0xf0; 0x20/0x10 no chão)
    uint32_t vu0_path;        // +0x30 o caminho VU0 das diferenças (0 no jogo: nada o liga)
};
static_assert(sizeof(WCollisionMgr) == 0x34);

// A lista de faixas de uma instância perto de um ponto (WCollisionMgr::GetInstanceStripList: no PS2 um vetor criado com
// __builtin_new(0x10) que o WCollider apaga no Clear; aqui compartilhado entre as cópias da lista).
using StripList = std::vector<const CollisionStrip*>;

// O par (instância, faixas) de uma lista de instâncias (WCollisionInstanceCache, 8 bytes no PS2); as faixas nulas =
// todas as do artigo.
struct InstanceListEntry {
    WCollisionInstance* instance;
    std::shared_ptr<const StripList> strips;
};

// Uma barreira de um artigo de colisão (0x20 bytes, a partir de +0x20 + barrier_offset do artigo): uma parede vertical
// de (x0, z0) a (x1, z1), da altura y0 a y1, no referencial da instância. Os w: a etiqueta (o tipo da face e o
// subtipo, que a máscara de barreiras do gerente filtra) e o inverso do comprimento em xz (escala (dz, -dx) para a
// normal).
struct CollisionBarrier {
    float x0, y0, z0;
    uint8_t type;      // +0x0c o tipo da face (0x0e: um evento de missão)
    uint8_t subtype;   // +0x0d
    uint16_t id;       // +0x0e
    float x1, y1, z1;
    float inv_length;  // +0x1c
};
static_assert(sizeof(CollisionBarrier) == 0x20);

// Uma barreira perto de uma consulta (0x28 bytes): as duas pontas no mundo (com os w da barreira), a instância e o
// índice da barreira no artigo. Numa instância inclinada (flags & 3) as pontas também vão para o mundo, mas a consulta
// usa a barreira do artigo no referencial dela.
struct BarrierListEntry {
    float p0[4];        // +0x00 (w: a etiqueta)
    float p1[4];        // +0x10 (w: o inverso do comprimento)
    uint32_t instance;  // +0x20 (p32 WCollisionInstance)
    uint32_t index;     // +0x24
};
static_assert(sizeof(BarrierListEntry) == 0x28);

// WCollisionObject ('co', 0x30 bytes): um cilindro em pé ou uma caixa, posto por uma instância de render do mundo.
struct WCollisionObject {
    float position[3];      // +0x00 o pé do cilindro (o centro da caixa)
    float radius;           // +0x0c o raio (do cilindro; o da esfera da caixa)
    float half[4];          // +0x10 as meias-medidas da caixa; o cilindro tem 2 x half[1] de altura
    uint8_t is_cylinder;    // +0x20
    uint8_t unknown21;
    uint16_t flags;         // +0x22 1: anda com a instância de render (a velocidade dela vai ao choque)
    uint16_t render_index;  // +0x24 a instância de render (fgWorld +0x20, 64 bytes cada)
    uint8_t face_type;      // +0x26
    uint8_t face_subtype;   // +0x27
    uint8_t unknown28[8];
};
static_assert(sizeof(WCollisionObject) == 0x30);

// WCollisionMgr::WorldCollisionInfo (0x60 bytes): um choque de um segmento com o mundo. A construção (inline nos
// chamadores) põe o ponto em (0, 0, 0, 1) e zera +0x20..+0x4b, +0x50..+0x55 e +0x58, mas não a normal, o +0x4c, o +0x56
// e o +0x5c (lixo da pilha, que o CheckHitWorld copia junto com o choque achado; ninguém os lê): o port os zera.
struct alignas(16) WorldCollisionInfo {
    float point[4];           // +0x00
    float normal[4];          // +0x10
    BarrierListEntry barrier; // +0x20 a barreira achada (copiada inteira)
    uint32_t object;          // +0x48 (p32 WCollisionObject) o objeto achado
    uint32_t unknown4c;
    uint8_t face_type;        // +0x50
    uint8_t face_subtype;     // +0x51
    uint8_t flag52;           // +0x52 o bit 1 das bandeiras da instância, ou o bit 0 das do objeto
    uint8_t hit;              // +0x53 0 nada, 1 o chão, 2 uma barreira, 3 um objeto
    uint16_t unknown54;
    uint16_t unknown56;
    uint32_t face_instance;   // +0x58 (p32 WCollisionInstance) a instância da face ou da barreira
    uint32_t unknown5c;
};
static_assert(sizeof(WorldCollisionInfo) == 0x60);
static_assert(offsetof(WorldCollisionInfo, barrier) == 0x20 && offsetof(WorldCollisionInfo, face_type) == 0x50 &&
              offsetof(WorldCollisionInfo, face_instance) == 0x58);
// A construção inline de um WorldCollisionInfo (o ponto (0, 0, 0, 1) de 0x00355ba0, o resto zerado menos a normal).
void WorldCollisionInfo_Init(WorldCollisionInfo& info);

// OrthoInverse (0x002d19b0, world_collision.cpp): a inversa de uma matriz ortonormal no lugar (a rotação
// transposta e a translação -t pelas linhas antigas). Uma só definição para o mundo, as barreiras e o CheckHitWorld.
void OrthoInverse(float m[16]);

// O mundo da pista para a física: o gerente e a grade montados sobre os dados do CARP (o grupo 'CDat', já
// desserializado e resolvido: Track::root()), e as três consultas que RigidBody::TempGetHeightInformation faz.
class TrackWorld : public WorldCollision {
public:
    // [driving] 0x002132b8 WCollisionMgr::Init e 0x0021c1f8 WGrid::Init sobre o grupo raiz do CARP da pista.
    // false se a pista não tem 'CDat' (a mensagem em `error`).
    bool init(UGroup* root, std::string* error = nullptr);
    bool find_closest_face(WWorldPos& wp, const float pos[3], bool keep) override;
    void make_face_at_point(WWorldPos& wp, const float pos[3]) override;
    float surface_bump_height(const float pos[3], const uint8_t* surface) override;
    // [driving] 0x002151b8 WCollisionMgr::GetWorldHeightAtPoint: a altura do plano da face sob o ponto.
    bool world_height_at_point(const float pos[3], float& height);
    void set_barrier_mask(uint32_t mask) override { mgr_.barrier_mask = mask; }
    void set_vu0_path(uint32_t on) override { mgr_.vu0_path = on; }

    WCollisionMgr& mgr() { return mgr_; }
    WGrid& grid() { return grid_; }
    uint32_t instance_count() const { return mgr_.instance_count; }
    WCollisionInstance* instances() const { return nf::p32_ptr<WCollisionInstance>(mgr_.instances); }
    WGridNode* node(uint32_t cell) const;
    TrackWorld* track_world() override { return this; }
    // Os objetos de colisão ('co') e as instâncias de render do mundo (o 'in  ' do 'Map ': WWorld +0x20, 64 bytes cada;
    // WCollisionObject::MakeMatrix lê a rotação delas, WWorld::GetSceneObjFromInstance o byte +0x0c).
    uint32_t object_count() const { return object_count_; }
    WCollisionObject* objects() const { return nf::p32_ptr<WCollisionObject>(mgr_.objects); }
    uint32_t render_instance_count() const { return render_count_; }
    const uint8_t* render_instances() const { return nf::p32_ptr<const uint8_t>(render_instances_); }
    // Contadores (só do port): consultas, faces achadas.
    uint64_t queries = 0, faces = 0;

private:
    WCollisionMgr mgr_{};
    WGrid grid_{};
    std::vector<uint32_t> node_table_;
    uint32_t object_count_ = 0;
    uint32_t render_instances_ = 0, render_count_ = 0;
};

// Guardas do port: células com lista dinâmica (o port não monta os 'de'), artigos nulos no Init.
extern uint64_t g_world_collision_guards;

// As funções portadas, sobre estruturas do leiaute do PS2 (o nf_driving_check as chama com as do port).
// [driving] 0x00212ed0 WCollisionInstance::MakeMatrix: o mundo para o referencial da instância.
void WCollisionInstance_MakeMatrix(const WCollisionMgr& mgr, const WCollisionInstance& ci, float m[16], bool translate);
// [driving] 0x00212da8 WCollisionInstance::CalcPosition: a posição da instância no mundo.
void WCollisionInstance_CalcPosition(const WCollisionMgr& mgr, const WCollisionInstance& ci, float out[3]);
// [driving] 0x0021cf80 WGrid::FindNodesBox; 0x0021cf30 WGrid::FindNodes(COORD3 &, float, ...).
void WGrid_FindNodesBox(const WGrid& g, const float box_min[4], const float box_max[4], std::vector<uint32_t>& cells);
void WGrid_FindNodes(const WGrid& g, const float centre[3], float radius, std::vector<uint32_t>& cells);
// [driving] 0x00217588 WCollisionMgr::GetInstanceList(... COORD3 &, float, bool, bool) e 0x00217228 ...Guts: as
// instâncias das células em volta do ponto cuja esfera (distância 2D aproximada) chega a `radius`.
void WCollisionMgr_GetInstanceList(WCollisionMgr& mgr, const WGrid& g, std::vector<InstanceListEntry>& list,
                                   const float point[3], float radius, bool strip_lists, bool flag_b);
// [driving] 0x002138b0 WCollisionMgr::FindFaceInTriStrip: o triângulo da faixa sob o ponto (xz), ou nulo.
const StripVertex* WCollisionMgr_FindFaceInTriStrip(const WCollisionMgr& mgr, const float local[3],
                                                    const StripVertex* vertices);
// [driving] 0x00213ff8 WCollisionMgr::FindFaceInCInst(COORD3 &, WCollisionInstance *, ...): a face mais próxima sob o
// ponto numa instância, levada ao mundo, e a distância vertical.
bool WCollisionMgr_FindFaceInCInst(const WCollisionMgr& mgr, const float point[3], const WCollisionInstance& ci,
                                   WWorldPos& face, float& height);
// [driving] 0x002324e8 WWorldPos::FindClosestFace(WCollisionInstanceCacheList &, COORD3 &)
bool WWorldPos_FindClosestFace(WWorldPos& wp, const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                               const float point[3]);
// [driving] 0x002322c8 WWorldPos::FindClosestFace(COORD3 &, bool)
bool WWorldPos_FindClosestFace(WWorldPos& wp, WCollisionMgr& mgr, const WGrid& g, const float point[3], bool keep,
                               int32_t sim_step);
// As buscas de face ao longo de um segmento (a câmera do jogador: RPlayerCamera::ResolveAllCollisions pelo WCollider com
// a máscara de instâncias e WCollisionMgr::GetWorldNormal), portadas do MIPS.
// [driving] 0x00216ec0 WCollisionMgr::GetInstanceStripList: as faixas da instância cuja esfera chega a `radius` do ponto
// e que têm um triângulo (pelo centro e o raio guardado no w do terceiro vértice) também perto; nulo se nenhuma (só xz
// com `flat`).
std::shared_ptr<const StripList> WCollisionMgr_GetInstanceStripList(const WCollisionMgr& mgr, const WCollisionInstance& ci,
                                                                    const float point[3], float radius, bool flat);
// [driving] 0x00231d68 WWorldMath::MakeSegSpaceMatrix: o referencial do segmento (o y ao longo de a - b, a origem em
// a); falso se os pontos coincidem (1e-4 em cada eixo).
bool WWorldMath_MakeSegSpaceMatrix(const float a[3], const float b[3], float m[16]);
// [driving] 0x00231af0 WWorldMath::IntersectSegPlane: o ponto do segmento no plano e o parâmetro t; verdadeiro se t em
// [0, 1] (falso também com o segmento paralelo, sem tocar nas saídas).
bool WWorldMath_IntersectSegPlane(const float a[3], const float b[3], const float on_plane[3], const float normal[3],
                                  float out[3], float& t);
// [driving] 0x00213bd8 (sem nome): a normal (unitária, para cima, y no máximo 0,99999) do triângulo de três vértices de
// 16 bytes; (0, 1, 0) num triângulo degenerado.
void WCollisionMgr_TriangleNormal(float out[3], const float* tri);
// [driving] 0x00213d68 WCollisionMgr::FindFaceInTriStrip(MATRIX4 &, COORD3 &, ...): o triângulo da faixa (os vértices
// levados por m) sob o ponto, o mais perto por baixo (a distância em y na saída; 1e38 sem nenhum), ou nulo.
const StripVertex* WCollisionMgr_FindFaceInTriStripSeg(const WCollisionMgr& mgr, const float m[16], const float point[3],
                                                       const StripVertex* vertices, float& height);
// [driving] 0x00214c08 WCollisionMgr::FindFaceInCInst(MATRIX4 &, COORD3 &, WCollisionInstanceCache &, WWorldPos &,
// float &) e 0x00214678 (a mesma, sem lista de faixas: todas as do artigo): a face da instância que o segmento (do
// referencial `seg`, de seg[12..14] a `end`) atravessa mais perto do começo; a face no mundo e a altura.
bool WCollisionMgr_FindFaceInCInstSeg(const WCollisionMgr& mgr, const float seg[16], const float end[3],
                                      const InstanceListEntry& entry, WWorldPos& face, float& height);
// [driving] 0x00232638 WWorldPos::FindClosestFace(WCollisionInstanceCacheList &, COORD3 &, COORD3 &): a face da lista
// que o segmento de a a b atravessa primeiro.
void WWorldPos_FindClosestFaceSeg(WWorldPos& wp, const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& list,
                                  const float a[3], const float b[3], int32_t sim_step);
// [driving] 0x00232228 WWorldPos::MakeFaceAtPoint
void WWorldPos_MakeFaceAtPoint(WWorldPos& wp, const WCollisionMgr& mgr, const float point[3]);
// [driving] 0x00213618 WCollisionMgr::SurfaceBumpHeight. Divergência: o sinf/cosf do newlib do PS2 não foi portado
// (nf::fm::sin/cos no lugar; o relevo tem 1 a 1,5 cm).
float WCollisionMgr_SurfaceBumpHeight(const float point[3], const uint8_t* surface);
// [driving] 0x00231a90 WWorldMath::GetPlaneY
float WWorldMath_GetPlaneY(const float normal[3], const float on_plane[3], const float point[3]);

// ---- as barreiras e os objetos (a colisão com os prédios: RigidBody::CollideWithWorld e o WCollider) -------------

// [driving] 0x00231548 WWorldMath::SegmentIntersect(COORD4 *, COORD4 *, COORD4 *): os segmentos a (a[0]..a[1]) e b
// (b[0]..b[1]) se cruzam no plano xz (com a folga de ±0,0001 nos parâmetros)? Com `out`, o ponto em a no parâmetro do
// cruzamento (o y também, pelo segmento a) e w = 1. Os pontos são quadwords (x, y, z, w; os segmentos 32 bytes).
bool WWorldMath_SegmentIntersect(const float* a, const float* b, float out[4]);
// [driving] 0x00231720 WWorldMath::IntersectCircle(float *, float *, float, float, float, float, float, float, float):
// o segmento (x0, z0)-(x1, z1) e o círculo (cx, cz, r). As duas pontas dentro: t0 = t1 = 0 e verdadeiro; senão as
// raízes da equação (t0 a menor) e verdadeiro se alguma está em [0, 1].
bool WWorldMath_IntersectCircle(float& t0, float& t1, float x0, float z0, float x1, float z1, float cx, float cz,
                                float r);
// [driving] 0x00219a50 WCollisionMgr::GetBarrierList(WCollisionBarrierList &, WCollisionInstanceCacheList &, COORD3 &,
// float): as barreiras das instâncias da lista que passam a menos de `radius` do centro no plano xz e cobrem a altura
// dele (± radius), sem as do subtipo da máscara do gerente, levadas ao mundo (o y das pontas em ordem).
void WCollisionMgr_GetBarrierList(const WCollisionMgr& mgr, std::vector<BarrierListEntry>& out,
                                  const std::vector<InstanceListEntry>& nearby, const float centre[3], float radius);
// [driving] 0x00219328 WCollisionMgr::GetClosestIntersectingBarrier: a barreira da lista que o segmento (2 quadwords)
// cruza mais perto do começo (a distância 2D aproximada), dentro da altura dela; o ponto em info.point e a barreira
// em info.barrier, info.hit = 2.
bool WCollisionMgr_GetClosestIntersectingBarrier(const WCollisionMgr& mgr, const std::vector<BarrierListEntry>& list,
                                                 const float* segment, WorldCollisionInfo& info);
// [driving] 0x0021a248 WCollisionMgr::GetBarrierNormal(WCollisionBarrierList &, COORD4 *, WorldCollisionInfo &): a
// barreira mais perto e a normal dela no plano xz, virada para o começo do segmento.
bool WCollisionMgr_GetBarrierNormal(const WCollisionMgr& mgr, const std::vector<BarrierListEntry>& list,
                                    const float* segment, WorldCollisionInfo& info);
// [driving] 0x00216d30 WCollisionMgr::ClosestCollisionInfo: dos dois choques, o mais perto do começo do segmento.
void WCollisionMgr_ClosestCollisionInfo(const WCollisionMgr& mgr, const float* segment, const WorldCollisionInfo& a,
                                        const WorldCollisionInfo& b, WorldCollisionInfo& out);
// [driving] 0x002166f8 WCollisionMgr::GetWorldNormal(WCollisionInstanceCacheList &, WCollisionBarrierList &, COORD4 *,
// WorldCollisionInfo &, bool): o choque mais perto do segmento com as barreiras e, com `faces`, com o chão das
// instâncias. Divergência: o chão (WWorldPos::FindClosestFace pela lista de faixas, 0x00232638, e
// WWorldMath::IntersectSegPlane) não foi portado; só os veículos com SUB_PHYSICS o pedem (conta como guarda).
bool WCollisionMgr_GetWorldNormal(const WCollisionMgr& mgr, const std::vector<InstanceListEntry>& instances,
                                  const std::vector<BarrierListEntry>& barriers, const float* segment,
                                  WorldCollisionInfo& info, bool faces);
// [driving] 0x00218440 WCollisionMgr::GetObjectLists(WCollisionObjectList &, WCollisionObjectList &, COORD3 &, float)
// e 0x00217e30 GetObjectListsGuts: os objetos das células em volta do ponto cuja esfera (a distância 2D aproximada)
// chega a `radius`: os cilindros numa lista, as caixas na outra (sem tirar os repetidos de células vizinhas).
void WCollisionMgr_GetObjectLists(const TrackWorld& world, std::vector<WCollisionObject*>& cylinders,
                                  std::vector<WCollisionObject*>& boxes, const float position[3], float radius);
// [driving] 0x00218af0 WCollisionMgr::GetClosestIntersectingCylObject: o cilindro que o segmento cruza mais perto do
// começo; quirk preservado: info.point fica com o ponto do último cilindro cruzado, mesmo que não seja o escolhido.
WCollisionObject* WCollisionMgr_GetClosestIntersectingCylObject(const float* segment, WorldCollisionInfo& info,
                                                                 const std::vector<WCollisionObject*>& list);
// [driving] 0x002187f0 WCollisionMgr::GetCylObjectNormal(COORD4 *, WCollisionObjectList &, WorldCollisionInfo &): o
// cilindro mais perto e a normal dele no ponto (horizontal, virada para o começo do segmento), info.hit = 3.
bool WCollisionMgr_GetCylObjectNormal(const float* segment, const std::vector<WCollisionObject*>& list,
                                      WorldCollisionInfo& info);
// [driving] 0x00212c10 WCollisionObject::MakeMatrix(MATRIX4 &, bool): a rotação da instância de render do objeto (os w
// zerados) e, com `translate`, a posição do objeto na linha 3.
void WCollisionObject_MakeMatrix(const TrackWorld& world, const WCollisionObject& obj, float m[4][4], bool translate);
struct OBB;
// [driving] 0x00218668 WCollisionMgr::GetObjectOBB(WCollisionObjectList &, COORD3 &, WSurface &) (nome de confiança 1):
// tira o último objeto da lista e devolve a caixa dele (no PS2 alocada com FastAlloc(0xb0) e liberada pelo chamador),
// a etiqueta da face e a velocidade (a da instância de render que anda, vezes o passo; zero se não anda). Divergência:
// a velocidade de um objeto que anda (bandeira 1) vem do RSceneObj da animação, que o port não tem: fica zero e conta
// como guarda.
bool WCollisionMgr_GetObjectOBB(const TrackWorld& world, std::vector<WCollisionObject*>& list, float velocity[3],
                                uint8_t tag[2], OBB& out);
// [driving] 0x00230bf0 WWorld::GetSceneObjFromInstance: a instância de render tem um objeto de cena (o byte +0x0c com o
// bit 0x10)? O port não monta os objetos de cena; devolve só se haveria um.
bool WWorld_HasSceneObj(const TrackWorld& world, uint32_t render_index);

}  // namespace drv
