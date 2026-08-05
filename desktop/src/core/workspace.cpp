#include "core/workspace.hpp"

#include "core/recurrence.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QStandardPaths>
#include <QUuid>

namespace zenapps {
namespace {

QString timestamp(const QDateTime& value) {
    return value.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime parseTimestamp(const QVariant& value) {
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toLocalTime();
}

QString effectiveRoot(const QString& overridePath) {
    if (!overridePath.trimmed().isEmpty()) {
        return QDir::cleanPath(overridePath);
    }
    const QString environment = qEnvironmentVariable("ZEN_APPS_DATA_DIR");
    if (!environment.trimmed().isEmpty()) {
        return QDir::cleanPath(environment);
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("ZenApps");
}

}  // namespace

Workspace::Workspace(QString rootOverride)
    : rootPath_(effectiveRoot(rootOverride)),
      databasePath_(QDir(rootPath_).filePath("workspace.sqlite")),
      connectionName_(QString("zenapps-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

Workspace::~Workspace() {
    if (database_.isValid()) {
        database_.close();
        database_ = {};
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

bool Workspace::initialize() {
    if (!QDir().mkpath(rootPath_)) {
        setError(QString("Cannot create data directory: %1").arg(rootPath_));
        return false;
    }

    database_ = QSqlDatabase::addDatabase("QSQLITE", connectionName_);
    database_.setDatabaseName(databasePath_);
    database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (!database_.open()) {
        setError(database_.lastError().text());
        return false;
    }

    const QStringList schema = {
        "PRAGMA foreign_keys = ON",
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = NORMAL",
        "CREATE TABLE IF NOT EXISTS notes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "title TEXT NOT NULL, body TEXT NOT NULL DEFAULT '',"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
        "CREATE INDEX IF NOT EXISTS notes_updated_idx ON notes(updated_at DESC)",
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL,"
        "details TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 2,"
        "due_date TEXT, completed INTEGER NOT NULL DEFAULT 0,"
        "source_note_id INTEGER, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "FOREIGN KEY(source_note_id) REFERENCES notes(id) ON DELETE SET NULL)",
        "CREATE INDEX IF NOT EXISTS tasks_due_idx ON tasks(completed, due_date, priority)",
        "CREATE TABLE IF NOT EXISTS events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL,"
        "starts_at TEXT NOT NULL, ends_at TEXT NOT NULL, all_day INTEGER NOT NULL DEFAULT 0,"
        "source_task_id INTEGER, created_at TEXT NOT NULL,"
        "FOREIGN KEY(source_task_id) REFERENCES tasks(id) ON DELETE SET NULL)",
        "CREATE INDEX IF NOT EXISTS events_start_idx ON events(starts_at)",
        "CREATE TABLE IF NOT EXISTS reminders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL,"
        "due_at TEXT NOT NULL, recurrence TEXT NOT NULL DEFAULT 'none',"
        "snoozed_until TEXT, completed INTEGER NOT NULL DEFAULT 0, source_note_id INTEGER,"
        "created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        "FOREIGN KEY(source_note_id) REFERENCES notes(id) ON DELETE SET NULL)",
        "CREATE INDEX IF NOT EXISTS reminders_due_idx ON reminders(completed, due_at)",
        "CREATE TABLE IF NOT EXISTS timeline ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, title TEXT NOT NULL,"
        "started_at TEXT NOT NULL, duration_seconds INTEGER NOT NULL, payload TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS calculations ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, expression TEXT NOT NULL,"
        "result TEXT NOT NULL, created_at TEXT NOT NULL)"
    };

    for (const QString& statement : schema) {
        if (!execSchema(statement)) {
            return false;
        }
    }
    return seedIfEmpty();
}

QString Workspace::rootPath() const { return rootPath_; }
QString Workspace::databasePath() const { return databasePath_; }
QString Workspace::lastError() const { return lastError_; }

bool Workspace::execSchema(const QString& statement) {
    QSqlQuery query(database_);
    if (!query.exec(statement)) {
        setError(query.lastError().text());
        return false;
    }
    return true;
}

bool Workspace::seedIfEmpty() {
    QSqlQuery count(database_);
    if (!count.exec("SELECT COUNT(*) FROM notes") || !count.next()) {
        setError(count.lastError().text());
        return false;
    }
    if (count.value(0).toLongLong() > 0) {
        return true;
    }

    const qint64 noteId = saveNote(0, "Start here",
        "# Your local workspace\n\n"
        "Notes, tasks, calendar events, focus sessions and reminders share one local database.\n\n"
        "Use **Create task** or **Create reminder** inside Notes to move an idea into action without copying it.\n");
    if (noteId <= 0) return false;

    if (createTask("Explore the standalone apps", QDate::currentDate().addDays(1), 1, noteId) <= 0) {
        return false;
    }
    if (createEvent("Review the workspace",
                    QDateTime(QDate::currentDate(), QTime(14, 0)),
                    QDateTime(QDate::currentDate(), QTime(14, 30))) <= 0) {
        return false;
    }
    return createReminder("Take a short break",
                          QDateTime::currentDateTime().addSecs(3600),
                          "none",
                          noteId) > 0;
}

QVariantList Workspace::queryRows(const QString& sql, const QVariantList& bindings) const {
    QVariantList rows;
    QSqlQuery query(database_);
    if (!query.prepare(sql)) {
        setError(query.lastError().text());
        return rows;
    }
    for (qsizetype index = 0; index < bindings.size(); ++index) {
        query.bindValue(static_cast<int>(index), bindings.at(index));
    }
    if (!query.exec()) {
        setError(query.lastError().text());
        return rows;
    }
    const QSqlRecord record = query.record();
    while (query.next()) {
        QVariantMap row;
        for (int column = 0; column < record.count(); ++column) {
            row.insert(record.fieldName(column), query.value(column));
        }
        rows.push_back(row);
    }
    return rows;
}

QVariantList Workspace::notes(const QString& search) const {
    const QString needle = search.trimmed();
    if (needle.isEmpty()) {
        return queryRows("SELECT id, title, body, created_at, updated_at FROM notes ORDER BY updated_at DESC");
    }
    return queryRows(
        "SELECT id, title, body, created_at, updated_at FROM notes "
        "WHERE title LIKE ? OR body LIKE ? ORDER BY updated_at DESC",
        {QString("%%1%").arg(needle), QString("%%1%").arg(needle)});
}

QVariantMap Workspace::note(qint64 id) const {
    const QVariantList rows = queryRows(
        "SELECT id, title, body, created_at, updated_at FROM notes WHERE id = ?", {id});
    return rows.isEmpty() ? QVariantMap{} : rows.first().toMap();
}

qint64 Workspace::saveNote(qint64 id, const QString& title, const QString& body) {
    const QString cleanTitle = title.trimmed().isEmpty() ? QString("Untitled note") : title.trimmed();
    const QString now = timestamp(QDateTime::currentDateTime());
    QSqlQuery query(database_);
    if (id <= 0) {
        query.prepare("INSERT INTO notes(title, body, created_at, updated_at) VALUES(?, ?, ?, ?)");
        query.addBindValue(cleanTitle);
        query.addBindValue(body);
        query.addBindValue(now);
        query.addBindValue(now);
    } else {
        query.prepare("UPDATE notes SET title = ?, body = ?, updated_at = ? WHERE id = ?");
        query.addBindValue(cleanTitle);
        query.addBindValue(body);
        query.addBindValue(now);
        query.addBindValue(id);
    }
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return id > 0 ? id : query.lastInsertId().toLongLong();
}

bool Workspace::deleteNote(qint64 id) {
    QSqlQuery query(database_);
    query.prepare("DELETE FROM notes WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

QVariantList Workspace::tasks(const QString& filter) const {
    QString condition;
    QVariantList bindings;
    if (filter == "open") condition = "WHERE completed = 0";
    else if (filter == "done") condition = "WHERE completed = 1";
    else if (filter == "today") {
        condition = "WHERE completed = 0 AND due_date = ?";
        bindings << QDate::currentDate().toString(Qt::ISODate);
    }
    return queryRows(
        "SELECT tasks.id, tasks.title, tasks.details, tasks.priority, tasks.due_date, "
        "tasks.completed, tasks.source_note_id, notes.title AS source_note_title "
        "FROM tasks LEFT JOIN notes ON notes.id = tasks.source_note_id " + condition +
        " ORDER BY completed ASC, CASE WHEN due_date IS NULL THEN 1 ELSE 0 END, "
        "due_date ASC, priority ASC, tasks.updated_at DESC",
        bindings);
}

qint64 Workspace::createTask(const QString& title,
                             const QDate& dueDate,
                             int priority,
                             qint64 sourceNoteId) {
    if (title.trimmed().isEmpty()) {
        setError("Task title cannot be empty");
        return 0;
    }
    const QString now = timestamp(QDateTime::currentDateTime());
    QSqlQuery query(database_);
    query.prepare("INSERT INTO tasks(title, priority, due_date, completed, source_note_id, created_at, updated_at) "
                  "VALUES(?, ?, ?, 0, ?, ?, ?)");
    query.addBindValue(title.trimmed());
    query.addBindValue(qBound(1, priority, 3));
    query.addBindValue(dueDate.isValid() ? dueDate.toString(Qt::ISODate) : QVariant{});
    query.addBindValue(sourceNoteId > 0 ? QVariant(sourceNoteId) : QVariant{});
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool Workspace::setTaskCompleted(qint64 id, bool completed) {
    QSqlQuery query(database_);
    query.prepare("UPDATE tasks SET completed = ?, updated_at = ? WHERE id = ?");
    query.addBindValue(completed ? 1 : 0);
    query.addBindValue(timestamp(QDateTime::currentDateTime()));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool Workspace::deleteTask(qint64 id) {
    QSqlQuery query(database_);
    query.prepare("DELETE FROM tasks WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

QVariantList Workspace::agenda(const QDate& date) const {
    if (!date.isValid()) return {};
    const QString day = date.toString(Qt::ISODate);
    QVariantList result;

    for (QVariant item : queryRows(
             "SELECT id, 'event' AS kind, title, starts_at AS moment, all_day, source_task_id AS source_id "
             "FROM events WHERE substr(starts_at, 1, 10) = ? ORDER BY starts_at", {day})) {
        result.push_back(item);
    }
    for (QVariant item : queryRows(
             "SELECT id, 'task' AS kind, title, due_date AS moment, 1 AS all_day, source_note_id AS source_id "
             "FROM tasks WHERE due_date = ? AND completed = 0 ORDER BY priority, title", {day})) {
        result.push_back(item);
    }
    for (QVariant item : queryRows(
             "SELECT id, 'reminder' AS kind, title, COALESCE(snoozed_until, due_at) AS moment, "
             "0 AS all_day, source_note_id AS source_id FROM reminders "
             "WHERE substr(COALESCE(snoozed_until, due_at), 1, 10) = ? AND completed = 0 "
             "ORDER BY COALESCE(snoozed_until, due_at)", {day})) {
        result.push_back(item);
    }
    return result;
}

qint64 Workspace::createEvent(const QString& title,
                              const QDateTime& startsAt,
                              const QDateTime& endsAt,
                              bool allDay,
                              qint64 sourceTaskId) {
    if (title.trimmed().isEmpty() || !startsAt.isValid() || !endsAt.isValid() || endsAt < startsAt) {
        setError("Event requires a title and a valid time range");
        return 0;
    }
    QSqlQuery query(database_);
    query.prepare("INSERT INTO events(title, starts_at, ends_at, all_day, source_task_id, created_at) "
                  "VALUES(?, ?, ?, ?, ?, ?)");
    query.addBindValue(title.trimmed());
    query.addBindValue(timestamp(startsAt));
    query.addBindValue(timestamp(endsAt));
    query.addBindValue(allDay ? 1 : 0);
    query.addBindValue(sourceTaskId > 0 ? QVariant(sourceTaskId) : QVariant{});
    query.addBindValue(timestamp(QDateTime::currentDateTime()));
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool Workspace::deleteEvent(qint64 id) {
    QSqlQuery query(database_);
    query.prepare("DELETE FROM events WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

QVariantList Workspace::reminders() const {
    return queryRows(
        "SELECT id, title, due_at, recurrence, snoozed_until, completed, source_note_id "
        "FROM reminders ORDER BY completed ASC, COALESCE(snoozed_until, due_at) ASC");
}

QVariantList Workspace::dueReminders(const QDateTime& now) const {
    return queryRows(
        "SELECT id, title, due_at, recurrence, snoozed_until FROM reminders "
        "WHERE completed = 0 AND COALESCE(snoozed_until, due_at) <= ? "
        "ORDER BY COALESCE(snoozed_until, due_at)", {timestamp(now)});
}

qint64 Workspace::createReminder(const QString& title,
                                 const QDateTime& dueAt,
                                 const QString& recurrence,
                                 qint64 sourceNoteId) {
    if (title.trimmed().isEmpty() || !dueAt.isValid()) {
        setError("Reminder requires a title and valid due time");
        return 0;
    }
    static const QStringList allowed = {"none", "daily", "weekly", "monthly"};
    const QString cleanRule = recurrence.trimmed().toLower();
    if (!allowed.contains(cleanRule)) {
        setError("Unsupported recurrence rule");
        return 0;
    }
    const QString now = timestamp(QDateTime::currentDateTime());
    QSqlQuery query(database_);
    query.prepare("INSERT INTO reminders(title, due_at, recurrence, completed, source_note_id, created_at, updated_at) "
                  "VALUES(?, ?, ?, 0, ?, ?, ?)");
    query.addBindValue(title.trimmed());
    query.addBindValue(timestamp(dueAt));
    query.addBindValue(cleanRule);
    query.addBindValue(sourceNoteId > 0 ? QVariant(sourceNoteId) : QVariant{});
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        setError(query.lastError().text());
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

bool Workspace::completeReminder(qint64 id, const QDateTime& now) {
    const QVariantList rows = queryRows(
        "SELECT due_at, recurrence FROM reminders WHERE id = ?", {id});
    if (rows.isEmpty()) return false;
    const QVariantMap row = rows.first().toMap();
    const QDateTime due = parseTimestamp(row.value("due_at"));
    const QString recurrence = row.value("recurrence").toString();
    const QDateTime next = nextOccurrence(due, recurrence, now);

    QSqlQuery query(database_);
    if (next.isValid()) {
        query.prepare("UPDATE reminders SET due_at = ?, snoozed_until = NULL, completed = 0, updated_at = ? WHERE id = ?");
        query.addBindValue(timestamp(next));
    } else {
        query.prepare("UPDATE reminders SET completed = 1, snoozed_until = NULL, updated_at = ? WHERE id = ?");
    }
    query.addBindValue(timestamp(QDateTime::currentDateTime()));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool Workspace::snoozeReminder(qint64 id, int minutes) {
    QSqlQuery query(database_);
    query.prepare("UPDATE reminders SET snoozed_until = ?, updated_at = ? WHERE id = ? AND completed = 0");
    query.addBindValue(timestamp(QDateTime::currentDateTime().addSecs(qMax(1, minutes) * 60)));
    query.addBindValue(timestamp(QDateTime::currentDateTime()));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool Workspace::deleteReminder(qint64 id) {
    QSqlQuery query(database_);
    query.prepare("DELETE FROM reminders WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

void Workspace::addTimeline(const QString& kind,
                            const QString& title,
                            const QDateTime& startedAt,
                            qint64 durationSeconds,
                            const QString& payload) {
    QSqlQuery query(database_);
    query.prepare("INSERT INTO timeline(kind, title, started_at, duration_seconds, payload) VALUES(?, ?, ?, ?, ?)");
    query.addBindValue(kind);
    query.addBindValue(title);
    query.addBindValue(timestamp(startedAt));
    query.addBindValue(qMax<qint64>(0, durationSeconds));
    query.addBindValue(payload);
    if (!query.exec()) setError(query.lastError().text());
}

void Workspace::addCalculation(const QString& expression, const QString& result) {
    QSqlQuery query(database_);
    query.prepare("INSERT INTO calculations(expression, result, created_at) VALUES(?, ?, ?)");
    query.addBindValue(expression);
    query.addBindValue(result);
    query.addBindValue(timestamp(QDateTime::currentDateTime()));
    if (!query.exec()) setError(query.lastError().text());
}

QVariantList Workspace::calculations(int limit) const {
    return queryRows(
        QString("SELECT id, expression, result, created_at FROM calculations ORDER BY id DESC LIMIT %1")
            .arg(qBound(1, limit, 200)));
}

void Workspace::setError(const QString& message) const {
    lastError_ = message;
}

}  // namespace zenapps
