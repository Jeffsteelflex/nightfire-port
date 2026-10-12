// Port do módulo Sprite do ACTION.ELF (0x001de3d8-0x001deaa4), de hashtable_set_sprite e das funções de
// desenho de sprites de View (View_DrawSprites, View_AddSprite, View_3DPoint2Screen). Ver sprite.h.
//
// Divergências:
// - View_DrawSprites: SPRF_TILE com textura de largura ou altura 0 desenha uma vez (o original dividiria
//   por zero: break 7); conta em g_sprite_guards.zero_tile.
// - Sprite_BuildList para em kSprActive sprites (o original escreveria além de SprActiveList; com 512 nós
//   na SpriteList isso não acontece).
// - Sprite_SetText/SetFmt/Create2 não medem um texto cujo formato ainda é 0xfcfcfcfc (o valor de um sprite
//   novo): o original leria esse endereço em Font_ParseFormat.
// Só no PC: as origens dos desenhos (SprOriginList, g_sprite_texture_hash) e g_psi_2d_origins antes de
// cada psiDrawSprites.

#include "engine/sprite.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/map_types.h"
#include "engine/sort.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"

DLISTINFO_tag SpriteList;
sprite* SprActiveList[kSprActive];
SPRITE_DRAW SprBuffList[kSprBuff];
Psi2DOrigin SprOriginList[kSprBuff];
int32_t SprCnt = 0;
SpriteGuards g_sprite_guards;

namespace {

char* const kNoFormat = reinterpret_cast<char*>(uintptr_t(0xfcfcfcfcu));
// Só no PC: o hashcode da textura que hashtable_set_sprite pôs em cada sprite (para a origem do desenho).
std::unordered_map<const sprite*, uint32_t> g_sprite_texture_hash;
// E o de cada registro de textura (sprite::texture) que ela já viu: um sprite que recebeu a textura por
// cópia (Label_Update copia o modelo M_LABEL::text para o sprite desenhado) ainda é achado por ele.
std::unordered_map<uint32_t, uint32_t> g_texture_rec_hash;

// O que Sprite_Create, Sprite_CreateLink e Sprite_Create2 fazem com um nó novo.
void init_sprite(sprite* s) {
    s->text = nullptr;
    s->length = 0;
    s->format = kNoFormat;
    s->scale.x = 1.0f;
    s->created = GameState.frame;
    s->x = 0;
    s->y = 0;
    s->u = 0;
    s->v = 0;
    s->uw = 0;
    s->vh = 0;
    s->scale.y = 1.0f;
}

// Font_GetTextExtent do texto do sprite, com a escala dele: uw e vh recebem a largura e a altura.
void measure(sprite* s, char* text, char* format) {
    if (format == kNoFormat) {
        ++g_sprite_guards.no_format;
        return;
    }
    GPOINT extent{};
    Font_GetTextExtent(text, format, &extent, &s->scale, -1);
    s->uw = int16_t(int32_t(extent.x));
    s->vh = int16_t(int32_t(extent.y));
}

// Descarrega SprBuffList (psiDrawSprites), com as origens (só no PC).
void flush_sprites() {
    g_psi_2d_origins = SprOriginList;
    psiDrawSprites(SprBuffList, int16_t(SprCnt));
    SprCnt = 0;
}

sprite* new_sprite() { return static_cast<sprite*>(DList_MoveFromFree2InUse(&SpriteList)); }

}  // namespace

void sprite_debug_power_on() {
    std::memset(&SpriteList, 0, sizeof(SpriteList));
    std::memset(SprActiveList, 0, sizeof(SprActiveList));
    std::memset(SprBuffList, 0, sizeof(SprBuffList));
    for (Psi2DOrigin& o : SprOriginList) o = Psi2DOrigin{};
    SprCnt = 0;
    g_sprite_guards = {};
    g_sprite_texture_hash.clear();
    g_texture_rec_hash.clear();
}

uint32_t sprite_texture_hash(const sprite* s) {
    const auto it = g_sprite_texture_hash.find(s);
    if (it != g_sprite_texture_hash.end()) return it->second;
    if (s == nullptr || s->texture == 0) return 0;
    const auto r = g_texture_rec_hash.find(s->texture);
    return r == g_texture_rec_hash.end() ? 0 : r->second;
}

Psi2DOrigin* View_SpriteOrigin(const SPRITE_DRAW* entry) {
    const ptrdiff_t i = entry - SprBuffList;
    static Psi2DOrigin scratch;  // só por segurança: uma entrada fora de SprBuffList
    if (i < 0 || i >= kSprBuff) return &scratch;
    return &SprOriginList[i];
}

