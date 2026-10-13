// O que a etapa 3c do macOS trouxe (docs/port.md, "macOS: os backends"), conferido onde faz sentido em cada plataforma:
// a tabela das teclas e a roda do macOS (platform/mac_keys.h: só dados, em toda plataforma), a pasta dos arquivos do
// jogador (os::user_files_dir: a do executável no Windows e no Linux, o Application Support no macOS), os volumes com um
// disco de PS2 (o /Volumes do macOS simulado numa pasta temporária), o PNG gravado e lido de volta (o WIC no Windows, o
// ImageIO no macOS; nada nas builds headless), e só no programa de janela do macOS o som (o Core Audio abre ou fica mudo)
// e os controles (o GameController sem nenhum ligado). NF_TEST_SYSTEM_BACKENDS (CMakeLists.txt) diz se a build tem os
// backends do sistema (1) ou os nulos (0).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

// A matemática da câmera do laço fora do Windows (src/app/xmath_compat.h, em nf::xm) contra o DirectXMath de verdade (só
// no Windows, onde o SDK o traz).
#define NF_XMATH_COMPAT_NO_ALIAS
#include "app/xmath_compat.h"
#if defined(_WIN32)
#include <DirectXMath.h>
#endif

#include "game/boot.h"
#include "platform/app_log.h"
#include "platform/audio_device.h"
#include "platform/disc_pack.h"
#include "platform/disc_source.h"
#include "platform/gamepad.h"
#include "platform/image_io.h"
#include "platform/keys.h"
#include "platform/mac_keys.h"
#include "platform/os.h"
#include "platform/psi_card.h"
#include "platform/settings.h"
#include "test_framework.h"

#if defined(__APPLE__) && NF_TEST_SYSTEM_BACKENDS
#include "platform/app_log_macos.h"
#endif

#ifndef NF_TEST_SYSTEM_BACKENDS
#define NF_TEST_SYSTEM_BACKENDS 0
#endif

namespace {

namespace fs = std::filesystem;
namespace kvk = nf::mac::kvk;

fs::path temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / "nf_tests_macos" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    FILE* f = nf::os::open_file(p.wstring(), "wb");
    if (f == nullptr) return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

[[maybe_unused]] bool ends_with_sep(const std::wstring& s) { return !s.empty() && (s.back() == nf::os::kPathSep || s.back() == L'/'); }

}  // namespace

