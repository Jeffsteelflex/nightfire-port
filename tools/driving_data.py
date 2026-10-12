"""Os dados das missões de carro (a pasta DRIVING/ do disco): arquivos BIGF da EA e o que vai dentro.

Formatos (docs/driving.md, "Os dados"):
- .VIV, .MUS, .SPE: arquivos "BIGF" da EA. Cabeçalho de 16 bytes: "BIGF", u32 LE o tamanho do arquivo (o do MISC.VIV
  não bate: 0x0b455c29 lido como big-endian fica 2407 bytes abaixo do tamanho; o carregador não o usa), u32 BE o número
  de entradas, u32 BE o tamanho do cabeçalho; depois as entradas {u32 BE posição, u32 BE tamanho, nome ASCII com zero}.
  As posições são alinhadas (0x800 nos .VIV). Nos .MUS e .SPE o cabeçalho termina com "L225".
- Compressão: quase tudo nos .VIV de missão é RefPack (QFS) da EA: "\\x10\\xfb" + o tamanho descomprimido em 24 bits
  big-endian; o MISC.VIV e os .MUS/.SPE não são comprimidos.
- Tipos (pelo conteúdo, depois de descomprimir): objetos ELF do EAGL (.o, os pares .dat + .rel, que são um ELF partido
  em dados e cabeçalhos; render methods com microcódigo da VU1, esqueletos, bancos de animação, modelos de personagem),
  CARP ("PRAC": carros, armas, pistas), SHPS (texturas "shape" da EA, .ssh), SCHl (fluxos de áudio da EA: música e falas,
  .asf), BNKl (bancos de som da EA, .bnk), "LAGG" (galerias do HUD e das telas, .gal), LOCH (textos, .loc), FNTS
  (fontes, .sfn), MPCh (filmes MPC: vídeo MPEG-2 + áudio da EA, .mpc) e texto (.atr, .tun, .def, .ini, .h, .sub, .dir).

Uso (a raiz dos dados é --root, padrão: a raiz do repositório; os arquivos ficam em <raiz>/DRIVING):
    python tools/driving_data.py stats                       todos os arquivos: entradas, tipos, bytes
    python tools/driving_data.py list <ARQ.VIV>              as entradas: posição, tamanho, comprimido, tipo, nome
    python tools/driving_data.py extract <ARQ.VIV> <pasta> [filtro...]   grava as entradas (descomprimidas) em out/
    python tools/driving_data.py ssh <ARQ.VIV>:<entrada>     as imagens de um .ssh (nome, formato, largura, altura)
    python tools/driving_data.py carp <ARQ.VIV>:<entrada>    o diretório de um CARP (.crp) por tipo de bloco
    python tools/driving_data.py asf <ARQ>:<entrada>         o cabeçalho PT de um fluxo SCHl (taxa, canais, codec)
    python tools/driving_data.py eagl <ARQ.VIV>:<entrada>    seções e símbolos de um objeto ELF do EAGL (.o, .dat/.rel)
    python tools/driving_data.py test                        confere o leitor e o RefPack com dados montados à mão

<entrada> é um pedaço do nome (a primeira que casar). O que sai daqui é dado do jogo: grave em out/.
"""
import argparse
import collections
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---------------------------------------------------------------- BIGF

def parse_bigf(head):
    """Entradas [(posição, tamanho, nome)] a partir do começo do arquivo (pelo menos o cabeçalho inteiro)."""
    if head[:4] not in (b"BIGF", b"BIG4"):
        raise ValueError("não é BIGF")
    count = struct.unpack_from(">I", head, 8)[0]
    p = 16
    out = []
    for _ in range(count):
        off, size = struct.unpack_from(">II", head, p)
        p += 8
        end = head.index(b"\0", p)
        out.append((off, size, head[p:end].decode("latin1")))
        p = end + 1
    return out


