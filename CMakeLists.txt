cmake_minimum_required(VERSION 3.24)
project(NightfirePort LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 32 bits (x86), como o PS2, é a versão de referência: as estruturas do jogo guardam ponteiros de 4 bytes, inclusive
# dentro dos dados carregados, e o código decompilado depende desse layout. A versão de 64 bits (NF_ARCH64, build.bat
# Release x64) mantém o mesmo layout: os campos de ponteiro do jogo são deslocamentos de 32 bits dentro de uma janela de
# 4 GB que contém a imagem do programa e a arena (platform/ptr32.h, platform/arena.h; docs/port.md, "64 bits").
option(NF_ARCH64 "Compila a versão de 64 bits (x64) com ponteiros de 32 bits nas estruturas do jogo" OFF)
# A prova da separação entre o código neutro e os backends (docs/port.md, "Plataforma: interfaces e backends"): as
# bibliotecas neutras, o nf_tests e as ferramentas de conferência de console com os backends nulos (os_null.cpp,
# backends_null.cpp), sem nenhuma API do Windows nem as bibliotecas dele. Sem janela nem renderizador: o nightfire.exe e
# o nf_driving.exe não entram. No Windows só em 32 bits (a arena de 64 bits precisa de um backend de verdade). build.bat
# Release headless compila em build\headless\Release.
option(NF_HEADLESS "Só o código neutro e os testes, com os backends nulos (sem API do sistema)" OFF)

# Fora do Windows (etapa 3 do macOS; docs/port.md, "macOS: compilação e CI"): por enquanto só a build headless, em 64
# bits (o macOS só roda 64 bits), com o backend POSIX do sistema operacional (os_posix.cpp: a arena e a pilha do jogo de
# verdade) e os nulos no resto. Compilada pelo clang da Apple e pelo GCC do Linux na CI (.github/workflows/portable.yml).
if(NOT WIN32)
    if(NOT NF_HEADLESS)
        message(FATAL_ERROR "Fora do Windows, por enquanto só a build headless: -DNF_HEADLESS=ON (a janela e o renderizador do macOS são a etapa 3c).")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Fora do Windows o port compila em 64 bits (com os ponteiros de 32 bits das estruturas do jogo).")
    endif()
    set(NF_ARCH64 ON)
elseif(NF_HEADLESS AND NF_ARCH64)
    message(FATAL_ERROR "NF_HEADLESS no Windows é só de 32 bits (a arena da versão de 64 bits precisa de um backend do sistema).")
endif()
if(NF_ARCH64)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "NF_ARCH64 pede um compilador de 64 bits (x64). Use build.bat Release x64, que chama o vcvars64.")
    endif()
    # A verificação dos ponteiros na janela (NF_P32_CHECK): um ponteiro nativo fora dela guardado num campo do jogo
    # para o programa com a pilha de chamadas. Barata (uma comparação por escrita de ponteiro); ligada por padrão.
    option(NF_P32_CHECK "x64: confere cada ponteiro guardado num campo de 32 bits" ON)
    if(MSVC)
        enable_language(ASM_MASM)
        # Um só C runtime (estático): o operator new do port (platform/arena_new.cpp) serve também a biblioteca padrão.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_compile_definitions(NF_P32_CHECK=$<BOOL:${NF_P32_CHECK}>)
    if(MSVC)
        # Um ponteiro truncado para 32 bits (ou um inteiro de 32 bits virando ponteiro) é erro: todo campo de ponteiro
        # do jogo passa por nf::P32/nf::p32_u/nf::p32_ptr (platform/ptr32.h).
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/we4311> $<$<COMPILE_LANGUAGE:CXX>:/we4312>
                            $<$<COMPILE_LANGUAGE:CXX>:/we4302> $<$<COMPILE_LANGUAGE:CXX>:/diagnostics:column>)
    endif()
