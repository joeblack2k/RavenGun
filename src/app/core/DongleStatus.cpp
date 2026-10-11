// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DongleStatus.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/AppPaths.h"
#include "core/SystemControl.h"

namespace rg {

QString DongleInfo::label() const {
    QStringList parts;
    if (!manufacturer.isEmpty()) parts << manufacturer;
    if (!product.isEmpty()) parts << product;
    if (!parts.isEmpty()) return parts.join(QLatin1Char(' '));
    return vendorName.isEmpty() ? QStringLiteral("USB dongle") : vendorName;
}

QString DongleInfo::line() const {
    return QStringLiteral("%1 (%2:%3) — runtime: %4").arg(label(), vendorId, productId, runtimeStatus.isEmpty() ? QStringLiteral("unknown") : runtimeStatus);
}

QString DongleStatus::text() const {
    if (!valid) return QStringLiteral("Dongle status unavailable");
    if (dongles.isEmpty()) return QStringLiteral("No dongle found");
    QString t = QStringLiteral("Dongle detected: ") + dongles.first().line();
    if (dongles.size() > 1) t += QStringLiteral(" (+%1 more)").arg(dongles.size() - 1);
    return t;
}

QString DongleStatus::tooltip() const {
    if (!valid) return error;
    return message;
}

const QList<DongleSource>& dongleSources() {
    static const QList<DongleSource> sources = {
        {QStringLiteral("usb-passthrough"), QStringLiteral("USB pass-through (physical dongle)")},
    };
    return sources;
}

QString dongleSourceLabel(const QString& value) {
    for (const DongleSource& s : dongleSources())
        if (s.value == value) return s.label;
    return value;
}

DongleStatus parseDongleJson(const QByteArray& output) {
    DongleStatus st;
    const int brace = output.indexOf('{');
    if (brace < 0) {
        st.error = QStringLiteral("scripts/dongle.py gave no result");
        return st;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(output.mid(brace), &pe);
    if (!doc.isObject()) {
        st.error = QStringLiteral("scripts/dongle.py gave an unreadable result: ") + pe.errorString();
        return st;
    }
    const QJsonObject o = doc.object();
    st.valid = true;
    st.ok = o.value(QStringLiteral("ok")).toBool();
    st.message = o.value(QStringLiteral("message")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("dongles")).toArray()) {
        const QJsonObject d = v.toObject();
        DongleInfo i;
        i.vendorId = d.value(QStringLiteral("vendor_id")).toString();
        i.productId = d.value(QStringLiteral("product_id")).toString();
        i.manufacturer = d.value(QStringLiteral("manufacturer")).toString();
        i.product = d.value(QStringLiteral("product")).toString();
        i.vendorName = d.value(QStringLiteral("vendor_name")).toString();
        const QJsonValue rs = d.value(QStringLiteral("runtime_status"));
        if (rs.isObject()) {
            i.runtimeName = rs.toObject().value(QStringLiteral("name")).toString();
            i.runtimeStatus = rs.toObject().value(QStringLiteral("status")).toString();
            i.installUrl = rs.toObject().value(QStringLiteral("install_url")).toString();
        } else {
            i.runtimeStatus = QStringLiteral("unknown");
        }
        st.dongles.append(i);
    }
    return st;
}

void detectDongle(QObject* context, std::function<void(const DongleStatus&)> done) {
    const QString script = AppPaths::dataRoot() + QStringLiteral("/scripts/dongle.py");
    if (AppPaths::dataRoot().isEmpty() || !QFileInfo::exists(script)) {
        DongleStatus st;
        st.error = QStringLiteral("scripts/dongle.py was not found");
        done(st);
        return;
    }
    SystemControl::run(QStringLiteral("python3"), {QStringLiteral("-I"), script, QStringLiteral("--json")}, context,
                       [done](int code, const QString& out) {
                           // Exit 1 (geen dongle of runtime niet actief) hoort er ook bij; de JSON staat dan evengoed op stdout.
                           DongleStatus st = parseDongleJson(out.toUtf8());
                           if (!st.valid && code < 0) st.error = QStringLiteral("python3 could not be run");
                           done(st);
                       });
}

}  // namespace rg
