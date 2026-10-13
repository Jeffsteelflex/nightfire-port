"""Gera as substitutas SVG da camada 2D em alta definição (hd_ui/svg) a partir de geometria própria.

Uso:
    python tools/ui_hd_assets.py            # escreve os SVG em hd_ui/svg
    python tools/ui_hd_assets.py --check    # mede as lâminas do medidor de vida contra os retângulos dos sprites

Os desenhos são vetoriais e feitos aqui (nada é copiado nem traçado das texturas do jogo); só as medidas
(onde cada sprite cai na tela do PS2 e o recorte dele na textura) vêm do inventário (--ui-inventory) e de
docs/port.md. Cada SVG cobre o retângulo da chave dele no hd_ui/map.json:

- mira (03000026, [0, 0, 31, 32]): círculo e cruz tracejada, em coordenadas de texel (o centro do texel u
  é x = u); o recorte do quadro da mira em uso (HUDCrossCoords) sai do mesmo desenho.
- faixa das mensagens (0300007a, pontas [185..190] e [191..196], meio [190..191] esticado): retângulo
  escuro de cantos arredondados.
- balas da pilha de munição (03000128, coluna [26, 0, 15, 256], v "exclusive"): 64 cartuchos de 4 texels
  de altura, alternando 1 texel para a esquerda, como a pilha original.
- medidor de vida (0300007a, sprites 8 a 14 da pane 1 e o anel 15): uma íris de 7 lâminas em espiral
  (o cano da abertura dos filmes). Cada lâmina é a faixa entre duas espirais theta = a - b / r no anel
  r_i..R, desenhada nas coordenadas da tela do PS2 (viewBox = o retângulo do sprite na tela), para que as
  sete se encaixem como no jogo. Cinza claro: a cor (verde, amarelo, vermelho) e o alfa vêm do sprite.
- menus (docs/port.md, "Inventário dos menus"): o painel 0x03000070 (moldura, faixa das abas, vidro e
  barra de ajuda, em coordenadas de texel: psi_2d_nine_slice corta nos trechos retos), as peças de
  0x03000042 (botão, rádio, setas, caixas de marcar, destaque, volume), o 007 e o canto do resultado
  (0x03000140, 0x0300014c) e os símbolos de botão dos textos nos conjuntos do PS2 e do Xbox ("prompts" no
  map.json; as letras são traços próprios, o Direct2D não desenha <text> de SVG). A imagem do painel da
  pausa (0x030000db) não tem substituta: no jogo ela é animada (o fundo da pausa, platform/psi_woman.h).
"""
import math
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "hd_ui", "svg")

# ---------------------------------------------------------------- medidor de vida (íris)

CX, CY = 64.5, 384.5      # centro da abertura na tela do PS2
R_IN, R_OUT = 15.5, 42.6  # abertura e borda das lâminas (o anel, sprite 15, cobre a borda)
TWIST = 1400.0            # graus x pixel: theta(r) = fase - TWIST / r

# Sprite da pane 1 -> retângulo na tela do PS2 (x, y, largura, altura), do inventário.
BLADES = {
    8: (42, 342, 35, 49),
    9: (20, 348, 37, 51),
    10: (21, 375, 51, 38),
    11: (33, 386, 47, 42),
    12: (67, 376, 33, 51),
    13: (64, 367, 42, 39),
    14: (52, 345, 50, 30),
}
RING = (20, 341, 87, 86)  # sprite 15


# Fase e largura de cada lâmina (as do jogo não são exatamente regulares: cada uma cabe no retângulo do
# sprite dela). Medidas na montagem das lâminas originais na tela (aresta de baixo em r = 32).
BLADE_PHASE = {14: -43.4, 13: 8.3, 12: 62.2, 11: 115.4, 10: 168.3, 9: 213.1, 8: 266.1}
BLADE_SPAN = {14: 54.2, 13: 57.0, 12: 55.3, 11: 56.1, 10: 51.2, 9: 56.0, 8: 55.4}


def blade_angle(sprite, r, edge):
    return BLADE_PHASE[sprite] - TWIST / r + (BLADE_SPAN[sprite] if edge else 0.0)


def polar(r, deg):
    a = math.radians(deg)
    return CX + r * math.cos(a), CY + r * math.sin(a)


def blade_outline(sprite, steps=24):
    """Contorno de uma lâmina: espiral de baixo para fora, arco externo, espiral de cima para dentro, arco
    interno."""
    pts = []
    for i in range(steps + 1):
        r = R_IN + (R_OUT - R_IN) * i / steps
        pts.append(polar(r, blade_angle(sprite, r, 0)))
    a0, a1 = blade_angle(sprite, R_OUT, 0), blade_angle(sprite, R_OUT, 1)
    for i in range(1, 12):
        pts.append(polar(R_OUT, a0 + (a1 - a0) * i / 12))
    for i in range(steps, -1, -1):
        r = R_IN + (R_OUT - R_IN) * i / steps
        pts.append(polar(r, blade_angle(sprite, r, 1)))
    b0, b1 = blade_angle(sprite, R_IN, 1), blade_angle(sprite, R_IN, 0)
    for i in range(1, 8):
        pts.append(polar(R_IN, b0 + (b1 - b0) * i / 8))
    return pts


def path_of(pts):
    return "M" + " L".join("%.2f,%.2f" % p for p in pts) + " Z"


def svg(view, body, defs=""):
    # width/height em 100%: o desenho ocupa o tamanho em que o Direct2D o rasteriza (com tamanhos absolutos,
    # ele ficaria nesse tamanho num canto); preserveAspectRatio="none" estica para o retângulo do elemento.
    x, y, w, h = view
    head = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="%g %g %g %g" width="100%%" height="100%%" '
            'preserveAspectRatio="none">\n' % (x, y, w, h))
    if defs:
        head += "<defs>\n" + defs + "</defs>\n"
    return head + body + "</svg>\n"


