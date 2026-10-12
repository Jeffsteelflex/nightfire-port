"""O DRIVING/DRIVING.SYM: a tabela de símbolos de uma compilação do DRIVING.ELF (as missões de carro).

Formato (docs/driving.md, "O DRIVING.SYM"): um mapa simples, little-endian, sem cabeçalho:
    N entradas de 8 bytes {u32 valor, u32 deslocamento do nome}, em ordem crescente de valor, e depois os nomes
    (ASCII terminado em zero, na ordem das entradas). N = o deslocamento do primeiro nome / 8.
Os nomes vêm já "demangled" (C++: "Classe::Metodo(int, float)") e cortados em 63 caracteres. Além das funções e dos
dados, cada objeto do link aparece com o seu caminho ("d:\\ToBurn\\BondPS2\\PS2_EE_Release\\RWater.obj",
"...\\libc.a(memcpy.o)") no endereço onde começa a sua parte de cada seção, e há constantes do linker
(_stack_size, _heap_size), rótulos da VU0 (VU0MICRO_*, endereços da memória de microprograma) e o scratchpad (0x70000000).

O SYM NÃO é do DRIVING.ELF do disco: é de uma compilação anterior e maior (14/10/2002, o ELF é de 18/10; .text 15% e
.rodata 2,5x maiores, a diferença espalhada pelo código todo). Os endereços só coincidem no começo do .text. `match` casa as funções das duas compilações por âncoras (as bibliotecas da Sony e do GCC, achadas por
padrão de bytes no ACTION.ELF, que tem símbolos; as funções type_info do RTTI, achadas pela cadeia com o nome do tipo;
os construtores e destrutores, pela tabela virtual que gravam; o ponto de entrada e o main; as entradas das tabelas
virtuais; os trechos de funções seguidas com o mesmo tamanho exato) e alinha o resto entre as âncoras pelo tamanho.
Confiança: 3 âncora, 2 alinhada com o tamanho igual (97-100% certas nos testes de `match --evaluate`), 1 só alinhada
(sugestão), 0 sem par. Ver docs/driving.md.

Uso (a raiz dos dados é --root, padrão: a raiz do repositório):
    python tools/driving_sym.py info                 formato, contagens, seções, módulos (diretórios dos objetos)
    python tools/driving_sym.py list [saida.tsv]     todas as entradas: endereço, seção, tipo, objeto, nome
    python tools/driving_sym.py objs [saida.tsv]     por objeto: módulo, bytes de código, funções, classes
    python tools/driving_sym.py modules              funções e bytes por grupo (pelo nome), e quantas têm nome no ELF
    python tools/driving_sym.py classes              as classes (pelos type_info) com o número de métodos, por módulo
    python tools/driving_sym.py match [--functions out/driving/functions.tsv] [--out out/driving/sym_map.tsv]
                                                     casa o SYM com o DRIVING.ELF (e mede a precisão nas âncoras)
    python tools/driving_sym.py ghidra <sym_map.tsv> <saida.tsv> [--min N]
                                                     os nomes para tools/ghidra_driving.sh names (só os confiáveis)
    python tools/driving_sym.py test                 confere o leitor e o "mangling" com dados montados à mão

O que sai daqui (listas de nomes, mapas) é derivado do disco: grave em out/.
"""
import argparse
import bisect
import collections
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------- o arquivo .SYM

def parse_sym(data):
    """Lista de (valor, nome) na ordem do arquivo. Confere a ordem e o encadeamento dos nomes."""
    if len(data) < 8:
        raise ValueError("SYM curto demais")
    first = struct.unpack_from("<I", data, 4)[0]
    if first % 8 or first > len(data):
        raise ValueError("SYM: o primeiro nome (0x%x) não fecha a tabela" % first)
    n = first // 8
    out = []
    expect = first
    for i in range(n):
        value, off = struct.unpack_from("<II", data, 8 * i)
        if off != expect:
            raise ValueError("SYM: nome %d em 0x%x, esperado 0x%x" % (i, off, expect))
        end = data.index(b"\0", off)
        out.append((value, data[off:end].decode("latin1")))
        expect = end + 1
    for i in range(1, n):
        if out[i][0] < out[i - 1][0]:
            raise ValueError("SYM: entrada %d fora de ordem" % i)
    return out


def is_object(name):
    """Marcador de objeto do link: o caminho de um .obj, .o ou membro de biblioteca (.a(...))."""
    low = name.lower()
    return "\\" in name and (low.endswith(".obj") or low.endswith(".o") or ".a(" in low or low.endswith(".o)"))


def object_module(path):
    """O 'módulo' de um objeto: o diretório do projeto ou da biblioteca (BondPS2, EAGLPS2/libeagla, SN/libc...)."""
    p = path.replace("\\", "/")
    if ".a(" in p:
        lib = p[:p.index(".a(")].rsplit("/", 1)[-1]
        top = p.split("/")[3] if p.lower().startswith("d:/toburn/library/") else p.split("/")[2]
        return "%s/%s" % (top, lib)
    parts = p.split("/")
    # d:/ToBurn/<Projeto>/PS2_EE_Release/x.obj, d:/ToBurn/BondPreProcessPS2/materials/x.obj
    return parts[2] if len(parts) > 3 else parts[-1]


def object_short(path):
    p = path.replace("\\", "/")
    if ".a(" in p:
        lib = p[:p.index(".a(")].rsplit("/", 1)[-1]
        return "%s.a(%s" % (lib, p[p.index(".a(") + 3:])
    return p.rsplit("/", 1)[-1]


class Sym:
    """O SYM classificado: cada entrada com tipo (obj, func, data, abs, vu), seção e objeto."""

    def __init__(self, entries):
        self.entries = entries
        n = len(entries)
        names = [e[1] for e in entries]
        first_obj = next(i for i, nm in enumerate(names) if is_object(nm))
        end_idx = max(i for i, nm in enumerate(names) if nm == "_end") if "_end" in names else n - 1
        fbss = names.index("_fbss") if "_fbss" in names else end_idx
        # Seções: o .text começa no primeiro objeto; o .data na segunda aparição do primeiro objeto, o .rodata na
        # terceira (a ordem dos objetos se repete em cada seção); o .bss no _fbss.
        first_name = names[first_obj]
        again = [i for i, nm in enumerate(names) if nm == first_name]
        data_start = again[1] if len(again) > 1 else fbss
        rodata_start = again[2] if len(again) > 2 else fbss
        self.text_start = entries[first_obj][0]
        self.data_start = entries[data_start][0]
        self.rodata_start = entries[rodata_start][0]
        self.bss_start = entries[fbss][0]
        self.end = entries[end_idx][0]
        self.kind = []
        self.section = []
        self.obj = []
        cur = None
        for i, (v, nm) in enumerate(entries):
            if i < first_obj or i > end_idx:
                kind, sec = ("abs", "abs")
            elif i < data_start:
                sec = "text"
                kind = "func"
                if nm.startswith("VU0_") and nm.endswith("_dma_start"):
                    kind, sec = ("vu", "vutext")
                elif nm == "<default>":
                    kind = "label"
            elif i < rodata_start:
                kind, sec = ("data", "data")
            elif i < fbss:
                kind, sec = ("data", "rodata")
            else:
                kind, sec = ("data", "bss")
            if is_object(nm) and kind != "abs":
                kind = "obj"
                cur = nm
            if nm == "<default>" or nm.startswith("_") and kind == "data" and nm in ("_fbss", "_gp", "_end"):
                kind = "label"
            self.kind.append(kind)
            self.section.append(sec)
            self.obj.append(cur if kind not in ("abs",) else None)

    def functions(self):
        """(índice, endereço, tamanho até o próximo símbolo do .text, nome, objeto) das funções."""
        idx = [i for i, k in enumerate(self.kind) if self.section[i] == "text"]
        out = []
        for j, i in enumerate(idx):
            if self.kind[i] != "func":
                continue
            nxt = self.entries[idx[j + 1]][0] if j + 1 < len(idx) else self.entries[i][0]
            out.append((i, self.entries[i][0], nxt - self.entries[i][0], self.entries[i][1], self.obj[i]))
        return out


