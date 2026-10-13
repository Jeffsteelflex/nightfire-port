// Port de ResetMap_Load, ResetMap_GameInit, GetLevelToLoadFromMovieCode, ReadTuningVars e da tela de carga
// (ResetMap_GenLoadScreen, ResetMap_LevelCode2Img). Ver resetmap.h.

#include "engine/lights.h"
#include "engine/resetmap.h"

#include <cstdio>
#include <cstring>

#include "engine/build.h"
#include "engine/collide.h"
#include "engine/drone.h"
#include "engine/effect.h"
#include "engine/font.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/loader.h"
#include "engine/memory.h"
#include "engine/mission.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/sprite.h"
#include "engine/txt.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "platform/psi.h"
#include "platform/psi_draw.h"
#include "platform/psi_gfx.h"
#include "platform/psi_loading.h"

uint8_t MPSettings[476];
uint8_t mf_menu = 0;
uint32_t switch_NO_DRONES = 0;
uint8_t Drone_bDisableSystem = 0;
obj_tag* glb_players[4];
uint8_t IsWarmReset = 0;
P32<cel_tag>* gbl_room_cels = nullptr;  // um vetor de palavras de 4 bytes no heap do jogo
uint32_t DrawInfo[4];
uint8_t g_port_no_load_screen = 0;

namespace {

uint32_t LoadState_270 = 0;
uint32_t StoreLevel_271 = 0;

constexpr uint32_t kFrontEnd = 0x7000048;  // pacote do menu principal
constexpr uint32_t kLoadScreen = 0x7000500;

void load_pack(uint32_t file) {
    // Só no PC: a cada volta, as bolinhas da tela de carga andam pelos VBlanks que passaram (no PS2 é a VBlankInt).
    while (static_cast<int8_t>(LoaderLoad(LOADER_ANIMS, file, nullptr, 0)) != 0) {
        psi_loading_pump();
    }
    while (LoaderLoad(LOADER_LEVEL, file, nullptr, 0) != 0) {
        psi_loading_pump();
    }
}

// Reinício comum aos passos 0 e 3.
void reset_all() {
    psiResetResources();
    MenuManager_Load(0, nullptr);
    Sound_Kill();
    // Só no port: a lista provisória e o céu (GfxList) apontam para o heap, que Mem_Init vai apagar. O original limpa
    // o céu no Env_Reset do ResetMap_GameInit (weather.cpp); o port limpa aqui também, antes do Mem_Init (o passo 0
    // de uma carga do menu principal não chama o ResetMap_GameInit).
    g_provisional_objects.clear();
    g_pending_placements.clear();
    // Só no port: as câmeras de terceira pessoa da fase anterior (o original nunca limpa ThirdPersonCameras, e um
    // fio que pedisse um índice que a fase nova não cria leria um objeto do heap já reiniciado).
    std::memset(ThirdPersonCameras, 0, sizeof(ThirdPersonCameras));
    g_all_placements.clear();
    g_player_init_call = PlayerInitCall{};
    std::memset(GfxList.sky, 0, sizeof(GfxList.sky));
    Mem_Init();
    hashtable_initialise();
    Sound_Init();
    Input_Ready();
}

// Script que acompanha cada filme de fundo (ResetMap_Load, passo 0).
uint32_t movie_script(uint32_t movie) {
    switch (movie) {
        case 0x7100001: return 0x600095a;
        case 0x7100005: return 0x6000961;
        case 0x7100009: return 0x6000963;
        case 0x710000c: return 0x6000965;
        case 0x7100011: return 0x6000967;
        case 0x7100014: return 0x6000969;
        case 0x7100018: return 0x600096b;
        case 0x7300004: return 0x600095d;
        case 0x7300008: return 0x6000962;
        case 0x730000b: return 0x6000964;
        case 0x730000d: return 0x6000966;
        case 0x7300013: return 0x6000968;
        case 0x7300017: return 0x600097a;
        case 0x730001b: return 0x600096c;
        default: return 0;
    }
}

}  // namespace

