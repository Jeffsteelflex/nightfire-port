// A música do lado do EE: os eventos do tick (Music_Event), a faixa de cada fase e os saltos entre os trechos dela
// (UpdateMusicalEvents e as funções de cada fase, UpdateMFX_*, 0x00111c70-0x001155cc). Ver sound.h.
//
// MapMusic (0x13 palavras por fase, do ACTION.ELF pelo kTables): +0x00 a fase, +0x04 a faixa (o hash do MFXINFO),
// +0x08 o trecho inicial, +0x0c o da morte, +0x10 o da vitória, +0x14 e +0x18 os do fim da missão vindo de um
// trecho de combate ou não, +0x1c e +0x20 os contadores do ataque, +0x24 até 5 trechos de combate e +0x38 até 5
// furtivos (-1 termina as listas). MapMusicData, o ponteiro do original para a linha da fase, é aqui o índice
// MapMusicIndex; ele começa na linha 0 (07000001), como no ELF, e fica na última fase com música (o menu principal
// não tem linha: lá a função de 07000001, ou a da última fase, continua rodando, como no PS2; os saltos que ela pede
// não valem sem faixa tocando).
//
// Os eventos (EventList, zerados a cada tick depois das funções de música) que estas funções leem: 2 o alerta da IA
// (1 e 3 os canais de alarme, 5 inimigos que viram o jogador), 3 e 8 o fim da missão, 4, 5 o fim do script de uma
// cena, 11 o script de cena que começou (o hash), 12 o fim da câmera dele, 14-16 eventos da IA; os outros (13, 17,
// 30-37, 40-47) são o número da sala por onde o jogador anda (cel_tag::misc[9], Player_Update). As funções
// pedem trechos com SFXJumpToMusicMarker (o IOP salta no próximo marcador, ou já com o 1) e guardam o último pedido
// em MusicLastJumpRequest; MusicLastJump é o trecho que o IOP diz tocar (SFXActiveData).

#include <cstring>

#include "engine/sound.h"
#include "engine/map_types.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "platform/psi_sound.h"

uint32_t EventList[64];
int32_t MapMusic[0x23 * 0x13];      // 0x0024f4d8
int32_t MapDroneData[0xc * 4];      // 0x0024f418: {fase, u16 MusicVars, u16 +2, u32 +4, f32 +8}
extern uint8_t MusicVars[36];       // 0x002707a0 (engine/drone_stubs.cpp): lido pela IA (NDrone2_SeenAndAttacking)

namespace {

uint32_t BackupEventList[64];       // 0x002501e8
uint32_t LastEventList[64];         // 0x002500e8
int32_t MapMusicIndex = 0;          // MapMusicData (0x0030c8c0, no ELF = &MapMusic[0]) como índice
int32_t NisEndedNow = 0;            // 0x0030c8c4
int32_t MusicNowPlaying = 0;        // 0x0030c8c8
int32_t MusicLastJumpRequest = -1;  // 0x0030c8cc
int32_t MusicLastJump = -1;         // 0x0030c8d0
int32_t MusicUnderAttack = 0;       // 0x0030c8d4
int32_t MusicUnderAttackCount = 0xa0;   // 0x0030c8d8
int32_t MusicUnderAttackCount2 = 0xa;   // 0x0030c8dc
int32_t MusicWaitForEnd = 0;        // 0x0030c8e0
int32_t MusicWaitForNisSfxTrigger = 0;  // 0x0030c8e4
int32_t MusicUnderAttackBonusCount = 0; // 0x0030c8e8
int32_t MusicTemp1 = 0, MusicTemp2 = 0, MusicTemp3 = 0, MusicTemp4 = 0, MusicTemp5 = 0;  // 0x0030c8ec-0x0030c90c
int32_t MusicTemp6 = 0, MusicTemp7 = 0, MusicTemp8 = 0, MusicTemp9 = 0;
int32_t WhichStealth = 0;           // 0x0030c910
int32_t WhichCombat = 0;            // 0x0030c914
int32_t IOP_DelayUpdateTimer = 0;   // 0x0030c970
// Os estáticos das funções de 07000001-03 (não voltam a 0 entre as fases, como no original).
int32_t WhichAttack_180 = 0, WhichNonAttack_181 = 0, WhichNonAttack_185 = 0, WhichAttack_189 = 0,
        WhichNonAttack_190 = 0;

constexpr int kCombat = 9;    // MapMusicData +0x24: os trechos de combate
constexpr int kStealth = 14;  // MapMusicData +0x38: os furtivos

const int32_t* music_data() { return MapMusic + MapMusicIndex * 0x13; }

// Os laços do original sobre uma lista de 5 trechos da linha: o valor está antes do primeiro -1?
bool music_in(int list, int32_t v) {
    const int32_t* p = music_data() + list;
    for (int i = 0; i < 5; ++i) {
        if (p[i] == -1) return false;
        if (p[i] == v) return true;
    }
    return false;
}

uint32_t ev(int i) { return EventList[i]; }

// Pede um trecho (MusicLastJumpRequest = trecho; SFXJumpToMusicMarker(trecho, imediato)).
void jump(int32_t section, int32_t instant) {
    MusicLastJumpRequest = section;
    SFXJumpToMusicMarker(section, instant);
}

// O bloco do fim da missão que quase todas as funções repetem: o trecho de +0x14 se a música está num trecho de
// combate, senão o de +0x18, imediato, e o fade dos sons (menos em 07000006).
void music_mission_end() {
    const int32_t* d = music_data();
    jump(music_in(kCombat, MusicLastJump) ? d[5] : d[6], 1);
    if (d[0] != 0x7000006) SFXFadeDown(0);
}

// Os eventos 8 e 3 terminam a missão (a primeira parte de quase todas as funções).
void mission_end_on_events() {
    if (ev(8) != 0 || ev(3) != 0) {
        music_mission_end();
        MusicWaitForEnd = 1;
    }
}

void set_music_mode(int16_t mode) { std::memcpy(MusicVars, &mode, 2); }
void set_music_vars(int16_t mode, int16_t a2, int32_t a4, float a8) {
    std::memcpy(MusicVars + 0, &mode, 2);
    std::memcpy(MusicVars + 2, &a2, 2);
    std::memcpy(MusicVars + 4, &a4, 4);
    std::memcpy(MusicVars + 8, &a8, 4);
}

// O estado da IA da fase (MapDroneData), ou o padrão (2, 0, 8, 25.0).
void music_vars_for(int32_t level) {
    for (int u = 0; u < 0xc; ++u) {
        const int32_t* row = MapDroneData + u * 4;
        if (row[0] != level) continue;
        int16_t mode, a2;
        float a8;
        std::memcpy(&mode, row + 1, 2);
        std::memcpy(&a2, reinterpret_cast<const uint8_t*>(row + 1) + 2, 2);
        std::memcpy(&a8, row + 3, 4);
        set_music_vars(mode, a2, row[2], a8);
        return;
    }
    set_music_vars(2, 0, 8, 25.0f);
}

// Quantos trechos a lista tem (até o primeiro -1). Divergência: com a lista vazia o original divide por 0 (o break 7
// do EE para o jogo); aqui o pedido não é feito. Nenhuma linha de MapMusic chega a isso.
int list_count(int list) {
    const int32_t* p = music_data() + list;
    int n = 0;
    for (int i = 0; i < 5 && p[i] != -1; ++i) ++n;
    return n;
}

void UpdateMFX(int32_t level);

}  // namespace

