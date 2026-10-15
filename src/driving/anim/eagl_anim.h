#pragma once
// A animação dos personagens: a libeaglanima da EA (o "EAGLAnim" do EAGL 4.06.01), portada do DRIVING.ELF
// (docs/driving.md seção 20; os nomes vêm do DRIVING.SYM e do nightfire-cxbx, da NightfireResearch).
//
// Os dados (os bancos data\actors\anims\*.dat, carregados pelo EAGL::DynamicLoader) são árvores de canais: cada
// animação começa por um u16 com o tipo do canal, e o FnAnim do tipo (criado pela fábrica 0x002647c8 num reservatório
// próprio) lê os dados por SetAnimMemoryMap e escreve a pose em Eval(anterior, tempo, pose). A pose é a do
// Skeleton: 12 floats (0x30 bytes) por osso, a escala xyz, a escala do comprimento, o quatérnio (x, y, z, w), a
// translação xyz e 1; um canal escreve grupos de floats nos índices (em floats) que a lista dele dá.
//
// Os tipos que os dados do disco usam (todas as animações dos 89 bancos dos nove arquivos, `nf_driving --check`):
//  - 15 FnCompoundChannel (0x1c bytes): a raiz de cada animação; cria os subcanais na primeira avaliação (do último
//    para o primeiro) e avalia todos do último para o primeiro; com UseFPS multiplica os tempos pelos quadros por
//    segundo do objeto;
//  - 11 FnDeltaQuatChan: os quatérnios de todos os quadros em DeltaCompressedData (o valor de cada quadro é o do
//    anterior mais o delta: 4, 8 ou 16 bits por canal e quadro, com deslocamento e escala por canal); entre dois quadros
//    a interpolação é linear (DecompressValuesIndexed) sem normalizar;
//  - 13 FnKeyQuatChan: quatérnios em quadros-chave (a lista de tempos em u16), o mesmo DeltaCompressedData por chave e o
//    slerp rápido da VU0 (0x00298478, igual ao VU0_fastqslerp 0x002cc018: a combinação linear e a renormalização);
//  - 20 FnDeltaF3 e 21 FnDeltaF1 (0x30 bytes): três floats (a translação, a escala) ou um float por canal em blocos de
//    2^n quadros (o primeiro com o valor inteiro em 16 bits, os outros com deltas de 8 bits), com ou sem quadros-chave,
//    e as constantes no fim; a interpolação é linear;
//  - 1 FnRawEventChan: os eventos (as animações de eventos do ActAnimGroup): o EvalEvent (a entrada +0x54, que o
//    ActPoser::DoEventPose chama com o ActEvents do ator) chama os tratadores da tabela pelo RawEventChannel::Eval; o
//    Eval da pose o chamaria com a pose como tabela, e a pose não é uma tabela: esse caminho não roda
//    (`AnimStats::events`).
// Outros tipos (os da fábrica que os dados não usam) não são portados: a fábrica devolve nulo e conta
// (`AnimStats::unknown_types`); o EvalSQT com máscara (só os misturadores de poses o pedem) também não.
//
// O Skeleton (o "__Bone"/"Skeleton" dos data\actors\skeleton\*.o): +8 o número de ossos, +0xc as escalas de
// comprimento (ou nulo), a partir de +0x10 os ossos de 0x70 bytes: +0 a escala, +0xc o pai (-1 a raiz), +0x10 o
// quatérnio, +0x20 a translação, +0x30 a inversa da pose de ligação (MATRIX4). As funções do esqueleto montam as
// matrizes: PoseSQTToLocal (o VU0_SQTquattom4 e a escala do comprimento na primeira coluna), PoseLocalToGlobal (local x
// global do pai, a raiz copiada) e PoseGlobalToSkin (inversa da ligação x global: o EAGLAnimationBuffer que o render
// method de pele lê).
//
// A aritmética é a do PS2 (platform/ps2_float.h e vu0_macro.h: a FPU do EE e a VU0 em modo macro, bit a bit). A
// conferência é a parte 16 do nf_driving_check (`--so personagens`): o código original no interpretador MIPS com a VU0
// ao lado deste porte, com os bancos e os esqueletos do disco e tempos sorteados.
//
// Divergências (só do port): o reservatório (MemoryPoolManager: a lista livre por tipo e por classe de tamanho, o bloco
// de 0xc800 bytes) vira objetos C++ e std::vector; os buffers do FnDeltaQuatChan e do FnKeyQuatChan crescem como o
// SetAnimMemoryMap original pede (o jogo só realoca quando o novo número de canais não é menor que o antigo; os valores
// antigos nunca são lidos depois de um SetAnimMemoryMap, que zera a chave em cache). Os laços do DecompressValues que
// avançam o ponteiro dos deltas só `nchan` por quadro (e não o número total de canais) estão como no original: o jogo
// só pede um quadro (de uma chave à seguinte) com nchan menor que o total.

