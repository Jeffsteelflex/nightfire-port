# Formatos de arquivo

Notas de engenharia reversa da versão NTSC-U (SLUS-20579).
Tudo aqui foi verificado contra os arquivos, exceto onde marcado como palpite.

## Executáveis

| Arquivo       | Papel | Engine | Símbolos |
|---------------|-------|--------|----------|
| `SLUS_205.79` | Idêntico ao `BASE.ELF` | | |
| `BASE.ELF`    | Loader: sobe os IRX e carrega `ACTION.ELF` ou `DRIVING.ELF` | Eurocom | stripped |
| `ACTION.ELF`  | Fases a pé, menus, multiplayer | EuroLand (Eurocom) | completos (3038 funções) |
| `DRIVING.ELF` | Fases de veículo | EAGL/REAL/CARP/SND (EA) | stripped; o `DRIVING/DRIVING.SYM` é de outra compilação (`docs/driving.md`) |

## FILES.BIN

Contêiner sem cabeçalho. O índice está no `ACTION.ELF`, no objeto global `FileList`
(`.data`, 3776 bytes = 118 entradas de 32 bytes).

```c
struct FileListEntry {   // 32 bytes
    char     name[16];   // ASCII, terminado em zero
    uint32_t offset;     // bytes dentro do FILES.BIN
    uint32_t size;
    uint32_t pad[2];     // zero
};
```

As entradas cobrem o arquivo inteiro sem sobreposição. Sobram 109 bytes de padding no fim.
Ferramenta: `tools/filesbin.py`.

Conteúdo:

| Nome | Qtde | Conteúdo |
|------|------|----------|
| `<LANG>Txt.dat`, `<LANG>TxtU.dat` | 18 | Textos por idioma: DU, FR, GR, IT, JAP, SP, SW, UK, USA |
| `070xxxxx.bin` | 37 | Pacote principal de fase: mapas, esqueletos, scripts, menus |
| `07Fxxxxx.bin` | 36 | Animações da fase de mesmo número |
| `078xxxxx.bin`, `079xxxxx.bin`, `07Axxxxx.bin` | 26 | Seções carregáveis 1, 2 e 3: cenas em tempo real (NIS) |
| `TuningVars.txt` | 1 | Parâmetros de jogo em texto puro: mira, auto-aim, saúde ao continuar |

Os hashcodes de fase usados pelo código aparecem nas tabelas `sp_level`, `mp_level`
e `MissionData` do `ACTION.ELF`. Hashcodes `09xxxxxx` são as fases de veículo.

### Como o nome do arquivo é montado

`LoaderLoad(tLoaderType type, uint hash, uchar *buf, uint section)` (`0x001d4a00`)
monta o nome com `sprintf("%s%8.8x.bin", prefixo, hash)`, depois de trocar os bits
20 a 23 do hashcode da fase:

| Condição | Bits 20-23 | Exemplo |
|----------|-----------|---------|
| `section != 0` | `section + 7` | `07800001`, `07900001`, `07A00001` |
| `type == 0` ou `type == 3` | `0xF` | `07F00001` |
| `type == 5` | `0xD` | nenhum arquivo no disco |
| `type == 4` | usa `CommonAnimFile` no lugar do hash | |
| demais | inalterado | `07000001` |

As seções 1 a 3 são carregadas por `LoadableLoad(hash, section)` (`0x001d69e8`),
que primeiro descarta os itens de hashtype `0x04000000` (animações) da seção anterior.

O nome de cada item é convertido em hashcode lendo até 8 dígitos hexa
depois da última barra. Nomes como `SKEL0002` geram hash 0.

## Pacotes de fase (07xxxxxx.bin)

```
0x000  uint32 table_size     múltiplo de 0x800
0x004  uint32 count
0x008  uint8  hash[12]       não verificado
       zeros até 0x800
0x800  item_entry[count]     tamanho variável
0x800 + table_size           dados, concatenados na ordem da tabela
```

```c
struct item_entry {
    uint32_t size;    // alinhado a 32 bytes
    uint8_t  type;
    char     name[];  // hashcode em hexa ("04000425") ou nome ("SKEL0002"), terminado em zero
};
```

A soma de `0x800 + table_size + sum(size)` bate exatamente com o tamanho dos 99 pacotes.
Ferramenta: `tools/levelpak.py`.

Tipos de item, confirmados em `LoaderProcess()` (`0x001d4880`):

| Tipo | Itens | Prefixo | Tratamento no jogo | Conteúdo |
|------|-------|---------|--------------------|----------|
| 0  | 978    | `01` | `parsemap_parsemap(hash, 0)` | mapa ou modelo |
| 1  | 35     | `01` | `AnimPostLoadInit(); parsemap_parsemap(hash, 1)` | mapa principal da fase, um por pacote |
| 2  | 551    | `01` | `parsemap_parsemap(hash, 0)` | mapa ou modelo |
| 3  | 1217   | `05` | `AnimLoadFile(hash, 0)` | animação |
| 4  | 18252  | `04`, `00` | `AnimLoadFile(hash, 0)` | animação (pacotes `07F`) |
| 5  | 18231  | `06` | `AnimLoadFile(hash, 0)` | animação |
| 6  | 1485   | `SKEL` | `AnimSkeletonProcess(buf)` | esqueleto |
| 7  | 468    | `06` | `Script_Load(hash, ...)` | script de cena (ver "Scripts de cena") |
| 8  | 34     | `08` | `MenuManager_Load(hash, buf)` | menu (ver "Menus") |
| 11 | 666    | `01` | igual ao tipo 0, com `MemType = 1` | mapa ou modelo |
| 12 | 26     | vazio | grava em `LoadableFiles[]` | descritor de seção carregável, 32 bytes |
| 15 | 1      | vazio | `SetUpIconFile(buf, len)` | ícone do memory card |
| 16 | 33     | `woman` etc. | `PS2SetUpWoman(buf)` | ? |

As texturas não têm tipo próprio: ficam embutidas nos itens de mapa.

## Itens de mapa (tipos 0, 1, 2 e 11)

Lidos por `parsemap_parsemap` (`0x001d1cf0`), `parsemap_parsenextblock` (`0x001d12e8`)
e `parsemap_handle_block_id` (`0x001d1020`). Ferramenta: `tools/mapfile.py`.

```
0x000  uint32 version
versão 1:  uint32 block_offset[]   offsets relativos a 0x004, um por bloco,
                                   até chegar no bloco 0x1D
outras:    blocos em sequência a partir de 0x004 (não visto no disco)

bloco:     uint32 header           id = header >> 24, size = header & 0xFFFFFF
                                   size inclui o próprio header
           uint8  payload[size - 4]
```

Blocos tratados pelo jogo:

| ID | Tratador | Observação |
|----|----------|------------|
| 0x00, 0x08 | `parsemap_block_dict_xyz` | |
| 0x01 | `parsemap_block_dict_uv` | |
| 0x02 | `parsemap_block_dict_rgba` | |
| 0x03, 0x0A, 0x0B | `parsemap_block_dict_comlist` | |
| 0x04, 0x1F | `parsemap_block_entity_params` | |
| 0x05 | `AIPath_Parse` | rede de caminhos da IA (ver "Rede de caminhos da IA") |
| 0x0E | `parsemap_block_map_header` | contagem de entidades no payload + 4 |
| 0x0F | `parsemap_block_palette_header` | entradas de 8 bytes |
| 0x10 | `parsemap_block_texture_header` | entradas de 12 bytes |
| 0x11 | `parsemap_block_palette_data_pc` | |
| 0x12 | `parsemap_block_palette_data_psx` | dados de paleta usados no PS2 |
| 0x15, 0x16 | `parsemap_block_texture_data_pc` | pixels; o PS2 usa o 0x16 |
| 0x17 | `parsemap_block_texture_data_GameCube` | não usado no PS2 |
| 0x18 | `parsemap_block_texture_data_Xbox` | não usado no PS2 |
| 0x19 | `parsemap_block_path_data` | |
| 0x1A | `parsemap_block_map_data_static` | posicionamento de entidades estáticas |
| 0x1C | `parsemap_block_memory_discard` | o que vem depois pode ser liberado |
| 0x1D | fim | |
| 0x20 | `parsemap_block_dict_norm` | |
| 0x21 | `parsemap_block_portal_data` | portais para culling |
| 0x22, 0x23 | `parsemap_block_coll_data` | colisão (formato antigo) |
| 0x24 | inline | ponteiro em `cel->gfx + 0xC` |
| 0x25 | `parsemap_block_datums` | |
| 0x26 | `Light_SetAmbientRadiators` | |
| 0x27 | `parsemap_block_LOD` | |
| 0x28 | `Sound_LoadMapSounds` | |
| 0x29 | `parsemap_block_texture_data_pcdx` | |
| 0x2B | `parsemap_block_morph_data` | alvos de morph do rosto (`entgfx_tag + 0x14`; "Morphs do rosto", abaixo) |
| 0x2C | `parsemap_block_hashlist` | hashcodes de dependências |
| 0x2D | `parsemap_block_PS2_GFX` | geometria do PS2, ponteiro em `cel->gfx + 0x8` |
| 0x2E, 0x2F | `parsemap_block_Coll_Data_New` | colisão |
| 0x30 | `parsemap_block_particles` | definições de emissores de partículas (`Emitter_LoadDefs`, abaixo) |

O mapa principal da primeira fase (`07000001`, item `01000093`) tem 1040 blocos:
215 entidades, cada uma com um bloco `0x2D` de geometria e um `0x04` de parâmetros,
190 blocos de colisão `0x2E` e 192 texturas.

### Texturas

Cabeçalho de texturas (bloco `0x10`), 12 bytes por textura, na mesma ordem dos
blocos de pixels (`0x16`) e de paleta (`0x12`), que vêm intercalados:

```c
struct TextureHeader {   // 12 bytes
    uint8_t  flags;      // bit 0: 8 bpp (senão 4 bpp); bits 0x08 e 0x10 desconhecidos
    uint8_t  unknown;
    uint16_t width_m1;   // largura - 1
    uint16_t height_m1;  // altura - 1
    uint16_t anim;       // byte baixo: número de quadros (1 = estática); byte alto: desconhecido
    uint32_t data;       // 0xFFFFFFFF no arquivo; ponteiro para os pixels após a carga
};

struct PaletteHeader {   // 8 bytes, bloco 0x0F
    uint32_t info;       // byte baixo: número de cores - 1 (15 ou 255); resto desconhecido
    uint32_t hash;       // 0xFFFFFFFF ou hashcode registrado na hashtable
};
```

- Pixels lineares, sem swizzle. Em 4 bpp, o nibble baixo é o primeiro pixel.
- Texturas animadas guardam os quadros em sequência no mesmo bloco de pixels.
- Paleta RGBA de 4 bytes por cor, com alfa de 0 a `0x80` (padrão do GS).
- Paletas de 256 cores estão em ordem CSM1: em cada grupo de 32 cores, as faixas
  8-15 e 16-23 vêm trocadas. Verificado visualmente.

Casos raros de texturas animadas com contagem de quadros inconsistente (3 em todo o jogo)
ainda são ignorados pelo exportador.

### Geometria (bloco 0x2D)

Lida por `parsemap_block_PS2_GFX` e `psiCreateEntityGfx` (`0x001171b0`), desenhada por
`RecurseAndDrawBoxes` (`0x001233e8`) e `DrawThisBox` (`0x001230d8`).
Ferramenta: `tools/geometry.py` (exporta `.glb`).

```
bloco + 0x04  uint32 gfx_header_offset     0x10 = bloco vazio
gfx_header:   int32  box_count
              uint32 boxes_offset          relativo ao início do bloco
              uint32 extra_offset          skinning ou objetos quebráveis (não analisado)

struct GlistBox {       // 0x38 bytes
    float    bbox_min[3], bbox_max[3];
    int32_t  child[2];           // -1 = folha (desenhada direto)
    uint32_t dma_offset;         // cadeia de DMA, chamada por uma tag CALL
    uint32_t texref_offset;      // lista de offsets de tags REF de textura, fim = 0xFFFFFFFF
    uint32_t unknown[2];
    uint32_t vertex_count;
    uint32_t flags;
};
```

O jogo chama a cadeia com `DMAtag CALL` e dispara o microprograma da VU1 com
`MSCALF 0` (caixa precisa de recorte) ou `MSCALF 2` (caixa inteira visível).

Conteúdo da cadeia:

| Tag DMA | Códigos VIF | Significado |
|---------|-------------|-------------|
| `REF qwc=4` | nulos | `addr` = índice da textura no próprio item; trocado na carga |
| `CNT` | `STCYCL 4,4`; `UNPACK V4-32 x1` | GIF tag: tira de triângulos, textura, Gouraud, alpha |
| | `UNPACK V2-32 xN` | UV em float |
| | `STCYCL 3,1`; `UNPACK V3-32 xN` | posição em float, já em coordenadas do mundo |
| | `UNPACK V4-8 xN` (com sinal) | normal em x/127; `w = -1` marca início de tira |
| | `UNPACK V4-8u xN` | cor RGBA, `0x80` = 1.0 |
| | `MSCNT` | a VU1 processa e desenha o lote |
| `CNT` | `FLUSHE`; `DIRECT` | registradores do GS (pares A+D), depois de cada `REF` de textura |
| `RET` | | fim |

Cada vértice ocupa 3 quadwords na memória da VU1: posição, cor e normal.
As UVs ficam numa área separada. A palavra w da GIF tag do lote (os REGS só usam 3 registradores:
RGBAQ, ST, XYZF2) escolhe a rotina do microprograma: 8 para a comum (`0x25e` sem luz nem tweak,
`0x2a3` com tweak, `0x340` com luzes), 0x10 para a de mapa de ambiente (`0x2e0`, ST pela normal).
As contas de cada rotina e a névoa estão em "Microprograma da VU1", abaixo.

