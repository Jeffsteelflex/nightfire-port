// Só do port: a ponte entre os menus Modern e os menus do jogo. Ver menu_modern.h. Nada aqui vem do ACTION.ELF: a
// leitura só olha os controles e as tabelas do jogo, e a escrita passa pelos caminhos dos próprios controles (o
// cursor do gerente, a mensagem 0x1d das listas e as ações do controle por um tick), como o mouse dos menus
// (engine/menu_mouse.cpp).

#include "engine/menu_modern.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/gamestate.h"
#include "engine/input.h"
#include "engine/menu.h"
#include "engine/menu_dossier.h"
#include "engine/menu_frontend.h"
#include "engine/mission.h"
#include "engine/player_stats.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/txt.h"
#include "platform/psi_sound.h"

MenuModernStats g_menu_modern_stats;

namespace {

std::vector<MmRequest> g_queue;

// MenuModern_CodenameOptionsBegin/End: o que as páginas de opções do codinome gravam.
struct OptionsPrint {
    PlayerInput_tag player{};
    uint32_t draw[3] = {};
    uint32_t music = 0, sfx = 0, mode = 0;
    bool valid = false;
};
OptionsPrint g_options_print;

OptionsPrint options_print() {
    OptionsPrint p;
    p.player = PlayerSetting[0];
    for (int i = 0; i < 3; ++i) p.draw[i] = DrawInfo[i];
    p.music = SFXMusicGetVolume();
    p.sfx = SFXGetVolume();
    p.mode = SFXGetMode();
    p.valid = true;
    return p;
}

M_PAGE* first_page(const M_MANAGER& m) { return static_cast<M_PAGE*>(ll_first(m.pages)); }
M_PAGE* next_page(M_PAGE* p) {
    return p->next != nullptr ? reinterpret_cast<M_PAGE*>(reinterpret_cast<uint8_t*>(p->next) - 0xa4) : nullptr;
}
M_CONTROL* first_control(M_PAGE* p) { return static_cast<M_CONTROL*>(ll_first(p->controls)); }

M_PAGE* find_page(const M_MANAGER& m, uint32_t id) {
    for (M_PAGE* p = first_page(m); p != nullptr; p = next_page(p)) {
        if (p->base.id == id) return p;
    }
    return nullptr;
}

// O controle da página pelo id, a tag (-1 qualquer) e o tipo (-1 qualquer).
M_CONTROL* find(M_PAGE* page, uint32_t id, int64_t tag = -1, int type = -1) {
    if (page == nullptr) return nullptr;
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->id != id) continue;
        if (tag >= 0 && c->tag != uint32_t(tag)) continue;
        if (type >= 0 && c->type != type) continue;
        return c;
    }
    return nullptr;
}

M_LISTBOX* find_list(M_PAGE* page, uint32_t id, int64_t tag) {
    return reinterpret_cast<M_LISTBOX*>(find(page, id, tag, MCT_LIST));
}

bool visible(const M_CONTROL* c) { return c != nullptr && (c->state & 1) == 0; }

const char* label_string(const M_CONTROL* c) {
    if (c == nullptr) return nullptr;
    if (c->type == MCT_LABEL) return reinterpret_cast<const M_LABEL*>(c)->string;
    if (c->type == MCT_BUTTON) return reinterpret_cast<const M_BUTTON*>(c)->label.string;
    if (c->type == MCT_MEMO) return reinterpret_cast<const M_MEMOBOX*>(c)->text;
    return nullptr;
}

// Um rótulo do *Txt.dat (vazio sem ele; o rótulo 0, a entrada 0 do arquivo, seria um buffer do heap de strings).
std::string text_of(uint32_t label) {
    if (label == kNoLabel || label == 0) return std::string();
    return MenuModern_Utf8(Txt_BindLabel(label, 0));
}

std::vector<MmItem> rows_of(M_LISTBOX* l, bool collapse) {
    std::vector<MmItem> out;
    if (l == nullptr) return out;
    for (const LIST_ROW* r = static_cast<const LIST_ROW*>(ll_first(l->rows)); r != nullptr; r = r->next) {
        if (r->continuation != 0 && !out.empty()) {
            out.back().text += " " + MenuModern_Utf8(r->cells[0].text, collapse);
            continue;
        }
        out.push_back(MmItem{MenuModern_Utf8(r->cells[0].text, collapse), r->data});
    }
    return out;
}

int selected_index(M_LISTBOX* l) { return l != nullptr ? List_SendMessage(l, 0x34, 0, 0) : 0; }

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool multiplayer() { return rd32(MPSettings + 0x180) != 0; }

int16_t player_of(const M_MANAGER* m) { return m->player < 0 || m->player > 3 ? 0 : m->player; }

// A página de baixo quando a caixa de mensagem está por cima (a anterior da transição ou o topo da pilha).
M_PAGE* base_page(M_MANAGER& m) {
    if (m.page == nullptr || (m.page->base.id != kMmBoxPage && m.page->base.id != kMmCardPage)) return m.page;
    if (m.previous != nullptr) return m.previous;
    if (Stack_IsEmpty(&m.stack) == 0) {
        M_CONTROL** e = reinterpret_cast<M_CONTROL**>(uintptr_t(uint32_t(Stack_Top(&m.stack))));
        if (e != nullptr && e[0] != nullptr && e[0]->type == MCT_PAGE) return reinterpret_cast<M_PAGE*>(e[0]);
    }
    return m.page;
}

