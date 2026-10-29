#pragma once

#include <QMap>
#include <QString>

// The small [General] subset of KConfig used by Wordspring. Keep the on-disk
// representation compatible so the KDE and CLI frontends share one config.
class Configuration {
public:
    static QString path();
    static void setPath(const QString &path);
    static QString read(const QString &key, const QString &fallback);
    static void write(const QMap<QString, QString> &values);
};
