// O HUD. Ver hud.h.
//
// Divergências (comentadas também em cada função):
// - HUD_CreateDefault pula um sprite que Sprite_Create2 não criou (SpriteList cheia).
// - Guardas contra nulo onde o original leria ou escreveria por um ponteiro nulo: um rótulo sem
//   texto (Txt_BindLabel nulo) num sprintf ou strcmp, o sprite de objetivo numa mensagem de tipo 2 fora da
//   pane de objetivos, a faixa (sprite 2) ausente de uma pane de mensagem, glb_viewer[4]/[5]
//   ausentes (testes); e contra índices fora das tabelas: o ícone de toque além dos 8, o quadro da
//   mira além dos 9 de HUDCrossCoords, o tipo de munição além dos 33 de BulletImg.189, os nomes do
//   radar além das palavras extra; a porcentagem de munição com pente 0 mostra 0 (o original para
//   numa divisão por zero).
// - HUD_UpdateAmmoPane lê os sprites 5, 6 e 7 só quando a pane os tem (o original lê além do bloco
//   da pane e só usa o valor quando a pane os tem: o efeito é o mesmo).
// - Só do port: HUD_UpdateCrossHair esconde a mira na pose de mira moderna (engine/player_ads.h).

#include "engine/hud.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/anim.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/input.h"
#include "engine/lights.h"
#include "engine/player_ads.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/objects.h"
#include "engine/bullet.h"
#include "engine/drone.h"
#include "engine/font.h"
#include "engine/script.h"
#include "engine/sprite.h"
#include "engine/text.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"
#include "platform/pending.h"

HUDPANECREATE_tag* PaneList[kHudPanes];
HUDPANECREATE_tag* MPPaneList[kHudPanes];
SpriteInfo CrossHair;
HUDPANECREATE_tag* HudMsgMissionStatusPane = nullptr;
HUDPANECREATE_tag* HudMsgObjectiveStatusPane = nullptr;
int16_t HUDCrossCoords[9][9];
float BulletImg_189[33][4];
uint8_t TexUV_291[0xa0];
HUDPANECREATE_tag HudPanePool[kHudPanePool];
SpriteInfo HudSprInfoPool[kHudSprInfoPool];
char HudFormatPool[kHudFormatPool][16];

int16_t ttimer = 0;
int16_t ctimer = 0;
int16_t ThirdIconTimer = 0;
int16_t CrouchIconTimer = 0;
int16_t OICW_timer = 0;
int16_t OICW_mode = 0;
uint8_t FuckOffSprite3 = 0;

namespace {

// Estáticas locais do original.
uint8_t Scrl_247 = 0;   // HUD_UpdateNightSightPane
uint8_t Scrl_272 = 0;   // HUD_UpdateRedeemerPane
uint8_t IsNew_224 = 0;  // HUD_UpdateStatusPane: a mensagem de objetivo é um objetivo novo
uint8_t IsComplete_225 = 0;
uint32_t ObjectiveSFXTriggered1_220 = 0;
uint32_t ObjectiveSFXTriggered2_221 = 0;
uint32_t ObjectiveSFXPlayMe1st_222 = 0;
uint32_t ObjectiveSFXPlayMe2nd_223 = 0;
MATRIX InvM_292;        // HUD_RadarUpdate
_VECTOR Target_293;

constexpr uint32_t kHudTag = 0x2704;  // etiqueta de memória do HUD (Mem_Malloc)
constexpr uint32_t kStringSize = 0x168;  // tamanho de uma string do heap do Txt_*

// Objeto do radar do multijogador (RADAROBJ, 0x20 bytes; MP_GetRadarObjects).
struct RadarObj {
    _VECTOR pos;      // 0x00
    uint32_t colour;  // 0x10
    uint16_t icon;    // 0x14 índice em TexUV.291
    uint8_t pad[10];
};
static_assert(sizeof(RadarObj) == 0x20);

HUDINFO_tag* hud_of(const BLData* b) { return static_cast<HUDINFO_tag*>(b->hud); }
viewer_tag* viewer_of(const BLData* b) { return glb_viewer[b->player]; }

template <class T>
T rd(const uint8_t* base, uint32_t offset) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    T v;
    std::memcpy(&v, base + offset, sizeof(T));
    return v;
}
template <class T>
T rd(const void* base, uint32_t offset) {
    return rd<T>(static_cast<const uint8_t*>(base), offset);
}

uint32_t mp_on() { return rd<uint32_t>(MPSettings, 0x180); }
uint32_t mp_u32(uint32_t offset) { return rd<uint32_t>(MPSettings, offset); }

void set_alpha(sprite* s, uint32_t a) { s->colour = (s->colour & 0xffffff00u) | (a & 0xffu); }

// Os 9 campos de HUDCrossCoords de um quadro da mira.
const int16_t* cross_coords(uint32_t frame) { return HUDCrossCoords[frame < 9 ? frame : 0]; }

// sprintf que aceita um formato nulo (um rótulo sem texto): escreve texto vazio.
void format_label(char* out, const char* fmt) {
    if (out == nullptr) return;
    if (fmt == nullptr) {
        out[0] = 0;
        return;
    }
    std::snprintf(out, kStringSize, "%s", fmt);
}

// O texto de um sprite para escrever (nulo quando o sprite não tem texto).
char* text_of(sprite* s) { return s != nullptr ? s->text : nullptr; }

// Desliga a pane (estado 0) se ela existe: o bloco que HUD_UpdateCrossHair e HUD_MonitorNightSight
// repetem para cada pane.
void pane_off(HUDINFO_tag* hud, uint32_t index) {
    if (hud != nullptr && hud->panes[index].valid != 0) {
        hud->panes[index].state = 0;
        hud->panes[index].enabled = 0;
    }
}
void pane_on(HUDINFO_tag* hud, uint32_t index) {
    if (hud != nullptr && hud->panes[index].valid != 0) {
        hud->panes[index].state = 0;
        hud->panes[index].enabled = 1;
    }
}

// HUD_CreateShrink, HUD_CreateOICWPane e HUD_CreateRedeemer: numa tela menor que a do PS2 (512x448)
// cada sprite fica com a metade do tamanho, recuado metade da posição.
void shrink_sprites(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create) {
    const viewer_tag* v = viewer_of(b);
    for (uint32_t i = 0; i < pane->count; i = uint16_t(i + 1)) {
        sprite* s = pane->sprites[i];
        if (v->width < 512.0f) {
            s->w = int16_t(s->w >> 1);
            s->x = int16_t(s->x - (create->sprites[i].x >> 1));
        }
        if (v->height < 448.0f) {
            s->h = int16_t(s->h >> 1);
            s->y = int16_t(s->y - (create->sprites[i].y >> 1));
        }
    }
}

// HUD_CreateMPHealthPane, HUD_CreateMPScorePane e HUD_CreateRadar: o mesmo recuo, sem mudar o tamanho.
void shift_sprites(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create) {
    const viewer_tag* v = viewer_of(b);
    for (uint32_t i = 0; i < pane->count; i = uint16_t(i + 1)) {
        sprite* s = pane->sprites[i];
        if (v->width < 512.0f) s->x = int16_t(s->x - (create->sprites[i].x >> 1));
        if (v->height < 448.0f) s->y = int16_t(s->y - (create->sprites[i].y >> 1));
    }
}

// u1 e w de uma barra pela carga (0 a 1): 28 a 228 pixels, no mínimo 1.
void set_bar(sprite* s, float fraction) {
    const int32_t v = int32_t(fraction * 200.0f + 28.0f);
    const int16_t w = int16_t(v);
    s->uw = int16_t(v) < 1 ? int16_t(1) : w;
    s->w = int16_t(v) < 1 ? int16_t(1) : w;
}

// glb_viewer[4] (câmera de cena) ligada. Só no port: sem a câmera (testes) conta como desligada.
bool scene_camera_active() { return glb_viewer[4] != nullptr && glb_viewer[4]->active != 0; }

// Pisca no ritmo do jogo: verdadeiro na primeira metade de cada meio segundo.
bool blink_on() {
    const int32_t half = int32_t(FRAME_RATE_INT >> 1);
    if (half == 0) return false;  // o original para (divisão por zero)
    return uint32_t(int32_t(GameState.frame) % half) < (FRAME_RATE_INT >> 2);
}

}  // namespace

// [decomp] 0x00199b70 HUD_CreateDefault
// Cria os sprites da pane a partir da descrição: o bloco de ponteiros (sprites e as palavras extra,
// zeradas), a posição validada na tela e cada sprite deslocado pela pane; os de flag 0x1000 são
// recortados na tela.
void HUD_CreateDefault(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag*) {
    pane->count = create->count;
    auto* block = static_cast<uint8_t*>(Mem_Malloc((uint32_t(pane->count) + create->extra) * 4, kHudTag, 0));
    pane->sprites = reinterpret_cast<P32<sprite>*>(block);
    pane->extra = reinterpret_cast<uint32_t*>(block + uint32_t(pane->count) * 4);
    for (uint32_t i = 0; i < create->extra; i = uint16_t(i + 1)) pane->extra[i] = 0;
    pane->update = create->update;
    pane->x = create->x;
    pane->y = create->y;
    pane->w = create->w;
    pane->h = create->h;
    const viewer_tag* v = viewer_of(b);
    const float x0 = v->viewport[0], x1 = v->viewport[1], y0 = v->viewport[2], y1 = v->viewport[3];
    HUD_ValidateXY(&pane->x, &pane->y, create->w, create->h, uint16_t(int16_t(b->player)), create->flags);
    for (uint32_t i = 0; i < pane->count; i = uint16_t(i + 1)) {
        SpriteInfo* info = &create->sprites[i];
        sprite* s = Sprite_Create2(info);
        pane->sprites[i] = s;
        if (s == nullptr) continue;  // só no port: SpriteList cheia (o original escreveria por nulo)
        s->x = int16_t(s->x + pane->x);
        s->y = int16_t(s->y + pane->y);
        if (info->label != 0xffffffffu) {
            s->tex_w = uint16_t(s->x);
            s->tex_h = uint16_t(s->y);
        }
        s->priority = int8_t(info->priority);
        if (info->label == 0) Txt_LockString(s->text);
        if ((info->flags & 0x1000) != 0) {
            if (x1 + x0 < float(int32_t(s->x) + int32_t(s->w))) s->w = int16_t(int32_t((x1 + x0) - float(s->x)));
            if (y1 + y0 < float(int32_t(s->y) + int32_t(s->h))) s->h = int16_t(int32_t((y1 + y0) - float(s->y)));
            if (float(s->x) < x0) {
                s->w = int16_t(int32_t(float(s->w) - (x0 - float(s->x))));
                s->x = int16_t(int32_t(x0));
            }
        }
    }
}

// [decomp] 0x00199e88 HUD_Init
// Cria o HUD do jogador (não dos personagens, tipos 2 e 0x11): a mira e cada pane da lista do modo
// de jogo (criação e uma atualização), depois desliga as panes especiais (HUD_Reset).
void HUD_Init(BLData* b, obj_tag* obj) {
    if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) return;
    auto* hud = static_cast<HUDINFO_tag*>(Mem_Malloc(sizeof(HUDINFO_tag), kHudTag, 0));
    b->hud = hud;
    if (hud == nullptr) return;
    std::memset(hud->panes, 0, sizeof(hud->panes));
    hud->unknown_004 = 0;
    hud->crosshair = nullptr;
    ttimer = 0;
    ctimer = 0;
    hud->crosshair = Sprite_Create2(&CrossHair);
    if (hud->crosshair != nullptr) Sprite_Link2Viewer(hud->crosshair, uint16_t(int16_t(b->player)));
    HUDPANECREATE_tag** list = mp_on() == 0 ? PaneList : MPPaneList;
    for (uint32_t i = 0; i < kHudPanes; i = uint16_t(i + 1)) {
        HUDPANECREATE_tag* create = list[i];
        HUDPANE_tag* pane = &hud_of(b)->panes[i];
        if (create == nullptr || create->create == nullptr) {
            pane->enabled = 0;
            pane->valid = 0;
            continue;
        }
        pane->valid = 1;
        pane->enabled = 1;
        pane->unknown_1d = 0;
        pane->unknown_1e = 0;
        pane->create = create;
        create->create(b, pane, create, obj);
        pane = &hud_of(b)->panes[i];
        if (pane->update != nullptr) pane->update(b, pane, obj);
    }
    HUD_Reset(b);
}

// [decomp] 0x0019a088 HUD_Update
// Uma vez por tick: a mira, a visão noturna e cada pane (ligada: os sprites voltam ao modo da
// descrição; desligada: escondidos), com a atualização de cada uma.
void HUD_Update(BLData* b, obj_tag* obj) {
    if (b == nullptr || b->hud == nullptr || obj == nullptr) return;
    HUDINFO_tag* hud = hud_of(b);
    HUD_UpdateCrossHair(b, hud->crosshair);
    HUD_MonitorNightSight(b);
    for (uint32_t i = 0; i < kHudPanes; i = uint16_t(i + 1)) {
        HUDPANE_tag* pane = &hud->panes[i];
        for (uint32_t k = 0; k < pane->count; k = uint16_t(k + 1)) {
            sprite* s = pane->sprites[k];
            if (s == nullptr) continue;
            s->priority = pane->enabled == 0 ? int8_t(-1) : int8_t(pane->create->sprites[k].priority);
        }
        if (pane->update != nullptr) pane->update(b, &hud_of(b)->panes[i], obj);
    }
}

