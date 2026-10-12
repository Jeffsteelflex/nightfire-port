// Ver crash_report.h.

#include "platform/crash_report.h"

#include <windows.h>
// dbghelp.h precisa de windows.h antes.
#include <dbghelp.h>

#include <cstdio>

namespace nf {
namespace {
LONG WINAPI report(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    std::fprintf(stderr, "\nFALHA: exceção 0x%08lx em %p", rec->ExceptionCode, rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        std::fprintf(stderr, " (%s 0x%08lx)", rec->ExceptionInformation[0] == 0 ? "lendo" : "escrevendo",
                     static_cast<unsigned long>(rec->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "\n");
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 32; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        if (pc == 0) break;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_disp = 0;
        if (SymFromAddr(process, pc, &disp, sym)) {
            if (SymGetLineFromAddr64(process, pc, &line_disp, &line)) {
                std::fprintf(stderr, "  %s +0x%llx  %s:%lu\n", sym->Name, disp, line.FileName, line.LineNumber);
            } else {
                std::fprintf(stderr, "  %s +0x%llx\n", sym->Name, disp);
            }
        } else {
            std::fprintf(stderr, "  %p\n", reinterpret_cast<void*>(static_cast<uintptr_t>(pc)));
        }
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

void install_crash_report() { SetUnhandledExceptionFilter(report); }
}  // namespace nf
