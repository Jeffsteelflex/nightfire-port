# RTX Remix: estudo e protótipo (`--renderer d3d9ff`)

Estudo de outubro de 2026 (fontes lidas em 10/10/2026) sobre um modo visual "next-gen" opcional para o port pelo
[NVIDIA RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix): path tracing, materiais PBR substituíveis, DLSS e Ray
Reconstruction. O visual original continua o padrão; o renderizador Direct3D 11 continua a referência e precisa seguir
a 60 fps em GPUs integradas, sem DLSS. O Remix muda rápido: os fatos abaixo têm data e link, e cada um deve ser
conferido de novo antes de qualquer trabalho grande.

## Veredito

**Viável, como caminho opcional por Direct3D 9 de função fixa, e o protótipo prova as partes de risco sem o Remix
instalado.** O Remix não vê o Direct3D 11 do port (só intercepta Direct3D 9), então ele precisa de um segundo caminho
de desenho. Esse caminho é barato para o port porque o port já tem tudo o que o Remix pede e que um jogo antigo
costuma esconder: cada malha é decodificada uma vez na carga (buffers estáticos, hashes estáveis), a câmera é uma
matriz conhecida, o skinning é por paleta (cabe no vertex blending indexado da função fixa), o 2D é um registro à parte
(dá para mandá-lo depois do 3D, com projeção ortográfica) e o programa já é de 32 bits, como os jogos que a ponte do
Remix atende. O protótipo (`src/render/remix_d3d9.*`) desenha as fases com geometria, câmera, texturas, skinning, mistura
e 2D iguais aos do Direct3D 11 (diferenças só de iluminação dinâmica e de névoa, as aproximações da função fixa).
**Não foi testado com o Remix** (o runtime não foi baixado): os passos para o usuário estão em "Como testar com o
Remix". Os riscos que só o Remix responde: o custo da ponte de 32 bits com o número de chamadas do port, a visibilidade
por portais (o path tracing quer o que está fora da tela) e o ajuste das luzes.

## Situação (10/10/2026): parado

**Primeiro teste do usuário, 10/10/2026: a camada RTX não funcionou** ("the RTX layer did not work"), sem logs. O
usuário decidiu parar o RTX por enquanto; o protótipo fica como está. Ao retomar, pedir antes de mexer em qualquer coisa:

- a linha de comando usada e a **pasta de trabalho** de onde o jogo foi aberto;
- os logs do Remix, que ele grava **na pasta de trabalho** (não ao lado do executável): `rtx-remix\logs\remix-dxvk.log`
  (o runtime), `rtx-remix\logs\bridge32.log` (o cliente da ponte, no processo do jogo) e `rtx-remix\logs\bridge64.log`
  (o servidor, `NvRemixBridge.exe`); se houver, os `*.dmp` de falha;
- os arquivos de configuração em uso: `rtx.conf`, `dxvk.conf` e `user.conf` (o runtime os procura na pasta de trabalho)
  e `.trex\bridge.conf` (ao lado do executável);
- o relatório de uma captura com `--renderer d3d9ff` (a linha `d3d9ff ... modulo` diz qual `d3d9.dll` foi carregado) e
  uma foto da tela do que apareceu.

O que se viu nesta sessão, sem o usuário (uma captura fora da tela com o `nightfire.exe` de `build\Release`, onde estão o
`d3d9.dll` da ponte e o `.trex` do Remix 1.5.2): o cliente da ponte carregou, o servidor subiu o runtime
`remix-1.5.2+68edea01` na RTX 5090 e criou o dispositivo sem erros; os logs, um `metrics.txt` e um
`nightfire.dxvk-cache` apareceram na pasta de trabalho da captura, e o `remix-dxvk.log` diz "No config file found at:
rtx.conf" (então um `rtx.conf` ao lado do executável, como no passo 4 da seção 9, não é lido se o jogo for aberto de
outra pasta: a conferir). A imagem capturada só tem o 2D: o Remix compõe a imagem com path tracing no `Present`, e a
leitura do back buffer não a vê; captura não serve para julgar o Remix, só a janela. Não havia `rtx-remix\logs` em
`build\Release` nem na raiz do repositório depois do teste do usuário: ou o jogo foi aberto de outra pasta, ou a ponte
nem chegou a carregar (por exemplo, sem `--renderer d3d9ff`: o caminho padrão, Direct3D 11, nunca abre o `d3d9.dll`).

## 1. O que o Remix intercepta

