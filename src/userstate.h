#pragma once

#include "store.h"

struct SavedEntry {
    QString source;
    qint64 id = 0;
    QString headword;
    QString dictionary;
    qint64 timestamp = 0;
};

class UserState {
public:
    static QString databasePath();
    static void recordVisit(const Article &article);
    static QVector<SavedEntry> history(const QString &filter = {});
    static void clearHistory();
    static bool isFavorite(const QString &source, qint64 id);
    static void setFavorite(const Article &article, bool favorite);
    static QVector<SavedEntry> favorites(const QString &filter = {});
    static QString lastError();
};
