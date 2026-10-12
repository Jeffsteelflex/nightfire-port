// A roda de armas. Ver weapon_wheel.h.

#include "engine/weapon_wheel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "engine/anim.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/menu.h"
#include "engine/menu_dossier.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "platform/psi_input.h"
#include "platform/fmath.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kOpenSeconds = 0.12f;
constexpr float kCloseSeconds = 0.10f;
constexpr float kPendingSeconds = 1.0f;  // uma escolha recusada agora (a arma no meio de uma animação) espera tanto

BLData* bloke(obj_tag* obj) { return static_cast<BLData*>(obj->data); }

uint32_t rd32(const void* p, uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t*>(p) + offset, 4);
    return v;
}

bool is_weapon(int8_t x) { return uint32_t(uint8_t(x)) - 1u < 0x47u; }
bool is_gadget(int8_t x) { return uint32_t(uint8_t(x)) - 0x4au < 0x15u; }

// A escolha que esperou (weapon_wheel.h: kPendingSeconds).
int8_t g_pending = -1;
float g_pending_time = 0.0f;

// A imagem do dossiê de cada classe (weapon_wheel.h). Os nomes do HUD (weapon_data +0x38) e os do dossiê nem sempre
// são iguais ("Frinesi Auto 12" / "Frinesi Automatic 12", "Winter Tactical Sniper" / "Tactical Sniper Rifle"): a
// tabela é do port, e o nf_loader_check confere que cada par divide uma palavra do nome nos dados do jogo. Sem imagem no
// dossiê: o soco (1), a Kowloon Type 80 (0x0c), a Delta Repeater (0x11), o lança-torpedos (0x30: o do dossiê, "V12
// Torpedo Launcher", é o do carro), a Q-Pen (0x41), o chapéu de Oddjob (0x45) e a pistola de sinalização (0x46).
struct IconRow {
    uint8_t klass;
    uint8_t table;  // WheelIconRef::table
    uint8_t index;
};
constexpr IconRow kIconRows[] = {
    {0x02, 3, 0}, {0x04, 3, 1}, {0x06, 3, 2}, {0x08, 3, 3},  // as pistolas pelo nível de melhoria (kUpgraded)
    {0x0a, 1, 2},  {0x0d, 1, 2},                             // Kowloon Type 40
    {0x0e, 1, 3},  {0x10, 1, 3},                             // Raptor Magnum (e a .50)
    {0x12, 1, 5},  {0x14, 1, 4},  {0x16, 1, 6},  {0x19, 1, 6}, {0x1a, 1, 7}, {0x1c, 1, 8},
    {0x1e, 1, 10}, {0x20, 1, 10}, {0x22, 1, 10},             // Tactical Sniper (as melhorias)
    {0x24, 1, 9},  {0x26, 1, 9},  {0x28, 1, 9},              // Covert Sniper
    {0x2a, 1, 11}, {0x2c, 1, 12}, {0x2e, 1, 13}, {0x33, 1, 14},
    {0x34, 1, 15}, {0x35, 1, 17}, {0x36, 1, 16}, {0x37, 1, 19}, {0x3a, 1, 20}, {0x3b, 1, 18},
    {0x42, 1, 1},                                            // Golden Gun
    {0x43, 2, 8},  {0x44, 2, 8},                             // Korsakov K5 (o dossiê o põe entre os aparelhos)
    {0x4a, 2, 0},  {0x4c, 2, 0},  {0x4e, 2, 1},  {0x4f, 2, 1}, {0x50, 2, 2}, {0x51, 2, 2},
    {0x52, 2, 7},  {0x53, 2, 7},  {0x54, 2, 3},  {0x56, 2, 4}, {0x57, 2, 4}, {0x58, 2, 5},
    {0x59, 3, 4},  {0x5b, 3, 4},                             // o barbeador (a imagem de P_DSGADGETS_Handler)
};
// As imagens da pistola por nível de melhoria (as constantes de P_DSWEAPONS_Handler, 0x00217038: Wolfram PP7, a PP7
// melhorada, Wolfram P2K, P2K de ouro), na ordem de kUpgraded (as classes 2, 4, 6, 8).
constexpr uint32_t kPistolImages[4] = {0x30000f3, 0x300014f, 0x30000f2, 0x3000150};

uint32_t fixed_image(int index) {
    if (index >= 0 && index < 4) return kPistolImages[index];
    // O barbeador: P_DSGADGETS_Handler troca a imagem pela frequência do vídeo (a do PAL, 50 Hz, é a da tabela).
    if (index == 4) return VIDEO_FRAME_RATE == 0x32 ? 0x30000e5u : 0x300018fu;
    return 0;
}

}  // namespace

