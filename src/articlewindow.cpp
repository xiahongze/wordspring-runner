#include "articlewindow.h"
#include "userstate.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScrollBar>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QIcon bookmarkIcon(bool saved, const QColor &color)
{
    const auto themed = QIcon::fromTheme(saved ? QStringLiteral("bookmarks-bookmarked") : QStringLiteral("bookmark-new"));
    if (!themed.isNull()) return themed;
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.8));
    painter.setBrush(saved ? QBrush(color) : Qt::NoBrush);
    QPainterPath path;
    path.moveTo(6, 3);
    path.lineTo(18, 3);
    path.lineTo(18, 21);
    path.lineTo(12, 16);
    path.lineTo(6, 21);
    path.closeSubpath();
    painter.drawPath(path);
    return QIcon(pixmap);
}

QIcon preferencesIcon(const QColor &color)
{
    const auto themed = QIcon::fromTheme(QStringLiteral("configure"));
    if (!themed.isNull()) return themed;
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(color);
    auto font = painter.font();
    font.setPointSize(18);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("⚙"));
    return QIcon(pixmap);
}
}

ArticleWindow::ArticleWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(i18nd("wordspring-runner", "Wordspring Runner"));
    resize(780, 620);
    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    m_source = new QLabel(this);
    m_source->setStyleSheet(QStringLiteral("font-weight: 600;"));
    toolbar->addWidget(m_source);
    toolbar->addStretch();
    m_bookmark = new QToolButton(this);
    m_bookmark->setObjectName(QStringLiteral("bookmarkButton"));
    m_bookmark->setCheckable(true);
    m_bookmark->setAutoRaise(true);
    m_bookmark->setIconSize(QSize(22, 22));
    toolbar->addWidget(m_bookmark);
    auto *preferences = new QToolButton(this);
    preferences->setObjectName(QStringLiteral("preferencesButton"));
    preferences->setAutoRaise(true);
    preferences->setIconSize(QSize(22, 22));
    preferences->setIcon(preferencesIcon(palette().color(QPalette::Text)));
    preferences->setToolTip(i18nd("wordspring-runner", "Preferences"));
    preferences->setAccessibleName(preferences->toolTip());
    toolbar->addWidget(preferences);
    layout->addLayout(toolbar);

    m_body = new QTextBrowser(this);
    m_body->setObjectName(QStringLiteral("articleBody"));
    m_body->setOpenLinks(false);
    m_body->setOpenExternalLinks(false);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->document()->setDocumentMargin(18);
    const auto textColor = palette().color(QPalette::Text);
    const auto baseColor = palette().color(QPalette::Base);
    const auto muted = QColor((textColor.red() * 6 + baseColor.red() * 4) / 10,
        (textColor.green() * 6 + baseColor.green() * 4) / 10,
        (textColor.blue() * 6 + baseColor.blue() * 4) / 10).name();
    m_body->document()->setDefaultStyleSheet(QStringLiteral(
        "body { font-size: 12pt; line-height: 135%; }"
        ".ws-head { font-family: serif; font-size: 22pt; font-weight: bold; margin-top: 2px; margin-bottom: 9px; }"
        ".ws-word { font-weight: bold; }"
        ".ws-pron { font-family: sans-serif; font-size: 11pt; font-weight: normal; color: %1; }"
        ".ws-pos { color: %1; font-size: 11pt; margin-top: 5px; margin-bottom: 9px; }"
        ".ws-posname { font-weight: bold; }"
        ".ws-sense { margin-top: 7px; margin-bottom: 5px; }"
        ".ws-translation-group { margin-top: 4px; margin-bottom: 4px; }"
        ".ws-example-row { margin-left: 18px; margin-top: 3px; margin-bottom: 3px; }"
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
    layout->addWidget(m_body);

    connect(m_bookmark, &QToolButton::clicked, this, [this](bool checked) {
        UserState::setFavorite(m_current, checked);
        updateBookmarkButton();
    });
    connect(preferences, &QToolButton::clicked, this, [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--settings")});
    });
    connect(m_body, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() != QStringLiteral("bword")) return;
        QString word = QUrl::fromAce(url.host().toLatin1(), QUrl::IgnoreIDNWhitelist);
        const auto path = url.path(QUrl::FullyDecoded);
        if (!path.isEmpty()) word += path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
        followWord(word);
    });
}

void ArticleWindow::updateBookmarkButton()
{
    const bool saved = m_bookmark->isChecked();
    m_bookmark->setIcon(bookmarkIcon(saved, palette().color(QPalette::Text)));
    m_bookmark->setToolTip(saved ? i18nd("wordspring-runner", "Remove bookmark") : i18nd("wordspring-runner", "Add bookmark"));
    m_bookmark->setAccessibleName(m_bookmark->toolTip());
}

void ArticleWindow::showArticle(const Article &article)
{
    if (article.headword.isEmpty()) {
        m_body->setPlainText(i18nd("wordspring-runner", "This entry is unavailable. Its dictionary may have been removed."));
        return;
    }
    m_current = article;
    m_source->setText(article.dictionary);
    const auto heading = article.html.contains(QStringLiteral("ws-head")) || article.html.left(300).contains(QStringLiteral("<h1"))
        ? QString() : QStringLiteral("<h1 class=\"ws-head\">%1</h1>").arg(article.headword.toHtmlEscaped());
    m_body->setHtml(QStringLiteral("<html><body>") + heading + article.html + QStringLiteral("</body></html>"));
    m_body->verticalScrollBar()->setValue(0);
    m_bookmark->setChecked(UserState::isFavorite(article.source, article.id));
    updateBookmarkButton();
    UserState::recordVisit(article);
    setWindowTitle(i18nd("wordspring-runner", "%1 — Wordspring Runner", article.headword));
}

void ArticleWindow::followWord(const QString &word)
{
    const auto matches = Store::lookup(word, 1);
    if (!matches.isEmpty()) showArticle(Store::article(matches.first().source, matches.first().id));
}
