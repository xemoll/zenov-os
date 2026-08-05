#include "ui/pages.hpp"

#include "core/expression.hpp"
#include "core/workspace.hpp"
#include "ui/style.hpp"

#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace zenapps {
namespace {

QString displayDateTime(const QVariant& value) {
    const QString text = value.toString();
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(text, Qt::ISODate);
    return parsed.isValid() ? parsed.toLocalTime().toString("ddd, d MMM  hh:mm") : text;
}

QString firstMeaningfulLine(const QString& body) {
    for (const QString& raw : body.split('\n')) {
        QString line = raw.trimmed();
        while (line.startsWith('#')) line = line.mid(1).trimmed();
        if (!line.isEmpty()) return line.left(100);
    }
    return {};
}

class NotesPage final : public QWidget {
public:
    explicit NotesPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);

        auto* headingRow = new QHBoxLayout;
        auto* headingText = new QVBoxLayout;
        headingText->addWidget(makeTitle("Notes", this));
        headingText->addWidget(makeSubtitle(
            "Local Markdown-like notes that can become tasks or reminders without copying text.", this));
        headingRow->addLayout(headingText, 1);
        newButton_ = makePrimaryButton("New note", this);
        headingRow->addWidget(newButton_);
        outer->addLayout(headingRow);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);

        auto* browser = makePanel(splitter);
        auto* browserLayout = new QVBoxLayout(browser);
        browserLayout->setContentsMargins(14, 14, 14, 14);
        browserLayout->setSpacing(10);
        search_ = new QLineEdit(browser);
        search_->setPlaceholderText("Search titles and content");
        noteList_ = new QListWidget(browser);
        noteList_->setSpacing(3);
        browserLayout->addWidget(search_);
        browserLayout->addWidget(noteList_, 1);

        auto* editor = makePanel(splitter);
        auto* editorLayout = new QVBoxLayout(editor);
        editorLayout->setContentsMargins(18, 16, 18, 16);
        editorLayout->setSpacing(11);
        title_ = new QLineEdit(editor);
        title_->setPlaceholderText("Note title");
        QFont titleFont = title_->font();
        titleFont.setPointSize(17);
        titleFont.setBold(true);
        title_->setFont(titleFont);
        body_ = new QPlainTextEdit(editor);
        body_->setPlaceholderText("Write here. Use [[links]] or move the note into action with the buttons below.");
        body_->setTabStopDistance(28.0);

        auto* actionRow = new QHBoxLayout;
        saveButton_ = makePrimaryButton("Save", editor);
        taskButton_ = makeQuietButton("Create task", editor);
        reminderButton_ = makeQuietButton("Create reminder", editor);
        deleteButton_ = makeQuietButton("Delete", editor);
        deleteButton_->setObjectName("danger");
        actionRow->addWidget(saveButton_);
        actionRow->addWidget(taskButton_);
        actionRow->addWidget(reminderButton_);
        actionRow->addStretch(1);
        actionRow->addWidget(deleteButton_);

        wordCount_ = makeSubtitle("0 words", editor);
        editorLayout->addWidget(title_);
        editorLayout->addWidget(body_, 1);
        editorLayout->addLayout(actionRow);
        editorLayout->addWidget(wordCount_);

        splitter->addWidget(browser);
        splitter->addWidget(editor);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({280, 760});
        outer->addWidget(splitter, 1);

        connect(newButton_, &QPushButton::clicked, this, [this] { createNew(); });
        connect(saveButton_, &QPushButton::clicked, this, [this] { saveCurrent(); });
        connect(deleteButton_, &QPushButton::clicked, this, [this] { deleteCurrent(); });
        connect(taskButton_, &QPushButton::clicked, this, [this] { createTaskFromNote(); });
        connect(reminderButton_, &QPushButton::clicked, this, [this] { createReminderFromNote(); });
        connect(search_, &QLineEdit::textChanged, this, [this] { refreshList(currentId_); });
        connect(noteList_, &QListWidget::currentItemChanged, this,
                [this](QListWidgetItem* current) { loadItem(current); });
        connect(body_, &QPlainTextEdit::textChanged, this, [this] { updateWordCount(); });

        refreshList();
    }

