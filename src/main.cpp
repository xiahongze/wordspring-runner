#include "settingswidget.h"
#include "store.h"
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(i18nd("wordspring-runner", "Wordspring Runner"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("settings"), i18nd("wordspring-runner", "Open dictionary settings")});
    parser.addOption({QStringLiteral("source"), i18nd("wordspring-runner", "Dictionary source"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("entry"), i18nd("wordspring-runner", "Entry ID"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("screenshot"), i18nd("wordspring-runner", "Save a screenshot and exit"), QStringLiteral("file")});
    parser.process(app);

    QWidget window;
    window.setWindowTitle(i18nd("wordspring-runner", "Wordspring Runner"));
    window.resize(650, 520);
    auto *layout = new QVBoxLayout(&window);
    if (parser.isSet(QStringLiteral("settings")) || !parser.isSet(QStringLiteral("entry"))) {
        auto *settings = new SettingsWidget(&window);
        layout->addWidget(settings);
        auto *save = new QPushButton(i18nd("wordspring-runner", "Save settings"), &window);
        layout->addWidget(save);
        QObject::connect(save, &QPushButton::clicked, settings, &SettingsWidget::save);
    } else {
        auto *headword = new QLabel(&window);
        auto font = headword->font();
        font.setPointSize(font.pointSize() + 9);
        font.setBold(true);
        headword->setFont(font);
        layout->addWidget(headword);
        auto *source = new QLabel(&window);
        layout->addWidget(source);
        auto *body = new QTextBrowser(&window);
        body->setOpenLinks(false);
        body->setOpenExternalLinks(false);
        layout->addWidget(body);
        auto show = [=, &window](const Article &article) {
            headword->setText(article.headword);
            source->setText(article.dictionary);
            body->setHtml(QStringLiteral("<html><head><style>body{font-size:12pt;line-height:1.5}p{margin:0 0 0.8em}blockquote{margin-left:1em;color:gray}</style></head><body>") + article.html + QStringLiteral("</body></html>"));
            window.setWindowTitle(i18nd("wordspring-runner", "%1 — Wordspring Runner", article.headword));
        };
        show(Store::article(parser.value(QStringLiteral("source")), parser.value(QStringLiteral("entry")).toLongLong()));
        QObject::connect(body, &QTextBrowser::anchorClicked, &window, [=](const QUrl &url) {
            if (url.scheme() != QStringLiteral("bword")) return;
            const QString word = QUrl::fromPercentEncoding(url.toEncoded().mid(sizeof("bword://") - 1));
            const auto matches = Store::lookup(word, 1);
            if (!matches.isEmpty()) show(matches.first());
        });
    }
    window.show();
    if (parser.isSet(QStringLiteral("screenshot"))) {
        QTimer::singleShot(250, &window, [&] {
            window.grab().save(parser.value(QStringLiteral("screenshot")));
            app.quit();
        });
    }
    return app.exec();
}
