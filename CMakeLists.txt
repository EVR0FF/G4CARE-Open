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

# yaml-cpp is vendored with only include/ and src/ shipped; the tests and
# tools subdirectories are not part of the repository, so disable them.
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/yaml-cpp)
include_directories(${PROJECT_SOURCE_DIR}/third_party/yaml-cpp/include)

include(${Geant4_USE_FILE})

file(GLOB_RECURSE INCLUDE_DIRS LIST_DIRECTORIES true ${PROJECT_SOURCE_DIR}/include/*)
foreach(dir ${INCLUDE_DIRS})
    if(IS_DIRECTORY ${dir})
        include_directories(${dir})
    endif()
endforeach()

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/**/*.cc)

file(GLOB_RECURSE HEADERS ${PROJECT_SOURCE_DIR}/include/*.hh)

add_executable(G4CARE ${SOURCES} ${HEADERS})

target_include_directories(G4CARE PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(G4CARE 
    ${Geant4_LIBRARIES}
    ${Geant4_LIBRARIES}
    ${ROOT_LIBRARIES}
    ROOT::ROOTDataFrame
    ROOT::ROOTNTuple
    yaml-cpp
)

target_compile_options(G4CARE PRIVATE 
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
    $<$<CONFIG:Debug>:-g -O0>
)

target_compile_features(G4CARE PRIVATE cxx_std_20)

set_target_properties(G4CARE PROPERTIES
    INSTALL_RPATH "/usr/lib/x86_64-linux-gnu"
    BUILD_WITH_INSTALL_RPATH TRUE
)
