// Itens pegáveis (módulo Pickup do ACTION.ELF), Player_EquipKey e Check_Target (o objeto na mira do
// jogador e o pulso FlashMod com que os itens piscam). Ver pickup.h.
//
// Divergências:
// - Nas contas da VU0 os temporários sem valor valem 0: o w do vetor da gravidade escalada em
//   Pickup_Update (vmulx.xyz não escreve o w de vf4, que fica com o que o registrador tinha; o
//   original soma esse lixo ao w da velocidade e da matriz), o w do recuo até o chão (vf4 carregado da
//   pilha não inicializada) e o w do fim do raio de Check_Target. O w da linha 3 da matriz não muda o
//   desenho: psiDrawObjectMatrix força a coluna w para (0, 0, 0, 1), como no PS2.
// - As mensagens (Txt_GetStringFromHeap, Txt_BindLabel) só são montadas com o texto e o formato não
//   nulos; com o módulo de texto ainda em stub, nenhuma mensagem sai (o original não testa).
// - Pickup_Handler nos bots e os ramos do multijogador leem os dados do bot (Drone_tag::bot) e as
//   regras da partida (MPSettings) pelos offsets do original; como os bots e o multijogador não foram
//   portados, BOT_getDefaultStats devolve um bloco zerado em vez de um ponteiro do PS2.
// - Guardas (contadas em g_pickup_guards, zero nos dados reais): um personagem do tipo 2 sem dados de
//   bot pegando um item (o original leria a partir do endereço 0), uma arma ou munição com o item fora
//   de weapon_data ou de BLData::weapons (o original leria e escreveria além das tabelas; o item fica)
//   e Player_EquipKey com uma chave que cairia fora do BLData (o original escreve sem limite).

#include "engine/pickup.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/anim.h"
#include "engine/collide.h"
#include "engine/door.h"
#include "engine/drone.h"
#include "engine/drone_stubs.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/modules.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/resetmap.h"
#include "engine/trigger.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/weapons.h"

alignas(4) int16_t PickupMatrix[kPickupSets * 5];
alignas(4) int16_t UseableGuns[27];
uint32_t g_pickup_guards = 0;

