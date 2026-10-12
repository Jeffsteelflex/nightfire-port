// O dossiê do menu principal: a página com a roda das quatro seções, os recordes e as recompensas de cada missão, e
// as rodas dos aparelhos e das armas com as melhorias ganhas. Ver menu_dossier.h.
//
// Divergências: nenhuma de comportamento. Só do port: MenuDossier_Describe (o relatório das capturas); Txt_BindLabel
// nulo (um rótulo fora da tabela de textos) vira "" nas cópias de texto e uma SCORETABLE sem linhas conta 0 momentos
// Bond, onde o original leria de um ponteiro nulo.

#include "engine/menu_dossier.h"

#include <cstdio>
#include <cstring>

#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/mission.h"
#include "engine/player_stats.h"
#include "engine/txt.h"

M_ITEM ds_options[4];
M_ITEM ds_weapons[27];
M_ITEM ds_gadgets[14];

namespace {

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }
template <class T>
T* ptr(int32_t v) {
    return reinterpret_cast<T*>(uintptr_t(uint32_t(v)));
}
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }
M_ITEM* sp_items() { return reinterpret_cast<M_ITEM*>(sp_level); }
uint64_t* bonus_of(uint8_t player) { return reinterpret_cast<uint64_t*>(menu_bonus) + player; }
const char* text_of(uint32_t label) {
    const char* s = Txt_BindLabel(label, 0);
    return s != nullptr ? s : "";
}

// Os textos das páginas (buf.369 0x0034d648: o recorde, os momentos Bond e as melhorias; ug_buf.355 0x0034d248 e
// ug_buf.362 0x0034d448: a descrição da arma e do aparelho com a melhoria).
char buf_369[3][0x20];
char ug_buf_355[0x200];
char ug_buf_362[0x200];
char kFormatD[] = "%d";        // 0x0030d2f0
char kFormatTwo[] = "%s\n%s";  // 0x0030d358

// A descrição do item com a linha da melhoria (o "%s\n%s" do original) em `out`.
void append_upgrade(char* out, size_t size, uint32_t label) {
    char tmp[0x100];
    std::snprintf(tmp, sizeof(tmp), "%s", out);
    std::snprintf(out, size, kFormatTwo, tmp, text_of(label));
}

}  // namespace

// [decomp] 0x00216de0 P_DOSSIER_Handler
// A página do dossiê: entrando do mapa das missões, do resultado ou do bônus (b = a página de antes), o íris abre e
// a roda volta ao começo; voltando de uma seção, o íris fica parado. A cada tick o íris anda.
int32_t P_DOSSIER_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t b) {
    if (message == 0x4c) {
        const uint32_t from = uint32_t(b);
        if (from == 0x4000001c || from == 0x40000036 || from == 0x40000038) {
            Menu_StartIris(0, index, 0x1000010b);
            __Menu_Send(index, 0x1000010c, 0x2e, 0, 0);
        } else {
            Menu_StartIris(4, index, 0x1000010b);
        }
    } else if (message == 0x50) {
        Menu_PlayIris(1, index, 0x1000010b);
    }
    return 1;
}

// [decomp] 0x00216eb0 C_SBDOSSIER_Handler
// A roda do dossiê (ds_options): escolher os recordes ou as recompensas mostra a página na hora; os aparelhos e as
// armas, depois do íris fechar.
int32_t C_SBDOSSIER_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    switch (message) {
        case 0x4b: {
            const int32_t v = __Menu_SendMessage(c, 0x40, 0, 0);
            if (v == 0) {
                Manager_SendMessage(mgr(index), 0x44, 0x4000003a, 0);
            } else if (v == 1) {
                Manager_SendMessage(mgr(index), 0x44, 0x4000003b, 0);
            } else if (v == 2) {
                Menu_ChangePageCloseIris(0x4000003c, index, 0x1000010b);
            } else if (v == 3) {
                Menu_ChangePageCloseIris(0x4000003d, index, 0x1000010b);
            }
            return 1;
        }
        case 0x51:
            __Menu_SendMessage(c, 0x27, 0, 3);
            return 1;
        case 0x49:
        case 0x54:
            Menu_UpdateWheel(index, c, ds_options, 0x1000010d, 0x1000010a, 0x100001ed, 0x1000010b, message == 0x49 ? 1 : 0);
            return 1;
        default:
            return 1;
    }
}

