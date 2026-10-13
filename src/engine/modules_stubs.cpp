// Stubs de funções do jogo ainda não portadas (ver modules.h) e as poucas já portadas que ainda
// não têm um arquivo próprio do módulo.

#include <cmath>
#include <cstring>

#include "engine/modules.h"
#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/input.h"
#include "engine/memory.h"
#include "engine/player.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/text.h"
#include "engine/vecmath.h"
#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/trigger.h"
#include "engine/view.h"
#include "platform/pending.h"

std::vector<ProvisionalObject> g_provisional_objects;

// ---- stubs ----

#define NF_STUB(name, params) \
    void name params { pending_call(#name); }

NF_STUB(MP_Start, ())
NF_STUB(MP_PostLoad_Init, ())
NF_STUB(Cameras_PostLoad, ())
NF_STUB(Creature_Init, ())
NF_STUB(MP_Init, ())
NF_STUB(Car_Init, ())

NF_STUB(MP_objectBeingDeleted, (obj_tag*))

// Chamadas pelo laço do jogo (game.cpp, gameflow.cpp, control.cpp, trigger.cpp).
NF_STUB(MP_Update, ())
NF_STUB(Drone_InitOnce, ())
NF_STUB(Boot_LoadPTPData, ())
void Boot_GetPTPData(uint8_t** data, uint32_t* size) {
    pending_call("Boot_GetPTPData");
    *data = nullptr;
    *size = 0;
}
NF_STUB(Car_Deactivate, (obj_tag*))
NF_STUB(MP_setLoadingSkins, ())



// Chamadas pelo jogador (player.cpp) e pelas câmeras.
NF_STUB(AnimDebugCreate, ())
void* BodyGlow_Create(obj_tag*) {
    pending_call("BodyGlow_Create");
    return nullptr;
}
bool MP_IsAssasin(obj_tag*) {
    pending_call("MP_IsAssasin");
    return false;
}
bool MP_IsTarget(obj_tag*) {
    pending_call("MP_IsTarget");
    return false;
}
NF_STUB(Car_PlayerHasDied, (obj_tag*))
NF_STUB(MP_PlayerKilled, (obj_tag*))
NF_STUB(MP_ReSpawn, (obj_tag*, int16_t))
NF_STUB(Car_Activate, (obj_tag*, obj_tag*))
NF_STUB(Camera_Orbit, (viewer_tag*, void*))
NF_STUB(camera_track_XZ_pos_behind, (_VECTOR*, _VECTOR*, viewer_tag*, float, float))
NF_STUB(camera_track_Y_pos, (obj_tag*, viewer_tag*, float, float))
NF_STUB(Camera_PosYRot, (_VECTOR*, viewer_tag*, float))
NF_STUB(Camera_PosXRot, (_VECTOR*, viewer_tag*, float))
NF_STUB(camera_freechase, (obj_tag*, viewer_tag*))
NF_STUB(camera_fixed, (obj_tag*, viewer_tag*))
NF_STUB(camera_snake, (obj_tag*, viewer_tag*))
NF_STUB(camera_chasecam, (obj_tag*, viewer_tag*))
float debug_cam_posy = 0.0f;

// Chamadas pelo módulo de animação (anim.cpp) e ainda não portadas.

NF_STUB(MP_EquipPlayer, (obj_tag*))
NF_STUB(MP_sendTeamBotMessage, (int32_t, int32_t, int32_t, int32_t, obj_tag*))


// ---- Chamadas da destruição ainda não portadas (break.cpp, explode.cpp) ----
NF_STUB(MP_ApplyDamage, (obj_tag*, float, obj_tag*))

// Chamadas dos itens ainda não portadas (declaradas em engine/pickup.h).
NF_STUB(MP_RegisterPickup, (obj_tag*))
NF_STUB(MP_UnregisterPickup, (obj_tag*))
MP_PICKUP* MP_getMPpickupFromPickupinfo(PICKUPINFO*) {
    pending_call("MP_getMPpickupFromPickupinfo");
    return nullptr;
}
uint8_t* BOT_getDefaultStats(uint32_t) {
    pending_call("BOT_getDefaultStats");
    alignas(4) static uint8_t stats[0x20];
    return stats;
}
uint32_t Upgrade_MPWeapon(int16_t, uint32_t weapon) {
    pending_call("Upgrade_MPWeapon");
    return weapon;
}
uint32_t BOTWEAP_EquipWeapon(Drone_tag*, int16_t, int16_t) {
    pending_call("BOTWEAP_EquipWeapon");
    return 0;
}
int32_t BOTWEAP_EquipAmmo(Drone_tag*, int16_t, int16_t) {
    pending_call("BOTWEAP_EquipAmmo");
    return 0;
}
uint32_t BOTWEAP_hasLoadedWeapon(Drone_tag*, int16_t) {
    pending_call("BOTWEAP_hasLoadedWeapon");
    return 0;
}
NF_STUB(BOTSTATE_pickupWeaponChangeChoice, (Drone_tag*, int16_t, int8_t, int8_t))
NF_STUB(BOTWEAP_setDroneArmour, (Drone_tag*))
NF_STUB(BOTSTATE_setPickupVisitTime, (Drone_tag*, MP_PICKUP*))
uint32_t MP_isObjectOnTeam(obj_tag*, uint32_t) {
    pending_call("MP_isObjectOnTeam");
    return 0;
}


// ---- Chamadas dos menus ainda não portadas (declaradas em modules.h e engine/menu.h) ----
// Som, filmes, cartão de memória, a figura do controle, o estilo dos botões e as estatísticas.
uint32_t psiMultitapPresent() {
    pending_call("psiMultitapPresent");
    return 0;
}
// Combo e spin: nenhum arquivo de menu do disco os usa.
NF_STUB(Combo_Init, (M_COMBO*, NEW_CONTROL*, M_CONTROL*))
int32_t Combo_SendMessage(M_COMBO*, uint32_t, int32_t, int32_t) {
    pending_call("Combo_SendMessage");
    return 0;
}
NF_STUB(Combo_Update, (M_MANAGER*, M_PAGE*, M_COMBO*))
NF_STUB(Spin_Init, (M_SPIN*, NEW_CONTROL*, M_CONTROL*))
int32_t Spin_SendMessage(M_SPIN*, uint32_t, int32_t, int32_t) {
    pending_call("Spin_SendMessage");
    return 0;
}
NF_STUB(Spin_Update, (M_MANAGER*, M_PAGE*, M_SPIN*))

// ---- Chamadas do HUD e da missão ainda não portadas (declaradas em modules.h) ----
char* MP_GetPlayerScore(uint16_t) {
    pending_call("MP_GetPlayerScore");
    return nullptr;
}
char* MP_GetTeamScore(uint16_t) {
    pending_call("MP_GetTeamScore");
    return nullptr;
}
uint32_t MP_GetRadarObjects(obj_tag*, RADAROBJ** list) {
    pending_call("MP_GetRadarObjects");
    *list = nullptr;
    return 0;
}
uint32_t MP_HasEsponage(obj_tag*) {
    pending_call("MP_HasEsponage");
    return 0;
}
uint32_t MP_HasTeamFlag(obj_tag*, uint16_t) {
    pending_call("MP_HasTeamFlag");
    return 0;
}
uint32_t MP_ObjTeamHasGEObj(obj_tag*, uint32_t) {
    pending_call("MP_ObjTeamHasGEObj");
    return 0;
}
int32_t MP_getUplinkStatus(int16_t) {
    pending_call("MP_getUplinkStatus");
    return -1;
}
// Globais de módulos ainda não portados que o HUD lê.
uint32_t switch_SHOW_COORDS = 0;  // depuração: coordenadas no HUD
uint32_t RealFrameRate = 0;       // quadros por segundo medidos (depuração)

uint8_t GlobalVars[24];

std::vector<PendingPlacement> g_pending_placements;
bool g_record_placements = false;
std::vector<PendingPlacement> g_all_placements;

void pending_create(const char* name, const _VECTOR& pos, const _VECTOR& rot, celglist_tag* glist, uint32_t flags,
                    const level_tag* level) {
    pending_call(name);
    PendingPlacement pl{};
    pl.create = name;
    pl.type = flags;
    pl.pos = pos;
    if (level != nullptr) std::memcpy(pl.params, level->params, sizeof(pl.params));
    g_pending_placements.push_back(pl);
    if (glist == nullptr) return;
    ProvisionalObject obj{};
    View_RotTransMatrix(&rot, &pos, &obj.world);
    obj.world.m[3][3] = 1.0f;
    obj.glist = glist;
    obj.flags = flags;
    obj.create = name;
    g_provisional_objects.push_back(obj);
}

// ---- portadas ----

// [decomp] 0x0018bf60 MP_DebugDraw
void MP_DebugDraw() {}

// [decomp] 0x00202750 Menu_IsDrivingLevel
// Os pacotes 09000001-09000003 e 09000005-09000006 são do DRIVING.ELF.
uint32_t Menu_IsDrivingLevel(uint32_t level) {
    return (0x9000000 < level && (level < 0x9000004 || (level < 0x9000007 && 0x9000004 < level))) ? 1 : 0;
}

// [decomp] 0x001c6390 AnimCacheInit
void AnimCacheInit() {}

// [decomp] 0x001c6398 AnimCacheEnable
void AnimCacheEnable() {}

// [decomp] 0x001c63a0 AnimCacheDisable
void AnimCacheDisable() {}

// [decomp] 0x001c7998 AnimSeqGetHdrBuf
uint32_t AnimSeqGetHdrBuf(int8_t) { return 0; }


// [decomp] 0x001e77f0 View_Init
void View_Init() {}

// [decomp] 0x001de368 Sound_SetLogSfxFile
void Sound_SetLogSfxFile() {}

