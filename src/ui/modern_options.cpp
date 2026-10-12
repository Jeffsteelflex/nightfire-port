// A tela de opções do PC no estilo Modern. Ver modern_options.h e docs/design.md.

#include "ui/modern_options.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include "engine/input.h"
#include "engine/menu.h"
#include "engine/txt.h"
#include "platform/game_options.h"
#include "platform/gamepad.h"
#include "platform/input_bindings.h"
#include "platform/pc_gameplay.h"
#include "platform/psi_input.h"
#include "platform/psi_pad.h"
#include "platform/fmath.h"

namespace nf::ui {

namespace {

constexpr int kTopPause = 0, kTopOptions = 1;
// As ações dos comandos da barra de baixo (PromptBar::Prompt::action).
enum : int { kActBack = 1, kActSelect, kActDefaults, kActApply, kActCancel, kActClear };

const PcText kCatLabel[kCatCount] = {PcText::MdCatVideo,    PcText::MdCatGraphics,      PcText::MdCatInterface,
                                     PcText::MdCatGameplay, PcText::MdCatAudio,         PcText::MdCatControls,
                                     PcText::MdCatMouseKeyboard, PcText::MdCatController};
const char* const kCatNames[kCatCount] = {"video", "graphics", "interface", "gameplay", "audio", "controls", "keys", "controller"};

// As opções do jogo (platform/game_options.h) de cada linha; GameOption::Count nas do PC.
nf::GameOption game_opt(Opt o) {
    switch (o) {
        case Opt::AutoAim: return nf::GameOption::AutoAim;
        case Opt::Crosshairs: return nf::GameOption::Crosshairs;
        case Opt::AimMode: return nf::GameOption::AimToggle;
        case Opt::CrouchMode: return nf::GameOption::CrouchToggle;
        case Opt::AutoSwitch: return nf::GameOption::AutoSwitch;
        case Opt::Flashing: return nf::GameOption::FlashingObjects;
        case Opt::HudAlways: return nf::GameOption::HudAlwaysOn;
        case Opt::Subtitles: return nf::GameOption::Subtitles;
        case Opt::Music: return nf::GameOption::MusicVolume;
        case Opt::Effects: return nf::GameOption::EffectsVolume;
        case Opt::Vibration: return nf::GameOption::Vibration;
        default: return nf::GameOption::Count;
    }
}

// Muda uma opção do jogo no lugar dele e grava (nf::persist_controls anota a mudança no .ini antes, com a regra de
// platform/game_options.h; sem g_controls_persist nada vai para o arquivo, e o game_options_sync do tick seguinte só
// atualiza a referência).
void set_game(nf::GameOption o, int32_t v) {
    nf::set_game_option(o, v);
    nf::persist_controls();
}

// As legendas do "Restore Defaults" da AV Options do jogo (P_CNAVOPTIONS_Handler): desligadas no inglês, alemão,
// francês, espanhol e na versão americana; ligadas nas outras línguas.
int32_t default_subtitles() { return LanguageToLoad < 0 || (3 < LanguageToLoad && LanguageToLoad != 6) ? 1 : 0; }

std::wstring T(PcText id) { return widen(PcText_Utf8(id)); }

std::wstring fmt(PcText id, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, id);
    std::vsnprintf(buf, sizeof(buf), PcText_Utf8(id), ap);
    va_end(ap);
    return widen(buf);
}

bool english() { return PcText_Language() == "en"; }

PcText desc_of(Opt o) {
    switch (o) {
        case Opt::Resolution: return PcText::MdDescResolution;
        case Opt::WindowMode: return PcText::MdDescWindowMode;
        case Opt::VSync: return PcText::MdDescVSync;
        case Opt::Fov: return PcText::MdDescFov;
        case Opt::RenderScale: return PcText::MdDescRenderScale;
        case Opt::AntiAliasing: return PcText::MdDescAntiAliasing;
        case Opt::Filter: return PcText::MdDescFilter;
        case Opt::HdTextures: return PcText::MdDescHdTextures;
        case Opt::Dlss: return PcText::MdDescDlss;
        case Opt::HdUi: return PcText::MdDescHdUi;
        case Opt::HudAnchor: return PcText::MdDescHudAnchor;
        case Opt::MenuStyle: return PcText::MdDescMenuStyle;
        case Opt::Language: return PcText::MdDescLanguage;
        case Opt::Music: return PcText::MdDescMusic;
        case Opt::Effects: return PcText::MdDescEffects;
        case Opt::Device: return PcText::MdDescDevice;
        case Opt::ButtonLayout: return PcText::MdDescButtonLayout;
        case Opt::InvertMouse: return PcText::MdDescInvertMouse;
        case Opt::InvertPad: return PcText::MdDescInvertPad;
        case Opt::MouseSens: return PcText::MdDescMouseSens;
        case Opt::AimSens: return PcText::MdDescAimSens;
        case Opt::PadSens: return PcText::MdDescPadSens;
        case Opt::Deadzone: return PcText::MdDescDeadzone;
        case Opt::Triggers: return PcText::MdDescTriggers;
        case Opt::Apply: return PcText::MdDescApply;
        case Opt::RestoreKeys: return PcText::MdDescRestoreKeys;
        case Opt::KeyRow: return PcText::MdDescKeys;
        case Opt::FpsCap: return PcText::MdDescFpsCap;
        case Opt::Interpolation: return PcText::MdDescInterpolation;
        case Opt::InstantMouse: return PcText::MdDescInstantMouse;
        case Opt::Adapter: return PcText::MdDescAdapter;
        case Opt::AimStyle: return PcText::MdDescAimStyle;
        case Opt::WheelSlowmo: return PcText::MdDescWheelSlowmo;
        default: return PcText::Count;
    }
}

const nf::TextureFilter kFilters[] = {nf::TextureFilter::Bilinear, nf::TextureFilter::Trilinear, nf::TextureFilter::Aniso2,
                                      nf::TextureFilter::Aniso4,   nf::TextureFilter::Aniso8,    nf::TextureFilter::Aniso16};

// O glifo SVG (hd_ui/svg/xbox_*.svg) de uma entrada do controle; vazio se não houver.
std::string glyph_of(nf::InputCode c) {
    if (!c.is_pad()) return {};
    switch (c.code) {
        case nf::kPadInA: return "xbox_a";
        case nf::kPadInB: return "xbox_b";
        case nf::kPadInX: return "xbox_x";
        case nf::kPadInY: return "xbox_y";
        case nf::kPadInLB: return "xbox_lb";
        case nf::kPadInRB: return "xbox_rb";
        case nf::kPadInLT: return "xbox_lt";
        case nf::kPadInRT: return "xbox_rt";
        case nf::kPadInLThumb: return "xbox_ls";
        case nf::kPadInRThumb: return "xbox_rs";
        case nf::kPadInStart: return "xbox_menu";
        case nf::kPadInBack: return "xbox_view";
        case nf::kPadInDpadUp:
        case nf::kPadInDpadDown: return "xbox_dpad_v";
        case nf::kPadInDpadLeft:
        case nf::kPadInDpadRight: return "xbox_dpad_h";
        default: return {};
    }
}

float stick(int16_t v) { return std::clamp(float(v) / 32767.0f, -1.0f, 1.0f); }

}  // namespace

const char* cat_name(Cat c) { return int(c) < kCatCount ? kCatNames[int(c)] : ""; }

bool parse_cat(const std::string& name, Cat* out) {
    for (int i = 0; i < kCatCount; ++i) {
        if (name == kCatNames[i]) {
            *out = Cat(i);
            return true;
        }
    }
    // O mouse está em CONTROLS; "bindings" e o nome antigo da categoria das teclas abrem KEY BINDINGS.
    if (name == "mouse") {
        *out = Cat::Controls;
        return true;
    }
    if (name == "bindings" || name == "mouse_keyboard") {
        *out = Cat::MouseKeyboard;
        return true;
    }
    return false;
}

OptionsScreen::OptionsScreen(const Tokens* tokens) : tk_(tokens != nullptr ? tokens : &default_tokens()) {}

// ---------------------------------------------------------------- abrir e fechar

void OptionsScreen::open(Cat cat) {
    active_ = true;
    closing_ = false;
    back_leaves_ = false;
    close_delay_ = -1.0f;
    time_ = 0.0f;
    appear_.snap(0.0f);
    appear_.set(1.0f, tk_->motion.open, tk_->motion.ease_out);
    pending_ = nf::video_controller().live();
    tabs_ = TabBar{};
    tabs_.active = kTopOptions;
    cats_ = CategoryList{};
    dialog_ = Dialog{};
    dialog_kind_ = DialogKind::None;
    toast_ = Toast{};
    mark_ = FocusMark{};
    capture_cmd_ = -1;
    swallow_ = true;
    for (bool& h : held_) h = false;
    drag_slider_ = -1;
    hover_cat_ = -1;
    mouse_active_ = false;
    prev_lmb_ = prev_back_ = true;  // o clique que abriu a tela não conta
    zone_ = Zone::Categories;
    cat_ = Cat::Count;
    set_category(cat, false);
    zone_ = Zone::Content;
    set_focus(first_focusable(), false);
    if (focus_ < 0) zone_ = Zone::Categories;
    content_.snap(1.0f);
    laid_out_ = false;
}

void OptionsScreen::open_at(Cat cat, bool back_leaves) {
    open(int(cat) < kCatCount ? cat : Cat::Video);
    back_leaves_ = back_leaves;
}

void OptionsScreen::leave() {
    if (embedded_) host_back_ = true;
    else request_close();
}

void OptionsScreen::request_close() {
    if (!active_ || closing_) return;
    if (capture_cmd_ >= 0) {
        psiInput_EndCapture();
        capture_cmd_ = -1;
    }
    closing_ = true;
    appear_.set(0.0f, tk_->motion.close, tk_->motion.ease_in);
}

void OptionsScreen::force_close() {
    if (capture_cmd_ >= 0) psiInput_EndCapture();
    capture_cmd_ = -1;
    if (nf::video_controller().confirming()) nf::video_controller().revert();
    active_ = false;
    closing_ = false;
}

float OptionsScreen::background() const { return active_ ? appear_.value() : 0.0f; }

// ---------------------------------------------------------------- conteúdo