namespace {

// Os dados de um gatilho de música (tipo 0x44, 0xc bytes).
struct MUSICTRIGGER_tag {
    uint32_t event;    // 0x00 o evento de música (parâmetro 2)
    uint16_t channel;  // 0x04 o canal que o liga (parâmetro 0)
    uint16_t stop;     // 0x06 o canal que o apaga (parâmetro 1)
    int16_t timer;     // 0x08 ticks de evento que faltam
    uint8_t once;      // 0x0a apagado depois do primeiro evento (parâmetro 3)
    uint8_t pad_0b;
};
static_assert(sizeof(MUSICTRIGGER_tag) == 0xc);

// Os canais são bytes de switch_channels[256]; um número maior (nenhum no disco) leria além da tabela no original e
// aqui conta como desligado.
bool channel_on(uint32_t ch) { return ch < 256 && switch_channels[ch] != 0; }

}  // namespace

// [decomp] 0x001b30e0 MusicTrigger_Create
// Sem evento (0xff) ou sem canal, não cria nada.
obj_tag* MusicTrigger_Create(_VECTOR* pos, _VECTOR* rot, level_tag* level, celglist_tag* glist) {
    const uint32_t* p = level->params;
    if (p[2] == 0xff || p[0] == 0) return nullptr;
    obj_tag* obj = control_create_object(sizeof(MUSICTRIGGER_tag), pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    MUSICTRIGGER_tag* d = static_cast<MUSICTRIGGER_tag*>(obj->data);
    d->channel = uint16_t(p[0]);
    d->stop = uint16_t(p[1]);
    d->event = p[2];
    d->once = uint8_t(p[3]);
    obj->type = 0x44;
    obj->glist = glist;
    obj->flags = glist != nullptr ? glist->flags : 0;  // guarda do port: o original lê glist->flags sem conferir
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// [decomp] 0x001b31a0 MusicTrigger_Update
// O canal de parada ligado pede a remoção (o evento deste tick ainda sai); o canal ligado recarrega 30 ticks de
// Music_Event(evento, 2), um por tick.
void MusicTrigger_Update(obj_tag* obj) {
    MUSICTRIGGER_tag* d = static_cast<MUSICTRIGGER_tag*>(obj->data);
    if (d->stop != 0 && channel_on(d->stop)) obj->life |= 1;
    if (channel_on(d->channel)) d->timer = 0x1e;
    if (d->timer != 0) {
        --d->timer;
        Music_Event(d->event, 2);
        if (d->once != 0) obj->life |= 1;
    }
}

// [decomp] 0x001de328 Music_Event
void Music_Event(uint32_t event, uint32_t value) {
    if (event < 0x40) EventList[event] = value;
}

// [decomp] 0x001de350 Get_Music_Event
uint32_t Get_Music_Event(uint32_t event) { return EventList[event & 0x3f]; }

// [decomp] 0x00111c70 PickRandomStealth
// Saindo de um trecho de combate (e sem um furtivo pedido ou tocando), o próximo furtivo da lista, em rodízio.
void PickRandomStealth() {
    if (music_in(kStealth, MusicLastJumpRequest)) return;
    if (music_in(kStealth, MusicLastJump)) return;
    if (!music_in(kCombat, MusicLastJump)) return;
    const int n = list_count(kStealth);
    if (n == 0) return;
    jump(music_data()[kStealth + WhichStealth % n], 0);
    ++WhichStealth;
}

// [decomp] 0x00111dc8 PickRandomCombat
// Sem um trecho de combate pedido ou tocando, o próximo de combate da lista, em rodízio.
void PickRandomCombat(int32_t instant) {
    if (music_in(kCombat, MusicLastJumpRequest)) return;
    if (music_in(kCombat, MusicLastJump)) return;
    const int n = list_count(kCombat);
    if (n == 0) return;
    jump(music_data()[kCombat + WhichCombat % n], instant);
    ++WhichCombat;
}

// [decomp] 0x00111ed8 UpdateMFX_02MayhewA
// 07000001: o ataque começa pelo trecho 3 e alterna 0xb e 0xe; 500 ticks depois, fora de combate, o 5 e o
// rodízio 7/9 (a cena 0x6000087 segura a música de combate).
void UpdateMFX_02MayhewA() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicUnderAttack == 0 && ev(12) != 0x6000087) {
        if (MusicTemp3 < 0) {
            if (music_in(kCombat, MusicLastJump) && MusicTemp1 == 0) {
                jump(5, 0);
                MusicTemp1 = 1;
            }
            if (music_in(kCombat, MusicLastJump) && !music_in(kStealth, MusicLastJumpRequest)) {
                if (WhichNonAttack_181 == 0) jump(7, 0);
                if (WhichNonAttack_181 == 1) jump(9, 0);
                if (++WhichNonAttack_181 > 1) WhichNonAttack_181 = 0;
            }
        }
    } else {
        if (MusicTemp2 == 0) {
            jump(3, 0);
            MusicTemp2 = 1;
            MusicTemp3 = 500;
        }
        if (!music_in(kCombat, MusicLastJump) && !music_in(kCombat, MusicLastJumpRequest)) {
            if (WhichAttack_180 == 0) jump(0xb, 0);
            if (WhichAttack_180 == 1) jump(0xe, 0);
            if (++WhichAttack_180 > 1) WhichAttack_180 = 0;
        }
    }
    --MusicTemp3;
}

