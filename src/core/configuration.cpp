#include "configuration.h"
#include "error.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString overridePath;

QString unescape(const QString &text)
{
    QString result;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('\\') || i + 1 == text.size()) {
            result += text[i];
            continue;
        }
        const QChar next = text[++i];
        if (next == QLatin1Char('n')) result += QLatin1Char('\n');
        else if (next == QLatin1Char('r')) result += QLatin1Char('\r');
        else if (next == QLatin1Char('t')) result += QLatin1Char('\t');
        else if (next == QLatin1Char('s')) result += QLatin1Char(' ');
        else if (next == QLatin1Char('x') && i + 2 < text.size()) {
            bool ok = false;
            const auto value = text.mid(i + 1, 2).toUShort(&ok, 16);
            if (ok) { result += QChar(value); i += 2; }
            else result += next;
        } else result += next;
    }
    return result;
}

QString escape(QString text)
{
    text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    if (text.startsWith(QLatin1Char(' '))) text.replace(0, 1, QStringLiteral("\\s"));
    if (text.endsWith(QLatin1Char(' '))) text.replace(text.size() - 1, 1, QStringLiteral("\\s"));
    return text;
}

QStringList lines()
{
    QFile file(Configuration::path());
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly))
        throw WordspringError("config_unavailable", "Cannot read settings: " + file.errorString());
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
}
}

QString Configuration::path()
{
    if (!overridePath.isEmpty()) return overridePath;
    const auto existing = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("wordspringrc"));
    return existing.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/wordspringrc") : existing;
}

void Configuration::setPath(const QString &path) { overridePath = path; }

QString Configuration::read(const QString &key, const QString &fallback)
{
    bool general = false;
    QString value = fallback;
    for (const auto &line : lines()) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) general = trimmed == QStringLiteral("[General]");
        else if (general && !trimmed.startsWith(QLatin1Char('#'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq >= 0 && line.left(eq).trimmed() == key) value = unescape(line.mid(eq + 1).trimmed());
        }
    }
    return value;
}

void Configuration::write(const QMap<QString, QString> &values)
{
    const auto destination = path();
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        throw WordspringError("config_unavailable", "Cannot create settings directory");
    QLockFile lock(destination + QStringLiteral(".lock"));
    if (!lock.tryLock(1000)) throw WordspringError("config_busy", "Settings are being changed by another process");
    auto input = lines();
    QStringList output;
    bool general = false;
    bool written = false;
    auto appendValues = [&] {
        for (auto it = values.cbegin(); it != values.cend(); ++it) output.append(it.key() + QLatin1Char('=') + escape(it.value()));
        written = true;
    };
    for (const auto &line : input) {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            if (general && !written) appendValues();
            general = trimmed == QStringLiteral("[General]");
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (!general || eq < 0 || !values.contains(line.left(eq).trimmed())) output.append(line);
    }
    if (!written) {
        if (!general) output.append(QStringLiteral("[General]"));
        appendValues();
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) throw WordspringError("config_unavailable", file.errorString());
    const auto bytes = (output.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) throw WordspringError("config_unavailable", file.errorString());
}
