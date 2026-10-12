// Página só do port: as opções do PC na pausa, em abas. Ver menu_pc_options.h e docs/port.md, "Opções do PC
// no menu". Nada aqui vem do ACTION.ELF: a página usa as estruturas, os controles e as peças do sistema de
// menus portado (as do 08000001 e, para a barra de volume, as do conjunto 10 do 08000002).

#include "engine/menu_pc_options.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/font.h"
#include "engine/input.h"
#include "engine/menu.h"
#include "engine/menu_mouse.h"
#include "engine/pc_text.h"
#include "platform/game_options.h"
#include "platform/input_bindings.h"
#include "platform/video_settings.h"

MenuPcStats g_menu_pc_stats;

namespace {

int32_t val(const void* p) { return int32_t(reinterpret_cast<uintptr_t>(p)); }
template <class T>
T* ptr(int32_t v) {
    return reinterpret_cast<T*>(uintptr_t(uint32_t(v)));
}
M_MANAGER* mgr(uint8_t index) { return &menu_manager[index]; }

// ---- o visual (os arquivos de menu do jogo) ----
constexpr uint32_t kPanelTexture = 0x03000070u;  // o painel da pausa com a faixa das abas
constexpr uint32_t kPanelColour = 0x7f7f7fffu;
constexpr uint32_t kOn = 0x645a49ffu;            // 0x7d6d59ff do arquivo (MenuManager_Create a troca por esta)
constexpr uint32_t kRowOff = 0x73330fc0u;        // as linhas sem foco das páginas de opções do 08000002
constexpr uint32_t kTabOff = 0x201707ffu;        // as abas sem foco da pausa
constexpr uint32_t kDisabled = 0x3a200a80u;      // só do port: uma linha apagada (a cor sem foco, mais fraca)
constexpr uint32_t kHelpColour = 0x060100ffu;    // a barra de ajuda da pausa (0x1000006f)
constexpr uint32_t kRowSelected = 0x806d59ffu;   // a linha escolhida das listas da pausa (P_PAUSE_Handler)
constexpr uint16_t kSetText = 2;                 // só texto (os botões das abas da pausa)
constexpr uint16_t kSetRadio = 3;                // rádio com as setas, lista com moldura e setas
constexpr uint16_t kSetScroll = 5;               // a barra com setas e cursor do 08000001
char kTabFormat[] = "\xff\x02\xfe\x03";          // fonte 2 centrada (as abas da pausa)
char kRowFormat[] = "\xff\x03";                  // fonte 3 (os nomes das linhas do "AV Options")
char kRadioFormat[] = "\xff\x03\xfe\x03";        // fonte 3 centrada (os rádios do "AV Options")
char kHelpFormat[] = "\xff\x02\xfe\x03";

// ---- a geometria, em coordenadas do arquivo (640 x 480) ----
constexpr int16_t kPanelX = 45, kPanelY = 107, kPanelW = 550, kPanelH = 266;  // o da pausa
constexpr int16_t kTabsLeft = 82, kTabsRight = 558, kTabsY = 120, kTabsH = 16;  // os da pausa: 84 a 555
constexpr int16_t kRowTop = 146, kRowPitch = 25, kRowH = 16;
constexpr int16_t kNameX = 70, kNameW = 268;
constexpr int16_t kCtlX = 310, kCtlW = 248;
constexpr int16_t kInfoX = 290, kInfoW = 268;  // o valor de uma linha de informação (mais largo)
constexpr int16_t kAreaX = 66, kAreaW = 508, kAreaY = 144, kAreaH = 164;  // a lista de teclas
constexpr int16_t kStatusY = 312;
constexpr int16_t kBarX = 570, kBarW = 14;
constexpr int16_t kHelpX = 45, kHelpY = 341, kHelpW = 550, kHelpH = 18;

// ---- as abas e as linhas ----
constexpr int kMaxRows = 12;
struct RowEntry {
    uint32_t id = 0;
    PcRowKind kind = PcRowKind::Info;
    bool enabled = true;
    char name[128] = {};   // o texto do nome (o rótulo aponta para ele)
    char value[128] = {};  // Info e Status
};
struct TabEntry {
    MenuPcTab desc;
    bool registered = false;
    RowEntry rows[kMaxRows];
    int count = 0;
    int top = 0;          // a primeira linha visível
    char title[48] = {};  // o texto do botão da aba
};
TabEntry g_tabs[kPcTabCount];
PcTab g_current = PcTab::Video;
PcTab g_requested = PcTab::Video;   // a aba que a página abre ao entrar
bool g_show_audio = true;           // a aba ÁUDIO coube na faixa
char g_help[192];
uint32_t g_text_generation = 0;
int32_t g_last_list_index = -1;    // a escolha da lista (teclas) no tick anterior
PcText g_help_text = PcText::Count; // o texto da barra de ajuda agora

// Páginas só do port registradas: o construtor e o controle do foco.
struct PageEntry {
    MenuPcBuilder build = nullptr;
    uint32_t focus = 0;
};
std::map<uint32_t, PageEntry>& page_table() {
    static std::map<uint32_t, PageEntry> t;
    return t;
}

// Os tratadores da pausa antes dos embrulhos (o do jogo, ou o de outro embrulho do port).
MenuHandler g_prev_pause_page = nullptr;
MenuHandler g_prev_pause_ctrl = nullptr;
MenuPcOptionsHook g_options_hook = nullptr;

// VÍDEO, GRÁFICOS e INTERFACE: a cópia editada e a confirmação.
nf::VideoSettings g_pending;
bool g_box_open = false;
int32_t g_box_seconds = -1;
char g_box_text[256];
uint32_t g_pause_generation = 0;

M_PAGE* next_page(M_PAGE* p) {
    return p->next != nullptr ? reinterpret_cast<M_PAGE*>(reinterpret_cast<uint8_t*>(p->next) - 0xa4) : nullptr;
}

M_PAGE* find_page(M_MANAGER* m, uint32_t id) {
    for (M_PAGE* p = static_cast<M_PAGE*>(ll_first(m->pages)); p != nullptr; p = next_page(p)) {
        if (p->base.id == id) return p;
    }
    return nullptr;
}

M_CONTROL* find_control(M_PAGE* page, uint32_t id, int type = -1, int tag = -1) {
    if (page == nullptr) return nullptr;
    for (M_CONTROL* c = static_cast<M_CONTROL*>(ll_first(page->controls)); c != nullptr; c = c->next) {
        if (c->id != id) continue;
        if (type >= 0 && c->type != type) continue;
        if (tag >= 0 && c->tag != uint32_t(tag)) continue;
        return c;
    }
    return nullptr;
}

M_PAGE* options_page(uint8_t index) {
    if (index >= kMenuManagers || menu_manager[index].active == 0) return nullptr;
    return find_page(mgr(index), kPcOptionsPage);
}

// O controle principal da linha (não o rótulo do nome).
M_CONTROL* row_control(M_PAGE* page, const RowEntry& r) {
    switch (r.kind) {
        case PcRowKind::Slider: return find_control(page, r.id, MCT_SCROLL);
        case PcRowKind::Choice: return find_control(page, r.id, MCT_RADIO);
        case PcRowKind::Button: return find_control(page, r.id, MCT_BUTTON);
        case PcRowKind::List: return find_control(page, r.id, MCT_LIST);
        case PcRowKind::Info: return find_control(page, r.id, MCT_LABEL, 1);
        case PcRowKind::Status: return find_control(page, r.id, MCT_LABEL, 0);
    }
    return nullptr;
}

M_CONTROL* row_name(M_PAGE* page, const RowEntry& r) {
    if (r.kind == PcRowKind::Button || r.kind == PcRowKind::List || r.kind == PcRowKind::Status) return nullptr;
    return find_control(page, r.id, MCT_LABEL, 0);
}

RowEntry* find_row(uint32_t id, PcTab* tab_out = nullptr) {
    for (int t = 0; t < kPcTabCount; ++t) {
        for (int i = 0; i < g_tabs[t].count; ++i) {
            if (g_tabs[t].rows[i].id != id) continue;
            if (tab_out != nullptr) *tab_out = PcTab(t);
            return &g_tabs[t].rows[i];
        }
    }
    return nullptr;
}

bool is_line(const RowEntry& r) { return r.kind != PcRowKind::List && r.kind != PcRowKind::Status; }
bool focusable(const RowEntry& r) { return r.enabled && r.kind != PcRowKind::Info && r.kind != PcRowKind::Status; }

// O retângulo do controle na tela do PS2 (FixupResolution, como os registros do arquivo).
NEW_CONTROL fixed(uint8_t type, int16_t x, int16_t y, int16_t w, int16_t h) {
    alignas(4) uint8_t record[0x58]{};
    NEW_CONTROL& d = *reinterpret_cast<NEW_CONTROL*>(record);
    record[0x21] = type;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    FixupResolution(&d);
    return d;
}

void send(M_CONTROL* c, uint32_t message, int32_t a, int32_t b = 0) {
    if (c != nullptr) __Menu_SendMessage(c, message, a, b);
}

// A cor sem foco de um controle mudada depois de montado. Label_Update, ao perder o foco (ou no primeiro
// update), cria um processo 0xe1 que desbota em 15 ticks até a cor sem foco daquele momento (e os processos
// rodam mesmo com o controle escondido): o processo em curso é apagado, senão terminaria na cor antiga.
void set_off_colour(M_CONTROL* c, uint32_t colour) {
    if (c == nullptr) return;
    __Menu_SendMessage(c, 0x1c, int32_t(colour), 0);
    Process_Destroy(c);
    if (c->type == MCT_BUTTON || c->type == MCT_CHECKBOX || c->type == MCT_RADIO) {
        Process_Destroy(reinterpret_cast<M_CONTROL*>(reinterpret_cast<uint8_t*>(c) + 0x94));  // o rótulo interno
    }
}

void copy_text(char* out, size_t size, const std::string& utf8) {
    const std::string g = PcText_ToGame(utf8.c_str());
    std::snprintf(out, size, "%s", g.empty() ? " " : g.c_str());
}

// Um rótulo de texto desabilitado (nome de linha, valor, ajuda).
M_CONTROL* add_label(M_MANAGER* m, uint32_t id, uint32_t tag, int16_t x, int16_t y, int16_t w, int16_t h, char* format,
                     char* text, uint32_t on, uint32_t off) {
    M_CONTROL* l = MenuPc_AddControl(m, id, MCT_LABEL, x, y, w, h, 26, 0, tag);
    send(l, 0x1b, int32_t(on));
    send(l, 0x1c, int32_t(off));
    send(l, 0x23, val(format));
    send(l, 0x18, val(text));
    send(l, 0x62, 0);
    send(l, 0x2b, 2);
    return l;
}

PcText tab_title(PcTab t) {
    switch (t) {
        case PcTab::Video: return PcText::TabVideo;
        case PcTab::Graphics: return PcText::TabGraphics;
        case PcTab::Interface: return PcText::TabInterface;
        case PcTab::Audio: return PcText::TabAudio;
        case PcTab::Controls: return PcText::TabControls;
        default: return PcText::TabKeys;
    }
}

bool tab_shown(PcTab t) { return g_tabs[int(t)].registered && (t != PcTab::Audio || g_show_audio); }

// ---- a faixa de abas ----

// Os botões das abas na faixa do painel: a largura do texto (fonte 2) mais uma folga, espaçados por igual e
// centrados. Se não cabem, a aba ÁUDIO (só "em breve") sai.
void layout_tabs(uint8_t index) {
    M_PAGE* page = options_page(index);
    if (page == nullptr) return;
    float widths[kPcTabCount] = {};
    float total_all = 0.0f;
    int n_all = 0;
    for (int t = 0; t < kPcTabCount; ++t) {
        std::snprintf(g_tabs[t].title, sizeof(g_tabs[t].title), "%s", PcText_Get(tab_title(PcTab(t))));
        GPOINT e{};
        Font_GetTextExtent(g_tabs[t].title, kTabFormat, &e, nullptr, -1);
        widths[t] = std::ceil(e.x / 0.8f) + 12.0f;
        if (!g_tabs[t].registered) continue;
        total_all += widths[t];
        ++n_all;
    }
    const float room = float(kTabsRight - kTabsLeft);
    g_show_audio = total_all + 4.0f * float(n_all - 1) <= room;
    float total = 0.0f;
    int n = 0;
    for (int t = 0; t < kPcTabCount; ++t) {
        if (!tab_shown(PcTab(t))) continue;
        total += widths[t];
        ++n;
    }
    const float gap = n > 1 ? std::clamp((room - total) / float(n - 1), 2.0f, 24.0f) : 0.0f;
    float x = float(kTabsLeft) + (room - total - gap * float(n - 1)) * 0.5f;
    for (int t = 0; t < kPcTabCount; ++t) {
        M_CONTROL* b = find_control(page, kPcTabFirst + uint32_t(t), MCT_BUTTON);
        if (b == nullptr) continue;
        if (!tab_shown(PcTab(t))) {
            send(b, 0x2b, 1);
            continue;
        }
        const NEW_CONTROL d = fixed(MCT_BUTTON, int16_t(std::lround(x)), kTabsY, int16_t(std::lround(widths[t])), kTabsH);
        b->x = d.x;
        b->y = d.y;
        b->w = d.w;
        b->h = d.h;
        send(b, 0x18, val(g_tabs[t].title));
        send(b, 0x2b, 0);
        x += widths[t] + gap;
    }
}

// A aba aberta fica acesa sem o foco (a cor sem foco dela é a acesa); as outras, escuras como as da pausa.
void colour_tabs(uint8_t index) {
    M_PAGE* page = options_page(index);
    for (int t = 0; t < kPcTabCount; ++t) {
        M_CONTROL* b = find_control(page, kPcTabFirst + uint32_t(t), MCT_BUTTON);
        set_off_colour(b, PcTab(t) == g_current ? kOn : kTabOff);
    }
}

int tab_of_control(const M_CONTROL* c) {
    if (c == nullptr || c->type != MCT_BUTTON || c->id < kPcTabFirst || c->id >= kPcTabFirst + kPcTabCount) return -1;
    return int(c->id - kPcTabFirst);
}

// ---- as linhas ----

// Mostra as linhas da aba aberta (as que cabem a partir de `top`) e esconde as outras; a barra da rolagem.
void layout_rows(uint8_t index) {
    M_PAGE* page = options_page(index);
    if (page == nullptr) return;
    for (int t = 0; t < kPcTabCount; ++t) {
        TabEntry& tab = g_tabs[t];
        int lines = 0;
        for (int i = 0; i < tab.count; ++i) lines += is_line(tab.rows[i]) ? 1 : 0;
        tab.top = std::clamp(tab.top, 0, std::max(0, lines - kPcRowsVisible));
        int line = 0;
        for (int i = 0; i < tab.count; ++i) {
            RowEntry& r = tab.rows[i];
            M_CONTROL* name = row_name(page, r);
            M_CONTROL* ctl = row_control(page, r);
            bool shown = PcTab(t) == g_current && !g_box_open;
            if (is_line(r)) {
                const int slot = line - tab.top;
                ++line;
                shown = shown && 0 <= slot && slot < kPcRowsVisible;
                if (shown) {
                    // Só a altura muda (a coluna é a da montagem); FixupResolution dá o y de cada tipo.
                    const int16_t y = int16_t(kRowTop + slot * kRowPitch);
                    for (M_CONTROL* c : {name, ctl}) {
                        if (c == nullptr) continue;
                        const NEW_CONTROL d = fixed(c->type, kCtlX, y, kCtlW, kRowH);
                        c->y = d.y;
                        c->h = d.h;
                    }
                }
            }
            if (!shown) {
                send(name, 0x2b, 1);
                send(ctl, 0x2b, 1);
                continue;
            }
            send(name, 0x2b, 2);
            set_off_colour(name, r.enabled ? kRowOff : kDisabled);
            if (ctl == nullptr) continue;
            // Um botão apagado continua com o foco possível (só a cor diz que não há o que fazer).
            const bool label_only = r.kind == PcRowKind::Info || r.kind == PcRowKind::Status;
            const bool off = !r.enabled && r.kind != PcRowKind::Button;
            send(ctl, 0x2b, label_only || off ? 2 : 0);
            if (r.kind != PcRowKind::List && r.kind != PcRowKind::Status) {
                set_off_colour(ctl, r.enabled ? kRowOff : kDisabled);
            }
        }
    }
    // A barra (desabilitada) só com mais linhas do que cabem.
    M_SCROLL* bar = reinterpret_cast<M_SCROLL*>(find_control(page, kPcRowsBar, MCT_SCROLL));
    if (bar != nullptr) {
        const TabEntry& tab = g_tabs[int(g_current)];
        int lines = 0;
        for (int i = 0; i < tab.count; ++i) lines += is_line(tab.rows[i]) ? 1 : 0;
        const int extra = lines - kPcRowsVisible;
        if (extra > 0 && !g_box_open) {
            Scroll_SendMessage(bar, 0x27, 0, extra);
            bar->pos = tab.top;
            Scroll_SendMessage(bar, 0x2b, 2, 0);
        } else {
            Scroll_SendMessage(bar, 0x2b, 1, 0);
        }
    }
}

// O foco num controle: como Menu_FindControl, o cursor do gerente no meio dele; Page_Update muda o foco
// (com os eventos e o som do jogo) no tick seguinte.
void focus_on(uint8_t index, M_CONTROL* c) {
    M_MANAGER* m = mgr(index);
    if (c == nullptr || m->cursor == nullptr || m->page == nullptr) return;
    m->cursor->x = int16_t(m->page->base.x + c->x + (c->w >> 1));
    m->cursor->y = int16_t(m->page->base.y + c->y + (c->h >> 1));
}

void focus_tab(uint8_t index, PcTab t) {
    focus_on(index, find_control(options_page(index), kPcTabFirst + uint32_t(t), MCT_BUTTON));
}

// Leva a linha i da aba aberta para a área visível (rolando) e põe o foco nela.
void focus_row(uint8_t index, int i) {
    TabEntry& tab = g_tabs[int(g_current)];
    if (i < 0 || i >= tab.count) return;
    if (is_line(tab.rows[i])) {
        int line = 0;
        for (int k = 0; k < i; ++k) line += is_line(tab.rows[k]) ? 1 : 0;
        const int top = tab.top;
        if (line < tab.top) tab.top = line;
        if (line >= tab.top + kPcRowsVisible) tab.top = line - kPcRowsVisible + 1;
        if (tab.top != top) layout_rows(index);
    }
    focus_on(index, row_control(options_page(index), tab.rows[i]));
}

int first_focusable(int from, int dir) {
    const TabEntry& tab = g_tabs[int(g_current)];
    for (int i = from; 0 <= i && i < tab.count; i += dir) {
        if (focusable(tab.rows[i])) return i;
    }
    return -1;
}

int row_index(const M_CONTROL* c) {
    if (c == nullptr) return -1;
    const TabEntry& tab = g_tabs[int(g_current)];
    for (int i = 0; i < tab.count; ++i) {
        if (tab.rows[i].id == c->id) return i;
    }
    return -1;
}

void set_help(uint8_t index, PcText text) {
    if (text == g_help_text) return;
    g_help_text = text;
    std::snprintf(g_help, sizeof(g_help), "%s", PcText_Get(text));
    send(find_control(options_page(index), kPcHelp, MCT_LABEL), 0x18, val(g_help));
}

// Troca a aba aberta: a anterior sai (leave), as linhas trocam e a nova entra (enter).
void show_tab(uint8_t index, PcTab t) {
    if (!g_tabs[int(t)].registered) return;
    if (t != g_current) {
        if (g_tabs[int(g_current)].desc.leave != nullptr) g_tabs[int(g_current)].desc.leave(index);
        g_current = t;
        ++g_menu_pc_stats.tab_changes;
    }
    g_last_list_index = -1;
    colour_tabs(index);
    layout_rows(index);
    if (g_tabs[int(t)].desc.enter != nullptr) g_tabs[int(t)].desc.enter(index);
}

// ---- a página ----

void build_options(uint8_t index, M_MANAGER* m) {
    // Sem a navegação do gerente: cima/baixo e a faixa de abas são da página (options_tick).
    m->page->navigation = 0;
    g_help_text = PcText::Count;
    MenuPc_EnsureComponents(index);
    MenuPc_AddControl(m, kPcWindow, MCT_WINDOW, 0, 0, 640, 480, 0, 0, 2);
    // O painel da pausa no tamanho do arquivo (a imagem 0x03000070 inteira).
    M_CONTROL* panel = MenuPc_AddControl(m, kPcPanel, MCT_LABEL, kPanelX, kPanelY, kPanelW, kPanelH, 30, 0);
    send(panel, 0x1b, int32_t(kPanelColour));
    send(panel, 0x1c, int32_t(kPanelColour));
    send(panel, 0x23, val(kTabFormat));
    send(panel, 0x2d, 0, 0x01ff00ff);
    send(panel, 0x24, int32_t(kPanelTexture));
    send(panel, 0x2b, 2);
    // As abas: botões do conjunto 2 (só o texto, fonte 2), com as cores da pausa (escuras; a do foco, clara).
    for (int t = 0; t < kPcTabCount; ++t) {
        M_CONTROL* b = MenuPc_AddControl(m, kPcTabFirst + uint32_t(t), MCT_BUTTON, kTabsLeft, kTabsY, 60, kTabsH, 25, kSetText);
        send(b, 0x23, val(kTabFormat));
        send(b, 0x1b, int32_t(kOn));
        send(b, 0x1c, int32_t(kTabOff));
        send(b, 0x62, 0);
        std::snprintf(g_tabs[t].title, sizeof(g_tabs[t].title), "%s", PcText_Get(tab_title(PcTab(t))));
        send(b, 0x18, val(g_tabs[t].title));
    }
    std::snprintf(g_help, sizeof(g_help), "%s", PcText_Get(PcText::HelpTabs));
    add_label(m, kPcHelp, 0, kHelpX, kHelpY, kHelpW, kHelpH, kHelpFormat, g_help, kHelpColour, kHelpColour);
    // A barra que mostra a rolagem das linhas (a do 08000001, desabilitada: só mostra).
    M_CONTROL* bar = MenuPc_AddControl(m, kPcRowsBar, MCT_SCROLL, kBarX, kRowTop, kBarW,
                                       int16_t(kPcRowsVisible * kRowPitch - (kRowPitch - kRowH)), 24, kSetScroll, 0, 0, 1);
    if (bar != nullptr) reinterpret_cast<M_SCROLL*>(bar)->wrap = 0;
    send(bar, 0x2b, 1);
    for (int t = 0; t < kPcTabCount; ++t) {
        g_tabs[t].count = 0;
        g_tabs[t].top = 0;
        if (g_tabs[t].registered && g_tabs[t].desc.build != nullptr) g_tabs[t].desc.build(index, m);
    }
    g_text_generation = PcText_Generation();
}

bool same_settings(const nf::VideoSettings& a, const nf::VideoSettings& b) { return a == b; }

void refresh_all_texts(uint8_t index) {
    g_text_generation = PcText_Generation();
    g_help_text = PcText::Count;
    layout_tabs(index);
    colour_tabs(index);
    show_tab(index, g_current);
}

int32_t options_page_handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t a, int32_t b);
int32_t tab_button(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t);

