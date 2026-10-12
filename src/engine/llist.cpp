// Port das listas duplamente ligadas genéricas (LList_*, 0x001cc600-0x001cccb8). Ver object.h.

#include <cstring>

#include "engine/memory.h"
#include "engine/object.h"

// [decomp] 0x001cc958 LList_Init
// Com count > 0, aloca os nós em bloco (node_size bytes cada) e já os põe na lista.
uint32_t LList_Init(LLISTINFO_tag* list, uint16_t node_size, uint16_t count) {
    if (list == nullptr) return 0;
    uint8_t* nodes = count == 0 ? nullptr : static_cast<uint8_t*>(Mem_Malloc(uint32_t(node_size) * count, 0x1b04, 0));
    list->first = nullptr;
    list->last = nullptr;
    list->count = 0;
    list->node_size = node_size;
    if (count != 0) {
        std::memset(nodes, 0, size_t(node_size) * count);
        for (uint16_t i = 0; i < count; ++i, nodes += node_size) LList_Add(list, reinterpret_cast<LLNODE_tag*>(nodes));
    }
    return 1;
}

// [decomp] 0x001cca28 LList_Add
// Acrescenta no fim.
uint32_t LList_Add(LLISTINFO_tag* list, LLNODE_tag* node) {
    if (node == nullptr || list == nullptr) return 0;
    if (list->last == nullptr) {
        list->first = node;
        node->prev = nullptr;
        node->next = nullptr;
    } else {
        list->last->next = node;
        node->next = nullptr;
        node->prev = list->last;
    }
    list->last = node;
    ++list->count;
    return 1;
}

// [decomp] 0x001ccac8 LList_Remove
LLNODE_tag* LList_Remove(LLISTINFO_tag* list, LLNODE_tag* node) {
    if (list == nullptr || node == nullptr || list->count == 0) return nullptr;
    if (list->first == node) list->first = node->prev != nullptr ? node->prev : node->next;
    if (list->last == node) list->last = node->next != nullptr ? node->next : node->prev;
    if (node->prev != nullptr) node->prev->next = node->next;
    if (node->next != nullptr) node->next->prev = node->prev;
    node->next = nullptr;
    node->prev = nullptr;
    --list->count;
    return node;
}

// [decomp] 0x001ccb68 LList_Insert
// Insere depois de "after", ou no começo se "after" for nulo.
void LList_Insert(LLISTINFO_tag* list, LLNODE_tag* after, LLNODE_tag* node) {
    if (list == nullptr || node == nullptr) return;
    if (after == nullptr) {
        node->prev = nullptr;
        node->next = list->first;
        list->first = node;
        if (list->last == nullptr) list->last = node;
        if (node->next != nullptr) node->next->prev = node;
    } else {
        LLNODE_tag* next = after->next;
        node->prev = after;
        node->next = next;
        if (next != nullptr) next->prev = node;
        after->next = node;
        if (next == nullptr) list->last = node;
    }
    ++list->count;
}

// [decomp] 0x001cca80 LList_Cut
// Tira e devolve o último.
LLNODE_tag* LList_Cut(LLISTINFO_tag* list) {
    if (list == nullptr) return nullptr;
    LLNODE_tag* node = list->last;
    if (node != nullptr) {
        list->last = node->prev;
        if (node->prev == nullptr) {
            list->first = nullptr;
        } else {
            node->prev->next = nullptr;
        }
        --list->count;
    }
    return node;
}

// [decomp] 0x001ccbe0 LList_AllocnNodes
void LList_AllocnNodes(LLISTINFO_tag* list, uint32_t count) {
    if (list == nullptr || count == 0) return;
    uint8_t* nodes = static_cast<uint8_t*>(Mem_Malloc(uint32_t(list->node_size) * count, 0x1b04, 0));
    std::memset(nodes, 0, size_t(list->node_size) * count);
    for (uint16_t i = 0; i < count; ++i) {
        if (nodes != nullptr) LList_Add(list, reinterpret_cast<LLNODE_tag*>(nodes));
        nodes += list->node_size;
    }
}

