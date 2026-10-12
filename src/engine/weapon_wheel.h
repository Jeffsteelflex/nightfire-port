#pragma once
// A roda de armas (só do port; docs/port.md "Mira moderna e roda de armas", docs/design.md "A roda de armas").
//
// Segurar o comando weapon_wheel (Tab; no controle L3, o único botão que nenhum dos oito estilos usa: platform/
// input_bindings.h) abre uma roda no estilo Modern sobre o jogo (ui/modern_wheel.h): no anel de fora as armas que o
// jogador tem (uma por classe, na variante escolhida, na ordem da troca de armas do jogo), no de dentro os aparelhos.
// O mouse (o movimento vira um vetor de escolha; o cursor some e a visão não gira) ou o analógico direito apontam um
// espaço; soltar o comando escolhe pelo caminho da troca do jogo: next_weapon como Player_WeaponChange o põe (as mesmas
// travas e a memória da última arma e do último aparelho), e a máquina de estados de Player_SetWeaponAnimObj guarda a
// arma e saca a nova com as animações do jogo. O meio da roda (sem espaço apontado) não troca nada.
//
// Enquanto ela está aberta: o jogo corre em câmera lenta ([game] wheel_slowmo, 0,25 por padrão: main.cpp roda um
// quarto dos ticks e a interpolação desenha entre eles; o som segue no tempo real), o jogador 0 não gira, não mira, não
// atira nem troca de arma (psiInput_SetWheelOpen; andar, pular e agachar continuam). Ela não abre (e fecha sem
// escolher) com um menu, numa cena com câmera, com o jogador morto, fora da primeira pessoa ou desativado.
//
// O tempo da missão (PlarStat_LogUpdateElapsedTime, psiGetTimeIn100ths) é o do relógio real, como no PS2 (que também
// conta o modo de 30 quadros por segundo em tempo real): com a roda aberta ele continua contando.
//
// Os espaços têm tamanho fixo (weapon_wheel_ring_slots): o anel de fora tem 8 espaços de 45 graus (12, 16 ou 24 quando
// o jogador tem mais armas), o de dentro 6 (8, 12 ou 16); as armas e os aparelhos que ele tem ocupam os primeiros, a
// partir de cima, na ordem da troca do jogo, e os outros ficam vazios (desenhados apagados; apontar um deles não
// escolhe nada). Assim duas armas não viram duas metades da roda.
//
// As imagens (weapon_wheel_icon): a de cada classe é a do dossiê do menu principal (ds_weapons/ds_gadgets, as tabelas
// copiadas do ELF, engine/menu_dossier.h; as pistolas pelas quatro imagens que P_DSWEAPONS_Handler põe por nível de
// melhoria, o barbeador pela regra de P_DSGADGETS_Handler). As texturas são do pacote do menu (07000048), que numa fase
// não está carregado: o nightfire.exe as decodifica do FILES.BIN ao iniciar (render/ui_modern_app.h) e a roda as
// desenha recortadas do disco verde do radar, em um tom só (ui/modern_wheel.h).

#include <cstdint>

#include "engine/object.h"

// Um espaço da roda.
struct WheelSlot {
    int8_t weapon = 0;        // a arma que soltar aqui escolhe (classe + variante)
    int8_t klass = 0;
    bool gadget = false;
    bool usable = false;      // tem munição (Player_WeaponHasAmmo): sem ela, apagado e sem escolha
    bool current = false;     // é a da mão (ou a pedida)
    bool uses_ammo = false;   // weapon_definition_tag::ammo_type != 0
    int16_t clip = 0;         // balas no pente (Player_AmmoInGun)
    int16_t stored = 0;       // guardadas (BLData::ammo)
    uint32_t name_label = 0;  // weapon_data +0x38 (o nome da arma no HUD)
    uint32_t mode_label = 0;  // +0x30 + modo x 4 (o modo de disparo); 0 nenhum
    uint32_t icon = 0;        // a imagem do dossiê (textura 03xxxxxx do pacote do menu; weapon_wheel_icon); 0 nenhuma
};
constexpr int kWheelMaxWeapons = 24;
constexpr int kWheelMaxGadgets = 16;