// ---- VÍDEO, GRÁFICOS, INTERFACE e ÁUDIO ----

const char* on_off(bool on) { return PcText_Utf8(on ? PcText::On : PcText::Off); }
int fov_int(float fov) { return int(std::lround(fov)); }

std::string fmt_int(PcText id, int v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), PcText_Utf8(id), v);
    return buf;
}

std::string mode_text(const nf::DisplayMode& d, nf::WindowMode wm) {
    const nf::VideoController& vc = nf::video_controller();
    char buf[64];
    if (wm == nf::WindowMode::Borderless && vc.desktop().width > 0) {
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::Desktop), vc.desktop().width, vc.desktop().height);
    } else {
        std::snprintf(buf, sizeof(buf), "%d x %d", d.width, d.height);
    }
    return buf;
}

std::vector<nf::DisplayMode> g_res_choices;
std::vector<int> g_scale_choices, g_msaa_choices, g_fps_choices;
std::vector<std::string> g_adapter_choices;  // os valores de [video] adapter da linha (adapter_choice_values)

nf::TextureFilter kFilters[] = {nf::TextureFilter::Bilinear, nf::TextureFilter::Trilinear, nf::TextureFilter::Aniso2,
                                nf::TextureFilter::Aniso4,   nf::TextureFilter::Aniso8,    nf::TextureFilter::Aniso16};

