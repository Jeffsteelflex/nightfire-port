#pragma once
// A camada dos atores (o "Act*" do DRIVING.ELF: ActActorDatabase.obj) no que posar e tocar uma animação num personagem
// precisa (docs/driving.md seção 20): o esqueleto (ActSkeleton), o grupo de animações (ActAnimGroup) e o poser
// (ActPoser): SetNormalAnimation (as duas), AdvanceTime, DoMainPose, DoSkeletonPose, DoEventPose, DoInitialPoses,
// TransformLocalPoseToWorldPose, Skin (o EAGLAnimationBuffer, fSkinnedTransforms: uma matriz por osso, a inversa da
// ligação x a global, que o render method de pele dos personagens, mat_char.h, multiplica pela escala do modelo na
// operação 0xb do GeoPrim::Draw), as matrizes do ActPoseMatrices (o CalcSnapAndCorrectionMatrices, o Rotate, o
// RotateX, a origem da animação) e os ossos da arma. O ator, o personagem, a arma, os eventos e os bancos ficam em
// driving/anim/act_actor.h.
//
// Os leiautes são os do PS2 (os ponteiros como palavras p32, para a parte 16 e a parte 20 do nf_driving_check
// compararem com o original): ActSkeleton 0x24, ActAnimGroup 0x20, ActPoseMatrices 0x10c, ActPoser 0x74,
// ActIKSolverArray 0x18, ActGlobalPoseOverrideArray 0x1c.
//
// Os personagens do jogo (as cadeias "esqueleto.modelo.texturas.escala" do ELF, lidas pelo ActCharacterInfo 0x0010de98):
// "2.c_bondc.h_bond.t_bondc.100" (Bond no carro), "3.c_domm.h_dommin.t_dommin.100" (Dominique), "2.h_pmask.h_pmask.
// t_paris.95" (os capangas mascarados de Paris), "4.a_handc.b_handc.90" (as mãos de Bond), "6.a_snipe.b_snipe.70" (o
// braço do atirador)...: o número é o esqueleto (data\actors\skeleton\<n>skel.o), a letra o tipo (h capanga, p pedestre,
// c personagem, a braço), o modelo é <nome><LOD>.dat (1 o mais detalhado) e a escala o último número (%).
//
// As sobreposições globais (ActGlobalPoseOverrideArray, a mira dos passageiros de Paris) são portadas (seção 26).
// O que não é portado (contado em actor_stats() no ponto em que o jogo o faria): o IK (ActIKSolverArray::Solve: o
// DoIK conta os solvers de um ator que os tenha); o modo 3 (a mistura manual) do DoMainPose e o GetWeaponRange. Divergências: as listas livres do MemoryPoolManager do EAGLAnim (os
// FnAnim do ActAnimGroup são objetos C++ do port: os ponteiros +8 e +0x10 apontam para eles).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "driving/anim/eagl_anim.h"
#include "platform/ptr32.h"

namespace drv {
class Bigf;
namespace eagl {
class DynamicLoader;
}
}  // namespace drv

