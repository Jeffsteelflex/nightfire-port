#pragma once
// Configuração do port no PC (só do port: o PS2 guarda as opções do jogo no perfil do codinome, no
// cartão de memória). Um arquivo INI simples, `nightfire.ini` ao lado do executável por padrão (ou o
// caminho de --config), com seções [video], [ui], [controls]... As opções da linha de comando valem
// por cima do arquivo e não são gravadas nele.
//
// Formato: linhas `[secao]`, `chave = valor` e comentários começados por `;` ou `#`. Ao gravar, as
// linhas lidas (comentários, ordem, chaves desconhecidas) são mantidas; valores mudados são trocados
// no lugar e chaves novas entram no fim da seção (seções novas no fim do arquivo). Seções e chaves não
// diferenciam maiúsculas de minúsculas.

#include <cstdint>
#include <string>
#include <vector>

namespace nf {

class Settings {
public:
    // Lê o arquivo; sem ele, fica vazio e devolve falso (o caminho é guardado para save()).
    bool load(const std::wstring& path);
    // Lê de um texto (testes).
    void parse(const std::string& text);
    // Grava no caminho de load(); falso se não deu.
    bool save() const;
    std::string text() const;

    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = {}) const;
    int32_t get_int(const std::string& section, const std::string& key, int32_t fallback) const;
    float get_float(const std::string& section, const std::string& key, float fallback) const;
    bool get_bool(const std::string& section, const std::string& key, bool fallback) const;

    void set(const std::string& section, const std::string& key, const std::string& value);
    void set_int(const std::string& section, const std::string& key, int32_t value);
    void set_float(const std::string& section, const std::string& key, float value);
    void set_bool(const std::string& section, const std::string& key, bool value);
    void remove(const std::string& section, const std::string& key);

    const std::wstring& path() const { return path_; }
    bool dirty() const { return dirty_; }

private:
    struct Line {
        std::string raw;      // a linha como lida (comentário, vazia ou seção)
        std::string section;  // seção em que está (minúsculas)
        std::string key;      // chave (minúsculas); vazia se não é chave = valor
        std::string name;     // a chave como escrita
        std::string value;
    };
    int32_t find(const std::string& section, const std::string& key) const;
    std::vector<Line> lines_;
    std::wstring path_;
    bool dirty_ = false;
};

// A configuração do executável (main.cpp a lê no começo e grava quando um menu muda algo).
Settings& settings();
// `nightfire.ini` na pasta do executável.
std::wstring default_settings_path();

}  // namespace nf