def blade_svg(sprite):
    rect = BLADES[sprite]
    pts = blade_outline(sprite)
    rm = (R_IN + R_OUT) * 0.5
    # Degradê da borda de cima (clara) para a de baixo (sombra), na metade do raio.
    lx, ly = polar(rm, blade_angle(sprite, rm, 1))
    dx, dy = polar(rm, blade_angle(sprite, rm, 0))
    defs = ('<linearGradient id="g" gradientUnits="userSpaceOnUse" x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f">\n'
            '<stop offset="0" stop-color="#f4f3f0"/>\n<stop offset="0.55" stop-color="#cfcecb"/>\n'
            '<stop offset="1" stop-color="#7d7c7a"/>\n</linearGradient>\n' % (lx, ly, dx, dy))
    # A aresta de baixo (a que fica sob a lâmina vizinha) leva um traço escuro; a de fora, um brilho fino.
    lower = [polar(R_IN + (R_OUT - R_IN) * i / 24, blade_angle(sprite, R_IN + (R_OUT - R_IN) * i / 24, 0))
             for i in range(25)]
    a0, a1 = blade_angle(sprite, R_OUT, 0), blade_angle(sprite, R_OUT, 1)
    outer = [polar(R_OUT - 0.6, a0 + (a1 - a0) * i / 12) for i in range(13)]
    body = '<path d="%s" fill="url(#g)"/>\n' % path_of(pts)
    body += ('<path d="M%s" fill="none" stroke="#3b3a38" stroke-width="0.9" stroke-linecap="round"/>\n'
             % " L".join("%.2f,%.2f" % p for p in lower))
    body += ('<path d="M%s" fill="none" stroke="#ffffff" stroke-opacity="0.7" stroke-width="0.6"/>\n'
             % " L".join("%.2f,%.2f" % p for p in outer))
    return svg(rect, body, defs)


def ring_svg():
    x, y, w, h = RING
    cx, cy = x + w / 2.0, y + h / 2.0
    body = ('<ellipse cx="%.2f" cy="%.2f" rx="%.2f" ry="%.2f" fill="none" stroke="#9a9a99" stroke-width="1.5"/>\n'
            % (cx, cy, w / 2.0 - 1.4, h / 2.0 - 1.4))
    body += ('<ellipse cx="%.2f" cy="%.2f" rx="%.2f" ry="%.2f" fill="none" stroke="#d8d8d6" stroke-opacity="0.6" '
             'stroke-width="0.5"/>\n' % (cx, cy, w / 2.0 - 1.9, h / 2.0 - 1.9))
    return svg(RING, body)


def check():
    """Quanto de cada lâmina cai fora do retângulo do sprite dela (o que o recorte do quadrado cortaria)."""
    ok = True
    for s, (x, y, w, h) in sorted(BLADES.items()):
        pts = blade_outline(s, 96)
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        out = max(x - min(xs), y - min(ys), max(xs) - (x + w), max(ys) - (y + h), 0.0)
        print("sprite %2d: lâmina %.1f..%.1f x %.1f..%.1f, retângulo %d..%d x %d..%d, fora %.2f px"
              % (s, min(xs), max(xs), min(ys), max(ys), x, x + w, y, y + h, out))
        ok &= out <= 3.5  # o quadrado do sprite corta o que passa; o anel cobre as pontas de fora
    return ok


# ---------------------------------------------------------------- mira

def crosshair_svg():
    # Coordenadas de texel (centro do texel u em x = u): círculo de raio 13,5 em (15, 16) e a cruz em
    # x = 15 e y = 16, tracejada como a original (falha no meio e a cada trecho).
    body = '<circle cx="15" cy="16" r="13.4" fill="none" stroke="#ffffff" stroke-opacity="0.62" stroke-width="1.15"/>\n'
    # Trechos da vertical (bordas dos texels: linhas 2-9, 11-14, 18-21 e 23-30); os da horizontal são os
    # mesmos um texel para a esquerda (colunas 1-8, 10-13, 17-20 e 22-29).
    dashes = [(1.5, 9.5), (10.5, 14.5), (17.5, 21.5), (22.5, 30.5)]
    for a, b in dashes:
        body += '<line x1="15" y1="%.1f" x2="15" y2="%.1f" stroke="#ffffff" stroke-width="1" stroke-linecap="butt"/>\n' % (a, b)
        body += '<line x1="%.1f" y1="16" x2="%.1f" y2="16" stroke="#ffffff" stroke-width="1" stroke-linecap="butt"/>\n' % (a - 1.0, b - 1.0)
    return svg((0, 0, 30, 31), body)


# ---------------------------------------------------------------- faixa das mensagens

BAND_FILL = ('<linearGradient id="f" x1="0" y1="0" x2="0" y2="1">\n'
             '<stop offset="0" stop-color="#26150c"/>\n<stop offset="0.5" stop-color="#140a06"/>\n'
             '<stop offset="1" stop-color="#0c0604"/>\n</linearGradient>\n')


def band_svg(part):
    # A faixa vai da borda esquerda da ponta esquerda (x = 185, o centro do primeiro texel dela, onde o
    # quadrado começa) à borda direita da ponta direita (196) e das linhas 0 a 55,5, com cantos de raio 3
    # e um fio claro por dentro; cada pedaço mostra a parte dele.
    views = {"left": (185, 0, 5, 56), "mid": (190, 0, 1, 56), "right": (191, 0, 5, 56)}
    body = ('<rect x="185" y="0" width="11" height="55.5" rx="3" ry="3" fill="url(#f)"/>\n'
            '<rect x="185.6" y="0.6" width="9.8" height="54.3" rx="2.4" ry="2.4" fill="none" stroke="#6a4a34" '
            'stroke-opacity="0.35" stroke-width="0.35"/>\n')
    if part == "mid":  # o meio é esticado: só o preenchimento e os fios de cima e de baixo
        body = ('<rect x="180" y="0" width="20" height="55.5" fill="url(#f)"/>\n'
                '<line x1="180" y1="0.6" x2="200" y2="0.6" stroke="#6a4a34" stroke-opacity="0.35" stroke-width="0.35"/>\n'
                '<line x1="180" y1="54.9" x2="200" y2="54.9" stroke="#6a4a34" stroke-opacity="0.35" stroke-width="0.35"/>\n')
    return svg(views[part], body, BAND_FILL)


