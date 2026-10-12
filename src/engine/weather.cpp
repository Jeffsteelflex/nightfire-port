// A chuva, a neve, o vento das salas, os raios e as nuvens. Ver weather.h.
//
// Divergências do original (só no port; também nas funções):
// - UpdateDrops atualiza as gotas no lugar: o original copia lotes de até 992 para a scratchpad (psiCopyToSP) e de
//   volta (psiCopyFromSP), o que dá no mesmo (992 é múltiplo de 32, o índice de RandTable2 não muda); sem o tempo dela
//   (UpdateDropsTime, UpdateT0Count).
// - Sem a câmera 4, UpdateDrops usa a 0 (o original leria o campo de um ponteiro nulo); DrawDrops sem o modelo ou a
//   textura das gotas não desenha (o original leria o endereço 0); Lightning_Create pula um segmento que não pôde ser
//   criado (o original escreveria no endereço 0xff). Contados em g_weather_guards.
// - Lightning_Update só sorteia os pontos do meio (FractalThing, em player_weapon.cpp) com os oito segmentos criados
//   (o original escreveria no segmento nulo).

#include "engine/weather.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/bullet.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/modules.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi_draw.h"

uint8_t DropType = 0xff;
int16_t CurrentMax = 0;
int16_t Added = 0;
uint8_t InitBodge = 0;
obj_tag* RainBoxRoot = nullptr;
alignas(16) _VECTOR DropVector[kMaxDrops];
uint8_t DropState[kMaxDrops];
alignas(16) _VECTOR RandTable1[32];
alignas(16) _VECTOR RandTable2[32];
alignas(16) _VECTOR WindVector;
alignas(16) _VECTOR CubeCentre;
alignas(16) _VECTOR PrevPos;
alignas(16) _VECTOR WindVector2;
WeatherStats g_weather_stats;
uint32_t g_weather_guards = 0;
uint8_t g_port_no_env = 0;

namespace {
RAINBOX_tag* box_of(obj_tag* o) { return static_cast<RAINBOX_tag*>(o->data); }
LIGHTNING_tag* bolt_of(obj_tag* o) { return static_cast<LIGHTNING_tag*>(o->data); }
}  // namespace

void weather_debug_power_on() {
    DropType = 0xff;
    CurrentMax = 0;
    Added = 0;
    InitBodge = 0;
    RainBoxRoot = nullptr;
    std::memset(DropVector, 0, sizeof(DropVector));
    std::memset(DropState, 0, sizeof(DropState));
    std::memset(RandTable1, 0, sizeof(RandTable1));
    std::memset(RandTable2, 0, sizeof(RandTable2));
    WindVector = CubeCentre = PrevPos = WindVector2 = _VECTOR{};
    g_weather_stats = WeatherStats{};
    g_weather_guards = 0;
}

// ------------------------------------------------------------------ gotas

// [decomp] 0x0018bf68 InitDrops
// As 32 velocidades por tick (RandTable1: x e z em +-0,02, ou +-0,002 nas fases 07000007-08; y de -0,025 a -0,1; o
// tipo 2 com um quarto e para cima), todas as gotas mortas em (0, 0, -1024), o vento zerado (y = -0,5 no tipo 1), o
// máximo do tipo e InitBodge nas fases 07000005-06.
void InitDrops(uint8_t kind) {
    if (g_port_no_env != 0) return;  // só do port: --no-env (depuração), como antes do porte
    DropType = kind;
    for (int i = 0; i < 32; ++i) {
        const uint32_t level = GameState.level_file;
        _VECTOR& r = RandTable1[i];
        if (0x7000008u < level || level < 0x7000007u) {
            r.x = Rand_FRand(0.04f) - 0.02f;
            r.y = -(Rand_FRand(0.075f) + 0.025f);
            r.z = Rand_FRand(0.04f) - 0.02f;
        } else {
            r.x = Rand_FRand(0.004f) - 0.002f;
            r.y = -(Rand_FRand(0.075f) + 0.025f);
            r.z = Rand_FRand(0.004f) - 0.002f;
        }
        if (DropType == 2) {
            r.x *= 0.25f;
            r.y *= -0.25f;
            r.z *= 0.25f;
        }
    }
    for (int i = 0; i < kMaxDrops; ++i) {
        DropVector[i].x = 0.0f;
        DropVector[i].y = 0.0f;
        DropVector[i].z = -1024.0f;
        DropState[i] = 0;
    }
    WindVector.x = 0.0f;
    WindVector.z = 0.0f;
    if (DropType == 1) {
        WindVector.y = -0.5f;
        CurrentMax = GameState.level_file - 0x7000009u < 3 ? 0x80 : 0x40;
    } else {
        CurrentMax = DropType == 0 ? 0x1000 : 0x800;
        WindVector.y = 0.0f;
    }
    PrevPos.x = PrevPos.y = PrevPos.z = -999.0f;
    const uint32_t level = GameState.level_file;
    InitBodge = (level <= 0x7000006u && 0x7000005u <= level) ? 1 : 0;
}