// [decomp] 0x001ca3d8 hashtable_set_sprite
// A textura do hashcode (a busca de hashtable_getitem): o registro dela e o tamanho, que vira também o
// tamanho na tela e o recorte inteiro.
uint32_t hashtable_set_sprite(sprite* s, uint32_t hash) {
    const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(hash));
    if (t == nullptr) return 0;
    s->texture = uint32_t(reinterpret_cast<uintptr_t>(t->rec));
    s->tex_w = uint16_t(t->width);
    s->tex_h = uint16_t(t->height);
    s->w = t->width;
    s->h = t->height;
    s->uw = t->width;
    s->vh = t->height;
    g_sprite_texture_hash[s] = hash;
    g_texture_rec_hash[s->texture] = hash;
    return 1;
}

// [decomp] 0x001de3d8 Sprite_Create2
// Um sprite descrito por "info": com rótulo de texto, o texto (Txt_BindLabel, travado no StringHeap) com o
// formato e a medida dele; senão a imagem da textura "hash", com tamanho e recorte (0 = o da textura).
// Comportamento original preservado: no texto, tex_w e tex_h recebem a posição.
sprite* Sprite_Create2(SpriteInfo* info) {
    sprite* s = new_sprite();
    if (s != nullptr) {
        s->text = nullptr;
        s->length = 0;
        s->format = kNoFormat;
        s->scale.x = 1.0f;
        s->created = GameState.frame;
        s->x = 0;
        s->y = 0;
        s->u = 0;
        s->v = 0;
        s->uw = 0;
        s->vh = 0;
        s->scale.y = 1.0f;
    }
    if (s == nullptr || info == nullptr) return s;
    if (info->label != 0xffffffffu) {
        s->priority = int8_t(info->priority);
        s->tex_w = uint16_t(info->x);
        s->tex_h = uint16_t(info->y);
        s->colour = info->colour;
        s->x = info->x;
        s->y = info->y;
        s->flags = 0;
        char* text = Txt_BindLabel(info->label, 0);
        char* format = info->format;
        s->text = text;
        s->format = format;
        if (text != nullptr && format != nullptr) {
            s->length = uint16_t(std::strlen(text));
            measure(s, text, format);
        }
        Txt_LockString(s->text);
    } else {
        if (info->hash != 0) hashtable_set_sprite(s, info->hash);
        s->x = info->x;
        s->y = info->y;
        s->w = info->w != 0 ? info->w : int16_t(s->tex_w);
        s->h = info->h != 0 ? info->h : int16_t(s->tex_h);
        s->u = info->u;
        s->v = info->v;
        s->uw = info->uw != 0 ? info->uw : int16_t(s->tex_w - 1);
        s->vh = info->vh != 0 ? info->vh : int16_t(s->tex_h - 1);
    }
    s->colour = info->colour;
    s->colour2 = info->colour2;
    s->priority = int8_t(info->priority);
    s->flags = info->flags;
    if (info->viewer >= 0) Sprite_Link2Viewer(s, uint16_t(int16_t(info->viewer)));
    return s;
}

// [decomp] 0x001de600 Sprite_InitLists
// 512 nós de 0x44 bytes (a cada fase; o Mem_Init da carga apagou os anteriores).
void Sprite_InitLists() {
    DList_Init(&SpriteList, 0x44, kSpriteCount);
    g_sprite_texture_hash.clear();
    g_texture_rec_hash.clear();
}

// [decomp] 0x001de628 Sprite_SetFmt
void Sprite_SetFmt(sprite* s, char* format, uint16_t) {
    s->format = format;
    if (s->text != nullptr) measure(s, s->text, format);
}

// [decomp] 0x001de698 Sprite_SetText
// Troca o texto (o formato continua) e mede de novo.
void Sprite_SetText(sprite* s, char* text, uint16_t) {
    if (s == nullptr || text == nullptr) return;
    s->length = 0;
    s->text = text;
    s->length = uint16_t(std::strlen(text));
    if (s->text != nullptr) measure(s, s->text, s->format);
}

// [decomp] 0x001de718 Sprite_SetTextNFmt
void Sprite_SetTextNFmt(sprite* s, char* text, char* format, uint16_t) {
    s->text = text;
    s->format = format;
    if (text == nullptr || format == nullptr) return;
    s->length = uint16_t(std::strlen(text));
    measure(s, text, format);
}

// [decomp] 0x001de7b0 Sprite_SetParams
// Prioridade, posição, cor e (se hash não for 0) a textura; as flags voltam a 0. Comportamento original
// preservado: tex_w e tex_h também recebem a posição (a textura, se houver, os troca depois).
void Sprite_SetParams(sprite* s, int16_t priority, int16_t x, int16_t y, uint32_t hash, uint32_t colour) {
    if (s == nullptr) return;
    s->priority = int8_t(priority);
    s->tex_w = uint16_t(x);
    s->tex_h = uint16_t(y);
    s->colour = colour;
    s->x = x;
    s->y = y;
    s->flags = 0;
    if (hash != 0) hashtable_set_sprite(s, hash);
}

