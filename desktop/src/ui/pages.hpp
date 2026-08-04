#pragma once

#include <QString>

class QWidget;

namespace zenapps {

class Workspace;

enum class AppKind {
    Notes,
    Tasks,
    Calendar,
    Clock,
    Calculator,
    Reminders
};

AppKind appKindFromString(const QString& value);
QString appTitle(AppKind kind);
QString appSubtitle(AppKind kind);
QWidget* createPage(AppKind kind, Workspace* workspace, QWidget* parent = nullptr);

}  // namespace zenapps
