cmake_minimum_required(VERSION 3.16)
project(RavenGun VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

option(RAVENGUN_BUILD_TESTS "Bouw de unittests" ON)
option(RAVENGUN_BUILD_CAMPROBE "Bouw het camera-meetprogramma" ON)

add_library(ravengun STATIC
  src/ravengun/json.cpp
  src/ravengun/parse.cpp
  src/ravengun/probe.cpp
  src/ravengun/doctor.cpp
  src/ravengun/report.cpp)
target_include_directories(ravengun PUBLIC src)

add_executable(ravengunctl src/ravengunctl/main.cpp)
target_link_libraries(ravengunctl PRIVATE ravengun)

if(RAVENGUN_BUILD_CAMPROBE)
  add_executable(camprobe src/tools/camprobe/camprobe.cpp)
endif()

if(RAVENGUN_BUILD_TESTS)
  enable_testing()
  add_executable(ravengun_tests tests/test_main.cpp)
  target_link_libraries(ravengun_tests PRIVATE ravengun)
  add_test(NAME ravengun_tests COMMAND ravengun_tests)
endif()
