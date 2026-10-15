// As configurações do controle (driving/game/input_config.h), portadas do MIPS do DRIVING.ELF (as desmontagens em
// out/agent_drv_car/ do checkout principal).

#include "driving/game/input_config.h"

#include <cstdlib>
#include <cstring>

#include "platform/crt_compat.h"

namespace drv {

namespace {

// A tabela 0x32e540 do StringToNumber dos nomes do menu: (número, nome).
const char* const kLocaleNames[14] = {"ButtonCross",  "ButtonTriangle", "ButtonCircle", "ButtonSquare", "DPadLeftRight",
                                      "DPadUpDown",   "ButtonLStick",   "ButtonL1",     "ButtonL2",     "ButtonRStick",
                                      "ButtonR1",     "ButtonR2",       "DESCRIPTION",  "TITLE"};

// strtok com "\t " (o estado é o ponteiro guardado), como o da newlib.
char* next_token(char*& state, char* start) {
    char* s = start ? start : state;
    if (!s) return nullptr;
    while (*s == '\t' || *s == ' ') ++s;
    if (*s == 0) {
        state = nullptr;
        return nullptr;
    }
    char* e = s;
    while (*e && *e != '\t' && *e != ' ') ++e;
    if (*e) {
        *e = 0;
        state = e + 1;
    } else {
        state = nullptr;
    }
    return s;
}

// A linha a partir de p até o '\r' (strchr(p, 0xd), strncpy, o nulo); falso sem '\r' ou longa demais para o buffer.
bool copy_line(const char* p, char* line, size_t cap) {
    const char* cr = std::strchr(p, '\r');
    if (!cr || size_t(cr - p) >= cap) return false;
    std::memcpy(line, p, size_t(cr - p));
    line[cr - p] = 0;
    return true;
}

}  // namespace

// [driving] 0x00167498 AdvanceFilePtr: depois do próximo '\n' (nulo se não há).
const char* AdvanceFilePtr(const char* p) {
    const char* nl = p ? std::strchr(p, '\n') : nullptr;
    return nl ? nl + 1 : nullptr;
}

int32_t input_locale_index(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < 14; ++i)
        if (std::strcmp(kLocaleNames[i], name) == 0) return i;
    return -1;
}

InputConfigManager::InputConfigManager() {
    d = InputConfigManagerData();
    d.type = 2;
}

// [driving] 0x00166548 InputConfigManager::ParseMasterConfigFile
void InputConfigManager_ParseMasterConfigFile(InputConfigManager& m, const std::string& text) {
    int32_t type = 2;  // s4: antes de um cabeçalho
    int32_t counter[2] = {0, 0};
    const char* p = text.c_str();
    for (;;) {
        char line[256];
        if (!p || !copy_line(p, line, sizeof line)) {
            ++m.guards;  // o PS2 copiaria até um ponteiro nulo
            return;
        }
        char* state = nullptr;
        char* tok = next_token(state, line);
        if (!tok) {
            ++m.guards;  // strcasecmp de um ponteiro nulo
            p = AdvanceFilePtr(p);
            continue;
        }
        if (nf::str_icmp(tok, "END") == 0) return;
        if (nf::str_icmp(tok, "Driving") == 0 || nf::str_icmp(tok, "POV") == 0) {
            type = nf::str_icmp(tok, "Driving") == 0 ? 0 : 1;
            const char* n = next_token(state, nullptr);
            const int32_t count = n ? std::atoi(n) : 0;
            m.d.count[type] = count;
            m.configs[type].assign(size_t(count > 0 ? count : 0), InputConfigInfo());
        } else {
            if (type > 1 || counter[type] >= int32_t(m.configs[type].size())) {
                ++m.guards;  // fora das listas (o tipo 2 de antes de um cabeçalho, ou mais nomes que a contagem)
            } else {
                InputConfigInfo& c = m.configs[type][size_t(counter[type])];
                if (std::strlen(tok) >= sizeof c.name) ++m.guards;  // strcpy além dos 0x20 bytes
                std::memset(c.name, 0, sizeof c.name);
                nf::str_copy_trunc(c.name, sizeof c.name, tok);
            }
            if (type <= 1) counter[type] += 1;
        }
        p = AdvanceFilePtr(p);
    }
}

