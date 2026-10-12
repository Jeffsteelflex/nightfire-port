// Testes da mira moderna e da roda de armas (engine/player_ads.h, engine/weapon_wheel.h, ui/modern_wheel.h,
// platform/pc_gameplay.h, o agendador plan_tick de render/frame_interp.h): as contas sem o jogo. Os casos com o jogador
// (a pose na arma na mão, a escolha ao soltar a roda) estão em test_weapon.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "data/pack_images.h"
#include "engine/menu_dossier.h"
#include "engine/player_ads.h"
#include "engine/player_weapon.h"
#include "engine/weapon_wheel.h"
#include "platform/pc_gameplay.h"
#include "platform/settings.h"
#include "render/frame_interp.h"
#include "test_framework.h"
#include "ui/modern_wheel.h"

namespace {

// Uma pistola feita à mão no espaço do objeto da arma (x para o lado, y para cima, z para a frente), com o meio em
// x = -0,3: o ferrolho uma caixa de 25 cm (z 1,40 a 1,65, y -0,25 a -0,20) só com vértices nas bordas, a alça (duas
// orelhas de 6 mm nas bordas, atrás) e a massa (um poste de 6 mm no meio, na frente), o cabo descendo e, atrás da mão, uma
// "coronha" mais alta que a linha (a mão em z 1,40: a silhueta começa 10 cm atrás dela, então a coronha fica de fora).
std::vector<std::array<float, 3>> pistol() {
    std::vector<std::array<float, 3>> p;
    const float x0 = -0.32f, x1 = -0.28f;
    for (float z : {1.40f, 1.45f, 1.50f, 1.55f, 1.60f, 1.65f}) {
        for (float x : {x0, x1}) {
            p.push_back({x, -0.20f, z});
            p.push_back({x, -0.25f, z});
        }
    }
    // A alça: as orelhas (bordas) até -0,194, atrás.
    for (float x : {x0, x1}) {
        p.push_back({x, -0.194f, 1.41f});
        p.push_back({x, -0.194f, 1.42f});
    }
    // A massa: no meio, na frente.
    p.push_back({-0.30f, -0.194f, 1.63f});
    p.push_back({-0.30f, -0.194f, 1.64f});
    // O cabo.
    for (float y : {-0.30f, -0.35f, -0.40f}) p.push_back({-0.30f, y, 1.42f});
    // A coronha alta, atrás da mão.
    p.push_back({-0.30f, -0.10f, 1.10f});
    p.push_back({-0.30f, -0.10f, 1.20f});
    return p;
}

}  // namespace

TEST(ads_finds_the_sight_line_of_a_hand_built_pistol) {
    const auto p = pistol();
    const float muzzle[3] = {-0.30f, -0.225f, 1.66f};
    AdsSights s;
    AdsFindStats st;
    CHECK(ads_find_sights(reinterpret_cast<const float(*)[3]>(p.data()), p.size(), muzzle, 1.40f - 0.1f, &s, &st));
    // A linha passa pelo alto da alça e da massa (y -0,194), no meio da arma, de trás para a frente.
    CHECK_NEAR(s.rear[0], -0.30f, 1e-6);
    CHECK_NEAR(s.front[0], -0.30f, 1e-6);
    CHECK_NEAR(s.rear[1], -0.194f, 1e-5);
    CHECK_NEAR(s.front[1], -0.194f, 1e-5);
    CHECK(s.rear[2] < 1.43f && s.front[2] > 1.62f);
    CHECK(st.span > 0.2f);
    // Sem o corte da mão a coronha alta (y -0,10) entra na silhueta e a linha muda: o corte importa.
    AdsSights s2;
    ads_find_sights(reinterpret_cast<const float(*)[3]>(p.data()), p.size(), muzzle, -10.0f, &s2, nullptr);
    CHECK(std::fabs(s2.rear[1] - s.rear[1]) > 0.01f || std::fabs(s2.rear[2] - s.rear[2]) > 0.05f);
    // Sem vértices: falso e o eixo do cano (25 cm até o anexo).
    AdsSights s3;
    CHECK(!ads_find_sights(nullptr, 0, muzzle, 0.0f, &s3, nullptr));
    CHECK_NEAR(s3.front[2], 1.66f, 1e-6);
    CHECK_NEAR(s3.rear[2], 1.41f, 1e-6);
    CHECK_NEAR(s3.rear[1], -0.225f, 1e-6);
}

