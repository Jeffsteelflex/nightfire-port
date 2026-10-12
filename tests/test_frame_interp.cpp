// Testes da interpolação entre ticks (src/render/frame_interp.cpp), dos marcadores do quadro (render/frame_markers.cpp)
// e do limite de quadros (platform/video_settings).

#include <cmath>
#include <cstring>

#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi_draw.h"
#include "platform/settings.h"
#include "platform/video_settings.h"
#include "render/frame_interp.h"
#include "render/frame_markers.h"
#include "test_framework.h"

using namespace nf;

namespace {

// Rotação de RotMatrix (o motor) com translação e escala uniforme, como std::array de vetor-linha.
std::array<float, 16> make_matrix(float rx, float ry, float rz, float tx, float ty, float tz, float scale = 1.0f) {
    MATRIX m;
    const _VECTOR e{rx, ry, rz, 0.0f};
    RotMatrix(&e, &m);
    std::array<float, 16> out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) out[size_t(i * 4 + j)] = m.m[i][j] * scale;
    }
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
    out[15] = 1.0f;
    return out;
}

// Uma câmera como a do jogador (Camera_SetToPlayer, ou set_game_camera do main): frente pelo giro e a elevação,
// direita horizontal (y x frente), cima = frente x direita; posição em p.
std::array<float, 16> camera_matrix(float yaw, float elev, float px, float py, float pz) {
    const float f[3] = {std::sin(yaw) * std::cos(elev), std::sin(elev), std::cos(yaw) * std::cos(elev)};
    const float r[3] = {std::cos(yaw), 0.0f, -std::sin(yaw)};
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    return {r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, f[0], f[1], f[2], 0, px, py, pz, 1};
}

// v * M (vetor-linha, ponto).
void apply(const float* m, const float p[3], float out[3]) {
    for (int j = 0; j < 3; ++j) out[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
}

float row_dot(const float* m, int a, int b) {
    return m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1] + m[a * 4 + 2] * m[b * 4 + 2];
}

float det3(const float* m) {
    return m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

// Um item rígido com identidade.
DrawItem rigid(uint64_t id, const std::array<float, 16>& world, int mesh = 1, int8_t list = 0,
               PsiOwnerKind kind = PsiOwnerKind::Object, const void* owner = nullptr, uint32_t created = 0) {
    DrawItem d;
    d.mesh = mesh;
    d.world = world;
    d.interp.id = id;
    d.interp.list = list;
    d.interp.owner_kind = uint8_t(kind);
    d.interp.owner = owner;
    d.interp.created = created;
    return d;
}

TickRecord record(std::vector<DrawItem> items, const std::array<float, 16>& cam = make_matrix(0, 0, 0, 0, 0, 0)) {
    TickRecord r;
    r.items = std::move(items);
    r.camera.m = cam;
    r.camera.fov_deg = 60.0f;
    return r;
}

}  // namespace

TEST(interp_matrix_endpoints_exact_and_orthonormal) {
    const auto a = make_matrix(0.3f, -1.2f, 0.4f, 1, 2, 3, 1.5f);
    const auto b = make_matrix(-0.2f, 0.9f, 1.1f, -4, 0.5f, 7, 2.0f);
    float out[16];
    interp_matrix(a.data(), b.data(), 0.0f, out);
    CHECK(std::memcmp(out, a.data(), sizeof(out)) == 0);
    interp_matrix(a.data(), b.data(), 1.0f, out);
    CHECK(std::memcmp(out, b.data(), sizeof(out)) == 0);
    interp_matrix(a.data(), b.data(), -0.5f, out);
    CHECK(std::memcmp(out, a.data(), sizeof(out)) == 0);
    for (float t = 0.05f; t < 1.0f; t += 0.1f) {
        interp_matrix(a.data(), b.data(), t, out);
        // As linhas são escala x rotação: normalizadas, ortonormais e de determinante +1.
        const float s = 1.5f + 0.5f * t;
        for (int i = 0; i < 3; ++i) CHECK_NEAR(std::sqrt(row_dot(out, i, i)), s, 1e-4);
        CHECK_NEAR(row_dot(out, 0, 1) / (s * s), 0.0, 1e-5);
        CHECK_NEAR(row_dot(out, 0, 2) / (s * s), 0.0, 1e-5);
        CHECK_NEAR(row_dot(out, 1, 2) / (s * s), 0.0, 1e-5);
        CHECK_NEAR(det3(out) / (s * s * s), 1.0, 1e-4);
        // A translação anda em linha reta.
        CHECK_NEAR(out[12], 1.0f + (-4.0f - 1.0f) * t, 1e-4);
        CHECK_NEAR(out[14], 3.0f + (7.0f - 3.0f) * t, 1e-4);
        CHECK(out[15] == 1.0f);
    }
}

