#pragma once
// O chão que a física consulta: o WWorldPos do DRIVING.ELF (WTargetPicker.obj; a face da malha de colisão sob um
// ponto, achada na grade WGrid do mundo) e as três chamadas que RigidBody::TempGetHeightInformation faz ao mundo. O
// mundo (o CARP da pista, WCollisionMgr, WGrid) não foi portado: o porte passa por WorldCollision, que diz qual face
// fica sob o ponto. Sem mundo instalado nada acha chão (e cada chamada conta como pendente). FlatGround é um mundo de
// teste do port: um plano horizontal infinito.

#include <cstdint>

namespace drv {

// WWorldPos (0x40 bytes, criado por FastAlloc(0x40) + WWorldPos::WWorldPos): só os campos que a física lê.
struct alignas(16) WWorldPos {
    float v0[4];         // +0x00 os vértices da face (no mundo)
    float v1[4];         // +0x10
    float v2[3];         // +0x20
    uint8_t surface[4];  // +0x2c WSurface (o tipo do piso; o byte 0 diferente de zero: não inventar face no ponto)
    const void* face;    // +0x30 a face achada (nulo: nenhuma)
    uint8_t rest[0xc];   // +0x34
};
static_assert(sizeof(WWorldPos) == 0x40);

// O mundo, do ponto de vista da física.
class WorldCollision {
public:
    virtual ~WorldCollision() = default;
    // [driving] 0x2322c8 WWorldPos::FindClosestFace(COORD3 &, bool): põe em wp a face sob pos; devolve se achou.
    virtual bool find_closest_face(WWorldPos& wp, const float pos[3], bool flag) = 0;
    // [driving] 0x232228 WWorldPos::MakeFaceAtPoint(COORD3 &) (nome de confiança 1): uma face no ponto.
    virtual void make_face_at_point(WWorldPos& wp, const float pos[3]) = 0;
    // [driving] 0x213618 WCollisionMgr::SurfaceBumpHeight(COORD3 &, WSurface &): o relevo do piso no ponto.
    virtual float surface_bump_height(const float pos[3], const uint8_t* surface) = 0;
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
