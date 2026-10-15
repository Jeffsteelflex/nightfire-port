#pragma once
// As tabelas da câmera do jogador do DRIVING.ELF e quem as preenche: RCameraIniLoader::LoadFile (0x1a49e0) lê
// data/render/camera.ini (pelo IniFiles, driving/camera/dafi.h) uma vez por missão. Cada seção é um modo de câmera, o
// tipo achado no nome ("Bumper", "Heli", "Fixed", "Spline", "Ellipse", "Dashboard", "Tumble", "WorldAnim",
// "RelativeAnim", "AIPathAnim", "Collision", "AutoDrive"; "Global", "Debug" e ":Arm" ficam de fora), e só entra se a lista
// "car" da seção tiver o carro do jogador (ou se não houver lista); as seções "<seção>:Arm<n>" são os braços de um modo
// Heli ou AutoDrive; [Global] as constantes. Cada modo tem uma entrada na tabela de modos (mais uma no fim, cópia da
// primeira) e a maioria um registro na tabela do seu tipo.
//
// Os leiautes são os do PS2 (os globais de 0x3339b0..0x333a12, 0x333610..0x33368c e 0x4244b0..0x4244fb, as tabelas que
// o operator new aloca), para conferir byte a byte contra o original no interpretador (nf_driving_check, parte 8). O que
// o original deixa sem escrever (os campos que o LoadFile não lê, os braços montados na pilha) fica zero aqui e fora da
// comparação. Nomes de campo: as chaves do arquivo; os nomes das estruturas e dos campos sem chave são do nightfire-cxbx
// (NightfireResearch: CameraIniLoader.h, leitura do Xbox; o leiaute do PS2 difere no ponteiro para membro de 8 bytes).

#include <cstdint>
#include <string>
#include <vector>

#include "driving/camera/dafi.h"
#include "platform/ptr32.h"