TEST(ads_pose_puts_the_sights_on_the_screen_centre) {
    // Uma linha inclinada e de lado (como as da tabela, que sobem alguns graus): a pose põe a alça a `depth` metros na
    // frente do olho, no centro, e a massa mais à frente na mesma linha (x = y = 0); com drop, abaixo do centro.
    const AdsSights s{{-0.3166f, -0.1934f, 1.4128f}, {-0.31f, -0.1076f, 1.9159f}};
    for (const float depth : {0.35f, 0.6f, 1.2f}) {
        for (const float drop : {0.0f, 0.035f}) {
            const AdsPose pose = ads_pose(s, depth, drop);
            float r[3], f[3];
            ads_view_point(pose.rot, pose.pos, s.rear, r);
            ads_view_point(pose.rot, pose.pos, s.front, f);
            CHECK_NEAR(r[0], 0.0f, 1e-5);
            CHECK_NEAR(r[1], -drop, 1e-5);
            CHECK_NEAR(r[2], depth, 1e-5);
            CHECK_NEAR(f[0], 0.0f, 1e-5);
            CHECK_NEAR(f[1], -drop, 1e-5);
            const float len = std::sqrt((s.front[0] - s.rear[0]) * (s.front[0] - s.rear[0]) +
                                        (s.front[1] - s.rear[1]) * (s.front[1] - s.rear[1]) +
                                        (s.front[2] - s.rear[2]) * (s.front[2] - s.rear[2]));
            CHECK_NEAR(f[2] - r[2], len, 1e-5);  // a rotação não estica a arma
        }
    }
    // Uma linha já reta para a frente: só translação (sem giro) e o "cima" da arma fica para cima.
    const AdsSights flat{{0.1f, 0.2f, 1.0f}, {0.1f, 0.2f, 1.5f}};
    const AdsPose p = ads_pose(flat, 0.5f, 0.0f);
    CHECK_NEAR(p.rot[0], 0.0f, 1e-6);
    CHECK_NEAR(p.rot[1], 0.0f, 1e-6);
    CHECK_NEAR(p.rot[2], 0.0f, 1e-6);
    const float up[3] = {0.1f, 0.3f, 1.0f};
    float u[3];
    ads_view_point(p.rot, p.pos, up, u);
    CHECK_NEAR(u[1], 0.1f, 1e-5);
}

TEST(ads_blend_moves_in_about_ten_ticks_and_eases) {
    // A 60 Hz o peso vai de 0 a 1 em 10 ticks (~0,17 s); a 30 Hz (FRAME_RATE_MUL 2) em 5; volta do mesmo jeito.
    float t = 0.0f;
    int ticks = 0;
    while (t < 1.0f && ticks < 100) {
        t = ads_step_weight(t, true, 1.0f);
        ++ticks;
    }
    CHECK(ticks == 10);
    float t2 = 0.0f;
    for (int i = 0; i < 5; ++i) t2 = ads_step_weight(t2, true, 2.0f);
    CHECK_NEAR(t2, 1.0f, 1e-6);
    for (int i = 0; i < 10; ++i) t = ads_step_weight(t, false, 1.0f);
    CHECK(t == 0.0f);
    // A curva: 0 e 1 nas pontas, simétrica, crescente e suave (sem degrau entre ticks maior que o do meio).
    CHECK(ads_ease(0.0f) == 0.0f && ads_ease(1.0f) == 1.0f);
    CHECK_NEAR(ads_ease(0.5f), 0.5f, 1e-6);
    float prev = 0.0f, max_step = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        const float e = ads_ease(float(i) / 10.0f);
        CHECK(e > prev);
        CHECK_NEAR(ads_ease(1.0f - float(i) / 10.0f), 1.0f - e, 1e-6);
        max_step = std::max(max_step, e - prev);
        prev = e;
    }
    CHECK(max_step < 0.16f);
}

