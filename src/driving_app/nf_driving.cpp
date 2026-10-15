// nf_driving.exe: a ferramenta de desenvolvimento das missões de carro (docs/driving.md seção 10).
//
//   nf_driving --data <pasta com DRIVING\> --viv MIS01.VIV --model vanquish [--yaw 0.6] [--pitch 0.25] [--dist 1.0]
//              [--width 1920 --height 1080] [--screenshot out\shots\drv\vanquish.png] [--no-spec] [--no-refl]
//              [--fallback off]                      um carro girando numa janela, ou uma captura (PNG + .txt)
//   nf_driving ... --model vanquish --ground          o carro assentado num chão plano pela física, com as rodas, as
//                                                     lâmpadas e a sombra do RVehicle (o fundo faz de chão)
//   nf_driving --data <pasta> --track paris_mis01 --car vanquish --drive [--car-pos x,y,z,giro] [--drive-input ...]
//              [--no-traffic] [--fire-trigger N[@s]]  o carro dirigível na pista, com o tráfego, os gatilhos e a missão
//              [--free-drive] [--mission]             (o SMissionManager, as regras e o HUD mínimo; docs/driving.md seção
//              [--difficulty 0|1|2] [--no-hud]        19): a missão começa onde o jogo começa; --free-drive (ou --car-pos)
//                                                     dirige sem ela, --mission a liga mesmo com --car-pos. N: um gatilho
//                                                     disparado na largada ou aos s segundos (depuração;
//                                                     NF_DRV_TRIGGERS=1 lista)
//              [--play-effect NOME@s[@m]]             (depuração, seção 25) um efeito da galeria m metros à frente
//              [--spawn-explosion s[@m[@dano]]]       uma explosão (o ESpawnExplosion) m metros à frente
//   nf_driving --data <pasta> --viv MIS01.VIV --actor 2.h_pmask.h_pmask.t_paris.95 [--anim srmg:<nome|n>]
//              [--seconds s] [--screenshot ...]       um personagem posado e tocando uma animação (seção 20);
//                                                     --list-anims lista os bancos e as animações do arquivo
//   nf_driving --check --data <pasta>                 todos os carros e personagens de todas as pistas: o que carrega
//   nf_driving --census --data <pasta>                os 115 render methods: famílias, microprogramas distintos
//
// Sem nada do sistema: o ponto de entrada é o do backend (platform/entry_win32.cpp), a janela e o renderizador vêm de
// platform/window.h e drv_renderer.h, o relógio e o console de platform/os.h.

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "driving/sound/audio.h"
#include "driving/sound/audio_stream.h"
#include "drv_fx.h"
#include "driving/sound/snd_spu.h"
#include "platform/audio_out.h"
#include "platform/crt_compat.h"
#include "platform/entry.h"
#include "platform/fmath.h"
#include "platform/gamepad.h"
#include "platform/os.h"
#include "platform/ps2_float.h"

#include "driving/ai/ai_weapons.h"
#include "driving/ai/element_controller.h"
#include "driving/ai/ground_vehicle.h"
#include "driving/anim/act_actor.h"
#include "driving/anim/actor.h"
#include "driving/anim/eagl_anim.h"
#include "driving/ai/road_nav.h"
#include "driving/ai/road_network.h"
#include "driving/ai/road_spawn.h"
#include "driving/ai/sim_random.h"
#include "driving/bigf.h"
#include "driving/camera/camera_aim.h"
#include "driving/camera/camera_ini.h"
#include "driving/camera/dafi.h"
#include "driving/camera/player_camera.h"
#include "platform/arena.h"
#include "driving/car_model.h"
#include "driving/game/ai_character.h"
#include "driving/game/ai_passengers.h"
#include "driving/game/explosion.h"
#include "driving/game/missile.h"
#include "driving/game/shells.h"
#include "driving/game/weapons.h"
#include "driving/render/anim_engine.h"
#include "driving/carp.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_model.h"
#include "driving/eagl_state.h"
#include "driving/game/attributes.h"
#include "driving/game/bond_car.h"
#include "driving/game/input.h"
#include "driving/game/scheduler.h"
#include "driving/game/smackable.h"
#include "driving/game/triggers.h"
#include "driving_app/drive_actors.h"
#include "driving/light_manager.h"
#include "driving/physics/collide.h"
#include "driving/physics/rigid_body.h"
#include "driving/physics/simulation.h"
#include "driving/physics/world_collision.h"
#include "driving/game/events.h"
#include "driving/game/feedback.h"
#include "driving/game/camera_events.h"
#include "driving/game/heli_section.h"
#include "driving/game/hud.h"
#include "driving/game/mission.h"
#include "driving/game/rail.h"
#include "driving/game/input_config.h"
#include "driving/physics/collision.h"
#include "driving/real/vu0_math.h"
#include "driving/mat_bond.h"
#include "driving/render/font.h"
#include "driving/render/path_engine.h"
#include "driving/render/shadow.h"
#include "driving/render/vehicle.h"
#include "driving/render_method.h"
#include "driving/track.h"
#include "driving_app/drv_renderer.h"
#include "platform/keys.h"
#include "platform/window.h"

namespace {

struct Args {
    std::string data = ".";
    std::string viv = "MIS01.VIV";
    std::string model = "vanquish";
    std::string screenshot;
    int width = 1280, height = 720;
    float yaw = 0.6f, pitch = 0.22f, dist = 1.0f, fov = 40.0f;
    bool check = false, census = false, anim_dump = false, spec = true, refl = true, fallback = true, spin = true;
    std::string adapter;
    int part = -1;  // só esta peça (depuração)
    bool verify = false;  // roda a família no interpretador da VU1 e compara (mat_car_verify.h)
    int bench_frames = 0;  // com --screenshot: mede tantos quadros fora da tela antes da captura
    std::string check_viv;  // --check só neste arquivo
    int view = 1;   // a lista de desenho (vd#n do CARP; 0: todas as instâncias)
    std::string track_census;  // --track-census <pista>: os métodos dos modelos da pista
    std::string track;         // --track <pista>: a pista com o carro
    std::string car;           // --car <carro> (com --track; vazio: sem carro)
    bool viv_given = false;
    bool cam_given = false;
    float cam[5] = {0, 0, 0, 0, 0};          // --cam x,y,z,giro,inclinação (radianos)
    float car_pos[4] = {0, 0, 0, 0};         // --car-pos x,y,z,giro
    bool no_sky = false;                     // --no-sky (depuração)
    bool probe = false;                      // --probe: as instâncias em volta da câmera (depuração)
    bool drive = false;                      // --drive: o carro dirigível (com --track e --car)
    std::string drive_input;                 // --drive-input "W:2 W+A:1.5 ...": as teclas por segundos (captura)
    float seconds = 0;                       // --seconds: o tempo simulado antes da captura (0: o do roteiro)
    bool car_pos_given = false;
    bool no_cull = false;                    // --no-cull: o corte de antes (só o volume de visão), sem a árvore e as cortinas
    bool ground = false;                     // --ground: o carro parado sobre um chão plano, com a sombra (captura)
    bool no_traffic = false;                 // --no-traffic: sem o tráfego nem os gatilhos (para comparar o custo)
    // Depuração do porte da IA (docs/driving.md seção 21): --car-reset s@x,y,z,giro recoloca o carro do jogador
    // (PBondCar::ResetCar) no tempo s; --autopilot e[@x,z[,v];...] dirige o carro do jogador pelo controle do PS2 (o
    // direcional e a pressão do X e do quadrado, como a entrada do roteiro) pelos pontos e depois atrás do veículo do
    // elemento e da IA, pelo rastro dele (v: a velocidade até o ponto, 28 m/s sem ela); --emp-hit s faz no tempo s o que o Q-Pulse do jogador faz no carro mais perto à
    // frente (PBondCar::AttackWithEmp: as armas do carro do jogador são de outro módulo).
    float car_reset_at = -1.0f;
    float car_reset[4] = {0, 0, 0, 0};
    int autopilot = -1;
    std::vector<std::pair<float, float>> autopilot_points;
    std::vector<float> autopilot_speeds;  // a velocidade pedida até cada ponto (x,z,v; padrão 28 m/s)
    std::vector<int> gunner;              // --gunner e1[,e2...][@altura]: o atirador do AutoDrive (seção 24)
    float gunner_height = 0.0f;
    bool gunner_tyres = false;            // --gunner-tyres: mira no pneu mais perto da câmera (o BLOWOUT das regras)
    float gunner_limit = 0.6f;            // --gunner-limit graus: o erro abaixo do qual o atirador aperta o R1 (seção 26)
    bool autopilot_fire = false;          // --autopilot-fire: o piloto atira a arma especial (L1) no veículo seguido
    // --shot-look e@d,h,giro (depuração, seção 26): o desenho olha o veículo do elemento e de d metros, h acima, pelo
    // rumo `giro` (graus, no mundo); a câmera do jogo não muda (só a vista do desenho).
    int shot_look_element = -1;
    float shot_look[3] = {8.0f, 3.0f, 0.0f};
    std::vector<float> emp_hits;
    // --damage-hit s@dano: no tempo s, um tiro no carro do jogador (PBondCar::ApplyDamage do tipo 2, de cima para o
    // centro): o que as armas dos inimigos fariam (as armas são de outro módulo).
    std::vector<std::pair<float, float>> damage_hits;
    // --kill-element e@s: no tempo s, o EKillAIElement do elemento e (o que o jogo faz com os alvos da seção do
    // helicóptero quando ela é jogada; o --fire-trigger 727@1 a pula e os deixa vivos).
    std::vector<std::pair<int, float>> kill_elements;
    std::vector<std::string> play_effects;  // --play-effect NOME@s[@m] (depuração, repetível; drv_fx.h)
    std::vector<std::string> spawn_explosions;  // --spawn-explosion s[@m[@dano]] (depuração, repetível; drv_fx.h)
    std::vector<std::pair<int, float>> fire_triggers;  // --fire-trigger N[@s] (depuração, repetível): dispara o gatilho
                                                       // N na largada (ou aos s segundos do relógio do jogo)
    bool free_drive = false;                 // --free-drive: o --drive sem a missão
    bool mission = false;                    // --mission: a missão mesmo com --car-pos
    int difficulty = 1;                      // --difficulty: 0 fácil, 1 normal, 2 difícil (o bloco da troca)
    bool no_hud = false;                     // --no-hud: a missão sem o HUD desenhado
    std::string audio_dump;                  // --audio-dump <wav>: o som do --drive num WAV (as capturas são mudas)
    bool no_audio = false;                   // --no-audio: a janela sem som
    std::string actor;                       // --actor "2.h_pmask.h_pmask.t_paris.95": um personagem (seção 20)
    std::string anim;                        // --anim <banco>[:<nome ou índice>]: a animação do personagem
    bool list_anims = false;                 // --list-anims: os bancos e as animações do .VIV
};

// "a,b,c" em n floats.
bool parse_floats(const std::string& s, float* out, int n) {
    const char* c = s.c_str();
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(c, &end);
        if (end == c) return false;
        c = end;
        if (*c == ',') ++c;
    }
    return true;
}

std::wstring widen(const std::string& s) { return nf::os::from_utf8(s); }

void console() { nf::os::attach_parent_console(); }

// A conferência contra o microprograma original (mat_car_verify.h) numa linha.
std::string verify_line(const drv::CarVerifyStats& v) {
    char b[512];
    std::snprintf(b, sizeof b,
                  "conferencia lotes %d falhas %d sem_pacote %d vertices %d stq_dif %d (max %u ulps, %.2g) cor_dif %d "
                  "(max %d) xyz_dif %d (max xy %d z %d) nevoa_dif %d adc_recorte %d adc_a_mais %d adc_costas %d variantes %d%s%s",
                  v.batches, v.failed_runs, v.missing_kicks, v.vertices, v.stq_diff, v.stq_max_ulps,
                  double(v.stq_max_abs), v.rgba_diff, v.rgba_max, v.xyz_diff, v.xyz_max, v.z_max, v.fog_diff, v.adc_clip,
                  v.adc_extra, v.adc_back, v.variant_batches, v.first_error.empty() ? "" : " | ", v.first_error.c_str());
    return b;
}

// O relatório de um carro (linhas "chave valor"), também gravado junto da captura.
std::string report(const drv::CarModel& car, const Args& a, const drv::DrvRenderStats* rs) {
    const drv::CarModelStats& s = car.stats();
    std::string out;
    char b[512];
    std::snprintf(b, sizeof b, "carro %s arquivo %s pecas %d grupos %d primitivas %d lotes %d vertices %d triangulos %d\n",
                  a.model.c_str(), a.viv.c_str(), s.models, s.groups, s.geoprims, s.batches, s.vertices, s.triangles);
    out += b;
    std::snprintf(b, sizeof b, "caixa x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f\n", s.bbox_min[0], s.bbox_max[0],
                  s.bbox_min[1], s.bbox_max[1], s.bbox_min[2], s.bbox_max[2]);
    out += b;
    out += "metodos";
    for (const auto& m : s.method_batches) {
        const bool family =
            drv::car_method_layout(m.first.c_str()) != nullptr || drv::bond_method_layout(m.first.c_str()) != nullptr;
        std::snprintf(b, sizeof b, " %s=%d/%d%s", m.first.c_str(), m.second, s.method_triangles.at(m.first),
                      family ? "" : "(substituto)");
        out += b;
    }
    out += "\n";
    std::snprintf(b, sizeof b,
                  "substitutos lotes %d triangulos %d | desenhos %zu texturas %zu | carregador mensagens %d "
                  "runtime_alloc_nao_portados %d | vif erros %d | ops_nao_portados %d | trocas_de_microprograma %d\n",
                  s.fallback_batches, s.fallback_triangles, car.draws().size(), car.textures().size(), s.loader_messages,
                  s.runtime_alloc_unported, s.vif_errors, s.unported_ops, s.microcode_switches);
    out += b;
    std::set<int> used;
    for (const drv::CarDraw& d : car.draws())
        for (int t : d.texture)
            if (t >= 0) used.insert(t);
    out += "texturas_usadas";
    for (int t : used) {
        std::snprintf(b, sizeof b, " %s(%s %dx%d)", car.textures()[size_t(t)].name.c_str(),
                      car.textures()[size_t(t)].source.c_str(), car.textures()[size_t(t)].width,
                      car.textures()[size_t(t)].height);
        out += b;
    }
    out += "\n";
    for (size_t i = 0; i < car.parts().size(); ++i) {
        const drv::CarPart& p = car.parts()[i];
        std::snprintf(b, sizeof b, "peca %zu %s grupos %d primitivas %d vertices %u caixa %.3f,%.3f,%.3f..%.3f,%.3f,%.3f\n",
                      i, p.name.c_str(), p.groups, p.geoprims, p.vertex_count, p.bbox_min[0], p.bbox_min[1],
                      p.bbox_min[2], p.bbox_max[0], p.bbox_max[1], p.bbox_max[2]);
        out += b;
    }
    std::snprintf(b, sizeof b, "instancias %d sem_modelo %d pecas_sem_instancia %d vista %d", car.stats().instances,
                  car.stats().instances_without_model, car.stats().parts_without_instance, a.view);
    out += b;
    for (int v = 1; v < car.view_count(); ++v) {
        std::snprintf(b, sizeof b, " vd#%04x=%zu", v, car.view_draw_list(unsigned(v)).size());
        out += b;
    }
    out += "\n";
    for (size_t i = 0; i < car.instances().size(); ++i) {
        const drv::CarInstance& in = car.instances()[i];
        std::snprintf(b, sizeof b, "instancia %zu peca %d as#%04x pos %.3f,%.3f,%.3f x %.2f,%.2f,%.2f bandeiras %08x %08x\n", i,
                      in.part, in.piece & 0xFFFF, in.matrix[12], in.matrix[13], in.matrix[14], in.matrix[0], in.matrix[1],
                      in.matrix[2], in.flags, in.extra[1]);
        out += b;
    }
    if (rs) {
        std::snprintf(b, sizeof b,
                      "desenho instancias %d chamadas %d passadas1 %d passadas2 %d passadas3 %d passadas4 %d substitutos "
                      "%d | camera giro %.3f inclinacao %.3f distancia %.3f fov %.1f | %dx%d | adaptador %s\n",
                      rs->instances, rs->draw_calls, rs->pass_calls[0], rs->pass_calls[1], rs->pass_calls[2],
                      rs->pass_calls[3], rs->fallback_calls, a.yaw, a.pitch, a.dist, a.fov, a.width, a.height, rs->adapter.c_str());
        out += b;
    }
    if (a.verify) {
        out += verify_line(car.verify_stats()) + "\n";
        if (car.bond_verify_stats().batches) out += "mundo " + verify_line(car.bond_verify_stats()) + "\n";
    }
    for (const std::string& m : car.messages()) out += "mensagem " + m + "\n";
    // Um desenho por linha: método, peça, triângulos, as texturas e o estado do GS de cada passada.
    for (size_t i = 0; i < car.draws().size(); ++i) {
        const drv::CarDraw& d = car.draws()[i];
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (uint32_t k = 0; k < d.index_count; ++k) {
            const float* pos = d.family == 2   ? car.char_vertices()[car.char_indices()[d.first_index + k]].pos
                               : d.family == 1 ? car.bond_vertices()[car.bond_indices()[d.first_index + k]].pos
                                               : car.vertices()[car.indices()[d.first_index + k]].pos;
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], pos[c]);
                hi[c] = std::max(hi[c], pos[c]);
            }
        }
        std::snprintf(b, sizeof b, "desenho %zu %s peca %d tris %u props %.2f %.2f %.2f %.2f caixa %.2f,%.2f,%.2f..%.2f,%.2f,%.2f",
                      i, d.method.c_str(), d.part, d.index_count / 3, d.props[0], d.props[1], d.props[2], d.props[3],
                      lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        out += b;
        for (int p = 0; p < drv::kCarMaxPasses; ++p) {
            if (!((d.passes >> p) & 1)) continue;
            const drv::eagl::GsRegs& g = d.pass[p];
            const char* tn = d.texture[p] >= 0 ? car.textures()[size_t(d.texture[p])].name.c_str() : "-";
            std::snprintf(b, sizeof b, " | p%d tex %s prim %03llx test %05llx alpha %02llx tfx %d tcc %d clamp %llx", p + 1,
                          tn, (unsigned long long)g.prim, (unsigned long long)(g.test & 0x7ffff),
                          (unsigned long long)(g.alpha & 0xff), int((g.tex0 >> 35) & 3), int((g.tex0 >> 34) & 1),
                          (unsigned long long)(g.clamp & 0xf));
            out += b;
        }
        out += "\n";
    }
    return out;
}

// O relatório sem as linhas por peça e por desenho (essas ficam só no .txt).
std::string summary(const std::string& rep) {
    std::string out;
    size_t at = 0;
    while (at < rep.size()) {
        size_t end = rep.find('\n', at);
        if (end == std::string::npos) end = rep.size();
        const std::string line = rep.substr(at, end - at);
        const bool detail = line.rfind("peca ", 0) == 0 || line.rfind("instancia ", 0) == 0 ||
                            (line.rfind("desenho ", 0) == 0 && line.size() > 8 && line[8] >= '0' && line[8] <= '9');
        if (!detail) out += line + "\n";
        at = end + 1;
    }
    return out;
}

// O disco do jogador (--data: a pasta com DRIVING\\, uma imagem .iso, uma unidade; nf::open_disc).
std::shared_ptr<nf::DiscSource> open_data(const Args& a) {
    std::string error;
    std::shared_ptr<nf::DiscSource> disc = nf::open_disc(std::wstring(a.data.begin(), a.data.end()), &error);
    if (disc == nullptr) std::fprintf(stderr, "não abri os dados em %s: %s\n", a.data.c_str(), error.c_str());
    return disc;
}

int run_census(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    drv::Bigf misc;
    if (disc == nullptr || !misc.open(*disc, "DRIVING/MISC.VIV")) {
        std::fprintf(stderr, "não abri MISC.VIV em %s\n", a.data.c_str());
        return 1;
    }
    drv::eagl::reset_registries();
    drv::eagl::register_eagl_types();
    std::vector<drv::RenderMethodInfo> all;
    std::vector<std::vector<uint8_t>> keep;
    std::vector<std::unique_ptr<drv::eagl::DynamicLoader>> loaders;
    for (const char* obj : {"eaglrm", "bondrm"}) {
        const int idx = misc.find(std::string("data\\render\\") + obj + ".o");
        std::vector<uint8_t> b;
        misc.read(idx, b);
        keep.push_back(std::vector<uint8_t>(b.size() + 16));
        uint8_t* p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(keep.back().data()) + 15) & ~uintptr_t(15));
        std::memcpy(p, b.data(), b.size());
        loaders.push_back(std::make_unique<drv::eagl::DynamicLoader>(p, uint32_t(b.size()), nullptr));
        auto c = drv::render_methods_census(*loaders.back(), obj);
        all.insert(all.end(), c.begin(), c.end());
    }
    std::map<uint64_t, std::vector<std::string>> by_code;
    std::map<std::string, std::vector<const drv::RenderMethodInfo*>> by_family;
    for (const auto& m : all) {
        by_code[m.code_hash].push_back(m.name);
        by_family[m.family].push_back(&m);
    }
    std::printf("render methods %zu, microprogramas distintos %zu\n", all.size(), by_code.size());
    for (const auto& f : by_family) {
        std::set<uint64_t> codes;
        int instr = 0;
        for (const auto* m : f.second) {
            if (codes.insert(m->code_hash).second) instr += m->instructions;
        }
        std::printf("familia %-22s metodos %3zu microprogramas %3zu instrucoes_distintas %5d:", f.first.c_str(),
                    f.second.size(), codes.size(), instr);
        for (const auto* m : f.second) std::printf(" %s", m->name.c_str());
        std::printf("\n");
    }
    for (const auto& m : all) {
        std::printf("metodo %-36s %-6s instr %4d max %3d xgkick %2d recorte %d efu %d pele %d familia %-20s abs",
                    m.name.c_str(), m.object.c_str(), m.instructions, m.max_verts, m.xgkicks, m.clip ? 1 : 0,
                    m.efu ? 1 : 0, m.skin ? 1 : 0, m.family.c_str());
        for (int r : m.abs_reads) std::printf(" %x", r);
        std::printf(" | parametros");
        for (const auto& p : m.params) std::printf(" %s", p.c_str());
        const auto& same = by_code[m.code_hash];
        if (same.size() > 1) {
            std::printf(" | mesmo_codigo");
            for (const auto& s : same)
                if (s != m.name) std::printf(" %s", s.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

// Os personagens de um arquivo de missão (a fase 0 do docs/driving.md, seção 16): os modelos (data\actors\models\*.dat
// com o .rel: o EAGL::DynamicLoader partido, os __Model desenhados pelo DMA e o VIF como as peças do carro, os métodos de
// pele no substituto), as armas (data\actors\weapons\*.crp, como um carro), os esqueletos (data\actors\skeleton\*.o: os
// __Bone) e os bancos de animação (data\actors\anims\*.dat com o .rel).
struct ActorTotals {
    int models = 0, batches = 0, fallback = 0, triangles = 0, weapons = 0, skeletons = 0, bones = 0, anim_banks = 0;
    int anim_symbols = 0, loader_messages = 0, failures = 0, shared_weapons = 0;
    int char_batches = 0, char_guards = 0, palette_matrices = 0;
    drv::CarVerifyStats char_verify;  // a família de pele (mat_char.h) contra o microprograma, com --verify
    // A animação (seção 20): as animações dos bancos pela fábrica, os canais por tipo, as avaliações e as guardas; os
    // esqueletos pelo ActSkeleton (com o osso "Root.Root").
    int anims = 0, anim_evals = 0, anim_guards = 0, anim_unknown = 0, anim_events = 0, skeleton_roots = 0;
    int event_anims = 0, mixed_anims = 0;
    std::map<int, int> channel_types;
    std::map<std::string, int> methods;
    std::map<std::string, int> messages;  // as mensagens dos modelos dos personagens (o carregador, o desenho)
};

void add_verify(drv::CarVerifyStats& total, const drv::CarVerifyStats& v) {
    total.batches += v.batches;
    total.failed_runs += v.failed_runs;
    total.missing_kicks += v.missing_kicks;
    total.vertices += v.vertices;
    total.stq_diff += v.stq_diff;
    total.rgba_diff += v.rgba_diff;
    total.xyz_diff += v.xyz_diff;
    total.fog_diff += v.fog_diff;
    total.adc_clip += v.adc_clip;
    total.adc_extra += v.adc_extra;
    total.adc_back += v.adc_back;
    total.variant_batches += v.variant_batches;
    total.stq_max_ulps = std::max(total.stq_max_ulps, v.stq_max_ulps);
    total.stq_max_abs = std::max(total.stq_max_abs, v.stq_max_abs);
    total.rgba_max = std::max(total.rgba_max, v.rgba_max);
    total.xyz_max = std::max(total.xyz_max, v.xyz_max);
    total.z_max = std::max(total.z_max, v.z_max);
    if (total.first_error.empty()) total.first_error = v.first_error;
}

// A animação de um banco (seção 20): cada animação criada pela fábrica e avaliada numa pose de 128 ossos em tempos do
// começo ao fim, para trás e fora da faixa (o que as conferências da parte 16 também fazem contra o original): os tipos
// de canal (a raiz e os subcanais dos compostos), as guardas, os tipos que a fábrica não porta e os eventos.
void scan_anim_bank(drv::Bigf& arc, const std::string& name, ActorTotals& t) {
    drv::anim::AnimBank bank;
    std::string err;
    if (!bank.load(arc, name, err)) {
        std::printf("FALHOU banco %s: %s\n", name.c_str(), err.c_str());
        ++t.failures;
        return;
    }
    const drv::anim::AnimStats before = drv::anim::anim_stats();
    std::vector<float> pose(size_t(128) * drv::anim::kPoseFloats, 0.0f);
    for (int i = 0; i < bank.num_anims(); ++i) {
        const uint32_t d = bank.anim(i);
        if (!d) continue;
        const uint16_t type = *nf::p32_ptr<const uint16_t>(d);
        t.channel_types[type]++;
        bool event_anim = type == drv::anim::kFnRawEvent;
        if (type == drv::anim::kFnCompound) {
            const int n = *nf::p32_ptr<const uint16_t>(d + 8);
            int events = 0;
            for (int k = 0; k < n; ++k) {
                const uint16_t st = *nf::p32_ptr<const uint16_t>(*nf::p32_ptr<const uint32_t>(d + 0xc + 4u * uint32_t(k)));
                t.channel_types[st]++;
                if (st == drv::anim::kFnRawEvent) ++events;
            }
            // As animações de eventos (a segunda do ActAnimGroup) só têm canais de eventos; as de pose, nenhum.
            if (events == n) ++t.event_anims;
            else if (events) ++t.mixed_anims;
            event_anim = events > 0;
        }
        std::unique_ptr<drv::anim::FnAnim> a = drv::anim::NewFnAnim(type);
        ++t.anims;
        if (!a || event_anim) continue;  // as de eventos não se avaliam numa pose (o DoMainPose só avalia a de pose)
        a->SetAnimMemoryMap(d);
        float len = 0.0f;
        a->GetLength(len);
        const float times[] = {0.0f, 0.5f, 1.0f, 2.25f, len * 0.5f, len - 1.0f, len - 0.25f, len, len + 3.0f,
                               len * 0.25f, 0.75f, -1.0f, len * 0.75f, 0.0f};
        float prev = 0.0f;
        for (float tm : times) {
            a->Eval(prev, tm, pose.data(), int(pose.size()));
            prev = tm;
            ++t.anim_evals;
        }
    }
    const drv::anim::AnimStats& after = drv::anim::anim_stats();
    t.anim_guards += int(after.guards - before.guards);
    t.anim_unknown += int(after.unknown_types - before.unknown_types);
    t.anim_events += int(after.events - before.events);
}

void check_actors(const nf::DiscSource& disc, const std::string& viv, drv::Bigf& arc, ActorTotals& t, bool verify) {
    auto lower_of = [](std::string s) {
        for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto starts = [](const std::string& s, const char* p) { return s.rfind(p, 0) == 0; };
    auto ends = [](const std::string& s, const char* x) {
        const size_t n = std::strlen(x);
        return s.size() >= n && s.compare(s.size() - n, n, x) == 0;
    };
    std::vector<std::string> ssh;
    for (const drv::BigfEntry& e : arc.entries()) {
        const std::string l = lower_of(e.name);
        if ((starts(l, "data\\actors\\textures\\") && ends(l, ".ssh")) || l == "data\\render\\actors.ssh")
            ssh.push_back(e.name);
    }
    // Um objeto inteiro ou partido pelo DynamicLoader (alinhado a 16, como o FastAlloc do jogo), sem resolver nada de
    // fora: os símbolos definidos por tipo e as mensagens do carregador.
    auto load_raw = [&](const std::string& dat_key, const std::string& rel_key, std::map<std::string, int>& types,
                        int& messages) {
        std::vector<uint8_t> dat, rel;
        const int di = arc.find(dat_key);
        if (di < 0) return false;
        arc.read(di, dat);
        if (!rel_key.empty()) {
            const int ri = arc.find(rel_key);
            if (ri < 0) return false;
            arc.read(ri, rel);
        }
        std::vector<uint8_t> store(dat.size() + 16), rstore(rel.size() + 16);
        uint8_t* d = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(store.data()) + 15) & ~uintptr_t(15));
        uint8_t* r = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(rstore.data()) + 15) & ~uintptr_t(15));
        std::memcpy(d, dat.data(), dat.size());
        if (!rel.empty()) std::memcpy(r, rel.data(), rel.size());
        drv::eagl::reset_registries();
        drv::eagl::diagnostics().clear();
        auto none = [](const char*, bool& found) -> uint32_t {
            found = false;
            return 0;
        };
        std::unique_ptr<drv::eagl::DynamicLoader> loader =
            rel_key.empty() ? std::make_unique<drv::eagl::DynamicLoader>(d, uint32_t(dat.size()), none)
                            : std::make_unique<drv::eagl::DynamicLoader>(d, uint32_t(dat.size()), r, false, none);
        if (!loader->ok()) return false;
        for (int i = 0; i < loader->GetCount(); ++i) {
            const drv::eagl::SymbolInfo s = loader->GetSymbol(i);
            if (s.defined) types[s.type ? s.type : ""]++;
        }
        messages = int(drv::eagl::diagnostics().size());
        return true;
    };
    for (const drv::BigfEntry& e : arc.entries()) {
        const std::string l = lower_of(e.name);
        const std::string base = drv::bigf_base_name(e.name);
        if (starts(l, "data\\actors\\models\\") && ends(l, ".dat")) {
            const std::string rel = e.name.substr(0, e.name.size() - 4) + ".rel";
            drv::CarModel m;
            m.set_verify(verify);
            std::string err;
            if (!m.load_eagl(disc, viv, e.name, rel, ssh, base, err)) {
                std::printf("FALHOU personagem %s %s: %s\n", viv.c_str(), base.c_str(), err.c_str());
                ++t.failures;
                continue;
            }
            const drv::CarModelStats& s = m.stats();
            std::printf("ok personagem %-10s %-12s pecas %3d primitivas %4d lotes %5d triangulos %6d substitutos %4d "
                        "carregador %d vif %d ops %d nulos %d texturas %zu\n",
                        viv.c_str(), base.c_str(), s.models, s.geoprims, s.batches, s.triangles, s.fallback_batches,
                        s.loader_messages, s.vif_errors, s.unported_ops, s.dma_null_sources, m.textures().size());
            for (const auto& op : s.unported_by_op) std::printf("  (op 0x%x pulada %d)", op.first, op.second);
            if (!s.unported_by_op.empty()) std::printf("\n");
            if (s.char_batches || s.char_guards)
                std::printf("  pele: lotes %d paletas %d guardas %d\n", s.char_batches, s.palette_matrices, s.char_guards);
            if (verify && m.char_verify_stats().batches)
                std::printf("  pele %s\n", verify_line(m.char_verify_stats()).c_str());
            t.char_batches += s.char_batches;
            t.char_guards += s.char_guards;
            t.palette_matrices += s.palette_matrices;
            add_verify(t.char_verify, m.char_verify_stats());
            ++t.models;
            t.batches += s.batches;
            t.fallback += s.fallback_batches;
            t.triangles += s.triangles;
            t.loader_messages += s.loader_messages;
            for (const auto& mb : s.method_batches) t.methods[mb.first] += mb.second;
            for (const std::string& msg : m.messages()) t.messages[msg]++;
        } else if (starts(l, "data\\actors\\weapons\\") && ends(l, ".crp")) {
            drv::CarModel m;
            std::string err;
            const std::string tex = e.name.substr(0, e.name.size() - 4) + ".ssh";
            if (!m.load_carp(disc, viv, e.name, {tex}, base, err)) {
                if (m.parts().empty() && m.carp_dir().group(drv::carp_tag("Shar"))) {
                    // O CARP só com o objeto articulado e o grupo 'Shar': o modelo é o de outra arma (sniper.crp).
                    std::printf("ok arma %-10s %-12s sem modelo proprio (grupo Shar)\n", viv.c_str(), base.c_str());
                    ++t.shared_weapons;
                    continue;
                }
                std::printf("FALHOU arma %s %s: %s\n", viv.c_str(), base.c_str(), err.c_str());
                ++t.failures;
                continue;
            }
            const drv::CarModelStats& s = m.stats();
            std::printf("ok arma %-10s %-12s pecas %3d instancias %3d lotes %5d triangulos %6d substitutos %4d "
                        "carregador %d vif %d\n",
                        viv.c_str(), base.c_str(), s.models, s.instances, s.batches, s.triangles, s.fallback_batches,
                        s.loader_messages, s.vif_errors);
            ++t.weapons;
            t.batches += s.batches;
            t.fallback += s.fallback_batches;
            t.triangles += s.triangles;
            for (const auto& mb : s.method_batches) t.methods[mb.first] += mb.second;
        } else if ((starts(l, "data\\actors\\skeleton\\") && ends(l, ".o")) ||
                   (starts(l, "data\\actors\\anims\\") && ends(l, ".dat"))) {
            const bool skel = ends(l, ".o");
            std::map<std::string, int> types;
            int messages = 0;
            const std::string rel = skel ? std::string() : e.name.substr(0, e.name.size() - 4) + ".rel";
            if (!load_raw(e.name, rel, types, messages)) {
                std::printf("FALHOU %s %s %s\n", skel ? "esqueleto" : "animacoes", viv.c_str(), base.c_str());
                ++t.failures;
                continue;
            }
            int total = 0;
            std::string list;
            for (const auto& kv : types) {
                total += kv.second;
                list += " " + (kv.first.empty() ? std::string("(sem tipo)") : kv.first) + "=" + std::to_string(kv.second);
            }
            std::printf("ok %s %-10s %-12s simbolos %d carregador %d |%s\n", skel ? "esqueleto" : "animacoes",
                        viv.c_str(), base.c_str(), total, messages, list.c_str());
            t.loader_messages += messages;
            if (skel) {
                ++t.skeletons;
                t.bones += types.count("Bone") ? types["Bone"] : 0;
                // O ActSkeleton (o número do nome: "4skel.o").
                drv::anim::ActSkeleton sk;
                std::string err;
                if (sk.load(arc, std::atoi(base.c_str()), err) && sk.d.root >= 0) ++t.skeleton_roots;
            } else {
                ++t.anim_banks;
                t.anim_symbols += total;
                scan_anim_bank(arc, base, t);
            }
        }
    }
}

