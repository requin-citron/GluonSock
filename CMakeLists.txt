cmake_minimum_required(VERSION 3.20)

# =============================================================================
# Cross-compilation setup (MUST be before project())
# =============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER /opt/Polaris-Obfuscator/src/build/bin/clang -mllvm -passes=fla,mba,indcall,bcf,sub)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TARGET_TRIPLE "x86_64-w64-mingw32")
set(MINGW_ROOT "/usr/x86_64-w64-mingw32" CACHE PATH "MinGW sysroot")

project(GluonSock LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =============================================================================
# Build type option: cmake -DDEBUG_BUILD=ON ..
# =============================================================================
option(DEBUG_BUILD "Build with debug symbols and stdlib support" OFF)

# =============================================================================
# Paths
# =============================================================================
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

# =============================================================================
# Compiler flags
# =============================================================================
add_compile_options(
    --target=${TARGET_TRIPLE}
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
    -mno-stack-arg-probe
    -fno-asynchronous-unwind-tables
    -fPIC
)

if(DEBUG_BUILD)
    add_compile_options(-g -O1)
    add_compile_definitions(_DEBUG=1)
else()
    add_compile_options(-O1 -fno-builtin -ffreestanding -fno-ident)
    add_compile_definitions(_DEBUG=0)
endif()

# =============================================================================
# Include directories
# =============================================================================
include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/socks/include
)

# =============================================================================
# Linker flags
# =============================================================================
add_link_options(
    -Wl,-e,main
    --target=${TARGET_TRIPLE}
    -fuse-ld=lld
    -L${MINGW_ROOT}/lib
    -Wl,--gc-sections
    -Wl,--no-seh
    -Wl,--enable-stdcall-fixup
)

if(NOT DEBUG_BUILD)
    add_link_options(-nostdlib -nostartfiles -s -Wl,--build-id=none)
endif()

# =============================================================================
# Library setup
# =============================================================================
if(DEBUG_BUILD)
    set(LIBS -lmsvcrt -lkernel32 -lws2_32)
else()
    set(LIBS -lkernel32 -lws2_32)
endif()

# =============================================================================
# socks — bibliothèque statique
# =============================================================================
file(GLOB SOCKS_SOURCES ${PROJECT_ROOT}/socks/src/*.c)

add_library(socks STATIC ${SOCKS_SOURCES})

target_include_directories(socks PUBLIC
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/socks/include
    ${PROJECT_ROOT}/include/gluonsock
)

# =============================================================================
# gluonsock — exécutable CLI
# =============================================================================
file(GLOB CLI_SOURCES ${PROJECT_ROOT}/cli/*.c)

add_executable(gluonsock ${CLI_SOURCES})

target_link_libraries(gluonsock PRIVATE socks ${LIBS})

if(DEBUG_BUILD)
    set(OUTPUT_SUFFIX "_debug")
else()
    set(OUTPUT_SUFFIX "_release")
endif()

set_target_properties(gluonsock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    OUTPUT_NAME "gluonsock${OUTPUT_SUFFIX}"
    SUFFIX ".exe"
)

# =============================================================================
# Test suite
# =============================================================================
option(ENABLE_TESTS "Build test suite" OFF)
if(ENABLE_TESTS)
    add_subdirectory(test)
endif()
