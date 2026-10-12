// O mapa de botões da aba CONTROLS da pausa (Menu_DisplayControllerStyleList, 0x001f44d8): para cada um
// dos oito estilos de botões (PlayerSetting::style), a lista 10 do painel 0x10000028 mostra em duas
// colunas o símbolo de cada botão do PS2 ("~A", "~B"... da fonte, specialchar) e o que ele faz (rótulos do
// *Txt.dat). Ver engine/menu.h.
//
// E o desenho do controle da página "Controller Setup" do menu principal (Menu_DisplayControllerStyle,
// 0x001f3558): os rótulos 0x10000062-6e, 0x100000c8 e 0x10000163 ao redor da imagem do controle com o que cada botão
// faz no estilo, e as linhas 0x10000061 (tag = os estilos em que a linha aparece, um bit por estilo).
//
// O original é um switch de 13 KB com o mesmo bloco repetido por estilo: aqui a tabela kStyleLabels,
// tirada da decompilação (os rótulos de cada estilo na ordem dos símbolos). Os blocos de cada caso têm um
// `if (estilo == 3 || estilo == 5)` que só vale nesses dois (Y e X com o mesmo rótulo); a tabela já tem o
// resultado.
//
// Divergências: o texto vai para cont_string com snprintf de 0x20 bytes (o original usa sprintf e passaria
// do fim com um rótulo longo); um rótulo sem texto (Txt_BindLabel nulo, sem arquivo de texto) vira "".

#include <cstdio>

#include "engine/menu.h"
#include "engine/modules.h"
#include "engine/txt.h"

namespace {

// "%s %s" (0x0030d1c0) e os símbolos dos botões (0x0030d1c8-0x0030d228, 8 bytes cada), na ordem em
// que aparecem: linhas (A, B), (Y, X), (C, D), (L, R), (F, E), (H, V) e a última só com T.
const char kStyleFormat[] = "%s %s";
const char* const kStyleSymbols[13] = {"~A", "~B", "~Y", "~X", "~C", "~D", "~L", "~R", "~F", "~E", "~H", "~V", "~T"};

// Os rótulos de cada estilo na ordem dos símbolos.
const uint32_t kStyleLabels[8][13] = {
    {0x8f, 0x8c, 0x8b, 0x8d, 0x84, 0x89, 0x85, 0x8a, 0x88, 0x90, 0x87, 0x86, 0x2f5},
    {0x9e, 0x9b, 0x9a, 0x9c, 0x93, 0x98, 0x94, 0x99, 0x97, 0x9f, 0x96, 0x95, 0x2f6},
    {0xad, 0xaa, 0xa9, 0xab, 0xa2, 0xa7, 0xa3, 0xa8, 0xa6, 0xae, 0xa5, 0xa4, 0x2f7},
    {0xbc, 0xb9, 0xba, 0xba, 0xb1, 0xb6, 0xb2, 0xb7, 0xb5, 0xbd, 0xb4, 0xb3, 0x2f8},
    {0xcb, 0xc8, 0xc7, 0xc9, 0xc0, 0xc5, 0xc1, 0xc6, 0xc4, 0xcc, 0xc3, 0xc2, 0x2f9},
    {0xda, 0xd7, 0xd8, 0xd8, 0xcf, 0xd4, 0xd0, 0xd5, 0xd3, 0xdb, 0xd2, 0xd1, 0x2fa},
    {0x1000198, 0x1000195, 0x1000194, 0x1000196, 0x100018d, 0x1000192, 0x100018e, 0x1000193, 0x1000191, 0x1000199,
     0x1000190, 0x100018f, 0x10001cd},
    {0x10001a7, 0x10001a4, 0x10001a3, 0x10001a5, 0x100019c, 0x10001a1, 0x100019d, 0x10001a2, 0x10001a0, 0x10001a8,
     0x100019f, 0x100019e, 0x10001ce},
};

char cont_string[13][0x20];   // 0x002e1c50 (texto de cada célula)
uint8_t cont_str_count = 0;   // 0x0030d1b8

int32_t val(const void* p) { return int32_t(nf::p32_u(p)); }

// Um texto "símbolo rótulo" em cont_string; devolve o ponteiro.
char* cell(uint32_t symbol, uint32_t label) {
    char* s = cont_string[cont_str_count];
    cont_str_count = uint8_t(cont_str_count + 1);
    const char* text = Txt_BindLabel(label, 0);
    std::snprintf(s, sizeof(cont_string[0]), kStyleFormat, kStyleSymbols[symbol], text != nullptr ? text : "");
    return s;
}

// Menu_DisplayControllerStyle: o primeiro rótulo de cada estilo (o de 0x100000c8; os de 0x10000062 a 0x1000006e
// seguem em ordem) e o de 0x10000163, como no switch de oito casos do original (0x001f358c-0x001f44d4).
const uint32_t kStyleFirstLabel[8] = {0x83, 0x92, 0xa1, 0xb0, 0xbf, 0xce, 0x100018c, 0x100019b};
const uint32_t kStyleLastLabel[8] = {0x2f5, 0x2f6, 0x2f7, 0x2f8, 0x2f9, 0x2fa, 0x10001cd, 0x10001ce};

}  // namespace

