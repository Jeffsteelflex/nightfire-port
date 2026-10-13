#pragma once
// nf_driving_check, parte 9: o escalonador do jogo (tests/driving_check_sched.cpp): o Scheduler, as agendas e as tarefas
// ORIGINAIS no interpretador MIPS lado a lado com o porte (driving/game/scheduler.h).

#include <cstdint>

namespace nf {
class Elf;
}

namespace drvcheck {

// Devolve o número de comparações que falharam (0: tudo igual). `cases`: operações sorteadas (tarefas, Runs...).
int run_sched_checks(const nf::Elf& elf, int cases, uint32_t seed);

}  // namespace drvcheck