TEST(mac_keys_each_key_once_and_the_right_code) {
    // Cada kVK uma vez só; o mesmo VK em duas teclas só no Enter (o Return e o do teclado numérico, como no Windows).
    std::set<uint16_t> macs;
    std::vector<int> vk_count(256, 0);
    for (const nf::mac::MacKey& k : nf::mac::kMacKeys) {
        CHECK(macs.insert(k.mac).second);
        CHECK(k.vk != 0);
        ++vk_count[k.vk];
    }
    for (int vk = 0; vk < 256; ++vk) {
        if (vk == nf::key::Return) {
            CHECK(vk_count[size_t(vk)] == 2);
        } else {
            CHECK(vk_count[size_t(vk)] <= 1);
        }
    }
    // As letras e os dígitos são o caractere; a posição física (kVK) não é a ordem alfabética.
    CHECK(nf::mac::vk_of_mac(kvk::A) == 'A' && nf::mac::vk_of_mac(kvk::Z) == 'Z' && nf::mac::vk_of_mac(kvk::Q) == 'Q');
    CHECK(nf::mac::vk_of_mac(kvk::N0) == '0' && nf::mac::vk_of_mac(kvk::N5) == '5' && nf::mac::vk_of_mac(kvk::N9) == '9');
    // As de controle com os nomes do Mac.
    CHECK(nf::mac::vk_of_mac(kvk::Return) == nf::key::Return && nf::mac::vk_of_mac(kvk::KeypadEnter) == nf::key::Return);
    CHECK(nf::mac::vk_of_mac(kvk::Delete) == nf::key::Back);
    CHECK(nf::mac::vk_of_mac(kvk::ForwardDelete) == nf::key::Delete);
    CHECK(nf::mac::vk_of_mac(kvk::Help) == nf::key::Insert);
    CHECK(nf::mac::vk_of_mac(kvk::Escape) == nf::key::Escape && nf::mac::vk_of_mac(kvk::Tab) == nf::key::Tab);
    CHECK(nf::mac::vk_of_mac(kvk::UpArrow) == nf::key::Up && nf::mac::vk_of_mac(kvk::DownArrow) == nf::key::Down);
    CHECK(nf::mac::vk_of_mac(kvk::LeftArrow) == nf::key::Left && nf::mac::vk_of_mac(kvk::RightArrow) == nf::key::Right);
    CHECK(nf::mac::vk_of_mac(kvk::PageUp) == nf::key::Prior && nf::mac::vk_of_mac(kvk::PageDown) == nf::key::Next);
    // F1 a F20 seguidas, o teclado numérico, a pontuação pela posição do teclado americano.
    const uint16_t fkeys[20] = {kvk::F1,  kvk::F2,  kvk::F3,  kvk::F4,  kvk::F5,  kvk::F6,  kvk::F7,
                                kvk::F8,  kvk::F9,  kvk::F10, kvk::F11, kvk::F12, kvk::F13, kvk::F14,
                                kvk::F15, kvk::F16, kvk::F17, kvk::F18, kvk::F19, kvk::F20};
    for (int i = 0; i < 20; ++i) CHECK(nf::mac::vk_of_mac(fkeys[i]) == nf::key::F1 + i);
    const uint16_t pad[10] = {kvk::Keypad0, kvk::Keypad1, kvk::Keypad2, kvk::Keypad3, kvk::Keypad4,
                              kvk::Keypad5, kvk::Keypad6, kvk::Keypad7, kvk::Keypad8, kvk::Keypad9};
    for (int i = 0; i < 10; ++i) CHECK(nf::mac::vk_of_mac(pad[i]) == nf::key::Numpad0 + i);
    CHECK(nf::mac::vk_of_mac(kvk::Semicolon) == nf::key::Oem1 && nf::mac::vk_of_mac(kvk::Grave) == nf::key::Oem3);
    CHECK(nf::mac::vk_of_mac(kvk::Backslash) == nf::key::Oem5 && nf::mac::vk_of_mac(kvk::Quote) == nf::key::Oem7);
    CHECK(nf::mac::vk_of_mac(kvk::IsoSection) == nf::key::Oem102);
    // Os modificadores: cada lado, e o genérico junto (o Command é a tecla Windows, o Option o Alt).
    const nf::mac::MacKey* ls = nf::mac::mac_key(kvk::Shift);
    const nf::mac::MacKey* rc = nf::mac::mac_key(kvk::RightControl);
    const nf::mac::MacKey* lo = nf::mac::mac_key(kvk::Option);
    CHECK(ls != nullptr && ls->vk == nf::key::LShift && ls->generic == nf::key::Shift);
    CHECK(rc != nullptr && rc->vk == nf::key::RControl && rc->generic == nf::key::Control);
    CHECK(lo != nullptr && lo->vk == nf::key::LMenu && lo->generic == nf::key::Menu);
    CHECK(nf::mac::vk_of_mac(kvk::Command) == nf::key::LWin && nf::mac::vk_of_mac(kvk::RightCommand) == nf::key::RWin);
    CHECK(nf::mac::vk_of_mac(kvk::Function) == 0);  // o Fn não chega ao jogo

    // Toda tecla que a tabela de teclas do port nomeia e um teclado da Apple tem sai de alguma tecla do Mac (os genéricos
    // pelo campo generic). O Mac não tem Pause, Scroll Lock nem Print Screen; a tecla / do ABNT2 (0xC1) não existe nos
    // teclados da Apple.
    std::vector<int> wanted = {nf::key::Space,    nf::key::Return,   nf::key::Tab,      nf::key::Back,
                               nf::key::Escape,   nf::key::LShift,   nf::key::RShift,   nf::key::LControl,
                               nf::key::RControl, nf::key::LMenu,    nf::key::RMenu,    nf::key::Up,
                               nf::key::Down,     nf::key::Left,     nf::key::Right,    nf::key::Insert,
                               nf::key::Delete,   nf::key::Home,     nf::key::End,      nf::key::Prior,
                               nf::key::Next,     nf::key::Capital,  nf::key::Multiply, nf::key::Add,
                               nf::key::Subtract, nf::key::Decimal,  nf::key::Divide,   nf::key::Oem1,
                               nf::key::OemPlus,  nf::key::OemComma, nf::key::OemMinus, nf::key::OemPeriod,
                               nf::key::Oem2,     nf::key::Oem3,     nf::key::Oem4,     nf::key::Oem5,
                               nf::key::Oem6,     nf::key::Oem7,     nf::key::Oem102,   nf::key::LWin,
                               nf::key::RWin,     nf::key::Apps};
    for (int c = 'A'; c <= 'Z'; ++c) wanted.push_back(c);
    for (int c = '0'; c <= '9'; ++c) wanted.push_back(c);
    for (int f = 0; f < 20; ++f) wanted.push_back(nf::key::F1 + f);
    for (int n = 0; n < 10; ++n) wanted.push_back(nf::key::Numpad0 + n);
    for (int vk : wanted) {
        if (vk_count[size_t(vk)] == 0) std::printf("  sem tecla do Mac: VK 0x%02X\n", vk);
        CHECK(vk_count[size_t(vk)] >= 1);
    }
    for (int g : {nf::key::Shift, nf::key::Control, nf::key::Menu}) {
        int sides = 0;
        for (const nf::mac::MacKey& k : nf::mac::kMacKeys) sides += k.generic == g;
        CHECK(sides == 2);
    }
    // De volta: o kVK de cada código (o Return pelo Return; os genéricos pelo lado esquerdo).
    for (const nf::mac::MacKey& k : nf::mac::kMacKeys) {
        if (k.mac == kvk::KeypadEnter) continue;
        CHECK(nf::mac::mac_of_vk(k.vk) == k.mac);
    }
    CHECK(nf::mac::mac_of_vk(nf::key::Shift) == kvk::Shift && nf::mac::mac_of_vk(nf::key::Menu) == kvk::Option);
    CHECK(nf::mac::mac_of_vk(0xC1) == 0xFFFF);
}

