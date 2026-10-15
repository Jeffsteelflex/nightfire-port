#pragma once
// Onde o tráfego nasce e quando um carro dele pode sumir (AIRoadSpawn do DRIVING.ELF, AIElementController.obj;
// docs/driving.md seção 18): a lista dos trechos de rua em volta do jogador onde um carro pode aparecer (até 50,
// RefreshSpawnData a refaz a cada 89 passos pela grade da pista), o sorteio de um lugar livre nela
// (GetRandomTrafficSpawn: um trecho com vaga na rua, uma faixa pela contagem, um t ao acaso, longe dos outros carros e a
// pelo menos `min_dist` do jogador) e as distâncias que liberam um carro para sumir ou nascer de novo.
//
// Portado do MIPS do PS2 (a verdade); só a parte do tráfego (a dos pedestres, GetRandomPedSpawn,
// GetPedSpawnPointOnSegment, PedRespawnAvailable e PedRespawnPosCheck, fica de fora: as missões a pé do carro não têm
// pedestres nos dados do PS2, NUM_PED_TYPES_PS2=0 em Paris).
//
// Os globais do EE são globais aqui (os endereços do PS2 nos comentários, para a parte 14 do nf_driving_check). O que
// vem de fora do tráfego: a matriz da câmera (o primeiro elemento da lista de câmeras do mundo, 0x333ed8: a linha 2 é a
// frente, a linha 3 a posição) e duas bandeiras do SMissionManager (+0x350: só nascer fora da vista da câmera; +0x3cc:
// sem tráfego), que os roteiros de missão mudam (não portados; zeradas como em SMissionManager::ClearState).

#include <cstdint>

namespace drv {

// Os globais do AIRoadSpawn (0x327d58..0x327d74, 0x3c8278, 0x423000).
struct RoadSpawnGlobals {
    uint32_t instance = 0;             // 0x327d58 fgRoadSpawn (o objeto de 1 byte do Init)
    int32_t num_segments = 0;          // 0x327d5c os trechos da lista
    uint8_t flag60 = 0;                // 0x327d60 (1 no Init e no Restart; não lido pelo tráfego)
    int32_t spawn_count = 0;           // 0x327d64 os nascimentos (escolhe a faixa)
    float min_dist = 0;                // 0x327d68 RefreshSpawnData: o mais perto do jogador (75)
    float max_dist = 0;                // 0x327d6c e o mais longe (150)
    int32_t forced = 0;                // 0x327d70 RefreshSpawnData forçado (pela câmera): tudo pode sumir
    int32_t refresh_step = 0;          // 0x3c8278 o passo da próxima RefreshSpawnData
    int32_t segments[50] = {};         // 0x423000 a lista (-1 no fim)
};
RoadSpawnGlobals& road_spawn();

// O que o tráfego lê de fora dele (ver o cabeçalho).
struct TrafficWorld {
    const float (*camera)[4] = nullptr;  // a matriz da câmera (nula: a da posição do jogador, sem frente)
    int32_t mission_camera_check = 0;    // SMissionManager +0x350
    int32_t mission_no_traffic = 0;      // SMissionManager +0x3cc
};
TrafficWorld& traffic_world();

// [driving] 0x0013c4d8 AIRoadSpawn::Init
void AIRoadSpawn_Init();
// [driving] 0x0013c558 AIRoadSpawn::Restart
void AIRoadSpawn_Restart();
// [driving] 0x0013c5b0 AIRoadSpawn::Shutdown
void AIRoadSpawn_Shutdown();
// [driving] 0x0013c5e8 AIRoadSpawn::GetTrafficSpawnPointOnSegment(short &, char &, float &, bool &): a faixa (pela
// contagem da rua e dos nascimentos), o t ao acaso e o sentido no trecho `segment`.
void AIRoadSpawn_GetTrafficSpawnPointOnSegment(const int16_t* segment, int8_t* lane, float* t, int32_t* forwards);
// [driving] 0x0013c798 AIRoadSpawn::TrafficRespawnPosCheck(COORD3 &, float): `pos` (com o raio do carro) está longe o
// bastante do jogador.
bool AIRoadSpawn_TrafficRespawnPosCheck(const float pos[3], float radius);
// [driving] 0x0013c860 AIRoadSpawn::TrafficRespawnAvailable(COORD3 &, float): um carro em `pos` pode sumir.
bool AIRoadSpawn_TrafficRespawnAvailable(const float pos[3]);
// [driving] 0x0013ca60 AIRoadSpawn::GetRandomTrafficSpawn(short &, char &, float &, bool &)
bool AIRoadSpawn_GetRandomTrafficSpawn(int16_t* segment, int8_t* lane, float* t, int32_t* forwards);
// [driving] 0x0013d618 AIRoadSpawn::RefreshSpawnData(bool, float, float)
void AIRoadSpawn_RefreshSpawnData(bool force, float min_dist, float max_dist);

// O WRoadNav estático do GetRandomTrafficSpawn (0x3c8560; a guarda em 0x327d74). Port: construído no primeiro uso.
struct WRoadNav;
WRoadNav& road_spawn_nav();
bool road_spawn_nav_built();

// Guardas do port: divisão por zero (um trecho sem faixas no GetTrafficSpawnPointOnSegment: o PS2 para com break 7).
uint64_t road_spawn_guards();

}  // namespace drv
