// O ponto de entrada dos programas de janela no Windows (o subsistema WIN32): o wWinMain. Ver platform/entry.h. As
// mensagens da janela são bombeadas pelo laço de cada programa (Window::pump, platform/window_win32.cpp).

#include "platform/entry.h"

#include <windows.h>
#include <shellapi.h>

#include "platform/arena.h"
#include "platform/crash_report.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    nf::install_crash_report();
    // 64 bits: o programa roda na pilha da arena (platform/arena.h); 32 bits: direto.
    return nf::run_main([] {
        // Os argumentos como o parse_args de antes os lia: o CommandLineToArgvW, sem cópia (a memória é do sistema,
        // fora da arena).
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        const int code = nf_app_main(argc, argv);
        LocalFree(argv);
        return code;
    });
}
