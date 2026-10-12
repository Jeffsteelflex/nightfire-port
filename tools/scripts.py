"""Lista e decodifica os scripts de cena (itens tipo 7, hash 06xxxxxx) dos pacotes de fase.

Os scripts são lidos por Script_Load (0x001d8248) e tocados por Script_Run/Script_Update
(0x001d8548-0x001db920, portados em src/engine/script.cpp). Formato (ver docs/formats.md,
"Scripts de cena"):

    u16 versão          sempre 0x1c (outro valor: o jogo marca o hash como "não é script")
    u16 câmera          diferente de zero: o script controla a câmera (ScriptCam)
    u16 n_streams       até 0x3e
    u16 loop_start      quadro para onde volta um script em laço
    u16 end             último quadro
    n_streams x {
        u16 first_key, last_key   intervalo de quadros-chave do stream (posição/rotação/escala/cor)
        u32 size                  inclui o próprio u32
        u8  commands[size - 4]
    }
    u32 n_keys
    u32 pad                       bytes até alinhar as chaves a 16
    KEYED_POSROT keys[n_keys]     0x30 bytes cada: f32 pos[4], f32 quat[4] (x, y, z, w),
                                  f32 scale, f32 alpha (ou u8 r, g, b nas luzes), u32 ?, f32 quadro

Cada stream é uma lista de comandos (um byte de código, os argumentos e, nos comandos com
tratador, um byte de fecho). Uso:

    python tools/scripts.py list   extracted/files/07000001.bin
    python tools/scripts.py dump   extracted/files/07000001.bin [--hash 06000052] [--keys]
    python tools/scripts.py stats  extracted/files/07*.bin
    python tools/scripts.py canais extracted/files/07*.bin   canais que os eventos 13 e 17 ligam, por fase
"""
import argparse
import collections
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import levelpak  # noqa: E402

KEY = struct.Struct("<4f4f2fIf")

# Eventos do comando 0x12 (Script_EventHandler).
EVENTS = {
    3: "pausa (espera n-1 ticks ou SP_UnPause)",
    4: "desliga o jogador",
    5: "desliga a IA",
    6: "devolve o jogador no fim (matriz do objeto anterior ou do jogador)",
    7: "religa a IA no fim",
    8: "cria um drone (Drone_CoderCreate)",
    9: "modo de câmera do jogador",
    10: "quebra o objeto anterior",
    11: "byte +0x62 da animação do objeto anterior",
    12: "objetos ficam no fim (não somem)",
    13: "liga canal",
    14: "o stream some no fim do laço",
    15: "chama o callback do dono",
    16: "pula a câmera e os sons",
    17: "canal condicional",
    18: "troca de fase",
    19: "ScriptCam",
}


def u8(d, p):
    return d[p], p + 1


def u16(d, p):
    return struct.unpack_from("<H", d, p)[0], p + 2


def u32(d, p):
    return struct.unpack_from("<I", d, p)[0], p + 4


def f32(d, p):
    return struct.unpack_from("<f", d, p)[0], p + 4


def decode_commands(d, p, end):
    """Devolve [(offset, código, texto)] de um stream, seguindo Script_Run."""
    out = []
    while p < end:
        start = p
        cmd, p = u8(d, p)
        txt = None
        closing = True
        if cmd == 4:
            w, p = u16(d, p)
            txt = f"fim do stream no quadro {w}"
            closing = False
        elif cmd == 5:
            w, p = u16(d, p)
            txt = f"espera até o quadro {w}"
        elif cmd == 6:
            w, p = u16(d, p)
            txt = f"marca do quadro {w} (passou do fim: termina)"
        elif cmd == 7:
            h, p = u32(d, p)
            has_parent, p = u8(d, p)
            parent, p = u8(d, p)
            spline, p = u8(d, p)
            typ, p = u8(d, p)
            n, p = u8(d, p)
            params = []
            for _ in range(n):
                v, p = u32(d, p)
                params.append(v)
            par = f" pai={parent}" if has_parent else ""
            txt = f"entidade {h:08x} tipo=0x{typ:02x}{par} spline={spline} params={params}"
        elif cmd == 9:
            txt = "fim da entidade"
        elif cmd == 10:
            s, p = u32(d, p)
            skin, p = u32(d, p)
            frame, p = u32(d, p)
            txt = f"animação script={s:08x} pele={skin:08x} quadro={frame}"
        elif cmd == 0xc:
            txt = "fim da animação"
        elif cmd == 0xd:
            spline, p = u8(d, p)
            a, p = u16(d, p)
            b, p = u16(d, p)
            txt = f"câmera chaves {a}-{b} spline={spline}"
        elif cmd == 0xf:
            txt = "fim da câmera"
        elif cmd == 0x12:
            n, p = u8(d, p)
            ev, p = u8(d, p)
            args = []
            for _ in range(n):
                v, p = u32(d, p)
                args.append(v)
            txt = f"evento {ev} ({EVENTS.get(ev, '?')}) {args}"
        elif cmd == 0x13:
            t, p = f32(d, p)
            txt = f"fade {t}"
        elif cmd == 0x14:
            vals = []
            for _ in range(5):
                v, p = u16(d, p)
                vals.append(v)
            h, p = u32(d, p)
            txt = f"sprite {h:08x} {vals}"
        elif cmd == 0x15:
            txt = "fim do sprite"
        elif cmd == 0x16:
            h, p = u32(d, p)
            mode, p = u8(d, p)
            own, p = u8(d, p)
            obj, p = u8(d, p)
            txt = f"som {h:08x} 2d={mode} objeto={obj if own else 'anterior'}"
        elif cmd == 0x17:
            txt = "fim do som"
        elif cmd == 0x18:
            has_parent, p = u8(d, p)
            parent, p = u8(d, p)
            kind, p = u8(d, p)
            par = f" pai={parent}" if has_parent else ""
            txt = f"luz tipo={kind}{par}"
        elif cmd == 0x1a:
            txt = "fim da luz"
        elif cmd == 0x1b:
            h, p = u32(d, p)
            has_parent, p = u8(d, p)
            parent, p = u8(d, p)
            par = f" pai={parent}" if has_parent else ""
            txt = f"sub-script {h:08x}{par}"
        elif cmd == 0x1d:
            txt = "fim do sub-script"
        elif cmd == 0x1e:
            h, p = u32(d, p)
            t, p = u16(d, p)
            txt = f"texto {h:08x} tempo={t}"
        else:
            txt = "(sem tratador)"
            closing = False
        if closing:
            p += 1
        out.append((start, cmd, txt))
        if cmd == 4:
            break
    return out