// [decomp] 0x00112270 UpdateMFX_02MayhewB
// 07000002: o trecho 2 espera a cena 0x6000084 (ou o evento 36); a cena 0x600007f toca o 6 e, no fim, o 8; os
// eventos 31/32 põem a IA em alerta (MusicVars) com o trecho 4; furtivo em rodízio 0x14/0x16; o evento 30 fecha
// com 0x12 (ou 10). Escreve MusicVars a cada tick (o evento 37 muda o fator +8 para 1.0).
void UpdateMFX_02MayhewB() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicLastJump == 0) {
        if (ev(12) != 0x6000084 && ev(36) == 0) return;
        jump(2, 0);
        return;
    }
    if (ev(37) == 0) {
        music_vars_for(music_data()[0]);
    } else {
        const float one = 1.0f;
        std::memcpy(MusicVars + 8, &one, 4);
    }
    if (ev(11) == 0x600007f) {
        MusicTemp7 = 0;
        jump(6, 1);
        MusicTemp4 = 1;
    }
    if (MusicTemp4 != 0) {
        if (ev(12) != 0x600007f) return;
        jump(8, 1);
        MusicTemp4 = 0;
        return;
    }
    if (MusicTemp1 != 0) {
        if (ev(15) == 0 && ev(16) == 0) return;
        PickRandomCombat(0);
        MusicTemp1 = 0;
        MusicTemp9 = 300;
    }
    const int32_t wait = MusicTemp9--;
    if (wait > 0) return;
    if (ev(31) != 0 && MusicTemp2 == 0) {
        jump(4, 1);
        set_music_mode(1);
        MusicUnderAttackBonusCount = 600;
        MusicTemp1 = 1;
        MusicTemp2 = 1;
        return;
    }
    if (ev(32) != 0 && MusicTemp3 == 0) {
        jump(4, 0);
        set_music_mode(1);
        MusicUnderAttackBonusCount = 600;
        MusicTemp1 = 1;
        MusicTemp3 = 1;
        return;
    }
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (MusicTemp5 == 0) {
        if (!music_in(kStealth, MusicLastJumpRequest)) {
            const int32_t instant = MusicLastJump == 4 ? 1 : 0;
            if (WhichNonAttack_185 == 0) jump(0x14, instant);
            if (WhichNonAttack_185 == 1) jump(0x16, instant);
            if (++WhichNonAttack_185 > 1) WhichNonAttack_185 = 0;
        }
    } else {
        jump(0x12, 0);
    }
    if (ev(30) == 0) return;
    MusicTemp5 = 1;
    if (music_in(kCombat, MusicLastJump)) {
        jump(0x12, 0);
    } else if (MusicLastJump == 0x12) {
        MusicTemp5 = 1;
    } else {
        jump(10, 0);
    }
}