// [decomp] 0x001d5080 ResetMap_GameInit
void ResetMap_GameInit() {
    GameState.level_file = GameState.load_file & 0xff00ffff;
    std::memcpy(MPSettings + 0x180, &GameState.multiplayer, 4);
    GameState.start_request = 0;
    GameState.unknown_24 = 0;
    GameState.paused = 0;
    GameState.unknown_51 = 1;
    mf_menu = 0;
    if (switch_NO_DRONES != 0) Drone_bDisableSystem = 1;
    GS_PauseGame(0);
    InitShards();
    Player_ResetStartPos();
    Env_Reset();
    std::memset(glb_viewer, 0, sizeof(glb_viewer));
    std::memset(glb_players, 0, sizeof(glb_players));
    glb_world = build_alloc_world();
    Txt_LanguageInit(IsWarmReset == 0);
    Script_Init();
    control_init_object_lists();
    View_Init();
    Sprite_InitLists();
    Text_Init(static_cast<int8_t>(IsWarmReset));
    Effect_InitLists();
    Light_Init();
    Drone_LevelReset();
    Door_Init();
    Copter_Init();
    Sensor_Init();
    Coll_ResetHitHeap();
    Bullet_InitCasingLists();
    AnimCacheInit();
    AnimPreLoadInit(0);
    AIPath_Init();
    Creature_Init();
    MP_Init();
    Camera_CreateCameras();
    Drone_PreLoad_Init();
    MenuManager_Init();
    Car_Init();
    SSys_Init();
    gbl_room_cels = nullptr;
    if (IsWarmReset == 0) Init_SwitchChannels();
    Sound_SetLogSfxFile();
    if ((GameState.load_file & 0xff0000) == 0) Mission_Init(GameState.level_file, IsWarmReset);
}

namespace {
// As dicas da tela de carga por fase (rótulos do texto do jogo; .rodata 0x00305308-0x00305584 e .sdata
// 0x0030d020-0x0030d034 do ACTION.ELF, a ordem do arquivo) e as genéricas (0x003054d8, 43). As do multijogador
// (0x00305438, 40) são os rótulos 0x0100013e-0x01000165 em sequência.
constexpr uint32_t kTips01[] = {0x10000d3, 0x10000d4};
constexpr uint32_t kTips02[] = {0x10000c7, 0x10000c8, 0x10000cb, 0x10000d1};
constexpr uint32_t kTips03[] = {0x10000ca, 0x10000cd, 0x10000ce, 0x10000c9, 0x10000d2};
constexpr uint32_t kTips04[] = {0x10000cf, 0x10000d0};
constexpr uint32_t kTips06[] = {0x10000c0, 0x10000c1};
constexpr uint32_t kTips08[] = {0x10000c3, 0x10000c4, 0x10000c5, 0x10000c6};
constexpr uint32_t kTips09[] = {0x10000d5, 0x10000d8, 0x10000df, 0x10000e2};
constexpr uint32_t kTips0a[] = {0x10000d7, 0x10000d9, 0x10000db, 0x10000e0, 0x10000e3, 0x10000e4, 0x10000e6};
constexpr uint32_t kTips0b[] = {0x10000dc, 0x10000da, 0x10000dd, 0x10000de, 0x10000e1, 0x10000e5};
constexpr uint32_t kTips0c[] = {0x10000e7, 0x10000e9, 0x10000eb, 0x10000ed, 0x10000ef, 0x10000f1, 0x10000f4};
constexpr uint32_t kTips0d[] = {0x10000e8, 0x10000ea, 0x10000ec, 0x10000ee, 0x10000f0, 0x10000f2, 0x10000f3, 0x10000f5};
constexpr uint32_t kTips11[] = {0x10000f6, 0x10000f8, 0x10000f8};
constexpr uint32_t kTips12[] = {0x10000fe, 0x10000f9, 0x10000fa, 0x10000fc, 0x10000fd};
constexpr uint32_t kTips14[] = {0x1000103, 0x1000104, 0x1000105, 0x1000106};
constexpr uint32_t kTips16[] = {0x1000100, 0x1000101, 0x1000102};
constexpr uint32_t kTips1b[] = {0x1000108, 0x1000109, 0x100010a, 0x100010b, 0x100010c,
                                0x100010d, 0x100010e, 0x100010f, 0x1000110};
constexpr uint32_t kTipsGeneric[] = {
    0x1000111, 0x1000112, 0x1000113, 0x1000114, 0x1000115, 0x1000116, 0x1000117, 0x1000118, 0x1000119,
    0x100011a, 0x100011b, 0x100011c, 0x100011d, 0x100011e, 0x100011f, 0x1000120, 0x1000121, 0x1000122,
    0x1000123, 0x1000124, 0x1000125, 0x1000126, 0x1000127, 0x1000128, 0x100012a, 0x100012b, 0x100012c,
    0x100012d, 0x100012e, 0x100012f, 0x1000130, 0x1000131, 0x1000132, 0x1000133, 0x1000134, 0x1000135,
    0x1000136, 0x1000137, 0x1000138, 0x1000139, 0x100013a, 0x100013b, 0x100013c};
static_assert(sizeof(kTipsGeneric) / 4 == 0x2b);

// A descrição da imagem (LoadingGfx.266, 0x002c93f8): 512 x 448 na lista 99, cor 0x7f7f7fff, prioridade 0x1e.
SpriteInfo LoadingGfx_266{0x7f7f7fffu, 0x7f7f7fffu, 0x1e, 0, 0, 0, 0x200, 0x1c0, 0, 0, 0, 0, 0xffffffffu, nullptr,
                          0x3000061, 0x63, {0, 0, 0}};
// Os formatos dos textos (.sdata 0x0030d040 e 0x0030d048): a fonte 2, o primeiro também com 0xfe 3.
char LoadFmt_0030d040[8] = {'\xff', '\x02', '\xfe', '\x03', 0, 0, 0, 0};
char LoadFmt_0030d048[8] = {'\xff', '\x02', 0, 0, 0, 0, 0, 0};
constexpr char kLoadLineFmt[] = "%c%s%c %s";  // 0x003056c0

// Um sorteio da tabela: Rand_Random() % n (divu: sem sinal) e o rótulo.
template <size_t N>
char* pick(const uint32_t (&tips)[N]) {
    return Txt_BindLabel(tips[Rand_Random() % N], 0);
}

// Os textos de um bloco quebrados na largura (Font_WordWrapString), um sprite da lista 99 por linha, de 15 em 15
// pixels a partir de y. params_first: SetParams antes do texto (o rótulo 0x3c1), senão depois (o nome e a dica).
float load_lines(char* text, char* format, uint32_t width, int16_t x, float y, bool params_first) {
    char* cursor = text;
    do {
        const size_t length = std::strlen(cursor);
        char* line = Font_WordWrapString(&cursor, uint16_t(length), format, width, nullptr);
        if (line == nullptr) break;
        sprite* s = Sprite_CreateLink(0x63);
        if (params_first) {
            Sprite_SetParams(s, 0, x, int16_t(int32_t(y)), 0, 0x7d6d59ffu);
            y += 15.0f;
            Sprite_SetTextNFmt(s, line, format, 0);
        } else {
            Sprite_SetTextNFmt(s, line, format, 0);
            Sprite_SetParams(s, 0, x, int16_t(int32_t(y)), 0, 0x7d6d59ffu);
            y += 15.0f;
        }
    } while (cursor != nullptr);
    return y;
}
}  // namespace

