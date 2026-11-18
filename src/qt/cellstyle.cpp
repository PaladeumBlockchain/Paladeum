// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cellstyle.h"

#include "guiutil.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTextEdit>
#include <QToolButton>

#include <cmath>

namespace CellStyle
{

const char* const INVALID_MARK = "/* invalid */";

QPainterPath cutRect(const QRectF& r, qreal cut)
{
    cut = qMax<qreal>(0, qMin(cut, qMin(r.width(), r.height()) / 2));
    QPainterPath path;
    path.moveTo(r.left() + cut, r.top());
    path.lineTo(r.right(), r.top());
    path.lineTo(r.right(), r.bottom() - cut);
    path.lineTo(r.right() - cut, r.bottom());
    path.lineTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + cut);
    path.closeSubpath();
    return path;
}

QPainterPath hexagon(const QRectF& r)
{
    QPainterPath path;
    const qreal cx = r.center().x();
    path.moveTo(cx, r.top());
    path.lineTo(r.right(), r.top() + r.height() * 0.25);
    path.lineTo(r.right(), r.top() + r.height() * 0.75);
    path.lineTo(cx, r.bottom());
    path.lineTo(r.left(), r.top() + r.height() * 0.75);
    path.lineTo(r.left(), r.top() + r.height() * 0.25);
    path.closeSubpath();
    return path;
}

void paintCut(QPainter* painter, const QRectF& rect, qreal cut, const QColor& fill, const QColor& ring, qreal ringWidth)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    const QPainterPath outer = cutRect(rect, cut);
    if (fill.isValid() && fill.alpha() > 0) {
        painter->setBrush(fill);
        painter->drawPath(outer);
    }
    if (ring.isValid()) {
        // the inner edge is parallel to the outer one, so its cut is shorter by (2 - sqrt 2) of the width
        const QPainterPath inner = cutRect(rect.adjusted(ringWidth, ringWidth, -ringWidth, -ringWidth),
                                           qMax<qreal>(0, cut - ringWidth * (2 - std::sqrt(2.0))));
        painter->setBrush(ring);
        painter->drawPath(outer.subtracted(inner));
    }
    painter->restore();
}

void paintMark(QPainter* painter, const QRectF& box)
{
    // the mark on its grid of 512 x 585
    const qreal s = qMin(box.width() / 512.0, box.height() / 585.0);
    const QPointF origin(box.center().x() - 256 * s, box.center().y() - 292.5 * s);
    auto polygon = [&](std::initializer_list<QPointF> points) {
        QPainterPath path;
        bool first = true;
        for (const QPointF& point : points) {
            const QPointF at(origin.x() + point.x() * s, origin.y() + point.y() * s);
            if (first)
                path.moveTo(at);
            else
                path.lineTo(at);
            first = false;
        }
        path.closeSubpath();
        return path;
    };
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor("#344C94"));
    painter->drawPath(polygon({{256, 0}, {512, 147}, {512, 438}, {256, 585}, {0, 438}, {0, 147}}));
    painter->setBrush(QColor("#F7DE4E"));
    painter->drawPath(polygon({{64, 183}, {256, 72}, {446, 183}, {446, 327}, {256, 437}, {191, 400}, {191, 256},
                               {256, 292}, {256, 364}, {382, 292}, {382, 218}, {256, 146}, {128, 218}, {128, 505}, {64, 470}}));
    painter->restore();
}

/** Is this widget a surface in the cell color for what is on it? */
static bool isPanel(const QWidget* w)
{
    if (w->property("cellSurface").toBool())
        return true;
    const QByteArray role = w->property("role").toByteArray();
    if (role == "card" || role == "tokenCard")
        return true;
    if (w->objectName() == QLatin1String("qt_tabwidget_stackedwidget"))
        return true;
    return qobject_cast<const QAbstractItemView*>(w) != nullptr;
}