// A missão: nome e descrição do menu (sp_level +4 e +8), a fase dentro dela e a dificuldade.
void mission_info(MmSnapshot& s) {
    const uint32_t i = Menu_GetLevelIndex(BaseMap);
    if (i < 12) {
        s.mission = text_of(rd32(sp_level + i * 0x18 + 4));
        s.mission_text = text_of(rd32(sp_level + i * 0x18 + 8));
    }
    for (const MISSIONDATA_tag& m : MissionData) {
        if (m.base_map != BaseMap || m.level == 0) continue;
        ++s.levels;
        if (m.order == ThisOrderNum) s.level = int(m.order) + 1;
    }
    // GameState +0x28: 1 Operative, 2 Agent, 3 00 Agent (a tabela `difficulty` do menu, +0xc).
    if (1 <= GameState.difficulty && GameState.difficulty <= 3) s.difficulty = text_of(0x14c + GameState.difficulty);
    s.current_label = text_of(0x2f4);
}

// Os objetivos da missão: os visíveis na ordem do jogo (Mission_ObjectiveState, a da lista da pausa) e depois os
// escondidos (estado 0 ou 1) de todas as fases da missão, sem texto.
void objectives(MmSnapshot& s) {
    if (BaseMap == 0xffffffffu) return;
    const int16_t n = Mission_NumVisObjectives();
    for (int16_t i = 0; i < n; ++i) {
        OBJ_STATE os{};
        os.text = kNoLabel;
        Mission_ObjectiveState(&os, uint16_t(i));
        MmObjective o;
        o.raw = os.state;
        o.state = MenuModern_ObjectiveState(os.state);
        o.guard = os.state == 2;
        o.label = text_of(os.label);
        o.text = text_of(os.text);
        o.status = text_of(os.status_label);
        s.objectives.push_back(o);
        if (o.state == MmObjState::Done) ++s.done;
        if (o.state == MmObjState::Failed) ++s.failed;
    }
    s.visible = n;
    for (const MISSIONDATA_tag& m : MissionData) {
        if (m.base_map != BaseMap || m.objectives == nullptr) continue;
        for (uint32_t k = 0; k < m.count; ++k) {
            const int32_t st = m.objectives[k].state;
            if (1 < st || st < 0) continue;
            MmObjective o;
            o.raw = st;
            o.guard = (m.objectives[k].flags & 2) != 0;
            s.objectives.push_back(o);
        }
    }
}

void pause_info(MmSnapshot& s, M_MANAGER& m) {
    M_PAGE* pp = find_page(m, kMmPausePage);
    if (pp == nullptr) return;
    for (const uint32_t tag : {kMmTabMission, kMmTabObjectives, kMmTabControls, kMmTabScore}) {
        s.tab_text[tag] = MenuModern_Utf8(label_string(find(pp, kMmPauseControls, tag, MCT_BUTTON)), true);
    }
    const M_CONTROL* f = pp->focus;
    if (f != nullptr && f->id == kMmPauseControls && f->type == MCT_BUTTON) s.tab = int(f->tag);
    M_LISTBOX* main = find_list(pp, kMmPauseControls, kMmTagMain);
    s.rows = rows_of(main, true);
    s.row = selected_index(main);
    // A confirmação de RESTART/QUIT: a lista 0xd aparece com a pergunta na 0xc (C_GCPAUSE_Handler).
    M_LISTBOX* confirm = find_list(pp, kMmPauseControls, kMmTagConfirm);
    if (pp == m.page && confirm != nullptr && visible(&confirm->base)) {
        s.dialog.open = true;
        s.dialog.title = MenuModern_Utf8(label_string(find(pp, kMmPauseControls, kMmTagQuestion, MCT_LABEL)));
        s.dialog.options = rows_of(confirm, true);
        s.dialog.focus = selected_index(confirm);
        s.dialog.list = kMmPauseControls;
        s.dialog.tag = kMmTagConfirm;
    }
    // Os controles: os estilos, o eixo Y e o mapa de botões que o jogo montou para o estilo.
    for (const MmItem& it : rows_of(find_list(pp, kMmPauseControls, kMmTagStyles), false)) s.styles.push_back(it.text);
    const PlayerInput_tag& p = PlayerSetting[player_of(&m)];
    s.style = p.style;
    s.inverted = p.invert_look != 0;
    for (M_CONTROL* c = first_control(pp); c != nullptr; c = c->next) {
        if (c->id == kMmPauseControls && c->tag == 0x10 && c->type == MCT_LABEL) {
            const std::string t = MenuModern_Utf8(label_string(c));
            if (!t.empty()) s.axis_label = t;
        }
    }
    s.axis_value = MenuModern_Utf8(label_string(find(pp, kMmPauseControls, kMmTagAxis, MCT_LABEL)));
    if (M_LISTBOX* b = find_list(pp, kMmPauseControls, kMmTagButtons)) {
        for (const LIST_ROW* r = static_cast<const LIST_ROW*>(ll_first(b->rows)); r != nullptr; r = r->next) {
            for (int k = 0; k < b->columns && k < 5; ++k) {
                const char* t = r->cells[k].text;
                if (t[0] != '~' || t[1] == 0) continue;
                MmButton bt;
                bt.symbol = t[1];
                bt.label = MenuModern_Utf8(t[2] == ' ' ? t + 3 : t + 2);
                s.buttons.push_back(bt);
            }
        }
    }
    // A aba SCORE: o placar no multijogador, as estatísticas da missão com um jogador (C_GCPAUSE_Handler, 0x4e).
    if (s.tab == int(kMmTabScore)) {
        if (M_LISTBOX* l = find_list(pp, kMmPauseControls, kMmTagObjectives)) {
            for (const LIST_ROW* r = static_cast<const LIST_ROW*>(ll_first(l->rows)); r != nullptr; r = r->next) {
                std::vector<std::string> cells;
                for (int k = 0; k < l->columns && k < 5; ++k) cells.push_back(MenuModern_Utf8(r->cells[k].text));
                s.score.push_back(cells);
            }
        }
    }
    mission_info(s);
    objectives(s);
}

