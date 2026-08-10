cmake_minimum_required(VERSION 3.20)

project(ib VERSION 4.9 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -----------------------------------------------------------------------------
# Project layout
# -----------------------------------------------------------------------------
set(IB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Compatibility for existing subdirectory CMake files.
set(PROJECT_ROOT "${IB_ROOT}")

set(SRC_DIR "${IB_ROOT}/src")
set(DOCTYPE_DIR "${IB_ROOT}/doctype")

set(
    IB_RUNTIME_OUTPUT_DIRECTORY
    "${IB_ROOT}/bin"
    CACHE PATH
    "Directory for executables produced by this build"
)

set(
    IB_INSTALL_BINDIR
    "${CMAKE_INSTALL_BINDIR}"
    CACHE STRING
    "Relative install directory for ib executables"
)

set(
    IB_INSTALL_LIBDIR
    "${CMAKE_INSTALL_LIBDIR}/ib"
    CACHE STRING
    "Relative install directory for ib libraries"
)

message(STATUS "[ib] Source root       : ${IB_ROOT}")
message(STATUS "[ib] Runtime output    : ${IB_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "[ib] Install binaries  : ${IB_INSTALL_BINDIR}")
message(STATUS "[ib] Install libraries : ${IB_INSTALL_LIBDIR}")

# -----------------------------------------------------------------------------
# Supported platforms
# -----------------------------------------------------------------------------
if(APPLE)
    set(IB_PLATFORM_MACOS TRUE)
    message(STATUS "[ib] Platform: macOS")
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD|DragonFly")
    set(IB_PLATFORM_BSD TRUE)
    message(STATUS "[ib] Platform: BSD (${CMAKE_SYSTEM_NAME})")
elseif(UNIX)
    set(IB_PLATFORM_LINUX TRUE)
    message(STATUS "[ib] Platform: Linux/Unix (${CMAKE_SYSTEM_NAME})")
else()
    message(FATAL_ERROR "ib currently supports macOS, Linux/Unix, and BSD")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IB_64_BIT_TARGET TRUE)
    message(STATUS "[ib] Target width: 64-bit")
else()
    set(IB_64_BIT_TARGET FALSE)
    message(STATUS "[ib] Target width: ${CMAKE_SIZEOF_VOID_P}-byte pointers")
endif()

# -----------------------------------------------------------------------------
# Native platform configuration header
# -----------------------------------------------------------------------------
# utils/configure.c measures the target C ABI and writes ../src/conf.h.inc.
# Run it while CMake is configuring so every library and plugin sees the header
# before any source compilation starts.
set(IB_CONF_HEADER "${SRC_DIR}/conf.h.inc")
set(IB_PREGENERATED_CONF_HEADER "" CACHE FILEPATH
    "Target-specific conf.h.inc to use when cross-compiling without an emulator"
)

if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    if(NOT IB_PREGENERATED_CONF_HEADER)
        message(FATAL_ERROR
            "Cross-compiling requires a target-generated conf.h.inc. "
            "Set IB_PREGENERATED_CONF_HEADER to a conf.h.inc produced by "
            "utils/configure.c on the target, or set CMAKE_CROSSCOMPILING_EMULATOR."
        )
    endif()

    if(NOT EXISTS "${IB_PREGENERATED_CONF_HEADER}")
        message(FATAL_ERROR
            "IB_PREGENERATED_CONF_HEADER does not exist: "
            "${IB_PREGENERATED_CONF_HEADER}"
        )
    endif()

    configure_file(
        "${IB_PREGENERATED_CONF_HEADER}"
        "${IB_CONF_HEADER}"
        COPYONLY
    )
else()
    try_run(
        IB_CONFIGURE_RUN_RESULT
        IB_CONFIGURE_COMPILE_RESULT
        "${CMAKE_BINARY_DIR}/CMakeFiles/ib-platform-config"
        "${IB_ROOT}/utils/configure.c"
        CMAKE_FLAGS
            "-DCMAKE_C_STANDARD=11"
            "-DCMAKE_C_STANDARD_REQUIRED=ON"
            "-DCMAKE_C_EXTENSIONS=OFF"
        COMPILE_OUTPUT_VARIABLE IB_CONFIGURE_COMPILE_OUTPUT
        RUN_OUTPUT_VARIABLE IB_CONFIGURE_RUN_OUTPUT
        WORKING_DIRECTORY "${IB_ROOT}/utils"
    )

    if(NOT IB_CONFIGURE_COMPILE_RESULT)
        message(FATAL_ERROR
            "Could not compile utils/configure.c:\n"
            "${IB_CONFIGURE_COMPILE_OUTPUT}"
        )
    endif()

    if(IB_CONFIGURE_RUN_RESULT STREQUAL "FAILED_TO_RUN" OR
       NOT IB_CONFIGURE_RUN_RESULT EQUAL 0)
        message(FATAL_ERROR
            "utils/configure.c failed with result ${IB_CONFIGURE_RUN_RESULT}:\n"
            "${IB_CONFIGURE_RUN_OUTPUT}"
        )
    endif()
endif()

if(NOT EXISTS "${IB_CONF_HEADER}")
    message(FATAL_ERROR
        "Platform probe completed without creating ${IB_CONF_HEADER}"
    )
endif()

file(STRINGS "${IB_CONF_HEADER}" _IB_CONF_HEADER_SANITY
    REGEX "^#define SIZEOF_(CHAR|INT|LONG_INT) "
)
if(NOT _IB_CONF_HEADER_SANITY)
    message(FATAL_ERROR
        "Generated ${IB_CONF_HEADER} does not contain the expected size macros"
    )
endif()

message(STATUS "[ib] Platform configuration: ${IB_CONF_HEADER}")

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
# A standalone ib checkout normally has no Schmate target or installed
# libschmate.  Enable vector support automatically only when the parent build
# has already provided one of the known internal targets.  Users can still
# request it explicitly with -DBUILD_VECTOR_INDEX=ON.
if(TARGET sbert_extended OR TARGET bert_model)
    set(_IB_DEFAULT_VECTOR_INDEX ON)
else()
    set(_IB_DEFAULT_VECTOR_INDEX OFF)
endif()

option(BUILD_VECTOR_INDEX
    "Build with HNSW/vector index support (requires Schmate)"
    ${_IB_DEFAULT_VECTOR_INDEX}
)
option(BUILD_SHARED_LIBS  "Build shared libraries"               ON)
option(BUILD_LARGE_INDEX "Use 64-bit index IDs; changes ABI and cache/index formats" OFF)

option(USE_LIBMAGIC    "Use libmagic for file type detection" ON)
option(USE_JEMALLOC    "Use jemalloc allocator"              ON)
option(USE_OPENMP      "Use OpenMP"                           ON)
option(USE_BERKELEY_DB "Use Berkeley DB"                      ON)

option(IB_ENABLE_FAST_MATH "Enable -ffast-math in optimized GNU/Clang builds" ON)

option(IB_CREATE_USER_PLUGIN_DIR "Create $HOME/.ib/lib/plugins while configuring" OFF)

if(APPLE)
    set(_IB_DEFAULT_HOMEBREW_HINTS ON)
else()
    set(_IB_DEFAULT_HOMEBREW_HINTS OFF)
endif()

option(IB_USE_HOMEBREW_HINTS
    "Use Homebrew prefixes as optional dependency hints on macOS"
    ${_IB_DEFAULT_HOMEBREW_HINTS}
)

if(BUILD_LARGE_INDEX AND NOT IB_64_BIT_TARGET)
    message(FATAL_ERROR
        "BUILD_LARGE_INDEX requires a 64-bit target platform"
    )
endif()

# LARGE_INDEX changes public types. It must be visible to every target,
# including targets created in doctype/plugins.
set(IB_COMPILE_DEFINITIONS
    SVR4
    WWW_SUPPORT
    __CSTRING__
    BSDN_EXTENSIONS=2
)

if(IB_PLATFORM_MACOS)
    list(APPEND IB_COMPILE_DEFINITIONS BSD)
endif()

if(IB_64_BIT_TARGET)
    # O_BUILD_IB64 is the spelling used by the index-ID declarations.
    # Keep DO_BUILD_IB64 for older source files which may still test it.
    list(APPEND IB_COMPILE_DEFINITIONS
        O_BUILD_IB64=1
        DO_BUILD_IB64=1
    )
endif()

if(BUILD_LARGE_INDEX)
    list(APPEND IB_COMPILE_DEFINITIONS LARGE_INDEX=1)
    message(STATUS "[ib] Index IDs: 64-bit LARGE_INDEX")
else()
    message(STATUS "[ib] Index IDs: normal 32-bit")
endif()

if(USE_LIBMAGIC)
    list(APPEND IB_COMPILE_DEFINITIONS USE_LIBMAGIC=1)
endif()

if(BUILD_VECTOR_INDEX)
    list(APPEND IB_COMPILE_DEFINITIONS VECTOR_INDEX=1)
endif()

add_compile_definitions(${IB_COMPILE_DEFINITIONS})

# -----------------------------------------------------------------------------
# Compiler options
# -----------------------------------------------------------------------------
add_compile_options(
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-O3>
)

if(IB_ENABLE_FAST_MATH)
    add_compile_options(
        $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>>:-ffast-math>
        $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>>:-ffast-math>
    )
endif()

if(IB_PLATFORM_MACOS)
    add_compile_options(
        $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<COMPILE_LANG_AND_ID:C,AppleClang>>:-fvectorize>
        $<$<AND:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<COMPILE_LANG_AND_ID:CXX,AppleClang>>:-fvectorize>
    )
endif()

# -----------------------------------------------------------------------------
# Portable runtime paths
# -----------------------------------------------------------------------------
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_NAME_DIR "@rpath")
endif()

function(ib_set_library_rpath target_name)
    if(APPLE)
        set_target_properties(${target_name} PROPERTIES
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(${target_name} PROPERTIES
            INSTALL_RPATH "\$ORIGIN"
        )
    endif()
endfunction()

function(ib_set_executable_rpath target_name)
    if(IS_ABSOLUTE "${IB_INSTALL_LIBDIR}")
        set(_ib_executable_rpath "${IB_INSTALL_LIBDIR}")
    elseif(APPLE)
        set(_ib_executable_rpath
            "@loader_path/../${IB_INSTALL_LIBDIR}"
        )
    elseif(UNIX)
        set(_ib_executable_rpath
            "\$ORIGIN/../${IB_INSTALL_LIBDIR}"
        )
    else()
        set(_ib_executable_rpath "")
    endif()

    if(_ib_executable_rpath)
        set_target_properties(${target_name} PROPERTIES
            INSTALL_RPATH "${_ib_executable_rpath}"
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Portable dependency discovery
#
# Standard CMake and pkg-config search paths are always used.  Explicit cache
# roots are optional hints, not required installation locations.  Homebrew is
# consulted only on macOS and only when IB_USE_HOMEBREW_HINTS is enabled.
# This keeps Linux, BSD, cross-compilation, and Jetson/sysroot builds native.
# -----------------------------------------------------------------------------
find_package(PkgConfig QUIET)

set(BERKELEY_DB_ROOT "" CACHE PATH
    "Optional Berkeley DB installation prefix"
)
set(LIBMAGIC_ROOT "" CACHE PATH
    "Optional libmagic installation prefix"
)
set(JEMALLOC_ROOT "" CACHE PATH
    "Optional jemalloc installation prefix"
)
set(GDBM_ROOT "" CACHE PATH
    "Optional GNU dbm installation prefix"
)
set(OPENSSL_ROOT "" CACHE PATH
    "Optional OpenSSL installation prefix used by plugins"
)

set(_IB_BDB_HINTS "")
set(_IB_MAGIC_HINTS "")
set(_IB_JEMALLOC_HINTS "")
set(_IB_GDBM_HINTS "")
set(_IB_OPENMP_HINTS "")
set(_IB_OPENSSL_HINTS "")

foreach(_ib_pair
        "BERKELEY_DB_ROOT;_IB_BDB_HINTS"
        "LIBMAGIC_ROOT;_IB_MAGIC_HINTS"
        "JEMALLOC_ROOT;_IB_JEMALLOC_HINTS"
        "GDBM_ROOT;_IB_GDBM_HINTS"
        "OPENSSL_ROOT;_IB_OPENSSL_HINTS")
    list(GET _ib_pair 0 _ib_root_variable)
    list(GET _ib_pair 1 _ib_hint_variable)
    if(NOT "${${_ib_root_variable}}" STREQUAL "")
        list(APPEND ${_ib_hint_variable} "${${_ib_root_variable}}")
    endif()
endforeach()

if(APPLE AND IB_USE_HOMEBREW_HINTS)
    find_program(IB_HOMEBREW_EXECUTABLE NAMES brew)

    if(IB_HOMEBREW_EXECUTABLE)
        function(ib_add_homebrew_hint package_name hint_variable)
            execute_process(
                COMMAND "${IB_HOMEBREW_EXECUTABLE}" --prefix "${package_name}"
                RESULT_VARIABLE _ib_brew_result
                OUTPUT_VARIABLE _ib_brew_prefix
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            if(_ib_brew_result EQUAL 0 AND IS_DIRECTORY "${_ib_brew_prefix}")
                set(_ib_hints "${${hint_variable}}")
                list(APPEND _ib_hints "${_ib_brew_prefix}")
                set(${hint_variable} "${_ib_hints}" PARENT_SCOPE)
            endif()
        endfunction()

        ib_add_homebrew_hint("berkeley-db" _IB_BDB_HINTS)
        ib_add_homebrew_hint("libmagic"    _IB_MAGIC_HINTS)
        ib_add_homebrew_hint("jemalloc"    _IB_JEMALLOC_HINTS)
        ib_add_homebrew_hint("gdbm"        _IB_GDBM_HINTS)
        ib_add_homebrew_hint("libomp"      _IB_OPENMP_HINTS)
        ib_add_homebrew_hint("openssl@3"   _IB_OPENSSL_HINTS)
    else()
        message(STATUS "[ib] Homebrew not found; using normal CMake search paths")
    endif()
endif()

# -----------------------------------------------------------------------------
# Core dependencies
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

if(USE_OPENMP)
    # AppleClang commonly needs the libomp prefix as a hint.  GCC/Clang on
    # Linux, including Jetson aarch64 systems, is discovered normally.
    set(_ib_saved_prefix_path "${CMAKE_PREFIX_PATH}")
    if(_IB_OPENMP_HINTS)
        list(PREPEND CMAKE_PREFIX_PATH ${_IB_OPENMP_HINTS})
    endif()
    find_package(OpenMP REQUIRED COMPONENTS C CXX)
    set(CMAKE_PREFIX_PATH "${_ib_saved_prefix_path}")
endif()

if(USE_BERKELEY_DB)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_BDB QUIET db)
    endif()

    find_path(BDB_INCLUDE
        NAMES db.h
        HINTS ${_IB_BDB_HINTS} ${PC_BDB_INCLUDE_DIRS}
        PATH_SUFFIXES include include/db18
        REQUIRED
    )
    find_library(BDB_LIB
        NAMES db db-18.1 db-6.2 db-5.3
        HINTS ${_IB_BDB_HINTS} ${PC_BDB_LIBRARY_DIRS}
        PATH_SUFFIXES lib lib64
        REQUIRED
    )
    message(STATUS "[ib] Berkeley DB: ${BDB_LIB}")
endif()

if(USE_LIBMAGIC)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_MAGIC QUIET libmagic)
    endif()

    find_path(MAGIC_INCLUDE
        NAMES magic.h
        HINTS ${_IB_MAGIC_HINTS} ${PC_MAGIC_INCLUDE_DIRS}
        PATH_SUFFIXES include
        REQUIRED
    )
    find_library(MAGIC_LIB
        NAMES magic
        HINTS ${_IB_MAGIC_HINTS} ${PC_MAGIC_LIBRARY_DIRS}
        PATH_SUFFIXES lib lib64
        REQUIRED
    )
    message(STATUS "[ib] libmagic: ${MAGIC_LIB}")
endif()

if(USE_JEMALLOC)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_JEMALLOC QUIET jemalloc)
    endif()

    find_library(JEMALLOC_LIB
        NAMES jemalloc
        HINTS ${_IB_JEMALLOC_HINTS} ${PC_JEMALLOC_LIBRARY_DIRS}
        PATH_SUFFIXES lib lib64
        REQUIRED
    )
    message(STATUS "[ib] jemalloc: ${JEMALLOC_LIB}")
endif()

# db_string.hxx includes <gdbm.h> unconditionally and db_string.cxx uses the
# GDBM API, so GNU dbm is a required core dependency rather than an optional
# feature.  Discover both the headers and library at configure time.
if(PkgConfig_FOUND)
    pkg_check_modules(PC_GDBM QUIET gdbm)
endif()

find_path(GDBM_INCLUDE
    NAMES gdbm.h
    HINTS ${_IB_GDBM_HINTS} ${PC_GDBM_INCLUDE_DIRS}
    PATH_SUFFIXES include
)
find_library(GDBM_LIB
    NAMES gdbm
    HINTS ${_IB_GDBM_HINTS} ${PC_GDBM_LIBRARY_DIRS}
    PATH_SUFFIXES lib lib64
)

if(NOT GDBM_INCLUDE OR NOT GDBM_LIB)
    message(FATAL_ERROR
        "GNU dbm development files were not found. "
        "Install gdbm (macOS: 'brew install gdbm'; "
        "Debian/Ubuntu/Jetson: 'sudo apt install libgdbm-dev') "
        "or configure with -DGDBM_ROOT=/path/to/gdbm."
    )
endif()

message(STATUS "[ib] GNU dbm headers: ${GDBM_INCLUDE}")
message(STATUS "[ib] GNU dbm library: ${GDBM_LIB}")

# Existing plugin CMake files consume OPENSSL_INCLUDE, SSL_LIB, and CRYPTO_LIB.
# Prefer CMake's portable OpenSSL package, then fall back to ordinary searches.
set(_ib_saved_prefix_path "${CMAKE_PREFIX_PATH}")
if(_IB_OPENSSL_HINTS)
    list(PREPEND CMAKE_PREFIX_PATH ${_IB_OPENSSL_HINTS})
endif()
find_package(OpenSSL QUIET)
set(CMAKE_PREFIX_PATH "${_ib_saved_prefix_path}")

if(OpenSSL_FOUND)
    set(OPENSSL_INCLUDE "${OPENSSL_INCLUDE_DIR}")
    set(SSL_LIB OpenSSL::SSL)
    set(CRYPTO_LIB OpenSSL::Crypto)
else()
    find_path(OPENSSL_INCLUDE
        NAMES openssl/ssl.h
        HINTS ${_IB_OPENSSL_HINTS}
        PATH_SUFFIXES include
    )
    find_library(SSL_LIB
        NAMES ssl
        HINTS ${_IB_OPENSSL_HINTS}
        PATH_SUFFIXES lib lib64
    )
    find_library(CRYPTO_LIB
        NAMES crypto
        HINTS ${_IB_OPENSSL_HINTS}
        PATH_SUFFIXES lib lib64
    )
endif()

# -----------------------------------------------------------------------------
# Schmate/vector support
# -----------------------------------------------------------------------------
set(SCHMATE_TARGET "")

if(BUILD_VECTOR_INDEX)
    if(TARGET sbert_extended)
        set(SCHMATE_TARGET sbert_extended)
        message(STATUS "[ib] Vector support: target sbert_extended")
    elseif(TARGET bert_model)
        set(SCHMATE_TARGET bert_model)
        message(STATUS "[ib] Vector support: target bert_model")
    else()
        if(NOT SCHMATE_INCLUDE_DIR)
            if(IS_DIRECTORY "${SCHMATE_ROOT}/include")
                set(SCHMATE_INCLUDE_DIR "${SCHMATE_ROOT}/include")
            endif()
        endif()

        if(NOT SCHMATE_LIBRARY)
            find_library(_IB_DISCOVERED_SCHMATE_LIBRARY
                NAMES schmate
                HINTS
                    "${SCHMATE_ROOT}"
                    "${SCHMATE_ROOT}/lib"
                    "${SCHMATE_ROOT}/build"
                PATH_SUFFIXES
                    lib
                    lib64
                    build
            )
            if(_IB_DISCOVERED_SCHMATE_LIBRARY)
                set(SCHMATE_LIBRARY
                    "${_IB_DISCOVERED_SCHMATE_LIBRARY}"
                    CACHE FILEPATH
                    "Path to an external Schmate library"
                    FORCE
                )
            endif()
        endif()

        if(NOT SCHMATE_LIBRARY OR NOT IS_DIRECTORY "${SCHMATE_INCLUDE_DIR}")
            message(FATAL_ERROR
                "BUILD_VECTOR_INDEX is ON, but Schmate was not found. "
                "Either configure the standalone ib build with "
                "-DBUILD_VECTOR_INDEX=OFF, set -DSCHMATE_ROOT=/path/to/Schmate, "
                "set both -DSCHMATE_INCLUDE_DIR=... and -DSCHMATE_LIBRARY=..., "
                "or provide an internal sbert_extended/bert_model target."
            )
        endif()

        message(STATUS "[ib] Vector support: ${SCHMATE_LIBRARY}")
        message(STATUS "[ib] Schmate includes: ${SCHMATE_INCLUDE_DIR}")
    endif()
endif()

function(ib_link_vector target_name visibility)
    if(NOT BUILD_VECTOR_INDEX)
        return()
    endif()

    if(SCHMATE_TARGET)
        target_link_libraries(
            ${target_name}
            ${visibility}
            ${SCHMATE_TARGET}
        )
    else()
        target_include_directories(
            ${target_name}
            ${visibility}
            "${SCHMATE_INCLUDE_DIR}"
        )
        target_link_libraries(
            ${target_name}
            ${visibility}
            "${SCHMATE_LIBRARY}"
        )
    endif()
endfunction()

function(ib_force_load_static_dependency target_name dependency)
    if(NOT APPLE OR NOT dependency)
        return()
    endif()

    if(TARGET "${dependency}")
        get_target_property(_ib_dependency_type "${dependency}" TYPE)
        if(_ib_dependency_type STREQUAL "STATIC_LIBRARY")
            target_link_options(
                ${target_name}
                PRIVATE
                "LINKER:-force_load,$<TARGET_FILE:${dependency}>"
            )
        endif()
    elseif(EXISTS "${dependency}" AND "${dependency}" MATCHES "\\.a$")
        target_link_options(
            ${target_name}
            PRIVATE
            "LINKER:-force_load,${dependency}"
        )
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Target helpers
# -----------------------------------------------------------------------------
function(ib_common_includes target_name)
    target_include_directories(
        ${target_name}
        PUBLIC
            "${SRC_DIR}"
            "${DOCTYPE_DIR}"
    )
endfunction()

function(ib_configure_library target_name)
    ib_set_library_rpath(${target_name})

    # The historical ib component libraries have intentional cross-library
    # references (for example ibUtils references symbols supplied by ibLocal
    # and ibSearch, while those libraries also depend on ibUtils).  Darwin
    # requires these references to remain unresolved until the final program
    # loads the complete ib library set.  Keep this relaxation target-scoped;
    # do not leak it into executables, plugins, or third-party targets.
    if(APPLE AND BUILD_SHARED_LIBS)
        target_link_options(
            ${target_name}
            PRIVATE
            "LINKER:-undefined,dynamic_lookup"
        )
    endif()
endfunction()

function(ib_configure_executable target_name)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${IB_RUNTIME_OUTPUT_DIRECTORY}"
    )
    ib_set_executable_rpath(${target_name})

    if(APPLE AND BUILD_VECTOR_INDEX)
        if(SCHMATE_TARGET)
            ib_force_load_static_dependency(
                ${target_name}
                "${SCHMATE_TARGET}"
            )
        else()
            ib_force_load_static_dependency(
                ${target_name}
                "${SCHMATE_LIBRARY}"
            )
        endif()

        if(TARGET bert_model)
            ib_force_load_static_dependency(
                ${target_name}
                bert_model
            )
        endif()
    endif()
