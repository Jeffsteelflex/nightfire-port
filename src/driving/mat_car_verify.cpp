// A conferência da família MatCar contra o microprograma original (mat_car_verify.h).

#include "driving/mat_car_verify.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>

#include "platform/crt_compat.h"
#include "platform/vu_interp.h"

namespace drv {

namespace {

// NF_VERIFY_DEBUG=1: cada diferença de cor (acima de 1) e de X/Y/Z fora da tolerância numa linha "DBG".
bool verify_debug() {
    static const bool on = nf::get_env_narrow("NF_VERIFY_DEBUG", nullptr);
    return on;
}

uint32_t ulps(float a, float b) {
    int32_t ia, ib;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);
    if (ia < 0) ia = int32_t(0x80000000u) - ia;
    if (ib < 0) ib = int32_t(0x80000000u) - ib;
    const int64_t d = int64_t(ia) - int64_t(ib);
    return uint32_t(d < 0 ? -d : d);
}

}  // namespace

CarConstants car_constants_from(const VuMemory& m) {
    CarConstants k;
    auto get = [&](int qw, float v[4]) {
        for (int c = 0; c < 4; ++c) v[c] = m.f(qw, c);
    };
    get(0, k.vp_offset);
    get(1, k.vp_scale);
    for (int i = 0; i < 4; ++i) get(0x40 + i, k.point_pos[i]);
    for (int i = 0; i < 4; ++i) get(0x44 + i, k.point_col[i]);
    get(0x48, k.fog);
    for (int i = 0; i < 3; ++i) get(0x49 + i, k.light_dir_t[i]);
    for (int i = 0; i < 3; ++i) get(0x4c + i, k.light_col[i]);
    get(0x4f, k.ambient);
    for (int i = 0; i < 4; ++i) get(0x50 + i, k.spec_mat[i]);
    get(0x54, k.refl_info1);
    get(0x55, k.refl_info2);
    for (int i = 0; i < 16; ++i) get(0x56 + i, k.damage[i]);
    return k;
}

namespace {

void verify_once(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CarBatch& b,
                 CarVerifyStats& s) {
    ++s.batches;
    const CarMethodLayout& l = *b.layout;
    ps2::VuMachine vu(ps2::VuUnit::VU1);
    vu.load_micro(0, code.data(), uint32_t(code.size() * 8));
    std::memcpy(vu.data(), mem.w, sizeof mem.w);
    vu.set_top(uint16_t(top), uint16_t(top));
    try {
        vu.run(2, 2'000'000);
    } catch (const std::exception& e) {
        ++s.failed_runs;
        if (s.first_error.empty()) s.first_error = e.what();
        return;
    }
    std::vector<CarVertexOut> ref[kCarMaxPasses];
    car_reference(b, car_constants_from(mem), ref);
    // Os pacotes de vértices: os XGKICK do endereço da etiqueta GIF dos vértices, um por passada.
    int pass = 0;
    for (const ps2::GifKick& k : vu.kicks()) {
        if (k.address != top + uint32_t(l.gif_tag)) continue;
        while (pass < kCarMaxPasses && ref[pass].empty()) ++pass;
        if (pass >= kCarMaxPasses) break;
        const std::vector<CarVertexOut>& r = ref[pass];
        // Três qwords por vértice (STQ, RGBAQ, XYZF2) na família; dois (RGBAQ, XYZF2) na sombra.
        const size_t stride = size_t(l.out_qw) * 16;
        if (k.data.size() < 16 + r.size() * stride) {
            ++s.missing_kicks;
            ++pass;
            continue;
        }
        for (size_t v = 0; v < r.size(); ++v) {
            uint32_t w[12] = {};
            if (l.out_qw == 2) {
                std::memcpy(w + 4, k.data.data() + 16 + v * stride, 32);
                for (int c = 0; c < 3; ++c) std::memcpy(&w[c], &r[v].stq[c], 4);  // a sombra não tem STQ
            } else {
                std::memcpy(w, k.data.data() + 16 + v * stride, 48);
            }
            for (int c = 0; c < 3; ++c) {
                float f;
                std::memcpy(&f, &w[c], 4);
                // Em ulps, ou em valor absoluto: o -32 do passo 1 deixa o STQ com a precisão absoluta de um float perto
                // de 32 (2^-18), e a VU trunca onde o PC arredonda; perto de zero isso são centenas de ulps.
                const uint32_t u = ulps(f, r[v].stq[c]);
                const float ad = std::fabs(f - r[v].stq[c]);
                if (u > 4 && ad > 1.0f / 65536.0f) ++s.stq_diff;
                if (u > s.stq_max_ulps && u < 0x7f000000u) s.stq_max_ulps = u;
                if (ad > s.stq_max_abs) s.stq_max_abs = ad;
            }
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int32_t(w[4 + c]) - r[v].rgba[c]);
                if (d) ++s.rgba_diff;
                if (d > s.rgba_max) s.rgba_max = d;
                if (d > 1 && verify_debug())
                    std::printf("DBG cor %s kind %d pass %d v %zu/%zu c %d ps2 %08x ref %08x\n", l.name, l.kind, pass, v,
                                r.size(), c, w[4 + c], uint32_t(r[v].rgba[c]));
            }
            for (int c = 0; c < 3; ++c) {
                // X e Y: até 1 (1/16 de pixel). Z: a janela de vista o leva a ~2^24 (x 16), onde 1 ulp do float já são
                // dezenas de unidades; a tolerância é relativa (2^-21, uns 4 ulps).
                const int64_t d = std::llabs(int64_t(int32_t(w[8 + c])) - int64_t(r[v].xyzf[c]));
                const int64_t tol = c < 2 ? 1 : std::max<int64_t>(1, std::llabs(int64_t(r[v].xyzf[c])) >> 21);
                if (d > tol) ++s.xyz_diff;
                if (d > tol && verify_debug())
                    std::printf("DBG xyz %s kind %d pass %d v %zu c %d ps2 %08x ref %08x\n", l.name, l.kind, pass, v, c,
                                w[8 + c], uint32_t(r[v].xyzf[c]));
                if (c < 2 && d > s.xyz_max) s.xyz_max = int(d);
                if (c == 2 && d > s.z_max) s.z_max = int(std::min<int64_t>(d, 0x7fffffff));
            }
            const bool adc_ps2 = (w[11] & 0x8000) != 0, adc_ref = (r[v].xyzf[3] & 0x8000) != 0;
            if (adc_ps2 && !adc_ref) ++s.adc_clip;
            if (adc_ref && !adc_ps2) ++s.adc_extra;
            if (adc_ref && adc_ps2 && l.kind == kCarGlass && r[v].xyzf[3] == 0x8000 && w[11] == 0x8000) ++s.adc_back;
            // A névoa: o w sem o ADC; nos vértices com ADC o microprograma mexe nos bits de baixo (o AND 0xbffe e os
            // somatórios do recorte), então só os sem ADC dos dois lados.
            if (!adc_ps2 && !adc_ref && std::abs(int32_t(w[11] & 0x7fff) - int32_t(r[v].xyzf[3] & 0x7fff)) > 16)
                ++s.fog_diff;
            ++s.vertices;
        }
        ++pass;
    }
    while (pass < kCarMaxPasses && ref[pass].empty()) ++pass;
    if (pass < kCarMaxPasses) ++s.missing_kicks;
}

}  // namespace

