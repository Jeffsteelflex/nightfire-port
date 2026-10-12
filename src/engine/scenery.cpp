// Os enfeites das fases. Ver scenery.h.
//
// Divergências do original (só no port; também nas funções):
// - Leaf_Create põe w = 0 na velocidade da folha (o original guarda o w da pilha, sem valor, e o soma à posição).
// - Particles_Create/Update: psiCreateParticles e psiParticleUpdate do PS2 são vazias e devolvem o que sobrou no
//   registro v0; no PC devolvem 0 (o objeto nasce e some no primeiro tick; nos dois casos nada é desenhado).
// - Env_Tree_Create, Leaf_Create e rotor_init conferem o objeto criado (o original não, numa memória cheia).

#include "engine/scenery.h"

#include <cstdio>
#include <cstring>

#include "engine/anim.h"
#include "engine/collide.h"
#include "engine/effect.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/objects.h"
#include "engine/parsemap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi_draw.h"

SceneryStats g_scenery_stats;

void scenery_debug_power_on() { g_scenery_stats = SceneryStats{}; }

// ------------------------------------------------------------------ ondulações

// [decomp] 0x001acc90 Ripples_Create
// Ondulações (posicionamento 0x21): o raio (parâmetro 0) e a frequência (parâmetro 1); sem frequência mas com raio, um
// anel por tick; sem os dois, raio 1 a cada 30 ticks. O objeto 0x34 tem o modelo do posicionamento e fica na sala.
obj_tag* Ripples_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    obj_tag* obj = control_create_object(4, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.ripples;
    auto* r = static_cast<RIPPLES_tag*>(obj->data);
    r->radius = int16_t(level->params[0]);
    r->rate = int16_t(level->params[1]);
    if (r->rate == 0 && r->radius == 0) {
        r->radius = 1;
        r->rate = 0x1e;
    }
    obj->glist = glist;
    obj->type = 0x34;
    obj->flags = glist->flags;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001ac028 Ripples_Update
// Com Rand_Rand(rate / FRAME_RATE_MUL) == 0: um anel de água (0x2000129) num ponto a até "radius" metros em x e z,
// tamanho 0,05 a 0,1 crescendo ao dobro, cor 0x7f7f7fff para 6, 42 ticks (Gas_Init tipo 1).
void Ripples_Update(obj_tag* obj) {
    const auto* r = static_cast<const RIPPLES_tag*>(obj->data);
    const float n = float(r->rate) / FRAME_RATE_MUL;
    if (Rand_Rand(n < 4294967296.0f && 0.0f <= n ? uint32_t(n) : 0u) != 0) return;
    _VECTOR pos = obj->pos;
    const float radius = float(r->radius);
    pos.x += Rand_FRand(radius + radius) - radius;
    pos.z += Rand_FRand(radius + radius) - radius;
    const float size = Rand_FRand(0.05f) + 0.05f;
    if (Gas_Init(obj->cel, &pos, nullptr, nullptr, size, size + size, 0x2000129, 1, 0x7f7f7fffu, 6, 0x2a) != nullptr) {
        ++g_scenery_stats.rings;
    }
}

// [decomp] 0x0017be00 Env_Breath
// O bafo do jogador e dos personagens (pela boca "mouth", na direção "dir"), numa sala já vista (cel_tag::misc[8]): a
// cada décimo de segundo do primeiro terço de cada 2 s desde a criação do objeto. Com a chuva de partículas no ambiente
// (GlobalEnv do tipo 0) e fora de sala coberta (bit 28 do tipo da sala), uma nuvem branca (0x200021d, 0,05 a 0,1, alfa
// 0x80 para 1, 40 ticks); senão só com a câmera 0 no modo de visão 2 (o infravermelho), uma nuvem girada ao acaso (0,05
// a 0,2, alfa 0xc8 para 1) que só aparece nesse modo (flag 0x400).
// Só no port: o w da posição é o da boca (o original soma um w sem valor) e o objeto do infravermelho é conferido.
void Env_Breath(_VECTOR* mouth, _VECTOR* dir, cel_tag* cel, uint32_t created) {
    if (cel == nullptr || cel->misc[0xa0 - 0x98] == 0) return;
    const uint32_t t = GameState.frame - created;
    const uint32_t step = FRAME_RATE_INT / 10;
    if (step == 0 || t % step != 0) return;
    const uint32_t phase = t % (FRAME_RATE_INT << 1);
    if ((FRAME_RATE + FRAME_RATE) * 0.33333334f < float(phase)) return;
    _VECTOR pos{mouth->x + dir->x * 0.1f, mouth->y + dir->y * 0.1f, mouth->z + dir->z * 0.1f, mouth->w};
    if (GlobalEnv != nullptr && GlobalEnv->kind == 0 && (cel->type & 0x10000000u) == 0) {
        Gas_Init(cel, &pos, nullptr, dir, 0.05f, 0.1f, 0x200021d, 0, 0xffffff80u, 0xffffff01u, 0x28);
        ++g_scenery_stats.breaths;
        return;
    }
    const viewer_tag* v = glb_viewer[0];
    if (v == nullptr || v->mode != 2) return;
    _VECTOR rot{0.0f, 0.0f, Rand_FRand(6.2831855f), 0.0f};
    obj_tag* gas = Gas_Init(cel, &pos, &rot, dir, 0.05f, 0.2f, 0x200021d, 0, 0xffffffc8u, 0xffffff01u, 0x28);
    if (gas != nullptr) gas->flags |= 0x400;
    ++g_scenery_stats.breaths;
}

// ------------------------------------------------------------------ folhas

// [decomp] 0x001acbd0 LeafGen_Create
// Um gerador de folhas (posicionamento 0x2b): objeto 0x31 com o modelo do posicionamento, na sala.
obj_tag* LeafGen_Create(_VECTOR* pos, _VECTOR* rot, level_tag*, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.leaf_gens;
    obj->glist = glist;
    obj->type = 0x31;
    obj->flags = glist->flags;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001acc48 LeafGen_Update
// Uma folha com chance 1 em FRAME_RATE_INT / 2 por tick.
void LeafGen_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    if (Rand_Rand(FRAME_RATE_INT >> 1) == 0) Leaf_Create(obj, 1);
}

// [decomp] 0x001abc88 Leaf_Create
// "count" folhas do gerador: a velocidade (1 a 3 cm por tick na direção do giro y do gerador; um sorteio de 0,02 antes
// fica sem uso, como no original) e, para cada uma, um ponto ao acaso no cubo do raio da esfera do gerador; objeto 0x2e
// com o modelo 0x20002ca, alfa 0xff, vida 256 ticks a 60 Hz, giro ao acaso (+-0,05 por tick), na sala do ponto (ou na
// do gerador).
void Leaf_Create(obj_tag* gen, uint16_t count) {
    const float r = gen->sphere.w;
    float c, s;
    PS2Sinf2(gen->rot.y + 1.5707964f, gen->rot.y, &c, &s);
    Rand_FRand(0.02f);
    _VECTOR vel{};
    vel.x = (Rand_FRand(0.02f) + 0.01f) * s;
    vel.z = (Rand_FRand(0.02f) + 0.01f) * c;
    vel.y = 0.0f;
    for (uint16_t i = 0; i < count; ++i) {
        _VECTOR pos = gen->pos;
        pos.x += Rand_FRand(r + r) - r;
        pos.z += Rand_FRand(r + r) - r;
        pos.y += Rand_FRand(r + r) - r;
        obj_tag* leaf = control_create_object(0x40, &pos, nullptr, nullptr);
        if (leaf == nullptr) continue;
        ++g_scenery_stats.leaves;
        leaf->type = 0x2e;
        leaf->unknown_103[3] = 0xff;
        auto* l = static_cast<LEAF_tag*>(leaf->data);
        l->life = int16_t(int32_t(256.0f / FRAME_RATE_MUL));
        const float m = FRAME_RATE_MUL;
        l->vel = {vel.x * m, vel.y * m, vel.z * m, vel.w};
        Rand_FRandHalf_Vec(&l->spin, 0.1f);
        l->spin = {l->spin.x * m, l->spin.y * m, l->spin.z * m, l->spin.w};
        hashtable_set_object_to_entity_gfx(leaf, 0x20002ca);
        leaf->flags |= 0x20000020u;
        if (build_LinkToRoom(leaf, 0, glb_world) == 0) control_link_object_to_cel(leaf, gen->cel);
    }
}

// [decomp] 0x001abee0 Leaf_Update
// Some com a vida no fim ou o alfa esgotado (o alfa cai "fade" por tick: 0). Senão anda pela velocidade, cai em
// zigue-zague (y de -0,05 a +0,025 por tick), a velocidade em x e z vaga (+-0,0025 por tick) e gira.
void Leaf_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    auto* l = static_cast<LEAF_tag*>(obj->data);
    if (l->life < 1) {
        obj->life |= 1;
        return;
    }
    const int32_t alpha = int32_t(obj->unknown_103[3]) - int32_t(l->fade);
    if (alpha <= 0) {
        obj->life |= 1;
        return;
    }
    obj->unknown_103[3] = uint8_t(alpha);
    if (0 < l->life) --l->life;
    obj->pos = {obj->pos.x + l->vel.x, obj->pos.y + l->vel.y, obj->pos.z + l->vel.z, obj->pos.w + l->vel.w};
    obj->pos.y -= (Rand_FRand(0.075f) - 0.025f) * FRAME_RATE_MUL;
    l->vel.x += (Rand_FRand(0.005f) - 0.0025f) * FRAME_RATE_MUL;
    l->vel.z += (Rand_FRand(0.005f) - 0.0025f) * FRAME_RATE_MUL;
    obj->rot = {obj->rot.x + l->spin.x, obj->rot.y + l->spin.y, obj->rot.z + l->spin.z, obj->rot.w + l->spin.w};
    obj->mode |= 0x20;
}

