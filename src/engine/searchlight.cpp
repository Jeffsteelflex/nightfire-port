// Holofotes. Ver searchlight.h.
//
// Divergências do original (só no port; também nas funções):
// - Searchlight_Create ignora uma peça vizinha de tipo acima de 4 (o original escreveria fora da tabela de peças; não
//   há nenhuma no disco) e, sem modelo, os flags ficam 0 (o original leria o endereço 0x44).
// - Searchlight_Update sem jogador (glb_players[0] nulo) não mira nem mexe nas peças (o original leria o objeto nulo);
//   conta em SearchlightStats::guards.
// - Os vetores com lixo da pilha na coordenada w (a mancha empurrada pela normal, o brilho) ficam com o w dos vetores
//   de verdade.
// - O tempo da varredura não dá a volta: o original calcula fmodf(tempo, 480) e descarta o resultado (igual aqui).

#include "engine/searchlight.h"

#include <cmath>
#include <cstring>

#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/lights.h"
#include "engine/memory.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/fmath.h"

SearchlightStats g_searchlight_stats;

namespace {

SEARCHLIGHT_tag* sl_of(obj_tag* obj) { return static_cast<SEARCHLIGHT_tag*>(obj->data); }

light_tag* light_of(obj_tag* spot) {
    light_tag* l;
    l = nf::p32_load<light_tag>(&sl_of(spot)->parts[2]);
    return l;
}

// As peças que acendem (a mancha com a luz, o feixe e o brilho) somem: a luz volta à lista de livres e os objetos pedem
// a remoção.
void kill_parts(SEARCHLIGHT_tag* d) {
    if (obj_tag* spot = d->parts[2]; spot != nullptr) {
        if (light_tag* l = light_of(spot); l != nullptr) {
            if (LLNODE_tag* node = LList_Remove(&LightList.used, &l->node); node != nullptr) {
                psiLight_Delete(l->handle);
                LList_Add(&LightList.free, node);
            }
        }
        spot->life |= 1;
        d->parts[2] = nullptr;
    }
    if (obj_tag* beam = d->parts[3]; beam != nullptr) {
        beam->life |= 1;
        d->parts[3] = nullptr;
    }
    if (obj_tag* glare = d->parts[4]; glare != nullptr) {
        glare->life |= 1;
        d->parts[4] = nullptr;
    }
}

_VECTOR spherical(float radius, float yaw, float pitch) {
    _VECTOR v{};
    Vec_Spherical_2_Cartesian(&v, radius, yaw, -pitch);
    return v;
}

// A mancha onde o raio da lâmpada (100 m pela direção dela) bate, deitada na superfície e 1 cm acima dela; sem acerto
// ela some. Devolve a distância da mancha ao jogador.
float place_spot(obj_tag* lamp, obj_tag* spot, obj_tag* player) {
    const _VECTOR d = spherical(100.0f, lamp->rot.y, lamp->rot.x);
    _VECTOR to = d;
    to.x = d.x + lamp->pos.x;
    to.y = d.y + lamp->pos.y;
    to.z = d.z + lamp->pos.z;
    P32<HITDATA_tag> hits = nullptr;
    if (!Collide_RayIntersect(&lamp->pos, &to, lamp->cel, lamp, nullptr, &hits, 0, 0x18, 0)) {
        View_SetDrawInNoViews(spot);
    } else {
        if (hits == nullptr) {
            View_SetDrawInNoViews(spot);
        } else {
            spot->pos = hits->pos;
            spot->mode |= 0x20;
            spot->rot.y = lamp->rot.y;
            const plane_equ_tag& n = hits->plane;
            spot->rot.x = 1.5707964f - ATAN2_APPROX(n.b, std::sqrt(n.a * n.a + n.c * n.c));
            spot->rot.y = ATAN2_APPROX(n.a, n.c);
            spot->rot.z = 0.0f;
            // vmulx.xyz + vadd.xyzw: o w somado é lixo da pilha no PS2; aqui fica o do ponto.
            spot->pos.x = spot->pos.x + n.a * 0.01f;
            spot->pos.y = spot->pos.y + n.b * 0.01f;
            spot->pos.z = spot->pos.z + n.c * 0.01f;
            View_SetDrawInAllViews(spot);
        }
        Collide_FreeHitList(&hits);
    }
    return Vec_Dist3D(&spot->pos, &player->pos);
}

}  // namespace

