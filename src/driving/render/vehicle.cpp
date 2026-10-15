// O objeto de render do carro (driving/render/vehicle.h), portado do MIPS do DRIVING.ELF (as desmontagens em
// out/agent_drv_car/ do checkout principal). As contas seguem a FPU do EE e a VU0 em modo macro (platform/ps2_float.h,
// platform/vu0_macro.h) na ordem das instruções.
//
// Divergências (port), cada uma também no comentário da função:
//  - RVehicle::PostLoad procura a instância de cada roda continuando de onde a busca da roda anterior parou (o
//    ponteiro não volta ao começo da lista): sem a instância de uma roda, as seguintes leriam além do fim da lista no
//    PS2; aqui a leitura além do fim conta como guarda e não acha nada;
//  - TriggerFX (0xb, 0xc) e TriggerIlluminate com uma lâmpada fora de 0..15 (o PS2 gravaria além das tabelas do
//    RVehicle) contam como guarda e não gravam;
//  - o BuildRotate (os ponteiros do painel) usa nf::fm::sin/cos no lugar do sinf/cosf do newlib do PS2 (não portado,
//    como no WCollisionMgr::SurfaceBumpHeight): o último bit do ângulo de um ponteiro pode diferir;
//  - o mapa da cena, o brilho de sombra, as molas e os efeitos (ver o cabeçalho) são contados em vehicle_pending.

#include "driving/render/vehicle.h"

#include <cstdlib>
#include <cstring>

#include "driving/game/bond_car.h"
#include "driving/physics/rigid_body.h"
#include "driving/real/vu0_math.h"
#include "driving/render/anim_engine.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {

VehiclePending g_pending;
VehicleSharedBuffers g_shared{};

constexpr uint32_t kOne = 0x3f800000u;

uint32_t bits(float f) { return ps2::fbits(f); }
float flt(uint32_t b) { return ps2::ffloat(b); }
uint32_t* R(float* p) { return reinterpret_cast<uint32_t*>(p); }
const uint32_t* R(const float* p) { return reinterpret_cast<const uint32_t*>(p); }

// A identidade (os ossos novos do AllocateSkeleton, o BuildRotate de 0 grau).
const Bone kIdentity = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
// A matriz de 0x383430, a do SetProcAnimXFormList sem lista: zero (nada no ELF a escreve; o nightfire-cxbx também a tem
// zerada, DefaultProcAnimXForm). Uma instância 0xfe sem esqueleto vira a matriz zero.
const Bone kZeroBone = {};

// Os nomes dos ossos que o PostLoad procura (as tabelas 0x334070, 0x334080 e 0x334090).
const char* const kWheelNames[4] = {"leftfrontwheel", "rightfrontwheel", "leftrearwheel", "rightrearwheel"};
const char* const kHubNames[4] = {"leftfronthub", "rightfronthub", "leftrearhub", "rightrearhub"};
const char* const kFinNames[4] = {"leftfin", "rightfin", "leftrudder", "rightrudder"};

// Os ponteiros do painel (.data 0x3340f0: mínimo, máximo, ângulo de partida, ângulo da faixa).
const float kNeedles[4][4] = {{0, 1, 0, 0}, {0, 330, 120, -225}, {0, 7000, 121, -210}, {0, 1, 0, 0}};

}  // namespace

VehiclePending& vehicle_pending() { return g_pending; }
VehicleSharedBuffers& vehicle_shared_buffers() { return g_shared; }

Vehicle::Vehicle() { RVehicle_Construct(*this); }

