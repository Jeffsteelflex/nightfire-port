"""Lista e extrai o conteúdo do FILES.BIN de 007: Nightfire (PS2, SLUS-20579).

O FILES.BIN não tem cabeçalho próprio. O índice fica embutido no ACTION.ELF,
no símbolo global `FileList` (seção .data): um array de entradas de 32 bytes.

    struct FileListEntry {        // 32 bytes
        char     name[16];        // ASCII, terminado em zero
        uint32_t offset;          // offset em bytes dentro do FILES.BIN
        uint32_t size;            // tamanho em bytes
        uint32_t pad[2];          // sempre zero
    };

Uso:
    python tools/filesbin.py list    [--elf ACTION.ELF] [--bin FILES.BIN]
    python tools/filesbin.py extract [--elf ACTION.ELF] [--bin FILES.BIN] [--out extracted/files]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from elfsyms import load  # noqa: E402

ENTRY = struct.Struct("<16sII8x")


def vaddr_to_offset(sections, vaddr):
    for _, s in sections:
        addr, off, size = s[3], s[4], s[5]
        if s[1] == 1 and addr <= vaddr < addr + size:  # SHT_PROGBITS
            return off + (vaddr - addr)
    raise ValueError(f"endereço 0x{vaddr:08x} fora das seções carregadas")


def read_file_list(elf_path):
    data, sections, syms = load(elf_path)
    sym = next((s for s in syms if s[4] == "FileList"), None)
    if sym is None:
        raise SystemExit("símbolo FileList não encontrado no ELF")
    vaddr, size = sym[0], sym[1]
    if size % ENTRY.size:
        raise SystemExit(f"tamanho inesperado de FileList: {size}")
    base = vaddr_to_offset(sections, vaddr)
    entries = []
    for i in range(size // ENTRY.size):
        name, offset, length = ENTRY.unpack_from(data, base + i * ENTRY.size)
        entries.append((name.split(b"\0", 1)[0].decode("ascii"), offset, length))
    return entries


def validate(entries, bin_size):
    problems = []
    ordered = sorted(entries, key=lambda e: e[1])
    for (n1, o1, s1), (n2, o2, _) in zip(ordered, ordered[1:]):
        if o1 + s1 > o2:
            problems.append(f"sobreposição: {n1} termina em 0x{o1 + s1:x}, {n2} começa em 0x{o2:x}")
    last = ordered[-1]
    if last[1] + last[2] > bin_size:
        problems.append(f"{last[0]} ultrapassa o fim do FILES.BIN")
    names = [e[0] for e in entries]
    dups = {n for n in names if names.count(n) > 1}
    if dups:
        problems.append(f"nomes duplicados: {sorted(dups)}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["list", "extract"])
    ap.add_argument("--elf", default="ACTION.ELF")
    ap.add_argument("--bin", default="FILES.BIN")
    ap.add_argument("--out", default=os.path.join("extracted", "files"))
    args = ap.parse_args()

    entries = read_file_list(args.elf)
    bin_size = os.path.getsize(args.bin)

    for p in validate(entries, bin_size):
        print("AVISO:", p, file=sys.stderr)

    if args.cmd == "list":
        print(f"{'#':>3}  {'nome':16s} {'offset':>10s} {'tamanho':>10s}")
        for i, (name, offset, length) in enumerate(entries):
            print(f"{i:3d}  {name:16s} 0x{offset:08x} {length:10d}")
        used = sum(e[2] for e in entries)
        print(f"\n{len(entries)} arquivos, {used:,} de {bin_size:,} bytes referenciados")
        return

    os.makedirs(args.out, exist_ok=True)
    with open(args.bin, "rb") as f:
        for name, offset, length in entries:
            f.seek(offset)
            blob = f.read(length)
            if len(blob) != length:
                raise SystemExit(f"leitura curta em {name}")
            with open(os.path.join(args.out, name), "wb") as o:
                o.write(blob)
    print(f"{len(entries)} arquivos extraídos em {args.out}")


if __name__ == "__main__":
    main()
