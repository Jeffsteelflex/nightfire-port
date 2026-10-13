#pragma once
// O registro do programa de janela do macOS (só do port; docs/port.md, "macOS: o pacote e a instalação"). Aberto pelo
// Finder, um aplicativo não tem console: a saída padrão e a de erro vão para o /dev/null. Para quem testa ter o que
// anexar num relato, o ponto de entrada do "Nightfire PC.app" (entry_macos.mm, com NF_APP_LOG) grava tudo o que o
// programa escreve nelas, o relatório de falhas incluído (crash_report_posix.cpp), em
// ~/Library/Application Support/Nightfire PC/nightfire.log, substituído a cada partida (o anterior fica como
// nightfire.prev.log), com a versão, o macOS, o modelo do Mac e as GPUs no começo.
//
// Como: as saídas 1 e 2 viram dois canos; uma thread (sem alocar) copia o que chega neles para o registro, linha a linha
// com a pasta do usuário trocada por "~" (platform/app_log.h), e também, como chegou, para onde as saídas apontavam antes
// (o Terminal, o tee da CI; o /dev/null do Finder). Numa falha o relatório espera a thread esvaziar os canos antes de o
// processo acabar (crash_report_set_flush), e o os::exit_process também; o fim normal (stop_app_log, também pelo atexit)
// devolve as saídas e espera a thread. A saída padrão passa a ter buffer de linha (com o cano ela teria o de 4 KB e as
// últimas linhas antes de uma falha se perderiam).
//
// Este cabeçalho não traz nada do sistema nem do Objective-C (o entry_macos.mm e os testes o incluem).

#include <string>

namespace nf::macos {

// Começa o registro na pasta dir (com ou sem a barra no fim; criada antes por quem chama: os::user_files_dir) e devolve o
// caminho dele (vazio se não deu: a saída continua como estava). build é a versão do código (o hash do git, NF_PORT_BUILD;
// nulo: só a do Info.plist). argv vai no cabeçalho (com a pasta do usuário trocada).
std::wstring start_app_log(const std::wstring& dir, const char* build, int argc, char** argv);
// Termina: a saída de antes volta, a thread grava o resto e o registro fecha com o código de saída. Sem registro: nada.
void stop_app_log(int exit_code);

}  // namespace nf::macos
