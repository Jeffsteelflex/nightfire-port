// A missão das fases de carro (driving/game/mission.h, events.h, hud.h, driving/render/font.h): propriedades
// independentes sobre dados montados à mão (sem dados do jogo): o cronômetro, as regras e as etapas, a criação dos
// eventos com os construtores que agem, o fim da missão, os textos do .loc, as mensagens do HUD e a fonte FNTS.

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "driving/carp_runtime.h"
#include "driving/game/events.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/triggers.h"
#include "driving/physics/simulation.h"
#include "driving/render/font.h"
#include "platform/ptr32.h"
#include "test_framework.h"

using namespace drv;

namespace {

void w32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4, 0);
    std::memcpy(b.data() + at, &v, 4);
}
void wf(std::vector<uint8_t>& b, size_t at, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    w32(b, at, u);
}

// Uma lista 'el' (o cabeçalho de 0x10 bytes e as entradas de 0x10; os dados de cada evento depois de todas, 0x20 bytes
// por evento) com os números e as palavras dadas.
using EvList = std::vector<std::pair<int32_t, std::vector<uint32_t>>>;
struct ElList {
    std::vector<uint8_t> bytes;
    explicit ElList(const EvList& events) {
        const size_t n = events.size();
        bytes.assign(0x10 + n * 0x10 + n * 0x20, 0);
        w32(bytes, 0, uint32_t(n));
        for (size_t i = 0; i < n; ++i) {
            const size_t e = 0x10 + i * 0x10, d = 0x10 + n * 0x10 + i * 0x20;
            w32(bytes, e, uint32_t(events[i].first));
            w32(bytes, e + 8, uint32_t(d - e));
            for (size_t k = 0; k < events[i].second.size(); ++k) w32(bytes, d + 4 + 4 * k, events[i].second[k]);
        }
    }
    uint32_t p32() const { return nf::p32_u(bytes.data()); }
};

// Os registros do 'Rule' (0x20 bytes: o tipo, as bandeiras, a etapa, a lista, os campos) e o UData deles.
struct RuleSet {
    std::vector<uint8_t> bytes;
    UData header{};
    void add(uint32_t type, uint32_t flags, int32_t stage, uint32_t list, uint32_t f10 = 0, uint32_t f14 = 0,
             uint32_t f18 = 0, uint32_t f1c = 0) {
        const size_t at = bytes.size();
        bytes.resize(at + 0x20, 0);
        w32(bytes, at, type);
        w32(bytes, at + 4, flags);
        w32(bytes, at + 8, uint32_t(stage));
        w32(bytes, at + 0xc, list);
        w32(bytes, at + 0x10, f10);
        w32(bytes, at + 0x14, f14);
        w32(bytes, at + 0x18, f18);
        w32(bytes, at + 0x1c, f1c);
    }
    UData* finish() {
        header.tag = 0x52756c65;
        header.flags = uint32_t(bytes.size()) << 8;
        header.count = uint32_t(bytes.size() / 0x20);
        header.offset = nf::p32_u(bytes.data());
        return &header;
    }
};

float fbits_to_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t float_bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void fresh_mission() {
    simulation_reset(60);
    simulation().cars.clear();
    trigger_manager().pending.clear();
    event_stats() = EventStats();
    uber_bond_defaults("test_mis", 1);
    GHud_Reset(hud());
    hud_exists() = true;
    SMissionManager_Construct(false);
}

void end_mission() {
    trigger_manager().pending.clear();
    mission_reset();
    hud_exists() = false;
    simulation_reset(60);
}

}  // namespace

// O cronômetro conta em passos da simulação: regressivo (-1) desce um por passo a partir do valor do Reset, parado fica
// com o valor do momento em que parou.
TEST(driving_mission_timer_counts_in_simulation_steps) {
    simulation_reset(60);
    SMissionTimer t;
    SMissionTimer_Construct(t);
    SMissionTimer_Reset(t, 600);
    simulation().steps = 100;
    SMissionTimer_Start(t, -1);
    CHECK(SMissionTimer_IsReverse(t));
    simulation().steps = 160;
    CHECK(SMissionTimer_GetValue(t) == 540);
    SMissionTimer_Stop(t);
    simulation().steps = 400;
    CHECK(SMissionTimer_GetValue(t) == 540);
    SMissionTimer_Start(t, 1);
    simulation().steps = 410;
    CHECK(SMissionTimer_GetValue(t) == 550);
    CHECK(!SMissionTimer_IsReverse(t));
    simulation_reset(60);
}