namespace drv::anim {

// O resolvedor de símbolos de uma carga (o do ActManager: "event.<nome>" vira o número do evento): devolve o valor e
// diz se achou.
using SymbolResolver = uint32_t (*)(const char* name, bool& found);

// Um objeto ELF do EAGL carregado e vivo (um .o inteiro, ou o .dat com o .rel), alinhado a 16 como o FastAlloc do jogo.
class EaglObject {
public:
    EaglObject();
    ~EaglObject();
    EaglObject(const EaglObject&) = delete;
    EaglObject& operator=(const EaglObject&) = delete;
    // `rel_key` vazio: um ELF inteiro. Os símbolos indefinidos vão a `resolver` (nulo: ficam nulos).
    bool load(Bigf& arc, const std::string& dat_key, const std::string& rel_key, std::string& error,
              SymbolResolver resolver = nullptr);
    // O primeiro símbolo definido do tipo (o GetNextAddr do jogo com o iterador zerado), 0 se não há.
    uint32_t first_of_type(const char* type) const;
    eagl::DynamicLoader* loader() const { return loader_.get(); }
    uint8_t* data() const { return dat_; }
    uint32_t data_size() const { return dat_size_; }
    uint8_t* rel() const { return rel_; }  // a segunda parte (o .rel; nulo num ELF inteiro)
    uint32_t rel_size() const { return rel_size_; }

private:
    std::vector<uint8_t> dat_store_, rel_store_;
    uint8_t* dat_ = nullptr;
    uint8_t* rel_ = nullptr;
    uint32_t dat_size_ = 0, rel_size_ = 0;
    std::unique_ptr<eagl::DynamicLoader> loader_;
};

// O banco de animações (data\actors\anims\<nome>.dat): o símbolo "AnimationBank" (+4 o número de animações, +0xc as
// animações, +0x10 os nomes em ordem) e o FnDefaultAnimBank em volta dele.
class AnimBank {
public:
    bool load(Bigf& arc, const std::string& name, std::string& error, SymbolResolver resolver = nullptr);
    uint32_t address() const { return bank_; }
    // 0x0010c788 EAGLAnim::FnDefaultAnimBank::GetNumAnims
    int num_anims() const;
    // 0x0010c798 EAGLAnim::FnDefaultAnimBank::GetAnim(int)
    uint32_t anim(int index) const;
    // 0x0010c7b0 EAGLAnim::FnDefaultAnimBank::GetAnimName(int)
    const char* anim_name(int index) const;
    // 0x0026ddd0 a busca binária pelo nome (o GetAnimIndex(char *)): -1 se não há.
    int find(const char* name) const;
    const EaglObject& object() const { return obj_; }

private:
    EaglObject obj_;
    uint32_t bank_ = 0;
};

// O ActSkeleton do PS2 (0x24 bytes).
struct ActSkeletonData {
    uint32_t file;        // +0x00 o .o carregado (o port: o p32 dos dados do EaglObject)
    uint32_t loader;      // +0x04 (p32 eagl::DynamicLoader do port)
    uint32_t skeleton;    // +0x08 (p32) o Skeleton
    uint32_t still_pose;  // +0x0c (p32 float[n * 12])
    uint32_t pose;        // +0x10 (p32 float[n * 12])
    uint32_t matrices;    // +0x14 (p32 float[n * 16]) as matrizes locais
    int32_t root;         // +0x18 "Root.Root"
    int32_t weapon;       // +0x1c "Root.Weapon" (ou "Root.RWeapon"): o osso da arma 0
    int32_t clip;         // +0x20 "Root.Clip": o osso da arma 1
};
static_assert(sizeof(ActSkeletonData) == 0x24);

// O ActSkeleton: os dados do PS2 em `d` (o p32 de `d` é o que o ActPoser guarda) e o armazenamento do port.
class ActSkeleton {
public:
    ActSkeleton();
    ActSkeleton(const ActSkeleton&) = delete;
    ActSkeleton& operator=(const ActSkeleton&) = delete;
    // 0x00114760 ActSkeleton::ActSkeleton (data\actors\skeleton\<index>skel.o)
    bool load(Bigf& arc, int index, std::string& error);
    // Só do port (testes): um Skeleton já na memória (endereço P32), com as poses e as matrizes do tamanho dele.
    void attach(uint32_t skel, int root);
    uint32_t skeleton() const { return d.skeleton; }
    // [driving] 0x00114a48 ActSkeleton::GetNumBones
    int num_bones() const { return Skeleton_NumBones(d.skeleton); }
    // 0x00114a08 ActSkeleton::GetBoneIndex: o "Bone" do nome (o índice que o símbolo guarda), -1 se não há.
    int bone_index(const char* name) const;
    // [driving] 0x00114a58 ActSkeleton::GetStillPose (na pose parada do objeto)
    void GetStillPose() { Skeleton_GetStillPose(d.skeleton, still_pose.data()); }
    const EaglObject& object() const { return obj_; }

    ActSkeletonData d{};
    std::vector<float> still_pose;  // o +0xc
    std::vector<float> pose;        // o +0x10
    std::vector<float> matrices;    // o +0x14

private:
    void alloc_buffers();
    EaglObject obj_;
};
// 0x00114a08 ActSkeleton::GetBoneIndex sobre os dados do PS2 (o DynamicLoader do +4).
int ActSkeleton_GetBoneIndex(const ActSkeletonData& s, const char* name);

// O ActAnimGroup do PS2 (0x20 bytes).
struct ActAnimGroup {
    float length = 0.0f;         // +0x00 os quadros da animação de pose
    float events_length = 0.0f;  // +0x04 os da de eventos
    uint32_t anim = 0;           // +0x08 (p32 FnAnim do port) a de pose
    uint32_t anim_data = 0;      // +0x0c
    uint32_t events = 0;         // +0x10 (p32 FnAnim do port) a de eventos
    uint32_t events_data = 0;    // +0x14
    int32_t index = -1;          // +0x18 o índice no banco (-1 nenhum)
    int32_t bank = -1;           // +0x1c o banco do ActAnimationDatabase

