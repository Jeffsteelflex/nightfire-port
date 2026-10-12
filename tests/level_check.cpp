// Regressão com os dados do jogo: monta todas as fases com o código C++ (inclusive a tabela de
// hashcodes portada) e imprime as contagens, para comparar com tools/geometry.py.
// Não entra no ctest porque precisa dos arquivos do usuário.
//
// Uso: nf_level_check [pasta com ACTION.ELF e FILES.BIN]

#include <cstdio>
#include <string>

#include "data/files_bin.h"
#include "platform/arena.h"
#include "game/level_scene.h"

int main_body(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::wstring wdir(dir.begin(), dir.end());
    nf::FilesBin files(wdir);
    int levels = 0, failures = 0;
    for (const std::string& level : nf::list_levels(files)) {
        try {
            nf::LevelScene scene(files, level);
            const nf::LevelStats& s = scene.stats();
            std::printf("%s colocados=%d sem_geometria=%d volumes=%d nao_resolvidos=%d triangulos=%zu\n",
                        level.c_str(), s.placed, s.without_geometry, s.volumes, s.unresolved, s.triangles);
            ++levels;
            if (s.unresolved) ++failures;
        } catch (const nf::FormatError& e) {
            std::printf("%s sem mapa principal (%s)\n", level.c_str(), e.what());
        }
    }
    std::printf("%d fases montadas, %d com referências não resolvidas\n", levels, failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    // 64 bits: na pilha da arena (platform/arena.h); 32 bits: direto.
    return nf::run_main([&] { return main_body(argc, argv); });
}
