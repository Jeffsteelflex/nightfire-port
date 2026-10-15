// A camada de som do jogo de carro (driving/sound/audio.h): ASystem, ASoundManager (Init, BuildPaths, o EAudioUpdate),
// AMix, ABank, AIndex, AFX, o TIMER e o rrandom. Portado do MIPS do DRIVING.ELF (BondAudioPS2.lib, SupportPS2.lib).
//
// Divergências (port):
//  - ASystem::SetOpts grava em +0x114 das opções o gancho de erro do jogo (UDevCons::AbortMessage); o SND do port
//    não chama ganchos de erro e o campo fica 0;
//  - o heap do SND (UMemory::Alloc(0x10000)) e os arquivos (UFileLoader) são vetores daqui; os bancos ficam com uma
//    folga de 0x100 bytes zerados no fim (o SNDDRV_dmqueue arredonda as transferências para 64 bytes e o PS2 lê além do
//    fim do arquivo);
//  - o ASoundManager::Init do port faz só a parte do som dos efeitos: os volumes, o AFX, o ASystem e os bancos (os
//    fluxos da música e das falas, AStream, e o AFader não estão portados);
//  - o relógio de 100 Hz do TIMER (a interrupção do contador 1 do EE) anda com o relógio do jogo (audio_set_timer);
//  - o random de seis palavras (0x0025a5ac) tem o seu estado aqui, sem os outros que o chamam no jogo.

#include "driving/sound/audio.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "driving/camera/dafi.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/snd.h"
#include "driving/sound/snd_spu.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/stream.h"
#include "driving/sound/snddrv_iop.h"
#include "platform/crt_compat.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

// ASoundManager::fLoadIOP (0x00355e58): 0 no DRIVING.ELF (o BASE.ELF já carregou os IRX), então o SND não carrega
// módulos.
constexpr uint32_t kLoadIop = 0;

std::vector<uint8_t>& heap() {
    static std::vector<uint8_t> h;
    return h;
}

struct Audio {
    bool ready = false;
    AudioFileLoader load;
    std::map<std::string, std::unique_ptr<AMix>, NoCaseLess> mixes;  // URefCounter<AMix>
    std::list<AMix*> mix_list;                                       // AMix::gList (os do .ini)
    std::map<std::string, std::unique_ptr<ABank>, NoCaseLess> banks;  // URefCounter<ABank>
    ABank empty;                                                       // 0x00356288
    ABank* categories[10] = {};                                        // ABank::fgBanks (0x003562a0)
    std::list<ABaseSound*> sounds;                                     // ASoundManager::fgSoundList
    int32_t count = 0;                                                 // ASoundManager::fgCount (0x00355e60)
    float time = 0;                                                    // ASoundManager::fgTime
    int32_t timer = 0;                                                 // TIMER (0x003c80d4)
    uint32_t rnd[6] = {0xf22d0e56, 0x883126e9, 0xc624dd2f, 0x0702c49c, 0x9e353f7d, 0x6fdf3b64};  // 0x00343810
    // AFX (0x00355ed8..0x00355f04, 0x00421c98..0x00421ccc)
    int32_t afx_request[2][2] = {{0, -1}, {0, -1}};  // {modo, prioridade} das duas vagas
    int32_t afx_current = -1;    // 0x00355ee8
    int32_t afx_inited = 0;      // 0x00355ef0
    float afx_level = 0;         // 0x00355ef4
    int32_t afx_paused = 0;      // 0x00355ef8
    int32_t afx_value = -1;      // 0x00355efc
    int32_t afx_delay = -1, afx_feedback = -1;  // 0x00355f00, 0x00355f04
    AMix* afx_mix[13] = {};      // 0x00421c98
    uint32_t afx_bus = 0x40;     // 0x00421ccc
    AudioStats stats;
};
Audio& A() {
    static Audio a;
    return a;
}

