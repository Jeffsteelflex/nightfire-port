// O modelo próprio dos objetos de cena do mundo (driving/scene_article.h), do MIPS do DRIVING.ELF (RSceneObj.obj e
// RDirectorQueue.obj). docs/driving.md seção 34.

#include "driving/scene_article.h"

#include <cstring>

#include "driving/fx/scene_fx.h"
#include "driving/light_manager.h"
#include "driving/real/vu0_math.h"
#include "driving/render/anim_engine.h"
#include "driving/render/vehicle.h"
#include "driving/world_vis.h"
#include "platform/ptr32.h"

namespace drv {

namespace {

SceneArticleEnv g_env;
SceneArticleStats g_stats;

// "<<unknown>>" (0x00386a30): o nome de um objeto sem 'Name'.
const char kUnknownName[] = "<<unknown>>";

// A instância única do ramo sem 'in' (no PS2, na pilha do UseArticle; o Handle::Create a copia na hora).
alignas(16) uint8_t g_single[0x40];

// Os ossos e os parâmetros do gProcAnimFunc 0xfe/0xef dos objetos de cena (o SetProcAnimState do RSceneObj é vazio:
// zerados aqui, ver o cabeçalho).
const Bone kNoBone = {};
const float kNoParams[12] = {};

uint32_t r32(uint32_t p, uint32_t off) {
    uint32_t v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, 4);
    return v;
}
void w32(uint32_t p, uint32_t off, uint32_t v) { std::memcpy(nf::p32_ptr<uint8_t>(p) + off, &v, 4); }
uint8_t r8(uint32_t p, uint32_t off) { return nf::p32_ptr<const uint8_t>(p)[off]; }
uint64_t r64(uint32_t p, uint32_t off) {
    uint64_t v;
    std::memcpy(&v, nf::p32_ptr<const uint8_t>(p) + off, 8);
    return v;
}

}  // namespace

SceneArticleEnv& scene_article_env() { return g_env; }
SceneArticleStats& scene_article_stats() { return g_stats; }

// [driving] 0x001c6fb8 (o dado de um UGroup pela etiqueta e a variante)
UData* RSceneObj_LocateVariant(const UGroup* g, uint32_t tag, uint32_t variant) {
    const uint32_t first = variant == 0xffffffffu ? tag : ((tag & 0xffff0000u) | variant);
    UData* d = UGroup_DataLocateTag(g, first);
    if (d == UGroup_DataEnd(g) && variant != 0) d = UGroup_DataLocateTag(g, tag & 0xffff0000u);
    return d == UGroup_DataEnd(g) ? nullptr : d;
}

// [driving] 0x001c8070 RSceneObj::Close
void RSceneObj_Close(RSceneObj& o, bool flag) {
    // [driving] 0x001ca178 RSceneObj::KillEffects (0x00232dc8: GFXGallery::CANVAS_PurgeSceneObjEffects(this))
    if (g_env.purge) g_env.purge(nf::p32_u(&o));
    if (o.article != 0) AnimHandle_Destroy(o.article);  // RAnimEngine::Handle::~Handle(3)
    o.article = 0;
    if (flag) {
        // Divergência: o URefCounter<RCARPFile> do +0x10 (o CARP do objeto) não existe no port; o WWorld::Open nunca
        // chega aqui (UseInstanceList chama o Close(false)).
        ++g_stats.close_true;
        o.carp = 0;
    }
}

