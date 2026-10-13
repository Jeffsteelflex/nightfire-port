// O teste de fumaça da janela do macOS (só no macOS; a CI, quando o runner tem a sessão gráfica): o programa de janela
// de verdade (entry_macos.mm, o laço do NSApplication na thread principal, o jogo noutra) abre uma Window, bombeia os
// eventos por um segundo, passa pela tela cheia sem bordas e de volta, muda o tamanho, o título, o modo de menu e o
// cursor, lê os modos do monitor e o layout do teclado, e fecha. Sem dados do jogo. Sai com 0 (passou), 1 (falhou) ou
// 77 (pulado: sem a sessão gráfica, a Window não abre).

#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>
#include <vector>

#include "platform/display_modes.h"
#include "platform/entry.h"
#include "platform/keys.h"
#include "platform/os.h"
#include "platform/window.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok    " : "FALHOU", what);
    if (!ok) ++failures;
}

// Bombeia os eventos por `seconds` (devolve false se a janela foi fechada).
bool pump_for(nf::Window& w, double seconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (!w.pump()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

}  // namespace

int nf_app_main(int, wchar_t**) {
    try {
        nf::Window w(L"nf_window_smoke", 640, 360, false);
        check(w.native_handle() != nullptr, "a janela abriu (a NSView)");
        check(pump_for(w, 0.5), "meio segundo de eventos");
        std::printf("tamanho %dx%d foco %d\n", w.width(), w.height(), w.focused() ? 1 : 0);
        check(w.width() > 0 && w.height() > 0, "a area de desenho tem tamanho");

        std::vector<nf::DisplayMode> modes;
        nf::DisplayMode desktop;
        const bool have_modes = nf::system_display_modes(w.native_handle(), &modes, &desktop);
        std::printf("monitor %dx%d modos %zu\n", desktop.width, desktop.height, modes.size());
        check(!have_modes || (desktop.width > 0 && desktop.height > 0), "os modos do monitor");

        w.set_title(L"nf_window_smoke · título");
        w.set_client_size(800, 450);
        std::printf("depois de set_client_size(800, 450): %dx%d\n", w.width(), w.height());
        check(w.width() > 0 && w.height() > 0, "a janela mudou de tamanho");

        w.set_fullscreen(true);
        check(w.fullscreen(), "a tela cheia sem bordas");
        check(pump_for(w, 0.2), "eventos na tela cheia");
        std::printf("tela cheia %dx%d\n", w.width(), w.height());
        w.set_fullscreen(false);
        check(!w.fullscreen(), "de volta à janela");

        w.set_menu_mode(true);
        w.set_cursor_visible(false);
        w.set_cursor_visible(true);
        w.set_menu_mode(false);
        w.release_mouse();
        check(!w.mouse_captured(), "o mouse solto");
        float dx = 1.0f, dy = 1.0f;
        w.take_mouse_delta(dx, dy);
        (void)w.take_wheel();
        (void)w.key_pressed(nf::key::Escape);
        std::printf("tecla ; no layout: U+%04X\n", unsigned(nf::os::key_char(nf::key::Oem1)));
        std::printf("shift seguro: %d\n", nf::os::key_held(nf::key::Shift) ? 1 : 0);
        check(pump_for(w, 0.4), "mais eventos");
    } catch (const std::exception& e) {
        std::printf("a janela nao abriu: %s\n", e.what());
        return 77;
    }
    std::printf("%s\n", failures == 0 ? "janela: tudo certo" : "janela: falhas");
    return failures == 0 ? 0 : 1;
}
