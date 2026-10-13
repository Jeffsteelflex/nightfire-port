#pragma once
// O leitor de INI do DRIVING.ELF: o DAFI da SupportPS2.lib da EA (DAFI_open e companhia, 0x2d9408..0x2d9bdc) e a classe
// IniFiles por cima dele (0x24b9c8..0x24bd60), portados. É o que lê data/render/camera.ini (RCameraIniLoader::LoadFile).
//
// Como o original, o DAFI copia o arquivo para um buffer seu e o recorta no lugar: DAFI_open marca as seções (o nome entre
// colchetes, a primeira linha depois do cabeçalho); cada DAFI_setsection volta a recortar as linhas da seção (a chave
// até o '=', o valor até o fim da linha ou o ';', os dois aparados de espaços e tabulações), escrevendo zeros no buffer.
// IniFiles::ReadFloat/ReadInteger/ReadString procuram a seção (sem diferença de caixa), recortam-na e procuram a chave
// (idem); o número sai do atof/atoi da newlib (strtod/strtol aqui: as mesmas regras) e o dptofp (o arredondamento para o
// float mais próximo). Peculiaridades mantidas: um valor vazio ("chave =" no fim da linha) troca a quebra de linha por um
// espaço e engole a linha seguinte; um comentário "//" depois do valor fica no valor (o número é lido do começo).
//
// Divergência: uma linha de seção sem '=' (nem vazia) faz o DAFI_setsection do PS2 girar para sempre no mesmo lugar; o
// port pula a linha e conta em dafi_guards() (o camera.ini do disco não tem nenhuma).

#include <cstdint>
#include <string>
#include <vector>

namespace drv {

class Dafi {
public:
    // [driving] 0x2d9548 DAFI_open: o arquivo (size bytes) copiado e as seções marcadas.
    void open(const char* data, uint32_t size);
    // [driving] 0x2d9a30 DAFI_getsectioncount
    int32_t section_count() const { return count_; }
    // [driving] 0x2d9ac8 DAFI_getsectionbyindex: o nome (-1: a seção corrente); nulo se a vaga não tiver nome.
    const char* section_by_index(int32_t index) const;
    // [driving] 0x2d9a38 DAFI_getsectionindex: a primeira seção com o nome (strcasecmp), -1 sem; nulo: a corrente.
    int32_t section_index(const char* name) const;
    // [driving] 0x2d9770 DAFI_setsection: recorta a seção com o nome; devolve o índice (-1 sem).
    int32_t set_section(const char* name);
    // [driving] 0x2d97b8 DAFI_setsectionbyindex
    void set_section_by_index(int32_t index);
    // [driving] 0x2d9af0 DAFI_getkeyindex: a primeira chave da seção recortada com o nome, -1 sem.
    int32_t key_index(const char* key) const;
    // [driving] 0x2d9bd0 DAFI_getvaluebyindex
    const char* value_by_index(int32_t index) const;
    // [driving] 0x2d9b88 (sem nome no SYM): o valor da chave, nulo sem ela.
    const char* value(const char* key) const;

private:
    // [driving] 0x2d9408 (o SYM diz CompressQuaternion; é o avanço de linha do DAFI): o começo da próxima linha que não
    // seja branca (pula o resto da linha e depois '\n', '\r', ' ', '\t' e zeros), ou o fim (-1 aqui; nulo no PS2).
    int32_t next_line(int32_t p) const;
    // [driving] 0x2d94b8 (sem nome): apara ' ' e '\t' dos dois lados, escrevendo zeros no fim; devolve o novo começo.
    int32_t trim(int32_t p);
    char at(int32_t p) const { return buffer_[size_t(p)]; }

    static constexpr int kMaxEntries = 1000;  // 0x3e8: seções e chaves
    std::vector<char> buffer_;                // +0x3e8c (o arquivo + um zero)
    int32_t end_ = 0;                         // +0x3e90 (o tamanho)
    int32_t current_ = -1;                    // +0x00
    int32_t count_ = 0;                       // +0x04
    int32_t names_[kMaxEntries] = {};         // +0x08 (deslocamentos no buffer; -1 = nulo)
    int32_t lines_[kMaxEntries] = {};         // +0xfa8 a primeira linha de cada seção (-1 = nula)
    int32_t key_count_ = 0;                   // +0x1f48
    int32_t keys_[kMaxEntries] = {};          // +0x1f4c
    int32_t values_[kMaxEntries] = {};        // +0x2eec
};

// IniFiles (o arquivo já na memória: o construtor do PS2 o lê pelo UFileLoader e chama DAFI_open).
class IniFile {
public:
    // [driving] 0x24b9c8 IniFiles::IniFiles(char *, int) (a parte que não lê do disco: o modo 0, sem criptografia)
    void open(const std::vector<uint8_t>& bytes);
    // [driving] 0x24bb80 IniFiles::ReadFloat(char *, char *, float)
    float read_float(const char* section, const char* key, float fallback);
    // [driving] 0x24bc20 IniFiles::ReadInteger(char *, char *, int)
    int32_t read_integer(const char* section, const char* key, int32_t fallback);
    // [driving] 0x24bcb8 IniFiles::ReadString(char *, char *, char *): o ponteiro para dentro do buffer (válido até o
    // próximo recorte da mesma seção), ou o padrão.
    const char* read_string(const char* section, const char* key, const char* fallback);
    // [driving] 0x24bd40 (o SYM diz IniFiles::WriteString): se a seção existe.
    bool section_exists(const char* section) const;
    Dafi& dafi() { return dafi_; }

private:
    // [driving] 0x24bb60 IniFiles::GetValue(char *)
    const char* get_value(const char* key) const { return dafi_.value(key); }
    Dafi dafi_;
};

// O atof e o atoi da newlib do jogo e o dptofp (0x24d848: o double arredondado para o float mais próximo).
float dafi_atof(const char* s);
int32_t dafi_atoi(const char* s);
// strcasecmp da newlib (tolower ASCII).
int dafi_strcasecmp(const char* a, const char* b);
uint64_t dafi_guards();

}  // namespace drv