// [driving] 0x001ca048 RSceneObj::InitEffects
void RSceneObj_InitEffects(RSceneObj& o, bool flag) {
    const uint32_t h = o.article;
    const AnimHandleData& hd = *nf::p32_ptr<const AnimHandleData>(h);
    const uint32_t n = hd.n_effects;
    const uint32_t bits = hd.effect_bits;
    for (uint32_t k = 0; k < n; ++k) {
        const uint32_t effect = hd.effects + k * 0x40;
        // dsrlv: o deslocamento é o índice módulo 64.
        if ((r64(h, 0x28) >> (k & 63)) & 1) {
            // Um efeito de dano sem zonas: a zona dele pela caixa do 'pD' (CalcDamageZone e as vizinhas, cujo resultado
            // o PS2 não usa: o SetEffectBits recebe 0).
            uint16_t zones;
            std::memcpy(&zones, nf::p32_ptr<const uint8_t>(bits) + k * 2, 2);
            if (zones != 0) continue;
            float pos[3];
            std::memcpy(pos, nf::p32_ptr<const uint8_t>(effect), 12);
            const uint32_t zone = BaseDesc_CalcDamageZone(o.unknown0c, pos);
            (void)BaseDesc_ZoneNeighbours(zone);
            AnimHandle_SetEffectBits(h, k, 0);
            continue;
        }
        const uint32_t f = r32(effect, 0x10);
        uint32_t on = (f >> 4) & 1;
        if (flag) {
            if (f & 4) on |= 1;
            on = on != 0;
        }
        if (on == 0) continue;
        ++g_stats.effects_on;
        if (g_env.toggle_effect) g_env.toggle_effect(nf::p32_u(&o), k, effect);
        else RSceneObj_ToggleEffect(-1.0f, nf::p32_u(&o), true, k, effect);
    }
}

// [driving] 0x001c7940 RSceneObj::UseInstanceList
void RSceneObj_UseInstanceList(RSceneObj& o, uint32_t instances, uint32_t n, uint32_t states, bool flag, uint32_t name) {
    RSceneObj_Close(o, false);
    o.unknown30[2] = name != 0 ? name : nf::p32_u(kUnknownName);  // +0x38
    o.unknown1c = 0;
    o.unknown1e = 0;
    o.unknown20 = 0;
    // __builtin_delete(+0x34) e o +0x34 zerado (o port nunca o grava: um objeto de cena do mundo não tem o que apagar).
    if (o.unknown30[1] != 0) ++g_stats.guards;
    o.unknown30[1] = 0;
    const uint32_t art = r32(instances, 0x1c);
    if (art != 0) {
        o.unknown0c = r32(art, 0);
        if (r8(art, 0x1e) == 1) {
            o.flags = (o.flags & 0xf7ffffffu) | 0x08000000u;  // um artigo de carro: desenhado por último
            ++g_stats.draw_last;
        }
    } else {
        o.unknown0c = 0;
    }
    // RAnimEngine::Handle::Create(n, RTimeData::fTime, instâncias, estados, this)
    const uint32_t h = AnimHandle_Create(n, rtime_data().time, instances, states, nf::p32_u(&o));
    o.article = h;
    ++g_stats.handles;
    g_stats.instances += n;
    o.flags |= 0x02000000u;
    const AnimHandleData& hd = *nf::p32_ptr<const AnimHandleData>(h);
    // O bit 25 fica só se toda cópia tem uma das bandeiras 0x41 (o laço para no primeiro que o apaga).
    for (uint32_t i = 0; i < hd.n_instances && (o.flags & 0x02000000u); ++i)
        if ((r32(h + 0x40 + i * 0x40, 0xc) & 0x41) == 0) o.flags &= 0xfdffffffu;
    if ((r32(o.instance, 0x3c) & 0x3ff) == 0) {
        // Divergência: ComputeBoundingRadius (0x001c98a0) e CARP::Instance::SetDimensions (0x002dac58) não portados
        // (nenhuma instância de objeto de cena de Paris sem medidas): contado.
        ++g_stats.no_radius;
    }
    RSceneObj_InitEffects(o, flag);
    // A entrada +0x94 da tabela virtual (0x001c7f40, vazia no RSceneObj).
    const uint32_t far = (r8(o.instance, 0xc) >> 3) & 1;
    o.flags = (o.flags & 0xfbffffffu) | (far << 26);
    if (o.flags & 0x04000000u) {
        ++g_stats.root_nodes;
        if (g_env.set_root_node) {
            g_env.set_root_node(o);
        } else {
            // [driving] 0x001c8cf8 RSceneObj::SetMapNode(*(fgWorld +0x1c)): o nó da raiz do 'Wmap'.
            const uint32_t wmap = fgWorldMap();
            if (wmap != 0) RSceneObj_SetMapNode(o, nf::p32_ptr<MapNode>(r32(wmap, 0)));
            else ++g_stats.guards;
        }
    }
}

