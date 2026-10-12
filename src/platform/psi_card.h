#pragma once
// O cartão de memória: a máquina de estados do ACTION.ELF (HandlePS2CardState e os MemCard_*,
// 0x0010e798-0x00111270) e as funções psi* que o jogo usa para gravar, ler, listar e apagar perfis
// (psiSaveData, psiLoadData, psiStartSaveEnum...), portadas sobre um cartão do PC.
//
// No PS2 cada estado da pilha CardState (estado, subestado) faz um passo e as chamadas da libmc
// (sceMcGetInfo, sceMcOpen, sceMcWrite...) terminam alguns quadros depois (sceMcSync). No port as chamadas
// sce* viram operações num nf::PcCard (a raiz de um cartão do PS2: pastas e arquivos) que terminam no
// próximo PS2PollMemoryCard: uma operação por quadro, como um cartão rápido. As mensagens (PS2CardFlow), as
// perguntas (sobrescrever, apagar) e os erros são os do jogo; com o cartão do PC sempre presente, formatado
// e com espaço, as mensagens de "sem cartão" e "formatar" não aparecem.
//
// O que o jogo grava (o mesmo que no cartão do PS2): uma pasta "BASLUS-20579<CODINOME>" com o ícone do
// pacote do menu principal (o item tipo 15 "007", SetUpIconFile), o icon.sys e o arquivo de dados
// "BASLUS-20579<CODINOME>": os blocos do perfil (LS_*, engine/loadsave.h) seguidos do crc32buf deles
// (psiSaveData acrescenta, MemCard_LOAD_DATA confere: diferente, "dados corrompidos"). O jogo confere a
// pasta depois de gravar: 5 entradas somando 78 KB (MemCard_SAVE_DATA), como no PS2.
//
// Os cartões do PC: nf::FolderCard (uma pasta do disco: saves/ ao lado do executável ou --save-dir) e
// nf::MemoryCard (em memória: testes, nf_loader_check e as capturas sem --save-dir, que nunca tocam nos
// saves do jogador). Sem nf::set_card, o cartão é um MemoryCard vazio.
//
// Divergências: HandlePS2CardState roda até 32 passos por chamada ou até uma operação ficar pendente (no PS2,
// o tempo de ~10 contagens do RCNT1); sceMcOpen com o modo de criar não trunca o arquivo, como a libmc sem
// SCE_TRUNC (o jogo regrava arquivos de tamanho fixo); "." e ".." contam 0 bytes no sceMcGetDir (é o que
// fecha os 78 KB da conferência); MemCard_LOAD_DATA lê no máximo 0x1000 bytes (o buffer do perfil; o
// original leria o arquivo inteiro) e um arquivo com menos de 4 bytes é "corrompido" (o original leria antes
// do buffer); a pilha nunca sai do vetor CardState.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// As mensagens e as respostas que a máquina de estados mostra (PS2CardFlow, 0x002462d0; PS2_DoCardFlow,
// engine/menu_frontend.h, as põe na caixa 0x4000002a e C_LBERROPTIONS_Handler devolve a resposta).
struct PS2CARDFLOW_tag {
    uint32_t options;   // 0x00 respostas oferecidas, um bit por valor: 1 OK, 2 sim, 4 cancelar, 8 tentar de novo,
                        //      0x10 formatar, 0x20 apagar
    uint32_t message;   // 0x04 0 nenhuma; 1 sem cartão, 2 sem formato, 3 sem espaço, 4 sobrescrever?, 5 falha ao
                        //      gravar, 6 dados corrompidos, 7 falha ao ler, 8 lendo, 9 gravando, 0xa formatando,
                        //      0xb conferindo, 0xc apagando, 0xd apagar?, 0xe falha ao apagar, 0xf sem espaço
                        //      (partida), 0x10 sem cartão (partida), 0x11 falha ao formatar
    uint8_t rebuild;    // 0x08 refazer a lista de respostas
    uint8_t answered;   // 0x09 o jogador escolheu
    uint8_t answer;     // 0x0a o valor da resposta escolhida
    uint8_t pad;
};
static_assert(sizeof(PS2CARDFLOW_tag) == 12);
extern PS2CARDFLOW_tag PS2CardFlow;