- **Só Direct3D 9.** "RTX Remix functions as a replacement for DirectX 9" e foi feito para jogos de DirectX 8 e 9 com
  pipeline de função fixa; DirectX 8 e OpenGL só por tradutores para D3D9 (D3D8to9); DirectX 10/11 nem aparece na
  página de compatibilidade (o resto "provavelmente não funciona").
  [Game Compatibility](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/introduction/intro-compatibility.html)
  (atualizada em 08/10/2026), [README do rtx-remix](https://github.com/NVIDIAGameWorks/rtx-remix).
- **Função fixa.** O Remix remonta a cena a partir das chamadas de função fixa (`SetTransform`, texturas por estágio,
  FVF). Shaders: suporte experimental a vertex shaders simples por captura de vértices (`rtx.useVertexCapture`: injeta
  código no vertex shader para pegar as posições finais), e pixel shaders não ("Remix needs raw surface color without
  lighting"). Num jogo misto, só a parte de função fixa ganha o path tracing.
  [FAQ](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/remix-faq.html),
  [RtxOptions.md](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/RtxOptions.md). No código
  (`src/d3d9/d3d9_rtx.cpp` do [dxvk-remix](https://github.com/NVIDIAGameWorks/dxvk-remix)), um desenho com vertex
  shader sem a captura ligada é ignorado ("Skipping draw call with shader usage as vertex capture is not enabled").
- **Direct3D 11: não.** O port desenha em Direct3D 11 com shaders próprios (as rotinas da VU1 em HLSL); nada disso
  chega ao Remix. Daí o caminho `d3d9ff`.
- **A API do Remix** ([RemixSDK.md](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/documentation/RemixSDK.md)):
  uma API em C (`remix_c.h`) para registrar malhas, materiais e luzes e desenhar instâncias direto no renderizador do
  Remix, sem Direct3D 9. É de 64 bits ("Remix API is 64-bit only"); por dentro da ponte, um jogo de 32 bits tem um
  subconjunto (`bridge_remix_api.h`: `CreateMaterial/Mesh/Light`, `DrawInstance`, `DrawLightInstance`,
  `SetConfigVariable`, `dxvk_RegisterD3D9Device`; **sem** `SetupCamera` e `Present`, que continuam do Direct3D 9). É o
  caminho para as luzes e os materiais do port no futuro (ver "Riscos e esforço").
- **Versões.** Runtime estável `remix-1.5.2` (16/06/2026,
  [releases](https://github.com/NVIDIAGameWorks/rtx-remix/releases)); o `dxvk-remix` recebeu commits até 09/10/2026; o
  repositório `bridge-remix` foi arquivado em maio de 2025 e a ponte mudou para a pasta `bridge/` do dxvk-remix.

## 2. Jogos de 32 bits: a ponte

O renderizador do Remix só roda em 64 bits. Para um jogo x86, a **ponte** ([bridge-remix](https://github.com/NVIDIAGameWorks/bridge-remix),
hoje `dxvk-remix/bridge`) faz o meio de campo:

- o `d3d9.dll` de 32 bits na pasta do jogo (o cliente) intercepta todas as chamadas de Direct3D 9 e as passa por filas
  em memória compartilhada (`IpcChannel`) ao `.trex/NvRemixBridge.exe` de 64 bits (o servidor), que as executa no
  `.trex/d3d9.dll` (o dxvk-remix) e apresenta direto na janela do jogo;
- o cliente instala Detours em `Direct3DCreate9`/`Direct3DCreate9Ex` ao ser carregado (`d3d9_bootstrap.cpp`);
- se o dispositivo não é criado com `D3DCREATE_FPU_PRESERVE`, o cliente põe a FPU em arredondamento para o mais perto
  (`setupFPU`), como o Direct3D 9 faz.

**Serve para o port** (decidido x86: as estruturas do PS2 têm ponteiros de 4 bytes). O protótipo cria o dispositivo com
`D3DCREATE_FPU_PRESERVE` (a lógica do jogo conta com a FPU como o port a deixa) e só carrega o `d3d9.dll` com
`--renderer d3d9ff` (`LoadLibrary`, nunca na tabela de importação): no modo Direct3D 11, mesmo com o Remix na pasta, os
Detours da ponte nunca entram no processo.

## 3. O que o Remix precisa identificar

- **A câmera.** Sai das matrizes `D3DTS_VIEW` e `D3DTS_PROJECTION` dos desenhos (`rtx_camera_manager.cpp`): a projeção
  é decomposta (campo de visão, aspecto, perto/longe, mão esquerda ou direita); projeções com cisalhamento acima de
  0.01 ou campo de visão nulo são rejeitadas; a primeira câmera válida do quadro é a principal, a do céu (desenhos
  marcados como céu) é outra, e com `rtx.viewModel.enable` um desenho com `MaxZ` do viewport até
  `rtx.viewModel.maxZThreshold` (ou com outro campo de visão) é "view model" (a arma em primeira pessoa). Projeção
  identidade = sem câmera. O mundo é de mão direita com y para cima por padrão (`rtx.leftHandedCoordinateSystem =
  False`, `rtx.zUp = False`); a escala é `rtx.sceneScale` = 1 cm / unidade do jogo (o port usa metros: 0.01).
- **A matriz de cada desenho.** `objectToWorld` = `D3DTS_WORLD` (com vertex shaders, não é confiável e é ignorada;
  `rtx.fusedWorldViewMode` para jogos que juntam mundo e visão).
- **Hashes estáveis** ([Setting Up Asset Replacements](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/howto/learning-assets.html):
  "Asset replacement requires a stable hash"):
  - textura: o XXH3 de 64 bits do nível 0 do buffer da CPU no envio (`D3D9CommonTexture::SetupForRtxFrom`;
    `rtx.useObsoleteHashOnTextureUpload` volta ao XXH64 antigo). Mesmo conteúdo, mesmo hash, em qualquer fase e
    execução; um texel diferente (outra paleta, outro formato de envio) é outra textura;
  - geometria: regra `rtx.geometryAssetHashRuleString = positions,indices,geometrydescriptor` (a usada nas
    substituições; a de geração inclui ainda `texcoords,vertexlayout,vertexshader`), calculada sobre a faixa de
    vértices que o desenho usa (do menor ao maior índice);
  - o que quebra: vertex buffers dinâmicos reescritos a cada quadro (skinning na CPU, partículas: a posição muda, o
    hash muda), lotes que juntam objetos diferentes numa faixa que varia, recortes que mudam a faixa de índices
    ("Older games frequently exhibit unstable hashes in world geometry in part due to culling mechanisms"), atlas de
    texturas montados em tempo de jogo, alvos de renderização usados como textura;
  - para conferir: Alt+X > Developer Settings > Rendering > Debug > Debug View "Geometry Hash" (cores fixas = hash
    estável; partículas mudam de cor de propósito).
- **Instâncias.** A mesma malha (mesmo hash) desenhada com matrizes diferentes vira instâncias; o Remix acompanha cada
  uma entre quadros pela distância (`rtx.uniqueObjectDistance`, 300 unidades, sem a escala da cena).

## 4. Personagens com skinning

O Remix lê o **vertex blending da função fixa**: com `D3DRS_VERTEXBLEND` diferente de `D3DVBF_DISABLE` e pesos no
vértice (e, com `D3DRS_INDEXEDVERTEXBLENDENABLE`, os índices `BLENDINDICES`), `processSkinning` copia as matrizes
`D3DTS_WORLDMATRIX(0..n)` e o lado do path tracing faz o skinning na GPU (`gpu_skinning.comp.slang`), até 4 ossos por
vértice; a malha fica na pose de repouso no buffer, então o hash não muda com a animação. Os limites do dxvk-remix: em
hardware, `MaxVertexBlendMatrixIndex = 8` (`d3d9_adapter.cpp`, 9 matrizes); com processamento de vértices em software
(`D3DCREATE_SOFTWARE_VERTEXPROCESSING` ou `SetSoftwareVertexProcessing(TRUE)` num dispositivo misto), 256. Skinning na
CPU ("CPU (software) animation will likely flicker" na vista de hash) dá hash instável: a substituição de personagens
animados assim "occurs on the engine side". As substituições com esqueleto próprio reaproveitam as transformações dos
ossos do original (remapeamento de juntas), "Skinned replacements require expertise".

No port, a paleta por osso que o `AnimObjectDraw` monta (já no mundo, com a pose inversa) é exatamente o que
`D3DTS_WORLDMATRIX(n)` espera, e os vértices têm dois ossos e um peso (`D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4`,
`D3DVBF_1WEIGHTS`). O maior índice de osso do disco é 72: acima dos 8 de hardware do dxvk-remix, então esses desenhos vão
por processamento em software (dispositivo misto).

## 5. O resto do conteúdo

- **Luzes.** As luzes ligadas no dispositivo (`SetLight` + `LightEnable`) viram luzes do path tracer (pontuais e spot
  como esferas de raio `rtx.lightConversionSphereLightFixedRadius`, direcionais como distantes;
  `rtx.calculateLightIntensityUsingLeastSquares` ajusta a intensidade à curva de queda do original), lidas do estado
  mesmo com `D3DRS_LIGHTING` desligado (`processRenderState` usa `enabledLightIndices`). Sem nenhuma luz, o Remix cria
  uma de reserva (`rtx.fallbackLightMode`). `rtx.ignoreGamePointLights` e afins as desligam; luzes novas se põem no
  Toolkit.
- **Cores de vértice (a luz "assada").** O Remix usa a cor do vértice no material legado com
  `rtx.vertexColorStrength` (0.6) e, com `rtx.vertexColorIsBakedLighting` (padrão), divide cada componente pelo maior
  (tira o brilho, guarda o tom); `rtx.ignoreAllVertexColorBakedLighting` a ignora toda e `rtx.ignoreBakedLightingTextures`
  por textura. As fases do Nightfire têm a iluminação nas cores dos vértices, então o path tracing as reilumina sem
  apagar o tom.
- **Névoa.** Lida dos estados do Direct3D 9 (`D3DRS_FOGENABLE`, a cor, o modo de tabela ou de vértice, começo, fim,
  densidade: `setFogState`) e levada à névoa volumétrica (`rtx.enableFog`, `rtx.fogColorScale`, `rtx.fogIgnoreSky`).
- **Teste de alfa e misturas.** O material legado lê o teste de alfa (função e referência) e a mistura
  (`setLegacyMaterialState`); `rtx.enableAlphaTest`/`rtx.enableAlphaBlend` ligam o path tracing deles; cortes que o jogo
  fazia com mistura podem ser marcados como "Legacy Cutout"; `rtx.ignoreAlphaOnTextures`.
- **Decalques.** `rtx.decalTextures` (as marcas de tiro e pegadas: o substituto de `dynamicDecalTextures`, obsoleto)
  com um pequeno deslocamento sobre a superfície.
- **Partículas e billboards.** `rtx.particleTextures` (desenho mais barato, "soft particles"), `rtx.beamTextures`,
  `rtx.particleEmitterTextures`; o Remix 1.4/1.5 tem um sistema de partículas próprio para substituí-las. Hash instável
  é o esperado.
- **Céu.** `rtx.skyBoxTextures`, `rtx.skyBoxGeometries`, `rtx.skyAutoDetect` e `rtx.skyMinZThreshold` (céu desenhado
  com a profundidade do viewport no fundo); o céu marcado vira o ambiente do path tracer.
- **UI e 2D.** Um desenho com projeção ortográfica (`_44 = 1`) e sem escrita de Z é UI (`rtx.orthographicIsUI`, padrão
  ligado), ou um com textura de `rtx.uiTextures`; vértices pré-transformados (XYZRHW) só com
  `rtx.preTransformedVerticesIsUI` (padrão desligado; senão são rasterizados e ignorados pelo path tracing). No primeiro
  desenho de UI o Remix "injeta" o path tracing: tudo o que vem depois no quadro é rasterizado por cima
  (`rtx.skipDrawCallsPostRTXInjection` os descarta). Ou seja: **o 2D tem de vir depois de todo o 3D**.
- **Alvos de renderização e pós-processamento.** Desenhos num alvo que não é o principal são rasterizados ("Found a draw
  call to a non-primary, non-raytraced render target"), salvo os de `rtx.raytracedRenderTargetTextures`; passadas de
  tela cheia do jogo viram UI ou ficam rasterizadas. Os desenhos de sombra por stencil e de máscara de sombra são
  ignorados.

## 6. Requisitos e licenças (10/10/2026)

- **Hardware e sistema** ([Technical Requirements](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/introduction/intro-requirements.html)):
  mínimo Windows 10 e qualquer GPU RTX; recomendado Windows 11, GeForce RTX 4070 ou melhor, 12 GB de VRAM, 32 GB de RAM.
  A versão mínima do driver não está escrita na documentação (o driver Game Ready 610.62 saiu junto com o Remix 1.5):
  usar o driver atual. A RTX 5090 do usuário sobra.
- **Licenças.** O repositório [rtx-remix](https://github.com/NVIDIAGameWorks/rtx-remix) é MIT; o
  [dxvk-remix](https://github.com/NVIDIAGameWorks/dxvk-remix) é Zlib (com partes MIT); a ponte é MIT. DLSS, NRD e RTXDI
  seguem as licenças dos SDKs deles ("will continue to be available via their existing SDKs and licenses",
  [anúncio do código aberto](https://www.nvidia.com/en-us/geforce/news/rtx-remix-runtime-open-source-download/)).
- **Distribuir um mod.** O runtime pode ir junto com os avisos de licença (MIT/Zlib) e os componentes da NVIDIA pelas
  licenças deles. Os dados do jogo nunca: um mod do Nightfire só pode levar o que é novo (texturas e malhas feitas pelo
  modder, o `mod.usda`, o `rtx.conf`), nunca capturas (`rtx-remix/captures` guarda texturas e malhas do disco) nem o
  executável com dados. O jogador usa o próprio disco, como o port já exige. Não achei uma política da NVIDIA ou do
  ModDB sobre mods além das licenças: conferir antes de publicar.

## 7. O renderizador do port num caminho compatível com o Remix

| Parte (Direct3D 11 hoje) | Caminho Direct3D 9 compatível com o Remix | No protótipo |
|---|---|---|
| Malhas das fases (salas, células; `DrawItem` com a matriz da célula) | VB/IB `D3DPOOL_MANAGED` criados uma vez por malha (`GfxMirror`), `DrawIndexedPrimitive` por parte com a faixa exata de vértices, `SetTransform(D3DTS_WORLD)`: hash estável, path tracing | feito |
| Objetos (portas, itens, destrutíveis, provisórios) | o mesmo, com a matriz do quadro; a mesma malha em vários lugares vira instâncias; o modelo quebrado de um destrutível é outra malha, outro hash | feito |
| Personagens com skinning (paleta na GPU) | vertex blending indexado da função fixa (`D3DTS_WORLDMATRIX(n)` = a paleta), pose de repouso no buffer: skinning na GPU do Remix | feito (software acima de 9 matrizes no Direct3D 9 nativo e no dxvk-remix) |
| A arma em primeira pessoa (lista 2) | geometria do mundo diante do olho; para o view model do Remix, o viewport com profundidade de 0 a 0.1 e `rtx.viewModel.enable` | opcional (`--d3d9-viewmodel 0.1`) |
| Céu (`View_DrawSky`, modelos que seguem a câmera) | malhas comuns; marcadas em `rtx.skyBoxTextures` (ou `rtx.skyAutoDetect`) viram o ambiente | os hashes saem no `rtx.conf` sugerido |
| Passada de névoa (`psiDrawFogPass`: Z do GS em alfa) | névoa linear por vértice nos desenhos antes da passada, com começo e meio da curva do GS; o Remix a lê como névoa volumétrica | feito (aproximação) |
| Partículas (`psiDrawParticleList`, quadrados virados para a câmera) | `DrawPrimitiveUP` (dinâmico por natureza), `rtx.particleTextures` | feito |
| Estilhaços de vidro (`psiDrawShard`) | `DrawPrimitiveUP`; geometria dinâmica (o Remix a traça, sem substituir) | feito |
| Marcas de tiro e pegadas (anexos) | malhas comuns; `rtx.decalTextures` | os hashes saem no `rtx.conf` sugerido |
| Luzes da VU1 (no vertex shader: `1/d² - 1/r²`, as duas mais perto) | sem efeito na imagem rasterizada; passadas ao Remix como `D3DLIGHT9` pontuais (queda 1/d², alcance r, cor TColor/128); melhor depois pela API do Remix, com hash fixo por luz e a lista inteira de luzes da fase | feito (exportação aproximada) |
| Cores dos vértices (luz assada, escala do GS) | `D3DTOP_MODULATE2X` textura x cor; o Remix tira o brilho (`vertexColorIsBakedLighting`) | feito |
| Tweak (`TAmbient`) | `D3DTA_TFACTOR` num segundo estágio (o Remix lê `textureFactor` multiestágio) | feito |
| Mapa de ambiente (rotina 0x2e0) | sem geração de ST (o Remix lê as coordenadas do buffer): as superfícies cromadas ganham um material metálico no Remix | coordenadas da malha (a geração da função fixa falhou, ver abaixo) |
| Estado do GS (mistura, teste de alfa, Z) | estados do Direct3D 9; FIX no `D3DRS_BLENDFACTOR`; alfa do destino gravado (`SEPARATEALPHABLENDENABLE`) | feito; alfa acima de 1.0 satura, sem pré-multiplicação |
| Visibilidade por portais | o path tracing quer o que está fora da tela (reflexos, sombras, luz indireta): mandar as salas vizinhas também, e `rtx.antiCulling.object.enable` | pendente |
| 2D original (HUD, menus Original, legendas) | depois de todo o 3D, ortográfico, sem Z: UI do Remix | feito |
| Menus Modern e camada 2D HD (Direct2D/DirectWrite no dispositivo Direct3D 11) | não existem em Direct3D 9: desenhar no Direct3D 11 numa textura fora da tela, copiar para uma textura do Direct3D 9 e desenhá-la como um quadrado de UI (cópia pela CPU: a ponte não compartilha superfícies) | desligados (o modo força o estilo Original e a camada HD) |
| Interpolação entre ticks | o quadro interpolado alimenta os dois caminhos; o Remix vê as matrizes de cada quadro (vetores de movimento por instância) | feito |
| Opções de vídeo (escala, MSAA, filtro, tela cheia exclusiva) | do Remix (DLSS, Ray Reconstruction; `rtx.allowFSE`) | não se aplicam; o back buffer não acompanha a janela |
| Texturas HD do pacote do PCSX2 | não vão: mudariam os hashes; no Remix as texturas se trocam pelo hash dele | não usadas |

## 8. O protótipo

Arquivos: `src/render/remix_d3d9.h/.cpp` (`RemixD3D9Renderer`: a interface e a fábrica `create_remix_d3d9` no
cabeçalho, a classe com o Direct3D 9 só no `.cpp`), o espelho `GfxMirror` em `src/render/runtime_gfx.h`
(o `RuntimeGfx` passa a ele cada textura e malha criada na carga, com o mesmo slot e o mesmo id do Renderer) e o
`src/app/app_main.cpp` (opções, câmera, captura; o antigo `src/main.cpp`). O Direct3D 11 não muda: no modo `d3d9ff` ele
continua existindo (as camadas 2D e o Direct2D usam o dispositivo dele) numa janela invisível, desenhando nada; a janela
do jogo fica com o Direct3D 9.
As capturas do Direct3D 11 continuam idênticas às de antes (`pngdiff`: 0 pixels em 07000001 e 07000012).

Opções:

| Opção | Efeito |
|---|---|
| `--renderer d3d9ff` | o caminho de Direct3D 9 de função fixa (o padrão é `d3d11`); força o estilo de menu Original e desliga a camada 2D HD |
| `--d3d9-no-lights` | não passa as luzes da VU1 como `D3DLIGHT9` |
| `--d3d9-viewmodel <z>` | a lista 2 (a arma, o clarão) no viewport com profundidade de 0 a `z` (0.1 é o usual; o Remix a separa como view model); com ou sem a opção, o Z é limpo antes da lista 2 como o jogo pede (`limpa_z` no relatório; `docs/port.md`, "A arma em primeira pessoa e a limpeza do Z") |
| `--d3d9-remix-conf <arquivo>` | grava um `rtx.conf` sugerido: escala, eixos, `orthographicIsUI` e os hashes (XXH3 do nível 0, a conta do dxvk-remix) das texturas vistas no 2D, no céu, nas marcas e nas partículas (a UI que também aparece no 3D fica de fora); na captura e ao sair |
| `NF_D3D9_DUMP=<pasta>` | depuração: grava o nível 0 de cada textura enviada (`slot_N.png`) e, na captura, as do Direct3D 11 nos mesmos slots (`d11_slot_N.png`); dados do jogo, só em `out/` |

O relatório das capturas ganha a linha `d3d9ff dispositivo ... | malhas N (sw N, falhas N) texturas N (falhas N) |
quadro rigidas pele sw ambiente particulas cacos quads2d pulados sem_textura hd luzes view_model limpa_z nevoa começo-fim |
modulo <d3d9.dll carregado> | texturas_d3d11 N`.

**Capturas** (1024 x 576, `--fixed-video-hz 60`; os PNG ficam em `out/remix/`, fora do git). O Direct3D 11 com `--no-hd`
(o pacote do PCSX2 não vai ao Direct3D 9). Erro médio por canal (0 a 255) e pixels com diferença acima de 8:

| Cena | Erro médio | > 8 | O que difere |
|---|---|---|---|
| 07000012, `--wait 2` (corredor, guardas, HUD) | 2.92 | 8.7% | bordas (o meio pixel do Direct3D 9), os guardas e a arma um pouco mais escuros (sem as luzes da VU1), o teto em grade (mipmaps feitos na CPU) |
| 07000012, três tiros (clarão, fumaça, marcas) | 2.95 | 8.8% | o mesmo; clarão aditivo, marcas de tiro e sangue iguais |
| 07000001, `--wait 2` (cena de abertura, câmera 4, Kiko com skinning) | 4.35 | 17.8% | Kiko mais escura: a luz da VU1 que a ilumina não existe na função fixa; geometria, câmera e animação iguais |
| 07000001, `--wait 45` (sala, inimigos, cortina de dano) | 2.65 | 8.9% | os braços, o inimigo e a luminária sem as luzes da VU1 |
| 0700000c, `--wait 2` (área aberta, névoa) | 5.76 | 18.5% | a névoa linear fecha antes da curva do GS (a torre ao longe some na névoa) |
| 07000012, pausa (menu Original) | — | — | painel, abas, linhas e símbolos iguais |
| Menu principal (sem `--level`) | — | — | a página do jogo no estilo Original |

O skinning por vertex blending indexado bate com o do Direct3D 11 (o maior índice de osso, 72, passa do que o hardware
faz: 4 a 6 desenhos por quadro em software). As texturas são as mesmas por slot (`NF_D3D9_DUMP`: 513 de 513 iguais em
0700000c). Os hashes do `rtx.conf` sugerido saem iguais em duas execuções.

Três achados no caminho:

- **Capturas determinísticas.** Sem `--fixed-video-hz`, a captura em Direct3D 9 andava a ~60 quadros por segundo (o
  `Present` em janela) e a lógica às vezes dava dois ticks numa volta (o modo de 30 Hz do PS2): o jogo divergia do
  Direct3D 11 (a 07000001 aos 45 s tinha outra vida e outros inimigos). Com `--fixed-video-hz 60` os dois caminhos dão o
  mesmo estado.
- **A geração de coordenadas do mapa de ambiente.** `D3DTSS_TCI_CAMERASPACENORMAL` com `D3DTS_TEXTURE0` em duas partes
  da 0700000c estragou as coordenadas de todos os desenhos seguintes (o HUD virou blocos), mesmo voltando
  `TEXCOORDINDEX`, `TEXTURETRANSFORMFLAGS` e a matriz (Direct3D 9 nativo, dispositivo misto, nesta máquina). Como o
  Remix lê as coordenadas do buffer de qualquer jeito, essas partes (987 no disco) usam as da malha.
- **A névoa.** Nenhuma névoa do Direct3D 9 reproduz a curva em 1/z do GS; a reta passa pelo começo dela e pelo ponto de
  50%. A névoa por vértice funciona com `D3DRS_LIGHTING` desligado.

## 9. Como testar com o Remix (passos para o usuário)

O agente não baixou nada; estes passos precisam do usuário (download de ~221 MB).

1. Instalar o driver Game Ready atual da NVIDIA.
2. Baixar `remix-1.5.2-release.zip` (231.778.218 bytes) de
   <https://github.com/NVIDIAGameWorks/rtx-remix/releases/tag/remix-1.5.2> (ou instalar o RTX Remix pelo app da
   NVIDIA: o runtime fica em `<instalação>\deps\remix_runtime\runtime`).
3. Extrair e copiar o `d3d9.dll` de cima e a pasta `.trex` inteira (com `NvRemixBridge.exe` e o outro `d3d9.dll`) para
   `build\Release\`, ao lado do `nightfire.exe`. O port só carrega o `d3d9.dll` com `--renderer d3d9ff`; no modo
   normal ele nem é aberto. Para voltar ao normal basta apagar os dois.
4. Gerar um `rtx.conf` inicial (opcional): jogar um pouco com
   `build\Release\nightfire.exe --level 07000012 --renderer d3d9ff --d3d9-remix-conf build\Release\rtx.conf` (sem o Remix
   na pasta, ou com ele) e sair; o arquivo fica ao lado do executável, onde o Remix o procura.
5. Rodar `build\Release\nightfire.exe --level 07000012 --renderer d3d9ff --d3d9-viewmodel 0.1` em janela (sem
   `--screenshot`: as capturas desenham fora da tela). Deve aparecer "Welcome to RTX Remix...". A linha `modulo` do
   relatório de uma captura diz qual `d3d9.dll` foi carregado.
6. No menu do Remix (Alt+X; marcar "Always Developer Menu"), conferir:
   - **câmera**: o path tracing acompanha o olhar e o andar; a câmera livre do Remix (`rtx.camera.enableFreeCamera`,
     no menu de desenvolvedor) voa pela fase;
   - **hashes**: Rendering > Debug > Debug View "Geometry Hash": salas e objetos com cores fixas, personagens sem
     piscar enquanto andam; partículas mudam de cor (esperado);
   - **UI**: Game Setup > Step 1 UI Textures: o HUD e os menus já são UI pela projeção ortográfica; as texturas do
     `rtx.conf` sugerido devem aparecer marcadas (se não aparecerem, o hash calculado pelo port não é o do Remix:
     anotar);
   - **parâmetros**: Game Setup > Step 2: Scene Unit Scale 0.01, Z up desligado, mão esquerda desligado;
   - **céu, marcas, partículas**: Steps 3, 7 e 6 (as texturas sugeridas marcadas);
   - **luzes**: Rendering > Light Conversion; as luzes do jogo aparecem como esferas (ajustar o raio fixo, 4 unidades
     por padrão, que em metros é grande);
   - **view model**: com `rtx.viewModel.enable = True` e `rtx.viewModel.maxZThreshold = 0.1`, a arma separada do mundo;
   - **desempenho**: a taxa de quadros com a ponte (o port faz centenas a milhares de chamadas por quadro);
   - Enhancements > Capture Scene: uma captura em USD (fica em `rtx-remix/captures`; tem dados do jogo: não publicar).
7. Trazer de volta os logs (`rtx-remix\logs\remix-dxvk.log`, `bridge32.log` e `bridge64.log`, na pasta de trabalho de
   onde o jogo foi aberto; ver "Situação" no começo), o `rtx.conf` salvo pelo menu (Save Settings) e capturas de tela.

## 10. Riscos e esforço

Riscos:

- **Só para RTX.** O modo Remix não substitui nada: o Direct3D 11 continua o padrão (GPUs integradas, 60 fps).
- **A ponte de 32 bits.** Cada chamada de Direct3D 9 atravessa a fila para o processo de 64 bits; o port faz uma
  dezena de `SetRenderState` por parte de malha e milhares de partes em fases abertas (2965 na 0700000c). Pode limitar a
  CPU. Remédios: cache de estado (só mandar o que muda), juntar partes, e a API do Remix pela ponte para luzes e
  materiais. Um port de 64 bits (sem ponte) não é opção: as estruturas do PS2 têm ponteiros de 4 bytes.
- **Visibilidade por portais.** O jogo só desenha o que a câmera vê; o path tracing precisa do entorno (reflexos,
  sombras e luz que vem de fora da tela). Precisa de um modo que mande também as salas vizinhas (sem mudar a lógica do
  jogo) e do anti-culling do Remix.
- **Luz.** A iluminação original está em três lugares: cores de vértice (o Remix tira o brilho), luzes dinâmicas da VU1
  (exportadas com outra curva de queda) e o tweak; acertar o visual pede ajuste por fase, e luzes e materiais novos no
  Toolkit são trabalho de modder.
- **Hashes.** Partículas, estilhaços e o 2D são dinâmicos por natureza (marcados por textura); texturas animadas só
  mandam o primeiro quadro; se o formato de envio das texturas mudar (o alfa dobrado, BGRA), todos os hashes mudam:
  fixar a convenção antes de qualquer mod.
- **O Remix muda rápido.** Opções e API mudam entre versões (a ponte foi arquivada e mudou de repositório em 2025);
  fixar uma versão do runtime e reconferir os fatos deste documento.
- **O que ficou de fora do protótipo**: os menus Modern e a camada HD, o redimensionamento do back buffer, a geração de
  coordenadas do mapa de ambiente, alfa acima de 1.0, o envio das salas vizinhas.

Esforço estimado para um caminho "pronto para o Remix" (além do protótipo, ~1 dia):

| Etapa | Estimativa |
|---|---|
| Cache de estado e menos chamadas (medir com a ponte primeiro) | 2-3 dias |
| Salas vizinhas para o path tracing + anti-culling | 2-3 dias |
| Luzes pela API do Remix (lista inteira de luzes da fase, hash fixo por luz) e materiais básicos | 3-5 dias |
| `rtx.conf` padrão do port (céu, partículas, marcas, view model, escala) validado no Remix | 2-3 dias |
| Menus Modern e camada HD como textura de UI | 3-4 dias |
| Redimensionar a janela, tela cheia, opção no menu de vídeo | 1-2 dias |
| Validação no hardware do usuário (iterativa) | 1-2 semanas |
| Conteúdo (texturas PBR, malhas, luzes novas no Toolkit) | trabalho de modder: meses |
