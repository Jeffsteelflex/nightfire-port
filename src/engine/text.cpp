// Port das mensagens e legendas (Text_*, 0x001b09f0-0x001b1700), da caixa de texto dos menus
// (0x00222b80-0x00224110) e de CirBuf_*. Ver text.h.
//
// Divergências:
// - Text_AddMsg: sem a câmera 4 (glb_viewer[4] nulo) a legenda conta como fora de cena (o original leria
//   o endereço 0); um tipo fora de 0..6 é recusado (o original leria além de TextMsgFormats); as duas
//   linhas de uma legenda de tipo 5 não passam de um buffer do StringHeap (strcat no original).
// - Text_UpdateMsg: jogador fora de glb_viewer conta como câmera ausente (o original leria além dela).
// - Text_AddMsg sem nó livre que possa criar (fila sem Text_Init, como nos testes de outros módulos, ou
//   heap cheio) descarta a mensagem e conta em g_text_stats.no_node; o original ficaria em laço.
// - Text_Update/Text_Update2Line/Text_FlushAllSubtitles: sem os sprites de legenda (SpriteList cheia na
//   criação) não fazem nada com eles (o original escreveria no endereço 0x38/0x39).
// - Caixa de texto dos menus: sem os dados do rótulo (Label_Init ainda é stub) o nome digitado não é
//   ligado nem editado (o original seguiria o ponteiro em +0x17c).
// - O construtor estático de SubtitlesInfo roda como inicialização estática do C++ (no PS2,
//   __do_global_ctors antes do main).
// Só no PC: os contadores de g_text_stats.

#include "engine/text.h"

#include <cstring>

#include "engine/font.h"
#include "engine/gameflow.h"
#include "engine/memory.h"
#include "engine/modules.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "engine/script.h"
#include "engine/game.h"
#include "engine/vecmath.h"
#include "engine/txt.h"
#include "engine/view.h"
#include "platform/psi.h"

int32_t CompMsgTxt(const void* a, const void* b);  // engine/sort.cpp

namespace {
// Formatos de TextMsgFormats (0x0030cdb0 e 0x0030cdb8) e o da caixa de texto (0x0030d668).
char kFmtSerpCentre[] = "\xff\x02\xfe\x03";
char kFmtNFontCentre[] = "\xff\x01\xfe\x03";
char kFmtSerp[] = "\xff\x02";
char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";  // 0x0030a360
}  // namespace

TXTMSGFORMAT_tag TextMsgFormats[kTextMsgTypes] = {
    {nullptr, 0, 0},
    {kFmtSerpCentre, 0x1e0, 0},
    {kFmtSerpCentre, 0x1e0, 0},
    {kFmtNFontCentre, 0x1e0, 0},
    {kFmtSerpCentre, 0x1e0, 0},
    {kFmtSerpCentre, 0x1cc, 0},
    {kFmtSerpCentre, 0x100, 0},
};
SpriteInfo SubtitlesInfo;
DLISTINFO_tag MsgQueue;
CIRBUF_INFO ReceivedMsgs;
int32_t ReceivedBuf[0x80];
TXT_MSG* SubTitleMsg = nullptr;
sprite* SubTitleSpr = nullptr;
sprite* SubTitleSpr1 = nullptr;
sprite* SubTitleSpr2 = nullptr;
char* SubTitleStr = nullptr;
char* SubTitleStr2 = nullptr;
TextStats g_text_stats;
SND2LBL_tag Snd2Lbl[kSnd2Lbl];
uint8_t SFXOutputData[kSfxOutputCount * 0x18];
float g_fSubtitleDurationModification = 0.92f;