class Bigf:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        h = self.f.read(16)
        self.declared = struct.unpack_from("<I", h, 4)[0]
        self.header_size = struct.unpack_from(">I", h, 12)[0]
        self.f.seek(0)
        self.entries = parse_bigf(self.f.read(self.header_size))
        self.size = os.path.getsize(path)

    def raw(self, entry):
        off, size, name = entry
        self.f.seek(off)
        return self.f.read(size)

    def data(self, entry):
        b = self.raw(entry)
        return refpack(b) if is_refpack(b) else b

    def find(self, key):
        key = key.lower().replace("/", "\\")
        for e in self.entries:
            if key in e[2].lower():
                return e
        raise KeyError("%s: nenhuma entrada com '%s'" % (os.path.basename(self.path), key))


# ---------------------------------------------------------------- RefPack (QFS)

def is_refpack(b):
    return len(b) >= 5 and b[1] == 0xFB and (b[0] & 0x3E) == 0x10


def refpack(src):
    """Descompressor do RefPack da EA (o 'unpackz'/REF_decode da biblioteca REAL)."""
    flags = src[0]
    if src[1] != 0xFB:
        raise ValueError("não é RefPack")
    big = flags & 0x80
    p = 2
    nsize = 4 if big else 3
    size = int.from_bytes(src[p:p + nsize], "big")
    p += nsize
    if flags & 0x01:  # tamanho comprimido também
        p += nsize
    out = bytearray()
    n = len(src)
    while p < n:
        b0 = src[p]
        if b0 < 0x80:
            b1 = src[p + 1]
            lit, cnt, off = b0 & 3, ((b0 >> 2) & 7) + 3, ((b0 & 0x60) << 3) + b1 + 1
            p += 2
        elif b0 < 0xC0:
            b1, b2 = src[p + 1], src[p + 2]
            lit, cnt, off = (b1 >> 6) & 3, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
            p += 3
        elif b0 < 0xE0:
            b1, b2, b3 = src[p + 1], src[p + 2], src[p + 3]
            lit, cnt = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5
            off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
            p += 4
        elif b0 < 0xFC:
            lit, cnt, off = ((b0 & 0x1F) << 2) + 4, 0, 0
            p += 1
        else:  # fim, com até 3 literais
            lit = b0 & 3
            p += 1
            out += src[p:p + lit]
            break
        out += src[p:p + lit]
        p += lit
        if cnt:
            if off > len(out):
                raise ValueError("RefPack: cópia antes do começo")
            start = len(out) - off
            for i in range(cnt):
                out.append(out[start + i])
    if len(out) != size:
        raise ValueError("RefPack: %d bytes, o cabeçalho diz %d" % (len(out), size))
    return bytes(out)


# ---------------------------------------------------------------- tipos

TEXT_EXT = {".atr", ".tun", ".def", ".ini", ".h", ".sub", ".dir", ".txt"}


def kind_of(name, b):
    m = b[:4]
    if m == b"\x7fELF":
        return "ELF do EAGL"
    if name.lower().endswith(".rel") and b[:2] == b"\0.":
        return "ELF do EAGL (cabeçalhos do .dat)"
    table = {b"PRAC": "CARP", b"SHPS": "SHPS (textura)", b"SCHl": "SCHl (áudio)", b"BNKl": "BNKl (banco de som)",
             b"LAGG": "GGAL (galeria)", b"LOCH": "LOCH (textos)", b"FNTS": "FNTS (fonte)", b"MPCh": "MPCh (filme)"}
    if m in table:
        return table[m]
    ext = os.path.splitext(name)[1].lower()
    if ext in TEXT_EXT or all(32 <= c < 127 or c in (9, 10, 13) for c in b[:64]):
        return "texto"
    if ext == ".bin":
        return "binário (%s)" % ext
    return "outro"


def archives(root):
    d = os.path.join(root, "DRIVING")
    return [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.upper().endswith((".VIV", ".MUS", ".SPE"))]