// [decomp] 0x0018c278 AddDrop
// Uma gota nova em drops[index] (state[index + base] = 1 se ficar): no cubo em volta de CubeCentre (x e z +-15; y de
// 7,5 a 15 acima, ou +-15 no tipo 2), só dentro de uma caixa de chuva ativa (RainBox_Clip): o fim (w) é o chão da caixa
// ou a câmera - 7,5 m (o tipo 2 sobe: começa no chão da caixa e acaba no teto, ou câmera + 7,5); deslocada pelo vento
// do caminho que vai cair. Fora de toda caixa ela desce 1054 m e não nasce. Added conta as tentativas.
void AddDrop(int16_t index, int32_t base, uint8_t* state, _VECTOR* drops) {
    ++Added;
    _VECTOR& d = drops[index];
    d = CubeCentre;
    d.x += Rand_FRand(30.0f) - 15.0f;
    d.z += Rand_FRand(30.0f) - 15.0f;
    const float dy = DropType == 2 ? Rand_FRand(30.0f) - 15.0f : Rand_FRand(7.5f) + 7.5f;
    d.y += dy;
    bool found = false;
    for (obj_tag* b = RainBoxRoot; b != nullptr && !found; b = b->owner) {
        const RAINBOX_tag* r = box_of(b);
        if (r->active == 0) continue;
        if (!(d.x < r->max[0] && r->min[0] < d.x && d.z < r->max[2] && r->min[2] < d.z)) continue;
        if (DropType == 2) {
            found = true;
            d.y = std::fmax(d.y, r->min[1]);
            d.w = std::fmin(CubeCentre.y + 7.5f, r->max[1]);
        } else if (r->min[1] < d.y) {
            found = true;
            d.w = std::fmax(CubeCentre.y - 7.5f, r->min[1]);
        }
    }
    if (!found) {
        d.y -= 1054.0f;
        ++g_weather_stats.missed;
        return;
    }
    if (WindVector.y != 0.0f) {
        d.x += WindVector.x * (dy / WindVector.y);
        d.z += WindVector.z * (dy / WindVector.y);
    }
    state[index + base] = 1;
    ++g_weather_stats.added;
}

