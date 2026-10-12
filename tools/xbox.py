#!/usr/bin/env python3
"""Lê a cópia da versão de Xbox, só como referência de leitura do código das missões de carro (docs/driving.md).

A imagem do disco é XDVDFS: a .xiso (extract-xiso e afins) começa na partição do jogo; a imagem inteira de um disco
(redump) tem a partição de vídeo antes, e a do jogo começa em 0x18300000. O executável é um XBE: cabeçalho, certificado
(título, região, versão), seções, as bibliotecas do XDK com as versões e o caminho do build.

A cópia fica em xbox/ (no .gitignore): nada dela vai para o repositório.

  python tools/xbox.py info <imagem|arquivo.xbe>      resumo: partição, arquivos, os XBE e o cabeçalho do default.xbe
  python tools/xbox.py list <imagem>                  os arquivos com setor e tamanho
  python tools/xbox.py extract <imagem> <pasta>       extrai tudo (ex.: xbox/disco)
  python tools/xbox.py xbe <arquivo.xbe>              o cabeçalho completo de um XBE
  python tools/xbox.py pe <arquivo.xbe> <saida.exe>   um PE32 equivalente para o Ghidra (e <saida>.labels.tsv: os
                                                      thunks do kernel e as seções do XDK, para o ApplySymbolTable)
  python tools/xbox.py vsh <objeto.o> <método|0xposição>   o programa de vértice (NV2A) de um render method do EAGL
                                                      de Xbox (o símbolo <método>__EAGLMicroCodeVS)
  python tools/xbox.py test                           testes com uma imagem e um XBE montados à mão
"""

import datetime
import os
import struct
import sys
import tempfile

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Onde a partição do jogo pode começar: .xiso (0), imagem inteira de XGD1 (Xbox), XGD2 e XGD3 (Xbox 360).
PARTITION_OFFSETS = ((0, "xiso"), (0x18300000, "imagem inteira (XGD1)"), (0xFD90000, "XGD2"), (0x2080000, "XGD3"))

# Chaves XOR do ponto de entrada e da tabela de thunks do kernel (retail, debug).
ENTRY_KEYS = ((0xA8FC57AB, "retail"), (0x94859D4B, "debug"))
THUNK_KEYS = ((0x5B6D40B6, "retail"), (0xEFB1F152, "debug"))
REGIONS = ((1, "América do Norte"), (2, "Japão"), (4, "resto do mundo (PAL)"), (0x80000000, "fabricação"))


# ---- XDVDFS ----

class Xiso:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.base = None
        for off, kind in PARTITION_OFFSETS:
            self.f.seek(off + 32 * SECTOR)
            head = self.f.read(SECTOR)
            if len(head) == SECTOR and head[:20] == MAGIC and head[0x7EC:0x7EC + 20] == MAGIC:
                self.base, self.kind = off, kind
                break
        if self.base is None:
            self.f.close()
            raise ValueError(f"{path}: não achei o volume XDVDFS (MICROSOFT*XBOX*MEDIA) em nenhuma partição conhecida")
        self.root_sector, self.root_size = struct.unpack_from("<II", head, 20)
        self.filetime = struct.unpack_from("<Q", head, 28)[0]

    def close(self):
        self.f.close()

    def read(self, sector, size):
        self.f.seek(self.base + sector * SECTOR)
        return self.f.read(size)

    def _table(self, sector, size):
        # Uma tabela de diretório é uma árvore binária: cada entrada tem os deslocamentos (em palavras de 4 bytes) das
        # subárvores esquerda e direita; 0 = nenhuma (a raiz da tabela está em 0). O espaço livre é 0xFF.
        if size == 0:
            return []
        table = self.read(sector, size)
        out, seen, stack = [], set(), [0]
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(table):
                continue
            seen.add(off)
            left, right, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = table[off + 14:off + 14 + nlen].decode("latin-1")
            out.append((name, attr, start, fsize))
            if left:
                stack.append(left * 4)
            if right:
                stack.append(right * 4)
        out.sort(key=lambda e: e[0].lower())
        return out

    def walk(self, sector=None, size=None, prefix="", depth=0):
        """(caminho, é_pasta, setor, tamanho) de cada entrada, em ordem."""
        if sector is None:
            sector, size = self.root_sector, self.root_size
        if depth > 64:
            return
        for name, attr, start, fsize in self._table(sector, size):
            path = prefix + name
            is_dir = bool(attr & 0x10)
            yield path, is_dir, start, fsize
            if is_dir:
                yield from self.walk(start, fsize, path + "/", depth + 1)

    def find(self, wanted):
        wanted = wanted.lower()
        for path, is_dir, start, size in self.walk():
            if not is_dir and path.lower() == wanted:
                return start, size
        return None

    def read_file(self, path):
        hit = self.find(path)
        return None if hit is None else self.read(*hit)


def _safe_name(name):
    # Os nomes vêm de um arquivo de fora: nada de separadores, "..", nem nomes vazios.
    return name not in ("", ".", "..") and not any(c in name for c in "/\\:\0")


def extract(img, dest):
    files = dirs = total = refused = 0
    root = os.path.abspath(dest)
    os.makedirs(root, exist_ok=True)
    for path, is_dir, start, size in img.walk():
        parts = path.split("/")
        if not all(_safe_name(p) for p in parts):
            refused += 1
            print(f"recusado (nome inválido): {path!r}", file=sys.stderr)
            continue
        out = os.path.abspath(os.path.join(root, *parts))
        if os.path.commonpath([root, out]) != root:
            refused += 1
            continue
        if is_dir:
            os.makedirs(out, exist_ok=True)
            dirs += 1
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        img.f.seek(img.base + start * SECTOR)
        left = size
        with open(out, "wb") as o:
            while left:
                chunk = img.f.read(min(left, 1 << 20))
                if not chunk:
                    break
                o.write(chunk)
                left -= len(chunk)
        files += 1
        total += size
    return files, dirs, total, refused