// [decomp] 0x001de808 Sprite_Create
sprite* Sprite_Create() {
    sprite* s = new_sprite();
    if (s != nullptr) init_sprite(s);
    return s;
}

// [decomp] 0x001de880 Sprite_CreateLink
sprite* Sprite_CreateLink(uint16_t viewer) {
    sprite* s = new_sprite();
    if (s != nullptr) {
        init_sprite(s);
        Sprite_Link2Viewer(s, viewer);
    }
    return s;
}

// [decomp] 0x001de918 Sprite_Link2Viewer
void Sprite_Link2Viewer(sprite* s, uint16_t viewer) {
    if (s != nullptr) s->viewer = uint8_t(viewer);
}

// [decomp] 0x001de930 Sprite_Delete
void Sprite_Delete(sprite* s) {
    if (s == nullptr) return;
    Txt_UnlockString(s->text);
    DList_RemoveFromInUse2Free(&SpriteList, &s->node);
    g_sprite_texture_hash.erase(s);
}

// [decomp] 0x001de970 Sprite_BuildList
// Os sprites da câmera a desenhar (prioridade diferente de -1, alfa diferente de 0, texto ou textura),
// com os 4 bits baixos das flags zerados, em ordem decrescente de prioridade (QuickSort COMP_SPR).
// count_a = 0; count_c = quantos; count_b = o índice do último sprite de fundo (prioridade >= 50), ou 0.
// Comportamento original preservado: count_b é um a menos que o número de sprites de fundo.
sprite** Sprite_BuildList(uint16_t camera, uint32_t* count_a, uint32_t* count_b, uint32_t* count_c) {
    uint32_t n = 0;
    for (LLNODE_tag* node = SpriteList.used.first; node != nullptr; node = node->next) {
        sprite* s = reinterpret_cast<sprite*>(node);
        if (s->priority == -1 || s->viewer != camera || uint8_t(s->colour) == 0) continue;
        if (s->text == nullptr && s->texture == 0) continue;
        if (n >= uint32_t(kSprActive)) {
            ++g_sprite_guards.active_full;
            break;
        }
        s->flags &= 0xfff0;
        SprActiveList[n++] = s;
    }
    *count_a = 0;
    *count_b = 0;
    *count_c = n;
    if (n != 0) {
        QuickSort(SprActiveList, n, 4, COMP_SPR, nullptr);
        if (SprActiveList[0]->priority >= 0x32) {
            *count_b = 0;
            for (uint32_t i = 1; i < n && SprActiveList[i]->priority >= 0x32; ++i) *count_b = i;
        }
    }
    return SprActiveList;
}

