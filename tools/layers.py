"""Classifica as funções do ACTION.ELF em camadas e mede a fronteira entre elas.

Entradas (geradas pelos scripts do Ghidra, ver docs/setup.md):
    out/functions.tsv   ExportAllCalls.java: endereço, tamanho, nome, nome original
    out/calls.tsv       ExportAllCalls.java: chamador, chamado
    out/hwuse.tsv       ExportHwUse.java: uso de COP2/VU0, MMI, registradores e scratchpad

Camadas:
    runtime    libc (newlib), libm e runtime do GCC linkados no fim. C padrão: o PC já tem.
    sdk        SDK da Sony e kernel do PS2 (sce*, syscalls, decodificador MPEG/IPU, SIF, memory card),
               linkados no fim a partir de zerobss. Não é portado: é substituído.
    plataforma código do jogo que chama o SDK, acessa registradores ou o scratchpad, tem o prefixo
               psi/PS2 (interface de plataforma da EuroLand), ou só é chamado por plataforma.
               Precisa ser reescrito para PC.
    vu0        lógica que usa a VU0 em modo macro (lqc2/vcallms...) mas não toca o resto do hardware.
               Portável trocando as instruções vetoriais por SSE/C.
    logica     o resto: C/C++ comum, portável por decompilação.

Uso: python tools/layers.py [--json saida.json]
"""
import argparse
import collections
import csv
import json
import os
import re

OUT = "out"


def load():
    funcs = {}
    for line in open(os.path.join(OUT, "functions.tsv"), encoding="utf-8"):
        addr, size, name, orig = line.rstrip("\n").split("\t")
        funcs[addr] = {"addr": addr, "size": int(size), "name": name, "orig": orig}
    calls = collections.defaultdict(set)
    callers = collections.defaultdict(set)
    for line in open(os.path.join(OUT, "calls.tsv"), encoding="utf-8"):
        a, b = line.split()
        calls[a].add(b)
        callers[b].add(a)
    for row in csv.DictReader(open(os.path.join(OUT, "hwuse.tsv"), encoding="utf-8"), delimiter="\t"):
        f = funcs.get(row["addr"])
        if f:
            for k in ("instr", "cop2", "mmi", "hwreg", "spr", "hwlui"):
                f[k] = int(row.get(k) or 0)
    return funcs, calls, callers


def module(name):
    n = name.split("::")[-1]
    m = re.match(r"_*([A-Za-z0-9]+?)(?:_|(?=[A-Z][a-z])|$)", n)
    return m.group(1) if m else n


RUNTIME = re.compile(
    r"^(mem(cpy|move|set|chr|cmp|clr)|str(cat|chr|cmp|cpy|len|ncpy|str|upr)|index|malloc|free|_?malloc_.*|"
    r"__malloc_.*|printf|sprintf|_?vfprintf.*|__sbprintf|fflush|std|_cleanup_r|__sinit|_?localeconv.*|"
    r"_Bfree|_multadd|_hi0bits|_lo0bits|_i2b|_multiply|_pow5mult|_lshift|__mcmp|__mdiff|_d2b|cvt|exponent|"
    r"quorem|_dtoa_r|isinf|isnan|isnanf|finitef|floorf|scalbnf|copysignf|fabsf|tanf|acosf|asinf|atan2f|atanf|"
    r"expf|fmodf|matherr|exit|__swrite|__sseek|__sclose|fptoui|fptodp|dp(add|sub|mul|div|cmp)|litodp|dptoli|"
    r"dptoul|dptofp|__unpack_[fd]|__make_(fp|dp)|_fpadd_parts|__fpcmp_parts_d|__do_global_ctors|__main|"
    r"gcc2_compiled\.|zerobss|_exit|_root|ftoi|printfloat)$")


