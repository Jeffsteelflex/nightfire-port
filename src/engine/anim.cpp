// Animação esquelética. Ver anim.h.
//
// Divergências:
// - A pele montada por AnimProcessSkinData é zerada ao ser alocada (o original deixa lixo nos
//   campos que a contagem não cobre; só é lido com a contagem diferente de zero).
// - AnimFrameCopy avalia os polinômios dos canais um a um; a VU0 os avalia em grupos de quatro,
//   com as mesmas multiplicações e somas na mesma ordem.
// - AnimObjectSetSleeveType não escreve num objeto sem animação (ver a função).

#include "engine/anim.h"
#include "engine/lights.h"
#include "engine/weapons.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/gameflow.h"
#include "engine/game.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/view.h"
#include "engine/vecmath.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_file.h"
#include "platform/fmath.h"

ANIMSKEL_tag* pSkeletons[kSkeletonSlots];
float Glb_SkinScale[4] = {1.0f, 1.0f, 1.0f, 0.0f};
float Glb_InvSkinScale[4] = {1.0f, 1.0f, 1.0f, 0.0f};
int32_t GlbAnimList = 0;
ANIMSCRIPT_tag* GlbAnimListItems[32];
uint8_t Glb_bScriptDeleted = 0;
ANIMSCRIPT_tag* Glb_pCurrentScript = nullptr;
uint8_t ReverseFlag = 0;

namespace {
uint32_t AnimTagTimeStamp = 0;
uint32_t AnimScriptTimeStamp = 0;
uint8_t Glb_AnimPostLoadDone = 0;
int32_t nDistanceTables = 0;
uint32_t AnimFileSize = 0;
uint32_t AnimFileSizes = 0;   // peles carregadas (bytes)
uint32_t SeqFileSizes = 0;    // 0x002c7234
uint32_t ScriptFileSizes = 0; // 0x002c7238
uint32_t SkinFiles = 0;       // 0x002c723c
uint32_t SeqFiles = 0;        // 0x002c7240
uint32_t ScriptFiles = 0;     // 0x002c7244
uint32_t AnimCounters[7];     // 0x002c7248: [0] soma dos quadros das sequências
void* pAnimFileData = nullptr;
const uint8_t* pAnimData = nullptr;
// Peles de manga (AnimSleeveGetEntity, SleeveEnts.324).
const uint32_t kSleeveEnts[8] = {0x20009d0, 0x2000900, 0x20001f7, 0x2000929,
                                 0x20009e6, 0x20009ff, 0x2000a14, 0x2000a13};

template <typename T>
T rd(const void* p) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}
}  // namespace

// ------------------------------------------------------------------ carga

// [decomp] 0x001c63b8 AnimPreLoadInit
// Antes de carregar: na carga fria zera os esqueletos, os carimbos e as escalas.
void AnimPreLoadInit(int8_t warm) {
    if (warm == 0) {
        AnimTagTimeStamp = 0x80000001u;
        AnimScriptTimeStamp = 1;
        for (float* s : {Glb_SkinScale, Glb_InvSkinScale}) s[0] = s[1] = s[2] = 1.0f;
        for (int i = kSkeletonSlots - 1; i >= 0; --i) pSkeletons[i] = nullptr;
        AnimDistanceTableInit();
    }
    Glb_AnimPostLoadDone = 0;
    for (int i = 6; i >= 0; --i) AnimCounters[i] = 0;
}

// [decomp] 0x00120c48 MP_NeedSkin
// Fora do multijogador toda pele é carregada; no multijogador, as dos personagens escolhidos.
// (MP_skins ainda não existe no port: o multijogador carrega todas.)
bool MP_NeedSkin(uint32_t) {
    if (GameState.multiplayer == 0) return true;
    pending_call("MP_NeedSkin (multijogador)");
    return true;
}

// [decomp] 0x001bdf40 AnimLoadFile
// Carrega <hash>.bin do FILES.BIN e registra na tabela de hashes (se ainda não houver): peles
// (05), sequências (04, processadas a menos que no_process) e scripts (06).
void AnimLoadFile(uint32_t hash, int8_t no_process) {
    char name[0x40];
    const uint32_t type = hash & 0xff000000u;
    if (type == 0x5000000u) {
        if (!MP_NeedSkin(hash)) return;
        std::snprintf(name, sizeof(name), "%8.8x.bin", hash);
        void* data = psiFileLoad(name, 0x1c04, &AnimFileSize);
        if (hashtable_getitem(hash) == nullptr) hashtable_additem(hash, data);
        AnimFileSizes += AnimFileSize;
        ++SkinFiles;
        pAnimFileData = data;
        AnimProcessSkinData(hash);
    } else if (type == 0x4000000u) {
        std::snprintf(name, sizeof(name), "%8.8x.%s", hash, "bin");
        void* data = psiFileLoad(name, 0x1e04, &AnimFileSize);
        if (hashtable_getitem(hash) == nullptr) hashtable_additem(hash, data);
        SeqFileSizes += AnimFileSize;
        ++SeqFiles;
        pAnimFileData = data;
        if (no_process == 0) AnimProcessSeqData(hash);
        const auto* raw = static_cast<const ANIMRAWSEQ_tag*>(hashtable_getitem(hash));
        AnimCounters[0] += uint32_t(int32_t(raw->frame_count));
    } else if (type == 0x6000000u) {
        std::snprintf(name, sizeof(name), "%8.8x.bin", hash);
        void* data = psiFileLoad(name, 0x1f04, &AnimFileSize);
        if (hashtable_getitem(hash) == nullptr) hashtable_additem(hash, data);
        ScriptFileSizes += AnimFileSize;
        ++ScriptFiles;
        pAnimFileData = data;
        AnimProcessScriptData(hash);
    }
}

// [decomp] 0x001bd328 AnimSkeletonProcess
void AnimSkeletonProcess(void* data) {
    auto* skel = static_cast<ANIMSKEL_tag*>(data);
    pSkeletons[int16_t(skel->id) & (kSkeletonSlots - 1)] = skel;
    pAnimData = static_cast<const uint8_t*>(data) + 0x10 + uint32_t(skel->bone_count) * 0xc;
}

// [decomp] 0x001bd500 AnimProcessSkinData
// Troca o arquivo da pele na tabela de hashes por um ANIMSKIN_tag que aponta para dentro dele: os
// pais dos ossos, os modelos (hash de entidade -> celglist; o bit 0x100000 escolhe a manga), os
// modelos rígidos, as matrizes de pose inversa e os anexos.
void AnimProcessSkinData(uint32_t hash) {
    if ((hash & 0xff000000u) != 0x5000000u) return;
    const uint8_t* p = static_cast<const uint8_t*>(hashtable_getitem(hash));
    pAnimData = p;
    if (p == nullptr) return;
    auto* skin = static_cast<ANIMSKIN_tag*>(Mem_Malloc(sizeof(ANIMSKIN_tag), 0x2004, 0));
    std::memset(skin, 0, sizeof(*skin));
    hashtable_modify(hash, skin);
    std::memcpy(skin->scale, p + 4, 12);
    skin->n_meshes = p[0x10];
    skin->n_discrete = p[0x11];
    skin->n_morphs = p[0x12];
    skin->n_extra = p[0x13];
    skin->skeleton = p[0x14];
    p += 0x15;
    const uint32_t bones = pSkeletons[skin->skeleton]->bone_count;
    skin->parents = const_cast<uint8_t*>(p);
    p += bones;
    const auto align4 = [&] {
        while ((reinterpret_cast<uintptr_t>(p) & 3) != 0) ++p;
    };
    if (skin->n_discrete != 0) {
        skin->discrete_bones = const_cast<uint8_t*>(p);
        p += skin->n_discrete;
        align4();
        skin->discrete = reinterpret_cast<P32<celglist_tag>*>(const_cast<uint8_t*>(p));
        for (uint32_t i = 0; i < skin->n_discrete; ++i) {
            const uint32_t h = rd<uint32_t>(p);
            p += 4;
            skin->discrete[i] = h == 0xffffffffu ? nullptr : hashtable_hashcode_to_celglist(h);
        }
    }
    if (skin->n_meshes != 0) {
        align4();
        skin->meshes = reinterpret_cast<P32<celglist_tag>*>(const_cast<uint8_t*>(p));
        for (uint32_t i = 0; i < skin->n_meshes; ++i) {
            const uint32_t h = rd<uint32_t>(p);
            p += 4;
            celglist_tag* g = nullptr;
            if (h != 0xffffffffu) g = hashtable_hashcode_to_celglist((h & 0x100000) != 0 ? AnimSleeveGetEntity(0) : h);
            skin->meshes[i] = g;
        }
        skin->inv_pose = static_cast<MATRIX*>(Mem_Malloc(bones << 6, 0x2104, 0));
        for (uint32_t i = 0; i < bones; ++i) {
            alignas(16) _VECTOR t{rd<float>(p), rd<float>(p + 4), rd<float>(p + 8), 0.0f};
            alignas(16) quaternion_tag q;
            std::memcpy(&q, p + 12, 16);
            p += 0x1c;
            Quat_QuatTransToMat(&q, &t, &skin->inv_pose[i]);
        }
    }
    if (skin->n_extra != 0) {
        skin->extras = const_cast<uint8_t*>(p);
        p += uint32_t(skin->n_extra) * 0x24;
    }
    pAnimData = p;
}

// [decomp] 0x001c4ce0 AnimProcessSeqData
// Acha o fim dos registros de canal (para o tamanho), marca algumas sequências de passo como "de
// distância" (flags 7) e troca as três primeiras palavras por ponteiro, tamanho e cursor.
void AnimProcessSeqData(uint32_t hash) {
    if ((hash & 0xff000000u) != 0x4000000u) return;
    auto* raw = static_cast<uint8_t*>(hashtable_getitem(hash));
    pAnimData = raw;
    if (raw == nullptr) return;
    raw[0xc] &= 0xf;
    raw[0xd] = 0;
    raw[0xe] = 0;
    raw[0xf] = 0;
    const uint16_t* q = reinterpret_cast<const uint16_t*>(raw + 0x40);
    const uint16_t* start = q;
    const int16_t frames = rd<int16_t>(raw + 0x38);
    const int16_t channels = rd<int16_t>(raw + 0x3a);
    uint32_t desc = 0;
    uint8_t half = 0;
    for (int32_t c = 0; c < channels; ++c) {
        if ((half & 1) == 0) {
            desc = (uint32_t(reinterpret_cast<const uint8_t*>(q)[2]) << 16 |
                    uint32_t(reinterpret_cast<const uint8_t*>(q)[3]) << 24) >> 16;
            q += 2;
        }
        ++half;
        uint32_t total = 0;
        uint8_t h;
        do {
            h = half;
            uint32_t d = desc;
            if ((h & 1) == 0) {
                d = q[0];
                desc = q[1];
                q += 2;
            }
            total += int32_t(d) >> 6;
            half = h ^ 1;
            for (uint32_t m = d & 0x3f; m != 0; m >>= 1) {
                if ((m & 1) != 0) q += 2;
            }
        } while (total < uint32_t(int32_t(frames)) - 1u);
        if ((half & 1) != 0) half = h;
    }
    uint32_t flags = rd<uint32_t>(raw + 0xc);
    bool mark = false;
    switch (hash) {
        case 0x40000e9: case 0x4000125: case 0x4000144: case 0x4000145: case 0x4000150: case 0x4000151:
        case 0x4000154: case 0x4000155: case 0x400015c: case 0x400015d: case 0x4000167: case 0x4000168:
        case 0x400079c: case 0x40007b0: case 0x40007b3: case 0x40007b6: case 0x40007e4: case 0x40007e5:
        case 0x4000804: case 0x4000809: case 0x400080c: case 0x400080f: case 0x4000811: case 0x400082c:
        case 0x400082d: case 0x4000831: case 0x4000832: case 0x4000835: case 0x4000838: case 0x4000847:
        case 0x400084e: case 0x400084f:
            mark = true;
            break;
        default:
            break;
    }
    if (mark) {
        flags |= 7;
        std::memcpy(raw + 0xc, &flags, 4);
    }
    auto* r = reinterpret_cast<ANIMRAWSEQ_tag*>(raw);
    r->size = uint32_t(reinterpret_cast<const uint8_t*>(q) - reinterpret_cast<const uint8_t*>(start));
    r->channels = reinterpret_cast<uint32_t*>(const_cast<uint16_t*>(start));
    r->cursor = nullptr;
    pAnimData = reinterpret_cast<const uint8_t*>(q);
}

// [decomp] 0x001bd3d8 AnimProcessScriptData
// Só percorre os comandos (o original não guarda nada; pAnimData para no fim).
void AnimProcessScriptData(uint32_t hash) {
    if ((hash & 0xff000000u) != 0x6000000u) return;
    const uint8_t* p = static_cast<const uint8_t*>(hashtable_getitem(hash));
    pAnimData = p;
    if (p == nullptr) return;
    uint32_t n = rd<uint16_t>(p + 6) & 0x1ff;
    p += 8;
    while (n-- != 0) {
        uint32_t words = p[1];
        if (p[0] == 0) words += 3;
        else if (p[0] < 5) words += 2;
        p += (words + 1) * 2;
    }
    pAnimData = p;
}

// [decomp] 0x001be1a8 AnimPostLoadInit
// Uma vez por carga: as peles de esqueleto 0 ou 1 (humanos) recebem os anexos da pele do Bond
// (05000007); a 0500002f junta os dela aos dele.
void AnimPostLoadInit() {
    if (Glb_AnimPostLoadDone != 0) return;
    Glb_AnimPostLoadDone = 1;
    const int count = hashtable_get_hashtype_count(0x5000000);
    auto* bond = static_cast<ANIMSKIN_tag*>(hashtable_getitem(0x5000007));
    if (count == 0 || bond == nullptr) return;
    uint32_t hashes[0xa8];
    hashtable_get_hashtype_data(0x5000000, hashes);
    for (int i = 0; i < count; ++i) {
        auto* skin = static_cast<ANIMSKIN_tag*>(hashtable_getitem(hashes[i]));
        if (skin->skeleton >= 2) continue;
        if (hashes[i] == 0x500002f) {
            const uint8_t own = skin->n_extra;
            uint8_t* own_extras = skin->extras;
            const uint32_t total = uint32_t(own) + bond->n_extra;
            skin->n_extra = uint8_t(total);
            skin->extras = static_cast<uint8_t*>(Mem_Malloc((total & 0xff) * 0x24, 0x2004, 0));
            amemcpy(skin->extras, bond->extras, uint32_t(bond->n_extra) * 0x24);
            amemcpy(skin->extras + uint32_t(bond->n_extra) * 0x24, own_extras, uint32_t(own) * 0x24);
        } else {
            skin->n_extra = bond->n_extra;
            skin->extras = bond->extras;
        }
    }
}

// [decomp] 0x001c6590 AnimSleeveGetEntity
// A primeira manga carregada a partir de "index" (dando a volta), ou 0.
uint32_t AnimSleeveGetEntity(uint32_t index) {
    for (uint32_t tries = 0;; ) {
        if (7 < index) index = 0;
        const uint32_t h = kSleeveEnts[index];
        if (hashtable_getitem(h) != nullptr) return h;
        ++tries;
        ++index;
        if (7 < tries) return 0;
    }
}

// [decomp] 0x001c76e8 AnimDistanceTableInit
void AnimDistanceTableInit() { nDistanceTables = 0; }

// ------------------------------------------------------------------ listas de scripts

// [decomp] 0x001c77c0 AnimTagGetUnique
int32_t AnimTagGetUnique() {
    if (AnimTagTimeStamp == 0) AnimTagTimeStamp = 1;
    return int32_t(AnimTagTimeStamp++);
}

