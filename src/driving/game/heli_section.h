#pragma once
// A seção do helicóptero de Paris e a passagem para a do carro (docs/driving.md seção 23): o que o porte do trilho
// (driving/game/rail.h) e da câmera (driving/camera/player_camera.h) não cobre.
//
// No PS2 o 'MSet' de Paris pede o veículo "helicopter" (SMissionManager::GetCarType): o GameLoop_StartUp cria o carro
// do jogador com ele (um PBondCar comum: helicopter.atr tem IS_HELICOPTER e SECONDARY_TYPE=vanquish; nenhuma física de
// helicóptero: o PHelicopter é o dos helicópteros da IA, driving/game/helicopter.h) e pré-carrega o modelo do
// SECONDARY_TYPE. O gatilho 685 põe o carro no trilho (o helicóptero voa pelo caminho 'path_Helicopter02') com a câmera
// da direção automática; no fim da seção o ECustomTransition("paris") troca o tipo do carro pelo secundário
// (PBondCar::ChangeCarType: o render, o som, o CarPhysics e o corpo rígido refeitos no mesmo lugar) e o
// EResetPlayerCarPos o põe na largada da seção do carro (o gatilho 683).
//
// Portado do MIPS do PS2, conferido na parte 19 do nf_driving_check. Divergências (port), também em cada função: o
// render (o RVehicle e o RSceneObj::Load) e o som (o APlayerVehicle) do tipo novo são do nf_driving (o gancho
// heli_change_car_hook); o resto do ECustomTransition (o SMissionManager::LoadWeaponSet com o 'MSet' mexido, o
// AICharacterPassenger::Init e o AICharacterBond::Init) é anotado (mission_trace) e contado.

#include <cstdint>
#include <functional>
#include <string>

namespace drv {

class AttributeCollection;
class BondCar;
struct CarDesc;
struct PendingEvent;

// [driving] 0x001932d0 PBondCar::GetSecondaryType: o +0x450 (o SECONDARY_TYPE do construtor; nulo sem).
const char* PBondCar_GetSecondaryType(const BondCar& car);
// O +0x450 do construtor (0x001863e4: AttributeSet::LookupString("SECONDARY_TYPE")), que o PBondCar_Construct do porte
// não grava: quem monta o carro chama (o texto fica guardado aqui).
void PBondCar_SetSecondaryType(BondCar& car, const AttributeCollection& attrs);

// O tipo novo de um ChangeCarType: os atributos e a descrição do modelo (o RSceneObj::GetBoundingDimensions dele).
struct CarTypeData {
    std::string name;
    const AttributeCollection* attrs = nullptr;
    const CarDesc* desc = nullptr;
};
// [driving] 0x00185e50 PBondCar::ChangeCarType(char *, bool): nada com o mesmo tipo; senão o nome, o CarPhysics e as
// chaves da coleção nova, o render e o som pelo gancho, InitializeCarVariables(false), ResetDamage e o corpo rígido
// refeito na mesma vaga (a posição, parado, de frente para o reset_direction, a massa e o tensor do tipo novo).
// Devolve se trocou.
bool PBondCar_ChangeCarType(BondCar& car, const CarTypeData& type, bool purge);

// O render e o som do tipo novo (o nf_driving: o modelo pré-carregado, o RVehicle, o APlayerVehicle).
std::function<void(BondCar& car, const CarTypeData& type)>& heli_change_car_hook();
// Os tipos que o ECustomTransition pode pedir (o nf_driving registra o secundário pré-carregado).
std::function<const CarTypeData*(const std::string& name)>& heli_car_type_lookup();

// [driving] 0x0014b020 ECustomTransition::~ECustomTransition (o evento 240): com "paris", o ChangeCarType do carro do
// jogador para o GetSecondaryType; devolve se o número é este.
bool heli_event_execute(const PendingEvent& e);

struct HeliSectionStats {
    uint64_t changes = 0, transitions = 0, guards = 0;
};
HeliSectionStats& heli_section_stats();

}  // namespace drv
