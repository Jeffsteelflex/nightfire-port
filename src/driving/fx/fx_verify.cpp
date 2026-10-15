#include "driving/fx/fx_verify.h"

#include <cstring>

#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drv {

namespace {

constexpr uint32_t kTop = 0x20;  // o TOP da conferência (acima das constantes globais 0 e 1)

void put_qw(ps2::VuMachine& vu, uint32_t qw, const void* src) { std::memcpy(vu.qword(qw), src, 16); }

void put_floats(ps2::VuMachine& vu, uint32_t qw, const float* v) { put_qw(vu, qw, v); }

void put_tag(ps2::VuMachine& vu, uint32_t qw, uint32_t nloop, bool eop, uint32_t nreg, uint64_t regs) {
    uint32_t* w = vu.qword(qw);
    const uint64_t lo = uint64_t(nloop & 0x7fff) | (eop ? (1ull << 15) : 0) | (uint64_t(nreg & 15) << 60);
    w[0] = uint32_t(lo);
    w[1] = uint32_t(lo >> 32);
    w[2] = uint32_t(regs);
    w[3] = uint32_t(regs >> 32);
}

bool run(ps2::VuMachine& vu, FxVerifyStats& s) {
    ++s.runs;
    vu.set_top(uint16_t(kTop), uint16_t(kTop));
    try {
        vu.run(2, 2'000'000);
    } catch (const std::exception& e) {
        ++s.failed_runs;
        if (s.first_error.empty()) s.first_error = e.what();
        return false;
    }
    return true;
}

void compare_vertex(const uint32_t* got, const GsVertexOut& want, int st_words, FxVerifyStats& s, const char* what) {
    ++s.vertices;
    bool bad = false;
    for (int c = 0; c < st_words; ++c)
        if (got[c] != want.st[c]) {
            ++s.st_diff;
            bad = true;
        }
    for (int c = 0; c < 4; ++c)
        if (got[4 + c] != want.rgba[c]) {
            ++s.rgba_diff;
            bad = true;
        }
    for (int c = 0; c < 4; ++c)
        if (got[8 + c] != want.xyz[c]) {
            ++s.xyz_diff;
            bad = true;
        }
    if (bad && s.first_error.empty()) {
        char b[256];
        std::snprintf(b, sizeof b, "%s: vu %08x %08x %08x %08x | %08x %08x %08x %08x, ref %08x %08x %08x %08x | %08x %08x %08x %08x",
                      what, got[0], got[1], got[2], got[3], got[8], got[9], got[10], got[11], want.st[0], want.st[1],
                      want.st[2], want.st[3], want.xyz[0], want.xyz[1], want.xyz[2], want.xyz[3]);
        s.first_error = b;
    }
}

}  // namespace

void fx_verify_sprite(const std::vector<uint64_t>& code, const FxVuGlobals& g, const float mvp[4][4],
                      const float control[4], const float coord[4], const float uvuv[4], const uint32_t colour[4],
                      FxVerifyStats& s) {
    ps2::VuMachine vu(ps2::VuUnit::VU1);
    vu.load_micro(0, code.data(), uint32_t(code.size() * 8));
    std::memset(vu.data(), 0, vu.data_size());
    put_floats(vu, 0, g.vp_offset);
    put_floats(vu, 1, g.vp_scale);
    const uint32_t count[4] = {1, 0, 0, 0};
    put_qw(vu, kTop + 0, count);
    for (int r = 0; r < 4; ++r) put_floats(vu, kTop + 1 + uint32_t(r), mvp[r]);
    put_tag(vu, kTop + 5, 0, true, 0, 0);
    put_floats(vu, kTop + 0xc, coord);
    put_floats(vu, kTop + 0x9c, uvuv);
    put_qw(vu, kTop + 0x12c, colour);
    put_tag(vu, kTop + 0x1bc, 0, true, 0, 0);
    put_floats(vu, kTop + 0x1c4, control);
    if (!run(vu, s)) return;
    ++s.sprites;
    GsVertexOut ref[6];
    render_sprites_rot_reference(g, mvp, control, coord, uvuv, colour, ref);
    if (ref[0].xyz[3] & 0x8000u) ++s.adc_sprites;
    for (int v = 0; v < 6; ++v) {
        uint32_t got[12];
        std::memcpy(got, vu.qword(kTop + 0x1c6 + uint32_t(v) * 3), 48);
        compare_vertex(got, ref[v], 4, s, "RenderSpritesRot");
    }
}

bool fx_triangle_inside(const float mvp[4][4], const float pos[3][4]) {
    for (int v = 0; v < 3; ++v) {
        float c[4];
        for (int k = 0; k < 4; ++k)
            c[k] = pos[v][0] * mvp[0][k] + pos[v][1] * mvp[1][k] + pos[v][2] * mvp[2][k] + pos[v][3] * mvp[3][k];
        if (!(c[3] > 0.5f)) return false;
        // x e y com folga; z até perto do plano de longe (o z de recorte do PS2 é quase w lá longe).
        for (int k = 0; k < 3; ++k) {
            const float lim = k == 2 ? 0.9999f : 0.9f;
            if (c[k] > c[3] * lim || c[k] < -c[3] * lim) return false;
        }
    }
    return true;
}

void fx_verify_triangle(const std::vector<uint64_t>& code, const FxVuGlobals& g, const float mvp[4][4],
                        const float pos[3][4], const float stq[3][4], const uint32_t colour[3][4], FxVerifyStats& s) {
    ps2::VuMachine vu(ps2::VuUnit::VU1);
    vu.load_micro(0, code.data(), uint32_t(code.size() * 8));
    std::memset(vu.data(), 0, vu.data_size());
    put_floats(vu, 0, g.vp_offset);
    put_floats(vu, 1, g.vp_scale);
    const uint32_t count[4] = {3, 0, 0, 0};
    put_qw(vu, kTop + 0, count);
    for (int r = 0; r < 4; ++r) put_floats(vu, kTop + 1 + uint32_t(r), mvp[r]);
    for (int v = 0; v < 3; ++v) {
        put_floats(vu, kTop + 5 + uint32_t(v), pos[v]);
        put_floats(vu, kTop + 0x51 + uint32_t(v), stq[v]);
        put_qw(vu, kTop + 0x9d + uint32_t(v), colour[v]);
    }
    put_tag(vu, kTop + 0xe9, 0, true, 0, 0);
    put_tag(vu, kTop + 0xf0, 1, false, 1, 0);  // o PRIM
    const uint32_t prim[4] = {3, 0, 0, 0};    // a lista de triângulos
    put_qw(vu, kTop + 0xf1, prim);
    if (!run(vu, s)) return;
    ++s.triangles;
    for (int v = 0; v < 3; ++v) {
        GsVertexOut ref;
        mat_debug_texture_reference(g, mvp, pos[v], stq[v], colour[v], ref);
        uint32_t got[12];
        std::memcpy(got, vu.qword(kTop + 0xf9 + uint32_t(v) * 3), 48);
        compare_vertex(got, ref, 3, s, "MatDebugTexture");
    }
}

}  // namespace drv
