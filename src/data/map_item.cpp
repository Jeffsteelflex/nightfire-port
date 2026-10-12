#include "data/map_item.h"

#include <algorithm>
#include <tuple>
#include <cmath>
#include "platform/fmath.h"

namespace nf {

std::vector<Block> read_blocks(ByteView item) {
    std::vector<Block> blocks;
    const uint32_t version = u32(item, 0);
    size_t pos = 4;
    for (uint32_t i = 1; i < 100000; ++i) {
        if (version == 1) pos = size_t(u32(item, size_t(4) * i)) + 4;
        const uint32_t header = u32(item, pos);
        Block b{static_cast<uint8_t>(header >> 24), header & 0xFFFFFF, static_cast<uint32_t>(pos)};
        if (b.size < 4) throw FormatError("bloco com tamanho inválido");
        blocks.push_back(b);
        if (b.id == block_id::kEnd) return blocks;
        if (version != 1) pos += b.size;
    }
    throw FormatError("item de mapa sem bloco de fim");
}

// ------------------------------------------------------------------ texturas

std::map<int, Texture> decode_textures(ByteView item, const std::vector<Block>& blocks) {
    std::map<int, Texture> out;
    const Block* header = nullptr;
    std::vector<const Block*> pixels, palettes;
    for (const Block& b : blocks) {
        if (b.id == block_id::kTextureHeader && !header) header = &b;
        if (b.id == 0x15 || b.id == block_id::kTextureData) pixels.push_back(&b);
        if (b.id == 0x11 || b.id == block_id::kPaletteData) palettes.push_back(&b);
    }
    if (!header) return out;
    ByteView h = header->payload(item);
    const size_t count = std::min({h.size() / 12, pixels.size(), palettes.size()});
    for (size_t i = 0; i < count; ++i) {
        const uint8_t flags = u8(h, i * 12);
        const int w = u16(h, i * 12 + 2) + 1;
        const int ht = u16(h, i * 12 + 4) + 1;
        const int frames = std::max(1, int(u16(h, i * 12 + 6) & 0xFF));
        Texture t;
        if (decode_texture(w, ht, frames, (flags & 1) != 0, pixels[i]->payload(item), palettes[i]->payload(item), t)) {
            out.emplace(int(i), std::move(t));
        }
    }
    return out;
}

bool decode_texture(int w, int ht, int frames, bool bpp8, ByteView px, ByteView pal, Texture& t) {
    const int bpp = bpp8 ? 8 : 4;
    const size_t frame_bytes = size_t(w) * ht * bpp / 8;
    if (px.size() != frame_bytes * frames || pal.empty()) return false;

    std::vector<std::array<uint8_t, 4>> palette(pal.size() / 4);
    for (size_t c = 0; c < palette.size(); ++c) {
        palette[c] = {pal[c * 4], pal[c * 4 + 1], pal[c * 4 + 2], pal[c * 4 + 3]};
    }
    if (bpp == 8 && palette.size() == 256) {
        // Paleta CSM1 do GS: em cada grupo de 32 cores, as faixas 8-15 e 16-23 vêm trocadas.
        for (size_t base = 0; base < 256; base += 32) {
            for (size_t k = 0; k < 8; ++k) std::swap(palette[base + 8 + k], palette[base + 16 + k]);
        }
    }
    t = Texture{};
    t.width = w;
    t.height = ht;
    t.frames = frames;
    t.rgba.resize(size_t(w) * ht * 4);
    for (int p = 0; p < w * ht; ++p) {
        int index;
        if (bpp == 8) {
            index = px[p];
        } else {
            const uint8_t byte = px[p / 2];
            index = (p & 1) ? (byte >> 4) : (byte & 0x0F);
        }
        const auto& c = index < int(palette.size()) ? palette[index] : palette[0];
        uint8_t* dst = &t.rgba[size_t(p) * 4];
        dst[0] = c[0];
        dst[1] = c[1];
        dst[2] = c[2];
        dst[3] = c[3];  // alfa como no GS (0x80 = opaco); o shader dobra, como nas texturas HD
        if (dst[3] < 0x7d) t.has_alpha = true;
    }
    return true;
}

// ------------------------------------------------------------------ geometria

namespace {

// Escritas A+D (descritor 0xE) das GIF tags de um pacote DIRECT. PACKED: NLOOP * NREG quadwords;
// REGLIST: NLOOP * NREG palavras de 64 bits (alinhadas a quadword); IMAGE: NLOOP quadwords.
int apply_direct(ByteView blk, size_t p, size_t end, GsState& state) {
    int writes = 0;
    while (p + 16 <= end) {
        const uint64_t tag = u64(blk, p);
        const uint64_t regs = u64(blk, p + 8);
        const uint32_t nloop = uint32_t(tag & 0x7FFF);
        const bool eop = ((tag >> 15) & 1) != 0;
        const uint32_t flg = uint32_t(tag >> 58) & 3;
        const uint32_t nreg = (uint32_t(tag >> 60) & 15) == 0 ? 16 : uint32_t(tag >> 60) & 15;
        p += 16;
        if (flg == 0) {
            for (uint32_t i = 0; i < nloop && p + 16 <= end; ++i) {
                for (uint32_t r = 0; r < nreg && p + 16 <= end; ++r, p += 16) {
                    if (((regs >> (4 * r)) & 15) != 0xE) continue;
                    const uint64_t data = u64(blk, p);
                    switch (u8(blk, p + 8)) {
                        case 0x42: state.alpha = data; state.has |= GsState::kAlpha; break;
                        case 0x47: state.test = data; state.has |= GsState::kTest; break;
                        case 0x4e: state.zbuf = data; state.has |= GsState::kZbuf; break;
                        default: break;
                    }
                    ++writes;
                }
            }
        } else if (flg == 1) {
            p += (size_t(nloop) * nreg * 8 + 15) / 16 * 16;
        } else {
            p += size_t(nloop) * 16;
        }
        if (eop) break;
    }
    return writes;
}

struct Batch {
    int texture = -1;
    bool textured = false;
    bool env = false;  // palavra w da GIF tag = 0x10: rotina 0x2e0 (mapa de ambiente)
    GsState gs;
    std::vector<std::array<float, 2>> uv;
    std::vector<std::array<float, 3>> xyz;
    std::vector<std::array<int8_t, 4>> normal;
    std::vector<std::array<uint8_t, 4>> color;

