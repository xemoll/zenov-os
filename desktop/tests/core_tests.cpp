#include "core/expression.hpp"
#include "core/recurrence.hpp"
#include "core/workspace.hpp"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void expressionPrecedence() {
        const zenapps::ExpressionResult result = zenapps::evaluateExpression("(42 + 8) / 5 + 3 * 2");
        QVERIFY(result.ok);
        QCOMPARE(result.value, 16.0);
        QCOMPARE(result.formatted, QString("16"));
    }

    void expressionRejectsUnsafeInput() {
        QVERIFY(!zenapps::evaluateExpression("1 / 0").ok);
        QVERIFY(!zenapps::evaluateExpression("2 + system('x')").ok);
        QVERIFY(!zenapps::evaluateExpression("(1 + 2").ok);
    }

    void monthlyRecurrencePreservesAnchor() {
        const QDateTime scheduled(QDate(2026, 1, 31), QTime(18, 0));
        QCOMPARE(zenapps::nextOccurrence(scheduled, "monthly", QDateTime(QDate(2026, 2, 1), QTime(0, 0))),
                 QDateTime(QDate(2026, 2, 28), QTime(18, 0)));
        QCOMPARE(zenapps::nextOccurrence(scheduled, "monthly", QDateTime(QDate(2026, 2, 28), QTime(18, 0))),
                 QDateTime(QDate(2026, 3, 31), QTime(18, 0)));
    }

    void workspaceCrudAndCrossAppAgenda() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        zenapps::Workspace workspace(temporary.path());
        QVERIFY2(workspace.initialize(), qPrintable(workspace.lastError()));

        const qint64 noteId = workspace.saveNote(0, "Linked note", "An actionable idea");
        QVERIFY(noteId > 0);
        const qint64 taskId = workspace.createTask("Ship the idea", QDate(2026, 8, 7), 1, noteId);
        QVERIFY(taskId > 0);
        const qint64 reminderId = workspace.createReminder(
            "Check the idea", QDateTime(QDate(2026, 8, 7), QTime(9, 30)), "daily", noteId);
        QVERIFY(reminderId > 0);
        const qint64 eventId = workspace.createEvent(
            "Review", QDateTime(QDate(2026, 8, 7), QTime(11, 0)),
            QDateTime(QDate(2026, 8, 7), QTime(11, 30)), false, taskId);
        QVERIFY(eventId > 0);

        const QVariantList agenda = workspace.agenda(QDate(2026, 8, 7));
        QCOMPARE(agenda.size(), qsizetype(3));
        QVERIFY(workspace.setTaskCompleted(taskId, true));
        QCOMPARE(workspace.tasks("done").size(), qsizetype(1));
        QVERIFY(workspace.snoozeReminder(reminderId, 10));
        QVERIFY(workspace.deleteEvent(eventId));
        QVERIFY(workspace.deleteNote(noteId));
    }
};

QTEST_MAIN(CoreTests)
#include "core_tests.moc"
