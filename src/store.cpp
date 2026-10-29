#include "store.h"

#include "core/configuration.h"
#include "core/error.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <sqlite3.h>

namespace {
bool hasOverride = false;
QVector<DictionaryInfo> dictionaryOverride;

void issue(QVector<StoreIssue> *issues, const QString &source, sqlite3 *db)
{
    if (issues) issues->push_back({QStringLiteral("database_unavailable"), QString::fromUtf8(sqlite3_errmsg(db)), source});
}

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
    result.dictionaryLabel = dictionary.label.isEmpty() ? Store::suggestLabel(dictionary.name) : dictionary.label;
    return result;
}
}

QString Store::trigger() { return Configuration::read("Trigger", QStringLiteral("define")); }

void Store::reload() {} // Settings are read afresh on every operation.

void Store::setTrigger(const QString &trigger)
{
    Configuration::write({{"Trigger", trigger.trimmed()}});
}

QString Store::pythonExecutable() { return Configuration::read("PythonExecutable", QStringLiteral("python3")); }

QString Store::suggestLabel(const QString &name)
{
    const auto words = name.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString initials;
    for (const auto &word : words) {
        if (word.compare(QStringLiteral("of"), Qt::CaseInsensitive) == 0
            || word.compare(QStringLiteral("the"), Qt::CaseInsensitive) == 0
            || word.compare(QStringLiteral("and"), Qt::CaseInsensitive) == 0) continue;
        initials += word.at(0).toUpper();
        if (initials.size() == 8) break;
    }
    return initials.size() > 1 ? initials : name.left(12);
}

void Store::setPythonExecutable(const QString &path)
{
    Configuration::write({{"PythonExecutable", path.trimmed()}});
}

QVector<DictionaryInfo> Store::dictionaries()
{
    QVector<DictionaryInfo> result;
    if (hasOverride) return dictionaryOverride;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(Configuration::read("Dictionaries", QStringLiteral("[]")).toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw WordspringError("invalid_config", "Dictionaries must be a JSON array");
    for (const auto &value : document.array()) {
        const auto object = value.toObject();
        DictionaryInfo item{object.value("source").toString(), object.value("name").toString(), object.value("enabled").toBool(true), object.value("label").toString()};
        if (item.label.isEmpty()) item.label = suggestLabel(item.name);
        if (!item.source.isEmpty()) result.push_back(item);
    }
    return result;
}

void Store::setDictionaries(const QVector<DictionaryInfo> &items)
{
    QJsonArray array;
    for (const auto &item : items) {
        array.append(QJsonObject{{"source", item.source}, {"name", item.name}, {"enabled", item.enabled}, {"label", item.label}});
    }
    Configuration::write({{"Dictionaries", QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact))}});
}

QString Store::databasePath(const QString &source)
{
    const auto hash = QCryptographicHash::hash(QDir::cleanPath(source).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wordspring/dictionaries/") + QString::fromLatin1(hash) + QStringLiteral(".sqlite");
}

void Store::useDictionaries(const QVector<DictionaryInfo> &items)
{
    dictionaryOverride = items;
    hasOverride = true;
}

QVector<Article> Store::lookup(const QString &word, int limitPerDictionary, QVector<StoreIssue> *issues)
{
    QVector<Article> results;
    const QByteArray key = normalized(word).toUtf8();
    if (key.isEmpty()) return results;
    for (const auto &dictionary : dictionaries()) {
        if (!dictionary.enabled) continue;
        sqlite3 *db = nullptr;
        const auto path = databasePath(dictionary.source).toUtf8();
        if (sqlite3_open_v2(path.constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (db) { issue(issues, dictionary.source, db); sqlite3_close(db); }
            continue;
        }
        sqlite3_stmt *stmt = nullptr;
        const char *sql = "SELECT e.id,e.headword,e.preview,'',e.meanings FROM keys k JOIN entries e ON e.id=k.entry_id WHERE k.norm=?1 ORDER BY e.id LIMIT ?2";
        const char *legacySql = "SELECT e.id,e.headword,e.preview,'','' FROM keys k JOIN entries e ON e.id=k.entry_id WHERE k.norm=?1 ORDER BY e.id LIMIT ?2";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) sqlite3_prepare_v2(db, legacySql, -1, &stmt, nullptr);
        if (stmt) {
            sqlite3_bind_text(stmt, 1, key.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, limitPerDictionary);
            int status;
            while ((status = sqlite3_step(stmt)) == SQLITE_ROW) results.push_back(readRow(stmt, dictionary));
            if (status != SQLITE_DONE) issue(issues, dictionary.source, db);
        }
        if (!stmt) issue(issues, dictionary.source, db);
        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
    }
    return results;
}

Article Store::article(const QString &source, qint64 id, QVector<StoreIssue> *issues)
{
    DictionaryInfo dictionary{source, source, true, suggestLabel(source)};
    for (const auto &item : dictionaries()) if (item.source == source) dictionary = item;
    Article result;
    sqlite3 *db = nullptr;
    const auto path = databasePath(source).toUtf8();
    if (sqlite3_open_v2(path.constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) { issue(issues, source, db); sqlite3_close(db); }
        return result;
    }
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id,headword,preview,html,meanings FROM entries WHERE id=?1", -1, &stmt, nullptr) != SQLITE_OK)
        sqlite3_prepare_v2(db, "SELECT id,headword,preview,html,'' FROM entries WHERE id=?1", -1, &stmt, nullptr);
    if (stmt) {
        sqlite3_bind_int64(stmt, 1, id);
        const int status = sqlite3_step(stmt);
        if (status == SQLITE_ROW) result = readRow(stmt, dictionary);
        else if (status != SQLITE_DONE) issue(issues, source, db);
    }
    if (!stmt) issue(issues, source, db);
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}
