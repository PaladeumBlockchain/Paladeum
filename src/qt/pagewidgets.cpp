// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pagewidgets.h"

#include "cellstyle.h"
#include "guiutil.h"
#include "tokenlists.h"

#include <QApplication>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolTip>
#include <QtMath>
#include <QVBoxLayout>

AmountLabel::AmountLabel(int pixelSize, int _weight, QWidget *parent) :
    QWidget(parent), px(pixelSize), minPx(pixelSize), weight(_weight), alignment(Qt::AlignLeft | Qt::AlignVCenter)
{
    color = GUIUtil::theme().text;
    unitColor = GUIUtil::theme().textSecondary;
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QFont AmountLabel::amountFont(int size) const
{
    // figures in Space Grotesk (it sets large sizes a little tighter by itself): 400 from 20 px,
    // 500 below, where the regular weight would look thin
    return GUIUtil::displayFont(size, size < 20 && weight >= QFont::Medium ? QFont::Medium : QFont::Normal);
}

QFont AmountLabel::unitFont(int size) const
{
    return GUIUtil::appFont(qMax(11, qRound(size * 0.45)), QFont::Medium);
}

void AmountLabel::setPixelSize(int size)
{
    if (minPx == px)
        minPx = size;
    px = size;
    minPx = qMin(minPx, px);
    updateGeometry();
    update();
}

int AmountLabel::widthAt(int size) const
{
    int w = GUIUtil::amountWidth(text, amountFont(size));
    if (!unit.isEmpty())
        w += qMax(4, size / 4) + QFontMetrics(unitFont(size)).horizontalAdvance(unit);
    return w + 2;
}

int AmountLabel::fittingSize(int width) const
{
    int size = px;
    while (size > minPx && widthAt(size) > width)
        --size;
    return size;
}

void AmountLabel::setMinimumPixelSize(int size)
{
    minPx = qMin(px, size);
    updateGeometry();
    update();
}

QSize AmountLabel::minimumSizeHint() const
{
    // the height stays that of the full size, so that a smaller amount keeps the line in place
    return QSize(widthAt(minPx), QFontMetrics(amountFont(px)).height() + 2);
}

void AmountLabel::setAmount(const QString& _amount, const QString& _unit)
{
    if (text == _amount && unit == _unit)
        return;
    text = _amount;
    unit = _unit;
    updateGeometry();
    update();
}

void AmountLabel::setColor(const QColor& _color)
{
    color = _color;
    update();
}

void AmountLabel::setUnitColor(const QColor& _color)
{
    unitColor = _color;
    update();
}

void AmountLabel::setAlignment(Qt::Alignment _alignment)
{
    alignment = _alignment;
    update();
}

QSize AmountLabel::sizeHint() const
{
    return QSize(widthAt(px), QFontMetrics(amountFont(px)).height() + 2);
}

void AmountLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    // a long amount in a narrow place gets smaller (see setMinimumPixelSize), on the same base line
    const int size = fittingSize(width());
    const QFont font = amountFont(size);
    const QFontMetrics fm(font);
    const int amountW = GUIUtil::amountWidth(text, font);
    const int gap = unit.isEmpty() ? 0 : qMax(4, size / 4);
    const int unitW = unit.isEmpty() ? 0 : QFontMetrics(unitFont(size)).horizontalAdvance(unit);
    int x = 0;
    if (alignment & Qt::AlignRight)
        x = width() - (amountW + gap + unitW);
    else if (alignment & Qt::AlignHCenter)
        x = (width() - (amountW + gap + unitW)) / 2;
    GUIUtil::paintAmount(&p, QRect(x, 0, amountW, height()), text, font, color, Qt::AlignLeft | Qt::AlignVCenter);
    if (!unit.isEmpty()) {
        const int baseline = (height() - fm.height()) / 2 + fm.ascent();
        p.setFont(unitFont(size));
        p.setPen(unitColor);
        p.drawText(x + amountW + gap, baseline, unit);
    }
}

BoxDirectionFollower::BoxDirectionFollower(QWidget *_watched, QBoxLayout *_layout, int _minWidth, QObject *owner) :
    QObject(owner ? owner : _watched), watched(_watched), layout(_layout), minWidth(_minWidth)
{
    watched->installEventFilter(this);
    if (watched->width() > 0)
        follow();
}