// ------------------------------------------------------------------ árvores

// [decomp] 0x0017c200 Env_Tree_Create
// Uma árvore animada (posicionamento 0xf3): objeto 0x3f na sala, com a pele do parâmetro 0 e o script do parâmetro 1
// (0x600008c troca para 0x600008d num Rand_Random ímpar). Fora da 07000043 o script fica em laço (bit 31 das flags) e
// começa num quadro inteiro ao acaso; na 07000043 sem o bit. As flags perdem 0x20 e ganham 0x80.
obj_tag* Env_Tree_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.trees;
    build_LinkToRoom(obj, 0, glb_world);
    obj->type = 0x3f;
    AnimObjectNew(obj, level->params[0]);
    uint32_t script = level->params[1];
    if (script == 0x600008cu && (Rand_Random() & 1) != 0) script = 0x600008du;
    ANIMSCRIPT_tag* s = AnimScriptAdd(obj, script);
    if (s != nullptr) {  // só no port: o original não confere
        if (GameState.load_file == 0x7000043u) {
            s->flags &= 0x7fffffffu;
        } else {
            s->flags |= 0x80000000u;
            s->frame = float(int32_t(Rand_FRand(float(s->frame_count))));
        }
    }
    obj->flags = (obj->flags | 0x80u) & 0xffffffdfu;
    return obj;
}

