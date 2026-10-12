// Port de QuickSort, Swap e dos comparadores Comp*. Ver sort.h.

#include "engine/sort.h"

#include <cstring>

namespace {

using CompFn = int32_t (*)(const void*, const void*);
CompFn Comp = nullptr;

template <typename T>
T deref(const void* p, int offset) {
    const uint8_t* q;
    std::memcpy(&q, p, sizeof(q));
    T v;
    std::memcpy(&v, q + offset, sizeof(T));
    return v;
}
template <typename T>
T at(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(p) + offset, sizeof(T));
    return v;
}

}  // namespace

// [decomp] 0x001d3320 CompSpr
int32_t CompSpr(const void* a, const void* b) {
    const int8_t x = deref<int8_t>(a, 0x38), y = deref<int8_t>(b, 0x38);
    if (y < x) return -1;
    return x < y ? 1 : 0;
}

// [decomp] 0x001d3350 CompLight
int32_t CompLight(const void* a, const void* b) {
    const float x = deref<float>(a, 0x38), y = deref<float>(b, 0x38);
    if (x <= y && y <= x) return 0;
    return x <= y ? -1 : 1;
}

// [decomp] 0x001d33c0 CompHitData
int32_t CompHitData(const void* a, const void* b) {
    const float x = deref<float>(a, 0xc), y = deref<float>(b, 0xc);
    if (y <= x) return x <= y ? 0 : 1;
    return -1;
}

// [decomp] 0x001d3430 CompDrawCels
int32_t CompDrawCels(const void* a, const void* b) {
    const uint16_t x = at<uint16_t>(a, 4), y = at<uint16_t>(b, 4);
    if (x < y) return -1;
    return y < x ? 1 : 0;
}

// [decomp] 0x001d3458 CompAlphaObj
int32_t CompAlphaObj(const void* a, const void* b) {
    const float x = at<float>(a, 0), y = at<float>(b, 0);
    if (y <= x) return x <= y ? 0 : 1;
    return -1;
}

// [decomp] 0x001d3400 CompMsgTxt
int32_t CompMsgTxt(const void* a, const void* b) {
    const int8_t x = deref<int8_t>(a, 0x1d), y = deref<int8_t>(b, 0x1d);
    if (x < y) return -1;
    return y < x ? 1 : 0;
}

// [decomp] 0x001d3490 CompTargetObj
int32_t CompTargetObj(const void* a, const void* b) {
    const float x = at<float>(a, 4), y = at<float>(b, 4);
    if (y <= x) return x <= y ? 0 : 1;
    return -1;
}

// [decomp] 0x001d34c8 CompMenuCtrl
int32_t CompMenuCtrl(const void* a, const void* b) {
    const int16_t x = at<int16_t>(a, 4), y = at<int16_t>(b, 4);
    if (x < y) return -1;
    return y < x ? 1 : 0;
}

// [decomp] 0x001d34f0 CompStrings
int32_t CompStrings(const void* a, const void* b) {
    const int r = std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b));
    if (r < 0) return -1;
    return r > 0 ? 1 : 0;
}

// [decomp] 0x001d3390 CompBoxList
int32_t CompBoxList(const void* a, const void* b) {
    return deref<float>(b, 4) <= deref<float>(a, 4) ? 1 : -1;
}

// [decomp] 0x001d3518 Swap
void Swap(char* a, char* b, uint32_t size) {
    if (a == b) return;
    for (; size != 0; --size, ++a, ++b) {
        const char t = *a;
        *a = *b;
        *b = t;
    }
}

// [decomp] 0x001d30a8 QuickSort
void QuickSort(void* base, uint32_t count, uint32_t size, COMP_FUNC comp, void*) {
    switch (comp) {
        case COMP_SPR: Comp = CompSpr; break;
        case COMP_HITDATA: Comp = CompHitData; break;
        case COMP_DRAWCELS: Comp = CompDrawCels; break;
        case COMP_ALPHAOBJ: Comp = CompAlphaObj; break;
        case COMP_MSGTXT: Comp = CompMsgTxt; break;
        case COMP_TARGETOBJ: Comp = CompTargetObj; break;
        case COMP_MENUCTRL: Comp = CompMenuCtrl; break;
        case COMP_STRINGS: Comp = CompStrings; break;
        case COMP_BOXLIST: Comp = CompBoxList; break;
        default: Comp = CompLight; break;
    }
    if (count < 2 || size == 0) return;
    char* lo_stack[64];
    char* hi_stack[64];
    int sp = 0;
    char* lo = static_cast<char*>(base);
    char* hi = lo + size * (count - 1);
    for (;;) {
        // Pivô (o elemento do meio) vai para a primeira posição.
        Swap(lo + ((uint32_t(hi - lo) / size + 1) >> 1) * size, lo, size);
        char* left = lo + size;
        char* right = hi + size;
        for (;;) {
            while (left <= hi && Comp(left, lo) < 1) left += size;
            right -= size;
            while (lo < right && Comp(right, lo) > -1) right -= size;
            if (right < left) break;
            Swap(left, right, size);
            left += size;
        }
        Swap(lo, right, size);
        // Continua pela parte menor e guarda a maior na pilha.
        if (int32_t(right - (lo + 1)) < int32_t(hi - left)) {
            if (left < hi) {
                lo_stack[sp] = left;
                hi_stack[sp] = hi;
                ++sp;
            }
            hi = right - size;
            if (lo + size < right) continue;
        } else {
            if (lo + size < right) {
                lo_stack[sp] = lo;
                hi_stack[sp] = right - size;
                ++sp;
            }
            lo = left;
            if (left < hi) continue;
        }
        if (--sp < 0) return;
        lo = lo_stack[sp];
        hi = hi_stack[sp];
    }
}