void OptionsScreen::build_items() {
    items_.clear();
    const auto sec = [&](PcText t) { items_.push_back(Item{Kind::Section, Opt::None, t}); };
    const auto row = [&](Kind k, Opt o, PcText t) { items_.push_back(Item{k, o, t}); };
    switch (cat_) {
        case Cat::Video:
            sec(PcText::MdSecDisplay);
            row(Kind::Dropdown, Opt::Resolution, PcText::Resolution);
            row(Kind::Dropdown, Opt::WindowMode, PcText::WindowMode);
            row(Kind::Toggle, Opt::VSync, PcText::VSync);
            // O adaptador de vídeo (platform/gpu_adapter.h): guardado com o Apply, vale na próxima partida.
            row(Kind::Dropdown, Opt::Adapter, PcText::GraphicsAdapter);
            sec(PcText::MdSecView);
            row(Kind::Slider, Opt::Fov, PcText::MdFieldOfView);
            // O ritmo dos quadros (render/frame_interp.h): o limite, a interpolação entre os ticks e o mouse sem atraso.
            sec(PcText::MdSecMotion);
            row(Kind::Dropdown, Opt::FpsCap, PcText::FpsCap);
            row(Kind::Toggle, Opt::Interpolation, PcText::FrameInterpolation);
            row(Kind::Toggle, Opt::InstantMouse, PcText::InstantMouse);
            row(Kind::Action, Opt::Apply, PcText::MdApply);
            break;
        case Cat::Graphics:
            sec(PcText::MdSecQuality);
            row(Kind::Dropdown, Opt::RenderScale, PcText::RenderScale);
            row(Kind::Dropdown, Opt::AntiAliasing, PcText::AntiAliasing);
            row(Kind::Dropdown, Opt::Filter, PcText::TextureFilter);
            sec(PcText::MdSecTextures);
            row(Kind::Toggle, Opt::HdTextures, PcText::HdTextures);
            sec(PcText::MdSecUpscaling);
            row(Kind::Locked, Opt::Dlss, PcText::Dlss);
            row(Kind::Action, Opt::Apply, PcText::MdApply);
            break;
        case Cat::Interface:
            sec(PcText::MdSecHud);
            row(Kind::Toggle, Opt::HdUi, PcText::HdInterface);
            row(Kind::Dropdown, Opt::HudAnchor, PcText::HudAnchor);
            sec(PcText::MdSecText);
            row(Kind::Toggle, Opt::Subtitles, PcText::MdSubtitles);  // a do jogo: vale na hora, sem o Apply
            sec(PcText::MdSecMenus);
            row(Kind::Dropdown, Opt::MenuStyle, PcText::MenuStyle);
            row(Kind::Dropdown, Opt::Language, PcText::Language);
            row(Kind::Action, Opt::Apply, PcText::MdApply);
            break;
        case Cat::Gameplay:
            // As da "Advanced Options" do jogo, menos a vibração (em CONTROLLER).
            sec(PcText::MdSecAiming);
            row(Kind::Toggle, Opt::AutoAim, PcText::MdAutoAim);
            row(Kind::Toggle, Opt::Crosshairs, PcText::MdCrosshairs);
            row(Kind::Dropdown, Opt::AimMode, PcText::MdManualAim);
            row(Kind::Dropdown, Opt::AimStyle, PcText::MdAimStyle);  // só do PC: a mira moderna (player_ads.h)
            sec(PcText::MdSecMovement);
            row(Kind::Dropdown, Opt::CrouchMode, PcText::MdCrouch);
            sec(PcText::MdSecItemsHud);
            row(Kind::Toggle, Opt::AutoSwitch, PcText::MdAutoSwitch);
            row(Kind::Toggle, Opt::Flashing, PcText::MdFlashing);
            row(Kind::Toggle, Opt::HudAlways, PcText::MdHudAlways);
            // Só do PC: a roda de armas (engine/weapon_wheel.h).
            sec(PcText::MdSecWheel);
            row(Kind::Dropdown, Opt::WheelSlowmo, PcText::MdWheelSlowmo);
            break;
        case Cat::Audio:
            // As barras da "AV Options" do jogo (o modo do som não aparece: no PC não há o que escolher).
            sec(PcText::MdSecVolume);
            row(Kind::Slider, Opt::Music, PcText::AudioMusic);
            row(Kind::Slider, Opt::Effects, PcText::AudioEffects);
            break;
        case Cat::Controls:
            // O aparelho e o mouse (a sensibilidade, a da mira e a inversão; o jogador procura aqui). As do controle
            // ficam em CONTROLLER, as teclas em KEY BINDINGS: nada repetido.
            sec(PcText::MdSecInput);
            row(Kind::Info, Opt::Device, PcText::MdDevice);
            sec(PcText::MdSecMouse);
            row(Kind::Slider, Opt::MouseSens, PcText::MdMouseSens);
            row(Kind::Slider, Opt::AimSens, PcText::MdAimSens);
            row(Kind::Toggle, Opt::InvertMouse, PcText::CtlMouseInvert);
            break;
        case Cat::MouseKeyboard:
            // KEY BINDINGS: a tabela de teclas (o mouse ficou em CONTROLS).
            sec(PcText::MdSecBindings);
            items_.push_back(Item{Kind::TableHead, Opt::None, PcText::Count});
            for (int c = 0; c < nf::kBindCommandCount; ++c) {
                Item it{Kind::KeyRow, Opt::KeyRow, PcText::Count};
                it.command = c;
                items_.push_back(it);
            }
            row(Kind::Action, Opt::RestoreKeys, PcText::MdRestoreKeys);
            break;
        case Cat::Controller:
            sec(PcText::MdSecSticks);
            row(Kind::Slider, Opt::PadSens, PcText::MdPadSens);
            row(Kind::Slider, Opt::Deadzone, PcText::MdDeadzone);
            row(Kind::Toggle, Opt::InvertPad, PcText::CtlPadInvert);
            sec(PcText::MdSecButtons);
            row(Kind::Dropdown, Opt::ButtonLayout, PcText::MdButtonLayout);
            row(Kind::Toggle, Opt::Triggers, PcText::MdTriggers);
            row(Kind::Toggle, Opt::Vibration, PcText::CtlVibration);
            break;
        default:
            break;
    }
    anims_.assign(items_.size(), ItemAnim{});
    for (size_t i = 0; i < items_.size(); ++i) {
        ItemAnim& a = anims_[i];
        const Item& it = items_[i];
        a.label.snap(0.0f);
        a.change.snap(1.0f);
        if (it.kind == Kind::Toggle) a.knob.snap(toggle_on(it.opt) ? 1.0f : 0.0f);
        if (it.kind == Kind::Slider || it.kind == Kind::LockedSlider) {
            const SliderSpec s = slider(it.opt);
            a.fill.snap(s.hi > s.lo ? float(s.pos - s.lo) / float(s.hi - s.lo) : 0.0f);
        }
        a.shown_value = it.kind == Kind::KeyRow ? std::wstring() : value_text(it.opt);
    }
    dd_item_ = -1;
    dd_closing_ = -1;
    dd_open_.snap(0.0f);
    layout_items();
}

void OptionsScreen::layout_items() {
    const auto& sp = tk_->space;
    float y = 0.0f;
    const int anim_item = dd_item_ >= 0 ? dd_item_ : dd_closing_;
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& it = items_[i];
        switch (it.kind) {
            case Kind::Section: it.h = sp.section_h; break;
            case Kind::TableHead: it.h = sp.table_head_h; break;
            case Kind::KeyRow: it.h = sp.key_row_h; break;
            default: it.h = sp.row_h; break;
        }
        it.y = y;
        y += it.h;
        if (int(i) == anim_item) y += sp.option_h * float(choice_count(it.opt)) * dd_open_.value();
    }
}

bool OptionsScreen::focusable(int i) const {
    if (i < 0 || size_t(i) >= items_.size()) return false;
    const Kind k = items_[size_t(i)].kind;
    return k != Kind::Section && k != Kind::TableHead;
}

int OptionsScreen::first_focusable() const {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (focusable(int(i))) return int(i);
    }
    return -1;
}

int OptionsScreen::step_focus(int from, int dir) const {
    for (int i = from + dir; i >= 0 && size_t(i) < items_.size(); i += dir) {
        if (focusable(i)) return i;
    }
    return -1;
}

float OptionsScreen::content_height() const {
    if (items_.empty()) return 0.0f;
    const Item& last = items_.back();
    float h = last.y + last.h;
    const int anim_item = dd_item_ >= 0 ? dd_item_ : dd_closing_;
    if (anim_item == int(items_.size()) - 1) h += tk_->space.option_h * float(choice_count(last.opt)) * dd_open_.value();
    return h;
}

float OptionsScreen::view_height() const { return tk_->space.list_bottom - tk_->space.list_top; }

float OptionsScreen::row_screen_y(int i) const {
    if (i < 0 || size_t(i) >= items_.size()) return 0.0f;
    return tk_->space.list_top + items_[size_t(i)].y - scroll_.value();
}

Rect OptionsScreen::item_rect(int i) const {
    if (i < 0 || size_t(i) >= items_.size()) return Rect{};
    const float y = row_screen_y(i);
    return Rect{tk_->space.mid_x0, y, tk_->space.mid_x1, y + items_[size_t(i)].h};
}

Rect OptionsScreen::option_rect(int option) const {
    const int at = dd_item_ >= 0 ? dd_item_ : dd_closing_;
    if (at < 0) return Rect{};
    const Rect r = item_rect(at);
    const float y = r.y1 + tk_->space.option_h * float(option);
    return Rect{tk_->space.mid_x0 + tk_->space.option_indent, y, tk_->space.mid_x1, y + tk_->space.option_h};
}

Rect OptionsScreen::category_rect(int c) const {
    if (c < 0 || size_t(c) >= cats_.rects.size()) {
        const float cy = tk_->space.cat_y + float(c) * tk_->space.cat_step;
        return Rect{tk_->space.marker_x - 16.0f, cy - tk_->space.cat_step * 0.5f, tk_->space.mid_x0 - 60.0f,
                    cy + tk_->space.cat_step * 0.5f};
    }
    return cats_.rects[size_t(c)];
}

namespace {
// Os centros das colunas da tabela de teclas (PRIMARY, SECONDARY, CONTROLLER), a partir da direita da coluna.
float key_col_centre(const Tokens& tk, int col) { return tk.space.mid_x1 - tk.space.pad_r - 70.0f - float(2 - col) * 136.0f; }
constexpr float kKeyColHalf = 64.0f;
}  // namespace

Rect OptionsScreen::underline_of(int i) const {
    // Em coordenadas da lista (y sem a rolagem e sem o topo): x0, x1 e o y do sublinhado em y1.
    const auto& sp = tk_->space;
    if (i < 0 || size_t(i) >= items_.size()) return Rect{};
    const Item& it = items_[size_t(i)];
    if (i == dd_item_) {
        const float y = it.y + it.h + sp.option_h * float(dd_focus_ + 1);
        return Rect{sp.mid_x0 + sp.option_indent, y, sp.mid_x1 - 8.0f, y};
    }
    if (it.kind == Kind::KeyRow) {
        const float cx = key_col_centre(*tk_, col_);
        return Rect{cx - kKeyColHalf, it.y + it.h, cx + kKeyColHalf, it.y + it.h};
    }
    return Rect{sp.mid_x0, it.y + it.h, sp.mid_x1 - 8.0f, it.y + it.h};
}

void OptionsScreen::place_mark(bool animate) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    if (zone_ != Zone::Content || focus_ < 0) {
        mark_.show(false, f);
        return;
    }
    const Rect u = underline_of(focus_);
    mark_.move_to(u.x0, u.x1, u.y1 - 1.0f, f, animate);
    mark_.show(true, f);
}

void OptionsScreen::ensure_visible(int i) {
    if (i < 0 || size_t(i) >= items_.size()) return;
    const Item& it = items_[size_t(i)];
    float y0 = it.y, y1 = it.y + it.h;
    if (i == dd_item_) {
        y0 = it.y + it.h + tk_->space.option_h * float(dd_focus_);
        y1 = y0 + tk_->space.option_h;
    }
    // A seção logo acima vem junto quando é a primeira linha dela.
    if (i > 0 && items_[size_t(i) - 1].kind == Kind::Section && i != dd_item_) y0 = items_[size_t(i) - 1].y;
    const float view = view_height();
    const float margin = 24.0f;
    float t = scroll_target_;
    if (y0 - margin < t) t = y0 - margin;
    if (y1 + margin > t + view) t = y1 + margin - view;
    const float total = content_height();
    // Com a lista aberta, a altura final (a animação ainda vai crescer).
    float final_total = total;
    if (dd_item_ >= 0) final_total = total + tk_->space.option_h * float(choice_count(items_[size_t(dd_item_)].opt)) * (1.0f - dd_open_.value());
    t = std::clamp(t, 0.0f, std::max(0.0f, final_total - view));
    if (t != scroll_target_) {
        scroll_target_ = t;
        scroll_.set(t, tk_->motion.scroll, tk_->motion.ease_out);
    }
}

