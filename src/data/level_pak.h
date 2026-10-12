#pragma once
// Pacote de fase (07xxxxxx.bin): ver docs/formats.md, "Pacotes de fase".

#include <string>
#include <vector>

#include "core/binary.h"

namespace nf {

enum class ItemType : uint8_t {
    Map = 0,
    MapMain = 1,
    Map2 = 2,
    SkinData = 3,
    Anim = 4,
    AnimScript = 5,
    Skeleton = 6,
    Script = 7,
    Menu = 8,
    MapAltMem = 11,
    Loadable = 12,
    MemcardIcon = 15,
    Woman = 16,
};

struct PakItem {
    uint32_t offset = 0;
    uint32_t size = 0;
    ItemType type{};
    std::string name;  // hashcode em hexa ("01000093") ou nome ("SKEL0002")

    ByteView view(ByteView pak) const { return sub(pak, offset, size); }
    bool is_map() const {
        return type == ItemType::Map || type == ItemType::MapMain || type == ItemType::Map2 ||
               type == ItemType::MapAltMem;
    }
};

std::vector<PakItem> parse_pak(ByteView pak);

}  // namespace nf
