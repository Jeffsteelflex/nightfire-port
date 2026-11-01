// O som dos personagens (o ACharacter do BondAudioPS2.lib, AMix.obj; driving/sound/audio.h), portado do MIPS do
// DRIVING.ELF; docs/driving.md seção 29.
//
// Como o PS2 faz: o AICharacter guarda no +0x94 um ACharacter (0xc0 bytes: um ABaseSound na mixagem "Characters" com a
// tabela virtual 0x3c5780, alcance 3..25 m, e a voz no +0xb0), criado na primeira fala (AICharacter::GetACharacter, o
// construtor inline). O ACharacter::Say escolhe o timbre pela primeira letra e a segunda (os capangas, "H": F as
// quedas, Y os berros, D as mortes, S os gritos, os outros os gemidos; "M" e o resto os pedestres) numa roda de nomes
// do .data que avança a cada fala, põe o alcance (50..300 m os capangas, 10..75 m os pedestres) e cria a voz de uma vez
// só (as três vistas sem laço) no banco da categoria 7 (Actors); com uma voz ainda tocando ele não faz nada. O
// ASoundManager toca o ACharacter a cada atualização (ACharacter::Play: o volume dele x o da mixagem x o do caminho) e
// apaga a voz quando ela acabou nas vistas em uso; o AICharacter::Execute põe o som na posição do personagem e, sem
// voz, apaga o ACharacter (a entrada +0xc da tabela com 3) e zera o +0x94.
//
// A ordem no PS2 (conferida no MIPS na seção 31; docs/driving.md): o ACharacter nasce na origem no Say (o +0x00..+0x08
// zerado pelo construtor inline do GetACharacter); o fim do AICharacter::Execute (0x00118e4c, no Human::Simulate do
// SimulateGame) copia a linha 3 da matriz do personagem para ele enquanto há voz; o ASoundManager::BuildPaths do
// EAudioUpdate só chama o Play de um som dentro do alcance (|posição - ouvinte|² < +0x64) ou com o volume do quadro
// anterior acima de 0. Uma fala que chega depois do Execute do passo (um tiro do EFireWeapon, no fim da passagem) fica
// na origem, longe do ouvinte (os capangas: 300 m), e a voz espera: o Execute seguinte a põe no personagem e o
// EAudioUpdate a toca. Os passageiros de Paris se ouvem (a partida ganha: um gemido do SFX_HGrunt1 aos 62 s, 36 quadros
// com 0,19 a 0,23 de volume do caminho). A seção 29 dizia o contrário (as vozes acabavam mudas): a leitura estava
// errada; só um ouvinte a menos de 300 m da origem tocaria a voz lá.
// Quirks do original que ficam: as rodas do .data têm entradas nulas (as mortes 3..10 dos capangas, o quarto grito) e
// nomes sem o terminador ("SFX_HDeath3" emendado no "SFX_HDeath4", "SFX_HScream3" no "SFX_HScream4"): o AIndex::Lookup
// não acha o timbre e a voz fica muda (8 de 11 mortes e 2 de 4 gritos calados).
// Divergências (port, contadas em acharacter_stats().guards):
//  - o nome nulo vai ao AIndex::Lookup como -1 (o PS2 leria o endereço 0 como texto: nenhum timbre tem esse nome);
//  - a roda das mulheres pedestres para depois de uma volta sem timbre presente (o PS2 ficaria no laço para sempre: o
//    sinal 0x00356200 que o pararia nunca é ligado); nenhum pedestre está portado;
//  - os ACharacter são guardados num registro: o audio_shutdown apaga todos os sons, e um AICharacter que ainda o
//    aponte o vê como acabado.

#include <cstring>
#include <set>

#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "driving/sound/snd.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

ACharacterStats g_stats;

// As rodas do .data (0x00356128..0x00356200), com os contadores de cada uma.
const char* const kHenchFall[3] = {"SFX_HenchFall1", "SFX_HenchFall2", "SFX_HenchFall3"};  // 0x00356128
const char* const kHYell[3] = {"SFX_HYell1", "SFX_HYell2", "SFX_HYell3"};                  // 0x00356138
// 0x00356148: o terceiro nome não tem o terminador no .rodata (0x003c55a8); as entradas 3..10 são nulas.
const char* const kHDeath[11] = {"SFX_HDeath1", "SFX_HDeath2", "SFX_HDeath3SFX_HDeath4", nullptr, nullptr, nullptr,
                                 nullptr,       nullptr,       nullptr,                  nullptr, nullptr};