void fail_info(MmSnapshot& s, M_MANAGER& m) {
    M_PAGE* fp = find_page(m, kMmFailPage);
    if (fp == nullptr) return;
    s.fail_title = MenuModern_Utf8(label_string(find(fp, 0x10000001, -1, MCT_LABEL)));
    M_LISTBOX* l = find_list(fp, kMmFailList, -1);
    s.fail_rows = rows_of(l, true);
    s.fail_row = selected_index(l);
    // O motivo: o rótulo de falha da missão (alarme, civis...), "Killed In Action" (o do resultado para o
    // estado 3) ou o primeiro objetivo fracassado.
    s.fail_reason = text_of(Mission_FailLabel());
    s.died = Mission_Status() == int32_t(kMissionDied);
    if (s.fail_reason.empty() && s.died) s.fail_reason = text_of(0x2fe);
    mission_info(s);
    objectives(s);
    if (s.fail_reason.empty()) {
        for (const MmObjective& o : s.objectives) {
            if (o.state == MmObjState::Failed && !o.label.empty()) {
                s.fail_reason = o.label;
                break;
            }
        }
    }
}

void results_info(MmSnapshot& s, M_MANAGER& m) {
    s.complete_title = text_of(0x2000006);
    s.results_title = text_of(0x2c5);
    if (M_PAGE* rp = find_page(m, kMmResultsPage)) {
        s.score_label = text_of(0x2cb);
        s.score_value = MenuModern_Utf8(label_string(find(rp, 0x10000167, -1, MCT_LABEL)));
        s.target_label = MenuModern_Utf8(label_string(find(rp, 0x1000002b, -1, MCT_LABEL)));
        s.target_value = MenuModern_Utf8(label_string(find(rp, 0x10000168, -1, MCT_LABEL)));
        s.medal = MenuModern_Utf8(label_string(find(rp, 0x100001d1, -1, MCT_MEMO)));
    }
    M_PAGE* cur = base_page(m);
    for (M_CONTROL* c = first_control(cur); c != nullptr; c = c->next) {
        if (c->id != 0x10000001 || c->type != MCT_LABEL) continue;
        const char* t = label_string(c);
        if (t != nullptr && std::strchr(t, '~') != nullptr) s.help = MenuModern_Utf8(t);
    }
    if (M_PAGE* sp = find_page(m, kMmStatsPage)) {
        if (M_LISTBOX* l = find_list(sp, 0x10000022, -1)) {
            bool head = true;
            for (const LIST_ROW* r = static_cast<const LIST_ROW*>(ll_first(l->rows)); r != nullptr; r = r->next) {
                std::vector<std::string> cells;
                for (int k = 0; k < 4; ++k) cells.push_back(MenuModern_Utf8(r->cells[k].text));
                if (head) {
                    s.stats_head = cells;
                    head = false;
                } else {
                    s.stats.push_back(cells);
                }
            }
        }
    }
    if (s.stats_head.empty()) s.stats_head = {text_of(0x1000204), text_of(0x10001e1), text_of(0x10001e2), text_of(0x163)};
    for (const uint32_t label : {0x165u, 0x15au, 0x15bu, 0x15cu, 0x16eu, 0x15du, 0x15eu, 0x15fu, 0x160u, 0x161u, 0x162u,
                                 0x164u, 0x163u}) {
        s.stats_names.push_back(text_of(label));
    }
    if (M_PAGE* bp = find_page(m, kMmBonusPage)) s.bonus = MenuModern_Utf8(label_string(find(bp, 0x100001d0, -1, MCT_MEMO)));
    if (BaseMap != 0xffffffffu) mission_info(s);
}

// ---- o menu principal ----

// O texto de um controle da página (o primeiro com o id e o tipo).
std::string text_of_control(M_PAGE* page, uint32_t id, int type) { return MenuModern_Utf8(label_string(find(page, id, -1, type))); }

// O texto de ajuda de um controle (help_table, como Page_SetHelpText: o id e o valor dele, ou qualquer valor).
std::string help_of(uint32_t id, uint32_t value) {
    for (int i = 0; i < kHelpEntries; ++i) {
        const uint32_t* e = &help_table[i * 3];
        if (e[0] == id && (e[1] == value || e[1] == 0xf0f0f0f0u)) return text_of(e[2]);
    }
    return std::string();
}

// A barra de ajuda da página: o rótulo 0x10000001/0x1000010e/0x10000220 com um "~".
std::string help_bar(M_PAGE* page) {
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->type != MCT_LABEL || (c->id != 0x10000001 && c->id != 0x1000010e && c->id != 0x10000220)) continue;
        const char* t = label_string(c);
        if (t != nullptr && std::strchr(t, '~') != nullptr) return MenuModern_Utf8(t);
    }
    return std::string();
}

