#pragma once
// A conferência da família MatCar contra o microprograma original (docs/driving.md seção 10): o interpretador da VU1
// (platform/vu_interp.h, a aritmética do PS2) roda o código do render method sobre a memória da VU de um lote, como o
// MSCNT a deixaria, e os pacotes de vértices que ele chuta (XGKICK) são comparados com car_reference, valor a valor.
//
// O que não deve bater (as divergências documentadas em mat_car.h): o ADC que o recorte põe nos triângulos fora da tela
// (contado à parte, `adc_recorte`); e o último bit das contas em float (a VU trunca, sem denormais; a referência usa o
// IEEE do PC), por isso o STQ é comparado em ulps ou em valor absoluto, o X e o Y (FTOI4) com tolerância de 1 e o Z (perto de
// 2^24 x 16) com tolerância relativa.

#include <cstdint>
#include <string>
#include <vector>

#include "driving/mat_car.h"
#include "driving/vif.h"

namespace drv {

struct CarVerifyStats {
    int batches = 0, failed_runs = 0, missing_kicks = 0;
    int vertices = 0;           // vértices comparados (somando as passadas)
    int stq_diff = 0;           // componentes de STQ a mais de 4 ulps e de 2^-16 em valor absoluto
    int rgba_diff = 0;          // componentes de cor diferentes
    int xyz_diff = 0;           // X ou Y (FTOI4) a mais de 1, Z a mais de 2^-21 do valor
    int fog_diff = 0;           // névoa (o w sem o ADC) a mais de 16 (1 em F)
    int adc_clip = 0;           // ADC só no PS2 (o recorte do microprograma)
    int adc_extra = 0;          // ADC só na referência (seria um erro)
    int adc_back = 0;           // o vidro: o ADC da face de trás (o w 0x8000) nos dois lados
    int variant_batches = 0;    // lotes rodados de novo com o vidro quebrado ou as luzes acesas (contados em batches)
    uint32_t stq_max_ulps = 0;
    float stq_max_abs = 0.0f;
    int rgba_max = 0, xyz_max = 0, z_max = 0;
    std::string first_error;
};

// As constantes globais como a memória da VU as tem (o inverso de CarConstants::write).
CarConstants car_constants_from(const VuMemory& mem);

// Roda o microprograma (as instruções do render method, inferior | superior << 32) a partir do par 2 (o MSCNT continua
// depois do par com o bit E do começo) com TOP = top e compara.
void car_verify_batch(const std::vector<uint64_t>& code, const VuMemory& mem, uint32_t top, const CarBatch& b,
                      CarVerifyStats& s);

}  // namespace drv
