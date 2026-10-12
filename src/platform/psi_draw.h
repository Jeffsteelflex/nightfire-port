#pragma once
// Desenho de cada quadro (psiPreDraw ... psiPostDraw), implementado para PC.
//
// No PS2 estas funções montam a cadeia de DMA da VU1 e os registradores do GS. No PC elas
// registram o quadro em g_psi_frame: cada psiDrawObjectMatrix vira um PsiDrawCall com a matriz, a
// câmera em uso (psiUseCamera) e o estado de desenho do momento (mistura, fase transparente, cor de
// "tweak"). O renderizador lê o registro depois do Game_Draw.
//
// Mistura (psiSetUpColourBlend, valor do registrador ALPHA do GS):
//   0 aditiva (Cs * As + Cd), 1 transparência (Cs - Cd) * As + Cd, 2 opaca, 3 subtrativa.
//
// Modelos com skinning (psiDrawSkinObjectMatrix): o registro guarda uma cópia da paleta de ossos em
// g_psi_frame.palettes (o ponteiro do motor não sobrevive ao quadro) e o skinning é feito na GPU. Os rostos
// (morphs: a malha com bloco 0x2B, Morph_Count de AnimObjectDraw) guardam os pesos em g_psi_frame.morphs, um
// por alvo (kPsiMorphs), e o renderizador os soma à pose de repouso antes do skinning, como o SkinIt.
//
// Partículas (psiDrawParticleList): o registro guarda uma cópia das posições e cores em
// g_psi_frame.particles e um PsiDrawCall sem modelo (particles >= 0) na ordem do Game_Draw; o
// renderizador faz de cada partícula um quadrado virado para a câmera.
//
// Estilhaços (psiDrawShard, platform/psi_shard.h): cada um vira três vértices no mundo em
// g_psi_frame.tri_vertices e um PsiDrawCall sem modelo (triangles >= 0), com o estado de
// psiSetShardRenderStates; o renderizador os desenha como triângulos soltos.
//
// Desenho 2D (psiDrawSprites, platform/psi_2d.h): cada chamada copia os sprites para
// g_psi_frame.quads2d, com a origem de cada um, e vira um PsiDrawCall sem modelo (sprites2d >= 0) na
// ordem do Game_Draw; o renderizador os desenha na tela, por cima do que veio antes.
//
// Tweak (psiSetTweakARGB): os valores vão de 0 a 255 = 0 a 1.0 (TAmbient = valor / 255 é a linha de
// ambiente da matriz de cor das luzes, sceVu0LightColorMatrix em FillMatrixChainRot/Skin); com
// TweakRGBA o microprograma multiplica a cor e o alfa de cada vértice por ela. 0xff em tudo é neutro.
// Com luzes (psiLight_SetLights), o tweak é o termo ambiente e as duas luzes somam por cima; as contas
// de cada rotina estão em platform/vu1.h.
//
// Névoa (FogOn, psiPreGameRun): psiPostSolid marca em g_psi_frame.fog_at onde o PS2 passaria a névoa
// pelo Z de cada pixel; na lista transparente (AlphaFogObjects), psiDrawObjectMatrix reduz o alfa do
// tweak pela distância do objeto, como FillMatrixChainRot.
//
// Esmaecimento (psiFadeView, pedido pelo Game_Draw para cada câmera com Camera_SetFade em andamento, depois dos
// sprites dela): no PS2, CreateTotalScreenSprite manda um sprite sem textura do tamanho da tela com a cor e o alfa
// dados, ALPHA_1 0x44 ((Cs - Cd) * As + Cd, As na escala do GS: 0x80 já é opaco), sem teste nem escrita de Z (TEST_1
// 0x20002: ZTE 0). No PC: um PsiDrawCall sem modelo (fade = 1) na ordem do Game_Draw; o renderizador o desenha como um
// quadrado 2D na janela inteira (RuntimeGfx::collect_frame).
//
// Limpeza do Z (psiClearZ): no PS2, o pacote ZClearPacket (CreateZClearSprite) escreve Z = 1 (o mais longe; o
// GS testa GEQUAL) na tela inteira sem tocar na cor. O Game_Draw o manda antes da lista 2 das câmeras 0-3 (a arma e
// os braços em primeira pessoa, o clarão do tiro), que assim ficam sempre por cima do cenário e só se testam entre
// si. No PC: g_psi_frame.clear_z_at marca onde cada limpeza caiu e o renderizador limpa a profundidade ali.
//
// Só do port: o dono de cada desenho (psi_draw_set_owner, posto pelo View_DrawObjList, View_DrawSky e os
// anexos): a sala ou célula, o objeto, o elemento do céu ou o anexo que pediu o desenho, a lista de desenho
// (0 opaca, 1 transparente, 2 sobreposição) e a ordem do desenho dentro do dono. É a identidade com que o
// desenho é achado de novo no tick seguinte para a interpolação entre ticks (render/frame_interp.h); não muda
// nada do que é desenhado.