def load_sym(root):
    path = os.path.join(root, "DRIVING", "DRIVING.SYM")
    return Sym(parse_sym(open(path, "rb").read()))


# ---------------------------------------------------------------- nomes C++

def split_params(name):
    """'A::B<int>::f(int, float)' -> ('A::B<int>::f', '(int, float)'). Nomes cortados podem não fechar o ')'."""
    depth = 0
    i = 0
    while i < len(name):
        c = name[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        elif c == "(" and depth <= 0:
            if name[:i].endswith("operator") and name[i:i + 2] == "()":
                i += 2
                continue
            return name[:i], name[i:]
        i += 1
    return name, ""


def split_scope(qual):
    """'A::B<x::y>::f' -> ['A', 'B<x::y>', 'f'] (os '::' dentro de <> não separam)."""
    parts = []
    depth = 0
    cur = ""
    i = 0
    while i < len(qual):
        c = qual[i]
        if c == "<":
            depth += 1
        elif c == ">":
            depth -= 1
        if depth == 0 and qual[i:i + 2] == "::":
            parts.append(cur)
            cur = ""
            i += 2
            continue
        cur += c
        i += 1
    parts.append(cur)
    return parts


def mangle_type(qual):
    """Nome do tipo como o g++ 2.95 o grava na cadeia do type_info ('19AnimationController',
    'Q28EAGLAnim10FnAnimBank'); None para modelos (templates) e tipos básicos."""
    if "<" in qual or " " in qual:
        return None
    parts = split_scope(qual)
    if any(not p for p in parts):
        return None
    enc = "".join("%d%s" % (len(p), p) for p in parts)
    if len(parts) == 1:
        return enc
    if len(parts) < 10:
        return "Q%d%s" % (len(parts), enc)
    return "Q_%d_%s" % (len(parts), enc)


# ---------------------------------------------------------------- o ELF

class Elf:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise ValueError("%s não é um ELF" % path)
        self.entry = struct.unpack_from("<I", d, 0x18)[0]
        shoff = struct.unpack_from("<I", d, 0x20)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
        secs = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
        strsec = secs[shstrndx] if shnum else None
        self.sections = {}
        for s in secs:
            if not strsec:
                break
            o = strsec[4] + s[0]
            nm = d[o:d.index(b"\0", o)].decode("latin1")
            self.sections[nm] = (s[3], s[4], s[5], s[1])  # addr, offset, size, type
        phoff = struct.unpack_from("<I", d, 0x1C)[0]
        phnum = struct.unpack_from("<H", d, 0x2C)[0]
        self.segments = [struct.unpack_from("<8I", d, phoff + i * 32) for i in range(phnum)]
        addr, off, size, _ = self.sections[".text"]
        self.text_addr, self.text_size = addr, size
        self.text = struct.unpack_from("<%dI" % (size // 4), d, off)

    def read(self, addr, size):
        for p in self.segments:
            if p[0] == 1 and p[2] <= addr and addr + size <= p[2] + p[4]:
                o = p[1] + addr - p[2]
                return self.data[o:o + size]
        return None

    def section_words(self, name):
        addr, off, size, typ = self.sections[name]
        if typ == 8:  # NOBITS
            return addr, ()
        return addr, struct.unpack_from("<%dI" % (size // 4), self.data, off)

    def in_text(self, a):
        return self.text_addr <= a < self.text_addr + self.text_size and a % 4 == 0

    def cstring_at(self, addr):
        b = self.read(addr, 1)
        if b is None:
            return None
        out = bytearray()
        while True:
            c = self.read(addr + len(out), 1)
            if not c or c == b"\0":
                return out.decode("latin1")
            out += c


# Instruções que somam um imediato de 16 bits com sinal a um registrador (o par lui + %lo).
LO_OPS = {0x09, 0x23, 0x2B, 0x20, 0x24, 0x21, 0x25, 0x28, 0x29, 0x31, 0x39, 0x37, 0x3F, 0x1E, 0x1F, 0x27, 0x26,
          0x22, 0x2A, 0x2E, 0x36, 0x3E, 0x1A, 0x1B, 0x2C, 0x2D, 0x34, 0x3C}


STORE_OPS = {0x28, 0x29, 0x2B, 0x2A, 0x2E, 0x2C, 0x2D, 0x3F, 0x1F, 0x39, 0x3D, 0x3E}


def written_reg(x):
    """Registrador inteiro que a instrução escreve (aproximado: tipo I no rt, tipo R no rd), ou None."""
    op = x >> 26
    if op == 0:
        return (x >> 11) & 31
    if op in STORE_OPS or op in (1, 2, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x31, 0x35, 0x36):
        return None
    if op == 3:
        return 31
    return (x >> 16) & 31


def address_refs(elf):
    """Endereço -> instruções do .text que o calculam (lui rX, %hi seguido de addiu/ori/load/store em rX)."""
    tw = elf.text
    refs = collections.defaultdict(list)
    n = len(tw)
    for i, w in enumerate(tw):
        if (w >> 26) != 0x0F:
            continue
        rt = (w >> 16) & 31
        hi = w & 0xFFFF
        for j in range(i + 1, min(i + 16, n)):
            x = tw[j]
            op = x >> 26
            if ((x >> 21) & 31) == rt and (op in LO_OPS or op == 0x0D):
                lo = x & 0xFFFF
                if op == 0x0D:
                    a = (hi << 16) | lo
                else:
                    a = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
                refs[a].append(elf.text_addr + j * 4)
            if written_reg(x) == rt:
                break
    return refs


def jal_targets(elf):
    out = set()
    base = elf.text_addr & 0xF0000000
    for w in elf.text:
        if (w >> 26) == 3:
            out.add(base | ((w & 0x03FFFFFF) << 2))
    return out


def load_functions_tsv(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if len(p) >= 2:
            try:
                out[int(p[0], 16)] = int(p[1])
            except ValueError:
                pass
    return out


def vtables(elf):
    """Tabelas virtuais do g++ 2.95 (sem 'thunks') no .rodata/.data: entradas de 8 bytes {s16 delta, s16 índice,
    ponteiro}; a primeira é nula e a segunda aponta a função type_info. O ponteiro da tabela gravado no objeto é o
    endereço + 8. Devolve {endereço: [(primeira palavra, ponteiro)]}, a começar pela entrada da type_info."""
    out = {}
    for sec in (".rodata", ".data"):
        if sec not in elf.sections:
            continue
        addr, ws = elf.section_words(sec)
        i = 0
        n = len(ws)
        while i + 3 < n:
            if ws[i] == 0 and ws[i + 1] == 0 and ws[i + 2] == 0 and elf.in_text(ws[i + 3]) and i % 2 == 0:
                slots = [(0, ws[i + 3])]
                k = i + 4
                while k + 1 < n and ws[k] < 0x10000 and elf.in_text(ws[k + 1]):
                    slots.append((ws[k], ws[k + 1]))
                    k += 2
                out[addr + i * 4] = slots
                i = k
            else:
                i += 1
    return out


# ---------------------------------------------------------------- casamento SYM x ELF

def masked(w):
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op in LO_OPS or op in (0x0F, 0x0D, 0x0C, 0x0A, 0x0B, 0x19, 0x18):
        return w & 0xFFFF0000
    return w


def elf_symbols(path):
    """Funções da .symtab de um ELF com símbolos (o ACTION.ELF): [(endereço, tamanho, nome)]."""
    d = open(path, "rb").read()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shentsize, shnum, _ = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for s in secs:
        if s[1] != 2:
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            nm, value, size, info, _o, _sh = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
            if info & 0xF == 2 and size:
                o = strtab[4] + nm
                out.append((value, size, d[o:d.index(b"\0", o)].decode("latin1")))
    return out


def library_anchors(elf, action_path, sym_by_name):
    """Funções das bibliotecas comuns (libc, libkernl, libpad, libmc, libgcc...) achadas no DRIVING.ELF pelo padrão
    de bytes da cópia do ACTION.ELF, com os campos relocados mascarados. Só os casamentos únicos com o mesmo nome
    no SYM."""
    act = Elf(action_path)
    mw = [masked(w) for w in elf.text]
    index = collections.defaultdict(list)
    for i in range(len(mw) - 2):
        index[(mw[i], mw[i + 1], mw[i + 2])].append(i)
    out = {}
    for addr, size, name in elf_symbols(action_path):
        if size < 24 or size > 32768 or name not in sym_by_name:
            continue
        b = act.read(addr, size)
        if b is None:
            continue
        am = [masked(w) for w in struct.unpack("<%dI" % (size // 4), b)]
        hits = [c for c in index.get((am[0], am[1], am[2]), ()) if mw[c:c + len(am)] == am]
        if len(hits) == 1:
            out[sym_by_name[name]] = elf.text_addr + hits[0] * 4
    return out


def chain(pairs):
    """Maior cadeia crescente nos dois índices (cada lado usado uma vez): [(s, e)] -> subconjunto ordenado."""
    pairs = sorted(set(pairs), key=lambda p: (p[0], -p[1]))
    # LIS em e, com s estritamente crescente (os pares com o mesmo s vêm com e decrescente)
    tails = []
    tails_idx = []
    prev = [-1] * len(pairs)
    for k, (s, e) in enumerate(pairs):
        j = bisect.bisect_left(tails, e)
        if j == len(tails):
            tails.append(e)
            tails_idx.append(k)
        else:
            tails[j] = e
            tails_idx[j] = k
        prev[k] = tails_idx[j - 1] if j > 0 else -1
    out = []
    k = tails_idx[-1] if tails_idx else -1
    while k >= 0:
        out.append(pairs[k])
        k = prev[k]
    out.reverse()
    # s também precisa ser estritamente crescente
    res = []
    for s, e in out:
        if not res or (s > res[-1][0] and e > res[-1][1]):
            res.append((s, e))
    return res


def func_containing(starts, a):
    k = bisect.bisect_right(starts, a) - 1
    return starts[k] if k >= 0 else None


def rtti_anchors(elf, sym, sfuncs, starts, refs):
    """As funções type_info ('X type_info function', o __tf do g++ 2.95) são as que leem a cadeia com o nome
    do tipo X. Uma __tf de classe derivada também lê o nome da base: a cadeia em ordem resolve."""
    rodata_addr, _, rodata_size, _ = elf.sections[".rodata"]
    blob = elf.read(rodata_addr, rodata_size)
    pairs = []
    for si, (i, a, size, name, obj) in enumerate(sfuncs):
        if not name.endswith(" type_info function"):
            continue
        mang = mangle_type(name[:-len(" type_info function")])
        if not mang:
            continue
        key = b"\0" + mang.encode("latin1") + b"\0"
        pos = blob.find(key)
        while pos >= 0:
            saddr = rodata_addr + pos + 1
            for r in refs.get(saddr, ()):
                f = func_containing(starts, r)
                if f is not None:
                    pairs.append((si, f))
            pos = blob.find(key, pos + 1)
    return chain(pairs)


def method_of(name):
    """'A::B::f(int)' -> ('A::B', 'f'); destrutor -> ('A::B', '~'); type_info -> (classe, '__tf')."""
    if name.endswith(" type_info function"):
        return name[:-len(" type_info function")], "__tf"
    parts = split_scope(split_params(name)[0])
    if len(parts) < 2:
        return "", parts[-1]
    cls = "::".join(parts[:-1])
    m = parts[-1]
    if m == "~" + parts[-2]:
        m = "~"
    elif m == parts[-2]:
        m = "<ctor>"
    return cls, m


def align_segment(s_items, e_items, ratio):
    """Alinha funções do SYM (endereço, tamanho) a funções do ELF (endereço, tamanho) entre duas âncoras.
    Cada função do SYM cobre um trecho contíguo de 1..4 funções do ELF (a função com nome e as sem nome que a
    seguem) ou nenhuma (sumiu na compilação final). Custo: |log(tamanho ELF / (tamanho SYM * razão))|."""
    n, m = len(s_items), len(e_items)
    INF = float("inf")
    if n == 0 or m == 0:
        return {}
    pre = [0]
    for a, sz in e_items:
        pre.append(pre[-1] + sz)
    cost = [[INF] * (m + 1) for _ in range(n + 1)]
    back = [[None] * (m + 1) for _ in range(n + 1)]
    cost[0][0] = 0.0
    # funções do ELF antes da primeira do SYM: sem nome (estáticas da função âncora anterior)
    for j in range(1, m + 1):
        cost[0][j] = cost[0][j - 1] + 1.5
        back[0][j] = (0, j - 1, "skip_e")
    DEL = 2.0
    EXTRA = 0.35
    for i in range(1, n + 1):
        ssz = max(s_items[i - 1][1], 4) * ratio
        for j in range(0, m + 1):
            best = cost[i - 1][j] + DEL
            bk = (i - 1, j, "del")
            for k in range(1, 5):
                if j - k < 0:
                    break
                c0 = cost[i - 1][j - k]
                if c0 == INF:
                    continue
                esz = max(pre[j] - pre[j - k], 4)
                c = c0 + abs(math.log(esz / ssz)) + EXTRA * (k - 1)
                if c < best:
                    best = c
                    bk = (i - 1, j - k, "map")
            cost[i][j] = best
            back[i][j] = bk
    i, j = n, m
    out = {}
    while i > 0 or j > 0:
        pi, pj, how = back[i][j]
        if how == "map":
            esz = pre[j] - pre[pj]
            ssz = max(s_items[i - 1][1], 4) * ratio
            out[i - 1] = (pj, j - pj, abs(math.log(max(esz, 4) / ssz)))
        i, j = pi, pj
    return out


# Custo abaixo do qual uma função alinhada conta como "tamanho igual" (confiança 2): |log| < 0.08, uns 8%.
SAME_SIZE = 0.08


class Matcher:
    """Casa as funções do SYM com as do DRIVING.ELF.

    1. Âncoras: as bibliotecas (padrão de bytes do ACTION.ELF), as type_info (a cadeia com o nome do tipo), os
       construtores e destrutores (gravam a tabela virtual da classe; só quando a classe tem tantas funções que gravam
       a tabela quanto nomes de construtor e destrutor no SYM), a entrada (ENTRYPOINT = e_entry).
    2. A maior cadeia crescente das âncoras (a ordem das funções no link é a mesma nas duas compilações).
    3. Entre duas âncoras, alinhamento pelo tamanho (align_segment), com a razão de tamanho local.
    4. As tabelas virtuais: a hierarquia vem das type_info (a __tf de uma classe derivada lê o nome da base); o nome do
       método de cada entrada vem de qualquer classe da família onde a função já tem nome; uma entrada sobrescrita
       numa classe vira âncora quando o SYM tem um só "Classe::método". Volta ao passo 2 com as âncoras novas.
    """

    def __init__(self, root, functions_tsv=None, action=True):
        self.sym = load_sym(root)
        self.elf = Elf(os.path.join(root, "DRIVING.ELF"))
        self.sfuncs = self.sym.functions()
        self.by_name = {}
        self.by_method = collections.defaultdict(list)
        self.methods_of = collections.defaultdict(set)
        for si, f in enumerate(self.sfuncs):
            self.by_name.setdefault(f[3], si)
            cls, m = method_of(f[3])
            self.by_method[(cls, m)].append(si)
            self.methods_of[cls].add(m)
        self.refs = address_refs(self.elf)
        self.vtabs = vtables(self.elf)
        self.starts = elf_function_starts(self.elf, functions_tsv, self.vtabs)
        end_text = self.elf.text_addr + self.elf.text_size
        self.span = {a: (self.starts[k + 1] if k + 1 < len(self.starts) else end_text) - a
                     for k, a in enumerate(self.starts)}
        self.index = {a: k for k, a in enumerate(self.starts)}
        self.lib = library_anchors(self.elf, os.path.join(root, "ACTION.ELF"), self.by_name) if action else {}
        self.tf = rtti_anchors(self.elf, self.sym, self.sfuncs, self.starts, self.refs)
        self.hier = self.class_hierarchy()
        self.ctor = self.ctor_anchors()
        self.entry = [(self.by_name["ENTRYPOINT"], self.elf.entry)] if "ENTRYPOINT" in self.by_name else []
        # main: a única função do jogo (abaixo do crt0) que o ENTRYPOINT chama
        self.main = []
        if self.entry and "main" in self.by_name:
            e0 = self.elf.entry
            calls = set()
            for k in range((e0 - self.elf.text_addr) // 4, (e0 + self.span[e0] - self.elf.text_addr) // 4):
                w = self.elf.text[k]
                if (w >> 26) == 3:
                    calls.add((self.elf.text_addr & 0xF0000000) | ((w & 0x03FFFFFF) << 2))
            game = [t for t in calls if t < e0]
            if len(game) == 1:
                self.main = [(self.by_name["main"], game[0])]

    # -- âncoras

    def tf_classes(self, tf_pairs):
        """{classe: endereço da __tf no ELF} das âncoras type_info."""
        return {self.sfuncs[s][3][:-len(" type_info function")]: e for s, e in tf_pairs}

    def class_hierarchy(self):
        """{classe: (tabela virtual [(palavra, ponteiro)], base ou None)} pelas type_info casadas."""
        tf_cls = self.tf_classes(self.tf)
        vt_of_tf = {slots[0][1]: (vt, slots) for vt, slots in self.vtabs.items()}
        rodata_addr, _, rodata_size, _ = self.elf.sections[".rodata"]
        blob = self.elf.read(rodata_addr, rodata_size)
        name_at = {}
        for cls in tf_cls:
            mang = mangle_type(cls)
            pos = blob.find(b"\0" + mang.encode("latin1") + b"\0")
            while pos >= 0:
                name_at[rodata_addr + pos + 1] = cls
                pos = blob.find(b"\0" + mang.encode("latin1") + b"\0", pos + 1)
        refs_from = collections.defaultdict(set)
        for a, lst in self.refs.items():
            if a in name_at:
                for r in lst:
                    f = func_containing(self.starts, r)
                    refs_from[f].add(name_at[a])
        out = {}
        for cls, e in tf_cls.items():
            vt = vt_of_tf.get(e)
            others = refs_from.get(e, set()) - {cls}
            # a __tf lê os nomes da base e dos ancestrais dela: a base é a mais derivada (a que lê mais nomes)
            base = max(others, key=lambda o: (len(refs_from.get(tf_cls[o], ())), o)) if others else None
            out[cls] = (vt, base)
        return out

    def ctor_anchors(self):
        pairs = []
        for cls, (vt, base) in self.hier.items():
            if vt is None:
                continue
            names = sorted(self.by_method.get((cls, "<ctor>"), []) + self.by_method.get((cls, "~"), []))
            cands = sorted({func_containing(self.starts, r) for r in self.refs.get(vt[0] + 8, ())} - {None})
            if names and len(cands) == len(names):
                pairs += list(zip(names, cands))
        return pairs

    def vtable_anchors(self, named):
        """Âncoras novas pelas entradas das tabelas virtuais. `named`: {endereço no ELF: índice no SYM} confiáveis."""
        hier = self.hier
        parent = {c: b for c, (vt, b) in hier.items() if b in hier}
        slots = {c: vt[1][1:] for c, (vt, b) in hier.items() if vt}

        def introducer(c, k):
            while c in parent and parent[c] in slots and k < len(slots[parent[c]]):
                c = parent[c]
            return c

        def definer(c, k, f):
            while c in parent and parent[c] in slots and k < len(slots[parent[c]]) and slots[parent[c]][k][1] == f:
                c = parent[c]
            return c

        pv = self.by_name.get("__pure_virtual")
        pure = next((e for e, s in named.items() if s == pv), None)
        slot_name = {}
        for c, sl in slots.items():
            for k, (w, f) in enumerate(sl):
                if w or f not in named or f == pure:
                    continue
                cls, m = method_of(self.sfuncs[named[f]][3])
                if m in ("__tf", "<ctor>"):
                    continue
                slot_name.setdefault((introducer(c, k), k), set()).add(m)
        # Entradas sem nome conhecido (a base é virtual pura, nenhuma classe da família casada): o nome que todas as
        # classes que sobrescrevem a entrada têm no SYM, se for um só (e não for o de outra entrada da família).
        overriders = collections.defaultdict(list)
        for c, sl in slots.items():
            for k, (w, f) in enumerate(sl):
                if not w and f != pure and definer(c, k, f) == c:
                    overriders[(introducer(c, k), k)].append(c)
        used = collections.defaultdict(set)
        for (a, k), ms in slot_name.items():
            used[a] |= ms
        for key, cls_list in overriders.items():
            if key in slot_name or len(cls_list) < 2:
                continue
            common = None
            for c in cls_list:
                ms = self.methods_of.get(c, set()) - {"<ctor>", "~", "__tf"}
                common = ms if common is None else common & ms
            common = (common or set()) - used[key[0]]
            if len(common) == 1:
                slot_name[key] = common
                used[key[0]] |= common
        new = []
        for c, sl in slots.items():
            for k, (w, f) in enumerate(sl):
                if w or f in named or f == pure:
                    continue
                ms = slot_name.get((introducer(c, k), k))
                if not ms or len(ms) != 1:
                    continue
                m = next(iter(ms))
                d = definer(c, k, f)
                cands = self.by_method.get((d, m), [])
                if len(cands) == 1:
                    new.append((cands[0], f))
        return new

    # -- alinhamento

    def align(self, anchor_pairs):
        """{índice no SYM: (endereço no ELF, custo, método)} a partir de uma lista de âncoras."""
        anchors = dict(chain(anchor_pairs))
        res = {}
        sidx = sorted(anchors)
        for s0, s1 in zip(sidx, sidx[1:]):
            e0, e1 = anchors[s0], anchors[s1]
            k0, k1 = self.index.get(e0), self.index.get(e1)
            if k0 is None or k1 is None or k1 <= k0:
                continue
            s_items = [(self.sfuncs[s][1], self.sfuncs[s][2]) for s in range(s0 + 1, s1)]
            e_items = [(self.starts[k], self.span[self.starts[k]]) for k in range(k0 + 1, k1)]
            ssum = self.sfuncs[s1][1] - self.sfuncs[s0][1]
            ratio = (e1 - e0) / ssum if ssum > 0 else 1.0
            for off, (pj, cnt, c) in align_segment(s_items, e_items, ratio).items():
                res[s0 + 1 + off] = (e_items[pj][0], c)
        for s, e in anchors.items():
            res[s] = (e, 0.0)
        return anchors, res

    def exact(self, s, e):
        """A função do ELF ocupa exatamente o mesmo número de bytes que a do SYM (código igual nas duas)."""
        return self.span.get(e) == self.sfuncs[s][2]

    def exact_runs(self, anchors, res, min_len=3, min_bytes=96):
        """Trechos de min_len funções seguidas (no SYM e no ELF) com o tamanho exatamente igual: viram âncoras."""
        out = []
        run = []

        def flush():
            if len(run) >= min_len and sum(self.sfuncs[x][2] for x in run) >= min_bytes:
                out.extend((x, res[x][0]) for x in run)

        for s in range(len(self.sfuncs)):
            if not (s in res and s not in anchors and self.exact(s, res[s][0])):
                flush()
                run = []
                continue
            if run and not (run[-1] == s - 1 and
                            self.index.get(res[s][0]) == self.index.get(res[run[-1]][0], -9) + 1):
                flush()
                run = []
            run.append(s)
        flush()
        return out

    def run(self, base_pairs, rounds=4):
        """As âncoras dadas, o alinhamento, e as rodadas das tabelas virtuais e dos trechos de tamanho exato.
        Devolve (âncoras, resultado, {par: método das âncoras novas})."""
        pairs = list(base_pairs)
        found = {}
        anchors, res = self.align(pairs)
        for _ in range(rounds):
            named = {}
            for s, (e, c) in res.items():
                if s in anchors or c < SAME_SIZE or self.exact(s, e):
                    named.setdefault(e, s)
            extra = {p: "vtable" for p in self.vtable_anchors(named) if p not in found}
            extra.update({p: "tamanho exato" for p in self.exact_runs(anchors, res) if p not in found
                          and p not in extra})
            if not extra:
                break
            found.update(extra)
            anchors, res = self.align(pairs + list(found))
        return anchors, res, found

    def base_pairs(self, lib=True, tf=True, ctor=True):
        out = list(self.entry) + list(self.main)
        if lib:
            out += list(self.lib.items())
        if tf:
            out += list(self.tf)
        if ctor:
            out += list(self.ctor)
        return out

    def confidence(self, s, e, c, anchors):
        if s in anchors:
            return 3
        return 2 if (c < SAME_SIZE or self.exact(s, e)) else 1

    def rows(self):
        anchors, res, found = self.run(self.base_pairs())
        kind = {}
        for s, e in self.entry:
            kind[(s, e)] = "entrada"
        for s, e in self.main:
            kind[(s, e)] = "main"
        for s, e in self.lib.items():
            kind.setdefault((s, e), "biblioteca")
        for s, e in self.tf:
            kind.setdefault((s, e), "rtti")
        for s, e in self.ctor:
            kind.setdefault((s, e), "construtor")
        for p, how in found.items():
            kind.setdefault(p, how)
        out = []
        for si, (i, a, size, name, obj) in enumerate(self.sfuncs):
            if si in res:
                e, c = res[si]
                conf = self.confidence(si, e, c, anchors)
                how = kind.get((si, e), "?") if conf == 3 else "tamanho"
                out.append((e, self.span.get(e, 0), a, size, how, conf, c, name, obj))
            else:
                out.append((None, 0, a, size, "-", 0, 0.0, name, obj))
        return anchors, out

    def evaluate(self):
        """Precisão: tira um grupo de âncoras, refaz o casamento sem elas e conta quantas voltam certas, no total e
        entre as que o casamento daria com confiança 2."""
        def score(anchors, res, truth):
            ok = sum(1 for s, e in truth if s in res and res[s][0] == e)
            hi = [(s, e) for s, e in truth if s in res and self.confidence(s, res[s][0], res[s][1], anchors) >= 2]
            ok_hi = sum(1 for s, e in hi if res[s][0] == e)
            return ok, len(truth), ok_hi, len(hi)

        def report(name, anchors, res, truth):
            ok, n, ok_hi, n_hi = score(anchors, res, truth)
            print("teste, %s: %d de %d certas (%.0f%%); das que sairiam com confiança 2: %d de %d (%.0f%%)" % (
                name, ok, n, 100.0 * ok / max(n, 1), ok_hi, n_hi, 100.0 * ok_hi / max(n_hi, 1)))

        tf_sorted = sorted(self.tf)
        a2, r2, _ = self.run(self.base_pairs(lib=False))
        report("bibliotecas tiradas (a ordem do link das bibliotecas mudou)", a2, r2, list(self.lib.items()))
        a2, r2, _ = self.run(self.entry + self.main + list(self.lib.items()) + tf_sorted[0::2] + list(self.ctor))
        report("metade das type_info tirada", a2, r2, tf_sorted[1::2])
        a2, r2, _ = self.run(self.base_pairs(ctor=False))
        report("construtores e destrutores tirados", a2, r2, list(self.ctor))
        anchors, res, found = self.run(self.base_pairs())
        for how in ("vtable", "tamanho exato"):
            lst = sorted(p for p, h in found.items() if h == how)
            keep = [p for p, h in found.items() if not (h == how and p in set(lst[1::2]))]
            a2, r2 = self.align(self.base_pairs() + keep)
            report("metade das âncoras '%s' tirada (só o alinhamento)" % how, a2, r2, lst[1::2])


def elf_function_starts(elf, functions_tsv, vtabs):
    starts = set()
    if functions_tsv and os.path.exists(functions_tsv):
        starts |= {a for a in load_functions_tsv(functions_tsv) if elf.in_text(a)}
    starts |= {a for a in jal_targets(elf) if elf.in_text(a)}
    for slots in vtabs.values():
        starts |= {f for w, f in slots if elf.in_text(f)}
    starts.add(elf.entry)
    return sorted(starts)


# ---------------------------------------------------------------- nomes para o Ghidra

def ghidra_name(full):
    """'A::B::f(int)' -> ('A::B', 'f'); espaços e caracteres que o Ghidra recusa viram '_'."""
    def clean(p):
        return p.replace(" ", "_").replace("*", "P").replace("&", "R").replace(",", "_") or "_"

    if full.endswith(" type_info function"):
        parts = split_scope(full[:-len(" type_info function")])
        return "::".join(clean(p) for p in parts), "__tf"
    parts = [clean(p) for p in split_scope(split_params(full)[0])]
    return "::".join(parts[:-1]), parts[-1]


# ---------------------------------------------------------------- comandos

def cmd_info(args):
    path = os.path.join(args.root, "DRIVING", "DRIVING.SYM")
    data = open(path, "rb").read()
    entries = parse_sym(data)
    sym = Sym(entries)
    print("%s: %d bytes, %d entradas de 8 bytes, nomes de 0x%x a 0x%x" %
          (path, len(data), len(entries), len(entries) * 8, len(data)))
    print("maior nome: %d caracteres (%d nomes com 63: cortados)" %
          (max(len(e[1]) for e in entries), sum(1 for e in entries if len(e[1]) == 63)))
    print("seções (pela ordem dos objetos): .text 0x%x, .data 0x%x, .rodata 0x%x, .bss 0x%x, fim 0x%x" %
          (sym.text_start, sym.data_start, sym.rodata_start, sym.bss_start, sym.end))
    kinds = collections.Counter((s, k) for s, k in zip(sym.section, sym.kind))
    for (s, k), c in sorted(kinds.items()):
        print("  %-7s %-6s %6d" % (s, k, c))
    print("absolutos:", ", ".join("%s=0x%x" % (nm, v) for (v, nm), k in zip(entries, sym.kind) if k == "abs"))
    objs = collections.OrderedDict()
    for (v, nm), k in zip(entries, sym.kind):
        if k == "obj":
            objs.setdefault(nm, 0)
    mods = collections.Counter(object_module(o) for o in objs)
    print("objetos distintos: %d" % len(objs))
    fn = sym.functions()
    per_mod_fn = collections.Counter(object_module(f[4]) if f[4] else "?" for f in fn)
    per_mod_bytes = collections.Counter()
    for f in fn:
        per_mod_bytes[object_module(f[4]) if f[4] else "?"] += f[2]
    print("%-28s %8s %8s %10s" % ("módulo", "objetos", "funções", "bytes .text"))
    for m, c in sorted(per_mod_bytes.items(), key=lambda kv: -kv[1]):
        print("%-28s %8d %8d %10d" % (m, mods.get(m, 0), per_mod_fn[m], c))
    names = [f[3] for f in fn]
    print("funções: %d; type_info: %d; tabelas virtuais: %d; classes com métodos: %d" % (
        len(fn), sum(1 for n in names if n.endswith("type_info function")),
        sum(1 for (v, n) in entries if n.endswith(" virtual table")),
        len({split_scope(split_params(n)[0])[0] for n in names if "::" in n})))


def cmd_list(args):
    sym = load_sym(args.root)
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    for (v, nm), k, s, o in zip(sym.entries, sym.kind, sym.section, sym.obj):
        out.write("%08x\t%s\t%s\t%s\t%s\n" % (v, s, k, object_short(o) if o else "", nm))


def cmd_objs(args):
    sym = load_sym(args.root)
    fn = sym.functions()
    per = collections.OrderedDict()
    for f in fn:
        o = f[4] or "?"
        d = per.setdefault(o, [0, 0, set()])
        d[0] += f[2]
        d[1] += 1
        q = split_scope(split_params(f[3])[0])
        if len(q) > 1:
            d[2].add(q[0])
    out = open(args.out, "w", encoding="utf-8") if args.out else sys.stdout
    out.write("modulo\tobjeto\tbytes_text\tfuncoes\tclasses\n")
    for o, (b, n, cl) in per.items():
        out.write("%s\t%s\t%d\t%d\t%s\n" % (object_module(o), object_short(o), b, n, ",".join(sorted(cl))))


GAME_GROUPS = [
    # (grupo, prefixos de classe ou de função global)
    ("Personagens e animação (Act*)", ("Act", "AnimationController", "StandardAnimation", "CrossFadeAnimation",
                                       "BlendedAnimation", "AimedAnimation", "AimedShootingAnimation",
                                       "DefaultEventHandler", "FireEvent", "DropWeaponEvent", "HeightCalcEvent")),
    ("IA (AI*)", ("AI",)),
    ("Física e veículos (P*, RigidBody, Newton)", ("PBondCar", "PVehicle", "PHelicopter", "PTank", "PhysicsObject",
                                                    "PhysicsData", "RigidBody", "PackedRigidBody",
                                                    "SimpleRigidBody", "Newton", "OBB", "Float")),
    ("Simulação, missões e armas (S*, armas)", ("Simulation", "SMission", "SWeapon", "SimRandom", "Missile",
                                                 "Sentry", "Explosion", "Grenade", "Mine", "Shell", "RayShell",
                                                 "Damage", "Human", "Smackable", "Replay", "FeatureManager")),
    ("Eventos de missão (E*)", ("E",)),
    ("Mundo: colisão, estradas, gatilhos (W*)", ("W",)),
    ("Render e câmeras (R*, Draw)", ("R", "Draw", "GRenderUtil", "ColourConversion", "Noise", "noise")),
    ("HUD, galeria, carga, legendas (G*)", ("G",)),
    ("Depuração, ajuste e perfil (D*, Debug*)", ("D", "Debug", "Profile", "Sapphire", "ESapphire", "ps2prof",
                                                 "debug", "Metrics")),
    ("Infraestrutura (atributos, eventos, entrada, opções, cartão)", ("Attribute", "Event", "Input", "action",
                                                                       "Action", "Option", "MemCard", "Scheduler",
                                                                       "Path", "IFeedback", "Bond", "UberBond",
                                                                       "GameLoop", "RunTheGame", "main",
                                                                       "MATH", "Util", "shiftjis", "RealClock",
                                                                       "EPath")),
    ("STL, modelos e construtores globais", ("_", "basic_string", "vector", "list", "map", "pair", "__",
                                             "global ", "chunky", "string")),
]


def group_of(f, crt0):
    """Grupo aproximado de uma função pelo nome (e pela região: o código do jogo vem antes do crt0.o)."""
    addr, name, obj = f[1], f[3], f[4] or ""
    qual = split_params(name)[0]
    parts = split_scope(qual)
    top = parts[0] if len(parts) > 1 else qual
    if addr < crt0:
        if name.endswith(" type_info function"):
            top = name[:-len(" type_info function")]
        for g, prefixes in GAME_GROUPS:
            for p in prefixes:
                if p in ("E", "W", "R", "G", "D"):
                    if len(top) > 1 and top[0] == p and top[1].isupper():
                        return g
                elif top.startswith(p):
                    return g
        return "Jogo: outros"
    low = obj.lower()
    if "EAGLAnim" in name or "libeaglanima" in low:
        return "EAGL: animação (libeaglanima)"
    if "EAGL" in name or "libeagla" in low or "libbondrmz" in low:
        return "EAGL: gráficos (libeagla, libbondrmz)"
    if "bondaudio" in low or "plaympc" in low or "rcmp" in low or (top[:1] == "A" and top[1:2].isupper()) or \
            top.startswith(("URefCounter<A", "RCMP", "PlayMPC", "PS2_SONY_CODEC", "EAGL_VD")):
        return "Áudio e filmes do jogo (BondAudioPS2, PlayMPC/RCMP)"
    if "supportps2" in low or top.startswith(("U", "CARP", "LString")) and top[1:2].isupper() or top == "CARP":
        return "SupportPS2 (U*, CARP, SpeedTalk)"
    if "realps2" in low or name.startswith(("REAL", "SND", "MEM", "FILE", "THREAD")):
        return "REAL (EA: arquivos, memória, entrada, som SND, sistema)"
    if name.startswith(("sce", "_sce", "sif", "Sif", "isce")) or "/ee/lib/" in low.replace("\\", "/"):
        return "SDK da Sony (libkernl, libpad, libmc, libcdvd, libmpeg, libdma...)"
    return "C/C++ do GCC (libc, libm, libgcc)"


def cmd_modules(args):
    sym = load_sym(args.root)
    fn = sym.functions()
    crt0 = next((a for (a, n) in sym.entries if n.lower().endswith("crt0.o")), sym.rodata_start)
    conf = {}
    map_path = os.path.join(args.root, "out", "driving", "sym_map.tsv")
    if os.path.exists(map_path):
        for line in open(map_path, encoding="utf-8"):
            p = line.rstrip("\n").split("\t")
            if p[0] != "elf" and len(p) >= 9:
                conf[int(p[2], 16)] = int(p[5])
    cnt = collections.Counter()
    size = collections.Counter()
    named = collections.Counter()
    for f in fn:
        g = group_of(f, crt0)
        cnt[g] += 1
        size[g] += f[2]
        if conf.get(f[1], 0) >= 2:
            named[g] += 1
    total_n = sum(cnt.values())
    total_b = sum(size.values())
    print("%-62s %7s %9s %6s %s" % ("grupo (pelo nome)", "funções", "bytes", "%", "com nome no ELF (conf. >= 2)"))
    for g in sorted(cnt, key=lambda k: -size[k]):
        print("%-62s %7d %9d %5.1f%% %d" % (g, cnt[g], size[g], 100.0 * size[g] / total_b, named[g]))
    print("%-62s %7d %9d" % ("total (compilação do SYM)", total_n, total_b))


def cmd_classes(args):
    sym = load_sym(args.root)
    fn = sym.functions()
    methods = collections.Counter()
    where = {}
    for f in fn:
        q = split_scope(split_params(f[3])[0])
        if len(q) > 1:
            cls = "::".join(q[:-1])
            methods[cls] += 1
            where.setdefault(cls, object_module(f[4]) if f[4] else "?")
    by_mod = collections.defaultdict(list)
    for cls, n in methods.items():
        by_mod[where[cls]].append((n, cls))
    for m in sorted(by_mod, key=lambda k: -sum(n for n, _ in by_mod[k])):
        lst = sorted(by_mod[m], reverse=True)
        print("%s (%d classes, %d métodos): %s" % (m, len(lst), sum(n for n, _ in lst),
                                                   ", ".join("%s %d" % (c, n) for n, c in lst[:args.top])))


def cmd_match(args):
    functions = args.functions or os.path.join(args.root, "out", "driving", "functions.tsv")
    if not os.path.exists(functions):
        print("aviso: sem %s (tools/ghidra_driving.sh functions); só os alvos de jal e as tabelas virtuais"
              % functions)
        functions = None
    m = Matcher(args.root, functions)
    anchors, rows = m.rows()
    print("funções no SYM (.text): %d; inícios de função conhecidos no ELF: %d" % (len(m.sfuncs), len(m.starts)))
    print("âncoras candidatas: bibliotecas %d, type_info %d, construtores/destrutores %d, entrada %d" %
          (len(m.lib), len(m.tf), len(m.ctor), len(m.entry)))
    kinds = collections.Counter(r[4] for r in rows if r[5] == 3)
    print("âncoras na cadeia: %d (%s)" % (len(anchors), ", ".join("%s %d" % kv for kv in sorted(kinds.items()))))
    cc = collections.Counter(r[5] for r in rows)
    print("casadas: confiança 3 (âncora) %d, 2 (alinhada, tamanho igual) %d, 1 (alinhada) %d, sem par %d" %
          (cc[3], cc[2], cc[1], cc[0]))
    out_path = args.out or os.path.join(args.root, "out", "driving", "sym_map.tsv")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as out:
        out.write("elf\telf_tam\tsym\tsym_tam\tmetodo\tconfianca\tcusto\tnome\tobjeto\n")
        for e, esz, a, size, how, conf, c, name, obj in rows:
            out.write("%s\t%d\t%08x\t%d\t%s\t%d\t%.3f\t%s\t%s\n" % (
                "%08x" % e if e is not None else "-", esz, a, size, how, conf, c, name,
                object_short(obj) if obj else ""))
    print("mapa:", out_path)
    vt_path = os.path.join(os.path.dirname(out_path), "sym_vtables.tsv")
    with open(vt_path, "w", encoding="utf-8") as out:
        out.write("elf\tclasse\n")
        for cls, (vt, base) in sorted(m.hier.items(), key=lambda kv: kv[1][0][0] if kv[1][0] else 0):
            if vt:
                out.write("%08x\t%s\n" % (vt[0], cls))
    print("tabelas virtuais:", vt_path)
    if args.evaluate:
        m.evaluate()


def cmd_ghidra(args):
    rows = []
    for line in open(args.map, encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if p[0] in ("elf", "-") or len(p) < 9:
            continue
        rows.append((int(p[0], 16), int(p[5]), p[4], p[7], p[8]))
    seen = set()
    n = collections.Counter()
    with open(args.out, "w", encoding="utf-8") as out:
        # as de maior confiança primeiro: um endereço fica com o nome mais confiável
        for e, conf, how, name, obj in sorted(rows, key=lambda r: (-r[1], r[0])):
            if e in seen or conf < 1:
                continue
            seen.add(e)
            note = "SYM: %s [%s; confiança %d, %s]" % (name, obj, conf, how)
            if conf >= args.min:
                ns, nm = ghidra_name(name)
                out.write("%08x\tF\t%s\t%s\t%s\n" % (e, ns, nm, note))
                n["F"] += 1
            else:
                out.write("%08x\tC\t\t\t%s (só sugestão: alinhamento pelo tamanho)\n" % (e, note))
                n["C"] += 1
        vt_path = args.vtables or os.path.join(os.path.dirname(args.map), "sym_vtables.tsv")
        if os.path.exists(vt_path):
            for line in open(vt_path, encoding="utf-8"):
                p = line.rstrip("\n").split("\t")
                if len(p) >= 2 and p[0] != "elf":
                    ns = "::".join(split_scope(p[1]))
                    out.write("%s\tD\t%s\t__vt\ttabela virtual de %s\n" % (p[0], ns, p[1]))
                    n["D"] += 1
    print("%s: %d funções com nome (confiança >= %d), %d sugestões em comentário, %d tabelas virtuais" %
          (args.out, n["F"], args.min, n["C"], n["D"]))


def cmd_test(args):
    # SYM montado à mão: três entradas e os nomes
    names = [b"_stack_size", b"d:\\ToBurn\\X\\PS2_EE_Release\\A.obj", b"A::f(int)"]
    first = 8 * len(names)
    pool = b""
    offs = []
    for nm in names:
        offs.append(first + len(pool))
        pool += nm + b"\0"
    blob = b"".join(struct.pack("<II", v, o) for v, o in zip((0x40000, 0x100000, 0x100008), offs)) + pool
    ent = parse_sym(blob)
    assert ent == [(0x40000, "_stack_size"), (0x100000, "d:\\ToBurn\\X\\PS2_EE_Release\\A.obj"),
                   (0x100008, "A::f(int)")], ent
    assert is_object(ent[1][1]) and not is_object(ent[2][1])
    assert is_object("d:\\ToBurn\\Library\\SN\\PS2\\ee\\lib\\libpad.a(libpad.o)")
    assert object_module("d:\\ToBurn\\Library\\SN\\PS2\\ee\\lib\\libpad.a(libpad.o)") == "SN/libpad"
    assert object_module("d:\\ToBurn\\BondPS2\\PS2_EE_Release\\RWater.obj") == "BondPS2"
    # fora de ordem
    bad = b"".join(struct.pack("<II", v, o) for v, o in zip((2, 1, 3), offs)) + pool
    try:
        parse_sym(bad)
        raise AssertionError("aceitou fora de ordem")
    except ValueError:
        pass
    assert mangle_type("AnimationController") == "19AnimationController"
    assert mangle_type("EAGLAnim::FnAnimBank") == "Q28EAGLAnim10FnAnimBank"
    assert mangle_type("DebugVariable<unsigned int>") is None
    assert split_params("A::B<int, C>::f(int, float)") == ("A::B<int, C>::f", "(int, float)")
    assert split_params("A::operator()(int)") == ("A::operator()", "(int)")
    assert split_scope("A::B<x::y>::f") == ["A", "B<x::y>", "f"]
    assert ghidra_name("A::B::f(int)") == ("A::B", "f")
    assert ghidra_name("X type_info function") == ("X", "__tf")
    assert chain([(0, 10), (1, 5), (1, 20), (2, 30), (3, 25)]) in ([(0, 10), (1, 20), (2, 30)],
                                                                    [(0, 10), (1, 20), (3, 25)])
    # alinhamento: três funções do SYM sobre quatro do ELF (a do meio partida em duas)
    al = align_segment([(0, 100), (100, 200), (300, 100)], [(0, 90), (90, 120), (210, 60), (270, 90)], 0.9)
    assert al[0][0] == 0 and al[1][0] == 1 and al[1][1] == 2 and al[2][0] == 3, al
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    p = sub.add_parser("list")
    p.add_argument("out", nargs="?")
    p = sub.add_parser("objs")
    p.add_argument("out", nargs="?")
    sub.add_parser("modules")
    p = sub.add_parser("classes")
    p.add_argument("--top", type=int, default=12)
    p = sub.add_parser("match")
    p.add_argument("--functions")
    p.add_argument("--out")
    p.add_argument("--evaluate", action="store_true")
    p = sub.add_parser("ghidra")
    p.add_argument("map")
    p.add_argument("out")
    p.add_argument("--min", type=int, default=2)
    p.add_argument("--vtables")
    sub.add_parser("test")
    args = ap.parse_args()
    {"info": cmd_info, "list": cmd_list, "objs": cmd_objs, "modules": cmd_modules, "classes": cmd_classes, "match": cmd_match,
     "ghidra": cmd_ghidra, "test": cmd_test}[args.cmd](args)


if __name__ == "__main__":
    main()
