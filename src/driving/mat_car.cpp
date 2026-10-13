// A família MatCar texturizada (mat_car.h): o leiaute dos lotes e a referência na CPU do microprograma da VU1.

#include "driving/mat_car.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace drv {

namespace {

// Os leiautes, dos endereços "IADDIU viN, vi14, X" e "LQ/SQ X(vi14)" de cada microprograma.
const CarMethodLayout kLayouts[] = {
    // name, passes, packed, max, count, mvp, world, props, tar, spec, refl, split, pos, stq, normal, dmgvec, dmgnrm,
    // colour, gif, out, state, env_state
    {"MatCarTexturePacked", kPassBase | kPassSpec | kPassRefl, true, 44, 0, 1, 5, 9, 0x0a, 0x11, 0x18, 0x1f, 0x22,
     0x4e, 0x7a, 0xa6, 0xd2, 0x11b, 0x147, 0x148, 0x10f, 0x107},
    {"MatCarChrome", kPassBase | kPassSpec | kPassRefl, true, 44, 0, 1, 5, 9, 0x0a, 0x11, 0x18, 0x1f, 0x22, 0x4e,
     0x7a, 0xa6, 0xd2, 0x11b, 0x147, 0x148, 0x10f, 0x107},
    {"MatCarTextureDullPacked", kPassBase, true, 44, 0, 1, 5, 9, 0x0a, 0, 0, 0x11, 0x14, 0x40, 0x6c, 0x98, 0xc4,
     0x101, 0x12d, 0x12e, 0xf9, 0},
    // Sem o passo 1: a normal vem antes do STQ, e a passada 2 chuta o SpecMapState (0x106) em vez do EnvMapState.
    {"MatCarDash", kPassBase | kPassSpec, false, 44, 0, 1, 5, 9, 0x0a, 0x11, 0, 0x1f, 0x22, 0x7a, 0x4e, 0xa6, 0xd2,
     0x11b, 0x147, 0x148, 0xfe, 0x106},
    // O vidro: o "tar" é o Texture (a passada 2), o "state" o GlassDmgState (o PRIM do teste faixa/lista em 0xfe), o
    // DmgTexture em 0x23 e a ModelView em 0x1f; os SplitReg em 0x2a (os parâmetros do método: Parameters, MyMatrix,
    // WorldMatrix, LightingProperties, Texture, SpecularMap, ReflectionMap2, ModelView, DmgTexture, SplitReg1-3,
    // Coordinates, Normals, STQ, DmgVec, DmgNrm, State 0xf5, GlassDmgState 0xfd, EnvMapState 0x105).
    {"MatCarGlassPacked", kPassBase | kPassSpec | kPassRefl | kPassGlassRefl, true, 40, 0, 1, 5, 9, 0x0a, 0x11, 0x18,
     0x2a, 0x2d, 0x7d, 0x55, 0xa5, 0xcd, 0x11a, 0x142, 0x143, 0xfd, 0x105, kCarGlass, 0x23, 0x1f},
    // As luzes: o SwitchData em 0x11 empurra o resto um qword (SplitReg 0x12); o SwitchParam em 0xf1, depois do DmgNrm.
    {"MatCarTextureSwitchPacked", kPassBase, true, 44, 0, 1, 5, 9, 0x0a, 0, 0, 0x12, 0x15, 0x41, 0x6d, 0x99, 0xc5,
     0x107, 0x133, 0x134, 0xfb, 0, kCarSwitch, 0, 0, 0x11, 0xf1},
};

float bits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
const float kI1 = bits(0x3a7ffff7);  // ~1/1024 (STQ, dano)
const float kI2 = bits(0x3c008080);  // 2/255 (normais)

// FTOI0 / FTOI4: trunca para zero, satura no int32.
int32_t ftoi(float v, int frac) {
    const double d = double(v) * double(1 << frac);
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return int32_t(-2147483647 - 1);
    return int32_t(d);
}

}  // namespace

const CarMethodLayout* car_method_layout(const char* name) {
    if (!name) return nullptr;
    for (const CarMethodLayout& l : kLayouts)
        if (std::strcmp(l.name, name) == 0) return &l;
    return nullptr;
}

int car_pass_stq(const CarMethodLayout& l, int pass) {
    if (l.kind == kCarGlass) return pass <= 1 ? 0 : pass - 1;
    return pass;
}

int car_pass_alpha(const CarMethodLayout& l, int pass) {
    if (l.kind == kCarGlass) return pass == 0 ? 1 : 2;
    return 0;
}

