// A luneta do fuzil de Bond (driving/render/sniper_zoom.h), portada do MIPS do DRIVING.ELF (RSniperZoom.obj). As contas
// seguem a ordem das instruções com a aritmética do EE (platform/ps2_float.h); o seno e o cosseno são os da libm do
// jogo (sinf/cosf: aqui o nf::fm, como no resto do port do carro; o nf_driving_check os põe nos dois lados).

#include "driving/render/sniper_zoom.h"

#include <cstring>

#include "platform/fmath.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {
// Os raios dos anéis (0x3353f8, 0x3353fc, 0x335400) e os cinzas das bordas (0x335404 o de fora, 0x335408 o do meio).
constexpr float kRadiusInner = 65.0f, kRadiusMiddle = 150.0f, kRadiusOuter = 530.0f;
constexpr uint32_t kGreyOuter = 0x00, kGreyMiddle = 0xb9;
}  // namespace

void RSniperZoom_Construct(SniperZoomData& z) {
    std::memset(&z, 0, sizeof z);
    z.vtable = 0x3934a0u;
    // ROffscreenBuffer::ROffscreenBuffer(64, 64, 24, 0, 0) e USimpleTexturedMaterial: do renderizador.
    std::memset(z.colour_inner, 0, sizeof z.colour_inner);
    std::memset(z.colour_outer, 0, sizeof z.colour_outer);
    z.on = 0;
    z.alpha = 0xff;
}

bool RSniperZoom_GrabBackBuffer(SniperZoomData& z, uint32_t mission_cinematic) {
    if (mission_cinematic != 0) z.on = 0;
    // ROffscreenBuffer::Lock e MakeTexCopy(0,5, a imagem subamostrada do RPostProcessing, 4): do renderizador.
    return z.on != 0;
}

void RSniperZoom_RingVertex(const SniperZoomView& v, float radius, float t, float pos[4], float uv[4]) {
    const float w = ee::cvt_s(v.screen_w), h = ee::cvt_s(v.screen_h);
    const float a = ee::mul(t, ps2::ffloat(0x40c90fdbu));  // 2 pi
    const float cx = ee::add(ee::mul(w, 0.5f), v.cursor_x);
    const float s = nf::fm::sin(a);
    const float x = ee::add(cx, ee::mul(radius, s));
    pos[0] = x;
    const float cy = ee::add(ee::mul(h, 0.5f), v.cursor_y);
    const float c = nf::fm::cos(a);
    const float y = ee::add(cy, ee::mul(radius, c));
    pos[2] = 1.0f;
    pos[3] = 1.0f;
    pos[1] = y;
    uv[0] = ee::div(pos[0], w);
    uv[3] = 1.0f;
    uv[2] = 1.0f;
    uv[1] = ee::div(pos[1], h);
}

void RSniperZoom_SetupVerts(SniperZoomData& z, const SniperZoomView& v) {
    const uint32_t a = (z.alpha >> 1) << 24;
    const uint32_t mid = kGreyMiddle >> 1, out = kGreyOuter >> 1;
    const uint32_t mid_colour = a | mid << 16 | mid << 8 | mid;
    const uint32_t out_colour = a | out << 16 | out << 8 | out;
    for (int i = 0; i < kSniperZoomVerts; ++i) {
        const bool odd = (i & 1) != 0;
        const float t = ee::div(ee::cvt_s(i / 2), 20.0f);
        RSniperZoom_RingVertex(v, odd ? kRadiusMiddle : kRadiusInner, t, z.pos_inner[i], z.uv_inner[i]);
        z.colour_inner[i] = odd ? mid_colour : 0x007f7f7fu;
        RSniperZoom_RingVertex(v, odd ? kRadiusOuter : kRadiusMiddle, t, z.pos_outer[i], z.uv_outer[i]);
        z.colour_outer[i] = odd ? out_colour : mid_colour;
    }
}

bool RSniperZoom_Draw(SniperZoomData& z, const SniperZoomView& v) {
    if (z.on == 0) return false;
    // O material: a mistura 0x8000000044 ((Cs - Cd) x As + Cd), sem o FIX; o teste de Z 1. A textura é a cópia do quadro
    // (+0x14); os dois USimpleTexturedMaterial::Draw(5, 42): primeiro o anel de fora, depois o de dentro.
    RSniperZoom_SetupVerts(z, v);
    // ROffscreenBuffer::Unlock: do renderizador.
    return true;
}

SniperZoomData& sniper_zoom_object() {
    static SniperZoomData z = [] {
        SniperZoomData d;
        RSniperZoom_Construct(d);
        return d;
    }();
    return z;
}

}  // namespace drv
