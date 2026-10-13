// Auxiliares de comportamento dos personagens da IA usados pelos estados: animações de parado e de
// andar, conversa de patrulha, alertas a todos, morte (falha de missão, canais, animação, sumir,
// granadas do capitão), reação a explosivos, ataque forçado, a contagem para a música de combate e
// as falas (grito de morte, alto-falantes, os guardas de 07000008 e os convidados de 07000007).
// Ver drone.h.
//
// Divergências intencionais:
// - DroneAnim_LocationDeathAnim lê a animação do jogador sem conferir se ele existe; aqui um jogador
//   nulo conta como "não mirando".

#include <cmath>
#include <cstring>

#include "engine/ainetwork.h"
#include "engine/anim.h"
#include "engine/bullet.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/modules.h"
#include "engine/npc.h"
#include "engine/pickup.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"

namespace {

bool bots_on() {
    uint32_t v;
    std::memcpy(&v, MPSettings + 0x184, 4);
    return v != 0;
}

template <class T>
T& music(uint32_t offset) {
    static_assert(!nf::is_native_ptr_v<T>, "ponteiro de 4 bytes: use P32<...>");
    return *reinterpret_cast<T*>(MusicVars + offset);
}

// Personagens que já contaram para a música de combate desde a última volta dela (0x0029abb0).
Drone_tag* pNPCPassed[100];
int32_t pDCVCount = 0;

}  // namespace

// [decomp] 0x0013ca68 DroneAnim_SetStandIdleAnim
// Animação de parado (estado de animação 0x24, ou 0x24-0x27 sorteado no estado 4 e na festa com a
// propriedade 0x47; 0x26 para a classe 0x10), no máximo uma troca até idle_anim_time quando "wait".
uint32_t DroneAnim_SetStandIdleAnim(DCVars_tag* dcv, int8_t wait) {
    Drone_tag* d = dcv->drone;
    uint32_t delay = 0;
    uint32_t call = 0x24;
    const bool prop47 = behaviour_util_getProperty(0x47, d->behaviour) != 0;
    if (wait != 0 && GameState.frame < d->idle_anim_time) return 0;
    const uint32_t st = d->sm.state;
    uint32_t variants = 0;
    if (st == 4) {
        variants = 3;
    } else if (st == 0x2f && prop47) {
        variants = 4;
    }
    if (variants != 0) {
        call = (Rand_Rand(variants) + 0x24) & 0xffff;
        delay = (Rand_Rand(5) + 5) * FRAME_RATE_INT;
    }
    if (d->klass == 0x10) call = 0x26;
    if (3 < ((call - 0x24) & 0xffff)) call = 0x24;
    if (call != 0x24 && !DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(call), 0)) call = 0x24;
    DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
    d->idle_anim_time = GameState.frame + delay;
    return 1;
}

// [decomp] 0x0013cc70 DroneAnim_SetWalkIdleAnim
// Animação de andar (0x37; na patrulha, 0x37-0x39 pelo id da máquina; 0x39 em 07000014).
uint32_t DroneAnim_SetWalkIdleAnim(DCVars_tag* dcv, int8_t wait) {
    Drone_tag* d = dcv->drone;
    if (wait != 0 && GameState.frame < d->idle_anim_time) return 0;
    uint32_t call = 0x37;
    if (d->sm.state == 7) call = static_cast<uint32_t>(static_cast<int32_t>(d->sm.id + 1) % 3 + 0x37);
    if (GameState.level_file == 0x7000014) call = 0x39;
    if (2 < ((call - 0x37) & 0xffff)) call = 0x37;
    if (call != 0x37 && !DroneAnim_CanDoAnimState(dcv, static_cast<int16_t>(call), 0)) call = 0x37;
    DroneAnim_CallAnim(0.0f, call, 0, 0, 0, 0, dcv);
    d->idle_anim_time = GameState.frame;
    return 1;
}

// [decomp] 0x0013e988 NDrone2_PatrolTalk
// De vez em quando (um por vez, metade das vezes) um guarda da classe 3 fala sozinho na patrulha.
uint32_t NDrone2_PatrolTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    if (d->health <= 0.0f) return 0;
    if (Drone_IsTalking(d) != 0) return 0;
    NPCGLOBALS_tag& n = NPCGlobals;
    if (GameState.frame < n.patrol_talk_time) return 0;
    if (Rand_Rand(2) != 0) {
        n.patrol_talk_time = GameState.frame + FRAME_RATE_INT * 5;
        return 0;
    }
    if (d->klass != 3) return 0;
    const uint32_t r = Rand_Rand(5);
    n.patrol_talk_time = GameState.frame + FRAME_RATE_INT * 10;
    NDrone2_SpeechSFX(dcv, r + 0xb3, 0, 0, 0);
    return 1;
}

namespace {

// O número do som que o personagem fala agora (o nó DYNAMICSOUNDS de Drone_tag::sound, +0x34 sem os bits do
// chamador), ou 0xffffffff sem fala. Como no original, o nó é lido mesmo depois de o som acabar.
uint32_t speech_sound(const Drone_tag* d) {
    if (d->sound == 0) return 0xffffffffu;
    return nf::p32_ptr<const DYNAMICSOUNDS>(d->sound)->sound & 0xfffff;
}

int32_t counter_210 = 0;  // NDrone2_HandleTalking: ticks até a próxima tentativa de fala em 07000008

}  // namespace