// [decomp] 0x00217248 P_DSRECORDS_Handler
// Os recordes: o rádio com as 12 missões (todas, liberadas ou não) na última jogada (GameState +0x48).
int32_t P_DSRECORDS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        M_CONTROL* radio = ptr<M_CONTROL>(__Menu_Send(index, 0x10000174, 0x39, 0, 0));
        Menu_AddItemsToControl(radio, sp_items(), 0xc, 0, 1);
        __Menu_SendMessage(radio, 0x1e, int32_t(GameState.unknown_44[1]), 0);
    }
    return 1;
}

// [decomp] 0x0020bf00 C_RBDSRECORDS_Handler
// A missão escolhida nos recordes: o recorde (SCORETABLE::best) e os momentos Bond (a linha 0) da ScoringTable, as
// melhorias (recompensas de tipo 4) ganhas nela e a melhor medalha, com a imagem; sem medalha, o rótulo e a imagem
// somem.
int32_t C_RBDSRECORDS_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x49) return 1;
    const uint32_t level = uint32_t(__Menu_SendMessage(c, 0x35, 0, 0));
    const SCORETABLE* totals = PlrStats_GetLevelTotals(level);
    const uint8_t* medals = Menu_GetLevelBonuses(level);
    if (totals == nullptr) {
        std::snprintf(buf_369[0], sizeof(buf_369[0]), kFormatD, 0);
        std::snprintf(buf_369[1], sizeof(buf_369[1]), kFormatD, 0);
    } else {
        std::snprintf(buf_369[0], sizeof(buf_369[0]), kFormatD, int32_t(totals->best));
        // Só do port: uma tabela sem as linhas (testes) conta 0, onde o original leria de nulo.
        const uint32_t moments = totals->rows != nullptr ? totals->rows[kScoreBondMoments].actual : 0;
        std::snprintf(buf_369[1], sizeof(buf_369[1]), kFormatD, int32_t(moments));
    }
    __Menu_Send(index, 0x10000175, 0x18, val(buf_369[0]), 0);
    __Menu_Send(index, 0x10000177, 0x18, val(buf_369[1]), 0);
    uint32_t upgrades = 0;
    REWARDINFO_tag info{};
    for (uint32_t medal = 1; medal < 5; ++medal) {
        PlrStarts_ProcessRewardCounter(bonus_of(0), level, medal, 0, &info);
        if (info.has != 0 && info.kind == 4) upgrades = (upgrades + 1) & 0xff;
    }
    std::snprintf(buf_369[2], sizeof(buf_369[2]), kFormatD, int32_t(upgrades));
    __Menu_Send(index, 0x10000178, 0x18, val(buf_369[2]), 0);
    // A melhor medalha: cada uma só conta com as de baixo (Menu_SetLevelBonus liga todas até a ganha).
    const bool bronze = medals[4] != 0, silver = medals[0x10] != 0, gold = medals[0x1c] != 0, platinum = medals[0x28] != 0;
    uint32_t label = 0;
    uint32_t image = 0;
    if (platinum && gold && silver && bronze) {
        label = 0x306, image = 0x300013f;
    } else if (gold && silver && bronze) {
        label = 0x307, image = 0x300013e;
    } else if (silver && bronze) {
        label = 0x308, image = 0x3000140;
    } else if (bronze) {
        label = 0x309, image = 0x300013d;
    }
    if (image == 0) {
        __Menu_Send(index, 0x100001f8, 0x2b, 1, 0);
        __Menu_Send(index, 0x10000176, 0x2b, 1, 0);
    } else {
        __Menu_Send(index, 0x100001f8, 0x2b, 2, 0);
        __Menu_Send(index, 0x10000176, 0x2b, 2, 0);
        __Menu_Send(index, 0x100001f8, 0x18, val(Txt_BindLabel(label, 0)), 0);
        __Menu_Send(index, 0x10000176, 0x24, int32_t(image), 0);
    }
    return 1;
}