namespace {

uint32_t mp_u32(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
int16_t mp_i16(uint32_t offset) {
    int16_t v;
    std::memcpy(&v, MPSettings + offset, 2);
    return v;
}
bool multiplayer() { return mp_u32(0x180) != 0; }
uint32_t weapon_u32(int32_t weapon, uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, weapon_data + weapon * kWeaponStride + offset, 4);
    return v;
}
_VECTOR& row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
_VECTOR sub4(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }

// Dados de um bot (Drone_tag::bot): +0x146 + arma x 0xc tem a arma; +0x1be; +0x75c o jogador do bot
// em MPSettings (0x30 bytes cada; +0x20 o time); +0xa4 a vida máxima; +0x769 o colete.
uint8_t* bot_of(Drone_tag* d) { return static_cast<uint8_t*>(d->bot); }
uint8_t& bot_has(uint8_t* bot, int32_t weapon) { return bot[weapon * 0xc + 0x146]; }
uint32_t bot_team(uint8_t* bot) {
    int16_t slot;
    std::memcpy(&slot, bot + 0x75c, 2);
    return mp_u32(uint32_t(slot * 0x30 + 0x20));
}

// sprintf com o texto e o formato do módulo de texto (nulos enquanto ele for stub).
template <class... Args>
char* format_msg(uint32_t label, Args... args) {
    char* text = Txt_GetStringFromHeap(0);
    const char* format = Txt_BindLabel(label, 0);
    if (text != nullptr && format != nullptr) std::sprintf(text, format, args...);
    return text;
}

// O bot "by" pega o item (só no multijogador). Devolve falso quando o item fica no chão.
bool bot_takes(PICKUPINFO* info, obj_tag* by) {
    Drone_tag* d = static_cast<Drone_tag*>(by->data);
    uint8_t* bot = bot_of(d);
    Control_Plr2Ind(by);
    if (bot == nullptr) {
        ++g_pickup_guards;  // port: um personagem que não é bot leria os dados dele no endereço 0
        return false;
    }
    uint32_t item = info->item;
    if (multiplayer()) {
        if (bot_team(bot) != 0) {
            if (item == 0xd) item = 10;
        } else if (item == 10 && bot[0x1be] != 0) {
            item = 0xd;
        }
    }
    const int16_t w = int16_t(item);
    switch (info->kind) {
        case kPickupWeapon:
            if (bot_has(bot, w) == 0) {
                int16_t drop = 0;
                if (mp_u32(0x184) != 0 && mp_u32(0x1bc) != 0) {
                    const uint32_t mode = mp_u32(0x1a4);
                    const bool team_rule = (mode == 0x20000080 && bot_team(bot) == 0) ||
                                           (mode == 0x20000040 && bot_team(bot) == 1);
                    const int16_t klass = weapon_def(PickupMatrix[mp_u32(0x1b4) * 5])->klass;
                    for (int16_t k = 2; k < 0x53; ++k) {
                        if (k == klass || bot_has(bot, k) == 0) continue;
                        if (k == 0x3b && team_rule) continue;
                        drop = k;
                        break;
                    }
                }
                if (BOTWEAP_EquipWeapon(d, w, int16_t(info->count)) == 0) return false;
                if (drop != 0) {
                    bot_has(bot, drop) = 0;
                    BOTSTATE_pickupWeaponChangeChoice(d, w, 0, 0);
                }
                if (BOTWEAP_hasLoadedWeapon(d, w) == 0) return false;
                BOTSTATE_pickupWeaponChangeChoice(d, w, 0, 0);
                break;
            }
            [[fallthrough]];
        case kPickupAmmo:
            BOTWEAP_EquipAmmo(d, w, int16_t(info->count));
            if (BOTWEAP_hasLoadedWeapon(d, w) == 0) return false;
            BOTSTATE_pickupWeaponChangeChoice(d, w, 1, 0);
            break;
        case kPickupArmour: {
            const float health = d->health + 20.0f;
            d->health = health;
            if (health < 0.0f) {
                d->health = 0.0f;
            } else {
                uint16_t max;
                std::memcpy(&max, bot + 0xa4, 2);
                if (float(int32_t(max)) < health) d->health = float(int32_t(max));
            }
            const uint8_t armour = uint8_t(bot[0x769] + uint8_t(info->count));
            bot[0x769] = armour < 0x33 ? armour : 0x32;
            BOTWEAP_setDroneArmour(d);
            break;
        }
        default:
            return true;  // sem a hora da visita
    }
    BOTSTATE_setPickupVisitTime(d, MP_getMPpickupFromPickupinfo(info));
    return true;
}

}  // namespace