PowerSfxEntry PowerSfx[18];

// [decomp] 0x0013eff8 NDrone2_DeathTalk
// O grito de morte pela classe do personagem (Drone_tag::klass; os de 0x15 marcado, 9 em vez de 1); em 0700000c/0d as
// classes 5 e 6 sorteiam 0xcd ou 0xd2, e nenhuma das duas se uma delas já toca. Não repete a fala que já toca.
uint32_t NDrone2_DeathTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (bots_on()) return 0;
    uint32_t sfx;
    switch (d->klass) {
        case 1: sfx = 4; break;
        case 2: sfx = 5; break;
        case 3: sfx = 3; break;
        case 5:
        case 6:
            sfx = 1;
            if (GameState.level_file - 0x700000c < 2) {
                const uint32_t now = speech_sound(d);
                if (Drone_SfxInfo[0xcd].sfx == now || Drone_SfxInfo[0xd2].sfx == now) return 0;
                sfx = Rand_Rand(2) != 0 ? 0xcd : 0xd2;
            }
            break;
        case 8:
        case 10: sfx = 10; break;
        case 0xc: sfx = 0xd; break;
        case 0x10: sfx = 8; break;
        case 0x12:
        case 0x13: sfx = 0xe; break;
        case 0x14: sfx = 2; break;
        default: sfx = d->unknown_15 != 0 ? 9 : 1; break;
    }
    if (Drone_SfxInfo[sfx].sfx == speech_sound(d)) return 0;
    NDrone2_SpeechSFX(dcv, sfx, 1, 0, 0);
    return 1;
}

// [decomp] 0x0013e770 NDrone2_PowerstationTalk
// Os alto-falantes de 0700000c/0d: com o canal 0x98 ligado, um personagem ativo e calado dá o aviso de cada canal de
// PowerSfx que ligou (um por canal: NPCGlobals.power_talk_channel guarda o último). Sempre devolve 0.
uint32_t NDrone2_PowerstationTalk(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (Drone_IsTalking(d) != 0) return 0;
    const obj_tag* obj = d->obj;
    bool unable = true;
    if (obj != nullptr) {
        const Drone_tag* self = static_cast<const Drone_tag*>(obj->data);
        const uint32_t flags = self->flags;
        if ((flags & 0x600) == 0 && (flags & 0x100) != 0 && 0.0f < self->health && obj->type != 0x11 &&
            (obj->life & 1) == 0) {
            unable = false;
        }
    }
    if (unable || switch_channels[0x98] == 0) return 0;
    const uint32_t level = GameState.level_file;
    if ((level != 0x700000c && level != 0x700000d) || PowerSfx[0].sfx == 0) return 0;
    for (int i = 0; i < 18; ++i) {
        const PowerSfxEntry& e = PowerSfx[i];
        const uint32_t ch = level == 0x700000c ? e.channel_c : e.channel_d;
        if (ch != 0 && ch != NPCGlobals.power_talk_channel && ch < 256 && switch_channels[ch] != 0 &&
            NDrone2_SpeechSFX(dcv, e.sfx, 0, 0, 0) != 0) {
            NPCGlobals.power_talk_channel = ch;
        }
        if (i + 1 >= 18 || PowerSfx[i + 1].sfx == 0) break;
    }
    return 0;
}

// [decomp] 0x0013f688 NDrone2_HandleTalking
// A cada tick de cada personagem: em 0700000c/0d os alto-falantes; em 07000008 os guardas da classe 0xc conversam
// com o jogador a menos de 3 m que veem (a cada 30 ticks no máximo): falas 0x9b, 0x9c e 0x9d em ciclo (5 s depois da
// primeira, 15 s depois das outras), ou 0x9e uma vez com o canal 0x4a ligado, que encerra a conversa.
void NDrone2_HandleTalking(DCVars_tag* dcv) {
    if (Drone_bDisableSystem != 0) return;
    Drone_tag* d = dcv->drone;
    obj_tag* player = NDrone2_Player();
    const uint32_t level = GameState.level_file;
    if (level != 0x7000008) {
        if (level - 0x700000c < 2) NDrone2_PowerstationTalk(dcv);
        return;
    }
    if (d->klass != 0xc || player == nullptr) return;
    if (9.0f <= Vec_SqDist2D(&player->pos, &dcv->obj->pos)) return;
    if (d->talk_step == 99 || GameState.frame < d->talk_next) return;
    if (counter_210 > 0) {
        --counter_210;
        return;
    }
    counter_210 = 0x1e;
    if (NDrone2_CanSeeObject(d, player, 5, 0) == 0) return;
    if (switch_channels[0x4a] != 0) {
        NDrone2_SpeechSFX(dcv, 0x9e, 1, 0, 0);
        d->talk_step = 99;
        return;
    }
    uint32_t wait;
    switch (d->talk_step) {
        case 0:
            NDrone2_SpeechSFX(dcv, 0x9b, 1, 1, 0);
            d->talk_step = 1;
            wait = FRAME_RATE_INT * 5;
            break;
        case 1:
            NDrone2_SpeechSFX(dcv, 0x9c, 1, 1, 0);
            d->talk_step = 2;
            wait = FRAME_RATE_INT * 15;
            break;
        case 2:
            NDrone2_SpeechSFX(dcv, 0x9d, 1, 1, 0);
            d->talk_step = 1;
            wait = FRAME_RATE_INT * 15;
            break;
        default:
            return;
    }
    d->talk_next = GameState.frame + wait;
}

