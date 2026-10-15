// As entradas por quadro dos render methods do mundo (driving/light_manager.h): o RTimeData e o bloco de luzes do
// RLightManager, com a aritmética do EE (platform/ps2_float.h) e a da VU0 em macro (platform/vu0_macro.h).

#include "driving/light_manager.h"

#include <cstdlib>
#include <cstring>

#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

RTimeData& rtime_data() {
    static RTimeData t;
    return t;
}

// [driving] 0x001d1ef8 RTimeData::Update
void RTimeData_Update(int32_t sim_steps) {
    RTimeData& t = rtime_data();
    const uint32_t v = uint32_t(sim_steps);
    t.time = v;
    if (sim_steps >= 0) {
        t.time_data[0] = ee::cvt_s(sim_steps);
    } else {  // sem sinal: a metade (com o bit de baixo) convertida e dobrada
        const float h = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
        t.time_data[0] = ee::add(h, h);
    }
    t.caustic_time = ee::mul(t.caustic[0], t.time_data[0]);
}

// [driving] 0x002d11c8 BuildRotate. Divergência: o sinf/cosf do newlib é o nf::fm::sin/cos.
void BuildRotate(float m[16], float degrees, float x, float y, float z) {
    const float turns = ee::mul(degrees, ps2::ffloat(0x3b360b61));  // x 1/360
    if (ee::eq(turns, 0.0f)) {
        const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m, id, 64);
        return;
    }
    const float rad = ee::mul(turns, ps2::ffloat(0x40c90fdb));  // x 2 pi
    const float s = nf::fm::sin(rad), c = nf::fm::cos(rad);
    const float len2 = ee::add(ee::add(ee::mul(x, x), ee::mul(y, y)), ee::mul(z, z));
    const float inv = ee::rsqrt(1.0f, len2);
    m[15] = 1.0f;
    x = ee::mul(x, inv);
    z = ee::mul(z, inv);
    const float t = ee::sub(1.0f, c);
    m[14] = 0.0f;
    m[3] = 0.0f;
    y = ee::mul(y, inv);
    m[7] = 0.0f;
    const float sz = ee::mul(s, z);
    m[11] = 0.0f;
    const float tz = ee::mul(t, z);
    m[12] = 0.0f;
    const float tx = ee::mul(t, x);
    m[13] = 0.0f;
    const float ty = ee::mul(t, y);
    const float sx = ee::mul(s, x);
    const float tzz = ee::mul(tz, z), txz = ee::mul(tx, z), tyz = ee::mul(ty, z);
    const float tzx = ee::mul(tz, x), txx = ee::mul(tx, x), tyx = ee::mul(ty, x);
    const float sy = ee::mul(s, y);
    const float tzy = ee::mul(tz, y), txy = ee::mul(tx, y), tyy = ee::mul(ty, y);
    m[8] = ee::add(tzx, sy);
    m[4] = ee::sub(tyx, sz);
    m[9] = ee::sub(tzy, sx);
    m[10] = ee::add(tzz, c);
    m[0] = ee::add(txx, c);
    m[1] = ee::add(txy, sz);
    m[2] = ee::sub(txz, sy);
    m[5] = ee::add(tyy, c);
    m[6] = ee::add(tyz, sx);
}

// [driving] 0x002d1920 TransformPoint
void TransformPoint(const float m[16], const float in[4], float out[4]) {
    uint32_t v[4], rows[4][4], acc[4] = {0, 0, 0, 0}, o[4] = {0, 0, 0, 0};
    std::memcpy(v, in, 16);
    std::memcpy(rows, m, 64);
    mulabc(acc, rows[0], v, BX, XYZW);
    maddabc(acc, rows[1], v, BY, XYZW);
    maddabc(acc, rows[2], v, BZ, XYZW);
    maddbc(o, acc, rows[3], v, BW, XYZW);
    std::memcpy(out, o, 16);
}

