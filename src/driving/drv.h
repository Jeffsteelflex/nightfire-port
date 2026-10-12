#pragma once
// As missões de carro: o porte do DRIVING.ELF, o motor da EA (docs/driving.md).
//
// Convenções do módulo:
//  - todo o código do DRIVING.ELF fica no namespace drv: o motor da Eurocom (src/engine) tem nomes iguais (Mat_*, Rand,
//    o heap, RigidBody...). A biblioteca nf_drv não depende de nf_engine; pode usar src/platform e src/core;
//  - cada função portada leva `// [driving] 0x<endereço no DRIVING.ELF> <Nome>` logo acima da definição, com o nome do
//    DRIVING.SYM / do projeto out/ghidra_driving (tools/ghidra_driving.sh);
//  - a verdade é o PS2: o DRIVING.ELF e os dados de DRIVING/ do disco americano. A versão de Xbox (out/ghidra_xbox,
//    tools/xbox_match.py, docs/driving.md seção 9) serve só para entender o algoritmo, nunca para copiar valores;
//  - os arquivos .cpp de src/driving entram sozinhos no CMake (GLOB), e os testes ficam em tests/test_driving_*.cpp,
//    que também entram sozinhos no nf_tests;
//  - o PS2 roda o DRIVING.ELF num processo novo a cada missão; aqui o código vive numa biblioteca à parte, e a escolha
//    entre DLL e segundo processo fica para a fase 4 (docs/driving.md seção 6).

namespace drv {

// O nome do módulo (para relatórios e testes).
const char* module_name();

}  // namespace drv
