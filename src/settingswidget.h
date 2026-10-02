#pragma once

#include "store.h"
#include <QWidget>

class QLineEdit;
class QTableWidget;
class QLabel;
class QProcess;

class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit SettingsWidget(QWidget *parent = nullptr);
    void load();
    void save();
signals:
    void modified();
private:
    void addSource(const QString &path);
    void importNext();
    void refreshTable();
    void removeSelected();
    void moveSelected(int direction);
    QLineEdit *m_trigger;
    QLineEdit *m_python;
    QTableWidget *m_table;
    QLabel *m_status;
    QLabel *m_help;
    QVector<DictionaryInfo> m_items;
    QStringList m_queue;
    QStringList m_pendingRemoval;
    QProcess *m_process = nullptr;
    QString m_importing;
    QByteArray m_outputBuffer;
};
