"""Desmontador dos microprogramas da VU (VU0/VU1) do ACTION.ELF.

O código das VUs fica na seção .vutext como pacotes de DMA com comandos MPG da VIF (cada MPG leva até
256 instruções a um endereço da memória de microprograma). Há duas cadeias: a primeira
(My_scissor_code) é o microprograma da VU1 (recorte, rotinas de desenho das malhas, partículas); a
segunda (My_dma_start) é o da VU0 que o SkinIt chama (_$SKIN_2_BONE...). As seções .DVP.overlay..0x*
têm o mesmo código pelo endereço da VU. Os rótulos (_$ROTATE_LIGHT...) vêm da tabela de símbolos.

Cada instrução tem 64 bits: a palavra baixa é a instrução inferior (lower) e a alta a superior
(upper). Bits da superior: 31 I (a inferior é um float carregado em I, LOI), 30 E (fim: o programa
para depois do par seguinte), 29 M, 28 D, 27 T.

Uso:
  python tools/vudis.py ACTION.ELF                    # VU1 inteiro
  python tools/vudis.py ACTION.ELF 0x340 0x3c0        # [início, fim) em instruções (x8 = bytes)
  python tools/vudis.py ACTION.ELF 0x340              # do início até o par depois do bit E ou do JR
  python tools/vudis.py ACTION.ELF ROTATE_LIGHT       # pelo rótulo (com ou sem _$)
  python tools/vudis.py ACTION.ELF --vu0              # o microprograma da VU0 (skinning)
  python tools/vudis.py ACTION.ELF --labels           # só os rótulos
  python tools/vudis.py DRIVING.ELF --prog=1 0xad      # DRIVING.ELF: três programas da VU0 (0 charAnim, 1 física,
                                                      # 2 cortinas); sem rótulos (estão no DRIVING.SYM, em bytes)
  python tools/vudis.py --eagl eaglrm.o LitGouraud     # um render method do EAGL (MISC.VIV), microprograma da VU1
  python tools/vudis.py --test                        # confere a decodificação de palavras conhecidas
"""
import struct
import sys

# ---------------------------------------------------------------- decodificação

BC = "xyzw"


def dest(w):
    d = (w >> 21) & 0xF
    return "".join(c for c, bit in zip("xyzw", (8, 4, 2, 1)) if d & bit)


def vf(n):
    return f"vf{n:02d}"


def vi(n):
    return f"vi{n:02d}"


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


UPPER_BC = {0x00: "ADD", 0x04: "SUB", 0x08: "MADD", 0x0C: "MSUB", 0x10: "MAX", 0x14: "MINI", 0x18: "MUL"}
UPPER_QI = {0x1C: ("MUL", "Q"), 0x1D: ("MAX", "I"), 0x1E: ("MUL", "I"), 0x1F: ("MINI", "I"),
            0x20: ("ADD", "Q"), 0x21: ("MADD", "Q"), 0x22: ("ADD", "I"), 0x23: ("MADD", "I"),
            0x24: ("SUB", "Q"), 0x25: ("MSUB", "Q"), 0x26: ("SUB", "I"), 0x27: ("MSUB", "I")}
UPPER_3 = {0x28: "ADD", 0x29: "MADD", 0x2A: "MUL", 0x2B: "MAX", 0x2C: "SUB", 0x2D: "MSUB", 0x2E: "OPMSUB",
           0x2F: "MINI"}
# Especiais (bits 5-2 = 1111): código = bits 10-6 << 2 | bits 1-0.
UPPER_SP_BC = {0x00: "ADDA", 0x04: "SUBA", 0x08: "MADDA", 0x0C: "MSUBA", 0x18: "MULA"}
UPPER_SP_CONV = {0x10: "ITOF0", 0x11: "ITOF4", 0x12: "ITOF12", 0x13: "ITOF15",
                 0x14: "FTOI0", 0x15: "FTOI4", 0x16: "FTOI12", 0x17: "FTOI15", 0x1D: "ABS"}
UPPER_SP_QI = {0x1C: ("MULA", "Q"), 0x1E: ("MULA", "I"), 0x20: ("ADDA", "Q"), 0x21: ("MADDA", "Q"),
               0x22: ("ADDA", "I"), 0x23: ("MADDA", "I"), 0x24: ("SUBA", "Q"), 0x25: ("MSUBA", "Q"),
               0x26: ("SUBA", "I"), 0x27: ("MSUBA", "I")}
