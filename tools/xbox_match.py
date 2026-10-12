#!/usr/bin/env python3
"""Casa as funções do Driving.xbe (Xbox: x86, MSVC 7) com as do DRIVING.ELF (PS2: MIPS, GCC 2.95), para usar a versão
de Xbox como referência de leitura do porte das missões de carro (docs/driving.md, "A versão de Xbox").

As duas compilações saem do mesmo código-fonte (d:\\ToBurn\\BondPS2 e D:\\ToBurn\\BondXbox), mas não têm um byte de
código em comum. O casamento usa o que sobrevive à troca de compilador e de processador:
  1. cadeias lidas pela função (com peso pela raridade nos dois lados);
  2. constantes float (o ajuste da física, números mágicos) e inteiras raras;
  3. o grafo de chamadas: os vizinhos (quem chama, quem é chamado) de pares já casados votam, e as chamadas de um par
     casado são alinhadas na ordem do código;
  4. as tabelas virtuais: a mesma classe tem os métodos na mesma ordem (no GCC 2.95 a primeira entrada é a type_info;
     o MSVC foi usado sem RTTI);
  5. as variáveis globais casadas pelos pares que as usam (o ajuste da física: os mesmos valores nos dois lados).
Confiança: 3 = cadeias raras, mútuas e com margem, ou par conferido lendo o código (MANUAL); 2 = constantes raras,
votos de vizinhos que concordam, entrada de tabela virtual de uma tabela casada; 1 = só a posição entre duas chamadas
casadas (sugestão). `--evaluate` esconde as cadeias de metade dos pares por cadeias e mede quantos voltam ao mesmo par.
As funções de vetor só do Xbox (XBOX_LABELS) recebem nomes nossos no namespace XboxMath.

Entradas (no out/ do checkout com os dados, NF_DATA ou a raiz do repositório):
  out/driving/features.tsv     tools/ghidra_driving.sh features (o PS2, com os nomes do DRIVING.SYM aplicados)
  out/xbox/Driving_features.tsv  tools/ghidra_xbox.sh features
  DRIVING.ELF, out/xbox/Driving.exe   para ler as constantes que as funções carregam da memória
  out/driving/sym_map.tsv, DRIVING/DRIVING.SYM   os módulos (tools/driving_sym.py)

  python tools/xbox_match.py match [--evaluate]   out/xbox/match.tsv, out/xbox/xbox_names.tsv (para
                                                  tools/ghidra_xbox.sh names) e as estatísticas por módulo
  python tools/xbox_match.py show <nome|0xendereço_ps2>...   o par e as provas de cada função
  python tools/xbox_match.py test                 testes com dois programas montados à mão
"""

import argparse
import bisect
import collections
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import driving_sym  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PE_BASE = 0x10000

# Pares conferidos lendo o código dos dois lados (docs/driving.md, "A versão de Xbox"), que o casamento automático não
# acha: (endereço no DRIVING.ELF, endereço no Driving.xbe, nota). Entram antes de tudo, com confiança 3.
MANUAL = [
    (0x1F8CF0, 0xB0EC0, "o passo do corpo rígido: no Xbox uma função só faz o que no PS2 são "
                        "Timestep_LoadAndStartVU0 (0x1f89e0), o microcódigo VU0MICRO_UpdatePositionAndOrientation e "
                        "RigidBody::Timestep_CollectVU0data"),
    (0x1FCFC0, 0xB09D0, "RigidBody::ControlSleep: o mesmo algoritmo e os mesmos deslocamentos"),
    (0x1F91E8, 0xAE150, "RigidBody::ResolveLeverForces: no Xbox o laço das alavancas que no PS2 é o microcódigo "
                        "VU0MICRO_ResolveLeverForces (com RigidBody::Resolve_CollectVU0data)"),
    (0x1F8088, 0xAD130, "RigidBody::ApplyAngularDamping: idêntica (0.995 no momento angular)"),
    (0x22F4A0, 0xD0D60, "AddCurtain: guarda a cortina de visibilidade (até 63, registros de 0x40 bytes); no PS2 a "
                        "carga vai para a VU0"),
    (0x22F5A8, 0xD0F50, "o teste de uma esfera contra as cortinas, que no PS2 é o microcódigo "
                        "VU0MICRO_CheckAgainstVisCurtains (sem nome no SYM: IsVisibleAgainstCurtains)"),
]

# Funções de vetor e matriz que o Xbox chama e que no PS2 são VU0 em linha (sem par e sem nome no SYM): nomes nossos,
# no namespace XboxMath, lidos no código (COORD3/COORD4 com o w preservado na saída; matrizes 4x4 por linhas, vetor à
# esquerda como no motor). Ajudam a ler a física do Xbox; não vêm do jogo.
XBOX_LABELS = [
    (0x0005CA40, "Vec3Copy", "dst = src (3 floats)"),
    (0x000BCE50, "ClampSymmetric", "v > lim ? val : v < -lim ? -val : v (o Util_Bound do jogo)"),
    (0x001134BA, "Matrix4Multiply", "out = a * b (4x4)"),
    (0x001135B8, "Matrix4Transpose", "out = transposta(m)"),
    (0x00113603, "QuatToMatrix", "matriz de rotação do quatérnio (x, y, z, w)"),
    (0x00115AB0, "Vec3Add", "out.xyz = a + b"),
    (0x00115AD0, "Vec3Sub", "out.xyz = a - b"),
    (0x00115AF0, "Vec4Add", "out = a + b"),
    (0x00115B30, "Vec3Dot", "a . b"),
    (0x00115BA0, "Sqrt", "sqrt (x87)"),
    (0x00115BF0, "Vec3Length", "|v|"),
    (0x00115C20, "Vec3LengthSq", "|v|^2"),
    (0x00115C80, "Vec4Copy", "dst = src"),
    (0x00115CC0, "Vec3Cross", "out.xyz = a x b"),
    (0x00115D40, "Vec3CrossNormalize", "normaliza(a x b) com rsqrtss (SSE, aproximado)"),
    (0x00115E20, "Vec3Normalize", "v / |v| com rsqrtss (SSE, aproximado: 12 bits)"),
    (0x00115F00, "Vec3Scale", "out.xyz = v * s"),
    (0x00115F30, "Vec4Scale", "out = v * s"),
    (0x00115F80, "Vec3ScaleAdd", "out.xyz = a * s + b"),
    (0x00116060, "Vec3LengthXZ", "sqrt(x^2 + z^2)"),
    (0x00116190, "Matrix4Diagonal", "diag(x, y, z, 1)"),
    (0x00116240, "Vec3TransformPoint", "out = (v, 1) * M (linha 3 = translação)"),
    (0x001162D0, "Vec3TransformVector", "out = v * M (só a 3x3)"),
]


