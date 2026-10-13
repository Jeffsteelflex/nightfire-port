#!/usr/bin/env python3
"""Importa os nomes do Driving.xbe e do DRIVING.ELF do projeto nightfire-cxbx (NightfireResearch) para os nossos projetos
do Ghidra, pelos nossos pares Xbox <-> PS2 (docs/driving.md, "O nightfire-cxbx").

O nightfire-cxbx porta a versão de Xbox rodando o x86 original e dá nome às funções do Driving.xbe no Ghidra deles
(tools/functions_driving.json, sincronizado do projeto) e, numa segunda frente, às do DRIVING.ELF do PS2 (os lotes
driving-symbol-matching/results/batches/batch-P*.json: os nomes da planilha de símbolos do PS2, que vem do DRIVING.SYM,
postos nos endereços do ELF de varejo pelo alinhamento deles ou lendo o código). O Driving.xbe deles tem o mesmo
leiaute do nosso (NTSC-U): 6.924 dos 7.050 inícios de função que o nosso Ghidra acha estão na lista deles. Só se lê o
clone (dado de fora, só leitura); nada dele é executado nem copiado para o repositório: o que sai daqui são tabelas em
out/ (nomes e endereços são fatos sobre o código; o crédito vai no comentário de cada nome).

Entradas (no out/ do checkout com os dados, NF_DATA ou a raiz do repositório):
  out/ref/nightfire-cxbx           o clone (ou --ref)
  out/driving/sym_map.tsv          tools/driving_sym.py match (os nossos nomes do DRIVING.SYM e a confiança)
  out/xbox/match.tsv               tools/xbox_match.py match (os nossos pares Xbox <-> PS2)
  out/xbox/xbox_names.tsv          os nomes que o nosso projeto do Xbox já tem

  python tools/cxbx_names.py compare   compara os nomes e os pares deles com os nossos (out/driving/cxbx_compare.txt)
  python tools/cxbx_names.py pairs     out/xbox/cxbx_pairs.tsv: pares pelo nome igual e único dos dois lados (o nome do
                                       Xbox é deles; o do PS2, nosso de confiança >= 2 ou o lote P deles), para
                                       tools/xbox_match.py match --cxbx
  python tools/cxbx_names.py names     out/driving/cxbx_names.tsv (o PS2) e out/xbox/cxbx_xbox_names.tsv (o Xbox), para
                                       tools/ghidra_driving.sh names e tools/ghidra_xbox.sh names
  python tools/cxbx_names.py test      testes com um clone e tabelas montados à mão

As regras do PS2 (por endereço):
  - nome nosso de confiança 2 ou 3 (driving_sym.py) e o lote deles concorda: nada a fazer;
  - nome nosso de confiança 2 ou 3 e o lote deles põe outro nome: disputa. Se o nosso veio só do alinhamento pelo
    tamanho ("tamanho", "tamanho exato") e o deles da leitura do código ("body review", "window review", "corrected")
    ou o par do Xbox tem o nome deles, o nome deles entra (F) e o nosso fica no comentário; senão o nosso fica e o deles
    vai para o comentário (A, acrescentado);
  - sem nome nosso de confiança 2 ou 3: o nome do lote deles (F); se não houver, o nome que eles deram ao par do Xbox
    (par de confiança >= 2, ou de confiança 1 quando bate com a nossa sugestão de confiança 1): F; outro par de
    confiança 1: só comentário (A);
  - as variáveis globais que eles puseram no ELF (results/globals/ps2-map.json, "exact"/"snapped"/"predicted"): D.
As regras do Xbox: o nome deles onde o nosso projeto do Xbox não tem nome (F); onde tem outro, o nosso fica e o deles vai
para o comentário (A); os rótulos das tabelas virtuais deles (results/globals/vt1-xbox.json): D.
"""

import argparse
import collections
import glob
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driving_sym  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CREDIT = "nightfire-cxbx (NightfireResearch)"