private:
    void refreshList(qint64 selectId = 0) {
        const QVariantList rows = workspace_->notes(search_->text());
        noteList_->blockSignals(true);
        noteList_->clear();
        QListWidgetItem* selection = nullptr;
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            auto* item = new QListWidgetItem(row.value("title").toString(), noteList_);
            item->setData(Qt::UserRole, row.value("id"));
            item->setToolTip(row.value("updated_at").toString());
            if (row.value("id").toLongLong() == selectId) selection = item;
        }
        noteList_->blockSignals(false);
        if (selection) noteList_->setCurrentItem(selection);
        else if (noteList_->count() > 0) noteList_->setCurrentRow(0);
        else createNew();
    }

    void loadItem(QListWidgetItem* item) {
        if (!item) return;
        currentId_ = item->data(Qt::UserRole).toLongLong();
        const QVariantMap row = workspace_->note(currentId_);
        title_->setText(row.value("title").toString());
        body_->setPlainText(row.value("body").toString());
        updateWordCount();
    }

    void createNew() {
        currentId_ = 0;
        noteList_->clearSelection();
        title_->clear();
        body_->clear();
        title_->setFocus();
    }

    void saveCurrent() {
        const qint64 saved = workspace_->saveNote(currentId_, title_->text(), body_->toPlainText());
        if (saved <= 0) {
            QMessageBox::critical(this, "Cannot save note", workspace_->lastError());
            return;
        }
        currentId_ = saved;
        refreshList(saved);
    }

    void deleteCurrent() {
        if (currentId_ <= 0) return;
        if (QMessageBox::question(this, "Delete note", "Delete this note and detach linked items?")
            != QMessageBox::Yes) return;
        if (!workspace_->deleteNote(currentId_)) {
            QMessageBox::critical(this, "Cannot delete note", workspace_->lastError());
            return;
        }
        currentId_ = 0;
        refreshList();
    }

    QString actionTitle() const {
        const QString fromTitle = title_->text().trimmed();
        if (!fromTitle.isEmpty()) return fromTitle;
        const QString fromBody = firstMeaningfulLine(body_->toPlainText());
        return fromBody.isEmpty() ? QString("Follow up") : fromBody;
    }

    qint64 ensureSaved() {
        if (currentId_ > 0) {
            saveCurrent();
            return currentId_;
        }
        const qint64 saved = workspace_->saveNote(0, title_->text(), body_->toPlainText());
        if (saved > 0) {
            currentId_ = saved;
            refreshList(saved);
        }
        return saved;
    }

    void createTaskFromNote() {
        const qint64 source = ensureSaved();
        if (source <= 0) return;
        if (workspace_->createTask(actionTitle(), QDate::currentDate().addDays(1), 2, source) <= 0) {
            QMessageBox::critical(this, "Cannot create task", workspace_->lastError());
            return;
        }
        QMessageBox::information(this, "Task created", "The task is linked to this note and appears in Tasks and Calendar.");
    }

    void createReminderFromNote() {
        const qint64 source = ensureSaved();
        if (source <= 0) return;
        if (workspace_->createReminder(actionTitle(), QDateTime::currentDateTime().addSecs(3600), "none", source) <= 0) {
            QMessageBox::critical(this, "Cannot create reminder", workspace_->lastError());
            return;
        }
        QMessageBox::information(this, "Reminder created", "The reminder is linked to this note and appears in Reminders and Calendar.");
    }

    void updateWordCount() {
        const QString simplified = body_->toPlainText().simplified();
        const int count = simplified.isEmpty() ? 0 : simplified.split(' ').size();
        wordCount_->setText(QString("%1 words  ·  local database: %2")
                                .arg(count)
                                .arg(workspace_->databasePath()));
    }

    Workspace* workspace_;
    qint64 currentId_ = 0;
    QLineEdit* search_ = nullptr;
    QListWidget* noteList_ = nullptr;
    QLineEdit* title_ = nullptr;
    QPlainTextEdit* body_ = nullptr;
    QLabel* wordCount_ = nullptr;
    QPushButton* newButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* taskButton_ = nullptr;
    QPushButton* reminderButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};

class TasksPage final : public QWidget {
public:
    explicit TasksPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);

        auto* headingRow = new QHBoxLayout;
        auto* headingText = new QVBoxLayout;
        headingText->addWidget(makeTitle("Tasks", this));
        headingText->addWidget(makeSubtitle(
            "Action list with priorities, due dates and direct links back to the note that created each task.", this));
        headingRow->addLayout(headingText, 1);
        filter_ = new QComboBox(this);
        filter_->addItem("All", "all");
        filter_->addItem("Open", "open");
        filter_->addItem("Today", "today");
        filter_->addItem("Done", "done");
        headingRow->addWidget(filter_);
        outer->addLayout(headingRow);

        auto* capture = makePanel(this);
        auto* captureLayout = new QHBoxLayout(capture);
        captureLayout->setContentsMargins(14, 12, 14, 12);
        title_ = new QLineEdit(capture);
        title_->setPlaceholderText("Add a task");
        due_ = new QDateEdit(QDate::currentDate().addDays(1), capture);
        due_->setCalendarPopup(true);
        due_->setDisplayFormat("yyyy-MM-dd");
        priority_ = new QComboBox(capture);
        priority_->addItem("Priority 1", 1);
        priority_->addItem("Priority 2", 2);
        priority_->addItem("Priority 3", 3);
        priority_->setCurrentIndex(1);
        addButton_ = makePrimaryButton("Add", capture);
        captureLayout->addWidget(title_, 1);
        captureLayout->addWidget(due_);
        captureLayout->addWidget(priority_);
        captureLayout->addWidget(addButton_);
        outer->addWidget(capture);

        auto* panel = makePanel(this);
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(12, 12, 12, 12);
        table_ = new QTableWidget(panel);
        table_->setColumnCount(5);
        table_->setHorizontalHeaderLabels({"Done", "Task", "Priority", "Due", "Source"});
        table_->verticalHeader()->setVisible(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

        auto* actionRow = new QHBoxLayout;
        completeButton_ = makeQuietButton("Toggle complete", panel);
        scheduleButton_ = makeQuietButton("Block 30 min in Calendar", panel);
        deleteButton_ = makeQuietButton("Delete", panel);
        deleteButton_->setObjectName("danger");
        actionRow->addWidget(completeButton_);
        actionRow->addWidget(scheduleButton_);
        actionRow->addStretch(1);
        actionRow->addWidget(deleteButton_);
        panelLayout->addWidget(table_, 1);
        panelLayout->addLayout(actionRow);
        outer->addWidget(panel, 1);

        connect(addButton_, &QPushButton::clicked, this, [this] { addTask(); });
        connect(title_, &QLineEdit::returnPressed, this, [this] { addTask(); });
        connect(filter_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
        connect(completeButton_, &QPushButton::clicked, this, [this] { toggleSelected(); });
        connect(deleteButton_, &QPushButton::clicked, this, [this] { deleteSelected(); });
        connect(scheduleButton_, &QPushButton::clicked, this, [this] { scheduleSelected(); });
        connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row) {
            table_->selectRow(row);
            toggleSelected();
        });
        refresh();
    }