# ---------------------------------------------------------------- balas

def bullets_svg():
    # Coluna de 64 cartuchos deitados (4 texels de altura cada), estojo de latão e ponta à direita; os
    # ímpares 1 texel à esquerda, como a pilha original. x: centros de texel 26..40 -> 0..14; y: bordas.
    defs = ('<linearGradient id="b" x1="0" y1="0" x2="0" y2="1">\n'
            '<stop offset="0" stop-color="#6e6236"/>\n<stop offset="0.3" stop-color="#e2ddc6"/>\n'
            '<stop offset="0.55" stop-color="#8a7a3c"/>\n<stop offset="1" stop-color="#3a2d0c"/>\n'
            '</linearGradient>\n'
            '<linearGradient id="t" x1="0" y1="0" x2="0" y2="1">\n'
            '<stop offset="0" stop-color="#7a5a34"/>\n<stop offset="0.3" stop-color="#c8a07a"/>\n'
            '<stop offset="1" stop-color="#3c2410"/>\n</linearGradient>\n')
    body = ""
    for i in range(64):
        y = 4 * i
        dx = -1.0 if i % 2 else 0.0
        x0 = 1.4 + dx
        # estojo
        body += ('<rect x="%.2f" y="%.2f" width="9.2" height="3.3" rx="0.4" fill="url(#b)" '
                 'stroke="#241604" stroke-width="0.3"/>\n' % (x0, y + 0.35))
        # aro
        body += '<rect x="%.2f" y="%.2f" width="0.8" height="3.5" fill="#4a3c18"/>\n' % (x0 - 0.5, y + 0.25)
        # ponta
        body += ('<path d="M%.2f,%.2f L%.2f,%.2f Q%.2f,%.2f %.2f,%.2f Z" fill="url(#t)" stroke="#2a1606" '
                 'stroke-width="0.3"/>\n' % (x0 + 9.2, y + 0.55, x0 + 11.2, y + 0.9, x0 + 14.3, y + 2.0, x0 + 9.2, y + 3.45))
    return svg((0, 0, 14, 256), body, defs)


# ================================================================ menus (docs/port.md, "Inventário dos menus")
#
# Desenhos próprios para as peças dos menus: o painel (0x03000070, em nove pedaços quando esticado), as peças
# de 0x03000042 (botão, rádio, setas, caixas de marcar, destaque, barra de volume), o 007 e o canto do
# resultado (0x03000140, 0x0300014c) e os símbolos de botão dos textos ("~A"...: o conjunto do PS2 e o do
# Xbox; as teclas do teclado são desenhadas pelo renderizador). A imagem do painel da pausa (0x030000db) fica
# sem substituta: o jogo a reescreve a cada quadro (o fundo animado, platform/psi_woman.h).

def hexc(rgb):
    return "#%02x%02x%02x" % rgb


def rr_path(x0, y0, x1, y1, rtl, rtr, rbr, rbl):
    """Retângulo de cantos arredondados com raio próprio em cada canto (arcos de círculo)."""
    return ("M%.2f,%.2f L%.2f,%.2f A%.2f,%.2f 0 0 1 %.2f,%.2f L%.2f,%.2f A%.2f,%.2f 0 0 1 %.2f,%.2f "
            "L%.2f,%.2f A%.2f,%.2f 0 0 1 %.2f,%.2f L%.2f,%.2f A%.2f,%.2f 0 0 1 %.2f,%.2f Z" % (
                x0 + rtl, y0, x1 - rtr, y0, rtr, rtr, x1, y0 + rtr, x1, y1 - rbr, rbr, rbr, x1 - rbr, y1,
                x0 + rbl, y1, rbl, rbl, x0, y1 - rbl, x0, y0 + rtl, rtl, rtl, x0 + rtl, y0))


def stops(*items):
    """(offset, cor, opacidade) -> <stop>s."""
    out = ""
    for it in items:
        off, col = it[0], it[1]
        op = it[2] if len(it) > 2 else 1.0
        out += '<stop offset="%g" stop-color="%s" stop-opacity="%g"/>\n' % (off, col, op)
    return out


def lin(gid, x1, y1, x2, y2, *items):
    return ('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="%g" y1="%g" x2="%g" y2="%g">\n%s'
            '</linearGradient>\n' % (gid, x1, y1, x2, y2, stops(*items)))


def rad(gid, cx, cy, r, *items):
    return ('<radialGradient id="%s" gradientUnits="userSpaceOnUse" cx="%g" cy="%g" r="%g">\n%s'
            '</radialGradient>\n' % (gid, cx, cy, r, stops(*items)))


# ---------------------------------------------------------------- o painel (0x03000070, 512 x 256)

PANEL_OUT = (5.0, 5.0, 506.0, 250.5, 56.0, 42.0, 46.0, 56.0)    # borda de fora
PANEL_GLASS = (10.0, 38.5, 502.0, 222.0, 3.0, 9.0, 44.0, 48.0)  # o vidro
PANEL_BAND = 37.0  # a linha vermelha sob a faixa das abas