namespace {

// fptoui do libgcc (fp-bit.c): negativo, zero e NaN viram 0; acima de 2^32 - 1, o máximo.
uint32_t fptoui(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 4294967296.0f) return 0xffffffffu;
    return static_cast<uint32_t>(f);
}

// O começo de cada ramo de NDrone2_BondTalk e de DroneFunc_DoBondTalk: a fala anterior ainda toca? (Uma que acabou
// solta o nó: Drone_tag::sound = 0.)
bool still_talking(Drone_tag* d) {
    if (d->sound == 0) return false;
    if (Sound_IsFinnished(d->sound) == 0) return true;
    d->sound = 0;
    return false;
}

// GameState.frame mais "ticks" quadros a 60 Hz, em float e fptoui como no original.
uint32_t frame_after(float ticks) { return fptoui(float(GameState.frame) + FRAME_RATE_DIV * ticks); }

}  // namespace

// [decomp] 0x00144488 DroneFunc_DoBondTalk
// Uma fala ao jogador enquanto ele mira no personagem (Drone_tag::in_sights): vira a cabeça para ele por `track` ticks,
// fala `sfx` e só fala de novo `wait` ticks (a 60 Hz) depois. state_a conta as falas, state_b é o quadro da próxima e
// state_c as falas desde que o jogador passou a mirar: sem `repeat`, uma só por mira (e o jogador desviar a mira zera
// state_a e state_c). Com state_a = 0xff (o estado acabou de começar) e a mira no personagem, tudo zera. Devolve 1 se
// falou (ou tentou: uma fala recusada pelo driver sem script de rosto devolve 1 sem marcar a espera), 0 se não.
uint32_t DroneFunc_DoBondTalk(DCVars_tag* dcv, uint32_t sfx, uint32_t track, uint32_t wait, int8_t repeat) {
    Drone_tag* d = dcv->drone;
    obj_tag* player = NDrone2_Player();  // o original lê NPCGlobals +0x1a0/+0x1a4 e glb_players como NDrone2_Player
    bool aimed = d->in_sights != 0;
    if (d->state_a == 0xff && aimed) {
        d->state_a = 0;
        d->state_b = 0;
        d->state_c = 0;
    }
    if (!aimed) {
        if (repeat == 0) d->state_a = 0;
        d->state_c = 0;
    }
    if (still_talking(d)) return 0;
    if (d->in_sights == 0) return 0;
    if (GameState.frame < d->state_b) return 0;
    if (d->state_c != 0 && repeat == 0) return 0;
    NDrone2_HeadTrackObj(player, track, 0.0f, dcv);
    if (NDrone2_SpeechSFX(dcv, sfx, 0, 1, 1) == 0 && d->face_script == nullptr) return 1;
    ++d->state_a;
    d->state_b = fptoui(float(GameState.frame) + float(wait) * FRAME_RATE_DIV);
    ++d->state_c;
    return 1;
}