private:
    QVariantMap selectedRow() const {
        const int row = table_->currentRow();
        if (row < 0) return {};
        return table_->item(row, 1)->data(Qt::UserRole).toMap();
    }

    void refresh() {
        const QVariantList rows = workspace_->tasks(filter_->currentData().toString());
        table_->setRowCount(0);
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            const int index = table_->rowCount();
            table_->insertRow(index);
            auto* done = new QTableWidgetItem(row.value("completed").toBool() ? "✓" : "");
            done->setTextAlignment(Qt::AlignCenter);
            auto* title = new QTableWidgetItem(row.value("title").toString());
            title->setData(Qt::UserRole, row);
            table_->setItem(index, 0, done);
            table_->setItem(index, 1, title);
            table_->setItem(index, 2, new QTableWidgetItem(QString("P%1").arg(row.value("priority").toInt())));
            table_->setItem(index, 3, new QTableWidgetItem(row.value("due_date").toString()));
            const QString source = row.value("source_note_title").toString();
            table_->setItem(index, 4, new QTableWidgetItem(source.isEmpty() ? "—" : source));
        }
        if (table_->rowCount() > 0) table_->selectRow(0);
    }

    void addTask() {
        if (workspace_->createTask(title_->text(), due_->date(), priority_->currentData().toInt()) <= 0) {
            QMessageBox::warning(this, "Cannot add task", workspace_->lastError());
            return;
        }
        title_->clear();
        refresh();
    }

    void toggleSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        if (!workspace_->setTaskCompleted(row.value("id").toLongLong(), !row.value("completed").toBool())) {
            QMessageBox::warning(this, "Cannot update task", workspace_->lastError());
        }
        refresh();
    }

    void deleteSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        if (!workspace_->deleteTask(row.value("id").toLongLong())) {
            QMessageBox::warning(this, "Cannot delete task", workspace_->lastError());
        }
        refresh();
    }

    void scheduleSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        QDate date = QDate::fromString(row.value("due_date").toString(), Qt::ISODate);
        if (!date.isValid()) date = QDate::currentDate();
        const QDateTime start(date, QTime(9, 0));
        if (workspace_->createEvent(row.value("title").toString(), start, start.addSecs(1800), false,
                                    row.value("id").toLongLong()) <= 0) {
            QMessageBox::warning(this, "Cannot schedule task", workspace_->lastError());
            return;
        }
        QMessageBox::information(this, "Calendar block created", "A 30-minute block was added to the task due date.");
    }

    Workspace* workspace_;
    QComboBox* filter_ = nullptr;
    QLineEdit* title_ = nullptr;
    QDateEdit* due_ = nullptr;
    QComboBox* priority_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* completeButton_ = nullptr;
    QPushButton* scheduleButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};

class CalendarPage final : public QWidget {
public:
    explicit CalendarPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);
        outer->addWidget(makeTitle("Calendar", this));
        outer->addWidget(makeSubtitle(
            "A single day view merges events, open tasks and reminders from the shared workspace.", this));

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);
        auto* calendarPanel = makePanel(splitter);
        auto* calendarLayout = new QVBoxLayout(calendarPanel);
        calendarLayout->setContentsMargins(12, 12, 12, 12);
        calendar_ = new QCalendarWidget(calendarPanel);
        calendar_->setGridVisible(false);
        calendarLayout->addWidget(calendar_, 1);

        auto* agendaPanel = makePanel(splitter);
        auto* agendaLayout = new QVBoxLayout(agendaPanel);
        agendaLayout->setContentsMargins(16, 14, 16, 14);
        selectedDate_ = makeTitle(calendar_->selectedDate().toString("dddd, d MMMM"), agendaPanel);
        selectedDate_->setStyleSheet("font-size: 19px; font-weight: 700;");
        agenda_ = new QListWidget(agendaPanel);
        agenda_->setSpacing(4);

        auto* form = new QHBoxLayout;
        eventTitle_ = new QLineEdit(agendaPanel);
        eventTitle_->setPlaceholderText("Add an event");
        eventTime_ = new QTimeEdit(QTime::currentTime().addSecs(3600), agendaPanel);
        eventTime_->setDisplayFormat("HH:mm");
        duration_ = new QSpinBox(agendaPanel);
        duration_->setRange(5, 480);
        duration_->setValue(30);
        duration_->setSuffix(" min");
        addButton_ = makePrimaryButton("Add", agendaPanel);
        form->addWidget(eventTitle_, 1);
        form->addWidget(eventTime_);
        form->addWidget(duration_);
        form->addWidget(addButton_);

        auto* actions = new QHBoxLayout;
        deleteButton_ = makeQuietButton("Delete selected event", agendaPanel);
        deleteButton_->setObjectName("danger");
        actions->addStretch(1);
        actions->addWidget(deleteButton_);

        agendaLayout->addWidget(selectedDate_);
        agendaLayout->addWidget(agenda_, 1);
        agendaLayout->addLayout(form);
        agendaLayout->addLayout(actions);
        splitter->addWidget(calendarPanel);
        splitter->addWidget(agendaPanel);
        splitter->setSizes({440, 620});
        outer->addWidget(splitter, 1);

        connect(calendar_, &QCalendarWidget::selectionChanged, this, [this] { refresh(); });
        connect(addButton_, &QPushButton::clicked, this, [this] { addEvent(); });
        connect(eventTitle_, &QLineEdit::returnPressed, this, [this] { addEvent(); });
        connect(deleteButton_, &QPushButton::clicked, this, [this] { deleteSelected(); });
        connect(agenda_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
            deleteButton_->setEnabled(item && item->data(Qt::UserRole + 1).toString() == "event");
        });
        refresh();
    }

