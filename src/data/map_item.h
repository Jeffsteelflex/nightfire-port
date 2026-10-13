#pragma once
// Itens de mapa da EuroLand (tipos 0, 1, 2 e 11): blocos, texturas, geometria (rígida e com
// skinning) e posicionamento. Formatos documentados em docs/formats.md; mesma lógica de
// tools/mapfile.py e tools/geometry.py.

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/binary.h"

namespace nf {

struct Block {
    uint8_t id = 0;
    uint32_t size = 0;    // inclui o cabeçalho de 4 bytes
    uint32_t offset = 0;  // posição no item
    ByteView payload(ByteView item) const { return sub(item, offset + 4, size - 4); }
    ByteView whole(ByteView item) const { return sub(item, offset, size); }
};

namespace block_id {
constexpr uint8_t kMapHeader = 0x0E;
constexpr uint8_t kPaletteHeader = 0x0F;
constexpr uint8_t kTextureHeader = 0x10;
constexpr uint8_t kPaletteData = 0x12;
constexpr uint8_t kTextureData = 0x16;
constexpr uint8_t kPlacement = 0x1A;
constexpr uint8_t kEnd = 0x1D;
constexpr uint8_t kEntityParams = 0x04;
constexpr uint8_t kEntityParamsAlt = 0x1F;
constexpr uint8_t kGfx = 0x2D;
}  // namespace block_id

std::vector<Block> read_blocks(ByteView item);

struct Texture {
    int width = 0;
    int height = 0;  // altura de um quadro
    int frames = 1;  // texturas animadas guardam os quadros empilhados
    bool has_alpha = false;
    std::vector<uint8_t> rgba;  // só o primeiro quadro, RGBA8 com o alfa como no GS (0x80 = opaco)
};

// Índice da textura (ordem do bloco 0x10) -> textura decodificada. Casos inconsistentes ficam de fora.
std::map<int, Texture> decode_textures(ByteView item, const std::vector<Block>& blocks);

// Uma textura: pixels indexados de 4 ou 8 bits (todos os quadros) e paleta RGBA do GS (alfa até
// 0x80, paletas de 256 cores no arranjo CSM1). Devolve false se o tamanho dos pixels não bate com
// o cabeçalho (casos raros ainda não entendidos).
bool decode_texture(int width, int height, int frames, bool bpp8, ByteView pixels, ByteView palette, Texture& out);
// O mesmo para um quadro de uma textura que o jogo reescreve durante a partida (o fundo animado da pausa): o resultado de
// decode_texture com frames = 1, escrito em `out` sem soltar a memória dele (depois da primeira vez, nada é alocado).
bool decode_texture_frame(int width, int height, bool bpp8, ByteView pixels, ByteView palette, Texture& out);

// Normal e cor como a VU1 as recebe (docs/formats.md, "Microprograma da VU1"): a normal é o inteiro do
// UNPACK V4-8 / 128, como a rotina de luz a usa (não normalizada: cerca de 127/128 de comprimento); a
// cor é a do disco, na escala do GS (0x80 = 1.0; r, g e b vão até 0xff, o dobro). Nos sprites a cor
// também está na escala do GS.
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint8_t color[4];  // RGBA na escala do GS
};

// Vértice de modelo com skinning na pose de repouso (a entrada do SkinIt, ver docs/formats.md,
// "Modelos com skinning"). Os ossos já são índices do esqueleto: o slot do vértice passou pela lista
// de ossos da malha (SkinInfo::bones_offset), que no PS2 escolhe as matrizes levadas à VU0.
struct SkinnedVertex {
    float pos[3];       // espaço do modelo, pose T
    float normal[3];    // a do disco (normal * 64) / 64; o SkinIt a leva a * 128 e a VU1 divide por 128
    float uv[2];
    uint8_t color[4];   // RGBA na escala do GS (0x80 = 1.0), como no disco
    uint8_t bones[4];   // [0] osso com peso "weight", [1] osso com peso 1 - weight; [2] e [3] = 0
    float weight;       // como está no arquivo (sempre entre 0 e 1 nos dados do jogo)
    // Índice de morph (16 bits altos do w da entrada do SkinIt): o elemento do bloco 0x2B onde começam os deltas
    // deste vértice, um por alvo (MorphTargets); -1 = sem morph.
    int32_t morph = -1;
};

