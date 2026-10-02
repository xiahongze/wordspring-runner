#include "runner.h"
#include "store.h"

#include <KPluginFactory>
#include <KLocalizedString>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerContext>
#include <QProcess>
#include <QVariantMap>

WordspringRunner::WordspringRunner(QObject *parent, const KPluginMetaData &data)
    : KRunner::AbstractRunner(parent, data)
{
}

void WordspringRunner::reloadConfiguration()
{
    Store::reload();
    m_trigger = Store::trigger().trimmed();
    if (m_trigger.isEmpty()) m_trigger = QStringLiteral("define");
    setSyntaxes({});
    addSyntax(m_trigger + QStringLiteral(" :q:"), i18nd("wordspring-runner", "Look up a word in enabled dictionaries"));
}

void WordspringRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query().trimmed();
    const QString prefix = m_trigger + QLatin1Char(' ');
    if (!query.startsWith(prefix, Qt::CaseInsensitive)) return;
    const QString word = query.mid(prefix.size()).trimmed();
    if (word.isEmpty() || word.size() > 128) return;
    for (const auto &article : Store::lookup(word)) {
        KRunner::QueryMatch match(this);
        match.setId(article.source + QLatin1Char('#') + QString::number(article.id));
        match.setText(article.headword);
        match.setSubtext(article.dictionary + QStringLiteral(" · ") + article.preview.left(220));
        match.setIconName(QStringLiteral("accessories-dictionary"));
        match.setData(QVariantMap{{"source", article.source}, {"id", article.id}});
        match.setRelevance(1.0);
        context.addMatch(match);
    }
}

void WordspringRunner::run(const KRunner::RunnerContext &, const KRunner::QueryMatch &match)
{
    const auto data = match.data().toMap();
    QProcess::startDetached(QStringLiteral(WORDSPRING_APP_PATH), {
        QStringLiteral("--source"), data.value(QStringLiteral("source")).toString(),
        QStringLiteral("--entry"), data.value(QStringLiteral("id")).toString()
    });
}

K_PLUGIN_CLASS_WITH_JSON(WordspringRunner, "krunner_wordspring.json")
#include "runner.moc"