def panel_svg():
    """O painel da pausa: moldura cor de bronze com a faixa dourada das abas em cima, a barra de ajuda
    laranja embaixo (escura à direita), um fio vermelho por fora e o vidro vermelho escuro translúcido no
    meio. Coordenadas de texel (viewBox = centros de 0..511 x 0..255): os cortes de psi_2d_nine_slice
    (64, 72, 64, 80 texels) caem nos trechos retos."""
    out = rr_path(*PANEL_OUT)
    glass = rr_path(*PANEL_GLASS)
    defs = ""
    # A moldura (anel de fora menos o vidro): laranja com o brilho no meio e marrom à direita, mais escura
    # embaixo.
    defs += lin("frame", 5, 0, 506, 0, (0, "#b0461a"), (0.28, "#cc7230"), (0.5, "#e3ac6c"), (0.7, "#b8662e"),
                (0.88, "#73351a"), (1, "#4e220e"))
    defs += lin("shade", 0, 200, 0, 251, (0, "#000000", 0), (0.55, "#000000", 0.08), (1, "#000000", 0.42))
    # A faixa das abas: bronze dourado de cima para baixo, com sombra nas pontas e um brilho à direita.
    defs += lin("band", 0, 5, 0, PANEL_BAND, (0, "#5c2610"), (0.18, "#94461e"), (0.5, "#c07a3e"), (0.82, "#d8a264"),
                (1, "#b5743e"))
    defs += lin("bandx", 5, 0, 506, 0, (0, "#000000", 0.32), (0.22, "#000000", 0.05), (0.55, "#ffffff", 0.10),
                (0.75, "#ffffff", 0.0), (1, "#000000", 0.30))
    defs += rad("bandhi", 300, 14, 70, (0, "#fff4dc", 0.32), (1, "#fff4dc", 0))
    # O vidro: vermelho escuro translúcido (2/3, como o alfa 0x55 da textura), mais escuro à esquerda.
    defs += lin("glass", 10, 0, 502, 0, (0, "#140302", 0.72), (0.1, "#380906", 0.68), (0.45, "#6a150d", 0.66),
                (0.75, "#5c130c", 0.66), (1, "#360907", 0.7))
    defs += rad("glasshi", 330, 110, 210, (0, "#8c2c18", 0.18), (0.6, "#8c2c18", 0.06), (1, "#8c2c18", 0))
    defs += lin("glasstop", 0, 38, 0, 222, (0, "#ffffff", 0.06), (0.25, "#ffffff", 0), (1, "#000000", 0.18))
    defs += lin("rim", 0, 5, 0, 251, (0, "#8a2410"), (0.5, "#a02c14"), (1, "#5c1406"))
    defs += rad("barhi", 260, 236, 150, (0, "#fff0d8", 0.30), (1, "#fff0d8", 0))
    defs += '<clipPath id="outclip"><path d="%s"/></clipPath>\n' % out
    defs += '<clipPath id="bandclip"><rect x="0" y="0" width="512" height="%g"/></clipPath>\n' % PANEL_BAND
    defs += '<clipPath id="barclip"><rect x="0" y="222" width="512" height="40"/></clipPath>\n'
    body = '<path d="%s %s" fill="url(#frame)" fill-rule="evenodd"/>\n' % (out, glass)
    body += '<path d="%s %s" fill="url(#shade)" fill-rule="evenodd"/>\n' % (out, glass)
    body += '<g clip-path="url(#outclip)">\n'
    body += '<g clip-path="url(#bandclip)">\n'
    body += '<rect x="0" y="0" width="512" height="%g" fill="url(#band)"/>\n' % PANEL_BAND
    body += '<rect x="0" y="0" width="512" height="%g" fill="url(#bandx)"/>\n' % PANEL_BAND
    body += '<ellipse cx="300" cy="14" rx="90" ry="12" fill="url(#bandhi)"/>\n'
    body += '</g>\n'
    body += '<g clip-path="url(#barclip)"><ellipse cx="260" cy="236" rx="170" ry="11" fill="url(#barhi)"/></g>\n'
    body += '</g>\n'
    # O fio vermelho sob a faixa e a sombra fina do vidro.
    body += ('<g clip-path="url(#outclip)"><line x1="0" y1="%g" x2="512" y2="%g" stroke="#c63e18" stroke-width="1.7"/>'
             '</g>\n' % (PANEL_BAND, PANEL_BAND))
    body += '<path d="%s" fill="url(#glass)"/>\n' % glass
    body += '<path d="%s" fill="url(#glasshi)"/>\n' % glass
    body += '<path d="%s" fill="url(#glasstop)"/>\n' % glass
    body += '<path d="%s" fill="none" stroke="#1c0503" stroke-opacity="0.9" stroke-width="1.3"/>\n' % glass
    # Por fora: o fio vermelho escuro e um realce fino logo dentro dele.
    body += '<path d="%s" fill="none" stroke="url(#rim)" stroke-width="2.2"/>\n' % out
    inner = rr_path(PANEL_OUT[0] + 1.6, PANEL_OUT[1] + 1.6, PANEL_OUT[2] - 1.6, PANEL_OUT[3] - 1.6,
                    PANEL_OUT[4] - 1.6, PANEL_OUT[5] - 1.6, PANEL_OUT[6] - 1.6, PANEL_OUT[7] - 1.6)
    body += '<path d="%s" fill="none" stroke="#ffd8a8" stroke-opacity="0.22" stroke-width="0.8"/>\n' % inner
    return svg((0, 0, 511, 255), body, defs)


# ---------------------------------------------------------------- 0x03000042 (64 x 64): botões e setas

ORANGE = ("#f4a040", "#e07818", "#a24c08")


def menu_button_svg():
    """[0, 0, 16, 16]: o quadrado cinza de cantos arredondados com borda laranja (o cursor das barras usa as
    linhas de cima e de baixo e o meio esticado)."""
    defs = lin("g", 0, 0, 0, 15, (0, "#e2e2e6"), (0.45, "#c4c4ca"), (1, "#8e8e96"))
    body = '<rect x="0.7" y="0.7" width="14.1" height="14.1" rx="3" ry="3" fill="url(#g)" stroke="#e8801c" stroke-width="1.3"/>\n'
    body += '<rect x="1.8" y="1.8" width="11.9" height="5" rx="2" ry="2" fill="#ffffff" fill-opacity="0.25"/>\n'
    return svg((0, 0, 16, 16), body, defs)


def menu_radio_svg():
    """[0, 16, 16, 16]: o quadrado cinza com a argola (rádio)."""
    defs = lin("g", 0, 0, 0, 15, (0, "#d8d8dc"), (0.5, "#a6a6ac"), (1, "#6e6e76"))
    body = '<rect x="0.7" y="0.7" width="14.1" height="14.1" rx="3" ry="3" fill="url(#g)" stroke="#2a2a2e" stroke-width="1.3"/>\n'
    body += '<circle cx="7.75" cy="7.75" r="3.9" fill="none" stroke="#e8e8ec" stroke-width="1.4"/>\n'
    body += '<circle cx="7.75" cy="7.75" r="3.9" fill="none" stroke="#3a3a40" stroke-width="0.5"/>\n'
    return svg((0, 0, 16, 16), body, defs)


