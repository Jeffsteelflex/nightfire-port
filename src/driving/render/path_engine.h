#pragma once
// Os caminhos do CARP e o motor que move coisas por eles (docs/driving.md seção 23): o CARP::PathInfo (o 'pt' do 'Map ':
// os canais de chaves que dão a posição e a rotação em função do tempo paramétrico, com a busca da chave, o linear, o
// slerp e a Bézier), o RPathEngine (RPathEngine.obj: os RPathHandle numa lista, cada um movendo uma instância pelo seu
// caminho com velocidade, acelerador, aceleração e espera, passando ao caminho seguinte no fim) e o que os trilhos da IA
// usam disso: o CARP::AISpline (o 'AISp': a matriz de colocação, o caminho e o nome) com o GetApplyTransform e o
// RotateSplineAboutBase.
//
// No PS2 o WWorld::Open entrega ao RPathEngine as instâncias do mundo cujo estado de animação ('ps') é de caminho (o tipo
// 2; em Paris nenhuma: os 557 são do tipo 1) e cada AISplinePath (o trilho de um carro: driving/game/rail.h) cria o seu
// RPathHandle (CreatePathHandle). O EAnimUpdate chama o RPathEngine::Update(passo da simulação) a cada passo.
//
// Portado do MIPS do PS2 (a verdade), com as mesmas operações na mesma ordem (platform/ps2_float.h, vu0_macro.h). O
// nightfire-cxbx (NightfireResearch: render/PathEngine.h, data/Carp.h, leitura do Xbox) deu os nomes dos campos. As
// contas conferidas contra o original na parte 19 do nf_driving_check.
//
// Divergências (port), também em cada função:
//  - a lista do RPathEngine (um std::list do G++ com nós de 0xdc bytes do FastAlloc) é um vetor de nós alocados no
//    operator new do port (a arena em 64 bits); a ordem é a mesma (push_back), e um RPathHandle tem o leiaute do PS2;
//  - o VU0_EulerToQuat (só nos canais de Bézier de rotação; nenhum caminho de Paris os tem) chama o cosf/sinf do newlib,
//    que aqui são o nf::fm::cos/sin (como a luz do mundo, light_manager.h);
//  - o aviso ao objeto de cena depois de mover a instância (a entrada +0x7c da tabela virtual do RSceneObj do estado) não
//    existe no port (nenhum caminho de Paris o tem): contado em path_engine_stats().scene_notify.

#include <cstdint>
#include <vector>

#include "platform/ptr32.h"

