// Port do modo, do tipo e do comportamento dos personagens da IA: NDrone2_DoModeSettingsOLD/NEW,
// NDrone2_DoTypeSettingsOLD, NDrone2_GetDroneTypeAttackTypeFriend, NDrone2_GetDTYPENEW,
// NDrone2_ChangeToAttackMode, as inicializações por modo (NDrone2_init_DMODE_*) e por tipo
// (NDrone2_initDTYPE_*) e as propriedades de comportamento (behaviour_util_*: bits empacotados em três
// palavras, descritos por bitDescs/bitMasks). Ver drone.h.
//
// Divergências intencionais (memória fora do lugar):
// - NDrone2_DoModeSettingsOLD lê DroneModeSettings no índice do estado de ataque (parâmetro 8) sem
//   limite; acima das 35 entradas o original lê a memória seguinte. Aqui vira 0 e conta em
//   g_drone_guards.
// - NDrone2_DoModeSettingsOLD escreve a arma da animação dos reféns (modos 9, 10 e 0x13) antes de
//   AnimObjectNew, isto é, num objeto de animação nulo na primeira inicialização (endereço 0x62 no
//   PS2). Aqui a escrita só acontece com o objeto de animação.
// - NDrone2_DoModeSettingsNEW indexa quatro níveis de alerta pela propriedade 0x20 sem limite; um
//   valor acima de 3 leria a pilha. Aqui vira 0 e conta em g_drone_guards.

#include <cstring>
#include <initializer_list>

#include "engine/anim.h"
#include "engine/drone.h"
#include "engine/gamestate.h"
#include "engine/memory.h"
#include "platform/psi.h"

uint32_t g_drone_guards = 0;

namespace {

void set_props(Drone_tag* d, std::initializer_list<int32_t> props, uint32_t value = 1) {
    for (int32_t p : props) behaviour_util_setProperty(p, d->behaviour, value);
}

// Estado inicial "parado" dos tipos sem estado na tabela: 6 se alguma das propriedades 0x24/0x25,
// senão 4.
uint16_t idle_state(Drone_tag* d) {
    if (behaviour_util_getProperty(0x24, d->behaviour) != 0) return 6;
    if (behaviour_util_getProperty(0x25, d->behaviour) != 0) return 6;
    return 4;
}

void apply_attack_state(Drone_tag* d) {
    if (d->attack_type == 0) {
        d->attack_state = 0;
    } else {
        d->attack_state = static_cast<uint16_t>(DroneTypeSettings[d->attack_type].attack_state);
        if (d->attack_state == 0) d->attack_state = static_cast<uint16_t>(DroneTypeSettings[d->type].attack_state);
    }
}

}  // namespace

// ---- propriedades ----

// [decomp] 0x001c7d98 behaviour_util_getProperty
uint32_t behaviour_util_getProperty(int32_t prop, uint32_t* words) {
    const uint8_t desc = bitDescs[prop * 2];
    return (words[desc & 7] & bitMasks[bitDescs[prop * 2 + 1]]) >> (desc >> 3);
}

// [decomp] 0x001c7de8 behaviour_util_setProperty
void behaviour_util_setProperty(int32_t prop, uint32_t* words, uint32_t value) {
    const uint8_t desc = bitDescs[prop * 2];
    words[desc & 7] &= ~bitMasks[bitDescs[prop * 2 + 1]];
    words[desc & 7] |= value << (desc >> 3);
}

// [decomp] 0x001c7e58 behaviour_util_getStats
uint32_t* behaviour_util_getStats(int32_t type, int32_t set) { return drone_stats[type][set]; }

