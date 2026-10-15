#pragma once
// As configurações do controle nas missões de carro: o InputConfigManager do DRIVING.ELF (actionqueuemgr.obj).
// Portado do MIPS e conferido contra o original no nf_driving_check (parte 12, `--so carro`). docs/driving.md seção 16.
//
// O data\Control\Master.def lista as configurações por tipo: "Driving N" (o carro) e "POV N" (a vista de dentro), cada
// cabeçalho seguido dos N nomes de .def, até "End". InitAndPreload (chamado pelo Bond_StartUpSystem):
//  - ParseMasterConfigFile monta as listas (ConfigInfo, 0x5c bytes: o nome, o texto carregado, 14 números de texto);
//  - carrega o data\Control\FrontEnd.def (a base) e o .def de cada configuração;
//  - ParseDefFileForFrontEnd lê de cada .def o que vem depois de "FRONTEND" ("ButtonCross 2706"...: o texto de cada
//    botão no menu do controle, pelos 14 nomes da tabela 0x32e540);
//  - escolhe a configuração de cada tipo pelo bloco da troca (UberBond +0xa14 a do carro, +0x98c a do POV, +0xa24 o
//    eixo invertido); uma fora da lista vira 0 com o aviso "Uberbond passed in invalid control config, setting to 0";
//  - SetConfig monta a tabela do IOModule da porta do controle: InputToAction::Reset, o FrontEnd.def e o .def da
//    configuração do carro (as ações do menu ficam na mesma tabela que as do carro).
// Divergência (memória inválida no PS2, contada em guards): um nome de botão fora da tabela no FRONTEND (o PS2 gravaria o
// número no ponteiro do texto, o +0x20 da configuração) e um nome de .def antes de qualquer cabeçalho (o PS2 gravaria
// pelo índice do tipo 2, fora das duas listas) são pulados.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "driving/game/input.h"

namespace drv {

// Uma configuração (ConfigInfo, 0x5c bytes).
struct InputConfigInfo {
    char name[0x20] = {};      // +0x00
    uint32_t file = 0;         // +0x20 (p32 do texto carregado; no port o índice em InputConfigManager::texts + 1)
    int32_t locale[14] = {};   // +0x24 os números de texto do menu, pelos nomes da tabela 0x32e540
};
static_assert(sizeof(InputConfigInfo) == 0x5c);

// O InputConfigManager (0x30 bytes no .bss 0x3c8780).
struct InputConfigManagerData {
    int32_t initialized = 0;   // +0x00
    int32_t type = 2;          // +0x04 o tipo corrente (0 o carro, 1 o POV)
    int32_t current[2] = {};   // +0x08 a configuração escolhida de cada tipo
    int32_t count[2] = {};     // +0x10
    uint32_t configs[2] = {};  // +0x18 (p32 ConfigInfo[])
    int32_t inverted = 0;      // +0x20
    uint32_t names = 0;        // +0x24 (o StringToNumber dos nomes, só durante a carga)
    uint32_t frontend = 0;     // +0x28 o texto do FrontEnd.def
    uint32_t unknown2c = 0;    // +0x2c
};
static_assert(sizeof(InputConfigManagerData) == 0x30);

class InputConfigManager {
public:
    InputConfigManagerData d;
    std::vector<InputConfigInfo> configs[2];
    std::vector<std::string> texts;  // os textos carregados (o .def de cada configuração, o FrontEnd.def)
    std::vector<std::string> warnings;
    uint64_t guards = 0;
    // [driving] 0x00166508 InputConfigManager::InputConfigManager
    InputConfigManager();
};

// O que InitAndPreload lê do bloco da troca (UberBond, 0x100000).
struct InputUberBond {
    int32_t driving_config = 0;  // +0xa14
    int32_t pov_config = 0;      // +0x98c
    int32_t inverted = 0;        // +0xa24
};

// O carregador de arquivos (UFileLoader::FileLoad): o texto do arquivo do .VIV da missão ("data/Control/..."); vazio
// se não existe (o PS2 devolveria nulo).
using InputFileLoader = std::function<std::string(const std::string& path)>;

// [driving] 0x00166548 InputConfigManager::ParseMasterConfigFile
void InputConfigManager_ParseMasterConfigFile(InputConfigManager& m, const std::string& text);
// [driving] 0x00166788 InputConfigManager::ParseDefFileForFrontEnd
void InputConfigManager_ParseDefFileForFrontEnd(InputConfigManager& m, InputConfigInfo& info, const std::string& text);
// [driving] 0x001668a0 InputConfigManager::InitAndPreload, com o SetConfig no fim (`map`: o InputToAction da porta).
void InputConfigManager_InitAndPreload(InputConfigManager& m, const InputFileLoader& load, InputUberBond& uber,
                                       InputMap& map);
// [driving] 0x00166f28 InputConfigManager::SetConfig(char *): Reset, o FrontEnd.def e o texto dado (vazio: nenhum).
void InputConfigManager_SetConfig(InputConfigManager& m, const std::string* text, InputMap& map);
// [driving] 0x00166cb0 InputConfigManager::SetConfig(ConfigType, int): muda o tipo (e a configuração, se não -1).
void InputConfigManager_SetConfigType(InputConfigManager& m, int32_t type, int32_t config, InputUberBond& uber,
                                      InputMap& map);
// [driving] 0x00166d80 GetNumConfigs e 0x00166d90 GetCurrentConfig (2: a do tipo corrente).
int32_t InputConfigManager_GetNumConfigs(const InputConfigManager& m, int32_t type);
int32_t InputConfigManager_GetCurrentConfig(const InputConfigManager& m, int32_t type);
// Os 14 nomes da tabela 0x32e540 (ButtonCross ... TITLE) e o número de cada um (StringToNumber); -1 fora.
int32_t input_locale_index(const char* name);

}  // namespace drv