// [decomp] 0x00112730 UpdateMFX_02MayhewC
// 07000003: os eventos 33/34 põem a IA em alerta (trechos 10 e 0xc); depois de 120 ticks, fora de combate o
// rodízio 6/8 saindo de combate ou o trecho 0; em combate 2/4, ou 0xe com o evento 35.
void UpdateMFX_02MayhewC() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicTemp1 != 0) {
        if (ev(15) == 0 && ev(16) == 0) return;
        PickRandomCombat(0);
        MusicTemp1 = 0;
        MusicTemp9 = 0x78;
    }
    const int32_t wait = MusicTemp9--;
    if (wait >= 1) return;
    if (ev(33) != 0 && MusicTemp2 == 0) {
        jump(10, 0);
        set_music_mode(1);
        MusicUnderAttackBonusCount = 600;
        MusicTemp2 = 1;
        MusicTemp1 = 1;
        return;
    }
    if (ev(34) != 0 && MusicTemp3 == 0) {
        jump(0xc, 0);
        set_music_mode(1);
        MusicUnderAttackBonusCount = 600;
        MusicTemp3 = 1;
        MusicTemp1 = 1;
        return;
    }
    if (MusicUnderAttack == 0) {
        if (music_in(kStealth, MusicLastJumpRequest)) return;
        if (music_in(kCombat, MusicLastJump)) {
            if (WhichNonAttack_190 == 0) jump(6, 0);
            if (WhichNonAttack_190 == 1) jump(8, 0);
            if (++WhichNonAttack_190 > 1) WhichNonAttack_190 = 0;
        } else if (MusicLastJump == 0xa || MusicLastJump == 0xc) {
            if (MusicUnderAttackBonusCount < 0) jump(0, 1);
        } else {
            jump(0, 0);
        }
    } else if (ev(35) == 0) {
        if (!music_in(kCombat, MusicLastJump) && !music_in(kCombat, MusicLastJumpRequest) && MusicLastJump != 0xe &&
            MusicLastJumpRequest != 0xe) {
            if (WhichAttack_189 == 0) jump(2, 0);
            if (WhichAttack_189 == 1) jump(4, 0);
            if (++WhichAttack_189 > 1) WhichAttack_189 = 0;
        }
    } else if (!music_in(kCombat, MusicLastJump) && !music_in(kCombat, MusicLastJumpRequest)) {
        jump(0xe, 0);
    }
}

// [decomp] 0x00112bf8 UpdateMFX_02MayhewD
// 07000004: a cena 0x6000082 toca o 2; o gatilho de música das cenas toca o 3 e depois o 4.
void UpdateMFX_02MayhewD() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(11) == 0x6000082) {
        jump(2, 1);
        MusicTemp2 = 1;
    }
    if (SfxNisMusicTrigger == 0) {
        if (ev(5) == 0 || MusicTemp2 == 0) return;
    } else if (MusicTemp1 == 0) {
        jump(3, 1);
        SfxNisMusicTrigger = 0;
        MusicTemp1 = 1;
        return;
    }
    jump(4, 1);
    SfxNisMusicTrigger = 0;
}

// [decomp] 0x00112d78 UpdateMFX_01CastleA
// 07000005: o alerta (evento 2 = 3) começa o combate e o segura 1200 ticks; a cena 0x6000994 escolhe 6 ou 2; o
// evento 13 também; fora de combate o rodízio furtivo (ou 0x14 depois do 2); o evento 4 fecha com o 9.
void UpdateMFX_01CastleA() {
    if (ev(2) == 3 && MusicTemp4 == 0) {
        MusicTemp4 = 1;
        MusicTemp3 = 0x4b0;
        PickRandomCombat(0);
        return;
    }
    if (MusicTemp3 > 0) {
        --MusicTemp3;
        return;
    }
    if (ev(12) == 0x6000994) {
        MusicTemp1 = 0;
        MusicTemp2 = 0x42;
    }
    if (MusicTemp2 > 0) {
        --MusicTemp2;
        return;
    }
    if (ev(11) == 0x6000994 || MusicTemp1 != 0) {
        jump(music_in(kCombat, MusicLastJump) ? 6 : 2, 0);
        MusicTemp1 = 1;
        return;
    }
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
    } else if (ev(13) != 0) {
        jump(music_in(kCombat, MusicLastJump) ? 6 : 2, 0);
    } else if (MusicLastJumpRequest != 2) {
        PickRandomStealth();
    } else {
        jump(0x14, 0);
    }
    mission_end_on_events();
    if (MusicWaitForEnd == 0 && ev(4) != 0) {
        jump(9, 0);
        MusicWaitForEnd = 1;
    }
}

// [decomp] 0x00113060 UpdateMFX_01CastleB
// 07000006: a cena 0x6000071 faz o salto de fim (sem fade nesta fase) e passa a esperar o gatilho de música das
// cenas, que toca o 0xc; até lá, furtivo ou combate.
void UpdateMFX_01CastleB() {
    if (MusicWaitForNisSfxTrigger == 0) {
        if (ev(11) == 0x6000071) {
            music_mission_end();
            MusicWaitForNisSfxTrigger = 1;
        }
        if (MusicUnderAttack == 0) {
            PickRandomStealth();
        } else {
            PickRandomCombat(0);
        }
    } else if (SfxNisMusicTrigger != 0) {
        jump(0xc, 0);
        SfxNisMusicTrigger = 0;
        MusicWaitForEnd = 1;
    }
}

