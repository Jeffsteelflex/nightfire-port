// O sistema de atributos (driving/game/attributes.h).

#include "driving/game/attributes.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "driving/bigf.h"

namespace drv {

namespace {

// Os campos da estrutura "CarPhysics" da classe "pvehicle" (PVehicle::InitializeGlobals 0x1968f8: a chave, o
// deslocamento e o analisador; 0x16c010 = inteiro).
struct CarField {
    const char* key;
    uint32_t offset;
    bool is_int;
};
const CarField kCarFields[] = {
    {"SPEED_CUTOFF_REVERSE", 0x08, false}, {"SPEED_CUTOFF_RATE_REVERSE", 0x0c, false},
    {"MAXACC", 0x14, false}, {"MAXREVACC", 0x18, false}, {"MAXBRAKE", 0x1c, false}, {"MAXSTEERING", 0x20, false},
    {"MODSTEERSPEED", 0x24, false}, {"MINSTEER", 0x28, false}, {"FRIC_MOD_SPEED", 0x2c, false},
    {"FRIC_MOD_RANGE", 0x30, false}, {"FRIC_MOD_MIN", 0x34, false}, {"COUNTER_STEER_SCALE", 0xb0, false},
    {"COUNTER_STEER_SCALE_LIMIT", 0xb4, false}, {"GEAR_LIMITR", 0xd0, false}, {"GEAR_LIMIT1", 0xd4, false},
    {"GEAR_LIMIT2", 0xd8, false}, {"GEAR_LIMIT3", 0xdc, false}, {"GEAR_LIMIT4", 0xe0, false},
    {"GEAR_LIMIT5", 0xe4, false}, {"GEAR_RATIOR", 0xe8, false}, {"GEAR_RATIO1", 0xec, false},
    {"GEAR_RATIO2", 0xf0, false}, {"GEAR_RATIO3", 0xf4, false}, {"GEAR_RATIO4", 0xf8, false},
    {"GEAR_RATIO5", 0xfc, false}, {"FRICTIONLIMITFRONT", 0x40, false}, {"FRICTIONLIMITREAR", 0x44, false},
    {"TYREGRIPFACTOR", 0x48, false}, {"NOSE_JUMP_ANGLE", 0x3c, false}, {"NUM_BLEND_STEPS", 0xb8, true},
    {"SLOPE_SCALE", 0x4c, false}, {"SPRING_STIFFNESS_FRONT", 0x50, false}, {"SPRING_STIFFNESS_REAR", 0x54, false},
    {"SPRING_DAMPING_FRONT", 0x58, false}, {"SPRING_DAMPING_REAR", 0x5c, false},
    {"SPRING_REST_LENGTH", 0x60, false}, {"SPRING_COMPRESSION_LIMIT", 0x64, false},
    {"SPRING_COMPRESSION_LIMIT_DRAW", 0x68, false}, {"WHEEL_RADIUS", 0x6c, false},
    {"BRAKING_FRICTION", 0x70, false}, {"ACCEL_FRW_RATIO", 0x74, false}, {"COAST_FRW_RATIO", 0x78, false},
    {"BRAKE_FRW_RATIO", 0x7c, false}, {"REVERSE_FRW_RATIO", 0x80, false}, {"HANDBRAKE_FRW_RATIO", 0x84, false},
    {"REV_HANDBRAKE_FRW_RATIO", 0x88, false}, {"HANDBRAKE_FORCE", 0x98, false},
    {"FRONT_SLIPPING_SCALE", 0x8c, false}, {"REAR_SLIPPING_SCALE", 0x90, false}, {"SLIPPING_RANGE", 0x94, false},
    {"TOTAL_HANDBRAKE_FRICTION_SCALE", 0x9c, false}, {"HANDBRAKE_RW_FRIC_SCALE", 0xa0, false},
    {"WHEELSPINSCALE", 0xa4, false}, {"WS_SPEED", 0xa8, false}, {"WS_ACCEL", 0xac, false},
    {"WHEEL_FORCE_APP_SCALE", 0x10, false}, {"YAW_STABILITY_FACTOR", 0x38, false}, {"IS_4X4", 0x00, true},
    {"IS_RALLY", 0x04, true}, {"IS_SUB", 0xbc, true}, {"IS_SNOWMOBILE", 0xc8, true},
    {"NO_WORLD_COLLISIONS", 0xc4, true}, {"SUB_PHYSICS", 0xc0, true}, {"IS_BOAT", 0xcc, true},
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool same_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// As linhas de uma seção do INI (DAFI_setsection: o nome sem diferença de caixa).
std::vector<std::pair<std::string, std::string>> section_lines(const std::string& text, const std::string& section) {
    std::vector<std::pair<std::string, std::string>> out;
    bool in = false, found = false;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = trim(text.substr(p, e - p));
        p = e + 1;
        if (!line.empty() && line[0] == '[') {
            const size_t close = line.find(']');
            const std::string name = line.substr(1, close == std::string::npos ? std::string::npos : close - 1);
            in = same_ci(name, section) && !found;
            if (in) found = true;
            continue;
        }
        if (!in) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out.emplace_back(trim(line.substr(0, eq)), line.substr(eq + 1));
    }
    return out;
}

}  // namespace

bool AttributeCollection::load(const Bigf& arc, const std::string& cls, const std::string& name,
                               const std::string& track_section, std::string* error) {
    name_ = name;
    values_.clear();
    has_physics_ = false;
    physics_ = CarPhysics{};
    parent_.reset();
    if (!same_ci(name, "default")) {
        parent_ = std::make_shared<AttributeCollection>();
        parent_->load(arc, cls, "default", track_section, nullptr);
    }
    const std::string key = "data\\sim\\attrib\\" + cls + "\\" + name + ".atr";
    const int idx = arc.find(key);
    if (idx < 0) {
        // Um nome que a classe não lista: uma coleção vazia (só a mãe).
        if (error) *error = "sem " + key;
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!arc.read(idx, bytes)) {
        if (error) *error = "não leu " + key;
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    std::vector<std::string> sections;
    if (!track_section.empty()) sections.push_back(track_section);
    if (!same_ci(name, "default")) sections.push_back(name);
    sections.push_back("default");
    for (const std::string& sec : sections) {
        for (const auto& [raw_key, value] : section_lines(text, sec)) {
            std::string k = raw_key;
            if (k.size() > 2 && k[k.size() - 2] == '.') k.resize(k.size() - 2);  // o sufixo de tipo
            if (k.empty()) continue;
            values_[k] = value;
            if (cls != "pvehicle") continue;
            for (const CarField& f : kCarFields) {
                if (k != f.key) continue;
                uint8_t* base = reinterpret_cast<uint8_t*>(&physics_);
                if (f.is_int) {
                    const int32_t v = int32_t(std::strtol(value.c_str(), nullptr, 10));
                    std::memcpy(base + f.offset, &v, 4);
                } else {
                    const float v = float(std::strtod(value.c_str(), nullptr));
                    std::memcpy(base + f.offset, &v, 4);
                }
                has_physics_ = true;
            }
        }
    }
    return true;
}

bool AttributeCollection::find(const std::string& key, std::string& value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        value = it->second;
        return true;
    }
    return parent_ ? parent_->find(key, value) : false;
}

float AttributeCollection::lookup_float(const std::string& key, bool* found) const {
    std::string v;
    const bool ok = find(key, v);
    if (found) *found = ok;
    return ok ? float(std::strtod(v.c_str(), nullptr)) : 0.0f;
}

int32_t AttributeCollection::lookup_int(const std::string& key, bool* found) const {
    std::string v;
    const bool ok = find(key, v);
    if (found) *found = ok;
    return ok ? int32_t(std::strtol(v.c_str(), nullptr, 10)) : 0;
}

bool AttributeCollection::lookup_bool(const std::string& key, bool* found) const {
    std::string v;
    const bool ok = find(key, v);
    if (found) *found = ok;
    if (!ok) return false;
    const std::string t = trim(v);
    return same_ci(t, "true") || t == "1";
}

const CarPhysics& AttributeCollection::car_physics() const {
    if (has_physics_ || !parent_) return physics_;
    return parent_->car_physics();
}

}  // namespace drv
