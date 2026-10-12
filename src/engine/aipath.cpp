// Carga da rede de navegação da IA (AIPath_*, AIBounds_*) e os objetos de IA posicionados na fase
// (nós de cobertura, pontos de IA, volumes e os nós de porta e de chute). Ver ainetwork.h.
//
// Divergências do port (ver ainetwork.h): AIPath_Parse pula caminhos e limites além dos 50 de
// AINetwork; os nós de cobertura e pontos de IA além de 256, ou com a tabela ainda não apontada
// (Drone_PreLoad_Init não rodou), são pulados; AIBounds_Prepare não pendura um segmento numa sala
// nula (o original escreveria em 0x34); o índice da tabela de Drone_CoverLowNode além das 8
// entradas lê 0 (o original lê a pilha). Tudo contado em g_ainetwork_guards.

#include "engine/ainetwork.h"

#include <cmath>
#include <cstring>

#include "engine/collide.h"
#include "engine/door.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "engine/npc.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"

AINETWORK_tag AINetwork;
CoverNode_tag CoverNodes[kMaxCoverNodes];
AIPoint_tag AIPoints[kMaxAIPoints];
LLISTINFO_tag AIBoxList;
NPCGLOBALS_tag NPCGlobals;
AINetworkGuards g_ainetwork_guards;

namespace {

uint32_t word(const uint8_t* p, int k) {
    uint32_t v;
    std::memcpy(&v, p + 4 * k, 4);
    return v;
}

int32_t iTimeToCalc_255 = 0;

}  // namespace

// [decomp] 0x001d2ea8 AIPath_Init
// Esvazia a rede (e zera max_slope, que Drone_PostLoad_Init põe de volta).
void AIPath_Init() {
    AINetwork.max_links = 0;
    AINetwork.path_count = 0;
    AINetwork.bounds_count = 0;
    AINetwork.max_slope = 0.0f;
    AINetwork.max_nodes = 0;
}

