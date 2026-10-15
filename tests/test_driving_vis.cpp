// A visibilidade do mundo das missões de carro (driving/world_vis.h, driving/vu0_culling.h): propriedades da geometria
// lida no código do jogo, conferidas sem o original (a conferência bit a bit contra o DRIVING.ELF fica no
// nf_driving_check, parte 11): o frustum 2D de uma câmera olhando para +z (o que fica na frente, atrás, nos lados e além
// do alcance; o olho recuado quando a câmera olha para baixo), a árvore de quadrados (fora, dentro, cruzando), a cortina
// que esconde o que está atrás dela só vista da frente, o limite de 63 cortinas, o std::sort do DrawPass (ordenado e
// uma permutação, também com o limite de profundidade zero) e os objetos de cena (o nó do WTree::FindNode, as listas do
// SetMapNode, o SetNowVisible).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "driving/physics/vu0_physics.h"
#include "driving/vu0_culling.h"
#include "driving/world_vis.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

// A câmera no olho `eye` olhando para +z (as linhas: direita, cima, frente, o olho).
void camera_forward(const float eye[3], float m[16]) {
    const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, eye[0], eye[1], eye[2], 1};
    std::memcpy(m, rows, sizeof rows);
}

}  // namespace

TEST(driving_vis_frustum_2d) {
    set_vu0_mode(Vu0Mode::Native);
    const float eye[3] = {10, 5, 20};
    float m[16];
    camera_forward(eye, m);
    RRenderWorldCulling c{};
    RRenderWorldCulling_Construct(c, 33, 400);
    Setup2dFrustrum(c, m + 12, m, 30.0f, 100.0f);
    // O alcance e a metade do campo como foram pedidos; o olho no lugar (a câmera não olha para baixo).
    CHECK(c.range == 100.0f && c.fov == 30.0f && c.eye[0] == 10.0f && c.eye[2] == 20.0f);
    float d = -1;
    const float ahead[4] = {10, 5, 70, 0};
    CHECK(IsInFrustum2d(c, ahead, 1.0f, true, 1.0f, &d) && std::fabs(d - 50.0f) < 0.01f);
    const float behind[4] = {10, 5, 10, 0};
    CHECK(!IsInFrustum2d(c, behind, 1.0f, true, 1.0f, nullptr));
    // Um pouco atrás, mas com o raio passando do olho: dentro.
    CHECK(IsInFrustum2d(c, behind, 12.0f, true, 1.0f, nullptr));
    // Além do alcance: fora com o corte no longe, dentro sem ele (a distância fica 0) e com a escala 2.
    const float far_away[4] = {10, 5, 150, 0};
    CHECK(!IsInFrustum2d(c, far_away, 1.0f, true, 1.0f, nullptr));
    d = -1;
    CHECK(IsInFrustum2d(c, far_away, 1.0f, false, 1.0f, &d) && d == 0.0f);
    CHECK(IsInFrustum2d(c, far_away, 1.0f, true, 2.0f, nullptr));
    // Os lados: a 45 graus da frente fica fora (o meio campo é 30), a 20 graus dentro.
    const float side45[4] = {10 + 40, 5, 20 + 40, 0}, side20[4] = {10 + 18, 5, 20 + 50, 0};
    CHECK(!IsInFrustum2d(c, side45, 0.5f, true, 1.0f, nullptr));
    CHECK(IsInFrustum2d(c, side20, 0.5f, true, 1.0f, nullptr));
    // A árvore: um quadrado em volta do olho cruza; um pequeno bem na frente fica dentro; um atrás, fora.
    const float around[4] = {10, 0, 20, 50};
    CHECK(QuadtreeFrustrumCheck2d(c, around, 50 * 1.414f) == 2);
    const float small[4] = {10, 0, 60, 2};
    CHECK(QuadtreeFrustrumCheck2d(c, small, 2 * 1.414f) == 1);
    const float back[4] = {10, 0, -40, 5};
    CHECK(QuadtreeFrustrumCheck2d(c, back, 5 * 1.414f) == 0);
    // Olhando para baixo (45 graus) o olho recua |frente.y| x alcance x 0,2 na direção do horizonte.
    float down[16] = {1, 0, 0, 0, 0, 0.7071068f, 0.7071068f, 0, 0, -0.7071068f, 0.7071068f, 0, 10, 5, 20, 1};
    Setup2dFrustrum(c, down + 12, down, 30.0f, 100.0f);
    CHECK(std::fabs(c.eye[2] - (20.0f - 0.7071068f * 100.0f * 0.2f)) < 0.01f && std::fabs(c.eye[0] - 10.0f) < 0.01f);
}

