// nf_driving_check, parte 10 (tests/driving_check_input.h): a entrada. O código ORIGINAL do DRIVING.ELF roda no
// interpretador MIPS lado a lado com o porte (driving/game/input.h):
//  - os construtores estáticos das tabelas de nomes (as ações e os modos: StringToNumber com o qsort e a busca
//    binária), o PS2PadDevice (o construtor e o Initialize com os 32 escalares) e o InputToAction (o construtor, com a
//    tabela dos nomes dos escalares);
//  - InputToAction::Reset e LoadParseDefFile sobre os .def do controle do MIS01.VIV (drivecfg.def, FrontEnd.def e os
//    oito povcfg): as tabelas de cada escalar (o modo, o limiar, a ação);
//  - quadro a quadro, PS2PadDevice::PollDevice e IOModule::SendGameMessages com o controle sorteado (botões seguros por
//    alguns quadros, os direcionais andando e às vezes todos em zero, as pressões, o controle digital do tipo 4, o eixo
//    invertido, o "mandar tudo", o relógio da repetição) para as tabelas do drivecfg.def e do FrontEnd.def (os modos R e
//    M).
// Compara as tabelas, as ações mandadas (ActionQueueManager::FeedQueue: a ação, o aparelho, o valor) e os valores, o
// relógio e o estado de cada escalar.
//
// Ganchos nativos: a memória (zerada), MEM_fill, MEM_copy, strchr, strncpy, strtok, strcasecmp, strcmp, strcpy,
// sscanf("%f"), qsort (com os dois comparadores do StringToNumber), PAD_getdataptr e PAD_getpadtype (o controle
// sorteado), ActionQueueManager::GetActionQueueManager e FeedQueue (registram), InputConfigManager::Get e IsInverted.

#include "driving_check_input.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/bigf.h"
#include "driving/camera/dafi.h"
#include "driving/game/input.h"
#include "platform/disc_source.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kFnActionTable = 0x1651f8, kFnModeTable = 0x1676d8;
constexpr uint32_t kFnPadCtor = 0x169510, kFnPadInit = 0x168b08, kFnPoll = 0x169288;
constexpr uint32_t kFnMapCtor = 0x167278, kFnMapReset = 0x167678, kFnParse = 0x1674d0, kFnSend = 0x168678;
constexpr uint32_t kCmpByName = 0x2ceae8, kCmpByNumber = 0x2ceb08;

// A RAM emulada.
constexpr uint32_t kPad = 0x01000000;   // os dados do controle (PAD_getdataptr)
constexpr uint32_t kIO = 0x01000100;    // o IOModule (os campos que o SendGameMessages lê)
constexpr uint32_t kMgr = 0x01000400;   // o ActionQueueManager falso
constexpr uint32_t kCfg = 0x01000500;   // o InputConfigManager falso
constexpr uint32_t kDev = 0x01001000;   // o PS2PadDevice (0x89c bytes)
constexpr uint32_t kMap = 0x01002000;   // o InputToAction (0x10 bytes)
constexpr uint32_t kText = 0x01010000;  // o .def
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1234) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
    int range(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
    bool chance(double p) { return double(next() & 0xffffff) < p * double(0x1000000); }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}
uint32_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

class InputEmu {
public:
    MipsMachine m;
    uint32_t heap = kHeap;
    uint32_t strtok_last = 0;
    int pad_type = 7;
    bool inverted = false;
    std::vector<std::string> log;

    const char* cstr(uint32_t a) { return reinterpret_cast<const char*>(m.mem(a, 1)); }