endfunction()

# ─────────────────────────────────────────────
# ibUtils — base/utility library
# ─────────────────────────────────────────────
add_library(ibUtils
    ${SRC_DIR}/string.cxx       ${SRC_DIR}/fuzzy.cxx
    ${SRC_DIR}/qsort.cxx        ${SRC_DIR}/qsort_simd.cxx
    ${SRC_DIR}/vlist.cxx
    ${SRC_DIR}/plist.cxx        ${SRC_DIR}/common.cxx
    ${SRC_DIR}/error.cxx        ${SRC_DIR}/glob.cxx
    ${SRC_DIR}/fieldmatch.cxx   ${SRC_DIR}/pathname.cxx
    ${SRC_DIR}/inode.cxx        ${SRC_DIR}/numbers.cxx
    ${SRC_DIR}/snefru.cxx       ${SRC_DIR}/directory.cxx
    ${SRC_DIR}/logger.cxx       ${SRC_DIR}/mmap.cxx
    ${SRC_DIR}/object.cxx       ${SRC_DIR}/dictionary.cxx
    ${SRC_DIR}/strlist.cxx      ${SRC_DIR}/strstack.cxx
    ${SRC_DIR}/lang-codes.cxx   ${SRC_DIR}/unicode.cxx
    ${SRC_DIR}/utf8_utils.cxx 
    ${SRC_DIR}/htmlcat.cxx      ${SRC_DIR}/date.cxx
    ${SRC_DIR}/listof.cxx       ${SRC_DIR}/hash.cxx
    ${SRC_DIR}/hashof.cxx       ${SRC_DIR}/buffer.cxx
    ${SRC_DIR}/glist.cxx        ${SRC_DIR}/gstack.cxx
    ${SRC_DIR}/registry.cxx     ${SRC_DIR}/fprec.cxx
    ${SRC_DIR}/fpt.cxx          ${SRC_DIR}/crc64.cxx
    ${SRC_DIR}/utf8.cxx         ${SRC_DIR}/eudex.cxx
    ${SRC_DIR}/metaphone.cxx
)
ib_common_includes(ibUtils)

