#pragma once

#include <QDate>
#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace zenapps {

class Workspace {
public:
    explicit Workspace(QString rootOverride = {});
    ~Workspace();

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    bool initialize();
    [[nodiscard]] QString rootPath() const;
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] QString lastError() const;

    QVariantList notes(const QString& search = {}) const;
    QVariantMap note(qint64 id) const;
    qint64 saveNote(qint64 id, const QString& title, const QString& body);
    bool deleteNote(qint64 id);

    QVariantList tasks(const QString& filter = "all") const;
    qint64 createTask(const QString& title,
                      const QDate& dueDate,
                      int priority,
                      qint64 sourceNoteId = 0);
    bool setTaskCompleted(qint64 id, bool completed);
    bool deleteTask(qint64 id);

    QVariantList agenda(const QDate& date) const;
    qint64 createEvent(const QString& title,
                       const QDateTime& startsAt,
                       const QDateTime& endsAt,
                       bool allDay = false,
                       qint64 sourceTaskId = 0);
    bool deleteEvent(qint64 id);

    QVariantList reminders() const;
    QVariantList dueReminders(const QDateTime& now) const;
    qint64 createReminder(const QString& title,
                          const QDateTime& dueAt,
                          const QString& recurrence,
                          qint64 sourceNoteId = 0);
    bool completeReminder(qint64 id, const QDateTime& now = QDateTime::currentDateTime());
    bool snoozeReminder(qint64 id, int minutes);
    bool deleteReminder(qint64 id);

    void addTimeline(const QString& kind,
                     const QString& title,
                     const QDateTime& startedAt,
                     qint64 durationSeconds,
                     const QString& payload = {});

    void addCalculation(const QString& expression, const QString& result);
    QVariantList calculations(int limit = 30) const;

private:
    bool execSchema(const QString& statement);
    bool seedIfEmpty();
    void setError(const QString& message) const;
    QVariantList queryRows(const QString& sql,
                           const QVariantList& bindings = {}) const;

    QString rootPath_;
    QString databasePath_;
    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};

}  // namespace zenapps
