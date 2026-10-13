// A entrada do jogo nas missões de carro (input.h).

#include "driving/game/input.h"

#include <cstdlib>
#include <cstring>

#include "platform/crt_compat.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

uint64_t g_guards = 0;

// Os nomes dos escalares (PS2PadDevice::Initialize: as cadeias em 0x3746a0..0x374870).
const char* const kScalarNames[kPadScalars] = {
    "ALX",          "ALY",           "ARX",           "ARY",           "AButtonRight",    "AButtonLeft",
    "AButtonUp",    "AButtonDown",   "AButtonTriangle", "AButtonCircle", "AButtonCross",  "AButtonSquare",
    "AButtonL1",    "AButtonR1",     "AButtonL2",     "AButtonR2",     "DButtonL2",       "DButtonR2",
    "DButtonL1",    "DButtonR1",     "DButtonTriangle", "DButtonCircle", "DButtonCross",  "DButtonSquare",
    "DButtonSelect", "DButtonLStick", "DButtonRStick", "DButtonStart",  "DButtonUp",       "DButtonRight",
    "DButtonDown",  "DButtonLeft",
};

// Os nomes das ações (a tabela 0x32c090 do StringToNumber de 0x32c440), na ordem dos números.
const char* const kActionNames[] = {
    "NULL_ACTION", "GAMEACTION_STEER", "GAMEACTION_STEERVERTICAL", "GAMEACTION_STEERVERTICALINV",
    "GAMEACTION_STEERLEFT", "GAMEACTION_STEERRIGHT", "GAMEACTION_STEERUP", "GAMEACTION_STEERDOWN",
    "GAMEACTION_STEERLEFTDIGITAL", "GAMEACTION_STEERRIGHTDIGITAL", "GAMEACTION_STEERUPDIGITAL",
    "GAMEACTION_STEERDOWNDIGITAL", "GAMEACTION_SOUND_STEERLEFT", "GAMEACTION_SOUND_STEERRIGHT",
    "GAMEACTION_SOUND_STEERUP", "GAMEACTION_SOUND_STEERDOWN", "GAMEACTION_SOUND_STEERHORIZONTALRELEASE",
    "GAMEACTION_SOUND_STEERVERTICALRELEASE", "GAMEACTION_SOUND_STEERPADLEFT", "GAMEACTION_SOUND_STEERPADRIGHT",
    "GAMEACTION_SOUND_STEERPADUP", "GAMEACTION_SOUND_STEERPADDOWN", "GAMEACTION_GAS", "GAMEACTION_GASBRAKE",
    "GAMEACTION_WHEELSPIN", "GAMEACTION_WHEELSPINRELEASE", "GAMEACTION_BRAKE", "GAMEACTION_HANDBRAKE",
    "GAMEACTION_HANDBRAKERELEASE", "GAMEACTION_FIREPRIMARY", "GAMEACTION_FIREPRIMARYRELEASE",
    "GAMEACTION_FIRESECONDARY", "GAMEACTION_FIRESECONDARYRELEASE", "GAMEACTION_TOGGLESECONDARY",
    "GAMEACTION_TOGGLESECONDARYUP", "GAMEACTION_TOGGLESECONDARYDOWN", "GAMEACTION_FIREGADGET",
    "GAMEACTION_FIREGADGETRELEASE", "GAMEACTION_CHANGECAMERA", "GAMEACTION_CHANGECAMERAUP",
    "GAMEACTION_CHANGECAMERADOWN", "GAMEACTION_CAMLOOKBACK", "GAMEACTION_CAMLOOKBACKRELEASE",
    "GAMEACTION_CAMZOOMBUTTON", "GAMEACTION_CAMROTATEX", "GAMEACTION_CAMROTATEY", "GAMEACTION_CAMROTATEYINV",
    "GAMEACTION_CAMROTATEX2", "GAMEACTION_CAMROTATEY2", "GAMEACTION_CAMROTATEY2INV", "GAMEACTION_CAMSPIN",
    "GAMEACTION_CAMCENTRE", "GAMEACTION_CAMZOOMINOUT", "GAMEACTION_CAMZOOMIN", "GAMEACTION_CAMZOOMOUT",
    "GAMEACTION_AIMZOOMLEFT", "GAMEACTION_AIMZOOMRIGHT", "GAMEACTION_AIMZOOMNONE", "GAMEACTION_AIMRELEASE",
    "GAMEACTION_AUTODRIVELOCK", "GAMEACTION_AUTODRIVELOCKRELEASE", "GAMEACTION_LEANRIGHT", "GAMEACTION_LEANLEFT",
    "GAMEACTION_POVRELOAD", "GAMEACTION_RESUMEAUTODRIVE", "FRONTENDACTION_UP", "FRONTENDACTION_DOWN",
    "FRONTENDACTION_LEFT", "FRONTENDACTION_RIGHT", "FRONTENDACTION_ACCEPT", "FRONTENDACTION_CANCEL",
    "FRONTENDACTION_START", "FRONTENDACTION_SCREENSHOT", "FRONTENDACTION_INVERT", "FRONTENDACTION_SKIPMOVIE",
    "DEBUGACTION_AXISX", "DEBUGACTION_AXISY", "DEBUGACTION_AXISRX", "DEBUGACTION_AXISRY", "DEBUGACTION_SCREENSHOT",
    "DEBUGACTION_STOPRECORDPATH", "CHEATACTION_DPAD_LEFT", "CHEATACTION_DPAD_LEFT_RELEASE", "CHEATACTION_DPAD_UP",
    "CHEATACTION_DPAD_UP_RELEASE", "CHEATACTION_DPAD_RIGHT", "CHEATACTION_DPAD_RIGHT_RELEASE",
    "CHEATACTION_DPAD_DOWN", "CHEATACTION_DPAD_DOWN_RELEASE", "CHEATACTION_BUT_LEFT", "CHEATACTION_BUT_LEFT_RELEASE",
    "CHEATACTION_BUT_UP", "CHEATACTION_BUT_UP_RELEASE", "CHEATACTION_BUT_RIGHT", "CHEATACTION_BUT_RIGHT_RELEASE",
    "CHEATACTION_BUT_DOWN", "CHEATACTION_BUT_DOWN_RELEASE", "CHEATACTION_TRIGGER_LEFT",
    "CHEATACTION_TRIGGER_LEFT_RELEASE", "CHEATACTION_TRIGGER_RIGHT", "CHEATACTION_TRIGGER_RIGHT_RELEASE",
    "ACTION_EXITAPPLICATION", "ACTION_MINIMIZEAPPLICATION", "ACTION_RESTOREAPPLICATION", "ACTION_MOUSEMOVE_X",
    "ACTION_MOUSEMOVE_Y", "ACTION_MOUSEMOVE", "ACTION_MOUSELBUTTONDOWN", "ACTION_MOUSELBUTTONUP",
    "ACTION_MOUSERBUTTONDOWN", "ACTION_MOUSERBUTTONUP", "ACTION_KEYDOWN", "ACTION_KEYUP", "ACTION_CHAR",
    "ACTION_CENTER", "ACTION_INPUTSYNC", "ACTION_PLUGUNPLUG",
};
constexpr int kActions = int(sizeof(kActionNames) / sizeof(kActionNames[0]));
static_assert(kActions == 117, "os 117 nomes de ação do jogo");