// [decomp] 0x00113188 UpdateMFX_01CastleC
// 07000007: as cenas 0x6000072 (trechos 2 e 5) e 0x6000073 (7 e 10), o combate depois delas (0xe) e o evento 13
// (0xc).
void UpdateMFX_01CastleC() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0 || MusicTemp2 != 0) return;
    if (MusicTemp1 == 0) {
        if (ev(11) == 0x6000072) jump(2, 0);
        if (ev(12) == 0x6000072 || SfxNisMusicTrigger != 0) {
            jump(5, 0);
            SfxNisMusicTrigger = 0;
            MusicTemp1 = 1;
        }
    }
    if (ev(11) == 0x6000073) {
        jump(7, 0);
        MusicTemp4 = 1;
    }
    if (MusicTemp4 != 0 && (ev(12) == 0x6000073 || SfxNisMusicTrigger != 0)) {
        jump(10, 0);
        MusicTemp1 = 10;
        SfxNisMusicTrigger = 0;
    }
    if (MusicTemp1 == 10 && MusicUnderAttack != 0) {
        jump(0xe, 0);
        MusicTemp2 = 1;
    }
    if (ev(13) != 0) jump(0xc, 0);
}

// [decomp] 0x001133b8 UpdateMFX_01CastleD
// 07000008: a cena 0x6000077 (2 e 4), o evento 13 (6) e o gatilho das cenas (8).
void UpdateMFX_01CastleD() {
    if (MusicLastJump == 0 && ev(11) == 0x6000077) jump(2, 1);
    if (ev(12) == 0x6000077) jump(4, 1);
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(13) != 0 && MusicTemp1 == 0) {
        jump(6, 0);
        MusicTemp1 = 1;
    }
    if (SfxNisMusicTrigger != 0) {
        jump(8, 1);
        SfxNisMusicTrigger = 0;
    }
}

// [decomp] 0x00113558 UpdateMFX_03TowerA
// 07000009: combate pelo rodízio; sem o evento 43, o 0x10 saindo de combate, o 4 depois do 2, senão o 0; com ele, o
// 2 ou (vindo de combate) o 0xc.
void UpdateMFX_03TowerA() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (ev(43) == 0) {
        if (MusicLastJumpRequest == 4 || MusicLastJumpRequest == 0 || MusicLastJumpRequest == 0x10) return;
        if (music_in(kCombat, MusicLastJump)) {
            jump(0x10, 0);
        } else if (MusicLastJump == 2) {
            jump(4, 0);
        } else {
            jump(0, 0);
        }
    } else {
        if (music_in(kStealth, MusicLastJumpRequest)) return;
        jump(music_in(kCombat, MusicLastJump) ? 0xc : 2, 0);
    }
}

// [decomp] 0x001137b0 UpdateMFX_03TowerB
// 0700000a: como a de 07000009, com os trechos 0x1c, 4 (depois do 0x13), 0x1a e, a partir do evento 40, 0x13/0xe.
void UpdateMFX_03TowerB() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (ev(40) == 0 && MusicTemp1 == 0) {
        if (MusicLastJumpRequest == 4 || MusicLastJumpRequest == 0x1a || MusicLastJumpRequest == 0x1c) return;
        if (music_in(kCombat, MusicLastJump)) {
            jump(0x1c, 0);
        } else if (MusicLastJump == 0x13) {
            jump(4, 0);
        } else {
            jump(0x1a, 0);
        }
    } else {
        MusicTemp1 = 1;
        if (music_in(kStealth, MusicLastJumpRequest)) return;
        jump(music_in(kCombat, MusicLastJump) ? 0xe : 0x13, 0);
    }
}

// [decomp] 0x00113a20 UpdateMFX_03TowerC
// 0700000b: uma sequência de passos (MusicTemp1) pelas cenas 0x60004f9/0x60004fa e os eventos 44, 41 e 17; no
// passo 4 o combate (8) e a volta (0x15); no fim, o 0xe com os eventos de fim de missão.
void UpdateMFX_03TowerC() {
    switch (MusicTemp1) {
        case 0:
            if (ev(11) == 0x60004f9) {
                jump(2, 0);
                MusicTemp1 = 1;
            }
            break;
        case 1:
            if (ev(44) != 0) {
                jump(4, 0);
                MusicTemp1 = 2;
            }
            break;
        case 2:
            if (ev(41) != 0) {
                jump(6, 0);
                MusicTemp1 = 3;
            }
            break;
        case 3:
            if (ev(17) != 0) {
                jump(8, 1);
                MusicTemp1 = 4;
            }
            break;
        case 4:
            if (MusicUnderAttack == 0) {
                if (MusicTemp2 != 0) jump(0x15, 0);
            } else {
                MusicTemp2 = 1;
                jump(8, 0);
            }
            if (ev(11) == 0x60004fa) {
                jump(MusicUnderAttack == 0 ? 0x17 : 10, 0);
                MusicTemp1 = 5;
            }
            break;
        case 5:
            if (ev(12) == 0x60004fa) {
                jump(0xc, 1);
                MusicTemp1 = 6;
            }
            break;
        case 6:
            if (ev(3) != 0 || ev(8) != 0) {
                jump(0xe, 1);
                MusicTemp1 = 99;
            }
            break;
        default:
            break;
    }
}