// As regras: a de sempre dispara a cada Update; a da etapa 0 só na etapa 0 e, "uma vez", desliga; o EChangeStage age
// já na construção (a próxima etapa) e a etapa muda no fim do ProcessRules; a regra de cronômetro dispara quando o
// cronômetro passa do limiar.
TEST(driving_mission_rules_stages_and_timers) {
    fresh_mission();
    // Etapa 0: muda para a 1 e arma o cronômetro 2 (3 s); etapa 1: o cronômetro abaixo de 1 s mostra o texto 777.
    ElList to_stage1(EvList{{kEventChangeStage, {1}}, {kEventSetTimer, {2, float_bits(3.0f), 0}}, {kEventStartTimer, {2, uint32_t(-1)}}});
    ElList show(EvList{{kEventDisplayText, {777, float_bits(2.0f)}}});
    ElList always(EvList{{kEventSetMagicCounter, {5, 9}}});
    // No heap (o UData guarda ponteiros de 4 bytes: no x64, dentro da janela da arena).
    auto rules = std::make_unique<RuleSet>();
    rules->add(0, 1, -1, always.p32());                // Always, ligada (sempre)
    rules->add(0, 3, 0, to_stage1.p32());              // Always, ligada e uma vez, etapa 0
    rules->add(9, 3, 1, show.p32(), 2, 0, float_bits(1.0f));  // Timer 2 abaixo de 1 s, etapa 1
    SMissionManagerData& m = mission_manager();
    SMissionManager_Load(m, rules->finish(), nullptr);
    CHECK(m.num_stages == 2);
    CHECK(mission_lists().rules[0].size() == 1 && mission_lists().rules[1].size() == 1 &&
          mission_lists().rules[2].size() == 1);
    SMissionManager_ChangeStage(m, 0);
    SMissionManager_Start(m);
    simulation().steps = 10;
    SMissionManager_ProcessRules(m);  // a etapa 0 (de 0 que o ChangeStage pôs em next) vira 1
    CHECK(m.stage == 1);
    CHECK(mission_lists().rules[1][0]->active == 0);  // uma vez
    CHECK(trigger_manager().pending.size() == 4);     // o contador, a etapa, o cronômetro armado e ligado
    events_run();
    CHECK(SMissionManager_GetMagicCounter(m, 5) == 9);
    CHECK(SMissionTimer_GetValue(m.timers[2]) == 181);  // (int)(3 x 60 + 1)
    // 2 s depois o cronômetro está em 61 passos (1,02 s), depois em 60 (1 s, não abaixo): a regra ainda não dispara; com
    // 59 passos, sim (abaixo do limiar e o último valor não estava).
    simulation().steps = 130;
    SMissionManager_ProcessRules(m);
    events_run();
    simulation().steps = 131;
    SMissionManager_ProcessRules(m);
    events_run();
    CHECK(hud().message_text == -1);
    simulation().steps = 132;
    SMissionManager_ProcessRules(m);
    events_run();
    CHECK(hud().message_text == 777);
    CHECK(hud().message_time == 2.0f);
    end_mission();
}

