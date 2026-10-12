#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>



/*
 * Palette for widgets that paint themselves (QPainter, QCustomPlot, syntax
 * highlighting), where the stylesheet cannot reach.
 * Must match the palette in resources/styles/dark.qss.
 */
namespace Theme {
    inline const QColor window      {0x1a, 0x1b, 0x1d};   // gaps between panels
    inline const QColor seam        {0x10, 0x11, 0x13};   // 1px panel outlines
    inline const QColor input       {0x1a, 0x1b, 0x1d};
    inline const QColor surface     {0x20, 0x21, 0x24};   // panel content
    inline const QColor surfaceAlt  {0x27, 0x28, 0x2b};   // header strips
    inline const QColor border      {0x2d, 0x2e, 0x31};
    inline const QColor borderStrong{0x3b, 0x3d, 0x41};
    inline const QColor grip        {0x5a, 0x5c, 0x62};   // resize-bar dots

    inline const QColor text        {0xe6, 0xe7, 0xe9};
    inline const QColor textMuted   {0xa6, 0xa8, 0xae};
    inline const QColor textFaint   {0x73, 0x75, 0x7b};

    inline const QColor accent      {0x4a, 0x8f, 0xe7};
    inline const QColor accentLight {0x7f, 0xb0, 0xee};   // accent on dark fills (icons)

    inline constexpr int panelRadius = 4;

    // Monochrome SVG icon (drawn white in resources/icons) tinted to the palette:
    // normalColor normally, full text color on hover or in a selected row,
    // onColor when checked / the current tab (QIcon::On).
    QIcon icon(const QString& svgPath, const QColor& onColor = text, const QColor& normalColor = textMuted);

    // One tinted rendering of such an SVG (for labels), crisp at the device pixel ratio.
    QPixmap pixmap(const QString& svgPath, int size, const QColor& color, qreal dpr = 1.0);
}