TEST(interp_matrix_slerp_about_one_axis) {
    // De 0 a 90 graus em volta de y: o meio é a rotação de 45 graus do motor (RotMatrixY), e um quarto, 22,5.
    MATRIX r0, r90, r45, r22;
    RotMatrixY(0.0f, &r0);
    RotMatrixY(1.5707964f, &r90);
    RotMatrixY(0.7853982f, &r45);
    RotMatrixY(0.3926991f, &r22);
    std::array<float, 16> a{}, b{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            a[size_t(i * 4 + j)] = r0.m[i][j];
            b[size_t(i * 4 + j)] = r90.m[i][j];
        }
    }
    a[15] = b[15] = 1.0f;
    float out[16];
    interp_matrix(a.data(), b.data(), 0.5f, out);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) CHECK_NEAR(out[i * 4 + j], r45.m[i][j], 1e-4);
    }
    interp_matrix(a.data(), b.data(), 0.25f, out);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) CHECK_NEAR(out[i * 4 + j], r22.m[i][j], 1e-4);
    }
    // Uma matriz espelhada (determinante negativo) dos dois lados interpola e continua espelhada; com sinais
    // diferentes, fica a do tick atual.
    auto ma = make_matrix(0.1f, 0.2f, 0.3f, 0, 0, 0);
    auto mb = make_matrix(0.2f, 0.4f, 0.3f, 1, 0, 0);
    for (int j = 0; j < 3; ++j) {
        ma[size_t(j)] = -ma[size_t(j)];
        mb[size_t(j)] = -mb[size_t(j)];
    }
    interp_matrix(ma.data(), mb.data(), 0.5f, out);
    CHECK(det3(out) < 0.0f);
    const auto plain = make_matrix(0.2f, 0.4f, 0.3f, 1, 0, 0);
    interp_matrix(ma.data(), plain.data(), 0.5f, out);
    CHECK(std::memcmp(out, plain.data(), sizeof(out)) == 0);
}

TEST(interp_decompose_round_trip) {
    const auto m = make_matrix(-0.7f, 2.1f, 0.25f, 10, -3, 0.5f, 0.8f);
    Xform x;
    CHECK(xform_decompose(m.data(), &x));
    float back[16] = {};
    back[15] = 1.0f;
    xform_compose(x, back);
    for (int k = 0; k < 15; ++k) CHECK_NEAR(back[k], m[size_t(k)], 1e-5);
    // Degenerada: falso.
    std::array<float, 16> zero{};
    CHECK(!xform_decompose(zero.data(), &x));
}

TEST(interp_palette_per_bone) {
    // Um modelo com skinning (3 ossos) que anda e dobra um osso: cada osso do quadro em 0.5 é a interpolação do
    // osso correspondente; o osso parado fica idêntico; world (identidade) não muda.
    DrawItem p = rigid(42, make_matrix(0, 0, 0, 0, 0, 0));
    DrawItem c = p;
    p.palette = {make_matrix(0, 0, 0, 0, 0, 0), make_matrix(0, 0.2f, 0, 0, 1, 0), make_matrix(0.1f, 0, 0, 0, 2, 0)};
    c.palette = {make_matrix(0, 0, 0, 0.1f, 0, 0), make_matrix(0, 0.4f, 0, 0.1f, 1, 0), make_matrix(0.1f, 0, 0, 0, 2, 0)};
    FrameInterpolator fi;
    fi.push(record({p}));
    fi.push(record({c}));
    CHECK(!fi.cut());
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f.size() == 1);
    if (f.size() != 1) return;
    CHECK(fi.stats().matched == 1 && fi.stats().moving == 1 && fi.stats().bones == 2);
    for (int b = 0; b < 2; ++b) {
        float expect[16];
        interp_matrix(p.palette[size_t(b)].data(), c.palette[size_t(b)].data(), 0.5f, expect);
        for (int k = 0; k < 16; ++k) CHECK_NEAR(f[0].palette[size_t(b)][size_t(k)], expect[k], 1e-6);
    }
    CHECK(f[0].palette[2] == c.palette[2]);
    CHECK(f[0].world == c.world);
    CHECK_NEAR(f[0].palette[0][12], 0.05f, 1e-6);
    // Nas pontas, exatos.
    const std::vector<DrawItem>& f0 = fi.build(0.0f, &cam);
    CHECK(f0[0].palette == p.palette);
    const std::vector<DrawItem>& f1 = fi.build(1.0f, &cam);
    CHECK(f1[0].palette == c.palette);
}