// [decomp] 0x001f3558 Menu_DisplayControllerStyle
// O controle desenhado de "Controller Setup" (0x40000022) no estilo pedido (0 a 7; outro não muda nada): o texto de
// cada botão ao redor da imagem, todas as linhas escondidas (estado 1) e as do estilo (tag com o bit 1 << estilo)
// mostradas (estado 2). Os rótulos de um estilo são consecutivos no *Txt.dat: 0x100000c8 o primeiro, depois
// 0x10000062-0x1000006e; a ordem das mensagens é a do original.
void Menu_DisplayControllerStyle(uint8_t index, uint32_t style) {
    if (style >= 8) return;
    const uint32_t base = kStyleFirstLabel[style];
    static const uint32_t kIds[13] = {0x10000062, 0x10000063, 0x10000064, 0x10000065, 0x10000067, 0x10000068, 0x10000069,
                                      0x1000006a, 0x1000006b, 0x1000006c, 0x1000006d, 0x1000006e, 0x10000066};
    for (uint32_t id : kIds) __Menu_Send(index, id, 0x18, 0, int32_t(base + (id - 0x10000061)));
    __Menu_Send(index, 0x100000c8, 0x18, 0, int32_t(base));
    __Menu_Send(index, 0x10000163, 0x18, 0, int32_t(kStyleLastLabel[style]));
    __Menu_Send(index, 0x10000061, 0x2b, 1, 0);
    __Menu_SendEx2(index, 0x10000061, 1u << style, 0x2b, 2, 0);
}

// [decomp] 0x001f44d8 Menu_DisplayControllerStyleList
// Limpa a lista 10 do painel da pausa, põe duas colunas de 52% e, para o estilo pedido (0 a 7; outro não
// mostra nada), seis linhas de dois botões e uma de um.
void Menu_DisplayControllerStyleList(uint8_t index, uint32_t style) {
    cont_str_count = 0;
    __Menu_SendEx(index, 0x10000028, 10, 0x17, 0, 0);
    __Menu_SendEx(index, 0x10000028, 10, 0xe, 0x34, 1);
    __Menu_SendEx(index, 0x10000028, 10, 0x28, 0, 0x34);
    if (style >= 8) return;
    const uint32_t* labels = kStyleLabels[style];
    for (uint32_t pair = 0; pair < 6; ++pair) {
        char* left = cell(pair * 2, labels[pair * 2]);
        const uint32_t row = uint32_t(__Menu_SendEx(index, 0x10000028, 10, 0x10, val(left), 0));
        char* right = cell(pair * 2 + 1, labels[pair * 2 + 1]);
        __Menu_SendEx(index, 0x10000028, 10, 0x19, int32_t((row & 0xff) << 16 | 1), val(right));
    }
    __Menu_SendEx(index, 0x10000028, 10, 0x10, val(cell(12, labels[12])), 0);
}