def open_entry(root, spec):
    if ":" not in spec:
        raise SystemExit("use <ARQUIVO>:<entrada>, por exemplo MIS01.VIV:vanquish.crp")
    arc, key = spec.split(":", 1)
    big = Bigf(os.path.join(root, "DRIVING", arc))
    e = big.find(key)
    return big, e, big.data(e)


# ---------------------------------------------------------------- formatos internos

SSH_TYPES = {0x01: "4 bits + paleta", 0x02: "8 bits + paleta", 0x04: "RGB 24", 0x05: "RGBA 32", 0x21: "paleta 32",
             0x22: "paleta 24", 0x23: "paleta 16", 0x69: "metadados", 0x6f: "comentário", 0x70: "nome longo",
             0x7c: "pixel region"}


def ssh_images(b):
    """[(nome, [(tipo, largura, altura)])] de um SHPS: diretório de {nome de 4 letras, posição}; cada imagem é uma
    cadeia de blocos {u8 tipo, u24 até o próximo (0 = último), u16 largura, u16 altura, ...}."""
    if b[:4] != b"SHPS":
        raise ValueError("não é SHPS")
    count = struct.unpack_from("<I", b, 8)[0]
    out = []
    for i in range(count):
        name = b[16 + i * 8:20 + i * 8].decode("latin1")
        p = struct.unpack_from("<I", b, 20 + i * 8)[0]
        blocks = []
        for _ in range(16):
            t = b[p]
            nxt = int.from_bytes(b[p + 1:p + 4], "little")
            w, h = struct.unpack_from("<HH", b, p + 4)
            blocks.append((t, w, h))
            if nxt == 0:
                break
            p += nxt
        out.append((name, blocks))
    return out


def carp_directory(b):
    """Um CARP é um UGroup serializado (src/driving/carp.h): grupos {FourCC (gravado ao contrário), u32 bandeiras, u32
    contagem, u32 posição} cujo vetor (em grupo + posição x 16 com o bit 1, senão absoluto) tem `bandeiras >> 5`
    subgrupos e depois `contagem` dados {etiqueta, bandeiras (tamanho = >> 8), contagem, posição}. Devolve os grupos
    [(caminho, etiqueta, dados)] e todos os dados [(caminho, etiqueta, bandeiras, contagem, posição dos dados)]."""
    magic = b[0:4]
    if magic != b"PRAC":
        raise ValueError("não é CARP")
    groups, data = [], []

    def walk(pos, path, depth):
        tag, flags, count, off = struct.unpack_from("<4sIII", b, pos)
        arr = pos + off * 16 if flags & 2 else off
        ngroups = flags >> 5
        name = tag[::-1]
        here = path + [name.decode("latin1")]
        groups.append(("/".join(here), name, count))
        for i in range(count):
            e = arr + (ngroups + i) * 16
            t, fl, k, o = struct.unpack_from("<4sIII", b, e)
            data.append(("/".join(here), t[::-1], fl, k, e + o if fl & 2 else o))
        if depth < 16:
            for i in range(ngroups):
                walk(arr + i * 16, here, depth + 1)

    walk(0, [], 0)
    return groups, data


def pt_header(b):
    """Os campos do cabeçalho PT de um SCHl/BNKl: {etiqueta: valor} (0x80 versão, 0x82 canais, 0x84 taxa, 0x85
    amostras, 0xa0 codec; 0xfd abre o sub-cabeçalho, 0xff fecha)."""
    p = b.find(b"PT\0\0")
    if p < 0:
        p = b.find(b"PT")
    if p < 0:
        raise ValueError("sem cabeçalho PT")
    platform = b[p + 2]
    p += 4
    out = collections.OrderedDict()
    while p < len(b):
        tag = b[p]
        p += 1
        if tag == 0xFF:
            break
        if tag in (0xFC, 0xFD, 0xFE):
            continue
        n = b[p]
        p += 1
        out[tag] = int.from_bytes(b[p:p + n], "big")
        p += n
    return platform, out


