#pragma once
// O banco de ajustes da EA (docs/driving.md seção 29): o DTuningFile::Reader (DTuningFile.obj), o OptionParser
// (GameLoop.obj) e os dbattrib_* (DebugMenu.obj) com que os sistemas do render leem os .tun da missão
// (data\tuning\<sistema>\<subsistema>\<pista>.tun ou default.tun: "Render:FX" para o RBulletStreak, "Render:Glare"
// para o RGlareManager).
//
// Como o PS2 faz: o DTuningDBMgr::LoadDatabase põe o texto do arquivo no Reader corrente (DTuningDBMgr::fgThis + 0xc,
// a primeira palavra o texto); cada dbattrib_<tipo>(nome, endereço, ...) acha a linha "nome=valor" (FindItem) ou, com
// um índice aberto pelo dbindex(nome do índice, variável, mínimo, máximo, nomes), uma linha "nome{índice}=valor" por
// valor do índice (FindIndexedItem: o índice é o nome da tabela de nomes ou o número em decimal; o nome inteiro com
// os espaços tirados, "Halo Tex{3}" vira "HaloTex{3}"), e escreve no endereço mais o índice vezes o passo. O valor:
// sscanf "%d" num int de 4 bytes (dbattrib_s8 e dbattrib_u8 também: o tipo do template é int), "%f" num float,
// "%d,%d,%d,%d" na cor (ParseData_Colour: a << 24 | b << 16 | g << 8 | r, isto é, R, G, B e A na memória com o texto
// "A,R,G,B"). Uma chave que não está no arquivo deixa o valor que estava. O OptionParser procura a chave com strstr
// (maiúsculas contam): só vale no começo de uma linha (o texto, ou depois de '\n' ou '\r'), seguida de ' ' ou '='; o
// valor começa depois dos ' ', '=', '#', '(' e '[' e vai até o '\n' (sem um ']' e depois um ')' finais).
//
// Divergência (só do port): o PS2 também monta, para cada atributo, a variável do menu de depuração (a DebugIndexer
// e as entradas do DebugMenu); o port só lê os valores. O sscanf é o do C do PC (o do newlib dá o mesmo nos números
// dos .tun: inteiros e decimais curtos).

#include <cstdint>
#include <string>

namespace drv {

// O texto de um .tun aberto (o Reader do DTuningDBMgr). Vazio: nenhum banco (as buscas não acham nada).
struct TuningReader {
    std::string text;
};

// [driving] 0x001790f8 OptionParser::OptionParser(char *texto, char *chave) com o 0x00179508
// OptionParser::GetFullString: o valor da chave em `text` (falso sem ela).
bool OptionParser_FindValue(const char* text, const char* key, std::string& value);
// [driving] 0x001440e0 (a cópia de um nome sem os espaços: a chave dos dois FindIndexedItem)
std::string tuning_strip_spaces(const char* s);
// [driving] 0x00144548 DTuningFile::Reader::FindItem(char *)
bool DTuningFile_FindItem(const TuningReader& r, const char* name, std::string& value);
// [driving] 0x00144648 DTuningFile::Reader::FindIndexedItem(char *, char *) ("%s{%s}"); o de (char *, int), 0x001445b8,
// é o mesmo com o índice em decimal (o dbattrib usa este, com o texto que o índice monta).
bool DTuningFile_FindIndexedItem(const TuningReader& r, const char* name, const char* index, std::string& value);
// [driving] 0x00144740 DTuningFile::ParseData_Colour(char *, unsigned int &): "%d,%d,%d,%d" (sem valor: nada).
void DTuningFile_ParseData_Colour(const std::string* value, uint32_t& out);

// O índice aberto pelo dbindex (0x00142b68): os valores lo..hi, com os nomes (nulo: o número em decimal).
struct TuningIndex {
    int32_t lo = 0, hi = -1;
    const char* const* names = nullptr;
};
// [driving] 0x00143ad8 dbattrib_argb(char *, unsigned int *, ...) (a cor: ParseData_Colour; também o
// 0x001439a8 do RGlareManager, a mesma função noutra instância) com o índice `ix` (nulo: sem índice).
void dbattrib_colour(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride);
// [driving] 0x00142088 dbattrib_s8 / 0x00142398 dbattrib_u8: sscanf "%d" num int de 4 bytes.
void dbattrib_int(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride);
// [driving] 0x001426a8 dbattrib_float: sscanf "%f".
void dbattrib_float(const TuningReader& r, const TuningIndex* ix, const char* name, uint8_t* first, uint32_t stride);

// O texto de um .tun da missão pela chave do DTuningDBMgr::LoadDatabase ("Render:FX" -> data\tuning\Render\FX\): o da
// pista, senão o default.tun. `read` lê um arquivo do arquivo da missão (falso sem ele).
template <typename Read>
TuningReader tuning_load(Read read, const std::string& system, const std::string& sub, const std::string& track) {
    TuningReader r;
    const std::string dir = "data\\tuning\\" + system + "\\" + sub + "\\";
    if (!read(dir + track + ".tun", r.text)) read(dir + "default.tun", r.text);
    return r;
}

}  // namespace drv
