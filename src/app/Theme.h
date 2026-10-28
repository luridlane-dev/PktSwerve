#pragma once
#include <QColor>
#include <QString>

class QApplication;

// Colors shared by every window.
namespace Theme {

inline const QColor bg       {0x00, 0x00, 0x00};
inline const QColor card     {0x1C, 0x1C, 0x1E};
inline const QColor fill     {0x2C, 0x2C, 0x2E};
inline const QColor fillHigh {0x3A, 0x3A, 0x3C};
inline const QColor line     {0x38, 0x38, 0x3A};
inline const QColor switchOff{0x39, 0x39, 0x3D};
inline const QColor text     {0xFF, 0xFF, 0xFF};
inline const QColor subtext  {0x8E, 0x8E, 0x93};
inline const QColor hint     {0x63, 0x63, 0x66};
inline const QColor blue     {0x0A, 0x84, 0xFF};
inline const QColor blueHigh {0x40, 0x9C, 0xFF};
inline const QColor green    {0x30, 0xD1, 0x58};
inline const QColor orange   {0xFF, 0x9F, 0x0A};
inline const QColor red      {0xFF, 0x45, 0x3A};

// Sets the style, palette, font and the stylesheet shared by all widgets.
void apply(QApplication& app);

// Replaces @name tokens (e.g. @card, @blue) with the colors above.
QString style(QString qss);

}