Estado do GS das malhas: o pacote `DIRECT` de cada referência de textura escreve, com pares A+D
(GIF tag PACKED, descritor 0xE), `ALPHA_1` (0x42), `ZBUF_1` (0x4e) e `TEST_1` (0x47); valem para os
lotes seguintes (o `FLUSHE` antes garante que o lote anterior já foi desenhado). Todos os lotes com
vértices dos dados escrevem os três. Nos 36 pacotes (16027 malhas, 1361 com mais de um estado),
contando partes (lotes seguidos com a mesma textura e o mesmo estado):

| ALPHA_1 | equação | TEST_1 | ZBUF_1 | partes |
|---------|---------|--------|--------|-------:|
| `0x44` | `(Cs - Cd) * As + Cd` | `0x5001b` (alfa >= 1) | escreve Z | 95305 |
| `0x44` | | `0x5001b` | não escreve (`ZMSK`) | 1496 |
| `0x44` | | `0x507fd` (alfa > 0x7f) | escreve Z | 294 |
| `0x44` | | `0x507fd` | não escreve | 36 |
| `0x7f00000048` | `Cs * As + Cd` (aditiva) | `0x5001b` | não escreve | 5635 |
| `0x7f00000048` | | `0x5001b` | escreve Z | 377 |
| `0x7f00000042` | `Cd - Cs * As` (subtrativa) | `0x5001b` | não escreve | 101 |
| `0x7f00000042` | | `0x5001b` | escreve Z | 8 |
| `0x64` (FIX 0) | `Cd`: só grava o alfa (máscara) | `0x5000b` (sempre) | não escreve | 6 |
| `0x64` (FIX 0) | `Cd` | `0x5001b` | escreve Z | 15 |
| `0x58` | `Cs * Ad + Cd` (pela máscara) | `0x5001b` | não escreve | 6 |

Todos os PRIM dos lotes têm ABE = 1. Os três últimos só aparecem em `07000041`. O teste de Z é
sempre `ZTE` com `GEQUAL`. Com `TEX0.TFX = MODULATE` e `TCC = 1` (`CacheTexture`), o alfa do pixel é
`At * Av / 128`: o alfa da cor do vértice conta (água, mistura de duas texturas no chão, papel
translúcido, brilhos que somem nas bordas).

### Microprograma da VU1 (iluminação, mapa de ambiente, névoa)

O código da VU1 está no `ACTION.ELF`, na seção `.vutext` (0x241c80, 0x2880 bytes): duas cadeias de
DMA (tag END) com comandos `MPG` da VIF. A primeira (`My_scissor_code`, 0x4b4 instruções em 0..0x25a0)
é o microprograma da VU1; a segunda (`My_dma_start`, 77 instruções) é o da VU0 do `SkinIt`. As seções
`.DVP.overlay..0x*` têm o mesmo código pelo endereço da VU, e a tabela de símbolos guarda os rótulos do
fonte (`_$ROTATE_LIGHT`...). Desmontador: `tools/vudis.py` (`docs/setup.md`). Endereços abaixo em
instruções (x8 = bytes).

Pontos de entrada (`MSCAL`/`MSCALF`): 0 `ROTATECLIPED` (caixa com recorte: depois da rotina, recorta
os triângulos no `SCISSOR`), 2 `ROTATE` (caixa inteira visível), 4 `POINTSPRITECOLOUR` (0x46e) e
6 `POINTSPRITE` (0x42b), os sprites de `psiDrawParticleList`.

Antes das caixas de cada modelo, `DrawThisBox` manda 24 quadwords para o endereço 0 da VU1 (`UNPACK
V4-32`), montados por `FillMatrixChainRot` (rígidos, 0x00122310) ou `FillMatrixChainSkin` (skinning,
0x00122788; o objeto é a identidade e tudo está no mundo); `MulMatrix2` (0x24f) os põe nos registradores:

| qw | Conteúdo | Registrador |
|----|----------|-------------|
| 0-3 | objeto -> tela (matriz do objeto x `world_view` x `view_screen`) | vf01-vf04 |
| 4-7 | objeto -> recorte (`view_clip`) | vf13-vf16 |
| 8, 9 | `TLight[0]`, `TLight[1]` no espaço do objeto: `(L - t) * R^T` (a inversa calculada como transposta, só certa sem escala); no mundo com skinning | vf06, vf05 |
| 12, 13 | `TColor0`, `TColor1` (r, g, b = cor 0..255 x intensidade x multiplicador; w = 1 / raio²) | vf08, vf07 |
| 14 | `TColor2` (não usado) | |
| 15 | `TAmbient` (tweak / 255) | vf12 |
| 16-19 | mapa de ambiente: `.x` = linha 0 da câmera / 256, `.y` = linha 1 / 256, qw 19 = 0.5 (só se a caixa 0 tem a flag 1; com skinning, sempre) | vf05-vf08 em `ROTATE_ENV` |
| 20-22 | `.w` = 0xcf, 0x1d8, 0x2e1: os três buffers de lote; qw 20 `.z` = 0x25 | |
| 22 | `.x` = rotina: 0, + 4 com luzes (`PS2NumLights` > 0), + 2 com `TweakRGBA` | |

Um lote fica num dos buffers: GIF tag na base, depois 3 quadwords por vértice (posição V3-32, cor
V4-8u como inteiros 0..255, normal V4-8 como inteiros com sinal) e as UVs em base + 0xc7. A rotina
salta para `JUMP_TABLE` + w da GIF tag + rotina de qw 22 (`JR`): w = 8 dá 0x25e (`ROTATE_FAST`), 0x2a3
(`ROTATE_TWEEK`), 0x340 (`ROTATE_LIGHT`, com ou sem tweak); w = 0x10 dá sempre 0x2e0 (`ROTATE_ENV`).
A saída sobrescreve o lote para o `XGKICK` (REGS 0x412: ST, RGBAQ, XYZF2): STQ = (u, v, 1) / w na
posição, a cor nova no lugar da cor e o XYZ (`FTOI4`) por cima da normal, cuja palavra w vira F e ADC
do XYZF2 (w = -1: ADC = 1, começo de tira). O PRIM de todos os lotes é 0x5c (tira, Gouraud, textura,
ABE) sem FGE: não há névoa por vértice. Nos 36 pacotes, 987 partes de 876 malhas usam a rotina do mapa
de ambiente, e essas malhas são exatamente as que têm a flag 1 na caixa 0 (com ela
`FillMatrixChainRot` manda os qw 16-19, então a rotina nunca lê valores velhos); metade dos vértices tem r, g ou b acima de 0x80 (a cor
do vértice vai até o dobro da textura) e o alfa nunca passa de 0x80 (`nf_loader_check`, linha `VU1`).

Fórmulas (todas por vértice; `FTOI0` trunca; `c` é a cor do disco, 0x80 = 1.0):

- `ROTATE_FAST` (0x25e): a cor não muda.
- `ROTATE_TWEEK` (0x2a3): `c' = FTOI0(TAmbient * c)` nos quatro canais.
- `ROTATE_LIGHT` (0x340), com `n = normal / 128` (ITOF0 e `MULi` com I = 1/128) e, para cada luz,
  `D = luz - p` (espaço do objeto), `att = max(0, 1 / |D|² - TColor.w)` (`ERSADD` e `DIV`),
  `s = max(0, att * (n . D))`:
  `rgb = min(255, TAmbient * c + TColor1 * s1 + TColor0 * s0)`, `a = min(255, TAmbient.a * c.a)`,
  `FTOI0`. Ou seja, a luz cai com `cos * (1/d - d/r²)` e some no raio; `n . D` não é normalizado. Sem
  a segunda luz `psiLight_SetLights` deixa a cor 0 (w = 1), que não soma nada. O tweak é o termo
  ambiente (com tweak branco, a cor do disco).
- `ROTATE_ENV` (0x2e0): `c' = FTOI0(TAmbient * c)` (nunca com luzes) e
  `ST = 0.5 + (n . linha da câmera) / 256` com `n` a normal inteira (cerca de 127 por unidade) no
  espaço do objeto e as linhas 0 e 1 da matriz da câmera no mundo; `Q = 1/w`. As UVs do lote não são
  usadas.
- Skinning: o `SkinIt` roda na VU0 (`SKIN_2_BONE` em 0, `SKIN_2_BONE2` em 0x17, `SKIN_1_BONE` em
  0x2e): posição `w * (p * M0) + (1 - w) * (p * M1)`; normal `FTOI0(ITOF15(palavra) * R0)`, isto é, a
  normal do disco (x 64) vira x 128 e gira só pelo primeiro osso, sem peso. A VU1 usa as mesmas rotinas
  no mundo (luzes sem transformar).

Névoa (`FogOn`, por fase em `psiPreGameRun`): não é por vértice. Depois da lista opaca, `psiPostSolid`
chama `psiDrawFogPass`, que manda dois pacotes montados na carga:

1. `RGB2APacket` (`CreateMoveRG2BA`): lê o buffer de Z (PSMZ24) como textura PSMZ16 e o escreve no
   quadro visto como PSMCT16, em faixas de 8 pixels deslocadas e com `FBMSK` 0x3fff, de modo que os
   bits 8-15 do Z de cada pixel vão para o alfa dele.
2. `FogOverlayPacket` (`CreateFogSprite`): sprites da largura da tela com a cor `PS2FogR/G/B`,
   Z 0x7fff, `TEST` com Z `GREATER` e `ALPHA` 0x11 = `(Cd - Cs) * Ad + Cs`: onde o Z do pixel é menor
   que 0x7fff, a cor vira `névoa + (cor - névoa) * Ad / 128`, com `Ad = (Z >> 8) & 0xff`.

O Z do GS vem da matriz de tela de `PS2WorldViewVU1Kick2`: `Z = A / z + B` (z na frente da câmera, em
metros), com `A = Ps2Near2 * Ps2Far2 * (Ps2Far1 - Ps2Near1) / (Ps2Far2 - Ps2Near2)` e
`B = (Ps2Near1 * Ps2Far2 - Ps2Far1 * Ps2Near2) / (Ps2Far2 - Ps2Near2)` (`Ps2Near1` = 8,
`Ps2Near2` = 0.1, `Ps2Far2` = 2^24); o `FTOI4` e o XYZF2 empacotado deixam a parte inteira. Assim
Ps2Far1 é o Z no plano de perto (0,1 m) e não muda o plano de longe, que é `Ps2Far2` = 16.777.216 m
(a matriz de recorte usa perto 0,1 e longe 2^24): Ps2Far1 só estica a escala do Z, e com ela a névoa.
O Z é apagado com 1 (`CreateZClearSprite`), então onde nada escreveu Z fica só a cor da névoa. A
fração da cor que fica é cerca de `(Ps2Far1 / 327680) / z`:

| Fase | Ps2Far1 | Cor | Começa | Metade | 90% névoa |
|------|---------|-----|-------:|-------:|----------:|
| 07000002 | 9987520 | 24 2e 4c | 30,5 m | 61 m | 306 m |
| 07000004 | 9379776 | 11 15 21 | 28,6 m | 57 m | 287 m |
| 07000005 | 19118528 | 3a 4d 68 | 58,4 m | 117 m | 585 m |
| 07000006 | 8059232 | 2e 3c 52 | 24,6 m | 49 m | 247 m |
| 07000008 | 13191350 | 3b 50 69 | 40,3 m | 81 m | 404 m |
| 0700000c | 20475840 | d1 d5 d6 | 62,5 m | 125 m | 626 m |
| 0700000d | 19707840 | 92 85 69 | 60,2 m | 120 m | 603 m |
| (outras, sem névoa) | 4194240 | | | | |

O céu da camada 1 (antes do cenário) e a lista opaca levam a névoa; o céu da camada 0, a lista
transparente, os fragmentos e os sprites vêm depois. Na lista transparente (`psiStartAlpha` com
`FogOn` liga `AlphaFogObjects`), `FillMatrixChainRot` troca o alfa do tweak por
`min(255, int(Z do centro da caixa 0) >> 7) * olda / 255` (`psiSetTweakA`, que também muda `olda`:
desenhos seguidos sem novo tweak somam a redução); caixas que precisam de recorte (`CheckObjectClipBox`
= 1) ficam com o alfa que tinham. `psiFog` é vazia e `Ps2FogNear/Ps2FogFar` não são usados.

### Rede de caminhos da IA (bloco 0x05)

Lido por `AIPath_Parse` (`src/engine/aipath.cpp`). Palavras de 32 bits: [1] versão (só 8 é lida),
[2] número de entradas; as entradas começam em +0xc. Cada entrada é um caminho ou um limite:

| Palavra | Conteúdo |
|---------|----------|
| 0x00-0x1f | nome (0x80 bytes) |
| 0x20 | flags: bit 0 limite; senão 0x2 rede de navegação (a única nas buscas de rota), 0x4 patrulha, 0x8 rota de missão |
| 0x21-0x28 | zero no disco |
| 0x29 | número de nós |
| 0x2a | deslocamento dos nós a partir de +0xac da entrada |

Caminhos: nós de 0x40 bytes (`MAPNODE_tag`: índice e caminho reescritos na carga, flags com
0x2468abce = nenhuma, ponteiro da sala preenchido depois, posição em +0x10 com y + 0,4 na carga),
depois uma palavra com o número de ligações e as ligações de 0x10 bytes (`LINK_tag`: no disco
(caminho << 16) | índice, flags e uso zerados na carga, nós a e b, comprimento calculado por
`AIPath_Prepare`). Limites: nós de 0x20 bytes (`BMAPNODE_tag`: altura em +8, posição em +0x10) e
segmentos de 0x14 bytes (`BLINK_tag`: flags, nós a e b, e dois ponteiros para as listas das salas).
A entrada seguinte começa logo depois da última ligação ou segmento. Os nós e as ligações ficam no
arquivo carregado; o jogo só aloca o estado de busca (`NODE_tag`, 0x30 bytes por nó) e as listas
dos nós de limite (`BNODE_tag`).

### Emissores de partículas (bloco 0x30)

`Emitter_LoadDefs` (0x0017ab40):

