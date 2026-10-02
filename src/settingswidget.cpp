#include "settingswidget.h"
#include <KLocalizedString>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_trigger = new QLineEdit(this);
    m_trigger->setPlaceholderText(QStringLiteral("define"));
    m_trigger->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\S+")), m_trigger));
    form->addRow(i18nd("wordspring-runner", "KRunner keyword:"), m_trigger);
    m_python = new QLineEdit(this);
    m_python->setPlaceholderText(QStringLiteral("python3"));
    form->addRow(i18nd("wordspring-runner", "Import Python:"), m_python);
    layout->addLayout(form);
    m_help = new QLabel(this);
    layout->addWidget(m_help);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({i18nd("wordspring-runner", "Enabled"), i18nd("wordspring-runner", "Label"), i18nd("wordspring-runner", "Dictionary")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    layout->addWidget(m_table);

    auto *buttons = new QHBoxLayout;
    auto addButton = [&](const QString &text, auto action) {
        auto *button = new QPushButton(text, this);
        buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, action);
    };
    addButton(i18nd("wordspring-runner", "Add Apple bundle"), [this] {
        const auto path = QFileDialog::getExistingDirectory(this, i18nd("wordspring-runner", "Choose .dictionary bundle"));
        if (!path.isEmpty()) addSource(path);
    });
    addButton(i18nd("wordspring-runner", "Add folder"), [this] {
        const auto path = QFileDialog::getExistingDirectory(this, i18nd("wordspring-runner", "Choose dictionary folder"));
        if (!path.isEmpty()) addSource(path);
    });
    addButton(i18nd("wordspring-runner", "Add StarDict"), [this] {
        const auto path = QFileDialog::getOpenFileName(this, i18nd("wordspring-runner", "Choose StarDict .ifo"), {}, i18nd("wordspring-runner", "StarDict (*.ifo)"));
        if (!path.isEmpty()) addSource(path);
    });
    layout->addLayout(buttons);

    auto *manage = new QHBoxLayout;
    auto manageButton = [&](const QString &text, auto action) {
        auto *button = new QPushButton(text, this);
        manage->addWidget(button);
        connect(button, &QPushButton::clicked, this, action);
    };
    manageButton(i18nd("wordspring-runner", "Reimport"), [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_items.size()) { m_queue.append(m_items[row].source); importNext(); }
    });
    manageButton(i18nd("wordspring-runner", "Remove"), [this] { removeSelected(); });
    manageButton(i18nd("wordspring-runner", "Move up"), [this] { moveSelected(-1); });
    manageButton(i18nd("wordspring-runner", "Move down"), [this] { moveSelected(1); });
    manage->addStretch();
    layout->addLayout(manage);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_trigger, &QLineEdit::textChanged, this, [this](const QString &trigger) {
        m_help->setText(i18nd("wordspring-runner", "Type “%1 word” in KRunner. Add dictionary bundles or a folder containing them.", trigger));
        emit modified();
    });
    connect(m_python, &QLineEdit::textChanged, this, &SettingsWidget::modified);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->row() >= m_items.size()) return;
        if (item->column() == 0) m_items[item->row()].enabled = item->checkState() == Qt::Checked;
        if (item->column() == 1) m_items[item->row()].label = item->text().trimmed().left(12);
        emit modified();
    });
    load();
}

void SettingsWidget::load()
{
    m_trigger->setText(Store::trigger());
    m_python->setText(Store::pythonExecutable());
    m_items = Store::dictionaries();
    m_pendingRemoval.clear();
    refreshTable();
}

void SettingsWidget::save()
{
    const auto trigger = m_trigger->text().trimmed().isEmpty() ? QStringLiteral("define") : m_trigger->text().trimmed();
    m_trigger->setText(trigger);
    Store::setTrigger(trigger);
    Store::setPythonExecutable(m_python->text().trimmed().isEmpty() ? QStringLiteral("python3") : m_python->text().trimmed());
    Store::setDictionaries(m_items);
    for (const auto &source : m_pendingRemoval) QFile::remove(Store::databasePath(source));
    m_pendingRemoval.clear();
    m_status->setText(i18nd("wordspring-runner", "Settings saved."));
}

