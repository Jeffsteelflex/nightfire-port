// A janela no macOS (AppKit): o backend de platform/window.h e de platform/display_modes.h (docs/port.md, "macOS: os
// backends"). Compilado com ARC (-fobjc-arc).
//
// As threads: o jogo chama a Window da thread dele (a da pilha da arena); o AppKit só aceita a janela na thread principal,
// que fica no laço de eventos do NSApplication (entry_macos.mm). O que muda a janela (criar, fechar, o tamanho, a tela
// cheia, o título, a captura e o cursor) vai à principal pelo run_on_main (platform/main_thread_macos.h); os eventos
// chegam na principal (a NSView e o delegate abaixo) e viram um estado compartilhado (Shared, com uma trava) que o
// pump() da thread do jogo copia para os campos da Window, como o pump do Windows processa as mensagens. Assim os campos
// da Window (o tamanho, o foco, a captura, a tela cheia) só mudam na thread do jogo.
//
// O que cada coisa faz, comparado com o Windows (window_win32.cpp):
//  - a área de desenho é uma NSView com uma CAMetalLayer (o renderizador Metal da etapa 3d desenha nela; nesta etapa o
//    renderizador nulo não desenha e a camada fica preta). native_handle() é a NSView; o tamanho da Window é o da camada em
//    pixels (os pontos da view vezes o backingScaleFactor: numa tela Retina 1600x900 pixels são 800x450 pontos, como uma
//    janela de 1600x900 num monitor 4K a 200% no Windows);
//  - as teclas: o keyCode do NSEvent (a posição física) pela tabela de platform/mac_keys.h; keyDown/keyUp/flagsChanged
//    (os modificadores pelos bits de cada lado). O macOS não manda o keyUp de uma tecla solta com o Command seguro: um
//    monitor local o reenvia à janela (como o GLFW). Option+Return alterna a tela cheia (o Alt+Enter do Windows);
//  - o mouse: o clique esquerdo captura (CGAssociateMouseAndMouseCursorPosition(false), o cursor parado e escondido) e o
//    olhar soma o deltaX/deltaY de cada evento de movimento. Atenção: esse delta é o do cursor, com a curva de aceleração
//    do sistema (o Raw Input do Windows não tem): quem joga pode desligá-la nos Ajustes do Sistema > Mouse ("Aceleração do
//    ponteiro", macOS 14) ou por defaults; um delta cru de verdade pediria o IOHIDManager (docs/port.md). Perder o foco
//    (Cmd+Tab, como o Alt+Tab) e abrir um menu soltam o mouse; os botões 1 a 5; a roda e o trackpad em entalhes
//    (nf::mac::MacWheel, sem a inércia do trackpad);
//  - no modo de menu o cursor do sistema aparece só enquanto o mouse é usado (setHiddenUntilMouseMoves);
//  - a tela cheia é a sem bordas do Windows: a janela sem moldura do tamanho do monitor, com a barra de menus e o Dock
//    escondidos (presentationOptions), na hora (sem a animação nem o Espaço próprio da tela cheia nativa). O botão verde,
//    Ctrl+Cmd+F e o menu View chamam a mesma coisa (toggleFullScreen: da janela). A exclusiva do DXGI não existe no macOS:
//    o modo Exclusive das opções é esta mesma (a janela abre sem bordas e o renderizador aceita a exclusiva sem trocar nada);
//  - fechar a janela (o botão vermelho, Cmd+Q, o Dock) faz o pump() devolver false, como o WM_CLOSE; a janela some no
//    destrutor.

#include "platform/display_modes.h"
#include "platform/keys.h"
#include "platform/mac_keys.h"
#include "platform/main_thread_macos.h"
#include "platform/os.h"
#include "platform/window.h"

// Sem os check(), verify() e require() antigos do AssertMacros.h (o Carbon o inclui).
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// A tabela de platform/mac_keys.h tem os números do HIToolbox: conferidos aqui com as constantes do Carbon.
namespace mk = nf::mac::kvk;
static_assert(mk::A == kVK_ANSI_A && mk::S == kVK_ANSI_S && mk::D == kVK_ANSI_D && mk::F == kVK_ANSI_F &&
              mk::H == kVK_ANSI_H && mk::G == kVK_ANSI_G && mk::Z == kVK_ANSI_Z && mk::X == kVK_ANSI_X &&
              mk::C == kVK_ANSI_C && mk::V == kVK_ANSI_V && mk::B == kVK_ANSI_B && mk::Q == kVK_ANSI_Q &&
              mk::W == kVK_ANSI_W && mk::E == kVK_ANSI_E && mk::R == kVK_ANSI_R && mk::Y == kVK_ANSI_Y &&
              mk::T == kVK_ANSI_T && mk::O == kVK_ANSI_O && mk::U == kVK_ANSI_U && mk::I == kVK_ANSI_I &&
              mk::P == kVK_ANSI_P && mk::L == kVK_ANSI_L && mk::J == kVK_ANSI_J && mk::K == kVK_ANSI_K &&
              mk::N == kVK_ANSI_N && mk::M == kVK_ANSI_M);