private:
    void refresh() {
        const QDate date = calendar_->selectedDate();
        selectedDate_->setText(date.toString("dddd, d MMMM"));
        agenda_->clear();
        const QVariantList rows = workspace_->agenda(date);
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            const QString kind = row.value("kind").toString();
            QString prefix;
            if (kind == "event") prefix = "EVENT";
            else if (kind == "task") prefix = "TASK";
            else prefix = "REMINDER";
            QString moment = row.value("moment").toString();
            QDateTime parsed = QDateTime::fromString(moment, Qt::ISODateWithMs);
            if (parsed.isValid()) moment = parsed.toLocalTime().time().toString("HH:mm");
            else if (moment.size() >= 10) moment = "all day";
            auto* item = new QListWidgetItem(QString("%1   %2   %3")
                                                  .arg(prefix, -9)
                                                  .arg(moment, -8)
                                                  .arg(row.value("title").toString()), agenda_);
            item->setData(Qt::UserRole, row.value("id"));
            item->setData(Qt::UserRole + 1, kind);
        }
        if (agenda_->count() == 0) {
            auto* empty = new QListWidgetItem("No events, tasks or reminders for this day", agenda_);
            empty->setFlags(Qt::NoItemFlags);
        }
        deleteButton_->setEnabled(false);
    }

    void addEvent() {
        const QDateTime start(calendar_->selectedDate(), eventTime_->time());
        if (workspace_->createEvent(eventTitle_->text(), start,
                                    start.addSecs(duration_->value() * 60)) <= 0) {
            QMessageBox::warning(this, "Cannot add event", workspace_->lastError());
            return;
        }
        eventTitle_->clear();
        refresh();
    }

    void deleteSelected() {
        QListWidgetItem* item = agenda_->currentItem();
        if (!item || item->data(Qt::UserRole + 1).toString() != "event") return;
        if (!workspace_->deleteEvent(item->data(Qt::UserRole).toLongLong())) {
            QMessageBox::warning(this, "Cannot delete event", workspace_->lastError());
        }
        refresh();
    }

    Workspace* workspace_;
    QCalendarWidget* calendar_ = nullptr;
    QLabel* selectedDate_ = nullptr;
    QListWidget* agenda_ = nullptr;
    QLineEdit* eventTitle_ = nullptr;
    QTimeEdit* eventTime_ = nullptr;
    QSpinBox* duration_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
};

class ClockPage final : public QWidget {
public:
    explicit ClockPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);

        auto* headingRow = new QHBoxLayout;
        auto* headingText = new QVBoxLayout;
        headingText->addWidget(makeTitle("Clock", this));
        headingText->addWidget(makeSubtitle(
            "Clock, focus sessions, stopwatch and timer. Completed focus sessions are written to the shared timeline.", this));
        headingRow->addLayout(headingText, 1);
        now_ = new QLabel(this);
        now_->setObjectName("metric");
        now_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        headingRow->addWidget(now_);
        outer->addLayout(headingRow);

        tabs_ = new QTabWidget(this);
        tabs_->addTab(buildFocusTab(), "Focus");
        tabs_->addTab(buildStopwatchTab(), "Stopwatch");
        tabs_->addTab(buildTimerTab(), "Timer");
        outer->addWidget(tabs_, 1);

        tick_ = new QTimer(this);
        tick_->setInterval(100);
        connect(tick_, &QTimer::timeout, this, [this] { updateAll(); });
        tick_->start();
        updateAll();
    }