EA_CODECS = {0x00: "PCM?", 0x04: "MT10?", 0x05: "VAG (PS-ADPCM)", 0x07: "PCM 16 BE", 0x08: "PCM 16 LE",
             0x09: "PCM 8", 0x0A: "EA-XA (ADPCM da EA)", 0x0F: "MPEG camada 2", 0x10: "MPEG camada 3"}


def elf_summary(b):
    """Seções e símbolos de um objeto ELF relocável do EAGL."""
    shoff = struct.unpack_from("<I", b, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2E)
    if shoff == 0 or shoff + shnum * shentsize > len(b):
        return None, []
    secs = [struct.unpack_from("<10I", b, shoff + i * shentsize) for i in range(shnum)]
    strsec = secs[shstrndx]

    def cstr(base, i):
        e = b.index(b"\0", base + i)
        return b[base + i:e].decode("latin1")

    sections = [(cstr(strsec[4], s[0]), s[5]) for s in secs]
    syms = []
    for s in secs:
        if s[1] != 2:
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            nm, value, size, info, _o, shndx = struct.unpack_from("<IIIBBH", b, s[4] + i * 16)
            if nm:
                syms.append((value, size, cstr(strtab[4], nm)))
    return sections, syms


# ---------------------------------------------------------------- comandos

def cmd_stats(args):
    kinds = collections.Counter()
    kbytes = collections.Counter()
    exts = collections.Counter()
    total_packed = total_raw = 0
    print("%-14s %8s %7s %12s %12s  %s" % ("arquivo", "entradas", "RefPack", "bytes", "descomprim.", "tipos"))
    for path in archives(args.root):
        big = Bigf(path)
        local = collections.Counter()
        packed = 0
        raw_total = 0
        for e in big.entries:
            b = big.raw(e)
            if is_refpack(b):
                packed += 1
                d = refpack(b)
            else:
                d = b
            k = kind_of(e[2], d)
            local[k] += 1
            kinds[k] += 1
            kbytes[k] += len(d)
            exts[os.path.splitext(e[2])[1].lower()] += 1
            raw_total += len(d)
        total_packed += big.size
        total_raw += raw_total
        note = "" if big.declared == big.size else " (tamanho no cabeçalho 0x%x, arquivo 0x%x)" % (big.declared,
                                                                                                big.size)
        print("%-14s %8d %7d %12d %12d  %s%s" % (os.path.basename(path), len(big.entries), packed, big.size,
                                                raw_total, ", ".join("%s %d" % kv for kv in local.most_common()),
                                                note))
    print("total: %d bytes no disco, %d descomprimidos" % (total_packed, total_raw))
    for k, c in kinds.most_common():
        print("  %-36s %5d entradas %12d bytes" % (k, c, kbytes[k]))
    print("extensões:", ", ".join("%s %d" % kv for kv in exts.most_common()))


def cmd_list(args):
    big = Bigf(os.path.join(args.root, "DRIVING", args.archive))
    print("%s: %d entradas, cabeçalho 0x%x, tamanho declarado 0x%x, arquivo 0x%x" % (
        args.archive, len(big.entries), big.header_size, big.declared, big.size))
    for e in big.entries:
        b = big.raw(e)
        packed = is_refpack(b)
        d = refpack(b) if packed else b
        print("%08x %9d %9s %-32s %s" % (e[0], e[1], len(d) if packed else "-", kind_of(e[2], d), e[2]))


def cmd_extract(args):
    big = Bigf(os.path.join(args.root, "DRIVING", args.archive))
    n = 0
    for e in big.entries:
        if args.filter and not any(f.lower() in e[2].lower() for f in args.filter):
            continue
        dst = os.path.join(args.dir, e[2].replace("\\", "/"))
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        with open(dst, "wb") as f:
            f.write(big.data(e))
        n += 1
    print("%d entradas em %s" % (n, args.dir))


