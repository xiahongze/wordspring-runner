#include "userstate.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <sqlite3.h>

namespace {
thread_local QString stateError;

int remember(sqlite3 *db, int status)
{
    if (status != SQLITE_OK && status != SQLITE_ROW && status != SQLITE_DONE)
        stateError = QString::fromUtf8(sqlite3_errmsg(db));
    return status;
}

int prepare(sqlite3 *db, const char *sql, int size, sqlite3_stmt **stmt, const char **tail)
{
    return remember(db, sqlite3_prepare_v2(db, sql, size, stmt, tail));
}
int step(sqlite3_stmt *stmt) { return remember(sqlite3_db_handle(stmt), sqlite3_step(stmt)); }
int execute(sqlite3 *db, const char *sql, int (*cb)(void*,int,char**,char**), void *data, char **error)
{
    return remember(db, sqlite3_exec(db, sql, cb, data, error));
}

struct Connection {
    sqlite3 *db = nullptr;
    Connection()
    {
        stateError.clear();
        const auto path = UserState::databasePath();
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            stateError = QStringLiteral("Cannot create saved-word directory");
            return;
        }
        if (sqlite3_open(path.toUtf8().constData(), &db) != SQLITE_OK) {
            if (db) { stateError = QString::fromUtf8(sqlite3_errmsg(db)); sqlite3_close(db); }
            db = nullptr;
            return;
        }
        sqlite3_busy_timeout(db, 1000);
        execute(db, "CREATE TABLE IF NOT EXISTS history(source TEXT NOT NULL, entry_id INTEGER NOT NULL, headword TEXT NOT NULL, dictionary TEXT NOT NULL, viewed_at INTEGER NOT NULL, PRIMARY KEY(source,entry_id));"
                         "CREATE TABLE IF NOT EXISTS favorites(source TEXT NOT NULL, entry_id INTEGER NOT NULL, headword TEXT NOT NULL, dictionary TEXT NOT NULL, added_at INTEGER NOT NULL, PRIMARY KEY(source,entry_id));", nullptr, nullptr, nullptr);
    }
    ~Connection() { if (db) sqlite3_close(db); }
};

QString textColumn(sqlite3_stmt *stmt, int column)
{
    const auto *value = sqlite3_column_text(stmt, column);
    return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString();
}

QVector<SavedEntry> list(const char *table, const QString &filter)
{
    QVector<SavedEntry> result;
    Connection connection;
    if (!connection.db) return result;
    if (qstrcmp(table, "favorites") == 0) {
        // Older article windows could save an empty source and damaged headword.
        // Recover only entries with a single unambiguous history match.
        execute(connection.db,
            "BEGIN;"
            "INSERT OR IGNORE INTO favorites(source,entry_id,headword,dictionary,added_at) "
            "SELECT h.source,h.entry_id,h.headword,h.dictionary,f.added_at FROM favorites f "
            "JOIN history h ON h.entry_id=f.entry_id AND h.dictionary=f.dictionary "
            "WHERE f.source='' AND h.source<>'' AND "
            "(SELECT COUNT(*) FROM history h2 WHERE h2.entry_id=f.entry_id AND h2.dictionary=f.dictionary)=1;"
            "DELETE FROM favorites WHERE source='' AND EXISTS "
            "(SELECT 1 FROM history h WHERE h.entry_id=favorites.entry_id AND h.dictionary=favorites.dictionary "
            "AND h.source<>'' AND (SELECT COUNT(*) FROM history h2 WHERE h2.entry_id=favorites.entry_id "
            "AND h2.dictionary=favorites.dictionary)=1);"
            "COMMIT;", nullptr, nullptr, nullptr);
    }
    const QByteArray sql = QByteArray("SELECT source,entry_id,headword,dictionary,") + (qstrcmp(table, "history") == 0 ? "viewed_at" : "added_at")
        + " FROM " + table + " WHERE headword LIKE ?1 OR dictionary LIKE ?1 ORDER BY "
        + (qstrcmp(table, "history") == 0 ? "viewed_at" : "added_at") + " DESC LIMIT 500";
    sqlite3_stmt *stmt = nullptr;
    if (prepare(connection.db, sql.constData(), -1, &stmt, nullptr) != SQLITE_OK) return result;
    const auto search = (QStringLiteral("%") + filter + QStringLiteral("%")).toUtf8();
    sqlite3_bind_text(stmt, 1, search.constData(), -1, SQLITE_TRANSIENT);
    while (step(stmt) == SQLITE_ROW) {
        result.push_back({textColumn(stmt, 0), sqlite3_column_int64(stmt, 1), textColumn(stmt, 2), textColumn(stmt, 3), sqlite3_column_int64(stmt, 4)});
    }
    sqlite3_finalize(stmt);
    return result;
}

