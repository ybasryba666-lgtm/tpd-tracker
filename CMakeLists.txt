cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(TPDTracker VERSION 1.0.0)

# ── Fix: CMake 3.27+ dropped support for cmake_minimum_required(VERSION < 3.5)
# One of Geode's dependencies (TulipHook) uses an older version requirement.
# This cache variable makes CMake accept it without erroring out.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE INTERNAL "")

# ── Locate Geode SDK ──────────────────────────────────────────────────────────
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR
        "[TPD Tracker] GEODE_SDK is not set. "
        "Point it to your Geode SDK directory and re-run cmake.")
else()
    message(STATUS "[TPD Tracker] Using Geode SDK at: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# ── Mod target ────────────────────────────────────────────────────────────────
add_library(${PROJECT_NAME} SHARED src/main.cpp)
target_link_libraries(${PROJECT_NAME} geode-sdk)
create_geode_file(${PROJECT_NAME})
