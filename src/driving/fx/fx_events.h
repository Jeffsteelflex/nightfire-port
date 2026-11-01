#pragma once
// Os eventos de efeito que a missão e os gatilhos criam (docs/driving.md seção 25): os destrutores do PS2 (o evento age
// no destrutor, como os outros de driving/game/events.h), portados do MIPS.
//  - 9 EPlayEffect (0x00153f80): o efeito da galeria de um registro de efeito do CARP (+0x14 o tipo 6, +0x18 o GFX, +0x10
//    as bandeiras, +0x15 a instância, a posição em +0 e o quatérnio em +0x20). Com as bandeiras 0x82, o
//    RSceneObj::TriggerFX do objeto (não portado: contado); sem objeto, na posição mais a translação da matriz do
//    evento (a posição não é girada) com o quatérnio da matriz vezes o do efeito; com objeto, preso a ele (a instância
//    +0x15 do efeito e a velocidade do RSceneObj::GetVelocity), espelhado em x quando a bandeira 0x200 pede outra
//    instância (a posição refletida na translação dela, o y e o z do quatérnio trocados de sinal);
//  - 196 ENuclearBlast (0x00151408): o SWeaponManager::StartNuclearSequence(pos, escala), a escala 1 se negativa;
//  - 250 EPlayGiottoEffect (0x001543e8): o GFX::Trigger pelo nome (0x00232f70) na posição e no quatérnio de um registro
//    (+0 e +0x30), com o w do quatérnio posto em 1 (como o PS2);
//  - 257 ESetCullDistanceFactor (0x00157ec0): o alcance do corte (0x333ef8) = fator x 400.
// Seção 32 (os que faltavam em Paris):
//  - 36 EPlaySystemAnim (0x00154ad8): o estímulo (+8) no sistema (+4) do RAnimEngine::Handle do objeto: o do evento
//    (+0x1c, o EventDynamicData +8), ou o do registro de instância (+0x10, com +0xc), ou o do dono da vaga da Simulation
//    (+0x18; +0x14 os corpos simples), o RSceneObj +0x48 dele (guardado no +0x1c);
//  - 39 EActorMuzzleFlash (0x001479c0): o ponto do efeito (+4) pela matriz da instância dele no objeto (+0xc, sem o
//    mundo) ao ActWeapon::StartMuzzleFlash da arma (+8, o EventDynamicData +0x34) (driving/fx/muzzle_flash.h);
//  - 130 ESetCollisionGeometry (0x00157be0): o WCollisionMgr::SetCollisionArticle da instância do objeto de cena da
//    instância (+4; +0xc o objeto, ou WWorld::GetSceneObjFromInstance) com o artigo de colisão +8 (-1: sem colisão);
//  - 173 ESuppressEffect (0x0015d320): o TriggerFX (a entrada +0x84) do objeto com -1: com o efeito (+4) o 5 pelo
//    identificador (+0x17 dele), sem ele o 4 com a zona 1 << (+0x1c + 16); o objeto como no 36 (+8, +0xc, +0x10, +0x14,
//    +0x18);
//  - 252/253 ESwitchEffectOff/On (0x0015d538/0x0015d650): o PVehicle::MissionEditorSwitch do carro do elemento da IA
//    (+4, pelo RedirectCarpElement; só os veículos) com o bit +8;
//  - 279 ESetReflectiveObject (0x001589f0): a assinatura do objeto refletido (o carro do jogador sem elemento; o +0xfc
//    do registro do CARP de um veículo, o +0x54 de um personagem) na Simulation +0xdc8 (Simulation::SetReflectiveObject
//    0x00201b68). Ninguém lê o +0xdc8 no port (o reflexo do carro).

#include <cstdint>

