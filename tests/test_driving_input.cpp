// A entrada das missões de carro (driving/game/input.h): propriedades lidas no código do jogo, conferidas sem o original
// (a conferência bit a bit contra o iomodule.obj e o actionqueuemgr.obj fica no nf_driving_check, parte 10): as escalas
// do controle, a zona morta, o y para cima, o controle digital, a leitura de um .def montado à mão (o cabeçalho, os
// modos, o limiar padrão, o fim no FRONTEND, o botão desconhecido) e as ações de cada modo.

#include <string>
#include <utility>
#include <vector>

#include "driving/game/input.h"
#include "test_framework.h"

using namespace drv;

TEST(driving_input_scales_and_poll) {
    CHECK(PS2ScaleJoystick(0) == -1.0f && PS2ScaleJoystick(255) == 1.0f);
    CHECK(PS2ScaleJoystick(128) > 0.0f && PS2ScaleJoystick(128) < 0.01f);
    CHECK(PS2ScaleAnalogButton(0) == 0.0f && PS2ScaleAnalogButton(192) == 1.0f && PS2ScaleAnalogButton(255) == 1.0f);
    CHECK(PS2ScaleAnalogButton(96) > 0.49f && PS2ScaleAnalogButton(96) < 0.51f);
    float v = 0.29f;
    DeadZoneChop(-0.3f, 0.3f, v);
    CHECK(v == 0.0f);
    v = 0.31f;
    DeadZoneChop(-0.3f, 0.3f, v);
    CHECK(v == 0.31f);
    PadDevice d;
    PadData p;
    p.lx = 255;
    p.ly = 0;  // para cima no PS2: o ALY do jogo é +1
    p.rx = 140;  // dentro da zona morta
    p.buttons = 1u << 6;  // X
    p.pressure[6] = 255;
    d.Poll(p);
    CHECK(d.cur[0] == 1.0f && d.cur[1] == 1.0f && d.cur[2] == 0.0f && d.cur[22] == 1.0f && d.cur[10] == 1.0f);
    CHECK(d.prev[0] == 0.0f);
    // O controle digital: os analógicos copiam os digitais (o X na pressão do X).
    PadData q;
    q.pad_type = 4;
    q.buttons = (1u << 5) | (1u << 13);  // bola, direita
    d.Poll(q);
    CHECK(d.prev[0] == 1.0f && d.cur[9] == 1.0f && d.cur[4] == 1.0f && d.cur[10] == 0.0f);
    // Os quatro direcionais em zero: sem analógico, tudo zero (o 0 não vira -1).
    PadData z;
    z.lx = z.ly = z.rx = z.ry = 0;
    d.Poll(z);
    CHECK(d.cur[0] == 0.0f && d.cur[1] == 0.0f);
    CHECK(pad_scalar_type(3) == 0 && pad_scalar_type(4) == 1 && pad_scalar_type(16) == 2);
    CHECK(std::string(pad_scalar_name(22)) == "DButtonCross");
}

TEST(driving_input_def_file) {
    const std::string def =
        "action\tbutton\tupdate method\r\n"
        "GAMEACTION_STEER\t\t\tALX\r\n"
        "GAMEACTION_GAS\t\tAButtonCross\r\n"
        "GAMEACTION_HANDBRAKE\t\tAButtonCircle\t\t>\t0.1\r\n"
        "GAMEACTION_HANDBRAKERELEASE\tAButtonCircle\t\t<\r\n"
        "GAMEACTION_FIREGADGET  DButtonL1  +\r\n"
        "NAO_EXISTE\tDButtonL1\t-\r\n"
        "GAMEACTION_GAS\tBotaoNenhum\r\n"
        "FRONTEND\t; o resto não é tabela\r\n"
        "GAMEACTION_BRAKE\tAButtonSquare\r\n";
    const uint64_t guards = input_guards();
    InputMap m;
    m.LoadParseDefFile(def);
    CHECK(m.table[0].size() == 1 && m.table[0][0].action == 1 && m.table[0][0].method == 0);
    CHECK(m.table[10].size() == 1 && m.table[10][0].action == 22);
    // O limiar: o dado (0,1) e o padrão de > e < (0,5).
    CHECK(m.table[9].size() == 2 && m.table[9][0].method == 3 && m.table[9][0].threshold == 0.1f);
    CHECK(m.table[9][1].method == 4 && m.table[9][1].threshold == 0.5f && m.table[9][1].action == 28);
    // Separados por espaços também; a ação desconhecida é 0x75.
    CHECK(m.table[18].size() == 2 && m.table[18][0].method == 1 && m.table[18][1].action == 0x75 &&
          m.table[18][1].method == 2);
    // O botão desconhecido: pulado e contado; depois do FRONTEND nada entra.
    CHECK(input_guards() == guards + 1 && m.table[11].empty());
    CHECK(getActionID("GAMEACTION_BRAKE") == 26 && getActionID("ACTION_INPUTSYNC") == 0x73);
    m.Reset();
    CHECK(m.table[9].empty());
}

TEST(driving_input_send_game_messages) {
    InputMap m;
    m.LoadParseDefFile(
        "h\r\nGAMEACTION_STEER\tALX\r\nGAMEACTION_FIREGADGET\tDButtonL1\t+\r\nGAMEACTION_FIREGADGETRELEASE\tDButtonL1\t-\r\n"
        "GAMEACTION_HANDBRAKE\tAButtonCircle\t>\t0.1\r\nGAMEACTION_SOUND_STEERHORIZONTALRELEASE\tALX\t=\r\n"
        "FRONTENDACTION_UP\tDButtonUp\tR\r\nEND\r\n");
    PadDevice d;
    IOModuleState io;
    io.first_delay = 18;
    io.repeat_delay = 9;
    std::vector<std::pair<int, float>> got;
    const InputFeed feed = [&](int32_t a, int32_t, float v) { got.emplace_back(a, v); };
    auto frame = [&](const PadData& p) {
        got.clear();
        d.Poll(p);
        SendGameMessages(io, d, m, feed);
    };
    PadData p;
    frame(p);
    // Nada mudou (o centro cai na zona morta): só o INPUTSYNC.
    CHECK(got.size() == 1 && got[0].first == 0x73);
    p.lx = 255;
    p.buttons = 1u << 2;  // L1
    p.pressure[5] = 64;  // a bola, um terço: passa o 0,1
    frame(p);
    CHECK(got.size() == 4 && got[0] == std::make_pair(1, 1.0f) && got[1].first == 27 && got[2].first == 36);
    // Seguro: nada (o U só manda quando muda).
    frame(p);
    CHECK(got.size() == 1);
    // Solto: a passagem para cima do L1 e a volta ao centro do ALX.
    p.lx = 128;
    p.buttons = 0;
    frame(p);
    CHECK(got.size() == 4 && got[0] == std::make_pair(1, 0.0f) && got[1].first == 16 && got[2].first == 37);
    // O "mandar tudo": o U manda mesmo sem mudança.
    io.send_all = 1;
    frame(p);
    CHECK(got.size() == 2 && got[0].first == 1);
    io.send_all = 0;
    // A repetição (R): na descida, depois de 18 ticks e a cada 9.
    p.buttons = 1u << 12;  // cima
    std::vector<int> when;
    for (int t = 100; t < 140; ++t) {
        io.now = t;
        frame(p);
        for (const auto& g : got)
            if (g.first == 65) when.push_back(t);
    }
    CHECK(when.size() == 4 && when[0] == 100 && when[1] == 119 && when[2] == 129 && when[3] == 139);
}
