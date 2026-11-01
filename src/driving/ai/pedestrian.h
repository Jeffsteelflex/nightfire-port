#pragma once
// Os pedestres das calçadas (o AICharacterPedestrian do DRIVING.ELF, AICharacterPassenger.obj; docs/driving.md seção
// 32): a parte que toda missão roda, o Init (do GameLoop_StartUp) e o Manage (a cada passo da IA).
//
// Como o PS2 faz: o Init escolhe o conjunto de pedestres da missão (GetPedBase: 1 snow1a, 2 snow2a, 3 junglea, 4
// jungleb, 5 uw_, 6 junglec, 0 as outras; a tabela PedSets de 0x3277b0, 0x70 bytes por conjunto: a contagem dos tipos,
// os 16 índices das descrições, a contagem dos especiais em +0x54 e os índices deles), troca a contagem dos tipos pelo
// NUM_PED_TYPES_PS2 da coleção "world" da pista quando ela o tem, guarda as ruas especiais (WRoadNetwork::
// GetSpecialRoads: o nó do fim de cada trecho e um ponto 10 m adiante) e, se o conjunto tem tipos ou especiais, cria os
// 16 pedestres (o AICharacterPedestrian, o Human, o ator, o WRoadNav no modo 2 das calçadas) e os recicla para os
// lugares de nascer; sem nenhum, fgNumPeds fica 0. O Manage conta o atraso da voz, guarda a posição do carro do jogador,
// e, nas ruas especiais, pede um pedestre especial quando o jogador chega a 130-250 m de uma (InvalidateFurthestPed e
// fgForceSpecialPed), mata o da rua que ficou a mais de 250 m e anda os pedestres.
//
// Paris não tem pedestres: o paris.atr (data\sim\attrib\world\paris.atr do MIS01.VIV) traz NUM_PED_TYPES_PS2=0 e o
// conjunto 0 não tem especiais, então o Init deixa fgNumPeds em 0 e o LoadPedTextures não carrega nenhum personagem; o
// Manage só conta e guarda posições. Portado do MIPS do PS2 o que Paris roda (o Init, o Manage, o InvalidateFurthestPed
// e o GetPedBase com zero pedestres); os pedestres de verdade (as 44 funções dos estados, o Recycle, o KillSelf, o
// AIRoadSpawn das calçadas, o modo 2 do navegador) ficam para as missões que os têm: o Init com algum tipo só conta
// (pedestrian_globals().unported). Conferido na parte 28 do nf_driving_check (tests/driving_check_fx_ped.cpp).

#include <cstdint>
#include <string>

namespace drv {

// Uma rua especial (0x18 bytes em 0x422460, até 32): o pedestre dela (-1 nenhum), o trecho, o fim (x, z) e o ponto
// 10 m adiante (x, z).
struct PedSpecialRoad {
    int32_t ped;
    int32_t segment;
    float x, z;
    float ahead_x, ahead_z;
};
static_assert(sizeof(PedSpecialRoad) == 0x18);

// Os globais dos pedestres (endereços do PS2).
struct PedestrianGlobals {
    int32_t enabled = 0;           // 0x3c8230 fgPedsEnabled
    int32_t ped_base = 0;          // 0x3c8234 fgPedBase
    int32_t num_peds = 0;          // 0x3c8238 fgNumPeds
    int32_t num_special = 0;       // 0x3c823c fgNumSpecialPedPoints
    int32_t force_special = -1;    // 0x3c8240 fgForceSpecialPed
    int32_t use_last_spawn = 0;    // 0x3c8244
    int32_t chat_allowed = 0;      // 0x3c8248
    int32_t last_spawn_type = 0;   // 0x3c824c
    uint8_t voice_delay = 0;       // 0x3c8250 (byte)
    int32_t invalidated = -1;      // 0x327ac4
    float player[3] = {0, 0, 0};   // 0x422760
    PedSpecialRoad special[32] = {};  // 0x422460
    // A tabela PedSets do .data (3 conjuntos de 0x70 bytes; o port a lê do DRIVING.ELF): a contagem dos tipos e a dos
    // especiais de cada um.
    int32_t set_count[7] = {16, 1, 6, 0, 0, 0, 0};
    int32_t set_special[7] = {0, 6, 0, 0, 0, 0, 0};
    uint64_t unported = 0;          // os Init com pedestres (o port não os cria)
    uint64_t manage_steps = 0;
    uint64_t guards = 0;
};
PedestrianGlobals& pedestrian_globals();

// [driving] 0x001266a8 AICharacterPedestrian::GetPedBase: o conjunto pelo nome da missão (0x1004f4).
int32_t AICharacterPedestrian_GetPedBase(const char* mission);
// [driving] 0x00125bb8 AICharacterPedestrian::Init(int): `mission` o nome da missão; `num_types` o NUM_PED_TYPES_PS2 da
// coleção "world" (`found` falso: sem o atributo). Sem a rede de estradas (WRoadNetwork::fValid) não faz nada.
void AICharacterPedestrian_Init(const char* mission, int32_t num_types, bool found);
// [driving] 0x001262f0 AICharacterPedestrian::Manage: `player` a posição do carro do jogador (PhysicsObject::
// GetPosition de Simulation +0xdcc[0]).
void AICharacterPedestrian_Manage(const float player[3]);
// [driving] 0x00126818 AICharacterPedestrian::InvalidateFurthestPed(int exceto): o mais longe reciclado; -1 nenhum.
int32_t AICharacterPedestrian_InvalidateFurthestPed(int32_t except);

}  // namespace drv
