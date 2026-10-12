// Port do módulo Font do ACTION.ELF (0x001ee298-0x001ef79c): medida, quebra de linha e desenho de
// textos. Ver font.h.
//
// Divergências:
// - A busca de um glifo (find_glyph) para no fim da tabela: com um código acima do último glifo (253 a
//   255; o último é 252) o original leria a entrada depois da tabela; aqui não há glifo
//   (g_font_guards.past_table).
// - O ™ (0x99) troca o glifo por 'T' e 'M'; sem eles na fonte o original seguiria um ponteiro nulo; aqui o
//   caractere é pulado (g_font_guards.no_glyph). Nenhuma fonte do disco está nesse caso.
// - "~" no fim do texto: o original pula o terminador e continua lendo depois da string; aqui o texto
//   acaba ali.
// - Font_GetTextExtent e Font_GetTextCountExtent passam a Font_ParseFormat uma variável de flags não
//   inicializada (só a flag 1, "só medir", importa: com ela a textura em CharInfo não muda); aqui ela
//   começa em 0.
// - Font_WordWrapString: uma linha maior que um buffer do StringHeap (0x168 bytes) é cortada no buffer
//   (g_font_guards.long_line); o original escreveria além dele.
// - Font_GetAlignment com formato nulo devolve 1 (o original leria o endereço 0).

#include "engine/font.h"

#include <algorithm>
#include <cstring>

#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "engine/sprite.h"
#include "engine/txt.h"
#include "platform/psi.h"

KERN_tag KernNFont2[kKernNFont2];
KERN_tag KernMedium[kKernMedium];
KERN_tag KernSerpLight[kKernSerpLight];
CHARDATA_tag FontDataNFont2[kFontChars];
CHARDATA_tag FontDataMedium[kFontChars];
CHARDATA_tag FontDataSerpLight[kFontChars];
FONT_tag FONTNFont2;
FONT_tag FONTMedium;
FONT_tag FONTSerpLight;
FONTTABLE_tag FontTable[kFontTableCount];
SPECIALCHAR_tag specialchar[kSpecialChars];
FONT_tag* Font = nullptr;
SPRITE_DRAW CharInfo;
PRINTINFO_tag PInfo;
FontGuards g_font_guards;
const void* g_font_origin_sprite = nullptr;
Psi2DPass g_font_origin_pass = Psi2DPass::Main;

namespace {

// Estáticos de __Font_DrawText (os sufixos são os dos símbolos).
GPOINT Size_126;                       // largura e altura medidas: o que a função devolve
GPOINT Scale_127;
GPOINT Pos_128;                        // a caneta
const CHARDATA_tag* fd_129 = nullptr;  // glifo atual
float x_130 = 0.0f;
float y_131 = 0.0f;
float fStartX_132 = 0.0f;
int32_t i_133 = 0;                     // caracteres lidos
// Só no PC, para a origem dos glifos (Psi2DOrigin::run/align): a chamada de __Font_DrawText em curso e
// os bits de alinhamento dela.
uint16_t run_count = 0;
uint8_t run_align = 0;
// De Font_DrawText.
GPOINT Size_137;
GPOINT TScale_138;

// A busca binária do original (repetida inline em cada função): lo = 0, hi = count, meio = (lo + hi) >> 1;
// código maior: hi = meio - 1; menor: lo = meio + 1; igual: o glifo. Sem igual, o da posição hi (o glifo
// antes do buraco: os códigos sem glifo entre first e last desenham o anterior). Fora de [first, last]:
// nenhum.
const CHARDATA_tag* find_glyph(const FONT_tag* f, uint32_t c) {
    if (f == nullptr || c < f->first || f->last < c) return nullptr;
    int32_t lo = 0, hi = int32_t(f->count);
    do {
        const int32_t mid = (lo + hi) >> 1;
        if (mid >= int32_t(f->count)) {
            ++g_font_guards.past_table;
            return nullptr;
        }
        const CHARDATA_tag* e = &f->chars[mid];
        if (c < e->code) {
            hi = mid - 1;
        } else if (e->code < c) {
            lo = mid + 1;
        } else {
            return e;
        }
    } while (lo <= hi);
    if (hi < 0) {
        ++g_font_guards.past_table;
        return nullptr;
    }
    return &f->chars[hi];
}

// O código de fonte (0xff n do formato) da fonte em uso, para a origem dos glifos; 0 se nenhuma.
uint8_t font_code(const FONT_tag* f) {
    for (int k = 0; k < kFontTableCount; ++k) {
        if (f != nullptr && FontTable[k].font == f) return uint8_t(k + 1);
    }
    return 0;
}

// O SPRITE_DRAW do glifo e a origem dele (só no PC).
void add_glyph(const SPRITE_DRAW& draw, Psi2DKind kind, uint8_t glyph, const char* text) {
    SPRITE_DRAW* slot = View_AddSprite(0);
    amemcpy(slot, &draw, sizeof(SPRITE_DRAW));
    Psi2DOrigin& o = *View_SpriteOrigin(slot);
    o = Psi2DOrigin{};
    o.kind = kind;
    o.pass = g_font_origin_pass;
    o.font = font_code(Font);
    o.glyph = glyph;
    o.sprite = g_font_origin_sprite;
    if (o.font != 0) o.texture_hash = FontTable[o.font - 1].hash;
    o.text = text;
    o.char_index = uint16_t(i_133);
    o.run = run_count;
    o.align = run_align;
    o.scale = Scale_127.y;
    o.baseline = Pos_128.y;
}

}  // namespace

