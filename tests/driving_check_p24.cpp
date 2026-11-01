// nf_driving_check, parte 24 (tests/driving_check_p24.h; docs/driving.md seção 28): os filmes. O código ORIGINAL do
// DRIVING.ELF roda no interpretador MIPS lado a lado com o porte (driving/render/ea_font.h, subtitles.h, play_mpc.h):
//  - a biblioteca FONT da EA com o driver de fontes do EAGL sobre as três fontes do MISC.VIV (loading.sfn, error.sfn,
//    debug.sfn): textos sorteados (ASCII, acentos, quebras de linha, caracteres que a fonte não tem), escalas, cores, a
//    filtragem (+0x48) e o teste de Z (+0x4c), a posição e o deslocamento do UV: o FONT_getrecta (os quatro floats) e o
//    FONT_drawtexta (as palavras do pacote do GIF que o driver escreve no acumulador do DmaMgr, as configurações e cada
//    SPRITE; os 12 qwords da textura vêm do port nos dois lados, pelo EAGL::TARExtension::OutputGifTags) e o cabeçalho
//    da fonte depois;
//  - o GSubtitles e o GSubtitleSplitter: os nove .sub do MISC.VIV e .sub sorteados (o LoadSubtitles com o strtok e o
//    atoi da newlib do original), o SetupFMVSubtitles por quadros sorteados, o DrawSubtitles (o pacote do GIF das
//    legendas pelo FONT_drawtextfa com o vsprintf do original), o DisplaySubtitle (o relógio do jogo), o ClearSubtitles,
//    o DrawSubtitlesCallback, com o TIMER e o passo do jogo andando: depois de cada operação o objeto (0x94), os pedaços
//    do texto pelo conteúdo, as durações, a tabela dos quadros;
//  - o relógio do AV_PLAYER: o AV_MS_TIMER::Update, o SyncedAudioTime (com o tempo tocado do fluxo sorteado), o
//    IsTimeForDecode, o SetSpeed (a chamada ao AUDIO_PLAYER::SetSpeed), com estados sorteados;
//  - o PlayMPC::ReadInput com os botões do controle sorteados.
// Os textos do .loc vêm do IDL_ENG.loc nos dois lados (o GSystem::LOCALE_Symbol do original devolve a cadeia do porte).
//
// Tudo aqui lê dados do jogo: a saída é um relatório, nada é gravado.

#include "driving_check_p24.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/game/hud.h"
#include "driving/physics/simulation.h"
#include "driving/render/ea_font.h"
#include "driving/render/play_mpc.h"
#include "driving/render/subtitles.h"
#include "driving/sound/audio.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções do original.
constexpr uint32_t kFnDrawTextA = 0x258020, kFnGetRect = 0x2580c0;
constexpr uint32_t kFnLoadSubs = 0x24a300, kFnSetupFmv = 0x24a598, kFnDrawSubs = 0x24a6d0, kFnDisplaySub = 0x24a1f0;
constexpr uint32_t kFnClearSubs = 0x24a870, kFnSubCallback = 0x24a088;
constexpr uint32_t kFnSynced = 0x3015b0, kFnIsTime = 0x301390, kFnSetSpeed = 0x301498, kFnTimerUpdate = 0x301950;
constexpr uint32_t kFnReadInput = 0x3007b0;
// Os globais.
constexpr uint32_t kGVifAcc = 0x34861c, kGFontAcc = 0x348b94, kGSubApp = 0x33ba60;
constexpr uint32_t kGGeoState0 = 0x3335c0, kGGeoState1 = 0x3335c4, kGSteps = 0x339f54, kGDt = 0x339f50;

// A RAM emulada.
constexpr uint32_t kFontAt = 0x00400000;    // a fonte (até 64 KB)
constexpr uint32_t kTextAt = 0x00420000;    // o texto
constexpr uint32_t kRectAt = 0x00421000;    // os quatro floats do retângulo
constexpr uint32_t kTexObj = 0x00422000;    // o EAGL::Texture falso da fonte
constexpr uint32_t kVifObj = 0x00423000;    // o acumulador do DmaMgr
constexpr uint32_t kAccObj = 0x00423100;    // o 0x00348b94 (0x18)
constexpr uint32_t kCtxObj = 0x00424000;    // o RenderContext (o +0x75c e o +0x760)
constexpr uint32_t kGeoObj = 0x00425000;    // os dois GeoPrimState do Draw
constexpr uint32_t kSubObj = 0x00426000;    // o GSubtitles (0x94)
constexpr uint32_t kAvObj = 0x00427000;     // o AV_PLAYER (0x68), o AV_MS_TIMER em +0x100, o AUDIO_PLAYER em +0x200
constexpr uint32_t kPlayObj = 0x00428000;   // o PlayMPC (0x20), o controle em +0x100
constexpr uint32_t kLocStr = 0x00430000;    // as cadeias do LOCALE_Symbol (uma por chamada, em anel)
constexpr uint32_t kFileAt = 0x00440000;    // o .sub do UFileLoader::FileLoad
constexpr uint32_t kPacketAt = 0x00500000;  // o pacote do GIF
constexpr uint32_t kHeapLo = 0x00600000, kHeapHi = 0x00f00000;  // o operator new

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) / double(0x1000000) < p; }
    float uniform(float lo, float hi) { return lo + (hi - lo) * float(next() & 0xffffff) / float(0x1000000); }
};