TEST(ads_aim_style_resolves_by_device) {
    // auto: moderno com o teclado e o mouse (aparelho 0), o do jogo com o controle (1); os outros não dependem dele.
    CHECK(ads_modern_for(AimStyle::Auto, 0) && !ads_modern_for(AimStyle::Auto, 1));
    CHECK(ads_modern_for(AimStyle::Modern, 0) && ads_modern_for(AimStyle::Modern, 1));
    CHECK(!ads_modern_for(AimStyle::Classic, 0) && !ads_modern_for(AimStyle::Classic, 1));
    AimStyle st = AimStyle::Auto;
    for (const AimStyle s : {AimStyle::Auto, AimStyle::Modern, AimStyle::Classic}) {
        CHECK(ads_parse_aim_style(ads_aim_style_name(s), &st) && st == s);
    }
    CHECK(!ads_parse_aim_style("sideways", &st));
    // O .ini: só as chaves que saíram do padrão (ou que o arquivo já tinha) são gravadas, e voltam iguais.
    nf::Settings ini;
    nf::PcGameplay g;
    nf::save_pc_gameplay(ini, g);
    CHECK(!ini.has("game", "aim_style") && !ini.has("game", "wheel_slowmo"));
    g.aim_style = uint8_t(AimStyle::Classic);
    g.wheel_slowmo = 0.5f;
    nf::save_pc_gameplay(ini, g);
    nf::PcGameplay back;
    nf::load_pc_gameplay(ini, &back);
    CHECK(back.aim_style == uint8_t(AimStyle::Classic));
    CHECK_NEAR(back.wheel_slowmo, 0.5f, 1e-6);
    nf::Settings bad;
    bad.parse("[game]\naim_style = upside\nwheel_slowmo = 7\n");
    nf::PcGameplay b2;
    nf::load_pc_gameplay(bad, &b2);
    CHECK(b2.aim_style == 0 && b2.wheel_slowmo == 1.0f);
    // Os valores da roda nos menus.
    CHECK(nf::wheel_slowmo_index(0.25f) == 2 && nf::kWheelSlowmoValues[nf::wheel_slowmo_index(0.3f)] == 0.25f);
}

TEST(weapon_wheel_picks_the_slot_in_the_pointed_direction) {
    // Oito armas e quatro aparelhos: o espaço 0 em cima, no sentido horário; o meio não aponta nada.
    CHECK(weapon_wheel_pick(0.0f, 0.0f, 8, 4).ring == 0);
    CHECK(weapon_wheel_pick(0.1f, 0.1f, 8, 4).ring == 0);
    const WheelPick up = weapon_wheel_pick(0.0f, 1.0f, 8, 4);
    CHECK(up.ring == 2 && up.index == 0);
    const WheelPick right = weapon_wheel_pick(1.0f, 0.0f, 8, 4);
    CHECK(right.ring == 2 && right.index == 2);
    const WheelPick down = weapon_wheel_pick(0.0f, -0.9f, 8, 4);
    CHECK(down.ring == 2 && down.index == 4);
    const WheelPick left = weapon_wheel_pick(-0.8f, 0.0f, 8, 4);
    CHECK(left.ring == 2 && left.index == 6);
    // Perto do meio, o anel de dentro (os aparelhos): 4 aparelhos ocupam 4 dos 6 espaços fixos; à direita é o 2.
    const WheelPick inner = weapon_wheel_pick(0.4f, 0.0f, 8, 4);
    CHECK(inner.ring == 1 && inner.index == 2);
    // A fronteira entre dois espaços é a metade do ângulo: 8 espaços de 45 graus, o 0 vai de -22,5 a 22,5.
    const float a1 = 0.3926991f - 0.01f, a2 = 0.3926991f + 0.01f;
    CHECK(weapon_wheel_pick(std::sin(a1), std::cos(a1), 8, 4).index == 0);
    CHECK(weapon_wheel_pick(std::sin(a2), std::cos(a2), 8, 4).index == 1);
    // Logo à esquerda de cima: o último espaço (não um índice negativo).
    CHECK(weapon_wheel_pick(-0.05f, 1.0f, 8, 4).index == 0);
    CHECK(weapon_wheel_pick(-0.6f, 0.8f, 8, 4).index == 7);
    // Sem aparelhos, o anel das armas vale desde a borda do meio; sem armas, o dos aparelhos.
    CHECK(weapon_wheel_pick(0.4f, 0.0f, 8, 0).ring == 2);
    CHECK(weapon_wheel_pick(0.9f, 0.0f, 0, 4).ring == 1);
    CHECK(weapon_wheel_pick(0.9f, 0.0f, 0, 0).ring == 0);
    // O ângulo de cada arma (no seu espaço fixo) volta a ela pelo pick, em qualquer quantidade.
    for (int n = 1; n <= kWheelMaxWeapons; ++n) {
        for (int i = 0; i < n; ++i) {
            const float a = weapon_wheel_item_angle(i, n, false);
            const WheelPick p = weapon_wheel_pick(std::sin(a), std::cos(a), n, 0);
            CHECK(p.ring == 2 && p.index == i);
        }
    }
    for (int n = 1; n <= kWheelMaxGadgets; ++n) {
        for (int i = 0; i < n; ++i) {
            const float a = weapon_wheel_item_angle(i, n, true);
            const WheelPick p = weapon_wheel_pick(std::sin(a) * 0.45f, std::cos(a) * 0.45f, 3, n);
            CHECK(p.ring == 1 && p.index == i);
        }
    }
}

