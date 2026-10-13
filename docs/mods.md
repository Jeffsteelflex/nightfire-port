# Mods da comunidade (plano)

Plano para dar suporte a mods feitos pela comunidade. Nada disto está feito ainda; o pedido veio do fórum
(2026-10-12). A ordem combinada com o usuário: depois do Mac jogável (etapa 3d e etapa 4), encaixado entre as missões de
carro e o multiplayer; a fase 1 é pequena e pode vir logo depois da 3d.

## Princípios

- **Mods nunca trazem dados do jogo.** Um mod traz só o conteúdo dele (texturas, modelos, sons, textos, ajustes) ou
  diferenças, e roda em cima do disco do próprio jogador (o mesmo `DiscSource` de sempre). O port não hospeda nem
  distribui mods; as ferramentas de modding trabalham com o disco de quem as usa.
- **O original fica intacto.** Mods são uma camada por cima, ligada e desligada no launcher (uma seção "Mods" com a
  lista, a ordem de carga e o que cada um muda). Sem mods ligados, o jogo é exatamente o de hoje.
- **Capturas, testes e conferências sem mods.** `--screenshot`, `--bench`, `nf_tests`, `nf_level_check` e
  `nf_loader_check` ignoram os mods, a não ser com uma opção explícita (`--mods <lista>`); o relatório das capturas
  ganha uma linha `mods ...` com o que estava ligado.
- **Primeiro só dados.** Mods de dados funcionam igual no Windows e no macOS e não executam código de terceiros. DLLs ou
  dylibs nativas ficam de fora: são frágeis (cada versão do port muda a ABI), inseguras e não rodam no Mac sem
  assinatura. Código, se vier, é script num ambiente isolado (fase 4).
- **Feito para a Anthology.** A pasta de mods, o manifesto, a camada de arquivos e a lista no launcher ficam em
  `src/platform` e `src/ui`, sem nada do Nightfire, para servir aos próximos jogos (CLAUDE.md, "Goal").

## O que já existe e vira base

| O que | Onde | O que já faz |
|-------|------|--------------|
| A leitura do disco | `platform/disc_source.h` | todo arquivo do disco passa por um `DiscSource` (pasta, ISO, DVD, cópia pessoal), com caminhos do disco (`/`, sem diferenciar maiúsculas) |
| Texturas em alta definição | `data/texture_pack.h`, `textures/` | pacotes de substituição do PCSX2, achados pelos hashes da memória do GS |
| A camada 2D em alta definição | `hd_ui/map.json`, `hd_ui/svg`, `hd_ui/fonts` | troca imagens do HUD e dos menus por SVG/PNG e as fontes do jogo por fontes vetoriais |
| O visual dos menus Modern | `hd_ui/design/tokens.json` | cores, tipos, espaços e animações lidos de um arquivo |
| Textos do jogo trocados | `engine/pc_text.cpp` (`kLabelOverrides`), `Txt_BindLabel` | troca rótulos de `USATxt.dat` por texto do port, sem mexer no arquivo do jogo |
| As tabelas do jogo | `game/static_init.cpp` (`kTables`) | armas, munição, dano da IA, pontuação etc. copiadas do executável na partida |
| Os recursos de cada fase | `RuntimeGfx::on_texture`/`on_entity`, a tabela de hashes (`engine/hashtable.cpp`) | cada item do pacote é registrado pelo hash (o byte alto é o tipo: 01 item do mapa, 02 entidade, 04 animação, 05 skin, 06 script, 07 pacote) e vira recurso da GPU na carga |
| Os formatos | `docs/formats.md`, os exportadores para glTF em `tools/` | fases, personagens com esqueleto e animações exportados |

## Fases

### Fase 1: a base (poucos dias)

- **A pasta e o manifesto.** `mods/<nome>/mod.json` ao lado do executável (no macOS, na pasta de dados do usuário,
  `os::user_files_dir`): nome, versão, autor, a versão do port e do disco que ele espera (`SLUS-20579`), uma descrição
  curta e o que ele toca (texturas, textos, arquivos).
- **A camada de arquivos.** Um `DiscSource` que sobrepõe os arquivos dos mods ligados aos do disco, pelo caminho do
  disco (`mods/<nome>/disc/PS2/...` substitui `PS2/...`), na ordem de carga; o primeiro mod da lista ganha. Como todo o
  jogo já lê pelo `DiscSource`, nada mais muda.
