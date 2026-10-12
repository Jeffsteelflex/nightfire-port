#pragma once
// Relatório de falhas: numa exceção não tratada (acesso inválido etc.), escreve no stderr o código, o
// endereço e a pilha de chamadas com os nomes das funções (dbghelp e o PDB gerado junto do .exe).

struct _EXCEPTION_POINTERS;

namespace nf {
void install_crash_report();
// O relatório de uma exceção (o filtro do install_crash_report; a pilha do jogo em x64, platform/arena.h, chama direto).
void crash_report_exception(_EXCEPTION_POINTERS* info);
// O relatório com a pilha de chamadas de agora, sem exceção (a verificação dos ponteiros de 32 bits, ptr32.h).
void crash_report_here(const char* why);
}
