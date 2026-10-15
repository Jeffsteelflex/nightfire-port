// Os eventos (driving/game/events.h). A tabela kEventTable foi lida das criadoras e dos construtores do DRIVING.ELF (o
// tamanho do Event::operator_new, a tabela virtual e a origem de cada palavra) e das funções de resolução do 'el'; os
// destrutores portados estão em event_execute (as desmontagens em out/agent_drv_mission/ do checkout principal).
//
// Divergências (port), cada uma também no evento:
//  - o anel de 32 KB do EventManager vira a fila do port (trigger_manager().pending); as palavras que a criadora não
//    escreve (o PS2 deixa o que o anel tinha) ficam 0 e fora da conferência (PendingEvent::written);
//  - só os destrutores da missão, do HUD, dos gatilhos, do tráfego e dos objetos derrubáveis rodam; os outros números
//    (a IA, a câmera, os efeitos, as armas, o som, os trilhos) são contados em event_stats().skipped;
//  - as chamadas a sistemas não portados dentro dos destrutores portados (a câmera, o som, o AIElementController, o
//    RColorize) ficam anotadas (mission_trace);
//  - o carro do jogador sem o AIGroundVehicle (o port não dá um ao carro do jogador): o +0x134 dele que o EPlayerWin
//    zera conta como guarda.

#include "driving/game/events.h"
#include "driving/sound/audio_stream.h"

#include <cstring>

#include "driving/ai/element_controller.h"
#include "driving/game/ai_character.h"
#include "driving/game/bond_car.h"
#include "driving/game/feedback.h"
#include "driving/game/camera_events.h"
#include "driving/game/heli_section.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/scheduler.h"
#include "driving/game/smackable.h"
#include "driving/game/target_picker.h"
#include "driving/game/triggers.h"
#include "driving/game/explosion.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

constexpr uint8_t kD = kFromData, kY = kFromDynamic;