bool vehicle_model_info(const uint8_t* carp, size_t size, const CarpGroup& arti, VehicleModelInfo& out) {
    out = VehicleModelInfo();
    // [driving] 0x001c9910 RSceneObj::GetNumBones e 0x001c9980 GetBoneIndex: o 'Skel' (registros de 0x20 bytes, o nome
    // pelo u16 +0x1c a partir do começo dos dados).
    if (const CarpEntry* skel = arti.find(carp_tag("Skel"))) {
        for (uint32_t i = 0; i < skel->count; ++i) {
            const size_t rec = skel->data + size_t(i) * 0x20;
            if (rec + 0x20 > size) break;
            uint16_t off;
            std::memcpy(&off, carp + rec + 0x1c, 2);
            const size_t at = skel->data + off;
            std::string name;
            for (size_t k = at; k < size && carp[k] != 0; ++k) name.push_back(char(carp[k]));
            out.bone_names.push_back(name);
        }
    }
    if (const CarpEntry* base = arti.find(carp_tag("Base"))) {
        if (base->data + 0x3c <= size) {
            std::memcpy(&out.bbox_min_y, carp + base->data + 0x14, 4);
            std::memcpy(&out.bbox_max_y, carp + base->data + 0x24, 4);
            std::memcpy(&out.desc38, carp + base->data + 0x38, 4);
        }
    }
    for (const CarpEntry& e : arti.data) {
        if (!e.short_tag() || (e.tag >> 16) != 0x696E) continue;  // 'in'
        for (uint32_t i = 0; i < e.count && e.data + size_t(i + 1) * 64 <= size; ++i) {
            const uint8_t* p = carp + e.data + size_t(i) * 64;
            VehicleModelInfo::Instance inst;
            inst.procanim = p[0xd];
            std::memcpy(&inst.index, p + 0xe, 2);
            std::memcpy(&inst.y, p + 0x34, 4);
            out.instances.push_back(inst);
        }
        break;
    }
    return !out.instances.empty();
}

// [driving] 0x001d2338 RVehicle::RVehicle
void RVehicle_Construct(Vehicle& v) {
    RVehicleData& d = v.d;
    d = RVehicleData{};
    // RSkeletalObj::RSkeletalObj (0x001cc568): +0x70 e o esqueleto zerados, a mola em (0, 0, 0, -1), os parâmetros zero.
    d.spring_cur[3] = -1.0f;
    std::memcpy(d.spring_prev, d.spring_cur, 16);
    // As lâmpadas: zero e o quadro -10000; acesas as 7, 8 e 11-15.
    for (int i = 0; i < 16; ++i) {
        d.switch_w[i] = 0.0f;
        d.switch_time[i] = -10000;
    }
    for (int i : {7, 8, 11, 12, 13, 14, 15}) d.switch_w[i] = 1.0f;
    // A instância própria: a identidade (o sqc2 vf0 na linha 3, o pexew/pexcw de (1, 0, 0, 0)).
    for (int i = 0; i < 16; ++i) d.own_instance[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    float* f1ec = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&d) + 0x1ec);
    float* f20c = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&d) + 0x20c);
    float* f220 = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(&d) + 0x220);
    for (int i = 0; i < 2; ++i) {
        f1ec[i * 4] = 1.0f;
        f20c[i * 4] = 0.0f;
        f220[i] = 0.0f;
    }
    for (int i = 0; i < 4; ++i) {
        d.wheel_bone[i] = 0;
        d.hub_bone[i] = 0;
    }
    v.bones.clear();
}

// [driving] 0x001cc8e8 RSkeletalObj::AllocateSkeleton
void RSkeletalObj_AllocateSkeleton(Vehicle& v, uint32_t count) {
    v.d.bone_count = count;
    v.bones.assign(count, kIdentity);
    v.d.bones = count ? nf::p32_u(v.bones.data()) : 0;
}

// [driving] 0x001c9980 RSceneObj::GetBoneIndex
int32_t RSceneObj_GetBoneIndex(const VehicleModelInfo& m, const char* name) {
    for (size_t i = 0; i < m.bone_names.size(); ++i)
        if (m.bone_names[i] == name) return int32_t(i + 1);
    return 0;
}