TEST(driving_vis_curtains_hide_from_the_front) {
    set_vu0_mode(Vu0Mode::Native);
    // Uma parede de x = -10 a 10 em z = 50, de y = 0 a 20; a frente é o lado de (pé - topo) x (0, altura, 0) girado: o
    // +z (a normal (0, 20, 0) x (-20, 0, 0) = (0, 0, 400)), então o olho em z = 100 a vê de frente.
    WVisCurtain wall;
    const float a[4] = {-10, 0, 50, 0}, b[4] = {10, 20, 50, 0};
    WVisCurtain_Construct(wall, a, b);
    CHECK(std::fabs(wall.start[3] - 10.0f) < 0.001f);  // a metade do comprimento no plano xz
    const float eye[4] = {0, 5, 100, 1};
    InitActiveCurtainList();
    AddCurtain(wall, eye);
    CHECK(GetCurtainCount() == 1);
    // Uma esfera pequena atrás da parede some; uma na frente, uma ao lado (fora dos planos do olho), uma acima da linha
    // do topo e uma atrás mas grande demais ficam.
    const float hidden[4] = {0, 5, 20, 1}, front[4] = {0, 5, 70, 1}, side[4] = {40, 5, 20, 1}, above[4] = {0, 100, 20, 1},
                big[4] = {0, 5, 20, 40};
    CHECK(!IsVisibleAgainstCurtains(hidden, 0.0f));
    CHECK(IsVisibleAgainstCurtains(front, 0.0f));
    CHECK(IsVisibleAgainstCurtains(side, 0.0f));
    CHECK(IsVisibleAgainstCurtains(above, 0.0f));
    CHECK(IsVisibleAgainstCurtains(big, 0.0f));
    CHECK(!CheckPositionAgainstActiveCurtains(hidden) && CheckPositionAgainstActiveCurtains(front));
    // A mesma parede vista de trás não vira cortina.
    const float eye_back[4] = {0, 5, 0, 1};
    InitActiveCurtainList();
    AddCurtain(wall, eye_back);
    CHECK(GetCurtainCount() == 0);
    // Uma cortina que outra já esconde (as duas pontas atrás da primeira) não entra; o limite é 63.
    InitActiveCurtainList();
    AddCurtain(wall, eye);
    WVisCurtain behind_wall;
    const float c0[4] = {-2, 0, 20, 0}, c1[4] = {2, 10, 20, 0};
    WVisCurtain_Construct(behind_wall, c0, c1);
    AddCurtain(behind_wall, eye);
    CHECK(GetCurtainCount() == 1);
    for (int i = 0; i < 80; ++i) {
        WVisCurtain w;
        const float s[4] = {-10.0f + float(i), 0, 80, 0}, e[4] = {-9.5f + float(i), 30, 80.5f, 0};
        WVisCurtain_Construct(w, s, e);
        AddCurtain(w, eye);
    }
    CHECK(GetCurtainCount() <= 63);
    InitActiveCurtainList();
}