TEST(weapon_wheel_slots_have_a_fixed_size) {
    // Os espaços não crescem com poucas armas: 8 de 45 graus no anel de fora até 8 armas, 12, 16 e 24 depois; 6 de
    // dentro até 6 aparelhos, 8, 12 e 16 depois. A quantidade de espaços sempre cabe as armas.
    for (int n = 0; n <= kWheelMaxWeapons; ++n) {
        const int s = weapon_wheel_ring_slots(n, false);
        CHECK(s >= n && s >= 8 && (s == 8 || s == 12 || s == 16 || s == 24));
        if (n > 0) CHECK(s >= weapon_wheel_ring_slots(n - 1, false));
    }
    CHECK(weapon_wheel_ring_slots(2, false) == 8 && weapon_wheel_ring_slots(8, false) == 8);
    CHECK(weapon_wheel_ring_slots(9, false) == 12 && weapon_wheel_ring_slots(13, false) == 16);
    for (int n = 0; n <= kWheelMaxGadgets; ++n) {
        const int s = weapon_wheel_ring_slots(n, true);
        CHECK(s >= n && s >= 6);
    }
    CHECK(weapon_wheel_ring_slots(1, true) == 6 && weapon_wheel_ring_slots(7, true) == 8);

    // Duas armas: em cima (o espaço 0) e a 45 graus (o 1); à direita e embaixo, espaços vazios (índices que passam das
    // armas: não escolhem nada).
    const float d45 = 0.7853982f;
    CHECK(weapon_wheel_pick(0.0f, 1.0f, 2, 0).index == 0);
    CHECK(weapon_wheel_pick(std::sin(d45), std::cos(d45), 2, 0).index == 1);
    const WheelPick right2 = weapon_wheel_pick(1.0f, 0.0f, 2, 0);
    CHECK(right2.ring == 2 && right2.index == 2);
    const WheelPick down2 = weapon_wheel_pick(0.0f, -1.0f, 2, 0);
    CHECK(down2.ring == 2 && down2.index == 4);
    // Nove armas: 12 espaços de 30 graus; à direita é o 3, embaixo o 6, à esquerda o 9 (vazio: só 0 a 8 têm arma).
    CHECK(weapon_wheel_pick(1.0f, 0.0f, 9, 0).index == 3);
    CHECK(weapon_wheel_pick(0.0f, -1.0f, 9, 0).index == 6);
    CHECK(weapon_wheel_pick(-1.0f, 0.0f, 9, 0).index == 9);
    const float d30 = 0.5235988f;
    CHECK(weapon_wheel_pick(std::sin(d30), std::cos(d30), 9, 0).index == 1);
    CHECK(weapon_wheel_pick(std::sin(d30 * 1.4f), std::cos(d30 * 1.4f), 9, 0).index == 1);
    CHECK(weapon_wheel_pick(std::sin(d30 * 1.6f), std::cos(d30 * 1.6f), 9, 0).index == 2);

    // A geometria desenhada: o espaço 0 em cima, todos do mesmo tamanho, sem se cobrir, com a folga entre eles; o nome
    // cabe nos anéis de até 16 espaços de fora e 8 de dentro (nos outros, só o ícone e a munição).
    const nf::ui::WheelGeometry& g = nf::ui::wheel_geometry();
    for (const auto& [ring, slots, names] : std::vector<std::tuple<int, int, bool>>{
             {2, 8, true}, {2, 12, true}, {2, 16, true}, {2, 24, false}, {1, 6, true}, {1, 8, true}, {1, 12, false},
             {1, 16, false}}) {
        const nf::ui::WheelSlotGeom first = nf::ui::wheel_slot_geometry(g, ring, 0, slots);
        CHECK(std::fabs(first.px - g.cx) < 0.01f && first.py < g.cy);
        CHECK(first.names == names);
        const float span = first.a1 - first.a0;
        for (int i = 0; i < slots; ++i) {
            const nf::ui::WheelSlotGeom s = nf::ui::wheel_slot_geometry(g, ring, i, slots);
            const nf::ui::WheelSlotGeom next = nf::ui::wheel_slot_geometry(g, ring, i + 1, slots);
            CHECK(std::fabs((s.a1 - s.a0) - span) < 1e-4f);
            CHECK(span < 2.0f * 3.14159265f / float(slots) && span > 2.0f * 3.14159265f / float(slots) * 0.9f);
            CHECK(s.a1 < next.a0);  // a folga
            CHECK(std::fabs(std::hypot(s.px - g.cx, s.py - g.cy) - s.rm) < 0.01f);
            CHECK(s.box > 0.0f && s.box <= s.r1 - s.r0 + 1e-3f);
            CHECK(s.r0 == (ring == 2 ? g.outer0 : g.inner0) && s.r1 == (ring == 2 ? g.outer1 : g.inner1));
        }
    }
    // Os anéis não se tocam: o miolo, o de dentro e o de fora.
    CHECK(g.hole < g.inner0 && g.inner0 < g.inner1 && g.inner1 < g.outer0 && g.outer0 < g.outer1);
}