void OptionsScreen::set_focus(int i, bool animate) {
    if (i < 0) {
        focus_ = -1;
        place_mark(animate);
        return;
    }
    focus_ = i;
    if (items_[size_t(i)].kind != Kind::KeyRow) col_ = 0;
    ensure_visible(i);
    place_mark(animate);
}

void OptionsScreen::set_category(Cat c, bool animate) {
    if (c == cat_ || int(c) >= kCatCount) return;
    cat_ = c;
    cats_.selected = int(c);
    build_items();
    scroll_target_ = 0.0f;
    scroll_.snap(0.0f);
    focus_ = -1;
    col_ = 0;
    mark_.placed = false;
    mark_.a.snap(0.0f);
    if (animate) {
        content_.snap(0.0f);
        content_.set(1.0f, tk_->motion.content, tk_->motion.ease_out);
    }
    if (zone_ == Zone::Content) set_focus(first_focusable(), false);
}

// ---------------------------------------------------------------- modelo

bool OptionsScreen::is_pending_opt(Opt o) const {
    switch (o) {
        case Opt::Resolution: case Opt::WindowMode: case Opt::VSync: case Opt::Fov: case Opt::RenderScale:
        case Opt::AntiAliasing: case Opt::Filter: case Opt::HdTextures: case Opt::HdUi: case Opt::HudAnchor:
        case Opt::MenuStyle: case Opt::Language: case Opt::FpsCap: case Opt::Interpolation: case Opt::InstantMouse:
        case Opt::Adapter:
            return true;
        default:
            return false;
    }
}

bool OptionsScreen::pending_changes() const { return !(pending_ == nf::video_controller().live()); }

PcText OptionsScreen::desc_for(Opt o) const {
    // O texto de ajuda do jogo muda com o valor (Page_SetHelpText: help_table por id e valor).
    const bool on = nf::game_option(game_opt(o)) != 0;
    switch (o) {
        case Opt::AutoAim: return on ? PcText::MdDescAutoAimOn : PcText::MdDescAutoAimOff;
        case Opt::Crosshairs: return on ? PcText::MdDescCrosshairsOn : PcText::MdDescCrosshairsOff;
        case Opt::AimMode: return on ? PcText::MdDescManualAimToggle : PcText::MdDescManualAimHold;
        case Opt::CrouchMode: return on ? PcText::MdDescCrouchToggle : PcText::MdDescCrouchHold;
        case Opt::AutoSwitch: return on ? PcText::MdDescAutoSwitchOn : PcText::MdDescAutoSwitchOff;
        case Opt::Flashing: return on ? PcText::MdDescFlashingOn : PcText::MdDescFlashingOff;
        case Opt::HudAlways: return on ? PcText::MdDescHudAlwaysOn : PcText::MdDescHudAlwaysOff;
        case Opt::Subtitles: return on ? PcText::MdDescSubtitlesOn : PcText::MdDescSubtitlesOff;
        case Opt::Vibration: return on ? PcText::MdDescVibrationOn : PcText::MdDescVibrationOff;
        default: return desc_of(o);
    }
}

OptionsScreen::Choice OptionsScreen::choices(Opt o) const {
    Choice c;
    const nf::VideoController& vc = nf::video_controller();
    const nf::VideoSettings& p = pending_;
    switch (o) {
        case Opt::Resolution: {
            const nf::DisplayMode cur{p.width, p.height};
            std::vector<nf::DisplayMode> modes = nf::resolution_choices(vc.display_modes(), vc.desktop(), p.window_mode, cur);
            if (modes.empty()) modes.push_back(cur);
            for (size_t i = 0; i < modes.size(); ++i) {
                char buf[64];
                if (p.window_mode == nf::WindowMode::Borderless && vc.desktop().width > 0) {
                    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::Desktop), vc.desktop().width, vc.desktop().height);
                } else {
                    std::snprintf(buf, sizeof(buf), "%d \xC3\x97 %d", modes[i].width, modes[i].height);
                }
                c.labels.push_back(widen(buf));
                if (modes[i] == cur) c.index = int(i);
            }
            break;
        }
        case Opt::WindowMode:
            c.labels = {T(PcText::ModeWindowed), T(PcText::ModeBorderless), T(PcText::ModeExclusive)};
            c.index = int(p.window_mode);
            break;
        case Opt::RenderScale: {
            const std::vector<int> v = nf::render_scale_values(p.render_scale);
            for (size_t i = 0; i < v.size(); ++i) {
                c.labels.push_back(std::to_wstring(v[i]) + L"%");
                if (v[i] == p.render_scale) c.index = int(i);
            }
            break;
        }
        case Opt::AntiAliasing: {
            const std::vector<int> v = nf::msaa_values(vc.msaa_support(), p.msaa);
            for (size_t i = 0; i < v.size(); ++i) {
                c.labels.push_back(v[i] <= 1 ? T(PcText::Off) : fmt(PcText::Msaa, v[i]));
                if (v[i] == p.msaa) c.index = int(i);
            }
            break;
        }
        case Opt::Filter:
            for (int i = 0; i < 6; ++i) {
                const nf::TextureFilter f = kFilters[i];
                if (f == nf::TextureFilter::Bilinear) c.labels.push_back(T(PcText::FilterBilinear));
                else if (f == nf::TextureFilter::Trilinear) c.labels.push_back(T(PcText::FilterTrilinear));
                else c.labels.push_back(fmt(PcText::FilterAniso, nf::texture_filter_anisotropy(f)));
                if (f == p.filter) c.index = i;
            }
            break;
        case Opt::HudAnchor:
            c.labels = {T(PcText::AnchorCentre), T(PcText::AnchorEdges)};
            c.index = p.hud_edges ? 1 : 0;
            break;
        case Opt::FpsCap: {
            const std::vector<int> v = nf::fps_cap_values(p.fps_cap);
            for (size_t i = 0; i < v.size(); ++i) {
                c.labels.push_back(v[i] <= 0 ? T(PcText::Off) : fmt(PcText::FpsCapValue, v[i]));
                if (v[i] == p.fps_cap) c.index = int(i);
            }
            break;
        }
        case Opt::Adapter: {
            // auto, integrated, discrete e um por placa (o nome; platform/video_settings.h, adapter_choice_values).
            const std::vector<std::string> v = nf::adapter_choice_values(vc.adapters(), p.adapter);
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == "auto") c.labels.push_back(T(PcText::AdapterAuto));
                else if (v[i] == "integrated") c.labels.push_back(T(PcText::AdapterIntegrated));
                else if (v[i] == "discrete") c.labels.push_back(T(PcText::AdapterDiscrete));
                else c.labels.push_back(widen(v[i]));
                if (v[i] == p.adapter) c.index = int(i);
            }
            break;
        }
        case Opt::MenuStyle:
            c.labels = {T(PcText::StyleModern), T(PcText::StyleOriginal)};
            c.index = p.menu_style == nf::MenuStyle::Original ? 1 : 0;
            break;
        case Opt::Language:
            c.labels = {widen(PcText_Utf8(PcText::LanguageName, "en")), widen(PcText_Utf8(PcText::LanguageName, "pt"))};
            c.index = p.language == "pt" ? 1 : 0;
            break;
        case Opt::ButtonLayout:
            for (int i = 0; i < 8; ++i) c.labels.push_back(widen(nf::kPadStyleNames[i]));
            c.index = std::clamp(int(PlayerSetting[0].style), 0, 7);
            break;
        case Opt::AimMode:
        case Opt::CrouchMode:
            // Na ordem da página do jogo: "Toggle" (1), "Hold" (0).
            c.labels = {T(PcText::MdModeToggle), T(PcText::MdModeHold)};
            c.index = nf::game_option(game_opt(o)) != 0 ? 0 : 1;
            break;
        case Opt::AimStyle:
            c.labels = {T(PcText::AimStyleAuto), T(PcText::AimStyleModern), T(PcText::AimStyleClassic)};
            c.index = std::clamp(int(nf::pc_gameplay().aim_style), 0, 2);
            break;
        case Opt::WheelSlowmo:
            for (int i = 0; i < nf::kWheelSlowmoChoices; ++i) {
                const float v = nf::kWheelSlowmoValues[i];
                if (v <= 0.0f) c.labels.push_back(T(PcText::WheelSlowmoPause));
                else if (v >= 1.0f) c.labels.push_back(T(PcText::WheelSlowmoOff));
                else c.labels.push_back(fmt(PcText::WheelSlowmoPercent, int(std::lround(v * 100.0f))));
            }
            c.index = nf::wheel_slowmo_index(nf::pc_gameplay().wheel_slowmo);
            break;
        default:
            break;
    }
    return c;
}

int OptionsScreen::choice_index(Opt o) const { return choices(o).index; }
int OptionsScreen::choice_count(Opt o) const { return int(choices(o).labels.size()); }

void OptionsScreen::set_choice(Opt o, int i) {
    const Choice c = choices(o);
    if (c.labels.empty()) return;
    i = std::clamp(i, 0, int(c.labels.size()) - 1);
    const nf::VideoController& vc = nf::video_controller();
    nf::VideoSettings& p = pending_;
    switch (o) {
        case Opt::Resolution: {
            const nf::DisplayMode cur{p.width, p.height};
            std::vector<nf::DisplayMode> modes = nf::resolution_choices(vc.display_modes(), vc.desktop(), p.window_mode, cur);
            if (size_t(i) < modes.size()) {
                p.width = modes[size_t(i)].width;
                p.height = modes[size_t(i)].height;
            }
            break;
        }
        case Opt::WindowMode:
            p.window_mode = nf::WindowMode(i);
            nf::fit_resolution_to_mode(p, vc.display_modes(), vc.desktop());
            break;
        case Opt::RenderScale: {
            const std::vector<int> v = nf::render_scale_values(p.render_scale);
            if (size_t(i) < v.size()) p.render_scale = v[size_t(i)];
            break;
        }
        case Opt::AntiAliasing: {
            const std::vector<int> v = nf::msaa_values(vc.msaa_support(), p.msaa);
            if (size_t(i) < v.size()) p.msaa = v[size_t(i)];
            break;
        }
        case Opt::Filter: p.filter = kFilters[i]; break;
        case Opt::HudAnchor: p.hud_edges = i == 1; break;
        case Opt::FpsCap: {
            const std::vector<int> v = nf::fps_cap_values(p.fps_cap);
            if (size_t(i) < v.size()) p.fps_cap = v[size_t(i)];
            break;
        }
        case Opt::MenuStyle: p.menu_style = i == 1 ? nf::MenuStyle::Original : nf::MenuStyle::Modern; break;
        case Opt::Language: p.language = i == 1 ? "pt" : "en"; break;
        case Opt::Adapter: {
            const std::vector<std::string> v = nf::adapter_choice_values(vc.adapters(), p.adapter);
            if (size_t(i) < v.size()) p.adapter = v[size_t(i)];
            break;
        }
        case Opt::ButtonLayout:
            // O esquema do jogo; mudado aqui, o .ini passa a lembrá-lo (pad_style, platform/game_options.h).
            if (int(PlayerSetting[0].style) != i) {
                Menu_ChangeControllerStyle(0, uint32_t(i));
                nf::persist_controls();
            }
            break;
        case Opt::AimMode:
        case Opt::CrouchMode:
            if (nf::game_option(game_opt(o)) != (i == 0 ? 1 : 0)) set_game(game_opt(o), i == 0 ? 1 : 0);
            break;
        case Opt::AimStyle:
            // Só do PC, na hora ([game] aim_style).
            if (int(nf::pc_gameplay().aim_style) != i) {
                nf::pc_gameplay().aim_style = uint8_t(i);
                nf::persist_pc_gameplay();
            }
            break;
        case Opt::WheelSlowmo:
            if (nf::wheel_slowmo_index(nf::pc_gameplay().wheel_slowmo) != i) {
                nf::pc_gameplay().wheel_slowmo = nf::kWheelSlowmoValues[i];
                nf::persist_pc_gameplay();
            }
            break;
        default: break;
    }
}