# ─────────────────────────────────────────────
# ibIO — network/IO library
# ─────────────────────────────────────────────
set(IO_SRCS
    ${SRC_DIR}/io.cxx         ${SRC_DIR}/protman.cxx
    ${SRC_DIR}/netbox.cxx     ${SRC_DIR}/strbox.cxx
    ${SRC_DIR}/treebox.cxx
)
if(USE_BERKELEY_DB)
    list(APPEND IO_SRCS ${SRC_DIR}/rldcache.cxx)
endif()
add_library(ibIO ${IO_SRCS})
ib_common_includes(ibIO)
target_link_libraries(ibIO PUBLIC ibUtils)
if(USE_BERKELEY_DB)
    target_include_directories(ibIO PUBLIC ${BDB_INCLUDE})
    target_link_libraries(ibIO PUBLIC ${BDB_LIB})
endif()

# ─────────────────────────────────────────────
# ibProcess
# ─────────────────────────────────────────────
add_library(ibProcess
    ${SRC_DIR}/popen.cxx
    ${SRC_DIR}/process.cxx
)
ib_common_includes(ibProcess)
target_link_libraries(ibProcess PUBLIC ibUtils)

# ─────────────────────────────────────────────
# ibLocal — ctype/ranking (C sources)
# ─────────────────────────────────────────────
add_library(ibLocal
    ${SRC_DIR}/ctype.c
    ${SRC_DIR}/ranking.c
    ${SRC_DIR}/extras.c
)
ib_common_includes(ibLocal)

