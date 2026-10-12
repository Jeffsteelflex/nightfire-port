// Stubs das funções da IA ainda não portadas. Ver drone_stubs.h.

#include "engine/drone_stubs.h"

#include "platform/pending.h"

#define NF_DRONE_STUB(name, params) \
    void name params { pending_call(#name); }

NF_DRONE_STUB(NDrone2_CreateNinjaEyes, (obj_tag*))
NF_DRONE_STUB(BodyGlow_Delete, (obj_tag*))
NF_DRONE_STUB(DroneFunc_HandleExplosives, (DCVars_tag*))
NF_DRONE_STUB(NDrone2_NavNodeCache, ())
uint8_t Drone_bDontDraw = 0;
alignas(4) uint8_t MusicVars[36];
NF_DRONE_STUB(BOT_setGender, (Drone_tag*))
NF_DRONE_STUB(BOT_postLoadInit, (Drone_tag*))
NF_DRONE_STUB(BOTWEAP_changeWeapon, (Drone_tag*, uint16_t))
uint32_t BOT_validateStateChange(DCVars_tag*, uint32_t) {
    pending_call("BOT_validateStateChange");
    return 1;
}
NF_DRONE_STUB(BOT_setOtherPlayerInfo, (DCVars_tag*))
float BOT_getMovementSpeedMul(Drone_tag*) {
    pending_call("BOT_getMovementSpeedMul");
    return 1.0f;
}
uint32_t BOTSTATE_getStateType(uint32_t) {
    pending_call("BOTSTATE_getStateType");
    return 0;
}

// Chamadas da visão ainda não portadas.
uint32_t BOT_handleOpponentHistory(Drone_tag*, obj_tag*) {
    pending_call("BOT_handleOpponentHistory");
    return 0;
}
uint32_t BOT_reactToDroneAlertMsg(uint32_t, Drone_tag*, Drone_tag*) {
    pending_call("BOT_reactToDroneAlertMsg");
    return 0;
}
NF_DRONE_STUB(BOTSTATE_combatWeaponChangeChoice, (Drone_tag*, obj_tag*, int8_t, int8_t, int8_t))
uint32_t BOTSTATE_opponentIsMissile(Drone_tag*, obj_tag*) {
    pending_call("BOTSTATE_opponentIsMissile");
    return 0;
}
NF_DRONE_STUB(BOTSTATE_reallyCloseCombatRange, (Drone_tag*))
NF_DRONE_STUB(BOTSTATE_defaultCombatRange, (Drone_tag*))
NF_DRONE_STUB(BOTSTATE_increaseDistraction, (Drone_tag*, float))
bool MP_areObjectsOnSameTeam(obj_tag*, obj_tag*) {
    pending_call("MP_areObjectsOnSameTeam");
    return false;
}
uint32_t MP_getObjectTeam(obj_tag*) {
    pending_call("MP_getObjectTeam");
    return 2;
}
NF_DRONE_STUB(MP_sendBotMessage, (obj_tag*, uint32_t, uint32_t, uint32_t))
obj_tag* MP_getProtectionObj_NotExt() {
    pending_call("MP_getProtectionObj_NotExt");
    return nullptr;
}
obj_tag* MP_getDemolitionObj_NotExt() {
    pending_call("MP_getDemolitionObj_NotExt");
    return nullptr;
}

// Chamadas do corpo ainda não portadas.
NF_DRONE_STUB(BOT_handlePain, (DCVars_tag*, float, uint16_t, int16_t))
NF_DRONE_STUB(BOT_soundEffect, (DCVars_tag*, uint32_t))

// Chamadas das armas da IA ainda não portadas.
NF_DRONE_STUB(BOT_opponentTargetting, (DCVars_tag*))
int32_t BOTWEAP_AmmoInGun(Drone_tag*, int16_t) {
    pending_call("BOTWEAP_AmmoInGun");
    return 0;
}
uint32_t BOTWEAP_tooCloseForWeapon(Drone_tag*, int16_t, float) {
    pending_call("BOTWEAP_tooCloseForWeapon");
    return 0;
}
NF_DRONE_STUB(BOTWEAP_decrRounds, (Drone_tag*, int16_t))
NF_DRONE_STUB(BOTSTATE_changeWeapon, (BOT_vars_t*, uint32_t, int8_t, int8_t))

// Chamadas dos estados de combate ainda não portadas.
