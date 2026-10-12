"""Decodificação das animações esqueléticas (itens tipo 4, hash 04xxxxxx).

Fontes no ACTION.ELF: AnimSkeletonProcess (0x001bd328), AnimProcessSeqData (0x001c4ce0)
e AnimFrameCopy (0x001c5668).

Esqueleto (item SKEL, tipo 6):
    +0x00  u16 skeleton_id
    +0x02  u8  bone_count
    +0x03  u8  desconhecido
    +0x04  u32 translation_mask[3]   bit i = osso i tem translação animada
    +0x10  bone_count x f32[3]       translação local padrão (relativa ao pai)

Sequência (item tipo 4):
    +0x00  u32 hash
    +0x10  f32[10]                    desconhecido (movimento da raiz? caixa?)
    +0x38  i16 frame_count
    +0x3A  i16 channel_count          soma de 3 por osso, mais 3 por osso com translação
    +0x3C  u16 x2                     desconhecido
    +0x40  registros de canal, em sequência, osso por osso:
           com translação: tx, ty, tz, qx, qy, qz;  sem translação: qx, qy, qz

Registro de canal:
    u32 header       16 bits baixos = tamanho do registro em bytes; 16 altos = 1º descritor
    depois, alternando: coeficientes do segmento atual e palavras com 2 descritores
    (16 bits baixos primeiro, depois os altos)

Descritor de segmento (16 bits):
    bits 0-5   máscara dos coeficientes presentes c0..c5 (os ausentes valem 0)
    bits 6-15  duração em quadros

O valor de um canal no quadro f (base 0) usa o primeiro segmento cuja soma acumulada
de durações é >= f e vale c0 + c1*f + c2*f^2 + ... + c5*f^5, com f absoluto.
O quatérnio guarda só x, y, z; w = sqrt(1 - x^2 - y^2 - z^2). Se o valor de z é >= 2,
o z real é valor - 4 e w é negativo.
"""
import math
import struct


def parse_skeleton(data):
    sid, count = struct.unpack_from("<HB", data, 0)
    mask = struct.unpack_from("<3I", data, 4)
    trans = [bool(mask[b >> 5] >> (b & 31) & 1) for b in range(count)]
    defaults = [struct.unpack_from("<3f", data, 0x10 + 12 * b) for b in range(count)]
    return {"id": sid, "bones": count, "animated_translation": trans, "default_translation": defaults}


def expected_channels(skel):
    return 3 * skel["bones"] + 3 * sum(skel["animated_translation"])


class Channel:
    __slots__ = ("segments",)

    def __init__(self):
        self.segments = []  # (fim_acumulado, coeficientes[6])

    def value(self, f):
        for end, coeffs in self.segments:
            if f <= end:
                break
        else:
            end, coeffs = self.segments[-1]
        v, p = 0.0, 1.0
        for c in coeffs:
            v += c * p
            p *= f
        return v


def parse_channel(data, pos, frames):
    header = struct.unpack_from("<I", data, pos)[0]
    length = header & 0xFFFF
    end_pos = pos + length
    ch = Channel()
    p = pos + 4
    pending = header >> 16
    have_pending = True
    cumulative = 0
    while True:
        if have_pending:
            desc = pending
            have_pending = False
        else:
            word = struct.unpack_from("<I", data, p)[0]
            p += 4
            desc, pending = word & 0xFFFF, word >> 16
            have_pending = True
        mask, dur = desc & 0x3F, desc >> 6
        coeffs = []
        for i in range(6):
            if mask >> i & 1:
                coeffs.append(struct.unpack_from("<f", data, p)[0])
                p += 4
            else:
                coeffs.append(0.0)
        cumulative += dur
        ch.segments.append((cumulative, coeffs))
        if cumulative >= frames - 1 or p >= end_pos:
            break
    return ch, end_pos


def parse_sequence(data):
    frames, nch = struct.unpack_from("<hh", data, 0x38)
    pos, channels = 0x40, []
    for _ in range(nch):
        ch, pos = parse_channel(data, pos, frames)
        channels.append(ch)
    return {"frames": frames, "channels": channels}


def quat_from_xyz(x, y, z_raw):
    neg = z_raw >= 2.0
    z = z_raw - 4.0 if neg else z_raw
    w2 = 1.0 - (x * x + y * y + z * z)
    w = math.sqrt(w2) if w2 > 0 else 0.0
    return (x, y, z, -w if neg else w)


def evaluate(seq, skel, scale=1.0):
    """Devolve, por quadro, a lista (translação, quatérnio) local de cada osso."""
    frames = []
    for f in range(seq["frames"]):
        pose, k = [], 0
        for b in range(skel["bones"]):
            if skel["animated_translation"][b]:
                t = tuple(seq["channels"][k + i].value(f) * scale for i in range(3))
                k += 3
            else:
                t = tuple(c * scale for c in skel["default_translation"][b])
            qx, qy, qz = (seq["channels"][k + i].value(f) for i in range(3))
            k += 3
            pose.append((t, quat_from_xyz(qx, qy, qz)))
        frames.append(pose)
    return frames