// 0x00356178: idem o terceiro (0x003c55e0), a quarta entrada nula.
const char* const kHScream[4] = {"SFX_HScream", "SFX_HScream2", "SFX_HScream3SFX_HScream4", nullptr};
const char* const kHGrunt[8] = {"SFX_HGrunt1", "SFX_HGrunt2", "SFX_HGrunt3", "SFX_HGrunt4",
                                "SFX_HGrunt5", "SFX_HGrunt6", "SFX_HGrunt7", "SFX_HGrunt8"};  // 0x00356190
const char* const kMPed[10] = {"SFX_MPed1a", "SFX_MPed1b", "SFX_MPed1c", "SFX_MPed2a", "SFX_MPed2b",
                               "SFX_MPed2c", "SFX_MPed3a", "SFX_MPed3b", "SFX_MPed3c", "SFX_MPed3d"};  // 0x003561b8
const char* const kFPed[5] = {"SFX_FPed1a", "SFX_FPed1b", "SFX_FPed1c", "SFX_FPed1d", "SFX_FPed1e"};  // 0x003561e8

ACharacterSayState g_say;

// ACharacter (0xc0): o ABaseSound e a voz.
class ACharacter : public ABaseSound {
public:
    AVoice* voice = nullptr;  // +0xb0
    ACharacter();
    ~ACharacter() override;
    void Play(APath& path) override;
};

std::set<ACharacter*>& live() {
    static std::set<ACharacter*> s;
    return s;
}

ACharacter* from(uint32_t h) {
    if (h == 0) return nullptr;
    ACharacter* c = nf::p32_ptr<ACharacter>(h);
    return live().count(c) != 0 ? c : nullptr;
}

// O construtor inline do AICharacter::GetACharacter (o mesmo do 0x002fc468 ACharacter::ACharacter(char *)).
ACharacter::ACharacter() : ABaseSound("Characters", 4) {
    min_distance = 3.0f;
    max_distance = 25.0f;
    max_distance2 = 625.0f;
    live().insert(this);
    ++g_stats.created;
}

// [driving] 0x002fc268 ACharacter::~ACharacter
ACharacter::~ACharacter() {
    delete voice;
    voice = nullptr;
    live().erase(this);
    ++g_stats.deleted;
}

// [driving] 0x002fc130 ACharacter::Play(APath &)
void ACharacter::Play(APath& path) {
    if (voice == nullptr) return;
    const int32_t view = path.listener->view;
    AVoice_Play(ACharacter_PlayVolume(volume, AMix_GetVolume(mix), path.volume), path.pitch, path.azimuth, 0.0f, 0.0f,
                voice, view);
    const int32_t over[3] = {voice->views[0].over, voice->views[1].over, voice->views[2].over};
    if (ACharacter_VoiceDone(over, views)) {
        delete voice;  // AVoice::View::~View das três vistas e UMemory::FastFree(voz, 100)
        voice = nullptr;
        ++g_stats.voices_done;
    }
}

}  // namespace

ACharacterStats& acharacter_stats() { return g_stats; }
ACharacterSayState& acharacter_say_state() { return g_say; }