void BoxDirectionFollower::follow()
{
    if (!layout)
        return;
    const QBoxLayout::Direction direction = watched->width() >= minWidth ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (layout->direction() != direction)
        layout->setDirection(direction);
}

bool BoxDirectionFollower::eventFilter(QObject *object, QEvent *event)
{
    if (object == watched && event->type() == QEvent::Resize)
        follow();
    return QObject::eventFilter(object, event);
}

ChoiceButton::ChoiceButton(const QString& text, Look _look, QWidget *parent) :
    QAbstractButton(parent), look(_look), hovered(false)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(look == Box ? QSizePolicy::Expanding : QSizePolicy::Minimum, QSizePolicy::Fixed);
}

QSize ChoiceButton::sizeHint() const
{
    const QStringList lines = text().split('\n');
    int w = 0;
    const QFontMetrics fm(GUIUtil::appFont(13, QFont::DemiBold));
    for (const QString& line : lines)
        w = qMax(w, fm.horizontalAdvance(line));
    return look == Box ? QSize(w + 24, 56) : QSize(w + 32, 40);
}

void ChoiceButton::enterEvent(QEvent *event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ChoiceButton::leaveEvent(QEvent *event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ChoiceButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool on = isChecked();
    const bool keyFocus = hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange);
    const QRectF r(rect());
    const bool panel = CellStyle::onPanel(this);
    if (look == Box) {
        // a quiet field; the chosen one has the ring of the selection
        const QColor fill = panel ? (hovered ? t.raisedHover : t.surfaceAlt) : (hovered ? t.surfaceAlt : t.surface);
        CellStyle::paintCut(&p, r, 8, fill, on || keyFocus ? t.accent : QColor(), 2);
    } else if (on || hovered || keyFocus) {
        // a segment: the chosen one is a tab of its own (cut 6), on the group's fill
        const QColor tab = panel ? t.surface : t.surfaceAlt;
        CellStyle::paintCut(&p, r, 6, on ? tab : QColor(), keyFocus ? t.accent : QColor(), 2);
    }
    const QColor textColor = !isEnabled() ? t.textDisabled : (on || hovered ? t.text : t.label);
    const QStringList lines = text().split('\n');
    p.setPen(textColor);
    if (lines.size() == 1) {
        p.setFont(GUIUtil::appFont(13, QFont::Medium));
        p.drawText(rect(), Qt::AlignCenter, lines[0]);
    } else {
        p.setFont(GUIUtil::appFont(13, QFont::Medium));
        p.drawText(QRect(0, height() / 2 - 20, width(), 20), Qt::AlignCenter, lines[0]);
        p.setFont(GUIUtil::appFont(12));
        p.setPen(isEnabled() ? t.muted : t.textDisabled);
        p.drawText(QRect(0, height() / 2, width(), 18), Qt::AlignCenter, lines[1]);
    }
}

ChipButton::ChipButton(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ChipButton::setText(const QString& _text)
{
    text = _text;
    updateGeometry();
    update();
}

QSize ChipButton::sizeHint() const
{
    return QSize(QFontMetrics(GUIUtil::appFont(12, QFont::Medium)).horizontalAdvance(text) + 12 + 26, 26);
}

void ChipButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    CellStyle::paintCut(&p, QRectF(rect()), 6, t.accentSoft);
    p.setPen(t.accent);
    p.setFont(GUIUtil::appFont(12, QFont::Medium));
    p.drawText(QRect(12, 0, width() - 12 - 24, height()), Qt::AlignLeft | Qt::AlignVCenter, text);
    p.drawPixmap(width() - 21, (height() - 12) / 2, GUIUtil::lineIcon("remove", 12, t.accent, 2.0));
}

void ChipButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (!rect().contains(event->pos()))
        return;
    if (event->pos().x() >= width() - 26)
        Q_EMIT removeClicked();
    else
        Q_EMIT clicked();
}

