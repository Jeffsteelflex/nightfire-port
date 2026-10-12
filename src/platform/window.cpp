#include "platform/window.h"

#include <algorithm>
#include <stdexcept>

namespace nf {

namespace {
constexpr wchar_t kClassName[] = L"NightfirePortWindow";
}

Window::Window(const std::wstring& title, int width, int height, bool fullscreen) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
    if (!hwnd_) throw std::runtime_error("CreateWindowEx falhou");

    RAWINPUTDEVICE mouse{0x01, 0x02, 0, hwnd_};  // página genérica, uso: mouse
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));

    ShowWindow(hwnd_, SW_SHOW);
    if (fullscreen) toggle_fullscreen();
    RECT cr;
    GetClientRect(hwnd_, &cr);
    width_ = cr.right - cr.left;
    height_ = cr.bottom - cr.top;
}

Window::~Window() {
    capture(false);
    if (hwnd_) DestroyWindow(hwnd_);
}

bool Window::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return hwnd_ != nullptr;
}

void Window::set_title(const std::wstring& title) { SetWindowTextW(hwnd_, title.c_str()); }

void Window::toggle_fullscreen() {
    const LONG style = GetWindowLongW(hwnd_, GWL_STYLE);
    if (!fullscreen_) {
        MONITORINFO mi{sizeof(mi)};
        GetWindowPlacement(hwnd_, &saved_);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &saved_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = !fullscreen_;
    if (captured_) capture(true);  // recalcula a área de ClipCursor
}

void Window::set_client_size(int width, int height) {
    if (fullscreen_ || width <= 0 || height <= 0) return;
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    const int w = r.right - r.left, h = r.bottom - r.top;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT& work = mi.rcWork;
    const int x = std::max<int>(work.left, work.left + (work.right - work.left - w) / 2);
    const int y = std::max<int>(work.top, work.top + (work.bottom - work.top - h) / 2);
    SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOOWNERZORDER);
    if (captured_) capture(true);
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

bool Window::key_down(int vk) const { return focused_ && (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool Window::key_pressed(int vk) {
    const bool p = pressed_[vk & 0xFF];
    pressed_[vk & 0xFF] = false;
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
    if (!focused_ || hwnd_ == nullptr || !GetCursorPos(&p) || !ScreenToClient(hwnd_, &p)) return false;
    RECT r;
    GetClientRect(hwnd_, &r);
    if (p.x < r.left || p.y < r.top || p.x >= r.right || p.y >= r.bottom) return false;
    *x = float(p.x) + 0.5f;
    *y = float(p.y) + 0.5f;
    return true;
}

void Window::capture(bool on) {
    if (on) {
        RECT r;
        GetClientRect(hwnd_, &r);
        MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
        ClipCursor(&r);
        if (!captured_) ShowCursor(FALSE);
    } else {
        ClipCursor(nullptr);
        if (captured_) ShowCursor(TRUE);
    }
    captured_ = on;
}

LRESULT CALLBACK Window::wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        reinterpret_cast<Window*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            const int w = LOWORD(lp), h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != width_ || h != height_)) {
                width_ = w;
                height_ = h;
                resized_ = true;
            }
            if (captured_) capture(true);
            return 0;
        }
        case WM_ACTIVATE:
            focused_ = LOWORD(wp) != WA_INACTIVE;
            if (!focused_) capture(false);
            return 0;
        case WM_LBUTTONDOWN:
            if (!captured_ && !menu_mode_) capture(true);
            return 0;
        case WM_SETCURSOR:
            // No menu, o cursor do Windows só enquanto o mouse é usado (set_cursor_visible).
            if (menu_mode_ && LOWORD(lp) == HTCLIENT) {
                SetCursor(cursor_visible_ ? LoadCursorW(nullptr, IDC_ARROW) : nullptr);
                return TRUE;
            }
            break;
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) !=
                    UINT(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE && captured_ && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                mouse_dx_ += float(raw.data.mouse.lLastX);
                mouse_dy_ += float(raw.data.mouse.lLastY);
            }
            return DefWindowProcW(hwnd_, msg, wp, lp);
        }
        case WM_MOUSEWHEEL:
            wheel_ += float(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (wp == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000)) {
                toggle_fullscreen();
                return 0;
            }
            // Esc não solta mais o mouse aqui: é a pausa do jogo (o menu solta o mouse, set_menu_mode); o main o solta
            // na câmera livre e quando nenhum menu abre (release_mouse).
            if (!(lp & (1 << 30))) pressed_[wp & 0xFF] = true;  // ignora repetição automática
            if (msg == WM_SYSKEYDOWN) break;
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            capture(false);
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace nf