namespace {

// [decomp] 0x001b1338 __static_initialization_and_destruction_0
// O construtor estático do módulo (chamado por _GLOBAL_$I$SubTitleStr com 1, 0xffff): a descrição dos
// sprites de legenda. Texto (rótulo 0x02000004, um espaço) bege 0x7d6d59ff com contorno 0x001400ff
// (SPRF_OUTLINE), escondido (prioridade 0xff), em (256, 426), formato de TextMsgFormats[4], câmera 5.
void __static_initialization_and_destruction_0(int32_t initialize, int32_t priority) {
    if (priority != 0xffff || initialize == 0) return;
    SpriteInfo info{};
    info.colour = 0x7d6d59ffu;
    info.colour2 = 0x001400ffu;
    info.priority = 0xffff;
    info.flags = 0x2000;
    info.x = 0x100;
    info.y = 0x1aa;
    info.label = 0x2000004;
    info.format = TextMsgFormats[4].format;
    info.viewer = 5;
    SubtitlesInfo = info;
}
const bool kSubtitlesInfoBuilt = (__static_initialization_and_destruction_0(1, 0xffff), true);

// __fixunssfsi (fptoui na libgcc do PS2): float para inteiro sem sinal, truncando.
uint32_t fptoui(float a) {
    if (a >= 2147483648.0f) return uint32_t(int32_t(a - 2147483648.0f)) + 0x80000000u;
    return uint32_t(int32_t(a));
}

// (float) de um inteiro sem sinal como o GCC do PS2 faz (cvt.s.w com o bit alto dividido).
float u32_to_float(uint32_t v) {
    if (int32_t(v) >= 0) return float(int32_t(v));
    return float(int32_t((v >> 1) | (v & 1))) * 2.0f;
}

TXT_MSG* first_msg() { return reinterpret_cast<TXT_MSG*>(MsgQueue.used.first); }
TXT_MSG* next_msg(const TXT_MSG* m) { return reinterpret_cast<TXT_MSG*>(m->node.next); }

// Um nó livre que nunca foi usado (tipo 0); sem nenhum, mais 32. Comportamento original preservado: os
// nós devolvidos por Text_DelMsg guardam o tipo e não são mais aproveitados. Só no port: com a fila sem
// Text_Init (nós de tamanho 0) ou sem memória para mais nós, devolve nulo (o original ficaria em laço).
TXT_MSG* take_node() {
    for (;;) {
        for (LLNODE_tag* n = MsgQueue.free.first; n != nullptr; n = n->next) {
            if (reinterpret_cast<TXT_MSG*>(n)->type == 0) {
                return reinterpret_cast<TXT_MSG*>(LList_Remove(&MsgQueue.free, n));
            }
        }
        const uint16_t before = MsgQueue.free.count;
        if (MsgQueue.free.node_size != sizeof(TXT_MSG)) break;
        DList_AllocnNodes(&MsgQueue, 0x20);
        if (MsgQueue.free.count == before) break;
    }
    ++g_text_stats.no_node;
    return nullptr;
}

// A primeira mensagem da fila do jogador e do tipo.
TXT_MSG* find_msg(int8_t player, int32_t type) {
    for (TXT_MSG* m = first_msg(); m != nullptr; m = next_msg(m)) {
        if (m->player == player && uint32_t(type) == m->type) return m;
    }
    return nullptr;
}

// A câmera de maior número até a 5 que desenha sprites (a das legendas); -1 nenhuma.
int32_t subtitle_viewer() {
    for (int32_t i = 5; i >= 0; --i) {
        const viewer_tag* v = glb_viewer[i];
        if (v != nullptr && v->sprites != 0) return i;
    }
    return -1;
}

}  // namespace

void text_debug_power_on() {
    std::memset(&MsgQueue, 0, sizeof(MsgQueue));
    std::memset(&ReceivedMsgs, 0, sizeof(ReceivedMsgs));
    std::memset(ReceivedBuf, 0, sizeof(ReceivedBuf));
    SubTitleMsg = nullptr;
    SubTitleSpr = SubTitleSpr1 = SubTitleSpr2 = nullptr;
    SubTitleStr = SubTitleStr2 = nullptr;
    g_text_stats = TextStats{};
}

// [decomp] 0x001ccfb8 CirBuf_Init
// "size" palavras em "buffer" (sem ele, no heap), zeradas.
uint32_t CirBuf_Init(CIRBUF_INFO* info, uint16_t size, int32_t* buffer) {
    if (info == nullptr) return 0;
    info->unknown_0a = 0;
    info->count = 0;
    info->tail = 0;
    info->head = 0;
    info->buffer = buffer;
    info->size = size;
    if (buffer == nullptr && size != 0) info->buffer = static_cast<int32_t*>(Mem_Malloc(uint32_t(size) << 2, 0x1b04, 0));
    std::memset(info->buffer, 0, size_t(size) << 2);
    return 1;
}

// [decomp] 0x001cd040 CirBuf_Add
// Guarda "value" na cabeça e devolve o que estava lá (o mais antigo quando cheio).
int32_t CirBuf_Add(CIRBUF_INFO* info, int32_t value) {
    if (info == nullptr) return 0;
    info->count = uint32_t(info->count) + 1 < info->size ? uint16_t(info->count + 1) : info->size;
    int32_t* slot = &info->buffer[info->head];
    const int32_t old = *slot;
    *slot = value;
    const uint16_t head = uint16_t(info->head + 1);
    info->head = head;
    if (info->size <= head) info->head = 0;
    if (info->tail == info->head) ++info->tail;
    if (info->size <= info->tail) info->tail = 0;
    return old;
}

