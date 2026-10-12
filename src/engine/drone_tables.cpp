// Tabelas estáticas dos personagens da IA (preenchidas na partida a partir do ACTION.ELF do usuário,
// game/static_init.cpp) e a tradução dos ponteiros de código delas para as funções portadas.

#include <cstring>

#include "engine/drone.h"

DroneTypeSetting DroneTypeSettings[kDroneTypeCount];
DroneModeSetting DroneModeSettings[kDroneModeCount];
DroneAnimInfo Drone_AnimInfo[kDroneAnimCount];
DroneAnimTableEntry Drone_AnimTables[kDroneAnimCount][kDroneAnimSubclasses];
DroneAnimState DroneAnimStates[kDroneAnimStateCount];
uint8_t DroneAnimStateCalls[kDroneAnimCallsSize];
DroneDataFunc DroneData_AnimFunc[2];
DroneDataFunc DroneData_DAStateFunc[2];
F32<DroneStateFuncSig> NDrone2_StateFuncs[kDroneStateCount];
uint8_t bitDescs[182];
uint32_t bitMasks[35];
float DroneCaptain_Mod_BulletDamage = 2.0f;
float DroneCaptain_Mod_BulletAccuracy = 2.0f;
float DroneCaptain_Mod_Health = 2.0f;
uint32_t drone_stats[17][3][8];

static_assert(sizeof(Drone_AnimTables) == 47088 && sizeof(Drone_AnimInfo) == 10464);
static_assert(sizeof(DroneAnimStates) == 1428 && sizeof(DroneTypeSettings) == 1020);
static_assert(sizeof(DroneModeSettings) == 420 && sizeof(NDrone2_StateFuncs) == 1000);
static_assert(sizeof(drone_stats) == 0x660);

