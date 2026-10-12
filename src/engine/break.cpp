// Port do módulo Break (quebráveis, estilhaços e destrutíveis), do Hurt (zonas que machucam) e de
// Debris_Create_Loop e Drone_BrokenObject. Ver break.h.

#include "engine/break.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/explode.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/spline.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi_draw.h"
#include "platform/psi_shard.h"

LLISTINFO_tag ShardList;
BreakGuards g_break_guards;
BreakStats g_break_stats;

namespace {

uint32_t mp_word(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}

// Divisão do FPU do EE: por zero dá +-FLT_MAX (sinal de a xor b), não infinito.
float ps2_div(float a, float b) {
    if (b == 0.0f) return (std::signbit(a) != std::signbit(b)) ? -FLT_MAX : FLT_MAX;
    return a / b;
}

BREAK_tag* break_of(obj_tag* obj) { return static_cast<BREAK_tag*>(obj->data); }
DESTROY_tag* destroy_of(obj_tag* obj) { return static_cast<DESTROY_tag*>(obj->data); }
HURT_tag* hurt_of(obj_tag* obj) { return static_cast<HURT_tag*>(obj->data); }

_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

// A arma de um projétil (BU_tag::weapon) atingido.
const weapon_definition_tag* bullet_weapon(obj_tag* bullet) { return bullet_of(bullet)->weapon; }

}  // namespace

void break_debug_power_on() {
    std::memset(&ShardList, 0, sizeof(ShardList));
    g_break_guards = {};
    g_break_stats = {};
}

// [decomp] 0x0012bed0 InitShards
// Na carga de cada fase: a lista de estilhaços recomeça vazia (o heap também recomeça).
void InitShards() { LList_Init(&ShardList, 0xa0, 0); }

// ---- quebráveis ----

// [decomp] 0x0012bfd8 Break_Update
// Os acertos do tick (Collide_Update os copia para obj->hits): tiros das classes fora da máscara
// (mais as 0x106) tiram o dano da arma da resistência; em 07000001, um personagem que atravessa um
// vidro tira 20 x o quadrado do quanto andou no tick. Abaixo de zero, Break_Kill com o último desses
// acertos.
void Break_Update(obj_tag* obj) {
    if (obj == nullptr || (obj->life & 1) != 0) return;
    BREAK_tag* b = break_of(obj);
    HITDATA_tag* last = nullptr;
    Collide_FilterBullets(&obj->hits, uint16_t(b->mask | 0x106));
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr) continue;
        if (o->type == 2) {
            if (GameState.level_file != 0x7000001 || b->kind != 2) continue;
            last = h;
            b->strength -= Vec_SqDist3D(&o->pos, &o->old_pos) * 20.0f;
        } else if (o->type == 5) {
            last = h;
            float damage;
            std::memcpy(&damage, &h->unknown_08, 4);
            b->strength -= damage;
        }
    }
    if (b->strength < 0.0f) Break_Kill(obj, last);
}

// [decomp] 0x0012c128 Break_ApplyDamage
// Dano de uma explosão (Explode_Propagate põe o dano em hit->unknown_08).
void Break_ApplyDamage(obj_tag* obj, HITDATA_tag* hit) {
    float damage;
    std::memcpy(&damage, &hit->unknown_08, 4);
    BREAK_tag* b = break_of(obj);
    b->strength -= damage;
    if (b->strength < 0.0f) Break_Kill(obj, hit);
}

