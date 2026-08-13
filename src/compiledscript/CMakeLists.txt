cmake_minimum_required(VERSION 3.25)

project(ETQWCompiledScript LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ETQW compiled scripts currently support Windows only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ETQW_COMPILED_SCRIPT_SUFFIX "x64")
else()
    set(ETQW_COMPILED_SCRIPT_SUFFIX "x86")
endif()

get_filename_component(ETQW_DEFAULT_WORKSPACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ETQW_WORKSPACE_ROOT "${ETQW_DEFAULT_WORKSPACE_ROOT}" CACHE PATH "ETQW runtime root")

file(GLOB ETQW_COMPILED_SCRIPT_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
file(GLOB ETQW_COMPILED_SCRIPT_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp"
)

if(NOT ETQW_COMPILED_SCRIPT_GENERATED_SOURCES)
    message(FATAL_ERROR
        "No generated scripts were found. Build the top-level compiledscript target first."
    )
endif()

add_library(compiledscript SHARED
    ${ETQW_COMPILED_SCRIPT_RUNTIME_SOURCES}
    ${ETQW_COMPILED_SCRIPT_GENERATED_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts.def"
)

target_include_directories(compiledscript PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_compile_definitions(compiledscript PRIVATE
    WIN32
    _WINDOWS
    _MBCS
    _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
target_compile_options(compiledscript PRIVATE
    /MP
    /bigobj
    /W3
    /GR
    /Zc:wchar_t
    "$<$<CONFIG:Release>:/Zi>"
)
target_precompile_headers(compiledscript PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Precompiled.h"
)
set_target_properties(compiledscript PROPERTIES
    CXX_STANDARD 98
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    OUTPUT_NAME "compiledscript${ETQW_COMPILED_SCRIPT_SUFFIX}"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/lib"
    PDB_OUTPUT_DIRECTORY "${ETQW_WORKSPACE_ROOT}"
)
foreach(config_name Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER "${config_name}" config_name_upper)
    set_target_properties(compiledscript PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_WORKSPACE_ROOT}"
        "PDB_OUTPUT_DIRECTORY_${config_name_upper}" "${ETQW_WORKSPACE_ROOT}"
    )
endforeach()
