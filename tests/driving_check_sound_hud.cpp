// nf_driving_check, parte 27 (tests/driving_check_sound_hud.h): os sons das armas, da mira e dos objetos do mundo e o
// resto do HUD de Paris, o código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado
// com o porte (docs/driving.md seção 31).
//
// Os sons (driving/sound/audio_scene.cpp, audio_world.cpp): o original roda com as vozes, as mixagens, os índices dos
// bancos, as alocações e a lista dos sons nos ganchos; o porte roda com o registro do audio_trace() ligado. Os dois
// lados escrevem as mesmas linhas, na ordem: as vozes criadas (a mixagem, o banco e o timbre), tocadas (os seis números
// do AVoice::Play) e soltas, as mixagens pedidas (Add, Get), as transições e o AMix::Reset, os sons apagados e os
// AMenuSound::Trigger; depois os campos dos objetos e das vozes vivas são comparados.
//  a) AMenuSound: o AMenuSoundPriv (o construtor e o Play), os três Trigger;
//  b) ATargeting (o construtor e o SetState), ACarAlarm (o construtor e o Play), AZoomObj (o construtor, o InTheZone,
//     o Play e o destrutor), ATurret (o construtor, o Play em sequências, o destrutor), AGun (o construtor, o
//     SetFirePatch, o Play em sequências e o destrutor, com os basic_string do GCC 2.95 nos ganchos);
//  c) ASmackable (o construtor, o PlayLanding, o PlayCollision com o 0x00355ea0, o IsStillActive, o Play em sequências,
//     o Detatch e o destrutor) e o fim do Smackable::Simulate (a posição ao som e o sono pelo IsStillActive);
//  d) AWorldSound (o construtor, o Play em sequências com o rrandom original e o ASoundManager::fgTime, o
//     SetWarningVoice, o SetTiming, o SetRepeat) e o WSoundGroup (o construtor, o Start, os três Add, o End, o Clear:
//     a árvore da STL do original no interpretador);
//  e) APlayerHeli (o construtor e o Play sem o do AVehicle), os AVehicle::Activate* e o StartAlarm;
//  f) o leitor dos campos de som da "PhysicsData" (0x00194d48);
//  g) a pausa do HUD (run_p27_pause_checks, em driving_check_p24_hud.cpp, no HudEmu da parte 24): o GHud::Render com a
//     fila "HUDQ", o PauseOn/PauseOff, o PauseMenuMsgHandler, o CheatMsgHandler, o SetControllerUnplugged, o
//     DrawPauseMenu, o DrawPauseErrors, o DrawBlinkingArrow, o GHud::Draw com a pausa, o GGirl::DoGirl (a moça do fundo,
//     com a tabela de cores do .data), o SMissionManager::TallyScores, o WordWrap, o
//     InputConfigManager::GetLocaleID e o GLocale::GetLanguage, em estados sorteados do GHud, da missão e do UberBond.
//
// Fora do código original (ganchos nativos): o AMix (Add, Get, GetVolume pelo volume da mixagem do porte, sem
// transição; SetTransition e Reset gravados), o AVoice::AVoice (as vistas iniciadas como o porte as inicia),
// o AVoice::Play e o AVoice::View::~View, o UMemory::FastAlloc/FastFree, o ABaseSound::operator new/delete e o
// ~ABaseSound, o __builtin_new/delete, o AIndex::Lookup e o ABank::Get (os bancos de teste, os mesmos nos dois lados),
// o printf de depuração, as cadeias do basic_string (a cópia do 0x00177260), o AMenuSound::Trigger(int, int, ...).

#include "driving_check_sound_hud.h"

#include "driving_check_p24.h"

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/game/smackable.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/audio_stream.h"
#include "driving/sound/snd_state.h"
#include "platform/crt_compat.h"
#include "platform/disc_source.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"
#include "platform/vu_interp.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;

// As funções originais conferidas.
constexpr uint32_t kFnMenuPrivCtor = 0x2f4780, kFnMenuPrivPlay = 0x2f4918, kFnMenuPrivDtor = 0x2f4cb8;
constexpr uint32_t kFnMenuTrigger = 0x2f4a20, kFnMenuTriggerBank = 0x2f4aa0, kFnMenuTriggerCat = 0x2f4b10;
constexpr uint32_t kFnTargetingCtor = 0x2e6a88, kFnTargetingSet = 0x2e6ac8;
constexpr uint32_t kFnAlarmCtor = 0x2debc8, kFnAlarmPlay = 0x2dec38;
constexpr uint32_t kFnZoomCtor = 0x2dc500, kFnZoomZone = 0x2dc5a0, kFnZoomPlay = 0x2dcb10, kFnZoomDtor = 0x2dcbb0;
constexpr uint32_t kFnTurretCtor = 0x2e4930, kFnTurretPlay = 0x2e4980, kFnTurretDtor = 0x2e5148;
constexpr uint32_t kFnGunCtor = 0x2f6e60, kFnGunDtor = 0x2f7078, kFnGunPatch = 0x2f7188, kFnGunPlay = 0x2f7300;
constexpr uint32_t kFnSmackCtor = 0x2ef6f0, kFnSmackLanding = 0x2ef7d0, kFnSmackCollision = 0x2ef7f8;
constexpr uint32_t kFnSmackActive = 0x2ef8c0, kFnSmackDtor = 0x2ef8d0, kFnSmackPlay = 0x2ef9d0;
constexpr uint32_t kFnSmackDetatch = 0x2efc80, kFnSmackableSimulate = 0x19e718;
constexpr uint32_t kFnWorldCtor = 0x2dd380, kFnWorldPlay = 0x2dcd50, kFnWorldWarning = 0x2dd500;
constexpr uint32_t kFnWorldTiming = 0x2dd5b8, kFnWorldRepeat = 0x2dd650;
constexpr uint32_t kFnGroupCtor = 0x227868, kFnGroupStart = 0x2278f8, kFnGroupAdd3 = 0x2279d0;
constexpr uint32_t kFnGroupAddText = 0x227d80, kFnGroupAdd2 = 0x227e38, kFnGroupEnd = 0x227e58;
constexpr uint32_t kFnGroupClear = 0x228560;
constexpr uint32_t kFnHeliCtor = 0x2f1428, kFnHeliPlay = 0x2f16b8;
constexpr uint32_t kFnActivateGun = 0x2df650, kFnActivateTurret = 0x2df6a0, kFnActivateZoom = 0x2df6e8;
constexpr uint32_t kFnStartAlarm = 0x2df780, kFnActivateWind = 0x2df738;
constexpr uint32_t kFnSoundField = 0x194d48;
// Os ganchos.
constexpr uint32_t kFnMixAdd = 0x2f2d50, kFnMixGet = 0x2f2dc8, kFnMixVolume = 0x2f34d0, kFnMixTrans = 0x2f33a0;
constexpr uint32_t kFnMixReset = 0x2f3450;
constexpr uint32_t kFnVoiceCtor = 0x2dd790, kFnVoicePlay = 0x2ddfd0, kFnViewDtor = 0x2de0b8;
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnFastFree = 0x2cfe50, kFnIndexLookup = 0x2f5880;
constexpr uint32_t kFnSoundNew = 0x2fca38, kFnSoundDtor = 0x2fcb28, kFnSoundDelete = 0x2fcad8;
constexpr uint32_t kFnBuiltinNew = 0x2d0910, kFnBuiltinDelete = 0x2d09a8, kFnOpNew = 0x2d09c8, kFnFree = 0x2d0a50;
constexpr uint32_t kFnDebug = 0x10c6c8, kFnBankGet = 0x2fd308, kFnStrAssign = 0x177260, kFnStrClone = 0x137e28;
constexpr uint32_t kFnVehicleCtor = 0x2defd0, kFnVehiclePlay = 0x2e1768, kFnGunCtorHook = kFnGunCtor;
constexpr uint32_t kFnStrlen = 0x2a9760, kFnStrcpy = 0x2a964c, kFnStrcasecmp = 0x2a9198, kFnMemset = 0x2a8038;
constexpr uint32_t kFnSscanf = 0x2a9110, kFnPrintf = 0x2a8148, kFnOpDelete2 = 0x2cfe50;
constexpr uint32_t kFnGetRigidBody = 0x1ff088, kFnAnySystemPlaying = 0x1a2cc0, kFnGoToSleep = 0x19e610;
constexpr uint32_t kFnPhysSimulate = 0x195aa8, kFnTreeDelete = 0x2fe050;

// Os globais do PS2.
constexpr uint32_t kViewMask = 0x355cd0, kSoundCount = 0x355e60, kSoundTime = 0x355e64, kIsPaused = 0x3c82c8;
constexpr uint32_t kNextHard = 0x355ea0, kRandom = 0x343810, kBanks = 0x3562a0, kEmpty = 0x356288;
constexpr uint32_t kStrEmptyFlag = 0x32f14c, kStrEmptyRefs = 0x32f148, kStrEmptyRep = 0x32f150;

