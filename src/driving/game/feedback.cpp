// A vibração do controle nas missões de carro (driving/game/feedback.h), portada do MIPS do DRIVING.ELF (as
// desmontagens em out/agent_drv_car/ do checkout principal). As contas em float seguem a FPU do EE.
//
// Divergências (port):
//  - o IFeedback::Update roda numa linha de execução do PS2 acordada pelo temporizador (IFeedback::Timer, um
//    TIMER_addtask); aqui quem chama decide (o nf_driving: uma vez por passo da simulação, 60 por segundo);
//  - a divisão por zero do tranco (break 7: um meio de duração zero com um tranco em curso, que o ApplyJolt nunca
//    deixa) conta como guarda e trata o quociente como zero.

#include "driving/game/feedback.h"

#include "driving/physics/collision.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

FeedbackState g_state;

// cvt.w.s(x + (x < 0 ? -0,5 : 0,5)): o arredondamento do código (add.S e o truncamento do cvt.w.s).
int32_t round_ee(float x) {
    const float h = ee::lt(x, 0.0f) ? -0.5f : 0.5f;
    return ee::cvt_w(ee::add(h, x));
}

void actuate(int32_t port, int32_t motor, uint8_t value) {
    if (g_state.actuator) g_state.actuator(port, motor, value);
}

IFeedbackPortData* port_of(const IFeedback& f) {
    return f.port >= 0 && f.port < 4 ? &g_state.ports[f.port] : nullptr;
}

}  // namespace

FeedbackState& feedback_state() { return g_state; }

void feedback_reset() {
    auto actuator = g_state.actuator;
    g_state = FeedbackState();
    g_state.actuator = actuator;
}

uint8_t feedback_dither(uint32_t index) {
    const uint8_t b = uint8_t(index);
    if (b == 0xff) return 0xfe;  // a última entrada da tabela é 254, não 255 (o nível 255 liga sempre)
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (b & (1u << i)) r = uint8_t(r | (0x80u >> i));
    return r;
}

// [driving] 0x00165b90 IFeedbackPort::Reset
void IFeedbackPort_Reset(IFeedbackPortData& p, int32_t port) {
    p.port = port;
    p.small_level = 0;
    p.big_level = 0;
    p.jolt_level = 0;
    p.jolt_count = 0;
    p.jolt_half = 0;
    p.rumble = 0;
    p.vibrate = 0;
    p.rumble_time = 0;
    p.vibrate_time = 0;
    p.small_base = 0.0f;
    p.big_base = 0.0f;
}

// [driving] 0x00165ed0 IFeedback::IFeedback(int)
void IFeedback_Construct(IFeedback& f, int32_t port) {
    f.port = port;
    IFeedbackPortData* p = port_of(f);
    if (!p || g_state.uber_vibration == 0 || g_state.paused != 0) return;
    if (p->refs == 0) {
        IFeedbackPort_Reset(*p, port);
        // O primeiro liga a linha de execução e o temporizador (FUN_00261b70, FUN_002611f0, TIMER_addtask).
        g_state.threads += 1;
    }
    p->refs += 1;
}

// [driving] 0x00166260 IFeedback::~IFeedback
void IFeedback_Destruct(IFeedback& f) {
    IFeedbackPortData* p = port_of(f);
    if (!p || p->refs <= 0) return;
    p->refs -= 1;
    if (p->refs == 0) {
        IFeedbackPort_Reset(*p, p->port);
        g_state.threads -= 1;  // o último para o temporizador e espera a linha de execução sair
    }
}