// [decomp] 0x001446b0 NDrone2_BondTalk
// O que os convidados da festa de 07000007 dizem ao jogador (dos estados Civilian, CivilianMission,
// CivilianMissionWait, PartyGirl e CivilianGuard), só se o personagem foi desenhado há pouco (obj_tag::unknown_fc),
// está desarmado, a até 3 m do oponente (o jogador) e virado para ele (opp_bearing até 90 graus). Pela classe (a pele):
//   9 (0x500001b/4f): uma vez, 0x95 ou 0x96, quando o jogador mira; depois, só ao reentrar no estado;
//   0xd (as moças: 0x5000015/20/22/23/24): a cada 10 s de mira, uma de 11 falas na voz da pele (0x58 + 4 x n + voz:
//       0x5000020 = 0, 22 = 1, 23 = 2, as outras 3; a primeira até 1 s depois da mira); o limite state_a <= 6 nunca
//       corta, porque state_a é sorteado de 0 a 6 depois de cada fala (preservado); as entradas 0x74-0x83 de
//       Drone_SfxInfo (as falas 7 a 10 de cada voz) não têm script de rosto e, pedidas com need_face, ficam mudas
//       (NDrone2_SpeechSFX), como no PS2;
//   0xe (0x5000019/21): DroneFunc_DoBondTalk com uma de 0x84-0x8c, olhar 200 ticks, uma por mira, 8 s entre elas;
//   0x10 (0x5000026/66): uma de 0x97-0x9a a cada 8 s de mira;
//   0x11 (0x5000012/13/14): 0x8e a cada 8 s de mira (a primeira até 1 s depois de o jogador mirar);
//   0x12 (0x5000017/18): sem precisar da mira, 2 s depois de começar o estado, a conversa de seis falas 0x52-0x57 em
//       passos de state_a, com as esperas 16, 2.5, 8, 4, 4 e 5 s (as 0x54 e 0x56 sem olhar nem rosto), e por fim
//       solta a cabeça (NDrone2_HeadTrackObj nulo por 600 ticks); com state_a 7 devolve 0.
// Fora desses casos devolve 0 (condições) ou 1 (a classe não fala). Os chamadores não usam o valor.
uint32_t NDrone2_BondTalk(DCVars_tag* dcv) {
    obj_tag* obj = dcv->obj;
    Drone_tag* d = dcv->drone;
    if (obj->unknown_fc == 0) return 0;
    if (3.0f < d->opp_dist) return 0;
    if (1.5707964f < std::fabs(d->opp_bearing)) return 0;
    if (NDrone2_ObjectIsArmed(obj) != 0) return 0;
    if (GameState.level_file != 0x7000007) return 0;
    obj_tag* player = NDrone2_Player();
    if (player == nullptr) return 0;
    switch (d->klass) {
        case 9:
            if (still_talking(d)) return 1;
            if (d->state_a == 0xff) {
                if (d->in_sights == 0) return 1;
                d->state_b = 0;
                d->state_a = 0;
                return 1;
            }
            if (d->in_sights == 0 || d->state_a != 0 || !(d->state_b < GameState.frame)) return 1;
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, Rand_Rand(2) + 0x95, 0, 1, 1) == 0 && d->face_script == nullptr) return 1;
            d->state_b = frame_after(480.0f);
            d->state_a = 1;
            return 1;
        case 0xd: {
            if (still_talking(d)) return 1;
            if (d->state_a == 0xff) {
                if (d->in_sights == 0) return 1;
                d->state_a = Rand_Rand(7);
                d->state_b = GameState.frame + Rand_Rand(FRAME_RATE_INT);
                return 1;
            }
            if (d->in_sights == 0 || 6 < d->state_a || !(d->state_b < GameState.frame)) return 1;
            uint32_t voice = 3;
            if (d->skin == 0x5000020) voice = 0;
            if (d->skin == 0x5000022) voice = 1;
            if (d->skin == 0x5000023) voice = 2;
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, Rand_Rand(0xb) * 4 + voice + 0x58, 0, 1, 1) == 0 && d->face_script == nullptr) {
                return 1;
            }
            d->state_b = frame_after(600.0f);
            d->state_a = Rand_Rand(7);
            return 1;
        }
        case 0xe:
            DroneFunc_DoBondTalk(dcv, Rand_Rand(9) + 0x84, 200, 0x1e0, 0);
            return 1;
        case 0x10:
            if (still_talking(d)) return 1;
            if (d->state_a == 0xff) {
                if (d->in_sights == 0) return 1;
                d->state_b = 0;
                d->state_a = 0;
                return 1;
            }
            if (d->in_sights == 0 || d->state_a != 0 || !(d->state_b < GameState.frame)) return 1;
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, Rand_Rand(4) + 0x97, 0, 1, 1) == 0 && d->face_script == nullptr) return 1;
            d->state_b = frame_after(480.0f);
            d->state_a = 0;
            return 1;
        case 0x11:
            if (still_talking(d)) return 1;
            if (d->state_a == 0xff) {
                if (d->in_sights == 0) return 1;
                d->state_a = 0;
                d->state_b = GameState.frame + Rand_Rand(FRAME_RATE_INT);
                return 1;
            }
            if (d->in_sights == 0 || d->state_a != 0 || !(d->state_b < GameState.frame)) return 1;
            NDrone2_HeadTrackObj(player, 200, 0.0f, dcv);
            if (NDrone2_SpeechSFX(dcv, 0x8e, 0, 1, 1) == 0 && d->face_script == nullptr) return 1;
            d->state_b = frame_after(480.0f);
            return 1;
        case 0x12: {
            if (still_talking(d)) return 1;
            const uint32_t step = d->state_a;
            if (step == 0xff) {
                d->state_a = 0;
                d->state_b = GameState.frame + FRAME_RATE_INT * 2;
                return 1;
            }
            if (6 < step) return 0;
            if (!(d->state_b < GameState.frame)) return 1;
            // A conversa (a tabela de saltos em 0x002f5310): {fala, olhar (ticks; 0 = não olha), olha e mexe o rosto,
            // espera até a próxima (ticks a 60 Hz)}. O passo 0 olha FRAME_RATE_DIV x 180 ticks.
            struct Line {
                uint32_t sfx;
                uint32_t track;
                int8_t face;
                float wait;
            };
            static const Line kLines[6] = {
                {0x52, 0, 1, 960.0f}, {0x53, 200, 1, 150.0f}, {0x54, 0, 0, 480.0f},
                {0x55, 240, 1, 240.0f}, {0x56, 0, 0, 240.0f}, {0x57, 180, 1, 300.0f},
            };
            if (step == 6) {
                NDrone2_HeadTrackObj(nullptr, 600, 0.0f, dcv);
                d->state_a = step + 1;
                return 1;
            }
            const Line& l = kLines[step];
            if (step == 0) {
                NDrone2_HeadTrackObj(player, fptoui(FRAME_RATE_DIV * 180.0f), 0.0f, dcv);
            } else if (l.track != 0) {
                NDrone2_HeadTrackObj(player, l.track, 0.0f, dcv);
            }
            if (NDrone2_SpeechSFX(dcv, l.sfx, 0, l.face, l.face) == 0 && d->face_script == nullptr) return 1;
            d->state_b = frame_after(l.wait);
            d->state_a = d->state_a + 1;
            return 1;
        }
        default:
            return 1;
    }
}

