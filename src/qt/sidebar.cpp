// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sidebar.h"

#include "clientversion.h"

#include "cellstyle.h"
#include "guiutil.h"

#include <QAction>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QFrame>
#include <QScrollArea>
#include <QPainterPath>
#include <QRegExp>
#include <QVBoxLayout>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(44, 24);
    setFocusPolicy(Qt::TabFocus);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF track(rect());
    const QRectF knobOff(4, 4, 16, 16);
    const QRectF knobOn(width() - 20, 4, 16, 16);
    // a switch that cannot be used is a flat track with a dim knob
    if (!isEnabled()) {
        CellStyle::paintCut(&p, track, 6, t.sideTrack);
        CellStyle::paintCut(&p, knobOff, 4, t.sideOff);
        return;
    }
    // on: the mark blue track (also in the dark theme, where it carries the white knob)
    CellStyle::paintCut(&p, track, 6, isChecked() ? t.fill : t.sideOff, hasFocus() ? t.accent : QColor(), 2);
    CellStyle::paintCut(&p, isChecked() ? knobOn : knobOff, 4, QColor("#FFFFFF"));
}

SideNavButton::SideNavButton(QAction* _action, const QString& _iconName, QWidget* parent) :
    QAbstractButton(parent), action(_action), iconName(_iconName), hovered(false)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    // 32: the twelve pages fit into the side bar of a window of the usual size
    setFixedHeight(32);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::TabFocus);
    connect(this, SIGNAL(clicked()), action, SLOT(trigger()));
    connect(action, SIGNAL(changed()), this, SLOT(actionChanged()));
    connect(action, SIGNAL(toggled(bool)), this, SLOT(actionChanged()));
    actionChanged();
}

/** Text of an action without its mnemonic, also in the form "개요(O)" that translations use. */
static QString plainActionText(const QAction* action)
{
    QString text = action->iconText();
    text.remove(QRegExp("\\s*\\(\\w\\)$"));
    return text;
}

void SideNavButton::actionChanged()
{
    setChecked(action->isChecked());
    setEnabled(action->isEnabled());
    // the tool tip names the shortcut of the page
    QString tip = action->toolTip();
    if (!action->shortcut().isEmpty())
        tip += QString(" (%1)").arg(action->shortcut().toString(QKeySequence::NativeText));
    setToolTip(tip);
    setText(plainActionText(action));
    update();
}

void SideNavButton::enterEvent(QEvent* event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void SideNavButton::leaveEvent(QEvent* event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void SideNavButton::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    // the button state follows the action; a click must not uncheck the active page
    const bool on = action->isChecked();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // text links; the page that is shown is a tab of the raised color, marked with a small yellow
    // hexagon (the column of the mark is kept on every row, so that the names line up)
    const bool keyFocus = hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange);
    if (on || keyFocus)
        CellStyle::paintCut(&p, QRectF(rect()), 6, on ? t.sideOnBg : QColor(), keyFocus ? t.accent : QColor(), 2);
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(t.action);
        p.drawPath(CellStyle::hexagon(QRectF(12, (height() - 11) / 2.0, 10, 11)));
    }
    QColor color = (on || hovered || keyFocus) ? t.sideStrong : t.sideText;
    if (!isEnabled())
        color = t.textDisabled;
    p.setPen(color);
    p.setFont(GUIUtil::appFont(14, on ? QFont::Medium : QFont::Normal));
    p.drawText(QRect(32, 0, width() - 40, height()), Qt::AlignLeft | Qt::AlignVCenter, plainActionText(action));
}

WalletSwitcher::WalletSwitcher(QWidget* parent) : QAbstractButton(parent), hovered(false), keyFocus(false)
{
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::TabFocus);
}

void WalletSwitcher::setWallet(const QString& _name, const QString& _state)
{
    name = _name;
    state = _state;
    setAccessibleName(tr("Wallet: %1").arg(name));
    update();
}

