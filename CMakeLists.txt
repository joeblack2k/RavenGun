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
  index.html app.css app-core.js app.js calibrate.html calibrate.css calibrate-core.js calibrate.js
  "hid-boards.json=${CMAKE_CURRENT_SOURCE_DIR}/packaging/hid-boards.json"
  "jvs-boards.json=${CMAKE_CURRENT_SOURCE_DIR}/packaging/jvs-boards.json"
  "control-pipe-profiles.json=${CMAKE_CURRENT_SOURCE_DIR}/packaging/control-pipe-profiles.json"
  "tp-shm-profiles.json=${CMAKE_CURRENT_SOURCE_DIR}/packaging/tp-shm-profiles.json")

# Bouwinformatie voor de infopagina (git-commit bij het uitvoeren van cmake; PSMoveAPI-pin zoals in docs/build.md).
set(RAVENGUN_PSMOVEAPI_PIN "256b6299e6076fe7b113e7277ed67016903f067a" CACHE STRING "Gepinde PSMoveAPI-commit (docs/build.md)")
find_package(Git QUIET)
set(RAVENGUN_GIT_COMMIT "onbekend")
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --always --dirty --abbrev=12
    OUTPUT_VARIABLE _rg_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _git_rc)
  if(_git_rc EQUAL 0 AND NOT _rg_commit STREQUAL "")
    set(RAVENGUN_GIT_COMMIT "${_rg_commit}")
  endif()
endif()
if(RAVENGUN_GIT_COMMIT_OVERRIDE)  # releasebouw uit een archief zonder .git (tools/dev/make-release.sh)
  set(RAVENGUN_GIT_COMMIT "${RAVENGUN_GIT_COMMIT_OVERRIDE}")
endif()
set(RAVENGUN_BUILD_DEFS
  "RAVENGUN_VERSION=\"${PROJECT_VERSION}\""
  "RAVENGUN_GIT_COMMIT=\"${RAVENGUN_GIT_COMMIT}\""
  "RAVENGUN_PSMOVEAPI_PIN=\"${RAVENGUN_PSMOVEAPI_PIN}\"")

# rg-exec (tools/rg-exec): het hulpprogramma tussen gamescope en bwrap van de native route; laat alleen DISPLAY van gamescope door.
add_executable(rg-exec tools/rg-exec/rg_exec.cpp)
install(TARGETS rg-exec RUNTIME DESTINATION bin)

add_library(ravengun STATIC
  src/ravengun/config/settings.cpp
  src/ravengun/config/led_color.cpp
  src/ravengun/kiosk/kiosk.cpp
  src/ravengun/games/games.cpp
  src/ravengun/games/readiness.cpp
  src/ravengun/games/seed.cpp
  src/ravengun/launch/native.cpp
  src/ravengun/launch/gamefolder.cpp
  src/ravengun/launch/unity.cpp
  src/ravengun/launch/direct.cpp
  src/ravengun/launch/direct_util.cpp
  src/ravengun/launch/direct_profile.cpp
  src/ravengun/launch/direct_files.cpp
  src/ravengun/launch/direct_apply.cpp
  src/ravengun/launch/ttc.cpp
  src/ravengun/launch/pe_stack.cpp
  src/ravengun/launch/gameview.cpp
  src/ravengun/launch/gamefind.cpp
  src/ravengun/launch/jvs_pipe.cpp
  src/ravengun/launch/amdaemon.cpp
  src/ravengun/games/aagames.cpp
  src/ravengun/gamesettings/gamesettings.cpp
  src/ravengun/gamesettings/gamepad_settings.cpp
  src/ravengun/gamesettings/controls_settings.cpp
  src/ravengun/iobridge/io_state.cpp
  src/ravengun/iobridge/tp_shm.cpp
  src/ravengun/iobridge/control_pipe.cpp
  src/ravengun/iobridge/hid_board.cpp
  src/ravengun/iobridge/jvs.cpp
  src/ravengun/iobridge/jvs_dialect.cpp
  src/ravengun/iobridge/routes.cpp
  src/ravengun/iobridge/unity_tcp.cpp
  src/ravengun/sysinfo/navcon.cpp
  src/ravengun/sysinfo/mice.cpp
  src/ravengun/sysinfo/gamepads.cpp
  src/ravengun/sysinfo/hidraw_scan.cpp
  src/ravengun/pairing/pairing.cpp
  src/ravengun/json.cpp
  src/ravengun/json_parse.cpp
  src/ravengun/ipc_protocol.cpp
  src/ravengun/ipc_client.cpp
  src/ravengun/web/web_core.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/web_assets.cpp"
  src/ravengun/core/aim.cpp
  src/ravengun/core/buttons.cpp
  src/ravengun/core/fusion.cpp
  src/ravengun/core/bias_store.cpp
  src/ravengun/core/input_model.cpp
  src/ravengun/core/pipeline.cpp
  src/ravengun/core/predict.cpp
  src/ravengun/output/gun_transform.cpp
  src/ravengun/output/gun_output.cpp
  src/ravengun/output/test_pattern.cpp
  src/ravengun/output/uinput_gun.cpp
  src/ravengun/output/uinput_kbd.cpp
  src/ravengun/output/key_mux.cpp
  src/ravengun/core/navcon.cpp
  src/ravengun/output/live_feed.cpp
  src/ravengun/output/mouse_gun.cpp
  src/ravengun/output/gamepad.cpp
  src/ravengun/output/aim_clock.cpp
  src/ravengun/output/rumble.cpp
  src/ravengun/core/move_report.cpp
  src/ravengun/core/move_clock.cpp
  src/ravengun/core/magnetometer.cpp
  src/ravengun/core/mag_yaw.cpp
  src/ravengun/core/vqf_estimator.cpp
  third_party/vqf/vqf.cpp
  src/ravengun/rec/recording.cpp
  src/ravengun/rec/replay.cpp
  src/ravengun/rec/drift.cpp
  src/ravengun/rec/latency.cpp
  src/ravengun/vision/ball_tracker.cpp
  src/ravengun/vision/frame_timing.cpp
  src/ravengun/vision/camera_feed.cpp
  src/ravengun/parse.cpp
  src/ravengun/paths.cpp
  src/ravengun/probe.cpp
  src/ravengun/doctor.cpp
  src/ravengun/report.cpp)
target_include_directories(ravengun PUBLIC src)
# VQF (gevendord, MIT, zie third_party/vqf/README.md): alleen intern; eigen waarschuwingsniveau uit voor de externe bron.
target_include_directories(ravengun PRIVATE third_party/vqf)
set_source_files_properties(third_party/vqf/vqf.cpp PROPERTIES COMPILE_OPTIONS "-w")

add_executable(ravengunctl src/ravengunctl/main.cpp src/ravengunctl/ctl_common.cpp src/ravengunctl/ctl_status.cpp src/ravengunctl/ctl_record.cpp
  src/ravengunctl/ctl_controllers.cpp src/ravengunctl/ctl_calibrate.cpp)
target_link_libraries(ravengunctl PRIVATE ravengun)