# ---- XBE ----

def _cstr(data, off):
    if off is None or off < 0 or off >= len(data):
        return None
    end = data.find(b"\0", off)
    return data[off:end if end >= 0 else len(data)].decode("latin-1")


def parse_xbe(data):
    if data[:4] != b"XBEH":
        raise ValueError("não é um XBE (falta XBEH)")
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    base = u32(0x104)
    size_headers, size_image = u32(0x108), u32(0x10C)

    def va(addr):
        off = addr - base
        return off if 0 <= off < len(data) else None

    x = {"base": base, "size_headers": size_headers, "size_image": size_image, "timedate": u32(0x114)}
    entry = u32(0x128)
    x["entry"], x["kind"] = entry, "?"
    for key, kind in ENTRY_KEYS:
        if base <= (entry ^ key) < base + size_image:
            x["entry"], x["kind"] = entry ^ key, kind
            break
    thunk = u32(0x158)
    x["kernel_thunk"] = thunk
    for key, kind in THUNK_KEYS:
        if kind == x["kind"]:
            x["kernel_thunk"] = thunk ^ key
    x["debug_path"] = _cstr(data, va(u32(0x14C)))
    x["debug_file"] = _cstr(data, va(u32(0x150)))

    c = va(u32(0x118))
    if c is not None and c + 0xB0 <= len(data):
        x["title_id"] = u32(c + 8)
        x["title"] = data[c + 0xC:c + 0xC + 80].decode("utf-16-le", "replace").split("\0")[0]
        x["region"] = u32(c + 0xA0)
        x["disk"] = u32(c + 0xA8)
        x["version"] = u32(c + 0xAC)
        x["cert_timedate"] = u32(c + 4)

    x["sections"] = []
    s = va(u32(0x120))
    for i in range(u32(0x11C)):
        if s is None or s + (i + 1) * 0x38 > len(data):
            break
        flags, vaddr, vsize, raw, rawsize, name_addr = struct.unpack_from("<IIIIII", data, s + i * 0x38)
        x["sections"].append({"name": _cstr(data, va(name_addr)) or f"#{i}", "flags": flags, "vaddr": vaddr,
                              "vsize": vsize, "raw": raw, "rawsize": rawsize})

    x["libraries"] = []
    lib = va(u32(0x164))
    for i in range(u32(0x160)):
        if lib is None or lib + (i + 1) * 0x10 > len(data):
            break
        name = data[lib + i * 0x10:lib + i * 0x10 + 8].split(b"\0")[0].decode("latin-1")
        major, minor, build, flags = struct.unpack_from("<HHHH", data, lib + i * 0x10 + 8)
        x["libraries"].append((name, major, minor, build, flags))
    return x


def kernel_thunks(data, x):
    """[(endereço, ordinal)] da tabela de thunks do kernel: palavras 0x80000000 | ordinal até um zero.

    Na carga o kernel troca cada palavra pelo endereço da função exportada; no arquivo ficam os ordinais."""
    off = file_offset(x, x["kernel_thunk"])
    out = []
    while off is not None and off + 4 <= len(data):
        word = struct.unpack_from("<I", data, off)[0]
        if word == 0 or not word & 0x80000000:
            break
        out.append((x["kernel_thunk"] + 4 * len(out), word & 0x7FFFFFFF))
        off += 4
    return out


def file_offset(x, addr):
    """Posição no arquivo XBE de um endereço virtual (cabeçalho ou dados brutos de uma seção), ou None."""
    if x["base"] <= addr < x["base"] + x["size_headers"]:
        return addr - x["base"]
    for s in x["sections"]:
        if s["vaddr"] <= addr < s["vaddr"] + s["rawsize"]:
            return s["raw"] + addr - s["vaddr"]
    return None


# ---- PE ----
#
# O XBE é um PE reempacotado pelo imagebld do XDK: as seções continuam no mesmo endereço virtual, mas os cabeçalhos são
# outros e os endereços de seção não são múltiplos de página. Para o Ghidra (carregador PE, analisadores do MSVC,
# especificação de compilador "windows") escrevemos um PE32 "plano": a posição de cada seção no arquivo é o próprio
# RVA (va - base), SectionAlignment = FileAlignment = 0x20 (todos os va do XBE são múltiplos de 0x20), sem importações,
# relocações nem diretórios. O que se perde: o cabeçalho do XBE (que o console mapeia em 0x10000; aqui ficam os
# cabeçalhos do PE) e os nomes de seção com mais de 8 caracteres ("$$XTIMAGE" vira "$$XTIMAG").

PE_ALIGN = 0x20
PE_SUBSYSTEM_XBOX = 14
# Seções que o imagebld marca como executáveis mas são dados (o .rdata, o .data; DOLBY é o programa do DSP de áudio,
# não x86; as imagens do título e do salvamento).
PE_DATA_SECTIONS = {".rdata", ".data", ".data1", "DOLBY", "$$XTIMAGE", "$$XSIMAGE", "$$XTIMAG", "$$XSIMAG"}
SCN_CODE, SCN_IDATA, SCN_UDATA = 0x20, 0x40, 0x80
SCN_EXEC, SCN_READ, SCN_WRITE = 0x20000000, 0x40000000, 0x80000000


def _align(v, a):
    return (v + a - 1) // a * a


def pe_section_flags(s):
    """Características PE de uma seção do XBE (bit 1 gravável, bit 4 executável; ver PE_DATA_SECTIONS)."""
    code = bool(s["flags"] & 4) and s["name"] not in PE_DATA_SECTIONS
    c = SCN_READ | (SCN_CODE | SCN_EXEC if code else SCN_IDATA if s["rawsize"] else SCN_UDATA)
    if s["flags"] & 1:
        c |= SCN_WRITE
    return c