    ActAnimGroup() = default;
    ActAnimGroup(const ActAnimGroup&) = delete;
    ActAnimGroup& operator=(const ActAnimGroup&) = delete;
    // 0x0010cac0 ActAnimGroup::~ActAnimGroup (os FnAnim voltam ao reservatório: aqui o delete)
    ~ActAnimGroup();
    FnAnim* anim_fn() const { return anim ? nf::p32_ptr<FnAnim>(anim) : nullptr; }
    FnAnim* events_fn() const { return events ? nf::p32_ptr<FnAnim>(events) : nullptr; }
    // 0x0010c928 ActAnimGroup::ChangeAnimation(int banco, int índice): as duas animações pelo
    // ActAnimationDatabase::GetAnimation (a tabela ALookup.bin; driving/anim/act_actor.h).
    void ChangeAnimation(int32_t bank, int32_t index);
    // A segunda metade do ChangeAnimation, com as duas animações já achadas (0 = nenhuma): os testes e o --actor do
    // nf_driving, que dão a animação direto.
    void ChangeAnimationData(uint32_t pose_data, uint32_t event_data);
};
static_assert(sizeof(ActAnimGroup) == 0x20);

// O ActPoseMatrices do PS2 (0x10c bytes).
struct ActPoseMatrices {
    float initial[16];        // +0x00 a matriz do CalcSnapAndCorrectionMatrices (o InitialTbOu; +0x30 a origem)
    float correction[16];     // +0x40
    float snap[16];           // +0x80
    float pose_to_world[16];  // +0xc0 = correction x snap x initial
    uint32_t globals;         // +0x100 (p32 float[n * 16]) as matrizes globais dos ossos
    uint32_t globals2;        // +0x104 o mesmo (o que o destrutor solta)
    int32_t bones;            // +0x108
};
static_assert(sizeof(ActPoseMatrices) == 0x10c);

// O ActIKSolverArray (0x18) e o ActGlobalPoseOverrideArray (0x1c) do PS2: só os campos do construtor (os solvers e as
// sobreposições não são portados: actor_stats()).
struct ActIKSolverArrayData {
    int32_t count;       // +0x00
    uint32_t solvers;    // +0x04
    int32_t root;        // +0x08
    uint32_t globals;    // +0x0c
    uint32_t matrices;   // +0x10
    uint32_t skeleton;   // +0x14
};
static_assert(sizeof(ActIKSolverArrayData) == 0x18);
// O ActGlobalPoseOverrideArray (docs/driving.md seção 26): as sobreposições que um personagem põe em alguns ossos (o
// pescoço e o ombro do atirador da janela, os pulsos da motorista), portadas do PS2.
struct ActGlobalPoseOverrideArrayData {
    int32_t count;        // +0x00
    uint32_t bones;       // +0x04 (p32 int32_t[count]) os ossos
    uint32_t matrices;    // +0x08 (p32 float[count * 16]) as matrizes pedidas
    uint32_t weights;     // +0x0c (p32 float[count]) os pesos (1: a matriz inteira)
    uint32_t globals;     // +0x10 as matrizes globais do poser (o ActPoseMatrices +0x100)
    int32_t local_space;  // +0x14 1: a matriz troca a local do osso; 0: a global (o pai desfeito)
    uint32_t set;         // +0x18 (p32 int32_t[count]) 1 depois do primeiro SetGlobalPoseOverride
};
static_assert(sizeof(ActGlobalPoseOverrideArrayData) == 0x1c);

// [driving] 0x0010f140 ActGlobalPoseOverrideArray::CreateGlobalPoseOverrides(int, int *ossos, bool local)
void ActGlobalPoseOverrideArray_CreateGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o, int32_t count,
                                                          const int32_t* bones, int32_t local_space);
// [driving] 0x0010f230 ActGlobalPoseOverrideArray::DeleteGlobalPoseOverrides
void ActGlobalPoseOverrideArray_DeleteGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o);
// [driving] 0x0010f2b0 ActGlobalPoseOverrideArray::BlendMatrices(MATRIX4 &a, MATRIX4 &b, float t): `a` vira o slerp
// rápido dos quatérnios das duas (t) com a translação de `a`.
void ActGlobalPoseOverrideArray_BlendMatrices(float t, float* a, const float* b);
// [driving] 0x0010f378 ActGlobalPoseOverrideArray::SetGlobalPoseOverride(int, MATRIX4 &, float peso)
void ActGlobalPoseOverrideArray_SetGlobalPoseOverride(ActGlobalPoseOverrideArrayData& o, int32_t index,
                                                      const float* m, float weight);