// [driving] 0x00355f80 os modos do AFX: o nome (o volume no .ini), "todas as vozes" (o nível do efeito em 0x7f), o
// índice na tabela dos modos do SND, o atraso e a realimentação.
struct AfxMode {
    const char* name;
    int32_t all_voices, mode, delay, feedback;
};
constexpr AfxMode kAfxModes[13] = {
    {"X:City", 0, 20, -1, -1},       {"X:Overpass", 1, 20, -1, -1},   {"X:AlpineTunnel", 1, 15, -1, -1},
    {"X:Grotto", 1, 14, -1, -1},     {"X:VolTunnel", 1, 9, -1, -1},   {"X:Compound", 1, 5, -1, -1},
    {"X:Canyon", 1, 6, -1, -1},      {"X:VolBowl", 1, 18, -1, -1},    {"X:SunkTanker", 1, 7, -1, -1},
    {"X:MetalTunnel", 1, 15, -1, -1}, {"X:LargeCave", 1, 16, -1, -1}, {"X:SmallCave", 1, 16, -1, -1},
    {"X:BigTunnel", 1, 22, -1, -1},
};
// [driving] 0x00355f08 o modo do SNDfxinitbus por índice.
constexpr int32_t kAfxModeValues[29] = {5,  5,  5,  10, 10, 10, 10, 20, 20, 20, 20, 30, 30, 30, 30,
                                        30, 30, 30, 30, 40, 50, 50, 50, 50, 50, 50, 100, 110, 120};

// As mixagens e o mestre de cada uma (ASoundManager::Init).
struct MixMaster {
    const char* name;
    const char* master;
};
constexpr MixMaster kMixMasters[] = {
    {"0:Fader2", "0:Fader1"},      {"0:Fader1", "0:Fader0"},        {"0:Fader0", "1:Effects"},
    {"Speech", "1:Effects"},       {"Ambience", "0:Fader2"},        {"Asphalt", "0:Fader2"},
    {"Blowout", "0:Fader2"},       {"Booster", "0:Fader2"},         {"Bullet Impact", "0:Fader2"},
    {"Bullet Ricochet", "0:Fader2"}, {"Characters", "0:Fader2"},    {"Collisions", "0:Fader2"},
    {"Explosions", "0:Fader2"},    {"ExplosionBass", "0:Fader2"},   {"Friendly", "0:Fader2"},
    {"Gadgets", "0:Fader2"},       {"Gatling Gun", "0:Fader2"},     {"Giotto", "0:Fader2"},
    {"Helicopter", "0:Fader2"},    {"Henchmen Fire", "0:Fader2"},   {"Horns", "0:Fader2"},
    {"Landings", "0:Fader1"},      {"Machine Gun", "0:Fader2"},     {"Menu Sounds", "1:Effects"},
    {"Music", "1:Music"},          {"NIS", "0:Fader2"},             {"Object Sounds", "0:Fader2"},
    {"OffRoad", "0:Fader2"},       {"Opponents", "0:Fader2"},       {"Player Car", "0:Fader2"},
    {"Player Grunts", "0:Fader1"}, {"Player Wind", "0:Fader1"},     {"POV Weapons", "0:Fader2"},
    {"Power Ups", "0:Fader2"},     {"Rail", "0:Fader2"},            {"Scrapes", "0:Fader2"},
    {"Shell Casings", "0:Fader2"}, {"Skids", "0:Fader1"},           {"Smackables", "0:Fader2"},
    {"Tank Treads", "0:Fader2"},   {"Tracking System", "0:Fader2"}, {"Traffic Cars", "0:Fader2"},
    {"Weapons Fire", "0:Fader2"},  {"Weapons Init", "0:Fader2"},    {"Weapons Reload", "0:Fader2"},
    {"Weapons Transit", "0:Fader2"}, {"Wood", "0:Fader2"},          {"World Sounds", "0:Fader2"},
};

int32_t round_ee(float x) {
    // (int)(x + (x < 0 ? -0,5 : 0,5)) com a FPU do EE
    return ee::cvt_w(ee::add(ee::lt(x, 0.0f) ? -0.5f : 0.5f, x));
}

}  // namespace

bool NoCaseLess::operator()(const std::string& a, const std::string& b) const {
    return dafi_strcasecmp(a.c_str(), b.c_str()) < 0;
}

std::list<ABaseSound*>& sound_list() { return A().sounds; }
int32_t& sound_count() { return A().count; }
int32_t TIMER_gettick() { return A().timer; }
void audio_set_timer(int32_t ticks) { A().timer = ticks; }