// [decomp] 0x0018c580 UpdateDrops
// Um tick das gotas (fora de pausa), pela câmera em uso (a 4 ativa, senão a 0): sem céu visível, Added = 999 (nada a
// desenhar). InitBodge: todas as gotas nascem em volta da câmera de uma vez. Senão: o vento anda ao acaso (+-0,0002 por
// tick, preso em +-0,01; não no tipo 1), o cubo vai para a câmera (7,5 m à frente do jogador com a câmera 0), as caixas
// são recortadas (sem nenhuma, Added = 999), RandTable2 = (vento + RandTable1) x FRAME_RATE_MUL e cada gota viva anda
// (o tipo 1 só pelo vento) e morre ao passar do fim (desce 1024 m); nas mortas, até 21 AddDrop por tick.
void UpdateDrops() {
    if (DropType == 0xff) return;
    if (GS_IsPaused(-1)) return;
    int camera = glb_viewer[4] != nullptr && glb_viewer[4]->active != 0 ? 4 : 0;
    if (glb_viewer[4] == nullptr) ++g_weather_guards;
    viewer_tag* v = glb_viewer[camera];
    if (v == nullptr) {
        ++g_weather_guards;
        return;
    }
    if (v->sky_visible == 0) {
        Added = 999;
        return;
    }
    if (InitBodge != 0) {
        InitBodge = 0;
        for (int i = 0; i < CurrentMax; ++i) {
            DropState[i] = 1;
            _VECTOR& d = DropVector[i];
            d = v->pos;
            d.x += Rand_FRand(30.0f) - 15.0f;
            d.z += Rand_FRand(30.0f) - 15.0f;
            d.y += Rand_FRand(30.0f) - 15.0f;
            d.w = v->pos.y - 7.5f;
        }
        return;
    }
    Added = 0;
    if (DropType != 1) {
        WindVector.x += Rand_FRand(0.0004f) - 0.0002f;
        WindVector.z += Rand_FRand(0.0004f) - 0.0002f;
        if (WindVector.x < -0.01f) {
            WindVector.x = -0.01f;
        } else if (0.01f < WindVector.x) {
            WindVector.x = 0.01f;
        }
        if (WindVector.z < -0.01f) {
            WindVector.z = -0.01f;
        } else if (0.01f < WindVector.z) {
            WindVector.z = 0.01f;
        }
    }
    obj_tag* player = glb_players[0];
    if (player == nullptr) return;
    CubeCentre = v->pos;
    if (camera == 0) {
        float c, s;
        PS2Sinf2(player->rot.y + 1.5707964f, player->rot.y, &c, &s);
        CubeCentre.x += s * 7.5f;
        CubeCentre.z += c * 7.5f;
    }
    if (RainBox_Clip() == 0) {
        Added = 999;
        return;
    }
    const float m = FRAME_RATE_MUL;
    // vmulx.xyz: o w fica (0: ninguém escreve o w do vento nem o de RandTable1).
    WindVector2 = {WindVector.x * m, WindVector.y * m, WindVector.z * m, WindVector.w};
    for (int i = 0; i < 32; ++i) {
        const _VECTOR& r = RandTable1[i];
        RandTable2[i] = {WindVector2.x + r.x * m, WindVector2.y + r.y * m, WindVector2.z + r.z * m, WindVector2.w + r.w};
    }
    for (int i = 0; i < CurrentMax; ++i) {
        if (DropState[i] == 1) {
            _VECTOR& d = DropVector[i];
            const _VECTOR& step = DropType == 1 ? WindVector2 : RandTable2[i & 0x1f];
            d = {d.x + step.x, d.y + step.y, d.z + step.z, d.w + step.w};
            const bool done = DropType == 2 ? d.w < d.y : d.y < d.w;
            if (done) {
                DropState[i] = 0;
                d.y -= 1024.0f;
            }
        } else if (Added < 0x15) {
            AddDrop(int16_t(i), 0, DropState, DropVector);
        }
    }
}

// [decomp] 0x0018cb90 DrawDrops
// As gotas (Game_Draw, depois dos estilhaços): nada sem tipo ou com Added 999. Tipo 1: um modelo 0x2000290 em cada gota
// viva (rotação identidade; nada com a câmera 0 debaixo da água da sala dela) e, nas fases 07000009-0b, também o
// 0x2000039 virado para a câmera (a rotação de viewer_tag::billboard, tweak 0x24 0x20 0x20 0x20). Os tipos 0 e 2: uma
// lista de partículas aditiva com todas as CurrentMax (as mortas ficam lá embaixo), textura 0x3000045 ou 0x3000087, sem
// cores (psiDrawParticleList usa então a cor fixa e o tamanho PS2SnowSize da fase).
void DrawDrops() {
    if (DropType == 0xff || Added == 999) return;
    if (DropType != 1) {
        void* tex = hashtable_getitem(DropType == 0 ? 0x3000045u : 0x3000087u);
        if (tex == nullptr) {
            ++g_weather_guards;
            return;
        }
        uint32_t rec;
        std::memcpy(&rec, tex, 4);
        psiDrawParticleList(reinterpret_cast<_VECTOR4*>(DropVector), CurrentMax, 0xff808080u, rec, nullptr, 1, 0.2f,
                            nullptr);
        return;
    }
    viewer_tag* v = glb_viewer[0];
    if (v == nullptr) return;
    if (v->cel != nullptr && v->pos.y < v->cel->water_y) return;
    MATRIX m;
    Mat_Identity(&m);
    celglist_tag* glist = hashtable_hashcode_to_celglist(0x2000290);
    const auto draw_all = [&](celglist_tag* g) {
        if (g == nullptr) {
            ++g_weather_guards;
            return;
        }
        for (int i = 0; i < CurrentMax; ++i) {
            if (DropState[i] != 1) continue;
            std::memcpy(m.m[3], &DropVector[i], sizeof(m.m[3]));
            psiDrawObjectMatrix(g, &m, 0);
        }
    };
    draw_all(glist);
    if (GameState.level_file - 0x7000009u < 3) {
        Mat_CopyRot(&v->billboard, &m);
        psiSetTweakARGB(0x24, 0x20, 0x20, 0x20);
        draw_all(hashtable_hashcode_to_celglist(0x2000039));
    }
}