// [decomp] 0x0018dda0 Pickup_Create
// Cria o item (objeto 0x2f com o modelo glist) na posição (ou na linha 3 de m, que também vira a
// matriz). Não cria sem modelo, com item 0xffff, nem a ficha 007 sem a medalha de ouro; os tipos 7-11
// viram a arma do conjunto (Pickup_CreateFromSet). A fase 07000008 dá 4 do item 0x2c. obj->pos fica no
// chão 1 m abaixo (build_PointOnFloor) e a matriz onde estava. Os posicionados (placed) ficam parados;
// os soltos se alinham para cima, saem de dentro do que estiverem tocando (esfera contra o cenário),
// descem até a base do modelo e caem (estado 0). Tingido pela luz da sala (flag 0x800), menos a ficha,
// o colete (que pisca perto da câmera, 0x80000) e os do multijogador, que também giram.
obj_tag* Pickup_Create(_VECTOR* pos, _VECTOR* rot, MATRIX* m, celglist_tag* glist, uint16_t kind, uint16_t item,
                       uint32_t count, uint32_t channel, uint32_t sound, int8_t placed, uint16_t respawn,
                       uint16_t lifetime, uint32_t label) {
    if (glist == nullptr || item == 0xffff) return nullptr;
    if (kind == kPickupBonus && PlrStats_HasGoldMedal() == 0) return nullptr;
    _VECTOR from_matrix;
    if (pos == nullptr) {
        from_matrix = row3(*m);
        pos = &from_matrix;
    }
    if (uint16_t(kind - kPickupSetFirst) < 5) return Pickup_CreateFromSet(pos, rot, kind);
    obj_tag* obj = control_create_object(0x40, pos, rot, nullptr);
    if (obj == nullptr) return nullptr;
    if (m != nullptr) Mat_Copy(m, &obj->matrix);
    PICKUPINFO* p = pickup_of(obj);
    if (GameState.level_file == 0x7000008 && item == 0x2c) count = 4;
    p->item = item;
    p->count = uint16_t(count);
    p->channel = uint16_t(channel);
    p->kind = kind;
    p->sound = sound == 0 ? uint16_t(0xffff) : uint16_t(sound);
    p->respawn = respawn;
    p->mp_index = -1;
    p->lifetime = lifetime;
    p->label = label;
    Vec_Zero(&p->vel);
    obj->glist = glist;
    obj->mode |= 0x44;
    obj->flags = (glist->flags | 0x840) & 0xffffdfffu;
    if (kind == kPickupBonus || multiplayer()) {
        p->spin = 1;
        obj->flags &= 0xfffff7ffu;
    } else {
        p->spin = 0;
    }
    if (kind == kPickupArmour) obj->flags = (obj->flags | 0x80000) & 0xfffff7ffu;
    _VECTOR floor = *pos;
    cel_tag* cel = build_FindCel(pos, glb_world);
    build_PointOnFloor(cel, nullptr, &floor, 1.0f, nullptr);
    Vec_Copy2(&floor, &obj->pos, &obj->old_pos);
    build_LinkToRoom(obj, 0, glb_world);
    Control_BuildWorldSph(obj);
    obj->type = kPickupObjectType;
    if (placed == 0) {
        _VECTOR dir;
        Mat_GetDir(&dir, &obj->matrix);
        Mat_Align2Up(&obj->matrix, &CONSTANT_UP1, &dir);
        P32<HITDATA_tag> hits = nullptr;
        _VECTOR centre = obj->sphere;
        Collide_SphereIntersect(&centre, obj->sphere.w, obj->cel, obj, nullptr, &hits, 0, 0, 0x50d, 8);
        Collide_FreeHitList(&hits);
        row3(obj->matrix) = add4(row3(obj->matrix), sub4(centre, obj->sphere));
        obj->matrix.m[3][1] = obj->matrix.m[3][1] - (obj->sphere.w - std::fabs(obj->glist->box_min.y));
        Control_BuildWorldSph(obj);
        p->state = kPickupFalling;
    } else {
        p->state = kPickupResting;
    }
    obj->mode |= 0x20;
    p->offset = sub4(row3(obj->matrix), obj->sphere);
    if (multiplayer()) {
        p->mp_index = -1;
        if (p->state != kPickupFalling) MP_RegisterPickup(obj);
    }
    return obj;
}

