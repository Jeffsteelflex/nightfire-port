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

bool install_files(const FilesBin& files, const std::wstring& data_dir) {
    std::memset(FileList, 0, sizeof(FileList));
    for (size_t i = 0; i < files.entries().size() && i < size_t(kFileListCount); ++i) {
        const FileEntry& e = files.entries()[i];
        std::memcpy(FileList[i].name, e.name.c_str(), std::min(e.name.size(), sizeof(FileList[i].name) - 1));
        FileList[i].offset = e.offset;
        FileList[i].size = e.size;
    }
    return psiFileSetup((data_dir + L"\\FILES.BIN").c_str());
}

bool install_game_tables(const std::wstring& data_dir, std::string* error, std::vector<std::string>* log) {
    try {
        const Elf elf(read_file(data_dir + L"\\ACTION.ELF"));
        return init_game_tables(elf, &Engine_ResolveCode, error, log) &&
               init_hud_tables(elf, &Engine_ResolveCode, error, log) && init_menu_tables(elf, error, log);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

}  // namespace nf
