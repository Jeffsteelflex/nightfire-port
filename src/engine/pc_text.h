#pragma once
// Textos só do port (a página de opções do PC e a entrada dela na pausa), em português e em inglês.
// O idioma vem de [ui] language_pc = pt|en no nightfire.ini (padrão en, como o texto do jogo); um dia pode seguir o idioma do
// jogo (LanguageToLoad). Os textos do jogo continuam vindo do *Txt.dat.
//
// As fontes do jogo (NFont2, SerpLight, Medium) só têm os 55 caracteres acima de 0x7f que o jogo usa
// (…Œ‘’“”–—™¡§©®º¿ÀÁÂÄÅÈÉÊÍÎÑÓÖÙÚÜßàáâäåçèéêëìíîïñòóôöùúûü, Windows-1252): faltam Ã, Õ, Ç, Ô, ã, õ,
// ° e outros, e o __Font_DrawText desenharia o glifo anterior no lugar de um que falta. Então o texto
// (UTF-8 na tabela) vira Windows-1252 e cada caractere sem glifo na fonte do menu (a 2, SerpLight) vira a
// letra sem acento: "OPÇÕES" sai "OPCOES", "Resolução" sai "Resoluçao". Com a camada HD ligada
// (PcText_SetHdText), o texto mantém todos os caracteres do Windows-1252: a camada redesenha a string com
// o DirectWrite (o glifo trocado do __Font_DrawText só dá a posição) e "OPÇÕES" sai certo.

#include <cstddef>
#include <cstdint>
#include <string>