static_assert(mk::N0 == kVK_ANSI_0 && mk::N1 == kVK_ANSI_1 && mk::N2 == kVK_ANSI_2 && mk::N3 == kVK_ANSI_3 &&
              mk::N4 == kVK_ANSI_4 && mk::N5 == kVK_ANSI_5 && mk::N6 == kVK_ANSI_6 && mk::N7 == kVK_ANSI_7 &&
              mk::N8 == kVK_ANSI_8 && mk::N9 == kVK_ANSI_9);
static_assert(mk::Equal == kVK_ANSI_Equal && mk::Minus == kVK_ANSI_Minus && mk::RightBracket == kVK_ANSI_RightBracket &&
              mk::LeftBracket == kVK_ANSI_LeftBracket && mk::Quote == kVK_ANSI_Quote &&
              mk::Semicolon == kVK_ANSI_Semicolon && mk::Backslash == kVK_ANSI_Backslash && mk::Comma == kVK_ANSI_Comma &&
              mk::Slash == kVK_ANSI_Slash && mk::Period == kVK_ANSI_Period && mk::Grave == kVK_ANSI_Grave &&
              mk::IsoSection == kVK_ISO_Section);
static_assert(mk::Return == kVK_Return && mk::Tab == kVK_Tab && mk::Space == kVK_Space && mk::Delete == kVK_Delete &&
              mk::Escape == kVK_Escape && mk::Command == kVK_Command && mk::Shift == kVK_Shift &&
              mk::CapsLock == kVK_CapsLock && mk::Option == kVK_Option && mk::Control == kVK_Control &&
              mk::RightShift == kVK_RightShift && mk::RightOption == kVK_RightOption &&
              mk::RightControl == kVK_RightControl && mk::Function == kVK_Function);
static_assert(mk::F1 == kVK_F1 && mk::F2 == kVK_F2 && mk::F3 == kVK_F3 && mk::F4 == kVK_F4 && mk::F5 == kVK_F5 &&
              mk::F6 == kVK_F6 && mk::F7 == kVK_F7 && mk::F8 == kVK_F8 && mk::F9 == kVK_F9 && mk::F10 == kVK_F10 &&
              mk::F11 == kVK_F11 && mk::F12 == kVK_F12 && mk::F13 == kVK_F13 && mk::F14 == kVK_F14 &&
              mk::F15 == kVK_F15 && mk::F16 == kVK_F16 && mk::F17 == kVK_F17 && mk::F18 == kVK_F18 &&
              mk::F19 == kVK_F19 && mk::F20 == kVK_F20);
static_assert(mk::Help == kVK_Help && mk::Home == kVK_Home && mk::PageUp == kVK_PageUp &&
              mk::ForwardDelete == kVK_ForwardDelete && mk::End == kVK_End && mk::PageDown == kVK_PageDown &&
              mk::LeftArrow == kVK_LeftArrow && mk::RightArrow == kVK_RightArrow && mk::DownArrow == kVK_DownArrow &&
              mk::UpArrow == kVK_UpArrow);
static_assert(mk::KeypadDecimal == kVK_ANSI_KeypadDecimal && mk::KeypadMultiply == kVK_ANSI_KeypadMultiply &&
              mk::KeypadPlus == kVK_ANSI_KeypadPlus && mk::KeypadClear == kVK_ANSI_KeypadClear &&
              mk::KeypadDivide == kVK_ANSI_KeypadDivide && mk::KeypadEnter == kVK_ANSI_KeypadEnter &&
              mk::KeypadMinus == kVK_ANSI_KeypadMinus && mk::Keypad0 == kVK_ANSI_Keypad0 &&
              mk::Keypad1 == kVK_ANSI_Keypad1 && mk::Keypad2 == kVK_ANSI_Keypad2 && mk::Keypad3 == kVK_ANSI_Keypad3 &&
              mk::Keypad4 == kVK_ANSI_Keypad4 && mk::Keypad5 == kVK_ANSI_Keypad5 && mk::Keypad6 == kVK_ANSI_Keypad6 &&
              mk::Keypad7 == kVK_ANSI_Keypad7 && mk::Keypad8 == kVK_ANSI_Keypad8 && mk::Keypad9 == kVK_ANSI_Keypad9);

