// As janelas que quebram (WCollisionMgr::CheckHitWindow, driving/game/shells.h), do MIPS do DRIVING.ELF
// (SWeaponManager.obj e WCollisionMgr.obj).
//
// O artigo de colisão de uma instância guarda, depois das barreiras, grupos de janelas por barreira (a palavra 2 de cada
// grupo é o índice da barreira no artigo; a 1 o tamanho do grupo): em cada grupo, listas por "lado" (a palavra 1 de cada
// lista contra as bandeiras da instância >> 2) de janelas de 8 bytes (s16: o começo ao longo da barreira em 1/8192 do
// comprimento, a altura do topo em 1/256 m, a largura e a altura). O choque de um segmento numa barreira (o hit 2 do
// CheckHitWorld) cai numa janela se o ponto, no referencial da instância, está entre o começo e o fim e entre o topo e o
// pé dela. A primeira vez o estado da janela (WindowDynData) guarda os dois cantos no mundo e um tipo sorteado; as outras
// só contam os tiros. O mapa do PS2 (o WCollisionMgr +0x14, um map<instância, map<janela, estado>> do STL do GCC 2.95)
// fica aqui em window_map() (zerado quando o mundo muda).
//
// Divergência: o EHitWindow (o evento dos sons e dos efeitos do vidro) fica em hit_window_events() em vez da fila dos
// eventos (não tem número na tabela); o destrutor 0x0014f480 (game/hit_window.cpp, seção 26) a esvazia no fim do passe.

#include <cstring>

#include "driving/game/shells.h"
#include "driving/ai/sim_random.h"
#include "driving/physics/world.h"
#include "driving/physics/world_collision.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu0_macro.h"