TEST(weapon_wheel_icons_come_from_the_dossier_tables) {
    // As imagens do dossiê: com as tabelas copiadas (aqui marcas), cada classe com imagem pega a de uma linha de
    // ds_weapons ou ds_gadgets (as pistolas e o barbeador, as imagens fixas dos tratadores do dossiê).
    M_ITEM save_w[27], save_g[14];
    std::memcpy(save_w, ds_weapons, sizeof(save_w));
    std::memcpy(save_g, ds_gadgets, sizeof(save_g));
    for (int i = 0; i < 27; ++i) ds_weapons[i].image = 0x3100000u + uint32_t(i);
    for (int i = 0; i < 14; ++i) ds_gadgets[i].image = 0x3200000u + uint32_t(i);
    int with_icon = 0;
    for (int c = 0; c < 0x73; ++c) {
        const uint32_t icon = weapon_wheel_icon(c);
        const WheelIconRef r = weapon_wheel_icon_ref(c);
        CHECK((icon != 0) == (r.table != 0));
        if (icon == 0) continue;
        ++with_icon;
        // Só as classes da roda: as armas (1 a 0x46) e os aparelhos (0x4a a 0x5e).
        CHECK((c >= 1 && c <= 0x46) || (c >= 0x4a && c <= 0x5e));
        if (r.table == 1) CHECK(icon == 0x3100000u + uint32_t(r.index));
        if (r.table == 2) CHECK(icon == 0x3200000u + uint32_t(r.index));
        // Os aparelhos vêm da tabela dos aparelhos (ou das imagens fixas), nunca da das armas.
        if (c >= 0x4a) CHECK(r.table != 1);
        if (r.table == 3) CHECK((icon >> 24) == 3);
    }
    CHECK(with_icon >= 40);
    // O soco não tem imagem; as variantes de uma classe dividem a imagem (a Tactical Sniper e as melhorias dela).
    CHECK(weapon_wheel_icon(1) == 0);
    CHECK(weapon_wheel_icon(0x1e) == weapon_wheel_icon(0x20) && weapon_wheel_icon(0x20) == weapon_wheel_icon(0x22));
    // As quatro pistolas (os níveis de melhoria de Upgrade_Weapon) têm imagens diferentes.
    const uint8_t save_up = Upgrades[0];
    uint32_t pistols[4];
    for (int level = 0; level < 4; ++level) {
        Upgrades[0] = uint8_t(level);
        pistols[level] = weapon_wheel_icon(int(Upgrade_Weapon(6)));
        CHECK(pistols[level] != 0);
        for (int j = 0; j < level; ++j) CHECK(pistols[j] != pistols[level]);
    }
    Upgrades[0] = save_up;
    // A lista das imagens a carregar: sem zeros nem repetidas, com todas as que a tabela dá.
    uint32_t list[96];
    const int n = weapon_wheel_icon_hashes(list, 96);
    for (int i = 0; i < n; ++i) {
        CHECK(list[i] != 0);
        for (int j = 0; j < i; ++j) CHECK(list[i] != list[j]);
    }
    for (int c = 0; c < 0x73; ++c) {
        const uint32_t icon = weapon_wheel_icon(c);
        if (icon != 0) CHECK(std::find(list, list + n, icon) != list + n);
    }
    std::memcpy(ds_weapons, save_w, sizeof(save_w));
    std::memcpy(ds_gadgets, save_g, sizeof(save_g));
}

