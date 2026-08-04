#include "core/recurrence.hpp"

#include <QDate>
#include <QtGlobal>

namespace zenapps {
namespace {

QDate addAnchoredMonth(const QDate& source, int months, int anchorDay) {
    const QDate first(source.year(), source.month(), 1);
    const QDate target = first.addMonths(months);
    const int day = qMin(anchorDay, target.daysInMonth());
    return QDate(target.year(), target.month(), day);
}

}  // namespace

QDateTime nextOccurrence(const QDateTime& scheduled,
                         const QString& recurrence,
                         const QDateTime& after) {
    if (!scheduled.isValid() || !after.isValid() || recurrence == "none") {
        return QDateTime{};
    }

    QDateTime candidate = scheduled;
    const QString rule = recurrence.trimmed().toLower();

    if (rule == "daily") {
        if (candidate > after) {
            return candidate;
        }
        const qint64 days = candidate.date().daysTo(after.date());
        candidate = candidate.addDays(qMax<qint64>(1, days));
        while (candidate <= after) {
            candidate = candidate.addDays(1);
        }
        return candidate;
    }

    if (rule == "weekly") {
        if (candidate > after) {
            return candidate;
        }
        const qint64 days = candidate.date().daysTo(after.date());
        const qint64 weeks = qMax<qint64>(1, days / 7);
        candidate = candidate.addDays(weeks * 7);
        while (candidate <= after) {
            candidate = candidate.addDays(7);
        }
        return candidate;
    }

    if (rule == "monthly") {
        const int anchorDay = scheduled.date().day();
        if (candidate > after) {
            return candidate;
        }
        int months = (after.date().year() - scheduled.date().year()) * 12
            + after.date().month() - scheduled.date().month();
        months = qMax(1, months);
        candidate.setDate(addAnchoredMonth(scheduled.date(), months, anchorDay));
        while (candidate <= after) {
            ++months;
            candidate.setDate(addAnchoredMonth(scheduled.date(), months, anchorDay));
        }
        return candidate;
    }

    return QDateTime{};
}

}  // namespace zenapps
