// Testes dos rostos: os alvos de morph do bloco 0x2B (nf::decode_morph e nf::morph_vertex, a conta do SkinIt que o
// shader repete), a sequência de rosto (AnimFrameCopy no modo de canal único), o estado do rosto
// (AnimFacialUpdate), a mistura dos pesos (AnimFacialBlend), a cabeça e os olhos acompanhando um alvo
// (AnimObjectHeadTrack, com Ang_RotateToBearing e Plane_PutPntOnPlane) e os pesos entre ticks (render/frame_interp.h).

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "data/map_item.h"
#include "engine/anim.h"
#include "engine/build.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/resetmap.h"
#include "engine/sound.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "render/frame_interp.h"
#include "test_framework.h"

namespace {

// ---------------------------------------------------------------- o bloco 0x2B

// Um bloco de morph como os do disco: cabeçalho (vértices, alvos, primeiro elemento), um índice por vértice e os
// deltas, alvo a alvo dentro de cada vértice.
std::vector<uint8_t> morph_block(uint32_t vertices, uint32_t targets, const std::vector<std::array<float, 3>>& deltas) {
    const uint32_t first = ((3 + vertices) * 4 + 11) / 12;
    std::vector<uint8_t> b(size_t(first + vertices * targets) * 12, 0);
    const auto put = [&](size_t at, const void* p) { std::memcpy(b.data() + at, p, 4); };
    put(0, &vertices);
    put(4, &targets);
    put(8, &first);
    for (uint32_t i = 0; i < vertices; ++i) {
        const uint32_t index = 7 + i;
        put(12 + size_t(i) * 4, &index);
    }
    for (size_t e = 0; e < deltas.size(); ++e) {
        for (int c = 0; c < 3; ++c) put((first + e) * 12 + size_t(c) * 4, &deltas[e][size_t(c)]);
    }
    return b;
}

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
    ScriptCam = 0;
    SEED_X = 1234;
    SEED_Y = 5678;
}

// Esqueleto de seis ossos em fila, cada um 0,2 m acima do pai (a coluna, osso 2, em y 0,4 e a cabeça, osso 5, em
// y 1,0), e uma pele com morphs (22 alvos) e uma malha (para haver paleta).
struct Face {
    alignas(16) uint8_t skel_data[0x10 + 6 * 12] = {};
    uint8_t parents[6] = {0xff, 0x80, 0x81, 0x82, 0x83, 0x84};
    celglist_tag* meshes[1] = {nullptr};
    alignas(16) MATRIX inv[6]{};
    ANIMSKIN_tag skin{};
    obj_tag* obj = nullptr;
    ANIMOBJ_tag* anim = nullptr;
    explicit Face(bool male = true) {
        auto* k = reinterpret_cast<ANIMSKEL_tag*>(skel_data);
        k->id = 4;
        k->bone_count = 6;
        for (int i = 1; i < 6; ++i) k->trans[i][1] = 0.2f;
        pSkeletons[4] = k;
        for (MATRIX& m : inv) Mat_Identity(&m);
        skin.parents = parents;
        skin.skeleton = 4;
        skin.n_meshes = 1;
        skin.meshes = meshes;
        skin.inv_pose = inv;
        skin.n_morphs = 22;
        skin.scale[0] = skin.scale[1] = skin.scale[2] = 1.0f;
        hashtable_additem(0x5000321, &skin);
        _VECTOR pos{0.0f, 0.0f, 0.0f, 1.0f};
        obj = control_create_object(0, &pos, nullptr, nullptr);
        CHECK(AnimObjectNew(obj, 0x5000321) == 1);
        anim = anim_of(obj);
        Mat_Identity(&obj->matrix);
        // Os homens têm anim_flags 0x400 (esqueleto diferente de 1).
        if (male) anim->anim_flags |= 0x400;
        auto* t = reinterpret_cast<_VECTOR*>(anim->frame);
        auto* q = reinterpret_cast<quaternion_tag*>(anim->frame + 6 * 16);
        for (int i = 0; i < 6; ++i) {
            t[i] = {0.0f, i == 0 ? 0.0f : 0.2f, 0.0f, 0.0f};
            q[i] = {0.0f, 0.0f, 0.0f, 1.0f};
        }
        anim->anim_flags |= 1;
    }
    void* state() { return anim_state(anim); }
    float* weights() { return anim->morph_weights; }
    // Os bones e a paleta de novo, com a pose do quadro (e o acompanhamento da cabeça).
    void rebuild() {
        anim->anim_flags &= ~0xcu;
        psiBuildMatrixPalette(obj, state(), 0);
    }
};