// [decomp] 0x001b09f0 Text_Update2Line
// A legenda de duas linhas (tipo 5, fora de cena com câmera): a mensagem é quebrada de novo em 60% da
// largura e na largura toda, nos sprites SubTitleSpr1 (y 402) e SubTitleSpr2 (y 422), centrados em x 256.
void Text_Update2Line() {
    if (Text_UpdateMsg(-1, 5, &SubTitleMsg) != 0 && SubTitleMsg != nullptr && SubTitleSpr1 != nullptr &&
        SubTitleSpr2 != nullptr) {
        char* cursor = SubTitleMsg->text;
        SubTitleStr = Font_WordWrapString(&cursor, uint16_t(std::strlen(cursor)), kFmtSerpCentre,
                                          fptoui(float(TextMsgFormats[5].width) * 0.6f), nullptr);
        Txt_LockString(SubTitleStr);
        Sprite_SetParams(SubTitleSpr1, 0, 0x100, 0x192, 0, 0x7d6d59ffu);
        Sprite_SetTextNFmt(SubTitleSpr1, SubTitleStr, kFmtSerpCentre, 0);
        int32_t v = subtitle_viewer();
        if (v >= 0) SubTitleSpr1->viewer = uint8_t(v);
        if (cursor == nullptr) {
            SubTitleSpr2->priority = -1;
        } else {
            SubTitleStr2 = Font_WordWrapString(&cursor, uint16_t(std::strlen(cursor)), kFmtSerpCentre,
                                               TextMsgFormats[5].width, nullptr);
            if (SubTitleStr2 != nullptr) {
                Txt_LockString(SubTitleStr2);
                Sprite_SetParams(SubTitleSpr2, 0, 0x100, 0x1a6, 0, 0x7d6d59ffu);
                Sprite_SetTextNFmt(SubTitleSpr2, SubTitleStr2, kFmtSerpCentre, 0);
                v = subtitle_viewer();
                if (v >= 0) SubTitleSpr2->viewer = uint8_t(v);
            }
        }
    }
    if (SubTitleMsg == nullptr) {
        if (SubTitleSpr1 != nullptr) SubTitleSpr1->priority = -1;
        if (SubTitleSpr2 != nullptr) SubTitleSpr2->priority = -1;
    }
}

// [decomp] 0x001b0c30 Text_AddMsg
// Uma mensagem do jogador (até 3; -1 para legendas) com prioridade de fila "priority", do tipo dado, com
// o texto e "time" ticks de tela. Legenda (4) vira 5 fora de cena com câmera (câmera 4 desligada, sem
// filme); prioridade 0xff numa legenda a força mesmo com as legendas desligadas (DrawInfo[2]). No
// multijogador o item pego (6) vira informação (1), e uma informação nova divide o tempo das que o
// jogador já tem. Uma situação da missão (3) encerra os objetivos (2) na tela. Os tipos 0, 2 e 3 vão
// para o histórico (ReceivedMsgs). Sem texto, a mensagem leva só "data". O tempo de cada linha é a
// parte dela no tempo total (arredondada); fora das legendas, no mínimo 60 ticks.
void Text_AddMsg(int8_t player, uint8_t priority, int32_t type, char* text, uint32_t data, int16_t time) {
    uint8_t prio = priority;
    bool forced = false;
    if (type == 4) {
        if (priority == 0xff) {
            prio = 0;
            forced = true;
        }
        const viewer_tag* scene = glb_viewer[4];
        if ((scene == nullptr || scene->active == 0) && psiMovieFinished()) type = 5;
    }
    uint32_t multiplayer;
    std::memcpy(&multiplayer, MPSettings + 0x180, 4);
    if (multiplayer != 0) {
        if (type == 6) type = 1;
        if (player != -1 && player < 4 && type == 1) {
            int32_t n = 0;
            for (TXT_MSG* m = first_msg(); m != nullptr; m = next_msg(m)) {
                if (m->player == player && uint32_t(type) == m->type) ++n;
            }
            ++n;
            if (n != 1) {
                for (TXT_MSG* m = first_msg(); m != nullptr; m = next_msg(m)) {
                    if (m->player == player && uint32_t(type) == m->type) m->time = m->time / float(n);
                }
            }
        }
    }
    if (type == 3) {
        for (TXT_MSG* m = first_msg(); m != nullptr; m = next_msg(m)) {
            if (m->type == 2) m->time = -1.0f;
        }
    }
    if (!(player < 4) || (!forced && DrawInfo[2] == 0 && uint32_t(type - 4) < 2)) {
        ++g_text_stats.refused;
        return;
    }
    if (type < 0 || type >= kTextMsgTypes) {  // só no port: o original leria além de TextMsgFormats
        ++g_text_stats.refused;
        return;
    }
    ++g_text_stats.added[type];
    if (text == nullptr) {
        TXT_MSG* m = take_node();
        if (m == nullptr) return;
        m->data = data;
        m->time = float(time);
        m->priority = int8_t(prio);
        m->type = uint8_t(type);
        m->player = player;
        m->text = nullptr;
        m->length = 0;
        m->next = nullptr;
        LList_InsertAt(&MsgQueue.used, &m->node, 0, &CompMsgTxt);
        return;
    }
    if (type != 1 && (type <= 0 || type >= 7 || type < 4)) {
        Txt_LockString(text);
        char* old = reinterpret_cast<char*>(uintptr_t(uint32_t(CirBuf_Add(&ReceivedMsgs, int32_t(uintptr_t(text))))));
        Txt_UnlockString(old);
    }
    const uint32_t total = uint32_t(std::strlen(text)) & 0xffff;
    const bool subtitle = uint32_t(type - 4) < 2;
    char* cursor = text;
    TXT_MSG* previous = nullptr;
    do {
        char* line;
        if (type == 5) {
            line = Font_WordWrapString(&cursor, uint16_t(std::strlen(cursor)), TextMsgFormats[5].format,
                                       fptoui(float(TextMsgFormats[5].width) * 0.6f), nullptr);
            if (line == nullptr) return;
            if (cursor != nullptr) {
                char* second = Font_WordWrapString(&cursor, uint16_t(std::strlen(cursor)), TextMsgFormats[5].format,
                                                   fptoui(float(TextMsgFormats[5].width) * 0.4f), nullptr);
                if (second != nullptr) {
                    const size_t room = size_t(kStringHeapSize) - 1 - std::strlen(line);
                    std::strncat(line, second, room);
                }
            }
        } else {
            line = Font_WordWrapString(&cursor, uint16_t(std::strlen(cursor)), TextMsgFormats[type].format,
                                       TextMsgFormats[type].width, nullptr);
        }
        if (line == nullptr) return;
        TXT_MSG* m = take_node();
        if (m == nullptr) return;
        m->data = 0;
        m->text = line;
        m->priority = int8_t(prio);
        Txt_LockString(line);
        m->length = uint16_t(std::strlen(m->text));
        if (time > 0) {
            const float share = float(time) * (float(m->length) / float(total)) + 0.5f;
            m->time = float(int16_t(int32_t(share)));
            if (!subtitle) {
                const uint32_t ticks = fptoui(m->time);
                m->time = u32_to_float(0x3b < ticks ? ticks : 0x3c);
            }
        } else {
            m->time = float(time);
        }
        m->type = uint8_t(type);
        m->player = player;
        m->next = nullptr;
        if (previous == nullptr) {
            LList_InsertAt(&MsgQueue.used, &m->node, 0, &CompMsgTxt);
        } else {
            LList_Insert(&MsgQueue.used, &previous->node, &m->node);
            previous->next = m;
        }
        ++g_text_stats.lines;
        previous = m;
    } while (cursor != nullptr);
}