// [driving] 0x00166788 InputConfigManager::ParseDefFileForFrontEnd
void InputConfigManager_ParseDefFileForFrontEnd(InputConfigManager& m, InputConfigInfo& info, const std::string& text) {
    bool before = true;
    const char* p = text.c_str();
    for (;;) {
        p = AdvanceFilePtr(p);
        char line[256];
        if (!p || !copy_line(p, line, sizeof line)) {
            ++m.guards;
            return;
        }
        char* state = nullptr;
        char* tok = next_token(state, line);
        if (!tok) {
            ++m.guards;
            continue;
        }
        if (nf::str_icmp(tok, "END") == 0) return;
        if (before) {
            if (nf::str_icmp(tok, "FRONTEND") == 0) before = false;
            continue;
        }
        const int32_t index = input_locale_index(tok);
        const char* n = next_token(state, nullptr);
        const int32_t value = n ? std::atoi(n) : 0;
        if (index < 0) {
            ++m.guards;  // o PS2 gravaria em +0x20 (o ponteiro do texto)
            continue;
        }
        info.locale[index] = value;
    }
}

// [driving] 0x00166f28 InputConfigManager::SetConfig(char *)
void InputConfigManager_SetConfig(InputConfigManager& m, const std::string* text, InputMap& map) {
    map.Reset();
    if (m.d.frontend != 0 && m.d.frontend <= m.texts.size()) map.LoadParseDefFile(m.texts[m.d.frontend - 1]);
    if (text) map.LoadParseDefFile(*text);
}

// [driving] 0x001668a0 InputConfigManager::InitAndPreload
void InputConfigManager_InitAndPreload(InputConfigManager& m, const InputFileLoader& load, InputUberBond& uber,
                                       InputMap& map) {
    InputConfigManager_ParseMasterConfigFile(m, load("data/Control/Master.def"));
    m.texts.push_back(load("data/Control/FrontEnd.def"));
    m.d.frontend = uint32_t(m.texts.size());
    for (int t = 0; t < 2; ++t)
        for (InputConfigInfo& c : m.configs[t]) {
            m.texts.push_back(load(std::string("data/Control/") + c.name));
            c.file = uint32_t(m.texts.size());
        }
    for (int t = 0; t < 2; ++t)
        for (InputConfigInfo& c : m.configs[t]) InputConfigManager_ParseDefFileForFrontEnd(m, c, m.texts[c.file - 1]);
    m.d.initialized = 1;
    m.d.type = 0;
    m.d.current[0] = uber.driving_config;
    m.d.current[1] = uber.pov_config;
    m.d.inverted = uber.inverted != 0 ? 1 : 0;
    for (int t = 0; t < 2; ++t) {
        if (m.d.count[t] <= m.d.current[t]) {
            m.d.current[t] = 0;
            m.warnings.push_back("Warning: Uberbond passed in invalid control config, setting to 0");
            if (t == 0) uber.driving_config = 0;
            else uber.pov_config = 0;
        }
    }
    const int32_t type = m.d.type, cur = m.d.current[type];
    if (cur < 0 || size_t(cur) >= m.configs[type].size()) {
        ++m.guards;  // a lista vazia (o PS2 leria o ponteiro do texto de fora dela)
        InputConfigManager_SetConfig(m, nullptr, map);
        return;
    }
    const uint32_t file = m.configs[type][size_t(cur)].file;
    InputConfigManager_SetConfig(m, file ? &m.texts[file - 1] : nullptr, map);
}

// [driving] 0x00166cb0 InputConfigManager::SetConfig(ConfigType, int)
void InputConfigManager_SetConfigType(InputConfigManager& m, int32_t type, int32_t config, InputUberBond& uber,
                                      InputMap& map) {
    m.d.type = type;
    if (config != -1) m.d.current[type & 1] = config;
    if (m.d.type == 0) uber.driving_config = m.d.current[0];
    else if (m.d.type == 1) uber.pov_config = m.d.current[1];
    const int32_t cur = m.d.current[type & 1];
    const std::vector<InputConfigInfo>& list = m.configs[m.d.type & 1];
    if (cur < 0 || size_t(cur) >= list.size()) {
        ++m.guards;
        return;
    }
    const uint32_t file = list[size_t(cur)].file;
    InputConfigManager_SetConfig(m, file ? &m.texts[file - 1] : nullptr, map);
}

// [driving] 0x00166d80 InputConfigManager::GetNumConfigs
int32_t InputConfigManager_GetNumConfigs(const InputConfigManager& m, int32_t type) { return m.d.count[type & 1]; }

// [driving] 0x00166d90 InputConfigManager::GetCurrentConfig
int32_t InputConfigManager_GetCurrentConfig(const InputConfigManager& m, int32_t type) {
    if (type == 2) return m.d.current[m.d.type & 1];
    return m.d.current[type & 1];
}

}  // namespace drv
