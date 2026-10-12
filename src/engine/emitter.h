#pragma once
// Emissores de partículas (módulo Emitter do ACTION.ELF, 0x0017a938-0x0017b968): faíscas e poeira
// dos tiros (Effect_Create), fontes de partículas posicionadas na fase (Emitter_CreatePlist) e os
// emissores de alguns objetos (GT, SpaceLaser, Lock, rastros de bala).
//
// As definições vêm do bloco 0x30 dos itens de mapa (parsemap_block_particles): Emitter_LoadDefs
// copia cada uma para o heap e a registra no hashtable pelo próprio hashcode (tipo 0x0c). Um emissor
// é um objeto do tipo 0x32 (mode 0x104, flags 0x21: lista transparente) com a definição, as
// partículas (velocidade, posição, idade, vida, cor) e o estado de liga/desliga. Emitter_Update faz
// nascer até `rate` partículas por tick, move cada uma (gravidade em y), envelhece e interpola a cor
// pelas chaves (ColourGetValue). Emitter_Draw desenha a lista de sprites (psiDrawParticleList) ou,
// se a definição tem modelo, um modelo por partícula com a cor no tweak.
//
// Divergências (documentadas nas funções): ColourGetValue repete a última chave onde o original
// leria uma além do fim da tabela; a coordenada w das posições não recebe o lixo da pilha que o
// original soma a cada tick; Emitter_Create com Control_CreateObjEx sem memória devolve nulo.

#include <cstddef>
#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"

struct viewer_tag;

// Chave de cor (KEYED_CLR, 0x14 bytes). Emitter_LoadDefs divide r, g e b do arquivo por 2 (escala do
// GS, 0x80 = 1.0); o alfa fica como no arquivo (255 = 2.0 no GS). time: fração da vida, 0..1.
struct KEYED_CLR {
    float r, g, b, a;  // 0x00
    float time;        // 0x10
};
static_assert(sizeof(KEYED_CLR) == 0x14);

// Definição de emissor (0x40 bytes no heap, seguidos das chaves de cor). No arquivo (bloco 0x30):
// u32 tipo (byte baixo 4, o resto vai para flags), u32 hashcode, u32 textura, u32 modelo, u16 count,
// u16 rate, float size, nove floats (0x0c..0x2c), u32 key_count e as chaves (r, g, b, a, time).
struct EMITTERDEF_tag {
    KEYED_CLR* keys;       // 0x00 logo depois da definição
    pstex_tag* texture;    // 0x04 primeira palavra do item 0x03 da textura (maptex_tag::rec), ou nulo
    celglist_tag* glist;   // 0x08 modelo de cada partícula (item 0x02), ou nulo: sprites
    float life;            // 0x0c vida de uma partícula, em segundos
    float life_rand;       // 0x10 variação (Rand_FRandHalf: ± metade)
    float gravity;         // 0x14 a velocidade y perde gravity * 9.8 m/s por segundo
    float speed;           // 0x18 m/s
    float speed_rand;      // 0x1c
    float polar;           // 0x20 ângulo da direção com o eixo y local (a direção do emissor)
    float polar_rand;      // 0x24
    float azimuth;         // 0x28 ângulo em volta do eixo y local, a partir de x
    float azimuth_rand;    // 0x2c
    float size;            // 0x30 tamanho do sprite (psiDrawParticleList)
    uint32_t key_count;    // 0x34
    uint16_t count;        // 0x38 partículas
    uint16_t rate;         // 0x3a nascimentos por tick, no máximo
    uint16_t flags;        // 0x3c 0: sprites aditivos; outro valor: transparência
    uint16_t pad;          // 0x3e (lixo da pilha no original; zero aqui)
};
static_assert(sizeof(EMITTERDEF_tag) == 0x40);
static_assert(offsetof(EMITTERDEF_tag, size) == 0x30 && offsetof(EMITTERDEF_tag, count) == 0x38);

// Dados de um emissor (obj_tag::data): 0x50 bytes de cabeçalho e as listas das partículas, nesta
// ordem na memória: vel, pos (count _VECTOR cada), age, life (count floats), colour (count * 4 bytes).
struct EMITTER_tag {
    _VECTOR* vel;          // 0x00 m/s (no mundo)
    _VECTOR* pos;          // 0x04
    uint8_t* colour;       // 0x08 r, g, b, a por partícula, escala do GS; alfa 0 = morta
    float* age;            // 0x0c segundos; 99999 = ainda não nasceu
    float* life;           // 0x10 vida sorteada no nascimento
    EMITTERDEF_tag* def;   // 0x14
    void* unknown_18;      // 0x18 último argumento de psiDrawParticleList (zero)
    uint32_t unknown_1c;
    _VECTOR rot;           // 0x20 rotação dos modelos (Emitter_Draw)
    uint32_t switch_on;    // 0x30 canal que liga o emissor (0: nenhum)
    uint32_t switch_off;   // 0x34 canal que desliga
    uint32_t switch_alive; // 0x38 canal ligado enquanto alguma partícula vive
    float duration;        // 0x3c segundos de emissão; < 0: sempre; 0: uma rajada só
    float period;          // 0x40 pisca: emite period / 2 segundos, para period (0: contínuo)
    float timer;           // 0x44 tempo na fase atual do pisca
    uint8_t period_on;     // 0x48 fase do pisca (1 emitindo)
    uint8_t dying;         // 0x49 0 vivo; conta até 3 e o objeto é removido
    uint8_t enabled;       // 0x4a pelos canais switch_on/switch_off
    uint8_t one_shot;      // 0x4b partícula que já viveu não renasce
    uint8_t done;          // 0x4c nada a desenhar (morrendo ou todas mortas)
    uint8_t pad[3];
};
static_assert(sizeof(EMITTER_tag) == 0x50);
static_assert(offsetof(EMITTER_tag, rot) == 0x20 && offsetof(EMITTER_tag, duration) == 0x3c);

constexpr uint8_t kEmitterObjectType = 0x32;
constexpr float kEmitterNotBorn = 99999.0f;

void ColourGetValue(KEYED_CLR* out, float t, KEYED_CLR* keys, int32_t count);
void Emitter_LoadDefs(uint8_t* data);
obj_tag* Emitter_Create(uint32_t hash, _VECTOR* pos, _VECTOR* dir, float duration, uint32_t switch_on,
                        uint32_t switch_off, uint32_t switch_alive, float period);
void Emitter_Update(obj_tag* obj);
void Emitter_Draw(obj_tag* obj, viewer_tag* viewer);
// No ELF é void, mas deixa em v0 o objeto de Emitter_Create (Script_CreateEntity o usa); aqui o devolve.
obj_tag* Emitter_CreatePlist(MATRIX* m, level_tag* level);
void Emitter_Kill(obj_tag* obj);

// Só no port: emissores criados desde a última carga de fase (nf_loader_check).
extern uint32_t g_emitters_created;