def xbe_to_pe(data):
    """Um PE32 equivalente ao XBE (bytes): base, seções, entrada; ver o comentário acima."""
    x = parse_xbe(data)
    base = x["base"]
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    secs = sorted(x["sections"], key=lambda s: s["vaddr"])
    if not secs:
        raise ValueError("XBE sem seções")
    for s in secs:
        if s["vaddr"] % PE_ALIGN or s["vaddr"] < base + 0x1000:
            raise ValueError(f"seção {s['name']} em {s['vaddr']:#x}: não cabe num PE com alinhamento {PE_ALIGN:#x}")
    for a, b in zip(secs, secs[1:]):
        if a["vaddr"] + a["vsize"] > b["vaddr"]:
            raise ValueError(f"seções {a['name']} e {b['name']} se sobrepõem")
    n = len(secs)
    lfanew = 0x40
    opt_size = 0xE0
    headers = lfanew + 4 + 20 + opt_size + 40 * n
    first_rva = secs[0]["vaddr"] - base
    if headers > first_rva:
        raise ValueError("os cabeçalhos do PE não cabem antes da primeira seção")
    size_image = _align(max(s["vaddr"] + s["vsize"] for s in secs) - base, PE_ALIGN)

    out = bytearray(first_rva)
    out[0:2] = b"MZ"
    struct.pack_into("<I", out, 0x3C, lfanew)
    out[lfanew:lfanew + 4] = b"PE\0\0"
    # Cabeçalho COFF: i386, sem relocações, executável, 32 bits.
    struct.pack_into("<HHIIIHH", out, lfanew + 4, 0x14C, n, u32(0x148) or x["timedate"], 0, 0, opt_size, 0x0103)

    flags = [pe_section_flags(s) for s in secs]
    code = [s for s, f in zip(secs, flags) if f & SCN_CODE]
    data_secs = [s for s, f in zip(secs, flags) if not f & SCN_CODE]
    o = lfanew + 24
    struct.pack_into("<HBB", out, o, 0x10B, 7, 0)
    struct.pack_into("<IIIIII", out, o + 4,
                     sum(_align(s["vsize"], PE_ALIGN) for s in code),
                     sum(_align(s["vsize"], PE_ALIGN) for s, f in zip(secs, flags) if f & SCN_IDATA),
                     sum(_align(s["vsize"], PE_ALIGN) for s, f in zip(secs, flags) if f & SCN_UDATA),
                     x["entry"] - base,
                     (code[0]["vaddr"] - base) if code else 0,
                     (data_secs[0]["vaddr"] - base) if data_secs else 0)
    struct.pack_into("<III", out, o + 28, base, PE_ALIGN, PE_ALIGN)
    struct.pack_into("<HHHHHH", out, o + 40, 4, 0, 0, 0, 4, 0)
    struct.pack_into("<IIII", out, o + 52, 0, size_image, first_rva, 0)
    stack = u32(0x130)
    struct.pack_into("<HH", out, o + 68, PE_SUBSYSTEM_XBOX, 0)
    struct.pack_into("<IIIIII", out, o + 72, stack, stack, u32(0x134), u32(0x138), 0, 16)
    # 16 diretórios de dados zerados (o resto do cabeçalho opcional já é zero).

    t = o + opt_size
    body = []
    for s, f in zip(secs, flags):
        rva = s["vaddr"] - base
        raw = min(s["rawsize"], s["vsize"])
        rawsize = min(_align(raw, PE_ALIGN), _align(s["vsize"], PE_ALIGN)) if raw else 0
        name = s["name"].encode("latin-1")[:8].ljust(8, b"\0")
        struct.pack_into("<8sIIIIIIHHI", out, t, name, s["vsize"], rva, rawsize, rva if rawsize else 0, 0, 0, 0, 0, f)
        t += 40
        chunk = data[s["raw"]:s["raw"] + raw]
        body.append((rva, chunk.ljust(rawsize, b"\0")))
    for rva, chunk in body:
        if len(out) < rva:
            out.extend(b"\0" * (rva - len(out)))
        out[rva:rva + len(chunk)] = chunk
    return bytes(out)


def parse_pe(data):
    """Lê de volta os cabeçalhos de um PE32 (para os testes e para conferir o que o `pe` escreveu)."""
    if data[:2] != b"MZ":
        raise ValueError("não é um PE (falta MZ)")
    lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[lfanew:lfanew + 4] != b"PE\0\0":
        raise ValueError("não é um PE (falta PE\\0\\0)")
    machine, n, timedate, _, _, opt_size, chars = struct.unpack_from("<HHIIIHH", data, lfanew + 4)
    o = lfanew + 24
    magic = struct.unpack_from("<H", data, o)[0]
    entry = struct.unpack_from("<I", data, o + 16)[0]
    image_base, sec_align, file_align = struct.unpack_from("<III", data, o + 28)
    size_image, size_headers = struct.unpack_from("<II", data, o + 56)
    subsystem = struct.unpack_from("<H", data, o + 68)[0]
    ndirs = struct.unpack_from("<I", data, o + 92)[0]
    dirs = [struct.unpack_from("<II", data, o + 96 + 8 * i) for i in range(ndirs)]
    secs = []
    t = o + opt_size
    for i in range(n):
        name, vsize, rva, rawsize, rawptr, _, _, _, _, f = struct.unpack_from("<8sIIIIIIHHI", data, t + 40 * i)
        secs.append({"name": name.rstrip(b"\0").decode("latin-1"), "vsize": vsize, "rva": rva, "rawsize": rawsize,
                     "rawptr": rawptr, "flags": f})
    return {"machine": machine, "timedate": timedate, "chars": chars, "magic": magic, "entry": entry,
            "image_base": image_base, "section_align": sec_align, "file_align": file_align, "size_image": size_image,
            "size_headers": size_headers, "subsystem": subsystem, "dirs": dirs, "sections": secs}


