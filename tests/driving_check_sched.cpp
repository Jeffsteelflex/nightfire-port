// nf_driving_check, parte 9 (tests/driving_check_sched.h): o escalonador. O código ORIGINAL do DRIVING.ELF
// (Scheduler::Init 0x179c48 com o construtor e as quatro agendas, Schedule::AddTask 0x17a158 com o EmptiestBucket,
// RemoveTask 0x17a270, RemoveAllTasks 0x17a320, Scheduler::Reset 0x179c88, ResetTime 0x179fa8 e Run 0x179d40 com os
// Process e o RunTasks) roda no interpretador MIPS lado a lado com o porte (driving/game/scheduler.h), com as mesmas
// operações sorteadas: tarefas nas quatro agendas (o criador, o argumento, a prioridade, o atraso, a contagem),
// remoções, e Runs com o relógio andando de 0 a 15 ticks, o timeScale (1; 0,75; 1,5; 0,5; 2; 1/3), o um-tick-por-volta
// e o modo das cinemáticas (desligado depois de alguns eventos). Compara a sequência de chamadas (o criador e o
// argumento de cada tarefa que roda, as passagens de eventos), as identidades devolvidas, os campos do Scheduler e as
// tarefas de cada balde (na ordem: o criador, o argumento, a prioridade, o atraso, a contagem, a identidade).
//
// Ganchos nativos: a memória (__builtin_new, UMemory::FastAlloc/FastFree, zeradas), memmove, os criadores de evento
// (as entradas da tabela 0x36fda0 apontam para tocos que registram) e o EventManager::RunEvents (0x175690: registra e,
// no modo das cinemáticas, desliga o modo depois de alguns eventos, como uma cinemática que termina).

#include "driving_check_sched.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "data/elf.h"
#include "data/mips_interp.h"
#include "driving/game/scheduler.h"
#include "platform/ps2_float.h"

namespace drvcheck {

namespace {

using nf::MipsMachine;
using R = MipsMachine::Reg;

constexpr uint32_t kTextBegin = 0x107100, kTextEnd = 0x3247fc;
constexpr uint32_t kGp = 0x3cfdf0;
constexpr uint32_t kFnInit = 0x179c48, kFnAddTask = 0x17a158, kFnRemoveTask = 0x17a270, kFnRemoveAll = 0x17a320;
constexpr uint32_t kFnReset = 0x179c88, kFnResetTime = 0x179fa8, kFnRun = 0x179d40, kFnRunEvents = 0x175690;
constexpr uint32_t kClock = 0x32f2a8, kFgScheduler = 0x32f2b0, kEventTable = 0x36fda0;
constexpr int kEvents = 20;
constexpr uint32_t kStubs = 0x01000000;
constexpr uint32_t kHeap = 0x01100000, kHeapEnd = 0x01800000;

struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 99) {}
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

// Uma linha do registro: "c fn arg" (um criador), "e" (os eventos).
struct Log {
    std::vector<std::string> lines;
    void call(int32_t fn, uint32_t arg) { lines.push_back("c " + hex(uint32_t(fn)) + " " + hex(arg)); }
    void events() { lines.push_back("e"); }
};

class SchedEmu {
public:
    MipsMachine m;
    uint32_t heap = kHeap;
    Log log;
    int cinematic_events_left = -1;  // >= 0: no modo das cinemáticas, desliga depois desse número de eventos

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
        m.hook(0x2cfe50, "UMemory::FastFree", [](MipsMachine&) {});
        m.hook(0x2d09a8, "__builtin_delete", [](MipsMachine&) {});
        m.hook(0x2a7f34, "memmove", [](MipsMachine& mm) {
            const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
            if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
            mm.set_gpr(R::v0, mm.gpr(R::a0));
        });
        for (int i = 0; i < kEvents; ++i) {
            const uint32_t stub = kStubs + uint32_t(i) * 8;
            m.write32(kEventTable + uint32_t(i) * 4, stub);
            m.hook(stub, "o criador de evento " + std::to_string(i),
                   [this, i](MipsMachine& mm) { log.call(i, uint32_t(mm.gpr(R::a0))); });
        }
        m.hook(kFnRunEvents, "EventManager::RunEvents", [this](MipsMachine& mm) {
            log.events();
            if (cinematic_events_left >= 0 && cinematic_events_left-- == 0)
                mm.write32(mm.read32(kFgScheduler) + 0x20, 0);
        });
    }
    void call(uint32_t fn, const std::vector<uint32_t>& args, uint32_t t0 = 0, uint32_t t1 = 0, uint32_t t2 = 0) {
        m.set_gpr(R::sp, MipsMachine::kStackTop);
        m.set_gpr(R::t0, t0);
        m.set_gpr(R::t1, t1);
        m.set_gpr(R::t2, t2);
        m.call(fn, args, 50'000'000);
    }
    uint32_t sched() const { return m.read32(kFgScheduler); }
    uint32_t schedule(int k) const { return m.read32(sched() + uint32_t(k) * 4); }
};

