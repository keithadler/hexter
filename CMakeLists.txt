cmake_minimum_required(VERSION 3.21)

project(hexter
    VERSION 2.0.2
    DESCRIPTION "Yamaha DX7 modeling software synthesizer"
    HOMEPAGE_URL "https://github.com/keithadler/hexter"
    LANGUAGES C)

# With MSVC, link the C runtime statically: a user's machine need not have the Visual C++
# redistributable, just as the MinGW build must not need libwinpthread-1.dll.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(HEXTER_BUILD_CLAP  "Build the CLAP plugin" ON)
option(HEXTER_BUILD_LV2   "Build the LV2 plugin" ON)
option(HEXTER_BUILD_DSSI  "Build the legacy DSSI plugin and GTK2 editor when their libraries are installed" ON)
option(HEXTER_BUILD_TOOLS "Build the hexter-render command line tool" ON)
option(HEXTER_BUILD_AUV2  "Build an Audio Unit (macOS) from the CLAP through clap-wrapper" ON)
option(HEXTER_BUILD_STANDALONE "Build a standalone application from the CLAP through clap-wrapper" ON)
option(HEXTER_BUILD_TESTS "Build the tests" ON)
option(HEXTER_FLOATING_POINT "Render in floating point instead of the default fixed point" OFF)

include(FetchContent)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)

find_package(Threads REQUIRED)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # -ffp-contract=off keeps the fixed-point engine bit-for-bit identical
    # between machines with and without fused multiply-add.
    set(HEXTER_C_FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -ffp-contract=off)
else()
    set(HEXTER_C_FLAGS)
endif()

# ---- the engine: everything that makes sound, with no host API attached ----

set(HEXTER_CORE_SOURCES
    src/dx7_voice.c
    src/dx7_voice_data.c
    src/dx7_voice_patches.c
    src/dx7_voice_render.c
    src/dx7_voice_tables.c
    src/hexter_synth.c
    src/dx7_bank.c)