// Alvos de morph de uma malha com skinning: o conteúdo do bloco 0x2B (parsemap_block_morph_data, entgfx_tag::morph;
// docs/formats.md, "Morphs do rosto"). Elementos de 12 bytes; os três primeiros u32 são o cabeçalho (vértices com
// morph, alvos por vértice, primeiro elemento dos deltas), seguidos de um u32 por vértice com morph (índice no
// dicionário de vértices de outras plataformas, sem uso no PS2) e dos deltas: para o vértice de índice de morph m e
// o alvo k, o delta é o elemento m + k. O SkinIt (0x0010bf30), antes do skinning, faz em cada vértice com m >= 0:
//   pos.xyz += soma, k em Morph_Index[0 .. Morph_Count), de Morph_Weight[k] * elemento[m + Morph_Index[k]]
// (vmulx.xyz e vadd na VU0). A VU0 trata números desnormalizados como zero: as palavras do cabeçalho lidas como
// float (alguns vértices do disco têm m = 0) valem 0, e aqui o cabeçalho já sai zerado.
constexpr int kMaxMorphTargets = 22;  // Morph_Weight[22] no ACTION.ELF; todos os rostos do disco têm 22
struct MorphTargets {
    uint32_t vertices = 0;      // +0
    uint32_t targets = 0;       // +4
    uint32_t first = 0;         // +8, em elementos de 12 bytes
    std::vector<float> deltas;  // todos os elementos do bloco, xyz; os desnormalizados (o cabeçalho) zerados
    bool empty() const { return deltas.empty(); }
    size_t elements() const { return deltas.size() / 3; }
};
// O conteúdo do bloco 0x2B (sem o cabeçalho de 4 bytes do bloco). Lança FormatError se o cabeçalho não fecha com o
// tamanho (first + vertices x targets elementos) ou se targets passa de kMaxMorphTargets.
MorphTargets decode_morph(ByteView payload);
// A posição de um vértice na pose de repouso com os morphs: a conta do SkinIt, com weights[k] o peso do alvo k
// (k < count, em ordem crescente de k, como Morph_Index depois de AnimObjectDraw; pesos 0 não mudam nada). Sem morph
// (v.morph < 0) é v.pos. Deltas fora do bloco valem 0 (como as leituras fora de um buffer na GPU).
void morph_vertex(const SkinnedVertex& v, const MorphTargets& m, const float* weights, int count, float pos[3]);

// Maior índice de osso aceito (tamanho da paleta no renderizador). Os índices da lista são u8; no
// disco o maior é 72 (esqueletos de até 73 ossos).
constexpr int kMaxSkinBones = 128;

// Posição e normal de um vértice com skinning dada a paleta (uma matriz vetor-linha de 16 floats
// por osso do esqueleto): w * (p * M[b0]) + (1 - w) * (p * M[b1]); a normal usa só a parte de
// rotação das mesmas matrizes, com os mesmos pesos, e não é renormalizada. É a conta do shader de
// skinning (src/render/renderer_d3d11.cpp), mantida aqui para os testes e para o lado da CPU.
void skin_vertex(const SkinnedVertex& v, const float* palette, float pos[3], float normal[3]);

// Estado do GS de um lote de geometria. A cadeia DMA de cada malha escreve os próprios registradores
// com pares A+D num pacote DIRECT (depois de cada referência de textura: REF, FLUSHE, DIRECT com
// ALPHA_1, ZBUF_1 e TEST_1), que valem para os lotes seguintes; o PRIM da GIF tag do lote diz se a
// mistura está ligada (ABE). has marca os registradores que a própria cadeia escreveu antes do lote;
// os outros são os que estavam no GS (no PC, os do desenho: psiSetUpColourBlend etc.).
struct GsState {
    enum : uint8_t { kAlpha = 1, kTest = 2, kZbuf = 4 };
    uint64_t alpha = 0;  // ALPHA_1 (0x42): A bits 0-1, B 2-3, C 4-5, D 6-7, FIX 32-39
    uint64_t test = 0;   // TEST_1 (0x47): ATE, ATST, AREF, AFAIL, DATE, DATM, ZTE, ZTST
    uint64_t zbuf = 0;   // ZBUF_1 (0x4e): bit 32 = ZMSK (não escreve no Z)
    uint8_t has = 0;
    uint8_t abe = 1;     // PRIM.ABE da GIF tag do lote (0: sem mistura)
    bool operator==(const GsState& o) const {
        return alpha == o.alpha && test == o.test && zbuf == o.zbuf && has == o.has && abe == o.abe;
    }
    bool operator!=(const GsState& o) const { return !(*this == o); }
    bool operator<(const GsState& o) const {
        if (has != o.has) return has < o.has;
        if (alpha != o.alpha) return alpha < o.alpha;
        if (test != o.test) return test < o.test;
        if (zbuf != o.zbuf) return zbuf < o.zbuf;
        return abe < o.abe;
    }
};