// [driving] 0x001d2648 RVehicle::PostLoad
// Divergência: a busca da instância de cada roda continua de onde a anterior parou (s1 não volta ao começo, como no
// PS2); o que ela leria além do fim da lista conta como guarda e não acha nada.
void RVehicle_PostLoad(Vehicle& v, const VehicleModelInfo& m, bool vehicle_owner, bool is_sub) {
    RVehicleData& d = v.d;
    // RSkeletalObj::PostLoad (0x001cc6b0): o esqueleto do 'Skel' (as molas, os sistemas do tipo 8 do Handle, ficam 0).
    RSkeletalObj_AllocateSkeleton(v, uint32_t(m.bone_names.size()));
    d.springs = 0;
    const bool no_bones = m.bone_names.empty();
    if (no_bones) RSkeletalObj_AllocateSkeleton(v, 4);
    const float f20 = ee::sub(m.desc38, ee::sub(m.bbox_max_y, m.bbox_min_y));
    const size_t count = m.instances.size() & 0xFF;  // o byte +0x1c do Handle
    size_t at = 0;  // s1: a instância corrente (não volta ao começo)
    for (int i = 0; i < 4; ++i) {
        d.hub_bone[i] = RSceneObj_GetBoneIndex(m, kHubNames[i]);
        if (vehicle_owner && is_sub) d.hub_bone[i] = RSceneObj_GetBoneIndex(m, kFinNames[i]);
        d.wheel_bone[i] = RSceneObj_GetBoneIndex(m, kWheelNames[i]);
        d.wheel_offset[i] = 0.0f;
        if (no_bones) d.wheel_bone[i] = i + 1;
        for (size_t k = 0; k < count; ++k, ++at) {
            if (at >= m.instances.size()) {
                ++g_pending.guards;
                continue;
            }
            const VehicleModelInfo::Instance& inst = m.instances[at];
            if (inst.procanim == 0xfe && int32_t(inst.index) == d.wheel_bone[i]) {
                d.wheel_offset[i] = ee::sub(inst.y, f20);
                ++at;
                break;
            }
        }
    }
    // Os modelos da sombra: o primeiro 'Arti' do arquivo, os 'as' em ordem (o byte +0x1e: 3 a sombra, 4 o volume).
    d.shadow_model = 0;
    d.shadow_volume = 0;
    if (vehicle_owner) {
        d.shadow_model = m.shadow_part >= 0 ? uint32_t(m.shadow_part + 1) : 0;
        d.shadow_volume = m.shadow_volume_part >= 0 ? uint32_t(m.shadow_volume_part + 1) : 0;
    }
}

