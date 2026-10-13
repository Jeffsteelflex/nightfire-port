// O ponto de entrada dos programas de janela no macOS (platform/entry.h; docs/port.md, "macOS: os backends"). Compilado
// com ARC (-fobjc-arc).
//
// A decisão das threads: o AppKit quer a janela e os eventos na thread principal, e o jogo roda na pilha da arena
// (platform/arena.h: os campos do jogo guardam endereços de variáveis locais, e a pilha tem de ficar na janela dos
// ponteiros de 32 bits), que no macOS é a de uma thread criada com pthread_attr_setstack (arena_posix.cpp). Então a thread
// principal fica com o laço de eventos ([NSApp run]) e o jogo roda noutra: uma thread que chama nf::run_main (que cria a
// da pilha da arena e espera por ela). Os backends levam à principal o que é do AppKit (platform/main_thread_macos.h) e
// os eventos voltam como um estado que o Window::pump lê. A outra saída, trocar a pilha na própria thread principal com
// um trecho de assembly de ARM64 e x86_64 (como o arena_stack_x64.asm do Windows), deixaria o jogo bombear os eventos ele
// mesmo, mas a janela pararia de responder durante as cargas (como no Windows) e o assembly teria de ser escrito e
// conferido sem um Mac; a thread não pede nada disso.
//
// O fim: quando nf_app_main volta, a thread do jogo para o laço da principal ([NSApp stop:] mais um evento vazio para
// acordá-lo) e o main devolve o código. Cmd+Q e o Quit do Dock não matam o processo: fecham as janelas (o pump() devolve
// false) e o jogo termina como no Windows.
//
// Sem sessão gráfica (um processo de fundo, a CI, uma sessão SSH): o NSApplication não abre; o programa roda sem o laço
// (as opções de linha de comando funcionam; a Window recusa abrir, com a mensagem).
//
// O "Nightfire PC.app" (etapa 4; NF_APP_LOG, só no alvo nightfire) grava a saída padrão e a de erro no nightfire.log
// (platform/app_log_macos.h): aberto pelo Finder ele não tem console. Os argumentos "-psn_..." que o macOS ainda passa
// em alguns casos (a primeira abertura de um aplicativo baixado) saem antes do nf_app_main, que trata qualquer argumento
// como uma linha de comando (sem a tela de entrada).

#include "platform/entry.h"

#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform/app_log_macos.h"
#include "platform/arena.h"
#include "platform/crash_report.h"
#include "platform/main_thread_macos.h"
#include "platform/os.h"

@interface NFAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation NFAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender;
    // Com uma janela aberta, ela "fecha" e o jogo sai do laço sozinho; sem nenhuma, termina já.
    return nf::macos::request_quit() ? NSTerminateCancel : NSTerminateNow;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return NO;
}
@end

namespace {

struct GameRun {
    int argc = 0;
    wchar_t** argv = nullptr;
    int code = 0;
};

// O menu do aplicativo (sem ele não há Cmd+Q nem Cmd+H) e o View com a tela cheia (Ctrl+Cmd+F: o toggleFullScreen: da
// janela do jogo, a tela cheia sem bordas do port). Os textos em inglês, como os do jogo.
void build_menu() {
    NSString* name = [[NSProcessInfo processInfo] processName];
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    NSMenu* app_menu = [[NSMenu alloc] init];
    [app_menu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
    app_item.submenu = app_menu;
    NSMenuItem* view_item = [[NSMenuItem alloc] init];
    [bar addItem:view_item];
    NSMenu* view_menu = [[NSMenu alloc] initWithTitle:@"View"];
    NSMenuItem* full = [view_menu addItemWithTitle:@"Toggle Full Screen"
                                            action:@selector(toggleFullScreen:)
                                     keyEquivalent:@"f"];
    full.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    view_item.submenu = view_menu;
    NSApp.mainMenu = bar;
}

void* game_thread(void* p) {
    auto* run = static_cast<GameRun*>(p);
    run->code = nf::run_main([run] { return nf_app_main(run->argc, run->argv); });
    // O jogo terminou: o laço da principal para (o stop só vale depois do evento em andamento: um evento vazio o acorda).
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp stop:nil];
      NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSZeroPoint
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
      [NSApp postEvent:wake atStart:YES];
    });
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    nf::install_crash_report();
    // Sem os "-psn_..." do macOS (o número do processo que o Finder ainda passa às vezes).
    std::vector<char*> kept;
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && argv[i] != nullptr && std::strncmp(argv[i], "-psn_", 5) == 0) continue;
        kept.push_back(argv[i]);
    }
    argc = int(kept.size());
    kept.push_back(nullptr);
    argv = kept.data();
#if defined(NF_APP_LOG)
    // O registro: com o cano no lugar da saída, o stdout teria o buffer de 4 KB; com linhas, uma falha não leva as
    // últimas (o setvbuf antes de qualquer escrita).
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#if defined(NF_PORT_BUILD)
    const char* build = NF_PORT_BUILD;
#else
    const char* build = nullptr;
#endif
    const std::wstring log_path = nf::macos::start_app_log(nf::os::user_files_dir(L"Nightfire PC"), build, argc, argv);
    if (!log_path.empty()) std::fprintf(stderr, "registro: %s\n", nf::os::to_utf8(log_path).c_str());
#endif
    // Os argumentos em texto largo (UTF-8 para UTF-32), vivos até o fim, como o argv do CommandLineToArgvW no Windows.
    std::vector<std::wstring> args;
    args.reserve(size_t(argc));
    for (int i = 0; i < argc; ++i) args.push_back(nf::os::from_utf8(argv[i] != nullptr ? argv[i] : ""));
    std::vector<wchar_t*> wargv;
    for (std::wstring& a : args) wargv.push_back(a.data());
    wargv.push_back(nullptr);
    GameRun run;
    run.argc = argc;
    run.argv = wargv.data();

    if (!nf::macos::has_window_server()) {
        // Sem sessão gráfica: sem o AppKit (a Window recusa abrir; o --list-adapters, o --pack-disc funcionam).
        const int code = nf::run_main([&run] { return nf_app_main(run.argc, run.argv); });
#if defined(NF_APP_LOG)
        nf::macos::stop_app_log(code);
#endif
        return code;
    }

    @autoreleasepool {
        [NSApplication sharedApplication];
        NFAppDelegate* delegate = [[NFAppDelegate alloc] init];
        NSApp.delegate = delegate;
        build_menu();
        // Sem o App Nap enquanto o programa roda (o macOS reduziria os temporizadores e a CPU de um jogo com a janela
        // coberta ou noutro Espaço; o Windows não faz isso). O sistema ainda pode dormir.
        id<NSObject> activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
                              reason:@"o laço do jogo"];
        // Antes da thread do jogo: o run_on_main dela vai à fila principal, que o [NSApp run] atende.
        nf::macos::set_main_loop_running(true);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, size_t(1) << 20);  // só espera a thread da pilha da arena
        pthread_t thread;
        if (pthread_create(&thread, &attr, game_thread, &run) != 0) {
            nf::macos::set_main_loop_running(false);
            std::fprintf(stderr, "entry: não consegui criar a thread do jogo\n");
            return 3;
        }
        pthread_attr_destroy(&attr);
        [NSApp run];
        nf::macos::set_main_loop_running(false);
        pthread_join(thread, nullptr);
        [[NSProcessInfo processInfo] endActivity:activity];
    }
#if defined(NF_APP_LOG)
    nf::macos::stop_app_log(run.code);
#endif
    return run.code;
}
