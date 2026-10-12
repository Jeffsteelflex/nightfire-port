#pragma once
// Construtores estáticos do ACTION.ELF. No PS2, o crt0 chama __main antes do main, e ele roda
// __do_global_ctors: cada função de __CTOR_LIST__ (_GLOBAL_$I$...) chama um
// __static_initialization_and_destruction_0, que monta tabelas na RAM (weapon_data, por exemplo,
// que não está pronta no .data do executável). O port roda esse mesmo código do ELF do usuário no
// interpretador MIPS (data/mips_interp.h) e copia as tabelas resultantes para as globais do motor,
// por nome de símbolo; nada do jogo fica no repositório.

#include <cstdint>
#include <string>
#include <vector>

namespace nf {

class Elf;
class MipsMachine;

// Ponteiro de código do PS2 numa tabela copiada -> função equivalente do port (o nome é o símbolo
// do ELF); 0 quando ela ainda não foi portada (o motor trata como "não portada").
using CodeResolver = uint32_t (*)(const char* symbol_name, uint32_t ps2_address);

// Funções de __CTOR_LIST__ na ordem em que __do_global_ctors as chama (da última para a primeira).
std::vector<uint32_t> static_constructors(const Elf& elf);

// Máquina pronta para os construtores: a imagem do ELF, o código que pode ser interpretado
// (__do_global_ctors, _GLOBAL_$I$*, __static_initialization_and_destruction_0) e as funções que
// eles chamam, nativas: memset, memcpy e PS2Sinf (a versão portada do motor).
void setup_constructor_machine(MipsMachine& m, const Elf& elf);

// Roda __do_global_ctors do ELF no interpretador e copia as tabelas que o port usa (weapon_data,
// ammo_data) para as globais do motor, conferindo o tamanho de cada símbolo. Os ponteiros de código
// das tabelas (weapon_data +0x104) passam por resolve_code; o log recebe cada tradução e qualquer
// outra palavra copiada que aponte para dentro da imagem do ELF. Falha (false + erro) se o ELF não
// tiver o que se espera ou se o interpretador parar.
bool init_game_tables(const Elf& elf, CodeResolver resolve_code, std::string* error,
                      std::vector<std::string>* log = nullptr);

// Diagnóstico: o que cada construtor escreve quando roda sozinho sobre a imagem inicial.
struct ConstructorEffect {
    uint32_t address = 0;
    std::string name;
    struct Write {
        std::string symbol;  // objeto que contém os bytes ("+0x..." quando é depois de um símbolo sem tamanho)
        uint32_t bytes = 0;  // bytes alterados
    };
    std::vector<Write> writes;
    std::string error;  // vazio se rodou até o fim
};
std::vector<ConstructorEffect> survey_static_constructors(const Elf& elf);

}  // namespace nf