TEST(interp_identity_matching) {
    // Os itens trocam de ordem entre os ticks: cada um acha o seu par pela identidade.
    const auto a0 = make_matrix(0, 0, 0, 0, 0, 0), a1 = make_matrix(0, 0, 0, 1, 0, 0);
    const auto b0 = make_matrix(0, 0, 0, 10, 0, 0), b1 = make_matrix(0, 0, 0, 10, 0, 1);
    FrameInterpolator fi;
    fi.push(record({rigid(1, a0), rigid(2, b0)}));
    fi.push(record({rigid(2, b1), rigid(1, a1)}));
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f.size() == 2);
    CHECK(fi.stats().matched == 2);
    if (f.size() == 2) {
        CHECK(f[0].interp.id == 2 && std::fabs(f[0].world[12] - 10.0f) < 1e-5f && std::fabs(f[0].world[14] - 0.5f) < 1e-5f);
        CHECK(f[1].interp.id == 1 && std::fabs(f[1].world[12] - 0.5f) < 1e-5f);
    }
    // Sem identidade (0), outro modelo, ou identidade repetida no tick anterior: sem par, fica o do tick atual.
    FrameInterpolator g;
    g.push(record({rigid(0, a0), rigid(5, a0, 1), rigid(7, a0), rigid(7, b0)}));
    g.push(record({rigid(0, a1), rigid(5, a1, 2), rigid(7, a1)}));
    const std::vector<DrawItem>& h = g.build(0.5f, &cam);
    CHECK(g.stats().matched == 0);
    for (const DrawItem& d : h) CHECK(d.world == a1);
    // identidades distintas pela ordem dentro do dono e pelo modelo
    int o = 0;
    CHECK(draw_identity(&o, 2, &o, 5, 0, false) != draw_identity(&o, 2, &o, 5, 1, false));
    CHECK(draw_identity(&o, 2, &o, 5, 0, false) != draw_identity(&o, 2, &o, 6, 0, false));
    CHECK(draw_identity(&o, 2, &o, 5, 0, false) != draw_identity(&o, 2, &o, 5, 0, true));
    CHECK(draw_identity(&o, 2, &o, 5, 0, false) == draw_identity(&o, 2, &o, 5, 0, false));
    CHECK(draw_identity(nullptr, 0, nullptr, 0, 0, false) != 0);
}

