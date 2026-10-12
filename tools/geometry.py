"""Converte a geometria do PS2 (blocos 0x2D dos itens de mapa) em glTF binário (.glb).

Cada bloco 0x2D (parsemap_block_PS2_GFX) contém:

    +0x04  uint32 gfx_header_offset            relativo ao início do bloco
    gfx_header:
        int32  box_count
        uint32 boxes_offset                     array de _GLIST_BOX (0x38 bytes cada)
        uint32 extra_offset                     dados de skinning/quebra (ainda não usado)

    _GLIST_BOX (0x38 bytes), ver RecurseAndDrawBoxes / DrawThisBox:
        +0x00  float  bbox[6]                   mínimo e máximo
        +0x18  int32  child[2]                  -1 = folha
        +0x20  uint32 dma_offset                cadeia de DMA chamada com uma tag CALL
        +0x24  uint32 texref_offset             offsets de tags REF de textura, fim = 0xFFFFFFFF
        +0x30  uint32 vertex_count
        +0x34  uint32 flags

Os rostos (bloco 0x2B da mesma entidade, ver tools/morph.py) saem como alvos de morph glTF (um por alvo do
jogo, 22), com os pesos em 0.

A cadeia de DMA alterna:
    REF  qwc=4, códigos VIF nulos   addr = índice da textura no item (trocado na carga
                                    pelo pacote de upload da textura)
    CNT  com códigos VIF:
        STCYCL 4,4 ; UNPACK V4-32 x1         -> GIF tag (tristrip, NLOOP = vértices)
        UNPACK V2-32 xN                       -> UV (float)
        STCYCL 3,1 ; UNPACK V3-32 xN          -> posição (float, coordenadas do mundo)
                     UNPACK V4-8  xN          -> normal (com sinal, /127); w = -1 inicia nova tira
                     UNPACK V4-8u xN          -> cor RGBA (0x80 = 1.0)
        MSCNT                                 -> o microprograma da VU1 desenha o lote
    RET

Modelos com skinning (itens tipo 2) trazem os vértices num reservatório que o SkinIt
transforma a cada quadro; as tags REF da cadeia leem o resultado. O exportador emula
as escritas na memória da VU1 e usa a entrada do SkinIt, que já está na pose de repouso.

Uso:
    python tools/geometry.py level <pacote 070xxxxx.bin> [--out arquivo.glb]   fase montada (bloco 0x1A)
    python tools/geometry.py level <pacote> --raw                              só o mapa principal, sem posicionar
    python tools/geometry.py item  <item.bin> [--out arquivo.glb]              um item avulso, sem posicionar
    python tools/geometry.py characters extracted/files/070*.bin              personagens (tipo 2) na pose de repouso
"""
import argparse
import collections
import json
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
from levelpak import parse as parse_pak  # noqa: E402
from mapfile import read_blocks, textures  # noqa: E402

GFX_BLOCK = 0x2D
MORPH_BLOCK = 0x2B
DMA_CNT, DMA_REF, DMA_RET, DMA_END = 1, 3, 6, 7
VIF_MSCAL, VIF_MSCALF, VIF_MSCNT = 0x14, 0x15, 0x17
VIF_STMASK, VIF_STROW, VIF_STCOL, VIF_MPG, VIF_DIRECT, VIF_DIRECTHL = 0x20, 0x30, 0x31, 0x4A, 0x50, 0x51


class Batch:
    """Um lote enviado à VU1: uma ou mais tiras de triângulos com a mesma textura."""

    def __init__(self, texture):
        self.texture = texture
        self.textured = False
        self.uv = []
        self.xyz = []
        self.normal = []
        self.color = []
        self.joints = []   # só em modelos com skinning: índices de osso do esqueleto
        self.weights = []
        self.morph = []    # só em modelos com skinning: índice de morph de cada vértice (-1 = nenhum)

    def complete(self):
        n = len(self.xyz)
        return n >= 3 and len(self.normal) == n and len(self.color) == n and (not self.uv or len(self.uv) == n)


