// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/SteamIntegration.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>

#include "core/AppPaths.h"

namespace rg::ui {

rg::ShortcutSpec RavenGunShortcutSpec(bool writeIcon) {
    rg::ShortcutSpec s;
    s.appName = QStringLiteral("RavenGun");
    s.exe = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    s.startDir = QFileInfo(s.exe).absolutePath();
    s.launchOptions = QStringLiteral("--fullscreen");
    const QString icon = rg::AppPaths::dataDir() + QStringLiteral("/ravengun-icon.png");
    if (writeIcon) {
        QDir().mkpath(QFileInfo(icon).path());
        rg::artwork::icon(256).save(icon, "PNG");
    }
    if (QFileInfo::exists(icon)) s.icon = icon;
    return s;
}

rg::ShortcutSpec GameShortcutSpec(const rg::GameEntry& game) {
    rg::ShortcutSpec s = RavenGunShortcutSpec(false);
    s.appName = game.displayName();
    s.launchOptions = QStringLiteral("--play ") + game.profile;
    if (!game.iconPath.isEmpty() && QFileInfo::exists(game.iconPath)) s.icon = game.iconPath;
    return s;
}

namespace {

// game = nullptr: RavenGun zelf (Big Picture, met de eigen afbeeldingen); anders een game (alleen het icoon, geen afbeeldingen).
SteamAddOutcome apply(const rg::SteamUser& user, const rg::GameEntry* game = nullptr) {
    SteamAddOutcome o;
    const QString cfg = user.configDir();
    if (!QFileInfo(cfg).isDir() && !QDir().mkpath(cfg)) {
        o.message = QStringLiteral("The Steam folder %1 does not exist.").arg(cfg);
        return o;
    }
    const rg::ShortcutSpec spec = game ? GameShortcutSpec(*game) : RavenGunShortcutSpec();
    const rg::ShortcutResult r = rg::SteamShortcuts::addOrUpdate(cfg + QStringLiteral("/shortcuts.vdf"), spec);
    if (!r.ok) {
        o.message = r.error;
        return o;
    }
    QString gerr;
    QStringList imgs;
    if (!game)
        imgs = rg::SteamShortcuts::writeGrid(cfg + QStringLiteral("/grid"), r.appId, rg::artwork::portrait(), rg::artwork::landscape(),
                                             rg::artwork::hero(), rg::artwork::logo(), rg::artwork::icon(256), &gerr);
    o.ok = true;
    o.message = (r.updated ? QStringLiteral("%1 was updated in the Steam library") : QStringLiteral("%1 was added to the Steam library")).arg(spec.appName) +
                (user.name.isEmpty() ? QString() : QStringLiteral(" of %1").arg(user.name)) + QStringLiteral(".\n\n") +
                QStringLiteral("Program: %1 %2\nShortcut id: %3\n").arg(spec.exe, spec.launchOptions).arg(r.appId) +
                (r.backup.isEmpty() ? QString() : QStringLiteral("Backup of the old list: %1\n").arg(r.backup)) +
                QStringLiteral("Artwork: %1 image(s)%2").arg(imgs.size()).arg(gerr.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(gerr));
    return o;
}

}  // namespace

SteamAddOutcome AddToSteamQuiet(const QString& account) {
    SteamAddOutcome o;
    const QString root = rg::SteamShortcuts::steamRoot();
    if (root.isEmpty()) {
        o.message = QStringLiteral("Steam was not found (no ~/.local/share/Steam/userdata).");
        return o;
    }
    const QList<rg::SteamUser> users = rg::SteamShortcuts::users(root);
    if (users.isEmpty()) {
        o.message = QStringLiteral("No Steam user was found. Log in to Steam once first.");
        return o;
    }
    rg::SteamUser user = users.first();
    if (!account.isEmpty()) {
        bool found = false;
        for (const auto& u : users)
            if (u.accountId == account) {
                user = u;
                found = true;
            }
        if (!found) {
            o.message = QStringLiteral("Steam user %1 was not found.").arg(account);
            return o;
        }
    }
    if (qEnvironmentVariable("RAVENGUN_STEAM_ROOT").isEmpty() && rg::SteamShortcuts::steamRunning()) {
        o.message = QStringLiteral("Steam is running. Close Steam first (Steam > Exit), then try again.");
        return o;
    }
    return apply(user);
}

SteamAddOutcome AddToSteamInteractive(QWidget* parent, const rg::GameEntry* game) {
    SteamAddOutcome o;
    const QString title = QObject::tr("Add to Steam");
    const QString what = game ? game->displayName() : QStringLiteral("RavenGun");
    const QString root = rg::SteamShortcuts::steamRoot();
    if (root.isEmpty()) {
        o.message = QObject::tr("Steam was not found on this PC.");
        QMessageBox::warning(parent, title, o.message);
        return o;
    }
    QList<rg::SteamUser> users = rg::SteamShortcuts::users(root);
    if (users.isEmpty()) {
        o.message = QObject::tr("No Steam user was found. Log in to Steam once, then try again.");
        QMessageBox::warning(parent, title, o.message);
        return o;
    }
    rg::SteamUser user = users.first();
    if (users.size() > 1) {
        QStringList items;
        for (const auto& u : users) items << (u.name.isEmpty() ? u.accountId : QStringLiteral("%1 (%2)").arg(u.name, u.accountId));
        bool ok = false;
        const QString pick = QInputDialog::getItem(parent, title, QObject::tr("Add %1 to the library of:").arg(what), items, 0, false, &ok);
        if (!ok) return o;
        user = users.value(static_cast<int>(items.indexOf(pick)), users.first());
    }
    // Steam leest en overschrijft shortcuts.vdf zelf: eerst sluiten. Nooit zonder vragen.
    const bool testRoot = !qEnvironmentVariable("RAVENGUN_STEAM_ROOT").isEmpty();
    if (!testRoot && rg::SteamShortcuts::steamRunning()) {
        QMessageBox box(QMessageBox::Question, title,
                        QObject::tr("Steam is running. Steam must be closed while %1 is added, otherwise Steam overwrites "
                                    "the change.\n\nClose Steam now? Games running in Steam are closed too.").arg(what),
                        QMessageBox::NoButton, parent);
        QPushButton* close = box.addButton(QObject::tr("Close Steam"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != reinterpret_cast<QAbstractButton*>(close)) return o;
        QProcess::startDetached(QStringLiteral("steam"), {QStringLiteral("-shutdown")});
        QProgressDialog wait(QObject::tr("Waiting for Steam to close..."), QObject::tr("Cancel"), 0, 0, parent);
        wait.setWindowModality(Qt::WindowModal);
        wait.show();
        QElapsedTimer t;
        t.start();
        while (rg::SteamShortcuts::steamRunning() && t.elapsed() < 45000 && !wait.wasCanceled())
            QApplication::processEvents(QEventLoop::AllEvents, 200);
        wait.close();
        if (rg::SteamShortcuts::steamRunning()) {
            o.message = QObject::tr("Steam is still running. Close Steam yourself (Steam > Exit) and try again.");
            QMessageBox::warning(parent, title, o.message);
            return o;
        }
    }
    o = apply(user, game);
    if (o.ok) {
        QMessageBox::information(parent, title,
                                 o.message + QObject::tr("\n\nStart Steam again. In Game Mode, %1 is in your library under "
                                                         "Non-Steam games.").arg(what) +
                                     (game ? QObject::tr(" It first asks you to turn on your PS Move and calibrates the aim, then "
                                                         "starts the game; quitting the game returns to Steam.")
                                           : QString()));
    } else {
        QMessageBox::warning(parent, title, o.message);
    }
    return o;
}

bool IsInSteam(QString* where) {
    const QString root = rg::SteamShortcuts::steamRoot();
    const QList<rg::SteamUser> users = rg::SteamShortcuts::users(root);
    if (users.isEmpty()) return false;
    const QString vdf = users.first().configDir() + QStringLiteral("/shortcuts.vdf");
    for (const auto& e : rg::SteamShortcuts::list(vdf))
        if (e.appName == QStringLiteral("RavenGun")) {
            if (where) *where = vdf;
            return true;
        }
    return false;
}

}  // namespace rg::ui
