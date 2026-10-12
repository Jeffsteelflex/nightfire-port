// Gerado por tools/gen_control_funcs.py a partir de control_funcs (0x0026f890) do ACTION.ELF.
// {atualização, colisão, remoção} por tipo de objeto (obj_tag::type). Não editar à mão.

#include "engine/ainetwork.h"
#include "engine/break.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/lights.h"
#include "engine/emitter.h"
#include "engine/gunimp.h"
#include "engine/hazard.h"
#include "engine/explode.h"
#include "engine/object.h"
#include "engine/pickup.h"
#include "engine/player.h"
#include "engine/player_traverse.h"
#include "engine/script.h"
#include "engine/rigid_body.h"
#include "engine/scenery.h"
#include "engine/searchlight.h"
#include "engine/sensor.h"
#include "engine/ssys.h"
#include "engine/turret.h"
#include "engine/copter.h"
#include "engine/simple_script.h"
#include "engine/space_station.h"
#include "engine/sound.h"
#include "engine/trigger.h"
#include "engine/weather.h"
#include "platform/pending.h"

// Ainda não portadas.
namespace {
void BodyGlow_Update(obj_tag*) { pending_call("BodyGlow_Update"); }
void Bubble_Update(obj_tag*) { pending_call("Bubble_Update"); }
void Car_CollisionHandler(obj_tag*) { pending_call("Car_CollisionHandler"); }
void Car_Delete(obj_tag*) { pending_call("Car_Delete"); }
void Car_Update(obj_tag*) { pending_call("Car_Update"); }
void Corona_Update(obj_tag*) { pending_call("Corona_Update"); }
void Creature_Delete(obj_tag*) { pending_call("Creature_Delete"); }
void Creature_Update(obj_tag*) { pending_call("Creature_Update"); }
void Discrete_Delete(obj_tag*) { pending_call("Discrete_Delete"); }
void DynamicObject_Update(obj_tag*) { pending_call("DynamicObject_Update"); }
void MP_ObjectDelete(obj_tag*) { pending_call("MP_ObjectDelete"); }
void MP_ObjectUpdate(obj_tag*) { pending_call("MP_ObjectUpdate"); }
void MiniSub_Collide(obj_tag*) { pending_call("MiniSub_Collide"); }
void MiniSub_Delete(obj_tag*) { pending_call("MiniSub_Delete"); }
void MiniSub_Update(obj_tag*) { pending_call("MiniSub_Update"); }
void Occlude_Update(obj_tag*) { pending_call("Occlude_Update"); }
void Sub_Collide(obj_tag*) { pending_call("Sub_Collide"); }
void Sub_Delete(obj_tag*) { pending_call("Sub_Delete"); }
void Sub_Update(obj_tag*) { pending_call("Sub_Update"); }
void Sunreflection_Update(obj_tag*) { pending_call("Sunreflection_Update"); }
}  // namespace

