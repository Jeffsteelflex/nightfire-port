"""Recomprime PNGs sem perda (só a biblioteca padrão): o filtro de cada linha escolhido pela menor soma (a heurística do
libpng), o zlib no nível 9, e RGB no lugar de RGBA quando o alfa é todo 255. Os pixels não mudam.

Uso: python tools/png_shrink.py arquivo.png [...]
Usado nas imagens de referência do desenho vetorial (tests/data/vector; docs/port.md, "macOS: o desenho vetorial"), que o
nf_vector_check grava pelo WIC do Windows com uma compressão menor.
"""
import os
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("não é PNG: " + path)
    pos, idat = 8, b""
    w = h = depth = ctype = interlace = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    if depth != 8 or ctype not in (2, 6) or interlace != 0:
        raise ValueError("só RGB ou RGBA de 8 bits sem entrelaçamento: " + path)
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    i = 0
    for y in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bpp, out


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def write_png(path, w, h, bpp, px):
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    for y in range(h):
        line = px[y * stride:(y + 1) * stride]
        best = None
        for f in (0, 1, 2, 4):
            enc = bytearray(stride)
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                if f == 0:
                    v = line[x]
                elif f == 1:
                    v = line[x] - a
                elif f == 2:
                    v = line[x] - b
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    v = line[x] - (a if pa <= pb and pa <= pc else (b if pb <= pc else c))
                enc[x] = v & 255
            score = sum(v if v < 128 else 256 - v for v in enc)
            if best is None or score < best[0]:
                best = (score, f, enc)
        out.append(best[1])
        out += best[2]
        prev = line
    head = struct.pack(">IIBBBBB", w, h, 8, 6 if bpp == 4 else 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(bytes(out), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def shrink(path):
    before = os.path.getsize(path)
    w, h, bpp, px = read_png(path)
    if bpp == 4 and all(px[i] == 255 for i in range(3, len(px), 4)):
        rgb = bytearray()
        for i in range(0, len(px), 4):
            rgb += px[i:i + 3]
        px, bpp = rgb, 3
    tmp = path + ".tmp"
    write_png(tmp, w, h, bpp, px)
    w2, h2, bpp2, px2 = read_png(tmp)
    if (w2, h2, bpp2, bytes(px2)) != (w, h, bpp, bytes(px)):
        os.remove(tmp)
        raise RuntimeError("a releitura não bate: " + path)
    after = os.path.getsize(tmp)
    if after < before:
        os.replace(tmp, path)
    else:
        os.remove(tmp)
    return before, min(before, after)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    total_before = total_after = 0
    for path in argv:
        before, after = shrink(path)
        total_before += before
        total_after += after
        print("%-40s %9d -> %9d" % (os.path.basename(path), before, after))
    print("total %d -> %d bytes" % (total_before, total_after))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