// [decomp] 0x001c7a98 behaviour_util_get
// Lê a estrutura de comportamento dos parâmetros do posicionamento: tipo, ataque, as palavras de
// bits do modo normal, o tipo amigo, as palavras do modo de ataque e, opcionalmente (marca
// 0x8001xxxx), as estatísticas do tipo (três conjuntos de 8 campos com 8,3,8,8,5,8,8,1 bits).
// Inválida: zera tudo e devolve 0.
uint32_t behaviour_util_get(_BehaviourStruct* out, uint32_t* data) {
    static const uint32_t kWidths[8] = {8, 3, 8, 8, 5, 8, 8, 1};
    out->type = data[0];
    if (data[0] < 0x11) {
        out->attack = data[1];
        const uint32_t w = data[2];
        out->words = static_cast<uint16_t>(w);
        out->bits = static_cast<uint16_t>(w >> 16);
        if (data[1] < 6 && ((w - 1) & 0xffff) < 3 && (w >> 16) - 1 <= 0x5a) {
            if (out->props != nullptr) amemcpy(out->props, data + 3, (w & 0xffff) << 2);
            uint32_t* p = data + 3 + out->words;
            out->friend_type = p[0];
            const uint32_t w2 = p[1];
            p += 2;
            out->words2 = static_cast<uint16_t>(w2);
            out->bits2 = static_cast<uint16_t>(w2 >> 16);
            if (out->props2 != nullptr) amemcpy(out->props2, p, (w2 & 0xffff) << 2);
            if (out->friend_type < 6 && out->words2 - 1u < 3 && out->bits2 - 1u < 0x5b) {
                const uint32_t marker = p[out->words2];
                if (static_cast<int32_t>(marker) >= 0) return 1;
                p += out->words2 + 1;
                if ((marker & 0xffff0000) == 0x80010000) {
                    for (int set = 0; set < 3; ++set) {
                        uint32_t cursor = 0;
                        for (int j = 0; j < 8; ++j) {
                            const uint32_t width = kWidths[j];
                            drone_stats[out->type][set][j] =
                                (p[cursor >> 5] >> (cursor & 0x1f)) & ((1u << (width & 0x1f)) - 1u) & 0xff;
                            cursor += width;
                        }
                        p += (marker & 0xffff) / 3;
                    }
                }
                return 1;
            }
        }
    }
    out->type = 0;
    out->attack = 0;
    out->bits = 0x5b;
    out->words = 3;
    std::memset(out->props, 0, 0xc);
    out->words2 = 3;
    out->bits2 = 0x5b;
    out->friend_type = 0;
    std::memset(out->props2, 0, 0xc);
    return 0;
}

// ---- modo ----

// [decomp] 0x0014cfa0 NDrone2_DoModeSettingsOLD
// Modo da tabela antiga: tipo, lado, alerta e a inicialização do modo, mais propriedades fixas.
void NDrone2_DoModeSettingsOLD(Drone_tag* d, uint32_t mode, uint32_t* behaviour, DIVars_tag* vars) {
    if (0x23 < mode) {
        NDrone2_DoModeSettingsNEW(d, mode, behaviour, vars);
        return;
    }
    const DroneModeSetting& m = DroneModeSettings[mode];
    d->mode_old = 1;
    d->mode_set = 1;
    d->alert = m.alert;
    d->alert_base = m.alert;
    std::memset(d->behaviour_a, 0, 0xc);
    std::memset(d->behaviour_b, 0, 0xc);
    d->behaviour = d->behaviour_a;
    const uint32_t cs = d->channel_state;
    d->type = m.type;
    if (cs == 0 || cs == 0x65 || d->channel_b != 0) {
        d->attack_type = 0;
    } else if (cs < kDroneModeCount) {
        d->attack_type = DroneModeSettings[cs].type;
    } else {
        ++g_drone_guards;  // port: o original lê além da tabela
        d->attack_type = 0;
    }
    d->side = m.side;
    if (m.init != nullptr) m.init(d);
    switch (d->klass) {
        case 0x10: behaviour_util_setProperty(0x51, d->behaviour, 1); break;
        case 0xd: set_props(d, {0x4d, 0x4c, 0x52}); break;
        case 0x11: behaviour_util_setProperty(0x4c, d->behaviour, 1); break;
        case 0x13: set_props(d, {0x4c, 0x52, 0x55}); break;
        default: break;
    }
    set_props(d, {0x18, 0x26, 0x27, 0x28, 0x29, 0x31, 0x32, 0x33, 0x4f, 0x1f, 0x3c, 0x30, 10, 0xc, 0x37, 0x38});
    if (d->type == 0x11) set_props(d, {0x40, 0x53, 0x54});
    if (8 < mode && (mode < 0xb || mode == 0x13)) {
        d->subclass = mode < 0xb ? 1 : 0xc;
        ANIMOBJ_tag* anim = anim_of(d->obj);
        if (anim != nullptr) {
            anim->weapon = 0;
        } else {
            ++g_drone_guards;  // port: o original escreve no endereço 0x62
        }
    }
}

