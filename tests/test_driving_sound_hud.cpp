// A pausa do HUD e os sons de Paris (docs/driving.md seção 31): propriedades independentes sobre dados montados à mão
// (sem dados do jogo): o WordWrap, as sequências das trapaças e a conferência do fim do registro, a língua do UberBond, o
// eixo invertido, os textos dos botões de cada configuração, a troca de uma imagem do 2D, o sono dos objetos derrubados
// e o ponto da moça do fundo.

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "driving/game/hud.h"
#include "driving/game/hud_pause.h"
#include "driving/game/input_config.h"
#include "driving/game/mission.h"
#include "driving/game/smackable.h"
#include "driving/render/gs2d.h"
#include "test_framework.h"

using namespace drv;

namespace {

// As palavras de um texto (separadas por espaços), para comparar o que o WordWrap quebrou com o original.
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

void put_word(GHudData& h, uint32_t at, int32_t v) { std::memcpy(reinterpret_cast<uint8_t*>(&h) + at, &v, 4); }

}  // namespace

TEST(driving_word_wrap_keeps_every_word_within_the_width) {
    const std::string text = "Destroy the van before it reaches the bridge and keep Dominique alive";
    std::vector<std::string> lines;
    const int32_t n = WordWrap(text.c_str(), 20, lines);
    CHECK(n >= 4 && n <= 5);
    CHECK(lines.size() == 5);
    std::vector<std::string> got;
    for (int32_t i = 0; i < n; ++i) {
        CHECK(lines[size_t(i)].size() <= 20);
        CHECK(lines[size_t(i)].empty() || lines[size_t(i)].front() != ' ');  // os espaços do corte são pulados
        for (const std::string& w : words(lines[size_t(i)])) got.push_back(w);
    }
    CHECK(got == words(text));
    // Um texto curto é uma linha só, inteira.
    CHECK(WordWrap("Paused", 20, lines) == 1 && lines[0] == "Paused");
    // Uma palavra maior que a largura é cortada nela.
    CHECK(WordWrap("ABCDEFGHIJKLMNOP xyz", 10, lines) == 2 && lines[0] == "ABCDEFGHIJ" && lines[1] == "KLMNOP xyz");
    // No máximo cinco linhas: o resto da quinta é cortado na largura.
    const std::string many = "aa bb cc dd ee ff gg hh ii jj kk ll";
    CHECK(WordWrap(many.c_str(), 5, lines) == 5);
    for (const std::string& l : lines) CHECK(l.size() <= 5);
}

TEST(driving_cheat_sequences_match_only_at_the_end_of_the_button_record) {
    int32_t count = 0;
    const CheatSequence* seqs = hud_cheat_sequences(&count);
    CHECK(count == 9);
    std::set<std::vector<int32_t>> distinct;
    for (int32_t i = 0; i < count; ++i) {
        const int32_t* s = seqs[i].words;
        const int32_t n = s[0];
        CHECK(n >= 2 && n <= 16);  // o registro tem 16 palavras (+0x4ac..+0x4e8)
        CHECK(s[1] == 0x61 && s[n] == 0x62);  // L1 e R1 abrem e fecham (as ações 0x61/0x62)
        // No meio, os botões aos pares (apertar, soltar): 0x59/0x5a, 0x5b/0x5c, 0x5d/0x5e.
        for (int32_t k = 2; k < n; k += 2) {
            CHECK(s[k] == 0x59 || s[k] == 0x5b || s[k] == 0x5d);
            CHECK(s[k + 1] == s[k] + 1);
        }
        distinct.insert(std::vector<int32_t>(s + 1, s + 1 + n));
    }
    CHECK(int32_t(distinct.size()) == count);
    // Cada sequência posta no fim do registro confere; uma palavra trocada, ou a sequência no começo, não.
    for (int32_t i = 0; i < count; ++i) {
        const int32_t* s = seqs[i].words;
        const int32_t n = s[0];
        GHudData h{};
        for (int32_t k = 0; k < 16; ++k) put_word(h, 0x4ac + uint32_t(k) * 4, 0x41);
        for (int32_t k = 0; k < n; ++k) put_word(h, 0x4ac + uint32_t(16 - n + k) * 4, s[1 + k]);
        CHECK(GHud_CheckCheatInput(h, s));
        put_word(h, 0x4ac + uint32_t(16 - n) * 4, 0x62);
        CHECK(!GHud_CheckCheatInput(h, s));
        if (n < 16) {
            GHudData g{};
            for (int32_t k = 0; k < n; ++k) put_word(g, 0x4ac + uint32_t(k) * 4, s[1 + k]);
            CHECK(!GHud_CheckCheatInput(g, s));
        }
    }
}

TEST(driving_locale_language_and_inverted_axis_come_from_the_uber_block) {
    UberBondBlock& u = uber_bond();
    const UberBondBlock saved = u;
    for (int32_t v = 0; v <= 5; ++v) {
        u.word(0x988) = v;
        CHECK(GLocale_GetLanguage() == v);
    }
    u.word(0x988) = 8;
    CHECK(GLocale_GetLanguage() == 6);
    u.word(0x988) = 6;
    CHECK(GLocale_GetLanguage() == 7);
    u.word(0x988) = -1;
    CHECK(GLocale_GetLanguage() == 7);
    InputConfigManager m;
    InputConfigManager_SetInverted(m, true);
    CHECK(InputConfigManager_IsInverted(m) && u.word(0xa24) == 1);
    InputConfigManager_SetInverted(m, false);
    CHECK(!InputConfigManager_IsInverted(m) && u.word(0xa24) == 0);
    u = saved;
}

