// Emissores de partículas (módulo Emitter do ACTION.ELF). Ver emitter.h.
//
// Divergências, todas documentadas na função:
// - ColourGetValue: quando t + 0.001 alcança a última chave, o original interpola com a chave
//   seguinte, uma além do fim da tabela (os bytes do próximo bloco do heap). Aqui a última chave é
//   repetida, que é o resultado do original sempre que esses bytes não são um float enorme (o peso
//   dessa chave fica abaixo de 0.001 / distância).
// - Emitter_Update: o original soma a w de cada posição um float não inicializado da pilha (a VU0
//   só escreve xyz do deslocamento) e deixa lixo na w das velocidades; aqui as duas ficam como
//   estavam (o microprograma de partículas não lê a w).
// - Emitter_Create: se Control_CreateObjEx não tiver memória, devolve nulo (o original escreveria no
//   endereço 0xe0); a coluna w das linhas 0-2 da matriz é zero (lixo da pilha no original).
// - Emitter_CreatePlist (chamado por parsemap_create_dynamic_objects): a w da linha de translação é
//   1 (o original passa um word não inicializado da pilha, que só chega à w da posição).
// - Canais (switch_channels) a partir de 256: leitura vale 0 e escrita é ignorada (o original leria e
//   escreveria além da tabela). Os dados do jogo só usam canais menores.

#include "engine/emitter.h"

#include <cstring>

#include "engine/build.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/memory.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"

uint32_t g_emitters_created = 0;

namespace {
// Globais do original: o resultado de ColourGetValue (clr_128), as chaves e os pesos da última
// interpolação (Key_114, DAT_0030d734, Weights_115, DAT_0030d73c), o contador de Emitter_Draw (i_135)
// e a matriz de cada modelo (M_136).
KEYED_CLR clr_128;
int32_t Key_114 = 0;
int32_t key_next = 0;
float Weights_115 = 0.0f;
float weight_next = 0.0f;
uint16_t i_135 = 0;
MATRIX M_136;

uint8_t to_byte(float v) { return uint8_t(int32_t(v)); }  // cvt.w.s e sb: trunca e fica o byte baixo

// switch_channels[ch]; canais além da tabela (256) valem 0 (divergência: o original leria fora dela).
uint8_t channel(uint32_t ch) { return ch < 256 ? switch_channels[ch] : 0; }
}  // namespace

// [decomp] 0x0017a938 ColourGetValue
// Cor no instante t (0..1 da vida): a última chave com time <= t + 0.001 (ou a primeira, se nenhuma)
// misturada linearmente com a seguinte. count é o número de chaves.
// Divergência: com a última chave escolhida o original mistura com keys[count], além do fim da
// tabela; aqui a seguinte é a própria última chave (ver o cabeçalho).
void ColourGetValue(KEYED_CLR* out, float t, KEYED_CLR* keys, int32_t count) {
    const float at = t + 0.001f;
    Key_114 = -1;
    for (int32_t k = count - 1; k >= 0; --k) {
        if (keys[k].time <= at) {
            Key_114 = k;
            break;
        }
    }
    if (Key_114 >= count) {  // inalcançável, como no original
        Weights_115 = 1.0f;
        weight_next = 0.0f;
        Key_114 = count;
        key_next = count;
    } else if (Key_114 < 0) {
        Key_114 = 0;
        Weights_115 = 1.0f;
        key_next = 0;
        weight_next = 0.0f;
    } else {
        key_next = Key_114 + 1;
        if (key_next >= count) {
            // Divergência: o original leria keys[count].
            key_next = Key_114;
            weight_next = 0.0f;
        } else {
            weight_next = (at - keys[Key_114].time) / (keys[key_next].time - keys[Key_114].time);
        }
        Weights_115 = 1.0f - weight_next;
    }
    const KEYED_CLR& a = keys[Key_114];
    const KEYED_CLR& b = keys[key_next];
    out->r = a.r * Weights_115 + b.r * weight_next;
    out->g = a.g * Weights_115 + b.g * weight_next;
    out->b = a.b * Weights_115 + b.b * weight_next;
    out->a = a.a * Weights_115 + b.a * weight_next;
}

