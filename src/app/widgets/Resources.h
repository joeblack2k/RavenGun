// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/Resources.{h,cpp} (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08):
// LoadNamedIcon laadt <naam>.png, <naam>@2x.png en <naam>@4x.png als één QIcon. De iconen zelf zijn het thema "Clean" uit
// Dolphin (Data/Sys/Themes/Clean), ingebouwd als Qt-resource (:/themes/clean). Copyright 2015 Dolphin Emulator Project.
#pragma once
#include <QIcon>
#include <string_view>

namespace rg::ui {

class Resources {
public:
    // Thema-icoon (open, refresh, play, stop, fullscreen, config, classic, browse, ...). Ontbreekt het, dan het
    // freedesktop-icoon `fallbackTheme` of een standaardicoon van de stijl.
    static QIcon GetThemeIcon(std::string_view name);
    static QIcon GetAppIcon();

private:
    static QIcon LoadNamedIcon(std::string_view name, const QString& dir);
};

}  // namespace rg::ui