TEST(interp_cuts) {
    InterpCamera a, b;
    a.m = make_matrix(0, 0.3f, 0, 0, 1.6f, 0);
    b.m = make_matrix(0, 0.35f, 0, 0.1f, 1.6f, 0);
    CHECK(!camera_cut(a, b, 1.0f));
    b.m = make_matrix(0, 0.3f, 0, 2.0f, 1.6f, 0);  // 2 m num tick
    CHECK(camera_cut(a, b, 1.0f));
    CHECK(!camera_cut(a, b, 2.0f));  // a 30 Hz, dois ticks de 60 Hz
    b.m = make_matrix(0, 1.4f, 0, 0, 1.6f, 0);  // 63 graus num tick
    CHECK(camera_cut(a, b, 1.0f));
    b.m = a.m;
    b.fov_deg = 30.0f;  // zoom de 60 para 30 graus
    CHECK(camera_cut(a, b, 1.0f));

    // Um corte no push: o quadro é o do tick atual inteiro, também a câmera.
    const auto m0 = make_matrix(0, 0, 0, 0, 0, 0), m1 = make_matrix(0, 0, 0, 0.5f, 0, 0);
    FrameInterpolator fi;
    fi.push(record({rigid(1, m0)}, make_matrix(0, 0, 0, 0, 0, 0)));
    CHECK(fi.cut());  // o primeiro depois do reset
    fi.push(record({rigid(1, m1)}, make_matrix(0, 0, 0, 5, 0, 0)));  // a câmera salta 5 m
    CHECK(fi.cut());
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f[0].world == m1);
    CHECK(cam.m[12] == 5.0f);
    // A câmera de script ligando: corte mesmo sem salto.
    TickRecord s = record({rigid(1, m0)}, make_matrix(0, 0, 0, 5, 0, 0));
    s.scene_camera = true;
    fi.push(std::move(s));
    CHECK(fi.cut());
    // Sem corte: a câmera no meio.
    fi.push(record({rigid(1, m1)}, make_matrix(0, 0, 0, 5.5f, 0, 0)));
    CHECK(fi.cut());  // a câmera de script desligou
    fi.push(record({rigid(1, m0)}, make_matrix(0, 0, 0, 5.7f, 0, 0)));
    CHECK(!fi.cut());
    fi.build(0.5f, &cam);
    CHECK_NEAR(cam.m[12], 5.6f, 1e-5);
    // reset: o próximo é um corte.
    fi.reset();
    CHECK(fi.empty());
    fi.push(record({rigid(1, m0)}));
    CHECK(fi.cut());
}

TEST(interp_item_jumps_snap) {
    // Um item que salta 5 m num tick (teleporte) ou gira 120 graus (o clarão, girado ao acaso) fica o do tick atual.
    const auto a = make_matrix(0, 0, 0, 0, 0, 0);
    FrameInterpolator fi;
    fi.push(record({rigid(1, a), rigid(2, a), rigid(3, a)}));
    const auto away = make_matrix(0, 0, 0, 5, 0, 0), spun = make_matrix(0, 0, 2.1f, 0, 0, 0);
    const auto step = make_matrix(0, 0, 0.1f, 0.2f, 0, 0);
    fi.push(record({rigid(1, away), rigid(2, spun), rigid(3, step)}));
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(fi.stats().matched == 3 && fi.stats().snapped == 2 && fi.stats().moving == 1);
    CHECK(f[0].world == away);
    CHECK(f[1].world == spun);
    CHECK(f[2].world != step);
}

TEST(interp_keeps_previous_visible) {
    // A sala (célula) que o tick anterior via e o atual não vê mais continua no quadro, no fim da lista 0; um objeto
    // só se ainda está vivo (e com o mesmo created); a lista 2 (presa à câmera) nunca.
    int room = 0, hall = 0, alive_obj = 0, dead_obj = 0, gun = 0;
    const auto m = make_matrix(0, 0, 0, 0, 0, 0);
    FrameInterpolator fi;
    fi.push(record({rigid(1, m, 1, 0, PsiOwnerKind::Cel, &room), rigid(2, m, 1, 0, PsiOwnerKind::Object, &alive_obj, 7),
                    rigid(3, m, 1, 0, PsiOwnerKind::Object, &dead_obj, 7), rigid(4, m, 1, 2, PsiOwnerKind::Object, &gun),
                    rigid(9, m, 1, 0, PsiOwnerKind::Cel, &hall)}));
    DrawItem hud;
    hud.quads2d.resize(3);
    hud.quads2d[1].x0 = 17.0f;
    hud.blend = Blend::Alpha;
    fi.push(record({rigid(9, m, 1, 0, PsiOwnerKind::Cel, &hall), rigid(5, m, 1, 1), hud}),
            [&](const void* owner, uint32_t created) { return owner == &alive_obj && created == 7; });
    CHECK(fi.stats().kept == 2);
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f.size() == 5);
    if (f.size() == 5) {
        // Ordem: a sala do atual, os mantidos da lista 0, a lista 1, o 2D.
        CHECK(f[0].interp.id == 9);
        CHECK(f[1].interp.id == 1 && f[2].interp.id == 2);
        CHECK(f[3].interp.id == 5);
        CHECK(f[4].quads2d.size() == 3 && f[4].quads2d[1].x0 == 17.0f);
    }
    // Um dono que o tick atual ainda desenha, com outro modelo (o destrutível quebrado): o desenho velho não fica.
    int crate = 0;
    FrameInterpolator g;
    g.push(record({rigid(20, m, 1, 0, PsiOwnerKind::Object, &crate, 3), rigid(21, m, 1, 0, PsiOwnerKind::Cel, &room)}));
    g.push(record({rigid(22, m, 2, 0, PsiOwnerKind::Object, &crate, 3), rigid(21, m, 1, 0, PsiOwnerKind::Cel, &room)}),
           [&](const void*, uint32_t) { return true; });
    CHECK(g.stats().kept == 0 && g.build(0.5f, &cam).size() == 2);
}

