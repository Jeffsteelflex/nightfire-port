// O escalonador do jogo (scheduler.h).

#include "driving/game/scheduler.h"

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

// [driving] 0x00179fc0 Schedule::Schedule
Schedule::Schedule(Kind k, int n) : kind(k), buckets(n) {}

// [driving] 0x0017a3d8 Schedule::EmptiestBucket
int Schedule::EmptiestBucket() const {
    // O menor tamanho (sem sinal) a partir de 999999; o primeiro no empate; -1 sem baldes.
    uint32_t best = 999999;
    int at = -1;
    for (int b = 0; b < buckets; ++b) {
        const uint32_t n = uint32_t(lists[b].size());
        if (n < best) {
            best = n;
            at = b;
        }
    }
    return at;
}

// [driving] 0x0017a158 Schedule::AddTask
int32_t Schedule::AddTask(int32_t fn, uint32_t arg, int priority, int delay, int count) {
    const int b = EmptiestBucket();
    SchedTask t;
    t.fn = fn;
    t.arg = arg;
    t.priority = uint8_t(priority);
    t.delay = uint16_t(delay);
    t.count = uint16_t(count);
    t.id = next_id;
    // O PS2 indexa o balde -1 (antes do primeiro) numa agenda sem baldes; nenhuma é criada assim.
    if (b >= 0) lists[b].push_back(t);
    return next_id++;
}

// [driving] 0x0017a270 Schedule::RemoveTask
bool Schedule::RemoveTask(int32_t id) {
    // Os 8 baldes (também os que a agenda não usa: listas vazias), a primeira tarefa com a identidade.
    for (std::list<SchedTask>& l : lists)
        for (auto it = l.begin(); it != l.end(); ++it)
            if (it->id == id) {
                l.erase(it);
                return true;
            }
    return false;
}

// [driving] 0x0017a320 Schedule::RemoveAllTasks
void Schedule::RemoveAllTasks() {
    for (std::list<SchedTask>& l : lists) l.clear();
}

// [driving] 0x0017a4a8 Schedule::RunTasks
void Schedule::RunTasks(int bucket, uint16_t priority, const SchedCall& call) {
    std::list<SchedTask>& l = lists[bucket & 7];
    for (auto it = l.begin(); it != l.end();) {
        SchedTask& t = *it;
        if (t.priority != priority) {
            ++it;
            continue;
        }
        if (t.delay != 0) {
            --t.delay;
            ++it;
            continue;
        }
        if (call) call(t.fn, t.arg);
        // A contagem: 0 é para sempre; ao chegar a 0 a tarefa sai (o próximo foi lido antes do RemoveTask).
        if (t.count == 0 || --t.count != 0) {
            ++it;
            continue;
        }
        const int32_t id = t.id;
        ++it;
        RemoveTask(id);
    }
}

// [driving] 0x0017a5a8 Schedule_OncePerGameLoop::Process
// [driving] 0x0017a5c8 Schedule_SimRate::Process
// [driving] 0x0017a5e8 Schedule_HalfSimRate::Process
// [driving] 0x0017a608 Schedule_QuarterSimRate::Process
void Schedule::Process(int32_t tick, uint16_t priority, const SchedCall& call) {
    switch (kind) {
    case kHalfSimRate: RunTasks(tick & 1, priority, call); break;
    case kQuarterSimRate: RunTasks(tick & 3, priority, call); break;
    default: RunTasks(0, priority, call); break;
    }
}

// [driving] 0x00179a28 Scheduler::Scheduler
Scheduler::Scheduler(int32_t clock) : last(clock) {
    once = std::make_unique<Schedule>(Schedule::kOncePerGameLoop, 1);
    sim = std::make_unique<Schedule>(Schedule::kSimRate, 1);
    half = std::make_unique<Schedule>(Schedule::kHalfSimRate, 2);
    quarter = std::make_unique<Schedule>(Schedule::kQuarterSimRate, 4);
    sim_schedules = {sim.get(), half.get(), quarter.get()};
}

// [driving] 0x00179c88 Scheduler::Reset
void Scheduler::Reset(int32_t clock) {
    time_scale = 1.0f;
    cinematic = 0;
    one_tick_per_run = 0;
    last = clock;
}

// [driving] 0x00179fa8 Scheduler::ResetTime
void Scheduler::ResetTime(int32_t clock) {
    field_18 = 0;
    last = clock;
}

// [driving] 0x00179d40 Scheduler::Run
void Scheduler::Run(int32_t clock) {
    auto sim_tick = [&](int32_t t) {
        for (int p = 0; p < 8; ++p)
            for (size_t i = 0; i < sim_schedules.size(); ++i) sim_schedules[i]->Process(t, uint16_t(p), call);
        if (run_events) run_events();
        ++ticks;
    };
    auto loop_pass = [&](int32_t t) {
        for (int p = 0; p < 8; ++p) once->Process(t, uint16_t(p), call);
        if (run_events) run_events();
    };
    if (cinematic != 0) {
        // As cinemáticas: ticks seguidos até o modo ser desligado; a agenda por volta a cada 4.
        int32_t t = last + 1;
        int n = 4;
        for (;;) {
            --n;
            sim_tick(t);
            if (n < 1) {
                n = 4;
                loop_pass(t);
            }
            ++t;
            if (cinematic == 0) break;
        }
        last = clock;
        return;
    }
    int32_t end = clock;
    if (one_tick_per_run != 0) end = clock == last ? clock : last + 1;
    // O passo: (relógio - último) x timeScale, truncado (cvt.s.w, mul.s, cvt.w.s do EE).
    const int32_t dt = ee::cvt_w(ee::mul(ee::cvt_s(end - last), time_scale));
    if (dt == 0) return;
    const int32_t target = last + dt;
    if (target - last >= 12) {
        ++discarded;
        last = clock;
        return;
    }
    for (int32_t t = last + 1; t <= target; ++t) {
        sim_tick(t);
        if (t == target) loop_pass(t);
    }
    last = clock;
}

// [driving] 0x00175d78 RunTheGame (as tarefas)
void RunTheGame_AddTasks(Scheduler& s, bool once_per_loop) {
    Schedule* sim = once_per_loop ? s.once.get() : s.sim.get();
    Schedule* loop = s.once.get();
    sim->AddTask(kEvSimFrameUpdate, 0, 0, 0, 0);
    sim->AddTask(kEvAnimUpdate, 0, 2, 0, 0);
    sim->AddTask(kEvAIUpdate, 0, 2, 0, 0);
    loop->AddTask(kEvRenderFrame, 0, 6, 0, 0);
    loop->AddTask(kEvAudioUpdate, 0, 7, 0, 0);
    sim->AddTask(kEvSimEndFrame, 0, 7, 0, 0);
    sim->AddTask(kEvCameraUpdate, 0, 2, 0, 0);
}

}  // namespace drv