def cmd_ssh(args):
    big, e, b = open_entry(args.root, args.entry)
    imgs = ssh_images(b)
    print("%s: %d imagens, versão %s" % (e[2], len(imgs), b[12:16].decode("latin1")))
    for name, blocks in imgs:
        print("  %-4s %s" % (name, "; ".join("%s %dx%d" % (SSH_TYPES.get(t, "0x%02x" % t), w, h)
                                            if t not in (0x69, 0x6f, 0x70) else SSH_TYPES.get(t, "0x%02x" % t)
                                            for t, w, h in blocks)))


def cmd_carp(args):
    big, e, b = open_entry(args.root, args.entry)
    groups, ents = carp_directory(b)
    print("%s: %d bytes, %d grupos, %d dados" % (e[2], len(b), len(groups), len(ents)))
    # Por caminho (as pistas têm centenas de grupos 'Arti', um por objeto): grupos, dados e os tipos de bloco.
    paths = collections.OrderedDict()
    for path, name, count in groups:
        paths.setdefault(path, [0, collections.Counter()])[0] += 1
    for gpath, tag, fl, k, off in ents:
        if fl & 1:
            key = tag[:2].decode("latin1") + "##"  # família numerada: 'sr' + u16 (etiqueta curta, bit 0)
        elif tag.isalpha():
            key = tag.decode("latin1")             # bloco único: 'Rule', 'Trgr', 'ELFd'...
        else:
            key = "(outros)"
        paths[gpath][1][key] += 1
    for path, (n, c) in paths.items():
        print("  %-16s %4d grupo(s), %6d dados: %s" % (path, n, sum(c.values()),
                                                     ", ".join("%s %d" % kv for kv in c.most_common())))


def cmd_asf(args):
    big, e, b = open_entry(args.root, args.entry)
    plat, f = pt_header(b)
    rate = f.get(0x84)
    ch = f.get(0x82, 1)
    n = f.get(0x85)
    codec = f.get(0xA0)
    blocks = collections.Counter()
    p = 0
    while p + 8 <= len(b):
        tag = b[p:p + 4]
        ln = struct.unpack_from("<I", b, p + 4)[0]
        blocks[tag.decode("latin1", "replace")] += 1
        if ln < 8:
            break
        p += ln
    dur = (n / rate) if n and rate else 0
    print("%s: plataforma %d, %s Hz, %s canais, %s amostras (%.1f s), codec 0x%02x %s" % (
        e[2], plat, rate, ch, n, dur, codec or 0, EA_CODECS.get(codec, "?")))
    print("  campos PT:", ", ".join("0x%02x=%d" % kv for kv in f.items()))
    print("  blocos:", ", ".join("%s %d" % kv for kv in blocks.items()))


def cmd_eagl(args):
    big, e, b = open_entry(args.root, args.entry)
    if e[2].lower().endswith(".dat"):
        rel = big.find(e[2][:-4] + ".rel")
        r = big.data(rel)
        print("%s: .dat %d bytes (cabeçalho ELF e .data), .rel %d bytes (as tabelas de seções e símbolos)" %
              (e[2], len(b), len(r)))
        b = b + r
    sections, syms = elf_summary(b)
    if sections is None:
        print("%s: ELF de %d bytes, cabeçalhos fora do bloco" % (e[2], len(b)))
        return
    print("%s: seções %s" % (e[2], ", ".join("%s %d" % s for s in sections if s[0])))
    for value, size, name in syms[:args.max]:
        print("  %08x %6d %s" % (value, size, name))
    if len(syms) > args.max:
        print("  ... %d símbolos" % len(syms))


