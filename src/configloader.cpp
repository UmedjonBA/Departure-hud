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
        {"autoScaleFit",   0.7},
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
        {"screens",        QVariantList{}},
        {"layer",          "bottom"},
        {"clickThrough",   true},
        {"keyboardFocus",  "none"},
    };
}

// Walk the buffer once, copying through everything except // and /* */
// comments that appear outside of string literals. Newlines inside
// block comments are kept so QJsonDocument error line numbers still
// roughly line up with the source.
QByteArray ConfigLoader::stripJsonComments(const QByteArray& src) {
    QByteArray out;
    out.reserve(src.size());
    const int n = src.size();
    int i = 0;
    while (i < n) {
        const char c = src[i];

        // String literal — copy verbatim, honour \" escapes
        if (c == '"') {
            out += c;
            ++i;
            while (i < n) {
                const char ch = src[i];
                out += ch;
                if (ch == '\\' && i + 1 < n) {
                    out += src[i + 1];
                    i += 2;
                } else if (ch == '"') {
                    ++i;
                    break;
                } else {
                    ++i;
                }
            }
            continue;
        }

        // // line comment
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            i += 2;
            while (i < n && src[i] != '\n') ++i;
            continue;
        }

        // /* block comment */
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            i += 2;
            while (i < n) {
                if (src[i] == '*' && i + 1 < n && src[i + 1] == '/') {
                    i += 2;
                    break;
                }
                if (src[i] == '\n') out += '\n';
                ++i;
            }
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

bool ConfigLoader::load(const QString& path) {
    m_settings = defaults();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qInfo("departure-hud: no config at %s — using defaults", qPrintable(path));
        return false;
    }

    const QByteArray raw      = f.readAll();
    const QByteArray stripped = stripJsonComments(raw);

    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(stripped, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning("departure-hud: could not parse %s — %s (offset %d)",
                 qPrintable(path),
                 qPrintable(err.errorString()),
                 err.offset);
        return false;
    }
    if (!doc.isObject()) {
        qWarning("departure-hud: %s is valid JSON but not an object — using defaults",
                 qPrintable(path));
        return false;
    }

    const auto obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        m_settings[it.key()] = it.value().toVariant();
    }
    return true;
}

bool ConfigLoader::installSample(const QString& targetPath) {
    const QFileInfo fi(targetPath);
    QDir().mkpath(fi.absoluteDir().absolutePath());

    if (fi.exists()) {
        qWarning("departure-hud: %s already exists, refusing to overwrite",
                 qPrintable(targetPath));
        return false;
    }

    QFile src(QStringLiteral(":/qt/qml/DepartureHud/config.json"));
    if (!src.open(QIODevice::ReadOnly)) {
        qWarning("departure-hud: bundled config.json resource is missing");
        return false;
    }

    QFile dst(targetPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("departure-hud: cannot write %s", qPrintable(targetPath));
        return false;
    }
    dst.write(src.readAll());
    fprintf(stderr,
            "departure-hud: installed sample config to %s\n",
            qPrintable(targetPath));
    return true;
}
