// Testes da rede de navegação da IA (engine/ainetwork.h, aipath.cpp) sem dados do jogo: um caminho
// sintético de quatro nós em linha.

#include <cstring>

#include "engine/ainetwork.h"
#include "engine/memory.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

// Nós em x = 0, 3, 7, 12 (y e z zero) ligados em sequência, mais uma ligação 0-3 por cima (y = 0).
struct Line {
    alignas(16) MAPNODE_tag map[4];
    NODE_tag nodes[4];
    LINK_tag links[4];
    AIPath_tag path;
};

void build(Line& l) {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    std::memset(&l, 0, sizeof(l));
    const float xs[4] = {0.0f, 3.0f, 7.0f, 12.0f};
    for (int i = 0; i < 4; ++i) {
        l.map[i].index = int16_t(i);
        l.map[i].pos = {xs[i], 0.0f, 0.0f, 1.0f};
        l.nodes[i].node = &l.map[i];
    }
    const uint16_t ends[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    for (int i = 0; i < 4; ++i) {
        l.links[i].a = ends[i][0];
        l.links[i].b = ends[i][1];
    }
    l.path.node_count = 4;
    l.path.map_nodes = l.map;
    l.path.nodes = l.nodes;
    l.path.link_count = 4;
    l.path.links = l.links;
    AIPath_Prepare(&l.path);
}

}  // namespace

TEST(aipath_prepare_lengths_and_node_links) {
    Line l;
    build(l);
    CHECK(l.links[0].length == 3.0f && l.links[1].length == 4.0f && l.links[2].length == 5.0f);
    CHECK(l.links[3].length == 12.0f);
    // Cada nó tem duas ligações, em sequência em node_links, e todas tocam o nó.
    int total = 0;
    for (int n = 0; n < 4; ++n) {
        CHECK(l.nodes[n].link_count == 2);
        for (int k = 0; k < l.nodes[n].link_count; ++k) {
            const LINK_tag& link = l.links[l.path.node_links[l.nodes[n].first_link + k]];
            CHECK(link.a == n || link.b == n);
        }
        total += l.nodes[n].link_count;
    }
    CHECK(total == 8 && l.nodes[3].first_link == 6);
}

TEST(ainetwork_route_distance_and_offsets) {
    Line l;
    build(l);
    uint16_t list[4] = {0, 1, 2, 3};
    AIRoute_tag route{};
    route.path = &l.path;
    route.list = list;
    route.count = 4;
    // Pelas ligações da rota (3 + 4 + 5), com sinal pelo sentido.
    CHECK(AINetwork_GetRouteDistanceBetweenNodes(&route, 0, 3) == 12.0f);
    CHECK(AINetwork_GetRouteDistanceBetweenNodes(&route, 3, 0) == -12.0f);
    CHECK(AINetwork_GetRouteDistanceBetweenNodes(&route, 1, 2) == 4.0f);
    CHECK(AINetwork_GetRouteDistanceBetweenNodes(&route, 2, 2) == 0.0f);
    CHECK(AINetwork_GetRouteDistanceBetweenNodes(&route, 0, 4) == 0.0f);  // além do fim
    // Rota até o fim: para no último nó (ou -1 antes do primeiro); circular: dá a volta.
    route.flags = 1;
    CHECK(AINetwork_RouteNodeOffset(2, 1, &route) == 3 && AINetwork_RouteNodeOffset(3, 1, &route) == 3);
    CHECK(AINetwork_RouteNodeOffset(0, -1, &route) == -1);
    route.flags = 2;
    CHECK(AINetwork_RouteNodeOffset(3, 1, &route) == 0 && AINetwork_RouteNodeOffset(0, -1, &route) == 3);
}
