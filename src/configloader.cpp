#include "configloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

QVariantMap ConfigLoader::defaults() {
    return {
        {"scale",          1.0},
        {"autoScale",      false},
        {"useBackground",  true},
        {"accentColor",    "#f08a28"},
        {"hotColor",       "#ff5a3c"},
        {"bgColor",        "#0d0d0d"},

        {"updateMs",       1000},
        {"disksToShow",    "/,/home"},
        {"netInterface",   ""},
        {"netMode",        "sum"},

        {"cpuMaxTemp",     90},
        {"gpuMaxTemp",     85},
        {"ssdMaxTemp",     65},

        {"showScope",      true},
        {"starCount",      30},
        {"gpuScriptPath",  "~/.local/bin/gpuinfo.sh"},

        {"screen",         ""},
        {"layer",          "bottom"},
        {"clickThrough",   true},
        {"keyboardFocus",  "none"},
    };
}

bool ConfigLoader::load(const QString& path) {
    m_settings = defaults();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo("departure-hud: no config at %s — using defaults", qPrintable(path));
        return false;
    }

    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("departure-hud: could not parse %s — %s",
                 qPrintable(path), qPrintable(err.errorString()));
        return false;
    }
    if (!doc.isObject()) {
        return false;
    }

    const auto obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        m_settings[it.key()] = it.value().toVariant();
    }
    return true;
}

bool ConfigLoader::installSample(const QString& targetPath) {
    QFileInfo fi(targetPath);
    QDir().mkpath(fi.absoluteDir().absolutePath());

    if (fi.exists()) {
        qWarning("departure-hud: %s already exists, refusing to overwrite",
                 qPrintable(targetPath));
        return false;
    }

    QJsonObject obj;
    const auto def = defaults();
    for (auto it = def.constBegin(); it != def.constEnd(); ++it) {
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    }

    QFile f(targetPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("departure-hud: cannot write %s", qPrintable(targetPath));
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    qInfo("departure-hud: installed sample config to %s", qPrintable(targetPath));
    return true;
}