// [driving] 0x001d2cb0 RVehicle::ComputeWheels
float RVehicle_ComputeWheels(Vehicle& v, BondCar* car, bool interior) {
    RVehicleData& d = v.d;
    if (!car || car->d.type != 1) return 0.0f;
    const PBondCarData& c = car->d;
    float spin[4];
    spin[0] = spin[1] = c.wheel_spin_angle[0];  // GetCarWheelSpinAngle(0) e (1): uma para cada eixo
    spin[2] = spin[3] = c.wheel_spin_angle[1];
    // A direção que o desenho pode andar por quadro: 0,002 (0x3b03126f) com a câmera de dentro nos carros das classes 0
    // e 1, senão 0,01 (0x3c23d70a).
    const float rate = ((c.car_class == 0 || c.car_class == 1) && interior) ? ps2::ffloat(0x3b03126fu)
                                                                            : ps2::ffloat(0x3c23d70au);
    const float lim = ps2::ffloat(0x3d99999au), nlim = ps2::ffloat(0xbd99999au);  // +-0,075
    float f22 = ee::max(ee::min(c.car_steer, lim), nlim);
    const float f1 = d.steer;
    const float f2 = ee::sub(f22, f1);
    if (ee::lt(rate, f2)) f22 = ee::add(f1, rate);
    if (ee::lt(f2, ee::neg(rate))) f22 = ee::sub(f1, rate);
    const float f24 = ee::neg(f22);
    float steer[4] = {f22, f24, 0.0f, 0.0f};
    const CarPhysics* ph = const_cast<BondCar*>(car)->physics();
    if (ph->is_sub != 0) {
        // Os submarinos: as barbatanas pelo controle vertical e os lemes pelo freio, cada um andando no máximo 0,075 e
        // 0,1 por quadro.
        float f21 = c.control.steering_vertical;
        const float f23 = ee::sub(f21, d.sub_steer_vertical);
        float f20 = c.control.brake;
        if (c.reversing != 0) f20 = 0.0f;
        const float b2 = d.sub_brake;
        const float f3 = ee::sub(f20, b2);
        if (ee::lt(lim, f23)) f21 = ee::add(d.sub_steer_vertical, lim);
        if (ee::lt(f23, nlim)) f21 = ee::sub(d.sub_steer_vertical, lim);
        const float tenth = ps2::ffloat(0x3dcccccdu);
        if (ee::lt(tenth, f3)) f20 = ee::add(b2, tenth);
        if (ee::lt(f3, ps2::ffloat(0xbdcccccdu))) f20 = ee::sub(b2, tenth);
        d.sub_brake = f20;
        const float fin = ee::mul(f21, ps2::ffloat(0x3d8f5c29u));    // 0,07
        const float rud = ee::mul(f20, ps2::ffloat(0x3e19999au));    // 0,15
        steer[0] = ee::add(steer[0], fin);
        d.sub_steer_vertical = f21;
        steer[1] = ee::add(steer[1], fin);
        steer[3] = ee::add(f24, rud);
        steer[2] = ee::sub(f24, rud);
    }
    d.steer = f22;
    const float limit = ph->spring_compression_limit_draw;
    const float rest = ph->spring_rest_length;
    const float radius = ph->wheel_radius;
    for (int i = 0; i < 4; ++i) {
        Bone scratch_hub{}, scratch_wheel{};
        Bone& hub = d.hub_bone[i] != 0 && size_t(d.hub_bone[i]) <= v.bones.size() ? v.bones[size_t(d.hub_bone[i] - 1)]
                                                                                   : scratch_hub;
        if (d.hub_bone[i] != 0 && size_t(d.hub_bone[i]) > v.bones.size()) ++g_pending.guards;
        VU0_MATRIX4setyrot(hub.m, steer[i]);
        const float comp = c.suspension[i];  // GetSuspensionCompression(i)
        const float value = ee::lt(limit, comp) ? limit : comp;
        hub.m[3][1] = ee::sub(ee::sub(ee::add(radius, value), d.wheel_offset[i]), rest);
        Bone& wheel = d.wheel_bone[i] != 0 && size_t(d.wheel_bone[i]) <= v.bones.size()
                          ? v.bones[size_t(d.wheel_bone[i] - 1)]
                          : scratch_wheel;
        if (d.wheel_bone[i] != 0 && size_t(d.wheel_bone[i]) > v.bones.size()) ++g_pending.guards;
        VU0_MATRIX4setxrot(wheel.m, spin[i]);
        VU0_MATRIX4_mult(wheel.m, hub.m, wheel.m);
    }
    return steer[0];
}

// [driving] 0x001d3440 RVehicle::GetNeedlePos
float RVehicle_GetNeedlePos(float value, int needle) {
    const float* t = kNeedles[needle & 3];
    if (ee::lt(value, t[0])) value = t[0];
    if (ee::lt(t[1], value)) value = t[1];
    const float range = ee::sub(t[1], t[0]);
    return ee::add(t[2], ee::mul(ee::div(ee::sub(value, t[0]), range), t[3]));
}

