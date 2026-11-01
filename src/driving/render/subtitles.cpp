// As legendas das missões de carro (driving/render/subtitles.h; docs/driving.md seção 28).
#include "driving/render/subtitles.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "driving/game/hud.h"
#include "driving/physics/simulation.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

GSubtitlesData* g_app = nullptr;
GSubtitlesData g_storage;
EaFont g_font;
SubtitleFiles g_files;
SubtitleStats g_stats;

// O (float)(unsigned) do GCC do EE: o cvt.s.w direto até 2^31, senão a metade arredondada vezes 2.
float u2f(uint32_t v) {
    if (int32_t(v) >= 0) return ee::cvt_s(int32_t(v));
    const float h = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
    return ee::add(h, h);
}

// O strtok da newlib do PS2 (o estado num global; o do MSVC é o mesmo algoritmo, mas avisado como inseguro).
char* g_strtok_last = nullptr;
char* ps2_strtok(char* s, const char* delim) {
    if (s == nullptr) s = g_strtok_last;
    if (s == nullptr) return nullptr;
    while (*s != 0 && std::strchr(delim, *s) != nullptr) ++s;
    if (*s == 0) {
        g_strtok_last = nullptr;
        return nullptr;
    }
    char* tok = s;
    while (*s != 0 && std::strchr(delim, *s) == nullptr) ++s;
    if (*s != 0) {
        *s = 0;
        g_strtok_last = s + 1;
    } else {
        g_strtok_last = nullptr;
    }
    return tok;
}

int32_t game_steps() { return simulation().steps; }
float game_dt() { return simulation().dt; }

}  // namespace

SubtitleStats& subtitle_stats() { return g_stats; }
SubtitleFiles& subtitle_files() { return g_files; }
GSubtitlesData* GSubtitles_TheApp() { return g_app; }
GSubtitlesData& gsubtitles_storage() { return g_storage; }
EaFont& gsubtitles_font() { return g_font; }

// ---------------------------------------------------------------- o divisor de texto

void GSubtitleSplitter_Construct(GSubtitleSplitterData& p) {
    p.count = 0;
    p.duration = 0.0f;
    p.start = 0;
}

void GSubtitleSplitter_Reset(GSubtitleSplitterData* p) {
    if (p != nullptr && p->count != 0) {
        for (uint32_t i = 0; i < p->count; ++i)
            if (p->lines[i]) delete[] nf::raw(p->lines[i]);  // FUN_002d0a50 (o operator delete[])
    }
    p->count = 0;
    p->start = 0;
    p->duration = 0.0f;
}

int32_t GSubtitleSplitter_WordWrap(GSubtitleSplitterData& p, const char* text, uint32_t max, GSubtitleSplitterData& out) {
    for (int i = 9; i >= 0; --i) out.lines[i] = nullptr;
    const float total = ee::cvt_s(int32_t(std::strlen(text)));
    int32_t n = 0;
    auto piece = [&](int32_t at, int32_t len) {
        char* s = new char[size_t(len) + 1];
        s[len] = 0;
        std::memcpy(s, text, size_t(len));
        out.lines[at] = s;
        out.durations[at] = ee::div(ee::mul(p.duration, ee::cvt_s(len)), total);
    };
    while (std::strlen(text) > max && n <= 8) {
        const int32_t m = int32_t(max);
        int32_t cut = m - 1;
        if (cut >= 0 && text[m - 1] != ' ') {
            cut = m - 2;
            while (cut >= 0 && text[cut] != ' ') --cut;
        }
        int32_t adv;
        if (cut == -1) {
            piece(n, m);
            adv = m;
        } else {
            piece(n, cut);
            adv = cut;
        }
        text += adv;
        while (*text == ' ') ++text;
        ++n;
    }
    uint32_t rest = uint32_t(std::strlen(text));
    if (rest > max) rest = max;
    piece(n, int32_t(rest));
    return n + 1;
}

void GSubtitleSplitter_Init(float seconds, GSubtitleSplitterData& p, const char* text, int32_t game_clock) {
    p.current = 0;
    p.duration = seconds;
    p.count = uint32_t(GSubtitleSplitter_WordWrap(p, text, 0x34, p));
    p.game_clock = game_clock;
    p.start = game_clock == 0 ? TIMER_gettick() : game_steps();
}

const char* GSubtitleSplitter_GetString(GSubtitleSplitterData& p) {
    if (ee::eq(p.duration, 0.0f)) return nullptr;
    float t;
    if (p.game_clock == 0)
        t = ee::div(ee::cvt_s(TIMER_gettick() - p.start), ee::cvt_s(100));  // TIMER_getfrequency
    else
        t = ee::mul(ee::cvt_s(game_steps() - p.start), game_dt());
    uint32_t i = p.current;
    if (i < 10 && ee::le(p.durations[i], t)) {
        if (i < 9) p.current = i + 1;
        p.start = p.game_clock == 0 ? TIMER_gettick() : game_steps();
        i = p.current;
    }
    return i < 10 ? nf::raw(p.lines[i]) : nullptr;
}