TEST(weapon_wheel_icon_cuts_the_radar_disc) {
    // Uma imagem do dossiê feita à mão (64 x 64): o disco verde do radar com o anel claro, uma "arma" cinza no meio, um
    // pedaço verde-oliva (uma granada) e o preto fora do disco.
    const int w = 64, h = 64;
    std::vector<uint8_t> img(size_t(w) * h * 4);
    const auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* p = img.data() + (size_t(y) * w + x) * 4;
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = 0x80;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float rr = std::hypot(x + 0.5f - 32.0f, y + 0.5f - 32.0f) / 64.0f;
            if (rr > 0.45f) put(x, y, 0, 1, 0);
            else if (rr > 0.40f) put(x, y, 70, 186, 53);  // o anel claro
            else put(x, y, 27, 74, 28);                    // o fundo do radar
        }
    }
    for (int y = 24; y < 40; ++y) {
        for (int x = 20; x < 32; ++x) put(x, y, 90, 92, 86);  // a arma (cinza)
        for (int x = 34; x < 42; ++x) put(x, y, 53, 75, 40);  // o verde-oliva
    }
    const std::vector<uint8_t> out = nf::ui::wheel_icon_image(img.data(), w, h);
    CHECK(out.size() == img.size());
    if (!(out.size() == img.size())) return;
    const auto px = [&](int x, int y) { return out.data() + (size_t(y) * w + x) * 4; };
    // A arma e a granada ficam (opacas, alfa do GS); o fundo do radar, o anel e o preto de fora somem.
    CHECK(px(26, 30)[3] >= 0x7e);
    CHECK(px(38, 30)[3] >= 0x70);
    CHECK(px(10, 32)[3] == 0);   // o radar
    CHECK(px(32, 5)[3] == 0);    // o anel claro (fora do disco)
    CHECK(px(1, 1)[3] == 0);     // o preto do canto
    // Um tom só: r = g = b; a arma cinza fica mais clara que o mínimo levantado (0,30).
    for (int i = 0; i < w * h; ++i) {
        CHECK(out[size_t(i) * 4] == out[size_t(i) * 4 + 1] && out[size_t(i) * 4 + 1] == out[size_t(i) * 4 + 2]);
    }
    CHECK(px(26, 30)[0] > uint8_t(0.30f * 255.0f) && px(26, 30)[0] < 255);
    // Sem imagem: vazio.
    CHECK(nf::ui::wheel_icon_image(nullptr, 0, 0).empty());
}

TEST(slowmo_tick_scheduler_runs_a_quarter_of_the_ticks) {
    // Um segundo a 60 VBlanks: um tick por VBlank em tempo real, 15 a 0,25, 6 a 0,1, nenhum a 0.
    for (const auto& [slowmo, expected] : std::vector<std::pair<float, int>>{{1.0f, 60}, {0.25f, 15}, {0.1f, 6}, {0.0f, 0}}) {
        float acc = 0.0f;
        int ticks = 0, consumed = 0;
        for (int v = 0; v < 60; ++v) {
            const nf::TickPlan p = nf::plan_tick(1, false, slowmo, &acc);
            CHECK(p.due && p.consumed == 1);
            consumed += p.consumed;
            if (p.run) {
                ++ticks;
                CHECK(p.skip == 1);
            }
        }
        CHECK(ticks == expected);
        CHECK(consumed == 60);  // o relógio do jogo (GameCount) anda em tempo real do mesmo jeito
    }
    // O ritmo do PS2 sem câmera lenta continua igual: 2 VBlanks de atraso viram um tick de 30 Hz; mais, o reacerto.
    float acc = 0.0f;
    nf::TickPlan p = nf::plan_tick(2, false, 1.0f, &acc);
    CHECK(p.run && p.skip == 2 && p.consumed == 2 && !p.resync);
    p = nf::plan_tick(5, false, 1.0f, &acc);
    CHECK(p.run && p.skip == 2 && p.resync);
    p = nf::plan_tick(0, false, 1.0f, &acc);
    CHECK(!p.due && !p.run);
    p = nf::plan_tick(0, true, 1.0f, &acc);
    CHECK(p.due && p.run);

    // A interpolação continua contínua na câmera lenta: um monitor de 60 Hz (um VBlank por quadro de vídeo), ticks a
    // cada 4 VBlanks; o tempo mostrado (o tick anterior mais alpha ticks, como o FrameInterpolator desenha) anda um
    // quarto de tick por quadro, sem saltos nem paradas depois do primeiro tick lento.
    acc = 0.0f;
    double tick_vblank = 0.0, game_time = 0.0, last_shown = -1.0;
    double rec_len = 1.0;     // os VBlanks entre os dois últimos registros (TickRecord::ticks)
    double since_rec = 0.0;   // os VBlanks somados desde o último tick (vblanks_since_draw)
    int slow_ticks = 0;
    for (int v = 1; v <= 80; ++v) {
        const nf::TickPlan q = nf::plan_tick(1, false, 0.25f, &acc);
        since_rec += q.consumed;
        if (q.run) {
            game_time += 1.0;
            tick_vblank = double(v);
            rec_len = since_rec;
            since_rec = 0.0;
            ++slow_ticks;
        }
        // O quadro mostrado no meio do VBlank seguinte (o relógio fracionário de psi_clock_vblanks).
        const double now = double(v) + 0.5;
        const float alpha = nf::tick_alpha(now, tick_vblank, rec_len);
        const double shown = (game_time - 1.0) + double(alpha);
        if (slow_ticks >= 2) {
            CHECK(shown >= last_shown);
            CHECK(shown - last_shown <= 0.25 + 1e-6);
            CHECK(shown - last_shown >= 0.25 - 1e-6);
        }
        last_shown = shown;
    }
    CHECK(slow_ticks == 20);
}

