"""Rostos: os alvos de morph (bloco 0x2B dos itens de mapa) e as sequências de rosto (itens tipo 4 de 22 canais).

Fontes no ACTION.ELF: parsemap_block_morph_data (0x001d23e8), SkinIt (0x0010bf30), AnimFrameCopy (0x001c5668,
modo de canal único), AnimFacialUpdate (0x001be310), AnimFacialBlend (0x001be648), AnimObjectDraw (0x001c4490).
Formato em docs/formats.md, "Morphs do rosto".

Bloco 0x2B (conteúdo, elementos de 12 bytes):
    u32 vertices          vértices com morph
    u32 targets           alvos por vértice (22 em todos os rostos do disco)
    u32 first             primeiro elemento dos deltas = ceil((3 + vertices) * 4 / 12)
    u32 index[vertices]   índice do vértice no dicionário xyz de outras plataformas (sem uso no PS2)
    ...                   até o elemento first
    f32 delta[vertices][targets][3]
O vértice de entrada do SkinIt guarda nos 16 bits altos do w o índice de morph m (-1 = nenhum): o elemento
onde começam os seus deltas (first + j * targets). O SkinIt soma, antes do skinning:
    pos += soma em k de Morph_Weight[k] * elemento[m + Morph_Index[k]]
Alguns vértices têm m = 0: leem o cabeçalho como float, desnormalizados, que a VU0 trata como 0.

Uso:
    python tools/morph.py list    extracted/files/07000001.bin [...]   entidades com morph e peles com n_morphs
    python tools/morph.py targets <pacote> <item>                       cada alvo: vértices, maior delta, centro
    python tools/morph.py faceseq <pacote>... <04xxxxxx|06xxxxxx>        os 22 pesos por quadro de uma sequência
                                                                        de rosto (ou da sequência de rosto de
                                                                        um script)
    python tools/morph.py mouth   <pacote> <item> <frames.tsv> [--wav x.wav] [--skin 05xxxxxx] [--from s --to s]
                                  a abertura da boca (o lábio de baixo) pelos pesos das colunas rosto_pesos de
                                  um --frames-dump e, com o WAV do --audio-dump (o mesmo relógio: a coluna t), a
                                  correlação com a energia da voz (e a defasagem que a maximiza)
    python tools/morph.py gltf    <pacote> <item> <saída.glb> [--face <04|06xxxxxx>] [--fps 60] [--only-face]
                                  o modelo com esqueleto e os 22 alvos de morph glTF (tools/viewer); com --face,
                                  uma animação dos pesos tirada da sequência de rosto (procurada também nos pacotes
                                  de animação da fase: 07F, 078, 079, 07A); --only-face exporta só a cabeça
"""
import argparse
import math
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(__file__))
from levelpak import parse as parse_pak  # noqa: E402
from mapfile import read_blocks  # noqa: E402

MORPH_BLOCK = 0x2B

# Os 22 alvos, pelo que os deltas movem (cabeças do disco: tools/morph.py targets): a boca (visemas), as
# pálpebras, os olhos (11 cima, 12 baixo, 13 para -x = a direita do personagem, 14 para +x), as sobrancelhas,
# expressões e o queixo. AnimFacialBlend escreve 11-14 com os olhos de AnimObjectHeadTrack.
TARGET_NAMES = ["boca0", "boca1", "boca2", "boca3", "boca4", "boca5", "boca6", "boca7", "boca8", "boca9",
                "piscar", "olhos_cima", "olhos_baixo", "olhos_direita", "olhos_esquerda", "sobrancelhas",
                "rosto16", "rosto17", "rosto18", "rosto19", "rosto20", "queixo"]


def _f32_vu(bits):
    """Float como a VU0 o lê: expoente 0 (desnormalizado) vale 0."""
    if bits & 0x7F800000 == 0:
        return 0.0
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def read_morph(payload):
    """O conteúdo de um bloco 0x2B: {vertices, targets, first, indices, elements, delta(m, k)}."""
    vertices, targets, first = struct.unpack_from("<3I", payload, 0)
    elements = first + vertices * targets
    if targets == 0 or targets > 22 or elements * 12 > len(payload) or first * 12 < (3 + vertices) * 4:
        raise ValueError("bloco de morph inesperado")
    words = struct.unpack_from(f"<{elements * 3}I", payload, 0)
    vals = [_f32_vu(w) for w in words]
    indices = struct.unpack_from(f"<{vertices}I", payload, 12)

    def delta(m, k):
        e = m + k
        if e < 0 or e >= elements:
            return (0.0, 0.0, 0.0)
        return (vals[e * 3], vals[e * 3 + 1], vals[e * 3 + 2])

    return {"vertices": vertices, "targets": targets, "first": first, "indices": indices, "elements": elements,
            "delta": delta}


