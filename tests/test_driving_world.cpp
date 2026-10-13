// O mundo das missões de carro (docs/driving.md seção 13): o CARP na memória (UGroup::Deserialize, as referências
// simbólicas e os resolvedores por etiqueta), a família MatBond (a referência do microprograma),
// o chão (WCollisionMgr, WGrid, WWorldPos) e a escolha do LOD, com dados montados à mão.

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "driving/carp.h"
#include "driving/carp_runtime.h"
#include "driving/mat_bond.h"
#include "driving/physics/world_collision.h"
#include "driving/track.h"
#include "test_framework.h"

using namespace drv;

namespace {

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(&b[at], &v, 4);
}
void putf(std::vector<uint8_t>& b, size_t at, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, at, u);
}

// Um UGroup montado à mão: a raiz 'CARP' com dois subgrupos ('Map ' e um 'Arti' de nome "caixa"). O 'Map ' tem uma
// instância ('in#0000', o artigo pela etiqueta 'sr#0001') e a referência simbólica 'sr#0001' = "CARP::caixa::{as
// 0001}"; o 'Arti' tem o 'Name', o artigo 'as#0001' (um LOD de 5 m, o modelo pela 'sr#0002') e 'sr#0002' =
// "EAGL::caixa_modelo".
struct Built {
    std::vector<uint8_t> store;
    uint8_t* p = nullptr;
};
Built build_carp() {
    std::vector<uint8_t> f(0x400, 0);
    // A raiz (0): dois subgrupos, posição relativa, vetor em +16.
    put32(f, 0x00, carp_tag("CARP"));
    put32(f, 0x04, 2u << 5 | 0x02);
    put32(f, 0x08, 0);
    put32(f, 0x0c, 1);
    // 'Map ' (0x10): 2 dados, vetor em 0x10 + 3 x 16 = 0x40.
    put32(f, 0x10, carp_tag("Map "));
    put32(f, 0x14, 0x02);
    put32(f, 0x18, 2);
    put32(f, 0x1c, 3);
    // 'Arti' (0x20): 3 dados, vetor em 0x20 + 4 x 16 = 0x60.
    put32(f, 0x20, carp_tag("Arti"));
    put32(f, 0x24, 0x02);
    put32(f, 0x28, 3);
    put32(f, 0x2c, 4);
    // Os dados do 'Map ' (0x40): 'in#0000' (64 bytes em 0x100) e 'sr#0001' (o texto em 0x140).
    put32(f, 0x40, carp_short_tag("in", 0));
    put32(f, 0x44, 64u << 8 | 0x03);
    put32(f, 0x48, 1);
    put32(f, 0x4c, 0x100 - 0x40);
    put32(f, 0x50, carp_short_tag("sr", 1));
    put32(f, 0x54, 32u << 8 | 0x03);
    put32(f, 0x58, 1);
    put32(f, 0x5c, 0x140 - 0x50);
    // Os do 'Arti' (0x60): 'Name' (0x180), 'as#0001' (40 bytes em 0x1a0), 'sr#0002' (0x1d0).
    put32(f, 0x60, carp_tag("Name"));
    put32(f, 0x64, 8u << 8 | 0x02);
    put32(f, 0x6c, 0x180 - 0x60);
    put32(f, 0x70, carp_short_tag("as", 1));
    put32(f, 0x74, 40u << 8 | 0x03);
    put32(f, 0x78, 1);
    put32(f, 0x7c, 0x1a0 - 0x70);
    put32(f, 0x80, carp_short_tag("sr", 2));
    put32(f, 0x84, 32u << 8 | 0x03);
    put32(f, 0x88, 1);
    put32(f, 0x8c, 0x1d0 - 0x80);
    // A instância: identidade, a posição (10, 2, -3), bandeiras 0x08 (sem corte no longe), o artigo 'sr#0001'.
    putf(f, 0x100, 1.0f);
    f[0x10c] = 0x08;
    putf(f, 0x114, 1.0f);
    put32(f, 0x11c, carp_short_tag("sr", 1));
    putf(f, 0x128, 1.0f);
    putf(f, 0x130, 10.0f);
    putf(f, 0x134, 2.0f);
    putf(f, 0x138, -3.0f);
    put32(f, 0x13c, 0x80000000u | 12u);  // raio 12 x 0,25 = 3 m
    std::memcpy(&f[0x140], "CARP::caixa::{as  0001}", 24);
    std::memcpy(&f[0x180], "caixa", 6);
    // O artigo: refs 0, um LOD (5 m, 'sr#0002'), passada 2, tipo 0, bandeiras 1.
    f[0x1a0 + 0x1c] = 1;
    f[0x1a0 + 0x1d] = 2;
    f[0x1a0 + 0x1f] = 1;
    putf(f, 0x1a0 + 0x20, 5.0f);
    put32(f, 0x1a0 + 0x24, carp_short_tag("sr", 2));
    std::memcpy(&f[0x1d0], "EAGL::caixa_modelo", 19);
    Built b;
    b.store.assign(f.size() + 16, 0);
    b.p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(b.store.data()) + 15) & ~uintptr_t(15));
    std::memcpy(b.p, f.data(), f.size());
    return b;
}

}  // namespace