def walk_chain(blk, dma, stats):
    """Percorre a cadeia de DMA de uma caixa e devolve os lotes completos."""
    batches, pos, texture = [], dma, None
    cur = Batch(texture)
    for _ in range(1_000_000):
        lo = struct.unpack_from("<Q", blk, pos)[0]
        qwc, tid, addr = lo & 0xFFFF, (lo >> 28) & 7, (lo >> 32) & 0x7FFFFFFF
        vif0, vif1 = struct.unpack_from("<II", blk, pos + 8)

        if tid == DMA_REF:
            if vif0 == 0 and vif1 == 0:
                texture = addr
                cur.texture = texture
            else:
                stats["tags_ref_de_vertices_ignoradas"] += 1
            pos += 16
            continue
        if tid not in (DMA_CNT, DMA_RET, DMA_END):
            raise ValueError(f"tag DMA inesperada {tid} em 0x{pos:x}")

        p, end = pos + 8, pos + 16 + qwc * 16
        while p < end:
            code = struct.unpack_from("<I", blk, p)[0]
            p += 4
            cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
            if cmd & 0x60 == 0x60:  # UNPACK
                vn, vl = (cmd >> 2) & 3, cmd & 3
                n = num or 256
                size = ((32 >> vl) * (vn + 1) * n + 31) // 32 * 4
                usn = (imm >> 14) & 1
                if vn == 3 and vl == 0 and n == 1:  # GIF tag
                    gif = struct.unpack_from("<Q", blk, p)[0]
                    prim = (gif >> 47) & 0x7FF
                    cur.textured = bool((prim >> 4) & 1)
                elif vn == 1 and vl == 0:  # V2-32: UV
                    cur.uv = [struct.unpack_from("<2f", blk, p + i * 8) for i in range(n)]
                elif vn == 2 and vl == 0:  # V3-32: posição
                    cur.xyz = [struct.unpack_from("<3f", blk, p + i * 12) for i in range(n)]
                elif vn == 3 and vl == 2 and not usn:  # V4-8 com sinal: normal + flag de tira
                    cur.normal = [struct.unpack_from("<4b", blk, p + i * 4) for i in range(n)]
                elif vn == 3 and vl == 2 and usn:  # V4-8 sem sinal: cor
                    cur.color = [struct.unpack_from("<4B", blk, p + i * 4) for i in range(n)]
                else:
                    stats[f"unpack_desconhecido_V{vn + 1}-{32 >> vl}"] += 1
                p += size
            elif cmd in (VIF_MSCNT, VIF_MSCAL, VIF_MSCALF):
                if cur.complete():
                    batches.append(cur)
                elif cur.xyz or cur.color:
                    stats["lotes_incompletos"] += 1
                cur = Batch(texture)
            elif cmd == VIF_STMASK:
                p += 4
            elif cmd in (VIF_STROW, VIF_STCOL):
                p += 16
            elif cmd == VIF_MPG:
                p += (num or 256) * 8
            elif cmd in (VIF_DIRECT, VIF_DIRECTHL):
                p += (imm or 65536) * 16
        if tid in (DMA_RET, DMA_END):
            return batches
        pos = end
    raise ValueError("cadeia de DMA sem RET")


# ---------------------------------------------------------------- modelos com skinning

class SkinVertex:
    __slots__ = ("pos", "weight", "normal", "bones", "morph")

    def __init__(self, pos, weight, normal, bones, morph=-1):
        self.pos, self.weight, self.normal, self.bones = pos, weight, normal, bones
        self.morph = morph  # elemento do bloco 0x2B onde começam os deltas do vértice (-1 = sem morph)