// Os modos (a tabela 0x32e5c0 do StringToNumber de 0x32e618; a busca linear com strcmp).
const char* const kMethods[] = {"U", "+", "-", ">", "<", "=", "I", "N", "R", "M"};

// StringToNumber::ConvertStringToNumber (0x2cec98): a busca exata (strcmp; a binária sobre a cópia ordenada nas
// tabelas de 0x1f ou mais nomes dá o mesmo, os nomes são únicos); -1 se não acha.
int32_t lookup(const char* const* names, int n, const char* s) {
    if (!s) return -1;
    for (int i = 0; i < n; ++i)
        if (std::strcmp(names[i], s) == 0) return i;
    return -1;
}

// strtok com "\t " (o estado é o ponteiro guardado).
char* next_token(char*& state, char* start) {
    char* s = start ? start : state;
    if (!s) return nullptr;
    while (*s == '\t' || *s == ' ') ++s;
    if (*s == 0) {
        state = nullptr;
        return nullptr;
    }
    char* e = s;
    while (*e && *e != '\t' && *e != ' ') ++e;
    if (*e) {
        *e = 0;
        state = e + 1;
    } else {
        state = nullptr;
    }
    return s;
}

}  // namespace

uint64_t input_guards() { return g_guards; }

const char* pad_scalar_name(int i) { return i >= 0 && i < kPadScalars ? kScalarNames[i] : ""; }

int pad_scalar_type(int i) { return i < 4 ? 0 : i < 16 ? 1 : 2; }

// [driving] 0x001651a0 getActionID
int32_t getActionID(const char* name) {
    const int32_t id = lookup(kActionNames, kActions, name);
    return id == -1 ? 0x75 : id;
}

const char* action_name(int32_t id) { return id >= 0 && id < kActions ? kActionNames[id] : "?"; }