void font_debug_power_on() {
    Font = nullptr;
    std::memset(&CharInfo, 0, sizeof(CharInfo));
    std::memset(&PInfo, 0, sizeof(PInfo));
    g_font_guards = {};
    g_font_origin_sprite = nullptr;
    g_font_origin_pass = Psi2DPass::Main;
    Size_126 = Scale_127 = Pos_128 = Size_137 = TScale_138 = GPOINT{};
    fd_129 = nullptr;
    x_130 = y_131 = fStartX_132 = 0.0f;
    i_133 = 0;
    run_count = 0;
    run_align = 0;
}

// [decomp] 0x001ee298 Font_GetKernAdjust
// O ajuste entre prev e c: procura o glifo de prev (sem sinal) e percorre os pares dele até um com o
// segundo caractere menor que c (com sinal) ou igual a ele. Comportamento original preservado: as listas
// estão em ordem crescente, então só o primeiro par de cada glifo pode valer ("AT" ajusta, "AY" não);
// caracteres acima de 0x7f (negativos) nunca têm kerning.
float Font_GetKernAdjust(char c, char prev) {
    const int32_t sc = int32_t(int8_t(c));
    const CHARDATA_tag* g = find_glyph(Font, uint8_t(prev));
    if (g == nullptr) return 0.0f;
    if (sc < int32_t(Font->first) || int32_t(Font->last) < sc) return 0.0f;
    if (g->kern_count == 0 || g->kern == nullptr) return 0.0f;
    for (uint32_t i = 0; i < g->kern_count; ++i) {
        const int32_t second = int32_t(g->kern[i].second);
        if (second < sc) return 0.0f;
        if (sc == second) return float(g->kern[i].adjust);
    }
    return 0.0f;
}

// [decomp] 0x001ee3e0 Font_ParseFormat
// Começa na fonte 1 (FontTable[0]) e aplica os códigos do formato: 0xfa r g b x põe a cor em CharInfo,
// 0xfe n escolhe o alinhamento (flags 0x04 à direita, 0x02 centrado; outros, à esquerda), 0xff n troca a
// fonte e, sem a flag 1, põe em CharInfo a textura dela e 1 / largura, 1 / altura; 0xfb n e 0xfc n não
// fazem nada aqui; qualquer outro byte é pulado. "scale" não é usado.
void Font_ParseFormat(char* format, uint32_t& flags, GPOINT&) {
    Font = FontTable[0].font;
    if (format == nullptr) return;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(format);
    while (*p != 0) {
        const int8_t code = int8_t(*p + 6);
        ++p;
        switch (code) {
            case 0:
                CharInfo.r = p[0];
                CharInfo.g = p[1];
                CharInfo.b = p[2];
                p += 4;
                break;
            case 1:
            case 2:
                ++p;
                break;
            case 4: {
                flags &= 0xffffff00u;
                const int8_t v = int8_t(*p);
                if (v == 2) {
                    flags |= 4;
                } else if (v == 3) {
                    flags |= 2;
                }
                ++p;
                break;
            }
            case 5: {
                const int32_t k = int32_t(int8_t(*p)) - 1;
                if (k < 0 || k >= kFontTableCount) {  // só no port: o original leria fora de FontTable
                    ++p;
                    break;
                }
                Font = FontTable[k].font;
                if ((flags & 1) == 0) {
                    const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(FontTable[k].hash));
                    if (t != nullptr) {
                        CharInfo.texture = uint32_t(reinterpret_cast<uintptr_t>(t->rec));
                        CharInfo.inv_h = 1.0f / float(uint16_t(t->height));
                        CharInfo.inv_w = 1.0f / float(uint16_t(t->width));
                    }
                }
                ++p;
                break;
            }
            default:  // 3 (0xfd) e os bytes que não são códigos
                break;
        }
    }
}