// O estado de uma janela que os eventos escrevem na thread principal e o pump() lê na do jogo (Window::Native::shared).
namespace nf {
struct WindowShared {
    std::mutex lock;
    bool closed = false;
    int width = 0, height = 0;  // a área de desenho em pixels
    bool focused = false;
    bool captured = false;
    bool fullscreen = false;
    float mdx = 0.0f, mdy = 0.0f;  // o olhar desde o último pump
    double wheel = 0.0;           // entalhes desde o último pump
    bool pressed[256] = {};       // bordas de subida das teclas (sem a repetição automática)
    bool down[256] = {};          // seguras agora (teclas e botões do mouse)
    bool cursor_inside = false;
    float cursor_x = 0.0f, cursor_y = 0.0f;
};
}  // namespace nf

@class NFWindow;
@class NFView;
@class NFWindowDelegate;

struct nf::Window::Native {
    WindowShared shared;
    // ---- só na thread principal
    NFWindow* window = nil;
    NFView* view = nil;
    NFWindowDelegate* delegate = nil;
    id key_up_monitor = nil;
    id layout_observer = nil;
    bool captured = false;        // a captura de verdade (o cursor preso e escondido)
    bool menu_mode = false;
    bool fullscreen = false;
    NSRect saved_frame = NSZeroRect;  // a janela antes da tela cheia
    NSWindowStyleMask saved_style = 0;
    nf::mac::MacWheel wheel;
    // ---- só na thread do jogo (a cópia do pump)
    bool down[256] = {};
    bool cursor_inside = false;
    float cursor_x = 0.0f, cursor_y = 0.0f;
};

// As classes do AppKit (as implementações vêm depois das funções que elas chamam): a janela, a área de desenho e o
// delegate, cada uma com o estado da Window dela.

@interface NFWindow : NSWindow {
@public
    nf::Window::Native* native;
}
@end

@interface NFView : NSView {
@public
    nf::Window::Native* native;
}
@end

@interface NFWindowDelegate : NSObject <NSWindowDelegate> {
@public
    nf::Window::Native* native;
}
@end

namespace {

using Native = nf::Window::Native;

// As janelas abertas (na thread principal): Cmd+Q e o Quit do Dock fecham todas (request_quit).
std::vector<Native*>& live_windows() {
    static std::vector<Native*> list;
    return list;
}

// Os bits de cada lado dos modificadores em modifierFlags (NX_DEVICE*KEYMASK do IOKit/IOLLEvent.h).
constexpr NSUInteger kDeviceLCtl = 0x00000001, kDeviceLShift = 0x00000002, kDeviceRShift = 0x00000004;
constexpr NSUInteger kDeviceLCmd = 0x00000008, kDeviceRCmd = 0x00000010, kDeviceLAlt = 0x00000020;
constexpr NSUInteger kDeviceRAlt = 0x00000040, kDeviceRCtl = 0x00002000;

NSString* ns_string(const std::string& utf8) {
    NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
    return s != nil ? s : @"";
}

void update_size(Native& n) {
    if (n.view == nil) return;
    const NSRect b = n.view.bounds;
    const CGFloat scale = n.window != nil ? n.window.backingScaleFactor : 1.0;
    const int w = int(std::lround(b.size.width * scale));
    const int h = int(std::lround(b.size.height * scale));
    CALayer* layer = n.view.layer;
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metal = (CAMetalLayer*)layer;
        metal.contentsScale = scale;
        if (w > 0 && h > 0) metal.drawableSize = CGSizeMake(CGFloat(w), CGFloat(h));
    }
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.width = w;
    n.shared.height = h;
}

