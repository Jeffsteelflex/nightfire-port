// Testes de anim.cpp com esqueletos, sequências e scripts sintéticos.

#include <cmath>
#include <cstring>
#include <vector>

#include "engine/sound.h"
#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "test_framework.h"

namespace {

void fresh() {
    psiMemHeapSize = 1u << 20;
    PtrHeap = nullptr;
    Mem_Init();
    object_debug_power_on();
    collide_debug_power_on();
    Coll_ResetHitHeap();
    control_init_object_lists();
    gbl_room_cels = nullptr;
    glb_world = build_alloc_world();
    hashtable_debug_power_on();
    hashtable_initialise();
    anim_debug_power_on();
    pending_reset();
    g_sound_stats = SoundStats{};
    GS_SetRefreshRate(60, 60);
}

// Esqueleto de dois ossos (o 1 filho do 0), só o osso 0 com translação animada; o osso 1 tem a
// translação padrão (1, 2, 3).
struct Skel2 {
    alignas(16) uint8_t data[0x10 + 2 * 12] = {};
    Skel2() {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(data);
        k->id = 3;
        k->bone_count = 2;
        k->trans_mask[0] = 1;
        k->trans[1][0] = 1.0f;
        k->trans[1][1] = 2.0f;
        k->trans[1][2] = 3.0f;
        pSkeletons[3] = k;
    }
};

// Monta um arquivo de sequência: cabeçalho de 0x40 bytes e um registro por canal, cada um com um
// só trecho de "frames" quadros e os coeficientes dados (máscara pelos não nulos de c[0..5]).
std::vector<uint32_t> make_seq(int16_t frames, uint8_t skeleton, const std::vector<std::vector<float>>& channels) {
    std::vector<uint32_t> w(0x10, 0);
    for (const auto& c : channels) {
        uint32_t mask = 0;
        std::vector<uint32_t> coeffs;
        for (size_t k = 0; k < c.size(); ++k) {
            if (c[k] != 0.0f) {
                mask |= 1u << k;
                uint32_t bits;
                std::memcpy(&bits, &c[k], 4);
                coeffs.push_back(bits);
            }
        }
        const uint32_t desc = uint32_t(frames) << 6 | mask;
        const uint32_t size = 4 + uint32_t(coeffs.size()) * 4;
        w.push_back(size | desc << 16);
        w.insert(w.end(), coeffs.begin(), coeffs.end());
    }
    auto* hdr = reinterpret_cast<uint8_t*>(w.data());
    std::memcpy(hdr + 0x38, &frames, 2);
    const int16_t n = int16_t(channels.size());
    std::memcpy(hdr + 0x3a, &n, 2);
    hdr[0x3c] = skeleton;
    return w;
}

const _VECTOR& vec_at(const uint8_t* frame, uint32_t i) { return *reinterpret_cast<const _VECTOR*>(frame + i * 16); }

}  // namespace

TEST(anim_frame_copy_decodes_channels) {
    fresh();
    Skel2 skel;
    // Osso 0: tx = 1 + 0,5 f, ty = 2, tz = 0,25 f² (f = quadro - 1); quatérnio z = 0,6 (w = 0,8).
    // Osso 1: x = 0,6 e z = 4 (z real 0, w negativo).
    std::vector<uint32_t> file = make_seq(10, 3, {{1.0f, 0.5f}, {2.0f}, {0, 0, 0.25f}, {}, {}, {0.6f},
                                                  {0.6f}, {}, {4.0f}});
    hashtable_additem(0x4000001, file.data());
    AnimProcessSeqData(0x4000001);
    auto* raw = static_cast<ANIMRAWSEQ_tag*>(hashtable_getitem(0x4000001));
    CHECK(raw->channels == file.data() + 0x10 && raw->size == (file.size() - 0x10) * 4);
    ANIMSEQ_tag seq{};
    CHECK(AnimSeqGetInfo(0x4000001, &seq, nullptr) == raw && seq.frame_count == 10 && seq.skeleton == 3);
    seq.first_frame = 1;
    seq.last_frame = 10;
    alignas(16) uint8_t frame[64] = {};
    AnimFrameCopy(frame, &seq, nullptr, 5.0f);
    const _VECTOR& t0 = vec_at(frame, 0);
    CHECK_NEAR(t0.x, 3.0f, 1e-6f);
    CHECK_NEAR(t0.y, 2.0f, 1e-6f);
    CHECK_NEAR(t0.z, 4.0f, 1e-6f);
    const _VECTOR& t1 = vec_at(frame, 1);
    CHECK(t1.x == 1.0f && t1.y == 2.0f && t1.z == 3.0f);  // a translação padrão
    const _VECTOR& q0 = vec_at(frame, 2);
    CHECK(q0.x == 0.0f && q0.y == 0.0f && q0.z == 0.6f);
    CHECK_NEAR(q0.w, 0.8f, 1e-6f);
    const _VECTOR& q1 = vec_at(frame, 3);
    CHECK(q1.x == 0.6f && q1.z == 0.0f);
    CHECK_NEAR(q1.w, -0.8f, 1e-6f);
    // O quadro é limitado ao intervalo da sequência e o inteiro é usado (5,9 -> 5).
    AnimFrameCopy(frame, &seq, nullptr, 25.0f);
    CHECK_NEAR(vec_at(frame, 0).x, 1.0f + 0.5f * 9.0f, 1e-6f);
    AnimFrameCopy(frame, &seq, nullptr, 5.9f);
    CHECK_NEAR(vec_at(frame, 0).x, 3.0f, 1e-6f);
}

