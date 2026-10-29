#include "store.h"
#include "userstate.h"
#include "core/articleformat.h"
#include "core/configuration.h"
#include "core/error.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cstdio>

namespace {
constexpr int protocol = 1;

QString expanded(QString path)
{
    if (path == QStringLiteral("~")) return QDir::homePath();
    if (path.startsWith(QStringLiteral("~/"))) path.replace(0, 1, QDir::homePath());
    return path;
}

QVector<DictionaryInfo> parseDictionaries(const QString &json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw WordspringError("invalid_config", "Dictionaries must be a JSON array");
    QVector<DictionaryInfo> result;
    for (const auto &value : document.array()) {
        if (!value.isObject()) throw WordspringError("invalid_config", "Every dictionary must be an object");
        const auto object = value.toObject();
        if (!object.value("source").isString() || object.value("source").toString().isEmpty()
            || !object.value("name").isString() || object.value("name").toString().isEmpty()
            || (object.contains("enabled") && !object.value("enabled").isBool())
            || (object.contains("label") && !object.value("label").isString()))
            throw WordspringError("invalid_config", "A dictionary needs source/name strings, an optional enabled boolean, and an optional label string");
        const auto source = expanded(object.value("source").toString());
        if (!QDir::isAbsolutePath(source)) throw WordspringError("invalid_config", "Dictionary sources must be absolute paths or start with ~/");
        for (const auto &item : result) if (item.source == source) throw WordspringError("invalid_config", "Duplicate dictionary source");
        result.append({source, object.value("name").toString(), object.value("enabled").toBool(true), object.value("label").toString()});
    }
    return result;
}

QJsonArray dictionariesJson(const QVector<DictionaryInfo> &items)
{
    QJsonArray result;
    for (const auto &item : items) result.append(QJsonObject{
        {"source", item.source}, {"name", item.name}, {"enabled", item.enabled},
        {"label", item.label.isEmpty() ? Store::suggestLabel(item.name) : item.label},
        {"available", QFileInfo::exists(Store::databasePath(item.source))}});
    return result;
}

QJsonArray issuesJson(const QVector<StoreIssue> &issues)
{
    QJsonArray result;
    for (const auto &issue : issues) result.append(QJsonObject{{"code", issue.code}, {"message", issue.message}, {"source", issue.source}});
    return result;
}

void checkState()
{
    if (!UserState::lastError().isEmpty()) throw WordspringError("state_unavailable", UserState::lastError());
}

QJsonObject articleJson(const Article &article, bool full)
{
    QJsonArray meanings;
    for (const auto &meaning : article.meanings.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) meanings.append(meaning);
    QJsonObject result{{"source", article.source}, {"id", QString::number(article.id)}, {"headword", article.headword},
        {"preview", article.preview}, {"meanings", meanings}, {"dictionary", article.dictionary}, {"label", article.dictionaryLabel}};
    if (full) {
        const auto formatted = formatArticle(article.html);
        result.insert("blocks", formatted.blocks);
        result.insert("links", formatted.links);
        result.insert("text", formatted.text);
        result.insert("favorite", UserState::isFavorite(article.source, article.id));
        checkState();
    }
    return result;
}

Article entry(const QCommandLineParser &parser)
{
    bool ok = false;
    const auto id = parser.value("id").toLongLong(&ok);
    const auto source = expanded(parser.value("source"));
    if (!ok || id <= 0 || source.isEmpty()) throw WordspringError("invalid_arguments", "An entry needs --source and a positive --id");
    QVector<StoreIssue> issues;
    const auto article = Store::article(source, id, &issues);
    if (!issues.isEmpty()) throw WordspringError(issues.first().code, issues.first().message);
    if (article.headword.isEmpty()) throw WordspringError("entry_not_found", "This entry is unavailable; its dictionary may have been reimported or removed");
    return article;
}

QJsonObject config()
{
    return {{"config_path", Configuration::path()}, {"trigger", Store::trigger()}, {"python", Store::pythonExecutable()}, {"dictionaries", dictionariesJson(Store::dictionaries())}};
}

QJsonArray saved(const QVector<SavedEntry> &entries)
{
    checkState();
    QJsonArray result;
    for (const auto &item : entries) result.append(QJsonObject{{"source", item.source}, {"id", QString::number(item.id)},
        {"headword", item.headword}, {"dictionary", item.dictionary}, {"timestamp", double(item.timestamp)}});
    return result;
}

QStringList discover(const QString &path)
{
    const QFileInfo info(expanded(path));
    if (!info.exists()) throw WordspringError("source_unavailable", "Dictionary source does not exist");
    if (info.suffix() == QStringLiteral("dictionary") || info.suffix() == QStringLiteral("ifo")) return {info.canonicalFilePath()};
    if (!info.isDir()) throw WordspringError("invalid_source", "Choose an Apple .dictionary bundle, a StarDict .ifo, or a folder");
    QStringList paths;
    QDirIterator it(info.absoluteFilePath(), {"*.dictionary", "*.ifo"}, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) paths.append(QFileInfo(it.next()).canonicalFilePath());
    paths.removeDuplicates();
    paths.sort();
    if (paths.isEmpty()) throw WordspringError("no_sources", "No dictionary bundles or StarDict files were found");
    return paths;
}

QJsonObject importDictionaries(const QCommandLineParser &parser, const QString &path)
{
    auto script = expanded(parser.value("importer"));
    if (script.isEmpty()) {
        script = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("wordspring/import_dictionary.py"));
        const auto bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/wordspring/import_dictionary.py");
        if (script.isEmpty() && QFileInfo::exists(bundled)) script = QDir::cleanPath(bundled);
    }
    if (script.isEmpty() || !QFileInfo::exists(script)) throw WordspringError("importer_missing", "Install import_dictionary.py alongside the CLI or supply --importer");
    const auto python = parser.isSet("python") ? expanded(parser.value("python")) : expanded(Store::pythonExecutable());
    bool validTimeout = false;
    const int timeout = parser.value("timeout-ms").toInt(&validTimeout);
    if (!validTimeout || timeout < 50 || timeout > 3600000) throw WordspringError("invalid_arguments", "Import timeout must be 50..3600000 milliseconds");
    auto items = Store::dictionaries();
    QJsonArray imported;
    QJsonArray warnings;
    for (const auto &source : discover(path)) {
        QProcess process;
        process.start(python, {script, "--input", source, "--output", Store::databasePath(source)});
        if (!process.waitForStarted(3000)) throw WordspringError("python_missing", "Could not start import interpreter: " + process.errorString());
        if (!process.waitForFinished(timeout)) {
            process.kill();
            process.waitForFinished(3000);
            throw WordspringError("timeout", "Dictionary import timed out; the previous imported copy is preserved");
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            warnings.append(QJsonObject{{"code", "import_failed"}, {"source", source}, {"message", QString::fromUtf8(process.readAllStandardError()).trimmed()}});
            continue;
        }
        QJsonObject metadata;
        for (const auto &line : process.readAllStandardOutput().split('\n')) {
            const auto object = QJsonDocument::fromJson(line).object();
            if (object.contains("name")) metadata = object;
        }
        if (metadata.isEmpty()) throw WordspringError("invalid_import_output", "Importer did not return dictionary metadata");
        bool found = false;
        for (auto &item : items) if (item.source == source) {
            const auto old = Store::suggestLabel(item.name);
            item.name = metadata.value("name").toString();
            if (item.label.isEmpty() || item.label == old) item.label = Store::suggestLabel(item.name);
            found = true;
        }
        if (!found) {
            const auto name = metadata.value("name").toString();
            items.append({source, name, true, Store::suggestLabel(name)});
        }
        Store::setDictionaries(items);
        metadata.insert("source", source);
        imported.append(metadata);
    }
    if (imported.isEmpty()) throw WordspringError("import_failed", warnings.isEmpty() ? "Nothing was imported" : warnings.first().toObject().value("message").toString());
    return {{"imported", imported}, {"warnings", warnings}, {"dictionaries", dictionariesJson(items)}};
}

