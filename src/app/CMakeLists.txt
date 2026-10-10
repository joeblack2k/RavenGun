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
  core/AppPaths.cpp
  core/BigPicture.cpp
  core/EngineClient.cpp
  core/FileSafety.cpp
  core/GameData.cpp
  core/GameLauncher.cpp
  core/GameLibrary.cpp
  core/GameListModel.cpp
  core/GameOptions.cpp
  core/Launcher.cpp
  core/MemoryCard.cpp
  core/SetupChecks.cpp
  core/SteamShortcuts.cpp
  core/SystemControl.cpp
  core/TextConfig.cpp
  core/TpProfile.cpp)
target_include_directories(ravengun_app_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ravengun_app_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# packaging/*.json als terugval in de binary (de bestanden in de repo gaan voor, zie AppPaths::packagingFile).
set(_rg_qrc "${CMAKE_CURRENT_BINARY_DIR}/packaging.qrc")
set(_rg_qrc_body "<RCC><qresource prefix=\"/packaging\">\n")
foreach(_f io-routes.json games-notes.json aagames-games.json game-options.json hid-boards.json jvs-boards.json)
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
  widgets/CreateProfileDialog.cpp
  widgets/GameList.cpp
  widgets/MainWindow.cpp
  widgets/MenuBar.cpp
  widgets/MoveConfigDialog.cpp
  widgets/PairWizardDialog.cpp
  widgets/PropertiesDialog.cpp
  widgets/QtUtils.cpp
  widgets/Resources.cpp
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
install(TARGETS ravengun_app RUNTIME DESTINATION bin)

if(RAVENGUN_BUILD_TESTS)
  find_package(Qt6 QUIET COMPONENTS Test)
  if(Qt6Test_FOUND)
    add_executable(ravengun_app_tests "${PROJECT_SOURCE_DIR}/tests/app/test_app_core.cpp")
    target_link_libraries(ravengun_app_tests PRIVATE ravengun_app_core Qt6::Test)
    set_target_properties(ravengun_app_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(ravengun_app_tests PRIVATE "RAVENGUN_TEST_DATA=\"${PROJECT_SOURCE_DIR}/tests/app/data\""
      "RAVENGUN_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    add_test(NAME ravengun_app_tests COMMAND ravengun_app_tests)
    set_tests_properties(ravengun_app_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    # Vensters met de echte packaging/game-options.json (alle games), de fullscreen-modus, de wizard en Steam-afbeeldingen.
    add_executable(ravengun_app_widget_tests "${PROJECT_SOURCE_DIR}/tests/app/test_app_widgets.cpp" resources/app.qrc "${_rg_qrc}")
    target_link_libraries(ravengun_app_widget_tests PRIVATE ravengun_app_widgets Qt6::Test)
    set_target_properties(ravengun_app_widget_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_compile_definitions(ravengun_app_widget_tests PRIVATE "RAVENGUN_TEST_DATA=\"${PROJECT_SOURCE_DIR}/tests/app/data\""
      "RAVENGUN_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    add_test(NAME ravengun_app_widget_tests COMMAND ravengun_app_widget_tests)
    set_tests_properties(ravengun_app_widget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