// [decomp] 0x001b11d8 Text_UpdateMsg
// A mensagem atual (*msg) do jogador e do tipo: desconta FRAME_RATE_MUL do tempo; acabado o tempo, apaga
// e passa para a próxima linha dela, ou para a primeira da fila desse jogador e tipo. Devolve 1 quando
// *msg mudou (ou estava vazia e foi procurada), 0 se continua a mesma ou se a câmera do jogador não
// desenha sprites.
uint32_t Text_UpdateMsg(int8_t player, int32_t type, TXT_MSG** msg) {
    if (player != -1) {
        const viewer_tag* v = player >= 0 && player < kViewerCount ? glb_viewer[player] : nullptr;
        if (v != nullptr && v->sprites == 0) return 0;
    }
    TXT_MSG* m = *msg;
    if (m == nullptr) {
        Text_DelMsg(nullptr);
        *msg = find_msg(player, type);
        return 1;
    }
    if (0.0f < m->time) m->time = m->time - FRAME_RATE_MUL;
    if (!(m->time <= 0.0f)) return 0;
    TXT_MSG* next = m->next;
    Text_DelMsg(m);
    *msg = next != nullptr ? next : find_msg(player, type);
    return 1;
}

// [decomp] 0x001b1428 Text_Init
// Os três sprites de legenda (SubtitlesInfo), a fila vazia e, na partida a frio, o histórico (128
// mensagens); numa partida quente as mensagens do histórico voltam a travar o StringHeap (o
// Txt_LanguageInit as destravou).
void Text_Init(int8_t warm) {
    SubTitleMsg = nullptr;
    SubTitleSpr = Sprite_Create2(&SubtitlesInfo);
    SubTitleSpr1 = Sprite_Create2(&SubtitlesInfo);
    SubTitleSpr2 = Sprite_Create2(&SubtitlesInfo);
    DList_Init(&MsgQueue, 0x20, 0);
    if (warm == 0) {
        CirBuf_Init(&ReceivedMsgs, 0x80, ReceivedBuf);
    } else {
        for (uint32_t i = 0; i < ReceivedMsgs.size; i = (i + 1) & 0xffff) {
            Txt_LockString(reinterpret_cast<char*>(uintptr_t(uint32_t(ReceivedMsgs.buffer[i]))));
        }
    }
}

// [decomp] 0x001b1510 Text_Update
// A legenda de uma linha (tipo 4, em cena com câmera) no SubTitleSpr, ligado à câmera das legendas.
void Text_Update() {
    if (Text_UpdateMsg(-1, 4, &SubTitleMsg) != 0 && SubTitleMsg != nullptr && SubTitleSpr != nullptr) {
        Sprite_SetText(SubTitleSpr, SubTitleMsg->text, TextMsgFormats[4].width);
        SubTitleSpr->priority = 0;
        const int32_t v = subtitle_viewer();
        if (v >= 0) SubTitleSpr->viewer = uint8_t(v);
    }
    if (SubTitleMsg == nullptr && SubTitleSpr != nullptr) SubTitleSpr->priority = -1;
}