// O fim: o EPlayerLose perde (estado 4, o resultado "fracassada", os gatilhos desligados) e um EPlayerWin depois não
// muda nada; no bloco da troca o resultado da missão (1: falhou com vida).
TEST(driving_mission_lose_then_win_is_ignored) {
    fresh_mission();
    WTriggerManager_Reset();
    SMissionManagerData& m = mission_manager();
    SMissionManager_Start(m);
    trigger_manager().active = 1;
    ElList lose(EvList{{kEventPlayerLose, {0, 0, 1, 0}}});
    ElList win(EvList{{kEventPlayerWin, {0, 0}}});
    const TriggerEvents* l = nf::p32_ptr<const TriggerEvents>(lose.p32());
    const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(l + 1);
    event_create(e->type, reinterpret_cast<const uint8_t*>(e) + e->data_offset);
    events_run();
    CHECK(m.state == 4);
    CHECK(trigger_manager().active == 0);
    CHECK(hud().result_text == 0x18577 && hud().result_state == 5);
    CHECK(uber_bond().word(0x978) == 1);
    const TriggerEvents* lw = nf::p32_ptr<const TriggerEvents>(win.p32());
    const TriggerEvent* ew = reinterpret_cast<const TriggerEvent*>(lw + 1);
    event_create(ew->type, reinterpret_cast<const uint8_t*>(ew) + ew->data_offset);
    events_run();
    CHECK(m.state == 4);
    CHECK(hud().result_text == 0x18577);
    end_mission();
}

// A imagem do objeto do PS2: a tabela virtual da classe e as palavras dos dados nos lugares da criadora.
TEST(driving_mission_event_object_image) {
    fresh_mission();
    ElList text(EvList{{kEventDisplayText, {4242, float_bits(5.0f)}}});
    const TriggerEvents* l = nf::p32_ptr<const TriggerEvents>(text.p32());
    const TriggerEvent* e = reinterpret_cast<const TriggerEvent*>(l + 1);
    event_create(e->type, reinterpret_cast<const uint8_t*>(e) + e->data_offset);
    const PendingEvent& p = trigger_manager().pending.back();
    const EventInfo* info = event_info(kEventDisplayText);
    CHECK(info != nullptr && p.size == info->size && p.obj[0] == info->vtable);
    CHECK(p.obj[1] == 4242 && fbits_to_float(p.obj[2]) == 5.0f);
    CHECK(p.written == 0x7u);
    CHECK(event_info(9999) == nullptr);
    end_mission();
}

// Os textos: o rótulo achado pela tabela (a busca pelos 16 bits de baixo), a língua corrente, e "MISSING" sem ele.
TEST(driving_mission_locale_lookup) {
    std::vector<uint8_t> f(0x200, 0);
    std::memcpy(f.data(), "LOCH", 4);
    w32(f, 4, 0x40);    // a tabela
    f[8] = 1;
    f[0xc] = 2;         // duas línguas
    f[0xe] = 1;         // a segunda
    w32(f, 0x10, 0x80);
    w32(f, 0x14, 0xc0);
    std::memcpy(f.data() + 0x40, "LOCI", 4);
    w32(f, 0x48, 2);
    // os pares {rótulo, índice} ordenados pelo rótulo
    f[0x50] = 0x34; f[0x51] = 0x12; f[0x52] = 1; f[0x53] = 0;
    f[0x54] = 0x78; f[0x55] = 0x56; f[0x56] = 0; f[0x57] = 0;
    w32(f, 0xc0 + 0xc, 2);
    w32(f, 0xc0 + 0x10, 0x20);
    w32(f, 0xc0 + 0x14, 0x28);
    std::memcpy(f.data() + 0xc0 + 0x20, "zero", 5);
    std::memcpy(f.data() + 0xc0 + 0x28, "um", 3);
    LocaleFile& loc = locale();
    const LocaleFile saved = loc;
    CHECK(loc.load(f));
    CHECK(std::string(LOCALE_Symbol(0x15678)) == "zero");  // os 16 bits de baixo: 0x5678
    CHECK(std::string(LOCALE_Symbol(0x1234)) == "um");
    CHECK(std::string(LOCALE_Symbol(0x4321)) == "MISSING");
    loc = saved;
}

