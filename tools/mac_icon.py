"""O ícone do pacote do macOS (src/app/macos/nightfire.icns; docs/port.md, "macOS: o pacote e a instalação").

Desenhado aqui com a arte do port, nada do jogo: o 007 cromado com a arma de hd_ui/svg/results_007.svg (a mesma
geometria de tools/ui_hd_assets.py, results_007_svg) sobre o quadrado de cantos arredondados dos ícones do macOS 11, no
fundo quente e escuro do estilo Modern (docs/design.md: "ground", o brilho da brasa, o fio dourado do foco).

Só a biblioteca padrão (sem Pillow): um rasterizador de polígonos com a cobertura de cada pixel por subamostras na
vertical e exata na horizontal, as curvas e as elipses em segmentos, os contornos do SVG como faixas entre o polígono
deslocado para fora e para dentro (cantos em ponta, como o stroke-linejoin padrão), os traços abertos com pontas redondas.
O ícone é desenhado em 1024 x 1024 e reduzido pela média de 2 x 2 até 16 x 16; o .icns guarda os PNG (ic07 a ic14, icp4 e
icp5: o conjunto de um iconset do iconutil).

Uso:
    python tools/mac_icon.py              # escreve src/app/macos/nightfire.icns (uns 20 s)
    python tools/mac_icon.py --png <dir>  # também os PNG de cada tamanho, para olhar
    python tools/mac_icon.py --check      # confere que o .icns do repositório é o que este script desenha

O .icns do repositório foi gravado no Windows; o seno e o cosseno da libm de outro sistema podem mudar um bit de um
vértice e o --check falhar lá sem que o desenho mude.
"""
import argparse
import math
import os
import struct
import sys
import zlib
from array import array

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "app", "macos", "nightfire.icns")
SIZE = 1024
SUB = 8  # subamostras por pixel na vertical


# ---------------------------------------------------------------- cores

def hexc(s, a=1.0):
    s = s.lstrip("#")
    return (int(s[0:2], 16) / 255.0, int(s[2:4], 16) / 255.0, int(s[4:6], 16) / 255.0, a)


def lerp_stops(stops, t):
    if t <= stops[0][0]:
        return stops[0][1]
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * f for a, b in zip(c0, c1))
    return stops[-1][1]


class Solid:
    def __init__(self, c):
        self.c = c

    def row(self, y):
        return self.c


class LinearY:
    """Um degradê vertical: stops [(y, cor)] em coordenadas do ícone."""

    def __init__(self, y0, y1, stops):
        self.y0, self.y1, self.stops = y0, y1, stops

    def row(self, y):
        t = (y - self.y0) / (self.y1 - self.y0)
        return lerp_stops(self.stops, t)


class Radial:
    """Um brilho elíptico: a cor com alfa a no centro, caindo com (1 - d)^2 até 0 na borda (rx, ry)."""

    def __init__(self, cx, cy, rx, ry, color, a):
        self.cx, self.cy, self.rx, self.ry, self.color, self.a = cx, cy, rx, ry, color, a

    def row(self, y):
        return None  # por pixel

    def at(self, x, y):
        dx = (x - self.cx) / self.rx
        dy = (y - self.cy) / self.ry
        d = math.sqrt(dx * dx + dy * dy)
        k = max(0.0, 1.0 - d)
        r, g, b, _ = self.color
        return (r, g, b, self.a * k * k)


# ---------------------------------------------------------------- tela e preenchimento