def triangle_svg(direction, size=16.0):
    """Seta laranja com contorno escuro, apontando para cima/baixo/esquerda/direita (o cursor das listas é a
    da direita)."""
    s = size
    pts = {"up": [(s / 2, 1.0), (s - 1.0, s - 1.5), (1.0, s - 1.5)],
           "down": [(1.0, 1.5), (s - 1.0, 1.5), (s / 2, s - 1.0)],
           "left": [(1.0, s / 2), (s - 1.5, 1.0), (s - 1.5, s - 1.0)],
           "right": [(1.5, 1.0), (s - 1.0, s / 2), (1.5, s - 1.0)]}[direction]
    d = "M" + " L".join("%.2f,%.2f" % p for p in pts) + " Z"
    axis = {"up": (0, 0, 0, s), "down": (0, 0, 0, s), "left": (0, 0, s, 0), "right": (0, 0, s, 0)}[direction]
    defs = lin("o", axis[0], axis[1], axis[2], axis[3], (0, ORANGE[0]), (0.5, ORANGE[1]), (1, ORANGE[2]))
    body = '<path d="%s" fill="url(#o)" stroke="#1e0c00" stroke-width="1.1" stroke-linejoin="round"/>\n' % d
    return svg((0, 0, s, s), body, defs)


def check_box_svg(on):
    """As caixas de marcar (os quadradinhos laranja e cinza, [25, 19] e [31, 19]): ligada laranja com o
    visto, desligada cinza vazia."""
    if on:
        defs = lin("g", 0, 0, 0, 12, (0, ORANGE[0]), (0.55, ORANGE[1]), (1, ORANGE[2]))
        body = '<rect x="0.8" y="0.8" width="10.4" height="10.4" rx="2.2" ry="2.2" fill="url(#g)" stroke="#3a1600" stroke-width="0.9"/>\n'
        body += ('<path d="M3,6.2 L5.1,8.4 L9.2,3.4" fill="none" stroke="#fff6e8" stroke-width="1.6" '
                 'stroke-linecap="round" stroke-linejoin="round"/>\n')
    else:
        defs = lin("g", 0, 0, 0, 12, (0, "#7a7a80"), (1, "#3c3c42"))
        body = '<rect x="0.8" y="0.8" width="10.4" height="10.4" rx="2.2" ry="2.2" fill="url(#g)" stroke="#cfcfd4" stroke-width="0.9"/>\n'
        body += '<rect x="2.2" y="2.2" width="7.6" height="7.6" rx="1.4" ry="1.4" fill="#1c1c20" fill-opacity="0.55"/>\n'
    return svg((0, 0, 12, 12), body, defs)


def highlight_svg():
    """O destaque de uma linha (o texel branco [38, 19] esticado; a cor vem do sprite): claro em cima, um
    fio de brilho e mais fraco embaixo; nada muda na horizontal, então esticar não deforma."""
    defs = lin("g", 0, 0, 0, 24, (0, "#ffffff", 1.0), (0.12, "#ffffff", 0.82), (0.6, "#ffffff", 0.66),
               (1, "#ffffff", 0.48))
    body = '<rect x="0" y="0" width="64" height="24" fill="url(#g)"/>\n'
    body += '<rect x="0" y="0.6" width="64" height="0.9" fill="#ffffff"/>\n'
    return svg((0, 0, 64, 24), body, defs)


def volume_svg():
    """A barra de volume ([0, 32, 64, 8]): oito segmentos do pêssego claro ao laranja, com espaço entre
    eles."""
    body = ""
    ramp = [(250, 222, 196), (246, 206, 168), (244, 190, 140), (240, 172, 112), (236, 154, 86), (234, 136, 62),
            (230, 120, 40), (226, 104, 22)]
    for i, c in enumerate(ramp):
        body += ('<rect x="%.2f" y="0.8" width="6.6" height="6.4" rx="1" ry="1" fill="%s" stroke="#5a2a08" '
                 'stroke-opacity="0.6" stroke-width="0.5"/>\n' % (0.4 + i * 8.0, hexc(c)))
    return svg((0, 0, 64, 8), body)


# ---------------------------------------------------------------- resultado (0x03000140 e 0x0300014c)

def results_007_svg():
    """O 007 com a arma (0x03000140, 256 x 128): letras grossas inclinadas cromadas e o cano saindo do 7,
    com o guarda-mato e o gatilho embaixo (desenho próprio)."""
    defs = lin("chrome", 0, 0, 0, 104, (0, "#ffffff"), (0.22, "#e2e2e2"), (0.46, "#a4a4a6"), (0.52, "#cfcfd1"),
               (0.78, "#f2f2f2"), (1, "#8e8e90"))
    defs += lin("barrel", 0, 6, 0, 20, (0, "#ffffff"), (0.4, "#d8d8da"), (0.7, "#9c9c9e"), (1, "#e8e8ea"))
    zero1 = ("M%s M%s" % (ellipse_d(30, 52, 27, 47), ellipse_d(30, 52, 11, 35)))
    zero2 = ("M%s M%s" % (ellipse_d(88, 52, 27, 47), ellipse_d(88, 52, 11, 35)))
    seven = "M120,6 L178,6 L178,21 L150,99 L126,99 L157,22 L120,22 Z"
    body = '<g transform="translate(25,0) skewX(-14)">\n'
    for d in (zero1, zero2):
        body += '<path d="%s" fill="url(#chrome)" fill-rule="evenodd" stroke="#3e3e40" stroke-width="1.3"/>\n' % d
    body += '<path d="%s" fill="url(#chrome)" stroke="#3e3e40" stroke-width="1.3"/>\n' % seven
    # A arma: o cano saindo do traço do 7, a mira, o guarda-mato e o gatilho.
    body += '<path d="M176,7 L226,7 C229,7 230,9 230,12 L230,16 C230,19 229,20 226,20 L176,20 Z" fill="url(#barrel)" stroke="#3e3e40" stroke-width="1.2"/>\n'
    body += '<rect x="221" y="3.5" width="4" height="4" fill="#d0d0d2" stroke="#3e3e40" stroke-width="0.8"/>\n'
    body += '<path d="M182,20 C182,31 196,33 199,22" fill="none" stroke="#3e3e40" stroke-width="4.2" stroke-linecap="round"/>\n'
    body += '<path d="M182,20 C182,31 196,33 199,22" fill="none" stroke="#d4d4d6" stroke-width="2.4" stroke-linecap="round"/>\n'
    body += '<path d="M189,20 C190,24 189,27 187,29" fill="none" stroke="#bcbcbe" stroke-width="1.6" stroke-linecap="round"/>\n'
    body += '</g>\n'
    return svg((0, 0, 255, 127), body, defs)