UPPER_SP_3 = {0x28: "ADDA", 0x29: "MADDA", 0x2A: "MULA", 0x2C: "SUBA", 0x2D: "MSUBA", 0x2E: "OPMULA"}


def upper(w):
    """Texto da instrução superior (sem os bits de controle)."""
    op = w & 0x3F
    d = dest(w)
    ft, fs, fd = (w >> 16) & 0x1F, (w >> 11) & 0x1F, (w >> 6) & 0x1F
    if (op & 0x3C) == 0x3C:
        sp = (((w >> 6) & 0x1F) << 2) | (w & 3)
        if sp == 0x2F:
            return "NOP"
        if (sp & 0x3C) in UPPER_SP_BC and sp < 0x1C:
            return f"{UPPER_SP_BC[sp & 0x3C]}{BC[sp & 3]}.{d} ACC, {vf(fs)}, {vf(ft)}{BC[sp & 3]}"
        if sp in UPPER_SP_CONV:
            return f"{UPPER_SP_CONV[sp]}.{d} {vf(ft)}, {vf(fs)}"
        if sp in UPPER_SP_QI:
            name, r = UPPER_SP_QI[sp]
            return f"{name}{r.lower()}.{d} ACC, {vf(fs)}, {r}"
        if sp == 0x1F:
            return f"CLIPw.xyz {vf(fs)}, {vf(ft)}w"
        if sp in UPPER_SP_3:
            return f"{UPPER_SP_3[sp]}.{d} ACC, {vf(fs)}, {vf(ft)}"
        return f"??? upper 0x{w & 0x7FFFFFF:07x}"
    if op < 0x1C:
        return f"{UPPER_BC[op & 0x3C]}{BC[op & 3]}.{d} {vf(fd)}, {vf(fs)}, {vf(ft)}{BC[op & 3]}"
    if op in UPPER_QI:
        name, r = UPPER_QI[op]
        return f"{name}{r.lower()}.{d} {vf(fd)}, {vf(fs)}, {r}"
    if op in UPPER_3:
        return f"{UPPER_3[op]}.{d} {vf(fd)}, {vf(fs)}, {vf(ft)}"
    return f"??? upper 0x{w & 0x7FFFFFF:07x}"


LOWER_SP = {
    0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD",
    0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR",
    0x3E: "ILWR", 0x3F: "ISWR", 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR",
    0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK",
    0x70: "ESADD", 0x71: "ERSADD", 0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy", 0x75: "EATANxz",
    0x76: "ESUM", 0x78: "ESQRT", 0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP", 0x7C: "ESIN",
    0x7D: "EATAN", 0x7E: "EEXP",
}
BRANCHES = {0x20: "B", 0x21: "BAL", 0x28: "IBEQ", 0x29: "IBNE", 0x2C: "IBLTZ", 0x2D: "IBGTZ",
            0x2E: "IBLEZ", 0x2F: "IBGEZ"}