// [decomp] 0x0018e148 Pickup_Update
// Um tick do item. Esperando reaparecer: volta (sem a flag 0x10) depois de respawn x 10 s. Caindo:
// a velocidade ganha a gravidade, a matriz anda, e um raio da posição anterior até a base do modelo
// procura o chão; ao passar dele o item pousa (a base no ponto atingido), herdando o movimento de
// objetos que não sejam balas, personagens ou os tipos 0x11/0x1a (então continua "caindo" sobre
// eles). Os que giram viram 1 rad/s em y. Depois procura quem o pega (Pickup_Handler): o primeiro
// jogador vivo (estado 1, não morto) e, no multijogador com bots, o primeiro bot ativo e vivo a menos
// de raio + 1,1 m do centro da esfera, com linha de visão até ela. Por fim conta a vida útil.
void Pickup_Update(obj_tag* obj) {
    if (obj == nullptr) return;
    PICKUPINFO* p = pickup_of(obj);
    if (p->state == kPickupRespawning) {
        if (uint32_t(GameState.frame - obj->created) <= FRAME_RATE_INT * p->respawn * 10) return;
        obj->flags &= 0xffffffefu;
        p->state = kPickupResting;
        return;
    }
    if (p->state == kPickupFalling) {
        const float dt2 = REC_FRAME_RATE * REC_FRAME_RATE;
        // vmulx.xyz: o w de vf4 não é escrito (ver o cabeçalho; aqui 0).
        p->vel = add4(p->vel, {WldGravity.x * dt2, WldGravity.y * dt2, WldGravity.z * dt2, 0.0f});
        row3(obj->matrix) = add4(row3(obj->matrix), p->vel);
        obj->mode |= 0x20;
        _VECTOR end = row3(obj->matrix);
        end.y = end.y - std::fabs(obj->glist->box_min.y);
        P32<HITDATA_tag> hits = nullptr;
        if (Collide_RayIntersect(&obj->old_pos, &end, obj->cel, obj, nullptr, &hits, 0, 0x50d, 0)) {
            const float below = obj->matrix.m[3][1] - hits->pos.y;
            bool riding = false;
            if (below < 0.0f) {
                for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
                    obj_tag* o = h->obj;
                    if (o == nullptr) continue;
                    switch (o->type) {
                        case 2:
                        case 5:
                        case 0x11:
                        case 0x1a:
                            break;
                        default:
                            Control_InheritVelocity(obj, o, 3.0f);
                            row3(obj->matrix) = obj->pos;
                            riding = true;
                            break;
                    }
                }
            }
            if (below < 0.0f) {
                const float h = -std::fabs(obj->glist->box_min.y);
                // vf4 vem da pilha (lixo no w; aqui 0) e só xyz recebe dir x h.
                const _VECTOR at{hits->pos.x + hits->dir.x * h, hits->pos.y + hits->dir.y * h,
                                 hits->pos.z + hits->dir.z * h, hits->pos.w};
                const _VECTOR placed = add4(at, p->offset);
                obj->pos = placed;
                row3(obj->matrix) = placed;
                if (std::fabs(at.y) != 0.0f) Vec_Zero(&p->vel);
                if (!riding) {
                    p->state = kPickupResting;
                    MP_RegisterPickup(obj);
                }
            }
            Collide_FreeHitList(&hits);
        }
    }
    if (p->spin != 0) {
        _VECTOR turn{0.0f, REC_FRAME_RATE, 0.0f, 0.0f};
        MATRIX r;
        Mat_IdentityT(&r);
        RotMatrix(&turn, &r);
        Concat(&obj->matrix, &r);
        Mat_CopyRot(&r, &obj->matrix);
    }
    const float reach = (obj->sphere.w + 1.1f) * (obj->sphere.w + 1.1f);
    for (uint16_t i = 0; i < 4; ++i) {
        obj_tag* pl = glb_players[i];
        if (pl == nullptr || int16_t(pl->state) != 1 || pl->type == 0x12) continue;
        if (!(Vec_SqDist3D(&obj->sphere, &pl->pos) < reach)) continue;
        if (Collide_LineOfSight(&pl->sphere, &obj->sphere, pl->cel, obj, nullptr, 0x721)) {
            Pickup_Handler(obj, p, pl);
            return;
        }
    }
    if (mp_u32(0x184) != 0) {
        for (uint16_t i = 0; i < 4; ++i) {
            obj_tag* bot;
            bot = nf::p32_load<obj_tag>(MPGame + (i + 4) * 0x30 + 0x1c);
            if (bot == nullptr || bot->type == 0x11) continue;
            const Drone_tag* d = static_cast<const Drone_tag*>(bot->data);
            uint32_t flags;
            std::memcpy(&flags, reinterpret_cast<const uint8_t*>(d) + 0x4f8, 4);
            if ((flags & 0x600) != 0 || (flags & 0x100) == 0 || d->health <= 0.0f || (bot->life & 1) != 0) continue;
            if (!(Vec_SqDist3D(&obj->sphere, &bot->pos) < reach)) continue;
            if (Collide_LineOfSight(&bot->pos, &obj->sphere, bot->cel, obj, nullptr, 0x721)) {
                Pickup_Handler(obj, p, bot);
                return;
            }
        }
    }
    if (p->lifetime != 0 && --p->lifetime == 0) {
        obj->life |= 1;
        MP_UnregisterPickup(obj);
    }
}