```c
u32 count;
struct {                    // count vezes; para no primeiro tipo cujo byte baixo não é 4
    u32 type;               // byte baixo 4; type >> 8 = flags (0: sprites aditivos; senão transparência)
    u32 hash;               // hashcode da definição (0x0c00xxxx)
    u32 texture;            // hashcode da textura (item 0x03) ou 0xffffffff
    u32 model;              // hashcode do modelo por partícula (item 0x02) ou 0xffffffff
    u16 count, rate;        // partículas; nascimentos por tick
    f32 size;               // tamanho do sprite
    f32 life, life_rand;    // segundos; a parte sorteada é ± metade (Rand_FRandHalf)
    f32 gravity;            // vel.y -= gravity * 9.8 por segundo
    f32 speed, speed_rand;  // m/s
    f32 polar, polar_rand;  // ângulo com a direção do emissor
    f32 azimuth, azimuth_rand;
    u32 key_count;
    struct { f32 r, g, b, a, time; } keys[key_count];  // cor por fração da vida; r, g, b de 0 a 255
} defs[count];
```

Na memória (`EMITTERDEF_tag`, `src/engine/emitter.h`) a definição tem 0x40 bytes com os campos na
ordem life (0x0c) ... azimuth_rand (0x2c), size (0x30), key_count (0x34), count, rate, flags
(0x38..0x3c), seguidos das chaves, com r, g e b divididos por 2 (escala do GS). Todas as fases com
mapa trazem as mesmas 18 definições no item `0100002e` (faíscas, poeira, fumaça, fogo...).

Itens do tipo 2 (personagens e objetos animados por ossos, como veículos e árvores)
usam outro esquema, descrito em "Modelos com skinning" abaixo.

Resultado da exportação: 35 mapas principais, de 8 mil a 133 mil triângulos cada,
sem lotes incompletos.

### Modelos com skinning

Desenhados por `psiDrawSkinObjectMatrix` (`0x00111828`). Quando a estrutura extra do
cabeçalho de geometria é diferente de zero, ela descreve o skinning:

```c
struct SkinInfo {            // apontada por gfx_header.extra_offset
    uint32_t input_offset;   // vértices de entrada, um par de quadwords por vértice
    uint32_t output_size;    // bytes do buffer de saída (mesmo tamanho da entrada)
    uint32_t patch_offset;   // índices de palavra da cadeia DMA, fim = -1
    uint32_t bones_offset;   // índices de osso (u8), fim = 0xFF
};

struct SkinVertexIn {        // 32 bytes
    float   x, y, z;         // posição na pose de repouso (pose T), espaço do modelo
    float   weight;          // peso do primeiro osso
    int32_t nx, ny, nz;      // 16 bits altos: normal * 64; 16 bits baixos: índices de osso
    int32_t w;               // 16 bits altos: índice de morph (-1 = nenhum); bit 15: início de tira
};
```

Fluxo em tempo de execução:

1. `CacheSkin` (`0x001112c0`) copia a cadeia DMA da caixa (`box + 0x20`, tamanho em
   `box + 0x28`), aloca o buffer de saída e soma a base dele ao endereço das tags REF
   listadas em `patch_offset`.
2. As matrizes dos ossos de `bones_offset` vão para a memória de dados da VU0
   (`0x11004000`), com as 4 linhas de cada matriz espaçadas de 64 quadwords.
3. `SkinIt` (`0x0010bf30`) leva a entrada para o scratchpad em blocos de até 512 quadwords,
   aplica morphs (bloco `0x2B`, se houver: "Morphs do rosto", abaixo) e transforma cada par com o
   microprograma da VU0 (`vcallms 0` e `0xB8`). A saída tem, por vértice, posição e normal em sequência.
4. A cadeia copiada é desenhada: as tags REF fazem `UNPACK V4-32` do buffer de saída com
   `STCYCL 3,2` ou `2,1`, encaixando posição e normal nas posições da VU1 e pulando a da cor.
   Cada trecho começa um quadword antes, por isso o endereço `0x7FFFFFF0` (-16) aparece.
   Cor, UV e GIF tag continuam nos pacotes CNT.

O exportador emula as escritas na memória da VU1 guardando a origem de cada quadword
e usa a entrada do `SkinIt` diretamente, o que dá o modelo na pose T.

Ossos de cada vértice: `nx & 0x7FFF` é o slot do primeiro osso na paleta (`bones_offset`),
com peso `weight`; `ny & 0x7FFF` é o slot do segundo, com peso `1 - weight`.

Mais detalhes conferidos nos dados e no código:

- A lista de ossos cabe na VU0 (no máximo 64 slots); no disco ela tem de 28 a 55 entradas, com
  índices de osso até 72. Os pesos estão sempre em [0, 1].
- `psiDrawSkinObjectMatrix` só desenha a primeira caixa do cabeçalho; as 582 malhas com skinning
  do disco (tipos 2 e 11) têm uma caixa só.
- A cadeia começa com uma chamada do microprograma sem vértices (GIF tag com NLOOP 5 e `MSCNT`
  antes de qualquer dado); o lote real reenvia a GIF tag no mesmo endereço.
- Posição e normal de cada vértice na VU1 vêm sempre do mesmo vértice de entrada (quadwords `2k` e
  `2k + 1` do buffer de saída).
- O `SkinCache` (`0x00246350`, 128 entradas) é procurado só pelo objeto: no mesmo quadro, um
  segundo desenho do mesmo objeto repete a cadeia já transformada. `AgeSkinCache` (em `psiPreDraw`)
  envelhece as entradas a cada quadro.

O port decodifica essas malhas em C++ (`nf::decode_gfx`, `src/data/map_item.cpp`) e faz o skinning
na GPU (`docs/port.md`, "Modelos com skinning").

### Morphs do rosto (bloco 0x2B)

Os rostos dos personagens (63 malhas com skinning no disco, uma por cabeça, e as 63 peles `05xxxxxx` que as
usam, todas com 22 alvos) mexem a boca, as pálpebras, os olhos e as sobrancelhas por alvos de morph: deslocamentos
da pose de repouso somados antes do skinning. `parsemap_block_morph_data` (`0x001d23e8`) guarda o conteúdo do bloco
em `entgfx_tag + 0x14` da entidade (os blocos de uma entidade vêm antes do bloco de parâmetros que a fecha).

Conteúdo, em elementos de 12 bytes:

```c
uint32_t vertices;          // vértices com morph (230 a 277 nas cabeças do disco)
uint32_t targets;           // alvos por vértice: 22 em todas
uint32_t first;             // primeiro elemento dos deltas = ceil((3 + vertices) * 4 / 12)
uint32_t index[vertices];   // índice no dicionário xyz (blocos 0x00/0x08) de outras plataformas; o PS2 não lê
...                         // zeros até o elemento first
float    delta[vertices][targets][3];   // x, y, z no espaço do modelo (pose de repouso), em metros
```

O vértice de entrada do `SkinIt` (`SkinVertexIn`, acima) guarda nos 16 bits altos do `w` o índice de morph `m`
(-1 = nenhum): o elemento onde começam os deltas daquele vértice, `first + j * targets`. Um vértice da malha que
não mexe tem -1; os que mexem (cerca de 340 das ~1.800 entradas de uma cabeça; ~600 dos vértices desenhados, com
as repetições das tiras) apontam para o seu `j`. O `SkinIt`, para cada vértice com `m != -1`, antes da VU0:

```
pos.xyz += soma, k de 0 a Morph_Count - 1, de Morph_Weight[k] * elemento[m + Morph_Index[k]]
```

(`vmulx.xyz` do delta pelo peso e `vadd`, só x, y, z voltam para o vértice; a entrada no scratchpad, não a do
disco, é mudada). A normal não muda. 36 das 63 cabeças têm na entrada do `SkinIt` um vértice com `m = 0`, que
leria o cabeçalho e os índices como floats: números desnormalizados (inteiros pequenos), que a VU0 trata como 0,
ou seja, sem morph; e nenhuma tira o desenha (`nf_loader_check`: `no_cabecalho=0`).

Os 22 alvos, pelo que os deltas movem (`tools/morph.py targets`; o rosto olha para +z, y para cima): 0 a 9 a boca
(visemas: lábios, cantos, a boca aberta), 10 as pálpebras fechando (piscar), 11 e 12 os olhos para cima e para
baixo, 13 e 14 os olhos para -x e +x, 15 as sobrancelhas, 16 a 20 expressões das bochechas e da boca, 21 o queixo
caindo (o maior: 2,6 cm no lábio de baixo).

Os pesos vêm das **sequências de rosto**: itens tipo 4 com o bit 3 das flags (+0x0C) e 22 canais, no modo de canal
único de `AnimFrameCopy` (cada canal é um valor: o peso de um alvo, com o mesmo formato de trechos e polinômios das
sequências de ossos, `c0 + c1 (f - 1) + ...`). 142 sequências assim no disco; o byte +0x3C (o "esqueleto") é 0 nas
dos homens e 1 nas das mulheres. Um script de animação (06) as toca pelo comando 0 como a do corpo, no slot 1
(`ANIMSCRIPT_tag::seq[1]`, quadro de 22 floats = 0x58 bytes, que `AnimScriptNew` só aloca nas peles com morphs);
quando a primeira sequência que um script começa é de rosto, ele ganha a flag 0x40000 (só de rosto) e fica fora
da mistura do corpo. Três tipos de script de rosto, pela marca (`ANIMSCRIPT_tag::tag`):

- 0xe0000000, a expressão parada pelo estado de alerta: `Male_Facial_State_Anims` (`0x002c6ea8`) = 060000f8,
  060000f9, 060000f5, 060000f6 e `Female_Facial_State_Anims` (`0x002c6ec0`) = 0600036d, 0600036f, 0600036a,
  0600036b (alertas 0 a 3; 2 ou 3 quadros, tocados com velocidade 0: fica o primeiro). A de alerta 0 é quase
  neutra; a de alerta 1 muda a boca e as bochechas (alvos 2 e 16 em 1);
- 0xe0000001, a piscada: o índice 4 das tabelas, 060000f3 (10 quadros) e 06000370 (12): o alvo 10 sobe a 1 no
  quadro 4 e volta, com os olhos um pouco para baixo (alvo 12 até 0,3);
- 0xe0000002, a fala: os scripts das falas da IA (`Drone_SfxInfo`; o comando 1 do script é o som da fala, no
  quadro 1; 199 scripts do disco tocam sequências de rosto, contando expressões, piscadas, falas e cenas) e, numa cena (`ScriptCam`), todo script de uma pele com
  morphs (`AnimScriptInit` os marca): as cenas trazem corpo e rosto no mesmo script (por exemplo 0600011b: corpo
  04000020 e rosto 04000021, 500 quadros).

O jogo junta os pesos por objeto em `AnimFacialBlend` e escolhe os 8 maiores para o desenho (`AnimObjectDraw`:
`Morph_Count`, `Morph_Index[22]`, `Morph_Weight[22]` em ordem crescente de alvo); ver docs/engine.md, "Rostos".

`tools/morph.py` lista as cabeças e as peles (`list`), descreve os alvos (`targets`), decodifica uma sequência de
rosto (`faceseq`), exporta a cabeça com os 22 alvos como morph targets glTF e, opcionalmente, a animação dos pesos
de uma sequência (`gltf`, para o `tools/viewer`), e mede a abertura da boca de um `--frames-dump` contra a voz do
`--audio-dump` (`mouth`). `tools/geometry.py` também exporta os alvos dos personagens.

### Esqueletos e skins de animação

- Item `SKEL` (tipo 6, `AnimSkeletonProcess`): `u16 skeleton_id`, `u8 bone_count`.
- Item tipo 3 (hash `05xxxxxx`, `AnimProcessSkinData`, `0x001bd500`):

```
+0x00  u32 hash
+0x04  f32[3]                 (sempre 1.0 nos casos vistos)
+0x10  u8 n_meshes, u8 n_discrete, u8 unknown, u8 n_extra, u8 skeleton_id
+0x15  u8 parent[bone_count]  0x80 | pai; 0xFF na raiz
       u8 discrete[n_discrete]; alinha a 4; u32 discrete_hash[n_discrete]
       alinha a 4; u32 mesh_hash[n_meshes]   hash da entidade; bit 0x100000 = malha trocável
       bone_count x { f32 t[3]; f32 q[4] }   pose inversa (Quat_QuatTransToMat)
       n_extra x 36 bytes
```

Todas as 67 mil rotações de pose inversa no disco são identidade: a articulação `i` fica
em `-t[i]` no espaço do modelo, com origem no quadril e Y para cima. Os prefixos de hash
usados por `AnimLoadFile` são `04` (sequências, `AnimProcessSeqData`), `05` (skins) e
`06` (scripts, `AnimProcessScriptData`).

O exportador liga cada modelo tipo 2 ao skin que lista a entidade dele e grava um skin
glTF com hierarquia, matrizes de pose inversa, `JOINTS_0` e `WEIGHTS_0`.
111 dos 114 modelos saem com esqueleto; os outros 3 são objetos rígidos de várias peças.
Resultado: 114 modelos únicos em todo o disco, sem vértices desalinhados.
Personagens não aparecem no bloco `0x1A`: entram na fase pelo sistema de IA.

### Entidades e posicionamento

Cada item de mapa tem uma lista de entidades. A entidade `i` é formada pelo `i`-ésimo
bloco de parâmetros (`0x04`) e pelo `i`-ésimo bloco de geometria (`0x2D`).
O total bate com a contagem no `map_header` (`0x0E`, payload + 4).

Bloco de parâmetros (`parsemap_block_entity_params`, `0x001d1508`), payload:

| Offset | Campo |
|--------|-------|
| 0x00 | hashcode da entidade (`02xxxxxx`), ou `0xFFFFFFFF`. Registrado na hashtable global |
| 0x04 | copiado para `cel + 0x44` |
| 0x08 a 0x2C | bounding box e outros campos copiados para a célula |

Bloco `0x1A` (`parsemap_block_map_data_static`, `0x001d1998`), só no mapa principal,
lista o que existe na fase. Entradas de tamanho variável:

