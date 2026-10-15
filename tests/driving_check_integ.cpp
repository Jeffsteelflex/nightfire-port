// nf_driving_check, parte 20: a ligação das armas com a IA de Paris (tests/driving_check_integ.h). Só despacha para os
// pedaços.

#include "driving_check_integ.h"

#include <cstdio>

namespace drvcheck {

int run_integ_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                     int cases, uint32_t seed, const std::string& only) {
    std::printf("\n== parte 20: as armas ligadas à IA (os tiros, os atores e os personagens, a IA)\n");
    int failures = 0;
    if (only.empty() || only == "tiros")
        failures += run_integ_shell_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    if (only.empty() || only == "atores")
        failures += run_integ_actor_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    if (only.empty() || only == "ia") failures += run_integ_ai_checks(elf, disc, vu0_program, vu0_size, cases, seed);
    std::printf("parte 20: %d diferentes\n", failures);
    return failures;
}

}  // namespace drvcheck