// [driving] 0x00165bc8 IFeedback::Update
void IFeedback_Update() {
    for (int32_t i = 0; i < 4; ++i) {
        IFeedbackPortData& p = g_state.ports[i];
        if (g_state.enabled == 0 || g_state.uber_vibration == 0 || g_state.paused != 0) {
            actuate(i, 1, 0);
            actuate(i, 0, 0);
            p.jolt_count = 0;
            p.vibrate_time = 0;
            p.rumble_time = 0;
            continue;
        }
        if (p.refs <= 0) continue;
        p.small_level = round_ee(ee::mul(p.small_base, 100.0f));
        p.big_level = round_ee(ee::mul(p.big_base, 125.0f));
        if (p.vibrate_time > 0) p.vibrate_time -= 1;
        else p.vibrate = 0;
        int32_t small = p.small_level + p.vibrate;
        if (small >= 0x100) small = 0xff;
        p.small_level = small;
        bool on;
        int32_t big;
        if (p.jolt_half < p.jolt_count) {
            on = int32_t(feedback_dither(g_state.counter & 0xff)) < small + p.jolt_level;
            big = p.big_level + p.jolt_level;
            p.jolt_count -= 1;
            if (big >= 0x100) big = 0xff;
        } else if (p.jolt_count == 0) {
            p.jolt_level = 0;
            on = int32_t(feedback_dither(g_state.counter & 0xff)) < small;
            big = p.big_level;
        } else {
            int32_t q = 0;
            if (p.jolt_half == 0) ++g_state.guards;
            else q = (p.jolt_level * p.jolt_count) / p.jolt_half;
            p.jolt_count -= 1;
            on = int32_t(feedback_dither((g_state.counter >> 1) & 0xff)) < small + q;
            big = p.big_level;
        }
        if (p.rumble_time > 0) p.rumble_time -= 1;
        else p.rumble = 0;
        big += p.rumble;
        if (big >= 0x100) big = 0xff;
        actuate(i, 1, uint8_t(big & 0xff));
        actuate(i, 0, on ? 1 : 0);
    }
    g_state.counter += 1;
}

// [driving] 0x00165fb0 IFeedback::ApplyJolt
void IFeedback_ApplyJolt(const IFeedback& f, float strength) {
    int32_t level, count;
    if (ee::lt(0.25f, strength)) {
        level = 0xff;
        count = round_ee(ee::mul(strength, 32.0f));
        if (count >= 0x19) count = 0x18;
    } else {
        level = round_ee(ee::mul(strength, 1020.0f));
        count = 8;
    }
    IFeedbackPortData* p = port_of(f);
    if (!p) return;
    if (p->jolt_level < level || p->jolt_count < count) {
        p->jolt_half = count >> 1;
        p->jolt_count = count;
        p->jolt_level = level;
    }
}

// [driving] 0x001660a0 IFeedback::AddCollisionInfo
void IFeedback_AddCollisionInfo(const IFeedback& f, const CollisionInfo& info) {
    float s = ee::abs(info.strength);
    if (int16_t(info.flags) == 0xe || int16_t(info.tag) == 0xe) s = ee::mul(s, ps2::ffloat(0x3e4ccccdu));  // 0,2
    IFeedback_ApplyJolt(f, s);
}

// [driving] 0x001660f0 IFeedback::SetRumble
void IFeedback_SetRumble(const IFeedback& f, float amount, float seconds) {
    IFeedbackPortData* p = port_of(f);
    const int32_t a = round_ee(ee::mul(amount, 255.0f));
    const int32_t t = round_ee(ee::mul(seconds, 60.0f));
    if (!p) return;
    p->rumble = a;
    p->rumble_time = t;
}

// [driving] 0x00166180 IFeedback::SetVibrate
void IFeedback_SetVibrate(const IFeedback& f, float amount, float seconds) {
    IFeedbackPortData* p = port_of(f);
    const int32_t a = round_ee(ee::mul(amount, 255.0f));
    const int32_t t = round_ee(ee::mul(seconds, 60.0f));
    if (!p) return;
    p->vibrate = a;
    p->vibrate_time = t;
}

// [driving] 0x00149ed8 ECollision::~ECollision (a vibração; a parte do som em sound/audio_vehicle.cpp, ECollision_Audio)
void ECollision_Feedback(const CollisionInfo& info, const std::function<const IFeedback*(int32_t slot)>& feedback_of) {
    if (info.kind < 6)
        if (const IFeedback* f = feedback_of(info.slot)) IFeedback_AddCollisionInfo(*f, info);
    if (info.type < 6)
        if (const IFeedback* f = feedback_of(int8_t(info.unknown_4d))) IFeedback_AddCollisionInfo(*f, info);
}

// [driving] 0x00166210 IFeedback::Pause
void IFeedback_Pause() {
    for (int32_t i = 0; i < 4; ++i) IFeedbackPort_Reset(g_state.ports[i], i);
}

}  // namespace drv
