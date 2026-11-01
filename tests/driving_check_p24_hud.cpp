// nf_driving_check, parte 24 (tests/driving_check_p24.h; docs/driving.md seção 28): o HUD das missões de carro. O código
// ORIGINAL do DRIVING.ELF roda no interpretador MIPS (com a VU0) lado a lado com o porte (driving/game/hud_draw.h,
// driving/render/ggallery.h, hud_gfx.h, ea_font.h):
//  - o GHud::GHud inteiro sobre o hudNTSC.gal, o blood.ssh e o loading.sfn do disco (o GGallery::GGallery com o
//    UGroup::Deserialize, o FONT_Load com o FONT_create, os GET_Canvas/GET_Puppet/GET_Key, as peças que sobem 10 pontos,
//    o GHud::Reset): os ponteiros dos puppets (pela posição no arquivo), as palavras do arquivo da galeria e o GHud;
//  - com estados sorteados do GHud, da galeria, da fonte, do gerente da missão (a vida, o escudo, os cronômetros), do
//    SWeaponManager (a arma, a munição, o nome), do passo da simulação, do modo de cinema, das falas e da música: o
//    GOrthoHudView::DoRender inteiro, o GHud::Render (o tempo do quadro e o fim da missão) e cada Draw* (o objetivo, a
//    mensagem, o botão com os símbolos *A* *G* *P* *D*, o cronômetro, a vida, a arma, o aparelho, o "stylin", o
//    resultado, o "fly-by-wire", as tarjas, o escurecer, os golpes na tela, a cortina de sangue): a lista de desenho
//    (cada USimpleMaterial::Draw e USimpleTexturedMaterial::Draw com o tipo, a contagem, o estado do material, a matriz,
//    as posições, o STQ, as cores e a textura; entre eles as palavras do GIF que o driver de fontes escreve), os sons
//    (AMenuSound::Trigger), a matriz da vista do HUD (EAGL::ViewPort::SetViewMatrix), o GHud, o estado da galeria e da
//    fonte depois.
// Os textos do .loc vêm do IDL_ENG.loc nos dois lados (o GSystem::LOCALE_Symbol do original devolve a cadeia do porte).
//
// Tudo aqui lê dados do jogo: a saída é um relatório, nada é gravado.

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/camera/camera_ini.h"
#include "driving/game/hud.h"
#include "driving/game/hud_draw.h"
#include "driving/game/hud_pause.h"
#include "driving/game/input_config.h"
#include "driving/game/triggers.h"
#include "driving/sound/audio.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/target_picker.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/render/ea_font.h"
#include "driving/render/ggallery.h"
#include "driving/render/hud_gfx.h"
#include "driving/render/subtitles.h"
#include "driving_check_p24.h"
#include "platform/crt_compat.h"
#include "platform/disc_source.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções do original.
constexpr uint32_t kFnGHudCtor = 0x23ae30, kFnRender = 0x23cbc0, kFnDoRender = 0x23aad0, kFnDraw = 0x23cf88;
constexpr uint32_t kFnObjective = 0x246c48, kFnMessage = 0x2469b8, kFnButton = 0x245ab8, kFnTimer = 0x23e580;
constexpr uint32_t kFnHealth = 0x23da30, kFnWeapon = 0x240c98, kFnGadget = 0x244f88, kFnStylin = 0x2472b0;
constexpr uint32_t kFnResult = 0x245f68, kFnFlyByWire = 0x23ead8, kFnLetterBox = 0x245190, kFnFades = 0x23e930;
constexpr uint32_t kFnHits = 0x23e7b8, kFnBlood = 0x23e2c0, kFnCrossHairs = 0x23f328;
constexpr uint32_t kFnZooming = 0x23ec08, kFnFading = 0x23f080, kFnTargeting = 0x22bda0, kFnAimOn = 0x244ef0;
constexpr uint32_t kFnZoomPercent = 0x1b8960;
constexpr uint32_t kGPicker = 0x33a970, kGSniper = 0x3353ec, kGAutoAimBlink = 0x33b1d0, kGSimCars = 0x339f5c;
constexpr uint32_t kGModeTab = 0x3339bc, kGArmTab = 0x4244f8;
// Os globais.
constexpr uint32_t kGVifAcc = 0x34861c, kGFontAcc = 0x348b94, kGHudApp = 0x33ab6c, kGMission = 0x33a024;
constexpr uint32_t kGWeapons = 0x33a530, kGRenderer = 0x333760, kGHudOn = 0x32e670, kGAudioTargets = 0x32e664;
constexpr uint32_t kGMatU = 0x3335c0, kGMatT = 0x3335c4, kGModel = 0x3335c8, kGZ = 0x3335cc;
constexpr uint32_t kGQuadN = 0x3335d4, kGQuadTex = 0x3335e0, kGSprN = 0x3335ec, kGSprTex = 0x3335f0;
constexpr uint32_t kGLayerIdxU = 0x3c82f4, kGLayersU = 0x3c82f8, kGLayerIdxT = 0x3c82fc, kGLayersT = 0x3c8300;
constexpr uint32_t kGSteps = 0x339f54, kGDt = 0x339f50, kGRate = 0x339f4c;
constexpr uint32_t kGBodyHit = 0x33b1c8, kGGadgetsEnum = 0x33b1cc, kUber = 0x100000;

// A RAM emulada.
constexpr uint32_t kGHud = 0x00410000;       // o GHud (0x500)
constexpr uint32_t kVifObj = 0x00423000;     // o acumulador do DmaMgr
constexpr uint32_t kAccObj = 0x00423100;     // o 0x00348b94
constexpr uint32_t kCtxObj = 0x00424000;     // o RenderContext
constexpr uint32_t kMission = 0x00480000;    // o SMissionManager (0x1b50)
constexpr uint32_t kWeapons = 0x00490000;    // o SWeaponManager (0x190); a tabela em +0x200
constexpr uint32_t kRenderer = 0x00494000;   // o RRenderer::fgRenderer
constexpr uint32_t kLayerU = 0x00495000, kLayerT = 0x00495100;  // as camadas dos dois materiais
constexpr uint32_t kMatU = 0x00495200, kMatT = 0x00495300;      // os dois materiais (GeoPrimState)
constexpr uint32_t kViewMat = 0x00496000;    // a matriz da vista (a identidade)
constexpr uint32_t kStreams = 0x00497000;    // os AStream falsos ("Music" +0, "Speech" +0x100) e a AMix +0x200
constexpr uint32_t kLocStr = 0x004a0000;     // as cadeias do LOCALE_Symbol (em anel)
constexpr uint32_t kPacketAt = 0x00500000;   // o pacote do GIF
constexpr uint32_t kHeapLo = 0x00600000, kHeapHi = 0x00f00000;
constexpr uint32_t kGalFile = 0x01000000, kBloodFile = 0x01200000, kFontFile = 0x01210000;
// Os alvos do WTargetPicker (16 de 0x50), os donos deles, o carro do jogador falso com as tabelas virtuais e o objeto
// do som da arma, o RSniperZoom, as funções falsas das tabelas, as tabelas da câmera.
constexpr uint32_t kTargets = 0x004c0000, kOwners = 0x004c1000, kCarList = 0x004c2000, kCar = 0x004c2100;
constexpr uint32_t kCarVt = 0x004c2200, kAudioObj = 0x004c2300, kAudioVt = 0x004c2500, kSniper = 0x004c2600;
constexpr uint32_t kFnV44 = 0x004c3000, kFnCharge = 0x004c3100;
constexpr uint32_t kModeTab = 0x004c4000, kArmTab = 0x004c5000, kCam = 0x004c6000;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 7) {}
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

std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%llx", (unsigned long long)v);
    return b;
}

// Um item da lista de desenho: as palavras do GIF entre dois materiais, ou um Draw de material.
struct Ev {
    bool packet = false;
    std::vector<uint64_t> words;
    bool textured = false;
    int32_t prim = 0, count = 0;
    uint64_t flags = 0, alpha = 0, misc = 0;
    bool has_m = false;
    uint32_t m[16] = {};
    std::vector<uint32_t> pos, stq, col;
    uint32_t tex = 0;  // a imagem do porte
};

std::string describe(const Ev& e) {
    if (e.packet) return "pacote de " + std::to_string(e.words.size()) + " palavras";
    return std::string(e.textured ? "material com textura" : "material") + " tipo " + std::to_string(e.prim) + " x" +
           std::to_string(e.count) + (e.has_m ? " (matriz)" : "") +
           (e.textured ? " textura " + std::to_string(e.tex) : std::string());
}

std::string compare_events(const std::vector<Ev>& a, const std::vector<Ev>& b) {
    if (a.size() != b.size()) {
        std::string s = "itens " + std::to_string(a.size()) + " x " + std::to_string(b.size());
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i)
            if (a[i].packet != b[i].packet || a[i].prim != b[i].prim || a[i].count != b[i].count) {
                s += " (o " + std::to_string(i) + ": " + describe(a[i]) + " x " + describe(b[i]) + ")";
                return s;
            }
        if (a.size() > n) s += " (a mais no original: " + describe(a[n]) + ")";
        if (b.size() > n) s += " (a mais no porte: " + describe(b[n]) + ")";
        return s;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const Ev& x = a[i];
        const Ev& y = b[i];
        const std::string at = "item " + std::to_string(i) + " (" + describe(x) + "): ";
        if (x.packet != y.packet) return at + "pacote x material";
        if (x.packet) {
            if (x.words.size() != y.words.size()) {
                std::string s = at + "palavras " + std::to_string(x.words.size()) + " x " + std::to_string(y.words.size());
                for (size_t k = 0; k < std::min(x.words.size(), y.words.size()); ++k)
                    if (x.words[k] != y.words[k]) {
                        s += " (a primeira diferente: " + std::to_string(k) + " " + hex(x.words[k]) + " x " +
                             hex(y.words[k]) + ")";
                        break;
                    }
                return s;
            }
            for (size_t k = 0; k < x.words.size(); ++k)
                if (x.words[k] != y.words[k])
                    return at + "palavra " + std::to_string(k) + " " + hex(x.words[k]) + " x " + hex(y.words[k]);
            continue;
        }
        if (x.textured != y.textured || x.prim != y.prim || x.count != y.count) return at + "tipo/contagem";
        if (x.flags != y.flags) return at + "ABE " + hex(x.flags) + " x " + hex(y.flags);
        if (x.alpha != y.alpha) return at + "ALPHA " + hex(x.alpha) + " x " + hex(y.alpha);
        if (x.misc != y.misc) return at + "+0x30 " + hex(x.misc) + " x " + hex(y.misc);
        if (x.has_m != y.has_m) return at + "matriz";
        for (int k = 0; k < 16 && x.has_m; ++k)
            if (x.m[k] != y.m[k]) return at + "matriz " + std::to_string(k) + " " + hex(x.m[k]) + " x " + hex(y.m[k]);
        for (size_t k = 0; k < x.pos.size(); ++k)
            if (x.pos[k] != y.pos[k])
                return at + "posição " + std::to_string(k) + " " + hex(x.pos[k]) + " x " + hex(y.pos[k]);
        for (size_t k = 0; k < x.col.size(); ++k)
            if (x.col[k] != y.col[k]) return at + "cor " + std::to_string(k) + " " + hex(x.col[k]) + " x " + hex(y.col[k]);
        if (x.textured) {
            for (size_t k = 0; k < x.stq.size(); ++k)
                if (x.stq[k] != y.stq[k])
                    return at + "STQ " + std::to_string(k) + " " + hex(x.stq[k]) + " x " + hex(y.stq[k]);
            if (x.tex != y.tex) return at + "textura " + hex(x.tex) + " x " + hex(y.tex);
        }
    }
    return "";
}

// A lista do porte (hud_frame()) como itens: os pacotes seguidos juntos (o acumulador do original é contíguo).
std::vector<Ev> port_events() {
    std::vector<Ev> out;
    for (const drv::HudFrameItem& it : drv::hud_frame()) {
        if (it.is_packet) {
            if (out.empty() || !out.back().packet) {
                Ev e;
                e.packet = true;
                out.push_back(e);
            }
            out.back().words.insert(out.back().words.end(), it.packet.words.begin(), it.packet.words.end());
            continue;
        }
        const drv::HudMatDraw& d = it.draw;
        Ev e;
        e.textured = d.textured;
        e.prim = d.prim;
        e.count = int32_t(d.colour.size());
        e.flags = d.state.flags & 0x40;
        e.alpha = d.state.alpha;
        e.misc = d.state.misc & 1;
        e.has_m = d.has_matrix;
        if (d.has_matrix) std::memcpy(e.m, d.matrix, 64);
        for (float f : d.pos) e.pos.push_back(fbits(f));
        e.col = d.colour;
        if (d.textured) {
            for (float f : d.stq) e.stq.push_back(fbits(f));
            e.tex = d.image;
        }
        out.push_back(std::move(e));
    }
    return out;
}

// O ambiente sorteado das falas e da música (os AStream falsos nos dois lados).
struct StreamFlags {
    bool music_not_found = false, music_over = false, music_paused = false, music_loop = false;
    bool speech_not_found = false, speech_over = false, speech_other_mix = false;
    int music_stops = 0;
};
StreamFlags g_sf;

class HudEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    uint32_t heap = kHeapLo;
    uint32_t loc_ring = 0;
    std::vector<uint8_t> gal_file, blood_file, font_file;
    std::map<uint32_t, uint32_t> tex_map;  // o EAGL::TAR emulado -> a imagem do porte
    uint32_t next_tex = 0x100;
    uint32_t font_image = 0x200;
    std::vector<Ev> events;
    uint32_t pk_from = kPacketAt;
    std::vector<drv::HudSound> sounds;
    bool fly_by_wire = false;
    uint32_t view_m[16] = {};
    int view_calls = 0;
    int pause_on = 0;
    float zoom_ret = 0, aim_ret = 0;             // GetZoomPercent, GetAimFraction
    std::map<uint32_t, bool> owner_emp;          // AttributeSet::LookupBool(dono + 0x5c, ...)
    std::vector<std::pair<int32_t, float>> car_calls;

    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        install();
        m.write32(kGVifAcc, kVifObj);
        m.write32(kGFontAcc, kAccObj);
        m.write32(0x3c7f54, 0x348be0);  // o driver de fontes do EAGL (FONT_installdriver do RRenderer)
        m.write32(0x3c80d0, 100);
        m.write32(kGMatU, kMatU);
        // O RSniperZoom, o carro do jogador (a lista da simulação) e as tabelas virtuais do som da arma.
        m.write32(kGSniper, kSniper);
        m.write32(kGSimCars, kCarList);
        m.write32(kCarList, kCar);
        m.write32(kCar + 0x68, kCarVt);
        m.write32(kCarVt + 0x40, 0);
        m.write32(kCarVt + 0x44, kFnV44);
        m.write32(kAudioObj + 0xac, kAudioVt);
        m.write32(kAudioVt + 0x50, 0);
        m.write32(kAudioVt + 0x54, kFnCharge);
        m.write32(kGMatT, kMatT);
        m.write32(kGLayerIdxU, 0);
        m.write32(kGLayerIdxT, 0);
        m.write32(kGLayersU, kLayerU);
        m.write32(kGLayersT, kLayerT);
        m.write32(kGRenderer, kRenderer);
        m.write32(kRenderer + 0x38, 3);
        m.write32(kRenderer + 0x50, 640);
        m.write32(kRenderer + 0x54, 448);
        m.write32(kGMission, kMission);
        m.write32(kGWeapons, kWeapons);
        m.write32(kGAudioTargets, 0);
        const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        std::memcpy(m.mem(kViewMat, 64), id, 64);
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 200'000'000);
    }
    uint32_t alloc(uint32_t n) {
        const uint32_t at = (heap + 15) & ~15u;
        heap = at + ((n + 15) & ~15u);
        if (heap >= kHeapHi) heap = kHeapLo;
        return at;
    }
    void flush_packet() {
        const uint32_t end = m.read32(kVifObj);
        if (end > pk_from) {
            Ev e;
            e.packet = true;
            e.words.resize((end - pk_from) / 8);
            std::memcpy(e.words.data(), m.mem(pk_from, uint32_t(e.words.size() * 8)), e.words.size() * 8);
            if (!events.empty() && events.back().packet)
                events.back().words.insert(events.back().words.end(), e.words.begin(), e.words.end());
            else
                events.push_back(std::move(e));
        }
        pk_from = end;
    }
    void begin_case() {
        events.clear();
        sounds.clear();
        m.write32(kVifObj, kPacketAt);
        pk_from = kPacketAt;
        view_calls = 0;
        pause_on = 0;
        car_calls.clear();
    }
    void record_material(MipsMachine& mm, bool textured) {
        flush_packet();
        Ev e;
        e.textured = textured;
        const uint32_t mat = uint32_t(mm.gpr(R::a0));
        e.prim = int32_t(mm.gpr(R::a1));
        e.count = int32_t(mm.gpr(R::a2));
        const uint32_t mtx = uint32_t(mm.gpr(R::a3));
        e.flags = (uint64_t(mm.read32(mat + 0x10)) | uint64_t(mm.read32(mat + 0x14)) << 32) & 0x40;
        e.alpha = uint64_t(mm.read32(mat + 0x40)) | uint64_t(mm.read32(mat + 0x44)) << 32;
        e.misc = mm.read32(mat + 0x30) & 1;
        e.has_m = mtx != 0;
        if (mtx) std::memcpy(e.m, mm.mem(mtx, 64), 64);
        const uint32_t layer = textured ? mm.read32(kGLayersT + mm.read32(kGLayerIdxT) * 4)
                                        : mm.read32(kGLayersU + mm.read32(kGLayerIdxU) * 4);
        const uint32_t n = uint32_t(e.count);
        auto words = [&](uint32_t at, uint32_t cnt, std::vector<uint32_t>& out) {
            out.resize(cnt);
            if (cnt) std::memcpy(out.data(), mm.mem(at, cnt * 4), cnt * 4);
        };
        words(mm.read32(layer + 0x18), n * 4, e.pos);
        if (textured) {
            words(mm.read32(layer + 0x20), n * 4, e.stq);
            words(mm.read32(layer + 0x28), n, e.col);
            const uint32_t tar = mm.read32(layer + 0x30);
            auto it = tex_map.find(tar);
            e.tex = it != tex_map.end() ? it->second : 0xdead0000u | (tar & 0xffff);
        } else {
            words(mm.read32(layer + 0x20), n, e.col);
        }
        events.push_back(std::move(e));
    }

    void install() {
        auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
        auto ret_a0 = [](MipsMachine& mm) { mm.set_gpr(R::v0, mm.gpr(R::a0)); };
        m.hook(0x0010c6c8, "printf", ret(0));
        m.hook(0x002d6b80, "GetFoundationVideoMode", ret(1));
        m.hook(0x002913f8, "TIMER_removetask", ret(0));
        m.hook(0x002cfda8, "UMemory::FastAlloc", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0))));
        });
        m.hook(0x002d0910, "__builtin_new", [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(0x002d09c8, "operator_new", [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(0x002871f0, "eagl_alloc", [this](MipsMachine& mm) { mm.set_gpr(R::v0, alloc(uint32_t(mm.gpr(R::a0)))); });
        m.hook(0x002d09a8, "__builtin_delete", ret(0));
        m.hook(0x001d40f0, "RViewCamera::RViewCamera", ret_a0);
        m.hook(0x00165288, "ActionQueue::ActionQueue", ret_a0);
        m.hook(0x001655a8, "ActionQueue::Flush", ret(0));
        m.hook(0x001654c8, "ActionQueue::IsEmpty", ret(1));
        m.hook(0x001d4500, "RViewCamera::Render", ret(0));
        m.hook(0x0024b3e8, "GSystem::FILE_FullPathname", ret(0));
        m.hook(0x0024b460, "GSystem::FILE_Load", [this](MipsMachine& mm) {
            std::memcpy(mm.mem(kGalFile, uint32_t(gal_file.size())), gal_file.data(), gal_file.size());
            mm.set_gpr(R::v0, kGalFile);
        });
        m.hook(0x002d7778, "UFileLoader::ShapeFileLoad", [this](MipsMachine& mm) {
            std::memcpy(mm.mem(kBloodFile, uint32_t(blood_file.size())), blood_file.data(), blood_file.size());
            mm.set_gpr(R::v0, kBloodFile);
        });
        m.hook(0x002d73c0, "UFileLoader::FileLoad", [this](MipsMachine& mm) {
            std::memcpy(mm.mem(kFontFile, uint32_t(font_file.size())), font_file.data(), font_file.size());
            std::memset(mm.mem(kFontFile + uint32_t(font_file.size()), 4), 0, 4);
            mm.set_gpr(R::v0, kFontFile);
        });
        m.hook(0x002d7ac8, "UFileLoader::FileFree", ret(0));
        m.hook(0x0027e1e8, "EAGL::TAR::TAR", [this](MipsMachine& mm) {
            const uint32_t tar = uint32_t(mm.gpr(R::a0));
            std::memset(mm.mem(tar, 0x60), 0, 0x60);
            tex_map[tar] = next_tex++;
            mm.set_gpr(R::v0, tar);
        });
        m.hook(0x00249f98, "GLocale::GetLanguage", ret(0));
        m.hook(0x0024a1d0, "GSubtitles::TheApp", ret(0));
        m.hook(0x0024a870, "GSubtitles::ClearSubtitles", ret(0));
        m.hook(0x0024a1e0, "GSubtitles::IsSubtitleDisplaying", ret(0));
        m.hook(0x0024a6d0, "GSubtitles::DrawSubtitles", ret(0));
        m.hook(0x00208558, "SMissionManager::CheckProgrammerEvent", [](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint32_t(drv::SMissionManager_CheckProgrammerEvent(drv::mission_manager(),
                                                                                  int32_t(mm.gpr(R::a1)))));
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
        m.hook(0x002f4b10, "AMenuSound::Trigger", [this](MipsMachine& mm) {
            drv::HudSound s;
            s.bank = int32_t(mm.gpr(R::a0));
            s.name = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
            s.mix = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a2)), 1));
            s.arg = int32_t(mm.gpr(R::a3));
            sounds.push_back(s);
            mm.set_gpr(R::v0, 0);
        });
        m.hook(0x0023eab0, "GHud fly-by-wire", [this](MipsMachine& mm) { mm.set_gpr(R::v0, fly_by_wire ? 1 : 0); });
        m.hook(0x00241048, "GHud::PauseOn", [this](MipsMachine& mm) {
            ++pause_on;
            mm.set_gpr(R::v0, 0);
        });
        // As miras: o atan2f do newlib pelo nf::fm::atan2 (como na visibilidade), o zoom e a fração da mira sorteados, o
        // atributo dos donos dos alvos, as funções das tabelas virtuais do carro e do som da arma, o ponto da tela.
        m.hook(0x002c0618, "atan2f", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::atan2(mm.fpr(12), mm.fpr(13))); });
        // O sinf/cosf do newlib (o BuildRotate) pelo nf::fm, como nas outras partes.
        m.hook(0x2bfc38, "sinf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::sin(mm.fpr(12))); });
        m.hook(0x2bfa48, "cosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::cos(mm.fpr(12))); });
        m.hook(kFnZoomPercent, "RPlayerCamera::GetZoomPercent", [this](MipsMachine& mm) { mm.set_fpr(0, zoom_ret); });
        m.hook(0x001237d0, "AICharacterHands::GetAimFraction", [this](MipsMachine& mm) { mm.set_fpr(0, aim_ret); });
        m.hook(0x00169b28, "AttributeSet::LookupBool", [this](MipsMachine& mm) {
            auto it = owner_emp.find(uint32_t(mm.gpr(R::a0)) - 0x5c);
            mm.set_gpr(R::v0, it != owner_emp.end() && it->second ? 1 : 0);
        });
        m.hook(kFnV44, "o objeto do som da arma do carro", ret(kAudioObj));
        m.hook(kFnCharge, "a carga da arma", [this](MipsMachine& mm) { car_calls.push_back({3, mm.fpr(12)}); });
        m.hook(0x002dc5a0, "AZoomObj::InTheZone", [this](MipsMachine& mm) {
            car_calls.push_back({2, float(int32_t(mm.gpr(R::a1)))});
        });
        m.hook(0x0022a128, "WTargetPicker (o ponto na tela)", [](MipsMachine& mm) {
            float pt[2];
            drv::WTargetPicker_GetScreenPos(drv::target_picker().world_target, pt);
            mm.set_gpr(R::v0, uint64_t(fbits(pt[0])) | uint64_t(fbits(pt[1])) << 32);
        });
        // O menu de pausa não tem porte (o nf_driving não pausa; o porte só conta).
        m.hook(0x00241f40, "GHud::DrawPauseErrors", ret(0));
        m.hook(0x002426a0, "GHud::DrawPauseMenu", ret(0));
        // O driver de fontes do EAGL (como na conferência dos filmes).
        m.hook(0x0027e388, "TAR::Set", ret(0));
        m.hook(0x0027d340, "EAGL::TARExtension::OutputGifTags", [this](MipsMachine& mm) {
            const uint32_t tex = uint32_t(mm.gpr(R::a0)) - 4;
            const bool linear = (mm.read32(tex + 0x10) & 0x30u) == 0x10u;
            uint64_t regs[12];
            drv::ea_font_texture_regs(font_image | (linear ? 0x80000000u : 0u), regs);
            std::memset(mm.mem(uint32_t(mm.gpr(R::a1)), 16), 0, 16);
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)) + 0x10, 96), regs, 96);
        });
        m.hook(0x00287788, "EAGL::Device::Get", ret(0x00424800));
        m.hook(0x00287728, "EAGL::Device::GetCurrentRenderContext", ret(kCtxObj));
        m.hook(0x00284038, "EAGL::GeoPrimState::SetDepthTestMethod", ret(0));
        // Os materiais: a lista de desenho.
        m.hook(0x002cf5b8, "USimpleMaterial::Draw", [this](MipsMachine& mm) { record_material(mm, false); });
        m.hook(0x002cf9a0, "USimpleTexturedMaterial::Draw", [this](MipsMachine& mm) { record_material(mm, true); });
        // A vista do HUD.
        m.hook(0x00297480, "EAGL::ViewPort::GetViewMatrix", ret(kViewMat));
        m.hook(0x00296e80, "EAGL::ViewPort::SetViewMatrix", [this](MipsMachine& mm) {
            std::memcpy(view_m, mm.mem(uint32_t(mm.gpr(R::a1)), 64), 64);
            ++view_calls;
        });
        // As falas e a música.
        m.hook(0x002e9688, "AStream::Get", [](MipsMachine& mm) {
            const std::string n = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a0)), 1));
            mm.set_gpr(R::v0, n == "Music" ? kStreams : kStreams + 0x100);
        });
        m.hook(0x002f2dc8, "AMix::Get", ret(kStreams + 0x200));
        m.hook(0x002e9bc8, "AStream::Stop", [](MipsMachine& mm) {
            if (uint32_t(mm.gpr(R::a0)) == kStreams) ++g_sf.music_stops;
            mm.set_gpr(R::v0, 0);
        });
        auto flag = [](bool StreamFlags::*mu, bool StreamFlags::*sp) {
            return [mu, sp](MipsMachine& mm) {
                const bool music = uint32_t(mm.gpr(R::a0)) == kStreams;
                mm.set_gpr(R::v0, (music ? g_sf.*mu : g_sf.*sp) ? 1 : 0);
            };
        };
        m.hook(0x002e9980, "AStream::IsNotFound", flag(&StreamFlags::music_not_found, &StreamFlags::speech_not_found));
        m.hook(0x002e9958, "AStream::IsOver", flag(&StreamFlags::music_over, &StreamFlags::speech_over));
        m.hook(0x002e9990, "AStream::IsPaused", flag(&StreamFlags::music_paused, &StreamFlags::music_paused));
        m.hook(0x002e99a0, "AStream::IsLoop", flag(&StreamFlags::music_loop, &StreamFlags::music_loop));
    }
};

// O porte com os mesmos AStream falsos.
void port_stream_env() {
    drv::HudDrawEnv& env = drv::hud_draw_env();
    env.speech_done = [] { return g_sf.speech_over || g_sf.speech_other_mix; };
    env.result_streams_done = [] {
        ++g_sf.music_stops;
        const bool music = g_sf.music_not_found || g_sf.music_over || g_sf.music_paused || g_sf.music_loop;
        if (!music) return false;
        return g_sf.speech_not_found || g_sf.speech_over || g_sf.speech_other_mix;
    };
}

struct Ctx {
    std::vector<uint8_t> gal, blood, font;
    std::vector<int32_t> labels;          // rótulos do .loc
    std::vector<int32_t> button_labels;   // os que têm *A*, *G*, *P* ou *D*
    std::vector<int32_t> percent_labels;  // os que têm '%' (o FONT_drawtextfa os formata)
    std::vector<int32_t> weapon_labels;   // os nomes das armas (a tabela do SWeaponManager: o sprintf do DrawWeapon
                                          // num buffer de pilha não cabe um texto qualquer)
    uint32_t port_base = 0;               // o arquivo da galeria do porte
    uint32_t emu_gallery = 0;             // o GGallery emulado
    std::vector<drv::WTargetableData> targets = std::vector<drv::WTargetableData>(16);  // os alvos do porte
    bool emp[16] = {};                    // ONLY_TARGET_WITH_EMP de cada alvo
    float zoom_ret = 0, aim_ret = 0;
};

// Um ponteiro do original e um do porte que devem ser o mesmo lugar do arquivo da galeria.
bool same_place(uint32_t e, uint32_t p, const Ctx& c, size_t size) {
    if (e == 0 || p == 0) return e == p;
    if (e < kGalFile || e >= kGalFile + size || p < c.port_base || p >= c.port_base + size) return false;
    return e - kGalFile == p - c.port_base;
}

uint32_t gal_u32(const drv::GGalleryData& g, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(&g) + off, 4);
    return v;
}