#include <cstdint>
#include <vector>

#include "engine/map_types.h"
#include "platform/psi_2d.h"

struct viewer_tag;
struct light_tag;

// Só do port: quem pediu o desenho (psi_draw_set_owner).
enum class PsiOwnerKind : uint8_t {
    None = 0,
    Cel = 1,     // sala ou célula (View_DrawCelGlist): o dono é o cel_tag
    Object = 2,  // objeto (View_DrawObjectGlist, AnimObjectDraw, Emitter_Draw): o obj_tag
    Sky = 3,     // elemento do céu (View_DrawSky): o SKYOBJ_tag
    Attach = 4,  // anexo (marcas de tiro; cel_tag::attach, obj_tag::attach): o ATTACH_tag
};

struct PsiDrawCall {
    MATRIX world;               // coluna w já corrigida por psiDrawObjectMatrix; identidade com skinning
    const celglist_tag* glist;
    const viewer_tag* camera;   // psiUseCamera em vigor
    uint32_t created;           // obj_tag::created (0 para salas e céu)
    uint8_t blend;              // psiSetUpColourBlend
    uint8_t alpha_pass;         // entre psiStartAlpha e psiEndAlpha
    uint8_t after_solid;        // depois de psiPostSolid
    uint8_t tweak[4];           // psiSetTweakARGB: a, r, g, b
    // Só com skinning: a paleta são g_psi_frame.palettes[palette .. palette + bones), uma matriz por
    // osso do esqueleto (índice do esqueleto, não o slot da malha). palette = -1: modelo rígido.
    int32_t palette = -1;
    uint16_t bones = 0;
    const obj_tag* obj = nullptr;  // o objeto animado (psiDrawSkinObjectMatrix)
    // Só nos rostos (com skinning, a malha com morphs e Morph_Count != 0): os pesos são
    // g_psi_frame.morphs[morph .. morph + kPsiMorphs), o do alvo k na posição k (0 nos que AnimObjectDraw não
    // escolheu). -1: sem morph (a pose de repouso como está).
    int32_t morph = -1;
    // Só nas partículas (glist nulo): índice em g_psi_frame.particle_lists.
    int32_t particles = -1;
    // Só nos triângulos soltos (glist nulo; os estilhaços de psiDrawShard): índice em
    // g_psi_frame.tri_lists.
    int32_t triangles = -1;
    // Só no desenho 2D (glist nulo; psiDrawSprites): índice em g_psi_frame.lists2d.
    int32_t sprites2d = -1;
    // Só no esmaecimento (glist nulo; psiFadeView): 1, com a cor e o alfa do sprite da tela inteira (alfa na escala
    // do GS: 0x80 = opaco).
    uint8_t fade = 0;
    uint8_t fade_rgba[4]{};
    // Registradores do GS escritos por psiSwitchZBufferRead/Write desde psiPreDraw (bit 1: TEST_1,
    // bit 2: ZBUF_1), em vigor neste desenho. Os modelos do jogo escrevem os próprios (pacotes A+D da
    // cadeia), que valem por cima destes.
    uint8_t gs_has = 0;
    uint64_t gs_test = 0;
    uint64_t gs_zbuf = 0;
    // Luzes de psiLight_SetLights em vigor (PS2NumLights, TLight, TColor0/TColor1): até 2, posição no
    // mundo (w = 1) e cor (r, g, b = cor da luz 0..255 x intensidade x multiplicador; w = 1 / raio²).
    // Sem a luz: rgb 0, w 1.
    uint8_t num_lights = 0;
    float light_pos[2][4]{};
    float light_color[2][4]{};
    // Só do port (psi_draw_set_owner): o dono do desenho, o tipo dele, a lista de View_DrawObjList (-1 fora
    // dela: céu, estilhaços, 2D) e a ordem deste desenho entre os do mesmo dono (0, 1, ...).
    const void* owner = nullptr;
    PsiOwnerKind owner_kind = PsiOwnerKind::None;
    int8_t list = -1;
    uint16_t owner_seq = 0;
};

// Uma partícula de psiDrawParticleList: posição no mundo e cor na escala do GS (0x80 = 1.0, também
// no alfa, que vai até 0xff).
struct PsiParticle {
    float pos[3];
    uint8_t rgba[4];
};

// Uma chamada de psiDrawParticleList: sprites quadrados e virados para a tela, um por partícula.
struct PsiParticleList {
    uint32_t first = 0;      // em g_psi_frame.particles
    uint32_t count = 0;      // já arredondado para baixo a múltiplo de 4, como no PS2
    int32_t texture = -1;    // índice da tabela Tex (pstex_tag::tex_index); -1: nenhuma
    float half_size = 0.0f;  // metade do lado, em metros (o tamanho que o sprite teria no PS2)
};