TEST(driving_carp_decode_tag) {
    CHECK(UDataGroupDecodeTag("{as  0001}") == 0x61730001u);
    CHECK(UDataGroupDecodeTag("{Base}") == carp_tag("Base"));
    CHECK(UDataGroupDecodeTag("{ef  00a1}::12") == 0x656600a1u);
    CHECK(UDataGroupDecodeTag("as  0001") == 0);
}

TEST(driving_carp_deserialize_and_resolve) {
    Built b = build_carp();
    UGroup* root = reinterpret_cast<UGroup*>(b.p);
    UGroup_Deserialize(root);
    // Depois de Deserialize as posições são endereços e o bit 1 some.
    CHECK((root->flags & 2) == 0 && UGroup_GetArray(root) == reinterpret_cast<UData*>(b.p + 0x10));
    UGroup* map = UGroup_GroupLocateTag(root, carp_tag("Map "));
    CHECK(map != UGroup_GroupEnd(root));
    UData* in = UGroup_DataLocateFirst(map, 0x696e2020, 0, -1);
    CHECK(in != UGroup_DataEnd(map) && UData_Data(in) == b.p + 0x100);
    CHECK(UGroup_DataLocateTag(map, carp_short_tag("sr", 1)) != UGroup_DataEnd(map));
    CHECK(UGroup_DataLocateTag(map, carp_short_tag("sr", 9)) == UGroup_DataEnd(map));

    // A resolução: o espaço "EAGL" responde um endereço falso para o modelo.
    static uint32_t fake_model[4];
    CarpSymbols sym;
    sym.add_carp_file(root);
    sym.add_namespace("EAGL", [](const char* name, uint32_t& size) -> uint32_t {
        size = 0;
        return std::strcmp(name, "caixa_modelo") == 0 ? nf::p32_u(fake_model) : 0;
    });
    CARP_ResolveSymbolicReferences(root, sym);
    CHECK(sym.misses.empty());
    // A 'sr#0001' virou o endereço do 'as#0001' do artigo "caixa" (o tamanho 40, a contagem 0).
    UData* sr1 = UGroup_DataLocateTag(map, carp_short_tag("sr", 1));
    CHECK(UData_Data(sr1) == b.p + 0x1a0 && UData_Size(sr1) == 40 && sr1->count == 0);
    // O resolvedor do 'in' trocou a etiqueta do artigo pelos dados da 'sr#0001', e o do 'as' a do modelo.
    const CarpInstanceRec* inst = reinterpret_cast<const CarpInstanceRec*>(b.p + 0x100);
    CHECK(inst->article == nf::p32_u(b.p + 0x1a0));
    const CarpArticle* art = reinterpret_cast<const CarpArticle*>(b.p + 0x1a0);
    CHECK(art->lods[0].model == nf::p32_u(fake_model) && art->lods[0].distance == 5.0f);
}

TEST(driving_carp_tag_reference_rules) {
    Built b = build_carp();
    UGroup* root = reinterpret_cast<UGroup*>(b.p);
    UGroup_Deserialize(root);
    UGroup* map = UGroup_GroupLocateTag(root, carp_tag("Map "));
    UGroup* arti = UGroup_GroupLocateTag(root, carp_tag("Arti"));
    uint32_t r = 0;
    CARP_TagReference(&r, arti, map);
    CHECK(r == 0);  // 0 fica 0
    r = 0xFFFFFFFFu;
    CARP_TagReference(&r, arti, map);
    CHECK(r == nf::p32_u(map));  // -1: o grupo local
    r = carp_tag("Name");  // não está no local ('Map '): procura no compartilhado ('Arti' aqui)
    CARP_TagReference(&r, arti, map);
    CHECK(r == nf::p32_u(b.p + 0x180));
    r = carp_tag("Rule");  // em nenhum: 0
    CARP_TagReference(&r, arti, map);
    CHECK(r == 0);
}

