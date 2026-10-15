// O EHitWindow (docs/driving.md seção 26): o evento das janelas atingidas por uma bala (WCollisionMgr::CheckHitWindow o
// cria; ver driving/physics/world_window.cpp). O destrutor (0x0014f480) toca o vidro e põe os efeitos na galeria.
// Portado do MIPS do PS2. O AOneShotSound é o do port (driving/sound/audio_internal.h): o construtor embutido no PS2
// (ABaseSound::operator_new 0x120, AMix::Add, AIndex::Lookup, AVoice::AVoice) e os campos escritos depois (+0 o lugar,
// +0x5c a distância mínima, +0x60 a máxima, +0x64 o quadrado, +0x114).
// Divergências: o PS2 põe o evento no anel do EventManager e o destrutor roda na ordem dos outros; o port o guarda à
// parte (o evento não tem número na tabela) e roda a fila depois dos eventos numerados do mesmo passe (só sons e efeitos,
// nada do jogo muda). Um banco nulo (o PS2 leria o endereço 8) é contado e o som não é criado.

#include <cstring>

#include "driving/game/shells.h"
#include "driving/real/vu0_math.h"
#include "driving/sound/audio_internal.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

HitWindowCounters g_counters;
HitWindowEnv g_env;
HitWindowStats g_stats;

// O 0x0032bbb0: os dois nomes do vidro.
const char* const kShatter[2] = {"SFX_GlassShatter1", "SFX_GlassShatter2"};
// O 0x00355bb0 (a identidade), o 0x00355b80 (zeros) e o 0x00355ba0 (0, 0, 0, 1).
alignas(16) const float kIdentity[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
const float kZero3[3] = {0.0f, 0.0f, 0.0f};
const float kQuat2[4] = {0.0f, 0.0f, 0.0f, 1.0f};

// O AOneShotSound(mix, banco da categoria, o som do índice, 1) no lugar, com as distâncias.
void one_shot(int32_t category, const char* name, const float pos[3], float min_distance, float max_distance,
              float max_distance2, float fx) {
    const char* mix = "Explosions";  // 0x0036f650
    if (g_env.sound) {
        g_env.sound(category, name, mix, pos, min_distance, max_distance, max_distance2, fx);
        return;
    }
    ABank* bank = ABank_Category(category);
    if (!bank) {
        ++g_stats.guards;
        return;
    }
    AOneShotSound* s = new AOneShotSound(mix, bank->snd_bank, bank->index.Lookup(name), 1.0f);
    std::memcpy(s->pos, pos, 12);
    s->max_distance = max_distance;
    s->min_distance = min_distance;
    s->max_distance2 = max_distance2;
    s->fx = fx;
}

}  // namespace

HitWindowCounters& hit_window_counters() { return g_counters; }
HitWindowEnv& hit_window_env() { return g_env; }
HitWindowStats& hit_window_stats() { return g_stats; }

// [driving] 0x0014f480 EHitWindow::~EHitWindow
void EHitWindow_Destroy(const HitWindowEvent& ev) {
    ++g_stats.events;
    float point[4];
    std::memcpy(point, &ev.image[1], 16);
    const uint32_t hits = ev.image[10];  // +0x28: o WindowDynData::hits da cópia
    if (hits == 0) {
        // ABank::fgBanks[2] (0x003562a8, Smackables): o vidro, alternado.
        const char* name = kShatter[g_counters.shatter & 1];
        ++g_counters.shatter;
        one_shot(2, name, point, 5.0f, 200.0f, 40000.0f, 1.0f);
        ++g_stats.shatter;
        if (ee::lt(point[1], 10.0f)) {
            if ((g_counters.alarm & 7) == 0) {
                // fgBanks[0] (Generic): o alarme da loja; o +0x114 fica 0.
                one_shot(0, "SFX_StoreAlarm", point, 2.0f, 70.0f, 4900.0f, 0.0f);
                ++g_stats.alarm;
            }
            ++g_counters.alarm;
        }
    } else if (hits < 5) {
        // fgBanks[1] (Collisions).
        one_shot(1, "SFX_Headlight", point, 5.0f, 200.0f, 40000.0f, 1.0f);
        ++g_stats.headlight;
    }
    if (hits != 0) return;
    alignas(16) float quat[4];
    VU0_m4toquat(kIdentity, quat);
    EffectsEnv& fe = effects_env();
    const uint32_t ref = fe.gfx_lookup ? fe.gfx_lookup("Wd_Glass") : 0u;
    if (fe.gfx_trigger_incidental) fe.gfx_trigger_incidental(ref, point, quat, kZero3, kQuat2, 0, 0, 0);
    ++g_stats.glass;
    if (ev.image[20] == 0) return;
    // O meio dos dois cantos da cópia (+0x30 e +0x40).
    float a[3], b[3];
    std::memcpy(a, &ev.image[12], 12);
    std::memcpy(b, &ev.image[16], 12);
    const float mid[3] = {ee::mul(ee::add(a[0], b[0]), 0.5f), ee::mul(ee::add(a[1], b[1]), 0.5f),
                          ee::mul(ee::add(a[2], b[2]), 0.5f)};
    const uint32_t ref2 = fe.gfx_lookup ? fe.gfx_lookup("Wd_Wsmk") : 0u;
    if (fe.gfx_trigger_incidental) fe.gfx_trigger_incidental(ref2, mid, quat, kZero3, kQuat2, 0, 0, 0);
    ++g_stats.smoke;
}

void hit_windows_run() {
    std::vector<HitWindowEvent>& q = hit_window_events();
    // (os criados enquanto estes rodam entrariam no fim do anel; o destrutor não cria eventos)
    for (size_t i = 0; i < q.size(); ++i) {
        const HitWindowEvent ev = q[i];
        EHitWindow_Destroy(ev);
    }
    q.clear();
}

}  // namespace drv