void car_switch_colour(const float sw[4], const float param[4], int32_t col[4]) {
    // MatCarTextureSwitchPacked 0xf1-0x112 (o microprograma): vf08 = (s.z, s.z, s.z, s.y) (ADDz.xyz e ADDy.w sobre zero),
    // x 255 (MULi), x k.x (MULx); vf07.w = 1 - s.w (SUB.w de vf00); a cor ITOF0 x (1 - s.w) no ACC (MULAw) + vf08 x s.w
    // (MADDw), MINIi 255 e FTOI0, as quatro componentes.
    float target[4] = {sw[2], sw[2], sw[2], sw[1]};
    for (float& t : target) t = (t * 255.0f) * param[0];
    const float keep = 1.0f - sw[3];
    for (int c = 0; c < 4; ++c) {
        const float v = float(col[c]) * keep + target[c] * sw[3];
        col[c] = ftoi(std::min(v, 255.0f), 0);
    }
}

bool car_glass_backface(const float mv[4][4], const float pos[3], const float n[3]) {
    // MatCarGlassPacked 0x17d-0x1a9 (o microprograma): MULAx/MADDAy/MADDz da normal, a posição com o MADDw da linha 3,
    // o MUL componente a componente e a soma no z (z + x, + y, + w = 0, - 0); o FMAND do sinal do z.
    float nc[3], pc[3];
    for (int c = 0; c < 3; ++c) {
        nc[c] = mv[0][c] * n[0] + mv[1][c] * n[1] + mv[2][c] * n[2];
        pc[c] = mv[0][c] * pos[0] + mv[1][c] * pos[1] + mv[2][c] * pos[2] + mv[3][c];
    }
    const float d = (nc[2] * pc[2] + nc[0] * pc[0]) + nc[1] * pc[1];
    return !std::signbit(d);
}

void CarConstants::write(VuMemory& m) const {
    auto put = [&](int qw, const float v[4]) { std::memcpy(&m.w[qw * 4], v, 16); };
    put(0, vp_offset);
    put(1, vp_scale);
    for (int i = 0; i < 4; ++i) put(0x40 + i, point_pos[i]);
    for (int i = 0; i < 4; ++i) put(0x44 + i, point_col[i]);
    put(0x48, fog);
    for (int i = 0; i < 3; ++i) put(0x49 + i, light_dir_t[i]);
    for (int i = 0; i < 3; ++i) put(0x4c + i, light_col[i]);
    put(0x4f, ambient);
    for (int i = 0; i < 4; ++i) put(0x50 + i, spec_mat[i]);
    put(0x54, refl_info1);
    put(0x55, refl_info2);
    for (int i = 0; i < 16; ++i) put(0x56 + i, damage[i]);
}

bool car_decode_batch(const VuMemory& m, uint32_t top, const CarMethodLayout& l, CarBatch& b) {
    const int t = int(top);
    b.layout = &l;
    b.count = m.i(t + l.count, 0);
    if (b.count <= 0 || b.count > l.max_verts) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            b.mvp[r][c] = m.f(t + l.mvp + r, c);
            b.world[r][c] = m.f(t + l.world + r, c);
        }
    for (int c = 0; c < 4; ++c) b.props[c] = m.f(t + l.props, c);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) b.modelview[r][c] = l.modelview ? m.f(t + l.modelview + r, c) : 0.0f;
    for (int c = 0; c < 4; ++c) {
        b.switch_data[c] = l.switch_data ? m.f(t + l.switch_data, c) : 0.0f;
        b.switch_param[c] = l.switch_param ? m.f(t + l.switch_param, c) : 0.0f;
    }
    auto packet = [&](int at, int qw, eagl::GsRegs& regs) {
        uint32_t buf[16 * 4];
        for (int i = 0; i < qw * 4; ++i) buf[i] = m.u(t + at + i / 4, i % 4);
        eagl::gs_read_packet(reinterpret_cast<const uint8_t*>(buf), qw, regs);
    };
    for (int p = 0; p < kCarMaxPasses; ++p) b.pass[p] = eagl::GsRegs();
    if (l.kind == kCarGlass) {
        // O GlassDmgState com o DmgTexture e depois com o Texture; o EnvMapState com o especular e com o reflexo.
        packet(l.state, 8, b.pass[0]);
        packet(l.tar_dmg, 7, b.pass[0]);
        packet(l.state, 8, b.pass[1]);
        packet(l.tar, 7, b.pass[1]);
        packet(l.env_state, 8, b.pass[2]);
        packet(l.tar_spec, 7, b.pass[2]);
        packet(l.env_state, 8, b.pass[3]);
        packet(l.tar_refl, 7, b.pass[3]);
    } else {
        packet(l.state, 8, b.pass[0]);
        packet(l.tar, 7, b.pass[0]);
        if (l.passes & kPassSpec) {
            packet(l.env_state, 8, b.pass[1]);
            packet(l.tar_spec, 7, b.pass[1]);
        }
        if (l.passes & kPassRefl) {
            packet(l.env_state, 8, b.pass[2]);
            packet(l.tar_refl, 7, b.pass[2]);
        }
    }
    b.strip = (m.u(t + l.state + 1, 0) & 7) != 3;
    for (int i = 0; i < 12; ++i) b.split[i] = int(m.u(t + l.split + i / 4, i % 4) & 0xFFFF);
    b.v.resize(size_t(b.count));
    for (int i = 0; i < b.count; ++i) {
        CarVertexIn& v = b.v[size_t(i)];
        for (int c = 0; c < 3; ++c) v.pos[c] = m.u(t + l.pos + i, c);
        for (int c = 0; c < 3; ++c) v.stq[c] = m.u(t + l.stq + i, c);
        for (int c = 0; c < 3; ++c) v.normal[c] = m.u(t + l.normal + i, c);
        for (int c = 0; c < 4; ++c) v.dmgvec[c] = m.u(t + l.dmgvec + i, c);
        for (int c = 0; c < 4; ++c) v.dmgnrm[c] = m.u(t + l.dmgnrm + i, c);
    }
    return true;
}