QJsonObject dispatch(const QString &command, const QStringList &arguments, const QCommandLineParser &parser)
{
    if (command == QStringLiteral("config")) return config();
    if (command == QStringLiteral("configure")) {
        QMap<QString, QString> values;
        if (parser.isSet("trigger")) {
            const auto trigger = parser.value("trigger").trimmed();
            if (trigger.isEmpty() || trigger.contains(QRegularExpression(QStringLiteral("\\s")))) throw WordspringError("invalid_config", "The KRunner keyword must be one nonempty word");
            values.insert("Trigger", trigger);
        }
        if (parser.isSet("python")) {
            if (parser.value("python").trimmed().isEmpty()) throw WordspringError("invalid_config", "Import interpreter cannot be empty");
            values.insert("PythonExecutable", parser.value("python").trimmed());
        }
        if (parser.isSet("dictionaries-json")) {
            const auto items = parseDictionaries(parser.value("dictionaries-json"));
            values.insert("Dictionaries", QString::fromUtf8(QJsonDocument(dictionariesJson(items)).toJson(QJsonDocument::Compact)));
        }
        Configuration::write(values);
        return config();
    }
    if (command == QStringLiteral("lookup")) {
        if (arguments.size() != 1) throw WordspringError("invalid_arguments", "Usage: wordspring-lookup lookup <query>");
        const auto query = arguments.first().trimmed();
        if (query.isEmpty() || query.size() > 128) throw WordspringError("invalid_query", "Enter a word or phrase of at most 128 characters");
        bool ok = false;
        const int limit = parser.value("limit").toInt(&ok);
        if (!ok || limit < 1 || limit > 20) throw WordspringError("invalid_arguments", "Result limit must be 1..20 per dictionary");
        bool enabled = false;
        for (const auto &dictionary : Store::dictionaries()) enabled |= dictionary.enabled;
        if (!enabled) throw WordspringError("no_dictionaries", "No dictionaries are enabled; add one in Wordspring's Dictionaries view");
        QVector<StoreIssue> issues;
        const auto articles = Store::lookup(query, limit, &issues);
        if (articles.isEmpty()) {
            if (!issues.isEmpty()) throw WordspringError(issues.first().code, issues.first().message + " (" + issues.first().source + ")");
            throw WordspringError("not_found", "No entry found for “" + query + "”");
        }
        QJsonArray results;
        for (const auto &article : articles) results.append(articleJson(article, false));
        return {{"query", query}, {"results", results}, {"warnings", issuesJson(issues)}};
    }
    if (command == QStringLiteral("entry")) {
        const auto article = entry(parser);
        if (parser.isSet("record-visit")) { UserState::recordVisit(article); checkState(); }
        return {{"article", articleJson(article, true)}};
    }
    if (command == QStringLiteral("favorite")) {
        const auto article = entry(parser);
        const auto favorite = parser.value("enabled");
        if (favorite != QStringLiteral("true") && favorite != QStringLiteral("false")) throw WordspringError("invalid_arguments", "Supply --enabled true or --enabled false");
        UserState::setFavorite(article, favorite == QStringLiteral("true"));
        checkState();
        return {{"favorite", favorite == QStringLiteral("true")}};
    }
    if (command == QStringLiteral("history")) return {{"entries", saved(UserState::history(parser.value("filter")))}};
    if (command == QStringLiteral("favorites")) return {{"entries", saved(UserState::favorites(parser.value("filter")))}};
    if (command == QStringLiteral("clear-history")) { UserState::clearHistory(); checkState(); return {}; }
    if (command == QStringLiteral("import")) {
        if (arguments.size() != 1) throw WordspringError("invalid_arguments", "Usage: wordspring-lookup import <source-or-folder>");
        return importDictionaries(parser, arguments.first());
    }
    if (command == QStringLiteral("remove")) {
        const auto source = expanded(parser.value("source"));
        auto items = Store::dictionaries();
        bool found = false;
        for (int i = items.size() - 1; i >= 0; --i) if (items[i].source == source) { items.removeAt(i); found = true; }
        if (!found) throw WordspringError("source_not_found", "Dictionary is not configured");
        Store::setDictionaries(items);
        const auto database = Store::databasePath(source);
        if (QFileInfo::exists(database) && !QFile::remove(database)) throw WordspringError("database_unavailable", "Could not remove imported database; source has been disabled");
        return config();
    }
    throw WordspringError("invalid_arguments", "Unknown command: " + command);
}