class Func:
    __slots__ = ("addr", "size", "name", "blocks", "ins", "strings", "floats", "ints", "calls", "callers", "vptrs",
                 "globals", "feat")

    def __init__(self, addr, size, name, blocks, ins):
        self.addr, self.size, self.name, self.blocks, self.ins = addr, size, name, blocks, ins
        self.strings = collections.Counter()
        self.floats = set()
        self.ints = set()
        self.calls = []        # chamados distintos, na ordem da primeira chamada
        self.callers = set()
        self.vptrs = []
        self.globals = set()   # endereços de variáveis globais (dados graváveis) que a função lê ou grava
        self.feat = None       # {chave: peso}


def _unesc(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"t": "\t", "n": "\n", "r": "\r", "\\": "\\"}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def float_key(bits):
    """A chave de uma constante float32 (6 algarismos), ou None se o padrão não parece um float de jogo."""
    bits &= 0xFFFFFFFF
    exp = (bits >> 23) & 0xFF
    if bits in (0, 0x80000000) or not 0x60 <= exp <= 0xA0:
        return None
    v = struct.unpack("<f", struct.pack("<I", bits))[0]
    return "f%.6g" % v


def double_key(raw8):
    v = struct.unpack("<d", raw8)[0]
    if v == 0 or v != v or abs(v) > 3e38:
        return None
    try:
        bits = struct.unpack("<I", struct.pack("<f", v))[0]
    except OverflowError:
        return None
    return float_key(bits)


class Program:
    """As funções de um lado, com cadeias, constantes e chamadas; `read(addr, n)` lê o binário."""

    def __init__(self, features_path, read, addr_range, mips, data_ranges=()):
        self.funcs = {}
        self.tables = []
        self.mips = mips
        lo, hi = addr_range
        cur = None
        for line in open(features_path, encoding="utf-8"):
            p = line.rstrip("\n").split("\t")
            k = p[0]
            if k == "F":
                cur = Func(int(p[1], 16), int(p[2]), _unesc(p[3]), int(p[4]), int(p[5]))
                self.funcs[cur.addr] = cur
                seen = set()
            elif k == "T":
                self.tables.append((int(p[1], 16), [int(x, 16) for x in p[2].split(",")]))
            elif cur is None:
                continue
            elif k == "C":
                a = int(p[1], 16)
                if a not in seen:
                    seen.add(a)
                    cur.calls.append(a)
            elif k == "S":
                cur.strings[_unesc(p[1])] += 1
            elif k == "X":
                fk = float_key(int(p[1], 16))
                if fk:
                    cur.floats.add(fk)
            elif k == "K":
                v = int(p[1], 16)
                fk = float_key(v) if v >= 0x30000000 else None
                if fk and not mips:
                    cur.floats.add(fk)       # x86: mov dword ptr [...], 3f800000h
                elif v >= 0x100 and not lo <= v < hi and not fk:
                    cur.ints.add("i%x" % v)
            elif k == "D":
                a, size = int(p[1], 16), int(p[2])
                if any(d0 <= a < d1 for d0, d1 in data_ranges):
                    cur.globals.add(a)
                    continue
                if size == 8:
                    raw = read(a, 8)
                    fk = double_key(raw) if raw and len(raw) == 8 else None
                else:
                    raw = read(a, 4)
                    fk = float_key(struct.unpack("<I", raw)[0]) if raw and len(raw) == 4 else None
                if fk:
                    cur.floats.add(fk)
            elif k == "V":
                cur.vptrs.append(int(p[1], 16))
        for f in self.funcs.values():
            f.calls = [c for c in f.calls if c in self.funcs and c != f.addr]
            for c in f.calls:
                self.funcs[c].callers.add(f.addr)


def compute_weights(ps2, xbox):
    """Peso de cada característica = log(N / df) do lado onde ela é mais comum; as que só um lado tem não contam."""
    dfp, dfx = collections.Counter(), collections.Counter()
    raw = {}
    for side, df in ((ps2, dfp), (xbox, dfx)):
        for f in side.funcs.values():
            keys = set("s" + s for s in f.strings) | f.floats | f.ints
            raw[(id(side), f.addr)] = keys
            df.update(keys)
    np_, nx = len(ps2.funcs), len(xbox.funcs)
    w = {}
    RARE.clear()
    for k in set(dfp) & set(dfx):
        if dfp[k] <= 3 and dfx[k] <= 3:
            RARE.add(k)
        wt = min(math.log(np_ / dfp[k]), math.log(nx / dfx[k]))
        if k[0] == "i":
            wt *= 0.5          # inteiros: deslocamentos de estrutura e máscaras, que mudam entre as plataformas
        elif k[0] == "f":
            wt *= 0.8
        if wt > 0.3:
            w[k] = wt
    for side in (ps2, xbox):
        for f in side.funcs.values():
            f.feat = {k: w[k] for k in raw[(id(side), f.addr)] if k in w}
    return w, dfp, dfx