// [decomp] 0x00113c18 UpdateMFX_PowerStationA1
// 0700000c: o evento 45 toca o 0x36 (vindo de combate) ou o 0x24 e encerra; antes, combate pelo rodízio, o 0xf, o
// 0x14 ao sair de combate e, com o evento 13, o 0x38 ou o 0x22.
void UpdateMFX_PowerStationA1() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(45) != 0 && MusicTemp2 == 0) {
        if (music_in(kCombat, MusicLastJumpRequest) && music_in(kCombat, MusicLastJump)) {
            jump(0x36, 0);
        } else {
            jump(0x24, 1);
        }
        MusicTemp1 = 1;
        MusicTemp2 = 1;
        return;
    }
    if (MusicTemp1 != 0) return;
    if (MusicUnderAttack != 0) {
        PickRandomCombat(0);
        return;
    }
    if (ev(13) == 0) {
        if (music_in(kCombat, MusicLastJumpRequest) && music_in(kCombat, MusicLastJump)) {
            jump(0xf, 0);
            return;
        }
        if (music_in(kStealth, MusicLastJumpRequest)) return;
        if (!music_in(kStealth, MusicLastJump)) jump(0x14, 1);
        return;
    }
    jump(music_in(kCombat, MusicLastJump) || MusicLastJump == 0x38 ? 0x38 : 0x22, 0);
}

// [decomp] 0x00113fc0 UpdateMFX_PowerStationA2
// 0700000d: a cena 0x600068f (ou o gatilho das cenas) toca o 0x2b e espera 600 ticks; depois combate, o 0xc ao sair
// dele, o 0x38/0x22 com o evento 13, e o 0x3a com o evento 45.
void UpdateMFX_PowerStationA2() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(12) == 0x600068f || SfxNisMusicTrigger != 0) {
        jump(0x2b, 1);
        MusicTemp1 = 1;
        MusicTemp2 = 600;
        return;
    }
    if (MusicTemp1 == 0) return;
    const int32_t wait = MusicTemp2--;
    if (wait >= 1) return;
    if (ev(45) == 0 && MusicTemp4 == 0) {
        if (MusicUnderAttack != 0) {
            PickRandomCombat(0);
        } else if (ev(13) == 0) {
            if (music_in(kCombat, MusicLastJump)) jump(0xc, 0);
        } else {
            jump(music_in(kCombat, MusicLastJump) || MusicLastJump == 0x38 ? 0x38 : 0x22, 0);
        }
    } else {
        jump(0x3a, MusicLastJumpRequest == 0x22 ? 1 : 0);
        MusicTemp4 = 1;
    }
}

// [decomp] 0x00114250 UpdateMFX_HT_Level_Tower2A
// 07000011: o evento 47 toca o 0xc; o fim da missão.
void UpdateMFX_HT_Level_Tower2A() {
    if (ev(47) != 0) jump(0xc, 1);
    mission_end_on_events();
}

// [decomp] 0x00114348 UpdateMFX_HT_Level_Tower2B
// 07000012: o primeiro ataque toca o 0xf, o fim dele o 0x11; depois combate pelo rodízio e o 0x16 ao sair.
void UpdateMFX_HT_Level_Tower2B() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicTemp1 == 1) {
        if (MusicUnderAttack == 0) {
            jump(0x11, 0);
            MusicTemp1 = 2;
        }
    } else if (MusicTemp1 < 2) {
        if (MusicTemp1 == 0 && MusicUnderAttack != 0) {
            jump(0xf, 1);
            MusicTemp1 = 1;
        }
    } else if (MusicTemp1 == 2) {
        if (MusicUnderAttack == 0) {
            if (music_in(kCombat, MusicLastJump)) jump(0x16, 0);
        } else {
            PickRandomCombat(0);
        }
    }
}

// [decomp] 0x00114528 UpdateMFX_HT_Level_Tower2Elevator
// 0700004a: a cena 0x60006d3 toca o 4; o fim da missão.
void UpdateMFX_HT_Level_Tower2Elevator() {
    mission_end_on_events();
    if (MusicWaitForEnd == 0 && ev(11) == 0x60006d3) jump(4, 0);
}

// [decomp] 0x00114640 UpdateMFX_HT_Level_Tower2C
// 07000013: em ataque, o trecho 0; o fim da missão.
void UpdateMFX_HT_Level_Tower2C() {
    if (MusicUnderAttack != 0) jump(0, 0);
    mission_end_on_events();
}

// [decomp] 0x00114728 UpdateMFX_HT_Level_EvilBase
// 07000014: as cenas 0x6000848 (0x18 no fim, 0x22 no começo, presos), 0x600062f (5 ou 0x12, uma vez só: depois
// disso a função para aí) e 0x6000833 (0xf/0x14 e o combate depois de 56 ticks; no começo 0x34 ou 0x16); fora delas,
// 0x32 ou, em ataque, 0x2e.
void UpdateMFX_HT_Level_EvilBase() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(12) == 0x6000848 || MusicTemp3 != 0) {
        MusicTemp3 = 1;
        jump(0x18, 1);
        return;
    }
    if (ev(11) == 0x6000848 || MusicTemp6 != 0) {
        MusicTemp6 = 1;
        jump(0x22, 1);
    }
    if (ev(11) == 0x600062f || MusicTemp2 != 0) {
        if (MusicTemp2 != 0) return;
        MusicTemp2 = 1;
        MusicTemp1 = 0;
        if (MusicUnderAttack == 0) {
            jump(5, 1);
        } else {
            jump(0x12, 0);
        }
    }
    if (ev(12) == 0x6000833 || MusicTemp1 != 0) {
        ++MusicTemp1;
        if (MusicUnderAttack == 0) {
            if (music_in(kCombat, MusicLastJump)) {
                jump(0xf, 0);
                return;
            }
            if (MusicLastJumpRequest == 0xf) return;
            jump(0x14, 1);
        } else if (MusicTemp1 > 0x37) {
            PickRandomCombat(1);
        } else {
            jump(0x14, 1);
        }
    } else if (ev(11) == 0x6000833 || MusicTemp4 != 0) {
        if (MusicTemp4 != 0) return;
        MusicTemp4 = 1;
        if (MusicLastJump == 0x2e) {
            jump(0x34, 0);
        } else {
            jump(0x16, 1);
        }
    } else if (MusicUnderAttack == 0) {
        if (MusicLastJump == 5) return;
        jump(0x32, 0);
    } else {
        jump(0x2e, 1);
    }
}