// O número, o nome, o tamanho, a tabela virtual da classe, a criadora, a resolução, as palavras dos dados resolvidas e
// a origem de cada palavra do objeto. Os 95 números das listas 'el' de Paris (88 nas listas dos gatilhos e das regras; o
// 11, o 12, o 26, o 27, o 32, o 129 e o 184 só em listas que nada dispara), mais o EEndMission, o ERestart e o 172 (a
// mesma criadora do 171, "menor que").
const EventInfo kEventTable[] = {
    {9, "EPlayEffect", 0x18, 0x370b70, 0x154308, 0x154358, 0x00e, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x8}, {0x14, kY, 0xc}}},
    {10, "EFireWeapon", 0x18, 0x3710b0, 0x14ed50, 0x14eda0, 0x002,
     {{0x4, kD, 0x4}, {0x8, kY, 0x8}, {0xc, kY, 0x0}, {0x10, kY, 0xc}, {0x14, kY, 0x34}}},
    {11, "EHideInstance", 0x8, 0x371038, 0x14f1c0, 0x14f1f0, 0x000, {{0x4, kY, 0x0}}},
    {12, "EShowInstance", 0x8, 0x370510, 0x159ae0, 0x159b10, 0x000, {{0x4, kY, 0x0}}},
    {19, "EEndMission", 0x4, 0x371158, 0x14c328, 0x14c350, 0x000, {}},
    {20, "EStreamEvent", 0x1c, 0x370318, 0x15d248, 0x15d290, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}, {0x18, kD, 0x18}}},
    {22, "EStartMission", 0xc, 0x3703c0, 0x15c920, 0x15c960, 0x000, {{0x4, kY, 0x30}, {0x8, kD, 0x4}}},
    {25, "EPowerUp", 0x1c, 0x370a80, 0x1555d0, 0x155638, 0x006,
     {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kY, 0x10}, {0x10, kY, 0x14}, {0x14, kY, 0x18}, {0x18, kY, 0x1c}}},
    {26, "ESpawnSimplePhysics", 0x1c, 0x370438, 0x15bb58, 0x15bba8, 0x004, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x38}, {0x14, kY, 0x3c}, {0x18, kY, 0x8}}},
    {27, "ESpawnSmackable", 0x18, 0x370420, 0x15bfb0, 0x15c000, 0x00a, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x8}, {0x14, kY, 0xc}}},
    {28, "ESpawnTraffic", 0xc, 0x370408, 0x15c3f0, 0x15c430, 0x000, {{0x4, kY, 0x30}, {0x8, kD, 0x4}}},
    {32, "EKillObject", 0xc, 0x370f30, 0x150a98, 0x150ad8, 0x002, {{0x4, kD, 0x4}, {0x8, kY, 0x8}}},
    {35, "EWakeupSmackable", 0x3c, 0x370210, 0x15e4b0, 0x15e548, 0x00e, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x44}, {0x2c, kY, 0x60}, {0x30, kY, 0x30}}},
    {31, "EMuzzleFlash", 0x10, 0x370ea0, 0x151378, 0x1513c0, 0x002, {{0x4, kD, 0x4}, {0x8, kY, 0x8}, {0xc, kY, 0xc}}},
    {33, "ESpawnExplosion", 0x50, 0x370498, 0x15ae90, 0x15af70, 0x142,
     {{0x4, kY, 0x10},  {0x8, kY, 0x14},  {0xc, kY, 0x18},  {0x10, kY, 0x1c}, {0x14, kY, 0x20},
      {0x18, kY, 0x24}, {0x1c, kY, 0x28}, {0x20, kY, 0x2c}, {0x24, kY, 0x8},  {0x28, kY, 0x38},
      {0x2c, kY, 0x3c}, {0x30, kD, 0x4},  {0x34, kD, 0x8},  {0x38, kD, 0xc},  {0x3c, kD, 0x10},
      {0x40, kD, 0x14}, {0x44, kD, 0x18}, {0x48, kD, 0x1c}, {0x4c, kD, 0x20}}},
    {36, "EPlaySystemAnim", 0x20, 0x370af8, 0x154bd0, 0x154c28, 0x018, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kY, 0x38}, {0x18, kY, 0x3c}, {0x1c, kY, 0x8}}},
    {40, "ECallStage", 0x8, 0x3713f8, 0x148db8, 0x148df0, 0x000, {{0x4, kD, 0x4}}},
    {41, "EChangeStage", 0x8, 0x371350, 0x1498a0, 0x1498d8, 0x000, {{0x4, kD, 0x4}}},
    {42, "EControlToCPU", 0x4, 0x3712d8, 0x14acd0, 0x14acf8, 0x000, {}},
    {43, "EControlToPlayer", 0x4, 0x3712c0, 0x14ae10, 0x14ae38, 0x000, {}},
    {44, "EDisplayText", 0xc, 0x371200, 0x14b968, 0x14b9a0, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {45, "EPlayerLose", 0x14, 0x370ae0, 0x155078, 0x1550b8, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {46, "EPlayerWin", 0xc, 0x370a98, 0x155418, 0x155450, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {47, "ESetTimer", 0x10, 0x370588, 0x159270, 0x1592b0, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {48, "EStartTimer", 0xc, 0x370390, 0x15cb10, 0x15cb48, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {49, "EStopTimer", 0x8, 0x370330, 0x15cf90, 0x15cfc8, 0x000, {{0x4, kD, 0x4}}},
    {53, "EDisableTrigger", 0x8, 0x371230, 0x14b7e8, 0x14b820, 0x002, {{0x4, kD, 0x4}}},
    {54, "EEnableTrigger", 0x8, 0x3711b8, 0x14bd30, 0x14bd68, 0x002, {{0x4, kD, 0x4}}},
    {56, "EPowerUpAmmo", 0xc, 0x370a68, 0x1557e8, 0x155820, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {60, "EPowerUpMultiDamage", 0xc, 0x370a08, 0x155ac8, 0x155b00, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {63, "EPlayCameraAnim", 0x18, 0x370bb8, 0x153910, 0x153958, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}}},
    {65, "EAICommand", 0x20, 0x371728, 0x146900, 0x146958, 0x01e, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kY, 0x30}, {0x18, kY, 0x38}, {0x1c, kY, 0x3c}}},
    {66, "EActivateAIElement", 0x18, 0x371638, 0x1478f0, 0x147940, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kY, 0x30}, {0x10, kY, 0x38}, {0x14, kY, 0x3c}}},
    {67, "EDeactivateAIElement", 0x8, 0x371260, 0x14b5b0, 0x14b5e8, 0x002, {{0x4, kD, 0x4}}},
    {68, "EEndCameraAnim", 0x14, 0x3711a0, 0x14bfe8, 0x14c028, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {69, "ESetObjective", 0x14, 0x370678, 0x158728, 0x158768, 0x00e, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {73, "ERestart", 0x4, 0x370ac8, 0x1568b0, 0x000000, 0x000, {}},
    {76, "ESetAutoDriveCamOff", 0x4, 0x3707f8, 0x1574f8, 0x157520, 0x000, {}},
    {77, "ESetAutoDriveCamOn", 0xc, 0x3707c8, 0x1577d8, 0x157810, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {78, "ESetVideo", 0xc, 0x370570, 0x1595a0, 0x1595d8, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {97, "EPlayCarCameraAnim", 0x1c, 0x370b88, 0x153ee0, 0x153f30, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kY, 0x38}, {0x18, kY, 0x3c}}},
    {103, "ELetterBoxOff", 0x8, 0x370ee8, 0x150d70, 0x150da8, 0x000, {{0x4, kD, 0x4}}},
    {104, "ELetterBoxOn", 0x8, 0x370ed0, 0x150e58, 0x150e90, 0x000, {{0x4, kD, 0x4}}},
    {108, "EAutoDriveSpeedChange", 0x8, 0x3714d0, 0x148378, 0x1483b0, 0x000, {{0x4, kD, 0x4}}},
    {112, "ESetSimRate", 0x8, 0x370600, 0x158d90, 0x158dc8, 0x000, {{0x4, kD, 0x4}}},
    {116, "EPathSetDesiredThrottleStatic", 0x10, 0x370c90, 0x152d28, 0x152d68, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {125, "ETwoWheelsOff", 0x4, 0x370258, 0x15dbf0, 0x15dc18, 0x000, {}},
    {129, "EStopEffect", 0x20, 0x370378, 0x15ccb0, 0x15cd08, 0x00e, {{0x4, kD, 0x4}, {0x8, kY, 0x8}, {0xc, kD, 0x8}, {0x10, kD, 0xc}, {0x14, kY, 0x38}, {0x18, kY, 0x3c}, {0x1c, kD, 0x10}}},
    {130, "ESetCollisionGeometry", 0x10, 0x370768, 0x157c70, 0x157cb0, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kY, 0x8}}},
    {131, "EAddScoreAction", 0x8, 0x371548, 0x147c88, 0x147cc0, 0x000, {{0x4, kD, 0x4}}},
    {140, "EResetPlayerCarPos", 0xc, 0x370918, 0x156638, 0x156670, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {144, "EAIElementFireOff", 0x8, 0x371710, 0x146ad0, 0x146b08, 0x002, {{0x4, kD, 0x4}}},
    {145, "EAIElementFireOn", 0x8, 0x3716f8, 0x146c28, 0x146c60, 0x002, {{0x4, kD, 0x4}}},
    {149, "EDisablePowerUp", 0xc, 0x371248, 0x14b6e0, 0x14b718, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {150, "EEnablePowerUp", 0xc, 0x3711d0, 0x14bc28, 0x14bc60, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {153, "EPathSetTimeStatic", 0x10, 0x370c00, 0x1533d0, 0x153410, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {154, "ESpawnExplosionStatic", 0x20, 0x370480, 0x15b188, 0x15b1d8, 0x086, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}, {0x18, kD, 0x18}, {0x1c, kD, 0x1c}}},
    {159, "EAIElementSetAccuracy", 0xc, 0x3716e0, 0x146d70, 0x146da8, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {161, "EKillAIElement", 0xc, 0x370f48, 0x150948, 0x150980, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {162, "EDamagePlayer", 0xc, 0x371278, 0x14b4b8, 0x14b4f0, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {164, "EPlaySoundPos", 0x1c, 0x370b10, 0x1549f0, 0x154a38, 0x00e, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}, {0x18, kD, 0x18}}},
    {171, "EFireTriggerSpeedCondition", 0x18, 0x3710c8, 0x14c9c0, 0x14ca10, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x38}, {0x14, kY, 0x3c}}},
    {172, "EFireTriggerSpeedCondition", 0x18, 0x3710c8, 0x14c9c0, 0x14ca10, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kY, 0x38}, {0x14, kY, 0x3c}}},
    {173, "ESuppressEffect", 0x20, 0x370300, 0x15d450, 0x15d4a8, 0x00e, {{0x4, kD, 0x4}, {0x8, kY, 0x8}, {0xc, kD, 0x8}, {0x10, kD, 0xc}, {0x14, kY, 0x38}, {0x18, kY, 0x3c}, {0x1c, kD, 0x10}}},
    {184, "E007Logo", 0x4, 0x371740, 0x145f98, 0x145fc0, 0x000, {}},
    {192, "ESetSpecialWeapon", 0x10, 0x3705e8, 0x158e60, 0x158ea0, 0x004, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {193, "EButtonMsgOff", 0x4, 0x371440, 0x148b40, 0x148b68, 0x000, {}},
    {194, "EButtonMsgOn", 0x18, 0x371428, 0x148c10, 0x148c58, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}}},
    {196, "ENuclearBlast", 0xc, 0x370e88, 0x1514b0, 0x1514e8, 0x004, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {200, "ESetMagicCounter", 0xc, 0x3706f0, 0x158168, 0x1581a0, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {202, "EControlToSpline", 0xc, 0x3712a8, 0x14af70, 0x14afa8, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {203, "EFireRandomTrigger", 0x2c, 0x3710e0, 0x14c7b8, 0x14c818, 0x0aa, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}, {0x18, kD, 0x18}, {0x1c, kD, 0x1c}, {0x20, kD, 0x20}, {0x24, kY, 0x38}, {0x28, kY, 0x3c}}},
    {204, "EPlayCameraSpline", 0x14, 0x370ba0, 0x153bb0, 0x153bf0, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {207, "EChangeCarCameraView", 0x14, 0x371380, 0x149728, 0x149768, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {209, "ESetNextAISpline", 0xc, 0x3706a8, 0x158450, 0x158488, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {215, "EPathChangeThrottleStatic", 0x18, 0x370d80, 0x151e68, 0x151eb0, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}}},
    {219, "EEndCarStop", 0x4, 0x371188, 0x14c0c8, 0x14c0f0, 0x000, {}},
    {220, "EForceCarStop", 0x8, 0x371098, 0x14ee68, 0x14eea0, 0x000, {{0x4, kD, 0x4}}},
    {225, "EObjectiveAdd", 0x14, 0x370e70, 0x1515b0, 0x1515f0, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}}},
    {228, "EObjectivePass", 0xc, 0x370e10, 0x151960, 0x151998, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {229, "EObjectiveSetCurrent", 0xc, 0x370df8, 0x151a50, 0x151a88, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {232, "ESetRecSpeedOffset", 0xc, 0x370660, 0x1588c0, 0x1588f8, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {233, "ESetAutoDriveCamOffMovie", 0x4, 0x3707e0, 0x157698, 0x1576c0, 0x000, {}},
    {234, "ESetAutoDriveCamOnMovie", 0xc, 0x3707b0, 0x157958, 0x157990, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {239, "EEndControlToSpline", 0x8, 0x371170, 0x14c1e8, 0x14c220, 0x002, {{0x4, kD, 0x4}}},
    {240, "ECustomTransition", 0x8, 0x371290, 0x14b360, 0x14b398, 0x002, {{0x4, kD, 0x4}}},
    {241, "ESetAudioMix", 0x10, 0x370810, 0x1573e0, 0x157420, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {242, "ECameraLockOn", 0x28, 0x3713e0, 0x149040, 0x149098, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}, {0x10, kD, 0x10}, {0x14, kD, 0x14}, {0x18, kD, 0x18}, {0x1c, kD, 0x1c}, {0x20, kD, 0x20}, {0x24, kD, 0x24}}},
    {246, "ECameraZoom", 0xc, 0x371398, 0x1494c0, 0x1494f8, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {249, "EGadgetOn", 0xc, 0x371050, 0x14f110, 0x14f148, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {250, "EPlayGiottoEffect", 0xc, 0x370b58, 0x1544c0, 0x1544f8, 0x006, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {251, "ECameraShake", 0x10, 0x3713c8, 0x1491e0, 0x149220, 0x000, {{0x4, kD, 0x4}, {0x8, kD, 0x8}, {0xc, kD, 0xc}}},
    {252, "ESwitchEffectOff", 0xc, 0x3702e8, 0x15d5d0, 0x15d608, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {253, "ESwitchEffectOn", 0xc, 0x3702d0, 0x15d6e8, 0x15d720, 0x002, {{0x4, kD, 0x4}, {0x8, kD, 0x8}}},
    {257, "ESetCullDistanceFactor", 0x8, 0x370738, 0x157f20, 0x157f58, 0x000, {{0x4, kD, 0x4}}},
    {266, "EResetAudioMix", 0x8, 0x370948, 0x156338, 0x156370, 0x000, {{0x4, kD, 0x4}}},
    {269, "EUnloadWeapon", 0x8, 0x370228, 0x15dd60, 0x15dd98, 0x000, {{0x4, kD, 0x4}}},
    {273, "EReleaseStream", 0x4, 0x370978, 0x156170, 0x156198, 0x000, {}},
    {275, "EAwardHitToPlayer", 0x8, 0x3714a0, 0x1485a8, 0x1485e0, 0x000, {{0x4, kD, 0x4}}},
    {276, "EAwardKillToPlayer", 0x8, 0x371488, 0x148678, 0x1486b0, 0x000, {{0x4, kD, 0x4}}},
    {279, "ESetReflectiveObject", 0x8, 0x370630, 0x158b08, 0x158b40, 0x002, {{0x4, kD, 0x4}}},
};

EventStats g_stats;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }

uint32_t rd(const uint8_t* p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

void call(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    mission_note_call(fn, a0, a1, a2, a3);
}

BondCar* player_or_guard() {
    BondCar* c = mission_player_car();
    if (!c) ++g_stats.guards;
    return c;
}

// O construtor (os que agem já na criação).
void constructor_effects(const PendingEvent& e) {
    if (!mission_exists()) return;  // o PS2 sempre tem o gerente; sem ele (as conferências dos gatilhos) nada
    SMissionManagerData& m = mission_manager();
    switch (e.type) {
        case kEventChangeStage:  // [driving] 0x00149828 EChangeStage::EChangeStage(int)
            SMissionManager_ChangeStage(m, int32_t(e.obj[1]));
            break;
        case kEventSetTimer: {  // [driving] 0x00159178 ESetTimer::ESetTimer(float, int, int)
            // O valor em passos: (int)(segundos x taxa + 1).
            if (SMissionTimer* t = mission_timer(m, int32_t(e.obj[1]))) {
                const float v = ee::add(ee::mul(flt(e.obj[2]), ee::cvt_s(simulation().frame_rate)), 1.0f);
                SMissionTimer_Reset(*t, ee::cvt_w(v));
            }
            break;
        }
        case kEventStartTimer:  // [driving] 0x0015ca58 EStartTimer::EStartTimer(int, int)
            if (SMissionTimer* t = mission_timer(m, int32_t(e.obj[1]))) {
                SMissionTimer_Start(*t, int32_t(e.obj[2]));
                if (e.obj[1] == 1) m.timers[1].ticking = 1;  // SMissionTimer::SetTicking
            }
            break;
        case kEventStopTimer:  // [driving] 0x0015cf10 EStopTimer::EStopTimer(int)
            if (SMissionTimer* t = mission_timer(m, int32_t(e.obj[1]))) SMissionTimer_Stop(*t);
            break;
        default:
            return;
    }
    ++g_stats.constructor_effects;
}

// [driving] 0x0015c738 EStartMission::~EStartMission / 0x00156460 EResetPlayerCarPos::~EResetPlayerCarPos: o carro do
// jogador no gatilho, de frente para o forward dele (Simulation::SetStartConditions 0x1fee68: o corpo na altura do
// chão + 1 pela matriz do Util_GenerateMatrix; PBondCar::ResetCar), com o momento da velocidade do evento.
void place_player(const PendingEvent& e) {
    const WTrigger* t = nf::p32_ptr<const WTrigger>(e.obj[1]);
    BondCar* car = player_or_guard();
    if (!t || !car) {
        if (!t) ++g_stats.guards;
        return;
    }
    float pos[4] = {t->position[0], t->position[1], t->position[2], 1.0f};
    const float dir[4] = {t->forward[0], t->forward[1], t->forward[2], 1.0f};
    RigidBody* body = car->body();
    {
        // Simulation::SetStartConditions (uma cópia de pos com o y na altura do chão + 1).
        float p[3] = {pos[0], pos[1], pos[2]};
        float h = 0.0f;
        TrackWorld* tw = world() ? world()->track_world() : nullptr;
        if (!tw || !tw->world_height_at_point(p, h)) h = 0.0f;
        p[1] = ee::add(h, 1.0f);
        float mtx[4][4];
        Util_GenerateMatrix(mtx, dir);
        RigidBody_ResetObject(body, mtx, p);
    }
    PBondCar_ResetCar(*car, pos, dir);  // a entrada +0x164 da tabela virtual
    // O momento: a frente (o dir do evento) normalizada pela VU0 vezes speed x massa.
    const float k = ee::mul(flt(e.obj[2]), body->mass);
    float v[3] = {dir[0], dir[1], dir[2]};
    uint32_t acc = ps2::mul(bits(v[0]), bits(v[0]));
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(bits(v[1]), bits(v[1])));
    acc = ps2::madd(acc, 0x3f800000u, ps2::mul(bits(v[2]), bits(v[2])));
    const uint32_t q = ps2::rsqrt(0x3f800000u, acc);
    for (int i = 0; i < 3; ++i) body->momentum[i] = flt(ps2::mul(ps2::mul(bits(v[i]), q), bits(k)));
}

// Os controles do carro do jogador que o EPlayerWin e o EPlayerLose mexem (as entradas da tabela virtual do PBondCar:
// +0xf4 SetCarControlGas, +0xfc Brake, +0x104 HandBrake, +0x114 FirePrimary, +0xd4 Steering, +0xdc SteeringVertical,
// +0x11c LockCarControlForever 0x193060: os controles copiados para os travados e o prazo 0x7fffffff).
void lock_controls(BondCar& car) {
    std::memcpy(&car.d.locked_control, &car.d.control, sizeof car.d.control);
    car.d.control_lock_timer = 0x7fffffff;
}

// [driving] 0x001551b8 EPlayerWin::~EPlayerWin
void player_win(const PendingEvent& e) {
    SMissionManagerData& m = mission_manager();
    if (m.state == 4) return;
    call(0x2ec968);  // ASoundManager::SetMissionOver
    audio_set_mission_over();  // (driving/sound/audio_stream.h)
    SMissionManager_Win(m);
    call(0x12c4c0);  // AIElementController::EveryoneChill
    if (ai_controller()) AIElementController_EveryoneChill();
    BondCar* car = player_or_guard();
    if (car) {
        // GetAIGroundVehiclePtr (+0x5c): o +0x134 do motorista zerado; o valor (0) vai ao acelerador e ao freio.
        if (car->d.ai_ground_vehicle == 0) ++g_stats.guards;
        else std::memset(nf::p32_ptr<uint8_t>(car->d.ai_ground_vehicle) + 0x134, 0, 4);
        car->d.control.gas = 0.0f;
        car->d.control.brake = 0.0f;
        car->d.control.fire_primary = 0;
        lock_controls(*car);
        if (car->physics()->is_sub == 0) car->d.control.hand_brake = 1;
        else if (m.missile != 0) ++g_stats.guards;  // o míssil +0x70c (o port não tem mísseis)
    }
    trigger_manager().active = 0;
    IFeedback_Pause();
    if (m.auto_drive != 0) {
        call(0x1bc400);  // RPlayerCamera::CameraLockOn(0)
        camera_events_lock_off();
    } else if (e.obj[1] == 0) {
        call(0x1bb9e8);  // RPlayerCamera::SetPauseCamera
    }
    if (hud_exists()) GHud_SetMissionPassed(hud());
    uber_bond().word(0x978) = 0;
}

// [driving] 0x00154cb0 EPlayerLose::~EPlayerLose
void player_lose(const PendingEvent& e) {
    SMissionManagerData& m = mission_manager();
    if (m.state == 3) return;
    UberBondBlock& u = uber_bond();
    if (u.word(0x628) != 0) {  // as tentativas ("Groundhog restarting level"): um ERestart (não portado)
        u.word(0x630) += 1;
        ++mission_stats().restarts;
        event_create_simple(kEventRestart);
        return;
    }
    SMissionManager_Lose(m);
    call(0x12c4c0);  // AIElementController::EveryoneChill
    if (ai_controller()) AIElementController_EveryoneChill();
    BondCar* car = player_or_guard();
    if (car) {
        car->d.control.gas = 0.0f;
        car->d.control.brake = 0.0f;
        if (car->physics()->is_sub == 0) {
            car->d.control.hand_brake = 1;
        } else {
            car->d.control.steering = 0.0f;
            car->d.control.steering_vertical = 0.0f;
            if (m.missile != 0) ++g_stats.guards;
        }
        car->d.control.fire_primary = 0;
        lock_controls(*car);
    }
    IFeedback_Pause();
    trigger_manager().active = 0;
    call(0x2ec968);  // ASoundManager::SetMissionOver
    audio_set_mission_over();  // (driving/sound/audio_stream.h)
    SMissionManager_SetDriveMissile(m, 0);
    if (m.auto_drive != 0) {
        if (const uint32_t hands = hands_globals().hands) AICharacterHands_HideHands(hands);
        call(0x1bc400);  // RPlayerCamera::CameraLockOn(0)
        camera_events_lock_off();
    } else if (e.obj[1] == 0) {
        call(0x1bb9e8);  // RPlayerCamera::SetPauseCamera
    }
    if (hud_exists()) {
        if (e.obj[4] != 0 || (m.auto_drive != 0 && ee::le(SMissionManager_GetPlayerHealth(m, false), 0.0f))) {
            call(0x2f2dc8);  // AMix::Get("0:Fader0"): o volume a 0,5
            call(0x2f4b10, 7, 0x36f8c0, 0x36f8d0, 2);  // AMenuSound::Trigger(7, "SFX_BondDeath", "Speech", 2)
            GHud_TriggerBloodCurtain(hud());
        }
        GHud_SetMissionFailed(hud(), e.obj[3] != 0);
    }
    u.word(0x978) = ee::le(SMissionManager_GetPlayerHealth(m, false), 0.0f) ? 4 : 1;
}

// [driving] 0x00158648 ESetObjective::~ESetObjective: o comentário e o alvo (o ponto, a instância ou o elemento).
void set_objective(const PendingEvent& e) {
    SMissionManagerData& m = mission_manager();
    SMissionManager_SetObjectiveComment(m, 0, e.obj[4]);
    if (e.obj[1] != 0) {
        SMissionManager_SetObjectivePos(m, 0, e.obj[1]);
    } else if (e.obj[2] != 0) {
        SMissionManager_SetObjectiveInstance(m, 0, e.obj[2]);
    } else if (e.obj[3] != 0) {
        SMissionManager_SetObjectiveElement(m, 0, e.obj[3]);
    } else {
        SMissionManager_ClearObjective(m, 0);
        SMissionManager_SetObjectiveComment(m, 0, 0);
    }
}

// [driving] 0x0014bdb8 EEndCameraAnim::~EEndCameraAnim: os quadros de volta (inteiros sem sinal convertidos como o
// PS2: acima de 1, x taxa / 60) e o RPlayerCamera::EndCameraAnim (anotado); o fim do modo de cinema 3.
void end_camera_anim(const PendingEvent& e) {
    auto u2f = [](uint32_t v) {
        if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
        const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
        return ee::add(f, f);
    };
    const int32_t fr = simulation().frame_rate;
    uint32_t a = e.obj[2];
    if (ee::lt(1.0f, u2f(a))) a = uint32_t(ee::cvt_w(ee::div(u2f(a * uint32_t(fr)), 60.0f)));
    uint32_t b = e.obj[1];
    if (ee::lt(1.0f, u2f(b))) b = uint32_t(ee::cvt_w(ee::div(u2f(b * uint32_t(fr)), 60.0f)));
    else b = 1;
    call(0x1bb098, a & 0xffff, b, e.obj[3] != 0 ? 1u : 0u);  // RPlayerCamera::EndCameraAnim
    camera_events_end_anim(uint16_t(a & 0xffff), b, e.obj[3] != 0 ? 1u : 0u, int16_t(e.obj[4]));
    SMissionManager_ClearCinematicMode(mission_manager(), 3);
}

}  // namespace

const EventInfo* event_info(int32_t type) {
    for (const EventInfo& i : kEventTable)
        if (i.type == type) return &i;
    return nullptr;
}

EventStats& event_stats() { return g_stats; }

// A criadora de um evento (a tabela 0x36fda0) e o construtor: a imagem do objeto e os efeitos da construção.
bool event_has_creator(int32_t type) {
    // As entradas nulas da tabela 0x36fda0.
    static const int16_t kNone[] = {0,   3,   7,   14,  15,  18,  21,  30,  34,  37,  50,  51,  52,  64,  71,  80,
                                    83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  102, 124, 146,
                                    147, 195, 205, 206, 208, 211, 236, 237, 238, 243, 262, 274, 280};
    if (type < 0 || type >= 281) return false;
    for (int16_t n : kNone)
        if (n == type) return false;
    return true;
}

void event_create(int32_t type, const uint8_t* data) {
    TriggerManagerGlobals& g = trigger_manager();
    const EventDynamicData& d = g.event_data;
    PendingEvent e{type, d.trigger, 0.0f, nf::p32_u(data), d.flag, d.index};
    if (type == kEventSpawnTraffic) std::memcpy(&e.speed, data + 4, 4);
    if (const EventInfo* info = event_info(type)) {
        e.size = info->size;
        e.obj[0] = info->vtable;
        e.written = 1;
        const uint8_t* dyn = reinterpret_cast<const uint8_t*>(&d);
        for (const EventField& f : info->fields) {
            if (f.source == 0) break;
            const uint32_t v = f.source == kFromData ? rd(data, f.from) : rd(dyn, f.from);
            e.obj[f.offset / 4] = v;
            e.written |= 1u << (f.offset / 4);
        }
    } else {
        ++g_stats.guards;  // um número que o port não conhece (Paris usa só os da tabela)
    }
    ++g_stats.created[type];
    g.pending.push_back(e);
    constructor_effects(g.pending.back());
}

void event_create_simple(int32_t type) {
    PendingEvent e{type, 0, 0.0f, 0, 0, 0};
    if (const EventInfo* info = event_info(type)) {
        e.size = info->size;
        e.obj[0] = info->vtable;
        e.written = 1;
    }
    ++g_stats.created[type];
    trigger_manager().pending.push_back(e);
}

void event_execute(const PendingEvent& e) {
    SMissionManagerData& m = mission_manager();
    const bool mission = mission_exists();
    bool ran = true;
    switch (e.type) {
        // Os do tráfego, dos gatilhos e dos objetos derrubáveis (driving/game/triggers.h, smackable.h).
        case kEventSpawnTraffic:
            if (const WTrigger* t = nf::p32_ptr<const WTrigger>(e.trigger)) ESpawnTraffic_Execute(*t, e.speed);
            break;
        case kEventFireRandomTrigger:
            EFireRandomTrigger_Execute(FireRandomTrigger_FromData(nf::p32_ptr<const uint8_t>(e.data), e.flag, e.index));
            break;
        case kEventWakeupSmackable:
            // O raio: o EventDynamicData +0x44, sempre 0 no port (sem RayShell).
            EWakeupSmackable_Execute(
                WakeupSmackable_FromData(nf::p32_ptr<const uint8_t>(e.data), e.trigger, 0, e.flag, e.index));
            break;
        case kEventFireTriggerSpeed:
        case kEventFireTriggerSpeedLess:
            EFireTriggerSpeedCondition_Execute(
                FireTriggerSpeed_FromData(nf::p32_ptr<const uint8_t>(e.data), e.flag, e.index));
            break;
        // [driving] 0x0014b790 EDisableTrigger::~EDisableTrigger / 0x0014bcd8 EEnableTrigger::~EEnableTrigger
        case kEventDisableTrigger:
            if (WTrigger* t = nf::p32_ptr<WTrigger>(e.obj[1])) t->flags = uint16_t(t->flags & ~kTrigEnabled);
            break;
        case kEventEnableTrigger:
            if (WTrigger* t = nf::p32_ptr<WTrigger>(e.obj[1])) t->flags = uint16_t(t->flags | kTrigEnabled);
            break;
        default:
            // Os da IA dos elementos (driving/ai/element_controller.h: EAICommand, EActivateAIElement...).
            ran = ai_event_execute(e);
            break;
    }
    if (!ran && mission) {
        ran = true;
        GHudData& h = hud();
        BondCar* car = mission_player_car();
        switch (e.type) {
            case kEventStartMission:
                place_player(e);
                mission_env().io_updating = 1;  // IOModule +4
                break;
            case kEventResetPlayerCarPos:
                place_player(e);
                break;
            // [driving] 0x00148d50 ECallStage::~ECallStage (SMissionManager::CallStage)
            case kEventCallStage:
                SMissionManager_CallStage(m, int32_t(e.obj[1]));
                break;
            // Os destrutores vazios (o trabalho foi no construtor).
            case kEventChangeStage:
            case kEventStartTimer:
            case kEventStopTimer:
                break;
            // [driving] 0x001591f8 ESetTimer::~ESetTimer: o cronômetro no HUD
            case kEventSetTimer:
                if (e.obj[3] != 0) {
                    if (hud_exists()) GHud_TriggerTimerOn(h, int32_t(e.obj[1]));
                    else ++g_stats.guards;
                }
                break;
            // [driving] 0x0014b8f8 EDisplayText::~EDisplayText
            case kEventDisplayText:
                if (hud_exists()) GHud_TriggerMissionMsg(h, flt(e.obj[2]), int32_t(e.obj[1]));
                else ++g_stats.guards;
                break;
            case kEventPlayerWin:
                player_win(e);
                break;
            case kEventPlayerLose:
                player_lose(e);
                break;
            case kEventSetObjective:
                set_objective(e);
                break;
            // [driving] 0x00151538 EObjectiveAdd / 0x001518d0 EObjectivePass / 0x001519c0 EObjectiveSetCurrent
            case kEventObjectiveAdd:
                SMissionManager_AddObjective(m, int32_t(e.obj[1]), int32_t(e.obj[2]), int32_t(e.obj[3]),
                                             int32_t(e.obj[4]));
                break;
            case kEventObjectivePass:
                SMissionManager_PassObjective(m, int32_t(e.obj[1]));
                if (e.obj[2] != 0 && hud_exists()) GHud_TriggerObjectiveMsg(h, 2, int32_t(e.obj[1]), 0);
                break;
            case kEventObjectiveSetCurrent:
                SMissionManager_SetCurrentObjective(m, int32_t(e.obj[1]));
                if (e.obj[2] != 0 && hud_exists()) GHud_TriggerObjectiveMsg(h, 1, int32_t(e.obj[1]), 0);
                break;
            // [driving] 0x00148b98 EButtonMsgOn / 0x00148ad8 EButtonMsgOff
            case kEventButtonMsgOn:
                if (hud_exists()) GHud_TriggerButtonMsg(h, flt(e.obj[2]), int32_t(e.obj[1]), int32_t(e.obj[3]));
                break;
            case kEventButtonMsgOff:
                if (hud_exists()) GHud_KillButtonMessage(h);
                break;
            // [driving] 0x00150dc8 ELetterBoxOn (o RColorize::DisableMotionBlur anotado) / 0x00150ce8 ELetterBoxOff
            case kEventLetterBoxOn:
                feedback_state().enabled = 0;  // 0x32c460
                if (hud_exists()) GHud_LetterBoxOn(h, e.obj[1] != 0);
                SMissionManager_SetCinematicMode(m, 4);
                call(0x1d9020);  // RColorize::DisableMotionBlur
                break;
            case kEventLetterBoxOff:
                if (hud_exists()) GHud_LetterBoxOff(h, e.obj[1] != 0);
                SMissionManager_ClearCinematicMode(m, 7);
                feedback_state().enabled = 1;
                break;
            case kEventEndCameraAnim:
                end_camera_anim(e);
                break;
            // [driving] 0x00158d28 ESetSimRate::~ESetSimRate: o timeScale do escalonador (no máximo 4)
            case kEventSetSimRate: {
                const float r = flt(e.obj[1]);
                if (Scheduler* s = mission_env().scheduler) s->time_scale = ee::lt(4.0f, r) ? 4.0f : r;
                break;
            }
            // [driving] 0x001580f8 ESetMagicCounter
            case kEventSetMagicCounter:
                SMissionManager_SetMagicCounter(m, int32_t(e.obj[1]), int32_t(e.obj[2]));
                break;
            // [driving] 0x00147b80 EAddScoreAction (o "stinger" do som anotado)
            case kEventAddScoreAction:
                SMissionManager_AddScore(m, 1, int32_t(e.obj[1]));
                call(0x2f9798);  // AFader::QueuePrimary / AStream::Event (o "stinger" do canal)
                break;
            // [driving] 0x00148600 EAwardKillToPlayer / 0x00148540 EAwardHitToPlayer
            case kEventAwardKillToPlayer:
                SMissionManager_IncKills(m, e.obj[1] == 0 ? 1 : int32_t(e.obj[1]));
                break;
            case kEventAwardHitToPlayer:
                SMissionManager_IncShotsHit(m, false);
                break;
            // [driving] 0x0014b3e0 EDamagePlayer: o ApplyDamage do carro do jogador (PBondCar::ApplyDamage não portado:
            // contado como o resto dos danos do carro).
            case kEventDamagePlayer:
                if (car) {
                    const float from[3] = {0, 0, 0};
                    car->apply_damage(from, from, flt(e.obj[1]), flt(e.obj[2]), 3);
                    ++mission_env().player_damage_requests;
                    mission_env().player_damage_requested = ee::add(mission_env().player_damage_requested, flt(e.obj[1]));
                } else {
                    ++g_stats.guards;
                }
                break;
            // [driving] 0x0014ede0 EForceCarStop (ForceStopOn 1 ou 3) / 0x0014c048 EEndCarStop (ForceStopOff 3)
            case kEventForceCarStop:
                if (car) car->d.force_stop = uint8_t(car->d.force_stop | (e.obj[1] == 0 ? 1 : 3));
                else ++g_stats.guards;
                break;
            case kEventEndCarStop:
                if (car) car->d.force_stop = uint8_t(car->d.force_stop & ~3u);
                else ++g_stats.guards;
                break;
            // [driving] 0x0015db78 ETwoWheelsOff (PBondCar::DisableTwoWheelStunt)
            case kEventTwoWheelsOff:
                if (car) {
                    PBondCar_DisableTwoWheelStunt(*car);
                    // O PBondCar::DisableTwoWheelStunt (0x18bb00) termina com o AMenuSound::Trigger(1, ...) do fim da
                    // manobra, que o porte do carro não toca: anotado aqui.
                    call(0x2f4b10, 1, 0x379e08, 0x379e18, 0);
                } else {
                    ++g_stats.guards;
                }
                break;
            // [driving] 0x001576e8 ESetAutoDriveCamOn / 0x00157460 ESetAutoDriveCamOff / 0x00157838
            // ESetAutoDriveCamOnMovie / 0x00157540 ESetAutoDriveCamOffMovie: a câmera (anotada) e a direção automática.
            case 77:
                if (m.auto_drive == 0) {
                    const float yaw = ee::div(flt(e.obj[1]), 360.0f), pitch = ee::div(flt(e.obj[2]), 360.0f);
                    call(0x1bb7c0, bits(yaw), bits(pitch), 1);
                    camera_events_autodrive_on(yaw, pitch);
                    SMissionManager_SetAutoDrive(m, 1, true);
                }
                break;
            case 76:
                call(0x1bb1f8);
                camera_events_last_selectable(0, 0, 0, 0);
                SMissionManager_SetAutoDrive(m, 0, true);
                break;
            case kEventSetAutoDriveCamOnMovie:
                if (m.auto_drive == 0) {
                    const float yaw = ee::div(flt(e.obj[1]), 360.0f), pitch = ee::div(flt(e.obj[2]), 360.0f);
                    call(0x1bb7c0, bits(yaw), bits(pitch), 1);
                    camera_events_autodrive_on(yaw, pitch);
                    if (const uint32_t hands = hands_globals().hands) AICharacterHands_ShowHands(hands);
                    call(0x22b638, 1);
                    WTargetPicker_ActivateTargeting(1);
                    SMissionManager_SetAutoDrive(m, 1, false);
                }
                break;
            case kEventSetAutoDriveCamOffMovie:
                call(0x1bb1f8, 0, 0, 0, 1);
                camera_events_last_selectable(0, 0, 0, 1);
                if (const uint32_t hands = hands_globals().hands) AICharacterHands_HideHands(hands);
                call(0x22b698);
                WTargetPicker_DeactivateTargeting();
                SMissionManager_SetAutoDrive(m, 0, false);
                break;
            default:
                ran = false;
                break;
        }
    }
    // As armas e os power-ups (driving/game/weapons.h), as explosões e os efeitos (driving/game/explosion.h).
    if (!ran) ran = audio_event_execute(e);  // os de som (driving/sound/audio_stream.h)
    if (!ran) ran = weapons_event_execute(e);
    if (!ran) ran = effects_event_execute(e);
    // O trilho, a troca de carro e a câmera (seção 23).
    if (!ran) ran = rail_event_execute(e) || heli_event_execute(e) || camera_event_execute(e);
    if (ran) ++g_stats.run[e.type];
    else ++g_stats.skipped[e.type];
}

// [driving] 0x00175690 EventManager::RunEvents
void events_run() {
    TriggerManagerGlobals& g = trigger_manager();
    // Os eventos criados enquanto estes rodam entram no fim da fila (como no anel do EventManager).
    for (size_t i = 0; i < g.pending.size(); ++i) {
        const PendingEvent e = g.pending[i];
        event_execute(e);
    }
    g.pending.clear();
}

}  // namespace drv
