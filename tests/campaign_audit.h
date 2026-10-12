#pragma once
// Auditoria da campanha a pé (nf_loader_check): por fase de missão (MissionData, pacotes 07xxxxxx), o que o port
// ainda não tem para completá-la. Ver docs/engine.md, "Campanha: o que falta para completar".
//
// - provisórios: os posicionamentos cujo *_Create ainda passa por pending_create (g_pending_placements), por função;
// - subestados: o que a fase pede ao jogador pelos posicionamentos (escada 0x22, corda e tirolesa 0x3e pelo tipo
//   do fio, gancho 0x3a, esgueirar 0x3f, ícones de toque 0xfa) e pela água (salas com cel_tag::water_y);
// - aparelhos: as classes de aparelho do inventário inicial (Player_InitWeapon) e se os alvos deles na fase ainda
//   são provisórios (gancho: pontos 0x3a e ícones; câmera: CamSubject 0xfd; decodificador: cadeados 0x2e dos tipos
//   0 e 2; laser: cadeados do tipo 1; Q-Worm: PCQWorm 0x35);
// - objetivos: para cada canal de objetivo da fase (OBJECTIVE_tag::channel) e o que o revela (trigger), quem o liga:
//   os objetos portados (gatilhos, interruptores, fusíveis, script players, destrutíveis, itens, geradores, portas,
//   a morte de personagens) e os posicionamentos provisórios cujo parâmetro de canal de saída é ele;
// - stubs: as funções não portadas chamadas na carga e nos 2 s de jogo da fase (pending_call).
//
// Os bloqueios de cada fase entram num ranking (quantas fases cada um bloqueia), impresso no fim.

#include <cstdint>

namespace campaign {

// A fase é de uma missão a pé (está em MissionData).
bool is_campaign_level(uint32_t level);
// Antes da carga: guarda as contagens de pending_call.
void begin_level();
// Depois da carga: o que a carga criou, o inventário inicial e os objetos vivos (a linha é montada aqui, antes que
// os testes da fase mudem os objetos).
void after_load(uint32_t level);
// Logo antes e logo depois dos 2 s de jogo (run_game): os stubs chamados.
void before_game();
void after_game();
// " campanha: provisorios=N subestados=... aparelhos=... objetivos=... stubs_jogador=... bloqueios=..."; com detail,
// também a lista completa dos provisórios (função:quantos) e dos stubs da fase.
void print_line(uint32_t level, bool detail);
// O ranking dos bloqueios (quantas fases cada um bloqueia, e quais).
void print_summary();

}  // namespace campaign