// Vértice de um triângulo solto (psi_draw_triangles): no mundo, com UV e a cor na escala do GS (como a
// VU1 a manda ao GS na rotina 0, sem tweak).
struct PsiTriVertex {
    float pos[3];
    float uv[2];
    uint8_t rgba[4];
};

// Uma chamada de psi_draw_triangles: count / 3 triângulos texturizados.
struct PsiTriList {
    uint32_t first = 0;    // em g_psi_frame.tri_vertices
    uint32_t count = 0;    // vértices (três por triângulo)
    int32_t texture = -1;  // índice da tabela Tex; -1: nenhuma
};

// Pesos de morph por desenho de rosto em PsiFrame::morphs (Morph_Weight[22] do ACTION.ELF).
constexpr int kPsiMorphs = 22;

struct PsiFrame {
    std::vector<PsiDrawCall> draws;
    std::vector<MATRIX> palettes;       // paletas copiadas pelos desenhos com skinning
    std::vector<float> morphs;          // pesos dos rostos, kPsiMorphs por desenho (PsiDrawCall::morph)
    std::vector<PsiParticle> particles;           // partículas copiadas por psiDrawParticleList
    std::vector<PsiParticleList> particle_lists;  // uma por chamada
    std::vector<PsiTriVertex> tri_vertices;       // triângulos soltos (estilhaços)
    std::vector<PsiTriList> tri_lists;            // uma por chamada de psi_draw_triangles
    std::vector<Psi2DQuad> quads2d;               // sprites 2D (psiDrawSprites), com a origem
    std::vector<Psi2DList> lists2d;               // uma por chamada de psiDrawSprites
    uint32_t clear_z = 0;               // psiClearZ (psiPreDraw chama uma vez)
    // Onde cada psiClearZ caiu: a limpeza vem antes de draws[i] (a de psiPreDraw é 0). O Game_Draw limpa o Z antes
    // da lista 2 (a arma em primeira pessoa, o clarão do tiro), que por isso nunca é cortada pelo cenário; o
    // renderizador limpa a profundidade no mesmo ponto (render/frame_markers.h).
    std::vector<int32_t> clear_z_at;
    uint32_t cameras = 0;               // psiUseCamera
    uint8_t fade[4]{};                  // último psiFadeView: r, g, b, a
    const viewer_tag* fade_camera = nullptr;
    uint32_t fades = 0;                 // psiFadeView neste quadro (cada um é também um PsiDrawCall com fade = 1)
    // ScreenBlur em vigor no psiPreDraw (psiSetScreenBlur do Game_Draw: 0x80 - GlobalBlur / 4): o peso do quadro novo,
    // em 128 avos, na cópia para o buffer de exibição (CreateSqueezeFrame). 0x80: sem rastro.
    uint32_t screen_blur = 0x80;
    bool finished = false;              // psiPostDraw
    // psiDrawFogPass (em psiPostSolid, com FogOn): a passada de névoa vem antes de draws[fog_at]
    // (-1: sem névoa neste quadro), com a cor (PS2FogR/G/B) e o Ps2Far1 em vigor.
    int32_t fog_at = -1;
    uint8_t fog_rgb[3]{};
    float fog_far1 = 0.0f;
};
extern PsiFrame g_psi_frame;

// Estado do desenho, com os nomes do PS2.
extern uint8_t FogOn;
extern uint8_t AlphaFogObjects;
extern uint32_t ScreenBlur;
extern uint8_t TweakRGBA;           // 1 se psiSetTweakARGB recebeu algo diferente de branco opaco
extern uint8_t olda, oldr, oldg, oldb;
extern float TAmbient[4];           // r, g, b, a de psiSetTweakARGB em 0..1
extern uint8_t WipeZBuffer;
// psiLight_SetLights: as duas luzes mais próximas em vigor (copiadas em cada PsiDrawCall).
extern uint32_t PS2NumLights;
extern float TLight[2][4];
extern float TColor0[4];
extern float TColor1[4];
// psiPreGameRun, por fase: névoa (FogOn, cor PS2FogR/G/B), o plano de longe do PS2 (Ps2Far1, em
// unidades do Z do GS) e os tamanhos dos sprites (PS2SnowSize, PS2ParticleSize = Ps2Far1 *
// 0.002386635).
extern uint8_t PS2FogR, PS2FogG, PS2FogB;
extern float Ps2Far1;
extern float PS2SnowSize;
extern float PS2ParticleSize;