// [decomp] 0x001e5290 View_DrawSprites
// Desenha os sprites list[first .. end) da câmera (se ela desenha sprites), sem tweak, luzes nem Z
// (psiStartSprites). Textos: Font_DrawText na posição + o canto da câmera, com a cor, a cor da sombra
// (colour2) e branco no realce; SPRF_OUTLINE vira o contorno (0x4000), SPRF_SHADOW a sombra (0x1000),
// SPRF_HIGHLIGHT o realce (0x2000); a largura medida (uw) alinha. Imagens com alfa: um SPRITE_DRAW por
// ladrilho (SPRF_TILE: (uw + 1) / largura da textura colunas e (vh + 1) / altura linhas, cada uma com
// metade do tamanho e do recorte por ladrilho; colunas e linhas depois da primeira saem espelhadas),
// SPRF_CENTRE centra; SPRF_SHADOW põe antes uma cópia preta e desloca a imagem de
// clamp(tamanho / 64, 1, 5) pixels. y começa em pixel inteiro. SprBuffList descarrega a cada 63.
// Comportamento original preservado: o espelhamento usa u = uw, não u + uw.
void View_DrawSprites(viewer_tag* viewer, sprite** list, uint32_t first, uint32_t end) {
    if (viewer->sprites == 0) return;
    psiSetTweakARGB(0xff, 0xff, 0xff, 0xff);
    psiSetUpColourBlend(2);
    psiLight_SetLights(nullptr, 0, 0.0f, 0.0f, 0.0f);
    psiStartSprites();
    for (uint32_t index = first; index < end; ++index) {
        sprite* s = list[index];
        if (s->text != nullptr) {
            uint32_t flags = (s->flags & SPRF_OUTLINE) != 0 ? 0x4000 : 0;
            if ((s->flags & SPRF_SHADOW) != 0) flags |= 0x1000;
            if ((s->flags & SPRF_HIGHLIGHT) != 0) flags |= 0x2000;
            g_font_origin_sprite = s;
            Font_DrawText(viewer->viewport[0] + float(s->x), viewer->viewport[2] + float(s->y), s->colour, s->colour2,
                          0xffffffffu, s->text, s->format, &s->scale, flags, s->length, float(s->uw));
            g_font_origin_sprite = nullptr;
            continue;
        }
        const uint32_t colour = s->colour;
        const uint8_t r = uint8_t(colour >> 24), g = uint8_t(colour >> 16), b = uint8_t(colour >> 8);
        const uint8_t a = uint8_t(colour);
        if (a == 0) continue;
        uint32_t cols = 1, rows = 1;
        if ((s->flags & SPRF_TILE) != 0) {
            if (s->tex_w == 0 || s->tex_h == 0) {
                ++g_sprite_guards.zero_tile;
            } else {
                cols = uint32_t((int32_t(s->uw) + 1) / int32_t(s->tex_w)) & 0xff;
                rows = uint32_t((int32_t(s->vh) + 1) / int32_t(s->tex_h)) & 0xff;
            }
        }
        float x = float(s->x);
        float y = float(s->y);
        const uint32_t cs = (cols - 1) & 0x1f, rs = (rows - 1) & 0x1f;
        const float w = float(int32_t(s->w) >> cs);
        const float h = float(int32_t(s->h) >> rs);
        const int16_t u = s->u, v = s->v;
        const int16_t uw = int16_t(int32_t(s->uw) >> cs);
        const int16_t vh = int16_t(int32_t(s->vh) >> rs);
        if ((s->flags & SPRF_CENTRE) != 0) {
            x = x - float(int16_t(s->w) >> 1);
            y = y - float(int16_t(s->h) >> 1);
        }
        const int16_t y_start = int16_t(int32_t(y));
        uint32_t flip_u = s->flags & SPRF_FLIP_U;
        uint32_t flip_v = s->flags & SPRF_FLIP_V;
        const uint32_t hash = sprite_texture_hash(s);
        for (uint32_t col = 0; col < cols; ++col) {
            y = float(y_start);
            const uint32_t next_flip_u = flip_u ^ 1;
            for (uint32_t row = 0; row < rows; ++row) {
                SPRITE_DRAW* shadow = nullptr;
                if ((s->flags & SPRF_SHADOW) != 0) {
                    if (0x3e <= uint32_t(SprCnt)) flush_sprites();
                    shadow = &SprBuffList[SprCnt++];
                }
                if (0x3f <= uint32_t(SprCnt)) flush_sprites();
                SPRITE_DRAW* d = &SprBuffList[SprCnt++];
                d->unknown_08 = s->created;
                d->texture = s->texture;
                d->flags = s->flags;
                d->inv_h = 1.0f / float(s->tex_h);
                d->r = r;
                d->g = g;
                d->b = b;
                d->a = a;
                d->inv_w = 1.0f / float(s->tex_w);
                d->x = x + viewer->viewport[0];
                d->w = w;
                d->h = h;
                d->y = y + viewer->viewport[2];
                d->u = flip_u != 0 ? uw : u;
                d->v = flip_v != 0 ? vh : v;
                d->uw = flip_u != 0 ? int16_t(-uw) : uw;
                d->vh = flip_v != 0 ? int16_t(-vh) : vh;
                Psi2DOrigin& o = *View_SpriteOrigin(d);
                o = Psi2DOrigin{};
                o.kind = Psi2DKind::Image;
                o.sprite = s;
                o.texture_hash = hash;
                if (shadow != nullptr) {
                    amemcpy(shadow, d, sizeof(SPRITE_DRAW));
                    shadow->r = 0;
                    shadow->b = 0;
                    shadow->g = 0;
                    Psi2DOrigin& so = *View_SpriteOrigin(shadow);
                    so = o;
                    so.pass = Psi2DPass::Shadow;
                    const float dx = d->w * 0.015625f;
                    d->x = d->x + (1.0f <= dx ? std::min(dx, 5.0f) : 1.0f);
                    const float dy = d->h * 0.015625f;
                    d->y = d->y + (1.0f <= dy ? std::min(dy, 5.0f) : 1.0f);
                }
                flip_v ^= 1;
                y = y + h;
            }
            x = x + w;
            flip_u = next_flip_u;
        }
    }
    if (SprCnt != 0) flush_sprites();
    psiEndSprites();
}

// [decomp] 0x001e7848 View_AddSprite
// A próxima entrada de SprBuffList, descarregando antes se sobrarem menos de "reserve" + 1 livres.
SPRITE_DRAW* View_AddSprite(uint16_t reserve) {
    if (uint32_t(0x3f - reserve) <= uint32_t(SprCnt)) flush_sprites();
    return &SprBuffList[SprCnt++];
}