WheelIconRef weapon_wheel_icon_ref(int klass) {
    for (const IconRow& r : kIconRows) {
        if (r.klass == klass) return WheelIconRef{r.table, r.index};
    }
    return WheelIconRef{};
}

uint32_t weapon_wheel_icon(int klass) {
    const WheelIconRef r = weapon_wheel_icon_ref(klass);
    switch (r.table) {
        case 1: return r.index < int(std::size(ds_weapons)) ? ds_weapons[r.index].image : 0;
        case 2: return r.index < int(std::size(ds_gadgets)) ? ds_gadgets[r.index].image : 0;
        case 3: return fixed_image(r.index);
        default: return 0;
    }
}

int weapon_wheel_icon_hashes(uint32_t* out, int max) {
    int n = 0;
    const auto add = [&](uint32_t h) {
        if (h == 0) return;
        for (int i = 0; i < n; ++i) {
            if (out[i] == h) return;
        }
        if (n < max) out[n++] = h;
    };
    for (const IconRow& r : kIconRows) add(weapon_wheel_icon(r.klass));
    // As duas imagens do barbeador (a de 50 e a de 60 Hz) e as quatro da pistola.
    add(0x30000e5u);
    add(0x300018fu);
    for (const uint32_t h : kPistolImages) add(h);
    return n;
}

int weapon_wheel_ring_slots(int count, bool gadgets) {
    if (gadgets) return count <= 6 ? 6 : count <= 8 ? 8 : count <= 12 ? 12 : 16;
    return count <= 8 ? 8 : count <= 12 ? 12 : count <= 16 ? 16 : 24;
}

int weapon_wheel_slots(obj_tag* player, bool gadgets, WheelSlot* out, int max) {
    if (player == nullptr || player->data == nullptr || player->sphere_src == nullptr) return 0;
    BLData* b = bloke(player);
    const ANIMOBJ_tag* anim = anim_of(player);
    // As faixas do ciclo de Player_WeaponChange: grupo 0 as armas (1 a 0x47), grupo 1 os aparelhos (0x4a a 0x5e). Na
    // festa da 07000007 (o canal 0x48 desligado) o jogo só deixa o soco (o grupo 2).
    int32_t lo = 1, hi = 0x47;
    if (gadgets) {
        lo = 0x4a;
        hi = 0x5f;
    } else if (GameState.level_file == 0x7000007 && switch_channels[0x48] == 0) {
        hi = 2;
    }
    const int8_t in_hand = anim->next_weapon != anim->weapon ? anim->next_weapon : anim->weapon;
    int n = 0;
    for (int32_t c = lo; c < hi && n < max; ++c) {
        const weapon_definition_tag* cd = weapon_def(c);
        if (cd->selectable != 1) continue;
        const PLRWEAPON_tag& s = b->weapons[c];
        if (s.have == 0) continue;
        // A variante lembrada da classe (como o ciclo: a 0x54 sem ela); sem munição nela, a classe.
        int8_t pick = int8_t(c != 0x54 ? c + s.variant : c);
        bool usable = Player_WeaponHasAmmo(b, pick);
        if (!usable && 0 < s.variant && Player_WeaponHasAmmo(b, int16_t(c))) {
            pick = int8_t(c);
            usable = true;
        }
        WheelSlot& w = out[n++];
        w = WheelSlot{};
        w.weapon = pick;
        w.klass = int8_t(c);
        w.gadget = gadgets;
        w.usable = usable;
        w.current = uint8_t(in_hand) < kWeaponSlots && weapon_def(in_hand)->klass == c;
        const weapon_definition_tag* d = weapon_def(pick);
        w.uses_ammo = d->ammo_type != 0;
        w.clip = Player_AmmoInGun(b, pick);
        w.stored = b->ammo[d->ammo_type];
        w.name_label = rd32(d, 0x38);
        const uint8_t mode = b->weapons[pick].mode;
        w.mode_label = d->modes > 1 && mode < 3 ? rd32(d, 0x30 + uint32_t(mode) * 4) : 0;
        if (w.mode_label == 0xffffffffu) w.mode_label = 0;
        if (w.name_label == 0xffffffffu) w.name_label = 0;
        w.icon = weapon_wheel_icon(c);
    }
    return n;
}

