#pragma once
// A thread principal do macOS (só do port; docs/port.md, "macOS: os backends"; os_macos.mm). Um cabeçalho dos backends do
// macOS, sem nada do Objective-C (os .cpp de som e imagem também o incluem).
//
// O AppKit quer a janela, os eventos, os painéis e o cursor na thread principal. O jogo roda noutra thread, a da pilha da
// arena (platform/arena.h, arena_posix.cpp): entry_macos.mm deixa a principal no laço de eventos do NSApplication ([NSApp
// run]) e chama nf_app_main numa thread; os backends levam à principal o que é do AppKit com run_on_main (o
// dispatch_sync na fila principal, que o laço de eventos atende também durante um redimensionamento ou um menu aberto) e
// recebem os eventos como um estado que o jogo lê no Window::pump. Fora do programa de janela (o nf_tests, as
// ferramentas de console), não há laço na principal: run_on_main roda a função na própria thread (ou recusa, para o que
// só a principal pode fazer: can_use_appkit).

#include <functional>

namespace nf::macos {

// O laço de eventos do NSApplication está rodando na thread principal (entry_macos.mm liga antes do [NSApp run]).
bool main_loop_running();
void set_main_loop_running(bool on);
bool on_main_thread();
// Dá para usar o AppKit agora: na própria principal, ou com o laço dela rodando (pelo run_on_main).
bool can_use_appkit();
// Roda fn na thread principal e espera; direto se já é ela ou se o laço não roda.
void run_on_main(const std::function<void()>& fn);
// Põe fn na fila da thread principal sem esperar (sem o laço, roda já).
void post_to_main(std::function<void()> fn);
// Há sessão gráfica (o WindowServer)? Sem ela (um processo de fundo, a CI) o NSApplication não abre: o programa diz e sai.
bool has_window_server();
// O caractere de cada tecla no layout do usuário (os::key_char): refeito na thread principal quando a janela abre e
// quando o layout muda (o TIS/UCKeyTranslate só pode ser chamado nela no macOS 14).
void refresh_key_chars();
// Cmd+Q, o Quit do Dock, o fim da sessão (na thread principal; window_macos.mm): cada janela aberta recebe o "fechar"
// (o pump() dela devolve false, como o WM_CLOSE). false sem nenhuma janela aberta (entry_macos.mm deixa o programa
// terminar na hora).
bool request_quit();

}  // namespace nf::macos