private:
    QWidget* buildFocusTab() {
        auto* panel = makePanel(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(16);
        focusLabel_ = new QLabel("25:00", panel);
        focusLabel_->setObjectName("metric");
        focusLabel_->setAlignment(Qt::AlignCenter);
        focusProgress_ = new QProgressBar(panel);
        focusProgress_->setRange(0, 1000);
        focusMinutes_ = new QSpinBox(panel);
        focusMinutes_->setRange(1, 180);
        focusMinutes_->setValue(25);
        focusMinutes_->setSuffix(" minutes");
        focusTitle_ = new QLineEdit(panel);
        focusTitle_->setPlaceholderText("What are you focusing on?");
        focusTitle_->setText("Deep work");
        auto* buttons = new QHBoxLayout;
        focusStart_ = makePrimaryButton("Start", panel);
        focusReset_ = makeQuietButton("Reset", panel);
        buttons->addStretch(1);
        buttons->addWidget(focusStart_);
        buttons->addWidget(focusReset_);
        buttons->addStretch(1);
        layout->addStretch(1);
        layout->addWidget(focusLabel_);
        layout->addWidget(focusProgress_);
        layout->addWidget(focusTitle_);
        layout->addWidget(focusMinutes_);
        layout->addLayout(buttons);
        layout->addStretch(1);
        connect(focusStart_, &QPushButton::clicked, this, [this] { toggleFocus(); });
        connect(focusReset_, &QPushButton::clicked, this, [this] { resetFocus(); });
        connect(focusMinutes_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { if (!focusRunning_) resetFocus(); });
        return panel;
    }

    QWidget* buildStopwatchTab() {
        auto* panel = makePanel(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(16);
        stopwatchLabel_ = new QLabel("00:00.0", panel);
        stopwatchLabel_->setObjectName("metric");
        stopwatchLabel_->setAlignment(Qt::AlignCenter);
        laps_ = new QListWidget(panel);
        auto* buttons = new QHBoxLayout;
        stopwatchStart_ = makePrimaryButton("Start", panel);
        auto* lap = makeQuietButton("Lap", panel);
        auto* reset = makeQuietButton("Reset", panel);
        buttons->addStretch(1);
        buttons->addWidget(stopwatchStart_);
        buttons->addWidget(lap);
        buttons->addWidget(reset);
        buttons->addStretch(1);
        layout->addWidget(stopwatchLabel_);
        layout->addLayout(buttons);
        layout->addWidget(laps_, 1);
        connect(stopwatchStart_, &QPushButton::clicked, this, [this] { toggleStopwatch(); });
        connect(lap, &QPushButton::clicked, this, [this] {
            laps_->insertItem(0, QString("Lap %1   %2").arg(laps_->count() + 1).arg(formatElapsed(stopwatchValue())));
        });
        connect(reset, &QPushButton::clicked, this, [this] {
            stopwatchRunning_ = false;
            stopwatchElapsedMs_ = 0;
            stopwatchStart_->setText("Start");
            laps_->clear();
            updateAll();
        });
        return panel;
    }

    QWidget* buildTimerTab() {
        auto* panel = makePanel(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(16);
        timerLabel_ = new QLabel("10:00", panel);
        timerLabel_->setObjectName("metric");
        timerLabel_->setAlignment(Qt::AlignCenter);
        timerProgress_ = new QProgressBar(panel);
        timerProgress_->setRange(0, 1000);
        timerMinutes_ = new QSpinBox(panel);
        timerMinutes_->setRange(0, 600);
        timerMinutes_->setValue(10);
        timerMinutes_->setSuffix(" min");
        timerSeconds_ = new QSpinBox(panel);
        timerSeconds_->setRange(0, 59);
        timerSeconds_->setSuffix(" sec");
        auto* durationRow = new QHBoxLayout;
        durationRow->addStretch(1);
        durationRow->addWidget(timerMinutes_);
        durationRow->addWidget(timerSeconds_);
        durationRow->addStretch(1);
        auto* buttons = new QHBoxLayout;
        timerStart_ = makePrimaryButton("Start", panel);
        auto* reset = makeQuietButton("Reset", panel);
        buttons->addStretch(1);
        buttons->addWidget(timerStart_);
        buttons->addWidget(reset);
        buttons->addStretch(1);
        layout->addStretch(1);
        layout->addWidget(timerLabel_);
        layout->addWidget(timerProgress_);
        layout->addLayout(durationRow);
        layout->addLayout(buttons);
        layout->addStretch(1);
        connect(timerStart_, &QPushButton::clicked, this, [this] { toggleTimer(); });
        connect(reset, &QPushButton::clicked, this, [this] { resetTimer(); });
        connect(timerMinutes_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { if (!timerRunning_) resetTimer(); });
        connect(timerSeconds_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { if (!timerRunning_) resetTimer(); });
        return panel;
    }

    static QString formatClock(qint64 milliseconds, bool tenths = false) {
        const qint64 totalSeconds = qMax<qint64>(0, milliseconds / 1000);
        const qint64 hours = totalSeconds / 3600;
        const qint64 minutes = (totalSeconds / 60) % 60;
        const qint64 seconds = totalSeconds % 60;
        if (tenths) {
            const qint64 tenth = (qMax<qint64>(0, milliseconds) / 100) % 10;
            return hours > 0
                ? QString("%1:%2:%3.%4").arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0')).arg(tenth)
                : QString("%1:%2.%3").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0')).arg(tenth);
        }
        return hours > 0
            ? QString("%1:%2:%3").arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'))
            : QString("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }

    static QString formatElapsed(qint64 milliseconds) { return formatClock(milliseconds, true); }

    qint64 stopwatchValue() const {
        return stopwatchElapsedMs_ + (stopwatchRunning_ ? stopwatchSlice_.elapsed() : 0);
    }

    void toggleFocus() {
        if (focusRunning_) {
            focusRemainingMs_ -= focusSlice_.elapsed();
            focusRemainingMs_ = qMax<qint64>(0, focusRemainingMs_);
            focusRunning_ = false;
            focusStart_->setText("Resume");
        } else {
            if (focusRemainingMs_ <= 0) resetFocus();
            focusRunning_ = true;
            focusSlice_.restart();
            if (!focusStarted_.isValid()) focusStarted_ = QDateTime::currentDateTime();
            focusStart_->setText("Pause");
        }
        updateAll();
    }

    void resetFocus() {
        focusRunning_ = false;
        focusTotalMs_ = static_cast<qint64>(focusMinutes_->value()) * 60 * 1000;
        focusRemainingMs_ = focusTotalMs_;
        focusStarted_ = {};
        focusStart_->setText("Start");
        updateAll();
    }

    void toggleStopwatch() {
        if (stopwatchRunning_) {
            stopwatchElapsedMs_ += stopwatchSlice_.elapsed();
            stopwatchRunning_ = false;
            stopwatchStart_->setText("Resume");
        } else {
            stopwatchRunning_ = true;
            stopwatchSlice_.restart();
            stopwatchStart_->setText("Pause");
        }
    }

    void toggleTimer() {
        if (timerRunning_) {
            timerRemainingMs_ -= timerSlice_.elapsed();
            timerRemainingMs_ = qMax<qint64>(0, timerRemainingMs_);
            timerRunning_ = false;
            timerStart_->setText("Resume");
        } else {
            if (timerRemainingMs_ <= 0) resetTimer();
            timerRunning_ = true;
            timerSlice_.restart();
            timerStart_->setText("Pause");
        }
        updateAll();
    }

    void resetTimer() {
        timerRunning_ = false;
        timerTotalMs_ = static_cast<qint64>(timerMinutes_->value() * 60 + timerSeconds_->value()) * 1000;
        timerRemainingMs_ = timerTotalMs_;
        timerStart_->setText("Start");
        updateAll();
    }

    void updateAll() {
        now_->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));

        qint64 focusDisplay = focusRemainingMs_;
        if (focusRunning_) focusDisplay -= focusSlice_.elapsed();
        if (focusDisplay <= 0 && focusRunning_) {
            focusRunning_ = false;
            focusDisplay = 0;
            const qint64 duration = focusTotalMs_ / 1000;
            workspace_->addTimeline("focus", focusTitle_->text().trimmed().isEmpty() ? "Focus session" : focusTitle_->text().trimmed(),
                                    focusStarted_.isValid() ? focusStarted_ : QDateTime::currentDateTime(), duration);
            focusStart_->setText("Start again");
            QApplication::beep();
        }
        focusLabel_->setText(formatClock(focusDisplay));
        focusProgress_->setValue(focusTotalMs_ > 0
            ? static_cast<int>((focusTotalMs_ - qMax<qint64>(0, focusDisplay)) * 1000 / focusTotalMs_) : 0);

        stopwatchLabel_->setText(formatElapsed(stopwatchValue()));

        qint64 timerDisplay = timerRemainingMs_;
        if (timerRunning_) timerDisplay -= timerSlice_.elapsed();
        if (timerDisplay <= 0 && timerRunning_) {
            timerRunning_ = false;
            timerDisplay = 0;
            timerStart_->setText("Start again");
            QApplication::beep();
        }
        timerLabel_->setText(formatClock(timerDisplay));
        timerProgress_->setValue(timerTotalMs_ > 0
            ? static_cast<int>((timerTotalMs_ - qMax<qint64>(0, timerDisplay)) * 1000 / timerTotalMs_) : 0);
    }

    Workspace* workspace_;
    QLabel* now_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTimer* tick_ = nullptr;

    QLabel* focusLabel_ = nullptr;
    QProgressBar* focusProgress_ = nullptr;
    QSpinBox* focusMinutes_ = nullptr;
    QLineEdit* focusTitle_ = nullptr;
    QPushButton* focusStart_ = nullptr;
    QPushButton* focusReset_ = nullptr;
    bool focusRunning_ = false;
    qint64 focusTotalMs_ = 25 * 60 * 1000;
    qint64 focusRemainingMs_ = 25 * 60 * 1000;
    QElapsedTimer focusSlice_;
    QDateTime focusStarted_;

    QLabel* stopwatchLabel_ = nullptr;
    QPushButton* stopwatchStart_ = nullptr;
    QListWidget* laps_ = nullptr;
    bool stopwatchRunning_ = false;
    qint64 stopwatchElapsedMs_ = 0;
    QElapsedTimer stopwatchSlice_;

    QLabel* timerLabel_ = nullptr;
    QProgressBar* timerProgress_ = nullptr;
    QSpinBox* timerMinutes_ = nullptr;
    QSpinBox* timerSeconds_ = nullptr;
    QPushButton* timerStart_ = nullptr;
    bool timerRunning_ = false;
    qint64 timerTotalMs_ = 10 * 60 * 1000;
    qint64 timerRemainingMs_ = 10 * 60 * 1000;
    QElapsedTimer timerSlice_;
};

class CalculatorPage final : public QWidget {
public:
    explicit CalculatorPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);
        outer->addWidget(makeTitle("Calculator", this));
        outer->addWidget(makeSubtitle(
            "A safe local expression calculator. Results can be copied or pinned directly into Notes.", this));

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);
        auto* calcPanel = makePanel(splitter);
        auto* calcLayout = new QVBoxLayout(calcPanel);
        calcLayout->setContentsMargins(20, 18, 20, 18);
        expression_ = new QLineEdit(calcPanel);
        expression_->setPlaceholderText("Type an expression, for example (42 + 8) / 5");
        result_ = new QLabel("0", calcPanel);
        result_->setObjectName("result");
        result_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        error_ = makeSubtitle("Supports +  -  ×  ÷  %  and parentheses", calcPanel);
        error_->setAlignment(Qt::AlignRight);
        calcLayout->addWidget(expression_);
        calcLayout->addWidget(result_);
        calcLayout->addWidget(error_);

        auto* keypad = new QGridLayout;
        const QStringList keys = {"7","8","9","/","4","5","6","*","1","2","3","-","0",".","%","+","(",")","C","="};
        for (int index = 0; index < keys.size(); ++index) {
            const QString key = keys.at(index);
            auto* button = key == "=" ? makePrimaryButton(key, calcPanel) : makeQuietButton(key, calcPanel);
            keypad->addWidget(button, index / 4, index % 4);
            connect(button, &QPushButton::clicked, this, [this, key] { pressKey(key); });
        }
        calcLayout->addLayout(keypad, 1);

        auto* actions = new QHBoxLayout;
        auto* copy = makeQuietButton("Copy result", calcPanel);
        auto* pin = makePrimaryButton("Pin to Notes", calcPanel);
        actions->addWidget(copy);
        actions->addStretch(1);
        actions->addWidget(pin);
        calcLayout->addLayout(actions);

        auto* historyPanel = makePanel(splitter);
        auto* historyLayout = new QVBoxLayout(historyPanel);
        historyLayout->setContentsMargins(16, 14, 16, 14);
        historyLayout->addWidget(makeTitle("History", historyPanel));
        history_ = new QListWidget(historyPanel);
        historyLayout->addWidget(history_, 1);
        splitter->addWidget(calcPanel);
        splitter->addWidget(historyPanel);
        splitter->setSizes({700, 350});
        outer->addWidget(splitter, 1);

        connect(expression_, &QLineEdit::returnPressed, this, [this] { evaluate(); });
        connect(copy, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(result_->text());
        });
        connect(pin, &QPushButton::clicked, this, [this] {
            if (!lastResult_.ok) evaluate();
            if (!lastResult_.ok) return;
            const QString body = QString("# Calculation\n\n`%1`\n\n**Result:** %2\n")
                                     .arg(expression_->text(), lastResult_.formatted);
            const qint64 id = workspace_->saveNote(0, "Calculation", body);
            if (id <= 0) QMessageBox::warning(this, "Cannot create note", workspace_->lastError());
            else QMessageBox::information(this, "Pinned", "The calculation was saved as a note.");
        });
        connect(history_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            expression_->setText(item->data(Qt::UserRole).toString());
            evaluate();
        });
        refreshHistory();
    }

