#pragma once
// Interpolação entre ticks (só do port; docs/port.md, "Interpolação entre ticks").
//
// A lógica do jogo roda a 60 Hz, como no PS2, e o Game_Draw roda uma vez por tick (o estado que ele guarda de um
// quadro para o outro, como os relâmpagos, o céu que gira da 0700001b e o GlobalBlur, anda no ritmo do PS2). Acima
// de 60 Hz, o renderizador desenha entre dois ticks: cada quadro de vídeo mistura o registro do tick anterior e o do
// atual por alpha = a fração do tick em curso que já passou (a imagem fica um tick atrás do jogo).
//
// O que é guardado: de cada tick, a saída do RuntimeGfx::collect_frame (os DrawItem do Game_Draw, já com a
// paleta copiada) e a câmera em uso (a matriz do viewer_tag e o campo de visão). O que é interpolado:
//   - a câmera: a posição em linha reta, a rotação por slerp, o campo de visão em linha reta;
//   - os modelos rígidos e com skinning: cada item do tick atual procura o mesmo desenho no anterior pela
//     identidade (DrawItem::interp: o dono do desenho, o modelo, obj_tag::created e a ordem dentro do dono, que
//     psi_draw grava; render/runtime_gfx.cpp a monta). Achado, a matriz (e cada osso da paleta) é decomposta em
//     translação, rotação e escala, interpolada e recomposta. Sem par, fica o do tick atual;
//   - os pesos dos morphs dos rostos (DrawItem::morph): em linha reta entre os dois ticks, peso a peso (um lado sem
//     pesos conta como zeros), no mesmo par do item;
//   - o alfa dos esmaecimentos (psiFadeView, DrawItem::fade): em linha reta, quando os dois ticks têm o esmaecimento
//     da mesma câmera (um que começa ou acaba fica como o do tick atual);
//   - as partículas, os estilhaços, o 2D (HUD, menus, legendas), a névoa, as luzes e o tweak: sempre os do tick
//     atual (o 2D nunca se interpola; as luzes e o tweak mudam pouco de um tick para o outro e são do estado de
//     desenho do jogo).
//
// Cortes (sem interpolação): o primeiro tick depois de uma carga (reset), a câmera do script (a 4) ligando ou
// desligando, um salto da câmera (mais de CutLimits::distance metros ou CutLimits::angle de giro por tick, ou o
// campo de visão mudando mais que CutLimits::fov_ratio). Num corte o quadro inteiro é o do tick atual. Um item que
// salta (mais de ItemLimits::distance metros ou ItemLimits::angle de giro num tick: o clarão do tiro, que gira ao
// acaso a cada tick, um objeto teleportado) fica com o do tick atual.
//
// Visibilidade: os portais e as listas de desenho continuam por tick (o jogo decide o que se vê). Para a câmera
// interpolada não perder o que o tick anterior via e o atual não vê mais (girando, atravessando um portal), o
// quadro também leva os itens do tick anterior sem par que ainda existem: salas e células sempre; objetos só se o
// jogo ainda os tem vivos e desenháveis (o teste `alive` de push). Eles entram como estavam no tick anterior.
//
// O olhar do mouse sem atraso (opcional): ver camera_look_delta e apply_about_eye; o main.cpp junta o giro do mouse
// que o tick atual já aplicou ((1 - alpha) dele, o que a interpolação ainda não mostrou) e o que chegou depois dele.

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "render/renderer.h"

