// O escalonador das missões de carro (driving/game/scheduler.h): propriedades lidas no código do jogo, conferidas sem o
// original (a conferência bit a bit contra o Profile.obj fica no nf_driving_check, parte 9): os ticks pelo relógio, o
// desenho só no último, o tempo de 12 ticks jogado fora, a fração do timeScale perdida, os baldes das agendas de meio e
// quarto de ritmo, o atraso e a contagem das tarefas, o modo das cinemáticas e as tarefas do RunTheGame.

#include <string>
#include <vector>

#include "driving/game/scheduler.h"
#include "test_framework.h"

using namespace drv;

namespace {

struct Recorder {
    std::vector<std::string> log;
    void attach(Scheduler& s) {
        s.call = [this](int32_t fn, uint32_t arg) { log.push_back(std::to_string(fn) + ":" + std::to_string(arg)); };
        s.run_events = [this] { log.push_back("e"); };
    }
    int count(const std::string& what) const {
        int n = 0;
        for (const std::string& l : log) n += l == what;
        return n;
    }
};

}  // namespace

TEST(driving_scheduler_ticks_render_and_discard) {
    Scheduler s(100);
    Recorder r;
    r.attach(s);
    s.sim->AddTask(5, 0, 0, 0, 0);   // a simulação, prioridade 0
    s.sim->AddTask(8, 0, 2, 0, 0);   // a câmera, prioridade 2
    s.once->AddTask(4, 0, 6, 0, 0);  // o desenho, por volta
    // Sem tempo: nada, e o último fica.
    s.Run(100);
    CHECK(r.log.empty() && s.last == 100);
    // Três ticks: a simulação e a câmera em cada um (com os eventos), o desenho só no último.
    s.Run(103);
    CHECK(r.count("5:0") == 3 && r.count("8:0") == 3 && r.count("4:0") == 1 && s.last == 103 && s.ticks == 3);
    CHECK(r.log.size() == 3 * 3 + 2 && r.log[0] == "5:0" && r.log[1] == "8:0" && r.log[2] == "e");
    CHECK(r.log[r.log.size() - 2] == "4:0" && r.log.back() == "e");
    // 12 ticks ou mais: jogados fora (nada roda), o último vira o relógio.
    r.log.clear();
    s.Run(115);
    CHECK(r.log.empty() && s.last == 115 && s.discarded == 1);
    s.Run(126);  // 11: roda
    CHECK(r.count("5:0") == 11 && r.count("4:0") == 1);
}

TEST(driving_scheduler_time_scale_loses_the_fraction) {
    // 0,75: um tick de relógio dá 0 (nada, o último fica); dois dão int(1,5) = 1, e o último vira o relógio: a fração
    // se perde, e a 0,75 o jogo anda à metade.
    Scheduler s(0);
    Recorder r;
    r.attach(s);
    s.sim->AddTask(5, 0, 0, 0, 0);
    s.time_scale = 0.75f;
    int32_t clock = 0;
    for (int i = 0; i < 400; ++i) s.Run(++clock);
    CHECK(s.ticks == 200);
    // 1,5: int(1,5) = 1 por tick de relógio, a mesma velocidade de 1.
    Scheduler f(0);
    f.sim->AddTask(5, 0, 0, 0, 0);
    f.time_scale = 1.5f;
    clock = 0;
    for (int i = 0; i < 400; ++i) f.Run(++clock);
    CHECK(f.ticks == 400);
    // O um-tick-por-volta: um tick mesmo com o relógio cinco à frente.
    Scheduler o(0);
    o.sim->AddTask(5, 0, 0, 0, 0);
    o.one_tick_per_run = 1;
    o.Run(5);
    CHECK(o.ticks == 1 && o.last == 5);
}

TEST(driving_scheduler_buckets_delay_and_count) {
    // A meio ritmo: duas tarefas vão para os dois baldes (o mais vazio, o primeiro no empate) e cada uma roda nos ticks
    // da sua paridade.
    Scheduler s(0);
    Recorder r;
    r.attach(s);
    const int32_t a = s.half->AddTask(1, 10, 0, 0, 0);
    const int32_t b = s.half->AddTask(1, 11, 0, 0, 0);
    CHECK(a == 1000 && b == 1001 && s.half->lists[0].size() == 1 && s.half->lists[1].size() == 1);
    CHECK(s.quarter->EmptiestBucket() == 0);
    s.Run(4);  // ticks 1-4: o balde 1 nos ímpares, o 0 nos pares
    CHECK(r.count("1:11") == 2 && r.count("1:10") == 2 && r.log[0] == "1:11");
    // O atraso pula passagens; a contagem tira a tarefa ao chegar a 0.
    Scheduler d(0);
    Recorder rd;
    rd.attach(d);
    d.sim->AddTask(7, 1, 3, 2, 2);  // pula 2, roda 2, sai
    d.Run(6);
    CHECK(rd.count("7:1") == 2 && d.sim->lists[0].empty());
    CHECK(!d.sim->RemoveTask(1000));
    const int32_t id = d.sim->AddTask(7, 2, 3, 0, 0);
    CHECK(d.sim->RemoveTask(id) && d.sim->lists[0].empty());
}

TEST(driving_scheduler_cinematic_and_run_the_game) {
    // As cinemáticas: ticks seguidos até o modo ser desligado (aqui no 6º evento), a agenda por volta a cada 4 ticks.
    Scheduler s(0);
    Recorder r;
    int events = 0;
    r.attach(s);
    s.run_events = [&] {
        r.log.push_back("e");
        if (++events == 6) s.cinematic = 0;
    };
    s.sim->AddTask(5, 0, 0, 0, 0);
    s.once->AddTask(4, 0, 6, 0, 0);
    s.cinematic = 1;
    s.Run(1);
    // Ticks 1-4: 4 eventos da simulação + 1 da volta (no 4º) = 5; o 5º tick dá o 6º evento e desliga.
    CHECK(s.ticks == 5 && r.count("4:0") == 1 && s.last == 1 && s.cinematic == 0);
    // As tarefas do RunTheGame: a simulação na agenda da simulação, o desenho e o áudio na por volta.
    Scheduler g(0);
    RunTheGame_AddTasks(g, false);
    CHECK(g.sim->lists[0].size() == 5 && g.once->lists[0].size() == 2);
    CHECK(g.sim->lists[0].front().fn == kEvSimFrameUpdate && g.sim->lists[0].back().fn == kEvCameraUpdate);
    CHECK(g.once->lists[0].front().fn == kEvRenderFrame && g.once->lists[0].front().priority == 6);
    Recorder rg;
    rg.attach(g);
    g.Run(1);
    // Por prioridade: a simulação (0), a animação, a IA e a câmera (2), o fim (7); depois os eventos; depois a volta.
    const std::vector<std::string> want = {"5:0", "16:0", "1:0", "8:0", "6:0", "e", "4:0", "2:0", "e"};
    CHECK(rg.log == want);
}