add_library(hexter_core OBJECT ${HEXTER_CORE_SOURCES})
target_include_directories(hexter_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(hexter_core PUBLIC
    VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${HEXTER_FLOATING_POINT}>:HEXTER_USE_FLOATING_POINT=1>)
target_compile_options(hexter_core PRIVATE ${HEXTER_C_FLAGS})
if(WIN32)
    target_compile_definitions(hexter_core PUBLIC _USE_MATH_DEFINES)
endif()

add_library(hexter_engine STATIC src/hexter_engine.c $<TARGET_OBJECTS:hexter_core>)
target_include_directories(hexter_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(hexter_engine PUBLIC
    VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${HEXTER_FLOATING_POINT}>:HEXTER_USE_FLOATING_POINT=1>)
target_compile_options(hexter_engine PRIVATE ${HEXTER_C_FLAGS})
target_link_libraries(hexter_engine PUBLIC Threads::Threads)
if(MINGW)
    # MinGW's pthreads come from libwinpthread-1.dll, which a Windows machine only has when MinGW is
    # installed. Every plugin and tool takes the engine, so link the runtime into each binary here and
    # the shipped zip stands on its own (2.0.0 and 2.0.1 failed with "libwinpthread-1.dll was not found").
    target_link_options(hexter_engine PUBLIC -static)
endif()
if(NOT MSVC)
    target_link_libraries(hexter_engine PUBLIC m)
endif()
if(WIN32)
    target_compile_definitions(hexter_engine PUBLIC _USE_MATH_DEFINES)
endif()

# ---- CLAP ----

if(HEXTER_BUILD_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        1.2.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap)

    add_library(hexter_clap MODULE plugins/clap/hexter_clap.c)
    target_link_libraries(hexter_clap PRIVATE hexter_engine clap)
    target_compile_options(hexter_clap PRIVATE ${HEXTER_C_FLAGS})
    set_target_properties(hexter_clap PROPERTIES OUTPUT_NAME hexter PREFIX "")
    if(APPLE)
        set_target_properties(hexter_clap PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/plugins/clap/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME hexter
            MACOSX_BUNDLE_GUI_IDENTIFIER com.github.keithadler.hexter
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_COPYRIGHT "GPL-2.0-or-later. Sean Bolton and others.")
        install(TARGETS hexter_clap DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    else()
        set_target_properties(hexter_clap PROPERTIES SUFFIX ".clap")
        install(TARGETS hexter_clap LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/clap)
    endif()
endif()

# ---- clap-wrapper: the Audio Unit (macOS) and the standalone application ----
# clap-wrapper links the CLAP statically into an AUv2 component and into a small standalone
# host with its own audio and MIDI I/O (RtAudio and RtMidi); it fetches those and Apple's
# AudioUnitSDK itself. Its Windows standalone shell is C++/WinRT, so on Windows the standalone
# needs MSVC or clang: a MinGW build skips it and still produces the plugins and the tool.

set(HEXTER_WRAP_AUV2 OFF)
set(HEXTER_WRAP_STANDALONE OFF)
if(HEXTER_BUILD_CLAP)
    if(APPLE AND HEXTER_BUILD_AUV2)
        set(HEXTER_WRAP_AUV2 ON)
    endif()
    if(HEXTER_BUILD_STANDALONE)
        if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
            message(STATUS "hexter: standalone skipped, clap-wrapper's Windows standalone needs MSVC or clang")
        else()
            set(HEXTER_WRAP_STANDALONE ON)
        endif()
    endif()
endif()

if(HEXTER_WRAP_AUV2 OR HEXTER_WRAP_STANDALONE)
    enable_language(CXX)
    if(APPLE)
        enable_language(OBJC OBJCXX)
    endif()
    set(CMAKE_CXX_STANDARD 17)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_BUILD_AUV2 ${HEXTER_WRAP_AUV2} CACHE BOOL "" FORCE)
    set(CLAP_WRAPPER_BUILD_STANDALONE ${HEXTER_WRAP_STANDALONE} CACHE BOOL "" FORCE)
    FetchContent_Declare(clap-wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG        v0.16.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(clap-wrapper)

    add_library(hexter_clap_impl STATIC plugins/clap/hexter_clap.c)
    target_link_libraries(hexter_clap_impl PUBLIC hexter_engine clap)
    target_include_directories(hexter_clap_impl PUBLIC plugins/clap)
    target_compile_definitions(hexter_clap_impl PRIVATE HEXTER_CLAP_NO_ENTRY=1)
    target_compile_options(hexter_clap_impl PRIVATE ${HEXTER_C_FLAGS})

    set(HEXTER_WRAP_FORMATS CLAP)
    if(HEXTER_WRAP_AUV2)
        list(APPEND HEXTER_WRAP_FORMATS AUV2)
    endif()
    set(HEXTER_WRAP_STANDALONE_ARGS)
    if(HEXTER_WRAP_STANDALONE)
        # target postfix, output name, CLAP id
        set(HEXTER_WRAP_STANDALONE_ARGS STANDALONE_CONFIGURATIONS standalone hexter com.github.keithadler.hexter)
    endif()

    # Outputs land in build/wrapped: hexter.component (macOS), and the standalone as hexter.app
    # (macOS), hexter (Linux) or Standalone-hexter_wrapped_standalone/hexter.exe (Windows).
    make_clapfirst_plugins(
        TARGET_NAME hexter_wrapped
        IMPL_TARGET hexter_clap_impl
        OUTPUT_NAME "hexter"
        ENTRY_SOURCE "plugins/clap/hexter_clap_entry.cpp"
        BUNDLE_IDENTIFIER "com.github.keithadler.hexter.au"
        BUNDLE_VERSION ${PROJECT_VERSION}
        COPY_AFTER_BUILD FALSE
        PLUGIN_FORMATS ${HEXTER_WRAP_FORMATS}
        ${HEXTER_WRAP_STANDALONE_ARGS}
        ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/wrapped
        AUV2_MANUFACTURER_NAME "Keith Adler"
        AUV2_MANUFACTURER_CODE "KAdl"
        AUV2_SUBTYPE_CODE "Hxtr"
        AUV2_INSTRUMENT_TYPE "aumu")
endif()

# ---- LV2 ----

if(HEXTER_BUILD_LV2)
    find_path(LV2_INCLUDE_DIR lv2/core/lv2.h)
    if(NOT LV2_INCLUDE_DIR)
        FetchContent_Declare(lv2
            GIT_REPOSITORY https://github.com/lv2/lv2.git
            GIT_TAG        v1.18.10
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(lv2)
        set(LV2_INCLUDE_DIR ${lv2_SOURCE_DIR}/include)
    endif()

    if(WIN32)
        set(HEXTER_LV2_SUFFIX ".dll")
    elseif(APPLE)
        set(HEXTER_LV2_SUFFIX ".dylib")
    else()
        set(HEXTER_LV2_SUFFIX ".so")
    endif()
    set(HEXTER_LV2_BUNDLE_DIR ${CMAKE_BINARY_DIR}/lv2/hexter.lv2)

    add_library(hexter_lv2 MODULE plugins/lv2/hexter_lv2.c)
    target_link_libraries(hexter_lv2 PRIVATE hexter_engine)
    target_include_directories(hexter_lv2 PRIVATE ${LV2_INCLUDE_DIR})
    target_compile_options(hexter_lv2 PRIVATE ${HEXTER_C_FLAGS})
    set_target_properties(hexter_lv2 PROPERTIES
        OUTPUT_NAME hexter
        PREFIX ""
        SUFFIX "${HEXTER_LV2_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY ${HEXTER_LV2_BUNDLE_DIR})
    configure_file(plugins/lv2/manifest.ttl.in ${HEXTER_LV2_BUNDLE_DIR}/manifest.ttl @ONLY)
    configure_file(plugins/lv2/hexter.ttl ${HEXTER_LV2_BUNDLE_DIR}/hexter.ttl COPYONLY)

    install(TARGETS hexter_lv2 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/hexter.lv2)
    install(FILES ${HEXTER_LV2_BUNDLE_DIR}/manifest.ttl ${HEXTER_LV2_BUNDLE_DIR}/hexter.ttl
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/lv2/hexter.lv2)
endif()

# ---- legacy DSSI plugin and GTK2 editor (Linux, when the libraries exist) ----

if(HEXTER_BUILD_DSSI)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DSSI QUIET dssi>=0.4 alsa)
        pkg_check_modules(HEXTER_GTK QUIET gtk+-2.0>=2.4 liblo>=0.12 alsa)
    endif()
    if(DSSI_FOUND)
        add_library(hexter_dssi MODULE src/hexter.c $<TARGET_OBJECTS:hexter_core>)
        target_include_directories(hexter_dssi PRIVATE src ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(hexter_dssi PRIVATE VERSION="${PROJECT_VERSION}" MIDI_ALSA=1
            $<$<BOOL:${HEXTER_FLOATING_POINT}>:HEXTER_USE_FLOATING_POINT=1>)
        target_compile_options(hexter_dssi PRIVATE ${HEXTER_C_FLAGS} ${DSSI_CFLAGS_OTHER})
        target_link_libraries(hexter_dssi PRIVATE ${DSSI_LIBRARIES} Threads::Threads m)
        # DSSI hosts look up ladspa_descriptor and dssi_descriptor by name
        set_target_properties(hexter_dssi PROPERTIES OUTPUT_NAME hexter PREFIX "" SUFFIX ".so"
                              C_VISIBILITY_PRESET default)
        install(TARGETS hexter_dssi LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi)
        message(STATUS "hexter: building the legacy DSSI plugin")
    else()
        message(STATUS "hexter: DSSI/ALSA not found, skipping the legacy DSSI plugin")
    endif()
    if(DSSI_FOUND AND HEXTER_GTK_FOUND)
        add_executable(hexter_gtk
            src/gui_main.c src/gui_callbacks.c src/gui_data.c src/gui_images.c
            src/gui_interface.c src/gui_midi.c src/gui_patch_edit.c
            src/gui_retro_console.c src/gui_retro_editor.c src/gui_widgy_alg.c
            src/gui_widgy_editor.c src/dx7_voice_data.c src/dx7_voice_patches.c
            src/dx7_bank.c)
        target_include_directories(hexter_gtk PRIVATE src ${HEXTER_GTK_INCLUDE_DIRS} ${DSSI_INCLUDE_DIRS})
        target_compile_definitions(hexter_gtk PRIVATE VERSION="${PROJECT_VERSION}" MIDI_ALSA=1)
        target_compile_options(hexter_gtk PRIVATE ${HEXTER_GTK_CFLAGS_OTHER} -Wno-deprecated-declarations)
        target_link_libraries(hexter_gtk PRIVATE ${HEXTER_GTK_LIBRARIES} m)
        install(TARGETS hexter_gtk RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/dssi/hexter)
        message(STATUS "hexter: building the legacy GTK2 editor")
    endif()
endif()

# ---- command line renderer ----

if(HEXTER_BUILD_TOOLS)
    add_executable(hexter_render tools/hexter_render.c)
    target_link_libraries(hexter_render PRIVATE hexter_engine)
    target_compile_options(hexter_render PRIVATE ${HEXTER_C_FLAGS})
    set_target_properties(hexter_render PROPERTIES OUTPUT_NAME hexter-render)
    install(TARGETS hexter_render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---- tests ----

if(HEXTER_BUILD_TESTS)
    enable_testing()

    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE hexter_engine)
    target_compile_options(test_engine PRIVATE ${HEXTER_C_FLAGS})
    add_test(NAME engine COMMAND test_engine ${CMAKE_CURRENT_SOURCE_DIR}/extra)

    if(HEXTER_BUILD_CLAP)
        add_executable(test_clap tests/test_clap.c)
        target_link_libraries(test_clap PRIVATE clap ${CMAKE_DL_LIBS})
        if(NOT MSVC)
            target_link_libraries(test_clap PRIVATE m)
        endif()
        target_compile_options(test_clap PRIVATE ${HEXTER_C_FLAGS})
        add_test(NAME clap COMMAND test_clap $<TARGET_FILE:hexter_clap> ${CMAKE_CURRENT_SOURCE_DIR}/extra)
    endif()

    if(HEXTER_BUILD_LV2)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LILV QUIET lilv-0)
        endif()
        if(LILV_FOUND)
            add_executable(test_lv2 tests/test_lv2.c)
            target_include_directories(test_lv2 PRIVATE ${LILV_INCLUDE_DIRS} ${LV2_INCLUDE_DIR})
            target_link_libraries(test_lv2 PRIVATE ${LILV_LINK_LIBRARIES} m)
            target_compile_options(test_lv2 PRIVATE ${HEXTER_C_FLAGS} ${LILV_CFLAGS_OTHER})
            add_test(NAME lv2 COMMAND test_lv2 ${HEXTER_LV2_BUNDLE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/extra)
            message(STATUS "hexter: lilv found, the LV2 plugin will be tested")
        else()
            message(STATUS "hexter: lilv not found, the LV2 plugin will be built but not tested")
        endif()
    endif()

    if(HEXTER_BUILD_TOOLS)
        add_test(NAME render_tool
            COMMAND hexter_render --bank ${CMAKE_CURRENT_SOURCE_DIR}/extra/dx7_roms.dx7
                    --program 0 --note 60 --seconds 1 --out ${CMAKE_BINARY_DIR}/render_test.wav)
        add_test(NAME render_list
            COMMAND hexter_render --bank ${CMAKE_CURRENT_SOURCE_DIR}/extra/dx7_roms.dx7 --list)
    endif()
endif()

# ---- packaging ----

install(FILES extra/default_patches.dx7 extra/dx7_roms.dx7 extra/tx7_roms.dx7
              extra/fb01_roms_converted_12.dx7 extra/fb01_roms_converted_34.dx7
              extra/fb01_roms_converted_5.dx7
        DESTINATION ${CMAKE_INSTALL_DATADIR}/hexter/banks)
