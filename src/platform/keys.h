#pragma once
// Os códigos das teclas e dos botões do mouse no port (só do port; docs/port.md, "Plataforma: interfaces e backends").
//
// O estado do teclado e do mouse que a plataforma entrega ao jogo (g_psi_keyboard_mouse.down[256], Window::key_down, a
// tabela de teclas do nightfire.ini em platform/input_bindings.h, os roteiros de --input) é indexado por um código de
// 0 a 255. Os números são os dos "virtual-key codes" do Windows (o backend do Windows passa os dele direto; o do macOS
// traduz os seus para estes): os nomes do .ini e os testes continuam valendo em qualquer plataforma. Letras e dígitos
// são o próprio caractere maiúsculo ('A'..'Z', '0'..'9').
//
// window_win32.cpp confere com static_assert que cada valor é o VK_ do Windows.

namespace nf::key {

constexpr int LButton = 0x01;
constexpr int RButton = 0x02;
constexpr int MButton = 0x04;
constexpr int XButton1 = 0x05;  // o botão 4 do mouse (voltar)
constexpr int XButton2 = 0x06;  // o botão 5
constexpr int Back = 0x08;      // Backspace
constexpr int Tab = 0x09;
constexpr int Return = 0x0D;    // Enter
constexpr int Shift = 0x10;
constexpr int Control = 0x11;
constexpr int Menu = 0x12;      // Alt
constexpr int Capital = 0x14;   // Caps Lock
constexpr int Escape = 0x1B;
constexpr int Space = 0x20;
constexpr int Prior = 0x21;     // Page Up
constexpr int Next = 0x22;      // Page Down
constexpr int End = 0x23;
constexpr int Home = 0x24;
constexpr int Left = 0x25;
constexpr int Up = 0x26;
constexpr int Right = 0x27;
constexpr int Down = 0x28;
constexpr int Insert = 0x2D;
constexpr int Delete = 0x2E;
constexpr int LWin = 0x5B;
constexpr int RWin = 0x5C;
constexpr int Apps = 0x5D;
constexpr int Numpad0 = 0x60;   // Numpad1..9 seguem (0x61..0x69)
constexpr int Numpad7 = 0x67;
constexpr int Numpad9 = 0x69;
constexpr int Multiply = 0x6A;
constexpr int Add = 0x6B;
constexpr int Subtract = 0x6D;
constexpr int Decimal = 0x6E;
constexpr int Divide = 0x6F;
constexpr int F1 = 0x70;        // F2..F24 seguem (0x71..0x87)
constexpr int F2 = 0x71;
constexpr int F3 = 0x72;
constexpr int F4 = 0x73;
constexpr int F9 = 0x78;
constexpr int F12 = 0x7B;
constexpr int F24 = 0x87;
constexpr int LShift = 0xA0;
constexpr int RShift = 0xA1;
constexpr int LControl = 0xA2;
constexpr int RControl = 0xA3;
constexpr int LMenu = 0xA4;
constexpr int RMenu = 0xA5;
constexpr int Oem1 = 0xBA;      // ; (no ABNT2, o Ç)
constexpr int OemPlus = 0xBB;   // =
constexpr int OemComma = 0xBC;  // ,
constexpr int OemMinus = 0xBD;  // -
constexpr int OemPeriod = 0xBE; // .
constexpr int Oem2 = 0xBF;      // /
constexpr int Oem3 = 0xC0;      // `
constexpr int AbntC1 = 0xC1;    // a tecla / do teclado ABNT2
constexpr int Oem4 = 0xDB;      // [
constexpr int Oem5 = 0xDC;      // barra invertida
constexpr int Oem6 = 0xDD;      // ]
constexpr int Oem7 = 0xDE;      // '
constexpr int Oem102 = 0xE2;    // a tecla < > (ou \) dos teclados ISO

}  // namespace nf::key