std::string filter_text(nf::TextureFilter f) {
    if (f == nf::TextureFilter::Bilinear) return PcText_Utf8(PcText::FilterBilinear);
    if (f == nf::TextureFilter::Trilinear) return PcText_Utf8(PcText::FilterTrilinear);
    return fmt_int(PcText::FilterAniso, nf::texture_filter_anisotropy(f));
}

void set_toggle(uint8_t index, uint32_t id, bool on) { MenuPc_SetChoices(index, id, {on_off(false), on_off(true)}, on ? 1 : 0); }

// Os botões "Aplicar mudanças" acendem só com algo para aplicar.
void refresh_apply(uint8_t index) {
    const bool pending = !same_settings(g_pending, nf::video_controller().live());
    for (const uint32_t id : {kPcRowApplyVideo, kPcRowApplyGraphics, kPcRowApplyInterface}) {
        MenuPc_SetRowEnabled(index, id, pending);
    }
}

void video_enter(uint8_t index) {
    const nf::VideoController& vc = nf::video_controller();
    nf::VideoSettings& p = g_pending;
    MenuPc_SetRowName(index, kPcRowResolution, PcText_Utf8(PcText::Resolution));
    MenuPc_SetRowName(index, kPcRowWindowMode, PcText_Utf8(PcText::WindowMode));
    MenuPc_SetRowName(index, kPcRowVSync, PcText_Utf8(PcText::VSync));
    MenuPc_SetRowName(index, kPcRowFov, fmt_int(PcText::FieldOfView, fov_int(p.fov)));
    MenuPc_SetRowName(index, kPcRowFpsCap, PcText_Utf8(PcText::FpsCap));
    MenuPc_SetRowName(index, kPcRowInterpolation, PcText_Utf8(PcText::FrameInterpolation));
    MenuPc_SetRowName(index, kPcRowInstantMouse, PcText_Utf8(PcText::InstantMouse));
    MenuPc_SetRowName(index, kPcRowAdapter, PcText_Utf8(PcText::GraphicsAdapterRow));
    MenuPc_SetRowName(index, kPcRowApplyVideo, PcText_Utf8(PcText::Apply));
    const nf::DisplayMode cur{p.width, p.height};
    g_res_choices = nf::resolution_choices(vc.display_modes(), vc.desktop(), p.window_mode, cur);
    if (g_res_choices.empty()) g_res_choices.push_back(cur);
    std::vector<std::string> res;
    int32_t sel = 0;
    for (size_t i = 0; i < g_res_choices.size(); ++i) {
        res.push_back(mode_text(g_res_choices[i], p.window_mode));
        if (g_res_choices[i] == cur) sel = int32_t(i);
    }
    MenuPc_SetChoices(index, kPcRowResolution, res, sel);
    MenuPc_SetChoices(index, kPcRowWindowMode,
                      {PcText_Utf8(PcText::ModeWindowed), PcText_Utf8(PcText::ModeBorderless),
                       PcText_Utf8(PcText::ModeExclusive)},
                      int32_t(p.window_mode));
    set_toggle(index, kPcRowVSync, p.vsync);
    // O campo de visão de 40 a 100 graus, de 5 em 5 (nf::fov_values): a posição 0 a 12 da barra.
    MenuPc_SetSlider(index, kPcRowFov, std::clamp((fov_int(p.fov) - 40 + 2) / 5, 0, 12));
    // O ritmo dos quadros (render/frame_interp.h): o limite (desligado, 30 ... 240), a interpolação e o mouse sem atraso.
    g_fps_choices = nf::fps_cap_values(p.fps_cap);
    std::vector<std::string> caps;
    sel = 0;
    for (size_t i = 0; i < g_fps_choices.size(); ++i) {
        caps.push_back(g_fps_choices[i] <= 0 ? std::string(on_off(false)) : fmt_int(PcText::FpsCapValue, g_fps_choices[i]));
        if (g_fps_choices[i] == p.fps_cap) sel = int32_t(i);
    }
    MenuPc_SetChoices(index, kPcRowFpsCap, caps, sel);
    set_toggle(index, kPcRowInterpolation, p.interpolation);
    set_toggle(index, kPcRowInstantMouse, p.instant_mouse_look);
    // O adaptador de vídeo (platform/gpu_adapter.h): os nomes curtos das placas (a coluna é estreita na fonte 3).
    g_adapter_choices = nf::adapter_choice_values(vc.adapters(), p.adapter);
    std::vector<std::string> adapters;
    sel = 0;
    for (size_t i = 0; i < g_adapter_choices.size(); ++i) {
        const std::string& v = g_adapter_choices[i];
        adapters.push_back(v == "auto"         ? std::string(PcText_Utf8(PcText::AdapterAuto))
                           : v == "integrated" ? std::string(PcText_Utf8(PcText::AdapterIntegrated))
                           : v == "discrete"   ? std::string(PcText_Utf8(PcText::AdapterDiscrete))
                                               : nf::adapter_short_name(v));
        if (v == p.adapter) sel = int32_t(i);
    }
    MenuPc_SetChoices(index, kPcRowAdapter, adapters, sel);
    refresh_apply(index);
}

