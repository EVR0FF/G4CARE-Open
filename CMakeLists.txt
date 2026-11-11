cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(G4CARE VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Geant4 REQUIRED ui_all vis_all)
find_package(ROOT REQUIRED)
find_package(TBB REQUIRED)
find_package(ZLIB REQUIRED)

# ── Optional DICOM import (GDCM) ────────────────────────────────────────────
option(G4CARE_WITH_DICOM "Enable GDCM-based DICOM import (CT phantom, RTSTRUCT, RTPLAN, RTDOSE)" OFF)
if(G4CARE_WITH_DICOM)
    find_package(GDCM REQUIRED)
endif()

# yaml-cpp is vendored with only include/ and src/ shipped; the tests and
# tools subdirectories are not part of the repository, so disable them.
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/yaml-cpp)
include_directories(${PROJECT_SOURCE_DIR}/third_party/yaml-cpp/include)

# toml++ (header-only, MIT) — cloud metadata files (*.meta.toml)
include_directories(${PROJECT_SOURCE_DIR}/third_party/toml++/include)

include(${Geant4_USE_FILE})

file(GLOB_RECURSE INCLUDE_DIRS LIST_DIRECTORIES true ${PROJECT_SOURCE_DIR}/include/*)
foreach(dir ${INCLUDE_DIRS})
    if(IS_DIRECTORY ${dir})
        include_directories(${dir})
    endif()
endforeach()

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/**/*.cc)

file(GLOB_RECURSE HEADERS ${PROJECT_SOURCE_DIR}/include/*.hh)

# Entry points (thin wrappers) are not part of the engine library.
list(REMOVE_ITEM SOURCES
    ${PROJECT_SOURCE_DIR}/src/main.cc
    ${PROJECT_SOURCE_DIR}/src/Core/engine_main.cc)

# ── Engine static library ────────────────────────────────────────────────────
add_library(g4care_engine STATIC ${SOURCES} ${HEADERS})

target_include_directories(g4care_engine PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party)

target_link_libraries(g4care_engine PUBLIC
    ${Geant4_LIBRARIES}
    ${ROOT_LIBRARIES}
    ROOT::ROOTDataFrame
    ROOT::ROOTNTuple
    yaml-cpp
    ZLIB::ZLIB
)

if(G4CARE_WITH_DICOM)
    target_include_directories(g4care_engine PRIVATE ${GDCM_INCLUDE_DIRS})
    # GDCM's install-tree config leaves GDCM_LIBRARIES empty; link the imported
    # targets directly. gdcmMSFF transitively pulls gdcmIOD/DSED/DICT/Common.
    target_link_libraries(g4care_engine PUBLIC gdcmMSFF gdcmMEXD)
    target_compile_definitions(g4care_engine PUBLIC G4CARE_HAS_DICOM)
endif()

target_compile_options(g4care_engine PRIVATE
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
    $<$<CONFIG:Debug>:-g -O0>
)

target_compile_features(g4care_engine PUBLIC cxx_std_20)

# ── CLI executable (thin wrapper) ────────────────────────────────────────────
add_executable(G4CARE ${PROJECT_SOURCE_DIR}/src/main.cc)
target_link_libraries(G4CARE g4care_engine)

# ── RPC sidecar executable ───────────────────────────────────────────────────
add_executable(g4care-engine ${PROJECT_SOURCE_DIR}/src/Core/engine_main.cc)
target_link_libraries(g4care-engine g4care_engine)

foreach(_exe G4CARE g4care-engine)
    set_target_properties(${_exe} PROPERTIES
        INSTALL_RPATH "/usr/lib/x86_64-linux-gnu"
        BUILD_WITH_INSTALL_RPATH TRUE)
endforeach()