// A RAM emulada.
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;
constexpr uint32_t kFake = 0x01900000;
constexpr uint32_t kMixes = kFake;                     // as mixagens falsas (0x50 bytes cada, o nome no +0x30)
constexpr uint32_t kBankObjs = kFake + 0x10000;        // os bancos de teste (0x18 bytes cada)
constexpr uint32_t kScratch = kFake + 0x20000;         // os argumentos dos casos
constexpr uint32_t kListener = kFake + 0x21000, kPath = kFake + 0x21100, kText = kFake + 0x21200;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2727) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
    float real(float a, float b) { return a + (b - a) * float(next() & 0xffffff) / float(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

std::string cstr(const MipsMachine& m, uint32_t a) {
    std::string s;
    if (a == 0 || a >= MipsMachine::kRamSize) return s;
    const uint32_t n = std::min<uint32_t>(0x1000, MipsMachine::kRamSize - a);
    const char* p = reinterpret_cast<const char*>(m.mem(a, n));
    size_t len = 0;
    while (len < n && p[len]) ++len;
    s.assign(p, len);
    return s;
}

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-60s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    failures += int(t.bad);
    std::fflush(stdout);
}

// O registro do porte (um por caso: os números das vozes continuam de uma chamada para a outra, como os do original).
drv::AudioTrace g_ptrace;
void ptrace_reset() {
    g_ptrace.log.clear();
    g_ptrace.ids.clear();
    g_ptrace.next = 0;
}

// ---- os bancos de teste ------------------------------------------------------------------------------------------

// Os nomes que os sons destas classes pedem e alguns a mais; cada banco tem só parte deles (os outros dão -1).
const char* const kNames[] = {
    "SFX_HeliMachGun", "SFX_ShellCasing", "SFX_TurretLR",   "SFX_TurretLRStop", "SFX_TurretUD",  "SFX_TurretUDStop",
    "SFX_servo",       "SFX_servodown",   "SFX_Kachung",    "SFX_breath",       "SFX_CarAlarm",  "SFX_targetlock",
    "SFX_targetunlock", "SFX_helichange", "SFX_heli",       "SFX_mntmachfire",  "SFX_LampPost1", "SFX_LampPost2",
    "SFX_LampPost3",   "SFX_ScrapePole",  "SFX_MetalBox1",  "SFX_Roll",         "SFX_Hydrant",   "SFX_nightclb",
    "SFX_truckbeep",   "SFX_Bench1",      "SFX_misstatus",  "SFX_Exit",         "SFX_whoosh",    "SFX_x",
};
constexpr int kNameCount = int(sizeof kNames / sizeof kNames[0]);
constexpr int kBankCount = 10;
const char* const kBankNames[kBankCount] = {"gen.bnk", "col.bnk", "smk.bnk", "cw.bnk", "wpn.bnk",
                                            "amb.bnk", "misc.bnk", "act.bnk", "com.bnk", "eng.bnk"};

// Um índice por banco (o mesmo nos dois lados): o nome entra com a probabilidade, num número sorteado.
struct TestBanks {
    std::map<std::string, int32_t> index[kBankCount];
    int32_t snd[kBankCount] = {};
    void randomize(Rng& r) {
        for (int b = 0; b < kBankCount; ++b) {
            index[b].clear();
            snd[b] = r.chance(0.05) ? -1 : r.range(0, 12);
            for (int i = 0; i < kNameCount; ++i)
                if (r.chance(0.7)) index[b][kNames[i]] = r.range(0, 0x7f);
        }
    }
    int32_t lookup(int b, const std::string& n) const {
        auto it = index[b].find(n);
        return it == index[b].end() ? -1 : it->second;
    }
};

// ---- o interpretador -----------------------------------------------------------------------------------------------

class Emu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeap;
    std::string error;
    uint64_t errors = 0;
    // O registro (as mesmas linhas do drv::AudioTrace).
    std::vector<std::string> log;
    std::map<uint32_t, int> ids;  // a voz (o endereço) -> o número
    int next_id = 0;
    std::map<std::string, uint32_t> mix_handle;
    std::map<uint32_t, std::string> mix_name;
    TestBanks* banks = nullptr;
    bool record_menu = true;   // o AMenuSound::Trigger(int, int, ...) gravado (sem rodar)
    std::set<uint32_t> sounds;  // os ABaseSound vivos (do operator new)

    uint32_t alloc(uint32_t n) {
        const uint32_t a = (heap + 15) & ~15u;
        if (a + n > kHeapEnd) throw nf::MipsError("o heap emulado acabou", a, n);
        heap = a + n;
        std::memset(m.mem(a, n), 0, n);
        return a;
    }
    int id(uint32_t v) {
        auto it = ids.find(v);
        if (it != ids.end()) return it->second;
        ids[v] = next_id;
        return next_id++;
    }
    void line(const char* fmt, ...) {
        char b[256];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        log.emplace_back(b);
    }
    std::string mixn(uint32_t h) const {
        auto it = mix_name.find(h);
        return it == mix_name.end() ? std::string("?") : it->second;
    }
    uint32_t mix(const std::string& name) {
        auto it = mix_handle.find(name);
        if (it != mix_handle.end()) return it->second;
        const uint32_t h = kMixes + uint32_t(mix_handle.size()) * 0x50u;
        std::memset(m.mem(h, 0x50), 0, 0x50);
        std::memcpy(m.mem(h + 0x30, 0x20), name.c_str(), std::min<size_t>(name.size() + 1, 0x1f));
        mix_handle[name] = h;
        mix_name[h] = name;
        return h;
    }
    bool call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        try {
            m.call(fn, args, 50'000'000);
            return true;
        } catch (const std::exception& ex) {
            if (errors++ == 0) error = ex.what();
            return false;
        }
    }
    float getf(uint32_t a) const { return ps2::ffloat(m.read32(a)); }
    void putf(uint32_t a, float f) { m.write32(a, ps2::fbits(f)); }

    // Uma voz emulada como o AVoice::AVoice do porte a inicia (o AVoice::Set: sem timbre, as vistas acabadas).
    void init_voice(uint32_t v, uint32_t mixh, int32_t bank, int32_t patch) {
        m.write32(v, mixh);
        for (int i = 0; i < 3; ++i) {
            const uint32_t w = v + 4 + 0x20u * uint32_t(i);
            std::memset(m.mem(w, 0x20), 0, 0x20);
            m.write32(w + 0x00, patch == -1 ? 0xffffffffu : uint32_t(patch));
            m.write32(w + 0x04, patch == -1 ? 0xffffffffu : uint32_t(bank));
            m.write32(w + 0x08, 1);
            m.write32(w + 0x0c, 0xffffffffu);
            m.write32(w + 0x10, 0x1000);
            m.write32(w + 0x14, 0xffff);  // delay -1, vol 0, listed 0
            m.write32(w + 0x18, 0);
            m.write32(w + 0x1c, patch == -1 ? 1u : 0u);
        }
    }

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        m.hook(kFnMixAdd, "AMix::Add", [this](MipsMachine& mm) {
            const std::string n = cstr(mm, uint32_t(mm.gpr(R::a0)));
            line("add %s", n.c_str());
            mm.set_gpr(R::v0, mix(n));
        });
        m.hook(kFnMixGet, "AMix::Get", [this](MipsMachine& mm) {
            const std::string n = cstr(mm, uint32_t(mm.gpr(R::a0)));
            line("get %s", n.c_str());
            mm.set_gpr(R::v0, mix(n));
        });
        m.hook(kFnMixVolume, "AMix::GetVolume", [this](MipsMachine& mm) {
            const std::string n = mixn(uint32_t(mm.gpr(R::a0)));
            float v = 0.0f;
            auto& mixes = *drv::audio_internals().mixes;
            auto it = mixes.find(n);
            if (it != mixes.end()) v = it->second->volume;
            mm.set_fpr(0, v);
        });
        m.hook(kFnMixTrans, "AMix::SetTransition", [this](MipsMachine& mm) {
            line("trans %s %08x %d", mixn(uint32_t(mm.gpr(R::a0))).c_str(), unsigned(mm.fpr_bits(12)),
                 int(int32_t(mm.gpr(R::a1))));
        });
        m.hook(kFnMixReset, "AMix::Reset", [this](MipsMachine& mm) { line("reset %d", int(int32_t(mm.gpr(R::a0)))); });
        m.hook(kFnVoiceCtor, "AVoice::AVoice", [this](MipsMachine& mm) {
            const uint32_t v = uint32_t(mm.gpr(R::a0)), mixh = uint32_t(mm.gpr(R::a1));
            const int32_t bank = int32_t(mm.gpr(R::a2)), patch = int32_t(mm.gpr(R::a3));
            line("voice#%d %s %d %d", id(v), mixn(mixh).c_str(), int(bank), int(patch));
            init_voice(v, mixh, bank, patch);
            mm.set_gpr(R::v0, v);
        });
        m.hook(kFnVoicePlay, "AVoice::Play", [this](MipsMachine& mm) {
            const uint32_t v = uint32_t(mm.gpr(R::a0));
            line("play#%d %08x %08x %08x %08x %08x %d", v ? id(v) : -1, unsigned(mm.fpr_bits(12)),
                 unsigned(mm.fpr_bits(13)), unsigned(mm.fpr_bits(14)), unsigned(mm.fpr_bits(15)),
                 unsigned(mm.fpr_bits(16)), int(int32_t(mm.gpr(R::a1))));
        });
        // O ~View das três vistas (2, 1, 0): a da vista 0 (o +4 da voz) fecha a voz, solta ou dentro de um som.
        m.hook(kFnViewDtor, "AVoice::View::~View", [this](MipsMachine& mm) {
            auto it = ids.find(uint32_t(mm.gpr(R::a0)) - 4u);
            if (it != ids.end()) {
                line("free#%d", it->second);
                ids.erase(it);
            }
        });
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u)));
        });
        m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kFnIndexLookup, "AIndex::Lookup", [this](MipsMachine& mm) {
            const uint32_t idx = uint32_t(mm.gpr(R::a0));
            const std::string n = cstr(mm, uint32_t(mm.gpr(R::a1)));
            int32_t r = -1;
            for (int b = 0; b < kBankCount; ++b)
                if (idx == kBankObjs + 0x18u * uint32_t(b) + 0x10u) r = banks->lookup(b, n);
            mm.set_gpr(R::v0, uint64_t(int64_t(r)));
        });
        m.hook(kFnBankGet, "ABank::Get", [this](MipsMachine& mm) {
            const std::string n = cstr(mm, uint32_t(mm.gpr(R::a0)));
            uint32_t r = kEmpty;
            for (int b = 0; b < kBankCount; ++b)
                if (nf::str_icmp(n.c_str(), kBankNames[b]) == 0) r = kBankObjs + 0x18u * uint32_t(b);
            mm.set_gpr(R::v0, r);
        });
        m.hook(kFnSoundNew, "ABaseSound::operator new", [this](MipsMachine& mm) {
            const uint32_t a = alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u));
            sounds.insert(a);
            mm.set_gpr(R::v0, a);
        });
        m.hook(kFnSoundDtor, "ABaseSound::~ABaseSound", [this](MipsMachine& mm) {
            const uint32_t s = uint32_t(mm.gpr(R::a0));
            line("del %s", mixn(mm.read32(s + 0x84)).c_str());
            sounds.erase(s);
        });
        m.hook(kFnSoundDelete, "ABaseSound::operator delete", [](MipsMachine&) {});
        m.hook(kFnBuiltinNew, "__builtin_new", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u)));
        });
        m.hook(kFnOpNew, "operator new", [this](MipsMachine& mm) {
            mm.set_gpr(R::v0, alloc(std::max(uint32_t(mm.gpr(R::a0)), 16u)));
        });
        m.hook(kFnBuiltinDelete, "__builtin_delete", [](MipsMachine&) {});
        m.hook(kFnFree, "free", [](MipsMachine&) {});
        m.hook(kFnDebug, "(o printf de depuração)", [](MipsMachine&) {});
        m.hook(kFnPrintf, "printf", [](MipsMachine&) {});
        setup_menu_hook();
        m.hook(kFnStrlen, "strlen", [](MipsMachine& mm) {
            mm.set_gpr(R::v0, uint32_t(cstr(mm, uint32_t(mm.gpr(R::a0))).size()));
        });
        m.hook(kFnStrcpy, "strcpy", [](MipsMachine& mm) {
            const std::string s = cstr(mm, uint32_t(mm.gpr(R::a1)));
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a0)), uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(kFnStrcasecmp, "strcasecmp", [](MipsMachine& mm) {
            const std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            mm.set_gpr(R::v0, uint64_t(int64_t(nf::str_icmp(a.c_str(), b.c_str()))));
        });
        // O basic_string do GCC 2.95: o replace(0, npos, s, n) com uma representação nova (o comprimento em -0x10, a
        // capacidade em -0xc, as referências em -8, o "egoísta" em -4).
        m.hook(kFnStrAssign, "basic_string::replace", [this](MipsMachine& mm) {
            const uint32_t obj = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a3));
            const uint32_t n = uint32_t(mm.gpr(R::t0));
            mm.write32(obj, new_string(std::string(reinterpret_cast<const char*>(mm.mem(src, n)), n)));
            mm.set_gpr(R::v0, obj);
        });
        m.hook(kFnStrClone, "basic_string::Rep::clone", [this](MipsMachine& mm) {
            const uint32_t rep = uint32_t(mm.gpr(R::a0));
            mm.set_gpr(R::v0, new_string(cstr(mm, rep + 0x10)) - 0x10);
        });
        m.write32(kStrEmptyFlag, 0);
        m.write32(kStrEmptyRefs, 1);
        m.write32(kStrEmptyRep - 0x10, 0);
        m.write32(kViewMask, 4);
    }
    void setup_menu_hook() {
        m.hook(kFnMenuTrigger, "AMenuSound::Trigger(int, int, char *, AView)", [this](MipsMachine& mm) {
            line("menu %d %d %s %d", int(int32_t(mm.gpr(R::a0))), int(int32_t(mm.gpr(R::a1))),
                 cstr(mm, uint32_t(mm.gpr(R::a2))).c_str(), int(int32_t(mm.gpr(R::a3))));
        });
    }
    // Uma cadeia nova (o ponteiro dos dados).
    uint32_t new_string(const std::string& s) {
        const uint32_t rep = alloc(0x10 + uint32_t(s.size()) + 1);
        m.write32(rep + 0, uint32_t(s.size()));
        m.write32(rep + 4, uint32_t(s.size()));
        m.write32(rep + 8, 1);
        m.write32(rep + 0xc, 0);
        std::memcpy(m.mem(rep + 0x10, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
        return rep + 0x10;
    }
    std::string string_at(uint32_t obj) const { return cstr(m, m.read32(obj)); }
    // Os bancos de teste no PS2: ABank::fgBanks e os ABank (+8 o banco do SND, +0x10 o índice).
    void put_banks(const TestBanks& b) {
        for (int i = 0; i < kBankCount; ++i) {
            const uint32_t a = kBankObjs + 0x18u * uint32_t(i);
            std::memset(m.mem(a, 0x18), 0, 0x18);
            m.write32(a + 8, uint32_t(b.snd[i]));
            m.write32(kBanks + 4u * uint32_t(i), a);
        }
        m.write32(kEmpty + 8, 0xffffffffu);  // o ABank::fgEmpty: sem banco do SND (o do porte também)
    }
    void begin_case() {
        ptrace_reset();
        heap = kHeap;
        log.clear();
        ids.clear();
        next_id = 0;
        sounds.clear();
        errors = 0;
    }
    void put_path(const drv::APath& p, int32_t view) {
        putf(kPath + 0x00, p.volume);
        putf(kPath + 0x04, p.pitch);
        putf(kPath + 0x08, p.azimuth);
        putf(kPath + 0x0c, p.facing);
        putf(kPath + 0x10, p.delay);
        putf(kPath + 0x14, p.distance);
        m.write32(kPath + 0x18, kListener);
        m.write32(kListener + 0x5c, uint32_t(view));
    }
};

// ---- o lado do porte -------------------------------------------------------------------------------------------------

// Os mesmos bancos no porte (ABank::fgBanks e o mapa do ABank::Get).
void port_banks(TestBanks& b) {
    drv::AudioInternals in = drv::audio_internals();
    for (int i = 0; i < kBankCount; ++i) {
        auto& slot = (*in.banks)[kBankNames[i]];
        if (!slot) slot = std::make_unique<drv::ABank>();
        drv::ABank& k = *slot;
        k.name = kBankNames[i];
        k.snd_bank = b.snd[i];
        k.index.by_name.clear();
        k.index.by_number.clear();
        for (const auto& kv : b.index[i]) {
            k.index.by_name[kv.first] = kv.second;
            k.index.by_number[kv.second] = kv.first;
        }
        in.categories[i] = &k;
    }
}
void port_banks_off() {
    drv::AudioInternals in = drv::audio_internals();
    for (int i = 0; i < kBankCount; ++i) {
        in.categories[i] = nullptr;
        in.banks->erase(kBankNames[i]);
    }
}

// As mixagens do porte com volumes sorteados, sem transição nem mestre (o gancho do original devolve o mesmo).
void port_mixes(Rng& r) {
    auto& mixes = *drv::audio_internals().mixes;
    for (const char* n : {"Smackables", "Weapons Init", "Machine Gun", "Tracking System", "Horns", "Player Car",
                          "World Sounds", "Shell Casings", "Menu Sounds", "Giotto", "Zz"}) {
        drv::AMix* mx = drv::AMix_Get(n);
        (void)mx;
    }
    for (auto& kv : mixes) {
        drv::AMix& mx = *kv.second;
        mx.transition = 0;
        mx.master = nullptr;
        mx.volume = r.chance(0.1) ? 0.0f : r.real(0, 1);
        mx.gain = 1.0f;
    }
}

drv::APath random_path(Rng& r) {
    drv::APath p;
    p.volume = r.chance(0.1) ? 0.0f : r.real(0, 1.2f);
    p.pitch = r.chance(0.05) ? 0.0f : r.real(0.3f, 2.5f);
    p.azimuth = r.real(-0.5f, 0.5f);
    p.facing = r.real(-1, 1);
    p.delay = r.real(0, 0.5f);
    p.distance = r.chance(0.3) ? r.real(0, 2.5f) : r.real(0, 400);
    return p;
}

// O registro do porte (um por caso: os números das vozes continuam de uma chamada para a outra, como os do original).
struct PortTrace {
    drv::AudioTrace& t = g_ptrace;
    PortTrace() {
        t.log.clear();
        drv::audio_trace() = &t;
    }
    ~PortTrace() { drv::audio_trace() = nullptr; }
};

bool same_log(const std::vector<std::string>& emu, const std::vector<std::string>& port, std::string& why) {
    const size_t n = std::max(emu.size(), port.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string a = i < emu.size() ? emu[i] : "(nada)";
        const std::string b = i < port.size() ? port[i] : "(nada)";
        if (a != b) {
            why = "linha " + std::to_string(i) + ": original \"" + a + "\", porte \"" + b + "\"";
            return false;
        }
    }
    return true;
}

// As vistas de uma voz emulada e da do porte (o timbre, o banco, o laço, o som, o fim).
bool same_voice(const Emu& e, uint32_t ev, const drv::AVoice* pv, const char* what, std::string& why) {
    if ((ev == 0) != (pv == nullptr)) {
        why = std::string(what) + ": original " + (ev ? "com" : "sem") + " voz, porte " + (pv ? "com" : "sem");
        return false;
    }
    if (ev == 0) return true;
    for (int i = 0; i < 3; ++i) {
        const uint32_t w = ev + 4 + 0x20u * uint32_t(i);
        const drv::AVoiceView& v = pv->views[i];
        if (int32_t(e.m.read32(w)) != v.patch || int32_t(e.m.read32(w + 4)) != v.bank ||
            int32_t(e.m.read32(w + 8)) != v.loop || int32_t(e.m.read32(w + 0xc)) != v.handle ||
            int32_t(e.m.read32(w + 0x1c)) != v.over) {
            why = std::string(what) + ": a vista " + std::to_string(i);
            return false;
        }
    }
    if (e.mixn(e.m.read32(ev)) != drv::audio_mix_name(pv->mix)) {
        why = std::string(what) + ": a mixagem da voz";
        return false;
    }
    return true;
}

// Uma voz já existente nos dois lados com o mesmo estado (as vistas acabadas e os sons sorteados).
void random_voice_state(Rng& r, Emu& e, uint32_t ev, drv::AVoice* pv) {
    for (int i = 0; i < 3; ++i) {
        const int32_t over = r.chance(0.4) ? 1 : 0, handle = r.chance(0.5) ? -1 : r.range(0, 30);
        const int32_t loop = r.chance(0.7) ? 1 : 0;
        const uint32_t w = ev + 4 + 0x20u * uint32_t(i);
        e.m.write32(w + 0x1c, uint32_t(over));
        e.m.write32(w + 0xc, uint32_t(handle));
        e.m.write32(w + 8, uint32_t(loop));
        pv->views[i].over = over;
        pv->views[i].handle = handle;
        pv->views[i].loop = loop;
    }
}

// Os campos do ABaseSound que os construtores escrevem.
bool same_base(const Emu& e, uint32_t es, const drv::ABaseSound& p, const char* what, std::string& why) {
    struct F {
        uint32_t off;
        float v;
        const char* n;
    };
    const F fs[] = {{0x5c, p.min_distance, "min"}, {0x60, p.max_distance, "max"}, {0x64, p.max_distance2, "max²"},
                    {0x68, p.falloff, "+0x68"},    {0x6c, p.volume, "volume"},      {0x70, p.pitch, "pitch"},
                    {0x28, p.dir[2], "dir.z"},     {0x38, p.axis1[2], "axis1.z"},   {0x48, p.axis2[2], "axis2.z"}};
    for (const F& f : fs)
        if (e.m.read32(es + f.off) != ps2::fbits(f.v)) {
            why = std::string(what) + ": o " + f.n + " (original " + hex(e.m.read32(es + f.off)) + ", porte " +
                  hex(ps2::fbits(f.v)) + ")";
            return false;
        }
    for (int i = 0; i < 3; ++i)
        if (e.m.read32(es + 0x74 + 4u * uint32_t(i)) != ps2::fbits(p.last_volume[i])) {
            why = std::string(what) + ": o último volume";
            return false;
        }
    if (e.m.read32(es + 0x88) != p.views || e.m.read32(es + 0x80) != uint32_t(p.disabled)) {
        why = std::string(what) + ": as vistas";
        return false;
    }
    if (e.mixn(e.m.read32(es + 0x84)) != drv::audio_mix_name(p.mix)) {
        why = std::string(what) + ": a mixagem";
        return false;
    }
    return true;
}

// Os sons do porte criados na conferência saem da lista (sem o registro).
void port_clear_sounds() {
    drv::AudioTrace* t = drv::audio_trace();
    drv::AudioTrace quiet;  // (a seco: as vozes não falam com o SND)
    drv::audio_trace() = &quiet;
    auto& l = drv::sound_list();
    while (!l.empty()) l.front()->destroy();
    // As vozes de aviso que o ~AWorldSound deixa (como no PS2) também saem a seco: os handles delas são sorteados, e
    // o SND do porte não está iniciado nesta parte (ver o run_sound_hud_checks).
    for (drv::AVoice* v : drv::world_leaked_voices()) delete v;
    drv::world_leaked_voices().clear();
    drv::voice_active_list().clear();
    drv::audio_trace() = t;
}

bool take_error(Emu& e, Tally& t, const std::string& what) {
    if (!e.errors) return false;
    t.fail(what + ": " + e.error);
    e.errors = 0;
    return true;
}


// ---- a) AMenuSound -------------------------------------------------------------------------------------------------

const char* const kMixNames[] = {"Smackables", "Weapons Init", "Machine Gun", "Tracking System", "Horns",
                                 "Menu Sounds", "Zz"};

uint32_t put_text(Emu& e, uint32_t at, const std::string& s) {
    std::memcpy(e.m.mem(at, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
    return at;
}

void check_menu(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tp, tt, tm;
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const int32_t bank = r.range(-1, 12), patch = r.chance(0.1) ? -1 : r.range(0, 0x7f);
        const std::string mixn = kMixNames[r.range(0, 6)];
        const int32_t view = r.range(0, 4);
        const uint32_t txt = put_text(e, kText, mixn);
        // O construtor.
        const uint32_t es = e.alloc(0x120);
        e.call(kFnMenuPrivCtor, {es, uint32_t(bank), uint32_t(patch), txt, uint32_t(view)});
        drv::AMenuSoundPriv* ps;
        std::vector<std::string> plog;
        {
            PortTrace t;
            ps = new drv::AMenuSoundPriv(bank, patch, mixn.c_str(), view);
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "AMenuSoundPriv::AMenuSoundPriv")) {
            if (!same_log(e.log, plog, why) || !same_base(e, es, *ps, "o som", why) ||
                !same_voice(e, es + 0xb0, &ps->voice, "a voz", why))
                tc.fail(why);
        }
        // O Play (a voz das vistas sorteadas; acabada, o som se apaga).
        random_voice_state(r, e, es + 0xb0, &ps->voice);
        const drv::APath path = random_path(r);
        drv::AListener lis;
        lis.view = 2;
        drv::APath pp = path;
        pp.listener = &lis;
        e.put_path(path, 2);
        e.log.clear();
        e.call(kFnMenuPrivPlay, {es, kPath});
        {
            PortTrace t;
            ps->Play(pp);
            plog = t.t.log;
        }
        ++tp.cases;
        if (!take_error(e, tp, "AMenuSoundPriv::Play") && !same_log(e.log, plog, why)) tp.fail(why);
        port_clear_sounds();
        // Os Trigger por categoria e por banco (o 0x002f4a20 gravado).
        const char* name = kNames[r.range(0, kNameCount - 1)];
        const uint32_t ntxt = put_text(e, kText + 0x40, name);
        const int32_t cat = r.range(0, kBankCount - 1);
        e.log.clear();
        e.call(kFnMenuTriggerCat, {uint32_t(cat), ntxt, txt, uint32_t(view)});
        {
            PortTrace t;
            drv::AMenuSound_TriggerCategory(cat, name, mixn.c_str(), view);
            plog = t.t.log;
        }
        ++tt.cases;
        if (!take_error(e, tt, "AMenuSound::Trigger(int, char *)") && !same_log(e.log, plog, why))
            tt.fail(std::string(name) + ": " + why);
        const std::string bname = r.chance(0.9) ? kBankNames[r.range(0, kBankCount - 1)] : "nenhum.bnk";
        const uint32_t btxt = put_text(e, kText + 0x80, bname);
        e.log.clear();
        e.call(kFnMenuTriggerBank, {btxt, ntxt, txt, uint32_t(view)});
        {
            PortTrace t;
            drv::AMenuSound_TriggerBank(bname.c_str(), name, mixn.c_str(), view);
            plog = t.t.log;
        }
        ++tt.cases;
        if (!take_error(e, tt, "AMenuSound::Trigger(char *, char *)") && !same_log(e.log, plog, why))
            tt.fail(bname + ": " + why);
        // O Trigger(int, int, ...): o operator new e o construtor com a vista 2 (a máscara das vistas tem o 4).
        e.m.unhook(kFnMenuTrigger);
        uint32_t got_view = 99, got_bank = 0, got_patch = 0;
        std::string got_mix;
        e.m.hook(kFnMenuPrivCtor, "AMenuSoundPriv::AMenuSoundPriv", [&](MipsMachine& mm) {
            got_bank = uint32_t(mm.gpr(R::a1));
            got_patch = uint32_t(mm.gpr(R::a2));
            got_mix = cstr(mm, uint32_t(mm.gpr(R::a3)));
            got_view = uint32_t(mm.gpr(R::t0));
        });
        e.call(kFnMenuTrigger, {uint32_t(bank), uint32_t(patch), txt, uint32_t(view)});
        e.m.unhook(kFnMenuPrivCtor);
        e.setup_menu_hook();
        drv::AMenuSoundPriv* made = drv::AMenuSound_Make(bank, patch, mixn.c_str(), view);
        ++tm.cases;
        if (!take_error(e, tm, "AMenuSound::Trigger(int, int)")) {
            if (got_bank != uint32_t(bank) || got_patch != uint32_t(patch) || got_mix != mixn || got_view != 2u ||
                made->views != (1u << 2) || made->voice.views[0].patch != (patch == -1 ? -1 : patch))
                tm.fail("a vista " + std::to_string(got_view));
        }
        port_clear_sounds();
    }
    port_banks_off();
    report("AMenuSoundPriv::AMenuSoundPriv", tc, failures);
    report("AMenuSoundPriv::Play", tp, failures);
    report("AMenuSound::Trigger (por categoria e por banco)", tt, failures);
    report("AMenuSound::Trigger(int, int, char *, AView)", tm, failures);
}

// ---- b) ATargeting, ACarAlarm, AZoomObj, ATurret, AGun ------------------------------------------------------------

void check_targeting(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, ts;
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const uint32_t et = e.alloc(0xc);
        e.m.write32(et, 0xdeadu);
        e.call(kFnTargetingCtor, {et});
        drv::ATargetingData pt;
        pt.state = 0xdead;
        std::vector<std::string> plog;
        {
            PortTrace t;
            drv::ATargeting_Construct(pt);
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "ATargeting::ATargeting")) {
            if (!same_log(e.log, plog, why)) tc.fail(why);
            else if (e.m.read32(et) != uint32_t(pt.state) || e.m.read32(et + 4) != uint32_t(pt.unknown04) ||
                     e.mixn(e.m.read32(et + 8)) != drv::audio_mix_name(nf::p32_ptr<drv::AMix>(pt.mix)))
                tc.fail("os campos");
        }
        for (int k = 0; k < 4; ++k) {
            const int32_t cur = r.range(-1, 3), want = r.chance(0.8) ? r.range(0, 2) : r.range(-1, 4);
            const int32_t paused = r.chance(0.1) ? 1 : 0;
            e.m.write32(et, uint32_t(cur));
            e.m.write32(kIsPaused, uint32_t(paused));
            pt.state = cur;
            drv::AStreamGlobalsView g = drv::astream_globals();
            g.is_paused = paused;
            drv::astream_set_globals(g);
            e.log.clear();
            e.call(kFnTargetingSet, {et, uint32_t(want)});
            {
                PortTrace t;
                drv::ATargeting_SetState(pt, want);
                plog = t.t.log;
            }
            ++ts.cases;
            if (take_error(e, ts, "ATargeting::SetState")) continue;
            if (!same_log(e.log, plog, why)) ts.fail(why);
            else if (e.m.read32(et) != uint32_t(pt.state)) ts.fail("o estado");
        }
    }
    drv::AStreamGlobalsView g = drv::astream_globals();
    g.is_paused = 0;
    drv::astream_set_globals(g);
    e.m.write32(kIsPaused, 0);
    port_banks_off();
    report("ATargeting::ATargeting", tc, failures);
    report("ATargeting::SetState (os bipes da trava)", ts, failures);
}