enum class PcText : uint16_t {
    PauseEntry,       // a linha nova da lista principal da pausa
    // As abas da página de opções (engine/menu_pc_options.h).
    TabVideo,
    TabGraphics,
    TabInterface,
    TabAudio,
    TabControls,
    TabKeys,
    // As barras de ajuda: na faixa das abas, nas linhas de opções, na de controles e na de teclas.
    HelpTabs,
    HelpOptions,
    // VÍDEO, GRÁFICOS e INTERFACE (nf::VideoSettings).
    Resolution,
    WindowMode,
    VSync,
    FieldOfView,      // formato: os graus
    RenderScale,
    AntiAliasing,
    TextureFilter,
    HdTextures,
    Dlss,
    HdInterface,
    HudAnchor,
    Language,
    Apply,
    ModeWindowed,
    ModeBorderless,
    ModeExclusive,
    Off,
    On,
    FilterBilinear,
    FilterTrilinear,
    FilterAniso,      // formato: "%dx"
    Msaa,             // formato: "%dx"
    AnchorCentre,
    AnchorEdges,
    LanguageName,     // o nome do próprio idioma
    ComingSoon,
    Desktop,          // a resolução da janela sem bordas (a do monitor)
    ConfirmText,      // formato: os segundos que faltam
    Yes,
    No,
    // ÁUDIO (ainda sem o módulo de som: as barras do jogo, apagadas).
    AudioMusic,
    AudioEffects,
    AudioNote,
    // CONTROLES e TECLAS (engine/menu_pc_controls.h). Os nomes dos comandos ficam na tabela deles
    // (platform/input_bindings.h, kBindCommands).
    CtlDevice,
    CtlKeyboardMouse,
    CtlPad,             // formato: o número do controle
    CtlKeyboardPad,     // formato: o número do controle
    CtlMouseSens,       // formato: a sensibilidade x 1000 (os centímetros por volta não cabem na coluna do nome)
    CtlAimSens,         // formato: o multiplicador
    CtlMouseInvert,
    CtlPadSens,         // formato: o multiplicador
    CtlDeadzone,        // formato: a porcentagem
    CtlPadInvert,
    CtlVibration,
    CtlTriggers,
    CtlHelp,
    KeysHelp,
    KeysRestore,
    KeysPress,          // formato: o comando
    KeysCancelled,
    KeysCleared,        // formato: o comando
    KeysBound,          // formato: a tecla, o comando
    KeysMoved,          // formato: a tecla, o comando, o comando que a perdeu
    KeysSwapped,        // formato: a tecla, o comando, o comando que trocou, a tecla dele
    KeysRefused,        // formato: a tecla, o comando que ficaria sem tecla
    KeysRequired,       // formato: o comando
    KeysRestored,
    // O estilo dos menus e o estilo Modern (src/ui/modern_options.h).
    MenuStyle,  // a linha do estilo dos menus (as duas páginas)
    StyleModern,
    StyleOriginal,
    MdTopPause,  // barra de cima
    MdTopOptions,
    MdHeader,  // o cabeçalho das categorias
    MdCatVideo,  // categorias
    MdCatGraphics,
    MdCatInterface,
    MdCatAudio,
    MdCatControls,
    MdCatMouseKeyboard,  // a categoria das teclas (Cat::MouseKeyboard): KEY BINDINGS
    MdCatController,
    MdSecDisplay,  // seções
    MdSecView,
    MdSecQuality,
    MdSecTextures,
    MdSecUpscaling,
    MdSecHud,
    MdSecMenus,
    MdSecVolume,
    MdSecInput,
    MdSecLook,
    MdSecMouse,
    MdSecBindings,
    MdSecSticks,
    MdSecButtons,
    MdFieldOfView,  // linhas sem número no nome
    MdDevice,
    MdButtonLayout,
    MdMouseSens,
    MdAimSens,
    MdPadSens,
    MdDeadzone,
    MdTriggers,
    MdApply,
    MdColAction,  // a tabela de teclas
    MdColPrimary,
    MdColSecondary,
    MdColController,
    MdRestoreKeys,
    MdCurrentValue,  // a descrição
    MdPending,
    MdMeterRender,
    MdPromptDefaults,  // barra de baixo
    MdPromptSelect,
    MdPromptBack,
    MdPromptApply,
    MdPromptChange,
    MdPromptRebind,
    MdPromptClear,
    MdPromptCancel,
    MdPromptToggle,
    MdPromptAdjust,
    MdKeepTitle,  // diálogos
    MdKeepBody,  // formato: os segundos
    MdKeep,
    MdRevert,
    MdDefaultsTitle,
    MdDefaultsBody,  // formato: a categoria
    MdRestore,
    MdCancel,
    MdToastApplied,  // avisos
    MdToastDefaults,
    MdToastKept,
    MdToastReverted,
    MdToastStyle,
    MdPressKey,  // formato: o comando
    MdPressPad,  // formato: o comando
    MdWantKey,
    MdWantPad,
    MdCaptureHint,
    MdNone,  // célula vazia
    MdFooterPort,  // barra de baixo
    MdFooterGame,
    MdFooterBuild,  // formato: a versão
    MdFooterLive,
    MdDescResolution,  // descrições
    MdDescWindowMode,
    MdDescVSync,
    MdDescFov,
    MdDescRenderScale,
    MdDescAntiAliasing,
    MdDescFilter,
    MdDescHdTextures,
    MdDescDlss,
    MdDescHdUi,
    MdDescHudAnchor,
    MdDescMenuStyle,
    MdDescLanguage,
    MdDescAudio,
    MdDescDevice,
    MdDescButtonLayout,
    MdDescInvertMouse,
    MdDescInvertPad,
    MdDescMouseSens,
    MdDescAimSens,
    MdDescPadSens,
    MdDescDeadzone,
    MdDescTriggers,
    MdDescVibration,
    MdDescApply,
    MdDescKeys,
    MdDescRestoreKeys,
    // As opções do jogo no estilo Modern (src/ui/modern_options.h: GAMEPLAY, as legendas em INTERFACE, os volumes
    // do ÁUDIO). As descrições em inglês são os textos de ajuda do próprio jogo (help_table, *Txt.dat), um por valor.
    MdCatGameplay,  // a categoria
    MdSecAiming,  // seções
    MdSecMovement,
    MdSecItemsHud,
    MdSecText,
    MdAutoAim,  // linhas
    MdCrosshairs,
    MdManualAim,
    MdCrouch,
    MdAutoSwitch,
    MdFlashing,
    MdHudAlways,
    MdSubtitles,
    MdModeToggle,  // os valores de agachar e mirar
    MdModeHold,
    MdDescAutoAimOn,  // descrições (as do jogo, pelo valor)
    MdDescAutoAimOff,
    MdDescCrosshairsOn,
    MdDescCrosshairsOff,
    MdDescManualAimToggle,
    MdDescManualAimHold,
    MdDescCrouchToggle,
    MdDescCrouchHold,
    MdDescAutoSwitchOn,
    MdDescAutoSwitchOff,
    MdDescFlashingOn,
    MdDescFlashingOff,
    MdDescHudAlwaysOn,
    MdDescHudAlwaysOff,
    MdDescSubtitlesOn,
    MdDescSubtitlesOff,
    MdDescMusic,
    MdDescEffects,
    MdDescVibrationOn,
    MdDescVibrationOff,
    MdAudioNotice,  // o aviso do ÁUDIO
    // A pausa, o fim de missão e o resultado no estilo Modern (src/ui/modern_pause.h, modern_end.h).
    MpHeader,  // o cabeçalho da pausa
    MpResume,  // a lista da pausa Modern
    MpResumeSub,
    MpRestart,
    MpRestartSub,
    MpQuit,
    MpQuitSub,
    MpOptions,
    MpOptionsSub,
    MpRestartBody,  // o texto das confirmações
    MpQuitBody,
    MpLevel,  // formato: o nível e quantos
    MpDifficulty,
    MpObjectivesDone,  // o medidor da pausa
    MpClassified,  // os objetivos escondidos
    MpClassifiedStatus,
    MpClassifiedBody,
    MpStatus,
    MpCountDone,  // o resumo dos objetivos
    MpCountFailed,
    MpCountHidden,
    MpSecObjectives,  // seções
    MpSecLayout,
    MpSecButtons,
    MpRebindHint,
    MpPromptResume,  // barra de baixo
    MpNoObjectives,
    MpNoHint,
    MpDevice,
    MpTabObjectives,  // as abas sem o texto do jogo (testes)
    MpTabControls,
    MpTabScore,
    MfReplaySub,  // o fim de missão Modern
    MfRestartSub,
    MfQuitSub,
    MrContinue,  // o resultado Modern
    MrContinueSub,
    MrStats,
    MrStatsSub,
    MrDossier,
    MrNoMedal,
    MrMedal,
    MrBonus,
    MrToastDossier,
    FeMultiplayer,  // o menu principal: os avisos do que não foi portado
    FeDriving,
    FeMainHeader,  // o menu principal Modern (src/ui/modern_front.h)
    FeQuit,
    FeQuitSub,
    FeQuitTitle,
    FeQuitBody,
    FeQuitYes,
    FeDescOptions,
    FeDescQuit,
    FeSubNightfire,
    FeSubMultiplayer,
    FeSubCodenames,
    FeStepCodename,  // as etapas da campanha
    FeStepDifficulty,
    FeStepMission,
    FeTagNew,  // as linhas das rodas de codinomes
    FeTagNoSave,
    FeTagDefault,
    FeTagCard,
    FeListing,
    FeLocked,  // o mapa das missões
    FeDrivingTag,
    FeDrivingNote,
    FeLockedToast,
    FeBestMedal,
    FeNoMedal,
    FeBestScore,
    FeMissions,
    FeNotAvailable,  // o menu do codinome
    FeNotAvailableToast,
    FeCodenameHint,  // o teclado da tela
    FeCodeHint,
    FeKeySpace,
    FeKeyDel,
    FeKeyEnd,
    FeCardTitle,  // os títulos das caixas do jogo
    FeNoticeTitle,
    FeConfirmTitle,
    FeWarningTitle,  // o aviso de controle desconectado
    FeWarningHint,
    FeProfile,
    FeCode,
    FePressStart,  // a tela de título
    DbgControls,  // a página de depuração (engine/menu_cheats.h): as caixas
    DbgFly,
    DbgWeapons,
    DbgInvincible,
    DbgHud,
    DbgCoords,
    DbgWidescreen,
    DbgDrones,
    DbgBlind,
    DbgDroneDebug,
    DbgDummy,
    DbgFreeze,
    DbgMusic,
    DbgDrawAll,
    DbgZeroG,
    DbgLockup,
    DbgBright,
    DbgUnlock,
    DbgEndMission,  // o botão sem nome (0x1000021e)
    DbgFail,  // as linhas da página 0x4000004c (P_CHEATMEDAL_Handler)
    DbgComplete,
    DbgBronze,
    DbgSilver,
    DbgGold,
    DbgPlatinum,
    MrDossierSub,  // o resultado Modern: a linha do dossiê
    FeCredits,  // o menu principal Modern: a linha dos créditos (P_CREDITS)
    FeCreditsSub,
    FeDescCredits,
    DsHeader,  // o dossiê Modern (src/ui/modern_dossier.h)
    DsRequires,
    DsRewards,
    DsUpgrade,
    DsUpgradeLevel,
    DsNoMedal,
    DsPromptScroll,
    MpScoreBody,  // a aba SCORE da pausa Modern com um jogador
    // O ritmo dos quadros (render/frame_interp.h; VÍDEO nas duas páginas de opções).
    FpsCap,              // o limite de quadros
    FpsCapValue,         // formato: os quadros por segundo
    FrameInterpolation,  // a interpolação entre os ticks
    InstantMouse,        // o olhar do mouse sem o atraso da interpolação
    MdSecMotion,         // a seção deles no Modern
    MdDescFpsCap,
    MdDescInterpolation,
    MdDescInstantMouse,
    // O adaptador de vídeo (platform/gpu_adapter.h; VÍDEO nas duas páginas de opções).
    GraphicsAdapter,      // o nome da linha
    GraphicsAdapterRow,   // o nome da linha na página Original (avisa que precisa reiniciar)
    AdapterAuto,
    AdapterIntegrated,
    AdapterDiscrete,
    MdDescAdapter,        // formato: o adaptador em uso
    MdToastRestart,
    MdMeterMouse,  // a tela Modern: o medidor da sensibilidade do mouse (a volta em centímetros)
    MdMeterMouseValue,
    // A mira moderna e a roda de armas (engine/player_ads.h, engine/weapon_wheel.h; GAMEPLAY nas duas páginas).
    MdAimStyle,          // o nome da linha
    AimStyleAuto,        // os valores
    AimStyleModern,
    AimStyleClassic,
    MdDescAimStyle,
    MdWheelSlowmo,       // o nome da linha
    WheelSlowmoPause,    // os valores: 0, os por cento, 1
    WheelSlowmoPercent,  // formato: os por cento da velocidade
    WheelSlowmoOff,
    MdDescWheelSlowmo,
    MdSecWheel,          // a seção da roda no Modern
    WheelWeapons,        // a roda: os anéis
    WheelGadgets,
    WheelRelease,        // a linha de baixo: soltar escolhe
    WheelKeep,           // sem espaço apontado
    WheelNoAmmo,
    WheelInHand,
    WheelUnarmed,        // o soco (a arma 1 não tem nome no HUD)
    SuHeader,  // a tela de configuração (src/ui/modern_setup.h): o cabeçalho
    SuTitle,
    SuPrompt,
    SuIntro,
    SuBrowse,  // as linhas
    SuBrowseSub,
    SuDrive,  // formato: a letra da unidade ("D:")
    SuDriveSub,
    SuPlay,
    SuPlaySub,
    SuPack,
    SuQuit,
    SuDescBrowseTitle,  // a descrição de cada linha
    SuDescBrowse,
    SuDescDriveTitle,
    SuDescDrive,
    SuDescPlayTitle,
    SuDescPlay,
    SuDescPlayLocked,
    SuDescPackTitle,
    SuDescPack,  // formato: o tamanho ("1.9 GB")
    SuPackWarning,
    SuDescQuitTitle,
    SuDescQuit,
    SuDisc,  // o disco escolhido
    SuSaveAs,
    SuNone,
    SuReady,  // formato: o código do disco
    SuReadyUnverified,
    SuErrMissing,  // os problemas de um disco
    SuErrUnreadable,
    SuErrNotPs2,
    SuErrWrongGame,  // formato: o código do disco
    SuErrWrongVersion,  // formato: o código do disco
    SuErrIncomplete,  // formato: o arquivo que falta
    SuErrTitle,  // a caixa do problema
    SuOk,
    SuCancel,
    SuPicking,
    SuPickerTitle,  // a janela de escolha do Windows
    SuPickerImages,
    SuPickerAll,
    SuPackingTitle,  // a cópia em arquivo único
    SuPacking,  // formato: os por cento
    SuPackDoneTitle,
    SuPackDone,  // formato: o arquivo
    SuPackFailTitle,
    SuPackInGit,
    SuPackSame,
    SuPackNoCnf,
    SuPackMissing,  // formato: o arquivo
    SuPackNoSpace,  // formato: o tamanho
    SuPackWrite,  // formato: o arquivo
    SuPackRead,  // formato: o arquivo
    SuPackCancelled,
    SuKindImage,  // o tipo do disco
    SuKindFolder,
    SuKindDrive,
    SuKindPacked,
    LnHeader,  // a tela de entrada (o launcher, src/ui/modern_setup.h): o cabeçalho e o rodapé
    LnFooter,
    LnSecDisc,  // as seções
    LnSecDisplay,
    LnSecInterface,
    LnSecStartup,
    LnDisc,  // as linhas
    LnDrive,  // formato: a letra da unidade ("D:")
    LnPack,
    LnHdUi,
    LnShowAtStartup,
    LnDiscNone,  // os valores
    LnDiscPacked,
    LnDiscDrive,  // formato: a letra da unidade
    LnDriveUse,
    LnDriveInUse,
    LnDriveLocked,
    LnPackValue,  // formato: o tamanho ("1.9 GB")
    LnPackLocked,
    LnDescPlay,  // as descrições
    LnDescQuit,
    LnDescDisc,
    LnDescDiscPacked,
    LnDescResolution,
    LnDescWindowMode,
    LnDescMenuStyle,
    LnDescHdUi,
    LnDescShow,
    LnChanged,  // o aviso: há mudanças (gravadas pelo PLAY)
    LnPromptBrowse,  // os comandos de baixo
    LnPromptPlay,
    LnPromptQuit,
    Count,
};

