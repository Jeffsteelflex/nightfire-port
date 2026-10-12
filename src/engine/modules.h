#pragma once
// Funções de outros módulos do jogo que o carregador chama. As marcadas como stub ainda não foram
// portadas: existem em modules_stubs.cpp (ou platform/psi_gfx.cpp), só registram a chamada
// (platform/pending.h) e não fazem nada.

#include <cstdint>
#include <vector>

#include "engine/anim.h"
#include "engine/map_types.h"
#include "engine/menu.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/player_stats.h"
#include "engine/player_traverse.h"
#include "engine/ssys.h"
#include "engine/turret.h"
#include "engine/copter.h"
#include "engine/simple_script.h"
#include "engine/script.h"
#include "platform/psi_sound.h"
#include "engine/sound.h"

struct obj_tag;
struct SCRIPTINFO;  // script de cena (engine/script.h)

// ---- animação (o módulo está em engine/anim.h) ----
void AnimCacheInit();                              // portada (vazia no original)
void AnimCacheEnable();                            // portada (vazia no original)
void AnimCacheDisable();                           // portada (vazia no original)

// ---- scripts e menus ----
// Script_Load e Script_FreeFromHashTable: portadas (script.cpp, declaradas em script.h).
void MenuManager_Load(uint32_t hash, uint8_t* data);  // portada

// ---- som, luz, partículas, IA ----
void Sound_Ready();                                // stub
void Sound_LoadMapSounds(uint8_t* data);           // stub
void Emitter_LoadDefs(uint8_t* data);              // portada (emitter.cpp)
void AIPath_Parse(uint8_t* block);                 // portada (aipath.cpp)

// ---- chamadas por ResetMap_Load e ResetMap_GameInit (stubs, exceto onde indicado) ----
void Sound_Kill();
void Sound_Init();
void Input_Ready();
// SP_Create: portada (script_player.cpp, declarada em script.h).
void AIPath_BindNodes();             // portada (aipath.cpp)
void ReadTuningVars();
void MP_Start();
void Drone_PostLoad_Init();  // portada (drone.cpp)
void MP_PostLoad_Init();
void Cameras_PostLoad();
uint32_t MenuManager_Create(uint32_t menu, uint32_t page, int16_t player, uint8_t index, uint32_t close_action,
                            int8_t pause_sounds, uint32_t status);  // portada (menu_manager.cpp)
void GS_PauseGame(int8_t pause);  // portada (menu_handlers.cpp)
void InitShards();                   // portada (break.cpp)
void Env_Reset();                    // portada (weather.cpp)
void Txt_LanguageInit(int8_t cold);  // portada (txt.cpp)
void Sprite_InitLists();            // portada (sprite.cpp)
void Text_Init(int8_t warm);         // portada (text.cpp)
void Drone_LevelReset();  // portada (drone.cpp)
void Door_Init();                    // portada (door.cpp)
void Copter_Init();  // portada (copter.cpp)
void Sensor_Init();                  // portada (script_player.cpp)
void Bullet_InitCasingLists();      // portada (bullet.cpp)
void AIPath_Init();                  // portada (aipath.cpp)
void Creature_Init();
void MP_Init();
void Camera_CreateCameras();        // portada (view.cpp)
void Drone_PreLoad_Init();  // portada (drone.cpp)
void MenuManager_Init();  // portada (menu_manager.cpp)
void Car_Init();
void SSys_Init();  // portada (sensor.cpp)
void Mission_Init(uint32_t level, uint16_t warm);  // portada (mission.cpp)
void Locks_Init();  // portada (ssys.cpp)
void View_Init();                    // portada (vazia)
void Sound_SetLogSfxFile();          // portada (vazia)
void Script_Init();                  // portada (script.cpp)
uint32_t AnimSeqGetHdrBuf(int8_t);   // portada (devolve 0)