// [decomp] 0x0018d0d0 RainBox_Clip
// Cada caixa de chuva (o modelo dela no lugar dela) recortada pelo cubo de 30 m em volta de CubeCentre; ativa se sobra
// área em x e z. Devolve quantas ficaram ativas.
uint32_t RainBox_Clip() {
    uint32_t active = 0;
    for (obj_tag* b = RainBoxRoot; b != nullptr; b = b->owner) {
        RAINBOX_tag* r = box_of(b);
        const celglist_tag* g = b->glist;
        r->min[0] = std::fmax(CubeCentre.x - 15.0f, g->box_min.x + b->pos.x);
        r->min[1] = std::fmax(CubeCentre.y - 15.0f, g->box_min.y + b->pos.y);
        r->min[2] = std::fmax(CubeCentre.z - 15.0f, g->box_min.z + b->pos.z);
        r->max[0] = std::fmin(CubeCentre.x + 15.0f, g->box_max.x + b->pos.x);
        r->max[1] = std::fmin(CubeCentre.y + 15.0f, g->box_max.y + b->pos.y);
        r->max[2] = std::fmin(CubeCentre.z + 15.0f, g->box_max.z + b->pos.z);
        if (r->min[0] < r->max[0] && r->min[2] < r->max[2]) {
            r->active = 1;
            ++active;
        } else {
            r->active = 0;
        }
    }
    return active;
}

// [decomp] 0x0018d840 RainBox_Create
// Uma caixa de chuva (posicionamento 0x32): objeto tipo 1 (sem atualização) com o modelo dela (a caixa é a do modelo,
// celglist_tag::box_min/box_max, no lugar do posicionamento), fora das salas (não é desenhado), no fim da lista
// RainBoxRoot (ligada por obj_tag::owner).
obj_tag* RainBox_Create(_VECTOR* pos, _VECTOR* rot, level_tag*, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x1c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    obj->type = 1;
    obj->flags = glist->flags;
    ++g_weather_stats.rainboxes;
    if (RainBoxRoot == nullptr) {
        RainBoxRoot = obj;
        return obj;
    }
    obj_tag* last = RainBoxRoot;
    while (last->owner != nullptr) last = last->owner;
    last->owner = obj;
    return obj;
}

// [decomp] 0x0018d838 InitRainBox
void InitRainBox() { RainBoxRoot = nullptr; }

// [decomp] 0x0017bc88 Env_Update
// Um tick do vento das salas (Game_Run, com DrawSnow): um sorteio por tick (Rand_Rand(20000) x REC_FRAME_RATE x
// 1/65000) faz o vento de cada sala com ambiente andar (ENV_tag::unknown_38 de -1 a 1, em volta) e a força
// (unknown_30) crescer (dobra mais o sorteio) até passar do máximo (unknown_34) e então cair (vezes 1 - sorteio) até um
// quarto dele; depois as gotas (UpdateDrops).
void Env_Update() {
    if (glb_world == nullptr) return;
    cel_tag* cel = glb_world->first;
    const float f = float(Rand_Rand(20000)) * REC_FRAME_RATE * 1.5384616e-05f;
    for (; cel != nullptr; cel = cel->next) {
        auto* env = static_cast<ENV_tag*>(cel->env);
        if (env == nullptr || env->unknown_34 == 0.0f) continue;
        env->unknown_38 += f * REC_FRAME_RATE;
        if (1.0f < env->unknown_38) env->unknown_38 = -1.0f;
        if (env->unknown_4d != 0) {
            env->unknown_30 = env->unknown_30 + (env->unknown_30 + f);
            if (env->unknown_34 < env->unknown_30) env->unknown_4d = 0;
        } else {
            env->unknown_30 *= 1.0f - f;
            if (env->unknown_30 < env->unknown_34 * 0.25f) env->unknown_4d = 1;
        }
    }
    UpdateDrops();
}