def classify(funcs, calls):
    sdk_start = min(int(a, 16) for a, f in funcs.items() if f["name"] == "zerobss")
    for a, f in funcs.items():
        f["layer"] = None
        if int(a, 16) >= sdk_start and "__F" not in f["orig"]:
            f["layer"] = "runtime" if RUNTIME.match(f["name"]) else "sdk"
    for a, f in funcs.items():
        if f["layer"]:
            continue
        calls_sdk = any(funcs.get(b, {}).get("layer") == "sdk" for b in calls[a])
        platform_name = re.match(r"^(psi|PS2|Ps2|ps2)", f["name"]) is not None
        if calls_sdk or platform_name or f.get("hwreg", 0) or f.get("spr", 0):
            f["layer"] = "plataforma"
        elif f.get("cop2", 0) or f.get("mmi", 0):
            f["layer"] = "vu0"
        else:
            f["layer"] = "logica"
    # Auxiliares internos: funções chamadas apenas pela plataforma também são plataforma
    # (ex.: DrawThisBox monta pacotes DMA na memória sem tocar registradores).
    callers = collections.defaultdict(set)
    for a, bs in calls.items():
        for b in bs:
            callers[b].add(a)
    changed = True
    while changed:
        changed = False
        for a, f in funcs.items():
            if f["layer"] not in ("logica", "vu0") or not callers[a]:
                continue
            # Matemática e lógica de jogo continuam portáveis mesmo se só a plataforma as chama.
            if re.match(r"^(Vec|Quat|Mat|Ang|Plane|Rot|Anim|Drone|NDrone|ByteCompare|crc32)", f["name"]):
                continue
            # main é plataforma (inicializa o hardware), mas o que ele chama é o fluxo do jogo.
            if all(funcs.get(c, {}).get("layer") == "plataforma" and funcs[c]["name"] != "main"
                   for c in callers[a]):
                f["layer"] = "plataforma"
                f["propagated"] = True
                changed = True
    return sdk_start


def report(funcs, calls, callers):
    sdk_start = classify(funcs, calls)
    by = collections.defaultdict(list)
    for f in funcs.values():
        by[f["layer"]].append(f)
    total = sum(f["size"] for f in funcs.values())
    lines = [f"Início do código de biblioteca (zerobss): 0x{sdk_start:08x}", ""]
    lines.append("| Camada | Funções | Bytes | % do código |")
    lines.append("|---|---|---|---|")
    for layer in ("logica", "vu0", "plataforma", "runtime", "sdk"):
        fs = by[layer]
        size = sum(f["size"] for f in fs)
        lines.append(f"| {layer} | {len(fs)} | {size:,} | {100 * size / total:.1f}% |")
    lines.append("")

    # Plataforma agrupada por módulo
    mods = collections.Counter()
    sizes = collections.Counter()
    for f in by["plataforma"]:
        mods[module(f["name"])] += 1
        sizes[module(f["name"])] += f["size"]
    lines.append("Plataforma por módulo (maiores):")
    for m, n in sorted(mods.items(), key=lambda kv: -sizes[kv[0]])[:30]:
        lines.append(f"  {m:20s} {n:4d} funções {sizes[m]:7,d} bytes")
    lines.append("")

    # Superfície: funções de plataforma chamadas por lógica/vu0
    surface = collections.Counter()
    for a, f in funcs.items():
        if f["layer"] in ("logica", "vu0"):
            for b in calls[a]:
                g = funcs.get(b)
                if g and g["layer"] == "plataforma":
                    surface[b] += 1
    lines.append(f"Superfície: {len(surface)} funções de plataforma são chamadas pela lógica.")
    for b, n in surface.most_common(40):
        lines.append(f"  {funcs[b]['name']:40s} chamada por {n} funções de lógica")
    lines.append("")

    # SDK usado
    sdk_used = collections.Counter()
    for a, f in funcs.items():
        if f["layer"] == "plataforma":
            for b in calls[a]:
                g = funcs.get(b)
                if g and g["layer"] == "sdk":
                    sdk_used[module(g["name"])] += 1
    lines.append("Bibliotecas do SDK chamadas pela plataforma (por prefixo):")
    lines.append("  " + ", ".join(f"{m} ({n})" for m, n in sdk_used.most_common(40)))
    lines.append("")

    vu0 = sorted(by["vu0"], key=lambda f: -f.get("cop2", 0))
    lines.append(f"Lógica com VU0 inline: {len(vu0)} funções; as que mais usam:")
    lines.append("  " + ", ".join(f"{f['name']} ({f.get('cop2', 0)})" for f in vu0[:25]))
    prop = sorted((f for f in by["plataforma"] if f.get("propagated")), key=lambda f: f["name"])
    lines.append("")
    lines.append(f"Classificadas como plataforma só por serem chamadas pela plataforma: {len(prop)}")
    lines.append("  " + ", ".join(f["name"] for f in prop))
    return "\n".join(lines), by, surface


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json")
    args = ap.parse_args()
    funcs, calls, callers = load()
    text, by, surface = report(funcs, calls, callers)
    print(text)
    if args.json:
        # Indexado por endereço: nomes sobrecarregados (ex.: Vec_MulR32) aparecem mais de uma vez.
        json.dump({f["addr"]: {"addr": f["addr"], "name": f["name"], "size": f["size"], "layer": f["layer"]}
                   for f in funcs.values()},
                  open(args.json, "w", encoding="utf-8"), indent=0, ensure_ascii=False)


if __name__ == "__main__":
    main()
