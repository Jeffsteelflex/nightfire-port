#pragma once
// O desenho da roda de armas no estilo Modern (só do port; docs/design.md "A roda de armas"). Sem GPU: a vista
// (WheelView, montada de engine/weapon_wheel.h por make_wheel_view, com os nomes do jogo) vira três listas na tela
// virtual de 1920 x 1080, que o renderizador desenha por cima do quadro do jogo (o HUD fica embaixo): o fundo (os
// espaços, o brilho, o miolo; Direct2D), os ícones (imagens do port pelo Direct3D, ModernUiApp::draw_wheel) e a frente
// (os textos, os losangos, o vetor de escolha; Direct2D).
//
// A roda no meio da tela, com espaços de tamanho fixo (engine/weapon_wheel.h: 8 no anel de fora, 6 no de dentro; mais
// quando o jogador tem mais): as armas que o jogador tem ocupam os primeiros a partir de cima, na ordem do jogo; os
// outros ficam como vidro apagado. Cada espaço com arma tem o ícone (a imagem do dossiê recortada, de um tom só), o
// nome (versalete espaçado) e a munição (pente-guardada, como o HUD do jogo); nos anéis de muitos espaços, só o ícone e
// a munição (o nome aparece no miolo). O apontado: o vidro mais claro e quente, um degradê de ouro que sobe para a
// borda, a borda de ouro do lado de fora com dois risquinhos nos cantos e o brilho de brasa além dela; o ícone e o nome
// em ouro. Sem munição: apagado, a borda em cinza. A da mão: o losango de ouro na borda de dentro. O miolo mostra o
// apontado em detalhe (nome, munição grande, modo de disparo; "No ammo", "In hand") ou, sem nada apontado, a arma da
// mão e "Release to keep". Embaixo, numa pílula escura, a tecla da roda e "Release to equip". O vetor de escolha
// aparece como um losango de brasa. Tudo entra com a escala e o alfa da animação de abrir (shown).

#include <cstdint>
#include <string>
#include <vector>

#include "ui/modern_kit.h"

class WeaponWheel;

namespace nf::ui {

struct WheelViewSlot {
    std::wstring name;
    std::wstring ammo;  // "7-41", "—" sem munição a gastar
    std::wstring mode;  // o modo de disparo (vazio se a arma tem um só)
    bool usable = true;
    bool current = false;
    uint32_t icon = 0;  // a imagem do port (ImageCmd::port; 0: o espaço só com o texto)
};

struct WheelView {
    std::vector<WheelViewSlot> weapons, gadgets;
    int outer_slots = 8;  // os espaços fixos de cada anel (weapon_wheel_ring_slots)
    int inner_slots = 6;
    int hover_ring = 0;   // 0 nenhum, 1 aparelhos, 2 armas
    int hover_index = -1; // o espaço apontado (pode ser um vazio)
    float sel_x = 0.0f, sel_y = 0.0f;  // o vetor de escolha (1 = a borda de fora; y para cima)
    float shown = 1.0f;                // a animação de abrir (0 a 1)
    std::wstring key;                  // a tecla da roda (teclado) ou
    std::string glyph;                 // o glifo do controle
    std::wstring in_hand;              // o nome da arma da mão (o miolo sem nada apontado)
};

// A geometria (unidades da tela virtual): o centro e os raios dos anéis.
struct WheelGeometry {
    float cx = 960.0f, cy = 540.0f;
    float hole = 112.0f;     // o miolo
    float inner0 = 122.0f;   // o anel dos aparelhos
    float inner1 = 222.0f;
    float outer0 = 232.0f;   // o anel das armas
    float outer1 = 372.0f;
    float gap = 3.0f;        // a folga entre os espaços (pixels de cada lado, no meio do anel)
};
const WheelGeometry& wheel_geometry();

// Um espaço fixo de um anel (ring 1 de dentro, 2 de fora; index de 0 a slots - 1, 0 em cima, sentido horário), com a
// escala k da animação de abrir.
struct WheelSlotGeom {
    float a0 = 0.0f, a1 = 0.0f;  // o setor desenhado (com a folga), radianos
    float mid = 0.0f;            // o meio
    float r0 = 0.0f, r1 = 0.0f;  // os raios
    float rm = 0.0f;             // o raio do meio
    float px = 0.0f, py = 0.0f;  // o centro do conteúdo
    float box = 0.0f;            // o lado do quadrado que cabe no espaço (o conteúdo)
    bool names = true;           // há lugar para o nome (senão o ícone e a munição; o nome vai para o miolo)
};
WheelSlotGeom wheel_slot_geometry(const WheelGeometry& g, int ring, int index, int slots, float k = 1.0f);

// A vista de agora da roda (os nomes pelos rótulos do jogo, Txt_BindLabel; a tecla e o glifo de quem chama).
WheelView make_wheel_view(const ::WeaponWheel& w, const std::wstring& key, const std::string& glyph);

// As três camadas (ver o cabeçalho): back e front pelo Direct2D, icons pelo Direct3D entre elas.
void build_weapon_wheel(DrawList& back, ImageList& icons, DrawList& front, const Canvas& cv, const Tokens& tk,
                        const Measure& m, const WheelView& v);

// O ícone da roda feito de uma imagem do dossiê (128 x 128: a arma sobre o disco verde do radar; RGBA com o alfa do GS,
// 0x80 opaco, como decode_texture dá): fora do disco e onde o verde do radar domina (o vermelho e o azul abaixo de ~60%
// do verde: o fundo; o verde-oliva de uma granada passa) fica transparente; o resto vira um tom só (a luminância, sem o
// verde refletido, levantada para as armas pretas aparecerem sobre o vidro escuro), que a roda tinge com a cor do
// estado. Devolve RGBA do mesmo tamanho, com o alfa do GS.
std::vector<uint8_t> wheel_icon_image(const uint8_t* rgba, int w, int h);

// O texto do jogo (Windows-1252) em UTF-16.
std::wstring wheel_game_text(const char* cp1252);

}  // namespace nf::ui
