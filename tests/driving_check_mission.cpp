// nf_driving_check, parte 15 (tests/driving_check_mission.h): a missão do DRIVING.ELF, o código ORIGINAL no
// interpretador MIPS (com a VU0 em modo macro) lado a lado com o porte. docs/driving.md seção 19.
//
//  a) O gerente: SMissionManager::Construct e Load com o 'Rule' e o 'MSet' de Paris copiados para a RAM emulada (as
//     listas 'el' de todo o CARP, os gatilhos, os registros do 'AIEl'; os ponteiros resolvidos do CARP trocados pelos
//     endereços emulados), ChangeStage(0) e Start: o gerente inteiro, as regras de cada lista (a imagem do objeto de
//     cada classe), as configurações, o bloco da troca e as chamadas a sistemas não portados.
//  b) O Update com estados sorteados (a etapa, os cronômetros, os eventos do programador, os contadores, as vidas, os
//     elementos da IA, o carro do jogador, o objetivo, as ações temporizadas, as regras ligadas): o gerente, as regras,
//     os eventos que as regras criaram (a imagem de cada objeto no anel do EventManager, pelas criadoras originais), os
//     construtores que agem (as etapas, os cronômetros) e as chamadas externas na ordem.
//  c) Cada evento das listas de Paris criado pela criadora original e pela do porte (a imagem e os efeitos do
//     construtor); depois, nos números cujo destrutor o porte executa (a missão, o HUD, os gatilhos), o destrutor
//     original pela tabela virtual e o do porte: o gerente, as listas dos objetivos, o GHud, o bloco da troca, o carro
//     do jogador, o escalonador, os gatilhos e as chamadas externas.
//
// Fora do código original (ganchos nativos): a memória (UMemory, __builtin_new/delete), o printf de depuração, o memset,
// as cadeias, a LookupEvent (a criadora pela tabela do porte, que a parte confere com os registros do PS2 por número), o
// WCollisionMgr::StepCheckHitWorld (o porte não tem: nada corta), o AttributeSet::LookupInt (o ACCUMULATE_SCORES: 0),
// o IFeedback::Pause, e as funções dos sistemas não portados, que só anotam a chamada (as mesmas que o porte anota em
// mission_trace()). O AIElementController, o GHud, o PBondCar e o resto do código rodam no interpretador.

#include "driving_check_mission.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/events.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/scheduler.h"
#include "driving/game/triggers.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/track.h"
#include "driving_check_world.h"
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

// As funções originais.
constexpr uint32_t kFnConstruct = 0x205188, kFnLoad = 0x205220, kFnChangeStage = 0x206338, kFnStart = 0x205ac8;
constexpr uint32_t kFnUpdate = 0x206b88, kFnLookupEvent = 0x1757b8;
// Os ganchos.
constexpr uint32_t kFnFastAlloc = 0x2cfda8, kFnFastFree = 0x2cfe50, kFnAlloc = 0x2d03b8, kFnFree = 0x2d0418;
constexpr uint32_t kFnBuiltinNew = 0x2d0910, kFnBuiltinDelete = 0x2d09a8, kFnAtexit = 0x2a71f8;
constexpr uint32_t kFnMemset = 0x2a8038, kFnStrcmp = 0x2a9508, kFnStrcasecmp = 0x2a9198, kFnStrncpy = 0x2a9a50;
constexpr uint32_t kFnPrintf = 0x2a8148, kFnSprintf = 0x2a9098, kFnDebug = 0x10c6c8;
constexpr uint32_t kFnStepCheckHitWorld = 0x216500, kFnLookupInt = 0x169bf0, kFnERestart = 0x156698;
constexpr uint32_t kFnFeedbackPause = 0x166210;
constexpr uint32_t kRestartVtable = 0x370ac8;
// As funções dos sistemas não portados que o porte anota (mission_trace): aqui só anotam.
constexpr uint32_t kNoted[] = {
    0x12c4c0,  // AIElementController::EveryoneChill
    0x12c678,  // AIElementController::CalcMightAsWellBeDead
    0x12daf0,  // AIElementController::SyncRuleInfo
    0x166cb0,  // InputConfigManager (a configuração da direção automática)
    0x168428,  // IOModule::ReleaseAllButtons
    0x174b80,  // Bond_SetExitToDriving
    0x183888,  // Missile::SetTarget
    0x1bb098,  // RPlayerCamera::EndCameraAnim
    0x1bb1f8,  // RPlayerCamera::SetLastSelectableCameraMode
    0x1bb7c0,  // RPlayerCamera::SetAutoDriveCamera
    0x1bb8a8,  // RPlayerCamera::SetMissileCamera
    0x1bb970,  // RPlayerCamera::EndMissileCamera
    0x1bb9e8,  // RPlayerCamera::SetPauseCamera
    0x1bc400,  // RPlayerCamera::CameraLockOn
    0x1d8470,  // RColorize::SetEnabled
    0x1d9020,  // RColorize::DisableMotionBlur
    0x207f20,  // SMissionManager::CalcEndMissionScores
    0x20bbe0,  // SWeaponManager::Reset
    0x20d378,  // SWeaponManager::LoadWeapon
    0x20d488,  // SWeaponManager::SetSecondary
    0x22b638,  // WTargetPicker::ActivateTargeting
    0x22b698,  // WTargetPicker::DeactivateTargeting
    0x23c820,  // GHud::Stylin
    0x241040,  // GHud::SetAutoDrive
    0x24a870,  // GSubtitles::ClearSubtitles
    0x2ec968,  // ASoundManager::SetMissionOver
    0x2f2dc8,  // AMix::Get
    0x2f4b10,  // AMenuSound::Trigger
    0x2f9798,  // AFader::QueuePrimary
};

// Os globais.
constexpr uint32_t kUberAddr = 0x100000, kMissionPtr = 0x33a024, kSim = 0x339190, kHudPtr = 0x33ab6c;
constexpr uint32_t kAICtlPtr = 0x327c30, kCameraList = 0x333ed8, kSchedPtr = 0x32f2b0, kEventData = 0x32e708;
constexpr uint32_t kRingBase = 0x32e6f8, kRingCursor = 0x32e6fc, kRingRead = 0x32e700;
constexpr uint32_t kDefaultSettings = 0x4162b8, kTrigManagerPtr = 0x33aa88, kSoundCinematic = 0x355e5c;
constexpr uint32_t kPBondCarVtable = 0x379ef0;

// A RAM emulada.
constexpr uint32_t kHeapBegin = 0x00a00000, kHeapEnd = 0x00c00000;
constexpr uint32_t kTrig = 0x00c00000;
constexpr uint32_t kListsBegin = 0x00c10000, kListsEnd = 0x00d00000;
constexpr uint32_t kRuleHdr = 0x00d00000, kMSetHdr = 0x00d00010, kRules = 0x00d00100, kMSet = 0x00d08000;
constexpr uint32_t kAIEl = 0x00d10000, kProxyBegin = 0x00d20000, kProxyEnd = 0x00d80000;
constexpr uint32_t kAICtl = 0x00e00000, kElems = 0x00e01000, kTracks = 0x00e05000, kAIObjs = 0x00e07000;
constexpr uint32_t kGHud = 0x00e09000, kCarList = 0x00e0a000, kCar = 0x00e0a100, kPhys = 0x00e0a700;
constexpr uint32_t kCarName = 0x00e0a800, kBodies = 0x00e0b000, kInfos = 0x00e0d000;
constexpr uint32_t kCamList = 0x00e1c000, kCamObj2 = 0x00e1c100, kSched = 0x00e1c200, kTrigMgr = 0x00e1c300;
constexpr uint32_t kDummy = 0x00e1d000, kRender = 0x00e1e000, kRenderVt = 0x00e1e100, kFnFake = 0x00e1f000;
constexpr uint32_t kRing = 0x00e20000, kRingSize = 0x8000;
constexpr int kMaxElements = 64;