# De twee programma's met rootrechten (src/syshelper, docs/build.md "Rechten"): ravengun-bt-helper (sudo, door de daemon bij
# het koppelen van een Move) en ravengun-setup-helper (pkexec, door de app bij het instellen: udev-regels, de Bluetooth-hulp en
# de sudoers-regel). Klein, zonder Qt en zonder shell. De setup-helper bakt zijn inhoud (de udev-regels en ravengun-bt-helper)
# in zichzelf, zodat hij als root niets uit een door de gebruiker beschrijfbare map leest.
add_library(ravengun_syshelper STATIC src/syshelper/common.cpp src/syshelper/bt_core.cpp)
target_include_directories(ravengun_syshelper PUBLIC src)
set_target_properties(ravengun_syshelper PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(ravengun_syshelper PUBLIC -fstack-protector-strong)
add_executable(ravengun-bt-helper src/syshelper/bt_main.cpp)
target_link_libraries(ravengun-bt-helper PRIVATE ravengun_syshelper)
set(_rg_embedded_cpp "${CMAKE_CURRENT_BINARY_DIR}/syshelper/embedded.cpp")
add_custom_command(OUTPUT "${_rg_embedded_cpp}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/syshelper"
  COMMAND ${CMAKE_COMMAND} "-DOUT=${_rg_embedded_cpp}"
    "-DITEMS=70-ravengun.rules=${CMAKE_CURRENT_SOURCE_DIR}/packaging/udev/70-ravengun.rules;71-ravengun-uhid.rules=${CMAKE_CURRENT_SOURCE_DIR}/packaging/udev/71-ravengun-uhid.rules;72-ravengun-dongle.rules=${CMAKE_CURRENT_SOURCE_DIR}/packaging/udev/72-ravengun-dongle.rules;ravengun-bt-helper=$<TARGET_FILE:ravengun-bt-helper>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBlobs.cmake"
  DEPENDS ravengun-bt-helper packaging/udev/70-ravengun.rules packaging/udev/71-ravengun-uhid.rules packaging/udev/72-ravengun-dongle.rules
          cmake/EmbedBlobs.cmake
  VERBATIM)
add_library(ravengun_syshelper_setup STATIC src/syshelper/setup_core.cpp src/syshelper/setup_content.cpp "${_rg_embedded_cpp}")
target_link_libraries(ravengun_syshelper_setup PUBLIC ravengun_syshelper)
add_executable(ravengun-setup-helper src/syshelper/setup_main.cpp)
target_link_libraries(ravengun-setup-helper PRIVATE ravengun_syshelper_setup)
foreach(_rg_h ravengun-bt-helper ravengun-setup-helper)
  target_link_options(${_rg_h} PRIVATE -pie -static-libstdc++ -static-libgcc -Wl,-z,relro,-z,now -s)
  set_target_properties(${_rg_h} PROPERTIES POSITION_INDEPENDENT_CODE ON INSTALL_RPATH "")
endforeach()
install(TARGETS ravengun-setup-helper RUNTIME DESTINATION bin)

# ravengund: de daemon. PSMoveAPI wordt user-local verwacht (zie docs/build.md).
option(RAVENGUN_BUILD_DAEMON "Bouw ravengund" ON)
find_package(Threads REQUIRED)
# rg-direct (tools/rg-direct): het hoofdproces van de unit bij een directe start (A10), op de plaats van launch-direct.py --run.
add_executable(rg-direct tools/rg-direct/rg_direct.cpp)
target_link_libraries(rg-direct PRIVATE ravengun Threads::Threads)
install(TARGETS rg-direct RUNTIME DESTINATION bin)
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
    src/ravengund/aagames_service.cpp
    src/ravengund/iobridge_service.cpp
    src/ravengund/mouse_service.cpp
    src/ravengund/gamepad_service.cpp
    src/ravengund/camera.cpp
    src/ravengund/hidraw_tap.cpp
    src/ravengund/hid_aux.cpp
    src/ravengund/move_slot.cpp
    src/ravengund/controllers.cpp
    src/ravengund/recorder.cpp)
  target_link_libraries(ravengund PRIVATE ravengun Threads::Threads)
  target_compile_definitions(ravengund PRIVATE ${RAVENGUN_BUILD_DEFS})
  if(PSMOVEAPI_INCLUDE_DIR AND PSMOVEAPI_LIBRARY)
    message(STATUS "PSMoveAPI: ${PSMOVEAPI_LIBRARY}")
    target_include_directories(ravengund SYSTEM PRIVATE "${PSMOVEAPI_INCLUDE_DIR}")
    target_link_libraries(ravengund PRIVATE "${PSMOVEAPI_LIBRARY}")
    get_filename_component(_psm_libdir "${PSMOVEAPI_LIBRARY}" DIRECTORY)
    # Gebouwd: rpath naar de PSMoveAPI-map. Geïnstalleerd (docs/build.md, "Installatie-indeling"): libpsmoveapi.so staat in
    # <prefix>/lib naast <prefix>/bin, dus $ORIGIN/../lib.
    set_target_properties(ravengund PROPERTIES BUILD_RPATH "${_psm_libdir}" INSTALL_RPATH "$ORIGIN/../lib")
    set(RAVENGUN_PSMOVEAPI_LIB_FILE "${PSMOVEAPI_LIBRARY}")
  else()
    message(WARNING "PSMoveAPI niet gevonden onder ${PSMOVEAPI_ROOT}: ravengund wordt zonder HID gebouwd")
    target_compile_definitions(ravengund PRIVATE RAVENGUN_NO_PSMOVEAPI=1)
  endif()
endif()