// [driving] 0x00169218 PS2ScaleJoystick
float PS2ScaleJoystick(uint8_t v) {
    return ee::sub(ee::mul(ee::cvt_s(int32_t(v)), ps2::ffloat(0x3c008081u)), 1.0f);
}

// [driving] 0x00169248 PS2ScaleAnalogButton
float PS2ScaleAnalogButton(uint8_t v) {
    const float f = ee::mul(ee::cvt_s(int32_t(v)), ps2::ffloat(0x3baaaaabu));
    return ee::le(1.0f, f) ? 1.0f : f;
}

// [driving] 0x00167c48 InputDevice::DeadZoneChop
void DeadZoneChop(float lo, float hi, float& v) {
    if (ee::lt(lo, v) && ee::lt(v, hi)) v = 0.0f;
}

// [driving] 0x00169288 PS2PadDevice::PollDevice
void PadDevice::Poll(const PadData& pad) {
    std::memcpy(prev, cur, sizeof cur);  // MEM_copy(anterior, atual, 0x80)
    if (pad.lx == 0 && pad.ly == 0 && pad.rx == 0 && pad.ry == 0) {
        cur[0] = cur[1] = cur[2] = cur[3] = 0.0f;
    } else {
        const float lo = ps2::ffloat(0xbe99999au), hi = ps2::ffloat(0x3e99999au);
        cur[0] = PS2ScaleJoystick(pad.lx);
        cur[1] = ee::neg(PS2ScaleJoystick(pad.ly));
        cur[2] = PS2ScaleJoystick(pad.rx);
        cur[3] = ee::neg(PS2ScaleJoystick(pad.ry));
        for (int i = 0; i < 4; ++i) DeadZoneChop(lo, hi, cur[i]);
    }
    for (int b = 0; b < 16; ++b) cur[16 + b] = (pad.buttons & (1u << b)) ? 1.0f : 0.0f;
    if (pad.pad_type == 4) {
        // O controle digital: os analógicos copiados dos digitais (direita, esquerda, cima, baixo, triângulo, bola, X,
        // quadrado, L1, R1, L2, R2).
        static const int kFrom[12] = {29, 31, 28, 30, 20, 21, 22, 23, 18, 19, 16, 17};
        for (int k = 0; k < 12; ++k) cur[4 + k] = cur[kFrom[k]];
    } else {
        for (int k = 0; k < 12; ++k) cur[4 + k] = PS2ScaleAnalogButton(pad.pressure[k]);
    }
}

// [driving] 0x001677f8 DeviceScalar::isDown
bool scalar_is_down(int type, float cur) {
    if (type == 2) return ee::eq(cur, 1.0f);
    return ee::le(0.5f, cur);
}

// [driving] 0x00167878 DeviceScalar::isUp
bool scalar_is_up(int type, float cur) {
    if (type == 2) return ee::eq(cur, 0.0f);
    if (type == 1) return ee::lt(cur, 0.5f);
    return ee::lt(cur, -0.5f);
}

// [driving] 0x001678f8 DeviceScalar::isDownTransition
bool scalar_down_transition(int type, float prev, float cur) {
    if (type == 2) return ee::eq(cur, 1.0f) && ee::eq(prev, 0.0f);
    return ee::le(0.5f, cur) && ee::lt(prev, 0.5f);
}

// [driving] 0x001679c8 DeviceScalar::isUpTransition
bool scalar_up_transition(int type, float prev, float cur) {
    if (type == 2) return ee::eq(cur, 0.0f) && ee::eq(prev, 1.0f);
    if (type == 1) return ee::lt(cur, 0.5f) && ee::le(0.5f, prev);
    return ee::lt(cur, -0.5f) && ee::le(-0.5f, prev);
}

// [driving] 0x00167a98 DeviceScalar::isDownTransitionThreshold
bool scalar_down_threshold(float t, float prev, float cur) { return ee::le(t, cur) && ee::lt(prev, t); }

// [driving] 0x00167ad8 DeviceScalar::isUpTransitionThreshold
bool scalar_up_threshold(float t, float prev, float cur) { return ee::lt(cur, t) && ee::le(t, prev); }

// [driving] 0x00167b18 DeviceScalar::isCenteredTransition
bool scalar_centered(float t, float prev, float cur) {
    const float nt = ee::neg(t);
    if (!(ee::le(nt, cur) && ee::le(cur, t))) return false;
    return !(ee::le(nt, prev) && ee::le(prev, t));
}

