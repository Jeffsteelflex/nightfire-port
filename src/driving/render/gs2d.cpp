// O 2D das missões de carro no PC: os pacotes do GIF interpretados (driving/render/gs2d.h; docs/driving.md seção 28).
#include "driving/render/gs2d.h"

#include <algorithm>
#include <cstring>

namespace drv {

namespace {

std::vector<UiImage>& images() {
    static std::vector<UiImage> v;
    return v;
}
std::vector<uint32_t>& dirty() {
    static std::vector<uint32_t> v;
    return v;
}

float bits_float(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Os atributos do desenho: os do PRIM com o PRMODECONT ligado, senão os do PRMODE.
uint64_t attrs(const Gs2dState& s) { return s.prmodecont ? s.prim : s.prmode; }

Gs2dBatch& batch_for(const Gs2dState& s, std::vector<Gs2dBatch>& out) {
    const uint64_t a = attrs(s);
    Gs2dBatch b;
    b.textured = (a & 0x10) != 0;
    b.st = (a & 0x100) == 0;
    b.blend = (a & 0x40) != 0;
    b.image = uint32_t(s.tex0 & 0x3fff);
    b.linear = (s.tex1 & 0x20) != 0;
    b.alpha = s.alpha;
    b.test = s.test;
    b.zwrite = ((s.zbuf >> 32) & 1) == 0;
    if (!out.empty()) {
        const Gs2dBatch& l = out.back();
        if (l.textured == b.textured && l.st == b.st && l.blend == b.blend && (!b.textured || l.image == b.image) &&
            l.linear == b.linear && l.alpha == b.alpha && l.test == b.test && l.zwrite == b.zwrite)
            return out.back();
    }
    out.push_back(b);
    return out.back();
}

void emit_triangle(const Gs2dState& s, const Gs2dVertex& a, const Gs2dVertex& b, const Gs2dVertex& c,
                   std::vector<Gs2dBatch>& out) {
    Gs2dBatch& bt = batch_for(s, out);
    bt.verts.push_back(a);
    bt.verts.push_back(b);
    bt.verts.push_back(c);
}

void vertex(uint64_t xyz, bool kick, Gs2dState& s, std::vector<Gs2dBatch>& out) {
    Gs2dVertex v;
    v.x = float(int32_t(uint32_t(xyz & 0xffff)) - s.offset_x) / 16.0f;
    v.y = float(int32_t(uint32_t((xyz >> 16) & 0xffff)) - s.offset_y) / 16.0f;
    v.z = uint32_t(xyz >> 32);
    v.rgba = uint32_t(s.rgbaq);
    if ((attrs(s) & 0x100) != 0) {
        v.u = float(uint32_t(s.uv & 0x3fff)) / 16.0f;
        v.v = float(uint32_t((s.uv >> 16) & 0x3fff)) / 16.0f;
    } else {
        const float q = s.q != 0.0f ? s.q : 1.0f;
        v.u = bits_float(uint32_t(s.st)) / q;
        v.v = bits_float(uint32_t(s.st >> 32)) / q;
    }
    const uint32_t prim = uint32_t(s.prim & 7);
    // O enfileirar como o GS (o XYZ3 enfileira sem desenhar).
    switch (prim) {
        case 6:  // SPRITE
            s.queue[s.queued++] = v;
            if (s.queued == 2) {
                s.queued = 0;
                if (!kick) break;
                const Gs2dVertex& p = s.queue[0];
                const Gs2dVertex& q = v;
                Gs2dVertex a = p, b = p, c = p, d = q;
                b.x = q.x;
                b.u = q.u;
                c.y = q.y;
                c.v = q.v;
                // A cor de um SPRITE é a do segundo vértice (o GS desenha com a cor do último).
                a.rgba = b.rgba = c.rgba = d.rgba = q.rgba;
                emit_triangle(s, a, b, c, out);
                emit_triangle(s, c, b, d, out);
            }
            break;
        case 3:  // TRIANGLE
            s.queue[s.queued++] = v;
            if (s.queued == 3) {
                s.queued = 0;
                if (kick) emit_triangle(s, s.queue[0], s.queue[1], s.queue[2], out);
            }
            break;
        case 4:  // TRISTRIP
            if (s.queued < 2) {
                s.queue[s.queued++] = v;
                break;
            }
            if (kick) emit_triangle(s, s.queue[0], s.queue[1], v, out);
            s.queue[0] = s.queue[1];
            s.queue[1] = v;
            break;
        case 5:  // TRIFAN
            if (s.queued < 2) {
                s.queue[s.queued++] = v;
                break;
            }
            if (kick) emit_triangle(s, s.queue[0], s.queue[1], v, out);
            s.queue[1] = v;
            break;
        default:
            break;
    }
}

}  // namespace

uint32_t ui_image_add(UiImage img) {
    images().push_back(std::move(img));
    return uint32_t(images().size());
}
const UiImage* ui_image_get(uint32_t id) {
    return id >= 1 && id <= images().size() ? &images()[id - 1] : nullptr;
}
uint32_t ui_image_count() { return uint32_t(images().size()); }
void ui_images_clear() {
    images().clear();
    dirty().clear();
}
bool ui_image_update(uint32_t id, UiImage img) {
    if (id < 1 || id > images().size()) return false;
    images()[id - 1] = std::move(img);
    if (std::find(dirty().begin(), dirty().end(), id) == dirty().end()) dirty().push_back(id);
    return true;
}
std::vector<uint32_t> ui_image_take_dirty() {
    std::vector<uint32_t> v;
    v.swap(dirty());
    return v;
}

void gs2d_write(uint8_t reg, uint64_t value, Gs2dState& s, std::vector<Gs2dBatch>& out) {
    switch (reg) {
        case 0x00:
            s.prim = value;
            s.queued = 0;
            break;
        case 0x01:
            s.rgbaq = value;
            s.q = bits_float(uint32_t(value >> 32));
            break;
        case 0x02: s.st = value; break;
        case 0x03: s.uv = value; break;
        case 0x04:
        case 0x05: vertex(value, true, s, out); break;
        case 0x0c:
        case 0x0d: vertex(value, false, s, out); break;
        case 0x06: s.tex0 = value; break;
        case 0x14: s.tex1 = value; break;
        case 0x18:
            s.offset_x = int32_t(value & 0xffff);
            s.offset_y = int32_t((value >> 32) & 0xffff);
            break;
        case 0x1a: s.prmodecont = (value & 1) != 0; break;
        case 0x1b: s.prmode = value; break;
        case 0x42: s.alpha = value; break;
        case 0x47: s.test = value; break;
        case 0x4e: s.zbuf = value; break;
        default: break;
    }
}

void gs2d_run(const uint64_t* words, size_t count, Gs2dState& s, std::vector<Gs2dBatch>& out) {
    // Os códigos do VIF depois da tag DMA (o primeiro qword), de 32 bits, até um DIRECT.
    const size_t n32 = count * 2;
    auto w32 = [&](size_t i) { return uint32_t(words[i / 2] >> (32 * (i & 1))); };
    size_t i = 4;
    while (i < n32) {
        const uint32_t code = w32(i++);
        const uint32_t cmd = (code >> 24) & 0x7f;
        if (cmd != 0x50 && cmd != 0x51) continue;
        uint32_t qwc = code & 0xffff;
        if (qwc == 0) qwc = 0x10000;
        size_t q = (i + 3) / 4;  // o qword seguinte
        const size_t end = q + qwc;
        while (q < end && q * 2 + 1 < count + 1) {
            const uint64_t lo = words[q * 2], hi = words[q * 2 + 1];
            ++q;
            const uint32_t nloop = uint32_t(lo & 0x7fff);
            const bool pre = ((lo >> 46) & 1) != 0;
            const uint32_t flg = uint32_t((lo >> 58) & 3);
            uint32_t nreg = uint32_t(lo >> 60);
            if (nreg == 0) nreg = 16;
            if (pre) gs2d_write(0x00, (lo >> 47) & 0x7ff, s, out);
            if (flg == 0) {  // PACKED
                for (uint32_t l = 0; l < nloop && q < end; ++l)
                    for (uint32_t r = 0; r < nreg && q < end; ++r, ++q) {
                        const uint64_t a = words[q * 2], b = words[q * 2 + 1];
                        const uint32_t desc = uint32_t((hi >> (4 * r)) & 0xf);
                        switch (desc) {
                            case 0xe: gs2d_write(uint8_t(b & 0xff), a, s, out); break;
                            case 0x1: {
                                const uint64_t rgba = (a & 0xff) | (((a >> 32) & 0xff) << 8) | ((b & 0xff) << 16) |
                                                      (((b >> 32) & 0xff) << 24);
                                // O Q do RGBAQ empacotado é o do último ST (o registrador interno do GS).
                                uint32_t qb;
                                std::memcpy(&qb, &s.q, 4);
                                gs2d_write(0x01, rgba | (uint64_t(qb) << 32), s, out);
                                break;
                            }
                            case 0x2:
                                gs2d_write(0x02, a, s, out);
                                s.q = bits_float(uint32_t(b));
                                break;
                            case 0x3: gs2d_write(0x03, (a & 0x3fff) | (((a >> 32) & 0x3fff) << 16), s, out); break;
                            case 0x4:
                            case 0x5: {
                                const uint64_t v = (a & 0xffff) | (((a >> 32) & 0xffff) << 16) |
                                                   (uint64_t(uint32_t(b)) << 32);
                                const bool adc = ((b >> 47) & 1) != 0;
                                vertex(v, !adc, s, out);
                                break;
                            }
                            default: break;
                        }
                    }
            } else if (flg == 1) {  // REGLIST: 64 bits por registrador
                size_t k = 0;
                const size_t total = size_t(nloop) * nreg;
                while (k < total && q < end) {
                    for (int h = 0; h < 2 && k < total; ++h, ++k) {
                        const uint64_t v = words[q * 2 + size_t(h)];
                        const uint32_t desc = uint32_t((hi >> (4 * (k % nreg))) & 0xf);
                        if (desc != 0xe && desc != 0xf) gs2d_write(uint8_t(desc), v, s, out);
                    }
                    ++q;
                }
            } else {
                q += nloop;  // IMAGE: os dados de uma transferência (o 2D não usa)
            }
        }
        i = end * 4;
    }
}

}  // namespace drv