TEST(anim_frame_blend_lerps_and_slerps) {
    fresh();
    alignas(16) float a[16] = {}, b[16] = {}, out[16] = {};
    // Dois ossos: translações e quatérnios (identidade e 90 graus em y).
    a[0] = 0.0f;
    b[0] = 2.0f;
    a[4] = 1.0f;
    b[4] = 3.0f;
    a[11] = 1.0f;
    b[9] = std::sin(0.7853982f);
    b[11] = std::cos(0.7853982f);
    a[15] = 1.0f;
    b[15] = 1.0f;
    obj_tag obj{};
    alignas(16) ANIMOBJ_tag anim{};
    obj.sphere_src = reinterpret_cast<uint8_t*>(&anim);
    AnimFrameBlend(&obj, out, a, b, 2, 0.5f, nullptr);
    CHECK_NEAR(out[0], 1.0f, 1e-6f);
    CHECK_NEAR(out[4], 2.0f, 1e-6f);
    // Metade de 90 graus: 45 graus em y.
    CHECK_NEAR(out[9], std::sin(0.3926991f), 1e-4f);
    CHECK_NEAR(out[11], std::cos(0.3926991f), 1e-4f);
    AnimFrameBlend(&obj, out, a, b, 2, 0.0f, nullptr);
    CHECK(std::memcmp(out, a, sizeof(a)) == 0);
    AnimFrameBlend(&obj, out, a, b, 2, 1.0f, nullptr);
    CHECK(std::memcmp(out, b, sizeof(b)) == 0);
    // Com lista de ossos, o osso listado fica com b inteiro.
    const uint8_t list[] = {1, 0, 0xff};
    AnimFrameBlend(&obj, out, a, b, 2, 0.5f, const_cast<uint8_t*>(list));
    CHECK_NEAR(out[0], 1.0f, 1e-6f);
    CHECK(out[4] == 3.0f && out[15] == 1.0f);
}

TEST(anim_script_end_modes) {
    fresh();
    ANIMSCRIPT_tag s{};
    s.frame_count = 10;
    s.speed = 1.0f;
    uint8_t done = 0;
    s.done_flag = &done;
    CHECK(AnimScriptEnd(nullptr, nullptr, &s, 4.5f) == 4.5f && s.frame == 4.5f);
    // 0: para no fim, marca terminado e liga done_flag.
    CHECK(AnimScriptEnd(nullptr, nullptr, &s, 12.0f) == 10.0f);
    CHECK((s.flags & 0x10000000u) != 0 && done == 1 && s.done_flag == nullptr);
    // 1: repete.
    s.flags = 0;
    s.end_mode = 1;
    CHECK(AnimScriptEnd(nullptr, nullptr, &s, 12.0f) == 2.0f && (s.flags & 0x40000000u) != 0);
    // 2: vai e volta (a velocidade inverte).
    s.end_mode = 2;
    CHECK(AnimScriptEnd(nullptr, nullptr, &s, 11.0f) == 9.0f && s.speed == -1.0f);
    CHECK(AnimScriptEnd(nullptr, nullptr, &s, 0.0f) == 2.0f && s.speed == 1.0f);
}

