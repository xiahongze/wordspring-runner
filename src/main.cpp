#include "articlewindow.h"
#include "settingswidget.h"
#include "store.h"

#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineParser>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(i18nd("wordspring-runner", "Wordspring Runner"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("settings"), i18nd("wordspring-runner", "Open dictionary settings")});
    parser.addOption({QStringLiteral("activity"), i18nd("wordspring-runner", "Open bookmarks and history")});
    parser.addOption({QStringLiteral("source"), i18nd("wordspring-runner", "Dictionary source"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("entry"), i18nd("wordspring-runner", "Entry ID"), QStringLiteral("id")});
    parser.addOption({QStringLiteral("screenshot"), i18nd("wordspring-runner", "Save a screenshot and exit"), QStringLiteral("file")});
    parser.process(app);

    QWidget *window = nullptr;
    if (parser.isSet(QStringLiteral("settings")) || !parser.isSet(QStringLiteral("entry"))) {
        window = new QWidget;
        window->setWindowTitle(i18nd("wordspring-runner", "Wordspring Runner"));
        window->resize(780, 620);
        auto *layout = new QVBoxLayout(window);
        auto *settings = new SettingsWidget(window);
        layout->addWidget(settings);
        if (parser.isSet(QStringLiteral("activity"))) settings->showActivity();
        auto *save = new QPushButton(i18nd("wordspring-runner", "Save settings"), window);
        layout->addWidget(save);
        QObject::connect(save, &QPushButton::clicked, settings, &SettingsWidget::save);
    } else {
        auto *articleWindow = new ArticleWindow;
        articleWindow->showArticle(Store::article(parser.value(QStringLiteral("source")), parser.value(QStringLiteral("entry")).toLongLong()));
        window = articleWindow;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    if (parser.isSet(QStringLiteral("screenshot"))) {
        const auto path = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(250, window, [window, path, &app] {
            window->grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
