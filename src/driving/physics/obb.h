#pragma once
// A caixa orientada (OBB, 0xb0 bytes) do DRIVING.ELF (RVehicleParticle.obj): o teste de eixos separadores entre duas
// caixas que a física usa nos choques entre corpos (RigidBody::CollideWithObject) e contra as caixas dos objetos de
// colisão do mundo (RigidBody::CollideWithWorld). Leiaute do PS2 byte a byte (o nf_driving_check a monta na RAM
// emulada); portada do MIPS (docs/driving.md seção 13), com a aritmética da VU0 em macro onde o original a usa.
//
// O que o teste faz (0x001f5ff8, lido na desmontagem; o nightfire-cxbx, NightfireResearch, deu os nomes): para cada
// uma das duas caixas e cada um dos seus três eixos (na ordem x, z, y), a distância entre os centros projetada no eixo
// menos a meia-medida da caixa do eixo e as três meias-medidas da outra projetadas; positiva: as caixas se separam
// (devolve falso). Senão guarda a menor sobreposição (o maior desses números, todos <= 0) em +0x90, a quina da outra
// caixa que mais entra (+0x70) e o eixo virado para ela (+0x80), sempre no `this`, mesmo na passada pelos eixos da
// outra caixa (então o eixo vai negado e +0x94 = 1).

#include <cstddef>
#include <cstdint>

namespace drv {

struct alignas(16) OBB {
    float centre[4];         // +0x00 (o w como veio)
    float axis[3][4];        // +0x10 as linhas 0-2 da matriz (os w como vieram)
    float extent[3][4];      // +0x40 cada eixo vezes a meia-medida (o w é o da linha)
    float contact[4];        // +0x70 a quina da outra caixa na menor sobreposição (zero no Reset)
    float normal[4];         // +0x80 o eixo da menor sobreposição (zero no Reset)
    float penetration;       // +0x90 a menor sobreposição (-100000 no Reset; negativa)
    uint8_t second_pass;     // +0x94 achada num eixo da outra caixa
    uint8_t unknown95[0xb];  // não escritos
    float half[4];           // +0xa0 as meias-medidas
};
static_assert(sizeof(OBB) == 0xb0);
static_assert(offsetof(OBB, contact) == 0x70 && offsetof(OBB, penetration) == 0x90 && offsetof(OBB, half) == 0xa0);

// [driving] 0x001f5de8 OBB::Reset(MATRIX4 *, COORD4 *, COORD4 *) e 0x001f5cf0 OBB::OBB(MATRIX4 *, COORD4 *, COORD4 *)
// (o mesmo código; o construtor devolve o this): as linhas 0-2 de `matrix` (16 bytes cada), o centro e as meias-medidas
// (16 bytes cada) copiados inteiros.
void OBB_Reset(OBB& o, const float matrix[4][4], const float centre[4], const float half[4]);
// [driving] 0x001f5eb0 OBB::CheckOBBOverlap(OBB *): só o teste (os gatilhos em caixa).
bool OBB_CheckOBBOverlap(const OBB& self, const OBB& other);
// [driving] 0x001f5ff8 OBB::CheckOBBOverlapAndFindIntersection(OBB *)
bool OBB_CheckOBBOverlapAndFindIntersection(OBB& self, const OBB& other);

}  // namespace drv
