// O HUD da missão (driving/game/hud.h), portado do MIPS do DRIVING.ELF (GHud.obj; GSystem.obj e o LOCALE da EA). As
// desmontagens ficam em out/agent_drv_mission/ do checkout principal (fora do git).
//
// Divergências (port): o desenho é do port (hud_view: os textos na tela do PS2 com a fonte de depuração do disco, as
// faixas, o escurecer); o passar do tempo das mensagens (GHud_Tick) segue os Draw* do PS2 com o tempo do quadro (as
// durações e a ordem: a mensagem some depois de meio segundo de esmaecer, o objetivo seguinte entra quando o atual
// acaba, as tarjas andam 30 quadros) sem as galerias; os sons do menu e o GSubtitles só anotados (mission_trace).

#include "driving/game/hud.h"

#include <cstdio>
#include <cstring>

#include "driving/game/mission.h"
#include "driving/physics/simulation.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

GHudData g_hud{};
bool g_hud_exists = false;
LocaleFile g_locale;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

}  // namespace

GHudData& hud() { return g_hud; }
bool& hud_exists() { return g_hud_exists; }

// [driving] 0x0023c858 GHud::Reset
void GHud_Reset(GHudData& h) {
    std::memset(&h, 0, sizeof h);
    h.timer_beep_second = 10;
    h.letterbox_mode = 1;
    h.letterbox_count = 0;
    h.paused = 0;
    h.button_text = 0;
    h.button_time = 0;
    h.button_fade = 0;
    h.objective_extra = 0;
    h.result_text = -1;
    h.result_state = 0;
    h.result_delay = 0;
    h.unknown318 = 0;
    h.message_time = 0;
    h.message_long = 0;
    h.message_text = -1;
    h.message_step = 0;
    h.unknown36c = 1.0f;
    h.language = 0;  // GLocale::GetLanguage (o inglês)
    h.aiming = 0;
    h.black_fade = 0;
    h.black_fading = 0;
    h.black_fade_start = 0;
    h.black_fade_from = 0;
    h.black_fade_rate = 0;
    h.timer = -1;
    h.objective_kind = -1;
    h.objective_text = -1;
    h.objective_next = -1;
    h.objective_next_kind = -1;
    h.objective_alpha = 0;
    h.objective_frames = 0;
    h.objective_time = 0;
    h.unknown480 = 0;
    h.unknown48c = 0;
    h.screen_width = 640.0f;   // o RRenderer +0x50 e +0x54 (a tela do PS2)
    h.screen_height = 448.0f;
    h.screen_cx = 320.0f;
    h.screen_cy = 224.0f;
    h.blood_curtain = -1.0f;
    h.frame_time = 1.0f / 60.0f;
}

// [driving] 0x0023c728 GHud::FadeInFromBlack(int passos): de 1 a 0 em `passos` (pelo menos 1).
void GHud_FadeInFromBlack(GHudData& h, int32_t steps) {
    const int32_t n = steps > 0 ? steps : 1;
    h.black_fading = 1;
    h.black_fade_start = simulation().steps;
    h.black_fade = 1.0f;
    h.black_fade_from = 1.0f;
    h.black_fade_rate = ee::div(-1.0f, ee::cvt_s(n));
}

// [driving] 0x0023c780 GHud::FadeOutToBlack(int passos): de 0 a 1 (o GSubtitles::ClearSubtitles de quem vai pular a cena
// anotado).
void GHud_FadeOutToBlack(GHudData& h, int32_t steps) {
    const int32_t n = steps > 0 ? steps : 1;
    h.black_fading = 1;
    h.black_fade_start = simulation().steps;
    h.black_fade = 0.0f;
    h.black_fade_from = 0.0f;
    h.black_fade_rate = ee::div(1.0f, ee::cvt_s(n));
    if (SMissionManager_GoingToSkipCinematic(mission_manager())) mission_note_call(0x24a870);
}

// [driving] 0x00244ea0 GHud::LetterBoxOn(bool já)
void GHud_LetterBoxOn(GHudData& h, bool instant) {
    if (instant) {
        h.letterbox_mode = 0;
        h.letterbox_count = 30;
    } else {
        h.letterbox_count = 0;
        h.letterbox_mode = 2;
    }
    h.aiming = 0;
}

// [driving] 0x00244ec8 GHud::LetterBoxOff(bool já)
void GHud_LetterBoxOff(GHudData& h, bool instant) { h.letterbox_mode = instant ? 1 : 3; }

// [driving] 0x00244f30 GHud::AimOff (o 0x3353ec +4, o alvo da mira do WTargetPicker, não existe no port)
void GHud_AimOff(GHudData& h) { h.aiming = 0; }

