#pragma once
// Cadeados e alvos do Q-Worm (módulo SSys do ACTION.ELF, 0x001ae1f8-0x001b0840). Ver docs/engine.md, "Cadeados e
// Q-Worm".
//
// - Cadeado (posicionamento 0x2e, objeto 0x3a; Lock_Create): o tipo (parâmetro 0) diz como abre: 0 teclado numérico
//   (usar abre a página 0x40000021 do menu de jogo 0x80000003 e o código certo abre), 1 laser (os tiros das armas 0x4e
//   e 0x4f esquentam o cadeado, que fica vermelho e abre), 2 só o decodificador. Todos abrem com a chave do parâmetro 1
//   (BLData +0x70c + chave, Player_EquipKey) e, os tipos 0 e 2, com o decodificador (arma 0x56/0x57, o PDA): os tiros
//   dele acham os quatro algarismos do código um a um (GlobalVars, que o PDA mostra) e o cadeado abre. Ao abrir:
//   a chave fica com o jogador, o canal do parâmetro 5 liga, o modelo troca pelo LOD seguinte e, com um caminho, o
//   cadeado anda por ele. O tipo 0 volta a trancar se o canal desligar. Com o parâmetro 6, na dificuldade 1
//   (Operative) o cadeado já começa aberto.
// - KeyCodes: 51 códigos de 4 algarismos ('0' a '8') sorteados a cada fase (Locks_Init, no estado GF_LOCKS_INIT) e um
//   byte "o jogador já sabe" (o Q-Worm revela o código com uma mensagem; o teclado então mostra o código).
// - Q-Worm (posicionamento 0x35, objeto 0x49; PCQWorm_Create): um computador que, atingido pelo Q-Worm (arma 0x58; com
//   o parâmetro 3) ou usado (com o parâmetro 2), troca de modelo, revela o código do parâmetro 0 (se houver) e liga o
//   canal do parâmetro 1.

#include <cstdint>

#include "engine/map_types.h"
#include "engine/object.h"
#include "platform/ptr32.h"

struct BLData;

// Dados do cadeado (0x20 bytes em obj->data, tipo 0x3a).
struct LOCK_tag {
    uint16_t kind;          // 0x00 parâmetro 0: 0 teclado, 1 laser, 2 decodificador
    uint16_t key;           // 0x02 parâmetro 1: a chave (BLData +0x70c) e o código (KeyCodes)
    uint16_t sound_open;    // 0x04 parâmetro 3 (0: 0xffff, nenhum)
    uint16_t sound_locked;  // 0x06 parâmetro 4 (0: 0xffff): usar sem a chave
    uint16_t channel;       // 0x08 parâmetro 5: ligado ao abrir
    uint16_t menu;          // 0x0a o gerente do teclado (MenuManager_Create; 0xffff nenhum)
    uint32_t param2;        // 0x0c parâmetro 2
    P32<framelist_tag> path;  // 0x10 o caminho por onde o cadeado anda ao abrir
    float t;                // 0x14 posição no caminho, em quadros
    float speed;            // 0x18 quadros por tick a 60 Hz
    uint32_t frames;        // 0x1c quadros do caminho - 1 (0: parado)
};
static_assert(sizeof(LOCK_tag) == 0x20);

// Dados do Q-Worm (6 bytes, tipo 0x49).
struct PCQWORM_tag {
    int16_t code;      // 0x0 parâmetro 0: o código (KeyCodes) que ele revela (0 nenhum)
    int16_t channel;   // 0x2 parâmetro 1
    uint8_t use;       // 0x4 parâmetro 2: usar também ativa
    uint8_t worm;      // 0x5 parâmetro 3: o Q-Worm ativa
};
static_assert(sizeof(PCQWORM_tag) == 6);

constexpr int kKeyCodes = 51;
extern char KeyCodes[kKeyCodes * 5];  // 4 algarismos e "revelado" por código
// GlobalVars (24 bytes, engine/modules.h): +0 o objeto do jogador que usou o teclado, +4 o BLData dele, +8 o código
// certo (4 algarismos e "revelado"), +0xd os algarismos digitados ou achados, +0x12 quantos algarismos tem o código,
// +0x13 quantos já foram, +0x14 o PDA mostra os algarismos.

obj_tag* Lock_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void Lock_Update(obj_tag* obj);
void Lock_Activate(obj_tag* lock, obj_tag* by);
void Lock_Unlock(obj_tag* by, obj_tag* lock, uint8_t silent);
void Lock_ReLock(obj_tag* by, obj_tag* lock);
void Locks_Init();
obj_tag* PCQWorm_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist);
void PCQWorm_Update(obj_tag* obj);
void PCQWorm_Activate(obj_tag* obj, obj_tag* by);
void Player_UnEquipKey(BLData* b, uint16_t key);

// Só no port: o que os cadeados fizeram desde a partida (nf_loader_check, testes).
struct SsysStats {
    uint32_t unlocked = 0;   // Lock_Unlock que abriu (não os da carga fácil)
    uint32_t keypads = 0;    // teclados abertos (Lock_Update, estado 3)
    uint32_t decrypted = 0;  // algarismos achados pelo decodificador
    uint32_t wormed = 0;     // Q-Worms ativados
};
extern SsysStats g_ssys_stats;

void ssys_debug_power_on();