// [decomp] 0x001d2598 AIPath_Parse
// Bloco 0x05: caminhos e limites (formato em docs/formats.md). Os nós e ligações ficam no arquivo,
// com índice, caminho e campos de estado reescritos; os NODE_tag/BNODE_tag são alocados aqui.
void AIPath_Parse(uint8_t* block) {
    AINetwork.version = word(block, 1);
    if (AINetwork.version != 8) return;
    const uint32_t count = word(block, 2);
    uint8_t* p = block + 0xc;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t flags = word(p, 0x20);
        const uint32_t node_count = word(p, 0x29);
        uint8_t* nodes_in_file = p + word(p, 0x2a) + 0xac;
        if ((flags & 1) == 0) {
            if (AINetwork.path_count >= uint32_t(kMaxAIPaths)) {
                // Port: o original escreveria além de AINetwork.paths. Pula o caminho.
                ++g_ainetwork_guards.paths_full;
                uint8_t* q = nodes_in_file + node_count * 0x40;
                p = q + 4 + word(q, 0) * 0x10;
                continue;
            }
            const uint32_t index = AINetwork.path_count;
            AIPath_tag& path = AINetwork.paths[index];
            path.index = int16_t(index);
            path.header = p;
            path.flags = flags;
            for (int k = 0; k < 8; ++k) path.unknown_0c[k] = word(p, 0x21 + k);
            path.found = 0;
            path.node_count = node_count;
            path.nodes = node_count == 0 ? nullptr : static_cast<NODE_tag*>(Mem_Malloc(node_count * 0x30, 0x1804, 0));
            if (AINetwork.max_nodes < node_count) AINetwork.max_nodes = node_count;
            path.map_nodes = reinterpret_cast<MAPNODE_tag*>(nodes_in_file);
            for (uint32_t k = 0; k < node_count; ++k) {
                MAPNODE_tag& n = path.map_nodes[k];
                n.index = int16_t(k);
                n.path = int16_t(index);
                if (n.flags == 0x2468abce) n.flags = 0;
                n.cel = nullptr;
                n.pos.y += 0.4f;
                if (path.nodes != nullptr) {
                    path.nodes[k].door = nullptr;
                    path.nodes[k].node = &n;
                }
            }
            uint8_t* q = nodes_in_file + node_count * 0x40;
            path.link_count = word(q, 0);
            path.links = reinterpret_cast<LINK_tag*>(q + 4);
            if (AINetwork.max_links < path.link_count) AINetwork.max_links = path.link_count;
            for (uint32_t k = 0; k < path.link_count; ++k) {
                LINK_tag& l = path.links[k];
                l.index = int16_t(word(reinterpret_cast<uint8_t*>(&l), 0));
                l.path = int16_t(index);
                l.flags = 0;
                l.used = 0;
                l.length = 0.0f;
            }
            path.node_links = nullptr;
            p = q + 4 + path.link_count * 0x10;
            ++AINetwork.path_count;
        } else {
            if (AINetwork.bounds_count >= uint32_t(kMaxAIBounds)) {
                ++g_ainetwork_guards.bounds_full;
                uint8_t* q = nodes_in_file + node_count * 0x20;
                p = q + 4 + word(q, 0) * 0x14;
                continue;
            }
            const uint32_t index = AINetwork.bounds_count;
            AIBounds_tag& b = AINetwork.bounds[index];
            b.index = int16_t(index);
            b.header = p;
            b.flags = flags;
            for (int k = 0; k < 8; ++k) b.unknown_0c[k] = word(p, 0x21 + k);
            b.node_count = node_count;
            b.nodes = node_count == 0 ? nullptr : static_cast<BNODE_tag*>(Mem_Malloc(node_count * 8, 0x1804, 0));
            b.map_nodes = reinterpret_cast<BMAPNODE_tag*>(nodes_in_file);
            for (uint32_t k = 0; k < node_count; ++k) {
                BMAPNODE_tag& n = b.map_nodes[k];
                n.index = int16_t(k);
                n.bounds = int16_t(index);
                n.cel = nullptr;
                n.pos.y += 0.4f;
                n.height = 0.0f;
                if (b.nodes != nullptr) b.nodes[k].node = &n;
            }
            uint8_t* q = nodes_in_file + node_count * 0x20;
            b.link_count = word(q, 0);
            b.links = reinterpret_cast<BLINK_tag*>(q + 4);
            for (uint32_t k = 0; k < b.link_count; ++k) {
                BLINK_tag& l = b.links[k];
                l.index = int16_t(word(reinterpret_cast<uint8_t*>(&l), 0));
                l.bounds = int16_t(index);
                l.flags = 0;
                l.next_a = nullptr;
                l.next_b = nullptr;
            }
            p = q + 4 + b.link_count * 0x14;
            ++AINetwork.bounds_count;
        }
    }
}

// [decomp] 0x001d2ec8 AIPath_Link2Cel
// Pendura o nó na lista da sala que o contém (nada se ele estiver fora de todas).
void AIPath_Link2Cel(NODE_tag* node, char*) {
    cel_tag* cel = build_FindCel(&node->node->pos, glb_world);
    if (cel == nullptr) return;
    node->next = cel->ai_nodes;
    node->node->cel = cel;
    cel->ai_nodes = node;
}

// [decomp] 0x001d2f18 AIBounds_Link2Cel
// Como AIPath_Link2Cel, e mede a altura do limite: um raio de 256 m para cima até o teto da sala
// (256 sem teto), no mínimo 2.
void AIBounds_Link2Cel(BNODE_tag* node, char*) {
    cel_tag* cel = build_FindCel(&node->node->pos, glb_world);
    if (cel == nullptr) return;
    node->next = cel->ai_bnodes;
    node->node->cel = cel;
    cel->ai_bnodes = node;
    HITDATA_tag* hits = nullptr;
    const _VECTOR& p = node->node->pos;
    const _VECTOR up{p.x, p.y + 256.0f, p.z, 0.0f};  // w: lixo da pilha no original
    Collide_RayIntersect(&p, &up, cel, nullptr, nullptr, &hits, 1, 0x90, 0);
    float height = hits != nullptr ? hits->dist : 256.0f;
    node->node->height = height;
    node->node->height = std::fmax(node->node->height, 2.0f);
    Collide_FreeHitList(&hits);
}