// [decomp] 0x0018e650 Pickup_Handler
// "by" pega o item. Um bot (tipo 2, só no multijogador) vai por BOTWEAP_*, sem mensagem nem canal. O
// jogador recebe a arma melhorada (Upgrade_Weapon; no multijogador Upgrade_MPWeapon, e as armas 10 e
// 0xd trocam pelo personagem): arma nova (no multijogador com bots e arma única, a que ele tinha da
// classe 2 ou de 3 a 0x47, fora a do conjunto, sai do inventário), ou munição se já tinha; munição;
// chave; colete (a 50, se estiver abaixo); invisibilidade; ficha 007. Se a arma ou a munição não
// entrou, ou o colete já estava em 50, o item fica. Depois a mensagem na tela do jogador (3 s; 1,5 s
// no multijogador), o canal de switch, o som e o destino do item (some, reaparece ou fica).
void Pickup_Handler(obj_tag* obj, PICKUPINFO* info, obj_tag* by) {
    if (by->type == 2) {
        if (!bot_takes(info, by)) return;
    } else {
        BLData* b = bloke_of(by);
        const int8_t player = b->player;
        uint32_t item = info->item;
        if (!multiplayer()) {
            item = Upgrade_Weapon(item);
        } else {
            const uint8_t* stats = BOT_getDefaultStats(mp_u32(uint32_t(player * 0x30 + 0x24)));
            if (stats[9] == 1) {
                if (item == 10 && b->weapons[10].have != 0) item = 0xd;
            } else if (item == 0xd) {
                item = 10;
            }
            item = Upgrade_MPWeapon(int16_t(b->player), item);
        }
        const int16_t w = int16_t(item);
        if ((info->kind == kPickupWeapon || info->kind == kPickupAmmo) &&
            (uint16_t(w) >= kWeaponSlots || info->item >= kWeaponDataCount)) {
            ++g_pickup_guards;  // port: arma fora de weapon_data/BLData::weapons (o original leria e escreveria além)
            return;
        }
        int32_t taken = 0;  // balas que entraram (mensagem da munição)
        uint16_t msg = info->kind;
        switch (info->kind) {
            case kPickupWeapon:
                if (b->weapons[w].have == 0) {
                    int16_t replace = 0;
                    if (mp_u32(0x184) != 0 && mp_u32(0x1bc) != 0) {
                        const int16_t klass = weapon_def(PickupMatrix[mp_u32(0x1b4) * 5])->klass;
                        if (klass == 2 || b->weapons[2].have == 0) {
                            for (int16_t k = 3; k < 0x48; ++k) {
                                if (k == klass || b->weapons[k].have == 0) continue;
                                replace = k;
                                break;
                            }
                        } else {
                            replace = 2;
                        }
                    }
                    if (Player_EquipWeapon(b, w, int16_t(info->count)) == 0) return;
                    if (replace != 0) {
                        b->weapons[replace].rounds = 0;
                        b->weapons[replace].have = 0;
                        ANIMOBJ_tag* anim = anim_of(by);
                        if (anim->weapon == replace) anim->next_weapon = int8_t(w);
                    }
                    msg = kPickupWeapon;
                    break;
                }
                taken = Player_EquipAmmo(b, w, int16_t(info->count));
                msg = kPickupAmmo;
                if (taken == 0) return;
                break;
            case kPickupAmmo:
                taken = Player_EquipAmmo(b, w, int16_t(info->count));
                if (taken == 0) return;
                break;
            case kPickupKey:
                Player_EquipKey(b, info->item);
                break;
            case kPickupArmour:
                if (50.0f <= b->armour) return;
                b->armour = 50.0f;
                b->unknown_8bc = 1.0f;
                break;
            case kPickupInvisibility:
                if (by->type == 3) bloke_of(by)->visibility = 0.0f;
                break;
            case kPickupBonus:
                PlrStat_LogBondBonus();
                break;
            default:
                break;
        }
        // A mensagem (o nome da arma em weapon_data +0x38, +0x3c no multijogador; o da munição em
        // ammo_data +8) e o som de cada tipo.
        char* text = nullptr;
        const auto ammo_name = [&] {
            uint32_t v;
            std::memcpy(&v, ammo_data + uint32_t(weapon_def(info->item)->ammo_type) * kAmmoStride + 8, 4);
            return v;
        };
        switch (msg) {
            case kPickupWeapon:
                if (!multiplayer()) {
                    char* name = Txt_BindLabel(weapon_u32(info->item, 0x38), 0);
                    text = format_msg(0x200000d, name);
                } else {
                    text = format_msg(weapon_u32(info->item, 0x3c));
                }
                info->sound = 0xf5;
                break;
            case kPickupAmmo:
                // "Picked up %dx %s"; na língua 1 o formato pede o nome antes da quantidade.
                text = Txt_GetStringFromHeap(0);
                if (multiplayer()) {
                    char* name = Txt_BindLabel(ammo_name(), 0);
                    if (text != nullptr) std::sprintf(text, "%d x %s", int(taken), name);
                } else if (Txt_GetLanguage() == 1) {
                    const char* format = Txt_BindLabel(0x200000e, 0);
                    char* name = Txt_BindLabel(ammo_name(), 0);
                    if (text != nullptr && format != nullptr) std::sprintf(text, format, name, int(taken));
                } else {
                    const char* format = Txt_BindLabel(0x200000e, 0);
                    char* name = Txt_BindLabel(ammo_name(), 0);
                    if (text != nullptr && format != nullptr) std::sprintf(text, format, int(taken), name);
                }
                info->sound = 0xf5;
                break;
            case kPickupKey:
                text = format_msg(0x2000010);
                break;
            case kPickupArmour:
                text = format_msg(0x200000f);
                info->sound = 0x141;
                break;
            case kPickupMission:
                if (info->label != 0) text = Txt_BindLabel(info->label, 0);
                break;
            case kPickupInvisibility:
                text = format_msg(0x2000013);
                break;
            case kPickupBonus:
                text = format_msg(0x200004a);
                info->sound = 0x14c;
                break;
            default:
                break;
        }
        if (text != nullptr) Text_AddMsg(b->player, 0, 6, text, 0, int16_t(int32_t(multiplayer() ? 90.0f : 180.0f)));
        if (info->channel != 0) {
            switch_channels[info->channel] = 1;
            switch_channels_time[info->channel] = GameState.frame;
        }
    }
    Sound_Play3D(info->sound, &row3(obj->matrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (info->respawn == 0) {
        obj->life |= 1;
        if (multiplayer()) MP_UnregisterPickup(obj);
    } else if (info->respawn != 0xffff) {
        obj->flags |= 0x10;
        info->state = kPickupRespawning;
        obj->created = GameState.frame;
    }
}

// [decomp] 0x0018f098 Pickup_MakeRandomWeaponSet
// O conjunto 10 de PickupMatrix: 5 armas diferentes sorteadas de UseableGuns (a primeira só entre as
// 26 primeiras).
void Pickup_MakeRandomWeaponSet() {
    int16_t* set = PickupMatrix + 50;
    for (int i = 4; i >= 0; --i) set[i] = 0;
    for (int i = 0; i < 5; ++i) {
        int16_t gun;
        bool repeated;
        do {
            gun = UseableGuns[int16_t(Rand_Rand(i != 0 ? 0x1b : 0x1a))];
            repeated = false;
            for (int k = 0; k < i; ++k) repeated = repeated || set[k] == gun;
        } while (repeated);
        set[i] = gun;
    }
}

// [decomp] 0x0018f1b0 Pickup_CreateFromSet
// Tipos 7-11 do multijogador: a arma kind - 7 do conjunto da partida (MPSettings + 0x1b4), pela classe
// dela, com dois pentes, parada e reaparecendo em 30 s; a 0x1a traz também a munição 0x1b (um pente,
// modelo 0x020007c4 em meia escala) 10 cm ao lado, e devolve esta.
obj_tag* Pickup_CreateFromSet(_VECTOR* pos, _VECTOR* rot, uint16_t kind) {
    const int16_t gun = PickupMatrix[uint16_t(kind - 7) + mp_i16(0x1b4) * 5];
    const int32_t klass = uint16_t(weapon_def(gun)->klass);
    const weapon_definition_tag* d = weapon_def(klass);
    obj_tag* obj = Pickup_Create(pos, rot, nullptr, hashtable_hashcode_to_celglist(weapon_u32(klass, 0x80)), 0,
                                 uint16_t(d->id), uint32_t(int32_t(d->clip) << 1), 0, 0xf5, 1, 3, 0, 0);
    if (gun == 0x1a) {
        _VECTOR at = *pos;
        at.x = at.x + 0.1f;
        at.z = at.z + 0.1f;
        const weapon_definition_tag* ammo = weapon_def(0x1b);
        obj = Pickup_Create(&at, rot, nullptr, hashtable_hashcode_to_celglist(0x20007c4), 1, uint16_t(ammo->id),
                            uint32_t(int32_t(ammo->clip)), 0, 0xffff, 1, 3, 0, 0);
        if (obj != nullptr) obj->scale = 0.5f;
    }
    return obj;
}

// [decomp] 0x0018f348 Pickup_CreateSimple
// Solta a arma "weapon" (0x6a e 0x6e viram 0x33; as sem item, 1, 0x45, 0x47, 0x50, 0x51 e 0x53, e as
// fora de 1-0x71 não) com "count" balas (nada com 0) na matriz m, com o modelo de weapon_data +0x80:
// cai até o chão e some em 2 min (30 s com bots).
obj_tag* Pickup_CreateSimple(MATRIX* m, int16_t weapon, int16_t count, int16_t kind) {
    if (count == 0) return nullptr;
    int32_t w = weapon;
    switch (w) {
        case 1:
        case 0x45:
        case 0x47:
        case 0x50:
        case 0x51:
        case 0x53:
            return nullptr;
        case 0x6a:
        case 0x6e:
            w = 0x33;
            break;
        default:
            break;
    }
    if (0x70 < uint32_t(w - 1)) return nullptr;
    celglist_tag* glist = hashtable_hashcode_to_celglist(weapon_u32(w, 0x80));
    const uint16_t lifetime = uint16_t(FRAME_RATE_INT * (mp_u32(0x184) != 0 ? 0x1e : 0x78));
    return Pickup_Create(nullptr, nullptr, m, glist, uint16_t(kind), uint16_t(weapon_def(w)->id), uint32_t(int32_t(count)),
                         0, 0xf5, 0, 0, lifetime, 0);
}

// [decomp] 0x001a97a0 Player_EquipKey
// Sem limite no original; uma chave que cairia fora do BLData é ignorada (guarda do port).
void Player_EquipKey(BLData* b, uint16_t key) {
    if (0x70c + uint32_t(key) >= sizeof(BLData)) {
        ++g_pickup_guards;
        return;
    }
    reinterpret_cast<uint8_t*>(b)[0x70c + key] = 1;
}

// ---- o que está na mira do jogador ----

namespace {
uint32_t DoorMsg = 0;  // mensagens de porta mostradas (no máximo 3 por sessão; nunca zerado)
uint32_t LoadMsg = 0;  // ticks até a próxima mensagem de gatilho de carga
}  // namespace

// [decomp] 0x001a7810 Check_Target
// A cada tick (Player_Weapon): calcula FlashMod, o pulso de 1 Hz entre 0,5 e 1 (pelo relógio em
// centésimos de GameState + 0x4c) com que os objetos marcados com 0x80000 (os itens) piscam perto da
// câmera (View_SetupRenderModes_Tweak). A cada 6 ticks, fora dos subestados 2, 6, 7 e 10-16, um raio
// de 50 m dos olhos na direção da arma acha o objeto na mira (BLData::target): um aliado ou civil (ou,
// no multijogador, alguém do time) marca a mira (+0x132 = 1, +0x133 = 3; o padrão é 0 e 2) se a arma
// na mão for letal (fora das fases 07000005-07000008, que marcam sempre); um personagem fica sabendo
// que está na mira (Drone_tag::in_sights e o quadro em unknown_2c4); uma porta a menos de 5 m mostra
// a mensagem dela (Door_DoMsg, até 3 vezes) e um gatilho de carga (modo 10) destrancado, "Press
// Action..." (0x2000009, no máximo a cada 20 s, menos na 07000009).
// Portada aqui com os itens porque FlashMod decide a cor deles: com ela parada em 0 os itens a menos de
// 4 m ficariam pretos.
void Check_Target(obj_tag* obj) {
    if (obj == nullptr) return;
    if (LoadMsg != 0) --LoadMsg;
    switch (int16_t(obj->unknown_f6 - 2)) {
        case 0:
        case 4:
        case 5:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            return;
        default:
            break;
    }
    FlashMod = float(int32_t(GameState.start_time)) * 0.01f;
    const float s = PS2Sinf(FlashMod * 6.2831855f + 1.5707964f);
    FlashMod = ((s + 1.0f) * 0.5f + 1.0f) * 0.5f;
    if (GameState.frame % 6 != 0) return;
    BLData* b = bloke_of(obj);
    uint32_t team = 2;
    if (mp_u32(0x184) != 0) team = MP_getObjectTeam(obj);
    b->unknown_132 = 0;
    b->target = nullptr;
    b->unknown_133[0] = 2;
    _VECTOR head;
    Player_GetHeadPos(&head, obj, nullptr);
    _VECTOR dir;
    Mat_GetDir(&dir, &b->gun->matrix);
    // vmulx.xyz: o w do produto fica com o lixo da pilha (aqui 0).
    const _VECTOR end{head.x + dir.x * 50.0f, head.y + dir.y * 50.0f, head.z + dir.z * 50.0f, head.w};
    P32<HITDATA_tag> hits = nullptr;
    Collide_RayIntersect(&head, &end, obj->cel, obj, nullptr, &hits, 0, 0xc, 0);
    const auto mark = [b] {
        b->unknown_132 = 1;
        b->unknown_133[0] = 3;
    };
    if (hits != nullptr && hits->obj != nullptr) {
        obj_tag* t = hits->obj;
        b->target = t;
        switch (t->type) {
            case 3:
                if (mp_u32(0x184) != 0 && mp_u32(0x198) != 0 && MP_isObjectOnTeam(t, team) != 0) mark();
                break;
            case 2: {
                Drone_tag* d = static_cast<Drone_tag*>(t->data);
                bool enemy;
                if (mp_u32(0x184) == 0) {
                    enemy = d->side == 1;
                } else {
                    enemy = mp_u32(0x198) == 0 || MP_isObjectOnTeam(d->obj, team) == 0;
                }
                d->in_sights = 1;
                d->unknown_2c4 = GameState.frame;
                if (enemy) break;
                const uint32_t level = GameState.level_file;
                if (0x7000008 < level || level < 0x7000005) {
                    // Armas que não matam (soco 1, 0x35, 0x36, 0x43, 0x44, 0x4a-0x4d) não marcam.
                    const int8_t w = anim_of(obj)->weapon;
                    const bool lethal = !(w == 1 || w == 0x35 || w == 0x36 || w == 0x43 || w == 0x44 ||
                                          (0x4a <= w && w <= 0x4d));
                    if (!lethal) break;
                }
                mark();
                break;
            }
            case 0x1c:
                if (GameState.level_file != 0x700004a && hits->dist < 5.0f && DoorMsg < 3) {
                    char* msg = Door_DoMsg(t);
                    if (msg != nullptr) {
                        ++DoorMsg;
                        Text_AddMsg(b->player, 0, 1, msg, 0, 0xb4);
                    }
                }
                break;
            case 0x1d:
                if (Trigger_Type(t) == 10 && hits->dist < 5.0f && LoadMsg == 0 && GameState.level_file != 0x7000009) {
                    LoadMsg = FRAME_RATE_INT * 0x14;
                    if (Trigger_Locked(t) == 0) Text_AddMsg(b->player, 0, 1, Txt_BindLabel(0x2000009, 0), 0, 0xb4);
                }
                break;
            default:
                break;
        }
    }
    Collide_FreeHitList(&hits);
}

void pickup_debug_power_on() {
    DoorMsg = 0;
    LoadMsg = 0;
    std::memset(PickupMatrix, 0, sizeof(PickupMatrix));
    std::memset(UseableGuns, 0, sizeof(UseableGuns));
    g_pickup_guards = 0;
}