// [driving] 0x00245eb0 GHud::KillButtonMessage
void GHud_KillButtonMessage(GHudData& h) {
    h.button_fade = 0;
    h.button_text = 0;
    h.button_time = 0;
}

// [driving] 0x00245ec0 GHud::ClearObjectiveMessage
void GHud_ClearObjectiveMessage(GHudData& h) {
    h.objective_time = 0;
    h.objective_next_kind = -1;
    h.objective_next = -1;
    h.objective_text = 0;
    h.objective_frames = 0;
    h.objective_alpha = 0;
}

// [driving] 0x00245890 GHud::TriggerMissionMsg: fora do resultado, a mensagem por `seconds` (o +0x410: o texto em
// inglês passa de 32 caracteres, na tela 4:3).
void GHud_TriggerMissionMsg(GHudData& h, float seconds, int32_t text) {
    if (h.result_state != 0) return;
    h.message_time = seconds;
    h.message_text = text;
    h.message_long = 0;
    if (std::strlen(LOCALE_Symbol(text)) > 0x20) h.message_long = 1;  // RRenderer +0x5c (a tela larga) 0
    h.message_step = simulation().steps;
}

// [driving] 0x00245910 GHud::TriggerObjectiveMsg(int tipo, int rótulo, int extra): mostrando outro, fica na fila (a
// mesma mensagem do mesmo tipo não repete); senão começa já (5 s, o som do objetivo anotado).
void GHud_TriggerObjectiveMsg(GHudData& h, int32_t kind, int32_t text, int32_t extra) {
    if (h.result_state != 0) return;
    if (ee::lt(0.0f, h.objective_time)) {
        if (h.objective_text == text && h.objective_kind == kind) {
            h.button_time = 0;
            return;
        }
        h.objective_next = text;
        h.objective_next_kind = kind;
        h.button_time = 0;
        return;
    }
    h.objective_frames = 0;
    h.objective_next_kind = -1;
    h.objective_text = text;
    h.objective_kind = kind;
    h.objective_alpha = 0.0f;
    h.objective_time = 5.0f;
    h.objective_extra = extra;
    mission_note_call(0x2f4b10, 8, 0x3aadf0, 0x3aaac8, 2);  // AMenuSound::Trigger(8, "SFX_objective"...)
    h.objective_next = -1;
    h.button_time = 0;
}

// [driving] 0x002459d0 GHud::TriggerButtonMsg(float segundos, int texto, int símbolo)
void GHud_TriggerButtonMsg(GHudData& h, float seconds, int32_t text, int32_t symbol) {
    h.button_text = text;
    h.button_symbol = symbol;
    h.button_time = seconds;
    h.button_fade = 0.5f;
    h.objective_time = 0;
    h.objective_frames = 0;
    h.objective_alpha = 0;
}

// [driving] 0x0023e568 GHud::TriggerTimerOn / 0x0023e570 TriggerTimerOff
void GHud_TriggerTimerOn(GHudData& h, int32_t timer) { h.timer = timer; }
void GHud_TriggerTimerOff(GHudData& h) { h.timer = -1; }

// [driving] 0x002456c0 GHud::SetMissionPassed
void GHud_SetMissionPassed(GHudData& h) {
    GHud_ClearObjectiveMessage(h);
    h.unknown318 = 0;
    h.result_text = 0x18576;
    GHud_AimOff(h);
    h.result_delay = 0;
    h.result_state = 5;
}

// [driving] 0x00245708 GHud::SetMissionFailed(bool já): já, ou com a cortina de sangue, o resultado (5) com o atraso da
// cortina; senão o estado 1 (o resultado espera).
void GHud_SetMissionFailed(GHudData& h, bool now) {
    GHud_ClearObjectiveMessage(h);
    h.unknown318 = 0;
    h.unknown48c = 0;
    h.unknown480 = 5;
    h.result_text = 0x18577;
    GHud_AimOff(h);
    if (now) {
        h.result_state = 5;
        h.result_delay = 0;
    } else if (ee::lt(0.0f, h.blood_curtain)) {
        h.result_state = 5;
        h.result_delay = h.blood_curtain;
    } else {
        h.result_state = 1;
    }
}

// [driving] 0x0023e558 GHud::TriggerBloodCurtain
void GHud_TriggerBloodCurtain(GHudData& h) { h.blood_curtain = 6.0f; }

