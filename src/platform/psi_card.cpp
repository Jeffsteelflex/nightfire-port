// O cartão de memória: HandlePS2CardState, os MemCard_* e as funções psi* de gravação do ACTION.ELF sobre um
// cartão do PC. Ver psi_card.h.

#include "platform/psi_card.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "platform/psi_file.h"
#include "platform/psi_gfx.h"

PS2CARDFLOW_tag PS2CardFlow;
CARDSTATE_tag CardState[kCardStates];
int32_t StateStack = 0;
uint32_t PSIOP_State = 0;
int32_t syncresult = 0;
uint8_t PS2SaveGameFine = 0;
uint8_t OverWriteCheckDone = 0;
int32_t NumberSaveGamesGot = 0;
int32_t GettingSaveGames = 0;
int32_t SaveGamesReturned = 0;
uint8_t PS2_one_codename = 0;
// Tabelas do ELF (copiadas na partida, game/static_init.cpp): ASCII -> Shift-JIS para o título do ícone, e as
// cores do fundo e a luz ambiente do icon.sys.
uint8_t ascii_table[12];     // 0x00265060 3 x {base, subtrai}: dígitos, maiúsculas, minúsculas
uint8_t ascii_special[132];  // 0x00264fd8 33 x {Shift-JIS, ASCII}: pontuação
uint8_t bgcolor[64];         // 0x00246280
uint8_t ambient[16];         // 0x002462c0

