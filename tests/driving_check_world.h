#pragma once
// nf_driving_check, parte 5: o chão das pistas (WCollisionMgr, WGrid, WWorldPos) do código ORIGINAL do EE no
// interpretador MIPS, sobre a colisão de verdade das pistas do disco, lado a lado com o porte (tests/driving_check_world.cpp).

#include <cstdint>
#include <map>
#include <string>

namespace nf {
class Elf;
class DiscSource;
class MipsMachine;
}  // namespace nf
namespace drv {
class TrackWorld;
struct WWorldPos;
}  // namespace drv

namespace drvcheck {

// A cópia do mundo de colisão de uma pista na RAM emulada (as instâncias, os artigos e os nós, com os ponteiros
// trocados pelos endereços emulados), para o código original consultar o chão (também a parte 6, o carro).
struct WorldCopy {
    uint32_t mgr = 0, grid = 0;
    uint32_t insts = 0, count = 0;
    std::map<uint32_t, uint32_t> art_emu;   // p32 do port -> endereço emulado
    std::map<uint32_t, uint32_t> art_port;  // o contrário
    uint32_t objs = 0;                       // os objetos ('co')
    uint32_t render = 0;                     // as instâncias de render (WWorld +0x20)
    uint32_t bytes = 0;
};
// Copia o mundo para begin..end, o gerente para mgr_addr e a grade para grid_addr, e aponta fgCollisionMgr e fgGrid.
bool copy_world(nf::MipsMachine& m, drv::TrackWorld& tw, WorldCopy& wc, uint32_t mgr_addr, uint32_t grid_addr,
                uint32_t begin, uint32_t end, std::string& error);
void put(nf::MipsMachine& m, uint32_t addr, const void* src, uint32_t n);
// Um WWorldPos do port na RAM emulada (os ponteiros trocados) e o da RAM emulada no espaço do port.
void wp_to_emu(const drv::WWorldPos& wp, const drv::TrackWorld& tw, const WorldCopy& wc, uint8_t out[0x40]);
drv::WWorldPos wp_from_emu(const uint8_t in[0x40], const drv::TrackWorld& tw, const WorldCopy& wc);

// Devolve o número de comparações que falharam (0: tudo bit a bit). `cases`: consultas de cada tipo por pista.
int run_world_checks(const nf::Elf& elf, const nf::DiscSource& disc, int cases, uint32_t seed);

}  // namespace drvcheck