- **A lista no launcher.** Uma seção "MODS" com cada mod (ligado/desligado, a ordem, a descrição, os avisos: versão
  diferente, dois mods que tocam no mesmo arquivo), gravada no `nightfire.ini` (`[mods]`).
- **Os sistemas de hoje como mods.** Um pacote de texturas HD, uma troca de imagens do HUD (`map.json`) ou um
  `tokens.json` dentro de um mod funcionam como hoje, só que ligados e desligados pelo launcher.
- **Traduções.** Um arquivo de textos por rótulo (`texts.json`: rótulo -> texto, com os mesmos cuidados de
  `kLabelOverrides`: só as conversões de formato do texto original) aplicado no `Txt_BindLabel`. Com o `language_pc`, a
  tradução também cobre os textos do port. Uma tradução completa para português é um bom primeiro mod; as fontes do
  jogo não têm Ã, Õ, Ç (docs/port.md), então ela pede a camada HD ou glifos novos nas fontes.

### Fase 2: substituição fina e ajustes de jogo (~1 semana)

- **Itens de um pacote pelo hash.** Em vez de substituir um pacote de fase inteiro, o mod troca um item pelo código:
  uma textura (`03xxxxxx`), um modelo (`02xxxxxx`), uma animação, um som. O lugar é o registro dos itens na carga
  (`LoaderProcess` e a tabela de hashes) para os dados do jogo, ou `RuntimeGfx::on_texture`/`on_entity` para trocas só
  de desenho (como as texturas HD).
- **Ajustes das tabelas.** Um arquivo de patch por campo nas tabelas que `kTables` copia (`weapon_data`, `ammo_data`,
  `DroneDamage_*`, `DroneFiring`, `Plr_DMod_*`, `ScoringTable`...): munição, cadência, dano, precisão da IA,
  dificuldade, pontuação. Aplicado depois da cópia na partida, com o nome do campo e uma checagem de faixa.
- **Os relatórios.** O que cada mod trocou, para quem testa (e para o `nf_loader_check --mods`).

### Fase 3: conteúdo novo (semanas)

- **Modelos.** Um importador de glTF para o formato de desenho do port (as malhas, os materiais com o estado do GS, a
  skin com o esqueleto do original para personagens animados), começando por trocas só de desenho, sem mexer em colisão.
- **Sons e música.** Amostras de WAV/OGG no lugar das do banco (`sfx_iop`, por item e amostra) e faixas de música no
  lugar dos trechos do `MusicStream`, respeitando os marcadores que a música interativa usa.
- **As fases.** Mudar a posição de objetos, inimigos e itens (os blocos de posicionamento que o `parsemap` lê), por
  fase e por índice.
- **Um guia de modding.** Os formatos (`docs/formats.md`), os exportadores e importadores, os hashes de cada fase, como
  testar um mod.

### Fase 4: mais adiante

- **Scripts** num ambiente isolado (Lua, por exemplo), com eventos do jogo (fase carregada, a cada tick, item pego,
  inimigo morto) e acesso só de leitura ao estado, mais uma lista curta de ações. O determinismo das capturas fica de
  fora: scripts nunca rodam nelas.
- **Fases novas.** Exigem salas, portais, colisão, a rede de navegação da IA e os pacotes: ferramentas grandes, só se a
  comunidade quiser.
- **Multiplayer**: arenas e modos, depois que o multiplayer existir.

## Atalhos e relações

- **RTX Remix** (docs/remix.md, parado): já é uma plataforma de mods com materiais PBR, luzes e path tracing; se o
  caminho Direct3D 9 for retomado, os modders do Remix trabalham nele sem nada do nosso lado.
- **O projeto da versão de Xbox** (nightfire-cxbx): a engine a pé é o mesmo código da Eurocom, então ferramentas de
  formato podem ser compartilhadas.

## Perguntas em aberto

- Onde os mods ficam no macOS dentro do pacote `.app` (etapa 4): na pasta de dados do usuário, como o `nightfire.ini`.
- Se a ordem de carga resolve conflitos por arquivo inteiro ou por item (fase 2).
- O formato dos patches de tabela (JSON com nomes de campos: precisa de nomes para os campos que hoje são só
  deslocamentos).