// [decomp] 0x0012c168 Break_DoBreak
// Quebra o primeiro quebrável cuja esfera contém pos (chamada pelos scripts): o acerto montado na pilha
// tem a posição no centro da esfera, a direção (0, 0, -0.5) e o plano com a frente do objeto
// (RotMatrix da rotação, Mat_GetDir).
// Divergência: o resto do acerto (material, objeto, célula) fica zerado; no original é lixo da pilha
// (Drone_BrokenObject leria hit->obj num quebrável que não é de vidro).
void Break_DoBreak(_VECTOR* pos) {
    obj_tag* obj = control_first_object();
    for (; obj != nullptr; obj = obj->next) {
        if (obj->type != 0x20) continue;
        if (Vec_SqDist3D(pos, &obj->sphere) < obj->sphere.w * obj->sphere.w) break;
    }
    if (obj == nullptr) return;
    HITDATA_tag hit{};
    MATRIX m;
    RotMatrix(&obj->rot, &m);
    _VECTOR dir;
    Mat_GetDir(&dir, &m);
    std::memcpy(&hit.plane, &dir, sizeof(dir));
    hit.pos = obj->sphere;
    hit.dir.x = 0.0f;
    hit.dir.y = 0.0f;
    hit.dir.z = -0.5f;
    Break_Kill(obj, &hit);
}