namespace {

// Os nomes do ELF (strings constantes).
constexpr char kDirName[] = "BASLUS-20579";          // DirName (0x0030c7fc)
constexpr char kGameTitleASCII[] = "007: NightFire";  // GameTitleASCII (0x0030c7f8)
constexpr char kIconName[] = "007.ICN";              // iconName (0x0030c810)

// O cartão (MC_Info, 0x00316940: livres, formatado, tipo).
uint32_t MC_Info = 0;       // clusters (KB) livres
int32_t MC_Format = 0;      // 0x00316944 formatado
int32_t MC_Type = 0;        // 0x00316948 2: cartão do PS2

uint8_t* LoadSaveBuff = nullptr;    // 0x0030c800
uint32_t LoadSaveBuffSize = 0;      // 0x0030d6cc
char SavingName[0x100];             // 0x00312540 "BASLUS-20579<codinome>"
uint16_t GameTitle[0x40];           // 0x003124c0 o título do ícone em Shift-JIS
uint8_t PS2IconHead[0x3c4];         // 0x00312560 o icon.sys
uint8_t FolderTable[0x100][0x40];   // 0x00312940 a tabela do sceMcGetDir (sceMcTblGetDir)
int32_t fd_271 = 0;                 // MemCard_CREATE_DIR
int32_t fd_278 = 0;                 // MemCard_SAVE_DATA
int32_t fd_305 = 0;                 // MemCard_LOAD_DATA
int32_t nDelete_297 = 0;            // MemCard_DELETE_DATA
int32_t nDelete_301 = 0;            // MemCard_DELETE_DATA2

int32_t get32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

CARDSTATE_tag& top() { return CardState[StateStack]; }

// Empilha um passo novo (subestado 0).
void push(uint8_t state) {
    ++StateStack;
    CardState[StateStack].state = state;
    CardState[StateStack].sub = 0;
}

// Uma pergunta ou um erro: a mensagem e as respostas para PS2_DoCardFlow.
void ask(uint32_t message, uint32_t options) {
    PS2CardFlow.options = options;
    PS2CardFlow.message = message;
    PS2CardFlow.rebuild = 1;
    PS2CardFlow.answer = 0;
    PS2CardFlow.answered = 0;
}

// O fim de uma resposta: limpa a pergunta.
void answered() {
    PS2CardFlow.options = 0;
    PS2CardFlow.message = 0;
    PS2CardFlow.answered = 0;
    PS2CardFlow.rebuild = 1;
}

// ---- a libmc do PC ----
// Cada chamada faz a operação no cartão do PC na hora e guarda o resultado; sceMcSync só o entrega na
// próxima chamada de HandlePS2CardState (uma operação por quadro, como no PS2 onde elas levam quadros).

nf::MemoryCard g_memory_card;
nf::PcCard* g_card = nullptr;

struct OpenFile {
    std::string path;
    std::vector<uint8_t> data;
    uint32_t pos = 0;
    bool write = false;
    bool dirty = false;
};

struct Libmc {
    std::string cwd;                // pasta atual (sceMcChdir), sem barras nas pontas
    std::map<int32_t, OpenFile> files;
    int32_t next_fd = 0;
    bool pending = false;           // uma operação esperando o sceMcSync
    int32_t result = 0;
    uint32_t polls = 0;             // chamadas de HandlePS2CardState
    uint32_t issued = 0;            // a chamada em que a operação pendente começou
    uint32_t operations = 0;
} mc;

nf::PcCard& the_card() { return g_card != nullptr ? *g_card : g_memory_card; }

void issue(int32_t result) {
    mc.pending = true;
    mc.result = result;
    mc.issued = mc.polls;
    ++mc.operations;
}

// Um caminho do cartão: "/a/b" absoluto, "b" relativo à pasta atual; ".." sobe.
std::string resolve(const char* path) {
    std::vector<std::string> parts;
    std::string p = path;
    if (p.empty() || p[0] != '/') {
        size_t i = 0;
        while (i < mc.cwd.size()) {
            const size_t j = std::min(mc.cwd.find('/', i), mc.cwd.size());
            if (j > i) parts.push_back(mc.cwd.substr(i, j - i));
            i = j + 1;
        }
    }
    size_t i = 0;
    while (i <= p.size()) {
        const size_t j = std::min(p.find('/', i), p.size());
        const std::string part = p.substr(i, j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j + 1;
    }
    std::string out;
    for (const std::string& s : parts) out += (out.empty() ? "" : "/") + s;
    return out;
}

void persist(OpenFile& f) {
    if (f.write && f.dirty) {
        the_card().write(f.path, f.data);
        f.dirty = false;
    }
}

// sceMcInit
void sceMcInit() {
    mc.cwd.clear();
    mc.files.clear();
    mc.pending = false;
}

// sceMcGetInfo(porta, slot, tipo, livres, formatado): o cartão do PC é um cartão do PS2 formatado com 8000 KB
// livres.
void sceMcGetInfo(int32_t* type, uint32_t* free_clusters, int32_t* format) {
    *type = 2;
    *free_clusters = 8000;
    *format = 1;
    issue(0);
}

// sceMcSync(1, ...): 0 enquanto a operação não terminou, 1 quando entrega o resultado, -1 sem operação.
int32_t sceMcSync() {
    if (!mc.pending) return -1;
    if (mc.issued == mc.polls) return 0;
    mc.pending = false;
    syncresult = mc.result;
    return 1;
}

void sceMcChdir(const char* path) {
    const std::string p = resolve(path);
    bool dir = false;
    if (the_card().exists(p, &dir) && dir) {
        mc.cwd = p;
        issue(0);
    } else {
        issue(-4);
    }
}

void sceMcMkdir(const char* path) {
    const std::string p = resolve(path);
    bool dir = false;
    issue(the_card().exists(p, &dir) ? -4 : (the_card().make_dir(p) ? 0 : -4));
}

// Modo: 1 ler, 2 escrever, 3 os dois; 0x200 cria. Sem truncar (a libmc só trunca com 0x400).
void sceMcOpen(const char* path, int32_t mode) {
    OpenFile f;
    f.path = resolve(path);
    f.write = (mode & 2) != 0;
    bool dir = false;
    const bool exists = the_card().exists(f.path, &dir);
    if (dir) {
        issue(-4);
        return;
    }
    if (exists) {
        the_card().read(f.path, &f.data);
    } else if ((mode & 0x200) == 0) {
        issue(-4);
        return;
    } else {
        // A pasta do arquivo precisa existir.
        const size_t slash = f.path.rfind('/');
        bool parent_dir = false;
        if (slash != std::string::npos && !(the_card().exists(f.path.substr(0, slash), &parent_dir) && parent_dir)) {
            issue(-4);
            return;
        }
        f.dirty = true;
    }
    if (f.write && f.dirty) persist(f);
    const int32_t fd = mc.next_fd++;
    mc.files[fd] = std::move(f);
    issue(fd);
}

OpenFile* file_of(int32_t fd) {
    const auto it = mc.files.find(fd);
    return it == mc.files.end() ? nullptr : &it->second;
}

void sceMcSeek(int32_t fd, int32_t offset, int32_t whence) {
    OpenFile* f = file_of(fd);
    if (f == nullptr) {
        issue(-4);
        return;
    }
    int64_t pos = whence == 0 ? offset : whence == 1 ? int64_t(f->pos) + offset : int64_t(f->data.size()) + offset;
    pos = std::clamp<int64_t>(pos, 0, 0x7fffffff);
    f->pos = uint32_t(pos);
    issue(int32_t(pos));
}

void sceMcRead(int32_t fd, uint8_t* buf, int32_t size) {
    OpenFile* f = file_of(fd);
    if (f == nullptr || buf == nullptr || size < 0) {
        issue(-4);
        return;
    }
    const uint32_t n = f->pos < f->data.size() ? std::min<uint32_t>(uint32_t(size), uint32_t(f->data.size()) - f->pos) : 0;
    if (n != 0) std::memcpy(buf, f->data.data() + f->pos, n);
    f->pos += n;
    issue(int32_t(n));
}

void sceMcWrite(int32_t fd, const uint8_t* buf, int32_t size) {
    OpenFile* f = file_of(fd);
    if (f == nullptr || !f->write || size < 0 || (buf == nullptr && size != 0)) {
        issue(-4);
        return;
    }
    if (f->data.size() < f->pos + uint32_t(size)) f->data.resize(f->pos + uint32_t(size));
    if (size != 0) std::memcpy(f->data.data() + f->pos, buf, uint32_t(size));
    f->pos += uint32_t(size);
    f->dirty = true;
    issue(size);
}

void sceMcFlush(int32_t fd) {
    OpenFile* f = file_of(fd);
    if (f == nullptr) {
        issue(-4);
        return;
    }
    persist(*f);
    issue(0);
}

void sceMcClose(int32_t fd) {
    OpenFile* f = file_of(fd);
    if (f == nullptr) {
        issue(-4);
        return;
    }
    persist(*f);
    mc.files.erase(fd);
    issue(0);
}

// sceMcGetDir(porta, slot, "/pasta/padrão", 0, máximo, tabela): as entradas que casam com o padrão (um "*" no
// fim), com "." e ".." numa listagem de pasta (como no PS2). Cada entrada é um sceMcTblGetDir de 0x40 bytes:
// +0x10 tamanho, +0x14 atributos (0x20 pasta), +0x20 o nome.
void sceMcGetDir(const char* pattern, int32_t max_entries, uint8_t (*table)[0x40]) {
    std::string p = pattern;
    const size_t slash = p.rfind('/');
    const std::string dir = resolve(slash == std::string::npos ? "" : p.substr(0, slash == 0 ? 1 : slash).c_str());
    std::string mask = slash == std::string::npos ? p : p.substr(slash + 1);
    const bool star = !mask.empty() && mask.back() == '*';
    if (star) mask.pop_back();
    bool is_dir = false;
    if (!the_card().exists(dir, &is_dir) || !is_dir) {
        issue(-4);
        return;
    }
    std::vector<nf::PcCard::Entry> entries;
    if (star && mask.empty() && !dir.empty()) {
        nf::PcCard::Entry dot;
        dot.dir = true;
        dot.name = ".";
        entries.push_back(dot);
        dot.name = "..";
        entries.push_back(dot);
    }
    for (const nf::PcCard::Entry& e : the_card().list(dir)) {
        if (star ? e.name.compare(0, mask.size(), mask) == 0 : e.name == mask) entries.push_back(e);
    }
    int32_t n = 0;
    for (const nf::PcCard::Entry& e : entries) {
        if (n >= max_entries) break;
        uint8_t* t = table[n++];
        std::memset(t, 0, 0x40);
        put32(t + 0x10, e.dir ? 0 : e.size);
        const uint16_t attr = e.dir ? 0x8427 : 0x8497;
        std::memcpy(t + 0x14, &attr, 2);
        std::snprintf(reinterpret_cast<char*>(t + 0x20), 0x20, "%s", e.name.c_str());
    }
    issue(n);
}

void sceMcDelete(const char* path) {
    const std::string p = resolve(path);
    bool dir = false;
    if (!the_card().exists(p, &dir)) {
        issue(-4);
        return;
    }
    if (dir && !the_card().list(p).empty()) {
        issue(-6);  // pasta não vazia
        return;
    }
    issue(the_card().remove(p) ? 0 : -5);
}

void sceMcFormat() {
    the_card().clear();
    mc.cwd.clear();
    mc.files.clear();
    issue(0);
}

}  // namespace

// ---- a máquina de estados ----

// [decomp] 0x0010e798 MemCard_GET_CARD_STATUS
void MemCard_GET_CARD_STATUS() {
    const uint8_t sub = top().sub;
    if (sub == 0) {
        top().sub = 1;
        sceMcGetInfo(&MC_Type, &MC_Info, &MC_Format);
        push(1);
    } else if (sub == 1) {
        --StateStack;
    }
}

namespace {

// MemCard_CHECK_FOR_CARD e MemCard_CHECK_FOR_CARD2: a diferença é a listagem vazia ao cancelar.
void check_for_card(bool clear_enum) {
    CARDSTATE_tag& s = top();
    if (s.sub == 1) {
        if (MC_Type != 2) {
            s.sub = 2;
            ask(1, 0xc);
            return;
        }
        --StateStack;
        return;
    }
    if (s.sub == 0) {
        s.sub = 1;
        push(10);
        return;
    }
    if (s.sub != 2 || PS2CardFlow.answered == 0) return;
    answered();
    if (PS2CardFlow.answer == 8) {
        StateStack = 1;
        CardState[1].sub = 0;
        return;
    }
    if (PS2CardFlow.answer == 4) {
        if (clear_enum) {
            NumberSaveGamesGot = 0;
            GettingSaveGames = 0;
        }
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// As telas de erro com "tentar de novo" (8) ou "cancelar" (4): a mensagem; na resposta, tentar volta ao começo
// do primeiro passo da pilha e cancelar termina.
bool error_screen(uint32_t message) {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        ++s.sub;
        ask(message, 0xc);
        return false;
    }
    if (s.sub != 1 || PS2CardFlow.answered == 0) return false;
    answered();
    return true;
}

}  // namespace

// [decomp] 0x0010e858 MemCard_CHECK_FOR_CARD
// Sem cartão do PS2 (tipo 2): "sem cartão", tentar de novo ou cancelar.
void MemCard_CHECK_FOR_CARD() { check_for_card(false); }

// [decomp] 0x0010e980 MemCard_CHECK_FOR_CARD2
void MemCard_CHECK_FOR_CARD2() { check_for_card(true); }

// [decomp] 0x0010eab8 MemCard_FORMAT
void MemCard_FORMAT() {
    CARDSTATE_tag& s = top();
    switch (s.sub) {
        case 0:
            ++s.sub;
            push(1);
            break;
        case 1:
            ++s.sub;
            push(3);
            break;
        case 2:
            ++s.sub;
            push(4);
            break;
        case 3:
            PS2CardFlow.message = 10;
            ++s.sub;
            sceMcFormat();
            push(1);
            break;
        case 4:
            ++s.sub;
            push(syncresult == 0 ? 10 : 0x17);
            break;
        case 5:
            --StateStack;
            break;
        default:
            break;
    }
}

// [decomp] 0x0010ec98 MemCard_ASK_DELETE
// "Apagar este codinome?": apagar (0x20) segue, cancelar (4) termina.
void MemCard_ASK_DELETE() {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        s.sub = 1;
        ask(0xd, 0x24);
        return;
    }
    if (s.sub != 1 || PS2CardFlow.answered == 0) return;
    answered();
    if (PS2CardFlow.answer == 0x20) {
        --StateStack;
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010ed60 MemCard_ASK_FORMAT
void MemCard_ASK_FORMAT() {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        s.sub = 1;
        ask(2, 0x14);
        return;
    }
    if (s.sub != 1 || PS2CardFlow.answered == 0) return;
    answered();
    if (PS2CardFlow.answer == 0x10) {
        s.state = 2;
        s.sub = 0;
    }
    if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010ee38 MemCard_ASK_OVERWRITE
// "Este codinome já existe. Sobrescrever?": sim (2) segue, cancelar (4) termina.
void MemCard_ASK_OVERWRITE() {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        s.sub = 1;
        ask(4, 6);
        return;
    }
    if (s.sub != 1 || PS2CardFlow.answered == 0) return;
    answered();
    if (PS2CardFlow.answer == 2) {
        --StateStack;
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010ef00 MemCard_CHECK_FREE_SPACE
// Menos de 82 KB livres: "sem espaço" e termina no OK.
void MemCard_CHECK_FREE_SPACE() {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        if (MC_Info < 0x52) {
            s.sub = 1;
            ask(3, 1);
            return;
        }
        --StateStack;
        return;
    }
    if (s.sub == 1 && PS2CardFlow.answered != 0) {
        answered();
        PSIOP_State = 4;
        StateStack = 0;
    }
}

// [decomp] 0x0010efc0 MemCard_FILE_CORRUPT
void MemCard_FILE_CORRUPT() {
    CARDSTATE_tag& s = top();
    if (s.sub == 0) {
        s.sub = 1;
        ask(6, 1);
        return;
    }
    if (s.sub == 1 && PS2CardFlow.answered != 0) {
        answered();
        PSIOP_State = 4;
        StateStack = 0;
    }
}

// [decomp] 0x0010f068 MemCard_DELETE_ERROR
void MemCard_DELETE_ERROR() {
    if (!error_screen(0xe)) return;
    if (PS2CardFlow.answer == 8) {
        StateStack = 1;
        CardState[1].sub = 0;
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010f138 MemCard_LOADERROR
void MemCard_LOADERROR() {
    if (!error_screen(7)) return;
    if (PS2CardFlow.answer == 8) {
        StateStack = 1;
        CardState[1].sub = 0;
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010f208 MemCard_FORMAT_ERROR
void MemCard_FORMAT_ERROR() {
    if (!error_screen(0x11)) return;
    if (PS2CardFlow.answer == 8) {
        StateStack = 1;
        CardState[1].sub = 0;
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010f2d8 MemCard_SAVEERROR
// "Falha ao gravar": tentar de novo apaga a pasta e grava do começo (MemCard_DELETE_DATA2).
void MemCard_SAVEERROR() {
    if (!error_screen(5)) return;
    if (PS2CardFlow.answer == 8) {
        push(0x12);
    } else if (PS2CardFlow.answer == 4) {
        StateStack = 0;
        PSIOP_State = 4;
    }
}

// [decomp] 0x0010f3c0 MemCard_SAVEERROR2
void MemCard_SAVEERROR2() { MemCard_SAVEERROR(); }

// [decomp] 0x0010f4a8 MemCard_CREATE_DIR
// A pasta nova do perfil: confere o espaço, cria a pasta, grava o icon.sys e o ícone.
void MemCard_CREATE_DIR() {
    if (sceMcSync() == 0) return;
    CARDSTATE_tag& s = top();
    if (syncresult < 0 && (syncresult != -4 || 2 < s.sub)) {
        push(0xf);
        return;
    }
    char path[0x100];
    switch (++s.sub) {
        case 1:
            push(5);
            break;
        case 2:
            std::snprintf(path, sizeof(path), "/%s", SavingName);
            sceMcMkdir(path);
            break;
        case 3:
            PS2CardFlow.message = 9;
            std::snprintf(path, sizeof(path), "/%s/icon.sys", SavingName);
            sceMcOpen(path, 0x203);
            break;
        case 4:
            fd_271 = syncresult;
            std::memset(PS2IconHead, 0, sizeof(PS2IconHead));
            std::memcpy(PS2IconHead, "PS2D", 4);          // DAT_0030c838
            std::memset(PS2IconHead + 4, 0, 4);           // DAT_0030c83c
            std::memcpy(PS2IconHead + 0xc0, GameTitle, 0x44);
            std::memcpy(PS2IconHead + 0x10, bgcolor, 0x40);
            std::memcpy(PS2IconHead + 0xb0, ambient, 0x10);
            PS2IconHead[6] = 0x1c;  // a quebra de linha do título: 14 caracteres
            PS2IconHead[0xc] = 0x40;
            std::snprintf(reinterpret_cast<char*>(PS2IconHead + 0x104), 0x40, "%s", kIconName);
            std::snprintf(reinterpret_cast<char*>(PS2IconHead + 0x144), 0x40, "%s", kIconName);
            std::snprintf(reinterpret_cast<char*>(PS2IconHead + 0x184), 0x40, "%s", kIconName);
            sceMcWrite(fd_271, PS2IconHead, sizeof(PS2IconHead));
            break;
        case 5:
        case 9:
            sceMcFlush(fd_271);
            break;
        case 6:
        case 10:
            sceMcClose(fd_271);
            break;
        case 7:
            std::snprintf(path, sizeof(path), "/%s/%s", SavingName, kIconName);
            sceMcOpen(path, 0x203);
            break;
        case 8:
            fd_271 = syncresult;
            sceMcWrite(syncresult, PS2IconBuffer, int32_t(PS2IconSize));
            break;
        case 11:
            --StateStack;
            break;
        default:
            break;
    }
}

// [decomp] 0x0010f778 MemCard_BOOTUP_CHECK
// Na partida: confere o cartão; sem cartão do PS2 ou sem espaço, a mensagem com OK ou tentar de novo.
void MemCard_BOOTUP_CHECK() {
    PS2CardFlow.message = 0;
    CARDSTATE_tag& s = top();
    uint8_t next = 0;
    switch (s.sub) {
        case 0:
            next = 1;
            break;
        case 1:
            next = 10;
            break;
        case 2:
            if (MC_Type == 2) {
                s.sub = uint8_t(s.sub + 2);
                return;
            }
            PS2CardFlow.message = 0x10;
            PS2CardFlow.options = 9;
            ++s.sub;
            PS2CardFlow.rebuild = 1;
            PS2CardFlow.answer = 0;
            PS2CardFlow.answered = 0;
            return;
        case 3:
        case 7:
            PS2CardFlow.message = s.sub == 3 ? 0x10 : 0xf;
            if (PS2CardFlow.answered != 0) {
                if (PS2CardFlow.answer == 1) {
                    PS2CardFlow.answered = 0;
                    PS2CardFlow.rebuild = 1;
                    PS2CardFlow.options = 0;
                    PS2CardFlow.message = 0;
                    PSIOP_State = 4;
                    --StateStack;
                    return;
                }
                PS2CardFlow.answered = 0;
                PS2CardFlow.rebuild = 1;
                PS2CardFlow.options = 0;
                if (PS2CardFlow.answer == 8) {
                    PS2CardFlow.message = 0;
                    StateStack = 1;
                    CardState[1].sub = 0;
                    return;
                }
            }
            return;
        case 4:
            ++s.sub;
            return;
        case 5:
            next = 10;
            break;
        case 6:
            if (0x51 < MC_Info || MC_Format == 0) {
                s.sub = uint8_t(s.sub + 2);
                return;
            }
            PS2CardFlow.message = 0xf;
            PS2CardFlow.options = 9;
            ++s.sub;
            PS2CardFlow.rebuild = 1;
            PS2CardFlow.answer = 0;
            PS2CardFlow.answered = 0;
            return;
        case 8:
            PSIOP_State = 4;
            --StateStack;
            return;
        default:
            return;
    }
    ++s.sub;
    push(next);
}

// [decomp] 0x0010fa08 MemCard_SAVE_DATA
// Grava o perfil: confere o cartão; com a pasta existindo pergunta se sobrescreve, sem ela cria (CREATE_DIR);
// grava o arquivo de dados e confere a pasta (5 entradas, 78 KB): PS2SaveGameFine e PSIOP_State 2.
void MemCard_SAVE_DATA() {
    const int32_t result = syncresult;
    PS2CardFlow.message = 0;
    CARDSTATE_tag& s = top();
    char path[0x100];
    const auto push_sync = [&]() { push(1); };
    switch (s.sub) {
        case 0:
            ++s.sub;
            push(10);
            return;
        case 1:
        case 2:
            ++s.sub;
            push(1);
            return;
        case 3:
            ++s.sub;
            push(3);
            return;
        case 4:
            ++s.sub;
            if (MC_Format != 0) return;
            push(8);
            return;
        case 5:
            ++s.sub;
            return;
        case 6:
            ++s.sub;
            std::snprintf(path, sizeof(path), "/%s", SavingName);
            sceMcChdir(path);
            push_sync();
            return;
        case 7:
            ++s.sub;
            if (-1 < syncresult) {
                if (OverWriteCheckDone != 0) return;
                push(0x10);
                OverWriteCheckDone = 1;
                return;
            }
            push(result == -4 ? 7 : 6);
            return;
        case 8:
            PS2CardFlow.message = 9;
            ++s.sub;
            std::snprintf(path, sizeof(path), "/%s", SavingName);
            sceMcChdir(path);
            push_sync();
            return;
        case 9:
            PS2CardFlow.message = 9;
            ++s.sub;
            sceMcOpen(SavingName, 0x203);
            push_sync();
            return;
        case 10:
            PS2CardFlow.message = 9;
            fd_278 = syncresult;
            ++s.sub;
            if (syncresult < 0) {
                push(6);
            } else {
                sceMcWrite(result, LoadSaveBuff, int32_t(LoadSaveBuffSize));
                push(1);
            }
            return;
        case 11:
            PS2CardFlow.message = 9;
            ++s.sub;
            if (syncresult < 0) {
                push(6);
            } else {
                sceMcFlush(fd_278);
                push(1);
            }
            return;
        case 12:
            PS2CardFlow.message = 9;
            ++s.sub;
            if (syncresult < 0) {
                push(6);
            } else {
                sceMcClose(fd_278);
                push(1);
            }
            return;
        case 13:
            PS2CardFlow.message = 9;
            ++s.sub;
            if (syncresult < 0) {
                push(6);
            } else {
                sceMcChdir("..");
                push(1);
            }
            return;
        case 14:
            PS2CardFlow.message = 9;
            ++s.sub;
            if (-1 < syncresult) return;
            push(6);
            return;
        case 15:
            PS2CardFlow.message = 9;
            ++s.sub;
            std::snprintf(path, sizeof(path), "/%s/*", SavingName);
            sceMcGetDir(path, 0x100, FolderTable);
            push_sync();
            return;
        case 16: {
            PS2CardFlow.message = 9;
            ++s.sub;
            if (syncresult != 5) {
                push(6);
                return;
            }
            int32_t kb = 0;
            for (int i = 0; i < 5; ++i) kb += int32_t((uint32_t(get32(FolderTable[i] + 0x10)) + 0x3ff) >> 10);
            if (kb != 0x4e) {
                push(6);
                return;
            }
            PS2SaveGameFine = 1;
            PSIOP_State = 2;
            --StateStack;
            return;
        }
        default:
            return;
    }
}

// [decomp] 0x00110188 MemCard_GET_SAVE_GAMES
// Lista as pastas "BASLUS-20579*" do cartão em FolderTable (só a primeira com PS2_one_codename).
void MemCard_GET_SAVE_GAMES() {
    if (sceMcSync() == 0) return;
    PS2CardFlow.message = 0;
    CARDSTATE_tag& s = top();
    char path[0x100];
    switch (++s.sub) {
        case 1:
            push(10);
            return;
        case 2:
            if (PS2_one_codename != 0) return;
            push(0x15);
            return;
        case 3:
            PS2CardFlow.message = 0xb;
            std::snprintf(path, sizeof(path), "/%s*", kDirName);
            sceMcGetDir(path, PS2_one_codename == 0 ? 0x100 : 1, FolderTable);
            return;
        case 4:
            PS2CardFlow.message = 0xb;
            --StateStack;
            NumberSaveGamesGot = syncresult;
            GettingSaveGames = 0;
            return;
        default:
            return;
    }
}

namespace {

// MemCard_DELETE_DATA e MemCard_DELETE_DATA2: lista a pasta, apaga os arquivos e a pasta.
bool delete_files(int32_t& n, uint32_t message, uint8_t sub) {
    char path[0x100];
    switch (sub) {
        case 5:
            PS2CardFlow.message = message;
            std::snprintf(path, sizeof(path), "/%s/*", SavingName);
            sceMcGetDir(path, 0x100, FolderTable);
            return true;
        case 6:
            n = 0;
            PS2CardFlow.message = message;
            NumberSaveGamesGot = syncresult;
            return true;
        case 7:
            PS2CardFlow.message = message;
            if (n < NumberSaveGamesGot) {
                uint16_t attr;
                std::memcpy(&attr, FolderTable[n] + 0x14, 2);
                if ((attr & 0x20) == 0) {
                    std::snprintf(path, sizeof(path), "/%s/%s", SavingName, reinterpret_cast<char*>(FolderTable[n] + 0x20));
                    sceMcDelete(path);
                }
                ++n;
                --top().sub;
            }
            return true;
        case 8:
            PS2CardFlow.message = message;
            std::snprintf(path, sizeof(path), "/%s", SavingName);
            sceMcDelete(path);
            return true;
        default:
            return false;
    }
}

}  // namespace

// [decomp] 0x00110320 MemCard_DELETE_DATA
// Apaga o perfil (com a pergunta de MemCard_ASK_DELETE): PSIOP_State 4 no fim.
void MemCard_DELETE_DATA() {
    if (sceMcSync() == 0) return;
    if (syncresult < 0 && syncresult != -4 && syncresult != -5 && -10 < syncresult) {
        syncresult = 0;
        push(0x13);
        return;
    }
    PS2CardFlow.message = 0;
    CARDSTATE_tag& s = top();
    const uint8_t sub = ++s.sub;
    switch (sub) {
        case 1:
            push(10);
            return;
        case 2:
            if (OverWriteCheckDone == 0) {
                OverWriteCheckDone = 1;
                push(0x14);
            }
            return;
        case 3:
            push(3);
            return;
        case 4:
            if (MC_Format != 0) return;
            push(0x13);
            return;
        case 9:
            PS2CardFlow.message = 0xc;
            --StateStack;
            PSIOP_State = 4;
            return;
        default:
            delete_files(nDelete_297, 0xc, sub);
            return;
    }
}

// [decomp] 0x00110638 MemCard_DELETE_DATA2
// Apaga a pasta de uma gravação que falhou e recomeça a gravação (o passo 1 da pilha).
void MemCard_DELETE_DATA2() {
    if (sceMcSync() == 0) return;
    PS2CardFlow.message = 0;
    CARDSTATE_tag& s = top();
    const uint8_t sub = ++s.sub;
    switch (sub) {
        case 1:
            push(10);
            return;
        case 9:
            StateStack = 1;
            CardState[1].sub = 0;
            syncresult = 0;
            return;
        default:
            delete_files(nDelete_301, 9, sub);
            return;
    }
}

// [decomp] 0x00110870 MemCard_LOAD_DATA
// Lê o arquivo de dados do perfil e confere o crc32buf dos últimos 4 bytes (zerados depois): PSIOP_State 2.
void MemCard_LOAD_DATA() {
    if (sceMcSync() == 0) return;
    CARDSTATE_tag& s = top();
    if (syncresult < 0 && 3 < s.sub) {
        push(0xb);
        return;
    }
    PS2CardFlow.message = 0;
    char path[0x100];
    switch (++s.sub) {
        case 1:
            push(10);
            return;
        case 2:
            push(3);
            return;
        case 3:
            if (MC_Format != 0) return;
            push(0xb);
            return;
        case 4:
            PS2CardFlow.message = 8;
            std::snprintf(path, sizeof(path), "/%s", SavingName);
            sceMcChdir(path);
            return;
        case 5:
            PS2CardFlow.message = 8;
            sceMcOpen(SavingName, 3);
            return;
        case 6:
            PS2CardFlow.message = 8;
            fd_305 = syncresult;
            sceMcSeek(syncresult, 0, 2);
            return;
        case 7:
            PS2CardFlow.message = 8;
            LoadSaveBuffSize = uint32_t(syncresult);
            // Só no port: no máximo o buffer do perfil (0x1000 bytes; o original leria o arquivo inteiro). Um
            // arquivo maior acaba em "dados corrompidos" pelo crc.
            if (0x1000 < LoadSaveBuffSize) LoadSaveBuffSize = 0x1000;
            sceMcSeek(fd_305, 0, 0);
            return;
        case 8:
            PS2CardFlow.message = 8;
            sceMcRead(fd_305, LoadSaveBuff, int32_t(LoadSaveBuffSize));
            return;
        case 9: {
            PS2CardFlow.message = 8;
            // Só no port: um arquivo com menos de 4 bytes não tem o crc (o original leria antes do buffer).
            if (LoadSaveBuffSize < 4) {
                push(0xd);
                return;
            }
            const int32_t crc = int32_t(crc32buf(reinterpret_cast<const char*>(LoadSaveBuff), LoadSaveBuffSize - 4));
            const int32_t stored = get32(LoadSaveBuff + LoadSaveBuffSize - 4);
            std::memset(LoadSaveBuff + LoadSaveBuffSize - 4, 0, 4);
            if (stored == crc) {
                sceMcClose(fd_305);
                return;
            }
            push(0xd);
            return;
        }
        case 10:
            PS2CardFlow.message = 8;
            return;
        case 11:
            PS2CardFlow.message = 8;
            --StateStack;
            PSIOP_State = 2;
            return;
        default:
            return;
    }
}

// [decomp] 0x00110b98 HandlePS2CardState
// Um quadro da máquina de estados do cartão (PS2PollMemoryCard): roda o passo do topo da pilha até a pilha
// esvaziar, uma operação ficar pendente ou 32 passos (no PS2, ~10 contagens do RCNT1).
void HandlePS2CardState() {
    ++mc.polls;
    for (int step = 0; step < 32; ++step) {
        if (CardState[StateStack].state == 0) return;
        if (mc.pending && mc.issued == mc.polls) return;  // só no port: espera o próximo quadro
        switch (CardState[StateStack].state) {
            case 1:
                if (sceMcSync() != 0) --StateStack;
                break;
            case 2: MemCard_FORMAT(); break;
            case 3: MemCard_CHECK_FOR_CARD(); break;
            case 4:
                if (MC_Format == 0) {
                    --StateStack;
                } else {
                    StateStack = 0;
                    PSIOP_State = 4;
                }
                break;
            case 5: MemCard_CHECK_FREE_SPACE(); break;
            case 6: MemCard_SAVEERROR(); break;
            case 7: MemCard_CREATE_DIR(); break;
            case 8: MemCard_ASK_FORMAT(); break;
            case 9: MemCard_SAVE_DATA(); break;
            case 10: MemCard_GET_CARD_STATUS(); break;
            case 0xb: MemCard_LOADERROR(); break;
            case 0xc: MemCard_LOAD_DATA(); break;
            case 0xd: MemCard_FILE_CORRUPT(); break;
            case 0xe: MemCard_GET_SAVE_GAMES(); break;
            case 0xf: MemCard_SAVEERROR2(); break;
            case 0x10: MemCard_ASK_OVERWRITE(); break;
            case 0x11: MemCard_DELETE_DATA(); break;
            case 0x12: MemCard_DELETE_DATA2(); break;
            case 0x13: MemCard_DELETE_ERROR(); break;
            case 0x14: MemCard_ASK_DELETE(); break;
            case 0x15: MemCard_CHECK_FOR_CARD2(); break;
            case 0x16: MemCard_BOOTUP_CHECK(); break;
            case 0x17: MemCard_FORMAT_ERROR(); break;
            default: return;
        }
        if (StateStack < 0 || kCardStates <= StateStack) {  // só no port: a pilha nunca sai do vetor
            StateStack = 0;
            return;
        }
    }
}

// [decomp] 0x00110f48 InitaliseMemoryCard
void InitaliseMemoryCard() {
    sceMcInit();
    MC_Info = 0;
    MC_Type = 0;
    CardState[0].state = 0;
    CardState[0].sub = 0;
    MC_Format = 0;
    StateStack = 0;
    PS2PollMemoryCard();
}

// [decomp] 0x00110fa0 PS2PollMemoryCard
void PS2PollMemoryCard() { HandlePS2CardState(); }

namespace {

// O nome da pasta e o título do ícone (SavingName, GameTitle).
void set_names(const char* name) {
    std::snprintf(SavingName, sizeof(SavingName), "%s%s", kDirName, name != nullptr ? name : "");
    uint8_t title[0x100];
    std::snprintf(reinterpret_cast<char*>(title), sizeof(title), "%s%s", kGameTitleASCII, name != nullptr ? name : "");
    title[0x3f] = 0;  // só no port: o título cabe em GameTitle
    AsciiString2Sjis(title, GameTitle);
}

}  // namespace

// [decomp] 0x00110df0 psiSaveData
int32_t psiSaveData(uint8_t* data, uint32_t size, char* name) {
    PSIOP_State = 1;
    PS2CardFlow.message = 0;
    OverWriteCheckDone = 0;
    PS2SaveGameFine = 0;
    LoadSaveBuff = data;
    LoadSaveBuffSize = size;
    set_names(name);
    const uint32_t crc = crc32buf(reinterpret_cast<const char*>(LoadSaveBuff), LoadSaveBuffSize);
    put32(LoadSaveBuff + LoadSaveBuffSize, crc);
    LoadSaveBuffSize += 4;
    push(9);
    return 0;
}

// [decomp] 0x00110fe0 psiStartSaveEnum
void psiStartSaveEnum(int8_t one) {
    push(0xe);
    PS2CardFlow.message = 0;
    GettingSaveGames = -1;
    PS2_one_codename = uint8_t(one);
    SaveGamesReturned = 0;
}

// [decomp] 0x00111038 psiGetNextSave
intptr_t psiGetNextSave() {
    if (GettingSaveGames == -1) return -1;
    if (NumberSaveGamesGot <= SaveGamesReturned) return 0;
    const int32_t n = SaveGamesReturned++;
    if (0x100 <= n) return 0;  // só no port: dentro de FolderTable
    return reinterpret_cast<intptr_t>(FolderTable[n] + 0x20 + 12);
}

// [decomp] 0x00111098 psiEndSaveEnum
void psiEndSaveEnum() {}

// [decomp] 0x001110a0 psiLoadData
int32_t psiLoadData(uint8_t* data, uint32_t*, char* name) {
    PS2CardFlow.message = 0;
    PSIOP_State = 0;
    LoadSaveBuff = data;
    set_names(name);
    push(0xc);
    return 0;
}

// [decomp] 0x00111158 psiLoadingData
uint32_t psiLoadingData() { return PSIOP_State; }

// [decomp] 0x00111160 psiSavingData
uint32_t psiSavingData() { return PSIOP_State; }

// [decomp] 0x00111168 psiDeleteData
int32_t psiDeleteData(char* name) {
    PS2CardFlow.message = 0;
    PSIOP_State = 0;
    OverWriteCheckDone = 0;
    set_names(name);
    push(0x11);
    return 0;
}

// [decomp] 0x00111220 psiDeletingData
uint32_t psiDeletingData() { return PSIOP_State; }

// [decomp] 0x00111228 psiDoBootupCheck
int32_t psiDoBootupCheck() {
    PS2CardFlow.message = 0;
    push(0x16);
    PSIOP_State = 0;
    return 0;
}

// [decomp] 0x00120b88 AsciiString2Sjis
// Cada caractere vira um Shift-JIS de 2 bytes (byte alto primeiro, como no título do icon.sys).
void AsciiString2Sjis(const uint8_t* in, uint16_t* out) {
    const size_t n = std::strlen(reinterpret_cast<const char*>(in));
    size_t i = 0;
    for (; i < n; ++i) {
        const uint32_t c = Ascii2Sjis(in[i]);
        out[i] = uint16_t(((c >> 8) & 0xff) | ((c & 0xff) << 8));
    }
    out[i] = 0;
}

// [decomp] 0x001201c0 Ascii2Sjis
// Dígitos e letras pela base de ascii_table; a pontuação por ascii_special; o resto 0.
uint32_t Ascii2Sjis(uint8_t c) {
    uint32_t special = 0;
    uint32_t row = 0;
    if (uint32_t(c) - 0x20 < 0x10) {
        special = 1;
    } else if (uint32_t(c) - 0x30 < 10) {
        row = 0;
    } else if (uint32_t(c) - 0x3a < 7) {
        special = 0xb;
    } else if (uint32_t(c) - 0x41 < 0x1a) {
        row = 1;
    } else if (uint32_t(c) - 0x5b < 6) {
        special = 0x25;
    } else if (uint32_t(c) - 0x61 < 0x1a) {
        row = 2;
    } else {
        special = 0x3f;
        if (3 < uint32_t(c) - 0x7b) return 0;
    }
    if (special == 0) {
        uint16_t base, sub;
        std::memcpy(&base, ascii_table + row * 4, 2);
        std::memcpy(&sub, ascii_table + row * 4 + 2, 2);
        return (uint32_t(base) + c - sub) & 0xffff;
    }
    uint16_t sjis;
    std::memcpy(&sjis, ascii_special + (c - (special + 0x1f)) * 4, 2);
    return sjis;
}

// ---- os cartões do PC ----

namespace nf {

namespace {

std::string parent_of(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::wstring widen(const std::string& s) {
    std::wstring w;
    for (const char c : s) w.push_back(wchar_t(uint8_t(c)));
    return w;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (const wchar_t c : w) s.push_back(c < 256 ? char(c) : '?');
    return s;
}

}  // namespace

bool MemoryCard::exists(const std::string& path, bool* dir) const {
    if (path.empty() || dirs.count(path) != 0) {
        if (dir != nullptr) *dir = true;
        return true;
    }
    if (dir != nullptr) *dir = false;
    return files.count(path) != 0;
}

bool MemoryCard::make_dir(const std::string& path) {
    bool dir = false;
    if (exists(path, &dir) || !exists(parent_of(path), &dir) || !dir) return false;
    dirs.insert(path);
    return true;
}

bool MemoryCard::read(const std::string& path, std::vector<uint8_t>* out) const {
    const auto it = files.find(path);
    if (it == files.end()) return false;
    *out = it->second;
    return true;
}

bool MemoryCard::write(const std::string& path, const std::vector<uint8_t>& data) {
    bool dir = false;
    if (!exists(parent_of(path), &dir) || !dir || dirs.count(path) != 0) return false;
    files[path] = data;
    return true;
}

bool MemoryCard::remove(const std::string& path) {
    if (files.erase(path) != 0) return true;
    if (dirs.count(path) == 0 || !list(path).empty()) return false;
    dirs.erase(path);
    return true;
}

std::vector<PcCard::Entry> MemoryCard::list(const std::string& dir) const {
    std::vector<Entry> out;
    const std::string prefix = dir.empty() ? std::string() : dir + "/";
    const auto child = [&](const std::string& p) {
        return p.compare(0, prefix.size(), prefix) == 0 && p.size() > prefix.size() &&
               p.find('/', prefix.size()) == std::string::npos;
    };
    for (const std::string& d : dirs) {
        if (child(d)) out.push_back(Entry{d.substr(prefix.size()), true, 0});
    }
    for (const auto& f : files) {
        if (child(f.first)) out.push_back(Entry{f.first.substr(prefix.size()), false, uint32_t(f.second.size())});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

void MemoryCard::clear() {
    files.clear();
    dirs.clear();
}

FolderCard::FolderCard(std::wstring root) : root_(std::move(root)) {
    while (!root_.empty() && (root_.back() == L'\\' || root_.back() == L'/')) root_.pop_back();
}

std::wstring FolderCard::full(const std::string& path) const {
    std::wstring w = root_;
    if (!path.empty()) {
        w += L'\\';
        for (const char c : path) w.push_back(c == '/' ? L'\\' : wchar_t(uint8_t(c)));
    }
    return w;
}

bool FolderCard::exists(const std::string& path, bool* dir) const {
    if (path.empty()) {
        if (dir != nullptr) *dir = true;
        return true;  // a pasta do cartão é criada na primeira escrita
    }
    const DWORD a = GetFileAttributesW(full(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
        if (dir != nullptr) *dir = false;
        return false;
    }
    if (dir != nullptr) *dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool FolderCard::make_dir(const std::string& path) {
    CreateDirectoryW(root_.c_str(), nullptr);
    bool dir = false;
    if (exists(path, &dir) || !exists(parent_of(path), &dir) || !dir) return false;
    return CreateDirectoryW(full(path).c_str(), nullptr) != 0;
}

bool FolderCard::read(const std::string& path, std::vector<uint8_t>* out) const {
    FILE* f = nullptr;
    if (_wfopen_s(&f, full(path).c_str(), L"rb") != 0 || f == nullptr) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) != 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

bool FolderCard::write(const std::string& path, const std::vector<uint8_t>& data) {
    bool dir = false;
    if (!exists(parent_of(path), &dir) || !dir) return false;
    CreateDirectoryW(root_.c_str(), nullptr);
    FILE* f = nullptr;
    if (_wfopen_s(&f, full(path).c_str(), L"wb") != 0 || f == nullptr) return false;
    const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

bool FolderCard::remove(const std::string& path) {
    bool dir = false;
    if (!exists(path, &dir)) return false;
    return dir ? RemoveDirectoryW(full(path).c_str()) != 0 : DeleteFileW(full(path).c_str()) != 0;
}

std::vector<PcCard::Entry> FolderCard::list(const std::string& dir) const {
    std::vector<Entry> out;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW((full(dir) + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        Entry e;
        e.name = narrow(name);
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = e.dir ? 0 : fd.nFileSizeLow;
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

void FolderCard::clear() {
    for (const Entry& e : list("")) {
        if (e.dir) {
            for (const Entry& f : list(e.name)) remove(e.name + "/" + f.name);
        }
        remove(e.name);
    }
}

void set_card(PcCard* c) { g_card = c; }

PcCard& card() { return the_card(); }

std::wstring default_save_dir() {
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir + L"saves";
}

uint32_t card_operations() { return mc.operations; }

}  // namespace nf

void psi_card_debug_power_on() {
    PS2CardFlow = PS2CARDFLOW_tag{};
    std::memset(CardState, 0, sizeof(CardState));
    StateStack = 0;
    PSIOP_State = 0;
    syncresult = 0;
    PS2SaveGameFine = 0;
    OverWriteCheckDone = 0;
    NumberSaveGamesGot = 0;
    GettingSaveGames = 0;
    SaveGamesReturned = 0;
    PS2_one_codename = 0;
    MC_Info = 0;
    MC_Format = 0;
    MC_Type = 0;
    LoadSaveBuff = nullptr;
    LoadSaveBuffSize = 0;
    std::memset(SavingName, 0, sizeof(SavingName));
    std::memset(GameTitle, 0, sizeof(GameTitle));
    std::memset(FolderTable, 0, sizeof(FolderTable));
    fd_271 = fd_278 = fd_305 = 0;
    nDelete_297 = nDelete_301 = 0;
    mc = Libmc{};
    g_memory_card.clear();
}

int psi_card_describe(char* out, size_t size) {
    uint32_t saves = 0;
    for (const nf::PcCard::Entry& e : the_card().list("")) {
        saves += e.dir && e.name.compare(0, sizeof(kDirName) - 1, kDirName) == 0;
    }
    return std::snprintf(out, size, "cartao %s pilha %d estado %u/%u resultado %u mensagem %u perfis %u ops %u",
                         g_card != nullptr ? "pasta" : "memoria", StateStack, CardState[StateStack < 0 ? 0 : StateStack].state,
                         CardState[StateStack < 0 ? 0 : StateStack].sub, PSIOP_State, PS2CardFlow.message, saves,
                         mc.operations);
}