// As mensagens do HUD: um objetivo novo durante outro fica na fila e entra quando o primeiro acaba (5 s); as tarjas
// entram em 30 quadros; a mensagem da missão esmaece meio segundo depois do tempo.
TEST(driving_mission_hud_messages) {
    GHudData& h = hud();
    GHud_Reset(h);
    GHud_TriggerObjectiveMsg(h, 1, 100, 0);
    GHud_TriggerObjectiveMsg(h, 2, 200, 0);
    CHECK(h.objective_text == 100 && h.objective_next == 200);
    for (int i = 0; i < 300; ++i) GHud_Tick(h, 1.0f / 60.0f);
    CHECK(h.objective_time == 0.0f);
    GHud_Tick(h, 1.0f / 60.0f);
    CHECK(h.objective_text == 200 && h.objective_kind == 2 && h.objective_time == 5.0f && h.objective_next == -1);
    GHud_LetterBoxOn(h, false);
    for (int i = 0; i < 29; ++i) GHud_Tick(h, 1.0f / 60.0f);
    CHECK(h.letterbox_mode == 2);
    GHud_Tick(h, 1.0f / 60.0f);
    CHECK(h.letterbox_mode == 0 && h.letterbox_count == 30);
    GHud_TriggerMissionMsg(h, 1.0f, 300);
    for (int i = 0; i < 61; ++i) GHud_Tick(h, 1.0f / 60.0f);
    CHECK(h.message_time == 0.0f && h.message_fade > 0.0f);
    GHud_SetMissionPassed(h);
    CHECK(h.result_text == 0x18576 && h.result_state == 5);
    GHud_TriggerMissionMsg(h, 1.0f, 400);  // com o resultado na tela, nada
    CHECK(h.message_text == 300);
    GHud_Reset(h);
}

// A fonte FNTS: um arquivo montado à mão (dois caracteres e uma imagem de 8 bits 4x4 com a paleta de 32 bits).
TEST(driving_mission_fnts_font) {
    std::vector<uint8_t> f(0x80 + 2 * 12, 0);
    std::memcpy(f.data(), "FNTS", 4);
    f[0xa] = 2;
    w32(f, 0x14, 0x80);
    w32(f, 0x24, 6);
    // 'A': 3x4 em (0, 0), avanço 4; ' ': 1x1, avanço 2
    f[0x80] = 'A'; f[0x82] = 3; f[0x83] = 4; f[0x88] = 4; f[0x8a] = 1;
    f[0x8c] = ' '; f[0x8e] = 1; f[0x8f] = 1; f[0x94] = 2;
    const size_t image = f.size();
    w32(f, 0x1c, uint32_t(image));
    // o bloco da imagem (8 bits, 4x4) e o da paleta (0x21, 256 cores de 32 bits)
    std::vector<uint8_t> img(16 + 16, 0);
    img[0] = 0x02;
    const uint32_t next = uint32_t(img.size());
    img[1] = uint8_t(next);
    img[4] = 4;
    img[6] = 4;
    for (int i = 0; i < 16; ++i) img[16 + i] = uint8_t(i == 5 ? 1 : 0);
    std::vector<uint8_t> pal(16 + 1024, 0);
    pal[0] = 0x21;
    pal[4] = 0;
    pal[5] = 1;  // 256 de largura
    pal[6] = 1;  // 1 de altura
    pal[16 + 4] = 0xff; pal[16 + 5] = 0xff; pal[16 + 6] = 0xff; pal[16 + 7] = 0x80;  // a cor 1: branca, opaca no GS
    f.insert(f.end(), img.begin(), img.end());
    f.insert(f.end(), pal.begin(), pal.end());
    FontFile font;
    CHECK(font_parse(f.data(), f.size(), font));
    CHECK(font.glyphs.size() == 2 && font.line_height == 6);
    CHECK(font.image_width == 4 && font.image_height == 4);
    CHECK(font.rgba.size() == 64 && font.rgba[5 * 4 + 3] == 0x80 && font.rgba[3] == 0);
    CHECK(font.width("A A") == 10);
    const std::vector<FontQuad> q = font_layout(font, "A A", 10, 20, 2.0f);
    CHECK(q.size() == 2);  // o espaço não desenha
    CHECK(q[0].x0 == 10.0f && q[0].y0 == 22.0f && q[0].x1 == 16.0f && q[0].y1 == 30.0f);
    CHECK(q[1].x0 == 22.0f);  // 10 + (4 + 2) x 2
    CHECK(q[0].u0 == 0.0f && q[0].u1 == 0.75f && q[0].v1 == 1.0f);
    CHECK(!font_parse(f.data(), 0x20, font));
}
