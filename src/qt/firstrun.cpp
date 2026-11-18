// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/paladeum-config.h"
#endif

#include "firstrun.h"

#include "cellstyle.h"
#include "guiutil.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

QPixmap WizardHeader::logo(int size, qreal dpr)
{
    // the mark, drawn from its outline (a hexagon as high as the square)
    QPixmap mark(QSize(size, size) * dpr);
    mark.setDevicePixelRatio(dpr);
    mark.fill(Qt::transparent);
    QPainter p(&mark);
    CellStyle::paintMark(&p, QRectF(0, 0, size, size));
    p.end();
    return mark;
}

WizardHeader::WizardHeader(Step step, const QString &title, const QString &subtitle, QWidget *parent) :
    QWidget(parent)
{
    const GUIUtil::Theme &t = GUIUtil::theme();
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    QHBoxLayout *brand = new QHBoxLayout();
    brand->setSpacing(12);
    QLabel *logoLabel = new QLabel(this);
    logoLabel->setFixedSize(44, 44);
    logoLabel->setPixmap(logo(44, devicePixelRatioF()));
    brand->addWidget(logoLabel);
    QVBoxLayout *words = new QVBoxLayout();
    words->setSpacing(0);
    QLabel *name = new QLabel(tr(PACKAGE_NAME), this);
    name->setFont(GUIUtil::appFont(13, QFont::DemiBold));
    name->setStyleSheet(QString("color: %1;").arg(t.textSecondary.name()));
    words->addWidget(name);
    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setFont(GUIUtil::appFont(20, QFont::DemiBold));
    titleLabel->setStyleSheet(QString("color: %1;").arg(t.text.name()));
    words->addWidget(titleLabel);
    brand->addLayout(words, 1);
    layout->addLayout(brand);

    // the steps: a numbered dot and a name each, the current one filled
    QHBoxLayout *steps = new QHBoxLayout();
    steps->setSpacing(8);
    const QStringList names = {tr("Start"), tr("Recovery words"), tr("Passphrase"), tr("Backup")};
    for (int i = 0; i < names.size(); ++i) {
        const bool current = i == step;
        const bool done = i < step;
        QLabel *dot = new QLabel(done ? QString::fromUtf8("✓") : QString::number(i + 1), this);
        dot->setFixedSize(22, 22);
        dot->setAlignment(Qt::AlignCenter);
        dot->setFont(GUIUtil::displayFont(11, QFont::Medium));
        // a small square with cut corners (painted from cellFill): mark blue for the step that is shown
        dot->setProperty("cellFill", current ? t.fill : done ? t.accentSoft : t.chip);
        dot->setProperty("cellCut", 6);
        dot->setStyleSheet(QString("background: transparent; color: %1;").arg((current ? t.onFill : done ? t.accent : t.chipText).name()));
        steps->addWidget(dot);
        QLabel *label = new QLabel(names.at(i), this);
        label->setFont(GUIUtil::appFont(12, current ? QFont::DemiBold : QFont::Medium));
        label->setStyleSheet(QString("color: %1;").arg((current ? t.text : t.textSecondary).name()));
        steps->addWidget(label);
        if (i + 1 < names.size()) {
            QFrame *rule = new QFrame(this);
            rule->setProperty("role", "hline");
            rule->setMinimumWidth(16);
            steps->addWidget(rule, 1);
        }
    }
    layout->addLayout(steps);

    if (!subtitle.isEmpty()) {
        QLabel *sub = new QLabel(subtitle, this);
        sub->setWordWrap(true);
        sub->setProperty("role", "secondary");
        layout->addWidget(sub);
    }
}

