// A câmera do jogador das missões de carro (driving/camera): propriedades independentes do leitor de INI do DAFI (as
// seções e chaves sem diferença de caixa, os comentários depois do ';', o valor vazio que engole a linha seguinte), do
// LoadFile sobre um camera.ini montado à mão (um modo Heli com dois braços, as contagens e os padrões), da lista de
// carros, do ponto no triângulo, do referencial de um segmento e do cruzamento com um plano, do ruído (contínuo e
// limitado) e da câmera seguindo um corpo (o olho atrás e acima da âncora, à distância do braço). A conferência bit a
// bit contra o código original fica no nf_driving_check (parte 8).

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "test_framework.h"

using namespace drv;

namespace {
bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }
}  // namespace

TEST(driving_dafi_sections_keys_and_quirks) {
    IniFile ini;
    ini.open(bytes("[Global]\nkA = 1.5 // comentário\n  kB=7;\n\n[Outra]\nchave =\nlinha = 3\nfim = 2\n"));
    CHECK(ini.dafi().section_count() == 2);
    CHECK(std::strcmp(ini.dafi().section_by_index(1), "Outra") == 0);
    CHECK(ini.section_exists("global") && !ini.section_exists("Nenhuma"));
    // O número do começo do valor (o "// comentário" fica no texto); o ';' termina o valor.
    CHECK(ini.read_float("GLOBAL", "ka", 0.0f) == 1.5f);
    CHECK(ini.read_integer("Global", "kB", 0) == 7);
    CHECK(std::strcmp(ini.read_string("Global", "kB", ""), "7") == 0);
    CHECK(ini.read_float("Global", "kC", 9.0f) == 9.0f);
    CHECK(ini.read_integer("Nenhuma", "kA", -3) == -3);
    // "chave =" sem valor engole a linha seguinte (a quebra vira espaço): "linha" deixa de ser chave.
    CHECK(std::strcmp(ini.read_string("Outra", "chave", "x"), "linha = 3") == 0);
    CHECK(ini.read_integer("Outra", "linha", -1) == -1);
    CHECK(ini.read_integer("Outra", "fim", 0) == 2);
    // Ler de novo a mesma seção (o buffer já recortado) dá o mesmo.
    CHECK(ini.read_float("Global", "kA", 0.0f) == 1.5f);
    CHECK(dafi_guards() == 0);
    // Um texto depois do ';' vira uma "linha" sem '=': o PS2 gira para sempre; o port pula e conta.
    IniFile bad;
    bad.open(bytes("[S]\na = 1;texto\nb = 2\n"));
    CHECK(bad.read_integer("S", "b", 0) == 2);
    CHECK(dafi_guards() == 1);
}

TEST(driving_camera_load_file_heli_mode) {
    const std::string text =
        "[Global]\nkMaxTumble = 7\nkTransRate = 0.5\n"
        "[CarroHeliCam0]\ndefaultCamera = 1\ncar = Vanquish,DB5\nshake = 1\nsmoothTrans = 1\nlookBack = 1\n"
        "Min_Rate = 0.06\nMax_Rate = 0.25\nFallback_Factor = 0.05\nMax_Fallback = 1.2\nMaxVertigoUphill = 3\n"
        "[CarroHeliCam0:Arm0]\nHeli_Height = 0.85\nHeli_Distance = -5.1\nAnchor_Y = 0.3\n"
        "[CarroHeliCam0:Arm1]\narmTransition = 30\nHeli_Distance = -9\n"
        "[OutroHeliCam]\ncar = tank\n"
        "[CarroBumperCam]\nforwardArm.z = 1.5\nbackwardsArm.panUp = 2\n";
    IniFile ini;
    ini.open(bytes(text));
    CameraTables t;
    CameraCarInfo car;
    car.type = "vanquish";
    t.load(ini, car);
    CHECK(t.loaded());
    CHECK(t.tables.mode_count == 2 && t.tables.heli_count == 1 && t.tables.bumper_count == 1);
    CHECK(t.tables.default_index == 0 && t.tables.tumble_index == -1);
    const CameraModeInfo& m = t.mode(0);
    CHECK(m.type == kCamHeli && m.index == 0 && m.shake == 1 && m.smooth_trans == 1 && m.default_fov == 33.0f);
    CHECK(m.update.fn == kCamUpdateMomentumHeli && m.update.index == -1);
    // A entrada extra no fim é uma cópia da primeira.
    CHECK(std::memcmp(&t.mode(2), &t.mode(0), sizeof(CameraModeInfo)) == 0);
    const HeliCamInfo& h = t.heli(0);
    CHECK(h.min_rate == 0.06f && h.max_fallback == 1.2f && h.check_collisions == 1 && h.rigid_arm == 0);
    CHECK(h.max_vertigo_uphill == 1.0f);  // limitado a 0..1
    CHECK(h.arm_count == 2);
    const HeliArmInfo* arms = nf::raw(h.arms);
    CHECK(arms[0].height == 0.85f && arms[0].distance == -5.1f && arms[0].anchor.offset[1] == 0.3f);
    CHECK(arms[0].arm_transition == 1 && arms[1].arm_transition == 30);  // o padrão é o smoothTrans do modo
    CHECK(t.mode(1).type == kCamBumper && nf::raw(t.tables.bumpers)[0].forward_arm[2] == 1.5f);
    CHECK(nf::raw(t.tables.bumpers)[0].backwards_arm[3] == 2.0f);
    CHECK(t.constants.max_tumble == 7 && t.constants.trans_rate == 0.5f && t.constants.max_collision == 0x32);
}