TEST(xmath_compat_camera_matrices) {
    namespace xm = nf::xm;
    // Câmeras quaisquer (olho, frente, cima, campo de visão, proporção) e os planos da profundidade invertida do laço.
    struct Cam {
        float eye[3], fwd[3], up[3], fov, aspect;
    };
    const Cam cams[] = {
        {{0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, 16.0f / 9.0f},
        {{12.5f, -3.25f, 99.0f}, {0.6f, -0.2f, -0.774597f}, {0.1f, 0.98f, -0.17f}, 75.0f, 21.0f / 9.0f},
        {{-410.0f, 22.0f, 7.5f}, {-0.9f, 0.3f, 0.316228f}, {0.3f, 0.95f, 0.08f}, 30.5f, 4.0f / 3.0f},
    };
    const float z_near = 0.05f, z_far = 20000.0f;
    for (const Cam& c : cams) {
        const xm::XMVECTOR eye = xm::XMVectorSet(c.eye[0], c.eye[1], c.eye[2], 1.0f);
        const xm::XMVECTOR fwd = xm::XMVectorSet(c.fwd[0], c.fwd[1], c.fwd[2], 0.0f);
        const xm::XMVECTOR up = xm::XMVectorSet(c.up[0], c.up[1], c.up[2], 0.0f);
        const xm::XMMATRIX view = xm::XMMatrixLookToRH(eye, fwd, up);
        const xm::XMMATRIX proj = xm::XMMatrixPerspectiveFovRH(xm::XMConvertToRadians(c.fov), c.aspect, z_far, z_near);
        xm::XMFLOAT4X4 vp;
        xm::XMStoreFloat4x4(&vp, view * proj);
        // As propriedades: o olho vai à origem da visão e a frente ao -z (mão direita).
        const auto apply = [&view](float x, float y, float z, int col) {
            return x * view.r[0].v[col] + y * view.r[1].v[col] + z * view.r[2].v[col] + view.r[3].v[col];
        };
        for (int k = 0; k < 3; ++k) CHECK_NEAR(apply(c.eye[0], c.eye[1], c.eye[2], k), 0.0, 1e-3);
        const float len = std::sqrt(c.fwd[0] * c.fwd[0] + c.fwd[1] * c.fwd[1] + c.fwd[2] * c.fwd[2]);
        const float ahead[3] = {c.eye[0] + 2.0f * c.fwd[0] / len, c.eye[1] + 2.0f * c.fwd[1] / len,
                                c.eye[2] + 2.0f * c.fwd[2] / len};
        CHECK_NEAR(apply(ahead[0], ahead[1], ahead[2], 2), -2.0, 1e-3);
        CHECK_NEAR(apply(ahead[0], ahead[1], ahead[2], 0), 0.0, 1e-3);
#if defined(_WIN32)
        // O DirectXMath do SDK (o caminho SSE): o seno e o cosseno e a projeção iguais bit a bit (a mesma conta escalar),
        // a visão e o produto a 1 ulp (a normalização e a ordem das somas do SSE).
        namespace dx = DirectX;
        const dx::XMMATRIX dview =
            dx::XMMatrixLookToRH(dx::XMVectorSet(c.eye[0], c.eye[1], c.eye[2], 1.0f),
                                 dx::XMVectorSet(c.fwd[0], c.fwd[1], c.fwd[2], 0.0f),
                                 dx::XMVectorSet(c.up[0], c.up[1], c.up[2], 0.0f));
        const dx::XMMATRIX dproj =
            dx::XMMatrixPerspectiveFovRH(dx::XMConvertToRadians(c.fov), c.aspect, z_far, z_near);
        dx::XMFLOAT4X4 dv, dp, dvp;
        dx::XMStoreFloat4x4(&dv, dview);
        dx::XMStoreFloat4x4(&dp, dproj);
        dx::XMStoreFloat4x4(&dvp, dview * dproj);
        xm::XMFLOAT4X4 v4, p4;
        xm::XMStoreFloat4x4(&v4, view);
        xm::XMStoreFloat4x4(&p4, proj);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                CHECK(p4.m[i][j] == dp.m[i][j]);
                CHECK_NEAR(v4.m[i][j], dv.m[i][j], 1e-6 * std::max(1.0, std::fabs(double(dv.m[i][j]))));
                CHECK_NEAR(vp.m[i][j], dvp.m[i][j], 2e-6 * std::max(1.0, std::fabs(double(dvp.m[i][j]))));
            }
        }
#endif
    }
    // O seno e o cosseno do DirectXMath (os polinômios dele, não o sinf).
    for (int i = -200; i <= 200; ++i) {
        const float a = float(i) * 0.0731f;
        float s = 0.0f, co = 0.0f;
        xm::XMScalarSinCos(&s, &co, a);
        CHECK_NEAR(s, std::sin(double(a)), 2e-6);
        CHECK_NEAR(co, std::cos(double(a)), 2e-6);
#if defined(_WIN32)
        float ds = 0.0f, dc = 0.0f;
        DirectX::XMScalarSinCos(&ds, &dc, a);
        CHECK(s == ds && co == dc);
#endif
    }
}

