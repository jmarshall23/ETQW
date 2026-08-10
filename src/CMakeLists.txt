cmake_minimum_required(VERSION 3.25)

project(
    EnemyTerritoryQuakeWars
    VERSION 1.0.0
    DESCRIPTION "Enemy Territory: Quake Wars source reconstruction"
    LANGUAGES C CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "The public ETQW SDK target currently supports Windows only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "gamex86.dll has a 32-bit ABI. Configure with a Win32 generator "
        "(for example: cmake --preset vs2022-win32)."
    )
endif()

get_filename_component(ETQW_WORKSPACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(
    ETQW_BUILD_STAGING_DIR
    "${ETQW_WORKSPACE_ROOT}/build"
    CACHE PATH
    "Directory for libraries, compile PDBs, and other staged build products"
)
set(
    ETQW_RUNTIME_DIR
    "${ETQW_WORKSPACE_ROOT}"
    CACHE PATH
    "Directory for gamex86.dll and its linker PDB"
)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS YES)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Keep the old SDK target names in the generated Visual Studio solution.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE ETQW_IDLIB_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib/*.cpp"
)

# The source tree also contains Doom 3 reference implementations and SDK
# sources that were disabled in the shipping VS2005 project. Keep this target
# byte-for-byte aligned with idlib_sdk.vcproj's enabled Win32 source set.
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/(Base64|CmdArgs|Lexer|Parser|Str|Token|precompiled)\\.cpp$"
)
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/bv/Frustum_gcc\\.cpp$"
)
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/geometry/DrawVert\\.cpp$"
)
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/(containers/HashIndex|FunctionSignals)\\.cpp$"
)
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/math/Simd_(AltiVec|Xenon)\\.cpp$"
)
list(FILTER ETQW_IDLIB_SOURCES EXCLUDE REGEX
    "/idlib/sys/threading/.*_posix\\.cpp$"
)

file(GLOB ETQW_GAME_DECL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/game/decls/*.cpp"
)
list(APPEND ETQW_GAME_DECL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/decllib/DeclType.cpp"
)

file(GLOB_RECURSE ETQW_GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/game/*.cpp"
)
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX "/game/decls/")
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX "/game/Pvs\\.cpp$")
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX
    "/game/gamesys/(Callbacks|SaveGame)\\.cpp$"
)
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX
    "/game/rules/GameRules_(DeathMatch|Multiplayer)\\.cpp$"
)
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX
    "/game/script/compiledscript/"
)
list(FILTER ETQW_GAME_SOURCES EXCLUDE REGEX
    "/game/weapons/ClientProjectile\\.cpp$"
)
list(APPEND ETQW_GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/BuildVersion.cpp"
)

# Fail at configure time if an accidental source-tree change silently alters
# one of the legacy SDK targets.
list(LENGTH ETQW_IDLIB_SOURCES ETQW_IDLIB_SOURCE_COUNT)
list(LENGTH ETQW_GAME_DECL_SOURCES ETQW_GAME_DECL_SOURCE_COUNT)
list(LENGTH ETQW_GAME_SOURCES ETQW_GAME_SOURCE_COUNT)
if(NOT ETQW_IDLIB_SOURCE_COUNT EQUAL 77)
    message(FATAL_ERROR
        "Expected 77 enabled idLib sources from idlib_sdk.vcproj; "
        "found ${ETQW_IDLIB_SOURCE_COUNT}."
    )
endif()
if(NOT ETQW_GAME_DECL_SOURCE_COUNT EQUAL 37)
    message(FATAL_ERROR
        "Expected 37 libGameDecl sources from libGameDecl_sdk.vcproj; "
        "found ${ETQW_GAME_DECL_SOURCE_COUNT}."
    )
endif()
if(NOT ETQW_GAME_SOURCE_COUNT EQUAL 230)
    message(FATAL_ERROR
        "Expected 230 game sources from game_sdk.vcproj; "
        "found ${ETQW_GAME_SOURCE_COUNT}."
    )
endif()

add_library(idLib STATIC ${ETQW_IDLIB_SOURCES})
add_library(libGameDecl STATIC ${ETQW_GAME_DECL_SOURCES})
add_library(game SHARED
    ${ETQW_GAME_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/game/Game.def"
)

set(ETQW_ALL_TARGETS idLib libGameDecl game)
foreach(target_name IN LISTS ETQW_ALL_TARGETS)
    target_include_directories("${target_name}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_compile_definitions("${target_name}" PRIVATE
        WIN32
        _WINDOWS
        _MBCS
        SD_SDK_BUILD
        SD_PUBLIC_BUILD
        SD_USE_DRAWVERT_SIZE_32
        SD_USE_INDEX_SIZE_16
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
    target_compile_options("${target_name}" PRIVATE
        /MP
        /bigobj
        /GR
        /Zc:wchar_t
        "$<$<CONFIG:Release>:/Zi>"
    )
    set_target_properties("${target_name}" PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/lib"
        COMPILE_PDB_OUTPUT_DIRECTORY
            "${ETQW_BUILD_STAGING_DIR}/$<CONFIG>/pdb"
        FOLDER "ETQW SDK"
    )
endforeach()

target_compile_definitions(idLib PRIVATE __IDLIB__ _LIB)
target_compile_options(idLib PRIVATE /W4)
target_include_directories(idLib PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/idlib"
)
target_precompile_headers(idLib PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/idlib/precompiled.h>"
)

target_compile_definitions(libGameDecl PRIVATE _LIB)
target_compile_options(libGameDecl PRIVATE /W3)
target_precompile_headers(libGameDecl PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/game/decls/precompiled.h>"
)
# DeclType.cpp intentionally uses decllib/precompiled.h and opted out of the
# game-decls PCH in the original Visual Studio project.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/decllib/DeclType.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_compile_definitions(game PRIVATE GAME_DLL)
target_compile_options(game PRIVATE
    /W3
    "$<$<CONFIG:Release>:/GS->"
)
target_include_directories(game PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/game"
)
target_precompile_headers(game PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/game/precompiled.h>"
)
target_link_libraries(game PRIVATE libGameDecl idLib)
target_link_options(game PRIVATE
    /LARGEADDRESSAWARE
    /STACK:4194304,4194304
    /SUBSYSTEM:WINDOWS
    "$<$<CONFIG:Release>:/DEBUG:FULL>"
    "$<$<CONFIG:Release>:/MAP:${ETQW_RUNTIME_DIR}/gamex86.map>"
)
set_target_properties(game PROPERTIES
    OUTPUT_NAME "gamex86"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${ETQW_RUNTIME_DIR}"
    PDB_OUTPUT_DIRECTORY "${ETQW_RUNTIME_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
)
# Multi-config generators append the configuration name unless each
# configuration has an explicit output path. The retail layout expects the
# active gamex86.dll directly beside the executable.
foreach(config_name Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER "${config_name}" config_name_upper)
    set_target_properties(game PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_RUNTIME_DIR}"
        "PDB_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_RUNTIME_DIR}"
    )
endforeach()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${ETQW_IDLIB_SOURCES}
    ${ETQW_GAME_DECL_SOURCES}
    ${ETQW_GAME_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/game/Game.def"
)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT game)

option(
    ETQW_ENABLE_ENGINE_RECONSTRUCTION
    "Generate the symbol-backed ETQW engine bootstrap target"
    ON
)
if(ETQW_ENABLE_ENGINE_RECONSTRUCTION)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EtqwEngine.cmake")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT etqw)
endif()

message(STATUS "ETQW SDK source counts:")
message(STATUS "  idLib:       ${ETQW_IDLIB_SOURCE_COUNT}")
message(STATUS "  libGameDecl: ${ETQW_GAME_DECL_SOURCE_COUNT}")
message(STATUS "  game:        ${ETQW_GAME_SOURCE_COUNT}")
message(STATUS "Runtime output: ${ETQW_RUNTIME_DIR}")
message(STATUS "Build staging: ${ETQW_BUILD_STAGING_DIR}")