void SettingsWidget::refreshTable()
{
    QSignalBlocker blocker(m_table);
    m_table->setRowCount(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        auto *enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        enabled->setCheckState(m_items[i].enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, enabled);
        auto *label = new QTableWidgetItem(m_items[i].label);
        label->setToolTip(i18nd("wordspring-runner", "Double-click to edit the short label shown in KRunner."));
        m_table->setItem(i, 1, label);
        auto *name = new QTableWidgetItem(m_items[i].name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setToolTip(m_items[i].source);
        m_table->setItem(i, 2, name);
    }
    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(1);
}

void SettingsWidget::addSource(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) return;
    if (canonical.endsWith(QStringLiteral(".dictionary")) || canonical.endsWith(QStringLiteral(".ifo"))) {
        for (const auto &item : m_items) if (item.source == canonical) return;
        m_pendingRemoval.removeAll(canonical);
        const auto name = QFileInfo(canonical).completeBaseName();
        m_items.push_back({canonical, name, true, Store::suggestLabel(name)});
        m_queue.append(canonical);
        refreshTable();
        emit modified();
        importNext();
        return;
    }
    QDirIterator it(canonical, {QStringLiteral("*.dictionary"), QStringLiteral("*.ifo")}, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) addSource(it.next());
}

void SettingsWidget::importNext()
{
    if (m_process || m_queue.isEmpty()) return;
    m_importing = m_queue.takeFirst();
    m_outputBuffer.clear();
    const auto script = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("wordspring/import_dictionary.py"));
    const QString importer = script.isEmpty() ? QStringLiteral(WORDSPRING_SOURCE_IMPORTER) : script;
    m_process = new QProcess(this);
    const QString python = m_python->text().trimmed().isEmpty() ? QStringLiteral("python3") : m_python->text().trimmed();
    m_status->setText(i18nd("wordspring-runner", "Importing %1…", QFileInfo(m_importing).fileName()));
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_outputBuffer += m_process->readAllStandardOutput();
        while (m_outputBuffer.contains('\n')) {
            const auto line = m_outputBuffer.left(m_outputBuffer.indexOf('\n'));
            m_outputBuffer.remove(0, line.size() + 1);
            const auto object = QJsonDocument::fromJson(line).object();
            if (object.contains("progress")) m_status->setText(i18nd("wordspring-runner", "Imported %1 entries…", object.value("progress").toInt()));
            if (object.contains("name")) {
                for (auto &item : m_items) if (item.source == m_importing) {
                    const auto oldSuggested = Store::suggestLabel(item.name);
                    item.name = object.value("name").toString();
                    if (item.label == oldSuggested) item.label = Store::suggestLabel(item.name);
                }
                refreshTable();
                emit modified();
                m_status->setText(i18nd("wordspring-runner", "Imported %1 entries.", object.value("count").toInt()));
            }
        }
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
        if (code != 0) {
            const auto message = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            m_status->setText(message.isEmpty() ? i18nd("wordspring-runner", "Import failed. Check the Python executable and dictionary files.") : message);
        }
        m_process->deleteLater();
        m_process = nullptr;
        importNext();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process) return;
        m_status->setText(i18nd("wordspring-runner", "Could not start the selected Python executable."));
        m_process->deleteLater();
        m_process = nullptr;
        importNext();
    });
    m_process->start(python, {importer, QStringLiteral("--input"), m_importing, QStringLiteral("--output"), Store::databasePath(m_importing)});
}

void SettingsWidget::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    const auto selected = m_items[row].source;
    m_queue.removeAll(selected);
    if (selected == m_importing && m_process) m_process->kill();
    m_pendingRemoval.append(selected);
    m_items.removeAt(row);
    refreshTable();
    emit modified();
}

void SettingsWidget::moveSelected(int direction)
{
    const int row = m_table->currentRow();
    const int next = row + direction;
    if (row < 0 || next < 0 || next >= m_items.size()) return;
    m_items.swapItemsAt(row, next);
    refreshTable();
    m_table->selectRow(next);
    emit modified();
}
