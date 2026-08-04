#pragma once

#include <QDateTime>
#include <QString>

namespace zenapps {

QDateTime nextOccurrence(const QDateTime& scheduled,
                         const QString& recurrence,
                         const QDateTime& after);

}  // namespace zenapps