SummaryCard::SummaryCard(const QString& title, QWidget *parent) : QFrame(parent)
{
    setProperty("role", "card");
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(0);

    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setProperty("role", "title");
    titleLabel->setFont(GUIUtil::displayFont(17, QFont::Medium));
    layout->addWidget(titleLabel);
    layout->addSpacing(16);

    // the total stands out
    QFrame *totalBox = new QFrame(this);
    totalBox->setProperty("role", "tile");
    QVBoxLayout *totalLayout = new QVBoxLayout(totalBox);
    totalLayout->setContentsMargins(16, 12, 16, 12);
    totalLayout->setSpacing(4);
    totalCaption = new QLabel(totalBox);
    totalCaption->setProperty("role", "caption");
    totalCaption->setFont(GUIUtil::appFont(13));
    total = new AmountLabel(28, QFont::Normal, totalBox);
    total->setUnitColor(GUIUtil::theme().textSecondary);
    total->setMinimumPixelSize(18);
    totalLayout->addWidget(totalCaption);
    totalLayout->addWidget(total);
    layout->addWidget(totalBox);
    layout->addSpacing(14);

    rowLayout = new QVBoxLayout();
    rowLayout->setSpacing(0);
    layout->addLayout(rowLayout);
    layout->addStretch(1);

    noteLabel = new QLabel(this);
    noteLabel->setProperty("role", "caption");
    noteLabel->setFont(GUIUtil::appFont(12));
    noteLabel->setWordWrap(true);
    noteLabel->hide();
    layout->addWidget(noteLabel);
    layout->addSpacing(12);
    footer = new QVBoxLayout();
    footer->setSpacing(8);
    layout->addLayout(footer);
}

void SummaryCard::setTotal(const QString& caption, const QString& amount, const QString& unit)
{
    totalCaption->setText(caption);
    total->setAmount(amount, unit);
}

void SummaryCard::setRows(const QList<Row>& rows)
{
    while (QLayoutItem *item = rowLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    const GUIUtil::Theme& t = GUIUtil::theme();
    bool first = true;
    for (const Row& row : rows) {
        QWidget *line = new QWidget(this);
        QVBoxLayout *outer = new QVBoxLayout(line);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        if (!first) {
            QFrame *separator = new QFrame(line);
            separator->setProperty("role", "hline");
            separator->setFixedHeight(1);
            outer->addWidget(separator);
        }
        first = false;
        QHBoxLayout *layout = new QHBoxLayout();
        layout->setContentsMargins(0, 9, 0, 9);
        layout->setSpacing(12);
        QLabel *caption = new QLabel(row.caption, line);
        caption->setProperty("role", "secondary");
        caption->setFont(GUIUtil::appFont(13));
        layout->addWidget(caption);
        layout->addStretch(1);
        if (row.isAmount) {
            AmountLabel *value = new AmountLabel(15, QFont::Medium, line);
            value->setMinimumPixelSize(12);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            value->setAmount(row.value, row.unit);
            if (row.alert)
                value->setColor(t.error);
            layout->addWidget(value);
        } else {
            QLabel *value = new QLabel(row.value, line);
            value->setFont(row.mono ? GUIUtil::monoFont(13, QFont::Medium) : GUIUtil::appFont(13, QFont::Medium));
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (row.alert)
                value->setStyleSheet(QString("QLabel { color: %1; }").arg(t.error.name()));
            layout->addWidget(value);
        }
        if (!row.toolTip.isEmpty())
            line->setToolTip(row.toolTip);
        outer->addLayout(layout);
        rowLayout->addWidget(line);
    }
}

void SummaryCard::setNote(const QString& note)
{
    noteLabel->setText(note);
    noteLabel->setVisible(!note.isEmpty());
}

void SummaryCard::addFooterWidget(QWidget *widget)
{
    footer->addWidget(widget);
}

StatsStrip::StatsStrip(QWidget *parent) : QFrame(parent)
{
    // the cells are panels of their own, on the ground
    setProperty("role", "plain");
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
}

int StatsStrip::addCell(const QString& caption, bool clickable)
{
    QHBoxLayout *strip = static_cast<QHBoxLayout*>(layout());
    // a stat cell of the guide: caption 13 muted, the figure in Space Grotesk; numbers only
    QFrame *cell = new QFrame(this);
    cell->setProperty("role", "card");
    QVBoxLayout *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(4);
    QHBoxLayout *captionRow = new QHBoxLayout();
    captionRow->setSpacing(4);
    QLabel *label = new QLabel(caption, cell);
    label->setProperty("role", "caption");
    label->setStyleSheet("QLabel { font-size: 13px; }");
    captionRow->addWidget(label);
    captionRow->addStretch();
    if (clickable) {
        QLabel *chevron = new QLabel(cell);
        chevron->setPixmap(GUIUtil::lineIcon("chevron-right", 12, GUIUtil::theme().accent, 2.0));
        captionRow->addWidget(chevron);
        cell->setCursor(Qt::PointingHandCursor);
        cell->setProperty("cell", cells.size());
        cell->installEventFilter(this);
    }
    layout->addLayout(captionRow);
    AmountLabel *value = new AmountLabel(26, QFont::Normal, cell);
    value->setMinimumPixelSize(14);
    value->setAmount(QString::fromUtf8("–"));
    layout->addWidget(value);
    strip->addWidget(cell, 1);
    cells << cell;
    values << value;
    return cells.size() - 1;
}

void StatsStrip::setValue(int cell, const QString& amount, const QString& unit, const QString& toolTip)
{
    if (cell >= 0 && cell < values.size()) {
        values[cell]->setAmount(amount, unit);
        values[cell]->setToolTip(toolTip);
    }
}

void StatsStrip::setCompact(bool compact)
{
    for (QWidget *cell : cells)
        cell->layout()->setContentsMargins(16, compact ? 12 : 16, 16, compact ? 12 : 16);
    for (AmountLabel *value : values)
        value->setPixelSize(compact ? 20 : 26);
}

bool StatsStrip::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease && object->property("cell").isValid())
        Q_EMIT cellClicked(object->property("cell").toInt());
    return QFrame::eventFilter(object, event);
}

