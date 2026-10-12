// Ver crash_report.h.

#include "platform/crash_report.h"

#include <windows.h>
// dbghelp.h precisa de windows.h antes.
#include <dbghelp.h>

#include <cstdio>
#include <cstring>

namespace nf {
namespace {
void walk(CONTEXT ctx) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    // O PDB ao lado do executável (o zip de lançamento) e na pasta atual; o caminho gravado no executável vem antes.
    char exe[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(nullptr, exe, DWORD(sizeof(exe)));
    char search[MAX_PATH * 2 + 4] = ".";
    if (n > 0 && n < sizeof(exe)) {
        char* slash = std::strrchr(exe, '\\');
        if (slash != nullptr) *slash = 0;
        std::snprintf(search, sizeof(search), "%s;.", exe);
    }
    SymInitialize(process, search, TRUE);
    STACKFRAME64 frame{};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 32; ++depth) {
        if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr)) {
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
}

LONG WINAPI report(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    std::fprintf(stderr, "\nFALHA: exceção 0x%08lx em %p", rec->ExceptionCode, rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
#if defined(_M_X64)
        std::fprintf(stderr, " (%s 0x%08llx)", rec->ExceptionInformation[0] == 0 ? "lendo" : "escrevendo",
                     static_cast<unsigned long long>(rec->ExceptionInformation[1]));
#else
        std::fprintf(stderr, " (%s 0x%08lx)", rec->ExceptionInformation[0] == 0 ? "lendo" : "escrevendo",
                     static_cast<unsigned long>(rec->ExceptionInformation[1]));
#endif
    }
    std::fprintf(stderr, "\n");
    walk(*info->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}
}  // namespace

void install_crash_report() { SetUnhandledExceptionFilter(report); }

void crash_report_exception(_EXCEPTION_POINTERS* info) { report(info); }

void crash_report_here(const char* why) {
    std::fprintf(stderr, "\nFALHA: %s\n", why);
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    walk(ctx);
}
}  // namespace nf