// [decomp] 0x001d5258 ResetMap_LevelCode2Img
// Por fase: a imagem, o nome da missão (mission) e uma dica ao acaso (tip) da tabela da fase; a maioria então sorteia
// de novo e, com o número par (ou sem dica), troca a dica por uma das 43 genéricas. As fases do multijogador têm a
// imagem 0x300016e e uma das 40 dicas delas; fora da tabela, 0x30000d7 sem textos. Os sorteios (Rand_Random, divu)
// são os do original, também os que não usam o valor (05, 07, 15, 4a).
// Comportamento original preservado: na 0700001b a dica sorteada vai para mission (sobrescrita logo depois pelo nome),
// então a dica é sempre uma genérica.
uint32_t ResetMap_LevelCode2Img(uint32_t level, char** mission, char** tip) {
    if (tip != nullptr) *tip = nullptr;
    if (mission != nullptr) *mission = nullptr;
    uint32_t image = 0x30000d2;
    const auto name = [&](uint32_t label) {
        if (mission != nullptr) *mission = Txt_BindLabel(label, 0);
    };
    const auto set_tip = [&](char* t) {
        if (tip != nullptr) *tip = t;
    };
    const auto fixed_tip = [&](uint32_t label) {
        if (tip == nullptr) return;
        Rand_Random();
        *tip = Txt_BindLabel(label, 0);
    };
    switch (level) {
        case 0x7000001: if (tip != nullptr) set_tip(pick(kTips01)); name(0x4000006); break;
        case 0x7000002: if (tip != nullptr) set_tip(pick(kTips02)); name(0x4000008); break;
        case 0x7000003: if (tip != nullptr) set_tip(pick(kTips03)); name(0x400000a); break;
        case 0x7000004: if (tip != nullptr) set_tip(pick(kTips04)); name(0x400000d); break;
        case 0x7000005:
            fixed_tip(0x1000126);
            name(0x4000000);
            return 0x30000d0;
        case 0x7000006: if (tip != nullptr) set_tip(pick(kTips06)); name(0x4000001); image = 0x30000d0; break;
        case 0x7000007: fixed_tip(0x10000c2); name(0x4000002); image = 0x30000d0; break;
        case 0x7000008: if (tip != nullptr) set_tip(pick(kTips08)); name(0x4000003); image = 0x30000d0; break;
        case 0x7000009: if (tip != nullptr) set_tip(pick(kTips09)); name(0x4000016); image = 0x30000d5; break;
        case 0x700000a: if (tip != nullptr) set_tip(pick(kTips0a)); name(0x4000019); image = 0x30000d5; break;
        case 0x700000b: if (tip != nullptr) set_tip(pick(kTips0b)); name(0x400001d); image = 0x30000d5; break;
        case 0x700000c: if (tip != nullptr) set_tip(pick(kTips0c)); name(0x400002c); image = 0x30000d3; break;
        case 0x700000d: if (tip != nullptr) set_tip(pick(kTips0d)); name(0x400002f); image = 0x30000d3; break;
        case 0x7000011: if (tip != nullptr) set_tip(pick(kTips11)); name(0x4000036); image = 0x30000d6; break;
        case 0x7000012: if (tip != nullptr) set_tip(pick(kTips12)); name(0x4000037); image = 0x30000d6; break;
        case 0x7000013: set_tip(nullptr); name(0x400003a); image = 0x30000d6; break;
        case 0x7000014: if (tip != nullptr) set_tip(pick(kTips14)); name(0x4000049); image = 0x30000d1; break;
        case 0x7000015: fixed_tip(0x10000ff); name(0x400004d); image = 0x30000d1; break;
        case 0x7000016: if (tip != nullptr) set_tip(pick(kTips16)); name(0x400004f); image = 0x30000d1; break;
        case 0x700001b:
            if (tip != nullptr) {
                char* t = pick(kTips1b);
                if (mission != nullptr) *mission = t;  // o original escreve em mission (sem conferir se veio)
            }
            name(0x400005a);
            image = 0x30000d4;
            break;
        case 0x7000021: case 0x7000022: case 0x7000023: case 0x7000024: case 0x7000025: case 0x7000026:
        case 0x7000027: case 0x7000028: case 0x7000029: case 0x700004b: case 0x700004c:
            if (tip != nullptr) set_tip(Txt_BindLabel(0x100013e + Rand_Random() % 0x28, 0));
            return 0x300016e;
        case 0x700004a:
            fixed_tip(0x10000fb);
            name(0x4000039);
            return 0x30000d6;
        default:
            return 0x30000d7;
    }
    // A dica genérica: com o sorteio par, ou sem dica da fase.
    const uint32_t r = Rand_Random();
    if ((r & 1) == 0 || (tip != nullptr && *tip == nullptr)) {
        if (tip != nullptr) set_tip(pick(kTipsGeneric));
    }
    return image;
}