uint32_t fbits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    int32_t timer = 0;           // o TIMER_gettick
    int32_t skip_cinematic = 0;  // o SMissionManager::GoingToSkipCinematic
    uint32_t heap = kHeapLo;
    uint32_t loc_ring = 0;
    std::vector<uint8_t> sub_file;  // o que o próximo UFileLoader::FileLoad devolve (vazio: 0)
    int32_t played_ms = 0;          // o SNDSTRM_requeststatus +4
    std::vector<uint32_t> speed_calls;
    uint16_t pad_buttons = 0;
    float fps = 29.97f;
    uint32_t image = 1;

    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        install();
        m.write32(kGVifAcc, kVifObj);
        m.write32(kGFontAcc, kAccObj);
        m.write32(kGGeoState0, kGeoObj);
        m.write32(kGGeoState1, kGeoObj + 0x100);
        m.write32(0x3c7f54, 0x348be0);  // o FONT_installdriver do RRenderer::LoadDebugFont: o driver do EAGL
        m.write32(0x3c80d0, 100);  // o TIMER_init(100): a frequência do TIMER
    }
    uint32_t call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 100'000'000);
        return uint32_t(m.gpr(R::v0));
    }
    uint32_t alloc(uint32_t n) {
        const uint32_t at = (heap + 15) & ~15u;
        heap = at + ((n + 15) & ~15u);
        if (heap >= kHeapHi) heap = kHeapLo;  // um anel: os pedaços antigos já foram comparados
        return at;
    }
    std::string cstr(uint32_t a) const {
        if (a == 0) return "(nulo)";
        return reinterpret_cast<const char*>(m.mem(a, 1));
    }

    void install() {
        auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
        m.hook(0x0010c6c8, "printf", ret(0));
        m.hook(0x00261038, "TIMER_gettick", [this](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(timer)); });
        m.hook(0x00261170, "TIMER_getfrequency", ret(100));
        m.hook(0x002d6b80, "GetFoundationVideoMode", ret(1));
        m.hook(0x00206788, "SMissionManager::GoingToSkipCinematic",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(skip_cinematic)); });
        m.hook(0x002d09c8, "operator_new", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0))));
        });
        m.hook(0x002d0a50, "operator_delete[]", ret(0));
        m.hook(0x002d09a8, "__builtin_delete", ret(0));
        m.hook(0x002cad80, "MEM_free", ret(0));
        m.hook(0x002d73c0, "UFileLoader::FileLoad", [this](MipsMachine& mm) {
            if (sub_file.empty()) {
                mm.set_gpr(R::v0, 0);
                return;
            }
            const uint32_t at = kFileAt;
            std::memcpy(mm.mem(at, uint32_t(sub_file.size())), sub_file.data(), sub_file.size());
            // O FileLoad(nome, 0x100) dá o arquivo com zeros no fim (o port: quatro).
            std::memset(mm.mem(at + uint32_t(sub_file.size()), 4), 0, 4);
            mm.set_gpr(R::v0, at);
        });
        m.hook(0x0024b1a0, "GSystem::LOCALE_Symbol", [this](MipsMachine& mm) {
            const char* s = drv::LOCALE_Symbol(int32_t(mm.gpr(R::a0)));
            const uint32_t n = uint32_t(std::strlen(s)) + 1;
            if (loc_ring + n > 0x10000) loc_ring = 0;
            const uint32_t at = kLocStr + loc_ring;
            std::memcpy(mm.mem(at, n), s, n);
            loc_ring += (n + 15) & ~15u;
            mm.set_gpr(R::v0, at);
        });
        // O driver de fontes do EAGL: a textura (FUN_0027e388) não faz nada; o OutputGifTags dá os 12 qwords do port.
        m.hook(0x0027e388, "TAR::Set", ret(0));
        m.hook(0x0027d340, "EAGL::TARExtension::OutputGifTags", [this](MipsMachine& mm) {
            const uint32_t tex = uint32_t(mm.gpr(R::a0)) - 4;
            const bool linear = (mm.read32(tex + 0x10) & 0x30u) == 0x10u;
            uint64_t regs[12];
            drv::ea_font_texture_regs(image | (linear ? 0x80000000u : 0u), regs);
            // A saída: uma tag do GIF (16 bytes) e os 12 qwords (o driver copia de sp + 0x10).
            std::memset(mm.mem(uint32_t(mm.gpr(R::a1)), 16), 0, 16);
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)) + 0x10, 96), regs, 96);
        });
        m.hook(0x00287788, "EAGL::Device::Get", ret(0x00424800));
        m.hook(0x00287728, "EAGL::Device::GetCurrentRenderContext", ret(kCtxObj));
        m.hook(0x00284038, "EAGL::GeoPrimState::SetDepthTestMethod", ret(0));
        m.hook(0x0019f8b0, "Draw::SetZDepth", ret(0));
        // O relógio do AV_PLAYER.
        m.hook(0x00307bb0, "SNDSYS_entercritical", ret(0));
        m.hook(0x00307be0, "SNDSYS_leavecritical", ret(0));
        m.hook(0x00307f70, "SNDSYS latência", ret(0));
        m.hook(0x00309a50, "SNDSTRM_status", [](MipsMachine& mm) {
            const uint32_t st = uint32_t(mm.gpr(R::a1));
            mm.write32(st + 0, 1);
            mm.write32(st + 4, 0x100);
            mm.write32(st + 8, 0);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x00309b48, "SNDSTRM_requeststatus", [this](MipsMachine& mm) {
            const uint32_t st = uint32_t(mm.gpr(R::a1));
            mm.write32(st + 0, 2);
            mm.write32(st + 4, uint32_t(played_ms));
            mm.write32(st + 8, 0);
            mm.write32(st + 12, 0);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x00301b20, "RCMP::AUDIO_PLAYER::SetSpeed", [this](MipsMachine& mm) {
            speed_calls.push_back(uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x003029b0, "RCMP::DECODER::GetFrameRate", [this](MipsMachine& mm) { mm.set_fpr(0, fps); });
        // O controle.
        m.hook(0x0025a4a8, "PAD_update", ret(0));
        m.hook(0x00259b78, "PAD_getdataptr", [this](MipsMachine& mm) {
            const uint32_t at = kPlayObj + 0x100;
            uint8_t* p = mm.mem(at, 0x10);
            std::memset(p, 0, 0x10);
            std::memcpy(p + 0xc, &pad_buttons, 2);
            mm.set_gpr(R::v0, at);
        });
    }
};

uint64_t g_div_zero = 0;

std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)v);
    return b;
}