// Os espaços: as armas (gadgets = false: classes 1 a 0x46) ou os aparelhos (0x4a a 0x5e) que o jogador tem.
int weapon_wheel_slots(obj_tag* player, bool gadgets, WheelSlot* out, int max);
// Quantos espaços fixos um anel tem para `count` armas (8, 12, 16 ou 24) ou aparelhos (6, 8, 12 ou 16).
int weapon_wheel_ring_slots(int count, bool gadgets);
// A imagem do dossiê de uma classe (o hash da textura 03xxxxxx), ou 0 (o soco, as armas que o dossiê não tem).
uint32_t weapon_wheel_icon(int klass);
// Todas as imagens que weapon_wheel_icon pode dar (sem repetir); devolve quantas (até max).
int weapon_wheel_icon_hashes(uint32_t* out, int max);
// A linha da tabela de uma classe: tabela 0 nenhuma, 1 ds_weapons, 2 ds_gadgets, 3 a imagem fixa (as pistolas e o
// barbeador); index o item da tabela (as pistolas: o nível de melhoria). Para os testes e o nf_loader_check.
struct WheelIconRef {
    int table = 0;
    int index = -1;
};
WheelIconRef weapon_wheel_icon_ref(int klass);
// A roda pode abrir agora (jogador 0 vivo, ativo, em primeira pessoa, sem menu, sem cena, sem pausa).
bool weapon_wheel_can_open(obj_tag* player);
// A troca de arma do jogo aceitaria agora (as travas de Player_WeaponChange).
bool weapon_wheel_can_select(obj_tag* player);
// Pede a arma como Player_WeaponChange (next_weapon e a memória da última arma/aparelho). Falso sem trocar.
bool weapon_wheel_select(obj_tag* player, int8_t weapon);

// O espaço apontado por um vetor de escolha (x para a direita, y para cima, 1 = a borda de fora): anel 0 nenhum (o
// meio), 1 o de dentro (aparelhos), 2 o de fora (armas); index o espaço fixo do anel (0 em cima, sentido horário; de
// weapon_wheel_ring_slots(n) espaços, então pode passar de n - 1: um espaço vazio). Sem aparelhos, o anel de fora vale
// desde o meio; sem armas, o de dentro.
struct WheelPick {
    int ring = 0;
    int index = -1;
};
constexpr float kWheelDeadZone = 0.22f;  // o meio
constexpr float kWheelInnerEdge = 0.58f; // do meio até aqui, o anel de dentro
WheelPick weapon_wheel_pick(float x, float y, int n_outer, int n_inner);
// O ângulo do meio do espaço i de n espaços (radianos, 0 em cima, sentido horário).
float weapon_wheel_slot_angle(int i, int n);
// O ângulo do espaço da i-ésima arma (gadgets = false) ou aparelho de um anel com `count` deles (os espaços fixos).
float weapon_wheel_item_angle(int i, int count, bool gadgets);

// O estado da roda (um quadro de vídeo por vez, no tempo real).
class WeaponWheel {
public:
    // held: o comando seguro; mouse_dx/dy: o movimento do mouse neste quadro (contagens; y para baixo); stick: o
    // analógico direito (-1 a 1, y para cima); player: glb_players[0] (nulo: fecha).
    void frame(float dt, bool held, float mouse_dx, float mouse_dy, float stick_x, float stick_y, obj_tag* player);
    bool open() const { return open_; }
    // 0 a 1: a animação de abrir e fechar (a roda aparece com ela; fechada e em 0, nada a desenhar).
    float shown() const { return shown_; }
    bool visible() const { return shown_ > 0.0f; }
    // A lógica em câmera lenta (aberta).
    bool slowmo() const { return open_; }
    const WheelSlot* slots(bool gadgets) const { return gadgets ? gadgets_ : weapons_; }
    int count(bool gadgets) const { return gadgets ? n_gadgets_ : n_weapons_; }
    WheelPick hovered() const { return hover_; }
    float sel_x() const { return sel_x_; }
    float sel_y() const { return sel_y_; }
    // A última escolha feita ao soltar (-1 nenhuma) e quantas.
    int8_t last_choice() const { return last_choice_; }
    uint32_t choices() const { return choices_; }
    uint32_t opens() const { return opens_; }
    void reset();
    // "roda aberta A mostrada S armas N aparelhos M espacos E/F imagens K apontado R:I vetor X Y escolhas C ultima W
    // aberturas O" (espacos: os fixos de cada anel; imagens: os espaços com imagem do dossiê)
    int describe(char* out, unsigned size) const;
    // Mouse: contagens até a borda (o vetor anda 1 / kMouseCounts por contagem).
    static constexpr float kMouseCounts = 260.0f;

private:
    void refresh(obj_tag* player);

    bool open_ = false;
    float shown_ = 0.0f;
    WheelSlot weapons_[kWheelMaxWeapons];
    WheelSlot gadgets_[kWheelMaxGadgets];
    int n_weapons_ = 0;
    int n_gadgets_ = 0;
    WheelPick hover_;
    float sel_x_ = 0.0f, sel_y_ = 0.0f;
    float stick_peak_ = 0.0f;  // o maior empurrão do analógico desde que saiu do meio
    bool was_held_ = false;
    int8_t last_choice_ = -1;
    uint32_t choices_ = 0;
    uint32_t opens_ = 0;
};