def read_skin_pool(blk, extra):
    """Entrada do SkinIt (0x0010bf30): estrutura extra do bloco 0x2D.

        u32 input_offset     pares de quadwords, um par por vértice
        u32 output_size      bytes; o buffer de saída tem o mesmo tamanho da entrada
        u32 patch_offset     índices de palavra da cadeia DMA cujo endereço recebe a base da saída
        u32 bones_offset     índices de osso (u8, fim = 0xFF) carregados na paleta da VU0

    Cada vértice de entrada:
        q0: f32 x, y, z, peso do primeiro osso (posição na pose de repouso, espaço do modelo)
        q1: 4 x i32. 16 bits altos de x, y, z = normal * 64; 16 bits baixos = índices de osso;
            w: 16 bits altos = índice de morph (-1 = nenhum), bit 15 = início de tira (sem desenho)
    """
    inp, out_size, _patch, bones_off = struct.unpack_from("<4I", blk, extra & ~3)
    base = inp & ~3
    verts = []
    for k in range(out_size // 32):
        x, y, z, w = struct.unpack_from("<4f", blk, base + k * 32)
        ix, iy, iz, iw = struct.unpack_from("<4i", blk, base + k * 32 + 16)
        normal = (ix >> 16, iy >> 16, iz >> 16, -1 if iw & 0x8000 else 0)
        verts.append(SkinVertex((x, y, z), w, normal, (ix & 0xFFFF, iy & 0xFFFF, iz & 0xFFFF), iw >> 16))
    palette, p = [], bones_off & ~3
    while blk[p] != 0xFF:
        palette.append(blk[p])
        p += 1
    return verts, palette


def _cycle(start, j, cl, wl):
    """Endereço de destino do j-ésimo elemento de um UNPACK com STCYCL (modo de pular)."""
    if wl <= cl:
        return start + (j // wl) * cl + (j % wl)
    return start + j


def _inline_value(blk, data_at, j, vn, vl, usn):
    if vn == 1 and vl == 0:
        return ("uv", struct.unpack_from("<2f", blk, data_at + j * 8))
    if vn == 2 and vl == 0:
        return ("xyz", struct.unpack_from("<3f", blk, data_at + j * 12))
    if vn == 3 and vl == 2:
        return ("col" if usn else "nrm", struct.unpack_from("<4B" if usn else "<4b", blk, data_at + j * 4))
    return ("raw", None)


def walk_chain_skinned(blk, dma, pool, palette, stats):
    """Emula as escritas da VIF1 na memória da VU1 para uma cadeia com vértices de skinning.

    As tags REF com UNPACK V4-32 leem do buffer de saída do SkinIt. O endereço delas é um
    deslocamento em bytes nesse buffer (31 bits com sinal; o CacheSkin soma a base na carga).
    Na memória da VU1, cada vértice ocupa 3 quadwords depois da GIF tag: posição, cor, normal.
    O buffer de saída tem, por vértice, posição e normal em sequência; as tags REF começam um
    quadword antes para que o padrão STCYCL 3,2 pule o espaço da cor.
    """
    mem = {}
    batches, pos, texture = [], dma, None
    cl, wl = 4, 4
    gif_addr = uv_addr = gif_info = None

    def finish():
        if gif_addr is None or gif_info is None:
            return
        nloop, textured = gif_info
        bt = Batch(texture)
        bt.textured = textured
        for i in range(nloop):
            xs, cs, ns = (mem.get(gif_addr + 1 + 3 * i + k) for k in range(3))
            if not (xs and ns and cs and xs[0] == "pool" and ns[0] == "pool" and cs[0] == "col"):
                stats["vertices_skin_incompletos"] += 1
                return
            vx, vn = xs[1], ns[1]
            if vx % 2 != 0 or vn % 2 != 1 or not (0 <= vx // 2 < len(pool)) or not (0 <= vn // 2 < len(pool)):
                stats["vertices_skin_desalinhados"] += 1
                return
            src = pool[vx // 2]
            bt.xyz.append(src.pos)
            bt.normal.append(pool[vn // 2].normal)
            a, b = src.bones[0] & 0x7FFF, src.bones[1] & 0x7FFF
            ja = palette[a] if a < len(palette) else 0
            jb = palette[b] if b < len(palette) else 0
            wa = min(max(src.weight, 0.0), 1.0)
            bt.joints.append((ja, jb, 0, 0))
            bt.weights.append((wa, 1.0 - wa, 0.0, 0.0))
            bt.morph.append(src.morph)
            bt.color.append(cs[1])
            if uv_addr is not None:
                uv = mem.get(uv_addr + i)
                bt.uv.append(uv[1] if uv and uv[0] == "uv" else (0.0, 0.0))
        if bt.complete():
            batches.append(bt)
            stats["lotes_skin"] += 1

    for _ in range(1_000_000):
        lo = struct.unpack_from("<Q", blk, pos)[0]
        qwc, tid, addr = lo & 0xFFFF, (lo >> 28) & 7, (lo >> 32) & 0x7FFFFFFF
        vif0, vif1 = struct.unpack_from("<II", blk, pos + 8)

        if tid == DMA_REF:
            if vif0 == 0 and vif1 == 0:
                texture = addr
            else:
                if addr & 0x40000000:
                    addr -= 0x80000000
                q0 = addr // 16
                for code in (vif0, vif1):
                    cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
                    if cmd == 0x01:
                        cl, wl = imm & 0xFF, (imm >> 8) & 0xFF
                    elif cmd & 0x60 == 0x60:
                        start = imm & 0x3FF
                        for j in range(num or 256):
                            mem[_cycle(start, j, cl, wl)] = ("pool", q0 + j)
            pos += 16
            continue
        if tid not in (DMA_CNT, DMA_RET, DMA_END):
            raise ValueError(f"tag DMA inesperada {tid} em 0x{pos:x}")

        p, end = pos + 8, pos + 16 + qwc * 16
        while p < end:
            code = struct.unpack_from("<I", blk, p)[0]
            p += 4
            cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
            if cmd & 0x60 == 0x60:
                vn, vl = (cmd >> 2) & 3, cmd & 3
                n = num or 256
                size = ((32 >> vl) * (vn + 1) * n + 31) // 32 * 4
                start, usn = imm & 0x3FF, (imm >> 14) & 1
                if vn == 3 and vl == 0 and n == 1:  # GIF tag
                    gif = struct.unpack_from("<Q", blk, p)[0]
                    gif_addr = start
                    gif_info = (gif & 0x7FFF, bool((((gif >> 47) & 0x7FF) >> 4) & 1))
                else:
                    if vn == 1 and vl == 0:
                        uv_addr = start
                    for j in range(n):
                        mem[_cycle(start, j, cl, wl)] = _inline_value(blk, p, j, vn, vl, usn)
                p += size
            elif cmd == 0x01:
                cl, wl = imm & 0xFF, (imm >> 8) & 0xFF
            elif cmd in (VIF_MSCNT, VIF_MSCAL, VIF_MSCALF):
                finish()
                gif_addr = uv_addr = gif_info = None
            elif cmd == VIF_STMASK:
                p += 4
            elif cmd in (VIF_STROW, VIF_STCOL):
                p += 16
            elif cmd == VIF_MPG:
                p += (num or 256) * 8
            elif cmd in (VIF_DIRECT, VIF_DIRECTHL):
                p += (imm or 65536) * 16
        if tid in (DMA_RET, DMA_END):
            return batches
        pos = end
    raise ValueError("cadeia de DMA sem RET")


def gfx_batches(blk, stats):
    header = struct.unpack_from("<I", blk, 4)[0]
    if header == 0x10:  # bloco vazio
        return []
    count, boxes_off, extra = struct.unpack_from("<iII", blk, header)
    pool = palette = None
    if extra:
        pool, palette = read_skin_pool(blk, extra)
    out = []
    for i in range(count):
        o = (boxes_off & ~3) + i * 0x38
        dma = struct.unpack_from("<I", blk, o + 0x20)[0] & ~3
        if dma:
            stats["caixas"] += 1
            if pool is not None:
                stats["caixas_skin"] += 1
                out.extend(walk_chain_skinned(blk, dma, pool, palette, stats))
            else:
                out.extend(walk_chain(blk, dma, stats))
    return out


def strip_to_triangles(batch, base):
    """Tira do GS: cada vértice cujo flag w não é -1 fecha um triângulo com os dois anteriores."""
    tris = []
    for i in range(2, len(batch.xyz)):
        if batch.normal[i][3] == -1:
            continue
        a, b, c = base + i - 2, base + i - 1, base + i
        tris.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return tris


# ---------------------------------------------------------------- glTF

def _png(w, h, rgba):
    rows = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


class GlbWriter:
    def __init__(self):
        self.bin = bytearray()
        self.gltf = {
            "asset": {"version": "2.0", "generator": "nightfire-port tools/geometry.py"},
            "buffers": [], "bufferViews": [], "accessors": [], "meshes": [],
            "nodes": [], "scenes": [{"nodes": []}], "scene": 0,
            "materials": [], "textures": [], "images": [],
            "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        }

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.gltf["bufferViews"].append(v)
        return len(self.gltf["bufferViews"]) - 1

    def accessor(self, data, ctype, count, atype, target, minmax=None, normalized=False):
        a = {"bufferView": self.view(data, target), "componentType": ctype, "count": count, "type": atype}
        if minmax:
            a["min"], a["max"] = minmax
        if normalized:
            a["normalized"] = True
        self.gltf["accessors"].append(a)
        return len(self.gltf["accessors"]) - 1

    def image_material(self, name, png, has_alpha):
        img = self.view(png)
        self.gltf["images"].append({"bufferView": img, "mimeType": "image/png", "name": name})
        self.gltf["textures"].append({"sampler": 0, "source": len(self.gltf["images"]) - 1})
        mat = {"name": name, "doubleSided": True,
               "pbrMetallicRoughness": {"baseColorTexture": {"index": len(self.gltf["textures"]) - 1},
                                        "metallicFactor": 0.0, "roughnessFactor": 1.0}}
        if has_alpha:
            mat["alphaMode"], mat["alphaCutoff"] = "MASK", 0.5
        self.gltf["materials"].append(mat)
        return len(self.gltf["materials"]) - 1

    def plain_material(self, name):
        self.gltf["materials"].append({"name": name, "doubleSided": True,
                                       "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0}})
        return len(self.gltf["materials"]) - 1

    def save(self, path):
        self.gltf["buffers"].append({"byteLength": len(self.bin)})
        js = json.dumps(self.gltf, separators=(",", ":")).encode()
        js += b" " * (-len(js) % 4)
        binc = bytes(self.bin) + b"\0" * (-len(self.bin) % 4)
        total = 12 + 8 + len(js) + 8 + len(binc)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(binc), 0x004E4942) + binc)


class ItemData:
    """Um item de mapa já lido: blocos, texturas e a lista de entidades (pares 0x2D + 0x04)."""

    def __init__(self, name, data):
        self.name, self.data = name, data
        _version, self.blocks = read_blocks(data)
        self.textures = {idx: (w, h, frames, rgba) for idx, w, h, _bpp, frames, rgba in textures(data, self.blocks)}
        gfx = [b for b in self.blocks if b.id == GFX_BLOCK]
        params = [b for b in self.blocks if b.id in (0x04, 0x1F)]
        if len(gfx) != len(params):
            raise ValueError(f"{name}: {len(gfx)} blocos 0x2D para {len(params)} blocos de parâmetros")
        self.entities = list(zip(gfx, params))
        # Os alvos de morph (bloco 0x2B) de cada entidade: os blocos de uma entidade vêm antes do bloco de
        # parâmetros que a fecha (parsemap_block_entity_params).
        self.morphs = {}
        ent, morph = 0, None
        for b in self.blocks:
            if b.id == MORPH_BLOCK:
                morph = b
            elif b.id in (0x04, 0x1F):
                if morph is not None:
                    self.morphs[ent] = morph
                ent, morph = ent + 1, None

    def entity_hash(self, i):
        return struct.unpack_from("<I", self.entities[i][1].payload(self.data), 0)[0]


class SceneBuilder:
    """Monta um .glb reaproveitando malhas e materiais entre instâncias."""

    def __init__(self, skinned=False):
        self.glb = GlbWriter()
        self.materials = {}
        self.meshes = {}
        self.stats = collections.Counter()
        self.skinned = skinned

    def skeleton(self, skin):
        """Cria os nós dos ossos e o skin glTF. Devolve o índice do skin.

        skin["parents"][i]: pai do osso i (None na raiz).
        skin["inverse_bind"][i]: translação t da pose inversa (rotação sempre identidade
        no disco), ou seja, a articulação fica em -t no espaço do modelo."""
        g = self.glb.gltf
        parents, tbind = skin["parents"], skin["inverse_bind"]
        world = [(-x, -y, -z) for x, y, z in tbind]
        first = len(g["nodes"])
        for i, par in enumerate(parents):
            wx, wy, wz = world[i]
            if par is not None:
                px, py, pz = world[par]
                wx, wy, wz = wx - px, wy - py, wz - pz
            g["nodes"].append({"name": f"bone{i:02d}", "translation": [wx, wy, wz]})
        for i, par in enumerate(parents):
            if par is not None:
                g["nodes"][first + par].setdefault("children", []).append(first + i)
        roots = [first + i for i, par in enumerate(parents) if par is None]
        mats = bytearray()
        for x, y, z in tbind:  # coluna-maior: translação nos elementos 12..14
            mats += struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1)
        ibm = self.glb.accessor(bytes(mats), 5126, len(tbind), "MAT4", None)
        self.joint_nodes = list(range(first, first + len(parents)))
        g.setdefault("skins", []).append({"name": skin.get("name", "skin"), "joints": list(range(first, first + len(parents))),
                                          "inverseBindMatrices": ibm, "skeleton": roots[0]})
        g["scenes"][0]["nodes"].extend(roots)
        return len(g["skins"]) - 1

    def material(self, item, tex_idx):
        key = (item.name, tex_idx)
        if key in self.materials:
            return self.materials[key]
        if tex_idx in item.textures:
            w, h, frames, rgba = item.textures[tex_idx]
            first = rgba[:w * (h // frames) * 4]
            has_alpha = any(first[k] < 250 for k in range(3, len(first), 4))
            mat = self.glb.image_material(f"{item.name}_tex{tex_idx:04d}", _png(w, h // frames, first), has_alpha)
        else:
            if tex_idx is not None:
                self.stats["textura_ausente"] += 1
            mat = self.glb.plain_material("untextured" if tex_idx is None else f"missing{tex_idx}")
        self.materials[key] = mat
        return mat

    def mesh(self, item, ent):
        """Índice da malha glTF da entidade, ou None se ela não tem geometria."""
        key = (item.name, ent)
        if key in self.meshes:
            return self.meshes[key]
        gfx = item.entities[ent][0]
        batches = gfx_batches(item.data[gfx.offset:gfx.offset + gfx.size], self.stats)
        groups = collections.defaultdict(list)
        for bt in batches:
            groups[bt.texture if bt.textured else None].append(bt)

        glb, primitives = self.glb, []
        morph = None
        if self.skinned and ent in item.morphs:
            from morph import read_morph  # noqa: E402 (tools/morph.py)
            morph = read_morph(item.morphs[ent].payload(item.data))
        for tex_idx, group in sorted(groups.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)):
            pos, nrm, uv, col, idx = bytearray(), bytearray(), bytearray(), bytearray(), bytearray()
            jnt, wgt = bytearray(), bytearray()
            mvert = []
            lo, hi = [float("inf")] * 3, [float("-inf")] * 3
            frames = item.textures[tex_idx][2] if tex_idx in item.textures else 1
            base = 0
            for bt in group:
                for i in range(len(bt.xyz)):
                    j = bt.joints[i] if bt.joints else (0, 0, 0, 0)
                    w = bt.weights[i] if bt.weights else (1.0, 0.0, 0.0, 0.0)
                    jnt += struct.pack("<4H", *j)
                    wgt += struct.pack("<4f", *w)
                    mvert.append(bt.morph[i] if bt.morph else -1)
                for i, (x, y, z) in enumerate(bt.xyz):
                    pos += struct.pack("<3f", x, y, z)
                    for k, v in enumerate((x, y, z)):
                        lo[k], hi[k] = min(lo[k], v), max(hi[k], v)
                    nx, ny, nz, _w = bt.normal[i]
                    ln = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
                    nrm += struct.pack("<3f", nx / ln, ny / ln, nz / ln)
                    s_, t = bt.uv[i] if bt.uv else (0.0, 0.0)
                    uv += struct.pack("<2f", s_, t / frames)  # textura animada: usa o primeiro quadro
                    r, g, b_, a = bt.color[i]
                    col += bytes((min(255, r * 2), min(255, g * 2), min(255, b_ * 2), min(255, a * 2)))
                for tri in strip_to_triangles(bt, base):
                    idx += struct.pack("<3I", *tri)
                base += len(bt.xyz)
            if not idx:
                continue
            self.stats["vertices_unicos"] += base
            self.stats["triangulos_unicos"] += len(idx) // 12
            attrs = {
                "POSITION": glb.accessor(bytes(pos), 5126, base, "VEC3", 34962, (lo, hi)),
                "NORMAL": glb.accessor(bytes(nrm), 5126, base, "VEC3", 34962),
                "TEXCOORD_0": glb.accessor(bytes(uv), 5126, base, "VEC2", 34962),
                "COLOR_0": glb.accessor(bytes(col), 5121, base, "VEC4", 34962, normalized=True),
            }
            if self.skinned:
                attrs["JOINTS_0"] = glb.accessor(bytes(jnt), 5123, base, "VEC4", 34962)
                attrs["WEIGHTS_0"] = glb.accessor(bytes(wgt), 5126, base, "VEC4", 34962)
            indices = glb.accessor(bytes(idx), 5125, len(idx) // 4, "SCALAR", 34963)
            prim = {"attributes": attrs, "indices": indices, "material": self.material(item, tex_idx), "mode": 4}
            if morph is not None:
                # Um alvo glTF por alvo do jogo: o deslocamento de cada vértice (o delta do elemento m + k).
                targets = []
                for k in range(morph["targets"]):
                    data = bytearray()
                    lo, hi = [0.0] * 3, [0.0] * 3
                    for m in mvert:
                        d = morph["delta"](m, k) if m >= 0 else (0.0, 0.0, 0.0)
                        data += struct.pack("<3f", *d)
                        for c in range(3):
                            lo[c], hi[c] = min(lo[c], d[c]), max(hi[c], d[c])
                    targets.append({"POSITION": glb.accessor(bytes(data), 5126, base, "VEC3", 34962, (lo, hi))})
                prim["targets"] = targets
                self.stats["alvos_de_morph"] = morph["targets"]
            primitives.append(prim)
        mesh = None
        if primitives:
            m = {"name": f"{item.name}_e{ent:04d}", "primitives": primitives}
            if morph is not None:
                from morph import TARGET_NAMES  # noqa: E402
                m["weights"] = [0.0] * morph["targets"]
                m["extras"] = {"targetNames": TARGET_NAMES[:morph["targets"]]}
            glb.gltf["meshes"].append(m)
            mesh = len(glb.gltf["meshes"]) - 1
        self.meshes[key] = mesh
        return mesh

    def node(self, name, mesh, translation=None, rotation=None, scale=None, extras=None, skin=None):
        n = {"name": name, "mesh": mesh}
        if skin is not None:
            n["skin"] = skin
        if translation and any(translation):
            n["translation"] = list(translation)
        if rotation and tuple(rotation) != (0.0, 0.0, 0.0, 1.0):
            n["rotation"] = list(rotation)
        if scale and any(abs(s - 1.0) > 1e-4 for s in scale):
            n["scale"] = list(scale)
        if extras:
            n["extras"] = extras
        self.glb.gltf["nodes"].append(n)
        self.glb.gltf["scenes"][0]["nodes"].append(len(self.glb.gltf["nodes"]) - 1)

    def save(self, path):
        self.glb.save(path)


# ---------------------------------------------------------------- posicionamento

PLACEMENT = struct.Struct("<HHII3f3f4f3fII")  # 72 bytes fixos; contagem de parâmetros em +72


def read_placements(item):
    """Entradas do bloco 0x1A (parsemap_block_map_data_static, 0x001d1998).

        u16 entity        índice no próprio item (0xFFFF = usar hash)
        u16 pad
        u32 hash          hashcode de entidade (0xFFFFFFFF = usar índice)
        u32 flags         byte baixo = tipo do objeto; 0x8000 = célula estática do mundo
        f32 position[3]
        f32 euler[3]      radianos
        f32 quat[4]       x, y, z, w: conjugado da rotação; às vezes desatualizado
        f32 scale[3]
        u32 unknown[2]
        u32 param_count, depois param_count pares (u32 índice, u32 valor)
    """
    out = []
    for b in item.blocks:
        if b.id != 0x1A:
            continue
        p, pos = b.payload(item.data), 0
        while pos + 76 <= len(p):
            v = PLACEMENT.unpack_from(p, pos)
            ent, h, flags = v[0], v[2], v[3]
            position, euler, quat, scale = v[4:7], v[7:10], v[10:14], v[14:17]
            count = struct.unpack_from("<I", p, pos + 72)[0]
            params = [struct.unpack_from("<II", p, pos + 76 + 8 * i) for i in range(count)]
            out.append({"entity": ent, "hash": h, "flags": flags, "position": position, "euler": euler,
                        "quat": quat, "scale": scale, "params": params})
            pos += 76 + 8 * count
    return out


def engine_rot_matrix(euler):
    """Matriz de rotação de RotMatrix (0x001e2368), nas linhas do MATRIX do jogo.
    O motor usa vetor-linha (v' = v * M, ver RotPreTransVec), então a forma
    vetor-coluna usada pelo glTF é a transposta."""
    x, y, z = euler
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return [[cy * cz, sx * sy * cz + cx * sz, -cx * sy * cz + sx * sz],
            [-cy * sz, cx * cz - sx * sy * sz, sx * cz + cx * sy * sz],
            [sy, -sx * cy, cx * cy]]


def euler_to_gltf_quat(euler):
    """Quatérnio glTF (x, y, z, w) equivalente à RotMatrix do jogo.

    O quatérnio gravado no bloco 0x1A, conjugado, dá o mesmo resultado em 98,9% das
    entradas. As demais têm quatérnio desatualizado (às vezes nem normalizado),
    por isso a rotação é recalculada a partir dos ângulos, como o motor faz."""
    m = engine_rot_matrix(euler)
    r = [[m[j][i] for j in range(3)] for i in range(3)]  # transposta: vetor-coluna
    tr = r[0][0] + r[1][1] + r[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s)
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = (0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s)
    elif r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = ((r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s)
    else:
        s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = ((r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[1][0] - r[0][1]) / s)
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def export_item(item_bytes, out_path, name="map", skin=None, animations=None):
    """Exporta todas as entidades de um item nas coordenadas do arquivo, sem posicionamento.
    Com skin (ver read_anim_skins), o modelo sai com esqueleto, ossos e pesos."""
    item = ItemData(name, item_bytes)
    sb = SceneBuilder(skinned=skin is not None)
    skin_index = sb.skeleton(skin) if skin else None
    for ent in range(len(item.entities)):
        mesh = sb.mesh(item, ent)
        if mesh is not None:
            sb.node(f"entity{ent:04d}", mesh, skin=skin_index)
            sb.stats["objetos"] += 1
    if skin:
        sb.stats["ossos"] = len(skin["parents"])
        from anim import add_gltf_animation
        for anim_name, poses, translation_mask in animations or []:
            if add_gltf_animation(sb.glb, anim_name, poses, sb.joint_nodes, translation_mask) is not None:
                sb.stats["animacoes"] += 1
    sb.save(out_path)
    return sb.stats


def export_level(pak_bytes, out_path, from_file_quat=False):
    """Exporta a fase montada: cada entrada de posicionamento do mapa principal vira um nó,
    com a malha da entidade referenciada (do próprio mapa ou de outro item do pacote)."""
    items, main = {}, None
    for off, size, typ, nm in parse_pak(pak_bytes):
        if typ in (0, 1, 2, 11):
            items[nm] = ItemData(nm, pak_bytes[off:off + size])
            if typ == 1 and main is None:
                main = items[nm]
    if main is None:
        raise SystemExit("pacote sem item de mapa principal (tipo 1)")

    by_hash = {}
    for it in items.values():
        for i in range(len(it.entities)):
            h = it.entity_hash(i)
            if h != 0xFFFFFFFF:
                by_hash[h] = (it, i)

    sb = SceneBuilder()
    placed = set()
    for k, pl in enumerate(read_placements(main)):
        if pl["hash"] != 0xFFFFFFFF:
            if pl["hash"] not in by_hash:
                sb.stats["hash_nao_resolvido"] += 1
                continue
            it, ent = by_hash[pl["hash"]]
        elif pl["entity"] < len(main.entities):
            it, ent = main, pl["entity"]
        else:
            sb.stats["indice_invalido"] += 1
            continue
        mesh = sb.mesh(it, ent)
        if mesh is None:
            sb.stats["posicionamentos_sem_geometria"] += 1
            continue
        if from_file_quat:
            qx, qy, qz, qw = pl["quat"]
            rotation = (-qx, -qy, -qz, qw)
        else:
            rotation = euler_to_gltf_quat(pl["euler"])
        sb.node(f"p{k:04d}_{it.name}_e{ent:04d}", mesh, pl["position"], rotation, pl["scale"],
                extras={"type": pl["flags"] & 0xFF, "flags": pl["flags"], "hash": f"{pl['hash']:08x}"})
        placed.add((it.name, ent))
        sb.stats["objetos_posicionados"] += 1

    # Entidades do mapa principal sem nenhum posicionamento entram nas coordenadas do arquivo.
    for ent in range(len(main.entities)):
        if (main.name, ent) not in placed:
            mesh = sb.mesh(main, ent)
            if mesh is not None:
                sb.node(f"unplaced_{main.name}_e{ent:04d}", mesh)
                sb.stats["entidades_sem_posicionamento"] += 1
    sb.save(out_path)
    return sb.stats


def read_anim_skins(pak_bytes):
    """Lê os itens tipo 3 (skin de animação, hash 05xxxxxx; AnimProcessSkinData, 0x001bd500).

        +0x00  u32 hash; +0x04..+0x0C 3 x f32 (escala?)
        +0x10  u8 n_meshes, u8 n_discrete, u8 unknown, u8 n_extra (entradas de 36 bytes), u8 skeleton_id
        +0x15  u8 parent[bones]          0x80 | índice do pai; 0xFF na raiz
               u8 discrete[n_discrete], alinha a 4, u32 discrete_hash[n_discrete]
               alinha a 4, u32 mesh_hash[n_meshes]   bit 0x100000 = malha trocável (manga)
               bones x { f32 t[3]; f32 q[4] }       pose inversa; q é sempre identidade
               n_extra x 36 bytes

    O número de ossos vem do item SKEL (tipo 6): u16 skeleton_id, u8 bone_count.
    Devolve {hash_da_entidade: skin}."""
    items = parse_pak(pak_bytes)
    bone_count = {struct.unpack_from("<H", pak_bytes, o)[0]: pak_bytes[o + 2] for o, _s, t, _n in items if t == 6}
    out = {}
    for off, size, typ, name in items:
        if typ != 3:
            continue
        p = pak_bytes[off:off + size]
        n_mesh, n_disc, skel = p[16], p[17], p[20]
        if skel not in bone_count or not n_mesh:
            continue
        bones = bone_count[skel]
        parents = [None if b == 0xFF else b & 0x7F for b in p[21:21 + bones]]
        pos = 21 + bones
        if n_disc:
            pos = ((pos + n_disc + 3) & ~3) + 4 * n_disc
        pos = (pos + 3) & ~3
        meshes = [struct.unpack_from("<I", p, pos + 4 * i)[0] for i in range(n_mesh)]
        pos += 4 * n_mesh
        tbind = [struct.unpack_from("<3f", p, pos + 28 * i) for i in range(bones)]
        skin = {"name": name, "skeleton": skel, "parents": parents, "inverse_bind": tbind}
        for m in meshes:
            out.setdefault(m & ~0x100000, skin)
    return out


def item_entity_hashes(item_bytes):
    _v, blocks = read_blocks(item_bytes)
    return [struct.unpack_from("<I", b.payload(item_bytes), 0)[0] for b in blocks if b.id in (0x04, 0x1F)]


def export_characters(paks, out_dir):
    """Exporta os itens tipo 2 (personagens com skinning) na pose de repouso, um .glb por modelo.
    O mesmo modelo aparece em vários pacotes com o mesmo nome; só a primeira cópia é exportada."""
    os.makedirs(out_dir, exist_ok=True)
    seen = {}
    all_skins = {}
    pak_data = []
    for pak_path in paks:
        data = open(pak_path, "rb").read()
        pak_data.append((pak_path, data))
        for h, sk in read_anim_skins(data).items():
            all_skins.setdefault(h, sk)
    for pak_path, data in pak_data:
        stem = os.path.splitext(os.path.basename(pak_path))[0]
        for off, size, typ, name in parse_pak(data):
            if typ != 2 or name in seen:
                continue
            seen[name] = stem
            item = data[off:off + size]
            skin = next((all_skins[h] for h in item_entity_hashes(item) if h in all_skins), None)
            out = os.path.join(out_dir, f"{name}.glb")
            stats = export_item(item, out, name, skin=skin)
            print(f"{stem}/{name}: " + ", ".join(f"{k}={v}" for k, v in sorted(stats.items())))
    print(f"{len(seen)} modelos em {out_dir}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["item", "level", "characters"])
    ap.add_argument("path", nargs="+")
    ap.add_argument("--out")
    ap.add_argument("--raw", action="store_true",
                    help="level: exporta só o mapa principal nas coordenadas do arquivo, sem posicionar")
    ap.add_argument("--file-quat", action="store_true",
                    help="usa o quatérnio gravado no arquivo em vez de recalcular pelos ângulos (para comparação)")
    args = ap.parse_args()

    if args.cmd == "characters":
        export_characters(args.path, args.out or os.path.join("extracted", "models", "characters"))
        return
    if len(args.path) != 1:
        raise SystemExit("item e level recebem um único arquivo")
    data = open(args.path[0], "rb").read()
    stem = os.path.splitext(os.path.basename(args.path[0]))[0]
    if args.cmd == "level" and not args.raw:
        out = args.out or os.path.join("extracted", "models", stem + ".glb")
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        stats = export_level(data, out, from_file_quat=args.file_quat)
    else:
        if args.cmd == "level":
            mains = [(o, s, n) for o, s, t, n in parse_pak(data) if t == 1]
            if not mains:
                raise SystemExit("pacote sem item de mapa principal (tipo 1)")
            o, s, n = mains[0]
            item, name = data[o:o + s], f"{stem}_{n}_raw"
        else:
            item, name = data, stem
        out = args.out or os.path.join("extracted", "models", name + ".glb")
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        stats = export_item(item, out, name)
    print(f"{out}: " + ", ".join(f"{k}={v}" for k, v in sorted(stats.items())))


if __name__ == "__main__":
    main()