void graphics_enter(uint8_t index) {
    const nf::VideoController& vc = nf::video_controller();
    nf::VideoSettings& p = g_pending;
    MenuPc_SetRowName(index, kPcRowRenderScale, PcText_Utf8(PcText::RenderScale));
    MenuPc_SetRowName(index, kPcRowAntiAliasing, PcText_Utf8(PcText::AntiAliasing));
    MenuPc_SetRowName(index, kPcRowFilter, PcText_Utf8(PcText::TextureFilter));
    MenuPc_SetRowName(index, kPcRowHdTextures, PcText_Utf8(PcText::HdTextures));
    MenuPc_SetRowName(index, kPcRowDlss, PcText_Utf8(PcText::Dlss));
    MenuPc_SetRowName(index, kPcRowApplyGraphics, PcText_Utf8(PcText::Apply));
    g_scale_choices = nf::render_scale_values(p.render_scale);
    std::vector<std::string> scales;
    int32_t sel = 0;
    for (size_t i = 0; i < g_scale_choices.size(); ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d%%", g_scale_choices[i]);
        scales.push_back(buf);
        if (g_scale_choices[i] == p.render_scale) sel = int32_t(i);
    }
    MenuPc_SetChoices(index, kPcRowRenderScale, scales, sel);
    g_msaa_choices = nf::msaa_values(vc.msaa_support(), p.msaa);
    std::vector<std::string> aa;
    sel = 0;
    for (size_t i = 0; i < g_msaa_choices.size(); ++i) {
        aa.push_back(g_msaa_choices[i] <= 1 ? std::string(on_off(false)) : fmt_int(PcText::Msaa, g_msaa_choices[i]));
        if (g_msaa_choices[i] == p.msaa) sel = int32_t(i);
    }
    MenuPc_SetChoices(index, kPcRowAntiAliasing, aa, sel);
    std::vector<std::string> filters;
    for (const nf::TextureFilter f : kFilters) filters.push_back(filter_text(f));
    MenuPc_SetChoices(index, kPcRowFilter, filters, int32_t(p.filter));
    set_toggle(index, kPcRowHdTextures, p.hd_textures);
    MenuPc_SetChoices(index, kPcRowDlss, {PcText_Utf8(PcText::ComingSoon)}, 0);
    refresh_apply(index);
}

void interface_enter(uint8_t index) {
    nf::VideoSettings& p = g_pending;
    MenuPc_SetRowName(index, kPcRowHdUi, PcText_Utf8(PcText::HdInterface));
    MenuPc_SetRowName(index, kPcRowHudAnchor, PcText_Utf8(PcText::HudAnchor));
    MenuPc_SetRowName(index, kPcRowLanguage, PcText_Utf8(PcText::Language));
    MenuPc_SetRowName(index, kPcRowMenuStyle, PcText_Utf8(PcText::MenuStyle));
    MenuPc_SetRowName(index, kPcRowSubtitles, PcText_Utf8(PcText::MdSubtitles));
    MenuPc_SetRowName(index, kPcRowApplyInterface, PcText_Utf8(PcText::Apply));
    set_toggle(index, kPcRowHdUi, p.hd_ui);
    set_toggle(index, kPcRowSubtitles, nf::game_option(nf::GameOption::Subtitles) != 0);
    MenuPc_SetChoices(index, kPcRowHudAnchor, {PcText_Utf8(PcText::AnchorCentre), PcText_Utf8(PcText::AnchorEdges)},
                      p.hud_edges ? 1 : 0);
    MenuPc_SetChoices(index, kPcRowLanguage,
                      {PcText_Utf8(PcText::LanguageName, "en"), PcText_Utf8(PcText::LanguageName, "pt")},
                      p.language == "pt" ? 1 : 0);
    MenuPc_SetChoices(index, kPcRowMenuStyle, {PcText_Utf8(PcText::StyleModern), PcText_Utf8(PcText::StyleOriginal)},
                      p.menu_style == nf::MenuStyle::Original ? 1 : 0);
    refresh_apply(index);
}

void audio_enter(uint8_t index) {
    MenuPc_SetRowName(index, kPcRowMusic, PcText_Utf8(PcText::AudioMusic));
    MenuPc_SetRowName(index, kPcRowEffects, PcText_Utf8(PcText::AudioEffects));
    MenuPc_SetRowName(index, kPcRowAudioNote, PcText_Utf8(PcText::AudioNote));
    MenuPc_SetRowValue(index, kPcRowAudioNote, PcText_Utf8(PcText::ComingSoon));
    // Os volumes padrão do "AV Options" do jogo (P_CNAVOPTIONS_Handler, "restaurar": música 14, efeitos 20 de 20).
    MenuPc_SetSlider(index, kPcRowMusic, 14);
    MenuPc_SetSlider(index, kPcRowEffects, 20);
}

void build_video(uint8_t, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Video, kPcRowResolution, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowWindowMode, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowVSync, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowFov, PcRowKind::Slider, 0, 12);
    MenuPc_AddRow(m, PcTab::Video, kPcRowFpsCap, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowInterpolation, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowInstantMouse, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowAdapter, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Video, kPcRowApplyVideo, PcRowKind::Button);
}

void build_graphics(uint8_t index, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowRenderScale, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowAntiAliasing, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowFilter, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowHdTextures, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowDlss, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Graphics, kPcRowApplyGraphics, PcRowKind::Button);
    MenuPc_SetRowEnabled(index, kPcRowDlss, false);
}

void build_interface(uint8_t, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Interface, kPcRowHdUi, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Interface, kPcRowHudAnchor, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Interface, kPcRowSubtitles, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Interface, kPcRowMenuStyle, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Interface, kPcRowLanguage, PcRowKind::Choice);
    MenuPc_AddRow(m, PcTab::Interface, kPcRowApplyInterface, PcRowKind::Button);
}

void build_audio(uint8_t index, M_MANAGER* m) {
    MenuPc_AddRow(m, PcTab::Audio, kPcRowMusic, PcRowKind::Slider, 0, 20);
    MenuPc_AddRow(m, PcTab::Audio, kPcRowEffects, PcRowKind::Slider, 0, 20);
    MenuPc_AddRow(m, PcTab::Audio, kPcRowAudioNote, PcRowKind::Info);
    MenuPc_SetRowEnabled(index, kPcRowMusic, false);
    MenuPc_SetRowEnabled(index, kPcRowEffects, false);
}

void refresh_video_tabs(uint8_t index) {
    switch (g_current) {
        case PcTab::Video: video_enter(index); break;
        case PcTab::Graphics: graphics_enter(index); break;
        case PcTab::Interface: interface_enter(index); break;
        default: refresh_apply(index); break;
    }
}

