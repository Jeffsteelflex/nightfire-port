#pragma once
// A física na VU0: o microprograma VU0_physics do DRIVING.ELF (o segundo dos três programas do .vutext, enviado por
// VU0_UploadMicrocode(0x324e60) a cada quadro) portado à mão para C++, e as funções do EE que carregam os registradores
// da VU0, disparam o programa (vcallms) e recolhem o resultado no quadro seguinte (o PS2 roda a VU0 em paralelo e
// recolhe o corpo anterior quando começa o próximo).
//
// O programa tem quatro pontos de entrada (instruções; o DRIVING.SYM dá os rótulos em bytes):
//   0x000 o quatérnio de vf01 em matriz (vf08..vf10)                    (sem rótulo: o começo do programa)
//   0x022 VU0MICRO_MultiplyMatrix: vf12..vf15 = vf04..vf07 x [vf08 vf09 vf10 vf11]
//   0x034 VU0MICRO_ResolveLeverForces: os impulsos das alavancas que tocam o chão, um depois do outro
//   0x0ad VU0MICRO_UpdatePositionAndOrientation: o passo do corpo rígido
// Os portes à mão leem e escrevem o mesmo estado da VU0 que o microcódigo (registradores e memória de dados, os
// temporários e as pistas que sobram incluídos), com as mesmas operações na mesma ordem (platform/vu0_macro.h), então
// o resultado tem os mesmos bits que o microcódigo rodado no interpretador (platform/vu_interp.h): é o que o
// nf_driving_check confere. Os indicadores (MAC, estado) não fazem parte do contrato: o EE não os lê depois destes
// programas (o porte só usa o sinal do impulso, que o programa testa com FMAND).
//
// Modos: o padrão roda os portes em C++ (Native); Interpreted roda o microcódigo original no interpretador (o
// nf_driving_check o carrega do DRIVING.ELF com set_vu0_physics_program). Os dois têm de dar o mesmo estado.

#include <cstdint>
#include <vector>

#include "platform/vu_interp.h"

namespace drv {

struct RigidBody;
struct RigidBodyInfo;
struct LeverParams;

// A VU0 das missões de carro (registradores e as memórias). O EE só a toca por estas funções.
ps2::VuMachine& vu0();
void vu0_reset();

enum class Vu0Mode { Native, Interpreted };
void set_vu0_mode(Vu0Mode mode);
Vu0Mode vu0_mode();
// O microcódigo do VU0_physics (os pares de 64 bits a partir da instrução 0), para o modo Interpreted.
void set_vu0_physics_program(std::vector<uint8_t> code);
bool has_vu0_physics_program();
// VU0_UploadMicrocode do programa da física (SimulateGame o chama a cada quadro): no modo Interpreted copia o
// microcódigo para a memória de microprograma.
void upload_vu0_physics();
// vcallms instr: roda a partir da instrução (porte em C++ ou o interpretador, pelo modo).
void vu0_callms(uint32_t instr);

// Os portes à mão (o estado é o de vu0()).
void VU0MICRO_QuatToMatrix(ps2::VuMachine& vu);                // 0x000
void VU0MICRO_MultiplyMatrix(ps2::VuMachine& vu);              // 0x022
void VU0MICRO_ResolveLeverForces(ps2::VuMachine& vu);          // 0x034
void VU0MICRO_UpdatePositionAndOrientation(ps2::VuMachine& vu);  // 0x0ad
// Roda o porte do ponto de entrada (instr 0x00, 0x22, 0x34 ou 0xad); false se não for um deles.
bool vu0_physics_native(ps2::VuMachine& vu, uint32_t instr);

// ---- o lado do EE ------------------------------------------------------------------------------------------------
// [driving] 0x1f89e0 Timestep_LoadAndStartVU0(RigidBody *, RigidBodyInfo *, float)
void Timestep_LoadAndStartVU0(RigidBody* rb, RigidBodyInfo* info, float scale);
// [driving] 0x1f8cf0 RigidBody::Timestep_CollectVU0data(void)
void Timestep_CollectVU0data();
// [driving] 0x1f8fd0 RigidBody::UpdatePositionAndOrientation(float)
void UpdatePositionAndOrientation(RigidBody* rb, float scale);
// [driving] 0x1f91e8 RigidBody::ResolveLeverForces(...)
void ResolveLeverForces(RigidBody* rb, const LeverParams* params, const float (*levers)[4], const float (*normals)[4],
                        const float (*tensor_rows)[4]);
// [driving] 0x1f8ff0 RigidBody::Resolve_CollectVU0data(void)
void Resolve_CollectVU0data();

// O que Resolve_CollectVU0data faria com o choque contra o chão (um evento ECollision para o som e o dano, quando a
// maior velocidade normal das alavancas passa de 0,3): o porte só conta e guarda o último.
struct GroundImpactStats {
    uint32_t events = 0;
    float last_speed = 0;
    float last_strength = 0;
    int last_slot = -1;
};
GroundImpactStats& ground_impacts();

}  // namespace drv