light_tag* Searchlight_Light(const obj_tag* spot) { return light_of(const_cast<obj_tag*>(spot)); }

// [decomp] 0x001cb4f8 Searchlight_Create
// Uma peça do holofote (ver searchlight.h): o tipo, o tempo da varredura ao acaso, a rotação do posicionamento, os
// canais e o arco (parâmetros 4 e 5: o centro e a metade, do menor ao maior). As peças a até 3 m na mesma sala se
// juntam na lâmpada; com um canal de acender, as peças acesas da lâmpada (todas menos o suporte) começam escondidas. A
// mancha ganha a luz (a lista de luzes cresce de 32 se não houver livre) e nasce 10 km longe.
obj_tag* Searchlight_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0x40, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_searchlight_stats.created;
    SEARCHLIGHT_tag* d = sl_of(obj);
    d->kind = uint16_t(level->params[0]);
    d->timer = float(Rand_Rand(0x1e0));
    d->rot = *rot;
    d->start = uint8_t(level->params[1]);
    d->off = uint8_t(level->params[2]);
    d->alarm = uint8_t(level->params[3]);
    const uint32_t a = level->params[4], b = level->params[5];
    const uint32_t lo = a < b ? a : b;
    const uint32_t hi = b < a ? a : b;
    d->pitch = (float(hi) * 0.017453294f + float(lo) * 0.017453294f) * 0.5f;
    d->half = (float(hi) * 0.017453294f - float(lo) * 0.017453294f) * 0.5f;
    obj->type = 0x22;
    obj->state = d->start == 0 ? 0 : 3;
    obj->glist = glist;
    obj->flags = glist != nullptr ? glist->flags : 0;
    build_LinkToRoom(obj, 0, glb_world);
    if (d->kind == 1) ++g_searchlight_stats.lamps;
    for (obj_tag* o = obj->cel != nullptr ? obj->cel->objects : nullptr; o != nullptr; o = o->cel_next) {
        if (o == obj || o->type != 0x22) continue;
        if (3.0f <= Vec_Dist3D(&obj->pos, &o->pos)) continue;
        SEARCHLIGHT_tag* other = sl_of(o);
        if (d->kind == 1) {
            if (other->kind < 5) d->parts[other->kind] = o;
            if (d->start != 0 && other->kind != 0) View_SetDrawInNoViews(o);
        } else if (other->kind == 1) {
            if (d->kind < 5) other->parts[d->kind] = obj;
            if (other->start != 0 && d->kind != 0) View_SetDrawInNoViews(obj);
        }
    }
    if (d->kind == 2) {
        auto* l = static_cast<light_tag*>(DList_MoveFromFree2InUse(&LightList));
        if (l == nullptr) {
            auto* nodes = static_cast<uint8_t*>(Mem_Malloc(0xa00, 0x3704, 0));
            std::memset(nodes, 0, 0xa00);
            for (uint32_t i = 0; i < 0x20; ++i) LList_Add(&LightList.free, reinterpret_cast<LLNODE_tag*>(nodes + i * 0x50));
            l = static_cast<light_tag*>(DList_MoveFromFree2InUse(&LightList));
        }
        l->r = 0xff;
        l->radius = 500.0f;
        l->g = 0xff;
        l->b = 0xff;
        l->intensity = 1.0f;
        l->kind = 2;
        l->channel = 0;
        l->flicker_rand = 0;
        l->life = 0;
        l->flicker_max = 0;
        l->on = 1;
        l->flicker = uint16_t(Rand_Rand(0));
        l->sound = 0xffff;
        l->frame_mask = 0;
        l->pos = obj->pos;
        l->handle = psiLight_Create(l);
        l->obj = obj;
        nf::p32_store(&d->parts[2], l);
        obj->pos.x = obj->pos.x - 10000.0f;
        obj->pos.y = obj->pos.y - 10000.0f;
        obj->pos.z = obj->pos.z - 10000.0f;
        ++g_searchlight_stats.lights;
    }
    return obj;
}