// Uma opção mudou (o rádio ou a barra): a cópia pendente.
void option_changed(uint8_t index, uint32_t id) {
    nf::VideoSettings& p = g_pending;
    const nf::VideoController& vc = nf::video_controller();
    const int32_t c = MenuPc_Choice(index, id);
    switch (id) {
        case kPcRowResolution:
            if (0 <= c && size_t(c) < g_res_choices.size()) {
                p.width = g_res_choices[size_t(c)].width;
                p.height = g_res_choices[size_t(c)].height;
            }
            break;
        case kPcRowWindowMode:
            if (c < 0 || c > 2) break;
            p.window_mode = nf::WindowMode(c);
            nf::fit_resolution_to_mode(p, vc.display_modes(), vc.desktop());
            break;
        case kPcRowVSync: p.vsync = c == 1; break;
        case kPcRowFov: p.fov = float(40 + 5 * MenuPc_Slider(index, kPcRowFov)); break;
        case kPcRowFpsCap:
            if (0 <= c && size_t(c) < g_fps_choices.size()) p.fps_cap = g_fps_choices[size_t(c)];
            break;
        case kPcRowInterpolation: p.interpolation = c == 1; break;
        case kPcRowInstantMouse: p.instant_mouse_look = c == 1; break;
        case kPcRowAdapter:
            if (0 <= c && size_t(c) < g_adapter_choices.size()) p.adapter = g_adapter_choices[size_t(c)];
            break;
        case kPcRowRenderScale:
            if (0 <= c && size_t(c) < g_scale_choices.size()) p.render_scale = g_scale_choices[size_t(c)];
            break;
        case kPcRowAntiAliasing:
            if (0 <= c && size_t(c) < g_msaa_choices.size()) p.msaa = g_msaa_choices[size_t(c)];
            break;
        case kPcRowFilter:
            if (0 <= c && c < 6) p.filter = kFilters[c];
            break;
        case kPcRowHdTextures: p.hd_textures = c == 1; break;
        case kPcRowHdUi: p.hd_ui = c == 1; break;
        case kPcRowHudAnchor: p.hud_edges = c == 1; break;
        case kPcRowLanguage: p.language = c == 1 ? "pt" : "en"; break;
        case kPcRowMenuStyle: p.menu_style = c == 1 ? nf::MenuStyle::Original : nf::MenuStyle::Modern; break;
        case kPcRowSubtitles:
            // A opção do jogo (a "Subtitles" da AV Options): no lugar dele e na hora; o .ini segue a regra de
            // platform/game_options.h.
            nf::set_game_option(nf::GameOption::Subtitles, c == 1 ? 1 : 0);
            nf::persist_controls();
            break;
        default: break;
    }
    refresh_video_tabs(index);
}

void set_box_text(uint8_t index) {
    const int32_t seconds = int32_t(std::ceil(nf::video_controller().remaining()));
    if (seconds == g_box_seconds) return;
    g_box_seconds = seconds;
    std::snprintf(g_box_text, sizeof(g_box_text), PcText_Get(PcText::ConfirmText), seconds);
    if (g_box_open) __Menu_Send(index, 0x1000011f, 0x18, val(g_box_text), 0);
}

// A caixa fechou (respondida, ou fechada aqui com pop): as linhas voltam com a configuração em vigor.
void close_box(uint8_t index, bool pop) {
    if (!g_box_open) return;
    g_box_open = false;
    g_box_seconds = -1;
    M_MANAGER* m = mgr(index);
    if (pop && Menu_CurrentPage(index) == 0x4000002f) Manager_SendMessage(m, 0x5f, 1, 0);
    g_pending = nf::video_controller().live();
    layout_rows(index);
    refresh_video_tabs(index);
}

// A caixa do jogo (página 0x4000002f) com a pergunta, SIM/NÃO e a contagem; as linhas somem por baixo, como
// a lista dos objetivos na pausa.
void open_box(uint8_t index) {
    g_box_open = true;
    layout_rows(index);
    g_box_seconds = -1;
    set_box_text(index);
    Menu_CreateOptionBox(index, g_box_text, kPcVideoConfirm, 0, 0);
    if (Menu_CurrentPage(index) != 0x4000002f) {
        // Sem a página da caixa (outro arquivo de menu): não dá para confirmar, volta já.
        g_box_open = false;
        nf::video_controller().revert();
        g_pending = nf::video_controller().live();
        layout_rows(index);
        refresh_video_tabs(index);
        return;
    }
    __Menu_Send(index, 0x10000120, 0x19, 0 << 16, val(PcText_Get(PcText::Yes)));
    __Menu_Send(index, 0x10000120, 0x19, 1 << 16, val(PcText_Get(PcText::No)));
    ++g_menu_pc_stats.confirms;
}

void apply_pending(uint8_t index) {
    nf::VideoController& vc = nf::video_controller();
    const bool confirm = vc.apply(g_pending);
    PcText_SetLanguage(vc.live().language);
    g_pending = vc.live();  // o gancho pode ter corrigido algo (MSAA sem suporte)
    refresh_video_tabs(index);
    if (confirm) open_box(index);
}

// Um tick com a caixa aberta (a página de opções é a anterior, na transição) ou logo depois da resposta.
void box_tick(uint8_t index) {
    nf::VideoController& vc = nf::video_controller();
    uint32_t id = 0;
    const uint32_t answer = Menu_UpdateOptionBox(&id);
    if (answer != 0 && id == kPcVideoConfirm) {
        if (answer == 1) {
            vc.keep();
        } else {
            vc.revert();
        }
        close_box(index, false);  // C_LBMSGOPTIONS_Handler já fechou a caixa
        return;
    }
    if (Menu_CurrentPage(index) != 0x4000002f) {
        // A caixa fechou sem resposta (o botão de voltar do gerente): como "não".
        vc.revert();
        close_box(index, false);
        return;
    }
    if (vc.confirming()) vc.tick(1.0f / 60.0f);
    if (!vc.confirming()) {
        close_box(index, true);  // o tempo acabou: voltou à anterior
        return;
    }
    set_box_text(index);
}

// As linhas de VÍDEO, GRÁFICOS e INTERFACE: os rádios e a barra mudam a cópia (0x49; escolher num rádio avança
// a opção, como antes); "Aplicar mudanças" a aplica.
int32_t video_row(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t, int32_t) {
    if (c == nullptr || g_box_open) return 1;
    if (message == 0x49 && (c->type == MCT_RADIO || c->type == MCT_SCROLL)) {
        option_changed(index, id);
    } else if (message == 0x4b && c->type == MCT_RADIO) {
        M_RADIO* r = reinterpret_cast<M_RADIO*>(c);
        const int32_t n = int32_t(r->options.count);
        if (n > 1) Radio_SendMessage(r, 0x1d, (MenuPc_Choice(index, id) + 1) % n, 0);
    } else if (message == 0x4b && c->type == MCT_BUTTON) {
        if (!same_settings(g_pending, nf::video_controller().live())) apply_pending(index);
    }
    return 1;
}

// ---- o tratador da página ----

// Scroll_Update chama Scroll_Init a cada tick, e Scroll_Init zera o estado da barra (como no jogo): uma barra
// apagada (as de ÁUDIO, a da rolagem) voltaria a receber o foco e o mouse. A página põe o 2 de novo no fim de
// cada tick (o 0x50 vem depois dos updates; o mouse e o foco do tick seguinte já o veem).
void keep_bars_disabled(uint8_t index) {
    M_PAGE* page = options_page(index);
    if (page == nullptr) return;
    const TabEntry& tab = g_tabs[int(g_current)];
    for (int i = 0; i < tab.count; ++i) {
        const RowEntry& r = tab.rows[i];
        if (r.kind != PcRowKind::Slider || r.enabled) continue;
        M_CONTROL* c = row_control(page, r);
        if (c != nullptr && (c->state & 1) == 0) c->state = 2;
    }
    M_CONTROL* bar = find_control(page, kPcRowsBar, MCT_SCROLL);
    if (bar != nullptr && (bar->state & 1) == 0) bar->state = 2;
}

void options_tick(uint8_t index) {
    M_MANAGER* m = mgr(index);
    M_PAGE* page = m->page;
    if (page == nullptr || page->base.id != kPcOptionsPage) return;
    keep_bars_disabled(index);
    if (g_text_generation != PcText_Generation()) refresh_all_texts(index);
    TabEntry& tab = g_tabs[int(g_current)];
    M_CONTROL* f = page->focus;
    const int focus_tab_index = tab_of_control(f);
    set_help(index, focus_tab_index >= 0 ? PcText::HelpTabs : tab.desc.help);
    if (tab.desc.tick != nullptr) tab.desc.tick(index);
    if (m->frozen != 0) return;
    const int16_t pl = m->player;
    const bool up = Menu_InputAction(pl, 0x1a, 0xc) != 0;
    const bool down = Menu_InputAction(pl, 0x1b, 0xc) != 0;
    const bool left = Menu_InputAction(pl, 0x1c, 0xc) != 0;
    const bool right = Menu_InputAction(pl, 0x1d, 0xc) != 0;
    // A roda que nenhum controle usou rola as linhas.
    if (const int32_t wheel = Menu_MouseTakeWheel(); wheel != 0) {
        const int top = tab.top;
        tab.top -= wheel;
        layout_rows(index);
        if (tab.top != top) {
            const int i = row_index(f);
            if (i >= 0) {
                int line = 0;
                for (int k = 0; k < i; ++k) line += is_line(tab.rows[k]) ? 1 : 0;
                if (line < tab.top || line >= tab.top + kPcRowsVisible) {
                    const int to = first_focusable(line < tab.top ? tab.top : tab.top + kPcRowsVisible - 1,
                                                   line < tab.top ? 1 : -1);
                    if (to >= 0) focus_row(index, to);
                }
            }
        }
    }
    if (focus_tab_index >= 0) {
        // Na faixa: esquerda/direita troca de aba na hora (como a pausa); baixo desce para as linhas.
        int to = -1;
        if (left || right) {
            const int dir = left ? -1 : 1;
            for (int k = 1; k < kPcTabCount && to < 0; ++k) {
                const int t = (focus_tab_index + dir * k + kPcTabCount) % kPcTabCount;
                if (tab_shown(PcTab(t))) to = t;
            }
        }
        if (to >= 0) {
            show_tab(index, PcTab(to));
            focus_tab(index, PcTab(to));
        } else if (down) {
            const int first = first_focusable(0, 1);
            if (first >= 0) focus_row(index, first);
        }
        return;
    }
    const int i = row_index(f);
    if (i < 0) return;
    if (tab.rows[i].kind == PcRowKind::List) {
        // A lista anda sozinha; de cima da primeira linha, sobe para a aba.
        const int32_t sel = __Menu_SendMessage(f, 0x34, 0, 0);
        if (up && sel == 0 && g_last_list_index == 0) focus_tab(index, g_current);
        g_last_list_index = sel;
        return;
    }
    if (up) {
        const int to = first_focusable(i - 1, -1);
        if (to >= 0) {
            focus_row(index, to);
        } else {
            focus_tab(index, g_current);
        }
    } else if (down) {
        const int to = first_focusable(i + 1, 1);
        if (to >= 0) focus_row(index, to);
    }
}

