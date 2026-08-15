// LiquidCam - Theme.cpp
#include "Theme.h"

#include <QFontDatabase>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

namespace lc {
namespace theme {

QString styleSheet()
{
    static const QString qss = QStringLiteral(R"QSS(
QWidget {
    background: #141419;
    color: #E6E6EF;
    font-family: "Segoe UI", "Inter", sans-serif;
    font-size: 12px;
}
QFrame#Sidebar {
    background: #1C1C24;
    border-right: 1px solid #2E2E3A;
}
QLabel#Wordmark {
    color: #E6E6EF;
    font-size: 17px;
    font-weight: 600;
    letter-spacing: 1px;
    padding: 18px 16px 2px 18px;
}
QLabel#Tagline {
    color: #8A8A9C;
    font-size: 10px;
    letter-spacing: 2px;
    padding: 0 16px 16px 18px;
}
QToolButton#NavButton {
    background: transparent;
    border: none;
    border-left: 3px solid transparent;
    color: #8A8A9C;
    padding: 11px 16px;
    text-align: left;
    font-size: 13px;
}
QToolButton#NavButton:hover {
    color: #E6E6EF;
    background: #23232D;
}
QToolButton#NavButton:checked {
    color: #FFFFFF;
    background: #23232D;
    border-left: 3px solid #8B5CF6;
    font-weight: 600;
}
QFrame#Card {
    background: #1C1C24;
    border: 1px solid #2E2E3A;
    border-radius: 6px;
}
QFrame#HeaderBar {
    background: #1C1C24;
    border-bottom: 1px solid #2E2E3A;
}
QLabel#PageTitle {
    font-size: 19px;
    font-weight: 600;
    color: #FFFFFF;
}
QLabel#CardTitle {
    font-size: 13px;
    font-weight: 600;
    color: #E6E6EF;
}
QLabel#Caption {
    color: #8A8A9C;
    font-size: 11px;
}
QLabel#Metric {
    color: #E6E6EF;
}
QLabel#MetricUnit {
    color: #8A8A9C;
    font-size: 11px;
}
QLabel#SectionLabel {
    color: #8A8A9C;
    font-size: 10px;
    font-weight: 600;
    letter-spacing: 1.5px;
}
QPushButton {
    background: #23232D;
    border: 1px solid #2E2E3A;
    border-radius: 4px;
    padding: 7px 16px;
    color: #E6E6EF;
}
QPushButton:hover  { border-color: #8B5CF6; }
QPushButton:pressed{ background: #2E2E3A; }
QPushButton:disabled { color: #55555F; border-color: #23232D; }
QPushButton#Primary {
    background: #8B5CF6;
    border: 1px solid #8B5CF6;
    color: #FFFFFF;
    font-weight: 600;
}
QPushButton#Primary:hover   { background: #9B72F8; }
QPushButton#Primary:pressed { background: #7A4DE0; }
QPushButton#Swatch {
    border: 1px solid #2E2E3A;
    border-radius: 4px;
    min-width: 34px;
    min-height: 26px;
}
QPushButton#Swatch:hover { border: 1px solid #E6E6EF; }
QComboBox, QSpinBox {
    background: #23232D;
    border: 1px solid #2E2E3A;
    border-radius: 4px;
    padding: 5px 8px;
    min-height: 18px;
    selection-background-color: #8B5CF6;
}
QComboBox:hover, QSpinBox:hover { border-color: #4A4A5A; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: #23232D;
    border: 1px solid #2E2E3A;
    selection-background-color: #8B5CF6;
    outline: none;
}
QCheckBox { spacing: 8px; padding: 3px 0; }
QCheckBox::indicator {
    width: 15px; height: 15px;
    border: 1px solid #4A4A5A;
    border-radius: 3px;
    background: #23232D;
}
QCheckBox::indicator:checked { background: #8B5CF6; border-color: #8B5CF6; }
QSlider::groove:horizontal {
    height: 4px;
    background: #2E2E3A;
    border-radius: 2px;
}
QSlider::sub-page:horizontal { background: #8B5CF6; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #E6E6EF;
    width: 12px;
    margin: -5px 0;
    border-radius: 6px;
}
QSlider::handle:horizontal:hover { background: #FFFFFF; }
QPlainTextEdit {
    background: #17171D;
    border: 1px solid #2E2E3A;
    border-radius: 4px;
    color: #8A8A9C;
    font-family: Consolas, "Cascadia Mono", monospace;
    font-size: 11px;
}
QScrollArea#PageScroll,
QScrollArea#PageScroll > QWidget > QWidget {
    background: transparent;
    border: none;
}

QScrollBar:vertical {
    background: transparent; width: 8px; margin: 0;
}
QScrollBar::handle:vertical { background: #2E2E3A; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #4A4A5A; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip {
    background: #23232D;
    color: #E6E6EF;
    border: 1px solid #8B5CF6;
    padding: 4px;
}
)QSS");
    return qss;
}

QFont readoutFont(int pointSize, bool bold)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setFamily(QStringLiteral("Consolas"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(pointSize);
    font.setBold(bold);
    return font;
}

QIcon appIcon()
{
    // Painted at runtime so the executable carries no image payload and the
    // tray icon is always crisp at whatever DPI Windows asks for.
    QIcon icon;
    for (int size : { 16, 24, 32, 48, 64, 128 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal s = size;
        QLinearGradient gradient(0, 0, s, s);
        gradient.setColorAt(0.0, QColor(0x9B, 0x72, 0xF8));
        gradient.setColorAt(1.0, QColor(0x5B, 0x3D, 0xA6));

        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(0, 0, s, s), s * 0.24, s * 0.24);

        // Three blades on a hub: a fan, read at 16 px as a simple pinwheel.
        painter.translate(s / 2.0, s / 2.0);
        painter.setBrush(QColor(0xF2, 0xF0, 0xFF));
        for (int i = 0; i < 3; ++i) {
            painter.rotate(120.0);
            QPainterPath blade;
            blade.moveTo(0, 0);
            blade.cubicTo(s * 0.10, -s * 0.16, s * 0.30, -s * 0.20, s * 0.34, -s * 0.04);
            blade.cubicTo(s * 0.24, s * 0.02, s * 0.10, s * 0.06, 0, 0);
            painter.drawPath(blade);
        }
        painter.setBrush(QColor(0x14, 0x14, 0x19));
        painter.drawEllipse(QPointF(0, 0), s * 0.085, s * 0.085);
        painter.end();

        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace theme
} // namespace lc