TEST(weapon_wheel_draws_rings_hover_and_prompt) {
    // A vista da roda vira os espaços fixos (8 de fora, 6 de dentro: os vazios também), o realce do apontado, os ícones,
    // os nomes e a linha de baixo.
    nf::ui::WheelView v;
    for (const wchar_t* n : {L"Wolfram P2K", L"Storm M32", L"Frinesi"}) {
        nf::ui::WheelViewSlot s;
        s.name = n;
        s.ammo = L"7-41";
        v.weapons.push_back(s);
    }
    v.weapons[0].current = true;
    v.weapons[0].icon = 0x30000f2;
    v.weapons[1].icon = 0x30000f8;
    v.weapons[2].usable = false;
    nf::ui::WheelViewSlot g;
    g.name = L"Q-Laser";
    g.ammo = L"—";
    v.gadgets.push_back(g);
    v.outer_slots = 8;
    v.inner_slots = 6;
    v.hover_ring = 2;
    v.hover_index = 1;
    v.sel_x = 0.8f;
    v.sel_y = 0.1f;
    v.key = L"Tab";
    v.in_hand = L"Wolfram P2K";
    const nf::ui::Canvas cv = nf::ui::make_canvas(1920, 1080);
    const nf::ui::Measure m = [](const std::wstring& t, const nf::ui::TextStyle& st) {
        return nf::ui::estimate_width(t, st);
    };
    const nf::ui::Tokens tk = nf::ui::default_tokens();
    nf::ui::DrawList back, front;
    nf::ui::ImageList icons;
    nf::ui::build_weapon_wheel(back, icons, front, cv, tk, m, v);
    // 8 + 6 espaços, cada um com o vidro e a linha fina da borda; o apontado (com arma e munição) troca a linha pelo
    // degradê de ouro, a borda de ouro e o brilho de brasa.
    CHECK(back.count(nf::ui::DrawCmd::Sector) == (8 - 1) * 2 + 4 + 6 * 2);
    CHECK(front.find_text(L"Storm M32") != nullptr && front.find_text(L"Q-Laser") != nullptr);
    // Os ícones: um por arma com imagem, imagens do port; o apontado em ouro claro, os outros não.
    CHECK(icons.size() == 2);
    if (!(icons.size() == 2)) return;
    CHECK(icons[0].port && icons[1].port && icons[0].hash == 0x30000f2 && icons[1].hash == 0x30000f8);
    CHECK(icons[1].tint == tk.color.accent_hi && !(icons[0].tint == tk.color.accent_hi));
    // O ícone fica no espaço dele (o 1, a 45 graus: acima e à direita do meio) e dentro do anel de fora.
    const float ix = icons[1].r.cx() - 960.0f, iy = 540.0f - icons[1].r.cy();
    const float ir = std::hypot(ix, iy);
    CHECK(ix > 0.0f && iy > 0.0f && ir > nf::ui::wheel_geometry().outer0 && ir < nf::ui::wheel_geometry().outer1);
    // O apontado em detalhe no meio; a arma da mão com o losango; o vetor de escolha também.
    CHECK(front.count(nf::ui::DrawCmd::Diamond) == 2);
    CHECK(front.find_text(L"TAB") != nullptr);  // a tecla (em maiúsculas, como as outras teclas do kit)
    // A pílula escura atrás da tecla e de "Release to equip" (o que a cena tem atrás não se mistura com a roda).
    CHECK(back.count(nf::ui::DrawCmd::Fill) == 1);
    // Apontar um espaço vazio: nada de ouro (só a linha da borda mais clara) e nenhum ícone em ouro.
    v.hover_index = 5;
    nf::ui::DrawList b2, f2;
    nf::ui::ImageList i2;
    nf::ui::build_weapon_wheel(b2, i2, f2, cv, tk, m, v);
    CHECK(b2.count(nf::ui::DrawCmd::Sector) == 8 * 2 + 6 * 2);
    for (const nf::ui::ImageCmd& ic : i2) CHECK(!(ic.tint == tk.color.accent_hi));
    // Fechada (shown 0): nada.
    v.shown = 0.0f;
    nf::ui::DrawList none, none2;
    nf::ui::ImageList none_i;
    nf::ui::build_weapon_wheel(none, none_i, none2, cv, tk, m, v);
    CHECK(none.cmds.empty() && none2.cmds.empty() && none_i.empty());
    // O texto do jogo (Windows-1252) com os de 0x80 a 0x9f.
    CHECK(nf::ui::wheel_game_text("Bond\x92s \x85 007\x99") == std::wstring(L"Bond’s … 007™"));
}