static const int PICK = 40;     // size of a token button (a hexagon as high as this)
static const int PICK_GAP = 10;
static const int PICK_MAX = TokenLimits::ICON_ROW; // root tokens shown without the list

TokenPicker::TokenPicker(QWidget *parent) : QWidget(parent), index(nullptr), hover(-1)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void TokenPicker::setIndex(HeldTokenIndex *_index)
{
    if (index == _index || !_index)
        return;
    index = _index;
    connect(index, SIGNAL(changed()), this, SLOT(indexChanged()));
    indexChanged();
}

void TokenPicker::indexChanged()
{
    if (!index)
        return;
    // the chosen token may be gone
    HeldToken token;
    if (!selected.isEmpty() && !(index->find(selected, &token) && token.held)) {
        selected.clear();
        Q_EMIT tokenSelected(selected);
    }
    updateGeometry();
    update();
}

bool TokenPicker::isEmpty() const
{
    return shown().isEmpty();
}

void TokenPicker::setCurrent(const QString& name)
{
    HeldToken token;
    const QString next = index && index->find(name, &token) && token.held ? name : QString();
    if (selected == next)
        return;
    selected = next;
    update();
    Q_EMIT tokenSelected(selected);
}

/** The first root tokens; the root of the chosen token takes the last place when it is not among them. */
QStringList TokenPicker::shown() const
{
    QStringList list;
    if (!index)
        return list;
    // Only what can be sent: a root with a balance of its own, or with tokens held below it
    // (these open the list of the sub tokens). A root of which only the owner token is held
    // is not listed; owner tokens are transferred with -advancedui.
    for (const RootToken& root : index->roots()) {
        if (list.size() >= PICK_MAX)
            break;
        if (root.held || root.children > 0)
            list << root.name;
    }
    const QString selectedRoot = TokenNames::root(selected);
    if (!selected.isEmpty() && !list.contains(selectedRoot)) {
        if (list.size() >= PICK_MAX)
            list.removeLast();
        list << selectedRoot;
    }
    return list;
}

QRect TokenPicker::itemRect(int item, int width) const
{
    const int perRow = qMax(1, (width + PICK_GAP) / (PICK + PICK_GAP));
    return QRect((item % perRow) * (PICK + PICK_GAP), (item / perRow) * (PICK + PICK_GAP), PICK, PICK);
}

int TokenPicker::heightForWidth(int width) const
{
    const int count = shown().size() + 1; // and the "+"
    const int perRow = qMax(1, (width + PICK_GAP) / (PICK + PICK_GAP));
    const int rows = (count + perRow - 1) / perRow;
    return rows * PICK + (rows - 1) * PICK_GAP;
}

QSize TokenPicker::sizeHint() const
{
    return QSize(400, heightForWidth(width() > 0 ? width() : 400));
}

int TokenPicker::itemAt(const QPoint& pos) const
{
    const int count = shown().size() + 1;
    for (int i = 0; i < count; ++i)
        if (itemRect(i, width()).contains(pos))
            return i;
    return -1;
}