bool OptionsScreen::toggle_on(Opt o) const {
    const nf::ControlsConfig& cfg = nf::controls();
    switch (o) {
        case Opt::VSync: return pending_.vsync;
        case Opt::HdTextures: return pending_.hd_textures;
        case Opt::HdUi: return pending_.hd_ui;
        case Opt::Interpolation: return pending_.interpolation;
        case Opt::InstantMouse: return pending_.instant_mouse_look;
        case Opt::InvertMouse: return cfg.mouse_invert_y;
        case Opt::InvertPad: return PlayerSetting[0].invert_look != 0;
        case Opt::Triggers: return cfg.pad_triggers;
        case Opt::Vibration:
        case Opt::AutoAim:
        case Opt::Crosshairs:
        case Opt::AutoSwitch:
        case Opt::Flashing:
        case Opt::HudAlways:
        case Opt::Subtitles:
            return nf::game_option(game_opt(o)) != 0;
        default: return false;
    }
}

void OptionsScreen::set_toggle(Opt o, bool on) {
    nf::ControlsConfig& cfg = nf::controls();
    PlayerInput_tag& pl = PlayerSetting[0];
    switch (o) {
        case Opt::VSync: pending_.vsync = on; return;
        case Opt::HdTextures: pending_.hd_textures = on; return;
        case Opt::HdUi: pending_.hd_ui = on; return;
        case Opt::Interpolation: pending_.interpolation = on; return;
        case Opt::InstantMouse: pending_.instant_mouse_look = on; return;
        case Opt::InvertMouse: cfg.mouse_invert_y = on; break;
        case Opt::InvertPad:
            pl.invert_look = on ? 1 : 0;
            cfg.pad_invert_y = on;
            break;
        case Opt::Triggers: cfg.pad_triggers = on; break;
        case Opt::Vibration:
            pl.unknown_09 = on ? 1 : 0;
            cfg.vibration = on;
            if (!on) Input_RumbleStop(0);
            break;
        case Opt::AutoAim:
        case Opt::Crosshairs:
        case Opt::AutoSwitch:
        case Opt::Flashing:
        case Opt::HudAlways:
        case Opt::Subtitles:
            // As do jogo: no lugar dele (o jogo as lê a cada tick: valem na missão em andamento).
            if (toggle_on(o) != on) nf::set_game_option(game_opt(o), on ? 1 : 0);
            break;
        default: return;
    }
    nf::persist_controls();
}

OptionsScreen::SliderSpec OptionsScreen::slider(Opt o) const {
    SliderSpec s;
    const nf::ControlsConfig& cfg = nf::controls();
    char buf[64] = {};
    switch (o) {
        case Opt::Fov: {
            s.lo = 0;
            s.hi = 12;
            const int fov = int(std::lround(pending_.fov));
            s.pos = std::clamp((fov - 40 + 2) / 5, 0, 12);
            std::snprintf(buf, sizeof(buf), "%d\xC2\xB0", fov);
            break;
        }
        case Opt::MouseSens:
            // Logarítmica (nf::mouse_sens_to_pos): 9% por passo, de 0,05 a 8,80; o valor x 1000.
            s.lo = nf::kMouseSensPosMin;
            s.hi = nf::kMouseSensPosMax;
            s.pos = nf::mouse_sens_to_pos(psiMouseSensitivity);
            std::snprintf(buf, sizeof(buf), "%.2f", double(psiMouseSensitivity * 1000.0f));
            break;
        case Opt::AimSens:
            s.lo = 2;
            s.hi = 40;
            s.pos = std::clamp(int32_t(cfg.mouse_aim_scale * 20.0f + 0.5f), 2, 40);
            std::snprintf(buf, sizeof(buf), "%.2fx", double(cfg.mouse_aim_scale));
            break;
        case Opt::PadSens:
            s.lo = 1;
            s.hi = 30;
            s.pos = std::clamp(int32_t(cfg.pad_sensitivity * 10.0f + 0.5f), 1, 30);
            std::snprintf(buf, sizeof(buf), "%.1fx", double(cfg.pad_sensitivity));
            break;
        case Opt::Deadzone:
            s.lo = 0;
            s.hi = 30;
            s.pos = std::clamp(int32_t(cfg.pad_deadzone * 127.0f / 2.0f + 0.5f), 0, 30);
            std::snprintf(buf, sizeof(buf), "%d%%", int(cfg.pad_deadzone * 100.0f + 0.5f));
            break;
        case Opt::Music:
        case Opt::Effects: {
            // As barras da "AV Options" do jogo: 0 a 20, o volume (0-100) / 5.
            const int32_t v = nf::game_option(game_opt(o));
            s.lo = 0;
            s.hi = 20;
            s.pos = std::clamp((v + 2) / 5, 0, 20);
            std::snprintf(buf, sizeof(buf), "%d%%", int(v));
            break;
        }
        default:
            break;
    }
    s.text = widen(buf);
    return s;
}

int OptionsScreen::slider_pos(Opt o) const { return slider(o).pos; }

void OptionsScreen::set_slider(Opt o, int pos) {
    const SliderSpec s = slider(o);
    pos = std::clamp(pos, s.lo, s.hi);
    if (pos == s.pos) return;
    nf::ControlsConfig& cfg = nf::controls();
    switch (o) {
        case Opt::Fov: pending_.fov = float(40 + 5 * pos); return;
        case Opt::MouseSens: psiMouseSensitivity = nf::mouse_sens_from_pos(pos); break;
        case Opt::AimSens: cfg.mouse_aim_scale = float(pos) * 0.05f; break;
        case Opt::PadSens: cfg.pad_sensitivity = float(pos) * 0.1f; break;
        case Opt::Deadzone: cfg.pad_deadzone = float(pos * 2) / 127.0f; break;
        case Opt::Music:
        case Opt::Effects: set_game(game_opt(o), pos * 5); return;
        default: return;
    }
    nf::persist_controls();
}

std::wstring OptionsScreen::device_text() const {
    const int pad = psiInput_PadIndex();
    if (pad < 0) return T(PcText::CtlKeyboardMouse);
    return fmt(psiInput_ActiveDevice() == 1 ? PcText::CtlPad : PcText::CtlKeyboardPad, pad + 1);
}

std::wstring OptionsScreen::value_text(Opt o) const {
    switch (o) {
        case Opt::VSync: case Opt::HdTextures: case Opt::HdUi: case Opt::InvertMouse: case Opt::InvertPad:
        case Opt::Triggers: case Opt::Vibration: case Opt::AutoAim: case Opt::Crosshairs: case Opt::AutoSwitch:
        case Opt::Flashing: case Opt::HudAlways: case Opt::Subtitles: case Opt::Interpolation: case Opt::InstantMouse:
            return T(toggle_on(o) ? PcText::On : PcText::Off);
        case Opt::Fov: case Opt::MouseSens: case Opt::AimSens: case Opt::PadSens: case Opt::Deadzone: case Opt::Music:
        case Opt::Effects:
            return slider(o).text;
        case Opt::Device: return device_text();
        case Opt::Dlss: return T(PcText::ComingSoon);
        case Opt::Apply:
        case Opt::RestoreKeys: return std::wstring();
        default: {
            const Choice c = choices(o);
            if (c.index >= 0 && size_t(c.index) < c.labels.size()) return c.labels[size_t(c.index)];
            return std::wstring();
        }
    }
}

std::wstring OptionsScreen::key_cell(int command, int col) const {
    const nf::Bindings& b = nf::controls().bindings;
    nf::InputCode in[nf::kMaxBinds];
    if (col < 2) {
        const int n = b.list(command, false, in, nf::kMaxBinds);
        if (col < n) return widen(nf::input_label(in[col], english()));
        return T(PcText::MdNone);
    }
    const int n = b.list(command, true, in, nf::kMaxBinds);
    if (n > 0) return widen(nf::input_label(in[0], english()));
    // Sem entrada na tabela: o que o esquema de botões do jogo faz (entre parênteses).
    const int action = nf::pad_style_action(command);
    if (action < 0) return T(PcText::MdNone);
    const uint32_t bits = psi_pad_style_inputs(PlayerSetting[0].style, action, nf::kBindCommands[command].targets[0].sign,
                                               nf::controls().pad_triggers);
    std::string s;
    for (uint8_t code = 0; code < nf::kPadInCount; ++code) {
        if ((bits & (1u << code)) == 0) continue;
        if (!s.empty()) s += " ";
        s += nf::input_label(nf::pad_input(code), english());
    }
    return s.empty() ? T(PcText::MdNone) : widen("(" + s + ")");
}

void OptionsScreen::apply() {
    nf::VideoController& vc = nf::video_controller();
    const nf::MenuStyle before = vc.live().menu_style;
    const std::string adapter_before = vc.live().adapter;
    const bool confirm = vc.apply(pending_);
    PcText_SetLanguage(vc.live().language);
    pending_ = vc.live();
    if (confirm) {
        const Frame f{*tk_, last_cv_, Measure{}, time_};
        dialog_.show(T(PcText::MdKeepTitle), fmt(PcText::MdKeepBody, int(std::ceil(vc.remaining()))),
                     {T(PcText::MdKeep), T(PcText::MdRevert)}, 0, f);
        dialog_.countdown = 1.0f;
        dialog_kind_ = DialogKind::Keep;
    } else if (vc.live().adapter != adapter_before) {
        show_toast(PcText::MdToastRestart);
    } else if (vc.live().menu_style != before) {
        show_toast(PcText::MdToastStyle);
    } else {
        show_toast(PcText::MdToastApplied);
    }
}

void OptionsScreen::restore_defaults() {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const nf::VideoSettings d{};
    switch (cat_) {
        case Cat::Video:
            pending_.vsync = d.vsync;
            pending_.fov = d.fov;
            pending_.fps_cap = d.fps_cap;
            pending_.interpolation = d.interpolation;
            pending_.instant_mouse_look = d.instant_mouse_look;
            pending_.adapter = d.adapter;
            show_toast(PcText::MdToastDefaults);
            return;
        case Cat::Graphics:
            pending_.render_scale = d.render_scale;
            pending_.msaa = d.msaa;
            pending_.filter = d.filter;
            pending_.hd_textures = d.hd_textures;
            show_toast(PcText::MdToastDefaults);
            return;
        case Cat::Interface:
            pending_.hd_ui = d.hd_ui;
            pending_.hud_edges = d.hud_edges;
            pending_.menu_style = d.menu_style;
            pending_.language = d.language;
            // As legendas são do jogo e valem na hora: o padrão do "Restore Defaults" da AV Options.
            if (nf::game_option(nf::GameOption::Subtitles) != default_subtitles()) {
                set_game(nf::GameOption::Subtitles, default_subtitles());
            }
            show_toast(PcText::MdToastDefaults);
            return;
        case Cat::Gameplay:
        case Cat::Audio:
        case Cat::Controls:
        case Cat::MouseKeyboard:
        case Cat::Controller: {
            const std::wstring name = T(kCatLabel[int(cat_)]);
            char body[256];
            std::snprintf(body, sizeof(body), PcText_Utf8(PcText::MdDefaultsBody), PcText_Utf8(kCatLabel[int(cat_)]));
            dialog_.show(T(PcText::MdDefaultsTitle), widen(body), {T(PcText::MdRestore), T(PcText::MdCancel)}, 1, f);
            dialog_kind_ = DialogKind::Defaults;
            (void)name;
            return;
        }
        default:
            return;
    }
}

