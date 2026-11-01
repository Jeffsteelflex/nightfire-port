#pragma once
// Os destroços (docs/driving.md seção 29): o RDebris e o RDebrisType (RDebris.obj; RDebris::fgThis 0x334a00), as trilhas
// 'gd' da galeria (GFXGallery::DEBRIS_SetState e DEBRIS_Draw) e o render method BondDebris (bondrm.o).
//
// Como o PS2 faz: o RDebris tem 23 tipos (a tabela 0x338e28, cinco palavras por tipo: as posições e os UV do modelo, um
// SplitReg1, o número de vértices e a etiqueta da textura no contexto 0: 'dmge', 'pipe', 'dgls', 'shel', 'coin', 'mail',
// 'grbg', 'frut', 'wdsp'); o construtor (com o 0x001d93e8, que escala o modelo do tipo 4, o cartucho, e faz o do tipo 22
// com o do 16 vezes 2,5) cria um RDebrisType de 0xe00 bytes por tipo: até 64 pedaços, cada um com a posição, a
// velocidade (o w o passo em que nasceu), o quatérnio (um eixo sorteado pelo RRandom do render e a velocidade de giro
// no w) e a cor. As trilhas 'gd' da galeria lançam os pedaços (DEBRIS_Draw: com o gerador do tremor do GSystem, a
// posição e a velocidade espalhadas pela chave e giradas pelo quatérnio da tela; o modo 0 é um cartucho só, o tipo 4).
// A cada quadro desenhado o RDebris::Draw (no DrawEffects, todos os tipos menos o 4) tira os pedaços com mais de 70
// passos, baixa o alfa (vezes (agora - 70) / nascimento, a conta do original) e manda o lote ao EAGL::DynamicModel: o
// BondDebris faz na VU1 o movimento inteiro de cada pedaço (o giro pelo ESIN do ângulo, a posição + v t, a gravidade do
// RTimeData (-0,002) vezes t², a matriz do quatérnio vezes a viewProj) e desenha o modelo do tipo (uma faixa de
// triângulos com textura, a cor do pedaço, Gouraud, a mistura (Cs - Cd) As + Cd).
//
// Divergências (só do port, também nas funções): o modelo de cada tipo vem do DRIVING.ELF do jogador (as posições e os
// UV da tabela 0x338e28, com o 0x001d93e8 aplicado; o port não os guarda); o EAGL::DynamicModel::Draw vira um registro
// para o PC (DebrisDraw) e a conta da VU1 é feita na CPU (bond_debris_vertices); o lixo da pilha que o DEBRIS_Draw passa
// no w da velocidade não existe aqui (o RDebrisType::Add o sobrescreve).

#include <cstdint>
#include <functional>
#include <vector>

