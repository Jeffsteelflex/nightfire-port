#pragma once
// A janela do jogo (só do port; docs/port.md, "Plataforma: interfaces e backends"): o tamanho, o foco, as teclas
// (códigos de platform/keys.h), o mouse em contagens do sensor (sem a aceleração do sistema), a roda, a captura do
// cursor e a tela cheia sem bordas. O clique captura o mouse; perder o foco (Alt+Tab) e abrir um menu o soltam; Esc é a
// tecla da pausa do jogo (não solta o mouse sozinho: release_mouse).
//
// Este cabeçalho não traz nada do sistema: o backend do Windows é window_win32.cpp (Win32, Raw Input para o mouse,
// GetAsyncKeyState para as teclas); o do macOS será o Cocoa (etapa 3). native_handle() é o identificador do sistema
// para o renderizador (o HWND no Windows).

#include <memory>
#include <string>

namespace nf {

class Window {
public:
    Window(const std::wstring& title, int width, int height, bool fullscreen);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Processa mensagens pendentes. Devolve false quando a janela foi fechada.
    bool pump();

    // O identificador da janela no sistema (o HWND no Windows; nulo depois de fechada).
    void* native_handle() const { return handle_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool resized() {
        const bool r = resized_;
        resized_ = false;
        return r;
    }
    bool focused() const { return focused_; }
    bool mouse_captured() const { return captured_; }
    // Solta o mouse (o cursor volta; um clique captura de novo). Esc no jogo é a pausa, que solta o mouse pelo menu
    // (set_menu_mode); o main chama esta na câmera livre e quando o Esc não abre menu nenhum.
    void release_mouse() {
        if (captured_) capture(false);
    }
    void set_title(const std::wstring& title);
    void toggle_fullscreen();
    // Só do PC (opções de vídeo): a janela sem bordas do tamanho do monitor ligada ou desligada, e o
    // tamanho da área de desenho na janela com bordas (centrada no monitor).
    void set_fullscreen(bool on) {
        if (on != fullscreen_) toggle_fullscreen();
    }
    bool fullscreen() const { return fullscreen_; }
    void set_client_size(int width, int height);
    // Só do PC (--bench): a janela por cima das outras (coberta, o Present não mostraria nada).
    void set_topmost();

    // Só do PC: com um menu aberto o mouse fica solto (sem a captura; o clique não captura) e o cursor do
    // sistema aparece só enquanto o mouse é usado (set_cursor_visible); fechado o menu, a captura volta se
    // estava ligada quando ele abriu.
    void set_menu_mode(bool on);
    bool menu_mode() const { return menu_mode_; }
    void set_cursor_visible(bool on);
    // A posição do cursor na área de desenho, em pixels (falso fora dela ou sem o foco).
    bool cursor_position(float* x, float* y) const;

    // Delta do mouse acumulado desde a última chamada (em contagens do sensor).
    void take_mouse_delta(float& dx, float& dy);
    float take_wheel();
    // A tecla (platform/keys.h) está segura agora.
    bool key_down(int key) const;
    // Teclas pressionadas desde a última chamada (borda de subida).
    bool key_pressed(int key);

    // O backend (window_win32.cpp): o estado só do sistema e a função de janela.
    struct Native;
    friend struct WindowEvents;

private:
    void capture(bool on);

    void* handle_ = nullptr;
    std::unique_ptr<Native> native_;
    int width_ = 0, height_ = 0;
    bool resized_ = false;
    bool focused_ = true;
    bool captured_ = false;
    bool fullscreen_ = false;
    bool menu_mode_ = false;
    bool capture_after_menu_ = false;  // a captura estava ligada quando o menu abriu
    bool cursor_visible_ = true;       // no modo de menu
    float mouse_dx_ = 0, mouse_dy_ = 0, wheel_ = 0;
    bool pressed_[256]{};
};

}  // namespace nf