void capture_main(Native& n, bool on) {
    if (on == n.captured) return;
    if (on) {
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
    } else {
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
    n.captured = on;
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.captured = on;
}

void set_focus_main(Native& n, bool on) {
    if (!on) {
        // Perder o foco (Cmd+Tab, outra janela): o mouse solto, como no Windows, e nenhuma tecla presa (o macOS não manda
        // o keyUp das teclas soltas fora da janela).
        capture_main(n, false);
        n.wheel.reset();
    }
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.focused = on;
    if (!on) {
        for (bool& d : n.shared.down) d = false;
        n.shared.cursor_inside = false;
    }
}

void set_fullscreen_main(Native& n, bool on) {
    if (on == n.fullscreen || n.window == nil) return;
    NSWindow* w = (NSWindow*)n.window;
    if (on) {
        n.saved_frame = w.frame;
        n.saved_style = w.styleMask;
        NSScreen* screen = w.screen != nil ? w.screen : NSScreen.mainScreen;
        @try {
            NSApp.presentationOptions = NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
        } @catch (NSException* exception) {
            (void)exception;
        }
        w.styleMask = NSWindowStyleMaskBorderless;
        if (screen != nil) [w setFrame:screen.frame display:YES];
    } else {
        @try {
            NSApp.presentationOptions = NSApplicationPresentationDefault;
        } @catch (NSException* exception) {
            (void)exception;
        }
        w.styleMask = n.saved_style;
        [w setFrame:n.saved_frame display:YES];
    }
    n.fullscreen = on;
    [w makeKeyAndOrderFront:nil];
    [w makeFirstResponder:(NSView*)n.view];
    {
        std::lock_guard<std::mutex> g(n.shared.lock);
        n.shared.fullscreen = on;
    }
    update_size(n);
}

void set_client_size_main(Native& n, int width, int height) {
    if (n.fullscreen || n.window == nil || width <= 0 || height <= 0) return;
    NSWindow* w = (NSWindow*)n.window;
    const CGFloat scale = w.backingScaleFactor;
    NSRect frame = [w frameRectForContentRect:NSMakeRect(0, 0, CGFloat(width) / scale, CGFloat(height) / scale)];
    NSScreen* screen = w.screen != nil ? w.screen : NSScreen.mainScreen;
    if (screen != nil) {
        // Centrada na área útil (sem a barra de menus e o Dock); maior que ela, a barra de título fica visível.
        const NSRect work = screen.visibleFrame;
        frame.origin.x = work.origin.x + std::max<CGFloat>(0.0, (work.size.width - frame.size.width) / 2.0);
        frame.origin.y = frame.size.height > work.size.height
                             ? NSMaxY(work) - frame.size.height
                             : work.origin.y + (work.size.height - frame.size.height) / 2.0;
    }
    [w setFrame:frame display:YES];
    update_size(n);
}

// Uma tecla (keyDown/keyUp): o código do port pela tabela.
void key_event(Native& n, NSEvent* e, bool down) {
    const uint16_t code = e.keyCode;
    // Option+Return: a tela cheia (o Alt+Enter do Windows), sem chegar ao jogo.
    if (down && (code == nf::mac::kvk::Return || code == nf::mac::kvk::KeypadEnter) &&
        (e.modifierFlags & NSEventModifierFlagOption) != 0) {
        set_fullscreen_main(n, !n.fullscreen);
        return;
    }
    const nf::mac::MacKey* k = nf::mac::mac_key(code);
    if (k == nullptr) return;
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.down[k->vk] = down;
    if (down && !e.isARepeat) n.shared.pressed[k->vk] = true;
}

// Um modificador (flagsChanged): seguro ou solto pelos bits do lado dele.
void flags_event(Native& n, NSEvent* e) {
    namespace kvk = nf::mac::kvk;
    const uint16_t code = e.keyCode;
    const nf::mac::MacKey* k = nf::mac::mac_key(code);
    if (k == nullptr) return;
    if (code == kvk::CapsLock) {
        // A trava: o macOS só diz que ela mudou (um toque), não quando a tecla é solta.
        std::lock_guard<std::mutex> g(n.shared.lock);
        n.shared.pressed[k->vk] = true;
        return;
    }
    NSUInteger side = 0, other = 0;
    NSEventModifierFlags generic = 0;
    switch (code) {
        case kvk::Shift: side = kDeviceLShift; other = kDeviceRShift; generic = NSEventModifierFlagShift; break;
        case kvk::RightShift: side = kDeviceRShift; other = kDeviceLShift; generic = NSEventModifierFlagShift; break;
        case kvk::Control: side = kDeviceLCtl; other = kDeviceRCtl; generic = NSEventModifierFlagControl; break;
        case kvk::RightControl: side = kDeviceRCtl; other = kDeviceLCtl; generic = NSEventModifierFlagControl; break;
        case kvk::Option: side = kDeviceLAlt; other = kDeviceRAlt; generic = NSEventModifierFlagOption; break;
        case kvk::RightOption: side = kDeviceRAlt; other = kDeviceLAlt; generic = NSEventModifierFlagOption; break;
        case kvk::Command: side = kDeviceLCmd; other = kDeviceRCmd; generic = NSEventModifierFlagCommand; break;
        case kvk::RightCommand: side = kDeviceRCmd; other = kDeviceLCmd; generic = NSEventModifierFlagCommand; break;
        default: return;
    }
    const NSUInteger raw = NSUInteger(e.modifierFlags);
    bool down = (raw & side) != 0;
    // Sem os bits de cada lado (um teclado remoto, um programa que injeta eventos): o bit genérico vale para este lado.
    if ((raw & (side | other)) == 0 && (raw & NSUInteger(generic)) != 0) down = true;
    std::lock_guard<std::mutex> g(n.shared.lock);
    const bool was = n.shared.down[k->vk];
    n.shared.down[k->vk] = down;
    if (down && !was) n.shared.pressed[k->vk] = true;
    if (k->generic != 0) {
        const bool any = (raw & NSUInteger(generic)) != 0;
        if (any && !n.shared.down[k->generic]) n.shared.pressed[k->generic] = true;
        n.shared.down[k->generic] = any;
    }
}

// Onde o cursor está na área de desenho (pixels, a origem em cima à esquerda, como no Windows).
void cursor_from_event(Native& n, NSEvent* e) {
    if (n.view == nil || e.window != (NSWindow*)n.window) return;
    const NSPoint p = [(NSView*)n.view convertPoint:e.locationInWindow fromView:nil];
    const NSRect b = n.view.bounds;
    const CGFloat scale = n.window != nil ? n.window.backingScaleFactor : 1.0;
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.cursor_inside = p.x >= 0 && p.y >= 0 && p.x < b.size.width && p.y < b.size.height;
    n.shared.cursor_x = float(p.x * scale);
    n.shared.cursor_y = float((b.size.height - p.y) * scale);
}

void motion_event(Native& n, NSEvent* e) {
    if (n.captured) {
        std::lock_guard<std::mutex> g(n.shared.lock);
        n.shared.mdx += float(e.deltaX);
        n.shared.mdy += float(e.deltaY);
    }
    cursor_from_event(n, e);
}

void button_event(Native& n, NSEvent* e, int vk, bool down) {
    // O clique esquerdo com o mouse solto e sem menu captura (o WM_LBUTTONDOWN do Windows).
    if (vk == nf::key::LButton && down && !n.captured && !n.menu_mode) capture_main(n, true);
    cursor_from_event(n, e);
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.down[vk & 0xFF] = down;
}

int other_button_vk(NSEvent* e) {
    switch (e.buttonNumber) {
        case 2: return nf::key::MButton;
        case 3: return nf::key::XButton1;
        case 4: return nf::key::XButton2;
        default: return 0;
    }
}

void scroll_event(Native& n, NSEvent* e) {
    // A inércia do trackpad depois que os dedos saem não conta (seria uma troca de arma a mais).
    if (e.momentumPhase != NSEventPhaseNone) return;
    double dy = e.scrollingDeltaY;
    if (e.isDirectionInvertedFromDevice) dy = -dy;  // a "rolagem natural" desfeita: positivo = para cima
    const double notches = n.wheel.add(dy, e.hasPreciseScrollingDeltas);
    if (notches == 0.0) return;
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.wheel += notches;
}

void request_close(Native& n) {
    std::lock_guard<std::mutex> g(n.shared.lock);
    n.shared.closed = true;
}

}  // namespace