namespace {

template <class F>
uint32_t code(F* fn) {
    return nf::p32_fn_u(fn);
}

struct CodeName {
    const char* name;
    uint32_t address;
};

const CodeName* code_table(size_t* count) {
    static const CodeName kCodes[] = {
        {"NDrone2_init_DMODE_Normal__FP9Drone_tag", code(NDrone2_init_DMODE_Normal)},
        {"NDrone2_init_DMODE_Guard__FP9Drone_tag", code(NDrone2_init_DMODE_Guard)},
        {"NDrone2_init_DMODE_Retreater__FP9Drone_tag", code(NDrone2_init_DMODE_Retreater)},
        {"NDrone2_init_DMODE_Sniper__FP9Drone_tag", code(NDrone2_init_DMODE_Sniper)},
        {"NDrone2_init_DMODE_Stealth__FP9Drone_tag", code(NDrone2_init_DMODE_Stealth)},
        {"NDrone2_init_DMODE_Attacker__FP9Drone_tag", code(NDrone2_init_DMODE_Attacker)},
        {"NDrone2_init_DMODE_RunToPoint__FP9Drone_tag", code(NDrone2_init_DMODE_RunToPoint)},
        {"NDrone2_init_DMODE_Assassin__FP9Drone_tag", code(NDrone2_init_DMODE_Assassin)},
        {"NDrone2_init_DMODE_HostageKiller__FP9Drone_tag", code(NDrone2_init_DMODE_HostageKiller)},
        {"NDrone2_init_DMODE_Hostage__FP9Drone_tag", code(NDrone2_init_DMODE_Hostage)},
        {"NDrone2_init_DMODE_HostageTied__FP9Drone_tag", code(NDrone2_init_DMODE_HostageTied)},
        {"NDrone2_init_DMODE_JustStand4Demo__FP9Drone_tag", code(NDrone2_init_DMODE_JustStand4Demo)},
        {"NDrone2_init_DMODE_DeleteMe__FP9Drone_tag", code(NDrone2_init_DMODE_DeleteMe)},
        {"NDrone2_init_DMODE_Civilian__FP9Drone_tag", code(NDrone2_init_DMODE_Civilian)},
        {"NDrone2_init_DMODE_CivilianScared__FP9Drone_tag", code(NDrone2_init_DMODE_CivilianScared)},
        {"NDrone2_init_DMODE_MissionFailer__FP9Drone_tag", code(NDrone2_init_DMODE_MissionFailer)},
        {"NDrone2_init_DMODE_Mayhew__FP9Drone_tag", code(NDrone2_init_DMODE_Mayhew)},
        {"NDrone2_init_DMODE_Ninja__FP9Drone_tag", code(NDrone2_init_DMODE_Ninja)},
        {"NDrone2_init_DMODE_AlarmRaiser__FP9Drone_tag", code(NDrone2_init_DMODE_AlarmRaiser)},
        {"NDrone2_init_DMODE_SearchLight__FP9Drone_tag", code(NDrone2_init_DMODE_SearchLight)},
        {"NDrone2_init_DMODE_Ambush__FP9Drone_tag", code(NDrone2_init_DMODE_Ambush)},
        {"NDrone2_init_DMODE_Zoe__FP9Drone_tag", code(NDrone2_init_DMODE_Zoe)},
        {"NDrone2_init_DMODE_PartyGirl__FP9Drone_tag", code(NDrone2_init_DMODE_PartyGirl)},
        {"NDrone2_init_DMODE_CivilianGuard__FP9Drone_tag", code(NDrone2_init_DMODE_CivilianGuard)},
        {"NDrone2_init_DMODE_Interogator__FP9Drone_tag", code(NDrone2_init_DMODE_Interogator)},
        {"NDrone2_init_DMODE_CivDoorGuard__FP9Drone_tag", code(NDrone2_init_DMODE_CivDoorGuard)},
        {"NDrone2_init_DMODE_TruckDriver__FP9Drone_tag", code(NDrone2_init_DMODE_TruckDriver)},
        {"NDrone2_init_DMODE_CastleChatGuard1__FP9Drone_tag", code(NDrone2_init_DMODE_CastleChatGuard1)},
        {"NDrone2_init_DMODE_CastleChatGuard2__FP9Drone_tag", code(NDrone2_init_DMODE_CastleChatGuard2)},
        {"NDrone2_init_DMODE_SniperAlert__FP9Drone_tag", code(NDrone2_init_DMODE_SniperAlert)},
        {"NDrone2_init_DMODE_PartyGirlLooker__FP9Drone_tag", code(NDrone2_init_DMODE_PartyGirlLooker)},
        {"NDrone2_init_DMODE_Bot__FP9Drone_tag", code(NDrone2_init_DMODE_Bot)},
        {"NDrone2_initDTYPE_Sniper__FP9Drone_tag", code(NDrone2_initDTYPE_Sniper)},
        {"NDrone2_initDTYPE_SniperAlert__FP9Drone_tag", code(NDrone2_initDTYPE_SniperAlert)},
        {"NDrone2_initDTYPE_BotInit__FP9Drone_tag", code(NDrone2_initDTYPE_BotInit)},
        {"DAnimFunc_Run__FP9Drone_tagUs", code(DAnimFunc_Run)},
        {"DAStateFunc_AimStand__FP9Drone_tagUs", code(DAStateFunc_AimStand)},
#define NF_DSTATE(name) {"NDrone2_DSTATE_" #name "__FP10DCVars_tagP9Drone_tagP7obj_tagP9MsgObject", code(NDrone2_DSTATE_##name)},
        NF_DRONE_STATES(NF_DSTATE)
#undef NF_DSTATE
    };
    *count = sizeof(kCodes) / sizeof(kCodes[0]);
    return kCodes;
}

}  // namespace

uint32_t Drone_ResolveCode(const char* symbol_name, uint32_t) {
    size_t n = 0;
    const CodeName* codes = code_table(&n);
    for (size_t i = 0; i < n; ++i) {
        if (std::strcmp(codes[i].name, symbol_name) == 0) return codes[i].address;
    }
    return 0;
}