// ---------------------------------------------------------------- GSubtitles

void GSubtitles_Construct(GSubtitlesData& s) {
    s.vtable = 0x3ac0f8;
    GSubtitleSplitter_Construct(s.splitter);
    s.font = 0;
    s.file = 0;
    s.label = 0;
    s.start = 0;
    s.duration = 0.0f;
    s.width = 640.0f;   // (float) RRenderer +0x50
    s.frames = nullptr;
    s.count = 0;
    s.index = 0;
    s.height = 448.0f;  // (float) RRenderer +0x54
    GSubtitles_LoadFont(s, "data\\loading\\loading.sfn");
    g_app = &s;
}

void GSubtitles_UnloadFont(GSubtitlesData& s) {
    // FUN_00257f50 (FONT_destroy, o driver apaga a textura) e o UFileLoader::FileFree; os campos ficam.
    if (s.font != 0) g_font = EaFont();
}

void GSubtitles_Destruct(GSubtitlesData& s) {
    s.vtable = 0x3ac0f8;
    GSubtitles_UnloadFont(s);
    GSubtitles_UnloadSubtitles(s);
    GSubtitles_ClearFrames(s);
    g_app = nullptr;
}

void GSubtitles_LoadFont(GSubtitlesData& s, const char* name) {
    if (s.font != 0) g_font = EaFont();
    std::vector<uint8_t> file;
    const bool ok = g_files.load && g_files.load(name, file);
    s.file = ok ? 1u : 0u;
    // FONT_create: o PS2 não confere o arquivo (um nulo vira a fonte nula); o port só cria com um FNTS.
    const uint32_t image = ok && g_files.image ? g_files.image(file) : 0u;
    s.font = ok && FONT_create(g_font, std::move(file), image) ? 1u : 0u;
}

void GSubtitles_UnloadSubtitles(GSubtitlesData& s) {
    if (s.frames) delete[] nf::raw(s.frames);
    s.frames = nullptr;
}

void GSubtitles_ClearFrames(GSubtitlesData& s) {
    if (s.frames) delete[] nf::raw(s.frames);
    s.frames = nullptr;
    s.index = 0;
    GSubtitleSplitter_Reset(&s.splitter);
}

void GSubtitles_ClearSubtitles(GSubtitlesData& s) {
    s.start = 0;
    s.duration = 0.0f;
    s.label = 0;
    GSubtitleSplitter_Reset(&s.splitter);
    GSubtitles_ClearFrames(s);
}

bool GSubtitles_IsSubtitleDisplaying(const GSubtitlesData& s) { return s.label != 0; }

void GSubtitles_LoadSubtitles(GSubtitlesData& s, const char* name) {
    if (name == nullptr) return;
    std::vector<uint8_t> file;
    if (!g_files.load || !g_files.load(name, file)) return;  // "ERROR - Could not load subtitle file"
    GSubtitles_LoadSubtitlesText(s, std::string(file.begin(), file.end()));
}

void GSubtitles_LoadSubtitlesText(GSubtitlesData& s, std::string text) {
    ++g_stats.loads;
    GSubtitles_ClearFrames(s);
    // O UFileLoader::FileLoad(nome, 0x100) dá o arquivo com um zero no fim; o strtok escreve nele.
    std::vector<char> buf(text.begin(), text.end());
    buf.push_back(0);
    buf.resize(buf.size() + 4, 0);
    char* base = buf.data();
    char* first = ps2_strtok(base, "\r\n");
    const int32_t n = first ? std::atoi(first) : 0;
    char* p = base + (first ? std::strlen(first) : 0) + 2;
    s.index = 0;
    s.count = uint32_t(n + 1);
    auto* frames = new int32_t[size_t(n + 1) * 2];
    frames[1] = 0;
    s.frames = frames;
    frames[0] = 0;
    s.label = 0;
    s.start = 0;
    s.duration = 0.0f;
    for (uint32_t i = 1; i < s.count; ++i) {
        if (p >= base + buf.size() - 4) p = base + buf.size() - 4;  // o fim (o PS2 lê o que vier depois do arquivo)
        char* line = ps2_strtok(p, "\r\n");
        if (line == nullptr) {
            ++g_stats.guards;
            frames[i * 2] = 0;
            frames[i * 2 + 1] = 0;
            continue;
        }
        p += std::strlen(line) + 2;
        char* key = ps2_strtok(line, "=");
        const int32_t frame = std::atoi(key + 6);
        char* val = ps2_strtok(nullptr, "=");
        const int32_t label = val ? std::atoi(val) : 0;
        frames[i * 2] = frame;  // GetFoundationVideoMode 1 (NTSC): sem o x 5 / 6
        frames[i * 2 + 1] = label;
    }
}

