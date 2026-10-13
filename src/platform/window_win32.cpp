// A janela no Windows (Win32): o backend de platform/window.h. Raw Input para o mouse (delta sem aceleração do
// Windows), captura de cursor (ClipCursor) e tela cheia sem bordas. As teclas são os "virtual-key codes" do Windows,
// que são os códigos do port (platform/keys.h, conferidos abaixo).

#include "platform/window.h"

#include <windows.h>

#include <algorithm>
#include <stdexcept>

#include "platform/keys.h"

namespace nf {

// Os códigos do port são os VK_ do Windows (platform/keys.h): esta janela passa os dela direto.
static_assert(key::LButton == VK_LBUTTON && key::RButton == VK_RBUTTON && key::MButton == VK_MBUTTON &&
              key::XButton1 == VK_XBUTTON1 && key::XButton2 == VK_XBUTTON2);
static_assert(key::Back == VK_BACK && key::Tab == VK_TAB && key::Return == VK_RETURN && key::Shift == VK_SHIFT &&
              key::Control == VK_CONTROL && key::Menu == VK_MENU && key::Capital == VK_CAPITAL &&
              key::Escape == VK_ESCAPE && key::Space == VK_SPACE);
static_assert(key::Prior == VK_PRIOR && key::Next == VK_NEXT && key::End == VK_END && key::Home == VK_HOME &&
              key::Left == VK_LEFT && key::Up == VK_UP && key::Right == VK_RIGHT && key::Down == VK_DOWN &&
              key::Insert == VK_INSERT && key::Delete == VK_DELETE);
static_assert(key::LWin == VK_LWIN && key::RWin == VK_RWIN && key::Apps == VK_APPS && key::Numpad0 == VK_NUMPAD0 &&
              key::Numpad7 == VK_NUMPAD7 && key::Numpad9 == VK_NUMPAD9 && key::Multiply == VK_MULTIPLY &&
              key::Add == VK_ADD && key::Subtract == VK_SUBTRACT && key::Decimal == VK_DECIMAL &&
              key::Divide == VK_DIVIDE);
static_assert(key::F1 == VK_F1 && key::F2 == VK_F2 && key::F3 == VK_F3 && key::F4 == VK_F4 && key::F9 == VK_F9 &&
              key::F12 == VK_F12 && key::F24 == VK_F24);
static_assert(key::LShift == VK_LSHIFT && key::RShift == VK_RSHIFT && key::LControl == VK_LCONTROL &&
              key::RControl == VK_RCONTROL && key::LMenu == VK_LMENU && key::RMenu == VK_RMENU);
static_assert(key::Oem1 == VK_OEM_1 && key::OemPlus == VK_OEM_PLUS && key::OemComma == VK_OEM_COMMA &&
              key::OemMinus == VK_OEM_MINUS && key::OemPeriod == VK_OEM_PERIOD && key::Oem2 == VK_OEM_2 &&
              key::Oem3 == VK_OEM_3 && key::Oem4 == VK_OEM_4 && key::Oem5 == VK_OEM_5 && key::Oem6 == VK_OEM_6 &&
              key::Oem7 == VK_OEM_7 && key::Oem102 == VK_OEM_102 && key::AbntC1 == 0xC1);

struct Window::Native {
    WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
};

namespace {
constexpr wchar_t kClassName[] = L"NightfirePortWindow";
HWND hwnd_of(const Window& w) { return static_cast<HWND>(w.native_handle()); }
}  // namespace

// A função de janela (amiga de Window: escreve o estado dela).
struct WindowEvents {
    static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT handle(Window& w, UINT, WPARAM, LPARAM);
};

Window::Window(const std::wstring& title, int width, int height, bool fullscreen) : native_(std::make_unique<Native>()) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &WindowEvents::wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    handle_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
    if (!handle_) throw std::runtime_error("CreateWindowEx falhou");
    HWND hwnd = hwnd_of(*this);

    RAWINPUTDEVICE mouse{0x01, 0x02, 0, hwnd};  // página genérica, uso: mouse
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));

    ShowWindow(hwnd, SW_SHOW);
    if (fullscreen) toggle_fullscreen();
    RECT cr;
    GetClientRect(hwnd, &cr);
    width_ = cr.right - cr.left;
    height_ = cr.bottom - cr.top;
}

Window::~Window() {
    capture(false);
    if (handle_) DestroyWindow(hwnd_of(*this));
}

bool Window::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return handle_ != nullptr;
}

void Window::set_title(const std::wstring& title) { SetWindowTextW(hwnd_of(*this), title.c_str()); }

void Window::toggle_fullscreen() {
    HWND hwnd = hwnd_of(*this);
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!fullscreen_) {
        MONITORINFO mi{sizeof(mi)};
        GetWindowPlacement(hwnd, &native_->saved);
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &native_->saved);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = !fullscreen_;
    if (captured_) capture(true);  // recalcula a área de ClipCursor
}

