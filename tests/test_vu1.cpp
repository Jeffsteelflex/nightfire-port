// Testes da referência das rotinas da VU1 e da névoa (src/platform/vu1.cpp), com valores calculados à
// mão a partir das fórmulas do microprograma (docs/formats.md, "Microprograma da VU1"), e do que o
// registro de psi_draw faz com a névoa (psiDrawFogPass, o alfa dos transparentes em psiDrawObjectMatrix).

#include <cmath>
#include <cstring>
#include <limits>

#include "engine/view.h"
#include "platform/psi_draw.h"
#include "platform/vu1.h"
#include "test_framework.h"

namespace {

vu1::Lights no_lights() {
    vu1::Lights l{};
    for (auto& c : l.color) c[3] = 1.0f;  // psiLight_SetLights: rgb 0, w 1 sem a luz
    return l;
}

constexpr float kFar1Level2 = 9987520.0f;  // psiPreGameRun, 07000002

}  // namespace

TEST(vu1_tweak_multiplies_and_truncates) {
    const float c[4] = {200, 100, 128, 128};
    const float t[4] = {0x80 * 0.003921569f, 1.0f, 0.25f, 0.5f};
    float o[4];
    vu1::tweak_vertex(c, t, o);
    // 200 * 0.50196 = 100.39 -> 100 (FTOI0 trunca).
    CHECK(o[0] == 100 && o[1] == 100 && o[2] == 32 && o[3] == 64);
    // Sem tweak (255 em tudo): TAmbient é 1 e a cor passa intacta.
    const float one[4] = {255 * 0.003921569f, 255 * 0.003921569f, 255 * 0.003921569f, 255 * 0.003921569f};
    vu1::tweak_vertex(c, one, o);
    CHECK(o[0] == 200 && o[1] == 100 && o[2] == 128 && o[3] == 128);
}

TEST(vu1_light_adds_attenuated_lambert_term) {
    const float c[4] = {64, 64, 64, 128};
    const float t[4] = {1, 1, 1, 1};
    const float p[3] = {0, 0, 0};
    const float up[3] = {0, 127, 0};  // normal do disco: n = 127 / 128
    vu1::Lights l = no_lights();
    l.pos[0][1] = 2.0f;
    l.color[0][0] = 255.0f;
    l.color[0][1] = 128.0f;
    l.color[0][3] = 1.0f / 16.0f;  // raio 4
    float o[4];
    vu1::light_vertex(c, up, p, t, l, o);
    // D = (0, 2, 0): att = 1/4 - 1/16 = 0.1875; n . D = 2 * 127/128 = 1.984375; s = 0.37207031.
    // r = 64 + 255 * s = 158.88 -> 158; g = 64 + 128 * s = 111.63 -> 111; b e a ficam.
    CHECK(o[0] == 158 && o[1] == 111 && o[2] == 64 && o[3] == 128);

    // De costas para a luz: n . D < 0, só o ambiente.
    const float down[3] = {0, -127, 0};
    vu1::light_vertex(c, down, p, t, l, o);
    CHECK(o[0] == 64 && o[1] == 64 && o[2] == 64);

    // Além do raio: 1/d² < 1/r², sem luz.
    l.pos[0][1] = 5.0f;
    vu1::light_vertex(c, up, p, t, l, o);
    CHECK(o[0] == 64 && o[1] == 64);

    // Perto: s = (4 - 1/16) * 0.49609375 = 1.95337; r e g saturam em 255.
    l.pos[0][1] = 0.5f;
    vu1::light_vertex(c, up, p, t, l, o);
    CHECK(o[0] == 255 && o[1] == 255 && o[2] == 64);

    // A segunda luz soma por cima, e o tweak é o termo ambiente (alfa também).
    l.pos[0][1] = 2.0f;
    l.pos[1][0] = 0.0f;
    l.pos[1][1] = 1.0f;
    l.color[1][2] = 100.0f;
    l.color[1][3] = 0.0f;  // raio infinito
    const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    vu1::light_vertex(c, up, p, half, l, o);
    // luz 1: D = (0, 1, 0), att = 1, s = 0.9921875 -> b = 32 + 99.22 = 131.
    CHECK(o[0] == 126 && o[1] == 79 && o[2] == 131 && o[3] == 64);
}