// ---------------------------------------------------------------- as classes do AppKit

@implementation NFWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
- (BOOL)canBecomeMainWindow {
    return YES;
}
// O botão verde, Ctrl+Cmd+F e o menu View: a tela cheia sem bordas do port (não a nativa, com o Espaço próprio).
- (void)toggleFullScreen:(id)sender {
    (void)sender;
    if (native != nullptr) set_fullscreen_main(*native, !native->fullscreen);
}
@end

@implementation NFView
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}
- (BOOL)wantsUpdateLayer {
    return YES;
}
- (void)updateLayer {
}
- (CALayer*)makeBackingLayer {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.opaque = YES;
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    return layer;
}
- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (native != nullptr) update_size(*native);
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    if (native != nullptr) update_size(*native);
}
- (void)keyDown:(NSEvent*)event {
    if (native != nullptr) key_event(*native, event, true);
}
- (void)keyUp:(NSEvent*)event {
    if (native != nullptr) key_event(*native, event, false);
}
- (void)flagsChanged:(NSEvent*)event {
    if (native != nullptr) flags_event(*native, event);
}
- (void)mouseDown:(NSEvent*)event {
    if (native != nullptr) button_event(*native, event, nf::key::LButton, true);
}
- (void)mouseUp:(NSEvent*)event {
    if (native != nullptr) button_event(*native, event, nf::key::LButton, false);
}
- (void)rightMouseDown:(NSEvent*)event {
    if (native != nullptr) button_event(*native, event, nf::key::RButton, true);
}
- (void)rightMouseUp:(NSEvent*)event {
    if (native != nullptr) button_event(*native, event, nf::key::RButton, false);
}
- (void)otherMouseDown:(NSEvent*)event {
    const int vk = other_button_vk(event);
    if (native != nullptr && vk != 0) button_event(*native, event, vk, true);
}
- (void)otherMouseUp:(NSEvent*)event {
    const int vk = other_button_vk(event);
    if (native != nullptr && vk != 0) button_event(*native, event, vk, false);
}
- (void)mouseMoved:(NSEvent*)event {
    if (native != nullptr) motion_event(*native, event);
}
- (void)mouseDragged:(NSEvent*)event {
    if (native != nullptr) motion_event(*native, event);
}
- (void)rightMouseDragged:(NSEvent*)event {
    if (native != nullptr) motion_event(*native, event);
}
- (void)otherMouseDragged:(NSEvent*)event {
    if (native != nullptr) motion_event(*native, event);
}
- (void)mouseExited:(NSEvent*)event {
    (void)event;
    if (native == nullptr) return;
    std::lock_guard<std::mutex> g(native->shared.lock);
    native->shared.cursor_inside = false;
}
- (void)scrollWheel:(NSEvent*)event {
    if (native != nullptr) scroll_event(*native, event);
}
@end