// ---- objetos (chamadas pelo núcleo de objetos e pelos *_Create portados; stubs) ----
void MP_objectBeingDeleted(obj_tag* obj);
// Caixas orientadas das juntas de um objeto animado, para a colisão (Collide_Jointy). Stub: devolve
// nullptr e *count = 0, como o original faz com um objeto sem dados de animação.
struct COLLJOINT_tag;
COLLJOINT_tag* AnimGetCollData(obj_tag* obj, uint16_t* count);  // portada (drone_move.cpp)
// Chamadas pelo tratador de objetos (control_movement_object_handler, control_handle_cel_change).
void Drone_InitComms();  // portada (drone.cpp)
void MP_DebugDraw();                 // portada (vazia no original)
void Drone_FellOutMap(obj_tag* obj);
void Car_Deactivate(obj_tag* obj);
// Pedidos de fase (ResetMap_LevelToLoad).
uint32_t Menu_IsDrivingLevel(uint32_t level);  // portada
void MP_setLoadingSkins();
void SP_RemoveObj(obj_tag* obj, void* script);  // portada (script_player.cpp)
void InitDrops(uint8_t kind);  // portada (weather.cpp)
void psiCreateBreakableData(celglist_tag* glist);  // portada (platform/psi_shard.cpp)

// ---- só no port, até cada tipo de objeto ser portado ----
// Os *_Create ainda não portados passam por pending_create, que registra a chamada e guarda a
// entidade e a matriz (View_RotTransMatrix, como control_create_object faria) para o renderizador
// desenhar o objeto no lugar. Esvaziada a cada Mem_Init do ResetMap_Load.
struct ProvisionalObject {
    MATRIX world;
    celglist_tag* glist;
    uint32_t flags;
    const char* create;  // função original que criaria o objeto
};
extern std::vector<ProvisionalObject> g_provisional_objects;
// Só no port (a auditoria da campanha do nf_loader_check): cada posicionamento que passou por pending_create, também
// os sem modelo, com o tipo do posicionamento e os primeiros parâmetros (level_tag::params). Esvaziada junto com
// g_provisional_objects.
struct PendingPlacement {
    const char* create;
    uint32_t type;
    _VECTOR pos;
    uint32_t params[12];
};
extern std::vector<PendingPlacement> g_pending_placements;
// Com g_record_placements ligado (só a auditoria), parsemap_create_dynamic_objects também guarda todos os
// posicionamentos dinâmicos, portados ou não (create = nulo), em g_all_placements.
extern bool g_record_placements;
extern std::vector<PendingPlacement> g_all_placements;
void pending_create(const char* name, const _VECTOR& pos, const _VECTOR& rot, celglist_tag* glist, uint32_t flags,
                    const level_tag* level = nullptr);