void check_alarm(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tp;
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const uint32_t ea = e.alloc(0x68);
        e.m.write32(ea + 0x64, 0x55aa55aau);  // o lixo do __builtin_new que o original passa ao AVoice::AVoice
        e.call(kFnAlarmCtor, {ea});
        std::unique_ptr<drv::ACarAlarm> pa;
        std::vector<std::string> plog;
        {
            PortTrace t;
            pa = std::make_unique<drv::ACarAlarm>();
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "ACarAlarm::ACarAlarm")) {
            // A voz do original leva a mixagem do lixo ("?" no registro), a do porte é construída sem mixagem.
            if (!same_log(e.log, plog, why)) tc.fail(why);
            else if (e.mixn(e.m.read32(ea + 0x64)) != drv::audio_mix_name(pa->mix)) tc.fail("a mixagem");
            else if (e.m.read32(ea) != 0x55aa55aau) tc.fail("a mixagem da voz (o lixo)");
        }
        e.m.write32(ea, e.mix("Horns"));
        random_voice_state(r, e, ea, &pa->voice);
        const drv::APath path = random_path(r);
        drv::AListener lis;
        lis.view = 2;
        drv::APath pp = path;
        pp.listener = &lis;
        e.put_path(path, 2);
        e.log.clear();
        e.call(kFnAlarmPlay, {ea, kPath});
        {
            PortTrace t;
            pa->Play(pp);
            plog = t.t.log;
        }
        ++tp.cases;
        if (!take_error(e, tp, "ACarAlarm::Play") && !same_log(e.log, plog, why)) tp.fail(why);
        {
            PortTrace t;
            pa.reset();
        }
    }
    port_banks_off();
    report("ACarAlarm::ACarAlarm", tc, failures);
    report("ACarAlarm::Play", tp, failures);
}

