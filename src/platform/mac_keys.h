#pragma once
// O teclado e a roda do mouse do macOS traduzidos para os códigos do port (só do port; docs/port.md, "macOS: os
// backends"). Só dados e contas, sem nada do sistema: o backend da janela do macOS (window_macos.mm) usa, e o nf_tests
// confere em todas as plataformas (tests/test_macos.cpp).
//
// As teclas: o NSEvent dá o código virtual do macOS (os kVK_* do Carbon, HIToolbox/Events.h: a posição física da tecla,
// a do teclado ANSI americano), e o port usa os números dos VK do Windows (platform/keys.h). A tabela liga um ao outro
// pela posição, como o Windows faz com o layout americano: kVK_ANSI_Semicolon é VK_OEM_1, kVK_ANSI_Grave é VK_OEM_3. O
// caractere que cada tecla dá no layout do usuário (o Ç do ABNT2) vem do UCKeyTranslate (os::key_char), não daqui.
// Particularidades do Mac:
//  - Return e o Enter do teclado numérico são os dois VK_RETURN (no Windows o do teclado numérico também é, com a
//    bandeira "estendida"), a única repetição;
//  - Command é a tecla Windows (VK_LWIN/VK_RWIN), Option é o Alt (VK_LMENU/VK_RMENU); Shift, Control e Option também
//    dão o VK genérico (VK_SHIFT, VK_CONTROL, VK_MENU), como o GetAsyncKeyState do Windows;
//  - Help (onde fica o Insert nos teclados estendidos da Apple) é VK_INSERT; Clear (no lugar do Num Lock) é VK_NUMLOCK;
//    a tecla Delete do Mac é o Backspace (VK_BACK) e a "forward delete" é o Delete (VK_DELETE);
//  - kVK_ISO_Section (a tecla § à esquerda do 1 nos teclados ISO da Apple) é VK_OEM_102; nos teclados ISO da Apple a
//    tecla ` fica ao lado do Shift esquerdo e continua dando kVK_ANSI_Grave (VK_OEM_3);
//  - o Fn, as teclas de volume e as do teclado japonês (Eisu, Kana, Yen, o "_") ficam de fora (nenhum VK).
// O macOS não tem Pause, Scroll Lock nem Print Screen; F13 a F20 existem nos teclados estendidos.
//
// A roda: o macOS dá a rolagem em linhas (as rodas comuns, já com a aceleração do sistema) ou em pontos (o trackpad e o
// Magic Mouse, hasPreciseScrollingDeltas), e o Windows em entalhes de 120 (WHEEL_DELTA); Window::take_wheel devolve
// entalhes (1.0 por entalhe). MacWheel converte: uma linha é um entalhe (um evento de menos de uma linha conta um
// entalhe inteiro, para uma roda lenta não se perder); kMacWheelPointsPerNotch pontos de um aparelho preciso são um
// entalhe, somados entre os eventos (a inércia do trackpad, o momentumPhase, fica de fora no backend). O sentido é o do
// aparelho (a "rolagem natural" do macOS desfeita pelo backend com isDirectionInvertedFromDevice): positivo = para cima,
// para longe do jogador, como no Windows.

#include <cstdint>

