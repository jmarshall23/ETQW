set(ETQW_PDB_FILE_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/reconstruction/pdb_files.tsv"
)

if(NOT EXISTS "${ETQW_PDB_FILE_MANIFEST}")
    message(FATAL_ERROR
        "The PDB source manifest is missing. Run "
        "reconstruction/GeneratePdbManifest.ps1 before configuring the "
        "engine reconstruction target."
    )
endif()

# Source ownership comes from the Microsoft PDB. For the bootstrap target,
# compile only units whose original path already exists in this tree. Missing
# ETQW units are added under their exact PDB paths as they are reconstructed.
set(ETQW_ENGINE_SOURCE_DIRS
    bse
    cm
    decllib
    framework
    openal
    renderer
    sdnet
    sound
    sys
)
set(ETQW_ENGINE_BOOTSTRAP_SOURCES)
set(ETQW_CURL_SOURCES)
set(ETQW_OGG_SOURCES)
set(ETQW_VORBIS_SOURCES)
set(ETQW_THEORA_SOURCES)
set(ETQW_SPEEX_SOURCES)
set(ETQW_ZLIB_SOURCES)
set(ETQW_CM_BOOTSTRAP_SOURCES)
set(ETQW_FRAMEWORK_BOOTSTRAP_SOURCES)
set(ETQW_OPENAL_BOOTSTRAP_SOURCES)
set(ETQW_RENDERER_BOOTSTRAP_SOURCES)
set(ETQW_SYS_BOOTSTRAP_SOURCES)

