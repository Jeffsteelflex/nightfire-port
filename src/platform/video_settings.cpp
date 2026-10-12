// Opções de vídeo e de interface do PC (só do port). Ver video_settings.h.

#include "platform/video_settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "platform/settings.h"

namespace nf {

namespace {

struct FilterName {
    TextureFilter filter;
    const char* name;
    int aniso;
};
constexpr FilterName kFilters[] = {
    {TextureFilter::Bilinear, "bilinear", 0}, {TextureFilter::Trilinear, "trilinear", 0},
    {TextureFilter::Aniso2, "aniso2", 2},     {TextureFilter::Aniso4, "aniso4", 4},
    {TextureFilter::Aniso8, "aniso8", 8},     {TextureFilter::Aniso16, "aniso16", 16},
};

const char* const kWindowModes[] = {"windowed", "borderless", "exclusive"};

std::vector<int> with_current(std::vector<int> values, int current) {
    if (std::find(values.begin(), values.end(), current) == values.end()) {
        values.insert(std::upper_bound(values.begin(), values.end(), current), current);
    }
    return values;
}

}  // namespace

int texture_filter_anisotropy(TextureFilter f) {
    for (const FilterName& n : kFilters) {
        if (n.filter == f) return n.aniso;
    }
    return 0;
}

const char* texture_filter_name(TextureFilter f) {
    for (const FilterName& n : kFilters) {
        if (n.filter == f) return n.name;
    }
    return "aniso8";
}

bool parse_texture_filter(const std::string& s, TextureFilter* out) {
    for (const FilterName& n : kFilters) {
        if (s == n.name) {
            *out = n.filter;
            return true;
        }
    }
    return false;
}

const char* window_mode_name(WindowMode m) {
    const size_t i = size_t(m);
    return i < 3 ? kWindowModes[i] : kWindowModes[0];
}

const char* menu_style_name(MenuStyle m) { return m == MenuStyle::Original ? "original" : "modern"; }

bool parse_menu_style(const std::string& s, MenuStyle* out) {
    if (s == "modern") {
        *out = MenuStyle::Modern;
        return true;
    }
    if (s == "original") {
        *out = MenuStyle::Original;
        return true;
    }
    return false;
}

bool parse_window_mode(const std::string& s, WindowMode* out) {
    for (size_t i = 0; i < 3; ++i) {
        if (s == kWindowModes[i]) {
            *out = WindowMode(i);
            return true;
        }
    }
    return false;
}

void load_video_settings(const Settings& s, VideoSettings& v) {
    v.width = std::max(320, s.get_int("video", "width", v.width));
    v.height = std::max(240, s.get_int("video", "height", v.height));
    // Antes de window_mode havia só "fullscreen" (a janela sem bordas).
    if (s.has("video", "fullscreen")) {
        v.window_mode = s.get_bool("video", "fullscreen", false) ? WindowMode::Borderless : WindowMode::Windowed;
    }
    parse_window_mode(s.get("video", "window_mode"), &v.window_mode);
    v.vsync = s.get_bool("video", "vsync", v.vsync);
    v.render_scale = std::clamp(s.get_int("video", "render_scale", v.render_scale), 25, 400);
    const int msaa = s.get_int("video", "msaa", v.msaa);
    v.msaa = msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 1;
    parse_texture_filter(s.get("video", "texture_filter"), &v.filter);
    v.fov = std::clamp(s.get_float("video", "fov", v.fov), 20.0f, 140.0f);
    v.hd_textures = s.get_bool("video", "hd_textures", v.hd_textures);
    parse_fps_cap(s.get("video", "fps_cap"), &v.fps_cap);
    v.interpolation = s.get_bool("video", "interpolation", v.interpolation);
    v.instant_mouse_look = s.get_bool("video", "instant_mouse_look", v.instant_mouse_look);
    if (AdapterPreference pref; parse_adapter_preference(s.get("video", "adapter"), &pref)) {
        v.adapter = adapter_preference_name(pref);
    }
    v.hd_ui = s.get_bool("ui", "hd", v.hd_ui);
    const std::string anchor = s.get("ui", "anchor");
    if (anchor == "edges") v.hud_edges = true;
    if (anchor == "centre" || anchor == "center") v.hud_edges = false;
    const std::string lang = s.get("ui", "language_pc");
    if (lang == "pt" || lang == "en") v.language = lang;
    parse_menu_style(s.get("ui", "menu_style"), &v.menu_style);
}

void store_video_settings(const VideoSettings& v, Settings& s) {
    s.set_int("video", "width", v.width);
    s.set_int("video", "height", v.height);
    s.set("video", "window_mode", window_mode_name(v.window_mode));
    s.remove("video", "fullscreen");  // substituída por window_mode
    s.set_bool("video", "vsync", v.vsync);
    s.set_int("video", "render_scale", v.render_scale);
    s.set_int("video", "msaa", v.msaa);
    s.set("video", "texture_filter", texture_filter_name(v.filter));
    s.set_float("video", "fov", v.fov);
    s.set_bool("video", "hd_textures", v.hd_textures);
    s.set("video", "fps_cap", fps_cap_name(v.fps_cap));
    s.set_bool("video", "interpolation", v.interpolation);
    s.set_bool("video", "instant_mouse_look", v.instant_mouse_look);
    s.set("video", "adapter", v.adapter);
    s.set_bool("ui", "hd", v.hd_ui);
    s.set("ui", "anchor", v.hud_edges ? "edges" : "centre");
    s.set("ui", "language_pc", v.language);
    s.set("ui", "menu_style", menu_style_name(v.menu_style));
}

void fit_resolution_to_mode(VideoSettings& v, const std::vector<DisplayMode>& modes, DisplayMode desktop) {
    if (v.window_mode == WindowMode::Windowed && desktop.width > 0 && (v.width > desktop.width || v.height > desktop.height)) {
        const std::vector<DisplayMode> fit = resolution_choices(modes, desktop, WindowMode::Windowed, DisplayMode{});
        if (!fit.empty()) {
            v.width = fit.back().width;
            v.height = fit.back().height;
        }
    }
    if (v.window_mode == WindowMode::Exclusive && !modes.empty()) {
        bool known = false;
        for (const DisplayMode& m : modes) known = known || (m.width == v.width && m.height == v.height);
        if (!known && desktop.width > 0) {
            v.width = desktop.width;
            v.height = desktop.height;
        }
    }
}

bool video_needs_confirmation(const VideoSettings& from, const VideoSettings& to) {
    return from.width != to.width || from.height != to.height || from.window_mode != to.window_mode;
}

std::vector<DisplayMode> build_mode_list(std::vector<DisplayMode> raw, DisplayMode current) {
    raw.erase(std::remove_if(raw.begin(), raw.end(), [](const DisplayMode& m) { return m.width < 640 || m.height < 480; }),
              raw.end());
    if (current.width > 0 && current.height > 0) raw.push_back(current);
    std::sort(raw.begin(), raw.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
    return raw;
}

std::vector<DisplayMode> resolution_choices(const std::vector<DisplayMode>& modes, DisplayMode desktop, WindowMode mode,
                                            DisplayMode current) {
    if (mode == WindowMode::Borderless) return {desktop.width > 0 ? desktop : current};
    std::vector<DisplayMode> out;
    for (const DisplayMode& m : modes) {
        const bool fits = desktop.width <= 0 || (m.width <= desktop.width && m.height <= desktop.height);
        if (mode == WindowMode::Exclusive || fits) out.push_back(m);
    }
    return build_mode_list(out, current);
}

DisplayMode cycle_mode(const std::vector<DisplayMode>& modes, DisplayMode current, int dir) {
    if (modes.empty()) return current;
    const auto it = std::find(modes.begin(), modes.end(), current);
    if (it == modes.end()) {
        // O primeiro maior (indo para a frente) ou o último menor (para trás), pela área.
        const int64_t area = int64_t(current.width) * current.height;
        if (dir >= 0) {
            for (const DisplayMode& m : modes) {
                if (int64_t(m.width) * m.height > area) return m;
            }
            return modes.front();
        }
        for (auto r = modes.rbegin(); r != modes.rend(); ++r) {
            if (int64_t(r->width) * r->height < area) return *r;
        }
        return modes.back();
    }
    const int n = int(modes.size());
    const int i = int(it - modes.begin());
    return modes[size_t(((i + (dir < 0 ? -1 : 1)) % n + n) % n)];
}

std::vector<std::string> adapter_choice_values(const std::vector<AdapterInfo>& list, const std::string& current) {
    std::vector<std::string> v{"auto", "integrated", "discrete"};
    for (const AdapterInfo& a : list) {
        if (a.software) continue;
        const std::string value = a.name.empty() ? std::to_string(a.index) : a.name;
        if (std::find(v.begin(), v.end(), value) == v.end()) v.push_back(value);
    }
    if (std::find(v.begin(), v.end(), current) == v.end() && !current.empty()) v.push_back(current);
    return v;
}

std::string adapter_short_name(const std::string& name) {
    std::string s = name;
    for (const char* cut : {"(TM)", "(tm)", "(R)", "(r)", "GeForce "}) {
        for (size_t at = s.find(cut); at != std::string::npos; at = s.find(cut)) s.erase(at, std::strlen(cut));
    }
    // O fabricante no começo também sai (a coluna da página Original, na fonte 3, cabe ~16 letras): "RTX 5090",
    // "Radeon Graphics", "UHD Graphics 770"; sem nada depois dele, o nome fica.
    for (const char* vendor : {"NVIDIA ", "AMD ", "Intel "}) {
        const size_t n = std::strlen(vendor);
        if (s.compare(0, n, vendor) == 0 && s.find_first_not_of(' ', n) != std::string::npos) s.erase(0, n);
    }
    std::string out;
    for (const char c : s) {
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<int> render_scale_values(int current) { return with_current({50, 75, 100, 125, 150, 200}, current); }

std::vector<int> msaa_values(const std::vector<int>& supported, int current) {
    std::vector<int> v{1};
    for (int n : {2, 4, 8}) {
        if (std::find(supported.begin(), supported.end(), n) != supported.end()) v.push_back(n);
    }
    return with_current(v, current);
}

std::vector<int> fov_values(int current) {
    std::vector<int> v;
    for (int f = 40; f <= 100; f += 5) v.push_back(f);
    return with_current(v, current);
}

std::vector<int> fps_cap_values(int current) { return with_current({0, 30, 60, 120, 144, 165, 240}, current); }

std::string fps_cap_name(int cap) { return cap <= 0 ? std::string("off") : std::to_string(cap); }

bool parse_fps_cap(const std::string& s, int* out) {
    if (s == "off" || s == "unlimited" || s == "0") {
        *out = 0;
        return true;
    }
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos || s.size() > 4) return false;
    const int v = std::stoi(s);
    if (v < 10 || v > 1000) return false;
    *out = v;
    return true;
}

int cycle_value(const std::vector<int>& values, int current, int dir) {
    if (values.empty()) return current;
    const auto it = std::find(values.begin(), values.end(), current);
    if (it == values.end()) return dir < 0 ? values.back() : values.front();
    const int n = int(values.size());
    const int i = int(it - values.begin());
    return values[size_t(((i + (dir < 0 ? -1 : 1)) % n + n) % n)];
}

// ---- controlador ----

void VideoController::init(const VideoSettings& live) {
    live_ = live;
    before_ = live;
    confirming_ = false;
    remaining_ = 0.0f;
    stats_ = Stats{};
}

bool VideoController::apply(const VideoSettings& next) {
    // A anterior de uma confirmação em andamento é a de antes dela (aplicar de novo não a perde).
    const VideoSettings base = confirming_ ? before_ : live_;
    const VideoSettings prev = live_;
    live_ = next;
    ++stats_.applied;
    if (apply_) apply_(live_, prev);
    if (video_needs_confirmation(base, live_)) {
        before_ = base;
        confirming_ = true;
        remaining_ = kVideoConfirmSeconds;
        return true;
    }
    confirming_ = false;
    remaining_ = 0.0f;
    store();
    return false;
}

bool VideoController::tick(float seconds) {
    if (!confirming_) return false;
    remaining_ -= seconds;
    if (remaining_ > 0.0f) return false;
    revert();
    return true;
}

void VideoController::keep() {
    if (!confirming_) return;
    confirming_ = false;
    remaining_ = 0.0f;
    ++stats_.kept;
    store();
}

void VideoController::revert() {
    if (!confirming_) return;
    confirming_ = false;
    remaining_ = 0.0f;
    const VideoSettings prev = live_;
    live_ = before_;
    ++stats_.reverted;
    if (apply_) apply_(live_, prev);
}

void VideoController::store() {
    if (store_ && store_(live_)) ++stats_.stored;
}

VideoController& video_controller() {
    static VideoController c;
    return c;
}

}  // namespace nf
