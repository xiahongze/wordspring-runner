#include "core/configuration.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTemporaryDir>
#include <QtTest>

class ConfigurationTest : public QObject {
    Q_OBJECT
private slots:
    void kconfigCompatibility();
};

void ConfigurationTest::kconfigCompatibility()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("wordspringrc"));
    Configuration::setPath(path);
    const auto kde = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    KConfigGroup group(kde, QStringLiteral("General"));
    const QStringList samples = {QStringLiteral("define"), QStringLiteral(" 日本語 "),
        QStringLiteral("path\\name\nwith\ttabs"), QStringLiteral("[{\"source\":\"/a b/日本語.ifo\",\"name\":\"A \\\"quote\\\"\"}]")};
    for (const auto &sample : samples) {
        group.writeEntry("Value", sample);
        group.sync();
        QCOMPARE(Configuration::read("Value", {}), sample);
        Configuration::write({{"Value", sample + QStringLiteral(" suffix")}});
        kde->reparseConfiguration();
        QCOMPARE(group.readEntry("Value", QString()), sample + QStringLiteral(" suffix"));
    }
    Configuration::setPath({});
}

QTEST_GUILESS_MAIN(ConfigurationTest)
#include "configuration_test.moc"