int check_ctor(HudEmu& e, Ctx& c) {
    int fail = 0;
    // O porte: as imagens em ordem (as texturas da galeria, depois a cortina), a da fonte 0x200.
    uint32_t next = 0x100;
    drv::HudFiles f;
    f.gallery = c.gal;
    f.blood = c.blood;
    f.font = c.font;
    f.image = [&next](const uint8_t*, uint32_t, bool) { return next++; };
    f.font_image = [](const std::vector<uint8_t>&) { return 0x200u; };
    drv::GHudData& h = drv::hud();
    std::memset(&h, 0, sizeof h);
    if (!drv::GHud_Construct(h, std::move(f))) {
        std::printf("  GHud::GHud do porte falhou\n");
        return 1;
    }
    c.port_base = nf::p32_u(drv::hud_gallery_store().file.data());
    // O original.
    std::memset(e.m.mem(kGHud, 0x500), 0, 0x500);
    e.call(kFnGHudCtor, {kGHud});
    c.emu_gallery = e.m.read32(kGHud + 8);
    const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
    std::string why;
    // Os ponteiros (0x10 a 0x2a0), a cortina (+0) e a raiz (+0xc).
    for (uint32_t off = 0; off < 0x2a0 && why.empty(); off += 4) {
        if (off == 8) continue;  // o GGallery
        const uint32_t ev = e.m.read32(kGHud + off);
        uint32_t pv;
        std::memcpy(&pv, hp + off, 4);
        if (off == 0) {
            auto it = e.tex_map.find(ev);
            if (it == e.tex_map.end() || it->second != pv) why = "a cortina " + hex(ev) + " x " + hex(pv);
            continue;
        }
        if (!same_place(ev, pv, c, c.gal.size())) why = "+" + hex(off) + ": " + hex(ev) + " x " + hex(pv);
    }
    // O resto do GHud (o GHud::Reset): igual, menos a tabela virtual.
    for (uint32_t off = 0x2a0; off < 0x500 && why.empty(); off += 4) {
        if (off == 0x4f4) continue;
        const uint32_t ev = e.m.read32(kGHud + off);
        uint32_t pv;
        std::memcpy(&pv, hp + off, 4);
        if (ev != pv) why = "+" + hex(off) + ": " + hex(ev) + " x " + hex(pv);
    }
    // O arquivo da galeria depois do Deserialize e das peças que subiram: as palavras iguais ou o mesmo lugar.
    const uint8_t* pf = drv::hud_gallery_store().file.data();
    size_t diffs = 0;
    for (uint32_t off = 0; off + 4 <= c.gal.size() && why.empty(); off += 4) {
        const uint32_t ev = e.m.read32(kGalFile + off);
        uint32_t pv;
        std::memcpy(&pv, pf + off, 4);
        if (ev == pv) continue;
        if (same_place(ev, pv, c, c.gal.size())) continue;
        ++diffs;
        why = "arquivo +" + hex(off) + ": " + hex(ev) + " x " + hex(pv);
    }
    // As texturas da galeria.
    const drv::GGalleryData& pg = drv::hud_gallery_store().gallery;
    const int32_t n = int32_t(e.m.read32(c.emu_gallery + 0x4c));
    if (why.empty() && n != pg.texture_count) why = "texturas " + std::to_string(n) + " x " + std::to_string(pg.texture_count);
    for (int32_t i = 0; i < n && why.empty(); ++i) {
        const uint32_t tar = e.m.read32(c.emu_gallery + 0x10 + uint32_t(i) * 4);
        if (e.tex_map[tar] != pg.textures[i]) why = "textura " + std::to_string(i);
    }
    std::printf("  GHud::GHud: %s (o hudNTSC.gal, %zu bytes; %d texturas; a fonte, a cortina)\n",
                why.empty() ? "igual" : ("DIFERENTE: " + why).c_str(), c.gal.size(), n);
    if (!why.empty()) ++fail;
    return fail;
}

// Os estados sorteados nos dois lados.
void randomize(HudEmu& e, Ctx& c, Rng& r) {
    drv::GHudData& h = drv::hud();
    auto lab = [&]() {
        if (!c.percent_labels.empty() && r.chance(0.05))
            return c.percent_labels[size_t(r.range(0, int(c.percent_labels.size()) - 1))];
        return c.labels.empty() ? 1 : c.labels[size_t(r.range(0, int(c.labels.size()) - 1))];
    };
    auto fr = [&](float lo, float hi) { return r.chance(0.08) ? 0.0f : r.uniform(lo, hi); };
    h.paused = r.chance(0.1) ? 1 : 0;
    h.letterbox_mode = r.range(0, 3);
    if (r.chance(0.4)) h.letterbox_mode = 1;
    h.letterbox_count = r.range(0, 30);
    h.health_flash = fr(-1, 4);
    h.health_fade = fr(-0.5f, 1);
    h.health_low = fr(-0.5f, 1);
    h.stylin_time = fr(-1, 4);
    h.stylin_state = r.range(0, 2);
    h.stylin_scale = r.uniform(0, 2);
    h.stylin_fade = fr(-1, 2);
    // O texto: o rótulo do +0x2fc (positivo), ou a cadeia do +0x2f8 (aqui nula: os ponteiros não são os mesmos).
    h.button_text = lab();
    if (r.chance(0.85))
        h.button_symbol = (!c.button_labels.empty() && r.chance(0.6))
                              ? c.button_labels[size_t(r.range(0, int(c.button_labels.size()) - 1))]
                              : lab();
    else
        h.button_symbol = 0, h.button_text = 0;
    h.button_time = fr(-1, 3);
    h.button_fade = fr(-0.5f, 1);
    h.objective_frames = r.range(0, 4);
    h.objective_alpha = fr(0, 0.8f);
    h.objective_time = fr(-1, 6);
    h.objective_text = lab();
    h.objective_next = r.chance(0.5) ? -1 : lab();
    h.objective_kind = r.range(-1, 3);
    h.objective_next_kind = r.range(-1, 3);
    h.objective_extra = r.range(0, 3);
    const int32_t rt[4] = {0x18576, 0x18577, -1, lab()};
    h.result_text = rt[r.range(0, 3)];
    h.result_state = r.chance(0.6) ? 0 : r.range(1, 5);
    h.unknown318 = r.range(0, 60);
    h.result_fade = fr(-0.5f, 1);
    h.result_wait = fr(-1, 4);
    h.result_delay = fr(-0.5f, 2);
    h.black_fade = fr(0, 1);
    h.black_fading = r.chance(0.5) ? 1 : 0;
    h.black_fade_from = fr(0, 1);
    h.black_fade_rate = fr(-0.05f, 0.05f);
    drv::simulation().steps = r.range(0, 200000);
    h.black_fade_start = drv::simulation().steps - r.range(0, 200);
    h.message_text = r.chance(0.8) ? lab() : -1;
    h.message_time = fr(-1, 4);
    h.message_fade = fr(-0.5f, 1);
    h.message_long = r.chance(0.15) ? 1 : 0;
    h.timer = r.range(-1, 3);
    h.timer_beep_second = r.range(0, 11);
    h.gadget = uint32_t(r.range(0, 7));
    h.gadget_time = fr(-1, 3);
    h.gadget_slide = fr(-1, 1);
    h.gadget_blink = fr(-1, 1);
    h.gadget_blinks = r.range(-1, 3);
    h.no_health_fade = r.chance(0.3) ? 1 : 0;
    h.hud_off = r.chance(0.1) ? 1 : 0;
    h.aiming = r.chance(0.5) ? 1 : 0;
    h.blood_curtain = fr(-1, 6);
    h.unknown36c = r.uniform(-0.2f, 1.2f);
    h.shield_last = r.uniform(-0.2f, 1.2f);
    h.frame_time = r.chance(0.1) ? 0.0f : r.uniform(0, 0.1f);
    h.frame_steps = uint32_t(r.range(0, 8));
    h.pause_errors = r.chance(0.3) ? 1 : 0;
    uint8_t* hb = reinterpret_cast<uint8_t*>(&h);
    auto w32 = [&](uint32_t off, uint32_t v) { std::memcpy(hb + off, &v, 4); };
    w32(0x2a8, r.chance(0.5) ? 1u : 0u);
    w32(0x328, uint32_t(r.range(-2, 20)));
    w32(0x32c, uint32_t(r.range(-2, 20)));
    w32(0x360, uint32_t(drv::simulation().steps - r.range(0, 8)));
    // A missão, as armas, a simulação.
    drv::SMissionManagerData& mm = drv::mission_manager();
    // (sem o máximo zero: a divisão por zero do EE dá 0x7f7fffff no interpretador e 0x7fffffff no ps2_float)
    mm.player_health_max = r.uniform(500, 4000);
    mm.player_health = r.uniform(-200, mm.player_health_max + 1);
    mm.player_shield_max = r.uniform(100, 2000);
    mm.player_shield = r.uniform(-50, mm.player_shield_max + 1);
    for (int i = 0; i < 4; ++i) {
        drv::SMissionTimer& t = mm.timers[i];
        t.active = r.chance(0.8) ? 1 : 0;
        t.ticking = 0;  // o bipe do SMissionTimer::GetValue (a parte 15 o confere) não é do HUD
        t.start_step = drv::simulation().steps - r.range(0, 20000);
        t.value = r.range(-100, 20000);
        t.rate = r.chance(0.5) ? -1 : r.range(0, 2);
        t.last_second = r.range(0, 300);
    }
    drv::Simulation& s = drv::simulation();
    s.frame_rate = r.chance(0.9) ? 60 : 30;
    s.dt = ps2::ee::div(1.0f, ps2::ee::cvt_s(s.frame_rate));
    drv::SWeaponManagerData& wm = *drv::weapon_manager();
    wm.secondary = r.range(0, 31);
    drv::WeaponEntry* tab = nf::p32_ptr<drv::WeaponEntry>(wm.weapons);
    for (int i = 0; i < 32; ++i) {
        tab[i].name_label = r.chance(0.2) || c.weapon_labels.empty()
                                ? 0x64b
                                : c.weapon_labels[size_t(r.range(0, int(c.weapon_labels.size()) - 1))];
        tab[i].ammo = r.chance(0.3) ? 0 : r.range(-5, 300);
    }
    // O ambiente e os globais.
    drv::HudDrawEnv& env = drv::hud_draw_env();
    env.hud_on = !r.chance(0.08);
    env.fly_by_wire = r.chance(0.08);
    env.renderer_frames = r.chance(0.1) ? r.range(0, 2) : r.range(3, 1000);
    drv::HudDrawGlobals& g = drv::hud_draw_globals();
    g.body_hit = r.range(0, 2);
    g.gadgets_enum = r.range(0, 31);
    g_sf.music_not_found = r.chance(0.3);
    g_sf.music_over = r.chance(0.3);
    g_sf.music_paused = r.chance(0.2);
    g_sf.music_loop = r.chance(0.2);
    g_sf.speech_not_found = r.chance(0.3);
    g_sf.speech_over = r.chance(0.3);
    g_sf.speech_other_mix = r.chance(0.3);
    drv::uber_bond().word(0x5d8) = r.chance(0.2) ? 1 : 0;
    drv::uber_bond().word(0x53c) = r.range(-1, 3);
    // A galeria e a fonte do porte.
    drv::GGalleryData& pg = drv::hud_gallery_store().gallery;
    pg.colour = r.next();
    for (float& v : pg.pos) v = r.uniform(-300, 600);
    for (float& v : pg.quat) v = r.uniform(-1, 1);
    for (float& v : pg.scale) v = r.uniform(0, 2);
    pg.texture = pg.textures[r.range(0, pg.texture_count - 1)];
    pg.custom_state = r.chance(0.2) ? 1 : 0;
    uint8_t* fp = nf::raw(pg.font)->ptr();
    const float fsx = r.uniform(0.5f, 2), fsy = r.uniform(0.5f, 2);
    const uint32_t fcol = r.next();
    std::memcpy(fp + 0x20, &fcol, 4);
    std::memcpy(fp + 0x38, &fsx, 4);
    std::memcpy(fp + 0x3c, &fsy, 4);
    // Os materiais e os lotes.
    drv::draw_power_on();
    drv::DrawGlobals& dg = drv::draw_globals();
    const uint64_t alphas[3] = {0x8000000044ull, 0x8000000048ull, 0x8000000062ull};
    for (drv::HudPrimState* st : {&dg.untextured, &dg.textured}) {
        st->flags = r.chance(0.5) ? 0x40 : 0;
        st->misc = r.range(0, 1);
        st->alpha = alphas[r.range(0, 2)];
    }
    dg.z = r.uniform(0.5f, 12);
    // ---- as miras: a pista, a arma (o tipo, o último tiro, o intervalo), os alvos do WTargetPicker, o zoom, a fração da
    // mira, o RSniperZoom, o GHud da mira
    {
        static const char* const kTracks[4] = {"paris_mis01", "junglea_mis13a", "snow2a_mis4", "rome_mis05"};
        char* tr = drv::uber_bond().text(0x4f4);
        std::memset(tr, 0, 0x20);
        std::strcpy(tr, kTracks[r.range(0, 3)]);
        static const int32_t kTypes[] = {0, 2, 2, 0x15, 0x18, 0x19, 0x1a, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1e, 5};
        drv::WeaponEntry& we = tab[wm.secondary];
        we.type = kTypes[r.range(0, int(sizeof kTypes / sizeof kTypes[0]) - 1)];
        we.last_fire[0] = drv::simulation().steps - r.range(-50, 600);
        we.fire_delay = r.range(1, 600);
        wm.multi_damage = r.range(0, 1);
        hb[0x438] = 0;
        w32(0x438, r.chance(0.2) ? 1u : 0u);
        h.aim_x = r.uniform(-50, 700);
        h.aim_y = r.uniform(-50, 520);
        h.lock = r.chance(0.4) ? 1 : 0;
        h.lock_frames = uint32_t(r.range(0, 9));
        h.lock_frames_seen = uint32_t(r.range(0, 9));
        h.lock_x = r.uniform(-50, 700);
        h.lock_y = r.uniform(-50, 520);
        h.zooming = r.range(0, 1);
        h.zoom_pct = r.chance(0.2) ? 0.0f : r.uniform(0, 1.3f);
        const int n = r.range(0, 6);
        for (int i = 0; i < 16; ++i) {
            uint8_t* t = reinterpret_cast<uint8_t*>(&c.targets[size_t(i)]);
            std::memset(t, 0, sizeof(drv::WTargetableData));
            const float tx = r.uniform(-200, 900), ty = r.uniform(-200, 700);
            std::memcpy(t + 0x10, &tx, 4);
            std::memcpy(t + 0x14, &ty, 4);
            const int32_t kind = r.range(0, 2), on = r.chance(0.7) ? 1 : 0;
            std::memcpy(t + 0x34, &kind, 4);
            std::memcpy(t + 0x44, &on, 4);
            const uint32_t obj = kind == 1 && r.chance(0.8) ? 0x1000u + uint32_t(i) : 0u;
            std::memcpy(t + 0x48, &obj, 4);
            c.emp[i] = r.chance(0.3);
        }
        drv::TargetPickerData& p = drv::target_picker();
        p.count = n;
        for (int i = 0; i < 16; ++i) p.sorted[i] = i < n ? nf::p32_u(&c.targets[size_t(i)]) : 0u;
        p.selected = r.chance(0.7) ? nf::p32_u(&c.targets[size_t(r.range(0, 7))]) : 0u;
        p.lock = r.range(0, 2);
        p.active = r.chance(0.7) ? 1 : 0;
        p.mode = r.range(0, 2);
        p.cursor[0] = r.uniform(-50, 700);
        p.cursor[1] = r.uniform(-50, 500);
        c.zoom_ret = r.uniform(-0.2f, 1.2f);
        c.aim_ret = r.uniform(-0.2f, 1.2f);
        drv::SniperZoomState& sz = drv::sniper_zoom();
        sz.on = r.range(0, 1);
        sz.alpha = r.range(0, 255);
        drv::hud_draw_globals().auto_aim_blink = r.range(0, 31);
        // O original: os alvos (com os donos), o WTargetPicker, a pista, o zoom, o RSniperZoom.
        for (int i = 0; i < 16; ++i) {
            const uint32_t at = kTargets + uint32_t(i) * 0x50;
            std::memcpy(e.m.mem(at, 0x50), &c.targets[size_t(i)], 0x50);
            const uint32_t owner = c.targets[size_t(i)].object != 0 ? kOwners + uint32_t(i) * 0x100 : 0u;
            e.m.write32(at + 0x48, owner);
            if (owner) e.owner_emp[owner] = c.emp[i];
        }
        std::memcpy(e.m.mem(kGPicker, 0xb0), &p, 0xb0);
        for (int i = 0; i < 16; ++i)
            e.m.write32(kGPicker + 0x30 + uint32_t(i) * 4, i < n ? kTargets + uint32_t(i) * 0x50 : 0u);
        uint32_t esel = 0;
        for (int i = 0; i < 16; ++i)
            if (p.selected != 0 && p.selected == nf::p32_u(&c.targets[size_t(i)])) esel = kTargets + uint32_t(i) * 0x50;
        e.m.write32(kGPicker + 0x28, esel);
        e.m.write32(kGPicker + 0x2c, 0);
        std::memcpy(e.m.mem(kUber + 0x4f4, 0x20), tr, 0x20);
        e.zoom_ret = c.zoom_ret;
        e.aim_ret = c.aim_ret;
        e.m.write32(kSniper + 4, uint32_t(sz.on));
        e.m.write32(kSniper + 8, uint32_t(sz.alpha));
        e.m.write32(kGAutoAimBlink, uint32_t(drv::hud_draw_globals().auto_aim_blink));
        e.m.write32(kAudioObj + 0xc0, 0x55);
    }
    // ---- o original
    std::memcpy(e.m.mem(kGHud + 0x2a0, 0x254), hb + 0x2a0, 0x254);  // até o +0x4f4 (a tabela virtual fica)
    std::memcpy(e.m.mem(kMission, sizeof(drv::SMissionManagerData)), &mm, sizeof(drv::SMissionManagerData));
    std::memcpy(e.m.mem(kWeapons, sizeof(drv::SWeaponManagerData)), &wm, sizeof(drv::SWeaponManagerData));
    e.m.write32(kWeapons + 0x10, kWeapons + 0x200);
    std::memcpy(e.m.mem(kWeapons + 0x200, 32 * 0x54), tab, 32 * 0x54);
    e.m.write32(kGSteps, uint32_t(s.steps));
    e.m.write32(kGDt, fbits(s.dt));
    e.m.write32(kGRate, uint32_t(s.frame_rate));
    e.m.write32(kGHudOn, env.hud_on ? 1 : 0);
    e.fly_by_wire = env.fly_by_wire;
    e.m.write32(kRenderer + 0x38, uint32_t(env.renderer_frames));
    e.m.write32(kRenderer + 0x50, 640);
    e.m.write32(kRenderer + 0x54, 448);
    e.m.write32(kRenderer + 0x58, 0);
    e.m.write32(kRenderer + 0x5c, 0);
    e.m.write32(kGBodyHit, uint32_t(g.body_hit));
    e.m.write32(kGGadgetsEnum, uint32_t(g.gadgets_enum));
    e.m.write32(kUber + 0x5d8, uint32_t(drv::uber_bond().word(0x5d8)));
    e.m.write32(kUber + 0x53c, uint32_t(drv::uber_bond().word(0x53c)));
    const uint32_t eg = c.emu_gallery;
    e.m.write32(eg + 0x50, pg.colour);
    std::memcpy(e.m.mem(eg + 0x60, 48), pg.pos, 16);
    std::memcpy(e.m.mem(eg + 0x70, 16), pg.quat, 16);
    std::memcpy(e.m.mem(eg + 0x80, 16), pg.scale, 16);
    for (const auto& kv : e.tex_map)
        if (kv.second == pg.texture) e.m.write32(eg + 0x90, kv.first);
    e.m.write32(eg + 0xb0, uint32_t(pg.custom_state));
    const uint32_t ef = e.m.read32(eg + 0xa8);
    e.m.write32(ef + 0x20, fcol);
    e.m.write32(ef + 0x38, fbits(fsx));
    e.m.write32(ef + 0x3c, fbits(fsy));
    auto mat = [&](uint32_t at, const drv::HudPrimState& st) {
        e.m.write32(at + 0x10, uint32_t(st.flags));
        e.m.write32(at + 0x14, 0);
        e.m.write32(at + 0x30, uint32_t(st.misc));
        e.m.write32(at + 0x34, 0);
        e.m.write32(at + 0x40, uint32_t(st.alpha));
        e.m.write32(at + 0x44, uint32_t(st.alpha >> 32));
    };
    mat(kMatU, dg.untextured);
    mat(kMatT, dg.textured);
    e.m.write32(kGModel, 0);
    e.m.write32(kGZ, fbits(dg.z));
    e.m.write32(kGQuadN, 0);
    e.m.write32(kGQuadTex, 0);
    e.m.write32(kGSprN, 0);
    e.m.write32(kGSprTex, 0);
    e.m.write32(kGHudApp, kGHud);
    // A mixagem da "Speech" (+0x84: a AMix::Get("Speech") ou outra).
    e.m.write32(kStreams + 0x100 + 0x84, g_sf.speech_other_mix ? kStreams + 0x300 : kStreams + 0x200);
    e.m.write32(kStreams + 0x84, kStreams + 0x200);
}

