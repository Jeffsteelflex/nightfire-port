#pragma once
// O escalonador do jogo (Profile.obj do DRIVING.ELF; docs/driving.md seção 14): o Scheduler, as agendas (Schedule) e as
// tarefas. O RunTheGame (0x00175d78) cria as tarefas do jogo e roda `while (estado != 2) { a fila de ações; os
// SYNCTASKs (a entrada); Scheduler::Run }`.
//
// - Quatro agendas: uma por volta do laço (OncePerGameLoop) e três no ritmo da simulação, na ordem em que o construtor as
//   põe no vetor: SimRate (um balde), HalfSimRate (dois: o balde `tick & 1`) e QuarterSimRate (quatro: `tick & 3`). Uma
//   tarefa nova vai para o balde com menos tarefas (o primeiro, no empate).
// - Uma tarefa: o índice do criador do evento (a tabela 0x0036fda0: 1 EAIUpdate, 2 EAudioUpdate, 4 ERenderFrame, 5
//   ESimFrameUpdate, 6 ESimEndFrame, 8 ECameraUpdate, 0x10 EAnimUpdate), o argumento, a prioridade (0-7), um atraso (as
//   passagens puladas antes da primeira chamada) e uma contagem (0 para sempre; ao chegar a 0 a tarefa sai).
// - Run: o relógio (`Clock`, somado pela interrupção do TIMER na taxa do vídeo) menos o último visto, vezes o
//   `timeScale` e truncado, são os ticks a rodar; 0 não faz nada (e o último fica); 12 ou mais são jogados fora (o
//   último vira o relógio); senão, para cada tick, as prioridades 0-7 de cada agenda da simulação, depois os eventos
//   (EventManager::RunEvents, 0x00175690: o trabalho de cada evento é o destrutor), e no último tick a agenda por volta
//   (as 8 prioridades) e os eventos de novo. `oneTickPerRun` limita a um tick por volta. O modo das cinemáticas roda
//   ticks seguidos até alguém desligá-lo, a agenda por volta a cada 4.
// - Peculiaridade mantida (o bug que o nightfire-cxbx corrigiu): a fração do produto pelo `timeScale` se perde a cada
//   Run e o último vira o relógio inteiro, então 0,75 roda a 0,5 e 1,5 roda a 1.
//
// As tarefas chamam `call(fn, arg)` (o criador do evento) e cada passagem de eventos chama `run_events()`; o laço do
// port (nf_driving --drive) liga os criadores ao que já foi portado.

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <vector>

namespace drv {

// TaskRecord (0x1c bytes no PS2): next, prev, o criador (+8), o argumento (+0xc), a prioridade (byte +0x10), o atraso
// (+0x12), a contagem (+0x14), a identidade (+0x18).
struct SchedTask {
    int32_t fn = 0;
    uint32_t arg = 0;
    uint8_t priority = 0;
    uint16_t delay = 0;
    uint16_t count = 0;
    int32_t id = 0;
};

using SchedCall = std::function<void(int32_t fn, uint32_t arg)>;

class Schedule {
public:
    enum Kind { kOncePerGameLoop, kSimRate, kHalfSimRate, kQuarterSimRate };
    // [driving] 0x00179fc0 Schedule::Schedule (e os construtores dos tipos, que só trocam a tabela virtual)
    Schedule(Kind kind, int buckets);
    // [driving] 0x0017a158 Schedule::AddTask: devolve a identidade (1000, 1001...).
    int32_t AddTask(int32_t fn, uint32_t arg, int priority, int delay, int count);
    // [driving] 0x0017a270 Schedule::RemoveTask
    bool RemoveTask(int32_t id);
    // [driving] 0x0017a320 Schedule::RemoveAllTasks
    void RemoveAllTasks();
    // [driving] 0x0017a3d8 Schedule::EmptiestBucket
    int EmptiestBucket() const;
    // [driving] 0x0017a4a8 Schedule::RunTasks
    void RunTasks(int bucket, uint16_t priority, const SchedCall& call);
    // [driving] 0x0017a5a8/0x0017a5c8/0x0017a5e8/0x0017a608 Schedule_*::Process
    void Process(int32_t tick, uint16_t priority, const SchedCall& call);

    Kind kind;
    int buckets;
    std::list<SchedTask> lists[8];
    int32_t next_id = 1000;
};

class Scheduler {
public:
    // [driving] 0x00179a28 Scheduler::Scheduler: as quatro agendas, o último = o relógio, o timeScale 1.
    explicit Scheduler(int32_t clock);
    // [driving] 0x00179c88 Scheduler::Reset
    void Reset(int32_t clock);
    // [driving] 0x00179fa8 Scheduler::ResetTime
    void ResetTime(int32_t clock);
    // [driving] 0x00179d40 Scheduler::Run
    void Run(int32_t clock);

    std::unique_ptr<Schedule> once, sim, half, quarter;
    std::vector<Schedule*> sim_schedules;  // +0x10: SimRate, HalfSimRate, QuarterSimRate
    int32_t last = 0;                      // +0x14
    int32_t field_18 = 0;                  // +0x18 (o ResetTime zera; nada mais o lê)
    int32_t one_tick_per_run = 0;          // +0x1c
    int32_t cinematic = 0;                 // +0x20
    float time_scale = 1.0f;               // +0x24
    SchedCall call;
    std::function<void()> run_events;
    uint64_t ticks = 0, discarded = 0;  // só do port: ticks rodados e Runs que jogaram o tempo fora
};

// Os índices dos criadores de eventos que o RunTheGame agenda (a tabela 0x0036fda0).
enum SchedEvent : int32_t {
    kEvAIUpdate = 1,
    kEvAudioUpdate = 2,
    kEvRenderFrame = 4,
    kEvSimFrameUpdate = 5,
    kEvSimEndFrame = 6,
    kEvCameraUpdate = 8,
    kEvAnimUpdate = 0x10,
};

// [driving] 0x00175d78 RunTheGame (as tarefas): na simulação (ou na agenda por volta, com `once_per_loop`)
// ESimFrameUpdate (0), EAnimUpdate, EAIUpdate (2), ESimEndFrame (7) e ECameraUpdate (2); por volta ERenderFrame (6) e
// EAudioUpdate (7), nessa ordem.
void RunTheGame_AddTasks(Scheduler& s, bool once_per_loop);

}  // namespace drv