// [decomp] 0x00149818 Drone_MayhewTalk
// Os personagens do tipo 0xc (Mayhew) dizem a fala sfx (sem rosto; NDrone2_ReachedDestNode, pelas flags do nó de rota
// a que um aliado chegou). Peculiaridade mantida: o primeiro do tipo 0xc inativo (sem objeto, desligado, morto, do tipo
// 0x11 ou a remover) encerra a busca, e os seguintes não falam.
void Drone_MayhewTalk(uint32_t sfx) {
    if (Drone_bDisableSystem != 0) return;
    for (LLNODE_tag* n = NPCGlobals.drones.first; n != nullptr; n = n->next) {
        Drone_tag* d = reinterpret_cast<Drone_tag*>(n);
        if (d->type != 0xc) continue;
        obj_tag* obj = d->obj;
        if (obj == nullptr) return;
        const Drone_tag* od = static_cast<const Drone_tag*>(obj->data);
        const uint32_t flags = od->flags;
        if ((flags & 0x600) != 0 || (flags & 0x100) == 0 || od->health <= 0.0f || obj->type == 0x11 ||
            (obj->life & 1) != 0) {
            return;
        }
        DCVars_tag dcv{};
        if (Drone_DCVfromOBJ(obj, &dcv) != 0) NDrone2_SpeechSFX(&dcv, sfx, 0, 0, 0);
    }
}

// [decomp] 0x001787c8 NDrone2_NearArmedPerson
// O oponente à vista está armado (quem tem a propriedade 0x3c). Peculiaridade mantida: o raio pedido
// não é usado; em 07000009/0a basta estar à vista.
uint32_t NDrone2_NearArmedPerson(DCVars_tag* dcv, float) {
    Drone_tag* d = dcv->drone;
    const uint32_t level = GameState.level_file;
    if (0x7000008 < level && level < 0x700000b) {
        if (behaviour_util_getProperty(0x3c, d->behaviour) == 0) return 0;
        return (d->sight_flags & 4) != 0 ? 1 : 0;
    }
    if (d->klass == 0xd) return 0;
    if (behaviour_util_getProperty(0x3c, d->behaviour) == 0) return 0;
    if ((d->sight_flags & 4) == 0) return 0;
    return DroneWeap_OpponentHasWeapon(dcv) != 0 ? 1 : 0;
}

// [decomp] 0x00154f98 NDrone2_BondWithinAngle
// O jogador está dentro do ângulo à frente do personagem.
uint32_t NDrone2_BondWithinAngle(DCVars_tag* dcv, float angle) {
    obj_tag* player = NDrone2_Player();
    if (player == nullptr) return 0;
    const _VECTOR& p = player->pos;
    const _VECTOR& o = dcv->obj->pos;
    const float yaw = ATAN2_APPROX(p.x - o.x, p.z - o.z);
    const float diff = Vec_AngleDifference(dcv->obj->rot.y, yaw);
    return std::fabs(diff) <= angle ? 1 : 0;
}

// [decomp] 0x0014ada0 NDrone2_HeadTrackObj
// Vira a cabeça para obj (com a propriedade 0x4c) por "ticks" (a 60 Hz), na rapidez dada (0,07).
uint32_t NDrone2_HeadTrackObj(obj_tag* obj, uint32_t ticks, float speed, DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (behaviour_util_getProperty(0x4c, d->behaviour) == 0) return 0;
    AnimObjectSetHeadTracking(dcv->obj, obj, 3.0f, 0.14f, 1, 1, 5);
    anim_of(dcv->obj)->head_speed = speed == 0.0f ? 0.07f : speed;
    d->head_target = obj;
    d->head_tracking = obj != nullptr ? 1 : 0;
    d->head_track_until = GameState.frame + static_cast<uint32_t>(float(ticks) * FRAME_RATE_DIV);
    return 1;
}

// ---- morte ----

// [decomp] 0x0014a2f0 NDrone2_SetAsDead
void NDrone2_SetAsDead(DCVars_tag* dcv) {
    DroneWeap_DropWeapon(dcv);
    Drone_tag* d = dcv->drone;
    d->flags |= 0x200;
    d->flags &= 0xffff7fff;
    d->control_flags = 0;
    if (GameState.level_file != 0x700004a) {
        d->flags &= ~0x20u;
        d->flags &= ~0x80u;
    }
    dcv->obj->flags |= 0x20;
    if ((d->flags & 0x1000000) == 0) d->health = 0.0f;
}

// [decomp] 0x0014a218 NDrone2_FadeOut
// Some aos poucos (o alfa do objeto em unknown_103[3]): 0,6 por tick, 8 no estado 0x49. 1 quando
// acabou.
uint32_t NDrone2_FadeOut(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const float step = d->sm.state == 0x49 ? 8.0f : 0.6f;
    if (0.0f < d->fade) {
        d->fade = d->fade - step;
        if (d->fade < 0.0f) d->fade = 0.0f;
        dcv->obj->unknown_103[3] = static_cast<uint8_t>(static_cast<int>(d->fade));
        return 0;
    }
    dcv->obj->unknown_103[3] = 0;
    return 1;
}