int32_t options_page_handler(uint8_t index, M_CONTROL*, uint32_t, uint32_t message, int32_t, int32_t) {
    if (message == 0x4c) {
        if (g_box_open) return 1;
        g_help_text = PcText::Count;
        g_pending = nf::video_controller().live();
        g_current = g_requested;
        if (!tab_shown(g_current)) g_current = PcTab::Video;
        for (TabEntry& t : g_tabs) t.top = 0;
        refresh_all_texts(index);
        // O foco na aba aberta (direto: o cursor do gerente e o foco da página, sem eventos).
        Manager_SendMessage(mgr(index), 0x22, int32_t(kPcTabFirst + uint32_t(g_current)), 0);
    } else if (message == 0x4d) {
        // A página volta na aba em que estava.
        g_requested = g_current;
        if (g_tabs[int(g_current)].desc.leave != nullptr) g_tabs[int(g_current)].desc.leave(index);
        if (!g_box_open) g_pending = nf::video_controller().live();
    } else if (message == 0x50) {
        if (g_box_open) {
            box_tick(index);
            return 1;
        }
        options_tick(index);
    }
    return 1;
}

// Os botões das abas: escolher (Espaço, o clique do mouse) abre a aba; escolher a aberta desce para as linhas.
int32_t tab_button(uint8_t index, M_CONTROL* c, uint32_t, uint32_t message, int32_t, int32_t) {
    const int t = tab_of_control(c);
    if (t < 0 || message != 0x4b || g_box_open) return 1;
    if (PcTab(t) != g_current) {
        show_tab(index, PcTab(t));
        focus_tab(index, PcTab(t));
    } else {
        const int first = first_focusable(0, 1);
        if (first >= 0) focus_row(index, first);
    }
    return 1;
}

// ---- a pausa ----

bool is_pc_page(uint32_t id) { return id == kPcOptionsPage; }

M_LISTBOX* pause_list(uint8_t index) {
    M_CONTROL* c = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 6, 0x39, 0, 0));
    return c != nullptr && c->type == MCT_LIST ? reinterpret_cast<M_LISTBOX*>(c) : nullptr;
}

bool has_row_data(M_LISTBOX* l, int32_t data) {
    for (LIST_ROW* r = static_cast<LIST_ROW*>(ll_first(l->rows)); r != nullptr; r = r->next) {
        if (r->data == data) return true;
    }
    return false;
}

// A quarta linha da lista principal (as do arquivo têm o valor 0) e a altura para ela caber.
void add_pause_row(uint8_t index) {
    M_LISTBOX* l = pause_list(index);
    if (l == nullptr || has_row_data(l, kPcPauseRow) || l->rows.count != uint32_t(kPcPauseRow)) return;
    __Menu_SendMessage(&l->base, 0x10, val(PcText_Get(PcText::PauseEntry)), kPcPauseRow);
    const int16_t need = int16_t(std::ceil(l->row_height * float(kPcPauseRow + 1)) + 1.0f);
    if (l->base.h < need) l->base.h = need;
    g_pause_generation = PcText_Generation();
    ++g_menu_pc_stats.pause_rows;
}

void refresh_pause_row(uint8_t index) {
    if (g_pause_generation == PcText_Generation()) return;
    M_LISTBOX* l = pause_list(index);
    if (l == nullptr || !has_row_data(l, kPcPauseRow)) return;
    g_pause_generation = PcText_Generation();
    __Menu_SendMessage(&l->base, 0x19, kPcPauseRow << 16, val(PcText_Get(PcText::PauseEntry)));
}

// A pergunta de RESTART/QUIT da pausa (a lista 0x10000028 de tag 0xd) está à mostra.
bool pause_question_open(uint8_t index) {
    const M_CONTROL* q = ptr<M_CONTROL>(__Menu_SendEx(index, 0x10000028, 0xd, 0x39, 0, 0));
    return q != nullptr && q->state == 0;
}

// O voltar que chegou ao topo da pausa saiu da pergunta (o 0x6b dela vem antes do 99).
bool g_pause_back_from_question = false;

// P_PAUSE_Handler com a linha nova. Voltando de uma página do PC (0x4c com a anterior em b), a pausa
// fica como estava: o 0x4c do jogo montaria as listas de novo (os 8 estilos de controle repetidos).
// Só do PC: voltar no topo da pausa (o 99 que Manager_SendMessage 0x5f manda com a pilha vazia) fecha a pausa como o
// Start: Esc (voltar nos menus) fecha a pausa que ele abriu, como na pausa Modern. No PS2 o triângulo no topo não faz
// nada; voltar da pergunta de RESTART/QUIT só fecha a pergunta, como no jogo.
int32_t pause_page(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b) {
    if (message == 0x4c && is_pc_page(uint32_t(b))) {
        refresh_pause_row(index);
        return 1;
    }
    if (message == 0x6b) g_pause_back_from_question = pause_question_open(index);
    if (message == 99 && MenuManager_GetStatus() == 3) {
        if (!g_pause_back_from_question) {
            // O MenuManager_Monitor deste tick fecha pela ação de fechar (Start), que não vale com o voltar ligado.
            const int16_t player = mgr(index)->player;
            Input_ClearAction(player, 0x22);
            Input_SetAction(player, mgr(index)->close_action, kActionDown | kActionPressed);
            ++g_menu_pc_stats.back_closes;
        }
        g_pause_back_from_question = false;
    }
    const int32_t r = g_prev_pause_page != nullptr ? g_prev_pause_page(index, c, id, message, a, b) : 0;
    if (message == 0x4c) {
        add_pause_row(index);
    } else if (message == 0x50) {
        refresh_pause_row(index);
    }
    return r;
}

// C_GCPAUSE_Handler: escolher a linha nova abre a página de opções; o resto é do jogo.
int32_t pause_ctrl(uint8_t index, M_CONTROL* c, uint32_t id, uint32_t message, int32_t a, int32_t b) {
    if (message == 0x4b && c != nullptr && c->type == MCT_LIST && c->tag == 6 && __Menu_SendMessage(c, 0x34, 0, 0) == kPcPauseRow) {
        // O estilo Modern abre por cima da pausa; senão, a página Original.
        if (g_options_hook != nullptr && g_options_hook(index)) return 1;
        if (MenuPc_ShowPage(index, kPcOptionsPage)) return 1;
    }
    return g_prev_pause_ctrl != nullptr ? g_prev_pause_ctrl(index, c, id, message, a, b) : 0;
}

}  // namespace

// ---- a montagem das linhas ----