// [decomp] 0x001b15c8 Text_FlushAllSubtitles
void Text_FlushAllSubtitles() {
    SubTitleMsg = nullptr;
    if (SubTitleSpr != nullptr) SubTitleSpr->priority = -1;
    if (SubTitleSpr1 != nullptr) SubTitleSpr1->priority = -1;
    if (SubTitleSpr2 != nullptr) SubTitleSpr2->priority = -1;
    for (TXT_MSG* m = first_msg(); m != nullptr;) {
        TXT_MSG* next = next_msg(m);
        if (uint8_t(m->type - 4) < 2) Text_DelMsg(m);
        m = next;
    }
}

// [decomp] 0x001b1640 Text_DelMsg
// Destrava o texto (tipos 1, 4, 5 e 6; numa legenda em tela também as linhas de Text_Update2Line) e
// devolve o nó (que guarda o tipo).
void Text_DelMsg(TXT_MSG* msg) {
    if (msg == nullptr) return;
    const uint8_t type = msg->type;
    bool unlock = type == 1 || type == 6;
    if (type == 4 || type == 5) {
        unlock = true;
        if (msg == SubTitleMsg) {
            if (SubTitleStr != nullptr) {
                Txt_UnlockString(SubTitleStr);
                SubTitleStr = nullptr;
            }
            if (SubTitleStr2 != nullptr) {
                Txt_UnlockString(SubTitleStr2);
                SubTitleStr2 = nullptr;
            }
        }
    }
    if (unlock && msg->text != nullptr) Txt_UnlockString(msg->text);
    DList_RemoveFromInUse2Free(&MsgQueue, &msg->node);
}

// [decomp] 0x001dde58 Sound_DoSubtitle
// (Módulo Sound; aqui porque só mexe com texto.) A fala do som como legenda forçada (Text_AddMsg com
// prioridade 0xff), se as legendas estão ligadas ou o rótulo tem o bit 31; o tempo é a duração do som
// (3 s fora da tabela) x g_fSubtitleDurationModification, em ticks.
void Sound_DoSubtitle(uint32_t sound) {
    uint32_t i = 0;
    const SND2LBL_tag* e = Snd2Lbl;
    while (e->sound != sound) {
        ++i;
        ++e;
        if (0x1e3 < i) return;
    }
    if (DrawInfo[2] == 0 && (e->label & 0x80000000u) == 0) return;
    char* text = Txt_BindLabel(e->label & 0x7fffffffu, 0);
    float seconds = 3.0f;
    if (sound < 0x60d) std::memcpy(&seconds, SFXOutputData + sound * 0x18 + 0x10, 4);
    const uint32_t ticks = fptoui(g_fSubtitleDurationModification * seconds * 60.0f);
    Text_AddMsg(-1, 0xff, 4, text, 0, int16_t(ticks));
}

// ---- caixa de texto dos menus ----