// Um passo da pilha da máquina de estados (CardState, 0x00316950).
struct CARDSTATE_tag {
    uint8_t state;  // 0 nada, 1 espera a libmc, 2 formatar, 3 conferir o cartão... (HandlePS2CardState)
    uint8_t sub;    // subestado do passo
};
constexpr int kCardStates = 32;
extern CARDSTATE_tag CardState[kCardStates];
extern int32_t StateStack;          // 0x0030d6dc topo da pilha (0: parada)
extern uint32_t PSIOP_State;        // 0x0030d6d8 0 andando, 1 gravação pedida, 2 deu certo, 4 terminou sem gravar/ler
extern int32_t syncresult;          // 0x0030d6d0 resultado da última operação da libmc
extern uint8_t PS2SaveGameFine;     // 0x0030c894 a última gravação conferiu
extern uint8_t OverWriteCheckDone;  // 0x0030d6d4 a pergunta de sobrescrever já foi feita nesta gravação
extern int32_t NumberSaveGamesGot;  // 0x0030c890 entradas da última listagem
extern int32_t GettingSaveGames;    // 0x0030c888 -1 listando; 0 pronta
extern int32_t SaveGamesReturned;   // 0x0030c88c entradas já devolvidas por psiGetNextSave
extern uint8_t PS2_one_codename;    // 0x0030c884 a listagem só precisa da primeira pasta
// Tabelas do ELF copiadas na partida (game/static_init.cpp).
extern uint8_t ascii_table[12];     // 0x00265060 ASCII -> Shift-JIS: {base, subtrai} de dígitos, maiúsculas, minúsculas
extern uint8_t ascii_special[132];  // 0x00264fd8 {Shift-JIS, ASCII} da pontuação
extern uint8_t bgcolor[64];         // 0x00246280 as cores do fundo do icon.sys
extern uint8_t ambient[16];         // 0x002462c0 a luz ambiente do icon.sys

// [decomp] a interface do jogo.
void InitaliseMemoryCard();
void PS2PollMemoryCard();
void HandlePS2CardState();
// Grava `size` bytes de `data` no perfil `name` (o crc32buf vai em data[size..size+4): o buffer precisa de 4
// bytes a mais). Devolve 0; o andamento em psiSavingData.
int32_t psiSaveData(uint8_t* data, uint32_t size, char* name);
// Lê o perfil `name` em `data` (até 0x1000 bytes; size não é usado, como no original). Andamento em
// psiLoadingData.
int32_t psiLoadData(uint8_t* data, uint32_t* size, char* name);
uint32_t psiSavingData();
uint32_t psiLoadingData();
int32_t psiDeleteData(char* name);
uint32_t psiDeletingData();
int32_t psiDoBootupCheck();
// Lista os perfis do cartão (one: só o primeiro); psiGetNextSave devolve -1 enquanto lista, 0 no fim e senão
// o endereço do nome do próximo (o nome da pasta sem "BASLUS-20579").
void psiStartSaveEnum(int8_t one);
intptr_t psiGetNextSave();
void psiEndSaveEnum();
void AsciiString2Sjis(const uint8_t* in, uint16_t* out);
uint32_t Ascii2Sjis(uint8_t c);

namespace nf {

// A raiz de um cartão de memória do PS2 no PC. Caminhos do cartão sem a barra inicial, com "/" entre as partes
// ("BASLUS-20579BOND/icon.sys").
class PcCard {
public:
    struct Entry {
        std::string name;
        bool dir = false;
        uint32_t size = 0;
    };
    virtual ~PcCard() = default;
    // Existe? (dir: é uma pasta). A raiz "" sempre existe.
    virtual bool exists(const std::string& path, bool* dir) const = 0;
    virtual bool make_dir(const std::string& path) = 0;
    virtual bool read(const std::string& path, std::vector<uint8_t>* out) const = 0;
    virtual bool write(const std::string& path, const std::vector<uint8_t>& data) = 0;
    // Apaga um arquivo ou uma pasta vazia.
    virtual bool remove(const std::string& path) = 0;
    // O conteúdo de uma pasta, em ordem de nome (sem "." e "..").
    virtual std::vector<Entry> list(const std::string& dir) const = 0;
    // Formata: apaga tudo.
    virtual void clear() = 0;
};

class MemoryCard : public PcCard {
public:
    bool exists(const std::string& path, bool* dir) const override;
    bool make_dir(const std::string& path) override;
    bool read(const std::string& path, std::vector<uint8_t>* out) const override;
    bool write(const std::string& path, const std::vector<uint8_t>& data) override;
    bool remove(const std::string& path) override;
    std::vector<Entry> list(const std::string& dir) const override;
    void clear() override;

    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs;
};

class FolderCard : public PcCard {
public:
    explicit FolderCard(std::wstring root);
    bool exists(const std::string& path, bool* dir) const override;
    bool make_dir(const std::string& path) override;
    bool read(const std::string& path, std::vector<uint8_t>* out) const override;
    bool write(const std::string& path, const std::vector<uint8_t>& data) override;
    bool remove(const std::string& path) override;
    std::vector<Entry> list(const std::string& dir) const override;
    void clear() override;
    const std::wstring& root() const { return root_; }

private:
    std::wstring full(const std::string& path) const;
    std::wstring root_;
};

// O cartão em uso (nulo volta ao MemoryCard interno). Não toma posse.
void set_card(PcCard* card);
PcCard& card();
// saves\ na pasta do executável.
std::wstring default_save_dir();
// Operações da libmc emuladas desde o começo (relatórios).
uint32_t card_operations();

}  // namespace nf

// Só no port: o estado da máquina e do cartão (testes; também zera o cartão em memória interno).
void psi_card_debug_power_on();
// "cartao pilha 0 estado 0/0 op 4 mensagem 0 perfis 2 ops 37" (relatório das capturas).
int psi_card_describe(char* out, size_t size);
