#pragma once
// As armas dos motoristas da IA (docs/driving.md seções 21 e 24): o AIGroundVehicle::DoWeaponUpdate (0x00130050) pede a
// torre (TurnTurret, 0x0012f170) e o tiro do carro com metralhadora (FireBullets, 0x0012f730), foguetes (FireRockets,
// 0x0012fa28) ou mísseis (FireMissiles, 0x0012fd58), portados em ground_vehicle_agent.cpp. O tiro não cria o projétil:
// como no carro do jogador, é um estímulo na animação do sistema da arma (o RAnimEngine::Handle do RSceneObj do carro,
// +0x28), cuja chave cria o EFireWeapon (game/fire_weapon.cpp). O ClearShot (0x0012f530) diz se nenhum agente amigo está
// na linha do tiro.
//
// Os ganchos (`ai_weapon_hooks()`, vazios no jogo) trocam cada uma das quatro funções por outra (a parte 17 do
// nf_driving_check só conta as chamadas, como o original com os ganchos dela); `ai_weapon_anim()` são as chamadas ao
// RAnimEngine (os padrões são as do porte; a parte 20 as anota).

#include <cstdint>
#include <functional>

namespace drv {

struct AIGroundVehicle;

struct AIWeaponHooks {
    std::function<void(AIGroundVehicle& v)> fire_bullets, fire_rockets, fire_missiles;
    std::function<void(AIGroundVehicle& v)> turn_turret;
};
AIWeaponHooks& ai_weapon_hooks();

// As chamadas ao RAnimEngine::Handle das armas (o handle é o +0x28 do RSceneObj do carro, a palavra do PS2).
struct AIWeaponAnim {
    std::function<void(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t steps, uint32_t mode)> stimulus;
    std::function<bool(uint32_t handle, uint32_t system)> is_playing;
    std::function<void(uint32_t handle, uint32_t system, uint32_t stimulus, uint32_t frame)> set_frame;
};
AIWeaponAnim& ai_weapon_anim();

struct AIWeaponStats {
    uint64_t bullets = 0, rockets = 0, missiles = 0, turret = 0;
    // Os estímulos de tiro (10) dados e os vetados (sem RSceneObj ou handle, o sistema ainda tocando).
    uint64_t shots = 0, no_handle = 0, busy = 0, blocked = 0, guards = 0;
};
AIWeaponStats& ai_weapon_stats();

// [driving] 0x0012f530 ClearShot(float distância, short vaga, COORD4 &frente, COORD4 &posição): falso se algum agente
// (um carro com motorista no estado 5) está à frente (o cosseno acima de 0,99) e mais perto que a distância.
bool ClearShot(float distance, int16_t slot, const float forward[4], const float position[4]);
// [driving] 0x0012f170 AIGroundVehicle::TurnTurret
void AIGroundVehicle_TurnTurret(AIGroundVehicle& v);
// [driving] 0x0012f730 AIGroundVehicle::FireBullets
void AIGroundVehicle_FireBullets(AIGroundVehicle& v);
// [driving] 0x0012fa28 AIGroundVehicle::FireRockets
void AIGroundVehicle_FireRockets(AIGroundVehicle& v);
// [driving] 0x0012fd58 AIGroundVehicle::FireMissiles
void AIGroundVehicle_FireMissiles(AIGroundVehicle& v);

}  // namespace drv