// [decomp] 0x001d2cc0 AIPath_Prepare
// Comprimento de cada ligação e a lista de ligações de cada nó (node_links, em sequência).
void AIPath_Prepare(AIPath_tag* path) {
    for (int32_t i = 0; i < int32_t(path->link_count); ++i) {
        LINK_tag& l = path->links[i];
        const _VECTOR& a = path->nodes[l.a].node->pos;
        const _VECTOR& b = path->nodes[l.b].node->pos;
        const _VECTOR d{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
        l.length = Vec_Magnitude(&d);
    }
    int32_t total = 0;
    for (int32_t n = 0; n < int32_t(path->node_count); ++n) {
        for (int32_t i = 0; i < int32_t(path->link_count); ++i) {
            if (path->links[i].a == uint32_t(n) || path->links[i].b == uint32_t(n)) ++total;
        }
    }
    if (total == 0) {
        path->node_links = nullptr;
        return;
    }
    path->node_links = static_cast<uint16_t*>(Mem_Malloc(uint32_t(total) * 2, 0x1804, 0));
    int32_t k = 0;
    for (int32_t n = 0; n < int32_t(path->node_count); ++n) {
        int32_t c = 0;
        for (int32_t i = 0; i < int32_t(path->link_count); ++i) {
            if (path->links[i].a == uint32_t(n) || path->links[i].b == uint32_t(n)) {
                path->node_links[k + c] = uint16_t(i);
                ++c;
            }
        }
        path->nodes[n].link_count = uint8_t(c);
        path->nodes[n].first_link = uint16_t(k);
        k += c;
    }
}

// [decomp] 0x001d3008 AIBounds_Prepare
// Pendura cada segmento na lista das salas das duas pontas (uma vez só se for a mesma sala).
void AIBounds_Prepare(AIBounds_tag* bounds) {
    for (uint32_t i = 0; i < bounds->link_count; ++i) {
        BLINK_tag* l = &bounds->links[i];
        BMAPNODE_tag* a = bounds->nodes[l->a].node;
        BMAPNODE_tag* b = bounds->nodes[l->b].node;
        if (a->cel == nullptr || b->cel == nullptr) {
            // Port: o original leria e escreveria em 0x34 (sala nula).
            ++g_ainetwork_guards.bounds_no_cel;
            if (a->cel == nullptr && b->cel == nullptr) continue;
        }
        if (a->cel != nullptr) {
            l->next_a = a->cel->ai_blinks;
            a->cel->ai_blinks = l;
        }
        if (a->cel == b->cel) {
            l->next_b = l->next_a;
        } else if (b->cel != nullptr) {
            l->next_b = b->cel->ai_blinks;
            b->cel->ai_blinks = l;
        }
    }
}

// [decomp] 0x001d2af8 AIPath_BindNodes
// Depois da carga: nós nas salas, ligações dos nós e segmentos de limite nas salas.
void AIPath_BindNodes() {
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.node_count; ++k) {
            AIPath_Link2Cel(&path.nodes[k], reinterpret_cast<char*>(path.header));
        }
        AIPath_Prepare(&path);
    }
    for (uint32_t i = 0; i < AINetwork.bounds_count; ++i) {
        AIBounds_tag& b = AINetwork.bounds[i];
        for (uint32_t k = 0; k < b.node_count; ++k) {
            AIBounds_Link2Cel(&b.nodes[k], reinterpret_cast<char*>(b.header));
        }
        AIBounds_Prepare(&b);
    }
}

// ---- objetos de IA posicionados ----

namespace {

uint8_t lowbyte(const level_tag* level, int k) { return uint8_t(level->params[k]); }

// Port: a tabela ainda não foi apontada (Drone_PreLoad_Init) ou está cheia.
CoverNode_tag* next_cover() {
    if (NPCGlobals.cover_nodes == nullptr || NPCGlobals.cover_count >= uint32_t(kMaxCoverNodes)) {
        ++g_ainetwork_guards.covers_full;
        return nullptr;
    }
    return &NPCGlobals.cover_nodes[NPCGlobals.cover_count];
}

}  // namespace

