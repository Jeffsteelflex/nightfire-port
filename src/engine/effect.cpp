// Efeitos de impacto, fumaça, rastros e detritos. Ver effect.h.
//
// Divergências:
// - Um material acima de 22 (HITDATA_tag::surface & 0x3f) leria além de EffectInfo: vale o 22,
//   contado em g_effect_guards (zero nos dados reais).
// - Effect_Create lê o plano do acerto antes de testar se ele é nulo; aqui só depois.
// - Effect_Laser não testa se control_create_object criou a faísca; aqui sem objeto não há faísca.
// - Nas contas da VU0 o temporário escalado (vmulx.xyz) entra na soma .xyzw com w indefinido; aqui
//   esse w vale 0, como no resto do port.

#include "engine/effect.h"

#include <cmath>
#include <cstring>

#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

alignas(16) HEINFO_tag EffectInfo[kEffectInfoCount];
DLISTINFO_tag EffectList;
EffectGuards g_effect_guards;

namespace {
constexpr float kHalfPi = 1.5707964f;

uint32_t mp_flag(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
_VECTOR& plane_vec(HITDATA_tag* hit) { return *reinterpret_cast<_VECTOR*>(&hit->plane); }
_VECTOR& tangent_of(HITDATA_tag* hit) { return *reinterpret_cast<_VECTOR*>(hit->unknown_20); }
_VECTOR& row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
// a + b * k nos três primeiros componentes; w = o de a (vmulx.xyz + vadd.xyzw, ver o cabeçalho).
_VECTOR madd3(const _VECTOR& a, const _VECTOR& b, float k) {
    return {a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w};
}
// O anexo mais antigo em uso vai embora quando a lista de marcas acaba.
ATTACH_tag* new_mark() {
    void* e = DList_MoveFromFree2InUse(&EffectList);
    if (e == nullptr) {
        auto* oldest = reinterpret_cast<ATTACH_tag*>(nf::raw(EffectList.used.first));
        Effect_UnLink(oldest);
        DList_RemoveFromInUse2Free(&EffectList, reinterpret_cast<LLNODE_tag*>(oldest));
        e = DList_MoveFromFree2InUse(&EffectList);
    }
    return static_cast<ATTACH_tag*>(e);
}
// Pendura a marca: no objeto atingido (em coordenadas dele) se o acerto não foi numa sala, senão
// na sala.
void hang_mark(ATTACH_tag* e, HITDATA_tag* hit, cel_tag* cel) {
    if (hit->cel == nullptr) {
        obj_tag* o = hit->obj;
        if (o == nullptr) return;
        MATRIX inv;
        Mat_Copy(&o->matrix, &inv);
        Mat_Inverse(&inv);
        const _VECTOR world = row3(e->matrix);
        Concat(&inv, &e->matrix);
        RotPreTransVec(&inv, &world, &row3(e->matrix));
        ATTACH_tag* head = o->attach;
        e->prev = nullptr;
        e->next = head;
        if (head != nullptr) head->prev = e;
        o->attach = e;
        e->parent = o;
        e->cel = nullptr;
    } else {
        ATTACH_tag* head = cel->attach;
        e->prev = nullptr;
        e->next = head;
        if (head != nullptr) head->prev = e;
        cel->attach = e;
        e->cel = cel;
        e->parent = nullptr;
    }
}
uint8_t color_byte(float f) {
    if (f < 0.0f) return 0;
    return uint8_t(int32_t(f < 255.0f ? f : 255.0f));
}
}  // namespace

const HEINFO_tag& effect_info(uint8_t surface) {
    uint32_t s = surface & 0x3f;
    if (kEffectInfoCount <= s) {
        ++g_effect_guards.surface_out_of_table;
        s = kEffectInfoCount - 1;
    }
    return EffectInfo[s];
}

// [decomp] 0x001cf268 Effect_InitLists
// 200 marcas de 0x60 bytes (ResetMap_GameInit, depois do Mem_Init da fase).
void Effect_InitLists() { DList_Init(&EffectList, 0x60, 200); }

// [decomp] 0x001ce688 Effect_Create
// O que um acerto deixa conforme o material: guarda em HITDATA_tag +0x20 uma tangente da superfície
// (a soma de dois eixos perpendiculares à normal; a normal do vidro, o material 0x10 GLASS do EffectInfo, vem
// invertida), com a flag 0x20 cria as faíscas (o emissor do material) e os detritos; no modo história, pela flag, os
// efeitos de bala (1), de passo (2), de laser (0x10) ou de corpo (0x80).
void Effect_Create(HITDATA_tag* hit, obj_tag* obj, uint16_t flags, float volume) {
    if (hit == nullptr || obj == nullptr || (hit->surface & 0x3f) == 0) return;
    const plane_equ_tag saved = hit->plane;
    cel_tag* cel = hit->cel;
    if (cel == nullptr && hit->obj != nullptr) cel = hit->obj->cel;
    _VECTOR& n = plane_vec(hit);
    if ((hit->surface & 0x3f) == 0x10) {
        n.x = -n.x;
        n.y = -n.y;
        n.z = -n.z;
    }
    _VECTOR u, v;
    Vec_MakeOrthonormalBasis(&u, &v, &n);
    tangent_of(hit) = {u.x + v.x, u.y + v.y, u.z + v.z, u.w + v.w};
    const HEINFO_tag& info = effect_info(hit->surface);
    if ((flags & 0x20) != 0) {
        Emitter_Create(info.emitter, &hit->pos, &n, 0.0f, 0, 0, 0, 0.0f);
        if (info.debris_on != 0) Debris_Create(hit, &info, cel, uint16_t(Rand_Rand(info.debris_max) + 1));
    }
    if (mp_flag(0x180) != 0) return;
    hit->plane = saved;
    if ((flags & 1) != 0) {
        Effect_Bullet(hit, obj, cel, flags);
    } else if ((flags & 2) != 0) {
        Effect_Player(hit, obj, cel, flags, volume);
    } else if ((flags & 0x10) != 0) {
        Effect_Laser(hit, obj, cel, flags);
    } else if ((flags & 0x80) != 0) {
        Effect_Body(hit, obj, cel, flags);
    }
}

// [decomp] 0x001ce8a8 Effect_Bullet
// Bala: o som do material (flag 0x40); a fumaça e a poeira do material (sempre nos tiros do jogador,
// metade das vezes nos outros); e a marca de bala, 1 cm fora da superfície, virada para a normal.
void Effect_Bullet(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags) {
    const _VECTOR& n = plane_vec(hit);
    _VECTOR face;
    Vec_NormToRot(&n, &face);
    _VECTOR out{-ATAN2_APPROX(-n.y, std::sqrt(-n.x * -n.x + -n.z * -n.z)), ATAN2_APPROX(-n.x, -n.z), 0.0f, 0.0f};
    const HEINFO_tag& info = effect_info(hit->surface);
    if ((flags & 0x40) != 0) Sound_Play3D(info.sound_bullet, &hit->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    obj_tag* shooter;
    shooter = nf::p32_load<obj_tag>(static_cast<uint8_t*>(obj->data) + 0x30);
    bool smoke = shooter != nullptr && shooter->type == 3;
    if (!smoke) smoke = Rand_Rand(2) == 0;
    if (smoke) {
        for (const HEGAS_tag& g : info.gas) {
            if (g.entity == 0) continue;
            _VECTOR at = hit->pos;
            if (g.on_surface != 0) {
                if (0.3f < std::fabs(n.y)) continue;
                at = madd3(hit->pos, n, 0.05f);
            }
            _VECTOR* rot = const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR);
            if (g.on_surface != 0) {
                rot = &face;
            } else if (g.face_out != 0) {
                rot = &out;
            }
            Gas_Init(cel, &at, rot, nullptr, g.scale0, g.scale1, g.entity, g.kind, g.color0, g.color1, g.life);
        }
    }
    if (info.decal == 0) return;
    ATTACH_tag* e = new_mark();
    if (e == nullptr) return;
    Mat_Align2Dir(&e->matrix, &n, &CONSTANT_UP1, &CONSTANT_UP2);
    row3(e->matrix) = madd3(hit->pos, n, 0.01f);
    e->parent = hit->obj;
    hashtable_set_sub_object_to_entity_gfx(e, info.decal);
    hang_mark(e, hit, cel);
}

// [decomp] 0x001cec48 Effect_Player
// Passo: o som do material e, com as flags 4 ou 8 (um pé e o outro: AnimObjectUpdate as alterna com
// ScriptFootstepFoot), a pegada no chão sob o pé (osso 55 com a flag 4, o pé de AnimObjectInitialPose; 51 com a 8;
// senão a posição do objeto), virada para onde o personagem olha, se o pé estiver a até 20 cm do chão.
void Effect_Player(HITDATA_tag* hit, obj_tag* obj, cel_tag* cel, uint16_t flags, float volume) {
    const HEINFO_tag& info = effect_info(hit->surface);
    if ((flags & 0x40) != 0) {
        const uint32_t h = Sound_Play3D(info.sound_step, &hit->pos, volume, -1.0f, -1.0f, 0, 0, 0);
        Sound_SetAlertness(h, 0.0f);
    }
    const uint32_t decal = (flags & 4) != 0 ? info.decal_foot_b : info.decal_foot_a;
    if ((flags & 0xc) == 0 || decal == 0) return;
    MATRIX* m;
    if ((flags & 4) != 0) {
        m = &anim_of(obj)->bones[55];
    } else {
        m = &obj->matrix;
        if ((flags & 8) != 0) m = &anim_of(obj)->bones[51];
    }
    _VECTOR dir;
    if (ScriptCam == 0 && mp_flag(0x180) == 0 && obj->type == 3) {
        Mat_GetDir(&dir, &obj->matrix);
    } else {
        Mat_GetDir(&dir, m);
    }
    ATTACH_tag* e = new_mark();
    if (e == nullptr) return;
    Mat_Align2Up(&e->matrix, &plane_vec(hit), &dir);
    row3(e->matrix) = row3(*m);
    const float y = hit->pos.y;
    // Longe do chão a marca fica reservada sem aparecer, como no original.
    if (0.2f < std::fabs(e->matrix.m[3][1] - y)) return;
    e->parent = obj;
    e->matrix.m[3][1] = y + 0.05f;
    hashtable_set_sub_object_to_entity_gfx(e, decal);
    hang_mark(e, hit, cel);
}

// [decomp] 0x001cef38 Effect_Laser
// Laser: o som, a fumaça do material e, 60% das vezes, uma faísca (detrito de um tick); a cada
// dois quadros uma poeira fina perto do ponto.
void Effect_Laser(HITDATA_tag* hit, obj_tag*, cel_tag* cel, uint16_t flags) {
    const HEINFO_tag& info = effect_info(hit->surface);
    if ((flags & 0x40) != 0) Sound_Play(info.sound_step, 100.0f, 0, 0);
    const _VECTOR& n = plane_vec(hit);
    if (info.laser_gas != 0) {
        _VECTOR rot{-ATAN2_APPROX(n.y, std::sqrt(n.x * n.x + n.z * n.z)), ATAN2_APPROX(n.x, n.z), 0.0f, 0.0f};
        _VECTOR at = madd3(hit->pos, n, 0.005f);
        Gas_Init(cel, &at, &rot, nullptr, 2.0f, 3.0f, info.laser_gas, 5, 0x7f7f64ff, 0x643c0018, 10);
        if (0x28 < Rand_Rand(100)) {
            // Os modelos de faísca em 0x00303728 (cinco).
            static const uint32_t kSparks[5] = {0x20002d1, 0x20002d2, 0x20002d3, 0x20002d4, 0x20002d5};
            obj_tag* s = control_create_object(0x40, &hit->pos, nullptr, nullptr);
            if (s != nullptr) {
                const uint16_t life = 1;
                std::memcpy(static_cast<uint8_t*>(s->data) + 0x30, &life, 2);
                s->type = 0x10;
                s->scale = Rand_FRand(2.0f) + 2.0f;
                s->rot.z = Rand_FRand(6.2831855f);
                s->pos.x += n.x * 0.01f;
                s->pos.y += n.y * 0.01f;
                s->pos.z += n.z * 0.01f;
                hashtable_set_object_to_entity_gfx(s, kSparks[Rand_Rand(5)]);
                s->flags |= 0x20;
                control_link_object_to_cel(s, cel);
            }
        }
    }
    if ((GameState.frame & 1) != 0) {
        _VECTOR r{0.0f, 0.0f, 0.0f, 0.0f};
        Rand_FRandHalf_Vec(&r, 0.1f);
        Vec_Mul(&r, &tangent_of(hit));
        r = {r.x + hit->pos.x, r.y + hit->pos.y, r.z + hit->pos.z, r.w + hit->pos.w};
        Gas_Init(cel, &r, const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), nullptr, 0.005f, 0.02f, 0x2000039, 0,
                 0xc8c8c87f, 0x20202006, 0x15);
    }
}