    void setup(const nf::Elf& elf) {
        m.load(elf);
        m.set_gpr(R::gp, kGp);
        m.allow_code(kTextBegin, kTextEnd);
        auto alloc = [this](MipsMachine& mm) {
            const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
            if (heap + n > kHeapEnd) throw nf::MipsError("alocação: a área acabou", heap, n);
            if (n) std::memset(mm.mem(heap, n), 0, n);
            mm.set_gpr(R::v0, heap);
            heap += n;
        };
        m.hook(0x2cfda8, "UMemory::FastAlloc", alloc);
        m.hook(0x2d0910, "__builtin_new", alloc);
        m.hook(0x2d09c8, "operator_new", alloc);
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x260300, "MEM_fill", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memset(mm.mem(dst, n), int(mm.gpr(R::a1) & 0xff), n);
        });
        m.hook(0x2601f0, "MEM_copy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
        });
        m.hook(0x2a9378, "strchr", [this](MipsMachine& mm) {
            const uint32_t a = uint32_t(mm.gpr(R::a0));
            const char* s = cstr(a);
            const char* p = std::strchr(s, int(mm.gpr(R::a1) & 0xff));
            mm.set_gpr(R::v0, p ? a + uint32_t(p - s) : 0);
        });
        m.hook(0x2a9a50, "strncpy", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            bool ended = false;
            for (uint32_t i = 0; i < n; ++i) {
                const uint8_t c = ended ? 0 : mm.mem(src + i, 1)[0];
                if (c == 0) ended = true;
                mm.mem(dst + i, 1)[0] = c;
            }
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        m.hook(0x2aabd8, "strtok", [this](MipsMachine& mm) {
            uint32_t s = uint32_t(mm.gpr(R::a0));
            const std::string delims(cstr(uint32_t(mm.gpr(R::a1))));
            if (s == 0) s = strtok_last;
            if (s == 0) {
                mm.set_gpr(R::v0, 0);
                return;
            }
            auto at = [&](uint32_t a) { return char(mm.mem(a, 1)[0]); };
            while (at(s) && delims.find(at(s)) != std::string::npos) ++s;
            if (at(s) == 0) {
                strtok_last = 0;
                mm.set_gpr(R::v0, 0);
                return;
            }
            uint32_t e = s;
            while (at(e) && delims.find(at(e)) == std::string::npos) ++e;
            if (at(e)) {
                mm.mem(e, 1)[0] = 0;
                strtok_last = e + 1;
            } else {
                strtok_last = 0;
            }
            mm.set_gpr(R::v0, s);
        });
        m.hook(0x2a9198, "strcasecmp", [this](MipsMachine& mm) {
            const int r = drv::dafi_strcasecmp(cstr(uint32_t(mm.gpr(R::a0))), cstr(uint32_t(mm.gpr(R::a1))));
            mm.set_gpr(R::v0, uint64_t(int64_t(r)));
        });
        m.hook(0x2a9508, "strcmp", [this](MipsMachine& mm) {
            const int r = std::strcmp(cstr(uint32_t(mm.gpr(R::a0))), cstr(uint32_t(mm.gpr(R::a1))));
            mm.set_gpr(R::v0, uint64_t(int64_t(r < 0 ? -1 : r > 0 ? 1 : 0)));
        });
        m.hook(0x2a964c, "strcpy", [this](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0));
            const std::string s = cstr(uint32_t(mm.gpr(R::a1)));
            std::memcpy(mm.mem(dst, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
            mm.set_gpr(R::v0, dst);
        });
        m.hook(0x2a9110, "sscanf", [this](MipsMachine& mm) {
            if (std::strcmp(cstr(uint32_t(mm.gpr(R::a1))), "%f") != 0)
                throw nf::MipsError("sscanf: formato não suportado", 0, 0);
            const char* s = cstr(uint32_t(mm.gpr(R::a0)));
            char* end = nullptr;
            const double d = std::strtod(s, &end);
            if (end == s) {
                mm.set_gpr(R::v0, 0);
                return;
            }
            const float f = float(d);
            std::memcpy(mm.mem(uint32_t(mm.gpr(R::a2)), 4), &f, 4);
            mm.set_gpr(R::v0, 1);
        });
        m.hook(0x2a8198, "qsort", [this](MipsMachine& mm) {
            // Os dois comparadores do StringToNumber: pelo nome (+4) e pelo número (+0); entradas de 8 bytes.
            const uint32_t base = uint32_t(mm.gpr(R::a0)), n = uint32_t(mm.gpr(R::a1)), size = uint32_t(mm.gpr(R::a2));
            const uint32_t cmp = uint32_t(mm.gpr(R::a3));
            if (size != 8 || (cmp != kCmpByName && cmp != kCmpByNumber))
                throw nf::MipsError("qsort: comparador desconhecido", cmp, size);
            std::vector<std::pair<uint32_t, uint32_t>> v(n);
            for (uint32_t i = 0; i < n; ++i) v[i] = {mm.read32(base + i * 8), mm.read32(base + i * 8 + 4)};
            std::vector<std::pair<uint32_t, uint32_t>> s = v;
            for (size_t i = 1; i < s.size(); ++i)  // por inserção: as chaves são únicas, a ordem final é a mesma
                for (size_t j = i; j > 0; --j) {
                    const bool less = cmp == kCmpByName
                                          ? std::strcmp(cstr(s[j].second), cstr(s[j - 1].second)) < 0
                                          : int32_t(s[j].first) < int32_t(s[j - 1].first);
                    if (!less) break;
                    std::swap(s[j], s[j - 1]);
                }
            for (uint32_t i = 0; i < n; ++i) {
                mm.write32(base + i * 8, s[i].first);
                mm.write32(base + i * 8 + 4, s[i].second);
            }
        });
        m.hook(0x259b78, "PAD_getdataptr", [](MipsMachine& mm) { mm.set_gpr(R::v0, kPad); });
        m.hook(0x259b90, "PAD_getpadtype", [this](MipsMachine& mm) { mm.set_gpr(R::v0, uint64_t(pad_type)); });
        m.hook(0x165720, "ActionQueueManager::GetActionQueueManager", [](MipsMachine& mm) { mm.set_gpr(R::v0, kMgr); });
        m.hook(0x165850, "ActionQueueManager::FeedQueue", [this](MipsMachine& mm) {
            const uint32_t a = uint32_t(mm.gpr(R::a1));
            log.push_back(hex(mm.read32(a)) + " " + hex(mm.read32(a + 4)) + " " + hex(mm.read32(a + 8)));
        });
        m.hook(0x1664c0, "InputConfigManager::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, kCfg); });
        m.hook(0x166d78, "InputConfigManager::IsInverted",
               [this](MipsMachine& mm) { mm.set_gpr(R::v0, inverted ? 1 : 0); });
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.call(fn, args, 50'000'000);
    }
};

