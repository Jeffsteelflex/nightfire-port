"""Lista seções e símbolos de um ELF MIPS do PS2 (little-endian, 32 bits).

Uso: python tools/elfsyms.py ACTION.ELF [saida.txt]
Saída: endereço, tamanho, tipo (FUNC/OBJECT/NOTYPE/SECTION/FILE), nome.
"""
import struct
import sys

TYPES = {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 3: "SECTION", 4: "FILE"}


def load(path):
    d = open(path, "rb").read()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]

    def cstr(sec, idx):
        start = sec[4] + idx
        return d[start:d.index(b"\0", start)].decode("latin1")

    sections = [(cstr(secs[shstrndx], s[0]), s) for s in secs]
    syms = []
    for _, s in sections:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for i in range(s[5] // 16):
            name, value, size, info, _other, shndx = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
            syms.append((value, size, TYPES.get(info & 0xF, str(info & 0xF)), shndx,
                         cstr(strtab, name) if name else ""))
    return d, sections, syms


def main():
    _, sections, syms = load(sys.argv[1])
    out = open(sys.argv[2], "w", encoding="utf-8") if len(sys.argv) > 2 else sys.stdout
    for name, s in sections:
        print(f"# {name:24s} addr=0x{s[3]:08x} off=0x{s[4]:08x} size=0x{s[5]:08x}", file=out)
    for value, size, typ, shndx, name in sorted(syms):
        print(f"{value:08x} {size:7d} {typ:7s} {shndx:5d} {name}", file=out)


if __name__ == "__main__":
    main()