// [decomp] 0x00114a38 UpdateMFX_HT_Level_EvilSilo
// 07000015: furtivo ou combate pelos rodízios; o fim da missão.
void UpdateMFX_HT_Level_EvilSilo() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (MusicUnderAttack == 0) {
        PickRandomStealth();
    } else {
        PickRandomCombat(0);
    }
}

// [decomp] 0x00114b38 UpdateMFX_HT_Level_EvilBaseC
// 07000016: os rodízios até a cena 0x6000631 (0x24, e 0x27 no fim dela), a 0x6000632 (0x29) e o 0x2b no fim (que o
// bloco do começo já pega com os mesmos eventos).
void UpdateMFX_HT_Level_EvilBaseC() {
    mission_end_on_events();
    if (MusicWaitForEnd != 0) return;
    if (ev(11) == 0x6000631) {
        jump(0x24, 1);
        MusicTemp1 = 1;
    }
    if (MusicTemp1 == 0) {
        if (MusicUnderAttack == 0) {
            PickRandomStealth();
        } else {
            PickRandomCombat(0);
        }
    } else if (MusicTemp1 == 2) {
        if (ev(11) == 0x6000632) {
            jump(0x29, 0);
            MusicTemp1 = 3;
        }
    } else if (MusicTemp1 < 3) {
        if (MusicTemp1 == 1 && ev(12) == 0x6000631) {
            jump(0x27, 1);
            MusicTemp1 = 2;
        }
    } else if (MusicTemp1 == 3 && (ev(3) != 0 || ev(8) != 0)) {
        jump(0x2b, 0);
        MusicWaitForEnd = 1;
    }
}

// [decomp] 0x00114d60 UpdateMFX_HT_Level_SpaceStationD
// 0700001b: no fim da cena 0x600085c, o trecho 0; o fim da missão.
void UpdateMFX_HT_Level_SpaceStationD() {
    mission_end_on_events();
    if (MusicWaitForEnd == 0 && ev(12) == 0x600085c) jump(0, 0);
}

// [decomp] 0x00114e70 UpdateMusicalEvents
// Uma vez por SFXUpdate: no começo da fase (eventos 0 = a fase, 1) acha a faixa em MapMusic, para a música, pede a
// nova (SFXStartMusic) e põe o estado da IA da fase em MusicVars; depois as cenas (11 começou, 12 acabou a câmera, 5
// acabou o script: NisPlaying e NisQuitten, que o IOP usa para pausar e apagar sons), o alarme e o ataque (evento 2:
// MusicVars e os contadores), os saltos de fim de missão (6 morte, 7 vitória) e a função de música da fase
// (UpdateMFX_*); por fim os eventos do tick são guardados e zerados. Pausado (SFXPaused) e sem começo de fase, nada
// (os eventos ficam para depois da pausa).
void UpdateMusicalEvents() {
    bool begun = false;
    if (EventList[1] != 0 && EventList[0] != 0) {
        if (EventList[0] != 0x7000048) {
            for (int i = 0; i < 0x23; ++i) {
                const int32_t* row = MapMusic + i * 0x13;
                if (uint32_t(row[0]) != EventList[0]) continue;
                if (row[1] != 0) {
                    NisPlaying = 0;
                    NisQuitten = 0;
                    NisEndedNow = 0;
                    MapMusicIndex = i;
                    SFXStopMusic();
                    MusicLastJumpRequest = -1;
                    MusicNowPlaying = row[1];
                    MusicLastJump = row[2];
                    SFXStartMusic(uint32_t(MusicNowPlaying), MusicLastJump);
                    MusicWaitForEnd = 0;
                    MusicUnderAttackCount = row[8];
                    MusicUnderAttackCount2 = row[7];
                    SfxNisMusicTrigger = 0;
                    MusicWaitForNisSfxTrigger = 0;
                    MusicTemp1 = MusicTemp2 = MusicTemp3 = MusicTemp4 = MusicTemp5 = 0;
                    MusicTemp6 = MusicTemp7 = MusicTemp8 = MusicTemp9 = 0;
                    MusicUnderAttack = 0;
                    WhichStealth = 0;
                    WhichCombat = 0;
                    MusicUnderAttackBonusCount = 0;
                    music_vars_for(row[0]);
                    IOP_DelayUpdateTimer = 10;
                }
                begun = true;
                break;
            }
        } else {
            begun = true;
        }
    }
    if (!begun && SFXPaused != 0) return;
    const int32_t* row = music_data();
    const int32_t delay = IOP_DelayUpdateTimer - 1;
    if (IOP_DelayUpdateTimer < 0) MusicLastJump = SFXActiveData[0x53];  // MusicSectionNowPlaying[0] do IOP
    if (EventList[11] != 0) NisPlaying = 1;
    if ((EventList[12] == 0 && EventList[5] == 0) || (NisPlaying = 0, EventList[5] == 0)) {
        if (NisQuitten != 0) NisQuitten = NisQuitten - 1;
    } else {
        NisQuitten = 5;
    }
    if (EventList[10] == 0) {
        if (MusicUnderAttackBonusCount < 0) music_vars_for(row[0]);
    } else {
        set_music_mode(1);
    }
    if (NisPlaying == 0) {
        if (EventList[2] == 2) {
            MusicUnderAttackBonusCount = 600;
            MusicUnderAttack = 1;
            music_vars_for(-99);
        }
        if (EventList[2] == 0 || EventList[2] == 2) {
            const bool was_negative = MusicUnderAttackBonusCount < 0;
            --MusicUnderAttackBonusCount;
            if (was_negative) {
                const bool count_negative = MusicUnderAttackCount < 0;
                --MusicUnderAttackCount;
                if (count_negative) {
                    MusicUnderAttack = 0;
                    MusicUnderAttackCount2 = row[7];
                }
            }
        } else {
            const bool negative = MusicUnderAttackCount2 < 0;
            --MusicUnderAttackCount2;
            if (negative) {
                music_vars_for(row[0]);
                MusicUnderAttack = 1;
                MusicUnderAttackBonusCount = 0;
                MusicUnderAttackCount = row[8];
            }
        }
    }
    IOP_DelayUpdateTimer = delay;
    if (EventList[6] != 0) {
        MusicLastJumpRequest = row[3];
        SFXJumpToMusicMarker(MusicLastJumpRequest, 1);
        SFXFadeDown(0);
        MusicWaitForEnd = 1;
    }
    if (EventList[7] != 0) {
        MusicLastJumpRequest = row[4];
        SFXJumpToMusicMarker(MusicLastJumpRequest, 1);
        SFXFadeDown(0);
        MusicWaitForEnd = 1;
    }
    std::memcpy(BackupEventList, EventList, sizeof(EventList));
    if (MusicWaitForEnd == 0) UpdateMFX(row[0]);
    std::memset(EventList, 0, sizeof(EventList));
    std::memcpy(LastEventList, BackupEventList, sizeof(LastEventList));
}