// [decomp] 0x002172c8 P_DSREWARDS_Handler
// As recompensas: o mesmo rádio de missões, na última jogada.
int32_t P_DSREWARDS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        M_CONTROL* radio = ptr<M_CONTROL>(__Menu_Send(index, 0x1000017a, 0x39, 0, 0));
        Menu_AddItemsToControl(radio, sp_items(), 0xc, 0, 1);
        __Menu_SendMessage(radio, 0x1e, int32_t(GameState.unknown_44[1]), 0);
    }
    return 1;
}

// [decomp] 0x0020c248 C_RBDSREWARDS_Handler
// As quatro recompensas da missão escolhida (Menu_GetLevelBonuses: bronze, prata, ouro e platina), imagem e texto,
// escondidas até a medalha delas e todas as de baixo.
int32_t C_RBDSREWARDS_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message != 0x49 && message != 0x54) return 1;
    const uint32_t level = uint32_t(__Menu_SendMessage(c, 0x35, 0, 0));
    const uint8_t* b = Menu_GetLevelBonuses(level);
    const uint32_t images[4] = {0x100001d6, 0x100001d7, 0x100001d8, 0x100001d9};
    const uint32_t texts[4] = {0x100001d2, 0x100001d3, 0x100001d4, 0x100001d5};
    for (uint32_t i = 0; i < 4; ++i) {
        __Menu_Send(index, images[i], 0x2b, 1, 0);
        __Menu_Send(index, texts[i], 0x2b, 1, 0);
    }
    const auto show = [&](uint32_t i, uint32_t image, uint32_t label) {
        uint32_t img;
        uint32_t lab;
        std::memcpy(&img, b + image, 4);
        std::memcpy(&lab, b + label, 4);
        __Menu_Send(index, images[i], 0x2b, 2, 0);
        __Menu_Send(index, images[i], 0x24, int32_t(img), 0);
        __Menu_Send(index, texts[i], 0x2b, 2, 0);
        __Menu_Send(index, texts[i], 0x18, val(Txt_BindLabel(lab, 0)), 0);
    };
    const bool bronze = b[4] != 0, silver = b[0x10] != 0, gold = b[0x1c] != 0, platinum = b[0x28] != 0;
    if (bronze) show(0, 0xc, 8);
    if (silver && bronze) show(1, 0x18, 0x14);
    if (gold && silver && bronze) show(2, 0x24, 0x20);
    if (platinum && gold && silver && bronze) show(3, 0x30, 0x2c);
    return 1;
}

// [decomp] 0x00217190 P_DSGADGETS_Handler
// Os aparelhos: o íris abre e a roda volta ao começo; a imagem do barbeador (Shaver) é a da versão do vídeo (0x30000e5
// a 50 Hz, 0x300018f a 60). A cada tick o íris anda.
int32_t P_DSGADGETS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        Menu_StartIris(0, index, 0x1000016d);
        __Menu_Send(index, 0x1000016f, 0x2e, 0, 0);
        ds_gadgets[6].image = VIDEO_FRAME_RATE == 0x32 ? 0x30000e5u : 0x300018fu;
    } else if (message == 0x50) {
        Menu_PlayIris(1, index, 0x1000016d);
    }
    return 1;
}