TEST(mac_wheel_lines_and_points_to_notches) {
    nf::mac::MacWheel w;
    // Uma roda comum: linhas, já com a aceleração do sistema; menos de uma linha conta um entalhe.
    CHECK(w.add(1.0, false) == 1.0);
    CHECK(w.add(-3.5, false) == -3.5);
    CHECK(w.add(0.1, false) == 1.0);
    CHECK(w.add(-0.25, false) == -1.0);
    CHECK(w.add(0.0, false) == 0.0);
    // O trackpad e o Magic Mouse: pontos somados até um entalhe (kMacWheelPointsPerNotch), o resto fica.
    const double n = nf::mac::kMacWheelPointsPerNotch;
    CHECK(w.add(n * 0.4, true) == 0.0);
    CHECK(w.add(n * 0.4, true) == 0.0);
    CHECK(w.add(n * 0.4, true) == 1.0);
    CHECK_NEAR(w.pending(), n * 0.2, 1e-9);
    // A troca de sentido esquece o resto do outro sentido.
    CHECK(w.add(-n * 0.5, true) == 0.0);
    CHECK_NEAR(w.pending(), -n * 0.5, 1e-9);
    CHECK(w.add(-n * 2.0, true) == -2.0);
    CHECK_NEAR(w.pending(), -n * 0.5, 1e-9);
    w.reset();
    CHECK(w.pending() == 0.0);
    CHECK(w.add(n * 3.25, true) == 3.0);
}