// As tarefas de uma agenda do original, balde a balde (o criador, o argumento, a prioridade, o atraso, a contagem, a
// identidade), e a próxima identidade.
std::string emu_tasks(const MipsMachine& m, uint32_t s) {
    std::string out;
    const int32_t n = int32_t(m.read32(s));
    for (int b = 0; b < 8; ++b) {
        const uint32_t cell = m.read32(s + 4 + uint32_t(b) * 4);
        out += "|";
        if (cell == 0 || b >= n) continue;
        const uint32_t head = m.read32(cell);
        for (uint32_t node = m.read32(head); node != head; node = m.read32(node)) {
            const uint32_t pd = m.read32(node + 0x10), cnt = m.read32(node + 0x14);
            out += " " + hex(m.read32(node + 8)) + "," + hex(m.read32(node + 0xc)) + "," + hex(pd & 0xff) + "," +
                   hex(pd >> 16) + "," + hex(cnt & 0xffff) + "," + hex(m.read32(node + 0x18));
        }
    }
    return out + "| " + hex(m.read32(s + 0x24));
}

std::string port_tasks(const drv::Schedule& s) {
    std::string out;
    for (int b = 0; b < 8; ++b) {
        out += "|";
        if (b >= s.buckets) continue;
        for (const drv::SchedTask& t : s.lists[b])
            out += " " + hex(uint32_t(t.fn)) + "," + hex(t.arg) + "," + hex(t.priority) + "," + hex(t.delay) + "," +
                   hex(t.count) + "," + hex(uint32_t(t.id));
    }
    return out + "| " + hex(uint32_t(s.next_id));
}

}  // namespace