def ellipse_d(cx, cy, rx, ry):
    return ("%.2f,%.2f A%.2f,%.2f 0 1 0 %.2f,%.2f A%.2f,%.2f 0 1 0 %.2f,%.2f Z"
            % (cx - rx, cy, rx, ry, cx + rx, cy, rx, ry, cx - rx, cy))


def results_corner_svg():
    """O canto escuro do resultado (0x0300014c, recorte [1, 1, 256, 256]): a área preta embaixo à direita sob
    um arco (um quarto de elipse, vertical na direita e deitado na esquerda) com um fio cor de cobre, e a
    ponta esquerda sumindo em degradê."""
    # Coordenadas de centro de texel (1..256): o arco vai de (256, 45) a (1, 186).
    arc = "M256,45 A255,141 0 0 1 1,186"
    region = arc + " L1,256 L256,256 Z"
    defs = lin("fade", 1, 0, 70, 0, (0, "#040404", 0), (0.35, "#040404", 0.7), (1, "#040404", 1))
    defs += lin("deep", 0, 40, 0, 256, (0, "#141414", 1), (1, "#000000", 1))
    defs += lin("rim", 1, 0, 256, 0, (0, "#c47a48", 0), (0.25, "#c47a48", 0.8), (0.8, "#e0a070", 1), (1, "#f0b888", 1))
    defs += '<clipPath id="r"><path d="%s"/></clipPath>\n' % region
    body = '<g clip-path="url(#r)">\n'
    body += '<rect x="70" y="0" width="200" height="260" fill="url(#deep)"/>\n'
    body += '<rect x="0" y="0" width="70" height="260" fill="url(#fade)"/>\n'
    body += '</g>\n'
    body += '<path d="%s" fill="none" stroke="url(#rim)" stroke-width="1.6"/>\n' % arc
    return svg((1, 1, 255, 255), body, defs)


# ---------------------------------------------------------------- símbolos de botão

# Letras de traço (caixa de altura 1, y para baixo), para os rótulos dos botões: o Direct2D não desenha
# <text> de SVG.
def _arc(cx, cy, rx, ry, a0, a1, n=10):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


GLYPHS = {
    "A": (0.9, [[(0, 1), (0.45, 0), (0.9, 1)], [(0.2, 0.62), (0.7, 0.62)]]),
    "B": (0.75, [[(0, 1), (0, 0), (0.42, 0)] + _arc(0.42, 0.25, 0.27, 0.25, -90, 90) + [(0, 0.5), (0.47, 0.5)]
                 + _arc(0.47, 0.75, 0.28, 0.25, -90, 90) + [(0, 1)]]),
    "X": (0.85, [[(0, 0), (0.85, 1)], [(0.85, 0), (0, 1)]]),
    "Y": (0.85, [[(0, 0), (0.425, 0.5), (0.85, 0)], [(0.425, 0.5), (0.425, 1)]]),
    "L": (0.62, [[(0, 0), (0, 1), (0.62, 1)]]),
    "R": (0.78, [[(0, 1), (0, 0), (0.45, 0)] + _arc(0.45, 0.26, 0.28, 0.26, -90, 90) + [(0, 0.52)],
                 [(0.36, 0.52), (0.78, 1)]]),
    "T": (0.8, [[(0, 0), (0.8, 0)], [(0.4, 0), (0.4, 1)]]),
    "S": (0.78, [[(0.76, 0.14)] + _arc(0.39, 0.25, 0.37, 0.25, -30, -270, 14) + _arc(0.39, 0.75, 0.37, 0.25, -90, 150, 14)]),
    "1": (0.5, [[(0.05, 0.2), (0.38, 0), (0.38, 1)]]),
    "2": (0.72, [_arc(0.36, 0.28, 0.34, 0.28, -160, 30, 10) + [(0.02, 1), (0.72, 1)]]),
}


def text_paths(text, cx, cy, h, gap=0.18):
    """Os traços de "text" centrados em (cx, cy) com altura h; devolve o atributo d."""
    width = sum(GLYPHS[c][0] for c in text) + gap * (len(text) - 1)
    x = cx - width * h / 2.0
    y = cy - h / 2.0
    d = ""
    for c in text:
        w, strokes = GLYPHS[c]
        for st in strokes:
            d += "M" + " L".join("%.2f,%.2f" % (x + px * h, y + py * h) for px, py in st) + " "
        x += (w + gap) * h
    return d.strip()


def letters(text, cx, cy, h, colour, width):
    return ('<path d="%s" fill="none" stroke="%s" stroke-width="%.2f" stroke-linecap="round" '
            'stroke-linejoin="round"/>\n' % (text_paths(text, cx, cy, h), colour, width))


def round_button(defs_id, cx, cy, r, top, bottom, rim, rim_op=0.7):
    defs = rad(defs_id, cx - r * 0.25, cy - r * 0.35, r * 1.35, (0, top), (1, bottom))
    body = '<circle cx="%g" cy="%g" r="%g" fill="url(#%s)" stroke="%s" stroke-opacity="%g" stroke-width="%g"/>\n' % (
        cx, cy, r - 0.5, defs_id, rim, rim_op, max(0.6, r * 0.09))
    body += '<ellipse cx="%g" cy="%g" rx="%g" ry="%g" fill="#ffffff" fill-opacity="0.13"/>\n' % (
        cx, cy - r * 0.42, r * 0.62, r * 0.32)
    return defs, body


