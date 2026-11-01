#pragma once
// Os efeitos das instâncias de um objeto de cena (docs/driving.md seção 32): o RSceneObj::TriggerFX e o que ele move
// (RSceneObj.obj do DRIVING.ELF), o desenho deles a cada quadro (RSceneObj::RenderEffects, RenderEffect: os clarões das
// lâmpadas, as luzes dinâmicas, os efeitos da galeria), o RHighLevelLightManager::AddDynamicLightEffect, o
// RVehicle::TriggerFX inteiro e as funções da galeria que eles chamam (GFX::Purge, SuppressEffect, SetIntensity e os
// CANVAS_* delas).
//
// Como o PS2 faz: o RAnimEngine::Handle de um objeto de cena (driving/render/anim_engine.h) traz as cópias dos efeitos
// ('ef', 0x40 bytes) dos artigos das instâncias e quatro máscaras de 64 bits: +0x20 os ligados, +0x28 os de dano (a
// bandeira 8), +0x30 os da galeria (o tipo 6) e +0x38 os que um TriggerFX liga (as bandeiras 0x96). Cada efeito: a
// posição (+0, no referencial da instância), as bandeiras (+0x10: 0x80 só com o sistema da instância tocando, 0x100 no
// chão, 0x2000 também nas zonas batidas, 0x14 não somem longe), o tipo (+0x14: 2 um clarão do RGlareManager, 5 uma luz
// dinâmica, 6 um efeito da galeria, 8 nada a desenhar), a instância (+0x15), a zona de dano (+0x16: o bit dela no +0x1c do
// objeto apaga o efeito), o identificador (+0x17), a referência da galeria (+0x18), a tela (+0x1c: o índice + 1, 0
// nenhuma) e o quatérnio (+0x20); os u16 do Handle +0x18 são as zonas de cada efeito (o alto da máscara de um
// TriggerFX), os bytes depois deles os identificadores.
//
// O TriggerFX(tipo, máscara): 0/1 liga (1: pelo identificador), 2/3 desliga, 4/5 suprime as telas (a galeria não as
// renova mais), 9 os efeitos de dano das zonas novas, 10 os estímulos 0x12 das zonas novas (o
// RAnimEngine::Handle::ProcessStimuliZones). Um efeito da galeria ligado ganha uma tela (GFX::Trigger, preso à instância
// com a velocidade do objeto) e a intensidade quando ela não é negativa; desligado, a tela é limpa (GFX::Purge). O
// objeto guarda no +0x20 o prazo de 600 quadros: depois dele, longe da câmera (170 m), os efeitos da galeria que não são
// das bandeiras 0x14 se desligam (DecayEffects).
//
// O RVehicle::TriggerFX troca o tipo 7 pelo dissolver da cor, o 6 pelas faíscas (RVehicleParticle::TriggerSparks), o 8
// pelo clarão do tiro (RVehicle::StartMuzzleFlash, driving/fx/muzzle_flash.h), o 0xb e o 0xc pelas lâmpadas (o +0x268)
// e manda os outros ao RSceneObj::TriggerFX.
//
// Portado do MIPS do PS2, sobre as imagens do PS2 (o RSceneObj de 0x40 bytes, o Handle, os efeitos: ponteiros p32);
// conferido na parte 28 do nf_driving_check (tests/driving_check_fx_ped.cpp).
//
// O que vem de fora (scene_fx_env(): o nf_driving liga, a parte 28 grava): as entradas virtuais do objeto
// (GetVelocity +0x74, GetInstancePosition), a altura do mundo (AdjustEffectPosition), o ProcessStimuliZones, o
// AddModelGlare e a câmera. Divergências (só do port, nas funções): o dissolver da cor (RVehicle::StartColourDissolve,
// tipo 7) e as faíscas (tipo 6) são contados; o laço de 64 bits do PS2 desloca as máscaras pelo índice módulo 64, como
// o DSRLV (o port faz o mesmo).

#include <cstdint>

