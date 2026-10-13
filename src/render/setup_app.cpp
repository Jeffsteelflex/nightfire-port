// A tela de entrada (o launcher) no nightfire.exe. Ver setup_app.h.

#include "render/setup_app.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "engine/pc_text.h"
#include "platform/disc_pack.h"
#include "platform/file_dialog.h"
#include "platform/keys.h"
#include "platform/os.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "ui/modern_setup.h"

namespace nf {

namespace {

std::wstring T(PcText id) { return ui::widen(PcText_Utf8(id)); }

std::wstring Tf(PcText id, const std::wstring& arg) {
    const std::wstring fmt = T(id);
    const size_t at = fmt.find(L"%s");
    return at == std::wstring::npos ? fmt : fmt.substr(0, at) + arg + fmt.substr(at + 2);
}

// Um caminho longo encurtado no meio para a caixa (o começo, "…" e o fim, com o nome do arquivo inteiro): a caixa do kit
// quebra as linhas nos espaços, e um caminho sem espaços passaria da borda.
std::wstring short_path(const std::wstring& p, size_t max = 56) {
    if (p.size() <= max) return p;
    const size_t slash = p.find_last_of(L"\\/");
    const size_t name = slash == std::wstring::npos ? p.size() : p.size() - slash;
    const size_t tail = std::min(p.size(), std::max(name, max - 13));
    return p.substr(0, 12) + L"…" + p.substr(p.size() - tail);
}

std::string narrow_utf8(const std::wstring& w) { return os::to_utf8(w); }

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

// Os leitores de DVD com um disco de PS2, procurados de novo a cada 2 s.
class DriveWatch {
public:
    struct Entry {
        std::wstring root;
        DiscCheck check;
        std::shared_ptr<DiscSource> source;
    };
    explicit DriveWatch(const DiscTitle* title) : title_(title) {}
    ~DriveWatch() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }
    // Uma procura agora (as capturas: sem a thread, para a imagem não depender do tempo).
    void scan_now() {
        std::vector<Entry> found = scan();
        std::lock_guard<std::mutex> lock(mutex_);
        drives_ = std::move(found);
        ++serial_;
    }
    void start() {
        thread_ = std::thread([this] {
            while (!stop_) {
                std::vector<Entry> found = scan();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const bool same = found.size() == drives_.size() &&
                                      std::equal(found.begin(), found.end(), drives_.begin(), [](const Entry& a, const Entry& b) {
                                          return a.root == b.root && a.check.status == b.check.status && a.check.boot_id == b.check.boot_id;
                                      });
                    if (!same) {
                        drives_ = std::move(found);
                        ++serial_;
                    }
                }
                for (int i = 0; i < 20 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    uint32_t serial() {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }
    std::vector<Entry> drives() {
        std::lock_guard<std::mutex> lock(mutex_);
        return drives_;
    }

private:
    std::vector<Entry> scan() const {
        std::vector<Entry> out;
        for (const std::wstring& root : optical_drives()) {
            // Só os leitores com um disco de PS2 (com SYSTEM.CNF) aparecem; o certo ou não.
            if (!os::exists(root + L"SYSTEM.CNF")) continue;
            DiscFound f = probe_disc(root, DiscOrigin::Drive, *title_);
            out.push_back(Entry{root, f.check, f.source});
        }
        return out;
    }
    const DiscTitle* title_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<Entry> drives_;
    uint32_t serial_ = 0;
};

// A cópia pessoal numa thread.
class PackJob {
public:
    ~PackJob() {
        progress_.cancel = true;
        if (thread_.joinable()) thread_.join();
    }
    bool running() const { return state_ == 1; }
    void start(std::shared_ptr<DiscSource> src, const DiscTitle* title, PackPlan plan, std::wstring exe, std::wstring out) {
        if (running()) return;
        if (thread_.joinable()) thread_.join();
        progress_.done = 0;
        progress_.total = plan.bytes;
        progress_.cancel = false;
        state_ = 1;
        out_ = out;
        thread_ = std::thread([this, src, title, plan = std::move(plan), exe, out] {
            PackError e = PackError::None;
            std::string d;
            const bool ok = pack_disc(*src, *title, plan, exe, out, &progress_, &e, &d);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                error_ = ok ? PackError::None : e;
                detail_ = d;
            }
            state_ = 2;
        });
    }
    void cancel() { progress_.cancel = true; }
    float progress() const {
        const uint64_t t = progress_.total;
        return t == 0 ? 0.0f : float(double(progress_.done) / double(t));
    }
    // Terminou: true com o erro (None: pronta).
    bool take(PackError* e, std::string* detail) {
        if (state_ != 2) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        *e = error_;
        *detail = detail_;
        state_ = 0;
        return true;
    }
    const std::wstring& out() const { return out_; }

private:
    std::thread thread_;
    PackProgress progress_;
    std::atomic<int> state_{0};
    std::mutex mutex_;
    PackError error_ = PackError::None;
    std::string detail_;
    std::wstring out_;
};

std::wstring pack_error_text(PackError e, const std::string& detail) {
    const std::wstring d = short_path(ui::widen(detail));
    switch (e) {
        case PackError::None: return {};
        case PackError::InsideGitTree: return T(PcText::SuPackInGit);
        case PackError::SameFile: return T(PcText::SuPackSame);
        case PackError::NotVerified: return T(PcText::SuPackNoCnf);
        case PackError::MissingFile: return Tf(PcText::SuPackMissing, d);
        case PackError::NoSpace: return Tf(PcText::SuPackNoSpace, d);
        case PackError::Write: return Tf(PcText::SuPackWrite, d);
        case PackError::Read: return Tf(PcText::SuPackRead, d);
        case PackError::Cancelled: return T(PcText::SuPackCancelled);
    }
    return {};
}

}  // namespace

SetupResult run_setup(const SetupOptions& o) {
    SetupResult result;
    Window& window = *o.window;
    Renderer& renderer = *o.renderer;
    // O estilo Modern (as fontes e os glifos do hd_ui; os tokens do design/tokens.json).
    renderer.set_modern_ui(o.hd_ui_dir);
    ui::Tokens tokens = ui::default_tokens();
    if (!o.hd_ui_dir.empty()) {
        bool ok = false;
        const std::string json = read_text(os::join(o.hd_ui_dir, L"design/tokens.json"), &ok);
        ui::Tokens t = tokens;
        std::string err;
        if (ok && ui::parse_tokens(json, t, &err)) tokens = t;
    }
    ui::SetupScreen screen(&tokens);
    screen.set_build(o.build);
    const ui::Measure measure = [&renderer](const std::wstring& t, const ui::TextStyle& st) { return renderer.modern_measure(t, st); };

    // O estado: o disco escolhido (ou o último recusado) e os leitores.
    DiscFound chosen = o.found;
    DriveWatch drives(o.title);
    drives.scan_now();
    if (!o.capture) drives.start();
    uint32_t drives_serial = drives.serial();
    std::vector<DriveWatch::Entry> drive_list = drives.drives();
    const std::wstring pack_out = default_pack_path(o.exe);
    std::wstring pack_size;
    const auto plan_for = [&](const DiscSource& src) {
        return pack_plan(src, *o.title, o.exe, o.pack_movies, o.hd_ui_dir, "PORT/HD_UI/");
    };
    const auto refresh_pack = [&] {
        pack_size.clear();
        if (chosen.ok() && personal_copy_supported()) {
            const PackPlan plan = plan_for(*chosen.source);
            if (plan.error == PackError::None) pack_size = ui::setup_size_text(plan.bytes);
        }
    };
    refresh_pack();
    // O primeiro estado: o último recusado da procura (o .ini apontando para uma imagem que sumiu, outra versão no
    // leitor...), sem os leitores (eles têm linha própria).
    std::wstring last_path;
    std::wstring last_problem;
    DiscKind last_kind = DiscKind::Image;
    for (const DiscFound::Rejected& r : o.found.rejected) {
        if (r.origin == DiscOrigin::Drive) continue;
        last_path = r.path;
        last_problem = ui::setup_problem_text(r.check);
        last_kind = r.origin == DiscOrigin::Command || r.origin == DiscOrigin::Settings ? DiscKind::Image : DiscKind::Folder;
    }
    FilePicker picker;
    PackJob pack;
    const auto view = [&] {
        ui::SetupView v;
        v.ready = chosen.ok();
        if (v.ready) {
            v.disc = chosen.source->location();
            v.disc_kind = ui::setup_kind_text(chosen.source->kind());
            v.disc_name = ui::setup_disc_name(chosen.source->kind(), v.disc);
            v.packed = chosen.source->kind() == DiscKind::Packed;
            v.status = ui::setup_ready_text(chosen.check);
        } else if (!last_path.empty()) {
            v.disc = last_path;
            v.disc_kind = ui::setup_kind_text(last_kind);
            v.disc_name = ui::setup_disc_name(last_kind, last_path);
            v.status = last_problem;
            v.problem = true;
        } else {
            v.status = T(PcText::SuNone);
        }
        for (const DriveWatch::Entry& d : drive_list) {
            ui::SetupView::Drive dv;
            dv.root = d.root;
            dv.usable = d.check.ok();
            dv.problem = ui::setup_problem_text(d.check);
            dv.in_use = v.ready && chosen.source->kind() == DiscKind::Drive && os::same_path(chosen.source->location(), d.root);
            v.drives.push_back(dv);
        }
        v.pack_available = personal_copy_supported();
        v.can_pack = v.ready && !pack_size.empty();
        v.pack_size = pack_size;
        v.pack_path = pack_out;
        v.picking = picker.busy();
        return v;
    };
    // Um caminho escolhido (a janela de escolha, um leitor): confere, guarda no .ini e leva o foco para PLAY; senão, a
    // caixa com o motivo.
    const auto choose = [&](const std::wstring& path, DiscOrigin origin) {
        DiscFound f = probe_disc(path, origin, *o.title);
        if (f.ok()) {
            chosen = f;
            last_path.clear();
            if (o.store) o.store(path);
            refresh_pack();
            screen.set_view(view());
            screen.focus_play();
        } else {
            last_path = path;
            last_problem = ui::setup_problem_text(f.check);
            last_kind = f.source != nullptr ? f.source->kind() : DiscKind::Image;
            screen.set_view(view());
            screen.show_message(T(PcText::SuErrTitle), last_problem);
        }
    };

    // O monitor (as resoluções da lista e o tamanho dele): as mesmas listas da página VIDEO do jogo.
    ui::LauncherHardware hardware;
    hardware.modes = renderer.display_modes();
    hardware.desktop = renderer.desktop_mode();
    screen.open(view(), o.settings, hardware);
    window.set_menu_mode(true);
    window.set_cursor_visible(true);
    const float ground[4] = {tokens.color.ground.r, tokens.color.ground.g, tokens.color.ground.b, 1.0f};
    auto last = std::chrono::steady_clock::now();
    float t = 0.0f;
    bool done = false;
    while (!done) {
        if (!window.pump()) {
            result.quit = true;
            break;
        }
        float dt = 1.0f / 60.0f;
        if (!o.capture) {
            const auto now = std::chrono::steady_clock::now();
            dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0f, 0.1f);
            last = now;
        }
        // Nas capturas, o relógio do roteiro para enquanto a cópia anda (ela é de tempo real): a imagem mostra o fim dela.
        if (o.capture && pack.running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            t += dt;
        }
        if (!renderer.offscreen() && window.resized()) renderer.resize(window.width(), window.height());
        // A entrada: a janela, ou o roteiro das capturas.
        SetupFrameInput in;
        if (o.capture) {
            if (o.script) o.script(t, in);
        } else {
            for (int vk = 0; vk < 256; ++vk) in.keys[vk] = window.focused() && window.key_down(vk);
            float mx = 0.0f, my = 0.0f;
            in.mouse_inside = window.cursor_position(&mx, &my);
            in.mouse_x = mx;
            in.mouse_y = my;
            in.wheel = window.take_wheel();
            PadState pads[kMaxPads];
            pad_source().poll(pads);
            for (const PadState& p : pads) {
                if (p.connected) {
                    in.pad = p;
                    break;
                }
            }
        }
        // O aparelho dos símbolos: o controle quando ele é usado.
        static int device = 0;
        if (in.pad.connected && (in.pad.buttons != 0 || std::abs(in.pad.lx) > 12000 || std::abs(in.pad.ly) > 12000)) device = 1;
        for (int vk = 0; vk < 256 && device == 1; ++vk) {
            if (in.keys[vk]) device = 0;
        }
        ui::UiInput ui_in;
        ui_in.keys = in.keys;
        ui_in.mouse_inside = in.mouse_inside;
        ui_in.mouse_x = in.mouse_x;
        ui_in.mouse_y = in.mouse_y;
        ui_in.lmb = in.keys[nf::key::LButton];
        ui_in.back_button = in.keys[nf::key::RButton] || in.keys[nf::key::XButton1];
        ui_in.wheel = in.wheel;
        ui_in.pad = &in.pad;
        ui_in.device = device;
        const ui::Canvas cv = ui::make_canvas(renderer.width(), renderer.height());

        // Os leitores mudaram (um disco entrou ou saiu).
        if (drives.serial() != drives_serial) {
            drives_serial = drives.serial();
            drive_list = drives.drives();
        }
        // A janela de escolha respondeu.
        {
            std::wstring path;
            if (picker.take(&path) && !path.empty()) choose(path, DiscOrigin::Picked);
        }
        // A cópia: o andamento e o fim.
        if (pack.running()) {
            wchar_t pct[64];
            std::swprintf(pct, 64, L"%d", int(pack.progress() * 100.0f));
            std::wstring body = T(PcText::SuPacking);
            const size_t at = body.find(L"%d");
            if (at != std::wstring::npos) body = body.substr(0, at) + pct + body.substr(at + 2);
            const size_t pp = body.find(L"%%");
            if (pp != std::wstring::npos) body.erase(pp, 1);
            screen.show_progress(T(PcText::SuPackingTitle), body, pack.progress());
        } else {
            PackError e = PackError::None;
            std::string detail;
            if (pack.take(&e, &detail)) {
                screen.hide_dialog();
                if (e == PackError::None) {
                    screen.show_message(T(PcText::SuPackDoneTitle),
                                        Tf(PcText::SuPackDone, short_path(pack.out())) + L"\n\n" + T(PcText::SuPackWarning));
                } else {
                    screen.show_message(T(PcText::SuPackFailTitle), pack_error_text(e, detail));
                }
            }
        }
        screen.set_view(view());
        screen.frame(dt, ui_in, cv, measure);
        const ui::SetupRequest req = screen.take_request();
        switch (req.action) {
            case ui::SetupAction::Browse:
                if (!o.pick.empty()) choose(o.pick, DiscOrigin::Picked);
                else if (!o.capture) {
                    picker.start(window.native_handle(),
                                 FilePickerText{T(PcText::SuPickerTitle), T(PcText::SuPickerImages), T(PcText::SuPickerAll)});
                }
                break;
            case ui::SetupAction::Drive:
                if (req.drive >= 0 && size_t(req.drive) < drive_list.size()) {
                    choose(drive_list[size_t(req.drive)].root, DiscOrigin::Drive);
                }
                break;
            case ui::SetupAction::Play:
                if (chosen.ok()) {
                    result.found = chosen;
                    result.settings = screen.settings();
                    result.settings_changed = screen.settings_changed();
                    done = true;
                }
                break;
            case ui::SetupAction::Pack:
                if (chosen.ok()) {
                    PackPlan plan = plan_for(*chosen.source);
                    pack.start(chosen.source, o.title, std::move(plan), o.exe, pack_out);
                }
                break;
            case ui::SetupAction::CancelPack: pack.cancel(); break;
            case ui::SetupAction::Quit:
                result.quit = true;
                done = true;
                break;
            default: break;
        }
        // O desenho: o fundo opaco e a tela (sem cena nem 2D do jogo).
        renderer.clear_target(ground);
        ui::DrawList dl;
        screen.build(dl, cv, measure);
        renderer.draw_modern(dl, cv);
        if (o.capture) {
            if (!done && t + 1e-4f >= o.wait) {
                renderer.save_png(o.screenshot);
                FILE* f = os::open_file(o.screenshot + L".txt", "w");
                if (f != nullptr) {
                    std::fprintf(f, "%s\n", screen.describe().c_str());
                    std::fprintf(f, "tela_entrada motivo %s monitor %dx%d modos %zu\n", ui::launcher_reason_name(o.reason),
                                 hardware.desktop.width, hardware.desktop.height, hardware.modes.size());
                    std::fprintf(f, "disco escolhido %s %s\n", chosen.ok() ? "ok" : "nenhum",
                                 chosen.ok() ? chosen.source->describe().c_str() : "-");
                    for (const DiscFound::Rejected& r : o.found.rejected) {
                        std::fprintf(f, "recusado %s %s %s %s\n", disc_origin_name(r.origin), disc_status_name(r.check.status),
                                     r.check.boot_id.empty() ? "-" : r.check.boot_id.c_str(), narrow_utf8(r.path).c_str());
                    }
                    for (const DriveWatch::Entry& d : drive_list) {
                        std::fprintf(f, "leitor %s %s %s\n", narrow_utf8(d.root).c_str(), disc_status_name(d.check.status),
                                     d.check.boot_id.empty() ? "-" : d.check.boot_id.c_str());
                    }
                    std::fprintf(f, "moderno desenho %s\n", renderer.modern_describe().c_str());
                    std::fclose(f);
                }
                result.captured = true;
                done = true;
            }
        } else {
            renderer.present();
            // Sem VSync, sem girar à toa (a tela não precisa de mais de ~120 quadros).
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
    pack.cancel();
    window.set_menu_mode(false);
    return result;
}

}  // namespace nf
