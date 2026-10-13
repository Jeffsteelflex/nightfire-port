// A ligação do estilo de menu Modern com o nightfire.exe. Ver ui_modern_app.h.

#include "render/ui_modern_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "data/files_bin.h"
#include "data/pack_images.h"
#include "engine/menu_modern.h"
#include "engine/weapon_wheel.h"
#include "platform/keys.h"
#include "platform/os.h"
#include "platform/psi_2d.h"
#include "platform/psi_input.h"
#include "render/renderer.h"

namespace nf {

namespace {

std::string read_text(const std::wstring& path, bool* ok) {
    std::string s;
    *ok = false;
    FILE* f = os::open_file(path, "rb");
    if (f == nullptr) return s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    *ok = true;
    return s;
}

// A câmera dos menus do jogo (Menu_CreateSprite).
constexpr uint32_t kMenuCamera = 7;

}  // namespace

void ModernUiApp::init(Renderer& renderer, const std::wstring& dir, const std::string& build) {
    renderer_ = &renderer;
    tokens_ = ui::default_tokens();
    tokens_status_ = "padrao";
    if (!dir.empty()) {
        bool ok = false;
        const std::string json = read_text(os::join(dir, L"design/tokens.json"), &ok);
        std::string err;
        if (ok) {
            ui::Tokens t = tokens_;
            if (ui::parse_tokens(json, t, &err)) {
                tokens_ = t;
                tokens_status_ = "tokens.json";
            } else {
                tokens_status_ = "tokens.json com erro: " + err;
            }
        }
    }
    pause_.set_tokens(&tokens_);
    pause_.set_build(build);
    fail_.set_tokens(&tokens_);
    fail_.set_build(build);
    results_.set_tokens(&tokens_);
    results_.set_build(build);
    front_.set_tokens(&tokens_);
    front_.set_build(build);
    warning_.set_tokens(&tokens_);
    available_ = renderer.set_modern_ui(dir);
    // Os pedidos das telas entram no tick do gerente de menus do jogo.
    MenuModern_Install();
}

bool ModernUiApp::open(ui::Cat cat) {
    if (!available_) return false;
    last_ = cat;
    const MmSnapshot s = MenuModern_Snapshot();
    if (pause_.active() && !pause_.closing()) {
        pause_.show_tab(ui::PauseScreen::Tab::Options, cat);
        return true;
    }
    if (s.screen != MmScreen::Pause) return false;
    pause_.open(s, ui::PauseScreen::Tab::Options, cat);
    return true;
}

bool ModernUiApp::open_last() { return open(last_); }

bool ModernUiApp::open_pause(ui::PauseScreen::Tab tab) {
    if (!available_) return false;
    const MmSnapshot s = MenuModern_Snapshot();
    if (pause_.active() && !pause_.closing()) {
        pause_.show_tab(tab, last_);
        return true;
    }
    if (s.screen != MmScreen::Pause) return false;
    pause_.open(s, tab, last_);
    return true;
}

bool ModernUiApp::active() const {
    return pause_.active() || fail_.active() || results_.active() || front_.active() || warning_.active();
}

uint32_t ModernUiApp::hidden_2d() const {
    // O fim de missão pela morte deixa o HUD (só a cortina de sangue está nele); as outras telas cobrem todo o 2D do
    // jogo (num fracasso por objetivo o HUD ainda mostra a vida e a munição).
    if (pause_.active() || results_.active() || front_.active() || warning_.active()) return ~0u;
    if (fail_.active()) return fail_.died() ? 1u << kMenuCamera : ~0u;
    return 0;
}

void ModernUiApp::frame(float dt, const ModernUiInput& in, bool enabled) {
    const MmSnapshot s = MenuModern_Snapshot();
    // As telas seguem o menu aberto do jogo.
    const auto follow = [&](auto& screen, MmScreen which, auto open) {
        const bool live = screen.active() && !screen.closing();
        if (s.screen == which) {
            if (!live && enabled && available_) open();
        } else if (live) {
            screen.close();
            MenuModern_Clear();
        }
    };
    follow(pause_, MmScreen::Pause, [&] { pause_.open(s, ui::PauseScreen::Tab::Pause, last_); });
    follow(fail_, MmScreen::Fail, [&] { fail_.open(s); });
    follow(results_, MmScreen::Results, [&] { results_.open(s); });
    // O menu principal: a arte do título existe no pacote (sem ela, a marca Modern); o fundo em laço de uma página, se
    // toca, no lugar dela.
    front_.set_art(psi_2d_texture_slot(0x3000074) >= 0);
    front_.set_movie(movie_ && movie_loop_ && renderer_->movie_image() != nullptr);
    follow(front_, MmScreen::Front, [&] { front_.open(s); });
    // O aviso de controle desconectado: só desenho, o jogo cuida da volta (a fila não muda).
    if (s.screen == MmScreen::Warning) {
        if (!(warning_.active() && !warning_.closing()) && enabled && available_) warning_.open(s);
    } else if (warning_.active() && !warning_.closing()) {
        warning_.close();
    }
    if (!active()) {
        if (was_active_) {
            psiInput_SetModal(false);
            was_active_ = false;
        }
        return;
    }
    ui::UiInput ui_in;
    ui_in.keys = in.keys;
    ui_in.mouse_inside = in.mouse_inside;
    ui_in.mouse_x = in.mouse_x;
    ui_in.mouse_y = in.mouse_y;
    const bool* k = in.keys;
    ui_in.lmb = k != nullptr && k[nf::key::LButton];
    ui_in.back_button = k != nullptr && (k[nf::key::RButton] || k[nf::key::XButton1]);
    ui_in.wheel = in.wheel;
    ui_in.pad = psiInput_PlayerPad();
    ui_in.device = psiInput_ActiveDevice();
    const ui::Canvas cv = ui::make_canvas(renderer_->width(), renderer_->height());
    Renderer* r = renderer_;
    const ui::Measure measure = [r](const std::wstring& t, const ui::TextStyle& st) { return r->modern_measure(t, st); };
    pause_.frame(dt, ui_in, s, cv, measure);
    fail_.frame(dt, ui_in, s, cv, measure);
    results_.frame(dt, ui_in, s, cv, measure);
    // Um filme de tela cheia: o menu principal Modern não recebe nada (o jogo recebe: Start pula o filme como na página
    // do jogo, Menu_StopMovie).
    front_.frame(dt, movie_full() ? ui::UiInput{} : ui_in, s, cv, measure);
    warning_.frame(dt, s, cv, measure);
    if (pause_.active() && pause_.tab() == ui::PauseScreen::Tab::Options) last_ = pause_.options().category();
    // O jogo não recebe nada enquanto uma tela está de pé; fechando (o menu do jogo já sumiu), já recebe.
    // O aviso de controle desconectado não prende: o jogo espera o Start dele.
    const bool live = (pause_.active() && !pause_.closing()) || (fail_.active() && !fail_.closing()) ||
                      (results_.active() && !results_.closing()) ||
                      (front_.active() && !front_.closing() && !movie_full());
    if (live != was_active_) {
        psiInput_SetModal(live);
        was_active_ = live;
    }
}

void ModernUiApp::draw_images(const ui::ImageList& list, const ui::Canvas& cv, const FrameParams& frame) {
    DrawItem item;
    item.blend = Blend::Alpha;
    item.clip2d[2] = float(renderer_->width());
    item.clip2d[3] = float(renderer_->height());
    for (const ui::ImageCmd& c : list) {
        int tw = 0, th = 0;
        // O quadro do filme que toca (a textura do renderizador, não uma do jogo), ou uma imagem do port.
        GpuImage* movie = c.hash == ui::kMovieImage ? renderer_->movie_image() : nullptr;
        if (c.port) movie = renderer_->ui_image(c.hash);
        const int32_t slot = movie != nullptr || c.port ? -1 : psi_2d_texture_slot(c.hash, &tw, &th);
        if ((slot < 0 && movie == nullptr) || c.alpha <= 0.001f) continue;
        Quad2D q;
        q.image = movie;
        q.smooth = c.port;
        q.x0 = ui::window_x(cv, c.r.x0);
        q.y0 = ui::window_y(cv, c.r.y0);
        q.x1 = ui::window_x(cv, c.r.x1);
        q.y1 = ui::window_y(cv, c.r.y1);
        q.u0 = c.uv.x0;
        q.v0 = c.uv.y0;
        q.u1 = c.uv.x1;
        q.v1 = c.uv.y1;
        if (c.whole && tw > 0 && th > 0) {
            q.u0 = 0.5f / float(tw);
            q.v0 = 0.5f / float(th);
            q.u1 = 1.0f - q.u0;
            q.v1 = 1.0f - q.v0;
        }
        // A cor na escala do GS (0x80 = 1): a textura como ela é (vezes a cor de tint), com o alfa da imagem.
        q.rgba[0] = uint8_t(std::clamp(c.tint.r, 0.0f, 1.0f) * 128.0f + 0.5f);
        q.rgba[1] = uint8_t(std::clamp(c.tint.g, 0.0f, 1.0f) * 128.0f + 0.5f);
        q.rgba[2] = uint8_t(std::clamp(c.tint.b, 0.0f, 1.0f) * 128.0f + 0.5f);
        q.rgba[3] = uint8_t(std::clamp(c.alpha, 0.0f, 1.0f) * 128.0f + 0.5f);
        q.texture = slot;
        q.alpha = 0x44;
        q.linear = true;
        item.quads2d.push_back(q);
    }
    if (!item.quads2d.empty()) renderer_->render_2d({item}, frame);
}

void ModernUiApp::draw(const std::vector<DrawItem>& overlay, const FrameParams& frame) {
    if (!active() || renderer_ == nullptr) return;
    const ui::Canvas cv = ui::make_canvas(renderer_->width(), renderer_->height());
    Renderer* r = renderer_;
    const ui::Measure measure = [r](const std::wstring& t, const ui::TextStyle& st) { return r->modern_measure(t, st); };
    if (front_.active() && !movie_full()) {
        // O menu principal, em camadas: o fundo, a arte do título (desfocada nos menus; ou o filme de fundo da página),
        // o véu, as imagens do jogo (o logotipo, a imagem da missão) e o resto. Com um filme de tela cheia, só ele (o
        // renderizador já o desenhou como fundo do quadro).
        ui::DrawList back;
        front_.build_back(back, cv, measure);
        renderer_->draw_modern(back, cv);
        ui::ImageList art;
        front_.backdrop(art, cv);
        draw_images(art, cv, frame);
        // O desfoque é de um núcleo fixo em 1/4 da janela: numa janela alta ele passa mais vezes, para a arte ficar tão
        // desfocada em 4K quanto em 1080p (o logotipo dela não aparece atrás das linhas).
        const int passes = std::max(1, int(std::lround(double(renderer_->height()) / 1080.0)));
        for (int i = 0; i < passes; ++i) renderer_->blur_background(front_.blur());
        ui::DrawList veil;
        front_.build_veil(veil, cv, measure);
        renderer_->draw_modern(veil, cv);
        ui::ImageList pictures;
        front_.images(pictures, cv);
        draw_images(pictures, cv, frame);
        ui::DrawList dl;
        front_.build(dl, cv, measure);
        renderer_->draw_modern(dl, cv);
    }
    const float scene_blur = std::max({pause_.background(), fail_.background(), warning_.background()});
    if (scene_blur > 0.0f) renderer_->blur_background(scene_blur);
    // O 2D do jogo que fica (a cortina de sangue do fim de missão): nítido sobre a cena desfocada, embaixo do véu da
    // tela e do texto.
    if (!overlay.empty()) renderer_->render_2d(overlay, frame);
    ui::DrawList back;
    fail_.build_back(back, cv, measure);
    if (!back.cmds.empty()) renderer_->draw_modern(back, cv);
    ui::DrawList dl;
    fail_.build(dl, cv, measure);
    results_.build(dl, cv, measure);
    pause_.build(dl, cv, measure);
    warning_.build(dl, cv, measure);
    renderer_->draw_modern(dl, cv);
}

int ModernUiApp::load_wheel_icons(const FilesBin& files) {
    wheel_icons_ = 0;
    if (renderer_ == nullptr || !files.has("07000048.bin")) return 0;
    uint32_t hashes[96];
    const int n = weapon_wheel_icon_hashes(hashes, 96);
    const std::vector<uint32_t> want(hashes, hashes + n);
    Bytes pak;
    try {
        pak = files.load("07000048.bin");
    } catch (const std::exception&) {
        return 0;
    }
    for (const auto& [hash, tex] : decode_pack_images(pak, want)) {
        const std::vector<uint8_t> icon = ui::wheel_icon_image(tex.rgba.data(), tex.width, tex.height);
        renderer_->set_ui_image(hash, icon.data(), tex.width, tex.height);
        ++wheel_icons_;
    }
    return wheel_icons_;
}

void ModernUiApp::draw_wheel(ui::WheelView view, const FrameParams& frame) {
    if (renderer_ == nullptr || !available_) return;
    // Sem a imagem carregada, o espaço fica com o texto.
    for (auto* ring : {&view.weapons, &view.gadgets}) {
        for (ui::WheelViewSlot& s : *ring) {
            if (s.icon != 0 && renderer_->ui_image(s.icon) == nullptr) s.icon = 0;
        }
    }
    const ui::Canvas cv = ui::make_canvas(renderer_->width(), renderer_->height());
    Renderer* r = renderer_;
    const ui::Measure measure = [r](const std::wstring& t, const ui::TextStyle& st) { return r->modern_measure(t, st); };
    ui::DrawList back, front;
    ui::ImageList icons;
    ui::build_weapon_wheel(back, icons, front, cv, tokens_, measure, view);
    renderer_->draw_modern(back, cv);
    draw_images(icons, cv, frame);
    renderer_->draw_modern(front, cv);
}

bool ModernUiApp::mouse_active() const {
    if (pause_.active()) return pause_.mouse_active();
    if (fail_.active()) return fail_.mouse_active();
    if (results_.active()) return results_.mouse_active();
    if (front_.active()) return front_.mouse_active();
    return false;
}

std::string ModernUiApp::describe() const {
    std::string s = pause_.options().describe();
    s += " | desenho " + (renderer_ != nullptr ? renderer_->modern_describe() : std::string("-"));
    s += " tokens " + tokens_status_;
    char bridge[256];
    MenuModern_Describe(bridge, sizeof(bridge));
    s += "\n" + pause_.describe() + "\n" + fail_.describe() + "\n" + results_.describe() + "\n" + front_.describe() + "\n" +
         warning_.describe() + "\nponte_moderna " + bridge;
    return s;
}

}  // namespace nf