// ---------------------------------------------------------------- ações

void OptionsScreen::command(Cmd c) { act(c); }

void OptionsScreen::show_toast(PcText id) { toast_.show(T(id)); }
void OptionsScreen::show_toast(const std::wstring& s) { toast_.show(s); }

void OptionsScreen::choose_tab(int t) {
    if (t < 0 || t > kTopOptions || t == tabs_.active) return;
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    tabs_.set_active(t, f, true);
    if (t == kTopPause) close_delay_ = tk_->motion.tab * 0.6f;  // o sublinhado desliza e a tela sai
}

void OptionsScreen::act(Cmd c) {
    if (!active_ || closing_ || capture_cmd_ >= 0) return;
    if (dialog_.open) {
        act_dialog(c);
        return;
    }
    switch (c) {
        case Cmd::TabPrev:
        case Cmd::TabNext:
            // Embutida na pausa: a aba é dela.
            if (embedded_) host_tab_ = c == Cmd::TabPrev ? -1 : 1;
            else choose_tab(tabs_.active + (c == Cmd::TabPrev ? -1 : 1));
            return;
        case Cmd::Defaults:
            if (dd_item_ < 0) restore_defaults();
            return;
        case Cmd::Apply:
            if (pending_changes()) apply();
            return;
        default: break;
    }
    if (zone_ == Zone::Categories) {
        switch (c) {
            case Cmd::Up:
                if (int(cat_) > 0) set_category(Cat(int(cat_) - 1));
                break;
            case Cmd::Down:
                if (int(cat_) + 1 < kCatCount) set_category(Cat(int(cat_) + 1));
                break;
            case Cmd::Right:
            case Cmd::Select:
                if (first_focusable() >= 0) {
                    zone_ = Zone::Content;
                    set_focus(first_focusable(), false);
                }
                break;
            case Cmd::Back:
                leave();
                break;
            default: break;
        }
        return;
    }
    act_content(c);
}

void OptionsScreen::act_dropdown(Cmd c) {
    const int n = choice_count(items_[size_t(dd_item_)].opt);
    switch (c) {
        case Cmd::Up:
            if (dd_focus_ > 0) --dd_focus_;
            ensure_visible(dd_item_);
            place_mark(true);
            break;
        case Cmd::Down:
            if (dd_focus_ + 1 < n) ++dd_focus_;
            ensure_visible(dd_item_);
            place_mark(true);
            break;
        case Cmd::Select:
            set_choice(items_[size_t(dd_item_)].opt, dd_focus_);
            close_dropdown();
            break;
        case Cmd::Back:
        case Cmd::Left:
            close_dropdown();
            break;
        default: break;
    }
}

void OptionsScreen::act_content(Cmd c) {
    if (dd_item_ >= 0) {
        act_dropdown(c);
        return;
    }
    if (focus_ < 0) {
        zone_ = Zone::Categories;
        return;
    }
    const Item& it = items_[size_t(focus_)];
    switch (c) {
        case Cmd::Up: {
            const int to = step_focus(focus_, -1);
            if (to >= 0) set_focus(to);
            break;
        }
        case Cmd::Down: {
            const int to = step_focus(focus_, 1);
            if (to >= 0) set_focus(to);
            break;
        }
        case Cmd::Left:
        case Cmd::Right: {
            const int dir = c == Cmd::Left ? -1 : 1;
            switch (it.kind) {
                case Kind::Dropdown: set_choice(it.opt, choice_index(it.opt) + dir); break;
                case Kind::Toggle: set_toggle(it.opt, dir > 0); break;
                case Kind::Slider: set_slider(it.opt, slider_pos(it.opt) + dir); break;
                case Kind::KeyRow:
                    if (col_ + dir < 0) {
                        zone_ = Zone::Categories;
                        place_mark(true);
                    } else {
                        col_ = std::min(2, col_ + dir);
                        place_mark(true);
                    }
                    break;
                default:
                    if (dir < 0) {
                        zone_ = Zone::Categories;
                        place_mark(true);
                    }
                    break;
            }
            break;
        }
        case Cmd::Select: activate(focus_); break;
        case Cmd::Back:
            // Aberta por um deep link (open_at), voltar das linhas sai de vez: quem veio pela categoria volta.
            if (back_leaves_) {
                leave();
                break;
            }
            zone_ = Zone::Categories;
            place_mark(true);
            break;
        default: break;
    }
}

void OptionsScreen::act_dialog(Cmd c) {
    const int n = int(dialog_.buttons.size());
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const auto move = [&](int dir) {
        dialog_.focus = std::clamp(dialog_.focus + dir, 0, n - 1);
        const Rect& r = dialog_.button_rects[size_t(dialog_.focus)];
        dialog_.mark.move_to(r.x0, r.x1, r.y1, f, true);
    };
    nf::VideoController& vc = nf::video_controller();
    switch (c) {
        case Cmd::Left:
        case Cmd::Up: move(-1); break;
        case Cmd::Right:
        case Cmd::Down: move(1); break;
        case Cmd::Back:
        case Cmd::Select: {
            const int choice = c == Cmd::Back ? 1 : dialog_.focus;
            dialog_.hide(f);
            if (dialog_kind_ == DialogKind::Keep) {
                if (choice == 0 && vc.confirming()) {
                    vc.keep();
                    show_toast(PcText::MdToastKept);
                } else if (vc.confirming()) {
                    vc.revert();
                    show_toast(PcText::MdToastReverted);
                }
                PcText_SetLanguage(vc.live().language);
                pending_ = vc.live();
            } else if (dialog_kind_ == DialogKind::Defaults && choice == 0) {
                nf::ControlsConfig& cfg = nf::controls();
                PlayerInput_tag& pl = PlayerSetting[0];
                const nf::ControlsConfig d{};
                if (cat_ == Cat::Controls) {
                    // O mouse.
                    psiMouseSensitivity = nf::kDefaultMouseSensitivity;
                    cfg.mouse_aim_scale = d.mouse_aim_scale;
                    cfg.mouse_invert_y = d.mouse_invert_y;
                } else if (cat_ == Cat::MouseKeyboard) {
                    cfg.bindings.reset();
                } else if (cat_ == Cat::Controller) {
                    cfg.pad_sensitivity = d.pad_sensitivity;
                    cfg.pad_deadzone = d.pad_deadzone;
                    cfg.pad_invert_y = d.pad_invert_y;
                    pl.invert_look = d.pad_invert_y ? 1 : 0;
                    cfg.pad_triggers = d.pad_triggers;
                    cfg.vibration = d.vibration;
                    pl.unknown_09 = d.vibration ? 1 : 0;
                    // O esquema do perfil padrão do jogo (def_codename: 7, Classic Bond), não o 0 do boot.
                    uint16_t style = 0;
                    std::memcpy(&style, def_codename + 0x20, 2);
                    style = uint16_t(std::min<int>(style, 7));
                    if (int(pl.style) != int(style)) Menu_ChangeControllerStyle(0, style);
                } else if (cat_ == Cat::Gameplay) {
                    // Os do perfil padrão do jogo (def_codename, como Menu_MapDefaultCodename os põe).
                    const uint8_t* dc = def_codename;
                    pl.autoaim = dc[0x24];
                    pl.unknown_08 = dc[0x29];
                    pl.aim_toggle = dc[0x26];
                    pl.crouch_toggle = dc[0x27];
                    pl.auto_switch = dc[0x2a];
                    pl.unknown_0c = dc[0x2b];
                    pl.unknown_0b = dc[0x2c];
                    // As só do PC: os padrões delas (auto, 25%).
                    nf::pc_gameplay().aim_style = 0;
                    nf::pc_gameplay().wheel_slowmo = nf::kDefaultWheelSlowmo;
                    nf::persist_pc_gameplay();
                } else if (cat_ == Cat::Audio) {
                    // Os do "Restore Defaults" da AV Options do jogo (barras 14 e 20 de 20).
                    nf::set_game_option(nf::GameOption::MusicVolume, 14 * 5);
                    nf::set_game_option(nf::GameOption::EffectsVolume, 20 * 5);
                }
                nf::persist_controls();
                show_toast(PcText::MdToastDefaults);
            }
            dialog_kind_ = DialogKind::None;
            break;
        }
        default: break;
    }
}

void OptionsScreen::activate(int i) {
    if (i < 0 || size_t(i) >= items_.size()) return;
    const Item& it = items_[size_t(i)];
    switch (it.kind) {
        case Kind::Dropdown: open_dropdown_at(i); break;
        case Kind::Toggle: set_toggle(it.opt, !toggle_on(it.opt)); break;
        case Kind::Action:
            if (it.opt == Opt::Apply && pending_changes()) apply();
            if (it.opt == Opt::RestoreKeys) restore_defaults();
            break;
        case Kind::KeyRow: begin_capture(i, col_); break;
        default: break;
    }
}

void OptionsScreen::open_dropdown_at(int i) {
    if (dd_item_ == i) {
        close_dropdown();
        return;
    }
    dd_closing_ = -1;
    dd_item_ = i;
    dd_focus_ = std::max(0, choice_index(items_[size_t(i)].opt));
    dd_open_.snap(0.0f);
    dd_open_.set(1.0f, tk_->motion.dropdown, tk_->motion.ease_out);
    focus_ = i;
    layout_items();
    ensure_visible(i);
    place_mark(true);
}

void OptionsScreen::close_dropdown() {
    if (dd_item_ < 0) return;
    dd_closing_ = dd_item_;
    dd_item_ = -1;
    dd_open_.set(0.0f, tk_->motion.dropdown, tk_->motion.ease_out);
    place_mark(true);
    ensure_visible(focus_);
}

void OptionsScreen::begin_capture(int i, int col) {
    const Item& it = items_[size_t(i)];
    if (it.command < 0) return;
    capture_cmd_ = it.command;
    capture_col_ = col;
    psiInput_BeginCapture();
}