TEST(vu1_lights_to_local_inverts_rotation_and_translation) {
    // Rotação de 90 graus em y (vetor-linha) e translação (10, 0, 5).
    MATRIX m{};
    m.m[0][2] = -1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][0] = 1.0f;
    m.m[3][0] = 10.0f;
    m.m[3][2] = 5.0f;
    m.m[3][3] = 1.0f;
    const float world[2][4] = {{11, 2, 3, 1}, {10, 0, 5, 1}};
    float local[2][4];
    vu1::lights_to_local(m, world, local);
    // Ida e volta: local * M = mundo.
    for (int i = 0; i < 2; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float back = local[i][0] * m.m[0][c] + local[i][1] * m.m[1][c] + local[i][2] * m.m[2][c] + m.m[3][c];
            CHECK_NEAR(back, world[i][c], 1e-5);
        }
        CHECK(local[i][3] == 1.0f);
    }
    CHECK_NEAR(local[1][0], 0, 0);  // a origem do objeto
    // A "inversa" do PS2 é a transposta: com escala ela não desfaz a matriz (preservado).
    MATRIX s{};
    s.m[0][0] = s.m[1][1] = s.m[2][2] = 2.0f;
    s.m[3][3] = 1.0f;
    const float one[2][4] = {{1, 0, 0, 1}, {0, 0, 0, 1}};
    vu1::lights_to_local(s, one, local);
    CHECK_NEAR(local[0][0], 2.0, 0);
}

TEST(vu1_env_st_from_normal_and_camera_rows) {
    const float right[3] = {1, 0, 0}, up[3] = {0, 1, 0};
    float st[2];
    const float facing[3] = {0, 0, 127};
    vu1::env_st(facing, right, up, st);
    CHECK_NEAR(st[0], 0.5, 0);
    CHECK_NEAR(st[1], 0.5, 0);
    const float side[3] = {127, -64, 0};
    vu1::env_st(side, right, up, st);
    CHECK_NEAR(st[0], 0.5 + 127.0 / 256.0, 1e-7);
    CHECK_NEAR(st[1], 0.25, 1e-7);
}

TEST(vu1_gs_z_spans_far1_to_near1) {
    // No plano de perto (Ps2Near2 = 0.1 m) o Z é Ps2Far1; no infinito, Ps2Near1 - Ps2Far1 * 0.1 / 2^24.
    CHECK_NEAR(vu1::screen_z(0.1f, kFar1Level2), kFar1Level2, kFar1Level2 * 1e-6);
    CHECK_NEAR(vu1::screen_z(1e10f, kFar1Level2), 8.0 - kFar1Level2 * 0.1 / 16777216.0, 1e-3);
    CHECK_NEAR(vu1::screen_z(0.1f, 4194240.0f), 4194240.0, 4.0);
    // 60 m em 07000002: A / 60 + B = 998751.2 / 60 + 7.94 = 16653.8 -> 16653.
    CHECK(vu1::gs_z(60.0f, kFar1Level2) == 16653);
    float a, b;
    vu1::screen_z_coefficients(kFar1Level2, a, b);
    CHECK_NEAR(a, 998751.2, 0.5);
    CHECK_NEAR(b, 7.9405, 1e-3);
}

TEST(vu1_fog_keeps_the_z_bits_8_to_15) {
    CHECK(vu1::fog_keep(1) == 0.0f);  // Z apagado: só a névoa
    CHECK(vu1::fog_keep(0x7fff) == 1.0f);
    CHECK(vu1::fog_keep(0x10000) == 1.0f);
    CHECK(vu1::fog_keep(0x7ffe) == 127.0f / 128.0f);
    CHECK(vu1::fog_keep(0x4000) == 0.5f);
    // 07000002: a névoa começa onde A / z + B = 0x7fff, a 30,49 m, e fica 65/128 aos 60 m.
    CHECK(vu1::fog_keep(vu1::gs_z(30.4f, kFar1Level2)) == 1.0f);
    CHECK(vu1::fog_keep(vu1::gs_z(30.6f, kFar1Level2)) < 1.0f);
    CHECK(vu1::fog_keep(vu1::gs_z(60.0f, kFar1Level2)) == 65.0f / 128.0f);
    CHECK(vu1::fog_keep(vu1::gs_z(1000.0f, kFar1Level2)) == 3.0f / 128.0f);
}

TEST(vu1_alpha_fog_scales_the_tweak_alpha) {
    CHECK(vu1::alpha_fog(32640.0f, 200) == 200);   // >> 7 = 255
    CHECK(vu1::alpha_fog(100000.0f, 200) == 200);  // satura em 255
    CHECK(vu1::alpha_fog(16384.0f, 200) == 100);   // 128 * 200 / 255 = 100.4
    CHECK(vu1::alpha_fog(std::numeric_limits<float>::infinity(), 77) == 77);
    // Atrás da câmera o Z é negativo: -1000 >> 7 = -8, -8 * 200 / 255 = -6, que vira 250 (uchar).
    CHECK(vu1::alpha_fog(-1000.0f, 200) == 250);
}