// [driving] 0x001674d0 InputToAction::LoadParseDefFile
// [driving] 0x00167498 AdvanceFilePtr
// [driving] 0x00167120 InputTable::AddTableEntry
void InputMap::LoadParseDefFile(const std::string& text) {
    std::string buf = text;  // o PS2 lê o arquivo carregado (e não o muda: copia cada linha)
    const char* p = buf.c_str();
    for (;;) {
        // AdvanceFilePtr: depois do próximo '\n' (nulo se não há).
        const char* nl = std::strchr(p, '\n');
        if (!nl) {
            ++g_guards;  // o PS2 seguiria com um ponteiro nulo
            return;
        }
        p = nl + 1;
        const char* cr = std::strchr(p, '\r');
        if (!cr || cr - p > 255) {
            ++g_guards;  // sem '\r' o PS2 copia até um ponteiro nulo; mais de 255 estoura o buffer da pilha
            return;
        }
        char line[256];
        std::memcpy(line, p, size_t(cr - p));
        line[cr - p] = 0;
        char* state = nullptr;
        char* name = next_token(state, line);
        if (!name) {
            ++g_guards;  // uma linha vazia: o PS2 compara um ponteiro nulo
            continue;
        }
        if (nf::str_icmp(name, "FRONTEND") == 0 || nf::str_icmp(name, "END") == 0) return;
        char* button = next_token(state, nullptr);
        const int32_t action = getActionID(name);
        const int32_t index = lookup(kScalarNames, kPadScalars, button);
        char* mode = next_token(state, nullptr);
        int32_t method = 0;
        if (mode) {
            const int32_t m = lookup(kMethods, 10, mode);
            if (m != -1) method = m;
        }
        float threshold = 0.0f;
        if (method >= 3 && method < 5) threshold = 0.5f;
        char* param = next_token(state, nullptr);
        if (param) {
            // sscanf("%f"): o número do começo do texto (strtod e depois float), nada se não há número.
            char* end = nullptr;
            const double d = std::strtod(param, &end);
            if (end != param) threshold = float(d);
        }
        if (index < 0) {
            ++g_guards;  // o PS2 grava na tabela -1 (antes do vetor)
            continue;
        }
        table[index].push_back({method, threshold, action});
    }
}

// [driving] 0x00167678 InputToAction::Reset
void InputMap::Reset() {
    for (std::vector<InputEntry>& t : table) t.clear();
}

// [driving] 0x00168678 IOModule::SendGameMessages
void SendGameMessages(IOModuleState& io, PadDevice& dev, const InputMap& map, const InputFeed& feed) {
    for (int i = 0; i < kPadScalars; ++i) {
        const int type = pad_scalar_type(i);
        const float cur = dev.cur[i], prev = dev.prev[i];
        const bool changed = !ee::eq(cur, prev);
        for (const InputEntry& e : map.table[i]) {
            if (io.send_all == 0 && e.method != 8 && e.method != 9 && !changed) continue;
            int32_t action = 0;
            switch (e.method) {
            case 0: action = e.action; break;
            case 1: if (scalar_down_transition(type, prev, cur)) action = e.action; break;
            case 2: if (scalar_up_transition(type, prev, cur)) action = e.action; break;
            case 3: if (scalar_down_threshold(e.threshold, prev, cur)) action = e.action; break;
            case 4: if (scalar_up_threshold(e.threshold, prev, cur)) action = e.action; break;
            case 5: if (scalar_centered(ps2::ffloat(0x3e4ccccdu), prev, cur)) action = e.action; break;
            case 6: if (io.inverted) action = e.action; break;
            case 7: if (!io.inverted) action = e.action; break;
            case 8:
            case 9: {
                // A repetição: a passagem manda e marca (estado 1); depois, seguro, a cada atraso (300 ms a primeira, 150 as
                // outras; estado 2); o estado 3 não repete.
                const bool edge = e.method == 8 ? scalar_down_transition(type, prev, cur) : scalar_up_transition(type, prev, cur);
                if (edge) {
                    action = e.action;
                    dev.timer[i] = io.now;
                    dev.state[i] = 1;
                } else if ((e.method == 8 ? scalar_is_down(type, cur) : scalar_is_up(type, cur)) && dev.state[i] != 3) {
                    const int32_t delay = dev.state[i] == 1 ? io.first_delay : io.repeat_delay;
                    if (dev.timer[i] + delay < io.now) {
                        action = e.action;
                        dev.timer[i] = io.now;
                        dev.state[i] = 2;
                    }
                }
                break;
            }
            default: break;
            }
            if (action != 0 && feed) feed(action, dev.slot, cur);
        }
    }
    if (feed) feed(0x73, dev.slot, 0.0f);  // ACTION_INPUTSYNC
}

}  // namespace drv
