#include "ui/style.hpp"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QStyleFactory>
#include <QWidget>

namespace zenapps {

void applyZenStyle(QApplication& application) {
    application.setStyle(QStyleFactory::create("Fusion"));
    QFont font = application.font();
    font.setFamilies({"Inter", "Noto Sans", "Segoe UI", "SF Pro Text"});
    font.setPointSize(10);
    application.setFont(font);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#0b0e15"));
    palette.setColor(QPalette::WindowText, QColor("#eef1f8"));
    palette.setColor(QPalette::Base, QColor("#111620"));
    palette.setColor(QPalette::AlternateBase, QColor("#171d29"));
    palette.setColor(QPalette::Text, QColor("#eef1f8"));
    palette.setColor(QPalette::Button, QColor("#171d29"));
    palette.setColor(QPalette::ButtonText, QColor("#eef1f8"));
    palette.setColor(QPalette::Highlight, QColor("#7c8cff"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#7f8798"));
    application.setPalette(palette);

    application.setStyleSheet(R"(
        QWidget {
            color: #eef1f8;
            background: #0b0e15;
        }
        QMainWindow, QDialog { background: #0b0e15; }
        QWidget#panel {
            background: #111620;
            border: 1px solid #252d3d;
            border-radius: 14px;
        }
        QLabel#title { font-size: 26px; font-weight: 700; }
        QLabel#subtitle { color: #949daf; font-size: 11px; }
        QLabel#metric { font-size: 42px; font-weight: 700; color: #ffffff; }
        QLabel#result { font-size: 36px; font-weight: 700; color: #ffffff; }
        QLabel#badge {
            background: #20283a;
            color: #bdc5d7;
            border-radius: 9px;
            padding: 4px 9px;
        }
        QLineEdit, QPlainTextEdit, QTextEdit, QDateEdit, QDateTimeEdit,
        QTimeEdit, QSpinBox, QComboBox, QListWidget, QTableWidget, QCalendarWidget {
            background: #111620;
            border: 1px solid #293246;
            border-radius: 10px;
            padding: 8px;
            selection-background-color: #6577ef;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QDateEdit:focus,
        QDateTimeEdit:focus, QTimeEdit:focus, QSpinBox:focus, QComboBox:focus {
            border: 1px solid #7c8cff;
        }
        QListWidget::item, QTableWidget::item { padding: 7px; }
        QListWidget::item:selected, QTableWidget::item:selected {
            background: #26304a;
            border-radius: 7px;
        }
        QHeaderView::section {
            color: #949daf;
            background: #111620;
            border: none;
            border-bottom: 1px solid #293246;
            padding: 8px;
            font-weight: 600;
        }
        QPushButton {
            background: #1a2130;
            border: 1px solid #303a50;
            border-radius: 10px;
            padding: 8px 14px;
            font-weight: 600;
        }
        QPushButton:hover { background: #232c40; border-color: #46536f; }
        QPushButton:pressed { background: #151b28; }
        QPushButton#primary {
            background: #7587ff;
            color: #ffffff;
            border: 1px solid #91a0ff;
        }
        QPushButton#primary:hover { background: #8494ff; }
        QPushButton#danger { color: #ffb2b2; border-color: #633c46; }
        QToolButton { border: none; padding: 6px; border-radius: 8px; }
        QToolButton:hover { background: #20283a; }
        QTabWidget::pane { border: none; }
        QTabBar::tab {
            background: transparent;
            color: #8e97aa;
            padding: 9px 16px;
            border-bottom: 2px solid transparent;
        }
        QTabBar::tab:selected { color: #ffffff; border-bottom-color: #7c8cff; }
        QProgressBar {
            background: #1a2130;
            border: none;
            border-radius: 6px;
            height: 12px;
            text-align: center;
        }
        QProgressBar::chunk { background: #7c8cff; border-radius: 6px; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #30394b; border-radius: 5px; min-height: 28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QSplitter::handle { background: transparent; width: 8px; height: 8px; }
    )");
}

QLabel* makeTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("title");
    return label;
}

QLabel* makeSubtitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("subtitle");
    label->setWordWrap(true);
    return label;
}

QPushButton* makePrimaryButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName("primary");
    return button;
}

QPushButton* makeQuietButton(const QString& text, QWidget* parent) {
    return new QPushButton(text, parent);
}

QWidget* makePanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName("panel");
    return panel;
}

}  // namespace zenapps