// [decomp] 0x0014c600 NDrone2_DoModeSettingsNEW
// Modo da estrutura de comportamento: propriedades dos dois modos, alertas (0, 0, 0.66, 1 pela
// propriedade 0x20), tipos, estatísticas e vida do tipo, ajustes por fase e pelo script de entrada.
void NDrone2_DoModeSettingsNEW(Drone_tag* d, uint32_t, uint32_t* behaviour, DIVars_tag* vars) {
    static const float kAlerts[4] = {0.0f, 0.0f, 0.66f, 1.0f};  // 0x002f5cc8
    d->mode_old = 0;
    d->mode_set = 1;
    _BehaviourStruct bs;
    std::memset(&bs, 0, sizeof(bs));
    bs.words = 3;
    bs.props = d->behaviour_a;
    bs.words2 = 3;
    bs.props2 = d->behaviour_b;
    behaviour_util_get(&bs, behaviour);
    d->behaviour = d->behaviour_a;
    const auto alert_of = [](uint32_t* words) {
        const uint32_t i = behaviour_util_getProperty(0x20, words);
        if (i < 4) return kAlerts[i];
        ++g_drone_guards;  // port: o original leria a pilha
        return 0.0f;
    };
    const float alert = alert_of(d->behaviour_a);
    d->alert = alert;
    d->alert_base = alert;
    d->alert_attack = alert_of(d->behaviour_b);
    d->behaviour_type = static_cast<uint8_t>(bs.type);
    d->behaviour_attack = static_cast<uint8_t>(bs.attack);
    d->behaviour_friend = static_cast<uint8_t>(bs.friend_type);
    NDrone2_GetDroneTypeAttackTypeFriend(d);
    const uint32_t* stats = behaviour_util_getStats(d->behaviour_type, 1);
    const uint32_t hp = stats[2];
    if (hp == 0) {
        d->stats[0] = 5;
        d->health = 10.0f;
        for (int i = 1; i < 7; ++i) d->stats[i] = 0;
    } else {
        d->health = float(hp);
        d->stats[0] = static_cast<uint8_t>(stats[0]);
        d->stats[1] = static_cast<uint8_t>(stats[1]);
        d->stats[2] = static_cast<uint8_t>(stats[3]);
        d->stats[3] = static_cast<uint8_t>(stats[4]);
        d->stats[4] = static_cast<uint8_t>(stats[5]);
        d->stats[5] = static_cast<uint8_t>(stats[6]);
        d->stats[6] = static_cast<uint8_t>(stats[7]);
    }
    d->accuracy = float(d->stats[0]);
    if (d->klass == 0x10) d->health = 100.0f;
    const uint32_t level = GameState.level_file;
    if (level != 0x7000007) {
        if (0x7000008 < level && level < 0x700000b) {
            behaviour_util_setProperty(0x1a, d->behaviour, 1);
            behaviour_util_setProperty(0x56, d->behaviour, 1);
        } else {
            behaviour_util_setProperty(0x1a, d->behaviour, 0);
        }
    }
    if (vars == nullptr) return;
    switch (vars->params[1]) {
        case 0x600011d:
            behaviour_util_setProperty(0x1d, d->behaviour, 1);
            d->type = 0x17;
            break;
        case 0x600011f:
            d->type = 0x18;
            d->chat_type = 0x19;
            break;
        case 0x6000124:
        case 0x6000126:
            d->type = 0x18;
            d->chat_type = 0x11;
            break;
        case 0x600021f:
            behaviour_util_setProperty(0x38, d->behaviour, 0);
            d->flags |= 0x10;
            break;
        default:
            break;
    }
}

// ---- tipo ----