// ---- jogador (chamadas pelo módulo Player e pelas câmeras; stubs) ----
struct BLData;
struct viewer_tag;
void Sound_StopAllWithId(uint32_t id);
void Sound_Stop(uint32_t handle);
void AnimDebugCreate();
// HUD_*: portadas (hud.cpp, declaradas também em engine/hud.h).
void HUD_Init(BLData* b, obj_tag* obj);
void HUD_Update(BLData* b, obj_tag* obj);
void HUD_Enable(void* hud, uint32_t pane, int8_t on, uint16_t a);
void HUD_Reset(BLData* b);
void* BodyGlow_Create(obj_tag* obj);
int32_t Mission_Status();  // portada (mission.cpp)
void Music_Event(uint32_t event, uint32_t a);
// Player_Climb, Player_SSGrapple, Player_Creep, Player_Wire, Player_Zipline, Player_InitGrapple,
// Player_GrappleSetRope, Player_CollWire, Player_CollLadder, Player_CollCreepWall, camera_tracking: portadas
// (engine/player_traverse.h).
void Env_Breath(_VECTOR* pos, _VECTOR* dir, cel_tag* cel, uint32_t created);  // portada (scenery.cpp)
void Env_WaterRipple(HITDATA_tag* hit);  // portada (effect.cpp)
void Player_LaserPointer(obj_tag* obj);  // portada (player_weapon.cpp)
void Player_SetWatch(obj_tag* obj);      // portada (player_weapon.cpp)
float Hurt_GetDamage(obj_tag* obj);    // portada (drone_damage.cpp)
uint16_t Hurt_GetType(obj_tag* obj);   // portada (drone_damage.cpp)
int32_t Control_Plr2Ind(obj_tag* obj);  // portada (control.cpp)
bool MP_IsAssasin(obj_tag* obj);
bool MP_IsTarget(obj_tag* obj);
bool MP_RegisterBulletHit(int16_t player, obj_tag* shooter, float health);  // portada (player.cpp)
// Morte do jogador: veículos, torretas, menu e multijogador ainda não portados (o HUD é hud.cpp).
void Car_PlayerHasDied(obj_tag* obj);
void GunImp_PlayerHasDied(obj_tag* obj);  // portada (gunimp.cpp)
void MP_PlayerKilled(obj_tag* obj);
void MP_ReSpawn(obj_tag* obj, int16_t player);
void HUD_DisableAll(BLData* b);
uint32_t HUD_State(void* hud, uint32_t pane);         // portada (hud.cpp)
uint32_t Menu_PlayerHasQuitOrRestarted();             // portada (menu_manager.cpp)
char* Txt_GetStringFromHeap(int8_t lock);             // portada (txt.cpp)
bool Drone_ModPlayerHitDamage(obj_tag* obj, HITDATA_tag* hit);  // portada (drone_weapon.cpp)
void SP_Activate(obj_tag* obj, obj_tag* by);  // portada (script_player.cpp)
void Trigger_Activate(obj_tag* obj, obj_tag* by);  // portada (trigger.cpp)
// SS_Activate: engine/simple_script.h.
void Car_Activate(obj_tag* obj, obj_tag* by);
void Monitor_Activate(obj_tag* obj, obj_tag* by);  // portada (sensor.cpp)
void GunImp_Activate(obj_tag* obj, obj_tag* by);  // portada (gunimp.cpp)
void Camera_Orbit(viewer_tag* v, void* orbit);
void camera_track_XZ_pos_behind(_VECTOR* pos, _VECTOR* rot, viewer_tag* v, float a, float b);
void camera_track_Y_pos(obj_tag* obj, viewer_tag* v, float a, float b);
void Camera_PosYRot(_VECTOR* pos, viewer_tag* v, float a);
void Camera_PosXRot(_VECTOR* pos, viewer_tag* v, float a);
void camera_freechase(obj_tag* obj, viewer_tag* v);
void camera_fixed(obj_tag* obj, viewer_tag* v);
void camera_snake(obj_tag* obj, viewer_tag* v);
void camera_chasecam(obj_tag* obj, viewer_tag* v);
extern float debug_cam_posy;

// ---- armas do jogador (chamadas por engine/player_weapon.cpp; stubs) ----
void MP_EquipPlayer(obj_tag* obj);
void Sound_ModAlertness(uint32_t handle, float level);
obj_tag* Particles_Create(int32_t kind, _VECTOR* pos, plane_equ_tag* plane);  // portada (scenery.cpp)
void MP_sendTeamBotMessage(int32_t a, int32_t b, int32_t weapon, int32_t c, obj_tag* obj);
void SSys_RamLoad(uint8_t* items);  // portada (sensor.cpp)
void Check_AutoAim(obj_tag* obj, obj_tag* target, float* best);  // portada (player_weapon.cpp)
void Check_Target(obj_tag* obj);  // portada (pickup.cpp)
uint32_t GT_DeployMiniGun(_VECTOR* pos, MATRIX* m, obj_tag* player);  // portada (turret.cpp)
void Draw_LaserBeam(BLData* b, _VECTOR* to);  // portada (player_weapon.cpp)
void Draw_TaserBeam(BLData* b, _VECTOR* to);  // portada (player_weapon.cpp)
void FractalThing(obj_tag** objs, int16_t from, int16_t to, _VECTOR* a, _VECTOR* b, uint8_t sqrt_mode);  // portada
// Globais de módulos ainda não portados (zeradas).
// NPCGlobals (lista dos drones e o resto da IA) está em engine/npc.h.
extern uint8_t SSysItems[40];       // engine/sensor.h
extern uint8_t GlobalVars[24];       // +0xd: algarismos do PDA; +0x14: o PDA mostra

