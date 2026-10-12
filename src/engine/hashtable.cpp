// Port do módulo hashtable do ACTION.ELF. Estado global como no original:
//   t_hashtable    (0x0031cbe0)  pares {chave, valor}, ordenados por chave
//   m_nhti                       número de entradas em uso (inclui as 2 sentinelas)
//   HashTypeRanges (0x00324be0)  {lo, hi} por tipo (byte alto da chave), 8 tipos
//   ht_insert_ind                posição de inserção deixada pela última busca sem sucesso
//
// Comportamentos do original mantidos de propósito:
//   - depois de inserir ou remover, só as faixas dos tipos 0 a 6 voltam para [0, n-1];
//     a do tipo 7 fica como estava até o próximo hashtable_create_sections;
//   - inserir uma chave que já existe insere outra cópia na posição deixada pela busca anterior.
// Divergências (o original corromperia memória):
//   - a faixa de busca é limitada ao tamanho atual da tabela. Como hashtable_initialise não reseta a
//     faixa do tipo 7, ela pode ficar maior que a tabela depois de uma reinicialização; o original
//     então busca além do fim e, no additem, faz memmove com tamanho negativo;
//   - remover uma chave inexistente não faz nada (o original faz memmove a partir de NULL);
//   - inserir com a tabela cheia não faz nada (o original escreve por cima de HashTypeRanges).

#include "engine/hashtable.h"
#include "engine/view.h"

#include <cstring>

#include "engine/object.h"

namespace {

constexpr uint32_t kCapacity = 4096;  // (0x00324be0 - 0x0031cbe0) / 8

struct Entry {
    uint32_t key;
    const void* value;
};

struct Range {
    uint16_t lo, hi;
};

Entry t_hashtable[kCapacity];
uint32_t m_nhti = 0;
Range HashTypeRanges[8];
uint32_t ht_insert_ind = 0;

uint32_t type_of(uint32_t hash) {
    const uint32_t t = hash >> 24;
    return t > 7 ? 0 : t;
}

// Busca binária dentro da faixa do tipo. Devolve o índice ou -1; em caso de falha grava
// ht_insert_ind com a posição de inserção (lo), como o original.
int find(uint32_t hash) {
    const Range r = HashTypeRanges[type_of(hash)];
    uint32_t lo = r.lo, hi = r.hi;
    if (hi >= m_nhti) hi = m_nhti - 1;  // divergência: ver o comentário do arquivo
    if (lo > hi) lo = 0;
    uint32_t mid = lo + hi;
    do {
        mid >>= 1;
        if (hash < t_hashtable[mid].key) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
            if (hash <= t_hashtable[mid].key) return int(mid);
        }
        mid = lo + hi;
    } while (lo <= hi);
    ht_insert_ind = lo;
    return -1;
}

void reset_ranges() {
    for (uint32_t t = 0; t < 7; ++t) {  // 7, não 8: ver o comentário do arquivo
        HashTypeRanges[t].lo = 0;
        HashTypeRanges[t].hi = uint16_t(m_nhti - 1);
    }
}

}  // namespace

// [decomp] 0x001ca4b8 hashtable_initialise
void hashtable_initialise() {
    m_nhti = 2;
    t_hashtable[0] = {0, nullptr};
    t_hashtable[1] = {0xFFFFFFFF, nullptr};
    ht_insert_ind = 0;
    for (uint32_t t = 0; t < 7; ++t) HashTypeRanges[t] = {0, 1};
}

// [decomp] 0x001ca2a8 hashtable_create_sections
// Para cada tipo, a faixa vai do vizinho anterior ao primeiro item do tipo até o vizinho seguinte
// ao último, para que a busca binária sempre tenha limites válidos.
void hashtable_create_sections() {
    const uint32_t n = m_nhti;
    for (uint32_t t = 0; t < 8; ++t) {
        HashTypeRanges[t].lo = 0;
        HashTypeRanges[t].hi = uint16_t(n - 1);
        if (n <= 1) continue;
        for (uint32_t i = 1; i < n; ++i) {
            if ((t << 24) != (t_hashtable[i].key & 0xFF000000)) continue;
            HashTypeRanges[t].lo = uint16_t(i - 1);
            uint32_t j = i;
            uint32_t next;
            do {
                next = (j + 1 < kCapacity) ? t_hashtable[j + 1].key : 0xFFFFFFFF;
                HashTypeRanges[t].hi = uint16_t((n - 1 <= j) ? j : j + 1);
                ++j;
            } while ((t << 24) == (next & 0xFF000000));
            break;
        }
    }
}

// [decomp] 0x001ca060 hashtable_additem
void hashtable_additem(uint32_t hash, const void* item) {
    find(hash);  // em caso de acerto, ht_insert_ind fica com o valor anterior (como no original)
    if (m_nhti >= kCapacity) return;
    const uint32_t ins = ht_insert_ind;
    std::memmove(&t_hashtable[ins + 1], &t_hashtable[ins], (m_nhti - ins) * sizeof(Entry));
    ++m_nhti;
    t_hashtable[ins] = {hash, item};
    reset_ranges();
}

