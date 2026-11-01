// O 2D do GS no nf_driving (driving_app/drv_hud2d.h; docs/driving.md seção 28).
#include "driving_app/drv_hud2d.h"

#include <algorithm>

namespace drv {
namespace hud2d {

namespace {
uint32_t g_uploaded = 0;
}

void reset() { g_uploaded = 0; }

void upload_images(DrvRenderer& r) {
    const uint32_t n = ui_image_count();
    for (uint32_t id = g_uploaded + 1; id <= n; ++id) {
        const UiImage* img = ui_image_get(id);
        if (img && img->width > 0 && img->height > 0)
            r.set_fx_texture(kImageSlotBase + int(id), img->width, img->height, img->rgba.data());
    }
    g_uploaded = n;
    for (const uint32_t id : ui_image_take_dirty()) {
        const UiImage* img = ui_image_get(id);
        if (img && img->width > 0 && img->height > 0)
            r.set_fx_texture(kImageSlotBase + int(id), img->width, img->height, img->rgba.data());
    }
}

void to_fx(const std::vector<Gs2dBatch>& in, int w, int h, std::vector<DrvFxBatch>& out) {
    // A tela do PS2 na área 4:3 pela altura (como o filme).
    float aw = float(h) * 4.0f / 3.0f, ah = float(h);
    if (aw > float(w)) {
        aw = float(w);
        ah = aw * 0.75f;
    }
    const float x0 = (float(w) - aw) * 0.5f, y0 = (float(h) - ah) * 0.5f;
    const float sx = aw / 640.0f, sy = ah / 448.0f;
    for (const Gs2dBatch& b : in) {
        if (b.verts.empty()) continue;
        DrvFxBatch f;
        const UiImage* img = b.textured ? ui_image_get(b.image) : nullptr;
        f.texture = img ? kImageSlotBase + int(b.image) : -1;
        f.world = false;
        f.alpha = b.blend ? b.alpha : 0x8000000064ull;  // ABE desligado: a cor da fonte (Cs)
        f.test = 0x30000ull;  // por cima da cena: sem o teste de Z
        f.zwrite = false;
        const float iw = img ? 1.0f / float(img->width) : 0.0f, ih = img ? 1.0f / float(img->height) : 0.0f;
        // Os lotes da largura da tela do PS2 (o escurecer, as tarjas, os golpes na tela, a cortina de sangue) vão de uma
        // borda à outra da janela (como o 2D a pé: psi_2d_map_rect).
        float minx = 1e9f, maxx = -1e9f;
        for (const Gs2dVertex& v : b.verts) {
            minx = std::min(minx, v.x);
            maxx = std::max(maxx, v.x);
        }
        const bool full = minx <= 0.5f && maxx >= 639.5f;
        const float bx0 = full ? 0.0f : x0, bsx = full ? float(w) / 640.0f : sx;
        f.verts.reserve(b.verts.size());
        for (const Gs2dVertex& v : b.verts) {
            DrvFxVertex o;
            const float px = bx0 + v.x * bsx, py = y0 + v.y * sy;
            o.pos[0] = px / float(w) * 2.0f - 1.0f;
            o.pos[1] = 1.0f - py / float(h) * 2.0f;
            o.pos[2] = 0.0f;
            o.pos[3] = 1.0f;
            o.uv[0] = b.st ? v.u : v.u * iw;
            o.uv[1] = b.st ? v.v : v.v * ih;
            o.colour = v.rgba;
            f.verts.push_back(o);
        }
        out.push_back(std::move(f));
    }
}

void packet_to_fx(const std::vector<uint64_t>& words, int w, int h, std::vector<DrvFxBatch>& out) {
    Gs2dState s;
    std::vector<Gs2dBatch> batches;
    gs2d_run(words.data(), words.size(), s, batches);
    to_fx(batches, w, h, out);
}

}  // namespace hud2d
}  // namespace drv