namespace drv {

// As funções de atualização dos modos (RPlayerCamera::Update*Cam): o endereço no PS2 é o que o ponteiro para membro
// guarda, e o port despacha por ele.
enum CameraUpdateFn : uint32_t {
    kCamUpdateMomentumHeli = 0x1b2b48,
    kCamUpdateTumble = 0x1b3788,
    kCamUpdateNothing = 0x1b3780,  // Collision
    kCamUpdateBumper = 0x1b3b40,
    kCamUpdateDashboard = 0x1b3ce0,
    kCamUpdateSpline = 0x1b4430,
    kCamUpdateEllipse = 0x1b4c30,
    kCamUpdateFixed = 0x1b5120,
    kCamUpdateWorldAnimation = 0x1b53c0,
    kCamUpdateRelativeAnimation = 0x1b5998,
    kCamUpdateAIPathAnimation = 0x1b6160,
    kCamUpdateAutoDrive = 0x1b72f0,
};

// O tipo do modo (CameraModeInfo::type)
enum CameraModeType : uint16_t {
    kCamBumper = 0x001,
    kCamHeli = 0x002,
    kCamSpline = 0x004,
    kCamEllipse = 0x008,
    kCamFixed = 0x010,
    kCamTumble = 0x020,
    kCamAnimation = 0x040,
    kCamAIPathAnimation = 0x080,
    kCamCollision = 0x100,
    kCamAutoDrive = 0x200,
    kCamDashboard = 0x400,
};

// Um ponteiro para função membro do GCC 2.95 (8 bytes): o ajuste do this, o índice na tabela virtual (-1: não virtual)
// e o endereço da função.
struct CameraPtmf {
    int16_t delta;
    int16_t index;
    uint32_t fn;
};
static_assert(sizeof(CameraPtmf) == 8, "um ponteiro para membro tem 8 bytes");

// Um modo (0x1c bytes)
struct CameraModeInfo {
    uint8_t tumble;          // +0x00
    uint8_t shake;           // +0x01
    int8_t smooth_trans;     // +0x02 (também o armTransition padrão dos braços)
    uint8_t look_back;       // +0x03
    int8_t index;            // +0x04 o registro na tabela do tipo (0 nos tipos sem tabela)
    uint8_t selectable;      // +0x05
    uint8_t lerp_rotation;   // +0x06
    uint8_t interior_view;   // +0x07
    int16_t cam_id;          // +0x08
    uint16_t type;           // +0x0a CameraModeType
    float default_fov;       // +0x0c
    float explosion_shake_scale;  // +0x10
    CameraPtmf update;       // +0x14
};
static_assert(sizeof(CameraModeInfo) == 0x1c, "um modo tem 28 bytes");

// Anchor_X/Y/Z, Anchor_Slide_Dist, Anchor_Slide_Rate, Anchor_Slide_Rec_Rate (RWorldCamera::ReadAnchorInfo)
struct CameraAnchorInfo {
    float offset[3];
    float slide_dist;
    float slide_rate;
    float slide_rec_rate;
};
static_assert(sizeof(CameraAnchorInfo) == 0x18, "a âncora tem seis floats");

struct BumperCamInfo {  // 0x40
    float forward_arm[4];    // +0x00 x, y, z, panUp
    float backwards_arm[4];  // +0x10
    CameraAnchorInfo anchor; // +0x20
    uint8_t unknown38[8];
};
static_assert(sizeof(BumperCamInfo) == 0x40, "bumper");

struct HeliArmInfo {  // 0x40 ("<seção>:Arm<n>")
    CameraAnchorInfo anchor; // +0x00
    uint32_t weapons[2];     // +0x18 a máscara do ResolveWeaponNames
    int32_t arm_transition;  // +0x20
    uint8_t unknown24[0xc];  // (lixo da pilha no PS2)
    float sideways;          // +0x30 Heli_Sideways
    float height;            // +0x34 Heli_Height
    float distance;          // +0x38 Heli_Distance
    uint8_t unknown3c[4];
};
static_assert(sizeof(HeliArmInfo) == 0x40, "braço");

struct HeliCamInfo {  // 0x4c
    float min_rate;          // +0x00 Min_Rate
    float max_rate;          // +0x04
    float speed_rate_diff;   // +0x08
    float height_factor;     // +0x0c
    float fallback_factor;   // +0x10
    uint8_t unknown14[4];
    float tumble_arm_scale;  // +0x18
    float max_fallback;      // +0x1c
    float vertigo_lerp;      // +0x20
    int32_t arm_count;       // +0x24
    uint8_t rigid_arm;       // +0x28
    uint8_t check_collisions; // +0x29
    uint8_t unknown2a[2];
    P32<HeliArmInfo> arms;   // +0x2c
    float max_vertigo_downhill;  // +0x30 (0..1)
    float max_vertigo_uphill;    // +0x34 (0..1)
    float noise_pace;        // +0x38
    float noise_amount;      // +0x3c
    float noise_frequency;   // +0x40
    float up_rate;           // +0x44
    float look_up;           // +0x48
};
static_assert(sizeof(HeliCamInfo) == 0x4c, "heli");

struct SplineCamInfo {  // 0x30
    CameraAnchorInfo anchor;
    float spline_speed;      // +0x18
    float height_offset;     // +0x1c
    float max_spline_cam_dist;  // +0x20
    float exp_life_time;     // +0x24
    float min_zoom_dist;     // +0x28
    float min_fov;           // +0x2c
};
static_assert(sizeof(SplineCamInfo) == 0x30, "spline");

struct EllipseCamInfo {  // 0x2c
    CameraAnchorInfo anchor;
    float x_rad;             // +0x18
    float z_rad;             // +0x1c
    int32_t facets;          // +0x20 (múltiplo de 4 para baixo)
    int32_t height_count;    // +0x24
    P32<int8_t> heights;     // +0x28 "heights", cada um dobrado (no máximo 30)
};
static_assert(sizeof(EllipseCamInfo) == 0x2c, "ellipse");

struct FixedCamInfo {  // 0x28
    CameraAnchorInfo anchor;
    float min_zoom_dist;     // +0x18
    float min_fov;           // +0x1c
    float max_zoom_dist;     // +0x20
    float rot_speed;         // +0x24
};
static_assert(sizeof(FixedCamInfo) == 0x28, "fixed");

struct DashboardCamInfo {  // 0xc0 (o quarto float dos vetores é zero)
    float forward_arm[4];    // +0x00
    float backwards_arm[4];  // +0x10
    float force_scale[4];    // +0x20
    float force_max[4];      // +0x30
    float torque_scale[4];   // +0x40
    float torque_max[4];     // +0x50
    float force_pace;        // +0x60
    float torque_pace;       // +0x64
    float forward_pitch;     // +0x68
    float forward_yaw;       // +0x6c
    float intertia_scale;    // +0x70 (a grafia do arquivo)
    float intertia_min;      // +0x74
    float intertia_max;      // +0x78
    float steer_scale;       // +0x7c
    float steer_max;         // +0x80
    float steer_pace;        // +0x84
    float glance_scale;      // +0x88
    float glance_max;        // +0x8c
    float glance_pace;       // +0x90
    float noise_amount;      // +0x94
    float noise_frequency;   // +0x98
    float vertigo_lerp;      // +0x9c
    float max_vertigo_downhill;  // +0xa0
    float max_vertigo_uphill;    // +0xa4
    CameraAnchorInfo anchor; // +0xa8
};
static_assert(sizeof(DashboardCamInfo) == 0xc0, "dashboard");

// Um braço do AutoDrive (0x100 bytes; os ângulos em voltas, os limites de inclinação em senos)
struct AutoDriveArmInfo {
    float rotation[4];       // +0x00 (0, 0, 0, 1)
    float rotation_from[4];  // +0x10
    float rotation_to[4];    // +0x20
    float rel_pos[3];        // +0x30
    uint8_t unknown3c[4];
    float max_deadzone_pitch;    // +0x40 (/2π)
    float max_deadzone_yaw;      // +0x44
    float max_autoaim_pitch;     // +0x48
    float max_autoaim_yaw;       // +0x4c
    float autoaim_interpol_speed;  // +0x50
    uint32_t allow_deadzone;     // +0x54
    float aim_fov;               // +0x58
    float min_fov;               // +0x5c
    uint32_t preserve_transform; // +0x60
    uint32_t has_rest_pos;       // +0x64
    uint32_t lock_arm_to_car;    // +0x68
    float rest_span;             // +0x6c a volta até o repouso (o dobro, depois de começar; escrito em jogo)
    float rest_interpol_fall_scale;  // +0x70
    int32_t rest_start;          // +0x74 o passo do começo da volta ao repouso (escrito em jogo)
    float rest_cur;              // +0x78 a inclinação corrente do repouso (voltas; escrito em jogo)
    float norm_cursor_move_speed;     // +0x7c (x π / 2)
    float norm_cursor_end_move_speed; // +0x80
    float target_cursor_move_speed;   // +0x84
    float target_cursor_end_move_speed;  // +0x88
    CameraAnchorInfo anchor;     // +0x8c
    uint32_t weapons[2];         // +0xa4
    float min_yaw[5];            // +0xac
    float max_yaw[5];            // +0xc0
    float min_pitch[5];          // +0xd4
    float max_pitch[5];          // +0xe8
    float rest_pitch;            // +0xfc a inclinação de repouso (voltas: o arco seno de min_pitch[0]; escrito ao entrar
                                 // no modo)
};
static_assert(sizeof(AutoDriveArmInfo) == 0x100, "autodrive");

// Os globais 0x3339b0..0x333a13
struct CameraTableGlobals {
    P32<BumperCamInfo> bumpers;         // 0x3339b0
    P32<DashboardCamInfo> dashboards;   // 0x3339b4
    P32<HeliCamInfo> helis;             // 0x3339b8
    P32<CameraModeInfo> modes;          // 0x3339bc (mode_count + 1)
    P32<SplineCamInfo> splines;         // 0x3339c0
    P32<EllipseCamInfo> ellipses;       // 0x3339c4
    P32<FixedCamInfo> fixeds;           // 0x3339c8
    uint8_t unknown_cc[4];
    // (os valores iniciais do .data)
    float autodrive_prev_weap_quat[4] = {0, 0, 0, 1};  // 0x3339d0 fAutoDrivePrevWeapQuat (o braço anterior)
    int32_t autodrive_prev_weapon;      // 0x3339e0 fAutoDrivePrevWeapIndex
    float autodrive_weap_trans_length;  // 0x3339e4 fAutoDriveWeapTransLength (voltas x 2 da troca de braço)
    float autodrive_weap_interp_time = 1.0f;  // 0x3339e8 fAutoDriveWeapInterpolTime (0..1)
    int32_t heli_count;                 // 0x3339ec
    int32_t spline_count;               // 0x3339f0
    int32_t ellipse_count;              // 0x3339f4
    int32_t fixed_count;                // 0x3339f8
    int32_t bumper_count;               // 0x3339fc
    int32_t dashboard_count;            // 0x333a00
    int32_t mode_count;                 // 0x333a04
    int8_t tumble_index;                // 0x333a08
    int8_t world_anim_index;            // 0x333a09
    int8_t rel_anim_index;              // 0x333a0a
    int8_t ai_path_anim_index;          // 0x333a0b
    int8_t collision_index;             // 0x333a0c
    int8_t autodrive_index;             // 0x333a0d
    int8_t pause_index;                 // 0x333a0e
    int8_t missile_index;               // 0x333a0f
    int8_t cinematic_mode_index;        // 0x333a10
    int8_t cinematic_heli_index;        // 0x333a11
    int8_t default_index;               // 0x333a12
    uint8_t unknown_13;
};
static_assert(sizeof(CameraTableGlobals) == 0x64, "0x3339b0..0x333a13");

// As constantes de [Global] (0x333610..0x33368f)
struct CameraConstants {
    int32_t max_tumble;                 // 0x333610 kMaxTumble
    int32_t max_collision;              // 0x333614
    int32_t default_transition;         // 0x333618 kDefualtTransition
    float trans_rate;                   // 0x33361c
    float trans_rate_lerp_rate;         // 0x333620
    float bumper_y_lerp_rate;           // 0x333624
    float collide_radius;               // 0x333628
    float spline_offset_lerp;           // 0x33362c
    float explosion_scale;              // 0x333630
    float explosion_max_shake;          // 0x333634
    int32_t explosion_shake_period;     // 0x333638
    float explosion_after_shock;        // 0x33363c
    float explosion_time_scale;         // 0x333640
    uint8_t unknown44[0xc];
    float object_radius_ex[4];          // 0x333650 kCameraObjectRadiusEx.x/y/z (o w não é lido)
    float object_sphere_rad;            // 0x333660
    float collision_min_rate;           // 0x333664
    float collision_max_rate;           // 0x333668
    float collision_rate_diff;          // 0x33366c
    float zoom_inc_speed;               // 0x333670
    float centering_speed;              // 0x333674
    int32_t auto_drive_latency;         // 0x333678
    int32_t weapon_arm_change_latency;  // 0x33367c
    int32_t missile_cam_latency;        // 0x333680
    float weapon_animation_length;      // 0x333684 kfWeaponAnimationLength
    float weapon_animation_amplitude;   // 0x333688
    float max_autoaim_distance;         // 0x33368c
};
static_assert(sizeof(CameraConstants) == 0x80, "0x333610..0x33368f");

// Os globais 0x4244b0..0x4244fb
struct CameraModeGlobals {
    CameraAnchorInfo tumble_anchor;     // 0x4244b0
    float vector_lerp;                  // 0x4244c8 (Tumble)
    float rel_pos_lerp;                 // 0x4244cc
    CameraAnchorInfo anim_anchor;       // 0x4244d0 (WorldAnim, RelativeAnim, AIPathAnim)
    float max_weap_trans_time;          // 0x4244e8 (AutoDrive)
    float inertia;                      // 0x4244ec
    float zoom_factor;                  // 0x4244f0
    int32_t autodrive_arm_count;        // 0x4244f4
    P32<AutoDriveArmInfo> autodrive_arms;  // 0x4244f8
};
static_assert(sizeof(CameraModeGlobals) == 0x4c, "0x4244b0..0x4244fb");

// O carro do jogador como CheckCameraAgainstCar o consulta (PBondCar: a classe na entrada +0x98 da tabela virtual, o tipo
// na +0x80, o tipo secundário na +0x290) e o tipo forçado do global 0x33196c.
struct CameraCarInfo {
    std::string type;          // "vanquish"
    std::string secondary;     // vazio: nenhum
    int32_t car_class = 0;
    std::string override_type; // vazio: nenhum
};

// Uma vaga do gerenciador de armas (o global 0x33a530, +0x10: 32 vagas de 0x54 bytes; o número em +0, o nome em +4),
// o que ResolveWeaponNames consulta. O gerenciador de armas não foi portado: sem vagas, as máscaras ficam zero.
struct CameraWeaponSlot {
    int32_t weapon = 0;
    std::string name;
};

class CameraTables {
public:
    // [driving] 0x1a49e0 RCameraIniLoader::LoadFile(void): uma vez (nada se as tabelas já existirem).
    void load(IniFile& ini, const CameraCarInfo& car, const std::vector<CameraWeaponSlot>& weapons = {});
    bool loaded() const { return !modes_.empty(); }
    void reset();