// O GHud, a galeria e a fonte depois (dos dois lados).
std::string compare_state(HudEmu& e, const Ctx& c) {
    const drv::GHudData& h = drv::hud();
    const uint8_t* hb = reinterpret_cast<const uint8_t*>(&h);
    for (uint32_t off = 0x2a0; off < 0x4f4; off += 4) {
        uint32_t pv;
        std::memcpy(&pv, hb + off, 4);
        const uint32_t ev = e.m.read32(kGHud + off);
        if (ev != pv) return "GHud +" + hex(off) + ": " + hex(ev) + " x " + hex(pv);
    }
    const drv::GGalleryData& pg = drv::hud_gallery_store().gallery;
    const uint32_t eg = c.emu_gallery;
    if (e.m.read32(eg + 0x50) != pg.colour) return "galeria cor";
    for (int i = 0; i < 12; ++i) {
        const float* p = i < 4 ? pg.pos + i : (i < 8 ? pg.quat + i - 4 : pg.scale + i - 8);
        if (e.m.read32(eg + 0x60 + uint32_t(i) * 4) != fbits(*p)) return "galeria +" + hex(0x60 + i * 4);
    }
    const uint32_t tar = e.m.read32(eg + 0x90);
    auto it = e.tex_map.find(tar);
    if ((it == e.tex_map.end() ? 0u : it->second) != pg.texture) return "galeria textura";
    const uint32_t ef = e.m.read32(eg + 0xa8);
    const uint8_t* fp = nf::raw(pg.font)->ptr();
    for (uint32_t off : {0x20u, 0x38u, 0x3cu}) {
        uint32_t pv;
        std::memcpy(&pv, fp + off, 4);
        if (e.m.read32(ef + off) != pv) return "fonte +" + hex(off);
    }
    const drv::HudDrawGlobals& g = drv::hud_draw_globals();
    if (int32_t(e.m.read32(kGBodyHit)) != g.body_hit) return "DAT_0033b1c8";
    if (int32_t(e.m.read32(kGGadgetsEnum)) != g.gadgets_enum) return "GadgetsEnum";
    if (int32_t(e.m.read32(kUber + 0x53c)) != drv::uber_bond().word(0x53c)) return "UberBond +0x53c";
    return "";
}

struct Fn {
    const char* name;
    uint32_t addr;
    void (*port)(drv::GHudData&);
    int weight;
    int arg;  // 0 o GHud, 1 o GHud e o puppet +0x288, 2 o WTargetPicker, 3 o GHud e um inteiro, 4 o GHud, um float e um
              // inteiro, 5 o GHud e um float
};

int32_t g_arg_i = 0;
float g_arg_f = 0;
void port_set_auto_drive(drv::GHudData& h) { drv::GHud_SetAutoDrive(h, g_arg_i); }
void port_gadget_on(drv::GHudData& h) { drv::GHud_GadgetOn(h, g_arg_f, uint32_t(g_arg_i)); }
void port_damage_flash(drv::GHudData& h) { drv::GHud_TriggerDamageFlash(h, g_arg_f); }

uint32_t g_port_ret = 0;
void port_render(drv::GHudData& h) { drv::GHud_Render(h, false); }
void port_zooming(drv::GHudData& h) { g_port_ret = drv::GHud_DrawZoomingCrossHairs(h, drv::hud_puppet(h, 0x288 / 4)); }
void port_fading(drv::GHudData& h) { drv::GHud_DrawFadingCrossHairs(h, drv::hud_puppet(h, 0x288 / 4)); }
void port_targeting(drv::GHudData& h) { drv::WTargetPicker_DrawTargetingSystem(h); }

const Fn kFns[] = {
    {"GOrthoHudView::DoRender", kFnDoRender, drv::GOrthoHudView_DoRender, 6},
    {"GHud::Draw", kFnDraw, drv::GHud_Draw, 3},
    {"GHud::Render", kFnRender, port_render, 3},
    {"GHud::DrawObjectiveMessage", kFnObjective, drv::GHud_DrawObjectiveMessage, 3},
    {"GHud::DrawMessage", kFnMessage, drv::GHud_DrawMessage, 2},
    {"GHud::DrawButtonMessage", kFnButton, drv::GHud_DrawButtonMessage, 3},
    {"GHud::DrawTimer", kFnTimer, drv::GHud_DrawTimer, 2},
    {"GHud::DrawHealth", kFnHealth, drv::GHud_DrawHealth, 3},
    {"GHud::DrawWeapon", kFnWeapon, drv::GHud_DrawWeapon, 2},
    {"GHud::DrawGadget", kFnGadget, drv::GHud_DrawGadget, 2},
    {"GHud::DrawStylin", kFnStylin, drv::GHud_DrawStylin, 2},
    {"GHud::DrawMissionResult", kFnResult, drv::GHud_DrawMissionResult, 3},
    {"GHud::DrawFlyByWire", kFnFlyByWire, drv::GHud_DrawFlyByWire, 1},
    {"GHud::DrawLetterBox", kFnLetterBox, drv::GHud_DrawLetterBox, 1},
    {"GHud::DrawScreenFades", kFnFades, drv::GHud_DrawScreenFades, 1},
    {"GHud::DrawScreenHits", kFnHits, drv::GHud_DrawScreenHits, 1},
    {"GHud::DrawBloodCurtain", kFnBlood, drv::GHud_DrawBloodCurtain, 1},
    {"GHud::DrawCrossHairs", kFnCrossHairs, drv::GHud_DrawCrossHairs, 8},
    {"GHud::DrawZoomingCrossHairs", kFnZooming, port_zooming, 2, 1},
    {"GHud::DrawFadingCrossHairs", kFnFading, port_fading, 2, 1},
    {"WTargetPicker::DrawTargetingSystem", kFnTargeting, port_targeting, 2, 2},
    {"GHud::AimOn", kFnAimOn, drv::GHud_AimOn, 1},
    {"GHud::SetAutoDrive", 0x241040, port_set_auto_drive, 1, 3},
    {"GHud::GadgetOn", 0x244f48, port_gadget_on, 1, 4},
    {"GHud::Stylin", 0x23c820, drv::GHud_Stylin, 1},
    {"GHud::TriggerDamageFlash", 0x23e270, port_damage_flash, 1, 5},
};

// RPlayerCamera::GetZoomPercent com tabelas de modos e de braços sorteadas (o gancho do resto da conferência sai).
int check_zoom_percent(HudEmu& e, int cases, uint32_t seed) {
    Rng r(seed ^ 0x5a4f4f4du);
    e.m.unhook(kFnZoomPercent);
    e.m.write32(kGModeTab, kModeTab);
    e.m.write32(kGArmTab, kArmTab);
    int fails = 0, zoomable = 0;
    std::string why;
    for (int i = 0; i < cases; ++i) {
        for (int k = 0; k < 8; ++k) {
            const uint32_t at = kModeTab + uint32_t(k) * 0x1c;
            for (uint32_t o = 0; o < 0x1c; o += 4) e.m.write32(at + o, r.next());
            const uint16_t type = r.chance(0.6) ? 0x200 : uint16_t(r.next());
            std::memcpy(e.m.mem(at + 0xa, 2), &type, 2);
            e.m.write32(at + 0xc, fbits(r.uniform(20, 90)));
        }
        for (int k = 0; k < 4; ++k) {
            const uint32_t at = kArmTab + uint32_t(k) * 0x100;
            for (uint32_t o = 0; o < 0x100; o += 4) e.m.write32(at + o, r.next());
            e.m.write32(at + 0x5c, fbits(r.uniform(10, 95)));
        }
        const int32_t mode = r.range(0, 7);
        const int8_t arm = int8_t(r.range(0, 3));
        const float fov = r.uniform(5, 100);
        e.m.write32(kCam + 0x120, uint32_t(mode));
        std::memcpy(e.m.mem(kCam + 0x27f, 1), &arm, 1);
        e.m.write32(kCam + 0xa4, fbits(fov));
        e.call(kFnZoomPercent, {kCam});
        const uint32_t ev = e.m.fpr_bits(0);
        drv::CameraModeInfo mi;
        std::memcpy(&mi, e.m.mem(kModeTab + uint32_t(mode) * 0x1c, 0x1c), 0x1c);
        std::vector<drv::AutoDriveArmInfo> arms(4);
        std::memcpy(arms.data(), e.m.mem(kArmTab, 0x400), 0x400);
        const float pv = drv::RPlayerCamera_GetZoomPercent(mi, arms.data(), arm, fov);
        if (mi.type == 0x200) ++zoomable;
        if (ev != fbits(pv)) {
            ++fails;
            if (why.empty()) why = "caso " + std::to_string(i) + ": " + hex(ev) + " x " + hex(fbits(pv));
        }
    }
    e.m.hook(kFnZoomPercent, "RPlayerCamera::GetZoomPercent", [&e](MipsMachine& mm) { mm.set_fpr(0, e.zoom_ret); });
    std::printf("  RPlayerCamera::GetZoomPercent: %d casos (%d no modo 0x200), %d diferentes%s%s\n", cases, zoomable, fails,
                why.empty() ? "" : " | ", why.c_str());
    return fails;
}

