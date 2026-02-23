cmake_minimum_required(VERSION 3.20)

# =============================================================================
# Cross-compilation setup (MUST be before project())
# =============================================================================
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TARGET_TRIPLE "x86_64-w64-mingw32")
set(MINGW_ROOT "/usr/x86_64-w64-mingw32" CACHE PATH "MinGW sysroot")

project(gluonsock_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Paths
# =============================================================================
set(PROJECT_ROOT ${CMAKE_SOURCE_DIR}/..)
set(SOCKS_INCLUDE ${PROJECT_ROOT}/socks/include)
set(SOCKS_SOURCE ${PROJECT_ROOT}/socks/src)

# =============================================================================
# Compiler flags
# =============================================================================
add_compile_options(
    --target=${TARGET_TRIPLE}
    -I${MINGW_ROOT}/include
    -Wall
    -Wextra
)

add_compile_definitions(
    _DEBUG=1
    DEBUG_BUILD=1
)

# =============================================================================
# Include directories
# =============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${SOCKS_INCLUDE}
    ${PROJECT_ROOT}/include/gluonsock
)

# =============================================================================
# Linker flags
# =============================================================================
add_link_options(
    --target=${TARGET_TRIPLE}
    -fuse-ld=lld
    -L${MINGW_ROOT}/lib
    -static
    -static-libgcc
    -static-libstdc++
)

# =============================================================================
# GTest library setup
# =============================================================================
set(GTEST_LIBS
    -Wl,--whole-archive -lgtest_main -lgtest -Wl,--no-whole-archive
    -lpthread
)

# =============================================================================
# Helper function to create test targets
# =============================================================================
function(add_gluonsock_test NAME)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBS" ${ARGN})

    add_executable(test_${NAME} ${TEST_SOURCES})
    target_link_libraries(test_${NAME} PRIVATE ${GTEST_LIBS} ${TEST_LIBS})
    set_target_properties(test_${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_NAME "test_${NAME}"
        SUFFIX ".exe"
    )

    # Add CTest test that runs with wine
    add_test(
        NAME ${NAME}
        COMMAND wine ${CMAKE_BINARY_DIR}/test_${NAME}.exe --gtest_color=no
    )
endfunction()

# =============================================================================
# Test targets
# =============================================================================
enable_testing()

add_gluonsock_test(socks
    SOURCES src/test_socks.cpp
    LIBS -lws2_32
)

# =============================================================================
# Convenience targets
# =============================================================================
add_custom_target(run
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_socks
    COMMENT "Running all tests with wine"
)

add_custom_target(run_socks
    COMMAND wine ${CMAKE_BINARY_DIR}/test_socks.exe --gtest_color=no
    DEPENDS test_socks
)
