// O relatório de falhas no macOS e no Linux: os sinais de falha (sigaction) e a pilha de chamadas do backtrace. Ver
// crash_report.h. O nome das funções vem da tabela de símbolos do executável (no Linux ele é ligado com -rdynamic; os
// arquivos e as linhas, como os do PDB no Windows, pedem o atos ou o addr2line sobre os endereços impressos).

#include "platform/crash_report.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace nf {

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        default: return "?";
    }
}

void write_text(const char* s) {
    size_t n = std::strlen(s);
    while (n > 0) {
        const ssize_t w = write(STDERR_FILENO, s, n);
        if (w <= 0) break;
        s += w;
        n -= size_t(w);
    }
}

// A pilha de agora (até 64 quadros), sem alocar: backtrace_symbols_fd escreve direto no stderr. Antes, onde o
// executável foi carregado (para o atos -l no macOS; no Linux os endereços já saem relativos, "+0x...", para o
// addr2line): tools/ci/symbolize.py traduz os dois.
void walk() {
    Dl_info self{};
    if (dladdr(reinterpret_cast<void*>(&walk), &self) != 0 && self.dli_fbase != nullptr) {
        char line[512];
        std::snprintf(line, sizeof(line), "imagem %s base %p\n", self.dli_fname != nullptr ? self.dli_fname : "?",
                      self.dli_fbase);
        write_text(line);
    }
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
}

void on_signal(int sig, siginfo_t* info, void*) {
    char line[160];
    std::snprintf(line, sizeof(line), "\nFALHA: sinal %d (%s) em %p\n", sig, signal_name(sig),
                  info != nullptr ? info->si_addr : nullptr);
    write_text(line);
    walk();
    // A ação padrão do sinal (o fim do processo com ele), como sem o relatório.
    struct sigaction dfl {};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

alignas(16) char g_main_alt_stack[size_t(64) << 10];

}  // namespace

void install_crash_report() {
    // O primeiro backtrace carrega o desenrolador (no Linux, a libgcc_s): melhor aqui do que dentro de um sinal.
    void* warm[1];
    backtrace(warm, 1);
    // Uma pilha alternativa para a thread principal (um estouro da pilha ainda chega aqui); a do jogo tem a sua
    // (arena_posix.cpp).
    stack_t alt{};
    alt.ss_sp = g_main_alt_stack;
    alt.ss_size = sizeof(g_main_alt_stack);
    sigaltstack(&alt, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int s : kSignals) sigaction(s, &sa, nullptr);
}

// O "system_exception" daqui é o siginfo_t de um sinal.
void crash_report_exception(void* system_exception) {
    const auto* info = static_cast<const siginfo_t*>(system_exception);
    char line[160];
    std::snprintf(line, sizeof(line), "\nFALHA: sinal %d (%s) em %p\n", info != nullptr ? info->si_signo : 0,
                  info != nullptr ? signal_name(info->si_signo) : "?", info != nullptr ? info->si_addr : nullptr);
    write_text(line);
    walk();
}

void crash_report_here(const char* why) {
    std::fflush(stderr);
    write_text("\nFALHA: ");
    write_text(why);
    write_text("\n");
    walk();
}

}  // namespace nf