// ---- Chamadas da destruição ainda não portadas (stubs em modules_stubs.cpp) ----
// Script player: Explode_Create toca o script do efeito da explosão (o fogo, o clarão e a fumaça vêm
// dele) e a explosão vive enquanto ele toca. Script_Load/Play/Update/IsPlaying/Free: portadas
// (script.cpp, declaradas em engine/script.h).
void MP_ApplyDamage(obj_tag* obj, float damage, obj_tag* from);

// ---- Os menus (engine/menu.h) ----
// MenuManager_*, Manager_*, Menu_*, Component_*, os controles e os scripts de menu: portados
// (menu.cpp, menu_manager.cpp, menu_controls.cpp, menu_script.cpp, menu_handlers.cpp).

// ---- Chamadas dos menus ainda não portadas (stubs em modules_stubs.cpp) ----
// Som, filmes, cartão de memória, controles e as estatísticas da missão.
// SFX*: portadas (platform/psi_sound.h, incluído abaixo).
uint32_t psiMultitapPresent();                                   // stub: 0
void PS2PollMemoryCard();                                         // portada (platform/psi_card.cpp)
void Menu_PlayMovie(uint32_t movie, int8_t skip, int8_t loop, int8_t sound, int8_t freeze, uint32_t counter);  // portada
void Menu_StopMovie(uint8_t force);                               // portada (menu_frontend.cpp)
void Menu_UpdateMovieCounter();                                   // portada (menu_frontend.cpp)
void Menu_UpdateMessageBox(uint8_t index, uint16_t mask, uint8_t no);  // portada (menu_codename.cpp)
// SFXMusicSetVolume/GetVolume, SFXSetVolume/GetVolume, SFXSetMode/GetMode: portadas (platform/psi_sound.h).
void Menu_DisplayControllerStyleList(uint8_t index, uint32_t style);  // portada (menu_controller_style.cpp)
void Menu_DisplayControllerStyle(uint8_t index, uint32_t style);      // portada (menu_controller_style.cpp)
// Input_RumbleStart/RumbleStop/ChangeControllerStyle: portadas (engine/input.h).
// ---- Chamadas do HUD e da missão ainda não portadas (stubs em modules_stubs.cpp) ----
// Assinaturas dos nomes com mangling do ELF. A camada 2D (Sprite_*, Txt_*, Text_*, Font_*) está em
// engine/sprite.h, txt.h, text.h e font.h.
struct RADAROBJ;
char* MP_GetPlayerScore(uint16_t player);                  // stub: nulo
char* MP_GetTeamScore(uint16_t team);                      // stub: nulo
uint32_t MP_GetRadarObjects(obj_tag* obj, RADAROBJ** list);  // stub: 0
uint32_t MP_HasEsponage(obj_tag* obj);                      // stub: 0
uint32_t MP_HasTeamFlag(obj_tag* obj, uint16_t team);       // stub: 0
uint32_t MP_ObjTeamHasGEObj(obj_tag* obj, uint32_t which);  // stub: 0
int32_t MP_getUplinkStatus(int16_t uplink);                 // stub: -1
void GS_PausePlayer(int8_t pause, uint16_t player);        // portada (menu_handlers.cpp)
uint32_t Menu_GetLevelIndex(uint32_t level);                // portada (menu_handlers.cpp)