// [decomp] 0x0019a250 HUD_ValidateXY
// Prende uma pane w x h dentro da área da câmera do jogador, 16 pixels para dentro das bordas
// (as bordas internas da tela dividida não contam). flags: 8 prende x, 0x10 prende y (posições
// abaixo da margem são somadas a ela); 0x80 e 0x100 somam a origem da câmera.
void HUD_ValidateXY(int16_t* x, int16_t* y, uint16_t w, uint16_t h, uint16_t player, uint16_t flags) {
    const viewer_tag* v = glb_viewer[player];
    const float y0 = v->viewport[2];
    const float width = v->width;
    const float height = v->height;
    const float x0 = v->viewport[0];
    uint16_t players = 1;
    if (mp_on() != 0) players = rd<uint16_t>(MPSettings, 0x1ac);
    uint16_t min_y = uint16_t(int32_t(y0 + 16.0f));
    uint16_t min_x = uint16_t(int32_t(x0 + 16.0f));
    uint16_t max_x = uint16_t(int32_t(float(uint16_t(int32_t(width - 16.0f))) + x0));
    uint16_t max_y = uint16_t(int32_t(float(uint16_t(int32_t(height - 16.0f))) + y0));
    if (players == 2) {
        if (DrawInfo[0] == 1) {
            if (player == 0) {
                max_x = uint16_t(int32_t(width));
            } else if (player == 1) {
                min_x = uint16_t(int32_t(x0));
            }
        } else if (player == 0) {
            max_y = uint16_t(int32_t(height));
        } else if (player == 1) {
            min_y = uint16_t(int32_t(y0));
        }
    } else if (2 < players && players < 5) {
        if (player == 1) {
            max_y = uint16_t(int32_t(height));
            min_x = uint16_t(int32_t(x0));
        } else if (player == 0) {
            max_x = uint16_t(int32_t(width));
            max_y = uint16_t(int32_t(height));
        } else if (player == 2) {
            min_y = uint16_t(int32_t(y0));
            max_x = uint16_t(int32_t(width));
        } else if (player == 3) {
            min_y = uint16_t(int32_t(y0));
            min_x = uint16_t(int32_t(x0));
        }
    }
    const int32_t px = *x;
    const int16_t py = *y;
    if ((flags & 8) != 0) {
        if (float(max_x) < float(px + int32_t(w))) *x = int16_t(max_x - w);
        if (*x < int32_t(min_x)) *x = int16_t(*x + min_x);
    }
    if ((flags & 0x10) != 0) {
        if (float(max_y) < float(int32_t(py) + int32_t(h))) *y = int16_t(max_y - h);
        if (*y < int32_t(min_y)) *y = int16_t(*y + min_y);
    }
    if ((flags & 0x80) != 0) *x = int16_t(int32_t(float(*x) + x0));
    if ((flags & 0x100) != 0) *y = int16_t(int32_t(float(*y) + y0));
}

// [decomp] 0x0019a598 HUD_CalcWidthHeight
// Largura e altura da caixa (que sempre inclui a origem) dos sprites sem a flag 0x1000.
void HUD_CalcWidthHeight(HUDPANECREATE_tag* create, int16_t* w, int16_t* h) {
    if (create == nullptr) return;
    int16_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    for (int32_t i = 0; i < int32_t(create->count); ++i) {
        const SpriteInfo& s = create->sprites[i];
        if ((s.flags & 0x1000) != 0) continue;
        if (s.x < min_x) min_x = s.x;
        if (s.y < min_y) min_y = s.y;
        int32_t right = int32_t(s.x) + int32_t(s.w);
        int32_t bottom = int32_t(s.y) + int32_t(s.h);
        if (right < max_x) right = max_x;
        if (bottom < max_y) bottom = max_y;
        max_x = int16_t(right);
        max_y = int16_t(bottom);
    }
    if (w != nullptr) *w = int16_t(max_x - min_x);
    if (h != nullptr) *h = int16_t(max_y - min_y);
}

// [decomp] 0x0019a6a8 HUD_UpdateBloodPane
// A cortina de sangue da morte: o escorrido (sprite 0) desce FRAME_RATE_MUL pixels por tick
// puxando o fundo (sprite 1); quando chega ao pé da tela a pane fica no estado 1 (HUD_State), que
// Player_HandleDeath espera. Desligada, volta ao topo.
void HUD_UpdateBloodPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    const float mul = FRAME_RATE_MUL;
    if (b == nullptr || obj == nullptr || pane == nullptr) return;
    sprite* back = pane->sprites[1];
    sprite* drip = pane->sprites[0];
    const viewer_tag* v = viewer_of(b);
    if (pane->enabled == 0) {
        drip->y = int16_t(int32_t(v->viewport[2]));
        back->h = 0;
        drip->h = 0;
        return;
    }
    const float done = float(int16_t(int32_t(float(drip->y) - v->viewport[2])));
    if (v->height <= done) {
        pane->state = 1;
        return;
    }
    const int16_t next = int16_t(int32_t(done + FRAME_RATE_MUL));
    drip->y = int16_t(int32_t(float(drip->y) + FRAME_RATE_MUL));
    back->h = next;
    if (mp_on() != 0) {
        const float room = v->height - float(int32_t(next));
        const float grown = float(drip->h) + mul;
        drip->h = int16_t(int32_t(room < grown ? room : grown));
        return;
    }
    drip->h = int16_t(int32_t(float(drip->h) + mul));
}

// [decomp] 0x0019a7f0 HUD_UpdateAmmoPane
// A munição: sprite 0 "pente-guardada" (ou a porcentagem nas munições 0x14, 0x1a e 0x1f e nos
// veículos), 1 o nome do modo de disparo, 2 o nome da arma (um segundo depois de trocar de arma,
// no lugar do modo), 5 e 6 as balas do pente desenhadas como pilha (recorte de BulletImg.189) e 7 o
// alvo das armas 0x50/0x51 (o objeto tipo 0x51 visível mais perto).
// Só no port: um pente de tamanho 0 nas munições em porcentagem mostra 0 (o original para numa
// divisão por zero) e o nome da arma com rótulo sem texto (Txt_BindLabel nulo) sai vazio.
void HUD_UpdateAmmoPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (b == nullptr || obj == nullptr || pane == nullptr || pane->enabled == 0) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    const weapon_definition_tag* d = weapon_def(anim->weapon);
    if (d->id == 0x4b) d = weapon_def(0x4a);
    if (d->id == 0x4d) d = weapon_def(0x4c);
    uint32_t ammo_type = d->ammo_type;
    const int16_t slot = Player_AmmoIndex(int16_t(anim->weapon));
    P32<sprite>* sprites = pane->sprites;
    sprite* count_spr = sprites[0];
    sprite* mode_spr = sprites[1];
    sprite* name_spr = sprites[2];
    const uint8_t fire_mode = b->weapons[anim->weapon].mode;
    const uint32_t mode_label = rd<uint32_t>(d, 0x30 + uint32_t(fire_mode) * 4);
    uint32_t clip = uint16_t(d->clip);
    uint32_t rounds = uint16_t(b->weapons[slot].rounds);
    if (ammo_type == 0x1f) {
        clip = (uint32_t(uint16_t(d->clip)) - 0xc) & 0xffff;
        const uint32_t left = (uint32_t(uint16_t(d->clip)) - uint32_t(uint16_t(b->weapons[slot].rounds))) & 0xffff;
        rounds = left < clip ? left : clip;
    }
    if (obj->unknown_f6 == 0xc) {
        ammo_type = 0x1f;
        clip = 100;
        const uint32_t v = uint32_t(int32_t(rd<float>(b->ride->data, 0x90) * 0.5f)) & 0xffff;
        rounds = v < 100 ? v : 100;
    }
    if (obj->unknown_f6 == 0x10) {
        ammo_type = 0x1f;
        clip = 100;
        const uint16_t v = rd<uint16_t>(b->ride->data, 0xf0);
        rounds = uint32_t((int32_t(int16_t(v)) + (v >> 15)) >> 1) & 0xffff;
    }
    if (obj->unknown_f6 == 0xb) {
        obj_tag* ride = b->ride;
        if (ride == nullptr) return;
        const uint8_t* data = ride->glist == hashtable_hashcode_to_celglist(0x2000194)
                                  ? static_cast<const uint8_t*>(ride->data)
                                  : static_cast<const uint8_t*>(ride->owner->data);
        if (char* t = text_of(count_spr)) std::snprintf(t, kStringSize, "%d", int32_t(rd<uint16_t>(data, 0x110)));
        Sprite_SetText(count_spr, text_of(count_spr), 0);
        if (char* t = text_of(mode_spr)) std::snprintf(t, kStringSize, "%d", int32_t(rd<float>(data, 0x104)));
        Sprite_SetText(mode_spr, text_of(mode_spr), 0);
        name_spr->priority = -1;
        mode_spr->priority = 0x1c;
        return;
    }
    if (count_spr != nullptr && pane->count != 0) {
        if ((weapon_def(anim->weapon)->flags & 0x20) != 0 || b->unknown_95e != 0) {
            count_spr->length = 0;
        } else {
            char* t = text_of(count_spr);
            if (ammo_type == 0x1a || ammo_type == 0x14 || ammo_type == 0x1f) {
                if (t != nullptr) std::snprintf(t, kStringSize, "%d", int32_t(clip != 0 ? (rounds * 100) / clip : 0));
            } else if (t != nullptr) {
                std::snprintf(t, kStringSize, "%d-%d", int32_t(rounds), int32_t(b->ammo[ammo_type]));
            }
            Sprite_SetText(count_spr, t, 0);
        }
    }
    if (mode_spr != nullptr && 1 < pane->count) {
        const char* name = mode_label == 0 ? "~-~" : Txt_BindLabel(mode_label, 0);
        format_label(text_of(mode_spr), name);
        Sprite_SetText(mode_spr, text_of(mode_spr), 0);
    }
    if (name_spr != nullptr && 2 < pane->count) {
        const int8_t weapon = anim->weapon;
        const int8_t next = anim->next_weapon;
        if (next != weapon && weapon_def(next)->klass != weapon_def(weapon)->klass) b->unknown_95e = uint8_t(int32_t(FRAME_RATE));
        uint8_t mode_mode;
        if (b->unknown_95e == 0) {
            mode_mode = 0x1c;
            name_spr->priority = -1;
        } else {
            --b->unknown_95e;
            mode_mode = 0xff;
            name_spr->priority = 0x1c;
        }
        mode_spr->priority = mode_mode;
        const uint32_t label = rd<uint32_t>(weapon_def(next), mp_on() == 0 ? 0x38 : 0x3c);
        if (label == 0xffffffffu) {
            if (name_spr->text != nullptr) name_spr->text[0] = 0;
        } else {
            format_label(name_spr->text, Txt_BindLabel(label, 0));
        }
        Sprite_SetText(name_spr, name_spr->text, 0);
    }
    sprite* stack = 5 < pane->count ? sprites[5] : nullptr;
    sprite* stack_back = 6 < pane->count ? sprites[6] : nullptr;
    sprite* target = 7 < pane->count ? sprites[7] : nullptr;
    if (stack != nullptr) {
        if (b->unknown_95e == 0) {
            const float scale = 0x57 < clip ? 16.0f : 1.0f;
            const float* img = BulletImg_189[ammo_type < 33 ? ammo_type : 0];
            stack->priority = 0x1c;
            const float height = float(rounds) * img[3];
            stack->x = int16_t(int32_t(float(count_spr->x) - img[2] * scale));
            const int16_t h = int16_t(int32_t(height));
            stack->h = h;
            stack->y = int16_t(int32_t(float(pane->y) - height));
            stack->u = int16_t(int32_t(img[0]));
            stack->uw = int16_t(int32_t(img[2]));
            stack->w = int16_t(int32_t(img[2] * scale));
            stack->vh = h;
            stack->v = int16_t(int32_t(float(int32_t(clip - rounds)) * img[3] + img[1]));
        } else {
            stack->priority = -1;
        }
    }
    if (stack_back != nullptr) {
        if (b->unknown_95e == 0) {
            const float scale = 0x57 < clip ? 16.0f : 1.0f;
            const float* img = BulletImg_189[ammo_type < 33 ? ammo_type : 0];
            stack_back->priority = 0x1d;
            stack_back->x = int16_t(int32_t(float(count_spr->x) - img[2] * scale));
            const int16_t h = int16_t(int32_t(float(clip) * img[3]));
            stack_back->vh = h;
            stack_back->h = h;
            stack_back->y = int16_t(int32_t(float(pane->y) - float(clip) * img[3]));
            stack_back->u = int16_t(int32_t(img[0]));
            stack_back->uw = int16_t(int32_t(img[2]));
            stack_back->w = int16_t(int32_t(img[2] * scale));
            stack_back->v = int16_t(int32_t(img[1]));
        } else {
            stack_back->priority = -1;
        }
    }
    if (target == nullptr) return;
    float best = 10000.0f;
    target->priority = -1;
    if (uint8_t(anim->weapon - 0x50) >= 2) return;
    viewer_tag* v = viewer_of(b);
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->unknown_fc == 0 || o->type != 0x51) continue;
        if (!Collide_LineOfSight(&o->pos, &v->pos, o->cel, obj, o, 8)) continue;
        _VECTOR screen{};
        if (View_3DPoint2Screen(&o->pos, &screen, uint16_t(int16_t(b->player))) <= 0) continue;
        const float dist = Vec_Dist3D(&o->pos, &obj->pos);
        if (best <= dist) continue;
        target->priority = 0x1c;
        target->x = int16_t(int32_t(screen.x));
        target->y = int16_t(int32_t(v->height - screen.y));
        best = dist;
        if (dist < weapon_def(0x50)->range) {
            target->colour = b->touch_type == 2 ? 0x00ff00ffu : 0xffffffffu;
        } else {
            target->colour = 0xffffffffu;
        }
    }
}