TEST(interp_2d_always_latest) {
    // O 2D (e as partículas) não se interpola: sai igual ao do tick atual em qualquer alpha.
    DrawItem a;
    a.quads2d.resize(2);
    a.quads2d[0].x0 = 1.0f;
    DrawItem b = a;
    b.quads2d[0].x0 = 100.0f;
    DrawItem pa;
    pa.sprites.resize(4);
    pa.sprites[0].pos[0] = 1.0f;
    DrawItem pb = pa;
    pb.sprites[0].pos[0] = 3.0f;
    FrameInterpolator fi;
    fi.push(record({a, pa}));
    fi.push(record({b, pb}));
    InterpCamera cam;
    for (float t : {0.0f, 0.3f, 1.0f}) {
        const std::vector<DrawItem>& f = fi.build(t, &cam);
        CHECK(f.size() == 2 && f[0].quads2d[0].x0 == 100.0f && f[1].sprites[0].pos[0] == 3.0f);
    }
}

TEST(interp_fade_alpha_between_ticks) {
    // O esmaecimento (psiFadeView, DrawItem::fade = a câmera) interpola o alfa entre dois ticks quando os dois têm o
    // da mesma câmera; os pontos são exatos; um que começa (sem par) fica como o do tick atual, mesmo num corte.
    const auto fade = [](int8_t cam, uint8_t alpha) {
        DrawItem d;
        d.fade = cam;
        d.quads2d.resize(1);
        d.quads2d[0].rgba[3] = alpha;
        d.quads2d[0].texture = kQuadNoTexture;
        return d;
    };
    FrameInterpolator fi;
    fi.push(record({fade(0, 255)}));
    InterpCamera cam;
    CHECK(fi.build(0.25f, &cam)[0].quads2d[0].rgba[3] == 255);  // sem o tick anterior (carga): o atual
    fi.push(record({fade(0, 155)}));
    CHECK(fi.stats().fades == 1);
    CHECK(fi.build(0.0f, &cam)[0].quads2d[0].rgba[3] == 255);
    CHECK(fi.build(0.5f, &cam)[0].quads2d[0].rgba[3] == 205);
    CHECK(fi.build(1.0f, &cam)[0].quads2d[0].rgba[3] == 155);
    // Outra câmera no tick seguinte (o esmaecimento da 4 começa): sem par, o valor do tick em qualquer alpha; o da 0
    // continua interpolado. Um corte da câmera não muda nada disto.
    fi.push(record({fade(0, 55), fade(4, 200)}, make_matrix(0, 3.0f, 0, 50, 0, 0)));
    CHECK(fi.cut() && fi.stats().fades == 1);
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f.size() == 2 && f[0].quads2d[0].rgba[3] == 105 && f[1].quads2d[0].rgba[3] == 200);
    // Depois de reset (uma carga), nada se interpola.
    fi.reset();
    fi.push(record({fade(0, 10)}));
    CHECK(fi.build(0.5f, &cam)[0].quads2d[0].rgba[3] == 10 && fi.stats().fades == 0);
}