# ─────────────────────────────────────────────
# ibSearch — search engine library
# ─────────────────────────────────────────────
add_library(ibSearch
    ${SRC_DIR}/attr.cxx         ${SRC_DIR}/attrlist.cxx
    ${SRC_DIR}/df.cxx           ${SRC_DIR}/dfd.cxx
    ${SRC_DIR}/dfdt.cxx         ${SRC_DIR}/dft.cxx
    ${SRC_DIR}/fc.cxx           ${SRC_DIR}/fct.cxx
    ${SRC_DIR}/fchits.cxx
    ${SRC_DIR}/nodetree.cxx     ${SRC_DIR}/idbobj.cxx
    ${SRC_DIR}/idb.cxx          ${SRC_DIR}/metadata.cxx
    ${SRC_DIR}/scan.cxx         ${SRC_DIR}/vidb.cxx
    ${SRC_DIR}/index.cxx        ${SRC_DIR}/db_string.cxx
    ${SRC_DIR}/index_sort.cxx
    ${SRC_DIR}/numsearch.cxx    ${SRC_DIR}/datesearch.cxx
    ${SRC_DIR}/dsearch.cxx      ${SRC_DIR}/geosearch.cxx
    ${SRC_DIR}/nlist.cxx        ${SRC_DIR}/dlist.cxx
    ${SRC_DIR}/nfldmgr.cxx      ${SRC_DIR}/intlist.cxx
    ${SRC_DIR}/intfield.cxx     ${SRC_DIR}/gpolylist.cxx
    ${SRC_DIR}/gpolyfield.cxx   ${SRC_DIR}/bboxfield.cxx
    ${SRC_DIR}/bboxlist.cxx     ${SRC_DIR}/nfield.cxx
    ${SRC_DIR}/dfield.cxx       ${SRC_DIR}/lock.cxx
    ${SRC_DIR}/iresult.cxx      ${SRC_DIR}/resource.cxx
    ${SRC_DIR}/irset.cxx        ${SRC_DIR}/mdt.cxx
    ${SRC_DIR}/mdthashtable.cxx ${SRC_DIR}/mdtrec.cxx
    ${SRC_DIR}/opobj.cxx        ${SRC_DIR}/operand.cxx
    ${SRC_DIR}/operator.cxx     ${SRC_DIR}/opstack.cxx
    ${SRC_DIR}/reclist.cxx      ${SRC_DIR}/record.cxx
    ${SRC_DIR}/recordsyntax.cxx ${SRC_DIR}/result.cxx
    ${SRC_DIR}/rset.cxx         ${SRC_DIR}/soundex.cxx
    ${SRC_DIR}/squery.cxx       ${SRC_DIR}/thesaurus.cxx
    ${SRC_DIR}/sterm.cxx        ${SRC_DIR}/termobj.cxx
    ${SRC_DIR}/rcache.cxx       ${SRC_DIR}/fcache.cxx
    ${SRC_DIR}/filemap.cxx      ${SRC_DIR}/mergeunit.cxx
    ${SRC_DIR}/infix2rpn.cxx    ${SRC_DIR}/tokengen.cxx
    ${SRC_DIR}/merge.cxx        ${SRC_DIR}/stoplist.cxx
    ${SRC_DIR}/support.cxx      ${SRC_DIR}/defs.cxx
)
ib_common_includes(ibSearch)
target_include_directories(ibSearch PUBLIC "${GDBM_INCLUDE}")
target_link_libraries(
    ibSearch
    PUBLIC
        ibUtils
        ibLocal
        Threads::Threads
        "${GDBM_LIB}"
)
if(USE_OPENMP)
    target_link_libraries(ibSearch PUBLIC OpenMP::OpenMP_CXX)