// [decomp] 0x0017c328 Env_Tree_Update
void Env_Tree_Update(obj_tag* obj) {
    if (ScriptCam == 0) AnimObjectUpdate(obj);
}

// ------------------------------------------------------------------ hélices

// [decomp] 0x0018f4b8 rotor_init
// Uma hélice (posicionamento 0x71): objeto 7 na sala com o modelo e as flags dele; o giro por tick é a própria
// rotação do posicionamento; o canal é o parâmetro 1 (o 0, "unused2", e "unused" não são lidos).
obj_tag* rotor_init(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist, uint16_t, uint8_t, uint8_t channel) {
    obj_tag* obj = control_create_object(0x20, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.rotors;
    auto* r = static_cast<ROTOR_tag*>(obj->data);
    obj->type = 7;
    r->channel = channel;
    r->spin.x = rot->x;
    r->spin.y = rot->y;
    r->spin.z = rot->z;
    if (glist != nullptr) {
        obj->glist = glist;
        obj->flags = glist->flags;
    }
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x0018f568 rotor_update
// Sem canal, ou com o canal no valor guardado (0: desligado), a rotação anda o giro x FRAME_RATE_MUL (xyz; o w soma o
// w do giro, 0) e a matriz é refeita (mode 0x20).
void rotor_update(obj_tag* obj) {
    const auto* r = static_cast<const ROTOR_tag*>(obj->data);
    if (r->channel != 0 && switch_channels[r->channel] != r->value) return;
    obj->mode |= 0x20;
    const float m = FRAME_RATE_MUL;
    obj->rot = {obj->rot.x + r->spin.x * m, obj->rot.y + r->spin.y * m, obj->rot.z + r->spin.z * m,
                obj->rot.w + r->spin.w};
}

// ------------------------------------------------------------------ paredes de um lado

// [decomp] 0x001e7ce8 OneSided_Create
obj_tag* OneSided_Create(_VECTOR* pos, _VECTOR* rot, celglist_tag* glist) {
    obj_tag* obj = control_create_object(0, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.one_sided;
    obj->glist = glist;
    obj->type = 0x4c;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001e7d58 OneSided_Update
// Escondida (flag 0x10) com a câmera 0 atrás dela: (posição - câmera) . frente (linha 2 da matriz) < 0.
void OneSided_Update(obj_tag* obj) {
    const viewer_tag* v = glb_viewer[0];
    if (v == nullptr) return;  // só no port: o original leria a câmera nula
    const _VECTOR d{obj->pos.x - v->pos.x, obj->pos.y - v->pos.y, obj->pos.z - v->pos.z, 0.0f};
    _VECTOR dir;
    Mat_GetDir(&dir, &obj->matrix);
    if (d.x * dir.x + d.y * dir.y + d.z * dir.z < 0.0f) {
        obj->flags |= 0x10;
    } else {
        obj->flags &= 0xffffffefu;
    }
}

// ------------------------------------------------------------------ partículas

// [decomp] 0x001cf478 Particles_Create
// Com um sistema de partículas da plataforma (psiCreateParticles diferente de -1): um objeto 0xc de partículas (mode
// 0x10, desenhado por psiDrawParticles; flags 0x21) na posição, com a esfera nela (raio 10, o w da posição por cima),
// em todas as câmeras, guardando o sistema em obj_tag::data.
obj_tag* Particles_Create(int32_t kind, _VECTOR* pos, plane_equ_tag* plane) {
    const uint32_t system = psiCreateParticles(uint32_t(kind), pos, plane);
    if (system == 0xffffffffu) return nullptr;
    obj_tag* obj = control_create_object(0, pos, nullptr, nullptr);
    if (obj == nullptr) return nullptr;
    ++g_scenery_stats.particles;
    obj->type = 0xc;
    obj->sphere.w = 10.0f;
    obj->flags |= 0x21;
    obj->mode |= 0x10;
    obj->sphere = *pos;
    build_LinkToRoom(obj, 0, glb_world);
    View_SetDrawInAllViews(obj);
    obj->data = nf::p32_ptr<void>(system);
    return obj;
}

// [decomp] 0x001cf528 Particles_Update
// Some quando o sistema acaba (psiParticleUpdate devolve 0).
void Particles_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    if (psiParticleUpdate(nf::p32_u(obj->data)) == 0) obj->life |= 1;
}

void Scenery_Describe(char* out, size_t size) {
    // O primeiro de cada tipo na lista de objetos (para pôr a câmera perto nas capturas: --at).
    const auto first = [](uint8_t type) -> const obj_tag* {
        for (const obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o->type == type) return o;
        }
        return nullptr;
    };
    uint32_t alive_leaves = 0;
    for (const obj_tag* o = control_first_object(); o != nullptr; o = o->next) alive_leaves += o->type == 0x2e ? 1 : 0;
    int n = std::snprintf(out, size, "enfeites ondulacoes %u aneis %u geradores %u folhas %u vivas %u arvores %u helices %u "
                                     "lados %u particulas %u bafos %u",
                          g_scenery_stats.ripples, g_scenery_stats.rings, g_scenery_stats.leaf_gens,
                          g_scenery_stats.leaves, alive_leaves, g_scenery_stats.trees, g_scenery_stats.rotors,
                          g_scenery_stats.one_sided, g_scenery_stats.particles, g_scenery_stats.breaths);
    const struct {
        const char* name;
        uint8_t type;
    } kinds[] = {{"ondulacao", 0x34}, {"gerador", 0x31}, {"arvore", 0x3f}, {"helice", 7}};
    for (const auto& k : kinds) {
        const obj_tag* o = first(k.type);
        if (o == nullptr || n < 0 || size_t(n) >= size) continue;
        n += std::snprintf(out + n, size - size_t(n), " %s %.2f,%.2f,%.2f", k.name, double(o->pos.x), double(o->pos.y),
                           double(o->pos.z));
    }
}