namespace {
// Objeto animado com a pele de dois ossos (pais 0xff e 0x80 | 0), uma malha (para haver paleta) e a
// pose inversa de "bind" (o osso 1 na posição (1, 0, 0) de repouso).
struct AnimObject {
    Skel2 skel;
    uint8_t parents[2] = {0xff, 0x80};
    celglist_tag* meshes[1] = {nullptr};
    alignas(16) MATRIX inv[2];
    ANIMSKIN_tag skin{};
    obj_tag* obj = nullptr;
    AnimObject() {
        Mat_Identity(&inv[0]);
        inv[0].m[3][0] = inv[0].m[3][1] = inv[0].m[3][2] = 0.0f;
        Mat_Identity(&inv[1]);
        inv[1].m[3][0] = -1.0f;
        inv[1].m[3][1] = inv[1].m[3][2] = 0.0f;
        skin.parents = parents;
        skin.skeleton = 3;
        skin.n_meshes = 1;
        skin.meshes = meshes;
        skin.inv_pose = inv;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        hashtable_additem(0x5000123, &skin);
        _VECTOR pos{10.0f, 0.0f, 0.0f, 1.0f};
        obj = control_create_object(0, &pos, nullptr, nullptr);
        CHECK(AnimObjectNew(obj, 0x5000123) == 1);
    }
};
}  // namespace

TEST(anim_object_new_allocates_per_bone) {
    fresh();
    AnimObject o;
    ANIMOBJ_tag* anim = anim_of(o.obj);
    CHECK(anim != nullptr && anim->skin == &o.skin && (o.obj->mode & 8) != 0);
    CHECK(anim->frame_size == 2 * 32 && anim->frame != nullptr && anim->bones != nullptr && anim->palette != nullptr);
    CHECK(o.skin.hash == 0x5000123 && (anim->anim_flags & 2) == 0);  // 2 ossos: não é personagem
    AnimObjectDelete(o.obj);
    CHECK(o.obj->sphere_src == nullptr);
}

TEST(anim_bones_chain_and_palette) {
    fresh();
    AnimObject o;
    ANIMOBJ_tag* anim = anim_of(o.obj);
    // Raiz girada 90 graus em y, filho a (1, 0, 0) da raiz e sem rotação local.
    auto* t = reinterpret_cast<_VECTOR*>(anim->frame);
    auto* q = reinterpret_cast<quaternion_tag*>(anim->frame + 2 * 16);
    t[0] = {0, 0, 0, 0};
    t[1] = {1, 0, 0, 0};
    q[0] = {0.0f, std::sin(0.7853982f), 0.0f, std::cos(0.7853982f)};
    q[1] = {0.0f, 0.0f, 0.0f, 1.0f};
    anim->anim_flags |= 1;  // pose resolvida
    psiBuildMatrixPalette(o.obj, anim_state(anim), 1);
    // O filho fica onde o quatérnio da raiz leva (1, 0, 0), somado à posição do objeto.
    _VECTOR expect;
    const _VECTOR x{1, 0, 0, 0};
    Quat_Rot_Vec(&q[0], &x, &expect);
    _VECTOR got;
    AnimGetBoneWorldTrans(o.obj, 0x80000001, 0, &got, nullptr);
    CHECK_NEAR(got.x, 10.0f + expect.x, 1e-5f);
    CHECK_NEAR(got.y, expect.y, 1e-5f);
    CHECK_NEAR(got.z, expect.z, 1e-5f);
    // A paleta leva o vértice de repouso na junta (1, 0, 0) até a junta no mundo.
    const MATRIX& p = anim->palette[1];
    const float vx = 1.0f;
    CHECK_NEAR(vx * p.m[0][0] + p.m[3][0], got.x, 1e-5f);
    CHECK_NEAR(vx * p.m[0][2] + p.m[3][2], got.z, 1e-5f);
    // Sem pose (flag 1), zero e identidade.
    anim->anim_flags = 0;
    MATRIX m;
    AnimGetBoneWorldTrans(o.obj, 0x80000001, 0, &got, &m);
    CHECK(got.x == 0.0f && got.y == 0.0f && m.m[0][0] == 1.0f);
}