TEST(psi_fade_view_records_a_call_in_order) {
    // psiFadeView (o sprite da tela inteira do CreateTotalScreenSprite) é um desenho sem modelo na ordem do quadro,
    // com a câmera em uso, a cor e o alfa dados; o último fica também em g_psi_frame.fade.
    psi_draw_debug_power_on();
    psiPreDraw();
    viewer_tag cam0{}, cam4{};
    psiUseCamera(&cam0, 1);
    const Psi2DQuad q{};
    psi_draw_2d(&q, 1);
    psiFadeView(0, 0, 0, 0x99);
    psiUseCamera(&cam4, 1);
    psiFadeView(0xff, 0xff, 0xff, 0x40);
    CHECK(g_psi_frame.draws.size() == 3 && g_psi_frame.fades == 2);
    if (g_psi_frame.draws.size() != 3) return;
    const PsiDrawCall& a = g_psi_frame.draws[1];
    const PsiDrawCall& b = g_psi_frame.draws[2];
    CHECK(g_psi_frame.draws[0].fade == 0 && g_psi_frame.draws[0].sprites2d == 0);
    CHECK(a.fade == 1 && a.glist == nullptr && a.camera == &cam0 && a.fade_rgba[3] == 0x99 && a.sprites2d < 0);
    CHECK(b.fade == 1 && b.camera == &cam4 && b.fade_rgba[0] == 0xff && b.fade_rgba[3] == 0x40);
    CHECK(a.owner == nullptr && a.list == -1 && a.particles < 0 && a.triangles < 0);
    CHECK(g_psi_frame.fade_camera == &cam4 && g_psi_frame.fade[3] == 0x40);
    psiPreDraw();
    CHECK(g_psi_frame.fades == 0 && g_psi_frame.draws.empty());
    psi_draw_debug_power_on();
}

TEST(interp_look_delta_and_overlay) {
    // A câmera gira 0,1 rad para a esquerda e sobe 0,05: o giro e a elevação da frente mudam isso; a posição fica.
    std::array<float, 16> m = camera_matrix(0.7f, 0.2f, 3, 1.6f, -2);
    const float yaw0 = camera_yaw(m.data()), elev0 = camera_elevation(m.data());
    CHECK_NEAR(yaw0, 0.7f, 1e-5);
    CHECK_NEAR(elev0, 0.2f, 1e-5);
    CHECK_NEAR(det3(m.data()), 1.0, 1e-5);
    float W[9];
    camera_look_delta(m.data(), 0.1f, 0.05f, 1.5707964f, W);
    CHECK_NEAR(camera_yaw(m.data()), yaw0 + 0.1f, 1e-4);
    CHECK_NEAR(camera_elevation(m.data()), elev0 + 0.05f, 1e-4);
    CHECK(m[12] == 3.0f && m[13] == 1.6f && m[14] == -2.0f);
    for (int i = 0; i < 3; ++i) CHECK_NEAR(row_dot(m.data(), i, i), 1.0, 1e-5);
    CHECK_NEAR(det3(m.data()), 1.0, 1e-5);
    // A elevação para em +-90 graus (Player_ViewClamping: BLData::pitch em -1..1).
    std::array<float, 16> up = camera_matrix(0.3f, 1.4f, 0, 0, 0);
    camera_look_delta(up.data(), 0.0f, 1.0f, 1.5707964f, W);
    CHECK_NEAR(camera_elevation(up.data()), 1.5707964f, 2e-3);
    std::array<float, 16> down = camera_matrix(0.3f, -1.0f, 0, 0, 0);
    camera_look_delta(down.data(), 0.0f, -0.3f, 1.5707964f, W);
    CHECK_NEAR(camera_elevation(down.data()), -1.3f, 1e-4);
    // Um desenho preso à câmera (a arma) girado por W em volta do olho fica no mesmo lugar visto da câmera.
    std::array<float, 16> cam = camera_matrix(-0.4f, 0.1f, 1, 2, 3);
    const std::array<float, 16> cam0 = cam;
    std::array<float, 16> gun = make_matrix(0.3f, -0.2f, 0.1f, 1.2f, 1.8f, 3.4f);
    camera_look_delta(cam.data(), -0.2f, 0.1f, 1.5707964f, W);
    const float eye[3] = {cam[12], cam[13], cam[14]};
    apply_about_eye(gun.data(), W, eye);
    const float local[3] = {0.1f, -0.2f, 0.3f};
    float before[3], after[3];
    // O ponto local da arma, no espaço da câmera, antes e depois.
    const auto to_cam = [](const std::array<float, 16>& c, const float* w, float* out) {
        const float d[3] = {w[0] - c[12], w[1] - c[13], w[2] - c[14]};
        for (int r = 0; r < 3; ++r) out[r] = d[0] * c[size_t(r * 4)] + d[1] * c[size_t(r * 4 + 1)] + d[2] * c[size_t(r * 4 + 2)];
    };
    float w0[3], w1[3];
    const std::array<float, 16> gun0 = make_matrix(0.3f, -0.2f, 0.1f, 1.2f, 1.8f, 3.4f);
    apply(gun0.data(), local, w0);
    apply(gun.data(), local, w1);
    to_cam(cam0, w0, before);
    to_cam(cam, w1, after);
    for (int k = 0; k < 3; ++k) CHECK_NEAR(after[k], before[k], 1e-4);

    // rotate_overlay gira só os itens da lista 2, e um build seguinte volta ao valor do tick (não acumula).
    const auto g = make_matrix(0, 0, 0, 0, 0, 1);
    FrameInterpolator fi;
    fi.push(record({rigid(1, g, 1, 2), rigid(2, g, 1, 0)}));
    fi.push(record({rigid(1, g, 1, 2), rigid(2, g, 1, 0)}));
    InterpCamera c;
    fi.build(0.5f, &c);
    const float origin[3] = {0, 0, 0};
    fi.rotate_overlay(W, origin);
    const std::vector<DrawItem>& f1 = fi.build(0.5f, &c);
    CHECK(f1[0].world == g && f1[1].world == g);
    fi.rotate_overlay(W, origin);
    CHECK(fi.build(0.5f, &c)[0].world == g);
}