private:
    void pressKey(const QString& key) {
        if (key == "C") {
            expression_->clear();
            result_->setText("0");
            error_->setText("Supports +  -  ×  ÷  %  and parentheses");
            lastResult_ = {};
        } else if (key == "=") {
            evaluate();
        } else {
            expression_->insert(key);
            expression_->setFocus();
        }
    }

    void evaluate() {
        lastResult_ = evaluateExpression(expression_->text());
        if (!lastResult_.ok) {
            result_->setText("—");
            error_->setText(lastResult_.error);
            error_->setStyleSheet("color: #ff9f9f;");
            return;
        }
        result_->setText(lastResult_.formatted);
        error_->setText("Press Enter to recalculate");
        error_->setStyleSheet({});
        workspace_->addCalculation(expression_->text(), lastResult_.formatted);
        refreshHistory();
    }

    void refreshHistory() {
        history_->clear();
        for (const QVariant& value : workspace_->calculations()) {
            const QVariantMap row = value.toMap();
            auto* item = new QListWidgetItem(QString("%1  =  %2")
                                                  .arg(row.value("expression").toString(), row.value("result").toString()), history_);
            item->setData(Qt::UserRole, row.value("expression"));
        }
    }

    Workspace* workspace_;
    QLineEdit* expression_ = nullptr;
    QLabel* result_ = nullptr;
    QLabel* error_ = nullptr;
    QListWidget* history_ = nullptr;
    ExpressionResult lastResult_;
};