// [decomp] 0x001c0fc0 AnimListBuild
// Junta em GlbAnimListItems (até 32) os scripts do objeto que passam em algum dos critérios
// (bits de "criteria", com "value"); com mode bit 1 a lista começa vazia, com bit 2 os que passam
// saem dela. Com fn, chama fn para cada um. Devolve o primeiro da lista (nulo se vazia).
ANIMSCRIPT_tag* AnimListBuild(obj_tag* obj, uint32_t criteria, uint32_t value, void (*fn)(obj_tag*, ANIMSCRIPT_tag*),
                              uint32_t mode) {
    if (obj == nullptr || anim_of(obj) == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    if ((mode & 1) != 0) GlbAnimList = 0;
    int32_t count = GlbAnimList;
    for (ANIMSCRIPT_tag* s = anim->scripts; s != nullptr; s = s->next) {
        uint32_t left = criteria;
        uint32_t bit = 1;
        while (left != 0) {
            while ((left & bit) == 0) bit <<= 1;
            left ^= bit;
            bool match;
            switch (bit) {
                case 1: match = true; break;
                case 2: match = nf::p32_u(s) != value; break;
                case 4: match = s->prev == nullptr; break;
                case 8: match = s->next == nullptr; break;
                case 0x10: match = (s->flags & 0x10000000u) != 0; break;
                case 0x20: match = s->hash == value; break;
                case 0x40:
                    match = nf::p32_u(s) == value || nf::p32_u(s->sync) == value;
                    break;
                case 0x80: match = nf::p32_u(s->sync) == value; break;
                case 0x100: match = s->sync != nullptr; break;
                case 0x200: match = s->tag == value; break;
                case 0x400: match = (s->tag & value) != 0; break;
                case 0x800: match = (s->flags & 0x40000u) != 0; break;
                case 0x1000: match = anim->skin->n_morphs != 0 && (s->seq[1]->flags & 8) != 0; break;
                case 0x2000: match = s->blend != nullptr; break;
                case 0x4000: match = (s->flags & 0x4000u) != 0; break;
                default: match = false; break;
            }
            bit <<= 1;
            if (match) {
                int32_t i = 0;
                while (i < count && GlbAnimListItems[i] != s) ++i;
                if ((mode & 2) == 0) {
                    if (i == GlbAnimList && i < 32) {
                        GlbAnimListItems[i] = s;
                        GlbAnimList = i + 1;
                    }
                } else if (i != count) {
                    GlbAnimList = count - 1;
                    for (; i < GlbAnimList; ++i) GlbAnimListItems[i] = GlbAnimListItems[i + 1];
                }
            }
            count = GlbAnimList;
        }
    }
    if (count == 0) return nullptr;
    if (fn != nullptr) {
        for (int32_t i = 0; i < GlbAnimList; ++i) fn(obj, GlbAnimListItems[i]);
    }
    return GlbAnimListItems[0];
}

// [decomp] 0x001c6b98 AnimListDelete
void AnimListDelete(obj_tag* obj) {
    for (int32_t i = 0; i < GlbAnimList; ++i) AnimScriptDelete(obj, &GlbAnimListItems[i]);
    GlbAnimList = 0;
}

// ------------------------------------------------------------------ sequências

// [decomp] 0x001c7248 AnimSeqNew
ANIMSEQ_tag* AnimSeqNew(uint32_t frame_size, uint32_t tag) {
    auto* seq = static_cast<ANIMSEQ_tag*>(Mem_Malloc(sizeof(ANIMSEQ_tag), 0x2304, 0));
    if (seq == nullptr) return nullptr;
    std::memset(seq, 0, sizeof(*seq));
    seq->frame = static_cast<uint8_t*>(Mem_Malloc(frame_size, tag, 0));
    if (seq->frame != nullptr) {
        std::memset(seq->frame, 0, frame_size);
        return seq;
    }
    void* p = seq;
    Mem_Free(&p);
    return nullptr;
}

// [decomp] 0x001c72e0 AnimSeqDelete
void AnimSeqDelete(P32<ANIMSEQ_tag>* seq) {
    ANIMSEQ_tag* s = *seq;
    if (s == nullptr) return;
    if (s->cursors != nullptr) Mem_Free(&s->cursors);
    if (s->frame != nullptr) Mem_Free(&s->frame);
    Mem_Free(seq);
}

// [decomp] 0x001c79a0 AnimSeqGetInfo
ANIMRAWSEQ_tag* AnimSeqGetInfo(uint32_t hash, ANIMSEQ_tag* seq, ANIMRAWSEQ_tag* raw) {
    seq->hash = hash;
    if ((hash & 0xff000000u) != 0x4000000u) return nullptr;
    if (raw == nullptr) {
        raw = static_cast<ANIMRAWSEQ_tag*>(hashtable_getitem(hash));
        if (raw == nullptr) return nullptr;
    }
    seq->raw = raw;
    std::memcpy(seq->sphere, raw->sphere, 16);
    std::memcpy(seq->info_20, raw->unknown_20, 12);
    std::memcpy(seq->info_2c, raw->unknown_2c, 12);
    seq->frame_count = raw->frame_count;
    seq->skeleton = raw->unknown_3c;
    seq->unknown_a8 = raw->unknown_3e;
    seq->channels = uint8_t(raw->channel_count);
    seq->flags = (seq->flags & 0xfffffff0u) | (raw->flags & 0xf);
    return raw;
}

// ------------------------------------------------------------------ quadros

namespace {
float Poly[6];  // 1, f, f², f³, f⁴, f⁵
}

// [decomp] 0x001c5668 AnimFrameCopy
// Decodifica o quadro "frame" (1 = o primeiro; -1 = o da sequência) da sequência em "out": as
// translações (ossos x 16 bytes) e depois os quatérnios (ossos x 16). Cada canal é um polinômio de
// grau até 5 no quadro absoluto (o segmento é o primeiro cuja duração acumulada alcança o quadro).
// Ossos sem translação animada recebem a padrão do esqueleto; com a pele, os ossos que ela não usa
// (pai sem 0x80) ficam como estão. O quatérnio vem de x, y, z (z >= 2: z - 4 com w negativo). No
// modo de canal único (flags bit 3, rostos) os valores vão em sequência. Ao fim a raiz é dividida
// pela escala da pele e "first"/"offset" acompanham a posição depois da última translação.
void AnimFrameCopy(void* out, ANIMSEQ_tag* seq, ANIMSKIN_tag* skin, float frame) {
    float f = float(int32_t(frame));
    if (f == -1.0f) f = seq->frame_pos;
    if (f < float(int32_t(seq->first_frame))) f = float(int32_t(seq->first_frame));
    if (float(int32_t(seq->last_frame)) < f) f = float(int32_t(seq->last_frame));
    Poly[0] = 1.0f;
    Poly[1] = f - 1.0f;
    Poly[2] = Poly[1] * Poly[1];
    Poly[3] = Poly[2] * Poly[1];
    Poly[4] = Poly[3] * Poly[1];
    Poly[5] = Poly[4] * Poly[1];
    const uint32_t target = uint32_t(Poly[1]);
    const ANIMSKEL_tag* skel = pSkeletons[seq->skeleton];
    const bool single = ((int32_t(seq->flags) >> 3) & 1) != 0;
    const uint32_t bones = single ? seq->channels : skel->bone_count;
    auto* base = static_cast<uint8_t*>(out);
    float* singles = static_cast<float*>(out);
    uint8_t* trans = base;
    uint8_t* quat = base + bones * 0x10;
    seq->raw->cursor = seq->raw->channels;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(nf::raw(seq->raw->channels));
    const uint32_t* rec = seq->raw->channels;
    const bool cached = ((int32_t(seq->flags) >> 0x15) & 1) != 0;
    uint32_t* cache = seq->cursors != nullptr ? seq->cursors - 2 : nullptr;
    const auto put = [&](int32_t code, float v) {
        switch (code) {
            default: {  // quatérnio z e w
                float* q = reinterpret_cast<float*>(quat);
                float sign = 1.0f;
                if (v < 2.0f) {
                    q[2] = v;
                } else {
                    q[2] = v - 4.0f;
                    sign = -1.0f;
                }
                float w = 1.0f - (q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                w = 0.0f < w ? sign * std::sqrt(w) : 0.0f;
                q[3] = w;
                quat += 0x10;
                break;
            }
            case 1:
                reinterpret_cast<float*>(quat)[1] = v;
                break;
            case 2:
                reinterpret_cast<float*>(quat)[0] = v;
                break;
            case 3:
                reinterpret_cast<float*>(trans)[2] = v * Glb_SkinScale[2];
                trans += 0x10;
                break;
            case 4:
                reinterpret_cast<float*>(trans)[1] = v * Glb_SkinScale[1];
                break;
            case 5:
                reinterpret_cast<float*>(trans)[0] = v * Glb_SkinScale[0];
                break;
            case 6:
                *singles++ = v;
                break;
        }
    };
    for (uint32_t bone = 0; bone < bones; ++bone) {
        bool skip = false;
        int32_t code;
        if (!single) {
            if (skin != nullptr && (skin->parents[bone] & 0x80) == 0) {
                skip = true;
                trans += 0x10;
                quat += 0x10;
            }
            code = 5;
            if ((skel->trans_mask[bone >> 5] & (1u << (bone & 0x1f))) == 0) {
                code = 2;
                if (!skip) {
                    float* t = reinterpret_cast<float*>(trans);
                    t[0] = skel->trans[bone][0];
                    t[1] = skel->trans[bone][1];
                    t[2] = skel->trans[bone][2];
                    Vec_Mul(reinterpret_cast<_VECTOR*>(trans), reinterpret_cast<const _VECTOR*>(Glb_SkinScale));
                    trans += 0x10;
                }
            }
        } else {
            code = 6;
        }
        for (;;) {
            uint32_t word = *rec;
            uint32_t half = 1;
            const uint32_t* p = rec + 1;
            rec = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(rec) + (word & 0xffff));
            if (!skip) {
                uint32_t frames = 0;
                uint32_t* slot = cache;
                if (cached) {
                    slot = cache + 2;
                    if (slot[0] != 0) {
                        frames = reinterpret_cast<const uint16_t*>(cache)[7];
                        p = reinterpret_cast<const uint32_t*>((slot[0] + uintptr_t(data)) & ~uintptr_t(1));
                        word = uint32_t(reinterpret_cast<const uint16_t*>(cache)[6]) << 16;
                        half = slot[0] & 1;
                    }
                }
                uint32_t mask;
                for (;;) {
                    if (cached) {
                        reinterpret_cast<uint16_t*>(slot)[2] = uint16_t(word >> 16);
                        slot[0] = uint32_t(reinterpret_cast<const uint8_t*>(p) - data) | half;
                        reinterpret_cast<uint16_t*>(slot)[3] = uint16_t(frames);
                    }
                    uint32_t d;
                    if (half != 0) {
                        d = word >> 16;
                    } else {
                        word = *p++;
                        d = word & 0xffff;
                    }
                    half = half == 0 ? 1 : 0;
                    mask = d & 0x3f;
                    frames += d >> 6;
                    if (target <= frames) break;
                    uint32_t n = 0;
                    for (uint32_t m = mask; m != 0; m &= m - 1) ++n;
                    p += n;
                }
                float c[6];
                for (int k = 0; k < 6; ++k) {
                    if ((mask >> k & 1) != 0) c[k] = rd<float>(p++);
                    else c[k] = 0.0f;
                }
                // ACC = c0 * 1 + c1 * f + c2 * f² + c3 * f³ + c4 * f⁴; resultado = ACC + c5 * f⁵.
                float v = c[0] * Poly[0];
                v += c[1] * Poly[1];
                v += c[2] * Poly[2];
                v += c[3] * Poly[3];
                v += c[4] * Poly[4];
                v += c[5] * Poly[5];
                put(code, v);
                cache = slot;
            }
            if (code == 6) code = 0;
            --code;
            if (code == -1) break;
        }
    }
    Vec_Mul(static_cast<_VECTOR*>(out), reinterpret_cast<const _VECTOR*>(Glb_InvSkinScale));
    if ((seq->flags & 0x100000u) == 0) {
        seq->flags |= 0x100000u;
        std::memcpy(&seq->first, trans, 16);
    }
    _VECTOR cur;
    std::memcpy(&cur, trans, 16);
    seq->offset = {cur.x - seq->first.x, cur.y - seq->first.y, cur.z - seq->first.z, cur.w - seq->first.w};
}

// [decomp] 0x001c2b80 AnimFrameBlend
// out = a mais (b - a) * t nas translações (sem passar de b) e slerp nos quatérnios (o preciso só
// de perto: closeness < 0xf1). t 0 ou 1 copia. Com bone_blend (lista de ossos terminada em 0xff, de
// 2 em 2 bytes), os ossos da lista ficam com b inteiros.
void AnimFrameBlend(obj_tag* obj, void* out, void* a, void* b, uint32_t bones, float t, void* bone_blend) {
    auto* po = static_cast<uint8_t*>(out);
    auto* pa = static_cast<uint8_t*>(a);
    auto* pb = static_cast<uint8_t*>(b);
    const uint32_t q = bones * 0x10;
    const int32_t exact = t == 0.0f ? -1 : (t == 1.0f ? 1 : 0);
    bool precise = true;
    if (ScriptCam == 0) {
        const uint8_t* c = anim_of(obj)->closeness;
        uint8_t m = c[0];
        for (int i = 1; i < 4; ++i) m = c[i] < m ? c[i] : m;
        precise = m < 0xf1;
    }
    const auto lerp = [&](float va, float vb) {
        const float d = vb - va;
        const float v = va + d * t;
        return (0.0f <= d ? vb < v : v < vb) ? vb : v;
    };
    const auto blend_bone = [&](uint32_t i) {
        const float* ta = reinterpret_cast<const float*>(pa + i * 0x10);
        const float* tb = reinterpret_cast<const float*>(pb + i * 0x10);
        float* to = reinterpret_cast<float*>(po + i * 0x10);
        to[0] = lerp(ta[0], tb[0]);
        to[1] = lerp(ta[1], tb[1]);
        to[2] = lerp(ta[2], tb[2]);
        const auto* qa = reinterpret_cast<const quaternion_tag*>(pa + q + i * 0x10);
        const auto* qb = reinterpret_cast<const quaternion_tag*>(pb + q + i * 0x10);
        auto* qo = reinterpret_cast<quaternion_tag*>(po + q + i * 0x10);
        if (precise) Quat_Slerp_Acc(t, qa, qb, qo);
        else Quat_Slerp(t, qa, qb, qo);
    };
    if (bone_blend == nullptr) {
        if (exact != 0) {
            void* src = exact < 0 ? a : b;
            if (out != src) amemcpy(out, src, bones << 5);
        } else if (out != a || out != b) {
            for (uint32_t i = 0; i < bones; ++i) blend_bone(i);
        }
        return;
    }
    const uint8_t* list = static_cast<const uint8_t*>(bone_blend);
    for (uint32_t i = 0; i < bones; ++i) {
        bool listed = false;
        for (const uint8_t* e = list; *e != 0xff; e += 2) {
            if (*e == i) {
                listed = true;
                break;
            }
        }
        const uint8_t* src = nullptr;
        if (listed || 0 < exact) src = pb;
        else if (exact < 0) src = pa;
        if (src != nullptr) {
            std::memcpy(po + i * 0x10, src + i * 0x10, 16);
            std::memcpy(po + q + i * 0x10, src + q + i * 0x10, 16);
        } else {
            blend_bone(i);
        }
    }
}

// [decomp] 0x001c75a0 AnimFrameSet
// O quadro da sequência com a fração t do próximo (o seguinte limitado ao último).
void AnimFrameSet(obj_tag* obj, ANIMSEQ_tag* seq, ANIMSKIN_tag* skin, float t) {
    alignas(16) static uint8_t next[0xc60];
    const float cur = seq->frame_pos;
    const uint8_t bones = pSkeletons[skin->skeleton]->bone_count;
    if (0.0002f < t || t < -0.0002f) {
        const float last = float(int32_t(seq->last_frame));
        const float following = last < cur + 1.0f ? last : cur + 1.0f;
        if (t == 1.0f) {
            AnimFrameCopy(seq->frame, seq, skin, following);
        } else {
            AnimFrameCopy(seq->frame, seq, skin, cur);
            AnimFrameCopy(next, seq, skin, following);
            AnimFrameBlend(obj, seq->frame, seq->frame, next, bones, t, nullptr);
        }
    } else {
        AnimFrameCopy(seq->frame, seq, skin, cur);
    }
}

// [decomp] 0x001c7460 AnimSeqSet
// Põe a sequência "hash" no slot (0 corpo, 1 rosto) do script, a partir do quadro "start": zera o
// movimento da raiz, guarda a posição do objeto e, no corpo de um objeto com modelo, a esfera da
// sequência vira a esfera local do objeto.
ANIMSEQ_tag* AnimSeqSet(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, ANIMRAWSEQ_tag* raw, uint32_t hash,
                        int32_t start, int32_t slot) {
    if ((hash & 0xff000000u) != 0x4000000u) return nullptr;
    ANIMSEQ_tag* seq = s->seq[slot];
    if (seq == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_from_state(state);
    if (anim->skin == nullptr) return nullptr;
    if (AnimSeqGetInfo(hash, seq, raw) == nullptr) return nullptr;
    seq->last_frame = seq->frame_count;
    seq->frame_pos = float(start);
    seq->first_frame = 1;
    Vec_Zero(&seq->root_delta);
    seq->obj_pos = obj->pos;
    Vec_Zero(&seq->first);
    Vec_Zero(&seq->offset);
    const uint32_t old = seq->flags;
    seq->flags = (old & 0x21efffffu) | 0x20000000u;
    if ((old & 7) != 0) seq->distance = AnimDistanceTableGet(hash);
    if (slot == 0 && obj->glist != nullptr) std::memcpy(&anim->sphere, seq->sphere, 16);
    seq->skeleton = anim->skin->skeleton;
    return seq;
}

// [decomp] 0x001c28d0 AnimSeqTick
// Passa as flags do script para a sequência, acerta o quadro dela pelo do script e decodifica: o
// rosto (modo de canal único) só de perto; o corpo interpolado entre dois quadros (ou só o inteiro
// com o bit 0x10000 do script). Depois mede quanto a raiz andou (zerando os eixos travados pelos
// bits 0x2000000-0x8000000) e zera a raiz decodificada.
void AnimSeqTick(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, ANIMSEQ_tag* seq) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    if (s != nullptr) {
        const uint32_t f = seq->flags & 0x1ffffffu;
        seq->flags = f;
        if ((s->flags & 0x10000000u) != 0) seq->flags = f | 0x10000000u;
        if (int32_t(s->flags) < 0) seq->flags |= 0x80000000u;
        if ((s->flags & 0x20000000u) != 0) {
            s->flags &= 0xdfffffffu;
            seq->flags |= 0x20000000u;
        }
        if ((s->flags & 0x40000000u) != 0) seq->flags |= 0x40000000u;
        if ((s->flags & 0x1000000u) != 0) seq->flags |= 0x2000000u;
        if ((s->flags & 0x2000000u) != 0) seq->flags |= 0x4000000u;
        if ((s->flags & 0x4000000u) != 0) seq->flags |= 0x8000000u;
        seq->frame_pos = (s->frame - float(int32_t(seq->start))) + 1.0f;
    }
    if (seq->callback == nullptr) {
        if ((seq->flags & 8) != 0) {
            bool near = true;
            if (ScriptCam == 0) {
                uint8_t m = anim->closeness[0];
                for (int i = 1; i < 4; ++i) m = anim->closeness[i] < m ? anim->closeness[i] : m;
                near = m < 0xd;
            }
            if (near) AnimFrameCopy(seq->frame, seq, nullptr, seq->frame_pos);
            return;
        }
        const float t = (s->flags & 0x10000u) == 0 ? seq->frame_pos - float(int32_t(seq->frame_pos)) : 0.0f;
        AnimFrameSet(obj, seq, anim->skin, t);
    } else {
        seq->callback(obj, anim->skin, seq, s);
    }
    _VECTOR* root = reinterpret_cast<_VECTOR*>(nf::raw(seq->frame));
    _VECTOR delta;
    const uint32_t flags = seq->flags;
    if ((flags & 0x60000000u) == 0) {
        delta = {root->x - seq->root.x, root->y - seq->root.y, root->z - seq->root.z, root->w - seq->root.w};
    } else {
        delta = seq->root_delta;
        seq->flags = flags & 0x9fffffffu;
    }
    seq->root = *root;
    if ((seq->flags & 0x2000000u) != 0) delta.x = 0.0f;
    if ((seq->flags & 0x4000000u) != 0) delta.y = 0.0f;
    if ((seq->flags & 0x8000000u) != 0) delta.z = 0.0f;
    seq->root_delta = delta;
    Vec_Zero(root);
}

// ------------------------------------------------------------------ scripts

// [decomp] 0x001c1398 AnimScriptNew
// Script vazio com a sequência do corpo (e a do rosto, se a pele tiver morphs), inserido depois
// de "after" na lista do objeto (ou sozinho, sem ligar na lista, sem "after").
ANIMSCRIPT_tag* AnimScriptNew(obj_tag* obj, ANIMSCRIPT_tag* after) {
    if (obj == nullptr) return nullptr;
    void* mem = Mem_Malloc(sizeof(ANIMSCRIPT_tag), 0x2504, 0);
    if (mem == nullptr) return nullptr;
    auto* s = static_cast<ANIMSCRIPT_tag*>(mem);
    std::memset(s, 0, sizeof(*s));
    s->magic = 0x159f5ab2u;
    ANIMSKIN_tag* skin = anim_of(obj)->skin;
    s->seq[0] = AnimSeqNew(uint32_t(skin_bone_count(skin)) << 5, 0x2404);
    if (s->seq[0] != nullptr) {
        const uint32_t face = skin->n_morphs != 0 ? 0x58 : 0;
        if (face != 0) s->seq[1] = AnimSeqNew(face, 0x3b04);
        if (face == 0 || s->seq[1] != nullptr) {
            if (after == nullptr) {
                s->next = nullptr;
                s->prev = nullptr;
                return s;
            }
            ANIMSCRIPT_tag* next = after->next;
            s->prev = after;
            s->next = next;
            after->next = s;
            if (next != nullptr) next->prev = s;
            return s;
        }
    }
    for (int i = 1; i >= 0; --i) {
        if (s->seq[i] != nullptr) AnimSeqDelete(&s->seq[i]);
    }
    Mem_Free(&mem);
    return nullptr;
}

// [decomp] 0x001c1600 AnimScriptInit
// Liga o script 06xxxxxx: quadros, modo de fim e de sincronia, direção (ReverseFlag de
// AnimScriptAddReverse começa do fim, para trás) e um carimbo de ordem.
void AnimScriptInit(obj_tag* obj, ANIMSCRIPT_tag* s, uint32_t script) {
    const uint8_t reverse = ReverseFlag;
    ReverseFlag = 0;
    if (obj == nullptr || anim_of(obj) == nullptr || s == nullptr || (script & 0xff000000u) != 0x6000000u) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    uint8_t* data = static_cast<uint8_t*>(hashtable_getitem(script));
    s->data = data;
    if (data == nullptr) return;
    const uint32_t old = s->flags;
    s->hash = script;
    s->active_cmds = 0;
    s->flags = (old & 0x280fffffu) | 0x20000000u;
    s->frame_count = rd<int16_t>(data + 4);
    const int16_t mode = rd<int16_t>(data + 6);
    if (mode < 0) s->flags = (old & 0x280fffffu) | 0xa0000000u;
    s->unknown_b2 = uint8_t(mode);
    s->unknown_b3 = uint8_t((uint16_t(mode) >> 9) & 0x3f);
    if (reverse == 0) {
        s->speed = 1.0f;
        s->frame = 1.0f;
    } else {
        s->speed = -1.0f;
        s->frame = float(int32_t(s->frame_count));
    }
    s->sync_mode = 0;
    s->end_mode = 0;
    s->prev_frame = s->frame;
    s->on_delete = nullptr;
    s->owner = nullptr;
    s->on_end = nullptr;
    s->done_flag = nullptr;
    s->event_fn = nullptr;
    if (AnimScriptTimeStamp == 0) AnimScriptTimeStamp = 1;
    s->stamp = AnimScriptTimeStamp++;
    s->tag = 0;
    if (ScriptCam != 0 && anim->skin->n_morphs != 0) s->tag = 0xe0000002u;
    s->fade = 1;
    s->weight = 1.0f;
    s->blend = nullptr;
    s->weight_max = 1.0f;
    s->sync_weight = 0.0f;
    s->unknown_8c = 0.0f;
}

// [decomp] 0x001c6d40 AnimScriptAdd
// Apaga os scripts do objeto e começa só "script".
ANIMSCRIPT_tag* AnimScriptAdd(obj_tag* obj, uint32_t script) {
    if (obj == nullptr || anim_of(obj) == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    AnimListBuild(obj, 1, 0, nullptr, 1);
    for (int32_t i = 0; i < GlbAnimList; ++i) AnimScriptDelete(obj, &GlbAnimListItems[i]);
    GlbAnimList = 0;
    ANIMSCRIPT_tag* s = AnimScriptNew(obj, nullptr);
    anim->scripts = s;
    AnimScriptInit(obj, s, script);
    return s;
}

// [decomp] 0x001c6e20 AnimScriptAddSpeed
// AnimScriptAdd com outra velocidade (quadros por tick).
void AnimScriptAddSpeed(obj_tag* obj, uint32_t script, float speed) {
    ANIMSCRIPT_tag* s = AnimScriptAdd(obj, script);
    if (s != nullptr) s->speed = speed;
}

// [decomp] 0x001c6f18 AnimScriptAddReverse
// AnimScriptAdd tocando do fim para o começo (ReverseFlag para AnimScriptInit). Sem objeto de
// animação a flag fica ligada para o próximo AnimScriptInit, como no original.
ANIMSCRIPT_tag* AnimScriptAddReverse(obj_tag* obj, uint32_t script) {
    ReverseFlag = 1;
    return AnimScriptAdd(obj, script);
}

// [decomp] 0x001c7168 AnimScriptSetPlayDirection
// Direção do primeiro script, se ainda não terminou: 0 para a frente, 1 para trás, 2 inverte.
void AnimScriptSetPlayDirection(obj_tag* obj, int32_t dir) {
    if (obj == nullptr || anim_of(obj) == nullptr) return;
    ANIMSCRIPT_tag* s = anim_of(obj)->scripts;
    if (s == nullptr || (s->flags & 0x10000000u) != 0) return;
    if (dir == 1) {
        s->speed = -std::fabs(s->speed);
    } else if (dir == 0) {
        s->speed = std::fabs(s->speed);
    } else if (dir == 2) {
        s->speed = -s->speed;
    }
}

// [decomp] 0x001c6610 AnimObjectSetSleeveType
// A manga dos braços da arma na mão: o primeiro modelo da pele vira a manga carregada a partir de
// "type" (a mesma busca de AnimSleeveGetEntity). Só no port: sem objeto de animação (a pele da
// arma não está carregada nesta fase, AnimObjectNew falhou) não faz nada; o original escreveria em
// 0xa8.
void AnimObjectSetSleeveType(obj_tag* obj, uint32_t type) {
    const uint32_t h = AnimSleeveGetEntity(type);
    if (h == 0) return;
    celglist_tag* g = hashtable_hashcode_to_celglist(h);
    if (g != nullptr && anim_of(obj) != nullptr) anim_of(obj)->mesh_override = g;
}

// [decomp] 0x001c14f0 AnimScriptDelete
// Avisa (on_delete), solta o dono, apaga as sequências e tira da lista. Apagar o script em
// atualização marca Glb_bScriptDeleted.
void AnimScriptDelete(obj_tag* obj, P32<ANIMSCRIPT_tag>* ps) {
    ANIMSCRIPT_tag* s = *ps;
    if (obj == nullptr || anim_of(obj) == nullptr || s == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (s == Glb_pCurrentScript) Glb_bScriptDeleted = 1;
    if (s->on_delete != nullptr) s->on_delete(obj, anim_state(anim), s);
    if (s->owner != nullptr && s->owner != ps) *s->owner = nullptr;
    for (int i = 1; i >= 0; --i) {
        if (s->seq[i] != nullptr) AnimSeqDelete(&s->seq[i]);
    }
    ANIMSCRIPT_tag* prev = s->prev;
    ANIMSCRIPT_tag* next = s->next;
    if (prev == nullptr) anim->scripts = next;
    else prev->next = next;
    if (next != nullptr) next->prev = prev;
    s->magic = 0x983c4837u;
    Mem_Free(ps);
}

#if NF_PTR64
void AnimScriptDelete(obj_tag* obj, ANIMSCRIPT_tag** ps) {
    P32<ANIMSCRIPT_tag> slot = *ps;
    AnimScriptDelete(obj, &slot);
    *ps = slot;
}
#endif

// [decomp] 0x001c70d8 AnimScriptIsStopped
// Algum script já terminou?
bool AnimScriptIsStopped(obj_tag* obj) {
    int32_t running = 0;
    AnimListBuild(obj, 1, 0, nullptr, 1);
    for (int32_t i = 0; i < GlbAnimList; ++i) {
        if ((GlbAnimListItems[i]->flags & 0x10000000u) == 0) ++running;
    }
    return running != GlbAnimList;
}

// [decomp] 0x001c1930 AnimScriptEnd
// Leva o quadro para dentro de 1..frame_count conforme o modo de fim: parar (marca terminado,
// liga done_flag e chama on_end), repetir, ou ir e voltar (inverte a velocidade).
float AnimScriptEnd(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, float frame) {
    if (int32_t(s->flags) < 0) s->end_mode = 1;
    const float frames = float(int32_t(s->frame_count));
    if (1.0f <= frame && frame <= frames) {
        s->frame = frame;
        return frame;
    }
    const float speed = s->speed;
    const bool back = speed < 0.0f;
    switch (s->end_mode) {
        case 0:
        case 3:
            s->flags |= 0x10000000u;
            frame = back ? 1.0f : frames;
            if (s->done_flag != nullptr) {
                *s->done_flag = 1;
                s->done_flag = nullptr;
            }
            if (s->on_end != nullptr) {
                s->on_end(obj, state, s);
                if (Glb_bScriptDeleted != 0) return 1.0f;
                s->on_end = nullptr;
            }
            break;
        case 1:
            frame = back ? frame + frames : frame - frames;
            s->flags |= 0x40000000u;
            break;
        case 2:
            frame = (back ? 1.0f : frames) + -speed;
            s->speed = -speed;
            break;
        default:
            break;
    }
    if (frame < 1.0f) frame = 1.0f;
    else if (frames < frame) frame = frames;
    s->frame = frame;
    return frame;
}

// [decomp] 0x001c2630 AnimScriptTick
// Avança o script um tick: os comandos do quadro inicial (na primeira vez), a velocidade conforme
// a sincronia (0 tempo, 1 distância andada, 2 fase de outro script, 3 espelho), o fim e os
// comandos entre o quadro anterior e o novo. Apaga o script terminado no modo de fim 3.
void AnimScriptTick(obj_tag* obj, void* state, ANIMSCRIPT_tag* s) {
    float speed = s->speed;
    const uint32_t started = s->flags & 0x20000000u;
    s->flags = (s->flags & 0xbfffffffu) | 0x800000u;
    ANIMSCRIPT_tag* cur = s;
    if (started != 0) AnimProcessScriptCmds(obj, state, s, int32_t(s->prev_frame), int32_t(s->frame));
    bool add = started == 0;
    const uint8_t mode = cur->sync_mode;
    if (mode == 1) {
        cur->flags |= 0x80000000u;
        speed = AnimDistanceTableDistanceToFrame2(obj, state, cur);
        cur->speed = speed;
    } else if (mode == 0) {
        speed *= FRAME_RATE_MUL;
    } else if (mode == 2) {
        ANIMSCRIPT_tag* ref = cur->sync;
        const float ref_phase = (ref->frame - 1.0f) / float(int32_t(ref->frame_count) - 1);
        const float phase = (cur->frame - 1.0f) / float(int32_t(cur->frame_count) - 1);
        float d = ref_phase - phase;
        if (0.0f <= ref->speed) {
            if (d < 0.0f) d = (1.0f - phase) + ref_phase;
        } else if (1.0f <= d) {
            d = (phase + 1.0f) - ref_phase;
        }
        speed = d * float(int32_t(cur->frame_count) - 1) + 1.0f;
        cur->speed = speed;
        add = true;
    } else if (mode == 3) {
        speed = float(int32_t(cur->frame_count) + 1) * cur->unknown_8c - cur->frame;
        cur->speed = speed;
        add = true;
    }
    const float from = cur->prev_frame;
    float to = cur->frame;
    if (add) to += speed;
    const float reached = AnimScriptEnd(obj, state, cur, to);
    if (Glb_bScriptDeleted == 0) {
        AnimProcessScriptCmds(obj, state, cur, int32_t(from), int32_t(reached));
        if (cur->end_mode == 3 && (cur->flags & 0x10000000u) != 0) AnimScriptDelete(obj, &cur);
    }
}

// ------------------------------------------------------------------ distância (passos)

namespace {
struct DistanceTable {
    uint32_t seq;
    float* table;
};
DistanceTable DistanceTables[32];  // 0x00319c80
}  // namespace

// [decomp] 0x001c4060 AnimDistanceTableCreate
// Distância acumulada da raiz quadro a quadro (só nos eixos dos bits de axes: 1 x, 2 y, 4 z; um
// passo menor que 1e-5 conta 0,01). Sem "open", o último passo é do quadro 1 ao 2 (ciclo).
// Tabela: [0] total, [1] quadros, [2] 0, [3...] acumulado por quadro.
float* AnimDistanceTableCreate(uint32_t hash, int32_t axes, int32_t open) {
    alignas(16) ANIMSEQ_tag seq;
    alignas(16) static uint8_t a[0xc60];
    alignas(16) static uint8_t b[0xc60];
    std::memset(&seq, 0, sizeof(seq));
    if ((hash & 0xff000000u) != 0x4000000u) return nullptr;
    AnimSeqGetInfo(hash, &seq, nullptr);
    seq.first_frame = 1;
    seq.last_frame = seq.frame_count;
    float frames = float(int32_t(seq.frame_count));
    if (open != 0) frames = float(int32_t(seq.frame_count) - 1);
    auto* t = static_cast<float*>(Mem_Malloc(uint32_t(int32_t(frames)) * 4 + 0xc, 0xc04, 0));
    if (t == nullptr) return nullptr;
    float total = 0.0f;
    const int32_t n = int32_t(frames);
    for (int32_t i = 1; i <= n; ++i) {
        int32_t f = i + 1;
        if (open == 0 && n < f) f = 2;
        AnimFrameCopy(a, &seq, nullptr, float(f));
        _VECTOR cur;
        std::memcpy(&cur, a, 16);
        AnimFrameCopy(b, &seq, nullptr, float(f - 1));
        _VECTOR prev;
        std::memcpy(&prev, b, 16);
        _VECTOR d{cur.x - prev.x, cur.y - prev.y, cur.z - prev.z, cur.w - prev.w};
        if ((axes & 1) == 0) d.x = 0.0f;
        if ((axes & 2) == 0) d.y = 0.0f;
        if ((axes & 4) == 0) d.z = 0.0f;
        float m = Vec_Magnitude(&d);
        if (m < 1e-05f) m = 0.01f;
        total += m;
        t[i + 2] = total;
    }
    t[0] = total;
    t[1] = frames;
    t[2] = 0.0f;
    return t;
}

// [decomp] 0x001c76f0 AnimDistanceTableGet
// A tabela da sequência (criada na primeira vez, até 32; só no eixo z).
void* AnimDistanceTableGet(uint32_t seq) {
    for (int32_t i = 0; i < nDistanceTables; ++i) {
        if (DistanceTables[i].seq == seq) return DistanceTables[i].table;
    }
    if (nDistanceTables >= 32) return nullptr;
    float* t = AnimDistanceTableCreate(seq, 4, 0);
    DistanceTables[nDistanceTables].seq = seq;
    DistanceTables[nDistanceTables].table = t;
    ++nDistanceTables;
    return t;
}

// [decomp] 0x001c4270 AnimDistanceTableDistanceToFrame2
// Sincronia por distância: avança a posição no ciclo pelo que o objeto andou e devolve quantos
// quadros o script deve andar para os pés acompanharem o chão.
float AnimDistanceTableDistanceToFrame2(obj_tag* obj, void* state, ANIMSCRIPT_tag* s) {
    if (obj == nullptr || anim_of(obj) == nullptr || s->seq[0] == nullptr) return 0.0f;
    const float* t = static_cast<const float*>(s->seq[0]->distance);
    if (t == nullptr) return 0.0f;
    ANIMOBJ_tag* anim = anim_from_state(state);
    const float total = t[0];
    const float frames = t[1];
    float d = anim->walk_pos + anim->walk_dir;
    while (total <= d) d -= total;
    while (d < 0.0f) d += total;
    int32_t i = int32_t((d * float(int32_t(frames))) / total);
    if (d < t[i + 3]) {
        if (i != 0) {
            while (d < t[i + 2]) {
                --i;
                if (i == 0) break;
            }
        }
    } else {
        do {
            ++i;
        } while (t[i + 3] <= d);
    }
    float span = t[i + 3];
    float into = d;
    if (0 < i) {
        span -= t[i + 2];
        into = d - t[i + 2];
    }
    anim->walk_pos = d;
    const float frame = s->frame;
    const float target = float(i + 1) + into / span;
    const float step = target - frame;
    if (0.0f <= anim->walk_dir) {
        if (step < 0.0f) return (float(int32_t(frames)) - frame) + target;
    } else if (0.0f <= step) {
        return -((float(int32_t(frames)) - target) + frame);
    }
    return step;
}

// ------------------------------------------------------------------ comandos dos scripts

namespace {
// O quadro andou de "from" a "to" (no sentido de "forward", dando a volta no fim) e passou pelo
// quadro f? Como o original: parado em f também conta.
bool crossed_point(int32_t from, int32_t to, int32_t f, bool forward) {
    if (!(from < f || f < from)) return true;
    if (!(to < f || f < to)) return true;
    if (!forward) {
        if (from < to) return f <= from || to <= f;
        return f <= from && to <= f;
    }
    if (to < from) return from <= f || f <= to;
    return from <= f && f <= to;
}

// O mesmo para o intervalo [lo, hi] (comando de sequência).
bool crossed_range(int32_t from, int32_t to, int32_t lo, int32_t hi, bool forward) {
    if (!(from < lo || hi < from)) return true;
    if (!(to < lo || hi < to)) return true;
    if (!forward) {
        if (from < to) {
            if (from < lo && lo < to && from < hi) return !(hi < to);
            return true;
        }
        if (from < lo || lo < to) {
            if (from < hi) return false;
            return !(hi < to);
        }
        return true;
    }
    if (to < from) {
        if (lo < from && to < lo && hi < from) return !(to < hi);
        return true;
    }
    if (lo < from || to < lo) {
        if (hi < from) return false;
        return !(to < hi);
    }
    return true;
}
}  // namespace

uint8_t ScriptFootstepFoot = 0;
uint8_t ScriptFootstepType = 0;

// [decomp] 0x001c7900 AnimDatumGetWeaponInfo
// Da arma do objeto (anim weapon < 0x73): osso do cano (0xff = nenhum), entidades do clarão e o
// tipo de tiro, de weapon_data.
int32_t AnimDatumGetWeaponInfo(obj_tag* obj, uint32_t* muzzle, uint32_t* flash, uint16_t* kind) {
    const uint8_t w = uint8_t(anim_of(obj)->weapon);
    if (0x72 < w) return -1;
    const uint8_t* d = weapon_data + int32_t(int8_t(w)) * 0x10c;
    if (muzzle != nullptr) *muzzle = rd<uint32_t>(d + 0x80);
    if (flash != nullptr) *flash = rd<uint32_t>(d + 0x4c);
    if (kind != nullptr) *kind = rd<uint16_t>(d + 0x60);
    const uint32_t bone = d[0x50];
    return bone == 0xff ? -1 : int32_t(bone);
}

// [decomp] 0x001c77f8 AnimDatumGetIndex
// O anexo da pele preso ao osso "bone" (os anexos guardam o osso na primeira palavra), ou -1.
int32_t AnimDatumGetIndex(obj_tag* obj, int32_t bone) {
    if (obj == nullptr || anim_of(obj) == nullptr) return -1;
    const ANIMSKIN_tag* skin = anim_of(obj)->skin;
    for (int32_t i = 0; i < skin->n_extra; ++i) {
        if (rd<int32_t>(skin->extras + i * 0x24) == bone) return i;
    }
    return -1;
}

// [decomp] 0x001c7848 AnimDatumSetEntity
// Põe a entidade "hash" no anexo do osso "bone" por "ticks" desenhos (0xff = sempre); hash 0 tira.
void AnimDatumSetEntity(obj_tag* obj, int32_t bone, uint32_t hash, uint32_t ticks) {
    if (obj == nullptr || anim_of(obj) == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    const int32_t i = AnimDatumGetIndex(obj, bone);
    if (i < 0) return;
    if (hash == 0) {
        anim->extra_timers[i] = 0;
    } else {
        anim->extra_glists[i] = hashtable_hashcode_to_celglist(hash);
        anim->extra_timers[i] = int8_t(ticks);
    }
}

// [decomp] 0x001c6490 AnimFacialAppend
// O script de rosto de uma fala (o som da fala é o comando 1 dele): no fim da lista do objeto, com a marca 0xe0000002
// (a dos rostos, que AnimFacialUpdate espera acabar), fim 3 (para e se apaga), e o estado do rosto 2 (falando).
// Divergência: o original não confere o script (lista cheia, 32 scripts, ou objeto sem animação: escreveria no
// endereço 0xb5); aqui devolve nulo sem mexer em nada.
ANIMSCRIPT_tag* AnimFacialAppend(obj_tag* obj, uint32_t script) {
    if (obj == nullptr || anim_of(obj) == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    ANIMSCRIPT_tag* s = AnimScriptAppend(obj, script);
    if (s == nullptr) return nullptr;
    s->end_mode = 3;
    s->tag = 0xe0000002u;
    anim->face_state = 2;
    return s;
}

// Os scripts de rosto (Male/Female_Facial_State_Anims, .data do ACTION.ELF): a expressão por estado de alerta
// (0 a 3, cada um com uma sequência de rosto de 2 ou 3 quadros parada no primeiro) e a piscada (10 quadros nos
// homens, 12 nas mulheres).
const uint32_t Male_Facial_State_Anims[5] = {0x60000f8, 0x60000f9, 0x60000f5, 0x60000f6, 0x60000f3};
const uint32_t Female_Facial_State_Anims[5] = {0x600036d, 0x600036f, 0x600036a, 0x600036b, 0x6000370};
uint32_t g_anim_face_guards = 0;

// [decomp] 0x001be310 AnimFacialUpdate
// Só fora de cena e sem a flag 0x10000. Estado 0: começa a expressão parada (marca 0xe0000000, velocidade 0, fim 3)
// e a primeira piscada em 1,5 a 4,5 s. Estado 1 (parado): se o alerta do personagem mudou, troca a expressão
// (apaga os scripts de rosto, os com a flag 0x40000); senão, fora do alerta 3, conta até a piscada (marca
// 0xe0000001, fim 3), a próxima em 1 a 3 s (2 a 4 s com alerta 0); só nas mulheres, metade das vezes nada mais, um
// quarto pisca de novo em 0,1 a 0,2 s (bit 0x80, apagado na seguinte) e um quarto pisca devagar (velocidade 0,2 a
// 0,4). Estado 2 (falando): quando não há mais script de fala (0xe0000002), volta a 1 e marca o alerta como mudado
// (a expressão é refeita no tick seguinte).
// Divergência: o original escreve no script devolvido por AnimScriptAppend sem conferir (nulo com 32 scripts na
// lista: escreveria em endereços baixos); aqui o caso é contado em g_anim_face_guards.
void AnimFacialUpdate(obj_tag* obj, void* state) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    if ((anim->anim_flags & 0x10000u) != 0 || ScriptCam != 0) return;
    const uint32_t male = (anim->anim_flags >> 10) & 1;
    const uint32_t* anims = male != 0 ? Male_Facial_State_Anims : Female_Facial_State_Anims;
    const auto append = [&](uint32_t script) {
        ANIMSCRIPT_tag* s = AnimScriptAppend(obj, script);
        if (s == nullptr) ++g_anim_face_guards;
        return s;
    };
    const uint8_t st = anim->face_state & 0x7f;
    if (st == 1) {
        bool blinks = true;  // fora do alerta 3 (ou sem estado de alerta)
        bool alert = true;   // alerta diferente de 0 (ou sem estado): piscadas mais seguidas
        uint8_t* status = Drone_AlertStatus(obj);
        if (status != nullptr) {
            if (int8_t(status[2]) != 0) {
                status[2] = 0;
                AnimListBuild(obj, 0x800, 0, nullptr, 1);
                AnimListDelete(obj);
                ANIMSCRIPT_tag* s = append(anims[status[0]]);
                if (s == nullptr) return;
                s->tag = 0xe0000000u;
                s->end_mode = 3;
                s->speed = 0.0f;
                return;
            }
            alert = status[0] != 0;
            blinks = status[0] != 3;
        }
        if (!blinks) return;
        anim->face_timer = int16_t(anim->face_timer - 1);
        if (0 <= anim->face_timer) return;
        if (!alert) {
            const uint32_t r = Rand_Rand(FRAME_RATE_INT << 1);
            anim->face_timer = int16_t(r + (FRAME_RATE_INT << 1));
        } else {
            const uint32_t r = Rand_Rand(FRAME_RATE_INT << 1);
            anim->face_timer = int16_t(uint16_t(FRAME_RATE_INT) + r);
        }
        ANIMSCRIPT_tag* s = append(anims[4]);
        if (s == nullptr) return;
        s->end_mode = 3;
        s->tag = 0xe0000001u;
        if (male != 0) return;
        if ((anim->face_state & 0x80) != 0) {
            anim->face_state &= 0x7f;
            return;
        }
        const uint32_t r = Rand_Rand(100);
        if (r < 0x33) return;
        if (r < 0x4c) {
            const uint32_t extra = Rand_Rand(FRAME_RATE_INT / 10);
            anim->face_state |= 0x80;
            anim->face_timer = int16_t(extra + FRAME_RATE_INT / 10);
        } else {
            s->speed = Rand_FRand(0.2f) + 0.2f;
        }
    } else if (st == 0) {
        ANIMSCRIPT_tag* s = append(anims[0]);
        if (s != nullptr) {
            s->tag = 0xe0000000u;
            s->end_mode = 3;
            s->speed = 0.0f;
        }
        anim->face_state = 1;
        const uint32_t r = Rand_Rand(FRAME_RATE_INT * 3);
        anim->face_timer = int16_t(r + ((FRAME_RATE_INT * 3) >> 1));
    } else if (st == 2) {
        if (AnimListBuild(obj, 0x200, 0xe0000002u, nullptr, 1) == nullptr) {
            anim->face_state = 1;
            uint8_t* status = Drone_AlertStatus(obj);
            if (status != nullptr) status[2] = 1;
        }
    }
}

// [decomp] 0x001be648 AnimFacialBlend
// Os pesos dos morphs a partir dos quadros de rosto (o slot 1) do primeiro script de cada marca. Fora de cena: a
// expressão (0xe0000000) copiada inteira, por cima a piscada (0xe0000001) e a fala (0xe0000002) onde o peso passa
// de 0,01 em módulo, e por fim os olhos (ANIMOBJ_tag::eye_x/eye_y em 0..255): alvo 14 = eye_x / 255, 13 = 1 -
// eye_x / 255, 11 = eye_y / 255, 12 = 1 - eye_y / 255, cada um só se passar de 0,01 (com eye_x e eye_y em 0, sem
// acompanhar a cabeça, os alvos 13 e 12 ficam em 1, como no original). Numa cena (ScriptCam), só a fala, inteira:
// todo script novo de uma pele com morphs é marcado 0xe0000002 durante a cena (AnimScriptInit).
void AnimFacialBlend(obj_tag* obj, void* state, ANIMSKIN_tag* skin) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    ANIMSCRIPT_tag* base = AnimListBuild(obj, 0x200, 0xe0000000u, nullptr, 1);
    ANIMSCRIPT_tag* blink = AnimListBuild(obj, 0x200, 0xe0000001u, nullptr, 1);
    ANIMSCRIPT_tag* talk = AnimListBuild(obj, 0x200, 0xe0000002u, nullptr, 1);
    const uint32_t n = skin->n_morphs;
    float* w = anim->morph_weights;
    const auto frame_of = [](ANIMSCRIPT_tag* s) { return reinterpret_cast<const float*>(nf::raw(s->seq[1]->frame)); };
    if (ScriptCam != 0) {
        if (talk == nullptr) return;
        for (uint32_t i = 0; i < n; ++i) w[i] = frame_of(talk)[i];
        return;
    }
    if (base != nullptr) {
        for (uint32_t i = 0; i < n; ++i) w[i] = frame_of(base)[i];
    }
    for (ANIMSCRIPT_tag* s : {blink, talk}) {
        if (s == nullptr) continue;
        for (uint32_t i = 0; i < n; ++i) {
            const float v = frame_of(s)[i];
            if (0.01f < std::fabs(v)) w[i] = v;
        }
    }
    const float k = 0.003921569f;  // 1 / 255
    const float x = float(int32_t(anim->eye_x)) * k;
    if (0.01f < x) w[14] = x;
    const float x2 = float(int32_t(0xff - uint32_t(anim->eye_x))) * k;
    if (0.01f < x2) w[13] = x2;
    const float y = float(int32_t(anim->eye_y)) * k;
    if (0.01f < y) w[11] = y;
    const float y2 = float(int32_t(0xff - uint32_t(anim->eye_y))) * k;
    if (0.01f < y2) w[12] = y2;
}

// [decomp] 0x001c66f8 AnimObjectIsClose
bool AnimObjectIsClose(obj_tag* obj, uint32_t limit) {
    if (ScriptCam != 0) return true;
    const uint8_t* c = anim_of(obj)->closeness;
    uint32_t m = c[0];
    for (int i = 1; i < 4; ++i) m = c[i] < m ? c[i] : m;
    return m <= limit;
}

// [decomp] 0x001c1b38 AnimProcessScriptCmds
// Executa os comandos do script cujo quadro foi atravessado de "from" a "to": 0 tocar uma
// sequência num intervalo de quadros (a do rosto só se o script deixar), 1 som 3D (alguns trocados
// conforme a arma do dono; na arma do jogador sai da ponta do cano), 4 eventos (passos, chamadas,
// parar som, efeito, tiro). Um bit por comando (active_cmds) evita repetir enquanto o quadro está
// dentro dele. Com a flag 0x40000 do objeto os sons e eventos não tocam.
void AnimProcessScriptCmds(obj_tag* obj, void* state, ANIMSCRIPT_tag* s, int32_t from, int32_t to) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    int32_t hit = 0;
    int32_t started = 0;
    const bool forward = 0.0f <= s->speed;
    const bool muted = ((int32_t(anim->anim_flags) >> 0x12) & 1) != 0;
    const uint16_t* cmd = reinterpret_cast<const uint16_t*>(s->data + 8);
    for (int32_t n = s->unknown_b2 - 1; 0 <= n; --n) {
        const uint32_t bit = 1u << (n & 0x1f);
        uint32_t words = cmd[0] >> 8;
        const bool active = (s->active_cmds & bit) != 0;
        switch (cmd[0] & 0xff) {
            case 0: {
                hit = crossed_range(from, to, cmd[1], cmd[2], forward) ? 1 : 0;
                if (hit != 0 && !active) {
                    const uint32_t hash = uint32_t(cmd[3]) | 0x4000000u;
                    auto* raw = static_cast<ANIMRAWSEQ_tag*>(hashtable_getitem(hash));
                    if (raw == nullptr) return;
                    const int32_t face = (int32_t(raw->flags) >> 3) & 1;
                    if (face == 0 || (s->flags & 0x80000u) == 0) {
                        const int32_t at = (forward ? to : from) - int32_t(cmd[1]);
                        ANIMSEQ_tag* seq = AnimSeqSet(obj, state, s, raw, hash, int32_t(float(at + 1)), face);
                        if (seq == nullptr) {
                            pending_call("AnimProcessScriptCmds (sequência sem slot)");
                        } else {
                            seq->start = int16_t(cmd[1]);
                            s->flags &= 0xfffbffffu;
                            if (started == 0 && face != 0) s->flags |= 0x40000u;
                            seq->flags &= 0xffdfffffu;
                            const bool cache = (s->flags & 0x10000u) != 0 && face == 0 && 0x100 < seq->frame_count;
                            if (cache) {
                                seq->flags |= 0x200000u;
                                if (seq->cursors == nullptr) {
                                    const uint32_t size = uint32_t(seq->channels) << 3;
                                    seq->cursors = static_cast<uint32_t*>(Mem_Malloc(size, 0x3d04, 0));
                                    if (seq->cursors != nullptr) std::memset(seq->cursors, 0, size);
                                }
                            }
                        }
                        ++started;
                    }
                }
                words += 4;
                break;
            }
            case 1: {
                const int32_t f = cmd[1];
                if ((s->flags & 0x10000u) != 0 && uint32_t(f - int32_t(s->frame)) < 0x33) SFXPreCache(cmd[2], f - int32_t(s->frame));
                hit = crossed_point(from, to, f, forward) ? 1 : 0;
                if (hit != 0 && !muted && !active) {
                    uint32_t id = cmd[2];
                    obj_tag* owner = obj->owner;
                    const auto owner_weapon = [&] { return int8_t(anim_of(owner)->weapon); };
                    if (id == 1 && owner != nullptr && (owner_weapon() == 7 || owner_weapon() == 9)) id = 2;
                    if (id == 0x403 && owner != nullptr && (owner_weapon() == 3 || owner_weapon() == 5)) id = 2;
                    if (id == 0xfa && owner != nullptr && owner_weapon() == 0x14) id = 0x1fb;
                    if (id == 0x106 && owner != nullptr && uint8_t(owner_weapon() - 0x1e) < 6) id = 0x284;
                    if (id == 0x1e1 && owner != nullptr && owner_weapon() == 0x10) id = 0x602;
                    if (id == 4 && owner != nullptr && uint8_t(owner_weapon() - 0x16) < 2) id = 0x604;
                    _VECTOR pos;
                    if (owner == nullptr) {
                        pos = obj->pos;
                    } else {
                        const MATRIX& m = owner->matrix;
                        const _VECTOR& p = obj->pos;
                        pos = {m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + owner->pos.x,
                               m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + owner->pos.y,
                               m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + owner->pos.z, p.w + owner->pos.w};
                        const uint8_t* od = static_cast<const uint8_t*>(owner->data);
                        if (owner->type == 3 && obj == rd<P32<obj_tag>>(od + 0x7e8)) {
                            if (owner_weapon() == 1 || AnimDatumGetIndex(obj, 0) < 0) {
                                const MATRIX& g = obj->matrix;
                                pos.x += g.m[2][0] * 1.5f;
                                pos.y += g.m[2][1] * 1.5f;
                                pos.z += g.m[2][2] * 1.5f;
                            } else {
                                AnimGetBoneWorldTrans(obj, 0, 0, &pos, nullptr);
                            }
                        }
                    }
                    const int16_t pitch = int16_t(Rand_Rand(500));
                    Sound_Play3D(id, &pos, 100.0f, 100.0f, 0.0f, int16_t(pitch - 0xfa), 0, 0);
                }
                words += 3;
                break;
            }
            case 2:
                words += 3;
                break;
            case 4: {
                hit = crossed_point(from, to, cmd[1], forward) ? 1 : 0;
                if (hit != 0 && !muted && !active) {
                    switch (cmd[2]) {
                        case 0:
                            ScriptFootstepFoot = ScriptFootstepFoot == 0 ? 1 : 0;
                            ScriptFootstepType = (s->hash == 0x6000099 || s->hash == 0x60000ae) ? 1 : 2;
                            break;
                        case 1:
                            if (obj->owner != nullptr) {
                                uint8_t* od = static_cast<uint8_t*>(obj->owner->data);
                                od[0x964] = od[0x964] == 0 ? 1 : 0;
                            }
                            break;
                        case 2:
                            if (s->event_fn != nullptr) s->event_fn(obj, state, s, cmd[3]);
                            break;
                        case 3:
                            Sound_StopAllWithId(cmd[3]);
                            break;
                        case 4:
                        case 5:
                            ScriptFootstepFoot = cmd[2] == 5 ? 1 : 0;
                            ScriptFootstepType = (s->hash == 0x6000099 || s->hash == 0x60000ae) ? 1 : 2;
                            break;
                        case 6:
                            Effect_Create(&anim->floor, obj, 0xc0, 1.0f);
                            break;
                        case 7:
                            AnimFireWeapon(obj);
                            break;
                        default:
                            break;
                    }
                }
                words += 3;
                break;
            }
            default:  // 3 e acima de 4: só o tamanho; o bit segue o comando anterior
                break;
        }
        cmd += words;
        if (hit == 0) s->active_cmds &= ~bit;
        else s->active_cmds |= bit;
    }
}

// Comando 4 tipo 7: o tiro de uma arma animada (o dono, ou o próprio objeto). Faz parte de
// AnimProcessScriptCmds no original; separado aqui só para ler melhor.
void AnimFireWeapon(obj_tag* obj) {
    obj_tag* who = obj->owner != nullptr ? nf::raw(obj->owner) : obj;
    const int32_t w = int32_t(anim_of(who)->weapon) * 0x10c;
    uint32_t flash = 0;
    uint16_t kind = 0;
    const int32_t bone = AnimDatumGetWeaponInfo(who, nullptr, &flash, &kind);
    if (bone < 0) return;
    _VECTOR muzzle;
    MATRIX m;
    AnimGetBoneWorldTrans(who, uint32_t(bone), 1, &muzzle, &m);
    // Direção do cano: a linha 2 da matriz do osso (vmulabc com (0, 0, 1), vmaddbc com vf0).
    const _VECTOR dir{m.m[2][0] + m.m[3][0], m.m[2][1] + m.m[3][1], m.m[2][2] + m.m[3][2], m.m[2][3] + m.m[3][3]};
    Bullet_init(-1, who, who, reinterpret_cast<weapon_definition_tag*>(weapon_data + w), &muzzle, &dir, kind);
    if (flash == 0) return;
    AnimDatumSetEntity(who, bone + 1, flash, 3);
    float range;
    std::memcpy(&range, weapon_data + w + 0x58, 4);
    Light_Create(&muzzle, weapon_data[w + 0x56], weapon_data[w + 0x55], weapon_data[w + 0x54], range, 0, 3, 1, 2.0f,
                 0, 0, 0, 0xffff);
    uint32_t wflags;
    std::memcpy(&wflags, weapon_data + w + 0x68, 4);
    if ((wflags & 0x2000) != 0) return;
    _VECTOR rot{0.0f, 0.0f, Rand_FRand(6.2831855f), 0.0f};
    Gas_Init(obj->cel, &muzzle, &rot, nullptr, 0.05f, 0.1f, 0x2000039, 7, 0x48403080, 1, 0x80);
}

// ------------------------------------------------------------------ pose

namespace {
// a + (b - a) * t sem passar de b (o padrão das misturas do módulo).
float lerp_to(float a, float b, float t) {
    const float d = b - a;
    const float v = a + d * t;
    return (0.0f <= d ? b < v : v < b) ? b : v;
}
}  // namespace

// [decomp] 0x001c35f8 AnimFrameResolve
// Junta as sequências dos scripts na pose do objeto (ANIMOBJ_tag::frame): primeiro mistura cada
// script de sincronia no seu script de referência; depois os scripts principais, em ordem de
// criação, cada um pesando conforme entra e sai (fade, weight); por fim os de mistura (bit 0x2000,
// por osso) e os de sobreposição (0x4000). Dá a altura da raiz, anda o objeto o quanto a raiz andou
// (girado pela rotação guardada no script com o bit 0x20000, ou pela do objeto), apaga os scripts
// que terminaram de sair e chama os ajustes de rosto e de pose.
void AnimFrameResolve(obj_tag* obj, void* state) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    ANIMSKIN_tag* skin = anim->skin;
    bool resolved = false;
    _VECTOR delta{};
    _VECTOR root{};
    if (AnimListBuild(obj, 0x100, 0, nullptr, 1) != nullptr) {
        const uint32_t bones = pSkeletons[GlbAnimListItems[0]->seq[0]->skeleton]->bone_count;
        for (int32_t i = 0; i < GlbAnimList; ++i) {
            ANIMSCRIPT_tag* s = GlbAnimListItems[i];
            uint8_t* ref = s->sync->seq[0]->frame;
            AnimFrameBlend(obj, ref, ref, s->seq[0]->frame, bones, s->sync_weight, s->blend);
        }
    }
    AnimListBuild(obj, 1, 0, nullptr, 1);
    AnimListBuild(obj, 0x6800, 0, nullptr, 2);
    const float step = FRAME_RATE_MUL;
    for (int32_t i = 0; i < GlbAnimList; ++i) {
        ANIMSCRIPT_tag* s = GlbAnimListItems[i];
        s->flags &= 0xffdfffffu;
        if (0 < s->fade) {
            s->weight += step;
        } else if (s->fade < 0) {
            s->weight -= step;
            if (s->weight <= 0.0f) s->flags |= 0x200000u;
        }
        if (s->weight < 0.0f) s->weight = 0.0f;
        else if (s->weight_max < s->weight) s->weight = s->weight_max;
        s->norm_weight = s->weight / s->weight_max;
    }
    AnimListBuild(obj, 0x100, 0, nullptr, 2);
    if (GlbAnimList != 0) {
        resolved = true;
        anim->anim_flags |= 1;
        if (GlbAnimList == 1) {
            ANIMSEQ_tag* seq = GlbAnimListItems[0]->seq[0];
            amemcpy(anim->frame, seq->frame, anim->frame_size);
            delta = seq->root_delta;
            root = seq->root;
        } else {
            for (int32_t i = 0; i < GlbAnimList - 1; ++i) {
                for (int32_t j = i + 1; j < GlbAnimList; ++j) {
                    if (GlbAnimListItems[j]->stamp < GlbAnimListItems[i]->stamp) {
                        ANIMSCRIPT_tag* t = GlbAnimListItems[i];
                        GlbAnimListItems[i] = GlbAnimListItems[j];
                        GlbAnimListItems[j] = t;
                    }
                }
            }
            const uint32_t bones = pSkeletons[GlbAnimListItems[0]->seq[0]->skeleton]->bone_count;
            const uint32_t q = bones * 0x10;
            for (int32_t i = 0; i < GlbAnimList - 1; ++i) {
                const float t = 1.0f - GlbAnimListItems[i]->norm_weight;
                uint8_t* base;
                bool root_rot = false;
                if (i == 0) {
                    ANIMSEQ_tag* seq = GlbAnimListItems[0]->seq[0];
                    delta = seq->root_delta;
                    base = seq->frame;
                    root = seq->root;
                    root_rot = ((int32_t(GlbAnimListItems[0]->flags) >> 0xf) & 1) != 0;
                } else {
                    base = anim->frame;
                }
                ANIMSCRIPT_tag* next = GlbAnimListItems[i + 1];
                ANIMSEQ_tag* nseq = next->seq[0];
                const _VECTOR nd = nseq->root_delta;
                uint8_t* other = nseq->frame;
                const _VECTOR nroot = nseq->root;
                if (root_rot) std::memcpy(base + q, other + q, 16);
                if ((next->flags & 0x8000u) != 0) std::memcpy(other + q, base + q, 16);
                AnimFrameBlend(obj, anim->frame, base, other, bones, t, nullptr);
                delta.x = lerp_to(delta.x, nd.x, t);
                delta.y = lerp_to(delta.y, nd.y, t);
                delta.z = lerp_to(delta.z, nd.z, t);
                root.x = lerp_to(root.x, nroot.x, t);
                root.y = lerp_to(root.y, nroot.y, t);
                root.z = lerp_to(root.z, nroot.z, t);
            }
        }
    }
    const auto overlay = [&](uint32_t criterion, bool with_bones) {
        if (AnimListBuild(obj, criterion, 0, nullptr, 1) == nullptr) return;
        ANIMSCRIPT_tag* s = GlbAnimListItems[0];
        uint8_t* src = s->seq[0]->frame;
        if (resolved) {
            const uint32_t bones = pSkeletons[s->seq[0]->skeleton]->bone_count;
            AnimFrameBlend(obj, anim->frame, anim->frame, src, bones, with_bones ? 1.0f : s->weight,
                           with_bones ? s->blend : nullptr);
        } else {
            resolved = true;
            anim->anim_flags |= 1;
            amemcpy(anim->frame, src, anim->frame_size);
            root = s->seq[0]->root;
        }
    };
    overlay(0x2000, true);
    overlay(0x4000, false);
    if (resolved) {
        anim->root_height = root.y + anim->root_offset;
        uint32_t mp;
        std::memcpy(&mp, MPSettings + 0x180, 4);
        if ((anim->anim_flags & 0x400) == 0 && mp != 0) anim->root_height = (root.y + anim->root_offset) * 0.8627321f;
        AnimListBuild(obj, 1, 0, nullptr, 1);
        int32_t rot = -1;
        for (int32_t i = 0; i < GlbAnimList; ++i) {
            if ((GlbAnimListItems[i]->flags & 0x20000u) != 0) {
                rot = i;
                break;
            }
        }
        const MATRIX& m = rot >= 0 ? GlbAnimListItems[rot]->rot : obj->matrix;
        _VECTOR move{m.m[0][0] * delta.x + m.m[1][0] * delta.y + m.m[2][0] * delta.z,
                     m.m[0][1] * delta.x + m.m[1][1] * delta.y + m.m[2][1] * delta.z,
                     m.m[0][2] * delta.x + m.m[1][2] * delta.y + m.m[2][2] * delta.z, delta.w};
        if (anim->root_motion != nullptr) anim->root_motion(obj, state, &move);
        obj->pos = {obj->pos.x + move.x, obj->pos.y + move.y, obj->pos.z + move.z, obj->pos.w + move.w};
        AnimListBuild(obj, 1, 0, nullptr, 1);
        AnimListBuild(obj, 0x800, 0, nullptr, 2);
        bool any = false;
        for (int32_t i = 0; i < GlbAnimList; ++i) {
            const uint32_t f = GlbAnimListItems[i]->flags;
            if ((f & 0x200000u) != 0 && (f & 0x100000u) != 0) {
                any = true;
                break;
            }
        }
        if (any) {
            for (int32_t i = 0; i < GlbAnimList; ++i) {
                ANIMSCRIPT_tag* s = GlbAnimListItems[i];
                bool gone = (s->flags & 0x200000u) != 0 && (s->flags & 0x100000u) != 0;
                if (s->fade == 0) gone = true;
                if (gone) AnimScriptDelete(obj, &s);
            }
        }
    }
    if (skin->n_morphs != 0 && AnimObjectIsClose(obj, 0xc)) AnimFacialBlend(obj, state, skin);
    if (anim->pose_fn != nullptr) {
        const uint32_t bones = pSkeletons[skin->skeleton]->bone_count;
        anim->pose_fn(obj, state, bones, anim->frame, anim->frame + bones * 0x10);
    }
}

// ------------------------------------------------------------------ objeto de animação

uint8_t Morph_Count = 0;
uint8_t Morph_Index[22];
float Morph_Weight[22];

namespace {
// Produto de Hamilton a ⊗ b em (x, y, z, w), na ordem de operações da sequência de vmr32 da VU0
// (psiBuildMatrixPalette, AnimObjectUpdate).
quaternion_tag quat_mul(const quaternion_tag& a, const quaternion_tag& b) {
    quaternion_tag r;
    r.x = ((b.x * a.w + a.x * b.w) + a.y * b.z) - a.z * b.y;
    r.y = ((b.y * a.w + a.y * b.w) + a.z * b.x) - a.x * b.z;
    r.z = ((b.z * a.w + a.z * b.w) + a.x * b.y) - a.y * b.x;
    r.w = ((b.w * a.w - a.x * b.x) - a.y * b.y) - a.z * b.z;
    return r;
}
_VECTOR& row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
}  // namespace

// [decomp] 0x001be910 AnimObjectNew
// Dá ao objeto um objeto de animação com a pele "skin" (as fases 0700000c e 07000014 trocam a
// 05000090): a pose, as matrizes dos ossos e a paleta, os anexos e os objetos rígidos presos aos
// ossos; o modelo do objeto passa a ser o primeiro da pele. Personagens (71 ossos ou mais) usam a
// luz e a sombra de personagem e ficam com a raiz na altura padrão.
uint32_t AnimObjectNew(obj_tag* obj, uint32_t skin_hash) {
    if ((skin_hash & 0xff000000u) != 0x5000000u || obj == nullptr) return 0;
    if (GameState.level_file == 0x700000c) {
        if (skin_hash == 0x5000090) skin_hash = 0x5000087;
    } else if (GameState.level_file == 0x7000014) {
        if (skin_hash == 0x5000090) skin_hash = 0x5000093;
    }
    auto* skin = static_cast<ANIMSKIN_tag*>(hashtable_getitem(skin_hash));
    if (skin == nullptr) return 0;
    auto* anim = static_cast<ANIMOBJ_tag*>(Mem_Malloc(sizeof(ANIMOBJ_tag), 0x2604, 0));
    bool ok = false;
    if (anim != nullptr) {
        obj->sphere_src = reinterpret_cast<uint8_t*>(anim);
        std::memset(anim, 0, sizeof(*anim));
        anim->skin = skin;
        const uint8_t bones = pSkeletons[skin->skeleton]->bone_count;
        obj->mode |= 8;
        anim->frame_size = uint16_t(bones << 5);
        anim->frame = static_cast<uint8_t*>(Mem_Malloc(uint32_t(bones) << 5, 0x2404, 0));
        if (anim->frame != nullptr) {
            std::memset(anim->frame, 0, anim->frame_size);
            if (skin->n_morphs != 0) {
                const uint32_t size = uint32_t(skin->n_morphs) << 2;
                anim->morph_weights = static_cast<float*>(Mem_Malloc(size, 0x3b04, 0));
                std::memset(anim->morph_weights, 0, size);
            }
            bool extras_ok = true;
            if (skin->n_extra != 0) {
                anim->extra_glists = static_cast<P32<celglist_tag>*>(Mem_Malloc(uint32_t(skin->n_extra) << 2, 0x2604, 0));
                anim->extra_timers =
                    anim->extra_glists == nullptr ? nullptr : static_cast<int8_t*>(Mem_Malloc(skin->n_extra, 0x2604, 0));
                extras_ok = anim->extra_timers != nullptr;
                for (int i = 0; extras_ok && i < skin->n_extra; ++i) {
                    anim->extra_glists[i] = nullptr;
                    anim->extra_timers[i] = 0;
                }
            }
            if (extras_ok) {
                const uint32_t mats = uint32_t(bones) << 6;
                anim->bones = static_cast<MATRIX*>(Mem_Malloc(mats, 0x2604, 0));
                anim->palette = anim->bones == nullptr ? nullptr : static_cast<MATRIX*>(Mem_Malloc(mats, 0x2604, 0));
                if (anim->palette != nullptr) {
                    if (0x46 < bones) {
                        const uint32_t f = anim->anim_flags;
                        anim->anim_flags = f | 2;
                        if (skin->skeleton != 1) anim->anim_flags = f | 0x402;
                    }
                    if (skin->n_meshes != 0) {
                        hashtable_set_object_to_entity_gfx(obj, hashtable_celglist_to_hashcode(skin->meshes[0]));
                    }
                    if ((anim->anim_flags & 2) != 0) obj->life |= 8;
                    if (skin->n_meshes != 0) obj->life |= 4;
                    ok = true;
                    if (skin->n_discrete != 0) {
                        anim->attached = static_cast<P32<obj_tag>*>(Mem_Malloc(uint32_t(skin->n_discrete) << 2, 0x2604, 0));
                        ok = anim->attached != nullptr;
                        if (ok) std::memset(anim->attached, 0, uint32_t(skin->n_discrete) << 2);
                        obj_tag* owner = skin->n_discrete * skin->n_meshes != 0 ? obj : nullptr;
                        for (int i = 0; ok && i < skin->n_discrete; ++i) {
                            obj_tag* part = Control_CreateObjEx(0, &obj->pos, nullptr, nullptr, skin->discrete[i], owner,
                                                                0, 0xc4, 1.0f, 0, 0xff, 0xff, 0xff);
                            if (part == nullptr) {
                                ok = false;
                                break;
                            }
                            part->type = 0x3d;
                            part->data = obj;
                            part->flags = (part->flags & 0xffffffdfu) | 0x8000;
                            if (skin->n_meshes != 0) part->life |= 4;
                            anim->attached[i] = part;
                        }
                    }
                    if (ok) {
                        skin->hash = skin_hash;
                        if ((anim->anim_flags & 2) != 0) {
                            const float base = (anim->anim_flags & 0x400) == 0 ? -0.995208f : -1.160398f;
                            obj->flags &= 0xffffffdfu;
                            anim->root_offset = (base - obj->glist->box_min.y / skin->scale[1]) + 0.02f;
                        }
                        return 1;
                    }
                }
            }
        }
    }
    // Falhou: solta o que conseguiu alocar.
    if (anim != nullptr) {
        if (anim->attached != nullptr) {
            for (int i = 0; i < skin->n_discrete; ++i) {
                if (anim->attached[i] != nullptr) control_delete_object(anim->attached[i]);
            }
            Mem_Free(&anim->attached);
        }
        // Na ordem do original: palette, bones, extra_timers, extra_glists, morph_weights, frame.
        if (anim->palette != nullptr) Mem_Free(&anim->palette);
        if (anim->bones != nullptr) Mem_Free(&anim->bones);
        if (anim->extra_timers != nullptr) Mem_Free(&anim->extra_timers);
        if (anim->extra_glists != nullptr) Mem_Free(&anim->extra_glists);
        if (anim->morph_weights != nullptr) Mem_Free(&anim->morph_weights);
        if (anim->frame != nullptr) Mem_Free(&anim->frame);
        Mem_Free(&obj->sphere_src);
    }
    (void)ok;
    return 0;
}

// [decomp] 0x001bee80 AnimObjectDelete
void AnimObjectDelete(obj_tag* obj) {
    if (obj == nullptr || anim_of(obj) == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    AnimSetDeleteAll(obj);
    AnimListBuild(obj, 1, 0, nullptr, 1);
    AnimListDelete(obj);
    if (anim->attached != nullptr) {
        for (uint32_t i = 0; i < anim->skin->n_discrete; ++i) {
            if (anim->attached[i] != nullptr) control_delete_object(anim->attached[i]);
        }
        Mem_Free(&anim->attached);
    }
    if (anim->palette != nullptr) Mem_Free(&anim->palette);
    if (anim->bones != nullptr) Mem_Free(&anim->bones);
    if (anim->extra_timers != nullptr) Mem_Free(&anim->extra_timers);
    if (anim->extra_glists != nullptr) Mem_Free(&anim->extra_glists);
    if (anim->morph_weights != nullptr) Mem_Free(&anim->morph_weights);
    if (anim->frame != nullptr) Mem_Free(&anim->frame);
    Mem_Free(&obj->sphere_src);
}

// [decomp] 0x001befd8 AnimObjectUpdate
// Um tick de animação: escala da pele, rosto (de perto), os scripts (comandos e quadros), os passos
// do jogador (a cada 1,65 m andados, ou 0,4125 subindo escada; marca o anim e faz o efeito ou o som),
// as sequências, a rotação de raiz dos scripts com o bit 0x20000 (gira o objeto com a animação) e a
// pose (AnimFrameResolve). Recalcula as matrizes do jogador e dos objetos presos aos ossos.
uint32_t AnimObjectUpdate(obj_tag* obj) {
    if (obj == nullptr) return 0;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim == nullptr) return 0;
    void* state = anim_state(anim);
    ANIMSKIN_tag* skin = anim->skin;
    bool rotated = false;
    anim->anim_flags &= 0xfffffff3u;
    Glb_SkinScale[0] = skin->scale[0];
    Glb_SkinScale[1] = skin->scale[1];
    Glb_SkinScale[2] = skin->scale[2];
    std::memcpy(&Glb_SkinScale[3], &skin->unknown_1c, 4);
    anim->tick = GameState.ticks;
    if ((anim->anim_flags & 0x8000) == 0) {
        Glb_InvSkinScale[2] = 1.0f;
        Glb_InvSkinScale[0] = 1.0f;
        Glb_InvSkinScale[1] = 1.0f;
    } else {
        Glb_InvSkinScale[2] = 1.0f / Glb_SkinScale[2];
        Glb_InvSkinScale[0] = 1.0f / Glb_SkinScale[0];
        Glb_InvSkinScale[1] = 1.0f / Glb_SkinScale[1];
    }
    ScriptFootstepType = 0;
    if (skin->n_morphs != 0 && (anim->anim_flags & 2) != 0 && AnimObjectIsClose(obj, 0xc)) AnimFacialUpdate(obj, state);
    for (ANIMSCRIPT_tag* s = anim->scripts; s != nullptr;) {
        if (s->data != nullptr && (s->flags & 0x10000000u) == 0) {
            Glb_bScriptDeleted = 0;
            Glb_pCurrentScript = s;
            AnimScriptTick(obj, state, s);
            // Como o original, lê o script mesmo que a atualização o tenha apagado (o bloco
            // continua no heap até ser reaproveitado).
            s->prev_frame = s->frame;
        }
        s = s->next;
    }
    anim->flags &= 0xfffb;
    bool climbing = false;
    if (obj->type == 3) {
        uint8_t* b = static_cast<uint8_t*>(obj->data);
        float stride = 1.65f;
        float step;
        std::memcpy(&step, b + 0x924, 4);
        step += std::fabs(anim->speed);
        climbing = obj->unknown_f6 == 1;
        if (climbing) stride = 0.4125f;
        const int8_t reset = int8_t(b[0x966]);
        if (reset != 0) {
            b[0x966] = uint8_t(reset - 1);
            if (reset - 1 == 0) step = stride + 0.0001f;
        }
        if (stride < step) {
            step = std::fmod(step, stride);
            anim->flags |= 4;
            ScriptFootstepFoot = ScriptFootstepFoot == 0 ? 1 : 0;
        }
        std::memcpy(b + 0x924, &step, 4);
    } else if (ScriptFootstepType != 0) {
        anim->flags |= 4;
    }
    if ((anim->flags & 4) != 0 && ScriptCam == 0) {
        const uint16_t foot = ScriptFootstepFoot != 0 ? 8 : 4;
        if (!climbing) {
            Effect_Create(&anim->floor, obj, uint16_t(foot | 0x42), 1.0f);
        } else {
            const uint32_t h = Sound_Play3D(0x286, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            Sound_SetAlertness(h, 0.0f);
        }
    }
    for (ANIMSCRIPT_tag* s = anim->scripts; s != nullptr; s = s->next) {
        for (int k = 0; k < 2; ++k) {
            ANIMSEQ_tag* seq = s->seq[k];
            if (seq != nullptr && s->data != nullptr && seq->hash != 0) AnimSeqTick(obj, state, s, seq);
        }
        if ((s->flags & 0x20000u) == 0) continue;
        ANIMSEQ_tag* body = s->seq[0];
        auto* rootq = reinterpret_cast<quaternion_tag*>(body->frame + uint32_t(pSkeletons[body->skeleton]->bone_count) * 0x10);
        const _VECTOR fwd{0.0f, 0.0f, 1.0f, 0.0f};
        _VECTOR v;
        Quat_Rot_Vec(rootq, &fwd, &v);
        const float angle = nf::fm::atan2(v.x, v.z);
        float* turn = s->turn;
        if (8.0f <= turn[0]) {
            turn[0] = angle;
            Control_GetObjMatrix(obj, &obj->matrix);
            Mat_CopyRot(&obj->matrix, &s->rot);
            turn[1] = angle;
        } else {
            rotated = true;
            obj->rot.y += Vec_AngleDifference(turn[1], angle);
            const _VECTOR e{0.0f, Vec_AngleDifference(angle, turn[0]), 0.0f, 0.0f};
            quaternion_tag q;
            Quat_EulerToQuat(&e, &q);
            const quaternion_tag r = quat_mul(q, *rootq);
            Quat_Copy(rootq, &r);
            turn[1] = angle;
        }
    }
    AnimFrameResolve(obj, state);
    for (float* s : {Glb_InvSkinScale, Glb_SkinScale}) s[0] = s[1] = s[2] = 1.0f;
    if (rotated) {
        obj->mode |= 0x20;
        Control_GetObjMatrix(obj, &obj->matrix);
        anim->anim_flags &= 0xfffffffbu;
        psiBuildMatrixPalette(obj, state, 0);
    }
    if (obj->type == 3) psiBuildMatrixPalette(obj, state, 0);
    if (skin->n_discrete != 0) {
        psiBuildMatrixPalette(obj, state, 0);
        for (uint32_t i = 0; i < skin->n_discrete; ++i) {
            obj_tag* part = anim->attached[i];
            part->mode |= 0x20;
            part->flags |= obj->flags & 0x1a030;
            part->unknown_f8 = obj->unknown_f8;
            Mat_Copy(&anim->bones[skin->discrete_bones[i]], &part->matrix);
        }
    }
    return 1;
}

// [decomp] 0x001c64e0 AnimObjectInitialPose
// Para personagens: a altura do pé (osso 0x37) na pose de pé (06000259, ou 0600035e no
// esqueleto 1) com o objeto na origem do mundo do objeto. Devolve o y do pé.
float AnimObjectInitialPose(obj_tag* obj) {
    _VECTOR foot;
    Vec_Zero(&foot);
    ANIMOBJ_tag* anim = anim_of(obj);
    if ((anim->anim_flags & 2) != 0) {
        const uint32_t script = (anim->anim_flags & 0x400) != 0 ? 0x6000259 : 0x600035e;
        ANIMSCRIPT_tag* s = AnimScriptAdd(obj, script);
        s->speed = 0.0f;
        s->flags &= 0x7fffffffu;
        AnimObjectUpdate(obj);
        AnimGetBoneWorldTrans(obj, 0x80000037, 0, &foot, nullptr);
        AnimScriptDelete(obj, &s);
    }
    return foot.y;
}

// [decomp] 0x001bf618 AnimObjectHeadTrack
// Chamada por psiBuildMatrixPalette logo depois de montar o osso da cabeça (5), com anim_flags 0x10 (acompanhar,
// AnimObjectSetHeadTracking) ou 0x20 (acompanhando; sem 0x10, a cabeça volta ao meio). Fora de cena, só com o objeto
// perto (o menor byte de closeness até 0x20); mirar (0x80/0x100) desliga o 0x10. Os eixos saem da pose: "cima" é a
// direção da coluna (osso 2) à cabeça, "lado" = cima x frente (a linha 2 da matriz da cabeça). O alvo é um ponto
// (head_target) ou, com 0x40, o osso head_bone de outro objeto animado (sem animação: um metro à frente). Dentro do
// alcance (head_dist_sq), o alvo é projetado em dois planos pela cabeça (frente e cima; frente e lado); a mais de
// 0,5 m do centro dá o ângulo até a frente (acos, sinal pelo produto misto): a inclinação (só para a frente do
// plano: cosseno >= 0) e o giro. Giro preso a +-1,1 rad (0 além de 2,6), inclinação entre -1 e 0,6; durante
// head_delay ficam os atuais. head_yaw/head_pitch andam até eles por Ang_RotateToBearing (head_speed), e o quatérnio
// da cabeça no mundo passa a ser giro(cima) x inclinação(lado) x o da animação (os produtos de quatérnio da VU0,
// quat_mul). Nas peles com morphs, os olhos (eye_x, eye_y em 0..255) levam o que a cabeça ainda não girou: 0,5 +
// 2 x (alvo - atual), presos a 0..1 (eye_y invertido). Voltando ao meio (sem 0x10) e quase lá (0,01 rad), apaga
// 0x30 e zera os olhos.
// Divergência: com o alvo nulo (o original leria o endereço 0), não acompanha (conta em g_anim_face_guards).
void AnimObjectHeadTrack(obj_tag* obj, void* state, quaternion_tag* world, MATRIX* bones, uint32_t bone) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    (void)obj;
    if (ScriptCam == 0) {
        const uint8_t* c = anim->closeness;
        uint8_t m = c[0];
        for (int i = 1; i < 4; ++i) m = c[i] < m ? c[i] : m;
        if (0x20 < m) return;
    }
    if ((anim->anim_flags & 0x180u) != 0) anim->anim_flags &= ~0x10u;
    MATRIX* head = &bones[bone];
    _VECTOR* pos = &row3(*head);
    const uint32_t flags = anim->anim_flags;
    anim->anim_flags = flags | 0x20u;
    uint32_t mode = (flags & 0x10u) != 0 ? 1u : 0u;
    if ((flags & 0x20u) != 0) mode |= 2u;
    alignas(16) _VECTOR fwd;
    std::memcpy(&fwd, &head->m[2][0], 16);
    alignas(16) _VECTOR up, side;
    Vec_UnitVector(&up, &row3(bones[2]), pos);
    Vec_CrossNormalise(&up, &fwd, &side);
    const _VECTOR* target = nullptr;
    alignas(16) _VECTOR ahead;
    if ((anim->anim_flags & 0x40u) == 0) {
        target = static_cast<const _VECTOR*>(anim->head_target);
    } else {
        auto* t = static_cast<obj_tag*>(anim->head_target);
        if (t != nullptr && t->sphere_src != nullptr) {
            target = &row3(anim_of(t)->bones[anim->head_bone]);
        } else {
            ahead = {pos->x + fwd.x, pos->y + fwd.y, pos->z + fwd.z, pos->w + fwd.w};
            target = &ahead;
        }
    }
    float pitch = 0.0f, yaw = 0.0f;
    uint32_t which = 0;
    if (mode == 0) {
        anim->head_yaw = 0.0f;
        anim->head_pitch = 0.0f;
    }
    if (mode != 2 && target == nullptr) ++g_anim_face_guards;
    if (mode != 2 && target != nullptr && Vec_SqDist3D(pos, target) <= anim->head_dist_sq) {
        // O ângulo do alvo até a frente, no plano da cabeça com a frente e `axis`; `sign` dá o lado. Devolve 0 se o
        // alvo projetado está a menos de 0,5 m da cabeça.
        const auto angle_to = [&](const _VECTOR& axis, const _VECTOR& sign, bool front_only, float* out) {
            alignas(16) const _VECTOR front{pos->x + fwd.x, pos->y + fwd.y, pos->z + fwd.z, pos->w + fwd.w};
            alignas(16) const _VECTOR other{pos->x + axis.x, pos->y + axis.y, pos->z + axis.z, pos->w + axis.w};
            alignas(16) plane_equ_tag plane;
            Plane_PlaneEq(&plane, pos, &other, &front);
            alignas(16) _VECTOR proj, fproj, u1, u2;
            Plane_PutPntOnPlane(&proj, target, &plane);
            if (!(0.25f <= Vec_SqDist3D(pos, &proj))) return false;
            Plane_PutPntOnPlane(&fproj, &front, &plane);
            Vec_UnitVector(&u1, pos, &fproj);
            Vec_UnitVector(&u2, pos, &proj);
            float dot = (u2.x * u1.x + u2.y * u1.y) + u2.z * u1.z;
            if (front_only && !(0.0f <= dot)) return true;
            if (dot < -1.0f) {
                dot = -1.0f;
            } else if (1.0f < dot) {
                dot = 1.0f;
            }
            float a = nf::fm::acos(dot);
            if (Vec_ScalarTripleProduct(&u1, &u2, &sign) < 0.0f) a = -a;
            *out = a;
            return true;
        };
        if (angle_to(up, side, true, &pitch)) which = 2;
        if (angle_to(side, up, false, &yaw)) which |= 1;
    }
    if (which == 1) {
        pitch = 0.0f;
    } else if (which == 2) {
        yaw = 0.0f;
    }
    const float yaw_raw = yaw, pitch_raw = pitch;
    float yaw_t;
    if (2.6f < std::fabs(yaw)) {
        yaw_t = 0.0f;
    } else {
        yaw_t = yaw < -1.1f ? -1.1f : (1.1f < yaw ? 1.1f : yaw);
    }
    float pitch_t = pitch < -1.0f ? -1.0f : (0.6f < pitch ? 0.6f : pitch);
    if (anim->head_delay != 0) {
        yaw_t = anim->head_yaw;
        anim->head_delay = int8_t(anim->head_delay - 1);
        pitch_t = anim->head_pitch;
    }
    const float pi = 3.1415927f;
    const float new_yaw = Ang_RotateToBearing(anim->head_yaw, yaw_t, anim->head_speed, pi);
    const float new_pitch = Ang_RotateToBearing(anim->head_pitch, pitch_t, anim->head_speed, pi);
    anim->head_yaw = new_yaw;
    anim->head_pitch = new_pitch;
    alignas(16) quaternion_tag qy{up.x, up.y, up.z, new_yaw};
    Quat_AxisAngleToQuat(&qy, &qy);
    alignas(16) quaternion_tag qp{side.x, side.y, side.z, new_pitch};
    Quat_AxisAngleToQuat(&qp, &qp);
    const quaternion_tag turn = quat_mul(qy, qp);
    alignas(16) const quaternion_tag nq = quat_mul(turn, world[bone]);
    Quat_Copy(&world[bone], &nq);
    Quat_QuatToMat(&nq, head);
    if (anim->skin->n_morphs != 0) {
        const float dx = yaw_raw - new_yaw;
        float x = (dx + dx) + 0.5f;
        x = x < 0.0f ? 0.0f : (1.0f < x ? 1.0f : x);
        const float dy = pitch_raw - new_pitch;
        float y = (dy + dy) + 0.5f;
        anim->eye_x = uint8_t(int32_t(x * 255.0f));
        y = y < 0.0f ? 0.0f : (1.0f < y ? 1.0f : y);
        anim->eye_y = uint8_t(int32_t((1.0f - y) * 255.0f));
    }
    if (mode == 2 && std::fabs(new_yaw) < 0.01f && std::fabs(new_pitch) < 0.01f) {
        anim->anim_flags &= ~0x30u;
        if (anim->skin->n_morphs != 0) {
            anim->eye_y = 0;
            anim->eye_x = 0;
        }
    }
}

uint32_t g_anim_aim_guards = 0;
bool g_anim_aim_off = false;

// [decomp] 0x001bfe58 AnimObjectAimAt
// Chamada por psiBuildMatrixPalette logo depois de montar o osso 2 (a coluna), com anim_flags 0x80 (mirar,
// AnimObjectSetAiming) ou 0x100 (mirando; sem 0x80, o tronco volta ao meio). Fora de cena, só com o objeto visto por
// alguma câmera (algum byte de closeness abaixo de 0xff). Apaga 0x1000/0x2000/0x4000 e liga 0x100. Sem arma (o osso do
// cano de weapon_data +0x50 é 0xff) não faz nada; no multijogador, também não com a arma sem o byte +0x84.
// A arma: o anexo "cano + 3" da pele (ou "cano + 1"; sem nenhum dos dois, o id 0x41, que não existe: posição zero e
// identidade) na pose da animação, sem a mira (AnimGetBoneWorldTrans2: os ossos depois do 2 ainda não foram montados);
// o cano aponta para -x da matriz dele. Os eixos saem do osso pai da coluna (a bacia): "cima" vai dele ao osso 2,
// "lado" = cima x cano. O alvo é um ponto (aim_target) ou, com 0x200, o osso aim_bone de outro objeto animado (sem
// animação: um metro à frente da bacia na direção do cano). Dentro do alcance (aim_dist_sq a partir da bacia), liga
// 0x1000 (DroneWeap_Ready2Fire passa a conferir se o cano aponta para o alvo) e mede dois ângulos, cada um num plano
// pela bacia: com cima e o cano (inclinação, em volta do lado) e com o lado e o cano (giro, em volta de cima). Em cada
// plano, projeta a arma, um ponto 100 m à frente do cano e o alvo; se a arma está mais perto da bacia que o alvo, a
// reta do cano corta a esfera (centro na bacia, raio até o alvo) num ponto, e o ângulo é o que leva esse ponto ao
// alvo (acos do produto escalar; negativo quando o produto misto com o eixo é >= 0). Fora do jogador, só alvos à
// frente (cosseno >= 0). A inclinação fica entre -0,9 e 0,9 rad (jogador: -pi/2 e 1,2708) e liga 0x4000 quando
// presa; o giro fica acima de -0,9 e liga 0x2000. O jogador (multijogador: Player_StandAtNewPosition) só inclina.
// Alvo projetado a menos de 0,5 m da bacia num plano zera o ângulo do outro (como em AnimObjectHeadTrack).
// aim_yaw/aim_pitch andam até eles por Ang_RotateToBearing (aim_speed), e o quatérnio do osso 2 no mundo passa a ser
// giro(cima) x inclinação(lado) x o da animação; os filhos (braços, mãos, a arma) seguem. Voltando ao meio (sem
// 0x80) e quase lá (0,01 rad), apaga 0x180.
// Peculiaridade mantida: o limite superior do giro compara a inclinação (0,9 < inclinação, nunca verdade fora do
// jogador, que não gira), então o giro só é preso por baixo.
// Divergências: o w do cano é lixo da pilha no original (vmulax.xyz não escreve w); aqui 0, e nada lê w. Com o alvo
// nulo (o original leria o endereço 0; AnimObjectSetAiming nunca deixa 0x80 com alvo nulo), não mede e conta em
// g_anim_aim_guards.
void AnimObjectAimAt(obj_tag* obj, void* state, quaternion_tag* world, MATRIX* bones, uint32_t bone) {
    if (g_anim_aim_off) return;
    ANIMOBJ_tag* anim = anim_from_state(state);
    anim->anim_flags &= 0xffff8fffu;
    if (ScriptCam == 0) {
        const uint8_t* c = anim_of(obj)->closeness;
        uint8_t m = c[0];
        for (int i = 1; i < 4; ++i) m = c[i] < m ? c[i] : m;
        if (0xfe < m) return;
    }
    const uint32_t flags = anim->anim_flags;
    uint32_t mode = (flags & 0x80u) != 0 ? 1u : 0u;
    anim->anim_flags = flags | 0x100u;
    if ((flags & 0x100u) != 0) mode |= 2u;
    const int32_t datum = AnimDatumGetWeaponInfo(obj, nullptr, nullptr, nullptr);
    if (datum < 0) return;
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp != 0 && weapon_data[int32_t(anim_of(obj)->weapon) * 0x10c + 0x84] == 0) return;
    uint32_t base = uint32_t(datum);
    int32_t flag = 3;
    if (AnimDatumGetIndex(obj, datum + 3) < 0) {
        flag = 1;
        if (AnimDatumGetIndex(obj, datum + 1) < 0) base = 0x40;
    }
    alignas(16) _VECTOR gun;
    MATRIX gunm;
    AnimGetBoneWorldTrans2(obj, base, flag, &gun, &gunm);
    MATRIX* spine = &bones[bone];
    const _VECTOR* root = &row3(bones[anim->skin->parents[bone] & 0x7f]);
    alignas(16) const _VECTOR dir{-gunm.m[0][0], -gunm.m[0][1], -gunm.m[0][2], 0.0f};
    alignas(16) _VECTOR up, side;
    Vec_UnitVector(&up, root, &row3(*spine));
    Vec_CrossNormalise(&up, &dir, &side);
    const _VECTOR* target = nullptr;
    alignas(16) _VECTOR ahead;
    if ((anim->anim_flags & 0x200u) == 0) {
        target = static_cast<const _VECTOR*>(anim->aim_target);
    } else {
        auto* t = static_cast<obj_tag*>(const_cast<void*>(nf::raw(anim->aim_target)));
        if (t != nullptr && t->sphere_src != nullptr) {
            target = &row3(anim_of(t)->bones[anim->aim_bone]);
        } else {
            ahead = {root->x + dir.x, root->y + dir.y, root->z + dir.z, root->w + dir.w};
            target = &ahead;
        }
    }
    float pitch = 0.0f, yaw = 0.0f;
    uint32_t which = 0;
    if (mode == 0) {
        anim->aim_yaw = 0.0f;
        anim->aim_pitch = 0.0f;
    }
    const bool player = obj->type == 3;
    if (mode != 2 && target == nullptr) ++g_anim_aim_guards;
    if (mode != 2 && target != nullptr && Vec_SqDist3D(root, target) <= anim->aim_dist_sq) {
        anim->anim_flags |= 0x1000u;
        alignas(16) const _VECTOR far_pt{gun.x + dir.x * 100.0f, gun.y + dir.y * 100.0f, gun.z + dir.z * 100.0f,
                                         gun.w + dir.w};
        alignas(16) const _VECTOR front{root->x + dir.x, root->y + dir.y, root->z + dir.z, root->w + dir.w};
        // O ângulo que leva o cano ao alvo no plano pela bacia com o cano e `axis` (o produto misto com `sign` dá o
        // sinal); `bit` vai para "which" com o alvo projetado a 0,5 m ou mais. Devolve 0 quando a arma está mais longe
        // da bacia que o alvo (nada a medir), 1 sem corte da esfera e 2 com ele (o ângulo fica 0 com o alvo atrás).
        const auto angle_to = [&](const _VECTOR& axis, const _VECTOR& sign, uint32_t bit, float* out) {
            alignas(16) const _VECTOR other{root->x + axis.x, root->y + axis.y, root->z + axis.z, root->w + axis.w};
            alignas(16) plane_equ_tag plane;
            Plane_PlaneEq(&plane, root, &other, &front);
            alignas(16) _VECTOR a, b, t, hit, u1, u2;
            Plane_PutPntOnPlane(&a, &gun, &plane);
            Plane_PutPntOnPlane(&b, &far_pt, &plane);
            Plane_PutPntOnPlane(&t, target, &plane);
            if (0.25f <= Vec_SqDist3D(root, &t)) which |= bit;
            const float da = Vec_Dist3D(root, &a);
            const float dt = Vec_Dist3D(root, &t);
            if (!(da <= dt)) return 0;
            if (!Vec_LineSphereIntersection(&a, &b, root, dt, &hit)) return 1;
            Vec_UnitVector(&u1, root, &t);
            Vec_UnitVector(&u2, root, &hit);
            float dot = (u2.x * u1.x + u2.y * u1.y) + u2.z * u1.z;
            if (player || 0.0f <= dot) {
                if (dot < -1.0f) {
                    dot = -1.0f;
                } else if (1.0f < dot) {
                    dot = 1.0f;
                }
                float ang = nf::fm::acos(dot);
                if (0.0f <= Vec_ScalarTripleProduct(&u1, &u2, &sign)) ang = -ang;
                *out = ang;
            }
            return 2;
        };
        if (angle_to(up, side, 2, &pitch) != 0) {
            float lo, hi;
            if (player) {
                lo = -1.5707964f;
                const uint32_t hb = 0x3fa2a974u;  // 1,2708
                std::memcpy(&hi, &hb, 4);
            } else {
                lo = -0.9f;
                hi = 0.9f;
            }
            if (pitch < lo) {
                pitch = lo;
                anim->anim_flags |= 0x4000u;
            } else if (hi < pitch) {
                pitch = hi;
                anim->anim_flags |= 0x4000u;
            }
        }
        if (!player && angle_to(side, up, 1, &yaw) == 2) {
            if (yaw < -0.9f) {
                yaw = -0.9f;
                anim->anim_flags |= 0x2000u;
            } else if (0.9f < pitch) {  // a inclinação: a peculiaridade do original
                yaw = 0.9f;
                anim->anim_flags |= 0x2000u;
            }
        }
    }
    if (which == 1) {
        pitch = 0.0f;
    } else if (which == 2) {
        yaw = 0.0f;
    }
    const float pi = 3.1415927f;
    const float new_yaw = Ang_RotateToBearing(anim->aim_yaw, yaw, anim->aim_speed, pi);
    const float new_pitch = Ang_RotateToBearing(anim->aim_pitch, pitch, anim->aim_speed, pi);
    anim->aim_yaw = new_yaw;
    anim->aim_pitch = new_pitch;
    alignas(16) quaternion_tag qy{up.x, up.y, up.z, new_yaw};
    Quat_AxisAngleToQuat(&qy, &qy);
    alignas(16) quaternion_tag qp{side.x, side.y, side.z, new_pitch};
    Quat_AxisAngleToQuat(&qp, &qp);
    const quaternion_tag turn = quat_mul(qy, qp);
    alignas(16) const quaternion_tag nq = quat_mul(turn, world[bone]);
    Quat_Copy(&world[bone], &nq);
    Quat_QuatToMat(&nq, spine);
    if (mode == 2 && std::fabs(new_yaw) < 0.01f && std::fabs(new_pitch) < 0.01f) anim->anim_flags &= ~0x180u;
}

