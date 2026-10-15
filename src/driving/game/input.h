#pragma once
// A entrada do jogo nas missões de carro (iomodule.obj e actionqueuemgr.obj do DRIVING.ELF; docs/driving.md seção 14):
// o controle do PS2 visto como 32 escalares (PS2PadDevice), a tabela que o .def do controle monta (InputToAction:
// data\Control\drivecfg.def, FrontEnd.def, povcfg*.def) e o IOModule::SendGameMessages, que compara cada escalar com as
// linhas da tabela e manda as ações (ActionData: a ação, o aparelho, o valor) para as filas (ActionQueueManager::
// FeedQueue). O carro lê as dele em PBondCar::GetControllerInput (game/bond_car.h).
//
// - Os escalares (PS2PadDevice::Initialize): 0-3 os direcionais analógicos ALX, ALY, ARX, ARY (tipo 0: de -1 a 1,
//   y para cima, a zona morta de +-0,3 zerada); 4-15 os botões analógicos (tipo 1, a pressão: x 1/192, até 1) na ordem
//   da libpad (direita, esquerda, cima, baixo, triângulo, bola, X, quadrado, L1, R1, L2, R2); 16-31 os digitais (tipo 2:
//   0 ou 1) na ordem dos bits (L2, R2, L1, R1, triângulo, bola, X, quadrado, Select, L3, R3, Start, cima, direita, baixo,
//   esquerda). Um controle digital (o tipo 4 da libpad) copia os digitais para os analógicos.
// - Uma linha do .def: "AÇÃO BOTÃO [MODO] [LIMIAR]" (separados por tabulação ou espaço; a primeira linha é o cabeçalho;
//   para em "FRONTEND" ou "END"). Os modos: U (o padrão: o valor quando muda), + e - (as passagens para baixo e para
//   cima: 0,5 nos analógicos, -0,5/0,5 nos direcionais, 1/0 nos digitais), > e < (as passagens pelo limiar, 0,5 se não
//   vier), = (a volta ao centro, +-0,2), I e N (com e sem o eixo invertido do jogo), R e M (a repetição: na descida, e
//   depois de 300 ms e a cada 150 ms com o botão seguro; M o mesmo na subida).
// - Peculiaridades mantidas: os modos que não são R/M só mandam quando o escalar mudou (salvo o "mandar tudo" do
//   IOModule); a ação 0x73 (ACTION_INPUTSYNC, valor 0) fecha cada passagem; o modo "=" usa 0,2 fixo.
// - Divergências (memória inválida no PS2, contadas em input_guards()): um botão que a tabela não conhece (o PS2 grava
//   antes do vetor, índice -1) e uma linha sem o '\r' (o PS2 copia até um ponteiro nulo) são pulados.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace drv {

// Os dados do controle como o PAD_getdataptr da EA os dá: +0xc os botões (1 = apertado), +0x10..+0x13 os direcionais
// (RX, RY, LX, LY: 0-255, centro 128), +0x14 as 12 pressões.
struct PadData {
    uint16_t buttons = 0;
    uint8_t rx = 128, ry = 128, lx = 128, ly = 128;
    uint8_t pressure[12] = {};
    int pad_type = 7;  // PAD_getpadtype: 7 o DUALSHOCK 2 (analógico), 4 o digital
};

constexpr int kPadScalars = 32;
const char* pad_scalar_name(int i);
// O tipo do escalar: 0 direcional, 1 botão analógico, 2 digital.
int pad_scalar_type(int i);

// [driving] 0x00169218 PS2ScaleJoystick
float PS2ScaleJoystick(uint8_t v);
// [driving] 0x00169248 PS2ScaleAnalogButton
float PS2ScaleAnalogButton(uint8_t v);
// [driving] 0x00167c48 InputDevice::DeadZoneChop
void DeadZoneChop(float lo, float hi, float& v);

// O PS2PadDevice (0x89c bytes no PS2): os valores de agora e os do quadro anterior, o relógio e o estado da repetição de
// cada escalar.
struct PadDevice {
    int32_t slot = 0;  // +0x14: o aparelho que as ações levam
    float prev[kPadScalars] = {};
    float cur[kPadScalars] = {};
    int32_t timer[kPadScalars] = {};
    int32_t state[kPadScalars] = {};
    // [driving] 0x00169288 PS2PadDevice::PollDevice
    void Poll(const PadData& pad);
};

// [driving] 0x001677f8 DeviceScalar::isDown (e os outros: 0x00167878 isUp, 0x001678f8 isDownTransition, 0x001679c8
// isUpTransition, 0x00167a98 isDownTransitionThreshold, 0x00167ad8 isUpTransitionThreshold, 0x00167b18
// isCenteredTransition)
bool scalar_is_down(int type, float cur);
bool scalar_is_up(int type, float cur);
bool scalar_down_transition(int type, float prev, float cur);
bool scalar_up_transition(int type, float prev, float cur);
bool scalar_down_threshold(float t, float prev, float cur);
bool scalar_up_threshold(float t, float prev, float cur);
bool scalar_centered(float t, float prev, float cur);

// Uma linha da tabela (InputTableEntry, 0xc bytes): o modo, o limiar, a ação.
struct InputEntry {
    int32_t method = 0;
    float threshold = 0.0f;
    int32_t action = 0;
};

// [driving] 0x001651a0 getActionID: o número da ação pelo nome (os 117 nomes do jogo); 0x75 se não existe.
int32_t getActionID(const char* name);
const char* action_name(int32_t id);

// AdvanceFilePtr (0x00167498, definido em input_config.cpp): depois do próximo '\n' (nulo se não há); o
// LoadParseDefFile e o InputConfigManager o usam.
const char* AdvanceFilePtr(const char* p);

// O InputToAction: uma tabela por escalar.
struct InputMap {
    std::vector<InputEntry> table[kPadScalars];
    // [driving] 0x001674d0 InputToAction::LoadParseDefFile (com 0x00167498 AdvanceFilePtr e 0x00167120
    // InputTable::AddTableEntry): acrescenta as linhas do texto (que precisa terminar a última linha com "\r\n").
    void LoadParseDefFile(const std::string& text);
    // [driving] 0x00167678 InputToAction::Reset
    void Reset();
};

// O que o SendGameMessages lê do IOModule.
struct IOModuleState {
    int32_t send_all = 0;      // +0x04: manda os modos U..I mesmo sem mudança (o UpdateAllDevices zera depois)
    int32_t now = 0;           // +0xa0: o TIMER_gettick da passagem
    int32_t first_delay = 0;   // +0xa8: 300 ms em ticks do TIMER
    int32_t repeat_delay = 0;  // +0xac: 150 ms
    bool inverted = false;     // InputConfigManager::IsInverted
};

using InputFeed = std::function<void(int32_t action, int32_t device, float value)>;

// [driving] 0x00168678 IOModule::SendGameMessages
void SendGameMessages(IOModuleState& io, PadDevice& dev, const InputMap& map, const InputFeed& feed);

uint64_t input_guards();

}  // namespace drv
