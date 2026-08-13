# The engine build is intentionally described from the checked-in source tree.
# It must not depend on generated PDB manifests or checked-in IDE projects.

# SDL2 owns the game window, OpenGL context, keyboard/mouse input and event
# pump. Keep the dependency vendored and static so etqw.exe has no SDL2.dll
# deployment dependency. ETQW uses the DLL MSVC runtime, so do not force SDL's
# static-runtime override from the Darklight build.
set(SDL_SHARED OFF CACHE BOOL "Do not build the SDL2 shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build the SDL2 static library" FORCE)
set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "ETQW provides its own entry point" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Do not install vendored SDL2" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "Do not create SDL2 uninstall targets" FORCE)
set(SDL_TEST OFF CACHE BOOL "Do not build SDL2 tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build the SDL2 test library" FORCE)
set(SDL_FORCE_STATIC_VCRT OFF CACHE BOOL "Match ETQW's DLL MSVC runtime" FORCE)
add_subdirectory(sys/sdl2 EXCLUDE_FROM_ALL)
set_target_properties(SDL2-static PROPERTIES FOLDER "ETQW/Third Party/SDL2")

file(GLOB ETQW_BSE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bse/*.cpp"
)
file(GLOB ETQW_CM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cm/*.cpp"
)
file(GLOB ETQW_DECLLIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/decllib/*.cpp"
)
file(GLOB_RECURSE ETQW_FRAMEWORK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cpp"
)

# These are older reference implementations which are intentionally disabled
# in the current ETQW engine. Their replacements live in decllib or in the
# active framework implementation.
list(FILTER ETQW_FRAMEWORK_SOURCES EXCLUDE REGEX
    "/framework/(DeclAF|DeclEntityDef|DeclFX|DeclParticle|DeclPDA|DeclSkin|DeclTable|Unzip)\\.cpp$"
)

set(ETQW_RENDERER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/DeviceContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_arb2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_atmosphere.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_common.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_foglights.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_new.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_ocq.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_raytracing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_shadow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/draw_shadowmap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/dynamicmodelcache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/GuiModel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/image_processor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/image_resampler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Image_init.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Image_files.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Image_load.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Image_process.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderer/Image_program.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/image_sequence.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Material.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/megatexture/MegaTexture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/megatexture/MegaTextureCodec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/megatexture/MegaTextureTileLoader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/megatexture/MegaTextureTileDecompressor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Model.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Model_lwo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Model_Stuff.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/ModelManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/querytimers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderbindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderbindingmanager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderlog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystem_init.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystemBackend.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/renderer/RendererMetrics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RuntimeSpirvCompiler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderWorld.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderWorld_demo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderWorld_load.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderWorld_portals.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/ScreenRect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/surfaceTypeMap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_lightrun.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_light.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_matrixmath.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_polytope.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_render.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_rendertools.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/tr_trisurf.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/VertexCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/VulkanBackend.cpp"
)

set(ETQW_SYSTEM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sdl_events.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sdl_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_local.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/SystemBootstrap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/splashscreen.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/rc/etqw.rc"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/render/win_opengl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/stacktracer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_soundthread.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_dinput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_xinput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_perfquery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_stack.cpp"
)

set(ETQW_SOUND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundShader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundSystemBootstrap.cpp"
)

set(ETQW_ENGINE_SUPPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/navigation/Navigation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/filelib/File.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/punkbuster/pbmd5.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/cg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/qgllib.cpp"
)

# Development map-authoring tools.
file(GLOB ETQW_DMAP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/dmap/*.cpp"
)
list(REMOVE_ITEM ETQW_DMAP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/dmap/lightmap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/dmap/optimize_gcc.cpp"
)
file(GLOB ETQW_MEGATEXTURE_COMPILER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/megatexture/*.cpp"
)
file(GLOB ETQW_NAVBUILD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/compilers/navbuild/*.cpp"
)
set(ETQW_TOOL_COMPILER_SOURCES
    ${ETQW_DMAP_SOURCES}
    ${ETQW_MEGATEXTURE_COMPILER_SOURCES}
    ${ETQW_NAVBUILD_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/radiant/megatexture/RoadBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/radiant/RadiantVulkan.cpp"
)

file(GLOB ETQW_RADIANT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/radiant/*.cpp"
)
list(REMOVE_ITEM ETQW_RADIANT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/radiant/RadiantVulkan.cpp"
)
set(ETQW_IMGUI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_win32.cpp"
)
list(APPEND ETQW_RADIANT_SOURCES ${ETQW_IMGUI_SOURCES})
set_source_files_properties(${ETQW_IMGUI_SOURCES} PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
)

get_filename_component(ETQW_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(ETQW_MSVC_TOOL_DIR "${ETQW_MSVC_BIN_DIR}/../../.." ABSOLUTE)
set(ETQW_MFC_AVAILABLE OFF)
if(EXISTS "${ETQW_MSVC_TOOL_DIR}/atlmfc/include/afxwin.h")
    set(ETQW_MFC_AVAILABLE ON)
endif()
set(ETQW_RADIANT_DEFAULT OFF)
if(ETQW_MFC_AVAILABLE AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ETQW_RADIANT_DEFAULT ON)
endif()
option(ETQW_BUILD_RADIANT "Build the native x64 ETQW Radiant editor (requires the MSVC MFC component)" ${ETQW_RADIANT_DEFAULT})
if(ETQW_BUILD_RADIANT AND NOT ETQW_MFC_AVAILABLE)
    message(FATAL_ERROR "ETQW_BUILD_RADIANT requires the MSVC MFC component for the active toolset")
elseif(ETQW_BUILD_RADIANT AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ETQW_BUILD_RADIANT is an x64-only target")
elseif(NOT ETQW_MFC_AVAILABLE)
    message(STATUS "ETQW Radiant target disabled: the MSVC MFC component is not installed")
endif()

set(ETQW_EXECUTABLE_SOURCES
    ${ETQW_BSE_SOURCES}
    ${ETQW_CM_SOURCES}
    ${ETQW_DECLLIB_SOURCES}
    ${ETQW_FRAMEWORK_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/openal/idal.cpp"
    ${ETQW_RENDERER_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/sdnet/SDNet.cpp"
    ${ETQW_SOUND_SOURCES}
    ${ETQW_SYSTEM_SOURCES}
    ${ETQW_ENGINE_SUPPORT_SOURCES}
    ${ETQW_TOOL_COMPILER_SOURCES}
	${ETQW_IMGUI_SOURCES}
)
if(ETQW_BUILD_RADIANT)
    list(APPEND ETQW_EXECUTABLE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/rc/Radiant.rc"
    )
endif()
list(REMOVE_DUPLICATES ETQW_EXECUTABLE_SOURCES)
list(SORT ETQW_EXECUTABLE_SOURCES)

# Vendor source sets match the revisions used by the engine without relying
# on the former PDB-derived source table.
file(GLOB ETQW_CURL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/lib/*.c"
)
list(FILTER ETQW_CURL_SOURCES EXCLUDE REGEX
    "/(amigaos|content_encoding|http_negotiate|http_ntlm|if2ip|inet_pton|krb4|memdebug|multi|nwlib|security|strtoofft|version)\\.c$"
)

file(GLOB ETQW_OGG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libogg/src/*.c"
)
file(GLOB ETQW_VORBIS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libvorbis/lib/*.c"
)
list(FILTER ETQW_VORBIS_SOURCES EXCLUDE REGEX
    "/(barkmel|lookup|psytune|tone|vorbisenc)\\.c$"
)
file(GLOB ETQW_THEORA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libtheora/lib/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libtheora/lib/x86_32_vs/*.c"
)
list(FILTER ETQW_THEORA_SOURCES EXCLUDE REGEX "/encoder_disabled\\.c$")
file(GLOB ETQW_SPEEX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/speex/speex/libspeex/*.c"
)
list(FILTER ETQW_SPEEX_SOURCES EXCLUDE REGEX
    "/(math_approx|speex_header|stereo|testenc|testenc_uwb|testenc_wb)\\.c$"
)
set(ETQW_ZLIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/adler32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/crc32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/inflate.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/inftrees.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/zutil.c"
)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND ETQW_ZLIB_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/inffast.c"
    )
endif()

function(etqw_configure_vendor_library target_name)
    target_compile_definitions(${target_name} PRIVATE
        WIN32
        _WINDOWS
        _MBCS
        _LIB
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
    target_compile_options(${target_name} PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:/W3>"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:/GS->"
        "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:C,CXX>>:/Zi>"
    )
    set_target_properties(${target_name} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/lib"
        FOLDER "ETQW/Third Party"
    )
endfunction()

file(GLOB ETQW_RECAST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/recast/Recast/Source/*.cpp"
)
file(GLOB ETQW_DETOUR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/recast/Detour/Source/*.cpp"
)
add_library(etqw_recast STATIC ${ETQW_RECAST_SOURCES})
target_include_directories(etqw_recast PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/recast/Recast/Include"
)
etqw_configure_vendor_library(etqw_recast)
add_library(etqw_detour STATIC ${ETQW_DETOUR_SOURCES})
target_include_directories(etqw_detour PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/recast/Detour/Include"
)
etqw_configure_vendor_library(etqw_detour)

add_library(etqw_curl STATIC ${ETQW_CURL_SOURCES})
target_include_directories(etqw_curl PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/lib"
)
target_compile_definitions(etqw_curl PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    _USRDLL
    CURLLIB_EXPORTS
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
)
target_compile_options(etqw_curl PRIVATE /W3)
set_target_properties(etqw_curl PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/lib"
    FOLDER "ETQW/Third Party"
)

add_library(etqw_ogg STATIC EXCLUDE_FROM_ALL ${ETQW_OGG_SOURCES})
target_include_directories(etqw_ogg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libogg/include"
)
etqw_configure_vendor_library(etqw_ogg)

add_library(etqw_vorbis STATIC EXCLUDE_FROM_ALL ${ETQW_VORBIS_SOURCES})
target_include_directories(etqw_vorbis
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libvorbis/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libogg/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libvorbis/lib"
)
target_link_libraries(etqw_vorbis PUBLIC etqw_ogg)
etqw_configure_vendor_library(etqw_vorbis)

add_library(etqw_theora STATIC EXCLUDE_FROM_ALL ${ETQW_THEORA_SOURCES})
target_include_directories(etqw_theora
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libtheora/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libogg/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/ogg/libtheora/lib"
)
target_compile_definitions(etqw_theora PRIVATE USE_ASM)
target_link_libraries(etqw_theora PUBLIC etqw_ogg)
etqw_configure_vendor_library(etqw_theora)

add_library(etqw_speex STATIC EXCLUDE_FROM_ALL ${ETQW_SPEEX_SOURCES})
target_include_directories(etqw_speex
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/speex/speex/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/speex/speex/libspeex"
)
etqw_configure_vendor_library(etqw_speex)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    enable_language(ASM_MASM)
    list(APPEND ETQW_ZLIB_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/contrib/masmx86/inffas32.asm"
    )
endif()
add_library(etqw_zlib STATIC ${ETQW_ZLIB_SOURCES})
target_include_directories(etqw_zlib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
)
target_compile_definitions(etqw_zlib PRIVATE ZLIB_WINAPI)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(etqw_zlib PRIVATE ASMV ASMINF)
endif()
etqw_configure_vendor_library(etqw_zlib)

add_custom_target(etqw_codecs)
add_dependencies(etqw_codecs
    etqw_ogg
    etqw_vorbis
    etqw_theora
    etqw_speex
    etqw_zlib
)
set_target_properties(etqw_codecs PROPERTIES FOLDER "ETQW/Third Party")

if(ETQW_BUILD_RADIANT)
add_library(etqw_radiant STATIC ${ETQW_RADIANT_SOURCES})
target_include_directories(etqw_radiant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sdl2/include"
)
target_compile_definitions(etqw_radiant PRIVATE
    WIN32
    _WINDOWS
    _AFXDLL
    _MBCS
    ZLIB_WINAPI
    SD_DEMO_BUILD
    SD_SUPPORT_REPEATER
    SD_RETAIL_SDNET_ABI
    SD_SDK_BUILD
    SD_USE_DRAWVERT_SIZE_32
    SD_USE_INDEX_SIZE_16
    ETQW_ENGINE_RECONSTRUCTION
    ETQW_WITH_RADIANT
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
target_compile_options(etqw_radiant PRIVATE /W3 /bigobj /GR /Zc:wchar_t)
target_precompile_headers(etqw_radiant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/radiant/RadiantPch.h"
)
target_link_libraries(etqw_radiant PRIVATE idLib)
set_target_properties(etqw_radiant PROPERTIES
    VS_GLOBAL_UseOfMfc Dynamic
    ARCHIVE_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/lib"
    FOLDER "ETQW/Tools"
)
endif()

# MiniZip retains ETQW's .cpp filenames, but these two units contain the
# original C implementation and must be compiled as C.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/minizip/ioapi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/minizip/unzip.cpp"
    PROPERTIES LANGUAGE C
)

add_executable(etqw WIN32 ${ETQW_EXECUTABLE_SOURCES})
find_path(ETQW_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS "$ENV{VULKAN_SDK}/Include"
    DOC "Directory containing the Vulkan headers"
)
if(NOT ETQW_VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR
        "Vulkan headers were not found. Install the Vulkan SDK or set "
        "ETQW_VULKAN_INCLUDE_DIR."
    )
endif()
target_include_directories(etqw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
    "${ETQW_VULKAN_INCLUDE_DIR}"
)
target_compile_definitions(etqw PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    ZLIB_WINAPI
    SD_DEMO_BUILD
    SD_SUPPORT_REPEATER
    SD_RETAIL_SDNET_ABI
    SD_SDK_BUILD
    SD_USE_DRAWVERT_SIZE_32
    SD_USE_INDEX_SIZE_16
    ETQW_ENGINE_RECONSTRUCTION
    SDL_MAIN_HANDLED
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
target_compile_options(etqw PRIVATE
    /W3
    /bigobj
    /GR
    /Zc:wchar_t
    "$<$<CONFIG:Release>:/GS->"
    "$<$<CONFIG:Release>:/Zi>"
)
target_precompile_headers(etqw PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/framework/precompiled.h>"
)
target_link_libraries(etqw PRIVATE
    idLib
    etqw_curl
    etqw_recast
    etqw_detour
    etqw_zlib
    SDL2::SDL2-static
    advapi32
    comctl32
    dbghelp
    dinput8
    dsound
    dxguid
    gdi32
    iphlpapi
    ole32
    oleaut32
    opengl32
    pdh
    setupapi
    shell32
    user32
    version
    winmm
    ws2_32
)
if(ETQW_BUILD_RADIANT)
    target_compile_definitions(etqw PRIVATE ETQW_WITH_RADIANT)
    target_link_libraries(etqw PRIVATE etqw_radiant)
    set_property(TARGET etqw PROPERTY VS_GLOBAL_UseOfMfc Dynamic)
endif()
target_link_options(etqw PRIVATE
    /LARGEADDRESSAWARE
    /STACK:4194304,4194304
    "$<$<CONFIG:Release>:/DEBUG:FULL>"
    "$<$<CONFIG:Release>:/MAP:${ETQW_RUNTIME_DIR}/${ETQW_EXECUTABLE_NAME}.map>"
)
set_target_properties(etqw PROPERTIES
    OUTPUT_NAME "${ETQW_EXECUTABLE_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${ETQW_RUNTIME_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
    FOLDER "ETQW"
)

# The engine can launch these SDK tools directly. Stage self-contained
# executables beside the engine so runtime shader compilation does not depend
# on a developer machine having VULKAN_SDK configured.
find_program(ETQW_GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator.exe glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
find_program(ETQW_SPIRV_VAL_EXECUTABLE
    NAMES spirv-val.exe spirv-val
    HINTS "$ENV{VULKAN_SDK}/Bin"
)
if(ETQW_GLSLANG_VALIDATOR_EXECUTABLE)
    add_custom_command(TARGET etqw POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:etqw>/vkcompiler"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${ETQW_GLSLANG_VALIDATOR_EXECUTABLE}"
            "$<TARGET_FILE_DIR:etqw>/vkcompiler/"
        VERBATIM
    )
    if(ETQW_SPIRV_VAL_EXECUTABLE)
        add_custom_command(TARGET etqw POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ETQW_SPIRV_VAL_EXECUTABLE}"
                "$<TARGET_FILE_DIR:etqw>/vkcompiler/"
            VERBATIM
        )
    endif()
else()
    message(WARNING
        "glslangValidator was not found; etqw will still search vkcompiler, "
        "VULKAN_SDK, and PATH at runtime."
    )
endif()
foreach(config_name Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER "${config_name}" config_name_upper)
    set_target_properties(etqw PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_RUNTIME_DIR}"
    )
endforeach()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${ETQW_EXECUTABLE_SOURCES}
    ${ETQW_CURL_SOURCES}
    ${ETQW_ZLIB_SOURCES}
)

list(LENGTH ETQW_EXECUTABLE_SOURCES ETQW_EXECUTABLE_SOURCE_COUNT)
list(LENGTH ETQW_CURL_SOURCES ETQW_CURL_SOURCE_COUNT)
list(LENGTH ETQW_OGG_SOURCES ETQW_OGG_SOURCE_COUNT)
list(LENGTH ETQW_VORBIS_SOURCES ETQW_VORBIS_SOURCE_COUNT)
list(LENGTH ETQW_THEORA_SOURCES ETQW_THEORA_SOURCE_COUNT)
list(LENGTH ETQW_SPEEX_SOURCES ETQW_SPEEX_SOURCE_COUNT)
list(LENGTH ETQW_ZLIB_SOURCES ETQW_ZLIB_SOURCE_COUNT)
message(STATUS "ETQW engine source counts:")
message(STATUS "  engine:     ${ETQW_EXECUTABLE_SOURCE_COUNT}")
message(STATUS "  curl:       ${ETQW_CURL_SOURCE_COUNT}")
message(STATUS "  libogg:     ${ETQW_OGG_SOURCE_COUNT}")
message(STATUS "  libvorbis:  ${ETQW_VORBIS_SOURCE_COUNT}")
message(STATUS "  libtheora:  ${ETQW_THEORA_SOURCE_COUNT}")
message(STATUS "  Speex:      ${ETQW_SPEEX_SOURCE_COUNT}")
message(STATUS "  zlib/asm:   ${ETQW_ZLIB_SOURCE_COUNT}")
