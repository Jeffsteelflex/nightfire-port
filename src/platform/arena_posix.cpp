// A pilha do jogo no macOS e no Linux (64 bits): o backend de nf::arena::call_on_stack (platform/arena.h; docs/port.md,
// "macOS: compilação e CI"). No Windows a mesma thread troca de pilha (arena_win32.cpp, o MASM); aqui o jogo roda numa
// thread criada com pthread_attr_setstack sobre a pilha que a arena separou (dentro da janela dos ponteiros de 32 bits:
// o código decompilado guarda endereços de variáveis locais em campos do jogo), e quem chamou espera o fim com join.
//
// O glibc guarda o descritor da thread e o TLS estático no topo da pilha que recebe; o macOS os guarda à parte. Em
// nenhum dos dois a thread ganha página de guarda própria: a de baixo é a que run_on_game_stack protege. Um sinal de
// falha (o acesso inválido, a página de guarda) roda numa pilha alternativa da thread, para o relatório de falhas
// (crash_report_posix.cpp) funcionar também num estouro da pilha.
//
// Nota para a etapa 3c: o AppKit quer a janela e os eventos na thread principal; com o jogo nesta thread, ou a principal
// bombeia os eventos e o jogo pede o resto a ela, ou esta função passa a trocar a pilha na própria thread principal, como
// no Windows (um trecho de assembly de ARM64).

#include "platform/arena.h"

#if NF_PTR64

#include <pthread.h>
#include <signal.h>

#include <cstdio>
#include <cstdlib>

#include "platform/os.h"

namespace nf::arena {

namespace {

struct StackCall {
    int (*fn)(void*);
    void* arg;
    int result;
};

constexpr size_t kAltStack = size_t(64) << 10;

void* stack_entry(void* p) {
    auto* call = static_cast<StackCall*>(p);
    stack_t alt{};
    alt.ss_size = kAltStack;
    alt.ss_sp = std::malloc(kAltStack);
    if (alt.ss_sp != nullptr) sigaltstack(&alt, nullptr);
    call->result = call->fn(call->arg);
    if (alt.ss_sp != nullptr) {
        stack_t off{};
        off.ss_flags = SS_DISABLE;
        sigaltstack(&off, nullptr);
        std::free(alt.ss_sp);
    }
    return nullptr;
}

[[noreturn]] void fatal(const char* what) {
    std::fprintf(stderr, "arena: %s\n", what);
    std::fflush(stderr);
    os::show_error_narrow("Nightfire (64 bits)", what);
    os::exit_process(3);
}

}  // namespace

int call_on_stack(int (*fn)(void*), void* arg, void* limit, void* top) {
    StackCall call{fn, arg, 0};
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) fatal("pthread_attr_init falhou");
    const size_t bytes = size_t(static_cast<char*>(top) - static_cast<char*>(limit));
    if (pthread_attr_setstack(&attr, limit, bytes) != 0) fatal("pthread_attr_setstack recusou a pilha do jogo");
    pthread_t thread;
    if (pthread_create(&thread, &attr, stack_entry, &call) != 0) fatal("não consegui criar a thread do jogo");
    pthread_attr_destroy(&attr);
    pthread_join(thread, nullptr);
    return call.result;
}

}  // namespace nf::arena

#endif