// [decomp] 0x001d5de0 ResetMap_GenLoadScreen
// A tela de carga (lista de sprites 99, câmera 10): a imagem da fase (LoadingGfx.266 com a textura de
// ResetMap_LevelCode2Img, 512 x 448), o rótulo 0x3c1 quebrado em 480 pixels a partir de x = 248, y = 322.56 (formato
// 0x30d040), 14 pixels abaixo o nome da missão ("º<0x2000054>º <nome>") e 4 abaixo a dica sem a primeira palavra
// ("º<0x2000055>º <dica>"), ambos em 456 pixels a partir de x = 28 (formato 0x30d048), linhas de 15 em 15, cor
// 0x7d6d59ff. Desenha quatro quadros (psiPreDraw com PS2CacheLoadingBlobs, psiUseCamera(câmera 10), Sprite_BuildList
// e View_DrawSprites da lista inteira, psiPostDraw; GameState.draws + 1 cada) e começa a tela (psiStartLoadingScreen).
// Os sprites e as strings ficam no heap que o reinício seguinte apaga.
// Só no port: as linhas montadas com snprintf no tamanho dos buffers do StringHeap (o original usa sprintf), a
// primeira palavra da dica não passa do fim da string e sem a câmera 10 nada é desenhado.
void ResetMap_GenLoadScreen() {
    char* label = Txt_BindLabel(0x3c1, 0);
    char* mission = nullptr;
    char* tip = nullptr;
    LoadingGfx_266.hash = ResetMap_LevelCode2Img(GameState.load_file & 0xff00ffffu, &mission, &tip);
    Sprite_Create2(&LoadingGfx_266);
    char* mission_line = Txt_GetStringFromHeap(0);
    char* tip_line = Txt_GetStringFromHeap(0);
    if (mission != nullptr && mission_line != nullptr) {
        const char* head = Txt_BindLabel(0x2000054, 0);
        std::snprintf(mission_line, kStringHeapSize, kLoadLineFmt, 0xba, head != nullptr ? head : "", 0xba, mission);
    }
    if (tip != nullptr) {
        while (*tip != ' ' && *tip != '\0') ++tip;
        if (*tip == ' ') ++tip;
        const char* head = Txt_BindLabel(0x2000055, 0);
        if (tip_line != nullptr) {
            std::snprintf(tip_line, kStringHeapSize, kLoadLineFmt, 0xba, head != nullptr ? head : "", 0xba, tip);
        }
    }
    float y = 322.56f;
    if (label != nullptr) y = load_lines(label, LoadFmt_0030d040, 0x1e0, 0xf8, y, true);
    y += 14.0f;
    if (mission != nullptr && mission_line != nullptr) y = load_lines(mission_line, LoadFmt_0030d048, 0x1c8, 0x1c, y, false);
    y += 4.0f;
    if (tip != nullptr && tip_line != nullptr) load_lines(tip_line, LoadFmt_0030d048, 0x1c8, 0x1c, y, false);
    for (int pass = 0; pass < 4; ++pass) {
        uint32_t a = 0, b = 0, c = 0;
        ++GameState.draws;
        PS2CacheLoadingBlobs = 1;
        psiPreDraw();
        psiUseCamera(glb_viewer[10], 0);
        P32<sprite>* list = Sprite_BuildList(0x63, &a, &b, &c);
        if (glb_viewer[10] != nullptr) View_DrawSprites(glb_viewer[10], list, a, c);
        psiPostDraw();
    }
    psiStartLoadingScreen();
}

