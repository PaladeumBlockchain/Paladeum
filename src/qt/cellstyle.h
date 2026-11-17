// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_QT_CELLSTYLE_H
#define PLB_QT_CELLSTYLE_H

#include <QColor>
#include <QPainterPath>
#include <QRectF>

QT_BEGIN_NAMESPACE
class QApplication;
class QPainter;
class QWidget;
QT_END_NAMESPACE

/**
 * The shapes of the Paladeum design guide ("Cell"): no rounded corners anywhere; the corners top left
 * and bottom right are cut at 45 degrees (6 on small tabs, 8 on buttons and fields, 12 on panels),
 * and the pointy-top hexagon of the mark holds what the chain counts (blocks, assets, list bullets).
 *
 * A style sheet cannot cut corners. install() paints them behind the widgets that need them: buttons,
 * fields, cards, tiles, tabs and tables. The style sheet gives those widgets no background and no
 * border of their own; it still sets their text, padding and size. What is painted follows the theme:
 *  - QPushButton: property primary=true in action yellow,
 *    look=secondary in mark blue, the others quiet (the cell color on the ground, raised on a panel);
 *    looks link and crumb have no fill.
 *  - fields (line edits, spin boxes, combo boxes, text edits): the cell color on the ground, raised on
 *    a panel, with a 2 px ring when focused; a field whose style sheet starts with INVALID_MARK in the
 *    error colors.
 *  - frames with the role card or tokenCard: a panel (cut 12); tile, segGroup, amountBox and the
 *    labels with a background role (notice, warning, tag, ...) their own fills.
 *  - item views and tab panes that are not on a panel already: a panel.
 * A widget with the property cellFill (a color) and optionally cellCut (default 8) is painted with
 * that fill. A widget with cellSurface=true counts as a panel for what is on it without being painted.
 */
namespace CellStyle
{
    /** Starts the style sheet of a field that holds an error (see GUIUtil::invalidFieldStyle()). */
    extern const char* const INVALID_MARK;

    /** A rectangle with the corners top left and bottom right cut at 45 degrees by `cut`. */
    QPainterPath cutRect(const QRectF& rect, qreal cut);
    /** The pointy-top hexagon that fills the rectangle (vertices at a quarter and three quarters of the height). */
    QPainterPath hexagon(const QRectF& rect);
    /** Paint a cut rectangle; with `ring` valid, a line of `ringWidth` along the inside of its edge. */
    void paintCut(QPainter* painter, const QRectF& rect, qreal cut, const QColor& fill,
                  const QColor& ring = QColor(), qreal ringWidth = 2);
    /** Paint the Paladeum mark (blue hexagon, yellow folded P) as large as it fits into the rectangle, centered. */
    void paintMark(QPainter* painter, const QRectF& rect);
    /** Paint the corners of the widgets described above, for the whole application. Call once. */
    void install(QApplication& app);
    /** Is the widget on a panel (a card, a tab pane, a table, a message box)? Fields and quiet buttons
        on a panel are raised; on the ground they have the cell color. */
    bool onPanel(const QWidget* widget);
}

#endif // PLB_QT_CELLSTYLE_H
