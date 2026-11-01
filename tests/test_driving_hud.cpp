// Os filmes e o HUD das missões de carro (docs/driving.md seção 28): propriedades independentes sobre dados montados à
// mão (sem dados do jogo): o MPC lido pedaço a pedaço, o texto da fonte formatado sem argumentos, o GS do 2D com um
// SPRITE, o .sub das legendas, a quebra das legendas em pedaços e o botão da mensagem do HUD.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "driving/game/hud_draw.h"
#include "driving/render/ea_font.h"
#include "driving/render/gs2d.h"
#include "driving/render/play_mpc.h"
#include "driving/render/subtitles.h"
#include "test_framework.h"

using namespace drv;

namespace {

void put_chunk(std::vector<uint8_t>& b, const char tag[4], const std::vector<uint8_t>& body) {
    const uint32_t size = uint32_t(body.size() + 8);
    b.insert(b.end(), tag, tag + 4);
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(size >> (8 * i)));
    b.insert(b.end(), body.begin(), body.end());
}

}  // namespace

TEST(driving_mpc_parse_reads_the_video_units_and_the_sequence_header) {
    // Um cabeçalho de sequência do MPEG-2 de 640 x 448 a 29,97 quadros por segundo (frame_rate_code 4).
    const std::vector<uint8_t> seq = {0x00, 0x00, 0x01, 0xb3, 0x28, 0x01, 0xc0, 0x24,
                                      0xff, 0xff, 0xe0, 0x18, 0x00, 0x00, 0x01, 0x00};
    const std::vector<uint8_t> pic = {0x00, 0x00, 0x01, 0x00, 0x00, 0x0f, 0xff, 0xf8};
    std::vector<uint8_t> f;
    put_chunk(f, "SCHl", {1, 2, 3, 4});
    put_chunk(f, "MPCh", seq);
    put_chunk(f, "SCDl", {5, 6});
    put_chunk(f, "MPCh", pic);
    put_chunk(f, "SCEl", {});
    MpcFile m;
    CHECK(m.parse(f));
    CHECK(m.width == 640 && m.height == 448);
    CHECK_NEAR(m.frame_rate, 29.97f, 0.001f);
    CHECK(m.video.size() == 2);
    CHECK(m.audio_chunks == 3);
    // Os pedaços de vídeo sem o cabeçalho de 8 bytes do MPC.
    CHECK(m.video[0].size == seq.size() && std::memcmp(&m.data[m.video[0].offset], seq.data(), seq.size()) == 0);
    CHECK(m.video[1].size == pic.size() && std::memcmp(&m.data[m.video[1].offset], pic.data(), pic.size()) == 0);
    // Sem vídeo, ou com um tamanho que passa do fim, não há filme.
    std::vector<uint8_t> audio_only;
    put_chunk(audio_only, "SCHl", {1, 2});
    CHECK(!MpcFile().parse(audio_only));
    std::vector<uint8_t> cut = f;
    cut.resize(20);
    CHECK(!MpcFile().parse(cut));
}

TEST(driving_font_text_is_formatted_like_vsprintf_without_arguments) {
    int32_t guards = 0;
    // O ' ' depois do '%' é um flag e o 'H' não é conversão: sai a letra.
    CHECK(FONT_format_noargs("50% HEALTH", &guards) == "50HEALTH");
    CHECK(FONT_format_noargs("100%% DONE", &guards) == "100% DONE");
    CHECK(FONT_format_noargs("Plain text", &guards) == "Plain text");
    CHECK(FONT_format_noargs("ends with %", &guards) == "ends with ");
    CHECK(guards == 0);
    // Uma conversão de verdade leria um argumento que não há: nada sai e conta.
    CHECK(FONT_format_noargs("%d left", &guards) == " left");
    CHECK(guards == 1);
}