TEST(anim_script_commands_fire_once_when_crossed) {
    fresh();
    AnimObject o;
    // Script de 10 quadros com um comando de som no quadro 5.
    alignas(4) uint16_t data[] = {0, 0, 10, 1, 1, 5, 0x123};
    hashtable_additem(0x6000777, data);
    ANIMSCRIPT_tag* s = AnimScriptAdd(o.obj, 0x6000777);
    CHECK(s != nullptr && s->frame_count == 10 && s->unknown_b2 == 1);
    void* state = anim_state(anim_of(o.obj));
    const auto sounds = [] { return int(g_sound_stats.calls); };
    AnimProcessScriptCmds(o.obj, state, s, 3, 6);  // passa pelo 5
    CHECK(sounds() == 1 && (s->active_cmds & 1) != 0);
    AnimProcessScriptCmds(o.obj, state, s, 5, 5);  // parado em 5: ainda dentro, não repete
    CHECK(sounds() == 1);
    AnimProcessScriptCmds(o.obj, state, s, 6, 8);  // depois: sai
    CHECK(sounds() == 1 && (s->active_cmds & 1) == 0);
    AnimProcessScriptCmds(o.obj, state, s, 9, 2);  // deu a volta: 9 -> 10, 1 -> 2 não passa pelo 5
    CHECK(sounds() == 1);
    AnimProcessScriptCmds(o.obj, state, s, 8, 6);  // para a frente, 8 -> 6 deu a volta: passa pelo 5
    CHECK(sounds() == 2);
    AnimProcessScriptCmds(o.obj, state, s, 7, 9);  // sai
    s->speed = -1.0f;
    AnimProcessScriptCmds(o.obj, state, s, 4, 2);  // para trás, longe do 5
    CHECK(sounds() == 2);
    AnimProcessScriptCmds(o.obj, state, s, 8, 4);  // para trás: passa pelo 5
    CHECK(sounds() == 3);
}

TEST(anim_facial_append_marks_the_speech_face_script) {
    // AnimFacialAppend (o rosto de uma fala, NDrone2_SpeechSFX): o script vai para o fim da lista com a marca dos rostos
    // (0xe0000002, a que AnimListBuild 0x200 acha), apaga-se ao acabar (fim 3) e o rosto fica "falando" (2).
    fresh();
    AnimObject o;
    alignas(4) uint16_t data[] = {0, 0, 10, 0};
    hashtable_additem(0x6000779, data);
    ANIMSCRIPT_tag* body = AnimScriptAppend(o.obj, 0x6000779);
    ANIMSCRIPT_tag* face = AnimFacialAppend(o.obj, 0x6000779);
    CHECK(face != nullptr && body->next == face && face->next == nullptr);
    CHECK(face->tag == 0xe0000002u && face->end_mode == 3 && anim_of(o.obj)->face_state == 2);
    AnimListBuild(o.obj, 0x200, 0xe0000002u, nullptr, 1);
    CHECK(GlbAnimList == 1 && GlbAnimListItems[0] == face);
    // Lista cheia (32 scripts): o original escreveria no script nulo; aqui nada muda.
    while (AnimScriptAppend(o.obj, 0x6000779) != nullptr) {
    }
    anim_of(o.obj)->face_state = 1;
    CHECK(AnimFacialAppend(o.obj, 0x6000779) == nullptr && anim_of(o.obj)->face_state == 1);
}

TEST(anim_list_build_criteria) {
    fresh();
    AnimObject o;
    alignas(4) uint16_t data[] = {0, 0, 10, 0};
    hashtable_additem(0x6000778, data);
    ANIMSCRIPT_tag* a = AnimScriptAdd(o.obj, 0x6000778);
    ANIMSCRIPT_tag* b = AnimScriptAppend(o.obj, 0x6000778);
    ANIMSCRIPT_tag* c = AnimScriptAppend(o.obj, 0x6000778);
    CHECK(a->next == b && b->next == c && c->prev == b);
    b->tag = 7;
    c->sync = a;
    CHECK(AnimListBuild(o.obj, 1, 0, nullptr, 1) == a && GlbAnimList == 3);
    AnimListBuild(o.obj, 0x200, 7, nullptr, 1);
    CHECK(GlbAnimList == 1 && GlbAnimListItems[0] == b);
    AnimListBuild(o.obj, 1, 0, nullptr, 1);
    AnimListBuild(o.obj, 0x100, 0, nullptr, 2);  // tira os que têm script de referência
    CHECK(GlbAnimList == 2 && GlbAnimListItems[0] == a && GlbAnimListItems[1] == b);
    AnimListBuild(o.obj, 4 | 8, 0, nullptr, 1);  // o primeiro e o último
    CHECK(GlbAnimList == 2 && GlbAnimListItems[0] == a && GlbAnimListItems[1] == c);
    // Apagar o do meio religa a lista.
    AnimScriptDelete(o.obj, &b);
    CHECK(a->next == c && c->prev == a && b == nullptr);
}
