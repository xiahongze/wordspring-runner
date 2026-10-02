#pragma once

#include <QString>
#include <QVector>

struct DictionaryInfo {
    QString source;
    QString name;
    bool enabled = true;
};

struct Article {
    QString source;
    qint64 id = 0;
    QString headword;
    QString preview;
    QString html;
    QString dictionary;
};

class Store {
public:
    static QString trigger();
    static void setTrigger(const QString &trigger);
    static QVector<DictionaryInfo> dictionaries();
    static void setDictionaries(const QVector<DictionaryInfo> &items);
    static QString databasePath(const QString &source);
    static QVector<Article> lookup(const QString &word, int limitPerDictionary = 2);
    static Article article(const QString &source, qint64 id);
};
