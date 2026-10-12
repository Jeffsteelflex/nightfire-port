// Registro de chamadas a stubs. Ver pending.h.

#include "platform/pending.h"

#include <unordered_map>

namespace {
std::map<std::string, int>& counts() {
    static std::map<std::string, int> c;
    return c;
}
// Os stubs do desenho são chamados a cada quadro: um atalho pelo endereço do nome evita montar a
// std::string toda vez (os nós do std::map não mudam de lugar).
std::unordered_map<const char*, int*>& by_pointer() {
    static std::unordered_map<const char*, int*> c;
    return c;
}
}  // namespace

void pending_call(const char* name) {
    int*& slot = by_pointer()[name];
    if (slot == nullptr) slot = &counts()[name];
    ++*slot;
}
const std::map<std::string, int>& pending_counts() { return counts(); }
void pending_reset() {
    by_pointer().clear();
    counts().clear();
}
void pending_restore(const std::map<std::string, int>& saved) {
    by_pointer().clear();
    counts() = saved;
}