// O idioma dos textos do port: "pt" ou "en" (outro valor vira "pt").
void PcText_SetLanguage(const std::string& lang);
const std::string& PcText_Language();
// A camada 2D em alta definição está desenhando os textos: sem trocar os caracteres que faltam nas fontes.
void PcText_SetHdText(bool on);
bool PcText_HdText();
// Muda a cada troca de idioma ou de PcText_SetHdText (quem copia os textos sabe quando refazer).
uint32_t PcText_Generation();
// O texto na fonte do jogo (Windows-1252, só caracteres que a fonte do menu tem). O ponteiro é fixo para
// cada id (o rótulo do menu o guarda); o conteúdo segue o idioma atual.
const char* PcText_Get(PcText id);
// O texto em UTF-8, como está na tabela (testes e relatórios).
const char* PcText_Utf8(PcText id, const std::string& lang);
// No idioma atual (UTF-8).
inline const char* PcText_Utf8(PcText id) { return PcText_Utf8(id, PcText_Language()); }
// UTF-8 -> Windows-1252 com os caracteres sem glifo trocados pela letra sem acento (ou '?').
// has_glyph nulo: a fonte 2 do jogo (FontTable), ou só ASCII se as fontes não foram carregadas.
std::string PcText_ToGame(const char* utf8, bool (*has_glyph)(uint8_t) = nullptr);