constexpr uint32_t kTagMap = 0x4d617020, kTagTrgr = 0x54726772, kTagEl = 0x656c2020, kTagRule = 0x52756c65;
constexpr uint32_t kTagMSet = 0x4d536574, kTagAIEl = 0x4149456c;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 15) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    float uni(float a, float b) { return a + (b - a) * float(next() & 0xffffff) / float(0x1000000); }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

struct Tally {
    uint64_t cases = 0, bad = 0;
    std::string first;
    void fail(const std::string& what) {
        if (bad++ == 0) first = what;
    }
};

void report(const char* what, const Tally& t, int& failures) {
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", what, t.cases, t.bad, t.bad ? ": " : "",
                t.bad ? t.first.c_str() : "");
    if (t.bad) ++failures;
}

uint32_t fbits(float f) { return ps2::fbits(f); }

// O CARP de Paris na RAM emulada: as listas 'el', os gatilhos, os registros do 'AIEl', o 'Rule' e o 'MSet'; os outros
// alvos dos ponteiros resolvidos (os 'AICo', 'AISp', 'Cams', as instâncias, as cadeias) viram cópias de 0x40 bytes.
struct CarpCopy {
    const uint8_t* carp = nullptr;
    size_t carp_size = 0;
    uint32_t trgr = 0, trgr_size = 0, aiel = 0, aiel_size = 0, rule = 0, rule_size = 0, mset = 0, mset_size = 0;
    int rule_count = 0, aiel_count = 0, trig_count = 0;
    uint32_t default_settings = 0;  // o MissionSettings padrão do porte (p32)
    uint32_t rule_hdr = 0, mset_hdr = 0, trgr_hdr = 0;  // os UData do porte (p32)
    struct List {
        uint32_t emu, size;
    };
    std::map<uint32_t, List> lists;  // a lista 'el' do porte (p32) -> a cópia
    std::map<uint32_t, uint32_t> proxies;
    uint32_t proxy_at = kProxyBegin;
    uint64_t outside = 0;  // ponteiros que saem do CARP (sem cópia)

    bool in(uint32_t p, uint32_t base, uint32_t size) const { return p >= base && p - base < size; }
    bool in_carp(uint32_t p) const {
        const uint32_t base = nf::p32_u(carp);
        return p >= base && p - base < carp_size;
    }
    uint32_t to_emu(MipsMachine& m, uint32_t p) {
        if (p == 0) return 0;
        if (in(p, trgr, trgr_size)) return kTrig + (p - trgr);
        if (in(p, aiel, aiel_size)) return kAIEl + (p - aiel);
        if (in(p, rule, rule_size)) return kRules + (p - rule);
        if (in(p, mset, mset_size)) return kMSet + (p - mset);
        if (p == default_settings) return kDefaultSettings;
        auto it = lists.upper_bound(p);
        if (it != lists.begin()) {
            --it;
            if (p - it->first < it->second.size) return it->second.emu + (p - it->first);
        }
        auto pr = proxies.find(p);
        if (pr != proxies.end()) return pr->second;
        if (proxy_at + 0x40 > kProxyEnd) return 0xdead0000u;
        const uint32_t a = proxy_at;
        proxy_at += 0x40;
        std::memset(m.mem(a, 0x40), 0, 0x40);
        if (in_carp(p)) {
            const uint32_t base = nf::p32_u(carp);
            const uint32_t n = uint32_t(std::min<size_t>(0x40, carp_size - (p - base)));
            put(m, a, nf::p32_ptr<uint8_t>(p), n);
        } else {
            ++outside;
        }
        proxies[p] = a;
        return a;
    }
    int element_index(uint32_t p) const { return in(p, aiel, aiel_size) ? int((p - aiel) / 0x100) : -1; }
};

void collect_lists(drv::UGroup* g, std::map<uint32_t, uint32_t>& out, int depth) {
    if (depth > 24) return;
    drv::UData* arr = drv::UGroup_GetArray(g);
    const uint32_t groups = drv::UGroup_GroupCount(g);
    for (uint32_t i = 0; i < groups; ++i) collect_lists(arr + i, out, depth + 1);
    for (drv::UData* d = arr + groups; d < arr + groups + g->count; ++d)
        if (drv::UData_MatchTag(d) == kTagEl) out[nf::p32_u(drv::UData_Data(d))] = drv::UData_Size(d);
}

// Uma entrada das listas 'el' de Paris (o número e os dados no porte).
struct PortEvent {
    int32_t type;
    uint32_t data;  // p32
};