// [decomp] 0x0012b970 Break_Kill
// Marca o objeto para remoção e, se o modelo tem colisão: o som (0x2e no vidro; senão 0x5f2 em 41% das
// vezes, 0x5f3 nas outras) e, no vidro, cada triângulo de psiGetTriList estilhaçado em
// ShatterPolyRecurse com a textura dele; nos outros, a fala dos inimigos (Drone_BrokenObject) e os
// detritos (Debris_Create_Loop) do material do acerto (0 vira 0x10), com hit->unknown_20 zerado.
// Divergência: sem acerto (Break_Update sem tiro no tick) o original leria o endereço 0x40; aqui sai,
// contado em g_break_guards.
void Break_Kill(obj_tag* obj, HITDATA_tag* hit) {
    colldata_tag* coll = obj->glist->coll;
    obj->life |= 1;
    if (coll == nullptr) return;
    ++g_break_stats.broken;
    BREAK_tag* b = break_of(obj);
    uint32_t sound = 0x2e;
    if (b->kind != 2) sound = Rand_Rand(100) < 0x29 ? 0x5f2 : 0x5f3;
    Sound_Play3D(sound, &obj->sphere, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (hit == nullptr) {
        ++g_break_guards.null_hit;
        return;
    }
    if (b->kind == 2) {
        TRILIST_tag* list = psiGetTriList(obj);
        if (list == nullptr) return;
        for (int32_t i = 0; i < list->tri_count; ++i) {
            const BREAKTRI_tag& t = list->tris[i];
            _SHATTER_VERT_LIST verts;
            for (int k = 0; k < 3; ++k) verts.vert[k] = list->verts[t.vert[k]];
            ShatterPolyRecurse(0, &verts, obj, hit, t.texture);
        }
        psiFinishedObjectShatter();
        return;
    }
    Drone_BrokenObject(obj, hit);
    if ((hit->surface & 0x3f) == 0) hit->surface = 0x10;
    Vec_Zero(reinterpret_cast<_VECTOR*>(hit->unknown_20));
    Debris_Create_Loop(coll, hit, obj);
}

// [decomp] 0x0012b270 ShatterPolyRecurse
// Corta o triângulo até a aresta mais longa (ao quadrado) não passar de metade do quadrado da
// distância do centro dele (no mundo: média dos vértices + obj->pos, sem a rotação do objeto) ao ponto
// atingido, entre 0.5 e 100. Para cortar, gira os vértices até a aresta mais longa ser a 0-1 e põe um
// vértice novo nela, numa fração sorteada entre 0.25 e 0.75 (posição, UV e cor interpoladas): os
// triângulos (0, novo, 2) e (novo, 1, 2) seguem cortando. Um triângulo pequeno vira um estilhaço:
// vértices relativos ao centro (psiCreateShard), parado por distância x 15 ticks, voando na direção
// do tiro a min(0.125 / distância, 0.1) por tick, girando ao acaso (+-0.5 / (10 x distância²) por
// tick), por 25 ticks. depth só conta a recursão (não limita).
// Divergência: o vértice novo parte de uma cópia do vértice 0 (o original, sem rotação, deixa lixo da
// pilha no cabeçalho e no w, que não são lidos); divisões por zero como no EE (ps2_div).
void ShatterPolyRecurse(int32_t depth, _SHATTER_VERT_LIST* list, obj_tag* obj, HITDATA_tag* hit, uint32_t texture) {
    SHATTER_VERT* v = list->vert;
    _VECTOR avg;
    Vec_Avg3(&v[0].pos, &v[1].pos, &v[2].pos, &avg);
    _VECTOR center = add4(avg, obj->pos);
    const float dist2 = Vec_SqDist3D(&hit->pos, &center);
    const float d01 = Vec_SqDist3D(&v[0].pos, &v[1].pos);
    const float d12 = Vec_SqDist3D(&v[1].pos, &v[2].pos);
    const float d20 = Vec_SqDist3D(&v[2].pos, &v[0].pos);
    const float limit = std::fmin(std::fmax(dist2 * 0.5f, 0.5f), 100.0f);
    const float longest = std::fmax(std::fmax(d20, d12), d01);
    if (!(limit < longest)) {
        const float dist = std::sqrt(dist2);
        for (int k = 0; k < 3; ++k) {
            v[k].pos = {v[k].pos.x - avg.x, v[k].pos.y - avg.y, v[k].pos.z - avg.z, v[k].pos.w - avg.w};
        }
        auto* s = static_cast<SHARD_tag*>(Mem_Malloc(0xa0, 0x3204, 0));
        if (s == nullptr) return;
        std::memset(s, 0, sizeof(*s));
        LList_Add(&ShardList, &s->node);
        ++g_break_stats.shards;
        v[0].texture = texture;
        s->chain = psiCreateShard(list);
        s->pos = add4(avg, obj->pos);
        s->rot.x = 0.0f;
        s->rot.y = 0.0f;
        s->rot.z = 0.0f;
        s->delay = dist * (REC_FRAME_RATE * 15.0f);
        const float spin_div = dist2 * 10.0f;
        s->spin.x = ps2_div(Rand_FRand(1.0f) - 0.5f, spin_div);
        s->spin.y = ps2_div(Rand_FRand(1.0f) - 0.5f, spin_div);
        s->spin.z = ps2_div(Rand_FRand(1.0f) - 0.5f, spin_div);
        float speed = ps2_div(1.0f, dist) * 0.125f;
        if (0.1f < speed) speed = 0.1f;
        s->vel.x = hit->dir.x * speed;
        s->vel.y = hit->dir.y * speed;
        s->vel.z = hit->dir.z * speed;
        s->life = REC_FRAME_RATE * 25.0f;
        s->texture = texture;
        return;
    }
    SHATTER_VERT mid = v[0];
    if (std::fmax(d20, d01) <= d12) {
        const SHATTER_VERT old0 = v[0];
        v[0] = v[1];
        v[1] = v[2];
        v[2] = old0;
        mid = old0;
    } else if (std::fmax(d01, d12) <= d20) {
        const SHATTER_VERT old0 = v[0];
        v[0] = v[2];
        v[2] = v[1];
        v[1] = old0;
        mid = old0;
    }
    const float t = Rand_FRand(1.0f) * 0.5f + 0.25f;
    mid.pos.x = (v[0].pos.x - v[1].pos.x) * t + v[1].pos.x;
    mid.pos.y = (v[0].pos.y - v[1].pos.y) * t + v[1].pos.y;
    mid.pos.z = (v[0].pos.z - v[1].pos.z) * t + v[1].pos.z;
    mid.uv[0] = (v[0].uv[0] - v[1].uv[0]) * t + v[1].uv[0];
    mid.uv[1] = (v[0].uv[1] - v[1].uv[1]) * t + v[1].uv[1];
    for (int c = 0; c < 4; ++c) mid.rgba[c] = (v[0].rgba[c] - v[1].rgba[c]) * t + v[1].rgba[c];
    mid.texture = v[0].texture;
    _SHATTER_VERT_LIST half{{v[0], mid, v[2]}};
    ShatterPolyRecurse(depth + 1, &half, obj, hit, texture);
    half = {{mid, v[1], v[2]}};
    ShatterPolyRecurse(depth + 1, &half, obj, hit, texture);
}

// [decomp] 0x0012b808 UpdateAllShards
// Fora de pausa, a cada tick: estilhaço sem vida sai da lista (a cadeia vai para psiDeleteShard e o nó
// é liberado); parado (delay > 0) só desconta o tempo; voando, anda vel, gira spin, cai pela gravidade
// (WldGravity x REC_FRAME_RATE² x 4 x FRAME_RATE_MUL por tick) e perde vida. Estilhaço sem cadeia
// fica parado na lista.
// Divergência: o w da velocidade não recebe o lixo de pilha que o original soma com a gravidade.
void UpdateAllShards() {
    if (GS_IsPaused(-1)) return;
    LLNODE_tag* n = ShardList.first;
    while (n != nullptr) {
        LLNODE_tag* next = n->next;
        auto* s = reinterpret_cast<SHARD_tag*>(n);
        if (s->chain != nullptr) {
            if (s->life <= 0.0f) {
                void* node = LList_Remove(&ShardList, n);
                psiDeleteShard(&s->chain);
                Mem_Free(&node);
            } else if (s->delay <= 0.0f) {
                s->pos = add4(s->pos, s->vel);
                s->rot = add4(s->rot, s->spin);
                const float g = REC_FRAME_RATE * REC_FRAME_RATE * 4.0f * FRAME_RATE_MUL;
                s->vel.x += WldGravity.x * g;
                s->vel.y += WldGravity.y * g;
                s->vel.z += WldGravity.z * g;
                s->life -= REC_FRAME_RATE;
            } else {
                s->delay -= REC_FRAME_RATE;
            }
        }
        n = next;
    }
}

// [decomp] 0x0012bef8 DrawAllShards
// Cada estilhaço vivo com cadeia: alfa do tweak = vida x FRAME_RATE x 10 (até 255; psiSetTweakA, que
// a rotina dos estilhaços não usa) e psiDrawShard com a matriz da posição e da rotação.
void DrawAllShards() {
    for (LLNODE_tag* n = ShardList.first; n != nullptr; n = n->next) {
        auto* s = reinterpret_cast<SHARD_tag*>(n);
        if (s->chain == nullptr || !(0.0f < s->life)) continue;
        float a = s->life * (FRAME_RATE * 10.0f);
        if (255.0f < a) a = 255.0f;
        psiSetTweakA(uint8_t(int32_t(a) & 0xff));
        MATRIX m;
        View_RotTransMatrix(&s->rot, &s->pos, &m);
        psiDrawShard(s->chain, &m, s->texture);
    }
}

// [decomp] 0x001ed548 Debris_Create_Loop
// Um detrito do material do acerto (Debris_Create, 1 por vez) a cada três triângulos de colisão de
// cada folha da árvore: o acerto vai para o centro do triângulo (vértices quantizados da folha, mais
// obj->pos, sem a rotação do objeto) com o plano dele (normal / 16383 e o d em float).
void Debris_Create_Loop(colldata_tag* coll, HITDATA_tag* hit, obj_tag* obj) {
    for (uint32_t i = 0; i < coll->box_count; ++i) {
        const auto* box = reinterpret_cast<const COLLBOX_tag*>(coll->boxes + i * 0x30);
        if (box->child_a >= 0) continue;
        const uint8_t* verts = coll->head + uint32_t(box->verts) * 0x40;
        for (int32_t t = box->child_b; t < box->tri_end; t += 3) {
            const auto* tri = reinterpret_cast<const uint16_t*>(coll->list + t * 8);
            _VECTOR p[3]{};
            for (int k = 0; k < 3; ++k) {
                const auto* q = reinterpret_cast<const int16_t*>(verts + uint32_t(tri[k]) * 2);
                p[k].x = float(q[0]) * box->scale + box->offset[0];
                p[k].y = float(q[1]) * box->scale + box->offset[1];
                p[k].z = float(q[2]) * box->scale + box->offset[2];
            }
            Vec_Avg3(&p[0], &p[1], &p[2], &hit->pos);
            hit->pos = add4(hit->pos, obj->pos);
            const auto* n = reinterpret_cast<const int16_t*>(verts + uint32_t(tri[3]) * 2);
            hit->plane.a = float(n[0]) * 6.103888e-05f;
            hit->plane.b = float(n[1]) * 6.103888e-05f;
            hit->plane.c = float(n[2]) * 6.103888e-05f;
            std::memcpy(&hit->plane.d, n + 3, 4);
            Debris_Create(hit, &effect_info(hit->surface), obj->cel, 1);
        }
    }
}

// [decomp] 0x00138588 Drone_BrokenObject
// Em 07000001, um tiro do jogador que quebra algo faz cada inimigo do tipo 0xc (enquanto o primeiro
// deles com vida não acabar a lista; um morto encerra a busca) comentar com uma das falas 0x40-0x43.
void Drone_BrokenObject(obj_tag*, HITDATA_tag* hit) {
    if (Drone_bDisableSystem != 0 || GameState.level_file != 0x7000001) return;
    obj_tag* bullet = hit->obj;
    if (bullet == nullptr || bullet->data == nullptr || bullet->type != 5) return;
    if (bullet_of(bullet)->from != glb_players[0]) return;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (d->type != 0xc) continue;
        if (d->health <= 0.0f) return;
        obj_tag* o = d->obj;
        if (o == nullptr || (o->type != 2 && o->type != 0x11)) continue;
        DCVars_tag dcv{o, static_cast<Drone_tag*>(o->data), o->cel, nullptr};
        dcv.sm = &dcv.drone->sm;
        NDrone2_SpeechSFX(&dcv, DroneFunc_ChooseSFX(0x40, 0x43), 0, 0, 0);
    }
}