// A família MatBond: a conversão "Packed" (u16 / 256 - 128), a cor do vértice com uma luz pontual, a névoa, a tela e a
// STQ x Q, e a STQ do mapa de ambiente, contra valores calculados à mão.
TEST(driving_matbond_reference_by_hand) {
    const BondMethodLayout* tp = bond_method_layout("MatBondTexturePacked");
    const BondMethodLayout* rp = bond_method_layout("MatBondReflectTexturePacked");
    CHECK(tp && rp && !bond_method_layout("MatCarTexturePacked"));
    CHECK(tp && !(tp->passes & kBondEnv) && rp && (rp->passes & kBondEnv));
    BondBatch b;
    b.layout = rp;
    b.count = 3;
    std::memset(b.mvp, 0, sizeof b.mvp);
    std::memset(b.world, 0, sizeof b.world);
    for (int i = 0; i < 4; ++i) {
        b.mvp[i][i] = 1.0f;
        b.world[i][i] = 1.0f;
    }
    b.mvp[3][3] = 0.0f;
    b.mvp[2][3] = 1.0f;  // clip.w = z
    b.camera[0] = 0.0f;
    b.camera[1] = 5.0f;  // menos 5: a origem do mapa de ambiente na altura 0
    b.camera[2] = 0.0f;
    b.camera[3] = 0.0f;
    std::memset(b.split, 0xff, sizeof b.split);
    for (int i = 0; i < 12; ++i) b.split[i] = 0x7fff;
    b.v.resize(3);
    // (u16 / 256 - 128): 32768 + 256 x (x + 0) -> x. Vértices em z = 2, 4, 8.
    const float zs[3] = {2.0f, 4.0f, 8.0f};
    for (int i = 0; i < 3; ++i) {
        b.v[size_t(i)].pos[0] = 32768u + 256u * 3u;  // x = 3
        b.v[size_t(i)].pos[1] = 32768u;              // y = 0
        b.v[size_t(i)].pos[2] = uint32_t(32768 + int(256 * zs[i]));
        b.v[size_t(i)].stq[0] = 32768u + 128u;  // s = 0,5
        b.v[size_t(i)].stq[1] = 32768u + 64u;   // t = 0,25
        b.v[size_t(i)].col[0] = 100;
        b.v[size_t(i)].col[1] = 50;
        b.v[size_t(i)].col[2] = 10;
        b.v[size_t(i)].col[3] = 0x80;
    }
    const BondVertexWork w = bond_convert(*rp, b.v[1]);
    CHECK(w.pos[0] == 3.0f && w.pos[1] == 0.0f && w.pos[2] == 4.0f && w.pos[3] == 1.0f);
    CHECK(w.stq[0] == 0.5f && w.stq[1] == 0.25f && w.stq[2] == 1.0f && w.col[0] == 100.0f && w.col[3] == 128.0f);
    CarConstants k;
    k.vp_offset[0] = k.vp_offset[1] = 2048.0f;
    k.vp_offset[2] = 0.0f;
    k.vp_scale[0] = 320.0f;
    k.vp_scale[1] = -224.0f;
    k.vp_scale[2] = 1.0f;
    // Uma luz pontual na origem com K = 1, cor (0,25, 0, 0): att = 255 - |p|^2; nos vértices |p|^2 = 9 + z^2.
    k.point_pos[3][0] = 1.0f;
    k.point_col[0][0] = 0.25f;
    for (int L = 1; L < 4; ++L) k.point_pos[0][L] = 1e6f;  // as outras longe (att 0)
    // A névoa: perto 1, longe 101, escala 2: F = 255 - 2 (clamp(w) - 1).
    k.fog[0] = 1.0f;
    k.fog[2] = 101.0f;
    k.fog[3] = 2.0f;
    std::vector<CarVertexOut> out[2];
    bond_reference(b, k, out);
    CHECK(out[0].size() == 3 && out[1].size() == 3);
    // Vértice 1 (z = 4): att = 255 - 25 = 230; vermelho = 100 + 0,25 x 230 = 157,5 -> 157.
    CHECK(out[0][1].rgba[0] == 157 && out[0][1].rgba[1] == 50 && out[0][1].rgba[2] == 10 && out[0][1].rgba[3] == 0x80);
    // Q = 1/4; tela x = 2048 + 3 x 320 / 4 = 2288 (x 16 no FTOI4), y = 2048 + 0, z = 4 x 1 / 4 = 1 -> 16.
    CHECK(out[0][1].xyzf[0] == 2288 * 16 && out[0][1].xyzf[1] == 2048 * 16 && out[0][1].xyzf[2] == 16);
    // F = 255 - 2 x (4 - 1) = 249 -> 249 x 16.
    CHECK(out[0][1].xyzf[3] == 249 * 16);
    CHECK(out[0][1].stq[0] == 0.125f && out[0][1].stq[1] == 0.0625f && out[0][1].stq[2] == 0.25f);
    // O ambiente: d = (3, 0, 4) normalizado x 0,4375 = (0,2625, 0, 0,35); s = 0,7625, t = 0,85 (x Q = 1/4).
    CHECK_NEAR(out[1][1].stq[0], 0.7625f * 0.25f, 1e-6f);
    CHECK_NEAR(out[1][1].stq[1], 0.85f * 0.25f, 1e-6f);
    // A mesma cor e posição nas duas passadas.
    CHECK(out[1][1].rgba[0] == 157 && out[1][1].xyzf[0] == out[0][1].xyzf[0]);
    // Vértice 2 (z = 8): att = 255 - 73 = 182 -> 100 + 45,5 = 145.
    CHECK(out[0][2].rgba[0] == 145);
}