def lower(w, pc=0, labels=None, mask=0x7FF):
    """Texto da instrução inferior; pc (em instruções) resolve o destino dos desvios, que dão a volta
    na memória de microprograma (mask: 0x7ff na VU1, 16 KB; 0x1ff na VU0, 4 KB)."""
    op = (w >> 25) & 0x7F
    d = dest(w)
    it, is_, fd = (w >> 16) & 0x1F, (w >> 11) & 0x1F, (w >> 6) & 0x1F
    fsf, ftf = BC[(w >> 21) & 3], BC[(w >> 23) & 3]
    imm11 = sext(w & 0x7FF, 11)
    if op == 0x40:
        fn = w & 0x3F
        if fn == 0x30:
            return f"IADD {vi(fd)}, {vi(is_)}, {vi(it)}"
        if fn == 0x31:
            return f"ISUB {vi(fd)}, {vi(is_)}, {vi(it)}"
        if fn == 0x32:
            return f"IADDI {vi(it)}, {vi(is_)}, {sext(fd, 5)}"
        if fn == 0x34:
            return f"IAND {vi(fd)}, {vi(is_)}, {vi(it)}"
        if fn == 0x35:
            return f"IOR {vi(fd)}, {vi(is_)}, {vi(it)}"
        if (fn & 0x3C) != 0x3C:
            return f"??? lower 0x{w:08x}"
        sp = (((w >> 6) & 0x1F) << 2) | (w & 3)
        name = LOWER_SP.get(sp)
        if name is None:
            return f"??? lower 0x{w:08x}"
        fs, ft = is_, it  # nos especiais, bits 15-11 = fs/is, bits 20-16 = ft/it
        if name == "MOVE" and d == "" and fs == 0 and ft == 0:
            return "NOP"
        if name in ("MOVE", "MR32"):
            return f"{name}.{d} {vf(ft)}, {vf(fs)}"
        if name == "LQI":
            return f"LQI.{d} {vf(ft)}, ({vi(is_)}++)"
        if name == "SQI":
            return f"SQI.{d} {vf(fs)}, ({vi(it)}++)"
        if name == "LQD":
            return f"LQD.{d} {vf(ft)}, (--{vi(is_)})"
        if name == "SQD":
            return f"SQD.{d} {vf(fs)}, (--{vi(it)})"
        if name in ("DIV", "RSQRT"):
            return f"{name} Q, {vf(fs)}{fsf}, {vf(ft)}{ftf}"
        if name == "SQRT":
            return f"SQRT Q, {vf(ft)}{ftf}"
        if name in ("WAITQ", "WAITP"):
            return name
        if name == "MTIR":
            return f"MTIR {vi(it)}, {vf(fs)}{fsf}"
        if name == "MFIR":
            return f"MFIR.{d} {vf(ft)}, {vi(is_)}"
        if name in ("ILWR", "ISWR"):
            return f"{name}.{d} {vi(it)}, ({vi(is_)})"
        if name in ("RNEXT", "RGET"):
            return f"{name}.{d} {vf(ft)}, R"
        if name in ("RINIT", "RXOR"):
            return f"{name} R, {vf(fs)}{fsf}"
        if name == "MFP":
            return f"MFP.{d} {vf(ft)}, P"
        if name in ("XTOP", "XITOP"):
            return f"{name} {vi(it)}"
        if name == "XGKICK":
            return f"XGKICK {vi(is_)}"
        if name in ("ESADD", "ERSADD", "ELENG", "ERLENG", "EATANxy", "EATANxz", "ESUM"):
            return f"{name} P, {vf(fs)}"
        return f"{name} P, {vf(fs)}{fsf}"
    if op == 0x00:
        return f"LQ.{d} {vf(it)}, {imm11}({vi(is_)})"
    if op == 0x01:
        return f"SQ.{d} {vf(is_)}, {imm11}({vi(it)})"
    if op == 0x04:
        return f"ILW.{d} {vi(it)}, {imm11}({vi(is_)})"
    if op == 0x05:
        return f"ISW.{d} {vi(it)}, {imm11}({vi(is_)})"
    if op in (0x08, 0x09):
        imm15 = ((w >> 10) & 0x7800) | (w & 0x7FF)
        return f"{'IADDIU' if op == 8 else 'ISUBIU'} {vi(it)}, {vi(is_)}, 0x{imm15:x}"
    if op in (0x10, 0x12, 0x13):
        return f"{ {0x10: 'FCEQ', 0x12: 'FCAND', 0x13: 'FCOR'}[op]} vi01, 0x{w & 0xFFFFFF:06x}"
    if op == 0x11:
        return f"FCSET 0x{w & 0xFFFFFF:06x}"
    if op in (0x14, 0x16, 0x17):
        imm12 = ((w >> 10) & 0x800) | (w & 0x7FF)
        return f"{ {0x14: 'FSEQ', 0x16: 'FSAND', 0x17: 'FSOR'}[op]} {vi(it)}, 0x{imm12:03x}"
    if op == 0x15:
        imm12 = ((w >> 10) & 0x800) | (w & 0x7FF)
        return f"FSSET 0x{imm12:03x}"
    if op in (0x18, 0x1A, 0x1B):
        return f"{ {0x18: 'FMEQ', 0x1A: 'FMAND', 0x1B: 'FMOR'}[op]} {vi(it)}, {vi(is_)}"
    if op == 0x1C:
        return f"FCGET {vi(it)}"
    if op in BRANCHES:
        target = (pc + 1 + imm11) & mask
        where = f"0x{target:03x}"
        if labels and target in labels:
            where += f" <{labels[target]}>"
        name = BRANCHES[op]
        if name == "B":
            return f"B {where}"
        if name == "BAL":
            return f"BAL {vi(it)}, {where}"
        if name in ("IBEQ", "IBNE"):
            return f"{name} {vi(it)}, {vi(is_)}, {where}"
        return f"{name} {vi(is_)}, {where}"
    if op == 0x24:
        return f"JR {vi(is_)}"
    if op == 0x25:
        return f"JALR {vi(it)}, {vi(is_)}"
    return f"??? lower 0x{w:08x}"