// [decomp] 0x00136ff0 Drone_CoverCornerNode
// Cobertura de quina. Parâmetros: 0, 1 e 2 escolhem lados (1, 2 ou ambos), 3 a abertura em graus
// (45 se zero), 4 e 5 os canais que a ligam e desligam, 6 e 7 dois bytes (8 se zero).
void Drone_CoverCornerNode(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    if (Drone_bDisableSystem != 0) return;
    CoverNode_tag* node = next_cover();
    if (node == nullptr) return;
    std::memset(node, 0, sizeof(*node));
    node->type = 0;
    node->flags = 0;
    node->cel = build_FindCel(pos, glb_world);
    node->user = nullptr;
    node->pos = *pos;
    node->rot = *rot;
    const uint32_t p0 = level->params[0];
    node->flags |= p0 == 1 ? 0x8u : p0 == 2 ? 0x4u : 0xcu;
    const uint32_t p1 = level->params[1];
    node->flags |= p1 == 1 ? 0x20u : p1 == 2 ? 0x40u : 0x60u;
    const uint32_t p2 = level->params[2];
    node->flags |= p2 == 1 ? 0x80u : p2 == 2 ? 0x100u : 0x180u;
    if (level->params[3] == 0) {
        node->angle = 0.7853982f;
    } else {
        node->angle = float(level->params[3]) * 0.017453294f;
    }
    node->channel_on = lowbyte(level, 4);
    node->channel_off = lowbyte(level, 5);
    node->corner_a = level->params[6] != 0 ? lowbyte(level, 6) : 8;
    node->corner_b = level->params[7] != 0 ? lowbyte(level, 7) : 8;
    AINetwork_InitEmitter(nullptr, pos, node->cel, &node->emitter, nullptr, 1);
    ++NPCGlobals.cover_count;
}

// [decomp] 0x00137340 Drone_CoverLowNode
// Cobertura baixa. Com o parâmetro 0 = 2, os parâmetros 1 e 2 escolhem as animações numa tabela
// (0x002f4540) e o 3 = 1 liga o bit 2; senão as animações são fixas (0x11 e 5 ou 10).
void Drone_CoverLowNode(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    static const uint16_t kAnims[8] = {0x11, 5, 10, 7, 0x1d, 0, 0, 0};
    const auto anim = [&](uint32_t k) -> uint8_t {
        if (k < 8) return uint8_t(kAnims[k]);
        ++g_ainetwork_guards.cover_table;  // port: o original lê a pilha
        return 0;
    };
    if (Drone_bDisableSystem != 0) return;
    CoverNode_tag* node = next_cover();
    if (node == nullptr) return;
    std::memset(node, 0, sizeof(*node));
    node->type = 1;
    if (level->params[0] == 2) {
        node->flags = 1;
        node->low_a = anim(level->params[1]);
        node->low_b = anim(level->params[2]);
        if (level->params[3] == 1) node->flags |= 2;
    } else {
        node->low_a = 0x11;
        node->low_b = level->params[0] == 1 ? 5 : 10;
    }
    node->cel = build_FindCel(pos, glb_world);
    node->user = nullptr;
    node->pos = *pos;
    node->rot = *rot;
    node->channel_on = lowbyte(level, 4);
    node->channel_off = lowbyte(level, 5);
    AINetwork_InitEmitter(nullptr, pos, node->cel, &node->emitter, nullptr, 1);
    ++NPCGlobals.cover_count;
}

// [decomp] 0x00137550 Drone_AIPoint
// Ponto de IA: tipo (parâmetro 0), raio (1) e um valor (2), com um emissor na rede.
void Drone_AIPoint(_VECTOR* pos, _VECTOR* rot, level_tag* level) {
    if (Drone_bDisableSystem != 0) return;
    if (NPCGlobals.ai_points == nullptr || NPCGlobals.ai_point_count >= uint32_t(kMaxAIPoints)) {
        ++g_ainetwork_guards.points_full;  // port: tabela não apontada ou cheia
        return;
    }
    AIPoint_tag* pt = &NPCGlobals.ai_points[NPCGlobals.ai_point_count];
    pt->active = 1;
    pt->kind = uint16_t(level->params[0]);
    pt->unknown_08 = 0;
    pt->unknown_10 = 0;
    pt->radius = float(level->params[1]);
    pt->pos.cel = build_FindCel(pos, glb_world);
    pt->pos.pos = *pos;
    pt->rot = *rot;
    pt->param = level->params[2];
    AINetwork_InitEmitter(nullptr, pos, pt->pos.cel, &pt->emitter, nullptr, 1);
    ++NPCGlobals.ai_point_count;
}

