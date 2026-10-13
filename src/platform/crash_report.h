#pragma once
// Relatório de falhas: numa exceção não tratada (acesso inválido etc.), escreve no stderr o código, o
// endereço e a pilha de chamadas com os nomes das funções. No Windows (crash_report_win32.cpp), o dbghelp e o PDB
// gerado junto do .exe; no macOS será o sinal (SIGSEGV) com o backtrace do sistema (etapa 3).

namespace nf {
void install_crash_report();
// O relatório de uma exceção do sistema (no Windows, um EXCEPTION_POINTERS*: o filtro do install_crash_report; a pilha
// do jogo em x64, platform/arena.h, chama direto).
void crash_report_exception(void* system_exception);
// O relatório com a pilha de chamadas de agora, sem exceção (a verificação dos ponteiros de 32 bits, ptr32.h).
void crash_report_here(const char* why);
}
