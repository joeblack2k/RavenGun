# RavenGun-app (Qt 6 Widgets). Alleen gebouwd als Qt 6 met headers en CMake-bestanden beschikbaar is (zie de hoofd-
# CMakeLists.txt en docs/build.md). Op SteamOS: scripts/build-app.sh bouwt in de container localhost/ravengun-qt-build
# tegen Qt 6.11, dezelfde hoofd/minor-versie als de systeem-Qt, zodat de binary op de host draait.
#
# Twee delen:
#  - ravengun_app_core: kern zonder widgets (Qt Core/Gui/Network): engineverbinding, bibliotheek, opties, memory card,
#    launcher-backends. Ook bedoeld voor een latere fullscreen-modus (QML of Widgets).
#  - ravengun_app: het programma `ravengun` (Qt Widgets), opgebouwd naar het voorbeeld van Dolphin (THIRD_PARTY.md).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(ravengun_app_core STATIC
  core/AppCore.cpp
  core/ArchiveExtract.cpp
  core/ComponentInstaller.cpp
  core/ComponentsCli.cpp
  core/Components.cpp
  core/AppPaths.cpp
  core/ControlsLegend.cpp
  core/ControlsWalk.cpp
  core/DongleStatus.cpp
  core/BigPicture.cpp
  core/EngineClient.cpp
  core/FileSafety.cpp
  core/GameData.cpp
  core/GameLauncher.cpp
  core/GameLibrary.cpp
  core/GameListModel.cpp
  core/Install.cpp
  core/GameOptions.cpp
  core/Launcher.cpp
  core/MemoryCard.cpp
  core/PreLaunch.cpp
  core/SaverLoop.cpp
  core/SetupChecks.cpp
  core/SteamShortcuts.cpp
  core/SystemControl.cpp
  core/TextConfig.cpp
  core/TpProfile.cpp
  # Instellingen en patches per game: dezelfde lezer als de engine (alleen de standaardbibliotheek).
  "${PROJECT_SOURCE_DIR}/src/ravengun/gamesettings/gamesettings.cpp"
  "${PROJECT_SOURCE_DIR}/src/ravengun/launch/gamefolder.cpp"
  # De gamepad-indeling per game ([Gamepad] in game.ini; Properties en Controller Settings): dezelfde code als de engine.
  "${PROJECT_SOURCE_DIR}/src/ravengun/gamesettings/gamepad_settings.cpp"
  "${PROJECT_SOURCE_DIR}/src/ravengun/gamesettings/controls_settings.cpp"
  "${PROJECT_SOURCE_DIR}/src/ravengun/output/gamepad.cpp")