class RemindersPage final : public QWidget {
public:
    explicit RemindersPage(Workspace* workspace, QWidget* parent = nullptr)
        : QWidget(parent), workspace_(workspace) {
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 24);
        outer->setSpacing(16);
        outer->addWidget(makeTitle("Reminders", this));
        outer->addWidget(makeSubtitle(
            "One-shot or recurring reminders with snooze. Due items are also visible in Calendar.", this));

        dueBanner_ = new QLabel(this);
        dueBanner_->setObjectName("badge");
        dueBanner_->setVisible(false);
        outer->addWidget(dueBanner_);

        auto* capture = makePanel(this);
        auto* captureLayout = new QHBoxLayout(capture);
        captureLayout->setContentsMargins(14, 12, 14, 12);
        title_ = new QLineEdit(capture);
        title_->setPlaceholderText("What should you remember?");
        due_ = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), capture);
        due_->setCalendarPopup(true);
        due_->setDisplayFormat("yyyy-MM-dd  HH:mm");
        recurrence_ = new QComboBox(capture);
        recurrence_->addItem("Once", "none");
        recurrence_->addItem("Daily", "daily");
        recurrence_->addItem("Weekly", "weekly");
        recurrence_->addItem("Monthly", "monthly");
        addButton_ = makePrimaryButton("Add", capture);
        captureLayout->addWidget(title_, 1);
        captureLayout->addWidget(due_);
        captureLayout->addWidget(recurrence_);
        captureLayout->addWidget(addButton_);
        outer->addWidget(capture);

        auto* panel = makePanel(this);
        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(12, 12, 12, 12);
        table_ = new QTableWidget(panel);
        table_->setColumnCount(4);
        table_->setHorizontalHeaderLabels({"Reminder", "Due", "Repeat", "State"});
        table_->verticalHeader()->setVisible(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        auto* actions = new QHBoxLayout;
        complete_ = makePrimaryButton("Complete", panel);
        snooze_ = makeQuietButton("Snooze 10 min", panel);
        delete_ = makeQuietButton("Delete", panel);
        delete_->setObjectName("danger");
        actions->addWidget(complete_);
        actions->addWidget(snooze_);
        actions->addStretch(1);
        actions->addWidget(delete_);
        panelLayout->addWidget(table_, 1);
        panelLayout->addLayout(actions);
        outer->addWidget(panel, 1);

        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            tray_ = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation), this);
            tray_->setToolTip("Reminders");
            tray_->show();
        }
        poll_ = new QTimer(this);
        poll_->setInterval(10'000);
        connect(poll_, &QTimer::timeout, this, [this] { pollDue(); });
        poll_->start();

        connect(addButton_, &QPushButton::clicked, this, [this] { addReminder(); });
        connect(title_, &QLineEdit::returnPressed, this, [this] { addReminder(); });
        connect(complete_, &QPushButton::clicked, this, [this] { completeSelected(); });
        connect(snooze_, &QPushButton::clicked, this, [this] { snoozeSelected(); });
        connect(delete_, &QPushButton::clicked, this, [this] { deleteSelected(); });
        refresh();
        pollDue();
    }