// [driving] 0x0010f620 ActGlobalPoseOverrideArray::SetGlobalPoseOverrides(MATRIX4 *, float *)
void ActGlobalPoseOverrideArray_SetGlobalPoseOverrides(ActGlobalPoseOverrideArrayData& o, const float* m,
                                                       const float* weights);
// [driving] 0x0010f698 ActGlobalPoseOverrideArray::DoGlobalOverrides(bool espelhado, MATRIX4 *locais)
void ActGlobalPoseOverrideArray_DoGlobalOverrides(ActGlobalPoseOverrideArrayData& o, int32_t mirrored, float* local);

// O ActPoser do PS2 (0x74 bytes). Os nomes dos campos são os do port anterior (a ordem e os deslocamentos do PS2).
struct ActPoser {
    int32_t has_matrix_callback = 0;  // +0x00 o ator tem a função da matriz (as mãos, os passageiros)
    int32_t mirrored = 0;             // +0x04
    uint32_t skeleton = 0;            // +0x08 (p32 ActSkeletonData)
    float time = 0, next_time = 0, previous_time = 0, length = 0, end_time = 0;  // +0x0c..+0x1c
    uint32_t animation = 0;           // +0x20 (p32 ActAnimGroup)
    uint32_t events = 0;              // +0x24 (p32 ActEvents: a tabela dos tratadores)
    uint32_t matrices = 0;            // +0x28 (p32 ActPoseMatrices)
    float fps = 20.0f;                // +0x2c
    float seconds_per_frame = 0.05f;  // +0x30 (0x3d4ccccd)
    float frame_time = 0;             // +0x34 o tempo de quadro do ActManager
    float time_scale = 1.0f;          // +0x38
    uint32_t zero4 = 0;               // +0x3c (p32 float[4]: 0, 0, 0, 0)
    uint32_t one4 = 0;                // +0x40 (p32 float[4]: 1, 1, 1, 1)
    uint32_t crossfade = 0;           // +0x44 o ActCrossFadeBlendData (0x130; não portado)
    uint32_t ik = 0;                  // +0x48 (p32 ActIKSolverArrayData)
    uint32_t overrides = 0;           // +0x4c (p32 ActGlobalPoseOverrideArrayData)
    int32_t suppress_translation = 0; // +0x50
    int32_t mode = 0;                 // +0x54 1 normal, 2 crossfade, 3 manual
    uint32_t manual[7] = {};          // +0x58 o ActPoserManualParams (o modo 3)

    // 0x00112928 ActPoser::ActPoser(ActSkeleton *, ActEvents *, bool espelhado, bool com função): fps 20,
    // 1/20, a escala do tempo 1, o tempo de quadro do ActManager (`frame_time`: o GetFoundationVideoModeRate do
    // ActManager::StartUp, segundos por quadro do jogo), os vetores, o ActPoseMatrices com as globais, o IK e as
    // sobreposições vazios.
    ActPoser(ActSkeleton* skeleton, uint32_t events, int32_t mirrored, int32_t has_callback, float frame_time);
    // Do port (os testes, o --actor do nf_driving): sem eventos, sem espelho, sem função.
    ActPoser(ActSkeleton* skeleton, float frame_time) : ActPoser(skeleton, 0, 0, 0, frame_time) {}
    ActPoser(const ActPoser&) = delete;
    ActPoser& operator=(const ActPoser&) = delete;
    // 0x00112a70 ActPoser::~ActPoser
    ~ActPoser();

    ActSkeletonData& skel() const { return *nf::p32_ptr<ActSkeletonData>(skeleton); }
    ActPoseMatrices& pm() const { return *nf::p32_ptr<ActPoseMatrices>(matrices); }
    ActAnimGroup* group() const { return animation ? nf::p32_ptr<ActAnimGroup>(animation) : nullptr; }
    // As matrizes globais dos ossos (o ActPoseMatrices +0x100).
    float* current() const { return nf::p32_ptr<float>(pm().globals); }
    // A PoseToWorld (o +0xc0 do ActPoseMatrices).
    float* pose_to_world() const { return pm().pose_to_world; }