void GSubtitles_DisplaySubtitle(GSubtitlesData& s, int32_t label, float seconds, bool skipping) {
    if (skipping) {
        GSubtitles_ClearSubtitles(s);
        return;
    }
    s.label = label;
    s.duration = seconds;
    s.start = game_steps();
    GSubtitleSplitter_Reset(&s.splitter);
    s.game_clock = 1;
    GSubtitleSplitter_Init(seconds, s.splitter, LOCALE_Symbol(s.label), s.game_clock);
    ++g_stats.shown;
}

void GSubtitles_SetupFMVSubtitles(GSubtitlesData& s, uint32_t frame) {
    const uint32_t count = s.count;
    if (count == 0) return;
    const uint32_t idx = s.index;
    if (idx >= count) return;
    s.game_clock = 0;
    const int32_t* frames = nf::raw(s.frames);
    if (frames == nullptr) {
        ++g_stats.guards;  // a tabela liberada (o ClearFrames não zera a contagem): o PS2 leria o endereço 0
        return;
    }
    if (idx < count - 1 && uint32_t(frames[idx * 2]) <= frame) {
        GSubtitleSplitter_Reset(&s.splitter);
        s.label = frames[idx * 2 + 1];
        const float secs = ee::div(u2f(uint32_t(frames[idx * 2 + 2] - frames[idx * 2])), 30.0f);
        GSubtitleSplitter_Init(secs, s.splitter, LOCALE_Symbol(s.label), s.game_clock);
        s.duration = secs;
        s.index = idx + 1;
        ++g_stats.shown;
    }
    s.start = TIMER_gettick();
}

namespace {
// [driving] 0x0024a980 GSubtitles::GetWidthHeight
void get_width_height(float scale, GSubtitlesData& s, const char* text, float* w, float* h) {
    (void)s;
    uint8_t* f = g_font.ptr();
    std::memcpy(f + 0x38, &scale, 4);
    std::memcpy(f + 0x3c, &scale, 4);
    FONT_getrecta(f, text, nullptr, nullptr, w, h);
}
// [driving] 0x0024a8b0 GSubtitles::DrawFont (o GeoPrimState do Draw sem o bit 0 de +0x30, o teste de Z 1 nos dois, o
// Z 0: o estado do EAGL que o driver de fontes não lê; anotado)
void draw_font(float x, float y, float scale, GSubtitlesData& s, const char* text, uint32_t colour, GsPacket& out) {
    (void)s;
    uint8_t* f = g_font.ptr();
    std::memcpy(f + 0x38, &scale, 4);
    std::memcpy(f + 0x3c, &scale, 4);
    std::memcpy(f + 0x20, &colour, 4);
    FONT_drawtextfa(x, y, g_font, text, out);  // o texto é o formato, sem argumentos
}
}  // namespace

void GSubtitles_DrawSubtitles(GSubtitlesData& s, GsPacket& out) {
    if (s.label == 0 || !ee::lt(0.0f, s.duration)) return;
    float t;
    if (s.game_clock == 0)
        t = ee::div(ee::cvt_s(TIMER_gettick() - s.start), ee::cvt_s(100));
    else
        t = ee::mul(ee::cvt_s(game_steps() - s.start), game_dt());
    if (ee::le(s.duration, t)) {
        GSubtitleSplitter_Reset(&s.splitter);
        s.label = 0;
        s.start = 0;
        s.duration = 0.0f;
        return;
    }
    const char* text = GSubtitleSplitter_GetString(s.splitter);
    if (text == nullptr || s.font == 0) return;
    float w = 0.0f, h = 0.0f;
    get_width_height(1.0f, s, text, &w, &h);
    const float y = ee::sub(s.height, 65.0f);
    // GSubtitles::DrawBackground (0x0024a978) não faz nada.
    draw_font(ee::mul(ee::sub(s.width, w), 0.5f), ee::sub(y, 2.0f), 1.0f, s, text, 0x7fffffffu, out);
    ++g_stats.drawn;
}

void DrawSubtitlesCallback(uint32_t frame, GsPacket& out) {
    if (g_app == nullptr) return;
    GSubtitles_SetupFMVSubtitles(*g_app, frame);
    GSubtitles_DrawSubtitles(*g_app, out);
}

}  // namespace drv
