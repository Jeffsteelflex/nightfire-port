// Camada 2D em alta definição no nightfire.exe. Ver ui_hd_app.h.

#include "render/ui_hd_app.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <vector>

#include "platform/psi_draw.h"
#include "render/renderer.h"
#include "render/runtime_gfx.h"
#include "render/ui_hd_game.h"

namespace nf {

namespace {

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string read_text(const std::wstring& path, bool* ok) {
    std::string s;
    *ok = false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    *ok = true;
    return s;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? char(c) : '?');
    return s;
}

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace

// hd_ui: a pasta pedida, ou a primeira que tiver map.json entre a do executável e as de cima (o repositório,
// a partir de build/Release), a dos dados e a atual.
std::wstring UiHdApp::find_dir(const std::wstring& option, const std::wstring& data_dir) {
    if (!option.empty()) return option;
    std::vector<std::wstring> candidates;
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir = buf;
    for (int up = 0; up < 4; ++up) {
        const size_t cut = dir.find_last_of(L"\\/");
        if (cut == std::wstring::npos) break;
        dir = dir.substr(0, cut);
        candidates.push_back(dir + L"\\hd_ui");
    }
    candidates.push_back(data_dir + L"\\hd_ui");
    GetCurrentDirectoryW(MAX_PATH, buf);
    candidates.push_back(std::wstring(buf) + L"\\hd_ui");
    for (const std::wstring& c : candidates) {
        if (exists(c + L"\\map.json")) return c;
    }
    return std::wstring();
}

UiHdApp::UiHdApp() = default;
UiHdApp::~UiHdApp() = default;

void UiHdApp::init(const UiHdOptions& options, const std::wstring& data_dir, Renderer& renderer, RuntimeGfx& gfx) {
    renderer_ = &renderer;
    gfx_ = &gfx;
    dir_ = find_dir(options.dir, data_dir);
    if (dir_.empty()) {
        error_ = "pasta hd_ui com map.json não encontrada";
    } else {
        bool ok = false;
        const std::string json = read_text(dir_ + L"\\map.json", &ok);
        std::string err;
        if (!ok) {
            error_ = "não li " + narrow(dir_) + "\\map.json";
        } else if (!ui_hd_parse_config(json, config_, &err)) {
            config_ = UiHdConfig{};
            error_ = "map.json: " + err;
        }
    }
    if (options.anchor == "edges") config_.anchor = UiHdAnchor::Edges;
    if (options.anchor == "centre" || options.anchor == "center") config_.anchor = UiHdAnchor::Centre;
    renderer.set_ui_hd(&config_, dir_);
    // Sem Direct2D (Windows antigo, dispositivo sem BGRA) a camada não liga: o 2D original continua.
    enabled_ = options.enabled && renderer.ui_hd_ok();
    gfx.set_ui_hd(enabled_ ? &config_ : nullptr);
    if (!options.inventory.empty()) {
        inventory_dir_ = options.inventory;
        std::error_code ec;
        std::filesystem::create_directories(inventory_dir_, ec);
        inventory_ = std::make_unique<UiHdInventory>();
        bool ok = false;
        const std::string tsv = read_text(inventory_dir_ + L"\\inventory.tsv", &ok);
        if (ok) inventory_->load(tsv);
        saved_entries_ = inventory_->entries().size();
    }
}

void UiHdApp::toggle() {
    enabled_ = !enabled_ && renderer_ != nullptr && renderer_->ui_hd_ok();
    if (gfx_ != nullptr) gfx_->set_ui_hd(enabled_ ? &config_ : nullptr);
}

void UiHdApp::after_collect(Renderer& renderer) {
    if (inventory_ == nullptr) return;
    inventory_->add_frame(g_psi_frame.quads2d.data(), g_psi_frame.quads2d.size(),
                          ui_hd_game_env(renderer.width(), renderer.height()));
    // Recorta as imagens vistas pela primeira vez nesta execução, enquanto a textura delas está carregada
    // (o slot de Tex muda de dono na troca de fase). Os texels vão para o PNG com o alfa do GS dobrado.
    std::set<std::string> done(cropped_.begin(), cropped_.end());
    int read_slot = -2, tw = 0, th = 0;
    std::vector<uint8_t> pixels;
    bool changed = inventory_->entries().size() != saved_entries_;
    for (const UiHdInventory::Entry& e : inventory_->entries()) {
        if (!e.seen_now || e.slot < 0) continue;
        char name[96];
        std::snprintf(name, sizeof(name), "%08x_%d_%d_%dx%d.png", e.hash, e.u, e.v, e.w, e.h);
        if (done.count(name) != 0) continue;
        if (read_slot != e.slot) {
            read_slot = e.slot;
            if (!renderer.read_texture(e.slot, tw, th, pixels)) tw = th = 0;
            for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = uint8_t(std::min(255, pixels[i] * 2));
            char tex_name[64];
            std::snprintf(tex_name, sizeof(tex_name), "tex_%08x.png", e.hash);
            if (tw > 0 && done.count(tex_name) == 0) {
                Renderer::write_png(inventory_dir_ + L"\\" + widen(tex_name), tw, th, pixels.data());
                cropped_.push_back(tex_name);
                done.insert(tex_name);
            }
        }
        cropped_.push_back(name);
        done.insert(name);
        if (tw <= 0) continue;
        const int x0 = std::clamp(e.u, 0, tw - 1), y0 = std::clamp(e.v, 0, th - 1);
        const int x1 = std::clamp(e.u + e.w, x0, tw - 1), y1 = std::clamp(e.v + e.h, y0, th - 1);
        const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
        std::vector<uint8_t> crop(size_t(cw) * size_t(ch) * 4);
        for (int y = 0; y < ch; ++y) {
            std::memcpy(&crop[size_t(y) * size_t(cw) * 4], &pixels[(size_t(y0 + y) * size_t(tw) + size_t(x0)) * 4],
                        size_t(cw) * 4);
        }
        Renderer::write_png(inventory_dir_ + L"\\" + widen(name), cw, ch, crop.data());
        changed = true;
    }
    if (changed) save_inventory();
}

void UiHdApp::save_inventory() {
    if (inventory_ == nullptr) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (inventory_dir_ + L"\\inventory.tsv").c_str(), L"wb") != 0 || f == nullptr) return;
    const std::string tsv = inventory_->to_tsv();
    std::fwrite(tsv.data(), 1, tsv.size(), f);
    std::fclose(f);
    saved_entries_ = inventory_->entries().size();
}

void UiHdApp::finish() { save_inventory(); }

std::string UiHdApp::status() const {
    std::string s = enabled_ ? "ligada" : "desligada";
    s += " pasta " + (dir_.empty() ? std::string("-") : narrow(dir_));
    s += " ancora ";
    s += config_.anchor == UiHdAnchor::Edges ? "bordas" : "centro";
    s += " substitutas " + std::to_string(config_.images.size());
    s += std::string(" simbolos ") + ui_hd_prompt_set_name(UiHd_PromptSetting()) + "/" +
         ui_hd_prompt_set_name(ui_hd_prompt_effective());
    if (renderer_ != nullptr) s += " desenho " + renderer_->ui_hd_describe();
    if (!error_.empty()) s += " erro: " + error_;
    if (inventory_ != nullptr) s += " inventario " + std::to_string(inventory_->entries().size());
    return s;
}

}  // namespace nf