void Window::set_client_size(int width, int height) {
    if (fullscreen_ || width <= 0 || height <= 0) return;
    HWND hwnd = hwnd_of(*this);
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    const int w = r.right - r.left, h = r.bottom - r.top;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& work = mi.rcWork;
    const int x = std::max<int>(work.left, work.left + (work.right - work.left - w) / 2);
    const int y = std::max<int>(work.top, work.top + (work.bottom - work.top - h) / 2);
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOOWNERZORDER);
    if (captured_) capture(true);
}

void Window::set_topmost() {
    SetWindowPos(hwnd_of(*this), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void Window::take_mouse_delta(float& dx, float& dy) {
    dx = mouse_dx_;
    dy = mouse_dy_;
    mouse_dx_ = mouse_dy_ = 0;
}

float Window::take_wheel() {
    const float w = wheel_;
    wheel_ = 0;
    return w;
}

bool Window::key_down(int key) const { return focused_ && (GetAsyncKeyState(key) & 0x8000) != 0; }

bool Window::key_pressed(int key) {
    const bool p = pressed_[key & 0xFF];
    pressed_[key & 0xFF] = false;
    return p;
}

void Window::set_menu_mode(bool on) {
    if (on == menu_mode_) return;
    menu_mode_ = on;
    if (on) {
        capture_after_menu_ = captured_;
        capture(false);
    } else if (capture_after_menu_ && focused_) {
        capture(true);
    }
}

void Window::set_cursor_visible(bool on) {
    if (on == cursor_visible_) return;
    cursor_visible_ = on;
    // O cursor sobre a área de desenho troca já (WM_SETCURSOR só vem quando o mouse mexe).
    float x = 0.0f, y = 0.0f;
    if (menu_mode_ && cursor_position(&x, &y)) SetCursor(on ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
}

bool Window::cursor_position(float* x, float* y) const {
    POINT p;
    HWND hwnd = hwnd_of(*this);
    if (!focused_ || hwnd == nullptr || !GetCursorPos(&p) || !ScreenToClient(hwnd, &p)) return false;
    RECT r;
    GetClientRect(hwnd, &r);
    if (p.x < r.left || p.y < r.top || p.x >= r.right || p.y >= r.bottom) return false;
    *x = float(p.x) + 0.5f;
    *y = float(p.y) + 0.5f;
    return true;
}

void Window::capture(bool on) {
    if (on) {
        HWND hwnd = hwnd_of(*this);
        RECT r;
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
        ClipCursor(&r);
        if (!captured_) ShowCursor(FALSE);
    } else {
        ClipCursor(nullptr);
        if (captured_) ShowCursor(TRUE);
    }
    captured_ = on;
}

LRESULT CALLBACK WindowEvents::wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<Window*>(cs->lpCreateParams)->handle_ = hwnd;
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? handle(*self, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT WindowEvents::handle(Window& w, UINT msg, WPARAM wp, LPARAM lp) {
    HWND hwnd = hwnd_of(w);
    switch (msg) {
        case WM_SIZE: {
            const int cw = LOWORD(lp), ch = HIWORD(lp);
            if (cw > 0 && ch > 0 && (cw != w.width_ || ch != w.height_)) {
                w.width_ = cw;
                w.height_ = ch;
                w.resized_ = true;
            }
            if (w.captured_) w.capture(true);
            return 0;
        }
        case WM_ACTIVATE:
            w.focused_ = LOWORD(wp) != WA_INACTIVE;
            if (!w.focused_) w.capture(false);
            return 0;
        case WM_LBUTTONDOWN:
            if (!w.captured_ && !w.menu_mode_) w.capture(true);
            return 0;
        case WM_SETCURSOR:
            // No menu, o cursor do Windows só enquanto o mouse é usado (set_cursor_visible).
            if (w.menu_mode_ && LOWORD(lp) == HTCLIENT) {
                SetCursor(w.cursor_visible_ ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
                return TRUE;
            }
            break;
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) !=
                    UINT(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && w.captured_ && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                w.mouse_dx_ += float(raw.data.mouse.lLastX);
                w.mouse_dy_ += float(raw.data.mouse.lLastY);
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_MOUSEWHEEL:
            w.wheel_ += float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)) {
                w.toggle_fullscreen();
                return 0;
            }
            // Esc não solta mais o mouse aqui: é a pausa do jogo (o menu solta o mouse, set_menu_mode); o main o solta
            // na câmera livre e quando nenhum menu abre (release_mouse).
            if (!(lp & (1 << 30))) w.pressed_[wp & 0xFF] = true;  // ignora repetição automática
            if (msg == WM_SYSKEYDOWN) break;
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            w.capture(false);
            w.handle_ = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace nf
