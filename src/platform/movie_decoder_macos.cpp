// O decodificador de MPEG-2 do sistema no macOS: nenhum (o backend de open_system_mpeg2_decoder, platform/
// movie_decoder.h). Os filmes tocam pelo decodificador do port (platform/mpeg2_decoder.h, o mesmo em toda plataforma;
// docs/port.md, "Filmes"): o VideoToolbox do macOS não promete o MPEG-2 em todo Mac (os de Apple Silicon não o têm no
// hardware), e o do port dá os mesmos quadros em qualquer plataforma.

#include "platform/movie_decoder.h"

namespace nf {

std::unique_ptr<Mpeg2Decoder::Backend> open_system_mpeg2_decoder(int, int, double, std::string*, std::string* failure) {
    if (failure != nullptr) *failure = "sem decodificador do sistema no macOS (o do port)";
    return nullptr;
}

}  // namespace nf