// [decomp] 0x0014d2e8 NDrone2_DoTypeSettingsOLD
// Estados inicial e de ataque do tipo; já em alerta, começa no estado de ataque (0x56 se nenhum).
void NDrone2_DoTypeSettingsOLD(Drone_tag* d, uint8_t type) {
    d->type = type;
    if (DroneTypeSettings[type].initial_state < 0) {
        uint16_t s;
        if (type == 4) {
            s = 8;
        } else if (type < 5) {
            if (type == 2) {
                s = d->alert < 1.0f ? 0x29 : 0x2a;
            } else {
                s = idle_state(d);
            }
        } else {
            s = type == 7 ? 0x85 : idle_state(d);
        }
        d->initial_state = s;
    } else {
        d->initial_state = static_cast<uint16_t>(DroneTypeSettings[type].initial_state);
    }
    apply_attack_state(d);
    if (DroneTypeSettings[d->type].init != nullptr) DroneTypeSettings[d->type].init(d);
    if (d->alert < 1.0f) return;
    if (d->klass == 0xb) {
        NDrone2_ChangeToAttackMode(d);
        d->health = 100.0f;
        return;
    }
    if (d->type == 5) return;
    if (d->attack_state != 0) {
        d->initial_state = d->attack_state;
        return;
    }
    // Estados que já são de alerta ficam; os outros viram 0x56.
    const int16_t s = static_cast<int16_t>(d->initial_state);
    if (s == 0x85) return;
    if (s < 0x86) {
        if (s == 10) return;
        if (10 < s) {
            if (s == 0x1c || s == 0x2a) return;
        } else if (s == 8) {
            return;
        }
    } else {
        if (s == 0xb4) return;
        if (s < 0xb4) {
            if (s == 0x89) return;
        } else if (s == 0xb6 || s == 0xb7) {
            return;
        }
    }
    d->initial_state = 0x56;
}

// [decomp] 0x0014cbd8 NDrone2_GetDroneTypeAttackTypeFriend
// Tipos (base, normal, de ataque) da estrutura de comportamento e os estados que eles dão.
void NDrone2_GetDroneTypeAttackTypeFriend(Drone_tag* d) {
    if (d->behaviour_attack == d->behaviour_friend || d->channel_state == 0x65) d->channel_state = 0;
    d->base_type = static_cast<uint8_t>(NDrone2_GetDTYPENEW(d, d->behaviour_type, 0));
    d->type = static_cast<uint8_t>(NDrone2_GetDTYPENEW(d, d->behaviour_type, d->behaviour_attack));
    if (d->channel_state == 0 || d->channel_state == 0x65) {
        d->attack_type = 0;
    } else if (d->channel_b == 0 && 1.0f <= d->alert_base) {
        d->attack_type = 0;
    } else {
        d->attack_type = static_cast<uint8_t>(NDrone2_GetDTYPENEW(d, d->behaviour_type, d->behaviour_friend));
    }
    switch (d->behaviour_type) {
        case 0xa: case 0xb: case 0xd: case 0xe: d->side = 3; break;
        case 0xc: d->side = 2; break;
        default: d->side = 1; break;
    }
    if (d->obj != nullptr) d->obj->state = 0;
    bool keep = false;
    if (DroneTypeSettings[d->type].initial_state < 0) {
        d->initial_state = 4;
        uint16_t s = 4;
        switch (d->type) {
            case 2:
                // Peculiaridade mantida: o 0x29 é sobrescrito logo abaixo.
                if (d->alert < 1.0f) d->initial_state = 0x29;
                s = 0x2a;
                break;
            default:
                if (behaviour_util_getProperty(0x24, d->behaviour) != 0 ||
                    behaviour_util_getProperty(0x25, d->behaviour) != 0) {
                    s = 6;
                } else {
                    keep = true;
                }
                break;
            case 4: s = 8; break;
            case 7: s = 0x85; break;
            case 0x10: s = 0x37; break;
            case 0x13: s = 0x30; break;
            case 0x16: s = 0x31; break;
            case 0x18: s = 0x36; break;
        }
        if (!keep) d->initial_state = s;
    } else {
        d->initial_state = static_cast<uint16_t>(DroneTypeSettings[d->type].initial_state);
    }
    apply_attack_state(d);
    if (DroneTypeSettings[d->type].init != nullptr) DroneTypeSettings[d->type].init(d);
    if (1.0f <= d->alert) {
        if (d->klass == 0xb) {
            NDrone2_ChangeToAttackMode(d);
            d->health = 100.0f;
            return;
        }
        switch (static_cast<int16_t>(d->initial_state - 4)) {
            case 0: case 1: case 2: case 0x2c: case 0x2d: case 0x32: case 0x52: d->initial_state = 0x56; break;
            case 0xc: case 0xe: case 0x2a: case 0x2e: case 0xb1: d->initial_state = 0x12; break;
            case 0x25: case 0x26: d->initial_state = 0x2a; break;
            case 0xa2: case 0xa3: d->initial_state = 0xa7; break;
            default: break;
        }
    }
    int16_t s4 = static_cast<int16_t>(d->attack_state);
    if (d->attack_type == 0) {
        if (s4 != 0) {
            if (d->channel_state == 0) d->channel_state = 100;
            return;
        }
        if (d->channel_b == 0 && d->alert_attack < 1.0f) return;
        uint16_t s;
        switch (static_cast<int16_t>(d->initial_state - 4)) {
            case 0: case 1: case 2: case 4: case 0x2c: case 0x2d: case 0x32: case 0x33: case 0x81: case 0x85:
                s = 0x56;
                break;
            default: s = 0; break;
            case 0xc: case 0x2a: case 0x2e: s = 0x12; break;
            case 0x25: s = 0x2a; break;
            case 0xa2: s = 0xa7; break;
        }
        d->attack_state = s;
        s4 = static_cast<int16_t>(s);
    }
    if (s4 == 0) return;
    if (d->channel_state == 0) d->channel_state = 100;
}