def ps2_face_svg(kind):
    """Os botões de face do PS2: botão escuro redondo com o símbolo colorido."""
    defs, body = round_button("b", 10, 10, 9.6, "#4c4c52", "#0c0c0e", "#a8a8b0")
    if kind == "cross":
        body += ('<path d="M6.2,6.2 L13.8,13.8 M13.8,6.2 L6.2,13.8" stroke="#a4b4ee" stroke-width="1.9" '
                 'stroke-linecap="round"/>\n')
    elif kind == "circle":
        body += '<circle cx="10" cy="10" r="4.3" fill="none" stroke="#f0603c" stroke-width="1.8"/>\n'
    elif kind == "triangle":
        body += ('<path d="M10,5.4 L14.6,13.4 L5.4,13.4 Z" fill="none" stroke="#3cc8aa" stroke-width="1.7" '
                 'stroke-linejoin="round"/>\n')
    elif kind == "square":
        body += '<rect x="6.2" y="6.2" width="7.6" height="7.6" fill="none" stroke="#ee88cc" stroke-width="1.7"/>\n'
    return svg((0, 0, 20, 20), body, defs)


def shoulder_svg(label, dark=False, trigger=False):
    """Os gatilhos L1/L2/R1/R2 (PS2: cinza com o rótulo branco) e LB/RB/LT/RT (Xbox: escuro; os gatilhos
    com a ponta de cima arredondada)."""
    top, bottom = ("#45454b", "#121214") if dark else ("#76767c", "#2c2c30")
    defs = lin("g", 0, 1, 0, 15, (0, top), (1, bottom))
    if trigger:
        shape = "M3,15 L3,6 C3,2.5 6,1 11,1 C16,1 19,2.5 19,6 L19,15 Z"
    else:
        shape = rr_path(1.0, 2.5, 21.0, 13.5, 3.5, 3.5, 3.5, 3.5)
    body = '<path d="%s" fill="url(#g)" stroke="#c4c4ca" stroke-opacity="0.75" stroke-width="0.9"/>\n' % shape
    body += letters(label, 11, 8.6, 6.2, "#f4f4f6", 1.25)
    return svg((0, 0, 22, 16), body, defs)


def stick_svg(label, dark=False):
    """Os analógicos (PS2: cinza; Xbox: escuro), com a letra."""
    top, bottom = ("#3e3e44", "#0a0a0c") if dark else ("#86868c", "#2a2a2e")
    defs, body = round_button("b", 10, 10, 9.6, top, bottom, "#c8c8ce")
    body += '<circle cx="10" cy="10" r="6.2" fill="none" stroke="#000000" stroke-opacity="0.35" stroke-width="1"/>\n'
    body += letters(label, 10, 10, 6.4, "#f4f4f6", 1.3)
    return svg((0, 0, 20, 20), body, defs)


def dpad_svg(lit, xbox=False):
    """O direcional: os braços acesos (dourados no PS2, brancos no Xbox) e os apagados."""
    on = ("#ffe08a", "#d8a020", "#7a5408") if not xbox else ("#ffffff", "#d8d8dc", "#8a8a90")
    off = ("#5a5a60", "#2a2a2e", "#111113")
    defs = ""
    body = ""
    # A pétala de cima (as outras giradas em volta do centro): retângulo arredondado com a ponta no centro.
    petal = "M7.1,1.6 C7.1,0.9 7.5,0.6 8.2,0.6 L11.8,0.6 C12.5,0.6 12.9,0.9 12.9,1.6 L12.9,6.6 L10,8.9 L7.1,6.6 Z"
    turns = {"up": 0, "right": 90, "down": 180, "left": 270}
    if xbox:
        defs += rad("bg", 10, 10, 10, (0, "#2a2a2e"), (1, "#0a0a0c"))
        body += '<circle cx="10" cy="10" r="9.6" fill="url(#bg)" stroke="#8a8a90" stroke-opacity="0.6" stroke-width="0.7"/>\n'
        petal = "M7.6,3 C7.6,2.4 8,2.1 8.6,2.1 L11.4,2.1 C12,2.1 12.4,2.4 12.4,3 L12.4,6.9 L10,8.9 L7.6,6.9 Z"
    defs += lin("lit", 0, 0.6, 0, 8.9, (0, on[0]), (0.5, on[1]), (1, on[2]))
    defs += lin("dim", 0, 0.6, 0, 8.9, (0, off[0]), (0.5, off[1]), (1, off[2]))
    for name, angle in turns.items():
        fill = "lit" if name in lit else "dim"
        body += ('<path d="%s" transform="rotate(%d 10 10)" fill="url(#%s)" stroke="#000000" stroke-opacity="0.7" '
                 'stroke-width="0.6" stroke-linejoin="round"/>\n' % (petal, angle, fill))
    return svg((0, 0, 20, 20), body, defs)


def start_svg():
    """O Start do PS2: o triângulo cinza deitado."""
    defs = lin("g", 0, 0, 0, 13, (0, "#c8c8ce"), (1, "#5c5c62"))
    body = ('<path d="M3,1.5 L23,6.5 L3,11.5 Z" fill="url(#g)" stroke="#1e1e22" stroke-width="0.9" '
            'stroke-linejoin="round"/>\n')
    return svg((0, 0, 26, 13), body, defs)


def select_svg():
    """O Select do PS2: o retângulo cinza achatado."""
    defs = lin("g", 0, 3, 0, 9, (0, "#d0d0d6"), (1, "#6a6a70"))
    body = '<rect x="2" y="3" width="12" height="6" rx="1.6" ry="1.6" fill="url(#g)" stroke="#1e1e22" stroke-width="0.8"/>\n'
    return svg((0, 0, 16, 12), body, defs)


