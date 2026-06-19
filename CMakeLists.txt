cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(TPDTracker VERSION 1.0.0)

# ── Locate Geode SDK ──────────────────────────────────────────────────────────
# Set the GEODE_SDK environment variable to your Geode installation path,
# or pass -DGEODE_SDK=/path/to/geode on the cmake command line.
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "[TPD Tracker] GEODE_SDK environment variable is not set.\n"
        "  Point it to your Geode SDK directory, e.g.:\n"
        "    export GEODE_SDK=/path/to/geode-sdk    (Linux / macOS)\n"
        "    set GEODE_SDK=C:\\path\\to\\geode-sdk  (Windows)\n"
        "  Then re-run cmake.")
else()
    message(STATUS "[TPD Tracker] Using Geode SDK at: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ── Mod target ────────────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} SHARED src/main.cpp)

target_link_libraries(${PROJECT_NAME} geode-sdk)

# create_geode_file reads mod.json and packages the .geode bundle.
create_geode_file(${PROJECT_NAME})