void TokenPicker::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QStringList list = shown();
    const QString selectedRoot = TokenNames::root(selected);
    for (int i = 0; i <= list.size(); ++i) {
        const QRect r = itemRect(i, width());
        // a hexagon as high as the button, at the left edge of its square
        const QRectF cell(r.left(), r.top(), PICK * 0.875, PICK);
        const QPainterPath hex = CellStyle::hexagon(cell);
        if (i == list.size()) {
            // "+": the list with the search field
            p.setOpacity(1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(hover == i ? t.raisedHover : t.surfaceAlt);
            p.drawPath(hex);
            p.drawPixmap(QPointF(cell.center().x() - 8, cell.center().y() - 8),
                         GUIUtil::lineIcon("plus", 16, hover == i ? t.accent : t.label, 2.0));
            continue;
        }
        const QString& name = list[i];
        const bool chosen = !selected.isEmpty() && name == selectedRoot;
        // what is not chosen is dimmed; the chosen token, and the one under the pointer, are in full color
        const bool vivid = chosen || hover == i;
        p.setOpacity(vivid ? 1.0 : 0.5);
        if (name == "PLB") {
            CellStyle::paintMark(&p, cell);
        } else {
            // the token avatar: a raised hexagon with the name (short names) or its initial
            p.setPen(Qt::NoPen);
            p.setBrush(t.surfaceAlt);
            p.drawPath(hex);
            p.setPen(vivid ? t.text : t.textSecondary);
            QFont font = GUIUtil::displayFont(name.size() <= 3 ? 11 : 9, QFont::Medium);
            p.setFont(font);
            p.drawText(cell, Qt::AlignCenter, QFontMetrics(font).elidedText(name, Qt::ElideRight, int(cell.width()) - 8));
        }
        p.setOpacity(1.0);
        if (chosen) {
            // the ring of the selection, inside the edge of the hexagon
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent, 2.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p.drawPath(CellStyle::hexagon(cell.adjusted(1.5, 1.5, -1.5, -1.5)));
        }
    }
}