namespace nf {

// ---- matemática (matrizes de vetor-linha: v' = v * M, translação na linha 3) ----

// Uma matriz afim decomposta: as linhas 0-2 são s[i] * R[i] (R uma rotação, o quatérnio q em x, y, z, w) e a
// linha 3 é t. Cisalhamento se perde (a recomposição sai ortogonal).
struct Xform {
    float t[3]{};
    float q[4]{0.0f, 0.0f, 0.0f, 1.0f};
    float s[3]{1.0f, 1.0f, 1.0f};
};
// Falso se a matriz é degenerada (uma linha de tamanho quase 0). Com determinante negativo, s[0] fica negativo.
bool xform_decompose(const float m[16], Xform* out);
// Escreve as colunas 0-2 das quatro linhas; a coluna w (m[3], m[7], m[11], m[15]) fica como está.
void xform_compose(const Xform& x, float m[16]);
// O ângulo entre duas rotações, em radianos (0 a pi).
float quat_angle(const float a[4], const float b[4]);
void quat_slerp(const float a[4], const float b[4], float t, float out[4]);
void xform_lerp(const Xform& a, const Xform& b, float t, Xform* out);
// A matriz entre a e b: alpha <= 0 dá a, alpha >= 1 dá b (exatas); no meio, a decomposição interpolada (a coluna
// w é a de b). Matrizes degeneradas, ou com o sinal do determinante diferente, dão b.
void interp_matrix(const float a[16], const float b[16], float alpha, float out[16]);
// Os pesos dos morphs entre dois ticks: a + (b - a) * alpha, peso a peso, com o mais curto completado com zeros;
// alpha <= 0 dá a e alpha >= 1 dá b, exatos (completados com zeros até o tamanho do maior).
void interp_weights(const std::vector<float>& a, const std::vector<float>& b, float alpha, std::vector<float>* out);

// A câmera de um registro: a matriz do viewer_tag (linhas 0 direita, 1 cima, 2 frente, 3 posição; determinante
// +1) e o campo de visão vertical em graus.
struct InterpCamera {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float fov_deg = 60.0f;
};
void interp_camera(const InterpCamera& a, const InterpCamera& b, float alpha, InterpCamera* out);
// O giro e a elevação da frente da câmera (atan2(f.x, f.z) e asin(f.y), em radianos): as grandezas do
// Player_Aiming (obj_tag::rot.y e BLData::pitch x 90 graus).
float camera_yaw(const float m[16]);
float camera_elevation(const float m[16]);
// Gira a câmera: a frente sobe dpitch (no plano frente-cima, com a elevação presa a +-max_elev, como o
// Player_ViewClamping) e depois tudo gira dyaw em volta do eixo y do mundo (dyaw positivo: para a esquerda, como
// obj_tag::rot.y). A posição fica. W recebe a rotação do mundo aplicada (linhas novas = linhas * W, 3 x 3).
void camera_look_delta(float m[16], float dyaw, float dpitch, float max_elev, float W[9]);
// A mesma rotação W em volta do olho, numa matriz de objeto: m' = m * T(-eye) * W * T(eye). Para os desenhos
// presos à câmera (a lista 2, a arma em primeira pessoa e o clarão), que giram junto com o olhar.
void apply_about_eye(float m[16], const float W[9], const float eye[3]);

// A identidade de um desenho entre ticks (nunca 0).
uint64_t draw_identity(const void* owner, uint8_t owner_kind, const void* glist, uint32_t created, uint16_t seq,
                       bool skinned);

struct CutLimits {
    float distance = 1.5f;   // metros por tick
    float angle = 0.7854f;   // radianos por tick (45 graus)
    float fov_ratio = 1.25f;
};
// A câmera saltou entre dois ticks (ticks: VBlanks entre eles, 1 ou 2; os limites são por tick de 60 Hz).
bool camera_cut(const InterpCamera& a, const InterpCamera& b, float ticks, const CutLimits& limits = CutLimits{});

struct ItemLimits {
    float distance = 2.0f;   // metros por tick
    float angle = 0.7854f;   // radianos por tick
};

// ---- os registros ----

struct TickRecord {
    std::vector<DrawItem> items;  // a saída do collect_frame
    InterpCamera camera;
    bool scene_camera = false;    // a câmera 4 (Script_ActiveCamera) em vez da 0
    float ticks = 1.0f;           // VBlanks desde o tick anterior (o skip do laço: 1, ou 2 a 30 Hz)
};

struct InterpStats {
    uint32_t items = 0;         // itens do quadro
    uint32_t matched = 0;       // com par no tick anterior
    uint32_t moving = 0;        // com par e matriz ou paleta diferente (interpolados)
    uint32_t snapped = 0;       // com par, mas saltaram (ficam os do tick atual)
    uint32_t kept = 0;          // do tick anterior sem par, mantidos no quadro (visibilidade)
    uint32_t bones = 0;         // ossos interpolados por quadro
    uint32_t morphs = 0;        // rostos com par e pesos de morph diferentes (interpolados)
    uint32_t fades = 0;         // esmaecimentos (psiFadeView) com o alfa interpolado
    bool cut = false;           // o tick atual é um corte
    uint32_t pushes = 0;        // ticks registrados desde o reset
};

class FrameInterpolator {
public:
    // O objeto `owner` (obj_tag*) com obj_tag::created = `created` ainda existe e seria desenhado (só o tick atual
    // sabe); usado para manter no quadro os objetos do tick anterior que o atual deixou de ver.
    using AliveFn = std::function<bool(const void* owner, uint32_t created)>;