void music_debug_power_on() {
    std::memset(EventList, 0, sizeof(EventList));
    std::memset(BackupEventList, 0, sizeof(BackupEventList));
    std::memset(LastEventList, 0, sizeof(LastEventList));
    MapMusicIndex = 0;
    NisEndedNow = MusicNowPlaying = MusicWaitForEnd = 0;
    MusicLastJumpRequest = MusicLastJump = -1;
    MusicUnderAttack = 0;
    MusicUnderAttackCount = 0xa0;
    MusicUnderAttackCount2 = 0xa;
    MusicWaitForNisSfxTrigger = MusicUnderAttackBonusCount = 0;
    MusicTemp1 = MusicTemp2 = MusicTemp3 = MusicTemp4 = MusicTemp5 = 0;
    MusicTemp6 = MusicTemp7 = MusicTemp8 = MusicTemp9 = 0;
    WhichStealth = WhichCombat = IOP_DelayUpdateTimer = 0;
    WhichAttack_180 = WhichNonAttack_181 = WhichNonAttack_185 = WhichAttack_189 = WhichNonAttack_190 = 0;
}

MusicState music_state() {
    MusicState s;
    s.level = music_data()[0];
    s.track = MusicNowPlaying;
    s.last_jump = MusicLastJump;
    s.request = MusicLastJumpRequest;
    s.under_attack = MusicUnderAttack;
    s.wait_for_end = MusicWaitForEnd;
    return s;
}

namespace {

// O switch de UpdateMusicalEvents pela fase de MapMusicData.
void UpdateMFX(int32_t level) {
    switch (level) {
        case 0x7000001: UpdateMFX_02MayhewA(); break;
        case 0x7000002: UpdateMFX_02MayhewB(); break;
        case 0x7000003: UpdateMFX_02MayhewC(); break;
        case 0x7000004: UpdateMFX_02MayhewD(); break;
        case 0x7000005: UpdateMFX_01CastleA(); break;
        case 0x7000006: UpdateMFX_01CastleB(); break;
        case 0x7000007: UpdateMFX_01CastleC(); break;
        case 0x7000008: UpdateMFX_01CastleD(); break;
        case 0x7000009: UpdateMFX_03TowerA(); break;
        case 0x700000a: UpdateMFX_03TowerB(); break;
        case 0x700000b: UpdateMFX_03TowerC(); break;
        case 0x700000c: UpdateMFX_PowerStationA1(); break;
        case 0x700000d: UpdateMFX_PowerStationA2(); break;
        case 0x7000011: UpdateMFX_HT_Level_Tower2A(); break;
        case 0x7000012: UpdateMFX_HT_Level_Tower2B(); break;
        case 0x7000013: UpdateMFX_HT_Level_Tower2C(); break;
        case 0x7000014: UpdateMFX_HT_Level_EvilBase(); break;
        case 0x7000015: UpdateMFX_HT_Level_EvilSilo(); break;
        case 0x7000016: UpdateMFX_HT_Level_EvilBaseC(); break;
        case 0x700001b: UpdateMFX_HT_Level_SpaceStationD(); break;
        case 0x700004a: UpdateMFX_HT_Level_Tower2Elevator(); break;
        default: break;
    }
}

}  // namespace