int check_draws(HudEmu& e, Ctx& c, int cases, uint32_t seed) {
    Rng r(seed ^ 0x48554421u);
    int total_w = 0;
    for (const Fn& f : kFns) total_w += f.weight;
    std::map<std::string, std::pair<int, int>> tally;  // casos, diferentes
    std::map<std::string, std::string> first_of;
    uint64_t mats = 0, packets = 0, sounds = 0, words = 0;
    int fails = 0;
    std::string first_why;
    port_stream_env();
    {
        drv::HudDrawEnv& env = drv::hud_draw_env();
        env.zoom_percent = [&c] { return c.zoom_ret; };
        env.aim_fraction = [&c] { return c.aim_ret; };
        env.pause_on = [](drv::GHudData&) { ++drv::hud_draw_stats().pause_menu; };  // (o original: enganchado)
        env.pause_draw = [](drv::GHudData&, bool) {};  // (o original: enganchados; conferidos na parte 27)
        env.only_emp = [&c](uint32_t t) {
            for (int i = 0; i < 16; ++i)
                if (nf::p32_u(&c.targets[size_t(i)]) == t) return c.emp[i];
            return false;
        };
    }
    for (int i = 0; i < cases; ++i) {
        int pick = r.range(0, total_w - 1);
        const Fn* fn = &kFns[0];
        for (const Fn& f : kFns) {
            if (pick < f.weight) {
                fn = &f;
                break;
            }
            pick -= f.weight;
        }
        randomize(e, c, r);
        e.begin_case();
        drv::hud_frame().clear();
        drv::hud_draw_stats().sounds.clear();
        g_sf.music_stops = 0;
        std::string why;
        try {
            g_arg_i = r.range(-1, 7);
            g_arg_f = r.chance(0.1) ? 0.0f : r.uniform(-3, 30);
            e.m.set_fpr(12, g_arg_f);
            if (fn->arg == 3 || fn->arg == 4)
                e.call(fn->addr, {kGHud, uint32_t(g_arg_i)});
            else if (fn->arg == 1)
                e.call(fn->addr, {kGHud, e.m.read32(kGHud + 0x288)});
            else if (fn->arg == 2)
                e.call(fn->addr, {kGPicker});
            else
                e.call(fn->addr, {kGHud});
        } catch (const std::exception& ex) {
            why = std::string("o original parou: ") + ex.what();
        }
        e.flush_packet();
        const int emu_stops = g_sf.music_stops;
        g_sf.music_stops = 0;
        drv::hud_draw_stats().car_calls.clear();
        const uint32_t emu_ret = uint32_t(e.m.gpr(R::v0));
        g_port_ret = 0;
        fn->port(drv::hud());
        if (why.empty() && fn->addr == kFnZooming && emu_ret != g_port_ret)
            why = "a cor devolvida " + hex(emu_ret) + " x " + hex(g_port_ret);
        if (why.empty()) why = compare_events(e.events, port_events());
        if (why.empty()) {
            const auto& ps = drv::hud_draw_stats().sounds;
            if (ps.size() != e.sounds.size()) why = "sons " + std::to_string(e.sounds.size()) + " x " + std::to_string(ps.size());
            for (size_t k = 0; k < ps.size() && why.empty(); ++k)
                if (ps[k].bank != e.sounds[k].bank || ps[k].name != e.sounds[k].name || ps[k].mix != e.sounds[k].mix ||
                    ps[k].arg != e.sounds[k].arg)
                    why = "som " + std::to_string(k) + ": " + e.sounds[k].name + " x " + ps[k].name;
        }
        if (why.empty() && emu_stops != g_sf.music_stops) why = "AStream::Stop da música";
        if (why.empty() && fn->addr == kFnDoRender) {
            // A matriz da vista: a identidade x a escala x o deslocamento.
            const drv::GHudData& h = drv::hud();
            const uint32_t want[16] = {fbits(h.view_sx), 0, 0, 0, 0, fbits(h.view_sy), 0, 0,
                                       0, 0, 0x3f800000u, 0, fbits(h.view_tx), fbits(h.view_ty), 0, 0x3f800000u};
            if (e.view_calls != 1) why = "SetViewMatrix " + std::to_string(e.view_calls);
            for (int k = 0; k < 16 && why.empty(); ++k)
                if (e.view_m[k] != want[k]) why = "matriz da vista " + std::to_string(k) + " " + hex(e.view_m[k]) + " x " + hex(want[k]);
        }
        if (why.empty()) why = compare_state(e, c);
        if (why.empty()) {
            // O som da arma do carro: o +0xc0 (o último valor), o InTheZone e a carga na ordem.
            std::vector<std::pair<int32_t, float>> pc;
            float last1 = -1.0f;
            for (const auto& cc : drv::hud_draw_stats().car_calls) {
                if (cc.first == 1)
                    last1 = cc.second;
                else
                    pc.push_back(cc);
            }
            const uint32_t ec0 = e.m.read32(kAudioObj + 0xc0);
            if ((last1 < 0 && ec0 != 0x55) || (last1 >= 0 && ec0 != uint32_t(last1)))
                why = "o +0xc0 do som da arma " + hex(ec0);
            if (why.empty() && pc.size() != e.car_calls.size()) why = "chamadas ao som da arma";
            for (size_t k = 0; k < pc.size() && why.empty(); ++k)
                if (pc[k].first != e.car_calls[k].first || fbits(pc[k].second) != fbits(e.car_calls[k].second))
                    why = "chamada " + std::to_string(k) + " ao som da arma";
            const drv::SniperZoomState& sz = drv::sniper_zoom();
            if (why.empty() && (int32_t(e.m.read32(kSniper + 4)) != sz.on || int32_t(e.m.read32(kSniper + 8)) != sz.alpha))
                why = "RSniperZoom";
            if (why.empty() && int32_t(e.m.read32(kGAutoAimBlink)) != drv::hud_draw_globals().auto_aim_blink)
                why = "DAT_0033b1d0";
        }
        for (const Ev& ev : e.events) {
            if (ev.packet) {
                ++packets;
                words += ev.words.size();
            } else {
                ++mats;
            }
        }
        sounds += e.sounds.size();
        auto& t = tally[fn->name];
        ++t.first;
        if (!why.empty()) {
            ++t.second;
            ++fails;
            if (first_why.empty()) first_why = std::string(fn->name) + " caso " + std::to_string(i) + ": " + why;
            if (first_of[fn->name].empty()) first_of[fn->name] = "caso " + std::to_string(i) + ": " + why;
            if (std::getenv("NF_HUD_DUMP")) {  // depuração: as duas listas do caso
                const std::vector<Ev> pe = port_events();
                std::printf("  -- %s caso %d: original %zu itens, porte %zu\n", fn->name, i, e.events.size(), pe.size());
                {
                    const drv::TargetPickerData& tp = drv::target_picker();
                    const drv::SWeaponManagerData& w = *drv::weapon_manager();
                    const drv::GHudData& hh = drv::hud();
                    std::printf("     arma %d pista %s alvos %d escolhido %08x trava %d zoom2a8 %u 438 %d mirando %d |",
                                nf::p32_ptr<drv::WeaponEntry>(w.weapons)[w.secondary].type, drv::uber_bond().text(0x4f4),
                                tp.count, tp.selected, hh.lock, uint32_t(reinterpret_cast<const uint8_t*>(&hh)[0x2a8]),
                                hh.unknown438, hh.aiming);
                    for (int k = 0; k < tp.count; ++k) {
                        const drv::WTargetableData& td = c.targets[size_t(k)];
                        std::printf(" [%08x tipo %d tela %d obj %x emp %d]", nf::p32_u(&td), td.kind, td.unknown44,
                                    td.object, int(c.emp[k]));
                    }
                    std::printf("\n");
                }
                for (size_t k = 0; k < std::max(pe.size(), e.events.size()); ++k)
                    std::printf("     %2zu %-40s | %s\n", k, k < e.events.size() ? describe(e.events[k]).c_str() : "-",
                                k < pe.size() ? describe(pe[k]).c_str() : "-");
            }
        }
    }
    for (const auto& kv : tally)
        std::printf("  %-28s %5d casos, %d diferentes%s%s\n", kv.first.c_str(), kv.second.first, kv.second.second,
                    first_of[kv.first].empty() ? "" : " | ", first_of[kv.first].c_str());
    std::printf("  desenhos: %" PRIu64 " materiais, %" PRIu64 " pacotes de fonte (%" PRIu64 " palavras), %" PRIu64
                " sons\n",
                mats, packets, words, sounds);
    if (!first_why.empty()) std::printf("  primeira diferença: %s\n", first_why.c_str());
    std::printf("  HUD: %d casos, %d diferentes\n", cases, fails);
    return fails;
}

// ---------------------------------------------------------------- a pausa (parte 27, seção 31)

// A RAM emulada da pausa.
constexpr uint32_t kPQueue = 0x004d0000;     // a fila "HUDQ" (0x974)
constexpr uint32_t kPObjList = 0x004d1000;   // a lista dos objetivos (o nó cabeça e os nós de 12 bytes)
constexpr uint32_t kPObjData = 0x004d2000;   // os SMissionObjective (0x14 cada)
constexpr uint32_t kPSettings = 0x004d3000;  // o MissionSettings (0x2f8)
constexpr uint32_t kPIcmCfg = 0x004d4000;    // as ConfigInfo das duas listas (0x5c cada; tipo 1 em +0x400)
constexpr uint32_t kPWorld = 0x004d5000;     // o AttributeSet do mundo (+0xc o nome) e o nome em +0x100
constexpr uint32_t kPSched = 0x004d5400;     // o Scheduler (+0x24 o timeScale)
constexpr uint32_t kPEvents = 0x004d6000;    // o anel dos eventos (Event::operator new)
constexpr uint32_t kPWrap = 0x004d7000;      // o texto do WordWrap e o ponteiro das linhas
constexpr uint32_t kPUberCopy = 0x004d8000;  // o UberBond do TallyScores (0xa50)
constexpr uint32_t kPGirlMat = 0x004d9000, kPGirlTar = 0x004d9200;  // os GGirlieMaterial e os TAR da moça
constexpr uint32_t kPGirlShape = 0x00540000;  // as duas texturas da moça (0x10 + 128 x 128 x 4 cada)
constexpr uint32_t kGIcm = 0x3c8780, kGIcmInit = 0x32e5b8, kGQueue = 0x33ab68, kGWorld = 0x33aac8;
constexpr uint32_t kGSchedPtr = 0x32f2b0, kGEventRing = 0x32e6fc, kGRunning = 0x32c460, kGSim = 0x339190;
constexpr uint32_t kGGirlBase = 0x33aae8, kGGirlMats = 0x3c8370;

struct PauseCtx {
    int32_t tick = 1000;
    bool multitap = false;
    int32_t video_mode = 1;
    int32_t accumulate = 0;
    std::vector<std::string> events;     // os construtores de evento do original, na ordem
    std::vector<std::string> exits;      // Bond_SetExitToDriving do original
    std::vector<uint32_t> set_config;    // InputConfigManager::SetConfig(char *) do original
    std::vector<drv::SMissionObjective> objectives;
    drv::InputConfigManager icm;
    float port_time_scale = 1.0f;
    std::vector<std::string> port_exits;
    uint32_t port_file = 0, emu_file = 0;  // os começos do hudNTSC.gal nos dois lados (o 'fi' da moça)
};
PauseCtx g_pc;

