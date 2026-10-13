"""Gera src/engine/control_funcs.cpp a partir da tabela control_funcs do ACTION.ELF.

A tabela (0x0026f890, 85 entradas de 12 bytes) guarda {atualização, colisão, remoção} de cada
tipo de objeto. Funções já portadas entram pelo nome; as outras viram stubs que registram a
chamada. Rodar de novo quando uma delas for portada (e tirá-la de PORTED se ainda não estiver).

Uso: python tools/gen_control_funcs.py [ACTION.ELF]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import elfsyms  # noqa: E402

PORTED = {"control_AnimUpdate", "Door_Update", "Door_ReactToHit", "Player_Update", "Player_CollisionHandler",
          "Bullet_update", "Bullet_CollisionHandler", "Bullet_Delete", "Bullet_casing_update", "Debris_Update",
          "gases_smoke", "Swoosh_Update", "Emitter_Update", "Emitter_Kill",
          "Flicker_Update", "Drone_Control", "Drone_Delete", "Drone_AIVolume_Delete", "Trigger_Update",
          "Drone_CollisionHandler", "Pickup_Update", "Break_Update", "Destroy_Update",
          "Destroy_CollisionHandler", "Explode_Update", "Explode_CollisionHandler", "Explode_Delete",
          "Hurt_Update",
          "SP_Update", "SP_Delete", "Switch_Update", "FuseBox_Update", "DroneSpawner_Control",
          "DroneSpawner_Delete", "SoundTrigger_Update", "MusicTrigger_Update", "Hint_Update",
          "ThirdIcon_Update", "Lock_Update", "PCQWorm_Update", "CamSubject_Update", "GT_Update", "GT_Delete", "Copter_Update", "Copter_Delete", "SS_Update",
          "Sensor_Update", "Monitor_Update", "Searchlight_Update", "Mine_Update", "Shooter_Update",
          "Apocalypse_Update", "InverterTrigger_Update", "GunImp_Update",
          "SpaceLaser_Update", "SpaceMissile_Update", "RB_Update", "RB_CollisionHandler",
          "Lightning_Update", "cloud_update", "rotor_update", "Leaf_Update", "LeafGen_Update", "Ripples_Update",
          "Env_Tree_Update", "Particles_Update", "OneSided_Update", "Occlude_Update"}
ADDR, COUNT = 0x0026F890, 85


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "ACTION.ELF"
    data, sections, syms = elfsyms.load(elf)
    names = {s[0]: s[4].split("__")[0] for s in syms}
    off = next(o + ADDR - a for _, (_, t, _, a, o, size, *_) in sections if a <= ADDR < a + size and t != 8)
    rows = [struct.unpack_from("<III", data, off + i * 12) for i in range(COUNT)]
    stubs = sorted({names[f] for row in rows for f in row if f} - PORTED)
    out = ["// Gerado por tools/gen_control_funcs.py a partir de control_funcs (0x0026f890) do ACTION.ELF.",
           "// {atualização, colisão, remoção} por tipo de objeto (obj_tag::type). Não editar à mão.",
           "",
           '#include "engine/ainetwork.h"',
           '#include "engine/break.h"',
           '#include "engine/bullet.h"',
           '#include "engine/door.h"',
           '#include "engine/drone.h"',
           '#include "engine/effect.h"',
           '#include "engine/lights.h"',
           '#include "engine/emitter.h"',
           '#include "engine/gunimp.h"',
           '#include "engine/hazard.h"',
           '#include "engine/explode.h"',
           '#include "engine/object.h"',
           '#include "engine/pickup.h"',
           '#include "engine/player.h"',
           '#include "engine/player_traverse.h"',
           '#include "engine/script.h"',
           '#include "engine/rigid_body.h"',
           '#include "engine/scenery.h"',
           '#include "engine/searchlight.h"',
           '#include "engine/sensor.h"',
           '#include "engine/ssys.h"',
           '#include "engine/turret.h"',
           '#include "engine/copter.h"',
           '#include "engine/simple_script.h"',
           '#include "engine/space_station.h"',
           '#include "engine/sound.h"',
           '#include "engine/trigger.h"',
           '#include "engine/weather.h"',
           '#include "platform/pending.h"',
           "",
           "// Ainda não portadas.",
           "namespace {"]
    for name in stubs:
        out.append(f'void {name}(obj_tag*) {{ pending_call("{name}"); }}')
    out += ["}  // namespace", "", "const CONTROLFUNCS_tag control_funcs[kControlFuncCount] = {"]
    for i, row in enumerate(rows):
        cells = ", ".join(names[f] if f else "nullptr" for f in row)
        out.append(f"    {{{cells}}},  // 0x{i:02x}")
    out.append("};")
    path = os.path.join(os.path.dirname(__file__), "..", "src", "engine", "control_funcs.cpp")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"{len(stubs)} stubs, {COUNT} entradas -> {os.path.normpath(path)}")


if __name__ == "__main__":
    main()
