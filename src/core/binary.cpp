#include "core/binary.h"

#include <filesystem>
#include <fstream>

namespace nf {

std::string cstring(ByteView data, size_t offset, size_t max_len) {
    std::string out;
    for (size_t i = offset; i < data.size() && out.size() < max_len; ++i) {
        if (data[i] == 0) break;
        out.push_back(static_cast<char>(data[i]));
    }
    return out;
}

static std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

Bytes read_file(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!f) throw FormatError("não consegui abrir " + narrow(path));
    const auto size = static_cast<size_t>(f.tellg());
    Bytes data(size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!f) throw FormatError("leitura incompleta de " + narrow(path));
    return data;
}

Bytes read_file_range(const std::wstring& path, uint64_t offset, uint64_t size) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) throw FormatError("não consegui abrir " + narrow(path));
    f.seekg(static_cast<std::streamoff>(offset));
    Bytes data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (!f) throw FormatError("leitura incompleta de " + narrow(path));
    return data;
}

}  // namespace nf