// [decomp] 0x001ee598 __Font_DrawText
// Percorre até "count" caracteres de "text" a partir de (x, y), com a fonte em uso (Font) e a escala;
// sem a flag 1, cada glifo vira um sprite (CharInfo copiado com View_AddSprite) com a cor "colour"
// (0xRRGGBBAA). Devolve {largura, altura}: a largura é o máximo que a caneta andou, a altura vai da
// primeira linha à última. Com "fit", guarda nele quantos caracteres cabiam em "width" (a contagem do
// último caractere lido enquanto a largura não passou dela). "colour2" e "colour3" não são usados.
//  - '\n' volta ao começo e desce (altura do primeiro glifo + espaço entre linhas + 1) * escala y;
//  - ' ' e 0xa0 andam space * 1.25 * escala x;
//  - 0xba troca a cor: a primeira vez 0x785a14ff, a segunda 0x7d6d59ff;
//  - '~X': o símbolo X de specialchar (cor 0x7f7f7f, alfa da cor do texto), andando a largura dele;
//  - 0x92 vira '\'' e 0x85 vira '.'; 0x99 (™) desenha 'T' e depois 'M' em meio tamanho;
//  - um glifo: kerning com o anterior, espaço antes (se houver anterior), o glifo, espaço depois.
GPOINT* __Font_DrawText(float x, float y, uint32_t colour, uint32_t, uint32_t, char* text, char*, GPOINT* scale,
                        uint32_t flags, int32_t count, float width, uint32_t* fit) {
    uint32_t tm = 0;  // estado do ™: 0, 1 ('T' desenhado), 2 ('M')
    Scale_127.y = 1.0f;
    y_131 = y;
    fStartX_132 = x;
    int32_t toggles = 0;
    Size_126 = GPOINT{0.0f, 0.0f};
    Scale_127.x = 1.0f;
    Pos_128 = GPOINT{x, y};
    fd_129 = nullptr;
    x_130 = x;
    i_133 = 0;
    if ((flags & 1) == 0) {  // só no PC: a origem dos glifos (uma chamada de desenho por vez)
        ++run_count;
        run_align = uint8_t(flags & 6);
    }
    if (Font == nullptr) return &Size_126;
    if (scale != nullptr) Scale_127 = *scale;
    const char* const start = text;
    int32_t c = 0;
    if (*text != 0 && count != 0) {
        do {
            --count;
            const int32_t prev = c;
            ++i_133;
            c = int8_t(*text++);
            for (;;) {
                uint32_t next = tm ^ 2;  // s2: o laço interno repete enquanto o ™ não terminou
                if (c == '\n') {
                    Pos_128.x = fStartX_132;
                    Pos_128.y = Pos_128.y + ((float(Font->chars[0].h) + Font->line_gap) + 1.0f) * Scale_127.y;
                } else if (c == ' ' || c == -0x60) {
                    Pos_128.x = Pos_128.x + Font->space * (Scale_127.x * 1.25f);
                } else if (c == -0x46) {
                    if (toggles == 0) {
                        colour = 0x785a14ffu;
                    } else if (toggles == 1) {
                        colour = 0x7d6d59ffu;
                    }
                    ++toggles;
                } else if (c == '~') {
                    float advance = 0.0f;
                    const int8_t code = int8_t(*text);
                    const SPECIALCHAR_tag* sc = nullptr;
                    for (int k = 0; k < kSpecialChars; ++k) {
                        if (specialchar[k].code == code) {
                            advance = float(specialchar[k].width);
                            sc = &specialchar[k];
                            break;
                        }
                    }
                    if (*text != 0) ++text;  // só no port: não passa do fim da string
                    ++count;
                    if (sc != nullptr) {
                        if ((flags & 1) == 0) {
                            SPRITE_DRAW* slot = View_AddSprite(0);
                            const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(sc->hash));
                            slot->texture = t != nullptr ? uint32_t(reinterpret_cast<uintptr_t>(t->rec)) : 0;
                            slot->inv_w = t != nullptr ? 1.0f / float(uint16_t(t->width)) : 1.0f;
                            slot->inv_h = t != nullptr ? 1.0f / float(uint16_t(t->height)) : 1.0f;
                            CharInfo.a = uint8_t(colour);
                            slot->r = slot->g = slot->b = 0x7f;
                            slot->a = CharInfo.a;
                            slot->w = float(sc->width) * Scale_127.x;
                            slot->h = float(sc->height) * Scale_127.y;
                            slot->x = Pos_128.x;
                            slot->y = Pos_128.y - (float(sc->height) + float(Font->chars[0].h) * Scale_127.y) * 0.5f;
                            slot->u = int16_t(sc->u);
                            slot->v = int16_t(sc->v);
                            slot->uw = int16_t(sc->uw);
                            slot->flags = 0;
                            slot->vh = int16_t(sc->vh);
                            Psi2DOrigin& o = *View_SpriteOrigin(slot);
                            o = Psi2DOrigin{};
                            o.kind = Psi2DKind::Symbol;
                            o.pass = g_font_origin_pass;
                            o.font = font_code(Font);
                            o.glyph = uint8_t(code);
                            o.sprite = g_font_origin_sprite;
                            o.texture_hash = sc->hash;
                            o.text = start;
                            o.char_index = uint16_t(i_133);
                            o.run = run_count;
                            o.align = run_align;
                            o.scale = Scale_127.y;
                            o.baseline = Pos_128.y;
                        }
                        Pos_128.x = Pos_128.x + advance;
                        Size_126.x = std::max(Pos_128.x - x_130, Size_126.x);
                    }
                } else {
                    if (c == -0x6e) c = 0x27;
                    if (c == -0x7b) c = 0x2e;
                    fd_129 = find_glyph(Font, uint32_t(c & 0xff));
                    if (fd_129 == nullptr) break;  // sem glifo: o caractere some (nem a contagem de "fit")
                    if (fd_129->code == 0x99) {
                        if (tm == 0) {
                            fd_129 = find_glyph(Font, 'T');
                            tm = 1;
                        } else if (tm == 1) {
                            fd_129 = find_glyph(Font, 'M');
                            tm = 2;
                        } else {
                            ++tm;
                        }
                        if (fd_129 == nullptr) {
                            ++g_font_guards.no_glyph;
                            tm = 0;
                            break;
                        }
                    }
                    const CHARDATA_tag* fd = fd_129;
                    float h = float(fd->h) * Scale_127.y;
                    float w = float(fd->w) * Scale_127.x;
                    if (tm != 0) {
                        w = w * 0.5f;
                        h = h * 0.5f;
                    }
                    const float kern = Font_GetKernAdjust(char(c), char(prev));
                    Pos_128.x = Pos_128.x + kern * Scale_127.x;
                    float pen = Pos_128.x;
                    if (prev != 0) pen = pen + float(fd->pre) * Scale_127.x;
                    Pos_128.x = pen;
                    if ((flags & 1) == 0) {
                        const CHARDATA_tag& first = Font->chars[0];
                        CharInfo.x = pen;
                        CharInfo.y = (Pos_128.y + float(fd->y_offset) * Scale_127.y) -
                                     float(int32_t(first.h) + int32_t(first.y_offset)) * Scale_127.y;
                        CharInfo.h = h;
                        CharInfo.w = w;
                        CharInfo.u = int16_t(fd->u);
                        CharInfo.uw = int16_t(fd->w);
                        CharInfo.v = int16_t(fd->v);
                        CharInfo.vh = int16_t(fd->h);
                        CharInfo.r = uint8_t(colour >> 24);
                        CharInfo.g = uint8_t(colour >> 16);
                        CharInfo.b = uint8_t(colour >> 8);
                        CharInfo.a = uint8_t(colour);
                        add_glyph(CharInfo, Psi2DKind::Glyph, uint8_t(fd->code), start);
                    }
                    next = tm ^ 2;
                    Pos_128.x = (Pos_128.x + float(fd->post) * Scale_127.x) + w;
                    Size_126.x = std::max(Pos_128.x - x_130, Size_126.x);
                }
                if (Size_126.x <= width && fit != nullptr) *fit = uint32_t(i_133);
                if (next == 0) tm = 0;
                if (tm == 0) break;
            }
        } while (*text != 0 && count != 0);
    }
    Size_126.y = (Pos_128.y - y_131) + ((Scale_127.y * float(Font->chars[0].h) + Font->line_gap) + 1.0f);
    return &Size_126;
}

