#pragma once
// A janela de escolha de arquivo do sistema, sem travar o laço (só do port; docs/port.md, "Plataforma: interfaces e
// backends"): a tela de configuração (render/setup_app.h) a abre para o jogador apontar a imagem do disco e continua
// desenhando e bombeando a janela do jogo enquanto ela está aberta. No Windows (file_dialog_win32.cpp), o
// IFileOpenDialog numa thread STA própria (a thread do jogo já está no apartamento multithread do Direct3D e do Media
// Foundation); no macOS o NSOpenPanel (file_dialog_macos.mm, etapa 3c).

#include <memory>
#include <string>

namespace nf {

struct FilePickerText {
    std::wstring title;   // o título da janela
    std::wstring images;  // o nome do filtro das imagens de disco (*.iso)
    std::wstring all;     // o nome do filtro de todos os arquivos
};

class FilePicker {
public:
    FilePicker();
    ~FilePicker();  // espera a janela fechar
    FilePicker(const FilePicker&) = delete;
    FilePicker& operator=(const FilePicker&) = delete;

    bool busy() const;
    // Abre a janela (dona: a janela do jogo, Window::native_handle). Nada se já estiver aberta.
    void start(void* owner, const FilePickerText& text);
    // A escolha terminou: true e o caminho (vazio se cancelada).
    bool take(std::wstring* path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nf