// A roda da página: a barra de rolagem (posição e limites).
void wheel_of(MmFront& f, M_PAGE* page, uint32_t id) {
    f.wheel = id;
    if (const M_SCROLL* s = reinterpret_cast<const M_SCROLL*>(find(page, id, -1, MCT_SCROLL))) {
        f.pos = s->pos;
        f.min = s->min;
        f.max = s->max;
    }
}

// Os rótulos das medalhas (bronze, prata, ouro, platina: "BRONZE MEDAL"...), como o C_RBDSRECORDS_Handler.
constexpr uint32_t kMedalLabels[5] = {0, 0x309, 0x308, 0x307, 0x306};

// Um item de uma tabela M_ITEM do jogo; `map`: uma missão (a de carro, a melhor medalha e o recorde do perfil).
MmWheelItem item_of(const M_ITEM& it, bool map) {
    MmWheelItem w;
    w.name = text_of(it.name);
    w.enabled = it.enabled != 0 || menu_unlock_everything != 0;
    w.text = text_of(w.enabled ? it.text : it.locked);
    if (w.text.empty()) w.text = text_of(it.text);
    w.value = it.value;
    w.image = it.image;
    if (map) {
        w.driving = Menu_IsDrivingLevel(it.value) != 0;
        // A melhor medalha como o dossiê a mostra (C_RBDSRECORDS_Handler): cada uma só conta com todas as de baixo.
        if (const uint8_t* m = Menu_GetLevelBonuses(it.value)) {
            const bool bronze = m[4] != 0, silver = m[0x10] != 0, gold = m[0x1c] != 0, platinum = m[0x28] != 0;
            if (platinum && gold && silver && bronze) w.medal = 4;
            else if (gold && silver && bronze) w.medal = 3;
            else if (silver && bronze) w.medal = 2;
            else if (bronze) w.medal = 1;
            w.medal_label = text_of(kMedalLabels[w.medal]);
        }
        if (const SCORETABLE* t = PlrStats_GetLevelTotals(it.value)) w.best = t->best;
    }
    return w;
}

// Os itens de uma tabela M_ITEM do jogo (sp_level, difficulty, cn_options, ds_options, ds_gadgets, ds_weapons).
void items_of(MmFront& f, const M_ITEM* items, uint32_t count, bool map) {
    for (uint32_t i = 0; i < count; ++i) f.items.push_back(item_of(items[i], map));
}

// Os nomes da roda de codinomes (codename_buf, Menu_UpdateCodenameWheel: a descrição de criar, do padrão e dos do
// cartão).
void codename_items(MmFront& f) {
    const uint32_t n = std::min<uint32_t>(menu_ls().count, uint32_t(kCodenames));
    for (uint32_t i = 0; i < n; ++i) {
        MmWheelItem w;
        w.name = MenuModern_Utf8(codename_buf[i]);
        w.text = text_of(i == 0 ? 0x1000004 : (i == 1 ? 0x1000003 : 0x1000005));
        w.value = i;
        w.image = i == 0 ? 0x300013b : 0x3000135;
        f.items.push_back(w);
    }
    f.listing = menu_ls().active != 0 && menu_ls().op == 2;
}

// ---- o dossiê e os créditos ----

int dossier_section(uint32_t page) {
    switch (page) {
        case kMmFeRecords: return 0;
        case kMmFeRewards: return 1;
        case kMmFeGadgets: return 2;
        case kMmFeWeapons: return 3;
        default: return -1;
    }
}

// Um rótulo da página pelo rótulo do *Txt.dat que ele mostra (os nomes das linhas dos recordes são todos 0x10000001).
const M_LABEL* label_with_text(M_PAGE* page, uint32_t label) {
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->type == MCT_LABEL && reinterpret_cast<const M_LABEL*>(c)->label == label) return reinterpret_cast<const M_LABEL*>(c);
    }
    return nullptr;
}

std::string label_or_text(M_PAGE* page, uint32_t label) {
    const M_LABEL* l = label_with_text(page, label);
    return l != nullptr ? MenuModern_Utf8(l->string) : text_of(label);
}

// O título da página (o primeiro rótulo 0x10000001 com texto que não é a barra de ajuda).
std::string page_title(M_PAGE* page) {
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->type != MCT_LABEL || c->id != 0x10000001) continue;
        const char* t = label_string(c);
        if (t == nullptr || t[0] == 0 || std::strchr(t, '~') != nullptr) continue;
        std::string u = MenuModern_Utf8(t);
        if (u.find_first_not_of(' ') != std::string::npos) return u;
    }
    return std::string();
}

// O texto que o tratador pôs num rótulo visível ("" escondido).
std::string shown_text(M_PAGE* page, uint32_t id) {
    const M_CONTROL* c = find(page, id, -1, MCT_LABEL);
    return visible(c) ? MenuModern_Utf8(label_string(c)) : std::string();
}

// A textura que o tratador pôs num rótulo de imagem visível (0 escondido).
uint32_t shown_image(M_PAGE* page, uint32_t id) {
    const M_CONTROL* c = find(page, id, -1, MCT_LABEL);
    return visible(c) ? reinterpret_cast<const M_LABEL*>(c)->image : 0;
}