// [decomp] 0x0017c1b0 Env_Reset
// Na carga de cada fase (ResetMap_GameInit): os elementos do céu (GfxList + 0x10, 0x540 bytes), as gotas sem tipo
// (InitDrops(0xff), que sorteia RandTable1 do mesmo jeito), sem caixas, sem sol e sem ambiente global.
void Env_Reset() {
    std::memset(GfxList.sky, 0, sizeof(GfxList.sky));
    InitDrops(0xff);
    InitRainBox();
    Vec_Zero(&SunVector);
    GlobalEnv = nullptr;
}

uint32_t Weather_LiveDrops() {
    uint32_t n = 0;
    for (int i = 0; i < kMaxDrops && i < CurrentMax; ++i) n += DropState[i] == 1 ? 1 : 0;
    return n;
}

// ------------------------------------------------------------------ raios

// [decomp] 0x0018cde8 Lightning_Create
// Um ponto de raio (posicionamento 0x2c, objeto 0x33 que atravessa salas, flag 0x8000): os parâmetros 0, 1 (o par) e 2.
// Procura os outros raios com o mesmo par: com cada um, este vira o primeiro segmento, cria mais seis no mesmo lugar
// (tipo 1, atravessando salas), o outro vira o oitavo (tipo 1) e guarda os dois pontos.
obj_tag* Lightning_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag*) {
    obj_tag* obj = control_create_object(0x50, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_weather_stats.lightnings;
    LIGHTNING_tag* l = bolt_of(obj);
    l->seg[0] = nullptr;
    l->param0 = level->params[0];
    l->pair = int16_t(level->params[1]);
    l->param2 = int16_t(level->params[2]);
    obj->state = 0;
    obj->type = 0x33;
    obj->flags |= 0x8000;
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o == obj || o->type != 0x33 || bolt_of(o)->pair != l->pair) continue;
        l->seg[0] = obj;
        for (int k = 1; k < 7; ++k) {
            obj_tag* s = control_create_object(0, pos, rot, nullptr);
            l->seg[k] = s;
            if (s == nullptr) {
                ++g_weather_guards;
                continue;
            }
            s->type = 1;
            s->flags |= 0x8000;
        }
        l->seg[7] = o;
        o->type = 1;
        l->start = obj->pos;
        l->end = o->pos;
        ++g_weather_stats.pairs;
    }
    return obj;
}

// [decomp] 0x0018cf68 Lightning_Update
// Com o par: enquanto o céu mostra o relâmpago com repique (SuperHappyLightningFlag), os oito segmentos com o modelo
// 0x200032c, visíveis em todas as câmeras, de um ao outro (PositionBeam; o último até o ponto do par), donos o jogador;
// a cada repique (Reflicker, que este apaga) os pontos do meio são sorteados de novo (FractalThing). Sem o relâmpago,
// escondidos.
void Lightning_Update(obj_tag* obj) {
    LIGHTNING_tag* l = bolt_of(obj);
    if (l->seg[0] == nullptr) return;
    if (SuperHappyLightningFlag == 0) {
        for (obj_tag* s : l->seg) {
            if (s != nullptr) View_SetDrawInNoViews(s);
        }
        return;
    }
    if (Reflicker != 0) {
        Reflicker = 0;
        bool whole = true;
        for (obj_tag* s : l->seg) whole = whole && s != nullptr;
        if (whole) {
            FractalThing(l->seg, 0, 8, &l->start, &l->end, 0);
        } else {
            ++g_weather_guards;
        }
    }
    ++g_weather_stats.bolts;
    for (int k = 0; k < 8; ++k) {
        obj_tag* s = l->seg[k];
        if (s == nullptr) continue;
        hashtable_set_object_to_entity_gfx(s, 0x200032c);
        View_SetDrawInAllViews(s);
        if (k < 7) {
            obj_tag* next = l->seg[k + 1];
            PositionBeam(s, &s->pos, next != nullptr ? &next->pos : &l->end);
        } else {
            PositionBeam(s, &s->pos, &l->end);
        }
        s->mode |= 0x20;
        s->owner = glb_players[0];
    }
}