bool onPanel(const QWidget* widget)
{
    for (const QWidget* p = widget->parentWidget(); p; p = p->parentWidget()) {
        if (isPanel(p))
            return true;
        if (p->isWindow())
            return p->inherits("QMessageBox");
    }
    return false;
}

static bool insideCalendar(const QWidget* widget)
{
    for (const QWidget* p = widget; p; p = p->parentWidget()) {
        if (p->inherits("QCalendarWidget"))
            return true;
        if (p->isWindow())
            break;
    }
    return false;
}

/** What to paint behind a widget */
struct Shape {
    QRectF rect;   //!< empty: the whole widget
    qreal cut = 8;
    QColor fill;
    QColor ring;
    qreal ringWidth = 2;
};

static bool keyboardFocus(const QWidget* w)
{
    return w->hasFocus() && w->window() && w->window()->testAttribute(Qt::WA_KeyboardFocusChange);
}

/** The frame painted as a field (an amount with its unit and buttons) that the widget is in, if any */
static QWidget* fieldFrame(const QWidget* w)
{
    for (QWidget* p = w->parentWidget(); p && !p->isWindow(); p = p->parentWidget()) {
        if (p->property("role").toByteArray() == "amountBox")
            return p;
        if (isPanel(p))
            break;
    }
    return nullptr;
}

static bool insideField(const QWidget* w)
{
    return fieldFrame(w) != nullptr;
}

/** The quiet fill: the cell color on the ground, raised on a panel */
static QColor quietFill(const QWidget* w, bool hover)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    if (insideField(w))
        return hover ? t.raisedHover : t.surface;
    if (onPanel(w))
        return hover ? t.raisedHover : t.surfaceAlt;
    return hover ? t.surfaceAlt : t.surface;
}

static bool buttonShape(const QPushButton* b, Shape& s)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    const QByteArray look = b->property("look").toByteArray();
    if (look == "link" || look == "crumb" || (b->isFlat() && look.isEmpty()))
        return false;
    const bool enabled = b->isEnabled();
    const bool hover = enabled && b->underMouse();
    const bool down = enabled && b->isDown();
    if (keyboardFocus(b))
        s.ring = t.accent;
    // the one primary action of a page: yellow. (Not the default button of a dialog: in a dialog the
    // default moves to the button that has the focus, and the color of its text would not follow.)
    if (b->property("primary").toBool()) {
        s.fill = !enabled ? quietFill(b, false) : down ? t.actionPressed : hover ? t.actionHover : t.action;
        return true;
    }
    if (look == "secondary") {
        s.fill = !enabled ? quietFill(b, false) : down ? t.fillPressed : hover ? t.fillHover : t.fill;
        return true;
    }
    if (look == "ghost" || look == "icon") {
        if (!(hover || down || s.ring.isValid()))
            return false;
        s.fill = hover || down ? quietFill(b, true) : QColor(Qt::transparent);
        return true;
    }
    if (look == "seg") {
        // a segment: the chosen one is a tab of its own (cut 6) on the fill of its group, which is
        // the cell color on the ground and raised on a panel
        s.cut = 6;
        if (b->isChecked())
            s.fill = onPanel(b) ? t.surface : t.surfaceAlt;
        else if (s.ring.isValid())
            s.fill = QColor(Qt::transparent);
        else
            return false;
        return true;
    }
    if (look == "chipButton" || look == "strip") {
        s.cut = 6;
        s.fill = quietFill(b, hover || down);
        return true;
    }
    if (look == "pend") {
        s.cut = 6;
        s.fill = hover ? t.raisedHover : t.warnBg;
        return true;
    }
    if (look == "wait") {
        s.cut = 6;
        s.fill = hover ? t.raisedHover : t.chip;
        return true;
    }
    if (look == "fee") {
        // a choice: the chosen one has the ring of the selection
        s.fill = quietFill(b, hover || down);
        if (b->isChecked())
            s.ring = t.accent;
        return true;
    }
    s.fill = quietFill(b, hover || down);
    return true;
}

