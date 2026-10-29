#include "settingswidget.h"
#include "userstate.h"
#include "core/error.h"
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

SettingsWidget::SettingsWidget(QWidget *parent) : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    outerLayout->addWidget(m_tabs);
    auto *dictionaryPage = new QWidget(m_tabs);
    m_tabs->addTab(dictionaryPage, i18nd("wordspring-runner", "Dictionaries"));
    auto *layout = new QVBoxLayout(dictionaryPage);
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

    auto *activityPage = new QWidget(m_tabs);
    m_tabs->addTab(activityPage, i18nd("wordspring-runner", "Bookmarks && History"));
    auto *activityLayout = new QVBoxLayout(activityPage);
    auto *filterRow = new QHBoxLayout;
    m_filter = new QLineEdit(activityPage);
    m_filter->setPlaceholderText(i18nd("wordspring-runner", "Filter bookmarks and history"));
    filterRow->addWidget(m_filter);
    auto *refresh = new QPushButton(i18nd("wordspring-runner", "Refresh"), activityPage);
    filterRow->addWidget(refresh);
    activityLayout->addLayout(filterRow);
    auto *activityTabs = new QTabWidget(activityPage);
    activityLayout->addWidget(activityTabs);

    auto *favoritesPage = new QWidget(activityTabs);
    auto *favoritesLayout = new QVBoxLayout(favoritesPage);
    m_favoritesList = new QListWidget(favoritesPage);
    favoritesLayout->addWidget(m_favoritesList);
    auto *favoriteButtons = new QHBoxLayout;
    auto *openFavorite = new QPushButton(i18nd("wordspring-runner", "Open"), favoritesPage);
    auto *removeFavorite = new QPushButton(i18nd("wordspring-runner", "Remove bookmark"), favoritesPage);
    favoriteButtons->addWidget(openFavorite);
    favoriteButtons->addWidget(removeFavorite);
    favoriteButtons->addStretch();
    favoritesLayout->addLayout(favoriteButtons);
    activityTabs->addTab(favoritesPage, i18nd("wordspring-runner", "Bookmarks"));

    auto *historyPage = new QWidget(activityTabs);
    auto *historyLayout = new QVBoxLayout(historyPage);
    m_historyList = new QListWidget(historyPage);
    historyLayout->addWidget(m_historyList);
    auto *historyButtons = new QHBoxLayout;
    auto *openHistory = new QPushButton(i18nd("wordspring-runner", "Open"), historyPage);
    auto *clearHistory = new QPushButton(i18nd("wordspring-runner", "Clear history"), historyPage);
    historyButtons->addWidget(openHistory);
    historyButtons->addWidget(clearHistory);
    historyButtons->addStretch();
    historyLayout->addLayout(historyButtons);
    activityTabs->addTab(historyPage, i18nd("wordspring-runner", "History"));

    connect(m_filter, &QLineEdit::textChanged, this, &SettingsWidget::refreshActivity);
    connect(refresh, &QPushButton::clicked, this, &SettingsWidget::refreshActivity);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) { if (index == 1) refreshActivity(); });
    connect(openFavorite, &QPushButton::clicked, this, [this] { openSaved(m_favoritesList); });
    connect(openHistory, &QPushButton::clicked, this, [this] { openSaved(m_historyList); });
    connect(m_favoritesList, &QListWidget::itemDoubleClicked, this, [this] { openSaved(m_favoritesList); });
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, [this] { openSaved(m_historyList); });
    connect(removeFavorite, &QPushButton::clicked, this, [this] {
        auto *item = m_favoritesList->currentItem();
        if (!item) return;
        Article article;
        article.source = item->data(Qt::UserRole).toString();
        article.id = item->data(Qt::UserRole + 1).toLongLong();
        article.headword = item->data(Qt::UserRole + 2).toString();
        UserState::setFavorite(article, false);
        refreshActivity();
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, i18nd("wordspring-runner", "Clear history"),
                i18nd("wordspring-runner", "Remove all lookup history? Bookmarks will be kept.")) != QMessageBox::Yes) return;
        UserState::clearHistory();
        refreshActivity();
    });
    load();
}

void SettingsWidget::showActivity()
{
    m_tabs->setCurrentIndex(1);
    refreshActivity();
}

void SettingsWidget::refreshActivity()
{
    const auto fill = [](QListWidget *list, const QVector<SavedEntry> &entries, bool showTime) {
        list->clear();
        for (const auto &entry : entries) {
            const auto time = showTime ? QStringLiteral("  ·  ") + QDateTime::fromMSecsSinceEpoch(entry.timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
            auto *item = new QListWidgetItem(entry.headword + QStringLiteral("  ·  ") + entry.dictionary + time, list);
            item->setToolTip(entry.source);
            item->setData(Qt::UserRole, entry.source);
            item->setData(Qt::UserRole + 1, entry.id);
            item->setData(Qt::UserRole + 2, entry.headword);
        }
    };
    fill(m_favoritesList, UserState::favorites(m_filter->text()), false);
    fill(m_historyList, UserState::history(m_filter->text()), true);
}

void SettingsWidget::openSaved(QListWidget *list)
{
    auto *item = list->currentItem();
    if (!item) return;
    const auto executable = QStandardPaths::findExecutable(QStringLiteral("wordspring"));
    if (executable.isEmpty()) return;
    QProcess::startDetached(executable, {QStringLiteral("--source"), item->data(Qt::UserRole).toString(),
        QStringLiteral("--entry"), item->data(Qt::UserRole + 1).toString()});
}

void SettingsWidget::load()
{
    m_trigger->setText(Store::trigger());
    m_python->setText(Store::pythonExecutable());
    m_items = Store::dictionaries();
    m_pendingRemoval.clear();
    refreshTable();
    refreshActivity();
}

void SettingsWidget::save()
{
    try {
    const auto trigger = m_trigger->text().trimmed().isEmpty() ? QStringLiteral("define") : m_trigger->text().trimmed();
    m_trigger->setText(trigger);
    Store::setTrigger(trigger);
    Store::setPythonExecutable(m_python->text().trimmed().isEmpty() ? QStringLiteral("python3") : m_python->text().trimmed());
    Store::setDictionaries(m_items);
    for (const auto &source : m_pendingRemoval) QFile::remove(Store::databasePath(source));
    m_pendingRemoval.clear();
    m_status->setText(i18nd("wordspring-runner", "Settings saved."));
    } catch (const WordspringError &error) {
        m_status->setText(error.message);
    }
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
    const auto bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/wordspring/import_dictionary.py");
    const QString importer = !script.isEmpty() ? script
        : QFileInfo::exists(bundled) ? QDir::cleanPath(bundled) : QStringLiteral(WORDSPRING_SOURCE_IMPORTER);
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