int run_sched_checks(const nf::Elf& elf, int cases, uint32_t seed) {
    std::printf("\n== parte 9: o escalonador (Scheduler, Schedule, as tarefas) ==\n");
    SchedEmu e;
    e.setup(elf);
    Rng rng(seed ^ 0x5c4ed);
    int32_t clock = 1000;
    e.m.write32(kClock, uint32_t(clock));
    e.call(kFnInit, {});
    drv::Scheduler s(clock);
    Log plog;
    int cinematic_left = -1;
    s.call = [&](int32_t fn, uint32_t arg) { plog.call(fn, arg); };
    s.run_events = [&] {
        plog.events();
        if (cinematic_left >= 0 && cinematic_left-- == 0) s.cinematic = 0;
    };
    drv::Schedule* port_sched[4] = {s.once.get(), s.sim.get(), s.half.get(), s.quarter.get()};
    std::vector<int32_t> ids;
    uint64_t compared = 0, bad = 0, runs = 0, ticks_ran = 0, discarded = 0, cinematics = 0, adds = 0, removes = 0;
    uint64_t calls = 0;
    std::string first;
    auto fail = [&](const std::string& what) {
        if (bad++ == 0) first = what;
    };
    static const float kScales[] = {1.0f, 1.0f, 1.0f, 0.75f, 1.5f, 0.5f, 2.0f, 1.0f / 3.0f};
    try {
        for (int c = 0; c < cases && bad == 0; ++c) {
            const int op = rng.range(0, 99);
            std::string what;
            if (op < 35) {
                const int k = rng.range(0, 3);
                const int fn = rng.range(1, kEvents - 1), prio = rng.range(0, 7);
                const uint32_t arg = rng.next() & 0xffff;
                const int delay = rng.chance(0.3) ? rng.range(1, 4) : 0;
                const int count = rng.chance(0.4) ? rng.range(1, 4) : 0;
                e.call(kFnAddTask, {e.schedule(k), uint32_t(fn), arg, uint32_t(prio)}, 1, uint32_t(delay),
                       uint32_t(count));
                const int32_t id_emu = int32_t(e.m.gpr(R::v0));
                const int32_t id_port = port_sched[k]->AddTask(fn, arg, prio, delay, count);
                if (id_emu != id_port) fail("AddTask: original " + std::to_string(id_emu) + ", porte " + std::to_string(id_port));
                ids.push_back(id_emu);
                ++adds;
                what = "AddTask";
            } else if (op < 45 && !ids.empty()) {
                const int k = rng.range(0, 3);
                const int32_t id = rng.chance(0.8) ? ids[size_t(rng.range(0, int(ids.size()) - 1))] : rng.range(900, 1200);
                e.call(kFnRemoveTask, {e.schedule(k), uint32_t(id)});
                const bool r_emu = e.m.gpr(R::v0) != 0, r_port = port_sched[k]->RemoveTask(id);
                if (r_emu != r_port) fail("RemoveTask " + std::to_string(id));
                ++removes;
                what = "RemoveTask";
            } else if (op < 47) {
                const int k = rng.range(0, 3);
                e.call(kFnRemoveAll, {e.schedule(k)});
                port_sched[k]->RemoveAllTasks();
                what = "RemoveAllTasks";
            } else if (op < 49) {
                clock += rng.range(0, 3);
                e.m.write32(kClock, uint32_t(clock));
                if (rng.chance(0.5)) {
                    e.call(kFnReset, {});
                    s.Reset(clock);
                    what = "Reset";
                } else {
                    e.call(kFnResetTime, {e.sched()});
                    s.ResetTime(clock);
                    what = "ResetTime";
                }
            } else {
                // Um Run: o relógio anda, às vezes o timeScale, o um-tick e as cinemáticas.
                clock += rng.chance(0.08) ? rng.range(5, 15) : rng.range(0, 3);
                if (rng.chance(0.1)) {
                    const float ts = kScales[rng.range(0, 7)];
                    uint32_t bits;
                    std::memcpy(&bits, &ts, 4);
                    e.m.write32(e.sched() + 0x24, bits);
                    s.time_scale = ts;
                }
                if (rng.chance(0.05)) {
                    const int32_t one = rng.chance(0.5) ? 1 : 0;
                    e.m.write32(e.sched() + 0x1c, uint32_t(one));
                    s.one_tick_per_run = one;
                }
                e.cinematic_events_left = cinematic_left = -1;
                if (rng.chance(0.03)) {
                    e.cinematic_events_left = cinematic_left = rng.range(0, 9);
                    e.m.write32(e.sched() + 0x20, 1);
                    s.cinematic = 1;
                    ++cinematics;
                }
                e.m.write32(kClock, uint32_t(clock));
                const uint64_t before = s.ticks, disc_before = s.discarded;
                e.call(kFnRun, {e.sched(), 0});
                s.Run(clock);
                ticks_ran += s.ticks - before;
                discarded += s.discarded - disc_before;
                ++runs;
                what = "Run";
            }
            // As chamadas e os eventos desde a última operação.
            ++compared;
            if (e.log.lines != plog.lines) {
                size_t i = 0;
                while (i < e.log.lines.size() && i < plog.lines.size() && e.log.lines[i] == plog.lines[i]) ++i;
                fail(what + " (caso " + std::to_string(c) + "): chamada " + std::to_string(i) + " original '" +
                     (i < e.log.lines.size() ? e.log.lines[i] : "-") + "', porte '" +
                     (i < plog.lines.size() ? plog.lines[i] : "-") + "'");
            }
            calls += e.log.lines.size();
            e.log.lines.clear();
            plog.lines.clear();
            // Os campos do Scheduler.
            const uint32_t sp = e.sched();
            uint32_t ts_bits;
            std::memcpy(&ts_bits, &s.time_scale, 4);
            if (int32_t(e.m.read32(sp + 0x14)) != s.last || int32_t(e.m.read32(sp + 0x18)) != s.field_18 ||
                int32_t(e.m.read32(sp + 0x1c)) != s.one_tick_per_run || int32_t(e.m.read32(sp + 0x20)) != s.cinematic ||
                e.m.read32(sp + 0x24) != ts_bits)
                fail(what + " (caso " + std::to_string(c) + "): os campos do Scheduler (último original " +
                     std::to_string(int32_t(e.m.read32(sp + 0x14))) + ", porte " + std::to_string(s.last) + ")");
            // As tarefas de cada agenda.
            for (int k = 0; k < 4; ++k) {
                const std::string a = emu_tasks(e.m, e.schedule(k)), b = port_tasks(*port_sched[k]);
                if (a != b) {
                    fail(what + " (caso " + std::to_string(c) + "): a agenda " + std::to_string(k) + ": original " + a +
                         " porte " + b);
                    break;
                }
            }
        }
    } catch (const std::exception& ex) {
        fail(std::string("o interpretador: ") + ex.what());
    }
    std::printf("  %-58s %8" PRIu64 " casos, %" PRIu64 " diferentes%s%s\n", "Scheduler/Schedule (as operações)", compared,
                bad, bad ? ": " : "", bad ? first.c_str() : "");
    std::printf("  cobertura: %" PRIu64 " tarefas, %" PRIu64 " remoções, %" PRIu64 " Runs, %" PRIu64 " ticks, %" PRIu64
                " Runs com o tempo jogado fora, %" PRIu64 " cinemáticas, %" PRIu64 " chamadas e eventos\n",
                adds, removes, runs, ticks_ran, discarded, cinematics, calls);
    return bad ? 1 : 0;
}

}  // namespace drvcheck
