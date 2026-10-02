#pragma once

#include <KRunner/AbstractRunner>

class WordspringRunner : public KRunner::AbstractRunner {
    Q_OBJECT
public:
    WordspringRunner(QObject *parent, const KPluginMetaData &data);
    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;
    void reloadConfiguration() override;
private:
    QString m_trigger;
};