// [decomp] 0x0017ab40 Emitter_LoadDefs
// Bloco 0x30: u32 quantidade e as definições (formato em EMITTERDEF_tag). Para no primeiro registro
// cujo byte baixo do tipo não é 4. A textura é a primeira palavra do item 0x03 (o pstex_tag que
// psiCreateMapTextures já criou: o bloco 0x30 vem depois das entidades do item) e o modelo é o
// celglist_tag do item 0x02. Comportamento original preservado: sem memória para uma definição, as
// chaves dela não são lidas e a próxima definição começa no meio delas.
void Emitter_LoadDefs(uint8_t* data) {
    uint8_t* p = data;
    const uint32_t n = BIN_GetDWord(&p);
    for (uint32_t i = 0; i < n; i = (i + 1) & 0xffff) {
        const uint32_t type = BIN_GetDWord(&p);
        if ((type & 0xff) != 4) return;
        const uint32_t hash = BIN_GetDWord(&p);
        EMITTERDEF_tag def{};
        def.flags = uint16_t(type >> 8);
        const uint32_t tex_hash = BIN_GetDWord(&p);
        const uint32_t glist_hash = BIN_GetDWord(&p);
        def.count = BIN_GetWord(&p);
        def.rate = BIN_GetWord(&p);
        def.size = BIN_GetFloat(&p);
        def.life = BIN_GetFloat(&p);
        def.life_rand = BIN_GetFloat(&p);
        def.gravity = BIN_GetFloat(&p);
        def.speed = BIN_GetFloat(&p);
        def.speed_rand = BIN_GetFloat(&p);
        def.polar = BIN_GetFloat(&p);
        def.polar_rand = BIN_GetFloat(&p);
        def.azimuth = BIN_GetFloat(&p);
        def.azimuth_rand = BIN_GetFloat(&p);
        def.key_count = BIN_GetDWord(&p);
        const void* tex_item = tex_hash != 0xffffffff ? hashtable_getitem(tex_hash) : nullptr;
        def.texture = nullptr;
        if (tex_item != nullptr) std::memcpy(&def.texture, tex_item, 4);
        def.glist = glist_hash == 0xffffffff ? nullptr : static_cast<celglist_tag*>(hashtable_getitem(glist_hash));
        auto* mem = static_cast<EMITTERDEF_tag*>(Mem_Malloc(def.key_count * 0x14 + 0x40, 0x4204, 0));
        if (mem == nullptr) continue;
        amemcpy(mem, &def, 0x40);
        mem->keys = reinterpret_cast<KEYED_CLR*>(mem + 1);
        for (uint32_t k = 0; k < def.key_count; k = (k + 1) & 0xffff) {
            KEYED_CLR& key = mem->keys[k];
            key.r = BIN_GetFloat(&p) * 0.5f;
            key.g = BIN_GetFloat(&p) * 0.5f;
            key.b = BIN_GetFloat(&p) * 0.5f;
            key.a = BIN_GetFloat(&p);
            key.time = BIN_GetFloat(&p);
        }
        hashtable_additem(hash, mem);
    }
}