    // [driving] 0x00112b58 ActPoser::GetBoneIndex
    int GetBoneIndex(const char* name) const { return ActSkeleton_GetBoneIndex(skel(), name); }
    // 0x00112b78 ActPoser::GetRootBonePosOri
    void GetRootBonePosOri(float* out) const;
    // 0x00112c18 ActPoser::GetWeaponBonePosOri(int, MATRIX4 &): a arma 0 (+0x1c do esqueleto) ou 1 (+0x20)
    void GetWeaponBonePosOri(int weapon, float* out) const;
    // 0x00112cc0 ActPoser::ChangeAnimationOrigin(COORD3 &)
    void ChangeAnimationOrigin(const float* origin);
    // 0x00112ce8 ActPoser::GetAnimationOrigin(COORD3 &)
    void GetAnimationOrigin(float* out) const;
    // 0x00112d10 ActPoser::SetAnimationOrigin(COORD3 &)
    void SetAnimationOrigin(const float* origin);
    // 0x00112d50 ActPoser::GetInitialTbOu(MATRIX4 &)
    void GetInitialTbOu(float* out) const;
    // 0x00112d80 ActPoser::SetInitialTbOu(MATRIX4 &)
    void SetInitialTbOu(const float* m);
    // 0x00112dc0 ActPoser::Rotate(float voltas): a rotação em y à esquerda da matriz inicial
    void Rotate(float turns);
    // 0x00112ef0 ActPoser::RotateX(float voltas)
    void RotateX(float turns);
    // 0x00113020 ActPoser::CalculateFinalCorrectionMatrix
    void CalculateFinalCorrectionMatrix();
    // 0x00113068 ActPoser::SnapMatrixLevelWithGround(MATRIX4 &)
    static void SnapMatrixLevelWithGround(float* m);
    // 0x001130f8 ActPoser::CalcSnapAndCorrectionMatrices(MATRIX4 &)
    void CalcSnapAndCorrectionMatrices(const float* m);
    // 0x001133d0 ActPoser::SetNormalAnimation(ActAnimGroup *, float): `time` em segundos.
    void SetNormalAnimation(ActAnimGroup* group, float time);
    // 0x00113430 ActPoser::SetNormalAnimation(ActAnimGroup *, float, MATRIX4 &)
    void SetNormalAnimation(ActAnimGroup* group, float time, const float* m);
    // 0x00113ae8 ActPoser::DoSkeletonPose
    void DoSkeletonPose();
    // 0x00113b48 ActPoser::DoMainPose (o modo 1; os modos 2 e 3 contam em actor_stats())
    void DoMainPose();
    // 0x00114180 ActPoser::DoIK (os solvers não são portados: contados)
    void DoIK();
    // 0x001141e8 ActPoser::TransformLocalPoseToWorldPose
    void TransformLocalPoseToWorldPose();
    // 0x00114218 ActPoser::Skin: o EAGLAnimationBuffer (16 floats por osso) em `skin` (o fSkinnedTransforms).
    void Skin(float* skin) const;
    // 0x00114250 ActPoser::DoEventPose (o modo 1 e o 3; o 2 conta em actor_stats())
    void DoEventPose();
    // 0x001143b8 ActPoser::DoInitialPoses
    void DoInitialPoses();
    // 0x001145c0 ActPoser::AdvanceTime (o modo 1; o 2 terminaria o crossfade, contado)
    void AdvanceTime();
    // 0x001146b8 ActPoser::FrameStep
    float FrameStep() const;
    // 0x0010ac58 ActActor::IsAnimationDone (sobre o poser do ator)
    bool IsAnimationDone() const;
};
static_assert(sizeof(ActPoser) == 0x74);

// O que a camada dos atores conta (o port).
struct ActorStats {
    uint64_t ik_solvers = 0;       // ActIKSolverArray::Solve (não portado)
    uint64_t pose_overrides = 0;   // ActGlobalPoseOverrideArray::DoGlobalOverrides (as chamadas)
    uint64_t crossfade = 0;        // os modos 2 e 3 do DoMainPose/DoEventPose/AdvanceTime (não portados)
    uint64_t events_fired = 0;     // os tratadores chamados pelo DoEventPose
    uint64_t guards = 0;           // memória inválida no PS2 (um grupo sem animação no CalcSnap...)
};
ActorStats& actor_stats();
void reset_actor_stats();

}  // namespace drv::anim