TEST(user_files_dir_is_where_the_player_files_go) {
    const std::wstring dir = nf::os::user_files_dir(L"Nightfire PC");
    const std::wstring exe = nf::os::exe_path();
#if defined(__APPLE__)
    // No macOS: ~/Library/Application Support/Nightfire PC/, criada (o executável fica dentro do pacote .app).
    std::wstring home;
    CHECK(nf::os::get_env(L"HOME", &home));
    CHECK(dir == home + L"/Library/Application Support/Nightfire PC/");
    CHECK(nf::os::is_dir(dir));
#else
    // No Windows e no Linux: a pasta do executável, como sempre (vazia no backend nulo, que não sabe o executável).
    if (exe.empty()) {
        CHECK(dir.empty());
    } else {
        CHECK(ends_with_sep(dir));
        CHECK(exe.compare(0, dir.size(), dir) == 0);
        CHECK(exe.find_first_of(L"\\/", dir.size()) == std::wstring::npos);
    }
#endif
    // O nightfire.ini e os saves ficam ali.
    CHECK(nf::default_settings_path() == dir + L"nightfire.ini");
    CHECK(nf::default_save_dir() == dir + L"saves");
}

TEST(volumes_with_a_ps2_disc_are_drives) {
    // O /Volumes do macOS numa pasta temporária: dois volumes com SYSTEM.CNF (o do jogo e o de outro jogo), um sem, um
    // arquivo solto.
    const fs::path vol = temp_dir("Volumes");
    write_file(vol / "NIGHTFIRE" / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SLUS_205.79;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n");
    write_file(vol / "NIGHTFIRE" / "ACTION.ELF", "elf");
    write_file(vol / "NIGHTFIRE" / "FILES.BIN", "bin");
    write_file(vol / "Another Game" / "SYSTEM.CNF", "BOOT2 = cdrom0:\\SLUS_200.01;1\r\n");
    write_file(vol / "Macintosh HD" / "Users" / "x.txt", "x");
    write_file(vol / "loose.txt", "x");
    const std::vector<std::wstring> found = nf::os::folders_with(vol.wstring(), L"SYSTEM.CNF");
    CHECK(found.size() == 2);
    if (found.size() != 2) return;
    // Em ordem de nome, como raízes com o separador no fim (como "D:\" no Windows).
    CHECK(found[0] == (vol / "Another Game").wstring() + nf::os::kPathSep);
    CHECK(found[1] == (vol / "NIGHTFIRE").wstring() + nf::os::kPathSep);
    // O mesmo pela lista de raízes (o macOS passa as montagens locais de /Volumes), sem a barra no fim também.
    const std::vector<std::wstring> roots = {(vol / "Macintosh HD").wstring(), (vol / "NIGHTFIRE").wstring(),
                                             (vol / "Missing").wstring()};
    const std::vector<std::wstring> with = nf::os::roots_with(roots, L"SYSTEM.CNF");
    CHECK(with.size() == 1 && with[0] == found[1]);
    // O volume abre como disco e a versão confere (o de outro jogo, não).
    std::string error;
    const std::shared_ptr<nf::DiscSource> disc = nf::open_disc(found[1], &error);
    CHECK(disc != nullptr);
    if (disc != nullptr) CHECK(nf::check_disc(*disc, nf::nightfire_disc()).ok());
    const std::shared_ptr<nf::DiscSource> other = nf::open_disc(found[0], &error);
    CHECK(other != nullptr && !nf::check_disc(*other, nf::nightfire_disc()).ok());
}

TEST(personal_copy_only_on_windows) {
#if defined(_WIN32)
    CHECK(nf::personal_copy_supported());
#else
    CHECK(!nf::personal_copy_supported());
#endif
    // A procura considera .iso em todo lugar e .cdr fora do Windows.
    CHECK(nf::disc_image_extension(L".iso"));
    CHECK(!nf::disc_image_extension(L".bin") && !nf::disc_image_extension(L".img") && !nf::disc_image_extension(L""));
#if defined(_WIN32)
    CHECK(!nf::disc_image_extension(L".cdr"));
#else
    CHECK(nf::disc_image_extension(L".cdr"));
#endif
}

TEST(bundle_of_exe_finds_the_app_package) {
    // O pacote do macOS a partir do executável (os::bundle_dir: o hd_ui em Contents/Resources, as imagens ao lado do .app).
    CHECK(nf::os::bundle_of_exe(L"/Applications/Nightfire PC.app/Contents/MacOS/Nightfire PC") ==
          L"/Applications/Nightfire PC.app");
    CHECK(nf::os::bundle_of_exe(L"/Users/a/Games/Nightfire PC.app/Contents/MacOS/nightfire") ==
          L"/Users/a/Games/Nightfire PC.app");
    CHECK(nf::os::bundle_of_exe(L"/x/build/nightfire").empty());                    // a build solta
    CHECK(nf::os::bundle_of_exe(L"/x/Nightfire.app/Contents/Resources/nightfire").empty());
    CHECK(nf::os::bundle_of_exe(L"/x/Nightfire/Contents/MacOS/nightfire").empty());  // sem o .app
    CHECK(nf::os::bundle_of_exe(L"/x/Nightfire PC.app/Contents/MacOS/").empty());    // sem o nome
    CHECK(nf::os::bundle_of_exe(L"/Contents/MacOS/x").empty());
    CHECK(nf::os::bundle_of_exe(L"").empty());
#if defined(_WIN32)
    CHECK(nf::os::bundle_dir().empty());  // o Windows nunca fica num pacote
#endif
}

TEST(app_log_masks_the_home_folder) {
    // O registro troca a pasta do usuário por "~" (os relatos vão para um fórum), só no fim de um nome.
    const char* home = "/Users/ana";
    const auto mask = [&](const char* text) {
        std::string out;
        nf::log_mask_home(text, std::strlen(text), home, std::strlen(home),
                          [&](const char* p, size_t n) { out.append(p, n); });
        return out;
    };
    CHECK(mask("disco: /Users/ana/Downloads/Nightfire.cdr\n") == "disco: ~/Downloads/Nightfire.cdr\n");
    CHECK(mask("/Users/ana") == "~");
    CHECK(mask("a /Users/ana b /Users/ana/x") == "a ~ b ~/x");
    CHECK(mask("/Users/anabela/x /Users/ana.b /Users/ana_c") == "/Users/anabela/x /Users/ana.b /Users/ana_c");
    CHECK(mask("\"/Users/ana\": /Users/ana") == "\"~\": ~");
    CHECK(mask("").empty());
    std::string none;
    nf::log_mask_home("/Users/ana/x", 12, "/", 1, [&](const char* p, size_t n) { none.append(p, n); });
    CHECK(none == "/Users/ana/x");  // um home de um caractere não troca nada
}

TEST(image_io_png_round_trip) {
    // Um RGBA com os quatro canais variando (sem alfa 0: os codificadores podem zerar a cor de um pixel invisível).
    const int w = 7, h = 5;
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 37 + y * 11);
            p[1] = uint8_t(255 - x * 29);
            p[2] = uint8_t(y * 51 + 3);
            p[3] = (x + y) % 3 == 0 ? 255 : uint8_t(1 + ((x * 7 + y * 13) * 9) % 254);
        }
    }
    const fs::path dir = temp_dir("png");
    const std::wstring path = (dir / "rgba.png").wstring();
    const bool wrote = nf::write_png_rgba(path, w, h, rgba.data());
