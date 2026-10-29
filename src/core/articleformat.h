#pragma once
#include <QJsonArray>
#include <QString>

struct FormattedArticle {
    QJsonArray blocks;
    QJsonArray links;
    QString text;
};

// Input is the importer's sanitized HTML, never a web page. Keep all article text
// and expose dictionary links as explicit native UI actions.
FormattedArticle formatArticle(const QString &html);