// [decomp] 0x0017ae00 Emitter_Create
// Emissor da definição "hash" em pos, emitindo em volta de dir (o eixo y local; x e z são uma base
// ortonormal qualquer). duration: segundos de emissão (< 0 sempre, 0 uma rajada só: cada partícula
// nasce uma vez). switch_on/switch_off: canais que ligam e desligam a emissão; switch_alive: canal
// ligado (com a hora em switch_channels_time) enquanto alguma partícula vive. period: pisca (emite
// period / 2 segundos e para period segundos); 0 é contínuo. A esfera do objeto tem o alcance máximo
// de uma partícula (velocidade máxima * vida máxima).
// Divergências: ver o cabeçalho (sem memória devolve nulo; coluna w zerada).
obj_tag* Emitter_Create(uint32_t hash, _VECTOR* pos, _VECTOR* dir, float duration, uint32_t switch_on,
                        uint32_t switch_off, uint32_t switch_alive, float period) {
    if (hash == 0) return nullptr;
    auto* def = static_cast<EMITTERDEF_tag*>(hashtable_getitem(hash));
    if (def == nullptr) return nullptr;
    const uint32_t data_size = (uint32_t(def->count) * 0x2c + 0x150) & 0xfffc;
    MATRIX m{};
    std::memcpy(&m.m[3][0], pos, sizeof(_VECTOR));
    _VECTOR u{}, v{};
    Vec_MakeOrthonormalBasis(&u, &v, dir);
    Mat_SetDir(&u, &m);
    Mat_SetNorm(&v, &m);
    Mat_SetUp(dir, &m);
    obj_tag* obj = Control_CreateObjEx(uint16_t(data_size), nullptr, nullptr, &m, nullptr, nullptr, 1, 0x104, 1.0f, 0,
                                       0xff, 0xff, 0xff);
    if (obj == nullptr) return nullptr;  // divergência: o original não testa
    obj->type = kEmitterObjectType;
    auto* e = static_cast<EMITTER_tag*>(obj->data);
    uint8_t* p = reinterpret_cast<uint8_t*>(e) + sizeof(EMITTER_tag);
    e->vel = reinterpret_cast<_VECTOR*>(p);
    p += size_t(def->count) * sizeof(_VECTOR);
    e->pos = reinterpret_cast<_VECTOR*>(p);
    p += size_t(def->count) * sizeof(_VECTOR);
    e->age = reinterpret_cast<float*>(p);
    p += size_t(def->count) * 4;
    e->life = reinterpret_cast<float*>(p);
    p += size_t(def->count) * 4;
    e->colour = p;
    e->def = def;
    e->dying = 0;
    e->duration = duration;
    e->switch_on = switch_on;
    e->switch_off = switch_off;
    e->switch_alive = switch_alive;
    e->period = period;
    e->one_shot = duration == 0.0f ? 1 : 0;
    e->period_on = 1;
    e->enabled = 1;
    e->timer = 0.0f;
    Vec_Zero(&e->rot);
    for (uint32_t i = 0; i < e->def->count; i = (i + 1) & 0xffff) {
        e->age[i] = kEmitterNotBorn;
        e->life[i] = 0.0f;
        std::memset(e->colour + i * 4, 0, 4);
    }
    std::memcpy(&obj->sphere, pos, sizeof(_VECTOR));
    obj->flags |= 0x21;
    obj->sphere.w = (def->speed + def->speed_rand) * (def->life + def->life_rand);
    ++g_emitters_created;
    return obj;
}