// Scripts de rosto vazios (sem comandos) para os hashes das tabelas, e um de fala.
alignas(4) uint16_t g_empty_script[] = {0, 0, 10, 0};
void add_face_scripts() {
    for (int i = 0; i < 5; ++i) {
        hashtable_additem(Male_Facial_State_Anims[i], g_empty_script);
        hashtable_additem(Female_Facial_State_Anims[i], g_empty_script);
    }
    hashtable_additem(0x6000400, g_empty_script);
}

int count_tag(obj_tag* obj, uint32_t tag, ANIMSCRIPT_tag** found = nullptr) {
    int n = 0;
    for (ANIMSCRIPT_tag* s = anim_of(obj)->scripts; s != nullptr; s = s->next) {
        if (s->tag != tag) continue;
        if (found != nullptr && n == 0) *found = s;
        ++n;
    }
    return n;
}

}  // namespace

TEST(face_morph_block_decode) {
    // Dois vértices com três alvos: o cabeçalho (2, 3, primeiro elemento 2 = ceil((3 + 2) x 4 / 12)), os índices e
    // os deltas. Os elementos do cabeçalho, lidos como float pelos vértices com m = 0, valem 0 (a VU0 não tem
    // desnormalizados).
    const std::vector<uint8_t> b = morph_block(2, 3, {{1, 0, 0}, {0, 2, 0}, {0, 0, 3}, {-1, 0, 0}, {0, -2, 0}, {0.5f, 0.5f, 0.5f}});
    const nf::MorphTargets m = nf::decode_morph(b);
    CHECK(m.vertices == 2 && m.targets == 3 && m.first == 2 && m.elements() == 8);
    for (size_t i = 0; i < 6; ++i) CHECK(m.deltas[i] == 0.0f);  // o cabeçalho
    CHECK(m.deltas[2 * 3 + 0] == 1.0f && m.deltas[3 * 3 + 1] == 2.0f && m.deltas[7 * 3 + 2] == 0.5f);
    // Tamanhos que não fecham com o cabeçalho e alvos demais são recusados.
    std::vector<uint8_t> cut(b.begin(), b.end() - 12);
    bool threw = false;
    try {
        nf::decode_morph(cut);
    } catch (const nf::FormatError&) {
        threw = true;
    }
    CHECK(threw);
    std::vector<uint8_t> many = b;
    const uint32_t targets = 23;
    std::memcpy(many.data() + 4, &targets, 4);
    threw = false;
    try {
        nf::decode_morph(many);
    } catch (const nf::FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(face_morph_vertex_weights) {
    // A conta do SkinIt: pos += soma de peso[k] x delta[m + k]. Peso 0 dá a pose de repouso exata, peso 1 num alvo dá
    // exatamente o alvo, e os alvos somam.
    const nf::MorphTargets m = nf::decode_morph(
        morph_block(2, 3, {{1, 0, 0}, {0, 2, 0}, {0, 0, 3}, {-1, 0, 0}, {0, -2, 0}, {0.5f, 0.5f, 0.5f}}));
    nf::SkinnedVertex v{};
    v.pos[0] = 0.1f;
    v.pos[1] = 0.2f;
    v.pos[2] = 0.3f;
    v.morph = int32_t(m.first + 1 * m.targets);  // o segundo vértice
    float p[3];
    const float zero[3] = {0, 0, 0};
    nf::morph_vertex(v, m, zero, 3, p);
    CHECK(p[0] == v.pos[0] && p[1] == v.pos[1] && p[2] == v.pos[2]);
    for (int k = 0; k < 3; ++k) {
        float w[3] = {0, 0, 0};
        w[k] = 1.0f;
        nf::morph_vertex(v, m, w, 3, p);
        for (int c = 0; c < 3; ++c) CHECK(p[c] == v.pos[c] + m.deltas[size_t(v.morph + k) * 3 + size_t(c)]);
    }
    const float half[3] = {0.5f, 0.5f, 0.0f};
    nf::morph_vertex(v, m, half, 3, p);
    CHECK_NEAR(p[0], 0.1f - 0.5f, 1e-6f);
    CHECK_NEAR(p[1], 0.2f - 1.0f, 1e-6f);
    CHECK_NEAR(p[2], 0.3f, 1e-6f);
    // Sem morph (-1) e com m = 0 (os vértices do disco que leem o cabeçalho): a pose de repouso.
    const float one[3] = {1, 1, 1};
    v.morph = -1;
    nf::morph_vertex(v, m, one, 3, p);
    CHECK(p[0] == 0.1f && p[1] == 0.2f && p[2] == 0.3f);
    // (Neste bloco pequeno só os elementos 0 e 1 são cabeçalho; nos do disco, os 22 primeiros ou mais.)
    const float header[3] = {1, 1, 0};
    v.morph = 0;
    nf::morph_vertex(v, m, header, 3, p);
    CHECK(p[0] == 0.1f && p[1] == 0.2f && p[2] == 0.3f);
}

TEST(face_sequence_single_channel) {
    // Uma sequência de rosto (flags bit 3) com 22 canais: cada canal é um peso, c0 + c1 (f - 1), e AnimFrameCopy os
    // põe em sequência (22 floats, o quadro de 0x58 bytes do slot 1 de cada script).
    fresh();
    std::vector<uint32_t> w(0x10, 0);
    for (int k = 0; k < 22; ++k) {
        const float c0 = 0.01f * float(k), c1 = k == 21 ? 0.1f : 0.0f;
        uint32_t mask = c0 != 0.0f ? 1u : 0u;
        if (c1 != 0.0f) mask |= 2;
        const uint32_t desc = 10u << 6 | mask;
        std::vector<uint32_t> coeffs;
        uint32_t bits;
        if (c0 != 0.0f) {
            std::memcpy(&bits, &c0, 4);
            coeffs.push_back(bits);
        }
        if (c1 != 0.0f) {
            std::memcpy(&bits, &c1, 4);
            coeffs.push_back(bits);
        }
        w.push_back((4 + uint32_t(coeffs.size()) * 4) | desc << 16);
        w.insert(w.end(), coeffs.begin(), coeffs.end());
    }
    auto* hdr = reinterpret_cast<uint8_t*>(w.data());
    const int16_t frames = 10, channels = 22;
    std::memcpy(hdr + 0x38, &frames, 2);
    std::memcpy(hdr + 0x3a, &channels, 2);
    hdr[0xc] = 8;  // modo de canal único
    hashtable_additem(0x4000300, w.data());
    AnimProcessSeqData(0x4000300);
    ANIMSEQ_tag seq{};
    CHECK(AnimSeqGetInfo(0x4000300, &seq, nullptr) != nullptr && seq.channels == 22 && (seq.flags & 8) != 0);
    seq.first_frame = 1;
    seq.last_frame = 10;
    alignas(16) float out[22] = {};
    AnimFrameCopy(out, &seq, nullptr, 4.0f);
    for (int k = 0; k < 21; ++k) CHECK_NEAR(out[k], 0.01f * float(k), 1e-6f);
    CHECK_NEAR(out[21], 0.21f + 0.3f, 1e-6f);  // quadro 4: f - 1 = 3

    // Num script, o comando 0 com a sequência de rosto vai para o slot 1 e marca o script como só de rosto (0x40000,
    // que AnimFrameResolve tira da mistura do corpo); a atualização decodifica o quadro no slot 1.
    Face f;
    alignas(4) uint16_t script[] = {0, 0, 10, 1, 0, 1, 10, 0x300};  // um comando: tipo 0, quadros 1-10, seq 0x300
    hashtable_additem(0x6000300, script);
    ANIMSCRIPT_tag* s = AnimScriptAppend(f.obj, 0x6000300);
    CHECK(s != nullptr && s->seq[1] != nullptr);
    if (s == nullptr || s->seq[1] == nullptr) return;
    AnimObjectUpdate(f.obj);
    CHECK((s->flags & 0x40000u) != 0 && s->seq[1]->hash == 0x4000300);
    const float* face = reinterpret_cast<const float*>(s->seq[1]->frame);
    CHECK_NEAR(face[5], 0.05f, 1e-6f);
}

TEST(face_state_machine) {
    fresh();
    add_face_scripts();
    Face f;
    // Estado 0: a expressão parada do alerta 0 (marca 0xe0000000, velocidade 0, fim 3) e a primeira piscada em 1,5
    // a 4,5 s.
    AnimFacialUpdate(f.obj, f.state());
    ANIMSCRIPT_tag* expr = nullptr;
    CHECK(f.anim->face_state == 1 && count_tag(f.obj, 0xe0000000u, &expr) == 1);
    CHECK(expr != nullptr && expr->hash == Male_Facial_State_Anims[0] && expr->speed == 0.0f && expr->end_mode == 3);
    CHECK(f.anim->face_timer >= 90 && f.anim->face_timer < 270);
    // Estado 1: conta até a piscada (marca 0xe0000001, a do índice 4); sem estado de alerta (não é personagem da
    // IA), a próxima em 1 a 3 s.
    f.anim->face_timer = 3;
    AnimFacialUpdate(f.obj, f.state());
    CHECK(f.anim->face_timer == 2 && count_tag(f.obj, 0xe0000001u) == 0);
    f.anim->face_timer = 0;
    AnimFacialUpdate(f.obj, f.state());
    ANIMSCRIPT_tag* blink = nullptr;
    CHECK(count_tag(f.obj, 0xe0000001u, &blink) == 1 && blink->hash == Male_Facial_State_Anims[4]);
    CHECK(blink->end_mode == 3 && f.anim->face_timer >= 60 && f.anim->face_timer < 180);
    // Numa cena, ou com a flag 0x10000, nada.
    const int16_t timer = f.anim->face_timer;
    ScriptCam = 0x6000001;
    AnimFacialUpdate(f.obj, f.state());
    ScriptCam = 0;
    f.anim->anim_flags |= 0x10000;
    AnimFacialUpdate(f.obj, f.state());
    f.anim->anim_flags &= ~0x10000u;
    CHECK(f.anim->face_timer == timer);
    // Estado 2 (AnimFacialAppend): fica enquanto houver script de fala; sem ele, volta a 1.
    ANIMSCRIPT_tag* talk = AnimFacialAppend(f.obj, 0x6000400);
    CHECK(talk != nullptr && f.anim->face_state == 2);
    AnimFacialUpdate(f.obj, f.state());
    CHECK(f.anim->face_state == 2);
    AnimScriptDelete(f.obj, &talk);
    AnimFacialUpdate(f.obj, f.state());
    CHECK(f.anim->face_state == 1);
}

TEST(face_state_follows_the_drone_alert) {
    // Um personagem da IA (tipo 2): a mudança de alerta troca a expressão (a da tabela pelo alerta, parada) e apaga a
    // marca de mudança; no alerta 3 não pisca; o fim de uma fala marca o alerta como mudado (a expressão é refeita).
    fresh();
    add_face_scripts();
    Face f;
    alignas(16) static uint8_t drone[0xd20];
    std::memset(drone, 0, sizeof(drone));
    auto* d = reinterpret_cast<Drone_tag*>(drone);
    f.obj->type = 2;
    f.obj->data = d;
    CHECK(Drone_AlertStatus(f.obj) == &d->alert_status);
    f.anim->face_state = 1;
    d->alert_status = 2;
    d->alert_status_changed = 1;
    AnimFacialUpdate(f.obj, f.state());
    ANIMSCRIPT_tag* expr = nullptr;
    CHECK(d->alert_status_changed == 0 && count_tag(f.obj, 0xe0000000u, &expr) == 1);
    CHECK(expr != nullptr && expr->hash == Male_Facial_State_Anims[2] && expr->speed == 0.0f);
    d->alert_status = 3;
    f.anim->face_timer = 0;
    AnimFacialUpdate(f.obj, f.state());
    CHECK(f.anim->face_timer == 0 && count_tag(f.obj, 0xe0000001u) == 0);
    f.anim->face_state = 2;
    AnimFacialUpdate(f.obj, f.state());
    CHECK(f.anim->face_state == 1 && d->alert_status_changed == 1);
    f.obj->type = 0;
    CHECK(Drone_AlertStatus(f.obj) == nullptr);
}

TEST(face_female_blinks) {
    // As mulheres (esqueleto 1, sem anim_flags 0x400): depois de cada piscada, ou nada, ou a próxima vem logo (bit
    // 0x80, de 0,1 a 0,2 s, e a seguinte o apaga), ou a piscada é lenta (velocidade 0,2 a 0,4).
    fresh();
    add_face_scripts();
    Face f(false);
    f.anim->face_state = 1;
    int quick = 0, slow = 0, plain = 0;
    for (int i = 0; i < 200; ++i) {
        f.anim->face_timer = 0;
        const bool had = (f.anim->face_state & 0x80) != 0;
        AnimFacialUpdate(f.obj, f.state());
        ANIMSCRIPT_tag* blink = nullptr;
        for (ANIMSCRIPT_tag* s = f.anim->scripts; s != nullptr; s = s->next) {
            if (s->tag == 0xe0000001u) blink = s;
        }
        CHECK(blink != nullptr && blink->hash == Female_Facial_State_Anims[4]);
        if (blink == nullptr) return;
        if (had) {
            CHECK((f.anim->face_state & 0x80) == 0);
        } else if ((f.anim->face_state & 0x80) != 0) {
            ++quick;
            CHECK(f.anim->face_timer >= 6 && f.anim->face_timer < 12);
        } else if (blink->speed != 1.0f) {
            ++slow;
            CHECK(blink->speed >= 0.2f && blink->speed < 0.4f);
        } else {
            ++plain;
        }
        AnimScriptDelete(f.obj, &blink);
    }
    CHECK(quick > 0 && slow > 0 && plain > 0);
}

TEST(face_blend_layers_and_eyes) {
    fresh();
    add_face_scripts();
    Face f;
    ANIMSCRIPT_tag* base = AnimScriptAppend(f.obj, Male_Facial_State_Anims[0]);
    ANIMSCRIPT_tag* blink = AnimScriptAppend(f.obj, Male_Facial_State_Anims[4]);
    ANIMSCRIPT_tag* talk = AnimScriptAppend(f.obj, 0x6000400);
    base->tag = 0xe0000000u;
    blink->tag = 0xe0000001u;
    talk->tag = 0xe0000002u;
    auto* fb = reinterpret_cast<float*>(base->seq[1]->frame);
    auto* fk = reinterpret_cast<float*>(blink->seq[1]->frame);
    auto* ft = reinterpret_cast<float*>(talk->seq[1]->frame);
    for (int k = 0; k < 22; ++k) fb[k] = 0.1f * float(k);
    fk[10] = 1.0f;
    fk[3] = 0.005f;  // abaixo de 0,01: não cobre
    ft[0] = 0.8f;
    ft[3] = -0.5f;
    // Os olhos sem acompanhar a cabeça (0, 0): os alvos 13 e 12 vão a 1 (os 14 e 11 ficam com a expressão).
    AnimFacialBlend(f.obj, f.state(), &f.skin);
    const float* w = f.weights();
    CHECK(w[0] == 0.8f && w[3] == -0.5f && w[10] == 1.0f && w[5] == fb[5]);
    CHECK(w[13] == 1.0f && w[12] == 1.0f && w[14] == fb[14] && w[11] == fb[11]);
    // Olhos no meio (127): metade e metade; no máximo (255): 14 e 11 em 1.
    f.anim->eye_x = 127;
    f.anim->eye_y = 255;
    AnimFacialBlend(f.obj, f.state(), &f.skin);
    CHECK_NEAR(w[14], 127.0f / 255.0f, 1e-6f);
    CHECK_NEAR(w[13], 128.0f / 255.0f, 1e-6f);
    CHECK_NEAR(w[11], 1.0f, 1e-6f);
    CHECK(w[12] == fb[12]);
    // Numa cena, só a fala, inteira (os olhos e a expressão não entram).
    ScriptCam = 0x6000001;
    AnimFacialBlend(f.obj, f.state(), &f.skin);
    ScriptCam = 0;
    for (int k = 0; k < 22; ++k) CHECK(w[k] == ft[k]);
}

TEST(face_ang_rotate_to_bearing) {
    const float pi = 3.1415927f, two_pi = 6.2831855f;
    CHECK(Ang_RotateToBearing(0.0f, 1.0f, 1.0f, pi) == 1.0f);
    CHECK_NEAR(Ang_RotateToBearing(0.0f, 1.0f, 0.5f, pi), 0.5f, 1e-6f);
    CHECK_NEAR(Ang_RotateToBearing(0.0f, 1.0f, 1.0f, 0.3f), 0.3f, 1e-6f);  // passo preso
    // O caminho mais curto por cima de 0/2pi; o resultado parte do atual sem normalizar.
    CHECK_NEAR(Ang_RotateToBearing(0.1f, two_pi - 0.1f, 1.0f, pi), -0.1f, 1e-5f);
    CHECK_NEAR(Ang_RotateToBearing(6.0f, 0.2f, 1.0f, pi), 6.0f + (0.2f + two_pi - 6.0f), 1e-5f);
    // A menos de 0,0001 do alvo, o próprio alvo.
    CHECK(Ang_RotateToBearing(1.00005f, 1.0f, 0.5f, pi) == 1.0f);
}

TEST(face_plane_put_point_on_plane) {
    alignas(16) const plane_equ_tag plane{0.0f, 1.0f, 0.0f, -2.0f};  // y = 2
    alignas(16) const _VECTOR p{3.0f, 5.0f, -1.0f, 7.0f};
    _VECTOR out;
    Plane_PutPntOnPlane(&out, &p, &plane);
    CHECK(out.x == 3.0f && out.y == 2.0f && out.z == -1.0f && out.w == 7.0f);
    // Plano inclinado: o resultado está no plano e a diferença é paralela à normal.
    const float h = 0.70710678f;
    alignas(16) const plane_equ_tag tilted{h, h, 0.0f, -1.0f};
    Plane_PutPntOnPlane(&out, &p, &tilted);
    CHECK_NEAR(tilted.a * out.x + tilted.b * out.y + tilted.c * out.z + tilted.d, 0.0f, 1e-5f);
    CHECK_NEAR((p.x - out.x) - (p.y - out.y), 0.0f, 1e-5f);
    CHECK_NEAR(p.z - out.z, 0.0f, 1e-6f);
}

TEST(face_head_track_turns_head_and_eyes) {
    fresh();
    Face f;
    // O alvo 45 graus para +x, na altura da cabeça (y 1); acompanhar (0x10) com velocidade 1 (vai de uma vez).
    alignas(16) _VECTOR target{1.0f, 1.0f, 1.0f, 0.0f};
    f.anim->head_target = &target;
    f.anim->head_dist_sq = 100.0f;
    f.anim->head_speed = 1.0f;
    f.anim->anim_flags |= 0x10;
    f.rebuild();
    const MATRIX& head = f.anim->bones[5];
    CHECK_NEAR(head.m[3][1], 1.0f, 1e-5f);
    // A frente da cabeça (linha 2) aponta para o alvo; o giro fica em head_yaw (positivo: em volta de +y, de +z
    // para +x) e os olhos no meio (a cabeça já chegou).
    CHECK_NEAR(head.m[2][0], 0.70710678f, 1e-4f);
    CHECK_NEAR(head.m[2][2], 0.70710678f, 1e-4f);
    CHECK_NEAR(f.anim->head_yaw, 0.7853982f, 1e-4f);
    CHECK_NEAR(f.anim->head_pitch, 0.0f, 1e-6f);
    CHECK((f.anim->anim_flags & 0x20) != 0 && f.anim->eye_x == 127 && f.anim->eye_y == 127);
    // Devagar (metade do caminho por tick), a cabeça fica atrás e os olhos vão na frente: eye_x no máximo.
    f.anim->head_yaw = 0.0f;
    f.anim->head_speed = 0.5f;
    f.rebuild();
    CHECK_NEAR(f.anim->head_yaw, 0.3926991f, 1e-4f);
    CHECK(f.anim->eye_x == 255);
    // Do outro lado, o giro é negativo; atrás da cabeça (mais de 2,6 rad), o giro alvo é 0.
    target = {-1.0f, 1.0f, 1.0f, 0.0f};
    f.anim->head_yaw = 0.0f;
    f.anim->head_speed = 1.0f;
    f.rebuild();
    CHECK_NEAR(f.anim->head_yaw, -0.7853982f, 1e-4f);
    CHECK(f.anim->eye_x == 127);
    target = {0.0f, 1.0f, -2.0f, 0.0f};
    f.anim->head_yaw = 0.0f;
    f.rebuild();
    CHECK_NEAR(f.anim->head_yaw, 0.0f, 1e-6f);
    // Durante head_delay, nada se move (os alvos são os ângulos atuais).
    target = {1.0f, 1.0f, 1.0f, 0.0f};
    f.anim->head_yaw = 0.2f;
    f.anim->head_delay = 2;
    f.rebuild();
    CHECK_NEAR(f.anim->head_yaw, 0.2f, 1e-6f);
    CHECK(f.anim->head_delay == 1);
    f.anim->head_delay = 0;
    // Sem 0x10 (soltou o alvo): a cabeça volta ao meio; quase lá, apaga 0x30 e zera os olhos.
    f.anim->anim_flags = (f.anim->anim_flags & ~0x10u) | 0x20u;
    f.anim->head_yaw = 0.005f;
    f.anim->head_pitch = 0.0f;
    f.rebuild();
    CHECK((f.anim->anim_flags & 0x30) == 0 && f.anim->eye_x == 0 && f.anim->eye_y == 0);
}

TEST(face_weights_interpolate_between_ticks) {
    using namespace nf;
    std::vector<float> out;
    interp_weights({0.0f, 1.0f}, {1.0f, 0.0f, 0.5f}, 0.25f, &out);
    CHECK(out.size() == 3 && out[0] == 0.25f && out[1] == 0.75f && out[2] == 0.125f);
    interp_weights({0.0f, 1.0f}, {1.0f, 0.0f, 0.5f}, 0.0f, &out);
    CHECK(out == std::vector<float>({0.0f, 1.0f, 0.0f}));
    interp_weights({0.0f, 1.0f}, {1.0f, 0.0f, 0.5f}, 1.0f, &out);
    CHECK(out == std::vector<float>({1.0f, 0.0f, 0.5f}));

    // Um rosto parado cuja boca abre entre dois ticks: o quadro no meio tem os pesos no meio; nas pontas, os do tick.
    DrawItem p;
    p.mesh = 3;
    p.world = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    p.palette = {p.world};
    p.interp.id = 77;
    p.interp.list = 0;
    p.interp.owner_kind = uint8_t(PsiOwnerKind::Object);
    DrawItem c = p;
    p.morph = {0.0f, 0.2f};
    c.morph = {1.0f, 0.2f};
    TickRecord a, b;
    a.items = {p};
    b.items = {c};
    a.camera.fov_deg = b.camera.fov_deg = 60.0f;
    FrameInterpolator fi;
    fi.push(std::move(a));
    fi.push(std::move(b));
    InterpCamera cam;
    const std::vector<DrawItem>& mid = fi.build(0.5f, &cam);
    CHECK(mid.size() == 1 && fi.stats().morphs == 1 && fi.stats().moving == 1);
    if (mid.size() != 1) return;
    CHECK(mid[0].morph == std::vector<float>({0.5f, 0.2f}));
    CHECK(fi.build(0.0f, &cam)[0].morph == p.morph);
    CHECK(fi.build(1.0f, &cam)[0].morph == c.morph);
}