// ------------------------------------------------------------------ nuvens

// [decomp] 0x0018d620 Cloud_Create
// Uma nuvem (posicionamento 0xd4, objeto 0x24): o modelo do posicionamento, a altura (parâmetro 0), as velocidades
// (parâmetros 1 e 2, x 0,001), a volta (parâmetro 3); não é desenhada como objeto, e sim como o elemento do céu do
// parâmetro 4 (camada 1, antes do cenário).
obj_tag* Cloud_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x18, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;  // só no port: o original não confere
    ++g_weather_stats.clouds;
    obj->glist = glist;
    obj->type = 0x24;
    auto* c = static_cast<CLOUD_tag*>(obj->data);
    c->height = int32_t(level->params[0]);
    c->speed_x = float(level->params[1]) * 0.001f;
    c->speed_z = float(level->params[2]) * 0.001f;
    c->off_z = 0.0f;
    c->wrap = int32_t(level->params[3]);
    c->off_x = 0.0f;
    View_SetDrawInNoViews(obj);
    View_AddSkyObj(uint16_t(level->params[4]), nullptr, nullptr, nullptr, obj, 0, 0, 0, 1);
    return obj;
}

// [decomp] 0x0018d768 cloud_update
// Com o jogador: o deslocamento anda (velocidade x FRAME_RATE_MUL, em volta da volta) e a nuvem fica nele em volta do
// jogador, na altura dele mais a dela.
void cloud_update(obj_tag* obj) {
    auto* c = static_cast<CLOUD_tag*>(obj->data);
    const obj_tag* p = glb_players[0];
    if (p == nullptr) return;
    c->off_x = std::fmod(c->off_x + c->speed_x * FRAME_RATE_MUL, float(c->wrap));
    c->off_z = std::fmod(c->off_z + c->speed_z * FRAME_RATE_MUL, float(c->wrap));
    obj->pos.x = p->pos.x + c->off_x;
    obj->pos.z = p->pos.z + c->off_z;
    obj->pos.y = p->pos.y + float(c->height);
}

void Weather_Describe(char* out, size_t size) {
    uint32_t boxes = 0, active = 0;
    for (obj_tag* b = RainBoxRoot; b != nullptr; b = b->owner) {
        ++boxes;
        active += box_of(b)->active != 0 ? 1 : 0;
    }
    std::snprintf(out, size,
                  "tempo tipo %d max %d vivas %u novas %d criadas %u fora %u caixas %u ativas %u vento %.4f %.4f "
                  "raios %u pares %u desenhados %u nuvens %u guardas %u",
                  DropType == 0xff ? -1 : int(DropType), int(CurrentMax), Weather_LiveDrops(), int(Added),
                  g_weather_stats.added, g_weather_stats.missed, boxes, active, double(WindVector.x),
                  double(WindVector.z), g_weather_stats.lightnings, g_weather_stats.pairs, g_weather_stats.bolts,
                  g_weather_stats.clouds, g_weather_guards);
    // O primeiro raio com par: os dois pontos (para as capturas).
    for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
        if (o->type != 0x33 || bolt_of(o)->seg[0] == nullptr) continue;
        const size_t len = std::strlen(out);
        if (len < size) {
            const LIGHTNING_tag* l = bolt_of(o);
            std::snprintf(out + len, size - len, " raio %.2f,%.2f,%.2f a %.2f,%.2f,%.2f", double(l->start.x),
                          double(l->start.y), double(l->start.z), double(l->end.x), double(l->end.y), double(l->end.z));
        }
        break;
    }
}