#if NF_TEST_SYSTEM_BACKENDS
    CHECK(wrote);
#else
    // Os backends nulos não gravam nem leem imagens.
    CHECK(!wrote);
    return;
#endif
    int rw = 0, rh = 0;
    std::vector<uint8_t> bgra;
    CHECK(nf::read_image_bgra(path, 4096, &rw, &rh, &bgra));
    CHECK(rw == w && rh == h && bgra.size() == rgba.size());
    if (bgra.size() != rgba.size()) return;
    int diff = 0;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        diff += bgra[i + 0] != rgba[i + 2] || bgra[i + 1] != rgba[i + 1] || bgra[i + 2] != rgba[i + 0] ||
                bgra[i + 3] != rgba[i + 3];
    }
    if (diff != 0) std::printf("  %d pixels diferentes\n", diff);
    CHECK(diff == 0);
    // O BGRA das capturas (Renderer::save_png) e o limite do lado na leitura.
    std::vector<uint8_t> shot(size_t(w) * h * 4);
    for (size_t i = 0; i < shot.size(); i += 4) {
        shot[i + 0] = uint8_t(i);
        shot[i + 1] = uint8_t(i * 3);
        shot[i + 2] = uint8_t(255 - i);
        shot[i + 3] = 255;
    }
    const std::wstring shot_path = (dir / "bgra.png").wstring();
    nf::write_png_bgra(shot_path, w, h, shot.data());
    CHECK(nf::read_image_bgra(shot_path, 4096, &rw, &rh, &bgra) && bgra == shot);
    CHECK(!nf::read_image_bgra(shot_path, 6, &rw, &rh, &bgra));
    CHECK(!nf::read_image_bgra((dir / "missing.png").wstring(), 4096, &rw, &rh, &bgra));
}

