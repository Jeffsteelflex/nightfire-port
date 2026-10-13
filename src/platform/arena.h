#pragma once
// A arena da versão de 64 bits (docs/port.md, "64 bits"; platform/ptr32.h).
//
// Os campos de ponteiro das estruturas do jogo guardam, em x64, o deslocamento do objeto dentro de uma janela de 4 GB
// que começa na imagem do executável. Para que todo endereço que o jogo guarda caiba nela, a arena reserva (sem
// comprometer) a maior região livre da janela logo depois da imagem e serve dali:
//  - páginas (alloc_pages): o heap do jogo (psiMem_Init), a pilha do jogo;
//  - um alocador geral (alloc/free), que é o operator new do port em x64 (platform/arena_new.cpp): std::vector,
//    std::string e new caem na janela, então os buffers que o port entrega ao jogo (arquivos lidos, os dados de teste,
//    os textos do PC) já são endereçáveis por 32 bits;
//  - a pilha onde o jogo roda (run_on_game_stack): o código decompilado guarda endereços de variáveis locais (os testes
//    de colisão, o DroneImpact_tag das explosões) em campos do jogo.
// As globais e as strings literais estão na imagem, dentro da janela, sem nada a fazer.
//
// Em 32 bits (x86) nada disto é usado: o heap do jogo continua sendo o VirtualAlloc de antes e não há troca de pilha.

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "platform/ptr32.h"

namespace nf::arena {

#if NF_PTR64

struct Stats {
    uintptr_t window_base = 0;  // __ImageBase
    uintptr_t begin = 0;        // a região reservada
    uintptr_t end = 0;
    uint64_t committed = 0;     // bytes comprometidos agora
    uint64_t in_use = 0;        // bytes entregues e não liberados (classes e blocos grandes)
    uint64_t peak = 0;
    uint64_t allocations = 0;   // chamadas a alloc/alloc_pages desde o início
    uint32_t chunks_used = 0;   // pedaços de 64 KB em uso
    uint32_t chunks_total = 0;
};

// Reserva a região (a primeira chamada a qualquer função também reserva). Falha: mensagem e ExitProcess.
void init();
// O endereço está na janela de 4 GB (o que um P32 consegue guardar)?
bool in_window(const void* p);
// Alocador geral (alinhamento até 16; maior: páginas de 64 KB). Nunca devolve nulo (sem memória: std::bad_alloc).
void* alloc(size_t size, size_t align = 16);
void free(void* p);
// Bytes utilizáveis do bloco (a classe ou as páginas).
size_t usable_size(const void* p);
// Páginas comprometidas e zeradas, alinhadas a 64 KB.
void* alloc_pages(size_t bytes);
void free_pages(void* p);
Stats stats();

// Chama fn(arg) numa pilha de `stack_bytes` dentro da arena e devolve o resultado. Uma exceção estruturada não tratada
// (acesso inválido...) vira o relatório de falhas (platform/crash_report.h) e o fim do processo.
int run_on_game_stack(int (*fn)(void*), void* arg, size_t stack_bytes = size_t(16) << 20);
// O backend da troca de pilha (arena_win32.cpp no Windows: os limites da pilha no TEB, a troca em MASM e o filtro de
// exceções estruturadas): chama fn(arg) com a pilha entre limit (o fim de baixo) e top e devolve o resultado. No macOS
// (arena_posix.cpp): uma thread com pthread_attr_setstack dentro da arena, esperada com join.
int call_on_stack(int (*fn)(void*), void* arg, void* limit, void* top);

#endif

}  // namespace nf::arena

namespace nf {

// O corpo de um main: em 64 bits na pilha da arena (os campos do jogo guardam endereços de variáveis locais); em 32 bits
// chamado direto, como antes.
template <class F>
int run_main(F&& body) {
#if NF_PTR64
    using Body = std::remove_reference_t<F>;
    return arena::run_on_game_stack([](void* p) -> int { return (*static_cast<Body*>(p))(); }, &body);
#else
    return body();
#endif
}

}  // namespace nf
