# The engine build is intentionally described from the checked-in source tree.
# It must not depend on generated PDB manifests or checked-in IDE projects.

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
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/Model_Stuff.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/ModelManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/querytimers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderbindings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderbindingmanager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/renderlog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystem_init.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/RenderSystemBackend.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_local.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/SystemBootstrap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/render/win_opengl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/stacktracer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_inputthread.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_soundthread.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_dinput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input_keyboard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input_mouse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_xinput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_perfquery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_stack.cpp"
)

set(ETQW_SOUND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundShader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundSystemBootstrap.cpp"
)

set(ETQW_ENGINE_SUPPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/AASLib/AASFileManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/filelib/File.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/punkbuster/pbmd5.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/cg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/qgllib.cpp"
)

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
)
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

enable_language(ASM_MASM)
list(APPEND ETQW_ZLIB_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib/contrib/masmx86/inffas32.asm"
)
add_library(etqw_zlib STATIC ${ETQW_ZLIB_SOURCES})
target_include_directories(etqw_zlib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
)
target_compile_definitions(etqw_zlib PRIVATE ZLIB_WINAPI ASMV ASMINF)
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
    etqw_zlib
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
target_link_options(etqw PRIVATE
    /LARGEADDRESSAWARE
    /STACK:4194304,4194304
    "$<$<CONFIG:Release>:/DEBUG:FULL>"
    "$<$<CONFIG:Release>:/MAP:${ETQW_RUNTIME_DIR}/etqw.map>"
)
set_target_properties(etqw PROPERTIES
    OUTPUT_NAME "etqw"
    RUNTIME_OUTPUT_DIRECTORY "${ETQW_RUNTIME_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
    FOLDER "ETQW"
)

# The Win32 game can launch these 64-bit tools directly. Stage self-contained
# SDK executables beside etqw.exe so runtime shader compilation does not depend
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
