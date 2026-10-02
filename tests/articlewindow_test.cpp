#include "articlewindow.h"
#include "store.h"
#include "userstate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QtTest>
#include <sqlite3.h>

class ArticleWindowTest : public QObject {
    Q_OBJECT
private slots:
    void bookmarkAndLinkedEntry();
};

void ArticleWindowTest::bookmarkAndLinkedEntry()
{
    qputenv("XDG_DATA_HOME", "/tmp/wordspring-article-test-data");
    qputenv("XDG_CONFIG_HOME", "/tmp/wordspring-article-test-config");
    const QString source = QStringLiteral("/tmp/wordspring-article-test.ifo");
    Store::setDictionaries({{source, QStringLiteral("Test Dictionary"), true, QStringLiteral("TD")}});
    const auto path = Store::databasePath(source);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile::remove(path);
    QFile::remove(UserState::databasePath());
    sqlite3 *db = nullptr;
    QCOMPARE(sqlite3_open(path.toUtf8().constData(), &db), SQLITE_OK);
    const char *sql = "CREATE TABLE entries(id INTEGER PRIMARY KEY,headword TEXT,preview TEXT,html TEXT,meanings TEXT);"
                      "CREATE TABLE keys(norm TEXT,entry_id INTEGER);"
                      "INSERT INTO entries VALUES(1,'happy','pleased','<p>See <a href=\"bword://%E7%8C%AB\">猫</a></p>','pleased');"
                      "INSERT INTO entries VALUES(2,'猫','cat','<p>猫 means cat</p>','cat');"
                      "INSERT INTO keys VALUES('happy',1);INSERT INTO keys VALUES('猫',2);";
    QCOMPARE(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(db);
    UserState::clearHistory();
    ArticleWindow window;
    window.showArticle(Store::article(source, 1));
    auto *bookmark = window.findChild<QToolButton *>(QStringLiteral("bookmarkButton"));
    auto *preferences = window.findChild<QToolButton *>(QStringLiteral("preferencesButton"));
    QVERIFY(bookmark);
    QVERIFY(preferences);
    QVERIFY(bookmark->text().isEmpty());
    QVERIFY(preferences->text().isEmpty());
    QVERIFY(!bookmark->icon().isNull());
    QVERIFY(!preferences->icon().isNull());
    bookmark->click();
    const auto saved = UserState::favorites();
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.first().source, source);
    QCOMPARE(saved.first().headword, QStringLiteral("happy"));
    auto *body = window.findChild<QTextBrowser *>(QStringLiteral("articleBody"));
    QVERIFY(body);
    const QUrl link(QStringLiteral("bword://%E7%8C%AB"));
    QVERIFY(QMetaObject::invokeMethod(body, "anchorClicked", Qt::DirectConnection, Q_ARG(QUrl, link)));
    QCOMPARE(window.currentArticle().headword, QStringLiteral("猫"));
    QVERIFY(body->toPlainText().contains(QStringLiteral("猫 means cat")));
    QCOMPARE(UserState::history().size(), 2);
    UserState::setFavorite(Store::article(source, 1), false);
    UserState::clearHistory();
    QFile::remove(path);
    QFile::remove(UserState::databasePath());
}

QTEST_MAIN(ArticleWindowTest)
#include "articlewindow_test.moc"