void check_zoom(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tz, tp, td;
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const uint32_t ez = e.alloc(0x10), owner = kScratch + 0x100;
        e.call(kFnZoomCtor, {ez, owner});
        std::unique_ptr<drv::AZoomObj> pz;
        std::vector<std::string> plog;
        {
            PortTrace t;
            pz = std::make_unique<drv::AZoomObj>(nullptr);
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "AZoomObj::AZoomObj")) {
            if (!same_log(e.log, plog, why) || !same_voice(e, e.m.read32(ez + 4), pz->voice, "a voz", why)) tc.fail(why);
            else if (e.mixn(e.m.read32(ez)) != drv::audio_mix_name(pz->mix) || e.m.read32(ez + 8) != owner ||
                     e.m.read32(ez + 0xc) != uint32_t(pz->in_zone))
                tc.fail("os campos");
        }
        for (int k = 0; k < 3; ++k) {
            const int32_t st = r.chance(0.5) ? 0 : r.range(1, 3), on = r.chance(0.5) ? 0 : r.range(1, 2);
            e.m.write32(ez + 0xc, uint32_t(st));
            pz->in_zone = st;
            e.log.clear();
            e.call(kFnZoomZone, {ez, uint32_t(on)});
            {
                PortTrace t;
                pz->InTheZone(on);
                plog = t.t.log;
            }
            ++tz.cases;
            if (take_error(e, tz, "AZoomObj::InTheZone")) continue;
            if (!same_log(e.log, plog, why)) tz.fail(why);
            else if (e.m.read32(ez + 0xc) != uint32_t(pz->in_zone)) tz.fail("o estado");
            port_mixes(r);
            // O Play.
            random_voice_state(r, e, e.m.read32(ez + 4), pz->voice);
            const drv::APath path = random_path(r);
            drv::AListener lis;
            lis.view = 2;
            drv::APath pp = path;
            pp.listener = &lis;
            e.put_path(path, 2);
            e.log.clear();
            e.call(kFnZoomPlay, {ez, kPath});
            {
                PortTrace t;
                pz->Play(pp);
                plog = t.t.log;
            }
            ++tp.cases;
            if (!take_error(e, tp, "AZoomObj::Play") && !same_log(e.log, plog, why)) tp.fail(why);
        }
        e.log.clear();
        e.call(kFnZoomDtor, {ez, 3});
        {
            PortTrace t;
            pz.reset();
            plog = t.t.log;
        }
        ++td.cases;
        if (!take_error(e, td, "AZoomObj::~AZoomObj") && !same_log(e.log, plog, why)) td.fail(why);
    }
    port_banks_off();
    report("AZoomObj::AZoomObj", tc, failures);
    report("AZoomObj::InTheZone (as mixagens da luneta)", tz, failures);
    report("AZoomObj::Play", tp, failures);
    report("AZoomObj::~AZoomObj", td, failures);
}

