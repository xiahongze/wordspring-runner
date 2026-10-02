#include "settingswidget.h"
#include "store.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Wordspring Runner"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("settings"), QStringLiteral("Open dictionary settings")});
    parser.addOption({QStringLiteral("source"), QStringLiteral("Dictionary source"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("entry"), QStringLiteral("Entry ID"), QStringLiteral("id")});
    parser.process(app);

    QWidget window;
    window.setWindowTitle(QStringLiteral("Wordspring Runner"));
    window.resize(650, 520);
    auto *layout = new QVBoxLayout(&window);
    if (parser.isSet(QStringLiteral("settings")) || !parser.isSet(QStringLiteral("entry"))) {
        auto *settings = new SettingsWidget(&window);
        layout->addWidget(settings);
        auto *save = new QPushButton(QStringLiteral("Save settings"), &window);
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
            window.setWindowTitle(article.headword + QStringLiteral(" — Wordspring Runner"));
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
    return app.exec();
}