// [decomp] 0x0019b110 HUD_FixMP
// Multijogador: prende o sprite dentro da área da câmera do jogador, 16 pixels para dentro das
// bordas externas da tela.
void HUD_FixMP(sprite* sp, int16_t player) {
    if (mp_on() == 0) return;
    sprite* s = sp;
    const viewer_tag* v = glb_viewer[player];
    const int16_t y1 = int16_t(int32_t(v->viewport[3]));
    const int16_t x0 = int16_t(int32_t(v->viewport[0]));
    const int16_t x1 = int16_t(int32_t(v->viewport[1]));
    const int16_t y0 = int16_t(int32_t(v->viewport[2]));
    int16_t lo_x = x0, hi_x = x1, lo_y = y0, hi_y = y1;
    const uint32_t players = mp_u32(0x1ac);
    const auto in = [](int16_t v) { return int16_t(v + 0x10); };
    const auto out = [](int16_t v) { return int16_t(v - 0x10); };
    if (players == 1) {
        lo_x = in(x0);
        hi_x = out(x1);
        lo_y = in(y0);
        hi_y = out(y1);
    } else if (players == 2) {
        if (DrawInfo[0] == 1) {
            if (player == 0) {
                lo_x = in(x0);
            } else {
                hi_x = out(x1);
            }
            lo_y = in(y0);
            hi_y = out(y1);
        } else if (player == 0) {
            lo_x = in(x0);
            hi_x = out(x1);
            lo_y = in(y0);
        } else {
            lo_x = in(x0);
            hi_x = out(x1);
            hi_y = out(y1);
        }
    } else if (player == 1) {
        hi_x = out(x1);
        lo_y = in(y0);
    } else if (player == 0) {
        lo_x = in(x0);
        lo_y = in(y0);
    } else if (player == 2) {
        lo_x = in(x0);
        hi_y = out(y1);
    } else if (player == 3) {
        hi_x = out(x1);
        hi_y = out(y1);
    }
    if (s->x < lo_x) s->x = lo_x;
    if (s->y < lo_y) s->y = lo_y;
    if (hi_x < int32_t(s->x) + int32_t(s->w)) s->x = int16_t(hi_x - s->w);
    if (hi_y < int32_t(s->y) + int32_t(s->h)) s->y = int16_t(hi_y - s->h);
}

// [decomp] 0x0019b390 HUD_CreateMPHealthPane
void HUD_CreateMPHealthPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shift_sprites(b, pane, create);
    const int16_t player = int16_t(b->player);
    HUD_FixMP(pane->sprites[0], player);
    HUD_FixMP(pane->sprites[2], player);
    pane->sprites[3]->x = int16_t(pane->sprites[0]->x + 8);
    pane->sprites[3]->y = int16_t(pane->sprites[0]->y + 4);
    // Os sprites 4 a 7 (indicadores de direção do tiro) encolhem como em HUD_CreateShrink.
    for (int32_t i = 4; i < 8; ++i) {
        sprite* s = pane->sprites[i];
        if (glb_viewer[b->player]->width < 512.0f) {
            s->w = int16_t(s->w >> 1);
            s->x = int16_t(s->x - (create->sprites[i].x >> 1));
        }
        if (glb_viewer[b->player]->height < 448.0f) {
            s->h = int16_t(s->h >> 1);
            s->y = int16_t(s->y - (create->sprites[i].y >> 1));
        }
    }
}

// [decomp] 0x0019b630 HUD_UpdateMPHealthPane
// Multijogador: barra de vida (sprite 2, até 128 pixels pela vida máxima da partida), colete
// (sprite 3), coordenadas de depuração (1) e os indicadores de direção do tiro (4 a 7).
void HUD_UpdateMPHealthPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (pane->enabled == 0) return;
    P32<sprite>* sprites = pane->sprites;
    sprite* frame = sprites[0];
    sprite* armour = sprites[3];
    sprite* bar = sprites[2];
    sprite* coords = sprites[1];
    for (int32_t i = 4; i < 8; ++i) sprites[i]->priority = -1;
    if (b->pain_flash != 0) {
        for (int32_t k = 0; k < 4; ++k) {
            if ((b->hit_dir & (1 << k)) == 0) continue;
            pane->sprites[4 + k]->priority = 0x1e;
            pane->sprites[4 + k]->colour = uint32_t(b->pain_flash) | 0xff000000u;
        }
        const float faded = float(b->pain_flash) - FRAME_RATE_MUL;
        b->pain_flash = uint8_t(int32_t(0.0f < faded ? faded : 0.0f));
    }
    const HUDINFO_tag* hud = hud_of(b);
    const bool hidden = hud->panes[kHudCamera].enabled != 0 || hud->panes[kHudOICW].enabled != 0 ||
                        hud->panes[kHudRonin].enabled != 0 || hud->panes[kHudSecCam].enabled != 0 ||
                        hud->panes[kHudRedeemer].enabled != 0 || hud->panes[kHudRCCar].enabled != 0;
    if (hidden) {
        frame->priority = -1;
        bar->priority = -1;
        armour->priority = -1;
        return;
    }
    if (bar != nullptr) {
        uint32_t colour = 0x52a88b00u;
        const float f = b->health / (float(rd<int32_t>(MPSettings, uint32_t(b->player) * 0x30 + 0x2c)) + 100.0f);
        const int16_t w = int16_t(int32_t(f * 128.0f));
        bar->uw = w;
        bar->w = w;
        if (f < 0.5f) colour = 0xc6984e00u;
        if (f < 0.2f) colour = 0x9d191200u;
        bar->priority = 0x1c;
        bar->colour = colour | 0xffu;
        frame->priority = 0x1d;
    }
    if (armour != nullptr) {
        if (b->armour == 0.0f) {
            armour->priority = -1;
        } else {
            armour->priority = 0x1b;
            armour->w = int16_t(int32_t(b->armour * 2.56f));
            armour->uw = int16_t(int32_t(b->armour * 2.56f));
        }
    }
    if (coords != nullptr) {
        if (switch_SHOW_COORDS == 0) {
            coords->priority = -1;
        } else {
            coords->priority = 0x1d;
            if (coords->text != nullptr) {
                std::snprintf(coords->text, kStringSize, "%.2f %.2f %.2f\nFPS %d\n", double(obj->pos.x), double(obj->pos.y),
                              double(obj->pos.z), int32_t(RealFrameRate));
            }
            Sprite_SetText(coords, coords->text, 0);
        }
    }
}

// [decomp] 0x0019b9c8 HUD_UpdateHealthPane
// Vida e colete: 8 segmentos de colete (sprites 0-7, acesos até colete x 0,16) e 7 de vida (8-14,
// acesos até vida x 0,07, verdes, amarelos abaixo de 4 e vermelhos abaixo de 2, o primeiro piscando
// quase sem vida), o ícone (15), todos esmaecendo com BLData::unknown_8bc (1 a cada ferimento); o
// 007 do momento Bond (16, BLData::bond_moment), as coordenadas de depuração (17), o ícone do que o
// jogador toca (18) e o de agachar (19); os indicadores de direção do tiro (20-23) pelos bits de
// BLData::hit_dir, com o vermelho do ferimento (BLData::pain_flash), que cai FRAME_RATE_MUL por tick.
// As barras somem com a câmera, o OICW, o Ronin, a câmera de segurança, o Redeemer ou o carrinho.
// Só no port: um ícone de toque fora da tabela de 8 (o original leria a pilha) fica escondido.
void HUD_UpdateHealthPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    P32<sprite>* sprites = pane->sprites;
    sprite* crouch = sprites[0x13];
    sprite* icon = sprites[0xf];
    sprite* hit = sprites[0x10];
    sprite* coords = sprites[0x11];
    sprite* touch = sprites[0x12];
    if (pane->enabled == 0) return;
    for (int32_t i = 0x14; i < 0x18; ++i) pane->sprites[i]->priority = -1;
    if (b->pain_flash != 0) {
        for (int32_t k = 0; k < 4; ++k) {
            if ((b->hit_dir & (1 << k)) == 0) continue;
            pane->sprites[0x14 + k]->priority = 0x1e;
            pane->sprites[0x14 + k]->colour = uint32_t(b->pain_flash) | 0xff000000u;
        }
        const float faded = float(b->pain_flash) - FRAME_RATE_MUL;
        b->pain_flash = uint8_t(int32_t(0.0f < faded ? faded : 0.0f));
    }
    const float armour = b->armour;
    if (GS_IsPaused(-1)) b->unknown_8bc = 1.0f;
    float fade = b->unknown_8bc * 4.0f;
    if (1.0f <= fade) fade = 1.0f;
    const HUDINFO_tag* hud = hud_of(b);
    const bool hidden = hud->panes[kHudCamera].enabled != 0 || hud->panes[kHudOICW].enabled != 0 ||
                        hud->panes[kHudRonin].enabled != 0 || hud->panes[kHudSecCam].enabled != 0 ||
                        hud->panes[kHudRedeemer].enabled != 0 || hud->panes[kHudRCCar].enabled != 0;
    if (hidden) {
        for (int32_t i = 0; i < 0x10; ++i) pane->sprites[i]->priority = -1;
    } else {
        const SpriteInfo* info = pane->create->sprites;
        for (uint32_t i = 0; i < 8; ++i) {
            sprite* s = pane->sprites[i];
            if (armour * 0.16f == 0.0f) {
                s->priority = -1;
                continue;
            }
            float level = float(i) < armour * 0.16f ? 0.75f : 0.35f;
            if (i == 0) level = 1.0f;
            s->priority = int8_t(info[i].priority);
            set_alpha(s, uint32_t(int32_t(level * fade * 255.0f)));
        }
        float health = b->health;
        if (health < 0.0f) health = 0.0f;
        else if (100.0f < health) health = 100.0f;
        health = health * 0.07f;
        uint32_t colour = 0x52a88b00u;
        if (health < 4.0f) colour = 0xc6984e00u;
        if (health < 2.0f) colour = 0x9d191200u;
        for (uint32_t i = 0; i < 7; ++i) {
            sprite* s = pane->sprites[8 + i];
            s->priority = int8_t(info[8 + i].priority);
            float level = float(i) < health ? 0.55f : 0.15f;
            if (i == 0 && health < 0.5f) {
                level = uint32_t(int32_t(GameState.frame) % 0x1e) < 0xf ? fade * 0.55f : fade * 0.15f;
            } else {
                level = level * fade;
            }
            s->colour = colour | (uint32_t(int32_t(level * 255.0f)) & 0xffu);
        }
        if (icon != nullptr) icon->colour = colour | (uint32_t(int32_t(fade * 140.0f)) & 0xffffu);
    }
    if (hit != nullptr) {
        hit->viewer = scene_camera_active() ? 4 : 5;
        if (int16_t(b->bond_moment) < 1) {
            b->bond_moment = 0;
            hit->priority = -1;
        } else {
            hit->x = 0x100;
            hit->y = 0x50;
            const int16_t t = int16_t(b->bond_moment);
            if (t < 0x65) {
                hit->priority = 0;
            } else {
                const float shrink = float(200 - t) * 0.01f;
                const float wave = PS2Sinf(float((t - 100) * 4) * 0.03141593f + 1.5707964f);
                hit->h = int16_t(int32_t(float(uint16_t(hit->tex_h)) * shrink * std::fabs(wave)));
                hit->vh = wave < 0.0f ? int16_t(-hit->tex_h) : int16_t(hit->tex_h);
                hit->w = int16_t(int32_t(float(uint16_t(hit->tex_w)) * shrink));
                hit->priority = 0;
            }
            b->bond_moment = uint16_t(int16_t(int32_t(float(int16_t(b->bond_moment)) - FRAME_RATE_MUL)));
        }
    }
    if (coords != nullptr) {
        if (switch_SHOW_COORDS == 0) {
            coords->priority = -1;
        } else {
            coords->priority = 0x1d;
            if (coords->text != nullptr) {
                std::snprintf(coords->text, kStringSize, "%.2f %.2f %.2f\nFPS %d\n", double(obj->pos.x), double(obj->pos.y),
                              double(obj->pos.z), int32_t(RealFrameRate));
            }
            Sprite_SetText(coords, coords->text, 0);
        }
    }
    if (touch != nullptr) {
        bool shown = false;
        if (b->touch_type == 0xff) {
            if (ThirdIconTimer != 0) {
                touch->priority = 0x1d;
                const int32_t t = ThirdIconTimer;
                --ThirdIconTimer;
                touch->colour = (uint32_t(int32_t(float(t * 0xff) / (FRAME_RATE * 0.5f))) & 0xffu) | 0x7f7f7f00u;
                shown = true;
            }
        } else {
            // Ícones do que o jogador toca (a tabela local do original).
            static const uint32_t kTouchIcons[8] = {0x3000064, 0x3000065, 0x3000066, 0x3000067,
                                                    0x3000068, 0x3000069, 0x300006a, 0};
            if (b->touch_type < 8 && hashtable_set_sprite(touch, kTouchIcons[b->touch_type]) != 0) {
                touch->priority = 0x1d;
                touch->colour = 0x7f7f7fffu;
                touch->x = 400;
                touch->y = 0x30;
                ThirdIconTimer = int16_t(int32_t(FRAME_RATE * 0.5f));
                shown = true;
            }
        }
        if (!shown) touch->priority = -1;
    }
    if (crouch != nullptr) {
        if (obj->unknown_f6 == 4) {
            hashtable_set_sprite(crouch, 0x3000173);
            crouch->priority = 0x1d;
            crouch->colour = 0x7f7f7fffu;
            crouch->y = 0x30;
            crouch->x = 0x30;
            CrouchIconTimer = int16_t(int32_t(FRAME_RATE));
        } else if (CrouchIconTimer == 0) {
            crouch->priority = -1;
        } else {
            hashtable_set_sprite(crouch, 0x3000174);
            crouch->priority = 0x1d;
            uint32_t alpha = 0xff;
            if (float(CrouchIconTimer) < FRAME_RATE * 0.5f) {
                alpha = uint32_t(int32_t(float(CrouchIconTimer * 0x100 - CrouchIconTimer) / (FRAME_RATE * 0.5f))) & 0xffu;
            }
            --CrouchIconTimer;
            crouch->colour = alpha | 0x7f7f7f00u;
        }
    }
}