// As tabelas do original (InputTable: a contagem +0, as linhas +8) contra as do porte.
std::string compare_tables(InputEmu& e, const drv::InputMap& map) {
    const uint32_t tables = e.m.read32(kMap + 4);
    for (int i = 0; i < drv::kPadScalars; ++i) {
        const uint32_t t = tables + uint32_t(i) * 0x3c;
        const uint32_t n = e.m.read32(t), entries = e.m.read32(t + 8);
        const std::vector<drv::InputEntry>& p = map.table[i];
        if (n != p.size())
            return std::string(drv::pad_scalar_name(i)) + ": " + std::to_string(n) + " linhas no original, " +
                   std::to_string(p.size()) + " no porte";
        for (uint32_t k = 0; k < n; ++k) {
            const uint32_t a = entries + k * 0xc;
            if (int32_t(e.m.read32(a)) != p[k].method || e.m.read32(a + 4) != fbits(p[k].threshold) ||
                int32_t(e.m.read32(a + 8)) != p[k].action)
                return std::string(drv::pad_scalar_name(i)) + " linha " + std::to_string(k) + ": original " +
                       hex(e.m.read32(a)) + "," + hex(e.m.read32(a + 4)) + "," + hex(e.m.read32(a + 8)) + " porte " +
                       hex(uint32_t(p[k].method)) + "," + hex(fbits(p[k].threshold)) + "," + hex(uint32_t(p[k].action));
        }
    }
    return {};
}