void WalletSwitcher::enterEvent(QEvent* event)
{
    hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void WalletSwitcher::leaveEvent(QEvent* event)
{
    hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void WalletSwitcher::focusInEvent(QFocusEvent* event)
{
    // It is the first control of the window, so the window gives it the focus when it opens:
    // that is not a choice of the user and is not drawn.
    keyFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason
               || event->reason() == Qt::ShortcutFocusReason;
    QAbstractButton::focusInEvent(event);
}

void WalletSwitcher::focusOutEvent(QFocusEvent* event)
{
    keyFocus = false;
    QAbstractButton::focusOutEvent(event);
}

/** One or two letters of a wallet name, for its mark */
static QString walletInitials(const QString& name)
{
    const QStringList words = name.split(' ', QString::SkipEmptyParts);
    QString letters;
    if (words.size() >= 2)
        letters = QString(words[0].at(0)) + words[1].at(0);
    else if (!words.isEmpty())
        letters = words[0].left(1);
    return letters.toUpper();
}

void WalletSwitcher::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // a quiet button on the rail: raised, darker under the pointer; the keyboard focus is a ring
    CellStyle::paintCut(&p, QRectF(rect()), 8, (hovered || isDown()) ? t.raisedHover : t.surfaceAlt,
                        hasFocus() && keyFocus ? t.accent : QColor(), 2);

    // the first letters of the name on a small cell
    const QRectF mark(12, (height() - 32) / 2.0, 32, 32);
    CellStyle::paintCut(&p, mark, 6, t.surface);
    p.setPen(t.sideStrong);
    p.setFont(GUIUtil::displayFont(13, QFont::Medium));
    p.drawText(mark, Qt::AlignCenter, walletInitials(name));

    // name and state on two lines of 18, centered in the 56 of the box
    const int left = 56;
    const int right = width() - 36;
    const QFont nameFont = GUIUtil::appFont(14, QFont::Medium);
    const QFont stateFont = GUIUtil::appFont(12);
    p.setFont(nameFont);
    p.setPen(t.sideStrong);
    p.drawText(QRect(left, 10, right - left, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, right - left));
    p.setFont(stateFont);
    p.setPen(t.muted);
    p.drawText(QRect(left, 28, right - left, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(stateFont).elidedText(state, Qt::ElideRight, right - left));
    p.drawPixmap(width() - 28, (height() - 16) / 2, GUIUtil::lineIcon("chevron", 16, t.muted, 1.5));
}

SideProgress::SideProgress(QWidget* parent) : QWidget(parent), value(0)
{
    setFixedHeight(4);
}

void SideProgress::setValue(double fraction)
{
    value = qBound(0.0, fraction, 1.0);
    update();
}

void SideProgress::paintEvent(QPaintEvent*)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    QPainter p(this);
    p.setPen(Qt::NoPen);
    p.setBrush(t.sideTrack);
    p.drawRect(rect());
    if (value > 0) {
        p.setBrush(t.sideBar);
        p.drawRect(QRectF(0, 0, qMax(4.0, width() * value), height()));
    }
}

static QLabel* sideLabel(const QString& text, const QColor& color, int px, int weight)
{
    QLabel* label = new QLabel(text);
    label->setFont(GUIUtil::appFont(px, weight));
    label->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(color.name()));
    return label;
}