void print(QJsonObject object)
{
    object.insert("protocol", protocol);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("wordspring-lookup");
    QCoreApplication::setApplicationVersion(WORDSPRING_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Offline dictionary JSON interface (protocol 1). Commands: lookup, entry, config, configure, import, remove, favorite, favorites, history, clear-history.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "Command to run");
    parser.addPositionalArgument("argument", "Query or import path", "[argument]");
    parser.addOptions({
        {"config", "Override wordspringrc path", "path"},
        {"dictionaries-json", "Dictionary list override; configure saves it", "json"},
        {"limit", "Maximum entries per dictionary", "count", "2"},
        {"source", "Dictionary source", "path"},
        {"id", "Entry ID", "id"},
        {"record-visit", "Record an opened entry in history"},
        {"enabled", "Favorite state", "bool"},
        {"filter", "Filter saved words", "text"},
        {"trigger", "Shared KRunner keyword", "word"},
        {"python", "Import interpreter", "path"},
        {"importer", "Override importer script location", "path"},
        {"timeout-ms", "Import timeout in milliseconds", "ms", "300000"},
    });
    try {
        if (!parser.parse(app.arguments())) throw WordspringError("invalid_arguments", parser.errorText());
        if (parser.isSet("help")) parser.showHelp();
        if (parser.isSet("version")) parser.showVersion();
        if (parser.isSet("config")) Configuration::setPath(expanded(parser.value("config")));
        auto positional = parser.positionalArguments();
        if (positional.isEmpty()) throw WordspringError("invalid_arguments", "Supply a command; see --help");
        const auto command = positional.takeFirst();
        if (parser.isSet("dictionaries-json") && command != QStringLiteral("configure")) Store::useDictionaries(parseDictionaries(parser.value("dictionaries-json")));
        auto result = dispatch(command, positional, parser);
        result.insert("ok", true);
        print(result);
        return 0;
    } catch (const WordspringError &error) {
        print({{"ok", false}, {"error", QJsonObject{{"code", error.code}, {"message", error.message}}}});
    } catch (const std::exception &error) {
        print({{"ok", false}, {"error", QJsonObject{{"code", "internal_error"}, {"message", QString::fromUtf8(error.what())}}}});
    }
    return 1;
}