def add_gltf_animation(glb, name, poses, joint_nodes, animated_translation, fps=30.0, tolerance=1e-5):
    """Grava uma animação glTF com amostras por quadro (interpolação linear).

    poses: saída de evaluate(); joint_nodes[b]: índice do nó glTF do osso b.
    Trilhas constantes são omitidas; a translação só entra nos ossos com translação animada.
    """
    g = glb.gltf
    n = len(poses)
    if n < 2:
        return None
    times = struct.pack(f"<{n}f", *(i / fps for i in range(n)))
    t_acc = glb.accessor(times, 5126, n, "SCALAR", None, ([0.0], [(n - 1) / fps]))
    samplers, channels = [], []
    bones = min(len(joint_nodes), len(poses[0]))
    for b in range(bones):
        rots = []
        prev = None
        for pose in poses:
            x, y, z, w = pose[b][1]
            ln = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
            q = [x / ln, y / ln, z / ln, w / ln]
            if prev is not None and sum(a * c for a, c in zip(q, prev)) < 0:
                q = [-c for c in q]  # mantém o hemisfério para a interpolação não dar a volta longa
            rots.append(q)
            prev = q
        if max(abs(r[i] - rots[0][i]) for r in rots for i in range(4)) > tolerance:
            out = glb.accessor(b"".join(struct.pack("<4f", *r) for r in rots), 5126, n, "VEC4", None)
            samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": joint_nodes[b], "path": "rotation"}})
        if animated_translation[b]:
            ts = [pose[b][0] for pose in poses]
            if max(abs(t[i] - ts[0][i]) for t in ts for i in range(3)) > tolerance:
                out = glb.accessor(b"".join(struct.pack("<3f", *t) for t in ts), 5126, n, "VEC3", None)
                samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1, "target": {"node": joint_nodes[b], "path": "translation"}})
    if not channels:
        return None
    g.setdefault("animations", []).append({"name": name, "samplers": samplers, "channels": channels})
    return len(g["animations"]) - 1


# ---------------------------------------------------------------- biblioteca por fase

def export_level_library(level_hash, files_dir, out_dir, fps=30.0):
    """Para uma fase, gera um .glb por esqueleto usado pelos personagens do pacote principal,
    com o primeiro modelo daquele esqueleto e todas as sequências compatíveis encontradas no
    pacote de animações (07F) e nas seções (078, 079, 07A).

    A compatibilidade é decidida pelo número de canais (3 por osso + 3 por osso com
    translação animada), que é o que o motor percorre em AnimFrameCopy."""
    import os
    import sys
    sys.path.insert(0, os.path.dirname(__file__))
    from levelpak import parse
    from geometry import export_item, read_anim_skins, item_entity_hashes

    h = int(level_hash, 16)
    main_path = os.path.join(files_dir, f"{level_hash}.bin")
    main = open(main_path, "rb").read()
    anim_paks = []
    for nib in (0xF, 0x8, 0x9, 0xA):
        name = f"{(h & 0xFF0FFFFF) | (nib << 20):08X}"[:3] + f"{h:08x}"[3:]
        path = os.path.join(files_dir, name + ".bin")
        if os.path.exists(path):
            anim_paks.append((name, open(path, "rb").read()))

    skels = {}
    for o, s, t, n in parse(main):
        if t == 6:
            sk = parse_skeleton(main[o:o + s])
            skels[sk["id"]] = sk
    skins = read_anim_skins(main)

    # um modelo representativo por layout de esqueleto (esqueletos com o mesmo número de
    # ossos e a mesma máscara de translação recebem exatamente as mesmas sequências)
    def layout(sk):
        return (sk["bones"], tuple(sk["animated_translation"]))

    reps, seen_layouts = {}, set()
    for o, s, t, n in parse(main):
        if t != 2:
            continue
        item = main[o:o + s]
        skin = next((skins[x] for x in item_entity_hashes(item) if x in skins), None)
        if not skin or skin["skeleton"] not in skels:
            continue
        key = layout(skels[skin["skeleton"]])
        if key not in seen_layouts:
            seen_layouts.add(key)
            reps[skin["skeleton"]] = (n, item, skin)

    os.makedirs(out_dir, exist_ok=True)
    results = []
    for sid, (model, item, skin) in sorted(reps.items()):
        sk = skels[sid]
        need = expected_channels(sk)
        animations = []
        for pak_name, data in anim_paks:
            for o, s, t, n in parse(data):
                if t == 4 and struct.unpack_from("<h", data, o + 0x3A)[0] == need:
                    seq = parse_sequence(data[o:o + s])
                    animations.append((f"{pak_name[:3]}_{n}", evaluate(seq, sk), sk["animated_translation"]))
        if not animations:
            continue
        out = os.path.join(out_dir, f"{level_hash}_skel{sid:02d}_{model}.glb")
        stats = export_item(item, out, model, skin=skin, animations=animations)
        results.append((out, sid, model, stats.get("animacoes", 0)))
        print(f"{out}: esqueleto {sid} ({sk['bones']} ossos), modelo {model}, {stats.get('animacoes', 0)} animações")
    return results


if __name__ == "__main__":
    import argparse
    import glob
    import os
    ap = argparse.ArgumentParser(description="Exporta bibliotecas de animação por fase (um .glb por esqueleto).")
    ap.add_argument("levels", nargs="*", help="hash da fase (ex.: 07000001); vazio = todas")
    ap.add_argument("--files", default=os.path.join("extracted", "files"))
    ap.add_argument("--out", default=os.path.join("extracted", "models", "anims"))
    args = ap.parse_args()
    levels = args.levels or sorted(os.path.splitext(os.path.basename(p))[0]
                                   for p in glob.glob(os.path.join(args.files, "070*.bin")))
    for lv in levels:
        export_level_library(lv, args.files, args.out)
