#pragma once
// Os eventos do jogo (os E* do DRIVING.ELF, DTuningFile.obj e vizinhos; o EventManager de Bond.obj; docs/driving.md
// seções 18 e 19).
//
// No PS2 um evento é um objeto de 4 a 0x3c bytes (a tabela virtual e os campos) alocado num anel de 32 KB (o
// Event::operator_new, 0x175730) pela função criadora do seu número (a tabela 0x36fda0, RegisterEvent::LookupEvent), que
// lê os dados do evento na lista 'el' e o EventDynamicData (0x32e708: quem disparou, o gatilho) e chama o construtor.
// O trabalho fica no destrutor, que o EventManager::RunEvents (0x175690) chama na ordem, no fim de cada passagem do
// escalonador; alguns construtores já agem (o EChangeStage muda a etapa, o ESetTimer, o EStartTimer e o EStopTimer
// mexem nos cronômetros). As listas vêm dos gatilhos (WTrigger::FireEvents) e das regras da missão
// (SMissionRule::RunEvents).
//
// Port: a imagem do objeto do PS2 (a tabela virtual do PS2 e os campos, kEventTable: o tamanho, a tabela virtual e de
// onde vem cada palavra, lidos da criadora e do construtor de cada número) fica no PendingEvent (driving/game/
// triggers.h) para a parte 15 do nf_driving_check comparar com o anel do original; os destrutores portados rodam em
// events_run(); os outros números são contados (event_stats()) e não fazem nada. A tabela traz também as palavras dos
// dados que a função de resolução do 'el' (a tabela 0x32bc20) troca por endereços (o CARP::TagReference).

#include <cstdint>
#include <map>

namespace drv {

// De onde vem uma palavra do objeto: dos dados do evento (kFromData) ou do EventDynamicData (kFromDynamic).
enum : uint8_t { kFromData = 1, kFromDynamic = 2 };
struct EventField {
    uint8_t offset;   // no objeto
    uint8_t source;
    uint8_t from;     // o deslocamento na origem
};
struct EventInfo {
    int32_t type;
    const char* name;
    uint32_t size;       // o operator new
    uint32_t vtable;     // a tabela virtual do PS2 (a da classe; o destrutor a troca pela de Event no fim)
    uint32_t creator;    // a função criadora do PS2 (a tabela 0x36fda0)
    uint32_t resolver;   // a função de resolução do 'el' (a tabela 0x32bc20; 0 nenhuma)
    uint32_t refs;       // bit k: a palavra k dos dados passa pelo CARP::TagReference
    EventField fields[20];
};
// [driving] 0x001757b8 RegisterEvent::LookupEvent: o número tem criadora (a tabela 0x36fda0, 281 entradas).
bool event_has_creator(int32_t type);
// A informação do número (nula: um número que Paris não usa e o port não conhece).
const EventInfo* event_info(int32_t type);

// Os números com nome no port.
enum EventType : int32_t {
    kEventEndMission = 19,
    kEventStartMission = 22,
    kEventSpawnTraffic = 28,
    kEventWakeupSmackable = 35,
    kEventCallStage = 40,
    kEventChangeStage = 41,
    kEventDisplayText = 44,
    kEventPlayerLose = 45,
    kEventPlayerWin = 46,
    kEventSetTimer = 47,
    kEventStartTimer = 48,
    kEventStopTimer = 49,
    kEventDisableTrigger = 53,
    kEventEnableTrigger = 54,
    kEventEndCameraAnim = 68,
    kEventSetObjective = 69,
    kEventRestart = 73,
    kEventLetterBoxOff = 103,
    kEventLetterBoxOn = 104,
    kEventSetSimRate = 112,
    kEventTwoWheelsOff = 125,
    kEventAddScoreAction = 131,
    kEventResetPlayerCarPos = 140,
    kEventDamagePlayer = 162,
    kEventFireTriggerSpeed = 171,
    kEventFireTriggerSpeedLess = 172,
    kEventButtonMsgOff = 193,
    kEventButtonMsgOn = 194,
    kEventSetMagicCounter = 200,
    kEventFireRandomTrigger = 203,
    kEventEndCarStop = 219,
    kEventForceCarStop = 220,
    kEventObjectiveAdd = 225,
    kEventObjectivePass = 228,
    kEventObjectiveSetCurrent = 229,
    kEventSetAutoDriveCamOffMovie = 233,
    kEventSetAutoDriveCamOnMovie = 234,
    kEventAwardHitToPlayer = 275,
    kEventAwardKillToPlayer = 276,
};

// O que o port conta: os criados e os executados por número, os que o port não executa.
struct EventStats {
    std::map<int32_t, uint64_t> created, run, skipped;
    uint64_t constructor_effects = 0;  // os construtores que agem (EChangeStage, os cronômetros)
    uint64_t guards = 0;
};
EventStats& event_stats();

// Cria o evento `type` com os dados `data` (na lista 'el') e o EventDynamicData corrente (trigger_manager()): a função
// criadora (a imagem do objeto) e o construtor (os efeitos dele); o evento entra na fila (trigger_manager().pending).
void event_create(int32_t type, const uint8_t* data);
// Um evento sem dados criado pelo código (o ERestart do SMissionManager::Update e do EPlayerLose).
void event_create_simple(int32_t type);
// [driving] 0x00175690 EventManager::RunEvents: os destrutores dos eventos da fila, na ordem (os criados enquanto eles
// rodam também); a fila fica vazia.
void events_run();

// O destrutor de um evento já criado (a parte 15 do nf_driving_check o chama sozinho).
struct PendingEvent;
void event_execute(const PendingEvent& e);

}  // namespace drv