bool copy_carp(MipsMachine& m, drv::Track& track, CarpCopy& cc, std::vector<PortEvent>& events, std::string& error) {
    drv::UGroup* root = track.root();
    drv::UGroup* map = drv::UGroup_GroupLocateTag(root, kTagMap);
    if (map == drv::UGroup_GroupEnd(root)) {
        error = "a pista não tem 'Map '";
        return false;
    }
    auto data_of = [&](uint32_t tag, uint32_t& addr, uint32_t& size, int& count) -> drv::UData* {
        drv::UData* d = drv::UGroup_DataLocateTag(map, tag);
        if (d == drv::UGroup_DataEnd(map)) return nullptr;
        addr = nf::p32_u(drv::UData_Data(d));
        size = drv::UData_Size(d);
        count = int(d->count);
        return d;
    };
    int mset_count = 0;
    drv::UData* trgr = data_of(kTagTrgr, cc.trgr, cc.trgr_size, cc.trig_count);
    drv::UData* rule = data_of(kTagRule, cc.rule, cc.rule_size, cc.rule_count);
    drv::UData* mset = data_of(kTagMSet, cc.mset, cc.mset_size, mset_count);
    drv::UData* aiel = data_of(kTagAIEl, cc.aiel, cc.aiel_size, cc.aiel_count);
    if (!trgr || !rule || !mset || !aiel) {
        error = "o 'Map ' de Paris sem 'Trgr', 'Rule', 'MSet' ou 'AIEl'";
        return false;
    }
    cc.rule_hdr = nf::p32_u(rule);
    cc.mset_hdr = nf::p32_u(mset);
    cc.trgr_hdr = nf::p32_u(trgr);
    if (cc.aiel_count > kMaxElements || cc.aiel_size != 0x100u * uint32_t(cc.aiel_count)) {
        error = "o 'AIEl' de Paris não tem 0x100 bytes por elemento";
        return false;
    }
    // As listas 'el' de todo o CARP (as dos gatilhos e as das regras).
    std::map<uint32_t, uint32_t> sizes;
    collect_lists(root, sizes, 0);
    uint32_t at = kListsBegin;
    for (const auto& [p, size] : sizes) {
        const uint32_t a = ((at + 15) & ~15u) + (p & 15u);
        if (a + size + 16 > kListsEnd) {
            error = "as listas 'el' não cabem na RAM emulada";
            return false;
        }
        put(m, a, nf::p32_ptr<uint8_t>(p), size);
        cc.lists[p] = {a, size};
        at = a + size;
    }
    // Os registros, depois os ponteiros resolvidos trocados.
    put(m, kTrig, nf::p32_ptr<uint8_t>(cc.trgr), cc.trgr_size);
    put(m, kAIEl, nf::p32_ptr<uint8_t>(cc.aiel), cc.aiel_size);
    put(m, kRules, nf::p32_ptr<uint8_t>(cc.rule), cc.rule_size);
    put(m, kMSet, nf::p32_ptr<uint8_t>(cc.mset), cc.mset_size);
    for (int i = 0; i < cc.trig_count; ++i) {
        const drv::WTrigger* t = reinterpret_cast<const drv::WTrigger*>(nf::p32_ptr<uint8_t>(cc.trgr)) + i;
        m.write32(kTrig + 0x40u * uint32_t(i) + 0x18, cc.to_emu(m, t->events));
    }
    for (int i = 0; i < cc.rule_count; ++i) {
        const uint8_t* r = nf::p32_ptr<uint8_t>(cc.rule) + 0x20 * i;
        uint32_t ev, kind, el;
        std::memcpy(&ev, r + 0xc, 4);
        std::memcpy(&kind, r + 0x10, 4);
        std::memcpy(&el, r + 0x14, 4);
        m.write32(kRules + 0x20u * uint32_t(i) + 0xc, cc.to_emu(m, ev));
        if (cc.in(el, cc.aiel, cc.aiel_size)) m.write32(kRules + 0x20u * uint32_t(i) + 0x14, cc.to_emu(m, el));
    }
    for (const auto& [p, l] : cc.lists) {
        const drv::TriggerEvents* list = nf::p32_ptr<const drv::TriggerEvents>(p);
        const drv::TriggerEvent* e = reinterpret_cast<const drv::TriggerEvent*>(list + 1);
        for (int k = 0; k < list->count; ++k) {
            const uint32_t data = nf::p32_u(reinterpret_cast<const uint8_t*>(&e[k]) + e[k].data_offset);
            events.push_back({e[k].type, data});
            const drv::EventInfo* info = drv::event_info(e[k].type);
            if (!info) continue;
            for (int w = 0; w < 32; ++w) {
                if (!((info->refs >> w) & 1)) continue;
                uint32_t v;
                std::memcpy(&v, nf::p32_ptr<uint8_t>(data + 4u * uint32_t(w)), 4);
                m.write32(cc.to_emu(m, data) + 4u * uint32_t(w), cc.to_emu(m, v));
            }
        }
    }
    // Os cabeçalhos do 'Rule' e do 'MSet' que o Load recebe.
    drv::UData h = *rule;
    h.flags = (cc.rule_size << 8) | (rule->flags & 0xfcu);
    h.offset = kRules;
    put(m, kRuleHdr, &h, 16);
    h = *mset;
    h.flags = (cc.mset_size << 8) | (mset->flags & 0xfcu);
    h.offset = kMSet;
    put(m, kMSetHdr, &h, 16);
    return true;
}

class MissionEmu {
public:
    MipsMachine m;
    ps2::VuMachine vu{ps2::VuUnit::VU0};
    std::vector<uint8_t> program;
    uint32_t heap = kHeapBegin;
    std::vector<uint32_t> trace;
    uint64_t render_calls = 0;

