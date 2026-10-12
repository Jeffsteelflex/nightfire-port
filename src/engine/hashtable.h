#pragma once
// Tabela global de hashcodes da EuroLand (módulo hashtable do ACTION.ELF, 0x001ca060-0x001caa40).
//
// Liga hashcodes de 32 bits (byte alto = tipo: 01 mapa, 02 entidade, 04 animação, 05 skin,
// 06 script...) a ponteiros. É um array ordenado de até 4096 pares (t_hashtable), com duas
// sentinelas fixas (chave 0 no início e 0xFFFFFFFF no fim), e uma faixa de busca por tipo
// (HashTypeRanges) para encurtar a busca binária.

#include <cstdint>

void hashtable_initialise();
void hashtable_create_sections();
void hashtable_additem(uint32_t hash, const void* item);
void hashtable_deleteitem(uint32_t hash);
void* hashtable_getitem(uint32_t hash);
void hashtable_modify(uint32_t hash, const void* item);
struct celglist_tag;
struct obj_tag;
struct ATTACH_tag;
celglist_tag* hashtable_hashcode_to_celglist(uint32_t hash);
uint32_t hashtable_celglist_to_hashcode(const celglist_tag* glist);
void hashtable_set_object_to_entity_gfx(obj_tag* obj, uint32_t hash);
void hashtable_set_sub_object_to_entity_gfx(ATTACH_tag* sub, uint32_t hash);
void hashtable_delete_hashtype(uint32_t hash_type);
int hashtable_get_hashtype_count(uint32_t hash_type);
void hashtable_get_hashtype_data(uint32_t hash_type, uint32_t* out);

// Só para testes e depuração: número de entradas, incluindo as duas sentinelas (m_nhti).
uint32_t hashtable_debug_size();
// Só para testes: zera todo o estado, como o BSS ao ligar o console.
void hashtable_debug_power_on();