Sidebar::Sidebar(QWidget* parent) : QWidget(parent), walletCount(0)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    setObjectName("sidebar");
    setFixedWidth(240);
    // a rail in the cell color: what is on it is on a panel (raised fields and buttons)
    setProperty("cellSurface", true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 24, 16, 20);
    layout->setSpacing(0);

    // the mark and the name
    QHBoxLayout* brand = new QHBoxLayout();
    brand->setContentsMargins(8, 0, 8, 0);
    brand->setSpacing(10);
    QLabel* logo = new QLabel();
    logo->setFixedSize(28, 32);
    {
        const qreal dpr = devicePixelRatioF();
        QPixmap mark(QSize(28, 32) * dpr);
        mark.setDevicePixelRatio(dpr);
        mark.fill(Qt::transparent);
        QPainter p(&mark);
        CellStyle::paintMark(&p, QRectF(0, 0, 28, 32));
        p.end();
        logo->setPixmap(mark);
    }
    brand->addWidget(logo);
    QLabel* name = new QLabel("Paladeum");
    name->setFont(GUIUtil::displayFont(19, QFont::Medium));
    name->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(t.sideStrong.name()));
    brand->addWidget(name);
    brand->addStretch();
    layout->addLayout(brand);
    layout->addSpacing(20);

    // the wallet that is shown; the menu switches, creates, opens and closes wallets
    walletSwitcher = new WalletSwitcher(this);
    walletSwitcher->setToolTip(tr("The wallet shown. Click to switch to another wallet, or to create, open or close one."));
    layout->addWidget(walletSwitcher);
    layout->addSpacing(12);
    connect(walletSwitcher, SIGNAL(clicked()), this, SIGNAL(walletClicked()));

    // the pages; the list scrolls when the window is too low for all of them
    QWidget* navBody = new QWidget();
    navBody->setObjectName("sideNav");
    navBody->setStyleSheet("QWidget#sideNav { background: transparent; }");
    QVBoxLayout* navOuter = new QVBoxLayout(navBody);
    navOuter->setContentsMargins(0, 0, 0, 0);
    navOuter->setSpacing(0);
    navLayout = new QVBoxLayout();
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(2);
    navOuter->addLayout(navLayout);
    navOuter->addStretch(1);
    QScrollArea* navScroll = new QScrollArea();
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navScroll->setStyleSheet(QString("QScrollArea { background: transparent; } QScrollBar:vertical { width: 6px; margin: 0; }"
                                     " QScrollBar::handle:vertical { background: %1; }").arg(t.borderStrong.name()));
    navScroll->viewport()->setAutoFillBackground(false);
    navScroll->setWidget(navBody);
    navScroll->setMinimumHeight(48);
    layout->addWidget(navScroll, 1);
    layout->addSpacing(12);

    // staking
    stakingCard = new QWidget();
    stakingCard->setObjectName("sideCard");
    stakingCard->setAttribute(Qt::WA_StyledBackground, true);
    stakingCard->setStyleSheet(QString("QWidget#sideCard { background: transparent; border-top: 1px solid %1; }").arg(t.sideCard.name()));
    QVBoxLayout* stakingLayout = new QVBoxLayout(stakingCard);
    stakingLayout->setContentsMargins(8, 14, 8, 6);
    stakingLayout->setSpacing(8);
    QHBoxLayout* stakingRow = new QHBoxLayout();
    QLabel* stakingTitle = new QLabel(tr("Staking"));
    stakingTitle->setFont(GUIUtil::displayFont(15, QFont::Medium));
    stakingTitle->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg(t.sideStrong.name()));
    stakingRow->addWidget(stakingTitle);
    stakingRow->addStretch();
    stakingSwitch = new ToggleSwitch();
    stakingSwitch->setAccessibleName(tr("Staking"));
    stakingRow->addWidget(stakingSwitch);
    stakingLayout->addLayout(stakingRow);
    stakingDetail = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    stakingDetail->setWordWrap(true);
    stakingLayout->addWidget(stakingDetail);
    layout->addWidget(stakingCard);
    layout->addSpacing(12);
    connect(stakingSwitch, SIGNAL(clicked()), this, SIGNAL(stakingToggled()));

    // synchronization
    syncBox = new QWidget();
    syncBox->setCursor(Qt::PointingHandCursor);
    syncBox->installEventFilter(this);
    QVBoxLayout* syncLayout = new QVBoxLayout(syncBox);
    syncLayout->setContentsMargins(8, 0, 8, 0);
    syncLayout->setSpacing(8);
    QHBoxLayout* syncRow = new QHBoxLayout();
    syncStatus = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    syncRemaining = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    syncRow->addWidget(syncStatus);
    syncRow->addStretch();
    syncRow->addWidget(syncRemaining);
    syncLayout->addLayout(syncRow);
    syncProgress = new SideProgress();
    syncLayout->addWidget(syncProgress);
    layout->addWidget(syncBox);
    layout->addSpacing(8);

    networkLabel = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    networkLabel->setContentsMargins(8, 0, 8, 0);
    networkLabel->setCursor(Qt::PointingHandCursor);
    networkLabel->installEventFilter(this);
    layout->addWidget(networkLabel);

    walletLabel = sideLabel(QString(), t.sideText, 12, QFont::Normal);
    walletLabel->setContentsMargins(8, 4, 8, 0);
    walletLabel->hide();
    layout->addWidget(walletLabel);

    // warnings of the node, such as the notice of a pre-release build: small, at the very bottom
    noticeLabel = sideLabel(QString(), t.muted, 11, QFont::Normal);
    noticeLabel->setContentsMargins(8, 6, 8, 0);
    noticeLabel->setWordWrap(true);
    noticeLabel->hide();
    layout->addWidget(noticeLabel);

    // which build this is: small, at the very bottom
    QLabel* versionLabel = new QLabel(QString::fromStdString(FormatFullVersion()));
    versionLabel->setFont(GUIUtil::monoFont(10));
    versionLabel->setStyleSheet(QString("color: %1; background: transparent;").arg(t.muted.name()));
    versionLabel->setContentsMargins(8, 8, 8, 0);
    versionLabel->setToolTip(tr("Version of this program"));
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(versionLabel);
}