void car_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CarBatch& b,
                      CarVerifyStats& s) {
    verify_once(code, mem, top, b, s);
    // Na carga as zonas de dano e os SwitchData são zero (o carro inteiro, as lâmpadas apagadas): o vidro e as luzes
    // rodam de novo com valores de jogo na cópia da memória da VU, para a conferência passar pelos dois caminhos (o vidro
    // quebrado, com o w 127 e o dano que move e escurece; a lâmpada acesa pela metade).
    const int kind = b.layout->kind;
    if (kind != kCarGlass && kind != kCarSwitch && kind != kCarGouraud && kind != kCarShadow) return;
    auto m2 = std::make_unique<VuMemory>(mem);
    auto put = [&](int qw, int c, float v) { std::memcpy(&m2->w[((qw & 1023) << 2) | c], &v, 4); };
    if (kind == kCarGouraud) {
        // O Gouraud com dano (escurece e move) e quatro luzes pontuais perto do carro.
        for (int z = 0; z < 16; ++z) {
            put(0x56 + z, 0, 0.125f * float(z & 3));
            put(0x56 + z, 1, 0.25f);
            put(0x56 + z, 2, 0.75f);
        }
        for (int L = 0; L < 4; ++L) {
            put(0x40 + 0, L, -0.5f + 0.4f * float(L));
            put(0x40 + 1, L, 0.2f * float(L));
            put(0x40 + 2, L, 1.0f - 0.6f * float(L));
            put(0x40 + 3, L, 20.0f + 30.0f * float(L));
            for (int c = 0; c < 3; ++c) put(0x44 + L, c, 0.1f * float(1 + ((L + c) % 3)));
        }
    } else if (kind == kCarShadow) {
        // A sombra numa câmera em perspectiva que corta parte do modelo (o w 2047 do recorte).
        static const float kMvp[16] = {1.4f, 0.0f, 0.1f, 0.0f, 0.0f, 1.8f, 0.3f, 0.0f, 0.2f, -0.1f, 1.0f, 1.0f,
                                       -0.3f, -0.2f, 1.2f, 1.5f};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) put(int(top) + b.layout->mvp + r, c, kMvp[r * 4 + c]);
    } else if (kind == kCarGlass) {
        for (int z = 0; z < 16; ++z) {
            put(0x56 + z, 0, 0.125f * float(z & 3));
            put(0x56 + z, 1, 0.25f);
            put(0x56 + z, 2, 0.75f);
            m2->w[((0x56 + z) << 2) | 3] = (z & 1) ? 127u : 0u;
        }
    } else {
        const int t = int(top) + b.layout->switch_data;
        put(t, 0, 0.0f);
        put(t, 1, 0.6f);
        put(t, 2, 0.9f);
        put(t, 3, 0.5f);
        if (m2->f(int(top) + b.layout->switch_param, 0) == 0.0f) put(int(top) + b.layout->switch_param, 0, 1.0f);
    }
    CarBatch b2;
    if (!car_decode_batch(*m2, top, *b.layout, b2)) return;
    ++s.variant_batches;
    verify_once(code, *m2, top, b2, s);
}

}  // namespace drv
