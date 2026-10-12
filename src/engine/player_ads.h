#pragma once
// A mira moderna (ADS, "aim down sights") e o estilo de mira (só do port; docs/port.md "Mira moderna e roda de armas").
//
// No PS2 o botão de mirar (ação 19, Player_Weapon) liga o modo de mira do jogo (ANIMOBJ_tag::flags bit 0): a mira se
// solta na tela e o analógico a move (Player_Aiming), a arma fica no quadril e só gira para a mira (Player_WeaponRecoil),
// a mira automática para (Player_AutoAim), e as armas de luneta (weapon_definition_tag::flags 0x40) escondem a arma e
// mostram a luneta do HUD (HUD_UpdateCrossHair), com o zoom do direcional (Player_Zoom, BLData::zoom, que a câmera
// leva em viewer_tag::lod_scale e PS2WorldViewVU1Kick2 multiplica na projeção).
//
// Com o estilo moderno (`[game] aim_style`: auto = moderno enquanto o último aparelho é o teclado e o mouse,
// psiInput_ActiveDevice; modern; classic = o do jogo), segurar o botão de mirar continua ligando o modo de mira do jogo
// (a mesma ação, a mesma regra Toggle/Hold do "Manual Aim"; a mira automática continua desligada mirando, como no
// jogo), e o port acrescenta:
//  - armas de luneta: nada muda (a luneta, o zoom e a roda que muda o zoom são os do jogo);
//  - as de zoom sem luneta (o zoom máximo acima de 1: uma luneta no modelo, em cima da linha de mira) e os
//    lança-foguetes 0x2c-0x31: a visão do jogo, com a mira presa no centro (abaixo);
//  - as outras armas de fogo: a mira fica presa no centro (Player_Aiming trata a arma como a de luneta: o analógico e as
//    setas giram a visão, BLData::aim_x/aim_y ficam 0, e o tiro sai do centro pelo Player_GetAimingPoint de sempre); com
//    a linha de mira medida (kAdsSights), a arma vai, em ~0,17 s, da pose do quadril para a pose de mira, com a alça e a
//    massa na linha do centro da tela (ads_pose); um zoom leve da visão (1,2 a 1,35, por classe de arma) só no desenho e
//    na sensibilidade do mouse; os braços descem (ads_pose_arms); a mira do HUD some. Recarregar, trocar de arma ou de
//    variante tira a arma da pose de mira (e ela volta sozinha no fim, com o botão ainda seguro), como nos jogos
//    modernos: a animação de recarga do jogo foi feita para o quadril. O coice (BLData::recoil_y) e as animações de tiro
//    continuam; o balanço de andar cai a 25%.
// O controle no estilo clássico (auto com o controle) não muda nada.
//
// A linha de mira de cada arma (alça e massa no espaço do objeto da arma na mão, na pose do primeiro quadro da
// animação de espera) foi medida da geometria do jogo por ads_find_sights (nf_loader_check --miras, tests/
// sights_measure.cpp): os vértices das partes rígidas da arma (ANIMSKIN_tag::discrete, presas aos ossos 38+), no
// corredor do cano (o anexo 0 da pele, o mesmo do clarão), a até 4 cm do x do cano e da mão direita menos 10 cm para a
// frente; o casco convexo de cima do perfil e a aresta quase horizontal mais longa dele. Sem aresta assim, o eixo do
// cano um pouco abaixo do centro.

#include <cstddef>
#include <cstdint>

#include "engine/object.h"

struct BLData;

// ---- o estilo de mira ----
enum class AimStyle : uint8_t { Auto = 0, Modern = 1, Classic = 2 };
const char* ads_aim_style_name(AimStyle s);  // "auto", "modern", "classic"
bool ads_parse_aim_style(const char* text, AimStyle* out);
// O estilo moderno vale para o aparelho (0 teclado e mouse, 1 controle; psiInput_ActiveDevice)?
bool ads_modern_for(AimStyle s, int device);

// ---- a linha de mira ----
struct AdsSights {
    float rear[3];   // a alça (ou o ponto de trás da linha) no espaço do objeto da arma
    float front[3];  // a massa
};
// Uma linha da tabela medida: a pele da arma (05xxxxxx, ANIMSKIN_tag::hash), a linha, o zoom leve e a qualidade
// (1 alça e massa achadas, 2 o eixo do cano, abaixo do centro em drop metros na profundidade da alça).
struct AdsSightEntry {
    uint32_t skin;
    float rear[3];
    float front[3];
    float zoom;
    uint8_t quality;
    float drop;
};
const AdsSightEntry* ads_sight_entry(uint32_t skin);
int ads_sight_entry_count();
const AdsSightEntry* ads_sight_entries();