def pe_labels(data, x):
    """Linhas para o ApplySymbolTable.java (endereço, tipo, namespace, nome, comentário): a tabela de thunks do kernel
    (`xboxkrnl_<ordinal>` em cada entrada), as seções das bibliotecas do XDK, a entrada e o diretório de TLS."""
    libs = {name: f"{major}.{minor}.{build}" for name, major, minor, build, _ in x["libraries"]}
    lib_of = {"D3D": "D3D8", "D3DX": "D3DX8", "XGRPH": "XGRAPHC", "DSOUND": "DSOUND", "XPP": "XAPILIB",
              "XMV": "XMV", "DOLBY": "DSOUND"}
    rows = []
    thunks = kernel_thunks(data, x)
    if thunks:
        rows.append((x["kernel_thunk"], "D", "", "xboxkrnl_thunk_table",
                     f"XBE: tabela de thunks do kernel, {len(thunks)} entradas (ordinais; o kernel grava os endereços na carga)"))
        for addr, ordinal in thunks:
            rows.append((addr, "D", "", f"xboxkrnl_{ordinal}", ""))
    for s in x["sections"]:
        if s["name"] in (".text", ".rdata", ".data"):
            continue
        lib = lib_of.get(s["name"])
        what = f"biblioteca do XDK {lib} {libs[lib]}" if lib in libs else "seção do XDK"
        if s["name"] == "DOLBY":
            what = "programa do DSP de áudio (Dolby, não é x86)"
        rows.append((s["vaddr"], "D", "", "xdk_section_" + "".join(c if c.isalnum() else "_" for c in s["name"]),
                     f"XBE: seção {s['name']}: {what}"))
    rows.append((x["entry"], "C", "", "", "XBE: ponto de entrada (decodificado com a chave " + x["kind"] + ")"))
    tls = struct.unpack_from("<I", data, 0x12C)[0]
    if tls:
        rows.append((tls, "D", "", "xbe_tls_directory", "XBE: diretório de TLS (IMAGE_TLS_DIRECTORY32)"))
    return rows


def _date(t):
    return datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


def print_xbe(x, full=True):
    title_id = x.get("title_id")
    print(f"  título \"{x.get('title', '?')}\" id {title_id:#010x}" if title_id is not None else "  sem certificado")
    if title_id is not None:
        region = x["region"]
        names = [n for bit, n in REGIONS if region & bit] or ["?"]
        print(f"  região {region:#x} ({', '.join(names)})  versão {x['version']:#x}  disco {x['disk']}")
    print(f"  build {_date(x['timedate'])}  {x['kind']}  base {x['base']:#x}  imagem {x['size_image']:#x}"
          f"  entrada {x['entry']:#x}")
    if x["debug_path"]:
        print(f"  caminho do build: {x['debug_path']}")
    secs = x["sections"]
    code = sum(s["vsize"] for s in secs if s["flags"] & 4)
    print(f"  {len(secs)} seções, {code:,} bytes executáveis; {len(x['libraries'])} bibliotecas do XDK")
    if full:
        for s in secs:
            print(f"    {s['name']:<12} va {s['vaddr']:#010x} tam {s['vsize']:#9x} arquivo {s['raw']:#9x}"
                  f" flags {s['flags']:#x}")
        for name, major, minor, build, flags in x["libraries"]:
            print(f"    {name:<8} {major}.{minor}.{build} flags {flags:#x}")


# ---- comandos ----

def cmd_info(path):
    with open(path, "rb") as f:
        head = f.read(4)
    if head == b"XBEH":
        with open(path, "rb") as f:
            print_xbe(parse_xbe(f.read()))
        return
    img = Xiso(path)
    try:
        files = dirs = total = 0
        xbes = []
        for p, is_dir, start, size in img.walk():
            if is_dir:
                dirs += 1
            else:
                files += 1
                total += size
                if p.lower().endswith(".xbe"):
                    xbes.append((p, size))
        print(f"{path}: {img.kind}, partição em {img.base:#x}, raiz no setor {img.root_sector}")
        print(f"  {files} arquivos em {dirs} pastas, {total:,} bytes")
        for p, size in xbes:
            print(f"  executável {p} ({size:,} bytes)")
        for p, _ in xbes:
            print(f"{p}:")
            print_xbe(parse_xbe(img.read_file(p)), full=False)
    finally:
        img.close()


def cmd_list(path):
    img = Xiso(path)
    try:
        for p, is_dir, start, size in img.walk():
            print(f"{'d' if is_dir else '-'} {start:8d} {size:12,d}  {p}{'/' if is_dir else ''}")
    finally:
        img.close()


def cmd_extract(path, dest):
    img = Xiso(path)
    try:
        files, dirs, total, refused = extract(img, dest)
    finally:
        img.close()
    print(f"{files} arquivos e {dirs} pastas extraídos em {dest} ({total:,} bytes)"
          + (f"; {refused} recusados" if refused else ""))