// [driving] 0x002d0aa8 rrandom: o random (0x0025a5ac, as seis palavras somadas em cascata como o iSNDrandom do SND)
// vezes 2^-32. Peculiaridade mantida: o random devolve a palavra estendida SEM sinal para 64 bits (dsll32 + dsrl32), então
// o bltz do rrandom (o caminho sem sinal: srl 1 | o bit de baixo, cvt.s.w, add.s) nunca salta e o cvt.s.w lê a palavra
// COM sinal: o resultado fica em [-0,5, 0,5), não em [0, 1) (o nf_driving_check, parte 13, o confere no interpretador).
float rrandom() {
    uint32_t* r = A().rnd;
    const uint32_t s5 = r[5], s4 = r[4], s3 = r[3], s2 = r[2], s1 = r[1];
    const uint32_t u1 = s5 + s4;
    const uint32_t c1 = (u1 < s5 || u1 < s4) ? 1 : 0;
    const uint32_t u2 = u1 + s3 + c1;
    const uint32_t u3 = u2 + s2 + (u2 < s3 ? 1 : 0);
    r[5] = s5 + 1;
    const uint32_t u4 = u3 + s1 + (u3 < s2 ? 1 : 0);
    r[0] = u4 + r[0] + (u4 < s1 ? 1 : 0);
    r[1] = u4;
    r[2] = u3;
    r[3] = u2;
    r[4] = u1;
    if (r[5] == 0) {
        r[4] = u1 + 1;
        if (r[4] == 0) {
            r[3] = u2 + 1;
            if (r[3] == 0) {
                r[2] = u3 + 1;
                if (r[2] == 0) {
                    r[1] = u4 + 1;
                    if (r[1] == 0) r[0] += 1;
                }
            }
        }
    }
    // (v0 = r[0] estendido sem sinal: nunca negativo em 64 bits; mtc1 leva os 32 bits de baixo ao cvt.s.w)
    return ee::mul(ee::cvt_s(int32_t(r[0])), ps2::ffloat(0x2f800000u));
}

// ---- AMix ----------------------------------------------------------------------------------------------------------

// [driving] 0x002f2d50 AMix::Add(char *)
AMix* AMix_Add(const char* name) {
    auto& m = A().mixes;
    auto it = m.find(name);
    if (it == m.end()) {
        it = m.emplace(name, std::make_unique<AMix>()).first;
        nf::str_copy_trunc(it->second->name, sizeof it->second->name, name);  // AMix::AMix (+0x30)
    }
    ++it->second->refs;  // URefCounter<AMix>::AddReference
    return it->second.get();
}

// [driving] 0x002f2dc8 AMix::Get(char *): sem a mixagem no .ini, avisa e a cria.
AMix* AMix_Get(const char* name) {
    auto& m = A().mixes;
    auto it = m.find(name);
    if (it != m.end()) return it->second.get();
    // printf("WARNING: Mix not found in INI file... manually adding %s")
    return AMix_Add(name);
}

// [driving] 0x002f2e18 AMix::Remove: uma referência a menos (a mixagem fica no mapa: URefCounter só apaga com 0, e
// as do jogo nunca chegam a 0 antes do fim).
void AMix_Remove(AMix* mix) {
    if (mix != nullptr && mix->refs > 0) --mix->refs;
}

// [driving] 0x002f34d0 AMix::GetVolume(void): anda a transição (a cada chamada) e multiplica pelos mestres.
float AMix_GetVolume(AMix* m) {
    if (m->transition != 0) {
        const float step = m->step;
        const float v = ee::add(m->volume, step);
        m->volume = v;
        bool done;
        if (ee::le(0.0f, step) || ee::lt(m->target, v)) {
            if (ee::le(step, 0.0f) || ee::lt(v, m->target)) done = false;
            else done = true;
        } else {
            done = true;
        }
        if (done) {
            m->volume = m->target;
            m->transition = 0;
        }
    }
    float r = m->volume;
    if (m->master != nullptr) r = ee::mul(r, AMix_GetVolume(m->master));
    return r;
}

// [driving] 0x002f3360 AMix::SetMaster(char *)
void AMix_SetMaster(AMix* m, const char* name) {
    if (m->master != nullptr) AMix_Remove(m->master);  // ClearMaster
    m->master = AMix_Add(name);
}

// [driving] 0x002f3450 AMix::Reset(int): cada mixagem do .ini volta ao volume do .ini em ticks quadros.
void AMix_Reset(int32_t ticks) {
    const int32_t t = 0 < ticks ? ticks : 1;
    for (AMix* m : A().mix_list) AMix_SetTransition(m->initial, m, t);
}

// [driving] 0x002f3578 AMix::GetPresetVolume
float AMix_GetPresetVolume(AMix* m) { return m->initial; }