// [decomp] 0x00111498 psiBuildMatrixPalette
// As matrizes dos ossos no mundo (uma vez por atualização, flag 4): a raiz com a matriz do objeto,
// os filhos encadeando quatérnios (pai ⊗ local) e a translação local girada pelo pai. Com
// "palette" e modelos, a paleta do skinning (flag 8): a rotação do osso com a translação da pose
// inversa levada pelo osso. Ossos que a pele não usa (pai sem 0x80) ficam como estão.
void psiBuildMatrixPalette(obj_tag* obj, void* state, int8_t palette) {
    ANIMOBJ_tag* anim = anim_from_state(state);
    ANIMSKIN_tag* skin = anim->skin;
    MATRIX* bones = anim->bones;
    const uint32_t count = pSkeletons[skin->skeleton]->bone_count;
    if ((anim->anim_flags & 4) == 0) {
        const uint32_t flags = anim->anim_flags;
        anim->anim_flags = flags | 4;
        const bool aim = (flags & 0x180) != 0;
        const bool head = (flags & 0x30) != 0;
        const MATRIX objm = obj->matrix;
        alignas(16) static quaternion_tag world[128];
        const _VECTOR* trans = reinterpret_cast<const _VECTOR*>(nf::raw(anim->frame));
        const quaternion_tag* local = reinterpret_cast<const quaternion_tag*>(anim->frame + count * 0x10);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t p = skin->parents[i];
            if ((p & 0x80) == 0) continue;
            const uint32_t parent = p & 0x7f;
            if (parent == 0x7f) {
                Quat_QuatTransToMat(&local[i], &trans[i], &bones[i]);
                RotTransMat(&objm, &bones[i]);
                Quat_MatToQuat(&world[i], &bones[i]);
                continue;
            }
            world[i] = quat_mul(world[parent], local[i]);
            Quat_QuatToMat(&world[i], &bones[i]);
            const MATRIX& pm = bones[parent];
            const _VECTOR& t = trans[i];
            row3(bones[i]) = {pm.m[0][0] * t.x + pm.m[1][0] * t.y + pm.m[2][0] * t.z + pm.m[3][0],
                              pm.m[0][1] * t.x + pm.m[1][1] * t.y + pm.m[2][1] * t.z + pm.m[3][1],
                              pm.m[0][2] * t.x + pm.m[1][2] * t.y + pm.m[2][2] * t.z + pm.m[3][2], t.w};
            if (head && i == 5) AnimObjectHeadTrack(obj, state, world, bones, 5);
            if (aim && i == 2) AnimObjectAimAt(obj, state, world, bones, 2);
        }
    }
    if (palette != 0 && skin->n_meshes != 0 && (anim->anim_flags & 8) == 0) {
        anim->anim_flags |= 8;
        MATRIX* out = anim->palette;
        for (uint32_t i = 0; i < count; ++i) {
            if ((skin->parents[i] & 0x80) == 0) continue;
            Mat_CopyRot(&bones[i], &out[i]);
            const MATRIX& b = bones[i];
            const _VECTOR& t = row3(skin->inv_pose[i]);
            _VECTOR& o = row3(out[i]);
            o.x = b.m[0][0] * t.x + b.m[1][0] * t.y + b.m[2][0] * t.z + b.m[3][0];
            o.y = b.m[0][1] * t.x + b.m[1][1] * t.y + b.m[2][1] * t.z + b.m[3][1];
            o.z = b.m[0][2] * t.x + b.m[1][2] * t.y + b.m[2][2] * t.z + b.m[3][2];
            o.w = b.m[0][3] * t.x + b.m[1][3] * t.y + b.m[2][3] * t.z + b.m[3][3];
        }
    }
}