void install_pause_hooks(HudEmu& e) {
    MipsMachine& m = e.m;
    auto ret = [](uint64_t v) { return [v](MipsMachine& mm) { mm.set_gpr(R::v0, v); }; };
    m.unhook(0x002426a0);  // DrawPauseMenu
    m.unhook(0x00241f40);  // DrawPauseErrors
    m.unhook(0x00241048);  // PauseOn
    m.unhook(0x001654c8);  // ActionQueue::IsEmpty
    m.unhook(0x00249f98);  // GLocale::GetLanguage (o UberBond +0x988)
    m.hook(0x00261038, "TIMER_gettick", [](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(g_pc.tick)); });
    m.hook(0x00167d80, "IOModule::GetIOModule", ret(0x00424900));
    m.hook(0x00168038, "IOModule::MultitapConnected", [](MipsMachine& mm) { mm.set_gpr(R::v0, g_pc.multitap ? 1 : 0); });
    m.hook(0x002d6b80, "GetFoundationVideoMode", [](MipsMachine& mm) { mm.set_gpr(R::v0, uint32_t(g_pc.video_mode)); });
    m.hook(0x00153470, "EPause::EPause", [](MipsMachine& mm) {
        g_pc.events.push_back("EPause");
        mm.write32(uint32_t(mm.gpr(R::a0)), 0x00370be8);
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    m.hook(0x001568d8, "EResume::EResume", [](MipsMachine& mm) {
        g_pc.events.push_back("EResume");
        mm.write32(uint32_t(mm.gpr(R::a0)), 0x00370900);
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    m.hook(0x00166f28, "InputConfigManager::SetConfig(char *)", [](MipsMachine& mm) {
        g_pc.set_config.push_back(uint32_t(mm.gpr(R::a1)));
    });
    m.hook(0x00169bf0, "AttributeSet::LookupInt", [](MipsMachine& mm) {
        const std::string n = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        mm.set_gpr(R::v0, n == "ACCUMULATE_SCORES" ? uint32_t(g_pc.accumulate) : 0u);
    });
    m.hook(0x00169f18, "AttributeSet::LookupString", ret(0));
    m.hook(0x00174b80, "Bond_SetExitToDriving", [](MipsMachine& mm) {
        g_pc.exits.push_back(std::to_string(int32_t(mm.gpr(R::a0))) + " " +
                             reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1)));
    });
    m.hook(0x0023a618, "GGirlieMaterial::~GGirlieMaterial", ret(0));
    // A moça: os dois GGirlieMaterial (0xd0), os seus EAGL::TAR (0x60) e as texturas 128 x 128 em lugares fixos,
    // alternados (o InitGirl de novo depois de um KillGirl não gasta o heap emulado).
    static int mat_slot = 0, tar_slot = 0, shape_slot = 0;
    m.hook(0x002d0910, "__builtin_new", [&e](MipsMachine& mm) {
        const uint32_t n = uint32_t(mm.gpr(R::a0));
        mm.set_gpr(R::v0, n == 0xd0 ? kPGirlMat + uint32_t(mat_slot++ & 1) * 0x100 : e.alloc(n));
    });
    m.hook(0x002871f0, "eagl_alloc", [&e](MipsMachine& mm) {
        const uint32_t n = uint32_t(mm.gpr(R::a0));
        mm.set_gpr(R::v0, n == 0x60 ? kPGirlTar + uint32_t(tar_slot++ & 1) * 0x100 : e.alloc(n));
    });
    m.hook(0x0025fdf0, "SHAPE_create", [](MipsMachine& mm) {
        const uint32_t w = uint32_t(mm.gpr(R::a0)), h = uint32_t(mm.gpr(R::a1));
        const uint32_t at = kPGirlShape + uint32_t(shape_slot++ & 1) * 0x10100;
        std::memset(mm.mem(at, 0x10 + w * h * 4), 0, 0x10 + w * h * 4);
        const uint16_t ww = uint16_t(w), hh = uint16_t(h);
        std::memcpy(mm.mem(at + 4, 2), &ww, 2);
        std::memcpy(mm.mem(at + 6, 2), &hh, 2);
        mm.set_gpr(R::v0, at);
    });
    m.hook(0x00260010, "SHAPE_rowbytes", [](MipsMachine& mm) {
        int16_t w;
        std::memcpy(&w, mm.mem(uint32_t(mm.gpr(R::a0)) + 4, 2), 2);
        mm.set_gpr(R::v0, uint32_t(int32_t(w) * 4));
    });
    m.write32(kGIcmInit, 1);
    m.write32(kGQueue, kPQueue);
    m.write32(kGWorld, kPWorld);
    m.write32(kPWorld + 0xc, kPWorld + 0x100);
    m.write32(kGSchedPtr, kPSched);
}

// O porte -> o original: a fila, a missão (os objetivos numa lista do PS2, as configurações), o InputConfigManager, o
// UberBond, a simulação, o mundo, a moça.
void pause_sync(HudEmu& e) {
    MipsMachine& m = e.m;
    std::memcpy(m.mem(kGHud + 0x2a0, 0x254), reinterpret_cast<const uint8_t*>(&drv::hud()) + 0x2a0, 0x254);
    std::memcpy(m.mem(kUber, 0xa50), drv::uber_bond().bytes, 0xa50);
    std::memcpy(m.mem(kPWorld + 0x100, 0x20), drv::uber_bond().text(0x4f4), 0x20);
    // A fila.
    std::memcpy(m.mem(kPQueue, 0x974), &drv::hud_queue(), 0x974);
    m.write32(kPQueue, 0);
    // A missão.
    const drv::SMissionManagerData& mm = drv::mission_manager();
    std::memcpy(m.mem(kMission, sizeof mm), &mm, sizeof mm);
    m.write32(kMission + 0, kPObjList);
    m.write32(kMission + 0x1c, kPSettings);
    std::memcpy(m.mem(kPSettings, 0x2f8), nf::p32_ptr<const uint8_t>(mm.settings), 0x2f8);
    const auto& objs = drv::mission_lists().objectives;
    uint32_t prev = kPObjList;
    for (size_t i = 0; i < objs.size(); ++i) {
        const uint32_t node = kPObjList + 0x10 + uint32_t(i) * 0x10;
        const uint32_t data = kPObjData + uint32_t(i) * 0x20;
        std::memcpy(m.mem(data, 0x14), objs[i], 0x14);
        m.write32(prev, node);
        m.write32(node + 4, prev);
        m.write32(node + 8, data);
        prev = node;
    }
    m.write32(prev, kPObjList);
    m.write32(kPObjList + 4, prev);
    // O InputConfigManager.
    const drv::InputConfigManager& icm = g_pc.icm;
    std::memcpy(m.mem(kGIcm, 0x30), &icm.d, 0x30);
    for (int t = 0; t < 2; ++t) {
        const uint32_t base = kPIcmCfg + uint32_t(t) * 0x400;
        m.write32(kGIcm + 0x18 + uint32_t(t) * 4, base);
        for (size_t i = 0; i < icm.configs[t].size(); ++i)
            std::memcpy(m.mem(base + uint32_t(i) * 0x5c, 0x5c), &icm.configs[t][i], 0x5c);
    }
    m.write32(kGIcm + 0x24, 0);
    m.write32(kGIcm + 0x28, 0);
    // A simulação.
    const drv::Simulation& s = drv::simulation();
    m.write32(kGSim + 0xdb0, uint32_t(s.prev_state));
    m.write32(kGSim + 0xdb4, uint32_t(s.state));
    m.write32(kGSteps, uint32_t(s.steps));
    m.write32(kGDt, fbits(s.dt));
    m.write32(kGRate, uint32_t(s.frame_rate));
    m.write32(kGRunning, uint32_t(drv::hud_game_running()));
    m.write32(kPSched + 0x24, fbits(g_pc.port_time_scale));
    m.write32(kGEventRing, kPEvents);
    // A moça.
    const drv::GGirlState& gs = drv::ggirl();
    const uint32_t edata = gs.data ? g_pc.emu_file + (nf::p32_u(gs.data) - g_pc.port_file) : 0u;
    m.write32(0x33aaec, edata);
    m.write32(0x33aaf4, uint32_t(gs.init));
    m.write32(0x33aaf8, uint32_t(gs.unknown_aaf8));
    m.write32(0x33aafc, uint32_t(gs.unknown_aafc));
    m.write32(0x33ab00, uint32_t(gs.unknown_ab00));
    m.write32(0x33ab04, uint32_t(gs.fps));
    const float fl[8] = {gs.scroll_x, gs.scroll_y, gs.speed_x, gs.speed_y, gs.offset_x, gs.offset_y, gs.unknown_ab20,
                         gs.unknown_ab24};
    for (int i = 0; i < 8; ++i) m.write32(0x33ab08 + uint32_t(i) * 4, fbits(fl[i]));
    m.write32(0x33ab28, gs.frames ? g_pc.emu_file + (nf::p32_u(gs.frames) - g_pc.port_file) : 0u);
    std::memcpy(m.mem(0x33ab30, 8), &gs.saved, 8);
    std::memcpy(m.mem(0x33ab38, 8), &gs.cursor, 8);
    m.write32(0x33ab40, uint32_t(gs.last_tick));
    m.write32(0x33ab44, fbits(gs.warmup));
    m.write32(0x33ab48, uint32_t(gs.frame_count));
    m.write32(0x33ab4c, uint32_t(gs.frame));
    m.write32(0x33ab50, uint32_t(gs.normal));
    m.write32(kGGirlBase, uint32_t(gs.buffer));
    if (gs.init != 0) {
        for (int b = 0; b < 2; ++b) {
            const uint32_t mat = m.read32(kGGirlMats + uint32_t(b) * 4);
            const uint32_t shape = mat ? m.read32(mat) : 0u;
            if (shape && gs.pixels[b].size() == 128 * 128)
                std::memcpy(m.mem(shape + 0x10, 128 * 128 * 4), gs.pixels[b].data(), 128 * 128 * 4);
        }
    }
}

// O original -> o porte nada; as diferenças depois de uma chamada (o resto, além do GHud, da galeria e dos desenhos).
std::string pause_compare(HudEmu& e) {
    MipsMachine& m = e.m;
    for (uint32_t off = 0; off < 0xa50; off += 4) {
        uint32_t pv;
        std::memcpy(&pv, drv::uber_bond().bytes + off, 4);
        if (m.read32(kUber + off) != pv) return "UberBond +" + hex(off) + ": " + hex(m.read32(kUber + off)) + " x " + hex(pv);
    }
    const drv::SMissionManagerData& mm = drv::mission_manager();
    const uint8_t* mb = reinterpret_cast<const uint8_t*>(&mm);
    for (uint32_t off = 0x24c; off < 0x1b40; off += 4) {
        if (off == 0x358 || off == 0x36c) continue;  // os ponteiros
        uint32_t pv;
        std::memcpy(&pv, mb + off, 4);
        if (m.read32(kMission + off) != pv) return "missão +" + hex(off) + ": " + hex(m.read32(kMission + off)) + " x " + hex(pv);
    }
    const auto& objs = drv::mission_lists().objectives;
    for (size_t i = 0; i < objs.size(); ++i)
        if (std::memcmp(m.mem(kPObjData + uint32_t(i) * 0x20, 0x14), objs[i], 0x14) != 0) return "objetivo " + std::to_string(i);
    const drv::Simulation& s = drv::simulation();
    if (int32_t(m.read32(kGSim + 0xdb4)) != s.state || int32_t(m.read32(kGSim + 0xdb0)) != s.prev_state)
        return "Simulation +0xdb0/+0xdb4";
    if (int32_t(m.read32(kGRunning)) != drv::hud_game_running()) return "DAT_0032c460";
    if (m.read32(kPSched + 0x24) != fbits(g_pc.port_time_scale)) return "Scheduler +0x24";
    for (uint32_t off : {0x0u, 0x4u, 0x8u, 0xcu, 0x10u, 0x14u, 0x20u}) {
        uint32_t pv;
        std::memcpy(&pv, reinterpret_cast<const uint8_t*>(&g_pc.icm.d) + off, 4);
        if (m.read32(kGIcm + off) != pv) return "InputConfigManager +" + hex(off);
    }
    // A fila.
    const drv::ActionQueueData& q = drv::hud_queue();
    for (uint32_t off = 4; off < 0x14; off += 4) {
        uint32_t pv;
        std::memcpy(&pv, reinterpret_cast<const uint8_t*>(&q) + off, 4);
        if (m.read32(kPQueue + off) != pv) return "fila +" + hex(off);
    }
    // Os eventos (os do porte na fila dos eventos).
    std::vector<std::string> pe;
    for (const drv::PendingEvent& ev : drv::trigger_manager().pending) {
        if (ev.type == drv::kEventPausePort) pe.push_back("EPause");
        else if (ev.type == drv::kEventResumePort) pe.push_back("EResume");
        else pe.push_back("evento " + std::to_string(ev.type));
    }
    if (pe != g_pc.events) return "eventos " + std::to_string(g_pc.events.size()) + " x " + std::to_string(pe.size());
    if (g_pc.port_exits != g_pc.exits) return "Bond_SetExitToDriving";
    // A moça.
    const drv::GGirlState& gs = drv::ggirl();
    if (int32_t(m.read32(0x33aaf4)) != gs.init) return "GGirl aaf4";
    if (gs.init != 0) {
        const uint32_t edata = gs.data ? g_pc.emu_file + (nf::p32_u(gs.data) - g_pc.port_file) : 0u;
        if (m.read32(0x33aaec) != edata) return "GGirl aaec";
        const float fl[8] = {gs.scroll_x, gs.scroll_y, gs.speed_x, gs.speed_y, gs.offset_x, gs.offset_y, gs.unknown_ab20,
                             gs.unknown_ab24};
        for (int i = 0; i < 8; ++i)
            if (m.read32(0x33ab08 + uint32_t(i) * 4) != fbits(fl[i])) return "GGirl +" + hex(0x33ab08 + i * 4);
        if (m.read32(0x33ab04) != uint32_t(gs.fps)) return "GGirl fps";
        if (m.read32(0x33ab28) != (gs.frames ? g_pc.emu_file + (nf::p32_u(gs.frames) - g_pc.port_file) : 0u))
            return "GGirl ab28";
        int64_t ev;
        std::memcpy(&ev, m.mem(0x33ab30, 8), 8);
        if (ev != gs.saved) return "GGirl ab30 " + std::to_string(ev) + " x " + std::to_string(gs.saved);
        std::memcpy(&ev, m.mem(0x33ab38, 8), 8);
        if (ev != gs.cursor) return "GGirl ab38 " + std::to_string(ev) + " x " + std::to_string(gs.cursor);
        if (int32_t(m.read32(0x33ab40)) != gs.last_tick) return "GGirl ab40";
        if (m.read32(0x33ab44) != fbits(gs.warmup)) return "GGirl ab44";
        if (int32_t(m.read32(0x33ab48)) != gs.frame_count) return "GGirl ab48";
        if (int32_t(m.read32(0x33ab4c)) != gs.frame) return "GGirl ab4c";
        if (int32_t(m.read32(kGGirlBase)) != gs.buffer) return "GGirl aae8";
        for (int b = 0; b < 2; ++b) {
            const uint32_t mat = m.read32(kGGirlMats + uint32_t(b) * 4);
            const uint32_t shape = mat ? m.read32(mat) : 0u;
            if (!shape || gs.pixels[b].size() != 128 * 128) return "GGirl textura " + std::to_string(b);
            const uint8_t* ep = m.mem(shape + 0x10, 128 * 128 * 4);
            for (uint32_t i = 0; i < 128 * 128; ++i) {
                uint32_t v;
                std::memcpy(&v, ep + i * 4, 4);
                if (v != gs.pixels[b][i])
                    return "GGirl textura " + std::to_string(b) + " pixel " + std::to_string(i) + " " + hex(v) + " x " +
                           hex(gs.pixels[b][i]);
            }
        }
    }
    return "";
}

// Os estados sorteados da pausa (depois do randomize da parte 24).
void pause_randomize(HudEmu& e, Ctx& c, Rng& r) {
    drv::GHudData& h = drv::hud();
    uint8_t* hb = reinterpret_cast<uint8_t*>(&h);
    auto w32 = [&](uint32_t off, int32_t v) { std::memcpy(hb + off, &v, 4); };
    auto wf = [&](uint32_t off, float v) { std::memcpy(hb + off, &v, 4); };
    auto lab = [&]() { return c.labels.empty() ? 1 : c.labels[size_t(r.range(0, int(c.labels.size()) - 1))]; };
    h.paused = r.chance(0.6) ? 1 : 0;
    const int32_t pages[] = {0, 0, 1, 1, 2, 2, 3, 3, 5, 5, 4, -1, 6};
    w32(0x480, pages[r.range(0, int(sizeof pages / sizeof pages[0]) - 1)]);
    w32(0x484, r.range(-1, 3));
    w32(0x488, r.range(-1, 5));
    w32(0x494, r.range(0, 3) == 3 ? r.range(0, 3) : r.range(0, 2));
    w32(0x498, r.chance(0.7) ? r.range(0, 1) : r.range(0, 2));
    wf(0x2b8, r.uniform(-0.2f, 1.2f));
    wf(0x2bc, float(r.range(0, 3000)));
    wf(0x2c0, r.uniform(-1, 2));
    w32(0x2c4, r.range(0, 3));
    w32(0x2a4, r.range(0, 1));
    w32(0x2a8, r.chance(0.5) ? 1 : 0);
    w32(0x444, r.range(-1, 3));
    h.hud_off = r.chance(0.2) ? 1 : 0;
    const int32_t rt[3] = {0x18576, 0x18577, -1};
    h.result_text = rt[r.range(0, 2)];
    for (uint32_t o = 0x4ac; o <= 0x4e8; o += 4) w32(o, r.chance(0.8) ? r.range(0x59, 0x62) : r.range(0, 0x70));
    if (r.chance(0.3)) {  // uma das sequências no fim do registro
        int32_t n = 0;
        const drv::CheatSequence* cs = drv::hud_cheat_sequences(&n);
        const drv::CheatSequence& s = cs[r.range(0, n - 1)];
        const int32_t k = s.words[0] - (r.chance(0.5) ? 1 : 0);
        for (int32_t i = 0; i < k; ++i) w32(uint32_t(0x4ac + (0x10 - k + i) * 4), s.words[1 + i]);
    }
    h.restart = r.range(0, 1);
    h.exit = r.range(0, 1);
    // O UberBond, a missão, a simulação.
    drv::UberBondBlock& u = drv::uber_bond();
    u.word(0x978) = r.range(0, 5);
    u.word(0x9a0) = int32_t(r.next());
    u.word(0x9a4) = int32_t(r.next() & 0xf);
    u.word(0x988) = r.chance(0.7) ? 0 : r.range(-1, 9);
    u.word(0xa3c) = r.range(0, 4);
    u.word(0xa24) = r.range(0, 1);
    u.word(0x974) = r.range(0, 12);
    if (r.chance(0.15)) {
        char* tr = u.text(0x4f4);
        std::memset(tr, 0, 0x20);
        std::strcpy(tr, r.chance(0.5) ? "snow2a_mis4" : "PARIS_mis01");
    }
    for (uint32_t o = 0x504; o < 0x640; o += 4) u.word(o) = r.chance(0.3) ? r.range(-5, 3000) : r.range(0, 9);
    drv::SMissionManagerData& mm = drv::mission_manager();
    mm.state = r.range(0, 4);
    mm.cinematic_mode = r.chance(0.3) ? uint32_t(r.range(1, 7)) : 0u;
    mm.skipping_cinematic = r.chance(0.2) ? 1 : 0;
    mm.end_step = r.chance(0.5) ? 0 : r.range(1, 9999);
    mm.difficulty = r.range(0, 3);
    mm.shots_fired = r.range(0, 500);
    mm.shots_hit = r.range(0, 500);
    mm.kills = r.range(0, 40);
    mm.player_damage = r.uniform(-10, 5000);
    mm.stylin = r.range(0, 30);
    mm.unknown3d8[0] = r.range(0, 30);
    mm.unknown3d8[1] = r.range(0, 30);
    for (int i = 0; i < 12; ++i) {
        mm.uber_state[i] = r.range(-10, 900);
        mm.scores[i] = r.range(-10, 900);
    }
    drv::MissionSettings* st = nf::p32_ptr<drv::MissionSettings>(mm.settings);
    uint8_t* sb = reinterpret_cast<uint8_t*>(st);
    for (uint32_t o = 0x24c; o < 0x2f8; o += 4) {
        const int32_t v = r.range(1, 400);  // (sem zero: as divisões do TallyScores)
        std::memcpy(sb + o, &v, 4);
    }
    st->shield_max = r.range(50, 2000);
    for (uint32_t o : {0x2d0u, 0x2d4u, 0x2d8u}) {
        const float f = r.uniform(0, 3);
        std::memcpy(sb + o, &f, 4);
    }
    drv::Simulation& s = drv::simulation();
    s.state = r.chance(0.3) ? 3 : r.range(0, 4);
    s.prev_state = r.range(0, 2);
    drv::hud_game_running() = r.range(0, 1);
    g_pc.port_time_scale = r.uniform(0.1f, 2);
    // Os objetivos.
    const int n = r.range(0, 6);
    g_pc.objectives.assign(size_t(n), drv::SMissionObjective{});
    auto& objs = drv::mission_lists().objectives;
    objs.clear();
    for (int i = 0; i < n; ++i) {
        drv::SMissionObjective& o = g_pc.objectives[size_t(i)];
        o.id = lab();
        o.text = lab();
        o.score = r.range(0, 100);
        o.arg = r.range(0, 3);
        o.state = r.range(0, 3);
        objs.push_back(&o);
    }
    // O InputConfigManager: uma a três configurações de cada tipo, os números de texto (com os trocados).
    drv::InputConfigManager& icm = g_pc.icm;
    static const int32_t kSpecial[] = {0xa98, 0xa99, 0x590, 0xa94, 0xa9f, 0xaa3, 0xa9a, 0xaa4};
    for (int t = 0; t < 2; ++t) {
        icm.configs[t].assign(size_t(r.range(1, 4)), drv::InputConfigInfo{});
        for (drv::InputConfigInfo& ci : icm.configs[t]) {
            for (int32_t& v : ci.locale) v = r.chance(0.3) ? kSpecial[r.range(0, 7)] : lab();
            ci.file = 0;
        }
        icm.d.count[t] = int32_t(icm.configs[t].size());
        icm.d.current[t] = r.range(0, icm.d.count[t] - 1);
    }
    icm.d.type = r.range(0, 1);
    icm.d.inverted = r.range(0, 1);
    // A configuração mostrada (+0x444) está sempre na lista do tipo (o PauseOn a lê, o 0x41/0x42 dá a volta nela): uma
    // fora da lista faria o original ler a memória seguinte (o porte devolve o rótulo 0).
    {
        int32_t t2;
        std::memcpy(&t2, hb + 0x2a8, 4);
        w32(0x444, r.range(0, icm.d.count[t2 & 1] - 1));
    }
    // A fila: algumas ações.
    drv::hud_queue_reset();
    static const int32_t kActions[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x47, 0x49, 0x4a, 0x21, 0x22, 0x23,
                                       0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x61, 0x62, 0x10, 0x48, 0x1f};
    const int na = r.chance(0.3) ? 0 : r.range(1, 6);
    for (int i = 0; i < na; ++i)
        drv::hud_queue_feed(kActions[r.range(0, int(sizeof kActions / sizeof kActions[0]) - 1)], 0, r.uniform(0, 1));
    // O ambiente.
    g_pc.tick += r.range(0, 120);
    drv::audio_set_timer(g_pc.tick);
    g_pc.multitap = r.chance(0.3);
    g_pc.video_mode = r.chance(0.7) ? 1 : r.range(1, 3);
    g_pc.accumulate = r.range(0, 1);
    drv::HudPauseEnv& pe = drv::hud_pause_env();
    pe.icm = &icm;
    pe.input_uber = nullptr;
    pe.input_map = nullptr;
    pe.multitap = g_pc.multitap;
    pe.accumulate_scores = g_pc.accumulate != 0;
    pe.time_scale = &g_pc.port_time_scale;
    pe.exit_to_driving = [](int32_t n2, const char* t) { g_pc.port_exits.push_back(std::to_string(n2) + " " + t); };
    drv::hud_draw_env().video_mode = g_pc.video_mode;
    drv::hud_renderer().wide = r.chance(0.2) ? 1 : 0;
    e.m.write32(kRenderer + 0x5c, uint32_t(drv::hud_renderer().wide));
    // A moça: de vez em quando do começo, com o espelho trocado.
    drv::GGirlState& gs = drv::ggirl();
    if (gs.init != 0 && r.chance(0.05)) gs.frames = nullptr;
    if (r.chance(0.05)) gs.normal = r.range(0, 1);
    drv::trigger_manager().pending.clear();
    g_pc.events.clear();
    g_pc.exits.clear();
    g_pc.port_exits.clear();
    g_pc.set_config.clear();
}

}  // namespace