M_CONTROL* MenuPc_AddRow(M_MANAGER* m, PcTab t, uint32_t id, PcRowKind kind, int32_t lo, int32_t hi) {
    TabEntry& tab = g_tabs[int(t)];
    if (tab.count >= kMaxRows) return nullptr;
    const int slot = tab.count;
    RowEntry& r = tab.rows[slot];
    r = RowEntry{};
    r.id = id;
    r.kind = kind;
    r.name[0] = ' ';
    r.value[0] = ' ';
    ++tab.count;
    const int16_t y = int16_t(kRowTop + std::min(slot, kPcRowsVisible - 1) * kRowPitch);
    M_CONTROL* ctl = nullptr;
    switch (kind) {
        case PcRowKind::Info:
            add_label(m, id, 0, kNameX, y, kNameW, kRowH, kRowFormat, r.name, kOn, kRowOff);
            ctl = add_label(m, id, 1, kInfoX, y, kInfoW, kRowH, kRadioFormat, r.value, kOn, kRowOff);
            break;
        case PcRowKind::Slider: {
            add_label(m, id, 0, kNameX, y, kNameW, kRowH, kRowFormat, r.name, kOn, kRowOff);
            // A barra de volume do "AV Options" (proporcional: o preenchimento vai até o valor).
            ctl = MenuPc_AddControl(m, id, MCT_SCROLL, kCtlX, y, kCtlW, kRowH, 25, kPcComponentSet, 0, uintptr_t(lo),
                                    uintptr_t(hi));
            send(ctl, 0x69, 0);
            break;
        }
        case PcRowKind::Choice:
            add_label(m, id, 0, kNameX, y, kNameW, kRowH, kRowFormat, r.name, kOn, kRowOff);
            // O rádio do "AV Options": o texto da opção centrado, as setas com o foco.
            ctl = MenuPc_AddControl(m, id, MCT_RADIO, kCtlX, y, kCtlW, kRowH, 25, kSetRadio);
            send(ctl, 0x23, val(kRadioFormat));
            send(ctl, 0x1b, int32_t(kOn));
            send(ctl, 0x1c, int32_t(kRowOff));
            send(ctl, 0x62, 0);
            break;
        case PcRowKind::Button:
            // Os botões das páginas de opções do jogo ("Restore Defaults"): só o texto, na coluna do nome.
            // As cores antes do texto: o 0x18 já atualiza o rótulo, e o primeiro update sem foco desbota para a
            // cor sem foco daquele momento (Label_Update; com a de Label_Init, o texto ficaria cinza).
            ctl = MenuPc_AddControl(m, id, MCT_BUTTON, kNameX, y, kNameW, kRowH, 25, kSetText);
            send(ctl, 0x23, val(kRowFormat));
            send(ctl, 0x1b, int32_t(kOn));
            send(ctl, 0x1c, int32_t(kRowOff));
            send(ctl, 0x62, 0);
            send(ctl, 0x18, val(r.name));
            break;
        case PcRowKind::List: {
            // A lista com moldura e barra de setas (conjunto 3, como as listas do menu principal).
            ctl = MenuPc_AddControl(m, id, MCT_LIST, kAreaX, kAreaY, kAreaW, kAreaH, 25, kSetRadio, 0, 1);
            if (ctl != nullptr) {
                M_LISTBOX* l = reinterpret_cast<M_LISTBOX*>(ctl);
                l->scroll.wrap = 0;
                send(ctl, 0x1b, int32_t(kOn));
                send(ctl, 0x1c, int32_t(kRowOff));
                send(ctl, 0x29, int32_t(kRowSelected));
                send(ctl, 0x62, 0);
            }
            (void)lo;
            break;
        }
        case PcRowKind::Status:
            ctl = add_label(m, id, 0, kAreaX, kStatusY, kAreaW, kRowH, kRadioFormat, r.value, kOn, kOn);
            break;
    }
    // Nasce escondida: layout_rows mostra as da aba aberta.
    for (M_CONTROL* c = static_cast<M_CONTROL*>(ll_first(m->page->controls)); c != nullptr; c = c->next) {
        if (c->id == id) __Menu_SendMessage(c, 0x2b, 1, 0);
    }
    return ctl;
}

M_CONTROL* MenuPc_Control(uint8_t index, uint32_t id) {
    const RowEntry* r = find_row(id);
    M_PAGE* page = options_page(index);
    return r != nullptr ? row_control(page, *r) : nullptr;
}

void MenuPc_SetRowName(uint8_t index, uint32_t id, const std::string& utf8) {
    RowEntry* r = find_row(id);
    if (r == nullptr) return;
    copy_text(r->name, sizeof(r->name), utf8);
    M_PAGE* page = options_page(index);
    if (r->kind == PcRowKind::Button) {
        send(row_control(page, *r), 0x18, val(r->name));
    } else {
        send(row_name(page, *r), 0x18, val(r->name));
    }
}

void MenuPc_SetRowValue(uint8_t index, uint32_t id, const std::string& utf8) {
    RowEntry* r = find_row(id);
    if (r == nullptr) return;
    copy_text(r->value, sizeof(r->value), utf8);
    if (r->kind == PcRowKind::Info || r->kind == PcRowKind::Status) send(row_control(options_page(index), *r), 0x18, val(r->value));
}

void MenuPc_SetChoices(uint8_t index, uint32_t id, const std::vector<std::string>& options, int32_t selected) {
    M_CONTROL* c = MenuPc_Control(index, id);
    if (c == nullptr || c->type != MCT_RADIO) return;
    M_RADIO* r = reinterpret_cast<M_RADIO*>(c);
    Radio_SendMessage(r, 0x17, 0, 0);
    for (size_t i = 0; i < options.size(); ++i) {
        char buf[0x80];
        copy_text(buf, sizeof(buf), options[i]);
        Radio_SendMessage(r, 0x10, val(buf), int32_t(i));
    }
    Radio_SendMessage(r, 0x1e, selected, 1);
}

int32_t MenuPc_Choice(uint8_t index, uint32_t id) {
    M_CONTROL* c = MenuPc_Control(index, id);
    if (c == nullptr || c->type != MCT_RADIO) return -1;
    return Radio_SendMessage(reinterpret_cast<M_RADIO*>(c), 0x35, 0, 0);
}

void MenuPc_SetSlider(uint8_t index, uint32_t id, int32_t pos) {
    M_CONTROL* c = MenuPc_Control(index, id);
    if (c == nullptr || c->type != MCT_SCROLL) return;
    M_SCROLL* s = reinterpret_cast<M_SCROLL*>(c);
    s->pos = std::clamp(pos, s->min, s->max);  // sem o 0x2e, que avisaria o dono
}

int32_t MenuPc_Slider(uint8_t index, uint32_t id) {
    M_CONTROL* c = MenuPc_Control(index, id);
    if (c == nullptr || c->type != MCT_SCROLL) return 0;
    return Scroll_SendMessage(reinterpret_cast<M_SCROLL*>(c), 0x40, 0, 0);
}

void MenuPc_SetRowEnabled(uint8_t index, uint32_t id, bool on) {
    PcTab t = PcTab::Video;
    RowEntry* r = find_row(id, &t);
    if (r == nullptr || r->enabled == on) return;
    r->enabled = on;
    if (t == g_current) layout_rows(index);
}

// ---- páginas, abas e o resto ----

M_CONTROL* MenuPc_AddControl(M_MANAGER* m, uint32_t id, uint8_t type, int16_t x, int16_t y, int16_t w, int16_t h,
                             uint16_t depth, uint16_t set, uint32_t tag, uintptr_t a, uintptr_t b) {
    // Como um registro 0xfffffff9 em MenuManager_Create.
    alignas(4) uint8_t record[0x58]{};
    NEW_CONTROL& desc = *reinterpret_cast<NEW_CONTROL*>(record);
    desc.id = id;
    record[0x21] = type;
    record[0x20] = type == MCT_SCROLL ? 1 : 0;
    desc.x = x;
    desc.y = y;
    desc.w = w;
    desc.h = h;
    desc.depth = depth;
    desc.unknown_0c = tag;
    desc.owner = set;
    desc.format = reinterpret_cast<char*>(a);
    desc.unknown_08 = uint32_t(b);
    if (type != MCT_WINDOW) FixupResolution(&desc);
    M_CONTROL* c = ptr<M_CONTROL>(Manager_SendMessage(m, 0x42, val(&desc), 0));
    if (c != nullptr) {
        for (M_SCRIPT*& s : c->defaults) s = nullptr;
    }
    return c;
}

void MenuPc_EnsureComponents(uint8_t index) {
    for (M_COMPONENTSET* s = static_cast<M_COMPONENTSET*>(ll_first(component_list)); s != nullptr; s = s->next) {
        if (s->id == kPcComponentSet) return;
    }
    // As peças do conjunto 10 do 08000002 (a barra de volume do "AV Options"), todas da textura 0x03000042:
    // 6 as setas esquerda/direita (aparecem com o foco, acesas ao apertar), 7 as setas cima/baixo, 8 o
    // preenchimento laranja que estica até o valor.
    M_COMPONENTSET* set = Component_AddSet(index, kPcComponentSet);
    for (M_COMPONENT& c : set->items) c = M_COMPONENT{};
    MenuSpriteInfo* p = Component_AddComponent(&set->items[6], 0, 0, 0, 0, 0x03000042, 4);
    Component_InitInstance(&p[0], 0, 0, 16, 16, 48, 0, 16, 16, 0xa8, 0x6f6f6fdf, 0.0f, 0.0f);
    Component_InitInstance(&p[1], 0, 0, 16, 16, 48, 16, 16, 16, 0x6a, 0x6f6f6fdf, 0.0f, 0.0f);
    Component_InitInstance(&p[2], 0, 0, 16, 16, 48, 0, 16, 16, 0x48, 0x7f7f7fff, 0.0f, 0.0f);
    Component_InitInstance(&p[3], 0, 0, 16, 16, 48, 16, 16, 16, 0x8a, 0x7f7f7fff, 0.0f, 0.0f);
    p = Component_AddComponent(&set->items[7], 0, 0, 0, 0, 0x03000042, 6);
    Component_InitInstance(&p[0], 0, 0, 16, 16, 16, 0, 16, 16, 0x14, 0x606060c0, 0.0f, 0.0f);
    Component_InitInstance(&p[1], 0, 0, 16, 16, 32, 0, 16, 16, 0x15, 0x606060c0, 0.0f, 0.0f);
    Component_InitInstance(&p[2], 0, 0, 16, 16, 16, 0, 16, 16, 0xa4, 0x6f6f6fdf, 0.0f, 0.0f);
    Component_InitInstance(&p[3], 0, 0, 16, 16, 32, 0, 16, 16, 0x65, 0x6f6f6fdf, 0.0f, 0.0f);
    Component_InitInstance(&p[4], 0, 0, 16, 16, 16, 0, 16, 16, 0x44, 0x7f7f7fff, 0.0f, 0.0f);
    Component_InitInstance(&p[5], 0, 0, 16, 16, 32, 0, 16, 16, 0x85, 0x7f7f7fff, 0.0f, 0.0f);
    p = Component_AddComponent(&set->items[8], 0, 0, 0, 0, 0x03000042, 1);
    Component_InitInstance(&p[0], 0, 0, 8, 16, 54, 50, 2, 2, 0xfc, 0x7f7f7fff, 1.0f, 0.0f);
}