// [decomp] 0x001c3120 AnimGetBoneWorldTrans
// Posição (e matriz) no mundo de um osso (bone com o bit 31: índice no esqueleto) ou de um anexo
// da pele (o anexo com o id bone + flag: a matriz dele presa ao osso). Sem pose: zero e identidade (Mat_Identity só
// escreve o 3x3: a translação e a coluna w de m ficam o que o chamador tinha; quem usa a linha 3 de m deve iniciá-la).
void AnimGetBoneWorldTrans(obj_tag* obj, uint32_t bone, int32_t flag, _VECTOR* out, MATRIX* m) {
    if (obj != nullptr) {
        ANIMOBJ_tag* anim = anim_of(obj);
        if (anim != nullptr && (anim->anim_flags & 1) != 0) {
            psiBuildMatrixPalette(obj, anim_state(anim), 0);
            ANIMSKIN_tag* skin = anim->skin;
            const uint32_t idx = bone & 0x7fffffffu;
            if (int32_t(bone) < 0) {
                *out = row3(anim->bones[idx]);
                if (m != nullptr) Mat_Copy(&anim->bones[idx], m);
                return;
            }
            const uint32_t id = idx + uint32_t(flag);
            for (uint32_t i = 0; i < skin->n_extra; ++i) {
                const uint8_t* e = skin->extras + i * 0x24;
                if (rd<uint32_t>(e) != id) continue;
                const uint32_t b = rd<uint32_t>(e + 4);
                alignas(16) quaternion_tag q;
                alignas(16) _VECTOR t;
                std::memcpy(&q, e + 0x14, 16);
                std::memcpy(&t, e + 8, 12);
                t.w = 0.0f;
                MATRIX local;
                Quat_QuatTransToMat(&q, &t, &local);
                Concat(&anim->bones[b], &local);
                *out = row3(local);
                if (m != nullptr) *m = local;
                return;
            }
        }
    }
    Vec_Zero(out);
    if (m != nullptr) Mat_Identity(m);
}