// [driving] 0x001b0038 (o RLightManager::SetLightDirections do nightfire-cxbx)
void RLightManager_SetLightDirections(LightBlock& block, const float angles[6]) {
    for (int k = 0; k < 3; ++k) {
        float v[4] = {0.0f, 0.0f, 1.0f, 0.0f};
        float m[16];
        BuildRotate(m, angles[k], 1.0f, 0.0f, 0.0f);
        TransformPoint(m, v, v);
        BuildRotate(m, angles[k + 3], 0.0f, 1.0f, 0.0f);
        TransformPoint(m, v, v);
        block.dir[0][k] = v[0];
        block.dir[1][k] = v[1];
        block.dir[2][k] = v[2];
    }
}

// [driving] 0x001b0158 RLightManager::RefreshLightBlocks: as direções dos quatro conjuntos; a "Dual Hemi" do World e do
// Character aponta para cima (0, 1, 0).
void RLightManager_RefreshLightBlocks(RLightManagerLights& m) {
    for (int k = 0; k < 4; ++k) RLightManager_SetLightDirections(m.blocks[k], m.angles[k]);
    for (int k = 0; k < 2; ++k) {
        m.blocks[k].dir[0][2] = 0.0f;
        m.blocks[k].dir[1][2] = 1.0f;
        m.blocks[k].dir[2][2] = 0.0f;
    }
}

namespace {

// "Chave{Conjunto}=a,b,c,d": os n valores (o sscanf "%f" do newlib: strtod e float); false sem a chave.
bool tun_values(const std::string& text, const std::string& key, float* out, int n) {
    size_t at = 0;
    for (;;) {
        at = text.find(key, at);
        if (at == std::string::npos) return false;
        if (at == 0 || text[at - 1] == '\n' || text[at - 1] == '\r') break;
        at += key.size();
    }
    const char* c = text.c_str() + at + key.size();
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = float(std::strtod(c, &end));
        if (end == c) break;
        c = end;
        if (*c == ',') ++c;
    }
    return true;
}

}  // namespace

// [driving] 0x001ae630 RLightManager::RLightManager (as cores e os ângulos do "Render:Lighting" e o RefreshLightBlocks)
void RLightManager_Construct(RLightManagerLights& m, const std::string& tun_text) {
    m = RLightManagerLights{};
    static const char* const kSets[4] = {"World", "Character", "Unused1", "Unused2"};
    // dbattrib_argb com o passo 0x70: "Ambient Sky" (+0xe0), "Ambient Ground" (+0xd0), "Light 1" (+0xb0), "Light 2"
    // (+0xc0); dbattrib_float com o passo 0x18: os ângulos (+0x24c, +0x240, +0x250, +0x244, +0x254, +0x248).
    for (int k = 0; k < 4; ++k) {
        const std::string set = std::string("{") + kSets[k] + "}=";
        LightBlock& b = m.blocks[k];
        tun_values(tun_text, "AmbientSky" + set, b.colour[3], 4);
        tun_values(tun_text, "AmbientGround" + set, b.colour[2], 4);
        tun_values(tun_text, "Light1" + set, b.colour[0], 4);
        tun_values(tun_text, "Light1AngleY" + set, &m.angles[k][3], 1);
        tun_values(tun_text, "Light1AngleX" + set, &m.angles[k][0], 1);
        tun_values(tun_text, "Light2" + set, b.colour[1], 4);
        tun_values(tun_text, "Light2AngleY" + set, &m.angles[k][4], 1);
        tun_values(tun_text, "Light2AngleX" + set, &m.angles[k][1], 1);
        tun_values(tun_text, "DualHemiAngleY" + set, &m.angles[k][5], 1);
        tun_values(tun_text, "DualHemiAngleX" + set, &m.angles[k][2], 1);
    }
    RLightManager_RefreshLightBlocks(m);
}

// [driving] 0x001aec20 RLightManager::SetLightingModel
void RLightManager_SetLightingModel(RLightManagerLights& m, int model) {
    if (model == 1 || model == 0) m.light_sources = m.blocks[0];
    else if (model == 2) m.light_sources = m.blocks[1];
    m.model = model;
}

}  // namespace drv
