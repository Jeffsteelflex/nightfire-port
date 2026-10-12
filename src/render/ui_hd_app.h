#pragma once
// Camada 2D em alta definição no nightfire.exe: acha a pasta hd_ui, lê o map.json, liga o desenho no
// renderizador e no RuntimeGfx, alterna (F4) e mantém o inventário dos elementos de imagem
// (--ui-inventory). Ver render/ui_hd.h e docs/port.md, "Camada 2D em alta definição".

#include <memory>
#include <string>
#include <vector>

#include "render/ui_hd.h"

namespace nf {

class Renderer;
class RuntimeGfx;

struct UiHdOptions {
    bool enabled = false;     // --hd-ui
    std::wstring dir;         // --hd-ui-dir (vazio: procura)
    std::string anchor;       // --hd-ui-anchor centre|edges (vazio: o do map.json)
    std::wstring inventory;   // --ui-inventory <pasta> (vazio: sem inventário)
};

class UiHdApp {
public:
    UiHdApp();
    ~UiHdApp();
    // Procura a pasta hd_ui (--hd-ui-dir; senão a do executável e as de cima, a dos dados, a atual), lê o
    // map.json e prepara o desenho. Sem a pasta ou com erro no arquivo, fica com a configuração padrão
    // (fontes padrão, sem substitutas) e o erro em status().
    void init(const UiHdOptions& options, const std::wstring& data_dir, Renderer& renderer, RuntimeGfx& gfx);
    void toggle();  // F4
    bool enabled() const { return enabled_; }
    // Só do PC (opções de vídeo): liga ou desliga (sem Direct2D fica desligada) e a âncora do HUD.
    void set_enabled(bool on) {
        if (on != enabled_) toggle();
    }
    bool anchor_edges() const { return config_.anchor == UiHdAnchor::Edges; }
    void set_anchor_edges(bool edges) { config_.anchor = edges ? UiHdAnchor::Edges : UiHdAnchor::Centre; }
    // Depois de cada RuntimeGfx::collect_frame: alimenta o inventário e recorta as imagens novas.
    void after_collect(Renderer& renderer);
    // Grava o inventário (no fim).
    void finish();
    std::string status() const;

private:
    void save_inventory();

    UiHdConfig config_;
    std::wstring dir_;
public:
    // A pasta hd_ui encontrada (vazia se nenhuma).
    const std::wstring& dir() const { return dir_; }

private:
    std::string error_;
    bool enabled_ = false;
    Renderer* renderer_ = nullptr;
    RuntimeGfx* gfx_ = nullptr;
    std::wstring inventory_dir_;
    std::unique_ptr<UiHdInventory> inventory_;
    std::vector<std::string> cropped_;
    size_t saved_entries_ = 0;
};

}  // namespace nf
