#pragma once
// O ponto de entrada dos programas de janela do port (nightfire.exe, nf_driving.exe; só do port; docs/port.md,
// "Plataforma: interfaces e backends"). Cada programa define nf_app_main (o laço, sem nada do sistema); o backend da
// plataforma tem o ponto de entrada de verdade (entry_win32.cpp: o wWinMain do Windows; entry_macos.mm: o main com o
// NSApplication na thread principal e o jogo noutra, etapa 3c), que instala o relatório de falhas
// (platform/crash_report.h), lê a linha de comando e chama nf_app_main na pilha do jogo (nf::run_main, platform/arena.h:
// em 64 bits a pilha da arena).

// Os argumentos da linha de comando como o argv do C (argv[0] é o programa), válidos durante a chamada; no Windows os
// do CommandLineToArgvW, como antes. Devolve o código de saída do processo.
int nf_app_main(int argc, wchar_t** argv);