namespace drv {

// ---- CARP::PathInfo -------------------------------------------------------------------------------------------------

// Um canal (0x14 bytes, em PathInfo + 0x40): as posições contam do começo do PathInfo.
struct PathChannel {
    uint32_t name;       // +0x00 o nome ("xform", "xlate")
    uint32_t times;      // +0x04 float[key_count], crescentes
    uint32_t keys;       // +0x08 float[4] por chave, ou 0x30 bytes (controle de entrada, valor, controle de saída)
    uint32_t key_count;  // +0x0c
    uint32_t flags;      // +0x10 kPath*
};
static_assert(sizeof(PathChannel) == 0x14);

enum : uint32_t {
    kPathSpline = 0x01,    // Bézier entre as chaves, senão linear
    kPathRotation = 0x02,  // quatérnios (slerp); os de Bézier são ângulos de Euler (em voltas) feitos quatérnio
    kPathScalar = 0x04,    // um float por chave, espalhado nos quatro
    kPathStepped = 0x08,   // a chave é floor(tempo)
};

// O cabeçalho (0x40 bytes) e os canais.
struct PathInfo {
    float duration;            // +0x00 o tempo paramétrico que dura
    uint32_t channel_count;    // +0x04
    uint32_t unknown08;
    int32_t rotation_channel;  // +0x0c -1: nenhum
    int32_t position_channel;  // +0x10 -1: nenhum
    uint8_t unknown14[0x2c];
    const PathChannel* channel(uint32_t i) const {
        return reinterpret_cast<const PathChannel*>(reinterpret_cast<const uint8_t*>(this) + 0x40 + 0x14 * i);
    }
    const uint8_t* at(uint32_t offset) const { return reinterpret_cast<const uint8_t*>(this) + offset; }
};
static_assert(sizeof(PathInfo) == 0x40);

// [driving] 0x002bfb50 floorf (newlib, s_floorf.c: pelos bits, com o "huge + x > 0")
float newlib_floorf(float x);
// [driving] 0x002dadc8 CARP::PathInfo::FindChannelIndex(char *): o índice do canal com o nome (sem caixa), ou a contagem.
uint32_t PathInfo_FindChannelIndex(const PathInfo* p, const char* name);
// [driving] 0x002dae58 CARP::PathInfo::GetKnotIndex(float, unsigned, PathChannel *): a chave em que o tempo cai,
// começando pela dica e as vizinhas, depois a busca binária (float lower_bound, 0x002dbfa0).
uint32_t PathInfo_GetKnotIndex(const PathInfo* p, float t, uint32_t hint, const PathChannel* c);
// [driving] 0x002dafb8 CARP::PathInfo::ComputeUnitTime(float t, float a, float b): (t - a) / (b - a), 0 se a >= b.
float PathInfo_ComputeUnitTime(float t, float a, float b);
// [driving] 0x002db028 CARP::PathInfo::ScaleToUnitTime(float, unsigned de, unsigned até, PathChannel *) (o degrau:
// floorf(t), um defeito do original).
float PathInfo_ScaleToUnitTime(const PathInfo* p, float t, uint32_t from, uint32_t to, const PathChannel* c);
// [driving] 0x002dafe0 CARP::PathInfo::ComputeLinear(COORD4 &a, COORD4 &b, COORD4 &out, float t, bool slerp)
void PathInfo_ComputeLinear(const float a[4], const float b[4], float out[4], float t, bool slerp);
// [driving] 0x002db080 CARP::PathInfo::EvaluateLinear e 0x002db188 CARP::PathInfo::EvaluateSpline: o valor do canal no
// tempo em out[4]; devolvem a chave (a dica da próxima vez).
uint32_t PathInfo_EvaluateLinear(const PathInfo* p, float t, uint32_t hint, const PathChannel* c, float out[4]);
uint32_t PathInfo_EvaluateSpline(const PathInfo* p, float t, uint32_t hint, const PathChannel* c, float out[4]);
// [driving] 0x002db320 CARP::PathInfo::Evaluate(unsigned canal, float, unsigned dica, COORD4 &)
uint32_t PathInfo_Evaluate(const PathInfo* p, uint32_t channel, float t, uint32_t hint, float out[4]);
// [driving] 0x002db370 CARP::PathInfo::EvaluateMatrix(float, unsigned &chave_pos, unsigned &chave_rot, MATRIX4 &, float
// &peso): a rotação do canal de rotação (VU0_quattom4) e a posição do de posição na linha 3 (o w dele em *peso); um canal
// que falta deixa a sua parte da matriz como estava.
void PathInfo_EvaluateMatrix(const PathInfo* p, float t, uint32_t& pos_key, uint32_t& rot_key, float m[4][4],
                             float& weight);
// [driving] 0x002ccab8 VU0_EulerToQuat(COORD4 &, COORD4 &): os ângulos (em voltas) numa matriz e o VU0_m4toquat.
void VU0_EulerToQuat(const float euler[4], float q[4]);

// ---- CARP::AISpline ('AISp', 0x70 bytes) ----------------------------------------------------------------------------

struct AISplineRec {
    float matrix[4][4];   // +0x00 a colocação
    uint32_t path;        // +0x40 (TagReference resolvido: p32 PathInfo)
    char name[0x28];      // +0x44 "path_Helicopter02"...
    uint8_t disabled;     // +0x6c não zero: a colocação não vale (a identidade)
    uint8_t unknown6d[3];
};
static_assert(sizeof(AISplineRec) == 0x70);

// [driving] 0x002db478 CARP::RotateSplineAboutBase(MATRIX4 &base, MATRIX4 &avaliada, MATRIX4 &spline, MATRIX4 *out)
void CARP_RotateSplineAboutBase(const float base[4][4], const float evaluated[4][4], const float spline[4][4],
                                float out[4][4]);
// [driving] 0x002db690 CARP::AISpline::GetApplyTransform(MATRIX4 *) const
void AISpline_GetApplyTransform(const AISplineRec* s, float out[4][4]);

// ---- o estado de animação de uma instância ('ps', 0x20 bytes) --------------------------------------------------------

struct ProcAnimState {
    uint8_t type;           // +0x00 2: caminho
    uint8_t unknown01;      // 0xfa no AISplinePath
    uint16_t unknown02;
    uint32_t flags;         // +0x04 bit 15: começa andando
    uint32_t scene_obj;     // +0x08 (p32 RSceneObj) avisado depois de cada movimento
    uint32_t path;          // +0x0c (p32 PathInfo)
    uint32_t master;        // +0x10 (p32 instância) a instância cujo caminho este segue a uma distância fixa
    float start_time;       // +0x14 o tempo paramétrico do começo
    float speed;            // +0x18 tempo paramétrico por 1/60 s no acelerador 1
    uint8_t identity_axes;  // +0x1c não zero: a matriz de aplicação é a identidade; senão kPathAxes
    uint8_t unknown1d[3];
};
static_assert(sizeof(ProcAnimState) == 0x20);

// ---- RPathEngine::RPathHandle (0xd4 bytes) ---------------------------------------------------------------------------

struct RPathHandle {
    uint32_t instance;      // +0x00 (p32) a instância movida (as três primeiras palavras de cada linha da matriz)
    uint32_t state;         // +0x04 (p32 ProcAnimState)
    float base[4][4];       // +0x08 a matriz da instância na construção (os w das linhas 0-2 zerados, o da 3 em 1)
    float apply[4][4];      // +0x48 a matriz de aplicação (a identidade ou kPathAxes)
    float velocity[3];      // +0x88 a da última atualização, por segundo
    float last_time;        // +0x94 em 1/60 s
    uint32_t current;       // +0x98 (p32 PathInfo)
    uint32_t next;          // +0x9c (p32 PathInfo) o seguinte, no fim do corrente
    uint32_t master;        // +0xa0 (p32 RPathHandle)
    float speed;            // +0xa4
    float delta;            // +0xa8 com mestre, quanto atrás dele; senão o passo da última atualização
    float time;             // +0xac o tempo paramétrico
    uint32_t pos_key;       // +0xb0 as dicas do EvaluateMatrix
    uint32_t rot_key;       // +0xb4
    float weight;           // +0xb8 o w do canal de posição
    float throttle;         // +0xbc
    float accel;            // +0xc0 o passo do acelerador para o desejado por atualização
    float desired;          // +0xc4
    int32_t accel_delay;    // +0xc8 atualizações antes de o acelerador andar
    int32_t running;        // +0xcc
    uint32_t user;          // +0xd0 "" na construção; o CreatePathHandle grava a palavra +0x34 do modelo do artigo
};
static_assert(sizeof(RPathHandle) == 0xd4);

// O tempo do RPathEngine (FUN_001b0f48: t x RTimeData::fFrequency x 60, em 1/60 s) e a taxa do RTimeData::Init
// (0x001d1ec0: fRate = GetFoundationVideoModeRate() = 60, fFrequency = 1 / fRate).
struct PathEngineClock {
    float rate = 60.0f;      // 0x333ff0
    float frequency = 0.0f;  // 0x333ff4
};
PathEngineClock& path_engine_clock();
// [driving] 0x001d1ec0 RTimeData::Init (só os dois globais do relógio)
void RTimeData_Init(float video_rate = 60.0f);
// [driving] 0x001b0f48 (sem nome): o tempo em 1/60 s de um passo da simulação
float path_engine_time(float t);

// [driving] 0x001b1110 RPathEngine::RPathHandle::RPathHandle(CARP::Instance &, CARP::ProcAnimState &)
void RPathHandle_Construct(RPathHandle& h, uint32_t instance, uint32_t state);
// [driving] 0x001b0f68 RPathEngine::RPathHandle::RPathHandle(const RPathHandle &): tudo menos as dicas (zeradas).
void RPathHandle_Copy(RPathHandle& h, const RPathHandle& from);
// [driving] 0x001b1258 RPathEngine::RPathHandle::Init(float)
void RPathHandle_Init(RPathHandle& h, float now);
// [driving] 0x001b14b8 RPathEngine::RPathHandle::Update(float)
void RPathHandle_Update(RPathHandle& h, float now);
// [driving] 0x001b18a8 RPathEngine::RPathHandle::GetOrientMat(MATRIX4 &) const: a matriz da instância sem a posição.
void RPathHandle_GetOrientMat(const RPathHandle& h, float out[4][4]);
// [driving] 0x001b1898 RPathEngine::RPathHandle::GetPosition(void) const: a linha 3 da instância.
const float* RPathHandle_GetPosition(const RPathHandle& h);
// Os acessores de uma instrução (0x001b1830..0x001b1880, os corpos lidos no PS2; os nomes do nightfire-cxbx).
inline void RPathHandle_SetCurrentPath(RPathHandle& h, uint32_t p) { h.current = p; }    // [driving] 0x001b1830
inline void RPathHandle_SetNextPath(RPathHandle& h, uint32_t p) { h.next = p; }          // [driving] 0x001b1838
inline float RPathHandle_GetParametricDuration(const RPathHandle& h) {                   // [driving] 0x001b1840
    return nf::p32_ptr<const PathInfo>(h.current)->duration;
}
inline void RPathHandle_SetRunning(RPathHandle& h, int32_t v) { h.running = v; }         // [driving] 0x001b1850
inline void RPathHandle_SetSpeed(RPathHandle& h, float v) { h.speed = v; }               // [driving] 0x001b1858
inline void RPathHandle_SetThrottle(RPathHandle& h, float v) { h.throttle = v; }         // [driving] 0x001b1860
inline void RPathHandle_SetDesiredThrottle(RPathHandle& h, float v) { h.desired = v; }   // [driving] 0x001b1868
inline void RPathHandle_SetAcceleration(RPathHandle& h, float v) { h.accel = v; }        // [driving] 0x001b1870
inline void RPathHandle_SetAccelDelay(RPathHandle& h, int32_t v) { h.accel_delay = v; }  // [driving] 0x001b1878
inline void RPathHandle_SetParametricTime(RPathHandle& h, float v) {                     // [driving] 0x001b1880
    if (h.master == 0) h.time = v;
}

// ---- RPathEngine (os estáticos: a lista fgPathHandles 0x3339a8 e o iterador 0x3d1cc8) --------------------------------

// [driving] 0x001b1c20 RPathEngine::CreatePathHandle(CARP::Instance &, CARP::ProcAnimState &, float): só para o estado
// de caminho (tipo 2) com caminho, numa instância com a bandeira 0x10; nulo senão.
RPathHandle* RPathEngine_CreatePathHandle(uint32_t instance, uint32_t state, float now);
// [driving] 0x001b1dc0 RPathEngine::DestroyPathHandle(RPathHandle *)
void RPathEngine_DestroyPathHandle(RPathHandle* h);
// [driving] 0x001b1988 RPathEngine::AddInstanceList(CARP::Instance *, CARP::ProcAnimState *, unsigned, float): os sem
// mestre primeiro, depois os com mestre, e o Init de todos.
void RPathEngine_AddInstanceList(uint32_t instances, uint32_t states, uint32_t count, float now);
// [driving] 0x001b2058 RPathEngine::Update(float) (pelo EAnimUpdate, com o passo da simulação)
void RPathEngine_Update(float now);
// [driving] 0x001b1e58 RPathEngine::Reset(float): o Init de todos
void RPathEngine_Reset(float now);
// [driving] 0x001b1ec0 RPathEngine::Purge(void)
void RPathEngine_Purge();
// [driving] 0x001b20c8 RPathEngine::GetPathHandle(CARP::Instance *)
RPathHandle* RPathEngine_GetPathHandle(uint32_t instance);
// [driving] 0x001b1f28 RPathEngine::GetFirstPathHandle e 0x001b1f68 GetNextPathHandle
RPathHandle* RPathEngine_GetFirstPathHandle();
RPathHandle* RPathEngine_GetNextPathHandle();
// Os nós na ordem da lista (o port).
const std::vector<RPathHandle*>& path_engine_handles();

struct PathEngineStats {
    uint64_t updates = 0;        // RPathHandle::Update que moveram a instância
    uint64_t wraps = 0;          // fins de caminho
    uint64_t scene_notify = 0;   // avisos ao RSceneObj que o port não dá
    uint64_t created = 0, destroyed = 0;
};
PathEngineStats& path_engine_stats();

}  // namespace drv
