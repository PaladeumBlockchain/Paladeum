// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_QT_PAGEWIDGETS_H
#define PLB_QT_PAGEWIDGETS_H

#include <QAbstractButton>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class HeldTokenIndex;
class QBoxLayout;
class QLabel;
class QVBoxLayout;

/**
 * Shows a figure the way all pages do: in the font of figures (Space Grotesk), the fraction (the
 * point and the digits after it) at 60 % of the size of the integer part on the same base line,
 * optionally followed by a unit. See GUIUtil::paintAmount().
 */
class AmountLabel : public QWidget
{
public:
    explicit AmountLabel(int pixelSize = 14, int weight = QFont::DemiBold, QWidget *parent = nullptr);

    void setAmount(const QString& amount, const QString& unit = QString());
    void setColor(const QColor& color);
    void setUnitColor(const QColor& color);
    void setAlignment(Qt::Alignment alignment);
    /** Let a long amount get smaller, down to this pixel size, instead of being cut off */
    void setMinimumPixelSize(int px);
    /** The full size of the figure */
    void setPixelSize(int px);
    QString amount() const { return text; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QFont amountFont(int size) const;
    QFont unitFont(int size) const;
    /** Width of the amount and its unit at a pixel size */
    int widthAt(int size) const;
    /** The largest pixel size (down to minPx) at which the amount fits into the width */
    int fittingSize(int width) const;

    int px;
    int minPx;
    int weight;
    QString text;
    QString unit;
    QColor color;
    QColor unitColor;
    Qt::Alignment alignment;
};

/**
 * Keeps the items of a box layout side by side while the watched widget is at least minWidth
 * wide, and one above the other below that: buttons that do not fit next to each other in a
 * narrow window are not cut off.
 */
class BoxDirectionFollower : public QObject
{
public:
    /** owner: what the follower lives as long as (the watched widget when there is none) */
    BoxDirectionFollower(QWidget *watched, QBoxLayout *layout, int minWidth, QObject *owner = nullptr);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void follow();
    QWidget *watched;
    QPointer<QBoxLayout> layout;
    int minWidth;
};

/**
 * A choice among a few: a segment of a segmented control or one of the fee
 * choices, with an optional second line. It paints itself, so that its colors
 * do not depend on the precedence of style sheet rules.
 */
class ChoiceButton : public QAbstractButton
{
public:
    enum Look { Segment, Box };
    ChoiceButton(const QString& text, Look look, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Look look;
    bool hovered;
};

/** A small label with cut corners that can be clicked and, optionally, removed with its "x". */
class ChipButton : public QWidget
{
    Q_OBJECT

public:
    explicit ChipButton(QWidget *parent = nullptr);
    void setText(const QString& text);
    QSize sizeHint() const override;

Q_SIGNALS:
    void clicked();
    void removeClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QString text;
};

/** The summary next to a send form: a large total, rows below it, a note and the send button. */
class SummaryCard : public QFrame
{
public:
    struct Row {
        QString caption;
        QString value;      //!< an amount when isAmount, text otherwise
        QString unit;
        bool isAmount;
        bool mono;          //!< text in the fixed-width font (addresses)
        bool alert;         //!< in the error color
        QString toolTip;
        Row(const QString& _caption = QString(), const QString& _value = QString(), const QString& _unit = QString(), bool _isAmount = true) :
            caption(_caption), value(_value), unit(_unit), isAmount(_isAmount), mono(false), alert(false) {}
    };

    explicit SummaryCard(const QString& title, QWidget *parent = nullptr);
    void setTotal(const QString& caption, const QString& amount, const QString& unit);
    void setRows(const QList<Row>& rows);
    void setNote(const QString& note);
    /** Widgets at the bottom: the send button */
    void addFooterWidget(QWidget *widget);

private:
    QLabel *totalCaption;
    AmountLabel *total;
    QVBoxLayout *rowLayout;
    QLabel *noteLabel;
    QVBoxLayout *footer;
};

/**
 * A row of stat cells: each a panel of its own with a caption and a figure. The chain
 * status at the top of the overview and of the explorer.
 */
class StatsStrip : public QFrame
{
    Q_OBJECT

public:
    explicit StatsStrip(QWidget *parent = nullptr);
    /** Add a cell; a clickable one shows a chevron and emits cellClicked() */
    int addCell(const QString& caption, bool clickable = false);
    /** toolTip: the exact value of a figure that is shown shortened */
    void setValue(int cell, const QString& amount, const QString& unit = QString(), const QString& toolTip = QString());
    /** Less padding and smaller figures */
    void setCompact(bool compact);

Q_SIGNALS:
    void cellClicked(int cell);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QList<AmountLabel*> values;
    QList<QWidget*> cells;
};

/**
 * Hexagon cells of the design guide, for what the chain counts: the assets of the wallet (PLB,
 * then tokens) and the latest blocks. Each cell has a first line (a name or a height) and a second
 * one (an amount or a count). The cells alternate between two rows; with Strip the last one is
 * larger (the newest block). As many cells as fit into the width are shown: the first ones are
 * left out when it is too narrow for all.
 */
class HexCells : public QWidget
{
    Q_OBJECT

public:
    /** The fills of the guide: newest (yellow), recent and the PLB asset (mark blue), settled (the
        cell color), old and "more" (raised) */
    enum Kind { Newest, Recent, Settled, Old };
    enum Layout { Assets, Strip };
    struct Cell {
        QString top;
        QString bottom;
        QString toolTip;
        Kind kind = Settled;
        QString key;   //!< what a click on the cell reports
    };

    explicit HexCells(Layout layout, QWidget *parent = nullptr);
    void setCells(const QList<Cell>& cells);
    /** The fewest cells that are shown however narrow it gets */
    void setMinimumCells(int count);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void cellClicked(const QString& key);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    /** Where each shown cell is, for the width of the widget; the first ones may be left out */
    QList<QRectF> placement(int *first) const;
    int cellAt(const QPoint& pos) const;
    /** Width and height of the cells at full size */
    QSizeF cellSize(bool large) const;
    qreal widthFor(int count) const;

    Layout layoutKind;
    QList<Cell> items;
    int minimumCells;
    int hover;
};

/**
 * The root tokens of the wallet as hexagon avatars, at most 20; a "+"
 * opens a list with a search field, where sub tokens and unique tokens are
 * found as well. Sub and unique tokens are never buttons of their own: a
 * wallet can hold tens of thousands of them.
 */
class TokenPicker : public QWidget
{
    Q_OBJECT

public:
    explicit TokenPicker(QWidget *parent = nullptr);
    void setIndex(HeldTokenIndex *index);
    void setCurrent(const QString& name);
    QString current() const { return selected; }
    bool isEmpty() const;
    /** Open the list of the sub and unique tokens of a root and choose from it */
    void chooseBelow(const QString& root);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

Q_SIGNALS:
    void tokenSelected(const QString& name);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private Q_SLOTS:
    void indexChanged();

private:
    QStringList shown() const;
    QRect itemRect(int index, int width) const;
    int itemAt(const QPoint& pos) const;

    HeldTokenIndex *index;
    QString selected;
    int hover;
};

namespace PageDialogs
{
    /** Ask for the label of an address. Returns false when cancelled. */
    bool askLabel(QWidget *parent, QString& label);
    /** Ask until when coins or tokens stay locked for the recipient. Returns false when cancelled. */
    bool askLockTime(QWidget *parent, QDateTime& until);
}

#endif // PLB_QT_PAGEWIDGETS_H