// [decomp] 0x001eee48 Font_DrawText
// O texto com o formato aplicado (fonte, alinhamento: x recua width / 2 centrado ou width à direita) e as
// cópias de efeito: flag 0x1000 uma sombra em (+1, +1) com "shadow"; senão, com 0x4000, um contorno de
// quatro cópias em (+-1, +-1) com "shadow"; com 0x2000, mais uma cópia em (-1, -1) com "outline". Cada
// cópia leva as flags baixas | 8. Devolve a medida do último desenho ({0, 0} sem texto, formato ou fonte).
GPOINT* Font_DrawText(float x, float y, uint32_t colour, uint32_t shadow, uint32_t outline, char* text, char* format,
                      GPOINT* scale, uint32_t flags, int32_t count, float width) {
    Size_137 = GPOINT{0.0f, 0.0f};
    if (count == 0 || text == nullptr || format == nullptr) return &Size_137;
    TScale_138.x = scale != nullptr ? scale->x : 1.0f;
    TScale_138.y = scale != nullptr ? scale->y : 1.0f;
    uint32_t f = flags;
    Font_ParseFormat(format, f, TScale_138);
    if (Font == nullptr) return &Size_137;
    if ((f & 2) != 0) x = x - width * 0.5f;
    if ((f & 4) != 0) x = x - width;
    const uint32_t copy = (f & 0xff) | 8;
    const Psi2DPass pass = g_font_origin_pass;
    if ((f & 0x1000) != 0) {
        g_font_origin_pass = Psi2DPass::Shadow;
        __Font_DrawText(x + 1.0f, y + 1.0f, shadow, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
    } else if ((f & 0x4000) != 0) {
        g_font_origin_pass = Psi2DPass::Outline;
        const float left = x - 1.0f, right = x + 1.0f, up = y - 1.0f, down = y + 1.0f;
        __Font_DrawText(left, up, shadow, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
        __Font_DrawText(left, down, shadow, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
        __Font_DrawText(right, up, shadow, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
        __Font_DrawText(right, down, shadow, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
    }
    if ((f & 0x2000) != 0) {
        g_font_origin_pass = Psi2DPass::Outline;
        __Font_DrawText(x - 1.0f, y - 1.0f, outline, 0, 0, text, format, &TScale_138, copy, count, -1.0f, nullptr);
    }
    g_font_origin_pass = pass;
    return __Font_DrawText(x, y, colour, shadow, 0, text, format, &TScale_138, f, count, -1.0f, nullptr);
}

// [decomp] 0x001ef178 Font_WordWrapString
// A próxima linha de *text que cabe em "width" pixels (sem escala), copiada para um buffer do
// StringHeap; *text passa para o resto (nulo no fim). Quebra depois do último espaço ou tab que coube,
// num '\n' (que fica no fim da linha), ou, sem espaço, antes do caractere que passou. Devolve nulo
// quando nada coube (com *text igual), ou o texto vazio (0x0030d138) quando a linha seria vazia num '\n'.
// "length" não é usado; "scale" só vai para Font_ParseFormat (a largura é medida com escala 1).
// Comportamento original preservado: o teste de espaço antes do '\n' olha o próprio '\n' e nunca recua.
char* Font_WordWrapString(char** text, uint16_t, char* format, uint32_t width, GPOINT* scale) {
    static char kEmptyLine[1] = "";  // 0x0030d138
    GPOINT s{scale != nullptr ? scale->x : 1.0f, scale != nullptr ? scale->y : 1.0f};
    char* const begin = *text;
    if (begin == nullptr) return nullptr;
    char* result = nullptr;
    PInfo.unknown_00 = 0;
    PInfo.unknown_06 = 0xffff;
    PInfo.unknown_04 = 0;
    PInfo.unknown_02 = 0;
    PInfo.unknown_08[0] = 1.0f;
    PInfo.unknown_08[1] = 1.0f;
    for (float& v : PInfo.unknown_10) v = 0.0f;
    std::memset(PInfo.unknown_2c, 0, sizeof(PInfo.unknown_2c));
    PInfo.unknown_30 = 0;
    PInfo.unknown_34 = 0;
    PInfo.format = format;
    PInfo.unknown_3c = 0;
    PInfo.unknown_28 = 0.0f;
    uint32_t flags = 0;
    Font_ParseFormat(format, flags, s);
    char* const line = *text;
    float extent = 0.0f;
    const float sx = 1.0f;
    char* cut = begin;   // s2: onde a linha termina
    uint32_t n = 0;      // s5: caracteres lidos
    const float limit = float(width);
    if (*line == 0) {
        cut = line;
    } else {
        int32_t prev = 0;
        const char* cursor = line;
        const char* after = line;  // s0
        bool overflow = false;
        bool newline = false;
        for (;;) {
            if (limit <= extent) {
                overflow = true;
                break;
            }
            ++n;
            int32_t c = int8_t(*cursor++);
            if (c == '\t' || c == ' ') {
                cut = line + n;
                after = cut;
            } else if (c == '\n') {
                cut = line + n;
                result = kEmptyLine;
                if (cut[-1] == ' ') --cut;
                newline = true;
                break;
            } else {
                if (c == -0x6e) c = 0x27;
                after = line + n;
                const CHARDATA_tag* g = find_glyph(Font, uint32_t(c & 0xff));
                if (g != nullptr) {
                    const float w = float(g->w) * sx + 1.0f;
                    const float kern = Font_GetKernAdjust(char(c), char(prev));
                    extent = extent + kern * sx;
                    extent = extent + float(g->pre) * sx;
                    extent = extent + float(g->post) * sx;
                    extent = extent + w;
                }
            }
            prev = c;
            if (*after == 0) break;
        }
        if (!overflow && !newline) {
            result = nullptr;
            cut = line + n;
        }
    }
    if (begin == cut && n != 0 && limit <= extent) cut = begin + n - 1;
    *text = *cut == 0 ? nullptr : cut;
    if (begin == cut) return result;
    result = Txt_GetStringFromHeap(0);
    uint32_t length = uint32_t(cut - begin) & 0xffff;
    if (length >= uint32_t(kStringHeapSize)) {
        ++g_font_guards.long_line;
        length = kStringHeapSize - 1;
    }
    amemcpy(result, begin, length);
    result[length] = 0;
    return result;
}

// [decomp] 0x001ef588 Font_GetTextExtent
// {largura, altura} de até "count" caracteres (-1: todos), com o formato e a escala; 0 sem texto ou
// sem destino.
uint32_t Font_GetTextExtent(char* text, char* format, GPOINT* extent, GPOINT* scale, int32_t count) {
    if (text == nullptr || extent == nullptr) return 0;
    GPOINT s{scale != nullptr ? scale->x : 1.0f, scale != nullptr ? scale->y : 1.0f};
    uint32_t flags = 0;
    Font_ParseFormat(format, flags, s);
    const GPOINT* r = __Font_DrawText(0.0f, 0.0f, 0, 0, 0, text, format, &s, 1, count, -1.0f, nullptr);
    amemcpy(extent, r, sizeof(GPOINT));
    return 1;
}

// [decomp] 0x001ef668 Font_GetTextCountExtent
// Quantos caracteres do texto cabem em "width" pixels.
uint32_t Font_GetTextCountExtent(char* text, char* format, GPOINT* scale, float width) {
    if (text == nullptr) return 0;
    uint32_t fit = 0;
    GPOINT s{scale != nullptr ? scale->x : 1.0f, scale != nullptr ? scale->y : 1.0f};
    uint32_t flags = 0;
    Font_ParseFormat(format, flags, s);
    __Font_DrawText(0.0f, 0.0f, 0, 0, 0, text, format, &s, 1, -1, width, &fit);
    return fit;
}

// [decomp] 0x001ef728 Font_GetAlignment
// O parâmetro do primeiro código de alinhamento (0xfe n) do formato; 1 (à esquerda) se não houver.
uint8_t Font_GetAlignment(char* format) {
    if (format == nullptr) return 1;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(format);
    while (*p != 0) {
        const uint8_t* next = p + 1;
        switch (int8_t(*p + 6)) {
            case 0: next = p + 5; break;
            case 1:
            case 2:
            case 5: next = p + 2; break;
            case 4: return p[1];
            default: break;
        }
        p = next;
    }
    return 1;
}