// [decomp] 0x001c32e0 AnimGetBoneWorldTrans2
// Como AnimGetBoneWorldTrans, mas da pose da animação (o quadro resolvido, anim->frame), sem as matrizes já montadas:
// sobe a cadeia do osso até a raiz e desce encadeando as matrizes (a raiz com a matriz do objeto). AnimObjectAimAt a
// chama no meio de psiBuildMatrixPalette, quando os ossos depois da coluna ainda não existem nesta atualização, para
// saber onde está a arma sem a mira. Com o bit 31, "bone" é o índice no esqueleto; sem ele, o anexo da pele com o id
// bone + flag (a matriz do anexo presa ao osso dele). Sem pose ou sem o anexo: zero e identidade.
// Divergência: o original monta as matrizes na pilha (96 delas, indexadas pelo número do osso); aqui num vetor do
// tamanho do maior esqueleto (128), com a coluna w de identidade (o original deixa o que havia na pilha; nada a lê).
void AnimGetBoneWorldTrans2(obj_tag* obj, uint32_t bone, int32_t flag, _VECTOR* out, MATRIX* m) {
    if (obj != nullptr) {
        ANIMOBJ_tag* anim = anim_of(obj);
        if (anim != nullptr && (anim->anim_flags & 1) != 0) {
            ANIMSKIN_tag* skin = anim->skin;
            const _VECTOR* trans = reinterpret_cast<const _VECTOR*>(nf::raw(anim->frame));
            const quaternion_tag* quats =
                reinterpret_cast<const quaternion_tag*>(anim->frame + skin_bone_count(skin) * 0x10);
            const bool by_index = int32_t(bone) < 0;
            uint32_t start = bone & 0x7fffffffu;
            int32_t extra = -1;
            if (!by_index) {
                const uint32_t id = start + uint32_t(flag);
                for (int32_t i = 0; i < int32_t(skin->n_extra); ++i) {
                    if (rd<uint32_t>(skin->extras + i * 0x24) == id) {
                        extra = i;
                        break;
                    }
                }
                if (extra >= 0) start = rd<uint32_t>(skin->extras + extra * 0x24 + 4);
            }
            if (by_index || extra >= 0) {
                uint8_t chain[128];
                int32_t n = 0;
                chain[n++] = uint8_t(start);
                for (uint32_t p = skin->parents[start] & 0x7fu; p != 0x7f && n < 128; p = skin->parents[p] & 0x7fu) {
                    chain[n++] = uint8_t(p);
                }
                static MATRIX mats[128];
                const MATRIX objm = obj->matrix;
                for (int32_t k = n - 1; k >= 0; --k) {
                    const uint32_t b = chain[k];
                    Mat_Identity(&mats[b]);
                    Quat_QuatTransToMat(&quats[b], &trans[b], &mats[b]);
                    if (k == n - 1) {
                        RotTransMat(&objm, &mats[b]);
                    } else {
                        Concat(&mats[chain[k + 1]], &mats[b]);
                    }
                }
                if (by_index) {
                    *out = row3(mats[start]);
                    if (m != nullptr) *m = mats[start];
                    return;
                }
                const uint8_t* e = skin->extras + extra * 0x24;
                alignas(16) quaternion_tag q;
                alignas(16) _VECTOR t;
                std::memcpy(&q, e + 0x14, 16);
                std::memcpy(&t, e + 8, 12);
                t.w = 0.0f;
                MATRIX local;
                Mat_Identity(&local);
                Quat_QuatTransToMat(&q, &t, &local);
                Concat(&mats[start], &local);
                *out = row3(local);
                if (m != nullptr) *m = local;
                return;
            }
        }
    }
    Vec_Zero(out);
    if (m != nullptr) Mat_Identity(m);
}