// [decomp] 0x001cb900 Searchlight_Update
// Só a lâmpada pensa (ver searchlight.h): o canal de apagar, os estados, as peças (o suporte gira com ela, a mancha vai
// onde o raio bate e acha o jogador a até 2 m, o feixe aponta pela lâmpada com o "para cima" para o jogador, o brilho de
// frente) e os tiros que a quebram.
void Searchlight_Update(obj_tag* obj) {
    SEARCHLIGHT_tag* d = sl_of(obj);
    if (d->kind != 1) return;
    obj_tag* player = glb_players[0];
    if (d->off != 0 && switch_channels[d->off] != 0) {
        obj->state = 3;
        kill_parts(d);
        d->start = 0;
        d->off = 0;
        d->alarm = 0;
        ++g_searchlight_stats.off;
        Control_NextLOD(obj);
    }
    const int16_t state = int16_t(obj->state);
    bool parts = false;
    if (state == 1) {
        if (d->alarm != 0) {
            switch_channels[d->alarm] = 1;
            ++g_searchlight_stats.alarms;
        }
        if (player == nullptr) {
            ++g_searchlight_stats.guards;
        } else {
            const float ly = obj->pos.y, py = player->pos.y;
            const float dx = player->pos.x - obj->pos.x;
            const float dz = player->pos.z - obj->pos.z;
            obj->rot.y = obj->rot.y + Vec_AngleDifference(obj->rot.y, nf::fm::atan2(dx, dz)) * 0.15f;
            if (d->half < Vec_AngleDifference(obj->rot.y, d->rot.y)) obj->rot.y = d->rot.y - d->half;
            if (Vec_AngleDifference(obj->rot.y, d->rot.y) < -d->half) obj->rot.y = d->rot.y + d->half;
            const float up = nf::fm::atan2((py - ly) - 1.0f, std::sqrt(dx * dx + dz * dz));
            obj->rot.x = obj->rot.x + Vec_AngleDifference(obj->rot.x, -up) * 0.15f;
            if (d->half < Vec_AngleDifference(obj->rot.x, d->pitch)) obj->rot.x = d->pitch - d->half;
            if (Vec_AngleDifference(obj->rot.x, d->pitch) < -d->half) obj->rot.x = d->pitch + d->half;
        }
        parts = true;
    } else if (state == 0) {
        obj->rot.x = PS2Sinf(d->timer * 0.013089971f) * d->half + d->pitch;
        obj->rot.y = PS2Sinf(d->timer * 0.013089971f + 1.5707964f) * d->half + d->rot.y;
        d->timer = d->timer + FRAME_RATE_MUL;
        (void)std::fmod(d->timer, 480.0f);  // o original descarta o resultado
        parts = true;
    } else if (state == 2) {
        if (obj->unknown_fc != 0 && int32_t(GameState.frame + uint8_t(obj->created)) % 7 == 0) {
            _VECTOR spin{0.0f, 0.0f, Rand_FRand(6.2831855f), 0.0f};
            Gas_Init(obj->cel, &obj->pos, &spin, nullptr, 0.05f, 0.25f, 0x2000039, 7, 0x48403080, 1, 0x80);
        }
        if (0.8f <= obj->rot.x) return;
        obj->mode |= 0x20;
        obj->rot.x = obj->rot.x + 0.05f;
        return;
    } else if (state == 3) {
        if (d->start != 0 && switch_channels[d->start] != 0) {
            obj->state = 0;
            ++g_searchlight_stats.switched;
            Sound_Play3D(0xe5, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
    } else {
        parts = true;
    }
    if (parts && player == nullptr) {
        if (state != 1) ++g_searchlight_stats.guards;
        parts = false;
    }
    if (parts) {
        if (obj_tag* base = d->parts[0]; base != nullptr) {
            base->mode |= 0x20;
            base->rot.y = obj->rot.y;
        }
        if (obj_tag* spot = d->parts[2]; spot != nullptr) {
            const float dist = place_spot(obj, spot, player);
            SEARCHLIGHT_tag* s = sl_of(spot);
            if (obj->state == 0 && dist <= 2.0f) {
                obj->state = 1;
                s->timer = 0.0f;
                ++g_searchlight_stats.spotted;
            }
            if (obj->state == 1) {
                if (dist <= 2.0f) {
                    s->timer = 0.0f;
                } else {
                    s->timer = s->timer + 1.0f;
                }
                if (FRAME_RATE * 15.0f < s->timer) {
                    obj->state = 0;
                    ++g_searchlight_stats.lost;
                }
            }
        }
        if (obj_tag* beam = d->parts[3]; beam != nullptr) {
            beam->mode |= 0x24;
            View_SetDrawInAllViews(beam);
            const _VECTOR dir = spherical(1.0f, obj->rot.y, obj->rot.x);
            const _VECTOR up{player->pos.x - obj->pos.x, player->pos.y - obj->pos.y, player->pos.z - obj->pos.z,
                             player->pos.w - obj->pos.w};
            Mat_Align2Dir(&beam->matrix, &dir, &up, &up);
            Mat_SetTrans(&obj->pos, &beam->matrix);
        }
        if (obj_tag* glare = d->parts[4]; glare != nullptr) {
            if (obj->unknown_fc == 0) {
                View_SetDrawInNoViews(glare);
            } else {
                const _VECTOR v = spherical(0.4f, obj->rot.y, obj->rot.x);
                glare->pos = {obj->pos.x + v.x, obj->pos.y + v.y, obj->pos.z + v.z, obj->pos.w + v.w};
                _VECTOR head{};
                Player_GetHeadPos(&head, player, nullptr);
                const float dist = Vec_Dist3D(&head, &obj->pos);
                const _VECTOR to{head.x - obj->pos.x, head.y - obj->pos.y, head.z - obj->pos.z, 0.0f};
                const float facing = (v.x * to.x + v.y * to.y + v.z * to.z) / dist;
                if (0.2f < facing) {
                    View_SetDrawInAllViews(glare);
                    const float f = (facing - 0.2f) * 4.0f;
                    glare->mode |= 0x20;
                    const int32_t alpha = int32_t(f * 255.0f);
                    glare->scale = f * 4.0f;
                    glare->unknown_103[3] = uint8_t(alpha);
                    if ((alpha & 0xff) != 0) {
                        if (Collide_LineOfSight(&glb_viewer[0]->pos, &glare->pos, player->cel, glare, player, 8)) {
                            glare->flags |= 0x2000;
                            glare->scale = glare->scale * 1.5f;
                        } else {
                            glare->flags &= 0xffffdfffu;
                        }
                    }
                } else {
                    View_SetDrawInNoViews(glare);
                }
            }
        }
        obj->mode |= 0x20;
    }
    for (HITDATA_tag* h = obj->hits; h != nullptr; h = h->next) {
        if (h->obj == nullptr || h->obj->type != 5) continue;
        float damage;
        std::memcpy(&damage, &h->unknown_08, 4);
        if (!(0.0f < damage)) continue;
        obj->state = 2;
        kill_parts(d);
        Control_NextLOD(obj);
        ++g_searchlight_stats.shot;
        Sound_Play3D(0xe6, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
}

void searchlight_debug_power_on() { g_searchlight_stats = {}; }
