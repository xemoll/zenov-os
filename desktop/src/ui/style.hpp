#pragma once

class QApplication;
class QString;
class QLabel;
class QPushButton;
class QWidget;

namespace zenapps {

void applyZenStyle(QApplication& application);
QLabel* makeTitle(const QString& text, QWidget* parent = nullptr);
QLabel* makeSubtitle(const QString& text, QWidget* parent = nullptr);
QPushButton* makePrimaryButton(const QString& text, QWidget* parent = nullptr);
QPushButton* makeQuietButton(const QString& text, QWidget* parent = nullptr);
QWidget* makePanel(QWidget* parent = nullptr);

}  // namespace zenapps