// A matriz da instância do RSceneObj::UpdatePosition (0x001c8d70, a marca em game/smackable.cpp).
bool RSceneObj_UpdateTransform(const RigidBody* body, float render_offset, float instance[16]) {
    const RigidBodyInfo* info = body->info;
    float m[4][4];
    std::memcpy(m, info->orient, 64);
    std::memcpy(m[3], body->pos, 12);
    if (!ee::eq(render_offset, 0.0f)) {
        // vmulx.xyz: a linha 1 vezes o deslocamento; vsub.xyz: a posição menos ela.
        uint32_t off[3];
        for (int c = 0; c < 3; ++c) off[c] = ps2::mul(R(m[1])[c], bits(render_offset));
        for (int c = 0; c < 3; ++c) R(m[3])[c] = ps2::sub(R(m[3])[c], off[c]);
    }
    // vsub.xyz; vmul.xyz; vmulax.x; vmadday.x (1); vmaddz.x (1): a distância ao quadrado da posição velha.
    uint32_t dv[3];
    for (int c = 0; c < 3; ++c) dv[c] = ps2::sub(R(instance + 12)[c], R(m[3])[c]);
    uint32_t acc = ps2::mul(dv[0], dv[0]);
    acc = ps2::madd(acc, kOne, ps2::mul(dv[1], dv[1]));
    const uint32_t d2 = ps2::madd(acc, kOne, ps2::mul(dv[2], dv[2]));
    // As linhas (xyz de cada uma; o w, as bandeiras da instância, fica).
    for (int r = 0; r < 4; ++r) std::memcpy(instance + r * 4, m[r], 12);
    return !ps2::less(d2, 0x3727c5acu);  // 1e-5
}

// [driving] 0x001d34a8 RVehicle::UpdatePosition
bool RVehicle_UpdatePosition(Vehicle& v, BondCar* car, bool player, bool interior, float instance[16],
                             float render_offset) {
    // RSkeletalObj::UpdatePosition (0x001cc738): o RSceneObj e as molas.
    bool moved = false;
    if (car && car->body()) {
        moved = RSceneObj_UpdateTransform(car->body(), render_offset, instance);
        if (moved) ++g_pending.map_updates;  // WTree::FindNode, SetMapNode, GetShadowBrightness
    }
    for (uint64_t s = v.d.springs; s != 0; s >>= 1)
        if (s & 1) ++g_pending.springs;
    const float steer = RVehicle_ComputeWheels(v, car, interior);
    v.d.params[0] = ee::mul(steer, -750.0f);
    if (!player || !car) return moved;
    // O velocímetro: km/h x 1,5 (GetCarSpeed x 3,6 x 1,5), zero andando para trás (a velocidade contra a frente do
    // corpo: vmul.xyz, vaddy.x, vaddz.x); o conta-giros pelo CalculateRPM.
    float speed = ee::mul(ee::mul(car->d.car_speed, ps2::ffloat(0x40666666u)), 1.5f);
    const RigidBody* body = car->body();
    uint32_t p[3];
    for (int c = 0; c < 3; ++c) p[c] = ps2::mul(R(body->vel)[c], R(body->info->orient[2])[c]);
    const uint32_t dot = ps2::add(ps2::add(p[0], p[1]), p[2]);
    if (ps2::less_equal(dot, 0)) speed = 0.0f;
    v.d.params[1] = RVehicle_GetNeedlePos(speed, 1);
    v.d.params[2] = RVehicle_GetNeedlePos(PBondCar_CalculateRPM(*car), 2);
    return moved;
}

// [driving] 0x001d3138 RVehicle::ComputeDamage
void RVehicle_ComputeDamage(Vehicle& v, const BondCar* car) {
    RVehicleData& d = v.d;
    if (!car) {
        std::memset(d.damage_zones, 0, sizeof d.damage_zones);
        return;
    }
    // GetDamageZones (0x0018aa78): as 16 zonas (dano, escurecimento) do PBondCar +0x2f0.
    const float limit = 0.75f;  // 0x332090
    for (int z = 0; z < 16; ++z) {
        const float x = car->d.damage_zones[z][0], y = car->d.damage_zones[z][1];
        d.damage_zones[z][0] = x;
        d.damage_zones[z][1] = y;
        d.damage_zones[z][2] = ee::mul(y, 3.0f);
        uint32_t w = 0;
        if (ee::lt(limit, x)) w = 127;  // 1.77965e-43: o inteiro 127, o alfa do vidro quebrado
        std::memcpy(&d.damage_zones[z][3], &w, 4);
    }
}