#include <cstdint>
#include <memory>
#include <vector>

namespace drv::anim {

constexpr int kPoseFloats = 12;  // floats por osso na pose
constexpr int kMatrixFloats = 16;

// Os tipos de canal da fábrica (o u16 no começo dos dados).
enum FnType : uint16_t {
    kFnRawEvent = 1,
    kFnDeltaQuat = 11,
    kFnKeyQuat = 13,
    kFnCompound = 15,
    kFnDeltaF3 = 20,
    kFnDeltaF1 = 21,
};

// Contadores só do port.
struct AnimStats {
    uint64_t created = 0;        // canais criados pela fábrica
    uint64_t unknown_types = 0;  // tipos que a fábrica não porta (o canal fica de fora)
    uint64_t events = 0;         // avaliações de um canal de eventos (não rodam: ver o cabeçalho)
    uint64_t masked = 0;         // EvalSQT com máscara (não portado)
    uint64_t guards = 0;         // casos guardados (um índice de pose fora da pose, um esqueleto vazio)
};
AnimStats& anim_stats();
void reset_anim_stats();

// Um canal (a classe EAGLAnim::FnAnim: +0 a lista livre, +4 o tipo, +8 a tabela virtual, +0xc os dados).
class FnAnim {
public:
    explicit FnAnim(uint16_t type) : type_(type) {}
    virtual ~FnAnim() = default;
    uint16_t type() const { return type_; }
    uint32_t data() const { return data_; }  // o endereço (P32) dos dados