// As missões do rádio dos recordes ou das recompensas: as opções dele (o nome do jogo e o valor, a fase) com o resto do
// item de sp_level (a imagem, a melhor medalha, o recorde), e a opção escolhida.
void mission_radio(MmFront& f, M_PAGE* page, uint32_t id) {
    f.wheel = id;
    const M_RADIO* r = reinterpret_cast<const M_RADIO*>(find(page, id, -1, MCT_RADIO));
    if (r == nullptr) return;
    const M_ITEM* sp = reinterpret_cast<const M_ITEM*>(sp_level);
    for (const LIST_ROW* o = static_cast<const LIST_ROW*>(ll_first(r->options)); o != nullptr; o = o->next) {
        MmWheelItem w;
        for (uint32_t i = 0; i < 12; ++i) {
            if (sp[i].value == uint32_t(o->data)) w = item_of(sp[i], true);
        }
        w.name = MenuModern_Utf8(o->cells[0].text);
        w.value = uint32_t(o->data);
        w.enabled = true;  // os recordes e as recompensas mostram todas as missões
        f.items.push_back(w);
    }
    f.pos = r->scroll.pos;
    f.min = 0;
    f.max = std::max(0, int(r->options.count) - 1);
}

void dossier_info(MmFront& f, M_PAGE* page) {
    MmDossier& d = f.dossier;
    d.section = dossier_section(page->base.id);
    for (const M_ITEM& it : ds_options) d.sections.push_back(item_of(it, false));
    f.title = page_title(page);
    switch (d.section) {
        case -1:
            wheel_of(f, page, kMmDsWheel);
            d.section_pos = f.pos;
            f.desc = text_of_control(page, 0x100001ed, MCT_MEMO);
            break;
        case 0: {
            mission_radio(f, page, kMmDsRecordsRadio);
            // Os rótulos das linhas (os do jogo na página) e os valores que o C_RBDSRECORDS_Handler escreveu.
            d.score_label = label_or_text(page, 0x302);
            d.moments_label = label_or_text(page, 0x165);
            d.upgrades_label = label_or_text(page, 0x305);
            d.score = shown_text(page, 0x10000175);
            d.moments = shown_text(page, 0x10000177);
            d.upgrades = shown_text(page, 0x10000178);
            d.medal = shown_text(page, 0x100001f8);
            d.medal_image = shown_image(page, 0x10000176);
            break;
        }
        case 1: {
            mission_radio(f, page, kMmDsRewardsRadio);
            for (uint32_t i = 0; i < 4; ++i) {
                MmReward& r = d.rewards[i];
                r.image = shown_image(page, 0x100001d6 + i);
                r.shown = r.image != 0;
                r.text = r.shown ? shown_text(page, 0x100001d2 + i) : std::string();
                r.medal = text_of(kMedalLabels[i + 1]);
            }
            break;
        }
        case 2:
        case 3: {
            const bool gadgets = d.section == 2;
            wheel_of(f, page, gadgets ? kMmDsGadgetWheel : kMmDsWeaponWheel);
            const M_ITEM* items = gadgets ? ds_gadgets : ds_weapons;
            const uint32_t n = gadgets ? 14u : 27u;
            items_of(f, items, n, false);
            for (uint32_t i = 0; i < n; ++i) d.upgrade.push_back(Menu_GetObjectUpgradeLevel(items[i].value, 0));
            d.memo = text_of_control(page, gadgets ? 0x1000016b : 0x10000172, MCT_MEMO);
            f.desc = d.memo;
            break;
        }
        default: break;
    }
}

