# Code en bestanden van derden

RavenGun valt onder GPL-3.0-or-later (LICENSE). Hieronder wat uit andere projecten is overgenomen, met bron en licentie.

## Dolphin Emulator (GPL-2.0-or-later)

Bron: https://github.com/dolphin-emu/dolphin, commit `e6f3ae17627e4344da95b13424af5baf4c892b08` (7 oktober 2026). Copyright Dolphin Emulator Project. GPL-2.0-or-later is verenigbaar met GPL-3.0-or-later; de overgenomen bestanden houden hun eigen SPDX-regel (GPL-2.0-or-later) en noemen het Dolphin-bestand in hun kop. De gebruikersinterface van de Qt-app (`src/app`) is opgebouwd naar het voorbeeld van DolphinQt.

Overgenomen code (aangepast voor RavenGun):

| RavenGun | Dolphin (Source/Core/DolphinQt/...) | Wat |
|---|---|---|
| `src/app/widgets/StackedSettingsWindow.{h,cpp}` | `Config/SettingsWindow.{h,cpp}` (StackedSettingsWindow) | venster met navigatielijst links en panelen rechts (Properties, Settings) |
| `src/app/widgets/QtUtils.{h,cpp}` | `QtUtils/WrapInScrollArea.{h,cpp}`, `QtUtils/NonDefaultQPushButton.{h,cpp}` | scrollgebied dat de grootte van zijn inhoud voorstelt; knop die geen standaardknop wordt |
| `src/app/widgets/SearchBar.{h,cpp}` | `SearchBar.{h,cpp}` | zoekbalk boven de gamelijst |
| `src/app/widgets/ToolBar.{h,cpp}` | `ToolBar.{h,cpp}` | werkbalk met tekst onder 32-px-iconen en gelijke knopbreedtes |
| `src/app/widgets/MenuBar.{h,cpp}` | `MenuBar.{h,cpp}` (AddGameListTypeSection, AddListColumnsMenu, menuopbouw) | menubalk |
| `src/app/widgets/GameList.{h,cpp}` | `GameList/GameList.{h,cpp}`, `GameList/ListProxyModel.{h,cpp}`, `GameList/GridProxyModel.{h,cpp}` | lijst- en rasterweergave in een QStackedWidget, proxymodellen, zoomen, OnHeaderViewChanged, rechtermuismenu, lege weergave met dubbelklik |
| `src/app/widgets/Resources.{h,cpp}` | `Resources.{h,cpp}` (LoadNamedIcon) | iconen met @2x/@4x laden |
| `src/app/core/GameListModel.{h,cpp}` | `GameList/GameListModel.{h,cpp}` | structuur: kolommen als enum, SORT_ROLE, zoekfilter, schaal |

Alleen als voorbeeld voor de opbouw (geen code letterlijk overgenomen): `MainWindow.cpp` (CreateComponents, ConnectMenuBar/ToolBar/GameList), `Config/GamecubeControllersWidget.cpp` (rijen "Port N" met keuzelijst en Configure; bij ons ControllersWindow), `Config/PropertiesDialog.cpp` en `Settings/PathPane.cpp` (Game Folders met Add.../Remove en "Search Subfolders").

Bestanden: de iconen van het thema "Clean" (`Data/Sys/Themes/Clean`: browse, classic, config, fullscreen, gcpad, graphics, open, play, refresh, stop, telkens 1x/2x/4x) staan in `src/app/resources/themes/clean/` en zijn via `src/app/resources/app.qrc` in de app ingebouwd.

## Overige

- VQF (MIT): `third_party/vqf/`, zie `third_party/vqf/README.md`.