// [decomp] 0x0019c310 HUD_UpdateStatusPane
// As panes de mensagem (missão, objetivos, informação, itens): uma máquina de 5 passos em extra[1]
// sobre a mensagem atual (extra[0], TXT_MSG, que Text_UpdateMsg troca; extra[2] é o tipo de
// mensagem da pane e extra[3] a velocidade): 0 espera uma mensagem; 1 abre a faixa (sprite 2) do
// centro para fora; 2 põe o texto (ou o ícone) e, nos objetivos, o título "novo objetivo" /
// "objetivo cumprido" (sprite 5) e os sons; 3 mostra e esmaece até a mensagem acabar; 4 fecha a
// faixa. Os sprites 3 e 4 acompanham as pontas da faixa.
// Só no port: sem o sprite de título (mensagem de objetivo numa pane que não é a de objetivos) ou
// sem o texto do rótulo (Txt_BindLabel nulo), o original escreveria ou leria por nulo; aqui pula.
void HUD_UpdateStatusPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    if (pane->enabled == 0) return;
    const uint16_t count = pane->count;
    uint32_t* extra = pane->extra;
    P32<sprite>* sprites = pane->sprites;
    const int32_t speed = int32_t(extra[3]);
    sprite* band = sprites[2];
    sprite* left = 3 < count ? sprites[3] : nullptr;
    sprite* right = 4 < count ? sprites[4] : nullptr;
    sprite* glow = 5 < count ? sprites[5] : nullptr;
    HUDPANECREATE_tag* create = pane->create;
    sprite* text = sprites[0];
    sprite* fail = create == HudMsgMissionStatusPane ? sprites[1] : nullptr;
    sprite* title = create == HudMsgObjectiveStatusPane ? sprites[5] : nullptr;
    auto* msg = nf::p32_ptr<TXT_MSG>(extra[0]);
    const SpriteInfo* band_info = &create->sprites[2];
    const int32_t centre = band_info->x + (band_info->w >> 1);
    bool place_ends = false;
    switch (extra[1]) {
        case 0: {
            for (uint32_t i = 0; i < pane->count; i = uint16_t(i + 1)) pane->sprites[i]->priority = -1;
            if (text != nullptr) text->colour &= 0xffffff00u;
            if (glow != nullptr) glow->colour &= 0xffffff00u;
            if (Text_UpdateMsg(int8_t(b->player), int32_t(extra[2]), reinterpret_cast<P32<TXT_MSG>*>(&extra[0])) != 0 &&
                extra[0] != 0) {
                if (band != nullptr) {
                    band->w = 0;
                    band->x = int16_t(centre);
                }
                extra[1] = 1;
            }
            return;
        }
        case 1:
            if (band == nullptr) {
                extra[1] = 2;
            } else {
                const int16_t w = create->sprites[2].w;
                band->w = w;
                band->x = int16_t(centre - (w >> 1));
                extra[1] = 2;
            }
            place_ends = true;
            break;
        case 2: {
            if (fail != nullptr) fail->colour = 0x7d6d59ffu;
            if (text != nullptr) text->colour = 0x7d6d59ffu;
            if (glow != nullptr) glow->colour |= 0xffu;
            if (msg->data != 0) {
                hashtable_set_sprite(text, msg->data);
                text->text = nullptr;
            } else {
                if (msg->type == 2) {
                    const char* done = Txt_BindLabel(0x2000053, 0);
                    IsNew_224 = (done == nullptr || msg->text == nullptr || std::strcmp(msg->text, done) != 0) ? 1 : 0;
                    IsComplete_225 = IsNew_224 == 0 ? 1 : 0;
                    const uint32_t label = IsNew_224 != 0 ? 0x2000051u : (IsComplete_225 != 0 ? 0x2000053u : 0x2000052u);
                    if (title != nullptr) {
                        title->text = Txt_BindLabel(label, 0);
                        Sprite_SetFmt(title, title->format, 0);
                    }
                    if (text != nullptr) text->colour = 0;
                    if (IsNew_224 != 0) {
                        ObjectiveSFXPlayMe1st_222 = 0x5fc;
                        ObjectiveSFXPlayMe2nd_223 = 0x1d5;
                        ObjectiveSFXTriggered1_220 = 0;
                        ObjectiveSFXTriggered2_221 = 0;
                    } else if (IsComplete_225 != 0) {
                        ObjectiveSFXPlayMe1st_222 = 0x1d4;
                        ObjectiveSFXTriggered2_221 = 1;
                        ObjectiveSFXTriggered1_220 = 0;
                    } else {
                        ObjectiveSFXTriggered2_221 = 1;
                        ObjectiveSFXTriggered1_220 = 1;
                    }
                }
                Sprite_SetText(text, msg->text, 0);
                Sprite_SetText(fail, Txt_BindLabel(Mission_FailLabel(), 0), 0);
                if (msg->time < 0.0f) {
                    text->colour = 0;
                    if (fail != nullptr) fail->colour = 0;
                }
            }
            extra[1] = 3;
            return;
        }
        case 3: {
            if (msg != nullptr) {
                text->colour |= 0xffu;
                if (fail != nullptr) fail->colour = (fail->colour & 0xffffff00u) | 0xffu;
                if (msg->type == 2 && title != nullptr) {
                    if ((IsNew_224 != 0 && 300.0f < msg->time + 60.0f) || (IsComplete_225 != 0 && 180.0f < msg->time + 60.0f)) {
                        text->colour = 0;
                        if (fail != nullptr) fail->colour = 0;
                        title->colour = std::fmod(msg->time, 40.0f) < 20.0f ? 0x785a14ffu : 0u;
                        title->colour2 = 0;
                        text->colour2 = 0;
                        if (fail != nullptr) fail->colour2 = 0;
                        if (ScriptCam == 0 && ObjectiveSFXTriggered1_220 == 0) {
                            Sound_Play(ObjectiveSFXPlayMe1st_222, 100.0f, 0, 0);
                            ObjectiveSFXTriggered1_220 = 1;
                        }
                    } else {
                        if (ObjectiveSFXTriggered2_221 == 0) {
                            Sound_Play(ObjectiveSFXPlayMe2nd_223, 100.0f, 0, 0);
                            ObjectiveSFXTriggered2_221 = 1;
                        }
                        title->colour = 0x785a14ffu;
                        text->colour = IsComplete_225 != 0 ? 0u : 0x7d6d59ffu;
                        if (fail != nullptr) fail->colour = 0x7d6d59ffu;
                        title->colour2 = 0xff;
                        text->colour2 = IsComplete_225 != 0 ? 0u : 0xffu;
                        if (fail != nullptr) fail->colour2 = 0xff;
                    }
                }
                if (0.0f <= msg->time && msg->time < 15.0f) {
                    text->colour = (text->colour & 0xffffff00u) | uint32_t(msg->time * 17.0f);
                    if (fail != nullptr) fail->colour = (fail->colour & 0xffffff00u) | uint32_t(msg->time * 17.0f);
                }
                if (msg->time < 0.0f) {
                    text->colour = 0;
                    if (fail != nullptr) fail->colour = 0;
                }
                if (msg->type == 2 && IsComplete_225 != 0 && text != nullptr) text->colour = 0;
            }
            if (Text_UpdateMsg(int8_t(b->player), int32_t(extra[2]), reinterpret_cast<P32<TXT_MSG>*>(&extra[0])) != 0) {
                extra[1] = extra[0] == 0 ? 4 : 2;
            }
            return;
        }
        case 4: {
            if (text != nullptr) text->colour &= 0xffffff00u;
            if (fail != nullptr) fail->colour &= 0xffffff00u;
            if (glow != nullptr) glow->colour &= 0xffffff00u;
            if (band == nullptr) {
                extra[1] = 0;
            } else {
                const int32_t w = int32_t(uint16_t(band->w)) - speed;
                band->w = int16_t(w);
                band->x = int16_t(centre - (int16_t(w) >> 1));
                if (int16_t(w) <= 0) extra[1] = 0;
            }
            place_ends = true;
            break;
        }
        default:
            return;
    }
    if (!place_ends || band == nullptr) return;  // só no port: sem faixa (o original leria por nulo)
    if (left != nullptr) left->x = int16_t(band->x - left->w);
    if (right != nullptr) right->x = int16_t(band->x + band->w);
}

// [decomp] 0x0019ca88 HUD_MPUpdateStatusPane
// Multijogador: a pane de informação só com o texto (ou o ícone) da mensagem, esmaecendo no fim.
void HUD_MPUpdateStatusPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    uint32_t* extra = pane->extra;
    auto* msg = nf::p32_ptr<TXT_MSG>(extra[0]);
    sprite* text = pane->sprites[0];
    if (pane->enabled == 0) return;
    switch (int32_t(extra[1])) {
        case 2:
            text->priority = 0x1e;
            if (msg->data == 0) {
                Sprite_SetText(text, msg->text, 0);
            } else {
                hashtable_set_sprite(text, msg->data);
                text->text = nullptr;
            }
            extra[1] = 3;
            return;
        case 0:
            for (uint32_t i = 0; i < pane->count; i = uint16_t(i + 1)) pane->sprites[i]->priority = -1;
            if (Text_UpdateMsg(int8_t(b->player), int32_t(extra[2]), reinterpret_cast<P32<TXT_MSG>*>(&extra[0])) == 0) return;
            if (extra[0] == 0) return;
            extra[1] = 2;
            return;
        case 3:
            if (msg != nullptr) {
                const float t = msg->time;
                if (0.0f < t && t < 15.0f) {
                    text->colour = (text->colour & 0xffffff00u) | uint32_t(t * 17.0f);
                } else {
                    text->colour |= 0xffu;
                }
            }
            if (Text_UpdateMsg(int8_t(b->player), int32_t(extra[2]), reinterpret_cast<P32<TXT_MSG>*>(&extra[0])) != 0) {
                extra[1] = extra[0] == 0 ? 4 : 2;
            }
            return;
        case 4:
            text->priority = -1;
            extra[1] = 0;
            return;
        default:
            return;
    }
}

