// O que platform/os.h só dá com o AppKit no macOS (o programa de janela: NF_MACOS_APP, a build sem NF_HEADLESS; o resto do
// sistema operacional é o os_posix.cpp, comum com o Linux): o caractere de uma tecla no layout do usuário
// (UCKeyTranslate), o estado do teclado do sistema (o Shift na partida) e a caixa de erro (NSAlert). E a ponte com a
// thread principal dos backends do macOS (platform/main_thread_macos.h). Compilado com ARC (-fobjc-arc).

#include "platform/mac_keys.h"
#include "platform/main_thread_macos.h"
#include "platform/os.h"

// Sem os check(), verify() e require() antigos do AssertMacros.h (o Carbon o inclui).
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <pthread.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <exception>
#include <string>

namespace nf {

namespace macos {

namespace {

std::atomic<bool> g_main_loop{false};

struct SyncCall {
    const std::function<void()>* fn;
    std::exception_ptr error;
};

void sync_trampoline(void* p) {
    auto* call = static_cast<SyncCall*>(p);
    try {
        (*call->fn)();
    } catch (...) {
        call->error = std::current_exception();  // levada de volta à thread que pediu (uma exceção não atravessa o GCD)
    }
}

void async_trampoline(void* p) {
    auto* fn = static_cast<std::function<void()>*>(p);
    try {
        (*fn)();
    } catch (...) {
    }
    delete fn;
}

// O caractere de cada código do port no layout em uso (refresh_key_chars, na thread principal).
std::array<std::atomic<uint32_t>, 256> g_key_chars{};

// O caractere de uma tecla como o MapVirtualKeyW(MAPVK_VK_TO_CHAR) do Windows dá: o da tecla sem modificadores, as
// letras maiúsculas, os de controle do Windows (Backspace, Tab, Enter, Esc) e nada para as setas e as de função (o
// UCKeyTranslate dá os da área privada 0xF700 do Unicode ou os de controle 0x1C-0x1F).
uint32_t windows_like_char(uint32_t ch) {
    if (ch >= 'a' && ch <= 'z') return ch - 32;
    if (ch == 0x08 || ch == 0x09 || ch == 0x0D || ch == 0x1B) return ch;
    if (ch < 0x20 || ch == 0x7F) return 0;
    if (ch >= 0xF700 && ch <= 0xF8FF) return 0;
    return ch;
}

}  // namespace

bool main_loop_running() { return g_main_loop.load(); }
void set_main_loop_running(bool on) { g_main_loop.store(on); }
bool on_main_thread() { return pthread_main_np() != 0; }
bool can_use_appkit() { return g_main_loop.load(); }

void run_on_main(const std::function<void()>& fn) {
    if (on_main_thread() || !g_main_loop.load()) {
        fn();
        return;
    }
    SyncCall call{&fn, nullptr};
    dispatch_sync_f(dispatch_get_main_queue(), &call, &sync_trampoline);
    if (call.error) std::rethrow_exception(call.error);
}

void post_to_main(std::function<void()> fn) {
    if (!g_main_loop.load()) {
        fn();
        return;
    }
    dispatch_async_f(dispatch_get_main_queue(), new std::function<void()>(std::move(fn)), &async_trampoline);
}

bool has_window_server() {
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (session == nullptr) return false;
    CFRelease(session);
    return true;
}

void refresh_key_chars() {
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef data = nullptr;
    if (source != nullptr) {
        data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    }
    if (data == nullptr) {
        // Um método de entrada sem layout próprio (japonês, chinês): o layout ASCII que ele usa por baixo.
        if (source != nullptr) CFRelease(source);
        source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
        if (source != nullptr) {
            data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
        }
    }
    if (data == nullptr) {
        if (source != nullptr) CFRelease(source);
        return;
    }
    const auto* layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data));
    for (int vk = 0; vk < 256; ++vk) {
        uint32_t ch = 0;
        const uint16_t mac = nf::mac::mac_of_vk(uint8_t(vk));
        if (mac != 0xFFFF && vk > 0x07) {
            UInt32 dead = 0;
            UniChar chars[4] = {};
            UniCharCount len = 0;
            const OSStatus st = UCKeyTranslate(layout, mac, kUCKeyActionDisplay, 0, LMGetKbdType(),
                                               kUCKeyTranslateNoDeadKeysMask, &dead, 4, &len, chars);
            if (st == noErr && len > 0) ch = windows_like_char(chars[0]);
        }
        g_key_chars[size_t(vk)].store(ch);
    }
    CFRelease(source);
}

}  // namespace macos

namespace os {

namespace {

NSString* ns_string(const std::string& utf8) {
    NSString* s = [NSString stringWithUTF8String:utf8.c_str()];
    // Uma mensagem de exceção que não é UTF-8 (a página de código do sistema do Windows): os bytes como Latin-1.
    if (s == nil) s = [NSString stringWithCString:utf8.c_str() encoding:NSISOLatin1StringEncoding];
    return s != nil ? s : @"";
}

void alert(const std::string& title, const std::string& text) {
    std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
    std::fflush(stderr);
    // Só com o laço de eventos (o programa de janela): o nf_tests e as ferramentas de console ficam no stderr.
    if (!macos::can_use_appkit()) return;
    macos::run_on_main([&] {
        @autoreleasepool {
            NSAlert* a = [[NSAlert alloc] init];
            a.messageText = ns_string(title);
            a.informativeText = ns_string(text);
            a.alertStyle = NSAlertStyleCritical;
            [a addButtonWithTitle:@"OK"];
            [NSApp activateIgnoringOtherApps:YES];
            [a runModal];
        }
    });
}

}  // namespace

uint32_t key_char(int key) { return macos::g_key_chars[size_t(key & 0xFF)].load(); }

bool key_held(int key) {
    // O estado do sistema (a sessão inteira, com ou sem uma janela com o foco), como o GetAsyncKeyState.
    const CGEventSourceStateID state = kCGEventSourceStateCombinedSessionState;
    const CGEventFlags flags = CGEventSourceFlagsState(state);
    switch (key & 0xFF) {
        case 0x10: return (flags & kCGEventFlagMaskShift) != 0;
        case 0x11: return (flags & kCGEventFlagMaskControl) != 0;
        case 0x12: return (flags & kCGEventFlagMaskAlternate) != 0;
        case 0x14: return (flags & kCGEventFlagMaskAlphaShift) != 0;
        case 0x01: return CGEventSourceButtonState(state, kCGMouseButtonLeft);
        case 0x02: return CGEventSourceButtonState(state, kCGMouseButtonRight);
        case 0x04: return CGEventSourceButtonState(state, kCGMouseButtonCenter);
        case 0x05: return CGEventSourceButtonState(state, CGMouseButton(3));
        case 0x06: return CGEventSourceButtonState(state, CGMouseButton(4));
        default: break;
    }
    const uint16_t mac = nf::mac::mac_of_vk(uint8_t(key & 0xFF));
    if (mac == 0xFFFF) return false;
    return CGEventSourceKeyState(state, CGKeyCode(mac));
}

void show_error(const std::wstring& title, const std::wstring& text) { alert(to_utf8(title), to_utf8(text)); }

void show_error_narrow(const char* title, const char* text) {
    alert(title != nullptr ? title : "", text != nullptr ? text : "");
}

}  // namespace os

}  // namespace nf