// [decomp] 0x001d6360 ResetMap_Load
void ResetMap_Load() {
    if (LoadState_270 == 1) {
        LoadState_270 = 2;
        return;
    }
    if (LoadState_270 == 0) {
        reset_all();
        if (GameState.load_file != kFrontEnd) {
            ResetMap_GameInit();
            StoreLevel_271 = GameState.load_file;
            GameState.load_file = kLoadScreen;
            load_pack(GameState.load_file);
            GameState.load_file = StoreLevel_271;
        }
        psiPreGameRun();
        if ((GameState.load_file & 0xff0000) != 0) {
            // Filme de fundo (bits 16-23 do código), opcionalmente com um script.
            if (DrawInfo[2] != 0) {
                const uint32_t script = movie_script(GameState.load_file);
                if (script != 0 && hashtable_getitem(script) != nullptr) {
                    SP_Create(const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR), const_cast<_VECTOR*>(&CONSTANT_ZERO_VECTOR),
                              script, 0, 1, 1, nullptr, nullptr, nullptr, 0);
                }
            }
            GameFlow_PushState(GF_MOVIE, 0.0f, 0xff);
            const uint32_t level = GameState.load_file & 0xff00ffff;
            GameState.movie_file = GameState.load_file;
            GameState.unknown_14 = 0;
            GameState.load_file = level;
            if (level == 0x7000018) GameState.load_file = 0x700001b;
            Mem_SetMallocMethod(MALLOC_QUICK);
            psiStartBackgroundMovie(GameState.movie_file, 0, SFXGetVolume());
            LoadState_270 = 1;
            return;
        }
    } else if (LoadState_270 == 2) {
        const uint32_t level = GetLevelToLoadFromMovieCode(GameState.movie_file);
        GameState.load_file = level;
        if (level == kFrontEnd) GameState.menu_page = 0x40000036;
        GameState.level_file = GameState.load_file;
        if ((level & 0xff0000) != 0) {
            LoadState_270 = 0;
            return;
        }
    } else if (LoadState_270 == 3) {
        if (GameState.load_file != kFrontEnd && g_port_no_load_screen == 0) ResetMap_GenLoadScreen();
        LoadableIndex = 0;
        psiInitTimeIn100ths();
        reset_all();
        ResetMap_GameInit();
        PS2_Sound_Ready = 1;
        AnimSeqGetHdrBuf(1);
        load_pack(GameState.load_file);
        LoadState_270 = 4;
        build_MakeCelOverlapBox(glb_world);
        build_link_objects_to_rooms(glb_world->first);
        parsemap_reset_to_block_memory_discard();
        parsemap_block_map_data_dynamic(DynamicBH, DynamicPtr, 1);
        build_LinkDoors2Portals(glb_world->first, control_first_object());
        AIPath_BindNodes();
        void* discard = FileDiscard;
        Mem_Shrink(&discard, uint32_t(FileLastBlock - FileDiscard));
        FileDiscard = static_cast<uint8_t*>(discard);
        bLevelLoaded = 1;
        ReadTuningVars();
        Player_Start();
        MP_Start();
        Drone_PostLoad_Init();
        MP_PostLoad_Init();
        Cameras_PostLoad();
        psiPreGameRun();
        if (GameState.level_file == kFrontEnd) {
            MenuManager_Create(GameState.menu_id, GameState.menu_page, -1, 0, 0x28, 0, 5);
        }
        Mem_SetMallocMethod(MALLOC_QUICK);
        hashtable_create_sections();
        GameFlow_PopState();
        GameFlow_PushState(GF_LOCKS_INIT, 0.0f, 0xff);
        LoadState_270 = 0;
        PS2_SFXWaitTillFinished();
        psiEndLoadingScreen();
        return;
    } else {
        return;
    }
    LoadState_270 = 3;
}

