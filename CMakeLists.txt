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

include(cmake/EmbedWebAssets.cmake)
embed_web_assets("${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/ravengund/web"
  index.html app.css app.js)

add_library(ravengun STATIC
  src/ravengun/json.cpp
  src/ravengun/json_parse.cpp
  src/ravengun/ipc_protocol.cpp
  src/ravengun/ipc_client.cpp
  src/ravengun/web/web_core.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp"
  src/ravengun/core/aim.cpp
  src/ravengun/core/buttons.cpp
  src/ravengun/core/fusion.cpp
  src/ravengun/core/input_model.cpp
  src/ravengun/core/pipeline.cpp
  src/ravengun/output/gun_transform.cpp
  src/ravengun/output/gun_output.cpp
  src/ravengun/output/test_pattern.cpp
  src/ravengun/output/uinput_gun.cpp
  src/ravengun/vision/ball_tracker.cpp
  src/ravengun/vision/frame_timing.cpp
  src/ravengun/parse.cpp
  src/ravengun/probe.cpp
  src/ravengun/doctor.cpp
  src/ravengun/report.cpp)
target_include_directories(ravengun PUBLIC src)

add_executable(ravengunctl src/ravengunctl/main.cpp)
target_link_libraries(ravengunctl PRIVATE ravengun)

# ravengund: de daemon. PSMoveAPI wordt user-local verwacht (zie docs/build.md).
option(RAVENGUN_BUILD_DAEMON "Bouw ravengund" ON)
find_package(Threads REQUIRED)
set(PSMOVEAPI_ROOT "$ENV{HOME}/.local/share/ravengun/psmoveapi" CACHE PATH "Prefix van de PSMoveAPI-installatie")
if(RAVENGUN_BUILD_DAEMON)
  find_package(Threads REQUIRED)
  find_path(PSMOVEAPI_INCLUDE_DIR psmoveapi/psmove.h HINTS "${PSMOVEAPI_ROOT}/include" NO_DEFAULT_PATH)
  find_library(PSMOVEAPI_LIBRARY psmoveapi HINTS "${PSMOVEAPI_ROOT}/lib" NO_DEFAULT_PATH)
  add_executable(ravengund
    src/ravengund/main.cpp
    src/ravengund/hid_thread.cpp
    src/ravengund/ipc_server.cpp
    src/ravengund/commands.cpp
    src/ravengund/web_server.cpp
    src/ravengund/output_service.cpp
    src/ravengund/camera.cpp)
  target_link_libraries(ravengund PRIVATE ravengun Threads::Threads)
  if(PSMOVEAPI_INCLUDE_DIR AND PSMOVEAPI_LIBRARY)
    message(STATUS "PSMoveAPI: ${PSMOVEAPI_LIBRARY}")
    target_include_directories(ravengund SYSTEM PRIVATE "${PSMOVEAPI_INCLUDE_DIR}")
    target_link_libraries(ravengund PRIVATE "${PSMOVEAPI_LIBRARY}")
    get_filename_component(_psm_libdir "${PSMOVEAPI_LIBRARY}" DIRECTORY)
    set_target_properties(ravengund PROPERTIES BUILD_RPATH "${_psm_libdir}" INSTALL_RPATH "${_psm_libdir}")
  else()
    message(WARNING "PSMoveAPI niet gevonden onder ${PSMOVEAPI_ROOT}: ravengund wordt zonder HID gebouwd")
    target_compile_definitions(ravengund PRIVATE RAVENGUN_NO_PSMOVEAPI=1)
  endif()
endif()

if(RAVENGUN_BUILD_CAMPROBE)
  add_executable(camprobe src/tools/camprobe/camprobe.cpp)
endif()

if(RAVENGUN_BUILD_TESTS)
  enable_testing()
  add_executable(ravengun_tests tests/test_main.cpp)
  target_link_libraries(ravengun_tests PRIVATE ravengun)
  add_test(NAME ravengun_tests COMMAND ravengun_tests)
  add_executable(ravengun_core_tests tests/test_core.cpp)
  target_link_libraries(ravengun_core_tests PRIVATE ravengun)
  add_test(NAME ravengun_core_tests COMMAND ravengun_core_tests)
  add_executable(ravengun_output_tests tests/test_output.cpp)
  target_link_libraries(ravengun_output_tests PRIVATE ravengun)
  add_test(NAME ravengun_output_tests COMMAND ravengun_output_tests)
  add_executable(ravengun_vision_tests tests/test_vision.cpp)
  target_link_libraries(ravengun_vision_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_vision_tests COMMAND ravengun_vision_tests)
  add_executable(ravengun_web_tests tests/test_web.cpp)
  target_link_libraries(ravengun_web_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_web_tests COMMAND ravengun_web_tests)
  if(RAVENGUN_BUILD_DAEMON)
    add_test(NAME ravengund_web
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_smoke.sh" "$<TARGET_FILE:ravengund>")
    # Integratietest: daemon zonder hardware (--no-hid), socket, lockfile en CLI.
    add_test(NAME ravengund_ipc
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc_smoke.sh" "$<TARGET_FILE:ravengund>" "$<TARGET_FILE:ravengunctl>")
  endif()
endif()
