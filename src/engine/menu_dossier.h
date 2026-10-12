#pragma once
// O dossiê do menu principal (arquivo de menu 08000002): os recordes, as recompensas, os aparelhos e as armas. Ver
// docs/engine.md, "Menu principal", e engine/menu_frontend.h para o resto do caminho de um jogador.
//
// De onde abre (o quadrado, mensagem 0x5d): o mapa das missões (C_SBNFMAP_Handler, com o íris), o resultado da
// missão (P_NFRESULTS_Handler) e o bônus (P_NFBONUS_Handler). Voltar (triângulo) desempilha a página de antes.
//
//   0x4000002b o dossiê (P_DOSSIER_Handler; a roda C_SBDOSSIER_Handler, 0x1000010c, sobre ds_options: Records,
//              Rewards, Gadgets, Weapons)
//   0x4000003a os recordes (P_DSRECORDS_Handler; a escolha de missão C_RBDSRECORDS_Handler, 0x10000174, um rádio com
//              as 12 de sp_level: o recorde e os momentos Bond da ScoringTable, as melhorias ganhas e a melhor medalha)
//   0x4000003b as recompensas (P_DSREWARDS_Handler; C_RBDSREWARDS_Handler, 0x1000017a: as quatro recompensas da
//              missão na RewardsTable, cada uma visível só com a sua medalha e as de baixo)
//   0x4000003c os aparelhos (P_DSGADGETS_Handler; a roda C_SBDSGTSCROLL_Handler, 0x1000016f, sobre ds_gadgets)
//   0x4000003d as armas (P_DSWEAPONS_Handler; a roda C_SBDSWPSCROLL_Handler, 0x10000173, sobre ds_weapons)
//
// As páginas dos aparelhos e das armas mostram imagens (texturas 03xxxxxx do pacote do menu, no rótulo de 150 x 150
// ao lado da roda, como o mapa das missões), não modelos 3D: o desenho é o da camada 2D, sem câmera especial.
//
// O que libera: as medalhas (os bits de `bonus`, menu_bonus no port, que Menu_SetLevelBonus liga no resultado e o
// perfil grava no bloco BNUS). Bronze: o cartão de recompensa; prata: uma roupa, um cenário ou um modo do
// multijogador; ouro: uma melhoria (tipo 4, contada por grupo: Menu_GetObjectUpgradeLevel); platina: outra roupa.
// As melhorias aparecem nos textos dos aparelhos e das armas e trocam a pistola (Wolfram PP7) da página das armas.

#include <cstdint>

#include "engine/menu.h"
#include "engine/menu_frontend.h"

extern M_ITEM ds_options[4];   // 0x002dfcd0 as quatro páginas do dossiê (copiadas do ELF)
extern M_ITEM ds_weapons[27];  // 0x002dfec8 as armas (valor: o grupo de melhoria, 0x3f nenhum)
extern M_ITEM ds_gadgets[14];  // 0x002e0150 os aparelhos

int32_t P_DOSSIER_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBDOSSIER_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_DSRECORDS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_RBDSRECORDS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_DSREWARDS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_RBDSREWARDS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_DSGADGETS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBDSGTSCROLL_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t P_DSWEAPONS_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);
int32_t C_SBDSWPSCROLL_Handler(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b);

// O nível de melhoria de um grupo (`id`: o bit base das recompensas de tipo 4, 0 a pistola, 9 o rifle de precisão...)
// do jogador: quantas medalhas de ouro do grupo ele tem (0 nenhuma ou fora da faixa).
uint32_t Menu_GetObjectUpgradeLevel(uint32_t id, uint8_t player);

// Só do port: "dossie pagina X item N ..." (relatório das capturas): a página aberta do dossiê, a posição da roda e,
// nos recordes e nas recompensas, a missão, o recorde, a medalha e as recompensas visíveis.
int MenuDossier_Describe(char* out, size_t size);
// O estado inicial do módulo (testes; as tabelas do ELF ficam).
void menu_dossier_debug_power_on();