target_include_directories(ravengun_app_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
# libarchive: tar.gz/xz, zip, zst en cab uitpakken zonder tar/unzip (core/ArchiveExtract). Zit in de buildcontainer (pacman hangt eraan)
# en op SteamOS (libarchive.so.13).
find_package(LibArchive REQUIRED)   # (de hoofd-CMakeLists controleert dit al)
target_link_libraries(ravengun_app_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network LibArchive::LibArchive)

# packaging/*.json als terugval in de binary (de bestanden in de repo gaan voor, zie AppPaths::packagingFile).
set(_rg_qrc "${CMAKE_CURRENT_BINARY_DIR}/packaging.qrc")
set(_rg_qrc_body "<RCC><qresource prefix=\"/packaging\">\n")
foreach(_f hid-boards.json jvs-boards.json components.json)
  if(EXISTS "${PROJECT_SOURCE_DIR}/packaging/${_f}")
    string(APPEND _rg_qrc_body "  <file alias=\"${_f}\">${PROJECT_SOURCE_DIR}/packaging/${_f}</file>\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/packaging/${_f}")
  endif()
endforeach()
string(APPEND _rg_qrc_body "</qresource></RCC>\n")
file(WRITE "${_rg_qrc}" "${_rg_qrc_body}")

# Vensters als bibliotheek, zodat de widgettests ze ook kunnen gebruiken; main.cpp alleen in het programma.
add_library(ravengun_app_widgets STATIC
  widgets/BigPictureWindow.cpp
  widgets/ControllersWindow.cpp
  widgets/ComponentsPage.cpp
  widgets/ControlsSetup.cpp
  widgets/CreateProfileDialog.cpp
  widgets/GameList.cpp
  widgets/MainWindow.cpp
  widgets/MenuBar.cpp
  widgets/MoveConfigDialog.cpp
  widgets/PairWizardDialog.cpp
  widgets/PreLaunchWindow.cpp
  widgets/PropertiesDialog.cpp
  widgets/QtUtils.cpp
  widgets/Resources.cpp
  widgets/SaverWindow.cpp
  widgets/Screenshots.cpp
  widgets/SearchBar.cpp
  widgets/SettingsWindow.cpp
  widgets/SetupWizard.cpp
  widgets/StackedSettingsWindow.cpp
  widgets/SteamIntegration.cpp
  widgets/ToolBar.cpp)
target_link_libraries(ravengun_app_widgets PUBLIC ravengun_app_core Qt6::Widgets)
target_compile_definitions(ravengun_app_widgets PRIVATE "RAVENGUN_APP_VERSION=\"${PROJECT_VERSION}\"")

add_executable(ravengun_app
  widgets/main.cpp
  resources/app.qrc
  "${_rg_qrc}")
set_target_properties(ravengun_app PROPERTIES OUTPUT_NAME ravengun RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(ravengun_app PRIVATE ravengun_app_widgets)
target_compile_definitions(ravengun_app PRIVATE "RAVENGUN_APP_VERSION=\"${PROJECT_VERSION}\"")
# Geïnstalleerd (docs/build.md, "Installatie-indeling"): bibliotheken van RavenGun in <prefix>/lib.
set_target_properties(ravengun_app PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS ravengun_app RUNTIME DESTINATION bin COMPONENT app)

if(RAVENGUN_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    add_executable(ravengun_app_tests "${PROJECT_SOURCE_DIR}/tests/app/test_app_core.cpp")
    target_link_libraries(ravengun_app_tests PRIVATE ravengun_app_core Qt6::Test)
    set_target_properties(ravengun_app_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(ravengun_app_tests PRIVATE "RAVENGUN_TEST_DATA=\"${PROJECT_SOURCE_DIR}/tests/app/data\""
      "RAVENGUN_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" "RAVENGUN_APP_BIN=\"$<TARGET_FILE:ravengun_app>\"")
    add_dependencies(ravengun_app_tests ravengun_app)
    add_test(NAME ravengun_app_tests COMMAND ravengun_app_tests)
    set_tests_properties(ravengun_app_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Onderdelen (catalogus, uitpakken, installeren in een tijdelijke map).
    add_executable(ravengun_components_tests "${PROJECT_SOURCE_DIR}/tests/app/test_components.cpp")
    target_link_libraries(ravengun_components_tests PRIVATE ravengun_app_core Qt6::Test)
    set_target_properties(ravengun_components_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(ravengun_components_tests PRIVATE "RAVENGUN_TEST_DATA=\"${PROJECT_SOURCE_DIR}/tests/app/data\""
      "RAVENGUN_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"" "RAVENGUN_APP_BIN=\"$<TARGET_FILE:ravengun_app>\"")
    add_dependencies(ravengun_components_tests ravengun_app)
    add_test(NAME ravengun_components_tests COMMAND ravengun_components_tests)
    set_tests_properties(ravengun_components_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Vensters met de echte patches/<Game>/options.json (alle games; patches/ in de repo of RAVENGUN_PATCHES_DIR), de fullscreen-modus, de wizard en Steam-afbeeldingen.
    add_executable(ravengun_app_widget_tests "${PROJECT_SOURCE_DIR}/tests/app/test_app_widgets.cpp" resources/app.qrc "${_rg_qrc}")
    target_link_libraries(ravengun_app_widget_tests PRIVATE ravengun_app_widgets Qt6::Test)
    set_target_properties(ravengun_app_widget_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(ravengun_app_widget_tests PRIVATE "RAVENGUN_TEST_DATA=\"${PROJECT_SOURCE_DIR}/tests/app/data\""
      "RAVENGUN_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    add_test(NAME ravengun_app_widget_tests COMMAND ravengun_app_widget_tests)
    set_tests_properties(ravengun_app_widget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