// [decomp] 0x0019cc90 HUD_CreateOICWPane
// A mira do OICW: encolhe como HUD_CreateShrink e escreve as linhas do "boot" da mira (só no modo
// história; no multijogador ela já começa pronta).
void HUD_CreateOICWPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shrink_sprites(b, pane, create);
    if (mp_on() != 0) {
        OICW_timer = -555;  // 0xfdd5
        OICW_mode = 2;
        return;
    }
    OICW_mode = 0;
    OICW_timer = 0x96;
    static const char* const kLines[5] = {"COMMAND.COM\n", "LOAD BIOS\n", "MEMORY SET\n", "SYSTEM STATUS\n", "OK\n"};
    for (int32_t i = 0; i < 5; ++i) {
        sprite* s = pane->sprites[2 + i];
        if (s->text != nullptr) std::snprintf(s->text, kStringSize, "%s", kLines[i]);
        Sprite_SetText(s, s->text, 0);
    }
}

// [decomp] 0x0019cef8 HUD_UpdateOICWPane
// O "boot" da mira do OICW (modo 0: as 5 linhas aparecem a cada meio segundo, a segunda piscando),
// o pisca-pisca do fim (modo 1, meio segundo) e a mira pronta (modo 2, linhas escondidas). Desligada
// volta ao começo. Mira ligada desliga a visão especial da câmera.
void HUD_UpdateOICWPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    if (mp_on() != 0) return;
    if (pane->enabled == 0) {
        if (OICW_mode != 2) {
            OICW_mode = 0;
            OICW_timer = 0x96;
        }
        return;
    }
    viewer_of(b)->mode = 0;
    if (OICW_mode == 1) {
        --OICW_timer;
        const int32_t phase = int32_t(OICW_timer) % 4;
        const uint8_t mode = (phase < 0 ? -phase : phase) < 2 ? uint8_t(0xff) : uint8_t(0x1c);
        for (int32_t i = 0; i < 5; ++i) pane->sprites[2 + i]->priority = mode;
        if (OICW_timer < 1) OICW_mode = 2;
        return;
    }
    if (1 < OICW_mode) {
        if (OICW_mode != 2) return;
        for (int32_t i = 0; i < 5; ++i) pane->sprites[2 + i]->priority = -1;
        return;
    }
    if (OICW_mode != 0) return;
    --OICW_timer;
    const int32_t lines = 5 - int32_t(OICW_timer) / 0x1e;
    int32_t y = int16_t(int32_t(408.0f - float(lines * 0x11)));
    for (int32_t i = 0; i < lines; ++i) {
        sprite* s = pane->sprites[2 + i];
        s->x = 0x32;
        s->y = int16_t(y);
        s->priority = 0x1c;
        if (i == 1 && lines == 2) {
            const int32_t phase = int32_t(OICW_timer) % 8;
            if ((phase < 0 ? -phase : phase) < 4) pane->sprites[3]->priority = -1;
        }
        y = int16_t(y + 0x11);
    }
    if (OICW_timer < 1) {
        OICW_mode = 1;
        OICW_timer = 0x1e;
    }
}

// [decomp] 0x0019d218 HUD_UpdateNightSightPane
// Visão noturna (modo 1 da câmera): ruído verde, a linha de varredura (sprite 0, v sorteado a cada
// dois ticks), uma luz branca na câmera (modo história) e a bateria (sprite 4, BLData::unknown_920
// de 0 a 1800). Fora do modo 1 a pane se desliga.
void HUD_UpdateNightSightPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    if (pane->enabled == 0) return;
    viewer_tag* v = viewer_of(b);
    if (v->mode != 1) {
        pane->enabled = 0;
        return;
    }
    if (scene_camera_active()) return;
    sprite* noise = pane->sprites[0];
    noise->colour = (Rand_Rand(0x18) + 7u) | 0xff0000u;
    const uint8_t scroll = Scrl_247;
    ++Scrl_247;
    if ((scroll & 1) != 0) noise->v = int16_t(Rand_Rand(uint32_t(int32_t(uint16_t(noise->tex_h)) - 1)));
    if (mp_on() == 0) Light_Create(&v->pos, 0xff, 0xff, 0xff, 32.0f, 0, 0, 1, 2.5f, 0, 0, 0, 0xffff);
    set_bar(pane->sprites[4], b->unknown_920 * 0.00055555557f);
}

// [decomp] 0x0019d3b8 HUD_UpdateSpacePane
// Estação espacial (0700001b): o painel dos 8 mísseis (sprites 1-8; MissileDeploy: 1 armado
// piscando, 2/3 pelos canais 1-8) e os alarmes dos canais 9, 0x18 e 0x1c (sprites 0, 9 e 10). Liga e
// desliga sozinha pela fase.
void HUD_UpdateSpacePane(BLData*, HUDPANE_tag* pane, obj_tag*) {
    if (pane->enabled == 0) {
        if (GameState.level_file != 0x700001b) return;
        pane->enabled = 1;
    }
    if (GameState.level_file != 0x700001b) {
        pane->enabled = 0;
        return;
    }
    for (int32_t i = 1; i < 9; ++i) {
        if (MissileDeploy[i - 1] != 1) continue;
        sprite* s = pane->sprites[i];
        hashtable_set_sprite(s, 0x3000177);
        s->colour = blink_on() ? 0x00ff00ffu : 0xffffffffu;
    }
    for (int32_t i = 1; i < 9; ++i) {
        if (switch_channels[i] == 0 || MissileDeploy[i - 1] == 3) continue;
        sprite* s = pane->sprites[i];
        hashtable_set_sprite(s, 0x300017a);
        s->colour = MissileDeploy[i - 1] == 2 ? 0xff000080u : 0xff0000ffu;
    }
    for (int32_t i = 1; i < 9; ++i) {
        if (MissileDeploy[i - 1] != 3) continue;
        sprite* s = pane->sprites[i];
        hashtable_set_sprite(s, 0x3000177);
        s->colour = 0x00ff0080u;
    }
    if (switch_channels[9] != 0) pane->sprites[0]->colour = blink_on() ? 0xff000080u : 0x7f7f7f7fu;
    pane->sprites[9]->colour = switch_channels[0x18] != 0 && blink_on() ? 0xff000080u : 0x7f7f7f7fu;
    pane->sprites[10]->colour = switch_channels[0x1c] != 0 && blink_on() ? 0xff000080u : 0x7f7f7f7fu;
}

// [decomp] 0x0019d770 HUD_UpdateLensFlarePane
// O reflexo do sol: com o céu visível, a linha de visão da cabeça até a cabeça - SunVector diz se o
// sol aparece (BLData::night_vision, aqui "sol visível"); o ponto na tela (pelo zoom) arrasta os 7
// sprites ao longo da reta do centro, com o alfa subindo 0x20 por tick enquanto o sol está na tela
// e caindo 0x10 fora dela.
void HUD_UpdateLensFlarePane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    viewer_tag* v = viewer_of(b);
    b->night_vision = 0;
    if (pane->enabled == 0) return;
    if (SunVector.x == 0.0f && SunVector.y == 0.0f && SunVector.z == 0.0f) return;
    alignas(16) _VECTOR sun;
    Vec_Zero(&sun);
    int32_t alpha = 0;
    if (v->sky_visible != 0) {
        const int32_t a = int32_t(pane->sprites[0]->colour & 0xff) - 0x10;
        if (-1 < a) alpha = a;
        alignas(16) _VECTOR head;
        Player_GetHeadPos(&head, obj, nullptr);
        // vsub.xyzw: a cabeça menos o vetor do sol, nas quatro componentes.
        sun.x = head.x - SunVector.x;
        sun.y = head.y - SunVector.y;
        sun.z = head.z - SunVector.z;
        sun.w = head.w - SunVector.w;
        if (Collide_LineOfSight(&head, &sun, obj->cel, obj, nullptr, 8)) b->night_vision = 1;
        if (0 < View_3DPoint2Screen(&sun, &sun, uint16_t(int16_t(b->player)))) {
            sun.x = (sun.x - v->width * 0.5f) * b->zoom + v->width * 0.5f;
            sun.y = (sun.y - v->height * 0.5f) * b->zoom + v->height * 0.5f;
            if (b->night_vision != 0 && -5.0f < sun.x && sun.x < 517.0f && -5.0f < sun.y && sun.y < 453.0f) {
                int32_t up = alpha + 0x20;
                if (0xff < up) up = 0xff;
                alpha = int16_t(up);
            }
        }
    }
    if (alpha < 1) {
        for (int32_t i = 0; i < 7; ++i) pane->sprites[i]->priority = -1;
        return;
    }
    const uint32_t colour = uint32_t(alpha) | 0xffffff00u;
    const float cx = v->width * 0.5f;
    const float cy = v->height * 0.5f;
    // Escala do tamanho da textura e posição ao longo da reta (1 = o sol, 0 = o centro, negativo além).
    static const float kSize[6] = {3.0f, 4.0f, 1.0f, 5.0f, 2.0f, 0.5f};
    static const float kPos[6] = {0.7f, 0.5f, 0.3f, -0.1f, -0.2f, -0.4f};
    for (int32_t i = 0; i < 6; ++i) {
        sprite* s = pane->sprites[i];
        if (i == 4) {
            s->w = int16_t(int32_t(float(uint16_t(s->tex_w)) + float(uint16_t(s->tex_w))));
            s->h = int16_t(int32_t(float(uint16_t(s->tex_h)) + float(uint16_t(s->tex_h))));
        } else if (i == 2) {
            s->w = int16_t(int32_t(float(uint16_t(s->tex_w))));
            s->h = int16_t(int32_t(float(uint16_t(s->tex_h))));
        } else {
            s->w = int16_t(int32_t(float(uint16_t(s->tex_w)) * kSize[i]));
            s->h = int16_t(int32_t(float(uint16_t(s->tex_h)) * kSize[i]));
        }
        s->x = int16_t(int32_t((sun.x - cx) * kPos[i] + cx));
        s->colour = colour;
        s->priority = 0x31;
        s->y = int16_t(int32_t((cy - sun.y) * kPos[i] + cy));
    }
    sprite* s = pane->sprites[6];
    float glare = 256.0f - std::sqrt((sun.x - cx) * (sun.x - cx) + (sun.y - cy) * (sun.y - cy));
    glare = (0.0f < glare ? glare : 0.0f) * 0.015625f;
    s->h = int16_t(int32_t(float(uint16_t(s->tex_h)) * glare));
    s->w = int16_t(int32_t(float(uint16_t(s->tex_w)) * glare));
    s->x = int16_t(int32_t((sun.x - cx) + cx));
    s->colour = colour;
    s->priority = 0x31;
    s->y = int16_t(int32_t((cy - sun.y) + cy));
}

// [decomp] 0x0019dec0 HUD_CreateRedeemer
// A tela do míssil Redeemer: encolhe como HUD_CreateShrink e, no modo história, cria os modelos 3D
// da mira (um por palavra extra, só na câmera 5, escondidos até a pane ligar).
// Só no port: sem a câmera 5 (testes) os modelos não entram numa sala.
void HUD_CreateRedeemer(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shrink_sprites(b, pane, create);
    auto* models = reinterpret_cast<P32<obj_tag>*>(nf::raw(pane->extra));
    for (uint32_t i = 0; i < create->extra; i = uint16_t(i + 1)) {
        if (mp_on() != 0) {
            models[i] = nullptr;
            continue;
        }
        celglist_tag* glist = hashtable_hashcode_to_celglist(0x200045e);
        obj_tag* m = Control_CreateObjEx(0, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR),
                                         const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), nullptr, glist, nullptr, 0, 0x20,
                                         2.0f, 0, 0xff, 0xff, 0xff);
        models[i] = m;
        if (m == nullptr) continue;  // só no port: sem objetos livres
        m->pos.z = 1.0f;
        m->unknown_f8 = 0x20;
        m->flags |= 0x10;
        if (glb_viewer[5] != nullptr) build_LinkToRoom(m, 0, glb_viewer[5]->world);
    }
    if (models[0] != nullptr) hashtable_set_object_to_entity_gfx(models[0], 0x200045e);
    if (models[1] != nullptr) hashtable_set_object_to_entity_gfx(models[1], 0x2000465);
    if (models[2] != nullptr) hashtable_set_object_to_entity_gfx(models[2], 0x2000465);
}