// [decomp] 0x0014a298 DroneFunc_DeadDrone
// Com corpos demais (mais que NPCGlobals.unknown_14c), o mais antigo some depressa (estado 0x49).
uint32_t DroneFunc_DeadDrone(DCVars_tag* dcv) {
    NPCGLOBALS_tag& n = NPCGlobals;
    if (n.unknown_14c < n.down_count && n.oldest_down_id == dcv->sm->id) {
        Drone_SM_SetState(dcv->sm, 0x49, 0);
        return 1;
    }
    return 0;
}

// [decomp] 0x0014b158 DroneFunc_DropGrenade
void DroneFunc_DropGrenade(DCVars_tag* dcv, uint16_t kind, uint16_t count) {
    Pickup_CreateSimple(&dcv->obj->matrix, kind, count, 0);
}

// [decomp] 0x0013e660 DroneFunc_SetDeathChannel
// Ao morrer, liga o canal do parâmetro 12; o valor 0x63 é falha da missão (morte pelo jogador ou não,
// inimigo ou não).
uint32_t DroneFunc_SetDeathChannel(Drone_tag* d) {
    const uint8_t ch = d->param12;
    if (ch == 0) return 0;
    if (ch == 0x63) {
        const bool by_player = d->attacker == NDrone2_Player();
        if (d->side == 1) {
            DroneFunc_SetMissionFailReason(d, by_player ? 2 : 4);
        } else {
            DroneFunc_SetMissionFailReason(d, by_player ? 1 : 3);
        }
        return 1;
    }
    switch_channels[ch] = 1;
    switch_channels_time[ch] = GameState.frame;
    return 1;
}

// [decomp] 0x00145bd0 DroneFunc_OnInitDeath
// Ao começar a morrer: falhas de missão (propriedades 0x53/0x54, conforme quem matou), a estatística
// de inimigos abatidos pelo jogador, a fala de morte e as granadas que o capitão deixa.
void DroneFunc_OnInitDeath(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    const bool by_player_obj = d->attacker != nullptr && d->attacker->type == 3;
    const auto fail_check = [&](uint32_t both, uint32_t only53) {
        if (behaviour_util_getProperty(0x54, d->behaviour) != 0 && by_player_obj) {
            DroneFunc_SetMissionFailReason(d, both);
        } else if (behaviour_util_getProperty(0x53, d->behaviour) != 0) {
            DroneFunc_SetMissionFailReason(d, by_player_obj ? both : only53);
        }
    };
    if (d->side == 1) {
        if ((d->flags & 0x1000000) == 0) {
            fail_check(2, 4);
            const bool killed_by_player =
                !bots_on() ? d->attacker == NDrone2_Player() : d->attacker == d->opponent;
            if (killed_by_player && (d->flags & 0x1000000) == 0 && d->side == 1) PlrStat_LogEnemyDispatched(0);
            if (d->head_shot == 0) NDrone2_DeathTalk(dcv);
        }
    } else {
        fail_check(1, 3);
        if (d->head_shot == 0) NDrone2_DeathTalk(dcv);
    }
    if (d->captain == 0) return;
    uint16_t kind34 = 0, kind35 = 0;
    switch (GameState.level_file) {
        case 0x7000001: case 0x7000002: case 0x7000003: case 0x7000004: case 0x7000005: case 0x7000006:
        case 0x7000007: case 0x7000008:
            kind35 = 2;
            break;
        case 0x7000009: case 0x700000a: case 0x700000b: case 0x7000011: case 0x7000012: case 0x7000013:
        case 0x7000014: case 0x7000015: case 0x7000016: case 0x700004a:
            kind34 = 1;
            break;
        default:
            break;
    }
    if (kind34 != 0) DroneFunc_DropGrenade(dcv, 0x34, kind34);
    if (kind35 != 0) DroneFunc_DropGrenade(dcv, 0x35, kind35);
}

// [decomp] 0x0013c780 DroneAnim_LocationDeathAnim
// Animação de morte: acertado por bala no corpo (unknown_52 4 ou 5) com o jogador mirando, cai para a
// frente ou para trás (0x42); senão a genérica (0x40, variante sorteada se a DAnim permite) e a fala.
uint32_t DroneAnim_LocationDeathAnim(DCVars_tag* dcv, int16_t end_state) {
    Drone_tag* d = dcv->drone;
    HITDATA_tag* hit = d->last_hit;
    obj_tag* player = glb_players[0];
    const bool aiming = player != nullptr && anim_of(player) != nullptr && (anim_of(player)->flags & 1) != 0;
    if (hit != nullptr && aiming && static_cast<uint32_t>(hit->unknown_52) - 4 < 2) {
        const float yaw = ATAN2_APPROX(hit->dir.x, hit->dir.z);
        const float diff = Vec_AngleDifference(yaw, dcv->obj->rot.y);
        DroneAnim_CallAnim(0.0f, 0x42, std::fabs(diff) <= 1.5707964f ? 1 : 0, end_state, 0, 1, dcv);
        return 1;
    }
    uint32_t variant = 0;
    d->head_shot = 0;
    if (d->anim_info_10 != 0) variant = Rand_Rand(4);
    NDrone2_DeathTalk(dcv);
    DroneAnim_CallAnim(0.0f, 0x40, variant, end_state, 0, 1, dcv);
    return 1;
}

