#include "game/boot.h"

#include <algorithm>
#include <cstring>

#include "data/elf.h"
#include "engine/code_resolve.h"
#include "game/menu_tables.h"
#include "game/hud_tables.h"
#include "game/static_init.h"
#include "platform/psi_file.h"

namespace nf {

const DiscTitle& nightfire_disc() {
    static const DiscTitle title{
        "SLUS-20579",
        "007: Nightfire (PS2, USA)",
        {"ACTION.ELF", "FILES.BIN"},
        {"ACTION.ELF", "SYSTEM.CNF", "FILES.BIN", "PS2/"},
        {"MOVIES/"},
    };
    return title;
}

bool install_files(const FilesBin& files) {
    std::memset(FileList, 0, sizeof(FileList));
    for (size_t i = 0; i < files.entries().size() && i < size_t(kFileListCount); ++i) {
        const FileEntry& e = files.entries()[i];
        std::memcpy(FileList[i].name, e.name.c_str(), std::min(e.name.size(), sizeof(FileList[i].name) - 1));
        FileList[i].offset = e.offset;
        FileList[i].size = e.size;
    }
    return files.disc() != nullptr && psiFileSetup(files.disc()->open("FILES.BIN"));
}

bool install_game_tables(const DiscSource& disc, std::string* error, std::vector<std::string>* log) {
    try {
        Bytes bytes;
        if (!disc.read("ACTION.ELF", bytes)) {
            if (error) *error = "não consegui ler ACTION.ELF de " + disc.describe();
            return false;
        }
        const Elf elf(std::move(bytes));
        return init_game_tables(elf, &Engine_ResolveCode, error, log) &&
               init_hud_tables(elf, &Engine_ResolveCode, error, log) && init_menu_tables(elf, error, log);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool install_game_tables(const std::wstring& data_dir, std::string* error, std::vector<std::string>* log) {
    std::string open_error;
    const std::shared_ptr<DiscSource> disc = open_disc(data_dir, &open_error);
    if (disc == nullptr) {
        if (error) *error = open_error;
        return false;
    }
    return install_game_tables(*disc, error, log);
}

}  // namespace nf