// [decomp] 0x0019e178 HUD_UpdateRedeemerPane
// A tela do Redeemer: mostra os modelos da mira (girados pelo míssil, BLData::ride), sai do modo de
// câmera quando o jogador não está no subestado 10, marca os helicópteros na tela (sprite 10,
// piscando no centro), mostra o combustível (sprite 2 sem míssil) e o ruído vermelho (sprite 1) que
// cresce acima de 85% do consumo.
void HUD_UpdateRedeemerPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) return;
    auto* models = reinterpret_cast<P32<obj_tag>*>(nf::raw(pane->extra));
    if (pane->enabled == 0) {
        for (int32_t i = 0; i < 3; ++i) {
            if (models[i] != nullptr) models[i]->flags |= 0x10;
        }
        return;
    }
    for (int32_t i = 0; i < 3; ++i) {
        if (models[i] != nullptr) models[i]->flags &= ~0x10u;
    }
    if (obj->unknown_f6 != 10) {
        Player_SetCamMode(b, 0);
        viewer_of(b)->cel = nullptr;
        pane_off(hud_of(b), kHudRedeemer);
    }
    const int32_t noise_r = int32_t(Rand_Rand(0x18));
    viewer_tag* v = viewer_of(b);
    sprite* marker = pane->sprites[10];
    marker->priority = -1;
    int32_t noise = int16_t(noise_r + 0x1b);
    for (LLNODE_tag* node = CopterList.first; node != nullptr; node = node->next) {
        obj_tag* body = Copter_GetBody(node);
        _VECTOR screen{};
        if (body == nullptr || View_3DPoint2Screen(&body->pos, &screen, v->index) < 1) continue;
        if (screen.x < 0.0f) screen.x = 0.0f;
        else if (512.0f < screen.x) screen.x = 512.0f;
        if (screen.y < 0.0f) screen.y = 0.0f;
        else if (448.0f < screen.y) screen.y = 448.0f;
        marker->x = int16_t(int32_t(screen.x - float(uint16_t(marker->tex_w))));
        marker->y = int16_t(int32_t((v->height - screen.y) - float(uint16_t(marker->tex_h))));
        marker->priority = 0x27;
        if (std::fabs(screen.x - 256.0f) < 32.0f && std::fabs(screen.y - 224.0f) < 32.0f && (GameState.frame & 8) != 0) {
            marker->priority = -1;
        }
    }
    if (b->cam_mode != 0xc) pane_off(hud_of(b), kHudRedeemer);
    obj_tag* ride = b->ride;
    float used;
    if (ride == nullptr) {
        used = 1.0f;
        pane->sprites[2]->priority = 0x27;
    } else {
        const uint8_t* data = static_cast<const uint8_t*>(ride->data);
        const float full = rd<float>(rd<P32<const uint8_t>>(data, 0x44), 0x1c);
        used = rd<float>(data, 0xf4);
        pane->sprites[2]->priority = -1;
        used = used / full;
        if (models[0] != nullptr && models[1] != nullptr && models[2] != nullptr) {
            models[0]->rot.z = (1.5707964f - ride->rot.z) + 3.1415927f;
            models[0]->mode |= 0x20;
            models[1]->rot.z = ride->rot.z - 0.7853982f;
            models[1]->mode |= 0x20;
            models[2]->rot.z = (ride->rot.z + 3.1415927f) - 0.7853982f;
            models[2]->mode |= 0x20;
        }
    }
    if (0.85f < used) noise = int16_t(int32_t(float(noise) + (used - 0.85f) * 3400.0f));
    pane->sprites[1]->colour = noise < 0x100 ? uint32_t(noise) | 0x7f000000u : 0x7f0000ffu;
    const uint8_t scroll = Scrl_272;
    ++Scrl_272;
    if ((scroll & 1) != 0) {
        sprite* s = pane->sprites[1];
        s->v = int16_t(Rand_Rand(uint32_t(int32_t(uint16_t(s->tex_h)) - 1)));
    }
}

// [decomp] 0x0019e6d0 HUD_UpdateCrossHair
// A mira: escondida com a cortina de sangue ou a câmera de segurança. Desliga as lunetas (Sight,
// Camera, OICW, Laser) e, mirando com uma arma de luneta (weapon_data +0x68 bit 0x40), liga a da arma
// (0x1a/0x1b OICW, 0x32/0x33 laser, 0x54 câmera, 0x55 vira 0x54, as outras a luneta comum) no lugar
// da mira. Sem mirar, o quadro de BLData::unknown_133[0] (mirando sem luneta, o 1) de
// HUDCrossCoords, verde na visão noturna, na posição da mira na tela (BLData::aim_cur_x/y); sem a
// opção de mira (PlayerSetting::unknown_08) só aparece mirando. Fora da primeira pessoa, escondida.
void HUD_UpdateCrossHair(BLData* b, sprite* sp) {
    sprite* s = sp;
    if (s == nullptr) return;
    HUDINFO_tag* hud = hud_of(b);
    if (hud->panes[kHudBlood].enabled != 0 || hud->panes[kHudSecCam].enabled != 0) {
        s->priority = -1;
        return;
    }
    const viewer_tag* v = viewer_of(b);
    ANIMOBJ_tag* anim = anim_of(glb_players[b->player]);
    const uint8_t aiming = uint8_t(anim->flags) & 1;
    pane_off(hud, kHudSight);
    pane_off(hud_of(b), kHudCamera);
    pane_off(hud_of(b), kHudOICW);
    pane_off(hud_of(b), kHudLaser);
    uint32_t frame;
    if (aiming == 0) {
        frame = b->unknown_133[0];
    } else if ((weapon_def(anim->weapon)->flags & 0x40) == 0) {
        frame = 1;
    } else {
        uint32_t scope;
        switch (int8_t(uint8_t(anim->weapon) - 0x1a)) {
            case 0:
            case 1:
                scope = kHudOICW;
                break;
            case 0x18:
            case 0x19:
                scope = kHudLaser;
                break;
            case 0x3b:
                anim->weapon = 0x54;
                anim->next_weapon = 0x54;
                scope = kHudCamera;
                break;
            case 0x3a:
                scope = kHudCamera;
                break;
            default:
                scope = kHudSight;
                break;
        }
        pane_on(hud_of(b), scope);
        s->priority = -1;
        return;
    }
    const int16_t* c = cross_coords(frame);
    s->u = c[0];
    s->v = c[1];
    s->w = c[2];
    s->uw = c[2];
    s->h = c[3];
    s->vh = c[3];
    s->colour = v->mode == 1 ? 0x208020ffu : 0xff0000ffu;
    const float aim_y = b->aim_cur_y;
    const float half_h = v->height * 0.5f;
    const float aim_x = b->aim_cur_x;
    const float half_w = v->width * 0.5f;
    s->priority = 0x31;
    s->y = int16_t(int32_t(half_h * -aim_y + half_h));
    s->x = int16_t(int32_t(half_w * aim_x + half_w));
    if (aiming == 0) s->priority = PlayerSetting[b->player].unknown_08 != 0 ? int8_t(0x31) : int8_t(-1);
    if (b->cam_mode != 0) s->priority = -1;
    // Só do port: na pose de mira moderna (engine/player_ads.h) a mira some; a alça e a massa fazem o papel dela.
    if (ads_hide_crosshair(b)) s->priority = -1;
}

// [decomp] 0x0019e9d8 HUD_MonitorNightSight
// A visão noturna e o raio X (modo da câmera 1 e 2): sacar os óculos (arma 0x5d) liga a visão
// noturna e devolve a arma anterior; a ação 13 (sem mirar) alterna nada -> noturna -> raio X ->
// nada (sem os óculos ainda, pede a arma 0x5d). A bateria (BLData::unknown_920) gasta FRAME_RATE_MUL
// por tick ligada e recarrega 3x mais rápido desligada, até 1800; vazia, desliga.
void HUD_MonitorNightSight(BLData* b) {
    obj_tag* p = glb_players[b->player];
    if (b == nullptr || b->active == 0) return;
    if (p->state != 1 || mp_on() != 0 || p->type == 0x12) return;
    const int16_t sub = int16_t(p->unknown_f6);
    if (sub != 0 && (sub < 3 || 5 < sub)) return;
    viewer_tag* v = viewer_of(b);
    if (v == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(p);
    if (anim->weapon == 0x5d) {
        if (b->gun->state != 0) return;
        v->mode = 1;
        pane_on(hud_of(b), kHudNightSight);
        anim->next_weapon = anim->prev_weapon;
        b->unknown_961 = 1;
        const int8_t next = anim->next_weapon;
        if (next == 0x47 || (0x46 < next && next < 0x5f && 0x5c < next)) anim->next_weapon = 1;
        return;
    }
    // Ação 13: alternar a visão (N no PC).
    if (Input_Action(b->player, 13, kActionPressed) != 0 && (anim->flags & 1) == 0) {
        if (b->unknown_961 == 0) {
            anim->next_weapon = 0x5d;
        } else if (v->mode == 0) {
            v->mode = 1;
            pane_on(hud_of(b), kHudNightSight);
        } else if (v->mode == 1) {
            v->mode = 2;
            pane_off(hud_of(b), kHudNightSight);
            pane_on(hud_of(b), kHudXray);
        } else if (v->mode == 2) {
            v->mode = 0;
            pane_off(hud_of(b), kHudNightSight);
            pane_off(hud_of(b), kHudXray);
        }
    }
    float battery;
    if (v->mode == 0) {
        battery = b->unknown_920 + FRAME_RATE_MUL * 3.0f;
        if (1800.0f <= battery) battery = 1800.0f;
    } else {
        battery = b->unknown_920 - FRAME_RATE_MUL;
    }
    b->unknown_920 = battery;
    if (0.0f < b->unknown_920) return;
    if (2 < v->mode || v->mode == 0) return;
    b->unknown_920 = 0.0f;
    v->mode = 0;
    pane_off(hud_of(b), kHudNightSight);
    pane_off(hud_of(b), kHudXray);
}

// [decomp] 0x0019ed10 HUD_CreateMPScorePane
// Multijogador: o placar (sprite 0) e os ícones do modo de jogo (1-6), arrumados pelo tipo de
// partida (MPSettings +0x1a4) e pelos times.
void HUD_CreateMPScorePane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shift_sprites(b, pane, create);
    P32<sprite>* s = pane->sprites;
    for (int32_t i = 0; i < 7; ++i) HUD_FixMP(s[i], int16_t(b->player));
    const uint32_t game = mp_u32(0x1a4);
    if (game == 0x400) {
        s[1]->y = int16_t(s[1]->y - 0x10);
        s[2]->y = int16_t(s[2]->y - 0x20);
    } else if (game == 0x60000008) {
        s[1]->y = int16_t(s[1]->y - 0x30);
        s[2]->y = int16_t(s[2]->y - 0x30);
        s[6]->y = int16_t(s[6]->y - 0x30);
        s[2]->x = int16_t(s[1]->x + 0x10);
        s[6]->x = int16_t(s[2]->x + 0x10);
        hashtable_set_sprite(s[1], 0x3000185);
        hashtable_set_sprite(s[2], 0x3000185);
        hashtable_set_sprite(s[6], 0x3000185);
    } else {
        s[1]->y = int16_t(s[1]->y - 0x18);
        s[2]->y = int16_t(s[2]->y - 0x18);
    }
    FuckOffSprite3 = 0;
    s[0]->x = int16_t(s[0]->x + 0x18);
    s[0]->y = int16_t(s[0]->y - 3);
    if (mp_u32(0x18c) == 0 && mp_u32(0x190) == 0 && game != 0x400) {
        s[3]->priority = -1;
        FuckOffSprite3 = 1;
        s[3]->y = int16_t(s[3]->y - 0x11);
    } else {
        s[0]->y = int16_t(s[0]->y + 0x11);
    }
    s[4]->y = int16_t(s[3]->y + 0x12);
    s[5]->y = int16_t(s[4]->y + 0x12);
}

// [decomp] 0x0019f0a0 HUD_MPUpdatePane
// Multijogador: o placar do time e do jogador (sprite 0) e os ícones do modo de jogo (bandeira,
// espionagem, assassino/alvo, objetos do GoldenEye, os três uplinks).
void HUD_MPUpdatePane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (pane->enabled == 0) return;
    if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) return;
    P32<sprite>* sprites = pane->sprites;
    sprite* score = sprites[0];
    obj_tag* me = rd<P32<obj_tag>>(MPGame, uint32_t(b->player) * 0x30 + 0x1c);
    sprite* icon_a = sprites[1];
    sprite* icon_b = sprites[2];
    sprite* icon_c = sprites[6];
    const uint16_t team = rd<uint16_t>(MPSettings, uint32_t(b->player) * 0x30 + 0x20);
    if (me != nullptr && (me->type == 2 || me->type == 0x11)) return;
    if (FuckOffSprite3 != 0) sprites[3]->priority = -1;
    const char* team_score = MP_GetTeamScore(team);
    const char* player_score = MP_GetPlayerScore(uint16_t(int16_t(b->player)));
    const uint32_t team_colour = team == 0 ? 0xff0000ffu : 0x0000ffffu;
    if (score->text != nullptr) {
        if (team_score != nullptr && player_score != nullptr) {
            std::snprintf(score->text, kStringSize, "%s\n%s", team_score, player_score);
            score->colour2 = team_colour;
        } else {
            if (player_score != nullptr) {
                std::snprintf(score->text, kStringSize, "%s", player_score);
            } else {
                score->text[0] = 0;
            }
            score->colour2 = 0xff;
        }
    }
    Sprite_SetText(score, score->text, 0);
    icon_a->priority = -1;
    icon_b->priority = -1;
    icon_c->priority = -1;
    const uint32_t game = mp_u32(0x1a4);
    const auto assassin_icons = [&]() {
        if (MP_IsAssasin(me)) {
            icon_a->priority = 0x1e;
            hashtable_set_sprite(icon_a, 0x3000180);
        }
        if (MP_IsTarget(me)) {
            icon_b->priority = 0x1e;
            hashtable_set_sprite(icon_b, 0x3000189);
        }
    };
    if (game == 0x20000100) {
        if (MP_HasEsponage(me) == 0) return;
        icon_a->priority = 0x1e;
        hashtable_set_sprite(icon_a, 0x300018a);
        return;
    }
    if (game < 0x20000100) {
        if (game == 0x400) {
            assassin_icons();
            return;
        }
        if (game != 0x20000004) return;
        if (MP_HasTeamFlag(me, team == 0 ? 1 : 0) == 0) return;
        icon_a->priority = 0x1e;
        hashtable_set_sprite(icon_a, 0x3000182);
        icon_a->colour = team == 0 ? 0x0040ffffu : 0xff0000ffu;
        return;
    }
    if (game == 0x20000200) {
        if (MP_ObjTeamHasGEObj(me, 0) != 0) {
            icon_a->priority = 0x1e;
            hashtable_set_sprite(icon_a, 0x300018b);
        }
        if (MP_ObjTeamHasGEObj(me, 1) != 0) {
            icon_b->priority = 0x1e;
            hashtable_set_sprite(icon_b, 0x300018c);
        }
        assassin_icons();
        return;
    }
    if (game != 0x60000008) return;
    icon_a->priority = 0x1e;
    icon_b->priority = 0x1e;
    icon_c->priority = 0x1e;
    sprite* uplinks[3] = {icon_a, icon_b, icon_c};
    for (int16_t i = 0; i < 3; ++i) {
        const int32_t status = MP_getUplinkStatus(i);
        uint32_t colour;
        if (status == 1) colour = 0x2d61d2ffu;
        else if (status == 0) colour = 0xd22d35ffu;
        else if (status == 2) colour = 0x7f7f7fffu;
        else if (i == 2) return;
        else continue;
        uplinks[i]->colour = colour;
    }
}

