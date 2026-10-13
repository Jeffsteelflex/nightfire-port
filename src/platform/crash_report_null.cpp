// O relatório de falhas sem o sistema (a opção NF_HEADLESS do Windows; ver backends_null.cpp): uma linha, sem a pilha.

#include "platform/crash_report.h"

#include <cstdio>

namespace nf {

void install_crash_report() {}
void crash_report_exception(void*) { std::fprintf(stderr, "\nFALHA: exceção do sistema\n"); }
void crash_report_here(const char* why) { std::fprintf(stderr, "\nFALHA: %s\n", why); }

}  // namespace nf