RARE = set()   # características que no máximo três funções de cada lado têm


def feature_score(fp, fx):
    """(peso em comum, Jaccard ponderado, número de características raras em comum)."""
    a, b = fp.feat, fx.feat
    if not a or not b:
        return 0.0, 0.0, 0
    if len(a) > len(b):
        a, b = b, a
    shared = 0.0
    rare = 0
    for k, wt in a.items():
        if k in b:
            shared += wt
            if k in RARE:
                rare += 1
    total = sum(fp.feat.values()) + sum(fx.feat.values()) - shared
    return shared, (shared / total if total else 0.0), rare


class Matcher:
    def __init__(self, ps2, xbox, mask=frozenset()):
        self.ps2, self.xbox = ps2, xbox
        self.mask = mask                 # funções do PS2 cujas cadeias e constantes ficam de fora (--evaluate)
        self.p2x, self.x2p = {}, {}
        self.how = {}                    # ps2 -> (método, confiança, nota)
        self.index = collections.defaultdict(list)
        for f in xbox.funcs.values():
            for k in f.feat:
                self.index[k].append(f.addr)
        self.ratio_mu, self.ratio_sd = 0.0, 0.6
        self.gmap = {}                   # global do PS2 -> global do Xbox
        self.use_globals = True

    # ---- utilidades

    def add(self, p, x, method, conf, note=""):
        if p in self.p2x or x in self.x2p:
            return False
        self.p2x[p], self.x2p[x] = x, p
        self.how[p] = (method, conf, note)
        return True

    def size_ok(self, p, x, k=2.5):
        fp, fx = self.ps2.funcs[p], self.xbox.funcs[x]
        r = math.log(max(fx.ins, 1) / max(fp.ins, 1))
        return abs(r - self.ratio_mu) <= k * self.ratio_sd + (1.0 if min(fp.ins, fx.ins) < 12 else 0.0)

    def calibrate(self):
        rs = [math.log(max(self.xbox.funcs[x].ins, 1) / max(self.ps2.funcs[p].ins, 1)) for p, x in self.p2x.items()]
        if len(rs) >= 20:
            rs.sort()
            self.ratio_mu = rs[len(rs) // 2]
            q1, q3 = rs[len(rs) // 4], rs[3 * len(rs) // 4]
            self.ratio_sd = max((q3 - q1) / 1.35, 0.25)

    def candidates(self, fp, max_df=60):
        out = collections.Counter()
        for k in fp.feat:
            lst = self.index.get(k, ())
            if len(lst) <= max_df:
                for x in lst:
                    out[x] += 1
        return out

    def best_by_features(self, fp, pool=None):
        scored = []
        for x in (pool if pool is not None else self.candidates(fp)):
            fx = self.xbox.funcs[x]
            s = feature_score(fp, fx)
            if s[0] > 0:
                scored.append((s[0], s[1], s[2], x))
        scored.sort(reverse=True)
        return scored

    # ---- 1 e 2: cadeias e constantes

    def anchor_features(self):
        """Pares mútuos pelas características; devolve quantos entraram."""
        best_x = {}
        for p, fp in self.ps2.funcs.items():
            if p in self.p2x or p in self.mask or not fp.feat:
                continue
            sc = [s for s in self.best_by_features(fp) if s[3] not in self.x2p]
            if sc:
                best_x[p] = (sc[0], sc[1][0] if len(sc) > 1 else 0.0)
        # o melhor do lado do Xbox
        best_p = {}
        for p, ((s, j, r, x), second) in best_x.items():
            cur = best_p.get(x)
            if cur is None or s > cur[0]:
                best_p[x] = (s, p, cur[0] if cur else 0.0)
            elif s > cur[2]:
                best_p[x] = (cur[0], cur[1], s)
        added = 0
        for p, ((s, j, r, x), second) in sorted(best_x.items(), key=lambda kv: -kv[1][0][0]):
            bp = best_p.get(x)
            if not bp or bp[1] != p:
                continue
            margin = s / max(second, bp[2], 1e-9)
            fp, fx = self.ps2.funcs[p], self.xbox.funcs[x]
            shared_str = [k for k in fp.feat if k[0] == "s" and k in fx.feat]
            rare_str = [k for k in shared_str if k in RARE]
            if rare_str and j >= 0.25 and margin >= 1.5:
                if self.add(p, x, "cadeias", 3, "%d cadeias em comum, J=%.2f" % (len(shared_str), j)):
                    added += 1
            elif r >= 2 and j >= 0.3 and margin >= 2.0 and self.size_ok(p, x):
                if self.add(p, x, "constantes", 2, "%d constantes raras, J=%.2f" % (r, j)):
                    added += 1
        return added

    # ---- 3: vizinhos

    def neighbor_votes(self):
        """Votos dos pares casados nos vizinhos sem par: {ps2: {xbox: [peso, votos]}}. Cada par vota em todas as
        combinações dos seus chamados (e dos que o chamam) sem par, com peso 1 / max(n_ps2, n_xbox): uma vizinhança
        de um contra um vale 1, uma de dez contra dez vale 0,1 para cada combinação (as funções utilitárias, que muitos
        chamam, juntavam votos por acaso)."""
        votes = collections.defaultdict(dict)
        for p, x in list(self.p2x.items()):
            fp, fx = self.ps2.funcs[p], self.xbox.funcs[x]
            # os chamados de p votam nos chamados de x; os que chamam p, nos que chamam x
            for lp, lx in (([c for c in fp.calls if c not in self.p2x], [c for c in fx.calls if c not in self.x2p]),
                           ([c for c in fp.callers if c not in self.p2x],
                            [c for c in fx.callers if c not in self.x2p])):
                if not lp or not lx or len(lp) > 40 or len(lx) > 40:
                    continue
                wt = 1.0 / max(len(lp), len(lx))
                for a in lp:
                    va = votes[a]
                    for b in lx:
                        e = va.get(b)
                        if e is None:
                            va[b] = [wt, 1]
                        else:
                            e[0] += wt
                            e[1] += 1
        return votes

    def propagate_neighbors(self):
        votes = self.neighbor_votes()
        best_for_x = {}
        prop = {}
        for p, cnt in votes.items():
            fp = self.ps2.funcs[p]
            ranked = []
            for x, (wv, v) in cnt.items():
                if x in self.x2p or not self.size_ok(p, x):
                    continue
                s, j, r = feature_score(fp, self.xbox.funcs[x]) if p not in self.mask else (0.0, 0.0, 0)
                ranked.append((v + min(s, 12.0) / 4.0, (wv, v), s, r, x))
            if not ranked:
                continue
            ranked.sort(reverse=True)
            top = ranked[0]
            second = ranked[1][0] if len(ranked) > 1 else 0.0
            prop[p] = (top, second)
            cur = best_for_x.get(top[4])
            if cur is None or top[0] > cur[0]:
                best_for_x[top[4]] = (top[0], p, cur[0] if cur else 0.0)
            elif top[0] > cur[2]:
                best_for_x[top[4]] = (cur[0], cur[1], top[0])
        added = 0
        for p, ((score, (wv, v), s, r, x), second) in sorted(prop.items(), key=lambda kv: -kv[1][0][0]):
            bx = best_for_x.get(x)
            if not bx or bx[1] != p or score <= max(second, bx[2]) + 0.5:
                continue
            agree, ratio = self.agreement(p, x)
            if (v >= 2 and ratio >= 0.4) or (v >= 1 and r >= 1 and ratio >= 0.25):
                if self.add(p, x, "vizinhos", 2, "%d votos, %d vizinhos concordam (%.0f%%), características %.1f"
                            % (v, agree, 100 * ratio, s)):
                    added += 1
        return added

    def agreement(self, p, x):
        """Quantos vizinhos casados de p (chamados e quem chama) têm o par entre os vizinhos de x, e a fração disso
        sobre o maior dos dois conjuntos de vizinhos casados. Uma função utilitária que muitos chamam junta votos por
        acaso, mas a maior parte dos seus vizinhos não concorda."""
        fp, fx = self.ps2.funcs[p], self.xbox.funcs[x]
        np_ = [n for n in set(fp.calls) | fp.callers if n in self.p2x]
        nx = set(fx.calls) | fx.callers
        nxm = sum(1 for n in nx if n in self.x2p)
        agree = sum(1 for n in np_ if self.p2x[n] in nx)
        return agree, agree / max(len(np_), nxm, 1)

    def align_calls(self):
        """Entre duas chamadas casadas (na ordem do código), os chamados sem par dos dois lados são alinhados pelo
        tamanho (até seis de cada lado; o MSVC expande em linha funções que o GCC chama, então sobram dos dois lados).
        Confiança 1: é uma sugestão."""
        added = 0
        for p, x in list(self.p2x.items()):
            lp, lx = self.ps2.funcs[p].calls, self.xbox.funcs[x].calls
            if not lp or not lx:
                continue
            pos_x = {c: j for j, c in enumerate(lx)}
            pts = [(i, pos_x[self.p2x[c]]) for i, c in enumerate(lp) if c in self.p2x and self.p2x[c] in pos_x]
            chain = _increasing(pts)
            if not chain and not (len(lp) == 1 and len(lx) == 1):
                continue                        # sem nenhuma chamada casada em comum, a ordem não diz nada
            bounds = [(-1, -1)] + chain + [(len(lp), len(lx))]
            for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
                gp = [c for c in lp[i0 + 1:i1] if c not in self.p2x]
                gx = [c for c in lx[j0 + 1:j1] if c not in self.x2p]
                if not gp or not gx or len(gp) > 6 or len(gx) > 6:
                    continue
                for a, b in self.align_by_size(gp, gx):
                    # As funções utilitárias de vetor do Xbox (COORD3: soma, produto escalar e vetorial) não têm par
                    # no PS2, onde viram VU0 em linha; o alinhamento por tamanho as casaria com qualquer vizinha. Um
                    # par cujos outros vizinhos casados discordam fica de fora.
                    agree, ratio = self.agreement(a, b)
                    na = sum(1 for n in set(self.ps2.funcs[a].calls) | self.ps2.funcs[a].callers if n in self.p2x)
                    nb = sum(1 for n in set(self.xbox.funcs[b].calls) | self.xbox.funcs[b].callers if n in self.x2p)
                    if max(na, nb) >= 4 and ratio < 0.3:
                        continue
                    if self.add(a, b, "ordem das chamadas", 1, "entre chamadas casadas de %x" % p):
                        added += 1
        return added

    def align_by_size(self, gp, gx):
        """Alinhamento monótono de duas listas curtas pelo tamanho (custo = desvio da razão x86/MIPS em desvios-padrão;
        pular custa 1). Devolve os pares de custo <= 1 (<= 2 quando há um só de cada lado)."""
        limit = 2.0 if len(gp) == 1 and len(gx) == 1 else 1.0
        def cost(a, b):
            fa, fb = self.ps2.funcs[a], self.xbox.funcs[b]
            if min(fa.ins, fb.ins) < 4:
                return 9.0
            return abs(math.log(fb.ins / fa.ins) - self.ratio_mu) / self.ratio_sd
        n, m = len(gp), len(gx)
        best = [[0.0] * (m + 1) for _ in range(n + 1)]
        move = [[None] * (m + 1) for _ in range(n + 1)]
        for i in range(n + 1):
            for j in range(m + 1):
                if i == 0 and j == 0:
                    continue
                opts = []
                if i and j:
                    opts.append((best[i - 1][j - 1] + cost(gp[i - 1], gx[j - 1]), "d"))
                if i:
                    opts.append((best[i - 1][j] + 1.0, "p"))
                if j:
                    opts.append((best[i][j - 1] + 1.0, "x"))
                best[i][j], move[i][j] = min(opts)
        out = []
        i, j = n, m
        while i or j:
            mv = move[i][j]
            if mv == "d":
                if cost(gp[i - 1], gx[j - 1]) <= limit:
                    out.append((gp[i - 1], gx[j - 1]))
                i, j = i - 1, j - 1
            elif mv == "p":
                i -= 1
            else:
                j -= 1
        return out[::-1]

    # ---- 4: tabelas virtuais

    def vtables(self):
        # GCC 2.95: as tabelas encostadas só se separam pela entrada nula, que a exportação não distingue das palavras
        # {delta, índice}; cada tabela começa na função type_info (__tf), então corta ali.
        ptabs = []
        for a, t in self.ps2.tables:
            cur = None
            for k, f in enumerate(t):
                fn = self.ps2.funcs.get(f)
                if fn is not None and fn.name.endswith("__tf"):
                    cur = (a + 8 * k, [f])
                    ptabs.append(cur)
                elif cur is not None:
                    cur[1].append(f)
        xtabs = self.xbox.tables
        where_x = collections.defaultdict(list)
        for ti, (a, t) in enumerate(xtabs):
            for k, f in enumerate(t):
                where_x[f].append((ti, k))
        added = 0
        for pa, pt in ptabs:
            slots = pt[1:]
            votes = collections.Counter()
            for k, f in enumerate(slots):
                x = self.p2x.get(f)
                if x is None:
                    continue
                for ti, kx in where_x.get(x, ()):
                    # o Xbox pode ter tabelas encostadas que o corte não separou: aceita uma tabela mais longa
                    if kx == k and len(xtabs[ti][1]) >= len(slots):
                        votes[ti] += 1
            if not votes:
                continue
            ranked = votes.most_common(2)
            ti, v = ranked[0]
            if len(ranked) > 1 and ranked[1][1] == v:
                continue                      # métodos herdados: várias tabelas do mesmo tamanho empatam
            xt = xtabs[ti][1]
            if v < 2 and (len(votes) > 1 or len(xt) != len(slots)):
                continue
            # (a entrada do destrutor difere por natureza: no MSVC é o "scalar deleting destructor", que chama o
            # destrutor; no GCC 2.95, o próprio destrutor com o parâmetro "in charge")
            bad = sum(1 for k, f in enumerate(slots) if f in self.p2x and self.p2x[f] != xt[k])
            if bad > max(1, v // 4):
                continue
            for k, f in enumerate(slots):
                if f not in self.p2x and xt[k] not in self.x2p and self.size_ok(f, xt[k], 3.0):
                    if self.add(f, xt[k], "tabela virtual", 2, "entrada %d de %x / %x (%d votos)" % (k, pa, xtabs[ti][0], v)):
                        added += 1
        return added

    # ---- 5: variáveis globais

    def map_globals(self):
        """Casa variáveis globais pelos pares casados que as usam (votos mútuos) e acrescenta cada global casada como
        característica das funções que a usam nos dois lados. Devolve quantas foram casadas."""
        votes = collections.defaultdict(collections.Counter)
        for p, x in self.p2x.items():
            if self.how[p][1] < 2:
                continue
            gp, gx = self.ps2.funcs[p].globals, self.xbox.funcs[x].globals
            if gp and gx and len(gp) <= 24 and len(gx) <= 24:
                for a in gp:
                    for b in gx:
                        votes[a][b] += 1
        best_x = {}
        for a, c in votes.items():
            (b, v), *rest = c.most_common(2)
            if v >= 2 and (not rest or v >= 2 * rest[0][1]):
                best_x[a] = (b, v)
        rev = collections.defaultdict(list)
        for a, (b, v) in best_x.items():
            rev[b].append((v, a))
        gmap = {}
        for b, lst in rev.items():
            lst.sort(reverse=True)
            if len(lst) == 1 or lst[0][0] >= 2 * lst[1][0]:
                gmap[lst[0][1]] = b
        new = {a: b for a, b in gmap.items() if a not in self.gmap}
        self.gmap.update(new)
        if not new:
            return 0
        users_p, users_x = collections.defaultdict(list), collections.defaultdict(list)
        for f in self.ps2.funcs.values():
            for a in f.globals:
                if a in new:
                    users_p[a].append(f)
        inv = {b: a for a, b in new.items()}
        for f in self.xbox.funcs.values():
            for b in f.globals:
                if b in inv:
                    users_x[inv[b]].append(f)
        np_, nx = len(self.ps2.funcs), len(self.xbox.funcs)
        for a in new:
            up, ux = users_p[a], users_x[a]
            if not up or not ux:
                continue
            key = "g%x" % a
            wt = 0.7 * min(math.log(np_ / len(up)), math.log(nx / len(ux)))
            if wt <= 0.3:
                continue
            if len(up) <= 3 and len(ux) <= 3:
                RARE.add(key)
            for f in up:
                f.feat[key] = wt
            for f in ux:
                f.feat[key] = wt
                self.index[key].append(f.addr)
        return len(new)

    # ---- tudo

    def run(self, log=None):
        for p, x, note in MANUAL:
            if p in self.ps2.funcs and x in self.xbox.funcs and p not in self.mask:
                self.add(p, x, "leitura", 3, note)
        n = self.anchor_features()
        self.calibrate()
        n += self.anchor_features()
        if log:
            log("características: %d pares; razão de tamanho x86/MIPS %.2f (desvio %.2f no log)"
                % (len(self.p2x), math.exp(self.ratio_mu), self.ratio_sd))
        for rnd in range(40):
            a = self.propagate_neighbors()
            b = self.vtables()
            c = self.anchor_features()
            g = d = 0
            if not (a or b or c):
                g = self.map_globals() if self.use_globals else 0
                if g:
                    c = self.anchor_features()
            if not (a or b or c):
                d = self.align_calls()
            if log:
                log("rodada %d: vizinhos %d, tabelas virtuais %d, características %d, globais %d, ordem %d"
                    % (rnd + 1, a, b, c, g, d))
            if not (a or b or c or d):
                break
        return self


def _increasing(pts):
    """A maior cadeia crescente nos dois índices."""
    pts = sorted(pts)
    if not pts:
        return []
    tails, prev, idx = [], [None] * len(pts), []
    for i, (_, j) in enumerate(pts):
        k = bisect.bisect_left([pts[t][1] for t in tails], j)
        if k == len(tails):
            tails.append(i)
        else:
            tails[k] = i
        prev[i] = tails[k - 1] if k else None
    out, i = [], tails[-1]
    while i is not None:
        out.append(pts[i])
        i = prev[i]
    return out[::-1]


# ---------------------------------------------------------------- dados reais

def data_root():
    return os.environ.get("NF_DATA", ROOT)


def load_real(root):
    elf = driving_sym.Elf(os.path.join(root, "DRIVING.ELF"))
    pe = open(os.path.join(root, "out", "xbox", "Driving.exe"), "rb").read()

    def read_pe(a, n):
        o = a - PE_BASE
        return pe[o:o + n] if 0x1000 <= o and o + n <= len(pe) else None

    # os dados graváveis (variáveis globais): .data, .sdata, .sbss, .bss no PS2; as seções graváveis do PE
    ps2_data = [(a, a + n) for name, (a, o, n, t) in elf.sections.items()
                if name in (".data", ".sdata", ".sbss", ".bss")]
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import xbox as xbox_tool
    hdr = xbox_tool.parse_pe(pe)
    xb_data = [(PE_BASE + s["rva"], PE_BASE + s["rva"] + s["vsize"]) for s in hdr["sections"]
               if s["flags"] & xbox_tool.SCN_WRITE and not s["flags"] & xbox_tool.SCN_EXEC]
    ps2 = Program(os.path.join(root, "out", "driving", "features.tsv"), elf.read, (0x100000, 0x500000), True,
                  ps2_data)
    xbox = Program(os.path.join(root, "out", "xbox", "Driving_features.tsv"), read_pe, (PE_BASE, PE_BASE + len(pe)),
                   False, xb_data)
    compute_weights(ps2, xbox)
    return ps2, xbox


def ps2_groups(root, ps2):
    """Grupo (módulo) de cada função do PS2 com nome de confiança 2-3, pelo sym_map.tsv e tools/driving_sym.py."""
    sym = driving_sym.load_sym(root)
    crt0 = next((a for (a, n) in sym.entries if n.lower().endswith("crt0.o")), sym.rodata_start)
    out = {}
    for line in open(os.path.join(root, "out", "driving", "sym_map.tsv"), encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if p[0] == "elf" or len(p) < 9 or int(p[5]) < 2:
            continue
        out[int(p[0], 16)] = driving_sym.group_of((None, int(p[2], 16), int(p[3]), p[7], p[8]), crt0)
    return out


def is_named(f):
    return not f.name.startswith(("FUN_", "thunk_FUN_"))


def cmd_match(args):
    root = data_root()
    ps2, xbox = load_real(root)
    log = lambda s: print(s)
    m = Matcher(ps2, xbox).run(log)
    groups = ps2_groups(root, ps2)
    out_dir = os.path.join(root, "out", "xbox")
    with open(os.path.join(out_dir, "match.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("ps2\tps2_tam\tnome\txbox\txbox_tam\tmetodo\tconfianca\tnota\tgrupo\n")
        for p in sorted(m.p2x):
            x = m.p2x[p]
            meth, conf, note = m.how[p]
            f.write("%08x\t%d\t%s\t%08x\t%d\t%s\t%d\t%s\t%s\n" % (p, ps2.funcs[p].size, ps2.funcs[p].name, x,
                                                                xbox.funcs[x].size, meth, conf, note,
                                                                groups.get(p, "")))
    with open(os.path.join(out_dir, "xbox_names.tsv"), "w", encoding="utf-8", newline="\n") as f:
        for x, name, note in XBOX_LABELS:
            if x in xbox.funcs and x not in m.x2p:
                f.write("%08x\tF\tXboxMath\t%s\tXbox: %s (nome nosso, lido no código; sem par no PS2)\n"
                        % (x, name, note))
        for p in sorted(m.p2x):
            fp = ps2.funcs[p]
            meth, conf, note = m.how[p]
            if not is_named(fp):
                continue
            qual = fp.name
            parts = qual.split("::")
            ns, nm = ("::".join(parts[:-1]), parts[-1]) if len(parts) > 1 else ("", qual)
            comment = "PS2 %08x %s [%s; confiança %d; %s]" % (p, qual, meth, conf, note)
            f.write("%08x\t%s\t%s\t%s\t%s\n" % (m.p2x[p], "F" if conf >= 2 else "C", ns, nm, comment))
    report(m, ps2, xbox, groups)
    if args.evaluate:
        evaluate(ps2, xbox)


def report(m, ps2, xbox, groups):
    by = collections.Counter((m.how[p][0], m.how[p][1]) for p in m.p2x)
    print("pares: %d de %d funções do PS2 e %d do Xbox" % (len(m.p2x), len(ps2.funcs), len(xbox.funcs)))
    for (meth, conf), n in sorted(by.items(), key=lambda kv: (-kv[0][1], -kv[1])):
        print("  %-20s confiança %d: %d" % (meth, conf, n))
    named = [p for p, f in ps2.funcs.items() if is_named(f)]
    conf2 = [p for p in m.p2x if m.how[p][1] >= 2]
    print("funções do PS2 com nome: %d; com par de confiança >= 2: %d (%.0f%%), com qualquer par: %d"
          % (len(named), sum(1 for p in conf2 if is_named(ps2.funcs[p])),
             100.0 * sum(1 for p in conf2 if is_named(ps2.funcs[p])) / max(len(named), 1),
             sum(1 for p in m.p2x if is_named(ps2.funcs[p]))))
    tot, hit2, bytes_tot, bytes_hit = (collections.Counter() for _ in range(4))
    for p, g in groups.items():
        if p not in ps2.funcs:
            continue
        tot[g] += 1
        bytes_tot[g] += ps2.funcs[p].size
        if p in m.p2x and m.how[p][1] >= 2:
            hit2[g] += 1
            bytes_hit[g] += ps2.funcs[p].size
    print("%-58s %6s %6s %6s %7s" % ("grupo (nomes de confiança 2-3 do SYM)", "funções", "par>=2", "%", "% bytes"))
    for g in sorted(tot, key=lambda k: -bytes_tot[k]):
        print("%-58s %6d %6d %5.0f%% %6.0f%%" % (g, tot[g], hit2[g], 100.0 * hit2[g] / tot[g],
                                              100.0 * bytes_hit[g] / max(bytes_tot[g], 1)))
    # O código do jogo (sem as bibliotecas, que no Xbox são outras: SDK da Sony, GCC, STL, EAGL, REAL)
    libs = ("SDK", "C/C++", "STL", "EAGL", "REAL")
    game = [g for g in tot if not g.startswith(libs)]
    t, h, bt, bh = (sum(c[g] for g in game) for c in (tot, hit2, bytes_tot, bytes_hit))
    print("%-58s %6d %6d %5.0f%% %6.0f%%" % ("código do jogo (sem as bibliotecas)", t, h, 100.0 * h / max(t, 1),
                                          100.0 * bh / max(bt, 1)))


def evaluate(ps2, xbox, seed=1):
    """Tira as cadeias e constantes de metade dos pares por cadeias e refaz tudo: quantos voltam, e ao mesmo par."""
    base = Matcher(ps2, xbox).run()
    anchors = sorted(p for p in base.p2x if base.how[p][0] == "cadeias")
    rnd = random.Random(seed)
    held = set(rnd.sample(anchors, len(anchors) // 2))
    m = Matcher(ps2, xbox, mask=frozenset(held)).run()
    back = [p for p in held if p in m.p2x]
    same = [p for p in back if m.p2x[p] == base.p2x[p]]
    by = collections.Counter(m.how[p][0] for p in back)
    ok = collections.Counter(m.how[p][0] for p in same)
    print("avaliação: %d pares por cadeias escondidos; %d voltaram por outro caminho, %d ao mesmo par (%.0f%%)"
          % (len(held), len(back), len(same), 100.0 * len(same) / max(len(back), 1)))
    for meth in by:
        print("  %-20s %d voltaram, %d certos (%.0f%%)" % (meth, by[meth], ok[meth], 100.0 * ok[meth] / by[meth]))
    # e a concordância geral: os pares das outras rodadas que também saem na base
    common = [p for p in m.p2x if p in base.p2x and p not in held]
    agree = sum(1 for p in common if m.p2x[p] == base.p2x[p])
    print("  pares fora do grupo escondido iguais aos da base: %d de %d" % (agree, len(common)))


def cmd_show(args):
    root = data_root()
    ps2, xbox = load_real(root)
    m = Matcher(ps2, xbox).run()
    for q in args.names:
        hits = [f for f in ps2.funcs.values() if f.name == q or (q.startswith("0x") and f.addr == int(q, 16))]
        if not hits:
            hits = [f for f in ps2.funcs.values() if q in f.name]
        for fp in hits[:20]:
            x = m.p2x.get(fp.addr)
            print("%08x %s (%d bytes, %d instruções)" % (fp.addr, fp.name, fp.size, fp.ins))
            if x is None:
                sc = m.best_by_features(fp)[:3]
                print("   sem par; melhores pelas características: %s" % ", ".join(
                    "%08x(%.1f)" % (s[3], s[0]) for s in sc))
                continue
            fx = xbox.funcs[x]
            meth, conf, note = m.how[fp.addr]
            s, j, r = feature_score(fp, fx)
            shared = sorted((k for k in fp.feat if k in fx.feat), key=lambda k: -fp.feat[k])
            print("   -> Xbox %08x (%d bytes, %d instruções) %s, confiança %d: %s" % (x, fx.size, fx.ins, meth, conf,
                                                                               note))
            print("      em comum (%.1f, J=%.2f): %s" % (s, j, ", ".join(k[:40] for k in shared[:12])))


# ---------------------------------------------------------------- testes

def _write_features(path, funcs, tables=()):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for a, name, ins, calls, strs, consts in funcs:
            f.write("F\t%08x\t%d\t%s\t1\t%d\n" % (a, ins * 4, name, ins))
            for c in calls:
                f.write("C\t%08x\n" % c)
            for s in strs:
                f.write("S\t%s\n" % s)
            for c in consts:
                f.write("X\t%x\n" % c if c >= 0x30000000 else "K\t%x\n" % c)
        for a, t in tables:
            f.write("T\t%08x\t%s\n" % (a, ",".join("%08x" % x for x in t)))


def cmd_test(args):
    import tempfile
    # PS2: A lê "alpha" e chama B, C; B tem duas constantes raras; C só é alcançável pelo grafo; D e E vêm da tabela
    # virtual de uma classe cujo __tf e F já casam. O Xbox tem os mesmos, com outros endereços e tamanhos, e ruído.
    fl = [0x41234567, 0x42345678, 0x43456789, 0x3f800000]
    noise_s = ["ruido %d" % i for i in range(40)]
    ps2_funcs = [
        (0x1000, "A::run", 40, [0x1100, 0x1200, 0x1300], ["alpha unique", "beta"], []),
        (0x1100, "B::calc", 30, [], [], fl[:2] + [fl[3]]),
        (0x1200, "C::step", 25, [0x1600], [], [fl[3]]),
        (0x1300, "Q::tf__tf", 4, [], ["9Qualquer"], []),
        (0x1400, "Q::f", 20, [], ["q method"], []),
        (0x1500, "Q::g", 22, [], [], []),
        (0x1600, "G::leaf", 10, [], [], []),
        (0x1700, "H::other", 30, [], ["gamma unique"], [fl[2]]),
    ] + [(0x3000 + 0x10 * i, "N%d" % i, 12, [], [noise_s[i]], []) for i in range(40)]
    xb_funcs = [
        (0x21000, "FUN_21000", 35, [0x21200, 0x21100, 0x21300], ["alpha unique", "beta"], []),
        (0x21100, "FUN_21100", 28, [], [], [fl[0], fl[1], fl[3]]),
        (0x21200, "FUN_21200", 20, [0x21600], [], [fl[3]]),
        (0x21300, "FUN_21300", 18, [], [], []),
        (0x21400, "FUN_21400", 17, [], ["q method"], []),
        (0x21500, "FUN_21500", 19, [], [], []),
        (0x21600, "FUN_21600", 9, [], [], []),
        (0x21700, "FUN_21700", 26, [], ["gamma unique"], [fl[2]]),
        (0x21800, "FUN_21800", 30, [], ["beta"], []),
    ] + [(0x23000 + 0x10 * i, "FUN_x%d" % i, 11, [], [noise_s[i]], []) for i in range(40)]
    with tempfile.TemporaryDirectory() as tmp:
        pp, px = os.path.join(tmp, "p.tsv"), os.path.join(tmp, "x.tsv")
        _write_features(pp, ps2_funcs, [(0x9000, [0x1300, 0x1400, 0x1500])])
        _write_features(px, xb_funcs, [(0x29000, [0x21400, 0x21500])])
        nothing = lambda a, n: None
        ps2 = Program(pp, nothing, (0x100000, 0x200000), True)
        xbox = Program(px, nothing, (0x10000, 0x20000), False)
        assert ps2.funcs[0x1100].floats == {float_key(fl[0]), float_key(fl[1]), float_key(fl[3])}
        assert xbox.funcs[0x21000].calls == [0x21200, 0x21100, 0x21300]
        assert 0x21000 in xbox.funcs[0x21200].callers
        compute_weights(ps2, xbox)
        m = Matcher(ps2, xbox).run()
        p2x = m.p2x
        assert p2x.get(0x1000) == 0x21000 and m.how[0x1000][0] == "cadeias", m.how.get(0x1000)
        assert p2x.get(0x1700) == 0x21700
        assert p2x.get(0x1400) == 0x21400
        assert p2x.get(0x1100) == 0x21100, (hex(p2x.get(0x1100, 0)), m.how.get(0x1100))
        assert p2x.get(0x1200) == 0x21200, m.how.get(0x1200)
        assert p2x.get(0x1600) == 0x21600, m.how.get(0x1600)
        assert p2x.get(0x1500) == 0x21500 and m.how[0x1500][0] == "tabela virtual", m.how.get(0x1500)
        assert 0x21800 not in m.x2p          # "beta" sozinho não basta
        assert all(p2x.get(0x3000 + 0x10 * i) == 0x23000 + 0x10 * i for i in range(40))
        assert len(set(p2x.values())) == len(p2x)
    assert _increasing([(0, 5), (1, 1), (2, 2), (3, 0), (4, 3)]) == [(1, 1), (2, 2), (4, 3)]
    assert float_key(0x3f800000) == "f1" and float_key(0) is None and float_key(0x00001234) is None
    assert double_key(struct.pack("<d", 0.5)) == "f0.5"
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    a = sub.add_parser("match")
    a.add_argument("--evaluate", action="store_true")
    a = sub.add_parser("show")
    a.add_argument("names", nargs="+")
    sub.add_parser("test")
    args = ap.parse_args()
    if args.cmd == "match":
        cmd_match(args)
    elif args.cmd == "show":
        cmd_show(args)
    elif args.cmd == "test":
        cmd_test(args)
    else:
        ap.print_help()
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