// [decomp] 0x0019f4e8 HUD_CreateRadar
// Multijogador: o radar (sprite 0) e um ponto por palavra extra (Sprite_CreateLink na câmera 5,
// escondidos).
void HUD_CreateRadar(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shift_sprites(b, pane, create);
    HUD_FixMP(pane->sprites[0], int16_t(b->player));
    auto* dots = reinterpret_cast<P32<sprite>*>(nf::raw(pane->extra));
    for (uint32_t i = 0; i < create->extra; i = uint16_t(i + 1)) {
        sprite* s = Sprite_CreateLink(5);
        dots[i] = s;
        if (s == nullptr) continue;  // só no port: sem memória
        Sprite_SetParams(s, 0, pane->x, pane->y, 0x300016f, 0x5000ff);
        s->w = 2;
        s->h = 2;
        s->priority = -1;
        s->flags |= 0x900;
    }
}

// [decomp] 0x0019f6e8 HUD_RadarUpdate
// Multijogador: os objetos do radar (MP_GetRadarObjects, na metade dos pontos) na direção relativa
// à câmera, até 50 m na borda; depois o nome de cada outro jogador visível na tela (opção de nomes) e
// as marcas de assassino e alvo.
void HUD_RadarUpdate(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (obj != nullptr && (obj->type == 2 || obj->type == 0x11)) return;
    sprite* radar = pane->sprites[0];
    const int16_t radar_h = radar->h;
    viewer_tag* v = viewer_of(b);
    auto* dots = reinterpret_cast<P32<sprite>*>(nf::raw(pane->extra));
    const float half_w = float(radar->w) * 0.45f;
    Mat_Inverse(&v->camera, &InvM_292);
    RADAROBJ* list = nullptr;
    const uint32_t objects = MP_GetRadarObjects(obj, &list);
    radar->priority = rd<int32_t>(MPSettings, uint32_t(b->player) * 0x30 + 0x28) != 0 ? int8_t(0x1e) : int8_t(-1);
    const uint16_t max_dots = pane->create->extra;
    uint32_t used = 0;
    if (objects != 0 && max_dots != 0) {
        uint32_t k = 0;
        do {
            const auto* r = reinterpret_cast<const RadarObj*>(list) + k;
            RotPreTransVec(&InvM_292, &r->pos, &Target_293);
            float len = Vec_NormaliseLen(&Target_293, &Target_293);
            Target_293.z = -Target_293.z;
            Target_293.y = -1.0f;
            Target_293.x = -Target_293.x;
            const float rise = (r->pos.y - obj->matrix.m[3][1]) * 0.125f;
            if (-1.0f <= rise) Target_293.y = rise < 1.0f ? rise : 1.0f;
            len = len * 0.02f;
            if (1.0f < len) len = 1.0f;
            sprite* d = dots[used];
            k = uint16_t(k + 1);
            used = uint16_t(used + 1);
            d->x = int16_t(int32_t(Target_293.x * len * half_w + float(radar->x) + 0.5f + float(radar->w) * 0.5f));
            d->y = int16_t(int32_t(Target_293.z * len * float(radar_h) * 0.45f + float(radar->y) + 0.5f +
                                   float(radar->h) * 0.5f));
            const uint8_t* uv = TexUV_291 + uint32_t(r->icon) * 0x14;
            d->u = rd<int16_t>(uv, 0);
            d->v = rd<int16_t>(uv, 4);
            d->w = rd<int16_t>(uv, 8);
            d->uw = d->w;
            d->h = rd<int16_t>(uv, 0xc);
            d->vh = d->h;
            d->colour = r->colour;
            d->priority = 0x1c;
        } while (k < objects && used < pane->create->extra);
    }
    while (used < uint32_t(pane->create->extra >> 1)) {
        dots[used]->priority = -1;
        used = uint16_t(used + 1);
    }
    sprite* assassin = nullptr;
    sprite* target_mark = nullptr;
    const uint32_t game = mp_u32(0x1a4);
    if (game == 0x400) {
        assassin = rd<P32<sprite>>(MPGame, uint32_t(b->player) * 8 + 0x1b4);
        target_mark = rd<P32<sprite>>(MPGame, uint32_t(b->player) * 8 + 0x1b0);
        if (assassin != nullptr) assassin->priority = -1;
        if (target_mark != nullptr) target_mark->priority = -1;
    }
    obj_tag* me = rd<P32<obj_tag>>(MPGame, uint32_t(b->player) * 0x30 + 0x1c);
    static const char kNameFmt[] = "\xff\x02";
    for (int32_t i = 0; i < 8; ++i) {
        if (mp_u32(0x1c4) == 0 && game != 0x400) break;
        obj_tag* other = rd<P32<obj_tag>>(MPGame, uint32_t(i) * 0x30 + 0x1c);
        if (i == b->player || other == nullptr) continue;
        const uint8_t type = other->type;
        if (type == 3) {
            if (other->state != 1) continue;
        } else if (type == 2) {
            const uint32_t bits = rd<uint32_t>(other->data, 0x4f8);
            bool skip = true;
            if ((bits & 0x600) == 0 && (bits & 0x100) != 0 && 0.0f < rd<float>(other->data, 0xac) && (other->life & 1) == 0) {
                skip = false;
            }
            if (skip) continue;
        } else if (type == 0x11 || type == 0x12) {
            continue;
        }
        _VECTOR screen{v->width, v->height, 0.0f, 0.0f};
        if (View_3DPoint2Screen(&other->pos, &screen, uint16_t(int16_t(b->player))) <= 0) continue;
        float extent[2] = {0.0f, 0.0f};
        Font_GetTextExtent(reinterpret_cast<char*>(MPSettings + i * 0x30), const_cast<char*>(kNameFmt),
                           reinterpret_cast<GPOINT*>(extent), nullptr, -1);
        float half = extent[0] * 0.5f;
        if (100.0f <= screen.z) continue;
        if (!(half < screen.x && screen.x < v->width - half)) continue;
        if (!Collide_LineOfSight(&other->pos, &me->pos, other->cel, other, me, 0x623)) continue;
        // Só no port: sem pontos livres o original escreveria além das palavras extra da pane.
        if (mp_u32(0x1c4) != 0 && used < pane->create->extra) {
            sprite* name = dots[used];
            name->format = const_cast<char*>(kNameFmt);
            Sprite_SetText(name, reinterpret_cast<char*>(MPSettings + i * 0x30), 0);
            float y = 12.0f;
            if (12.0f <= screen.y) {
                const float limit = v->height - 12.0f;
                y = screen.y < limit ? screen.y : limit;
            }
            name->x = int16_t(int32_t((screen.x - half) + v->viewport[0]));
            name->y = int16_t(int32_t((v->height - y) + v->viewport[2]));
            name->priority = 0x1e;
            if (mp_u32(0x18c) == 0) {
                name->colour = 0x1e504bffu;
            } else {
                name->colour = rd<int32_t>(MPSettings, uint32_t(i) * 0x30 + 0x20) == 0 ? 0x5a1414ffu : 0x14145affu;
            }
            used = uint16_t(used + 1);
        }
        if (game != 0x400) continue;
        float w = 80.0f, h = 16.0f;
        sprite* mark = assassin;
        if (!MP_IsAssasin(other)) {
            mark = nullptr;
            if (MP_IsTarget(other)) {
                h = 40.0f;
                w = 40.0f;
                mark = target_mark;
            }
        }
        if (mark == nullptr) continue;
        const int16_t mx = int16_t(int32_t((screen.x - w * 0.5f) + v->viewport[0]));
        mark->x = mx;
        const int16_t my = int16_t(int32_t((v->height - screen.y) + v->viewport[2]));
        mark->y = my;
        const float rel_y = float(int32_t(my)) - v->viewport[2];
        if (rel_y < 0.0f) continue;
        if (v->height - h <= rel_y || float(int32_t(mx)) - v->viewport[0] < 0.0f) continue;
        if (rel_y < v->width - w) mark->priority = 0x1e;
    }
    for (; used < pane->create->extra; used = uint16_t(used + 1)) {
        if (dots[used] != nullptr) dots[used]->priority = -1;
    }
}

// [decomp] 0x001a0bb8 HUD_CreateShrink
// Panes de tela cheia (luneta, cortina de sangue...): numa tela menor que a do PS2 cada sprite fica
// com a metade do tamanho.
void HUD_CreateShrink(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    shrink_sprites(b, pane, create);
}

// [decomp] 0x001a0d08 HUD_DisableAll
void HUD_DisableAll(BLData* b) {
    for (uint32_t i = 0; i < kHudPanes; i = uint16_t(i + 1)) hud_of(b)->panes[i].enabled = 0;
}

// [decomp] 0x001a0d38 HUD_Reset
// Desliga as panes que só aparecem em situações especiais.
void HUD_Reset(BLData* b) {
    static const uint32_t kOff[] = {kHudSight,   kHudNightSight, kHudAir,  kHudRedeemer, kHudRCCar,
                                    kHudCamera,  kHudBlood,      kHudXray, kHudSecCam,   kHudOICW,
                                    kHudRonin,   kHudLaser,      kHudSpace};
    for (const uint32_t p : kOff) HUD_Enable(b->hud, p, 0, 0);
}

// [decomp] 0x001a0e60 HUD_Enable
void HUD_Enable(void* hud, uint32_t pane, int8_t on, uint16_t state) {
    if (hud == nullptr || kHudPanes <= pane) return;
    HUDPANE_tag& p = static_cast<HUDINFO_tag*>(hud)->panes[pane];
    if (p.valid == 0) return;
    p.state = state;
    p.enabled = uint8_t(on);
}

// [decomp] 0x001a0ea8 HUD_State
uint32_t HUD_State(void* hud, uint32_t pane) {
    if (hud == nullptr) return 0;
    if (pane < kHudPanes) return static_cast<HUDINFO_tag*>(hud)->panes[pane].state;
    return 0;
}

// [decomp] 0x001a0ed8 HUD_CreateAmmoPane
void HUD_CreateAmmoPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
}

// [decomp] 0x001a0f40 HUD_CreateHealthPane
// Os indicadores de direção do tiro (sprites 20-23) cobrem a tela do PS2.
void HUD_CreateHealthPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    HUD_CalcWidthHeight(create, reinterpret_cast<int16_t*>(&create->w), reinterpret_cast<int16_t*>(&create->h));
    HUD_CreateDefault(b, pane, create, obj);
    for (int32_t i = 0x14; i < 0x18; ++i) {
        sprite* s = pane->sprites[i];
        s->x = 0;
        s->y = 0;
        s->w = 0x200;
        s->h = 0x1c0;
    }
    ThirdIconTimer = 0;
    CrouchIconTimer = 0;
}

namespace {
// As panes de mensagem: passo 0, tipo de mensagem do Text_* e velocidade da faixa.
void create_status(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj, uint32_t type, uint32_t speed) {
    HUD_CreateDefault(b, pane, create, obj);
    pane->extra[1] = 0;
    pane->extra[2] = type;
    pane->extra[3] = speed;
}
}  // namespace

// [decomp] 0x001a1028 HUD_CreateMissionStatusPane
void HUD_CreateMissionStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    create_status(b, pane, create, obj, 3, 0x20);
}

