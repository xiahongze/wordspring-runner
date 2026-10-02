#include "settingswidget.h"
#include <KCModule>
#include <KPluginFactory>
#include <QVBoxLayout>

class WordspringKcm : public KCModule {
    Q_OBJECT
public:
    WordspringKcm(QObject *parent, const KPluginMetaData &data) : KCModule(parent, data)
    {
        auto *layout = new QVBoxLayout(widget());
        m_settings = new SettingsWidget(widget());
        layout->addWidget(m_settings);
        connect(m_settings, &SettingsWidget::modified, this, &KCModule::markAsChanged);
    }
    void load() override { m_settings->load(); KCModule::load(); }
    void save() override { m_settings->save(); KCModule::save(); }
private:
    SettingsWidget *m_settings;
};

K_PLUGIN_CLASS(WordspringKcm)
#include "kcm.moc"