    bool complete() const {
        const size_t n = xyz.size();
        return n >= 3 && normal.size() == n && color.size() == n && (uv.empty() || uv.size() == n);
    }
};

constexpr uint32_t kDmaCnt = 1, kDmaRef = 3, kDmaRet = 6, kDmaEnd = 7;
// Palavra w da GIF tag de um lote: o microprograma da VU1 salta para a tabela em 8 + w + rotina (0, 2,
// 4 ou 6 por luzes e tweak): 8 dá as rotinas comuns, 0x10 sempre a do mapa de ambiente (0x2e0).
constexpr uint32_t kGifRoutineEnv = 0x10;

void walk_chain(ByteView blk, size_t dma, std::vector<Batch>& out) {
    size_t pos = dma;
    int texture = -1;
    Batch cur;
    GsState state;  // registradores escritos por esta cadeia até aqui
    for (int guard = 0; guard < 1000000; ++guard) {
        const uint64_t lo = u64(blk, pos);
        const uint32_t qwc = lo & 0xFFFF;
        const uint32_t tid = (lo >> 28) & 7;
        const uint32_t addr = (lo >> 32) & 0x7FFFFFFF;
        const uint32_t vif0 = u32(blk, pos + 8), vif1 = u32(blk, pos + 12);

        if (tid == kDmaRef) {
            if (vif0 == 0 && vif1 == 0) {  // referência de textura: addr = índice da textura no item
                texture = int(addr);
                cur.texture = texture;
            }
            pos += 16;
            continue;
        }
        if (tid != kDmaCnt && tid != kDmaRet && tid != kDmaEnd) throw FormatError("tag DMA inesperada");

        size_t p = pos + 8;
        const size_t end = pos + 16 + size_t(qwc) * 16;
        while (p < end) {
            const uint32_t code = u32(blk, p);
            p += 4;
            const uint32_t cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
            if ((cmd & 0x60) == 0x60) {  // UNPACK
                const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
                const uint32_t n = num ? num : 256;
                const size_t size = (size_t((32 >> vl) * (vn + 1)) * n + 31) / 32 * 4;
                const bool usn = (imm >> 14) & 1;
                if (vn == 3 && vl == 0 && n == 1) {  // GIF tag
                    const uint64_t gif = u64(blk, p);
                    const uint32_t prim = (gif >> 47) & 0x7FF;
                    cur.textured = (prim >> 4) & 1;
                    cur.env = u32(blk, p + 12) == kGifRoutineEnv;
                    if ((gif >> 46) & 1) state.abe = uint8_t((prim >> 6) & 1);  // PRE: o PRIM vem da tag
                } else if (vn == 1 && vl == 0) {
                    cur.uv.resize(n);
                    for (uint32_t i = 0; i < n; ++i) cur.uv[i] = {f32(blk, p + i * 8), f32(blk, p + i * 8 + 4)};
                } else if (vn == 2 && vl == 0) {
                    cur.xyz.resize(n);
                    for (uint32_t i = 0; i < n; ++i) {
                        cur.xyz[i] = {f32(blk, p + i * 12), f32(blk, p + i * 12 + 4), f32(blk, p + i * 12 + 8)};
                    }
                } else if (vn == 3 && vl == 2 && !usn) {
                    cur.normal.resize(n);
                    for (uint32_t i = 0; i < n; ++i) {
                        for (int k = 0; k < 4; ++k) cur.normal[i][k] = static_cast<int8_t>(u8(blk, p + i * 4 + k));
                    }
                } else if (vn == 3 && vl == 2 && usn) {
                    cur.color.resize(n);
                    for (uint32_t i = 0; i < n; ++i) {
                        for (int k = 0; k < 4; ++k) cur.color[i][k] = u8(blk, p + i * 4 + k);
                    }
                }
                p += size;
            } else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {  // MSCAL / MSCALF / MSCNT
                // O lote é desenhado com o estado em vigor agora (a cadeia põe FLUSHE antes de cada
                // DIRECT, então uma escrita nunca alcança o lote anterior).
                cur.gs = state;
                if (cur.complete()) out.push_back(std::move(cur));
                cur = Batch{};
                cur.texture = texture;
            } else if (cmd == 0x20) {
                p += 4;
            } else if (cmd == 0x30 || cmd == 0x31) {
                p += 16;
            } else if (cmd == 0x4A) {
                p += size_t(num ? num : 256) * 8;
            } else if (cmd == 0x50 || cmd == 0x51) {
                const size_t size = size_t(imm ? imm : 65536) * 16;
                apply_direct(blk, p, std::min(p + size, end), state);
                p += size;
            }
        }
        if (tid == kDmaRet || tid == kDmaEnd) return;
        pos = end;
    }
    throw FormatError("cadeia DMA sem RET");
}

// ---- skinning

// Um vértice de entrada do SkinIt (SkinVertexIn, 32 bytes).
struct SkinIn {
    float pos[3];
    float weight;
    int16_t normal[3];   // 16 bits altos de nx, ny, nz: normal * 64
    uint16_t slots[2];   // 16 bits baixos de nx e ny: slots na lista de ossos (bit 15 ignorado)
    bool strip_start;    // bit 15 de w: começa uma tira nova sem desenhar
    int16_t morph;       // 16 bits altos de w: índice de morph (-1 = nenhum)
};

// Origem de um quadword da memória de dados da VU1. Como em tools/geometry.py, o que interessa é de
// onde veio cada quadword: do buffer de saída do SkinIt (por uma tag REF) ou de um UNPACK dos pacotes
// CNT (UV, cor, outros).
struct VuQword {
    enum Kind : uint8_t { kNone, kPool, kUv, kColor, kOther };
    Kind kind = kNone;
    int64_t pool = 0;  // kPool: quadword do buffer de saída (2k = posição do vértice k, 2k + 1 = normal)
    float uv[2]{};
    uint8_t color[4]{};
};

// Endereços além dos 1024 quadwords da VU1 não aparecem nos dados; a emulação aceita um pouco mais
// (como o dicionário do exportador) e recusa o resto.
constexpr size_t kVuQwords = 0x800;

struct SkinBatch {
    int texture = -1;
    bool textured = false;
    bool env = false;
    GsState gs;
    bool has_uv = false;
    std::vector<SkinnedVertex> verts;
    std::vector<uint8_t> strip_start;
};

// Endereço do j-ésimo elemento de um UNPACK com STCYCL cl, wl (modo de pular quando wl <= cl).
uint32_t cycle_addr(uint32_t start, uint32_t j, uint32_t cl, uint32_t wl) {
    if (wl != 0 && wl <= cl) return start + (j / wl) * cl + (j % wl);
    return start + j;
}

// Percorre a cadeia de DMA de uma caixa com skinning emulando as escritas da VIF1 na memória da VU1
// (mesma lógica de walk_chain_skinned em tools/geometry.py). As tags REF com UNPACK V4-32 leem o
// buffer de saída do SkinIt; o endereço delas é um deslocamento em bytes nesse buffer (31 bits com
// sinal: a primeira começa em -16, um quadword antes, para que o STCYCL 3,2 pule a cor). Cada vértice
// ocupa 3 quadwords depois da GIF tag: posição, cor, normal. A posição e os ossos vêm da entrada do
// SkinIt correspondente, que está na pose de repouso.
void walk_chain_skinned(ByteView blk, size_t dma, const std::vector<SkinIn>& pool, const std::vector<uint8_t>& bones,
                        std::vector<SkinBatch>& out, uint32_t& bad_batches) {
    std::vector<VuQword> mem(kVuQwords);
    const auto slot = [&mem](uint32_t addr) -> VuQword& {
        if (addr >= mem.size()) throw FormatError("endereço da VU1 fora da memória emulada");
        return mem[addr];
    };
    size_t pos = dma;
    int texture = -1;
    uint32_t cl = 4, wl = 4;
    int64_t gif_addr = -1, uv_addr = -1;
    uint32_t nloop = 0;
    bool textured = false;
    bool env = false;
    GsState state;

    // MSCAL/MSCNT: a VU1 desenha o lote descrito pela última GIF tag.
    const auto finish = [&]() {
        if (gif_addr < 0) return;
        SkinBatch b;
        b.texture = texture;
        b.textured = textured;
        b.env = env;
        b.gs = state;
        b.has_uv = uv_addr >= 0;
        uint32_t missing = 0;
        bool broken = false;
        for (uint32_t i = 0; i < nloop && !broken; ++i) {
            const uint32_t base = uint32_t(gif_addr) + 1 + 3 * i;
            const VuQword& xs = slot(base);
            const VuQword& cs = slot(base + 1);
            const VuQword& ns = slot(base + 2);
            if (xs.kind != VuQword::kPool || ns.kind != VuQword::kPool || cs.kind != VuQword::kColor) {
                ++missing;
                continue;
            }
            // Posição num quadword par, normal no ímpar seguinte, do mesmo vértice.
            const int64_t vx = xs.pool, vn = ns.pool;
            if (vx < 0 || vn < 0 || vx % 2 != 0 || vn != vx + 1 || size_t(vx / 2) >= pool.size()) {
                broken = true;
                break;
            }
            const SkinIn& src = pool[size_t(vx / 2)];
            const uint32_t s0 = src.slots[0] & 0x7fffu, s1 = src.slots[1] & 0x7fffu;
            if (s0 >= bones.size() || s1 >= bones.size()) {
                broken = true;
                break;
            }
            SkinnedVertex v{};
            for (int k = 0; k < 3; ++k) v.pos[k] = src.pos[k];
            // Como a VU1 a vê: o SkinIt leva normal * 64 a normal * 128 (ITOF15), e a rotina de luz
            // divide por 128.
            for (int k = 0; k < 3; ++k) v.normal[k] = float(src.normal[k]) * (1.0f / 64.0f);
            if (uv_addr >= 0) {
                const VuQword& uv = slot(uint32_t(uv_addr) + i);
                if (uv.kind == VuQword::kUv) {
                    v.uv[0] = uv.uv[0];
                    v.uv[1] = uv.uv[1];
                }
            }
            for (int k = 0; k < 4; ++k) v.color[k] = cs.color[k];  // escala do GS, como no disco
            v.bones[0] = bones[s0];
            v.bones[1] = bones[s1];
            v.weight = src.weight;
            v.morph = src.morph;
            b.verts.push_back(v);
            b.strip_start.push_back(src.strip_start ? 1 : 0);
        }
        if (broken || (missing != 0 && missing != nloop)) {
            ++bad_batches;
            return;
        }
        // Sem nenhum vértice: a chamada inicial do microprograma, antes dos dados (uma por malha).
        if (missing == nloop) return;
        if (b.verts.size() >= 3) out.push_back(std::move(b));
    };

    for (int guard = 0; guard < 1000000; ++guard) {
        const uint64_t lo = u64(blk, pos);
        const uint32_t qwc = lo & 0xFFFF;
        const uint32_t tid = (lo >> 28) & 7;
        const uint32_t addr = (lo >> 32) & 0x7FFFFFFF;
        const uint32_t vif0 = u32(blk, pos + 8), vif1 = u32(blk, pos + 12);

        if (tid == kDmaRef) {
            if (vif0 == 0 && vif1 == 0) {
                texture = int(addr);  // referência de textura: índice da textura no item
            } else {
                int64_t offset = addr;
                if (offset & 0x40000000) offset -= 0x80000000LL;
                const int64_t q0 = offset >= 0 ? offset / 16 : -((-offset + 15) / 16);
                for (const uint32_t code : {vif0, vif1}) {
                    const uint32_t cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
                    if (cmd == 0x01) {  // STCYCL
                        cl = imm & 0xFF;
                        wl = (imm >> 8) & 0xFF;
                    } else if ((cmd & 0x60) == 0x60) {
                        const uint32_t start = imm & 0x3FF;
                        const uint32_t n = num ? num : 256;
                        for (uint32_t j = 0; j < n; ++j) {
                            VuQword& q = slot(cycle_addr(start, j, cl, wl));
                            q = VuQword{};
                            q.kind = VuQword::kPool;
                            q.pool = q0 + j;
                        }
                    }
                }
            }
            pos += 16;
            continue;
        }
        if (tid != kDmaCnt && tid != kDmaRet && tid != kDmaEnd) throw FormatError("tag DMA inesperada");

        size_t p = pos + 8;
        const size_t end = pos + 16 + size_t(qwc) * 16;
        while (p < end) {
            const uint32_t code = u32(blk, p);
            p += 4;
            const uint32_t cmd = (code >> 24) & 0x7F, num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
            if ((cmd & 0x60) == 0x60) {  // UNPACK
                const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
                const uint32_t n = num ? num : 256;
                const size_t size = (size_t((32 >> vl) * (vn + 1)) * n + 31) / 32 * 4;
                const uint32_t start = imm & 0x3FF;
                const bool usn = (imm >> 14) & 1;
                if (vn == 3 && vl == 0 && n == 1) {  // GIF tag
                    const uint64_t gif = u64(blk, p);
                    gif_addr = start;
                    nloop = uint32_t(gif & 0x7FFF);
                    textured = ((((gif >> 47) & 0x7FF) >> 4) & 1) != 0;
                    env = u32(blk, p + 12) == kGifRoutineEnv;
                    if ((gif >> 46) & 1) state.abe = uint8_t((((gif >> 47) & 0x7FF) >> 6) & 1);
                } else {
                    if (vn == 1 && vl == 0) uv_addr = start;
                    for (uint32_t j = 0; j < n; ++j) {
                        VuQword& q = slot(cycle_addr(start, j, cl, wl));
                        q = VuQword{};
                        if (vn == 1 && vl == 0) {
                            q.kind = VuQword::kUv;
                            q.uv[0] = f32(blk, p + j * 8);
                            q.uv[1] = f32(blk, p + j * 8 + 4);
                        } else if (vn == 3 && vl == 2 && usn) {
                            q.kind = VuQword::kColor;
                            for (int k = 0; k < 4; ++k) q.color[k] = u8(blk, p + j * 4 + k);
                        } else {
                            q.kind = VuQword::kOther;
                        }
                    }
                }
                p += size;
            } else if (cmd == 0x01) {  // STCYCL
                cl = imm & 0xFF;
                wl = (imm >> 8) & 0xFF;
            } else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {  // MSCAL / MSCALF / MSCNT
                finish();
                gif_addr = uv_addr = -1;
            } else if (cmd == 0x20) {
                p += 4;
            } else if (cmd == 0x30 || cmd == 0x31) {
                p += 16;
            } else if (cmd == 0x4A) {
                p += size_t(num ? num : 256) * 8;
            } else if (cmd == 0x50 || cmd == 0x51) {
                const size_t size = size_t(imm ? imm : 65536) * 16;
                apply_direct(blk, p, std::min(p + size, end), state);
                p += size;
            }
        }
        if (tid == kDmaRet || tid == kDmaEnd) return;
        pos = end;
    }
    throw FormatError("cadeia DMA sem RET");
}

// Malha com skinning: a estrutura extra do cabeçalho (SkinInfo) dá a entrada do SkinIt e a lista de
// ossos. Como psiDrawSkinObjectMatrix (0x00111828), só a primeira caixa é desenhada (todas as malhas
// do disco têm uma só).
EntityMesh decode_skinned(ByteView blk, int32_t count, uint32_t boxes, uint32_t extra,
                          const std::function<int(int)>& frames_of) {
    EntityMesh mesh;
    mesh.skinned = true;
    const uint32_t info = extra & ~3u;
    const uint32_t input = u32(blk, info) & ~3u;
    const uint32_t out_size = u32(blk, info + 4);
    const uint32_t bones_offset = u32(blk, info + 12) & ~3u;

    std::vector<uint8_t> bones;
    for (size_t p = bones_offset; u8(blk, p) != 0xFF; ++p) {
        bones.push_back(u8(blk, p));
        if (bones.size() > 256) throw FormatError("lista de ossos sem fim");
    }
    for (const uint8_t b : bones) mesh.bone_count = std::max(mesh.bone_count, int(b) + 1);
    if (mesh.bone_count > kMaxSkinBones) throw FormatError("osso além da paleta do renderizador");

    std::vector<SkinIn> pool(out_size / 32);
    for (size_t k = 0; k < pool.size(); ++k) {
        const size_t at = input + k * 32;
        SkinIn& v = pool[k];
        for (int c = 0; c < 3; ++c) v.pos[c] = f32(blk, at + 4 * c);
        v.weight = f32(blk, at + 12);
        int32_t q1[4];
        for (int c = 0; c < 4; ++c) q1[c] = i32(blk, at + 16 + 4 * c);
        for (int c = 0; c < 3; ++c) v.normal[c] = int16_t(q1[c] >> 16);
        v.slots[0] = uint16_t(q1[0] & 0xFFFF);
        v.slots[1] = uint16_t(q1[1] & 0xFFFF);
        v.strip_start = (q1[3] & 0x8000) != 0;
        v.morph = int16_t(q1[3] >> 16);
    }

    std::vector<SkinBatch> batches;
    if (count > 0) {
        const uint32_t dma = u32(blk, boxes + 0x20) & ~3u;
        if (dma) walk_chain_skinned(blk, dma, pool, bones, batches, mesh.bad_batches);
    }

    // Partes na ordem da cadeia (a ordem em que o GS desenha): lotes seguidos com a mesma textura e o
    // mesmo estado do GS viram uma parte só.
    std::vector<std::pair<std::tuple<int, GsState, bool>, std::vector<const SkinBatch*>>> groups;
    for (const SkinBatch& b : batches) {
        const std::tuple<int, GsState, bool> key{b.textured ? b.texture : -1, b.gs, b.env};
        if (groups.empty() || groups.back().first != key) groups.push_back({key, {}});
        groups.back().second.push_back(&b);
        if ((b.gs.has & 7) != 7) ++mesh.inherited_gs;
    }
    for (auto& [key, group] : groups) {
        const int tex = std::get<0>(key);
        SubMesh sm;
        sm.texture = tex;
        sm.gs = std::get<1>(key);
        sm.env = std::get<2>(key);
        sm.first_index = uint32_t(mesh.indices.size());
        const float vscale = tex >= 0 ? 1.0f / float(std::max(1, frames_of(tex))) : 1.0f;
        for (const SkinBatch* b : group) {
            const uint32_t base = uint32_t(mesh.skin_vertices.size());
            for (SkinnedVertex v : b->verts) {
                v.uv[1] *= vscale;
                mesh.skin_vertices.push_back(v);
            }
            // Tira do GS, como nas malhas rígidas: o vértice marcado inicia uma nova tira sem desenhar.
            for (size_t i = 2; i < b->verts.size(); ++i) {
                if (b->strip_start[i]) continue;
                const uint32_t a = base + uint32_t(i) - 2, c = base + uint32_t(i) - 1, d = base + uint32_t(i);
                if (i % 2 == 0) {
                    mesh.indices.insert(mesh.indices.end(), {a, c, d});
                } else {
                    mesh.indices.insert(mesh.indices.end(), {c, a, d});
                }
            }
        }
        sm.index_count = uint32_t(mesh.indices.size()) - sm.first_index;
        if (sm.index_count) mesh.submeshes.push_back(sm);
    }
    return mesh;
}

}  // namespace

void skin_vertex(const SkinnedVertex& v, const float* palette, float pos[3], float normal[3]) {
    const float* m0 = palette + 16 * size_t(v.bones[0]);
    const float* m1 = palette + 16 * size_t(v.bones[1]);
    const float w0 = v.weight, w1 = 1.0f - v.weight;
    for (int c = 0; c < 3; ++c) {
        const float r0 = v.normal[0] * m0[c] + v.normal[1] * m0[4 + c] + v.normal[2] * m0[8 + c];
        const float r1 = v.normal[0] * m1[c] + v.normal[1] * m1[4 + c] + v.normal[2] * m1[8 + c];
        const float p0 = v.pos[0] * m0[c] + v.pos[1] * m0[4 + c] + v.pos[2] * m0[8 + c] + m0[12 + c];
        const float p1 = v.pos[0] * m1[c] + v.pos[1] * m1[4 + c] + v.pos[2] * m1[8 + c] + m1[12 + c];
        pos[c] = w0 * p0 + w1 * p1;
        normal[c] = w0 * r0 + w1 * r1;
    }
}

MorphTargets decode_morph(ByteView payload) {
    MorphTargets m;
    m.vertices = u32(payload, 0);
    m.targets = u32(payload, 4);
    m.first = u32(payload, 8);
    if (m.targets == 0 || m.targets > uint32_t(kMaxMorphTargets)) throw FormatError("morph com alvos demais");
    // Os deltas acabam no fim do bloco (no disco, exatamente; um resto menor que um elemento seria alinhamento).
    const size_t elements = size_t(m.first) + size_t(m.vertices) * m.targets;
    if (size_t(m.first) * 12 < (size_t(3) + m.vertices) * 4 || elements * 12 > payload.size() ||
        payload.size() - elements * 12 >= 12) {
        throw FormatError("bloco de morph com tamanho inesperado");
    }
    m.deltas.resize(elements * 3);
    for (size_t i = 0; i < elements * 3; ++i) {
        const uint32_t bits = u32(payload, i * 4);
        // A VU0 não tem desnormalizados: expoente 0 vale 0 (o cabeçalho, lido como float por vértices com m = 0).
        m.deltas[i] = (bits & 0x7f800000u) == 0 ? 0.0f : f32(payload, i * 4);
    }
    return m;
}

void morph_vertex(const SkinnedVertex& v, const MorphTargets& m, const float* weights, int count, float pos[3]) {
    for (int c = 0; c < 3; ++c) pos[c] = v.pos[c];
    if (v.morph < 0) return;
    const size_t elements = m.elements();
    for (int k = 0; k < count; ++k) {
        const size_t e = size_t(v.morph) + size_t(k);
        if (e >= elements) continue;
        // vmulx.xyz vf5, vf3, vf1 (delta x peso) e vadd vf2, vf2, vf5.
        for (int c = 0; c < 3; ++c) pos[c] += m.deltas[e * 3 + size_t(c)] * weights[k];
    }
}

int gs_apply_direct(ByteView packet, GsState& state) { return apply_direct(packet, 0, packet.size(), state); }

// Cada cor entra com um coeficiente k = k1 + kx * C (k1 em {0, 1}, kx em {-1, 0, 1}), que vira um fator
// e um sinal; os sinais escolhem a operação.
GsBlendEq gs_blend_equation(uint64_t alpha, bool abe, bool premultiplied) {
    const uint32_t a = uint32_t(alpha) & 3, b = uint32_t(alpha >> 2) & 3, c = uint32_t(alpha >> 4) & 3,
                   d = uint32_t(alpha >> 6) & 3;
    const auto coef = [&](uint32_t who, int& k1, int& kx) {
        kx = (a == who ? 1 : 0) - (b == who ? 1 : 0);
        k1 = d == who ? 1 : 0;
    };
    int s1, sx, d1, dx;
    coef(0, s1, sx);  // Cs
    coef(1, d1, dx);  // Cd
    GsBlendEq out;
    const BlendFactor x = c == 0 ? BlendFactor::SrcAlpha : (c == 1 ? BlendFactor::DstAlpha : BlendFactor::Fix);
    const BlendFactor inv_x =
        c == 0 ? BlendFactor::InvSrcAlpha : (c == 1 ? BlendFactor::InvDstAlpha : BlendFactor::InvFix);
    out.premultipliable = c == 0 && s1 == 0 && sx != 0;
    out.fix = std::min(float((alpha >> 32) & 0xff) / 128.0f, 1.0f);
    const auto factor = [&](int k1, int kx, bool src, BlendFactor& f, int& sign) {
        sign = 1;
        if (k1 == 0 && kx == 0) {
            f = BlendFactor::Zero;
        } else if (k1 == 1 && kx == 0) {
            f = BlendFactor::One;
        } else if (k1 == 0) {
            f = (src && premultiplied && out.premultipliable) ? BlendFactor::One : x;
            sign = kx;
        } else if (kx == -1) {
            f = inv_x;
        } else {
            return false;  // 1 + C
        }
        return true;
    };
    int ss = 1, ds = 1;
    if (!factor(s1, sx, true, out.src, ss) || !factor(d1, dx, false, out.dst, ds)) {
        out.supported = false;
        out.src = BlendFactor::SrcAlpha;
        out.dst = BlendFactor::InvSrcAlpha;
        out.op = BlendOp::Add;
        out.enable = abe;
        return out;
    }
    if (ss > 0 && ds > 0) {
        out.op = BlendOp::Add;
    } else if (ss > 0) {
        out.op = BlendOp::Subtract;
    } else if (ds > 0) {
        out.op = BlendOp::RevSubtract;
    } else {  // os dois negativos: o GS satura em 0
        out.src = out.dst = BlendFactor::Zero;
        out.op = BlendOp::Add;
    }
    out.enable = abe && !(out.src == BlendFactor::One && out.dst == BlendFactor::Zero && out.op == BlendOp::Add);
    if (!out.enable) {
        out.src = BlendFactor::One;
        out.dst = BlendFactor::Zero;
        out.op = BlendOp::Add;
    }
    return out;
}

bool gs_alpha_coverage_candidate(float atst, float aref, bool aint, const GsBlendEq& g) {
    if (atst != 5.0f && atst != 6.0f) return false;
    const float t = aref + (aint && atst == 6.0f ? 1.0f : 0.0f);  // o menor alfa que passa (escala do GS)
    if (t <= 0.0f) return false;
    if (!g.enable) return true;
    const bool lerp = g.src == BlendFactor::SrcAlpha && g.dst == BlendFactor::InvSrcAlpha && g.op == BlendOp::Add;
    return lerp && t >= 128.0f;
}

bool gfx_is_skinned(ByteView blk) {
    try {
        const uint32_t header = u32(blk, 4);
        return header != 0x10 && u32(blk, header + 8) != 0;
    } catch (const FormatError&) {
        return false;
    }
}

EntityMesh decode_gfx(ByteView blk, const std::function<int(int)>& frames_of) {
    EntityMesh mesh;
    const uint32_t header = u32(blk, 4);
    if (header == 0x10) return mesh;  // bloco vazio
    const int32_t count = i32(blk, header);
    const uint32_t boxes = u32(blk, header + 4) & ~3u;
    const uint32_t extra = u32(blk, header + 8);
    if (extra) return decode_skinned(blk, count, boxes, extra, frames_of);
    std::vector<Batch> batches;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t dma = u32(blk, boxes + size_t(i) * 0x38 + 0x20) & ~3u;
        if (dma) walk_chain(blk, dma, batches);
    }

