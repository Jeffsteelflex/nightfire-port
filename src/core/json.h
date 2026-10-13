#pragma once
// JSON mínimo (só do port): o hd_ui/map.json da camada 2D em alta definição e o hd_ui/design/tokens.json do
// estilo de menu Modern. Objetos guardam as chaves na ordem do arquivo; números são double.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nf {

struct JMember;

struct JVal {
    enum Type : uint8_t { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> a;
    // Os membros de um objeto, na ordem do arquivo. Não é um std::pair<std::string, JVal>: com a libstdc++ o pair de um
    // tipo ainda incompleto não compila no clang (os construtores condicionais do pair perguntam pelo JVal enquanto ele
    // é definido).
    std::vector<JMember> o;
    const JVal* get(const char* key) const;
};

struct JMember {
    JMember(std::string k, JVal v) : first(std::move(k)), second(std::move(v)) {}
    std::string first;
    JVal second;
};

inline const JVal* JVal::get(const char* key) const {
    if (type != Obj) return nullptr;
    for (const auto& [k, v] : o) {
        if (k == key) return &v;
    }
    return nullptr;
}

// Lê o texto inteiro; false com a mensagem (e a linha) em `error`.
bool json_parse(const std::string& text, JVal& out, std::string* error);

// UTF-8 -> UTF-16 (os textos do JSON para o DirectWrite).
std::wstring widen_utf8(const std::string& s);

}  // namespace nf
