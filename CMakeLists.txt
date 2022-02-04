cmake_minimum_required(VERSION 3.11...3.16)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

project(Sekkeizu VERSION 0.1 LANGUAGES CXX)

option(BUILD_TESTS                  "Build tests"               ON)
option(Boost_USE_STATIC_LIBS        "Use Boost static libs"     ON)
option(BUILD_PALANTEER_VIEWER       "Build viewer"              ON)

set(BOOST_VERSION                   "1.73"                      CACHE STRING    "Boost version")
set(CMAKE_CXX_STANDARD              20                          CACHE STRING    "Default C++ standard")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(target_link_libraries_system)

add_subdirectory(src)
add_subdirectory(dep)

# if (BUILD_TESTS)
#     add_subdirectory(test)
# endif()