// Equação de mistura do GS (ALPHA_1: (A - B) * C + D, com A, B, D em {Cs, Cd, 0} e C em {As, Ad,
// FIX}) como fatores de uma unidade de mistura comum: cor = Cs * src (op) Cd * dst. C vale 0x80 = 1.0
// no GS; aqui As e Ad são os alfas já nessa escala e Fix = FIX / 128 (saturado em 1).
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha, Fix, InvFix };
enum class BlendOp : uint8_t { Add, Subtract, RevSubtract };  // src + dst, src - dst, dst - src
struct GsBlendEq {
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    BlendOp op = BlendOp::Add;
    bool enable = false;           // false: escreve Cs (ABE desligado, ou Cs * 1 + Cd * 0)
    bool supported = true;         // false: algum termo tem 1 + C; devolve a equação de transparência
    bool premultipliable = false;  // C = As e Cs entra com +-As: o shader pode multiplicar Cs por As
    float fix = 0.0f;
};
// premultiplied: o shader já multiplicou Cs por As (só vale quando premultipliable); o fator de Cs
// vira One, o que permite As acima de 1 (o alfa do GS vai até 0xff = 2.0).
GsBlendEq gs_blend_equation(uint64_t alpha, bool abe, bool premultiplied);
// Só do PC (o renderizador com MSAA): um desenho que o teste de alfa recorta e que fica opaco onde passa, para ir com o
// alfa para a cobertura (alpha-to-coverage) em vez de descartar pixels inteiros. atst/aref: o teste em vigor (ATST 5
// GEQUAL ou 6 GREATER contam; aref na escala do GS), aint: o teste inteiro do GS (senão a regra contínua antiga). Sem
// mistura (g.enable falso) qualquer limiar acima de 0 vale; com a mistura (Cs - Cd) * As + Cd, só um teste que deixa
// passar apenas As >= 1 (o TEST 0x507fd das folhas e grades: GREATER 0x7f), onde a mistura dá Cs.
bool gs_alpha_coverage_candidate(float atst, float aref, bool aint, const GsBlendEq& g);

// Escritas A+D de um pacote DIRECT/DIRECTHL (GIF tags em PACKED com descritor 0xE) em state;
// devolve quantos registradores foram escritos. Só ALPHA_1, TEST_1 e ZBUF_1 entram no estado.
int gs_apply_direct(ByteView packet, GsState& state);

struct SubMesh {
    int texture = -1;  // índice da textura no item; -1 = sem textura
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    GsState gs;        // estado do GS dos lotes desta parte
    // Palavra w da GIF tag dos lotes = 0x10: a VU1 usa a rotina 0x2e0 (mapa de ambiente: ST pela normal
    // e pela câmera, sem luzes); 8 nos outros (rotinas comuns, escolhidas por luzes e tweak).
    bool env = false;
};

struct EntityMesh {
    std::vector<Vertex> vertices;               // malhas rígidas
    std::vector<SkinnedVertex> skin_vertices;   // malhas com skinning (vertices fica vazio)
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
    // Com skinning: os alvos de morph do rosto (bloco 0x2B da entidade, que decode_gfx não lê: quem tem a entidade
    // os põe aqui, como RuntimeGfx::on_entity com entgfx_tag::morph). Vazio: sem morphs.
    MorphTargets morph;
    bool skinned = false;
    int bone_count = 0;        // com skinning: maior osso da lista da malha + 1 (matrizes da paleta)
    uint32_t bad_batches = 0;  // com skinning: lotes com vértices que não vêm do SkinIt (descartados)
    uint32_t inherited_gs = 0; // lotes desenhados sem ALPHA_1, TEST_1 e ZBUF_1 próprios (dependem do GS)
    bool empty() const { return indices.empty(); }
};

// Decodifica o bloco 0x2D (cadeia DMA/VIF para a VU1). frames_of(textura) dá o número de quadros
// para ajustar a coordenada V das texturas animadas. Com a estrutura de skinning, os vértices saem
// da entrada do SkinIt na pose de repouso (skin_vertices), com o índice de morph de cada um; os alvos (bloco
// 0x2B, outro bloco da entidade) vêm de decode_morph.
EntityMesh decode_gfx(ByteView gfx_block, const std::function<int(int)>& frames_of);
// O bloco 0x2D tem a estrutura de skinning? (false também se o cabeçalho estiver fora do bloco)
bool gfx_is_skinned(ByteView gfx_block);

struct Placement {
    uint16_t entity = 0;  // índice no próprio mapa, usado quando hash == 0xFFFFFFFF
    uint32_t hash = 0;
    uint32_t flags = 0;   // byte baixo: tipo do objeto
    float position[3]{};
    float euler[3]{};
    float scale[3]{1, 1, 1};
};

std::vector<Placement> read_placements(ByteView item, const std::vector<Block>& blocks);

// Matriz 4x4 na convenção do motor (vetor-linha: v' = v * M), com escala, RotMatrix(euler)
// (0x001e2368) e translação na última linha.
std::array<float, 16> placement_matrix(const Placement& p);

class MapItem {
public:
    MapItem(std::string name, int type, Bytes data);

    const std::string& name() const { return name_; }
    int type() const { return type_; }
    ByteView data() const { return data_; }
    const std::vector<Block>& blocks() const { return blocks_; }
    size_t entity_count() const { return entities_.size(); }
    const Block& entity_gfx(size_t i) const { return entities_[i].first; }
    uint32_t entity_hash(size_t i) const;
    const std::map<int, Texture>& textures() const { return textures_; }

private:
    std::string name_;
    int type_;
    Bytes data_;
    std::vector<Block> blocks_;
    std::vector<std::pair<Block, Block>> entities_;  // (geometria 0x2D, parâmetros 0x04)
    std::map<int, Texture> textures_;
};

}  // namespace nf
