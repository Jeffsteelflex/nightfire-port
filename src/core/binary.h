#pragma once
// Leitura de dados little-endian com verificação de limites.

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace nf {

using Bytes = std::vector<uint8_t>;
using ByteView = std::span<const uint8_t>;

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template <typename T>
T read(ByteView data, size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) {
        throw FormatError("leitura fora do buffer no offset " + std::to_string(offset));
    }
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

inline uint8_t u8(ByteView d, size_t o) { return read<uint8_t>(d, o); }
inline uint16_t u16(ByteView d, size_t o) { return read<uint16_t>(d, o); }
inline uint32_t u32(ByteView d, size_t o) { return read<uint32_t>(d, o); }
inline int32_t i32(ByteView d, size_t o) { return read<int32_t>(d, o); }
inline int16_t i16(ByteView d, size_t o) { return read<int16_t>(d, o); }
inline uint64_t u64(ByteView d, size_t o) { return read<uint64_t>(d, o); }
inline float f32(ByteView d, size_t o) { return read<float>(d, o); }

inline ByteView sub(ByteView d, size_t offset, size_t size) {
    if (offset > d.size() || size > d.size() - offset) {
        throw FormatError("subfaixa fora do buffer no offset " + std::to_string(offset));
    }
    return d.subspan(offset, size);
}

// Lê uma string terminada em zero a partir de offset.
std::string cstring(ByteView data, size_t offset, size_t max_len = 256);

Bytes read_file(const std::wstring& path);
Bytes read_file_range(const std::wstring& path, uint64_t offset, uint64_t size);

}  // namespace nf