def cmd_test(args):
    # RefPack montado à mão: 4 literais "ABCD", depois cópia de 3 a 4 bytes para trás, fim.
    comp = bytes([0x10, 0xFB, 0x00, 0x00, 0x07, 0xE0]) + b"ABCD" + bytes([0x00, 0x03, 0xFC])
    assert is_refpack(comp)
    assert refpack(comp) == b"ABCDABC", refpack(comp)
    # 1 literal "x" e cópia de 3 a 1 byte para trás (sobreposta: repete o "x"), fim
    comp3 = bytes([0x10, 0xFB, 0x00, 0x00, 0x04, 0x01, 0x00]) + b"x" + bytes([0xFC])
    assert refpack(comp3) == b"xxxx", refpack(comp3)
    # BIGF montado à mão: duas entradas
    names = [b"a\\one.txt", b"two.bin"]
    hdr_size = 16 + sum(8 + len(n) + 1 for n in names)
    body = [b"hello", comp]
    offs = []
    pos = (hdr_size + 15) & ~15
    for d in body:
        offs.append(pos)
        pos += len(d)
    head = b"BIGF" + struct.pack("<I", pos) + struct.pack(">II", len(names), hdr_size)
    for n, o, d in zip(names, offs, body):
        head += struct.pack(">II", o, len(d)) + n + b"\0"
    ents = parse_bigf(head)
    assert ents == [(offs[0], 5, "a\\one.txt"), (offs[1], len(comp), "two.bin")], ents
    assert kind_of("x.atr", b"[sentry]\r\nA=1") == "texto"
    assert kind_of("x.o", b"\x7fELF....") == "ELF do EAGL"
    plat, f = pt_header(b"SCHl\x2c\0\0\0PT\x05\0\x06\x01\x65\xfd\x80\x01\x02\x82\x01\x02\x84\x03\x00\xbb\x80"
                        b"\xa0\x01\x0a\xff")
    assert plat == 5 and f[0x82] == 2 and f[0x84] == 48000 and f[0xA0] == 0x0A, f
    # UGroup: a raiz com um subgrupo ('Arti', dois dados) e um dado ('ELFd')
    carp = bytearray(96)
    carp[0:16] = b"PRAC" + struct.pack("<III", 1 << 5 | 2, 1, 1)
    carp[16:32] = b"itrA" + struct.pack("<III", 2, 2, 2)
    carp[32:48] = b"dFLE" + struct.pack("<III", 0x40 << 8 | 2, 0, 0x30)
    carp[48:64] = struct.pack("<IIII", 0x736E0005, 0x10 << 8 | 9, 0, 0x99)
    carp[64:80] = struct.pack("<IIII", 0x73720001, 8 << 8 | 3, 0, 0x10)
    groups, ents = carp_directory(bytes(carp))
    assert [g[0] for g in groups] == ["CARP", "CARP/Arti"], groups
    assert ents[0] == ("CARP", b"ELFd", 0x40 << 8 | 2, 0, 32 + 0x30), ents
    assert ents[1][0] == "CARP/Arti" and ents[1][4] == 0x99 and ents[2][4] == 64 + 0x10, ents
    print("ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("stats")
    p = sub.add_parser("list")
    p.add_argument("archive")
    p = sub.add_parser("extract")
    p.add_argument("archive")
    p.add_argument("dir")
    p.add_argument("filter", nargs="*")
    for name in ("ssh", "carp", "asf"):
        p = sub.add_parser(name)
        p.add_argument("entry")
    p = sub.add_parser("eagl")
    p.add_argument("entry")
    p.add_argument("--max", type=int, default=60)
    sub.add_parser("test")
    args = ap.parse_args()
    {"stats": cmd_stats, "list": cmd_list, "extract": cmd_extract, "ssh": cmd_ssh, "carp": cmd_carp,
     "asf": cmd_asf, "eagl": cmd_eagl, "test": cmd_test}[args.cmd](args)


if __name__ == "__main__":
    main()
