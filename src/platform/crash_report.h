#pragma once
// Relatório de falhas: numa exceção não tratada (acesso inválido etc.), escreve no stderr o código, o
// endereço e a pilha de chamadas com os nomes das funções. No Windows (crash_report_win32.cpp), o dbghelp e o PDB
// gerado junto do .exe; no macOS e no Linux o sinal (SIGSEGV) com o backtrace do sistema (crash_report_posix.cpp).

namespace nf {
void install_crash_report();
// O relatório de uma exceção do sistema (no Windows, um EXCEPTION_POINTERS*: o filtro do install_crash_report; a pilha
// do jogo em x64, platform/arena.h, chama direto).
void crash_report_exception(void* system_exception);
// O relatório com a pilha de chamadas de agora, sem exceção (a verificação dos ponteiros de 32 bits, ptr32.h).
void crash_report_here(const char* why);
// Só no POSIX (crash_report_posix.cpp; o Windows não tem nem chama): a função que o relatório chama depois de escrever a
// pilha e antes de o processo acabar, e que o os::exit_process chama antes de sair. O registro do programa de janela do
// macOS (platform/app_log_macos.h) espera nela o nightfire.log receber o que ainda está no caminho. Ela roda dentro de
// um sinal: só funções seguras ali (read, write, nanosleep). Nula: nada.
void crash_report_set_flush(void (*flush)());
void crash_report_flush();
}