```c
struct Placement {               // 76 bytes + 8 * param_count
    uint16_t entity;             // índice no próprio mapa, se hash == 0xFFFFFFFF
    uint16_t pad;
    uint32_t hash;               // entidade de qualquer item do pacote, pela hashtable
    uint32_t flags;              // byte baixo: tipo do objeto; 0x8000: célula estática do mundo
    float    position[3];
    float    euler[3];           // radianos; é o que o motor usa (cel + 0x70)
    float    quat[4];            // conjugado da rotação; desatualizado em ~1% das entradas
    float    scale[3];
    uint32_t unknown[2];
    uint32_t param_count;
    struct { uint32_t index, value; } params[];
};
```

- Entidades usadas em vários lugares têm geometria em coordenadas locais. A geometria de
  peças únicas do cenário já vem em coordenadas do mundo, com posição zero na entrada.
- A rotação é `RotMatrix(euler)` (`0x001e2368`). O motor multiplica vetor-linha por matriz
  (`RotPreTransVec`), então o glTF usa a transposta. O quatérnio gravado, conjugado,
  coincide com essa matriz em 98,9% das entradas; o resto está desatualizado ou nem é
  normalizado, por isso o exportador recalcula a partir dos ângulos.
- Em todo o jogo, todos os hashcodes resolvem dentro do próprio pacote e toda entidade
  do mapa principal é usada por pelo menos uma entrada.
- Entradas sem geometria (cerca de 30%) são gatilhos, volumes de IA, luzes e afins.
  Os tipos ainda não foram mapeados; ver `parsemap_create_dynamic_objects` (`0x001d04f0`).

## Arquivos de texto (`*Txt.dat`)

Um arquivo por idioma no FILES.BIN (`UKTxt.dat`, `USATxt.dat`, `FRTxt.dat`...; os `*TxtU.dat`, com o
dobro do tamanho, não são lidos por `Txt_LoadLanguage`). O nome vem de `FNames[SelLang]` (`0x002c7498`: 0 UK,
1 francês, 2 alemão, 3 espanhol, 4 italiano, 5 holandês, 6 USA, 7 japonês, 8 sueco); a versão
americana usa o 6. Lido por `Txt_LoadLanguage` (portado em `src/engine/txt.cpp`):

```
uint32 size                      bytes das strings
char   strings[size]             Latin-1 (0x92 = ’, 0x85 = …, 0x99 = ™), terminadas em zero
       pad                       4 - size % 4 bytes (4 quando já alinhado)
uint32 N                         entradas (a 0 não está no arquivo: é "")
uint32 offsets[N - 1]            das strings 1..N-1, relativos ao começo das strings (o primeiro é 0)
uint32 F                         tabelas de rótulos
uint32 fixups[F]                 índice da primeira string de cada tabela
```