// [decomp] 0x001ca1b0 hashtable_deleteitem
void hashtable_deleteitem(uint32_t hash) {
    const int d = find(hash);
    if (d < 0) return;
    --m_nhti;
    std::memmove(&t_hashtable[d], &t_hashtable[d + 1], (m_nhti - uint32_t(d)) * sizeof(Entry));
    reset_ranges();
}

// [decomp] 0x001ca5b0 hashtable_getitem
void* hashtable_getitem(uint32_t hash) {
    const int i = find(hash);
    return i < 0 ? nullptr : const_cast<void*>(t_hashtable[i].value);
}

// [decomp] 0x001ca798 hashtable_hashcode_to_celglist
// A mesma busca de hashtable_getitem, com 0xffffffff valendo "nenhum".
celglist_tag* hashtable_hashcode_to_celglist(uint32_t hash) {
    if (hash == 0xffffffffu) return nullptr;
    const int i = find(hash);
    return i < 0 ? nullptr : static_cast<celglist_tag*>(const_cast<void*>(t_hashtable[i].value));
}

// [decomp] 0x001ca840 hashtable_celglist_to_hashcode
// Busca linear pelo valor, sem as sentinelas (entradas 1 a m_nhti - 2). Divergência: com a
// tabela vazia o original percorreria memória além dela (m_nhti - 1 sem sinal).
uint32_t hashtable_celglist_to_hashcode(const celglist_tag* glist) {
    if (glist == nullptr || m_nhti < 2) return 0;
    for (uint32_t i = 1; i < m_nhti - 1; ++i) {
        if (t_hashtable[i].value == glist) return t_hashtable[i].key;
    }
    return 0;
}

// [decomp] 0x001ca6d8 hashtable_set_object_to_entity_gfx
void hashtable_set_object_to_entity_gfx(obj_tag* obj, uint32_t hash) {
    if (hash == 0xffffffffu) return;
    const int i = find(hash);
    celglist_tag* g = i < 0 ? nullptr : static_cast<celglist_tag*>(const_cast<void*>(t_hashtable[i].value));
    if (g != nullptr) Control_SetGList(obj, g);
}

// [decomp] 0x001ca890 hashtable_set_sub_object_to_entity_gfx
// O modelo de um anexo (marca de tiro): como hashtable_set_object_to_entity_gfx, mas só troca o
// glist do anexo.
void hashtable_set_sub_object_to_entity_gfx(ATTACH_tag* sub, uint32_t hash) {
    if (hash == 0xffffffffu) return;
    const int i = find(hash);
    celglist_tag* g = i < 0 ? nullptr : static_cast<celglist_tag*>(const_cast<void*>(t_hashtable[i].value));
    if (g != nullptr) sub->glist = g;
}

// [decomp] 0x001ca648 hashtable_modify
void hashtable_modify(uint32_t hash, const void* item) {
    const int i = find(hash);
    if (i >= 0) t_hashtable[i].value = item;
}

// [decomp] 0x001ca520 hashtable_delete_hashtype
// Percorre de trás para frente (entradas n-1 até 1), então as remoções não pulam itens.
void hashtable_delete_hashtype(uint32_t hash_type) {
    if (m_nhti - 1 != 0) {
        for (uint32_t i = m_nhti - 1; i != 0; --i) {
            if (i < m_nhti && (t_hashtable[i].key & 0xFF000000) == (hash_type & 0xFF000000)) {
                hashtable_deleteitem(t_hashtable[i].key);
            }
        }
    }
    hashtable_create_sections();
}

// [decomp] 0x001ca938 hashtable_get_hashtype_count
// Conta as entradas 1 a n-2 (fora as sentinelas) do tipo pedido.
int hashtable_get_hashtype_count(uint32_t hash_type) {
    int count = 0;
    for (uint32_t i = 1; i + 1 < m_nhti; ++i) {
        if ((t_hashtable[i].key & 0xFF000000) == (hash_type & 0xFF000000)) ++count;
    }
    return count;
}

// [decomp] 0x001ca9a0 hashtable_get_hashtype_data
void hashtable_get_hashtype_data(uint32_t hash_type, uint32_t* out) {
    for (uint32_t i = 1; i + 1 < m_nhti; ++i) {
        if ((t_hashtable[i].key & 0xFF000000) == (hash_type & 0xFF000000)) *out++ = t_hashtable[i].key;
    }
}

uint32_t hashtable_debug_size() { return m_nhti; }

void hashtable_debug_power_on() {
    std::memset(t_hashtable, 0, sizeof(t_hashtable));
    std::memset(HashTypeRanges, 0, sizeof(HashTypeRanges));
    m_nhti = 0;
    ht_insert_ind = 0;
}