def parse_script(d):
    version, cam, n, loop_start, end = struct.unpack_from("<5H", d, 0)
    if version != 0x1c:
        return None
    p = 10
    streams = []
    for _ in range(n):
        a, b, size = struct.unpack_from("<HHI", d, p)
        streams.append({"first": a, "last": b, "start": p + 8, "end": p + 4 + size})
        p += 4 + size
    n_keys, pad = struct.unpack_from("<II", d, p)
    keys_at = p + 8 + pad
    keys = [KEY.unpack_from(d, keys_at + i * KEY.size) for i in range(n_keys)]
    return {"camera": cam, "loop_start": loop_start, "end": end, "streams": streams, "keys": keys,
            "keys_at": keys_at}


def scripts_in(path):
    data = open(path, "rb").read()
    for off, size, typ, name in levelpak.parse(data):
        if typ == 7:
            yield name, data[off:off + size]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["list", "dump", "stats", "canais"])
    ap.add_argument("paks", nargs="+")
    ap.add_argument("--hash")
    ap.add_argument("--keys", action="store_true")
    args = ap.parse_args()
    counts = collections.Counter()
    events = collections.Counter()
    entity_types = collections.Counter()
    total = 0
    bad = 0
    channels = collections.defaultdict(set)  # canal -> scripts que o ligam (eventos 13 e 17)
    for path in levelpak.expand(args.paks):
        pak = os.path.splitext(os.path.basename(path))[0]
        for name, d in scripts_in(path):
            if args.hash and name.lower() != args.hash.lower():
                continue
            s = parse_script(d)
            total += 1
            if s is None:
                print(f"{pak} {name}: versão desconhecida")
                continue
            if args.cmd == "list":
                print(f"{pak} {name}: {len(s['streams'])} streams, {len(s['keys'])} chaves, fim {s['end']}, "
                      f"laço {s['loop_start']}, câmera {s['camera']}")
                continue
            if args.cmd == "dump":
                print(f"== {pak} {name}: câmera={s['camera']} laço={s['loop_start']} fim={s['end']} "
                      f"chaves={len(s['keys'])} (em 0x{s['keys_at']:x})")
            for i, st in enumerate(s["streams"]):
                cmds = decode_commands(d, st["start"], st["end"])
                if args.cmd == "dump":
                    print(f"  stream {i} chaves {st['first']}-{st['last']}")
                    for off, cmd, txt in cmds:
                        print(f"    {off:5x} {cmd:02x} {txt}")
                last = cmds[-1][0] + 4 if cmds and cmds[-1][1] == 4 else -1
                if last != st["end"]:
                    print(f"{pak} {name}: stream {i} termina em 0x{last:x}, esperado 0x{st['end']:x}")
                    bad += 1
                for off, cmd, txt in cmds:
                    counts[cmd] += 1
                    if cmd == 0x12:
                        events[d[off + 2]] += 1
                        n, ev = d[off + 1], d[off + 2]
                        a = [u32(d, off + 3 + 4 * k)[0] for k in range(n)]
                        # Script_EventHandler: 13 liga a[0] com o valor a[1]; 17 liga a[1] ou a[2] com a[3].
                        if ev == 13 and n >= 2 and a[0] != 0 and a[1] != 0:
                            channels[a[0] & 0xff].add(name)
                        if ev == 17 and n >= 4 and a[3] != 0:
                            for c in (a[1], a[2]):
                                if c != 0:
                                    channels[c & 0xff].add(name)
                    if cmd == 7:
                        entity_types[d[off + 8]] += 1
            if args.cmd == "canais":
                pass
            if args.cmd == "dump" and args.keys:
                for i, k in enumerate(s["keys"]):
                    print(f"  chave {i:4d} quadro {k[11]:7.1f} pos ({k[0]:.2f}, {k[1]:.2f}, {k[2]:.2f}) "
                          f"quat ({k[4]:.3f}, {k[5]:.3f}, {k[6]:.3f}, {k[7]:.3f}) escala {k[8]:.3f} "
                          f"alfa {k[9]:.3f} ({struct.pack('<f', k[9]).hex()})")
    if args.cmd == "canais":
        print("canais ligados por eventos:", ", ".join(f"{c}({len(v)})" for c, v in sorted(channels.items())))
    if args.cmd == "stats":
        print(f"{total} scripts, {bad} streams fora do lugar")
        print("comandos:", ", ".join(f"0x{c:02x}={n}" for c, n in sorted(counts.items())))
        print("eventos:", ", ".join(f"{e}={n}" for e, n in sorted(events.items())))
        print("entidades por tipo:", ", ".join(f"0x{t:02x}={n}" for t, n in sorted(entity_types.items())))


if __name__ == "__main__":
    main()