#if defined(__APPLE__) && NF_TEST_SYSTEM_BACKENDS

TEST(macos_audio_device_opens_or_stays_mute) {
    // O Core Audio: sem aparelho de saída (a CI pode não ter) o jogo fica mudo (nulo); com ele, a fila de 16 pedaços.
    std::unique_ptr<nf::AudioDevice> dev = nf::open_audio_device();
    if (dev == nullptr) {
        std::printf("  sem aparelho de som: mudo\n");
        return;
    }
    std::vector<int16_t> silence(480 * 2, 0);
    CHECK(dev->queued_buffers() == 0 && dev->played_frames() == 0);
    int accepted = 0;
    for (int i = 0; i < 17; ++i) accepted += dev->submit(silence.data(), 480) ? 1 : 0;
    CHECK(accepted == 16);
    CHECK(dev->queued_buffers() == 16);
    dev->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const uint64_t played = dev->played_frames();
    std::printf("  aparelho de som: %llu de %d frames tocados em 200 ms, %d pedaços na fila\n",
                static_cast<unsigned long long>(played), 16 * 480, dev->queued_buffers());
    CHECK(played <= uint64_t(16 * 480));
    CHECK(dev->queued_buffers() >= 0 && dev->queued_buffers() <= 16);
}

TEST(macos_gamepads_read_as_xinput) {
    // O GameController: os controles que não estão ligados saem zerados (na CI, nenhum).
    nf::PadState pads[nf::kMaxPads];
    nf::system_pad_source().poll(pads);
    int connected = 0;
    for (const nf::PadState& p : pads) {
        if (!p.connected) {
            CHECK(p.buttons == 0 && p.lt == 0 && p.rt == 0 && p.lx == 0 && p.ly == 0 && p.rx == 0 && p.ry == 0);
        } else {
            ++connected;
        }
    }
    std::printf("  controles ligados: %d\n", connected);
    nf::system_pad_source().vibrate(0, 1000, 1000);  // sem vibração nesta etapa: não faz nada
}