// [decomp] 0x0017b080 Emitter_Update
// Um tick: os canais ligam ou desligam a emissão, o pisca alterna, a duração acaba (vira rajada
// única). Cada partícula viva ganha a cor do instante (ColourGetValue pela fração da vida), anda
// vel * REC_FRAME_RATE, cai (vel.y -= gravity * 9.8 * REC_FRAME_RATE, depois de andar) e envelhece;
// as mortas ficam com alfa 0. Depois nascem até `rate` partículas nas vagas (morta ou nunca nascida),
// na posição do emissor, com a cor da primeira chave, a vida sorteada e a velocidade sorteada em
// volta do eixo y do emissor. Com todas mortas numa rajada (ou já morrendo e todas mortas neste
// tick), o emissor conta três ticks e pede a remoção.
// Divergências: a w das posições e das velocidades não recebe lixo da pilha; canais além de 255 (ver
// o cabeçalho).
void Emitter_Update(obj_tag* obj) {
    auto* e = static_cast<EMITTER_tag*>(obj->data);
    obj->sphere.x = obj->matrix.m[3][0];
    obj->sphere.y = obj->matrix.m[3][1];
    obj->sphere.z = obj->matrix.m[3][2];
    if (e->switch_on != 0) e->enabled = channel(e->switch_on);
    if (e->switch_off != 0) e->enabled = channel(e->switch_off) == 0 ? 1 : 0;
    if (e->period != 0.0f) {
        float limit = e->period;
        if (e->period_on != 0) limit = limit * 0.5f;
        e->timer = e->timer + REC_FRAME_RATE;
        if (limit < e->timer) {
            e->timer = 0.0f;
            e->period_on = e->period_on == 0 ? 1 : 0;
        }
    }
    if (0.0f < e->duration) {
        e->duration = e->duration - REC_FRAME_RATE;
        if (e->duration < 0.0f) e->one_shot = 1;
    }
    if (e->switch_alive < 256 && e->switch_alive != 0) switch_channels[e->switch_alive] = 0;

    EMITTERDEF_tag* def = e->def;
    uint32_t dead_now = 0;
    for (uint32_t i = 0; i < def->count; ++i) {
        uint8_t* c = e->colour + i * 4;
        if (e->age[i] < e->life[i]) {
            ColourGetValue(&clr_128, e->age[i] * (1.0f / e->life[i]), def->keys, int32_t(def->key_count));
            c[0] = to_byte(clr_128.r);
            c[1] = to_byte(clr_128.g);
            c[2] = to_byte(clr_128.b);
            c[3] = to_byte(clr_128.a);
            _VECTOR& pos = e->pos[i];
            _VECTOR& vel = e->vel[i];
            pos.x = pos.x + vel.x * REC_FRAME_RATE;
            pos.y = pos.y + vel.y * REC_FRAME_RATE;
            pos.z = pos.z + vel.z * REC_FRAME_RATE;
            vel.y = vel.y + REC_FRAME_RATE * (e->def->gravity * -9.8f);
            e->age[i] = e->age[i] + REC_FRAME_RATE;
            if (e->switch_alive < 256 && e->switch_alive != 0) {
                switch_channels[e->switch_alive] = 1;
                switch_channels_time[e->switch_alive] = GameState.frame;
            }
        } else {
            ++dead_now;
            c[3] = 0;
        }
        def = e->def;
    }

    // Nascimentos: até `rate` por tick, enquanto vivo, ligado e na fase de emissão do pisca.
    int32_t births = def->rate;
    if (def->count != 0 && births > 0 && e->dying == 0 && e->enabled != 0 && e->period_on != 0) {
        for (uint32_t i = 0;;) {
            float& age = e->age[i];
            if (e->life[i] < age && (e->one_shot == 0 || !(age < kEmitterNotBorn))) {
                age = 0.0f;
                --births;
                uint8_t* c = e->colour + i * 4;
                const KEYED_CLR& k0 = e->def->keys[0];
                c[0] = to_byte(k0.r);
                c[1] = to_byte(k0.g);
                c[2] = to_byte(k0.b);
                c[3] = to_byte(k0.a);
                const float azimuth = e->def->azimuth + e->def->azimuth_rand * Rand_FRandHalf(1.0f);
                const float polar = e->def->polar + e->def->polar_rand * Rand_FRandHalf(1.0f);
                float cos_p, sin_p, cos_a, sin_a;
                PS2Sinf2(polar + 1.5707964f, polar, &cos_p, &sin_p);
                PS2Sinf2(azimuth + 1.5707964f, azimuth, &cos_a, &sin_a);
                _VECTOR v{sin_p * cos_a, cos_p, sin_p * sin_a, 0.0f};
                Vec_MulR32(&v, &v, e->def->speed + e->def->speed_rand * Rand_FRandHalf(1.0f));
                std::memcpy(&e->pos[i], &obj->matrix.m[3][0], sizeof(_VECTOR));
                e->life[i] = e->def->life + e->def->life_rand * Rand_FRandHalf(1.0f);
                // vmulax/vmadday/vmaddz.xyz: v pela rotação do emissor (linhas 0, 1 e 2).
                const MATRIX& m = obj->matrix;
                _VECTOR& vel = e->vel[i];
                vel.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
                vel.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
                vel.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
                vel.w = v.w;
            }
            ++i;
            if (e->def->count <= i || births < 1 || e->dying != 0 || e->enabled == 0 || e->period_on == 0) break;
        }
    }
    def = e->def;

    uint32_t dead = 0;
    for (uint32_t i = 0; i < def->count; ++i) {
        if (e->life[i] <= e->age[i]) ++dead;
    }
    const uint8_t dying = e->dying;
    e->done = uint8_t(dying | (dead == def->count ? 1 : 0));
    const bool finished = (dying != 0 && dead_now == def->count) || (e->one_shot != 0 && dead == def->count);
    if (!finished) return;
    e->dying = uint8_t(dying + 1);
    if (2 < e->dying) obj->life |= 1;
}