// [decomp] 0x001d3800 ResetMap_LevelToLoad
void ResetMap_LevelToLoad(uint32_t level, int8_t warm, int8_t no_movie) {
    if (level == 0xffffffff) return;
    IsWarmReset = uint8_t(warm);
    switch (GameFlow_GetState()) {
        case GF_WAIT_START:
        case GF_FADE_REQUEST:
        case GF_FADE_WAIT:
        case GF_CAMERA_REQUEST:
        case GF_CAMERA_ONLY:
            break;
        default:
            return;
    }
    // Só no port: conta os recomeços (a mesma fase de novo, pelos menus de pausa e de fim de missão).
    if (level == GameState.level_file) ++g_mission_port.restarts;
    GameState.skip_draw = 1;
    GameState.multiplayer = 0;
    if (Menu_IsDrivingLevel(level) != 0) {
        GameFlow_PushState(GF_LAUNCH_DRIVING, 0.0f, 0xff);
        GameState.load_file = level;
        return;
    }
    // Fases do multijogador: 07000021-07000029 e 0700004b-0700004c.
    if (0x7000020 < level && (level < 0x700002a || (level < 0x700004d && 0x700004a < level))) {
        GameState.multiplayer = 1;
        MP_setLoadingSkins();
    }
    if (no_movie == 0) {
        if (0x7000001 <= level && level <= 0x7000016) {
            level += 0x100000;
        } else if (level == 0x700001b) {
            level = 0x7100018;
        } else if (level == 0x700004a) {
            level = 0x710004a;
        }
    }
    GameState.load_file = level;
    GameFlow_PushState(GF_LOAD_MAP, 0.0f, 0xff);
}

namespace {

// Os 32 valores dos personagens que todo caso do switch do ReadTuningVars grava (DroneDamage_*, DroneArmour_*,
// DroneFiring_*, DroneCaptain_*). O original grava cada caso por inteiro, com as constantes em registradores; a
// ordem dos stores não importa (nada os lê no meio).
struct DroneTuning {
    float damage_easy, damage_normal, damage_hard;  // DroneDamage_Easy/Normal/Hard (o dano que o jogador causa)
    float head, legs, arms, torso;                   // DroneDamage_Head/Legs/Arms/Torso
    float helmet, combat, jacket, vest;              // DroneArmour_*
    DroneFiringSettings firing;                      // DroneFiring_BurstDelay_Min .. DroneFiring_PlayerBackShot_Damage
    float captain_damage, captain_accuracy, captain_health;  // DroneCaptain_Mod_*
};

// O conjunto que quase todo caso grava (Henderson, 07000001-04, e as bases de 07000014-16); os outros mudam poucos.
constexpr DroneTuning kStandardTuning = {
    1.5f, 1.0f, 0.8f,
    10.0f, 0.75f, 1.0f, 1.0f,
    1.0f, 0.25f, 0.5f, 0.75f,
    {15.0f, 45.0f, 60.0f, 4.0f, 30.0f,  // pausa entre rajadas: mínima, normal, máxima, distâncias
     0.5f, 0.7f, 1.0f,                   // precisão por dificuldade
     2.0f, 2.0f, 0.25f, 0.75f, 2.0f, 1.0f,  // tempos e precisões de alvo novo, em movimento, parado
     3.0f, 2.0f, 2.0f, 2.0f},            // de perto (distância, precisão, dano) e pelas costas
    2.0f, 2.0f, 2.0f,
};

void set_drone_tuning(const DroneTuning& t) {
    DroneDamage_Easy = t.damage_easy;
    DroneDamage_Normal = t.damage_normal;
    DroneDamage_Hard = t.damage_hard;
    DroneDamage_Head = t.head;
    DroneDamage_Legs = t.legs;
    DroneDamage_Arms = t.arms;
    DroneDamage_Torso = t.torso;
    DroneArmour_Helmet = t.helmet;
    DroneArmour_Combat = t.combat;
    DroneArmour_Jacket = t.jacket;
    DroneArmour_Vest = t.vest;
    DroneFiring = t.firing;
    DroneCaptain_Mod_BulletDamage = t.captain_damage;
    DroneCaptain_Mod_BulletAccuracy = t.captain_accuracy;
    DroneCaptain_Mod_Health = t.captain_health;
}

void set_drone_accuracy(DroneTuning& t, float easy, float normal, float hard) {
    t.firing.accuracy_easy = easy;
    t.firing.accuracy_normal = normal;
    t.firing.accuracy_hard = hard;
}

// O dano que o jogador recebe por dificuldade (Plr_DMod_Easy/Normal/Hard, lidos por Player_HandlePain).
void set_player_damage(float easy, float normal, float hard) {
    Plr_DMod_Easy = easy;
    Plr_DMod_Normal = normal;
    Plr_DMod_Hard = hard;
}

}  // namespace