endif()
if(USE_LIBMAGIC)
    target_include_directories(ibSearch PUBLIC ${MAGIC_INCLUDE})
    target_link_libraries(ibSearch PUBLIC ${MAGIC_LIB})
endif()

if(BUILD_VECTOR_INDEX)
    ib_link_vector(ibSearch PUBLIC)
endif()


# ─────────────────────────────────────────────
# ibDoc — document type handlers
# ─────────────────────────────────────────────
add_library(ibDoc
    ${SRC_DIR}/dtreg.cxx            ${DOCTYPE_DIR}/ParseWordsUTF8.cxx
    ${DOCTYPE_DIR}/doctype.cxx      ${DOCTYPE_DIR}/autodetect.cxx
    ${DOCTYPE_DIR}/resourcedoc.cxx  ${DOCTYPE_DIR}/image.cxx
    ${DOCTYPE_DIR}/plaintext.cxx    ${DOCTYPE_DIR}/simple.cxx
    ${DOCTYPE_DIR}/para.cxx         ${DOCTYPE_DIR}/ptext.cxx
    ${DOCTYPE_DIR}/binary.cxx       ${DOCTYPE_DIR}/filter.cxx
    ${DOCTYPE_DIR}/xfilter.cxx      ${DOCTYPE_DIR}/ftp.cxx
    ${DOCTYPE_DIR}/bibtex.cxx       ${DOCTYPE_DIR}/firstline.cxx
    ${DOCTYPE_DIR}/colondoc.cxx     ${DOCTYPE_DIR}/tsldoc.cxx
    ${DOCTYPE_DIR}/csvdoc.cxx       ${DOCTYPE_DIR}/ozsearch.cxx
    ${DOCTYPE_DIR}/psdoc.cxx        ${DOCTYPE_DIR}/iknowdoc.cxx
    ${DOCTYPE_DIR}/roadsdoc.cxx     ${DOCTYPE_DIR}/colongrp.cxx
    ${DOCTYPE_DIR}/euromedia.cxx    ${DOCTYPE_DIR}/mismedia.cxx
    ${DOCTYPE_DIR}/bibcolon.cxx     ${DOCTYPE_DIR}/dif.cxx
    ${DOCTYPE_DIR}/iafadoc.cxx      ${DOCTYPE_DIR}/mailfolder.cxx
    ${DOCTYPE_DIR}/newsfolder.cxx   ${DOCTYPE_DIR}/referbib.cxx
    ${DOCTYPE_DIR}/irlist.cxx       ${DOCTYPE_DIR}/mailman.cxx
    ${DOCTYPE_DIR}/listdigest.cxx   ${DOCTYPE_DIR}/maildigest.cxx
    ${DOCTYPE_DIR}/yahoolist.cxx    ${DOCTYPE_DIR}/digesttoc.cxx
    ${DOCTYPE_DIR}/medline.cxx      ${DOCTYPE_DIR}/filmline.cxx
    ${DOCTYPE_DIR}/oneline.cxx      ${DOCTYPE_DIR}/dvbline.cxx
    ${DOCTYPE_DIR}/memodoc.cxx      ${DOCTYPE_DIR}/soif.cxx
    ${DOCTYPE_DIR}/harvest.cxx      ${DOCTYPE_DIR}/sgmltag.cxx
    ${DOCTYPE_DIR}/gils.cxx         ${DOCTYPE_DIR}/sgmlnorm.cxx
    ${DOCTYPE_DIR}/sgml.cxx         ${DOCTYPE_DIR}/xml.cxx
    ${DOCTYPE_DIR}/gilsxml.cxx      ${DOCTYPE_DIR}/gils_isoteia.cxx
    ${DOCTYPE_DIR}/rss2.cxx         ${DOCTYPE_DIR}/html.cxx
    ${DOCTYPE_DIR}/HTMLEntities.cxx ${DOCTYPE_DIR}/htmlmeta.cxx
    ${DOCTYPE_DIR}/htmlhead.cxx     ${DOCTYPE_DIR}/htmlremote.cxx
    ${DOCTYPE_DIR}/antihtml.cxx     ${DOCTYPE_DIR}/htmlcache.cxx
    ${DOCTYPE_DIR}/filter2.cxx      ${DOCTYPE_DIR}/pandoc.cxx
    ${DOCTYPE_DIR}/pdf.cxx
    ${DOCTYPE_DIR}/jsondoc.cxx
    ${DOCTYPE_DIR}/jsonlddoc.cxx
    ${DOCTYPE_DIR}/ejsondoc.cxx
)
ib_common_includes(ibDoc)
target_link_libraries(ibDoc PUBLIC ibUtils ibLocal)

