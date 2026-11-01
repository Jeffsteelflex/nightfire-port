// O ASound do BondAudioPS2.lib (SupportPS2.lib UDevCons.obj no SYM) e os sons 'gs' da galeria do Giotto
// (GFXGallery::SOUND_Play); driving/sound/audio.h, docs/driving.md seção 29. Portado do MIPS do DRIVING.ELF.
//
// Como o PS2 faz os sons da galeria: a primeira vez que o CANVAS_Draw passa por uma trilha 'gs' de uma tela (até oito
// por tela, no +0x70), ele lê a chave dela (GET_FirstIdleKey + 0x30: o volume em %, a categoria do banco, o nome do
// timbre, o pitch em %, o alcance) e, se o nome não é "(null)", cria um ASound("Object Sounds") (o construtor sem
// timbre: volume 0) e, com a bandeira 0x800 da tela (uma vez só), tira o laço das três vistas; o SOUND_Play então troca
// a mixagem do ABaseSound por "Giotto" (a da voz fica "Object Sounds"), põe o timbre na voz (AVoice::Set) e o volume, o
// pitch e o alcance da chave. Nos quadros seguintes o SOUND_Play só leva a posição da tela e a velocidade de quem a
// disparou ao som (a direção (0, 1, 0)). O CANVAS_HasStopped apaga os sons quando os sistemas de partículas da tela
// acabam: os de uma vez só seguram a tela até a voz acabar nas vistas em uso; o PurgeAllEffects apaga todos.
//
// Divergência (port): os ASound da galeria ficam num registro (o audio_shutdown apaga todos os sons; a galeria que ainda
// os aponte os vê como acabados).

#include <cstring>
#include <set>

#include "driving/sound/audio.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"
#include "platform/ptr32.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

GallerySoundStats g_stats;

std::set<ASound*>& live() {
    static std::set<ASound*> s;
    return s;
}

ASound* from(uint32_t h) {
    if (h == 0) return nullptr;
    ASound* s = nf::p32_ptr<ASound>(h);
    return live().count(s) != 0 ? s : nullptr;
}

}  // namespace

// [driving] 0x002ed3d0 ASound::ASound(int banco, int timbre, char *mistura)
ASound::ASound(const char* mix_name, int32_t bank, int32_t patch) : ABaseSound(mix_name, 4), voice(mix, bank, patch) {
    volume = 1.0f;
    pitch = 1.0f;
}

// [driving] 0x002ed260 ASound::ASound(char *mistura): a voz sem timbre na mixagem do som, volume 0, pitch 1.
ASound::ASound(const char* mix_name) : ABaseSound(mix_name, 4), voice(mix, -1, -1) {
    for (AVoiceView& v : voice.views) v = AVoiceView();  // (o construtor do AVoice::View inline: sem o Set)
    volume = 0.0f;
    pitch = 1.0f;
}

// [driving] 0x002ed5a0 ASound::~ASound (as vistas da voz, depois o ABaseSound)
ASound::~ASound() { live().erase(this); }

// [driving] 0x002ed518 ASound::Play(APath &)
void ASound::Play(APath& path) {
    const int32_t view = path.listener->view;
    const float mv = AMix_GetVolume(mix);
    AVoice_Play(ee::mul(ee::mul(volume, mv), path.volume), ee::mul(pitch, path.pitch), path.azimuth, path.delay, 0.0f,
                &voice, view);
}

GallerySoundStats& gallery_sound_stats() { return g_stats; }

// O ABaseSound::operator new(0x120, "ASound") e o 0x002ed260 do CANVAS_Draw (0x00234818), com a bandeira 0x800 da tela
// as três vistas sem laço.
uint32_t GFXGallery_NewSound(bool one_shot) {
    ASound* s = new ASound("Object Sounds");
    live().insert(s);
    if (one_shot)
        for (AVoiceView& v : s->voice.views) v.loop = 0;
    ++g_stats.created;
    return nf::p32_u(s);
}

void GFXGallery_SOUND_Fields(const uint8_t* desc, float out[5]) {
    int32_t w[4];
    std::memcpy(&w[0], desc + 0x00, 4);
    std::memcpy(&w[1], desc + 0x2c, 4);
    std::memcpy(&w[2], desc + 0x30, 4);
    std::memcpy(&w[3], desc + 0x34, 4);
    const float pct = ps2::ffloat(0x3c23d70au);  // 0,01
    out[0] = ee::mul(ee::cvt_s(w[0]), pct);      // +0x6c o volume
    out[1] = ee::mul(ee::cvt_s(w[1]), pct);      // +0x70 o pitch
    out[2] = ee::cvt_s(w[2]);                    // +0x5c o alcance mínimo
    out[3] = ee::cvt_s(w[3]);                    // +0x60 o máximo
    out[4] = ee::mul(out[3], out[3]);            // +0x64 o quadrado
}

// [driving] 0x002363b0 GFXGallery::SOUND_Play(GFXGallery::Track *, GFXGallery::SoundDesc *, ASound *, COORD3 &, COORD3 &)
void GFXGallery_SOUND_Play(uint32_t handle, const uint8_t* desc, const float pos[3], const float vel[3]) {
    ASound* s = from(handle);
    if (s == nullptr) {
        ++g_stats.guards;  // port: o som já foi apagado (o audio_shutdown)
        return;
    }
    std::memcpy(s->pos, pos, 12);
    s->dir[0] = 0.0f;  // 0x0039c9f0
    s->dir[1] = 1.0f;
    s->dir[2] = 0.0f;
    std::memcpy(s->vel, vel, 12);
    if (desc == nullptr) return;
    AMix_Remove(s->mix);
    s->mix = AMix_Add("Giotto");
    int32_t category;
    std::memcpy(&category, desc + 8, 4);
    ABank* bank = ABank_Category(category);  // ABank::fgBanks[categoria]
    char name[0x20] = {};
    std::memcpy(name, desc + 0xc, sizeof name - 1);
    const int32_t patch = bank->index.Lookup(name);
    s->voice.Set(bank->snd_bank, patch);
    float f[5];
    GFXGallery_SOUND_Fields(desc, f);
    s->volume = f[0];
    s->pitch = f[1];
    s->min_distance = f[2];
    s->max_distance = f[3];
    s->max_distance2 = f[4];
    ++g_stats.started;
    if (patch < 0) ++g_stats.silent;
}

// A parte do CANVAS_HasStopped (0x00235270): a voz acabou nas vistas em uso (+0x110, +0xd0, +0xf0).
bool GFXGallery_SoundDone(uint32_t handle) {
    const ASound* s = from(handle);
    if (s == nullptr) return true;
    const int32_t over[3] = {s->voice.views[0].over, s->voice.views[1].over, s->voice.views[2].over};
    return ACharacter_VoiceDone(over, s->views);
}

// A entrada +0xc da tabela virtual com 3 (o ~ASound e o operator delete).
void GFXGallery_DeleteSound(uint32_t handle) {
    if (ASound* s = from(handle)) {
        s->destroy();
        ++g_stats.deleted;
    }
}

}  // namespace drv