// [decomp] 0x001d3a80 ReadTuningVars
// O ajuste da fase, a cada carga (ResetMap_Load, depois de criar os objetos). Apesar do nome, não lê arquivo
// nenhum: tudo são constantes do código. Sem condição: a velocidade da mira e do giro do jogador, a vida mínima ao
// recomeçar (50) e a mira automática (cones de 0,12 x 0,22 rad, alcance 25, lock-on x 1,4, fáceis x 1,9, 00 Agent
// x 0). Depois um switch por GameState.level_file (a tabela de saltos 0x00305160 cobre 07000001-0700004c) com os
// valores dos personagens e o dano que o jogador recebe, por grupo de fases; as fases fora dele (as cenas, o menu
// principal, as de teste) ficam com os valores da carga anterior (os do .data só antes da primeira carga). As fases do
// multijogador (07000021-29, 4b, 4c) gravam o dano por parte do corpo (Plr_DMod_Multi/Head/LowerLimb/UpperLimb) e
// não o por dificuldade. Cada valor foi conferido na desmontagem (e o nf_loader_check roda o ReadTuningVars do ELF no
// interpretador MIPS para cada fase e compara as 65 globais com este); a divisão em "conjunto padrão + diferenças" é
// a do ReadTuningVars/SetStandardDroneTuning do nightfire-cxbx (NightfireResearch), que bate com os stores do PS2.
void ReadTuningVars() {
    Plr_AimSpeed_X = 0.1275f;
    Plr_AimSpeed_Y = 0.136f;
    Plr_AimTurnSpeed_X = 0.05f;
    Plr_AimTurnSpeed_Y = 0.06f;
    Plr_ScopeSpeed_X = 0.015f;
    Plr_ScopeSpeed_Y = 0.011f;
    Plr_ScopeSpeed_Y_Mul = 3.0f;
    Plr_NoAimTurnSpeed_X = 0.04f;
    Plr_NoAimTurnSpeed_Y = 0.01f;
    Plr_NoAimTurnSpeed_Y_Mul = 2.0f;
    Plr_NoAimTurnSpeed_Y_Steps = 120.0f;
    ContinueHealthBoostHard = 50.0f;
    Autoaim_Angle_H = 0.12f;
    Autoaim_Angle_V = 0.22f;
    Autoaim_Range = 25.0f;
    Autoaim_LockOnMul = 1.4f;
    Autoaim_EasyMul = 1.9f;
    Autoaim_NormalMul = 1.0f;
    Plr_ScopeSpeed_X_Mul = 3.0f;
    Plr_ScopeSpeed_X_Steps = 120.0f;
    Plr_ScopeSpeed_Y_Steps = 120.0f;
    Plr_NoAimTurnSpeed_X_Mul = 2.0f;
    Plr_NoAimTurnSpeed_X_Steps = 120.0f;
    ContinueHealthBoostEasy = 50.0f;
    ContinueHealthBoostMedium = 50.0f;
    Autoaim_HardMul = 0.0f;

    DroneTuning t = kStandardTuning;
    switch (GameState.level_file) {
        case 0x7000001: case 0x7000002: case 0x7000003: case 0x7000004:  // Henderson
            set_drone_tuning(t);
            set_player_damage(0.5f, 0.7f, 1.2f);
            break;
        case 0x7000005: case 0x7000006: case 0x7000007: case 0x7000008:  // o castelo
            set_drone_accuracy(t, 0.8f, 0.9f, 1.3f);
            set_drone_tuning(t);
            set_player_damage(0.6f, 0.7f, 1.0f);
            break;
        case 0x7000009: case 0x700000a: case 0x700000b:  // a torre
            set_drone_accuracy(t, 0.8f, 1.0f, 1.0f);
            set_drone_tuning(t);
            set_player_damage(0.6f, 0.8f, 1.0f);
            break;
        case 0x700000c: case 0x700000d:  // a usina
            set_drone_accuracy(t, 0.6f, 0.8f, 1.2f);
            t.firing.target_first_stopped_time_to_hit = 1.0f;
            t.firing.target_first_stopped_accuracy = 0.1f;
            t.captain_damage = 1.5f;
            t.captain_accuracy = 1.5f;
            set_drone_tuning(t);
            set_player_damage(0.4f, 0.7f, 1.2f);
            break;
        case 0x7000011: case 0x7000012: case 0x7000013: case 0x700004a:  // a segunda torre e o elevador
            t.damage_normal = 1.2f;
            t.damage_hard = 1.0f;
            set_drone_accuracy(t, 0.7f, 0.8f, 1.0f);
            set_drone_tuning(t);
            set_player_damage(0.5f, 0.6f, 1.0f);
            break;
        case 0x7000014: case 0x7000015: case 0x7000016:  // a base
            set_drone_tuning(t);
            set_player_damage(0.4f, 0.6f, 1.0f);
            break;
        case 0x700001b:  // a estação espacial
            t.damage_easy = 1.0f;
            t.damage_normal = 1.0f;
            t.damage_hard = 1.0f;
            set_drone_accuracy(t, 0.6f, 0.8f, 1.2f);
            set_drone_tuning(t);
            set_player_damage(0.6f, 0.7f, 1.0f);
            break;
        case 0x7000021: case 0x7000022: case 0x7000023: case 0x7000024: case 0x7000025: case 0x7000026:
        case 0x7000027: case 0x7000028: case 0x7000029: case 0x700004b: case 0x700004c:  // o multijogador
            t.damage_easy = 2.0f;
            t.damage_normal = 1.5f;
            t.damage_hard = 0.5f;
            t.helmet = 0.5f;
            set_drone_accuracy(t, 0.8f, 1.0f, 1.5f);
            t.firing.too_close_distance = 4.0f;
            set_drone_tuning(t);
            Plr_DMod_Multi = 4.0f;
            Plr_DMod_Head = 4.0f;
            Plr_DMod_LowerLimb = 0.8f;
            Plr_DMod_UpperLimb = 0.8f;
            break;
        default:
            break;
    }
}

