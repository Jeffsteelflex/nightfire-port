"""Leitura dos itens de mapa da EuroLand (tipos 0, 1, 2 e 11 dos pacotes de fase).

Formato confirmado em parsemap_parsemap / parsemap_parsenextblock / parsemap_handle_block_id
do ACTION.ELF. Ver docs/formats.md, seção "Itens de mapa".

Uso:
    python tools/mapfile.py blocks   <item.bin>
    python tools/mapfile.py textures <item.bin> [--out pasta] [--no-csm1]
"""
import argparse
import collections
import os
import struct
import zlib

BLOCK_NAMES = {
    0x00: "dict_xyz", 0x01: "dict_uv", 0x02: "dict_rgba", 0x03: "dict_comlist",
    0x04: "entity_params", 0x05: "ai_path", 0x08: "dict_xyz", 0x0A: "dict_comlist",
    0x0B: "dict_comlist", 0x0E: "map_header", 0x0F: "palette_header",
    0x10: "texture_header", 0x11: "palette_data_pc", 0x12: "palette_data_psx",
    0x15: "texture_data_pc", 0x16: "texture_data_pc", 0x17: "texture_data_gamecube",
    0x18: "texture_data_xbox", 0x19: "path_data", 0x1A: "map_data_static",
    0x1C: "memory_discard", 0x1D: "end", 0x1F: "entity_params", 0x20: "dict_norm",
    0x21: "portal_data", 0x22: "coll_data", 0x23: "coll_data", 0x24: "unknown_0x24",
    0x25: "datums", 0x26: "ambient_radiators", 0x27: "lod", 0x28: "map_sounds",
    0x29: "texture_data_pcdx", 0x2B: "morph_data", 0x2C: "hashlist", 0x2D: "ps2_gfx",
    0x2E: "coll_data_new", 0x2F: "coll_data_new", 0x30: "particles",
}

END = 0x1D


class Block:
    __slots__ = ("id", "size", "offset")

    def __init__(self, bid, size, offset):
        self.id, self.size, self.offset = bid, size, offset

    @property
    def name(self):
        return BLOCK_NAMES.get(self.id, f"unknown_0x{self.id:02x}")

    def payload(self, data):
        """Conteúdo do bloco sem o cabeçalho de 4 bytes."""
        return data[self.offset + 4:self.offset + self.size]


def read_blocks(data):
    """Versão 1: tabela de offsets (relativos a +4) logo após a versão.
    Outras versões: blocos em sequência a partir de +4. Termina no bloco 0x1D."""
    version = struct.unpack_from("<I", data, 0)[0]
    blocks, i, pos = [], 1, 4
    while True:
        if version == 1:
            pos = struct.unpack_from("<I", data, 4 * i)[0] + 4
            i += 1
        header = struct.unpack_from("<I", data, pos)[0]
        block = Block(header >> 24, header & 0xFFFFFF, pos)
        blocks.append(block)
        if block.id == END:
            return version, blocks
        if version != 1:
            pos += block.size


def unswizzle_csm1(palette):
    """Paletas de 256 cores no PS2 (CSM1) trocam as faixas 8-15 e 16-23 de cada grupo de 32."""
    out = list(palette)
    for base in range(0, 256, 32):
        for k in range(8):
            out[base + 8 + k], out[base + 16 + k] = palette[base + 16 + k], palette[base + 8 + k]
    return out


def textures(data, blocks, csm1=True):
    """Gera (índice, largura, altura_total, bpp, quadros, rgba_bytes) para cada textura do item.

    Cabeçalho de textura (bloco 0x10), 12 bytes por entrada:
        u8 flags (bit 0: 8 bpp; senão 4 bpp. Bits 0x08 e 0x10: desconhecidos)
        u8 desconhecido, u16 largura-1, u16 altura-1,
        u16 animação: byte baixo = número de quadros (1 = estática), byte alto = desconhecido
            (talvez velocidade). Os quadros vêm em sequência no bloco de pixels.
        u32 preenchido em tempo de carga com o ponteiro dos pixels
    """
    by_id = collections.defaultdict(list)
    for b in blocks:
        by_id[b.id].append(b)
    if 0x10 not in by_id:
        return
    headers = by_id[0x10][0].payload(data)
    tex_blocks = [b for b in blocks if b.id in (0x15, 0x16)]
    pal_blocks = [b for b in blocks if b.id in (0x11, 0x12)]
    for idx in range(len(headers) // 12):
        fmt, _unk1, w1, h1, anim, _ptr = struct.unpack_from("<BBHHHI", headers, idx * 12)
        w, h = w1 + 1, h1 + 1
        pixels = tex_blocks[idx].payload(data)
        raw_pal = pal_blocks[idx].payload(data)
        palette = [tuple(raw_pal[i:i + 4]) for i in range(0, len(raw_pal), 4)]
        bpp = 8 if fmt & 1 else 4
        frames = max(1, anim & 0xFF)
        expected = w * h * bpp // 8 * frames
        if len(pixels) != expected:
            print(f"AVISO: textura {idx} ({w}x{h}, fmt=0x{fmt:02x}, anim=0x{anim:04x}) "
                  f"tem {len(pixels)} bytes, esperado {expected}; ignorada")
            continue
        h *= frames  # quadros empilhados verticalmente
        if bpp == 8:
            if csm1 and len(palette) == 256:
                palette = unswizzle_csm1(palette)
            indices = pixels[:w * h]
        else:
            indices = bytearray()
            for byte in pixels[:(w * h) // 2]:
                indices.append(byte & 0x0F)
                indices.append(byte >> 4)
        rgba = bytearray()
        for ix in indices:
            r, g, b, a = palette[ix]
            rgba += bytes((r, g, b, min(255, a * 2)))  # alfa do PS2 vai de 0 a 0x80
        yield idx, w, h, bpp, frames, bytes(rgba)


def write_png(path, w, h, rgba):
    rows = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows, 6)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["blocks", "textures"])
    ap.add_argument("item")
    ap.add_argument("--out")
    ap.add_argument("--no-csm1", action="store_true", help="não reordenar paletas de 256 cores")
    args = ap.parse_args()

    data = open(args.item, "rb").read()
    version, blocks = read_blocks(data)

    if args.cmd == "blocks":
        print(f"versão {version}, {len(blocks)} blocos")
        counts = collections.Counter((b.id, b.name) for b in blocks)
        for (bid, name), n in sorted(counts.items()):
            size = sum(b.size for b in blocks if b.id == bid)
            print(f"  0x{bid:02x} {name:22s} x{n:<5d} {size:10,d} bytes")
        return

    out = args.out or os.path.join("extracted", "textures", os.path.splitext(os.path.basename(args.item))[0])
    os.makedirs(out, exist_ok=True)
    n = 0
    for idx, w, h, bpp, frames, rgba in textures(data, blocks, csm1=not args.no_csm1):
        suffix = f"_{frames}frames" if frames > 1 else ""
        write_png(os.path.join(out, f"tex{idx:04d}_{w}x{h // frames}_{bpp}bpp{suffix}.png"), w, h, rgba)
        n += 1
    print(f"{n} texturas exportadas em {out}")


if __name__ == "__main__":
    main()