namespace drv {

namespace ee = ps2::ee;
using namespace ps2::vu;

namespace {
std::map<uint32_t, std::map<uint32_t, WindowDynData>> g_windows;
std::vector<HitWindowEvent> g_events;
const TrackWorld* g_windows_world = nullptr;

// x m (as linhas 0-2 e a 3 vezes 1: vmulax.xyz, vmadday.xyz, vmaddaz.xyz, vmaddw.xyz com o vf0; o w fica 0).
void xform(const float m[16], const float v[4], float out[4]) {
    Reg a, r0, r1, r2, r3, acc, d = {};
    const Reg vf0 = {0, 0, 0, 0x3f800000u};
    load(a, v);
    load(r0, m);
    load(r1, m + 4);
    load(r2, m + 8);
    load(r3, m + 12);
    mulabc(acc, r0, a, BX, XYZ);
    maddabc(acc, r1, a, BY, XYZ);
    maddabc(acc, r2, a, BZ, XYZ);
    maddbc(d, acc, r3, vf0, BW, XYZ);
    store(out, d);
}

float s16(const uint8_t* p, int off) {
    int16_t v;
    std::memcpy(&v, p + off, 2);
    return ee::cvt_s(v);
}
uint32_t u32(const uint8_t* p, int off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
}  // namespace

std::map<uint32_t, std::map<uint32_t, WindowDynData>>& window_map() { return g_windows; }
std::vector<HitWindowEvent>& hit_window_events() { return g_events; }

// [driving] 0x0025b890 (sem nome)
void Mat_TransformPoint3(const float* a, const float m[16], float* out) {
    const float x = a[0], y = a[1], z = a[2];
    const float rx = ee::add(ee::add(ee::add(ee::mul(x, m[0]), ee::mul(y, m[4])), ee::mul(z, m[8])), m[12]);
    const float ry = ee::add(ee::add(ee::add(ee::mul(x, m[1]), ee::mul(y, m[5])), ee::mul(z, m[9])), m[13]);
    const float rz = ee::add(ee::add(ee::add(ee::mul(x, m[2]), ee::mul(y, m[6])), ee::mul(z, m[10])), m[14]);
    out[0] = rx;
    out[1] = ry;
    out[2] = rz;
}

// [driving] 0x0025b9f8 (sem nome)
void Mat_TransformVec4(const float* a, const float m[16], float* out) {
    const float x = a[0], y = a[1], z = a[2], w = a[3];
    float r[4];
    for (int c = 0; c < 4; ++c)
        r[c] = ee::add(ee::add(ee::add(ee::mul(x, m[c]), ee::mul(y, m[4 + c])), ee::mul(z, m[8 + c])),
                       ee::mul(w, m[12 + c]));
    std::memcpy(out, r, 16);
}

// [driving] 0x002153b0 WCollisionMgr::CheckHitWindow
bool WCollisionMgr_CheckHitWindow(WorldCollisionInfo& info, bool effect, int32_t mode) {
    ShellsStats& st = shells_stats();
    ++st.windows_checked;
    if (info.hit != 2) return false;
    WCollisionInstance* ci = nf::p32_ptr<WCollisionInstance>(info.barrier.instance);
    WorldCollision* w = world();
    TrackWorld* tw = w ? w->track_world() : nullptr;
    if (!ci || !tw) {
        ++st.guards;  // o PS2 leria a instância nula
        return false;
    }
    if (tw != g_windows_world) {
        g_windows.clear();
        g_windows_world = tw;
    }
    const uint8_t* art = nf::p32_ptr<const uint8_t>(ci->article);
    const CollisionArticle* ah = reinterpret_cast<const CollisionArticle*>(art);
    if (ah->window_groups == 0) return false;
    // A barreira e o ponto no referencial da instância.
    alignas(16) float m[16];
    WCollisionInstance_MakeMatrix(tw->mgr(), *ci, m, true);
    alignas(16) float p0[4], p1[4], pt[4];
    xform(m, info.barrier.p0, p0);
    xform(m, info.barrier.p1, p1);
    xform(m, info.point, pt);
    float d[3] = {ee::sub(p1[0], p0[0]), 0.0f, ee::sub(p1[2], p0[2])};  // o y zerado (sw zero 0x74(sp))
    const float e[3] = {ee::sub(pt[0], p0[0]), ee::sub(pt[1], p0[1]), ee::sub(pt[2], p0[2])};
    float u = 0.0f;
    if (ee::lt(0.0f, ee::abs(d[0])) || ee::lt(0.0f, ee::abs(d[2]))) {
        if (ee::lt(ee::abs(d[2]), ee::abs(d[0]))) u = ee::div(e[0], d[0]);
        else u = ee::div(e[2], d[2]);
    }
    // O grupo da barreira e a lista do lado da instância.
    const uint8_t* entry = nullptr;
    const uint8_t* g = art + 0x20 + ah->barrier_offset + ah->window_offset;
    for (int32_t left = int32_t(ah->window_groups) - 1; left != -1; --left) {
        if (u32(g, 8) == info.barrier.index) {
            const uint32_t side = uint32_t(ci->flags) >> 2;
            const uint8_t* e2 = g + 0xc;
            for (uint32_t k = 0; k < u32(g, 0); ++k) {
                if (u32(e2, 4) == side) {
                    entry = e2;
                    break;
                }
                e2 += u32(e2, 0) * 8 + 8;
            }
            break;
        }
        const uint32_t size = u32(g, 4);
        if (size > 0x7fff) {
            // O aviso de depuração (FUN_0010c6c8 com 0x399028): um grupo maior que 0x7fff bytes; nenhuma janela.
            ++st.guards;
            effects_note(0x0010c6c8);
            return false;
        }
        g += size;
    }
    const uint8_t* window = nullptr;
    if (entry) {
        const float k = ps2::ffloat(0x39000000u);   // 1/8192
        const float k2 = ps2::ffloat(0x3b800000u);  // 1/256
        const uint8_t* wnd = entry + 8;
        for (uint32_t n = u32(entry, 0); n != 0; --n, wnd += 8) {
            const float u0 = ee::mul(s16(wnd, 0), k);
            if (!ee::le(u0, u)) continue;
            if (!ee::le(u, ee::add(u0, ee::mul(s16(wnd, 4), k)))) continue;
            const float top = ee::mul(s16(wnd, 2), k2);
            if (!ee::le(e[1], top)) continue;
            if (ee::le(ee::sub(top, ee::mul(s16(wnd, 6), k2)), e[1])) {
                window = wnd;
                break;
            }
        }
    }
    if (!window || !effect) return false;
    ++st.windows_hit;
    // O estado da janela no mapa (a instância, depois a janela).
    std::map<uint32_t, WindowDynData>& inst = g_windows[info.barrier.instance];
    const uint32_t key = nf::p32_u(window);
    auto it = inst.find(key);
    if (it == inst.end()) {
        WindowDynData v{};
        alignas(16) float mr[16];
        WCollisionInstance_MakeMatrix(tw->mgr(), *ci, mr, false);
        float nl[3];
        Mat_TransformPoint3(info.normal, mr, nl);
        const float k = ps2::ffloat(0x39000000u), k2 = ps2::ffloat(0x3b800000u);
        const float c = ps2::ffloat(0x3d4ccccdu);  // 0,05: um pouco para fora da parede
        const float bx = ee::add(p0[0], ee::mul(nl[0], c));
        const float by = ee::add(p0[1], ee::mul(nl[1], c));
        const float bz = ee::add(p0[2], ee::mul(nl[2], c));
        const float ax = ee::add(bx, ee::mul(d[0], ee::mul(s16(window, 0), k)));
        const float az = ee::add(bz, ee::mul(d[2], ee::mul(s16(window, 0), k)));
        const float ay = ee::add(by, ee::mul(s16(window, 2), k2));
        const float ex = ee::add(ax, ee::mul(d[0], ee::mul(s16(window, 4), k)));
        const float ez = ee::add(az, ee::mul(d[2], ee::mul(s16(window, 4), k)));
        const float ey = ee::sub(ee::add(by, ee::mul(s16(window, 2), k2)), ee::mul(s16(window, 6), k2));
        alignas(16) float inv[16];
        std::memcpy(inv, m, 64);
        OrthoInverse(inv);
        RRandom& rr = render_random();
        const uint32_t p = rr.seed * rr.multiplier;
        rr.seed = p & 0xffffu;
        int32_t type = int32_t((p >> 8) & 3);
        if (ee::lt(10.0f, ee::add(info.barrier.p0[1], ee::mul(s16(window, 2), k2)))) type += 4;
        v.type = type;
        v.hits = 0;
        v.unknown08 = 0;
        v.a[0] = ax;
        v.a[1] = ay;
        v.a[2] = az;
        v.a[3] = 1.0f;
        v.b[0] = ex;
        v.b[1] = ey;
        v.b[2] = ez;
        v.b[3] = 1.0f;
        Mat_TransformVec4(v.a, inv, v.a);
        Mat_TransformVec4(v.b, inv, v.b);
        it = inst.emplace(key, v).first;
    } else {
        ++it->second.hits;
    }
    // EHitWindow::EHitWindow (0x0014f330; Event::operator_new 0x54): o ponto, a normal, o estado e o modo.
    HitWindowEvent ev{};
    ev.image[0] = 0x00371008;
    std::memcpy(&ev.image[1], info.point, 16);
    std::memcpy(&ev.image[5], info.normal, 16);
    std::memcpy(&ev.image[9], &it->second, sizeof(WindowDynData));
    ev.image[20] = mode == 1 ? 1u : 0u;
    g_events.push_back(ev);
    ++st.window_events;
    effects_note(0x0014f330, ev.image[1], ev.image[2], ev.image[3], ev.image[9], ev.image[10], ev.image[20]);
    info.face_type = 0x70;
    info.face_subtype = 0;
    return true;
}

}  // namespace drv