file(STRINGS "${ETQW_PDB_FILE_MANIFEST}" ETQW_PDB_FILE_ROWS)
foreach(manifest_row IN LISTS ETQW_PDB_FILE_ROWS)
    string(REPLACE "\t" ";" manifest_fields "${manifest_row}")
    list(LENGTH manifest_fields manifest_field_count)
    if(manifest_field_count LESS 5)
        continue()
    endif()

    list(GET manifest_fields 0 relative_path)
    list(GET manifest_fields 1 extension)
    list(GET manifest_fields 4 source_status)
    if(NOT extension MATCHES "^\\.(c|cc|cpp|cxx)$")
        continue()
    endif()
    # A manifest entry remains "missing" until the manifest is regenerated.
    # Pick up newly reconstructed files immediately when they appear at the
    # exact PDB path.
    if(source_status STREQUAL "missing"
            AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}")
        continue()
    endif()

    string(REGEX MATCH "^[^/]+" top_level_dir "${relative_path}")
    if(top_level_dir STREQUAL "curl")
        list(APPEND ETQW_CURL_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(relative_path MATCHES "^libs/ogg/libogg/")
        list(APPEND ETQW_OGG_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(relative_path MATCHES "^libs/ogg/libvorbis/")
        list(APPEND ETQW_VORBIS_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(relative_path MATCHES "^libs/ogg/libtheora/")
        list(APPEND ETQW_THEORA_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(relative_path MATCHES "^libs/speex/speex/")
        list(APPEND ETQW_SPEEX_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(relative_path MATCHES "^libs/zlib/")
        list(APPEND ETQW_ZLIB_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
    elseif(top_level_dir IN_LIST ETQW_ENGINE_SOURCE_DIRS)
        list(APPEND ETQW_ENGINE_BOOTSTRAP_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
        )
        if(top_level_dir STREQUAL "cm")
            list(APPEND ETQW_CM_BOOTSTRAP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
            )
        elseif(top_level_dir STREQUAL "framework")
            list(APPEND ETQW_FRAMEWORK_BOOTSTRAP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
            )
        elseif(top_level_dir STREQUAL "openal")
            list(APPEND ETQW_OPENAL_BOOTSTRAP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
            )
        elseif(top_level_dir STREQUAL "renderer")
            list(APPEND ETQW_RENDERER_BOOTSTRAP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
            )
        elseif(top_level_dir STREQUAL "sys")
            list(APPEND ETQW_SYS_BOOTSTRAP_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_path}"
            )
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES ETQW_ENGINE_BOOTSTRAP_SOURCES)
list(REMOVE_DUPLICATES ETQW_CURL_SOURCES)
list(REMOVE_DUPLICATES ETQW_OGG_SOURCES)
list(REMOVE_DUPLICATES ETQW_VORBIS_SOURCES)
list(REMOVE_DUPLICATES ETQW_THEORA_SOURCES)
list(REMOVE_DUPLICATES ETQW_SPEEX_SOURCES)
list(REMOVE_DUPLICATES ETQW_ZLIB_SOURCES)
list(REMOVE_DUPLICATES ETQW_CM_BOOTSTRAP_SOURCES)
list(REMOVE_DUPLICATES ETQW_FRAMEWORK_BOOTSTRAP_SOURCES)
list(REMOVE_DUPLICATES ETQW_OPENAL_BOOTSTRAP_SOURCES)
list(REMOVE_DUPLICATES ETQW_RENDERER_BOOTSTRAP_SOURCES)
list(REMOVE_DUPLICATES ETQW_SYS_BOOTSTRAP_SOURCES)
list(SORT ETQW_ENGINE_BOOTSTRAP_SOURCES)
list(SORT ETQW_CURL_SOURCES)
list(SORT ETQW_OGG_SOURCES)
list(SORT ETQW_VORBIS_SOURCES)
list(SORT ETQW_THEORA_SOURCES)
list(SORT ETQW_SPEEX_SOURCES)
list(SORT ETQW_ZLIB_SOURCES)

# lookup.c is textually included by the generated lookup tables and appears
# in the PDB source-file table, but it did not own a retail object file.
list(FILTER ETQW_VORBIS_SOURCES EXCLUDE REGEX "/lookup\.c$")
list(SORT ETQW_CM_BOOTSTRAP_SOURCES)
list(SORT ETQW_FRAMEWORK_BOOTSTRAP_SOURCES)
list(SORT ETQW_OPENAL_BOOTSTRAP_SOURCES)
list(SORT ETQW_RENDERER_BOOTSTRAP_SOURCES)
list(SORT ETQW_SYS_BOOTSTRAP_SOURCES)

list(LENGTH ETQW_ENGINE_BOOTSTRAP_SOURCES ETQW_ENGINE_BOOTSTRAP_SOURCE_COUNT)
list(LENGTH ETQW_CURL_SOURCES ETQW_CURL_SOURCE_COUNT)
list(LENGTH ETQW_OGG_SOURCES ETQW_OGG_SOURCE_COUNT)
list(LENGTH ETQW_VORBIS_SOURCES ETQW_VORBIS_SOURCE_COUNT)
list(LENGTH ETQW_THEORA_SOURCES ETQW_THEORA_SOURCE_COUNT)
list(LENGTH ETQW_SPEEX_SOURCES ETQW_SPEEX_SOURCE_COUNT)
list(LENGTH ETQW_ZLIB_SOURCES ETQW_ZLIB_SOURCE_COUNT)
list(LENGTH ETQW_CM_BOOTSTRAP_SOURCES ETQW_CM_BOOTSTRAP_SOURCE_COUNT)
list(LENGTH ETQW_FRAMEWORK_BOOTSTRAP_SOURCES ETQW_FRAMEWORK_BOOTSTRAP_SOURCE_COUNT)
list(LENGTH ETQW_OPENAL_BOOTSTRAP_SOURCES ETQW_OPENAL_BOOTSTRAP_SOURCE_COUNT)
list(LENGTH ETQW_RENDERER_BOOTSTRAP_SOURCES ETQW_RENDERER_BOOTSTRAP_SOURCE_COUNT)
list(LENGTH ETQW_SYS_BOOTSTRAP_SOURCES ETQW_SYS_BOOTSTRAP_SOURCE_COUNT)

set(ETQW_RENDERER_CORE_SOURCES
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
)
set(ETQW_SYSTEM_CORE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_input.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_local.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/SystemBootstrap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/render/win_opengl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/stacktracer.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_asyncthread.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_dinput.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input_keyboard.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_input_mouse.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/input/win_xinput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_perfquery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32/win_stack.cpp"
)
set(ETQW_SOUND_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundSystemBootstrap.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundShader.cpp"
)
set(ETQW_ENGINE_SUPPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/bse/BSE_EffectTemplate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/bse/BSE_Manager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/AdManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/GraphManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/NotificationSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/async/ServerScan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/filelib/File.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/punkbuster/pbmd5.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/cg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/qglLib/qgllib.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/AASLib/AASFileManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdnet/SDNet.cpp"
)

# This generated curl parser is present in the recovered tree but absent from
# the PDB source-file table.  curl's cookie/FTP code still imports it.
list(APPEND ETQW_CURL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/curl/lib/getdate.c"
)

# The surviving renderer implementation is Doom 3 code behind ETQW public
# headers.  Keep it available as an explicit convergence target, but do not
# mix its incompatible private types into the reconstructed executable.
set(ETQW_EXECUTABLE_SOURCES ${ETQW_ENGINE_BOOTSTRAP_SOURCES})
# The completed Darklight BSE conversion is retained under its PDB paths, but
# its renderer-facing implementation is enabled as a unit after the ETQW
# renderer private types replace the surviving Doom 3 headers.  Keep the
# already-integrated manager boundary in ETQW_ENGINE_SUPPORT_SOURCES for now.
list(FILTER ETQW_EXECUTABLE_SOURCES EXCLUDE REGEX "/bse/")
list(FILTER ETQW_EXECUTABLE_SOURCES EXCLUDE REGEX "/renderer/")
list(FILTER ETQW_EXECUTABLE_SOURCES EXCLUDE REGEX "/sys/")
list(APPEND ETQW_EXECUTABLE_SOURCES ${ETQW_RENDERER_CORE_SOURCES})
list(APPEND ETQW_EXECUTABLE_SOURCES ${ETQW_SYSTEM_CORE_SOURCES})
list(APPEND ETQW_EXECUTABLE_SOURCES ${ETQW_SOUND_CORE_SOURCES})
list(APPEND ETQW_EXECUTABLE_SOURCES ${ETQW_ENGINE_SUPPORT_SOURCES})

add_library(etqw_curl STATIC EXCLUDE_FROM_ALL ${ETQW_CURL_SOURCES})
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
    FOLDER "ETQW Engine/Third Party"
)

# The retail PDB records the exact codec revisions and their VS2005 build
# flags. Keep the recovered vendor units independently buildable while the
# sound and cinematic consumers are reconstructed around them.
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
        COMPILE_PDB_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/pdb"
        FOLDER "ETQW Engine/Third Party"
    )
endfunction()

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
add_library(etqw_zlib STATIC EXCLUDE_FROM_ALL ${ETQW_ZLIB_SOURCES})
target_include_directories(etqw_zlib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
)
target_compile_definitions(etqw_zlib PRIVATE ZLIB_WINAPI ASMV ASMINF)
etqw_configure_vendor_library(etqw_zlib)

# The retail files retain .cpp names because they belonged to ETQW, while
# their zlib-1.2.3 MiniZip bodies use the original C declaration style.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/minizip/ioapi.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/minizip/unzip.cpp"
    PROPERTIES LANGUAGE C
)

add_custom_target(etqw_codecs)
add_dependencies(etqw_codecs
    etqw_ogg
    etqw_vorbis
    etqw_theora
    etqw_speex
    etqw_zlib
)
set_target_properties(etqw_codecs PROPERTIES FOLDER "ETQW Engine/Third Party")

# Keep each reconstruction area independently buildable. This makes it
# possible to converge a private ETQW interface against the public SDK and
# PDB before unrelated Doom 3 implementation drift is introduced.
add_library(etqw_cm OBJECT EXCLUDE_FROM_ALL ${ETQW_CM_BOOTSTRAP_SOURCES})
target_include_directories(etqw_cm PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
)
target_compile_definitions(etqw_cm PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    ZLIB_WINAPI
    SD_DEMO_BUILD
    # The SDK game DLL is built with the retail repeater interfaces enabled.
    # SD_DEMO_BUILD suppresses this in BuildDefines.h, so restore it explicitly
    # on every engine reconstruction unit to keep cross-module vtables aligned.
    SD_SUPPORT_REPEATER
    SD_SDK_BUILD
    SD_USE_DRAWVERT_SIZE_32
    SD_USE_INDEX_SIZE_16
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
target_compile_options(etqw_cm PRIVATE
    /W3
    /bigobj
    /GR
    /Zc:wchar_t
    "$<$<CONFIG:Release>:/GS->"
    "$<$<CONFIG:Release>:/Zi>"
)
target_precompile_headers(etqw_cm PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/framework/precompiled.h>"
)
set_target_properties(etqw_cm PROPERTIES
    COMPILE_PDB_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/pdb"
    FOLDER "ETQW Engine/Reconstruction"
)

function(etqw_add_reconstruction_area target_name)
    add_library(${target_name} OBJECT EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
        "${CMAKE_CURRENT_SOURCE_DIR}/framework"
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
        "${CMAKE_CURRENT_SOURCE_DIR}/sys"
        "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
    )
    target_compile_definitions(${target_name} PRIVATE
        WIN32
        _WINDOWS
        _MBCS
        ZLIB_WINAPI
        SD_DEMO_BUILD
        SD_SUPPORT_REPEATER
        SD_SDK_BUILD
        SD_USE_DRAWVERT_SIZE_32
        SD_USE_INDEX_SIZE_16
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
    target_compile_options(${target_name} PRIVATE
        /W3
        /bigobj
        /GR
        /Zc:wchar_t
        "$<$<CONFIG:Release>:/GS->"
        "$<$<CONFIG:Release>:/Zi>"
    )
    target_precompile_headers(${target_name} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/framework/precompiled.h>"
    )
    set_target_properties(${target_name} PROPERTIES
        COMPILE_PDB_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/pdb"
        FOLDER "ETQW Engine/Reconstruction"
    )
endfunction()

etqw_add_reconstruction_area(etqw_framework ${ETQW_FRAMEWORK_BOOTSTRAP_SOURCES})
etqw_add_reconstruction_area(etqw_openal ${ETQW_OPENAL_BOOTSTRAP_SOURCES})
etqw_add_reconstruction_area(etqw_renderer ${ETQW_RENDERER_CORE_SOURCES})
etqw_add_reconstruction_area(etqw_renderer_legacy ${ETQW_RENDERER_BOOTSTRAP_SOURCES})
etqw_add_reconstruction_area(etqw_sound ${ETQW_SOUND_CORE_SOURCES})
etqw_add_reconstruction_area(etqw_sys ${ETQW_SYSTEM_CORE_SOURCES})
etqw_add_reconstruction_area(etqw_sys_legacy ${ETQW_SYS_BOOTSTRAP_SOURCES})
etqw_add_reconstruction_area(etqw_sys_core ${ETQW_SYSTEM_CORE_SOURCES})
etqw_add_reconstruction_area(etqw_engine_support ${ETQW_ENGINE_SUPPORT_SOURCES})
etqw_add_reconstruction_area(etqw_framework_foundation
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/BuildVersion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/CmdSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Compressor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Console.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/CVarSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/DeclManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/DemoFile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/EditField.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/EventLoop.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/FileSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/KeyInput.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/UsercmdGen.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/async/AsyncNetwork.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/async/MsgChannel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/async/NetworkSystem.cpp"
)
etqw_add_reconstruction_area(etqw_framework_common
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Common.cpp"
)
etqw_add_reconstruction_area(etqw_framework_session
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Session.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Session_menu.cpp"
)
etqw_add_reconstruction_area(etqw_framework_base
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/BuildVersion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/CmdSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/Compressor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/CVarSystem.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/KeyInput.cpp"
)

add_executable(etqw WIN32 EXCLUDE_FROM_ALL
    ${ETQW_EXECUTABLE_SOURCES}
)
target_include_directories(etqw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/zlib"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/win32"
)
target_compile_definitions(etqw PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    ZLIB_WINAPI
    SD_DEMO_BUILD
    SD_SUPPORT_REPEATER
    # The public game DLL has the full retail sdNetService vtable. Preserve
    # that ABI while retaining demo guards around unrelated engine systems.
    SD_RETAIL_SDNET_ABI
    # The PDB includes the private PunkBuster SDK. Keep that integration
    # disabled until its symbol-backed compatibility boundary is recreated.
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
    PDB_OUTPUT_DIRECTORY "${ETQW_RUNTIME_DIR}"
    COMPILE_PDB_OUTPUT_DIRECTORY "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/pdb"
    VS_DEBUGGER_WORKING_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
    FOLDER "ETQW Engine"
)
foreach(config_name Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER "${config_name}" config_name_upper)
    set_target_properties(etqw PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_RUNTIME_DIR}"
        "PDB_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_RUNTIME_DIR}"
    )
endforeach()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${ETQW_EXECUTABLE_SOURCES}
    ${ETQW_RENDERER_BOOTSTRAP_SOURCES}
    ${ETQW_CURL_SOURCES}
)

message(STATUS "ETQW engine bootstrap source counts:")
message(STATUS "  reusable engine units: ${ETQW_ENGINE_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  collision units:       ${ETQW_CM_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  framework units:       ${ETQW_FRAMEWORK_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  OpenAL units:          ${ETQW_OPENAL_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  renderer units:        ${ETQW_RENDERER_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  system units:          ${ETQW_SYS_BOOTSTRAP_SOURCE_COUNT}")
message(STATUS "  curl units:            ${ETQW_CURL_SOURCE_COUNT}")
message(STATUS "  libogg units:          ${ETQW_OGG_SOURCE_COUNT}")
message(STATUS "  libvorbis units:       ${ETQW_VORBIS_SOURCE_COUNT}")
message(STATUS "  libtheora units:       ${ETQW_THEORA_SOURCE_COUNT}")
message(STATUS "  Speex units:           ${ETQW_SPEEX_SOURCE_COUNT}")
message(STATUS "  zlib C units:          ${ETQW_ZLIB_SOURCE_COUNT}")
