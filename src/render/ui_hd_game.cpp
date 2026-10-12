// Camada 2D em alta definição: o que ela precisa saber das estruturas do jogo a cada quadro (ver ui_hd.h,
// UiHdEnv). Só lê; nada aqui muda o estado do jogo.

#include "render/ui_hd_game.h"

#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "engine/font.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/map_types.h"
#include "engine/menu.h"
#include "engine/object.h"
#include "engine/player.h"
#include "engine/resetmap.h"
#include "platform/input_bindings.h"
#include "platform/psi_pad.h"

namespace nf {

namespace {

// A altura do 'H' de uma fonte do jogo (a altura das maiúsculas, em texels = pixels da tela do PS2 com
// escala 1); 0 sem a fonte.
float cap_height(const FONT_tag* f) {
    if (f == nullptr || f->chars == nullptr) return 0.0f;
    for (uint32_t i = 0; i < f->count && i < 512; ++i) {
        if (f->chars[i].code == 'H') return float(f->chars[i].h);
    }
    return 0.0f;
}

// O texto da tecla de cada símbolo, guardado enquanto a tabela de teclas, o estilo e os gatilhos não mudam
// (ui_hd_prompt_keys simula o controle do PS2 em cada estilo).
std::wstring cached_key_label(uint8_t code, bool game) {
    static nf::Bindings bindings{};
    static uint16_t style = 0xffff;
    static bool triggers = false;
    static std::map<int, std::wstring> labels;
    const nf::ControlsConfig& c = nf::controls();
    const uint16_t st = PlayerSetting[0].style;
    if (std::memcmp(&bindings, &c.bindings, sizeof(bindings)) != 0 || style != st || triggers != psiPadTriggersR1) {
        bindings = c.bindings;
        style = st;
        triggers = psiPadTriggersR1;
        labels.clear();
    }
    const int key = int(code) << 1 | (game ? 1 : 0);
    const auto it = labels.find(key);
    if (it != labels.end()) return it->second;
    return labels[key] = ui_hd_prompt_keys(code, game, bindings, style, triggers);
}

// Os sprites do mapa de botões do jogo: as células da lista 10 do painel 0x10000028 da pausa
// (Menu_DisplayControllerStyleList), onde os símbolos são os comandos do jogo, não os do menu.
std::shared_ptr<std::unordered_set<const void*>> controller_map_sprites() {
    auto out = std::make_shared<std::unordered_set<const void*>>();
    const M_MANAGER& m = menu_manager[0];
    if (m.active == 0 || m.page == nullptr) return out;
    for (const M_CONTROL* c = static_cast<const M_CONTROL*>(ll_first(m.page->controls)); c != nullptr; c = c->next) {
        if (c->id != 0x10000028u || c->tag != 10 || c->type != MCT_LIST) continue;
        const auto* l = reinterpret_cast<const M_LISTBOX*>(c);
        for (const auto& row : l->cells) {
            for (const sprite* s : row) {
                if (s != nullptr) out->insert(s);
            }
        }
    }
    return out;
}

}  // namespace

UiHdEnv ui_hd_game_env(int width, int height) {
    UiHdEnv env;
    env.width = width;
    env.height = height;
    env.texture_size = [](uint32_t hash, int& w, int& h) {
        if ((hash >> 24) != 3) return false;
        const auto* t = static_cast<const maptex_tag*>(hashtable_getitem(hash));
        if (t == nullptr || t->width <= 0 || t->height <= 0) return false;
        w = t->width;
        h = t->height;
        return true;
    };
    // Os sprites do HUD de cada jogador: a mira (pane -1) e os sprites de cada pane.
    auto ids = std::make_shared<std::unordered_map<const void*, UiHdIdentity>>();
    for (const obj_tag* player : glb_players) {
        if (player == nullptr || player->data == nullptr) continue;
        const auto* b = static_cast<const BLData*>(player->data);
        const auto* hud = static_cast<const HUDINFO_tag*>(b->hud);
        if (hud == nullptr) continue;
        if (hud->crosshair != nullptr) {
            UiHdIdentity id;
            id.pane = -1;
            id.sprite = 0;
            (*ids)[hud->crosshair] = id;
        }
        for (uint32_t p = 0; p < kHudPanes; ++p) {
            const HUDPANE_tag& pane = hud->panes[p];
            if (pane.valid == 0 || pane.sprites == nullptr) continue;
            for (uint32_t k = 0; k < pane.count; ++k) {
                if (pane.sprites[k] == nullptr) continue;
                UiHdIdentity id;
                id.pane = int(p);
                id.sprite = int(k);
                (*ids)[pane.sprites[k]] = id;
            }
        }
    }
    env.identity = [ids](const void* sprite) {
        const auto it = ids->find(sprite);
        return it != ids->end() ? it->second : UiHdIdentity{};
    };
    for (int k = 1; k <= 3; ++k) {
        const float h = cap_height(FontTable[k - 1].font);
        if (h > 0.0f) env.cap_height[k] = h;
    }
    // Símbolos de botão: o conjunto em vigor (o escolhido, ou o do último aparelho), as teclas e o mapa de
    // botões da pausa.
    env.prompts = ui_hd_prompt_effective();
    env.triggers_r1 = psiPadTriggersR1;
    env.key_label = [](uint8_t code, bool game) { return cached_key_label(code, game); };
    if (env.prompts == UiHdPromptSet::Keyboard) {
        auto map = controller_map_sprites();
        env.game_context = [map](const void* sprite) { return map->count(sprite) != 0; };
    }
    return env;
}

}  // namespace nf