// ---- destrutíveis ----

// [decomp] 0x0012c318 Destroy_Create
// Objeto destrutível (tipo 0x21; no multijogador só com MPSettings + 0x1d4): o modelo quebrado é o
// lod da entidade ou a entidade seguinte (nenhum se o objeto tem a flag 0x10); os parâmetros vão para
// DESTROY_tag (unsigned para float onde o original converte assim).
obj_tag* Destroy_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist, celglist_tag* next_glist) {
    if (mp_word(0x180) != 0 && mp_word(0x1d4) == 0) return nullptr;
    obj_tag* obj = control_create_object(0x18, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    obj->type = 0x21;
    Control_SetGList(obj, glist);
    build_LinkToRoom(obj, 0, glb_world);
    DESTROY_tag* d = destroy_of(obj);
    d->smashed = glist->lod != nullptr ? nf::raw(glist->lod) : next_glist;
    d->channel_in = uint16_t(level->params[1]);
    d->channel_out = uint16_t(level->params[0]);
    d->explosion = int8_t(level->params[2]);
    d->strength = float(level->params[3]);
    d->mask = level->params[4] == 0 ? 0 : uint16_t(~level->params[4]);
    d->blast = float(level->params[5]) * 0.01f;
    d->smashed_flag = 0;
    d->delay = int32_t(float(level->params[6]) * -FRAME_RATE);
    if ((obj->flags & 0x10) != 0) d->smashed = nullptr;
    return obj;
}

// [decomp] 0x0012c4f8 Destroy_Update
// Depois de quebrado, com canal de saída e atraso: conta os ticks e, ao fim, liga o canal e vira tipo
// 1 (sem atualização). Antes de quebrar, o canal de entrada ligado o quebra (Destroy_Smash) e é
// esquecido.
void Destroy_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    DESTROY_tag* d = destroy_of(obj);
    if (d->channel_out != 0 && 0 < d->delay) {
        if (--d->delay != 0) return;
        switch_channels[d->channel_out] = 1;
        obj->type = 1;
        return;
    }
    if (obj->glist != d->smashed && d->channel_in != 0 && switch_channels[d->channel_in] != 0) {
        d->channel_in = 0;
        Destroy_Smash(obj, nullptr, 0);
    }
}