// [driving] 0x002f33a0 AMix::SetTransition(float, int)
void AMix_SetTransition(float to, AMix* m, int32_t ticks) {
    if (ee::lt(to, 0.0f)) to = 0.0f;
    else if (ee::lt(1.0f, to)) to = 1.0f;
    m->transition = 1;
    if (ticks < 1) ticks = 1;
    m->saved = m->volume;
    m->target = to;
    m->step = ee::div(ee::sub(to, m->volume), ee::cvt_s(ticks));
}

// [driving] 0x002f3058 AMix::Load(char *): as linhas "nome = volume, prioridade".
void AMix_Load(const std::vector<uint8_t>& ini) {
    const int32_t size = int32_t(ini.size());
    int32_t p = 0;
    while (p < size) {
        char line[128];
        int32_t n = 0;
        while (p < size && ini[size_t(p)] != '\r' && n < 0x7f) line[n++] = char(ini[size_t(p++)]);
        line[n] = 0;
        while (p < size && ini[size_t(p)] != '\r') ++p;
        p += 2;
        // sscanf(line, "%[^=]=%f, %d", name, &volume, &priority)
        char name[128];
        const char* eq = std::strchr(line, '=');
        if (eq == nullptr || eq == line) continue;
        const size_t len = size_t(eq - line);
        std::memcpy(name, line, len);
        name[len] = 0;
        const char* q = eq + 1;
        char* end = nullptr;
        const float volume = dafi_atof(q);
        std::strtod(q, &end);
        if (end == q) continue;
        q = end;
        while (*q == ' ' || *q == '\t') ++q;
        if (*q != ',') continue;
        ++q;
        long priority = std::strtol(q, &end, 10);
        if (end == q) continue;
        size_t l = std::strlen(name);
        while (l != 0 && name[l - 1] == ' ') name[--l] = 0;
        AMix* m = AMix_Add(name);
        A().mix_list.push_back(m);
        m->volume = volume;
        m->saved = volume;
        m->preset = volume;
        m->priority = int32_t(priority);
        m->initial = volume;
        std::snprintf(m->name, sizeof m->name, "%s", name);
    }
}

// ---- AIndex, ABank -------------------------------------------------------------------------------------------------

int32_t AIndex::Lookup(const char* name) const {
    if (name == nullptr) return -1;
    auto it = by_name.find(name);
    return it == by_name.end() ? -1 : it->second;
}

const char* AIndex::Lookup(int32_t number) const {
    auto it = by_number.find(number);
    return it == by_number.end() ? "" : it->second.c_str();
}

