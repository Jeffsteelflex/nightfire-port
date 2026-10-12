"""Progresso da decompilação: cruza os marcadores [decomp] do código com as funções do ACTION.ELF.

Cada função portada leva, logo acima da definição, um comentário no formato
    // [decomp] 0x001e3000 Vec_Normalise
com o endereço original e o nome (demangled, sem parâmetros).

Entradas: src/**/*.cpp e out/functions.tsv (gerado pelo ExportAllCalls.java; ver docs/setup.md).
Se existir out/layers.json (tools/layers.py --json), o relatório também separa por camada.

Uso:
    python tools/decomp_status.py            resumo e módulos com algo portado
    python tools/decomp_status.py --all      todos os módulos
    python tools/decomp_status.py --missing Vec_    funções ainda não portadas com esse prefixo
"""
import argparse
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = re.compile(r"//\s*\[decomp\]\s*0x([0-9a-fA-F]{8})\s+([A-Za-z_][A-Za-z0-9_:]*)")


def module(name):
    # "MemCard_SAVE_DATA" -> MemCard, "GameFlow_Main" -> GameFlow; sem "_" ou com prefixo minúsculo,
    # corta na primeira palavra: "RotMatrixX" -> Rot, "psiMem_Init" -> psi.
    n = name.split("::")[-1].lstrip("_")
    if n[:1].isupper() and "_" in n:
        return n.split("_")[0]
    m = re.match(r"([A-Za-z0-9]+?)(?:_|(?=[A-Z][a-z])|$)", n)
    return m.group(1) if m else name


def load_functions():
    path = os.path.join(ROOT, "out", "functions.tsv")
    if not os.path.exists(path):
        sys.exit("out/functions.tsv não existe: rode o ExportAllCalls.java (docs/setup.md)")
    funcs = {}
    for line in open(path, encoding="utf-8"):
        addr, size, name, _orig = line.rstrip("\n").split("\t")
        funcs[int(addr, 16)] = (name, int(size))
    return funcs


def load_layers():
    path = os.path.join(ROOT, "out", "layers.json")
    if not os.path.exists(path):
        return {}
    data = json.load(open(path, encoding="utf-8"))
    return {int(v["addr"], 16): v["layer"] for v in data.values()}


def scan_markers():
    found = {}
    for base, _dirs, files in os.walk(os.path.join(ROOT, "src")):
        for f in files:
            if not f.endswith(".cpp"):  # marcadores ficam nas definições; exemplos em .h não contam
                continue
            path = os.path.join(base, f)
            for n, line in enumerate(open(path, encoding="utf-8"), 1):
                m = MARK.search(line)
                if m:
                    found.setdefault(int(m.group(1), 16), []).append((m.group(2), os.path.relpath(path, ROOT), n))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--missing", metavar="PREFIXO")
    args = ap.parse_args()

    funcs = load_functions()
    layers = load_layers()
    marks = scan_markers()

    problems = []
    for addr, places in marks.items():
        if addr not in funcs:
            problems.append(f"endereço 0x{addr:08x} não é início de função: {places}")
        elif any(name.split("::")[-1] != funcs[addr][0].split("::")[-1] for name, _f, _l in places):
            problems.append(f"0x{addr:08x} é {funcs[addr][0]}, mas o marcador diz {places}")
        if len(places) > 1:
            problems.append(f"0x{addr:08x} marcado mais de uma vez: {places}")

    if args.missing:
        for addr, (name, size) in sorted(funcs.items()):
            if name.startswith(args.missing) and addr not in marks:
                print(f"0x{addr:08x} {size:6d} B  {name}")
        return

    # Considera só o código do jogo (fora SDK e runtime) no total.
    game = {a: v for a, v in funcs.items() if layers.get(a, "logica") not in ("sdk", "runtime")}
    done = {a for a in marks if a in game}
    total_b = sum(s for _n, s in game.values())
    done_b = sum(game[a][1] for a in done)
    print(f"Funções do jogo portadas: {len(done)} de {len(game)} "
          f"({done_b:,} de {total_b:,} bytes, {100 * done_b / total_b:.2f}%)")
    if layers:
        by_layer = collections.Counter(layers.get(a, "?") for a in done)
        print("Por camada: " + ", ".join(f"{k} {v}" for k, v in sorted(by_layer.items())))
    print()

    per = collections.defaultdict(lambda: [0, 0, 0, 0])  # portadas, total, bytes portados, bytes
    for a, (name, size) in game.items():
        p = per[module(name)]
        p[1] += 1
        p[3] += size
        if a in done:
            p[0] += 1
            p[2] += size
    rows = sorted(per.items(), key=lambda kv: (-kv[1][2], -kv[1][3]))
    print(f"{'módulo':20s} {'funções':>12s} {'bytes':>18s}")
    for mod, (n, t, b, tb) in rows:
        if not args.all and n == 0:
            continue
        print(f"{mod:20s} {n:5d} / {t:<5d} {b:8,d} / {tb:<8,d} {'completo' if n == t else ''}")

    if problems:
        print("\nProblemas nos marcadores:")
        for p in problems:
            print("  " + p)
        sys.exit(1)


if __name__ == "__main__":
    main()