void check_turret(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tp, td;
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const uint32_t et = e.alloc(0x1c);
        e.call(kFnTurretCtor, {et});
        std::unique_ptr<drv::ATurret> pt;
        std::vector<std::string> plog;
        {
            PortTrace t;
            pt = std::make_unique<drv::ATurret>();
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "ATurret::ATurret")) {
            if (!same_log(e.log, plog, why)) tc.fail(why);
            else if (e.mixn(e.m.read32(et)) != drv::audio_mix_name(pt->mix) || e.m.read32(et + 4) != 0 ||
                     e.m.read32(et + 0x10) != 0)
                tc.fail("os campos");
        }
        for (int k = 0; k < 12; ++k) {
            // As vistas das vozes vivas sorteadas nos dois lados.
            const uint32_t ev[3] = {e.m.read32(et + 4), e.m.read32(et + 8), e.m.read32(et + 0xc)};
            drv::AVoice* pv[3] = {pt->servo_voice, pt->sideways_voice, pt->updown_voice};
            for (int i = 0; i < 3; ++i)
                if (ev[i] && pv[i]) random_voice_state(r, e, ev[i], pv[i]);
            const uint32_t side = r.chance(0.5) ? 1u : 0u, ud = r.chance(0.5) ? 1u : 0u, servo = r.chance(0.5) ? 1u : 0u;
            const drv::APath path = random_path(r);
            drv::AListener lis;
            lis.view = 2;
            drv::APath pp = path;
            pp.listener = &lis;
            e.put_path(path, 2);
            e.log.clear();
            e.call(kFnTurretPlay, {et, side, ud, servo, kPath});
            {
                PortTrace t;
                pt->Play(int32_t(side), int32_t(ud), int32_t(servo), pp);
                plog = t.t.log;
            }
            ++tp.cases;
            if (take_error(e, tp, "ATurret::Play")) break;
            if (!same_log(e.log, plog, why)) {
                tp.fail(why);
                break;
            }
            const uint32_t ev2[3] = {e.m.read32(et + 4), e.m.read32(et + 8), e.m.read32(et + 0xc)};
            const drv::AVoice* pv2[3] = {pt->servo_voice, pt->sideways_voice, pt->updown_voice};
            bool ok = true;
            for (int i = 0; i < 3 && ok; ++i) ok = same_voice(e, ev2[i], pv2[i], "uma voz", why);
            if (ok && (e.m.read32(et + 0x10) != uint32_t(pt->servo) || e.m.read32(et + 0x14) != uint32_t(pt->sideways) ||
                       e.m.read32(et + 0x18) != uint32_t(pt->updown))) {
                ok = false;
                why = "os estados";
            }
            if (!ok) {
                tp.fail(why);
                break;
            }
        }
        e.log.clear();
        e.call(kFnTurretDtor, {et, 3});
        {
            PortTrace t;
            pt.reset();
            plog = t.t.log;
        }
        ++td.cases;
        if (!take_error(e, td, "ATurret::~ATurret") && !same_log(e.log, plog, why)) td.fail(why);
    }
    port_banks_off();
    report("ATurret::ATurret", tc, failures);
    report("ATurret::Play (sequências)", tp, failures);
    report("ATurret::~ATurret", td, failures);
}


// O dono de um AGun no porte (o IsTracked sorteado).
class TestOwner : public drv::ASceneObj {
public:
    bool tracked = false;
    TestOwner() : ASceneObj("Zz") {}
    bool IsTracked() const override { return tracked; }
    void Play(drv::APath&) override {}
};

constexpr uint32_t kVtSceneObj = 0x3c4438, kVtTraffic = 0x3c2e38;

void put_random_words(Emu& e, Rng& r) {
    uint32_t* w = drv::audio_internals().random;
    for (int i = 0; i < 6; ++i) {
        w[i] = r.next() ^ (r.next() << 11);
        e.m.write32(kRandom + 4u * uint32_t(i), w[i]);
    }
}
bool same_words(const Emu& e) {
    const uint32_t* w = drv::audio_internals().random;
    for (int i = 0; i < 6; ++i)
        if (e.m.read32(kRandom + 4u * uint32_t(i)) != w[i]) return false;
    return true;
}

// Os AOneShotSound que o AGun criou (os do original pelos ganchos, na ordem; os do porte no começo da lista).
bool same_oneshots(Emu& e, const std::vector<uint32_t>& emu, std::string& why) {
    std::vector<drv::ABaseSound*> port;
    for (drv::ABaseSound* s : drv::sound_list())
        if (dynamic_cast<drv::AOneShotSound*>(s)) port.push_back(s);
    std::reverse(port.begin(), port.end());  // (o mais novo é o primeiro da lista)
    if (port.size() != emu.size()) {
        why = "os tiros: original " + std::to_string(emu.size()) + ", porte " + std::to_string(port.size());
        return false;
    }
    for (size_t i = 0; i < emu.size(); ++i) {
        const auto* o = static_cast<drv::AOneShotSound*>(port[i]);
        const uint32_t es = emu[i];
        if (!same_base(e, es, *o, "o tiro", why) || !same_voice(e, es + 0xb0, &o->voice, "a voz do tiro", why))
            return false;
        for (int c = 0; c < 3; ++c)
            if (e.m.read32(es + 4u * uint32_t(c)) != ps2::fbits(o->pos[c]) ||
                e.m.read32(es + 0x10 + 4u * uint32_t(c)) != ps2::fbits(o->vel[c])) {
                why = "a posição do tiro";
                return false;
            }
        if (e.m.read32(es + 0x114) != ps2::fbits(o->fx) || e.m.read32(es + 0x118) != ps2::fbits(o->lifetime)) {
            why = "o efeito e a vida do tiro";
            return false;
        }
    }
    return true;
}

void check_gun(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tf, tp, td;
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        // O dono: um ASceneObj ou um ATrafficVehicle (o IsTracked: o tipo 5).
        const bool traffic = r.chance(0.5);
        const bool tracked = traffic && r.chance(0.5);
        const uint32_t eo = e.alloc(0x270);
        e.m.write32(eo + 0xac, traffic ? kVtTraffic : kVtSceneObj);
        e.m.write32(eo + 0xd0, tracked ? 5u : 2u);
        const int32_t loops = r.chance(0.5) ? 1 : 0;
        e.m.write32(eo + 0xc4, uint32_t(loops));
        TestOwner* po = new TestOwner();
        po->tracked = tracked;
        po->unknown_c4 = loops;
        const std::string mixn = r.chance(0.5) ? "Machine Gun" : kMixNames[r.range(0, 6)];
        const uint32_t txt = put_text(e, kText, mixn);
        const uint32_t eg = e.alloc(0x34);
        e.call(kFnGunCtor, {eg, eo, txt});
        std::unique_ptr<drv::AGun> pg;
        std::vector<std::string> plog;
        {
            PortTrace t;
            pg = std::make_unique<drv::AGun>(po, mixn.c_str());
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        auto fields = [&]() -> bool {
            if (e.m.read32(eg) != eo || e.m.read32(eg + 4) != kBankObjs + 0x18u * 4u ||
                e.mixn(e.m.read32(eg + 8)) != drv::audio_mix_name(pg->mix)) {
                why = "o dono, o banco ou a mixagem";
                return false;
            }
            const uint32_t fv[5] = {ps2::fbits(pg->fire_timer), ps2::fbits(pg->sustain), ps2::fbits(pg->unknown18),
                                    ps2::fbits(pg->fire_interval), ps2::fbits(pg->unknown20)};
            for (int i = 0; i < 5; ++i)
                if (e.m.read32(eg + 0x10 + 4u * uint32_t(i)) != fv[i]) {
                    why = "o +0x" + hex(0x10 + 4 * i).substr(6);
                    return false;
                }
            if (e.string_at(eg + 0x24) != pg->fire_patch || e.string_at(eg + 0x28) != pg->shell_patch ||
                e.string_at(eg + 0x2c) != pg->mix_name || e.m.read32(eg + 0x30) != uint32_t(pg->shells)) {
                why = "as cadeias";
                return false;
            }
            return same_voice(e, e.m.read32(eg + 0xc), pg->voice, "a voz", why);
        };
        if (!take_error(e, tc, "AGun::AGun") && (!same_log(e.log, plog, why) || !fields())) tc.fail(why);
        // O SetFirePatch (a cadeia por valor: uma representação com uma referência).
        if (r.chance(0.5)) {
            const std::string patch = r.chance(0.8) ? kNames[r.range(0, kNameCount - 1)] : "";
            const uint32_t arg = e.alloc(4);
            e.m.write32(arg, e.new_string(patch));
            e.log.clear();
            e.call(kFnGunPatch, {eg, arg});
            {
                PortTrace t;
                pg->SetFirePatch(patch);
                plog = t.t.log;
            }
            ++tf.cases;
            if (!take_error(e, tf, "AGun::SetFirePatch") && (!same_log(e.log, plog, why) || !fields())) tf.fail(why);
        }
        // O Play em sequência (atirando ou não; o tempo do som de 1 a 3 passos).
        for (int k = 0; k < 16; ++k) {
            if (r.chance(0.3)) {
                const int32_t sh = r.chance(0.5) ? 1 : 0;
                e.m.write32(eg + 0x30, uint32_t(sh));
                pg->shells = sh;
            }
            for (int i = 0; i < 3; ++i) {
                const float p = r.real(-500, 500), v = r.real(-30, 30);
                e.putf(eo + 4u * uint32_t(i), p);
                e.putf(eo + 0x10 + 4u * uint32_t(i), v);
                po->pos[i] = p;
                po->vel[i] = v;
            }
            put_random_words(e, r);
            if (e.m.read32(eg + 0xc) && pg->voice) random_voice_state(r, e, e.m.read32(eg + 0xc), pg->voice);
            const float frames = float(r.range(1, 3));
            const uint32_t firing = r.chance(0.6) ? 1u : r.range(0, 2) == 0 ? 0u : 2u;
            const drv::APath path = random_path(r);
            drv::AListener lis;
            lis.view = 2;
            drv::APath pp = path;
            pp.listener = &lis;
            e.put_path(path, 2);
            e.log.clear();
            std::vector<uint32_t> before(e.sounds.begin(), e.sounds.end());
            e.m.set_fpr(12, frames);
            e.call(kFnGunPlay, {eg, firing, kPath});
            {
                PortTrace t;
                pg->Play(frames, int32_t(firing), pp);
                plog = t.t.log;
            }
            ++tp.cases;
            if (take_error(e, tp, "AGun::Play")) break;
            std::vector<uint32_t> made;
            for (uint32_t s : e.sounds)
                if (std::find(before.begin(), before.end(), s) == before.end()) made.push_back(s);
            std::sort(made.begin(), made.end());  // (o heap cresce: a ordem da criação)
            bool ok = same_log(e.log, plog, why) && fields() && same_oneshots(e, made, why);
            if (ok && !same_words(e)) {
                ok = false;
                why = "o random";
            }
            {
                // Os tiros saem dos dois lados (o dono fica).
                drv::AudioTrace* saved = drv::audio_trace();
                drv::AudioTrace quiet;
                drv::audio_trace() = &quiet;
                std::vector<drv::ABaseSound*> shots;
                for (drv::ABaseSound* s : drv::sound_list())
                    if (dynamic_cast<drv::AOneShotSound*>(s)) shots.push_back(s);
                for (drv::ABaseSound* s : shots) s->destroy();
                drv::audio_trace() = saved;
                for (uint32_t s : made) e.sounds.erase(s);
            }
            if (!ok) {
                tp.fail(why);
                break;
            }
        }
        e.log.clear();
        e.call(kFnGunDtor, {eg, 3});
        {
            PortTrace t;
            pg.reset();
            plog = t.t.log;
        }
        ++td.cases;
        if (!take_error(e, td, "AGun::~AGun") && !same_log(e.log, plog, why)) td.fail(why);
        port_clear_sounds();
    }
    port_banks_off();
    report("AGun::AGun", tc, failures);
    report("AGun::SetFirePatch", tf, failures);
    report("AGun::Play (sequências, os tiros e os cartuchos)", tp, failures);
    report("AGun::~AGun", td, failures);
}

