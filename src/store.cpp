#include "store.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <sqlite3.h>

namespace {
KConfigGroup settings() { return KSharedConfig::openConfig(QStringLiteral("wordspringrc"))->group(QStringLiteral("General")); }

QString normalized(const QString &word) { return word.trimmed().toCaseFolded(); }

QString column(sqlite3_stmt *stmt, int n)
{
    const auto *value = sqlite3_column_text(stmt, n);
    return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString();
}

Article readRow(sqlite3_stmt *stmt, const DictionaryInfo &dictionary)
{
    Article result;
    result.source = dictionary.source;
    result.id = sqlite3_column_int64(stmt, 0);
    result.headword = column(stmt, 1);
    result.preview = column(stmt, 2);
    result.html = column(stmt, 3);
    result.meanings = column(stmt, 4);
    result.dictionary = dictionary.name;
    return result;
}
}

QString Store::trigger() { return settings().readEntry("Trigger", QStringLiteral("define")); }

void Store::reload() { KSharedConfig::openConfig(QStringLiteral("wordspringrc"))->reparseConfiguration(); }

void Store::setTrigger(const QString &trigger)
{
    auto group = settings();
    group.writeEntry("Trigger", trigger.trimmed());
    group.sync();
}

QString Store::pythonExecutable() { return settings().readEntry("PythonExecutable", QStringLiteral("python3")); }

void Store::setPythonExecutable(const QString &path)
{
    auto group = settings();
    group.writeEntry("PythonExecutable", path.trimmed());
    group.sync();
}

QVector<DictionaryInfo> Store::dictionaries()
{
    QVector<DictionaryInfo> result;
    const auto document = QJsonDocument::fromJson(settings().readEntry("Dictionaries", QByteArray("[]")));
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        DictionaryInfo item{object.value("source").toString(), object.value("name").toString(), object.value("enabled").toBool(true)};
        if (!item.source.isEmpty()) result.push_back(item);
    }
    return result;
}

void Store::setDictionaries(const QVector<DictionaryInfo> &items)
{
    QJsonArray array;
    for (const auto &item : items) {
        array.append(QJsonObject{{"source", item.source}, {"name", item.name}, {"enabled", item.enabled}});
    }
    auto group = settings();
    group.writeEntry("Dictionaries", QJsonDocument(array).toJson(QJsonDocument::Compact));
    group.sync();
}

QString Store::databasePath(const QString &source)
{
    const auto hash = QCryptographicHash::hash(QDir::cleanPath(source).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wordspring/dictionaries/") + QString::fromLatin1(hash) + QStringLiteral(".sqlite");
}

QVector<Article> Store::lookup(const QString &word, int limitPerDictionary)
{
    QVector<Article> results;
    const QByteArray key = normalized(word).toUtf8();
    if (key.isEmpty()) return results;
    for (const auto &dictionary : dictionaries()) {
        if (!dictionary.enabled) continue;
        sqlite3 *db = nullptr;
        const auto path = databasePath(dictionary.source).toUtf8();
        if (sqlite3_open_v2(path.constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            continue;
        }
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT e.id,e.headword,e.preview,e.html,e.meanings FROM keys k JOIN entries e ON e.id=k.entry_id WHERE k.norm=?1 ORDER BY e.id LIMIT ?2";
        const char *legacySql = "SELECT e.id,e.headword,e.preview,e.html,'' FROM keys k JOIN entries e ON e.id=k.entry_id WHERE k.norm=?1 ORDER BY e.id LIMIT ?2";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) sqlite3_prepare_v2(db, legacySql, -1, &stmt, nullptr);
        if (stmt) {
            sqlite3_bind_text(stmt, 1, key.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limitPerDictionary);
            while (sqlite3_step(stmt) == SQLITE_ROW) results.push_back(readRow(stmt, dictionary));
        }
        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
    }
    return results;
}

Article Store::article(const QString &source, qint64 id)
{
    DictionaryInfo dictionary{source, source};
    for (const auto &item : dictionaries()) if (item.source == source) dictionary = item;
    Article result;
    sqlite3 *db = nullptr;
    const auto path = databasePath(source).toUtf8();
    if (sqlite3_open_v2(path.constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return result;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id,headword,preview,html,meanings FROM entries WHERE id=?1", -1, &stmt, nullptr) != SQLITE_OK)
        sqlite3_prepare_v2(db, "SELECT id,headword,preview,html,'' FROM entries WHERE id=?1", -1, &stmt, nullptr);
    if (stmt) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) result = readRow(stmt, dictionary);
    }
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}