// Os contadores dos Draw* (port: uma vez por quadro).
void GHud_Tick(GHudData& h, float dt) {
    h.frame_time = dt;
    // GHud::DrawScreenFades (0x0023e930): o escurecer pelo passo da simulação, preso em [0, 1].
    if (h.black_fading != 0) {
        const float v = ee::add(h.black_fade_from,
                                ee::mul(h.black_fade_rate, ee::cvt_s(simulation().steps - h.black_fade_start)));
        if (ee::lt(v, 0.0f)) {
            h.black_fade = 0.0f;
            h.black_fading = 0;
        } else if (ee::lt(1.0f, v)) {
            h.black_fade = 1.0f;
            h.black_fading = 0;
        } else {
            h.black_fade = v;
        }
    }
    // GHud::DrawLetterBox (0x00245190)
    if (h.letterbox_mode == 2) {
        if (++h.letterbox_count >= 30) {
            h.letterbox_mode = 0;
            h.letterbox_count = 30;
        }
    } else if (h.letterbox_mode == 3) {
        if (--h.letterbox_count <= 0) {
            h.letterbox_count = 0;
            h.letterbox_mode = 1;
        }
    }
    // GHud::DrawMessage (0x002469b8): o esmaecer e o tempo.
    if (ee::lt(0.0f, h.message_fade)) {
        h.message_fade = ee::sub(h.message_fade, dt);
        if (ee::le(h.message_fade, 0.0f)) {
            h.message_fade = 0.0f;
            h.message_long = 0;
        }
    }
    if (ee::lt(0.0f, h.message_time)) {
        h.message_time = ee::sub(h.message_time, dt);
        if (ee::le(h.message_time, 0.0f)) {
            h.message_time = 0.0f;
            h.message_fade = 0.5f;
        }
    }
    // GHud::DrawObjectiveMessage (0x00246c48): o tempo do objetivo e o próximo da fila.
    if (h.paused == 0) {
        if (ee::lt(0.0f, h.objective_time)) {
            h.objective_frames += 1;
            h.objective_time = ee::sub(h.objective_time, dt);
            if (ee::le(h.objective_time, 0.0f)) h.objective_time = 0.0f;
        } else if (h.objective_next != -1) {
            h.objective_frames = 0;
            h.objective_text = h.objective_next;
            h.objective_kind = h.objective_next_kind;
            h.objective_alpha = 0;
            h.objective_time = 5.0f;
            h.objective_next = -1;
            h.objective_next_kind = -1;
        }
    }
    // GHud::DrawButtonMessage (0x00245ab8)
    if (ee::lt(0.0f, h.button_time)) {
        h.button_fade = ee::sub(h.button_fade, dt);
        if (ee::le(h.button_fade, 0.0f)) h.button_fade = 0.5f;
        h.button_time = ee::sub(h.button_time, dt);
        if (ee::le(h.button_time, 0.0f)) GHud_KillButtonMessage(h);
    }
    if (ee::lt(0.0f, h.blood_curtain)) h.blood_curtain = ee::sub(h.blood_curtain, dt);
}

// ---------------------------------------------------------------- os textos

bool LocaleFile::load(std::vector<uint8_t> bytes) {
    data = std::move(bytes);
    if (data.size() < 0x14 || std::memcmp(data.data(), "LOCH", 4) != 0) {
        data.clear();
        return false;
    }
    return true;
}

// [driving] 0x00259360 (LOCALE_getstr) com 0x002592e0 (a busca do rótulo: bsearch de pares {u16, u16} pelo u16 de
// baixo do rótulo).
const char* LocaleFile::symbol(int32_t id) const {
    if (data.empty()) return nullptr;
    const uint8_t* l = data.data();
    const size_t n = data.size();
    int32_t index = id;
    if (l[8] & 1) {
        const uint32_t tab = le32(l + 4);
        if (tab + 0x10 > n) return nullptr;
        const uint32_t count = le32(l + tab + 8);
        const uint16_t key = uint16_t(id);
        uint32_t lo = 0, hi = count;
        index = -1;
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            const uint8_t* e = l + tab + 0x10 + mid * 4;
            if (e + 4 > l + n) return nullptr;
            const uint16_t k = le16(e);
            if (k == key) {
                index = le16(e + 2);
                break;
            }
            if (key < k) hi = mid;
            else lo = mid + 1;
        }
    }
    const uint16_t lang = le16(l + 0xe);
    if (0x10u + lang * 4u + 4 > n) return nullptr;
    const uint32_t block = le32(l + 0x10 + lang * 4u);
    if (index < 0 || block + 0x10 > n) return nullptr;
    const uint32_t count = le32(l + block + 0xc);
    if (uint32_t(index) >= count) return nullptr;
    const uint32_t off = le32(l + block + 0x10 + uint32_t(index) * 4);
    if (block + off >= n) return nullptr;
    return reinterpret_cast<const char*>(l + block + off);
}

