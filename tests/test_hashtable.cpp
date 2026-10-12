// Testes do port da tabela de hashcodes (src/engine/hashtable.cpp).

#include <cstdint>

#include "engine/hashtable.h"
#include "test_framework.h"

namespace {
int values[64];
}

TEST(hashtable_add_get_modify) {
    hashtable_debug_power_on();
    hashtable_initialise();
    const uint32_t keys[] = {0x02000644, 0x01000093, 0x04000425, 0x020001e1, 0x05000040, 0x06000087};
    for (int i = 0; i < 6; ++i) hashtable_additem(keys[i], &values[i]);
    hashtable_create_sections();
    for (int i = 0; i < 6; ++i) CHECK(hashtable_getitem(keys[i]) == &values[i]);
    CHECK(hashtable_getitem(0x02000645) == nullptr);
    hashtable_modify(0x04000425, &values[10]);
    CHECK(hashtable_getitem(0x04000425) == &values[10]);
    CHECK(hashtable_debug_size() == 8);  // 6 itens + 2 sentinelas
}

TEST(hashtable_lookup_works_without_sections_for_types_0_to_6) {
    hashtable_debug_power_on();
    hashtable_initialise();
    hashtable_additem(0x02000001, &values[0]);
    hashtable_additem(0x02000003, &values[1]);
    hashtable_additem(0x02000002, &values[2]);
    // additem deixa as faixas dos tipos 0-6 em [0, n-1]: a busca funciona sem create_sections.
    CHECK(hashtable_getitem(0x02000002) == &values[2]);
    CHECK(hashtable_getitem(0x02000003) == &values[1]);
}

TEST(hashtable_type7_needs_create_sections) {
    // Comportamento do original: a faixa do tipo 7 (pacotes de fase, 07xxxxxx) não é atualizada por
    // additem, então a busca só acha o item depois de hashtable_create_sections.
    hashtable_debug_power_on();
    hashtable_initialise();
    hashtable_additem(0x07000001, &values[0]);
    CHECK(hashtable_getitem(0x07000001) == nullptr);
    hashtable_create_sections();
    CHECK(hashtable_getitem(0x07000001) == &values[0]);
}

TEST(hashtable_delete_hashtype_and_counts) {
    hashtable_debug_power_on();
    hashtable_initialise();
    for (int i = 0; i < 10; ++i) hashtable_additem(0x04000100 + i, &values[i]);
    for (int i = 0; i < 5; ++i) hashtable_additem(0x02000100 + i, &values[20 + i]);
    hashtable_create_sections();
    CHECK(hashtable_get_hashtype_count(0x04000000) == 10);
    CHECK(hashtable_get_hashtype_count(0x02000000) == 5);
    uint32_t keys[16] = {};
    hashtable_get_hashtype_data(0x02000000, keys);
    CHECK(keys[0] == 0x02000100 && keys[4] == 0x02000104);
    hashtable_delete_hashtype(0x04000000);
    CHECK(hashtable_get_hashtype_count(0x04000000) == 0);
    CHECK(hashtable_get_hashtype_count(0x02000000) == 5);
    CHECK(hashtable_getitem(0x02000103) == &values[23]);
    CHECK(hashtable_getitem(0x04000105) == nullptr);
    CHECK(hashtable_debug_size() == 7);
}

TEST(hashtable_stale_type7_range_is_clamped) {
    // Cenário que derrubaria o original: tabela grande, create_sections, reinicialização e inserção.
    hashtable_debug_power_on();
    hashtable_initialise();
    for (int i = 0; i < 20; ++i) hashtable_additem(0x02000100 + i, &values[i]);
    hashtable_create_sections();  // faixa do tipo 7 vai para [0, 21]
    hashtable_initialise();       // não reseta a faixa do tipo 7
    hashtable_additem(0x07000001, &values[0]);
    hashtable_create_sections();
    CHECK(hashtable_getitem(0x07000001) == &values[0]);
    CHECK(hashtable_debug_size() == 3);
}

TEST(hashtable_delete_missing_is_noop) {
    hashtable_debug_power_on();
    hashtable_initialise();
    hashtable_additem(0x02000001, &values[0]);
    hashtable_deleteitem(0x02000099);  // o original faria memmove a partir de NULL
    CHECK(hashtable_debug_size() == 3);
    hashtable_deleteitem(0x02000001);
    CHECK(hashtable_debug_size() == 2);
    CHECK(hashtable_getitem(0x02000001) == nullptr);
}

TEST(hashtable_many_items_stay_sorted) {
    hashtable_debug_power_on();
    hashtable_initialise();
    // Chaves únicas em ordem embaralhada: multiplicar por um ímpar é uma bijeção módulo 2^24.
    static int big[3000];
    const auto key = [](uint32_t i) { return 0x02000000u | ((i * 2654435761u) & 0x00FFFFFFu); };
    for (uint32_t i = 0; i < 3000; ++i) hashtable_additem(key(i), &big[i]);
    hashtable_create_sections();
    int found = 0;
    for (uint32_t i = 0; i < 3000; ++i) {
        if (hashtable_getitem(key(i)) == &big[i]) ++found;
    }
    CHECK(found == 3000);
}