// As palavras do pacote do original (o acumulador andou: o tamanho pelo ponteiro do DmaMgr).
std::vector<uint64_t> emu_packet(Emu& e) {
    const uint32_t end = e.m.read32(kVifObj);
    std::vector<uint64_t> w((end - kPacketAt) / 8);
    if (!w.empty()) std::memcpy(w.data(), e.m.mem(kPacketAt, uint32_t(w.size() * 8)), w.size() * 8);
    return w;
}
void emu_packet_begin(Emu& e) { e.m.write32(kVifObj, kPacketAt); }

bool same_packet(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b, std::string& why) {
    if (a.size() != b.size()) {
        why = "tamanho " + std::to_string(a.size()) + " x " + std::to_string(b.size());
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) {
            why = "palavra " + std::to_string(i) + ": " + hex(a[i]) + " x " + hex(b[i]);
            return false;
        }
    return true;
}

// Um texto sorteado para a fonte.
std::string random_text(Rng& r, int maxlen) {
    std::string s;
    const int n = r.range(0, maxlen);
    for (int i = 0; i < n; ++i) {
        const int k = r.range(0, 99);
        char c;
        if (k < 70) c = char(r.range(0x20, 0x7e));
        else if (k < 80) c = ' ';
        else if (k < 85) c = '\n';
        else if (k < 95) c = char(r.range(0xc0, 0xff));
        else c = char(r.range(0x01, 0x1f));
        if (c == '\n' && !s.empty() && s.back() == '\n') c = 'x';
        s.push_back(c);
    }
    return s;
}

struct FontCase {
    std::string name;
    std::vector<uint8_t> file;
};