TEST(driving_matbond_split_adc) {
    BondBatch b;
    b.layout = bond_method_layout("MatBondTexturePacked");
    b.count = 4;
    b.strip = true;
    for (int i = 0; i < 12; ++i) b.split[i] = 0x7fff;
    b.split[0] = 6;  // o recomeço no vértice 2: ADC nos vértices 2 e 3
    std::vector<uint16_t> tris;
    bond_triangles(b, tris);
    CHECK(tris.empty());  // os dois triângulos da faixa (fechados em 2 e 3) têm ADC
    b.split[0] = 9;  // recomeço no 3: só o primeiro triângulo fica
    tris.clear();
    bond_triangles(b, tris);
    CHECK(tris.size() == 3 && tris[0] == 0 && tris[2] == 2);
}

TEST(driving_track_viv_and_lod_rule) {
    CHECK(track_viv_name("paris_mis01") == "MIS01.VIV");
    CHECK(track_viv_name("junglea_mis13a") == "MIS13A.VIV");
    CHECK(track_viv_name("snow2a_race") == "RACE.VIV");
    TrackInstance inst;
    std::memset(inst.matrix, 0, sizeof inst.matrix);
    inst.matrix[12] = 10.0f;
    inst.radius = 3.0f;
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    // A distância menos o raio (10 - 3), vezes o multiplicador; nunca abaixo de 0.
    CHECK(Track::view_distance(inst, eye) == 7.0f);
    CHECK(Track::view_distance(inst, eye, 2.0f) == 14.0f);
    const float near_eye[3] = {9.0f, 0.0f, 0.0f};
    CHECK(Track::view_distance(inst, near_eye) == 0.0f);
}