void MenuPc_RegisterTab(PcTab tab, const MenuPcTab& desc) {
    TabEntry& t = g_tabs[int(tab)];
    t.desc = desc;
    t.registered = desc.build != nullptr;
}

const char* MenuPc_TabName(PcTab tab) {
    static const char* const kNames[kPcTabCount] = {"video", "graphics", "interface", "audio", "controls", "keys"};
    return int(tab) < kPcTabCount ? kNames[int(tab)] : "";
}

bool MenuPc_ParseTab(const std::string& name, PcTab* out) {
    for (int t = 0; t < kPcTabCount; ++t) {
        if (name == MenuPc_TabName(PcTab(t))) {
            *out = PcTab(t);
            return true;
        }
    }
    return false;
}

void MenuPc_RegisterPage(uint32_t page, MenuPcBuilder build, uint32_t focus) {
    if (build == nullptr) {
        page_table().erase(page);
    } else {
        page_table()[page] = PageEntry{build, focus};
    }
}

bool MenuPc_PageAvailable(uint8_t index, uint32_t page) {
    if (page_table().count(page) != 0) return true;
    return index < kMenuManagers && menu_manager[index].active != 0 && find_page(mgr(index), page) != nullptr;
}

bool MenuPc_ShowPage(uint8_t index, uint32_t page) {
    if (index >= kMenuManagers) return false;
    M_MANAGER* m = mgr(index);
    if (m->active == 0 || m->page == nullptr) return false;
    const auto it = page_table().find(page);
    M_PAGE* p = find_page(m, page);
    if (p == nullptr) {
        if (it == page_table().end()) return false;
        // 0x43 cria a página e a põe como atual (sem eventos, escondendo a de agora); os controles entram
        // nela pela 0x42; depois a de agora volta a ser a atual, para o 0x44 abaixo empilhá-la.
        M_PAGE* cur = m->page;
        M_PAGE* prev = m->previous;
        const uint8_t transition = m->transition;
        p = ptr<M_PAGE>(Manager_SendMessage(m, 0x43, int32_t(page), 0));
        if (p == nullptr) return false;
        it->second.build(index, m);
        p->base.state = 1;
        m->page = cur;
        m->previous = prev;
        m->transition = transition;
        cur->base.state = 0;
        ++g_menu_pc_stats.built;
    }
    Manager_SendMessage(m, 0x44, int32_t(page), 0);
    const uint32_t focus = it != page_table().end() ? it->second.focus : 0;
    if (focus != 0) Manager_SendMessage(m, 0x22, int32_t(focus), 0);
    ++g_menu_pc_stats.shown;
    return true;
}

bool MenuPc_ShowTab(uint8_t index, PcTab tab) {
    if (!g_tabs[int(tab)].registered) return false;
    g_requested = tab;
    if (Menu_CurrentPage(index) == kPcOptionsPage) {
        show_tab(index, tab);
        Manager_SendMessage(mgr(index), 0x22, int32_t(kPcTabFirst + uint32_t(tab)), 0);
        return true;
    }
    return MenuPc_ShowPage(index, kPcOptionsPage);
}

PcTab MenuPc_CurrentTab() { return g_current; }

void MenuPc_SetOptionsHook(MenuPcOptionsHook hook) { g_options_hook = hook; }

void MenuPc_Install() {
    const MenuHandlerEntry* e = Menu_FindHandler(0x4000004b);
    if (e == nullptr || e->handler != pause_page) {
        g_prev_pause_page = e != nullptr ? e->handler : nullptr;
        Menu_PortRegisterHandler(0x4000004b, "P_PAUSE_Handler (PC)", pause_page);
    }
    e = Menu_FindHandler(0x10000028);
    if (e == nullptr || e->handler != pause_ctrl) {
        g_prev_pause_ctrl = e != nullptr ? e->handler : nullptr;
        Menu_PortRegisterHandler(0x10000028, "C_GCPAUSE_Handler (PC)", pause_ctrl);
    }
    Menu_PortRegisterHandler(kPcOptionsPage, "P_PCOPTIONS_Handler", options_page_handler);
    for (int t = 0; t < kPcTabCount; ++t) Menu_PortRegisterHandler(kPcTabFirst + uint32_t(t), "C_PCTAB_Handler", tab_button);
    for (const uint32_t id : {kPcRowResolution, kPcRowWindowMode, kPcRowVSync, kPcRowFov, kPcRowFpsCap,
                              kPcRowInterpolation, kPcRowInstantMouse, kPcRowApplyVideo,
                              kPcRowRenderScale, kPcRowAntiAliasing, kPcRowFilter, kPcRowHdTextures, kPcRowDlss,
                              kPcRowApplyGraphics, kPcRowHdUi, kPcRowHudAnchor, kPcRowLanguage, kPcRowMenuStyle,
                              kPcRowSubtitles, kPcRowApplyInterface}) {
        Menu_PortRegisterHandler(id, "C_PCVIDEO_Handler", video_row);
    }
    MenuPc_RegisterPage(kPcOptionsPage, build_options, 0);
    MenuPcTab video;
    video.build = build_video;
    video.enter = video_enter;
    MenuPc_RegisterTab(PcTab::Video, video);
    MenuPcTab graphics;
    graphics.build = build_graphics;
    graphics.enter = graphics_enter;
    MenuPc_RegisterTab(PcTab::Graphics, graphics);
    MenuPcTab ui;
    ui.build = build_interface;
    ui.enter = interface_enter;
    MenuPc_RegisterTab(PcTab::Interface, ui);
    MenuPcTab audio;
    audio.build = build_audio;
    audio.enter = audio_enter;
    MenuPc_RegisterTab(PcTab::Audio, audio);
}

void MenuPc_Frame(bool hd_text) {
    PcText_SetHdText(hd_text);
    nf::VideoController& vc = nf::video_controller();
    PcText_SetLanguage(vc.live().language);
    // O menu fechou (Start) com a confirmação aberta: a configuração anterior volta.
    if (vc.confirming() && MenuManager_GetStatus() == 0) {
        vc.revert();
        g_box_open = false;
        g_box_seconds = -1;
    }
}

int MenuPc_Describe(char* out, size_t size) {
    const nf::VideoController& vc = nf::video_controller();
    const auto settings = [](const nf::VideoSettings& v, char* buf, size_t n) {
        std::snprintf(buf, n,
                      "%dx%d %s vsync %d escala %d msaa %d filtro %s fov %d hd_tex %d hd_ui %d bordas %d idioma %s menus %s "
                      "limite %d interp %d mouse %d",
                      v.width, v.height, nf::window_mode_name(v.window_mode), v.vsync ? 1 : 0, v.render_scale, v.msaa,
                      nf::texture_filter_name(v.filter), fov_int(v.fov), v.hd_textures ? 1 : 0, v.hd_ui ? 1 : 0,
                      v.hud_edges ? 1 : 0, v.language.c_str(), nf::menu_style_name(v.menu_style), v.fps_cap,
                      v.interpolation ? 1 : 0, v.instant_mouse_look ? 1 : 0);
    };
    char pending[256], live[256];
    settings(g_pending, pending, sizeof(pending));
    settings(vc.live(), live, sizeof(live));
    uint32_t focus = 0, tag = 0;
    Menu_Focus(0, &focus, &tag);
    int row = -1;
    if (Menu_CurrentPage(0) == kPcOptionsPage) {
        const TabEntry& t = g_tabs[int(g_current)];
        for (int i = 0; i < t.count; ++i) {
            if (t.rows[i].id == focus) row = i;
        }
    }
    return std::snprintf(out, size,
                         "pc_opcoes pagina %08x aba %s abas %d foco %08x linha %d topo %d caixa %d confirmando %d resta "
                         "%.1f aplicadas %u gravadas %u mantidas %u desfeitas %u | pendente %s | em_vigor %s",
                         Menu_CurrentPage(0), MenuPc_TabName(g_current), g_show_audio ? 6 : 5, focus, row,
                         g_tabs[int(g_current)].top, g_box_open ? 1 : 0, vc.confirming() ? 1 : 0, double(vc.remaining()),
                         vc.stats().applied, vc.stats().stored, vc.stats().kept, vc.stats().reverted, pending, live);
}

void menu_pc_debug_power_on() {
    page_table().clear();
    g_prev_pause_page = nullptr;
    g_prev_pause_ctrl = nullptr;
    g_options_hook = nullptr;
    for (TabEntry& t : g_tabs) t = TabEntry{};
    g_current = PcTab::Video;
    g_requested = PcTab::Video;
    g_show_audio = true;
    g_help[0] = 0;
    g_text_generation = 0;
    g_last_list_index = -1;
    g_help_text = PcText::Count;
    g_pending = nf::VideoSettings{};
    g_box_open = false;
    g_box_seconds = -1;
    g_box_text[0] = 0;
    g_pause_generation = 0;
    g_pause_back_from_question = false;
    g_res_choices.clear();
    g_scale_choices.clear();
    g_msaa_choices.clear();
    g_menu_pc_stats = MenuPcStats{};
}