    // Partes na ordem da cadeia, como no caso com skinning: a ordem importa nas partes misturadas (e
    // nas máscaras de alfa do destino, ALPHA 0x64 seguido de 0x58).
    std::vector<std::pair<std::tuple<int, GsState, bool>, std::vector<const Batch*>>> groups;
    for (const Batch& b : batches) {
        const std::tuple<int, GsState, bool> key{b.textured ? b.texture : -1, b.gs, b.env};
        if (groups.empty() || groups.back().first != key) groups.push_back({key, {}});
        groups.back().second.push_back(&b);
        if ((b.gs.has & 7) != 7) ++mesh.inherited_gs;
    }

    for (auto& [key, group] : groups) {
        const int tex = std::get<0>(key);
        SubMesh sm;
        sm.texture = tex;
        sm.gs = std::get<1>(key);
        sm.env = std::get<2>(key);
        sm.first_index = uint32_t(mesh.indices.size());
        const float vscale = tex >= 0 ? 1.0f / float(std::max(1, frames_of(tex))) : 1.0f;
        for (const Batch* b : group) {
            const uint32_t base = uint32_t(mesh.vertices.size());
            for (size_t i = 0; i < b->xyz.size(); ++i) {
                Vertex v{};
                for (int k = 0; k < 3; ++k) v.pos[k] = b->xyz[i][k];
                // Como a rotina de luz da VU1 a usa: o inteiro do UNPACK V4-8 vezes 1/128 (I).
                for (int k = 0; k < 3; ++k) v.normal[k] = float(b->normal[i][k]) * (1.0f / 128.0f);
                if (!b->uv.empty()) {
                    v.uv[0] = b->uv[i][0];
                    v.uv[1] = b->uv[i][1] * vscale;
                }
                for (int k = 0; k < 4; ++k) v.color[k] = b->color[i][k];  // escala do GS, como no disco
                mesh.vertices.push_back(v);
            }
            // Tira do GS: o vértice com w == -1 inicia uma nova tira sem desenhar.
            for (size_t i = 2; i < b->xyz.size(); ++i) {
                if (b->normal[i][3] == -1) continue;
                const uint32_t a = base + uint32_t(i) - 2, c = base + uint32_t(i) - 1, d = base + uint32_t(i);
                if (i % 2 == 0) {
                    mesh.indices.insert(mesh.indices.end(), {a, c, d});
                } else {
                    mesh.indices.insert(mesh.indices.end(), {c, a, d});
                }
            }
        }
        sm.index_count = uint32_t(mesh.indices.size()) - sm.first_index;
        if (sm.index_count) mesh.submeshes.push_back(sm);
    }
    return mesh;
}