@implementation NFWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    // O botão vermelho: o jogo sai do laço (pump() devolve false) e a janela some no destrutor da Window.
    if (native != nullptr) request_close(*native);
    return NO;
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    if (native != nullptr) set_focus_main(*native, true);
}
- (void)windowDidResignKey:(NSNotification*)notification {
    (void)notification;
    if (native != nullptr) set_focus_main(*native, false);
}
- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    if (native != nullptr) update_size(*native);
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    (void)notification;
    if (native != nullptr) update_size(*native);
}
- (void)windowDidChangeScreen:(NSNotification*)notification {
    (void)notification;
    if (native != nullptr) update_size(*native);
}
@end

// ---------------------------------------------------------------- a criação e o fim (na thread principal)

namespace {

void create_main(Native& n, const std::string& title, int width, int height, bool fullscreen) {
    // Um executável fora de um pacote .app começa sem ícone no Dock nem janelas com o foco: um aplicativo comum.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSScreen* screen = NSScreen.mainScreen;
    const CGFloat scale = screen != nil ? screen.backingScaleFactor : 1.0;
    const NSRect rect = NSMakeRect(0, 0, CGFloat(width) / scale, CGFloat(height) / scale);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                    NSWindowStyleMaskResizable;
    NFWindow* win = [[NFWindow alloc] initWithContentRect:rect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    win->native = &n;
    win.releasedWhenClosed = NO;
    win.title = ns_string(title);
    win.backgroundColor = NSColor.blackColor;
    win.acceptsMouseMovedEvents = YES;
    win.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    NFView* view = [[NFView alloc] initWithFrame:rect];
    view->native = &n;
    view.wantsLayer = YES;
    win.contentView = view;
    NFWindowDelegate* delegate = [[NFWindowDelegate alloc] init];
    delegate->native = &n;
    win.delegate = delegate;
    n.window = win;
    n.view = view;
    n.delegate = delegate;

    // O keyUp de uma tecla solta com o Command seguro não chega à janela: reenviado a ela (como o GLFW).
    Native* np = &n;
    n.key_up_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
                                                             handler:^NSEvent*(NSEvent* event) {
                                                               if ((event.modifierFlags & NSEventModifierFlagCommand) != 0 &&
                                                                   np->window != nil &&
                                                                   event.window == (NSWindow*)np->window) {
                                                                   key_event(*np, event, false);
                                                               }
                                                               return event;
                                                             }];
    // O layout do teclado mudou (a bandeira na barra de menus): os caracteres das teclas de novo (os::key_char).
    n.layout_observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSTextInputContextKeyboardSelectionDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                  (void)note;
                  nf::macos::refresh_key_chars();
                }];
    nf::macos::refresh_key_chars();

    [win center];
    [win makeKeyAndOrderFront:nil];
    [win makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];
    live_windows().push_back(&n);
    update_size(n);
    {
        std::lock_guard<std::mutex> g(n.shared.lock);
        n.shared.focused = win.isKeyWindow;
    }
    if (fullscreen) set_fullscreen_main(n, true);
}

