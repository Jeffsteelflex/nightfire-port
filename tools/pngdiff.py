"""Compara dois PNG (8 bits, RGB ou RGBA, sem entrelaçamento) pixel a pixel.

Uso: python tools/pngdiff.py a.png b.png
Conta os pixels com diferença maior que 8 em algum canal e mostra a caixa que os contém. Serve para
conferir mudanças no renderizador: compile o commit anterior numa worktree e gere as duas imagens
com o mesmo --cam e --screenshot.
"""
import struct
import sys
import zlib


def load(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos = 8
    idat = b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and inter == 0, (depth, inter)
            bpp = {6: 4, 2: 3}[ctype]
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
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
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, bpp, out


w1, h1, b1, p1 = load(sys.argv[1])
w2, h2, b2, p2 = load(sys.argv[2])
assert (w1, h1) == (w2, h2)
diff = 0
box = [w1, h1, -1, -1]
for y in range(h1):
    for x in range(w1):
        o1 = (y * w1 + x) * b1
        o2 = (y * w2 + x) * b2
        if max(abs(p1[o1 + k] - p2[o2 + k]) for k in range(3)) > 8:
            diff += 1
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
print(f'{diff} pixels diferentes de {w1 * h1} ({100.0 * diff / (w1 * h1):.3f}%), caixa {box}')