// [decomp] 0x0014c938 NDrone2_GetDTYPENEW
// O tipo (DTYPE) para um tipo de comportamento e um modo (0 normal, ou o de ataque/amigo).
uint32_t NDrone2_GetDTYPENEW(Drone_tag* d, uint32_t behaviour_type, uint32_t attack) {
    if (attack == 0 && behaviour_util_getProperty(0x10, d->behaviour) != 0) return 0x10;
    const uint32_t level = GameState.level_file;
    switch (behaviour_type) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
            if (level == 0x7000007 && attack == 0) {
                return behaviour_util_getProperty(0x4b, d->behaviour) == 0 ? 0x13 : 0x16;
            }
            break;
        case 10:
        case 0xb:
            if (attack == 0) {
                if (level == 0x7000007) {
                    behaviour_util_setProperty(0x1f, d->behaviour, 0);
                    behaviour_util_setProperty(0x54, d->behaviour, 1);
                }
                if (d->alert < 1.0f) return d->unknown_15 != 0 ? 0x12 : 9;
                return 10;
            }
            break;
        case 0xc:
            if (attack == 0) return d->klass == 0xc ? 0x11 : 0xc;
            break;
        case 0xd:
            if (attack == 0) {
                if (d->klass == 0xf) return 0x17;
                if (level == 0x7000007) behaviour_util_setProperty(0x1f, d->behaviour, 0);
                return d->alert < 1.0f ? 9 : 10;
            }
            break;
        case 0xe:
            if (attack == 0) {
                if (behaviour_util_getProperty(0x4e, d->behaviour) != 0) return 5;
                return 1.0f <= d->alert ? 10 : 9;
            }
            break;
        case 0xf:
            if (attack == 0) return 0xd;
            break;
        case 0x10:
            return 0x1e;
        default:
            break;
    }
    switch (attack) {
        case 2: return 1.0f <= d->alert ? 0x19 : 2;
        case 3: return behaviour_util_getProperty(0x56, d->behaviour) != 0 ? 0xe : 7;
        case 4: return 4;
        case 5: return 0x15;
        default: return 0;
    }
}

// [decomp] 0x0014a648 NDrone2_ChangeToAttackMode
// Passa ao tipo de ataque (o atirador 2 vira 0x19 e entra em 0x56); o modo 100 troca as propriedades
// pelas do modo de ataque.
int16_t NDrone2_ChangeToAttackMode(Drone_tag* d) {
    int16_t s = static_cast<int16_t>(d->attack_state);
    if (s == 0) return s;
    if (d->attack_type != 0) d->type = d->attack_type;
    if (d->type == 2) {
        d->type = 0x19;
        if (DroneTypeSettings[0x19].init != nullptr) DroneTypeSettings[0x19].init(d);
        s = 0x56;
    }
    d->attack_type = 0;
    d->attack_state = 0;
    if (d->divars.params[5] == 100) {
        d->behaviour = d->behaviour_b;
        d->unknown_29 = 1;
    }
    d->unknown_28 = 1;
    return s;
}

// ---- inicialização por modo e por tipo ----