// [decomp] 0x001c4490 AnimObjectDraw
// Desenha o objeto animado (atualizado no último tick): os modelos da pele com a paleta
// (psiDrawSkinObjectMatrix; o primeiro pode ser trocado por mesh_override), os pesos de morph mais
// fortes (até 8, só de perto), o clarão do tiro, a mira laser (arma 0x18) e os anexos ativos
// (presos aos ossos, com a cor de "tweak" do objeto).
void AnimObjectDraw(obj_tag* obj, viewer_tag* viewer, uint8_t, uint8_t, uint8_t, int8_t) {
    if (obj == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (anim == nullptr || (anim->anim_flags & 1) == 0 || 2 <= int32_t(GameState.ticks - anim->tick)) return;
    anim->tick = GameState.ticks;
    psiBuildMatrixPalette(obj, anim_state(anim), 1);
    ANIMSKIN_tag* skin = anim->skin;
    if (skin->n_meshes == 0 || skin->n_discrete == 0) View_SetupRenderModes(viewer, obj);
    else View_SetupRenderModes2(viewer, obj);
    Morph_Count = 0;
    if (ScriptCam != 0 || AnimObjectIsClose(obj, 0xc)) {
        const uint32_t n = skin->n_morphs;
        if (n != 0) {
            for (uint32_t i = 0; i < n; ++i) {
                Morph_Index[i] = uint8_t(i);
                Morph_Weight[i] = anim->morph_weights[i];
            }
            // Os 8 maiores pesos primeiro (seleção), sem os quase nulos do fim; depois por índice.
            const uint32_t top = n < 9 ? n : 8;
            uint32_t k = 0;
            for (uint32_t i = 0; i < top; ++i) {
                k = i + 1;
                for (uint32_t j = k; j < n; ++j) {
                    if (Morph_Weight[i] < Morph_Weight[j]) {
                        std::swap(Morph_Weight[i], Morph_Weight[j]);
                        std::swap(Morph_Index[i], Morph_Index[j]);
                    }
                }
            }
            uint32_t count = k;
            while (0 < count && std::fabs(Morph_Weight[count - 1]) < 1e-06f) --count;
            Morph_Count = uint8_t(count);
            for (uint32_t i = 0; i + 1 < count; ++i) {
                for (uint32_t j = i + 1; j < count; ++j) {
                    if (Morph_Index[j] < Morph_Index[i]) {
                        std::swap(Morph_Weight[i], Morph_Weight[j]);
                        std::swap(Morph_Index[i], Morph_Index[j]);
                    }
                }
            }
        }
    }
    MATRIX unused;
    for (uint32_t i = 0; i < skin->n_meshes; ++i) {
        celglist_tag* g = skin->meshes[i];
        if (g == nullptr) continue;
        celglist_tag* draw = (i == 0 && anim->mesh_override != nullptr) ? nf::raw(anim->mesh_override) : g;
        psiDrawSkinObjectMatrix(draw, &unused, obj, anim->palette);
    }
    Morph_Count = 0;
    if ((anim->anim_flags & 2) != 0 && skin->n_extra != 0) {
        uint32_t flash = 0;
        const int32_t bone = AnimDatumGetWeaponInfo(obj, &flash, nullptr, nullptr);
        if (flash != 0) AnimDatumSetEntity(obj, bone < 0 ? 0 : bone, flash, 1);
    }
    if (anim->weapon == 0x18) {
        // Mira laser: um feixe do anexo 8 até onde um raio de 100 m (no x dele) bate.
        P32<HITDATA_tag> hits = nullptr;
        _VECTOR from;
        MATRIX m;
        AnimGetBoneWorldTrans(obj, 8, 3, &from, &m);
        _VECTOR to{m.m[0][0] * -100.0f + m.m[3][0], m.m[0][1] * -100.0f + m.m[3][1], m.m[0][2] * -100.0f + m.m[3][2],
                   0.0f};
        if (Collide_RayIntersect(&from, &to, obj->cel, obj, nullptr, &hits, 0, 8, 0) && hits != nullptr) to = hits->pos;
        Collide_FreeHitList(&hits);
        celglist_tag* beam = hashtable_hashcode_to_celglist(viewer->mode == 1 ? 0x2000131 : 0x2000133);
        if (beam != nullptr) {
            const _VECTOR d{from.x - to.x, from.y - to.y, from.z - to.z, 0.0f};
            _VECTOR rot;
            Vec_Cartesian_2_Spherical(&rot, d.x, d.y, d.z);
            rot.x = -rot.x;
            rot.z = 0.0f;
            _VECTOR scale{1.0f, 1.0f, Vec_Magnitude(&d), 0.0f};
            View_DrawGlist(beam, &from, &rot, &scale, obj->created);
        }
    }
    for (uint32_t i = 0; i < skin->n_extra; ++i) {
        int8_t& timer = anim->extra_timers[i];
        if (timer == 0) continue;
        if (timer != -1) --timer;
        celglist_tag* g = anim->extra_glists[i];
        if (g == nullptr) continue;
        const uint8_t* e = skin->extras + i * 0x24;
        alignas(16) quaternion_tag q;
        alignas(16) _VECTOR t;
        std::memcpy(&q, e + 0x14, 16);
        std::memcpy(&t, e + 8, 12);
        t.w = 0.0f;
        MATRIX m;
        Quat_QuatTransToMat(&q, &t, &m);
        const int32_t b = rd<int32_t>(e + 4);
        if (-1 < b) Concat(&anim->bones[b], &m);
        if ((g->flags & 0x800) == 0) {
            psiSetTweakARGB(obj->unknown_103[3], obj->unknown_103[0], obj->unknown_103[1], obj->unknown_103[2]);
        }
        psiDrawObjectMatrix(g, &m, obj->created);
    }
    bCharacterLight = 0;
    bShadowCharacter = 0;
}

// Só para testes.
void anim_debug_power_on() {
    for (ANIMSKEL_tag*& s : pSkeletons) s = nullptr;
    GlbAnimList = 0;
    Glb_bScriptDeleted = 0;
    Glb_pCurrentScript = nullptr;
    ReverseFlag = 0;
    AnimTagTimeStamp = 0;
    AnimScriptTimeStamp = 0;
    Glb_AnimPostLoadDone = 0;
    nDistanceTables = 0;
    for (float* s : {Glb_SkinScale, Glb_InvSkinScale}) s[0] = s[1] = s[2] = 1.0f;
    ScriptFootstepFoot = 0;
    ScriptFootstepType = 0;
    g_anim_face_guards = 0;
    Morph_Count = 0;
}

// ------------------------------------------------------------------ conjuntos (parado e andando)

namespace {
// Tabelas de scripts dos conjuntos do jogador (AnimSet_*): parado, variações, 0; andar, 0.
const uint32_t AnimSet_Rifle_SP[] = {0x6000a00, 0x6000a00, 0x6000a00, 0, 0x6000a00, 0x6000a00, 0x6000a00, 0x6000a00, 0};
const uint32_t AnimSet_RifleCrouch_SP[] = {0x60009ff, 0x60009ff, 0x60009ff, 0, 0x60009ff,
                                           0x60009ff, 0x60009ff, 0x60009ff, 0};
const uint32_t AnimSet_Rifle[] = {0x60001d4, 0x60001db, 0x60001d9, 0, 0x600097c, 0x600097c, 0x600097d, 0x600097d, 0};
const uint32_t AnimSet_RifleCrouch[] = {0x60001d7, 0x60008f9, 0x60008f9, 0, 0x6000914, 0x6000914, 0x6000915, 0x6000915, 0};
const uint32_t AnimSet_Unarmed[] = {0x60001f1, 0x60001f2, 0x60001f0, 0, 0x60009e4, 0x60009e4, 0x60009e5, 0x60009e5, 0};
const uint32_t AnimSet_UnarmedCrouch[] = {0x60001ee, 0x6000931, 0x6000931, 0, 0x6000930,
                                          0x6000930, 0x600092f, 0x600092f, 0};
const uint32_t AnimSet_Handgun[] = {0x60009dc, 0x60009de, 0x60009db, 0, 0x60009d7, 0x60009d7, 0x60009d8, 0x60009d8, 0};
const uint32_t AnimSet_HandgunCrouch[] = {0x60009d9, 0x60009f5, 0x60009f5, 0, 0x60009f3,
                                          0x60009f3, 0x60009f4, 0x60009f4, 0};
const uint32_t AnimSet_Launcher[] = {0x60001e6, 0x60001e7, 0x60001e5, 0, 0x60009df, 0x60009df, 0x60009e0, 0x60009e0, 0};
const uint32_t AnimSet_LauncherCrouch[] = {0x60001e3, 0x600091f, 0x600091f, 0, 0x600091e,
                                           0x600091e, 0x600091d, 0x600091d, 0};
const uint32_t AnimSet_Handgun2H[] = {0x60001ce, 0x60001cf, 0x60001cd, 0, 0x60009fc, 0x60009fc, 0x60009fd, 0x60009fd, 0};
const uint32_t AnimSet_Handgun2HCrouch[] = {0x60001cb, 0x60009d2, 0x60009d2, 0, 0x60009d1,
                                            0x60009d1, 0x60009d0, 0x60009d0, 0};
const uint32_t AnimSet_TwinHandguns[] = {0x60009fe, 0x60009b5, 0x60009b7, 0, 0x60009ba,
                                         0x60009ba, 0x60009bb, 0x60009bb, 0};
const uint32_t AnimSet_TwinHandgunsCrouch[] = {0x60009bc, 0x60009d3, 0x60009d3, 0, 0x60009d4,
                                               0x60009d4, 0x60009d5, 0x60009d5, 0};
const uint32_t AnimSet_SMG[] = {0x60009b0, 0x60009b2, 0x60009af, 0, 0x60009b3, 0x60009b3, 0x60009b4, 0x60009b4, 0};
const uint32_t AnimSet_SMGCrouch[] = {0x60009ad, 0x60009aa, 0x60009aa, 0, 0x60009ab, 0x60009ab, 0x60009ac, 0x60009ac, 0};

// Tipo de arma para os conjuntos: no modo história sempre 999 (rifle do Bond); no multijogador o
// byte 0x84 da arma.
uint32_t player_set_kind(obj_tag* obj) {
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp == 0) return 999;
    return weapon_data[int32_t(anim_of(obj)->weapon) * 0x10c + 0x84];
}
}  // namespace