TEST(driving_vis_drawpass_sort) {
    // O std::sort do DrawPass: o resultado está em ordem pelo comparador e é uma permutação da entrada, com empates,
    // listas curtas (só a ordenação por inserção) e o limite de profundidade zero (o partial_sort).
    uint32_t s = 12345;
    auto rnd = [&] {
        s = s * 1103515245u + 12345u;
        return s >> 8;
    };
    for (int round = 0; round < 40; ++round) {
        const int n = round < 5 ? round * 4 : int(rnd() % 1000);
        std::vector<InstanceAndDistance> v(static_cast<size_t>(n));
        for (auto& x : v) {
            x.instance = 0x1000 + 0x40 * (rnd() % 500);
            x.distance = float(rnd() % 30) * 3.5f;
        }
        for (int sort = 1; sort <= 2; ++sort) {
            std::vector<InstanceAndDistance> p = v;
            const int depth = round % 3 == 0 ? 0 : 20;
            if (n > 0) {
                introsort_loop(p.data(), p.data() + n, depth, sort);
                final_insertion_sort(p.data(), p.data() + n, sort);
            }
            bool ordered = true;
            for (int i = 1; i < n; ++i) {
                if (sort == 1) ordered = ordered && p[size_t(i - 1)].instance <= p[size_t(i)].instance;
                else ordered = ordered && p[size_t(i - 1)].distance >= p[size_t(i)].distance;
            }
            CHECK(ordered);
            auto key = [](const InstanceAndDistance& x) { return (uint64_t(x.instance) << 32) | uint64_t(x.distance * 4); };
            std::vector<uint64_t> a, b;
            for (const auto& x : v) a.push_back(key(x));
            for (const auto& x : p) b.push_back(key(x));
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            CHECK(a == b);
        }
    }
}

TEST(driving_vis_scene_objects_find_node_and_lists) {
    // Uma árvore de dois níveis: a raiz (o quadrado de 0 a 100 em x e z) e quatro filhos de 50; nenhum neto.
    std::vector<MapNode> nodes(5);
    std::memset(nodes.data(), 0, sizeof(MapNode) * nodes.size());
    nodes[0].children = nf::p32_u(&nodes[1]);
    const float box[4] = {50, 0, 50, 50};
    const float s0[4] = {10, 0, 10, 5}, s1[4] = {90, 0, 10, 5}, s2[4] = {10, 0, 90, 5}, s3[4] = {90, 0, 90, 5};
    const float across[4] = {50, 0, 20, 5};  // cruza a divisa em x: fica na raiz
    CHECK(WTree_FindNode(&nodes[0], box, s0) == &nodes[1]);
    CHECK(WTree_FindNode(&nodes[0], box, s1) == &nodes[2]);
    CHECK(WTree_FindNode(&nodes[0], box, s2) == &nodes[3]);
    CHECK(WTree_FindNode(&nodes[0], box, s3) == &nodes[4]);
    CHECK(WTree_FindNode(&nodes[0], box, across) == &nodes[0]);
    // O SetMapNode: entra no começo da lista do nó; mudar de nó tira da lista antiga.
    std::vector<RSceneObj> objs(3);
    std::memset(objs.data(), 0, sizeof(RSceneObj) * objs.size());
    for (auto& o : objs) RSceneObj_SetMapNode(o, &nodes[1]);
    CHECK(nodes[1].scene_objs == nf::p32_u(&objs[2]) && objs[2].next == nf::p32_u(&objs[1]) &&
          objs[1].next == nf::p32_u(&objs[0]) && objs[0].next == 0 && objs[1].prev == nf::p32_u(&objs[2]));
    RSceneObj_SetMapNode(objs[1], &nodes[2]);
    CHECK(objs[2].next == nf::p32_u(&objs[0]) && objs[0].prev == nf::p32_u(&objs[2]));
    CHECK(nodes[2].scene_objs == nf::p32_u(&objs[1]) && objs[1].next == 0 && objs[1].map_node == nf::p32_u(&nodes[2]));
    RSceneObj_SetMapNode(objs[2], &nodes[2]);
    CHECK(nodes[1].scene_objs == nf::p32_u(&objs[0]) && objs[0].prev == 0);
    // O SetNowVisible: só com o bit 24; o bit 27 vai para a lista dos desenhados por último.
    PrepareSceneObjsForCulling(&scratchpad_list());
    objs[0].flags = 0x01000000;
    objs[1].flags = 0x09000000;
    objs[2].flags = 0;
    for (auto& o : objs) RSceneObj_SetNowVisible(o);
    const SceneObjCullState& sc = scene_obj_cull();
    CHECK(sc.num_visible == 1 && sc.visible[0] == nf::p32_u(&objs[0]));
    CHECK(sc.num_draw_last == 1 && sc.draw_last[0] == nf::p32_u(&objs[1]));
}
