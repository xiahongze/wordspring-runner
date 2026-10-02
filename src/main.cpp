#include "settingswidget.h"
#include "store.h"
#include "userstate.h"
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
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
    parser.addOption({QStringLiteral("activity"), i18nd("wordspring-runner", "Open favorites and history")});
    parser.addOption({QStringLiteral("source"), i18nd("wordspring-runner", "Dictionary source"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("entry"), i18nd("wordspring-runner", "Entry ID"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("screenshot"), i18nd("wordspring-runner", "Save a screenshot and exit"), QStringLiteral("file")});
    parser.process(app);

    QWidget window;
    window.setWindowTitle(i18nd("wordspring-runner", "Wordspring Runner"));
    window.resize(780, 620);
    auto *layout = new QVBoxLayout(&window);
    if (parser.isSet(QStringLiteral("settings")) || !parser.isSet(QStringLiteral("entry"))) {
        auto *settings = new SettingsWidget(&window);
        layout->addWidget(settings);
        if (parser.isSet(QStringLiteral("activity"))) settings->showActivity();
        auto *save = new QPushButton(i18nd("wordspring-runner", "Save settings"), &window);
        layout->addWidget(save);
        QObject::connect(save, &QPushButton::clicked, settings, &SettingsWidget::save);
    } else {
        auto *toolbar = new QHBoxLayout;
        auto *source = new QLabel(&window);
        source->setStyleSheet(QStringLiteral("font-weight: 600;"));
        toolbar->addWidget(source);
        toolbar->addStretch();
        auto *favorite = new QPushButton(&window);
        favorite->setCheckable(true);
        toolbar->addWidget(favorite);
        auto *history = new QPushButton(i18nd("wordspring-runner", "Favorites && History"), &window);
        toolbar->addWidget(history);
        layout->addLayout(toolbar);
        auto *body = new QTextBrowser(&window);
        body->setOpenLinks(false);
        body->setOpenExternalLinks(false);
        body->setFrameShape(QFrame::NoFrame);
        body->document()->setDocumentMargin(18);
        const auto textColor = window.palette().color(QPalette::Text);
        const auto baseColor = window.palette().color(QPalette::Base);
        const auto muted = QColor((textColor.red() * 6 + baseColor.red() * 4) / 10,
            (textColor.green() * 6 + baseColor.green() * 4) / 10,
            (textColor.blue() * 6 + baseColor.blue() * 4) / 10).name();
        body->document()->setDefaultStyleSheet(QStringLiteral(
            "body { font-size: 12pt; line-height: 135%; }"
            ".ws-head { font-family: serif; font-size: 22pt; font-weight: bold; margin-top: 2px; margin-bottom: 9px; }"
            ".ws-word { font-weight: bold; }"
            ".ws-pron { font-family: sans-serif; font-size: 11pt; font-weight: normal; color: %1; }"
            ".ws-pos { color: %1; font-size: 11pt; margin-top: 5px; margin-bottom: 9px; }"
            ".ws-posname { font-weight: bold; }"
            ".ws-sense { margin-top: 7px; margin-bottom: 5px; }"
            ".ws-number, .ws-qualifier { color: %1; }"
            ".ws-definition { font-weight: bold; }"
            ".ws-example { color: %1; font-style: italic; }"
            ".ws-subsense { margin-left: 20px; margin-top: 2px; margin-bottom: 2px; }"
            ".ws-section { margin-top: 16px; }"
            ".ws-section-title { color: %1; font-size: 9pt; font-weight: bold; margin-top: 8px; margin-bottom: 8px; }"
            ".ws-phrase { margin-left: 14px; margin-top: 6px; margin-bottom: 6px; }"
            ".ws-phrase-head { font-weight: bold; }"
            ".ws-phrase-definition { margin-left: 12px; margin-top: 2px; margin-bottom: 4px; }"
            "a { text-decoration: none; }"
        ).arg(muted));
        layout->addWidget(body);
        Article current;
        auto show = [&](const Article &article) {
            if (article.headword.isEmpty()) {
                body->setPlainText(i18nd("wordspring-runner", "This entry is unavailable. Its dictionary may have been removed."));
                return;
            }
            current = article;
            source->setText(article.dictionary);
            const auto heading = article.html.contains(QStringLiteral("ws-head")) || article.html.left(300).contains(QStringLiteral("<h1"))
                ? QString() : QStringLiteral("<h1 class=\"ws-head\">%1</h1>").arg(article.headword.toHtmlEscaped());
            body->setHtml(QStringLiteral("<html><body>") + heading + article.html + QStringLiteral("</body></html>"));
            body->verticalScrollBar()->setValue(0);
            favorite->setChecked(UserState::isFavorite(article.source, article.id));
            favorite->setText(favorite->isChecked() ? i18nd("wordspring-runner", "★ Saved") : i18nd("wordspring-runner", "☆ Add favorite"));
            UserState::recordVisit(article);
            window.setWindowTitle(i18nd("wordspring-runner", "%1 — Wordspring Runner", article.headword));
        };
        QObject::connect(favorite, &QPushButton::clicked, &window, [&](bool checked) {
            UserState::setFavorite(current, checked);
            favorite->setText(checked ? i18nd("wordspring-runner", "★ Saved") : i18nd("wordspring-runner", "☆ Add favorite"));
        });
        QObject::connect(history, &QPushButton::clicked, &window, [&] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--settings"), QStringLiteral("--activity")});
        });
        show(Store::article(parser.value(QStringLiteral("source")), parser.value(QStringLiteral("entry")).toLongLong()));
        QObject::connect(body, &QTextBrowser::anchorClicked, &window, [&](const QUrl &url) {
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