// [decomp] 0x0012bd48 Destroy_CollisionHandler
// Tiros das classes fora da máscara tiram o dano da resistência; abaixo de zero, o canal de saída liga
// (sem atraso) ou o atraso começa a contar (Destroy_Update), e Destroy_Smash com quem atirou. A classe
// 0x67 aquece o objeto: g e b da cor (obj_tag::unknown_103[1..2], o tweak) caem 5 por tick com acerto
// dela e voltam 1 por tick sem.
void Destroy_CollisionHandler(obj_tag* obj) {
    DESTROY_tag* d = destroy_of(obj);
    int32_t heat = 0;
    Collide_FilterBullets(&obj->hits, d->mask);
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr || o->type != 5) continue;
        float damage;
        std::memcpy(&damage, &h->unknown_08, 4);
        const float left = d->strength - damage;
        if (bullet_weapon(o)->klass == 0x67) heat = 5;
        d->strength = left;
        if (left < 0.0f) {
            if (d->channel_out != 0) {
                if (d->delay == 0) {
                    switch_channels[d->channel_out] = 1;
                } else {
                    d->delay = -d->delay;
                }
            }
            Destroy_Smash(obj, bullet_of(h->obj)->from, 0);
            return;
        }
    }
    const auto clamp = [](int32_t v) { return uint8_t(v < 0 ? 0 : (v < 0x100 ? v : 0xff)); };
    uint8_t* colour = obj->unknown_103;  // r, g, b, a: +0x104 g, +0x105 b
    if (heat == 0) {
        colour[2] = clamp(int32_t(colour[2]) + 1);
        colour[1] = clamp(int32_t(colour[1]) + 1);
    } else {
        colour[2] = clamp(int32_t(colour[2]) - heat);
        colour[1] = clamp(int32_t(colour[1]) - heat);
    }
}