// [decomp] 0x001376c0 Drone_AIVolume_Create
// Volume de IA: um objeto tipo 0x3e invisível (flags 0x30) cuja caixa é a do modelo vezes a escala
// do posicionamento, na lista AIBoxList. Em 07000005 e com o parâmetro 1 = 4, a rotação z troca de
// sinal e o centro sobe 1,5 x a escala y.
obj_tag* Drone_AIVolume_Create(_VECTOR* pos, _VECTOR* rot, _VECTOR* scale, level_tag* level, celglist_tag* glist) {
    if (glist == nullptr) return nullptr;
    if (NPCGlobals.cover_nodes == nullptr) {
        // Port: sem Drone_PreLoad_Init, AIBoxList não foi reiniciada para esta fase.
        ++g_ainetwork_guards.volumes_skipped;
        return nullptr;
    }
    _VECTOR p = *pos;
    _VECTOR r = *rot;
    if (GameState.level_file == 0x7000005 || level->params[1] == 4) {
        r.z = -r.z;
        p.y += scale->y * 1.5f;
    }
    obj_tag* obj = control_create_object(0x120, &p, &r, nullptr);
    if (obj == nullptr) return nullptr;
    obj->glist = glist;
    auto* vol = static_cast<AIVolume_tag*>(obj->data);
    obj->type = 0x3e;
    obj->flags = glist->flags | 0x30;
    vol->obj = obj;
    vol->value = float(level->params[0]) * 0.01f;
    vol->half = {glist->box_max.x - glist->box_min.x, glist->box_max.y - glist->box_min.y,
                 glist->box_max.z - glist->box_min.z, glist->box_max.w - glist->box_min.w};
    vol->half.x = std::fabs(vol->half.x * 0.5f) * scale->x;
    vol->half.y = std::fabs(vol->half.y * 0.5f) * scale->y;
    vol->half.z = std::fabs(vol->half.z * 0.5f) * scale->z;
    vol->param2 = level->params[2];
    vol->param3 = lowbyte(level, 3);
    vol->param4 = uint16_t(level->params[4]);
    switch (level->params[1]) {
        case 0: vol->kind = 0; break;
        case 1: vol->kind = 2; break;
        case 2: vol->kind = 1; break;
        case 4: vol->kind = 3; break;
        default: break;
    }
    LList_Add(&AIBoxList, &vol->node);
    return obj;
}

// [decomp] 0x0013a1d0 Drone_AIVolume_Delete
void Drone_AIVolume_Delete(obj_tag* obj) {
    LList_Remove(&AIBoxList, static_cast<LLNODE_tag*>(obj->data));
}

// [decomp] 0x0013a290 Drone_PointInAnyAIBox
// O ponto está num volume do tipo pedido? Em 0700001b, sem volume nenhum, vale qualquer lugar.
uint32_t Drone_PointInAnyAIBox(_VECTOR* p, uint16_t kind) {
    auto* node = reinterpret_cast<AIVolume_tag*>(AIBoxList.first);
    if (node == nullptr) return GameState.level_file == 0x700001b ? 1 : 0;
    for (; node != nullptr; node = reinterpret_cast<AIVolume_tag*>(node->node.next)) {
        if (node->kind == kind && Intersect_PointOOBox(&node->obj->matrix, &node->half, p, 0.0f) != 0) return 1;
    }
    return 0;
}