void OptionsScreen::finish_capture() {
    const PsiCapture state = psiInput_CaptureState();
    if (state == PsiCapture::Waiting) return;
    const int command = capture_cmd_;
    const char* name = english() ? nf::kBindCommands[command].label_en : nf::kBindCommands[command].label_pt;
    nf::ControlsConfig& cfg = nf::controls();
    char buf[256] = {};
    nf::InputCode code;
    if (state == PsiCapture::Cancelled || state == PsiCapture::Idle) {
        show_toast(PcText::KeysCancelled);
    } else if (state == PsiCapture::Clear) {
        if (nf::clear_bind_slot(cfg.bindings, command, capture_col_)) {
            std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysCleared), name);
            nf::persist_controls();
        } else {
            std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysRequired), name);
        }
        show_toast(widen(buf));
    } else if (psiInput_CaptureResult(&code)) {
        const bool want_pad = capture_col_ == 2;
        if (code.is_pad() != want_pad) {
            show_toast(want_pad ? PcText::MdWantPad : PcText::MdWantKey);
        } else {
            const std::string key = nf::input_label(code, english());
            const nf::BindResult r = nf::bind_slot(cfg.bindings, command, capture_col_, code);
            const auto label_of = [](int c) {
                return english() ? nf::kBindCommands[c].label_en : nf::kBindCommands[c].label_pt;
            };
            if (r.refused) {
                std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysRefused), key.c_str(),
                              r.moved_from >= 0 ? label_of(r.moved_from) : name);
            } else {
                if (r.swapped_to >= 0) {
                    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysSwapped), key.c_str(), name,
                                  label_of(r.swapped_to), nf::input_label(r.swapped, english()).c_str());
                } else if (r.moved_from >= 0) {
                    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysMoved), key.c_str(), name, label_of(r.moved_from));
                } else {
                    std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::KeysBound), key.c_str(), name);
                }
                nf::persist_controls();
            }
            show_toast(widen(buf));
        }
    }
    psiInput_EndCapture();
    capture_cmd_ = -1;
    swallow_ = true;
}

// ---------------------------------------------------------------- mouse

int OptionsScreen::hit_item(float x, float y) const {
    const auto& sp = tk_->space;
    if (x < sp.mid_x0 || x >= sp.mid_x1 || y < sp.list_top || y >= sp.list_bottom) return -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Rect r = item_rect(int(i));
        if (r.contains(x, y)) return int(i);
    }
    return -1;
}

int OptionsScreen::hit_option(float x, float y) const {
    if (dd_item_ < 0) return -1;
    const auto& sp = tk_->space;
    if (y < sp.list_top || y >= sp.list_bottom) return -1;
    const int n = choice_count(items_[size_t(dd_item_)].opt);
    for (int j = 0; j < n; ++j) {
        const Rect r = option_rect(j);
        if (Rect{sp.mid_x0, r.y0, r.x1, r.y1}.contains(x, y)) return j;
    }
    return -1;
}

int OptionsScreen::key_col_at(float x) const {
    for (int c = 0; c < 3; ++c) {
        const float cx = key_col_centre(*tk_, c);
        if (x >= cx - kKeyColHalf - 4.0f && x < cx + kKeyColHalf + 4.0f) return c;
    }
    return -1;
}

void OptionsScreen::slider_from_x(int i, float x) {
    const Frame f{*tk_, last_cv_, Measure{}, time_};
    const Item& it = items_[size_t(i)];
    const SliderSpec s = slider(it.opt);
    const Rect t = slider_track(f, item_rect(i));
    const float k = std::clamp((x - t.x0) / std::max(1.0f, t.w()), 0.0f, 1.0f);
    set_slider(it.opt, s.lo + int(std::lround(k * float(s.hi - s.lo))));
}

void OptionsScreen::mouse(const UiInput& in, const Canvas& cv) {
    const float x = canvas_x(cv, in.mouse_x), y = canvas_y(cv, in.mouse_y);
    const bool moved = in.mouse_inside && (std::fabs(x - last_mx_) > 0.5f || std::fabs(y - last_my_) > 0.5f);
    if (in.mouse_inside) {
        last_mx_ = x;
        last_my_ = y;
    }
    const bool pressed = in.lmb && !prev_lmb_;
    const bool back = in.back_button && !prev_back_;
    prev_lmb_ = in.lmb;
    prev_back_ = in.back_button;
    if (!in.lmb) drag_slider_ = -1;
    if (moved || pressed || back || in.wheel != 0.0f) mouse_active_ = true;
    if (!mouse_active_ || !in.mouse_inside) return;
    if (back) {
        act(Cmd::Back);
        return;
    }
    const Frame f{*tk_, cv, Measure{}, time_};
    if (dialog_.open) {
        const int h = dialog_.hit(x, y);
        dialog_.hover = h;
        if (h >= 0 && (moved || pressed) && h != dialog_.focus) {
            dialog_.focus = h;
            const Rect& r = dialog_.button_rects[size_t(h)];
            dialog_.mark.move_to(r.x0, r.x1, r.y1, f, true);
        }
        if (pressed && h >= 0) act_dialog(Cmd::Select);
        return;
    }
    if (drag_slider_ >= 0 && in.lmb) {
        slider_from_x(drag_slider_, x);
        return;
    }
    // A barra de baixo e a de cima.
    if (pressed) {
        switch (bar_.hit(x, y)) {
            case kActBack: act(Cmd::Back); return;
            case kActSelect: act(Cmd::Select); return;
            case kActDefaults: act(Cmd::Defaults); return;
            case kActApply: act(Cmd::Apply); return;
            default: break;
        }
        const int t = embedded_ ? -1 : tabs_.hit(x, y);
        if (t == -2) {
            act(Cmd::TabPrev);
            return;
        }
        if (t == -3) {
            act(Cmd::TabNext);
            return;
        }
        if (t >= 0) {
            choose_tab(t);
            return;
        }
    }
    // As categorias.
    const int c = cats_.hit(x, y);
    hover_cat_ = c;
    cats_.hover = c;
    if (c >= 0) {
        if (pressed) {
            close_dropdown();
            zone_ = Zone::Categories;
            set_category(Cat(c));
            place_mark(true);
        }
        return;
    }
    // A lista aberta.
    if (dd_item_ >= 0) {
        const int o = hit_option(x, y);
        if (o >= 0) {
            if (moved && o != dd_focus_) {
                dd_focus_ = o;
                place_mark(true);
            }
            if (pressed) {
                dd_focus_ = o;
                act_dropdown(Cmd::Select);
            }
            return;
        }
    }
    // As linhas.
    const int i = hit_item(x, y);
    if (in.wheel != 0.0f && x >= tk_->space.mid_x0 - 40.0f && x < tk_->space.mid_x1 + 40.0f) {
        const float view = view_height();
        scroll_target_ = std::clamp(scroll_target_ - in.wheel * tk_->space.row_h, 0.0f, std::max(0.0f, content_height() - view));
        scroll_.set(scroll_target_, tk_->motion.scroll, tk_->motion.ease_out);
    }
    if (i < 0 || !focusable(i)) {
        if (pressed && dd_item_ >= 0) close_dropdown();
        return;
    }
    const Item& it = items_[size_t(i)];
    if ((moved || pressed) && (i != focus_ || zone_ != Zone::Content) && dd_item_ < 0) {
        zone_ = Zone::Content;
        focus_ = i;
        if (it.kind != Kind::KeyRow) col_ = 0;
        place_mark(true);
    }
    if (it.kind == Kind::KeyRow && (moved || pressed) && dd_item_ < 0) {
        const int col = key_col_at(x);
        if (col >= 0 && col != col_) {
            col_ = col;
            place_mark(true);
        }
    }
    if (!pressed) return;
    if (dd_item_ >= 0 && i != dd_item_) {
        close_dropdown();
        return;
    }
    switch (it.kind) {
        case Kind::Slider: {
            const Rect t = slider_track(f, item_rect(i));
            if (x >= t.x0 - 16.0f) {
                slider_from_x(i, x);
                drag_slider_ = i;
            }
            break;
        }
        case Kind::KeyRow:
            if (key_col_at(x) >= 0) activate(i);
            break;
        default: activate(i); break;
    }
}

// ---------------------------------------------------------------- o quadro

void OptionsScreen::frame(float dt, const UiInput& in, const Canvas& cv, const Measure& m) {
    if (!active_) return;
    last_cv_ = cv;
    time_ += dt;
    appear_.update(dt);
    content_.update(dt);
    dd_open_.update(dt);
    scroll_.update(dt);
    mark_.update(dt);
    tabs_.update(dt);
    dialog_.update(dt);
    toast_.update(dt, *tk_);
    if (closing_) {
        if (appear_.done()) {
            active_ = false;
            closing_ = false;
        }
        return;
    }
    if (close_delay_ >= 0.0f) {
        close_delay_ -= dt;
        if (close_delay_ < 0.0f) {
            request_close();
            return;
        }
    }
    if (dd_item_ < 0 && dd_closing_ >= 0 && dd_open_.done()) dd_closing_ = -1;
    pad_device_ = in.device;

    // Os valores (a troca anima), as chaves, as barras e a cor dos nomes.
    for (size_t i = 0; i < items_.size(); ++i) {
        ItemAnim& a = anims_[i];
        const Item& it = items_[i];
        a.label.update(dt);
        a.knob.update(dt);
        a.fill.update(dt);
        a.change.update(dt);
        const bool lit = zone_ == Zone::Content && int(i) == focus_;
        a.label.set(lit ? 1.0f : 0.0f, tk_->motion.focus, tk_->motion.ease_out);
        if (it.kind == Kind::Toggle) a.knob.set(toggle_on(it.opt) ? 1.0f : 0.0f, tk_->motion.toggle, tk_->motion.ease_out);
        if (it.kind == Kind::Slider) {
            const SliderSpec s = slider(it.opt);
            a.fill.set(s.hi > s.lo ? float(s.pos - s.lo) / float(s.hi - s.lo) : 0.0f, 0.1f, tk_->motion.ease_out);
        }
        if (it.kind == Kind::Dropdown || it.kind == Kind::Info) {
            const std::wstring v = value_text(it.opt);
            if (v != a.shown_value) {
                a.old_value = a.shown_value;
                a.shown_value = v;
                a.change.snap(0.0f);
                a.change.set(1.0f, tk_->motion.value, tk_->motion.ease_out);
            }
        }
    }
    layout_items();

    // A barra de cima e a de baixo (os retângulos para o mouse).
    const Frame f{*tk_, cv, m, time_};
    const bool pad = in.device == 1;
    {
        const auto first_key = [&](const char* key) {
            const int c = nf::find_bind_command(key);
            nf::InputCode in_list[nf::kMaxBinds];
            if (c >= 0 && nf::controls().bindings.list(c, pad, in_list, nf::kMaxBinds) > 0) return in_list[0];
            return nf::InputCode{};
        };
        const nf::InputCode p = first_key("menu_tab_prev"), n = first_key("menu_tab_next");
        tabs_.glyph_prev = pad ? glyph_of(p) : std::string();
        tabs_.glyph_next = pad ? glyph_of(n) : std::string();
        tabs_.key_prev = p.valid() && !p.is_pad() ? widen(nf::input_label(p, true)) : L"Q";
        tabs_.key_next = n.valid() && !n.is_pad() ? widen(nf::input_label(n, true)) : L"E";
        if (pad && tabs_.glyph_prev.empty()) tabs_.glyph_prev = "xbox_lb";
        if (pad && tabs_.glyph_next.empty()) tabs_.glyph_next = "xbox_rb";
    }
    tabs_.labels = {T(PcText::MdTopPause), T(PcText::MdTopOptions)};
    tabs_.corner_left = L"NF.PC / SYS";
    tabs_.corner_right = L"SLUS-20579";
    tabs_.layout(f);
    if (!laid_out_) tabs_.set_active(tabs_.active, f, false);
    cats_.header = T(PcText::MdHeader);
    cats_.items.clear();
    for (int c = 0; c < kCatCount; ++c) cats_.items.push_back({T(kCatLabel[c]), false});
    cats_.selected = int(cat_);
    cats_.focused = zone_ == Zone::Categories;
    cats_.layout(f);
    prompts(f, bar_);
    bar_.layout(f);
    if (dialog_.open) dialog_.layout(f);
    if (!laid_out_) place_mark(false);
    laid_out_ = true;

    // A confirmação de vídeo conta o tempo; acabado, a anterior já voltou.
    nf::VideoController& vc = nf::video_controller();
    if (dialog_.open && dialog_kind_ == DialogKind::Keep) {
        if (vc.confirming()) {
            vc.tick(dt);
            dialog_.countdown = vc.remaining() / nf::kVideoConfirmSeconds;
            dialog_.body = fmt(PcText::MdKeepBody, int(std::ceil(vc.remaining())));
        }
        if (!vc.confirming()) {
            dialog_.hide(f);
            dialog_kind_ = DialogKind::None;
            PcText_SetLanguage(vc.live().language);
            pending_ = vc.live();
            if (vc.stats().reverted > 0) show_toast(PcText::MdToastReverted);
        }
    }

    // A captura de uma tecla: o resto da entrada espera.
    if (capture_cmd_ >= 0) {
        finish_capture();
        prev_lmb_ = in.lmb;
        prev_back_ = in.back_button;
        return;
    }

    // Teclado e controle: os comandos do menu da tabela de teclas, mais Enter, Esc, F e os botões do controle.
    nf::InputFrame frame_in;
    frame_in.keys = in.keys;
    frame_in.pad = in.pad;
    frame_in.pad_deadzone = 0.5f;
    const nf::Bindings& b = nf::controls().bindings;
    const auto cmd = [&](const char* key) {
        const int c = nf::find_bind_command(key);
        return c >= 0 && nf::command_value(b, c, frame_in) > 0.5f;
    };
    const bool* k = in.keys;
    const auto key = [&](int vk) { return k != nullptr && k[vk]; };
    const nf::PadState* pd = in.pad != nullptr && in.pad->connected ? in.pad : nullptr;
    const auto btn = [&](uint16_t mask) { return pd != nullptr && (pd->buttons & mask) != 0; };
    const float lx = pd != nullptr ? stick(pd->lx) : 0.0f, ly = pd != nullptr ? stick(pd->ly) : 0.0f;
    bool held[10];
    held[int(Cmd::Up)] = cmd("menu_up") || btn(nf::kPadDpadUp) || ly > 0.55f;
    held[int(Cmd::Down)] = cmd("menu_down") || btn(nf::kPadDpadDown) || ly < -0.55f;
    held[int(Cmd::Left)] = cmd("menu_left") || btn(nf::kPadDpadLeft) || lx < -0.55f;
    held[int(Cmd::Right)] = cmd("menu_right") || btn(nf::kPadDpadRight) || lx > 0.55f;
    held[int(Cmd::Select)] = cmd("menu_select") || key(VK_RETURN) || btn(nf::kPadA);
    held[int(Cmd::Back)] = cmd("menu_back") || key(VK_ESCAPE) || btn(nf::kPadB);
    held[int(Cmd::TabPrev)] = cmd("menu_tab_prev");
    held[int(Cmd::TabNext)] = cmd("menu_tab_next");
    held[int(Cmd::Defaults)] = cmd("menu_defaults");
    held[int(Cmd::Apply)] = key('F') || btn(nf::kPadY);
    if (swallow_) {
        bool any = in.lmb || in.back_button;
        for (bool h : held) any = any || h;
        if (!any) swallow_ = false;
        for (int i = 0; i < 10; ++i) held_[i] = held[i];
        prev_lmb_ = in.lmb;
        prev_back_ = in.back_button;
        return;
    }
    for (int i = 0; i < 10; ++i) {
        bool fire = held[i] && !held_[i];
        if (i < 4) {
            // As setas seguradas repetem.
            if (!held[i]) {
                repeat_[i] = 0.0f;
            } else if (fire) {
                repeat_[i] = tk_->motion.repeat_delay;
            } else {
                repeat_[i] -= dt;
                if (repeat_[i] <= 0.0f) {
                    fire = true;
                    repeat_[i] += tk_->motion.repeat_rate;
                }
            }
        }
        held_[i] = held[i];
        if (fire) {
            mouse_active_ = false;
            act(Cmd(i));
            if (!active_ || closing_) return;
        }
    }
    mouse(in, cv);
}