int run_p27_pause_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("  -- a pausa do HUD (o menu, as trapaças, a fila HUDQ, a moça do fundo)\n");
    int failures = 0;
    try {
        drv::Bigf misc, mis01;
        if (!misc.open(disc, "DRIVING/MISC.VIV") || !mis01.open(disc, "DRIVING/MIS01.VIV")) {
            std::printf("  sem o DRIVING/MISC.VIV ou o MIS01.VIV\n");
            return 1;
        }
        auto read = [](drv::Bigf& b, const char* name, std::vector<uint8_t>& out) {
            const int i = b.find(name);
            return i >= 0 && b.read(i, out);
        };
        Ctx c;
        std::vector<uint8_t> loc;
        if (read(misc, "data\\gfx\\locale\\IDL_ENG.loc", loc)) drv::locale().load(loc);
        if (!read(misc, "data\\gfx\\data\\hudNTSC.gal", c.gal) || !read(misc, "data\\loading\\loading.sfn", c.font) ||
            !read(mis01, "data\\render\\blood.ssh", c.blood)) {
            std::printf("  sem o hudNTSC.gal, o loading.sfn ou o blood.ssh\n");
            return 1;
        }
        // Os rótulos sem '%' (o FONT_drawtextfa formataria um '%' com os registradores de cada lado: os textos da
        // pausa não têm).
        for (int32_t id = 0; id < 0x20000; ++id)
            if (const char* t = drv::locale().symbol(id))
                if (!std::strchr(t, '%')) c.labels.push_back(id);
        if (drv::GSubtitlesData* s = drv::GSubtitles_TheApp()) drv::GSubtitles_Destruct(*s);
        const drv::TargetPickerData saved_picker = drv::target_picker();
        std::vector<uint8_t> saved_uber(drv::uber_bond().bytes, drv::uber_bond().bytes + 0xa50);
        const int32_t saved_steps = drv::simulation().steps, saved_rate = drv::simulation().frame_rate;
        const int32_t saved_state = drv::simulation().state, saved_prev = drv::simulation().prev_state;
        const float saved_dt = drv::simulation().dt;
        const bool created_weapons = !drv::weapon_manager_exists();
        drv::mission_reset();
        drv::uber_bond_defaults("paris_mis01", 1);
        drv::SMissionManager_Construct(false);
        if (!drv::weapon_manager_exists()) drv::weapons_create();
        std::vector<uint8_t> saved_wm(sizeof(drv::SWeaponManagerData)), saved_tab(32 * sizeof(drv::WeaponEntry));
        std::memcpy(saved_wm.data(), drv::weapon_manager(), saved_wm.size());
        std::memcpy(saved_tab.data(), nf::p32_ptr<drv::WeaponEntry>(drv::weapon_manager()->weapons), saved_tab.size());
        drv::hud_draw_stats() = drv::HudDrawStats();
        drv::hud_draw_env() = drv::HudDrawEnv();
        drv::hud_renderer() = drv::HudRendererInfo();
        drv::draw_power_on();
        HudEmu e;
        e.setup(elf);
        e.gal_file = c.gal;
        e.blood_file = c.blood;
        e.font_file = c.font;
        std::memcpy(e.m.mem(kUber, 0xa50), drv::uber_bond().bytes, 0xa50);
        failures += check_ctor(e, c);
        if (failures) return failures;
        install_pause_hooks(e);
        // A moça: os dois lados com o 'fi' da galeria e a tabela de cores do .data.
        g_pc.port_file = c.port_base;
        g_pc.emu_file = kGalFile;
        std::vector<int8_t>& colours = drv::ggirl_colour_table();
        colours.resize(0xc000);
        std::memcpy(colours.data(), e.m.mem(0x39d918, 0xc000), 0xc000);
        drv::ggirl() = drv::GGirlState();
        drv::ggirl_image_sink() = [&e](int32_t b, const std::vector<uint32_t>&) -> uint32_t {
            const uint32_t mat = e.m.read32(kGGirlMats + uint32_t(b) * 4);
            const uint32_t tar = mat ? e.m.read32(mat + 0x10) : 0u;
            auto it = e.tex_map.find(tar);
            return it == e.tex_map.end() ? 0u : it->second;
        };
        drv::GGirl_InitGirl(drv::hud());
        e.m.write32(kGHudApp, kGHud);
        e.m.write32(0x33aaf4, 0);
        e.call(0x00239718, {});  // GGirl::InitGirl: os dois GGirlieMaterial e o 'fi'
        const uint32_t edata = e.m.read32(0x33aaec);
        const uint32_t pdata = nf::p32_u(drv::ggirl().data);
        std::printf("  GGirl::InitGirl: o 'fi' em +%s no original, +%s no porte\n", hex(edata - kGalFile).c_str(),
                    hex(pdata - c.port_base).c_str());
        if (!drv::ggirl().data || edata - kGalFile != pdata - c.port_base) ++failures;
        port_stream_env();
        drv::hud_draw_env().zoom_percent = [&c] { return c.zoom_ret; };
        drv::hud_draw_env().aim_fraction = [&c] { return c.aim_ret; };
        drv::hud_draw_env().only_emp = [&c](uint32_t t) {
            for (int i = 0; i < 16; ++i)
                if (nf::p32_u(&c.targets[size_t(i)]) == t) return c.emp[i];
            return false;
        };
        struct PFn {
            const char* name;
            uint32_t addr;
            int weight;
            int arg;  // 0 o GHud, 1 o GHud e uma ação, 2 o GHud e (x, y) em f12/f13
        };
        static const PFn kP[] = {
            {"GHud::DrawPauseMenu", 0x002426a0, 10, 0},  {"GHud::DrawPauseErrors", 0x00241f40, 2, 0},
            {"GHud::Render (a fila HUDQ)", kFnRender, 6, 0}, {"GHud::PauseMenuMsgHandler", 0x00241210, 6, 1},
            {"GHud::CheatMsgHandler", 0x00241bb8, 4, 1},  {"GHud::PauseOn", 0x00241048, 2, 0},
            {"GHud::PauseOff", 0x002411a0, 2, 0},         {"GHud::SetControllerUnplugged", 0x0023cb58, 2, 1},
            {"GHud::DrawBlinkingArrow", 0x002422b8, 2, 2}, {"GGirl::DoGirl", 0x00239848, 6, 0},
            {"GHud::Draw (com a pausa)", kFnDraw, 3, 0},
        };
        int total_w = 0;
        for (const PFn& f : kP) total_w += f.weight;
        std::map<std::string, std::pair<int, int>> tally;
        std::map<std::string, std::string> first_of;
        Rng r(seed ^ 0x50415553u);
        int fails = 0;
        uint64_t mats = 0, packets = 0;
        for (int i = 0; i < cases; ++i) {
            int pick = r.range(0, total_w - 1);
            const PFn* fn = &kP[0];
            for (const PFn& f : kP) {
                if (pick < f.weight) {
                    fn = &f;
                    break;
                }
                pick -= f.weight;
            }
            randomize(e, c, r);
            pause_randomize(e, c, r);
            pause_sync(e);
            e.begin_case();
            drv::hud_frame().clear();
            drv::hud_draw_stats().sounds.clear();
            g_sf.music_stops = 0;
            std::string why;
            const int32_t arg = fn->arg == 1 ? (r.chance(0.8) ? r.range(0x41, 0x4a) : r.range(0x50, 0x66)) : 0;
            const float ax = r.uniform(-50, 600), ay = r.uniform(-50, 450);
            try {
                if (fn->arg == 2) {
                    e.m.set_fpr(12, ax);
                    e.m.set_fpr(13, ay);
                }
                if (fn->addr == 0x0023cb58) {
                    e.call(fn->addr, {kGHud, uint32_t(arg & 1)});
                } else if (fn->addr == 0x00239848) {
                    e.call(fn->addr, {});
                } else {
                    e.call(fn->addr, {kGHud, uint32_t(arg)});
                }
            } catch (const std::exception& ex) {
                why = std::string("o original parou: ") + ex.what();
            }
            e.flush_packet();
            const int emu_stops = g_sf.music_stops;
            g_sf.music_stops = 0;
            drv::GHudData& h = drv::hud();
            switch (fn->addr) {
                case 0x002426a0: drv::GHud_DrawPauseMenu(h); break;
                case 0x00241f40: drv::GHud_DrawPauseErrors(h); break;
                case kFnRender: drv::GHud_Render(h, false); break;
                case 0x00241210: drv::GHud_PauseMenuMsgHandler(h, arg); break;
                case 0x00241bb8: drv::GHud_CheatMsgHandler(h, arg); break;
                case 0x00241048: drv::GHud_PauseOn(h); break;
                case 0x002411a0: drv::GHud_PauseOff(h); break;
                case 0x0023cb58: drv::GHud_SetControllerUnplugged(h, arg & 1); break;
                case 0x002422b8: drv::GHud_DrawBlinkingArrow(h, ax, ay); break;
                case 0x00239848: drv::GGirl_DoGirl(h); break;
                case kFnDraw: drv::GHud_Draw(h); break;
                default: break;
            }
            if (why.empty()) why = compare_events(e.events, port_events());
            if (why.empty()) {
                const auto& ps = drv::hud_draw_stats().sounds;
                if (ps.size() != e.sounds.size()) why = "sons " + std::to_string(e.sounds.size()) + " x " + std::to_string(ps.size());
                for (size_t k = 0; k < ps.size() && why.empty(); ++k)
                    if (ps[k].bank != e.sounds[k].bank || ps[k].name != e.sounds[k].name || ps[k].mix != e.sounds[k].mix ||
                        ps[k].arg != e.sounds[k].arg)
                        why = "som " + std::to_string(k) + ": " + e.sounds[k].name + " x " + ps[k].name;
            }
            if (why.empty() && emu_stops != g_sf.music_stops) why = "AStream::Stop da música";
            if (why.empty()) why = compare_state(e, c);
            if (why.empty()) {
                try {
                    why = pause_compare(e);
                } catch (const std::exception& ex) {
                    why = std::string("a comparação parou: ") + ex.what();
                }
            }
            for (const Ev& ev : e.events) (ev.packet ? packets : mats) += 1;
            auto& t = tally[fn->name];
            ++t.first;
            if (!why.empty()) {
                ++t.second;
                ++fails;
                if (first_of[fn->name].empty()) first_of[fn->name] = "caso " + std::to_string(i) + ": " + why;
            }
        }
        for (const auto& kv : tally)
            std::printf("  %-34s %5d casos, %d diferentes%s%s\n", kv.first.c_str(), kv.second.first, kv.second.second,
                        first_of[kv.first].empty() ? "" : " | ", first_of[kv.first].c_str());
        std::printf("  desenhos da pausa: %" PRIu64 " materiais, %" PRIu64 " pacotes de fonte; moça: %" PRIu64
                    " quadros, %" PRIu64 " recomeços\n",
                    mats, packets, drv::hud_pause_stats().girl_frames, drv::hud_pause_stats().girl_restarts);
        failures += fails;
        // ---- o resto: TallyScores, WordWrap, GetLocaleID, GetLanguage
        {
            int tf = 0, wf = 0, lf = 0, gf = 0;
            std::string tw, ww, lw, gw;
            for (int i = 0; i < cases; ++i) {
                randomize(e, c, r);
                pause_randomize(e, c, r);
                pause_sync(e);
                // TallyScores num UberBond sorteado (o da troca não muda).
                drv::UberBondBlock ub{};
                for (uint32_t o = 0; o < 0xa50; o += 4) {
                    const int32_t v = r.chance(0.5) ? r.range(-5, 900) : r.range(1, 9);
                    std::memcpy(ub.bytes + o, &v, 4);
                }
                std::memcpy(e.m.mem(kPUberCopy, 0xa50), ub.bytes, 0xa50);
                const bool fin = r.chance(0.3);
                std::string why;
                try {
                    e.call(0x00205c30, {kMission, kPUberCopy, fin ? 1u : 0u});
                } catch (const std::exception& ex) {
                    why = std::string("o original parou: ") + ex.what();
                }
                drv::SMissionManager_TallyScores(drv::mission_manager(), ub, fin);
                for (uint32_t o = 0; o < 0xa50 && why.empty(); o += 4) {
                    uint32_t pv;
                    std::memcpy(&pv, ub.bytes + o, 4);
                    if (e.m.read32(kPUberCopy + o) != pv) why = "UberBond +" + hex(o);
                }
                const uint8_t* mb = reinterpret_cast<const uint8_t*>(&drv::mission_manager());
                for (uint32_t o = 0x648; o < 0x678 && why.empty(); o += 4)
                    if (std::memcmp(e.m.mem(kMission + o, 4), mb + o, 4) != 0) why = "missão +" + hex(o);
                if (!why.empty() && tw.empty()) tw = "caso " + std::to_string(i) + ": " + why;
                if (!why.empty()) ++tf;
                // WordWrap de um texto do .loc (ou um sem espaços).
                std::string text = drv::LOCALE_Symbol(c.labels[size_t(r.range(0, int(c.labels.size()) - 1))]);
                if (r.chance(0.1)) text = std::string(size_t(r.range(0, 200)), 'x');
                if (r.chance(0.1)) text = "  a  b   " + text + "   ";
                const uint32_t max = uint32_t(r.range(1, 80));
                std::memcpy(e.m.mem(kPWrap + 0x100, uint32_t(text.size()) + 1), text.c_str(), text.size() + 1);
                why.clear();
                int32_t en = 0;
                std::vector<std::string> el;
                try {
                    e.call(0x002424a0, {kPWrap + 0x100, max, kPWrap});
                    en = int32_t(e.m.gpr(R::v0));
                    const uint32_t arr = e.m.read32(kPWrap);
                    for (int32_t k = 0; k < en; ++k)
                        el.push_back(reinterpret_cast<const char*>(e.m.mem(e.m.read32(arr + uint32_t(k) * 4), 1)));
                } catch (const std::exception& ex) {
                    why = std::string("o original parou: ") + ex.what();
                }
                std::vector<std::string> pl;
                const int32_t pn = drv::WordWrap(text.c_str(), max, pl);
                if (why.empty() && pn != en) why = "linhas " + std::to_string(en) + " x " + std::to_string(pn);
                for (int32_t k = 0; k < en && why.empty(); ++k)
                    if (el[size_t(k)] != pl[size_t(k)]) why = "linha " + std::to_string(k) + " \"" + el[size_t(k)] + "\" x \"" + pl[size_t(k)] + "\"";
                if (!why.empty() && ww.empty()) ww = "caso " + std::to_string(i) + ": " + why;
                if (!why.empty()) ++wf;
                // GetLocaleID.
                const int32_t type = r.range(0, 1);
                const int32_t cfg = r.range(0, g_pc.icm.d.count[type] - 1);
                const int32_t idx = r.range(0, 13);
                int32_t ev = -1;
                try {
                    e.call(0x00166dc0, {kGIcm, uint32_t(type), uint32_t(cfg), uint32_t(idx)});
                    ev = int32_t(e.m.gpr(R::v0));
                } catch (const std::exception&) {
                }
                const int32_t pv = drv::InputConfigManager_GetLocaleID(g_pc.icm, type, cfg, idx);
                if (ev != pv) {
                    ++lf;
                    if (lw.empty()) lw = "caso " + std::to_string(i) + ": " + hex(uint32_t(ev)) + " x " + hex(uint32_t(pv));
                }
                // GLocale::GetLanguage.
                try {
                    e.call(0x00249f98, {});
                } catch (const std::exception&) {
                    e.m.set_gpr(R::v0, 0xdead);
                }
                if (int32_t(e.m.gpr(R::v0)) != drv::GLocale_GetLanguage()) {
                    ++gf;
                    if (gw.empty())
                        gw = "caso " + std::to_string(i) + ": +0x988 " + std::to_string(drv::uber_bond().word(0x988)) + " (" +
                             std::to_string(int32_t(e.m.read32(kUber + 0x988))) + "): " +
                             std::to_string(int32_t(e.m.gpr(R::v0))) + " x " + std::to_string(drv::GLocale_GetLanguage());
                }
            }
            auto line = [](const char* name, int n, int f, const std::string& w) {
                std::printf("  %-34s %5d casos, %d diferentes%s%s\n", name, n, f, w.empty() ? "" : " | ", w.c_str());
            };
            line("SMissionManager::TallyScores", cases, tf, tw);
            line("WordWrap", cases, wf, ww);
            line("InputConfigManager::GetLocaleID", cases, lf, lw);
            line("GLocale::GetLanguage", cases, gf, gw);
            failures += tf + wf + lf + gf;
        }
        std::printf("  instruções do interpretador (pausa): %" PRIu64 "\n", uint64_t(e.m.steps()));
        drv::mission_lists().objectives.clear();  // (os objetivos sorteados não são do gerente)
        drv::mission_reset();
        drv::hud_exists() = false;
        drv::hud_draw_env() = drv::HudDrawEnv();
        drv::hud_draw_globals() = drv::HudDrawGlobals();
        drv::hud_pause_env() = drv::HudPauseEnv();
        drv::hud_queue_reset();
        drv::ggirl() = drv::GGirlState();
        drv::ggirl_image_sink() = nullptr;
        drv::ggirl_colour_table().clear();
        drv::trigger_manager().pending.clear();
        drv::sniper_zoom() = drv::SniperZoomState();
        drv::draw_power_on();
        drv::target_picker() = saved_picker;
        std::memcpy(drv::uber_bond().bytes, saved_uber.data(), 0xa50);
        drv::simulation().steps = saved_steps;
        drv::simulation().frame_rate = saved_rate;
        drv::simulation().dt = saved_dt;
        drv::simulation().state = saved_state;
        drv::simulation().prev_state = saved_prev;
        drv::hud_game_running() = 1;
        if (created_weapons) {
            drv::weapons_destroy();
        } else {
            std::memcpy(drv::weapon_manager(), saved_wm.data(), saved_wm.size());
            std::memcpy(nf::p32_ptr<drv::WeaponEntry>(drv::weapon_manager()->weapons), saved_tab.data(), saved_tab.size());
        }
    } catch (const std::exception& ex) {
        std::printf("  erro: %s\n", ex.what());
        return 1;
    }
    return failures;
}