UNNAMED = re.compile(r"^(FUN_|thunk_FUN_|LAB_|SUB_|switchD_)")
# Nomes que só existem no Xbox (o que o MSVC gera e o GCC 2.95 não): não descrevem uma função do PS2.
XBOX_ONLY = re.compile(r"(scalar_deleting_destructor|vector_deleting_destructor|_Unwind_|_Catch_|__ehhandler|"
                       r"_SEH_|__CxxFrameHandler|`|^dsnd|::dsnd|XBox|Xbox|^D3D|^XG)")
XBOX_ONLY_NS = ("D3D8", "DSOUND", "XAPILIB", "XGRAPHC", "D3DX", "xboxkrnl.exe", "XMV", "DOLBY")
REVIEW = re.compile(r"(body review|window review|corrected|hand-checked|by hand)", re.I)
SIZE_ONLY = ("tamanho", "tamanho exato")


def data_root():
    return os.environ.get("NF_DATA", ROOT)


# ---------------------------------------------------------------- nomes

def canon(name):
    """Forma comparável de um nome: sem parâmetros, com os caracteres que o Ghidra recusa trocados como no
    driving_sym.ghidra_name, e 'global constructors keyed to X' = 'X_global_ctors' (a convenção deles)."""
    if not name:
        return ""
    for pre, suf in (("global constructors keyed to ", "_global_ctors"), ("global destructors keyed to ", "_global_dtors")):
        if name.startswith(pre):
            ns, nm = driving_sym.ghidra_name(name[len(pre):])
            return ("%s::%s" % (ns, nm) if ns else nm) + suf
    ns, nm = driving_sym.ghidra_name(name)
    return "%s::%s" % (ns, nm) if ns else nm


def short(c):
    """O último componente de um nome canônico (para comparar quando um lado não tem o namespace)."""
    return driving_sym.split_scope(c)[-1] if c else ""


def same_name(a, b):
    ca, cb = canon(a), canon(b)
    return bool(ca) and (ca == cb or (("::" not in ca or "::" not in cb) and short(ca) == short(cb)))


def xbox_name_usable(name, namespace=""):
    if not name or UNNAMED.match(name.split("::")[-1]) or XBOX_ONLY.search(name):
        return False
    return namespace not in XBOX_ONLY_NS and not name.startswith(XBOX_ONLY_NS)


def split_for_ghidra(name):
    """(namespace, nome) para o ApplySymbolTable."""
    c = canon(name)
    parts = driving_sym.split_scope(c)
    return "::".join(parts[:-1]), parts[-1]


# ---------------------------------------------------------------- leitura: o nosso lado