def cmd_pe(path, dest):
    with open(path, "rb") as f:
        data = f.read()
    x = parse_xbe(data)
    pe = xbe_to_pe(data)
    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(pe)
    labels = os.path.splitext(dest)[0] + ".labels.tsv"
    rows = pe_labels(data, x)
    with open(labels, "w", encoding="utf-8", newline="\n") as f:
        for addr, kind, ns, name, comment in rows:
            f.write("\t".join((f"{addr:08x}", kind, ns, name, comment)) + "\n")
    p = parse_pe(pe)
    print(f"{dest}: PE32 de {len(pe):,} bytes, base {p['image_base']:#x}, entrada {p['image_base'] + p['entry']:#x},"
          f" {len(p['sections'])} seções")
    for s in p["sections"]:
        kind = "código" if s["flags"] & SCN_EXEC else "dados" + (" graváveis" if s["flags"] & SCN_WRITE else "")
        print(f"  {s['name']:<8} va {p['image_base'] + s['rva']:#010x} tam {s['vsize']:#9x} {kind}")
    print(f"{labels}: {len(rows)} rótulos ({len(kernel_thunks(data, x))} thunks do kernel)")


# ---- programas de vértice do NV2A ----
#
# Os render methods do EAGL de Xbox (data\render\eaglrm.o e bondrm.o do misc.viv; contêiner ELF "MIPS" como os do PS2)
# trazem o programa de vértice já montado pelo XGAssembleShader no símbolo <método>__EAGLMicroCodeVS: uma palavra
# 0x2078 | (instruções << 16) e as instruções do NV2A, de 4 palavras cada. Os campos são os que os emuladores
# documentam (Cxbx-Reloaded, xemu: (palavra, bit, largura)); o bastante para ler a fórmula de um método, não para
# traduzi-lo. Ex.: o fim padrão que o montador acrescenta, "MUL oPos.xyz, r12, c[-38] : RCC r1.x, r12.w" e
# "MAD oPos.xyz, r12, r1.x, c[-37]" (escala e deslocamento da janela de vista; r12 é o espelho de oPos).

VSH_MAC = ("NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DPH", "DP4", "DST", "MIN", "MAX", "SLT", "SGE", "ARL")
VSH_ILU = ("NOP", "MOV", "RCP", "RCC", "RSQ", "EXP", "LOG", "LIT")
VSH_OUT = {0: "oPos", 3: "oD0", 4: "oD1", 5: "oFog", 6: "oPts", 7: "oB0", 8: "oB1", 9: "oT0", 10: "oT1", 11: "oT2",
           12: "oT3"}
# (palavra, bit, largura) de cada campo
VSH_F = {"ilu": (1, 25, 3), "mac": (1, 21, 4), "const": (1, 13, 8), "v": (1, 9, 4),
         "a_neg": (1, 8, 1), "a_swz": (1, 0, 8), "a_r": (2, 28, 4), "a_mux": (2, 26, 2),
         "b_neg": (2, 25, 1), "b_swz": (2, 17, 8), "b_r": (2, 13, 4), "b_mux": (2, 11, 2),
         "c_neg": (2, 10, 1), "c_swz": (2, 2, 8), "c_r_hi": (2, 0, 2), "c_r_lo": (3, 30, 2), "c_mux": (3, 28, 2),
         "mac_mask": (3, 24, 4), "r": (3, 20, 4), "ilu_mask": (3, 16, 4), "o_mask": (3, 12, 4), "orb": (3, 11, 1),
         "o_addr": (3, 3, 8), "o_mux": (3, 2, 1), "a0x": (3, 1, 1), "final": (3, 0, 1)}


def _vf(w, name):
    word, pos, n = VSH_F[name]
    return (w[word] >> pos) & ((1 << n) - 1)


def _vsh_src(w, s):
    neg, swz, mux = _vf(w, s + "_neg"), _vf(w, s + "_swz"), _vf(w, s + "_mux")
    r = (_vf(w, "c_r_hi") << 2 | _vf(w, "c_r_lo")) if s == "c" else _vf(w, s + "_r")
    if mux == 1:
        reg = f"r{r}"
    elif mux == 2:
        reg = f"v{_vf(w, 'v')}"
    elif mux == 3:
        idx = _vf(w, "const") - 96          # c[-96..95]; os do usuário são c0..c95
        reg = f"c[a0.x{idx:+d}]" if _vf(w, "a0x") else f"c[{idx}]"
    else:
        reg = "?"
    comp = "".join("xyzw"[(swz >> sh) & 3] for sh in (6, 4, 2, 0))
    comp = "" if comp == "xyzw" else comp[0] if len(set(comp)) == 1 else comp
    return ("-" if neg else "") + reg + ("." + comp if comp else "")


def _vsh_mask(m):
    return "".join(c for c, bit in zip("xyzw", (8, 4, 2, 1)) if m & bit)


def vsh_disasm(blob):
    """As instruções de um programa de vértice do NV2A (bytes a partir do cabeçalho 0x2078) em texto."""
    head = struct.unpack_from("<I", blob, 0)[0]
    if head & 0xFFFF != 0x2078:
        raise ValueError(f"não é um programa de vértice do Xbox (cabeçalho {head:#010x})")
    out = []
    for i in range(head >> 16):
        w = struct.unpack_from("<4I", blob, 4 + 16 * i)
        mac, ilu = _vf(w, "mac"), _vf(w, "ilu")
        o_mask, o_mux = _vf(w, "o_mask"), _vf(w, "o_mux")
        o_addr = _vf(w, "o_addr")
        out_reg = (VSH_OUT.get(o_addr, f"o{o_addr}") if _vf(w, "orb") else f"c[{o_addr - 96}]") + "." + \
            _vsh_mask(o_mask)
        parts = []
        if mac:
            name = VSH_MAC[mac] if mac < len(VSH_MAC) else f"mac{mac}"
            srcs = {1: "a", 13: "a", 3: "ac", 4: "abc"}.get(mac, "ab")
            dst = [f"r{_vf(w, 'r')}.{_vsh_mask(_vf(w, 'mac_mask'))}"] if _vf(w, "mac_mask") else []
            if o_mask and o_mux == 0:
                dst.append(out_reg)
            if mac == 13:
                dst = ["a0.x"]
            parts.append(f"{name} {'+'.join(dst) or '-'}, " + ", ".join(_vsh_src(w, s) for s in srcs))
        if ilu:
            # com o MAC também ativo, o ILU só escreve em r1
            dst = [f"r{1 if mac else _vf(w, 'r')}.{_vsh_mask(_vf(w, 'ilu_mask'))}"] if _vf(w, "ilu_mask") else []
            if o_mask and o_mux == 1:
                dst.append(out_reg)
            parts.append(f"{VSH_ILU[ilu]} {'+'.join(dst) or '-'}, {_vsh_src(w, 'c')}")
        out.append(" : ".join(parts) or "NOP")
    return out