// [decomp] 0x0014aba8 NDrone2_ExplosionDir
// De que lado veio a explosão (impact + 0x10 = posição): 1 frente, 0 trás, 2/3 lados.
uint32_t NDrone2_ExplosionDir(DCVars_tag* dcv, const void* impact) {
    if (impact == nullptr) return 1;
    const _VECTOR* at = reinterpret_cast<const _VECTOR*>(static_cast<const uint8_t*>(impact) + 0x10);
    const _VECTOR& o = dcv->obj->pos;
    const float yaw = ATAN2_APPROX(at->x - o.x, at->z - o.z);
    const float diff = Vec_AngleDifference(dcv->obj->rot.y, yaw);
    if (0.7853982f <= std::fabs(diff)) {
        if (2.3561947f < std::fabs(diff)) return 0;
        return diff < 0.0f ? 2 : 3;
    }
    return 1;
}

// ---- ataque e explosivos ----

// [decomp] 0x00148c60 DroneFunc_DoForcedAttack
// Mensagem 0x1f: um personagem vivo e ativo que esperava o ataque forçado (0x20000) passa a atacar
// (estado 0x56). Em 07000007, depois do aviso, só os tipos 0x13 e 0x16.
uint32_t DroneFunc_DoForcedAttack(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    obj_tag* obj = dcv->obj;
    if (bots_on()) return 0;
    if (GameState.level_file == 0x7000007 && NPCGlobals.interrogating != 0 && d->type != 0x13 && d->type != 0x16) {
        return 0;
    }
    bool skip = true;
    if (obj != nullptr) {
        Drone_tag* od = static_cast<Drone_tag*>(obj->data);
        const uint32_t f = od->flags;
        if ((f & 0x600) == 0 && (f & 0x100) != 0 && 0.0f < od->health && obj->type != 0x11 && (obj->life & 1) == 0) {
            skip = false;
        }
    }
    if (skip || (d->flags & 0x2020000) != 0x20000) return 0;
    d->flags &= 0xfffdffff;
    d->flags |= 0x24000000;
    dcv->sm->result = 0x56;
    if (dcv->sm->result != 0) return Drone_SM_SetState(dcv->sm, dcv->sm->result, 0);
    return 0;
}

// [decomp] 0x001431c0 DroneFunc_ConsiderExplosive
// Mensagem 0x21: viu um explosivo (obj). Quem reage a granadas (propriedades 0x34/0x3a/0x3b) e não
// está num estado que impede foge dele (estado 0x86) por um tempo e um raio que dependem da arma
// (granadas 4 m, minas e bombas 14 m, 0x36 5 m, 0x37 10 m); depois volta ao estado pedido.
void DroneFunc_ConsiderExplosive(DCVars_tag* dcv, obj_tag* obj, int16_t state) {
    Drone_tag* d = dcv->drone;
    uint32_t* b = d->behaviour;
    if (behaviour_util_getProperty(0x34, b) == 0 && behaviour_util_getProperty(0x3a, d->behaviour) == 0 &&
        behaviour_util_getProperty(0x3b, d->behaviour) == 0) {
        return;
    }
    const uint32_t st = d->sm.state;
    if (st < 0x56) {
        if (0x52 < st || st == 0x2d || st == 0x28 || st == 0x4a || st == 0x4b) return;
    } else if (st == 99 || (0x85 < st && st < 0x89)) {
        return;
    }
    int32_t next = state;
    if (0x85 < next && next < 0x88) return;
    if (d->explosive_seen != 0) return;
    d->explosive_pos = obj->pos;
    d->explosive_radius = 14.0f;
    d->explosive_cel = obj->cel;
    d->explosive_state = 0x87;
    d->explosive_time = FRAME_RATE_INT * 10 + GameState.frame;
    if (next == 0 && d->sm.state < 0x69 && 0x66 < d->sm.state) next = static_cast<int16_t>(d->sm.state);
    const uint16_t next16 = static_cast<uint16_t>(next);
    bool check_3b = false;
    if (obj->type == 5) {
        const BU_tag* bu = static_cast<const BU_tag*>(obj->data);
        if (bu->weapon != nullptr) {
            const auto grenade_prop = [&] {
                return behaviour_util_getProperty(0x3a, d->behaviour) != 0 ||
                       behaviour_util_getProperty(0x3b, d->behaviour) != 0;
            };
            switch (*reinterpret_cast<const uint16_t*>(nf::raw(bu->weapon))) {
                case 0x2a: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
                    if (!grenade_prop()) return;
                    d->explosive_radius = 4.0f;
                    d->explosive_time = GameState.frame + FRAME_RATE_INT * 2;
                    check_3b = true;
                    break;
                case 0x34: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f: case 0x40: case 0x6c: {
                    if (!grenade_prop()) return;
                    d->explosive_radius = 14.0f;
                    const float t = float(GameState.frame) + bu->timer + float(static_cast<int32_t>(FRAME_RATE_INT << 1));
                    d->explosive_time = static_cast<uint32_t>(t);
                    check_3b = true;
                    break;
                }
                case 0x36:
                    if (behaviour_util_getProperty(0x34, d->behaviour) == 0) return;
                    if (behaviour_util_getProperty(0x36, d->behaviour) != 0) return;
                    d->explosive_state = next16;
                    d->explosive_radius = 5.0f;
                    break;
                case 0x37:
                    if (!grenade_prop()) return;
                    d->explosive_state = next16;
                    d->explosive_radius = 10.0f;
                    break;
                default:
                    break;
            }
        }
    } else if (obj->type == 0x2a) {
        if (behaviour_util_getProperty(0x34, d->behaviour) == 0) return;
        if (behaviour_util_getProperty(0x36, d->behaviour) != 0) return;
        d->explosive_state = next16;
        d->explosive_radius = 5.0f;
    }
    if (check_3b && behaviour_util_getProperty(0x3b, d->behaviour) != 0) d->explosive_state = 0x56;
    if (!(0x85 < next && next < 0x88)) d->sm.return_state = static_cast<uint32_t>(next);
    if (d->explosive_state == 0) d->explosive_state = next16;
    d->explosive_seen = 1;
    Drone_SM_SetState(dcv->sm, 0x86, 0);
}

