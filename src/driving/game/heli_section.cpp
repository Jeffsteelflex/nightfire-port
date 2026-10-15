// A seção do helicóptero e a passagem para a do carro (driving/game/heli_section.h), do MIPS do DRIVING.ELF. As
// desmontagens ficam em out/agent_drv_heli/ do checkout principal (fora do git).
//
// Divergências (port), também em cada função: o render e o som do tipo novo vêm do gancho (o nf_driving); o
// SMissionManager::LoadWeaponSet, o AICharacterPassenger::Init e o AICharacterBond::Init do ECustomTransition são
// anotados (mission_note_call) e contados.

#include "driving/game/heli_section.h"

#include <cstring>
#include <map>

#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/triggers.h"
#include "driving/physics/collision.h"
#include "driving/physics/simulation.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

HeliSectionStats g_stats;
// O texto do SECONDARY_TYPE de cada carro (o +0x450 aponta para cá).
std::map<const BondCar*, std::string>& secondary_names() {
    static std::map<const BondCar*, std::string> m;
    return m;
}

}  // namespace

HeliSectionStats& heli_section_stats() { return g_stats; }

std::function<void(BondCar& car, const CarTypeData& type)>& heli_change_car_hook() {
    static std::function<void(BondCar&, const CarTypeData&)> hook;
    return hook;
}

std::function<const CarTypeData*(const std::string& name)>& heli_car_type_lookup() {
    static std::function<const CarTypeData*(const std::string&)> fn;
    return fn;
}

// [driving] 0x001932d0 PBondCar::GetSecondaryType
const char* PBondCar_GetSecondaryType(const BondCar& car) {
    uint32_t p;
    std::memcpy(&p, car.d.unknown450, 4);
    return nf::p32_ptr<const char>(p);
}

void PBondCar_SetSecondaryType(BondCar& car, const AttributeCollection& attrs) {
    std::string v;
    uint32_t p = 0;
    if (attrs.find("SECONDARY_TYPE", v)) {
        std::string& s = secondary_names()[&car];
        s = v;
        p = nf::p32_u(s.c_str());
    }
    std::memcpy(car.d.unknown450, &p, 4);
}

// [driving] 0x00185e50 PBondCar::ChangeCarType
bool PBondCar_ChangeCarType(BondCar& car, const CarTypeData& type, bool purge) {
    (void)purge;  // o RSceneObj::PurgePreloaded do modelo antigo: o render é do gancho
    if (nf::str_icmp(car.name_.c_str(), type.name.c_str()) == 0) return false;
    if (!type.attrs || !type.desc) {
        ++g_stats.guards;
        return false;
    }
    PBondCarData& c = car.d;
    RigidBody* body = GetRigidBody(simulation(), c.slot);
    float pos[3];
    std::memcpy(pos, body->pos, 12);
    // AttributeSet::SetName e o +0x2cc; o CarPhysics da coleção nova (PVehicle::CarPhysicsAttrib) e as chaves que a física
    // lê (o PS2 as procura no AttributeSet na hora; o porte as guarda no carro, como o PBondCar_Construct).
    const AttributeCollection& attrs = *type.attrs;
    car.name_ = type.name;
    car.car_physics = attrs.car_physics();
    c.physics = nf::p32_u(&car.car_physics);
    car.boost_time = attrs.lookup_int("BOOST_TIME");
    car.boost_extra_acc = attrs.lookup_float("BOOST_EXTRA_ACC");
    car.lookup_mass = attrs.lookup_float("MASS");
    car.wheel_x_offset = attrs.lookup_float("CAR_WHEEL_X_OFFSET");
    car.wheel_zf_offset = attrs.lookup_float("CAR_WHEEL_ZF_OFFSET");
    car.wheel_zr_offset = attrs.lookup_float("CAR_WHEEL_ZR_OFFSET");
    car.is_police = attrs.lookup_bool("IS_POLICE");
    car.is_heli = attrs.lookup_bool("IS_HELI");
    car.has_siren = attrs.lookup_bool("HAS_SIREN");
    car.tyre_radius = attrs.lookup_float("TYRE_RADIUS");
    car.spring_rest_length_attr = attrs.lookup_float("SPRING_REST_LENGTH");
    car.tyre_damage_points_attr = uint32_t(attrs.lookup_int("TYRE_DAMAGE_POINTS"));
    // O RVehicle novo (RSceneObj::Load do modelo: a geometria de colisão e as medidas) e o APlayerVehicle novo.
    float dims[4];
    CarDesc_HalfExtents(*type.desc, dims);
    car.collision_points = type.desc->collision;
    car.collision_y_offset = ee::neg(CarDesc_RenderOffset(*type.desc));
    if (heli_change_car_hook()) heli_change_car_hook()(car, type);
    uint8_t* raw = reinterpret_cast<uint8_t*>(&c);
    raw[0x25a] = 0;
    PBondCar_InitializeCarVariables(car, false, car.tyre_damage_points_attr);
    PBondCar_ResetDamage(car);  // a entrada +0x19c da tabela virtual
    RigidBodyParams p;
    Util_GenerateMatrix(p.orient, c.reset_direction);
    const int32_t cls = c.car_class;
    p.type = cls < 0 ? 2 : cls <= 1 ? 1 : cls == 3 ? 3 : 2;
    p.mass = car.lookup_mass;
    p.slot = c.slot;
    std::memcpy(p.pos, pos, 12);
    std::memcpy(p.half_size, dims, 16);
    Util_GenerateCarTensor(p.inv_inertia, p.mass, dims[0], dims[1], dims[2]);
    p.owner = &car;
    p.moving = 1;
    p.flag2 = 0;
    if (body) RigidBody_Construct(body, p);
    ++g_stats.changes;
    return true;
}

// [driving] 0x0014b020 ECustomTransition::~ECustomTransition
bool heli_event_execute(const PendingEvent& e) {
    if (e.type != 240) return false;
    const char* name = nf::p32_ptr<const char>(e.obj[1]);
    if (name && nf::str_icmp(name, "paris") == 0) {
        ++g_stats.transitions;
        BondCar* car = rail_player_car();
        const char* sec = car ? PBondCar_GetSecondaryType(*car) : nullptr;
        const CarTypeData* t = (sec && heli_car_type_lookup()) ? heli_car_type_lookup()(sec) : nullptr;
        if (car && t) PBondCar_ChangeCarType(*car, *t, false);
        else ++g_stats.guards;
        // O 'MSet' copiado, as armas e os personagens com ele mexido (o +0x2c..+0x4b, o +0x4c ou o +0x54), e o 'MSet'
        // de volta: anotados.
        mission_note_call(0x205498);  // SMissionManager::LoadWeaponSet
        mission_note_call(0x125438);  // AICharacterPassenger::Init
        mission_note_call(0x119e20);  // AICharacterBond::Init
    }
    return true;
}

}  // namespace drv