TEST(interp_frame_pacing_maths) {
    // O prazo anda um período por quadro, sem acumular erro.
    const double period = 1.0 / 144.0;
    double d = 10.0;
    for (int i = 0; i < 1000; ++i) d = next_frame_deadline(d, d - 0.001, period);
    CHECK_NEAR(d, 10.0 + 1000.0 * period, 1e-9);
    // Atrasado menos de um período: o prazo segue a grade (o quadro seguinte recupera).
    CHECK_NEAR(next_frame_deadline(10.0, 10.0 + period * 1.5, period), 10.0 + period, 1e-12);
    // Atrasado mais de um período: recomeça agora (sem rajada).
    CHECK_NEAR(next_frame_deadline(10.0, 10.5, period), 10.5, 1e-12);
    // O alpha: a fração do tick, presa a 0..1 (a 30 Hz o tick dura dois VBlanks).
    CHECK_NEAR(tick_alpha(100.25, 100.0, 1.0), 0.25, 1e-6);
    CHECK_NEAR(tick_alpha(101.0, 100.0, 2.0), 0.5, 1e-6);
    CHECK(tick_alpha(99.0, 100.0, 1.0) == 0.0f);
    CHECK(tick_alpha(103.0, 100.0, 1.0) == 1.0f);
    // Um monitor de 144 Hz: 2,4 quadros por tick, e os alphas de um tick sobem.
    float prev = -1.0f;
    for (int k = 0; k < 2; ++k) {
        const float a = tick_alpha((12.0 + k) * 60.0 / 144.0, 5.0, 1.0);
        CHECK(a > prev);
        prev = a;
    }
}

TEST(interp_fps_cap_setting) {
    int cap = 99;
    CHECK(parse_fps_cap("off", &cap) && cap == 0);
    CHECK(parse_fps_cap("unlimited", &cap) && cap == 0);
    CHECK(parse_fps_cap("144", &cap) && cap == 144);
    CHECK(!parse_fps_cap("5", &cap) && cap == 144);
    CHECK(!parse_fps_cap("fast", &cap));
    CHECK(fps_cap_name(0) == "off" && fps_cap_name(165) == "165");
    const std::vector<int> v = fps_cap_values(0);
    CHECK((v == std::vector<int>{0, 30, 60, 120, 144, 165, 240}));
    CHECK(fps_cap_values(100).size() == 8);
    // No ini: gravado e lido de volta, com a interpolação e o olhar do mouse.
    Settings s;
    VideoSettings a;
    a.fps_cap = 165;
    a.interpolation = false;
    a.instant_mouse_look = false;
    store_video_settings(a, s);
    VideoSettings b;
    load_video_settings(s, b);
    CHECK(b.fps_cap == 165 && !b.interpolation && !b.instant_mouse_look);
    CHECK(s.get("video", "fps_cap") == "165");
}

