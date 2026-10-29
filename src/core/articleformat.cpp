#include "articleformat.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVector>

namespace {
QString decode(const QString &text)
{
    static const QRegularExpression entity(QStringLiteral("&(#x[0-9a-fA-F]+|#[0-9]+|amp|lt|gt|quot|apos|nbsp);"));
    QString out;
    int offset = 0;
    auto matches = entity.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        out += text.mid(offset, match.capturedStart() - offset);
        const auto name = match.captured(1);
        if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = name.mid(name.startsWith(QStringLiteral("#x")) ? 2 : 1).toUInt(&ok, name.startsWith(QStringLiteral("#x")) ? 16 : 10);
            if (ok && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff)) {
                const char32_t point = code;
                out += QString::fromUcs4(&point, 1);
            } else out += match.captured();
        } else if (name == QStringLiteral("amp")) out += QLatin1Char('&');
        else if (name == QStringLiteral("lt")) out += QLatin1Char('<');
        else if (name == QStringLiteral("gt")) out += QLatin1Char('>');
        else if (name == QStringLiteral("quot")) out += QLatin1Char('"');
        else if (name == QStringLiteral("nbsp")) out += QLatin1Char(' ');
        else out += QLatin1Char('\'');
        offset = match.capturedEnd();
    }
    return out + text.mid(offset);
}

QString role(const QString &tag, const QString &classes)
{
    if (classes.contains(QStringLiteral("ws-head-sub"))) return QStringLiteral("origin");
    if (classes.contains(QStringLiteral("ws-head")) || tag == QStringLiteral("h1")) return QStringLiteral("heading");
    if (classes.contains(QStringLiteral("ws-note")) || tag == QStringLiteral("blockquote")) return QStringLiteral("note");
    if (classes.contains(QStringLiteral("ws-example"))) return QStringLiteral("example");
    if (classes.contains(QStringLiteral("ws-pron"))) return QStringLiteral("pronunciation");
    if (classes.contains(QStringLiteral("ws-pos"))) return QStringLiteral("part_of_speech");
    if (tag == QStringLiteral("h2") || tag == QStringLiteral("h3")) return QStringLiteral("section");
    return {};
}
}

FormattedArticle formatArticle(const QString &html)
{
    struct Frame { QString tag; QString role; QString href; QString linkText; };
    static const QRegularExpression tokens(QStringLiteral("<[^>]*>|[^<]+"));
    static const QRegularExpression tagName(QStringLiteral("^<\\s*/?\\s*([a-zA-Z0-9]+)"));
    static const QRegularExpression attributes(QStringLiteral("(class|href)\\s*=\\s*[\"']([^\"']*)[\"']"));
    const QSet<QString> blockTags{"div", "p", "br", "li", "blockquote", "h1", "h2", "h3", "hr", "table", "tr"};
    const QSet<QString> voidTags{"br", "hr", "img", "meta", "input"};
    FormattedArticle result;
    QVector<Frame> stack;
    QSet<QString> linked;
    QString text;
    QString currentRole = QStringLiteral("body");
    auto flush = [&] {
        const auto value = text.simplified();
        if (!value.isEmpty()) {
            result.blocks.append(QJsonObject{{"role", currentRole}, {"text", value}});
            if (!result.text.isEmpty()) result.text += QLatin1Char('\n');
            result.text += value;
        }
        text.clear();
    };
    auto inheritedRole = [&] {
        for (auto it = stack.crbegin(); it != stack.crend(); ++it) if (!it->role.isEmpty()) return it->role;
        return QStringLiteral("body");
    };
    auto addLink = [&](const Frame &frame) {
        const QUrl url(decode(frame.href));
        if (url.scheme() != QStringLiteral("bword")) return;
        QString query = QUrl::fromAce(url.host().toLatin1(), QUrl::IgnoreIDNWhitelist);
        const auto path = url.path(QUrl::FullyDecoded);
        query += path.startsWith(QLatin1Char('/')) ? path.mid(1) : path;
        if (query.isEmpty() || linked.contains(query)) return;
        linked.insert(query);
        const auto label = frame.linkText.simplified();
        result.links.append(QJsonObject{{"query", query}, {"text", label.isEmpty() ? query : label}});
    };
    auto matches = tokens.globalMatch(html);
    while (matches.hasNext()) {
        const auto token = matches.next().captured();
        if (!token.startsWith(QLatin1Char('<'))) {
            bool suppressed = false;
            for (auto &frame : stack) {
                if (frame.tag == QStringLiteral("script") || frame.tag == QStringLiteral("style")) suppressed = true;
                if (!frame.href.isEmpty()) frame.linkText += decode(token);
            }
            if (!suppressed) text += decode(token);
            continue;
        }
        const auto nameMatch = tagName.match(token);
        if (!nameMatch.hasMatch()) continue;
        const auto name = nameMatch.captured(1).toLower();
        const bool closing = token.startsWith(QStringLiteral("</"));
        if (blockTags.contains(name)) flush();
        if (closing) {
            for (int i = stack.size() - 1; i >= 0; --i) {
                if (stack[i].tag != name) continue;
                while (stack.size() > i) addLink(stack.takeLast());
                break;
            }
        } else if (!voidTags.contains(name)) {
            QString classes, href;
            auto attrs = attributes.globalMatch(token);
            while (attrs.hasNext()) {
                const auto attr = attrs.next();
                if (attr.captured(1) == QStringLiteral("class")) classes = attr.captured(2);
                else href = attr.captured(2);
            }
            stack.append({name, role(name, classes), href, {}});
        }
        // Inline pronunciation/part-of-speech stays with the surrounding text.
        // Block boundaries choose the most specific semantic ancestor.
        if (blockTags.contains(name)) currentRole = inheritedRole();
    }
    flush();
    while (!stack.isEmpty()) addLink(stack.takeLast());
    return result;
}