# LD_PRELOAD-hulp voor de jvs-route: laat GetCommModemStatus in Wine op een pty slagen (tools/wine-comm-shim/).
option(RAVENGUN_BUILD_WINE_SHIM "Bouw de LD_PRELOAD-hulp voor de jvs-route (C-compiler nodig)" ON)
if(RAVENGUN_BUILD_WINE_SHIM)
  include(CheckLanguage)
  check_language(C)
  if(CMAKE_C_COMPILER)
    enable_language(C)
    add_library(rg_comm_shim SHARED tools/wine-comm-shim/rg_comm_shim.c)
    target_compile_options(rg_comm_shim PRIVATE -fno-stack-protector -w)
    set_target_properties(rg_comm_shim PROPERTIES PREFIX "lib" OUTPUT_NAME "rg_comm_shim" C_VISIBILITY_PRESET default)
    # 32-bit variant (de game is een 32-bit Wine-proces): zonder systeemheaders te bouwen; lukt dat niet, dan alleen een waarschuwing.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_comm_shim.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32"
      COMMAND sh -c "\"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -w -fno-stack-protector -nostdlib -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_comm_shim.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/wine-comm-shim/rg_comm_shim.c\" || echo 'waarschuwing: 32-bit LD_PRELOAD-hulp niet gebouwd (gcc -m32)'"
      DEPENDS tools/wine-comm-shim/rg_comm_shim.c VERBATIM)
    add_custom_target(rg_comm_shim32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_comm_shim.so")
    # Invoerhulp voor de native Raw Thrills-route (Halo): kastschakelaars uit het gedeelde geheugen van de IO-brug (tools/rt-input-shim/).
    add_library(rg_rt_input SHARED tools/rt-input-shim/rg_rt_input.c)
    target_compile_options(rg_rt_input PRIVATE -fno-stack-protector -w)
    target_link_libraries(rg_rt_input PRIVATE pthread m ${CMAKE_DL_LIBS})
    set_target_properties(rg_rt_input PROPERTIES PREFIX "lib" OUTPUT_NAME "rg_rt_input" C_VISIBILITY_PRESET default)
    # Virtuele webcams voor games die met camera's in de geweren richten (Halo); tools/v4l2-fake/rg_v4l2_fake.c.
    add_library(rg_v4l2_fake SHARED tools/v4l2-fake/rg_v4l2_fake.c)
    target_compile_options(rg_v4l2_fake PRIVATE -Wall -Wextra)
    target_link_libraries(rg_v4l2_fake PRIVATE pthread ${CMAKE_DL_LIBS})
    set_target_properties(rg_v4l2_fake PROPERTIES PREFIX "lib" OUTPUT_NAME "rg_v4l2_fake" C_VISIBILITY_PRESET default)
    # IRTrack-geweren (FTDI-camera's) voor Halo op de native route; tools/rt-irtrack/rg_irtrack.c, tabel patches/<Game>/irtrack.ini.
    add_library(rg_irtrack SHARED tools/rt-irtrack/rg_irtrack.c)
    target_compile_options(rg_irtrack PRIVATE -Wall -Wextra -Wno-pedantic -fno-stack-protector)
    target_link_libraries(rg_irtrack PRIVATE pthread ${CMAKE_DL_LIBS})
    set_target_properties(rg_irtrack PROPERTIES PREFIX "lib" OUTPUT_NAME "rg_irtrack" C_VISIBILITY_PRESET default)
    # FMOD-wrapper voor Halo (tools/fmod-bridge): libfmod.so.10 en libfmodstudio.so.10 uit een bron, in build/fmod (de libdir van het
    # profiel). Echte FMOD: een native drop-in van de gebruiker, anders FMOD 1.10.2 onder Wine in rg-fmod-host.exe
    # (tools/dev/build-fmod-bridge.sh, patches/Halo/data/fmod), anders stil.
    foreach(_fm fmod fmodstudio)
      add_library(rg_${_fm}10 SHARED tools/fmod-bridge/fmod_bridge.c)
      target_compile_options(rg_${_fm}10 PRIVATE -Wall -Wextra -Wno-pedantic -fvisibility=hidden)
      target_link_libraries(rg_${_fm}10 PRIVATE pthread ${CMAKE_DL_LIBS} m)
      set_target_properties(rg_${_fm}10 PROPERTIES PREFIX "lib" OUTPUT_NAME "${_fm}" SUFFIX ".so.10" C_VISIBILITY_PRESET hidden
                            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fmod")
    endforeach()
    # Invoerbrug voor het RIO-lightgun-bord van de native Raw Thrills-Unity-route (NERF): librio.so van de game leest het bordbeeld
    # van de IO-brug (tools/rio-bridge/, tabel patches/<Game>/rio-invoer.ini).
    add_library(rg_rio_bridge SHARED tools/rio-bridge/rg_rio_bridge.c)
    target_compile_options(rg_rio_bridge PRIVATE -fno-stack-protector)
    target_link_libraries(rg_rio_bridge PRIVATE pthread m ${CMAKE_DL_LIBS})
    set_target_properties(rg_rio_bridge PROPERTIES PREFIX "lib" OUTPUT_NAME "rg_rio_bridge" C_VISIBILITY_PRESET default)
    # Draadhulp voor de native i386-route (Jurassic Park): een draad die SCHED_FIFO vraagt en dat als gewone gebruiker niet mag,
    # start toch met de planning van de aanroeper (tools/rg-threadprio/). 32-bits, zoals het spel. SteamOS heeft geen
    # gnu/stubs-32.h; dan een lege stub in de build, zoals scripts/build-elfloader.sh. Lukt het niet, dan alleen een waarschuwing.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-threadprio.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32" "${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu"
      COMMAND sh -c "{ [ -f /usr/include/gnu/stubs-32.h ] || : > \"${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu/stubs-32.h\"; } && \"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -I\"${CMAKE_CURRENT_BINARY_DIR}/inc32\" -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-threadprio.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rg-threadprio/rg_threadprio.c\" -ldl || echo 'waarschuwing: 32-bit draadhulp (librg-threadprio.so) niet gebouwd (gcc -m32)'"
      DEPENDS tools/rg-threadprio/rg_threadprio.c VERBATIM)
    add_custom_target(rg_threadprio32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-threadprio.so")
    # GLSL-hulp voor de native i386-route (Aliens Armageddon): herschrijft fragment-shaders die gl_FragData mengen met eigen
    # uitvoeren (out_Color/out_Bloom), wat Mesa weigert en NVIDIA toeliet (tools/rg-glslfix/). 32-bits, zoals het spel.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-glslfix.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32" "${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu"
      COMMAND sh -c "{ [ -f /usr/include/gnu/stubs-32.h ] || : > \"${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu/stubs-32.h\"; } && \"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -I\"${CMAKE_CURRENT_BINARY_DIR}/inc32\" -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-glslfix.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rg-glslfix/rg_glslfix.c\" -ldl || echo 'waarschuwing: 32-bit GLSL-hulp (librg-glslfix.so) niet gebouwd (gcc -m32)'"
      DEPENDS tools/rg-glslfix/rg_glslfix.c VERBATIM)
    add_custom_target(rg_glslfix32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-glslfix.so")
    # Eigen libcaca.so.0 (32-bits stub) voor The Walking Dead: de game linkt libcaca, SteamOS heeft geen i386-versie
    # (tools/rg-caca-stub/).
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/libcaca.so.0"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32" "${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu"
      COMMAND sh -c "{ [ -f /usr/include/gnu/stubs-32.h ] || : > \"${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu/stubs-32.h\"; } && \"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -I\"${CMAKE_CURRENT_BINARY_DIR}/inc32\" -Wl,-soname,libcaca.so.0 -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/libcaca.so.0\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rg-caca-stub/caca_stub.c\" || echo 'waarschuwing: 32-bit libcaca-stub niet gebouwd (gcc -m32)'"
      DEPENDS tools/rg-caca-stub/caca_stub.c VERBATIM)
    add_custom_target(rg_caca_stub32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/libcaca.so.0")
    # De RIO-brug ook 32-bits: Jurassic Park heeft de RIO-bibliotheek in zijn i386-spelbeeld; de brug antwoordt daar als het
    # bord zelf over USB (tools/rio-bridge/, tabel patches/JurassicPark/rio-invoer.ini).
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_rio_bridge.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32" "${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu"
      COMMAND sh -c "{ [ -f /usr/include/gnu/stubs-32.h ] || : > \"${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu/stubs-32.h\"; } && \"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -fno-stack-protector -D_FILE_OFFSET_BITS=64 -I\"${CMAKE_CURRENT_BINARY_DIR}/inc32\" -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_rio_bridge.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rio-bridge/rg_rio_bridge.c\" -lpthread -lm -ldl || echo 'waarschuwing: 32-bit RIO-brug niet gebouwd (gcc -m32)'"
      DEPENDS tools/rio-bridge/rg_rio_bridge.c VERBATIM)
    add_custom_target(rg_rio_bridge32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_rio_bridge.so")
    # Twee paden als één seriële verbinding (tools/rg-ptylink/): Target: Terror Gold koppelde /dev/ttyS0 van de game en
    # /tmp/ttyJamma van zijn bordemulator met socat; dit doet dat zonder socat en zonder shell. 32-bits, zoals het spel.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_ptylink.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32" "${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu"
      COMMAND sh -c "{ [ -f /usr/include/gnu/stubs-32.h ] || : > \"${CMAKE_CURRENT_BINARY_DIR}/inc32/gnu/stubs-32.h\"; } && \"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -I\"${CMAKE_CURRENT_BINARY_DIR}/inc32\" -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_ptylink.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rg-ptylink/rg_ptylink.c\" -ldl -lpthread || echo 'waarschuwing: 32-bit ptylink (librg_ptylink.so) niet gebouwd (gcc -m32)'"
      DEPENDS tools/rg-ptylink/rg_ptylink.c VERBATIM)
    add_custom_target(rg_ptylink32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_ptylink.so")
    # OSS-shim (tools/rg-oss-stub/): /dev/dsp en /dev/mixer voor i386-games in de sandbox. Target: Terror Gold crashte zonder /dev/dsp
    # in zijn geluidsthread (SNDCTL_DSP_GETOSPACE op fd -1, ongevulde struct, memset met een onzinlengte; 2026-10-17). Header-vrij.
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-oss-stub.so"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib32"
      COMMAND sh -c "\"${CMAKE_C_COMPILER}\" -m32 -shared -fPIC -O2 -Wall -o \"${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-oss-stub.so\" \"${CMAKE_CURRENT_SOURCE_DIR}/tools/rg-oss-stub/rg_oss_stub.c\" || echo 'waarschuwing: 32-bit OSS-shim (librg-oss-stub.so) niet gebouwd (gcc -m32)'"
      DEPENDS tools/rg-oss-stub/rg_oss_stub.c VERBATIM)
    add_custom_target(rg_oss_stub32 ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-oss-stub.so")
  else()
    message(WARNING "Geen C-compiler: de LD_PRELOAD-hulp voor de jvs-route wordt niet gebouwd")
  endif()
endif()

# Offline analyse van opnamen (fase 0/1/3); opent nooit hardware.
add_executable(ravengun-analyze src/tools/analyze/ravengun_analyze.cpp)
target_link_libraries(ravengun-analyze PRIVATE ravengun)

# Experimenten naar de oorzaak van bewegingsafhankelijke yaw-drift (varianten van de integratie, zelfde meetlat).
add_executable(ravengun-driftlab src/tools/analyze/driftlab.cpp)
target_link_libraries(ravengun-driftlab PRIVATE ravengun)

# Testprogramma voor de Unity-route (Tomb Raider / Rabbids Hollywood): klant van de BepInEx-plugin, aim via de IPC van ravengund.
add_executable(ravengun-aagames-probe src/tools/aagames_probe/aagames_probe.cpp)
target_link_libraries(ravengun-aagames-probe PRIVATE ravengun Threads::Threads)

# Het JVS-bord van de jvs-route zonder daemon (pty + symlink), voor proeven met een game of met Wine.
add_executable(ravengun-jvs-probe src/tools/jvs_probe/jvs_probe.cpp)
target_link_libraries(ravengun-jvs-probe PRIVATE ravengun Threads::Threads)

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
  target_compile_definitions(ravengun_output_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_output_tests COMMAND ravengun_output_tests)
  add_executable(ravengun_vision_tests tests/test_vision.cpp)
  target_link_libraries(ravengun_vision_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_vision_tests COMMAND ravengun_vision_tests)
  add_executable(ravengun_config_tests tests/test_config.cpp)
  target_link_libraries(ravengun_config_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_config_tests COMMAND ravengun_config_tests)
  add_executable(ravengun_web_tests tests/test_web.cpp)
  target_link_libraries(ravengun_web_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_web_tests COMMAND ravengun_web_tests)
  add_executable(ravengun_games_tests tests/test_games.cpp)
  target_link_libraries(ravengun_games_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_games_tests COMMAND ravengun_games_tests)
  # De poort vóór de start (games/readiness.*): lightgun of pad, wie wacht, wie kalibreert, de time-out; met de echte packaging-JSON.
  add_executable(ravengun_readiness_tests tests/test_readiness.cpp)
  target_link_libraries(ravengun_readiness_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_readiness_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_readiness_tests COMMAND ravengun_readiness_tests)
  add_executable(ravengun_aagames_tests tests/test_aagames.cpp)
  target_link_libraries(ravengun_aagames_tests PRIVATE ravengun Threads::Threads)
  target_compile_definitions(ravengun_aagames_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_aagames_tests COMMAND ravengun_aagames_tests)
  add_executable(ravengun_phase013_tests tests/test_phase013.cpp)
  target_link_libraries(ravengun_phase013_tests PRIVATE ravengun)
  add_test(NAME ravengun_phase013_tests COMMAND ravengun_phase013_tests)
  add_executable(ravengun_fusion_tests tests/test_fusion.cpp)
  target_link_libraries(ravengun_fusion_tests PRIVATE ravengun)
  add_test(NAME ravengun_fusion_tests COMMAND ravengun_fusion_tests)
  add_executable(ravengun_predict_tests tests/test_predict.cpp)
  target_link_libraries(ravengun_predict_tests PRIVATE ravengun)
  add_test(NAME ravengun_predict_tests COMMAND ravengun_predict_tests)
  add_executable(ravengun_rumble_tests tests/test_rumble.cpp)
  target_link_libraries(ravengun_rumble_tests PRIVATE ravengun)
  add_test(NAME ravengun_rumble_tests COMMAND ravengun_rumble_tests)
  add_executable(ravengun_vqf_tests tests/test_vqf.cpp)
  target_link_libraries(ravengun_vqf_tests PRIVATE ravengun)
  add_test(NAME ravengun_vqf_tests COMMAND ravengun_vqf_tests)
  add_executable(ravengun_bias_tests tests/test_bias.cpp)
  target_link_libraries(ravengun_bias_tests PRIVATE ravengun)
  add_test(NAME ravengun_bias_tests COMMAND ravengun_bias_tests)
  add_executable(ravengun_pairing_tests tests/test_pairing.cpp)
  target_link_libraries(ravengun_pairing_tests PRIVATE ravengun)
  add_test(NAME ravengun_pairing_tests COMMAND ravengun_pairing_tests)
  add_test(NAME ravengun_bt_helper COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/bt_helper_test.sh")
  set_tests_properties(ravengun_bt_helper PROPERTIES SKIP_RETURN_CODE 77)
  # De programma's met rootrechten (src/syshelper): argumentcontrole, de bestanden voor BlueZ in een tijdelijke map, de
  # udev-regels/sudoers/Bluetooth-hulp in een tijdelijke root, en de echte programma's zonder root (nooit iets op het systeem).
  add_executable(ravengun_syshelper_tests tests/test_syshelper.cpp)
  target_link_libraries(ravengun_syshelper_tests PRIVATE ravengun_syshelper_setup)
  target_compile_definitions(ravengun_syshelper_tests PRIVATE
    "RG_BT_HELPER_BIN=\"$<TARGET_FILE:ravengun-bt-helper>\"" "RG_SETUP_HELPER_BIN=\"$<TARGET_FILE:ravengun-setup-helper>\""
    "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_dependencies(ravengun_syshelper_tests ravengun-bt-helper ravengun-setup-helper)
  add_test(NAME ravengun_syshelper_tests COMMAND ravengun_syshelper_tests)
  # Instellingen en patches per game (scripts/gamesettings.py, migratie, tp-add-game): lagen, lege waarden, patches aan/uit.
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME ravengun_gamesettings_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gamesettings.py")
    # Het eigen spelprofiel in game.ini: ConfigValues, importer, pad afleiden uit de gamemappen, start zonder UserProfiles-XML, Patreon-poort.
    add_test(NAME ravengun_ownprofile_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ownprofile.py")
    # Dongle (USB pass-through): detectie via een nep-sysfs, runtime-controle, nooit een serienummer (scripts/dongle.py).
    add_test(NAME ravengun_dongle_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dongle.py")
    # De hulpmiddelen voor de Sega Ring-spellen zonder TeknoParrot (docs/research/segajvs/README.md): de statische scan van de
    # AM Library in een exe (poorten, versies, functiebegin) en de meetluisteraar voor 127.0.0.1:40100..40115; synthetische PE, geen spel.
    add_test(NAME ravengun_segajvs_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_segajvs.py")
    # De zoekvolgorde van patches/<Game>/ (loader-patches/ onderaan, gebruiker, $RAVENGUN_PATCHES_DIR bovenaan; een plek, geen
    # tweede kopie in amdaemon/region/matrix/hasp) en het vinden van een game op [Game] Profile in plaats van op de mapnaam.
    add_test(NAME ravengun_patches_roots COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_patches_roots.py")
    # Het schema van een gamemap (levering: game.ini, patches.ini, dropin/**, data/**; de rest is onderzoek en verhuist niet mee),
    # tegen de echte patches/ van de loader en de patches-repo (dezelfde map via een koppeling wordt als een map gecontroleerd).
    add_test(NAME ravengun_patches_layout COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_patches_layout.py")
    # Gedeelde items (shared/<item>): een item staat er een keer en wordt per game in [Shared] aangezet (niets impliciet); de
    # voorrang is die van de patches (eigen map van de game boven de gedeelde standaard, gebruiker boven beide); elk opgelost
    # bestand noemt zijn laag; een aangezet item dat nergens bestaat is een harde fout; de drift tussen twee repo's wordt gezien.
    add_test(NAME ravengun_shared_items COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shared_items.py")
    # Welke camera een spel krijgt (scripts/camera.py): de rol per game, de keuze uit de instellingen, de gebruikerslaag, en dat
    # een game.ini niets anders dan /dev/videoN de sandbox in kan vragen. Hermetisch.
    add_test(NAME ravengun_camera_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_camera.py")
    # Schrijfpaden naar de NAS: geen route van ons schrijft eigen bestanden in een gamemap op de NAS.
    add_test(NAME ravengun_nas_schrijfpaden_views COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_nas_schrijfpaden_views.py")
    # Het profiel van een Unity-game voor de scripts (scripts/unityprofile.py): lagen, mapnaam in de gamemappen, eigen pad, kapot = fout.
    add_test(NAME ravengun_unityprofile_py COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_unityprofile.py")
    add_test(NAME ravengun_aagames_schrijfpaden COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_aagames_schrijfpaden.py")
    # De BepInEx-plugins van de WartranTroopers-familie (plugins/bepinex/shared/Bordbeeld.cs) lezen het bordbeeld zelf: hun tabel is
    # precies het profiel WartranTroopers uit packaging/tp-shm-profiles.json, en de plugins gebruiken alleen die tabel.
    add_test(NAME ravengun_bepinex_bordbeeld COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bepinex_bordbeeld.py")
  endif()
  # Het statusbord van de tv (ravengun --saver): ontbreekt het statusbestand of staat er onzin in, dan zegt het bord dat; een status
  # die niet klopt wordt niet geschreven; en "draait er nu een spel" (zichtbaar, onzichtbaar, geen). Skip (77) zonder app-build.
  add_test(NAME ravengun_saver_status COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/saver_status_test.sh")
  set_tests_properties(ravengun_saver_status PROPERTIES SKIP_RETURN_CODE 77)
  # De invoerhulp van de native route (tools/rt-input-shim): schakelaars, geweerknoppen en assen uit het bordbeeld naar de game,
  # volgens een tabel per game. Zonder tabel het oude gedrag (byte voor byte vergeleken), de rekenregel van de IO-brug voor de
  # assen, en een tabel die niet klopt wordt afgekeurd. Geen spel nodig. Skip (77) zonder C-compiler.
  add_test(NAME ravengun_rt_input COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/rt_input_test.sh")
  set_tests_properties(ravengun_rt_input PROPERTIES SKIP_RETURN_CODE 77)
  # Games zonder TeknoParrot (docs/research/losse/<Game>.md): de zuivere logica van de vervangende DLL's en laders per game, native
  # getest (protocol, afbeelding van het gedeelde geheugen, importtabel); de Windows-DLL's bouwen mee als de mingw-sysroot er is.
  # Geen spel en geen Wine nodig. Skip (77) zonder C-compiler.
  foreach(rg_game_test kaboom pqmt aliens farcry_usbio silentscope_macaw ptt_wk lostland_wajv)
    add_test(NAME ravengun_${rg_game_test} COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/${rg_game_test}_test.sh")
    set_tests_properties(ravengun_${rg_game_test} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  # De RIO-brug (tools/rio-bridge): het lightgun-bord van de native Raw Thrills-Unity-route (NERF) uit het bordbeeld. Echte dlopen
  # van een nep-librio.so, aanpassen, schakelaars als tellers van overgangen, kanalen, een andere build met rust laten, de probe,
  # en de tabellen van NERF. Geen spel nodig. Skip (77) zonder C-compiler.
  add_test(NAME ravengun_rio_bridge COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/rio_bridge_test.sh")
  set_tests_properties(ravengun_rio_bridge PROPERTIES SKIP_RETURN_CODE 77)
  # rg-adrio (tools/rg-adrio): de eigen KaboomAdrioFxPlusFourFeeders64.dll (Rampage; niet te verwarren met tools/rg-kaboom, het
  # seriele Kaboom-bord van Infinity Blade FX). De rekenkern: de tabel per game lezen en in zijn geheel afkeuren, het voorbeeld is de
  # standaard, bordbeeld naar paneel, spelersknoppen en muntentellers (flanken), en de meegeleverde tabellen in patches/. Geen spel
  # en geen Windows nodig. Skip (77) zonder C-compiler.
  add_test(NAME ravengun_adrio COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/adrio_test.sh")
  set_tests_properties(ravengun_adrio PROPERTIES SKIP_RETURN_CODE 77)
  # rg-unis (tools/rg-unis): de eigen UsbPluginsDll.dll van UNIS (Elevator Action Invasion, Raccoon Rampage). De rekenkern: de tabel
  # lezen en afkeuren, muntflanken, bordbeeld naar de bytes van FCoUnisIoData/FESIoData/FPACKET (zoals de PDB's van de games ze
  # lezen) en de meegeleverde tabellen. Geen spel en geen Windows nodig. Skip (77) zonder C-compiler.
  add_test(NAME ravengun_unis COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/unis_test.sh")
  set_tests_properties(ravengun_unis PROPERTIES SKIP_RETURN_CODE 77)
  # De RIO-brug als USB-bord (32-bits): een nep-game zonder PIE met vier pmrt_usb-functies en een xrtea; handvat, trampolines,
  # rapport type 2 met tellers, kanaal en controlesom, met en zonder versleuteling (Aliens Armageddon/Walking Dead), afkeuren
  # zonder versleutelingsregel, niets bij een verkeerde handtekening, en de usb-tabellen in patches/. Skip (77) zonder gcc -m32.
  add_test(NAME ravengun_rio_bridge_usb COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/rio_bridge_usb_test.sh")
  set_tests_properties(ravengun_rio_bridge_usb PROPERTIES SKIP_RETURN_CODE 77)
  # rg-ptylink: twee paden die niet bestaan als één rauwe pty-verbinding (Target: Terror Gold, /dev/ttyS0 <-> /tmp/ttyJamma),
  # 64- en 32-bits, met tegenproef zonder de bibliotheek. Skip (77) zonder C-compiler.
  add_test(NAME ravengun_ptylink COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/ptylink_test.sh")
  set_tests_properties(ravengun_ptylink PROPERTIES SKIP_RETURN_CODE 77)
  # tools/dev/rg-tv-env.sh: de schermomgeving voor een run met beeld op de tv; stopt luid zonder sessie of zonder bereikbaar scherm.
  add_test(NAME ravengun_tv_env COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/tv_env_test.sh")
  # rg-elfloader: gast/libs met alleen DT_GNU_HASH (geen DT_HASH) + analysemodus --ontleed-alleen (start de gast niet).
  add_test(NAME ravengun_elfloader_gnuhash COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/elfloader_gnuhash_test.sh")
  set_tests_properties(ravengun_elfloader_gnuhash PROPERTIES SKIP_RETURN_CODE 77)
  # rg-elfloader: de slottabel per game (RG_ELF_SLOT_TABLE) voor de gepakte klasse -- een synthetische gast met stale eerste-PLT-slots.
  add_test(NAME ravengun_elfloader_slottabel COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/elfloader_slottabel_test.sh")
  set_tests_properties(ravengun_elfloader_slottabel PROPERTIES SKIP_RETURN_CODE 77)
  # rg-elfloader: een DT_NEEDED met een absoluut pad dat in de sandbox niet bestaat (/usr/lib/libcsv.so) -> de basisnaam via
  # LD_LIBRARY_PATH; een bestaand absoluut pad blijft. Aliens Armageddon, Walking Dead, GhostBusters.
  add_test(NAME ravengun_elfloader_absneeded COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/elfloader_absneeded_test.sh")
  set_tests_properties(ravengun_elfloader_absneeded PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME ravengun_elfloader_install COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/elfloader_install_test.sh" "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(ravengun_elfloader_install PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(ravengun_hid_board_tests tests/test_hid_board.cpp)
  target_link_libraries(ravengun_hid_board_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_hid_board_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_hid_board_tests COMMAND ravengun_hid_board_tests)
  add_executable(ravengun_jvs_tests tests/test_jvs.cpp)
  target_link_libraries(ravengun_jvs_tests PRIVATE ravengun Threads::Threads)
  target_compile_definitions(ravengun_jvs_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_jvs_tests COMMAND ravengun_jvs_tests)
  # De AM-daemon van de Sega Ring-games (src/ravengun/launch/amdaemon.*): verzoeken ontleden, antwoorden die de AM Library kan lezen, de
  # luisteraar met een nepcliënt over loopback-sockets.
  add_executable(ravengun_amdaemon_tests tests/test_amdaemon.cpp)
  target_link_libraries(ravengun_amdaemon_tests PRIVATE ravengun Threads::Threads)
  add_test(NAME ravengun_amdaemon_tests COMMAND ravengun_amdaemon_tests)
  # De nagebootste DMI-tabel van rg-amhw (tools/rg-amhw/amhw_dmi.h), nagelezen zoals de AM Library van de Ring-games haar leest.
  add_executable(ravengun_amhw_tests tests/test_amhw.cpp)
  add_test(NAME ravengun_amhw_tests COMMAND ravengun_amhw_tests)
  # De DLL zelf onder Wine zonder game (wegwerp-prefix, MinGW-container); slaat over (77) zonder podman, Wine of image.
  add_test(NAME ravengun_amhw_wine COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/test-amhw-wine.sh")
  set_tests_properties(ravengun_amhw_wine PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 200)
  # De camera voor een spel: het bestand van de daemon voor rg-mfcam.dll (src/ravengun/vision/camera_feed.*).
  add_executable(ravengun_camera_feed_tests tests/test_camera_feed.cpp)
  target_link_libraries(ravengun_camera_feed_tests PRIVATE ravengun)
  add_test(NAME ravengun_camera_feed_tests COMMAND ravengun_camera_feed_tests)
  # Het USIO-model van rg-usio.dll (tools/rg-usio/usio_model.h): gedeeld geheugen -> wat Point Blank X via bnusio.dll leest.
  add_executable(ravengun_usio_model_tests tests/test_usio_model.cpp)
  add_test(NAME ravengun_usio_model_tests COMMAND ravengun_usio_model_tests)
  # Het USIO-bordmodel van winusb.dll voor Time Crisis 5 (tools/rg-winusb/usio_board.h): 6-byte WinUSB-protocol, registers, schakelaars.
  add_executable(ravengun_usio_board_tests tests/test_usio_board.cpp)
  add_test(NAME ravengun_usio_board_tests COMMAND ravengun_usio_board_tests)
  # Het USB-IO-protocol van rg-allsio.dll (tools/rg-allsio/allsio_proto.h): productstring, rapporten, AllsUsbIo-pijp -> ingangsrapport.
  add_executable(ravengun_allsio_proto_tests tests/test_allsio_proto.cpp)
  add_test(NAME ravengun_allsio_proto_tests COMMAND ravengun_allsio_proto_tests)
  # De JVS-omleiding van rg-mfcam.dll (tools/rg-mfcam/jvs_com.h): COM3 -> pipe, sense-lijn.
  add_executable(ravengun_jvs_com_tests tests/test_jvs_com.cpp)
  add_test(NAME ravengun_jvs_com_tests COMMAND ravengun_jvs_com_tests)
  # Het eigen ShareK-object van rg-mfcam.dll (tools/rg-mfcam/sharek_fake.h): CLSID/IID, slottabel van Mario Kart DX.
  add_executable(ravengun_sharek_fake_tests tests/test_sharek_fake.cpp)
  add_test(NAME ravengun_sharek_fake_tests COMMAND ravengun_sharek_fake_tests)
  # docs/compatibility.md moet bij patches/<Game>/game.ini passen (tools/dev/gen-compatibility.py).
  if(Python3_Interpreter_FOUND)
    add_test(NAME ravengun_compat_list COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/gen-compatibility.py" --check)
  endif()
  add_executable(ravengun_gamesettings_tests tests/test_gamesettings.cpp)
  target_link_libraries(ravengun_gamesettings_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_gamesettings_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_gamesettings_tests COMMAND ravengun_gamesettings_tests)
  # Bediening per game ([Controls] in game.ini): formaat, keuzes per speler, opslaan, needsSetup, toepassen op de indeling.
  add_executable(ravengun_controls_tests tests/test_controls.cpp)
  target_link_libraries(ravengun_controls_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_controls_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_controls_tests COMMAND ravengun_controls_tests)
  # De native route zonder shellscripts (src/ravengun/launch/native.*): profiel, commando, invoertabel, schrijflaag, sha256.
  # rg-exec: alleen de genoemde variabelen gaan mee (gamescope -> rg-exec --keep DISPLAY -> bwrap).
  add_test(NAME ravengun_rg_exec COMMAND sh -c "out=$(env FOO=1 DISPLAY=:9 \"$<TARGET_FILE:rg-exec>\" --keep DISPLAY -- /usr/bin/env) && test \"$out\" = DISPLAY=:9")
  add_executable(ravengun_native_launch_tests tests/test_native_launch.cpp)
  target_link_libraries(ravengun_native_launch_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_native_launch_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_native_launch_tests COMMAND ravengun_native_launch_tests)
  # De Unity-route zonder shellscripts (src/ravengun/launch/unity.*): de view met de NAS-controle en het Proton-commando.
  add_executable(ravengun_unity_launch_tests tests/test_unity_launch.cpp)
  target_link_libraries(ravengun_unity_launch_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_unity_launch_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
  add_test(NAME ravengun_unity_launch_tests COMMAND ravengun_unity_launch_tests)
  # De directe route zonder scripts (src/ravengun/launch/direct.*, jvs_pipe.*, tools/rg-direct): plan, weigeren en terugvallen op het
  # script, drop-ins plaatsen zonder op de (nep-)NAS te schrijven, invoerroute, joystick-ids, en rg-direct --run met een nep-wine.
  add_executable(ravengun_direct_launch_tests tests/test_direct_launch.cpp)
  target_link_libraries(ravengun_direct_launch_tests PRIVATE ravengun)
  target_compile_definitions(ravengun_direct_launch_tests PRIVATE "RG_DIRECT_BIN=\"$<TARGET_FILE:rg-direct>\"")
  add_dependencies(ravengun_direct_launch_tests rg-direct)
  add_test(NAME ravengun_direct_launch_tests COMMAND ravengun_direct_launch_tests)
  # Gouden vergelijking: het plan van de engine tegen de vastgelegde plannen in tests/golden/direct/ (gemaakt en nagerekend met
  # de Python-tweeling launch-direct.py toen die er nog was) voor elk profiel in patches/ met [Launch] Mode = direct (hermetisch:
  # tijdelijke HOME, nep-runtime, views en gamemappen). Skip (77) zonder patches/. Bewuste wijziging: tests/direct_golden.py --update.
  if(Python3_Interpreter_FOUND)
    add_test(NAME ravengun_direct_golden COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/direct_golden.py"
      --rg-direct "$<TARGET_FILE:rg-direct>" --patches "${CMAKE_CURRENT_SOURCE_DIR}/patches")
    set_tests_properties(ravengun_direct_golden PROPERTIES SKIP_RETURN_CODE 77)
    # De automatische view ([Launch] View): tp-add-game.py --view en de engine maken dezelfde boom op een nep-NAS;
    # verversen, View = off, weigeren en niets op de NAS schrijven.
    add_test(NAME ravengun_gameview COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gameview.py"
      --rg-direct "$<TARGET_FILE:rg-direct>")
    set_tests_properties(ravengun_gameview PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  add_executable(ravengun_paths_tests tests/test_paths.cpp)
  target_link_libraries(ravengun_paths_tests PRIVATE ravengun)
  add_test(NAME ravengun_paths_tests COMMAND ravengun_paths_tests)
  add_executable(ravengun_drift_tests tests/test_drift.cpp)
  target_link_libraries(ravengun_drift_tests PRIVATE ravengun)
  add_test(NAME ravengun_drift_tests COMMAND ravengun_drift_tests)
  if(RAVENGUN_BUILD_DAEMON)
    # Muis als lightgun: parser, toestandsmachine, IPC-parser en de service met een FIFO als nep-apparaat.
    add_executable(ravengun_mouse_tests tests/test_mouse.cpp src/ravengund/mouse_service.cpp src/ravengund/output_service.cpp)
    target_include_directories(ravengun_mouse_tests PRIVATE src/ravengund)
    target_link_libraries(ravengun_mouse_tests PRIVATE ravengun Threads::Threads)
    add_test(NAME ravengun_mouse_tests COMMAND ravengun_mouse_tests)
    # Gamepad naast de lightgun: opsomming (met redenen), PadReader, indeling uit [Gamepad], IPC-parser, IoState-signalen en
    # GamepadService met een FIFO als nep-apparaat (nooit een echte gamepad).
    add_executable(ravengun_gamepad_tests tests/test_gamepad.cpp src/ravengund/gamepad_service.cpp src/ravengund/output_service.cpp)
    target_include_directories(ravengun_gamepad_tests PRIVATE src/ravengund)
    target_link_libraries(ravengun_gamepad_tests PRIVATE ravengun Threads::Threads)
    target_compile_definitions(ravengun_gamepad_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    add_test(NAME ravengun_gamepad_tests COMMAND ravengun_gamepad_tests)
    # Opdrachtlaag van de daemon (set_led, set_camera, info, ...) zonder hardware: nep-HID-context.
    add_executable(ravengund_command_tests tests/test_commands.cpp src/ravengund/commands.cpp src/ravengund/controllers.cpp
      src/ravengund/camera.cpp src/ravengund/output_service.cpp src/ravengund/aagames_service.cpp src/ravengund/iobridge_service.cpp
      src/ravengund/mouse_service.cpp src/ravengund/gamepad_service.cpp)
    target_include_directories(ravengund_command_tests PRIVATE src/ravengund)
    target_link_libraries(ravengund_command_tests PRIVATE ravengun Threads::Threads)
    target_compile_definitions(ravengund_command_tests PRIVATE ${RAVENGUN_BUILD_DEFS})
    add_test(NAME ravengund_command_tests COMMAND ravengund_command_tests)
    add_executable(ravengund_players_tests tests/test_players.cpp src/ravengund/move_slot.cpp src/ravengund/hid_aux.cpp
      src/ravengund/hidraw_tap.cpp src/ravengund/recorder.cpp src/ravengund/output_service.cpp
      src/ravengund/aagames_service.cpp src/ravengund/iobridge_service.cpp src/ravengund/gamepad_service.cpp)
    target_include_directories(ravengund_players_tests PRIVATE src/ravengund)
    target_link_libraries(ravengund_players_tests PRIVATE ravengun Threads::Threads)
    add_test(NAME ravengund_players_tests COMMAND ravengund_players_tests)
    add_executable(ravengun_iobridge_tests tests/test_iobridge.cpp src/ravengund/output_service.cpp src/ravengund/aagames_service.cpp
      src/ravengund/iobridge_service.cpp src/ravengund/gamepad_service.cpp)
    target_include_directories(ravengun_iobridge_tests PRIVATE src/ravengund)
    target_link_libraries(ravengun_iobridge_tests PRIVATE ravengun Threads::Threads)
    target_compile_definitions(ravengun_iobridge_tests PRIVATE "RAVENGUN_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    add_test(NAME ravengun_iobridge_tests COMMAND ravengun_iobridge_tests)
    # JS-unittests van de kalibratie-UI (node in een wegwerp-container); overgeslagen (77) zonder podman of netwerk.
    add_test(NAME ravengun_calibrate_js COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-js.sh"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(ravengun_calibrate_js PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    add_test(NAME ravengund_web
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/web_smoke.sh" "$<TARGET_FILE:ravengund>")
    # Installatietest (A9): installeren in een tijdelijke prefix/HOME, engine en app daaruit starten, uninstall. Overgeslagen
    # (77) zolang de app niet gebouwd is (scripts/build-app.sh). Zie tests/install_test.sh.
    add_test(NAME ravengun_install
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.sh" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
              "${CMAKE_CURRENT_SOURCE_DIR}/build-app")
    set_tests_properties(ravengun_install PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
    # Indeling van het releasearchief (geen install.sh, startbestand, onderdelen, alleen de levering van de patches): hermetisch,
    # zonder containers. tools/dev/release-assemble.sh, tests/release_layout_test.sh.
    add_test(NAME ravengun_release_layout
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_layout_test.sh" "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(ravengun_release_layout PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    # Integratietest: daemon zonder hardware (--no-hid), socket, lockfile en CLI.
    add_test(NAME ravengund_ipc
      COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/ipc_smoke.sh" "$<TARGET_FILE:ravengund>" "$<TARGET_FILE:ravengunctl>")
  endif()
endif()

# Installeren (docs/build.md, "Installatie-indeling"): `cmake --install build --prefix ~/.local/opt/ravengun` (of via
# scripts/install-user.sh). Indeling: bin/ (ravengun, ravengund, ravengunctl), lib/ (libpsmoveapi.so, de LD_PRELOAD-hulpen, rpath
# $ORIGIN/../lib) en share/ravengun/ (de "datamap": scripts/, packaging/, docs/, plugins/, licenties). Engine en app zoeken de
# datamap in vaste volgorde (RAVENGUN_DATA_DIR, <prefix>/share/ravengun, de repo; src/ravengun/paths.hpp).
set(RAVENGUN_DATADIR "share/ravengun")
install(TARGETS ravengunctl RUNTIME DESTINATION bin)
if(RAVENGUN_BUILD_DAEMON)
  install(TARGETS ravengund RUNTIME DESTINATION bin)
  if(RAVENGUN_PSMOVEAPI_LIB_FILE)
    install(FILES "${RAVENGUN_PSMOVEAPI_LIB_FILE}" DESTINATION lib)
  endif()
endif()
if(TARGET rg_comm_shim)
  install(TARGETS rg_comm_shim rg_rt_input rg_rio_bridge rg_v4l2_fake rg_irtrack LIBRARY DESTINATION lib)
  install(TARGETS rg_fmod10 rg_fmodstudio10 LIBRARY DESTINATION lib/fmod)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_comm_shim.so" "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-threadprio.so"
                "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_rio_bridge.so" "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg_ptylink.so"
                "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-oss-stub.so"
                "${CMAKE_CURRENT_BINARY_DIR}/lib32/librg-glslfix.so" "${CMAKE_CURRENT_BINARY_DIR}/lib32/libcaca.so.0"
          DESTINATION lib/lib32 OPTIONAL)
endif()
# De datamap. Niet mee: build-app.sh (bouwt uit de bronboom). Ontwikkel- en benchgereedschap staat in tools/dev/ en wordt nooit geinstalleerd.
install(DIRECTORY scripts/ DESTINATION ${RAVENGUN_DATADIR}/scripts USE_SOURCE_PERMISSIONS
  PATTERN "build-app.sh" EXCLUDE PATTERN "__pycache__" EXCLUDE)
# Instellingen en patches per game (docs/per-game-patches.md). Die staan in hun EIGEN repo
# (ravengun-patches), niet in die van de loader: het is gebruikersinhoud en de loader hoort schoon
# te zijn. Een verse kloon heeft deze map dus niet, en dan wordt er niets geinstalleerd -- de loader
# vindt de patches via $RAVENGUN_PATCHES_DIR, een geinstalleerde kopie in de datamap, of de
# gebruikerslaag (~/.local/share/ravengun/patches). Staat de map er wel (een ontwikkelcheckout met
# een koppeling ernaartoe), dan gaan ze mee zoals altijd.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/patches")
  # Eerst de koppeling herleiden: install(DIRECTORY) kopieert een symlink als symlink, en de relatieve
  # koppeling "patches -> ../ravengun-patches/patches" wijst vanuit de installatie nergens heen. Dan ziet
  # de daemon geen enkele game.ini en valt elke game terug op de route evdev (gemeten 2026-10-12).
  get_filename_component(RAVENGUN_PATCHES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/patches" REALPATH)
  # Mee: ALLEEN wat de lader, de daemon en de app lezen, plus de README per game. Dat is een vaste lijst:
  #   <Game>/*.ini (game.ini, patches.ini, ...), <Game>/native.json (het native profiel, docs/native-profiel.md), <Game>/options.json (opties en memory card voor de app, docs/game-opties.md),
  #   <Game>/dongle.json, <Game>/README.md, en alles onder <Game>/dropin/ en <Game>/data/ (bestanden die het native profiel in
  #   de sandbox zet, zoals data/udev-rules.d van NERF en Jurassic Park; zonder die map weigert de daemon de start) en
  #   <Game>/seed/ (zaaigoed voor de schrijflaag van de Wine-routes, src/ravengun/games/seed.hpp: de geweerkalibratie van Gaia Attack 4).
  #   (_defaults/ en _template/ zijn mappen op dezelfde hoogte als een game en vallen onder dezelfde regels.)
  # Al het andere in de patches-repo is DOSSIER: logboeken van benchruns, metingen, meetscripts, onderzoeksnotities,
  # foto's, generatoren. Dat is bewijs en hoort in die repo, maar niet in een installatie: niets leest het daar, en het
  # noemt ontwikkelpaden, waar tests/install_test.sh terecht op afkeurt ("de installatie noemt het repopad").
  # Daarom een lijst van wat MEE gaat en geen lijst van wat niet mee gaat: een lijst met uitzonderingen loopt achter
  # zodra iemand een nieuwe bewijsmap een nieuwe naam geeft (probe/, logs-*/, measurements/, probes/, ...), en dan
  # breekt de installatietest op iets wat geen lek is. Een nieuw soort bestand dat de lader WEL moet lezen komt hier bij.
  string(REGEX REPLACE "([][+.*()^$?|\\\\])" "\\\\\\1" _rg_patches_rx "${RAVENGUN_PATCHES_SRC}")
  install(DIRECTORY "${RAVENGUN_PATCHES_SRC}/" DESTINATION ${RAVENGUN_DATADIR}/patches
    FILES_MATCHING
    REGEX "^${_rg_patches_rx}/[^/]+/([^/]+\\.ini|native\\.json|unity\\.json|options\\.json|dongle\\.json|README\\.md)$"
    REGEX "^${_rg_patches_rx}/[^/]+/dropin/"
    REGEX "^${_rg_patches_rx}/[^/]+/data/"
    REGEX "^${_rg_patches_rx}/[^/]+/seed/")
  # FILES_MATCHING laat de mappen zelf wel aanmaken, ook als er niets in komt. Lege mappen (de bewijsmappen) weer weg.
  install(CODE "
    set(_rg_p \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${RAVENGUN_DATADIR}/patches\")
    foreach(_rg_ronde RANGE 12)
      file(GLOB_RECURSE _rg_d LIST_DIRECTORIES true \"\${_rg_p}/*\")
      set(_rg_weg FALSE)
      foreach(_rg_x IN LISTS _rg_d)
        if(IS_DIRECTORY \"\${_rg_x}\" AND NOT IS_SYMLINK \"\${_rg_x}\")
          file(GLOB _rg_c \"\${_rg_x}/*\")
          if(NOT _rg_c)
            file(REMOVE_RECURSE \"\${_rg_x}\")
            set(_rg_weg TRUE)
          endif()
        endif()
      endforeach()
      if(NOT _rg_weg)
        break()
      endif()
    endforeach()
  ")
else()
  message(STATUS "geen patches/-map: die staan in de repo ravengun-patches (zie docs/geen-dongle-in-de-loader.md)")
endif()
install(DIRECTORY packaging/ DESTINATION ${RAVENGUN_DATADIR}/packaging USE_SOURCE_PERMISSIONS
  PATTERN "containers/qt-app" EXCLUDE PATTERN "containers/release" EXCLUDE PATTERN "release" EXCLUDE PATTERN "icons/halo.png" EXCLUDE
  PATTERN "desktop/game-*.desktop" EXCLUDE PATTERN "desktop/halo-ravengun.desktop" EXCLUDE
  PATTERN "desktop/teknoparrot.desktop" EXCLUDE PATTERN "desktop/ravengun-stop.desktop" EXCLUDE
  PATTERN "desktop/ravengun.desktop" EXCLUDE PATTERN "desktop/ravengun-qt.desktop" EXCLUDE PATTERN "desktop/ravengun-web.desktop" EXCLUDE)
install(DIRECTORY plugins/unity DESTINATION ${RAVENGUN_DATADIR}/plugins)
# De gebouwde Unity-plugins (scripts/build-unity-plugin.sh schrijft ze in de repo naar build/unity-plugin/<Game>/): naast de eigen
# bibliotheken, waar de daemon ze zoekt bij een start zonder script (src/ravengun/launch/unity.*). Nog niet gebouwd = overgeslagen.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/unity-plugin/" DESTINATION lib/unity-plugin OPTIONAL)
install(FILES docs/user-guide.md DESTINATION ${RAVENGUN_DATADIR}/docs)
install(FILES LICENSE THIRD_PARTY.md DESTINATION ${RAVENGUN_DATADIR})
install(DIRECTORY third_party/ DESTINATION ${RAVENGUN_DATADIR}/third_party FILES_MATCHING PATTERN "README.md" PATTERN "LICENSE*")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/VERSION" "${PROJECT_VERSION}\n${RAVENGUN_GIT_COMMIT}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VERSION" DESTINATION ${RAVENGUN_DATADIR})

# De Qt-app `ravengun` (src/app; CMake-doel ravengun_app, want `ravengun` is al de naam van de bibliotheek). Alleen als
# Qt 6 met headers en CMake-bestanden beschikbaar is. SteamOS heeft wel de systeem-Qt maar geen headers; daar bouwt
# scripts/build-app.sh de app in een container (docs/build.md). Zonder Qt blijven de gewone build en ctest ongewijzigd.
option(RAVENGUN_BUILD_APP "Bouw de Qt-app (ravengun) als Qt 6 beschikbaar is" ON)
if(RAVENGUN_BUILD_APP)
  find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Widgets Network)
  find_package(LibArchive QUIET)   # onderdelen uitpakken zonder tar/unzip (src/app/core/ArchiveExtract); headers zitten in de buildcontainer
  if(Qt6_FOUND AND Qt6Widgets_FOUND AND Qt6Network_FOUND AND LibArchive_FOUND)
    message(STATUS "Qt ${Qt6_VERSION}: de app (ravengun) wordt gebouwd")
    add_subdirectory(src/app)
  else()
    message(STATUS "Qt 6 of libarchive (met headers) niet gevonden: de app (ravengun) wordt niet gebouwd; zie docs/build.md")
  endif()
endif()
