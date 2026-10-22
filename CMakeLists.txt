cmake_minimum_required(VERSION 3.20)
project(f1_pitwall_backend VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(ENABLE_TESTS   "Build unit tests"    ON)

# --------------------------------------------------------------------------
# Compiler flags
# --------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 $<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -Wextra -pedantic)
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

set(DSA_SOURCES
    src/dsa/dp_solver.cpp
    src/dsa/greedy_solver.cpp
    src/dsa/branch_bound_solver.cpp
    src/dsa/graph_solver.cpp
    src/dsa/genetic_solver.cpp
    src/dsa/comparison_harness.cpp
)

set(API_SOURCES
    src/api/server.cpp
    src/api/routes_dsa.cpp
)

# --------------------------------------------------------------------------
# Main executable
# --------------------------------------------------------------------------
add_executable(f1_pitwall
    src/main.cpp
    ${MODEL_SOURCES}
    ${DB_SOURCES}
    ${DSA_SOURCES}
    ${API_SOURCES}
)

if(SQLITE_AVAILABLE)
    target_link_libraries(f1_pitwall sqlite3_lib)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(ENABLE_TESTS)
    enable_testing()
    add_executable(pitwall_tests
        tests/test_main.cpp
        tests/test_dsa_solvers.cpp
        tests/test_degradation.cpp
        tests/test_db_reader.cpp
        ${MODEL_SOURCES}
        ${DB_SOURCES}
        ${DSA_SOURCES}
    )
    if(SQLITE_AVAILABLE)
        target_link_libraries(pitwall_tests sqlite3_lib)
    endif()
    add_test(NAME all_tests COMMAND pitwall_tests)
endif()