// [decomp] 0x001cf400 Effect_Body
// Corpo atingido: só o som do material, que deixa os inimigos mais atentos.
void Effect_Body(HITDATA_tag* hit, obj_tag*, cel_tag*, uint16_t flags) {
    if ((flags & 0x40) == 0) return;
    const uint32_t h = Sound_Play3D(effect_info(hit->surface).sound_body, &hit->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Sound_ModAlertness(h, 0.5f);
}

// [decomp] 0x001cf300 Effect_UnLink
// Tira a marca da lista de anexos da sala ou do objeto (sem devolvê-la à EffectList).
void Effect_UnLink(ATTACH_tag* e) {
    if (e == nullptr) return;
    ATTACH_tag* next = e->next;
    ATTACH_tag* prev = e->prev;
    if (next != nullptr) next->prev = prev;
    if (prev != nullptr) prev->next = next;
    if (e->cel != nullptr && e->cel->attach == e) e->cel->attach = next;
    if (e->parent != nullptr && e->parent->attach == e) e->parent->attach = next;
    e->prev = nullptr;
    e->next = nullptr;
}

// [decomp] 0x001cf360 Effect_RemoveSObjs
// O objeto vai embora (ou quebra): todas as marcas dele voltam à EffectList.
void Effect_RemoveSObjs(obj_tag* obj) {
    ATTACH_tag* e = obj->attach;
    obj->attach = nullptr;
    while (e != nullptr) {
        ATTACH_tag* next = e->next;
        ATTACH_tag* prev = e->prev;
        if (next != nullptr) next->prev = prev;
        if (prev != nullptr) prev->next = next;
        if (e->cel != nullptr && e->cel->attach == e) e->cel->attach = next;
        if (e->parent != nullptr && e->parent->attach == e) e->parent->attach = next;
        e->next = nullptr;
        e->prev = nullptr;
        DList_RemoveFromInUse2Free(&EffectList, reinterpret_cast<LLNODE_tag*>(e));
        e = next;
    }
}

// ------------------------------------------------------------------ fumaça

// Dados de um objeto de fumaça (tipo 0xe; 0x40 bytes, 0x60 nos tipos 9 e 10, que soltam fumaça).
struct GAS_tag {
    _VECTOR vel;       // 0x00 por tick; nos tipos 9 e 10, a posição de onde a fumaça sai
    float dcolor[4];   // 0x10 r, g, b, a por tick
    float color[4];    // 0x20
    float spin;        // 0x30 giro em z por tick
    float dscale;      // 0x34 por tick
    float life;        // 0x38 ticks
    uint8_t kind;      // 0x3c
    uint8_t pad[3];
    uint32_t color0;   // 0x40 só nos tipos 9 e 10: a fumaça que soltam
    uint32_t color1;   // 0x44
    uint32_t entity;   // 0x48
    float scale0;      // 0x4c
    float scale1;      // 0x50
    float life0;       // 0x54
    uint8_t pad2[8];
};
static_assert(sizeof(GAS_tag) == 0x60 && offsetof(GAS_tag, kind) == 0x3c && offsetof(GAS_tag, life0) == 0x54);

// [decomp] 0x001aacc8 Gas_Init
// Um objeto de fumaça (tipo 0xe) que cresce de scale0 a scale1 e muda de cor de color0 a color1 em
// "life" ticks, girando devagar. A velocidade vem de "vel" (m/s) ou do tipo: 7 o vento da sala, 9 e
// 10 ficam parados e soltam mais fumaça (escondidos), os outros sobem 1 m/s.
obj_tag* Gas_Init(cel_tag* cel, _VECTOR* pos, _VECTOR* rot, _VECTOR* vel, float scale0, float scale1,
                  uint32_t entity, uint32_t kind, uint32_t color0, uint32_t color1, uint16_t life) {
    if (cel == nullptr || life == 0) return nullptr;
    const bool emits = kind < 0xb && 8 < kind;
    obj_tag* obj = control_create_object(emits ? 0x60 : 0x40, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    auto* g = static_cast<GAS_tag*>(obj->data);
    float t = float(life);
    if (emits) {
        g->life0 = t;
        g->color0 = color0;
        g->color1 = color1;
        g->scale0 = scale0;
        g->entity = entity;
        g->scale1 = scale1;
    }
    g->life = t;
    t = 1.0f / t;
    g->kind = uint8_t(kind);
    obj->type = 0xe;
    obj->scale = scale0;
    obj->flags |= 0x200;
    obj->unknown_103[0] = uint8_t(color0 >> 24);
    obj->unknown_103[1] = uint8_t(color0 >> 16);
    obj->unknown_103[2] = uint8_t(color0 >> 8);
    obj->unknown_103[3] = uint8_t(color0);
    g->dscale = (scale1 - scale0) * t;
    g->dcolor[0] = (float(color1 >> 24) - float(color0 >> 24)) * t;
    g->dcolor[1] = (float((color1 >> 16) & 0xff) - float((color0 >> 16) & 0xff)) * t;
    g->dcolor[2] = (float((color1 >> 8) & 0xff) - float((color0 >> 8) & 0xff)) * t;
    g->dcolor[3] = (float(color1 & 0xff) - float(color0 & 0xff)) * t;
    g->color[0] = float(obj->unknown_103[0]);
    g->color[1] = float(obj->unknown_103[1]);
    g->color[2] = float(obj->unknown_103[2]);
    g->color[3] = float(obj->unknown_103[3]);
    g->spin = Rand_FRand(REC_FRAME_RATE * kHalfPi);
    if (kind == 0xb) g->spin *= 0.25f;
    if (0x7f < Rand_Rand(0x100)) g->spin = -g->spin;
    hashtable_set_object_to_entity_gfx(obj, entity);
    obj->flags |= 0x20020120;
    if (build_LinkToRoom(obj, 0, glb_world) == 0) control_link_object_to_cel(obj, cel);
    if (vel != nullptr) {
        if (kind == 4) {
            g->vel.x = vel->x * REC_FRAME_RATE + Rand_FRand(0.05f);
            g->vel.y = vel->y * REC_FRAME_RATE;
            g->vel.z = vel->z * REC_FRAME_RATE + Rand_FRand(0.05f);
        } else {
            g->vel.x = vel->x * REC_FRAME_RATE;
            g->vel.y = vel->y * REC_FRAME_RATE;
            g->vel.z = vel->z * REC_FRAME_RATE;
        }
        return obj;
    }
    if (kind == 7) {
        Env_GetWindVel(cel, pos, &g->vel);
        g->vel.x = (g->vel.x + (Rand_FRand(0.2f) - 0.1f)) * REC_FRAME_RATE;
        g->vel.y = (g->vel.y + Rand_FRand(0.2f)) * REC_FRAME_RATE;
        g->vel.z = (g->vel.z + (Rand_FRand(0.2f) - 0.1f)) * REC_FRAME_RATE;
        return obj;
    }
    if (kind == 9 || kind == 10) {
        if (kind == 10) g->life *= 0.5f;
        g->vel = *pos;
        obj->flags |= 0x10;
        return obj;
    }
    g->vel.z = 0.0f;
    g->vel.y = REC_FRAME_RATE;
    g->vel.x = 0.0f;
    return obj;
}

// [decomp] 0x001ab2d0 gases_smoke
// A atualização da fumaça (tipo 0xe) pelo tipo: anda (com uma fração da gravidade puxando para
// cima, sem passar do teto da sala), gira, cresce e muda de cor; os tipos 9 e 10 soltam mais fumaça
// de tempos em tempos. Some quando a vida acaba.
void gases_smoke(obj_tag* obj) {
    auto* g = static_cast<GAS_tag*>(obj->data);
    const auto lift = [&](float k) {
        obj->pos = madd3(obj->pos, WldGravity, k);
        obj->pos = {obj->pos.x + g->vel.x, obj->pos.y + g->vel.y, obj->pos.z + g->vel.z, obj->pos.w + g->vel.w};
    };
    // O teto da sala (box_max.y), lido só nos tipos que o usam.
    const auto ceiling = [&] { return obj->cel->box_max.y; };
    switch (g->kind) {
        case 0:
            Vec_MulR32(&g->vel, &g->vel, FRAME_RATE_DIV * 0.95f);
            [[fallthrough]];
        case 7:
            obj->rot.z += g->spin * FRAME_RATE_MUL;
            lift(REC_FRAME_RATE * -0.02f * FRAME_RATE_MUL);
            if (ceiling() < obj->pos.y) obj->pos.y = ceiling();
            obj->scale += g->dscale * FRAME_RATE_MUL;
            break;
        case 1:
        case 5:
            obj->scale += g->dscale * FRAME_RATE_MUL;
            break;
        case 3:
        case 6:
            obj->rot.z += g->spin;
            lift(REC_FRAME_RATE * -0.1f * FRAME_RATE_MUL);
            if (ceiling() < obj->pos.y) {
                obj->pos.y = ceiling();
                break;
            }
            obj->scale += g->dscale * FRAME_RATE_MUL;
            break;
        case 4: {
            g->vel = madd3(g->vel, WldGravity, REC_FRAME_RATE * 0.01f * FRAME_RATE_MUL);
            obj->pos = {obj->pos.x + g->vel.x, obj->pos.y + g->vel.y, obj->pos.z + g->vel.z, obj->pos.w + g->vel.w};
            const float drag = FRAME_RATE_MUL * 0.95f - REC_FRAME_RATE;
            obj->scale += g->dscale * FRAME_RATE_MUL;
            g->vel.z *= drag;
            g->vel.x *= drag;
            break;
        }
        case 8:
            obj->scale += g->dscale * FRAME_RATE_MUL;
            obj->pos = madd3(obj->pos, WldGravity, REC_FRAME_RATE * 0.05f * FRAME_RATE_MUL);
            break;
        case 9:
            obj->flags |= 0x10;
            if (float(GameState.frame - obj->created) < FRAME_RATE_MUL * 20.0f) return;
            if ((GameState.frame & 3) == 3) {
                Gas_Init(obj->cel, &g->vel, &obj->rot, nullptr, g->scale0, g->scale1, g->entity, 8,
                         (g->color0 & 0xffffff00u) | obj->unknown_103[3], g->color1, uint16_t(int32_t(g->life0)));
            }
            break;
        case 10: {
            obj->flags |= 0x10;
            const uint32_t mask = uint32_t(int32_t(FRAME_RATE_MUL * 7.0f)) & 0xffff;
            if (mask != (GameState.frame & mask)) break;
            _VECTOR up{0.0f, REC_FRAME_RATE + REC_FRAME_RATE, 0.0f, 0.0f};
            Gas_Init(obj->cel, &g->vel, &obj->rot, &up, g->scale0, g->scale1, g->entity, 7,
                     (g->color0 & 0xffffff00u) | obj->unknown_103[3], g->color1, uint16_t(int32_t(g->life0)));
            break;
        }
        case 0xb:
            obj->rot.z += g->spin;
            lift(REC_FRAME_RATE * -0.02f * FRAME_RATE_MUL);
            if (ceiling() < obj->pos.y) obj->pos.y = ceiling();
            obj->scale += g->dscale * FRAME_RATE_MUL;
            if (g->life < 30.0f) g->color[3] = g->life * 8.5f;
            break;
        default:
            break;
    }
    for (int i = 0; i < 4; ++i) g->color[i] += g->dcolor[i];
    for (int i = 0; i < 4; ++i) obj->unknown_103[i] = color_byte(g->color[i]);
    g->life -= FRAME_RATE_MUL;
    if (g->life <= 0.0f) {
        obj->life |= 1;
    } else {
        obj->mode |= 0x20;
    }
}

// [decomp] 0x0017c048 Env_GetWindVel
// O vento da sala (o ambiente em cel_tag::env) no ponto: a direção dele vezes a força, modulada
// pela posição ao longo da direção. Sem ambiente ou sem vento, zero.
void Env_GetWindVel(cel_tag* cel, _VECTOR* pos, _VECTOR* out) {
    if (out == nullptr) return;
    const uint8_t* env = cel != nullptr ? static_cast<const uint8_t*>(cel->env) : nullptr;
    float on = 0.0f;
    if (env != nullptr) std::memcpy(&on, env + 0x34, 4);
    if (env == nullptr || on == 0.0f) {
        Vec_Zero(out);
        return;
    }
    _VECTOR dir;
    float strength, gust, spread;
    std::memcpy(&dir, env, sizeof(dir));
    std::memcpy(&strength, env + 0x30, 4);
    std::memcpy(&gust, env + 0x38, 4);
    std::memcpy(&spread, env + 0x44, 4);
    const _VECTOR d{(pos->x - cel->sphere.x) * dir.x, (pos->y - cel->sphere.y) * dir.y, (pos->z - cel->sphere.z) * dir.z,
                    0.0f};
    Vec_MulR32(out, &dir, strength * ((d.x + d.y + d.z) * spread + 1.0f) * std::fabs(gust) * 0.5f);
}

// [decomp] 0x0017c118 Env_WaterRipple
// Um passo (ou um tiro de drone) na água rasa: com o material do acerto 13 ou 14 (água), uma fumaça de marola no ponto
// (entidade 0x2000129, tipo 1, de 0,5 a 1,01, cinza 0x7f7f7f7f, 0x3f ticks), na sala do acerto ou na do objeto
// atingido.
void Env_WaterRipple(HITDATA_tag* hit) {
    if (hit == nullptr || 2u <= uint32_t((hit->surface & 0x3f) - 0xd)) return;
    cel_tag* cel = hit->cel;
    if (cel == nullptr) {
        if (hit->obj != nullptr) cel = hit->obj->cel;
        if (cel == nullptr) return;
    }
    Gas_Init(cel, &hit->pos, nullptr, nullptr, 0.5f, 1.01f, 0x2000129, 1, 0x7f7f7f7fu, 2, 0x3f);
}

// ------------------------------------------------------------------ rastros

// Dados de um rastro (tipo 0x41, 0x30 bytes).
struct SWOOSH_tag {
    _VECTOR from;     // 0x00
    _VECTOR to;       // 0x10
    int16_t timer;    // 0x20 ticks que faltam
    int16_t total;    // 0x22
    uint8_t drift;    // 0x24 sobe 1 mm por tick
    uint8_t pad[0x0b];
};
static_assert(sizeof(SWOOSH_tag) == 0x30);

// [decomp] 0x0018d8e8 Swoosh_Create
// Um rastro (modelo esticado de "from" a "to") que some em "width" quadros de 60 Hz (a metade no
// multijogador, a partir de 61).
obj_tag* Swoosh_Create(_VECTOR* from, _VECTOR* to, int16_t width, uint32_t entity, uint8_t kind) {
    obj_tag* obj = control_create_object(0x30, to, nullptr, nullptr);
    if (obj == nullptr) return nullptr;
    int32_t n = width;
    if (mp_flag(0x180) != 0 && 0x3c < n) n = (n + int32_t(uint16_t(width) >> 15)) >> 1;
    auto* s = static_cast<SWOOSH_tag*>(obj->data);
    s->from = *from;
    s->to = *to;
    s->drift = kind;
    const int16_t ticks = int16_t(int32_t(float(n) / FRAME_RATE_MUL));
    s->total = ticks;
    s->timer = ticks;
    obj->type = 0x41;
    obj->scale = 1.0f;
    obj->mode |= 0x204;
    hashtable_set_object_to_entity_gfx(obj, entity);
    return obj;
}

// [decomp] 0x0018d200 Swoosh_Update
// O rastro vira para a câmera 0 em volta da linha "from"-"to", tem o comprimento dela e afina e
// some com o tempo (largura e alfa pela fração que falta).
void Swoosh_Update(obj_tag* obj) {
    auto* s = static_cast<SWOOSH_tag*>(obj->data);
    const int16_t timer = s->timer;
    s->timer = int16_t(timer - 1);
    if (s->timer < 1) obj->life |= 1;
    obj->mode |= 0x20;
    obj->unknown_103[3] = uint8_t(int32_t((float(int32_t(s->timer)) * 128.0f) / float(int32_t(s->total))));
    if (s->drift != 0) {
        s->from.x += 0.001f;
        s->to.x += 0.001f;
        obj->pos.x += 0.001f;
        s->from.y += 0.001f;
        s->to.y += 0.001f;
        obj->pos.y += 0.001f;
    }
    const _VECTOR along{s->to.x - s->from.x, s->to.y - s->from.y, s->to.z - s->from.z, s->to.w - s->from.w};
    const _VECTOR& eye = glb_viewer[0]->pos;
    const _VECTOR to_eye{eye.x - obj->pos.x, eye.y - obj->pos.y, eye.z - obj->pos.z, eye.w - obj->pos.w};
    Mat_Align2Dir(&obj->matrix, &along, &to_eye, &to_eye);
    row3(obj->matrix) = obj->pos;
    const float len = Vec_Dist3D(&s->from, &s->to);
    Mat_Scale3f(&obj->matrix, &obj->matrix, float(int32_t(s->timer)) / float(int32_t(s->total)), 1.0f, len);
    obj->scale = Vec_Dist3D(&s->from, &s->to);
}

// ------------------------------------------------------------------ detritos

// [decomp] 0x001aaa00 Debris_Create
// "count" detritos do material (tipo 0x10, Debris_Update): saem do ponto com metade da velocidade
// do material na direção da normal e o resto sorteado ao longo da tangente, girando.
void Debris_Create(HITDATA_tag* hit, const HEINFO_tag* info, cel_tag* cel, uint16_t count) {
    if (info->debris == 0) return;
    const float speed = info->debris_speed;
    const float half = speed * 0.5f;
    const _VECTOR& n = plane_vec(hit);
    const _VECTOR& t = tangent_of(hit);
    for (uint16_t i = 0; i < count; ++i) {
        obj_tag* obj = control_create_object(0x40, &hit->pos, nullptr, nullptr);
        if (obj == nullptr) continue;
        obj->type = 0x10;
        uint8_t* d = static_cast<uint8_t*>(obj->data);
        obj->unknown_103[3] = 0xff;
        obj->state = 0;
        obj->scale = Rand_FRand(0.1f) + 0.4f;
        const int16_t extra = int16_t(Rand_Rand(uint32_t(info->debris_life >> 1)));
        d[0x32] = 1;
        const uint16_t life = uint16_t((info->debris_life >> 1) + extra);
        std::memcpy(d + 0x30, &life, 2);
        _VECTOR vel{};
        vel.x = (half * n.x + t.x * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        vel.y = (half * n.y + t.y * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        vel.z = (half * n.z + t.z * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        std::memcpy(d + 0x10, &vel.x, 12);
        _VECTOR spin{};
        std::memcpy(&spin, d + 0x20, sizeof(spin));
        Rand_FRandHalf_Vec(&spin, info->debris_spin);
        std::memcpy(d + 0x20, &spin, sizeof(spin));
        hashtable_set_object_to_entity_gfx(obj, info->debris);
        obj->flags |= 0x20000020;
        if (build_LinkToRoom(obj, 0, glb_world) == 0) control_link_object_to_cel(obj, cel);
    }
}

// [decomp] 0x001ac808 Debris_CreateEx
// Detritos de uma explosão: "count" modelos sorteados de "entities", com velocidade ao longo de
// "dir" e espalhados por "side".
void Debris_CreateEx(_VECTOR* pos, _VECTOR* dir, _VECTOR* side, float speed, uint32_t life, uint32_t count,
                     cel_tag* cel, uint32_t* entities, uint32_t n_entities) {
    if (entities == nullptr) return;
    for (uint32_t i = 0; i < count; i = (i + 1) & 0xffff) {
        obj_tag* obj = control_create_object(0x40, pos, nullptr, nullptr);
        if (obj == nullptr) continue;
        uint8_t* d = static_cast<uint8_t*>(obj->data);
        obj->type = 0x10;
        obj->scale = Rand_FRand(0.1f) + 0.4f;
        obj->state = 0;
        obj->unknown_103[3] = 0xff;
        const int16_t extra = int16_t(Rand_Rand(life >> 1));
        d[0x32] = 1;
        const int16_t ticks = int16_t(extra + int16_t(life >> 1));
        std::memcpy(d + 0x30, &ticks, 2);
        const float k = speed * (Rand_FRand(1.5f) + 0.1f) * 0.5f;
        _VECTOR vel{};
        vel.x = (k * dir->x + side->x * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        vel.y = (k * dir->y + side->y * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        vel.z = (k * dir->z + side->z * Rand_FRandHalf(speed)) * REC_FRAME_RATE;
        std::memcpy(d + 0x10, &vel.x, 12);
        _VECTOR spin{};
        std::memcpy(&spin, d + 0x20, sizeof(spin));
        Rand_FRandHalf_Vec(&spin, 0.23561947f);
        std::memcpy(d + 0x20, &spin, sizeof(spin));
        hashtable_set_object_to_entity_gfx(obj, entities[Rand_Rand(n_entities - 1)]);
        obj->flags |= 0x20000020;
        if (build_LinkToRoom(obj, 0, glb_world) == 0) control_link_object_to_cel(obj, cel);
    }
}

void effect_debug_power_on() {
    std::memset(&EffectList, 0, sizeof(EffectList));
    g_effect_guards = {};
}
