#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

class ConfigLoader {
public:
    static QVariantMap defaults();
    bool load(const QString& path);
    const QVariantMap& settings() const { return m_settings; }

    // Copies the bundled commented template to `targetPath`. Refuses to
    // overwrite. The template lives in the binary as a QRC resource.
    static bool installSample(const QString& targetPath);

    // Removes // line comments and /* block */ comments from JSON-ish
    // input while leaving strings intact. Exposed for tests; load()
    // calls this before handing the buffer to QJsonDocument.
    static QByteArray stripJsonComments(const QByteArray& src);

private:
    QVariantMap m_settings;
};