void psiPreDraw();
void psiPostDraw();
void psiPostSolid();
void psiDrawFogPass();
void psiUseCamera(viewer_tag* viewer, uint16_t mode);
void psiEndUseCamera(viewer_tag* viewer);
void psiDrawObjectMatrix(celglist_tag* glist, MATRIX* m, uint32_t created);
// Modelo com skinning (chamado por AnimObjectDraw). palette: uma MATRIX por osso do esqueleto, no
// mundo e com a pose inversa (psiBuildMatrixPalette), indexada pelos valores da lista de ossos da
// malha; um vértice p da pose de repouso vai ao mundo por p * palette[osso]. matrix não é usada (o
// PS2 a troca pela identidade). obj: o objeto animado (obj->sphere recorta, obj->sphere_src não pode
// ser nulo). Copia palette[0 .. maior osso da lista] para g_psi_frame.palettes. Com os morphs em vigor
// (Morph_Count/Index/Weight de AnimObjectDraw, a malha com bloco 0x2B e, fora de cena, o objeto bem perto: o menor
// byte de ANIMOBJ_tag::closeness até 2), copia os pesos para g_psi_frame.morphs.
void psiDrawSkinObjectMatrix(celglist_tag* glist, MATRIX* matrix, obj_tag* obj, MATRIX* palette);
void psiSetTweakARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
void psiSetTweakA(uint8_t a);
void psiSetUpColourBlend(int32_t mode);
void psiStartAlpha();
void psiEndAlpha();
void psiLight_SetLights(P32<light_tag>* lights, uint16_t count, float r, float g, float b);
void psiFog(int8_t on, float near_dist, float far_dist, uint8_t r, uint8_t g, uint8_t b);
void psiSetScreenBlur(uint8_t blur);
void psiClearZ();
void psiDrawParticles(uint32_t data);
// Os sistemas de partículas da plataforma (Particles_Create, Particles_Update): vazios no PS2, que devolve o que sobrou
// no registro de retorno; no PC, 0 (um sistema que não existe e acaba no primeiro tick).
uint32_t psiCreateParticles(uint32_t kind, _VECTOR* pos, plane_equ_tag* plane);
uint32_t psiParticleUpdate(uint32_t system);
// Lista de sprites (no PS2: a cadeia de CreatePointerSpriteList[Colour] e o microprograma da VU1 em
// 0x46e com cores, 0x42b sem). positions: count posições (só xyz é lido); colours: count cores
// (r, g, b, a na escala do GS) ou nulo, que usa a cor fixa (0x3f, 0x3f, 0x3f, 0x7f); colour não é
// usado (nem no PS2); texture: o pstex_tag* da textura (0: fica a textura anterior do GS; no PC,
// nenhuma); additive != 0: mistura aditiva, senão transparência; size: tamanho com cores (sem cores
// vale PS2SnowSize). Liga o teste de Z sem escrita e alfa > 0 e deixa a mistura escolhida em vigor.
void psiDrawParticleList(_VECTOR4* positions, int32_t count, uint32_t colour, uint32_t texture, uint32_t* colours,
                         int8_t additive, float size, void* unused);
// TEST_1 (0: teste de Z ligado e alfa > 0; 1: sem teste de Z) e ZBUF_1 (0: escreve Z; 1: não escreve).
void psiSwitchZBufferRead(int8_t mode);
void psiSwitchZBufferWrite(int8_t mode);
// Metade do lado do sprite de psiDrawParticleList no mundo, em metros (with_colours: o caminho com
// lista de cores, que usa size; sem ela vale PS2SnowSize).
float psi_particle_half_size(float size, bool with_colours);
// Estado dos estilhaços (portada em platform/psi_shard.cpp, com psiDrawShard).
void psiSetShardRenderStates();
// Só no PC: registra count vértices (três por triângulo, já no mundo) como um desenho sem modelo, com a
// câmera, a mistura e os registradores do GS em vigor (psiDrawShard no lugar do CALL da cadeia).
void psi_draw_triangles(const PsiTriVertex* vertices, uint32_t count, int32_t texture);
// O sprite da tela inteira com a cor e o alfa (escala do GS) dados, sobre o que já foi desenhado (ver o comentário
// do começo). Registra um PsiDrawCall com fade = 1 na câmera em uso.
void psiFadeView(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
// Só no PC: registra count sprites 2D (psiDrawSprites) como um desenho sem modelo, com a câmera e os
// registradores do GS em vigor.
void psi_draw_2d(const Psi2DQuad* quads, uint32_t count);

// Só do port: o dono dos próximos desenhos (nulo: nenhum) e a lista de desenho em curso (-1: fora das listas).
// Um dono novo recomeça a contagem owner_seq. Ver o comentário do começo.
void psi_draw_set_owner(const void* owner, PsiOwnerKind kind, int8_t list);
// O dono em vigor (para devolvê-lo depois dos anexos).
const void* psi_draw_owner();
PsiOwnerKind psi_draw_owner_kind();
int8_t psi_draw_list();

// Só para testes: esvazia o registro e volta o estado inicial.
void psi_draw_debug_power_on();