// [driving] 0x001d3c28 RVehicle::TriggerFX
// Divergência: uma lâmpada fora de 0..15 (o PS2 gravaria além do +0x268) conta como guarda e não grava nada.
bool RVehicle_TriggerFX(Vehicle& v, int32_t type, int32_t which) {
    if (type == 0xb || type == 0xc) {
        if (which < 0 || which > 15) {
            ++g_pending.guards;
            return true;
        }
        v.d.switch_w[which] = type == 0xb ? 1.0f : 0.0f;
        return true;
    }
    ++g_pending.effects;  // 7 o dissolver, 6 as faíscas, 8 o clarão, o resto o RSceneObj::TriggerFX (efeitos e danos)
    return false;
}

// [driving] 0x001d2aa0 RVehicle::TriggerIlluminate
// Divergência: uma lâmpada fora de 0..15 conta como guarda (o PS2 gravaria além das tabelas).
void RVehicle_TriggerIlluminate(Vehicle& v, int32_t which, int32_t time) {
    if (which < 0 || which > 15) {
        ++g_pending.guards;
        return;
    }
    v.d.switch_time[which] = time;
    v.d.switch_w[which] = 2.0f;
}

// [driving] 0x001d2910 RVehicle::InitSharedBuffers
void RVehicle_InitSharedBuffers(const std::string& tun) {
    static const char* const kNames[16] = {"Booster[0]",   "Headlights[1]",  "Brakelights[2]", "LeftSignal[3]",
                                           "RightSignal[4]", "Reverse[5]",   "Smokescreen[6]", "Dashboard[7]",
                                           "AlwaysOn[8]",  "LeftButton[9]",  "RightButton[10]", "Misc[11]",
                                           "Misc[12]",     "Misc[13]",       "Misc[14]",       "Misc[15]"};
    for (int i = 0; i < 16; ++i) {
        g_shared.switch_data[i][0] = 0.0f;
        g_shared.switch_data[i][1] = 0.0f;
        g_shared.switch_data[i][2] = 0.5f;
        g_shared.switch_data[i][3] = 0.0f;
    }
    // O banco "Render:SwitchEffects": "V Scroll", "Glow", "Illuminate" (os nomes sem os espaços no arquivo), indexados
    // pela lista "Switch Effect" (os nomes da tabela 0x3340a0, sem o espaço antes do colchete).
    static const char* const kKeys[3] = {"VScroll", "Glow", "Illuminate"};
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 16; ++i) {
            const std::string key = std::string(kKeys[k]) + "{" + kNames[i] + "}=";
            const size_t p = tun.find(key);
            if (p == std::string::npos) continue;
            g_shared.switch_data[i][k] = float(std::strtod(tun.c_str() + p + key.size(), nullptr));
        }
}

// [driving] 0x001d2a50 RVehicle::ClearSharedBuffers
void RVehicle_ClearSharedBuffers() {
    for (auto& s : g_shared.switch_data) s[3] = 0.0f;
    std::memset(g_shared.damage_zones, 0, sizeof g_shared.damage_zones);
}

// [driving] 0x001d2ac0 (o desvanecer dos botões)
float RVehicle_ButtonFade(int32_t time, int32_t start) {
    if (start <= 0) return 0.0f;
    const float t = ee::cvt_s(time - start);
    if (!ee::lt(0.0f, t)) return 0.0f;
    const float f = ee::sub(1.0f, ee::mul(t, ps2::ffloat(0x3d088889u)));  // 0,033333335
    return ee::le(f, 0.0f) ? 0.0f : f;
}