TEST(frame_markers_order) {
    // Os marcadores do registro do quadro (render/frame_markers.h): a névoa antes de draws[fog_at], a limpeza do Z
    // antes de draws[i]; a do começo (psiPreDraw, índice 0) não vira item; no mesmo índice, a névoa primeiro (ela lê
    // o Z que a limpeza apaga). Os provisórios entram antes do primeiro misturado, da névoa ou da limpeza.
    PsiFrame f;
    f.draws.resize(4);
    f.fog_at = 2;
    f.clear_z_at = {0, 2, 4};
    f.fog_rgb[1] = 0x2e;
    f.fog_far1 = 123.0f;
    std::vector<DrawItem> out;
    for (size_t i = 0; i <= f.draws.size(); ++i) {
        add_frame_markers(f, i, out);
        if (i < f.draws.size()) out.push_back(rigid(100 + i, make_matrix(0, 0, 0, 0, 0, 0)));
    }
    CHECK(out.size() == 7);
    if (out.size() != 7) return;
    CHECK(out[0].interp.id == 100 && out[1].interp.id == 101);
    CHECK(out[2].fog_pass && !out[2].clear_depth && out[2].fog_rgb[1] == 0x2e && out[2].fog_far1 == 123.0f);
    CHECK(out[3].clear_depth && !out[3].fog_pass && out[3].mesh < 0);
    CHECK(out[4].interp.id == 102 && out[5].interp.id == 103);
    CHECK(out[6].clear_depth);  // a do fim (depois do último desenho)
    // Os provisórios: antes da limpeza, quando tudo antes dela é opaco.
    std::vector<DrawItem> items{rigid(1, make_matrix(0, 0, 0, 0, 0, 0)), clear_depth_item(),
                                rigid(2, make_matrix(0, 0, 0, 0, 0, 0), 1, 2)};
    CHECK(provisional_insert_point(items) - items.begin() == 1);
    items[0].blend = Blend::Alpha;
    CHECK(provisional_insert_point(items) - items.begin() == 0);
    std::vector<DrawItem> plain{rigid(1, make_matrix(0, 0, 0, 0, 0, 0))};
    CHECK(provisional_insert_point(plain) == plain.end());
}

TEST(interp_keeps_previous_before_z_clear) {
    // A sala que o tick anterior via e o atual não vê mais entra no fim da lista 0, antes da limpeza do Z: nada do
    // cenário é desenhado depois dela, por cima da arma (lista 2), que gira com o olhar sem levar a limpeza junto.
    int room = 0, hall = 0, gun = 0;
    const auto m = make_matrix(0, 0, 0, 0, 0, 0);
    FrameInterpolator fi;
    fi.push(record({rigid(1, m, 1, 0, PsiOwnerKind::Cel, &room), rigid(9, m, 1, 0, PsiOwnerKind::Cel, &hall),
                    clear_depth_item(), rigid(4, m, 2, 2, PsiOwnerKind::Object, &gun)}));
    fi.push(record({rigid(9, m, 1, 0, PsiOwnerKind::Cel, &hall), clear_depth_item(),
                    rigid(4, m, 2, 2, PsiOwnerKind::Object, &gun)}));
    CHECK(fi.stats().kept == 1);
    InterpCamera cam;
    const std::vector<DrawItem>& f = fi.build(0.5f, &cam);
    CHECK(f.size() == 4);
    if (f.size() != 4) return;
    CHECK(f[0].interp.id == 9 && f[1].interp.id == 1);
    CHECK(f[2].clear_depth);
    CHECK(f[3].interp.id == 4 && f[3].interp.list == 2);
    float W[9];
    std::array<float, 16> c = camera_matrix(0.0f, 0.0f, 0, 0, 0);
    camera_look_delta(c.data(), 0.3f, 0.0f, 1.5707964f, W);
    const float origin[3] = {0, 0, 0};
    fi.rotate_overlay(W, origin);  // gira o quadro montado (f), até o próximo build
    CHECK(f[2].clear_depth && f[3].world != m && f[0].world == m && f[1].world == m);
}