// [decomp] 0x001435f0 DroneFunc_HandleExplosives
// No fim de cada NDrone2_ControlSTANDARD, fora do multijogador e enquanto o personagem não reagiu a um explosivo
// (explosive_seen): percorre os pontos de atenção (Drone_BuildDynamicAwarePoints) e para no primeiro de tipo 2
// (granadas e afins, e as nuvens de fumaça) a menos do raio dele, se o personagem reage a granadas (propriedade 0x3b
// ou 0x3a); se ele vê o objeto (visibilidade >= 0,7 e linha de visão pelo osso 5), manda a si mesmo a mensagem 0x21
// (DroneFunc_ConsiderExplosive: foge). Peculiaridades mantidas: os pontos de tipo 1 (o projétil da arma 0x36) só
// consultam as propriedades 0x34 e 0x36 e seguem adiante; o primeiro de tipo 2 no raio encerra a busca mesmo sem
// visibilidade.
void DroneFunc_HandleExplosives(DCVars_tag* dcv) {
    if (bots_on()) return;
    Drone_tag* d = dcv->drone;
    if (d->explosive_seen != 0) return;
    obj_tag* obj = dcv->obj;
    NPCGLOBALS_tag* n = &NPCGlobals;
    const DroneAwarePoint_tag* found = nullptr;
    for (uint32_t i = 0; i < n->aware_count; ++i) {
        const DroneAwarePoint_tag& p = n->aware_points[i];
        if (p.kind == 1) {
            if (behaviour_util_getProperty(0x34, dcv->drone->behaviour) != 0) {
                behaviour_util_getProperty(0x36, dcv->drone->behaviour);
            }
            continue;
        }
        if (p.kind != 2) continue;
        if (behaviour_util_getProperty(0x3b, dcv->drone->behaviour) == 0 &&
            behaviour_util_getProperty(0x3a, dcv->drone->behaviour) == 0) {
            continue;
        }
        if (!(Vec_SqDist3D(&obj->pos, &p.pos) < p.radius * p.radius)) continue;
        if (0.7f <= DroneVision_ObjectVisibility(d, p.obj) && NDrone2_CanSeeObject(d, p.obj, 5, 0) != 0) found = &p;
        break;
    }
    if (found != nullptr) Drone_SM_SendMsgSelf(0x21, nf::p32_u(found->obj), 0, 0, dcv);
}

// [decomp] 0x00145150 NDrone2_SeenAndAttacking
// Conta, uma vez por volta da música (MusicVars), os inimigos que viram o jogador há pouco e estão
// atacando; com bastantes (ou um "visto atacando"), liga a música de combate (evento 2).
void NDrone2_SeenAndAttacking(DCVars_tag* dcv) {
    Drone_tag* d = dcv->drone;
    if (d->forced_attack == 0 && d->side != 1) return;
    if (GameState.level_file == 0x700000c && (d->sight_flags & 8) == 0) return;
    const float dist = d->opp_dist;
    bool recent = GameState.frame < d->seen_time + music<uint32_t>(4) * FRAME_RATE_INT;
    if (dist < music<float>(8) && music<uint16_t>(0xc) != 0) recent = true;
    if (dist < music<float>(0x14)) music<float>(0x14) = dist;
    if (d->seen_ticks != 0 && d->opp_dist < music<float>(0x18)) music<float>(0x18) = d->opp_dist;
    if (music<uint32_t>(0x1c) < d->seen_time) music<uint32_t>(0x1c) = d->seen_time;
    if (!recent && d->forced_attack == 0) return;
    if (music<uint32_t>(0x20) == 0) {
        for (int32_t i = 0; i < pDCVCount; ++i) pNPCPassed[i] = nullptr;
        music<uint32_t>(0x20) = 1;
        pDCVCount = 0;
    }
    for (int32_t i = 0; i < pDCVCount; ++i) {
        if (pNPCPassed[i] == d) return;
    }
    if (pDCVCount < 100) pNPCPassed[pDCVCount++] = d;
    ++music<uint16_t>(0xe);
    ++NPCGlobals.unknown_188[1];
    if (music<uint16_t>(0) <= music<uint16_t>(0xe) || music<uint16_t>(0xc) != 0 || d->forced_attack != 0) {
        music<uint16_t>(0xc) = 1;
        Music_Event(2, 5);
        music<uint32_t>(0x10) = GameState.frame;
    }
}