    void setup(const nf::Elf& elf, const uint8_t* prog, uint32_t size) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        m.attach_vu0(&vu);
        program.assign(prog, prog + size);
        vu.load_micro(0, program.data(), uint32_t(program.size()));
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", heap, n);
            std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(kFnFastAlloc, "UMemory::FastAlloc", alloc);
        m.hook(kFnAlloc, "UMemory::Alloc", alloc);
        m.hook(kFnBuiltinNew, "__builtin_new", alloc);
        m.hook(kFnFastFree, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(kFnFree, "UMemory::Free", [](MipsMachine&) {});
        m.hook(kFnBuiltinDelete, "__builtin_delete", [](MipsMachine&) {});
        m.hook(kFnAtexit, "atexit", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnPrintf, "printf", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnSprintf, "sprintf", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnDebug, "o printf de depuração (0x10c6c8)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnMemset, "memset", [](MipsMachine& mm) {
            const uint32_t a = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(a, n), int(mm.gpr(R::a1) & 0xff), n);
            mm.set_gpr(R::v0, a);
        });
        auto cstr = [](MipsMachine& mm, uint32_t a) {
            std::string s;
            for (uint32_t i = 0; i < 4096; ++i) {
                const char c = char(*mm.mem(a + i, 1));
                if (!c) break;
                s.push_back(c);
            }
            return s;
        };
        m.hook(kFnStrcmp, "strcmp", [cstr](MipsMachine& mm) {
            const std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            const int r = std::strcmp(a.c_str(), b.c_str());
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        m.hook(kFnStrcasecmp, "strcasecmp", [cstr](MipsMachine& mm) {
            std::string a = cstr(mm, uint32_t(mm.gpr(R::a0))), b = cstr(mm, uint32_t(mm.gpr(R::a1)));
            for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
            const int r = std::strcmp(a.c_str(), b.c_str());
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        m.hook(kFnStrncpy, "strncpy", [](MipsMachine& mm) {
            const uint32_t d = uint32_t(mm.gpr(R::a0)), s = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            bool end = false;
            for (uint32_t i = 0; i < n; ++i) {
                const uint8_t c = end ? 0 : *mm.mem(s + i, 1);
                if (c == 0) end = true;
                *mm.mem(d + i, 1) = c;
            }
            mm.set_gpr(R::v0, d);
        });
        m.hook(kFnLookupEvent, "RegisterEvent::LookupEvent (a tabela do porte)", [](MipsMachine& mm) {
            const drv::EventInfo* info = drv::event_info(int32_t(mm.gpr(R::a0)));
            mm.set_gpr(R::v0, info ? info->creator : 0);
        });
        m.hook(kFnStepCheckHitWorld, "WCollisionMgr::StepCheckHitWorld (nada corta)",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnLookupInt, "AttributeSet::LookupInt (o ACCUMULATE_SCORES: 0)",
               [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
        m.hook(kFnFeedbackPause, "IFeedback::Pause", [](MipsMachine&) {});
        m.hook(kFnERestart, "ERestart::ERestart", [this](MipsMachine& mm) {
            trace.push_back(kFnERestart);
            mm.write32(uint32_t(mm.gpr(R::a0)), kRestartVtable);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        for (uint32_t fn : kNoted)
            m.hook(fn, "anotada " + hex(fn), [this, fn](MipsMachine& mm) {
                trace.push_back(fn);
                mm.set_gpr(R::v0, kDummy);
                mm.set_fpr(0, 0.0f);
            });
        // O render falso do carro (a tabela virtual do RSceneObj em +0x48).
        m.hook(kFnFake, "o render falso", [this](MipsMachine& mm) {
            ++render_calls;
            mm.set_gpr(R::v0, 0);
        });
        for (uint32_t off = 0; off < 0x200; off += 8) {
            m.write32(kRenderVt + off, 0);
            m.write32(kRenderVt + off + 4, kFnFake);
        }
        m.write32(kRender, kRenderVt);
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) { m.call(fn, args, 50'000'000); }
};

// ---------------------------------------------------------------- o estado dos dois lados

struct Context {
    MissionEmu* e = nullptr;
    CarpCopy* cc = nullptr;
    drv::BondCar* car = nullptr;
    drv::Scheduler* sched = nullptr;
    uint32_t mgr = 0;  // o gerente emulado
    // As regras: a lista (0 a de sempre, 1 + etapa) e o objeto emulado de cada regra do porte.
    std::map<const drv::SMissionRuleData*, uint32_t> rule_emu;
};

// Os nós de uma std::list do GCC 2.95 ({próximo, anterior, dado}; a palavra da cabeça aponta para a sentinela).
std::vector<uint32_t> list_items(MipsMachine& m, uint32_t head_word) {
    std::vector<uint32_t> out;
    const uint32_t sentinel = m.read32(head_word);
    if (!sentinel) return out;
    uint32_t n = m.read32(sentinel);
    for (int guard = 0; n != sentinel && guard < 4096; ++guard) {
        out.push_back(m.read32(n + 8));
        n = m.read32(n);
    }
    return out;
}

uint32_t list_head(int list) { return list == 0 ? 0x20u : 0x24u + 4u * uint32_t(list - 1); }

// O elemento da IA do porte de um índice do 'AIEl'.
drv::MissionAIElement& element_at(CarpCopy& cc, int i) { return drv::mission_ai().get(cc.aiel + 0x100u * uint32_t(i)); }

void put_world(Context& c) {
    MipsMachine& m = c.e->m;
    CarpCopy& cc = *c.cc;
    // A Simulation: o carro do jogador e o corpo dele.
    drv::Simulation& sim = drv::simulation();
    m.write32(kSim + 0x04, 0);
    m.write32(kSim + 0x1c, kBodies);
    m.write32(kSim + 0x28, kBodies);
    m.write32(kSim + 0xdbc, uint32_t(sim.frame_rate));
    std::memcpy(m.mem(kSim + 0xdc0, 4), &sim.dt, 4);
    m.write32(kSim + 0xdc4, uint32_t(sim.steps));
    m.write32(kSim + 0xdcc, kCarList);
    m.write32(kSim + 0xdd0, kCarList + 4);
    m.write32(kCarList, kCar);
    drv::BondCar& car = *c.car;
    const int slot = car.d.slot;
    drv::RigidBody* rb = car.body();
    put(m, kBodies + 0x80u * uint32_t(slot), rb, 0x80);
    m.write32(kBodies + 0x80u * uint32_t(slot) + 0x5c, kInfos + 0x500u * uint32_t(slot));
    put(m, kInfos + 0x500u * uint32_t(slot), nf::raw(rb->info), 0x500);
    put(m, kCar, &car.d, 0x460);
    m.write32(kCar + 0x48, kRender);
    m.write32(kCar + 0x4c, 0);
    m.write32(kCar + 0x50, 0);
    m.write32(kCar + 0x54, car.d.hit_point_loc == nf::p32_u(&drv::mission_manager().player_health) ? c.mgr + 0x378
                                                                                                     : kCar + 0x42c);
    m.write32(kCar + 0x68, kPBondCarVtable);
    put(m, kPhys, car.physics(), 0x100);
    m.write32(kCar + 0x23c, kPhys);
    m.write32(kCar + 0x26c, 0);
    m.write32(kCar + 0x2cc, kCarName);
    put(m, kCarName, car.name_.c_str(), uint32_t(car.name_.size() + 1));
    m.write32(kCar + 0x2e0, 0);
    m.write32(kCar + 0x2e4, 0);
    m.write32(kCar + 0x388, kDummy);
    m.write32(kCar + 0x434, 0);
    // O AIElementController: os elementos, os acompanhamentos (o objeto com a vida em +0x4c), as estatísticas.
    m.write32(kAICtlPtr, kAICtl);
    m.write32(kAICtl + 0xc, kElems);
    m.write32(kAICtl + 0x10, kAIEl);
    m.write32(kAICtl + 0x14, kTracks);
    put(m, kAICtl + 0x1c, drv::mission_ai().stats, sizeof drv::mission_ai().stats);
    for (int i = 0; i < cc.aiel_count; ++i) {
        const drv::MissionAIElement& el = element_at(cc, i);
        const uint32_t t = kTracks + 0x50u * uint32_t(i), o = kAIObjs + 0x50u * uint32_t(i), x = kElems + 0x100u * uint32_t(i);
        std::memset(m.mem(t, 0x50), 0, 0x50);
        std::memset(m.mem(o, 0x50), 0, 0x50);
        std::memset(m.mem(x, 0x100), 0, 0x100);
        m.write32(t + 0x20, o);
        std::memcpy(m.mem(o + 0x4c, 4), &el.health, 4);
        put(m, t + 0x30, el.pos, 12);
        std::memcpy(m.mem(t + 0x3c, 4), &el.speed, 4);
        put(m, x + 0x30, el.pos, 12);
    }
    // A câmera, o escalonador, o GHud, o gerente dos gatilhos, o bloco da troca.
    m.write32(kCameraList, kCamList);
    m.write32(kCamList + 4, kCamObj2);
    put(m, kCamObj2 + 0x30, drv::mission_env().camera_pos, 12);
    m.write32(kSchedPtr, kSched);
    m.write32(kSched + 0x20, uint32_t(c.sched->cinematic));
    std::memcpy(m.mem(kSched + 0x24, 4), &c.sched->time_scale, 4);
    m.write32(kSoundCinematic, uint32_t(drv::mission_env().sound_cinematic_mode));
    put(m, kGHud, &drv::hud(), sizeof(drv::GHudData));
    m.write32(kHudPtr, drv::hud_exists() ? kGHud : 0);
    m.write32(kTrigManagerPtr, kTrigMgr);
    m.write32(kTrigMgr, uint32_t(drv::trigger_manager().count));
    m.write32(kTrigMgr + 4, kTrig);
    m.write32(kTrigMgr + 8, uint32_t(drv::trigger_manager().active));
    for (int i = 0; i < cc.trig_count; ++i) {
        drv::WTrigger t = *drv::trigger_at(i);
        t.events = cc.to_emu(m, t.events);
        put(m, kTrig + 0x40u * uint32_t(i), &t, 0x40);
    }
    put(m, kUberAddr, &drv::uber_bond(), sizeof(drv::UberBondBlock));
    // O EventDynamicData e o anel do EventManager.
    drv::EventDynamicData d = drv::trigger_manager().event_data;
    d.trigger = cc.to_emu(m, d.trigger);
    put(m, kEventData, &d, 0x64);
    std::memset(m.mem(kRing, kRingSize), 0xcd, kRingSize);
    m.write32(kRingBase, kRing);
    m.write32(kRingCursor, kRing);
    m.write32(kRingRead, kRing);
    c.e->trace.clear();
}

// Os ponteiros do gerente (os campos que o porte guarda como p32).
void manager_pointers(Context& c, const drv::SMissionManagerData& p, uint32_t out[0x1b50 / 4], bool& ok) {
    CarpCopy& cc = *c.cc;
    MipsMachine& m = c.e->m;
    auto set = [&](uint32_t off, uint32_t v) { out[off / 4] = v; };
    set(0x14, p.rules ? kRuleHdr : 0);
    set(0x18, p.mset ? kMSetHdr : 0);
    set(0x1c, cc.to_emu(m, p.settings));
    for (int i = 0; i < 2; ++i) {
        // O alvo é sempre um ponteiro do CARP (a posição de um gatilho, a instância, o elemento), também depois do
        // ClearObjective, que só zera o tipo.
        if (p.objective_kind[i] != 0 || cc.in_carp(p.objective_target[i]))
            set(0x5f8 + 4u * uint32_t(i), cc.to_emu(m, p.objective_target[i]));
        set(0x608 + 4u * uint32_t(i), cc.to_emu(m, p.objective_comment[i]));
    }
    set(0x1b44, cc.to_emu(m, p.car_type));
    ok = true;
}

// As faixas do gerente que só o PS2 tem (as cabeças das listas, a fila de ações).
bool manager_skip(uint32_t off) { return off < 8 || (off >= 0x20 && off < 0x150) || (off >= 0x36c && off < 0x370); }

void put_manager(Context& c) {
    MipsMachine& m = c.e->m;
    const drv::SMissionManagerData& p = drv::mission_manager();
    uint32_t words[0x1b50 / 4];
    std::memcpy(words, &p, sizeof words);
    bool ok = false;
    manager_pointers(c, p, words, ok);
    for (uint32_t off = 0; off < 0x1b50; off += 4)
        if (!manager_skip(off)) m.write32(c.mgr + off, words[off / 4]);
    // As regras (os campos que mudam: ligada, uma vez, os últimos valores).
    for (const auto& [r, a] : c.rule_emu) {
        uint32_t w[10];
        std::memcpy(w, r, 0x28);
        w[0] = c.cc->to_emu(m, r->events);
        if ((r->type == 2 || r->type == 3 || r->type == 4 || r->type == 7 || r->type == 8 || r->type == 10 ||
             r->type == 11 || r->type == 12) &&
            r->w[0] == 4)
            w[6] = c.cc->to_emu(m, r->w[1]);
        for (uint32_t k = 0; k < r->size / 4; ++k)
            if (k != 4) m.write32(a + 4 * k, w[k]);
    }
}

std::string compare_manager(Context& c) {
    MipsMachine& m = c.e->m;
    const drv::SMissionManagerData& p = drv::mission_manager();
    uint32_t words[0x1b50 / 4];
    std::memcpy(words, &p, sizeof words);
    bool ok = false;
    manager_pointers(c, p, words, ok);
    for (uint32_t off = 0; off < 0x1b50; off += 4) {
        if (manager_skip(off)) continue;
        const uint32_t a = m.read32(c.mgr + off), b = words[off / 4];
        if (a != b) return "SMissionManager +0x" + hex(off).substr(4) + ": original " + hex(a) + ", porte " + hex(b);
    }
    // As regras de cada lista, na ordem.
    const drv::MissionLists& l = drv::mission_lists();
    for (int k = 0; k < 65; ++k) {
        const std::vector<uint32_t> emu = list_items(m, c.mgr + list_head(k));
        if (emu.size() != l.rules[k].size())
            return "a lista " + std::to_string(k) + " das regras: " + std::to_string(emu.size()) + " no original, " +
                   std::to_string(l.rules[k].size()) + " no porte";
        for (size_t i = 0; i < emu.size(); ++i) {
            const drv::SMissionRuleData* r = l.rules[k][i];
            uint32_t w[10];
            std::memcpy(w, r, 0x28);
            w[0] = c.cc->to_emu(m, r->events);
            if ((r->type == 2 || r->type == 3 || r->type == 4 || r->type == 7 || r->type == 8 || r->type == 10 ||
                 r->type == 11 || r->type == 12) &&
                r->w[0] == 4)
                w[6] = c.cc->to_emu(m, r->w[1]);
            for (uint32_t q = 0; q < r->size / 4; ++q)
                if (m.read32(emu[i] + 4 * q) != w[q])
                    return "a regra " + std::to_string(i) + " da lista " + std::to_string(k) + " (tipo " +
                           std::to_string(r->type) + ") +0x" + hex(4 * q).substr(6) + ": original " +
                           hex(m.read32(emu[i] + 4 * q)) + ", porte " + hex(w[q]);
        }
    }
    // Os objetivos e as mensagens.
    const std::vector<uint32_t> objs = list_items(m, c.mgr + 0);
    if (objs.size() != l.objectives.size())
        return "objetivos: " + std::to_string(objs.size()) + " no original, " + std::to_string(l.objectives.size()) +
               " no porte";
    for (size_t i = 0; i < objs.size(); ++i)
        for (uint32_t q = 0; q < 5; ++q) {
            uint32_t b;
            std::memcpy(&b, reinterpret_cast<const uint8_t*>(l.objectives[i]) + 4 * q, 4);
            if (m.read32(objs[i] + 4 * q) != b)
                return "o objetivo " + std::to_string(i) + " +" + std::to_string(4 * q) + ": original " +
                       hex(m.read32(objs[i] + 4 * q)) + ", porte " + hex(b);
        }
    const std::vector<uint32_t> msgs = list_items(m, c.mgr + 4);
    if (msgs.size() != l.messages.size())
        return "mensagens: " + std::to_string(msgs.size()) + " no original, " + std::to_string(l.messages.size()) +
               " no porte";
    // O bloco da troca, o GHud, o escalonador, o gerente dos gatilhos.
    const uint8_t* u = reinterpret_cast<const uint8_t*>(&drv::uber_bond());
    for (uint32_t off = 0; off < sizeof(drv::UberBondBlock); off += 4) {
        uint32_t b;
        std::memcpy(&b, u + off, 4);
        if (m.read32(kUberAddr + off) != b)
            return "UberBond +0x" + hex(off).substr(4) + ": original " + hex(m.read32(kUberAddr + off)) + ", porte " +
                   hex(b);
    }
    if (drv::hud_exists()) {
        const uint8_t* h = reinterpret_cast<const uint8_t*>(&drv::hud());
        for (uint32_t off = 0; off < sizeof(drv::GHudData); off += 4) {
            uint32_t b;
            std::memcpy(&b, h + off, 4);
            if (m.read32(kGHud + off) != b)
                return "GHud +0x" + hex(off).substr(4) + ": original " + hex(m.read32(kGHud + off)) + ", porte " + hex(b);
        }
    }
    if (m.read32(kSched + 0x20) != uint32_t(c.sched->cinematic) || m.read32(kSched + 0x24) != fbits(c.sched->time_scale))
        return "o escalonador: original " + hex(m.read32(kSched + 0x20)) + "/" + hex(m.read32(kSched + 0x24)) +
               ", porte " + hex(uint32_t(c.sched->cinematic)) + "/" + hex(fbits(c.sched->time_scale));
    if (m.read32(kTrigMgr + 8) != uint32_t(drv::trigger_manager().active))
        return "o gerente dos gatilhos: ativo " + hex(m.read32(kTrigMgr + 8)) + " no original";
    // As chamadas externas, na ordem.
    const std::vector<drv::MissionCall>& pt = drv::mission_trace();
    std::vector<uint32_t> port_fns;
    for (const drv::MissionCall& mc : pt) port_fns.push_back(mc.fn);
    if (port_fns != c.e->trace) {
        std::string a, b;
        for (uint32_t f : c.e->trace) a += " " + hex(f).substr(2);
        for (uint32_t f : port_fns) b += " " + hex(f).substr(2);
        return "as chamadas externas: original" + (a.empty() ? std::string(" -") : a) + ", porte" +
               (b.empty() ? std::string(" -") : b);
    }
    return std::string();
}

// O carro do jogador (os campos que os destrutores mexem).
std::string compare_car(Context& c) {
    MipsMachine& m = c.e->m;
    const uint8_t* pd = reinterpret_cast<const uint8_t*>(&c.car->d);
    for (uint32_t off = 0x180; off < 0x460; off += 4) {
        if ((off >= 0x23c && off < 0x240) || (off >= 0x26c && off < 0x270) || (off >= 0x2cc && off < 0x2d0) ||
            (off >= 0x2e0 && off < 0x2e8) || (off >= 0x388 && off < 0x38c) || (off >= 0x434 && off < 0x438))
            continue;
        uint32_t b;
        std::memcpy(&b, pd + off, 4);
        if (m.read32(kCar + off) != b)
            return "PBondCar +0x" + hex(off).substr(5) + ": original " + hex(m.read32(kCar + off)) + ", porte " + hex(b);
    }
    return std::string();
}

// Os eventos criados: a imagem de cada objeto no anel (as palavras que a criadora escreve) e o cursor.
std::string compare_events(Context& c) {
    MipsMachine& m = c.e->m;
    const std::vector<drv::PendingEvent>& pending = drv::trigger_manager().pending;
    uint32_t at = kRing;
    for (size_t i = 0; i < pending.size(); ++i) {
        const drv::PendingEvent& e = pending[i];
        const drv::EventInfo* info = drv::event_info(e.type);
        if (!info) return "o evento " + std::to_string(e.type) + " sem a informação do porte";
        for (uint32_t k = 0; k < 16; ++k) {
            if (!((e.written >> k) & 1)) continue;
            // As palavras que são ponteiros: as dos dados resolvidos e as do EventDynamicData (+0x30 o gatilho).
            bool ptr = false;
            for (const drv::EventField& f : info->fields) {
                if (f.source == 0) break;
                if (uint32_t(f.offset / 4) != k) continue;
                if (f.source == drv::kFromData) ptr = ((info->refs >> (f.from / 4)) & 1) != 0;
                else ptr = f.from == 0x30;
            }
            const uint32_t want = ptr ? c.cc->to_emu(m, e.obj[k]) : e.obj[k];
            if (m.read32(at + 4 * k) != want)
                return "o evento " + std::to_string(i) + " (" + info->name + ") +0x" + hex(4 * k).substr(6) +
                       ": original " + hex(m.read32(at + 4 * k)) + ", porte " + hex(want);
        }
        at += (e.size + 15) & ~15u;
    }
    if (m.read32(kRingCursor) != at)
        return "o anel do EventManager: " + std::to_string((m.read32(kRingCursor) - kRing)) + " bytes no original, " +
               std::to_string(at - kRing) + " no porte";
    return std::string();
}

void clear_port_run() {
    drv::trigger_manager().pending.clear();
    drv::mission_trace().clear();
}

// ---------------------------------------------------------------- a) o gerente

int check_load(Context& c, Tally& t) {
    MipsMachine& m = c.e->m;
    CarpCopy& cc = *c.cc;
    ++t.cases;
    drv::uber_bond_defaults("paris_mis01", 1);
    put_world(c);
    drv::mission_trace_enabled() = true;
    drv::mission_trace().clear();
    c.e->trace.clear();
    drv::SMissionManager_Construct(false);
    c.e->call(kFnConstruct, {});
    c.mgr = m.read32(kMissionPtr);
    if (!c.mgr) {
        t.fail("o Construct original não criou o gerente");
        return 1;
    }
    cc.default_settings = drv::mission_manager().settings;
    // As configurações padrão (o MEM_fill e os valores do construtor).
    for (uint32_t off = 0; off < sizeof(drv::MissionSettings); off += 4) {
        uint32_t b;
        std::memcpy(&b, nf::p32_ptr<uint8_t>(cc.default_settings) + off, 4);
        if (m.read32(kDefaultSettings + off) != b) {
            t.fail("as configurações padrão +0x" + hex(off).substr(5) + ": original " +
                   hex(m.read32(kDefaultSettings + off)) + ", porte " + hex(b));
            return 1;
        }
    }
    std::string why = compare_manager(c);
    if (!why.empty()) {
        t.fail("Construct: " + why);
        return 1;
    }
    // Load, ChangeStage(0), Start.
    drv::mission_trace().clear();
    c.e->trace.clear();
    drv::SMissionManager_Load(drv::mission_manager(), nf::p32_ptr<drv::UData>(cc.rule_hdr),
                              nf::p32_ptr<drv::UData>(cc.mset_hdr));
    c.e->call(kFnLoad, {c.mgr, kRuleHdr, kMSetHdr});
    drv::SMissionManager_ChangeStage(drv::mission_manager(), 0);
    c.e->call(kFnChangeStage, {c.mgr, 0});
    drv::SMissionManager_Start(drv::mission_manager());
    c.e->call(kFnStart, {c.mgr});
    // As regras emuladas de cada regra do porte (a mesma ordem nas listas).
    const drv::MissionLists& l = drv::mission_lists();
    for (int k = 0; k < 65; ++k) {
        const std::vector<uint32_t> emu = list_items(m, c.mgr + list_head(k));
        for (size_t i = 0; i < std::min(emu.size(), l.rules[k].size()); ++i) c.rule_emu[l.rules[k][i]] = emu[i];
    }
    why = compare_manager(c);
    if (why.empty()) {
        for (uint32_t off = 0; off < cc.mset_size; off += 4) {
            uint32_t b;
            std::memcpy(&b, nf::p32_ptr<uint8_t>(cc.mset) + off, 4);
            if (m.read32(kMSet + off) != b) {
                why = "o 'MSet' +0x" + hex(off).substr(5) + ": original " + hex(m.read32(kMSet + off)) + ", porte " + hex(b);
                break;
            }
        }
    }
    if (!why.empty()) {
        t.fail("Load: " + why);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- b) o Update

void random_state(Rng& rng, Context& c) {
    drv::SMissionManagerData& m = drv::mission_manager();
    drv::Simulation& sim = drv::simulation();
    CarpCopy& cc = *c.cc;
    sim.steps = uint32_t(rng.range(0, 30000));
    const int32_t steps = int32_t(sim.steps);
    m.state = rng.chance(0.85) ? 1 : rng.range(0, 4);
    m.stage = rng.range(0, std::max(0, m.num_stages - 1));
    m.next_stage = rng.chance(0.8) ? m.stage : rng.range(0, std::max(0, m.num_stages - 1));
    for (int i = 0; i < 10; ++i) {
        drv::SMissionTimer& t = m.timers[i];
        t.active = rng.chance(0.6) ? 1 : 0;
        t.value = rng.range(-300, 20000);
        const int rates[4] = {-1, 0, 1, 2};
        t.rate = rates[rng.range(0, 3)];
        t.start_step = steps - rng.range(0, 2000);
        t.ticking = rng.chance(0.15) ? 1 : 0;
        t.last_second = rng.range(-1, 300);
    }
    for (int i = 0; i < 64; ++i) m.programmer_events[i] = rng.chance(0.08) ? 1 : 0;
    for (int i = 0; i < 64; ++i) m.magic_counters[i] = rng.range(0, 60);
    m.player_health = rng.chance(0.15) ? rng.uni(-50, 0) : rng.uni(1, 3000);
    m.player_shield = rng.uni(0, 1000);
    m.difficulty = rng.range(0, 2);
    m.cinematic_mode = rng.chance(0.2) ? uint32_t(rng.range(0, 7)) : 0u;
    m.skipping_cinematic = rng.chance(0.1) ? 1 : 0;
    for (int i = 0; i < 2; ++i) {
        const int k = rng.range(0, 3);
        m.objective_kind[i] = k == 2 ? 3 : k == 3 ? 1 : 0;
        if (m.objective_kind[i] == 1)
            m.objective_target[i] = nf::p32_u(drv::trigger_at(rng.range(0, cc.trig_count - 1))->position);
        else if (m.objective_kind[i] == 3)
            m.objective_target[i] = cc.aiel + 0x100u * uint32_t(rng.range(0, cc.aiel_count - 1));
        else
            m.objective_target[i] = 0;
    }
    m.objective_check_step = steps + rng.range(-20, 20);
    m.objective_seen_step = steps - rng.range(0, 600);
    for (int i = 0; i < 8; ++i) {
        drv::MissionTimedAction& t = m.timed[i];
        t.active = rng.chance(0.2) ? 1 : 0;
        t.limit = rng.uni(1, 30);
        t.target = rng.uni(0.5f, t.limit);
        t.points_min = rng.range(0, 100);
        t.points_max = rng.range(100, 1000);
        t.start_step = steps - rng.range(0, 3000);
    }
    m.last_damage_step = steps - rng.range(-50, 100);
    drv::UberBondBlock& u = drv::uber_bond();
    u.word(0x9a0) = rng.chance(0.2) ? rng.range(0, 7) : 0;
    u.word(0x9a4) = rng.chance(0.1) ? 1 : 0;
    u.word(0x62c) = rng.chance(0.05) ? rng.range(1, std::max(1, steps)) : 0;
    drv::hud().paused = rng.chance(0.1) ? 1 : 0;
    for (int i = 0; i < cc.aiel_count; ++i) {
        drv::MissionAIElement& el = element_at(cc, i);
        el.health = rng.chance(0.3) ? 0.0f : rng.chance(0.2) ? rng.uni(1, 999) : 1000.0f;
        el.speed = rng.uni(0, 40);
        for (int k = 0; k < 3; ++k) el.pos[k] = rng.uni(-1200, 800);
    }
    for (float& f : drv::mission_ai().stats) f = rng.chance(0.5) ? 0.0f : rng.uni(0, 2);
    // O carro do jogador perto de um gatilho, andando.
    drv::RigidBody* rb = c.car->body();
    const drv::WTrigger* t = drv::trigger_at(rng.range(0, cc.trig_count - 1));
    for (int k = 0; k < 3; ++k) rb->pos[k] = t->position[k] + rng.uni(-30, 30);
    for (int k = 0; k < 3; ++k) rb->vel[k] = rng.chance(0.2) ? 0.0f : rng.uni(-30, 30);
    for (int k = 0; k < 3; ++k) drv::mission_env().camera_pos[k] = rb->pos[k] + rng.uni(-8, 8);
    for (const auto& [r, a] : c.rule_emu)
        if (rng.chance(0.1)) const_cast<drv::SMissionRuleData*>(r)->active ^= 1;
    c.sched->cinematic = 0;
    c.sched->time_scale = 1.0f;
}

uint64_t g_update_events = 0, g_update_with_events = 0, g_update_calls = 0;

int check_update(Context& c, int cases, Rng& rng, Tally& t) {
    for (int i = 0; i < cases; ++i) {
        ++t.cases;
        random_state(rng, c);
        put_world(c);
        put_manager(c);
        clear_port_run();
        drv::SMissionManager_Update(drv::mission_manager());
        c.e->call(kFnUpdate, {c.mgr});
        g_update_events += drv::trigger_manager().pending.size();
        g_update_with_events += drv::trigger_manager().pending.empty() ? 0 : 1;
        g_update_calls += c.e->trace.size();
        std::string why = compare_manager(c);
        if (why.empty()) why = compare_events(c);
        if (!why.empty()) t.fail("caso " + std::to_string(i) + ": " + why);
    }
    return 0;
}

// ---------------------------------------------------------------- c) os eventos

// Os números cujo destrutor o porte executa e a conferência compara (sem os que mexem no mundo: o EStartMission e o
// EResetPlayerCarPos põem o carro no chão, o EDamagePlayer chama o ApplyDamage, o tráfego, os gatilhos sorteados e os
// objetos derrubáveis ficam nas partes 12 e 14).
bool dtor_checked(int32_t type) {
    static const std::set<int32_t> kTypes = {40,  41,  44,  45,  46,  47,  48,  49,  53,  54,  68,  69,  76,  77,  103,
                                             104, 112, 125, 131, 193, 194, 200, 219, 220, 225, 228, 229, 233, 234, 275, 276};
    return kTypes.count(type) != 0;
}

std::string compare_events_from(Context& c, size_t first) {
    MipsMachine& m = c.e->m;
    const std::vector<drv::PendingEvent>& pending = drv::trigger_manager().pending;
    uint32_t at = kRing;
    for (size_t i = 0; i < first && i < pending.size(); ++i) at += (pending[i].size + 15) & ~15u;
    for (size_t i = first; i < pending.size(); ++i) {
        const drv::PendingEvent& e = pending[i];
        const drv::EventInfo* info = drv::event_info(e.type);
        if (!info) return "o evento " + std::to_string(e.type) + " sem a informação do porte";
        for (uint32_t k = 0; k < 16; ++k) {
            if (!((e.written >> k) & 1)) continue;
            bool ptr = false;
            for (const drv::EventField& f : info->fields) {
                if (f.source == 0) break;
                if (uint32_t(f.offset / 4) != k) continue;
                if (f.source == drv::kFromData) ptr = ((info->refs >> (f.from / 4)) & 1) != 0;
                else ptr = f.from == 0x30;
            }
            const uint32_t want = ptr ? c.cc->to_emu(m, e.obj[k]) : e.obj[k];
            if (m.read32(at + 4 * k) != want)
                return "o evento " + std::to_string(i) + " (" + info->name + ") +0x" + hex(4 * k).substr(6) +
                       ": original " + hex(m.read32(at + 4 * k)) + ", porte " + hex(want);
        }
        at += (e.size + 15) & ~15u;
    }
    if (m.read32(kRingCursor) != at)
        return "o anel do EventManager: " + std::to_string(m.read32(kRingCursor) - kRing) + " bytes no original, " +
               std::to_string(at - kRing) + " no porte";
    return std::string();
}

std::string compare_triggers(Context& c) {
    MipsMachine& m = c.e->m;
    for (int i = 0; i < c.cc->trig_count; ++i) {
        const drv::WTrigger* t = drv::trigger_at(i);
        if (m.read32(kTrig + 0x40u * uint32_t(i) + 0x10) != (uint32_t(t->unknown10) | uint32_t(t->shape) << 8 |
                                                              uint32_t(t->flags) << 16))
            return "o gatilho " + std::to_string(i) + ": as bandeiras " + hex(m.read32(kTrig + 0x40u * uint32_t(i) + 0x10)) +
                   " no original";
    }
    return std::string();
}

int check_events(Context& c, const std::vector<PortEvent>& events, int cases, Rng& rng, Tally& t_create,
                 Tally& t_dtor, std::map<int32_t, uint64_t>& dtor_types) {
    MipsMachine& m = c.e->m;
    CarpCopy& cc = *c.cc;
    // Primeiro cada entrada das listas uma vez, depois sorteadas (metade entre as de destrutor conferido).
    std::vector<PortEvent> with_dtor;
    for (const PortEvent& pe : events)
        if (dtor_checked(pe.type)) with_dtor.push_back(pe);
    const int total = int(events.size()) + cases;
    for (int i = 0; i < total; ++i) {
        PortEvent pe = i < int(events.size()) ? events[size_t(i)] : events[size_t(rng.range(0, int(events.size()) - 1))];
        if (i >= int(events.size()) && !with_dtor.empty() && rng.chance(0.5))
            pe = with_dtor[size_t(rng.range(0, int(with_dtor.size()) - 1))];
        const drv::EventInfo* info = drv::event_info(pe.type);
        if (!info) continue;
        random_state(rng, c);
        drv::EventDynamicData& d = drv::trigger_manager().event_data;
        std::memset(&d, 0, sizeof d);
        d.instance_index = -1;
        d.trigger = nf::p32_u(drv::trigger_at(rng.range(0, cc.trig_count - 1)));
        d.flag = rng.chance(0.5) ? 1u : 0u;
        d.index = rng.range(-1, 8);
        for (int k = 0; k < 4; ++k) d.position[k] = rng.uni(-1000, 1000);
        put_world(c);
        put_manager(c);
        clear_port_run();
        // A criação.
        ++t_create.cases;
        drv::event_create(pe.type, nf::p32_ptr<uint8_t>(pe.data));
        c.e->call(info->creator, {cc.to_emu(m, pe.data)});
        std::string why = compare_events(c);
        if (why.empty()) why = compare_manager(c);
        if (!why.empty()) {
            t_create.fail(std::string(info->name) + ": " + why);
            continue;
        }
        if (!dtor_checked(pe.type) || drv::trigger_manager().pending.empty()) continue;
        // O destrutor (a entrada +8 da tabela virtual, com o 3 do EventManager::RunEvents).
        ++t_dtor.cases;
        ++dtor_types[pe.type];
        const drv::PendingEvent e = drv::trigger_manager().pending[0];
        drv::mission_trace().clear();
        c.e->trace.clear();
        drv::event_execute(e);
        const uint32_t vt = m.read32(kRing);
        int16_t delta = 0;
        std::memcpy(&delta, m.mem(vt + 8, 2), 2);
        c.e->call(m.read32(vt + 0xc), {kRing + uint32_t(int32_t(delta)), 3});
        why = compare_manager(c);
        if (why.empty()) why = compare_events_from(c, 1);
        if (why.empty()) why = compare_car(c);
        if (why.empty()) why = compare_triggers(c);
        if (!why.empty()) t_dtor.fail(std::string(info->name) + ": " + why);
    }
    return 0;
}

}  // namespace

int run_mission_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                       int cases, uint32_t seed) {
    std::printf("\nparte 15: a missão (o SMissionManager de Paris, as regras, os eventos, o HUD)\n");
    drv::Track track;
    std::string error;
    if (!track.load(disc, "paris_mis01", "", error)) {
        std::printf("  Paris: %s\n", error.c_str());
        return 1;
    }
    // O carro do jogador do porte (o vanquish, como a parte 14 o monta).
    drv::Bigf arc;
    if (!arc.open(disc, "DRIVING/MIS01.VIV")) {
        std::printf("  o MIS01.VIV não abriu\n");
        return 1;
    }
    drv::reset_rigid_globals();
    drv::reset_bond_car_tuning();
    drv::physics_pending_reset();
    drv::simulation_reset(60);
    drv::AttributeCollection attrs;
    drv::CarDesc desc;
    if (!attrs.load(arc, "pvehicle", "vanquish", "paris_mis01", &error) || !drv::read_car_desc(arc, "vanquish", desc, &error)) {
        std::printf("  o vanquish: %s\n", error.c_str());
        return 1;
    }
    drv::BondCar* car = new drv::BondCar();
    {
        const float dir[3] = {0.0f, 0.0f, 1.0f}, pos[3] = {0.0f, 1.0f, 0.0f};
        if (!drv::PBondCar_Construct(*car, 0, "vanquish", desc, attrs, dir, pos)) {
            std::printf("  sem vaga para o corpo do vanquish\n");
            delete car;
            return 1;
        }
    }
    drv::simulation().cars.push_back(car);
    int failures = 0;
    MissionEmu e;
    e.setup(elf, vu0_program, vu0_size);
    CarpCopy cc;
    cc.carp = track.models().carp_data();
    cc.carp_size = track.models().carp_size();
    std::vector<PortEvent> events;
    if (!copy_carp(e.m, track, cc, events, error)) {
        std::printf("  %s\n", error.c_str());
        delete car;
        drv::simulation().cars.clear();
        return 1;
    }
    // Os gatilhos do porte (o Init roda o Restart, que cria os EStartMission: esquecidos).
    drv::WTriggerManager_Init(nf::p32_ptr<drv::UData>(cc.trgr_hdr));
    drv::trigger_manager().pending.clear();
    drv::Scheduler sched(0);
    drv::mission_env() = drv::MissionEnv();
    drv::mission_env().scheduler = &sched;
    drv::GHud_Reset(drv::hud());
    drv::hud_exists() = true;
    Context c;
    c.e = &e;
    c.cc = &cc;
    c.car = car;
    c.sched = &sched;
    Rng rng(seed ^ 0x15u);
    Tally t_load, t_update, t_create, t_dtor;
    std::map<int32_t, uint64_t> dtor_types;
    try {
        if (check_load(c, t_load) == 0) {
            // A vida do carro do jogador no gerente (o SetHitPointLoc do GameLoop_StartUp).
            car->d.hit_point_loc = nf::p32_u(&drv::mission_manager().player_health);
            for (int i = 0; i < cc.aiel_count; ++i) element_at(cc, i);
            check_update(c, cases, rng, t_update);
            check_events(c, events, cases, rng, t_create, t_dtor, dtor_types);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    {
        int per_stage = 0;
        for (int k = 1; k < 65; ++k) per_stage += int(drv::mission_lists().rules[k].size());
        std::map<int32_t, int> types;
        for (const PortEvent& pe : events) types[pe.type]++;
        std::string unknown;
        for (const auto& [type, n] : types)
            if (!drv::event_info(type)) unknown += " " + std::to_string(type) + ":" + std::to_string(n);
        if (!unknown.empty()) std::printf("  números sem a criadora no porte (fora de Paris):%s\n", unknown.c_str());
        std::printf("  Paris: %d regras (%zu sempre, %d nas %d etapas), %zu eventos nas listas 'el' (%zu números), %d "
                    "gatilhos, %d elementos da IA; ponteiros fora do CARP %" PRIu64 "\n",
                    cc.rule_count, drv::mission_lists().rules[0].size(), per_stage, drv::mission_manager().num_stages,
                    events.size(), types.size(), cc.trig_count, cc.aiel_count, cc.outside);
    }
    report("Construct, Load (Paris), ChangeStage(0), Start", t_load, failures);
    report("SMissionManager::Update (estados sorteados)", t_update, failures);
    std::printf("  (no Update: %" PRIu64 " casos criaram eventos, %" PRIu64 " eventos, %" PRIu64 " chamadas externas)\n",
                g_update_with_events, g_update_events, g_update_calls);
    report("a criação dos eventos (as listas de Paris e sorteados)", t_create, failures);
    report("os destrutores portados (missão, HUD, gatilhos)", t_dtor, failures);
    {
        std::string s;
        for (const auto& [type, n] : dtor_types) s += " " + std::to_string(type) + ":" + std::to_string(n);
        std::printf("  (destrutores por número:%s; o interpretador: %" PRIu64 " instruções, %" PRIu64 " chamadas nativas)\n",
                    s.c_str(), e.m.steps(), e.m.native_calls());
    }
    drv::mission_trace_enabled() = false;
    drv::mission_trace().clear();
    drv::mission_reset();
    drv::hud_exists() = false;
    drv::mission_env() = drv::MissionEnv();
    drv::WTriggerManager_Reset();
    delete car;
    drv::simulation().cars.clear();
    drv::simulation_reset(60);
    return failures;
}

}  // namespace drvcheck