// [decomp] 0x001a1070 HUD_CreateObjectiveStatusPane
void HUD_CreateObjectiveStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    create_status(b, pane, create, obj, 2, 0x10);
}

// [decomp] 0x001a10b8 HUD_CreateInfoStatusPane
void HUD_CreateInfoStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    create_status(b, pane, create, obj, 1, 0x10);
    if (mp_on() == 0) return;
    HUD_FixMP(pane->sprites[0], int16_t(b->player));
    pane->sprites[0]->y = int16_t(pane->sprites[0]->y + 0x48);
}

// [decomp] 0x001a1140 HUD_CreatePickupStatusPane
void HUD_CreatePickupStatusPane(BLData* b, HUDPANE_tag* pane, HUDPANECREATE_tag* create, obj_tag* obj) {
    create_status(b, pane, create, obj, 6, 0x10);
    if (mp_on() == 0) return;
    HUD_FixMP(pane->sprites[0], int16_t(b->player));
    pane->sprites[0]->y = int16_t(pane->sprites[0]->y + 0x48);
}

// [decomp] 0x001a11c8 HUD_UpdateAirPane
// O fôlego debaixo d'água (BLData::water_flags 0x10; no subestado 6, o fio, 100 - unknown_8cc):
// a barra (sprite 1) de 28 a 228 pixels.
void HUD_UpdateAirPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    sprite* frame = pane->sprites[0];
    sprite* bar = pane->sprites[1];
    sprite* wire = pane->sprites[2];
    if (pane->enabled == 0) return;
    const float air = obj->unknown_f6 == 6 ? 100.0f - b->unknown_8cc : b->air;
    if ((b->water_flags & 0x10) == 0) {
        bar->priority = -1;
        frame->priority = -1;
        wire->priority = -1;
    } else {
        bar->priority = 0x1e;
        frame->priority = 0x1f;
        if (obj->unknown_f6 == 6) {
            bar->colour = 0x68d6b1ffu;
            wire->priority = 0x1f;
        } else {
            bar->colour = 0x68b1d6ffu;
            wire->priority = -1;
        }
    }
    set_bar(bar, air * 0.01f);
}

// [decomp] 0x001a12d0 HUD_UpdateSightPane
void HUD_UpdateSightPane(BLData*, HUDPANE_tag*, obj_tag*) {}

// [decomp] 0x001a12d8 HUD_UpdateCameraPane
// A câmera espiã: o clarão do disparo (BLData::muzzle_flash) acende o sprite 0 por 10 ticks; cada
// inimigo vivo na tela (fora do estado 1) ganha um marcador (objeto tipo 0x10, entidade 0x2000832).
void HUD_UpdateCameraPane(BLData* b, HUDPANE_tag* pane, obj_tag* obj) {
    if (pane->enabled == 0) return;
    uint8_t mode = 0xff;
    if (b->muzzle_flash != 0) {
        pane->extra[0] = 10;
        mode = 0x28;
    } else if (pane->extra[0] != 0) {
        pane->extra[0] = pane->extra[0] - 1;
        if (pane->extra[0] != 0) mode = 0x28;
    }
    pane->sprites[0]->priority = mode;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        auto* d = reinterpret_cast<Drone_tag*>(n);
        obj_tag* o = d->obj;
        if (o->unknown_fc == 0 || !(0.0f < d->health) || o->state == 1) continue;
        obj_tag* m = control_create_object(0x40, &o->pos, nullptr, nullptr);
        auto* data = static_cast<uint8_t*>(m->data);
        m->type = 0x10;
        data[0x32] = 0;
        const int16_t zero = 0;
        std::memcpy(data + 0x30, &zero, 2);
        hashtable_set_object_to_entity_gfx(m, 0x2000832);
        m->owner = obj;
        m->flags |= 0x2000;
    }
}

// [decomp] 0x001a1410 HUD_UpdateXRayPane
// O raio X (modo 2 da câmera): ruído azul, a varredura (v do sprite 0 andando) e a bateria (sprite 3).
void HUD_UpdateXRayPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    if (pane->enabled == 0) return;
    if (viewer_of(b)->mode != 2) {
        pane->enabled = 0;
        return;
    }
    if (scene_camera_active()) return;
    sprite* noise = pane->sprites[0];
    noise->colour = (Rand_Rand(0x18) + 7u) | 0xffffff00u;
    noise->v = int16_t((noise->v + 1) & 0x1f);
    set_bar(pane->sprites[3], b->unknown_920 * 0.00055555557f);
}

// [decomp] 0x001a1538 HUD_UpdateSecCamPane
// A câmera de segurança: sem visão especial e o "REC" (sprite 6) piscando.
void HUD_UpdateSecCamPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    if (pane->enabled == 0) return;
    viewer_of(b)->mode = 0;
    pane->sprites[6]->colour = (GameState.frame & 0x1f) < 0x10 ? 0x7f7f7f78u : 0x7f7f7f00u;
}

// [decomp] 0x001a15a0 HUD_UpdateCarPane
// O carrinho de controle remoto: as luzes do modo (sprites 1-5) pelo modelo do carrinho.
void HUD_UpdateCarPane(BLData* b, HUDPANE_tag* pane, obj_tag*) {
    obj_tag* car = b->ride;
    if (pane->enabled == 0 || car == nullptr) return;
    P32<sprite>* s = pane->sprites;
    if (car->glist == hashtable_hashcode_to_celglist(0x2000194)) {
        s[0]->colour = 0x20ffu;
        s[1]->priority = -1;
        s[2]->priority = -1;
        s[3]->priority = 0x27;
        s[4]->priority = 0x27;
        s[5]->priority = 0x27;
    } else {
        s[0]->colour = 0x2000ffu;
        s[1]->priority = 0x27;
        s[2]->priority = 0x27;
        s[3]->priority = -1;
        s[4]->priority = -1;
        s[5]->priority = -1;
    }
}

namespace {

template <class F>
uint32_t code(F* fn) {
    return nf::p32_fn_u(fn);
}

struct HudCodeName {
    const char* name;
    uint32_t address;
};

}  // namespace

uint32_t Hud_ResolveCode(const char* symbol_name, uint32_t) {
    static const HudCodeName kCodes[] = {
        {"HUD_CreateDefault__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateDefault)},
        {"HUD_CreateAmmoPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateAmmoPane)},
        {"HUD_CreateHealthPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateHealthPane)},
        {"HUD_CreateMissionStatusPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag",
         code(HUD_CreateMissionStatusPane)},
        {"HUD_CreateObjectiveStatusPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag",
         code(HUD_CreateObjectiveStatusPane)},
        {"HUD_CreateInfoStatusPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateInfoStatusPane)},
        {"HUD_CreatePickupStatusPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag",
         code(HUD_CreatePickupStatusPane)},
        {"HUD_CreateShrink__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateShrink)},
        {"HUD_CreateOICWPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateOICWPane)},
        {"HUD_CreateRedeemer__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateRedeemer)},
        {"HUD_CreateMPHealthPane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateMPHealthPane)},
        {"HUD_CreateMPScorePane__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateMPScorePane)},
        {"HUD_CreateRadar__FP6BLDataP11HUDPANE_tagP17HUDPANECREATE_tagP7obj_tag", code(HUD_CreateRadar)},
        {"HUD_UpdateAmmoPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateAmmoPane)},
        {"HUD_UpdateHealthPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateHealthPane)},
        {"HUD_UpdateStatusPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateStatusPane)},
        {"HUD_UpdateAirPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateAirPane)},
        {"HUD_UpdateSightPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateSightPane)},
        {"HUD_UpdateNightSightPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateNightSightPane)},
        {"HUD_UpdateLensFlarePane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateLensFlarePane)},
        {"HUD_UpdateRedeemerPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateRedeemerPane)},
        {"HUD_UpdateCarPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateCarPane)},
        {"HUD_UpdateCameraPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateCameraPane)},
        {"HUD_UpdateBloodPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateBloodPane)},
        {"HUD_UpdateXRayPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateXRayPane)},
        {"HUD_UpdateSecCamPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateSecCamPane)},
        {"HUD_UpdateOICWPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateOICWPane)},
        {"HUD_UpdateSpacePane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateSpacePane)},
        {"HUD_UpdateMPHealthPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_UpdateMPHealthPane)},
        {"HUD_MPUpdateStatusPane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_MPUpdateStatusPane)},
        {"HUD_MPUpdatePane__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_MPUpdatePane)},
        {"HUD_RadarUpdate__FP6BLDataP11HUDPANE_tagP7obj_tag", code(HUD_RadarUpdate)},
    };
    for (const HudCodeName& c : kCodes) {
        if (std::strcmp(c.name, symbol_name) == 0) return c.address;
    }
    return 0;
}

// ---- só no port ----

uint32_t HUD_PaneCount(const BLData* b) {
    if (b == nullptr || b->hud == nullptr) return 0;
    uint32_t n = 0;
    for (const HUDPANE_tag& p : hud_of(b)->panes) n += p.valid != 0 ? 1u : 0u;
    return n;
}

int HUD_Describe(const BLData* b, char* out, size_t size) {
    if (b == nullptr || b->hud == nullptr) return std::snprintf(out, size, "sem_hud");
    const HUDINFO_tag* hud = hud_of(b);
    int n = std::snprintf(out, size, "paineis");
    const auto put = [&](const char* fmt, auto... args) {
        if (n >= 0 && size_t(n) < size) n += std::snprintf(out + n, size - size_t(n), fmt, args...);
    };
    bool first = true;
    for (uint32_t i = 0; i < kHudPanes; ++i) {
        if (hud->panes[i].enabled == 0) continue;
        put(first ? " %u" : ",%u", i);
        first = false;
    }
    if (first) put(" nenhum");
    // Os segmentos acesos das barras: o alfa acima do meio entre o aceso e o apagado de cada barra
    // (vida: 0,55 e 0,15; colete: 0,75 e 0,35, vezes o esmaecimento BLData::unknown_8bc).
    float fade = b->unknown_8bc * 4.0f;
    if (1.0f <= fade) fade = 1.0f;
    const HUDPANE_tag& health = hud->panes[kHudHealth];
    int life = -1, armour = -1;
    if (health.valid != 0 && 15 <= health.count) {
        life = 0;
        armour = 0;
        for (uint32_t i = 0; i < 7; ++i) {
            const sprite* s = health.sprites[8 + i];
            if (s->priority != -1 && float(s->colour & 0xff) > 0.35f * fade * 255.0f) ++life;
        }
        for (uint32_t i = 0; i < 8; ++i) {
            const sprite* s = health.sprites[i];
            if (s->priority != -1 && float(s->colour & 0xff) > 0.55f * fade * 255.0f) ++armour;
        }
    }
    put(" vida %.0f (%d/7) colete %.0f (%d/8)", double(b->health), life, double(b->armour), armour);
    const HUDPANE_tag& ammo = hud->panes[kHudAmmo];
    const sprite* count = ammo.valid != 0 && ammo.count != 0 ? ammo.sprites[0] : nullptr;
    // O texto que a pane escreveu (Sprite_SetText).
    // nf::raw: com o P32 de um lado e o texto literal do outro, o GCC e o clang convertiam o literal num P32 (passado ao
    // %s como os 4 bytes do campo); assim os dois lados são const char*.
    put(" municao %s",
        count != nullptr && count->text != nullptr && count->text[0] != 0 ? nf::raw(count->text) : "-");
    int arrows = 0;
    if (health.valid != 0 && 0x18 <= health.count) {
        for (uint32_t i = 0x14; i < 0x18; ++i) arrows += health.sprites[i]->priority != -1 ? 1 : 0;
    }
    put(" sangue %u (%d)", uint32_t(b->pain_flash), arrows);
    const sprite* cross = hud->crosshair;
    put(" mira %d", cross != nullptr && cross->priority != -1 ? 1 : 0);
    const HUDPANE_tag& blood = hud->panes[kHudBlood];
    put(" cortina %u/%u", uint32_t(blood.enabled), uint32_t(blood.state));
    return n;
}

void hud_debug_power_on() {
    std::memset(PaneList, 0, sizeof(PaneList));
    std::memset(MPPaneList, 0, sizeof(MPPaneList));
    std::memset(&CrossHair, 0, sizeof(CrossHair));
    CrossHair.label = 0xffffffffu;
    HudMsgMissionStatusPane = nullptr;
    HudMsgObjectiveStatusPane = nullptr;
    ttimer = ctimer = 0;
    ThirdIconTimer = CrouchIconTimer = 0;
    OICW_timer = OICW_mode = 0;
    FuckOffSprite3 = 0;
    Scrl_247 = Scrl_272 = 0;
    IsNew_224 = IsComplete_225 = 0;
    ObjectiveSFXTriggered1_220 = ObjectiveSFXTriggered2_221 = 0;
    ObjectiveSFXPlayMe1st_222 = ObjectiveSFXPlayMe2nd_223 = 0;
}