namespace drv {

// O que os efeitos leem de fora.
struct SceneFxEnv {
    // RSceneObj::GetVelocity (a entrada +0x74): o endereço (p32) da velocidade, ou 0.
    uint32_t (*velocity)(uint32_t scene) = nullptr;
    // RSceneObj::GetInstancePosition(instância, MATRIX4 &, true): a matriz da instância no mundo. Falso: sem ela (a
    // matriz fica zero).
    bool (*instance_position)(uint32_t scene, int32_t instance, float m[4][4]) = nullptr;
    // RSceneObj::GetTransform (0x001c9190; a matriz do objeto, os w 0, 0, 0, 1): nulo, a cópia do +0x08.
    void (*transform)(uint32_t scene, float m[4][4]) = nullptr;
    // WCollisionMgr::GetWorldHeightAtPoint(x, &y, 1): a altura do chão sob (x, z) em *y; falso sem chão.
    bool (*world_height)(const float pos[3], float* y) = nullptr;
    // RAnimEngine::Handle::ProcessStimuliZones(estímulo, zonas, tempo, modo) (0x001a2b50; não portado: nulo, contado).
    void (*stimuli_zones)(uint32_t handle, uint32_t stimulus, uint32_t zones, uint32_t time, uint32_t mode) = nullptr;
    // RGlareManager::AddModelGlare(0, efeito, matriz) (driving/fx/glare.h; nulo: o do port com a câmera abaixo).
    void (*model_glare)(uint32_t effect, const float m[4][4]) = nullptr;
    // A frente da câmera do jogador (o RCamera +0x20, para o AddModelGlare) e o olho (+0x30, para o DecayEffects).
    float forward[4] = {0, 0, 1, 0};
    float eye[4] = {0, 0, 0, 1};
    // As faíscas e o dissolver (RVehicle::TriggerFX 6 e 7; nulos: contados).
    void (*sparks)(uint32_t vehicle, uint32_t which, const float pos[3], const float dir[3], float scale) = nullptr;
    // RAnimEngine::Handle::IsSystemPlaying(GetInstanceSystemID(instância)) (o RenderEffects; nulo: os do port).
    bool (*system_playing)(uint32_t handle, uint32_t instance) = nullptr;
    // As chamadas à galeria (nulas: as do port, GFX_Trigger, GFX_Purge, GFX_SuppressEffect, GFX_SetIntensity).
    int32_t (*gfx_trigger)(uint32_t ref, const float pos[3], const float quat[4], const float pos2[3],
                           const float quat2[4], uint32_t scene, int32_t instance, uint32_t extra) = nullptr;
    void (*gfx_purge)(int32_t handle) = nullptr;
    void (*gfx_suppress)(int32_t handle) = nullptr;
    void (*gfx_intensity)(int32_t handle, float intensity) = nullptr;
    // RHighLevelLightManager::AddDynamicLightEffect (nulo: o do port no light_manager_positional().hl).
    void (*dynamic_light)(uint32_t effect, const float m[4][4]) = nullptr;
};
SceneFxEnv& scene_fx_env();

// [driving] 0x001ca198 RSceneObj::ToggleEffect(bool, unsigned int, CARP::Effect &, float): liga (`on`) ou desliga o
// efeito `index` (a cópia `effect`, p32) do objeto `scene` (p32).
void RSceneObj_ToggleEffect(float intensity, uint32_t scene, bool on, uint32_t index, uint32_t effect);
// [driving] 0x001ca4b0 RSceneObj::TriggerEffects(bool, bool, unsigned int, float)
void RSceneObj_TriggerEffects(float intensity, uint32_t scene, bool on, bool by_id, uint32_t mask);
// [driving] 0x001ca640 RSceneObj::SuppressEffects(bool, unsigned int)
void RSceneObj_SuppressEffects(uint32_t scene, bool by_id, uint32_t mask);
// [driving] 0x001ca7a0 RSceneObj::DamageEffects(unsigned int)
void RSceneObj_DamageEffects(uint32_t scene, uint32_t zones);
// [driving] 0x001caba8 RSceneObj::TriggerFX(SceneObjFXType, unsigned int, unsigned int, float, COORD3 &, COORD3 &)
void RSceneObj_TriggerFX(float intensity, uint32_t scene, uint32_t type, uint32_t mask);
// [driving] 0x001ca340 RSceneObj::DecayEffects
void RSceneObj_DecayEffects(uint32_t scene);
// [driving] 0x001ca8e0 RSceneObj::AdjustEffectPosition(CARP::Effect &, MATRIX4 &): a bandeira 0x100 põe o y da linha 3
// na altura do chão (0 sem chão).
void RSceneObj_AdjustEffectPosition(uint32_t effect, float m[4][4]);
// [driving] 0x001cab38 RSceneObj::RenderEffect(CARP::Effect &, MATRIX4 &): verdadeiro se o efeito fica ligado.
bool RSceneObj_RenderEffect(uint32_t effect, const float m[4][4]);
// [driving] 0x001ca930 RSceneObj::RenderEffects(bool): `glares_only` (o argumento) desenha só os do tipo 2.
void RSceneObj_RenderEffects(uint32_t scene, bool glares_only);
// A entrada +0x84 da tabela virtual (o TriggerFX de cada classe): o RVehicle (0x003885d8) pelo RVehicle::TriggerFX, os
// outros (o RSceneObj, o ActWeapon) pelo do RSceneObj. `p0`/`p1` as duas posições (COORD3; nulas: zero).
void SceneObj_TriggerFX(float intensity, uint32_t scene, int32_t type, uint32_t mask, uint32_t extra,
                        const float* p0, const float* p1);
// [driving] 0x001d3c28 RVehicle::TriggerFX(SceneObjFXType, unsigned int, unsigned int, float, COORD3 &, COORD3 &)
void RVehicle_TriggerFXFull(float intensity, uint32_t vehicle, int32_t type, uint32_t mask, uint32_t extra,
                            const float* p0, const float* p1);
constexpr uint32_t kVtRVehicle = 0x003885d8;

// [driving] 0x001af708 (o brilho de uma luz dinâmica no tempo: a fase pela frequência, o ciclo, o pisca e a onda)
float DynamicLight_Brightness(uint32_t effect, uint32_t time);
// [driving] 0x001af8c0 RHighLevelLightManager::AddDynamicLightEffect(CARP::DynamicLight &, MATRIX4 &): a luz na posição
// do efeito pela matriz (o w é o brilho), a cor dos bytes +0x1f, +0x1e, +0x1d, +0x1c / 255, a prioridade do +0x18.
struct RHighLevelLightManager;
void RHighLevelLightManager_AddDynamicLightEffect(RHighLevelLightManager& m, uint32_t effect, const float mat[4][4],
                                                  uint32_t time);

// [driving] 0x00233160 GFX::Purge (0x002356b0 CANVAS_Purge, 0x00235520 CANVAS_PurgeSceneObj)
void GFX_Purge(int32_t handle);
// [driving] 0x002331c0 GFX::SuppressEffect (0x00235720 CANVAS_Suppress)
void GFX_SuppressEffect(int32_t handle);
// [driving] 0x002331e8 GFX::SetIntensity (0x00235750 CANVAS_SetIntensity)
void GFX_SetIntensity(int32_t handle, float intensity);
struct GfxGallery;
void GFXGallery_CANVAS_PurgeSceneObj(GfxGallery& g, int32_t canvas);
void GFXGallery_CANVAS_Suppress(GfxGallery& g, int32_t handle);
void GFXGallery_CANVAS_SetIntensity(GfxGallery& g, int32_t handle, float intensity);

// O que o port conta.
struct SceneFxStats {
    uint64_t trigger_fx = 0, toggled_on = 0, toggled_off = 0, gfx_triggers = 0, purges = 0, suppressed = 0;
    uint64_t intensities = 0, damage = 0, decays = 0, renders = 0, glares = 0, lights = 0, stimuli_zones = 0;
    uint64_t sparks = 0, dissolves = 0, muzzle = 0, lamps = 0, guards = 0;
};
SceneFxStats& scene_fx_stats();

}  // namespace drv
