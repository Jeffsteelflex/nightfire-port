// A troca de pilha da arena no Windows (x64): o backend de nf::arena::call_on_stack (platform/arena.h). Os limites da
// pilha no TEB (o desenrolador do Windows, nas exceções C++ e SEH, só aceita quadros dentro deles), a troca em MASM
// (arena_stack_x64.asm) e o filtro das exceções estruturadas (o relatório de falhas e o fim do processo).

#include "platform/arena.h"

#if NF_PTR64

#include <windows.h>

#include <cstdio>

#include "platform/crash_report.h"

extern "C" void nf_arena_call_on_stack(void (*fn)(void*), void* arg, void* stack_top);

namespace nf::arena {

namespace {
struct StackCall {
    int (*fn)(void*);
    void* arg;
    int result;
};

LONG filter(EXCEPTION_POINTERS* info) {
    crash_report_exception(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

void stack_entry(void* p) {
    auto* call = static_cast<StackCall*>(p);
    __try {
        call->result = call->fn(call->arg);
    } __except (filter(GetExceptionInformation())) {
        std::fflush(stderr);
        ExitProcess(3);
    }
}
}  // namespace

int call_on_stack(int (*fn)(void*), void* arg, void* limit, void* top) {
    StackCall call{fn, arg, 0};
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    void* const old_base = tib->StackBase;
    void* const old_limit = tib->StackLimit;
    tib->StackBase = top;
    tib->StackLimit = limit;
    nf_arena_call_on_stack(stack_entry, &call, top);
    tib->StackBase = old_base;
    tib->StackLimit = old_limit;
    return call.result;
}

}  // namespace nf::arena

#endif