namespace {
// Uma câmera na origem olhando para +z, 60 graus, 4:3.
void look_forward(viewer_tag& v) {
    std::memset(&v, 0, sizeof(v));
    v.camera.m[0][0] = v.camera.m[1][1] = v.camera.m[2][2] = v.camera.m[3][3] = 1.0f;
    v.fov = 1.0471976f;
    v.aspect = 4.0f / 3.0f;
}

// Modelo com uma caixa de 1 m de lado em volta da origem (cabeçalho de geometria já com ponteiros).
struct OneBox {
    float box[14]{-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
    uint32_t hdr[3]{};
    entgfx_tag gfx{};
    celglist_tag glist{};
    OneBox() {
        hdr[0] = 1;
        hdr[1] = uint32_t(reinterpret_cast<uintptr_t>(box));
        gfx.ps2_gfx = reinterpret_cast<uint8_t*>(hdr);
        glist.gfx = &gfx;
    }
};

MATRIX at_z(float z) {
    MATRIX m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    m.m[3][2] = z;
    return m;
}
}  // namespace

TEST(psi_alpha_objects_fade_with_distance_when_fogged) {
    psi_draw_debug_power_on();
    viewer_tag v;
    look_forward(v);
    OneBox model;
    FogOn = 1;
    Ps2Far1 = kFar1Level2;
    psiPreDraw();
    psiUseCamera(&v, 1);
    psiStartAlpha();
    CHECK(AlphaFogObjects == 1);
    psiSetTweakARGB(200, 0xff, 0xff, 0xff);
    // A 60 m: Z = 16653, >> 7 = 130, 130 * 200 / 255 = 101.
    MATRIX m = at_z(60.0f);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws.size() == 1 && g_psi_frame.draws[0].tweak[0] == 101 && olda == 101);
    // Como no PS2, o próximo desenho parte do alfa já reduzido: 130 * 101 / 255 = 51.
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws[1].tweak[0] == 51);
    // Perto (Z acima de 32640) o alfa fica.
    psiSetTweakARGB(200, 0xff, 0xff, 0xff);
    m = at_z(10.0f);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws[2].tweak[0] == 200);
    // Caixa cortando o plano de perto (precisa de recorte): fica o alfa que estava.
    psiSetTweakARGB(90, 0xff, 0xff, 0xff);
    m = at_z(0.3f);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws[3].tweak[0] == 90);
    // Fora da tela (atrás da câmera): nada seria desenhado, o tweak não muda; o registro fica.
    m = at_z(-100.0f);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws.size() == 5 && olda == 90);
    // Sem névoa não há redução.
    psiEndAlpha();
    FogOn = 0;
    psiStartAlpha();
    m = at_z(60.0f);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.draws[5].tweak[0] == 90);
    psi_draw_debug_power_on();
}

TEST(psi_fog_pass_is_recorded_after_the_solids) {
    psi_draw_debug_power_on();
    OneBox model;
    MATRIX m = at_z(5.0f);
    psiPreDraw();
    psiDrawObjectMatrix(&model.glist, &m, 0);
    psiPostSolid();
    CHECK(g_psi_frame.fog_at == -1);  // FogOn desligado
    FogOn = 1;
    PS2FogR = 0x24, PS2FogG = 0x2e, PS2FogB = 0x4c;
    Ps2Far1 = kFar1Level2;
    psiPreDraw();
    psiDrawObjectMatrix(&model.glist, &m, 0);
    psiDrawObjectMatrix(&model.glist, &m, 0);
    psiPostSolid();
    psiDrawObjectMatrix(&model.glist, &m, 0);
    CHECK(g_psi_frame.fog_at == 2 && g_psi_frame.fog_far1 == kFar1Level2);
    CHECK(g_psi_frame.fog_rgb[0] == 0x24 && g_psi_frame.fog_rgb[1] == 0x2e && g_psi_frame.fog_rgb[2] == 0x4c);
    CHECK(g_psi_frame.draws[2].after_solid == 1 && g_psi_frame.draws[1].after_solid == 0);
    psiPreDraw();
    CHECK(g_psi_frame.fog_at == -1);
    psi_draw_debug_power_on();
}

TEST(psi_set_tweak_a_changes_only_the_alpha) {
    psi_draw_debug_power_on();
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    CHECK(TweakRGBA == 0);
    psiSetTweakA(0x80);
    CHECK(olda == 0x80 && TweakRGBA == 1 && oldr == 0xff);
    CHECK_NEAR(TAmbient[3], 0x80 * 0.003921569, 1e-7);
    CHECK_NEAR(TAmbient[0], 1.0, 1e-6);
    psiSetTweakA(0xff);
    CHECK(TweakRGBA == 0);
    psi_draw_debug_power_on();
}