// [decomp] 0x001386d8 Drone_IsCoverNodeUsable
// A cobertura protege do oponente do lado `side`? Escreve em node->usable[side] o motivo (0x80 perto
// demais, 0x200 fora do ângulo, 0x400 o oponente vê o ponto; 2/0x12 e 4/0x24 o lado da quina,
// 0x48 a baixa) e devolve se fez um teste de visão (NPCGlobals.vision_checks mudou), o que limita
// Drone_ProcessCoverNodes a um teste caro por lado.
bool Drone_IsCoverNodeUsable(uint32_t side, CoverNode_tag* node) {
    constexpr float kHalfPi = 1.5707964f;
    constexpr float kPi = 3.1415927f;
    if (node == nullptr || side >= 8) return false;
    NPCOpponent_tag& opp = NPCGlobals.opponents[side];
    uint32_t& usable = node->usable[side];
    usable = 0;
    if (opp.obj == nullptr) return false;
    if (node->channel_on != 0 && switch_channels[node->channel_on] == 0) return false;
    if (node->channel_off != 0 && switch_channels[node->channel_off] != 0) return false;
    obj_tag* user = node->user;
    if ((opp.flags & 3) == 0 && (user == nullptr || user->type != 2)) return false;
    if (user != nullptr && user->type == 2) {
        if (user->data == nullptr) return false;
        if (static_cast<uint8_t*>(user->data)[0x174] != side) return false;
    }
    float wide, narrow;  // f20, f21
    if (node->type == 1) {
        if (GameState.level_file == 0x700000d) {
            wide = 1.3962635f;
            narrow = 1.308997f;
        } else {
            wide = 0.7853982f;
            narrow = 0.5235988f;
        }
        if ((node->flags & 0x10) != 0) {
            narrow += 0.08726647f;
            wide += 0.08726647f;
        }
    } else if ((node->flags & 0x10) == 0) {
        wide = node->angle + 0.08726647f;
        narrow = node->angle * 0.66f;
    } else {
        wide = node->angle + 0.17453294f;
        narrow = node->angle * 0.77f;
    }
    const _VECTOR d{opp.pos.x - node->pos.x, opp.pos.y - node->pos.y, opp.pos.z - node->pos.z,
                    opp.pos.w - node->pos.w};
    if (Vec_Magnitude(&d) < 4.0f) {
        usable |= 0x80;
        return false;
    }
    const float a = Vec_AngleDifference(node->rot.y, ATAN2_APPROX(d.x, d.z));
    if (node->type == 0) {
        if ((node->flags & 0x100) != 0 && kHalfPi - narrow < a && a < wide + kHalfPi) {
            const uint32_t old = usable;
            usable = old | 2;
            if (a < kHalfPi) usable = old | 0x12;
        } else {
            bool ok = false;
            if ((node->flags & 0x80) != 0) {
                if (0.0f < a && kPi - wide < a) ok = true;
                else if (a < 0.0f && a < narrow - kPi) ok = true;
            }
            if (!ok) {
                usable |= 0x200;
                return false;
            }
            const uint32_t old = usable;
            usable = old | 4;
            if (-kPi < a) usable = old | 0x24;
        }
    } else {
        if (wide < std::fabs(a)) {
            usable |= 0x200;
            return false;
        }
        usable |= 0x48;
    }
    _VECTOR eye = node->pos;
    eye.y += 1.5f;
    if (GameState.level_file == 0x7000011) eye.y -= 0.35f;
    const uint16_t before = NPCGlobals.vision_checks;
    if (DroneVision_CanSeePosition(opp.obj, &eye, node->cel, node->user) == 0) {
        usable = (usable & 0xffffff8f) | 0x400;
    }
    return before != NPCGlobals.vision_checks;
}

// [decomp] 0x00138e78 Drone_ProcessCoverNodes
// A cada 5 chamadas (o contador desce 2 de 10), avalia as coberturas para cada um dos 8 lados,
// continuando de onde parou, até um teste de visão por lado.
void Drone_ProcessCoverNodes() {
    iTimeToCalc_255 -= 2;
    if (iTimeToCalc_255 >= 1) return;
    iTimeToCalc_255 = 10;
    for (uint32_t side = 0; side < 8; ++side) {
        if (NPCGlobals.cover_count == 0) continue;
        uint32_t tries = 0;
        bool checked;
        do {
            checked = Drone_IsCoverNodeUsable(side, &NPCGlobals.cover_nodes[NPCGlobals.cover_next]);
            ++NPCGlobals.cover_next;
            if (NPCGlobals.cover_count <= NPCGlobals.cover_next) NPCGlobals.cover_next = 0;
            ++tries;
        } while (!checked && tries < NPCGlobals.cover_count);
    }
}