int run_p24_hud_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed) {
    std::printf("\n== parte 24: o HUD (GHud::GHud, GOrthoHudView::DoRender, GHud::Render, os Draw*) ==\n");
    int failures = 0;
    try {
        drv::Bigf misc, mis01;
        if (!misc.open(disc, "DRIVING/MISC.VIV") || !mis01.open(disc, "DRIVING/MIS01.VIV")) {
            std::printf("  sem o DRIVING/MISC.VIV ou o MIS01.VIV\n");
            return 1;
        }
        auto read = [](drv::Bigf& b, const char* name, std::vector<uint8_t>& out) {
            const int i = b.find(name);
            return i >= 0 && b.read(i, out);
        };
        Ctx c;
        std::vector<uint8_t> loc;
        if (read(misc, "data\\gfx\\locale\\IDL_ENG.loc", loc)) drv::locale().load(loc);
        if (!read(misc, "data\\gfx\\data\\hudNTSC.gal", c.gal) || !read(misc, "data\\loading\\loading.sfn", c.font) ||
            !read(mis01, "data\\render\\blood.ssh", c.blood)) {
            std::printf("  sem o hudNTSC.gal, o loading.sfn ou o blood.ssh\n");
            return 1;
        }
        // Os rótulos do .loc (e os da mensagem de botão, com os símbolos).
        for (int32_t id = 0; id < 0x20000; ++id) {
            const char* s = drv::locale().symbol(id);
            if (s == nullptr) continue;
            c.labels.push_back(id);
            if (std::strstr(s, "*A*") || std::strstr(s, "*G*") || std::strstr(s, "*P*") || std::strstr(s, "*D*"))
                c.button_labels.push_back(id);
            if (std::strchr(s, '%')) c.percent_labels.push_back(id);
        }
        // O porte: uma missão (o gerente, os cronômetros), o SWeaponManager, o GSubtitles fora. O que a conferência muda
        // volta no fim (o WTargetPicker, as armas, o UberBond, a simulação: as partes seguintes os usam).
        if (drv::GSubtitlesData* s = drv::GSubtitles_TheApp()) drv::GSubtitles_Destruct(*s);
        const drv::TargetPickerData saved_picker = drv::target_picker();
        std::vector<uint8_t> saved_uber(drv::uber_bond().bytes, drv::uber_bond().bytes + 0xa50);
        const int32_t saved_steps = drv::simulation().steps, saved_rate = drv::simulation().frame_rate;
        const float saved_dt = drv::simulation().dt;
        const bool created_weapons = !drv::weapon_manager_exists();
        drv::mission_reset();
        drv::uber_bond_defaults("paris_mis01", 1);
        drv::SMissionManager_Construct(false);
        if (!drv::weapon_manager_exists()) drv::weapons_create();
        std::vector<uint8_t> saved_wm(sizeof(drv::SWeaponManagerData)), saved_tab(32 * sizeof(drv::WeaponEntry));
        std::memcpy(saved_wm.data(), drv::weapon_manager(), saved_wm.size());
        std::memcpy(saved_tab.data(), nf::p32_ptr<drv::WeaponEntry>(drv::weapon_manager()->weapons), saved_tab.size());
        {
            const drv::WeaponEntry* tab = nf::p32_ptr<drv::WeaponEntry>(drv::weapon_manager()->weapons);
            for (int i = 0; i < 32; ++i)
                if (tab[i].name_label > 0 && drv::locale().symbol(tab[i].name_label)) c.weapon_labels.push_back(tab[i].name_label);
        }
        drv::hud_draw_stats() = drv::HudDrawStats();
        drv::hud_draw_env() = drv::HudDrawEnv();
        drv::hud_renderer() = drv::HudRendererInfo();
        drv::draw_power_on();
        HudEmu e;
        e.setup(elf);
        e.gal_file = c.gal;
        e.blood_file = c.blood;
        e.font_file = c.font;
        std::memcpy(e.m.mem(kUber, 0xa50), drv::uber_bond().bytes, 0xa50);
        std::printf("  %zu rótulos no IDL_ENG.loc (%zu com símbolos de botão, %zu com %%)\n", c.labels.size(), c.button_labels.size(),
                    c.percent_labels.size());
        failures += check_ctor(e, c);
        if (failures == 0) failures += check_zoom_percent(e, std::max(1, cases / 4), seed);
        if (failures == 0) failures += check_draws(e, c, cases, seed);
        std::printf("  instruções do interpretador: %" PRIu64 "\n", uint64_t(e.m.steps()));
        drv::mission_reset();
        drv::hud_exists() = false;
        drv::hud_draw_env() = drv::HudDrawEnv();
        drv::hud_draw_globals() = drv::HudDrawGlobals();
        drv::sniper_zoom() = drv::SniperZoomState();
        drv::draw_power_on();
        drv::target_picker() = saved_picker;
        std::memcpy(drv::uber_bond().bytes, saved_uber.data(), 0xa50);
        drv::simulation().steps = saved_steps;
        drv::simulation().frame_rate = saved_rate;
        drv::simulation().dt = saved_dt;
        if (created_weapons) {
            drv::weapons_destroy();
        } else {
            std::memcpy(drv::weapon_manager(), saved_wm.data(), saved_wm.size());
            std::memcpy(nf::p32_ptr<drv::WeaponEntry>(drv::weapon_manager()->weapons), saved_tab.data(), saved_tab.size());
        }
    } catch (const std::exception& ex) {
        std::printf("  erro: %s\n", ex.what());
        return 1;
    }
    return failures;
}

}  // namespace drvcheck