elseif(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "O port compila em 32 bits (x86), ou em 64 bits com NF_ARCH64. Use build.bat, que chama o vcvarsamd64_x86 (ou o vcvars64 com x64).")
else()
    add_link_options(/LARGEADDRESSAWARE)
endif()
if(MSVC)
    # PDB também no Release, para o relatório de falhas (platform/crash_report.h) nomear as funções.
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG:FULL> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    # O executável guarda só o nome do PDB, sem a pasta da compilação (o zip de lançamento não leva caminhos desta
    # máquina); o relatório de falhas o procura ao lado do executável.
    add_link_options($<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>)

    set(NF_DEFINES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set(NF_OPTIONS /W4 /utf-8 /permissive-)
else()
    # O clang (o da Apple no macOS) e o GCC (o Linux da CI). O C++20 sem as extensões do GNU, e o código compartilhado
    # com a semântica que ele tem no MSVC, que é a referência:
    #  -ffp-contract=off: nada de juntar a * b + c numa FMA (o ARM64 junta por padrão; o MSVC não junta sem
    #    /fp:contract): a física das missões de carro e a pontuação são conferidas bit a bit;
    #  -fno-strict-aliasing: o código decompilado lê um float como uint32_t e campos por deslocamento (o MSVC não
    #    otimiza por tipo);
    #  -fwrapv: conta de inteiro com sinal que estoura dá a volta, como no MIPS e no MSVC (os geradores aleatórios, os
    #    hashes);
    #  -fno-delete-null-pointer-checks: um teste de nulo depois de um acesso fica (o código decompilado tem disso).
    # -g1: as linhas para o atos/addr2line sobre os endereços do relatório de falhas (crash_report_posix.cpp). O Release
    # em -O2 (o -O3 do CMake compila bem mais devagar e não muda nada que importe aqui).
    set(CMAKE_CXX_EXTENSIONS OFF)
    string(REPLACE "-O3" "-O2" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    set(NF_DEFINES)
    # Um texto literal virando char* é erro: com um P32 do outro lado de um ?:, o GCC e o clang convertiam o literal
    # num P32 (e um printf recebia os 4 bytes do campo no lugar do texto).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(NF_OPTIONS -Wall -Wno-unknown-pragmas -Werror=writable-strings)
    else()
        set(NF_OPTIONS -Wall -Wno-unknown-pragmas -Werror=write-strings)
    endif()
    add_compile_options(-ffp-contract=off -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks -g1)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Os nomes das funções do executável na pilha do backtrace (crash_report_posix.cpp).
        add_link_options(-rdynamic)
    endif()
endif()

# Interface de plataforma psi* para PC (relógio, heap, cópias) e as interfaces do sistema operacional: o código neutro
# aqui, os backends de cada sistema logo abaixo (docs/port.md, "Plataforma: interfaces e backends"; tools/check_platform.py
# confere que nada do sistema aparece fora deles).
add_library(nf_platform STATIC
    src/platform/pending.cpp
    src/platform/psi.cpp
    src/platform/psi_file.cpp
    src/platform/disc_source.cpp
    src/platform/disc_pack.cpp
    src/platform/psi_card.cpp
    src/platform/psi_gfx.cpp
    src/platform/psi_draw.cpp
    src/platform/psi_shard.cpp
    src/platform/psi_2d.cpp
    src/platform/psi_input.cpp
    src/platform/psi_pad.cpp
    src/platform/gamepad.cpp
    src/platform/input_bindings.cpp
    src/platform/psi_loading.cpp
    src/platform/psi_stubs.cpp
    src/platform/psi_movie.cpp
    src/platform/pss.cpp
    src/platform/movie_decoder.cpp
    src/platform/mpeg2_decoder.cpp
    src/platform/psi_sound.cpp
    src/platform/sfx_iop.cpp
    src/platform/spu2.cpp
    src/platform/audio_out.cpp
    src/platform/game_options.cpp
    src/platform/pc_gameplay.cpp
    src/platform/settings.cpp
    src/platform/video_settings.cpp
    src/platform/gpu_adapter.cpp
    src/platform/bench_stats.cpp
    src/platform/vu1.cpp
    src/platform/ps2_float.cpp
    src/platform/vu_interp.cpp
    src/core/json.cpp
)
target_include_directories(nf_platform PUBLIC src)
# Os backends do Windows (Win32, Raw Input, XInput, XAudio2, Media Foundation, WIC, dbghelp, IFileOpenDialog): as mesmas
# APIs de antes, atrás das interfaces de src/platform. Com NF_HEADLESS, os nulos; fora do Windows (só headless, por
# enquanto) o sistema operacional e o relatório de falhas são os do POSIX (macOS e Linux) e o resto é nulo.
if(NF_HEADLESS AND WIN32)
    target_sources(nf_platform PRIVATE src/platform/os_null.cpp src/platform/backends_null.cpp
                                       src/platform/crash_report_null.cpp)
elseif(NF_HEADLESS)
    target_sources(nf_platform PRIVATE src/platform/os_posix.cpp src/platform/backends_null.cpp
                                       src/platform/crash_report_posix.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(nf_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
elseif(WIN32)
    target_sources(nf_platform PRIVATE
        src/platform/os_win32.cpp
        src/platform/window_win32.cpp
        src/platform/file_dialog_win32.cpp
        src/platform/gamepad_xinput.cpp
        src/platform/audio_device_xaudio2.cpp
        src/platform/movie_decoder_mf.cpp
        src/platform/image_io_wic.cpp
        src/platform/crash_report_win32.cpp
    )
    target_link_libraries(nf_platform PUBLIC dbghelp xinput xaudio2 ole32 shell32 mfplat mfuuid windowscodecs psapi)
    # O ponto de entrada dos programas de janela (platform/entry.h): entra direto em cada executável WIN32.
    set(NF_WIN_ENTRY src/platform/entry_win32.cpp)
    # O renderizador do Windows: Direct3D 11, o Direct2D e o DirectWrite das camadas 2D, o quadro dos filmes, os
    # adaptadores pelo DXGI e o caminho de Direct3D 9 para o RTX Remix (a fachada neutra é render/renderer.h).
    set(NF_RENDER_BACKEND
        src/render/renderer_d3d11.cpp
        src/render/movie_layer_d3d11.cpp
        src/render/ui_hd_draw_d2d.cpp
        src/render/ui_modern_draw_d2d.cpp
        src/render/gpu_adapters_dxgi.cpp
        src/render/remix_d3d9.cpp
    )
    set(NF_RENDER_LIBS d3d11 dxgi d3dcompiler d2d1 dwrite shlwapi)
endif()
# A versão de 64 bits: a arena (a janela dos ponteiros de 32 bits do jogo) e a troca de pilha; o operator new dela entra
# direto em cada executável (NF_EXE_SOURCES).
set(NF_EXE_SOURCES)
if(NF_ARCH64)
    target_sources(nf_platform PRIVATE src/platform/arena.cpp)
    if(WIN32)
        # A troca de pilha do jogo: os limites no TEB, o MASM e o SEH do Windows.
        target_sources(nf_platform PRIVATE src/platform/arena_win32.cpp src/platform/arena_stack_x64.asm)
    else()
        # A pilha do jogo numa thread com pthread_attr_setstack.
        target_sources(nf_platform PRIVATE src/platform/arena_posix.cpp)
    endif()
    set(NF_EXE_SOURCES src/platform/arena_new.cpp)
endif()
target_compile_definitions(nf_platform PUBLIC ${NF_DEFINES})
target_compile_options(nf_platform PRIVATE ${NF_OPTIONS})

# Código portado do ACTION.ELF (independente de plataforma, testável sem janela nem dados).
add_library(nf_engine STATIC
    src/engine/vecmath.cpp
    src/engine/hashtable.cpp
    src/engine/gameflow.cpp
    src/engine/memory.cpp
    src/engine/build.cpp
    src/engine/loader.cpp
    src/engine/parsemap.cpp
    src/engine/modules_stubs.cpp
    src/engine/resetmap.cpp
    src/engine/llist.cpp
    src/engine/control.cpp
    src/engine/control_funcs.cpp
    src/engine/objects.cpp
    src/engine/pickup.cpp
    src/engine/sort.cpp
    src/engine/collide.cpp
    src/engine/bullet.cpp
    src/engine/view.cpp
    src/engine/player.cpp
    src/engine/player_weapon.cpp
    src/engine/player_ads.cpp
    src/engine/weapon_wheel.cpp
    src/engine/player_stats.cpp
    src/engine/player_traverse.cpp
    src/engine/ssys.cpp
    src/engine/sensor.cpp
    src/engine/searchlight.cpp
    src/engine/hazard.cpp
    src/engine/weather.cpp
    src/engine/scenery.cpp
    src/engine/gunimp.cpp
    src/engine/space_station.cpp
    src/engine/turret.cpp
    src/engine/copter.cpp
    src/engine/simple_script.cpp
    src/engine/rigid_body.cpp
    src/engine/effect.cpp
    src/engine/emitter.cpp
    src/engine/spline.cpp
    src/engine/door.cpp
    src/engine/game.cpp
    src/engine/lights.cpp
    src/engine/trigger.cpp
    src/engine/input.cpp
    src/engine/anim.cpp
    src/engine/weapons.cpp
    src/engine/code_resolve.cpp
    src/engine/drone.cpp
    src/engine/drone_anim.cpp
    src/engine/drone_behaviour.cpp
    src/engine/drone_combat.cpp
    src/engine/drone_damage.cpp
    src/engine/drone_func.cpp
    src/engine/drone_init.cpp
    src/engine/drone_move.cpp
    src/engine/drone_route.cpp
    src/engine/drone_sm.cpp
    src/engine/drone_states.cpp
    src/engine/drone_stubs.cpp
    src/engine/drone_tables.cpp
    src/engine/drone_vision.cpp
    src/engine/drone_weapon.cpp
    src/engine/drone_spawner.cpp
    src/engine/aipath.cpp
    src/engine/ainetwork.cpp
    src/engine/script.cpp
    src/engine/script_player.cpp
    src/engine/script_support.cpp
    src/engine/drone_script.cpp
    src/engine/break.cpp
    src/engine/explode.cpp
    src/engine/txt.cpp
    src/engine/font.cpp
    src/engine/sprite.cpp
    src/engine/text.cpp
    src/engine/hud.cpp
    src/engine/mission.cpp
    src/engine/menu.cpp
    src/engine/menu_manager.cpp
    src/engine/menu_script.cpp
    src/engine/menu_controls.cpp
    src/engine/menu_handlers.cpp
    src/engine/menu_frontend.cpp
    src/engine/menu_codename.cpp
    src/engine/menu_options.cpp
    src/engine/menu_secret.cpp
    src/engine/menu_tweaks.cpp
    src/engine/menu_dossier.cpp
    src/engine/menu_cheats.cpp
    src/engine/loadsave.cpp
    src/engine/menu_controller_style.cpp
    src/engine/menu_pc_controls.cpp
    src/engine/menu_pc_options.cpp
    src/engine/menu_mouse.cpp
    src/engine/menu_modern.cpp
    src/engine/pc_text.cpp
    src/engine/sound.cpp
    src/engine/music.cpp
)
target_link_libraries(nf_engine PUBLIC nf_platform)
target_compile_options(nf_engine PRIVATE ${NF_OPTIONS})
if(NOT MSVC)
    # A pontuação conta com o arredondamento do EE (nf::RoundTowardZero, platform/compiler.h): o GCC ignora o
    # #pragma STDC FENV_ACCESS, e -frounding-math faz o papel dele (nada de dobrar contas supondo o modo padrão).
    set_source_files_properties(src/engine/player_stats.cpp tests/test_determinism.cpp
                                PROPERTIES COMPILE_OPTIONS -frounding-math)
endif()

# Leitura dos dados do jogo e montagem das fases.
add_library(nf_data STATIC
    src/core/binary.cpp
    src/core/xxh3.cpp
    src/data/elf.cpp
    src/data/mips_interp.cpp
    src/data/files_bin.cpp
    src/data/level_pak.cpp
    src/data/map_item.cpp
    src/data/pack_images.cpp
    src/data/texture_pack.cpp
    src/game/level_scene.cpp
    src/game/boot.cpp
    src/game/static_init.cpp
    src/game/hud_tables.cpp
    src/game/menu_tables.cpp
)
target_link_libraries(nf_data PUBLIC nf_engine)
target_compile_options(nf_data PRIVATE ${NF_OPTIONS})

# Camada 2D em alta definição: a parte sem GPU (configuração, textos, âncoras, substitutas), testável.
add_library(nf_ui_hd STATIC src/render/ui_hd.cpp)
target_link_libraries(nf_ui_hd PUBLIC nf_platform)
target_compile_options(nf_ui_hd PRIVATE ${NF_OPTIONS})

# O estilo de menu Modern (só do port): tokens, componentes e telas sem GPU (testáveis); o desenho com
# Direct2D fica no executável (render/ui_modern_draw_d2d.cpp).
add_library(nf_ui_modern STATIC
    src/ui/modern_ui.cpp
    src/ui/modern_kit.cpp
    src/ui/modern_options.cpp
    src/ui/modern_input.cpp
    src/ui/modern_pause.cpp
    src/ui/modern_end.cpp
    src/ui/modern_front.cpp
    src/ui/modern_dossier.cpp
    src/ui/modern_wheel.cpp
    src/ui/modern_setup.cpp
)
target_link_libraries(nf_ui_modern PUBLIC nf_engine nf_ui_hd)
target_compile_options(nf_ui_modern PRIVATE ${NF_OPTIONS})

# A interpolação entre ticks (só do port; render/frame_interp.h): as contas e os registros, sem GPU (testáveis); e os
# marcadores do quadro (render/frame_markers.h: a névoa e a limpeza do Z na lista de itens).
add_library(nf_frame_interp STATIC src/render/frame_interp.cpp src/render/frame_markers.cpp)
target_link_libraries(nf_frame_interp PUBLIC nf_data)
target_compile_options(nf_frame_interp PRIVATE ${NF_OPTIONS})

# As missões de carro (DRIVING.ELF, o motor da EA; docs/driving.md, src/driving/drv.h): uma biblioteca à parte, no
# namespace drv, sem o motor da Eurocom. Os .cpp de src/driving entram sozinhos (o módulo cresce em paralelo).
file(GLOB_RECURSE NF_DRV_SOURCES CONFIGURE_DEPENDS src/driving/*.cpp)
add_library(nf_drv STATIC ${NF_DRV_SOURCES})
target_link_libraries(nf_drv PUBLIC nf_platform)
target_compile_options(nf_drv PRIVATE ${NF_OPTIONS})

# A ferramenta de desenvolvimento do render das missões de carro (docs/driving.md seção 10): um modelo do EAGL
# girando numa janela, as capturas (--screenshot), o --check de todos os carros e o --census dos render methods.
if(NOT NF_HEADLESS)
add_executable(nf_driving WIN32
    src/driving_app/nf_driving.cpp
    src/driving_app/drv_renderer_d3d11.cpp
    src/render/gpu_adapters_dxgi.cpp
    ${NF_WIN_ENTRY}
    ${NF_EXE_SOURCES}
)
target_link_libraries(nf_driving PRIVATE nf_drv d3d11 dxgi d3dcompiler)
target_compile_options(nf_driving PRIVATE ${NF_OPTIONS})
endif()

# A versão do port (o commit na hora do cmake) na barra de baixo dos menus Modern.
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE NF_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT NF_GIT_HASH)
    set(NF_GIT_HASH "dev")
endif()
# Para comparar capturas entre builds de commits diferentes: o hash fixo na barra dos menus Modern.
if(NF_BUILD_HASH)
    set(NF_GIT_HASH "${NF_BUILD_HASH}")
endif()

# O nightfire.exe: o laço neutro (src/app), a ligação das estruturas do jogo com o renderizador e as telas (src/render,
# sem a API gráfica), o backend do renderizador e o ponto de entrada do sistema.
if(NOT NF_HEADLESS)
add_executable(nightfire WIN32
    src/app/app_main.cpp
    src/render/runtime_gfx.cpp
    src/render/ui_hd_app.cpp
    src/render/ui_hd_game.cpp
    src/render/ui_modern_app.cpp
    src/render/setup_app.cpp
    ${NF_RENDER_BACKEND}
    ${NF_WIN_ENTRY}
    ${NF_EXE_SOURCES}
)
target_link_libraries(nightfire PRIVATE nf_data nf_ui_hd nf_ui_modern nf_frame_interp ${NF_RENDER_LIBS})
target_compile_definitions(nightfire PRIVATE NF_PORT_BUILD="${NF_GIT_HASH}")
# A versão de 64 bits grava nightfire64.pdb: no zip de lançamento (tools/make_release.py) as duas ficam na mesma pasta,
# nightfire.exe com nightfire.pdb e nightfire64.exe com nightfire64.pdb (o relatório de falhas acha o PDB pelo nome).
if(NF_ARCH64)
    set_target_properties(nightfire PROPERTIES PDB_NAME nightfire64)
endif()
target_compile_options(nightfire PRIVATE ${NF_OPTIONS})
endif()

enable_testing()
add_executable(nf_tests
    tests/test_main.cpp
    tests/test_settings.cpp
    tests/test_bench.cpp
    tests/test_vecmath.cpp
    tests/test_vecmath2.cpp
    tests/test_hashtable.cpp
    tests/test_gameflow.cpp
    tests/test_memory.cpp
    tests/test_loader.cpp
    tests/test_psi_gfx.cpp
    tests/test_objects.cpp
    tests/test_collide.cpp
    tests/test_bullet.cpp
    tests/test_view.cpp
    tests/test_door.cpp
    tests/test_game.cpp
    tests/test_texture_pack.cpp
    tests/test_player.cpp
    tests/test_traverse.cpp
    tests/test_ssys.cpp
    tests/test_turret.cpp
    tests/test_copter.cpp
    tests/test_security.cpp
    tests/test_presentation.cpp
    tests/test_simple_script.cpp
    tests/test_rigid_body.cpp
    tests/test_aim.cpp
    tests/test_anim.cpp
    tests/test_face.cpp
    tests/test_weapon.cpp
    tests/test_ads_wheel.cpp
    tests/test_effect.cpp
    tests/test_lights.cpp
    tests/test_drone.cpp
    tests/test_drone_body.cpp
    tests/test_drone_combat.cpp
    tests/test_drone_vision.cpp
    tests/test_drone_weapon.cpp
    tests/test_drone_spawner.cpp
    tests/test_drone_talk.cpp
    tests/test_ainetwork.cpp
    tests/test_trigger.cpp
    tests/test_pickup.cpp
    tests/test_player_stats.cpp
    tests/test_emitter.cpp
    tests/test_gs_state.cpp
    tests/test_skin.cpp
    tests/test_mips.cpp
    tests/test_vu1.cpp
    tests/test_vu_interp.cpp
    tests/test_script.cpp
    tests/test_break.cpp
    tests/test_explode.cpp
    tests/test_text.cpp
    tests/test_sprite.cpp
    tests/test_hud.cpp
    tests/test_menu.cpp
    tests/test_input.cpp
    tests/test_pc_options.cpp
    tests/test_ui_hd.cpp
    tests/test_modern_ui.cpp
    tests/test_modern_menus.cpp
    tests/test_frontend.cpp
    tests/test_modern_front.cpp
    tests/test_modern_dossier.cpp
    tests/test_options_pages.cpp
    tests/test_movie.cpp
    tests/test_mpeg2.cpp
    tests/test_dossier.cpp
    tests/test_sound.cpp
    tests/test_frame_interp.cpp
    tests/test_ptr32.cpp
    tests/test_disc.cpp
    tests/test_launcher.cpp
    tests/test_platform.cpp
    tests/test_determinism.cpp
    ${NF_EXE_SOURCES}
)
# Os testes das missões de carro (tests/test_driving_*.cpp) entram sozinhos.
file(GLOB NF_DRV_TESTS CONFIGURE_DEPENDS tests/test_driving_*.cpp)
target_sources(nf_tests PRIVATE ${NF_DRV_TESTS})
target_link_libraries(nf_tests PRIVATE nf_data nf_ui_hd nf_ui_modern nf_frame_interp nf_drv)
# Os testes da camada HD leem o hd_ui/map.json do repositório.
target_compile_definitions(nf_tests PRIVATE NF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(nf_tests PRIVATE ${NF_OPTIONS})
add_test(NAME nf_tests COMMAND nf_tests)

# Regressão com os dados do jogo (fora do ctest: precisa de ACTION.ELF e FILES.BIN).
add_executable(nf_level_check tests/level_check.cpp ${NF_EXE_SOURCES})
target_link_libraries(nf_level_check PRIVATE nf_data)
target_compile_options(nf_level_check PRIVATE ${NF_OPTIONS})

# O disco do jogo entre duas fontes, arquivo por arquivo (a imagem .iso contra os arquivos extraídos, o leitor de DVD, a
# cópia pessoal; platform/disc_source.h). Fora do ctest: precisa dos dados do usuário.
add_executable(nf_disc_check tests/disc_check.cpp ${NF_EXE_SOURCES})
target_link_libraries(nf_disc_check PRIVATE nf_data)
target_compile_options(nf_disc_check PRIVATE ${NF_OPTIONS})

# O decodificador de MPEG-2 do port (platform/mpeg2_decoder.h) contra o do sistema, quadro a quadro, nos 22 filmes do
# disco (MOVIES/30_FPS). Fora do ctest: precisa dos dados do usuário.
add_executable(nf_movie_check tests/movie_check.cpp ${NF_EXE_SOURCES})
target_link_libraries(nf_movie_check PRIVATE nf_platform)
target_compile_options(nf_movie_check PRIVATE ${NF_OPTIONS})

# Regressão do carregador original portado (LoaderLoad/parsemap_*) com os dados do jogo.
add_executable(nf_loader_check tests/loader_check.cpp tests/campaign_audit.cpp tests/sights_measure.cpp ${NF_EXE_SOURCES})
target_link_libraries(nf_loader_check PRIVATE nf_data)
target_compile_options(nf_loader_check PRIVATE ${NF_OPTIONS})

# A física das missões de carro conferida contra o DRIVING.ELF (docs/driving.md, seção 11): o microcódigo da VU0 no
# interpretador contra o porte à mão, o passo inteiro dos dois jeitos, as funções do EE no interpretador MIPS.
add_executable(nf_driving_check tests/driving_check.cpp tests/driving_check_ee.cpp ${NF_EXE_SOURCES})
target_link_libraries(nf_driving_check PRIVATE nf_data nf_drv)
target_compile_options(nf_driving_check PRIVATE ${NF_OPTIONS})