namespace nf::mac {

struct MacKey {
    uint16_t mac;     // o kVK_*
    uint8_t vk;       // o código do port (platform/keys.h)
    uint8_t generic;  // o VK genérico que também fica seguro (VK_SHIFT, VK_CONTROL, VK_MENU) ou 0
};

// Os kVK_* (os números do HIToolbox/Events.h; window_macos.mm confere com static_assert contra as constantes do Carbon).
namespace kvk {
constexpr uint16_t A = 0x00, S = 0x01, D = 0x02, F = 0x03, H = 0x04, G = 0x05, Z = 0x06, X = 0x07, C = 0x08, V = 0x09;
constexpr uint16_t IsoSection = 0x0A, B = 0x0B, Q = 0x0C, W = 0x0D, E = 0x0E, R = 0x0F, Y = 0x10, T = 0x11;
constexpr uint16_t N1 = 0x12, N2 = 0x13, N3 = 0x14, N4 = 0x15, N6 = 0x16, N5 = 0x17, Equal = 0x18, N9 = 0x19;
constexpr uint16_t N7 = 0x1A, Minus = 0x1B, N8 = 0x1C, N0 = 0x1D, RightBracket = 0x1E, O = 0x1F, U = 0x20;
constexpr uint16_t LeftBracket = 0x21, I = 0x22, P = 0x23, Return = 0x24, L = 0x25, J = 0x26, Quote = 0x27, K = 0x28;
constexpr uint16_t Semicolon = 0x29, Backslash = 0x2A, Comma = 0x2B, Slash = 0x2C, N = 0x2D, M = 0x2E, Period = 0x2F;
constexpr uint16_t Tab = 0x30, Space = 0x31, Grave = 0x32, Delete = 0x33, Escape = 0x35, RightCommand = 0x36;
constexpr uint16_t Command = 0x37, Shift = 0x38, CapsLock = 0x39, Option = 0x3A, Control = 0x3B, RightShift = 0x3C;
constexpr uint16_t RightOption = 0x3D, RightControl = 0x3E, Function = 0x3F, F17 = 0x40, KeypadDecimal = 0x41;
constexpr uint16_t KeypadMultiply = 0x43, KeypadPlus = 0x45, KeypadClear = 0x47, KeypadDivide = 0x4B;
constexpr uint16_t KeypadEnter = 0x4C, KeypadMinus = 0x4E, F18 = 0x4F, F19 = 0x50, Keypad0 = 0x52, Keypad1 = 0x53;
constexpr uint16_t Keypad2 = 0x54, Keypad3 = 0x55, Keypad4 = 0x56, Keypad5 = 0x57, Keypad6 = 0x58, Keypad7 = 0x59;
constexpr uint16_t F20 = 0x5A, Keypad8 = 0x5B, Keypad9 = 0x5C, F5 = 0x60, F6 = 0x61, F7 = 0x62, F3 = 0x63, F8 = 0x64;
constexpr uint16_t F9 = 0x65, F11 = 0x67, F13 = 0x69, F16 = 0x6A, F14 = 0x6B, F10 = 0x6D, ContextualMenu = 0x6E;
constexpr uint16_t F12 = 0x6F, F15 = 0x71, Help = 0x72, Home = 0x73, PageUp = 0x74, ForwardDelete = 0x75, F4 = 0x76;
constexpr uint16_t End = 0x77, F2 = 0x78, PageDown = 0x79, F1 = 0x7A, LeftArrow = 0x7B, RightArrow = 0x7C;
constexpr uint16_t DownArrow = 0x7D, UpArrow = 0x7E;
}  // namespace kvk

// Os VK do Windows que platform/keys.h não nomeia e a tabela usa.
constexpr uint8_t kVkNumLock = 0x90;

inline constexpr MacKey kMacKeys[] = {
    // letras e dígitos: o próprio caractere
    {kvk::A, 'A', 0}, {kvk::B, 'B', 0}, {kvk::C, 'C', 0}, {kvk::D, 'D', 0}, {kvk::E, 'E', 0}, {kvk::F, 'F', 0},
    {kvk::G, 'G', 0}, {kvk::H, 'H', 0}, {kvk::I, 'I', 0}, {kvk::J, 'J', 0}, {kvk::K, 'K', 0}, {kvk::L, 'L', 0},
    {kvk::M, 'M', 0}, {kvk::N, 'N', 0}, {kvk::O, 'O', 0}, {kvk::P, 'P', 0}, {kvk::Q, 'Q', 0}, {kvk::R, 'R', 0},
    {kvk::S, 'S', 0}, {kvk::T, 'T', 0}, {kvk::U, 'U', 0}, {kvk::V, 'V', 0}, {kvk::W, 'W', 0}, {kvk::X, 'X', 0},
    {kvk::Y, 'Y', 0}, {kvk::Z, 'Z', 0},
    {kvk::N0, '0', 0}, {kvk::N1, '1', 0}, {kvk::N2, '2', 0}, {kvk::N3, '3', 0}, {kvk::N4, '4', 0},
    {kvk::N5, '5', 0}, {kvk::N6, '6', 0}, {kvk::N7, '7', 0}, {kvk::N8, '8', 0}, {kvk::N9, '9', 0},
    // as de controle
    {kvk::Return, 0x0D, 0}, {kvk::KeypadEnter, 0x0D, 0}, {kvk::Tab, 0x09, 0}, {kvk::Space, 0x20, 0},
    {kvk::Delete, 0x08, 0}, {kvk::Escape, 0x1B, 0}, {kvk::ForwardDelete, 0x2E, 0}, {kvk::Help, 0x2D, 0},
    {kvk::Home, 0x24, 0}, {kvk::End, 0x23, 0}, {kvk::PageUp, 0x21, 0}, {kvk::PageDown, 0x22, 0},
    {kvk::LeftArrow, 0x25, 0}, {kvk::UpArrow, 0x26, 0}, {kvk::RightArrow, 0x27, 0}, {kvk::DownArrow, 0x28, 0},
    {kvk::CapsLock, 0x14, 0}, {kvk::ContextualMenu, 0x5D, 0},
    // os modificadores (os eventos flagsChanged)
    {kvk::Shift, 0xA0, 0x10}, {kvk::RightShift, 0xA1, 0x10}, {kvk::Control, 0xA2, 0x11},
    {kvk::RightControl, 0xA3, 0x11}, {kvk::Option, 0xA4, 0x12}, {kvk::RightOption, 0xA5, 0x12},
    {kvk::Command, 0x5B, 0}, {kvk::RightCommand, 0x5C, 0},
    // F1-F20
    {kvk::F1, 0x70, 0}, {kvk::F2, 0x71, 0}, {kvk::F3, 0x72, 0}, {kvk::F4, 0x73, 0}, {kvk::F5, 0x74, 0},
    {kvk::F6, 0x75, 0}, {kvk::F7, 0x76, 0}, {kvk::F8, 0x77, 0}, {kvk::F9, 0x78, 0}, {kvk::F10, 0x79, 0},
    {kvk::F11, 0x7A, 0}, {kvk::F12, 0x7B, 0}, {kvk::F13, 0x7C, 0}, {kvk::F14, 0x7D, 0}, {kvk::F15, 0x7E, 0},
    {kvk::F16, 0x7F, 0}, {kvk::F17, 0x80, 0}, {kvk::F18, 0x81, 0}, {kvk::F19, 0x82, 0}, {kvk::F20, 0x83, 0},
    // o teclado numérico
    {kvk::Keypad0, 0x60, 0}, {kvk::Keypad1, 0x61, 0}, {kvk::Keypad2, 0x62, 0}, {kvk::Keypad3, 0x63, 0},
    {kvk::Keypad4, 0x64, 0}, {kvk::Keypad5, 0x65, 0}, {kvk::Keypad6, 0x66, 0}, {kvk::Keypad7, 0x67, 0},
    {kvk::Keypad8, 0x68, 0}, {kvk::Keypad9, 0x69, 0}, {kvk::KeypadMultiply, 0x6A, 0}, {kvk::KeypadPlus, 0x6B, 0},
    {kvk::KeypadMinus, 0x6D, 0}, {kvk::KeypadDecimal, 0x6E, 0}, {kvk::KeypadDivide, 0x6F, 0},
    {kvk::KeypadClear, kVkNumLock, 0},
    // a pontuação, pela posição do teclado americano (os VK_OEM_*)
    {kvk::Semicolon, 0xBA, 0}, {kvk::Equal, 0xBB, 0}, {kvk::Comma, 0xBC, 0}, {kvk::Minus, 0xBD, 0},
    {kvk::Period, 0xBE, 0}, {kvk::Slash, 0xBF, 0}, {kvk::Grave, 0xC0, 0}, {kvk::LeftBracket, 0xDB, 0},
    {kvk::Backslash, 0xDC, 0}, {kvk::RightBracket, 0xDD, 0}, {kvk::Quote, 0xDE, 0}, {kvk::IsoSection, 0xE2, 0},
};

// O código do port de um kVK (0: nenhum).
inline uint8_t vk_of_mac(uint16_t mac) {
    for (const MacKey& k : kMacKeys) {
        if (k.mac == mac) return k.vk;
    }
    return 0;
}

// A entrada da tabela de um kVK (nula se nenhuma).
inline const MacKey* mac_key(uint16_t mac) {
    for (const MacKey& k : kMacKeys) {
        if (k.mac == mac) return &k;
    }
    return nullptr;
}

// O kVK de um código do port (o primeiro: VK_RETURN é o Return), 0xFFFF se nenhum. Os genéricos (VK_SHIFT, VK_CONTROL,
// VK_MENU) dão o do lado esquerdo.
inline uint16_t mac_of_vk(uint8_t vk) {
    if (vk == 0x10) vk = 0xA0;
    if (vk == 0x11) vk = 0xA2;
    if (vk == 0x12) vk = 0xA4;
    for (const MacKey& k : kMacKeys) {
        if (k.vk == vk) return k.mac;
    }
    return 0xFFFF;
}

// ---------------------------------------------------------------- a roda

constexpr double kMacWheelPointsPerNotch = 40.0;

// Os entalhes de cada evento de rolagem (delta: linhas, ou pontos com precise; positivo para cima, no sentido do
// aparelho). Os pontos que sobram ficam para o próximo evento.
class MacWheel {
public:
    double add(double delta, bool precise) {
        if (delta == 0.0) return 0.0;
        if (!precise) {
            // Uma roda comum: linhas (com a aceleração do sistema). Menos de uma linha num evento conta um entalhe.
            if (delta > 0.0 && delta < 1.0) return 1.0;
            if (delta < 0.0 && delta > -1.0) return -1.0;
            return delta;
        }
        // Um aparelho preciso: pontos, somados até um entalhe (o resto fica; a troca de sentido zera o resto).
        if ((points_ > 0.0 && delta < 0.0) || (points_ < 0.0 && delta > 0.0)) points_ = 0.0;
        points_ += delta;
        double notches = 0.0;
        while (points_ >= kMacWheelPointsPerNotch) {
            points_ -= kMacWheelPointsPerNotch;
            notches += 1.0;
        }
        while (points_ <= -kMacWheelPointsPerNotch) {
            points_ += kMacWheelPointsPerNotch;
            notches -= 1.0;
        }
        return notches;
    }
    void reset() { points_ = 0.0; }
    double pending() const { return points_; }

private:
    double points_ = 0.0;
};

}  // namespace nf::mac