CarVertexWork car_convert(const CarMethodLayout& l, const CarVertexIn& v) {
    CarVertexWork o{};
    for (int c = 0; c < 3; ++c) o.pos[c] = bits(v.pos[c]);
    o.zone = v.dmgnrm[3];
    if (l.packed) {
        // ITOF0 e depois ACC = x * I, resultado = ACC + (-32 ou -1) x 1 (MULAi + MADDw).
        for (int c = 0; c < 2; ++c) o.stq[c] = float(int32_t(v.stq[c])) * kI1 + -32.0f;
        o.stq[2] = 1.0f;
        for (int c = 0; c < 3; ++c) o.normal[c] = float(int32_t(v.normal[c])) * kI2 + -1.0f;
        for (int c = 0; c < 4; ++c) o.dmgvec[c] = float(int32_t(v.dmgvec[c])) * kI1 + -32.0f;
        for (int c = 0; c < 3; ++c) o.dmgnrm[c] = float(int32_t(v.dmgnrm[c])) * kI1 + -32.0f;
    } else {
        for (int c = 0; c < 3; ++c) o.stq[c] = bits(v.stq[c]);
        for (int c = 0; c < 3; ++c) o.normal[c] = bits(v.normal[c]);
        for (int c = 0; c < 4; ++c) o.dmgvec[c] = bits(v.dmgvec[c]);
        for (int c = 0; c < 3; ++c) o.dmgnrm[c] = bits(v.dmgnrm[c]);
    }
    return o;
}