void bindArticle(sqlite3_stmt *stmt, const Article &article)
{
    const auto source = article.source.toUtf8();
    const auto word = article.headword.toUtf8();
    const auto dictionary = article.dictionary.toUtf8();
    sqlite3_bind_text(stmt, 1, source.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, article.id);
    sqlite3_bind_text(stmt, 3, word.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, dictionary.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, QDateTime::currentMSecsSinceEpoch());
}
}

QString UserState::databasePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wordspring/state.sqlite");
}

void UserState::recordVisit(const Article &article)
{
    if (article.headword.isEmpty()) return;
    Connection connection;
    if (!connection.db) return;
    sqlite3_stmt *stmt = nullptr;
    if (prepare(connection.db, "INSERT INTO history(source,entry_id,headword,dictionary,viewed_at) VALUES(?1,?2,?3,?4,?5) "
        "ON CONFLICT(source,entry_id) DO UPDATE SET viewed_at=excluded.viewed_at,headword=excluded.headword,dictionary=excluded.dictionary", -1, &stmt, nullptr) == SQLITE_OK) {
        bindArticle(stmt, article);
        step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
    execute(connection.db, "DELETE FROM history WHERE rowid NOT IN (SELECT rowid FROM history ORDER BY viewed_at DESC LIMIT 500)", nullptr, nullptr, nullptr);
}

QVector<SavedEntry> UserState::history(const QString &filter) { return list("history", filter); }

void UserState::clearHistory()
{
    Connection connection;
    if (connection.db) execute(connection.db, "DELETE FROM history", nullptr, nullptr, nullptr);
}

bool UserState::isFavorite(const QString &source, qint64 id)
{
    Connection connection;
    if (!connection.db) return false;
    sqlite3_stmt *stmt = nullptr;
    bool found = false;
    if (prepare(connection.db, "SELECT 1 FROM favorites WHERE source=?1 AND entry_id=?2", -1, &stmt, nullptr) == SQLITE_OK) {
        const auto bytes = source.toUtf8();
        sqlite3_bind_text(stmt, 1, bytes.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, id);
        found = step(stmt) == SQLITE_ROW;
    }
    if (stmt) sqlite3_finalize(stmt);
    return found;
}

void UserState::setFavorite(const Article &article, bool favorite)
{
    if (article.headword.isEmpty()) return;
    Connection connection;
    if (!connection.db) return;
    sqlite3_stmt *stmt = nullptr;
    if (favorite) {
        if (prepare(connection.db, "INSERT INTO favorites(source,entry_id,headword,dictionary,added_at) VALUES(?1,?2,?3,?4,?5) "
            "ON CONFLICT(source,entry_id) DO UPDATE SET headword=excluded.headword,dictionary=excluded.dictionary", -1, &stmt, nullptr) == SQLITE_OK) {
            bindArticle(stmt, article);
            step(stmt);
        }
    } else if (prepare(connection.db, "DELETE FROM favorites WHERE source=?1 AND entry_id=?2", -1, &stmt, nullptr) == SQLITE_OK) {
        const auto bytes = article.source.toUtf8();
        sqlite3_bind_text(stmt, 1, bytes.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, article.id);
        step(stmt);
    }
    if (stmt) sqlite3_finalize(stmt);
}

QVector<SavedEntry> UserState::favorites(const QString &filter) { return list("favorites", filter); }

QString UserState::lastError() { return stateError; }