namespace {

// [driving] 0x002f54e0 AIndex::AIndex(char *, char *): os "#define NOME número" do .h; o nome guardado é "SFX_" mais
// o que vem depois do primeiro '_' a partir do quinto caractere (o original olha o quinto e depois procura), até o '.'.
void AIndex_Build(AIndex& idx, const std::vector<uint8_t>& text) {
    std::string s(text.begin(), text.end());
    size_t p = 0;
    for (;;) {
        char token[32] = {};
        int value = 0;
        // sscanf(p, "#define%31s%d", token, &value)
        const char* c = s.c_str() + p;
        bool ok = std::strncmp(c, "#define", 7) == 0;
        if (ok) {
            c += 7;
            while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') ++c;
            int n = 0;
            while (*c != 0 && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n' && n < 31) token[n++] = *c++;
            token[n] = 0;
            ok = n > 0;
            if (ok) {
                char* end = nullptr;
                const long v = std::strtol(c, &end, 10);
                ok = end != c;
                value = int(v);
            }
        }
        if (ok) {
            std::string name;
            const char* t = token;
            const char* tail = nullptr;
            if (t[4] == '_') {
                tail = t + 4;
            } else if (t[4] != 0) {
                for (const char* q = t + 5; *q != 0; ++q)
                    if (*q == '_') {
                        tail = q;
                        break;
                    }
            }
            name = tail != nullptr ? std::string("SFX_") + (tail + 1) : std::string(token);
            const size_t dot = name.find('.', 4);
            if (dot != std::string::npos) name.resize(dot);
            idx.by_number.emplace(value, name);
            idx.by_name.emplace(name, value);
        }
        const size_t next = s.find('#', p + 1);
        if (next == std::string::npos) break;
        p = next;
    }
}

}  // namespace

ABank* ABank_Category(int32_t category) {
    ABank* b = category >= 0 && category < 10 ? A().categories[category] : nullptr;
    return b != nullptr ? b : &A().empty;
}

// [driving] 0x002fd308 ABank::Get(char *)
ABank* ABank_Get(const char* name) {
    auto it = A().banks.find(name);
    return it == A().banks.end() ? &A().empty : it->second.get();
}

namespace {

// [driving] 0x002fd458 ABank::ABank(char *, bool): o .h e o .bnk; depois do download só o cabeçalho fica.
void ABank_Construct(ABank& b, const std::string& name) {
    b.name = name;
    const size_t dot = name.rfind('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    AIndex_Build(b.index, A().load("data/audio/" + stem + ".h"));
    std::vector<uint8_t> file = A().load("data/audio/" + name);  // UFileLoader::FileLoadz(fgPath + name)
    if (file.empty()) {
        b.status = -0x13;
        return;
    }
    file.resize(file.size() + 0x100, 0);
    b.data = std::move(file);
    b.status = SNDbankadd(&b.snd_bank, b.data.data());
    if (b.status == 7) {
        std::vector<uint8_t> header(size_t(SNDbankheadersize(b.snd_bank)) + 0x100, 0);
        SNDbankheadercopy(header.data(), b.snd_bank);
        b.data = std::move(header);
    }
}

// [driving] 0x002fd348 ABank::Remove
void ABank_Remove(ABank* b) {
    if (b == nullptr || b == &A().empty || --b->refs > 0) return;
    if (b->snd_bank >= 0) SNDbankremove(b->snd_bank);
    for (auto it = A().banks.begin(); it != A().banks.end(); ++it)
        if (it->second.get() == b) {
            A().banks.erase(it);
            break;
        }
}

// [driving] 0x002fd288 ABank::Load(char *, int): o banco da categoria.
ABank* ABank_Load(const char* name, int32_t category) {
    if (name != nullptr) {
        ABank_Remove(A().categories[category]);
        auto it = A().banks.find(name);  // ABank::Load(char *, bool) 0x002fd1f8
        if (it == A().banks.end()) {
            it = A().banks.emplace(name, std::make_unique<ABank>()).first;
            ABank_Construct(*it->second, name);
        }
        ++it->second->refs;
        A().categories[category] = it->second.get();
        it->second->category = category;
        if (it->second->snd_bank >= 0) ++A().stats.banks;
        else ++A().stats.banks_failed;
    }
    return ABank_Category(category);
}

}  // namespace

// ---- AFX -----------------------------------------------------------------------------------------------------------

// [driving] 0x002f7ca0 AFX::Init
void AFX_Init() {
    for (int i = 0; i < 13; ++i) A().afx_mix[i] = AMix_Add(kAfxModes[i].name);
    A().afx_inited = 1;
    A().afx_bus = 0x40;
}

// [driving] 0x002f7d18 AFX::SetMode(int, int, int): o pedido de maior prioridade de cada vaga.
void AFX_SetMode(int32_t mode, int32_t slot, int32_t priority) {
    int32_t* r = A().afx_request[slot];
    if (r[1] <= priority && (priority != r[1] || r[0] <= mode)) {
        r[1] = priority;
        r[0] = mode;
    }
}

// [driving] 0x002f7e48 AFX::IsAllVoices
int32_t AFX_IsAllVoices() { return A().afx_current != -1 ? kAfxModes[A().afx_current].all_voices : 0; }
int32_t AFX_CurrentMode() { return A().afx_current; }

// [driving] 0x002f7eb8 AFX::Update: o modo pedido; troca o reverb com o nível em zero e o leva ao volume da mixagem do
// modo (0,3 por passo da simulação subindo, 0,04 descendo).
void AFX_Update() {
    Audio& a = A();
    if (heap().empty() || a.afx_paused != 0) return;
    int32_t mode;
    if (a.afx_request[0][1] < a.afx_request[1][1]) {
        a.afx_request[1][1] = -1;
        mode = a.afx_request[1][0];
    } else {
        a.afx_request[0][1] = -1;
        mode = a.afx_request[0][0];
    }
    const float count = ee::cvt_s(a.count);
    float level;
    if (a.afx_current == mode) {
        AMix* m = a.afx_mix[mode];
        if (ee::le(AMix_GetVolume(m), a.afx_level)) {
            if (ee::le(a.afx_level, AMix_GetVolume(m))) return;
            a.afx_level = AMix_GetVolume(m);
            SNDfxmasterlevel(a.afx_bus, round_ee(ee::mul(a.afx_level, 127.0f)));
            return;
        }
        a.afx_level = ee::add(a.afx_level, ee::mul(count, 0.3f));
        if (ee::le(AMix_GetVolume(m), a.afx_level)) a.afx_level = AMix_GetVolume(m);
        level = ee::mul(a.afx_level, 127.0f);
    } else {
        const AfxMode& e = kAfxModes[mode];
        if (a.afx_value == e.mode) {
            a.afx_current = mode;
            return;
        }
        if (a.afx_current != -1) {
            a.afx_current = -1;
            return;
        }
        if (ee::le(a.afx_level, 0.0f)) {  // dpcmp(level, 0) < 1
            a.afx_current = mode;
            // printf("%s", nome)
            SNDfxinitbus(a.afx_bus, round_ee(ee::mul(a.afx_level, 127.0f)), kAfxModeValues[e.mode], e.delay,
                         e.feedback);
            a.afx_value = e.mode;
            a.afx_delay = e.delay;
            a.afx_feedback = e.feedback;
            return;
        }
        a.afx_level = ee::sub(a.afx_level, ee::mul(count, 0.04f));
        if (ee::le(a.afx_level, 0.0f)) a.afx_level = 0.0f;
        level = ee::mul(a.afx_level, 127.0f);
    }
    SNDfxmasterlevel(a.afx_bus, ee::cvt_w(ee::add(ee::lt(level, 0.0f) ? -0.5f : 0.5f, level)));
}

// ---- ASystem -------------------------------------------------------------------------------------------------------

// [driving] 0x002e6c88 ASystem::SetOpts(ASystem::Mode)
void ASystem_SetOpts(uint32_t mode) {
    SndSysOpts o;
    SNDSYS_getopts(&o);
    auto w16 = [&](int off, uint16_t v) { std::memcpy(o.raw + off, &v, 2); };
    if (kLoadIop == 0) o.raw[sndopt::kLoadModules] = 0;
    // (o.raw + 0x6c: o caminho dos IRX quando kLoadIop; + 0x114: o gancho de erro, ver o cabeçalho)
    if (mode < 2) {
        o.raw[sndopt::kModeCount] = 3;
        w16(sndopt::kModes + 0, 0x28);
        w16(sndopt::kModes + 2, 0x24);
        w16(sndopt::kModes + 4, 0x120);
        o.raw[sndopt::kIopVoices] = 6;
    } else if (1 < int32_t(mode)) {
        w16(sndopt::kModes + 0, 0x48);
        w16(sndopt::kModes + 2, 0x44);
        w16(sndopt::kModes + 4, 0x140);
        w16(sndopt::kModes + 6, 0x28);
        w16(sndopt::kModes + 8, 0x24);
        w16(sndopt::kModes + 10, 0x120);
        o.raw[sndopt::kIopVoices] = 6;
        o.raw[sndopt::kModeCount] = 6;
    }
    if (mode == 1) o.raw[sndopt::kOutputMode] = 2;
    else if (mode == 0) o.raw[sndopt::kOutputMode] = 1;
    else if (int32_t(mode) >= 2 && int32_t(mode) < 5) o.raw[sndopt::kOutputMode] = 3;
    // (outro modo: só a mensagem de erro, as opções vão como estão)
    SNDSYS_setopts(&o);
}

// [driving] 0x002e6db8 ASystem::ASystem(void)
int32_t ASystem_Construct() {
    // (0x003077e8: registra o SNDSYS_service como tarefa do jogo; no port o EAudioUpdate o chama)
    heap().assign(0x10000, 0);
    const int32_t r = SNDSYSI_init(heap().data(), 0x10000);
    SNDmastervol(0x40);
    return r;
}

// [driving] 0x002e6ee0 ASystem::Shutdown
void ASystem_Shutdown() {
    if (heap().empty()) return;
    SNDSYS_restore();
    heap().clear();
    heap().shrink_to_fit();
}

uint8_t* ASystem_heap() { return heap().empty() ? nullptr : heap().data(); }

// ---- ASoundManager -------------------------------------------------------------------------------------------------

// [driving] 0x002eb7a8 ASoundManager::Init (a parte do port; ver o cabeçalho)
bool audio_init(const AudioInit& init, const AudioFileLoader& load) {
    audio_shutdown();
    Audio& a = A();
    a.load = load;
    a.stats = AudioStats{};
    for (int i = 0; i < 10; ++i) a.categories[i] = nullptr;
    // O SND e o IOP de baixo (o PS2 os liga no boot).
    snd_power_on();
    snd_spu_power_on();
    snddrv_iop_start(snd_spu_libsd());
    // Os volumes: data/audio/<pista>.ini e as mixagens fixas com os seus mestres.
    AMix_Load(load("data/audio/" + init.track + ".ini"));
    for (const char* n : {"1:Effects", "1:Music", "0:Fader2", "0:Fader1", "0:Fader0"}) AMix_Add(n);
    for (const char* n : {"0:Fader2", "0:Fader1", "0:Fader0", "Speech"}) {
        AMix* m = AMix_Get(n);
        m->saved = m->volume;
        m->volume = 1.0f;
    }
    for (const MixMaster& mm : kMixMasters) AMix_SetMaster(AMix_Get(mm.name), mm.master);
    AFX_Init();
    ASystem_SetOpts(init.sound_mode);
    // ASystem::Init (o jogo só cria o ASystem se o ASoundManager::Init recebe 1)
    const int32_t r = ASystem_Construct();
    if (r < 0) {
        std::fprintf(stderr, "nf_driving: SNDSYSI_init devolveu %d\n", int(r));
        return false;
    }
    // AStream::SetPath, AStream::Create da música e das falas, AFader::Create("SpeechVsAmbience")
    // (driving/sound/audio_stream.cpp)
    if (!init.stream_file.empty() && init.big_open) {
        filesys_set_opener(init.big_open);
        audio_streams_create(init.stream_file);
    }
    ABank_Load("g_common.bnk", 8);
    const std::vector<uint8_t> ini = load("data/audio/banks.ini");
    if (!ini.empty()) {
        Dafi d;
        d.open(reinterpret_cast<const char*>(ini.data()), uint32_t(ini.size()));
        if (d.set_section(init.track.c_str()) < 0) d.set_section("default");
        static const char* kKeys[10] = {"Generic", "Collisions", "Smackables", "CarWeapons", "Weapons",
                                        "Ambience", "Misc",    "Actors",     nullptr,      "Engine"};
        for (int i = 0; i < 10; ++i) {
            if (kKeys[i] == nullptr) continue;
            const char* v = d.value(kKeys[i]);
            if (v != nullptr) ABank_Load(v, i);
        }
    }
    AMix* music = AMix_Get("1:Music");
    music->saved = music->volume;
    music->volume = init.music_volume;
    AMix* fx = AMix_Get("1:Effects");
    fx->saved = fx->volume;
    fx->volume = init.sfx_volume;
    a.count = 0;
    a.ready = true;
    return true;
}

bool audio_ready() { return A().ready; }

void audio_vehicle_forget();

void audio_shutdown() {
    Audio& a = A();
    audio_streams_destroy();
    while (!a.sounds.empty()) a.sounds.front()->destroy();
    audio_vehicle_forget();
    if (a.ready) {
        for (auto& b : a.banks)
            if (b.second->snd_bank >= 0) SNDbankremove(b.second->snd_bank);
        ASystem_Shutdown();
    }
    a.banks.clear();
    a.mixes.clear();
    a.mix_list.clear();
    for (int i = 0; i < 10; ++i) a.categories[i] = nullptr;
    a.ready = false;
    a.count = 0;
    a.time = 0;
    for (auto& r : a.afx_request) r[0] = 0, r[1] = -1;
    a.afx_current = -1;
    a.afx_level = 0;
    a.afx_value = -1;
}

void audio_sim_step() {
    // [driving] 0x00159d10 ESimFrameUpdate::~ESimFrameUpdate (fora do modo 3, o das cinemáticas)
    A().time = ee::add(A().time, ps2::ffloat(0x3c888889u));
    A().count += 1;
}

// [driving] 0x002ec238 ASoundManager::BuildPaths(AListener &)
static void ASoundManager_BuildPaths(AListener& l) {
    AFX_Update();
    auto& list = A().sounds;
    for (auto it = list.begin(); it != list.end();) {
        auto next = std::next(it);
        ABaseSound* s = *it;
        if (s->disabled == 0) {
            // |posição - ouvinte|² (vsub, vmul, vmulax/vmadday/vmaddz)
            const float dx = ee::sub(s->pos[0], l.pos[0]), dy = ee::sub(s->pos[1], l.pos[1]),
                        dz = ee::sub(s->pos[2], l.pos[2]);
            const float d2 = ps2::ffloat(ps2::madd(ps2::madd(ps2::mul(ps2::fbits(dx), ps2::fbits(dx)), ps2::fbits(1.0f),
                                                             ps2::fbits(ee::mul(dy, dy))),
                                                  ps2::fbits(1.0f), ps2::fbits(ee::mul(dz, dz))));
            APath path;
            path.listener = &l;
            bool audible = false;
            if ((s->views & (1u << (uint32_t(l.view) & 31))) != 0)
                audible = ee::eq(s->max_distance2, 0.0f) || ee::lt(d2, s->max_distance2);
            float vol = 0.0f;
            if (audible) {
                APath_Build(path, *s);
                vol = path.volume;
            }
            float& last = s->last_volume[l.view];
            if (ee::lt(0.0f, vol)) {
                last = vol;
                s->Play(path);
            } else if (ee::lt(0.0f, last)) {
                path = APath{};
                path.listener = &l;
                last = vol;
                s->Play(path);
            } else {
                last = vol;
            }
        }
        it = next;
    }
}

// [driving] 0x00147e30 EAudioUpdate::~EAudioUpdate
void audio_update(const AudioListenerInput& in) {
    Audio& a = A();
    if (!a.ready) return;
    // (WSoundGroup::Update dos sons do mundo: não portado)
    // AListener(MATRIX4 &, COORD3 &, AView) 0x002f4e00 com a matriz da câmera e a vista 2.
    AListener l;
    const float(&m)[4][4] = in.matrix;
    for (int c = 0; c < 3; ++c) l.pos[c] = m[3][c];
    l.pos[3] = 1.0f;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) l.rot[r][c] = m[c][r];
        l.rot[r][3] = 0.0f;
    }
    // trans = (-pos) * rot (vmulax/vmadday/vmaddaz/vmaddw com a linha 3 = (0, 0, 0, 1))
    for (int c = 0; c < 4; ++c) {
        uint32_t acc = ps2::mul(ps2::fbits(l.rot[0][c]), ps2::fbits(ee::neg(l.pos[0])));
        acc = ps2::madd(acc, ps2::fbits(l.rot[1][c]), ps2::fbits(ee::neg(l.pos[1])));
        acc = ps2::madd(acc, ps2::fbits(l.rot[2][c]), ps2::fbits(ee::neg(l.pos[2])));
        acc = ps2::madd(acc, ps2::fbits(c == 3 ? 1.0f : 0.0f), 0);
        l.trans[c] = ps2::ffloat(acc);
    }
    l.trans[3] = 1.0f;
    for (int c = 0; c < 3; ++c) l.vel[c] = in.vel[c];
    l.view = 2;
    for (int c = 0; c < 3; ++c) l.car[c] = in.car_pos[c];
    l.car[3] = 1.0f;
    // O ambiente do WWorld::GetEnviroDesc sob o carro: AFX::SetMode, o AFader e a faixa de altura.
    audio_streams_enviro(in.enviro, l);
    ASoundManager_BuildPaths(l);
    audio_streams_fader_update();  // AFader::Update do "SpeechVsAmbience"
    // A mixagem "0:Fader2" volta a 1 em 0,01 por quadro (depois de um ESetAudioMix).
    AMix* fade = AMix_Get("0:Fader2");
    const float v = fade->volume;
    fade->saved = v;
    if (ee::lt(v, 1.0f)) {
        fade->volume = ee::add(v, 0.01f);
        if (ee::lt(1.0f, fade->volume)) fade->volume = 1.0f;
    }
    AVoice_PlayVoices();
    SNDSYS_service();
    a.count = 0;
    ++a.stats.updates;
}

AudioStats audio_stats() {
    AudioStats s = A().stats;
    s.sounds = int32_t(A().sounds.size());
    s.afx_mode = A().afx_current;
    s.afx_level = A().afx_level;
    s.guards += snd_guards() + snddrv_guards();
    return s;
}

AudioStats& audio_stats_mut() { return A().stats; }

AudioInternals audio_internals() {
    AudioInternals r;
    r.random = A().rnd;
    r.active = &voice_active_list();
    r.mixes = &A().mixes;
    r.mix_list = &A().mix_list;
    r.categories = A().categories;
    audio_vehicle_internals(r);
    return r;
}

}  // namespace drv
