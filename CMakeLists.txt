cmake_minimum_required(VERSION 3.20)
project(f1_pitwall_backend VERSION 1.0.0 LANGUAGES CXX C)

# CUDA language is enabled later only if ENABLE_CUDA=ON
if(ENABLE_CUDA)
    enable_language(CUDA)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(ENABLE_OPENMP  "Enable OpenMP multi-threading"       ON)
option(ENABLE_CUDA    "Enable CUDA GPU implementation"      OFF)
option(ENABLE_TESTS   "Build unit tests"                    ON)

# --------------------------------------------------------------------------
# Compiler flags
# --------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 $<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    # Optimization level controlled per-target (see COA targets below)
endif()

# --------------------------------------------------------------------------
# OpenMP
# --------------------------------------------------------------------------
if(ENABLE_OPENMP)
    find_package(OpenMP REQUIRED)
endif()

# --------------------------------------------------------------------------
# Third-party: cpp-httplib + nlohmann/json (header-only, vendored)
# --------------------------------------------------------------------------
include_directories(third_party/cpp-httplib)
include_directories(third_party)

# --------------------------------------------------------------------------
# SQLite3 amalgamation
# --------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
    add_library(sqlite3_lib STATIC third_party/sqlite/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC third_party/sqlite)
    set(SQLITE_AVAILABLE TRUE)
else()
    message(WARNING "SQLite3 amalgamation not found in third_party/sqlite/.")
    set(SQLITE_AVAILABLE FALSE)
endif()
include_directories(third_party/sqlite)

# --------------------------------------------------------------------------
# Project include dirs
# --------------------------------------------------------------------------
include_directories(include)

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
set(MODEL_SOURCES
    src/models/tyre.cpp
    src/models/race_state.cpp
    src/models/degradation.cpp
    src/models/driver.cpp
    src/models/team.cpp
)

set(DB_SOURCES
    src/db/db_reader.cpp
)

set(COA_SOURCES
    src/coa/monte_carlo_single.cpp
    src/coa/monte_carlo_omp.cpp
    src/coa/profiler.cpp
)

set(API_SOURCES
    src/api/server.cpp
    src/api/routes_coa.cpp
)

# --------------------------------------------------------------------------
# Main executable
# --------------------------------------------------------------------------
add_executable(f1_pitwall
    src/main.cpp
    ${MODEL_SOURCES}
    ${DB_SOURCES}
    ${COA_SOURCES}
    ${API_SOURCES}
)

if(ENABLE_OPENMP)
    target_link_libraries(f1_pitwall OpenMP::OpenMP_CXX)
endif()
if(SQLITE_AVAILABLE)
    target_link_libraries(f1_pitwall sqlite3_lib)
endif()

# Wire in the CUDA translation unit when the toolkit is available
if(ENABLE_CUDA)
    target_sources(f1_pitwall PRIVATE src/coa/monte_carlo_cuda.cu)
    target_compile_definitions(f1_pitwall PRIVATE ENABLE_CUDA)
    set_target_properties(f1_pitwall PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()

# --------------------------------------------------------------------------
# COA benchmark targets (separate binaries per optimization level)
# --------------------------------------------------------------------------
foreach(OPT_LEVEL O0 O1 O2 O3)
    add_executable(bench_${OPT_LEVEL}
        src/coa/bench_main.cpp
        ${MODEL_SOURCES}
        src/coa/monte_carlo_single.cpp
        src/coa/monte_carlo_omp.cpp
        src/coa/profiler.cpp
    )
    if(NOT MSVC)
        target_compile_options(bench_${OPT_LEVEL} PRIVATE -${OPT_LEVEL})
    endif()
    if(ENABLE_OPENMP)
        target_link_libraries(bench_${OPT_LEVEL} OpenMP::OpenMP_CXX)
    endif()
endforeach()

# --------------------------------------------------------------------------
# CUDA target (optional)
# --------------------------------------------------------------------------
if(ENABLE_CUDA)
    add_executable(bench_cuda
        src/coa/bench_main.cpp
        src/coa/monte_carlo_cuda.cu
        src/coa/monte_carlo_single.cpp
        src/coa/monte_carlo_omp.cpp
        src/coa/profiler.cpp
        ${MODEL_SOURCES}
    )
    target_compile_definitions(bench_cuda PRIVATE ENABLE_CUDA)
    set_target_properties(bench_cuda PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    if(ENABLE_OPENMP)
        target_link_libraries(bench_cuda OpenMP::OpenMP_CXX)
    endif()
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(ENABLE_TESTS)
    enable_testing()
    add_executable(pitwall_tests
        tests/test_main.cpp
        tests/test_degradation.cpp
        tests/test_monte_carlo.cpp
        tests/test_db_reader.cpp
        ${MODEL_SOURCES}
        ${DB_SOURCES}
        src/coa/monte_carlo_single.cpp
        src/coa/monte_carlo_omp.cpp
        src/coa/profiler.cpp
    )
    if(ENABLE_OPENMP)
        target_link_libraries(pitwall_tests OpenMP::OpenMP_CXX)
    endif()
    if(SQLITE_AVAILABLE)
        target_link_libraries(pitwall_tests sqlite3_lib)
    endif()
    add_test(NAME all_tests COMMAND pitwall_tests)
endif()