// A parte do ACharacter::Say (0x002fbe00) que escolhe o nome e o alcance.
const char* ACharacter_PickName(const char* what, const std::function<int32_t(const char*)>& present, float range[3]) {
    const char* name;
    if (what[0] == 'H') {
        const char c = what[1];
        if (c == 'F') {
            name = kHenchFall[g_say.fall++];
            if (2 < g_say.fall) g_say.fall = 0;
        } else if (c == 'Y') {
            name = kHYell[g_say.yell++];
            if (2 < g_say.yell) g_say.yell = 0;
        } else if (c == 'D') {
            name = kHDeath[g_say.death++];
            if (10 < g_say.death) g_say.death = 0;
        } else if (c == 'S') {
            name = kHScream[g_say.scream++];
            if (3 < g_say.scream) g_say.scream = 0;
        } else {
            name = kHGrunt[g_say.grunt++];
            if (7 < g_say.grunt) g_say.grunt = 0;
        }
        range[0] = 50.0f;
        range[1] = 300.0f;
        range[2] = 90000.0f;
        return name;
    }
    if (what[0] == 'M') {
        do {
            name = kMPed[g_say.mped++];
            if (9 < g_say.mped) {
                g_say.mped_wrapped = 1;
                g_say.mped = 0;
            }
        } while (present(name) == 0 && g_say.mped_wrapped == 0);
    } else {
        int tries = 0;
        do {
            name = kFPed[g_say.fped++];
            if (4 < g_say.fped) g_say.fped = 0;
            if (++tries > 5 && g_say.fped_wrapped == 0) {
                ++g_stats.guards;  // port: o PS2 ficaria no laço (o sinal 0x00356200 nunca é ligado)
                break;
            }
        } while (present(name) == 0 && g_say.fped_wrapped == 0);
    }
    range[0] = 10.0f;
    range[1] = 75.0f;
    range[2] = 5625.0f;
    return name;
}

float ACharacter_PlayVolume(float volume, float mix_volume, float path_volume) {
    return ee::mul(ee::mul(volume, mix_volume), path_volume);
}

// O fim da voz no ACharacter::Play: a vista 2 acabou e, sem a vista 2 na máscara global das vistas (0x00355cd0), as
// vistas 0 e 1 que estão nela também.
bool ACharacter_VoiceDone(const int32_t over[3], uint32_t mask) {
    if (over[2] == 0) return false;
    if ((mask & 4) != 0) return true;
    for (uint32_t k = 0; k < 2; ++k)
        if ((mask & (1u << k)) != 0 && over[k] == 0) return false;
    return true;
}

// O construtor inline do AICharacter::GetACharacter (0x00118988): ABaseSound::operator new(0xc0, "ACharacter").
uint32_t ACharacter_New() { return nf::p32_u(new ACharacter()); }

// [driving] 0x002fbe00 ACharacter::Say(char *)
void ACharacter_Say(uint32_t handle, const char* what) {
    ACharacter* c = from(handle);
    if (c == nullptr) {
        ++g_stats.guards;  // port: o som já foi apagado (o audio_shutdown)
        return;
    }
    if (c->voice != nullptr) return;
    ABank* bank = ABank_Category(7);  // ABank::fgBanks[7] (0x003562bc): Actors
    auto lookup = [&](const char* name) -> int32_t {
        if (name == nullptr) {
            ++g_stats.guards;  // port: o AIndex::Lookup do PS2 leria o endereço 0
            return -1;
        }
        return bank->index.Lookup(name);
    };
    // [driving] 0x002fd698 ABank::IsPatchPresent(int) const
    auto present = [&](const char* name) -> int32_t {
        const int32_t patch = lookup(name);
        if (bank->snd_bank < 0 || uint32_t(patch) >= 0x80) return 0;
        return SNDbankpatpresent(bank->snd_bank, patch) == 1 ? 1 : 0;
    };
    float range[3];
    const char* name = ACharacter_PickName(what, present, range);
    c->min_distance = range[0];
    c->max_distance = range[1];
    c->max_distance2 = range[2];
    const int32_t patch = lookup(name);
    c->voice = new AVoice(c->mix, bank->snd_bank, patch);  // UMemory::FastAlloc(100, "AVoice"), AVoice::AVoice
    for (AVoiceView& v : c->voice->views) v.loop = 0;
    ++g_stats.says;
    if (patch < 0) ++g_stats.silent;
}

bool ACharacter_Done(uint32_t handle) {
    const ACharacter* c = from(handle);
    return c == nullptr || c->voice == nullptr;
}

void ACharacter_SetPos(uint32_t handle, const float pos[3]) {
    if (ACharacter* c = from(handle)) std::memcpy(c->pos, pos, 12);
}

void ACharacter_Delete(uint32_t handle) {
    if (ACharacter* c = from(handle)) c->destroy();
}

}  // namespace drv