// [decomp] 0x0020bcc0 C_SBDSGTSCROLL_Handler
// A roda dos aparelhos (ds_gadgets, 14): a imagem, os nomes e a descrição, e com uma melhoria do grupo do aparelho a
// linha dela embaixo (grapple, câmera, dardo, decodificador, stunner, laser).
int32_t C_SBDSGTSCROLL_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) return 1;
    if (message == 0x51) {
        __Menu_SendMessage(c, 0x27, 0, 0xd);
        __Menu_SendMessage(c, 0x2e, 0, 0);
        return 1;
    }
    if (message != 0x49 && message != 0x54) return 1;
    Menu_UpdateWheel(index, c, ds_gadgets, 0x1000016a, 0x1000016c, 0x1000016b, 0x1000016d, message == 0x49 ? 1 : 0);
    const int32_t pos = __Menu_SendMessage(c, 0x40, 0, 0);
    const uint32_t group = ds_gadgets[pos].value;
    const uint32_t level = Menu_GetObjectUpgradeLevel(group, 0);
    std::snprintf(ug_buf_362, sizeof(ug_buf_362), "%s", text_of(ds_gadgets[pos].text));
    if (level != 0) {
        switch (group) {
            case 3: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b3); break;
            case 6: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b4); break;
            case 0xc: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b5); break;
            case 0xf: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b6); break;
            case 0x12: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b7); break;
            case 0x15: append_upgrade(ug_buf_362, sizeof(ug_buf_362), 0x3b8); break;
            default: break;
        }
    }
    __Menu_Send(index, 0x1000016b, 0x18, val(ug_buf_362), 0);
    return 1;
}

// [decomp] 0x00217038 P_DSWEAPONS_Handler
// As armas: a pistola (o primeiro item) mostra o modelo do nível de melhoria dela (0 a 3: Wolfram PP7, com silenciador,
// ...), o íris abre e a roda volta ao começo. A cada tick o íris anda.
int32_t P_DSWEAPONS_Handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        switch (Menu_GetObjectUpgradeLevel(0, 0)) {
            case 0: ds_weapons[0].image = 0x30000f3, ds_weapons[0].name = 0x33d, ds_weapons[0].text = 0x359; break;
            case 1: ds_weapons[0].image = 0x300014f, ds_weapons[0].name = 0x3c5, ds_weapons[0].text = 0x3c6; break;
            case 2: ds_weapons[0].image = 0x30000f2, ds_weapons[0].name = 0x33e, ds_weapons[0].text = 0x35a; break;
            case 3: ds_weapons[0].image = 0x3000150, ds_weapons[0].name = 0x3c7, ds_weapons[0].text = 0x3c8; break;
            default: break;
        }
        Menu_StartIris(0, index, 0x1000016e);
        __Menu_Send(index, 0x10000173, 0x2e, 0, 0);
    } else if (message == 0x50) {
        Menu_PlayIris(1, index, 0x1000016e);
    }
    return 1;
}

// [decomp] 0x0020bab0 C_SBDSWPSCROLL_Handler
// A roda das armas (ds_weapons, 27): a imagem, os nomes e a descrição; o rifle de precisão (grupo 9) ganha a linha da
// melhoria 1 ou 2.
int32_t C_SBDSWPSCROLL_Handler(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4b) return 1;
    if (message == 0x51) {
        __Menu_SendMessage(c, 0x27, 0, 0x1a);
        __Menu_SendMessage(c, 0x2e, 0, 0);
        return 1;
    }
    if (message != 0x49 && message != 0x54) return 1;
    Menu_UpdateWheel(index, c, ds_weapons, 0x10000171, 0x10000170, 0x10000172, 0x1000016e, message == 0x49 ? 1 : 0);
    const int32_t pos = __Menu_SendMessage(c, 0x40, 0, 0);
    const uint32_t group = ds_weapons[pos].value;
    const uint32_t level = Menu_GetObjectUpgradeLevel(group, 0);
    std::snprintf(ug_buf_355, sizeof(ug_buf_355), "%s", text_of(ds_weapons[pos].text));
    if (level != 0 && group == 9) {
        if (level == 1) {
            append_upgrade(ug_buf_355, sizeof(ug_buf_355), 0x3b1);
        } else if (level == 2) {
            append_upgrade(ug_buf_355, sizeof(ug_buf_355), 0x3b2);
        }
    }
    __Menu_Send(index, 0x10000172, 0x18, val(ug_buf_355), 0);
    return 1;
}