bool weapon_wheel_can_open(obj_tag* player) {
    if (player == nullptr || player->data == nullptr || player->sphere_src == nullptr) return false;
    if (player->type != 3) return false;  // 0x12: morto
    const BLData* b = static_cast<const BLData*>(player->data);
    if (b->player != 0 || b->active != 1 || b->gun == nullptr || b->cam_mode != 0 || b->frozen != 0) return false;
    if (int16_t(player->state) != 1 || GS_IsPaused(0) || MenuManager_GetStatus() != 0 || ScriptCam != 0) return false;
    if (psiInput_Modal()) return false;
    return true;
}

bool weapon_wheel_can_select(obj_tag* player) {
    if (player == nullptr || player->data == nullptr || player->sphere_src == nullptr) return false;
    const BLData* b = static_cast<const BLData*>(player->data);
    if (b->gun == nullptr) return false;
    // As travas de Player_WeaponChange.
    const int16_t sub = int16_t(player->unknown_f6);
    if (sub == 2 || sub == 10 || sub == 12 || sub == 13 || sub == 16) return false;
    const uint16_t gs = b->gun->state;
    if (gs == 1 || gs == 3 || (5 <= gs && gs <= 9) || gs == 11 || (13 <= gs && gs <= 16)) return false;
    const ANIMOBJ_tag* anim = anim_of(player);
    const int8_t w = anim->weapon;
    if (w == kWeaponNone) return false;
    if (kWeaponNone < w && w < 0x5f && 0x5c < w) return false;
    return (anim->flags & 1) == 0;
}

bool weapon_wheel_select(obj_tag* player, int8_t weapon) {
    if (!weapon_wheel_can_select(player)) return false;
    BLData* b = bloke(player);
    ANIMOBJ_tag* anim = anim_of(player);
    // Só uma arma que o jogador tem e pode usar (uma escolha esperando pode atravessar uma troca de fase).
    if (uint8_t(weapon) >= kWeaponSlots || b->weapons[weapon_def(weapon)->klass].have == 0 ||
        !Player_WeaponHasAmmo(b, weapon)) {
        return false;
    }
    const int8_t w = anim->weapon;
    if (weapon == w) {
        anim->next_weapon = w;  // a da mão: desfaz uma troca pedida e ainda não começada
        return true;
    }
    // A memória do ciclo (Player_WeaponChange): a arma ou o aparelho que sai, para a troca rápida.
    if (is_weapon(w)) b->last_weapon = w;
    if (is_gadget(w)) b->last_gadget = int16_t(w == 0x55 ? 0x54 : w);
    anim->prev_weapon = w;
    anim->next_weapon = weapon;
    // A variante escolhida fica lembrada na classe, como na troca de variante do jogo.
    const int16_t k = weapon_def(weapon)->klass;
    if (k != 0x54) b->weapons[k].variant = int8_t(weapon - k);
    return true;
}

float weapon_wheel_slot_angle(int i, int n) {
    if (n <= 0) return 0.0f;
    return 2.0f * kPi * float(i) / float(n);
}

float weapon_wheel_item_angle(int i, int count, bool gadgets) {
    return weapon_wheel_slot_angle(i, weapon_wheel_ring_slots(count, gadgets));
}

WheelPick weapon_wheel_pick(float x, float y, int n_outer, int n_inner) {
    WheelPick p;
    const float r = std::sqrt(x * x + y * y);
    if (r <= kWheelDeadZone || (n_outer <= 0 && n_inner <= 0)) return p;
    int ring;
    if (n_inner <= 0) {
        ring = 2;
    } else if (n_outer <= 0) {
        ring = 1;
    } else {
        ring = r < kWheelInnerEdge ? 1 : 2;
    }
    // Os espaços fixos do anel (os vazios também: apontar um deles não escolhe nada).
    const int n = weapon_wheel_ring_slots(ring == 2 ? n_outer : n_inner, ring == 1);
    // O ângulo a partir de cima, no sentido horário (x para a direita, y para cima).
    float a = nf::fm::atan2(x, y);
    if (a < 0.0f) a += 2.0f * kPi;
    int i = int(std::floor(a / (2.0f * kPi) * float(n) + 0.5f));
    if (i >= n) i -= n;
    p.ring = ring;
    p.index = i;
    return p;
}

void WeaponWheel::reset() {
    *this = WeaponWheel{};
    g_pending = -1;
    g_pending_time = 0.0f;
}

void WeaponWheel::refresh(obj_tag* player) {
    n_weapons_ = weapon_wheel_slots(player, false, weapons_, kWheelMaxWeapons);
    n_gadgets_ = weapon_wheel_slots(player, true, gadgets_, kWheelMaxGadgets);
}