void TokenPicker::mouseMoveEvent(QMouseEvent *event)
{
    const int item = itemAt(event->pos());
    if (item != hover) {
        hover = item;
        setCursor(item >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TokenPicker::leaveEvent(QEvent*)
{
    hover = -1;
    update();
}

void TokenPicker::mouseReleaseEvent(QMouseEvent *event)
{
    const QStringList list = shown();
    const int item = itemAt(event->pos());
    if (item < 0 || !index)
        return;
    if (item == list.size()) {
        TokenListDialog dialog(index, QString(), TokenListDialog::Choose, this);
        if (dialog.exec() == QDialog::Accepted)
            setCurrent(dialog.chosen());
        return;
    }
    const RootToken *root = index->findRoot(list[item]);
    if (root && root->held)
        setCurrent(root->name);
    else if (root && root->children > 0)
        chooseBelow(root->name); // only tokens below this root are held
}

void TokenPicker::chooseBelow(const QString& root)
{
    if (!index)
        return;
    TokenListDialog dialog(index, root, TokenListDialog::Choose, this);
    if (dialog.exec() == QDialog::Accepted)
        setCurrent(dialog.chosen());
}

bool TokenPicker::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *help = static_cast<QHelpEvent*>(event);
        const QStringList list = shown();
        const int item = itemAt(help->pos());
        if (item >= 0 && item < list.size())
            QToolTip::showText(help->globalPos(), list[item], this);
        else if (item == list.size())
            QToolTip::showText(help->globalPos(), tr("Search all tokens of the wallet, also sub tokens and unique tokens"), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

HexCells::HexCells(Layout _layout, QWidget *parent) : QWidget(parent), layoutKind(_layout), minimumCells(1), hover(-1)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HexCells::setCells(const QList<Cell>& cells)
{
    items = cells;
    hover = -1;
    updateGeometry();
    update();
}

void HexCells::setMinimumCells(int count)
{
    minimumCells = qMax(1, count);
    updateGeometry();
}

QSizeF HexCells::cellSize(bool large) const
{
    // as in the cell mock-ups: 88 x 100 for a block, 132 x 150 for the newest, 96 x 110 for an asset
    if (layoutKind == Strip)
        return large ? QSizeF(132, 150) : QSizeF(88, 100);
    return QSizeF(96, 110);
}

/** Vertical offset of the cells of the lower row */
static qreal lowerRow(HexCells::Layout layout)
{
    return layout == HexCells::Strip ? 46 : 26;
}

qreal HexCells::widthFor(int count) const
{
    if (count <= 0)
        return 0;
    if (layoutKind == Strip)
        return (count - 1) * 80 + cellSize(true).width();
    return (count - 1) * 102 + cellSize(false).width();
}

QSize HexCells::sizeHint() const
{
    const qreal height = layoutKind == Strip ? lowerRow(Strip) + cellSize(true).height() : lowerRow(Assets) + cellSize(false).height();
    return QSize(qCeil(widthFor(qMax(items.size(), minimumCells))), qCeil(height));
}

QSize HexCells::minimumSizeHint() const
{
    // smaller down to 80 %, then the first cells are left out
    return QSize(qCeil(widthFor(minimumCells) * 0.8), sizeHint().height());
}

QList<QRectF> HexCells::placement(int *first) const
{
    QList<QRectF> rects;
    *first = 0;
    const int n = items.size();
    if (n == 0)
        return rects;
    int count = n;
    qreal scale = qMin<qreal>(1.0, width() / widthFor(count));
    while (scale < 0.8 && count > minimumCells) {
        --count;
        scale = qMin<qreal>(1.0, width() / widthFor(count));
    }
    *first = n - count;
    const qreal fullHeight = sizeHint().height();
    const qreal y0 = (height() - fullHeight * scale) / 2;
    // the strip is aligned to the right (the newest block at the right edge), the assets to the left
    const qreal x0 = layoutKind == Strip ? width() - widthFor(count) * scale : 0;
    for (int i = 0; i < count; ++i) {
        const bool large = layoutKind == Strip && i == count - 1;
        const QSizeF size = cellSize(large) * scale;
        // the cells alternate between two rows; in the strip the newest is in the lower one
        const bool lower = layoutKind == Strip ? ((count - 1 - i) % 2 == 0) : (i % 2 == 1);
        const qreal x = x0 + (layoutKind == Strip ? i * 80 : i * 102) * scale;
        const qreal y = y0 + (lower ? lowerRow(layoutKind) * scale : 0);
        rects << QRectF(QPointF(x, y), size);
    }
    return rects;
}

int HexCells::cellAt(const QPoint& pos) const
{
    int first = 0;
    const QList<QRectF> rects = placement(&first);
    for (int i = 0; i < rects.size(); ++i)
        if (CellStyle::hexagon(rects[i]).contains(QPointF(pos)))
            return first + i;
    return -1;
}

void HexCells::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);
    int first = 0;
    const QList<QRectF> rects = placement(&first);
    for (int i = 0; i < rects.size(); ++i) {
        const Cell& cell = items[first + i];
        const QRectF r = rects[i];
        const qreal scale = r.height() / cellSize(layoutKind == Strip && i == rects.size() - 1).height();
        QColor fill, top, bottom;
        switch (cell.kind) {
        case Newest: fill = t.action; top = t.onAction; bottom = t.onAction; bottom.setAlphaF(0.72); break;
        case Recent: fill = t.fill; top = t.onFill; bottom = QColor(214, 220, 240); break;
        case Settled: fill = t.surface; top = t.text; bottom = t.muted; break;
        case Old: fill = t.surfaceAlt; top = t.textSecondary; bottom = t.muted; break;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(CellStyle::hexagon(r));
        if (first + i == hover && !cell.key.isEmpty()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent, 2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p.drawPath(CellStyle::hexagon(r.adjusted(1.5, 1.5, -1.5, -1.5)));
        }
        const bool large = layoutKind == Strip && i == rects.size() - 1;
        QFont topFont, bottomFont;
        if (layoutKind == Strip) {
            // heights in the fixed-width font, counts below
            topFont = GUIUtil::monoFont(qMax(9, qRound((large ? 15 : 12) * scale)), QFont::Medium);
            bottomFont = GUIUtil::appFont(qMax(9, qRound((large ? 12 : 11) * scale)));
        } else {
            topFont = GUIUtil::displayFont(qMax(10, qRound(14 * scale)), QFont::Medium);
            bottomFont = GUIUtil::monoFont(qMax(9, qRound(12 * scale)));
        }
        const QFontMetrics topMetrics(topFont), bottomMetrics(bottomFont);
        const qreal gap = 2 * scale;
        const qreal block = topMetrics.height() + gap + bottomMetrics.height();
        const qreal textTop = r.center().y() - block / 2;
        const int textWidth = int(r.width() * 0.8);
        const QRectF topRect(r.center().x() - textWidth / 2.0, textTop, textWidth, topMetrics.height());
        const QRectF bottomRect(topRect.left(), textTop + topMetrics.height() + gap, textWidth, bottomMetrics.height());
        p.setFont(topFont);
        p.setPen(top);
        p.drawText(topRect, Qt::AlignCenter, topMetrics.elidedText(cell.top, Qt::ElideRight, textWidth));
        p.setFont(bottomFont);
        p.setPen(bottom);
        p.drawText(bottomRect, Qt::AlignCenter, bottomMetrics.elidedText(cell.bottom, Qt::ElideRight, textWidth));
    }
}

void HexCells::mouseMoveEvent(QMouseEvent *event)
{
    const int cell = cellAt(event->pos());
    if (cell != hover) {
        hover = cell;
        setCursor(cell >= 0 && !items[cell].key.isEmpty() ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void HexCells::leaveEvent(QEvent*)
{
    hover = -1;
    update();
}

void HexCells::mouseReleaseEvent(QMouseEvent *event)
{
    const int cell = cellAt(event->pos());
    if (cell >= 0 && !items[cell].key.isEmpty())
        Q_EMIT cellClicked(items[cell].key);
}

bool HexCells::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *help = static_cast<QHelpEvent*>(event);
        const int cell = cellAt(help->pos());
        if (cell >= 0 && !items[cell].toolTip.isEmpty())
            QToolTip::showText(help->globalPos(), items[cell].toolTip, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

bool PageDialogs::askLabel(QWidget *parent, QString& label)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QApplication::translate("PageDialogs", "Label"));
    dialog.setMinimumWidth(380);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    QLabel *text = new QLabel(QApplication::translate("PageDialogs", "A label for this address. It is saved in your address book and shown instead of the address in your lists. It is not sent to anyone."), &dialog);
    text->setWordWrap(true);
    text->setProperty("role", "secondary");
    layout->addWidget(text);
    QLineEdit *edit = new QLineEdit(label, &dialog);
    edit->setPlaceholderText(QApplication::translate("PageDialogs", "For example: Minsu, exchange deposit"));
    edit->setMinimumHeight(40);
    layout->addWidget(edit);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    edit->setFocus();
    edit->selectAll();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    label = edit->text().trimmed();
    return true;
}

bool PageDialogs::askLockTime(QWidget *parent, QDateTime& until)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QApplication::translate("PageDialogs", "Send with a lock"));
    dialog.setMinimumWidth(420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);
    QLabel *text = new QLabel(QApplication::translate("PageDialogs", "The recipient receives the amount now but cannot spend it before the time you choose here."), &dialog);
    text->setWordWrap(true);
    text->setProperty("role", "secondary");
    layout->addWidget(text);

    const QDateTime now = QDateTime::currentDateTime();
    QDateTimeEdit *edit = new QDateTimeEdit(until.isValid() && until > now ? until : now.addMonths(1), &dialog);
    edit->setDisplayFormat("yyyy-MM-dd HH:mm");
    edit->setCalendarPopup(true);
    edit->setMinimumDateTime(now);
    edit->setFont(GUIUtil::monoFont(14));
    edit->setMinimumHeight(40);
    layout->addWidget(edit);

    QHBoxLayout *quick = new QHBoxLayout();
    quick->setSpacing(8);
    const std::pair<QString, int> choices[] = {
        {QApplication::translate("PageDialogs", "1 month"), 1},
        {QApplication::translate("PageDialogs", "3 months"), 3},
        {QApplication::translate("PageDialogs", "6 months"), 6},
        {QApplication::translate("PageDialogs", "1 year"), 12},
    };
    for (const std::pair<QString, int>& choice : choices) {
        QPushButton *button = new QPushButton(choice.first, &dialog);
        button->setAutoDefault(false);
        button->setProperty("look", "soft");
        const int months = choice.second;
        QObject::connect(button, &QPushButton::clicked, [edit, months] { edit->setDateTime(QDateTime::currentDateTime().addMonths(months)); });
        quick->addWidget(button);
    }
    quick->addStretch();
    layout->addLayout(quick);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    until = edit->dateTime();
    return true;
}