// [decomp] 0x001cc600 LList_InsertAt
// Com from_first = 0 (o que Text_AddMsg usa): do último para o primeiro, entra depois do primeiro nó com
// compare(node, nó) 0 ou 1, ou no começo. Com from_first != 0: do primeiro para o último, entra antes do
// primeiro nó com compare 0; sem ele, antes do último com compare -1. Divergência: sem nenhum dos dois
// (lista vazia ou só compare 1) o original seguiria um ponteiro nulo; aqui entra no começo.
LLNODE_tag* LList_InsertAt(LLISTINFO_tag* list, LLNODE_tag* node, int8_t from_first,
                           int32_t (*compare)(const void*, const void*)) {
    if (list == nullptr || node == nullptr || compare == nullptr) return nullptr;
    LLNODE_tag* after = nullptr;
    // Os comparadores (sort.cpp) leem os nós como palavras de ponteiro de 4 bytes (P32).
    const P32<LLNODE_tag> node_word = node;
    if (from_first != 0) {
        LLNODE_tag* mark = nullptr;
        for (P32<LLNODE_tag> cur = list->first; cur != nullptr; cur = cur->next) {
            const int32_t r = compare(&node_word, &cur);
            if (r == 0) {
                mark = cur;
                break;
            }
            if (r == -1) mark = cur;
        }
        after = mark != nullptr ? mark->prev : nullptr;
    } else {
        for (P32<LLNODE_tag> cur = list->last; cur != nullptr; cur = cur->prev) {
            const int32_t r = compare(&node_word, &cur);
            if (r == 0 || r == 1) {
                after = cur;
                break;
            }
        }
    }
    if (after == nullptr) {
        node->prev = nullptr;
        node->next = list->first;
        list->first = node;
        if (list->last == nullptr) list->last = node;
        if (node->next != nullptr) node->next->prev = node;
    } else {
        LLNODE_tag* next = after->next;
        node->prev = after;
        node->next = next;
        if (next != nullptr) next->prev = node;
        after->next = node;
        if (next == nullptr) list->last = node;
    }
    ++list->count;
    return after;
}

// ---- listas duplas (DList_*): uma lista de nós livres e outra dos em uso ----

// [decomp] 0x001cc778 DList_Init
// Aloca "count" nós de node_size bytes, zerados, todos na lista de livres.
uint32_t DList_Init(DLISTINFO_tag* list, uint16_t node_size, uint16_t count) {
    if (list == nullptr) return 0;
    LList_Init(&list->free, node_size, count);
    list->used.first = nullptr;
    list->used.last = nullptr;
    list->used.count = 0;
    list->used.node_size = node_size;
    return 1;
}

// [decomp] 0x001ccd60 DList_AllocnNodes
// Mais "count" nós livres.
void DList_AllocnNodes(DLISTINFO_tag* list, uint32_t count) {
    if (list == nullptr || count == 0) return;
    LList_AllocnNodes(&list->free, count);
}

// [decomp] 0x001cccb8 DList_MoveFromFree2InUse
// Tira o último nó livre e o põe no fim da lista em uso; nulo sem nós livres.
void* DList_MoveFromFree2InUse(DLISTINFO_tag* list) {
    if (list == nullptr) return nullptr;
    LLNODE_tag* node = LList_Cut(&list->free);
    if (node == nullptr) return nullptr;
    LList_Add(&list->used, node);
    return node;
}

// [decomp] 0x001cc868 DList_RemoveFromInUse2Free
// Tira o nó da lista em uso e o põe no começo da de livres. Como no original, se o nó era o
// primeiro (ou o último) da lista em uso, a ponta passa para o vizinho de um lado ou, sem ele, do
// outro.
uint32_t DList_RemoveFromInUse2Free(DLISTINFO_tag* list, LLNODE_tag* node) {
    if (list == nullptr || node == nullptr || list->used.count == 0) return 0;
    LLISTINFO_tag& used = list->used;
    if (used.first == node) used.first = node->prev != nullptr ? node->prev : node->next;
    if (used.last == node) used.last = node->next != nullptr ? node->next : node->prev;
    if (node->prev != nullptr) node->prev->next = node->next;
    if (node->next != nullptr) node->next->prev = node->prev;
    node->next = nullptr;
    node->prev = nullptr;
    --used.count;
    LLISTINFO_tag& free = list->free;
    node->prev = nullptr;
    node->next = free.first;
    free.first = node;
    if (free.last == nullptr) free.last = node;
    if (node->next != nullptr) node->next->prev = node;
    ++free.count;
    return 1;
}