def flags(up):
    return "".join(c for c, bit in zip("IEMDT", (31, 30, 29, 28, 27)) if (up >> bit) & 1)


def disasm_pair(lo, up, pc=0, labels=None, mask=0x7FF):
    """(upper, lower, flags) de um par; com o bit I a inferior vira LOI <float>."""
    u = upper(up)
    if (up >> 31) & 1:
        f = struct.unpack("<f", struct.pack("<I", lo))[0]
        low = f"LOI {f:.9g} (0x{lo:08x})"
    else:
        low = lower(lo, pc, labels, mask)
    return u, low, flags(up)


# ---------------------------------------------------------------- ELF e .vutext

def load_elf(path):
    d = open(path, "rb").read()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]

    def cstr(sec, idx):
        start = sec[4] + idx
        return d[start:d.index(b"\0", start)].decode("latin1")

    sections = {cstr(secs[shstrndx], s[0]): s for s in secs}
    syms = []
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            name, value, _size, _info, _other, _shndx = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
            if name:
                syms.append((value, cstr(strtab, name)))
    return d, sections, syms


def load_programs(path):
    """Lê as cadeias de DMA da .vutext. Devolve uma lista de programas: (memória de microprograma
    como dict instrução -> (lower, upper), rótulos instrução -> nome)."""
    d, sections, syms = load_elf(path)
    vt = sections[".vutext"]
    base_addr, off, size = vt[3], vt[4], vt[5]
    programs = []
    code, spans = {}, []
    p = 0
    while p + 16 <= size:
        tag = struct.unpack_from("<Q", d, off + p)[0]
        qwc, tid = tag & 0xFFFF, (tag >> 28) & 7
        if tag == 0:
            p += 16
            continue
        # ACTION.ELF: cada programa é uma tag END com os dados; DRIVING.ELF: uma tag CNT com os dados e uma END vazia.
        if tid not in (1, 7):
            raise ValueError(f"tag de DMA inesperada em .vutext+0x{p:x}: 0x{tag:016x}")
        q = p + 8
        end = p + 16 + qwc * 16
        while q < end:
            w = struct.unpack_from("<I", d, off + q)[0]
            cmd, num, imm = (w >> 24) & 0x7F, (w >> 16) & 0xFF, w & 0xFFFF
            q += 4
            if cmd == 0x4A:  # MPG
                n = num or 256
                for i in range(n):
                    lo, up = struct.unpack_from("<II", d, off + q + i * 8)
                    code[imm + i] = (lo, up)
                spans.append((base_addr + q, base_addr + q + n * 8, imm))
                q += n * 8
            elif cmd in (0, 0x10, 0x11, 0x13, 0x17):  # NOP, FLUSHE, FLUSH, FLUSHA, MSCNT
                pass
            else:
                raise ValueError(f"código VIF inesperado em .vutext+0x{q - 4:x}: 0x{w:08x}")
        p = end
        if tid != 7:
            continue
        labels = {}
        for value, name in syms:
            if not name.startswith("_$"):
                continue
            for start, stop, addr in spans:
                if start <= value < stop:
                    labels.setdefault(addr + (value - start) // 8, name[2:])
        programs.append((code, labels))
        code, spans = {}, []
    return programs


def load_eagl_method(path, method):
    """O microprograma da VU1 de um render method do EAGL (DRIVING: data/render/eaglrm.o e bondrm.o do MISC.VIV).
    O símbolo <método>0__EAGLMicroCode aponta um cabeçalho cuja primeira palavra é o número de instruções; logo antes
    fica a cadeia de DMA que o envia: um pedaço por comando MPG da VIF (até 256 instruções; o endereço na VU vem zerado e
    é preenchido na carga), cada um seguido de uma tag REF com o endereço do código na .data. Os pedaços ficam seguidos
    na .data. Devolve (código como em load_programs, rótulos vazios)."""
    d, sections, syms = load_elf(path)
    data = sections[".data"]
    off = data[4]
    name = method if method.endswith("__EAGLMicroCode") else method + "0__EAGLMicroCode"
    value = next((v for v, n in syms if n == name), None)
    if value is None:
        raise ValueError(f"{path}: sem o símbolo {name}")
    count = struct.unpack_from("<I", d, off + value)[0]
    chunks = []                                # (endereço do código, instruções), do último para o primeiro
    total = 0
    for a in range(value - 4, max(value - 0x1000, 0), -4):
        w = struct.unpack_from("<I", d, off + a)[0]
        if (w >> 24) & 0x7F != 0x4A:
            continue
        tag = struct.unpack_from("<Q", d, off + a + 4)[0]
        if (tag >> 28) & 7 != 3:               # REF
            continue
        n = (w >> 16) & 0xFF or 256
        chunks.append(((tag >> 32) & 0xFFFFFFFF, n))
        total += n
        if total >= count:
            break
    chunks.reverse()
    if total != count or any(a + 8 * n != b for (a, n), (b, _) in zip(chunks, chunks[1:])):
        raise ValueError(f"{path}: a cadeia de {name} não fecha ({total} de {count} instruções)")
    base = chunks[0][0]
    return {i: struct.unpack_from("<II", d, off + base + 8 * i) for i in range(count)}, {}


def parse_addr(text, labels):
    for addr, name in labels.items():
        if name == text or "_$" + name == text:
            return addr
    return int(text, 0)


def print_range(code, labels, start, stop, mask=0x7FF, out=sys.stdout):
    pc = start
    after_end = None
    while pc in code and (stop is None or pc < stop):
        if pc in labels:
            print(f"{labels[pc]}:", file=out)
        lo, up = code[pc]
        u, low, fl = disasm_pair(lo, up, pc, labels, mask)
        tag = f"[{fl}]" if fl else ""
        print(f"  {pc:04x} {pc * 8:05x}  {tag:5s} {u:38s} {low}", file=out)
        # Sem fim pedido: para no par seguinte a um bit E ou a um JR/JALR (fim de rotina), que ainda roda.
        ends = (up >> 30) & 1 or (not (up >> 31) & 1 and ((lo >> 25) & 0x7F) in (0x24, 0x25))
        if stop is None and after_end is None and ends:
            after_end = pc + 1
        if after_end is not None and pc >= after_end:
            break
        pc += 1


# ---------------------------------------------------------------- testes

def self_test():
    cases = [
        # (lower, upper, superior esperada, inferior esperada)
        (0x8000033C, 0x000002FF, "NOP", "NOP"),
        (0x8000033C, 0x400002FF, "NOP", "NOP"),                                  # bit E
        (0x3F800000, 0x800002FF, "NOP", "LOI 1 (0x3f800000)"),
        (0x8000033C, 0x01E4012A, "MUL.xyzw vf04, vf00, vf04", "NOP"),
        (0x8000033C, 0x01E1203C, "ADDAx.xyzw ACC, vf04, vf01x", "NOP"),
        (0x8000033C, 0x01E128C8, "MADDx.xyzw vf03, vf05, vf01x", "NOP"),
        (0x8000033C, 0x01E0003C, "ADDAx.xyzw ACC, vf00, vf00x", "NOP"),
        (0x8000033C, 0x01C721FF, "CLIPw.xyz vf04, vf07w", "NOP"),
        (0x8000033C, 0x01E5293C, "ITOF0.xyzw vf05, vf05", "NOP"),
        (0x8000033C, 0x01E0017D, "FTOI4.xyzw vf00, vf00", "NOP"),
        (0x8000033C, 0x0021001E, "MULi.w vf00, vf00, I", "NOP"),
        (0x8000033C, 0x0000001C, "MULq. vf00, vf00, Q", "NOP"),
        (0x0000033C | 0x8000033C, 0x000002FF, "NOP", "NOP"),
        (0x8000033C | 0x01E00000 | (2 << 16) | (3 << 11), 0x000002FF, "NOP", "MOVE.xyzw vf02, vf03"),
        (0x8000037C | 0x01E00000 | (4 << 16) | (5 << 11), 0x000002FF, "NOP", "LQI.xyzw vf04, (vi05++)"),
        (0x8000037D | 0x01E00000 | (6 << 16) | (7 << 11), 0x000002FF, "NOP", "SQI.xyzw vf07, (vi06++)"),
        (0x800003BC | (1 << 21) | (3 << 23) | (8 << 11) | (9 << 16), 0x000002FF, "NOP", "DIV Q, vf08y, vf09w"),
        (0x800003BF, 0x000002FF, "NOP", "WAITQ"),
        (0x800006FC | (10 << 11), 0x000002FF, "NOP", "XGKICK vi10"),
        (0x800006BD | (2 << 16), 0x000002FF, "NOP", "XITOP vi02"),
        (0x800003FC | (3 << 16) | (4 << 11) | (2 << 21), 0x000002FF, "NOP", "MTIR vi03, vf04z"),
        (0x800007BE | (5 << 11) | (3 << 21), 0x000002FF, "NOP", "ERCPR P, vf05w"),
        (0x8000067C | 0x00200000 | (6 << 16), 0x000002FF, "NOP", "MFP.w vf06, P"),
        (0x00000000 | 0x01E00000 | (1 << 16) | (2 << 11) | 5, 0x000002FF, "NOP", "LQ.xyzw vf01, 5(vi02)"),
        (0x02000000 | 0x01E00000 | (1 << 16) | (2 << 11) | 0x7FF, 0x000002FF, "NOP", "SQ.xyzw vf02, -1(vi01)"),
        (0x10000000 | (1 << 16) | (2 << 11) | 0x7FF | (0xF << 21), 0x000002FF, "NOP", "IADDIU vi01, vi02, 0x7fff"),
        (0x80000030 | (1 << 6) | (2 << 11) | (3 << 16), 0x000002FF, "NOP", "IADD vi01, vi02, vi03"),
        (0x80000032 | (0x1F << 6) | (2 << 11) | (3 << 16), 0x000002FF, "NOP", "IADDI vi03, vi02, -1"),
        (0x40000000 | 0x7FE, 0x000002FF, "NOP", "B 0x00f"),                       # pc 0x10 + 1 - 2
        (0x52000000 | (1 << 16) | (2 << 11) | 3, 0x000002FF, "NOP", "IBNE vi01, vi02, 0x014"),
        (0x48000000 | (4 << 11), 0x000002FF, "NOP", "JR vi04"),
        (0x2C000000 | (1 << 16) | 0x7FF, 0x000002FF, "NOP", "FSAND vi01, 0x7ff"),
        (0x24000000 | 0x123456, 0x000002FF, "NOP", "FCAND vi01, 0x123456"),
    ]
    bad = 0
    for lo, up, eu, el in cases:
        u, low, _ = disasm_pair(lo, up, 0x10)
        if u != eu or low != el:
            print(f"FALHOU {lo:08x} {up:08x}: '{u}' | '{low}' (esperado '{eu}' | '{el}')")
            bad += 1
    if flags(0x400002FF) != "E" or flags(0x800002FF) != "I" or flags(0x080002FF) != "T":
        print("FALHOU bits de controle")
        bad += 1
    print(f"{len(cases) - bad}/{len(cases)} casos ok")
    return bad == 0


def main(argv):
    if "--test" in argv:
        return 0 if self_test() else 1
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 1
    if "--eagl" in argv:
        code, labels = load_eagl_method(args[0], args[1])
        print_range(code, labels, 0, max(code) + 1, 0x3FF)
        return 0
    programs = load_programs(args[0])
    prog = 1 if "--vu0" in argv else 0
    for a in argv:
        if a.startswith("--prog="):
            prog = int(a.split("=", 1)[1])
    code, labels = programs[prog]
    vu0 = "--vu0" in argv or prog > 0 and len(programs) > 2  # DRIVING.ELF: os três programas são da VU0
    if "--labels" in argv:
        for addr in sorted(labels):
            print(f"  {addr:04x} {addr * 8:05x}  {labels[addr]}")
        return 0
    start = parse_addr(args[1], labels) if len(args) > 1 else min(code)
    stop = parse_addr(args[2], labels) if len(args) > 2 else (None if len(args) > 1 else max(code) + 1)
    print_range(code, labels, start, stop, 0x1FF if vu0 or len(programs) > 2 else 0x7FF)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