// ---- Textos do jogo trocados no PC (só do port) ----
//
// Alguns rótulos do *Txt.dat falam do hardware do PS2: o cartão de memória ("memory card (8MB) (for PlayStation®2)
// in MEMORY CARD slot 1", "do not remove... switch off the console"), o controle analógico (DUALSHOCK®2) e o
// multitap. No PC não há cartão (os perfis ficam numa pasta, platform/psi_card.h) nem portas de controle: a tabela
// kLabelOverrides (pc_text.cpp; a lista também em docs/port.md, "Textos do PC no lugar dos do PS2") dá um texto do
// PC, em inglês e em português ([ui] language_pc), para cada um desses rótulos. Txt_BindLabel (engine/txt.cpp) a
// consulta antes de devolver a string do arquivo, então vale para tudo o que o jogo mostra (as caixas do cartão no
// estilo Original, o aviso do controle desconectado, e os menus Modern, que leem o texto da caixa). O arquivo do
// jogo não muda.
//
// Duas travas: a string do jogo tem de conter o trecho `match` (outra versão do arquivo, com outros rótulos, passa
// direto) e o texto do PC só pode usar, na mesma ordem, um prefixo das conversões (%s, %d) da string do jogo,
// porque o jogo o usa como formato do snprintf com os argumentos do original ("%.0s" consome o slot sem
// imprimir). Sem as duas, fica a string do jogo.
struct PcLabelOverride {
    uint32_t label;     // o rótulo do jogo (tabela << 24 | índice)
    const char* match;  // um trecho da string do jogo (sem diferenciar maiúsculas)
    const char* pt;     // UTF-8
    const char* en;
};
size_t PcText_LabelOverrideCount();
const PcLabelOverride& PcText_LabelOverrideAt(size_t i);
// O texto do PC para o rótulo, na codificação do jogo (como PcText_Get: o ponteiro é fixo para cada rótulo e o
// conteúdo segue o idioma atual), ou nulo: o rótulo não tem troca, ou game (a string do jogo) não tem o trecho
// esperado ou consome menos argumentos que o texto do PC.
const char* PcText_GameLabel(uint32_t label, const char* game);
// As conversões de pc formam um prefixo das de game (%s com %s, %d/%i com %d/%i; flags, largura e precisão à parte;
// "%%" não conta)? É o que torna o texto do PC seguro como formato no lugar do do jogo.
bool PcText_FormatPrefix(const char* pc, const char* game);