private:
    QVariantMap selectedRow() const {
        const int row = table_->currentRow();
        if (row < 0) return {};
        return table_->item(row, 0)->data(Qt::UserRole).toMap();
    }

    void refresh() {
        table_->setRowCount(0);
        for (const QVariant& value : workspace_->reminders()) {
            const QVariantMap row = value.toMap();
            const int index = table_->rowCount();
            table_->insertRow(index);
            auto* titleItem = new QTableWidgetItem(row.value("title").toString());
            titleItem->setData(Qt::UserRole, row);
            table_->setItem(index, 0, titleItem);
            const QVariant effectiveDue = row.value("snoozed_until").toString().isEmpty()
                ? row.value("due_at") : row.value("snoozed_until");
            table_->setItem(index, 1, new QTableWidgetItem(displayDateTime(effectiveDue)));
            table_->setItem(index, 2, new QTableWidgetItem(row.value("recurrence").toString()));
            table_->setItem(index, 3, new QTableWidgetItem(row.value("completed").toBool() ? "Done" : "Open"));
        }
        if (table_->rowCount() > 0) table_->selectRow(0);
    }

    void addReminder() {
        if (workspace_->createReminder(title_->text(), due_->dateTime(), recurrence_->currentData().toString()) <= 0) {
            QMessageBox::warning(this, "Cannot add reminder", workspace_->lastError());
            return;
        }
        title_->clear();
        due_->setDateTime(QDateTime::currentDateTime().addSecs(3600));
        refresh();
        pollDue();
    }

    void completeSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        if (!workspace_->completeReminder(row.value("id").toLongLong())) {
            QMessageBox::warning(this, "Cannot complete reminder", workspace_->lastError());
        }
        notified_.remove(row.value("id").toLongLong());
        refresh();
        pollDue();
    }

    void snoozeSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        if (!workspace_->snoozeReminder(row.value("id").toLongLong(), 10)) {
            QMessageBox::warning(this, "Cannot snooze reminder", workspace_->lastError());
        }
        notified_.remove(row.value("id").toLongLong());
        refresh();
        pollDue();
    }

    void deleteSelected() {
        const QVariantMap row = selectedRow();
        if (row.isEmpty()) return;
        if (!workspace_->deleteReminder(row.value("id").toLongLong())) {
            QMessageBox::warning(this, "Cannot delete reminder", workspace_->lastError());
        }
        notified_.remove(row.value("id").toLongLong());
        refresh();
        pollDue();
    }

    void pollDue() {
        const QVariantList dueRows = workspace_->dueReminders(QDateTime::currentDateTime());
        if (dueRows.isEmpty()) {
            dueBanner_->setVisible(false);
            return;
        }
        const QVariantMap first = dueRows.first().toMap();
        dueBanner_->setText(QString("Due now: %1").arg(first.value("title").toString()));
        dueBanner_->setVisible(true);
        for (const QVariant& value : dueRows) {
            const QVariantMap row = value.toMap();
            const qint64 id = row.value("id").toLongLong();
            if (notified_.contains(id)) continue;
            notified_.insert(id);
            if (tray_) {
                tray_->showMessage("Reminder", row.value("title").toString(),
                                   QSystemTrayIcon::Information, 8000);
            }
        }
    }

    Workspace* workspace_;
    QLabel* dueBanner_ = nullptr;
    QLineEdit* title_ = nullptr;
    QDateTimeEdit* due_ = nullptr;
    QComboBox* recurrence_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* complete_ = nullptr;
    QPushButton* snooze_ = nullptr;
    QPushButton* delete_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QTimer* poll_ = nullptr;
    QSet<qint64> notified_;
};

}  // namespace

AppKind appKindFromString(const QString& value) {
    const QString kind = value.trimmed().toLower();
    if (kind == "tasks") return AppKind::Tasks;
    if (kind == "calendar") return AppKind::Calendar;
    if (kind == "clock") return AppKind::Clock;
    if (kind == "calculator") return AppKind::Calculator;
    if (kind == "reminders") return AppKind::Reminders;
    return AppKind::Notes;
}

QString appTitle(AppKind kind) {
    switch (kind) {
    case AppKind::Notes: return "Notes";
    case AppKind::Tasks: return "Tasks";
    case AppKind::Calendar: return "Calendar";
    case AppKind::Clock: return "Clock";
    case AppKind::Calculator: return "Calculator";
    case AppKind::Reminders: return "Reminders";
    }
    return "Notes";
}

QString appSubtitle(AppKind kind) {
    switch (kind) {
    case AppKind::Notes: return "Local notes and connected actions";
    case AppKind::Tasks: return "Priorities, due dates and source links";
    case AppKind::Calendar: return "Events, tasks and reminders in one day view";
    case AppKind::Clock: return "Focus, stopwatch and timer";
    case AppKind::Calculator: return "Safe expressions and reusable results";
    case AppKind::Reminders: return "Recurring reminders and snooze";
    }
    return {};
}

QWidget* createPage(AppKind kind, Workspace* workspace, QWidget* parent) {
    switch (kind) {
    case AppKind::Notes: return new NotesPage(workspace, parent);
    case AppKind::Tasks: return new TasksPage(workspace, parent);
    case AppKind::Calendar: return new CalendarPage(workspace, parent);
    case AppKind::Clock: return new ClockPage(workspace, parent);
    case AppKind::Calculator: return new CalculatorPage(workspace, parent);
    case AppKind::Reminders: return new RemindersPage(workspace, parent);
    }
    return new NotesPage(workspace, parent);
}

}  // namespace zenapps