// [driving] 0x001c7658 RSceneObj::UseArticle
bool RSceneObj_UseArticle(RSceneObj& o, const UGroup* group, uint32_t variant) {
    ++g_stats.articles;
    UData* in = RSceneObj_LocateVariant(group, 0x696e2020u, variant);  // 'in  '
    UData* nd = UGroup_DataLocateTag(group, 0x4e616d65u);               // 'Name'
    const uint32_t name = nd != UGroup_DataEnd(group) ? nf::p32_u(UData_Data(nd)) : 0u;
    uint32_t states = 0, list = 0, n = 0;
    if (in != nullptr) {
        list = nf::p32_u(UData_Data(in));
        n = in->count;
        if (UData* ps = RSceneObj_LocateVariant(group, 0x70732020u, variant))  // 'ps  '
            states = nf::p32_u(UData_Data(ps));
        o.flags = (o.flags & 0xffff00ffu) | ((variant & 0xff) << 8);  // +0x19
        ++g_stats.lists;
    } else {
        // Uma só instância: a do mundo com a rotação e a posição da identidade (kIdentityMatrix4 0x355bb0: os x, y, z
        // das quatro linhas; os w ficam), o gProcAnimFunc 0xff e as bandeiras 0x11 apagadas.
        std::memcpy(g_single, nf::p32_ptr<const uint8_t>(o.instance), 0x40);
        g_single[0xd] = 0xff;
        g_single[0xc] &= 0xee;
        static const float kIdentity[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
        for (int row = 0; row < 4; ++row) std::memcpy(g_single + row * 0x10, kIdentity[row], 12);
        list = nf::p32_u(g_single);
        n = 1;
        uint32_t art;
        std::memcpy(&art, g_single + 0x1c, 4);
        if (art == 0) {
            UData* as = UGroup_DataLocateFirst(group, 0x61732020u, 0, 0xffff);  // 'as  '
            if (as == UGroup_DataEnd(group)) {
                n = 0;
            } else {
                art = nf::p32_u(UData_Data(as));
                std::memcpy(g_single + 0x1c, &art, 4);
            }
        }
        ++g_stats.single;
    }
    if (n == 0) {
        ++g_stats.without;
        return false;
    }
    RSceneObj_UseInstanceList(o, list, n, states, false, name);
    return true;
}

// [driving] 0x001c85f8 RSceneObj::Render, 0x001ad6b8 DrawInstanceList e 0x001ad498 DrawInstance (as matrizes)
void RSceneObj_Render(const RSceneObj& o, std::vector<SceneObjDraw>& out) {
    const uint32_t h = o.article;
    if (h == 0) return;
    // RSceneObj::GetTransform (0x001c9190): a instância de origem com o w das linhas 0, 0, 0, 1.
    float transform[4][4];
    std::memcpy(transform, nf::p32_ptr<const uint8_t>(o.instance), 64);
    transform[0][3] = transform[1][3] = transform[2][3] = 0.0f;
    transform[3][3] = 1.0f;
    const AnimHandleData& hd = *nf::p32_ptr<const AnimHandleData>(h);
    ProcAnimContext ctx;
    ctx.xforms = &kNoBone;
    ctx.xform_count = 1;
    ctx.params = kNoParams;
    ctx.param_count = 12;
    ctx.states = hd.damage ? nf::p32_ptr<const uint8_t>(hd.damage) : nullptr;  // o Handle +8 (o a3 do DrawInstance)
    for (uint32_t i = 0; i < hd.n_instances; ++i) {
        const uint32_t copy = h + 0x40 + i * 0x40;
        if (r8(copy, 0xc) & 1) {
            ++g_stats.hidden;
            continue;
        }
        const uint32_t art = r32(copy, 0x1c);
        if (art == 0) {
            ++g_stats.guards;  // o PS2 leria a passada (+0x1d) do endereço 0
            continue;
        }
        const uint8_t fn = r8(copy, 0xd);
        if (fn == 0xfe || fn == 0xef) ++g_stats.proc_other;
        uint32_t raw[16];
        std::memcpy(raw, nf::p32_ptr<const uint8_t>(copy), 64);
        SceneObjDraw d;
        if (!GetInstanceMatrix(raw, ctx, d.world)) continue;
        VU0_MATRIX4_mult(d.world, transform, d.world);  // 0x002cbb88
        d.copy = copy;
        d.article = art;
        d.pass = r8(art, 0x1d);
        out.push_back(d);
        ++g_stats.draws;
    }
}

}  // namespace drv