namespace {
template <typename T>
T& at(void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
M_CONTROL* as_control(void* p) { return static_cast<M_CONTROL*>(p); }
// Os dados do rótulo (M_LABEL +0xe8 = caixa +0x17c): +0x18 o texto, +0x20 o tamanho.
uint8_t* label_data(M_TEXTBOX* box) { return at<uint8_t*>(box, 0x17c); }
int32_t line(const M_TEXTBOX* box) { return int32_t(box->line_height); }
uint32_t frames_120 = 0;  // ticks com a direção segurada (repete a cada 5 depois de 16)

// O teclado da tela (Text_Init): caractere, x, y, largura, altura, lados bloqueados.
struct KeyDef {
    char key;
    uint8_t x, y, w, blocked;
};
constexpr KeyDef kKeys[65] = {
    {'A', 0, 0, 0x19, 0},     {'B', 0x19, 0, 0x19, 0},     {'C', 0x32, 0, 0x19, 0},     {'D', 0x4b, 0, 0x19, 0},
    {'E', 100, 0, 0x19, 0},   {'F', 0x7d, 0, 0x19, 0},     {'G', 0x96, 0, 0x19, 0},     {'H', 0xaf, 0, 0x19, 0},
    {'I', 200, 0, 0x19, 0},   {'J', 0xe1, 0, 0x19, 0},     {'K', 0, 0x10, 0x19, 2},     {'L', 0x19, 0x10, 0x19, 2},
    {'M', 0x32, 0x10, 0x19, 0}, {'N', 0x4b, 0x10, 0x19, 0}, {'O', 100, 0x10, 0x19, 0},   {'P', 0x7d, 0x10, 0x19, 0},
    {'Q', 0x96, 0x10, 0x19, 0}, {'R', 0xaf, 0x10, 0x19, 0}, {'S', 200, 0x10, 0x19, 2},   {'T', 0xe1, 0x10, 0x19, 2},
    {'U', 0x32, 0x20, 0x19, 4}, {'V', 0x4b, 0x20, 0x19, 0}, {'W', 100, 0x20, 0x19, 0},   {'X', 0x7d, 0x20, 0x19, 0},
    {'Y', 0x96, 0x20, 0x19, 0}, {'Z', 0xaf, 0x20, 0x19, 8}, {'a', 0, 0x30, 0x19, 1},     {'b', 0x19, 0x30, 0x19, 1},
    {'c', 0x32, 0x30, 0x19, 0}, {'d', 0x4b, 0x30, 0x19, 0}, {'e', 100, 0x30, 0x19, 0},   {'f', 0x7d, 0x30, 0x19, 0},
    {'g', 0x96, 0x30, 0x19, 0}, {'h', 0xaf, 0x30, 0x19, 0}, {'i', 200, 0x30, 0x19, 1},   {'j', 0xe1, 0x30, 0x19, 1},
    {'k', 0, 0x40, 0x19, 2},  {'l', 0x19, 0x40, 0x19, 2},  {'m', 0x32, 0x40, 0x19, 0},  {'n', 0x4b, 0x40, 0x19, 0},
    {'o', 100, 0x40, 0x19, 0}, {'p', 0x7d, 0x40, 0x19, 0},  {'q', 0x96, 0x40, 0x19, 0},  {'r', 0xaf, 0x40, 0x19, 0},
    {'s', 200, 0x40, 0x19, 2}, {'t', 0xe1, 0x40, 0x19, 2},  {'u', 0x32, 0x50, 0x19, 4},  {'v', 0x4b, 0x50, 0x19, 0},
    {'w', 100, 0x50, 0x19, 0}, {'x', 0x7d, 0x50, 0x19, 0},  {'y', 0x96, 0x50, 0x19, 0},  {'z', 0xaf, 0x50, 0x19, 8},
    {'0', 0, 0x60, 0x19, 3},  {'1', 0x19, 0x60, 0x19, 1},  {'2', 0x32, 0x60, 0x19, 0},  {'3', 0x4b, 0x60, 0x19, 0},
    {'4', 100, 0x60, 0x19, 2}, {'5', 0x7d, 0x60, 0x19, 0},  {'6', 0x96, 0x60, 0x19, 0},  {'7', 0xaf, 0x60, 0x19, 0},
    {'8', 200, 0x60, 0x19, 1}, {'9', 0xe1, 0x60, 0x19, 3},  {' ', 0x19, 0x70, 0x4b, 0xc}, {'-', 0x7d, 0x70, 0x32, 4},
    {'+', 0xaf, 0x70, 0x32, 8},
};
}  // namespace

// [decomp] 0x00222b80 Text_Init
// A caixa: o rótulo com o nome (formato SerpLight, 0x13 caracteres em M_TEXTBOX::name), três componentes
// (cursor, destaque da tecla, fundo do teclado) e as 65 teclas.
void Text_Init(M_TEXTBOX* box, NEW_CONTROL* desc, M_CONTROL* owner) {
    box->type = 0xc;
    box->state = 0;
    at<M_CONTROL*>(box, 0xc) = owner;
    at<uint32_t>(box, 0x10) = 0;
    at<uint32_t>(box, 0x14) = uint32_t(desc->owner) << 16 | 10;
    at<uint32_t>(box, 0x18) = desc->id;
    at<uint32_t>(box, 8) = 0;
    at<uint32_t>(box, 0x20) = desc->unknown_0c;
    box->depth = desc->depth;
    GPOINT extent{};
    Font_GetTextExtent(kAlphabet, kFmtSerp, &extent, nullptr, -1);
    box->key = 0xffff;
    box->line_height = extent.y + 2.0f;
    box->x = desc->x;
    box->y = desc->y;
    box->w = int16_t(Component_GetHandle(as_control(box))->left + 0xfa);
    box->h = int16_t(line(box) + 5);
    NEW_CONTROL label;
    amemcpy(&label, desc, sizeof(NEW_CONTROL));
    label.x = int16_t(label.x + Component_GetHandle(as_control(box))->left);
    const M_COMPONENTHANDLE* a = Component_GetHandle(as_control(box));
    const M_COMPONENTHANDLE* b = Component_GetHandle(as_control(box));
    label.depth = uint16_t(label.depth - 1);
    label.format = kFmtSerp;
    label.h = int16_t(line(box) + 5);
    label.w = int16_t(a->left - (b->right - 0xfa));
    label.unknown_10 = 0;
    Label_Init(reinterpret_cast<M_LABEL*>(box->label), &label, as_control(box));
    std::memset(box->name, 0, sizeof(box->name));
    if (label_data(box) != nullptr) at<char*>(label_data(box), 0x18) = box->name;
    at<int16_t>(box, 0x220) = int16_t(desc->depth - 3);
    at<uint32_t>(box, 0x1bc) = uint32_t(desc->owner) << 16 | 0xb;
    Component_SetupInstance(as_control(box->cursor), 0, -1, -1, -1, -1);
    at<int16_t>(box, 0x2b4) = int16_t(desc->depth - 2);
    at<uint32_t>(box, 0x250) = uint32_t(desc->owner) << 16 | 0xd;
    Component_SetupInstance(as_control(box->highlight), 0, -1, -1, -1, -1);
    at<int16_t>(box, 0x348) = int16_t(desc->depth - 1);
    at<uint32_t>(box, 0x2e4) = uint32_t(desc->owner) << 16 | 0xc;
    Component_SetupInstance(as_control(box->keyboard), 0, -1, -1, -1, -1);
    for (const KeyDef& k : kKeys) Text_SetupChar(box, uint8_t(k.key), k.x, k.y, k.w, 0x10, k.blocked);
    at<uint32_t>(box, 0x1c) = 0;
    Script_PrepareScripting(as_control(box));
}

// [decomp] 0x00223600 Text_SendMessage
// Mensagens da caixa: 0x14 sai do laço do script, 0x16 toca o script a, 0x21 escolhe o script b na
// posição a (até 7), 0x2b troca o estado, 0x39 devolve a caixa, 0x3b o texto digitado, 0x3d o campo
// +0x20, 0x47 libera tudo, 0x53 fecha o teclado (e avisa o dono); as outras vão para o rótulo.
int32_t Text_SendMessage(M_TEXTBOX* box, uint32_t message, int32_t a, int32_t b) {
    M_TEXTBOX* self = box;
    switch (message) {
        case 0x14:
            return Script_ExitLoop(as_control(box));
        case 0x16:
            return Script_Play(as_control(box), uint32_t(a), int8_t(b));
        case 0x21:
            if (uint32_t(a) >= 7) return 0;
            at<M_SCRIPT*>(self, 0x7c + uint32_t(a) * 4) = Script_Select(as_control(box), uint32_t(b));
            return 1;
        case 0x2b:
            box->state = uint8_t(a);
            return 1;
        case 0x39:
            return int32_t(reinterpret_cast<uintptr_t>(box));
        case 0x3b:
            return label_data(box) != nullptr ? int32_t(reinterpret_cast<uintptr_t>(at<char*>(label_data(box), 0x18))) : 0;
        case 0x3d:
            return int32_t(at<uint32_t>(box, 0x20));
        case 0x47: {
            Label_SendMessage(reinterpret_cast<M_LABEL*>(box->label), 0x47, 0, 0);
            Component_Free(as_control(self));
            Component_Free(as_control(self->keyboard));
            Component_Free(as_control(self->cursor));
            Component_Free(as_control(self->highlight));
            Script_DelAllScripts(as_control(self));
            void* p = self;
            Menu_Free(&p, 3);
            return 0;
        }
        case 0x53:
            at<uint32_t>(box, 0x1c) &= 0x7fffffffu;
            at<uint32_t>(box, 0x1c) &= 0xffffff7fu;
            at<uint32_t>(box, 0x1c) |= 0x40;
            box->h = int16_t(line(box) + 5);
            __Menu_SendMessage(at<M_CONTROL*>(box, 0xc), 0, int32_t(reinterpret_cast<uintptr_t>(box)), 0);
            return 1;
        default:
            return Label_SendMessage(reinterpret_cast<M_LABEL*>(box->label), message, a, b);
    }
}

// [decomp] 0x002237c8 TextBox_Update
// Um tick da caixa. Fechada pelo dono ou por ela mesma, libera os componentes. Sem o foco da página,
// fecha o teclado se estava aberto. Com o foco e o teclado aberto (bit 31 do estado): acha a tecla sob o
// cursor, move o cursor pelas direções (0x1d direita, 0x1c esquerda, 0x1b baixo, 0x1a cima: um passo ao
// apertar e, segurando, um a cada 5 ticks depois de 16), respeitando os lados bloqueados e a área do
// teclado (248 x 126), e com a ação 0x1f usa a tecla: '-' apaga, '+' fecha, as outras escrevem (até 18
// caracteres, avisando o dono com 0x4a). Por fim posiciona o rótulo e os componentes.
void TextBox_Update(M_MANAGER* manager, M_PAGE* page, M_TEXTBOX* box) {
    if ((at<uint8_t>(at<M_CONTROL*>(box, 0xc), 0x7a) & 5) != 0 || (box->state & 1) != 0) {
        box->state |= 4;
        Component_Free(as_control(box));
        Component_Free(as_control(box->cursor));
        Component_Free(as_control(box->keyboard));
        Component_Free(as_control(box->highlight));
        return;
    }
    box->state &= 0xfb;
    const int16_t left = Component_GetHandle(as_control(box))->left;
    Menu_ResizeControl(as_control(box->label), int16_t(box->x + left), box->y,
                       int16_t(box->x + box->w - Component_GetHandle(as_control(box))->right), box->h);
    uint32_t& state = at<uint32_t>(box, 0x1c);
    const uint32_t flags = state;
    const int16_t player = at<int16_t>(manager, 0x1cc);
    if (at<uint32_t>(at<uint8_t*>(page, 0xac), 0x18) != at<uint32_t>(box, 0x18)) {
        if (int32_t(flags) < 0) {
            Text_SendMessage(box, 0x53, 0, 0);
            state |= 0x10;
        }
    } else {
        state = flags | 0x20;
        if (int32_t(flags | 0x20) >= 0) {
            state = (flags | 0x20) & 0xffffffbfu;
        } else {
            state = ((flags | 0x20) & 0xffffffbfu) | 0x80;
            box->h = int16_t(line(box) + Component_GetHandle(as_control(box->cursor))->bottom + 0x84);
            for (uint32_t k = 0; k < 0x41; k = (k + 1) & 0xff) {
                const TEXTBOX_KEY& key = box->keys[k];
                if (key.x <= box->cursor_x && key.y <= box->cursor_y && box->cursor_x <= key.x + key.w &&
                    box->cursor_y <= key.y + key.h) {
                    box->key = uint16_t(k);
                    break;
                }
            }
            // Uma direção: "first" é o passo ao apertar, "held" o passo repetido (os dois só se couber).
            const auto step = [&](bool can, int32_t& coord, int32_t delta) {
                if ((state & 0x40000000u) == 0) {
                    if (can) coord += delta;
                    state |= 0x40000000u;
                    frames_120 = 0;
                    return;
                }
                if (frames_120 < 0x10 || !can) {
                    ++frames_120;
                    return;
                }
                ++frames_120;
                if (frames_120 % 5 == 0) coord += delta;
            };
            const TEXTBOX_KEY* key = &box->keys[box->key];
            if (Menu_InputAction(player, 0x1d, 1) != 0 && (key->blocked & 8) == 0) {
                step(box->cursor_x < 0xf8 - key->w, box->cursor_x, key->w);
            } else if (Menu_InputAction(player, 0x1c, 1) != 0 && (key->blocked & 4) == 0) {
                step(key->w < box->cursor_x, box->cursor_x, -key->w);
            } else if (Menu_InputAction(player, 0x1b, 1) != 0 && (key->blocked & 2) == 0) {
                step(box->cursor_y < 0x7e - key->h, box->cursor_y, key->h);
            } else if (Menu_InputAction(player, 0x1a, 1) != 0 && (key->blocked & 1) == 0) {
                step(key->h < box->cursor_y, box->cursor_y, -key->h);
            } else {
                state &= 0xbfffffffu;
            }
            uint8_t* data = label_data(box);
            if (Menu_InputAction(player, 0x1f, 4) != 0 && data != nullptr) {
                const uint8_t c = box->keys[box->key].key;
                uint16_t& length = at<uint16_t>(data, 0x20);
                char* name = at<char*>(data, 0x18);
                if (c == '-') {
                    if (length != 0) {
                        --length;
                        name[length] = 0;
                        __Menu_SendMessage(at<M_CONTROL*>(box, 0xc), 0x4a, int32_t(reinterpret_cast<uintptr_t>(box)), 0);
                    }
                } else if (c == '+') {
                    Text_SendMessage(box, 0x53, 0, 0);
                } else {
                    name[length++] = char(c);
                    if (length < 0x13) {
                        __Menu_SendMessage(at<M_CONTROL*>(box, 0xc), 0x4a, int32_t(reinterpret_cast<uintptr_t>(box)), 0);
                    } else {
                        --length;
                    }
                    name[length] = 0;
                }
            }
        }
        GPOINT extent{};
        if (label_data(box) != nullptr) Font_GetTextExtent(at<char*>(label_data(box), 0x18), kFmtSerp, &extent, nullptr, -1);
    }
    const int32_t over = Menu_CursorOverMe(manager, as_control(box));
    Label_Update(manager, page, reinterpret_cast<M_LABEL*>(box->label), int8_t(over));
    Component_SetupInstance(as_control(box), state, -1, -1, -1, -1);
    const M_COMPONENTHANDLE* k = Component_GetHandle(as_control(box->keyboard));
    Component_SetupInstance(as_control(box->cursor), state, box->x + k->left, box->y + line(box) + 5, 0x100, 0x80);
    const int32_t kx = box->x + Component_GetHandle(as_control(box->keyboard))->left;
    const int32_t ky = box->y + line(box) + Component_GetHandle(as_control(box->keyboard))->top + 4;
    const int32_t kw = 0xfa - Component_GetHandle(as_control(box->keyboard))->right;
    const int32_t kh = 0x80 - Component_GetHandle(as_control(box->keyboard))->bottom;
    Component_SetupInstance(as_control(box->keyboard), state, kx, ky, kw, kh);
    const int16_t hl = Component_GetHandle(as_control(box->keyboard))->left;
    const TEXTBOX_KEY& key = box->keys[box->key];
    Component_SetupInstance(as_control(box->highlight), state, box->x + hl + key.x, box->y + line(box) + key.y + 7, key.w,
                            key.h);
}

// [decomp] 0x00224098 Text_SetupChar
// A próxima tecla do teclado (M_TEXTBOX::key conta a partir de 0xffff).
void Text_SetupChar(M_TEXTBOX* box, uint8_t key, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t blocked) {
    box->key = uint16_t(box->key + 1);
    TEXTBOX_KEY& k = box->keys[box->key % 65];  // só no port: o original escreveria além da tabela
    k.key = key;
    k.x = x;
    k.y = y;
    k.w = w;
    k.h = h;
    k.blocked = blocked;
}