TEST(macos_app_log_copies_the_output) {
    // O nightfire.log numa pasta temporária: o cabeçalho, a saída padrão e a de erro (que continuam indo para onde iam),
    // a pasta do usuário trocada, e a partida anterior guardada como nightfire.prev.log.
    const fs::path dir = temp_dir("app_log");
    const auto read_all = [](const fs::path& p) {
        std::string s;
        FILE* f = nf::os::open_file(p.wstring(), "rb");
        if (f == nullptr) return s;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        std::fclose(f);
        return s;
    };
    std::wstring home_w;
    CHECK(nf::os::get_env(L"HOME", &home_w));
    const std::string home = nf::os::to_utf8(home_w);
    char arg0[] = "nf_tests";
    char arg1[] = "--teste";
    char* argv[] = {arg0, arg1, nullptr};
    const std::wstring path = nf::macos::start_app_log(dir.wstring(), "teste1", 2, argv);
    CHECK(path == (dir / "nightfire.log").wstring());
    std::printf("  registro: linha da saida padrao\n");
    std::fprintf(stderr, "  registro: linha da saida de erro em %s/Library\n", home.c_str());
    std::printf("  registro: sem o fim da linha");
    nf::macos::stop_app_log(7);
    std::printf("\n");
    const std::string first = read_all(dir / "nightfire.log");
    CHECK(first.find("Nightfire PC ") == 0);
    CHECK(first.find("(build teste1)") != std::string::npos);
    CHECK(first.find("macOS: ") != std::string::npos && first.find("\nGPU: ") != std::string::npos);
    CHECK(first.find("Arguments: --teste\n") != std::string::npos);
    CHECK(first.find("  registro: linha da saida padrao\n") != std::string::npos);
    CHECK(first.find("  registro: linha da saida de erro em ~/Library\n") != std::string::npos);
    // A linha sem o fim não se mistura com a da outra saída (a ordem entre as duas saídas é a de quem a thread leu
    // primeiro), e o fim começa numa linha nova.
    CHECK(first.find("  registro: sem o fim da linha\n") != std::string::npos);
    CHECK(first.find("\n---- end (exit code 7)\n") != std::string::npos);
    CHECK(home.size() < 2 || first.find(home + "/") == std::string::npos);
    // A partida seguinte: a anterior vira nightfire.prev.log.
    const std::wstring again = nf::macos::start_app_log(dir.wstring(), "teste2", 1, argv);
    CHECK(again == path);
    nf::macos::stop_app_log(0);
    CHECK(read_all(dir / "nightfire.prev.log") == first);
    const std::string second = read_all(dir / "nightfire.log");
    CHECK(second.find("(build teste2)") != std::string::npos && second.find("Arguments: (none)\n") != std::string::npos);
    std::printf("  registro: %zu e %zu bytes\n", first.size(), second.size());
}

#endif
