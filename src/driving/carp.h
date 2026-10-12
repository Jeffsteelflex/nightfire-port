#pragma once
// O contêiner CARP (.crp, "PRAC" = 'CARP' little-endian; docs/driving.md seções 5 e 10): carros, armas e pistas.
//
// É um UGroup serializado (a biblioteca "realmem" do jogo: UGroup::GetArray 0x002d6940, GroupLocateTag 0x002d51f0,
// DataLocateTag 0x002d5a10, DataLocateFirst 0x002d5920), lido aqui como o jogo o percorre:
//  - um grupo é um cabeçalho de 16 bytes {u32 etiqueta, u32 bandeiras, u32 contagem, u32 posição}; o arquivo começa com
//    o grupo raiz ('CARP'). O vetor do grupo fica em `grupo + posição x 16` com o bit 1 das bandeiras, senão na posição
//    absoluta; nele vêm primeiro `bandeiras >> 5` cabeçalhos de subgrupos e depois `contagem` entradas de dados (o bit
//    0x10 diz que os subgrupos estão ordenados pela etiqueta, o 0x08 que os dados estão; a busca do jogo é binária
//    nesse caso, linear senão: o resultado é o mesmo);
//  - uma entrada de dados é {u32 etiqueta, u32 bandeiras, u32 contagem, u32 posição}: o tamanho em bytes é
//    `bandeiras >> 8`; os dados ficam em `entrada + posição` com o bit 1, senão na posição absoluta; com o bit 0 a
//    etiqueta é curta, duas letras e um índice de 16 bits ('as' 0x6173 << 16 | i: as famílias numeradas; o jogo as
//    compara com `etiqueta & 0xffff0000 | 0x2020`, "as  ").
// Um carro (vanquish.crp): a raiz tem dois grupos ('Arti' com 280 dados, 'Shar' vazio) e quatro dados: 'ELFd' e 'ELFr'
// (o modelo do EAGL partido em dois, eagl_loader.h), 'Sect' e 'sn#0000' (o nome do .ssh). O 'Arti' (o objeto
// articulado) tem 'Base', 'Cams', 'Name', 'Skel' (os ossos: capô, cubos e rodas, retrovisores, porta-malas), as
// instâncias 'in#0000', as peças 'as', os nomes 'sr', as animações 'an'/'aq', 'el', 'ef', 'vd', 'cs', 'si'.

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

struct CarpEntry {
    uint32_t tag = 0;
    uint32_t flags = 0;
    uint32_t count = 0;
    uint32_t offset = 0;
    size_t pos = 0;   // posição da entrada no arquivo
    size_t data = 0;  // posição dos dados no arquivo (bit 1: relativa à entrada)
    uint32_t size() const { return flags >> 8; }
    bool short_tag() const { return (flags & 1) != 0; }
    std::string tag_name() const;  // "ELFd", ou "sn#0001" para as famílias numeradas
};

struct CarpGroup {
    uint32_t tag = 0, flags = 0, count = 0, offset = 0;
    size_t pos = 0;    // o cabeçalho do grupo no arquivo
    size_t array = 0;  // o vetor (UGroup::GetArray)
    std::vector<CarpGroup> groups;
    std::vector<CarpEntry> data;
    // A busca do jogo por etiqueta inteira (as famílias curtas também: 0x61730019 = as#0019).
    const CarpGroup* group(uint32_t tag) const;   // UGroup::GroupLocateTag
    const CarpEntry* find(uint32_t tag) const;    // UGroup::DataLocateTag
    std::string tag_name() const;
};

struct Carp {
    CarpGroup root;
    std::vector<CarpEntry> entries;  // todos os dados, a raiz primeiro e depois cada grupo em profundidade
    const CarpEntry* find(uint32_t tag) const { return root.find(tag); }
    const CarpGroup* group(uint32_t tag) const { return root.group(tag); }
};

constexpr uint32_t carp_tag(const char t[5]) {
    return uint32_t(uint8_t(t[0])) << 24 | uint32_t(uint8_t(t[1])) << 16 | uint32_t(uint8_t(t[2])) << 8 | uint8_t(t[3]);
}
// A etiqueta curta de uma família numerada ("as", 0x19 -> 0x61730019).
constexpr uint32_t carp_short_tag(const char t[3], uint32_t index) {
    return uint32_t(uint8_t(t[0])) << 24 | uint32_t(uint8_t(t[1])) << 16 | (index & 0xFFFF);
}

bool carp_parse(const uint8_t* data, size_t size, Carp& out);

// Os dados de uma entrada como texto (as 'sr': "EAGL::vanquish::{as  0000}::Model[0].00").
std::string carp_string(const uint8_t* file, size_t file_size, const CarpEntry& e);

// Uma instância de um objeto articulado (o CARP::Instance: um registro de 64 bytes das entradas 'in' do grupo, que o
// RSceneObj::UseInstanceList percorre de 0x40 em 0x40): as linhas 0-2 são a rotação e a linha 3 a translação
// (p' = p x M); os w guardam as bandeiras (linha 0: o byte 0 tem 0x80 nas cópias espelhadas, o byte 1 0xff/0xfe/0xef, os
// bytes 2-3 o osso do 'Skel' (1 = capô ... 10 = porta-malas) ou o ponteiro do painel), a etiqueta da peça 'as' (linha
// 1) e duas palavras ainda não lidas (linhas 2 e 3). O registro 'as' (40 bytes) tem na palavra 9 a etiqueta 'sr' do nome
// do __Model; os de 32 bytes não têm modelo (luzes).
struct CarpInstance {
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    uint32_t flags = 0, piece = 0, extra[2] = {0, 0};
    std::string model;  // o nome do símbolo sem o "EAGL::" ("vanquish::{as  0000}::Model[0].00"); vazio sem modelo
};
std::vector<CarpInstance> carp_instances(const uint8_t* file, size_t file_size, const CarpGroup& object);

// As listas de desenho por vista ('vd' | vista: u16 índices de instância; RSceneObj::GetViewDrawList): [vista].
std::vector<std::vector<uint16_t>> carp_view_lists(const uint8_t* file, size_t file_size, const CarpGroup& object);

}  // namespace drv