Um rótulo de texto do código (`0x0200000d`, por exemplo) é `tabela << 24 | índice`: a string é
`Bank[fixups[rótulo >> 24] + (rótulo & 0xffffff)]`. No `USATxt.dat`: N = 2801, F = 7, fixups = 0, 1000,
1812, 1903, 1958, 2065, 2183. A tabela 0 tem os menus (`0x1c3` = "Player"), a 1 as dicas e ajudas
(`0x1000216` = "Hold manual aim to use camera"), a 2 as mensagens do jogo (`0x200000d` = "Picked up %s",
`0x200000b` = "Press Action to open door", `0x2000003` = "Door Unlocked"), a 3 os nomes das missões
(`0x3000000` = "Training"), a 4 os objetivos, a 5 opções e a 6 as falas das cenas (`0x6000085` = "Is
that dress standard Company issue, Nightshade...?"). O índice final 0 não é uma string: `Txt_BindLabel`
devolve um buffer novo do StringHeap (256 buffers de 0x168 bytes para textos montados).

`Snd2Lbl` (`0x002d25d8`, 484 pares {rótulo, som}) liga as falas aos sons: quando um som da tabela
começa, `Sound_DoSubtitle` põe o rótulo como legenda pelo tempo do som (`SFXOutputData`, `0x002c94a0`:
0x18 bytes por som, duração em segundos em +0x10) x 0.92. O bit 31 do rótulo força a legenda mesmo
com elas desligadas.

## Fontes

As três fontes são dados do ACTION.ELF (`.data`), não do FILES.BIN; o port as copia na partida
(`kTables`, `src/game/static_init.cpp`). A imagem de cada uma é uma textura comum (4 bits, 16 cores, alfa
na escala do GS) do item de mapa `010001d8`, que todos os 36 pacotes carregam:

| Fonte (`FontTable`) | Formato `0xff n` | Textura | Tamanho | Espaço entre linhas | Espaço |
|---|---|---|---|---|---|
| `FONTNFont2` | 1 | `0x03000005` | 512 x 128 | 6 | 8 |
| `FONTSerpLight` | 2 | `0x03000003` | 512 x 64 | 5 | 3 |
| `FONTMedium` | 3 | `0x030000d8` | 512 x 128 | 7 | 4 |

```c
struct FONT_tag {          // 0x1c bytes (FONTNFont2 0x00305a58, FONTMedium, FONTSerpLight a cada 0x20)
    uint32_t kern_count;   // pares de kerning da fonte (42, 442, 321)
    KERN_tag* kern;        // KernNFont2 0x002d9be0...
    uint16_t first, last;  // 0x21 e 0xff: códigos aceitos
    uint32_t count;        // 149 glifos
    CHARDATA_tag* chars;   // FontDataNFont2 0x002d9ce0... (ordenados pelo código, até 252)
    float line_gap;        // espaço entre linhas
    float space;           // largura do espaço (vezes 1.25 e a escala)
};
struct CHARDATA_tag {      // 0x18 bytes
    uint16_t u, v, w, h;   // retângulo na textura; w e h também são o tamanho na tela (escala 1)
    int16_t y_offset;      // deslocamento vertical; o do primeiro glifo ('!') dá a linha de base
    int16_t pre, post;     // espaço antes (só com um caractere antes) e depois
    uint16_t kern_count;   // pares deste glifo
    KERN_tag* kern;
    uint16_t code;         // Latin-1
    uint16_t pad;
};
struct KERN_tag { uint16_t first, second; int8_t adjust; uint8_t pad; };  // 6 bytes
struct FONTTABLE_tag { uint32_t hash; FONT_tag* font; };  // FontTable 0x002dd8b0: 5 entradas, 3 usadas
```

Medida de um texto (`__Font_DrawText`): para cada caractere, a caneta anda o kerning (`Font_GetKernAdjust`
procura o glifo do caractere ANTERIOR e percorre os pares dele até um segundo caractere menor ou igual ao
atual: como as listas estão em ordem crescente, só o primeiro par de cada glifo vale), o espaço antes (se
há anterior), a largura e o espaço depois, tudo vezes a escala. Um código sem glifo dentro de
[first, last] usa o glifo anterior ao buraco (a busca binária devolve a posição de baixo). Na quebra de
linha (`Font_WordWrapString`) cada glifo conta kerning + antes + depois + largura + 1, sem escala.

Formato de texto (bytes de controle antes do texto, lidos por `Font_ParseFormat`): `0xfa r g b x` cor (o
desenho a troca pela cor do sprite), `0xfb n`/`0xfc n` ignorados, `0xfd`, `0xfe n` alinhamento (2 à
direita, 3 centrado), `0xff n` fonte n. Dentro do texto: `0xba` troca a cor (0x785a14ff, depois
0x7d6d59ff), `~X` desenha um símbolo de botão de `specialchar` (`0x002dd8d8`, 17 entradas de 0x18 bytes:
caractere, textura `0x03000075` 256 x 128, recorte, largura e altura na tela) e 0x99 (™) vira T e M em
meio tamanho.

## Texturas dos sprites (hashcodes `03xxxxxx`)

Imagens 2D (HUD, ícones, tarjas, fontes, menus) são texturas comuns de itens de mapa (bloco 0x10,
registradas na tabela de hashcodes como as outras, com `maptex_tag` {registro, largura, altura}), mas com
hashcodes do tipo 03. `hashtable_set_sprite` liga um sprite a uma delas. Os itens que as trazem:

| Item | Pacotes | Texturas 03 | Uso |
|---|---|---|---|
| `010001d8` | 36 | 3 | as fontes |
| `0100002d` | 34 | 5 de 6 | `0x03000002` (32 x 32, a tarja de cinema `BorderInfo`) e outras do HUD |
| `0100002e` | 34 | 28 de 67 | HUD |
| `01000070` | 34 | 12 | HUD |
| `010000a2` | 33 | 6 | `0x03000075` (os símbolos de botão de `specialchar`) |
| `01000081` | 33 | 3 de 32 | |
| `0100009a` | 1 (`07000048`, o menu) | 203 | as telas do menu |

Outros itens com uma a cinco texturas 03 aparecem em grupos de fases (missões com radar, satélite...).
`BorderInfo` (`0x002c9468`, um `SpriteInfo` de 44 bytes no `.data`): imagem preta 512 x 64, câmera 4,
escondida até uma cena com câmera.

## Áudio

O som das fases a pé e dos menus não está no `FILES.BIN`: fica na pasta `PS2/` do disco e é tocado pelo
driver de som do IOP, `MODULES/SFX.IRX` (o "EuroSound" da Eurocom: `Q:/SS/source/SFX/SFX_Main.cpp`), que o
`BASE.ELF` sobe junto com o `LIBSD.IRX` (a biblioteca da SPU2 da Sony). O `SNDDRV.IRX` ("SND_Library_EE_Driver",
da EA) e o `SDRDRV.IRX` (o `libsdr` da Sony) servem ao `DRIVING.ELF` e aos filmes, não ao `ACTION.ELF`.

O `SFX.IRX` é um ELF relocável do IOP (`e_type` 0xff80, MIPS R3000) **com** `.symtab` e STABS (seção `.mdebug`):
os nomes das 250 funções e as estruturas abaixo vêm de lá (`python tools/sound.py irxsyms SFX.IRX`). O Ghidra o
carrega com a linguagem `MIPS:LE:32:default` (`tools/ghidra_irx.sh import SFX.IRX`, num projeto separado do
ACTION.ELF; o importador de STABS do plugin do Emotion Engine falha, os nomes vêm da `.symtab`); `gp` = 0x3b880
(o `.sdata` começa em 0x33890). Tudo o que segue foi conferido no código do driver e nos arquivos.

| Arquivo | Conteúdo |
|---------|----------|
| `PS2/SBINFO.SBI` | 150 `int32`: o hash de cada banco de efeitos (-1 = vazio). O banco de hash `h` é o arquivo `SB_<i>` com `SBI[i] == h` |
| `PS2/<LÍNGUA>/SB_<i/8>/SB_<i>.SFX` | as definições dos sons do banco |
| `PS2/<LÍNGUA>/SB_<i/8>/SB_<i>.SHF` | os cabeçalhos das amostras |
| `PS2/<LÍNGUA>/SB_<i/8>/SB_<i>.SBF` | as amostras, PS-ADPCM, inteiras na RAM de som |
| `PS2/<LÍNGUA>/STREAMS/STREAMS.LUT` | índice das falas: 1681 x {marcadores, tamanho, dados, tamanho} |
| `PS2/<LÍNGUA>/STREAMS/STREAMS.BIN` | as falas (PS-ADPCM mono, 22050 Hz), lidas do disco durante o jogo |
| `PS2/MFXINFO.MXI` | 150 `int32`: o hash de cada faixa de música (a faixa `n` é `MFX_<n>` com `MXI[n-1] == hash`) |
| `PS2/MUSIC/MFX_<n/16>/MFX_<n>.SMF` | os marcadores da música interativa |
| `PS2/MUSIC/MFX_<n/16>/MFX_<n>.SSD` | a música (PS-ADPCM estéreo, 32000 Hz) em streaming |
| `PS2/DEBUG.TXT` | sobra da ferramenta: os nomes `SFX_*` e os `.wav` de origem de um banco |

A língua (`SFXLanguage`, `SFXSetLanguage`) é `ENGLISH` na versão americana (só ela está no disco). O caminho base é
`\PS2\` (`SFXFilePath`; `SFXSetFilePath` não é chamado pelo jogo).

### Os bancos de efeitos (`SB_n`)

Uma fase carrega **um** banco (`Sound_Ready`, 0x001dceb8, no carregamento: `SFXReset` e `SFXLoadSoundBank(hash)`):
o hash por fase está em `tools/sound.py` (`LEVEL_BANKS`): 07000001 = 1 (`SB_10`), 07000012 = 0x1f (`SB_22`), o
menu principal 07000048 alterna 0xc (`SB_0`) e 0x38 (`SB_1`) a cada carga, o multijogador usa 0x16 (`SB_2`). Cada
banco tem de 5 (menus) a 407 sons e até 291 amostras, 1.3 a 1.9 MB de ADPCM.

`SB_n.SFX`:

```c
uint32 n;                       // sons no arquivo
struct SFXHeader { int32 HashCode, Offset; } headers[n];   // o número do som (o mesmo do jogo) e onde começa
// em cada Offset: SFXParameters seguido de SampleCount x SFXSamplePoolFiles
struct SFXParameters {          // 68 bytes (STABS)
    int32 ReverbSend;           // 0x00 != 0: a voz manda para o reverb (VMIXEL/R)
    int32 TrackingType;         // 0x04 0 2D, 1 posição relativa ao ouvinte, 2 3D, 3 sempre à frente do ouvinte
    int32 InnerRadiusReal;      // 0x08 metros: volume cheio até aqui
    int32 OuterRadiusReal;      // 0x0c metros: silêncio daqui em diante
    int32 MaxVoices;            // 0x10 cópias do mesmo som ao mesmo tempo (0 = sem limite)
    int32 Priority;             // 0x14 SFXGetImportance: a menor é roubada primeiro
    int32 Group;                // 0x18 (não usado pelo driver)
    int16 MaxReject;            // 0x1c != 0: acima do limite, recusa o novo em vez de roubar o mais velho
    int16 Action2, IgnoreAge;   // 0x1e, 0x20 (não usados)
    int16 Ducker;               // 0x22 != 0: abaixa a música para 100 - Ducker por DuckerLength
    int32 DuckerLength;         // 0x24 milissegundos (o relógio do driver, que anda de 100 em 100)
    int32 MasterVolume;         // 0x28 0-100
    int32 MinDelay, MaxDelay;   // 0x2c, 0x30 intervalo entre amostras de um som em sequência ou em laço (ticks)
    int16 MultiSample;          // 0x34 != 0: toca as amostras em sequência (ou todas juntas, com Polyphonic)
    int16 RandomPick;           // 0x36 (o driver sorteia sempre que MultiSample = 0)
    int16 Shuffled;             // 0x38 sequência embaralhada
    int16 Loop;                 // 0x3a no fim, recomeça (SFXUpdateEnding)
    int16 Polyphonic;           // 0x3c todas as amostras ao mesmo tempo, cada uma numa voz (a música dos menus)
    int8  Outdoors;             // 0x3e o volume segue SFXIndoorsVolume (50 em salas abertas)
    int8  PauseInNis;           // 0x3f pausa enquanto uma cena (NIS) toca
    int32 SampleCount;          // 0x40
};
struct SFXSamplePoolFiles {     // 28 bytes
    int32 FileRef;              // amostra do .SHF; < 0: a fala -1 - FileRef de STREAMS.LUT
    int32 PitchOffset, RandomPitchOffset;   // pitch = f + f * (PitchOffset + rnd(-1..1) * RandomPitchOffset) / 0x3000
    int32 BaseVolume, RandomVolumeOffset;   // 0-100, mais rnd(-1..1) * RandomVolumeOffset
    int32 Pan, RandomPan;       // -100 (esquerda) a 100, só nos sons 2D; os da música dos menus usam +-102400 (= +-100)
};
```

`SB_n.SHF`: `uint32 n` e `n` x `SampleHeaderData` (36 bytes): `Flags` (bit 0: a amostra é um laço, `VoiceLooping`),
`Address` (o deslocamento no `.SBF`; o driver o troca pelo endereço na RAM de som, 0x5010 + o que já foi carregado),
`Size` (bytes de ADPCM), `Frequency` (**o pitch da SPU**, não Hz: 0x1000 = 48 kHz; no disco 0x400 = 12000 Hz,
0x3ad = 11025, 0x75a = 22050, 0xaab = 32000), `RealSize` (o tamanho sem o bloco de silêncio do fim), `NumberOfChannels`
(1), `BitsPerSample` (4), `psi_SampleHeader` (índice x 0x60, só da ferramenta) e `LoopOffset` (não usado pelo driver:
o laço vem das flags do ADPCM).

`SB_n.SBF`: as amostras em sequência, PS-ADPCM sem cabeçalho (o "VAG" da SPU), cada uma começando com um bloco de
silêncio. O driver copia o arquivo inteiro para a RAM de som (`psiLoadSoundBank`, `sceSdVoiceTrans`).

### PS-ADPCM

Blocos de 16 bytes com 28 amostras de 16 bits: byte 0 = filtro (bits 4-7) e deslocamento (bits 0-3), byte 1 =
flags, 14 bytes de nibbles (o de baixo primeiro). Amostra = `(nibble << 12, com sinal) >> deslocamento + (s1 * f0 +
s2 * f1 + 32) >> 6`, cortada em 16 bits, com `(f0, f1)` = (0, 0), (60, 0), (115, -52), (98, -55), (122, -60).
Flags: bit 2 = início do laço (a SPU guarda o endereço), bit 0 = fim do bloco: com o bit 1 a voz volta ao início
do laço, sem ele a voz para. As amostras de laço do disco começam com o bit 2 no bloco 1 e terminam com 3; as outras
terminam com 1 (as falas: 1 e depois 7). O driver programa um envelope que não muda o som (`ADSR1` 0x000f, `ADSR2`
0x1fc0: ataque de 3 amostras, sustentação cheia, soltura em 2 amostras).

Prova de que a decodificação está certa (`python tools/sound.py wavbank 10 out/audio/SB_10`): as 236 amostras do
banco da 07000001 saem com envelopes que decaem (tiros, impactos), picos perto de 0 dB e 0 a 11 amostras cortadas em
dezenas de milhares (normalizadas no estúdio, não erro de decodificação); um preditor errado produz ruído com
envelope plano ou explosões de corte. As falas têm envelope silábico (-7 a -12 dB, caindo a -60 no fim) e 1.4 a 1.9 mil
cruzamentos por zero por segundo, o esperado para voz a 22 kHz.

### Falas (`STREAMS.LUT` / `STREAMS.BIN`)

`STREAMS.LUT`: 1681 x `{u32 offset_marcadores, u32 tamanho_marcadores (0x88), u32 offset_dados, u32 tamanho_dados}`
(o driver lê até 3000). Os dados começam 0x1000 depois dos marcadores. Os marcadores (`StreamMarkerHeaderData`, 20
bytes: `StartMarkerCount`, `MarkerCount`, `StartMarkerOffset`, `MarkerOffset`, `BaseVolume` = 100) apontam para um
`MusicMarkerStartData` (52 bytes) e dois `MusicMarkerData` (32 bytes: `Name`, `Pos`, `MType`, `Flags`, `Extra`,
`LoopStart`, `MarkerCount`, `LoopMarkerCount`): o início e o fim (`MType` 9, `Pos` = fim dos dados em bytes). Os
dados são PS-ADPCM mono; o driver os toca a 22050 Hz (`Frequency` 0x5622; o `StartSample` põe o pitch 0x759).

Um som cujo `FileRef` é negativo é uma fala: `SFXInitialiseStream(FileRef)` toca a entrada `-1 - FileRef` da LUT
num dos dois canais de fala (`Streams[1]` e `[2]`; o `[0]` é a música). Cada canal de fala tem duas vozes da SPU
reservadas (a da frente e a de trás, como um som 3D).

### Música (`MFX_n`)

`MFX_n.SMF`: o mesmo cabeçalho de marcadores (`StartMarkerCount` trechos, `MarkerCount` marcadores, os dois
deslocamentos e `BaseVolume`), com dezenas de trechos e marcadores (a música interativa). Cada trecho
(`MusicMarkerStartData`, 52 bytes) é uma cópia do seu marcador inicial seguida de `MarkerPos` (o índice desse
marcador na lista), `IsInstant` (um salto para ele não espera o próximo marcador), `InstantBuffer` e `State[2]`. Os
marcadores (32 bytes, em ordem de `Pos`, um deslocamento em bytes no `.SSD`) têm `MType`: 10 início de trecho, 0
ponto de salto (os mais comuns), 6 e 7 laço (a leitura volta a `LoopStart` e o marcador atual passa a ser
`LoopMarkerCount`; o 7 não aceita saltos), 9 fim (a música para); o driver também trata um tipo 5 (pausa até o
próximo salto), que não aparece no disco. Nas 16 faixas: 2038 do tipo 0, 303 do 10, 77 do 6, 61 do 7 e 61 do 9.
`SFXJumpToMusicMarker(trecho, imediato)` (pedido pelo jogo) guarda o pedido; o driver salta no próximo marcador que
não seja do tipo 7, ou na hora se o trecho tem `IsInstant` ou o pedido é imediato, e devolve ao EE o último pedido
(`MusicLastJump`) e o trecho que toca (`MusicSectionNowPlaying`) em `SFXActiveData` +0x144/+0x14c. Os cortes caem em
múltiplos de 0x100 (`StreamAlign`). `MFX_n.SSD`: PS-ADPCM estéreo a 32000 Hz, **0x80 bytes do canal esquerdo e 0x80
do direito** alternados (`psiStreamMemCpy` separa cada 0x100 bytes em `StereoLeft`/`StereoRight`; as flags dos blocos
não contam, quem decide são os marcadores); 6 a 34 MB por faixa, 16 faixas (hashes 0xf-0x18, 0x19, 0x1e, 0x22-0x24,
0x27: `MXI[n-1]`). A faixa de cada fase e os trechos dos eventos vêm da tabela `MapMusic` do ACTION.ELF (0x13 palavras
por fase: `UpdateMusicalEvents`, 0x00114e70, começa a faixa, salta para o trecho da morte e o da vitória), e as
funções `UpdateMFX_*` (uma por fase) escolhem os outros saltos pelos eventos do jogo (`Music_Event`: alarme, combate,
cenas). Os créditos tocam a faixa 0x27 (`MFX_16`) com `SFXStartMusic(0x27, 0)`. `python tools/sound.py music -v` lista
os marcadores; `musicwav` decodifica.

A música dos menus não é uma faixa `MFX`: são os sons 0x1d7 (`SB_0`) e 0x470 (`SB_1`), dois laços mono de 32000 Hz
(36 s, laço a partir do bloco 9567) tocados juntos, um em cada lado (`Polyphonic`, `Pan` +-100), no volume da música.

### A tabela dos sons no ACTION.ELF (`SFXOutputData`)

`SFXOutputData` (0x002c94a0, 1549 x 0x18 bytes; o número do som indexa a tabela, 0x60d sons): `+0x00` o número,
`+0x04 float` alcance mínimo (padrão 10), `+0x08 float` alcance máximo (padrão 20), `+0x0c float` o quanto a IA
ouve (`Sound_Alertness`), `+0x10 float` duração em segundos (legendas), `+0x14 u8` laço, `+0x15 u8` "3D de verdade"
(um som desses não começa além de 1.1 x o alcance e não ganha legenda além da metade). `python tools/sound.py
sfxtable`.

O bloco 0x28 do mapa (`Sound_LoadMapSounds`) traz os sons de ambiente da fase: `u32 n` e `n` x 32 bytes (`GMapSounds`,
até 50; `MAPSOUND_tag` em `engine/sound.h`): `+0x04` o som, `+0x08` a posição (3 floats), `+0x14 float` um parâmetro
que vai como `short` no 6º argumento de `Sound_Play3D`, `+0x18 float` o raio, que é ao mesmo tempo onde ele liga
(`HandleMapSoundAllocation` o liga com a câmera dentro do raio e o desliga além de 1.2 x o raio², comparando distâncias
ao quadrado) e o alcance máximo do som, `+0x1c float` o alcance mínimo. O jogo guarda até 50 (o laço do carregador
não limita; o port corta em 50).

### Do jogo ao IOP

```
Sound_Play / Sound_Play3D (0x001dd880 / 0x001dd548)       nó de DynamicSoundList (DYNAMICSOUNDS, 0x60 bytes, 256)
  -> Sound_UpdateListeners (Game_Run, a cada tick)        ouvinte = câmera; sala -> SFXSetEnvironment; sons de mapa
     -> Sound_UpdateSounds: estado 0 -> Sound_ReqestPlaySfx -> SFXStart / SFXStart3D (comandos 3 / 0xd)
                            estado 1 -> SFXSetPositionAndVelocity (0xb), SFXIsSFXPlaying (fim -> 2)
                            estado 2 -> SFXRemove (0xc), devolve o nó
  -> SFXUpdate (psiPreGame_Run/psiPostGame_Run, uma vez por VBlank): 0x19 (cena), 4 (update), 5 (fim)
     -> PS2_SFX_SendBatchCommandsToIOP: sceSifCallRpc(0x40013575) com SFXBatchCommands (até 0x800 palavras)
        <- 0x154 bytes: SFXActiveData = {n, 40 x {Handle, HashCode}, ...} os sons vivos no IOP
```

O "handle" de um som é o próprio nó `DYNAMICSOUNDS*`; o IOP o recebe como número e o devolve em `SFXActiveData`
(`SFXIsSFXPlaying` procura nele e em `SFXRequestedActiveData`, os 40 pedidos dos últimos 5 VBlanks). Coordenadas
vão em milímetros (`x 1000`, inteiros). Os comandos lentos (carregar banco, pausar as streams, suspender o acesso ao
disco) usam um segundo buffer (`SFXBatchCommands2`) e esperam o IOP. Comandos (`sifrpc_server`, 0x0000f600 no IRX):

| Cód. | Comando | | Cód. | Comando |
|---|---|---|---|---|
| 0 | `SFXInitialise` | | 0x12 | `SFXStopMusic` |
| 1 | `SFXLoadSoundBank(hash)` | | 0x13 | `SFXJumpToMusicMarker(marcador, imediato)` |
| 2 | `SFXDeLoadSoundBank` | | 0x14 | `SFXSetFilePath` (50 palavras) |
| 3 | `SFXStart(som, handle)` | | 0x15 | `SFXMusicSetVolume` |
| 4 | `SFXUpdate` | | 0x16 / 0x17 | `SFXPauseAllStreams` / `SFXUnPauseAllStreams` |
| 5 | fim do lote: liga/desliga as vozes, devolve `SFXActiveData` | | 0x18 | `SFXSetNumberOfListeners` |
| 6 / 7 | `SFXFadeDown` / `SFXFadeUp` | | 0x19 | `NisPlaying`, `NisQuitten` |
| 8 | `SFXSetVolume` | | 0x1b | `SFXSetLanguage` |
| 9 / 0xa | `SFXPause` / `SFXUnPause` | | 0x1c | `SFXSetMode` |
| 0xb | `SFXSetPositionAndVelocity(pos, vel, handle, som)` | | 0x1d | `SFXSetEnvironment(reverb, aberto)` |
| 0xc | `SFXRemove(handle, som)` | | 0x1e-0x23 | mudo/som de música e efeitos |
| 0xd | `SFXStart3D(som, pos, vel, handle, volume, mín, máx)` | | 0x24 / 0x25 | suspende / retoma o disco |
| 0xe | `SFXSetupListener(pos, vel, dir, up, norm, n)` | | 0x26 | `SFXPreCache(som, atraso)` |
| 0xf | `SFXReset` | | 0x11 | `SFXStartMusic(faixa, marcador)` |

### O que o driver do IOP faz

- **Itens** (`SFXItem`, 704 bytes, 40 em `SFXActive`): um som começado; até 10 vozes (`VoiceHandle[10]`, uma por
  amostra polifônica), a fila de amostras de um som em sequência (`CuedSamples`, `CueSampleWaiting`), posição,
  raios, pausa, fade de fim de cena. `SFXStart3D` procura o som nos bancos carregados, cria o item (`VolumeOverRide`
  = 100: **o volume pedido pelo jogo é ignorado**, o driver escreve 100 por cima do argumento) e chama `SFXSetup`.
- **Quantas vozes** (`SFXSetup`): o limite é `15 - SFXStartedCount / 400` vozes de efeitos (`SFXStartedCount` soma
  100 a cada som começado, até 2000, e cai 10% por VBlank: rajadas de sons reduzem o limite para 10). Um som 3D com
  prioridade < 100 nem começa se `volume(distância) x prioridade < vozes x limite x 2000 / limite²`. Acima do limite,
  os itens de menor `Priority` (que não sejam `TrackingType` 3) são marcados para morrer; com `MaxReject` o próprio
  novo morre. `MaxVoices` limita as cópias do mesmo som: o mais velho (`Counter`) morre, ou o novo com `MaxReject`.
- **Vozes da SPU** (`psiRequestVoiceHandle`): 48 vozes (24 por núcleo); a primeira que não está tocando nem travada.
  Um som 3D (`TrackingType` != 0) usa **duas**: a da frente (L/R) e a "de trás" (L = v, R = 0x7fff - v: a mesma
  amplitude com a fase invertida no canal direito, a codificação de surround do Pro Logic; no modo mono, sem
  inversão). As duas falas e a música reservam 6 vozes na partida.
- **Volume** (`ES_CombineVolumes`): `BaseVolume x SfxMasterVolume x (100) x MasterVolume x FadeVolume`, x
  `SFXIndoorsVolume` nos sons `Outdoors`, / 2 debaixo d'água (fora de `BonbUnderWaterSFX`), x `(100 - (100 -
  DuckerCurrentVolume) / 3)` nos sons que não abaixam a música, e por fim **ao quadrado** (`v² / 100`). Os sons 0x1d7
  e 0x470 usam `MusicMasterVolume`. Na voz: `0..100 -> 0..0x3fff` (`PS2_GetSampleVolume`).
- **3D** (`PS2_SFXCalculate3D`, inteiro, em decímetros): `d = sqrt(dx² + dy² + dz²) - 10 x interno`, `v = 100 - d x
  100 / (10 x (externo - interno))`, cortado em 0..100 e **elevado ao quadrado** (`v² / 100`); a direção normalizada
  (x 1000) faz o produto com o vetor da direita do ouvinte (`ListenerNorm`), com o oposto e com o "para trás" (-Dir):
  `trás = (d_trás + 1000) x v / 2000`, `esquerda = ((d_dir + 1000) x v / 2000) x (100 - trás) / 100`, a direita
  igual com -Norm, a voz de trás com `trás x 75 / 100`. A raiz é o `PS2_SquareRoot` do driver: 10 passos de Newton
  a partir de 1, `x - (x² - n) / 2x` com a divisão de 64 bits truncada: chegando por cima, para um acima da raiz
  (10 m = 100 dm vira 101 dm) e, como só reduz o erro à metade a cada passo enquanto está longe, não converge para
  distâncias grandes (50 m vira 51.8 m, 100 m vira 129.7 m): o som some antes do raio externo. Sem efeito Doppler (a velocidade é lida e
  descartada). `TrackingType` 3 toca à frente do ouvinte com metade do volume em cada lado e 30% atrás.
- **2D** (`PS2_SFXSetup2D`): `esquerda = (100 - pan) / 2 x v / 100`, `direita = (100 + pan) / 2 x v / 100`.
- **Ambiente** (`SFXSetEnvironment`, da sala da câmera: `cel_tag` +0x98, o reverb 0-100; -0x6768 = debaixo d'água):
  o reverb da SPU (modo 4 "studio C", profundidade 0) com o volume de efeito `reverb x 80 / 100`, que anda até o
  alvo em passos de +16/-4 por VBlank; em salas abertas (`cel_tag::type` bit 28 ou reverb >= 20) os sons `Outdoors`
  vão a 50%.
- **Fade e ducker**: `SFXFadeDown/Up` levam `FadeVolume` de 10000 a 0 (ou o contrário) em 2 s pelo relógio do driver
  (`psiSFXGetTimer`: milissegundos, contados de 100 em 100 por uma thread do IOP); o ducker abaixa a música para `100 - Ducker` e a devolve.
- **Streams** (`Streams[3]`: 0 a música, 1 e 2 as falas): cada canal tem as suas vozes reservadas na partida
  (`PS2_CreateStreamVoiceHandles`: a música duas, esquerda e direita sem voz de trás; cada fala uma alça de duas
  vozes, frente e trás). A fala começa por um som com `FileRef` negativo, ocupa o primeiro canal livre (com os dois
  ocupados, o som é recusado) e para no fim dos dados. A música (`SFXStartMusic(faixa, trecho)`) segue os
  marcadores (acima), toca a 32 kHz (pitch 0xaaa) e o seu volume é `MusicMasterVolume x DuckerCurrentVolume x
  BaseVolume / 10000 x FadeMusicVolume / 10000 x ShutDownVolume / 100`, ao quadrado (`ES_MusicSetVolume`, a cada
  update).
- **Pausa** (`SFXPause`, pelo menu de pausa): cada voz vai a pitch 0 (o PS2 congela a voz em vez de desligá-la) e
  volta ao pitch guardado no `SFXUnPause`; os sons começados durante a pausa nascem pausados. `PauseInNis` pausa um
  som durante uma cena; os sons começados durante uma cena (`StartedWhenNisPlaying`) somem em fade de 20 VBlanks
  quando ela acaba (`NisQuitten`).

### A mistura da SPU2 (os dois núcleos)

O que vai do volume de uma voz até a saída, pelo código do `SFX.IRX`, do `LIBSD.IRX` e do EE (o `LIBSD.IRX` não tem
símbolos: `tools/ghidra_irx.sh import LIBSD.IRX`, decompilação em `out/decomp_irx/LIBSD/`):

- **O estado que o `sceSdInit` deixa** (a função em 0x00000ee0 do módulo, chamada por `psiInitialiseSoundSystem`):
  `MMIX` 0xff0 no núcleo 0 e 0xffc no núcleo 1, `AVOL` (a entrada externa, que no núcleo 1 é a saída do núcleo 0)
  0x7fff no núcleo 1 e 0 no 0, `BVOL` (a entrada de dados, os filmes) 0, `MVOL` e `EVOL` 0, e todas as vozes em
  `VMIXL/R` e `VMIXEL/R`. O 0xffc liga no núcleo 1 a entrada externa só no seco (bits 2-3; os bits 0-1, ao reverb,
  ficam desligados), a entrada de dados e as vozes no seco e no reverb.
- **O driver** escreve `MVOL` 0x3fff nos dois núcleos no fim de `psiInitialiseSoundRam` (`PS2_SetMasterVolume(100)`:
  `100 x 0x3fff / 100`; 0 no começo dela), `EVOL` = `reverb x 80 / 100 x 0x3fff / 100` nos dois núcleos
  (`psiSetReverb`, pela sala), `VMIXEL/R` pelo `ReverbSend` de cada som (`PS2_SendKeyONKeyOff`) e os volumes das
  vozes (`PS2_GetSampleVolume`: `0..100 -> 0..0x3fff`); nunca mexe em `AVOL`, `BVOL` nem `MMIX`. O volume pedido pelo
  jogo é ignorado (`VolumeOverRide` = 100) e o `SfxMasterVolume` padrão é 100: um efeito sai com o volume do banco.
- **O EE** só fala com a SPU pelo driver; o `sceSdRemote` do ACTION.ELF é o dos filmes (`audioDecCreate` ->
  `changeMasterVolume(0x3fff)`, o mesmo `MVOL`; `changeInputVolume` põe o `BVOL` do núcleo 0 em `MovieVolume x 100`
  enquanto um filme toca e o zera no fim).
- **Os formatos**: os volumes das vozes e o `MVOL` são do modo fixo de 15 bits (bit 15 = 0, o valor com sinal em 0-14
  vezes 2: 0x3fff = 0x7ffe/0x8000 = 0.99994); `EVOL`, `AVOL` e `BVOL` são de 16 bits com sinal (0x7fff = ~1.0, 0x3fff
  = 0.5): o `sceSdInit` põe 0x7fff no `AVOL`, que no formato de 15 bits seria quase zero e invertido, e na SPU do PS1,
  de que a SPU2 deriva, os volumes de saída do reverb e das entradas (`vLOUT/vROUT`, CD, externo) também são de 16
  bits. O `EVOL` máximo do driver (0x3332) devolve o reverb a 0.4.
- **A soma**: cada núcleo soma as suas 24 vozes e satura a soma em 16 bits (a SPU2 grava essas somas como amostras
  de 16 bits na "área de saída" da RAM de som, 0x1000-0x1fff); o núcleo 0 soma o seco e o reverb x `EVOL`, multiplica
  por `MVOL` e satura (a saída dele também vai para a RAM, em 0x800/0xa00); o núcleo 1 soma o seco das vozes dele, a
  saída do núcleo 0 x `AVOL` e o reverb dele x `EVOL`, multiplica por `MVOL` e satura: é o DAC.

Com tudo isso, o ganho de um efeito da voz até a saída é 1.0: um som 2D no centro sai com 50 em cada lado
(`(100 - pan) / 2`), 0x1fff, metade; um 3D à frente idem, mais a voz de trás. As amostras dos bancos são
normalizadas perto de 0 dBFS (`wavbank`), então um efeito cheio tem pico de cerca de -6 dB e dois a quatro sons altos
ao mesmo tempo (tiros, impactos perto do ouvinte) passam do limite e saturam, primeiro na soma do núcleo 0 (onde
caem a maioria das vozes de efeitos: as 6 das falas e da música são reservadas primeiro, e as de efeitos são as
primeiras livres). Não há atenuação depois disso.

## Vídeos (`MOVIES/30_FPS/*.PSS`)

Os 22 filmes do disco ficam fora do `FILES.BIN`, em `MOVIES/30_FPS/<código>.PSS` (o código é o hash do filme em
hexadecimal maiúsculo; `python tools/movies.py list`). O caminho é o de `psiStartBackgroundMovie`:
`"%s%s%8.8x.pss%s"` com `HostName` = `cdrom0:\movies\`, `DirName` = `30_FPS\` e `EndTag` = `;1`; `strFileOpen` passa
para maiúsculas e lê pelo streaming do CD (`sceCdStInit`/`sceCdStRead`, 80 setores de 0x800). A pasta `30_FPS` é a
da versão NTSC (o `DirName` é o único lugar com a taxa).

### O contêiner

Um PSS é um *program stream* MPEG-2 comum, como o que o `ps2str` da Sony gera:

- pacotes (`0x000001BA`, cabeçalho MPEG-2 de 14 bytes, SCR e 0x8000 bytes/s por 50) de exatamente 0x4000 bytes
  (16 KB, a leitura de `fillBuff`); o primeiro traz o cabeçalho de sistema (`0xBB`); o último PES é de enchimento
  (`0xBE`) e o arquivo termina em `0x000001B9`;
- PES de vídeo no stream `0xE0` (cabeçalho MPEG-2 `0x83 0x80/0xC0`, 10 bytes de dados de cabeçalho: o PTS, às vezes
  o DTS, e enchimento `0xff`), 0xffa bytes cada;
- PES de áudio no *private stream 1* (`0xBD`), com PTS no primeiro de cada pacote; o *payload* começa com 4 bytes
  (`0xff`, o sub-stream `0xa0`, 0, 0) e o resto são 4073 bytes de áudio, que não fecham uma amostra: o áudio é o
  *payload* de todos os PES, sem os 4 bytes, colado na ordem.

Áudio e vídeo vêm bem intercalados: na ordem do arquivo, o áudio fica a no máximo ±0.13 s do vídeo, e o primeiro PTS
dos dois é o mesmo (07100001: 13783 em 90 kHz, 0.153 s) — o quadro `k` em ordem de exibição vale o áudio a partir da
amostra `k x 1600`.

### O vídeo

MPEG-2 Main Profile @ Main Level, **512 x 448** (a tela inteira do PS2, sem borda), **30 quadros por segundo**
(código de taxa 5, 30.000 exatos), aspecto **4:3**, 4:2:0, progressivo, 4 Mbit/s (VBV 160 KB), codificado com o
TMPGEnc ("encoded by TMPGEnc b12a" no *user data*), um cabeçalho de sequência no começo (com a extensão de sequência e
a de exibição: componente, 512 x 448) e o código de fim no final, a matriz não intra própria no cabeçalho de sequência
(a intra é a padrão; nenhuma extensão de matrizes). Os filmes com quadros B (`IBBPBBP...`, todos menos quatro) têm GOPs
de 18 quadros quase sempre (I, 5 P e 12 B; menores nos cortes de cena), quase todos abertos (o primeiro e os de alguns
cortes fechados); os quatro só com I e P (os dois fundos do menu, a abertura da EA 07370048 e o trailer 07390048) têm
GOPs fechados de 10 quadros (menores nos cortes).

Todas as imagens (um levantamento das extensões de imagem dos 22 filmes): imagens de quadro (`picture_structure` 3),
`progressive_frame` 1, `frame_pred_frame_dct` 1 (só previsão de quadro e DCT de quadro), `q_scale_type` 1 (a escala não
linear), `intra_vlc_format` 1 (os intra pela tabela B.15), zigue-zague, `intra_dc_precision` 0 (DC de 8 bits), sem
vetores de ocultação, `top_field_first` 1, `f_code` de 1 a 4. É o que o decodificador do port tem de fazer
(`platform/mpeg2_decoder.h`; `docs/port.md`, "Filmes").

### O áudio

O formato "SS" da Sony, o que a SPU2 lê na entrada de dados (não é o ADPCM da SPU):

```
"SShd" uint32 tamanho (0x18)  int32 tipo (1 = PCM de 16 bits little-endian; 0x10 seria PS-ADPCM)
       int32 taxa (48000) int32 canais (2) int32 intercalação (0x200) int32 laço_início (-1) int32 laço_fim (-1)
"SSbd" uint32 tamanho do corpo
corpo: blocos de 0x200 bytes alternados, esquerdo e direito (256 amostras de cada canal por par de blocos)
```

Os 40 bytes do cabeçalho são os que `audioDecBeginPut` junta antes de aceitar dados (`0x28`). Os fundos do menu
(07330048, 07350048) não têm áudio. Os níveis no disco: picos de cerca de -8 dBFS (07380048), fala em -22 a -30 dB
RMS.

### Os filmes e onde o jogo os toca

`UsingMovie` (0x00107eb8) é a lista dos 23 códigos que existem (07360048 não está no disco: `strFileOpen` trava o PS2
num laço se ele não abrir); os nomes são os da página de depuração `P_FMVTEST_Handler`. Duração em quadros a 30 fps.

| Código | Nome (P_FMVTEST) | Quadros | Som | Onde |
|--------|------------------|---------|-----|------|
| 07100001 | Estate - Meet Mayhew | 1280 (42.7 s) | sim | abertura da missão 07000001 |
| 07100005 | Castle - Halo Jump | 2776 (92.5 s) | sim | abertura da missão 07000005 |
| 07100009 | Tower Infil - Intro | 2564 (85.5 s) | sim | abertura da missão 07000009 |
| 0710000C | Nuclear Plant - Interview | 2387 (79.6 s) | sim | abertura da missão 0700000c |
| 07100011 | Tower Escape - Dom Death | 2643 (88.1 s) | sim | abertura da missão 07000011 |
| 07100014 | Evil Base - Descent All | 1283 (42.8 s) | sim | abertura da missão 07000014 |
| 07100018 | Space Station - Arrival | 907 (30.2 s) | sim | abertura da missão 0700001b (a página de depuração pede 07100017, que não existe) |
| 07300004 | Estate - Mayhew Death | 1490 (49.7 s) | sim | fim da missão 07000001 |
| 07300008 | Castle - Gondola | 950 (31.7 s) | sim | fim da missão 07000005 |
| 0730000B | Tower Infil - Outro | 888 (29.6 s) | sim | fim da missão 07000009 |
| 0730000D | Nuclear Plant - Entering Vent | 1700 (56.7 s) | sim | fim da missão 0700000c |
| 07300013 | Tower Escape - Lobby Rescue | 1275 (42.5 s) | sim | fim da missão 07000011 |
| 07300017 | Evil Base - Kiko Death | 926 (30.9 s) | sim | fim da missão 07000014 |
| 0730001B | Arrival - Happy Ending | 2115 (70.5 s) | sim | fim da missão 0700001b |
| 07330048 | Menu - BG Loop | 1800 (60 s) | não | fundo do menu principal (página 0x40000002), em laço |
| 07350048 | Menu - FE Loop | 1800 (60 s) | não | fundo das outras 40 páginas do `08000002` que têm filme, em laço |
| 07370048 | Menu - EA Logo | 96 (3.2 s) | sim | `P_INTRO_Handler`, primeiro; não pula |
| 07380048 | Menu - MGM Logo | 329 (11.0 s) | sim | `P_INTRO_Handler`, segundo; não pula |
| 07390048 | Menu - DAD Trailer | 2304 (76.8 s) | sim | `P_TRAILER_Handler` (o trailer de *Die Another Day*, da AV Options) |
| 073A0048 | Menu - Attract Movie | 2023 (67.4 s) | sim | `P_ATTRACT_Handler` (45 s sem entrada no "Press START" ou no menu principal) |
| 073B0048 | Menu - Esthero | 1756 (58.5 s) | sim | `P_ESTHERO_Handler` (depois do "Press START", pula depois de 180 ticks) e a demonstração do menu principal, uma vez sim, outra não |
| 073F0048 | Menu - Making Of | 4230 (141 s) | sim | `P_WINGAME_Handler`, depois da última missão (e então os créditos) |

As aberturas e os fins das missões vêm dos códigos de carga: `ResetMap_LevelToLoad(fase, 0, 0)` (o mapa das missões, a
troca de fase sem `no_movie`) soma 0x100000 às fases 07000001-07000016 e troca 0700001b por 07100018 e 0700004a por
0710004a; `Mission_Update` termina a missão em `LevelToEndTo` (07300004, 07300008...). `ResetMap_Load`, com os bits
16-23 do código ligados, cria o script das legendas do filme (só com as legendas ligadas, `DrawInfo[2]`: 0600095a para
07100001 etc., um `SP_Create`; o texto do filme é o 2D do jogo por cima), empilha `GF_MOVIE`, chama
`psiStartBackgroundMovie` e, quando o filme acaba (ou o jogador aperta Start, a ação 0x19), carrega a fase de
`GetLevelToLoadFromMovieCode` (0700xxxx, ou o menu principal com os resultados para os 073xxxxx). Os códigos sem
arquivo (07100002, os 072xxxxx das trocas de fase no meio da missão) não passam de `UsingMovie` e o `GF_MOVIE` sai no
tick seguinte. As páginas do menu principal guardam um filme de fundo (o terceiro campo do registro de página, ver
"Menus"): `MenuManager_Update` chama `Menu_PlayMovie(filme, 0, 1, 0, 0, 0)` (em laço, sem trocar a música) quando a
página nova tem outro filme e `psiStopBackgroundMovie` quando não tem.

### O código do EE que os toca

O ACTION.ELF usa a biblioteca `libmpeg` da Sony (o IPU decodifica; `sceMpegCreate`, `sceMpegDemuxPss`,
`sceMpegGetPicture`) e o exemplo "playpss" da Sony quase sem mudança (`StartUpBGFMV`, `videoCallback`,
`audioCallback`, `audioDec*`, `strFile*`):

- `psiStartBackgroundMovie(filme, laço, volume)` (0x001081a8): só filmes de `UsingMovie`; espera os sons do driver
  (`PS2_SFXWaitTillFinished`), para o filme anterior, guarda `LoopingMovie`, `MoviePlaying`, `MovieVolume`, aloca
  ~2.3 MB do heap (`AllocateMovieMemory`: o buffer do MPEG 0xfd768, a imagem RGBA 512x448x4 = 0xe0000, as tags de
  carga da imagem, o buffer do demultiplexador 0x30000, o de áudio 0xc000 e 0x6000 + 0x800 bytes do heap do IOP),
  abre o arquivo e, se abriu, `BGFMVPlaying = 1` e `StartUpBGFMV` (cria o `sceMpeg` com os callbacks: vídeo para o
  anel do demultiplexador, áudio para o `AudioDec`, "sem dados" alimentando o DMA do IPU, "fundo" que lê o CD enquanto
  espera; enche o buffer).
- O `main` (0x00109080), com um filme tocando, roda a lógica só nos VBlanks pares (`(VBlankCount & ~1) - GameCount`,
  então 30 ticks por segundo: `GS_SetRefreshRate(30, 60)`) e, antes de cada `GameFlow_Main`,
  `psiDecodeBackgroundMovieFrame` decodifica **um** quadro (`DecodeBGFMVFrame`: `sceMpegGetPicture` para RGBA32,
  as tags de carga centradas em 512 x 448 nos dois buffers de quadro, `audioDecSendToIOP`, e o começo do som quando o
  IOP tem 0x6000 bytes); enquanto espera o VBlank, `psiSwapBGFMVFrame` continua lendo o arquivo. No fim do vídeo
  (`sceMpegIsEnd`) o filme para (`BGFMVPlaying = 0`) ou, em laço, o arquivo é reaberto. O vídeo anda por VBlanks, o som
  pelo relógio da SPU2: o PS2 não os sincroniza (o vídeo começa logo, o som depois de 0x6000 bytes; a 59.94 Hz o
  vídeo fica 0.1% mais lento que os 30 fps do arquivo).
- A imagem: `VIFDMA`, no começo do DMA de cada quadro, chama `PS2UploadBGMImage`, que copia a imagem (o `ImageToUpload`
  do último quadro decodificado) para o buffer de quadro antes do resto: o filme é o fundo do quadro e o que o jogo
  desenha (as legendas, os menus) fica por cima. `psiDrawFogPass` não desenha a névoa com um filme tocando.
- O som: `audioDecCreate` põe o `MVOL` dos dois núcleos em 0x3fff (`changeMasterVolume`, o mesmo valor do driver de
  som); `audioDecStart` põe o `BVOL` do núcleo 0 (a entrada de dados) em `MovieVolume x 100` (`changeInputVolume`;
  `MovieVolume` é o `SFXGetVolume()` que os chamadores passam: 100 por padrão, 10000 = 0x2710 = 0.305 no formato de
  16 bits com sinal, -10.3 dB) e começa a transferência em bloco em laço (`sceSdRemote(1, rSdBlockTrans, 0, 0x13,
  ...)`: o canal 0, o PCM entra no núcleo 0 a 48 kHz, no seco e no reverb pelo `MMIX` 0xff0); `audioDecReset` zera o
  `BVOL` e para a transferência. O volume dos efeitos mudado durante um filme só vale no próximo.
- Pular: no `GF_MOVIE` (as missões), a ação 0x19 (Start) com `Input_Action(-1, 0x19, 1)`; nas páginas do menu,
  `MenuManager_Update` chama `Menu_StopMovie(0)` com a ação 0x19 (`Menu_InputAction(-1, 0x19, 4)`), que só para os
  filmes com `movie_skip` depois de `movie_counter` ticks (`Menu_UpdateMovieCounter`); as aberturas de `P_INTRO` não
  pulam (`movie_skip = 0`), o clipe de `P_ESTHERO` pula depois de 180 ticks (6 s a 30 ticks por segundo).

No PC (`platform/psi_movie.h`, `docs/port.md` "Filmes") as mesmas funções do jogo rodam sobre um demultiplexador
próprio (`platform/pss.h`), o decodificador de MPEG-2 do Windows (Media Foundation) e a entrada de dados da SPU2 em
software; um dump do som da abertura de 07000001 é o áudio do arquivo vezes 0.3051 (o `BVOL` 0x2710), sem atraso.

`python tools/movies.py info <código>` mostra os pacotes, os streams, o vídeo (quadros I/P/B, GOPs) e o cabeçalho do
áudio; `demux <código> <pasta>` grava o vídeo elementar (`.m2v`) e o áudio (`.wav`, desintercalado) em `out/`.

## Outros formatos (não analisados ainda)

| Arquivo | Formato conhecido |
|---------|-------------------|
| `DRIVING/*.VIV`, `*.MUS`, `*.SPE` | BIGF da EA com RefPack: objetos ELF do EAGL, CARP, SHPS, fluxos SCHl (EA-XA), bancos BNKl, galerias, filmes MPC; ver `docs/driving.md`, "Os dados" (`tools/driving_data.py`) |

## Seções carregáveis (cenas NIS)

Os pacotes `078`, `079` e `07A` **não têm geometria**. Cada um contém só animações
(tipo 4) e exatamente um script (tipo 7): é uma cena em tempo real, chamada de NIS
(Non-Interactive Sequence) no código.

O pacote principal da fase declara cada seção num item tipo 12 de 32 bytes:

```c
struct LoadableDescriptor {   // item tipo 12, registrado em LoadableFiles[] (até 16)
    uint32_t script_hash;     // hash do script dentro do pacote da seção
    uint8_t  section;         // 1, 2 ou 3; o pacote é o hash da fase com bits 20-23 = section + 7
    uint8_t  zero[27];
};
```

Fluxo no jogo:

1. Um objeto "script player" posicionado na fase chega a `SP_LoadScript` (`0x001dbbf8`).
2. `isLoadable(script_hash)` procura o hash em `LoadableFiles[]` e devolve a seção.
3. Se for carregável: pausa vibração e efeitos sonoros, chama
   `LoadableLoad(hash, section)` (`0x001d69e8`), que descarta as animações da seção
   anterior (hashtype `0x04000000`) e carrega o pacote novo com `LoaderLoad(2, ...)`.
4. `Script_Load` e `Script_Play` tocam a cena (formato em "Scripts de cena", abaixo); ao soltar o
   script, `Script_Free` chama `LoadableReload`, que devolve as animações da fase.

`C_NIS_Handler` (`0x00205f10`) é um menu de depuração que ficou no executável: lista as
cenas agrupadas por fase, permite tocá-las e liga um contador de quadros. Os nomes
estão em `tools/data/nis_menu.json` (45 cenas em 22 grupos).

Exportar as cenas depende de animação esquelética e dos modelos de personagem com
skinning, ainda não suportados.

## Scripts de cena (itens tipo 7)

Lidos por `Script_Load` (`0x001d8248`) e tocados por `Script_Run` (`0x001d8548`); portados em
`src/engine/script.cpp`. Ferramenta: `tools/scripts.py list|dump|stats` (com `--keys` mostra os
quadros-chave). Os 468 itens do disco têm a versão 0x1c, até 53 streams e até 25360 chaves.

```
0x00  u16 versão        0x1c (outra: o hash fica marcado com -1 e nunca toca)
0x02  u16 câmera        diferente de 0: a cena controla a câmera (SCRIPTINFO::flags 0x40)
0x04  u16 n_streams     até 0x3e
0x06  u16 loop_start    quadro para onde um script em laço volta
0x08  u16 end           último quadro (o fim de todos os streams no disco)
0x0a  n_streams x {
          u16 first_key, last_key    quadros-chave do stream
          u32 size                   inclui o próprio u32
          u8  comandos[size - 4]
      }
      u32 n_keys
      u32 pad                        bytes até alinhar as chaves a 16 (lidas com lq na VU0)
      KEYED_POSROT keys[n_keys]
```

```c
struct KEYED_POSROT {     // 0x30 bytes
    float pos[4];         // w = 1
    float quat[4];        // x, y, z, w
    float scale;          // 0x20 entidade: escala; câmera: campo de visão vertical / proporção (0.02 a 1.3)
    float alpha;          // 0x24 entidade: alfa 0..1; luz: bytes b, g, r em 0x24..0x26
    float unused;         // 0x28 1.0 em todo o disco
    float frame;          // 0x2c quadro (crescente dentro do stream)
};
```

Cada comando é um byte de código, os argumentos e, nos que têm tratador, um byte de fecho (o próprio
código; 5 e 6 trocam: o 5 fecha com 6 e o 6 com 5). Comandos sem tratador são só o código.

| Código | Argumentos | Efeito |
|--------|------------|--------|
| 4  | u16 quadro | fim do stream (antes dele o comando se repete; no fim o script para ou volta ao laço) |
| 5  | u16 quadro | espera até o quadro |
| 6  | u16 quadro, u8 | marca: passou do fim do script, ele termina |
| 7  | u32 hash, u8 tem_pai, u8 pai, u8 spline, u8 tipo, u8 n, n x u32 | entidade (o tipo escolhe o criador; os parâmetros vão para o posicionamento falso `tptr`) |
| 9  | | fim da entidade |
| 10 | u32 script, u32 pele, u32 quadro | objeto animado tocando um script de animação 06 |
| 12 | | fim da animação |
| 13 | u8 spline, u16 primeira, u16 última + 1 | câmera pelos quadros-chave indicados |
| 15 | | fim da câmera |
| 18 | u8 n, u8 evento, n x u32 | evento (abaixo) |
| 19 | f32 | fade da câmera 4 |
| 20 | 5 x u16 (x0, y0, x1, y1 em 640 x 480, flags), u32 hash | sprite |
| 21 | | fim do sprite |
| 22 | u32 som, u8 2d, u8 tem_objeto, u8 stream | som (preso ao objeto do stream indicado ou ao do anterior) |
| 23 | | fim do som (só para sons em laço) |
| 24 | u8 tem_pai, u8 pai, u8 tipo | luz (raio pela escala, cor pelos bytes da chave) |
| 26 | | fim da luz |
| 27 | u32 hash, u8 tem_pai, u8 pai | sub-script em laço |
| 29 | | fim do sub-script |
| 30 | u32 rótulo, u16 duração | legenda |

Tipos de entidade com criador próprio (`Script_CreateEntity`): 0x20 quebrável, 0x22 escada, 0x29
interruptor, 0xe1 destrutível, 0xea/0xeb gatilhos de toque, 0xf0 item, 0xf2 emissor, 0xf5 volume de IA;
os outros (0x31 na grande maioria dos 4985 do disco) são objetos simples com o modelo do hash.

Eventos (`Script_EventHandler`; "anterior" é o objeto do stream anterior): 3 pausa por a0 - 1 ticks (0:
até `SP_UnPause`), 4 desliga o jogador, 5 desliga a IA, 6 devolve o jogador no fim (na matriz do
anterior), 7 religa a IA no fim, 8 cria um personagem (`Drone_CoderCreate`), 9 modo de câmera do
jogador, 10 quebra o anterior, 11 arma do anterior, 12 os objetos ficam no fim, 13 liga o canal a0 com o
valor a1, 14 o stream some no fim do laço, 15 callback do dono (a0), 16 sem câmera e sem sons, 17 canal
condicional (a0 ligado: a1, senão a2, recebe a3), 18 troca para a fase a0, 19 `ScriptCam`. Contagem no
disco: 3 = 303, 13 = 195, 15 = 119, 12 = 65, 4 a 7 cerca de 30 cada.

## Menus (item tipo 8)

Os 34 itens tipo 8 do disco são dois arquivos: `08000001` (27 424 bytes, igual em todas as fases: a
pausa e os avisos do menu 0x80000002, o fim de missão do menu 0x80000004 e uma página do menu
0x80000003) e `08000002` (133 536 bytes, só no menu principal 07000048: 56 páginas do menu 0x80000002,
52 delas do PS2). `MenuManager_Load` guarda o endereço; `MenuManager_Create(menu, página, ...)` (portado
em `src/engine/menu_manager.cpp`) lê o arquivo do começo a cada menu criado. Tudo little-endian, sem
alinhamento: um cabeçalho `0xfffffffa` e registros que começam por uma palavra de tipo.

```
0xfffffff0 página      uint32 id (0x400000xx), uint32 menu (0x8000000x), uint32 filme de fundo,
                       uint8 plataforma, uint32 tamanho (bytes dos registros seguintes da página,
                       pulados quando o menu não é o pedido ou a plataforma não é 0 ou 3)
0xfffffff9 controle    uint32 id (0x100xxxxx; uma janela usa um id 0x400000xx), uint8 tipo, uint8
                       plataforma, int16 x, y, w, h (em 640 x 480), uint16 profundidade, uint32 tag
                       (o +0x20 do controle), uint16 conjunto de componentes (16 bits altos do
                       componente do controle), uint32 A, uint32 B (parâmetros do tipo: rótulo A
                       formato e B texto; botão A estilo; caixa A marcada e B texto; barra A mínimo
                       e B máximo; lista A modo do destaque)
0xfffffff6 mensagem    uint8 mensagem, uint32 a, uint32 b: mandada ao último controle (0x18 texto com
                       b = rótulo do *Txt.dat, 0x10 linha, 0x1a-0x1c cores, 0x2b estado, 0x2e...);
                       a cor 0x7d6d59ff nas mensagens 0x1a-0x1c é trocada por 0x645a49ff; 0x1d é
                       ignorada
0xfffffff8 formato     32 bytes: o formato de texto do último controle (mensagem 0x23); na primeira
                       leitura está em 16 bits por caractere e o jogo o converte para 8 bits ali mesmo
0xfffffff7 script      uint32 id, uint32 flags (1 spline): script novo do último controle
0xfffffff5 quadro      uint16 quadro, int16 x, y, w, h, uint32 flags (1 spline, 2 começo do laço, 4 fim
                       do laço, 0x20 fixo): quadro-chave do último script
0xfffffff4 msg quadro  uint32 alvo (id; 0xfffffffd o gerente, 0xfffffffe o dono do script), uint8
                       mensagem, uint32 a, uint32 b: mensagem do último quadro-chave (0xe1 e acima
                       criam um processo de cor)
0xfffffff3 msg gerente uint8 mensagem, uint32 a, uint32 b: mandada ao gerente
0xfffffffd conjunto    uint16 número: conjunto de componentes novo (até 14 componentes)
0xfffffffc componente  uint16 peças, uint32 textura (03xxxxxx), int16 margens esquerda, topo, direita,
                       baixo (a área do conteúdo dentro da moldura)
0xfffffffb peça        int16 x, y, w, h, uint16 u, v, largura e altura na textura, uint32 cor (RGBA,
                       0x80 = 1), uint32 flags, uint32 estica w, uint32 estica h (0 ou 1)
0xfffffffe             uint32 (ignorado)
0xfffffff2 fim
```

Plataforma: 0 em todas, 3 só no PS2, 1 e 2 dos outros consoles (o mesmo rótulo "Vibration" do PS2
aparece como "Rumble Feature" na plataforma 2). Tipos de controle: 1 botão, 2 caixa de marcar, 5
rótulo, 6 lista, 9 rádio, 10 barra de rolagem, 13 janela, 200 memo (o código ainda tem 3 combo, 11 spin
e 12 caixa de texto, que o disco não usa). `08000001`: 9 páginas e 183 controles (92 rótulos, 34
barras, 19 botões, 18 caixas, 10 listas, 9 janelas, 1 memo), 11 conjuntos com 154 componentes e 121
peças, 11 scripts. `08000002`: 1003 controles (757 rótulos, 86 botões, 56 janelas, 48 rádios, 34
memos, 17 barras, 5 listas), 13 conjuntos, 182 componentes, 183 peças, 44 scripts.

Flags de uma peça: os bits 0x10-0x80 são os estados do controle em que ela aparece (0x10 normal,
0x20 com o foco, 0x40 apertado ou marcado, 0x80 o botão com o foco sob o cursor); 1 presa embaixo, 2 presa à direita, 4/8
centrada em x/y, 0x100/0x200 copia a altura/largura do controle, 0x400/0x800 não ocupa espaço,
0x1000/0x2000 desenho aditivo/subtrativo. Uma peça que estica (`estica w/h` = 1) ocupa a fração
correspondente do espaço que sobra entre as fixas, e as fixas presas à direita ou embaixo andam com o
tamanho do controle (`Component_SetupInstance`).

Os ids ligam o arquivo ao código: cada página e controle com comportamento tem um tratador no
executável (`Handler_HandleMessage`, 103 ids: `P_PAUSE_Handler` para 0x4000004b, `C_GCPAUSE_Handler`
para 0x10000028, `P_ENDMISSION_Handler` para 0x40000042...). Os textos são rótulos do `*Txt.dat`
(mensagem 0x18 com o rótulo em b), e o rótulo 2 ("1") é um marcador que o tratador troca.

## Catálogo das fases

`tools/levels.py` cruza três tabelas do `ACTION.ELF` com os pacotes do disco:

- `MissionData` (24 entradas de 40 bytes): `level_hash, mission_hash, part, level_info_ptr, ...`.
  O nome interno da fase é o símbolo apontado por `level_info_ptr`.
- `mp_level` (8 entradas de 24 bytes): o hash do mapa está no quarto `uint32`.
- Os descritores tipo 12 e o menu NIS, para os nomes das cenas.

| Pacote | Nome interno | Missão, parte | Cenas nas seções |
|--------|--------------|---------------|------------------|
| 07000001 | Henderson_a | 1, 0 | Meet Mayhew |
| 07000002 | Henderson_b | 1, 1 | Bunker Objectives, Bath House |
| 07000003 | Henderson_c | 1, 2 | |
| 07000004 | Henderson_d | 1, 3 | |
| 07000005 | Castle_Exterior | 5, 0 | |
| 07000006 | Castle_CourtYards | 5, 1 | |
| 07000007 | Castle_Indoors1 | 5, 2 | M1_library, Engagement |
| 07000008 | Castle_Indoors2 | 5, 3 | Castle Meeting, Castle Rook |
| 07000009 | Tower_A | 9, 0 | Infiltrate Intro |
| 0700000a | Tower_B | 9, 1 | |
| 0700000b | Tower_C | 9, 2 | Infiltrate Buzz, Infiltrate Stair, Infiltrate Outro |
| 0700000c | PowerStation_A1 | c, 0 | |
| 0700000d | PowerStation_A2 | c, 1 | Rook Retreat, Enter Vent |
| 07000011 | Tower_2A | 11, 0 | Dominique Death |
| 07000012 | Tower_2B | 11, 1 | |
| 0700004a | Tower_2Elevator | 11, 2 | Shaft Attack, Elevator Plummet |
| 07000013 | Tower_2C | 11, 3 | Lobby Rescue, Elevator Exit |
| 07000014 | Evil_Base | 14, 0 | Kiko Arrival, Kiko Command Centre, Rook Reveal |
| 07000015 | Evil_Silo | 14, 1 | |
| 07000016 | EvilBase_ShuttleBay | 14, 2 | Bond Dropped, Kiko Flees, Kiko Death |
| 0700001b | Space_Station_D | 1b, 0 | Laser Taunt, Drake Explosion |
| 07000023 a 07000029, 0700004b | multiplayer 1 a 8 | | |

Conteúdo cortado: `MissionData` ainda lista `PowerStation_B1` (`0700000e`),
`PowerStation_B2` (`0700000f`) e `Caves` (`07000010`), que não existem no disco.
Os pacotes `07000041`, `07000043`, `07000046`, `07000048`, `07000049`, `07000500` e
`07000999` não aparecem em nenhuma tabela; parecem ser menus e cenários de apoio.

## Animação esquelética

Decodificada por `tools/anim.py`, a partir de `AnimSkeletonProcess` (`0x001bd328`),
`AnimProcessSeqData` (`0x001c4ce0`) e `AnimFrameCopy` (`0x001c5668`).

Item `SKEL` (tipo 6):

```
+0x00  u16 skeleton_id
+0x02  u8  bone_count
+0x04  u32 translation_mask[3]     bit i: osso i tem translação animada
+0x10  bone_count x f32[3]         translação local padrão, relativa ao pai
```

As translações padrão coincidem com as da pose de repouso dos skins (escala global 1).

Sequência (item tipo 4, hash `04xxxxxx`):

```
+0x00  u32 hash
+0x10  f32[10]                     desconhecido
+0x38  i16 frame_count
+0x3A  i16 channel_count           3 por osso + 3 por osso com translação animada
+0x40  registros de canal, osso por osso: [tx, ty, tz,] qx, qy, qz
```

Registro de canal: `u32` com o tamanho do registro nos 16 bits baixos e o primeiro
descritor de segmento nos 16 altos; depois, alternando, os coeficientes do segmento e
palavras com dois descritores (16 bits baixos primeiro). Descritor: bits 0-5 = máscara
dos coeficientes `c0..c5` presentes, bits 6-15 = duração em quadros.

O valor no quadro `f` (base 0) usa o primeiro segmento cuja duração acumulada é `>= f`
e vale `c0 + c1·f + … + c5·f^5`, com `f` absoluto (a VU0 faz o produto escalar com
`1, f, f², f³, f⁴, f⁵`). Do quatérnio só vêm x, y, z: `w = sqrt(1 - x² - y² - z²)`;
se o valor de z é `>= 2`, o z real é `valor - 4` e `w` é negativo. Em cerca de 0,02% das
amostras (ossos de dedos) o ajuste passa de 1 e o motor zera `w`; o exportador normaliza.

A sequência não indica o esqueleto. A compatibilidade vem do número de canais: 445 das
468 sequências da primeira fase batem com algum esqueleto. As outras 23 têm 22 canais e
o bit 3 das flags: são as sequências de rosto, no modo de canal único de `AnimFrameCopy`
(cada canal é o peso de um alvo de morph; ver "Morphs do rosto").

O quatérnio usa a mesma convenção do glTF (o `Quat_QuatToMat` do motor, em vetor-linha,
equivale ao glTF em vetor-coluna). As poses exportadas são anatomicamente coerentes.

**Taxa de quadros**: não confirmada. O exportador assume 30 quadros por segundo.

`tools/anim.py <fase>` gera em `extracted/models/anims/` um `.glb` por layout de esqueleto
usado na fase, com um modelo representativo e todas as sequências compatíveis do pacote
de animações (`07F`) e das seções de cena (`078`, `079`, `07A`). O nome de cada animação
começa pelo pacote de origem. A primeira fase rende 344 animações em 41 MB.