// ---- c) ASmackable ---------------------------------------------------------------------------------------------------

int32_t random_id(Rng& r, const TestBanks& tb) {
    if (r.chance(0.2)) return -1;
    const int b = r.range(0, kBankCount - 1);
    return int32_t(uint32_t(tb.snd[b]) << 7) | r.range(0, 0x7f);
}

void check_smackable(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tl, tk, ta, tp, tt, td;
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        int32_t ids[4];
        for (int32_t& i : ids) i = random_id(r, tb);
        const std::string mixn = r.chance(0.7) ? "Smackables" : kMixNames[r.range(0, 6)];
        const uint32_t txt = put_text(e, kText, mixn);
        const uint32_t es = e.alloc(0xf0);
        e.call(kFnSmackCtor, {es, uint32_t(ids[0]), uint32_t(ids[1]), uint32_t(ids[2]), uint32_t(ids[3]), txt});
        drv::ASmackable* ps;
        std::vector<std::string> plog;
        {
            PortTrace t;
            ps = new drv::ASmackable(ids[0], ids[1], ids[2], ids[3], mixn.c_str());
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        auto fields = [&]() -> bool {
            if (!same_base(e, es, *ps, "o som", why)) return false;
            for (int i = 0; i < 3; ++i)
                if (e.m.read32(es + 0xd8 + 4u * uint32_t(i)) != uint32_t(ps->ids[i])) {
                    why = "os timbres do choque";
                    return false;
                }
            if (e.m.read32(es + 0xe4) != uint32_t(ps->roll_frames) || e.m.read32(es + 0xe8) != ps2::fbits(ps->roll_volume) ||
                e.m.read32(es + 0xec) != uint32_t(ps->detached)) {
                why = "o rolar (" + std::to_string(int32_t(e.m.read32(es + 0xe4))) + "/" +
                      std::to_string(ps->roll_frames) + ")";
                return false;
            }
            return same_voice(e, e.m.read32(es + 0xd0), ps->collision_voice, "a voz do choque", why) &&
                   same_voice(e, e.m.read32(es + 0xd4), ps->roll_voice, "a voz do rolar", why);
        };
        if (!take_error(e, tc, "ASmackable::ASmackable") && (!same_log(e.log, plog, why) || !fields())) tc.fail(why);
        bool alive = true;
        for (int k = 0; k < 24 && alive; ++k) {
            const int32_t count = r.range(1, 3);
            e.m.write32(kSoundCount, uint32_t(count));
            drv::sound_count() = count;
            for (int i = 0; i < 3; ++i) {
                const float v = r.chance(0.3) ? r.real(-1, 1) : r.real(-15, 15);
                e.putf(es + 0x10 + 4u * uint32_t(i), v);
                ps->vel[i] = v;
            }
            if (e.m.read32(es + 0xd0) && ps->collision_voice) random_voice_state(r, e, e.m.read32(es + 0xd0), ps->collision_voice);
            const int op = r.range(0, 9);
            e.log.clear();
            const char* what = "";
            Tally* t = nullptr;
            if (op <= 2) {
                // PlayCollision (a força, o 0x00355ea0)
                const float force = r.chance(0.5) ? r.real(0, 0.75f) : r.real(0.7f, 1.0f);
                const int32_t hard = r.range(0, 1);
                e.m.write32(kNextHard, uint32_t(hard));
                drv::asmackable_next_hard() = hard;
                e.m.set_fpr(12, force);
                e.call(kFnSmackCollision, {es, kScratch, kScratch + 0x10, uint32_t(r.range(0, 15))});
                PortTrace tr;
                ps->PlayCollision(force, nullptr, nullptr, 0);
                plog = tr.t.log;
                what = "ASmackable::PlayCollision";
                t = &tk;
                if (e.m.read32(kNextHard) != uint32_t(drv::asmackable_next_hard())) why = "o 0x00355ea0";
            } else if (op == 3) {
                if (r.chance(0.5)) {
                    e.m.write32(es + 0xe4, uint32_t(r.range(0, 12)));
                    ps->roll_frames = int32_t(e.m.read32(es + 0xe4));
                }
                e.m.set_fpr(12, r.real(0, 1));
                e.call(kFnSmackLanding, {es, kScratch, kScratch + 0x10});
                PortTrace tr;
                ps->PlayLanding(0, nullptr, nullptr);
                plog = tr.t.log;
                what = "ASmackable::PlayLanding";
                t = &tl;
            } else if (op == 4) {
                const uint32_t got = e.call(kFnSmackActive, {es}) ? uint32_t(e.m.gpr(R::v0)) & 0xff : 9u;
                plog.clear();
                what = "ASmackable::IsStillActive";
                t = &ta;
                if ((got != 0) != ps->IsStillActive()) why = "a resposta";
            } else if (op == 5) {
                // Detatch: o objeto some se a voz acabou (ou não há).
                e.m.write32(kViewMask, 4);
                e.call(kFnSmackDetatch, {es});
                bool deleted;
                {
                    PortTrace tr;
                    const size_t n = drv::sound_list().size();
                    ps->Detatch();
                    deleted = drv::sound_list().size() != n;
                    plog = tr.t.log;
                }
                what = "ASmackable::Detatch";
                t = &tt;
                if (deleted) alive = false;
            } else {
                const drv::APath path = random_path(r);
                drv::AListener lis;
                lis.view = 2;
                drv::APath pp = path;
                pp.listener = &lis;
                e.put_path(path, 2);
                if (r.chance(0.2)) {
                    e.m.write32(es + 0xe8, ps2::fbits(r.real(0, 1)));
                    ps->roll_volume = e.getf(es + 0xe8);
                }
                if (r.chance(0.2)) {
                    e.m.write32(es + 0xe4, uint32_t(r.range(0, 12)));
                    ps->roll_frames = int32_t(e.m.read32(es + 0xe4));
                }
                if (r.chance(0.1)) {
                    e.m.write32(es + 0xec, 1);
                    ps->detached = 1;
                }
                e.call(kFnSmackPlay, {es, kPath});
                bool deleted;
                {
                    PortTrace tr;
                    const size_t n = drv::sound_list().size();
                    ps->Play(pp);
                    deleted = drv::sound_list().size() != n;
                    plog = tr.t.log;
                }
                what = "ASmackable::Play";
                t = &tp;
                if (deleted) alive = false;
            }
            ++t->cases;
            if (take_error(e, *t, what)) break;
            if (!why.empty() || !same_log(e.log, plog, why) || (alive && !fields())) {
                t->fail(why);
                why.clear();
                break;
            }
        }
        if (alive) {
            e.log.clear();
            e.call(kFnSmackDtor, {es, 3});
            {
                PortTrace tr;
                ps->destroy();
                plog = tr.t.log;
            }
            ++td.cases;
            if (!take_error(e, td, "ASmackable::~ASmackable") && !same_log(e.log, plog, why)) td.fail(why);
        }
        port_clear_sounds();
    }
    e.m.write32(kNextHard, 0);
    drv::asmackable_next_hard() = 0;
    port_banks_off();
    report("ASmackable::ASmackable", tc, failures);
    report("ASmackable::PlayLanding", tl, failures);
    report("ASmackable::PlayCollision", tk, failures);
    report("ASmackable::IsStillActive", ta, failures);
    report("ASmackable::Play (o rolar, o choque, o objeto solto)", tp, failures);
    report("ASmackable::Detatch", tt, failures);
    report("ASmackable::~ASmackable", td, failures);
}

// O fim do Smackable::Simulate (0x0019e718): a posição e a velocidade do corpo no objeto de som e o sono pelo
// IsStillActive (o ASmackable original da tabela 0x003c4220), a animação e o estado do corpo; o GoToSleep e o
// PhysicsObject::Simulate nos ganchos.
void check_smackable_simulate(Emu& e, Rng& r, int cases, int& failures) {
    Tally t;
    int sleeps = 0, sims = 0;
    uint32_t body = 0;
    uint32_t anim = 0;
    e.m.hook(kFnGetRigidBody, "Simulation::GetRigidBody", [&body](MipsMachine& mm) { mm.set_gpr(R::v0, body); });
    e.m.hook(kFnAnySystemPlaying, "RAnimEngine::Handle::AnySystemPlaying",
             [&anim](MipsMachine& mm) { mm.set_gpr(R::v0, anim); });
    e.m.hook(kFnGoToSleep, "Smackable::GoToSleep", [&sleeps](MipsMachine&) { ++sleeps; });
    e.m.hook(kFnPhysSimulate, "PhysicsObject::Simulate", [&sims](MipsMachine&) { ++sims; });
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        body = e.alloc(0x80);
        const uint32_t obj = e.alloc(0x88), snd = e.alloc(0xf0), render = e.alloc(0x40);
        float pos[4], vel[4];
        for (int i = 0; i < 4; ++i) {
            pos[i] = r.real(-500, 500);
            vel[i] = r.chance(0.3) ? r.real(-0.05f, 0.05f) : r.real(-5, 5);
            e.putf(body + 0x10 + 4u * uint32_t(i), pos[i]);
            e.putf(body + 0x20 + 4u * uint32_t(i), vel[i]);
        }
        const uint8_t state = uint8_t(r.range(0, 3));
        e.m.mem(body + 0x6e, 1)[0] = state;
        const bool has_sound = r.chance(0.8), voice = r.chance(0.5), has_render = r.chance(0.5);
        anim = r.chance(0.3) ? 1u : 0u;
        e.m.write32(snd + 0xac, 0x3c4220);
        e.m.write32(snd + 0xd0, voice ? 0x1234u : 0u);
        e.m.write32(obj + 0x4c, has_sound ? snd : 0u);
        e.m.write32(obj + 0x48, has_render ? render : 0u);
        e.m.write32(render + 0x28, 0x777);
        e.m.write32(obj + 0x46, 3);
        const uint32_t delay = uint32_t(r.range(0, 3)), awake = uint32_t(r.range(0, 700));
        e.m.write32(obj + 0x6c, delay);
        e.m.write32(obj + 0x70, awake);
        sleeps = sims = 0;
        e.call(kFnSmackableSimulate, {obj});
        ++t.cases;
        if (take_error(e, t, "Smackable::Simulate")) continue;
        // O porte: o objeto de som do porte com a voz do choque (ou não) e o passo do sono.
        drv::SmackableData d{};
        d.sleep_delay = delay;
        drv::ASmackable* ps = new drv::ASmackable(-1, -1, -1, -1, "Smackables");
        if (voice) ps->collision_voice = new drv::AVoice(ps->mix, -1, -1);
        const uint32_t h = nf::p32_u(ps);
        drv::ASmackable_Feed(h, pos, vel);
        const bool active = has_sound && drv::ASmackable_IsStillActive(h);
        const bool go = drv::Smackable_SleepStep(d, state, has_render && anim != 0, active);
        std::string why;
        if ((sleeps != 0) != go) why = "o GoToSleep";
        else if (sims != 1) why = "o PhysicsObject::Simulate";
        else if (e.m.read32(obj + 0x6c) != d.sleep_delay) why = "o atraso";
        else if (has_sound) {
            for (int i = 0; i < 3 && why.empty(); ++i)
                if (e.m.read32(snd + 4u * uint32_t(i)) != ps2::fbits(ps->pos[i]) ||
                    e.m.read32(snd + 0x10 + 4u * uint32_t(i)) != ps2::fbits(ps->vel[i]))
                    why = "a posição no som";
        }
        if (!why.empty()) t.fail(why);
        port_clear_sounds();
    }
    for (uint32_t f : {kFnGetRigidBody, kFnAnySystemPlaying, kFnGoToSleep, kFnPhysSimulate}) e.m.unhook(f);
    report("Smackable::Simulate (o som e o sono)", t, failures);
}