void Sidebar::addPage(QAction* action, const QString& iconName)
{
    navLayout->addWidget(new SideNavButton(action, iconName, this));
}

void Sidebar::addSeparator()
{
    QWidget* box = new QWidget(this);
    box->setFixedHeight(17);
    QVBoxLayout* boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(8, 8, 8, 8);
    QFrame* rule = new QFrame(box);
    rule->setFixedHeight(1);
    rule->setStyleSheet(QString("background-color: %1; border: none;").arg(GUIUtil::theme().sideCard.name()));
    boxLayout->addWidget(rule);
    navLayout->addWidget(box);
}

void Sidebar::setStaking(bool on, const QString& detail, const QString& toolTip, bool available)
{
    stakingSwitch->setChecked(on);
    // without an address that may stake the switch would do nothing: it cannot be used then
    stakingSwitch->setEnabled(available || on);
    stakingSwitch->setCursor(available || on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    stakingDetail->setText(detail);
    stakingCard->setToolTip(toolTip);
    stakingSwitch->setToolTip(toolTip);
}

void Sidebar::setStakingVisible(bool visible)
{
    stakingCard->setVisible(visible);
}

void Sidebar::setSyncState(const QString& status, const QString& remaining, double progress, bool showProgress, const QString& toolTip)
{
    syncStatus->setText(status);
    syncRemaining->setText(remaining);
    syncProgress->setValue(showProgress ? progress : 1.0);
    syncBox->setToolTip(toolTip);
}

void Sidebar::setNetworkState(const QString& text, const QString& toolTip)
{
    networkLabel->setText(text);
    networkLabel->setToolTip(toolTip);
}

void Sidebar::setWalletState(const QString& text, const QString& toolTip)
{
    // the state of the wallet is the second line of the wallet at the top
    walletStateText = text.isEmpty() ? tr("Not encrypted") : text;
    walletLabel->hide();
    walletSwitcher->setWallet(walletName, walletStateText);
    walletSwitcher->setToolTip((toolTip.isEmpty() ? tr("This wallet has no passphrase.") : toolTip) + "<br><br>" +
        tr("Click to switch to another wallet, or to create, open or close one."));
}

void Sidebar::setWalletName(const QString& name, int count)
{
    walletName = name;
    walletCount = count;
    walletSwitcher->setWallet(walletName, walletStateText);
}

QPoint Sidebar::walletMenuPosition() const
{
    return walletSwitcher->mapToGlobal(QPoint(0, walletSwitcher->height() + 4));
}

int Sidebar::walletMenuWidth() const
{
    return walletSwitcher->width();
}

void Sidebar::setNotice(const QString& text, const QString& toolTip, bool warning)
{
    const GUIUtil::Theme& t = GUIUtil::theme();
    noticeLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }").arg((warning ? t.error : t.muted).name()));
    // at most two short lines; the tool tip has the whole text
    const QFontMetrics fm(noticeLabel->font());
    const int width = 232 - 32 - 16;
    QString first = text, second;
    if (fm.horizontalAdvance(text) > width) {
        int cut = text.size();
        while (cut > 0 && fm.horizontalAdvance(text.left(cut)) > width)
            --cut;
        const int space = text.lastIndexOf(' ', cut);
        if (space > cut / 2)
            cut = space;
        first = text.left(cut).trimmed();
        second = fm.elidedText(text.mid(cut).trimmed(), Qt::ElideRight, width);
    }
    noticeLabel->setText(second.isEmpty() ? first : first + "\n" + second);
    noticeLabel->setToolTip(toolTip);
    noticeLabel->setVisible(!text.isEmpty());
}

void Sidebar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), GUIUtil::theme().sideBg);
}

bool Sidebar::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        if (object == syncBox)
            Q_EMIT syncClicked();
        else if (object == networkLabel)
            Q_EMIT networkClicked();
    }
    return QWidget::eventFilter(object, event);
}
