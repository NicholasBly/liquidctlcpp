// LiquidCam - Theme.h
// One dark theme, defined once. The stylesheet is a compiled-in string rather
// than a .qrc resource: no rcc step, nothing to load from disk at startup.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

namespace lc {
namespace theme {

// Graphite panels, one violet accent, instrument-style readouts.
inline const QColor kBackground  (0x14, 0x14, 0x19);
inline const QColor kPanel       (0x1C, 0x1C, 0x24);
inline const QColor kPanelRaised (0x23, 0x23, 0x2D);
inline const QColor kLine        (0x2E, 0x2E, 0x3A);
inline const QColor kText        (0xE6, 0xE6, 0xEF);
inline const QColor kTextMuted   (0x8A, 0x8A, 0x9C);
inline const QColor kAccent      (0x8B, 0x5C, 0xF6);
inline const QColor kAccentDim   (0x5B, 0x3D, 0xA6);
inline const QColor kGood        (0x2D, 0xD4, 0xBF);
inline const QColor kWarn        (0xF5, 0x9E, 0x0B);
inline const QColor kBad         (0xEF, 0x44, 0x44);

QString styleSheet();
QFont   readoutFont(int pointSize, bool bold = false);
QIcon   appIcon();

} // namespace theme
} // namespace lc