// ---- d) AWorldSound e WSoundGroup ----------------------------------------------------------------------------------

bool same_world(const Emu& e, uint32_t es, const drv::AWorldSound& w, std::string& why) {
    const uint32_t f[] = {ps2::fbits(w.min_delay), ps2::fbits(w.max_delay), ps2::fbits(w.min_repeat),
                          ps2::fbits(w.max_repeat), uint32_t(w.pan_mode), uint32_t(w.added),
                          ps2::fbits(w.next_time), ps2::fbits(w.repeats), uint32_t(w.waiting)};
    for (int i = 0; i < 9; ++i)
        if (e.m.read32(es + 0x114 + 4u * uint32_t(i)) != f[i]) {
            why = "o +0x" + hex(0x114 + 4 * i).substr(5) + " (original " + hex(e.m.read32(es + 0x114 + 4u * uint32_t(i))) +
                  ", porte " + hex(f[i]) + ")";
            return false;
        }
    if (e.m.read32(es + 0x13c) != uint32_t(w.pass_frames)) {
        why = "os quadros da passagem";
        return false;
    }
    return same_voice(e, es + 0xb0, &w.voice, "a voz", why) &&
           same_voice(e, e.m.read32(es + 0x138), w.warning, "a voz de aviso", why);
}

void check_world(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tp, tw, ts;
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const int32_t bank = r.range(-1, 12), patch = r.chance(0.1) ? -1 : r.range(0, 0x7f);
        const std::string mixn = r.chance(0.7) ? "World Sounds" : kMixNames[r.range(0, 6)];
        const uint32_t txt = put_text(e, kText, mixn);
        const uint32_t es = e.alloc(0x140);
        e.call(kFnWorldCtor, {es, uint32_t(bank), uint32_t(patch), txt});
        drv::AWorldSound* pw;
        std::vector<std::string> plog;
        {
            PortTrace t;
            pw = new drv::AWorldSound(bank, patch, mixn.c_str());
            plog = t.t.log;
        }
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "AWorldSound::AWorldSound") &&
            (!same_log(e.log, plog, why) || !same_base(e, es, *pw, "o som", why) || !same_world(e, es, *pw, why)))
            tc.fail(why);
        float now = r.real(0, 100);
        for (int k = 0; k < 24; ++k) {
            const int op = r.range(0, 9);
            e.log.clear();
            if (op == 0) {
                const int32_t id = random_id(r, tb);
                e.call(kFnWorldWarning, {es, uint32_t(id)});
                PortTrace t;
                pw->SetWarningVoice(id);
                plog = t.t.log;
                ++tw.cases;
                if (take_error(e, tw, "AWorldSound::SetWarningVoice")) break;
                if (!same_log(e.log, plog, why) || !same_world(e, es, *pw, why)) {
                    tw.fail(why);
                    break;
                }
                continue;
            }
            if (op == 1) {
                const float a = r.chance(0.2) ? 0.0f : r.real(0, 10), b = r.chance(0.2) ? 0.0f : r.real(0, 20);
                const float c1 = r.real(0, 3), c2 = r.real(0, 5);
                e.m.set_fpr(12, a);
                e.m.set_fpr(13, b);
                e.call(kFnWorldTiming, {es});
                e.m.set_fpr(12, c1);
                e.m.set_fpr(13, c2);
                e.call(kFnWorldRepeat, {es});
                pw->SetTiming(a, b);
                pw->SetRepeat(c1, c2);
                ++ts.cases;
                if (take_error(e, ts, "AWorldSound::SetTiming/SetRepeat")) break;
                if (!same_world(e, es, *pw, why)) {
                    ts.fail(why);
                    break;
                }
                continue;
            }
            // O Play: o tempo, o estado sorteado às vezes, as vistas, a posição, a velocidade e o carro do ouvinte.
            if (r.chance(0.3)) {
                const int32_t wt = r.chance(0.5) ? 1 : 0;
                const float rep = r.chance(0.5) ? 0.0f : r.real(0, 3);
                const float nt = r.chance(0.3) ? 0.0f : r.real(now - 5, now + 5);
                const int32_t pan = r.chance(0.5) ? 1 : 0;
                e.m.write32(es + 0x134, uint32_t(wt));
                e.putf(es + 0x130, rep);
                e.putf(es + 0x12c, nt);
                e.m.write32(es + 0x124, uint32_t(pan));
                pw->waiting = wt;
                pw->repeats = rep;
                pw->next_time = nt;
                pw->pan_mode = pan;
            }
            now = ps2::ee::add(now, r.real(0, 0.2f));
            e.putf(kSoundTime, now);
            drv::sound_time() = now;
            const int32_t count = r.range(1, 3);
            e.m.write32(kSoundCount, uint32_t(count));
            drv::sound_count() = count;
            random_voice_state(r, e, es + 0xb0, &pw->voice);
            if (pw->warning) random_voice_state(r, e, e.m.read32(es + 0x138), pw->warning);
            put_random_words(e, r);
            drv::AListener lis;
            lis.view = 2;
            for (int i = 0; i < 3; ++i) {
                const float p = r.real(-50, 50), v = r.real(-3, 3), car = r.real(-50, 50);
                e.putf(es + 4u * uint32_t(i), p);
                e.putf(es + 0x10 + 4u * uint32_t(i), v);
                pw->pos[i] = p;
                pw->vel[i] = v;
                lis.car[i] = car;
                e.putf(kListener + 0x60 + 4u * uint32_t(i), car);
            }
            e.m.write32(es + 0xc, 0x3f800000u);  // o w da posição (que a cópia leva)
            const drv::APath path = random_path(r);
            drv::APath pp = path;
            pp.listener = &lis;
            e.put_path(path, 2);
            e.call(kFnWorldPlay, {es, kPath});
            {
                PortTrace t;
                pw->Play(pp);
                plog = t.t.log;
            }
            ++tp.cases;
            if (take_error(e, tp, "AWorldSound::Play")) break;
            if (!same_log(e.log, plog, why) || !same_world(e, es, *pw, why) || !same_words(e)) {
                if (why.empty()) why = "o random";
                tp.fail(why);
                break;
            }
        }
        port_clear_sounds();
    }
    port_banks_off();
    report("AWorldSound::AWorldSound", tc, failures);
    report("AWorldSound::Play (as repetições, a voz de aviso)", tp, failures);
    report("AWorldSound::SetWarningVoice", tw, failures);
    report("AWorldSound::SetTiming e SetRepeat", ts, failures);
}

// O map<int, WSound *> do original (a árvore do GCC 2.95: o cabeçalho no +0xc, nós de 0x18 com a chave no +0x10 e o
// valor no +0x14), em ordem.
std::vector<std::pair<int32_t, uint32_t>> emu_map(const Emu& e, uint32_t group) {
    std::vector<std::pair<int32_t, uint32_t>> out;
    const uint32_t tree = e.m.read32(group + 0xc), header = e.m.read32(tree);
    uint32_t n = e.m.read32(header + 8);  // o mais à esquerda
    for (int guard = 0; n != header && guard < 4096; ++guard) {
        out.emplace_back(int32_t(e.m.read32(n + 0x10)), e.m.read32(n + 0x14));
        uint32_t right = e.m.read32(n + 0xc);
        if (right != 0) {
            n = right;
            while (e.m.read32(n + 8) != 0) n = e.m.read32(n + 8);
        } else {
            uint32_t p = e.m.read32(n + 4);
            while (n == e.m.read32(p + 0xc)) {
                n = p;
                p = e.m.read32(p + 4);
            }
            if (e.m.read32(n + 0xc) != p) n = p;
        }
    }
    return out;
}

void check_group(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tg;
    for (int c = 0; c < cases / 8 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const uint32_t eg = e.alloc(0x10);
        e.call(kFnGroupCtor, {eg});
        drv::WSoundGroup pg;
        std::string why;
        bool ok = !take_error(e, tg, "WSoundGroup::WSoundGroup");
        for (int k = 0; k < 30 && ok; ++k) {
            const int op = r.range(0, 9);
            e.log.clear();
            std::vector<std::string> plog;
            const int32_t id = r.range(0, 12);
            {
                PortTrace t;
                if (op <= 3) {
                    const int32_t bank = r.range(-1, 12), patch = r.chance(0.1) ? -1 : r.range(0, 9);
                    e.call(kFnGroupAdd3, {eg, uint32_t(id), uint32_t(bank), uint32_t(patch)});
                    pg.Add(id, bank, patch);
                } else if (op <= 5) {
                    const int32_t packed = int32_t(uint32_t(r.range(0, 12)) << 7) | r.range(0, 9);
                    e.call(kFnGroupAdd2, {eg, uint32_t(id), uint32_t(packed)});
                    pg.Add(id, packed);
                } else if (op == 6) {
                    std::string text = std::string(kBankNames[r.range(0, kBankCount - 1)]) + ":" +
                                       (r.chance(0.5) ? " " : "") + kNames[r.range(0, kNameCount - 1)];
                    if (r.chance(0.1)) text = r.chance(0.5) ? "semdoispontos" : ": SFX_x";
                    const uint32_t txt = put_text(e, kText, text);
                    e.call(kFnGroupAddText, {eg, uint32_t(id), txt});
                    pg.Add(id, text.c_str());
                } else if (op == 7) {
                    e.call(kFnGroupStart, {eg});
                    pg.Start();
                } else if (op == 8) {
                    e.call(kFnGroupEnd, {eg});
                    pg.End();
                } else if (r.chance(0.3)) {
                    e.call(kFnGroupClear, {eg});
                    pg.Clear();
                } else {
                    continue;
                }
                plog = t.t.log;
            }
            ++tg.cases;
            if (take_error(e, tg, "WSoundGroup (op " + std::to_string(op) + ")")) break;
            if (!same_log(e.log, plog, why)) {
                tg.fail("op " + std::to_string(op) + ": " + why);
                break;
            }
            const auto em = emu_map(e, eg);
            if (em.size() != pg.sounds.size()) {
                tg.fail("o tamanho do mapa");
                break;
            }
            size_t i = 0;
            for (const auto& kv : pg.sounds) {
                const uint32_t ew = em[i].second;
                if (em[i].first != kv.first || e.m.read32(ew + 0x128) != uint32_t(kv.second->added) ||
                    !same_voice(e, ew + 0xb0, &kv.second->voice, "a voz", why) ||
                    e.m.read32(ew + 0x140) != kv.second->path_handle) {
                    if (why.empty()) why = "o som " + std::to_string(kv.first);
                    ok = false;
                    break;
                }
                ++i;
            }
            if (!ok) tg.fail(why);
        }
        {
            PortTrace t;
            pg.Clear();
        }
        port_clear_sounds();
    }
    port_banks_off();
    report("WSoundGroup (Start, Add x3, End, Clear: a árvore do original)", tg, failures);
}