LocaleFile& locale() { return g_locale; }

// [driving] 0x0024b1a0 GSystem::LOCALE_Symbol (o modo dos números, 0x33ba70, desligado)
const char* LOCALE_Symbol(int32_t id) {
    const char* s = g_locale.symbol(id);
    return s ? s : "MISSING";
}

// ---------------------------------------------------------------- o desenho do port

HudView hud_view(const GHudData& h) {
    HudView v;
    v.black = h.black_fade;
    // As tarjas (DrawLetterBox: a altura count x 65 / 30).
    if (h.letterbox_mode != 1) {
        const float bar = float(int(float(h.letterbox_count) * 65.0f / 30.0f));
        v.boxes.push_back({0, 0, h.screen_width, bar, 0xff000000u});
        v.boxes.push_back({0, h.screen_height - bar, h.screen_width, bar, 0xff000000u});
    }
    const uint32_t gold = 0x00b3dbfau;  // o 0xb3dbfa dos Draw* (BGR: um amarelo claro)
    auto with_alpha = [](uint32_t rgb, float a) {
        const int ai = int(a * 255.0f + 0.5f);
        return rgb | uint32_t(ai < 0 ? 0 : ai > 255 ? 255 : ai) << 24;
    };
    // O resultado (DrawMissionResult).
    if (h.result_state != 0 && h.result_text != -1) {
        v.boxes.push_back({0, 180, h.screen_width, 70, 0xa0000000u});
        v.lines.push_back({LOCALE_Symbol(h.result_text), h.screen_cx, 196, 2.0f, with_alpha(gold, 1.0f), true});
        return v;
    }
    // O cronômetro (DrawTimer: "m:ss" em cima, vermelho nos últimos 10 s da contagem regressiva).
    if (h.timer >= 0 && h.timer < 10 && mission_exists()) {
        SMissionTimer& t = mission_manager().timers[h.timer];
        const int32_t fr = simulation().frame_rate > 0 ? simulation().frame_rate : 60;
        const int32_t s = SMissionTimer_GetValue(t) / fr;
        if (s > 0) {
            char b[16];
            std::snprintf(b, sizeof b, "%2d:%02d", s / 60, s % 60);
            const bool red = SMissionTimer_IsReverse(t) && s / 60 == 0 && s % 60 <= 9;
            v.boxes.push_back({h.screen_cx - 42, 30, 84, 26, 0x900f0505u});
            v.lines.push_back({b, h.screen_cx, 35, 1.4f, red ? 0xff0505c8u : with_alpha(gold, 1.0f), true});
        }
    }
    // O objetivo (DrawObjectiveMessage: o título e o texto).
    if (ee::lt(0.0f, h.objective_time) && h.objective_text != -1) {
        const int32_t title = h.objective_kind == 2 ? 99999 : h.objective_kind == 3 ? 0x18598 : 0x1869e;
        float a = 1.0f;
        if (h.objective_time < 0.5f) a = h.objective_time * 2.0f;
        v.boxes.push_back({h.screen_cx - 200, 64, 400, 46, with_alpha(0x000000, 0.45f * a)});
        v.lines.push_back({LOCALE_Symbol(title), h.screen_cx, 68, 1.1f, with_alpha(0xfecc64u, a), true});
        v.lines.push_back({LOCALE_Symbol(h.objective_text), h.screen_cx, 88, 1.1f, with_alpha(gold, a), true});
    }
    // A mensagem de botão (DrawButtonMessage, y 100).
    if (ee::lt(0.0f, h.button_time)) {
        const char* s = h.button_symbol > 0 ? LOCALE_Symbol(h.button_symbol) : "";
        v.lines.push_back({s, h.screen_cx, 100, 1.4f, with_alpha(gold, 0.6f + h.button_fade * 0.8f), true});
    }
    // A mensagem da missão (DrawMessage, y 380, com o fundo).
    float ma = 0.0f;
    if (ee::lt(0.0f, h.message_time)) ma = 1.0f;
    else if (ee::lt(0.0f, h.message_fade)) ma = h.message_fade * 2.0f;
    if (ma > 0.0f && h.message_text != -1) {
        v.boxes.push_back({40, 372, h.screen_width - 80, 28, with_alpha(0x000000, 0.5f * ma)});
        v.lines.push_back({LOCALE_Symbol(h.message_text), h.screen_cx, 380, 1.2f, with_alpha(gold, ma), true});
    }
    return v;
}

}  // namespace drv