namespace {

// Ligações que saem do nó (como ponta a) para um nó com `flag`: as que cruzam um limite ganham
// `bound_flag` no limite e `link_flag` na ligação.
void mark_links(AIPath_tag& path, NODE_tag& node, uint32_t flag, uint32_t bound_flag, uint16_t link_flag) {
    for (int32_t s = 0; s < int32_t(node.link_count); ++s) {
        LINK_tag& l = path.links[path.node_links[node.first_link + s]];
        if (&path.nodes[l.a] != &node) continue;
        MAPNODE_tag* b = path.nodes[l.b].node;
        if ((b->flags & flag) == 0) continue;
        if (AINetwork_ModIntersectedLinkFlags_Bounds(&node.node->pos, &b->pos, bound_flag, 0, nullptr, 0) != 0) {
            l.flags |= link_flag;
        }
    }
}

}  // namespace

// [decomp] 0x00139a70 NDrone_InitDoorNodes
// Cada nó de porta fica com a porta (objeto 0x1c) mais próxima (a primeira achada, trocada pelas mais
// perto); sem porta, perde o bit. As ligações de porta a porta que cruzam um limite marcam o limite
// (kBoundDoor) e a ligação (kLinkDoor). Zera NODE_tag::door de todos os nós.
void NDrone_InitDoorNodes() {
    float best = 4.0f;  // não volta a 4 entre nós, como no original (a primeira porta sempre entra)
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.node_count; ++k) {
            NODE_tag& node = path.nodes[k];
            node.door = nullptr;
            if ((node.node->flags & kNodeDoor) == 0) continue;
            obj_tag* door = nullptr;
            for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
                if (o->type != 0x1c) continue;
                const float dx = o->pos.x - node.node->pos.x;
                const float dy = o->pos.y - node.node->pos.y;
                const float dz = o->pos.z - node.node->pos.z;
                const float d = dx * dx + dy * dy + dz * dz;
                if (d < best || door == nullptr) {
                    best = d;
                    door = o;
                }
            }
            node.door = door;
            if (door == nullptr) node.node->flags &= ~kNodeDoor;
            mark_links(path, node, kNodeDoor, kBoundDoor, kLinkDoor);
        }
    }
}

// [decomp] 0x00139ce0 NDrone_InitKickNodes
// Como NDrone_InitDoorNodes para os nós de chute: o objeto é um 0x1b com o script 060005bb (o mais
// perto); sem objeto, o nó fica como está.
void NDrone_InitKickNodes() {
    float best = 0.0f;
    for (uint32_t i = 0; i < AINetwork.path_count; ++i) {
        AIPath_tag& path = AINetwork.paths[i];
        for (uint32_t k = 0; k < path.node_count; ++k) {
            NODE_tag& node = path.nodes[k];
            if ((node.node->flags & kNodeKick) == 0) continue;
            obj_tag* found = nullptr;
            for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
                if (o->type != 0x1b) continue;
                auto* info = static_cast<uint8_t*>(SP_getScriptInfo(o));
                if (info == nullptr) continue;
                uint32_t script;
                std::memcpy(&script, info + 0x98, 4);
                if (script != 0x60005bb) continue;
                const float dx = o->pos.x - node.node->pos.x;
                const float dy = o->pos.y - node.node->pos.y;
                const float dz = o->pos.z - node.node->pos.z;
                const float d = dx * dx + dy * dy + dz * dz;
                if (found == nullptr || d < best) {
                    best = d;
                    found = o;
                }
            }
            if (found != nullptr) node.door = found;
            mark_links(path, node, kNodeKick, kBoundKick, kLinkKick);
        }
    }
}
