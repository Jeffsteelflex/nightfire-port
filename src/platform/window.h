#pragma once
// Janela Win32 com Raw Input para o mouse (delta sem aceleração do Windows),
// captura de cursor e tela cheia sem bordas. O clique captura o mouse; perder o foco (Alt+Tab) e abrir um menu o
// soltam; Esc é a tecla da pausa do jogo (não solta o mouse sozinho: release_mouse).

#include <windows.h>

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

    HWND hwnd() const { return hwnd_; }
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

    // Só do PC: com um menu aberto o mouse fica solto (sem a captura; o clique não captura) e o cursor do
    // Windows aparece só enquanto o mouse é usado (set_cursor_visible); fechado o menu, a captura volta se
    // estava ligada quando ele abriu.
    void set_menu_mode(bool on);
    bool menu_mode() const { return menu_mode_; }
    void set_cursor_visible(bool on);
    // A posição do cursor na área de desenho, em pixels (falso fora dela ou sem o foco).
    bool cursor_position(float* x, float* y) const;

    // Delta do mouse acumulado desde a última chamada (em contagens do sensor).
    void take_mouse_delta(float& dx, float& dy);
    float take_wheel();
    bool key_down(int vk) const;
    // Teclas pressionadas desde a última chamada (borda de subida).
    bool key_pressed(int vk);

private:
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void capture(bool on);

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;
    bool resized_ = false;
    bool focused_ = true;
    bool captured_ = false;
    bool fullscreen_ = false;
    bool menu_mode_ = false;
    bool capture_after_menu_ = false;  // a captura estava ligada quando o menu abriu
    bool cursor_visible_ = true;       // no modo de menu
    WINDOWPLACEMENT saved_{sizeof(WINDOWPLACEMENT)};
    float mouse_dx_ = 0, mouse_dy_ = 0, wheel_ = 0;
    bool pressed_[256]{};
};

}  // namespace nf
