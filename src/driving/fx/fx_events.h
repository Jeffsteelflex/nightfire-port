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
// Ficam contados (sem efeito no port): 36 EPlaySystemAnim (o RAnimEngine do objeto pelo AIElementController ou pelas
// tabelas de objetos da Simulation), 130 ESetCollisionGeometry (WCollisionMgr::SetCollisionArticle), 173 ESuppressEffect
// (o RSceneObj::TriggerFX), 252/253 ESwitchEffectOff/On (o PVehicle::MissionEditorSwitch dos veículos da IA) e 279
// ESetReflectiveObject (a Simulation +0xdc8, que o desenho do port não lê).

#include <cstdint>

namespace drv {

struct PendingEvent;

// O ambiente (o nf_driving põe a velocidade dos objetos de cena; nulo: 0, como um RSceneObj sem física nem caminho).
struct FxEventsEnv {
    // RSceneObj::GetVelocity (0x001c9c78, a entrada +0x74 da tabela virtual): o endereço (p32) da velocidade, ou 0.
    uint32_t (*scene_velocity)(uint32_t scene) = nullptr;
    // SWeaponManager::StartNuclearSequence(COORD3 &, float) (0x0020ea20); nulo: o do port (driving/game/weapons.h).
    void (*start_nuclear)(const float pos[3], float scale) = nullptr;
};
FxEventsEnv& fx_events_env();

// O evento de efeito: verdadeiro se foi executado (9, 196, 250, 257); os só contados devolvem falso (ficam como sem
// porte no relatório do nf_driving).
bool fx_event_execute(const PendingEvent& e);

// O que o port conta (por número, os executados e os só contados).
struct FxEventStats {
    uint64_t play_effect = 0, play_effect_scene = 0, play_effect_mirror = 0, trigger_fx = 0;
    uint64_t giotto = 0, giotto_missing = 0, nuclear = 0, cull_factor = 0, counted = 0, guards = 0;
};
FxEventStats& fx_event_stats();

}  // namespace drv
