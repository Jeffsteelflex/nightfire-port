#pragma once
// O WCollider do DRIVING.ELF (SWeaponManager.obj, 0x60 bytes no PS2): o que um objeto que anda guarda do mundo em
// volta dele, para as consultas de cada quadro não varrerem a grade. Uma região (uma esfera um pouco maior que o
// objeto, puxada para onde ele anda) e, para ela, as instâncias de colisão (com as faixas perto) e as barreiras que o
// gerente lista; as listas só são refeitas quando o objeto sai da região ou quando uma instância de onde elas vieram
// mudou de artigo. Portado do MIPS (docs/driving.md seção 13); o nightfire-cxbx (NightfireResearch,
// src/driving/world/Collider.cpp) deu os nomes.
//
// O leiaute do PS2: +0x00 a posição e o raio, +0x10 os do quadro anterior, +0x20 o centro e o raio da região, +0x30 o
// vetor das instâncias, +0x3c o das barreiras, +0x48 o dos carimbos (o artigo de cada barreira quando listada e onde a
// instância o guarda), +0x54 a região está montada, +0x58 "plano" (para GetInstanceStripList), +0x5c a máscara (4:
// barreiras, 8: instâncias). Aqui os vetores são std::vector (o conteúdo e a ordem são os do PS2; os tamanhos
// reservados, 100 e 21, também).

#include <cstdint>
#include <vector>

#include "driving/physics/world_collision.h"

namespace drv {

struct WCollider {
    enum : uint32_t { kBarriers = 4, kInstances = 8 };
    struct Stamp {
        uint32_t article;        // o artigo da instância quando a barreira foi listada (p32)
        const uint32_t* source;  // onde a instância o guarda (WCollisionInstance +0x1c)
    };
    float position[3] = {};
    float radius = 0;
    float prev_position[3] = {};
    float prev_radius = 0;
    float region_centre[3] = {};
    float region_radius = 0;
    std::vector<InstanceListEntry> instances;
    std::vector<BarrierListEntry> barriers;
    std::vector<Stamp> stamps;
    uint32_t region_valid = 0;
    uint32_t flat = 0;
    uint32_t mask = 0;
};

// [driving] 0x002117e8 WCollider::WCollider(COORD3 &, float, bool, unsigned int)
void WCollider_Construct(WCollider& c, TrackWorld& world, const float position[3], float radius, bool flat,
                         uint32_t mask);
// [driving] 0x00211e18 WCollider::Refresh(COORD3 &, float)
void WCollider_Refresh(WCollider& c, TrackWorld& world, const float position[3], float radius);
// [driving] 0x002124b8 WCollider::Validate(void) const
bool WCollider_Validate(const WCollider& c);
// [driving] 0x002126d0 WCollider::InRegion(COORD3 &, float, unsigned int) const: a esfera inteira dentro da região.
bool WCollider_InRegion(const WCollider& c, const float position[3], float radius, uint32_t mask);
// [driving] 0x00212368 WCollider::Clear(void)
void WCollider_Clear(WCollider& c);
// [driving] 0x00211ed0 WCollider::PrepareRegion(void)
void WCollider_PrepareRegion(WCollider& c, TrackWorld& world);
// [driving] 0x00211ac8 (sem nome; Util_FitColliderRegion no nightfire-cxbx): a região de uma esfera que anda. Sem região
// anterior, a esfera com o raio x 1,1; senão o centro vai 1,1 vezes o passo à frente e o raio é 1,1 x (raio + o
// passo, no máximo 1).
void WCollider_FitRegion(bool had_region, const float position[3], const float prev_position[3], float centre[3],
                         float& region_radius, float radius);
// [driving] 0x002127f0 WCollider::GetWorldNormal(COORD4 *, WCollisionMgr::WorldCollisionInfo &): sem nada nas listas,
// falso (sem tocar em info).
bool WCollider_GetWorldNormal(const WCollider& c, const TrackWorld& world, const float* segment, WorldCollisionInfo& info);

}  // namespace drv