// [decomp] 0x0014d528 NDrone2_init_DMODE_Defaults
void NDrone2_init_DMODE_Defaults(Drone_tag* d) {
    set_props(d, {1, 6, 7, 10, 0xc, 0x48, 0xf, 0x11, 0x13, 0x18, 0x1d, 0x4c, 0x21, 0x29, 0x2e, 0x37, 0x41, 0x42, 0x4f,
                  0x51, 0x5a, 0x24, 0x3c, 0x1f, 0x44, 0x33, 0x32, 0x31, 0x28, 0x27, 0x26, 0x30, 0x2f, 0x2a, 0x43, 0x3b,
                  10, 0xc, 0x17, 0x46});
}
// [decomp] 0x0014d958 NDrone2_init_DMODE_Normal
void NDrone2_init_DMODE_Normal(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014d978 NDrone2_init_DMODE_Guard
void NDrone2_init_DMODE_Guard(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014d998 NDrone2_init_DMODE_Retreater
void NDrone2_init_DMODE_Retreater(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014d9b8 NDrone2_init_DMODE_Sniper
void NDrone2_init_DMODE_Sniper(Drone_tag* d) {
    set_props(d, {0x3c, 0x1f, 0x33, 0x32, 0x31, 0x43, 0x17, 0x39, 0x40, 0x5a});
}
// [decomp] 0x0014da78 NDrone2_init_DMODE_Stealth
void NDrone2_init_DMODE_Stealth(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    behaviour_util_setProperty(0x25, d->behaviour, 1);
}
// [decomp] 0x0014dab0 NDrone2_init_DMODE_Attacker
void NDrone2_init_DMODE_Attacker(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014dad0 NDrone2_init_DMODE_RunToPoint
void NDrone2_init_DMODE_RunToPoint(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    set_props(d, {0x23, 0x58});
}
// [decomp] 0x0014db18 NDrone2_init_DMODE_Assassin
void NDrone2_init_DMODE_Assassin(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014db38 NDrone2_init_DMODE_HostageKiller
void NDrone2_init_DMODE_HostageKiller(Drone_tag* d) {
    set_props(d, {0x3c, 0x1f, 0x33, 0x32, 0x31, 0x29, 0x28, 0x27, 0x26, 0x43, 0x40, 0x22});
}
// [decomp] 0x0014dc18 NDrone2_init_DMODE_Hostage
void NDrone2_init_DMODE_Hostage(Drone_tag* d) { set_props(d, {0x24, 0x3c, 0x1f, 0x40, 0x4e}); }
// [decomp] 0x0014dc88 NDrone2_init_DMODE_HostageTied
void NDrone2_init_DMODE_HostageTied(Drone_tag* d) { set_props(d, {0x24, 0x3c, 0x1f, 0x40, 0x4e}); }
// [decomp] 0x0014dcf8 NDrone2_init_DMODE_JustStand4Demo
void NDrone2_init_DMODE_JustStand4Demo(Drone_tag* d) { behaviour_util_setProperty(0x40, d->behaviour, 1); }
// [decomp] 0x0014dd20 NDrone2_init_DMODE_DeleteMe
void NDrone2_init_DMODE_DeleteMe(Drone_tag*) {}
// [decomp] 0x0014dd28 NDrone2_init_DMODE_Civilian
void NDrone2_init_DMODE_Civilian(Drone_tag* d) {
    set_props(d, {0x1d, 0x24, 0x3c, 0x1f, 0x29, 0x46, 0x32, 0x31, 0x27, 0x26, 0x3b, 0x4c, 0x4f, 0x50, 0x51, 0x53});
}
// [decomp] 0x0014de48 NDrone2_init_DMODE_CivilianScared
void NDrone2_init_DMODE_CivilianScared(Drone_tag* d) {
    set_props(d, {0x3c, 0x1f, 0x32, 0x31, 0x27, 0x26, 0x3b, 0x4c, 0x4f, 0x50, 0x51, 0x53});
}
// [decomp] 0x0014df28 NDrone2_init_DMODE_MissionFailer
void NDrone2_init_DMODE_MissionFailer(Drone_tag* d) { set_props(d, {0x3c, 0x55}); }
// [decomp] 0x0014df68 NDrone2_init_DMODE_Mayhew
void NDrone2_init_DMODE_Mayhew(Drone_tag* d) { behaviour_util_setProperty(0x1d, d->behaviour, 1); }
// [decomp] 0x0014df90 NDrone2_init_DMODE_Ninja
void NDrone2_init_DMODE_Ninja(Drone_tag*) {}
// [decomp] 0x0014df98 NDrone2_init_DMODE_AlarmRaiser
void NDrone2_init_DMODE_AlarmRaiser(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    set_props(d, {0x23, 0x56});
}
// [decomp] 0x0014dfe0 NDrone2_init_DMODE_SearchLight
void NDrone2_init_DMODE_SearchLight(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    behaviour_util_setProperty(0x40, d->behaviour, 1);
}
// [decomp] 0x0014e018 NDrone2_init_DMODE_Ambush
void NDrone2_init_DMODE_Ambush(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    behaviour_util_setProperty(0x10, d->behaviour, 1);
}
// [decomp] 0x0014e050 NDrone2_init_DMODE_Zoe
void NDrone2_init_DMODE_Zoe(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    behaviour_util_setProperty(0x40, d->behaviour, 1);
    behaviour_util_setProperty(0x21, d->behaviour, 0);
}
// [decomp] 0x0014e098 NDrone2_init_DMODE_PartyGirl
void NDrone2_init_DMODE_PartyGirl(Drone_tag* d) {
    set_props(d, {0x1d, 0x24, 0x3c, 0x1f, 0x46, 0x26, 0x27, 0x28, 0x29, 0x51});
}
// [decomp] 0x0014e158 NDrone2_init_DMODE_CivilianGuard
void NDrone2_init_DMODE_CivilianGuard(Drone_tag* d) {
    set_props(d, {0x1d, 0x24, 0x3c, 0x1f, 0x46, 0x26, 0x27, 0x28, 0x29, 0x4f, 0x50, 0x51});
}
// [decomp] 0x0014e238 NDrone2_init_DMODE_Interogator
void NDrone2_init_DMODE_Interogator(Drone_tag* d) { NDrone2_init_DMODE_Defaults(d); }
// [decomp] 0x0014e258 NDrone2_init_DMODE_CivDoorGuard
void NDrone2_init_DMODE_CivDoorGuard(Drone_tag* d) {
    set_props(d, {0x1d, 0x18, 6, 0x3c, 0x1f, 0x46, 0x26, 0x27, 0x28, 0x29, 0x4f, 0x50, 0x51});
}
// [decomp] 0x0014e348 NDrone2_init_DMODE_TruckDriver
void NDrone2_init_DMODE_TruckDriver(Drone_tag* d) { behaviour_util_setProperty(0x1d, d->behaviour, 1); }
// [decomp] 0x0014e370 NDrone2_init_DMODE_CastleChatGuard1
void NDrone2_init_DMODE_CastleChatGuard1(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    d->chat_type = 0x19;
}
// [decomp] 0x0014e3a0 NDrone2_init_DMODE_CastleChatGuard2
void NDrone2_init_DMODE_CastleChatGuard2(Drone_tag* d) {
    NDrone2_init_DMODE_Defaults(d);
    behaviour_util_setProperty(0x1d, d->behaviour, 1);
    d->chat_type = 0x11;
}
// [decomp] 0x0014e3e0 NDrone2_init_DMODE_SniperAlert
void NDrone2_init_DMODE_SniperAlert(Drone_tag* d) {
    set_props(d, {0x3c, 0x1f, 0x33, 0x32, 0x31, 0x43, 0x17, 0x39, 0x40});
}
// [decomp] 0x0014e490 NDrone2_init_DMODE_PartyGirlLooker
void NDrone2_init_DMODE_PartyGirlLooker(Drone_tag* d) {
    set_props(d, {0x1d, 0x24, 0x3c, 0x1f, 0x46, 0x47, 0x26, 0x27, 0x28, 0x29, 0x51});
    d->head_mode = 2;
}
// [decomp] 0x0014e568 NDrone2_init_DMODE_Bot
void NDrone2_init_DMODE_Bot(Drone_tag*) {}
// [decomp] 0x0014e570 NDrone2_initDTYPE_Sniper
void NDrone2_initDTYPE_Sniper(Drone_tag* d) { d->flags |= 0x10; }
// [decomp] 0x0014e580 NDrone2_initDTYPE_SniperAlert
void NDrone2_initDTYPE_SniperAlert(Drone_tag* d) {
    DCVars_tag dcv;
    Drone_DCVfromOBJ(d->obj, &dcv);
    d->flags |= 0x10;
}
// [decomp] 0x001291e8 NDrone2_initDTYPE_BotInit
void NDrone2_initDTYPE_BotInit(Drone_tag*) {}