// ---------------------------------------------------------------- desenho

std::wstring OptionsScreen::key_name(int command) const {
    nf::InputCode in[nf::kMaxBinds];
    if (command >= 0 && nf::controls().bindings.list(command, false, in, nf::kMaxBinds) > 0) {
        return widen(nf::input_label(in[0], true));
    }
    return std::wstring();
}

std::string OptionsScreen::pad_glyph(int which) const {
    switch (which) {
        case kActSelect: return "xbox_a";
        case kActBack:
        case kActCancel: return "xbox_b";
        case kActDefaults: {
            const int c = nf::find_bind_command("menu_defaults");
            nf::InputCode in[nf::kMaxBinds];
            if (c >= 0 && nf::controls().bindings.list(c, true, in, nf::kMaxBinds) > 0) {
                const std::string g = glyph_of(in[0]);
                if (!g.empty()) return g;
            }
            return "xbox_x";
        }
        case kActApply: return "xbox_y";
        default: return std::string();
    }
}

void OptionsScreen::prompts(const Frame& f, PromptBar& bar) const {
    (void)f;
    const bool pad = pad_device_ == 1;
    bar.prompts.clear();
    bar.info_title = T(PcText::MdFooterPort);
    bar.info_sub = L"SLUS-20579 · NTSC-U";
    bar.info2_title = fmt(PcText::MdFooterBuild, build_.empty() ? "dev" : build_.c_str());
    bar.info2_sub = T(PcText::MdFooterGame);
    const auto add = [&](int action, const std::wstring& key, PcText label) {
        Prompt p;
        p.action = action;
        p.label = T(label);
        if (pad) p.glyph = pad_glyph(action);
        p.key = key;
        bar.prompts.push_back(p);
    };
    const std::wstring sel_key = key_name(nf::find_bind_command("menu_select"));
    if (capture_cmd_ >= 0) {
        // Durante a captura só o teclado cancela ou limpa (qualquer botão do controle vira a ligação nova).
        bar.prompts.push_back(Prompt{L"Del", std::string(), T(PcText::MdPromptClear), kActClear});
        bar.prompts.push_back(Prompt{L"Esc", std::string(), T(PcText::MdPromptCancel), kActCancel});
        return;
    }
    if (dialog_.open) {
        add(kActSelect, sel_key.empty() ? L"Enter" : sel_key, PcText::MdPromptSelect);
        add(kActBack, L"Esc", PcText::MdPromptBack);
        return;
    }
    const bool defaults = dd_item_ < 0;
    if (pending_changes()) add(kActApply, L"F", PcText::MdPromptApply);
    if (defaults) add(kActDefaults, key_name(nf::find_bind_command("menu_defaults")), PcText::MdPromptDefaults);
    PcText select = PcText::MdPromptSelect;
    bool adjust = false, inert = false;
    if (zone_ == Zone::Content && focus_ >= 0 && dd_item_ < 0) {
        switch (items_[size_t(focus_)].kind) {
            case Kind::Toggle: select = PcText::MdPromptToggle; break;
            case Kind::Slider: adjust = true; break;
            case Kind::KeyRow: select = PcText::MdPromptRebind; break;
            case Kind::Info:
            case Kind::Locked:
            case Kind::LockedSlider: inert = true; break;  // só para ler
            case Kind::Action: inert = items_[size_t(focus_)].opt == Opt::Apply && !pending_changes(); break;
            default: break;
        }
    }
    if (inert) {
    } else if (adjust) {
        // A barra muda com esquerda/direita (e com o mouse no trilho).
        bar.prompts.push_back(Prompt{L"← →", pad ? std::string("xbox_dpad_h") : std::string(), T(PcText::MdPromptAdjust), 0});
    } else {
        add(kActSelect, sel_key.empty() ? L"Enter" : sel_key, select);
    }
    add(kActBack, L"Esc", PcText::MdPromptBack);
}

Description OptionsScreen::description(const Frame& f) const {
    (void)f;
    Description d;
    d.strip[0] = L"NF_PC/OPT";
    d.strip[1] = L"CAT." + std::to_wstring(int(cat_) + 1);
    d.strip[2] = L"[" + to_upper(widen(cat_name(cat_))) + L"]";
    d.strip[3] = L"#" + widen(build_.empty() ? std::string("dev") : build_);
    if (zone_ == Zone::Categories || focus_ < 0) {
        d.title = T(kCatLabel[int(cat_)]);
        std::wstring body;
        for (const Item& it : items_) {
            if (it.kind != Kind::Section) continue;
            if (!body.empty()) body += L"  ·  ";
            body += T(it.label);
        }
        d.body = body;
    } else {
        const Item& it = items_[size_t(focus_)];
        if (it.kind == Kind::KeyRow) {
            const char* name = english() ? nf::kBindCommands[it.command].label_en : nf::kBindCommands[it.command].label_pt;
            d.title = widen(name);
            d.body = T(PcText::MdDescKeys);
            const PcText cols[3] = {PcText::MdColPrimary, PcText::MdColSecondary, PcText::MdColController};
            d.value_label = T(cols[col_]) + L":";
            d.value = key_cell(it.command, col_);
        } else {
            d.title = T(it.label);
            const PcText desc = desc_for(it.opt);
            if (it.opt == Opt::Adapter) {
                const std::string& used = nf::video_controller().adapter_in_use();
                d.body = fmt(desc, used.empty() ? "?" : used.c_str());
            } else if (desc != PcText::Count) {
                d.body = T(desc);
            }
            if (it.kind != Kind::Action) {
                d.value_label = T(PcText::MdCurrentValue);
                d.value = it.kind == Kind::Locked || it.kind == Kind::LockedSlider ? T(PcText::ComingSoon) : value_text(it.opt);
            }
        }
    }
    if (capture_cmd_ >= 0) {
        const char* name = english() ? nf::kBindCommands[capture_cmd_].label_en : nf::kBindCommands[capture_cmd_].label_pt;
        char buf[256];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(capture_col_ == 2 ? PcText::MdPressPad : PcText::MdPressKey), name);
        d.notice = widen(buf) + L". " + T(PcText::MdCaptureHint) + L".";
    } else if (pending_changes() && (cat_ == Cat::Video || cat_ == Cat::Graphics || cat_ == Cat::Interface)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdPending), pad_device_ == 1 ? "Y" : "F");
        d.notice = widen(buf);
    } else if (cat_ == Cat::Audio) {
        // Os volumes são os do driver do jogo (`SFXMusicSetVolume`/`SFXSetVolume`) e valem na hora; o aviso diz isso.
        d.notice = T(PcText::MdAudioNotice);
    }
    // A sensibilidade do mouse: a volta completa em centímetros (800 e 1600 DPI), o enchimento pela posição da barra.
    if (zone_ == Zone::Content && focus_ >= 0 && items_[size_t(focus_)].opt == Opt::MouseSens) {
        d.meter = true;
        d.meter_label = T(PcText::MdMeterMouse);
        char buf[160];
        std::snprintf(buf, sizeof(buf), PcText_Utf8(PcText::MdMeterMouseValue),
                      double(nf::mouse_cm_per_360(psiMouseSensitivity, 800.0f)),
                      double(nf::mouse_cm_per_360(psiMouseSensitivity, 1600.0f)));
        d.meter_value = widen(buf);
        d.meter_fill = float(nf::mouse_sens_to_pos(psiMouseSensitivity) - nf::kMouseSensPosMin) /
                       float(nf::kMouseSensPosMax - nf::kMouseSensPosMin);
    }
    if (cat_ == Cat::Video || cat_ == Cat::Graphics) {
        d.meter = true;
        d.meter_label = T(PcText::MdMeterRender);
        const int w = std::max(1, pending_.width * pending_.render_scale / 100);
        const int h = std::max(1, pending_.height * pending_.render_scale / 100);
        wchar_t buf[96];
        if (pending_.msaa > 1) {
            swprintf(buf, 96, L"%d × %d  ·  %d%%  ·  MSAA %dx", w, h, pending_.render_scale, pending_.msaa);
        } else {
            swprintf(buf, 96, L"%d × %d  ·  %d%%", w, h, pending_.render_scale);
        }
        d.meter_value = buf;
        d.meter_fill = std::clamp(float(pending_.render_scale) / 200.0f, 0.0f, 1.0f);
    }
    return d;
}