// [decomp] 0x001c6a48 AnimSetInit
// Conta os scripts das duas listas da tabela (por velocidade: parado e andando cada vez mais
// rápido; de lado), zera o estado e apaga os scripts do objeto.
void AnimSetInit(obj_tag* obj, ANIMSET_tag* set, const uint32_t* table, float min_speed, float stride) {
    if (obj == nullptr || anim_of(obj) == nullptr || set == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    set->table = table;
    set->speed_count = 0;
    if (table[0] != 0) {
        while (table[++set->speed_count] != 0) {
        }
    }
    set->side_count = 0;
    set->side = set->table + set->speed_count + 1;
    if (set->side[0] != 0) {
        while (set->side[++set->side_count] != 0) {
        }
    }
    set->transition = 0;
    set->blend = nullptr;
    set->hold = 0;
    set->stamp_a = 0;
    set->stamp_b = 0;
    set->walk_script = nullptr;
    set->current = -1;
    set->strafe = -1;
    const uint32_t r = Rand_Random();
    set->min_speed = min_speed;
    set->idle_time = uint16_t((r & 0x3ff) + 0xff);
    anim->walk_pos = min_speed;
    set->stride = stride;
    AnimListBuild(obj, 1, 0, nullptr, 1);
    AnimListDelete(obj);
}

// [decomp] 0x001c07b8 AnimSetAppend
// Um conjunto novo no fim da lista do objeto (até 4).
ANIMSET_tag* AnimSetAppend(obj_tag* obj, const uint32_t* table, float min_speed, float stride) {
    if (obj == nullptr || anim_of(obj) == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    ANIMSET_tag* last = anim->sets;
    ANIMSET_tag* set;
    if (last == nullptr) {
        set = static_cast<ANIMSET_tag*>(Mem_Malloc(sizeof(ANIMSET_tag), 0x3c04, 0));
        if (set != nullptr) {
            std::memset(set, 0, sizeof(*set));
            set->prev = nullptr;
            set->next = nullptr;
            set->tag = uint32_t(AnimTagGetUnique());
        }
        anim->sets = set;
    } else {
        int32_t n = 1;
        while (last->next != nullptr) {
            last = last->next;
            ++n;
        }
        if (3 < n) return nullptr;
        set = static_cast<ANIMSET_tag*>(Mem_Malloc(sizeof(ANIMSET_tag), 0x3c04, 0));
        if (set != nullptr) {
            std::memset(set, 0, sizeof(*set));
            ANIMSET_tag* next = last->next;
            set->prev = last;
            set->next = next;
            last->next = set;
            if (next != nullptr) next->prev = set;
            set->tag = uint32_t(AnimTagGetUnique());
        }
    }
    AnimSetInit(obj, set, table, min_speed, stride);
    return set;
}

// [decomp] 0x001c6928 AnimSetDeleteAll
void AnimSetDeleteAll(obj_tag* obj) {
    if (obj == nullptr || anim_of(obj) == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    for (ANIMSET_tag* set = anim->sets; set != nullptr;) {
        ANIMSET_tag* next = set->next;
        if (set->blend != nullptr) AnimScriptDelete(obj, &set->blend);
        AnimListBuild(obj, 0x200, set->tag, nullptr, 1);
        AnimListDelete(obj);
        ANIMSET_tag* prev = set->prev;
        if (prev == nullptr) anim->sets = next;
        else prev->next = next;
        if (next != nullptr) next->prev = prev;
        Mem_Free(&set);
        set = next;
    }
}

// [decomp] 0x001c6a08 AnimSet_WalkUpdater
// Decodificador das sequências de andar: interpola entre quadros pela fração do quadro.
void AnimSet_WalkUpdater(obj_tag* obj, ANIMSKIN_tag* skin, ANIMSEQ_tag* seq, void*) {
    AnimFrameSet(obj, seq, skin, seq->frame_pos - float(int32_t(seq->frame_pos)));
}

// [decomp] 0x001c7000 AnimScriptAppend
// Mais um script no fim da lista (até 32).
ANIMSCRIPT_tag* AnimScriptAppend(obj_tag* obj, uint32_t script) {
    if (obj == nullptr || anim_of(obj) == nullptr) return nullptr;
    ANIMOBJ_tag* anim = anim_of(obj);
    ANIMSCRIPT_tag* s;
    if (anim->scripts == nullptr) {
        s = AnimScriptNew(obj, nullptr);
        anim->scripts = s;
    } else {
        ANIMSCRIPT_tag* last = anim->scripts;
        int32_t n = 1;
        while (last->next != nullptr) {
            last = last->next;
            ++n;
        }
        if (0x1f < n) return nullptr;
        s = AnimScriptNew(obj, last);
    }
    AnimScriptInit(obj, s, script);
    return s;
}

// [decomp] 0x001c17c0 AnimScriptAppendBlend
// Como AnimScriptAppend, mas antes faz os scripts principais saírem aos poucos (8 ticks).
ANIMSCRIPT_tag* AnimScriptAppendBlend(obj_tag* obj, uint32_t script) {
    AnimListBuild(obj, 1, 0, nullptr, 1);
    AnimListBuild(obj, 0x800, 0, nullptr, 2);
    for (int32_t i = 0; i < GlbAnimList; ++i) {
        ANIMSCRIPT_tag* s = GlbAnimListItems[i];
        if ((s->flags & 0x400000u) == 0) {
            s->fade = -1;
            s->flags |= 0x500000u;
            s->weight_max = 8.0f;
            s->weight = 8.0f;
        }
    }
    if (obj == nullptr) return nullptr;
    return AnimScriptAppend(obj, script);
}

// [decomp] 0x001c0938 AnimSetUpdate
// Escolhe e mistura os scripts do conjunto pela velocidade: parado (o primeiro da lista, trocado a
// cada idle_time) ou andar (dois scripts de andar vizinhos misturados pela fração da velocidade,
// sincronizados pela distância andada). Uma transição em curso (transition 1) espera o script
// terminar. No multijogador, andar de lado mistura um script de passo lateral.
void AnimSetUpdate(obj_tag* obj, ANIMSET_tag* set, float speed, float forward, float max_speed, float side,
                   float side_max) {
    if (obj == nullptr || anim_of(obj) == nullptr || set == nullptr) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    float blend_t = 0.0f;
    if (speed <= 0.0002f && -0.0002f <= speed) forward = 0.0f;
    anim->walk_dir = forward * set->stride;
    bool skip_walk = false;
    while (set->transition != 0) {
        if (set->transition != 1) {
            skip_walk = true;
            break;
        }
        ANIMSCRIPT_tag* s = AnimListBuild(obj, 0x200, set->tag, nullptr, 1);
        if ((s->flags & 0x10000000u) == 0 && speed == 0.0f) {
            skip_walk = true;
            break;
        }
        AnimScriptDelete(obj, &s);
        set->transition = 0;
    }
    if (!skip_walk) {
        float f = std::fabs(forward);
        if (f < 0.0f) f = 0.0f;
        else if (max_speed < f) f = max_speed;
        f = f / max_speed;
        int32_t pick = 0;
        if (0.0002f < f || f < -0.0002f) {
            f = f * float(int32_t(set->speed_count) - 2);
            int32_t i = int32_t(f);
            if (i == int32_t(set->speed_count) - 2) --i;
            pick = i + 1;
            blend_t = f - float(i);
        }
        if (set->hold != 0) --set->hold;
        int32_t current = set->current;
        if (pick != current && set->hold == 0) {
            set->hold = 4;
            set->current = int8_t(pick);
            if (current == 0 || pick == 0) {
                AnimListBuild(obj, 0x200, set->tag, nullptr, 1);
                for (int32_t i = 0; i < GlbAnimList; ++i) {
                    ANIMSCRIPT_tag* s = GlbAnimListItems[i];
                    if ((s->flags & 0x400000u) == 0) {
                        s->fade = -1;
                        s->flags |= 0x500000u;
                        s->weight_max = 8.0f;
                        s->weight = 8.0f;
                    }
                }
            }
            if (pick == 0) {
                ANIMSCRIPT_tag* s = AnimScriptAppend(obj, set->table[0]);
                s->tag = set->tag;
            } else {
                if (current != 0 && pick != 0) {
                    AnimListBuild(obj, 0x200, set->tag, nullptr, 1);
                    for (int32_t i = 0; i < GlbAnimList; ++i) {
                        ANIMSCRIPT_tag* s = GlbAnimListItems[i];
                        if (set->stamp_a == s->stamp || set->stamp_b == s->stamp) AnimScriptDelete(obj, &s);
                    }
                }
                ANIMSCRIPT_tag* a = AnimScriptAppend(obj, set->table[pick]);
                a->sync_mode = 1;
                a->tag = set->tag;
                a->flags |= 0x8d000000u;
                a->seq[0]->callback = AnimSet_WalkUpdater;
                set->stamp_a = a->stamp;
                ANIMSCRIPT_tag* b = AnimScriptAppend(obj, set->table[pick + 1]);
                b->sync_mode = 2;
                b->tag = set->tag;
                b->flags |= 0x8d000000u;
                b->sync = a;
                set->stamp_b = b->stamp;
                set->walk_script = b;
                b->owner = &set->walk_script;
            }
            AnimObjectUpdate(obj);
            current = pick;
        }
        if (current != 0 && set->walk_script != nullptr) set->walk_script->sync_weight = blend_t;
        AnimListBuild(obj, 0x200, set->tag, nullptr, 1);
        if (GlbAnimList == 1) {
            anim->walk_pos = set->min_speed;
        } else {
            set->idle_time = uint16_t((Rand_Random() & 0x3ff) + 0xff);
        }
    }
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp == 0) return;
    const float ratio = side / side_max;
    const float angle = nf::fm::atan2(ratio, std::fabs(forward / max_speed));
    uint8_t strafe = 0;
    if ((0.0002f < side || side < -0.0002f) && 0.5f <= std::fabs(angle * 0.63661975f)) strafe = 0.0f < side ? 1 : 2;
    if (strafe != uint8_t(set->strafe)) {
        set->strafe = int8_t(strafe);
        set->current = -1;
        if (set->blend != nullptr) AnimScriptDelete(obj, &set->blend);
        if (strafe != 0) {
            const uint32_t script = set->table[set->speed_count + (strafe == 1 ? 1 : 3)];
            set->blend = AnimScriptAppendBlend(obj, script);
            set->blend->owner = &set->blend;
            set->blend->flags |= 0x8d004000u;
        }
    }
    if (strafe != 0 && set->blend != nullptr) {
        set->blend->speed = std::fabs(ratio);
        set->blend->weight = std::fabs(angle * 0.63661975f);
    }
}

// [decomp] 0x0018fcd8 PlayerAnimSetInitNormal
void PlayerAnimSetInitNormal(obj_tag* obj) {
    const uint32_t* table;
    switch (player_set_kind(obj)) {
        case 1: table = AnimSet_Handgun; break;
        case 2: table = AnimSet_SMG; break;
        case 3: table = AnimSet_Rifle; break;
        case 4: table = AnimSet_Handgun2H; break;
        case 5: table = AnimSet_Launcher; break;
        case 6: table = AnimSet_TwinHandguns; break;
        case 999: table = AnimSet_Rifle_SP; break;
        default: table = AnimSet_Unarmed; break;
    }
    bloke_of(obj)->anim_set = AnimSetAppend(obj, table, 0.37f, 0.5f);
}

// [decomp] 0x0018fea0 PlayerAnimSetInitCrouch
void PlayerAnimSetInitCrouch(obj_tag* obj) {
    const uint32_t* table;
    switch (player_set_kind(obj)) {
        case 1: table = AnimSet_HandgunCrouch; break;
        case 2: table = AnimSet_SMGCrouch; break;
        case 3: table = AnimSet_RifleCrouch; break;
        case 4: table = AnimSet_Handgun2HCrouch; break;
        case 5: table = AnimSet_LauncherCrouch; break;
        case 6: table = AnimSet_TwinHandgunsCrouch; break;
        case 999: table = AnimSet_RifleCrouch_SP; break;
        default: table = AnimSet_UnarmedCrouch; break;
    }
    bloke_of(obj)->anim_set = AnimSetAppend(obj, table, 0.37f, 0.5f);
}

// [decomp] 0x00195b90 PlayerAnimSetInit
void PlayerAnimSetInit(obj_tag* obj) {
    bloke_of(obj)->anim_set = nullptr;
    AnimSetDeleteAll(obj);
    if (obj->unknown_f6 == 4) PlayerAnimSetInitCrouch(obj);
    else PlayerAnimSetInitNormal(obj);
}

namespace {
// As transições de agachar: o script da arma (3 no modo história) a 4 quadros por tick.
void player_transition(obj_tag* obj, const uint32_t scripts[7]) {
    uint32_t kind = 3;
    uint32_t mp;
    std::memcpy(&mp, MPSettings + 0x180, 4);
    if (mp != 0) kind = weapon_data[int32_t(anim_of(obj)->weapon) * 0x10c + 0x84];
    const uint32_t script = (1 <= kind && kind <= 6) ? scripts[kind] : scripts[0];
    ANIMSCRIPT_tag* s = AnimScriptAdd(obj, script);
    if (s == nullptr) return;
    s->speed = 0.0f <= s->speed ? 4.0f : -4.0f;
    s->flags |= 0xd000000u;
}
}  // namespace

// [decomp] 0x00190068 PlayerAnimCrouch2Stand
void PlayerAnimCrouch2Stand(obj_tag* obj) {
    static const uint32_t kScripts[7] = {0x60001f4, 0x60009da, 0x60009ae, 0x60001dd, 0x60001d1, 0x60001e9, 0x60009b6};
    player_transition(obj, kScripts);
}

// [decomp] 0x00190198 PlayerAnimStand2Crouch
void PlayerAnimStand2Crouch(obj_tag* obj) {
    static const uint32_t kScripts[7] = {0x60001f3, 0x60009dd, 0x60009b1, 0x60001dc, 0x60001d0, 0x60001e8, 0x60009b9};
    player_transition(obj, kScripts);
}