# binary.hxx exposes libmagic types when USE_LIBMAGIC is enabled, and
# binary.cxx is compiled into ibDoc.  The include directory and library
# therefore belong to ibDoc (PUBLIC because the header is public).
if(USE_LIBMAGIC)
    target_include_directories(ibDoc PUBLIC "${MAGIC_INCLUDE}")
    target_link_libraries(ibDoc PUBLIC "${MAGIC_LIB}")
endif()

# ─────────────────────────────────────────────
# ibApps — exports main routines
# ─────────────────────────────────────────────
add_library(ibApps
    ${SRC_DIR}/iindex_main.cxx
    ${SRC_DIR}/isearch_main.cxx
    ${SRC_DIR}/iutil_main.cxx
    ${SRC_DIR}/ibtest_main.cxx
)
ib_common_includes(ibApps)
target_link_libraries(ibApps PUBLIC ibUtils ibSearch ibDoc ibProcess)

# -----------------------------------------------------------------------------
# Shared-library metadata
# -----------------------------------------------------------------------------
set(IB_LIBRARY_TARGETS
    ibUtils
    ibIO
    ibProcess
    ibLocal
    ibSearch
    ibDoc
    ibApps
)

foreach(_ib_library IN LISTS IB_LIBRARY_TARGETS)
    ib_configure_library(${_ib_library})

    if(BUILD_SHARED_LIBS)
        set_target_properties(${_ib_library} PROPERTIES
            VERSION "${PROJECT_VERSION}"
            SOVERSION "4"
        )
    endif()
