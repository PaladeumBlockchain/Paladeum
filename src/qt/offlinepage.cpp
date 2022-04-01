#include <qt/offlinepage.h>
#include <qt/forms/ui_offlinepage.h>

#include <qt/clientmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include "guiconstants.h"
#include "base58.h"

#include <QSortFilterProxyModel>

OfflinePage::OfflinePage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OfflinePage),
    clientModel(nullptr),
    walletModel(nullptr)
{
    ui->setupUi(this);

    ui->offlineFrame->setStyleSheet(QString(".QFrame {background-color: %1;}").arg(platformStyle->WidgetBackGroundColor().name()));
    ui->labelStaking->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelSpending->setStyleSheet(STRING_LABEL_COLOR);

    ui->stakingEdit->setPlaceholderText(QObject::tr("Enter a offline staking address"));
    ui->spendingEdit->setPlaceholderText(QObject::tr("Enter a spending address for offline stake"));
}


void OfflinePage::on_createButton_clicked()
{
	CYonaAddress stakingAddress(ui->stakingEdit->text().toStdString());
    CKeyID stakingKeyID;
    if (!stakingAddress.IsValid() || !stakingAddress.GetKeyID(stakingKeyID)) {
        ui->resultEdit->setText("Staking address is invalid");
        return;
    }

    CYonaAddress spendingAddress(ui->spendingEdit->text().toStdString());
    CKeyID spendingKeyID;
    if (!spendingAddress.IsValid() || !spendingAddress.GetKeyID(spendingKeyID)) {
    	ui->resultEdit->setText("Spending address is invalid");
        return;
    }

    spendingAddress.GetKeyID(spendingKeyID);

    ui->resultEdit->setText(QString::fromStdString(CYonaAddress(stakingKeyID, spendingKeyID).ToString()));
}

OfflinePage::~OfflinePage()
{
    delete ui;
}

void OfflinePage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void OfflinePage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}