TEST(driving_button_text_ids_follow_the_track_and_the_config) {
    UberBondBlock& u = uber_bond();
    const UberBondBlock saved = u;
    InputConfigManager m;
    for (int t = 0; t < 2; ++t) {
        m.configs[t].resize(8);
        for (InputConfigInfo& c : m.configs[t]) {
            const int32_t ids[14] = {0xa98, 0xa99, 0x590, 0xa94, 0xa9f, 0xaa3, 0x123, 0, 0, 0, 0, 0, 0, 0x777};
            std::memcpy(c.locale, ids, sizeof ids);
        }
    }
    auto set_track = [&](const char* t) {
        std::memset(u.text(0x4f4), 0, 0x20);
        std::strcpy(u.text(0x4f4), t);
    };
    set_track("paris_mis01");
    // Fora do alcance: 0.
    CHECK(InputConfigManager_GetLocaleID(m, 0, 8, 0) == 0 && InputConfigManager_GetLocaleID(m, 0, 0, 14) == 0);
    // Os textos comuns passam como estão; 0xa9f é sempre "Not Used" (0xaa1).
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 6) == 0x123 && InputConfigManager_GetLocaleID(m, 1, 3, 13) == 0x777);
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 4) == 0xaa1 && InputConfigManager_GetLocaleID(m, 1, 0, 4) == 0xaa1);
    // Paris: as duas armas do carro somem só na configuração do POV (tipo 1).
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 0) == 0xa98 && InputConfigManager_GetLocaleID(m, 1, 0, 0) == 0xaa1);
    CHECK(InputConfigManager_GetLocaleID(m, 1, 0, 1) == 0xaa1);
    // 0xaa3 em Paris no POV: 0x6ad nas configurações 6 e 7, "Not Used" nas outras.
    CHECK(InputConfigManager_GetLocaleID(m, 1, 6, 5) == 0x6ad && InputConfigManager_GetLocaleID(m, 1, 7, 5) == 0x6ad);
    CHECK(InputConfigManager_GetLocaleID(m, 1, 5, 5) == 0xaa1 && InputConfigManager_GetLocaleID(m, 0, 6, 5) == 0xaa3);
    // 0x590 só vale na snow1a_mis3; 0xa94 some nas pistas submarinas.
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 2) == 0xaa1 && InputConfigManager_GetLocaleID(m, 0, 0, 3) == 0xa94);
    set_track("snow1a_mis3");
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 2) == 0x590 && InputConfigManager_GetLocaleID(m, 1, 0, 0) == 0xa98);
    CHECK(InputConfigManager_GetLocaleID(m, 1, 6, 5) == 0xaa3);
    set_track("uw_mis11");
    CHECK(InputConfigManager_GetLocaleID(m, 0, 0, 3) == 0xaa1);
    u = saved;
}

TEST(driving_ui_image_update_replaces_the_pixels_and_marks_them_for_upload) {
    ui_images_clear();
    UiImage a;
    a.width = 2;
    a.height = 1;
    a.rgba = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint32_t id = ui_image_add(a);
    CHECK(id == 1 && ui_image_take_dirty().empty());  // uma imagem nova sobe pelo número, não pela lista das trocadas
    UiImage b = a;
    b.rgba[0] = 99;
    CHECK(ui_image_update(id, b));
    CHECK(ui_image_update(id, b));  // duas trocas no mesmo quadro: uma subida só
    CHECK(!ui_image_update(id + 1, b) && !ui_image_update(0, b));
    const std::vector<uint32_t> dirty = ui_image_take_dirty();
    CHECK(dirty.size() == 1 && dirty[0] == id);
    CHECK(ui_image_get(id)->rgba[0] == 99 && ui_image_count() == 1);
    CHECK(ui_image_take_dirty().empty());
    ui_images_clear();
}

TEST(driving_smackable_sleeps_only_after_its_delay_when_nothing_holds_it) {
    SmackableData d{};
    d.sleep_delay = 3;
    // Acordado pela física (estado 2), pela animação ou pelo som: o atraso não corre.
    CHECK(!Smackable_SleepStep(d, 2, false, false) && d.sleep_delay == 3);
    CHECK(!Smackable_SleepStep(d, 1, true, false) && d.sleep_delay == 3);
    CHECK(!Smackable_SleepStep(d, 1, false, true) && d.sleep_delay == 3);
    // Parado: três passos de espera, e no quarto dorme.
    int steps = 0;
    while (!Smackable_SleepStep(d, 1, false, false)) ++steps;
    CHECK(steps == 3 && d.sleep_delay == 0);
    CHECK(Smackable_SleepStep(d, 1, false, false));
}

TEST(driving_girl_points_land_in_the_current_buffer_only) {
    GGirlState& g = ggirl();
    const GGirlState saved = g;
    for (auto& p : g.pixels) p.assign(size_t(128) * 128, 0u);
    g.buffer = 1;
    GGirl_DrawPoint(5, 7, 0x40112233u);
    GGirl_DrawPoint(127, 127, 0x7f000001u);
    GGirl_DrawPoint(-1, 0, 0xffffffffu);     // fora: nada
    GGirl_DrawPoint(0, 128, 0xffffffffu);    // fora: nada
    CHECK(g.pixels[1][size_t(5 + 7 * 128)] == 0x40112233u);
    CHECK(g.pixels[1][size_t(127 + 127 * 128)] == 0x7f000001u);
    size_t set = 0;
    for (uint32_t p : g.pixels[1]) set += p != 0;
    CHECK(set == 2);
    for (uint32_t p : g.pixels[0]) CHECK(p == 0u);
    g = saved;
}
