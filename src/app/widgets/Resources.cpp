// SPDX-License-Identifier: GPL-2.0-or-later
// Naar Dolphin, Source/Core/DolphinQt/Resources.cpp (GPL-2.0-or-later, commit e6f3ae17627e4344da95b13424af5baf4c892b08).
// Copyright 2015 Dolphin Emulator Project.
#include "widgets/Resources.h"

#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QStyle>

namespace rg::ui {

QIcon Resources::LoadNamedIcon(std::string_view name, const QString& dir) {
    const QString base_path = dir + QLatin1Char{'/'} + QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
    QIcon icon;
    auto load_png = [&](int scale) {
        QString suffix = QStringLiteral(".png");
        if (scale > 1) suffix = QString::fromLatin1("@%1x.png").arg(scale);
        QPixmap pixmap(base_path + suffix);
        if (!pixmap.isNull()) {
            pixmap.setDevicePixelRatio(scale);
            icon.addPixmap(pixmap);
        }
    };
    // Since we are caching the files, we need to try and load all known sizes up-front.
    for (auto scale : {1, 2, 4}) load_png(scale);
    return icon;
}

QIcon Resources::GetThemeIcon(std::string_view name) {
    static QHash<QString, QIcon> cache;
    const QString key = QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
    auto it = cache.find(key);
    if (it != cache.end()) return *it;
    QIcon icon = LoadNamedIcon(name, QStringLiteral(":/themes/clean"));
    if (icon.isNull()) {
        // Eigen namen zonder Dolphin-icoon: een freedesktop-thema-icoon, anders een standaardicoon van de stijl.
        static const QHash<QString, QPair<QString, QStyle::StandardPixmap>> fallback{
            {QStringLiteral("properties"), {QStringLiteral("document-properties"), QStyle::SP_FileDialogDetailedView}},
            {QStringLiteral("folder"), {QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon}},
            {QStringLiteral("delete"), {QStringLiteral("edit-delete"), QStyle::SP_TrashIcon}},
            {QStringLiteral("engine"), {QStringLiteral("system-run"), QStyle::SP_ComputerIcon}},
            {QStringLiteral("pair"), {QStringLiteral("network-wireless"), QStyle::SP_DriveNetIcon}},
        };
        const auto f = fallback.value(key);
        if (!f.first.isEmpty()) icon = QIcon::fromTheme(f.first);
        if (icon.isNull() && qApp) icon = qApp->style()->standardIcon(f.second);
    }
    cache.insert(key, icon);
    return icon;
}

QIcon Resources::GetAppIcon() {
    // Getekend (geen extra bestand of SVG-plugin nodig): een vizier op een donkerrood vlak.
    static QIcon icon;
    if (!icon.isNull()) return icon;
    for (int size : {16, 32, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x8e, 0x1b, 0x1b));
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.18, size * 0.18);
        QPen pen(Qt::white);
        pen.setWidthF(std::max(1.5, size * 0.07));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QPointF c(size / 2.0, size / 2.0);
        p.drawEllipse(c, size * 0.28, size * 0.28);
        p.drawLine(QPointF(c.x(), size * 0.1), QPointF(c.x(), size * 0.36));
        p.drawLine(QPointF(c.x(), size * 0.64), QPointF(c.x(), size * 0.9));
        p.drawLine(QPointF(size * 0.1, c.y()), QPointF(size * 0.36, c.y()));
        p.drawLine(QPointF(size * 0.64, c.y()), QPointF(size * 0.9, c.y()));
        icon.addPixmap(pm);
    }
    return icon;
}

}  // namespace rg::ui