endforeach()

# -----------------------------------------------------------------------------
# Common executable link set
# -----------------------------------------------------------------------------
set(COMMON_LINK_LIBS
    ibLocal
    ibUtils
    ibSearch
    ibDoc
    ibProcess
    ibIO
    ibApps
    Threads::Threads
)

if(CMAKE_DL_LIBS)
    list(APPEND COMMON_LINK_LIBS "${CMAKE_DL_LIBS}")
endif()

if(UNIX)
    list(APPEND COMMON_LINK_LIBS m)
endif()

if(USE_LIBMAGIC)
    list(APPEND COMMON_LINK_LIBS "${MAGIC_LIB}")
endif()

if(USE_BERKELEY_DB)
    list(APPEND COMMON_LINK_LIBS "${BDB_LIB}")
endif()

if(USE_JEMALLOC)
    list(APPEND COMMON_LINK_LIBS "${JEMALLOC_LIB}")
endif()

if(USE_OPENMP)
    list(APPEND COMMON_LINK_LIBS OpenMP::OpenMP_CXX)
endif()

add_subdirectory(doctype/plugins)

# -----------------------------------------------------------------------------
# Main executables
# -----------------------------------------------------------------------------
foreach(_ib_executable Iindex Isearch Iutil quarry)
    add_executable(
        ${_ib_executable}
        "${SRC_DIR}/${_ib_executable}.c"
    )
    target_link_libraries(
        ${_ib_executable}
        PRIVATE
        ${COMMON_LINK_LIBS}
    )
    ib_configure_executable(${_ib_executable})