void write_pad(InputEmu& e, const drv::PadData& p) {
    uint8_t* d = e.m.mem(kPad, 0x20);
    std::memset(d, 0, 0x20);
    d[0xc] = uint8_t(p.buttons & 0xff);
    d[0xd] = uint8_t(p.buttons >> 8);
    d[0x10] = p.rx;
    d[0x11] = p.ry;
    d[0x12] = p.lx;
    d[0x13] = p.ly;
    std::memcpy(d + 0x14, p.pressure, 12);
    e.pad_type = p.pad_type;
}

}  // namespace

int run_input_checks(const nf::Elf& elf, const nf::DiscSource& disc, int frames, uint32_t seed) {
    std::printf("\n== parte 10: a entrada (PS2PadDevice, os .def do controle, SendGameMessages) ==\n");
    drv::Bigf arc;
    if (!arc.open(disc, "DRIVING/MIS01.VIV")) {
        std::printf("  o MIS01.VIV não abriu\n");
        return 1;
    }
    const char* kDefs[] = {"drivecfg.def", "FrontEnd.def", "povcfg0.def", "povcfg1.def", "povcfg2.def",
                           "povcfg3.def",  "povcfg4.def",  "povcfg5.def", "povcfg6.def", "povcfg7.def"};
    InputEmu e;
    uint64_t bad = 0, cases = 0, tables = 0, lines = 0, actions = 0, repeats = 0, digital = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };
    try {
        e.setup(elf);
        e.call(kFnActionTable, {1, 0xffff});
        e.call(kFnModeTable, {1, 0xffff});
        e.call(kFnPadCtor, {kDev, 0});
        e.call(kFnPadInit, {kDev});
        e.call(kFnMapCtor, {kMap, kDev});
        // Os campos do IOModule que o SendGameMessages lê.
        const int32_t first_delay = 18, repeat_delay = 9;  // 300 e 150 ms num TIMER de 60
        e.m.write32(kIO + 0xa8, uint32_t(first_delay));
        e.m.write32(kIO + 0xac, uint32_t(repeat_delay));
        Rng rng(seed ^ 0x1af7);
        for (const char* def : kDefs) {
            std::vector<uint8_t> text;
            const int idx = arc.find(std::string("data\\Control\\") + def);
            if (idx < 0 || !arc.read(idx, text)) {
                fail(std::string("sem ") + def);
                continue;
            }
            std::string s(text.begin(), text.end());
            std::memcpy(e.m.mem(kText, uint32_t(s.size() + 1)), s.c_str(), s.size() + 1);
            e.call(kFnMapReset, {kMap});
            e.call(kFnParse, {kMap, kText});
            drv::InputMap map;
            map.LoadParseDefFile(s);
            ++tables;
            for (const auto& t : map.table) lines += t.size();
            ++cases;
            const std::string d = compare_tables(e, map);
            if (!d.empty()) fail(std::string(def) + ": " + d);
            // Os quadros: só para o drivecfg.def (o carro) e o FrontEnd.def (as repetições R e M).
            if (std::strcmp(def, "drivecfg.def") != 0 && std::strcmp(def, "FrontEnd.def") != 0) continue;
            drv::PadDevice dev;
            drv::IOModuleState io;
            io.first_delay = first_delay;
            io.repeat_delay = repeat_delay;
            // O aparelho do original começa zerado (o construtor), como o do porte: zera de novo entre as tabelas.
            std::memset(e.m.mem(kDev + 0x69c, 0x200), 0, 0x200);
            drv::PadData pad;
            int32_t now = 1000;
            std::vector<std::string> plog;
            const drv::InputFeed feed = [&](int32_t a, int32_t dv, float v) {
                plog.push_back(hex(uint32_t(a)) + " " + hex(uint32_t(dv)) + " " + hex(fbits(v)));
            };
            for (int f = 0; f < frames && bad == 0; ++f) {
                // O controle: cada botão troca com probabilidade baixa (fica seguro por alguns quadros).
                for (int b = 0; b < 16; ++b)
                    if (rng.chance(0.08)) pad.buttons ^= uint16_t(1u << b);
                auto stick = [&](uint8_t& v) {
                    if (rng.chance(0.15)) v = uint8_t(rng.chance(0.3) ? (rng.chance(0.5) ? 0 : 255) : rng.range(0, 255));
                };
                stick(pad.lx);
                stick(pad.ly);
                stick(pad.rx);
                stick(pad.ry);
                if (rng.chance(0.03)) pad.lx = pad.ly = pad.rx = pad.ry = 0;
                for (uint8_t& pr : pad.pressure)
                    if (rng.chance(0.1)) pr = uint8_t(rng.chance(0.4) ? (rng.chance(0.5) ? 0 : 255) : rng.range(0, 255));
                if (rng.chance(0.02)) pad.pad_type = pad.pad_type == 4 ? 7 : 4;
                if (pad.pad_type == 4) ++digital;
                if (rng.chance(0.02)) e.inverted = !e.inverted;
                io.inverted = e.inverted;
                io.send_all = rng.chance(0.05) ? 1 : 0;
                now += rng.range(0, 3);
                io.now = now;
                e.m.write32(kIO + 4, uint32_t(io.send_all));
                e.m.write32(kIO + 0xa0, uint32_t(now));
                write_pad(e, pad);
                e.log.clear();
                plog.clear();
                e.call(kFnPoll, {kDev});
                e.call(kFnSend, {kIO, kDev, kMap});
                dev.Poll(pad);
                drv::SendGameMessages(io, dev, map, feed);
                ++cases;
                actions += plog.size();
                for (int i = 0; i < drv::kPadScalars; ++i) repeats += dev.state[i] == 2;
                if (e.log != plog) {
                    size_t i = 0;
                    while (i < e.log.size() && i < plog.size() && e.log[i] == plog[i]) ++i;
                    fail(std::string(def) + " quadro " + std::to_string(f) + ": ação " + std::to_string(i) +
                         " original '" + (i < e.log.size() ? e.log[i] : "-") + "' porte '" +
                         (i < plog.size() ? plog[i] : "-") + "'");
                }
                const uint8_t* prev = e.m.mem(kDev + 0x69c, 0x80);
                const uint8_t* cur = e.m.mem(kDev + 0x71c, 0x80);
                const uint8_t* timer = e.m.mem(kDev + 0x79c, 0x80);
                const uint8_t* state = e.m.mem(kDev + 0x81c, 0x80);
                if (std::memcmp(prev, dev.prev, 0x80) || std::memcmp(cur, dev.cur, 0x80) ||
                    std::memcmp(timer, dev.timer, 0x80) || std::memcmp(state, dev.state, 0x80)) {
                    int k = 0;
                    while (k < 32 && !std::memcmp(cur + k * 4, &dev.cur[k], 4)) ++k;
                    fail(std::string(def) + " quadro " + std::to_string(f) + ": os escalares (o primeiro valor diferente: " +
                         (k < 32 ? drv::pad_scalar_name(k) : "o relógio/estado") + ")");
                }
            }
        }
    } catch (const std::exception& ex) {
        fail(std::string("o interpretador: ") + ex.what());
    }
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", "as tabelas e os quadros", cases, bad,
                bad ? ": " : "", bad ? first.c_str() : "");
    std::printf("  cobertura: %" PRIu64 " tabelas com %" PRIu64 " linhas, %" PRIu64 " ações, %" PRIu64
                " escalares repetindo, %" PRIu64 " quadros com o controle digital, guardas do porte %" PRIu64 "\n",
                tables, lines, actions, repeats, digital, drv::input_guards());
    return bad ? 1 : 0;
}

}  // namespace drvcheck
