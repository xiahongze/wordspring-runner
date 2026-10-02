#pragma once

#include "store.h"
#include <QWidget>

class QLabel;
class QToolButton;
class QTextBrowser;

class ArticleWindow : public QWidget {
    Q_OBJECT
public:
    explicit ArticleWindow(QWidget *parent = nullptr);
    void showArticle(const Article &article);
    const Article &currentArticle() const { return m_current; }
private:
    void followWord(const QString &word);
    void updateBookmarkButton();
    Article m_current;
    QLabel *m_source;
    QToolButton *m_bookmark;
    QTextBrowser *m_body;
};
