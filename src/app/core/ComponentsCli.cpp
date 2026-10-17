// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ComponentsCli.h"

#include <QEventLoop>
#include <QHash>

#include "core/ComponentInstaller.h"

namespace rg {

QString ComponentsCli::stateName(ComponentState s) {
    switch (s) {
        case ComponentState::Installed: return QStringLiteral("installed");
        case ComponentState::Missing: return QStringLiteral("missing");
        case ComponentState::Outdated: return QStringLiteral("outdated");
        case ComponentState::Unavailable: return QStringLiteral("not available");
    }
    return {};
}

int ComponentsCli::list(const QList<Component>& all, const ComponentEnv& env, std::FILE* out) {
    for (const Component& c : all) {
        const ComponentStatus st = Components::status(c, env);
        std::fprintf(out, "%s\t%s\t%s\t%s\t%s\n", qPrintable(c.id), qPrintable(c.name), qPrintable(c.version.isEmpty() ? QStringLiteral("-") : c.version),
                     qPrintable(c.totalSize() > 0 ? Components::formatSize(c.totalSize()) : QStringLiteral("-")), qPrintable(stateName(st.state)));
    }
    return 0;
}

QStringList ComponentsCli::splitIds(const QStringList& values) {
    QStringList out;
    for (const QString& v : values)
        for (const QString& part : v.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString id = part.trimmed();
            if (!id.isEmpty() && !out.contains(id)) out << id;
        }
    return out;
}

bool ComponentsCli::resolveIds(const QList<Component>& all, const ComponentEnv& env, const QStringList& ids, QStringList* out, QString* err) {
    QStringList res;
    for (const QString& id : ids) {
        if (id == QLatin1String("all")) {
            for (const Component& c : all) {
                const ComponentState s = Components::status(c, env).state;
                if ((s == ComponentState::Missing || s == ComponentState::Outdated) && !res.contains(c.id)) res << c.id;
            }
            continue;
        }
        const Component* found = nullptr;
        for (const Component& c : all)
            if (c.id == id) found = &c;
        if (!found) { *err = QStringLiteral("unknown component: %1").arg(id); return false; }
        const ComponentStatus st = Components::status(*found, env);
        if (st.state == ComponentState::Unavailable) {
            *err = QStringLiteral("%1 is not available on this machine%2").arg(id, st.detail.isEmpty() ? QString() : QStringLiteral(": ") + st.detail);
            return false;
        }
        if (!res.contains(id)) res << id;
    }
    *out = res;
    return true;
}

int ComponentsCli::install(const QList<Component>& all, const ComponentEnv& env, const QStringList& ids, std::FILE* out, std::FILE* err) {
    QStringList todo;
    QString msg;
    if (!resolveIds(all, env, ids, &todo, &msg)) {
        std::fprintf(err, "Error: %s\n", qPrintable(msg));
        return 1;
    }
    if (todo.isEmpty()) {
        std::fprintf(out, "Nothing to install.\n");
        return 0;
    }
    ComponentInstaller inst(env);
    QEventLoop loop;
    bool allOk = true;
    QHash<QString, int> lastPct;   // id + fase -> laatst getoonde percentage
    QObject::connect(&inst, &ComponentInstaller::started, &inst, [&](const QString& id) { std::fprintf(out, "[%s] start\n", qPrintable(id)); });
    QObject::connect(&inst, &ComponentInstaller::progress, &inst, [&](const QString& id, const QString& phase, qint64 done, qint64 total) {
        if (total <= 0) return;
        const int pct = static_cast<int>(done * 100 / total);
        const QString key = id + QLatin1Char('|') + phase;
        const int prev = lastPct.value(key, -100);
        if (pct < 100 && pct - prev < 5) return;
        if (pct >= 100 && prev >= 100) return;
        lastPct.insert(key, pct);
        std::fprintf(out, "[%s] %s %d%%\n", qPrintable(id), qPrintable(phase), pct);
        std::fflush(out);
    });
    QObject::connect(&inst, &ComponentInstaller::finished, &inst, [&](const QString& id, bool ok, const QString& m) {
        if (ok) std::fprintf(out, "[%s] installed\n", qPrintable(id));
        else {
            std::fprintf(err, "Error: %s: %s\n", qPrintable(id), qPrintable(m));
            allOk = false;
        }
    });
    QObject::connect(&inst, &ComponentInstaller::allFinished, &loop, [&](bool ok) {
        if (!ok) allOk = false;
        loop.quit();
    });
    inst.install(all, todo);
    loop.exec();
    return allOk ? 0 : 1;
}

}  // namespace rg