// [decomp] 0x001d6958 Reset_MapLoadSettings
void Reset_MapLoadSettings() {
    if (GameState.load_file == 0) GameState.load_file = kFrontEnd;
    ResetMap_LevelToLoad(GameState.load_file, 0, 0);
}

// [decomp] 0x001d3560 GetLevelToLoadFromMovieCode
// Fase que vem depois de cada filme (0x07000048 é o menu). Sem entrada: o pacote atual sem a
// seção de filme.
uint32_t GetLevelToLoadFromMovieCode(uint32_t movie) {
    switch (movie) {
        case 0x7200001: return 0x7100002;
        case 0x7200002: return 0x7100003;
        case 0x7200003: return 0x7100004;
        case 0x7200005: return 0x7100006;
        case 0x7200006: return 0x7100007;
        case 0x7200007: return 0x7100008;
        case 0x7200009: return 0x710000a;
        case 0x720000a: return 0x710000b;
        case 0x720000c: return 0x710000d;
        case 0x720000e: return 0x710000f;
        case 0x7200011: return 0x7100012;
        case 0x7200012: return 0x710004a;
        case 0x7200014: return 0x7100015;
        case 0x7200015: return 0x7100016;
        case 0x720004a: return 0x7100013;
        case 0x7300004:
        case 0x7300008:
        case 0x730000b:
        case 0x730000d:
        case 0x730000f:
        case 0x7300010:
        case 0x7300013:
        case 0x7300017:
        case 0x730001b: return 0x7000048;
        default: return GameState.load_file & 0xff00ffff;
    }
}
