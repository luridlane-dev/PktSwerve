#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

#include <array>
#include <utility>

namespace {

// Longer names first so "@fillHigh" is not eaten by "@fill".
const std::array<std::pair<const char*, const QColor*>, 13> kTokens{{
    {"@fillHigh", &Theme::fillHigh},
    {"@blueHigh", &Theme::blueHigh},
    {"@subtext",  &Theme::subtext},
    {"@orange",   &Theme::orange},
    {"@green",    &Theme::green},
    {"@card",     &Theme::card},
    {"@fill",     &Theme::fill},
    {"@line",     &Theme::line},
    {"@text",     &Theme::text},
    {"@hint",     &Theme::hint},
    {"@blue",     &Theme::blue},
    {"@red",      &Theme::red},
    {"@bg",       &Theme::bg},
}};

const char* kAppStyle = R"(
    QWidget { color: @text; font-size: 13px; }
    QWidget:disabled { color: @hint; }

    QLabel#title   { font-size: 24px; font-weight: 700; }
    QLabel#heading { font-size: 14px; font-weight: 600; }
    QLabel#note    { font-size: 12px; color: @subtext; }
    QLabel#meta    { font-size: 12px; color: @hint; }

    QToolTip {
        background: @fill;
        color: @text;
        border: 1px solid @line;
        padding: 4px 8px;
    }

    QTabWidget::pane { border: none; }
    QTabWidget::tab-bar { alignment: center; }
    QTabBar { qproperty-drawBase: 0; }
    QTabBar::tab {
        background: transparent;
        color: @subtext;
        padding: 7px 16px;
        margin: 0 2px;
        border-radius: 8px;
        font-weight: 500;
    }
    QTabBar::tab:selected { background: @fill; color: @text; }
    QTabBar::tab:hover:!selected { color: @text; }

    QGroupBox {
        background: @card;
        border: none;
        border-radius: 12px;
        margin-top: 26px;
        padding: 14px 16px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top left;
        left: 16px;
        color: @subtext;
        font-size: 12px;
    }

    QLineEdit, QSpinBox, QComboBox, QTextEdit {
        background: @fill;
        border: 1px solid transparent;
        border-radius: 10px;
        padding: 7px 11px;
        selection-background-color: @blue;
    }
    QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QTextEdit:focus { border-color: @blue; }

    QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }

    QComboBox::drop-down { border: none; width: 28px; }
    QComboBox::down-arrow { image: url(:/icons/chevron.png); width: 14px; height: 14px; }
    QComboBox QAbstractItemView {
        background: @card;
        border: 1px solid @line;
        padding: 4px;
        outline: 0;
        selection-background-color: @blue;
    }

    QPushButton {
        background: @fill;
        color: @blue;
        border: none;
        border-radius: 10px;
        padding: 9px 18px;
        font-weight: 500;
    }
    QPushButton:hover   { background: @fillHigh; }
    QPushButton:pressed { background: @line; }
    QPushButton:disabled { color: @hint; }

    QPushButton#primaryBtn { background: @blue; color: @text; font-weight: 600; }
    QPushButton#primaryBtn:hover { background: @blueHigh; }
    QPushButton#primaryBtn:disabled { background: @fill; color: @hint; }

    QPushButton#destructiveBtn { color: @red; }

    QFrame#chip { background: @fill; border-radius: 14px; }
    QFrame#chip QLabel { background: transparent; }
    QPushButton#chipRemove {
        background: @fillHigh;
        color: @subtext;
        border-radius: 10px;
        padding: 0;
        min-width: 20px; max-width: 20px;
        min-height: 20px; max-height: 20px;
    }
    QPushButton#chipRemove:hover { background: @red; color: @text; }

    QScrollArea { border: none; }
    QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
    QScrollBar::handle:vertical { background: @hint; border-radius: 3px; min-height: 28px; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)";

QPalette makePalette()
{
    QPalette p;
    p.setColor(QPalette::Window,          Theme::bg);
    p.setColor(QPalette::WindowText,      Theme::text);
    p.setColor(QPalette::Base,            Theme::card);
    p.setColor(QPalette::AlternateBase,   Theme::fill);
    p.setColor(QPalette::Text,            Theme::text);
    p.setColor(QPalette::PlaceholderText, Theme::hint);
    p.setColor(QPalette::Button,          Theme::fill);
    p.setColor(QPalette::ButtonText,      Theme::blue);
    p.setColor(QPalette::ToolTipBase,     Theme::fill);
    p.setColor(QPalette::ToolTipText,     Theme::text);
    p.setColor(QPalette::Highlight,       Theme::blue);
    p.setColor(QPalette::HighlightedText, Theme::text);
    p.setColor(QPalette::Link,            Theme::blue);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, Theme::hint);
    return p;
}

}

QString Theme::style(QString qss)
{
    for (const auto& [name, color] : kTokens)
        qss.replace(QLatin1String(name), color->name());
    return qss;
}

void Theme::apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setPalette(makePalette());

#ifdef Q_OS_WIN
    QFont font = app.font();
    font.setFamilies({"Segoe UI Variable Text", "Segoe UI"});
    app.setFont(font);
#endif

    app.setStyleSheet(style(kAppStyle));
}