// [decomp] 0x0012bb60 Destroy_Smash
// Quebra uma vez: apaga a luz da fase que estava nele (Light_FindMapLight pela esfera) e as marcas de
// bala, troca para o modelo quebrado e inverte o atraso. Sem canal de saída vira tipo 1; com canal e
// sem atraso, liga o canal e vira tipo 1 (em 07000002 o modelo 020001d9 conta como cenário atingido);
// com atraso continua (Destroy_Update conta). Com explosão: Explode_Create no centro da esfera, raio
// raio x 2, 4 ou 3 (tipos 2, 1 e outros: scripts 0x600004f, 0x6000027, 0x6000052), raio do dano
// DESTROY_tag::blast e dano tipo x 10, creditada a from. flags não é usado.
void Destroy_Smash(obj_tag* obj, obj_tag* from, uint16_t) {
    DESTROY_tag* d = destroy_of(obj);
    if (d->smashed_flag != 0) return;
    d->smashed_flag = 1;
    ++g_break_stats.smashed;
    Light_Delete(Light_FindMapLight(&obj->sphere, obj->sphere.w));
    Effect_RemoveSObjs(obj);
    if (d->smashed != nullptr) Control_SetGList(obj, d->smashed);
    d->delay = -d->delay;
    if (d->channel_out == 0) {
        obj->type = 1;
    } else if (d->delay == 0) {
        switch_channels[d->channel_out] = 1;
        obj->type = 1;
        if (GameState.level_file == 0x7000002 && obj->glist != nullptr &&
            obj->glist == static_cast<celglist_tag*>(hashtable_getitem(0x20001d9))) {
            PlrStat_LogShotHitScenery(0);
        }
    }
    if (d->explosion == 0) return;
    uint32_t scale = 3;
    uint32_t script = 0x6000052;
    if (d->explosion == 2) {
        scale = 2;
        script = 0x600004f;
    } else if (d->explosion == 1) {
        scale = 4;
        script = 0x6000027;
    }
    Explode_Create(obj, &obj->sphere, &CONSTANT_UP1, obj->sphere.w * float(scale), d->blast, script,
                   float(int32_t(d->explosion)) * 10.0f, 200, 200, 0, from, 0);
}