// [decomp] 0x0017b708 Emitter_Draw
// Sem modelo: a lista de sprites (psiDrawParticleList; aditiva se flags == 0, transparente senão).
// Com modelo: cada partícula viva (alfa != 0) desenha o modelo com a cor dela no tweak, na posição
// dela e com a rotação do emissor, que cresce REC_FRAME_RATE em x e y a cada modelo desenhado.
void Emitter_Draw(obj_tag* obj, viewer_tag*) {
    auto* e = static_cast<EMITTER_tag*>(obj->data);
    if (e->done != 0) return;
    EMITTERDEF_tag* def = e->def;
    if (def->glist == nullptr) {
        psiDrawParticleList(reinterpret_cast<_VECTOR4*>(e->pos), int32_t(def->count), 0xffffffffu,
                            uint32_t(reinterpret_cast<uintptr_t>(def->texture)),
                            reinterpret_cast<uint32_t*>(e->colour), int8_t(def->flags == 0 ? 1 : 0), def->size,
                            e->unknown_18);
        return;
    }
    for (i_135 = 0; i_135 < e->def->count; ++i_135) {
        const uint8_t* c = e->colour + uint32_t(i_135) * 4;
        if (c[3] == 0) continue;
        psiSetTweakARGB(c[3], c[0], c[1], c[2]);
        e->rot.x = e->rot.x + REC_FRAME_RATE;
        e->rot.y = e->rot.y + REC_FRAME_RATE;
        e->rot.x = LimitRadAngle(e->rot.x);
        e->rot.y = LimitRadAngle(e->rot.y);
        View_RotTransMatrix(&e->rot, &e->pos[i_135], &M_136);
        psiDrawObjectMatrix(e->def->glist, &M_136, 0);
    }
}

// [decomp] 0x0017b848 Emitter_CreatePlist
// Emissor posicionado na fase: na translação de m, emitindo na direção da linha 1 (para cima). Os
// parâmetros do posicionamento: [0] definição, [1] duração em ticks (0: sempre, 1: rajada única),
// [2] canal que liga, [3] canal que desliga, [4] canal de partículas vivas, [5] pisca em ticks.
obj_tag* Emitter_CreatePlist(MATRIX* m, level_tag* level) {
    _VECTOR up;
    Mat_GetUp(&up, m);
    const uint32_t d = level->params[1];
    const float duration = d == 0 ? -1.0f : (d == 1 ? 0.0f : float(d) * 0.016666668f);
    return Emitter_Create(level->params[0], reinterpret_cast<_VECTOR*>(&m->m[3][0]), &up, duration,
                          level->params[2], level->params[3], level->params[4], float(level->params[5]) * 0.016666668f);
}

// [decomp] 0x0017b960 Emitter_Kill
// Vazia no original: os dados ficam no bloco do próprio objeto.
void Emitter_Kill(obj_tag*) {}