// O achador. pts: os vértices da arma (espaço do objeto, x para o lado, y para cima, z para a frente), já no corredor
// do cano; muzzle: o anexo do cano (o x dele é o meio da arma); z_min: a silhueta só conta daí para a frente (a mão
// direita menos 10 cm: a coronha atrás dela não é linha de mira). Devolve falso sem uma aresta de cima quase horizontal
// de pelo menos 1/4 do comprimento ou 20 cm (a linha então é o eixo do cano: out recebe o eixo, e quem chama põe o
// drop).
struct AdsFindStats {
    int band_points = 0;  // vértices na faixa da silhueta
    float length = 0.0f;  // comprimento da arma em z
    float span = 0.0f;    // comprimento da aresta escolhida
};
bool ads_find_sights(const float (*pts)[3], size_t n, const float muzzle[3], float z_min, AdsSights* out,
                     AdsFindStats* stats);

// A pose de mira: os ângulos (RotMatrixZYX, como obj_tag::rot da arma na mão) e a posição (obj_tag::pos, com a
// compensação (0, 0,2, -0,5) que Player_SetWeaponAnimObj soma e Player_PositionGun desfaz) que põem a alça a `depth`
// metros na frente do olho, `drop` metros abaixo do centro, e a massa na mesma linha do centro.
struct AdsPose {
    float rot[3];
    float pos[3];
};
AdsPose ads_pose(const AdsSights& s, float depth, float drop);
// Onde um ponto do objeto da arma fica no espaço da visão (x, y, z: lado, cima, frente a partir do olho) com rot/pos,
// como Player_PositionGun o põe (sem a inclinação nem o giro do jogador, que são os da câmera).
void ads_view_point(const float rot[3], const float pos[3], const float p[3], float out[3]);

// A curva da transição (0 a 1, suave nas pontas).
float ads_ease(float t);
// Um tick da transição: o peso linear anda FRAME_RATE_MUL / 10 por tick (~0,17 s) para 1 (want) ou para 0.
float ads_step_weight(float t, bool want, float frame_rate_mul);

// ---- o estado (jogador 0) ----
// O peso da pose de mira (0 quadril, 1 mirando), sem e com a curva.
float ads_weight();
// O zoom leve em vigor (1 sem): o desenho divide a tangente do campo de visão por ele vezes o zoom do jogo.
float ads_view_zoom();
// O zoom total que a sensibilidade do mouse usa: o do jogo (BLData::zoom) vezes o leve.
float ads_effective_zoom(const BLData* b);
// A mira presa no centro (o estilo moderno vale e a arma na mão tem linha de mira e não é de luneta).
bool ads_fixed_crosshair(obj_tag* obj);
// A mira do HUD some (pose de mira mais da metade).
bool ads_hide_crosshair(const BLData* b);
// Andar para a frente e para trás mirando (o estilo moderno vale para o jogador 0). No PS2 o modo de mira zera a frente
// e o giro em Player_Move (o analógico move a mira); no estilo moderno o mouse gira a visão e W/S andam, como nos jogos
// de PC.
bool ads_move_while_aiming(obj_tag* obj);
// Um tick, no fim de Player_WeaponRecoil: anda o peso e mistura a pose do jogo (gun->pos/rot, com o balanço "sway")
// com a de mira.
void ads_tick(obj_tag* player, const _VECTOR* sway);
// Depois de AnimObjectUpdate da arma na mão (Player_Collision): na pose de mira, o peito, os braços e parte dos
// antebraços (ossos 0, 1, 19, 37 e 2, 20 da arma na mão) descem e recuam no referencial do olhar, as mãos e a arma ficam:
// a pose do quadril com a arma no centro poria o ombro na frente do olho. As partes rígidas da arma já foram postas.
void ads_pose_arms(obj_tag* player);
// O esqueleto dos braços das peles de arma (os 38 primeiros ossos: peito 0, braços 1-2-3 e 19-20-21, 37 no 2)?
bool ads_arm_skeleton(const uint8_t* parents, uint32_t bones);
// O estilo em vigor e o aparelho (para os testes: o padrão segue psiInput_ActiveDevice e a opção do .ini).
void ads_set_style(AimStyle s);
AimStyle ads_style();
// Só para testes: o aparelho no lugar de psiInput_ActiveDevice (-1 volta a usar o real).
void ads_set_device_override(int device);
// "ads estilo S aparelho D ativo A peso W zoom Z arma X linha Q" (relatório das capturas).
int ads_describe(char* out, unsigned size);
// Um jogador novo (Player_Init): a pose no quadril, sem zoom leve.
void ads_reset();
void ads_debug_power_on();
// Só para testes: uma linha a mais na tabela (nula: nenhuma), procurada antes das medidas.
void ads_debug_extra_entry(const AdsSightEntry* e);