TEST(driving_camera_car_list_and_geometry) {
    // A primeira ocorrência do nome na lista, seguida de ' ', ',' ou do fim.
    CHECK(CameraCarListContains("vanquish", "vanquish,vanquishalps"));
    CHECK(CameraCarListContains("vanquishalps", "vanquish,vanquishalps"));
    // Só a primeira ocorrência conta: "vanquish" acha "vanquishalps" antes e o 'a' depois recusa.
    CHECK(!CameraCarListContains("vanquish", "vanquishalps,vanquish"));
    CHECK(!CameraCarListContains("db5", "db"));
    char list[] = "Z8,DB5";
    CameraCarInfo car;
    car.type = "db5";
    CHECK(CheckCameraAgainstCar(list, car) && std::strcmp(list, "z8,db5") == 0);
    CHECK(CameraClamp(-2.0f, 0.0f, 1.0f) == 0.0f && CameraClamp(0.5f, 0.0f, 1.0f) == 0.5f);
    // O ponto no triângulo (o plano xz: a normal maior em y).
    const float a[4] = {0, 0, 0, 1}, b[4] = {4, 0, 0, 1}, c[4] = {0, 0, 4, 1};
    const float in[4] = {1, 0, 1, 1}, out[4] = {3, 0, 3, 1};
    CHECK(CameraPointInTriangle(in, a, b, c) && !CameraPointInTriangle(out, a, b, c));
    // O referencial de um segmento: ortonormal, o y ao longo de a - b, a origem em a.
    const float p0[3] = {1, 5, 2}, p1[3] = {1, 1, 2};
    float m[16];
    CHECK(WWorldMath_MakeSegSpaceMatrix(p0, p1, m));
    CHECK(near(m[5], 1.0f, 1e-6f) && near(m[12], 1.0f, 0) && near(m[13], 5.0f, 0) && m[15] == 1.0f);
    for (int r = 0; r < 3; ++r)
        CHECK(near(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2], 1.0f, 1e-5f));
    CHECK(!WWorldMath_MakeSegSpaceMatrix(p0, p0, m));
    // O segmento de y = 5 a y = 1 corta o plano y = 2 em t = 0,75.
    const float on[3] = {0, 2, 0}, n[3] = {0, 1, 0};
    float hit[3], t = 0;
    CHECK(WWorldMath_IntersectSegPlane(p0, p1, on, n, hit, t));
    CHECK(near(t, 0.75f, 1e-6f) && near(hit[1], 2.0f, 1e-6f));
    // O ruído: limitado e contínuo.
    Noise_Init();
    float prev = Noise_Noise1(0.0f), worst = 0;
    for (int i = 1; i < 2000; ++i) {
        const float v = Noise_Noise1(float(i) * 0.01f);
        CHECK(std::fabs(v) <= 1.5f);
        worst = std::fmax(worst, std::fabs(v - prev));
        prev = v;
    }
    CHECK(worst < 0.1f);
}

// A câmera seguindo um corpo andando em linha reta (sem mundo: sem colisão): o olho fica atrás da âncora, à distância
// do braço mais o recuo pela velocidade, e acima; a matriz é ortonormal e olha para a âncora.
TEST(driving_camera_follows_a_moving_body) {
    const std::string text =
        "[CarroHeliCam0]\ndefaultCamera = 1\nMin_Rate = 0.06\nMax_Rate = 0.25\nSpeed_Rate_Diff = 0.00166667\n"
        "Fallback_Factor = 0.05\nMax_Fallback = 1.2\ncheckCollisions = 0\n"
        "[CarroHeliCam0:Arm0]\nHeli_Height = 1.0\nHeli_Distance = -6\n";
    IniFile ini;
    ini.open(bytes(text));
    CameraTables* tables = new CameraTables();
    CameraCarInfo car;
    car.type = "vanquish";
    tables->load(ini, car);
    Simulation& sim = simulation();
    simulation_reset(60);
    RigidBody* rb = GetRigidBody(sim, 0);
    *rb = RigidBody{};
    rb->info = GetRigidBodyInfo(sim, 0);
    RigidBodyInfo* info = nf::raw(rb->info);
    *info = RigidBodyInfo{};
    for (int i = 0; i < 4; ++i) info->orient[i][i] = 1.0f;
    rb->vel[2] = 20.0f;
    CameraAnchor* anchor = new CameraAnchor();
    anchor->kind = 1;
    anchor->body = 0;
    CameraContext ctx;
    ctx.sim = &sim;
    ctx.cars = {anchor};
    PlayerCamera* cam = new PlayerCamera();
    cam->construct(*tables, ctx);
    for (int f = 0; f < 240; ++f) {
        rb->pos[2] += 20.0f / 60.0f;
        ++sim.steps;
        cam->update(ctx);
    }
    CHECK(cam->d.mode == 0 && cam->heli_updates == 240);
    // Atrás (z menor), acima, à distância do braço (6 m) mais o recuo (20 x 0,05 = 1 m), em linha.
    const float back = rb->pos[2] - cam->d.eye[2];
    CHECK(back > 6.0f && back < 7.5f);
    CHECK(near(cam->d.eye[0], 0.0f, 1e-3f) && cam->d.eye[1] > 0.5f && cam->d.eye[1] < 1.5f);
    const float* m = cam->d.matrix;
    CHECK(near(m[8] * m[8] + m[9] * m[9] + m[10] * m[10], 1.0f, 1e-4f) && m[10] > 0.95f);  // olha para a frente
    CHECK(near(m[12], cam->d.eye[0], 0) && near(m[14], cam->d.eye[2], 0));
    CHECK(cam->d.fov == 33.0f);
    delete cam;
    delete anchor;
    delete tables;
}