endforeach()

add_executable(Idelete "${SRC_DIR}/Idelete.cxx")
ib_common_includes(Idelete)
target_link_libraries(Idelete PRIVATE ${COMMON_LINK_LIBS})
ib_configure_executable(Idelete)

# -----------------------------------------------------------------------------
# Utility executables
# -----------------------------------------------------------------------------
foreach(_ib_utility siscompress sisdump sisdump32)
    add_executable(
        ${_ib_utility}
        "${SRC_DIR}/${_ib_utility}.c"
    )
    ib_configure_executable(${_ib_utility})
endforeach()

add_executable(sischeck "${SRC_DIR}/sischeck.cxx")
target_link_libraries(sischeck PRIVATE ${COMMON_LINK_LIBS})
ib_configure_executable(sischeck)

add_executable(Iwatch "${SRC_DIR}/Iwatch.c")
ib_configure_executable(Iwatch)

add_executable(ziplist "${SRC_DIR}/ziplist.cxx")
ib_configure_executable(ziplist)

foreach(_ib_executable zpresent zsearch)
    add_executable(
        ${_ib_executable}
        "${SRC_DIR}/${_ib_executable}.cxx"
    )
    ib_common_includes(${_ib_executable})
    target_link_libraries(
        ${_ib_executable}
        PRIVATE
        ${COMMON_LINK_LIBS}
    )
    ib_configure_executable(${_ib_executable})
endforeach()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
install(
    TARGETS
        Iindex
        Isearch
        Iutil
        Idelete
        zpresent
        zsearch
        Iwatch
        ziplist
    RUNTIME DESTINATION "${IB_INSTALL_BINDIR}"
)

install(
    TARGETS
        ibUtils
        ibSearch
        ibDoc
        ibProcess
        ibLocal
        ibIO
        ibApps
    ARCHIVE DESTINATION "${IB_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${IB_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${IB_INSTALL_BINDIR}"
)

if(IB_CREATE_USER_PLUGIN_DIR)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        file(MAKE_DIRECTORY "$ENV{HOME}/.ib/lib/plugins")
    else()
        message(WARNING
            "IB_CREATE_USER_PLUGIN_DIR is ON, but HOME is not set"
        )
    endif()
endif()