void WeaponWheel::frame(float dt, bool held, float mouse_dx, float mouse_dy, float stick_x, float stick_y,
                        obj_tag* player) {
    const bool can = weapon_wheel_can_open(player);
    if (!open_) {
        if (held && !was_held_ && can) {
            open_ = true;
            ++opens_;
            sel_x_ = sel_y_ = 0.0f;
            stick_peak_ = 0.0f;
            hover_ = WheelPick{};
            refresh(player);
        }
    } else if (!can) {
        open_ = false;  // um menu, uma cena, a morte: fecha sem escolher
    } else {
        refresh(player);
        // O mouse: o movimento anda o vetor (y da tela para baixo); o analógico direito aponta direto. Ao voltar ao meio,
        // o analógico deixa o vetor onde estava (o espaço apontado fica); o anel é o do maior empurrão desde que saiu do
        // meio (todo empurrado: o de fora).
        sel_x_ += mouse_dx / kMouseCounts;
        sel_y_ -= mouse_dy / kMouseCounts;
        const float sm = std::sqrt(stick_x * stick_x + stick_y * stick_y);
        if (sm > 0.3f) {
            stick_peak_ = std::max(stick_peak_, sm);
            const float len = stick_peak_ >= 0.85f ? 1.0f : 0.4f;
            sel_x_ = stick_x / sm * len;
            sel_y_ = stick_y / sm * len;
        } else {
            stick_peak_ = 0.0f;
        }
        const float r = std::sqrt(sel_x_ * sel_x_ + sel_y_ * sel_y_);
        if (r > 1.0f) {
            sel_x_ /= r;
            sel_y_ /= r;
        }
        hover_ = weapon_wheel_pick(sel_x_, sel_y_, n_weapons_, n_gadgets_);
        if (!held) {
            open_ = false;
            const WheelSlot* s = nullptr;
            if (hover_.ring == 2 && hover_.index < n_weapons_) s = &weapons_[hover_.index];
            if (hover_.ring == 1 && hover_.index < n_gadgets_) s = &gadgets_[hover_.index];
            if (s != nullptr && s->usable && !s->current) {
                g_pending = s->weapon;
                g_pending_time = kPendingSeconds;
            }
        }
    }
    was_held_ = held;
    if (g_pending >= 0) {
        if (player != nullptr && weapon_wheel_select(player, g_pending)) {
            last_choice_ = g_pending;
            ++choices_;
            g_pending = -1;
        } else if ((g_pending_time -= dt) <= 0.0f || player == nullptr) {
            g_pending = -1;
        }
    }
    const float step = dt / (open_ ? kOpenSeconds : kCloseSeconds);
    shown_ = open_ ? std::min(1.0f, shown_ + step) : std::max(0.0f, shown_ - step);
}

int WeaponWheel::describe(char* out, unsigned size) const {
    int icons = 0;
    for (int i = 0; i < n_weapons_; ++i) icons += weapons_[i].icon != 0;
    for (int i = 0; i < n_gadgets_; ++i) icons += gadgets_[i].icon != 0;
    int len = std::snprintf(out, size, "roda aberta %d mostrada %.2f armas %d aparelhos %d espacos %d/%d imagens %d "
                                       "apontado %d:%d vetor %.2f %.2f escolhas %u ultima %d aberturas %u |",
                            open_ ? 1 : 0, double(shown_), n_weapons_, n_gadgets_, weapon_wheel_ring_slots(n_weapons_, false),
                            weapon_wheel_ring_slots(n_gadgets_, true), icons, hover_.ring, hover_.index, double(sel_x_),
                            double(sel_y_), choices_, int(last_choice_), opens_);
    // Os espaços: a arma de cada um (as com imagem marcadas com *).
    for (int g = 0; g < 2 && len > 0 && unsigned(len) < size; ++g) {
        const WheelSlot* list = g == 0 ? weapons_ : gadgets_;
        const int n = g == 0 ? n_weapons_ : n_gadgets_;
        for (int i = 0; i < n && unsigned(len) < size; ++i) {
            len += std::snprintf(out + len, size - unsigned(len), " %02x%s", unsigned(uint8_t(list[i].weapon)),
                                 list[i].icon != 0 ? "*" : "");
        }
        if (g == 0 && unsigned(len) < size) len += std::snprintf(out + len, size - unsigned(len), " /");
    }
    return len;
}
