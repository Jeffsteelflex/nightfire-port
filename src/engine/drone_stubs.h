#pragma once
// Funções chamadas pelo núcleo dos drones e ainda não portadas (stubs em drone_stubs.cpp, cada um
// conta a chamada com pending_call). Saem daqui conforme os módulos da IA forem portados.

#include <cstdint>

#include "engine/object.h"
#include "engine/player_stats.h"

struct ANIMSCRIPT_tag;
struct DCVars_tag;
struct MsgObject;
struct Drone_tag;

// Efeitos.
void NDrone2_CreateNinjaEyes(obj_tag* obj);
void BodyGlow_Delete(obj_tag* glow);
// Armas (as DroneWeap_* estão em drone_weapon.cpp).
void DroneFunc_HandleExplosives(DCVars_tag* dcv);
// Processamento global por tick (Drone_InitComms).
void NDrone2_NavNodeCache();
void Sound_ZeroAlertness();
extern uint8_t Drone_bDontDraw;
alignas(4) extern uint8_t MusicVars[36];  // módulo de música (0x002707a0): +2, +0xc, +0xe u16; +0x14/+0x18 float; +0x20
// As estatísticas (PlrStat_*) estão em engine/player_stats.h; Pickup_CreateSimple, que solta a arma, em
// engine/pickup.h.
// Missão (portada em mission.cpp, ver mission.h) e switches (portados em script_player.cpp, ver script.h).
void Mission_SetFailLabel(uint32_t label);
obj_tag* Switch_GetNearest(_VECTOR* pos);
uint16_t Switch_GetSwitchChannel(obj_tag* sw);
// Bots do multijogador.
void BOT_setGender(Drone_tag* drone);
void BOT_postLoadInit(Drone_tag* drone);
void BOTWEAP_changeWeapon(Drone_tag* drone, uint16_t weapon);
uint32_t BOT_validateStateChange(DCVars_tag* dcv, uint32_t state);  // stub: 1 (aceita)
void BOT_setOtherPlayerInfo(DCVars_tag* dcv);
float BOT_getMovementSpeedMul(Drone_tag* drone);  // stub: 1
uint32_t BOTSTATE_getStateType(uint32_t state);    // stub: 0

// Chamadas da visão ainda não portadas (bots, multijogador e som; só os bots chegam às BOT_*/MP_*).
struct DYNAMICSOUNDS;
uint32_t BOT_handleOpponentHistory(Drone_tag* drone, obj_tag* opponent);  // stub: 0
uint32_t BOT_reactToDroneAlertMsg(uint32_t msg, Drone_tag* drone, Drone_tag* other);  // stub: 0 (não reage)
void BOTSTATE_combatWeaponChangeChoice(Drone_tag* drone, obj_tag* opponent, int8_t a, int8_t b, int8_t c);
uint32_t BOTSTATE_opponentIsMissile(Drone_tag* drone, obj_tag* obj);  // stub: 0
void BOTSTATE_reallyCloseCombatRange(Drone_tag* drone);
void BOTSTATE_defaultCombatRange(Drone_tag* drone);
void BOTSTATE_increaseDistraction(Drone_tag* drone, float amount);
bool MP_areObjectsOnSameTeam(obj_tag* a, obj_tag* b);  // stub: falso (sem times)
uint32_t MP_getObjectTeam(obj_tag* obj);               // stub: 2 (sem time, como fora dos modos de times)
void MP_sendBotMessage(obj_tag* obj, uint32_t msg, uint32_t data, uint32_t delay);
obj_tag* MP_getProtectionObj_NotExt();                 // stub: nulo
obj_tag* MP_getDemolitionObj_NotExt();                 // stub: nulo
// O que um personagem em pos ouve: devolve a intensidade, escreve o nível e o som (portada: engine/sound.h).
float Sound_Alertness(_VECTOR* pos, float* level, DYNAMICSOUNDS** sound);

// Chamadas do corpo ainda não portadas.
void BOT_handlePain(DCVars_tag* dcv, float damage, uint16_t kind, int16_t location);
void BOT_soundEffect(DCVars_tag* dcv, uint32_t sound);

// Chamadas das armas da IA ainda não portadas (só os bots, DTYPE 0x1e, chegam às BOT*_*).
struct BOT_vars_t;
void BOT_opponentTargetting(DCVars_tag* dcv);
int32_t BOTWEAP_AmmoInGun(Drone_tag* drone, int16_t weapon);                     // stub: 0 (sem munição)
uint32_t BOTWEAP_tooCloseForWeapon(Drone_tag* drone, int16_t weapon, float dist);  // stub: 0
void BOTWEAP_decrRounds(Drone_tag* drone, int16_t rounds);
void BOTSTATE_changeWeapon(BOT_vars_t* bot, uint32_t weapon, int8_t a, int8_t b);

// Chamadas dos estados de combate ainda não portadas.
void SP_UnPause(obj_tag* obj);              // script player (portado em script_player.cpp)