def entity_morphs(item):
    """[(índice da entidade, hash da entidade, bloco 0x2B, bloco 0x2D)] de um item de mapa."""
    _v, blocks = read_blocks(item)
    out, ent, morph, gfx = [], 0, None, None
    for b in blocks:
        if b.id == MORPH_BLOCK:
            morph = b
        elif b.id == 0x2D:
            gfx = b
        elif b.id in (0x04, 0x1F):
            if morph is not None:
                out.append((ent, struct.unpack_from("<I", b.payload(item), 0)[0], morph, gfx))
            ent, morph, gfx = ent + 1, None, None
    return out


def skin_inputs(blk):
    """Os vértices de entrada do SkinIt de um bloco 0x2D: [(x, y, z, m)]."""
    header = struct.unpack_from("<I", blk, 4)[0]
    _count, _boxes, extra = struct.unpack_from("<iII", blk, header)
    if not extra:
        return []
    inp, out_size, _patch, _bones = struct.unpack_from("<4I", blk, extra & ~3)
    base = inp & ~3
    verts = []
    for k in range(out_size // 32):
        x, y, z = struct.unpack_from("<3f", blk, base + k * 32)
        w = struct.unpack_from("<i", blk, base + k * 32 + 28)[0]
        verts.append((x, y, z, w >> 16))
    return verts


def find_item(paths, name):
    for path in paths:
        data = open(path, "rb").read()
        for o, s, t, n in parse_pak(data):
            if n.lower() == name.lower():
                return data[o:o + s], t, path
    raise SystemExit(f"{name} não está em {', '.join(paths)}")


def sibling_paks(pak):
    """O pacote e os de animação da mesma fase (07F, 078, 079, 07A), onde estão as sequências e os scripts."""
    d, base = os.path.split(pak)
    out = [pak]
    if len(base) >= 8 and base[:3] == "070":
        for nib in "F89A":
            path = os.path.join(d, "07" + nib + base[3:])
            if os.path.exists(path):
                out.append(path)
    return out


def head_data(pak, item_name):
    item, _t, _p = find_item([pak], item_name)
    ents = entity_morphs(item)
    if not ents:
        raise SystemExit(f"{item_name} não tem bloco 0x2B")
    _ent, h, mb, gb = ents[0]
    morph = read_morph(mb.payload(item))
    verts = skin_inputs(item[gb.offset:gb.offset + gb.size])
    return h, morph, verts


def cmd_list(paths):
    seen = set()
    for path in paths:
        data = open(path, "rb").read()
        stem = os.path.basename(path)
        for o, s, t, n in parse_pak(data):
            item = data[o:o + s]
            if t == 3 and n.startswith("05") and item[0x12]:
                print(f"{stem} pele {n} n_morphs {item[0x12]} esqueleto {item[0x14]}")
            if t not in (0, 1, 2, 11) or n in seen:
                continue
            seen.add(n)
            try:
                ents = entity_morphs(item)
            except Exception:
                continue
            for ent, h, mb, gb in ents:
                m = read_morph(mb.payload(item))
                verts = skin_inputs(item[gb.offset:gb.offset + gb.size]) if gb else []
                used = sorted({v[3] for v in verts if v[3] >= 0})
                header = sum(1 for v in verts if 0 <= v[3] < m["first"])
                print(f"{stem} {n} entidade {ent} {h:08x}: vertices {m['vertices']} alvos {m['targets']} "
                      f"primeiro {m['first']} | malha {len(verts)} vertices, {len(used)} indices de morph distintos, "
                      f"{header} com m < primeiro (lendo o cabecalho)")


def cmd_targets(pak, item_name):
    h, m, verts = head_data(pak, item_name)
    pos = {}
    for x, y, z, mi in verts:
        if mi >= m["first"]:
            pos.setdefault(mi, (x, y, z))
    print(f"entidade {h:08x}: {m['vertices']} vertices com morph, {m['targets']} alvos")
    for k in range(m["targets"]):
        n, mx, wsum, c, s = 0, 0.0, 0.0, [0.0] * 3, [0.0] * 3
        for mi, p in pos.items():
            d = m["delta"](mi, k)
            mag = math.sqrt(sum(v * v for v in d))
            if mag > 1e-5:
                n += 1
                mx = max(mx, mag)
                wsum += mag
                for i in range(3):
                    c[i] += p[i] * mag
                    s[i] += d[i]
        if wsum:
            c = [v / wsum for v in c]
        print(f"alvo {k:2d} {TARGET_NAMES[k]:15s} vertices {n:3d} maior {mx:.4f} m centro "
              f"({c[0]:.3f} {c[1]:.3f} {c[2]:.3f}) soma ({s[0]:+.3f} {s[1]:+.3f} {s[2]:+.3f})")


def face_sequence(paths, name):
    """A sequência de rosto `name` (04xxxxxx) ou a primeira sequência de rosto do script `name` (06xxxxxx)."""
    data, t, _p = find_item(paths, name)
    if name.lower().startswith("06"):
        frames, mode = struct.unpack_from("<hh", data, 4)
        p = 8
        for _ in range(mode & 0x1FF):
            typ, words = data[p], data[p + 1]
            if typ == 0:
                h = "04%06x" % struct.unpack_from("<H", data, p + 6)[0]
                seq, _t, _p = find_item(paths, h)
                if struct.unpack_from("<I", seq, 0xC)[0] & 8:
                    return h, seq
            p += (words + (3 if typ == 0 else 2 if typ < 5 else 0) + 1) * 2
        raise SystemExit(f"{name} não toca sequência de rosto")
    return name, data


def face_weights(seq):
    """Os pesos por quadro (base 0) de uma sequência de rosto: 22 canais, um peso cada."""
    from anim import parse_sequence
    if not struct.unpack_from("<I", seq, 0xC)[0] & 8:
        raise SystemExit("não é uma sequência de rosto (flags sem o bit 3)")
    s = parse_sequence(seq)
    return [[ch.value(f) for ch in s["channels"]] for f in range(s["frames"])]


def cmd_faceseq(paths, name, step):
    h, seq = face_sequence(paths, name)
    w = face_weights(seq)
    print(f"{h}: {len(w)} quadros, {len(w[0])} canais")
    for f in range(0, len(w), step):
        print(f"{f + 1:5d} " + " ".join(f"{v:5.2f}" for v in w[f]))


def lip_set(morph, verts, k=21, count=8):
    """Os vértices do lábio de baixo: os que o alvo 21 (queixo) mais abaixa."""
    seen = {}
    for x, y, z, mi in verts:
        if mi >= morph["first"]:
            seen.setdefault(mi, (x, y, z))
    return sorted(seen, key=lambda mi: morph["delta"](mi, k)[1])[:count]


def opening(morph, lips, weights):
    """Quanto o lábio de baixo desce (m) com os pesos: a média de - soma peso x delta.y."""
    tot = 0.0
    for mi in lips:
        tot += -sum(w * morph["delta"](mi, k)[1] for k, w in enumerate(weights))
    return tot / len(lips)


def read_tsv(path, skin=None):
    """(t, pesos) de cada quadro do frames.tsv; com skin, só os quadros em que o rosto é o dessa pele (os outros
    contam como pesos 0)."""
    rows = []
    with open(path, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        it, iw = head.index("t"), head.index("rosto_pesos")
        ip = head.index("rosto_pele") if "rosto_pele" in head else None
        for line in f:
            c = line.rstrip("\n").split("\t")
            w = [float(v) for v in c[iw].split(",")] if iw < len(c) and c[iw] else [0.0] * 22
            if skin and ip is not None and c[ip].lower() != skin.lower():
                w = [0.0] * 22
            rows.append((float(c[it]), w))
    return rows


def wav_rms(path, times, window):
    """RMS (0..1) do WAV em janelas de `window` s centradas em cada tempo (o relógio da captura)."""
    with wave.open(path, "rb") as w:
        rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        raise SystemExit("WAV de 16 bits esperado")
    samples = struct.unpack(f"<{len(raw) // 2}h", raw)
    out = []
    half = int(window * rate / 2)
    for t in times:
        c = int(t * rate)
        a, b = max(0, c - half), min(n, c + half)
        if b <= a:
            out.append(0.0)
            continue
        acc = 0.0
        for i in range(a, b):
            v = sum(samples[i * ch + j] for j in range(ch)) / ch
            acc += v * v
        out.append(math.sqrt(acc / (b - a)) / 32768.0)
    return out


def pearson(a, b):
    n = len(a)
    if n < 3:
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va == 0 or vb == 0:
        return 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def cmd_mouth(pak, item_name, tsv, wav_path, lag_max, window, skin, t0, t1):
    _h, morph, verts = head_data(pak, item_name)
    lips = lip_set(morph, verts)
    rows = [r for r in read_tsv(tsv, skin) if t0 <= r[0] <= t1]
    times = [t for t, _w in rows]
    op = [opening(morph, lips, w) * 1000.0 for _t, w in rows]
    rms = wav_rms(wav_path, times, window) if wav_path else None
    for i, (t, _w) in enumerate(rows):
        line = f"{t:9.4f} abertura_mm {op[i]:6.2f}"
        if rms:
            line += f" voz_rms {rms[i]:.4f} " + "#" * int(rms[i] * 400)
        print(line)
    if rms:
        best = None
        for lag in range(-lag_max, lag_max + 1):
            a = op[max(0, lag):len(op) + min(0, lag)]
            b = rms[max(0, -lag):len(rms) + min(0, -lag)]
            r = pearson(a, b)
            if best is None or r > best[1]:
                best = (lag, r)
            if lag == 0:
                r0 = r
        print(f"correlacao abertura x voz: r(0) = {r0:.3f}; maior r = {best[1]:.3f} com a boca {best[0]} quadros "
              f"{'depois' if best[0] > 0 else 'antes'} da voz")


def cmd_gltf(pak, item_name, out, face, fps, only_face=False):
    from anim import add_gltf_animation  # noqa: F401  (mesmo módulo que exporta as animações)
    from geometry import ItemData, SceneBuilder, read_anim_skins, item_entity_hashes
    item_bytes, _t, _p = find_item([pak], item_name)
    skins = read_anim_skins(open(pak, "rb").read())
    skin = next((skins[x] for x in item_entity_hashes(item_bytes) if x in skins), None)
    if skin is None:
        raise SystemExit(f"nenhuma pele lista {item_name}")
    item = ItemData(item_name, item_bytes)
    sb = SceneBuilder(skinned=True)
    skin_index = sb.skeleton(skin)
    face_node = None
    for ent in range(len(item.entities)):
        if only_face and ent not in item.morphs:
            continue
        mesh = sb.mesh(item, ent)
        if mesh is None:
            continue
        sb.node(f"entity{ent:04d}", mesh, skin=skin_index)
        if ent in item.morphs:
            face_node = len(sb.glb.gltf["nodes"]) - 1
    if face and face_node is not None:
        _h, seq = face_sequence(sibling_paks(pak), face)
        w = face_weights(seq)
        g, n = sb.glb, len(w)
        times = struct.pack(f"<{n}f", *(i / fps for i in range(n)))
        t_acc = g.accessor(times, 5126, n, "SCALAR", None, ([0.0], [(n - 1) / fps]))
        vals = b"".join(struct.pack(f"<{len(row)}f", *row) for row in w)
        o_acc = g.accessor(vals, 5126, n * len(w[0]), "SCALAR", None)
        g.gltf.setdefault("animations", []).append({
            "name": f"rosto_{face}",
            "samplers": [{"input": t_acc, "output": o_acc, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": face_node, "path": "weights"}}]})
    sb.save(out)
    print(f"{out}: alvos de morph {sb.stats.get('alvos_de_morph', 0)}, rosto no nó {face_node}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["list", "targets", "faceseq", "mouth", "gltf"])
    ap.add_argument("args", nargs="+")
    ap.add_argument("--wav")
    ap.add_argument("--lag-max", type=int, default=30)
    ap.add_argument("--window", type=float, default=1.0 / 30.0, help="janela do RMS da voz, em segundos")
    ap.add_argument("--step", type=int, default=1)
    ap.add_argument("--face")
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--only-face", action="store_true", help="gltf: só a malha do rosto (a cabeça)")
    ap.add_argument("--skin", help="mouth: só os quadros do rosto desta pele (coluna rosto_pele)")
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    a = ap.parse_args()
    if a.cmd == "list":
        cmd_list(a.args)
    elif a.cmd == "targets":
        cmd_targets(a.args[0], a.args[1])
    elif a.cmd == "faceseq":
        cmd_faceseq(a.args[:-1], a.args[-1], a.step)
    elif a.cmd == "mouth":
        cmd_mouth(a.args[0], a.args[1], a.args[2], a.wav, a.lag_max, a.window, a.skin, a.t0, a.t1)
    elif a.cmd == "gltf":
        cmd_gltf(a.args[0], a.args[1], a.args[2], a.face, a.fps, a.only_face)


if __name__ == "__main__":
    main()