void credits_info(MmFront& f, M_PAGE* page) {
    MmCredits& cr = f.credits;
    for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
        if (c->type != MCT_LABEL || (c->id != kMmCreditNames && c->id != kMmCreditTitles) || !visible(c)) continue;
        const M_LABEL* l = reinterpret_cast<const M_LABEL*>(c);
        std::string t = MenuModern_Utf8(l->string);
        const size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
        if (a == std::string::npos) continue;
        MmCreditLine line;
        line.text = t.substr(a, b - a + 1);
        line.x = int16_t(page->base.x + c->x);
        line.y = int16_t(page->base.y + c->y);
        line.w = c->w;
        line.right = c->id == kMmCreditTitles;
        line.wide = !line.right && c->w > 0x100;
        // O formato do P_CREDITS_Handler: 0xff e a fonte, 0xfe e o alinhamento.
        if (uint8_t(l->format_buf[0]) == 0xff) line.font = uint8_t(l->format_buf[1]);
        cr.lines.push_back(line);
    }
    std::sort(cr.lines.begin(), cr.lines.end(), [](const MmCreditLine& a, const MmCreditLine& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    // O escurecer do fim: o rótulo de tela cheia 0x10000215, que o processo 0xe1 leva do alfa 0 ao 0xff.
    if (const M_CONTROL* c = find(page, kMmCreditFade, -1, MCT_LABEL)) {
        cr.fade = float(reinterpret_cast<const M_LABEL*>(c)->text.colour & 0xff) / 255.0f;
    }
}

void front_info(MmSnapshot& s, M_MANAGER& m, M_PAGE* page) {
    MmFront& f = s.front;
    f.view = MenuModern_FrontView(page->base.id);
    f.help = help_bar(page);
    f.profile = MenuModern_Utf8(menu_ls().name);
    f.slot = menu_ls().slot;
    switch (f.view) {
        case MmFrontView::Title:
            f.press = text_of_control(page, 0x100000fa, MCT_LABEL);
            f.legal = text_of_control(page, 0x10000197, MCT_MEMO);
            break;
        case MmFrontView::Main:
            for (const uint32_t id : kMmFeMainButtons) {
                f.buttons.push_back(text_of_control(page, id, MCT_BUTTON));
                f.button_help.push_back(help_of(id, 0));
                if (page->focus != nullptr && page->focus->id == id) f.focus = int(f.buttons.size()) - 1;
            }
            f.desc = text_of_control(page, 0x10000225, MCT_MEMO);
            break;
        case MmFrontView::Campaign:
            f.title = text_of_control(page, 0x10000001, MCT_LABEL);
            if (f.title.empty() || f.title.find('~') != std::string::npos) f.title = text_of(0x1f2);
            wheel_of(f, page, kMmFeWheelCampaign);
            f.desc = text_of_control(page, 0x100001f2, MCT_MEMO);
            codename_items(f);
            break;
        case MmFrontView::Codenames:
            f.title = text_of_control(page, 0x10000029, MCT_LABEL);
            wheel_of(f, page, kMmFeWheelCodenames);
            f.desc = text_of_control(page, 0x100001f1, MCT_MEMO);
            codename_items(f);
            break;
        case MmFrontView::Name: {
            f.title = text_of_control(page, 0x10000242, MCT_LABEL);
            f.entry = MenuModern_Utf8(codename_entry);
            f.suggested = del_when_keypressed != 0;
            f.cheat = cheat_mode != 0;
            f.max_len = kCodenameMax;
            for (M_CONTROL* c = first_control(page); c != nullptr; c = c->next) {
                if (c->id != kMmFeKeys || c->type != MCT_BUTTON) continue;
                MmKey k;
                k.tag = c->tag;
                k.text = MenuModern_Utf8(label_string(c));
                k.x = c->x;
                k.y = c->y;
                k.w = c->w;
                k.h = c->h;
                f.keys.push_back(k);
            }
            break;
        }
        case MmFrontView::CodenameMenu:
            f.title = text_of_control(page, 0x100001f9, MCT_LABEL);
            wheel_of(f, page, kMmFeWheelCnMenu);
            f.desc = text_of_control(page, 0x100001ec, MCT_MEMO);
            items_of(f, cn_options, 7, false);
            break;
        case MmFrontView::Difficulty:
            f.title = text_of_control(page, 0x10000001, MCT_LABEL);
            if (f.title.empty() || f.title.find('~') != std::string::npos) f.title = text_of(0x1f8);
            wheel_of(f, page, kMmFeWheelDifficulty);
            f.desc = text_of_control(page, 0x100001ee, MCT_MEMO);
            items_of(f, difficulty, 3, false);
            break;
        case MmFrontView::Map:
            f.title = text_of_control(page, 0x10000029, MCT_LABEL);
            wheel_of(f, page, kMmFeWheelMap);
            f.desc = text_of_control(page, 0x1000001f, MCT_MEMO);
            items_of(f, reinterpret_cast<const M_ITEM*>(sp_level), 12, true);
            break;
        case MmFrontView::Options:
            f.option_page = page->base.id;
            break;
        case MmFrontView::Dossier: dossier_info(f, page); break;
        case MmFrontView::Credits: credits_info(f, page); break;
        default:
            break;
    }
    (void)m;
}

// As letras espaçadas dos menus ("C O N T I N U E", "Q U I T  G A M E"): letras sozinhas separadas por um espaço,
// e dois entre as palavras.
bool spaced_letters(const std::string& s) {
    int letters = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == ' ') return false;
        ++letters;
        ++i;
        if (i == s.size()) break;
        if (s[i] != ' ') return false;
        ++i;
        if (i < s.size() && s[i] == ' ') ++i;
        if (i == s.size()) return false;
    }
    return letters >= 2;
}

}  // namespace

MmObjState MenuModern_ObjectiveState(int32_t state) {
    switch (state) {
        case 2:
        case 3: return MmObjState::Active;
        case 4: return MmObjState::Done;
        case 5: return MmObjState::Failed;
        default: return MmObjState::Hidden;
    }
}

MmFrontView MenuModern_FrontView(uint32_t page) {
    switch (page) {
        case kMmFeLanguage:
        case kMmFeCardInit:
        case kMmFeIntro:
        case kMmFeStart:
        case kMmFeAttract:
        case kMmFeEnum:
        case kMmFeHero: return MmFrontView::Title;
        case kMmFeMain: return MmFrontView::Main;
        case kMmFeCampaign: return MmFrontView::Campaign;
        case kMmFeCodenames: return MmFrontView::Codenames;
        case kMmFeName: return MmFrontView::Name;
        case kMmFeCnMenu: return MmFrontView::CodenameMenu;
        case kMmFeDifficulty: return MmFrontView::Difficulty;
        case kMmFeMap: return MmFrontView::Map;
        case kMmFeControls:
        case kMmFeAdvanced:
        case kMmFeAv: return MmFrontView::Options;
        case kMmFeDossier:
        case kMmFeRecords:
        case kMmFeRewards:
        case kMmFeGadgets:
        case kMmFeWeapons: return MmFrontView::Dossier;
        case kMmFeCredits: return MmFrontView::Credits;
        default: return MmFrontView::None;
    }
}

uint32_t MenuModern_KeyFor(const std::vector<MmKey>& keys, char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == ' ') {
        for (const MmKey& k : keys) {
            if (k.tag == kMmFeKeySpace) return k.tag;
        }
        return 0xffffffffu;
    }
    for (const MmKey& k : keys) {
        if (k.tag >= kMmFeKeyDel) continue;
        if (k.text.size() == 1 && k.text[0] == c) return k.tag;
    }
    return 0xffffffffu;
}