namespace {

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void block(std::vector<uint8_t>& item, uint8_t id, const std::vector<uint8_t>& payload) {
    put32(item, uint32_t(id) << 24 | uint32_t(payload.size() + 4));
    item.insert(item.end(), payload.begin(), payload.end());
}

}  // namespace

TEST(pack_images_decode_textures_by_their_hash) {
    // Um pacote feito à mão: um item de mapa com duas texturas de 4 x 2 em 4 bits, os hashes no cabeçalho das paletas
    // (bloco 0x0F), na ordem dos cabeçalhos das texturas (0x10) e dos blocos de pixels (0x16) e de paleta (0x12).
    const uint32_t ha = 0x3000aaa, hb = 0x3000bbb;
    std::vector<uint8_t> item;
    put32(item, 0);  // versão 0: blocos em sequência
    std::vector<uint8_t> pal_header, tex_header;
    for (const uint32_t h : {ha, hb}) {
        put32(pal_header, 15);
        put32(pal_header, h);
        tex_header.push_back(0);  // 4 bpp
        tex_header.push_back(0);
        put16(tex_header, 3);  // largura - 1
        put16(tex_header, 1);  // altura - 1
        put16(tex_header, 1);  // um quadro
        put32(tex_header, 0xffffffffu);
    }
    block(item, 0x0f, pal_header);
    block(item, 0x10, tex_header);
    for (int t = 0; t < 2; ++t) {
        // Os pixels: índices 0..7 (dois por byte, o de baixo primeiro); a paleta: a cor i = (i * 10 + t, 0, 0, 0x80).
        block(item, 0x16, {0x10, 0x32, 0x54, 0x76});
        std::vector<uint8_t> pal;
        for (int i = 0; i < 16; ++i) {
            pal.push_back(uint8_t(i * 10 + t));
            pal.push_back(0);
            pal.push_back(0);
            pal.push_back(0x80);
        }
        block(item, 0x12, pal);
    }
    block(item, 0x1d, {});
    std::vector<uint8_t> pak(0x800, 0);
    pak[1] = 0x08;  // table_size 0x800
    pak[4] = 1;     // um item
    std::vector<uint8_t> entry;
    put32(entry, uint32_t(item.size()));
    entry.push_back(1);  // o mapa principal
    for (const char c : std::string("0100009a")) entry.push_back(uint8_t(c));
    entry.push_back(0);
    entry.resize(0x800, 0);
    pak.insert(pak.end(), entry.begin(), entry.end());
    pak.insert(pak.end(), item.begin(), item.end());

    // Pedida só a segunda (e uma que não existe): vem a segunda, com as cores da paleta dela.
    const std::map<uint32_t, nf::Texture> got = nf::decode_pack_images(pak, {hb, 0x3000ccc});
    CHECK(got.size() == 1 && got.count(hb) == 1);
    if (got.count(hb) == 0) return;
    const nf::Texture& t = got.at(hb);
    CHECK(t.width == 4 && t.height == 2 && t.rgba.size() == 4u * 2u * 4u);
    for (int p = 0; p < 8; ++p) CHECK(t.rgba[size_t(p) * 4] == uint8_t(p * 10 + 1) && t.rgba[size_t(p) * 4 + 3] == 0x80);
    // As duas; nada pedido; um pacote estragado (sem exceção).
    CHECK(nf::decode_pack_images(pak, {ha, hb}).size() == 2);
    CHECK(nf::decode_pack_images(pak, {}).empty());
    std::vector<uint8_t> bad(pak.begin(), pak.begin() + 0x1000 + 20);
    CHECK(nf::decode_pack_images(bad, {ha}).empty());
}