    // Esquece os registros (carga de fase): o próximo push não interpola.
    void reset();
    // Um tick novo. Com o anterior, acha os pares, decide o corte e monta a base do quadro.
    void push(TickRecord&& rec, const AliveFn& alive = nullptr, const CutLimits& cut = CutLimits{},
              const ItemLimits& item = ItemLimits{});
    bool empty() const { return !has_cur_; }
    bool has_previous() const { return has_prev_; }
    const TickRecord& latest() const { return cur_; }
    bool cut() const { return stats_.cut; }
    // O quadro em alpha (0 = o tick anterior, 1 = o atual; preso a 0..1): os itens (o armazenamento é reaproveitado
    // até o próximo build ou push) e a câmera.
    const std::vector<DrawItem>& build(float alpha, InterpCamera* camera);
    // Depois de build: gira os itens da lista 2 (presos à câmera) por W em volta do olho (apply_about_eye).
    void rotate_overlay(const float W[9], const float eye[3]);
    const InterpStats& stats() const { return stats_; }
    // O item do último quadro montado cujo dono é `owner` (o primeiro com modelo; nulo se não houver).
    const DrawItem* find_owner(const void* owner) const;

private:
    struct Slot {
        uint32_t frame = 0;      // índice em frame_
        int32_t prev = -1;       // índice no anterior (-1: sem par)
        uint32_t cur = 0;        // índice no atual
        bool overlay = false;    // lista 2: volta ao valor base a cada build (rotate_overlay)
        bool interp = false;     // interpola (par, sem salto, matriz ou paleta diferente)
        bool world_moves = false;
        bool morph_moves = false;  // os pesos dos morphs mudaram (DrawItem::morph)
        uint32_t bone_first = 0;  // em bone_a_/bone_b_/bone_moves_
        uint32_t bone_count = 0;
        Xform wa, wb;
    };

    // Um esmaecimento com o alfa entre o do tick anterior (a) e o do atual (b): frame_[frame].quads2d[0].
    struct FadeSlot {
        uint32_t frame;
        uint8_t a, b;
    };
    std::vector<FadeSlot> fades_;
    TickRecord prev_, cur_;
    bool has_prev_ = false;
    bool has_cur_ = false;
    std::vector<DrawItem> frame_;
    std::vector<Slot> slots_;
    std::vector<Xform> bone_a_, bone_b_;
    std::vector<uint8_t> bone_moves_;
    std::unordered_map<uint64_t, int32_t> prev_ids_;
    InterpStats stats_;
};

// ---- o ritmo dos quadros (só do port; [video] fps_cap, platform/video_settings.h) ----

// O próximo prazo de um quadro: prev + period; se o laço já passou do prazo por mais de um período, recomeça de now
// (sem rajada de quadros para recuperar o atraso). Tempos em segundos.
double next_frame_deadline(double prev_deadline, double now, double period);
// A fração do tick em curso (o alpha da interpolação): (vblanks - tick_vblank) / tick_len, presa a 0..1.
float tick_alpha(double vblanks, double tick_vblank, double tick_len);

// O agendador dos ticks do main.cpp: o do main do PS2 (0x00109080: os VBlanks desde o último tick, no máximo 2, que viram
// a taxa de GS_SetRefreshRate) mais a câmera lenta da roda de armas (só do port; engine/weapon_wheel.h). elapsed:
// VBlankCount - GameCount (os pares com um filme); force: um tick mesmo sem VBlank novo (antes da primeira carga).
// Com slowmo < 1 os VBlanks continuam sendo consumidos (o relógio, o som e o tempo da missão seguem o real), mas só
// ganham ticks na fração slowmo (acumulada em *acc): cada tick que roda é um tick de 60 Hz (skip 1), e o main desenha
// entre eles com a interpolação (o registro do tick dura os VBlanks consumidos desde o anterior). slowmo 0: nenhum tick.
struct TickPlan {
    bool due = false;       // um VBlank novo (ou force): o main consome `consumed` VBlanks
    bool run = false;       // e roda um tick
    bool resync = false;    // mais de 2 VBlanks atrasado: GameCount volta a VBlankCount antes (como no PS2)
    int32_t consumed = 0;   // VBlanks somados a GameCount
    int32_t skip = 1;       // a taxa do tick (GS_SetRefreshRate(60 / skip))
};
TickPlan plan_tick(int32_t elapsed, bool force, float slowmo, float* acc);

}  // namespace nf