// ------------------------------------------------------------------ posicionamento

std::vector<Placement> read_placements(ByteView item, const std::vector<Block>& blocks) {
    std::vector<Placement> out;
    for (const Block& b : blocks) {
        if (b.id != block_id::kPlacement) continue;
        ByteView p = b.payload(item);
        size_t pos = 0;
        while (pos + 76 <= p.size()) {
            Placement pl;
            pl.entity = u16(p, pos);
            pl.hash = u32(p, pos + 4);
            pl.flags = u32(p, pos + 8);
            for (int k = 0; k < 3; ++k) {
                pl.position[k] = f32(p, pos + 12 + 4 * k);
                pl.euler[k] = f32(p, pos + 24 + 4 * k);
                pl.scale[k] = f32(p, pos + 52 + 4 * k);
            }
            const uint32_t params = u32(p, pos + 72);
            out.push_back(pl);
            pos += 76 + size_t(params) * 8;
        }
    }
    return out;
}

std::array<float, 16> placement_matrix(const Placement& p) {
    const float cx = nf::fm::cos(p.euler[0]), sx = nf::fm::sin(p.euler[0]);
    const float cy = nf::fm::cos(p.euler[1]), sy = nf::fm::sin(p.euler[1]);
    const float cz = nf::fm::cos(p.euler[2]), sz = nf::fm::sin(p.euler[2]);
    const float r[3][3] = {
        {cy * cz, sx * sy * cz + cx * sz, -cx * sy * cz + sx * sz},
        {-cy * sz, cx * cz - sx * sy * sz, sx * cz + cx * sy * sz},
        {sy, -sx * cy, cx * cy},
    };
    std::array<float, 16> m{};
    for (int row = 0; row < 3; ++row) {
        const float s = std::fabs(p.scale[row]) > 1e-6f ? p.scale[row] : 1.0f;
        for (int col = 0; col < 3; ++col) m[row * 4 + col] = r[row][col] * s;
    }
    m[12] = p.position[0];
    m[13] = p.position[1];
    m[14] = p.position[2];
    m[15] = 1.0f;
    return m;
}

// ------------------------------------------------------------------ item

MapItem::MapItem(std::string name, int type, Bytes data)
    : name_(std::move(name)), type_(type), data_(std::move(data)) {
    blocks_ = read_blocks(data_);
    std::vector<Block> gfx, params;
    for (const Block& b : blocks_) {
        if (b.id == block_id::kGfx) gfx.push_back(b);
        if (b.id == block_id::kEntityParams || b.id == block_id::kEntityParamsAlt) params.push_back(b);
    }
    if (gfx.size() != params.size()) throw FormatError(name_ + ": blocos de geometria e de parâmetros não pareiam");
    for (size_t i = 0; i < gfx.size(); ++i) entities_.emplace_back(gfx[i], params[i]);
    textures_ = decode_textures(data_, blocks_);
}

uint32_t MapItem::entity_hash(size_t i) const { return u32(entities_[i].second.payload(data_), 0); }

}  // namespace nf
