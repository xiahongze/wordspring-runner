#pragma once

#include <QString>
#include <QVector>

struct DictionaryInfo {
    QString source;
    QString name;
    bool enabled = true;
    QString label;
};

struct Article {
    QString source;
    qint64 id = 0;
    QString headword;
    QString preview;
    QString meanings;
    QString html;
    QString dictionary;
    QString dictionaryLabel;
};

struct StoreIssue {
    QString code;
    QString message;
    QString source;
};

class Store {
public:
    static QString trigger();
    static void reload();
    static void setTrigger(const QString &trigger);
    static QString pythonExecutable();
    static void setPythonExecutable(const QString &path);
    static QVector<DictionaryInfo> dictionaries();
    static void setDictionaries(const QVector<DictionaryInfo> &items);
    static QString databasePath(const QString &source);
    static QString suggestLabel(const QString &name);
    static QVector<Article> lookup(const QString &word, int limitPerDictionary = 2, QVector<StoreIssue> *issues = nullptr);
    static Article article(const QString &source, qint64 id, QVector<StoreIssue> *issues = nullptr);
    // Process-local override for CLI callers; never writes shared settings.
    static void useDictionaries(const QVector<DictionaryInfo> &items);
};