// O chão (physics/world_collision.h) sobre uma grade montada à mão: uma célula com uma instância de colisão posta em
// (10, 2, 20) (a posição do referencial dela é o oposto), cujo artigo tem uma faixa de um triângulo no plano y = 0 dela.
TEST(driving_world_ground_on_a_hand_built_grid) {
    struct Art {
        CollisionArticle head;
        CollisionStrip strip;
        StripVertex v[3];
    };
    std::vector<Art> art(1);
    std::memset(art.data(), 0, sizeof(Art));
    art[0].head.strip_count = 1;
    art[0].strip.radius = 10 * 16;
    art[0].strip.vertex_offset = 16;  // depois da esfera, a partir de +0x20
    art[0].v[0] = {-5.0f, 0.0f, -5.0f, 3u};         // w: a contagem
    art[0].v[1] = {5.0f, 0.0f, -5.0f, 2u};          // w: qualquer sentido
    art[0].v[2] = {0.0f, 0.0f, 5.0f, 0x00120007u};  // w: tipo 7, subtipo 0, id 0x12
    std::vector<WCollisionInstance> inst(1);
    std::memset(inst.data(), 0, sizeof(WCollisionInstance));
    inst[0].right[0] = 1.0f;
    inst[0].forward[2] = 1.0f;
    inst[0].half_x = inst[0].half_z = 6.0f;
    inst[0].half_height = 1.0f;
    inst[0].position[0] = -10.0f;
    inst[0].position[1] = -2.0f;
    inst[0].position[2] = -20.0f;
    inst[0].radius = 8.0f;
    inst[0].article = nf::p32_u(art.data());
    std::vector<uint8_t> node(32, 0);
    reinterpret_cast<WGridNode*>(node.data())->static_count[0] = 1;  // o índice 0 logo depois do cabeçalho
    std::vector<uint32_t> table(4, 0);
    table[0 * 2 + 0] = nf::p32_u(node.data());  // linha 0 (z de 0 a 24), coluna 0 (x de 0 a 24)
    WGrid g{};
    g.cell_size = 24.0f;
    g.inv_cell = 1.0f / 24.0f;
    g.rows = g.columns = 2;
    g.nodes = nf::p32_u(table.data());
    WCollisionMgr mgr{};
    mgr.barrier_mask = 0xf0;
    mgr.instances = nf::p32_u(inst.data());
    mgr.instance_count = 1;

    // As células de uma caixa, presas à grade (coluna por coluna).
    std::vector<uint32_t> cells;
    const float lo[4] = {-100.0f, 0, -100.0f, 0}, hi[4] = {1000.0f, 0, 30.0f, 0};
    WGrid_FindNodesBox(g, lo, hi, cells);
    CHECK(cells.size() == 4 && cells[0] == 0 && cells[1] == 2 && cells[2] == 1 && cells[3] == 3);

    float pos[3];
    WCollisionInstance_CalcPosition(mgr, inst[0], pos);
    CHECK(pos[0] == 10.0f && pos[1] == 2.0f && pos[2] == 20.0f);

    // Acima da face, a 1 m: achada, com os vértices no mundo e a etiqueta da faixa.
    WWorldPos wp{};
    const float p[3] = {10.0f, 3.0f, 22.0f};
    CHECK(WWorldPos_FindClosestFace(wp, mgr, g, p, false, 77));
    CHECK(wp.valid == 1 && wp.instance == nf::p32_u(inst.data()) && wp.article == inst[0].article);
    CHECK(wp.v0[0] == 5.0f && wp.v0[1] == 2.0f && wp.v0[2] == 15.0f && wp.v2[2] == 25.0f);
    CHECK(wp.surface[0] == 7 && wp.surface[1] == 0 && wp.surface[2] == 0x12 && wp.last_query == 77);
    CHECK(mgr.query_stamp == 1 && inst[0].query_stamp == 1);
    // Guardada: dentro do mesmo triângulo não procura de novo (devolve falso); fora dele, procura.
    CHECK(!WWorldPos_FindClosestFace(wp, mgr, g, p, true, 78));
    CHECK(mgr.query_stamp == 1);
    const float out_of_tri[3] = {14.9f, 3.0f, 15.5f};
    CHECK(WWorldPos_FindClosestFace(wp, mgr, g, out_of_tri, true, 79));
    CHECK(wp.valid == 0 && mgr.query_stamp == 2);
    // Abaixo da face (mais de meio metro): nada.
    WWorldPos below{};
    const float under[3] = {10.0f, 1.0f, 22.0f};
    WWorldPos_FindClosestFace(below, mgr, g, under, false, 0);
    CHECK(below.valid == 0);
    // O subtipo na máscara de barreiras: a face não conta.
    art[0].v[2].w = 0x00121007u;
    WWorldPos masked{};
    mgr.barrier_mask = 0x10;
    WWorldPos_FindClosestFace(masked, mgr, g, p, false, 0);
    CHECK(masked.valid == 0);
    mgr.barrier_mask = 0x20;
    WWorldPos_FindClosestFace(masked, mgr, g, p, false, 0);
    CHECK(masked.valid == 1);
    // A face inventada no ponto.
    WWorldPos made{};
    WWorldPos_MakeFaceAtPoint(made, mgr, p);
    CHECK(made.valid == 1 && made.surface[0] == 1 && made.instance == mgr.instances && made.article == 0);
    CHECK(made.v1[0] > 10.19f && made.v1[0] < 10.21f && made.v2[2] > 22.19f && made.v2[2] < 22.21f);
    // O relevo: nada nos tipos sem ondas, -10 no buraco (tipo 6).
    const uint8_t plain[4] = {1, 0, 0, 0}, hole[4] = {6, 0, 0, 0};
    CHECK(WCollisionMgr_SurfaceBumpHeight(p, plain) == 0.0f);
    CHECK(WCollisionMgr_SurfaceBumpHeight(p, hole) == -10.0f);
    const float n[3] = {0.0f, 1.0f, 0.0f}, p0[3] = {0.0f, 2.0f, 0.0f};
    CHECK(WWorldMath_GetPlaneY(n, p0, p) == 2.0f);
}