    CameraTableGlobals tables{};
    CameraConstants constants{};
    CameraModeGlobals modes{};
    // As seções de tipo desconhecido (o PS2 escreve uma entrada a mais que a tabela tem; o port não) e outros casos.
    uint32_t guards = 0;

    const CameraModeInfo& mode(int i) const { return nf::raw(tables.modes)[i]; }
    const HeliCamInfo& heli(int i) const { return nf::raw(tables.helis)[i]; }

private:
    // O armazenamento (na arena em 64 bits: os ponteiros das tabelas são P32).
    std::vector<CameraModeInfo> modes_;
    std::vector<BumperCamInfo> bumpers_;
    std::vector<HeliCamInfo> helis_;
    std::vector<SplineCamInfo> splines_;
    std::vector<EllipseCamInfo> ellipses_;
    std::vector<FixedCamInfo> fixeds_;
    std::vector<DashboardCamInfo> dashboards_;
    std::vector<std::vector<HeliArmInfo>> heli_arms_;
    std::vector<std::vector<int8_t>> heights_;
    std::vector<AutoDriveArmInfo> autodrive_arms_;
};

// [driving] 0x1d69c8 RWorldCamera::ReadAnchorInfo(IniFiles *, char *, RWorldCameraAnchorInfo *)
void ReadAnchorInfo(IniFile& ini, const char* section, CameraAnchorInfo* info);
// [driving] 0x1a6cf8 RCameraIniLoader::CheckCameraAgainstCar(char *): a lista vai para minúsculas no lugar.
bool CheckCameraAgainstCar(char* cars, const CameraCarInfo& car);
// [driving] 0x1a6c68 (sem nome): o nome é a primeira ocorrência na lista, seguida de ' ', ',' ou do fim.
bool CameraCarListContains(const char* name, const char* list);
// [driving] 0x1a6e18 RCameraIniLoader::ResolveWeaponNames(char *, unsigned int *)
void ResolveWeaponNames(const char* names, uint32_t mask[2], const std::vector<CameraWeaponSlot>& weapons);
// [driving] 0x1a49b0 (sem nome): o valor entre lo e hi (c.lt.s: um NaN passaria; o PS2 não os tem).
float CameraClamp(float v, float lo, float hi);

}  // namespace drv