def check_svg():
    """O visto dos objetivos cumpridos."""
    body = ('<path d="M2.5,6.5 L6.2,10 L13.5,2" fill="none" stroke="#203020" stroke-width="2.8" stroke-linecap="round" '
            'stroke-linejoin="round"/>\n')
    body += ('<path d="M2.5,6.5 L6.2,10 L13.5,2" fill="none" stroke="#e8f4e4" stroke-width="1.6" stroke-linecap="round" '
             'stroke-linejoin="round"/>\n')
    return svg((0, 0, 16, 12), body)


def dot_svg():
    """O ponto dos objetivos."""
    defs = rad("g", 3.0, 2.8, 3.6, (0, "#fff0e0"), (1, "#f0b888"))
    return svg((0, 0, 7, 7), '<circle cx="3.5" cy="3.5" r="2.9" fill="url(#g)"/>\n', defs)


XBOX_FACE = {"A": "#6cc24a", "B": "#ea4a3e", "X": "#3e8ee8", "Y": "#f2c232"}


def xbox_face_svg(letter):
    """Os botões de face do Xbox: escuros, com a letra e o aro na cor do botão."""
    colour = XBOX_FACE[letter]
    defs, body = round_button("b", 10, 10, 9.6, "#38383e", "#060608", colour, 0.85)
    body += letters(letter, 10, 10, 8.4, colour, 1.9)
    return svg((0, 0, 20, 20), body, defs)


def xbox_menu_svg(kind):
    """Menu (as três linhas) e View (os dois retângulos) do Xbox."""
    defs, body = round_button("b", 10, 10, 9.6, "#38383e", "#060608", "#c8c8ce")
    if kind == "menu":
        for y in (6.6, 10, 13.4):
            body += '<line x1="6" y1="%g" x2="14" y2="%g" stroke="#f4f4f6" stroke-width="1.4" stroke-linecap="round"/>\n' % (y, y)
    else:
        body += '<rect x="5.2" y="5.6" width="6.4" height="5.4" rx="1" ry="1" fill="none" stroke="#f4f4f6" stroke-width="1.2"/>\n'
        body += '<rect x="8.4" y="9" width="6.4" height="5.4" rx="1" ry="1" fill="#1a1a1e" stroke="#f4f4f6" stroke-width="1.2"/>\n'
    return svg((0, 0, 20, 20), body, defs)


def menu_files():
    """Os SVG dos menus e dos símbolos de botão (nome -> conteúdo)."""
    files = {
        "menu_panel.svg": panel_svg(),
        "menu_button.svg": menu_button_svg(),
        "menu_radio.svg": menu_radio_svg(),
        "menu_arrow_up.svg": triangle_svg("up"),
        "menu_arrow_down.svg": triangle_svg("down"),
        "menu_arrow_left.svg": triangle_svg("left"),
        "menu_arrow_right.svg": triangle_svg("right"),
        "menu_bigarrow_right.svg": triangle_svg("right", 23.0),
        "menu_bigarrow_left.svg": triangle_svg("left", 23.0),
        "menu_check_on.svg": check_box_svg(True),
        "menu_check_off.svg": check_box_svg(False),
        "menu_highlight.svg": highlight_svg(),
        "menu_volume.svg": volume_svg(),
        "results_007.svg": results_007_svg(),
        "results_corner.svg": results_corner_svg(),
        "ps2_cross.svg": ps2_face_svg("cross"),
        "ps2_circle.svg": ps2_face_svg("circle"),
        "ps2_triangle.svg": ps2_face_svg("triangle"),
        "ps2_square.svg": ps2_face_svg("square"),
        "ps2_l1.svg": shoulder_svg("L1"),
        "ps2_l2.svg": shoulder_svg("L2"),
        "ps2_r1.svg": shoulder_svg("R1"),
        "ps2_r2.svg": shoulder_svg("R2"),
        "ps2_lstick.svg": stick_svg("L"),
        "ps2_rstick.svg": stick_svg("R"),
        "ps2_dpad_v.svg": dpad_svg(("up", "down")),
        "ps2_dpad_h.svg": dpad_svg(("left", "right")),
        "ps2_dpad.svg": dpad_svg(("up", "down", "left", "right")),
        "ps2_start.svg": start_svg(),
        "ps2_select.svg": select_svg(),
        "mark_check.svg": check_svg(),
        "mark_dot.svg": dot_svg(),
        "xbox_a.svg": xbox_face_svg("A"),
        "xbox_b.svg": xbox_face_svg("B"),
        "xbox_x.svg": xbox_face_svg("X"),
        "xbox_y.svg": xbox_face_svg("Y"),
        "xbox_lb.svg": shoulder_svg("LB", dark=True),
        "xbox_rb.svg": shoulder_svg("RB", dark=True),
        "xbox_lt.svg": shoulder_svg("LT", dark=True, trigger=True),
        "xbox_rt.svg": shoulder_svg("RT", dark=True, trigger=True),
        "xbox_ls.svg": stick_svg("L", dark=True),
        "xbox_rs.svg": stick_svg("R", dark=True),
        "xbox_menu.svg": xbox_menu_svg("menu"),
        "xbox_view.svg": xbox_menu_svg("view"),
        "xbox_dpad_v.svg": dpad_svg(("up", "down"), xbox=True),
        "xbox_dpad_h.svg": dpad_svg(("left", "right"), xbox=True),
        "xbox_dpad.svg": dpad_svg(("up", "down", "left", "right"), xbox=True),
    }
    return files


def main():
    if "--check" in sys.argv:
        sys.exit(0 if check() else 1)
    os.makedirs(OUT, exist_ok=True)
    files = {"crosshair.svg": crosshair_svg(), "band_left.svg": band_svg("left"), "band_mid.svg": band_svg("mid"),
             "band_right.svg": band_svg("right"), "bullets_column.svg": bullets_svg(), "health_ring.svg": ring_svg()}
    for s in sorted(BLADES):
        files["health_blade_%d.svg" % s] = blade_svg(s)
    files.update(menu_files())
    for name, text in files.items():
        with open(os.path.join(OUT, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("%d SVG em %s" % (len(files), os.path.normpath(OUT)))


if __name__ == "__main__":
    main()