void car_reference(const CarBatch& b, const CarConstants& k, std::vector<CarVertexOut> out[kCarMaxPasses]) {
    const int n = b.count;
    const int passes = b.layout ? b.layout->passes : kPassBase;
    const bool glass = b.layout && b.layout->kind == kCarGlass;
    for (int p = 0; p < kCarMaxPasses; ++p) out[p].assign(size_t((passes >> p) & 1 ? n : 0), CarVertexOut{});

    // Passos 0-2: a cor de partida (127, 127, 127, 128) = vf00 + 127, a conversão e o dano.
    struct Work {
        float pos[3], stq[3], n[3], alpha_f;
        int32_t col[4];
        uint32_t zone_w;  // a palavra w crua da zona de dano (o alfa do vidro)
    };
    std::vector<Work> w(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const CarVertexWork v = b.layout ? car_convert(*b.layout, b.v[size_t(i)]) : CarVertexWork{};
        Work& o = w[size_t(i)];
        const float* z = k.damage[v.zone & 15];
        std::memcpy(&o.zone_w, &z[3], 4);
        for (int c = 0; c < 3; ++c) o.stq[c] = v.stq[c];
        for (int c = 0; c < 3; ++c) o.pos[c] = v.pos[c] + v.dmgvec[c] * z[0];
        for (int c = 0; c < 3; ++c) o.n[c] = v.normal[c] + v.dmgnrm[c] * z[0];
        const float s_rgb = 1.0f - v.dmgvec[3] * z[1];
        const float s_a = 1.0f - v.dmgvec[3] * z[2];
        for (int c = 0; c < 3; ++c) o.col[c] = ftoi(std::max(127.0f * s_rgb, 0.0f), 0);
        o.alpha_f = std::max(128.0f * s_a, 0.0f);
        o.col[3] = 128;
    }

    // Passo 3: as luzes direcionais.
    float r[3][3];
    for (int i = 0; i < 3; ++i) {
        const float len2 = b.world[i][0] * b.world[i][0] + b.world[i][1] * b.world[i][1] + b.world[i][2] * b.world[i][2];
        const float inv = 1.0f / std::sqrt(len2);
        for (int c = 0; c < 3; ++c) r[i][c] = b.world[i][c] * inv;
    }
    float c0[3], amb[3];
    for (int c = 0; c < 3; ++c) {
        c0[c] = k.light_col[0][c] * b.props[1];
        amb[c] = k.ambient[c] * b.props[0];
    }
    float A[3][3];
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c)
            A[i][c] = k.light_dir_t[0][c] * r[i][0] + k.light_dir_t[1][c] * r[i][1] + k.light_dir_t[2][c] * r[i][2];
    for (int i = 0; i < n; ++i) {
        Work& o = w[size_t(i)];
        float d[3];
        for (int c = 0; c < 3; ++c) d[c] = std::max(A[0][c] * o.n[0] + A[1][c] * o.n[1] + A[2][c] * o.n[2], 0.0f);
        for (int c = 0; c < 3; ++c) {
            float l = c0[c] * d[0] + k.light_col[1][c] * d[1] + k.light_col[2][c] * d[2] + amb[c];
            l = l * float(o.col[c]);
            o.col[c] = ftoi(std::min(l, 255.0f), 0);
        }
    }

    // Passo 4: as luzes pontuais.
    for (int i = 0; i < n; ++i) {
        Work& o = w[size_t(i)];
        float pw[3];
        for (int c = 0; c < 3; ++c)
            pw[c] = b.world[0][c] * o.pos[0] + b.world[1][c] * o.pos[1] + b.world[2][c] * o.pos[2] + b.world[3][c];
        float att[4];
        for (int L = 0; L < 4; ++L) {
            const float dx = k.point_pos[0][L] - pw[0], dy = k.point_pos[1][L] - pw[1], dz = k.point_pos[2][L] - pw[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            att[L] = std::max(d2 * -k.point_pos[3][L] + 255.0f, 0.0f);
        }
        for (int c = 0; c < 3; ++c) {
            float l = float(o.col[c]);
            l = l + k.point_col[0][c] * att[0];
            l = l + k.point_col[1][c] * att[1];
            l = l + k.point_col[2][c] * att[2];
            l = l + k.point_col[3][c] * att[3];
            o.col[c] = ftoi(std::min(l, 255.0f), 0);
        }
    }

    // As luzes: o passo do SwitchData.
    if (b.layout && b.layout->kind == kCarSwitch)
        for (int i = 0; i < n; ++i) car_switch_colour(b.switch_data, b.switch_param, w[size_t(i)].col);

    // Passo 5: a transformação, a névoa e a primeira passada.
    std::vector<bool> adc(size_t(n) + 2, false);
    for (int s : b.split) {
        const int v = s / 3;
        if (v < n) adc[size_t(v)] = true;
        if (v + 1 < n) adc[size_t(v) + 1] = true;
    }
    for (int i = 0; i < n; ++i) {
        const Work& o = w[size_t(i)];
        float clip[4];
        for (int c = 0; c < 4; ++c)
            clip[c] = b.mvp[0][c] * o.pos[0] + b.mvp[1][c] * o.pos[1] + b.mvp[2][c] * o.pos[2] + b.mvp[3][c];
        const float ww = std::min(std::max(clip[3], k.fog[0]), k.fog[2]);
        const float dw = ww - k.fog[0];
        const float q = 1.0f / clip[3];
        float scr[3];
        for (int c = 0; c < 3; ++c) scr[c] = k.vp_offset[c] + (clip[c] * k.vp_scale[c]) * q;
        const float f = 255.0f - k.fog[3] * dw;
        CarVertexOut& v = out[0][size_t(i)];
        v.stq[0] = o.stq[0] * q;
        v.stq[1] = o.stq[1] * q;
        v.stq[2] = o.stq[2] * q;  // o z do STQ é 1 nos métodos "Packed" (o passo 1) e o do arquivo no Dash
        for (int c = 0; c < 4; ++c) v.rgba[c] = o.col[c];
        if (glass) v.rgba[3] = int32_t(o.zone_w);  // o alfa do vidro: o w da zona (SQI.w vf18)
        for (int c = 0; c < 3; ++c) v.xyzf[c] = ftoi(scr[c], 4);
        v.xyzf[3] = ftoi(f, 4);
        v.adc = adc[size_t(i)];
        // O vidro: a face de trás grava 0x8000 no w (ISW.w) antes do AND dos SplitReg.
        if (glass && car_glass_backface(b.modelview, o.pos, o.n)) {
            v.xyzf[3] = 0x8000;
            v.adc = true;
        }
        if (adc[size_t(i)]) v.xyzf[3] = ((v.xyzf[3] & 0xFFFF) & 0xBFFE) | 0x8000;
    }
    // O alfa das passadas seguintes do vidro: int(127 - w) (ITOF0.w, MULi -1, ADDi 127, FTOI0.w).
    std::vector<int32_t> inv_w(size_t(n), 128);
    if (glass)
        for (int i = 0; i < n; ++i) inv_w[size_t(i)] = ftoi(float(int32_t(w[size_t(i)].zone_w)) * -1.0f + 127.0f, 0);
    // No vidro a passada 1 é o Texture (a base com o outro alfa); o especular e o reflexo vêm em 2 e 3.
    const int ps = glass ? 2 : 1, pr = glass ? 3 : 2;
    if (glass) {
        out[1] = out[0];
        for (int i = 0; i < n; ++i) out[1][size_t(i)].rgba[3] = inv_w[size_t(i)];
    }
    if (!((passes >> ps) & 1)) return;

    // Passo 6: o especular (mapa esférico das normais).
    float E[3][4];
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 4; ++c)
            E[i][c] = (k.spec_mat[0][c] * b.world[i][0] + k.spec_mat[1][c] * b.world[i][1] +
                       k.spec_mat[2][c] * b.world[i][2] + k.spec_mat[3][c] * b.world[i][3]) *
                      k.refl_info2[2];
    for (int i = 0; i < n; ++i) {
        const Work& o = w[size_t(i)];
        CarVertexOut& v = out[ps][size_t(i)];
        v = out[0][size_t(i)];
        v.stq[0] = (E[0][0] * o.n[0] + E[1][0] * o.n[1] + E[2][0] * o.n[2]) + 0.5f;
        v.stq[1] = (E[0][1] * o.n[0] + E[1][1] * o.n[1] + E[2][1] * o.n[2]) + 0.5f;
        v.stq[2] = 1.0f;
        for (int c = 0; c < 3; ++c) v.rgba[c] = ftoi(b.props[2] * o.alpha_f, 0);
        if (glass) v.rgba[3] = inv_w[size_t(i)];
    }

    if (!((passes >> pr) & 1)) return;

    // Passo 7: o reflexo.
    float Wy[3][4];
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 4; ++c) Wy[i][c] = b.world[i][c] * k.refl_info2[1];
    for (int i = 0; i < n; ++i) {
        const Work& o = w[size_t(i)];
        CarVertexOut& v = out[pr][size_t(i)];
        v = out[0][size_t(i)];
        if (glass) v.rgba[3] = inv_w[size_t(i)];
        float vv[3];
        for (int c = 0; c < 3; ++c) vv[c] = o.pos[c] * k.refl_info2[0] + o.n[c];
        float a[3];
        for (int c = 0; c < 3; ++c) a[c] = Wy[0][c] * vv[0] + Wy[1][c] * vv[1] + Wy[2][c] * vv[2];
        v.stq[0] = a[0] + 0.5f;
        v.stq[1] = a[2] + 0.5f;
        v.stq[2] = 1.0f;
        const float gate = std::max(a[1], 0.0f) * 100000.0f;
        for (int c = 0; c < 3; ++c) v.rgba[c] = ftoi(std::min(b.props[3] * o.alpha_f, gate), 0);
    }
}

void car_triangles(const CarBatch& b, std::vector<uint16_t>& tris, bool closing_first) {
    const int n = b.count;
    std::vector<bool> adc(size_t(n) + 2, false);
    for (int s : b.split) {
        const int v = s / 3;
        if (v < n) adc[size_t(v)] = true;
        if (v + 1 < n) adc[size_t(v) + 1] = true;
    }
    auto put = [&](int v) {
        if (closing_first) {
            tris.push_back(uint16_t(v));
            tris.push_back(uint16_t(v - 2));
            tris.push_back(uint16_t(v - 1));
        } else {
            tris.push_back(uint16_t(v - 2));
            tris.push_back(uint16_t(v - 1));
            tris.push_back(uint16_t(v));
        }
    };
    if (b.strip) {
        for (int v = 2; v < n; ++v)
            if (!adc[size_t(v)]) put(v);
    } else {
        for (int v = 2; v < n; v += 3)
            if (!adc[size_t(v)]) put(v);
    }
}

}  // namespace drv