class Canvas:
    """RGBA pré-multiplicado em float."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        n = w * h
        self.r = array("d", bytes(8 * n))
        self.g = array("d", bytes(8 * n))
        self.b = array("d", bytes(8 * n))
        self.a = array("d", bytes(8 * n))

    def fill(self, polys, paint, evenodd=False, opacity=1.0):
        """Preenche os polígonos fechados (listas de (x, y)) com a regra não nula ou par-ímpar."""
        edges = []
        for poly in polys:
            n = len(poly)
            for i in range(n):
                x0, y0 = poly[i]
                x1, y1 = poly[(i + 1) % n]
                if y0 == y1:
                    continue
                if y0 < y1:
                    edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0), 1))
                else:
                    edges.append((y1, y0, x1, (x0 - x1) / (y0 - y1), -1))
        if not edges:
            return
        edges.sort()
        ymin = max(0, int(math.floor(edges[0][0])))
        ymax = min(self.h - 1, int(math.ceil(max(e[1] for e in edges))))
        xlo = max(0, int(math.floor(min(min(p[0] for p in poly) for poly in polys))) - 1)
        xhi = min(self.w, int(math.ceil(max(max(p[0] for p in poly) for poly in polys))) + 2)
        w = self.w
        nxt = 0
        active = []
        inv = 1.0 / SUB
        for y in range(ymin, ymax + 1):
            while nxt < len(edges) and edges[nxt][0] < y + 1:
                active.append(edges[nxt])
                nxt += 1
            active = [e for e in active if e[1] > y]
            if not active:
                continue
            acc = [0.0] * (w + 2)
            diff = [0.0] * (w + 2)
            any_span = False
            for k in range(SUB):
                sy = y + (k + 0.5) * inv
                xs = []
                for (ey0, ey1, ex0, slope, d) in active:
                    if ey0 <= sy < ey1:
                        xs.append((ex0 + (sy - ey0) * slope, d))
                if not xs:
                    continue
                xs.sort()
                wind = 0
                start = 0.0
                for x, d in xs:
                    was = (wind & 1) if evenodd else (wind != 0)
                    wind += d
                    now = (wind & 1) if evenodd else (wind != 0)
                    if now and not was:
                        start = x
                    elif was and not now:
                        xa = min(max(start, 0.0), float(w))
                        xb = min(max(x, 0.0), float(w))
                        if xb > xa:
                            any_span = True
                            ia = int(xa)
                            ib = int(xb)
                            if ia == ib:
                                acc[ia] += (xb - xa) * inv
                            else:
                                acc[ia] += (ia + 1 - xa) * inv
                                if ib > ia + 1:
                                    diff[ia + 1] += inv
                                    diff[ib] -= inv
                                acc[ib] += (xb - ib) * inv
            if not any_span:
                continue
            color = paint.row(y + 0.5)
            run = 0.0
            base = y * w
            R, G, B, A = self.r, self.g, self.b, self.a
            for x in range(xlo, xhi):
                run += diff[x]
                c = acc[x] + run
                if c <= 0.0:
                    continue
                if c > 1.0:
                    c = 1.0
                cr, cg, cb, ca = color if color is not None else paint.at(x + 0.5, y + 0.5)
                sa = ca * c * opacity
                if sa <= 0.0:
                    continue
                i = base + x
                keep = 1.0 - sa
                R[i] = cr * sa + R[i] * keep
                G[i] = cg * sa + G[i] * keep
                B[i] = cb * sa + B[i] * keep
                A[i] = sa + A[i] * keep

    def half(self):
        """A média de 2 x 2 (pré-multiplicada)."""
        w, h = self.w // 2, self.h // 2
        out = Canvas(w, h)
        for ch_in, ch_out in ((self.r, out.r), (self.g, out.g), (self.b, out.b), (self.a, out.a)):
            for y in range(h):
                r0 = 2 * y * self.w
                r1 = r0 + self.w
                o = y * w
                for x in range(w):
                    s = 2 * x
                    ch_out[o + x] = (ch_in[r0 + s] + ch_in[r0 + s + 1] + ch_in[r1 + s] + ch_in[r1 + s + 1]) * 0.25
        return out

    def png(self):
        """PNG RGBA de 8 bits (cor sem pré-multiplicar), filtro por linha (Sub ou Up, o de menor soma)."""
        w, h = self.w, self.h
        rows = []
        for y in range(h):
            line = bytearray(4 * w)
            o = y * w
            for x in range(w):
                a = self.a[o + x]
                if a > 1e-6:
                    r, g, b = self.r[o + x] / a, self.g[o + x] / a, self.b[o + x] / a
                else:
                    r = g = b = 0.0
                j = 4 * x
                line[j] = max(0, min(255, int(r * 255.0 + 0.5)))
                line[j + 1] = max(0, min(255, int(g * 255.0 + 0.5)))
                line[j + 2] = max(0, min(255, int(b * 255.0 + 0.5)))
                line[j + 3] = max(0, min(255, int(a * 255.0 + 0.5)))
            rows.append(line)
        raw = bytearray()
        prev = bytearray(4 * w)
        for line in rows:
            sub = bytearray(4 * w)
            up = bytearray(4 * w)
            for j in range(4 * w):
                sub[j] = (line[j] - (line[j - 4] if j >= 4 else 0)) & 255
                up[j] = (line[j] - prev[j]) & 255
            cost_sub = sum(v if v < 128 else 256 - v for v in sub)
            cost_up = sum(v if v < 128 else 256 - v for v in up)
            if cost_up < cost_sub:
                raw.append(2)
                raw += up
            else:
                raw.append(1)
                raw += sub
            prev = line

        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

        ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
                chunk(b"IEND", b""))


# ---------------------------------------------------------------- geometria

def ellipse(cx, cy, rx, ry, n=256):
    return [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n)) for i in range(n)]


def rounded_rect(x, y, w, h, r, n=40):
    pts = []
    corners = [(x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)]
    for cx, cy, a0 in corners:
        for i in range(n + 1):
            a = math.radians(a0 + 90.0 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def cubic(p0, p1, p2, p3, n=24):
    out = []
    for i in range(1, n + 1):
        t = i / n
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                    u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return out


def area(poly):
    s = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return s * 0.5


def offset(poly, d):
    """O polígono deslocado d para fora (d < 0: para dentro), cantos em ponta (limite 4, como o SVG)."""
    n = len(poly)
    sign = -1.0 if area(poly) > 0 else 1.0  # a normal "para fora" é a da direita num polígono de área positiva
    normals = []
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy) or 1.0
        normals.append((sign * -dy / ln, sign * dx / ln))
    out = []
    for i in range(n):
        na = normals[i - 1]
        nb = normals[i]
        dot = na[0] * nb[0] + na[1] * nb[1]
        k = 1.0 / max(1.0 + dot, 2.0 / 16.0)  # o comprimento da ponta até 4 vezes d
        out.append((poly[i][0] + d * (na[0] + nb[0]) * k, poly[i][1] + d * (na[1] + nb[1]) * k))
    return out


def stroke_closed(poly, width):
    return [offset(poly, width / 2), offset(poly, -width / 2)]  # par-ímpar


def stroke_open(points, width):
    """Um traço aberto com pontas e junções redondas: um quadrilátero por segmento e um círculo por ponto (todos no
    mesmo sentido: a regra não nula dá a união)."""
    r = width / 2
    polys = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy)
        if ln == 0:
            continue
        nx, ny = -dy / ln * r, dx / ln * r
        quad = [(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)]
        if area(quad) < 0:
            quad.reverse()
        polys.append(quad)
    for (x, y) in points:
        c = ellipse(x, y, r, r, 32)
        if area(c) < 0:
            c.reverse()
        polys.append(c)
    return polys


# ---------------------------------------------------------------- o 007 (hd_ui/svg/results_007.svg)

SKEW = math.tan(math.radians(-14.0))


def svg_point(p):
    """O transform do grupo: translate(25,0) skewX(-14)."""
    return (p[0] + SKEW * p[1] + 25.0, p[1])


def logo_shapes():
    """As figuras do 007 em coordenadas do SVG (antes do transform), na ordem de pintura: (polígonos, par-ímpar, tinta,
    contorno (largura, cor) ou nada). A tinta "chrome"/"barrel" é o degradê do SVG."""
    shapes = []
    for cx in (30.0, 88.0):
        ring = [ellipse(cx, 52, 27, 47), ellipse(cx, 52, 11, 35)]
        stroke = [ellipse(cx, 52, 27.65, 47.65), ellipse(cx, 52, 26.35, 46.35),
                  ellipse(cx, 52, 11.65, 35.65), ellipse(cx, 52, 10.35, 34.35)]
        shapes.append((ring, True, "chrome", None))
        shapes.append((stroke, True, "#3e3e40", None))
    seven = [(120, 6), (178, 6), (178, 21), (150, 99), (126, 99), (157, 22), (120, 22)]
    shapes.append(([seven], False, "chrome", None))
    shapes.append((stroke_closed(seven, 1.3), True, "#3e3e40", None))
    barrel = [(176, 7), (226, 7)] + cubic((226, 7), (229, 7), (230, 9), (230, 12)) + [(230, 16)]
    barrel += cubic((230, 16), (230, 19), (229, 20), (226, 20)) + [(176, 20)]
    shapes.append(([barrel], False, "barrel", None))
    shapes.append((stroke_closed(barrel, 1.2), True, "#3e3e40", None))
    sight = [(221, 3.5), (225, 3.5), (225, 7.5), (221, 7.5)]
    shapes.append(([sight], False, "#d0d0d2", None))
    shapes.append((stroke_closed(sight, 0.8), True, "#3e3e40", None))
    guard = [(182, 20)] + cubic((182, 20), (182, 31), (196, 33), (199, 22))
    shapes.append((stroke_open(guard, 4.2), False, "#3e3e40", None))
    shapes.append((stroke_open(guard, 2.4), False, "#d4d4d6", None))
    trigger = [(189, 20)] + cubic((189, 20), (190, 24), (189, 27), (187, 29))
    shapes.append((stroke_open(trigger, 1.6), False, "#bcbcbe", None))
    return shapes


CHROME = [(0, "#ffffff"), (0.22, "#e2e2e2"), (0.46, "#a4a4a6"), (0.52, "#cfcfd1"), (0.78, "#f2f2f2"), (1, "#8e8e90")]
BARREL = [(0, "#ffffff"), (0.4, "#d8d8da"), (0.7, "#9c9c9e"), (1, "#e8e8ea")]


# ---------------------------------------------------------------- o ícone

def draw():
    c = Canvas(SIZE, SIZE)
    # O corpo do ícone do macOS 11: 824 x 824 no meio de 1024, cantos de ~185.
    bx, by, bw, br = 100.0, 100.0, 824.0, 185.0
    body = rounded_rect(bx, by, bw, bw, br)
    # A sombra: anéis cada vez maiores, deslocados para baixo, com pouco alfa (somados, uma borda macia).
    for k in range(14, 0, -1):
        e = 1.8 * k
        c.fill([rounded_rect(bx - e, by - e + 10, bw + 2 * e, bw + 2 * e, br + e)], Solid(hexc("#000000", 0.028)))
    # O fundo quente e escuro (o "ground" do estilo Modern embaixo), o brilho da brasa atrás do 007 e um reflexo em cima.
    c.fill([body], LinearY(by, by + bw, [(0, hexc("#2b1f15")), (0.55, hexc("#161009")), (1, hexc("#0d0a08"))]))
    c.fill([body], Radial(512, 560, 470, 400, hexc("#ff8a3d"), 0.20))
    c.fill([body], LinearY(by, by + 360, [(0, hexc("#fff4e0", 0.07)), (1, hexc("#fff4e0", 0.0))]))
    # O fio dourado por dentro da borda.
    c.fill([rounded_rect(bx + 10, by + 10, bw - 20, bw - 20, br - 10), rounded_rect(bx + 13, by + 13, bw - 26, bw - 26, br - 13)],
           Solid(hexc("#f2c77a", 0.30)), evenodd=True)

    # O 007: a escala que dá 690 de largura, centrado um pouco acima do meio.
    shapes = logo_shapes()
    pts = [svg_point(p) for polys, _, _, _ in shapes for poly in polys for p in poly]
    x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
    y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
    s = 690.0 / (x1 - x0)
    ox = 512.0 - s * (x0 + x1) / 2
    oy = 470.0 - s * (y0 + y1) / 2

    def place(poly):
        return [(ox + s * q[0], oy + s * q[1]) for q in (svg_point(p) for p in poly)]

    for polys, evenodd, ink, _ in shapes:
        if ink == "chrome":
            paint = LinearY(oy, oy + s * 104, [(t, hexc(col)) for t, col in CHROME])
        elif ink == "barrel":
            paint = LinearY(oy + s * 6, oy + s * 20, [(t, hexc(col)) for t, col in BARREL])
        else:
            paint = Solid(hexc(ink))
        c.fill([place(p) for p in polys], paint, evenodd=evenodd)

    # O foco do estilo Modern embaixo: o fio dourado com a brasa.
    line_y = oy + s * y1 + 70
    c.fill([rounded_rect(512 - 330, line_y - 50, 660, 100, 50)], Radial(512, line_y, 330, 46, hexc("#ff8a3d"), 0.55))
    c.fill([rounded_rect(512 - 170, line_y - 6, 340, 12, 6)],
           LinearY(line_y - 6, line_y + 6, [(0, hexc("#ffe2a8")), (1, hexc("#e6b064"))]))
    return c


# O conjunto de um iconset (o que o iconutil grava): o tipo, o tamanho em pixels.
ICNS_ENTRIES = [("icp4", 16), ("icp5", 32), ("ic11", 32), ("ic12", 64), ("ic07", 128), ("ic13", 256), ("ic08", 256),
                ("ic14", 512), ("ic09", 512), ("ic10", 1024)]


def build(png_dir=None):
    big = draw()
    pngs = {}
    level = big
    size = SIZE
    while size >= 16:
        pngs[size] = level.png()
        if png_dir:
            os.makedirs(png_dir, exist_ok=True)
            with open(os.path.join(png_dir, "icon_%d.png" % size), "wb") as f:
                f.write(pngs[size])
        if size == 16:
            break
        level = level.half()
        size //= 2
    body = b"".join(t.encode("ascii") + struct.pack(">I", 8 + len(pngs[n])) + pngs[n] for t, n in ICNS_ENTRIES)
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def main():
    ap = argparse.ArgumentParser(description="o ícone do pacote do macOS")
    ap.add_argument("--png", default=None, help="grava também os PNG de cada tamanho nesta pasta")
    ap.add_argument("--check", action="store_true", help="confere o .icns do repositório")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    data = build(a.png)
    if a.check:
        old = open(a.out, "rb").read() if os.path.isfile(a.out) else b""
        if old != data:
            print("mac_icon: %s não é o que o script desenha (rode python tools/mac_icon.py)" % a.out)
            return 1
        print("mac_icon: %s confere (%d bytes)" % (a.out, len(data)))
        return 0
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(data)
    print("mac_icon: %s (%d bytes, %d imagens)" % (a.out, len(data), len(ICNS_ENTRIES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