int check_fonts(Emu& e, const std::vector<FontCase>& fonts, int cases, uint32_t seed) {
    Rng r(seed ^ 0x1234u);
    uint64_t n = 0, bad = 0, words = 0, glyphs = 0;
    std::string first;
    for (int c = 0; c < cases; ++c) {
        const FontCase& fc = fonts[size_t(c) % fonts.size()];
        drv::EaFont pf;
        drv::FONT_create(pf, fc.file, e.image);
        // O mesmo cabeçalho nos dois lados (o que o jogo escreve antes de desenhar).
        const float scale_x = r.chance(0.5) ? 1.0f : r.uniform(0.4f, 2.5f);
        const float scale_y = r.chance(0.7) ? scale_x : r.uniform(0.4f, 2.5f);
        const uint32_t colour = r.next();
        const float z = float(r.range(0, 0xffffff));
        const int32_t filt = r.range(0, 1), ztest = r.chance(0.5) ? 1 : r.range(0, 3);
        const float uvx = r.chance(0.5) ? 0.5f : r.uniform(0.0f, 1.0f), uvy = r.chance(0.5) ? 0.5f : r.uniform(0.0f, 1.0f);
        uint8_t* p = pf.ptr();
        std::memcpy(p + 0x38, &scale_x, 4);
        std::memcpy(p + 0x3c, &scale_y, 4);
        std::memcpy(p + 0x20, &colour, 4);
        std::memcpy(p + 0x28, &z, 4);
        std::memcpy(p + 0x30, &uvx, 4);
        std::memcpy(p + 0x34, &uvy, 4);
        std::memcpy(p + 0x48, &filt, 4);
        std::memcpy(p + 0x4c, &ztest, 4);
        // O lado emulado: a mesma fonte com a textura falsa em +0x7c.
        std::memcpy(e.m.mem(kFontAt, uint32_t(pf.file.size())), pf.file.data(), pf.file.size());
        e.m.write32(kFontAt + 0x7c, kTexObj);
        e.m.write32(kTexObj + 0x10, r.next());
        const std::string text = random_text(r, 80);
        std::memcpy(e.m.mem(kTextAt, uint32_t(text.size() + 1)), text.c_str(), text.size() + 1);
        const float x = r.uniform(-50.0f, 650.0f), y = r.uniform(-50.0f, 500.0f);
        ++n;
        std::string why;
        bool ok = true;
        // FONT_getrecta
        {
            float pr[4] = {-1, -1, -1, -1};
            drv::FONT_getrecta(pf.ptr(), text.c_str(), &pr[0], &pr[1], &pr[2], &pr[3]);
            for (int i = 0; i < 4; ++i) e.m.write32(kRectAt + uint32_t(i) * 4, 0xdeadbeef);
            e.call(kFnGetRect, {kFontAt, kTextAt, kRectAt, kRectAt + 4, kRectAt + 8, kRectAt + 12});
            for (int i = 0; i < 4 && ok; ++i)
                if (e.m.read32(kRectAt + uint32_t(i) * 4) != fbits(pr[i])) {
                    ok = false;
                    why = "getrect " + std::to_string(i);
                }
        }
        // FONT_drawtexta
        if (ok) {
            drv::GsPacket pp;
            drv::FONT_drawtexta(x, y, pf, text.c_str(), pp);
            emu_packet_begin(e);
            e.m.set_fpr(12, x);
            e.m.set_fpr(13, y);
            e.call(kFnDrawTextA, {kFontAt, kTextAt});
            const std::vector<uint64_t> ep = emu_packet(e);
            words += pp.words.size();
            glyphs += pp.words.size() > 40 ? (pp.words.size() - 40) / 8 : 0;
            if (!same_packet(ep, pp.words, why)) ok = false;
            // O cabeçalho da fonte (sem o +0x7c).
            if (ok)
                for (uint32_t i = 0; i < 0x80 && ok; ++i)
                    if (i < 0x7c && e.m.mem(kFontAt + i, 1)[0] != pf.file[i]) {
                        ok = false;
                        why = "cabeçalho +" + hex(i);
                    }
        }
        if (!ok) {
            ++bad;
            if (first.empty()) first = fc.name + " \"" + text.substr(0, 30) + "\": " + why;
        }
    }
    std::printf("  FONT (getrect e drawtexta, %zu fontes): %" PRIu64 " casos, %" PRIu64 " palavras, %" PRIu64
                " caracteres: %" PRIu64 " diferentes\n",
                fonts.size(), n, words, glyphs, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    return bad ? 1 : 0;
}

// O GSubtitles do original e do porte.
std::string compare_subtitles(Emu& e, const drv::GSubtitlesData& s) {
    const uint8_t* o = e.m.mem(kSubObj, 0x94);
    auto w = [&](uint32_t off) {
        uint32_t v;
        std::memcpy(&v, o + off, 4);
        return v;
    };
    auto cmp = [&](const char* what, uint32_t off, uint32_t port) -> std::string {
        if (w(off) != port) return std::string(what) + " " + hex(w(off)) + " x " + hex(port);
        return std::string();
    };
    std::string d;
    if ((w(0x1c) == 0) != (s.frames == nullptr)) return "tabela nula";
    if (w(0x24) == s.count && w(0x1c) != 0 && s.frames)
        for (uint32_t i = 0; i < s.count * 2; ++i)
            if (e.m.read32(w(0x1c) + i * 4) != uint32_t(nf::raw(s.frames)[i]))
                return "tabela " + std::to_string(i) + " " + hex(e.m.read32(w(0x1c) + i * 4)) + " x " +
                       hex(uint32_t(nf::raw(s.frames)[i]));
    if (!(d = cmp("modo", 0x0, uint32_t(s.game_clock))).empty()) return d;
    if (!(d = cmp("duracao", 0x4, fbits(s.duration))).empty()) return d;
    if (!(d = cmp("inicio", 0x8, uint32_t(s.start))).empty()) return d;
    if (!(d = cmp("indice", 0x20, s.index)).empty()) return d;
    if (!(d = cmp("contagem", 0x24, s.count)).empty()) return d;
    if (!(d = cmp("rotulo", 0x28, uint32_t(s.label))).empty()) return d;
    if ((w(0x1c) == 0) != (s.frames == nullptr)) return "tabela nula";
    if (w(0x1c) != 0 && s.frames)
        for (uint32_t i = 0; i < s.count * 2; ++i)
            if (e.m.read32(w(0x1c) + i * 4) != uint32_t(nf::raw(s.frames)[i])) return "tabela " + std::to_string(i);
    const uint32_t sp = 0x2c;
    const drv::GSubtitleSplitterData& p = s.splitter;
    if (!(d = cmp("pedacos", sp + 0x50, p.count)).empty()) return d;
    if (!(d = cmp("atual", sp + 0x54, p.current)).empty()) return d;
    if (!(d = cmp("total", sp + 0x58, fbits(p.duration))).empty()) return d;
    if (!(d = cmp("comeco", sp + 0x5c, uint32_t(p.start))).empty()) return d;
    if (!(d = cmp("relogio", sp + 0x60, uint32_t(p.game_clock))).empty()) return d;
    for (uint32_t i = 0; i < p.count && i < 10; ++i) {
        const std::string a = e.cstr(w(sp + i * 4));
        const char* b = nf::raw(p.lines[i]);
        if (a != (b ? b : "(nulo)")) return "pedaco " + std::to_string(i);
        // O 0/0 de um texto vazio (o rótulo 0 dos .sub): o interpretador dá o ±FLT_MAX (0x7f7fffff) da divisão por zero
        // e o ps2_float do port o máximo do PS2 (0x7fffffff); o GetString não avança nenhum dos dois. Contados à parte.
        if (w(sp + 0x28 + i * 4) == 0x7f7fffffu && fbits(p.durations[i]) == 0x7fffffffu) {
            ++g_div_zero;
            continue;
        }
        if (!(d = cmp("tempo", sp + 0x28 + i * 4, fbits(p.durations[i]))).empty()) return d;
    }
    return std::string();
}

std::string random_sub(Rng& r, const std::vector<int32_t>& labels) {
    const int n = r.range(0, 14);
    std::string s = std::to_string(n) + "\r\n";
    int f = r.range(0, 40);
    for (int i = 0; i < n; ++i) {
        f += r.chance(0.1) ? -r.range(0, 30) : r.range(0, 200);
        if (f < 0) f = 0;
        const int32_t label = r.chance(0.4) ? 0 : labels[size_t(r.next()) % labels.size()];
        s += "FRAME_" + std::to_string(f) + "=" + std::to_string(label);
        if (i + 1 < n || r.chance(0.5)) s += "\r\n";
    }
    return s;
}

int check_subtitles(Emu& e, const std::vector<std::string>& subs, const std::vector<uint8_t>& font, int cases,
                    uint32_t seed) {
    Rng r(seed ^ 0x5a5au);
    uint64_t n = 0, bad = 0, loads = 0, setups = 0, draws = 0, packets = 0, displays = 0, guarded = 0;
    std::string first;
    // Os rótulos que os .sub usam e outros do .loc.
    std::vector<int32_t> labels = {8123, 8124, 8125, 8126, 8127, 8128, 8161, 8162, 8173, 8174, 8175, 8176, 8605, 8706,
                                   0x18576, 0x18577, 1, 99999};
    // O porte: o GSubtitles com a fonte do loading.sfn (a imagem 1); o original: o objeto em kSubObj com a fonte em
    // kFontAt.
    drv::GSubtitlesData& ps = drv::gsubtitles_storage();
    drv::subtitle_files().load = [&](const std::string& name, std::vector<uint8_t>& out) {
        if (name == "data\\loading\\loading.sfn") {
            out = font;
            return true;
        }
        if (e.sub_file.empty()) return false;
        out = e.sub_file;
        return true;
    };
    drv::subtitle_files().image = [&](const std::vector<uint8_t>&) { return e.image; };
    drv::GSubtitles_Construct(ps);
    std::memcpy(e.m.mem(kFontAt, uint32_t(font.size())), font.data(), font.size());
    e.m.write32(kFontAt + 0x7c, kTexObj);
    e.m.write32(kTexObj + 0x10, 0);
    std::memset(e.m.mem(kSubObj, 0x94), 0, 0x94);
    {
        uint8_t* o = e.m.mem(kSubObj, 0x94);
        const float wdt = 640.0f, hgt = 448.0f;
        std::memcpy(o + 0x14, &wdt, 4);
        std::memcpy(o + 0x18, &hgt, 4);
        e.m.write32(kSubObj + 0xc, kFontAt);
        e.m.write32(kSubObj + 0x10, kFontAt);
        e.m.write32(kSubObj + 0x90, 0x3ac0f8);
    }
    e.m.write32(kGSubApp, kSubObj);
    drv::simulation().dt = 1.0f / 60.0f;
    for (int c = 0; c < cases; ++c) {
        // O relógio: o TIMER e o passo do jogo andam.
        e.timer += r.range(0, 40);
        drv::audio_set_timer(e.timer);
        drv::simulation().steps += r.range(0, 30);
        e.m.write32(kGSteps, uint32_t(drv::simulation().steps));
        const float dt = drv::simulation().dt;
        std::memcpy(e.m.mem(kGDt, 4), &dt, 4);
        int op = r.range(0, 99);
        if (op >= 6 && op < 50 && ps.frames == nullptr && ps.count != 0) {
            // A tabela liberada pelo ClearSubtitles com a contagem de antes: o original leria a tabela no endereço 0
            // (o port guarda e conta). No lugar do quadro, uma carga.
            ++guarded;
            op = 0;
        }
        std::string what;
        drv::GsPacket pp;
        bool packet = false;
        if (op < 6) {
            // LoadSubtitles: um .sub do disco ou sorteado (ou nenhum: o arquivo não existe)
            std::string s = r.chance(0.6) ? subs[size_t(r.next()) % subs.size()] : random_sub(r, labels);
            if (r.chance(0.05)) s.clear();
            e.sub_file.assign(s.begin(), s.end());
            drv::GSubtitles_LoadSubtitles(ps, "x.sub");
            e.m.write32(kTextAt, 0x75732e78);  // "x.su"
            e.m.write32(kTextAt + 4, 0x62);
            e.call(kFnLoadSubs, {kSubObj, kTextAt});
            what = "LoadSubtitles";
            ++loads;
        } else if (op < 50) {
            // SetupFMVSubtitles e DrawSubtitles (o DrawSubtitlesCallback)
            const uint32_t frame = uint32_t(r.range(0, 2000));
            if (r.chance(0.5)) {
                drv::DrawSubtitlesCallback(frame, pp);
                emu_packet_begin(e);
                e.call(kFnSubCallback, {frame});
                what = "DrawSubtitlesCallback";
                packet = true;
            } else {
                drv::GSubtitles_SetupFMVSubtitles(ps, frame);
                e.call(kFnSetupFmv, {kSubObj, frame});
                what = "SetupFMVSubtitles";
            }
            ++setups;
        } else if (op < 85) {
            drv::GSubtitles_DrawSubtitles(ps, pp);
            emu_packet_begin(e);
            e.call(kFnDrawSubs, {kSubObj});
            what = "DrawSubtitles";
            packet = true;
            ++draws;
        } else if (op < 97) {
            const int32_t label = labels[size_t(r.next()) % labels.size()];
            const float secs = r.chance(0.2) ? 0.0f : r.uniform(0.0f, 8.0f);
            e.skip_cinematic = r.chance(0.15) ? 1 : 0;
            drv::GSubtitles_DisplaySubtitle(ps, label, secs, e.skip_cinematic != 0);
            e.m.set_fpr(12, secs);
            e.call(kFnDisplaySub, {kSubObj, uint32_t(label)});
            what = "DisplaySubtitle";
            ++displays;
        } else {
            drv::GSubtitles_ClearSubtitles(ps);
            e.call(kFnClearSubs, {kSubObj});
            what = "ClearSubtitles";
        }
        ++n;
        std::string why = compare_subtitles(e, ps);
        if (why.empty() && packet) {
            const std::vector<uint64_t> ep = emu_packet(e);
            if (!same_packet(ep, pp.words, why)) why = "pacote " + why;
            if (!pp.words.empty()) ++packets;
        }
        if (!why.empty()) {
            ++bad;
            if (first.empty()) {
                char b[256];
                std::snprintf(b, sizeof b, " | emu modo %u inicio %u dur %08x rot %d | porte modo %d inicio %d dur %08x rot %d | passo %d timer %d",
                              e.m.read32(kSubObj), e.m.read32(kSubObj + 8), e.m.read32(kSubObj + 4),
                              int(e.m.read32(kSubObj + 0x28)), ps.game_clock, ps.start, fbits(ps.duration), ps.label,
                              drv::simulation().steps, e.timer);
                first = "caso " + std::to_string(c) + " " + what + ": " + why + b;
            }
        }
    }
    drv::GSubtitles_Destruct(ps);
    drv::subtitle_files() = drv::SubtitleFiles();
    std::printf("  GSubtitles e GSubtitleSplitter: %" PRIu64 " operações (%" PRIu64 " cargas, %" PRIu64
                " quadros de filme, %" PRIu64 " desenhos, %" PRIu64 " legendas do jogo, %" PRIu64
                " pacotes com texto; %" PRIu64 " divisões 0/0 de texto vazio; %" PRIu64
                " quadros com a tabela liberada trocados por cargas): %" PRIu64 " diferentes\n",
                n, loads, setups, draws, displays, packets, g_div_zero, guarded, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    return bad ? 1 : 0;
}

int check_clock(Emu& e, int cases, uint32_t seed) {
    Rng r(seed ^ 0x777u);
    uint64_t n = 0, bad = 0, synced = 0, corrected = 0;
    std::string first;
    drv::AvPlayerData pav{};
    drv::AvMsTimer pt{};
    drv::AudioPlayerData pa{};
    drv::av_audio_probe() = [&](int32_t) { return e.played_ms; };
    for (int c = 0; c < cases; ++c) {
        // Um estado sorteado nos dois lados.
        if (c % 16 == 0) {
            std::memset(&pav, 0, sizeof pav);
            pt = drv::AvMsTimer();
            pt.last = e.timer;
            pt.ticks = r.chance(0.5) ? 0 : int64_t(r.range(0, 100000));
            pt.speed = r.chance(0.7) ? 0x1000 : r.range(0, 0x4000);
            pav.timer = &pt;
            pav.offset_ms = r.chance(0.8) ? 0x21 : r.range(-100, 100);
            pav.frame = uint32_t(r.range(0, 3000));
            pav.base_ms = r.range(-2000, 2000);
            pav.last_audio_ms = r.range(-100, 50000);
            pav.drift = r.range(-500, 500);
            pav.has_audio = r.chance(0.8) ? 1 : 0;
            pa.stream = 0;
            pav.audio = r.chance(0.8) ? &pa : nullptr;
            if (!pav.audio) pav.has_audio = 0;  // o original leria o +0xc do nulo
            e.played_ms = r.range(0, 60000);
        }
        // O lado emulado: o AV_PLAYER em kAvObj, o relógio em +0x100, o AUDIO_PLAYER em +0x200.
        uint8_t* av = e.m.mem(kAvObj, 0x300);
        std::memset(av, 0, 0x300);
        auto put = [&](uint32_t off, uint32_t v) { e.m.write32(kAvObj + off, v); };
        put(0x0, pav.audio ? kAvObj + 0x200 : 0);
        put(0x200 + 0xc, 0);
        put(0x40, uint32_t(pav.offset_ms));
        put(0x44, pav.frame);
        put(0x48, fbits(pav.target));
        put(0x4c, uint32_t(pav.base_ms));
        put(0x50, uint32_t(pav.last_audio_ms));
        put(0x54, uint32_t(pav.has_audio));
        put(0x58, uint32_t(pav.drift));
        put(0x5c, kAvObj + 0x100);
        put(0x64, kAvObj + 0x280);  // o DECODER (o GetFrameRate é um gancho)
        put(0x100, uint32_t(pt.last));
        std::memcpy(av + 0x108, &pt.ticks, 8);
        put(0x110, uint32_t(pt.speed));
        e.timer += r.chance(0.3) ? 0 : r.range(1, 30);
        drv::audio_set_timer(e.timer);
        if (r.chance(0.6)) e.played_ms += r.range(-50, 300);
        e.fps = r.chance(0.9) ? ps2::ffloat(0x41efc29fu) : r.uniform(10.0f, 60.0f);
        const int op = r.range(0, 99);
        std::string what;
        uint32_t ret_emu = 0, ret_port = 0;
        e.speed_calls.clear();
        std::vector<uint32_t> port_speed;
        if (op < 40) {
            ret_port = uint32_t(drv::AV_PLAYER_SyncedAudioTime(pav));
            ret_emu = e.call(kFnSynced, {kAvObj});
            what = "SyncedAudioTime";
            ++synced;
        } else if (op < 80) {
            ret_port = drv::AV_PLAYER_IsTimeForDecode(pav, e.fps) ? 1u : 0u;
            ret_emu = e.call(kFnIsTime, {kAvObj}) & 1u;
            what = "IsTimeForDecode";
        } else if (op < 92) {
            const uint32_t sp = r.chance(0.5) ? (r.chance(0.5) ? 0u : 0x1000u) : uint32_t(r.range(0, 0x6000));
            ret_port = uint32_t(drv::AV_PLAYER_SetSpeed(pav, sp));
            if (pav.audio) port_speed.push_back(sp < 0x4001 ? sp : 0x4000);
            ret_emu = e.call(kFnSetSpeed, {kAvObj, sp});
            what = "SetSpeed";
        } else {
            drv::AV_MS_TIMER_Update(pt);
            e.call(kFnTimerUpdate, {kAvObj + 0x100});
            what = "AV_MS_TIMER::Update";
        }
        ++n;
        std::string why;
        if (ret_emu != ret_port) why = "retorno " + hex(ret_emu) + " x " + hex(ret_port);
        auto chk = [&](const char* nm, uint32_t off, uint32_t port) {
            if (why.empty() && e.m.read32(kAvObj + off) != port) why = std::string(nm) + " " + hex(e.m.read32(kAvObj + off)) + " x " + hex(port);
        };
        chk("alvo", 0x48, fbits(pav.target));
        chk("base", 0x4c, uint32_t(pav.base_ms));
        chk("ultimo_som", 0x50, uint32_t(pav.last_audio_ms));
        chk("deriva", 0x58, uint32_t(pav.drift));
        chk("relogio.last", 0x100, uint32_t(pt.last));
        chk("relogio.ticks", 0x108, uint32_t(uint64_t(pt.ticks)));
        chk("relogio.ticks_hi", 0x10c, uint32_t(uint64_t(pt.ticks) >> 32));
        chk("relogio.speed", 0x110, uint32_t(pt.speed));
        if (why.empty() && e.speed_calls != port_speed) why = "AUDIO_PLAYER::SetSpeed";
        if (why.empty() && pav.last_audio_ms != 0 && what == "SyncedAudioTime") ++corrected;
        if (!why.empty()) {
            ++bad;
            if (first.empty()) first = "caso " + std::to_string(c) + " " + what + ": " + why;
        }
    }
    drv::av_audio_probe() = nullptr;
    std::printf("  o relógio do AV_PLAYER (SyncedAudioTime, IsTimeForDecode, SetSpeed, AV_MS_TIMER::Update): %" PRIu64
                " casos (%" PRIu64 " SyncedAudioTime): %" PRIu64 " diferentes\n",
                n, synced, bad);
    if (bad) std::printf("  primeiro: %s\n", first.c_str());
    return bad ? 1 : 0;
}

int check_read_input(Emu& e, int cases, uint32_t seed) {
    Rng r(seed ^ 0x99u);
    uint64_t n = 0, bad = 0, stops = 0;
    drv::PlayMpcData pp{};
    std::memset(e.m.mem(kPlayObj, 0x20), 0, 0x20);
    for (int c = 0; c < cases; ++c) {
        if (r.chance(0.05)) {
            pp = drv::PlayMpcData();
            std::memset(e.m.mem(kPlayObj, 0x20), 0, 0x20);
        }
        const int k = r.range(0, 9);
        e.pad_buttons = uint16_t(k < 5 ? 0 : (k < 8 ? (r.next() & 0xffff) : (r.chance(0.5) ? 0x800 : 0x40)));
        drv::PlayMPC_ReadInput(pp, e.pad_buttons);
        e.call(kFnReadInput, {kPlayObj});
        ++n;
        const uint16_t eb = uint16_t(e.m.read32(kPlayObj + 8) & 0xffff);
        const uint32_t es = e.m.read32(kPlayObj + 0x10);
        if (eb != pp.buttons || es != uint32_t(pp.stop)) ++bad;
        if (pp.stop) ++stops;
    }
    std::printf("  PlayMPC::ReadInput: %" PRIu64 " leituras (%" PRIu64 " paradas): %" PRIu64 " diferentes\n", n, stops,
                bad);
    return bad ? 1 : 0;
}

}  // namespace

int run_p24_movie_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n== parte 24: os filmes (as fontes da EA, as legendas, o relógio do AV_PLAYER) ==\n");
    int failures = 0;
    try {
        drv::Bigf misc;
        if (!misc.open(disc, "DRIVING/MISC.VIV")) {
            std::printf("  sem o DRIVING/MISC.VIV\n");
            return 1;
        }
        auto read = [&](const char* name, std::vector<uint8_t>& out) {
            const int i = misc.find(name);
            return i >= 0 && misc.read(i, out);
        };
        std::vector<uint8_t> loc;
        if (read("data\\gfx\\locale\\IDL_ENG.loc", loc)) drv::locale().load(loc);
        std::vector<FontCase> fonts;
        for (const char* f : {"data\\loading\\loading.sfn", "data\\loading\\error.sfn", "data\\render\\debug.sfn"}) {
            FontCase fc;
            fc.name = f;
            if (read(f, fc.file)) fonts.push_back(std::move(fc));
        }
        std::vector<std::string> subs;
        for (const auto& en : misc.entries()) {
            const std::string& nm = en.name;
            if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, ".sub") == 0) {
                std::vector<uint8_t> b;
                if (read(nm.c_str(), b)) subs.emplace_back(b.begin(), b.end());
            }
        }
        if (fonts.empty() || subs.empty()) {
            std::printf("  sem as fontes ou os .sub no MISC.VIV\n");
            return 1;
        }
        std::printf("  %zu fontes, %zu .sub, o IDL_ENG.loc\n", fonts.size(), subs.size());
        Emu e;
        e.setup(elf);
        failures += check_fonts(e, fonts, cases, seed);
        failures += check_subtitles(e, subs, fonts[0].file, cases, seed);
        failures += check_clock(e, cases, seed);
        failures += check_read_input(e, cases, seed);
        std::printf("  instruções do interpretador: %" PRIu64 "\n", uint64_t(e.m.steps()));
    } catch (const std::exception& ex) {
        std::printf("  erro: %s\n", ex.what());
        return 1;
    }
    return failures;
}

}  // namespace drvcheck
