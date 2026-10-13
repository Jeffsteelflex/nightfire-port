// Os backends nulos das outras interfaces de plataforma (a opção NF_HEADLESS do CMake; ver os_null.cpp): sem controle
// (nenhum conectado), sem aparelho de som (a mistura vai para o WAV ou para nada, como nas capturas), sem decodificador
// de MPEG-2 do sistema (os filmes tocam pelo do port, platform/mpeg2_decoder.h), sem imagens em arquivo e sem a janela
// de escolha de arquivo. Nada aqui é do Windows. O relatório de falhas nulo fica em crash_report_null.cpp (a build
// headless do macOS e do Linux usa estes backends com o relatório de verdade, crash_report_posix.cpp).

#include "platform/audio_device.h"
#include "platform/file_dialog.h"
#include "platform/gamepad.h"
#include "platform/image_io.h"
#include "platform/movie_decoder.h"

namespace nf {

// ---------------------------------------------------------------- controles

namespace {
class NoPads final : public PadSource {
public:
    void poll(PadState out[kMaxPads]) override {
        for (int i = 0; i < kMaxPads; ++i) out[i] = PadState{};
    }
    void vibrate(int, uint16_t, uint16_t) override {}
};
NoPads g_no_pads;
}  // namespace

PadSource& system_pad_source() { return g_no_pads; }

// ---------------------------------------------------------------- som

std::unique_ptr<AudioDevice> open_audio_device() { return nullptr; }

// ---------------------------------------------------------------- filmes

// Nenhum decodificador do sistema: os filmes tocam pelo do port (platform/mpeg2_decoder.h).
std::unique_ptr<Mpeg2Decoder::Backend> open_system_mpeg2_decoder(int, int, double, std::string*, std::string* failure) {
    *failure = "sem decodificador do sistema (NF_HEADLESS)";
    return nullptr;
}

// ---------------------------------------------------------------- imagens

bool write_png_rgba(const std::wstring&, int, int, const uint8_t*) { return false; }
void write_png_bgra(const std::wstring&, int, int, const uint8_t*) {}
bool read_image_bgra(const std::wstring&, int, int*, int*, std::vector<uint8_t>*) { return false; }

// ---------------------------------------------------------------- a escolha de arquivo

struct FilePicker::Impl {};
FilePicker::FilePicker() = default;
FilePicker::~FilePicker() = default;
bool FilePicker::busy() const { return false; }
void FilePicker::start(void*, const FilePickerText&) {}
bool FilePicker::take(std::wstring*) { return false; }

}  // namespace nf