const CONTROLFUNCS_tag control_funcs[kControlFuncCount] = {
    {nullptr, nullptr, nullptr},  // 0x00
    {nullptr, nullptr, nullptr},  // 0x01
    {Drone_Control, Drone_CollisionHandler, Drone_Delete},  // 0x02
    {Player_Update, Player_CollisionHandler, nullptr},  // 0x03
    {nullptr, nullptr, nullptr},  // 0x04
    {Bullet_update, Bullet_CollisionHandler, Bullet_Delete},  // 0x05
    {Bubble_Update, nullptr, nullptr},  // 0x06
    {rotor_update, nullptr, nullptr},  // 0x07
    {nullptr, nullptr, nullptr},  // 0x08
    {nullptr, nullptr, nullptr},  // 0x09
    {nullptr, nullptr, nullptr},  // 0x0a
    {nullptr, nullptr, nullptr},  // 0x0b
    {Particles_Update, nullptr, nullptr},  // 0x0c
    {Bullet_casing_update, nullptr, nullptr},  // 0x0d
    {gases_smoke, nullptr, nullptr},  // 0x0e
    {Explode_Update, Explode_CollisionHandler, Explode_Delete},  // 0x0f
    {Debris_Update, nullptr, nullptr},  // 0x10
    {Drone_Control, nullptr, Drone_Delete},  // 0x11
    {Player_Update, Player_CollisionHandler, nullptr},  // 0x12
    {nullptr, nullptr, nullptr},  // 0x13
    {nullptr, nullptr, nullptr},  // 0x14
    {control_AnimUpdate, nullptr, nullptr},  // 0x15
    {nullptr, nullptr, nullptr},  // 0x16
    {nullptr, nullptr, nullptr},  // 0x17
    {nullptr, nullptr, nullptr},  // 0x18
    {Sunreflection_Update, nullptr, nullptr},  // 0x19
    {RB_Update, RB_CollisionHandler, nullptr},  // 0x1a
    {SP_Update, nullptr, SP_Delete},  // 0x1b
    {Door_Update, Door_ReactToHit, nullptr},  // 0x1c
    {Trigger_Update, nullptr, nullptr},  // 0x1d
    {nullptr, nullptr, nullptr},  // 0x1e
    {control_AnimUpdate, nullptr, nullptr},  // 0x1f
    {Break_Update, nullptr, nullptr},  // 0x20
    {Destroy_Update, Destroy_CollisionHandler, nullptr},  // 0x21
    {Searchlight_Update, nullptr, nullptr},  // 0x22
    {Creature_Update, nullptr, Creature_Delete},  // 0x23
    {cloud_update, nullptr, nullptr},  // 0x24
    {SS_Update, nullptr, nullptr},  // 0x25
    {Flicker_Update, nullptr, nullptr},  // 0x26
    {Hurt_Update, nullptr, nullptr},  // 0x27
    {Car_Update, Car_CollisionHandler, Car_Delete},  // 0x28
    {nullptr, nullptr, nullptr},  // 0x29
    {Occlude_Update, nullptr, nullptr},  // 0x2a
    {nullptr, nullptr, nullptr},  // 0x2b
    {nullptr, nullptr, nullptr},  // 0x2c
    {nullptr, nullptr, nullptr},  // 0x2d
    {Leaf_Update, nullptr, nullptr},  // 0x2e
    {Pickup_Update, nullptr, nullptr},  // 0x2f
    {DroneSpawner_Control, nullptr, DroneSpawner_Delete},  // 0x30
    {LeafGen_Update, nullptr, nullptr},  // 0x31
    {Emitter_Update, nullptr, Emitter_Kill},  // 0x32
    {Lightning_Update, nullptr, nullptr},  // 0x33
    {Ripples_Update, nullptr, nullptr},  // 0x34
    {MP_ObjectUpdate, nullptr, MP_ObjectDelete},  // 0x35
    {GT_Update, nullptr, GT_Delete},  // 0x36
    {Sensor_Update, nullptr, nullptr},  // 0x37
    {Monitor_Update, nullptr, nullptr},  // 0x38
    {Switch_Update, nullptr, nullptr},  // 0x39
    {Lock_Update, nullptr, nullptr},  // 0x3a
    {Copter_Update, nullptr, Copter_Delete},  // 0x3b
    {FuseBox_Update, nullptr, nullptr},  // 0x3c
    {nullptr, nullptr, Discrete_Delete},  // 0x3d
    {nullptr, nullptr, Drone_AIVolume_Delete},  // 0x3e
    {Env_Tree_Update, nullptr, nullptr},  // 0x3f
    {SoundTrigger_Update, nullptr, nullptr},  // 0x40
    {Swoosh_Update, nullptr, nullptr},  // 0x41
    {nullptr, ThirdIcon_Update, nullptr},  // 0x42
    {Hint_Update, nullptr, nullptr},  // 0x43
    {MusicTrigger_Update, nullptr, nullptr},  // 0x44
    {Corona_Update, nullptr, nullptr},  // 0x45
    {BodyGlow_Update, nullptr, nullptr},  // 0x46
    {GunImp_Update, nullptr, nullptr},  // 0x47
    {CamSubject_Update, nullptr, nullptr},  // 0x48
    {PCQWorm_Update, nullptr, nullptr},  // 0x49
    {Sub_Update, Sub_Collide, Sub_Delete},  // 0x4a
    {Mine_Update, nullptr, nullptr},  // 0x4b
    {OneSided_Update, nullptr, nullptr},  // 0x4c
    {MiniSub_Update, MiniSub_Collide, MiniSub_Delete},  // 0x4d
    {Shooter_Update, nullptr, nullptr},  // 0x4e
    {SpaceMissile_Update, nullptr, nullptr},  // 0x4f
    {SpaceLaser_Update, nullptr, nullptr},  // 0x50
    {nullptr, nullptr, nullptr},  // 0x51
    {DynamicObject_Update, nullptr, nullptr},  // 0x52
    {InverterTrigger_Update, nullptr, nullptr},  // 0x53
    {Apocalypse_Update, nullptr, nullptr},  // 0x54
};
