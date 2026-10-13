#pragma once
// O chão que a física consulta: o WWorldPos do DRIVING.ELF (WTargetPicker.obj; a face da malha de colisão sob um
// ponto, achada na grade WGrid do mundo) e as três chamadas que RigidBody::TempGetHeightInformation faz ao mundo, pela
// interface WorldCollision: o mundo de verdade é TrackWorld (world_collision.h: WCollisionMgr, WGrid e WWorldPos
// portados sobre o 'CDat' do CARP da pista). Sem mundo instalado nada acha chão (e cada chamada conta como pendente).
// FlatGround é um mundo de teste do port: um plano horizontal infinito.

#include <cstdint>
#include "platform/ptr32.h"

namespace drv {

class TrackWorld;

// WWorldPos (0x40 bytes, criado por FastAlloc(0x40) + WWorldPos::WWorldPos 0x00232180).
struct alignas(16) WWorldPos {
    float v0[4];         // +0x00 os vértices da face (no mundo); o w de v0 fica com o que estava (lixo da pilha no PS2)
    float v1[4];         // +0x10 o w: as bandeiras da faixa
    float v2[3];         // +0x20
    uint8_t surface[4];  // +0x2c a etiqueta da face: tipo (o piso; 1 na face inventada), subtipo, id u16
    uint32_t valid;      // +0x30 uma face foi achada (ou inventada); o nightfire-cxbx o chama faceValid
    uint32_t instance;   // +0x34 (p32) a WCollisionInstance da face
    uint32_t article;    // +0x38 (p32) o artigo da instância quando a face foi achada (se mudou, a face é velha)
    int32_t last_query;  // +0x3c o passo da simulação da última busca
};
static_assert(sizeof(WWorldPos) == 0x40);

// O mundo, do ponto de vista da física.
class WorldCollision {
public:
    virtual ~WorldCollision() = default;
    // WWorldPos::FindClosestFace(COORD3 &, bool) (0x2322c8; o porte em world_collision.h): põe em wp a face sob pos;
    // devolve se procurou.
    virtual bool find_closest_face(WWorldPos& wp, const float pos[3], bool flag) = 0;
    // WWorldPos::MakeFaceAtPoint(COORD3 &) (0x232228): uma face no ponto.
    virtual void make_face_at_point(WWorldPos& wp, const float pos[3]) = 0;
    // WCollisionMgr::SurfaceBumpHeight(COORD3 &, WSurface &) (0x213618): o relevo do piso no ponto.
    virtual float surface_bump_height(const float pos[3], const uint8_t* surface) = 0;
    // O WCollisionMgr +0x2c (as faces cujo subtipo tem um bit em comum com a máscara não contam), que
    // TempGetHeightInformation põe em 0x20 antes da busca e em 0x10 ao sair. Um mundo sem máscara o ignora.
    virtual void set_barrier_mask(uint32_t mask) { (void)mask; }
    // O WCollisionMgr +0x30, o caminho VU0 das contas (Simulation::SimulateGame o liga durante a colisão com os prédios
    // e os Simulate dos objetos e o desliga antes da colisão com o chão).
    virtual void set_vu0_path(uint32_t on) { (void)on; }
    // O mundo da pista inteiro (as barreiras, os objetos, a grade), que a colisão com os prédios
    // (RigidBody::CollideWithWorld) consulta; nulo num mundo de teste (FlatGround: sem paredes).
    virtual TrackWorld* track_world() { return nullptr; }
};

void set_world(WorldCollision* world);
WorldCollision* world();

// Um plano horizontal em y = height (port). A face devolvida é um triângulo grande em volta do ponto; a normal sai
// (0, 1, 0), que TempGetHeightInformation limita a y = 0,9999 como no PS2.
class FlatGround : public WorldCollision {
public:
    explicit FlatGround(float height) : height_(height) {}
    bool find_closest_face(WWorldPos& wp, const float pos[3], bool flag) override;
    void make_face_at_point(WWorldPos& wp, const float pos[3]) override;
    float surface_bump_height(const float pos[3], const uint8_t* surface) override;
    float height() const { return height_; }
    uint32_t queries = 0;

private:
    float height_;
};

}  // namespace drv
