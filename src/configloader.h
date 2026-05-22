#pragma once

#include <QString>
#include <QVariantMap>

class ConfigLoader {
public:
    static QVariantMap defaults();
    bool load(const QString& path);
    const QVariantMap& settings() const { return m_settings; }
    static bool installSample(const QString& targetPath);

private:
    QVariantMap m_settings;
};