// ---- e) APlayerHeli e os AVehicle::Activate* ------------------------------------------------------------------------

void check_heli(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally tc, tp, ta;
    std::vector<std::string> calls;
    e.m.hook(kFnVehicleCtor, "AVehicle::AVehicle", [&calls](MipsMachine& mm) {
        calls.push_back("vehicle " + cstr(mm, uint32_t(mm.gpr(R::a1))) + " " + std::to_string(int32_t(mm.gpr(R::a2))));
        mm.write32(uint32_t(mm.gpr(R::a0)) + 0x84, 0);
    });
    e.m.hook(kFnActivateGun, "AVehicle::ActivateGun",
             [&calls](MipsMachine& mm) { calls.push_back("gun " + cstr(mm, uint32_t(mm.gpr(R::a1)))); });
    e.m.hook(kFnActivateZoom, "AVehicle::ActivateZoom", [&calls](MipsMachine&) { calls.push_back("zoom"); });
    e.m.hook(kFnActivateWind, "AVehicle::ActivateWind", [&calls](MipsMachine&) { calls.push_back("wind"); });
    e.m.hook(kFnVehiclePlay, "AVehicle::Play", [](MipsMachine&) {});
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        port_mixes(r);
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        calls.clear();
        const uint32_t eh = e.alloc(0x230);
        e.call(kFnHeliCtor, {eh});
        drv::APlayerHeli* ph = new drv::APlayerHeli();
        ++tc.cases;
        std::string why;
        if (!take_error(e, tc, "APlayerHeli::APlayerHeli")) {
            // O que o construtor do APlayerHeli faz (o AVehicle::AVehicle e os Activate nos ganchos).
            const std::vector<std::string> want = {"vehicle Player Car 13", "gun Machine Gun", "zoom", "wind"};
            bool ok = calls == want;
            if (!ok) why = "as chamadas";
            for (int i = 0; i < 3 && ok; ++i)
                ok = same_voice(e, e.m.read32(eh + 0x210 + 4u * uint32_t(i)), ph->change[i], "SFX_helichange", why) &&
                     same_voice(e, e.m.read32(eh + 0x21c + 4u * uint32_t(i)), ph->rotor[i], "SFX_heli", why);
            if (ok && (e.m.read32(eh + 0x60) != ps2::fbits(ph->max_distance) ||
                       e.m.read32(eh + 0x64) != ps2::fbits(ph->max_distance2) || e.m.read32(eh + 0xd0) != 2u ||
                       e.m.read32(eh + 0x228) != ps2::fbits(ph->blend))) {
                ok = false;
                why = "os campos";
            }
            if (!ok) tc.fail(why);
        }
        e.m.write32(eh + 0x84, e.mix(drv::audio_mix_name(ph->mix)));
        for (int k = 0; k < 8; ++k) {
            const float vol = r.real(0, 1.5f), pitch = r.real(0.5f, 2), blend = r.real(0, 1);
            e.putf(eh + 0x6c, vol);
            e.putf(eh + 0x70, pitch);
            e.putf(eh + 0x228, blend);
            ph->volume = vol;
            ph->pitch = pitch;
            ph->blend = blend;
            for (int i = 0; i < 3; ++i) {
                const float v = r.chance(0.3) ? r.real(-0.5f, 0.5f) : r.real(-30, 30);
                e.putf(eh + 0x10 + 4u * uint32_t(i), v);
                ph->vel[i] = v;
            }
            const drv::APath path = random_path(r);
            drv::AListener lis;
            lis.view = 2;
            drv::APath pp = path;
            pp.listener = &lis;
            e.put_path(path, 2);
            e.log.clear();
            e.call(kFnHeliPlay, {eh, kPath});
            std::vector<std::string> plog;
            {
                PortTrace t;
                // (as vozes do porte sem número: a primeira chamada as numera na ordem do original)
                for (drv::AVoice* v : ph->change) t.t.id(v);
                for (drv::AVoice* v : ph->rotor) t.t.id(v);
                ph->PlayOwn(pp);
                plog = t.t.log;
            }
            // Os números das vozes do original na mesma ordem.
            std::map<uint32_t, int> renum;
            for (int i = 0; i < 3; ++i) renum[e.m.read32(eh + 0x210 + 4u * uint32_t(i))] = i;
            for (int i = 0; i < 3; ++i) renum[e.m.read32(eh + 0x21c + 4u * uint32_t(i))] = 3 + i;
            std::vector<std::string> elog;
            for (const std::string& l : e.log) {
                if (l.rfind("play#", 0) != 0) {
                    elog.push_back(l);
                    continue;
                }
                const int old = std::atoi(l.c_str() + 5);
                uint32_t addr = 0;
                for (const auto& kv : e.ids)
                    if (kv.second == old) addr = kv.first;
                const size_t sp = l.find(' ');
                elog.push_back("play#" + std::to_string(renum.count(addr) ? renum[addr] : -1) + l.substr(sp));
            }
            ++tp.cases;
            if (take_error(e, tp, "APlayerHeli::Play")) break;
            if (!same_log(elog, plog, why)) {
                tp.fail(why);
                break;
            }
            if (e.m.read32(eh + 0x228) != ps2::fbits(ph->blend)) {
                tp.fail("a mistura");
                break;
            }
        }
        port_clear_sounds();
    }
    for (uint32_t f : {kFnVehicleCtor, kFnActivateGun, kFnActivateZoom, kFnActivateWind, kFnVehiclePlay}) e.m.unhook(f);
    // Os AVehicle::Activate* e o StartAlarm: o objeto só sem o de antes (os construtores nos ganchos).
    int made = 0;
    e.m.hook(kFnGunCtor, "AGun::AGun", [&made](MipsMachine& mm) {
        ++made;
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    e.m.hook(kFnTurretCtor, "ATurret::ATurret", [&made](MipsMachine& mm) {
        ++made;
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    e.m.hook(kFnZoomCtor, "AZoomObj::AZoomObj", [&made](MipsMachine& mm) {
        ++made;
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    e.m.hook(kFnAlarmCtor, "ACarAlarm::ACarAlarm", [&made](MipsMachine& mm) {
        ++made;
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    for (int c = 0; c < cases / 4 + 1; ++c) {
        e.begin_case();
        const uint32_t ev = e.alloc(0x210);
        const int which = r.range(0, 3);
        const uint32_t off[4] = {0x1a4, 0x1a8, 0x1ac, 0x1f0};
        const uint32_t fn[4] = {kFnActivateGun, kFnActivateTurret, kFnActivateZoom, kFnStartAlarm};
        const bool had = r.chance(0.3);
        e.m.write32(ev + off[which], had ? 0x4444u : 0u);
        made = 0;
        e.call(fn[which], {ev, kText});
        ++ta.cases;
        if (take_error(e, ta, "AVehicle::Activate*")) continue;
        const uint32_t now = e.m.read32(ev + off[which]);
        if (had ? (made != 0 || now != 0x4444u) : (made != 1 || now == 0)) ta.fail("o " + hex(off[which]));
    }
    for (uint32_t f : {kFnGunCtor, kFnTurretCtor, kFnZoomCtor, kFnAlarmCtor}) e.m.unhook(f);
    port_banks_off();
    report("APlayerHeli::APlayerHeli", tc, failures);
    report("APlayerHeli::Play (sem o do AVehicle)", tp, failures);
    report("AVehicle::ActivateGun/Turret/Zoom e StartAlarm", ta, failures);
}

// ---- f) o leitor dos campos de som da "PhysicsData" (0x00194d48) -----------------------------------------------------

void check_sound_field(Emu& e, Rng& r, TestBanks& tb, int cases, int& failures) {
    Tally t;
    for (int c = 0; c < cases; ++c) {
        e.begin_case();
        tb.randomize(r);
        e.put_banks(tb);
        port_banks(tb);
        const char* name = r.chance(0.9) ? kNames[r.range(0, kNameCount - 1)] : "SFX_nenhum";
        const uint32_t txt = put_text(e, kText, name);
        const int32_t first = r.range(0, kBankCount - 1), second = r.range(0, kBankCount - 1);
        const uint32_t res = e.alloc(0x10), base = e.alloc(0x40), off = 4u * uint32_t(r.range(0, 9));
        e.m.write32(base + off, 0x5a5a5a5au);
        e.call(kFnSoundField, {res, 0, txt, off, uint32_t(first), uint32_t(second), 0, base});
        ++t.cases;
        if (take_error(e, t, "0x00194d48")) continue;
        const int32_t want = drv::audio_patch_id(name, first, second);
        if (int32_t(e.m.read32(base + off)) != want) t.fail(std::string(name) + ": o id");
        else if (e.m.read32(res) != 0xfffffffdu || e.m.read32(res + 4) != 1u || e.m.read32(res + 8) != base + off)
            t.fail("o resultado");
    }
    port_banks_off();
    report("o leitor dos sons da PhysicsData (0x00194d48)", t, failures);
}

}  // namespace

int run_sound_hud_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0, uint32_t vu0_size,
                         int cases, uint32_t seed) {
    (void)disc;
    std::printf("\nparte 27: os sons das armas, da mira e dos objetos e o resto do HUD de Paris\n");
    int failures = 0;
    // O SND do porte como depois de ligar o aparelho: esta parte não o inicia (as vozes falam com o SND só a seco, e o
    // que sobrar cai no SNDVOICEI_get de um SND sem vozes). As partes anteriores (13, 23...) terminam com o
    // audio_shutdown, cujo SNDSYS_restore libera as vozes como no PS2 sem esquecer o ponteiro nem o número delas: um
    // SNDstop depois dele lia o heap já liberado (a queda no ~AWorldSound das vozes de aviso, com handles sorteados).
    drv::snd_power_on();
    static Emu e;
    e.setup(elf, vu0, vu0_size);
    Rng r(seed ^ 0x27);
    TestBanks tb;
    e.banks = &tb;
    check_menu(e, r, tb, cases, failures);
    check_targeting(e, r, tb, cases, failures);
    check_alarm(e, r, tb, cases, failures);
    check_zoom(e, r, tb, cases, failures);
    check_turret(e, r, tb, cases, failures);
    check_gun(e, r, tb, cases, failures);
    check_smackable(e, r, tb, cases, failures);
    check_smackable_simulate(e, r, cases, failures);
    check_world(e, r, tb, cases, failures);
    check_group(e, r, tb, cases, failures);
    check_heli(e, r, tb, cases, failures);
    check_sound_field(e, r, tb, cases, failures);
    drv::sound_count() = 0;
    drv::sound_time() = 0;
    // O resto do HUD (a pausa, as trapaças, a moça do fundo): com a emulação do HUD da parte 24.
    failures += run_p27_pause_checks(elf, disc, cases, seed);
    return failures;
}

}  // namespace drvcheck