def elf_symbol(data, name):
    """(posição no arquivo, valor) do símbolo de um objeto ELF32 (o contêiner dos objetos do EAGL), ou None."""
    shoff = struct.unpack_from("<I", data, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    secs = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    for s in secs:
        if s[1] != 2:                       # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            nm, value, _, _, _, shndx = struct.unpack_from("<IIIBBH", data, s[4] + i * 16)
            if _cstr(data, strtab[4] + nm) == name and 0 < shndx < len(secs):
                return secs[shndx][4] + value, value
    return None


def cmd_vsh(path, what):
    with open(path, "rb") as f:
        data = f.read()
    if what.startswith("0x"):
        off = int(what, 16)
    else:
        name = what if what.endswith("__EAGLMicroCodeVS") else what + "__EAGLMicroCodeVS"
        hit = elf_symbol(data, name)
        if hit is None:
            raise SystemExit(f"{path}: sem o símbolo {name}")
        off = hit[0]
    for i, line in enumerate(vsh_disasm(data[off:])):
        print(f"  {i:3d}  {line}")


# ---- testes ----

def _entry(left, right, start, size, attr, name):
    raw = struct.pack("<HHIIBB", left, right, start, size, attr, len(name)) + name.encode("latin-1")
    return raw + b"\xff" * (-len(raw) % 4)


def _pad(b):
    return b + b"\xff" * (-len(b) % SECTOR)


def _build_xbe():
    base = 0x10000
    d = bytearray(0x1000)
    d[0:4] = b"XBEH"
    struct.pack_into("<IIIII", d, 0x104, base, 0x1000, 0x3000, 0x178, 1033000000)
    struct.pack_into("<III", d, 0x118, base + 0x200, 1, base + 0x300)
    struct.pack_into("<I", d, 0x128, (base + 0x1234) ^ 0xA8FC57AB)
    struct.pack_into("<I", d, 0x14C, base + 0x420)
    struct.pack_into("<I", d, 0x158, (base + 0x2000) ^ 0x5B6D40B6)
    struct.pack_into("<II", d, 0x160, 2, base + 0x480)
    struct.pack_into("<IIQ", d, 0x200, 0x1D0, 1033000000, 0)
    struct.pack_into("<I", d, 0x208, 0x45410042)
    title = "007: NightFire".encode("utf-16-le")
    d[0x20C:0x20C + len(title)] = title
    struct.pack_into("<I", d, 0x2A0, 4)
    struct.pack_into("<I", d, 0x2AC, 0x102)
    struct.pack_into("<IIIIII", d, 0x300, 6, base + 0x1000, 0x2000, 0x1000, 0x2000, base + 0x400)
    d[0x400:0x406] = b".text\0"
    path = b"d:\\nightfire\\release\\default.exe\0"
    d[0x420:0x420 + len(path)] = path
    d[0x480:0x488] = b"D3D8\0\0\0\0"
    struct.pack_into("<HHHH", d, 0x488, 1, 0, 5849, 0x4000)
    d[0x490:0x498] = b"XAPILIB\0"
    struct.pack_into("<HHHH", d, 0x498, 1, 0, 5849, 0x4000)
    return bytes(d)


def _build_image():
    xbe = _build_xbe()
    viv = bytes(range(256)) * 20  # 5120 bytes: atravessa três setores
    # setores: 32 volume, 33 raiz, 34 pasta driving, 35-36 default.xbe, 37-39 misc.viv
    sub = _pad(_entry(0, 0, 37, len(viv), 0x80, "misc.viv"))
    a = _entry(0, 0, 35, len(xbe), 0x80, "default.xbe")
    b = _entry(0, 0, 34, len(sub), 0x10, "driving")
    evil = _entry(0, 0, 37, 4, 0x80, "..")
    # árvore: a raiz "default.xbe" tem à direita "driving", que tem à esquerda ".." (recusado na extração)
    a = struct.pack("<HH", 0, len(a) // 4) + a[4:]
    b = struct.pack("<HH", (len(a) + len(b)) // 4, 0) + b[4:]
    root = _pad(a + b + evil)
    vol = bytearray(SECTOR)
    vol[:20] = MAGIC
    struct.pack_into("<II", vol, 20, 33, len(root))
    vol[0x7EC:0x7EC + 20] = MAGIC
    img = bytearray(32 * SECTOR) + vol + root + sub + _pad(xbe) + _pad(viv)
    return bytes(img), xbe, viv


def _build_xbe_sections():
    """Um XBE com cinco seções fora do alinhamento de página, a tabela de thunks no .rdata e .data com zeros no fim."""
    base = 0x10000
    names_at, secs_at, cert_at = 0x400, 0x200, 0x180
    #       nome         flags  va             vsize  dados
    plan = [(".text", 0x16, base + 0x1000, 0x123, bytes([0x90] * 0x122) + b"\xc3"),
            ("D3D", 0x07, base + 0x1140, 0x80, bytes(range(0x60))),
            (".rdata", 0x06, base + 0x11e0, 0x40, struct.pack("<III", 0x80000001, 0x800000A1, 0) + b"abc\0"),
            (".data", 0x07, base + 0x1220, 0x400, b"\x11" * 0x30),
            ("$$XTIMAGE", 0x38, base + 0x1620, 0x20, b"\x22" * 0x20)]
    d = bytearray(0x800)
    d[0:4] = b"XBEH"
    struct.pack_into("<IIIII", d, 0x104, base, 0x600, 0x1640, 0x178, 1033000000)
    struct.pack_into("<III", d, 0x118, base + cert_at, len(plan), base + secs_at)
    struct.pack_into("<I", d, 0x128, (base + 0x1010) ^ 0xA8FC57AB)
    struct.pack_into("<IIII", d, 0x130, 0x10000, 0x100000, 0x1000, 0)
    struct.pack_into("<I", d, 0x148, 1034000000)
    struct.pack_into("<I", d, 0x158, (base + 0x11e0) ^ 0x5B6D40B6)
    struct.pack_into("<II", d, 0x160, 1, base + 0x500)
    d[0x500:0x508] = b"D3D8\0\0\0\0"
    struct.pack_into("<HHHH", d, 0x508, 1, 0, 4831, 0x4001)
    for i, (name, flags, va, vsize, raw) in enumerate(plan):
        name_at = names_at + 0x10 * i
        d[name_at:name_at + len(name)] = name.encode()
        struct.pack_into("<IIIIII", d, secs_at + 0x38 * i, flags, va, vsize, len(d), len(raw), base + name_at)
        d += raw + b"\0" * (-len(raw) % 0x10)
    return bytes(d), plan


def _test_pe():
    xbe, plan = _build_xbe_sections()
    x = parse_xbe(xbe)
    assert [s["name"] for s in x["sections"]] == [p[0] for p in plan]
    assert kernel_thunks(xbe, x) == [(0x111E0, 1), (0x111E4, 0xA1)]
    pe = xbe_to_pe(xbe)
    p = parse_pe(pe)
    assert p["machine"] == 0x14C and p["magic"] == 0x10B and p["chars"] & 0x102 == 0x102
    assert p["image_base"] == 0x10000 and p["entry"] == 0x1010 and p["timedate"] == 1034000000
    assert p["section_align"] == p["file_align"] == 0x20 and p["size_headers"] == 0x1000
    assert p["subsystem"] == PE_SUBSYSTEM_XBOX and len(p["dirs"]) == 16 and all(d == (0, 0) for d in p["dirs"])
    assert p["size_image"] == 0x1640, hex(p["size_image"])
    assert [s["name"] for s in p["sections"]] == [".text", "D3D", ".rdata", ".data", "$$XTIMAG"]
    flags = [s["flags"] for s in p["sections"]]
    assert flags[0] == SCN_CODE | SCN_EXEC | SCN_READ                  # .text: executável, não gravável
    assert flags[1] == SCN_CODE | SCN_EXEC | SCN_READ | SCN_WRITE      # D3D: código e dados da biblioteca
    assert flags[2] == SCN_IDATA | SCN_READ                            # .rdata: executável no XBE, dados aqui
    assert flags[3] == SCN_IDATA | SCN_READ | SCN_WRITE
    assert flags[4] == SCN_IDATA | SCN_READ
    for (name, _, va, vsize, raw), s in zip(plan, p["sections"]):
        rva = va - 0x10000
        assert s["rva"] == rva and s["vsize"] == vsize and s["rawptr"] == rva, (name, s)
        assert s["rawsize"] == min(_align(len(raw), PE_ALIGN), _align(vsize, PE_ALIGN)) and s["rawsize"] % PE_ALIGN == 0
        assert pe[rva:rva + len(raw)] == raw, name
        assert pe[rva + len(raw):rva + s["rawsize"]] == b"\0" * (s["rawsize"] - len(raw))
    assert len(pe) == p["sections"][-1]["rva"] + p["sections"][-1]["rawsize"]
    labels = pe_labels(xbe, x)
    names = {(a, n) for a, k, _, n, _ in labels if k == "D"}
    assert {(0x111E0, "xboxkrnl_thunk_table"), (0x111E0, "xboxkrnl_1"), (0x111E4, "xboxkrnl_161"),
            (0x11140, "xdk_section_D3D")} <= names, names
    assert any(k == "C" and a == 0x11010 for a, k, _, _, _ in labels)
    assert any("D3D8 1.0.4831" in c for a, _, _, n, c in labels if n == "xdk_section_D3D")
    bad = bytearray(xbe)
    struct.pack_into("<I", bad, 0x200 + 0x38 + 4, 0x10000 + 0x1110)  # D3D por cima do .text
    try:
        xbe_to_pe(bytes(bad))
        raise AssertionError("aceitou seções sobrepostas")
    except ValueError:
        pass
    struct.pack_into("<I", bad, 0x200 + 0x38 + 4, 0x10000 + 0x1150)  # fora do alinhamento de 0x20
    try:
        xbe_to_pe(bytes(bad))
        raise AssertionError("aceitou uma seção fora do alinhamento")
    except ValueError:
        pass


def _vsh_pack(**fields):
    w = [0, 0, 0, 0]
    for name, v in fields.items():
        word, pos, n = VSH_F[name]
        assert 0 <= v < (1 << n), (name, v)
        w[word] |= v << pos
    return struct.pack("<4I", *w)


def _build_elf_obj(symbol, value, data_bytes):
    """Um objeto ELF32 mínimo: .data, .symtab com um símbolo em .data, .strtab, .shstrtab."""
    shstr = b"\0.data\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0" + symbol.encode() + b"\0"
    symtab = b"\0" * 16 + struct.pack("<IIIBBH", 1, value, 4, 0x11, 0, 1)
    body = bytearray(0x34)
    offs = {}
    for name, blob in (("data", data_bytes), ("symtab", symtab), ("strtab", strtab), ("shstr", shstr)):
        offs[name] = len(body)
        body += blob + b"\0" * (-len(blob) % 4)
    shoff = len(body)
    body[0:4] = b"\x7fELF"
    struct.pack_into("<I", body, 0x20, shoff)
    struct.pack_into("<HHH", body, 0x2E, 40, 5, 4)
    sh = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          (1, 1, 3, 0, offs["data"], len(data_bytes), 0, 0, 16, 0),
          (7, 2, 0, 0, offs["symtab"], len(symtab), 3, 1, 4, 16),
          (15, 3, 0, 0, offs["strtab"], len(strtab), 0, 0, 1, 0),
          (23, 3, 0, 0, offs["shstr"], len(shstr), 0, 0, 1, 0)]
    for s in sh:
        body += struct.pack("<10I", *s)
    return bytes(body), offs["data"]


def _test_vsh():
    ident = 0x1B                                     # xyzw
    prog = [
        _vsh_pack(mac=7, a_mux=2, v=0, a_swz=ident, b_mux=3, const=0, b_swz=ident, o_mask=8, orb=1, o_addr=0),
        _vsh_pack(mac=2, ilu=3, a_mux=1, a_r=12, a_swz=ident, b_mux=3, const=58, b_swz=ident,
                  c_mux=1, c_r_hi=3, c_r_lo=0, c_swz=0xFF, o_mask=0xE, orb=1, o_addr=0, ilu_mask=8),
        _vsh_pack(mac=4, a_mux=1, a_r=11, a_swz=ident, b_mux=2, v=1, b_swz=ident, c_mux=1, c_swz=ident,
                  o_mask=0xF, orb=1, o_addr=3, final=1),
        _vsh_pack(mac=13, a_mux=3, const=15, a_swz=0x00),
        _vsh_pack(mac=3, a_mux=2, v=2, a_neg=1, a_swz=0x55, c_mux=3, const=91, a0x=1, c_swz=ident,
                  mac_mask=0xE, r=3),
        _vsh_pack(ilu=4, c_mux=1, c_r_hi=0, c_r_lo=3, c_swz=0xFF, ilu_mask=1, r=1),
    ]
    blob = struct.pack("<I", 0x2078 | len(prog) << 16) + b"".join(prog)
    got = vsh_disasm(blob)
    want = ["DP4 oPos.x, v0, c[-96]",
            "MUL oPos.xyz, r12, c[-38] : RCC r1.x, r12.w",
            "MAD oD0.xyzw, r11, v1, r0",
            "ARL a0.x, c[-81].x",
            "ADD r3.xyz, -v2.y, c[a0.x-5]",
            "RSQ r1.w, r3.w"]
    assert got == want, got
    try:
        vsh_disasm(struct.pack("<I", 0x12345678))
        raise AssertionError("aceitou um cabeçalho errado")
    except ValueError:
        pass
    obj, data_off = _build_elf_obj("LitTest0__EAGLMicroCodeVS", 0x10, b"\0" * 0x10 + blob)
    assert elf_symbol(obj, "LitTest0__EAGLMicroCodeVS") == (data_off + 0x10, 0x10)
    assert elf_symbol(obj, "Outro") is None
    off, _ = elf_symbol(obj, "LitTest0__EAGLMicroCodeVS")
    assert vsh_disasm(obj[off:]) == want


def cmd_test():
    _test_pe()
    _test_vsh()
    data, xbe, viv = _build_image()
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "teste.xiso")
        with open(path, "wb") as f:
            f.write(data)
        img = Xiso(path)
        entries = list(img.walk())
        names = [(p, d) for p, d, _, _ in entries]
        assert names == [("..", False), ("default.xbe", False), ("driving", True), ("driving/misc.viv", False)], names
        assert img.base == 0 and img.kind == "xiso"
        assert img.read_file("DRIVING/MISC.VIV") == viv
        assert img.read_file("default.xbe") == xbe
        files, dirs, total, refused = extract(img, os.path.join(tmp, "out"))
        img.close()
        assert (files, dirs, refused) == (2, 1, 1), (files, dirs, refused)
        with open(os.path.join(tmp, "out", "driving", "misc.viv"), "rb") as f:
            assert f.read() == viv
        bad = os.path.join(tmp, "ruim.bin")
        with open(bad, "wb") as f:
            f.write(b"\0" * (40 * SECTOR))
        try:
            Xiso(bad)
            raise AssertionError("aceitou uma imagem sem volume")
        except ValueError:
            pass
    x = parse_xbe(xbe)
    assert x["kind"] == "retail" and x["entry"] == 0x11234 and x["kernel_thunk"] == 0x12000
    assert x["title"] == "007: NightFire" and x["title_id"] == 0x45410042
    assert x["region"] == 4 and x["version"] == 0x102
    assert x["debug_path"] == "d:\\nightfire\\release\\default.exe"
    assert [s["name"] for s in x["sections"]] == [".text"] and x["sections"][0]["vsize"] == 0x2000
    assert x["libraries"] == [("D3D8", 1, 0, 5849, 0x4000), ("XAPILIB", 1, 0, 5849, 0x4000)]
    print("ok")


def main(argv):
    if len(argv) >= 2 and argv[1] == "test":
        cmd_test()
    elif len(argv) == 3 and argv[1] == "info":
        cmd_info(argv[2])
    elif len(argv) == 3 and argv[1] == "list":
        cmd_list(argv[2])
    elif len(argv) == 4 and argv[1] == "extract":
        cmd_extract(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == "xbe":
        with open(argv[2], "rb") as f:
            print_xbe(parse_xbe(f.read()))
    elif len(argv) == 4 and argv[1] == "pe":
        cmd_pe(argv[2], argv[3])
    elif len(argv) == 4 and argv[1] == "vsh":
        cmd_vsh(argv[2], argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