// [decomp] 0x002025f8 Menu_GetObjectUpgradeLevel
// O nível de melhoria do grupo `id` (o bit base das recompensas de tipo 4): a contagem do grupo na primeira medalha de
// tipo 4 do grupo que o jogador tem, procurando missão a missão (sp_level) e medalha a medalha; 0 sem nenhuma ou com
// `id` fora dos 63 bits.
uint32_t Menu_GetObjectUpgradeLevel(uint32_t id, uint8_t player) {
    if (0x3e < id) return 0;
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t level = sp_items()[i].value;
        for (uint32_t medal = 1; medal < 5; ++medal) {
            REWARDINFO_tag info{};
            PlrStarts_ProcessRewardCounter(bonus_of(player), level, medal, 0, &info);
            if (info.has != 0 && info.kind == 4 && info.bit == id) return info.count;
        }
    }
    return 0;
}

// ---- só do port ----

int MenuDossier_Describe(char* out, size_t size) {
    const uint32_t page = Menu_CurrentPage(0);
    const auto wheel = [](uint32_t id) {
        M_CONTROL* c = ptr<M_CONTROL>(__Menu_Send(0, id, 0x39, 0, 0));
        return c != nullptr ? __Menu_SendMessage(c, 0x40, 0, 0) : -1;
    };
    const auto radio_level = [](uint32_t id) {
        M_CONTROL* c = ptr<M_CONTROL>(__Menu_Send(0, id, 0x39, 0, 0));
        return c != nullptr ? uint32_t(__Menu_SendMessage(c, 0x35, 0, 0)) : 0u;
    };
    const auto shown = [](uint32_t id) {
        M_CONTROL* c = ptr<M_CONTROL>(__Menu_Send(0, id, 0x39, 0, 0));
        return c != nullptr && (c->state & 1) == 0 ? 1 : 0;
    };
    switch (page) {
        case 0x4000002b:
            return std::snprintf(out, size, "dossie pagina %08x item %d", page, wheel(0x1000010c));
        case 0x4000003a:
            return std::snprintf(out, size, "dossie pagina %08x missao %08x recorde %s momentos %s melhorias %s medalha %d",
                                 page, radio_level(0x10000174), buf_369[0], buf_369[1], buf_369[2], shown(0x10000176));
        case 0x4000003b:
            return std::snprintf(out, size, "dossie pagina %08x missao %08x recompensas %d%d%d%d", page,
                                 radio_level(0x1000017a), shown(0x100001d6), shown(0x100001d7), shown(0x100001d8),
                                 shown(0x100001d9));
        case 0x4000003c:
            return std::snprintf(out, size, "dossie pagina %08x item %d melhoria %u", page, wheel(0x1000016f),
                                 wheel(0x1000016f) >= 0 ? Menu_GetObjectUpgradeLevel(ds_gadgets[wheel(0x1000016f)].value, 0) : 0);
        case 0x4000003d:
            return std::snprintf(out, size, "dossie pagina %08x item %d pistola %u", page, wheel(0x10000173),
                                 Menu_GetObjectUpgradeLevel(0, 0));
        default:
            return std::snprintf(out, size, "dossie fechado melhorias pistola %u rifle %u", Menu_GetObjectUpgradeLevel(0, 0),
                                 Menu_GetObjectUpgradeLevel(9, 0));
    }
}

void menu_dossier_debug_power_on() {
    std::memset(buf_369, 0, sizeof(buf_369));
    std::memset(ug_buf_355, 0, sizeof(ug_buf_355));
    std::memset(ug_buf_362, 0, sizeof(ug_buf_362));
}