def load_sym_map(path):
    """Endereço do ELF -> (nome completo do SYM, confiança, método), o de maior confiança por endereço."""
    out = {}
    with open(path, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        ix = {k: i for i, k in enumerate(head)}
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < len(head) or p[ix["elf"]] in ("", "-"):
                continue
            a, c = int(p[ix["elf"]], 16), int(p[ix["confianca"]])
            if a in out and out[a][1] >= c:
                continue
            out[a] = (p[ix["nome"]], c, p[ix["metodo"]])
    return out


def load_match(path):
    """Pares do xbox_match.py: ps2 -> (xbox, confiança, método)."""
    out = {}
    with open(path, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        ix = {k: i for i, k in enumerate(head)}
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < len(head):
                continue
            out[int(p[ix["ps2"]], 16)] = (int(p[ix["xbox"]], 16), int(p[ix["confianca"]]), p[ix["metodo"]])
    return out


def load_names_tsv(path):
    """Uma tabela do ApplySymbolTable: endereço -> nome com namespace, só as linhas F."""
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 4 and p[1] == "F":
            out[int(p[0], 16)] = "%s::%s" % (p[2], p[3]) if p[2] else p[3]
    return out


# ---------------------------------------------------------------- leitura: o lado deles

def _when(s):
    try:
        return time.mktime(time.strptime(s, "%a %b %d %H:%M:%S %Y"))
    except (ValueError, TypeError):
        return 0.0


def load_their_ps2(ref):
    """Os lotes do DRIVING.ELF (batch-P*.json) aplicados com sucesso, na ordem em que foram aplicados (o último nome
    de um endereço vale): endereço -> {name, sheet, batch, evidence}."""
    d = os.path.join(ref, "driving-symbol-matching", "results", "batches")
    logs = []
    for lp in glob.glob(os.path.join(d, "batch-P*.log.json")):
        lg = json.load(open(lp, encoding="utf-8"))
        logs.append((_when(lg.get("applied")), lp, lg))
    logs.sort(key=lambda t: (t[0], t[1]))
    out = {}
    for _, lp, lg in logs:
        bp = lp[:-len(".log.json")] + ".json"
        if not os.path.exists(bp):
            continue
        batch = json.load(open(bp, encoding="utf-8"))
        if batch.get("program") != "DRIVING.ELF":
            continue
        items = {it["xbox"]: it for it in batch.get("items", [])}
        for it in lg.get("items", []):
            if not it.get("ok"):
                continue
            src = items.get(it["xbox"], {})
            name = src.get("name") or it.get("new_name")
            if not name:
                continue
            ev = src.get("evidence") or [""]
            out[int(it["xbox"], 16)] = {"name": name, "sheet": src.get("sheet_name") or name,
                                        "batch": batch.get("batch", os.path.basename(bp)), "evidence": ev[0]}
    return out


def load_their_xbox(ref):
    """tools/functions_driving.json: endereço -> (nome, namespace)."""
    out = {}
    for f in json.load(open(os.path.join(ref, "tools", "functions_driving.json"), encoding="utf-8")):
        out[int(f["address"], 16)] = (f["name"], f.get("namespace", ""))
    return out


def load_their_ps2_globals(ref):
    p = os.path.join(ref, "driving-symbol-matching", "results", "globals", "ps2-map.json")
    out = []
    if os.path.exists(p):
        for g in json.load(open(p, encoding="utf-8")):
            if g.get("ps2") and g.get("how") and not g["how"].startswith("conflict"):
                out.append((g["ps2"], g["name"], g["how"], g.get("obj") or ""))
    return out


def load_their_xbox_labels(ref):
    p = os.path.join(ref, "driving-symbol-matching", "results", "globals", "vt1-xbox.json")
    out = []
    if os.path.exists(p):
        for lb in json.load(open(p, encoding="utf-8")).get("labels", []):
            out.append((int(lb["address"], 16), lb["name"]))
    return out


# ---------------------------------------------------------------- análise

class Study:
    def __init__(self, sym, match, theirs_ps2, theirs_xbox, our_xbox_names):
        self.sym, self.match = sym, match
        self.tp, self.tx = theirs_ps2, theirs_xbox
        self.ox = our_xbox_names
        self.x2p = {x: p for p, (x, c, m) in match.items()}

    def our_name(self, a, min_conf=2):
        s = self.sym.get(a)
        return s[0] if s and s[1] >= min_conf else None

    def xbox_name_of_pair(self, p):
        m = self.match.get(p)
        if not m:
            return None, None
        n = self.tx.get(m[0])
        if not n or not xbox_name_usable(*n):
            return m, None
        return m, n[0]

    def ps2_disputes(self):
        """(endereço, nosso, deles, evidência deles, veredito)."""
        out = []
        for a, t in sorted(self.tp.items()):
            s = self.sym.get(a)
            if not s or s[1] < 2 or same_name(s[0], t["sheet"]) or same_name(s[0], t["name"]):
                continue
            m, xn = self.xbox_name_of_pair(a)
            if m and m[2] == "nomes cxbx":
                xn = None                     # o par veio do próprio nome: não desempata
            if xn and (same_name(xn, t["name"]) or same_name(xn, t["sheet"])):
                why = "o par do Xbox tem o nome deles"
            elif xn and same_name(xn, s[0]):
                why = "o par do Xbox tem o nosso nome"
            elif REVIEW.search(t["evidence"]):
                why = "eles leram o código"
            else:
                why = "alinhamento contra alinhamento"
            out.append((a, s, t, why))
        return out

    def name_pairs(self):
        """Pares pelo nome igual e único: [(ps2, xbox, nome canônico, origem do nome do PS2, situação)]."""
        ps2 = collections.defaultdict(set)
        origin = {}
        # um endereço em disputa (o nosso nome e o deles diferentes) não dá par: o nome não é certo
        disputed = {a for a, s, t, why in self.ps2_disputes()}
        for a, (n, c, m) in self.sym.items():
            if c >= 2 and a not in disputed:
                k = canon(n)
                ps2[k].add(a)
                origin[(k, a)] = "SYM confiança %d" % c
        for a, t in self.tp.items():
            if a in disputed:
                continue
            for nm in {canon(t["name"]), canon(t["sheet"])}:
                ps2[nm].add(a)
                origin.setdefault((nm, a), "lote %s deles" % t["batch"])
        xb = collections.defaultdict(set)
        for x, (n, ns) in self.tx.items():
            if xbox_name_usable(n, ns):
                xb[canon(n)].add(x)
        out = []
        for k, xs in xb.items():
            ps = ps2.get(k)
            if not ps or len(ps) != 1 or len(xs) != 1:
                continue
            p, x = next(iter(ps)), next(iter(xs))
            if p in self.match:
                st = "confirma" if self.match[p][0] == x else "contradiz (nosso par: %08x)" % self.match[p][0]
            elif x in self.x2p:
                st = "contradiz (o Xbox está no par de %08x)" % self.x2p[x]
            else:
                st = "novo"
            out.append((p, x, k, origin.get((k, p), ""), st))
        return sorted(out)

    def ps2_names(self, globals_=()):
        """As linhas do ApplySymbolTable para o DRIVING.ELF e a contagem por regra."""
        rows, n = [], collections.Counter()
        disputes = {a: (s, t, why) for a, s, t, why in self.ps2_disputes()}
        done = set()
        for a, (s, t, why) in sorted(disputes.items()):
            done.add(a)
            ns, nm = split_for_ghidra(t["name"])
            if s[2] in SIZE_ONLY and why in ("o par do Xbox tem o nome deles", "eles leram o código"):
                rows.append((a, "F", ns, nm, "%s: %s [lote %s: %s]; antes, pelo DRIVING.SYM (%s, confiança %d): %s" % (
                    CREDIT, t["sheet"], t["batch"], t["evidence"], s[2], s[1], s[0])))
                n["disputa: o nome deles (%s)" % why] += 1
            else:
                rows.append((a, "A", "", "", "%s põe aqui %s [lote %s: %s] (%s)" % (
                    CREDIT, t["sheet"], t["batch"], t["evidence"], why)))
                n["disputa: o nosso fica, o deles no comentário (%s)" % why] += 1
        for a, t in sorted(self.tp.items()):
            if a in done or self.our_name(a):
                continue
            done.add(a)
            ns, nm = split_for_ghidra(t["name"])
            s = self.sym.get(a)
            if s and s[1] == 1:
                if same_name(s[0], t["sheet"]) or same_name(s[0], t["name"]):
                    extra, key = "; confirma a sugestão de confiança 1 do driving_sym.py", "lote deles = a nossa sugestão"
                else:
                    extra, key = "; a sugestão de confiança 1 do driving_sym.py era %s" % s[0], "lote deles, outra sugestão nossa"
            else:
                extra, key = "", "lote deles, sem nome nosso"
            rows.append((a, "F", ns, nm, "%s: %s [lote %s: %s]%s" % (CREDIT, t["sheet"], t["batch"], t["evidence"], extra)))
            n[key] += 1
        for p in sorted(self.match):
            if p in done or self.our_name(p):
                continue
            m, xn = self.xbox_name_of_pair(p)
            if not xn:
                continue
            s = self.sym.get(p)
            sug = s[0] if s and s[1] == 1 else None
            agree = sug is not None and same_name(sug, xn)
            note = "%s: o par no Xbox %08x tem o nome %s (par %s, confiança %d)" % (CREDIT, m[0], xn, m[2], m[1])
            if sug:
                note += "; %s a sugestão de confiança 1 do driving_sym.py%s" % (
                    "confirma" if agree else "outra que", "" if agree else " (%s)" % sug)
            if m[1] >= 2 or agree:
                ns, nm = split_for_ghidra(xn)
                rows.append((p, "F", ns, nm, note))
                n["nome do par do Xbox (par de confiança %s)" % ("1, igual à sugestão" if m[1] < 2 else ">= 2")] += 1
            else:
                rows.append((p, "A", "", "", note))
                n["nome do par do Xbox só em comentário (par de confiança 1)"] += 1
            done.add(p)
        for a, name, how, obj in globals_:
            ns, nm = split_for_ghidra(name)
            rows.append((a, "D", ns, nm, "%s: %s (%s; %s)" % (CREDIT, name, obj, how)))
            n["variáveis globais"] += 1
        return rows, n

    def xbox_names(self, labels=()):
        rows, n = [], collections.Counter()
        for x, (name, ns) in sorted(self.tx.items()):
            if UNNAMED.match(name.split("::")[-1]):
                continue
            ours = self.ox.get(x)
            if ours is None:
                gns, gnm = split_for_ghidra(name)
                rows.append((x, "F", gns, gnm, "%s: %s" % (CREDIT, name)))
                n["nome deles, sem nome nosso"] += 1
            elif same_name(ours, name):
                n["igual ao nosso"] += 1
            else:
                rows.append((x, "A", "", "", "%s chama esta função de %s" % (CREDIT, name)))
                n["outro nome: o nosso fica, o deles no comentário"] += 1
        for x, name in labels:
            ns, nm = split_for_ghidra(name)
            rows.append((x, "D", ns, nm, "%s: %s" % (CREDIT, name)))
            n["rótulos (tabelas virtuais)"] += 1
        return rows, n


def write_rows(path, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for a, kind, ns, nm, note in rows:
            f.write("%08x\t%s\t%s\t%s\t%s\n" % (a, kind, ns, nm, note.replace("\t", " ").replace("\n", " ")))


# ---------------------------------------------------------------- comandos

def paths(args):
    root = data_root()
    ref = args.ref or os.path.join(root, "out", "ref", "nightfire-cxbx")
    return root, ref


def load_study(args):
    root, ref = paths(args)
    match_path = args.match or os.path.join(root, "out", "xbox", "match.tsv")
    st = Study(load_sym_map(os.path.join(root, "out", "driving", "sym_map.tsv")), load_match(match_path),
               load_their_ps2(ref), load_their_xbox(ref),
               load_names_tsv(os.path.join(root, "out", "xbox", "xbox_names.tsv")))
    return root, ref, st


def cmd_compare(args):
    root, ref, st = load_study(args)
    lines = []
    out = lambda s: (print(s), lines.append(s))
    named_x = sum(1 for n, ns in st.tx.values() if not UNNAMED.match(n.split("::")[-1]))
    out("deles: %d funções no Driving.xbe (%d com nome), %d endereços do DRIVING.ELF nos lotes P aplicados"
        % (len(st.tx), named_x, len(st.tp)))
    c = collections.Counter()
    for a, t in st.tp.items():
        s = st.sym.get(a)
        if not s or s[1] == 0:
            c["sem nome nosso"] += 1
        elif s[1] == 1:
            c["sugestão nossa (confiança 1) %s" % ("igual" if same_name(s[0], t["sheet"]) or same_name(s[0], t["name"])
                                                    else "diferente")] += 1
        else:
            c["nome nosso (confiança 2-3) %s" % ("igual" if same_name(s[0], t["sheet"]) or same_name(s[0], t["name"])
                                                 else "diferente")] += 1
    out("o DRIVING.ELF: os lotes deles contra o driving_sym.py")
    for k in sorted(c):
        out("  %-48s %d" % (k, c[k]))
    pc = collections.Counter()
    for p, (x, conf, meth) in st.match.items():
        s = st.sym.get(p)
        tn = st.tx.get(x)
        if not s or s[1] < 2 or not tn or not xbox_name_usable(*tn):
            pc[(conf, "sem nome (PS2 de confiança 2-3, Xbox deles) de um dos lados")] += 1
        else:
            pc[(conf, "igual" if same_name(s[0], tn[0]) else "diferente")] += 1
    out("os nossos %d pares contra o nome que eles deram à função do Xbox (pela confiança do par):" % len(st.match))
    for k in sorted(pc, key=lambda k: (-k[0], k[1])):
        out("  par de confiança %d, %-58s %d" % (k[0], k[1], pc[k]))
    pr = st.name_pairs()
    pc = collections.Counter(r[4].split(" (")[0] for r in pr)
    out("pares pelo nome igual e único: %s" % ", ".join("%s %d" % kv for kv in sorted(pc.items())))
    disputes = st.ps2_disputes()
    out("disputas no DRIVING.ELF (nome nosso de confiança 2-3, outro nome no lote deles): %d" % len(disputes))
    by = collections.Counter((s[2], why) for a, s, t, why in disputes)
    for (meth, why), k in sorted(by.items(), key=lambda kv: -kv[1]):
        out("  nosso por %-14s %-34s %d" % (meth, why, k))
    for a, s, t, why in disputes:
        lines.append("  %08x  nosso: %s [%s %d]  deles: %s [%s]  (%s)" % (a, s[0], s[2], s[1], t["sheet"],
                                                                         t["evidence"][:90], why))
    path = os.path.join(root, "out", "driving", "cxbx_compare.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("lista inteira em %s" % path)


def cmd_pairs(args):
    root, ref, st = load_study(args)
    pr = st.name_pairs()
    path = os.path.join(root, "out", "xbox", "cxbx_pairs.tsv")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("ps2\txbox\tnome\torigem_ps2\tsituacao\n")
        for p, x, k, o, s in pr:
            f.write("%08x\t%08x\t%s\t%s\t%s\n" % (p, x, k, o, s))
    c = collections.Counter(r[4].split(" (")[0] for r in pr)
    print("%s: %d pares (%s)" % (path, len(pr), ", ".join("%s %d" % kv for kv in sorted(c.items()))))


def cmd_names(args):
    root, ref, st = load_study(args)
    rows, n = st.ps2_names(load_their_ps2_globals(ref))
    p1 = os.path.join(root, "out", "driving", "cxbx_names.tsv")
    write_rows(p1, rows)
    print("%s: %d linhas" % (p1, len(rows)))
    for k in sorted(n):
        print("  %-66s %d" % (k, n[k]))
    rows, n = st.xbox_names(load_their_xbox_labels(ref))
    p2 = os.path.join(root, "out", "xbox", "cxbx_xbox_names.tsv")
    write_rows(p2, rows)
    print("%s: %d linhas" % (p2, len(rows)))
    for k in sorted(n):
        print("  %-66s %d" % (k, n[k]))


def cmd_test(args):
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, "ref")
        bd = os.path.join(ref, "driving-symbol-matching", "results", "batches")
        gd = os.path.join(ref, "driving-symbol-matching", "results", "globals")
        os.makedirs(bd)
        os.makedirs(gd)
        os.makedirs(os.path.join(ref, "tools"))
        # dois lotes do PS2: o segundo, aplicado depois, corrige um endereço do primeiro
        items1 = [
            {"xbox": "0x00100000", "name": "A::f", "sheet_name": "A::f(int)", "evidence": ["sheet row 1, placed by align"]},
            {"xbox": "0x00100010", "name": "A::g", "sheet_name": "A::g(void)", "evidence": ["sheet row 2, placed by align"]},
            {"xbox": "0x00100020", "name": "B::h", "sheet_name": "B::h(void)", "evidence": ["sheet row 3, placed by align"]},
            {"xbox": "0x00100030", "name": "B::k", "sheet_name": "B::k(void)", "evidence": ["sheet row 4, body review: x"]},
            {"xbox": "0x00100040", "name": "C::m", "sheet_name": "C::m(void)", "evidence": ["sheet row 5, placed by align"]},
            {"xbox": "0x00100050", "name": "C::bad", "sheet_name": "C::bad(void)", "evidence": ["x"]},
        ]
        items2 = [{"xbox": "0x00100020", "name": "B::h2", "sheet_name": "B::h2(void)", "evidence": ["sheet row 3b"]}]
        for nm, items, when, oks in (("P001", items1, "Fri Sep 25 10:00:00 2026", [True] * 5 + [False]),
                                     ("P002", items2, "Fri Sep 25 11:00:00 2026", [True])):
            json.dump({"batch": nm, "program": "DRIVING.ELF", "items": items},
                      open(os.path.join(bd, "batch-%s.json" % nm), "w"))
            json.dump({"batch": nm, "program": "DRIVING.ELF", "applied": when,
                       "items": [{"xbox": it["xbox"], "ok": ok} for it, ok in zip(items, oks)]},
                      open(os.path.join(bd, "batch-%s.log.json" % nm), "w"))
        # um lote do Xbox não entra no PS2
        json.dump({"batch": "001", "items": [{"xbox": "0x00100060", "name": "Z::z"}]},
                  open(os.path.join(bd, "batch-001.json"), "w"))
        xfun = [("0x00011000", "A::f", "A"), ("0x00011010", "D::d", "D"), ("0x00011020", "FUN_00011020", "Global"),
                ("0x00011030", "E::e", "E"), ("0x00011040", "C::m", "C"), ("0x00011050", "A::scalar_deleting_destructor", "A"),
                ("0x00011060", "D3DDevice_SetRenderState", "D3D8"), ("0x00011070", "Q::q", "Q"),
                ("0x00011090", "A::g", "A")]
        json.dump([{"name": n, "address": a, "namespace": ns} for a, n, ns in xfun],
                  open(os.path.join(ref, "tools", "functions_driving.json"), "w"))
        json.dump([{"name": "gTable", "ps2": 0x300000, "how": "exact", "obj": "X.obj"},
                   {"name": "gLost", "ps2": None, "how": "conflict (lost 0x1 to y)"}], open(os.path.join(gd, "ps2-map.json"), "w"))
        json.dump({"labels": [{"address": "0x00180000", "name": "A::vftable"}]}, open(os.path.join(gd, "vt1-xbox.json"), "w"))
        tp = load_their_ps2(ref)
        assert set(tp) == {0x100000, 0x100010, 0x100020, 0x100030, 0x100040}, sorted(tp)
        assert tp[0x100020]["name"] == "B::h2"          # o lote aplicado por último vale
        sym = {0x100000: ("A::f(int)", 3, "type_info"),          # concorda
               0x100010: ("A::other(void)", 3, "tamanho exato"),  # disputa automática: o nosso fica
               0x100030: ("B::wrong(void)", 2, "tamanho"),        # disputa: eles leram o código -> o deles
               0x100040: ("C::sug(void)", 1, "alinhamento"),      # o lote deles vence a nossa sugestão
               0x100100: ("D::d(void)", 1, "alinhamento"),        # o par do Xbox confirma a sugestão
               0x100110: ("E::x(void)", 1, "alinhamento"),        # par de confiança 1 que não bate: comentário
               0x100120: ("Q::q(void)", 2, "tamanho")}            # par novo pelo nome
        match = {0x100000: (0x11000, 3, "cadeias"), 0x100100: (0x11010, 1, "ordem das chamadas"),
                 0x100110: (0x11030, 1, "ordem das chamadas"), 0x100040: (0x11040, 2, "vizinhos"),
                 0x100010: (0x11090, 2, "nomes cxbx")}   # par que veio do próprio nome: não desempata
        st = Study(sym, match, tp, load_their_xbox(ref), {0x11000: "A::f", 0x11030: "E::other"})
        d = {a: why for a, s, t, why in st.ps2_disputes()}
        assert d == {0x100010: "alinhamento contra alinhamento", 0x100030: "eles leram o código"}, d
        rows, n = st.ps2_names(load_their_ps2_globals(ref))
        by = {(a, k): (ns, nm, note) for a, k, ns, nm, note in rows}
        assert (0x100000, "F") not in by and (0x100000, "A") not in by
        assert (0x100010, "A") in by and "A::g" in by[(0x100010, "A")][2]
        assert by[(0x100030, "F")][:2] == ("B", "k") and "B::wrong" in by[(0x100030, "F")][2]
        assert by[(0x100020, "F")][:2] == ("B", "h2")
        assert by[(0x100040, "F")][:2] == ("C", "m") and "C::sug" in by[(0x100040, "F")][2]
        assert by[(0x100100, "F")][:2] == ("D", "d") and "confirma" in by[(0x100100, "F")][2]
        assert (0x100110, "A") in by and (0x100110, "F") not in by
        assert by[(0x300000, "D")][:2] == ("", "gTable") and len([r for r in rows if r[1] == "D"]) == 1
        assert all(CREDIT in r[4] for r in rows)
        pr = {(p, x): s for p, x, k, o, s in st.name_pairs()}
        assert pr[(0x100000, 0x11000)] == "confirma"
        assert pr[(0x120 + 0x100000, 0x11070)] == "novo"
        assert pr[(0x100040, 0x11040)] == "confirma"
        assert not any(x == 0x11050 or x == 0x11060 for p, x in pr)   # só do Xbox: fora
        assert not any(p == 0x100010 for p, x in pr)                  # em disputa: sem par pelo nome
        xr, xn = st.xbox_names(load_their_xbox_labels(ref))
        xby = {(a, k): (ns, nm, note) for a, k, ns, nm, note in xr}
        assert (0x11000, "F") not in xby and (0x11000, "A") not in xby       # igual ao nosso
        assert xby[(0x11010, "F")][:2] == ("D", "d")
        assert (0x11030, "A") in xby and "E::e" in xby[(0x11030, "A")][2]   # outro nome: o nosso fica
        assert (0x11020, "F") not in xby
        assert xby[(0x180000, "D")][:2] == ("A", "vftable")
    assert canon("global constructors keyed to WGrid::fgGrid") == canon("WGrid::fgGrid_global_ctors")
    assert same_name("_List_base<ActActor *, allocator<ActActor *> >::clear(void)",
                     "_List_base<ActActor_*,_allocator<ActActor_*>_>::clear")
    assert same_name("main", "main") and not same_name("A::f(int)", "A::g")
    assert not xbox_name_usable("X::scalar_deleting_destructor") and not xbox_name_usable("Foo", "D3D8")
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    for name in ("compare", "pairs", "names"):
        a = sub.add_parser(name)
        a.add_argument("--ref", help="o clone do nightfire-cxbx (padrão: out/ref/nightfire-cxbx)")
        a.add_argument("--match", help="os pares (padrão: out/xbox/match.tsv)")
    sub.add_parser("test")
    args = ap.parse_args()
    cmd = {"compare": cmd_compare, "pairs": cmd_pairs, "names": cmd_names, "test": cmd_test}.get(args.cmd)
    if not cmd:
        ap.print_help()
        return 1
    cmd(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
