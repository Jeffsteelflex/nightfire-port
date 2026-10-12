"""Lista e extrai os itens dos pacotes de fase (07xxxxxx.bin) extraídos do FILES.BIN.

Formato (ver docs/formats.md):

    0x000  uint32 table_size    tamanho da tabela de itens (múltiplo de 0x800)
    0x004  uint32 count         número de itens
    0x008  uint8  hash[12]      checksum ou hash do pacote (não verificado)
    ...    zeros até 0x800
    0x800  item_entry[count]    registros de tamanho variável
    0x800 + table_size          dados dos itens, concatenados na ordem da tabela

    struct item_entry {
        uint32 size;            // tamanho dos dados do item (alinhado a 32 bytes)
        uint8  type;            // tipo do recurso (ver TYPE_HINTS)
        char   name[];          // hashcode em hexa ("04000425") ou nome ("SKEL0002"), terminado em zero
    };

Uso:
    python tools/levelpak.py list    extracted/files/07000001.bin
    python tools/levelpak.py summary extracted/files/07*.bin
    python tools/levelpak.py extract extracted/files/07000001.bin [--out extracted/paks]
"""
import argparse
import collections
import glob
import os
import struct

# Confirmado em LoaderProcess() do ACTION.ELF (0x001d4880).
TYPE_HINTS = {
    0: "map",            # parsemap_parsemap(hash, 0)
    1: "map_main",       # AnimPostLoadInit(); parsemap_parsemap(hash, 1)
    2: "map",            # parsemap_parsemap(hash, 0)
    3: "anim",           # AnimLoadFile(hash, 0)
    4: "anim",           # AnimLoadFile(hash, 0)
    5: "anim",           # AnimLoadFile(hash, 0)
    6: "skeleton",       # AnimSkeletonProcess(buf)
    7: "script",         # Script_Load(hash, ...)
    8: "menu",           # MenuManager_Load(hash, buf)
    11: "map_alt_mem",   # igual ao 0, mas com MemType = 1
    12: "loadable",      # registra uma seção carregável (LoadableFiles)
    15: "memcard_icon",  # SetUpIconFile(buf, len)
    16: "woman",         # PS2SetUpWoman(buf)
}


def parse(data):
    table_size, count = struct.unpack_from("<II", data, 0)
    pos, off, items = 0x800, 0x800 + table_size, []
    for _ in range(count):
        size = struct.unpack_from("<I", data, pos)[0]
        typ = data[pos + 4]
        end = data.index(b"\0", pos + 5)
        items.append((off, size, typ, data[pos + 5:end].decode("latin1")))
        pos, off = end + 1, off + size
    if off != len(data):
        raise ValueError(f"tamanhos não fecham: dados terminam em 0x{off:x}, arquivo tem 0x{len(data):x}")
    return items


def expand(patterns):
    for p in patterns:
        yield from (sorted(glob.glob(p)) or [p])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["list", "summary", "extract"])
    ap.add_argument("paks", nargs="+")
    ap.add_argument("--out", default=os.path.join("extracted", "paks"))
    args = ap.parse_args()

    totals = collections.Counter()
    for path in expand(args.paks):
        data = open(path, "rb").read()
        items = parse(data)
        pak = os.path.splitext(os.path.basename(path))[0]

        if args.cmd == "list":
            print(f"# {pak}: {len(items)} itens")
            for off, size, typ, name in items:
                print(f"0x{off:08x} {size:9d}  tipo {typ:2d} {TYPE_HINTS.get(typ, '?'):16s} {name}")
        elif args.cmd == "summary":
            per = collections.Counter(t for _, _, t, _ in items)
            totals.update(per)
            print(f"{pak}: {len(items):5d} itens  " + " ".join(f"t{t}={c}" for t, c in sorted(per.items())))
        else:
            for off, size, typ, name in items:
                folder = os.path.join(args.out, pak, f"{typ:02d}_{TYPE_HINTS.get(typ, 'unknown')}")
                os.makedirs(folder, exist_ok=True)
                with open(os.path.join(folder, f"{name or 'unnamed'}_{off:08x}.bin"), "wb") as f:
                    f.write(data[off:off + size])
            print(f"{pak}: {len(items)} itens extraídos em {os.path.join(args.out, pak)}")

    if args.cmd == "summary":
        print("\nTOTAL: " + " ".join(f"t{t}={c}" for t, c in sorted(totals.items())))


if __name__ == "__main__":
    main()