void destroy_main(Native& n) {
    capture_main(n, false);
    if (n.fullscreen) {
        @try {
            NSApp.presentationOptions = NSApplicationPresentationDefault;
        } @catch (NSException* exception) {
            (void)exception;
        }
    }
    [NSCursor setHiddenUntilMouseMoves:NO];
    if (n.key_up_monitor != nil) [NSEvent removeMonitor:n.key_up_monitor];
    n.key_up_monitor = nil;
    if (n.layout_observer != nil) [[NSNotificationCenter defaultCenter] removeObserver:n.layout_observer];
    n.layout_observer = nil;
    auto& list = live_windows();
    list.erase(std::remove(list.begin(), list.end(), &n), list.end());
    if (n.window != nil) {
        NFWindow* win = n.window;
        win.delegate = nil;
        win->native = nullptr;
        if (n.view != nil) n.view->native = nullptr;
        [win orderOut:nil];
        [win close];
    }
    n.window = nil;
    n.view = nil;
    n.delegate = nil;
}

}  // namespace

namespace nf {

namespace macos {

// Cmd+Q, o Quit do Dock, o fim da sessão: cada janela aberta recebe o "fechar" (o pump() dela devolve false). Sem
// janela nenhuma, o NSApplication termina na hora (entry_macos.mm).
bool request_quit() {
    bool any = false;
    for (Native* n : live_windows()) {
        request_close(*n);
        any = true;
    }
    return any;
}

}  // namespace macos

// ---------------------------------------------------------------- a Window (na thread do jogo)

Window::Window(const std::wstring& title, int width, int height, bool fullscreen) : native_(std::make_unique<Native>()) {
    if (!macos::can_use_appkit()) {
        throw std::runtime_error("a janela do macOS precisa da sessão gráfica (o WindowServer) e do laço de eventos do "
                                 "NSApplication (entry_macos.mm)");
    }
    Native* n = native_.get();
    const std::string t = os::to_utf8(title);
    macos::run_on_main([n, &t, width, height, fullscreen] { create_main(*n, t, width, height, fullscreen); });
    handle_ = (__bridge void*)n->view;
    std::lock_guard<std::mutex> g(n->shared.lock);
    width_ = n->shared.width;
    height_ = n->shared.height;
    focused_ = n->shared.focused;
    fullscreen_ = n->shared.fullscreen;
}

Window::~Window() {
    Native* n = native_.get();
    macos::run_on_main([n] { destroy_main(*n); });
    handle_ = nullptr;
}

bool Window::pump() {
    Native& n = *native_;
    if (handle_ == nullptr) return false;
    bool closed = false;
    {
        std::lock_guard<std::mutex> g(n.shared.lock);
        WindowShared& s = n.shared;
        closed = s.closed;
        if (s.width > 0 && s.height > 0 && (s.width != width_ || s.height != height_)) {
            width_ = s.width;
            height_ = s.height;
            resized_ = true;
        }
        focused_ = s.focused;
        captured_ = s.captured;
        fullscreen_ = s.fullscreen;
        mouse_dx_ += s.mdx;
        mouse_dy_ += s.mdy;
        s.mdx = s.mdy = 0.0f;
        wheel_ += float(s.wheel);
        s.wheel = 0.0;
        for (int i = 0; i < 256; ++i) {
            if (s.pressed[i]) pressed_[i] = true;
            s.pressed[i] = false;
            n.down[i] = s.down[i];
        }
        n.cursor_inside = s.cursor_inside;
        n.cursor_x = s.cursor_x;
        n.cursor_y = s.cursor_y;
    }
    if (closed) {
        handle_ = nullptr;
        return false;
    }
    return true;
}

namespace {
// Depois de uma mudança feita na thread principal (o tamanho, a tela cheia), os campos da Window na hora, como o
// WM_SIZE síncrono do SetWindowPos no Windows.
void sync_after(Native& n, int* width, int* height, bool* resized, bool* fullscreen) {
    std::lock_guard<std::mutex> g(n.shared.lock);
    if (n.shared.width > 0 && n.shared.height > 0 && (n.shared.width != *width || n.shared.height != *height)) {
        *width = n.shared.width;
        *height = n.shared.height;
        *resized = true;
    }
    *fullscreen = n.shared.fullscreen;
}
}  // namespace

void Window::set_title(const std::wstring& title) {
    Native* n = native_.get();
    const std::string t = os::to_utf8(title);
    macos::post_to_main([n, t] {
        if (n->window != nil) n->window.title = ns_string(t);
    });
}

void Window::toggle_fullscreen() {
    Native* n = native_.get();
    macos::run_on_main([n] { set_fullscreen_main(*n, !n->fullscreen); });
    sync_after(*n, &width_, &height_, &resized_, &fullscreen_);
}

void Window::set_client_size(int width, int height) {
    if (fullscreen_ || width <= 0 || height <= 0) return;
    Native* n = native_.get();
    macos::run_on_main([n, width, height] { set_client_size_main(*n, width, height); });
    sync_after(*n, &width_, &height_, &resized_, &fullscreen_);
}

void Window::set_topmost() {
    Native* n = native_.get();
    macos::run_on_main([n] {
        if (n->window != nil) n->window.level = NSFloatingWindowLevel;
    });
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

bool Window::key_down(int key) const { return focused_ && native_->down[key & 0xFF]; }

bool Window::key_pressed(int key) {
    const bool p = pressed_[key & 0xFF];
    pressed_[key & 0xFF] = false;
    return p;
}

void Window::set_menu_mode(bool on) {
    if (on == menu_mode_) return;
    menu_mode_ = on;
    Native* n = native_.get();
    macos::run_on_main([n, on] {
        n->menu_mode = on;
        if (!on) [NSCursor setHiddenUntilMouseMoves:NO];
    });
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
    Native* n = native_.get();
    // No modo de menu, escondido até o mouse mexer (o jogo o mostra de novo quando vê o movimento).
    macos::post_to_main([n, on] {
        if (n->menu_mode && !n->captured) [NSCursor setHiddenUntilMouseMoves:on ? NO : YES];
    });
}

bool Window::cursor_position(float* x, float* y) const {
    if (!focused_ || handle_ == nullptr || !native_->cursor_inside) return false;
    *x = native_->cursor_x;
    *y = native_->cursor_y;
    return true;
}

void Window::capture(bool on) {
    Native* n = native_.get();
    macos::run_on_main([n, on] { capture_main(*n, on); });
    captured_ = on;
}

// ---------------------------------------------------------------- os modos do monitor (platform/display_modes.h)

bool system_display_modes(void* native_window, std::vector<DisplayMode>* modes, DisplayMode* desktop) {
    CGDirectDisplayID display = CGMainDisplayID();
    if (native_window != nullptr && macos::can_use_appkit()) {
        NSView* view = (__bridge NSView*)native_window;
        macos::run_on_main([&] {
            NSScreen* screen = view.window.screen != nil ? view.window.screen : NSScreen.mainScreen;
            NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
            if (number != nil) display = CGDirectDisplayID(number.unsignedIntValue);
        });
    }
    CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
    if (current == nullptr) return false;
    desktop->width = int(CGDisplayModeGetPixelWidth(current));
    desktop->height = int(CGDisplayModeGetPixelHeight(current));
    CGDisplayModeRelease(current);
    // Com os modos HiDPI (os "parece 1440x900" de uma tela Retina, em pixels 2880x1800).
    const void* keys[] = {kCGDisplayShowDuplicateLowResolutionModes};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef options =
        CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef all = CGDisplayCopyAllDisplayModes(display, options);
    if (options != nullptr) CFRelease(options);
    std::set<std::pair<int, int>> sizes;
    if (all != nullptr) {
        const CFIndex count = CFArrayGetCount(all);
        for (CFIndex i = 0; i < count; ++i) {
            CGDisplayModeRef m = (CGDisplayModeRef)CFArrayGetValueAtIndex(all, i);
            if (!CGDisplayModeIsUsableForDesktopGUI(m)) continue;
            sizes.insert({int(CGDisplayModeGetPixelWidth(m)), int(CGDisplayModeGetPixelHeight(m))});
        }
        CFRelease(all);
    }
    modes->clear();
    for (const auto& s : sizes) modes->push_back(DisplayMode{s.first, s.second});
    return true;
}

}  // namespace nf