    // A tabela virtual: SetAnimMemoryMap (+0x80), Eval (+0x20), GetLength (+0x28: escreve o número de quadros e
    // devolve 1; o padrão dos canais sem comprimento devolve 0 sem escrever). `pose_floats` é o tamanho da pose (só
    // do port: um índice de um canal fora dela é guardado e contado).
    virtual void SetAnimMemoryMap(uint32_t data) = 0;
    virtual void Eval(float prev, float time, float* pose, int pose_floats) = 0;
    virtual bool GetLength(float& out) const = 0;
    // A entrada +0x54: EvalEvent(anterior, tempo, tratadores, dados): os eventos entre os dois tempos chamam os
    // tratadores (a tabela `handlers`, p32: uma palavra por número de evento, o ActEvents do ator). O padrão
    // (0x0026dff8 EAGLAnim::FnAnim::EvalEvent) não faz nada e devolve 0.
    virtual bool EvalEvent(float /*prev*/, float /*time*/, uint32_t /*handlers*/, uint32_t /*user*/) { return false; }

protected:
    uint16_t type_;
    uint32_t data_ = 0;
};

// 0x002647c8 NewFnAnim (a fábrica do reservatório): o canal do tipo, ou nulo para um tipo não portado.
std::unique_ptr<FnAnim> NewFnAnim(uint16_t type);

// O canal composto (o tipo 15).
class FnCompoundChannel : public FnAnim {
public:
    FnCompoundChannel() : FnAnim(kFnCompound) {}
    void SetAnimMemoryMap(uint32_t data) override;
    void Eval(float prev, float time, float* pose, int pose_floats) override;
    bool GetLength(float& out) const override;
    // 0x002659a8 EAGLAnim::FnCompoundChannel::EvalEvent: os subcanais (criados como no Eval), do último para o
    // primeiro; devolve 1 se algum devolveu.
    bool EvalEvent(float prev, float time, uint32_t handlers, uint32_t user) override;
    // UseFPS: os tempos passam a ser em segundos (multiplicados pelos quadros por segundo, o byte +0x18).
    void UseFPS(bool on, uint8_t fps) {
        use_fps_ = on;
        fps_ = fps;
    }
    int channels() const { return int(subs_.size()); }
    const FnAnim* channel(int i) const { return subs_[size_t(i)].get(); }

private:
    void MakeSubChannels();
    std::vector<std::unique_ptr<FnAnim>> subs_;  // +0x10 (criados na primeira avaliação)
    bool subs_made_ = false;
    bool use_fps_ = false;                       // +0x14
    uint8_t fps_ = 0;                            // +0x18
};

// ---- o DeltaCompressedData (+0 u16 o número de canais, +2 u8 os bits por delta (4, 8 ou 16), a partir de +4 os
// canais de 12 bytes {deslocamento, escala, base}, depois os deltas de cada quadro) -------------------------------
// 0x00276820 EAGLAnim::DeltaCompressedData::DecompressValues
void DecompressValues(uint32_t data, int first, int nchan, int from, int to, const float* cache, float* out);
// 0x00276c70 EAGLAnim::DeltaCompressedData::DecompressValuesIndexed (`index` = a lista u16 de índices em
// floats, um por grupo de `stride` canais; `out_floats` só do port: os índices fora dela são guardados).
void DecompressValuesIndexed(float t, uint32_t data, int first, int n, int from, int to, const float* cache, float* out,
                             int stride, const uint16_t* index, int out_floats);

// ---- a VU0 -------------------------------------------------------------------------------------------------------
// 0x00298478 o slerp rápido (= VU0_fastqslerp 0x002cc018): q = q1 t +- q0 (1 - t) (o sinal pela distância
// das duas pontas), renormalizado com o RSQRT da VU.
void fast_qslerp(const float* q0, const float* q1, float* out, float t);
// 0x00297c60 EAGL::VU0_MATRIX4_mult: out = a x b (linhas de a pela matriz b).
void MATRIX4_mult(const float* a, const float* b, float* out);
// 0x002985d0 EAGL::VU0_SQTquattom4: a matriz da rotação do quatérnio com as linhas x escala e a translação.
void SQTquattom4(const float* scale, const float* quat, const float* trans, float* out);

// ---- o Skeleton (os dados em `skel`, endereço P32) -----------------------------------------------------------------
int Skeleton_NumBones(uint32_t skel);
int Skeleton_Parent(uint32_t skel, int bone);
// 0x00262aa8 EAGLAnim::Skeleton::GetStillPose (sem máscara)
void Skeleton_GetStillPose(uint32_t skel, float* pose);
// 0x002623c0 EAGLAnim::Skeleton::PoseSQTToLocal (sem máscara)
void Skeleton_PoseSQTToLocal(uint32_t skel, const float* pose, float* local);
// 0x00262508 EAGLAnim::Skeleton::PoseLocalToGlobal (sem máscara; `local` e `global` podem ser o mesmo)
void Skeleton_PoseLocalToGlobal(uint32_t skel, const float* local, float* global);
// 0x002634f8 EAGLAnim::Skeleton::PoseGlobalToSkin (sem máscara)
void Skeleton_PoseGlobalToSkin(uint32_t skel, const float* global, float* skin);
// 0x002627e8 EAGLAnim::Skeleton::PoseSQTToGlobal (sem máscara): a matriz de cada osso pelo quatérnio da pose
// (a conta da VU0 em linha, não o VU0_SQTquattom4), a escala do comprimento na primeira coluna e a translação, vezes a
// global do pai (a raiz como está). O CalcSnapAndCorrectionMatrices do ActPoser a usa.
void Skeleton_PoseSQTToGlobal(uint32_t skel, const float* pose, float* global);

// ---- os eventos ----------------------------------------------------------------------------------------------------
// Um evento dos dados (0x10 bytes): +0 o número (o índice na tabela dos tratadores), +4 o quadro, +8 e +0xc os valores.
// 0x00274958 EAGLAnim::RawEventChannel::Eval(float anterior, float tempo, int &cursor, float &último, tabela,
// dados): acha o cursor pelos tempos e chama o tratador de cada evento passado (do cursor ao fim e do começo, quando o
// tempo volta; só os do próprio quadro quando anterior == tempo).
void RawEventChannel_Eval(uint32_t data, int32_t& cursor, float& last, float prev, float time, uint32_t handlers,
                          uint32_t user);
// A chamada de um tratador (a entrada +0xc da tabela virtual dele, +4 no objeto): `event` (p32) aponta o evento. Fica no
// driving/anim/act_actor.cpp (o FireEvent, o DropWeaponEvent, o HeightCalcEvent e o DefaultEventHandler do jogo).
void ActEvent_Dispatch(uint32_t handler, float time, uint32_t event, uint32_t user);
// O tratador que o RawEventChannel_Eval chama (o padrão: ActEvent_Dispatch; a parte 20 do nf_driving_check o troca).
using RawEventDispatch = void (*)(uint32_t handler, float time, uint32_t event, uint32_t user);
RawEventDispatch& raw_event_dispatch();

}  // namespace drv::anim