static bool toolButtonShape(const QToolButton* b, Shape& s)
{
    const QWidget* parent = b->parentWidget();
    // the buttons inside fields, tab bars and the calendar have their own look
    if (!parent || qobject_cast<const QLineEdit*>(parent) || qobject_cast<const QAbstractSpinBox*>(parent)
        || qobject_cast<const QTabBar*>(parent) || parent->inherits("QToolBar") || parent->inherits("QMenuBar")
        || insideCalendar(b))
        return false;
    const GUIUtil::Theme& t = GUIUtil::theme();
    const QByteArray look = b->property("look").toByteArray();
    const bool enabled = b->isEnabled();
    const bool hover = enabled && b->underMouse();
    const bool down = enabled && b->isDown();
    if (keyboardFocus(b))
        s.ring = t.accent;
    if (look == "icon" || b->autoRaise()) {
        if (!(hover || down || s.ring.isValid()))
            return false;
        s.fill = hover || down ? quietFill(b, true) : QColor(Qt::transparent);
        return true;
    }
    // back and forward of the page header: no fill when there is nowhere to go
    if (look == "nav" && !enabled)
        return false;
    s.fill = quietFill(b, hover || down || b->isChecked());
    return true;
}

static bool invalidField(const QWidget* w)
{
    return w && w->styleSheet().startsWith(QLatin1String(INVALID_MARK));
}

static void fieldShape(const QWidget* w, bool focus, bool invalid, Shape& s)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    s.cut = 8;
    s.fill = onPanel(w) ? t.surfaceAlt : t.surface;
    if (invalid) {
        s.fill = t.errorSoft;
        s.ring = t.error;
        s.ringWidth = 1.5;
    } else if (focus && w->isEnabled()) {
        s.ring = t.accent;
    }
}

static bool labelShape(const QWidget* w, const QByteArray& role, Shape& s)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    if (role == "notice") { s.fill = t.accentSoft; s.cut = 8; return true; }
    if (role == "warning") { s.fill = t.warnBg; s.cut = 8; return true; }
    if (role == "resultOk") { s.fill = t.okSoft; s.cut = 8; return true; }
    if (role == "resultError") { s.fill = t.errorSoft; s.cut = 8; return true; }
    if (role == "pill") { s.fill = t.warnBg; s.cut = 6; return true; }
    if (role == "tag") { s.fill = t.chip; s.cut = 6; return true; }
    Q_UNUSED(w);
    return false;
}

