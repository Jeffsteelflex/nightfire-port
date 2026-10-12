#pragma once
// Relatório de falhas: numa exceção não tratada (acesso inválido etc.), escreve no stderr o código, o
// endereço e a pilha de chamadas com os nomes das funções (dbghelp e o PDB gerado junto do .exe).

namespace nf {
void install_crash_report();
}