namespace drv {

constexpr int kDebrisTypes = 23, kDebrisMax = 64;

// O modelo de um tipo (a tabela 0x338e28 lida do ELF, com o 0x001d93e8 aplicado).
struct DebrisGeometry {
    std::vector<float> coords;  // count x 4 (o w lido também)
    std::vector<float> uv;      // count x 2
    uint32_t split = 0;         // o SplitReg1 (o terceiro campo: um ponteiro para uma palavra; o port guarda a palavra)
    int32_t count = 0;
    uint32_t tag = 0;           // a etiqueta da textura (4 letras, a do disco)
};
// Lê os 23 modelos de um DRIVING.ELF (`read` copia n bytes do endereço; falso fora do arquivo) e aplica o 0x001d93e8.
bool debris_load_geometry(const std::function<bool(uint32_t addr, void* out, uint32_t n)>& read,
                          std::vector<DebrisGeometry>& out);
// [driving] 0x001d93e8 (do construtor do RDebris): o tipo 4 = os 26 vértices de 0x334a10 vezes o float de 0x334bb0, o
// tipo 22 = o 16 vezes 2,5 (só o xyz).
void RDebris_PrepareGeometry(std::vector<DebrisGeometry>& g, const float shell_base[26][4], float shell_scale);

// O RDebrisType (0xe00 bytes no PS2; a parte de dados com o leiaute do PS2).
struct RDebrisTypeData {
    float offsets[kDebrisMax][4];   // +0x000
    float velocity[kDebrisMax][4];  // +0x400 (o w: o passo em que nasceu)
    float quats[kDebrisMax][4];     // +0x800 (o eixo e a velocidade do giro)
    uint32_t colours[kDebrisMax];   // +0xc00 (as cores do GS: o R e o B trocados, metade)
    uint32_t control[4];            // +0xd00 o ControlData (+0xd04 o número de pedaços)
    float sim_w;                    // +0xd10 o f12 do último Add (vai ao w do SimTick no desenho)
    uint32_t life;                  // +0xd14 0x46
    uint32_t count;                 // +0xd18
    uint32_t unknownd1c;
};
static_assert(sizeof(RDebrisTypeData) == 0xd20);
struct RDebrisType {
    RDebrisTypeData d{};
    uint8_t state[0x80] = {};       // +0xd20 o GeoPrimState
    int index = 0;                  // o tipo (a camada +0xda0: o modelo, a textura)
    uint32_t texture_flags = 0;     // o +0x10 da textura (o TFX limpo)
};
struct RDebris {
    RDebrisType types[kDebrisTypes];
    uint32_t unknown5c = 0;         // +0x5c
};
RDebris& debris();

// O que o EAGL::DynamicModel::Draw de um tipo manda à VU1 (os parâmetros do BondDebris).
struct DebrisDraw {
    int type = 0;
    int count = 0;                  // os pedaços
    float simtick[4] = {};          // o RTimeData::fTimeData com o w = sim_w
    std::vector<float> offsets, velocity, quats;  // count x 4
    std::vector<uint32_t> colours;
    uint64_t alpha = 0, test = 0;
};

// [driving] 0x001d9628 RDebrisType::RDebrisType
void RDebrisType_Construct(RDebrisType& t);
// [driving] 0x001d9720 RDebrisType::Init(int tipo)
void RDebrisType_Init(RDebrisType& t, int type);
// [driving] 0x001d9bb8 RDebrisType::Add(COORD4 &pos, COORD4 &vel, unsigned cor, unsigned passo) (f12: o sim_w)
void RDebrisType_Add(RDebrisType& t, float sim_w, const float pos[4], const float vel[4], uint32_t colour, uint32_t time);
// [driving] 0x001d9920 RDebrisType::Draw (`now` o RTimeData::fTimeData.x; `out` o lote, nulo: só anda)
void RDebrisType_Draw(RDebrisType& t, const float time_data[4], std::vector<DebrisDraw>* out);
// [driving] 0x001d94a8 RDebris::RDebris
void RDebris_Construct(RDebris& r);
// [driving] 0x001d9560 RDebris::Draw (os tipos menos o 4)
void RDebris_Draw(RDebris& r, const float time_data[4], std::vector<DebrisDraw>* out);
// [driving] 0x001d95c0 RDebris::Reset
void RDebris_Reset(RDebris& r);
// [driving] 0x001d95e8 RDebris::Add(DebrisGeometries, COORD4 &, COORD4 &, unsigned cor, bool) (f12 vai ao tipo; o
// passo é o RTimeData::fTime)
void RDebris_Add(RDebris& r, float sim_w, int32_t type, const float pos[4], const float vel[4], uint32_t colour,
                 uint32_t enable, uint32_t time);

// [driving] 0x00235808 GFXGallery::DEBRIS_SetState (vazio no PS2)
void GFXGallery_DEBRIS_SetState();
// [driving] 0x00235810 GFXGallery::DEBRIS_Draw(UGroup *, Debris *chave, COORD4 *pos, quaternion *, COORD4 *vel, int):
// `key` a chave da trilha (os dados em +0x30).
void GFXGallery_DEBRIS_Draw(const uint8_t* key, const float pos[4], const float quat[4], const float* vel, uint32_t time);
// [driving] 0x002cbc50 (sem nome: o mesmo código do VU0_v3quatrotate, noutra cópia)
void VU0_quatrotate_copy(const float q[4], const float v[4], float out[4]);

// Os vértices de um pedaço como o BondDebris os calcula (a matriz do pedaço na VU1: o giro, a posição, a gravidade),
// em mundo (para o desenho do PC): count x 4 posições e o UV; `m` recebe a matriz do pedaço (linhas, sem a viewProj).
void bond_debris_instance_matrix(const float simtick[4], const float offset[4], const float velocity[4],
                                 const float quat[4], float m[4][4]);

struct DebrisStats {
    uint64_t added = 0, full = 0, expired = 0, drawn = 0, pieces = 0, gallery = 0, gallery_single = 0, skipped = 0;
};
DebrisStats& debris_stats();

}  // namespace drv