void MenuModern_CodenameOptionsBegin() { g_options_print = options_print(); }

bool MenuModern_CodenameOptionsEnd() {
    if (!g_options_print.valid) return false;
    const OptionsPrint now = options_print();
    const OptionsPrint& was = g_options_print;
    const bool changed = std::memcmp(&now.player, &was.player, sizeof(now.player)) != 0 ||
                         std::memcmp(now.draw, was.draw, sizeof(now.draw)) != 0 || now.music != was.music ||
                         now.sfx != was.sfx || now.mode != was.mode;
    g_options_print.valid = false;
    if (changed) cn_modified_flag = 1;
    return changed;
}

std::string MenuModern_Utf8(const char* text, bool collapse) {
    std::string out;
    if (text == nullptr) return out;
    std::string src = text;
    if (collapse && spaced_letters(src)) {
        std::string joined;
        for (size_t i = 0; i < src.size(); ++i) {
            if (src[i] != ' ') {
                joined += src[i];
            } else if (i + 1 < src.size() && src[i + 1] == ' ') {
                joined += ' ';
                ++i;
            }
        }
        src = joined;
    }
    // Windows-1252 para UTF-8 (os do jogo: ’ … ™ e o latim-1).
    for (const char ch : src) {
        const uint8_t c = uint8_t(ch);
        uint32_t cp = c;
        switch (c) {
            case 0x85: cp = 0x2026; break;
            case 0x91: cp = 0x2018; break;
            case 0x92: cp = 0x2019; break;
            case 0x93: cp = 0x201c; break;
            case 0x94: cp = 0x201d; break;
            case 0x96: cp = 0x2013; break;
            case 0x97: cp = 0x2014; break;
            case 0x99: cp = 0x2122; break;
            case 0x8c: cp = 0x152; break;
            case 0x9c: cp = 0x153; break;
            default: break;
        }
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xc0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3f));
        } else {
            out += char(0xe0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3f));
            out += char(0x80 | (cp & 0x3f));
        }
    }
    return out;
}

MmSnapshot MenuModern_Snapshot() {
    MmSnapshot s;
    s.status = MenuManager_GetStatus();
    s.multiplayer = multiplayer();
    M_MANAGER& m = menu_manager[0];
    if (m.active == 0 || m.page == nullptr) return s;
    s.page = m.page->base.id;
    M_PAGE* base = base_page(m);
    s.base_page = base != nullptr ? base->base.id : s.page;
    s.ready = m.frozen == 0;
    s.accepts = s.ready && m.transition == 0 && m.previous == nullptr && m.page->ticks >= m.page->input_delay;
    // A pausa só na página dela: a página de opções Original (0x40000060, aberta no estilo Original) fica como está.
    if (m.status == 3 && s.base_page == kMmPausePage) {
        s.screen = MmScreen::Pause;
        pause_info(s, m);
    } else if (m.status == 4 && s.base_page == kMmFailPage) {
        s.screen = MmScreen::Fail;
        fail_info(s, m);
    } else if (m.status == 5 && (s.base_page == kMmResultsPage || s.base_page == kMmStatsPage || s.base_page == kMmBonusPage)) {
        s.screen = MmScreen::Results;
        results_info(s, m);
    } else if (m.status == 5 && base != nullptr && MenuModern_FrontView(s.base_page) != MmFrontView::None) {
        s.screen = MmScreen::Front;
        front_info(s, m, base);
    } else if (m.status == 2 && s.page == kMmBoxPage) {
        // O aviso de controle desconectado (MenuManager_Monitor): o texto da caixa, abaixo.
        s.screen = MmScreen::Warning;
    }
    // A caixa de mensagem do jogo por cima (Menu_CreateOptionBox): o texto e as respostas.
    if (s.page == kMmBoxPage) {
        s.dialog = MmDialog{};
        s.dialog.open = true;
        s.dialog.box = true;
        s.dialog.body = MenuModern_Utf8(label_string(find(m.page, 0x1000011f, -1, MCT_MEMO)));
        M_LISTBOX* l = find_list(m.page, kMmBoxList, -1);
        s.dialog.options = rows_of(l, true);
        s.dialog.focus = selected_index(l);
        s.dialog.list = kMmBoxList;
        s.dialog.tag = l != nullptr ? l->base.tag : 0;
    }
    // A caixa do cartão de memória (gravar o perfil depois do resultado): a mensagem e as respostas, quando há.
    if (s.page == kMmCardPage) {
        s.dialog = MmDialog{};
        s.dialog.open = true;
        s.dialog.box = true;
        s.dialog.body = MenuModern_Utf8(label_string(find(m.page, 0x100000fb, -1, MCT_MEMO)));
        M_LISTBOX* l = find_list(m.page, kMmCardList, -1);
        s.dialog.options = rows_of(l, true);
        s.dialog.focus = selected_index(l);
        s.dialog.list = kMmCardList;
        s.dialog.tag = l != nullptr ? l->base.tag : 0;
    }
    return s;
}

void MenuModern_Request(const MmRequest& r) { g_queue.push_back(r); }

const std::vector<MmRequest>& MenuModern_Queue() { return g_queue; }

bool MenuModern_Queued(MmAction kind) {
    for (const MmRequest& r : g_queue) {
        if (r.kind == kind) return true;
    }
    return false;
}