void OptionsScreen::draw_content(DrawList& dl, const Frame& f, float alpha, float dx) const {
    const Tokens& tk = *tk_;
    const auto& sp = tk.space;
    const float top = sp.list_top, bottom = sp.list_bottom;
    const float scroll = scroll_.value();
    const float total = content_height();
    dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, top - 2.0f, sp.mid_x1 + 20.0f + dx, bottom});
    // A opacidade de uma faixa da lista: some perto das pontas cortadas.
    const auto fade = [&](float y0, float y1) {
        float a = 1.0f;
        const float cy = (y0 + y1) * 0.5f;
        // Uma linha cortada pela ponta some de vez; as vizinhas da ponta ficam meio apagadas.
        if (scroll > 0.5f) a *= std::clamp((cy - top - 6.0f) / (sp.fade_px * 1.6f), 0.0f, 1.0f);
        if (total - scroll > (bottom - top) + 0.5f) a *= std::clamp((bottom - cy - 6.0f) / (sp.fade_px * 1.6f), 0.0f, 1.0f);
        return a;
    };
    const int anim_item = dd_item_ >= 0 ? dd_item_ : dd_closing_;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const ItemAnim& an = anims_[i];
        const float y0 = top + it.y - scroll;
        const float y1 = y0 + it.h;
        if (y1 < top - 4.0f || y0 > bottom + 4.0f) {
            if (int(i) != anim_item) continue;
        }
        const Rect row{sp.mid_x0 + dx, y0, sp.mid_x1 + dx, y1};
        RowState s;
        s.alpha = alpha * fade(y0, y1);
        s.focused = zone_ == Zone::Content && int(i) == focus_ && dd_item_ < 0;
        s.label_t = an.label.value();
        if (dd_item_ >= 0 && int(i) != dd_item_) s.alpha *= 0.55f;  // com uma lista aberta, o resto recua
        const float hair_x1 = sp.mid_x1 - 8.0f + dx;
        switch (it.kind) {
            case Kind::Section:
                draw_section(dl, f, row, T(it.label), s.alpha);
                break;
            case Kind::Dropdown: {
                draw_row_label(dl, f, row, T(it.label), s);
                draw_dropdown_value(dl, f, row, an.shown_value, int(i) == dd_item_, s, an.old_value, an.change.value());
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            }
            case Kind::Toggle:
                draw_row_label(dl, f, row, T(it.label), s);
                draw_toggle(dl, f, row, an.knob.value(), value_text(it.opt), s,
                            std::max(text_width(f, T(PcText::On), tk.type.value), text_width(f, T(PcText::Off), tk.type.value)));
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            case Kind::Slider:
                draw_row_label(dl, f, row, T(it.label), s);
                draw_slider(dl, f, row, an.fill.value(), value_text(it.opt), s);
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            case Kind::LockedSlider: {
                RowState d = s;
                d.disabled = true;
                draw_row_label(dl, f, row, T(it.label), d);
                const SliderSpec sl = slider(it.opt);
                draw_slider(dl, f, row, float(sl.pos - sl.lo) / float(std::max(1, sl.hi - sl.lo)), sl.text, d);
                const Rect t = slider_track(f, row);
                dl.padlock(Rect{t.x0 - 26.0f, row.cy() - 8.0f, t.x0 - 14.0f, row.cy() + 7.0f}, tk.color.dim.with_alpha(s.alpha));
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            }
            case Kind::Info:
                draw_row_label(dl, f, row, T(it.label), s);
                draw_info_value(dl, f, row, an.shown_value, s);
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            case Kind::Locked: {
                RowState d = s;
                d.disabled = true;
                d.label_t = 0.0f;
                draw_row_label(dl, f, row, T(it.label), d);
                draw_locked_value(dl, f, row, T(PcText::ComingSoon), s);
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            }
            case Kind::Action: {
                const bool on = it.opt == Opt::RestoreKeys || pending_changes();
                draw_action(dl, f, row, T(it.label), on, s, it.opt == Opt::Apply ? std::wstring(L"F") : std::wstring(),
                            it.opt == Opt::Apply && pad_device_ == 1 ? std::string("xbox_y") : std::string());
                break;
            }
            case Kind::TableHead: {
                const TextStyle& hs = tk.type.table_head;
                const Color hc = tk.color.dim.with_alpha(s.alpha);
                const float cy = row.cy() + 6.0f;
                const float h = hs.size * 1.6f;
                dl.text(Rect{row.x0 + sp.pad_l, cy - h * 0.5f, row.x0 + 400.0f, cy + h * 0.5f}, to_upper(T(PcText::MdColAction)), hs,
                        hc, Align::Left);
                const PcText cols[3] = {PcText::MdColPrimary, PcText::MdColSecondary, PcText::MdColController};
                for (int c = 0; c < 3; ++c) {
                    const float cx = key_col_centre(tk, c) + dx;
                    dl.text(Rect{cx - 90.0f, cy - h * 0.5f, cx + 90.0f, cy + h * 0.5f}, to_upper(T(cols[c])), hs, hc, Align::Centre);
                }
                draw_hairline(dl, f, row.x0, hair_x1, y1, s.alpha);
                break;
            }
            case Kind::KeyRow: {
                const char* name = english() ? nf::kBindCommands[it.command].label_en : nf::kBindCommands[it.command].label_pt;
                draw_row_label(dl, f, row, widen(name), s, 0.0f, &tk.type.cell);
                for (int c = 0; c < 3; ++c) {
                    const float cx = key_col_centre(tk, c) + dx;
                    const bool on = s.focused && c == col_;
                    std::wstring v = key_cell(it.command, c);
                    Color vc = mix(tk.color.text_2, tk.color.text, on ? s.label_t : 0.0f);
                    if (on) vc = mix(tk.color.text_2, tk.color.accent, s.label_t);
                    if (capture_cmd_ == it.command && capture_col_ == c) {
                        v = L"…";
                        vc = tk.color.accent.with_alpha(0.55f + 0.45f * nf::fm::sin(f.time * 7.0f));
                    }
                    if (v == T(PcText::MdNone) || (!v.empty() && v[0] == L'(')) vc = mix(vc, tk.color.dim, 0.6f);
                    const float h = tk.type.cell.size * 1.6f;
                    dl.text(Rect{cx - kKeyColHalf, row.cy() - h * 0.5f, cx + kKeyColHalf, row.cy() + h * 0.5f}, v, tk.type.cell,
                            vc.with_alpha(s.alpha), Align::Centre);
                }
                dl.line(row.x0, y1, hair_x1, y1, tk.color.hairline.with_alpha(0.6f * s.alpha), 1.0f);
                break;
            }
        }
        // As opções da lista aberta (ou fechando), recuadas, abaixo da linha.
        if (int(i) == anim_item) {
            const Choice ch = choices(it.opt);
            const float open = dd_open_.value();
            const float area = sp.option_h * float(ch.labels.size()) * open;
            dl.push_clip(Rect{sp.mid_x0 - 40.0f + dx, std::max(top, y1), sp.mid_x1 + 20.0f + dx, std::min(bottom, y1 + area)});
            for (size_t j = 0; j < ch.labels.size(); ++j) {
                const float oy = y1 + sp.option_h * float(j) - sp.option_h * float(ch.labels.size()) * (1.0f - open) * 0.35f;
                const Rect orow{sp.mid_x0 + sp.option_indent + dx, oy, sp.mid_x1 + dx, oy + sp.option_h};
                RowState os;
                os.alpha = alpha * open * fade(oy, oy + sp.option_h);
                os.focused = dd_item_ >= 0 && int(j) == dd_focus_;
                os.label_t = os.focused ? 1.0f : 0.0f;
                draw_option(dl, f, orow, ch.labels[j], int(j) == ch.index, os);
                draw_hairline(dl, f, orow.x0, hair_x1, orow.y1, os.alpha);
            }
            dl.pop_clip();
        }
    }
    mark_.draw(dl, f, alpha, top - scroll);
    dl.pop_clip();
    // O indicador de rolagem.
    const float view = bottom - top;
    if (total > 1.0f) {
        draw_scroll(dl, f, sp.scroll_x + dx, top + 24.0f, bottom, view / total,
                    total > view ? scroll / (total - view) : 0.0f, alpha);
    }
}

void OptionsScreen::build(DrawList& dl, const Canvas& cv, const Measure& m) const {
    if (!active_) return;
    last_cv_ = cv;
    const Tokens& tk = *tk_;
    const Frame f{tk, cv, m, time_};
    const float k = appear_.value();
    if (!embedded_) draw_background(dl, f, k);
    // As colunas entram uma depois da outra, deslizando alguns pixels.
    const auto staggered = [&](int n) {
        if (closing_ || appear_.duration <= 0.0f) return k;
        const float t = (appear_.elapsed - tk.motion.stagger * float(n)) / std::max(0.01f, tk.motion.open);
        return curve_eval(tk.motion.ease_out, std::clamp(t, 0.0f, 1.0f));
    };
    const float k0 = staggered(0), k1 = staggered(1), k2 = staggered(2);
    const float c = content_.value();
    if (!embedded_) tabs_.draw(dl, f, k);
    cats_.draw(dl, f, k0, -tk.space.slide * (1.0f - k0));
    draw_content(dl, f, k1 * (0.35f + 0.65f * c), tk.space.slide * 0.6f * (1.0f - k1) + 10.0f * (1.0f - c));
    draw_description(dl, f, description(f), k2 * (0.3f + 0.7f * c), tk.space.slide * (1.0f - k2));
    bar_.draw(dl, f, k);
    dialog_.draw(dl, f);
    toast_.draw(dl, f);
}

std::string OptionsScreen::describe() const {
    char buf[512];
    std::string focus_name = "-";
    if (focus_ >= 0 && size_t(focus_) < items_.size()) {
        const Item& it = items_[size_t(focus_)];
        focus_name = it.kind == Kind::KeyRow ? nf::kBindCommands[it.command].key
                                             : (it.label != PcText::Count ? PcText_Utf8(it.label, "en") : "-");
    }
    std::snprintf(buf, sizeof(buf),
                  "moderno ativo %d fechando %d aparecer %.2f categoria %s zona %s foco %d (%s) coluna %d lista_aberta %d "
                  "opcao %d rolagem %.0f dialogo %d captura %d pendente %d aviso %d mouse %d",
                  active_ ? 1 : 0, closing_ ? 1 : 0, double(appear_.value()), cat_name(cat_),
                  zone_ == Zone::Categories ? "categorias" : "linhas", focus_, focus_name.c_str(), col_, dd_item_, dd_focus_,
                  double(scroll_.value()), dialog_.open ? 1 : 0, capture_cmd_, pending_changes() ? 1 : 0,
                  toast_visible() ? 1 : 0, mouse_active_ ? 1 : 0);
    return buf;
}

}  // namespace nf::ui