namespace drv {

struct PendingEvent;

// O ambiente (o nf_driving põe a velocidade dos objetos de cena; nulo: 0, como um RSceneObj sem física nem caminho).
struct FxEventsEnv {
    // RSceneObj::GetVelocity (0x001c9c78, a entrada +0x74 da tabela virtual): o endereço (p32) da velocidade, ou 0.
    uint32_t (*scene_velocity)(uint32_t scene) = nullptr;
    // SWeaponManager::StartNuclearSequence(COORD3 &, float) (0x0020ea20); nulo: o do port (driving/game/weapons.h).
    void (*start_nuclear)(const float pos[3], float scale) = nullptr;
    // Seção 32. O RSceneObj (+0x48, p32) do dono de uma vaga da Simulation (+0x270 os rígidos, +0x330 os simples), ou 0.
    uint32_t (*physics_scene)(int32_t simple, int32_t slot) = nullptr;
    // RSceneObj::GetInstancePosition(instância, MATRIX4 &, false) (0x001c9368 sem o mundo: a identidade vezes a matriz
    // da cópia da instância); falso sem ela (a identidade fica).
    bool (*instance_position_local)(uint32_t scene, int32_t instance, float m[4][4]) = nullptr;
    // RAnimEngine::Handle::ProcessStimuli(sistema, estímulo, tempo, modo); nulo: o do port (driving/render/anim_engine.h).
    void (*stimuli)(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t time, uint32_t mode) = nullptr;
    // WWorld::GetSceneObjFromInstance (0x00230bf0): o objeto de cena (p32) de uma instância de render, ou 0.
    uint32_t (*scene_from_instance)(uint32_t instance) = nullptr;
    // WCollisionMgr::SetCollisionArticle(instância de render, artigo) (driving/physics/world_collision.h); nulo: contado.
    void (*set_collision_article)(uint32_t instance, int32_t article) = nullptr;
    // O passo da simulação (a Simulation +0xdc4) para o StartMuzzleFlash; nulo: simulation().steps.
    int32_t (*steps)() = nullptr;
    // PhysicsObject::GetSig de um objeto físico (p32 da imagem do PS2: o carro de um elemento, o +0x54 de um personagem)
    // e do carro do jogador (Simulation::GetPlayerObject); nulos: 0.
    uint32_t (*object_sig)(uint32_t object) = nullptr;
    uint32_t (*player_sig)() = nullptr;
    // A entrada +0x84 do objeto (o TriggerFX(-1, tipo, máscara, 0, zero, zero) do 173); nulo: SceneObj_TriggerFX.
    void (*trigger_fx)(uint32_t scene, int32_t type, uint32_t mask) = nullptr;
    // AIElementController::RedirectCarpElement (o 252/253); nulo: o do port (driving/ai/element_controller.h).
    uint32_t (*redirect_element)(uint32_t carp) = nullptr;
};
FxEventsEnv& fx_events_env();

// O evento de efeito: verdadeiro se foi executado (9, 196, 250, 257); os só contados devolvem falso (ficam como sem
// porte no relatório do nf_driving).
bool fx_event_execute(const PendingEvent& e);

// O que o port conta (por número, os executados e os só contados).
struct FxEventStats {
    uint64_t play_effect = 0, play_effect_scene = 0, play_effect_mirror = 0, trigger_fx = 0;
    uint64_t giotto = 0, giotto_missing = 0, nuclear = 0, cull_factor = 0, counted = 0, guards = 0;
    // Seção 32: os executados e os sem objeto (o PS2 não faz nada).
    uint64_t system_anim = 0, system_anim_none = 0, actor_flash = 0, actor_flash_none = 0, collision_geometry = 0;
    uint64_t collision_none = 0, suppress = 0, suppress_none = 0, switch_effect = 0, switch_none = 0;
    uint64_t reflective = 0, reflective_none = 0;
};
// A Simulation +0xdc8 (o objeto refletido do ESetReflectiveObject; a assinatura).
uint32_t& sim_reflective_object();
FxEventStats& fx_event_stats();

}  // namespace drv