static bool shapeFor(QWidget* w, Shape& s)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    const QVariant customFill = w->property("cellFill");
    if (customFill.isValid()) {
        s.fill = customFill.value<QColor>();
        s.cut = w->property("cellCut").isValid() ? w->property("cellCut").toReal() : 8;
        return s.fill.isValid();
    }
    if (QPushButton* button = qobject_cast<QPushButton*>(w))
        return buttonShape(button, s);
    if (QToolButton* button = qobject_cast<QToolButton*>(w))
        return toolButtonShape(button, s);
    const QByteArray look = w->property("look").toByteArray();
    if (QLineEdit* edit = qobject_cast<QLineEdit*>(w)) {
        // the field of a spin box or a combo box is painted with its box
        QWidget* parent = w->parentWidget();
        if (look == "bare" || qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent) || insideField(w))
            return false;
        fieldShape(w, edit->hasFocus() && !edit->isReadOnly(), invalidField(w), s);
        return true;
    }
    if (QAbstractSpinBox* box = qobject_cast<QAbstractSpinBox*>(w)) {
        if (look == "bare" || insideField(w))
            return false;
        fieldShape(w, box->hasFocus(), invalidField(w), s);
        return true;
    }
    if (QComboBox* combo = qobject_cast<QComboBox*>(w)) {
        if (look == "bare")
            return false;
        fieldShape(w, combo->hasFocus(), invalidField(w) || invalidField(combo->lineEdit()), s);
        // the unit inside an amount field stands out from the field
        if (insideField(w) && !s.ring.isValid())
            s.fill = t.surface;
        return true;
    }
    if (QPlainTextEdit* edit = qobject_cast<QPlainTextEdit*>(w)) {
        if (look == "bare")
            return false;
        fieldShape(w, edit->hasFocus() && !edit->isReadOnly(), invalidField(w), s);
        return true;
    }
    if (QTextEdit* edit = qobject_cast<QTextEdit*>(w)) {
        if (look == "bare" || w->inherits("QTextBrowser"))
            return false;
        fieldShape(w, edit->hasFocus() && !edit->isReadOnly(), invalidField(w), s);
        return true;
    }
    const QByteArray role = w->property("role").toByteArray();
    if (!role.isEmpty()) {
        if (role == "card" || role == "tokenCard") {
            s.fill = t.surface;
            s.cut = 12;
            return true;
        }
        if (role == "tile") {
            s.fill = onPanel(w) ? t.surfaceAlt : t.surface;
            s.cut = 8;
            return true;
        }
        if (role == "segGroup") {
            s.fill = onPanel(w) ? t.surfaceAlt : t.surface;
            s.cut = 8;
            return true;
        }
        if (role == "amountBox") {
            // the ring and the error colors of the fields inside it
            bool invalid = false;
            for (const QWidget* child : w->findChildren<QWidget*>())
                invalid = invalid || invalidField(child);
            fieldShape(w, w->isAncestorOf(QApplication::focusWidget()), invalid, s);
            return true;
        }
        if (qobject_cast<QLabel*>(w))
            return labelShape(w, role, s);
    }
    if (QTabBar* bar = qobject_cast<QTabBar*>(w)) {
        if (bar->currentIndex() < 0)
            return false;
        s.rect = QRectF(bar->tabRect(bar->currentIndex()));
        s.fill = onPanel(w) ? t.surfaceAlt : t.surface;
        s.cut = 6;
        return true;
    }
    // tables and tab panes on the ground are panels of their own
    if (w->objectName() == QLatin1String("qt_tabwidget_stackedwidget")
        || (qobject_cast<QAbstractItemView*>(w) && !qobject_cast<QHeaderView*>(w))) {
        if (w->isWindow()) {
            // a pop-up list: square, in the cell color
            s.fill = t.surface;
            s.cut = 0;
            return true;
        }
        if (onPanel(w) || insideCalendar(w) || w->window()->windowType() == Qt::Popup)
            return false;
        s.fill = t.surface;
        s.cut = 12;
        return true;
    }
    return false;
}

/** Paints the shapes behind the widgets, before they paint their text and content. */
class CutCornerPainter : public QObject
{
public:
    explicit CutCornerPainter(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Paint: {
            if (!object->isWidgetType())
                break;
            QWidget* widget = static_cast<QWidget*>(object);
            Shape shape;
            if (shapeFor(widget, shape)) {
                QPainter painter(widget);
                paintCut(&painter, shape.rect.isEmpty() ? QRectF(widget->rect()) : shape.rect, shape.cut, shape.fill,
                         shape.ring, shape.ringWidth);
            }
            break;
        }
        case QEvent::Polish:
            // the fill of buttons changes under the pointer
            if (qobject_cast<QPushButton*>(object) || qobject_cast<QToolButton*>(object))
                static_cast<QWidget*>(object)->setAttribute(Qt::WA_Hover, true);
            break;
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::StyleChange:
            // the ring (and the error colors) of a spin box, a combo box or an amount field follow
            // the field inside it
            if (object->isWidgetType()) {
                QWidget* widget = static_cast<QWidget*>(object);
                if (QWidget* parent = widget->parentWidget()) {
                    if (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent))
                        parent->update();
                }
                if (QWidget* frame = fieldFrame(widget))
                    frame->update();
            }
            break;
        default:
            break;
        }
        return false;
    }
};

void install(QApplication& app)
{
    app.installEventFilter(new CutCornerPainter(&app));
}

} // namespace CellStyle