// ---- zonas que machucam ----

// [decomp] 0x0017e870 Hurt_Create
// Zona que machuca (tipo 0x27, mode | 0x44): segue o caminho do posicionamento a quadros / duração por
// tick (path_unknown, convertido como FRAME_RATE / duração x REC_FRAME_RATE); com canal desligado
// começa escondida (flag 0x20).
// Divergência: duração 0 dá FLT_MAX como a divisão do EE (no PC seria infinito).
obj_tag* Hurt_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x1c, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    HURT_tag* h = hurt_of(obj);
    h->t = 0.0f;
    h->path = level->path;
    const uint16_t count = uint16_t(level->path_count);
    h->frames = count == 0 ? 0 : uint16_t(count - 1);
    h->type = uint8_t(level->params[0]);
    h->damage = float(level->params[1]);
    h->channel = uint16_t(level->params[2]);
    h->sound = level->params[3] != 0 ? uint16_t(level->params[3]) : 0xffff;
    h->touch = uint16_t(level->params[4]);
    h->once = uint8_t(level->params[5]);
    const float per_tick = float(h->frames) * REC_FRAME_RATE;
    h->speed = per_tick;
    h->speed = per_tick * ps2_div(FRAME_RATE, float(level->path_unknown));
    obj->type = 0x27;
    Control_SetGList(obj, glist);
    obj->mode = uint16_t(obj->mode | 0x44);
    build_LinkToRoom(obj, 0, glb_world);
    if (h->channel != 0 && switch_channels[h->channel] == 0) obj->flags |= 0x20;
    return obj;
}

// [decomp] 0x0017e5d0 Hurt_Update
// Com canal, a zona aparece (sem a flag 0x20) com ele ligado e some com ele desligado. Estado 0: dispara
// (estado 1) quando o canal liga ou, sem canal, quando quem a toca está na máscara (8 jogador, 0x10
// drone, 2 bala; máscara 0 dispara sozinha). Estado 1: toca o som no começo do caminho, anda pelo
// caminho (Spline_Interp na matriz do objeto) e, no fim, volta ao começo; "once" a apaga (estado 2,
// tipo 1). Com o canal desligado no meio, volta ao estado 0 e se esconde (andando ainda este tick).
void Hurt_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    HURT_tag* h = hurt_of(obj);
    if (h->channel != 0) {
        if (switch_channels[h->channel] != 0) obj->flags &= ~0x20u;
        if (switch_channels[h->channel] == 0) obj->flags |= 0x20;
    }
    if (obj->state == 2) {
        obj->type = 1;
        return;
    }
    if (obj->state == 0) {
        if (h->channel != 0 && switch_channels[h->channel] == 0) return;
        uint32_t fire = 0;
        if (h->channel != 0) {
            fire = switch_channels[h->channel];
        } else if (h->touch == 0) {
            fire = 1;
        } else {
            uint32_t who = 0;
            for (HITDATA_tag* hit = obj->hits; hit != nullptr; hit = hit->next) {
                if (hit->obj == nullptr) continue;
                switch (hit->obj->type) {
                    case 3: who |= 8; break;
                    case 2: who |= 0x10; break;
                    case 5: who |= 2; break;
                    default: break;
                }
            }
            fire = h->touch & who;
        }
        if (fire != 0) obj->state = 1;
        return;
    }
    if (obj->state != 1 || h->frames == 0) return;
    if (h->channel != 0 && switch_channels[h->channel] == 0) {
        obj->state = 0;
        obj->flags |= 0x20;
    }
    if (h->t == 0.0f) Sound_Play3D(h->sound, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Spline_Interp(h->path, 0, h->frames, h->t, &obj->matrix, nullptr, 2);
    obj->mode = uint16_t(obj->mode | 0x20);
    h->t = h->t + h->speed;
    if (float(h->frames) < h->t) {
        h->t = 0.0f;
        if (h->once == 1) obj->state = 2;
    }
}