TEST(driving_gs2d_sprite_becomes_two_triangles_in_screen_pixels) {
    Gs2dState s;
    s.offset_x = 2048 * 16;
    s.offset_y = 2048 * 16;
    std::vector<Gs2dBatch> out;
    auto xyz = [&](uint32_t x, uint32_t y) { return uint64_t((x + 2048) * 16) | uint64_t((y + 2048) * 16) << 16; };
    gs2d_write(0x00, 0x46, s, out);                      // SPRITE com ABE
    gs2d_write(0x01, 0x3f80000040102030ull, s, out);     // a cor do primeiro vértice
    gs2d_write(0x05, xyz(10, 20), s, out);
    CHECK(out.empty());
    gs2d_write(0x01, 0x3f80000080605040ull, s, out);     // a do segundo: a do SPRITE
    gs2d_write(0x05, xyz(110, 70), s, out);
    CHECK(out.size() == 1);
    const Gs2dBatch& b = out[0];
    CHECK(b.blend && !b.textured);
    CHECK(b.verts.size() == 6);
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (const Gs2dVertex& v : b.verts) {
        minx = std::min(minx, v.x);
        maxx = std::max(maxx, v.x);
        miny = std::min(miny, v.y);
        maxy = std::max(maxy, v.y);
        CHECK(v.rgba == 0x80605040u);
    }
    CHECK(minx == 10.0f && maxx == 110.0f && miny == 20.0f && maxy == 70.0f);
    // A área dos dois triângulos é a do retângulo.
    float area = 0;
    for (int t = 0; t < 2; ++t) {
        const Gs2dVertex* v = &b.verts[size_t(t) * 3];
        area += std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y)) * 0.5f;
    }
    CHECK_NEAR(area, 100.0f * 50.0f, 1e-3f);
    // O XYZ3 enfileira sem desenhar.
    gs2d_write(0x05, xyz(0, 0), s, out);
    gs2d_write(0x0d, xyz(5, 5), s, out);
    CHECK(out[0].verts.size() == 6);
}

TEST(driving_subtitles_file_gives_frame_label_pairs) {
    GSubtitlesData s{};  // zerado como o do jogo (o P32 no x86 é um ponteiro sem valor inicial)
    GSubtitles_LoadSubtitlesText(s, "2\r\nFRAME_10=4100\r\nFRAME_95=0\r\n");
    CHECK(s.count == 3);
    CHECK(s.index == 0 && s.label == 0);
    const int32_t* f = nf::raw(s.frames);
    CHECK(f != nullptr);
    CHECK(f[0] == 0 && f[1] == 0);
    CHECK(f[2] == 10 && f[3] == 4100);
    CHECK(f[4] == 95 && f[5] == 0);
    GSubtitles_ClearFrames(s);
}

TEST(driving_subtitle_splitter_cuts_at_spaces_within_the_limit) {
    GSubtitleSplitterData p{};
    GSubtitleSplitter_Construct(p);
    p.duration = 6.0f;
    const char* text = "The quick brown fox jumps over the lazy dog and keeps running far away";
    const uint32_t max = 20;
    const int32_t n = GSubtitleSplitter_WordWrap(p, text, max, p);
    p.count = uint32_t(n);
    CHECK(n >= 4);
    std::string joined;
    float total = 0;
    for (int32_t i = 0; i < n; ++i) {
        const char* line = nf::raw(p.lines[i]);
        CHECK(line != nullptr);
        CHECK(std::strlen(line) <= max);
        CHECK(line[0] != ' ');
        if (!joined.empty()) joined += ' ';
        joined += line;
        total += p.durations[i];
        // O tempo de cada pedaço é proporcional ao tamanho dele.
        CHECK_NEAR(p.durations[i], 6.0f * float(std::strlen(line)) / float(std::strlen(text)), 1e-4f);
    }
    // Os pedaços, juntos com um espaço, dão o texto (os espaços do corte saem); o tempo soma menos que o total (os
    // espaços cortados não têm tempo).
    CHECK(joined == text);
    CHECK(total <= 6.0f);
    GSubtitleSplitter_Reset(&p);
    CHECK(p.count == 0);
}

TEST(driving_hud_button_message_splits_around_the_button_symbol) {
    char before[0x100] = {}, after[0x100] = {};
    CHECK(GHud_GetButton("Press *P* to fire the missiles", before, after) == 2);
    CHECK(std::string(before) == "Press ");
    CHECK(std::string(after) == " to fire the missiles");
    char b2[0x100] = {}, a2[0x100] = {};
    CHECK(GHud_GetButton("*A* jumps", b2, a2) == 0);
    CHECK(b2[0] == 0 && std::string(a2) == " jumps");
    char b3[0x100] = {}, a3[0x100] = {};
    CHECK(GHud_GetButton("No button here", b3, a3) == -1);
    CHECK(b3[0] == 0 && a3[0] == 0);
}
