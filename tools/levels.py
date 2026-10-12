"""Catálogo das fases: nomes, missões, seções carregáveis e cenas (NIS).

Fontes:
  - MissionData (ACTION.ELF, .data): 24 entradas de 40 bytes
        u32 level_hash, u32 mission_hash, u32 part, u32 level_info_ptr, u32 unknown, ...
    O nome da fase vem do símbolo apontado por level_info_ptr (ex.: Henderson_a, Tower_2C).
  - mp_level (ACTION.ELF, .data): 8 entradas de 24 bytes; terceiro u32 = hash do mapa.
  - Itens tipo 12 dos pacotes 070xxxxx: descritor de seção carregável (32 bytes)
        u32 script_hash, u8 section (1-3), zeros
    A seção N fica no pacote com os bits 20-23 do hash trocados por N + 7 (078, 079, 07A)
    e contém animações (tipo 4) e um único script (tipo 7).
  - tools/data/nis_menu.json: nomes das cenas, tirados do menu de depuração C_NIS_Handler.

Uso:
    python tools/levels.py                      tabela no terminal
    python tools/levels.py --json saida.json    catálogo em JSON (o visualizador usa extracted/models/levels.json)
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from elfsyms import load  # noqa: E402
from filesbin import read_file_list, vaddr_to_offset  # noqa: E402
from levelpak import parse as parse_pak  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def symbol(syms, name):
    return next(s for s in syms if s[4] == name)


def mission_data(elf):
    data, secs, syms = load(elf)
    md = symbol(syms, "MissionData")
    names = {s[0]: s[4] for s in syms if s[2] == "OBJECT"}
    base = vaddr_to_offset(secs, md[0])
    out = {}
    for i in range(md[1] // 40):
        level, mission, part, info = struct.unpack_from("<4I", data, base + i * 40)
        out[f"{level:08x}"] = {"name": names.get(info, f"0x{info:08x}"), "mission": f"{mission:08x}",
                               "part": part, "mode": "campanha"}
    mp = symbol(syms, "mp_level")
    base = vaddr_to_offset(secs, mp[0])
    for i in range(mp[1] // 24):
        level = struct.unpack_from("<I", data, base + i * 24 + 12)[0]
        out.setdefault(f"{level:08x}", {"name": f"Multiplayer {i + 1}", "mission": None,
                                        "part": i, "mode": "multiplayer"})
    return out


def nis_names():
    path = os.path.join(HERE, "data", "nis_menu.json")
    names = {}
    for group in json.load(open(path, encoding="utf-8"))["groups"]:
        for s in group["scripts"]:
            names.setdefault(s["hash"], {"name": s["name"], "menu_group": group["menu_group"]})
    return names


def section_pak_name(level_hash, section):
    h = int(level_hash, 16)
    return f"{(h & 0xFF0FFFFF) | ((section + 7) << 20):08X}"[:3] + f"{h:08x}"[3:]


def catalog(elf, files_dir):
    missions = mission_data(elf)
    nis = nis_names()
    on_disc = {os.path.splitext(n)[0].lower() for n, _o, _s in read_file_list(elf)}
    levels = []
    for level_hash in sorted(set(missions) | {n for n in on_disc if n.startswith("070")}):
        info = missions.get(level_hash, {"name": None, "mission": None, "part": None, "mode": "?"})
        entry = {"hash": level_hash, **info, "on_disc": level_hash in on_disc, "sections": []}
        if entry["on_disc"]:
            pak = open(os.path.join(files_dir, level_hash + ".bin"), "rb").read()
            for off, _size, typ, _name in parse_pak(pak):
                if typ != 12:
                    continue
                script = f"{struct.unpack_from('<I', pak, off)[0]:08x}"
                sec = pak[off + 4]
                sec_name = section_pak_name(level_hash, sec)
                anims = scripts = 0
                sec_path = os.path.join(files_dir, sec_name + ".bin")
                if os.path.exists(sec_path):
                    for _o, _s, t, _n in parse_pak(open(sec_path, "rb").read()):
                        anims += t in (3, 4, 5)
                        scripts += t == 7
                entry["sections"].append({
                    "section": sec, "pak": sec_name, "script": script,
                    "cutscene": nis.get(script, {}).get("name"),
                    "animations": anims, "scripts": scripts, "pak_on_disc": os.path.exists(sec_path),
                })
            entry["sections"].sort(key=lambda s: s["section"])
        levels.append(entry)
    return levels


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", default="ACTION.ELF")
    ap.add_argument("--files", default=os.path.join("extracted", "files"))
    ap.add_argument("--json")
    args = ap.parse_args()

    levels = catalog(args.elf, args.files)
    if args.json:
        os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
        json.dump(levels, open(args.json, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        print(f"{len(levels)} fases em {args.json}")
        return
    for lv in levels:
        disc = "" if lv["on_disc"] else "  (não está no disco)"
        print(f"{lv['hash']}  {lv['mode']:11s} {lv['name'] or '?':22s} missão={lv['mission'] or '-'} "
              f"parte={lv['part'] if lv['part'] is not None else '-'}{disc}")
        for s in lv["sections"]:
            print(f"    seção {s['section']} ({s['pak']}): script {s['script']} "
                  f"\"{s['cutscene'] or '?'}\", {s['animations']} animações")


if __name__ == "__main__":
    main()