int run_check(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    std::vector<std::string> vivs;
    if (disc != nullptr) {
        // Os .VIV de DRIVING\\, sem o diretório, na ordem do disco (a do FindFirstFile numa pasta do NTFS: por nome).
        for (const nf::DiscEntry& e : disc->list("DRIVING")) {
            std::string name = e.path.substr(e.path.find_last_of('/') + 1);
            const size_t semi = name.find(';');
            if (semi != std::string::npos) name.resize(semi);
            if (name.size() > 4 && nf::str_icmp(name.c_str() + name.size() - 4, ".VIV") == 0) vivs.push_back(name);
        }
    }
    if (vivs.empty()) {
        std::fprintf(stderr, "sem arquivos .VIV em %s\\DRIVING\n", a.data.c_str());
        return 1;
    }
    int ok = 0, fail = 0;
    drv::CarVerifyStats total;
    std::map<std::string, int> methods, fallback;
    ActorTotals actors;
    for (const std::string& viv : vivs) {
        if (!a.check_viv.empty() && nf::str_icmp(viv.c_str(), a.check_viv.c_str()) != 0) continue;
        drv::Bigf arc;
        if (!arc.open(*disc, "DRIVING/" + viv)) continue;
        for (const drv::BigfEntry& e : arc.entries()) {
            std::string lower = e.name;
            for (char& c : lower) c = char(tolower(static_cast<unsigned char>(c)));
            if (lower.find("data\\car\\model\\") == std::string::npos || lower.size() < 4 ||
                lower.compare(lower.size() - 4, 4, ".crp") != 0)
                continue;
            const std::string name = drv::bigf_base_name(e.name);
            std::fflush(stdout);
            drv::CarModel car;
            car.set_verify(a.verify);
            std::string err;
            if (!car.load(*disc, viv, name, err)) {
                std::printf("FALHOU %s %s: %s\n", viv.c_str(), name.c_str(), err.c_str());
                ++fail;
                continue;
            }
            ++ok;
            const drv::CarModelStats& s = car.stats();
            std::printf("ok %-10s %-22s pecas %3d instancias %3d primitivas %4d lotes %5d triangulos %6d substitutos "
                        "%4d carregador %d vif %d ops %d nulos %d memoria %u MB\n",
                        viv.c_str(), name.c_str(), s.models, s.instances, s.geoprims, s.batches, s.triangles,
                        s.fallback_batches, s.loader_messages, s.vif_errors, s.unported_ops, s.dma_null_sources,
                        unsigned(nf::os::private_memory() >> 20));
            if (a.verify) {
                // Um carro com referências não resolvidas (lidas como zeros) fica fora da soma: o lixo não diz nada.
                std::printf("  %s%s\n", verify_line(car.verify_stats()).c_str(),
                            s.dma_null_sources ? " (fora da soma: dados nulos)" : "");
            }
            if (a.verify && !s.dma_null_sources) {
                const drv::CarVerifyStats& v = car.verify_stats();
                total.batches += v.batches;
                total.failed_runs += v.failed_runs;
                total.missing_kicks += v.missing_kicks;
                total.vertices += v.vertices;
                total.stq_diff += v.stq_diff;
                total.rgba_diff += v.rgba_diff;
                total.xyz_diff += v.xyz_diff;
                total.fog_diff += v.fog_diff;
                total.adc_clip += v.adc_clip;
                total.adc_extra += v.adc_extra;
                total.adc_back += v.adc_back;
                total.variant_batches += v.variant_batches;
                total.stq_max_ulps = std::max(total.stq_max_ulps, v.stq_max_ulps);
                total.stq_max_abs = std::max(total.stq_max_abs, v.stq_max_abs);
                total.rgba_max = std::max(total.rgba_max, v.rgba_max);
                total.xyz_max = std::max(total.xyz_max, v.xyz_max);
                total.z_max = std::max(total.z_max, v.z_max);
                if (total.first_error.empty()) total.first_error = v.first_error;
            }
            for (const auto& m : s.method_batches) {
                methods[m.first] += m.second;
                if (!drv::car_method_layout(m.first.c_str())) fallback[m.first] += m.second;
            }
        }
        check_actors(*disc, viv, arc, actors, a.verify);
    }
    std::printf("carros carregados %d, falhas %d\n", ok, fail);
    std::printf("personagens: modelos %d (lotes %d, substitutos %d, triangulos %d), armas %d (+%d sem modelo proprio), "
                "esqueletos %d (ossos %d), bancos de animacao %d (simbolos %d), mensagens do carregador %d, falhas %d\n",
                actors.models, actors.batches, actors.fallback, actors.triangles, actors.weapons, actors.shared_weapons,
                actors.skeletons, actors.bones, actors.anim_banks, actors.anim_symbols, actors.loader_messages,
                actors.failures);
    std::printf("personagens, pele: lotes %d matrizes de paleta %d guardas %d\n", actors.char_batches,
                actors.palette_matrices, actors.char_guards);
    if (a.verify) std::printf("personagens, pele %s (todos)\n", verify_line(actors.char_verify).c_str());
    {
        std::string types;
        for (const auto& kv : actors.channel_types) types += " " + std::to_string(kv.first) + "=" + std::to_string(kv.second);
        std::printf("personagens, animacao: animacoes %d (de eventos %d, mistas %d) avaliacoes %d guardas %d "
                    "tipos_sem_porte %d eventos %d esqueletos_com_raiz %d | canais por tipo%s\n",
                    actors.anims, actors.event_anims, actors.mixed_anims, actors.anim_evals, actors.anim_guards,
                    actors.anim_unknown, actors.anim_events, actors.skeleton_roots, types.c_str());
        if (actors.anim_guards || actors.anim_unknown || actors.mixed_anims) ++fail;
    }
    {
        // As mensagens mais comuns dos modelos dos personagens (o que falta para desenhá-los).
        std::vector<std::pair<int, std::string>> top;
        for (const auto& kv : actors.messages) top.push_back({kv.second, kv.first});
        std::sort(top.begin(), top.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        for (size_t i = 0; i < top.size() && i < 12; ++i)
            std::printf("  personagens, mensagem x%d: %s\n", top[i].first, top[i].second.c_str());
    }
    fail += actors.failures;
    for (const auto& m : actors.methods) {
        methods[m.first] += m.second;
        if (!drv::car_method_layout(m.first.c_str()) && !drv::bond_method_layout(m.first.c_str()) &&
            !drv::char_method_layout(m.first.c_str()))
            fallback[m.first] += m.second;
    }
    if (a.verify) std::printf("%s (todos)\n", verify_line(total).c_str());
    // As 8 pistas (a tabela do main do DRIVING; docs/driving.md seção 1): o CARP resolvido, os modelos, as instâncias e
    // o mundo de colisão ('CDat').
    if (a.check_viv.empty()) {
        static const char* const kTracks[] = {"paris_mis01", "uw_mis11",    "junglea_mis13a", "jungleb_mis13b",
                                              "snow1a_mis3", "snow2a_mis4", "junglec_mis13c", "snow2a_race"};
        int tracks_ok = 0;
        for (const char* t : kTracks) {
            drv::Track track;
            std::string e;
            const bool loaded = track.load(*disc, t, std::string(), e);
            drv::TrackWorld world;
            const bool ground = loaded && world.init(track.root(), &e);
            if (!loaded || !ground) {
                std::printf("pista %s: FALHA %s\n", t, e.c_str());
                ++fail;
                continue;
            }
            ++tracks_ok;
            const drv::TrackStats& ts = track.stats();
            const drv::CarModelStats& ms = track.models().stats();
            uint32_t cells = 0;
            for (uint32_t c = 0; c < world.grid().rows * world.grid().columns; ++c) cells += world.node(c) != nullptr;
            std::printf("pista %-15s %s: modelos %d lotes %d substitutos %d instancias %d artigos %d | colisao %u "
                        "instancias, %u celulas com no\n",
                        t, track.viv().c_str(), ms.models, ms.batches, ms.fallback_batches, ts.instances, ts.articles,
                        world.instance_count(), cells);
        }
        std::printf("pistas carregadas %d de 8\n", tracks_ok);
    }
    // Os efeitos (docs/driving.md seção 25): a galeria e as partículas; com --verify, os dois métodos deles.
    fail += drv_fx_check(*disc, a.verify);
    std::printf("metodos (lotes):");
    for (const auto& m : methods) std::printf(" %s=%d%s", m.first.c_str(), m.second, fallback.count(m.first) ? "*" : "");
    std::printf("\n(* fora da família reproduzida: desenhados pelo substituto)\n");
    return fail ? 2 : 0;
}

bool parse(int argc, wchar_t** argv, Args& a) {
    auto narrow = [](const wchar_t* w) { return nf::os::to_utf8(w); };
    for (int i = 1; i < argc; ++i) {
        const std::string k = narrow(argv[i]);
        auto next = [&]() { return i + 1 < argc ? narrow(argv[++i]) : std::string(); };
        if (k == "--data") a.data = next();
        else if (k == "--viv") {
            a.viv = next();
            a.viv_given = true;
        }
        else if (k == "--model") a.model = next();
        else if (k == "--screenshot") a.screenshot = next();
        else if (k == "--width") a.width = std::stoi(next());
        else if (k == "--height") a.height = std::stoi(next());
        else if (k == "--yaw") a.yaw = std::stof(next());
        else if (k == "--pitch") a.pitch = std::stof(next());
        else if (k == "--dist") a.dist = std::stof(next());
        else if (k == "--fov") a.fov = std::stof(next());
        else if (k == "--check") a.check = true;
        else if (k == "--check-viv") {
            a.check = true;
            a.check_viv = next();
        }
        else if (k == "--census") a.census = true;
        else if (k == "--anim-dump") a.anim_dump = true;
        else if (k == "--no-spec") a.spec = false;
        else if (k == "--no-refl") a.refl = false;
        else if (k == "--no-fallback") a.fallback = false;
        else if (k == "--still") a.spin = false;
        else if (k == "--adapter") a.adapter = next();
        else if (k == "--part") a.part = std::stoi(next());
        else if (k == "--verify") a.verify = true;
        else if (k == "--bench-frames") a.bench_frames = std::stoi(next());
        else if (k == "--view") a.view = std::stoi(next());
        else if (k == "--track-census") a.track_census = next();
        else if (k == "--track") a.track = next();
        else if (k == "--car") a.car = next();
        else if (k == "--cam") a.cam_given = parse_floats(next(), a.cam, 5);
        else if (k == "--car-pos") a.car_pos_given = parse_floats(next(), a.car_pos, 4);
        else if (k == "--car-reset") {
            const std::string v = next();
            const size_t at = v.find('@');
            if (at != std::string::npos && parse_floats(v.substr(at + 1), a.car_reset, 4))
                a.car_reset_at = std::stof(v.substr(0, at));
        } else if (k == "--autopilot") {
            const std::string v = next();
            const size_t at = v.find('@');
            a.autopilot = std::stoi(v.substr(0, at));
            size_t p = at == std::string::npos ? v.size() : at + 1;
            while (p < v.size()) {
                size_t e = v.find(';', p);
                if (e == std::string::npos) e = v.size();
                const std::string pt = v.substr(p, e - p);
                float xz[3] = {0, 0, 28.0f};
                const bool with_speed = std::count(pt.begin(), pt.end(), ',') == 2;
                if (parse_floats(pt, xz, with_speed ? 3 : 2)) {
                    a.autopilot_points.push_back({xz[0], xz[1]});
                    a.autopilot_speeds.push_back(xz[2]);
                }
                p = e + 1;
            }
        } else if (k == "--gunner") {
            const std::string v = next();
            const size_t at = v.find('@');
            const std::string list = v.substr(0, at);
            size_t p = 0;
            while (p < list.size()) {
                size_t e = list.find(',', p);
                if (e == std::string::npos) e = list.size();
                a.gunner.push_back(std::stoi(list.substr(p, e - p)));
                p = e + 1;
            }
            if (at != std::string::npos) a.gunner_height = std::stof(v.substr(at + 1));
        } else if (k == "--gunner-tyres") {
            a.gunner_tyres = true;
        } else if (k == "--gunner-limit") {
            a.gunner_limit = std::stof(next());
        } else if (k == "--shot-look") {
            const std::string v = next();
            const size_t at = v.find('@');
            a.shot_look_element = std::atoi(v.substr(0, at).c_str());
            if (at != std::string::npos) parse_floats(v.substr(at + 1), a.shot_look, 3);
        } else if (k == "--autopilot-fire") {
            a.autopilot_fire = true;
        } else if (k == "--kill-element") {
            const std::string v = next();
            const size_t at = v.find('@');
            if (at != std::string::npos) a.kill_elements.push_back({std::stoi(v.substr(0, at)), std::stof(v.substr(at + 1))});
        } else if (k == "--emp-hit") {
            a.emp_hits.push_back(std::stof(next()));
        } else if (k == "--damage-hit") {
            const std::string v = next();
            const size_t at = v.find('@');
            if (at != std::string::npos) a.damage_hits.push_back({std::stof(v.substr(0, at)), std::stof(v.substr(at + 1))});
        }
        else if (k == "--drive") a.drive = true;
        else if (k == "--drive-input") a.drive_input = next();
        else if (k == "--seconds") a.seconds = std::stof(next());
        else if (k == "--no-sky") a.no_sky = true;
        else if (k == "--probe") a.probe = true;
        else if (k == "--no-cull") a.no_cull = true;
        else if (k == "--ground") a.ground = true;
        else if (k == "--no-traffic") a.no_traffic = true;
        else if (k == "--fire-trigger") {
            const std::string v = next();
            const size_t at = v.find('@');
            a.fire_triggers.push_back({std::stoi(v.substr(0, at)), at == std::string::npos ? 0.0f : std::stof(v.substr(at + 1))});
        }
        else if (k == "--play-effect") a.play_effects.push_back(next());
        else if (k == "--spawn-explosion") a.spawn_explosions.push_back(next());
        else if (k == "--free-drive") a.free_drive = true;
        else if (k == "--mission") a.mission = true;
        else if (k == "--difficulty") a.difficulty = std::stoi(next());
        else if (k == "--no-hud") a.no_hud = true;
        else if (k == "--audio-dump") a.audio_dump = next();
        else if (k == "--no-audio") a.no_audio = true;
        else if (k == "--actor") a.actor = next();
        else if (k == "--anim") a.anim = next();
        else if (k == "--list-anims") a.list_anims = true;
        else {
            std::fprintf(stderr, "opção desconhecida: %s\n", k.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

// O valor de uma chave "NOME=valor" de um .tun (as linhas terminam em "\r\n"; os nomes registrados têm espaços que o
// arquivo não tem: "fog START" é "fogSTART").
bool tun_floats(const std::string& text, const std::string& key, float* out, int n) {
    const size_t p = text.find(key + "=");
    if (p == std::string::npos) return false;
    const char* c = text.c_str() + p + key.size() + 1;
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        out[i] = std::strtof(c, &end);
        if (end == c) return i > 0;
        c = end;
        if (*c == ',') ++c;
    }
    return true;
}

// [driving] 0x001ac668 RFog::RFog e 0x001ac978 RFog::SetFogParams: o "Render:Fog" da pista (fog START, fog END, fog
// mode, fog Density, Fog Colour) no FogData da VU (0x48): {início, 0, fim x escala (1), densidade x 255 / (fim -
// início)}; a cor ARGB do arquivo vai para o FOGCOL. Os padrões do RFog: início 1, fim 5000, modo 3, densidade 0, cor
// 0x80386885.
void track_fog(const std::string& tun, drv::CarConstants& k, float color[3]) {
    float start = 1.0f, end = 5000.0f, density = 0.0f, argb[4] = {128, 56, 104, 133};
    tun_floats(tun, "fogSTART", &start, 1);
    tun_floats(tun, "fogEND", &end, 1);
    tun_floats(tun, "fogDensity", &density, 1);
    tun_floats(tun, "FogColour", argb, 4);
    const float fog_end = end * 1.0f;  // CalcFogEnd: o fim x a escala (+0x28, 1 fora do SetFogScale)
    k.fog[0] = start;
    k.fog[1] = 0.0f;
    k.fog[2] = fog_end;
    k.fog[3] = (density * 255.0f) / (fog_end - start);
    color[0] = argb[1];
    color[1] = argb[2];
    color[2] = argb[3];
}

std::string read_entry(drv::Bigf& arc, const std::string& key) {
    const int idx = arc.find(key);
    if (idx < 0) return std::string();
    std::vector<uint8_t> b;
    arc.read(idx, b);
    return std::string(b.begin(), b.end());
}

// Um desenho da pista na cena: o céu (passada 4) levado ao olho, sem névoa nem luzes pontuais (RSky::Draw).
// Depuração (NF_DRV_HIDE=i,j,...): instâncias da pista que não são desenhadas.
const std::set<int>& hidden_instances() {
    static std::set<int> hidden = [] {
        std::set<int> h;
        std::string env;
        if (nf::get_env_narrow("NF_DRV_HIDE", &env)) {
            size_t p = 0;
            while (p < env.size()) {
                h.insert(std::atoi(env.c_str() + p));
                const size_t c = env.find(',', p);
                if (c == std::string::npos) break;
                p = c + 1;
            }
        }
        return h;
    }();
    return hidden;
}

void push_track_draw(const drv::Track& track, const drv::Track::Draw& d, const float eye[3], drv::DrvScene& scene) {
    if (!hidden_instances().empty() && hidden_instances().count(d.instance)) return;
    const drv::TrackInstance& inst = track.instances()[size_t(d.instance)];
    drv::DrvItem it;
    it.model = &track.models();
    it.part = d.part;
    std::memcpy(it.world, inst.matrix, sizeof it.world);
    const drv::TrackArticle& art = track.articles()[size_t(inst.article)];
    if (art.render_flags & 1) it.flags |= drv::DrvItem::kNoFog;
    if (d.pass == 4) {
        it.flags |= drv::DrvItem::kNoFog | drv::DrvItem::kNoPointLights | drv::DrvItem::kSky;
        it.world[12] = eye[0];
        it.world[13] = eye[1];
        it.world[14] = eye[2];
    }
    scene.items.push_back(it);
}

// O referencial de uma câmera livre como o RCamera o guarda (as linhas: direita, cima, frente, o olho).
void free_camera_matrix(const float eye[3], const float dir[3], float m[16]) {
    float r[3] = {dir[2], 0.0f, -dir[0]};  // (0, 1, 0) x frente
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl > 0) {
        r[0] /= rl;
        r[2] /= rl;
    } else {
        r[0] = 1.0f;
    }
    const float u[3] = {dir[1] * r[2] - dir[2] * r[1], dir[2] * r[0] - dir[0] * r[2], dir[0] * r[1] - dir[1] * r[0]};
    const float rows[4][3] = {{r[0], r[1], r[2]}, {u[0], u[1], u[2]}, {dir[0], dir[1], dir[2]}, {eye[0], eye[1], eye[2]}};
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c) m[i * 4 + c] = rows[i][c];
        m[i * 4 + 3] = i == 3 ? 1.0f : 0.0f;
    }
}

// O fov do RCamera (metade do campo horizontal numa tela 4:3, em graus) para um campo vertical do PC.
float game_fov_from_vertical(float fovy_deg) {
    const double half = double(fovy_deg) * 3.14159265358979 / 360.0;
    return float(std::atan(std::tan(half) * (4.0 / 3.0)) * 180.0 / 3.14159265358979);
}

// Só do port: o PC desenha Hor+ (mais largo que 4:3 numa janela larga); o frustum 2D do corte usa o ângulo que cobre a
// janela, ou o do jogo numa janela de 4:3 ou mais estreita (o jogo tem o widescreen do RRenderer, fov x 1,25, para o
// 16:9 da TV; aqui a conta exata).
float cull_fov_for_window(float game_fov, float aspect) {
    if (aspect <= 4.0f / 3.0f) return game_fov;
    const double t = std::tan(double(game_fov) * 3.14159265358979 / 180.0) * double(aspect) * 0.75;
    return float(std::atan(t) * 180.0 / 3.14159265358979);
}

// A pista cortada como o RRenderWorldCamera::DoRender (Track::cull): o céu, as passadas 3 e 2, os objetos de cena, os
// carros (`cars`) com os objetos de cena desenhados por último, a passada 1 e a 0. Devolve a linha do relatório.
std::string push_culled_track(const drv::Track& track, const drv::Track::Culled& c, const float eye[3], bool no_sky,
                              drv::DrvScene& scene, const std::function<void()>& cars) {
    if (!no_sky)
        for (const auto& d : c.sky) push_track_draw(track, d, eye, scene);
    for (const auto& d : c.world) push_track_draw(track, d, eye, scene);
    for (const auto& d : c.scene_objects) push_track_draw(track, d, eye, scene);
    for (const auto& d : c.scene_objects_last) push_track_draw(track, d, eye, scene);
    if (cars) cars();
    for (const auto& d : c.pass1) push_track_draw(track, d, eye, scene);
    for (const auto& d : c.pass0) push_track_draw(track, d, eye, scene);
    char b[320];
    std::snprintf(b, sizeof b,
                  "corte nos %d cortinas %d testadas %d guardadas %d | ceu %zu mundo %zu objetos_de_cena %zu/%zu de %zu "
                  "passada1 %zu passada0 %zu | guardas %llu",
                  c.nodes, c.curtains, c.tested, c.kept, c.sky.size(), c.world.size(), c.scene_objects.size(),
                  c.scene_objects_last.size(), track.scene_objects().size(), c.pass1.size(), c.pass0.size(),
                  (unsigned long long)drv::world_vis_guards());
    return b;
}

// A pista com um carro: a cena que o WRender e o RSky desenham (track.h) e o carro numa posição, vistos de uma câmera.
int run_track(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    drv::Track track;
    track.set_verify(a.verify);
    std::string err;
    const uint64_t freq = nf::os::perf_frequency();
    const uint64_t t0 = nf::os::perf_counter();
    if (!track.load(*disc, a.track, a.viv_given ? a.viv : std::string(), err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    const double load_ms = 1000.0 * double(int64_t(nf::os::perf_counter() - t0)) / double(int64_t(freq));
    drv::CarModel car;
    const bool with_car = !a.car.empty();
    if (with_car && !car.load(*disc, track.viv(), a.car, err)) {
        std::fprintf(stderr, "nf_driving: o carro: %s\n", err.c_str());
        return 1;
    }
    drv::Bigf arc;
    arc.open(*disc, "DRIVING/" + track.viv());
    drv::DrvScene scene;
    track_fog(read_entry(arc, "data\\tuning\\Render\\Fog\\" + a.track + ".tun"), scene.k, scene.fog_color);
    for (int c = 0; c < 3; ++c) scene.clear[c] = scene.fog_color[c] / 255.0f;
    // As entradas por quadro do mundo: o LightSources do RLightManager e o SimStep do RTimeData (aqui o passo
    // --seconds x 60: a cena parada no tempo).
    std::memcpy(scene.light_sources, &track.lights().light_sources, sizeof scene.light_sources);
    drv::RTimeData_Update(int32_t(a.seconds * 60.0f));
    std::memcpy(scene.sim_step, drv::rtime_data().time_data, sizeof scene.sim_step);
    if (with_car) {
        // As luzes do carro (as do .tun da pista, car_model.h) e a névoa da pista.
        const drv::CarConstants& ck = car.constants();
        std::memcpy(scene.k.light_dir_t, ck.light_dir_t, sizeof ck.light_dir_t);
        std::memcpy(scene.k.light_col, ck.light_col, sizeof ck.light_col);
        std::memcpy(scene.k.ambient, ck.ambient, sizeof ck.ambient);
        std::memcpy(scene.k.refl_info2, ck.refl_info2, sizeof ck.refl_info2);
        std::memcpy(car.constants().fog, scene.k.fog, sizeof scene.k.fog);
    }
    float lo[3], hi[3];
    track.bounds(lo, hi);
    float cam[5] = {0.5f * (lo[0] + hi[0]), hi[1] + 50.0f, 0.5f * (lo[2] + hi[2]), 0.0f, -0.6f};
    if (a.cam_given) std::memcpy(cam, a.cam, sizeof cam);
    const float eye[3] = {cam[0], cam[1], cam[2]};
    const float dir[3] = {nf::fm::sin(cam[3]) * nf::fm::cos(cam[4]), nf::fm::sin(cam[4]), nf::fm::cos(cam[3]) * nf::fm::cos(cam[4])};
    const float at[3] = {eye[0] + dir[0], eye[1] + dir[1], eye[2] + dir[2]};
    drv::drv_look_at(eye, at, scene.view);
    const int w = a.width, h = a.height;
    drv::drv_perspective(a.fov, float(w) / float(h), 0.17f, 3000.0f, scene.proj);  // perto 0,17, longe 3000 (a câmera)
    scene.eye[0] = eye[0];
    scene.eye[1] = eye[1];
    scene.eye[2] = eye[2];
    scene.spec_from_view = true;
    float vp[16];
    drv::drv_mat_mul(scene.view, scene.proj, vp);
    std::vector<drv::Track::Draw> draws;
    std::string cull_line;
    auto push_car = [&]() {
        if (!with_car) return;
        float carm[16] = {nf::fm::cos(a.car_pos[3]), 0, -nf::fm::sin(a.car_pos[3]), 0, 0, 1, 0, 0,
                          nf::fm::sin(a.car_pos[3]), 0, nf::fm::cos(a.car_pos[3]), 0, a.car_pos[0], a.car_pos[1],
                          a.car_pos[2], 1};
        std::vector<uint16_t> list = car.view_draw_list(1);
        for (uint16_t ii : list) {
            if (ii >= car.instances().size() || car.instances()[ii].part < 0) continue;
            drv::DrvItem it;
            it.model = &car;
            it.part = car.instances()[ii].part;
            drv::drv_mat_mul(car.instances()[ii].matrix, carm, it.world);
            scene.items.push_back(it);
        }
    };
    if (!a.no_cull && track.has_scene_tree()) {
        // O corte do jogo (Track::cull): a câmera livre como um RCamera.
        float cm[16];
        free_camera_matrix(eye, dir, cm);
        drv::Track::Culled culled;
        track.cull(cm, cull_fov_for_window(game_fov_from_vertical(a.fov), float(w) / float(h)), false, culled);
        cull_line = push_culled_track(track, culled, eye, a.no_sky, scene, push_car);
        for (auto* v : {&culled.sky, &culled.world, &culled.scene_objects, &culled.scene_objects_last, &culled.pass1,
                        &culled.pass0})
            draws.insert(draws.end(), v->begin(), v->end());
        if (a.probe) {
            // Depuração: as instâncias com lotes dos métodos animados ou iluminados (Dynamic, Scroll2, FrameAnim).
            std::set<int> parts_kind[5];
            for (const drv::CarDraw& dd : track.models().draws())
                if (dd.family == 1 && dd.bond_kind >= drv::kBondDynamic) parts_kind[dd.bond_kind].insert(dd.part);
            for (size_t k = 0; k < track.instances().size(); ++k) {
                const drv::TrackInstance& inst = track.instances()[k];
                if (inst.article < 0) continue;
                for (int kind = drv::kBondDynamic; kind <= drv::kBondFrameAnim; ++kind) {
                    const auto& lods = track.articles()[size_t(inst.article)].lods;
                    if (lods.empty() || !parts_kind[kind].count(lods[0].part)) continue;
                    char pb[200];
                    std::snprintf(pb, sizeof pb, "\nmetodo_animado %d %s tipo %d pos %.1f,%.1f,%.1f", int(k),
                                  track.articles()[size_t(inst.article)].name.c_str(), kind, double(inst.matrix[12]),
                                  double(inst.matrix[13]), double(inst.matrix[14]));
                    cull_line += pb;
                }
            }
            // Depuração: o que o volume de visão deixaria e o corte tirou, até 100 m (com "fora_da_arvore" quando nenhum
            // nó do 'Wmap' lista a instância).
            {
                std::set<int> in_tree, kept;
                std::vector<const drv::MapNode*> stack;
                uint32_t root = 0;
                std::memcpy(&root, nf::p32_ptr<const uint8_t>(track.world_map()), 4);
                if (root) stack.push_back(nf::p32_ptr<const drv::MapNode>(root));
                while (!stack.empty()) {
                    const drv::MapNode* n = stack.back();
                    stack.pop_back();
                    for (int i = 0; i < n->instance_count; ++i) in_tree.insert(drv::MapNode_IndexList(n)[i]);
                    if (n->children)
                        for (int c = 0; c < 4; ++c) stack.push_back(nf::p32_ptr<const drv::MapNode>(n->children) + c);
                }
                for (const auto& d : draws) kept.insert(d.instance);
                std::vector<drv::Track::Draw> all;
                track.visible(vp, eye, all);
                for (const auto& d : all) {
                    if (kept.count(d.instance) || d.distance > 100.0f) continue;
                    const drv::TrackInstance& inst = track.instances()[size_t(d.instance)];
                    char pb[256];
                    std::snprintf(pb, sizeof pb,
                                  "\ncorte_tirou %d %s bandeiras %02x passada %d distancia %.1f pos %.1f,%.1f,%.1f%s",
                                  d.instance, track.articles()[size_t(inst.article)].name.c_str(), inst.flags, d.pass,
                                  double(d.distance), double(inst.matrix[12]), double(inst.matrix[13]),
                                  double(inst.matrix[14]), in_tree.count(d.instance) ? "" : " fora_da_arvore");
                    cull_line += pb;
                }
                char pb[96];
                std::snprintf(pb, sizeof pb, "\nna_arvore %zu de %zu instancias | fora:", in_tree.size(),
                              track.instances().size());
                cull_line += pb;
                std::map<int, int> hist, hist_in;
                for (size_t k = 0; k < track.instances().size(); ++k) {
                    if (!in_tree.count(int(k))) ++hist[track.instances()[k].flags];
                    else ++hist_in[track.instances()[k].flags];
                }
                for (const auto& [f, n] : hist) {
                    std::snprintf(pb, sizeof pb, " %02x:%d", f, n);
                    cull_line += pb;
                }
                cull_line += " | dentro:";
                for (const auto& [f, n] : hist_in) {
                    std::snprintf(pb, sizeof pb, " %02x:%d", f, n);
                    cull_line += pb;
                }
            }
            // Depuração: o que o corte guardou (a instância, as bandeiras, a passada, a distância do corte, a posição).
            for (const auto& d : draws) {
                const drv::TrackInstance& inst = track.instances()[size_t(d.instance)];
                char pb[256];
                std::snprintf(pb, sizeof pb, "\ncorte_guardou %d %s bandeiras %02x passada %d distancia %.1f pos %.1f,%.1f,%.1f raio %.1f",
                              d.instance, track.articles()[size_t(inst.article)].name.c_str(), inst.flags, d.pass,
                              double(d.distance), double(inst.matrix[12]), double(inst.matrix[13]), double(inst.matrix[14]),
                              double(inst.radius));
                cull_line += pb;
            }
        }
    } else {
        track.visible(vp, eye, draws);
        // A ordem do RRenderWorldCamera::DoRender: o céu (passada 4, levado ao olho, sem névoa nem luzes pontuais), as
        // passadas 3 e 2, os carros, a 1 e a 0 (do longe para o perto).
        for (int pass : {4, 3, 2}) {
            if (pass == 4 && a.no_sky) continue;
            for (const auto& d : draws)
                if (d.pass == pass) push_track_draw(track, d, eye, scene);
        }
        push_car();
        for (int pass : {1, 0}) {
            std::vector<drv::Track::Draw> sorted;
            for (const auto& d : draws)
                if (d.pass == pass) sorted.push_back(d);
            std::stable_sort(sorted.begin(), sorted.end(),
                             [](const drv::Track::Draw& x, const drv::Track::Draw& y) { return x.distance > y.distance; });
            for (const auto& d : sorted) push_track_draw(track, d, eye, scene);
        }
        cull_line = "corte volume (sem a arvore de cenas)";
    }

    drv::DrvRenderOptions ro;
    ro.width = w;
    ro.height = h;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    if (!r.init(nullptr, ro, err) || !r.upload(track.models(), err) || (with_car && !r.upload(car, err))) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    if (a.bench_frames > 0) {
        r.draw_scene(scene);
        r.finish();
        const uint64_t b0 = nf::os::perf_counter();
        for (int i = 0; i < a.bench_frames; ++i) r.draw_scene(scene);
        r.finish();
        const double ms = 1000.0 * double(int64_t(nf::os::perf_counter() - b0)) / double(int64_t(freq)) / a.bench_frames;
        std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f | %s\n", a.bench_frames, ms, 1000.0 / ms,
                    r.stats().adapter.c_str());
    }
    r.draw_scene(scene);
    const drv::TrackStats& ts = track.stats();
    const drv::CarModelStats& ms = track.models().stats();
    char b[1024];
    std::string rep;
    std::snprintf(b, sizeof b,
                  "pista %s arquivo %s carga_ms %.0f | modelos %d lotes %d triangulos %d substitutos %d | instancias %d "
                  "artigos %d lods %d lods_sem_modelo %d sem_artigo %d passadas %d/%d/%d/%d/%d\n",
                  track.name().c_str(), track.viv().c_str(), load_ms, ms.models, ms.batches, ms.triangles,
                  ms.fallback_batches, ts.instances, ts.articles, ts.lods, ts.lods_without_model,
                  ts.instances_without_article, ts.pass_count[0], ts.pass_count[1], ts.pass_count[2], ts.pass_count[3],
                  ts.pass_count[4]);
    rep += b;
    std::snprintf(b, sizeof b,
                  "camera %.2f %.2f %.2f giro %.3f inclinacao %.3f fov %.1f | visiveis %zu itens %zu | desenho chamadas %d "
                  "mundo %d substitutos %d | nevoa %.2f %.2f %.6f cor %.0f %.0f %.0f | caixa %.0f,%.0f,%.0f..%.0f,%.0f,%.0f "
                  "| %dx%d | adaptador %s\n",
                  cam[0], cam[1], cam[2], cam[3], cam[4], a.fov, draws.size(), scene.items.size(), r.stats().draw_calls,
                  r.stats().bond_calls, r.stats().fallback_calls, scene.k.fog[0], scene.k.fog[2], scene.k.fog[3],
                  scene.fog_color[0], scene.fog_color[1], scene.fog_color[2], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], w, h,
                  r.stats().adapter.c_str());
    rep += b;
    rep += cull_line + "\n";
    rep += "metodos";
    for (const auto& m : ms.method_batches) {
        const bool family =
            drv::car_method_layout(m.first.c_str()) != nullptr || drv::bond_method_layout(m.first.c_str()) != nullptr;
        std::snprintf(b, sizeof b, " %s=%d/%d%s", m.first.c_str(), m.second, ms.method_triangles.at(m.first),
                      family ? "" : "(substituto)");
        rep += b;
    }
    rep += "\n";
    if (a.verify) rep += "mundo " + verify_line(track.models().bond_verify_stats()) + "\n";
    // Depuração: as instâncias cuja caixa contém a câmera.
    if (a.probe) {
        for (size_t i = 0; i < track.instances().size(); ++i) {
            const drv::TrackInstance& inst = track.instances()[i];
            if (inst.article < 0) continue;
            const drv::TrackArticle& art = track.articles()[size_t(inst.article)];
            const int part = art.lods.empty() ? -1 : art.lods[0].part;
            if (part < 0) continue;
            const drv::CarPart& p = track.models().parts()[size_t(part)];
            // A câmera no espaço da instância (a matriz é rotação + translação).
            float d[3] = {eye[0] - inst.matrix[12], eye[1] - inst.matrix[13], eye[2] - inst.matrix[14]}, l[3];
            for (int c = 0; c < 3; ++c)
                l[c] = (d[0] * inst.matrix[c * 4] + d[1] * inst.matrix[c * 4 + 1] + d[2] * inst.matrix[c * 4 + 2]);
            bool in = true;
            for (int c = 0; c < 3; ++c) in = in && l[c] >= p.bbox_min[c] - 2 && l[c] <= p.bbox_max[c] + 2;
            if (!in) continue;
            std::snprintf(b, sizeof b,
                          "dentro instancia %zu %s bandeiras %02x passada %d pos %.1f,%.1f,%.1f raio %.1f peca %d caixa "
                          "%.0f,%.0f,%.0f..%.0f,%.0f,%.0f dims %08x\n",
                          i, art.name.c_str(), inst.flags, inst.pass, inst.matrix[12], inst.matrix[13], inst.matrix[14],
                          inst.radius, part, p.bbox_min[0], p.bbox_min[1], p.bbox_min[2], p.bbox_max[0], p.bbox_max[1],
                          p.bbox_max[2], inst.packed_dims);
            rep += b;
        }
    }
    // O céu (as peças das instâncias da passada 4): o estado do GS de cada desenho.
    for (const drv::TrackInstance& inst : track.instances()) {
        if (inst.pass != 4 || inst.article < 0) continue;
        for (const auto& lod : track.articles()[size_t(inst.article)].lods) {
            for (const drv::CarDraw& d : track.models().draws()) {
                if (d.part != lod.part) continue;
                const drv::eagl::GsRegs& g = d.pass[0];
                const drv::CarPart& pp = track.models().parts()[size_t(d.part)];
                std::snprintf(b, sizeof b,
                              "ceu peca %d %s tris %u test %05llx zbuf %llx alpha %02llx prim %03llx caixa "
                              "%.0f,%.0f,%.0f..%.0f,%.0f,%.0f instancia %.1f,%.1f,%.1f\n",
                              d.part, d.method.c_str(), d.index_count / 3, (unsigned long long)(g.test & 0x7ffff),
                              (unsigned long long)(g.zbuf >> 32), (unsigned long long)(g.alpha & 0xff),
                              (unsigned long long)g.prim, pp.bbox_min[0], pp.bbox_min[1], pp.bbox_min[2], pp.bbox_max[0],
                              pp.bbox_max[1], pp.bbox_max[2], inst.matrix[12], inst.matrix[13], inst.matrix[14]);
                rep += b;
            }
        }
    }
    std::printf("%s", rep.c_str());
    if (!a.screenshot.empty()) {
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- o carro dirigível (--drive)

// As teclas de um passo do roteiro (--drive-input): W acelera, S freia, A/D viram, SPACE é o freio de mão; R1, L1, R2,
// L2 e TRI os botões; LU/LD, RL/RR, RU/RD os direcionais.
struct DriveKeys {
    bool gas = false, brake = false, left = false, right = false, hand = false;
    // Os botões das armas (os do drivecfg.def): R1, L1, R2, L2 e o triângulo.
    bool r1 = false, l1 = false, r2 = false, l2 = false, tri = false;
    // Os direcionais nos extremos (só do roteiro): LU/LD o esquerdo para cima e para baixo, RL/RR/RU/RD o direito.
    bool lu = false, ld = false, rl = false, rr = false, ru = false, rd = false;
};

DriveKeys parse_keys(const std::string& s) {
    DriveKeys k;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('+', p);
        if (e == std::string::npos) e = s.size();
        const std::string t = s.substr(p, e - p);
        if (t == "W" || t == "w" || t == "up") k.gas = true;
        else if (t == "S" || t == "s" || t == "down") k.brake = true;
        else if (t == "A" || t == "a" || t == "left") k.left = true;
        else if (t == "D" || t == "d" || t == "right") k.right = true;
        else if (t == "SPACE" || t == "space") k.hand = true;
        else if (t == "R1" || t == "r1") k.r1 = true;
        else if (t == "L1" || t == "l1") k.l1 = true;
        else if (t == "R2" || t == "r2") k.r2 = true;
        else if (t == "L2" || t == "l2") k.l2 = true;
        else if (t == "TRI" || t == "tri") k.tri = true;
        else if (t == "LU") k.lu = true;
        else if (t == "LD") k.ld = true;
        else if (t == "RL") k.rl = true;
        else if (t == "RR") k.rr = true;
        else if (t == "RU") k.ru = true;
        else if (t == "RD") k.rd = true;
        p = e + 1;
    }
    return k;
}

// O controle do PS2 que o teclado e o controle do PC fazem (só do port): o teclado aperta o X (W/seta acima: o acelerador
// do drivecfg.def), o quadrado (S/seta abaixo: o freio), a bola (espaço: o freio de mão) com a pressão inteira e põe o
// direcional esquerdo nos extremos (A/D, setas); o controle do PC entra pela posição dos botões, como o controle virtual
// do jogo a pé (A = X, B = bola, X = quadrado, Y = triângulo, LB/RB = L1/R1, LT/RT = L2/R2, os direcionais e o
// D-pad), com a pressão 255 nos botões e os gatilhos acima de 30.
drv::PadData pad_from(const DriveKeys& k, const nf::PadState* pc) {
    drv::PadData p;
    // Os bits: L2 0, R2 1, L1 2, R1 3, triângulo 4, bola 5, X 6, quadrado 7, Select 8, L3 9, R3 10, Start 11, cima 12,
    // direita 13, baixo 14, esquerda 15; as pressões: direita, esquerda, cima, baixo, triângulo, bola, X, quadrado, L1,
    // R1, L2, R2.
    auto press = [&](int bit, int pressure) {
        p.buttons |= uint16_t(1u << bit);
        if (pressure >= 0) p.pressure[pressure] = 255;
    };
    if (k.gas) press(6, 6);
    if (k.brake) press(7, 7);
    if (k.hand) press(5, 5);
    if (k.r1) press(3, 9);
    if (k.l1) press(2, 8);
    if (k.r2) press(1, 11);
    if (k.l2) press(0, 10);
    if (k.tri) press(4, 4);
    if (pc && pc->connected) {
        const uint16_t b = pc->buttons;
        if (b & nf::kPadA) press(6, 6);
        if (b & nf::kPadB) press(5, 5);
        if (b & nf::kPadX) press(7, 7);
        if (b & nf::kPadY) press(4, 4);
        if (b & nf::kPadLB) press(2, 8);
        if (b & nf::kPadRB) press(3, 9);
        if (pc->lt > nf::kPadTriggerThreshold) press(0, 10);
        if (pc->rt > nf::kPadTriggerThreshold) press(1, 11);
        if (b & nf::kPadBack) press(8, -1);
        if (b & nf::kPadLThumb) press(9, -1);
        if (b & nf::kPadRThumb) press(10, -1);
        if (b & nf::kPadStart) press(11, -1);
        if (b & nf::kPadDpadUp) press(12, 2);
        if (b & nf::kPadDpadRight) press(13, 0);
        if (b & nf::kPadDpadDown) press(14, 3);
        if (b & nf::kPadDpadLeft) press(15, 1);
        // Os direcionais: -32768..32767 para 0..255, o y do PS2 para baixo.
        auto byte = [](int v) { return uint8_t((v + 32768) >> 8); };
        p.lx = byte(pc->lx);
        p.ly = uint8_t(255 - byte(pc->ly));
        p.rx = byte(pc->rx);
        p.ry = uint8_t(255 - byte(pc->ry));
    }
    if (k.left != k.right) p.lx = k.left ? 0 : 255;
    if (k.lu != k.ld) p.ly = k.lu ? 0 : 255;
    if (k.rl != k.rr) p.rx = k.rl ? 0 : 255;
    if (k.ru != k.rd) p.ry = k.ru ? 0 : 255;
    return p;
}

// O IOModule do jogo (game/input.h): o PS2PadDevice, a tabela do data\Control\drivecfg.def e o SendGameMessages, que
// manda as ações para a fila do carro (o ActionQueueManager, que só tem a do carro aqui).
struct DriveInput {
    drv::InputMap map;
    drv::PadDevice dev;
    drv::IOModuleState io;
    uint64_t actions = 0;
    // A câmera do jogador também recebe as ações (RPlayerCamera::CameraInputCallback: na configuração do POV, a da mira
    // do AutoDrive, o AutoDriveCamInputHandler; seção 23).
    std::function<void(int32_t action, float value)> camera;
    void feed(drv::BondCar& car, const drv::PadData& pad, int32_t now) {
        io.now = now;
        dev.Poll(pad);
        drv::SendGameMessages(io, dev, map, [&](int32_t action, int32_t device, float value) {
            ++actions;
            car.push_action(action, value);
            drv::weapons_feed(action, device, value);  // a fila "WeaponManQ" do SWeaponManager (FeedQueue: todas as filas)
            static const bool log = nf::get_env_narrow("NF_DRV_WEAPON_LOG", nullptr);  // depuração: as ações
            if (log)
                std::printf("acao passo %d %d (%s) %.2f\n", int(drv::simulation().steps), action,
                            drv::action_name(action), double(value));
            if (camera) camera(action, value);
        });
        io.send_all = 0;  // o UpdateAllDevices zera depois de cada passagem
    }
};

float ee_add_one(float h) { return ps2::ee::add(h, 1.0f); }

// Depuração (--autopilot, docs/driving.md seção 21; só do port): o carro do jogador dirigido como um jogador, pelo
// controle do PS2 (o direcional esquerdo e a pressão do X e do quadrado, que a entrada do jogo leva às ações do
// drivecfg.def): primeiro pelos pontos dados, depois atrás do veículo de um elemento da IA, mantendo-se a uns 20 m. O
// caminho até os pontos dados é em linha reta (um ponto passado fica para trás); até o veículo, longe do rastro dele,
// vai pela rede de estradas (o menor caminho entre os nós do 'RNgp', as curvas pelos pontos de controle), refeito a
// cada segundo. Preso (parado com o acelerador), dá ré por um segundo.
struct Autopilot {
    int element = -1;
    std::vector<std::pair<float, float>> points;
    std::vector<float> speeds;
    size_t next_point = 0;
    std::vector<std::array<float, 2>> route;
    size_t route_next = 0;
    std::deque<std::array<float, 2>> trail;  // por onde o veículo do elemento passou (a cada 2 m)
    int stuck = 0, reverse = 0, replan = 0;
    uint64_t steps = 0, following = 0, plans = 0;
    float last_target[2] = {0, 0};
    // --autopilot-fire: seguindo o elemento, o L1 (a arma especial: o Q-Pulse do Vanquish) quando o veículo dele está a
    // menos de 50 m e a menos de 0,15 rad da frente do carro, um tiro a cada 1,5 s.
    bool fire = false;
    int fire_hold = 0;
    uint64_t next_fire = 0, shots = 0;
    int plan_from = -1, plan_to = -1, plan_seen = 0, plan_nodes = 0;

    // O menor caminho pela rede (Dijkstra sobre os trechos: vizinhos pelos nós das pontas e pelos cruzamentos, o custo o
    // comprimento) do trecho mais perto de (x0, z0) ao mais perto de (x1, z1); os pontos são os de ligação (o nó comum
    // ou o centro do cruzamento) e o meio das curvas, e por fim o ponto final.
    void plan(float x0, float z0, float x1, float z1) {
        ++plans;
        route.clear();
        route_next = 0;
        const drv::RoadNetworkGlobals& rn = drv::road_network();
        const int n = rn.num_segments;
        auto mid = [](const drv::WRoadSegment* s, float out[2]) {
            const drv::WRoadNode* a = drv::road_node(s->node[0]);
            const drv::WRoadNode* b = drv::road_node(s->node[1]);
            out[0] = 0.5f * (a->position[0] + b->position[0]);
            out[1] = 0.5f * (a->position[2] + b->position[2]);
            if (s->flags & drv::kSegCurved) {
                out[0] = 0.125f * (a->position[0] + 3 * s->start_control[0] + 3 * s->end_control[0] + b->position[0]);
                out[1] = 0.125f * (a->position[2] + 3 * s->start_control[2] + 3 * s->end_control[2] + b->position[2]);
            }
        };
        auto nearest = [&](float x, float z) {
            int best = -1;
            float bd = 1e30f;
            for (int i = 0; i < n; ++i) {
                const drv::WRoadSegment* s = drv::road_segment(i);
                if (!s) continue;
                float m[2];
                mid(s, m);
                const float d = (m[0] - x) * (m[0] - x) + (m[1] - z) * (m[1] - z);
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
            return best;
        };
        // Os vizinhos de um trecho e o ponto de ligação.
        auto neighbours = [&](int si, std::vector<std::pair<int, std::array<float, 2>>>& out) {
            out.clear();
            const drv::WRoadSegment* s = drv::road_segment(si);
            for (int k = 0; k < 2; ++k) {
                const drv::WRoadNode* nd = drv::road_node(s->node[k]);
                if (nd)
                    for (int j = 0; j < nd->segment_count && j < 8; ++j)
                        if (nd->segments[j] != si) out.push_back({nd->segments[j], {nd->position[0], nd->position[2]}});
                if (s->intersection[k] >= 0 && s->intersection[k] < rn.num_intersections) {
                    const drv::WRoadIntersection* in = drv::road_intersection(s->intersection[k]);
                    if (in)
                        for (int j = 0; j < in->segment_count && j < 11; ++j)
                            if (in->segments[j] >= 0 && in->segments[j] != si)
                                out.push_back({in->segments[j], {in->position[0], in->position[2]}});
                }
            }
        };
        // O destino: o trecho mais perto do ponto final; a busca parte dele (a rede não tem sentido único aqui) e a
        // largada é o trecho alcançado mais perto do carro (a rede tem pedaços soltos, como o da largada de Paris).
        const int b = nearest(x1, z1);
        plan_to = b;
        plan_from = -1;
        plan_seen = 0;
        plan_nodes = 0;
        if (b >= 0) {
            std::vector<float> dist(size_t(n), 1e30f);
            std::vector<int> prev(size_t(n), -1);
            std::vector<std::array<float, 2>> link(static_cast<size_t>(n));
            std::vector<bool> done(size_t(n), false);
            std::vector<std::pair<int, std::array<float, 2>>> nb;
            dist[size_t(b)] = 0.0f;
            for (;;) {
                int u = -1;
                for (int k = 0; k < n; ++k)
                    if (!done[size_t(k)] && dist[size_t(k)] < 1e29f && (u < 0 || dist[size_t(k)] < dist[size_t(u)])) u = k;
                if (u < 0) break;
                done[size_t(u)] = true;
                ++plan_seen;
                neighbours(u, nb);
                const drv::WRoadSegment* su = drv::road_segment(u);
                const float len = su && su->length > 0.0f ? su->length : 1.0f;
                for (const auto& e : nb) {
                    const int w = e.first;
                    if (w < 0 || w >= n) continue;
                    if (dist[size_t(u)] + len < dist[size_t(w)]) {
                        dist[size_t(w)] = dist[size_t(u)] + len;
                        prev[size_t(w)] = u;
                        link[size_t(w)] = e.second;
                    }
                }
            }
            float bd = 1e30f;
            for (int k = 0; k < n; ++k) {
                if (!done[size_t(k)]) continue;
                float m[2];
                mid(drv::road_segment(k), m);
                const float d = (m[0] - x0) * (m[0] - x0) + (m[1] - z0) * (m[1] - z0);
                if (d < bd) {
                    bd = d;
                    plan_from = k;
                }
            }
            for (int v = plan_from; v >= 0; v = prev[size_t(v)]) {
                float m[2];
                mid(drv::road_segment(v), m);
                route.push_back({m[0], m[1]});
                if (prev[size_t(v)] >= 0) route.push_back(link[size_t(v)]);
                ++plan_nodes;
            }
        }
        route.push_back({x1, z1});
        // Começa no ponto do caminho mais perto do carro, ou no seguinte se o carro já passou dele (sem voltar atrás).
        size_t k = 0;
        float kd = 1e30f;
        for (size_t i = 0; i < route.size(); ++i) {
            const float d = (route[i][0] - x0) * (route[i][0] - x0) + (route[i][1] - z0) * (route[i][1] - z0);
            if (d < kd) {
                kd = d;
                k = i;
            }
        }
        if (k + 1 < route.size()) {
            const float vx = route[k + 1][0] - route[k][0], vz = route[k + 1][1] - route[k][1];
            if (vx * (x0 - route[k][0]) + vz * (z0 - route[k][1]) > 0.0f) ++k;
        }
        route_next = k;
    }

    void drive(drv::PadData& pad, const drv::BondCar& car) {
        const drv::RigidBody* b = car.body();
        if (!b) return;
        ++steps;
        const float px = b->pos[0], pz = b->pos[2];
        static const bool log = nf::get_env_narrow("NF_DRV_PILOT_LOG", nullptr);  // depuração: o caminho por segundo
        if (log && steps % 60 == 0)
            std::printf("piloto_passo %llu %.1f %.1f alvo %.1f %.1f velocidade %.1f\n", (unsigned long long)steps,
                        double(px), double(pz), double(last_target[0]), double(last_target[1]), double(car.d.car_speed));
        auto dist = [&](float x, float z) { return std::sqrt((x - px) * (x - px) + (z - pz) * (z - pz)); };
        const drv::AIElement* e = drv::ai_element(element);
        const drv::AIElementTracking* t = drv::ai_tracking(element);
        const bool awake = e && t && e->state == 1;
        // Um ponto dado alcançado (a menos de 15 m) ou já passado (o carro além dele no rumo do seguinte).
        auto point_passed = [&](size_t i) {
            if (i + 1 >= points.size() || dist(points[i].first, points[i].second) > 60.0f) return false;
            const float vx = points[i + 1].first - points[i].first, vz = points[i + 1].second - points[i].second;
            return vx * (px - points[i].first) + vz * (pz - points[i].second) > 0.0f;
        };
        while (next_point < points.size() &&
               (dist(points[next_point].first, points[next_point].second) < 15.0f || point_passed(next_point))) {
            ++next_point;
            replan = 0;
        }
        if (awake && (trail.empty() || std::hypot(trail.back()[0] - t->pos[0], trail.back()[1] - t->pos[2]) >= 2.0f))
            trail.push_back({t->pos[0], t->pos[2]});
        float gx = 0, gz = 0, want = 0;
        bool on_trail = false;
        if (next_point < points.size()) {
            gx = points[next_point].first;
            gz = points[next_point].second;
            want = next_point < speeds.size() ? speeds[next_point] : 28.0f;
        } else if (awake) {
            ++following;
            gx = t->pos[0];
            gz = t->pos[2];
            const float d = dist(gx, gz);
            want = std::clamp(t->speed + (d - 20.0f) * 0.5f, 0.0f, 45.0f);
            // O rastro do veículo: o primeiro ponto a mais de 12 m (os de antes do carro são jogados fora); longe do
            // rastro (mais de 30 m do ponto mais perto), o caminho pela rede.
            size_t nearest = 0;
            float nd = 1e30f;
            for (size_t i = 0; i < trail.size(); ++i) {
                const float di = dist(trail[i][0], trail[i][1]);
                if (di < nd) {
                    nd = di;
                    nearest = i;
                }
            }
            if (nd < 30.0f) {
                while (nearest > 0) {
                    trail.pop_front();
                    --nearest;
                }
                while (trail.size() > 1 && dist(trail.front()[0], trail.front()[1]) < 12.0f) trail.pop_front();
                on_trail = !trail.empty();
            }
        } else {
            return;
        }
        float tx = gx, tz = gz;
        if (next_point < points.size()) {
            // (os pontos dados: em linha reta)
        } else if (on_trail) {
            tx = trail.front()[0];
            tz = trail.front()[1];
        } else {
            if (--replan <= 0) {
                plan(px, pz, gx, gz);
                replan = 60;
            }
            // O ponto alcançado (a menos de 12 m) ou já passado (o carro além dele no rumo do seguinte) fica para trás.
            auto passed = [&](size_t i) {
                const float vx = route[i + 1][0] - route[i][0], vz = route[i + 1][1] - route[i][1];
                return vx * (px - route[i][0]) + vz * (pz - route[i][1]) > 0.0f;
            };
            while (route_next + 1 < route.size() &&
                   (dist(route[route_next][0], route[route_next][1]) < 12.0f || passed(route_next)))
                ++route_next;
            if (dist(gx, gz) > 25.0f && route_next < route.size()) {
                tx = route[route_next][0];
                tz = route[route_next][1];
            }
        }
        last_target[0] = tx;
        last_target[1] = tz;
        const float dx = tx - px, dz = tz - pz;
        const float(*o)[4] = b->info->orient;
        const float ang = std::atan2(o[0][0] * dx + o[0][2] * dz, o[2][0] * dx + o[2][2] * dz);
        float steer = std::clamp(ang * 2.0f, -1.0f, 1.0f);
        const float speed = car.d.car_speed;
        if (reverse > 0) {
            --reverse;
            steer = -steer;
            pad.buttons |= uint16_t(1u << 7);
            pad.pressure[7] = 255;
        } else {
            if (std::fabs(ang) > 0.6f) want = std::min(want, 12.0f);
            if (speed < want - 1.0f) {
                pad.buttons |= uint16_t(1u << 6);
                pad.pressure[6] = uint8_t(std::clamp((want - speed) * 40.0f, 80.0f, 255.0f));
            } else if (speed > want + 4.0f) {
                pad.buttons |= uint16_t(1u << 7);
                pad.pressure[7] = uint8_t(std::clamp((speed - want) * 30.0f, 40.0f, 255.0f));
            }
            stuck = (std::fabs(speed) < 1.0f && want > 3.0f) ? stuck + 1 : 0;
            if (stuck > 90) {
                stuck = 0;
                reverse = 70;
                replan = 0;
            }
        }
        pad.lx = uint8_t(std::clamp(128.0f + steer * 127.0f, 0.0f, 255.0f));
        if (fire && awake && next_point >= points.size()) {
            const float ex = t->pos[0] - px, ez = t->pos[2] - pz;
            const float ea = std::atan2(o[0][0] * ex + o[0][2] * ez, o[2][0] * ex + o[2][2] * ez);
            if (fire_hold == 0 && steps >= next_fire && std::fabs(ea) < 0.15f && dist(t->pos[0], t->pos[2]) < 50.0f) {
                fire_hold = 4;
                next_fire = steps + 90;
                ++shots;
            }
        }
        if (fire_hold > 0) {
            --fire_hold;
            pad.buttons |= uint16_t(1u << 2);
            pad.pressure[8] = 255;
        }
    }
};

// Depuração (--gunner e1[,e2...], docs/driving.md seção 24; só do port): o atirador da seção do helicóptero como um
// jogador, pelo controle do PS2 na configuração do POV: o direcional esquerdo gira a câmera do AutoDrive até a mira
// (a frente da RPlayerCamera::GetForwardAimVec4) apontar para o carro do primeiro elemento da lista acordado, vivo e
// sem a propriedade BLOWOUT nem a bandeira "pode estar morto" (o que as regras perguntam), e o R1 atira quando o erro
// fica abaixo de 0,6 grau (--gunner-limit). O jogo não sabe de nada disto: só vê os botões. O direcional é um empurrão
// mínimo de 70 (a zona morta) mais 3000 x o erro; NF_DRV_GUN_PUSH, NF_DRV_GUN_SX/SY (os sinais) e NF_DRV_GUN_LOG (o
// erro a cada meio segundo) para experimentar.
struct Gunner {
    std::vector<int> elements;
    float height = 0.0f;  // somado à altura do carro do alvo (+ acima)
    bool tyres = false;   // a mira no pneu (a alavanca 0-3 do corpo) mais perto da câmera em vez do centro
    int current = -1;
    uint64_t steps = 0, aimed = 0, fired = 0;
    float error_deg = 0.0f;
    int hold = 0;  // os passos que o R1 fica apertado (o tiro é na borda)
    uint64_t next_shot = 0;  // o próximo tiro (20 passos depois do anterior)
    float limit_deg = 0.6f;  // o erro abaixo do qual atira (--gunner-limit; NF_DRV_GUN_LIMIT o troca)

    void drive(drv::PadData& pad) {
        drv::PlayerCamera* cam = drv::current_player_camera();
        if (!cam || !drv::ai_controller()) return;
        ++steps;
        current = -1;
        const drv::AIElementTracking* tr = nullptr;
        for (int e : elements) {
            const drv::AIElement* el = drv::ai_element(e);
            drv::AIElementTracking* t = drv::ai_tracking(e);
            if (!el || !t || el->state != 1 || !(el->health > 0.0f)) continue;
            if ((el->flags & drv::kElemMightBeDead) || drv::AIElementTracking_GetProperty(*t, "BLOWOUT")) continue;
            current = e;
            tr = t;
            break;
        }
        if (!tr) return;
        const float* m = cam->d.matrix;
        const float* f = drv::RPlayerCamera_GetForwardAimVec4(*cam, 1.0f);
        float aim[3] = {tr->pos[0], tr->pos[1] + height, tr->pos[2]};
        if (tyres) {
            // O carro do elemento (AIElement +0xfc, o PBondCarData) e o pneu mais perto da câmera: a alavanca no
            // referencial do corpo girada pela orientação, mais a posição.
            const drv::AIElement* el = drv::ai_element(current);
            for (drv::BondCar* c : drv::simulation().cars) {
                if (!el || nf::p32_u(&c->d) != el->physics) continue;
                const drv::RigidBody* b = c->body();
                const drv::RigidBodyInfo* in = b ? nf::raw(b->info) : nullptr;
                if (!in) break;
                float best = 1e30f;
                for (int w = 0; w < 4; ++w) {
                    if (c->d.tyre_damage_points[w] == 0) continue;
                    const float* l = in->lever_local[w];
                    float p[3];
                    for (int i = 0; i < 3; ++i)
                        p[i] = b->pos[i] + l[0] * in->orient[0][i] + l[1] * in->orient[1][i] + l[2] * in->orient[2][i];
                    const float dd = (p[0] - m[12]) * (p[0] - m[12]) + (p[1] - m[13]) * (p[1] - m[13]) +
                                     (p[2] - m[14]) * (p[2] - m[14]);
                    if (dd < best) {
                        best = dd;
                        for (int i = 0; i < 3; ++i) aim[i] = p[i];
                        aim[1] += height;
                    }
                }
                break;
            }
        }
        float d[3] = {aim[0] - m[12], aim[1] - m[13], aim[2] - m[14]};
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(len > 0.1f)) return;
        for (float& v : d) v /= len;
        // O erro no referencial da mira: a direita e o alto feitos da frente da mira e do alto do mundo, com a mão
        // (destra ou canhota) do referencial da vista (numa câmera animada a vista olha para outro lado).
        const float fx = f[0], fy = f[1], fz = f[2];
        const float hand = (m[1] * m[6] - m[2] * m[5]) * m[8] + (m[2] * m[4] - m[0] * m[6]) * m[9] +
                           (m[0] * m[5] - m[1] * m[4]) * m[10];
        float r[3] = {hand > 0 ? fz : -fz, 0.0f, hand > 0 ? -fx : fx};  // (0,1,0) x f, ou f x (0,1,0)
        const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
        if (!(rl > 1e-4f)) return;
        r[0] /= rl;
        r[2] /= rl;
        // o alto: f x r (destro) ou r x f (canhoto)
        float u[3] = {fy * r[2] - fz * r[1], fz * r[0] - fx * r[2], fx * r[1] - fy * r[0]};
        if (!(hand > 0)) {
            for (float& v : u) v = -v;
        }
        const float ex = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        const float ey = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        const float cosang = std::clamp(d[0] * fx + d[1] * fy + d[2] * fz, -1.0f, 1.0f);
        error_deg = std::acos(cosang) * 57.29578f;
        // O direcional: um empurrão mínimo (a zona morta do controle) mais o proporcional ao erro.
        static const float push = [] {
            std::string v;
            return nf::get_env_narrow("NF_DRV_GUN_PUSH", &v) ? std::stof(v) : 70.0f;
        }();
        auto stick = [](float e) {
            if (std::fabs(e) < 0.0015f) return uint8_t(128);
            const float v = (e > 0 ? push : -push) + e * 3000.0f;
            return uint8_t(std::clamp(128.0f + v, 0.0f, 255.0f));
        };
        static const float sgn_x = [] {
            std::string v;
            return nf::get_env_narrow("NF_DRV_GUN_SX", &v) ? std::stof(v) : 1.0f;
        }();
        static const float sgn_y = [] {
            std::string v;
            return nf::get_env_narrow("NF_DRV_GUN_SY", &v) ? std::stof(v) : -1.0f;
        }();
        static const bool log = nf::get_env_narrow("NF_DRV_GUN_LOG", nullptr);
        if (log && steps % 30 == 0)
            std::printf("atirador_passo %llu alvo %d erro %.2f ex %.3f ey %.3f vista %.3f modo %d rumo %.1f "
                        "inclinacao %.1f preso %d\n",
                        (unsigned long long)steps, current, double(error_deg), double(ex), double(ey),
                        double(r[0] * m[0] + r[1] * m[1] + r[2] * m[2]), int(cam->d.mode),
                        double(cam->d.ad_yaw) * 360.0, double(cam->d.ad_pitch) * 360.0, int(cam->d.limited));
        pad.lx = stick(sgn_x * ex);
        pad.ly = stick(sgn_y * ey);
        if (hold > 0) {
            --hold;
            pad.buttons |= uint16_t(1u << 3);
            pad.pressure[9] = 255;
        } else if (error_deg < limit_deg && cosang > 0.0f) {
            ++aimed;
            if (steps >= next_shot) {
                hold = 3;
                ++fired;
                next_shot = steps + 20;
            }
        }
    }
};

// O carro do jogador com o render do jogo (driving/render/vehicle.h, shadow.h): o RVehicle (as rodas, os ponteiros, as
// lâmpadas, o dano) e a sombra simples, desenhados como o RVehicle::Render: a sombra, o ComputeDamage, o
// SetPerObjectBuffers e as instâncias pela animação procedural (GetInstanceMatrix) vezes a matriz do objeto.
struct CarRig {
    drv::Vehicle vehicle;
    drv::VehicleModelInfo info;
    drv::ShadowSettings shadow;
    float shadow_width = 0.8f, shadow_length = 0.8f;
    int cshd = -1;  // a textura 'cshd' do common.ssh (a da sombra)
    float instance[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // a instância de origem (UpdatePosition)
    drv::ShadowQuad last_shadow;
    bool moved = false;
    bool player = true;  // o carro do jogador (os ponteiros do painel; o dano também vai para as constantes da cena)
    // As zonas de dano e as luzes deste carro (as RVehicle::SetPerObjectBuffers dele, guardadas: os buffers do jogo são
    // compartilhados e cada carro os reescreve antes de se desenhar).
    float damage[16][4] = {};
    float switch_data[16][4] = {};

    // Antes de PBondCar_Construct (o construtor do PS2 cria o RVehicle antes do GlareOn(1) do carro do jogador).
    void attach(drv::BondCar& car) {
        drv::RVehicle_Construct(vehicle);
        car.vehicle = &vehicle;
    }
    // Depois da carga do modelo: o PostLoad, os buffers das lâmpadas, a sombra.
    void post_load(const drv::CarModel& model, const drv::BondCar& car, const std::string& switch_tun,
                   const std::string& fx_tun, const std::string& car_name, bool shared = true) {
        model.vehicle_info(info);
        drv::RVehicle_PostLoad(vehicle, info, true, const_cast<drv::BondCar&>(car).physics()->is_sub != 0);
        if (shared) drv::RVehicle_InitSharedBuffers(switch_tun);
        drv::RShadowMap_LoadAttributes(shadow, fx_tun, car_name, shadow_width, shadow_length);
        for (size_t t = 0; t < model.textures().size(); ++t)
            if (model.textures()[t].name == "cshd") cshd = int(t);
    }
    // Um quadro: RVehicle::UpdatePosition e RVehicle::Render. `interior` é o byte +0x07 do modo da câmera; `hidden` o tipo
    // 1 do modo (a vista de dentro: o Render do carro do jogador não desenha nada); `time` o RTimeData::fTime (o passo).
    void frame(drv::BondCar& car, const drv::CarModel& model, float render_offset, bool interior, bool hidden,
               int32_t time, const std::vector<uint16_t>& list, drv::DrvScene& scene) {
        moved = drv::RVehicle_UpdatePosition(vehicle, &car, player, interior, instance, render_offset);
        if (hidden) return;
        // RVehicle::RenderShadow (com o carro a menos de 0,125 da distância de vista: o multiplicador de LOD da vista é
        // 0, docs/driving.md seção 13, então sempre): com o modelo de sombra, RShadowMap::Draw -> DrawSimpleShadow.
        last_shadow = drv::ShadowQuad();
        if (vehicle.d.shadow_model != 0)
            drv::RShadowMap_DrawSimpleShadow(car, shadow_width, shadow_length, shadow.strength, last_shadow);
        if (last_shadow.drawn) {
            drv::DrvQuad q;
            q.model = &model;
            q.texture = cshd;
            for (int k = 0; k < 4; ++k) {
                for (int c = 0; c < 4; ++c) q.pos[k][c] = last_shadow.corner[k][c];
                q.uv[k][0] = last_shadow.uv[k][0];
                q.uv[k][1] = last_shadow.uv[k][1];
            }
            q.colour = last_shadow.colour[0];
            drv::DrvItem it;
            it.quad = int(scene.quads.size());
            scene.quads.push_back(q);
            scene.items.push_back(it);
        }
        drv::RVehicle_ComputeDamage(vehicle, &car);
        drv::RVehicle_SetPerObjectBuffers(vehicle, time);  // RRenderSharedData::SendPerCarInstance
        std::memcpy(damage, drv::vehicle_shared_buffers().damage_zones, sizeof damage);
        std::memcpy(switch_data, drv::vehicle_shared_buffers().switch_data, sizeof switch_data);
        if (player) std::memcpy(scene.k.damage, damage, sizeof scene.k.damage);
        const drv::ProcAnimContext ctx = drv::RSkeletalObj_SetProcAnimState(vehicle);
        float transform[4][4];
        std::memcpy(transform, instance, 64);
        transform[0][3] = transform[1][3] = transform[2][3] = 0.0f;
        transform[3][3] = 1.0f;
        for (uint16_t ii : list) {
            if (ii >= model.instances().size() || model.instances()[ii].part < 0) continue;
            const drv::CarInstance& ci = model.instances()[ii];
            uint32_t raw[16];
            if (vehicle.d.anim != 0) {
                // A cópia da instância no RAnimEngine::Handle (o RVehicle::Render desenha as cópias: o +0xc bit 1
                // escondida, o DrawInstance pula; o +0xd e o +0xe a animação, o EvaluateInstance do GetInstanceMatrix).
                const uint8_t* copy = nf::p32_ptr<uint8_t>(vehicle.d.anim) + 0x40 + size_t(ii) * 0x40;
                if (copy[0xc] & 1) continue;
                std::memcpy(raw, copy, 64);
            } else {
                std::memcpy(raw, ci.matrix, 64);
                raw[3] = ci.flags;
                raw[7] = ci.piece;
                raw[11] = ci.extra[0];
                raw[15] = ci.extra[1];
                if (((ci.flags >> 8) & 0xff) < 0x64) continue;  // a animação sem o Handle: como antes, não desenhada
            }
            float m[4][4];
            if (!drv::GetInstanceMatrix(raw, ctx, m)) continue;
            drv::VU0_MATRIX4_mult(m, transform, m);  // DrawInstance
            drv::DrvItem it;
            it.model = &model;
            it.part = ci.part;
            std::memcpy(it.world, m, 64);
            it.switch_data = switch_data;
            it.damage = damage;
            scene.items.push_back(it);
        }
    }
};

// O RSceneObj do carro do jogador para o EFireWeapon e as explosões (driving/game/missile.h, explosion.h): as funções
// do RSceneObj que eles chamam, sobre o CarRig (o RVehicle e o RAnimEngine::Handle das instâncias).
struct FxScene {
    CarRig* rig = nullptr;
    drv::BondCar* car = nullptr;
    float render_offset = 0;
    const drv::AttributeCollection* attrs = nullptr;
    uint32_t player_image = 0;  // (p32 PBondCarData)
    // Os carros dos elementos da IA (a imagem e os atributos do tipo): o AI_GOODAIM, o IS_FLYING e o HAS_TURRET que o
    // EFireWeapon pergunta (seção 24).
    std::vector<std::pair<uint32_t, const drv::AttributeCollection*>> ai_attrs;
};
FxScene g_fx;

const drv::AttributeCollection* fx_attrs_of(uint32_t image) {
    if (image == g_fx.player_image) return g_fx.attrs;
    for (const auto& a : g_fx.ai_attrs)
        if (a.first == image) return a.second;
    return nullptr;
}

// [driving] 0x001c9368 RSceneObj::GetInstancePosition(índice, m, true) do RVehicle: a matriz do objeto (GetTransform: a
// instância de origem com o w das linhas 0, 0, 0, 1), o SetProcAnimState (a entrada +0x5c), a GetInstanceMatrix da
// cópia vezes ela; um índice fora das cópias (ou -1) dá só a do objeto.
bool fx_instance_position(uint32_t scene, int32_t idx, float m[4][4]) {
    if (drv::anim::ActWeapon_FromScene(scene)) return drv::anim::ActWeapon_GetInstancePosition(scene, idx, m);  // as armas dos atores
    CarRig* r = g_fx.rig;
    if (!r || scene != nf::p32_u(&r->vehicle.d)) return false;
    // A instância de origem como o RVehicle::UpdatePosition a deixa (o RSceneObj::UpdatePosition do corpo de agora: as
    // capturas não desenham os passos antes da foto, e o jogo a refaz a cada quadro).
    float t[4][4];
    std::memcpy(t, r->instance, 64);
    if (g_fx.car && g_fx.car->body()) drv::RSceneObj_UpdateTransform(g_fx.car->body(), g_fx.render_offset, &t[0][0]);
    t[0][3] = t[1][3] = t[2][3] = 0.0f;
    t[3][3] = 1.0f;
    const uint32_t h = r->vehicle.d.anim;
    if (h == 0 || idx < 0 || uint32_t(idx) >= nf::p32_ptr<const uint8_t>(h)[0x1c]) {
        std::memcpy(m, t, 64);
        return true;
    }
    const drv::ProcAnimContext ctx = drv::RSkeletalObj_SetProcAnimState(r->vehicle);
    uint32_t raw[16];
    std::memcpy(raw, nf::p32_ptr<const uint8_t>(h) + 0x40 + size_t(idx) * 0x40, 64);
    float im[4][4];
    if (!drv::GetInstanceMatrix(raw, ctx, im)) return false;
    drv::VU0_MATRIX4_mult(im, t, m);
    return true;
}
// [driving] 0x001c9328 RSceneObj::GetInstanceSystemID
uint32_t fx_instance_system(uint32_t scene, uint32_t idx) {
    if (drv::anim::ActWeapon_FromScene(scene)) return drv::anim::ActWeapon_GetInstanceSystemID(scene, idx);
    CarRig* r = g_fx.rig;
    if (!r || scene != nf::p32_u(&r->vehicle.d) || r->vehicle.d.anim == 0) return 0;
    return drv::AnimHandle_GetInstanceSystemID(r->vehicle.d.anim, idx);
}
int32_t fx_lookup_int(uint32_t object, const char* key) {
    const drv::AttributeCollection* a = fx_attrs_of(object);
    return a ? a->lookup_int(key) : 0;
}

// O tráfego (docs/driving.md seção 18): cada tipo sorteado pelo CreateSimTrafficCars com o modelo, os atributos e a
// descrição carregados uma vez; cada carro um PBondCar de classe 3 com o seu render (CarRig).
// Depuração (NF_DRV_TRIGGERS, NF_DRV_RULES): o que um evento de uma lista 'el' aponta: os gatilhos (">índice"), a etapa
// (">e"), o cronômetro e o valor (">c"), o comando 'AICo' e o elemento 'AIEl' (">k", ">a").
std::string event_note(int32_t type, const uint8_t* dd, uint32_t tbase, uint32_t aiel, uint32_t aico) {
    auto w = [dd](int off) {
        uint32_t v = 0;
        std::memcpy(&v, dd + off, 4);
        return v;
    };
    auto idx = [](uint32_t v, uint32_t base, uint32_t size) {
        return (base && v >= base && (v - base) % size == 0 && (v - base) / size < 4096) ? int((v - base) / size) : -1;
    };
    std::string r;
    char b[64];
    if (type == 53 || type == 54 || type == 203) {
        for (int wo = 4; wo <= (type == 203 ? 0x20 : 4); wo += 4)
            if (int i = idx(w(wo), tbase, 0x40); i >= 0) r += ">" + std::to_string(i);
    } else if (type == 40 || type == 41) {
        r = ">e" + std::to_string(int32_t(w(4)));
    } else if (type == 47) {
        float f;
        std::memcpy(&f, dd + 8, 4);
        std::snprintf(b, sizeof b, ">c%d:%g", int32_t(w(4)), double(f));
        r = b;
    } else if (type == 48 || type == 49) {
        r = ">c" + std::to_string(int32_t(w(4)));
    } else if (type == 65) {
        r = ">k" + std::to_string(idx(w(4), aico, 0xa0)) + ">a" + std::to_string(idx(w(0xc), aiel, 0x100));
    } else if (type == 66 || type == 67 || type == 161) {
        r = ">a" + std::to_string(idx(w(4), aiel, 0x100));
    }
    return r;
}

struct TrafficType {
    bool ok = false;
    drv::CarModel model;
    drv::AttributeCollection attrs;
    drv::CarDesc desc;
    float render_offset = 0.0f;
    std::vector<uint16_t> list;
};
struct TrafficCar {
    drv::BondCar* car = nullptr;
    CarRig* rig = nullptr;
    TrafficType* type = nullptr;
};

int run_drive(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    // O GameLoop_StartUp: o RAnimEngine (RTimeData::Init: 1 / a taxa do vídeo) e o SWeaponManager antes do WWorld::Open.
    drv::anim_engine_reset(60.0f);
    drv::weapons_create();
    const uint64_t freq = nf::os::perf_frequency();
    const uint64_t t0 = nf::os::perf_counter();
    drv::Track track;
    std::string err;
    if (!track.load(*disc, a.track, a.viv_given ? a.viv : std::string(), err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    // O carro do jogador: o da linha de comando (padrão vanquish), ou, com a missão, o do 'MSet' da pista (o
    // SMissionManager::GetCarType do GameLoop_StartUp: "helicopter" em Paris; docs/driving.md seção 23).
    std::string car_name = a.car.empty() ? std::string("vanquish") : a.car;
    if (!a.no_traffic && (a.mission || (!a.free_drive && !a.car_pos_given))) {
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        if (map != drv::UGroup_GroupEnd(root)) {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x4d536574u);  // 'MSet'
            if (d != drv::UGroup_DataEnd(map)) {
                const char* ct = reinterpret_cast<const char*>(drv::UData_Data(d));
                if (ct[0] != 0) car_name.assign(ct, strnlen(ct, 0x2c));
            }
        }
    }
    drv::CarModel model;
    if (!model.load(*disc, track.viv(), car_name, err)) {
        std::fprintf(stderr, "nf_driving: o carro: %s\n", err.c_str());
        return 1;
    }
    drv::Bigf arc;
    arc.open(*disc, "DRIVING/" + track.viv());
    {
        // Os atores (driving_app/drive_actors.h): o ActManager::StartUp do GameLoop_StartUp com a lista de bancos do
        // ACT_ANIM_BANK da coleção "world" da pista.
        drv::AttributeCollection wa;
        std::string list, ae;
        if (wa.load(arc, "world", a.track.substr(0, a.track.find('_')), a.track)) wa.find("ACT_ANIM_BANK", list);
        if (!drv::drive_actors::startup(*disc, track.viv(), arc, list, 1.0f / 60.0f, ae))
            std::fprintf(stderr, "nf_driving: os atores: %s\n", ae.c_str());
    }
    drv::AttributeCollection attrs;
    if (!attrs.load(arc, "pvehicle", car_name, a.track, &err)) {
        std::fprintf(stderr, "nf_driving: os atributos: %s\n", err.c_str());
        return 1;
    }
    drv::CarDesc desc;
    if (!drv::read_car_desc(arc, car_name, desc, &err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    // O SECONDARY_TYPE (o GameLoop_StartUp pré-carrega o render dele; o ECustomTransition troca o carro por ele com o
    // PBondCar::ChangeCarType): o modelo, os atributos e a descrição.
    struct SecondaryType {
        drv::CarModel model;
        drv::AttributeCollection attrs;
        drv::CarDesc desc;
        drv::CarTypeData data;
    };
    std::unique_ptr<SecondaryType> secondary;
    {
        std::string sec;
        if (attrs.find("SECONDARY_TYPE", sec) && !sec.empty()) {
            secondary = std::make_unique<SecondaryType>();
            std::string e2;
            if (!secondary->model.load(*disc, track.viv(), sec, e2) ||
                !secondary->attrs.load(arc, "pvehicle", sec, a.track, &e2) ||
                !drv::read_car_desc(arc, sec, secondary->desc, &e2)) {
                std::fprintf(stderr, "nf_driving: o SECONDARY_TYPE %s: %s\n", sec.c_str(), e2.c_str());
                secondary.reset();
            } else {
                secondary->data.name = sec;
                secondary->data.attrs = &secondary->attrs;
                secondary->data.desc = &secondary->desc;
            }
        }
    }
    // Os bancos de ajuste da física (o default.tun de cada um) e o sistema de corpos rígidos.
    auto tun = [&](const std::string& bank) {
        std::string t = read_entry(arc, "data\\tuning\\Physics\\" + bank + "\\" + a.track + ".tun");
        return t.empty() ? read_entry(arc, "data\\tuning\\Physics\\" + bank + "\\default.tun") : t;
    };
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    const std::string rigid = tun("Rigid");
    drv::InitRigidBodySystem(a.track.c_str(), rigid.empty() ? nullptr : rigid.c_str());
    drv::reset_bond_car_tuning();
    const std::string physical = tun("Physical"), friction = tun("Friction");
    drv::load_bond_car_tuning(physical.c_str(), friction.c_str());
    drv::TrackWorld world;
    if (!world.init(track.root(), &err)) {
        std::fprintf(stderr, "nf_driving: o chão: %s\n", err.c_str());
        return 1;
    }
    drv::set_world(&world);
    drv::simulation_reset(60);
    drv::SimRandom_Reset(drv::sim_random());  // o Simulation::Reset (0x1feda0) reinicia o SimRandom
    drv::RRandom_StartUp(drv::render_random(), 0x2e81a11u);  // o Render_InitLibRender (0x1abe70)
    // A missão (docs/driving.md seção 19): o bloco da troca que o BASE.ELF deixaria, os textos (o .loc do bloco), o GHud
    // e, no WWorld::Open antes dos gatilhos, o SMissionManager com as regras ('Rule') e as configurações ('MSet') do
    // 'Map ' da pista, na etapa 0. O Restart dos gatilhos dispara o EStartMission, que põe o carro na largada da missão
    // no primeiro tick. Sem a missão (--free-drive, --car-pos sem --mission, --no-traffic) nada disso existe.
    const bool mission_on = !a.no_traffic && (a.mission || (!a.free_drive && !a.car_pos_given));
    drv::mission_reset();
    drv::event_stats() = drv::EventStats();
    drv::hud_exists() = false;
    drv::Bigf misc_arc;
    const bool have_misc_arc = misc_arc.open(*disc, "DRIVING/MISC.VIV");
    auto read_any = [&](const std::string& key) {
        std::string t = read_entry(arc, key);
        if (t.empty() && have_misc_arc) t = read_entry(misc_arc, key);
        return t;
    };
    std::string mission_note = "desligada";
    if (mission_on) {
        drv::uber_bond_defaults(a.track.c_str(), a.difficulty);
        {
            const std::string loc = read_any(std::string("data\\gfx\\locale\\") + drv::uber_bond().text(0x540));
            if (!drv::locale().load(std::vector<uint8_t>(loc.begin(), loc.end())))
                std::fprintf(stderr, "nf_driving: sem os textos %s\n", drv::uber_bond().text(0x540));
        }
        drv::GHud_Reset(drv::hud());
        drv::hud_exists() = true;
        drv::AttributeCollection world_attrs;
        bool acc = false;
        if (world_attrs.load(arc, "world", a.track.substr(0, a.track.find('_')), a.track))
            acc = world_attrs.lookup_int("ACCUMULATE_SCORES", nullptr) != 0;
        drv::SMissionManager_Construct(acc);  // (o SWeaponManager já existe: o LoadWeaponSet do Load o carrega)
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        drv::UData* rule = nullptr;
        drv::UData* mset = nullptr;
        if (map != drv::UGroup_GroupEnd(root)) {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x52756c65u);  // 'Rule'
            if (d != drv::UGroup_DataEnd(map)) rule = d;
            d = drv::UGroup_DataLocateTag(map, 0x4d536574u);  // 'MSet'
            if (d != drv::UGroup_DataEnd(map)) mset = d;
        }
        if (rule) {
            drv::SMissionManagerData& m = drv::mission_manager();
            drv::SMissionManager_Load(m, rule, mset);
            std::string rules_env;
            if (nf::get_env_narrow("NF_DRV_RULES", &rules_env)) {
                // Depuração: as regras de cada lista (0 a de sempre, 1 + a etapa), a classe, as bandeiras, os campos
                // e os números dos eventos.
                const drv::MissionLists& ml = drv::mission_lists();
                for (int li = 0; li < 65; ++li)
                    for (size_t ri = 0; ri < ml.rules[li].size(); ++ri) {
                        const drv::SMissionRuleData* r = ml.rules[li][ri];
                        int cls = -1;
                        for (int k = 0; k < 15; ++k)
                            if (drv::kRuleVtable[k] == r->vtable) cls = k;
                        std::string evs;
                        if (const auto* l = nf::p32_ptr<const drv::TriggerEvents>(r->events)) {
                            const auto* ev = reinterpret_cast<const drv::TriggerEvent*>(l + 1);
                            drv::UData* tg = drv::UGroup_DataLocateTag(map, 0x54726772u);  // 'Trgr'
                            drv::UData* ae = drv::UGroup_DataLocateTag(map, 0x4149456cu);  // 'AIEl'
                            drv::UData* ac = drv::UGroup_DataLocateTag(map, 0x4149436fu);  // 'AICo'
                            const uint32_t tbase = tg != drv::UGroup_DataEnd(map) ? nf::p32_u(drv::UData_Data(tg)) : 0u;
                            const uint32_t aiel = ae != drv::UGroup_DataEnd(map) ? nf::p32_u(drv::UData_Data(ae)) : 0u;
                            const uint32_t aico = ac != drv::UGroup_DataEnd(map) ? nf::p32_u(drv::UData_Data(ac)) : 0u;
                            for (int k = 0; k < l->count; ++k)
                                evs += " " + std::to_string(ev[k].type) +
                                       event_note(ev[k].type,
                                                  reinterpret_cast<const uint8_t*>(&ev[k]) + ev[k].data_offset, tbase,
                                                  aiel, aico);
                        }
                        int el = -1;
                        drv::UData* aiel = drv::UGroup_DataLocateTag(map, 0x4149456cu);  // 'AIEl'
                        if (aiel != drv::UGroup_DataEnd(map)) {
                            const uint32_t base = nf::p32_u(drv::UData_Data(aiel));
                            if (r->w[1] >= base && (r->w[1] - base) % 0x100 == 0 && (r->w[1] - base) < 0x10000)
                                el = int((r->w[1] - base) / 0x100);
                        }
                        std::printf("regra lista %d #%zu %s ativa %d uma %d inverte %d campos %08x %08x %08x %08x %08x "
                                    "elemento %d eventos%s\n",
                                    li, ri, cls >= 0 ? drv::kRuleName[cls] : "?", int(r->active), int(r->once),
                                    int(r->invert), r->w[0], r->w[1], r->w[2], r->w[3], r->w[4], el, evs.c_str());
                    }
            }
            drv::SMissionManager_ChangeStage(m, 0);
            drv::SMissionManager_Start(m);
            mission_note = "ligada";
        } else {
            mission_note = "sem 'Rule'";
        }
    }
    // O WWorld::Open: os gatilhos da pista (o 'Trgr' do 'Map '; o Init roda o Restart, que dispara os EStartMission).
    // Depois o AIVehicleController::Init e a rede de estradas (o WRoadNetwork::Init), na ordem do GameLoop_StartUp.
    {
        drv::UData* trgr = nullptr;
        drv::UGroup* root = track.root();
        drv::UGroup* map = drv::UGroup_GroupLocateTag(root, 0x4d617020u);
        if (!a.no_traffic && map != drv::UGroup_GroupEnd(root)) {
            drv::UData* d = drv::UGroup_DataLocateTag(map, 0x54726772u);
            if (d != drv::UGroup_DataEnd(map)) trgr = d;
        }
        drv::WTriggerManager_Init(trgr);
        // O que os objetos de cena derrubáveis (driving/game/smackable.h) leem do WWorld: as instâncias e o 'ps  '.
        {
            drv::SmackableWorld& sw = drv::smackable_world();
            const drv::WRender& wr = track.world_render();
            sw.instances = wr.instances;
            sw.instance_count = 0;
            for (int p = 0; p < 5; ++p) sw.instance_count += wr.pass_count[p];
            sw.proc_anims = wr.proc_anims;
            sw.moved.clear();
            drv::fgWorldMap() = track.world_map();  // o WWorld::Open (o RSceneObj::UpdatePosition acha o nó por ele)
            // [driving] 0x001d1ec0 RTimeData::Init (o relógio) e o WWorld::Open (0x0022fef0): o RPathEngine com as
            // instâncias da pista e o 'ps  ' (os caminhos de instância; nenhum em Paris).
            drv::RTimeData_Init(60.0f);
            drv::RPathEngine_Purge();
            drv::RPathEngine_AddInstanceList(sw.instances, sw.proc_anims, uint32_t(sw.instance_count),
                                             ps2::ee::cvt_s(drv::simulation().steps));
        }
        std::string dump;
        if (nf::get_env_narrow("NF_DRV_TRIGGERS", &dump)) {
            // Depuração: os gatilhos (o índice, a forma, as bandeiras, o centro, o raio e os números dos eventos).
            for (int i = 0; i < drv::trigger_manager().count; ++i) {
                const drv::WTrigger* t = drv::trigger_at(i);
                std::string evs;
                if (const auto* l = nf::p32_ptr<const drv::TriggerEvents>(t->events)) {
                    const auto* e = reinterpret_cast<const drv::TriggerEvent*>(l + 1);
                    const uint32_t tbase = nf::p32_u(drv::trigger_at(0));
                    drv::UData* ae = drv::UGroup_DataLocateTag(map, 0x4149456cu);  // 'AIEl'
                    drv::UData* ac = drv::UGroup_DataLocateTag(map, 0x4149436fu);  // 'AICo'
                    const uint32_t aiel = ae != drv::UGroup_DataEnd(map) ? nf::p32_u(drv::UData_Data(ae)) : 0u;
                    const uint32_t aico = ac != drv::UGroup_DataEnd(map) ? nf::p32_u(drv::UData_Data(ac)) : 0u;
                    for (int k = 0; k < l->count; ++k)
                        evs += " " + std::to_string(e[k].type) +
                               event_note(e[k].type, reinterpret_cast<const uint8_t*>(&e[k]) + e[k].data_offset, tbase,
                                          aiel, aico);
                }
                std::printf("gatilho %d forma %d bandeiras %04x pos %.2f,%.2f,%.2f raio %.2f altura %.2f caixa %.2f x %.2f "
                            "frente %.2f,%.2f,%.2f eventos%s\n",
                            i, int(t->shape), unsigned(t->flags), double(t->position[0]), double(t->position[1]),
                            double(t->position[2]), double(t->radius), double(t->height), double(t->width),
                            double(t->depth), double(t->forward[0]), double(t->forward[1]), double(t->forward[2]),
                            evs.c_str());
            }
        }
        // O AIElementController::Construct (os elementos da IA do 'AIEl' do 'Map ', docs/driving.md seção 21), só com a
        // missão: os limites HENCH/VEHICLE/HELI_SLOTS_AVAIL da coleção "world" da pista. Depois o AIVehicleController
        // (os motoristas dos agentes saem da contagem do controlador) e o AIZoneController.
        drv::AIElementController_Destruct();
        if (mission_on && map != drv::UGroup_GroupEnd(root)) {
            drv::UData* aiel = drv::UGroup_DataLocateTag(map, 0x4149456cu);  // 'AIEl'
            if (aiel != drv::UGroup_DataEnd(map)) {
                drv::AttributeCollection wa;
                const bool have = wa.load(arc, "world", a.track.substr(0, a.track.find('_')), a.track);
                drv::AIElementController_Construct(aiel, [&](const char* key, bool* found) -> int32_t {
                    return have ? wa.lookup_int(key, found) : 0;
                });
            }
        }
        drv::AIVehicleController_Init();
        drv::AIZoneController_Init();
        drv::WRoadNetwork_Shutdown();
        if (!a.no_traffic) drv::WRoadNetwork_Init(track.root(), a.track);
        std::string roads_env;
        float box[4];
        if (nf::get_env_narrow("NF_DRV_ROADS", &roads_env) && parse_floats(roads_env, box, 4)) {
            // Depuração: os trechos da rede com uma ponta na caixa x0,z0,x1,z1 (o índice, os nós e as pontas).
            for (int i = 0; i < drv::road_network().num_segments; ++i) {
                const drv::WRoadSegment* sg = drv::road_segment(i);
                const drv::WRoadNode* n0 = drv::road_node(sg->node[0]);
                const drv::WRoadNode* n1 = drv::road_node(sg->node[1]);
                auto in = [&](const drv::WRoadNode* nd) {
                    return nd->position[0] >= box[0] && nd->position[0] <= box[2] && nd->position[2] >= box[1] &&
                           nd->position[2] <= box[3];
                };
                if (!in(n0) && !in(n1)) continue;
                std::printf("trecho %d nos %d,%d de %.1f,%.1f,%.1f a %.1f,%.1f,%.1f bandeiras %04x\n", i, int(sg->node[0]),
                            int(sg->node[1]), double(n0->position[0]), double(n0->position[1]), double(n0->position[2]),
                            double(n1->position[0]), double(n1->position[1]), double(n1->position[2]), unsigned(sg->flags));
            }
        }
    }
    // A largada (GameLoop_StartUp 0x176658): o ponto (padrão: a origem; --car-pos escolhe outro), y = a altura do chão
    // sob ele + 1 (sem chão, a altura fica 0: y = 1). A missão depois põe o carro na largada dela (as regras e os
    // gatilhos do SMissionManager, não portados).
    float start[3] = {a.car_pos[0], a.car_pos[1], a.car_pos[2]};
    float h = 0;
    const bool ground = world.world_height_at_point(start, h);
    start[1] = ee_add_one(h);
    const float dir[3] = {nf::fm::sin(a.car_pos[3]), 0.0f, nf::fm::cos(a.car_pos[3])};
    drv::BondCar* car = new drv::BondCar();
    // O render do carro (o RVehicle que o construtor do PBondCar cria) e a vibração (as portas do IFeedback).
    CarRig* rig = new CarRig();
    rig->attach(*car);
    drv::feedback_reset();
    // O WTargetPicker do global 0x33a970 (seção 24): os carros da IA se registram nele ao nascer.
    drv::WTargetPicker_Construct();
    drv::RigidBody* rb = drv::PBondCar_Construct(*car, 0, car_name, desc, attrs, dir, start);
    if (!rb) {
        std::fprintf(stderr, "nf_driving: sem vaga para o corpo do carro\n");
        return 1;
    }
    drv::PBondCar_SetSecondaryType(*car, attrs);
    drv::simulation().cars.push_back(car);  // o Simulation::SpawnCarObject: o primeiro carro é o do jogador
    // [driving] 0x00195a08 PhysicsObject::SetHitPointLoc (GameLoop_StartUp): a vida do carro do jogador é a do gerente da
    // missão (+0x378); o ValidateHitPoints (0x195a28) não faz nada.
    if (drv::mission_exists()) car->d.hit_point_loc = nf::p32_u(&drv::mission_manager().player_health);
    std::string fx = read_entry(arc, "data\\tuning\\Render\\FX\\" + a.track + ".tun");
    if (fx.empty()) fx = read_entry(arc, "data\\tuning\\Render\\FX\\default.tun");
    rig->post_load(model, *car, read_entry(arc, "data\\tuning\\Render\\SwitchEffects\\default.tun"), fx, car_name);
    // O RSceneObj::UseInstanceList do carro: o RAnimEngine::Handle das instâncias do 'Arti' (com o CARP resolvido: os
    // artigos, as animações e as chaves com as listas 'el'), o dono físico no +0x24 do RVehicle (o EventDynamicData), e o
    // SWeaponManager::SetPlayerCar (o fim do GameLoop_StartUp).
    // O mesmo para o carro novo do PBondCar::ChangeCarType (o gancho abaixo): o RVehicle novo tem o Handle dele.
    std::vector<std::unique_ptr<drv::CarModel>> anim_models;
    auto attach_anim = [&](CarRig* r, drv::BondCar& c, const drv::CarModel& m, const std::string& name) {
        auto am = std::make_unique<drv::CarModel>();
        am->set_resolve_carp(true);
        std::string anim_err;
        uint32_t inst = 0, n = 0, dmg = 0;
        r->vehicle.d.physics = nf::p32_u(&c.d);
        if (am->load(*disc, track.viv(), name, anim_err) && drv::carp_instance_list(am->carp_root(), inst, n, dmg) &&
            n == m.instances().size()) {
            r->vehicle.d.anim = drv::AnimHandle_Create(n, uint32_t(drv::simulation().steps), inst, dmg,
                                                       nf::p32_u(&r->vehicle.d));
        }
        anim_models.push_back(std::move(am));
    };
    attach_anim(rig, *car, model, car_name);
    {
        drv::WeaponsEnv& we = drv::weapons_env();
        we.anim.is_playing = [](uint32_t h, int32_t s) { return h != 0 && drv::AnimHandle_IsSystemPlaying(h, uint32_t(s)); };
        we.anim.state = [](uint32_t h, int32_t s) {
            return h != 0 ? int32_t(drv::AnimHandle_GetSystemState(h, uint32_t(s))) : 0;
        };
        we.anim.stimulus = [](uint32_t h, int32_t s, int32_t st, int32_t t, int32_t mode) {
            if (h != 0) drv::AnimHandle_ProcessStimuli(h, uint32_t(s), uint32_t(st), uint32_t(t), uint32_t(mode));
        };
        we.anim_handle = [](uint32_t scene) { return scene ? nf::p32_ptr<drv::RVehicleData>(scene)->anim : 0u; };
        we.lookup_int = [&attrs](const char* k, bool* f) { return attrs.lookup_int(k, f); };
        we.lookup_bool = [&attrs](const char* k) { return attrs.lookup_bool(k); };
        we.player_position = [rb](float out[3]) { std::memcpy(out, rb->pos, 12); };
        we.car_type = [&car_name]() { return car_name.c_str(); };
        we.car_is_sub = [car]() { return int32_t(car->physics()->is_sub); };
        we.camera_exists = []() { return drv::current_player_camera() != nullptr; };
        we.camera_aiming = []() {
            return drv::current_player_camera() && drv::RPlayerCamera_CameraAiming(*drv::current_player_camera()) != 0;
        };
        we.selected_target = []() { return drv::WTargetPicker_GetSelectedTarget(); };
        we.hud_exists = []() { return drv::hud_exists(); };
        we.hud_2a0 = []() { return 0; };
        we.hud_lava_damage = []() { return false; };
        we.player_body = [rb](float pos[4], float fwd[4], float& back) {
            std::memcpy(pos, rb->pos, 16);
            std::memset(fwd, 0, 16);
            back = 0.0f;
        };
        drv::SWeaponManager_SetPlayerCar(*drv::weapon_manager(), nf::p32_u(&rig->vehicle.d));
        // O EFireWeapon e as explosões (driving/game/missile.h, explosion.h).
        g_fx.rig = rig;
        g_fx.car = car;
        g_fx.render_offset = drv::CarDesc_RenderOffset(desc);
        g_fx.attrs = &attrs;
        g_fx.player_image = nf::p32_u(&car->d);
        drv::effects_reset();
        drv::shells_reset();  // as balas (driving/game/shells.h): os globais, as contas, o mapa das janelas
        drv::EffectsEnv& fe = drv::effects_env();
        fe.instance_position = fx_instance_position;
        fe.instance_system = fx_instance_system;
        fe.lookup_int = fx_lookup_int;
        // As balas (seção 24): os atributos booleanos dos carros, o alvo do carro do jogador (a entrada +0x29c: o farol
        // de alvo do PBondCar) e o RPlayerCamera::WeaponFired.
        drv::ShellsEnv& se = drv::shells_env();
        se.lookup_bool = [](uint32_t image, const char* key) {
            const drv::AttributeCollection* at = fx_attrs_of(image);
            return at ? at->lookup_bool(key) : false;
        };
        se.player_targetable = []() { return g_fx.car ? g_fx.car->d.target_beacon : 0u; };
        // Os passageiros (seção 26): o cano das armas (RSceneObj::LocateFX do ActWeapon) e os pneus do carro deles
        // (a entrada +0x24 do personagem, GetVehiclePtr, e a +0x13c do carro, GetNumTires).
        se.locate_fx = [](uint32_t scene, uint32_t effect, float m[4][4]) {
            return drv::anim::ActWeapon_LocateFX(scene, effect, m);
        };
        se.actor_vehicle_value = [](uint32_t actor, int32_t& value) {
            const drv::AIVtable* v = drv::ai_vtable(nf::p32_ptr<const drv::AICharacterData>(actor)->vtable);
            const uint32_t car = v && v->slot24 ? uint32_t(v->slot24(actor)) : 0u;
            drv::BondCar* c = car ? drv::ai_car_from_physics(car) : nullptr;
            if (!c) return false;
            value = 0;
            for (int i = 0; i < 4; ++i) value += c->d.tyre_damage_points[i] != 0;
            return true;
        };
        fe.level = a.track.c_str();  // o nome do nível do UberBond (0x1004f4) é o da pista: "paris_mis01"
    }
    // Os efeitos (driving/fx, drv_fx.h): a galeria e o sistema de partículas.
    DrvFxApp fx_app;
    fx_app.init(have_misc_arc ? &misc_arc : nullptr, &arc, fx_instance_position);
    fx_app.debug_effects = a.play_effects;
    fx_app.debug_explosions = a.spawn_explosions;
    float render_offset = drv::CarDesc_RenderOffset(desc);
    // O modelo desenhado (o do tipo corrente: o ChangeCarType troca pelo secundário).
    drv::CarModel* cur_model = &model;
    std::string cur_name = car_name;
    // O tráfego (GameLoop_StartUp: AIRoadSpawn::Init, AIVehicleController::CreateSimTrafficCars, RegisterSimCarList):
    // MAX_TRAFFIC e NUM_TRAFFIC_TYPES_PS2 da coleção "world" da pista (paris.atr: 20 e 3), cada carro um PBondCar de
    // classe 3 montado na origem e guardado longe (o UnSpawn do RegisterSimCarList) até um ESpawnTraffic de um gatilho
    // ou o AIRoadSpawn o pôr na rua. A cor sorteada não é desenhada (o modelo fica com a cor da carga: contada).
    drv::AIRoadSpawn_Init();
    std::map<std::string, std::unique_ptr<TrafficType>> traffic_types;
    std::vector<TrafficCar> traffic;
    uint64_t traffic_missing = 0, traffic_colours = 0;
    int traffic_num_types = 0, traffic_max = 0;
    // Um tipo de carro (o modelo, os atributos e a descrição carregados uma vez): o tráfego e os veículos dos elementos
    // da IA (driving/ai/element_controller.h).
    auto type_of = [&](const char* name) -> TrafficType* {
        auto it = traffic_types.find(name);
        if (it != traffic_types.end()) return it->second.get();
        auto t = std::make_unique<TrafficType>();
        std::string e2;
        t->ok = t->model.load(*disc, track.viv(), name, e2) && t->attrs.load(arc, "pvehicle", name, a.track, &e2) &&
                drv::read_car_desc(arc, name, t->desc, &e2);
        if (t->ok) {
            t->render_offset = drv::CarDesc_RenderOffset(t->desc);
            // Sem lista de vista (o +0x348 do RVehicle só o carro do jogador recebe, pelo SetViewDrawList): o
            // RVehicle::Render (0x1d35d8) desenha todas as instâncias (DrawInstanceList).
            for (size_t ii = 0; ii < t->model.instances().size(); ++ii) t->list.push_back(uint16_t(ii));
        } else {
            std::fprintf(stderr, "nf_driving: o tráfego %s: %s\n", name, e2.c_str());
        }
        TrafficType* r = t.get();
        traffic_types[name] = std::move(t);
        return r;
    };
    if (!a.no_traffic) {
        drv::AttributeCollection world_attrs;
        const bool have_world = world_attrs.load(arc, "world", a.track.substr(0, a.track.find('_')), a.track);
        bool found = false;
        traffic_num_types = have_world ? world_attrs.lookup_int("NUM_TRAFFIC_TYPES_PS2", &found) : 0;
        if (!found) traffic_num_types = -1;
        found = false;
        traffic_max = have_world ? world_attrs.lookup_int("MAX_TRAFFIC", &found) : 0;
        if (!found) traffic_max = 20;
        drv::AIVehicleController_CreateSimTrafficCars(
            traffic_num_types, traffic_max,
            [&](const char* name) -> uint32_t {
                // PVehicle::NumColoursAttrib (as coleções de Paris não têm NUM_COLOURS: 0, então uma cor só)
                TrafficType* t = type_of(name);
                return t && t->ok ? uint32_t(t->attrs.lookup_int("NUM_COLOURS")) : 0u;
            },
            [&](const char* name, uint32_t colour) {
                if (colour != 0) ++traffic_colours;
                TrafficType* t = type_of(name);
                if (!t || !t->ok) {
                    ++traffic_missing;
                    return;
                }
                TrafficCar tc;
                tc.car = new drv::BondCar();
                tc.rig = new CarRig();
                tc.rig->player = false;
                tc.rig->attach(*tc.car);
                const float zero[3] = {0.0f, 0.0f, 0.0f};  // Simulation::SpawnCarObject(3, tipo, cor, (0,0,0), (0,0,0))
                if (!drv::PBondCar_Construct(*tc.car, 3, name, t->desc, t->attrs, zero, zero)) {
                    ++traffic_missing;
                    return;
                }
                tc.rig->post_load(t->model, *tc.car, std::string(), fx, name, false);
                tc.type = t;
                drv::simulation().cars.push_back(tc.car);
                traffic.push_back(tc);
            });
        drv::AIVehicleController_RegisterSimCarList();
    }
    // Os veículos dos elementos da IA (driving/ai/element_controller.h): o RSceneObj::PreLoad do construtor do
    // controlador carrega os modelos antes (aqui os tipos, como os do tráfego); o WakeUp pede o carro à fábrica
    // (Simulation::SpawnCarObject: o PBondCar da classe com o render) e o GoToSleep o devolve.
    std::vector<TrafficCar> ai_cars;
    uint64_t ai_missing = 0;
    // O fim: o controlador some (os carros dele são da simulação) e a fábrica deixa de apontar para este quadro.
    auto ai_end = [&]() {
        drv::AIElementController_Destruct();
        drv::ai_car_factory() = drv::AICarFactory{};
        g_fx.ai_attrs.clear();
        for (TrafficCar& tc : ai_cars) delete tc.rig;
        ai_cars.clear();
    };
    if (drv::AIElementControllerData* ctl = drv::ai_controller()) {
        for (int i = 0; i < ctl->count; ++i)
            if (const drv::AIElement* e = drv::ai_element(i))
                if (e->type == 1 && e->heli == 0) type_of(e->name);
        drv::ai_car_factory().spawn = [&](int32_t car_class, const char* name, uint8_t colour, const float d[3],
                                          const float p[3]) -> drv::BondCar* {
            (void)colour;  // a cor do carro não é desenhada (como no tráfego)
            TrafficType* t = type_of(name);
            if (!t || !t->ok) {
                ++ai_missing;
                return nullptr;
            }
            TrafficCar tc;
            tc.car = new drv::BondCar();
            tc.rig = new CarRig();
            tc.rig->player = false;
            tc.rig->attach(*tc.car);
            if (!drv::PBondCar_Construct(*tc.car, car_class, name, t->desc, t->attrs, d, p)) {
                ++ai_missing;
                delete tc.rig;
                delete tc.car;
                return nullptr;
            }
            tc.rig->post_load(t->model, *tc.car, std::string(), fx, name, false);
            tc.type = t;
            g_fx.ai_attrs.push_back({nf::p32_u(&tc.car->d), &t->attrs});
            drv::simulation().cars.push_back(tc.car);  // Simulation::SpawnPhysicsObject
            ai_cars.push_back(tc);
            return tc.car;
        };
        drv::ai_car_factory().remove = [&](drv::BondCar* c) {
            for (size_t i = 0; i < g_fx.ai_attrs.size(); ++i)
                if (g_fx.ai_attrs[i].first == nf::p32_u(&c->d)) {
                    g_fx.ai_attrs.erase(g_fx.ai_attrs.begin() + std::ptrdiff_t(i));
                    break;
                }
            for (size_t i = 0; i < ai_cars.size(); ++i)
                if (ai_cars[i].car == c) {
                    delete ai_cars[i].rig;
                    ai_cars.erase(ai_cars.begin() + std::ptrdiff_t(i));
                    return;
                }
        };
    }
    // Depuração: os gatilhos pedidos disparam na largada (como se o carro do jogador os tocasse: o índice é a vaga dele);
    // os eventos rodam no primeiro tick, com os da largada.
    for (const auto& ft : a.fire_triggers)
        if (ft.second <= 0.0f && ft.first >= 0 && ft.first < drv::trigger_manager().count)
            drv::WTrigger_FireEvents(*drv::trigger_at(ft.first), 0, rb->slot, 0);
    const double load_ms = 1000.0 * double(int64_t(nf::os::perf_counter() - t0)) / double(int64_t(freq));

    // A cena: o mundo (como run_track) e o carro na matriz do corpo.
    drv::DrvScene scene;
    track_fog(read_entry(arc, "data\\tuning\\Render\\Fog\\" + a.track + ".tun"), scene.k, scene.fog_color);
    for (int c = 0; c < 3; ++c) scene.clear[c] = scene.fog_color[c] / 255.0f;
    {
        const drv::CarConstants& ck = model.constants();
        std::memcpy(scene.k.light_dir_t, ck.light_dir_t, sizeof ck.light_dir_t);
        std::memcpy(scene.k.light_col, ck.light_col, sizeof ck.light_col);
        std::memcpy(scene.k.ambient, ck.ambient, sizeof ck.ambient);
        std::memcpy(scene.k.refl_info2, ck.refl_info2, sizeof ck.refl_info2);
        std::memcpy(model.constants().fog, scene.k.fog, sizeof scene.k.fog);
        for (auto& tt : traffic_types)
            if (tt.second->ok) std::memcpy(tt.second->model.constants().fog, scene.k.fog, sizeof scene.k.fog);
    }
    std::vector<uint16_t> car_list = model.view_draw_list(1);
    if (secondary) std::memcpy(secondary->model.constants().fog, scene.k.fog, sizeof scene.k.fog);
    // [driving] 0x00185e50 PBondCar::ChangeCarType, a parte do render e do som (driving/game/heli_section.h): o RVehicle
    // novo sobre o modelo pré-carregado (o CarRig refeito), a lista de desenho e o deslocamento do render dele.
    drv::heli_car_type_lookup() = [&](const std::string& name) -> const drv::CarTypeData* {
        if (secondary && nf::str_icmp(name.c_str(), secondary->data.name.c_str()) == 0) return &secondary->data;
        return nullptr;
    };
    drv::heli_change_car_hook() = [&](drv::BondCar& c, const drv::CarTypeData& t) {
        if (!secondary || &t != &secondary->data) return;
        CarRig* nr = new CarRig();
        nr->attach(c);
        nr->post_load(secondary->model, c, read_entry(arc, "data\\tuning\\Render\\SwitchEffects\\default.tun"), fx,
                      t.name);
        rig = nr;  // o RVehicle antigo fica (o port não o apaga: o render dele não é mais chamado)
        cur_model = &secondary->model;
        cur_name = t.name;
        car_list = secondary->model.view_draw_list(1);
        render_offset = drv::CarDesc_RenderOffset(secondary->desc);
        // O RSceneObj::UseInstanceList do RVehicle novo (o Handle das instâncias) e o SWeaponManager::SetPlayerCar do
        // SMissionManager::InitPlayerCar; o EFireWeapon e as explosões passam a ler o carro novo.
        attach_anim(nr, c, secondary->model, t.name);
        if (drv::weapon_manager()) drv::SWeaponManager_SetPlayerCar(*drv::weapon_manager(), nf::p32_u(&nr->vehicle.d));
        g_fx.rig = nr;
        g_fx.render_offset = render_offset;
        g_fx.attrs = &secondary->attrs;
        drv::WeaponsEnv& we2 = drv::weapons_env();
        const drv::AttributeCollection* sa = &secondary->attrs;
        we2.lookup_int = [sa](const char* k, bool* f) { return sa->lookup_int(k, f); };
        we2.lookup_bool = [sa](const char* k) { return sa->lookup_bool(k); };
        we2.car_type = [&cur_name]() { return cur_name.c_str(); };
    };
    // A câmera do jogador (driving/camera/player_camera.h): as tabelas do data\render\camera.ini do .VIV da missão para
    // este carro, o ruído, a âncora (o corpo do carro) e o RPlayerCamera, atualizado a cada passo da simulação como no
    // jogo (UpdateCamera só anda quando o passo muda).
    drv::CameraTables* cam_tables = new drv::CameraTables();
    {
        drv::IniFile ini;
        const std::string text = read_entry(arc, "data\\render\\camera.ini");
        ini.open(std::vector<uint8_t>(text.begin(), text.end()));
        drv::CameraCarInfo info;
        info.type = car_name;
        // O tipo secundário (PBondCar::GetSecondaryType): o helicóptero de Paris também lê as câmeras do vanquish.
        if (secondary) info.secondary = secondary->data.name;
        cam_tables->load(ini, info);
    }
    drv::Noise_Init();
    drv::CameraAnchor* anchor = new drv::CameraAnchor();
    anchor->kind = 1;
    anchor->flags = 0;
    anchor->body = int16_t(rb->slot);
    anchor->object = car;
    drv::CameraContext cam_ctx;
    cam_ctx.sim = &drv::simulation();
    cam_ctx.world = &world;
    cam_ctx.cars = {anchor};
    drv::PlayerCamera* cam = new drv::PlayerCamera();
    cam->construct(*cam_tables, cam_ctx);
    drv::effects_env().camera_1f4 = &cam->d.weapon_anim_step;  // o RPlayerCamera::WeaponFired do EFireWeapon
    drv::current_player_camera() = cam;  // o DAT_00333ed8 +4 (a mira pela câmera, o WTargetPicker)
    // O que a câmera lê da missão e do bloco da troca (os modos da seção 23), atualizado antes de cada uso; e a câmera
    // dos eventos de câmera da missão (driving/game/camera_events.h).
    cam_ctx.track = a.track;
    auto refresh_cam_ctx = [&]() {
        if (drv::mission_exists()) {
            const drv::SMissionManagerData& mm = drv::mission_manager();
            cam_ctx.mission_state = mm.state;
            cam_ctx.mission_auto_drive = mm.auto_drive;
            cam_ctx.mission_missile = mm.missile;
            cam_ctx.mission_difficulty = mm.difficulty;
            cam_ctx.mission_cinematic = mm.cinematic_mode;
            cam_ctx.mission_5ec = mm.unknown5ec;
        }
        cam_ctx.uber_a40 = int32_t(drv::uber_bond().word(0xa40));
        cam_ctx.uber_a44 = int32_t(drv::uber_bond().word(0xa44));
        // O alvo escolhido pelo WTargetPicker (GetSelectedTarget: a posição do WTargetable).
        const uint32_t sel = drv::WTargetPicker_GetSelectedTarget();
        cam_ctx.target = sel ? nf::p32_ptr<const drv::WTargetableData>(sel)->pos : nullptr;
    };
    refresh_cam_ctx();
    drv::camera_events_env().camera = cam;
    drv::camera_events_env().ctx = &cam_ctx;
    // O AICharacter::Init (as mãos de Bond; o PS2 o chama no fim do GameLoop_StartUp, antes da câmera: o Init não a lê).
    drv::drive_actors::characters_init(&attrs, reinterpret_cast<const uint8_t*>(&cam->d), [&]() {
        refresh_cam_ctx();
        cam->update(cam_ctx);
    }, &cam->statics.zoom_button);
    // O que o tráfego lê da câmera (AIRoadSpawn: os carros nascem fora da vista; a matriz do RCamera do mundo).
    drv::traffic_world().camera = reinterpret_cast<const float(*)[4]>(cam->d.matrix);
    float eye[3] = {0, 0, 0};
    auto chase = [&](float) {
        refresh_cam_ctx();
        cam->update(cam_ctx);
        std::memcpy(eye, cam->d.matrix + 12, sizeof eye);
    };
    // A matriz de visão a partir do referencial da câmera (as linhas: direita, cima, frente, o olho) e o campo de visão:
    // o RViewCamera do jogo projeta 2 x fov na horizontal da tela 4:3 (EAGL::ViewPort::SetPerspective, lido no
    // nightfire-cxbx); aqui o vertical dessa tela (Hor+ nas janelas mais largas).
    auto camera_view = [&](float view[16]) {
        const float* m = cam->d.matrix;
        const float vm[16] = {m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1};
        std::memcpy(view, vm, sizeof vm);
        for (int c = 0; c < 3; ++c) view[12 + c] = -(m[c * 4] * m[12] + m[c * 4 + 1] * m[13] + m[c * 4 + 2] * m[14]);
    };
    auto camera_fov_y = [&]() {
        const double half = double(cam->d.fov) * 3.14159265358979 / 180.0;
        return float(2.0 * std::atan(std::tan(half) * 0.75) * 180.0 / 3.14159265358979);
    };
    std::vector<drv::Track::Draw> draws;
    std::string cull_line;
    uint64_t cull_ticks = 0, cull_calls = 0;  // o custo do corte na CPU (o relatório)
    uint64_t rig_ticks = 0;  // o tempo do render do carro na CPU (o --bench-frames)
    int traffic_drawn = 0;   // os carros do tráfego desenhados no último quadro
    int missiles_drawn = 0;  // os mísseis desenhados no último quadro
    std::memcpy(scene.light_sources, &track.lights().light_sources, sizeof scene.light_sources);
    // O HUD mínimo da missão (driving/game/hud.h): a fonte de depuração do disco (data\render\debug.sfn do MISC.VIV) na
    // textura da interface; a tela do PS2 (640 x 448) na janela pela altura, centrada.
    drv::FontFile hud_font;
    bool hud_font_ok = false;
    std::vector<uint8_t> hud_atlas;
    if (drv::hud_exists() && !a.no_hud) {
        const std::string f = read_any("data\\render\\debug.sfn");
        hud_font_ok = drv::font_parse(reinterpret_cast<const uint8_t*>(f.data()), f.size(), hud_font);
        if (hud_font_ok) {
            // O alfa na escala do GS (0x80 = opaco) quando nenhum passa de 0x80; branco opaco no fim do atlas para as
            // faixas sem textura.
            uint8_t amax = 0;
            for (size_t i = 3; i < hud_font.rgba.size(); i += 4) amax = std::max(amax, hud_font.rgba[i]);
            hud_atlas = hud_font.rgba;
            if (amax <= 0x80)
                for (size_t i = 3; i < hud_atlas.size(); i += 4) hud_atlas[i] = uint8_t(std::min(255, hud_atlas[i] * 2));
        } else {
            std::fprintf(stderr, "nf_driving: sem a fonte data\\render\\debug.sfn (o HUD sem texto)\n");
        }
    }
    uint64_t hud_lines_drawn = 0;
    auto push_hud = [&](int w, int hgt) {
        scene.sprites.clear();
        if (!drv::hud_exists() || a.no_hud) return;
        const drv::HudView v = drv::hud_view(drv::hud());
        const float sc = float(hgt) / 448.0f, x0 = float(w) * 0.5f - 320.0f * sc;
        auto box = [&](float x, float y, float bw, float bh, uint32_t colour) {
            drv::DrvSprite s;
            s.x0 = x;
            s.y0 = y;
            s.x1 = x + bw;
            s.y1 = y + bh;
            s.colour = colour;
            scene.sprites.push_back(s);
        };
        // As faixas da largura da tela do PS2 (as tarjas, o fundo do resultado) vão de uma borda à outra da janela.
        for (const drv::HudBox& b : v.boxes) {
            if (b.x <= 0.0f && b.w >= 640.0f) box(0, b.y * sc, float(w), b.h * sc, b.colour);
            else box(x0 + b.x * sc, b.y * sc, b.w * sc, b.h * sc, b.colour);
        }
        if (hud_font_ok) {
            for (const drv::HudLine& l : v.lines) {
                // Quebra em linhas de até 600 pixels do PS2 (os textos do .loc não têm quebras).
                std::vector<std::string> rows;
                std::string cur;
                size_t p = 0;
                while (p <= l.text.size()) {
                    size_t e = l.text.find(' ', p);
                    if (e == std::string::npos) e = l.text.size();
                    const std::string word = l.text.substr(p, e - p);
                    const std::string tryit = cur.empty() ? word : cur + " " + word;
                    if (!cur.empty() && float(hud_font.width(tryit)) * l.scale > 600.0f) {
                        rows.push_back(cur);
                        cur = word;
                    } else {
                        cur = tryit;
                    }
                    p = e + 1;
                }
                if (!cur.empty()) rows.push_back(cur);
                float y = l.y;
                for (const std::string& row : rows) {
                    const float tw = float(hud_font.width(row)) * l.scale;
                    const float x = l.centered ? l.x - tw * 0.5f : l.x;
                    // A sombra (um pixel do PS2 para baixo e para a direita) e o texto.
                    for (int pass = 0; pass < 2; ++pass) {
                        const float d = pass == 0 ? 1.0f : 0.0f;
                        const uint32_t colour = pass == 0 ? (l.colour & 0xff000000u) : l.colour;
                        for (const drv::FontQuad& q :
                             drv::font_layout(hud_font, row, x0 + (x + d) * sc, (y + d) * sc, l.scale * sc)) {
                            drv::DrvSprite s;
                            s.x0 = q.x0;
                            s.y0 = q.y0;
                            s.x1 = q.x1;
                            s.y1 = q.y1;
                            s.u0 = q.u0;
                            s.v0 = q.v0;
                            s.u1 = q.u1;
                            s.v1 = q.v1;
                            s.colour = colour;
                            s.textured = true;
                            scene.sprites.push_back(s);
                        }
                    }
                    y += float(hud_font.line_height) * l.scale;
                    ++hud_lines_drawn;
                }
            }
        }
        if (v.black > 0.0f) box(0, 0, float(w), float(hgt), uint32_t(std::min(255.0f, v.black * 255.0f)) << 24);
    };
    std::function<void(int, int)> compose_game;
    // --shot-look (depuração): a matriz da câmera que olha o carro do elemento (falso: não há).
    auto shot_look_matrix = [&](float out[16]) -> bool {
        drv::AIElement* le = a.shot_look_element >= 0 ? drv::ai_element(a.shot_look_element) : nullptr;
        drv::BondCar* lc = le && le->physics ? drv::ai_car_from_physics(le->physics) : nullptr;
        drv::RigidBody* lb = lc ? lc->body() : nullptr;
        if (!lb) return false;
        const float yaw = a.shot_look[2] * 3.14159265f / 180.0f;
        const float e3[3] = {lb->pos[0] + std::sin(yaw) * a.shot_look[0], lb->pos[1] + a.shot_look[1],
                             lb->pos[2] + std::cos(yaw) * a.shot_look[0]};
        float f[3] = {lb->pos[0] - e3[0], lb->pos[1] + 1.0f - e3[1], lb->pos[2] - e3[2]};
        float n = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& x : f) x /= n;
        float r[3] = {f[2], 0.0f, -f[0]};
        n = std::sqrt(r[0] * r[0] + r[2] * r[2]);
        r[0] /= n;
        r[2] /= n;
        const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
        const float m[16] = {r[0], r[1], r[2], 0, u[0], u[1], u[2], 0, f[0], f[1], f[2], 0, e3[0], e3[1], e3[2], 1};
        std::memcpy(out, m, sizeof m);
        return true;
    };
    auto compose = [&](int w, int hgt) {
        // A matriz da câmera trocada só durante o desenho (os efeitos andam com a câmera do jogo: o RRandom do render
        // que as partículas sorteiam também decide o jogo, e a depuração não pode mudá-lo).
        float m[16];
        if (!shot_look_matrix(m)) return compose_game(w, hgt);
        float saved[16];
        std::memcpy(saved, cam->d.matrix, sizeof saved);
        std::memcpy(cam->d.matrix, m, sizeof m);
        std::memcpy(eye, m + 12, 3 * sizeof(float));
        compose_game(w, hgt);
        std::memcpy(cam->d.matrix, saved, sizeof saved);
        std::memcpy(eye, saved + 12, sizeof eye);
    };
    compose_game = [&](int w, int hgt) {
        scene.items.clear();
        scene.quads.clear();
        push_hud(w, hgt);
        // [driving] 0x001d1ef8 RTimeData::Update (StartFrameRender): o passo da simulação deste desenho.
        drv::RTimeData_Update(drv::simulation().steps);
        std::memcpy(scene.sim_step, drv::rtime_data().time_data, sizeof scene.sim_step);
        camera_view(scene.view);
        drv::drv_perspective(camera_fov_y(), float(w) / float(hgt), 0.17f, 3000.0f, scene.proj);
        std::memcpy(scene.eye, eye, sizeof eye);
        scene.spec_from_view = true;
        float vp[16];
        drv::drv_mat_mul(scene.view, scene.proj, vp);
        draws.clear();
        // O carro, no ponto do DoRender em que os carros entram: RVehicle::UpdatePosition e Render (CarRig) com o modo
        // da câmera (o +0x07 a vista de dentro; o tipo 1 ou a transição para ele esconde o carro do jogador).
        auto push_car = [&]() {
            const drv::CameraModeInfo& mi = cam_tables->mode(cam->d.mode);
            const bool hidden =
                mi.type == 1 || (cam->d.transition > 0 && cam_tables->mode(cam->d.prev_selectable).type == 1);
            const uint64_t rig0 = nf::os::perf_counter();
            rig->frame(*car, *cur_model, render_offset, mi.interior_view != 0, hidden, int32_t(drv::simulation().steps),
                       car_list, scene);
            // Os carros do tráfego na rua (os guardados longe pelo UnSpawn têm o corpo congelado, estado 3). O jogo os
            // põe na cena pela visibilidade do RRenderHigh (não portada): aqui os que estão a menos de 400 m do olho.
            traffic_drawn = 0;
            for (TrafficCar& tc : traffic) {
                const drv::RigidBody* b = tc.car->body();
                if (b->state == 3) continue;
                const float dx = b->pos[0] - eye[0], dy = b->pos[1] - eye[1], dz = b->pos[2] - eye[2];
                if (dx * dx + dy * dy + dz * dz > 400.0f * 400.0f) continue;
                tc.rig->frame(*tc.car, tc.type->model, tc.type->render_offset, false, false,
                              int32_t(drv::simulation().steps), tc.type->list, scene);
                ++traffic_drawn;
            }
            // Os veículos dos elementos da IA acordados, pela mesma regra.
            for (TrafficCar& tc : ai_cars) {
                const drv::RigidBody* b = tc.car->body();
                if (!b || b->state == 3) continue;
                const float dx = b->pos[0] - eye[0], dy = b->pos[1] - eye[1], dz = b->pos[2] - eye[2];
                if (dx * dx + dy * dy + dz * dz > 400.0f * 400.0f) continue;
                tc.rig->frame(*tc.car, tc.type->model, tc.type->render_offset, false, false,
                              int32_t(drv::simulation().steps), tc.type->list, scene);
                ++traffic_drawn;
            }
            // Os mísseis (driving/game/missile.h): o RSceneObj do Missile desenha a instância do carro que o disparou
            // (RSceneObj::UseInstance com a identidade) na pose do corpo simples; escondido enquanto morre.
            missiles_drawn = 0;
            for (drv::PhysicsObject* po : drv::simulation().missiles) {
                const drv::Missile* msl = dynamic_cast<const drv::Missile*>(po);
                if (!msl || msl->source_scene != nf::p32_u(&rig->vehicle.d) || msl->d.decaying != 0) continue;
                const int32_t ii = msl->source_instance;
                const drv::CarModel& mmodel = *cur_model;
                if (ii < 0 || size_t(ii) >= mmodel.instances().size() || mmodel.instances()[size_t(ii)].part < 0) continue;
                const drv::SimpleRigidBody* sb = msl->body();
                if (!sb) continue;
                float mm[4][4];
                drv::VU0_quattom4(sb->quat, mm);
                mm[0][3] = mm[1][3] = mm[2][3] = 0.0f;
                mm[3][0] = sb->pos[0];
                mm[3][1] = sb->pos[1];
                mm[3][2] = sb->pos[2];
                mm[3][3] = 1.0f;
                drv::DrvItem it;
                it.model = &mmodel;
                it.part = mmodel.instances()[size_t(ii)].part;
                std::memcpy(it.world, mm, 64);
                it.switch_data = rig->switch_data;
                it.damage = rig->damage;
                scene.items.push_back(it);
                ++missiles_drawn;
            }
            drv::drive_actors::draw(scene.items, cam->d.matrix, cam->d.fov);  // os atores (as mãos de Bond)
            rig_ticks += nf::os::perf_counter() - rig0;
        };
        if (!a.no_cull && track.has_scene_tree()) {
            // O corte do jogo (Track::cull) com o RCamera da câmera do jogador.
            drv::Track::Culled culled;
            const uint64_t c0 = nf::os::perf_counter();
            track.cull(cam->d.matrix, cull_fov_for_window(cam->d.fov, float(w) / float(hgt)), false, culled);
            cull_ticks += nf::os::perf_counter() - c0;
            ++cull_calls;
            cull_line = push_culled_track(track, culled, eye, a.no_sky, scene, push_car);
            char cb[96];
            std::snprintf(cb, sizeof cb, " | corte_ms %.3f",
                          1000.0 * double(int64_t(cull_ticks)) / double(int64_t(nf::os::perf_frequency())) /
                              double(cull_calls));
            cull_line += cb;
            for (auto* v : {&culled.sky, &culled.world, &culled.scene_objects, &culled.scene_objects_last,
                            &culled.pass1, &culled.pass0})
                draws.insert(draws.end(), v->begin(), v->end());
            scene.fx.clear();
            fx_app.push(scene, cam->d.fov, float(w) / float(hgt));
            return;
        }
        track.visible(vp, eye, draws);
        for (int pass : {4, 3, 2}) {
            if (pass == 4 && a.no_sky) continue;
            for (const auto& d : draws)
                if (d.pass == pass) push_track_draw(track, d, eye, scene);
        }
        push_car();
        for (int pass : {1, 0}) {
            std::vector<drv::Track::Draw> sorted;
            for (const auto& d : draws)
                if (d.pass == pass) sorted.push_back(d);
            std::stable_sort(sorted.begin(), sorted.end(),
                             [](const drv::Track::Draw& x, const drv::Track::Draw& y) { return x.distance > y.distance; });
            for (const auto& d : sorted) push_track_draw(track, d, eye, scene);
        }
        cull_line = "corte volume (sem a arvore de cenas)";
        scene.fx.clear();
        fx_app.push(scene, cam->d.fov, float(w) / float(hgt));
    };
    DriveInput input;
    drv::InputConfigManager* icm = new drv::InputConfigManager();
    drv::InputUberBond input_uber;
    {
        // O InputConfigManager (InitAndPreload): o Master.def escolhe a configuração do carro (a 0 do bloco da troca:
        // "Driving 1 / drivecfg.def") e a tabela é o FrontEnd.def mais ela. Os arquivos da missão, ou os do MISC.VIV.
        drv::Bigf misc;
        const bool have_misc = misc.open(*disc, "DRIVING/MISC.VIV");
        auto loader = [&](const std::string& path) {
            std::string key = path;
            for (char& ch : key)
                if (ch == '/') ch = '\\';
            std::string t = read_entry(arc, key);
            if (t.empty() && have_misc) t = read_entry(misc, key);
            return t;
        };
        drv::InputConfigManager_InitAndPreload(*icm, loader, input_uber, input.map);
        if (icm->d.count[0] == 0) {
            std::fprintf(stderr, "nf_driving: sem o data\\Control\\Master.def\n");
            return 1;
        }
        input.io.inverted = icm->d.inverted != 0;
        input.io.first_delay = 18;  // 300 e 150 ms num relógio de 60
        input.io.repeat_delay = 9;
    }
    // A direção automática troca a configuração de controle (SMissionManager::SetAutoDrive: a do POV, em que as ações vão
    // à mira da câmera); o IOModule::ReleaseAllButtons (os soltos dos botões presos) não foi portado.
    drv::mission_env().auto_drive_input = [&](int32_t type) {
        drv::InputConfigManager_SetConfigType(*icm, type, -1, input_uber, input.map);
    };
    input.camera = [&](int32_t action, float value) {
        refresh_cam_ctx();
        if (icm->d.type == 1) cam->CameraInputCallback(cam_ctx, action, value, 1);
    };
    // A vibração do carro do jogador (IFeedback, porta 0) no controle do PC, como o jogo a pé faz com o seu
    // (platform/psi_pad.cpp: o motor grande no de baixa frequência, o pequeno ligado em 0xffff), só quando muda; nas
    // capturas, só contada. Os choques do carro mandam o tranco (o ~ECollision).
    uint64_t rumble_changes = 0;
    {
        static uint8_t sent[4][2] = {};
        std::memset(sent, 0, sizeof sent);
        const bool to_pad = a.screenshot.empty();
        drv::feedback_state().actuator = [&rumble_changes, to_pad](int32_t port, int32_t motor, uint8_t value) {
            if (port < 0 || port > 3 || sent[port][motor & 1] == value) return;
            sent[port][motor & 1] = value;
            ++rumble_changes;
            if (to_pad && port == 0)
                nf::system_pad_source().vibrate(0, uint16_t(sent[0][1] * 257u), sent[0][0] != 0 ? 0xffff : 0);
        };
        drv::collide_events().collision = [car](const drv::CollisionInfo& ci) {
            drv::ECollision_Feedback(ci, [car](int32_t slot) -> const drv::IFeedback* {
                return slot == car->d.slot && car->feedback.port >= 0 ? &car->feedback : nullptr;
            });
            drv::ECollision_Audio(ci, car->d.slot);  // o som do choque (driving/sound/audio.h)
        };
    }
    // O som (driving/sound/audio.h, docs/driving.md seção 17): o ASoundManager::Init com os arquivos do .VIV da missão,
    // o objeto de som do carro do jogador (PBondCar::InitAudioObject), o fim do PBondCar::Simulate (bond_car_audio_hook)
    // e o ECollision (acima); a saída é a SPU2 do som do carro (snd_render) pela platform/audio_out.h: na janela o
    // aparelho (sem --no-audio), nas capturas só com --audio-dump (senão nada é misturado). O relógio do áudio são os
    // VBlanks do relógio do jogo (800 amostras por tick), o TIMER de 100 Hz do EA anda com ele.
    const bool audio_wanted = !a.no_audio && (a.screenshot.empty() || !a.audio_dump.empty());
    bool audio_on = false;
    drv::WWorldEnviro audio_env;  // o 'Envi' da pista (o EnviroDesc sob o carro)
    if (audio_wanted) {
        auto loader = [&arc](const std::string& path) {
            std::string key = path;
            for (char& ch : key)
                if (ch == '/') ch = '\\';
            std::vector<uint8_t> out;
            for (int i = 0; i < int(arc.entries().size()); ++i)
                if (nf::str_icmp(arc.entries()[size_t(i)].name.c_str(), key.c_str()) == 0) {
                    arc.read(i, out);
                    break;
                }
            return out;
        };
        drv::AudioInit ai;
        ai.track = a.track;
        // Os fluxos (driving/sound/audio_stream.h, docs/driving.md seção 27): o .SPE da missão em inglês ("MIS01.VIV"
        // -> "MIS01EN"; a música é o .MUS sem o idioma) e os BIG do disco.
        {
            std::string base = track.viv();
            if (base.size() > 4) base.resize(base.size() - 4);
            ai.stream_file = base + "EN";
            const std::shared_ptr<nf::DiscSource> sdisc = disc;
            ai.big_open = [sdisc](const std::string& path) -> std::unique_ptr<drv::Bigf> {
                auto b = std::make_unique<drv::Bigf>();
                if (!b->open(*sdisc, path)) return nullptr;
                return b;
            };
        }
        if (drv::UGroup* map = track.map_group()) {
            drv::UData* in = drv::UGroup_DataLocateFirst(map, 0x696e2020u, 0, -1);  // 'in  '
            if (in != drv::UGroup_DataEnd(map))
                audio_env.instances = reinterpret_cast<const drv::CarpInstanceRec*>(drv::UData_Data(in));
            drv::UData* ev = drv::UGroup_DataLocateTag(map, 0x456e7669u);  // 'Envi'
            if (ev != drv::UGroup_DataEnd(map)) {
                audio_env.count = int32_t(ev->count);
                audio_env.table = drv::UData_Data(ev);
            }
        }
        if (drv::audio_init(ai, loader)) {
            drv::AudioCarAttributes ca;
            attrs.find("SFX_ENGINE", ca.engine);
            ca.volume = attrs.lookup_float("SFX_VOLUME");
            ca.skid_multiple = attrs.lookup_float("SFX_SKIDMULTIPLE");
            drv::audio_create_player_car(ca);
            drv::bond_car_audio_hook() = [](drv::BondCar& c, float rpm) { drv::PBondCar_AudioFeed(c, rpm); };
            nf::audio_out_set_source(drv::snd_render, &drv::snd_spu());
            if (!a.audio_dump.empty()) audio_on = nf::audio_out_open_dump(widen(a.audio_dump));
            else audio_on = nf::audio_out_open_device();
            if (!audio_on) std::fprintf(stderr, "nf_driving: sem saída de som\n");
        }
    }
    auto audio_end = [&]() {
        nf::audio_out_close();
        nf::audio_out_set_source(nullptr, nullptr);
        drv::bond_car_audio_hook() = nullptr;
        drv::audio_shutdown();
    };
    uint64_t steps = 0;
    std::string smack_env;
    const bool smack_log = nf::get_env_narrow("NF_DRV_SMACK_LOG", &smack_env);  // depuração: os objetos derrubados a cada passo
    // O laço do jogo (RunTheGame, 0x00175d78): o escalonador com as tarefas do jogo. Cada tarefa cria o seu evento e os
    // eventos rodam em ordem depois de cada tick (EventManager::RunEvents): ESimFrameUpdate (prioridade 0), EAnimUpdate,
    // EAIUpdate e ECameraUpdate (2), ESimEndFrame (7); por volta ERenderFrame (6) e EAudioUpdate (7). Os que o port tem:
    // a simulação (com os gatilhos), o tráfego (o EAIUpdate), a câmera e o pedido de desenho; os outros só contados. Os
    // eventos que os gatilhos criam rodam no fim da passagem (driving/game/triggers.h).
    drv::Scheduler sched(0);
    drv::RunTheGame_AddTasks(sched, false);
    std::vector<int32_t> events;
    uint64_t event_counts[0x14] = {};
    bool render_requested = false;
    sched.call = [&](int32_t fn, uint32_t) { events.push_back(fn); };
    sched.run_events = [&] {
        for (size_t i = 0; i < events.size(); ++i) {
            const int32_t fn = events[i];
            if (fn >= 0 && fn < 0x14) ++event_counts[fn];
            if (fn == drv::kEvSimFrameUpdate) {
                drv::ESimFrameUpdate(drv::simulation());
                if (smack_log) {
                    for (const drv::Smackable* sm : drv::smackables()) {
                        const drv::RigidBody* sb = sm->body();
                        std::printf("smack passo %d pos %.3f %.3f %.3f vel %.3f %.3f %.3f estado %d carro %.3f %.3f %.3f "
                                    "vel %.3f %.3f %.3f\n",
                                    int(drv::simulation().steps), double(sb->pos[0]), double(sb->pos[1]),
                                    double(sb->pos[2]), double(sb->vel[0]), double(sb->vel[1]), double(sb->vel[2]),
                                    int(sb->state), double(rb->pos[0]), double(rb->pos[1]), double(rb->pos[2]),
                                    double(rb->vel[0]), double(rb->vel[1]), double(rb->vel[2]));
                    }
                }
                // As instâncias que os objetos derrubados moveram: a matriz em cache do desenho.
                for (uint32_t moved : drv::smackable_world().moved) track.refresh_instance(track.instance_index(moved));
                drv::smackable_world().moved.clear();
                drv::IFeedback_Update();  // a linha de execução do IFeedback (o temporizador: aqui uma vez por passo)
                drv::audio_sim_step();    // ASoundManager::fgCount += 1
                ++steps;
            } else if (fn == drv::kEvAnimUpdate) {
                // [driving] 0x00147d60 EAnimUpdate::~EAnimUpdate: o RAnimEngine e o RPathEngine::Update com o passo da
                // simulação (os trilhos, driving/render/path_engine.h), fora da simulação parada.
                if (drv::simulation().state != 3) {
                    drv::RAnimEngine_Update(uint32_t(drv::simulation().steps));
                    drv::RPathEngine_Update(ps2::ee::cvt_s(drv::simulation().steps));
                }
            } else if (fn == drv::kEvAIUpdate) {
                drv::AIVehicleController_UpdateVehicles();  // o EAIUpdate: o tráfego (os pedestres e as zonas não)
            } else if (fn == drv::kEvCameraUpdate) {
                chase(0.12f);
            } else if (fn == drv::kEvRenderFrame) {
                render_requested = true;
                // O RRenderWorldCamera::DrawStaticWorldGeometry -> WTargetPicker::UpdateTargets do quadro: os alvos da
                // IA na tela, a escolha e o alvo do mundo (driving/game/target_picker.h), que os mísseis seguem.
                drv::WTargetPicker_UpdateTargets();
            } else if (fn == drv::kEvAudioUpdate) {
                // EAudioUpdate: o ouvinte na câmera do jogador (a matriz e a velocidade, +0x40), o carro.
                drv::AudioListenerInput l;
                std::memcpy(l.matrix, cam->d.matrix, sizeof l.matrix);
                std::memcpy(l.vel, cam->d.velocity, sizeof l.vel);
                std::memcpy(l.car_pos, rb->pos, sizeof l.car_pos);
                // O WWorldPos do carro do jogador (o PhysicsObject +0): a face sob ele, procurada de novo se não é deste
                // passo, e o WWorld::GetEnviroDesc dela.
                drv::WWorldPos& wp = car->d.world_pos;
                if (wp.last_query != int32_t(drv::simulation().steps) && drv::world() != nullptr)
                    drv::world()->find_closest_face(wp, rb->pos, true);
                if (wp.valid != 0 && wp.instance != 0) l.enviro = drv::WWorld_GetEnviroDesc(audio_env, wp);
                drv::audio_update(l);
            }
        }
        // Os eventos que os gatilhos criaram durante estes (o anel do EventManager: no fim, na ordem).
        drv::trigger_run_events();
        events.clear();
    };
    drv::mission_env().scheduler = &sched;
    int32_t clock = 0;  // o Clock do jogo (RealClock: um por quadro de vídeo, 60 Hz)
    // Um quadro do roteiro: a entrada (os SYNCTASKs, antes do Run) e um tick do relógio.
    // Os gatilhos pedidos para depois da largada (--fire-trigger N@s).
    auto fire_late = [&](int32_t before, int32_t now) {
        for (const auto& ft : a.fire_triggers) {
            const int32_t at = int32_t(ft.second * 60.0f + 0.5f);
            if (ft.second > 0.0f && at > before && at <= now && ft.first >= 0 && ft.first < drv::trigger_manager().count)
                drv::WTrigger_FireEvents(*drv::trigger_at(ft.first), 0, rb->slot, 0);
        }
    };
    // O tempo das mensagens do HUD (os Draw* do PS2 descontam o tempo do quadro).
    auto hud_tick = [&](float seconds) {
        std::memcpy(drv::mission_env().camera_pos, eye, sizeof eye);
        if (drv::hud_exists()) drv::GHud_Tick(drv::hud(), seconds);
    };
    Autopilot autopilot;
    autopilot.element = a.autopilot;
    autopilot.points = a.autopilot_points;
    autopilot.speeds = a.autopilot_speeds;
    autopilot.fire = a.autopilot_fire;
    Gunner gunner;
    gunner.elements = a.gunner;
    gunner.height = a.gunner_height;
    gunner.tyres = a.gunner_tyres;
    gunner.limit_deg = a.gunner_limit;
    {
        std::string v;
        if (nf::get_env_narrow("NF_DRV_GUN_LIMIT", &v)) gunner.limit_deg = std::stof(v);  // depuração: o erro do tiro
    }
    uint64_t emp_hits = 0;
    std::string ai_log_env;
    const bool ai_log = nf::get_env_narrow("NF_DRV_AI_LOG", &ai_log_env);  // depuração: o estado da IA a cada segundo
    auto step = [&](const DriveKeys& k) {
        refresh_cam_ctx();
        drv::PadData pad = pad_from(k, nullptr);
        // Com o atirador, o piloto só depois da troca do helicóptero pelo carro (ECustomTransition) e o atirador só antes.
        const bool car_section = drv::heli_section_stats().changes > 0;
        if (a.autopilot >= 0 && (gunner.elements.empty() || car_section)) autopilot.drive(pad, *car);
        if (!gunner.elements.empty() && !car_section) gunner.drive(pad);
        input.feed(*car, pad, clock);
        if (ai_log && clock % 600 == 0)
            for (int i = 0; drv::ai_controller() && i < drv::ai_controller()->count; ++i)
                if (const drv::AIElement* e = drv::ai_element(i))
                    std::printf("ia_elemento t %.0f %d tipo %d estado %d vida %.0f bandeiras %04x heli %d passageiros %d %.20s\n",
                                clock / 60.0, i, int(e->type), int(e->state), double(e->health), unsigned(e->flags),
                                int(e->heli), int(e->passengers), e->type == 1 ? e->name : "(personagem)");
        if (ai_log && clock % 60 == 0) {
            const drv::RigidBody* pb = car->body();
            std::printf("ia_log t %.1f jogador %.1f,%.1f %.1fm/s vida %.0f etapa %d estado %d piloto %.0f,%.0f",
                        clock / 60.0, double(pb->pos[0]), double(pb->pos[2]), double(car->d.car_speed),
                        double(car->hit_points()), drv::mission_exists() ? int(drv::mission_manager().stage) : -1,
                        drv::mission_exists() ? int(drv::mission_manager().state) : -1,
                        double(autopilot.last_target[0]), double(autopilot.last_target[1]));
            for (int i = 0; drv::ai_controller() && i < drv::ai_controller()->count; ++i) {
                const drv::AIElement* e = drv::ai_element(i);
                const drv::AIElementTracking* t = drv::ai_tracking(i);
                if (!e || !t || e->state != 1) continue;
                const float dx = t->pos[0] - pb->pos[0], dz = t->pos[2] - pb->pos[2];
                if (ai_log_env != "all" && dx * dx + dz * dz > 400.0f * 400.0f) continue;
                const drv::AIGroundVehicle* v = nf::p32_ptr<const drv::AIGroundVehicle>(e->ai);
                const drv::BondCar* vc = v ? drv::car_of(*v) : nullptr;
                std::printf(" | %d %.1f,%.1f %.1fm/s n%d a%d pede %.1f g%.2f b%.2f d%.2f para %.0f,%.0f vida %.0f", i,
                            double(t->pos[0]), double(t->pos[2]), double(t->speed), v ? int(v->navigate_mode) : -1,
                            v ? int(v->attack_mode) : -1, v ? double(v->speed) : 0.0,
                            vc ? double(vc->d.control.gas) : 0.0, vc ? double(vc->d.control.brake) : 0.0,
                            vc ? double(vc->d.control.steering) : 0.0, v ? double(v->drive_to[0]) : 0.0,
                            v ? double(v->drive_to[2]) : 0.0, double(e->health));
            }
            std::printf("\n");
        }
        if (a.car_reset_at >= 0.0f && clock == int32_t(a.car_reset_at * 60.0f + 0.5f)) {
            // Depuração: o carro do jogador recolocado (PBondCar::ResetCar, como o do EResetCar).
            float pos[3] = {a.car_reset[0], a.car_reset[1], a.car_reset[2]};
            const float dir[3] = {nf::fm::sin(a.car_reset[3]), 0.0f, nf::fm::cos(a.car_reset[3])};
            drv::PBondCar_ResetCar(*car, pos, dir);
        }
        for (const auto& ke : a.kill_elements) {
            if (clock != int32_t(ke.second * 60.0f + 0.5f) || !drv::ai_controller()) continue;
            // Depuração: o EKillAIElement (o evento 161) do elemento, como o de uma lista de eventos.
            drv::PendingEvent ev{161, 0, 0.0f, 0, 0, 0};
            if (const drv::EventInfo* info = drv::event_info(161)) {
                ev.size = info->size;
                ev.obj[0] = info->vtable;
            }
            ev.obj[1] = drv::ai_controller()->carp + uint32_t(ke.first) * 0x100u;
            drv::ai_event_execute(ev);
        }
        for (float at : a.emp_hits) {
            if (clock != int32_t(at * 60.0f + 0.5f)) continue;
            // Depuração: o EMP do jogador no carro mais perto à frente, a até 120 m (PBondCar::AttackWithEmp; o
            // Q-Pulse é um projétil que segue o alvo, o alcance de verdade é o das armas).
            drv::BondCar* best = nullptr;
            float best_d = 120.0f;
            const drv::RigidBody* pb = car->body();
            for (drv::BondCar* c : drv::simulation().cars) {
                if (c == car) continue;
                const drv::RigidBody* cb = c->body();
                const float dx = cb->pos[0] - pb->pos[0], dz = cb->pos[2] - pb->pos[2];
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d < best_d && pb->info->orient[2][0] * dx + pb->info->orient[2][2] * dz > 0.0f) {
                    best = c;
                    best_d = d;
                }
            }
            if (best) {
                drv::PBondCar_AttackWithEmp(*best);
                ++emp_hits;
            }
        }
        for (const auto& dh : a.damage_hits) {
            if (clock != int32_t(dh.first * 60.0f + 0.5f)) continue;
            const drv::RigidBody* pb = car->body();
            const float from[3] = {pb->pos[0], pb->pos[1] + 5.0f, pb->pos[2]};
            const float to[3] = {pb->pos[0], pb->pos[1], pb->pos[2]};
            drv::PBondCar_ApplyDamage(*car, from, to, dh.second, 0.5f, 2);
        }
        fire_late(clock, clock + 1);
        ++clock;
        hud_tick(1.0f / 60.0f);
        drv::audio_set_timer(clock * 100 / 60);
        sched.Run(clock);
        {
            // Os efeitos andam a cada passo (drv_fx.h): a câmera do passo.
            float fv[16];
            camera_view(fv);
            fx_app.update(drv::simulation().steps, cam->d.matrix, fv);
        }
        if (audio_on) nf::audio_out_catch_up(uint32_t(clock));
    };
    // O roteiro: "teclas:segundos" separados por espaço (60 passos por segundo).
    std::vector<std::pair<DriveKeys, int>> script;
    {
        size_t p = 0;
        const std::string& s = a.drive_input;
        while (p < s.size()) {
            while (p < s.size() && s[p] == ' ') ++p;
            size_t e = s.find(' ', p);
            if (e == std::string::npos) e = s.size();
            const std::string t = s.substr(p, e - p);
            p = e;
            const size_t c = t.find(':');
            if (c == std::string::npos) continue;
            script.push_back({parse_keys(t.substr(0, c)), int(std::strtof(t.c_str() + c + 1, nullptr) * 60.0f + 0.5f)});
        }
    }
    std::string rep;
    auto report_line = [&](double ms_per_frame) {
        char b[1024];
        const drv::PBondCarData& c = car->d;
        std::snprintf(b, sizeof b,
                      "carro %s largada %.2f,%.2f,%.2f chao %d | passos %llu pos %.2f,%.2f,%.2f velocidade %.2f m/s "
                      "marcha %d rodas_no_chao %d direcao %.3f acelerador %.2f freio %.2f compressao %.3f/%.3f/%.3f/%.3f | "
                      "caixa %.3f,%.3f,%.3f deslocamento %.3f pontos %zu alavancas %d massa %.0f | carga_ms %.0f "
                      "ms_por_quadro %.3f\n",
                      cur_name.c_str(), start[0], start[1], start[2], ground ? 1 : 0, (unsigned long long)steps,
                      rb->pos[0], rb->pos[1], rb->pos[2], double(c.car_speed), int(c.gear), int(c.num_wheels_on_ground),
                      double(c.control.steering), double(c.control.gas), double(c.control.brake), double(c.suspension[0]),
                      double(c.suspension[1]), double(c.suspension[2]), double(c.suspension[3]),
                      double((desc.bbox_max[0] - desc.bbox_min[0]) * 0.5f), double((desc.bbox_max[1] - desc.bbox_min[1]) * 0.5f),
                      double((desc.bbox_max[2] - desc.bbox_min[2]) * 0.5f), double(render_offset), desc.collision.size(),
                      int(rb->info->lever_count), double(rb->mass), load_ms, ms_per_frame);
        rep = b;
        const drv::BondCarStats& st = drv::bond_car_stats();
        const drv::CollideEvents& ce = drv::collide_events();
        std::snprintf(b, sizeof b,
                      "eventos aterrissagens %llu poeira %llu recolocar %llu guardas %llu | choques %llu encostado %d "
                      "dano %llu (%.1f) estimulos %llu | pendentes %s\n",
                      (unsigned long long)st.landing_impacts, (unsigned long long)st.landing_fx,
                      (unsigned long long)st.resets, (unsigned long long)st.guards, (unsigned long long)ce.collisions,
                      int(car->d.against_wall), (unsigned long long)car->damage_calls, double(car->damage_total),
                      (unsigned long long)ce.stimuli, drv::physics_pending_report());
        rep += b;
        std::snprintf(b, sizeof b,
                      "agenda relogio %d ticks %llu jogados_fora %llu | eventos simulacao %llu animacao %llu ia %llu "
                      "camera %llu fim %llu desenho %llu audio %llu | entrada acoes %llu guardas %llu\n",
                      int(clock), (unsigned long long)sched.ticks, (unsigned long long)sched.discarded,
                      (unsigned long long)event_counts[drv::kEvSimFrameUpdate],
                      (unsigned long long)event_counts[drv::kEvAnimUpdate],
                      (unsigned long long)event_counts[drv::kEvAIUpdate],
                      (unsigned long long)event_counts[drv::kEvCameraUpdate],
                      (unsigned long long)event_counts[drv::kEvSimEndFrame],
                      (unsigned long long)event_counts[drv::kEvRenderFrame],
                      (unsigned long long)event_counts[drv::kEvAudioUpdate], (unsigned long long)input.actions,
                      (unsigned long long)drv::input_guards());
        rep += b;
        {
            // O tráfego: os carros, os tipos, os que estão na rua, os gatilhos e o motorista.
            int on_street = 0;
            for (const TrafficCar& tc : traffic) on_street += tc.car->body()->state != 3;
            std::string types;
            for (const auto& tt : traffic_types)
                types += (types.empty() ? "" : ",") + tt.first + "(" + std::to_string(tt.second->list.size()) + "/" +
                         std::to_string(tt.second->ok ? tt.second->model.instances().size() : 0) + ")";
            const drv::TriggerStats& ts = drv::trigger_stats();
            uint64_t created = 0;
            for (const auto& ev : ts.events) created += ev.second;
            const drv::GroundVehicleStats& gs = drv::ground_vehicle_stats();
            const drv::VehicleControllerGlobals& vc = drv::vehicle_controller();
            std::snprintf(b, sizeof b,
                          "trafego carros %zu tipos %s (%d de MAX_TRAFFIC %d) na_rua %d desenhados %d ativos %d/%d | gatilhos "
                          "%d disparos %llu eventos %llu postos_na_rua %llu raios %llu/%llu | motorista atualizacoes %llu nascimentos %llu "
                          "sumicos %llu sinais_vermelhos %llu atras %llu guardas %llu | faltando %llu cores %llu\n",
                          traffic.size(), types.empty() ? "-" : types.c_str(), traffic_num_types, traffic_max, on_street,
                          traffic_drawn, int(vc.num_active_traffic), int(vc.max_active_traffic),
                          int(drv::trigger_manager().count), (unsigned long long)ts.fired, (unsigned long long)created,
                          (unsigned long long)ts.traffic_spawned, (unsigned long long)ts.rays, (unsigned long long)ts.ray_hits,
                          (unsigned long long)gs.updates,
                          (unsigned long long)gs.spawns, (unsigned long long)gs.unspawns,
                          (unsigned long long)gs.red_lights, (unsigned long long)gs.blocked,
                          (unsigned long long)(gs.guards + ts.guards), (unsigned long long)traffic_missing,
                          (unsigned long long)traffic_colours);
            rep += b;
            // Os carros na rua: a posição, a velocidade e os modos do motorista (até 8).
            std::string cars;
            int listed = 0;
            for (const TrafficCar& tc : traffic) {
                const drv::RigidBody* tb = tc.car->body();
                if (tb->state == 3 || listed >= 8) continue;
                const drv::AIGroundVehicle* v = drv::ai_of(*tc.car);
                char cb[160];
                std::snprintf(cb, sizeof cb, " %s@%.1f,%.1f,%.1f/%.1fm/s/%d%d", tc.car->name_.c_str(), double(tb->pos[0]),
                              double(tb->pos[1]), double(tb->pos[2]), double(tc.car->d.car_speed),
                              v ? int(v->navigate_mode) : -1, v ? int(v->attack_mode) : -1);
                cars += cb;
                ++listed;
            }
            if (!cars.empty()) rep += "trafego_na_rua" + cars + "\n";
            const drv::SmackableStats& ss = drv::smackable_stats();
            std::string moving;
            for (const drv::Smackable* sm : drv::smackables()) {
                const drv::RigidBody* sb = sm->body();
                char mb[96];
                std::snprintf(mb, sizeof mb, " %.1f,%.1f,%.1f/%d(m%.0f c%.2f,%.2f,%.2f v%.1f)", double(sb->pos[0]),
                              double(sb->pos[1]), double(sb->pos[2]), int(sb->state), double(sb->mass),
                              double(sb->info->half_size[0]), double(sb->info->half_size[1]),
                              double(sb->info->half_size[2]), double(sm->d.hit_points));
                if (moving.size() < 400) moving += mb;
            }
            std::snprintf(b, sizeof b,
                          "objetos_derrubaveis acordados %llu vivos %zu dormiram %llu escondidos %llu apagados %llu sem_vaga "
                          "%llu danos %llu estimulos %llu newton %llu grade %llu guardas %llu |%s\n",
                          (unsigned long long)ss.woken, drv::smackables().size(), (unsigned long long)ss.asleep,
                          (unsigned long long)ss.hidden, (unsigned long long)ss.deleted, (unsigned long long)ss.no_room,
                          (unsigned long long)ss.damage, (unsigned long long)ss.stimuli, (unsigned long long)ss.newton,
                          (unsigned long long)ss.dynamic_grid, (unsigned long long)ss.guards, moving.c_str());
            rep += b;
        }
        if (const drv::AIElementControllerData* ctl = drv::ai_controller()) {
            // Os elementos da IA (driving/ai/element_controller.h): os acordados com o estado, a vida, a posição, a
            // velocidade e os modos do motorista; as armas pedidas (driving/ai/ai_weapons.h).
            const drv::AIElementStats& es = drv::ai_element_stats();
            const drv::AIWeaponStats& ws = drv::ai_weapon_stats();
            std::string awake;
            for (int i = 0; i < ctl->count; ++i) {
                const drv::AIElement* e = drv::ai_element(i);
                const drv::AIElementTracking* t = drv::ai_tracking(i);
                if (!e || !t || e->state != 1 || awake.size() > 1500) continue;
                const drv::AIGroundVehicle* v = nf::p32_ptr<const drv::AIGroundVehicle>(e->ai);
                char eb[256];
                const drv::WRoadNav* vn = v ? nf::p32_ptr<const drv::WRoadNav>(v->nav) : nullptr;
                const drv::BondCar* vc = v ? drv::car_of(*v) : nullptr;
                std::snprintf(eb, sizeof eb,
                              " %d:%s@%.1f,%.1f,%.1f/vida%.0f/%.1fm/s/%d%d/pede%.1f/trecho%d/dist%.0f/g%.2f,b%.2f,d%.2f/para%.0f,%.0f",
                              i, e->name, double(t->pos[0]), double(t->pos[1]), double(t->pos[2]), double(e->health),
                              double(t->speed), v ? int(v->navigate_mode) : -1, v ? int(v->attack_mode) : -1,
                              v ? double(v->speed) : 0.0, vn ? int(vn->segment) : -2, v ? double(v->unknown1d0) : 0.0,
                              vc ? double(vc->d.control.gas) : 0.0, vc ? double(vc->d.control.brake) : 0.0,
                              vc ? double(vc->d.control.steering) : 0.0, v ? double(v->drive_to[0]) : 0.0,
                              v ? double(v->drive_to[2]) : 0.0);
                awake += eb;
            }
            std::snprintf(b, sizeof b,
                          "ia elementos %d acordados %d/%d/%d carros %zu acordou %llu dormiu %llu comandos %llu sem_carro "
                          "%llu personagens %llu helicopteros %llu guardas %llu | armas balas %llu foguetes %llu misseis %llu "
                          "torre %llu |",
                          int(ctl->count), int(ctl->characters_awake), int(ctl->vehicles_awake), int(ctl->helis_awake),
                          ai_cars.size(), (unsigned long long)es.wakes, (unsigned long long)es.sleeps,
                          (unsigned long long)es.commands, (unsigned long long)(es.no_factory + ai_missing),
                          (unsigned long long)es.characters, (unsigned long long)es.helis, (unsigned long long)es.guards,
                          (unsigned long long)ws.bullets, (unsigned long long)ws.rockets, (unsigned long long)ws.missiles,
                          (unsigned long long)ws.turret);
            rep += b + awake + "\n";
        }
        {
            // O dano dos carros (driving/game/bond_car.h "o dano"): as chamadas do PBondCar::ApplyDamage, a vida do
            // carro do jogador e a maior zona amassada/rasgada dele.
            const drv::CarDamageStats& ds = drv::car_damage_stats();
            float za = 0.0f, zb = 0.0f;
            for (int z = 0; z < 16; ++z) {
                za = std::max(za, car->d.damage_zones[z][0]);
                zb = std::max(zb, car->d.damage_zones[z][1]);
            }
            std::snprintf(b, sizeof b,
                          "dano chamadas %llu acertos %llu fora %llu escudo %llu pneus %llu estouros %llu mortes %llu "
                          "abatidos %llu estimulos %llu efeitos %llu sons %llu hud %llu forcas %llu emp %llu guardas %llu | "
                          "jogador vida %.1f zonas %.2f/%.2f\n",
                          (unsigned long long)ds.calls, (unsigned long long)ds.hits, (unsigned long long)ds.misses,
                          (unsigned long long)ds.shield, (unsigned long long)ds.tyres_hit,
                          (unsigned long long)ds.blowouts, (unsigned long long)ds.deaths, (unsigned long long)ds.kills,
                          (unsigned long long)ds.stimuli, (unsigned long long)ds.render_fx, (unsigned long long)ds.sounds,
                          (unsigned long long)ds.hud, (unsigned long long)ds.forces, (unsigned long long)ds.emp,
                          (unsigned long long)ds.guards, double(car->hit_points()), double(za), double(zb));
            rep += b;
            {
                // A mira (o WTargetPicker, seção 24).
                const drv::WTargetPickerData& tp = drv::target_picker();
                const drv::TargetPickerStats& ts = drv::target_picker_stats();
                const uint32_t sel = drv::WTargetPicker_GetSelectedTarget();
                std::snprintf(b, sizeof b,
                              "mira ativa %d modo %d alvos %d escolhido %d trava %d som %d | registros %llu/%llu "
                              "quadros %llu visiveis %llu na_tela %llu travas %llu autodrive %llu guardas %llu\n",
                              tp.active, tp.mode, tp.count, sel != 0 ? 1 : 0, tp.lock, drv::target_picker_sound(),
                              (unsigned long long)ts.registered, (unsigned long long)ts.unregistered,
                              (unsigned long long)ts.updates, (unsigned long long)ts.visible,
                              (unsigned long long)ts.on_screen, (unsigned long long)ts.locks,
                              (unsigned long long)ts.autodrive, (unsigned long long)ts.guards);
                rep += b;
            }
            if (a.autopilot >= 0 || !a.emp_hits.empty()) {
                std::snprintf(b, sizeof b,
                              "piloto elemento %d passos %llu seguindo %llu pontos %zu/%zu caminho %zu alvo %.1f,%.1f | "
                              "emp %llu especial %llu | plano %d -> %d vistos %d nos %d planos %llu\n",
                              a.autopilot, (unsigned long long)autopilot.steps, (unsigned long long)autopilot.following,
                              autopilot.next_point, autopilot.points.size(), autopilot.route.size() - autopilot.route_next,
                              double(autopilot.last_target[0]), double(autopilot.last_target[1]),
                              (unsigned long long)emp_hits, (unsigned long long)autopilot.shots, autopilot.plan_from, autopilot.plan_to, autopilot.plan_seen,
                              autopilot.plan_nodes, (unsigned long long)autopilot.plans);
                rep += b;
            }
            if (!a.gunner.empty()) {
                std::snprintf(b, sizeof b, "atirador elemento %d passos %llu erro %.2f mirado %llu tiros %llu\n",
                              gunner.current, (unsigned long long)gunner.steps, double(gunner.error_deg),
                              (unsigned long long)gunner.aimed, (unsigned long long)gunner.fired);
                rep += b;
            }
        }
        rep += cull_line;
        rep += '\n';
        {
            // A missão: o estado, a etapa, as regras, os eventos, os cronômetros, o objetivo e o HUD.
            const drv::MissionStats& ms = drv::mission_stats();
            const drv::EventStats& es = drv::event_stats();
            uint64_t ev_created = 0, ev_run = 0, ev_skipped = 0;
            for (const auto& e : es.created) ev_created += e.second;
            for (const auto& e : es.run) ev_run += e.second;
            for (const auto& e : es.skipped) ev_skipped += e.second;
            std::string timers, objective = "-", skipped;
            int32_t state = 0, stage = -1, stages = 0;
            float health = 0;
            if (drv::mission_exists()) {
                drv::SMissionManagerData& m = drv::mission_manager();
                state = m.state;
                stage = m.stage;
                stages = m.num_stages;
                health = m.player_health;
                for (int i = 0; i < 10; ++i) {
                    drv::SMissionTimer& t = m.timers[i];
                    if (t.rate == 0 && t.value == 0) continue;
                    timers += (timers.empty() ? "" : ",") + std::to_string(i) + ":" +
                              std::to_string(drv::SMissionTimer_GetValue(t)) + "/" + std::to_string(t.rate);
                }
                for (const drv::SMissionObjective* o : drv::mission_lists().objectives)
                    if (o->state == 1) objective = std::to_string(o->id);
            }
            for (const auto& e : es.skipped)
                if (skipped.size() < 300) skipped += " " + std::to_string(e.first) + ":" + std::to_string(e.second);
            const drv::GHudData& h = drv::hud();
            std::snprintf(b, sizeof b,
                          "missao %s carro_da_missao %s estado %d etapa %d etapas %d vida %.1f | regras %llu disparos %llu "
                          "ia %llu sem_porte %llu externas %llu recomecos %llu guardas %llu | eventos criados %llu "
                          "executados %llu sem_porte %llu construtores %llu guardas %llu | cronometros %s | objetivo %s | "
                          "hud %s mensagem %d objetivo %d botao %d resultado %d tarjas %d escuro %.2f linhas %llu\n",
                          mission_note.c_str(),
                          drv::mission_exists() ? drv::SMissionManager_GetCarType(drv::mission_manager()) : "-", int(state),
                          int(stage), int(stages), double(health), (unsigned long long)ms.rule_checks,
                          (unsigned long long)ms.rules_fired, (unsigned long long)ms.ai_queries,
                          (unsigned long long)ms.unported_rules, (unsigned long long)ms.external_calls,
                          (unsigned long long)ms.restarts, (unsigned long long)ms.guards, (unsigned long long)ev_created,
                          (unsigned long long)ev_run, (unsigned long long)ev_skipped,
                          (unsigned long long)es.constructor_effects, (unsigned long long)es.guards,
                          timers.empty() ? "-" : timers.c_str(), objective.c_str(),
                          drv::hud_exists() ? (hud_font_ok ? "fonte" : "sem_fonte") : "nao", int(h.message_text),
                          int(h.objective_text), int(h.button_symbol), int(h.result_text), int(h.letterbox_mode),
                          double(h.black_fade), (unsigned long long)hud_lines_drawn);
            rep += b;
            rep += "eventos_sem_porte" + (skipped.empty() ? std::string(" -") : skipped) + "\n";
            std::string ran;
            for (const auto& e : es.run)
                if (ran.size() < 300) ran += " " + std::to_string(e.first) + ":" + std::to_string(e.second);
            rep += "eventos_executados" + (ran.empty() ? std::string(" -") : ran) + "\n";
        }
        if (const drv::SWeaponManagerData* w = drv::weapon_manager()) {
            // As armas (driving/game/weapons.h) e o motor de animação das instâncias (driving/render/anim_engine.h).
            const drv::WeaponsStats& ws = drv::weapons_stats();
            const drv::AnimEngineStats& as = drv::anim_engine_stats();
            const drv::WeaponEntry* t = drv::weapon_entry(const_cast<drv::SWeaponManagerData&>(*w), 0);
            std::string loaded;
            for (int i = 0; i < drv::kWeaponCount; ++i)
                if (t[i].flags & 1) loaded += " " + std::to_string(i) + ":" + std::to_string(t[i].ammo) + "/" +
                                              std::to_string(t[i].clip);
            std::snprintf(b, sizeof b,
                          "armas secundaria %d especial %d ultima %d |%s | updates %llu acoes %llu tiros %llu estimulos "
                          "%llu recargas %llu guardas %llu pendentes %llu | animacao handles %llu estimulos %llu "
                          "comecados %llu acabados %llu tocando %u eventos %llu\n",
                          w->secondary, w->special, w->last_fired, loaded.c_str(), (unsigned long long)ws.updates,
                          (unsigned long long)ws.actions, (unsigned long long)ws.fired, (unsigned long long)ws.stimuli,
                          (unsigned long long)ws.reloads, (unsigned long long)ws.guards, (unsigned long long)ws.pending,
                          (unsigned long long)as.handles, (unsigned long long)as.stimuli, (unsigned long long)as.started,
                          (unsigned long long)as.finished, drv::anim_engine().count, (unsigned long long)as.events);
            rep += b;
            // Os tiros (EFireWeapon), os mísseis e as explosões (driving/game/missile.h, explosion.h).
            const drv::FireStats& fs = drv::fire_stats();
            const drv::MissileStats& ms2 = drv::missile_stats();
            const drv::EffectsStats& xs = drv::effects_stats();
            std::snprintf(b, sizeof b,
                          "tiros eventos %llu misseis %llu sons %llu sons_pov %llu trancos %llu outros %llu ia %llu "
                          "camera %llu helicoptero %llu efeito %llu guardas %llu | misseis vivos %zu desenhados %d criados "
                          "%llu recusados %llu explodiram %llu mundo %llu corpos %llu expirados %llu apagados %llu "
                          "guardas %llu | explosoes vivas %zu criadas %llu recusadas %llu apagadas %llu forcas %llu danos "
                          "%llu efeitos %llu sons %llu graves %llu sem_banco %llu trancos %llu guardas %llu\n",
                          (unsigned long long)fs.events, (unsigned long long)fs.missiles, (unsigned long long)fs.sounds,
                          (unsigned long long)fs.menu_sounds, (unsigned long long)fs.jolts,
                          (unsigned long long)fs.other_projectiles,
                          (unsigned long long)(fs.ai_vehicle + fs.ai_aim), (unsigned long long)fs.camera_aim,
                          (unsigned long long)fs.helicopter, (unsigned long long)fs.locate_fx,
                          (unsigned long long)fs.guards, drv::simulation().missiles.size(), missiles_drawn,
                          (unsigned long long)ms2.spawned, (unsigned long long)ms2.refused,
                          (unsigned long long)ms2.exploded, (unsigned long long)ms2.world_hits,
                          (unsigned long long)ms2.body_hits, (unsigned long long)ms2.expired,
                          (unsigned long long)ms2.deleted, (unsigned long long)ms2.guards,
                          drv::simulation().explosions.size(), (unsigned long long)xs.explosions_spawned,
                          (unsigned long long)xs.spawn_refused, (unsigned long long)xs.explosions_deleted,
                          (unsigned long long)xs.forces, (unsigned long long)xs.damage_calls,
                          (unsigned long long)xs.gfx_triggers, (unsigned long long)xs.sounds,
                          (unsigned long long)xs.limited_sounds, (unsigned long long)xs.sounds_skipped,
                          (unsigned long long)xs.jolts, (unsigned long long)xs.guards);
            rep += b;
            rep += drv::drive_actors::report();  // os atores e as mãos de Bond
            rep += drv::passenger_report();       // os passageiros dos carros da IA (seção 26)
            rep += fx_app.report() + "\n";  // os efeitos desenhados (drv_fx.h)
            std::string where;
            for (drv::PhysicsObject* po : drv::simulation().missiles) {
                const drv::Missile* msl = dynamic_cast<const drv::Missile*>(po);
                const drv::SimpleRigidBody* sb = msl ? msl->body() : nullptr;
                if (!sb || where.size() > 300) continue;
                char mb[160];
                std::snprintf(mb, sizeof mb, " %.2f,%.2f,%.2f(%.1f m/s arma %d inst %d %s)", double(sb->pos[0]),
                              double(sb->pos[1]), double(sb->pos[2]), double(drv::SimpleRigidBody_GetScalarVelocity(sb)),
                              int(msl->d.weapon), int(msl->source_instance), msl->d.decaying ? "morrendo" : "voando");
                where += mb;
            }
            rep += "misseis" + (where.empty() ? std::string(" -") : where) + "\n";
            // As balas (driving/game/shells.h): os RayShell, os Shell, as janelas.
            const drv::ShellsStats& bs = drv::shells_stats();
            const drv::RayShellGlobals& bg = drv::rayshell_globals();
            std::snprintf(b, sizeof b,
                          "balas raios %llu corpos %llu mundo %llu danos %llu rastros %d claroes %d gatilhos %d | shell "
                          "vivos %zu criados %llu recusados %llu acertos %llu apagados %llu | janelas %llu eventos %llu "
                          "efeitos %llu sons %llu explosoes %llu guardas %llu\n",
                          (unsigned long long)bs.rayshells, (unsigned long long)bs.fired,
                          (unsigned long long)bs.world_hits, (unsigned long long)bs.damage_calls, bg.num_tracers,
                          bg.num_flashes, bg.num_active, drv::simulation().shells.size(),
                          (unsigned long long)bs.shells_spawned, (unsigned long long)bs.shells_refused,
                          (unsigned long long)bs.shell_hits, (unsigned long long)bs.shells_deleted,
                          (unsigned long long)bs.windows_hit, (unsigned long long)bs.window_events,
                          (unsigned long long)bs.world_effects, (unsigned long long)bs.sounds,
                          (unsigned long long)bs.cheat_explosions, (unsigned long long)bs.guards);
            rep += b;
        }
        {
            // O som: os bancos, os sons vivos, a lista do PlayVoices, o motor, o reverb e a saída.
            const drv::AudioStats as = drv::audio_stats();
            const nf::AudioOutStats os = nf::audio_out_stats();
            std::snprintf(b, sizeof b,
                          "som %s bancos %d falhas %d sons %d vozes_ativas %d tocou %llu paradas %llu avulsos %llu "
                          "choques %llu aterrissagens %llu rpm %.0f pitch %.3f carga %.2f reverb %d %.2f guardas %llu | "
                          "saida pico %.1f rms %.1f frames %llu ticks_driver %llu\n",
                          audio_on ? (a.audio_dump.empty() ? "aparelho" : "wav") : "mudo", as.banks, as.banks_failed,
                          as.sounds, as.voices_active, (unsigned long long)as.plays, (unsigned long long)as.stops,
                          (unsigned long long)as.oneshots, (unsigned long long)as.collisions,
                          (unsigned long long)as.landings, double(as.rpm), double(as.engine_pitch),
                          double(as.engine_load), as.afx_mode, double(as.afx_level), (unsigned long long)as.guards,
                          double(os.peak_db), double(os.rms_db), (unsigned long long)os.frames,
                          (unsigned long long)drv::snd_ticks());
            rep += b;
            // Os fluxos (docs/driving.md seção 27): a música e as falas, o AFader e o mixador do EE.
            const drv::AudioStreamStats ss = drv::audio_stream_stats();
            std::snprintf(b, sizeof b,
                          "fluxos musica %d [%s] fila %d vol %.3f | falas %d [%s] fila %d vol %.3f | ambiente [%s] "
                          "nivel %.3f | eventos %llu pedidos %llu falhas %llu vozes_ee %llu guardas %llu\n",
                          ss.music_req, ss.music_file.c_str(), ss.music_queue, double(ss.music_volume), ss.speech_req,
                          ss.speech_file.c_str(), ss.speech_queue, double(ss.speech_volume), ss.ambience.c_str(),
                          double(ss.fader_level), (unsigned long long)ss.events, (unsigned long long)ss.queued,
                          (unsigned long long)ss.failed, (unsigned long long)ss.mix_voices,
                          (unsigned long long)ss.guards);
            rep += b;
        }
        // O render do carro: as rodas (o giro dos eixos e a direção desenhada), as lâmpadas acesas, a sombra, a
        // vibração e as configurações do controle.
        const drv::RVehicleData& vd = rig->vehicle.d;
        std::string lamps;
        for (int i = 0; i < 16; ++i)
            if (vd.switch_w[i] != 0.0f) lamps += (lamps.empty() ? "" : ",") + std::to_string(i);
        const drv::VehiclePending& vp = drv::vehicle_pending();
        const drv::IFeedbackPortData& fp = drv::feedback_state().ports[0];
        std::snprintf(b, sizeof b,
                      "rodas giro %.3f/%.3f direcao %.4f ossos %u rodas %d,%d,%d,%d cubos %d,%d,%d,%d altura %.3f,%.3f,%.3f,%.3f "
                      "| lampadas %s | sombra %s nivel %d motivo %d | vibracao refs %d tranco %d/%d mudancas %llu | controle "
                      "configs %d/%d frontend %u guardas %llu | pendentes mapa %llu efeitos %llu render_fx %llu procanim %llu "
                      "guardas %llu\n",
                      double(car->d.wheel_spin_angle[0]), double(car->d.wheel_spin_angle[1]), double(vd.steer),
                      vd.bone_count, vd.wheel_bone[0], vd.wheel_bone[1], vd.wheel_bone[2], vd.wheel_bone[3], vd.hub_bone[0],
                      vd.hub_bone[1], vd.hub_bone[2], vd.hub_bone[3],
                      rig->vehicle.bones.empty() || vd.hub_bone[0] <= 0 ? 0.0
                                                                        : double(rig->vehicle.bones[size_t(vd.hub_bone[0] - 1)].m[3][1]),
                      rig->vehicle.bones.empty() || vd.hub_bone[1] <= 0 ? 0.0
                                                                        : double(rig->vehicle.bones[size_t(vd.hub_bone[1] - 1)].m[3][1]),
                      rig->vehicle.bones.empty() || vd.hub_bone[2] <= 0 ? 0.0
                                                                        : double(rig->vehicle.bones[size_t(vd.hub_bone[2] - 1)].m[3][1]),
                      rig->vehicle.bones.empty() || vd.hub_bone[3] <= 0 ? 0.0
                                                                        : double(rig->vehicle.bones[size_t(vd.hub_bone[3] - 1)].m[3][1]),
                      lamps.empty() ? "-" : lamps.c_str(), rig->last_shadow.drawn ? "desenhada" : "nao",
                      rig->last_shadow.level, rig->last_shadow.reason, fp.refs, fp.jolt_level, fp.jolt_count,
                      (unsigned long long)rumble_changes, icm->d.count[0], icm->d.count[1], icm->d.frontend,
                      (unsigned long long)icm->guards, (unsigned long long)vp.map_updates,
                      (unsigned long long)vp.effects, (unsigned long long)car->render_fx,
                      (unsigned long long)vp.procanim_other, (unsigned long long)vp.guards);
        rep += b;
    };

    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    if (!a.screenshot.empty()) {
        // A captura: o roteiro (ou --seconds parado), depois um quadro fora da tela.
        if (!r.init(nullptr, ro, err) || !r.upload(track.models(), err) || !r.upload(model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        for (auto& tt : traffic_types)
            if (tt.second->ok && !r.upload(tt.second->model, err)) {
                std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
                return 1;
            }
        if ((secondary && !r.upload(secondary->model, err)) || !drv::drive_actors::upload(r, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        if (hud_font_ok) r.set_ui_texture(hud_font.image_width, hud_font.image_height, hud_atlas.data());
        fx_app.textures(have_misc_arc ? &misc_arc : nullptr, &arc, r);
        chase(1.0f);
        int total = 0;
        for (const auto& s : script) total += s.second;
        const int want = a.seconds > 0 ? int(a.seconds * 60.0f + 0.5f) : total;
        int done = 0;
        for (const auto& s : script)
            for (int i = 0; i < s.second && done < want; ++i, ++done) step(s.first);
        for (; done < want; ++done) step(DriveKeys{});
        double ms = 0;
        if (a.bench_frames > 0) {
            // O custo de um quadro inteiro: um passo da física, a visibilidade, a cena e o desenho (fora da tela).
            compose(a.width, a.height);
            r.draw_scene(scene);
            r.finish();
            const uint64_t b0 = nf::os::perf_counter();
            DriveKeys k;
            k.gas = true;
            rig_ticks = 0;
            for (int i = 0; i < a.bench_frames; ++i) {
                step(k);
                compose(a.width, a.height);
                r.draw_scene(scene);
            }
            r.finish();
            ms = 1000.0 * double(int64_t(nf::os::perf_counter() - b0)) / double(int64_t(freq)) / a.bench_frames;
            // O render do carro na CPU (RVehicle::UpdatePosition, a sombra, as instâncias): parte do quadro.
            const double rig_ms = 1000.0 * double(int64_t(rig_ticks)) / double(int64_t(freq)) / a.bench_frames;
            std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f carro_cpu_ms %.3f | %s\n", a.bench_frames, ms,
                        1000.0 / ms, rig_ms, r.stats().adapter.c_str());
        }
        compose(a.width, a.height);
        r.draw_scene(scene);
        report_line(ms);
        char b[512];
        std::snprintf(b, sizeof b,
                      "camera %.2f %.2f %.2f modo %d braco %d fov %.2f distancia %.2f olhar %.2f %.2f %.2f colisoes %llu "
                      "chao %llu barreiras %llu antivertigem %d pendentes %llu | itens %zu desenho chamadas %d | %dx%d | "
                      "adaptador %s\n",
                      eye[0], eye[1], eye[2], int(cam->d.mode), int(cam->d.arm), double(cam->d.fov),
                      double(cam->d.distance), double(cam->d.look_at[0]), double(cam->d.look_at[1]),
                      double(cam->d.look_at[2]), (unsigned long long)cam->collisions,
                      (unsigned long long)cam->ground_hits, (unsigned long long)cam->barrier_hits,
                      int(cam->statics.vertigo_count), (unsigned long long)drv::camera_pending(), scene.items.size(),
                      r.stats().draw_calls, a.width, a.height, r.stats().adapter.c_str());
        rep += b;
        {
            // Os modos da seção 23: o tipo do modo, o AutoDrive (o braço, a inclinação e o rumo em graus, o ponto preso),
            // as câmeras animadas e os eventos de câmera.
            const drv::CameraModeInfo& mi = cam_tables->mode(cam->d.mode);
            const drv::CameraEventsEnv& ce = drv::camera_events_env();
            std::snprintf(b, sizeof b,
                          "camera_modos tipo 0x%x id %d autodrive %llu braco %d inclinacao %.1f rumo %.1f preso %d "
                          "animacao %llu trilho %llu acoes %llu entrada_pov %d | eventos %llu carro_anim %llu "
                          "guardas %llu\n",
                          unsigned(mi.type), int(mi.cam_id), (unsigned long long)cam->autodrive_updates,
                          int(cam->d.ad_arm), double(cam->d.ad_pitch) * 360.0, double(cam->d.ad_yaw) * 360.0,
                          cam->d.lock_target != 0 ? 1 : 0, (unsigned long long)cam->anim_updates,
                          (unsigned long long)cam->ai_path_updates, (unsigned long long)cam->input_actions,
                          int(icm->d.type), (unsigned long long)ce.executed, (unsigned long long)ce.car_anims,
                          (unsigned long long)cam->guards);
            rep += b;
            // O trilho do carro do jogador e a troca de carro (seção 23): o estado do AIGroundVehicle, o RPathHandle.
            const drv::RailStats& rs = drv::rail_stats();
            const drv::HeliSectionStats& hs = drv::heli_section_stats();
            const drv::AIGroundVehicle* rv = drv::rail_player_vehicle();
            const drv::AISplinePath* rsp = rv ? nf::p32_ptr<const drv::AISplinePath>(rv->spline_path) : nullptr;
            const drv::RPathHandle* rh = rsp ? nf::p32_ptr<const drv::RPathHandle>(rsp->handle) : nullptr;
            std::snprintf(b, sizeof b,
                          "trilho estado %d caminho %d tempo %.2f acelerador %.3f desejado %.3f andando %d | engates %llu "
                          "solturas %llu passos %llu fisica %llu seguintes %llu eventos_caminho %llu ia %llu instancias "
                          "%llu caminhos %zu | trocas_de_carro %llu transicoes %llu guardas %llu\n",
                          rv ? int(rv->controller) : -1, rh ? 1 : 0, rh ? double(rh->time) : 0.0,
                          rh ? double(rh->throttle) : 0.0, rh ? double(rh->desired) : 0.0, rh ? int(rh->running) : 0,
                          (unsigned long long)rs.engaged, (unsigned long long)rs.disengaged,
                          (unsigned long long)rs.spline_updates, (unsigned long long)rs.spline_physics,
                          (unsigned long long)rs.next_paths, (unsigned long long)rs.path_events,
                          (unsigned long long)rs.ai_elements, (unsigned long long)rs.instances,
                          drv::path_engine_handles().size(), (unsigned long long)hs.changes,
                          (unsigned long long)hs.transitions, (unsigned long long)(rs.guards + hs.guards));
            rep += b;
        }
        std::string pick_env;
        if (nf::get_env_narrow("NF_DRV_PICK", &pick_env)) {
            // Depuração: as instâncias da pista desenhadas sob um pixel (o centro e o raio delas projetados).
            float px = 0, py = 0;
            NF_SSCANF(pick_env.c_str(), "%f,%f", &px, &py);
            float vpm[16];
            drv::drv_mat_mul(scene.view, scene.proj, vpm);
            for (const auto& d : draws) {
                const drv::TrackInstance& in = track.instances()[size_t(d.instance)];
                const float* c = in.matrix + 12;
                float clip[4];
                for (int k = 0; k < 4; ++k) clip[k] = c[0] * vpm[k] + c[1] * vpm[4 + k] + c[2] * vpm[8 + k] + vpm[12 + k];
                if (clip[3] <= 0.01f && pick_env != "all") continue;
                const float sx = (clip[0] / clip[3] * 0.5f + 0.5f) * float(a.width);
                const float sy = (0.5f - clip[1] / clip[3] * 0.5f) * float(a.height);
                const float sr = std::max(40.0f, in.radius / clip[3] * scene.proj[0] * 0.5f * float(a.width));
                if (pick_env != "all" && (std::fabs(sx - px) > sr || std::fabs(sy - py) > sr)) continue;
                const std::string an = in.article >= 0 ? track.articles()[size_t(in.article)].name : std::string("-");
                char pb[300];
                std::snprintf(pb, sizeof pb,
                              "pick instancia %d passada %d parte %d artigo %s pos %.1f,%.1f,%.1f raio %.1f bandeiras %02x "
                              "tela %.0f,%.0f\n",
                              d.instance, d.pass, d.part, an.c_str(), double(c[0]), double(c[1]), double(c[2]),
                              double(in.radius), unsigned(in.flags), double(sx), double(sy));
                rep += pb;
                for (const drv::CarDraw& cd : track.models().draws()) {
                    if (cd.part != d.part) continue;
                    std::snprintf(pb, sizeof pb, "  desenho %s tris %u", cd.method.c_str(), cd.index_count / 3);
                    rep += pb;
                    if (cd.family == 1) {
                        float lo[4] = {1e9f, 1e9f, 1e9f, 1e9f}, hi[4] = {-1e9f, -1e9f, -1e9f, -1e9f};
                        for (uint32_t k = 0; k < cd.index_count; ++k) {
                            const auto& v = track.models().bond_vertices()[track.models().bond_indices()[cd.first_index + k]];
                            for (int ch = 0; ch < 4; ++ch) {
                                lo[ch] = std::min(lo[ch], v.col[ch]);
                                hi[ch] = std::max(hi[ch], v.col[ch]);
                            }
                        }
                        std::snprintf(pb, sizeof pb, " cor %.0f-%.0f %.0f-%.0f %.0f-%.0f alfa %.0f-%.0f", double(lo[0]),
                                      double(hi[0]), double(lo[1]), double(hi[1]), double(lo[2]), double(hi[2]),
                                      double(lo[3]), double(hi[3]));
                        rep += pb;
                    }
                    for (int ps = 0; ps < drv::kCarMaxPasses; ++ps) {
                        if (!((cd.passes >> ps) & 1)) continue;
                        const drv::eagl::GsRegs& g = cd.pass[ps];
                        const char* tn = cd.texture[ps] >= 0 ? track.models().textures()[size_t(cd.texture[ps])].name.c_str()
                                                              : "-";
                        std::snprintf(pb, sizeof pb, " | p%d tex %s prim %03llx test %05llx alpha %02llx tfx %d tcc %d",
                                      ps + 1, tn, (unsigned long long)g.prim, (unsigned long long)(g.test & 0x7ffff),
                                      (unsigned long long)(g.alpha & 0xff), int((g.tex0 >> 35) & 3),
                                      int((g.tex0 >> 34) & 1));
                        rep += pb;
                    }
                    rep += "\n";
                }
            }
        }
        std::printf("%s", rep.c_str());
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        audio_end();
        ai_end();
        drv::mission_reset();
        drv::set_world(nullptr);
        return 0;
    }
    // A janela: a física a 60 passos por segundo do relógio, o desenho a cada volta.
    nf::Window win(L"nf_driving — " + widen(a.track) + L" — " + widen(car_name), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(track.models(), err) || !r.upload(model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    for (auto& tt : traffic_types)
        if (tt.second->ok && !r.upload(tt.second->model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
    if ((secondary && !r.upload(secondary->model, err)) || !drv::drive_actors::upload(r, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    if (hud_font_ok) r.set_ui_texture(hud_font.image_width, hud_font.image_height, hud_atlas.data());
    fx_app.textures(have_misc_arc ? &misc_arc : nullptr, &arc, r);
    chase(1.0f);
    report_line(0);
    std::printf("%s", rep.c_str());
    std::printf("W/seta acima acelera, S/seta abaixo freia (e dá ré), A/D/setas viram, espaço é o freio de mão, Esc sai\n");
    // O relógio do jogo pelo da parede (60 por segundo); a cada volta a entrada e o Run, que roda os ticks que passaram
    // (12 ou mais são jogados fora, como no PS2) e pede o desenho no último; sem tick, espera o próximo.
    const double t0s = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const int32_t clock0 = clock;
    (void)freq;
    while (win.pump()) {
        if (win.key_down(nf::key::Escape)) break;
        DriveKeys k;
        k.gas = win.key_down('W') || win.key_down(nf::key::Up);
        k.brake = win.key_down('S') || win.key_down(nf::key::Down);
        k.left = win.key_down('A') || win.key_down(nf::key::Left);
        k.right = win.key_down('D') || win.key_down(nf::key::Right);
        k.hand = win.key_down(nf::key::Space);
        // As armas no teclado (só do port): Ctrl o R1, Shift o L1, E o R2, Q o L2, F o triângulo.
        k.r1 = win.key_down(nf::key::Control);
        k.l1 = win.key_down(nf::key::Shift);
        k.r2 = win.key_down('E');
        k.l2 = win.key_down('Q');
        k.tri = win.key_down('F');
        nf::PadState pads[nf::kMaxPads];
        nf::system_pad_source().poll(pads);
        refresh_cam_ctx();
        input.feed(*car, pad_from(k, &pads[0]), clock);
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const int32_t clock_before = clock;
        clock = clock0 + int32_t((now - t0s) * 60.0);
        fire_late(clock_before, clock);
        if (clock > clock_before) hud_tick(float(clock - clock_before) / 60.0f);
        render_requested = false;
        drv::audio_set_timer(clock * 100 / 60);
        sched.Run(clock);
        if (audio_on) nf::audio_out_catch_up(uint32_t(clock));
        if (!render_requested) {
            nf::os::wait_until(t0s + double(clock - clock0 + 1) / 60.0);
            continue;
        }
        if (win.resized()) r.resize(win.width(), win.height());
        compose(win.width(), win.height());
        r.draw_scene(scene);
        r.present();
    }
    audio_end();
    ai_end();
    drv::mission_reset();
    drv::set_world(nullptr);
    return 0;
}

// O carro sozinho, parado sobre um chão plano (--ground, só do port): a física assentando o carro (o PBondCar sobre o
// FlatGround, 2 s de passos sem entrada), depois o RVehicle (as rodas na altura da suspensão, as lâmpadas, o dano) e a
// sombra simples desenhados como no jogo, vistos de uma câmera em volta. Não há chão desenhado: a sombra (Cd - Cs)
// escurece o fundo.
// --actor "<esqueleto>.<modelo>.<texturas...>[.<escala>]" (a cadeia do ActCharacterInfo, docs/driving.md seção 20):
// o personagem do .VIV desenhado pela família de pele, posado pelo ActPoser com uma animação de um banco (`--anim
// <banco>[:<nome ou índice>]`) tocando em laço a 60 quadros do jogo por segundo; `--seconds` o tempo de animação da
// captura; `--list-anims` lista os bancos e as animações do arquivo.
int run_actor(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    drv::Bigf arc;
    if (!arc.open(*disc, "DRIVING/" + a.viv)) {
        std::fprintf(stderr, "nf_driving: sem DRIVING/%s\n", a.viv.c_str());
        return 1;
    }
    auto lower_of = [](std::string s) {
        for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
        return s;
    };
    if (a.list_anims) {
        for (const drv::BigfEntry& e : arc.entries()) {
            const std::string l = lower_of(e.name);
            if (l.rfind("data\\actors\\anims\\", 0) != 0 || l.size() < 4 || l.compare(l.size() - 4, 4, ".dat") != 0) continue;
            drv::anim::AnimBank bank;
            std::string err;
            if (!bank.load(arc, drv::bigf_base_name(e.name), err)) continue;
            std::printf("banco %s: %d animacoes\n", drv::bigf_base_name(e.name).c_str(), bank.num_anims());
            for (int i = 0; i < bank.num_anims(); ++i) {
                float len = 0;
                std::unique_ptr<drv::anim::FnAnim> fa = drv::anim::NewFnAnim(*nf::p32_ptr<const uint16_t>(bank.anim(i)));
                if (fa) {
                    fa->SetAnimMemoryMap(bank.anim(i));
                    fa->GetLength(len);
                }
                std::printf("  %3d %-28s quadros %.0f\n", i, bank.anim_name(i), double(len));
            }
        }
        return 0;
    }
    // A cadeia do personagem: o esqueleto, o modelo (+ o LOD), a escala (o último número, %).
    std::vector<std::string> tok;
    {
        size_t p = 0;
        while (p <= a.actor.size()) {
            const size_t q = a.actor.find('.', p);
            tok.push_back(a.actor.substr(p, q == std::string::npos ? std::string::npos : q - p));
            if (q == std::string::npos) break;
            p = q + 1;
        }
    }
    if (tok.size() < 2) {
        std::fprintf(stderr, "nf_driving: --actor \"<esqueleto>.<modelo>...\" (ex. 2.h_pmask.h_pmask.t_paris.95)\n");
        return 1;
    }
    const int skel_index = std::atoi(tok[0].c_str());
    std::string model_name = tok[1];
    if (model_name.empty() || !std::isdigit(static_cast<unsigned char>(model_name.back()))) model_name += "1";  // o LOD 1
    float char_scale = 1.0f;
    if (tok.size() >= 3 && !tok.back().empty() && std::isdigit(static_cast<unsigned char>(tok.back()[0])))
        char_scale = float(std::atoi(tok.back().c_str())) / 100.0f;
    std::vector<std::string> ssh;
    for (const drv::BigfEntry& e : arc.entries()) {
        const std::string l = lower_of(e.name);
        if ((l.rfind("data\\actors\\textures\\", 0) == 0 && l.size() > 4 && l.compare(l.size() - 4, 4, ".ssh") == 0) ||
            l == "data\\render\\actors.ssh")
            ssh.push_back(e.name);
    }
    std::string err;
    drv::CarModel model;
    model.set_verify(a.verify);
    // As texturas do personagem nos lugares do Actors.ssh pela primeira letra do nome (h_ a cabeça 'hhhh', t_ o tronco
    // 'tttt', b_ o corpo 'bbbb'): o LoadCharacter carrega as duas (ou a uma) da cadeia e o ActModel::SetTexture as troca.
    {
        std::vector<std::pair<std::string, std::string>> aliases;
        for (size_t i = 2; i < tok.size(); ++i) {
            const std::string& t = tok[i];
            if (t.size() < 3 || t[1] != '_') continue;
            const char c = char(tolower(static_cast<unsigned char>(t[0])));
            if (c != 'h' && c != 't' && c != 'b') continue;
            aliases.push_back({std::string(4, c), "data\\actors\\textures\\" + t + ".ssh"});
        }
        model.set_texture_aliases(aliases);
    }
    const std::string mkey = "data\\actors\\models\\" + model_name;
    if (!model.load_eagl(*disc, a.viv, mkey + ".dat", mkey + ".rel", ssh, model_name, err)) {
        std::fprintf(stderr, "nf_driving: %s: %s\n", model_name.c_str(), err.c_str());
        return 1;
    }
    drv::anim::ActSkeleton skel;
    if (!skel.load(arc, skel_index, err)) {
        std::fprintf(stderr, "nf_driving: esqueleto %d: %s\n", skel_index, err.c_str());
        return 1;
    }
    // A animação: o banco e a animação pelo nome ou pelo número (sem --anim: a pose parada).
    drv::anim::AnimBank bank;
    drv::anim::ActAnimGroup group;
    std::string anim_label = "pose parada";
    if (!a.anim.empty()) {
        const size_t colon = a.anim.find(':');
        const std::string bname = a.anim.substr(0, colon);
        const std::string which = colon == std::string::npos ? std::string("0") : a.anim.substr(colon + 1);
        if (!bank.load(arc, bname, err)) {
            std::fprintf(stderr, "nf_driving: banco %s: %s\n", bname.c_str(), err.c_str());
            return 1;
        }
        int idx = bank.find(which.c_str());
        if (idx < 0 && !which.empty() && std::isdigit(static_cast<unsigned char>(which[0]))) idx = std::atoi(which.c_str());
        if (idx < 0 || idx >= bank.num_anims()) {
            std::fprintf(stderr, "nf_driving: o banco %s não tem a animação %s (--list-anims)\n", bname.c_str(),
                         which.c_str());
            return 1;
        }
        group.ChangeAnimationData(bank.anim(idx), 0);
        anim_label = bname + ":" + bank.anim_name(idx);
    }
    // O poser: o tempo de quadro do jogo (1/60 s), a PoseToWorld identidade (o esqueleto em unidades dele); a matriz do
    // mundo do desenho é a escala do ActActor (BuildScale: a escala do personagem x o GetBaseScaleFactor 0,1).
    drv::anim::ActPoser poser(&skel, 1.0f / 60.0f);
    const int n = skel.num_bones();
    std::vector<float> skin(size_t(n) * 16, 0.0f);
    int loops = 0, ticks = 0;
    auto start_anim = [&]() {
        if (group.anim_data) poser.SetNormalAnimation(&group, 0.0f);
    };
    auto tick = [&]() {
        // Um quadro do jogo: o ActActor posa (DoSkeletonPose) e depois avança o tempo; no fim, recomeça (só do port: o
        // laço que o controlador de animação do ator faria).
        poser.DoSkeletonPose();
        poser.Skin(skin.data());
        poser.AdvanceTime();
        if (group.anim_data && poser.IsAnimationDone()) {
            start_anim();
            ++loops;
        }
        ++ticks;
    };
    start_anim();
    if (!group.anim_data) {
        // Sem animação: a pose parada pelo PoseSQTToLocal (o DoMainPose só a calcula com uma animação).
        skel.GetStillPose();
        drv::anim::Skeleton_PoseSQTToLocal(skel.skeleton(), skel.still_pose.data(), skel.matrices.data());
    }
    const int ticks_wanted = int(a.seconds * 60.0f + 0.5f);
    for (int i = 0; i <= ticks_wanted; ++i) tick();
    const float s = 0.1f * char_scale;
    float world[16] = {s, 0, 0, 0, 0, s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1};
    // A caixa dos ossos no mundo (o alvo e a distância da câmera).
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    auto bounds = [&]() {
        for (int c = 0; c < 3; ++c) {
            lo[c] = 1e30f;
            hi[c] = -1e30f;
        }
        for (int b = 0; b < n; ++b)
            for (int c = 0; c < 3; ++c) {
                const float v = poser.current()[size_t(16 * b + 12 + c)] * s;
                lo[c] = std::min(lo[c], v);
                hi[c] = std::max(hi[c], v);
            }
    };
    bounds();
    const float center[3] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};
    float radius = 0;
    for (int c = 0; c < 3; ++c) radius += (hi[c] - lo[c]) * (hi[c] - lo[c]) * 0.25f;
    radius = std::max(0.3f, std::sqrt(radius));
    drv::DrvScene scene;
    auto compose = [&](int w, int hgt, float yaw) {
        scene = drv::DrvScene();
        scene.clear[0] = 0.40f;
        scene.clear[1] = 0.42f;
        scene.clear[2] = 0.45f;
        scene.k = model.constants();
        scene.fog_color[0] = scene.fog_color[1] = scene.fog_color[2] = 128.0f;
        const float d = radius * 2.6f * a.dist;
        const float eye[3] = {center[0] + d * nf::fm::cos(a.pitch) * nf::fm::sin(yaw), center[1] + d * nf::fm::sin(a.pitch),
                              center[2] - d * nf::fm::cos(a.pitch) * nf::fm::cos(yaw)};
        drv::drv_look_at(eye, center, scene.view);
        drv::drv_perspective(a.fov, float(w) / float(hgt), std::max(0.05f, d - radius * 2.0f), d + radius * 2.0f,
                             scene.proj);
        std::memcpy(scene.eye, eye, sizeof eye);
        for (size_t p = 0; p < model.parts().size(); ++p) {
            drv::DrvItem it;
            it.model = &model;
            it.part = int(p);
            std::memcpy(it.world, world, sizeof world);
            it.bones = skin.data();
            it.bone_count = n;
            scene.items.push_back(it);
        }
    };
    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.spec = a.spec;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    auto report_text = [&]() {
        const drv::CarModelStats& ms = model.stats();
        char b[1024];
        std::snprintf(b, sizeof b,
                      "personagem %s esqueleto %d ossos %d raiz %d modelo %s escala %.2f | animacao %s quadros %.1f "
                      "tempo %.3f anterior %.3f laços %d quadros_do_jogo %d | caixa %.2f,%.2f,%.2f - %.2f,%.2f,%.2f | "
                      "pele lotes %d paletas %d guardas %d desenhos %d | animacao guardas %llu | %dx%d | adaptador %s\n",
                      a.actor.c_str(), skel_index, n, skel.d.root, model_name.c_str(), double(char_scale),
                      anim_label.c_str(), double(group.length), double(poser.time), double(poser.previous_time), loops,
                      ticks, double(lo[0]), double(lo[1]), double(lo[2]), double(hi[0]), double(hi[1]), double(hi[2]),
                      ms.char_batches, ms.palette_matrices, ms.char_guards, r.stats().char_calls,
                      static_cast<unsigned long long>(drv::anim::anim_stats().guards), a.width, a.height,
                      r.stats().adapter.c_str());
        std::string out = b;
        for (const drv::CarDraw& d : model.draws()) {
            if (d.family != 2) continue;
            char l[256];
            std::snprintf(l, sizeof l, "  desenho %s passadas %d indices %u prim %llx fge %d alfa %llx teste %llx\n",
                          d.method.c_str(), d.passes, d.index_count, static_cast<unsigned long long>(d.pass[0].prim),
                          int((d.pass[0].prim >> 5) & 1), static_cast<unsigned long long>(d.pass[0].alpha),
                          static_cast<unsigned long long>(d.pass[0].test));
            out += l;
        }
        return out;
    };
    if (!a.screenshot.empty()) {
        if (!r.init(nullptr, ro, err) || !r.upload(model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        compose(a.width, a.height, a.yaw);
        r.draw_scene(scene);
        Args b = a;
        b.model = model_name;
        std::string rep = report(model, b, &r.stats()) + report_text();
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        std::printf("%s", summary(rep).c_str());
        std::printf("%s", report_text().c_str());
        return 0;
    }
    nf::Window win(L"nf_driving — " + widen(a.actor), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s", report_text().c_str());
    const uint64_t freq = nf::os::perf_frequency();
    uint64_t t0 = nf::os::perf_counter();
    double acc = 0;
    float yaw = a.yaw;
    while (win.pump()) {
        const uint64_t t1 = nf::os::perf_counter();
        const double dt = double(int64_t(t1 - t0)) / double(int64_t(freq));
        t0 = t1;
        acc += dt;
        while (acc >= 1.0 / 60.0) {
            tick();
            acc -= 1.0 / 60.0;
        }
        if (a.spin) yaw += float(dt) * 0.3f;
        if (win.resized()) r.resize(win.width(), win.height());
        compose(win.width(), win.height(), yaw);
        r.draw_scene(scene);
        r.present();
    }
    return 0;
}

int run_still_ground(const Args& a) {
    const std::shared_ptr<nf::DiscSource> disc = open_data(a);
    if (!disc) return 1;
    std::string err;
    drv::CarModel model;
    model.set_verify(a.verify);
    if (!model.load(*disc, a.viv, a.model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    drv::Bigf arc;
    arc.open(*disc, "DRIVING/" + a.viv);
    drv::AttributeCollection attrs;
    drv::CarDesc desc;
    if (!attrs.load(arc, "pvehicle", a.model, "default", &err) || !drv::read_car_desc(arc, a.model, desc, &err)) {
        std::fprintf(stderr, "nf_driving: --ground: %s\n", err.c_str());
        return 1;
    }
    auto tun = [&](const std::string& bank) { return read_entry(arc, "data\\tuning\\Physics\\" + bank + "\\default.tun"); };
    drv::reset_rigid_globals();
    drv::physics_pending_reset();
    const std::string rigid = tun("Rigid");
    drv::InitRigidBodySystem("default", rigid.empty() ? nullptr : rigid.c_str());
    drv::reset_bond_car_tuning();
    const std::string physical = tun("Physical"), friction = tun("Friction");
    drv::load_bond_car_tuning(physical.c_str(), friction.c_str());
    drv::FlatGround flat(0.0f);
    drv::set_world(&flat);
    drv::simulation_reset(60);
    const float start[3] = {0.0f, 1.0f, 0.0f}, dir[3] = {0.0f, 0.0f, 1.0f};
    drv::BondCar* car = new drv::BondCar();
    CarRig* rig = new CarRig();
    rig->attach(*car);
    drv::feedback_reset();
    drv::RigidBody* rb = drv::PBondCar_Construct(*car, 0, a.model, desc, attrs, dir, start);
    if (!rb) {
        std::fprintf(stderr, "nf_driving: sem vaga para o corpo do carro\n");
        return 1;
    }
    rig->post_load(model, *car, read_entry(arc, "data\\tuning\\Render\\SwitchEffects\\default.tun"),
                   read_entry(arc, "data\\tuning\\Render\\FX\\default.tun"), a.model);
    for (int i = 0; i < 120; ++i) drv::ESimFrameUpdate(drv::simulation());
    const float render_offset = drv::CarDesc_RenderOffset(desc);
    const std::vector<uint16_t> list = model.view_draw_list(1);
    // O raio da caixa do carro (a câmera fica a 2,2 raios, como a vista girando sem chão).
    float radius = 0;
    for (int c = 0; c < 3; ++c) {
        const float e = (desc.bbox_max[c] - desc.bbox_min[c]) * 0.5f;
        radius += e * e;
    }
    radius = std::sqrt(radius);
    drv::DrvScene scene;
    auto compose = [&](int w, int hgt, float yaw) {
        scene = drv::DrvScene();
        scene.clear[0] = 0.40f;
        scene.clear[1] = 0.42f;
        scene.clear[2] = 0.45f;
        scene.k = model.constants();
        scene.fog_color[0] = scene.fog_color[1] = scene.fog_color[2] = 128.0f;
        const float d = radius * 2.2f * a.dist;
        const float at[3] = {rb->pos[0], rb->pos[1], rb->pos[2]};
        const float eye[3] = {at[0] + d * nf::fm::cos(a.pitch) * nf::fm::sin(yaw), at[1] + d * nf::fm::sin(a.pitch),
                              at[2] - d * nf::fm::cos(a.pitch) * nf::fm::cos(yaw)};
        drv::drv_look_at(eye, at, scene.view);
        drv::drv_perspective(a.fov, float(w) / float(hgt), std::max(0.05f, d - radius * 1.5f), d + radius * 1.5f,
                             scene.proj);
        std::memcpy(scene.eye, eye, sizeof eye);
        rig->frame(*car, model, render_offset, false, false, int32_t(drv::simulation().steps), list, scene);
    };
    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.refl = a.refl;
    ro.spec = a.spec;
    ro.fallback = a.fallback;
    drv::DrvRenderer r;
    const drv::RVehicleData& vd = rig->vehicle.d;
    auto report_text = [&]() {
        char b[768];
        std::snprintf(b, sizeof b,
                      "chao_plano carro %s pos %.3f,%.3f,%.3f rodas_no_chao %d compressao %.3f/%.3f/%.3f/%.3f | rodas "
                      "ossos %u rodas %d,%d,%d,%d cubos %d,%d,%d,%d | sombra %s nivel %d motivo %d cantos %.2f,%.2f,%.2f "
                      "%.2f,%.2f,%.2f | itens %zu quadrados %d desenho chamadas %d | %dx%d | adaptador %s\n",
                      a.model.c_str(), rb->pos[0], rb->pos[1], rb->pos[2], int(car->d.num_wheels_on_ground),
                      double(car->d.suspension[0]), double(car->d.suspension[1]), double(car->d.suspension[2]),
                      double(car->d.suspension[3]), vd.bone_count, vd.wheel_bone[0], vd.wheel_bone[1], vd.wheel_bone[2],
                      vd.wheel_bone[3], vd.hub_bone[0], vd.hub_bone[1], vd.hub_bone[2], vd.hub_bone[3],
                      rig->last_shadow.drawn ? "desenhada" : "nao", rig->last_shadow.level, rig->last_shadow.reason,
                      double(rig->last_shadow.corner[0][0]), double(rig->last_shadow.corner[0][1]),
                      double(rig->last_shadow.corner[0][2]), double(rig->last_shadow.corner[3][0]),
                      double(rig->last_shadow.corner[3][1]), double(rig->last_shadow.corner[3][2]), scene.items.size(),
                      r.stats().quads, r.stats().draw_calls, a.width, a.height, r.stats().adapter.c_str());
        return std::string(b);
    };
    if (!a.screenshot.empty()) {
        if (!r.init(nullptr, ro, err) || !r.upload(model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        compose(a.width, a.height, a.yaw);
        r.draw_scene(scene);
        std::string rep = report(model, a, &r.stats()) + report_text();
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        std::printf("%s", summary(rep).c_str());
        drv::set_world(nullptr);
        return 0;
    }
    nf::Window win(L"nf_driving — " + widen(a.model), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    const uint64_t freq = nf::os::perf_frequency();
    uint64_t t0 = nf::os::perf_counter();
    float yaw = a.yaw;
    while (win.pump()) {
        const uint64_t t1 = nf::os::perf_counter();
        const float dt = float(double(int64_t(t1 - t0)) / double(int64_t(freq)));
        t0 = t1;
        if (a.spin) yaw += dt * 0.6f;
        if (win.resized()) r.resize(win.width(), win.height());
        compose(win.width(), win.height(), yaw);
        r.draw_scene(scene);
        r.present();
    }
    drv::set_world(nullptr);
    return 0;
}

int nf_app_main(int argc, wchar_t** argv) {
    console();
    Args a;
    if (!parse(argc, argv, a)) return 1;
    if (a.census) return run_census(a);
    if (a.anim_dump) {
        // A animação das instâncias do carro (driving/render/anim_engine.h): os sistemas, as animações e os eventos.
        const std::shared_ptr<nf::DiscSource> disc = open_data(a);
        if (!disc) return 1;
        drv::CarModel m;
        m.set_resolve_carp(true);
        std::string err;
        if (!m.load(*disc, a.viv, a.model, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        uint32_t inst = 0, n = 0, dmg = 0;
        if (!drv::carp_instance_list(m.carp_root(), inst, n, dmg)) return 1;
        drv::anim_engine_reset();
        const uint32_t h = drv::AnimHandle_Create(n, 0, inst, dmg, 0);
        std::printf("%s", drv::AnimHandle_Describe(h).c_str());
        return 0;
    }
    if (a.check) return run_check(a);
    if (!a.track.empty() && a.drive) return run_drive(a);
    if (!a.track.empty()) return run_track(a);
    if (a.ground) return run_still_ground(a);
    if (!a.actor.empty() || a.list_anims) return run_actor(a);
    if (!a.track_census.empty()) {
        const std::shared_ptr<nf::DiscSource> disc = open_data(a);
        drv::CarModel m;
        m.set_resolve_carp(true);
        m.set_verify(a.verify);
        std::string e;
        const std::string t = a.track_census;
        if (!disc || !m.load_carp(*disc, a.viv, "data\\track\\" + t + ".crp", {"data\\track\\" + t + ".ssh"}, t, e)) {
            std::fprintf(stderr, "nf_driving: %s\n", e.c_str());
            return 1;
        }
        Args b = a;
        b.model = t;
        std::printf("%s", summary(report(m, b, nullptr)).c_str());
        std::printf("simbolos_nulos");
        for (const auto& kv : m.carp_misses()) std::printf(" %s=%d", kv.first.c_str(), kv.second);
        const drv::CarpResolveStats& rs = drv::carp_resolve_stats();
        std::printf(" | sr %d nulos %d registros_por_etiqueta %d listas_de_eventos %d\n", rs.symbolic,
                    rs.symbolic_null, rs.tag_records, rs.event_lists);
        return 0;
    }

    drv::CarModel car;
    car.set_verify(a.verify);
    std::string err;
    if (!car.load(a.data, a.viv, a.model, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    drv::DrvRenderOptions ro;
    ro.width = a.width;
    ro.height = a.height;
    ro.spec = a.spec;
    ro.refl = a.refl;
    ro.fallback = a.fallback;
    ro.fov_deg = a.fov;
    ro.adapter = a.adapter;
    ro.only_part = a.part;
    ro.view = a.view;
    drv::DrvRenderer r;
    if (!a.screenshot.empty()) {
        if (!r.init(nullptr, ro, err) || !r.upload(car, err)) {
            std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
            return 1;
        }
        if (a.bench_frames > 0) {
            // Quadros girando, fora da tela, até a GPU terminar: o custo médio por quadro (sem Present).
            r.draw(car, a.yaw, a.pitch, a.dist);
            r.finish();
            const uint64_t f = nf::os::perf_frequency();
            const uint64_t t0 = nf::os::perf_counter();
            for (int i = 0; i < a.bench_frames; ++i) r.draw(car, a.yaw + 0.01f * float(i), a.pitch, a.dist);
            r.finish();
            const uint64_t t1 = nf::os::perf_counter();
            const double ms = 1000.0 * double(int64_t(t1 - t0)) / double(int64_t(f)) / a.bench_frames;
            std::printf("medida quadros %d ms_por_quadro %.3f qps %.0f | %s\n", a.bench_frames, ms, 1000.0 / ms,
                        r.stats().adapter.c_str());
        }
        r.draw(car, a.yaw, a.pitch, a.dist);
        if (!r.save_png(widen(a.screenshot))) {
            std::fprintf(stderr, "nf_driving: não gravei %s\n", a.screenshot.c_str());
            return 1;
        }
        const std::string rep = report(car, a, &r.stats());
        FILE* f = nf::fopen_narrow((a.screenshot + ".txt").c_str(), "wb");
        if (f) {
            std::fwrite(rep.data(), 1, rep.size(), f);
            std::fclose(f);
        }
        std::printf("%s", summary(rep).c_str());
        return 0;
    }
    nf::Window win(L"nf_driving — " + widen(a.model), a.width, a.height, false);
    if (!r.init(win.native_handle(), ro, err) || !r.upload(car, err)) {
        std::fprintf(stderr, "nf_driving: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s", summary(report(car, a, nullptr)).c_str());
    const uint64_t freq = nf::os::perf_frequency();
    uint64_t t0 = nf::os::perf_counter();
    float yaw = a.yaw;
    while (win.pump()) {
        const uint64_t t1 = nf::os::perf_counter();
        const float dt = float(double(int64_t(t1 - t0)) / double(int64_t(freq)));
        t0 = t1;
        if (a.spin) yaw += dt * 0.6f;
        if (win.resized()) r.resize(win.width(), win.height());
        r.draw(car, yaw, a.pitch, a.dist);
        r.present();
    }
    return 0;
}

