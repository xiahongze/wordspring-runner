#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtTest>
#include <sqlite3.h>

class StoreTest : public QObject {
    Q_OBJECT
private slots:
    void lookupOnlyEnabled();
    void shortLabels();
};

void StoreTest::shortLabels()
{
    QCOMPARE(Store::suggestLabel(QStringLiteral("Oxford Dictionary of English")), QStringLiteral("ODE"));
    QCOMPARE(Store::suggestLabel(QStringLiteral("Oxford Thesaurus of English")), QStringLiteral("OTE"));
    QCOMPARE(Store::suggestLabel(QStringLiteral("現代漢語同義詞典")), QStringLiteral("現代漢語同義詞典"));
}

void StoreTest::lookupOnlyEnabled()
{
    qputenv("XDG_DATA_HOME", "/tmp/wordspring-test-data");
    qputenv("XDG_CONFIG_HOME", "/tmp/wordspring-test-config");
    const QString first = QStringLiteral("/tmp/wordspring-test-alpha.ifo");
    const QString second = QStringLiteral("/tmp/wordspring-test-beta.ifo");
    Store::setDictionaries({{first, QStringLiteral("Alpha"), true}, {second, QStringLiteral("Beta"), false}});
    for (const auto &source : {first, second}) {
        const QString path = Store::databasePath(source);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        sqlite3 *db = nullptr;
        QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
        const char *sql = "CREATE TABLE entries(id INTEGER PRIMARY KEY,headword TEXT,preview TEXT,html TEXT);"
                          "CREATE TABLE keys(norm TEXT,entry_id INTEGER);"
                          "INSERT INTO entries VALUES(1,'Straße','A road','<p>A road</p>');"
                          "INSERT INTO keys VALUES('strasse',1);";
        char *error = nullptr;
        QCOMPARE(sqlite3_exec(db, sql, nullptr, nullptr, &error), SQLITE_OK);
        sqlite3_close(db);
    }
    const auto results = Store::lookup(QStringLiteral("STRASSE"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().dictionary, QStringLiteral("Alpha"));
    QCOMPARE(Store::article(first, 1).headword, QStringLiteral("Straße"));
    QFile::remove(Store::databasePath(first));
    QFile::remove(Store::databasePath(second));
}

QTEST_MAIN(StoreTest)
#include "store_test.moc"