// [driving] 0x001d2b38 RVehicle::SetPerObjectBuffers
void RVehicle_SetPerObjectBuffers(Vehicle& v, int32_t time) {
    v.d.switch_w[9] = RVehicle_ButtonFade(time, v.d.switch_time[9]);
    v.d.switch_w[10] = RVehicle_ButtonFade(time, v.d.switch_time[10]);
    for (int i = 0; i < 16; ++i) g_shared.switch_data[i][3] = v.d.switch_w[i];
    std::memcpy(g_shared.damage_zones, v.d.damage_zones, sizeof g_shared.damage_zones);
}

// [driving] 0x001cd178 RSkeletalObj::SetProcAnimState
ProcAnimContext RSkeletalObj_SetProcAnimState(const Vehicle& v) {
    ProcAnimContext c;
    // SetProcAnimXFormList (0x001bef30): sem esqueleto, a matriz zero de 0x383430 com uma entrada.
    if (!v.bones.empty()) {
        c.xforms = v.bones.data();
        c.xform_count = v.d.bone_count;
    } else {
        c.xforms = &kZeroBone;
        c.xform_count = 1;
    }
    c.params = v.d.params;  // SetProcAnimParamList (0x001bef90): +0x40, 12
    c.param_count = 12;
    return c;
}

// [driving] 0x001bef68 GetProcAnimXForm
const Bone& GetProcAnimXForm(const ProcAnimContext& c, uint32_t index) {
    if (c.xform_count <= index) index = 0;
    return c.xforms[index];
}

// [driving] 0x001befc8 GetProcAnimParameter
float GetProcAnimParameter(const ProcAnimContext& c, uint32_t index) {
    if (index < c.param_count) return c.params[index];
    return c.params[0];
}

// [driving] 0x001beee0 GetInstanceMatrix
bool GetInstanceMatrix(const uint32_t inst[16], const ProcAnimContext& c, float out[4][4]) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(inst);
    const uint8_t fn = b[0xd];
    uint16_t index;
    std::memcpy(&index, b + 0xe, 2);
    // A cópia da instância com a coluna w zerada e o 1 em [3][3] (as três funções começam assim).
    auto copy = [&]() {
        std::memcpy(out, inst, 64);
        out[0][3] = 0.0f;
        out[1][3] = 0.0f;
        out[2][3] = 0.0f;
        out[3][3] = 1.0f;
    };
    if (fn == 0xff) {  // 0x001bf140
        copy();
        return true;
    }
    if (fn == 0xfe) {  // 0x001bf600: o osso (u16 - 1) vezes a instância
        copy();
        VU0_MATRIX4_mult(GetProcAnimXForm(c, uint32_t(index) - 1u).m, out, out);
        return true;
    }
    if (fn == 0xef) {  // 0x001bf598: a rotação em z pelo parâmetro (FUN_001bf048, FUN_001bf070)
        uint32_t idx = index;
        if ((b[0xc] >> 4) & 1) {
            // O índice do estado (o u16 +2 do ProcAnimState da instância).
            if (c.states) {
                uint16_t s;
                std::memcpy(&s, c.states + size_t(index) * 0x20 + 2, 2);
                idx = s;
            } else {
                ++g_pending.guards;
            }
        }
        const float p = GetProcAnimParameter(c, idx);
        copy();
        float rot[4][4];
        BuildRotate(rot, p, 0.0f, 0.0f, 1.0f);
        VU0_MATRIX4_mult(rot, out, out);
        return true;
    }
    if (fn < 0x64) {  // 0x001bf318: a animação da instância (o RAnimEngine; driving/render/anim_engine.h)
        RAnimEngine_EvaluateInstance(b, out);
        return true;
    }
    ++g_pending.procanim_other;
    return false;
}

}  // namespace drv
