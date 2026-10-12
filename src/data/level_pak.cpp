#include "data/level_pak.h"

namespace nf {

std::vector<PakItem> parse_pak(ByteView pak) {
    std::vector<PakItem> items;
    if (pak.size() < 0x800) return items;
    const uint32_t table_size = u32(pak, 0);
    const uint32_t count = u32(pak, 4);
    size_t pos = 0x800;
    uint32_t data = 0x800 + table_size;
    for (uint32_t i = 0; i < count; ++i) {
        PakItem it;
        it.size = u32(pak, pos);
        it.type = static_cast<ItemType>(u8(pak, pos + 4));
        it.name = cstring(pak, pos + 5, 64);
        pos += 5 + it.name.size() + 1;
        it.offset = data;
        data += it.size;
        items.push_back(std::move(it));
    }
    if (data != pak.size()) throw FormatError("tamanhos dos itens não fecham com o pacote");
    return items;
}

}  // namespace nf