void MenuModern_Clear() {
    g_menu_modern_stats.dropped += uint32_t(g_queue.size());
    g_queue.clear();
}

void MenuModern_Install() { Menu_SetPortTick(&MenuModern_Tick); }

void MenuModern_Tick(M_MANAGER* m) {
    if (g_queue.empty() || m == nullptr || m->page == nullptr || m->frozen != 0) return;
    size_t done = 0;
    while (done < g_queue.size()) {
        const MmRequest r = g_queue[done++];
        ++g_menu_modern_stats.requests;
        bool end_tick = false;
        switch (r.kind) {
            case MmAction::Tab: {
                // O cursor no meio do botão: o Page_Update deste tick muda o foco com o 0x4f/0x4e e o som, como o
                // direcional (Menu_FindControl põe o cursor do mesmo jeito).
                M_CONTROL* b = find(m->page, kMmPauseControls, r.a, MCT_BUTTON);
                if (b == nullptr || b->state != 0 || m->cursor == nullptr) {
                    ++g_menu_modern_stats.dropped;
                    break;
                }
                m->cursor->x = int16_t(m->page->base.x + b->x + (b->w >> 1));
                m->cursor->y = int16_t(m->page->base.y + b->y + (b->h >> 1));
                ++g_menu_modern_stats.tabs;
                end_tick = true;
                break;
            }
            case MmAction::Pick: {
                M_LISTBOX* l = find_list(m->page, r.a, r.b == kMmAnyTag ? -1 : int64_t(r.b));
                if (l == nullptr) {
                    ++g_menu_modern_stats.dropped;
                    break;
                }
                if (selected_index(l) != r.c && r.c >= 0 && uint32_t(r.c) < l->rows.count) List_SendMessage(l, 0x1d, r.c, 0);
                ++g_menu_modern_stats.picks;
                break;
            }
            case MmAction::Press:
                // Um tick com o botão apertado (como o do controle): os controles e o gerente o leem neste tick.
                Input_SetAction(player_of(m), r.a, kActionDown | kActionPressed);
                ++g_menu_modern_stats.presses;
                end_tick = true;
                break;
            case MmAction::Focus: {
                // Como Tab, para qualquer controle visível: o Page_Update deste tick leva o foco ao controle sob o
                // cursor (Menu_GetControl), com os eventos e o som do jogo.
                M_CONTROL* c = find(m->page, r.a, r.b == kMmAnyTag ? -1 : int64_t(r.b));
                if (c == nullptr || (c->state & 1) != 0 || m->cursor == nullptr) {
                    ++g_menu_modern_stats.dropped;
                    break;
                }
                m->cursor->x = int16_t(m->page->base.x + c->x + (c->w >> 1));
                m->cursor->y = int16_t(m->page->base.y + c->y + (c->h >> 1));
                ++g_menu_modern_stats.focuses;
                end_tick = true;
                break;
            }
            case MmAction::Step: {
                // A roda anda como no Scroll_Update com o direcional: um passo por vez (sem dar a volta), o som do
                // passo e o 0x49 ao dono (o tratador refaz os rótulos, a imagem e a descrição). Num rádio é a barra
                // dele: o 0x49 vai ao rádio, que escolhe a opção da posição e passa o 0x49 ao dono dele (o
                // C_RBDSRECORDS_Handler refaz os recordes, por exemplo).
                M_SCROLL* s = reinterpret_cast<M_SCROLL*>(find(m->page, r.a, -1, MCT_SCROLL));
                if (s == nullptr) {
                    if (M_CONTROL* radio = find(m->page, r.a, -1, MCT_RADIO)) s = &reinterpret_cast<M_RADIO*>(radio)->scroll;
                }
                if (s == nullptr) {
                    ++g_menu_modern_stats.dropped;
                    break;
                }
                const int32_t target = std::clamp(r.c, s->min, s->max);
                while (s->pos != target) {
                    const int32_t dir = target < s->pos ? -1 : 1;
                    s->pos += dir;
                    Menu_PlaySound(dir < 0 ? 3 : 4);
                    __Menu_SendMessage(s->base.owner, 0x49, int32_t(reinterpret_cast<uintptr_t>(s)), 0);
                    ++g_menu_modern_stats.steps;
                }
                break;
            }
            case MmAction::Show:
                // A mensagem do script de um controle que troca a página (o alvo 0xfffffffd, o gerente): a página de
                // agora vai à pilha (voltar volta a ela) e a nova recebe o 0x4c com a de antes.
                __Menu_SendMessage(&m->base, 0x44, int32_t(r.a), 0);
                ++g_menu_modern_stats.shows;
                end_tick = true;
                break;
        }
        if (end_tick) break;
    }
    g_queue.erase(g_queue.begin(), g_queue.begin() + std::ptrdiff_t(done));
}

int MenuModern_Describe(char* out, size_t size) {
    return std::snprintf(out, size, "pedidos %u abas %u linhas %u botoes %u focos %u passos %u paginas %u perdidos %u fila %zu",
                         g_menu_modern_stats.requests, g_menu_modern_stats.tabs, g_menu_modern_stats.picks,
                         g_menu_modern_stats.presses, g_menu_modern_stats.focuses, g_menu_modern_stats.steps,
                         g_menu_modern_stats.shows, g_menu_modern_stats.dropped, g_queue.size());
}

void menu_modern_debug_power_on() {
    g_queue.clear();
    g_menu_modern_stats = MenuModernStats{};
    g_options_print = OptionsPrint{};
}
