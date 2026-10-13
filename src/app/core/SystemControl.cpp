// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SystemControl.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include "core/AppPaths.h"
#include "core/FileSafety.h"
#include "core/TextConfig.h"

namespace rg {

void SystemControl::run(const QString& program, const QStringList& args, QObject* context, Done done, int timeoutMs) {
    // Het proces is geen kind van `context`: anders roept ~QProcess (tijdens het afbreken van een venster) de callback nog aan
    // terwijl de widgets van dat venster al weg zijn. Verdwijnt de context, dan stopt het proces zonder callback.
    auto* p = new QProcess;
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto alive = std::make_shared<bool>(true);
    auto finished = std::make_shared<bool>(false);
    if (context) {
        QObject::connect(context, &QObject::destroyed, p, [p, alive, finished] {
            *alive = false;
            *finished = true;
            QObject::disconnect(p, nullptr, nullptr, nullptr);
            if (p->state() != QProcess::NotRunning) {
                p->kill();
                p->waitForFinished(1000);
            }
            p->deleteLater();
        });
    }
    auto finish = [p, done, alive, finished](int code) {
        if (*finished) return;
        *finished = true;
        const QString out = QString::fromUtf8(p->readAll());
        if (*alive && done) done(code, out);
        p->deleteLater();
    };
    QObject::connect(p, &QProcess::finished, p, [finish](int code, QProcess::ExitStatus st) { finish(st == QProcess::NormalExit ? code : -1); });
    QObject::connect(p, &QProcess::errorOccurred, p, [finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) finish(-2);
    });
    QTimer::singleShot(timeoutMs, p, [p, finish] {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            finish(-1);
        }
    });
    p->start(program, args);
}

void SystemControl::systemctl(const QStringList& args, QObject* context, Done done) {
    QString prog = qEnvironmentVariable("RAVENGUN_SYSTEMCTL");
    if (prog.isEmpty()) prog = QStringLiteral("systemctl");
    run(prog, QStringList{QStringLiteral("--user")} + args, context, std::move(done));
}

void SystemControl::unitState(const QString& unit, QObject* context, std::function<void(const QString&, const QString&)> cb) {
    systemctl({QStringLiteral("is-active"), unit}, context, [unit, context, cb](int, const QString& a) {
        const QString active = a.trimmed().section(QLatin1Char('\n'), 0, 0);
        systemctl({QStringLiteral("is-enabled"), unit}, context, [active, cb](int, const QString& e) {
            cb(active.isEmpty() ? QStringLiteral("unknown") : active, e.trimmed().section(QLatin1Char('\n'), 0, 0));
        });
    });
}

QJsonObject SystemControl::engineConfig() { return readJson(AppPaths::engineConfigFile()).object(); }

bool SystemControl::setEngineWeb(bool lan, bool requireToken, QString* err) {
    const QString path = AppPaths::engineConfigFile();
    QJsonObject o;
    if (QFileInfo::exists(path)) {
        QString e;
        const QJsonDocument d = readJson(path, &e);
        if (!d.isObject()) {
            if (err) *err = QStringLiteral("cannot read %1: %2").arg(path, e);
            return false;
        }
        o = d.object();
    } else {
        o.insert(QStringLiteral("schema"), 2);
    }
    QJsonObject web = o.value(QStringLiteral("web")).toObject();
    web.insert(QStringLiteral("lan"), lan);
    web.insert(QStringLiteral("require_token"), requireToken);
    o.insert(QStringLiteral("web"), web);
    // Zelfde bestandsrechten als de engine (0600).
    if (!writeJsonAtomic(path, o, err)) return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

int SystemControl::engineWebPort() {
    return engineConfig().value(QStringLiteral("web")).toObject().value(QStringLiteral("port")).toInt(8765);
}

QString SystemControl::webToken() {
    QFile f(AppPaths::configDir() + QStringLiteral("/web-token"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.read(256)).trimmed();
}

QStringList SystemControl::lanAddresses() {
    QStringList out;
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol && a.isPrivateUse()) out << a.toString();
        }
    }
    return out;
}

QString SystemControl::oledDropInPath() {
    return AppPaths::configDir().section(QLatin1Char('/'), 0, -2) + QStringLiteral("/systemd/user/") + QLatin1String(kOledUnit) +
           QStringLiteral(".d/ravengun-app.conf");
}

namespace {
QString cameraChoiceOrAuto(const QString& v) {
    static const QRegularExpression dev(QStringLiteral("^/dev/video[0-9]+$"));
    const QString s = v.trimmed();
    return (s == QStringLiteral("none") || dev.match(s).hasMatch()) ? s : QStringLiteral("auto");
}
}  // namespace

QString SystemControl::gameCamera() {
    QSettings s(AppPaths::appSettingsFile(), QSettings::IniFormat);
    return cameraChoiceOrAuto(s.value(QStringLiteral("Games/camera")).toString());
}

void SystemControl::setGameCamera(const QString& choice) {
    QSettings s(AppPaths::appSettingsFile(), QSettings::IniFormat);
    s.setValue(QStringLiteral("Games/camera"), cameraChoiceOrAuto(choice));
    s.sync();
}

QList<QPair<QString, QString>> SystemControl::cameras(const QString& sysRoot) {
    // Een camera heeft vaak twee nodes (beeld en metagegevens): alleen de eerste van elke camera (index 0) telt. Op nummer.
    QList<QPair<int, QPair<QString, QString>>> found;
    static const QRegularExpression node(QStringLiteral("^video([0-9]+)$"));
    const auto readField = [&sysRoot](const QString& n, const char* field) {
        QFile f(sysRoot + QLatin1Char('/') + n + QLatin1Char('/') + QLatin1String(field));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    for (const QString& n : QDir(sysRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto m = node.match(n);
        if (!m.hasMatch()) continue;
        const QString index = readField(n, "index");
        if (!index.isEmpty() && index != QStringLiteral("0")) continue;
        const QString name = readField(n, "name");
        found.append({m.captured(1).toInt(), {QStringLiteral("/dev/") + n, name.isEmpty() ? n : name}});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QList<QPair<QString, QString>> out;
    for (const auto& c : found) out.append(c.second);
    return out;
}

int SystemControl::oledIdleSeconds() {
    QFile f(oledDropInPath());
    if (f.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral("RG_SAVER_IDLE_S=(\\d+)"));
        const auto m = re.match(QString::fromUtf8(f.readAll()));
        if (m.hasMatch()) return m.captured(1).toInt();
    }
    return 600;
}

bool SystemControl::setOledIdleSeconds(int seconds, QString* err) {
    const QString text = QStringLiteral("# Written by the RavenGun app (Settings > Display)\n[Service]\nEnvironment=RG_SAVER_IDLE_S=%1\n").arg(seconds);
    return writeTextAtomic(oledDropInPath(), text.toUtf8(), err);
}

QString SystemControl::tail(const QString& path, int maxLines) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const qint64 size = f.size();
    const qint64 from = size > 256 * 1024 ? size - 256 * 1024 : 0;
    f.seek(from);
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    if (from > 0 && !lines.isEmpty()) lines.removeFirst();
    if (lines.size() > maxLines) lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

}  // namespace rg
