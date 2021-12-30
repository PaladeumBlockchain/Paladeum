// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "restrictedassignqualifier.h"
#include "ui_restrictedassignqualifier.h"

#include "yonaunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "tokenfilterproxy.h"
#include "tokentablemodel.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QCompleter>
#include <validation.h>
#include <utiltime.h>

AssignQualifier::AssignQualifier(const PlatformStyle *_platformStyle, QWidget *parent) :
        QWidget(parent),
        ui(new Ui::AssignQualifier),
        clientModel(0),
        walletModel(0),
        platformStyle(_platformStyle)
{
    ui->setupUi(this);

    ui->buttonSubmit->setDisabled(true);
    ui->lineEditAddress->installEventFilter(this);
    ui->lineEditChangeAddress->installEventFilter(this);
    ui->lineEditTokenData->installEventFilter(this);
    connect(ui->buttonClear, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->buttonCheck, SIGNAL(clicked()), this, SLOT(check()));
    connect(ui->lineEditAddress, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->lineEditChangeAddress, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->lineEditTokenData, SIGNAL(textChanged(QString)), this, SLOT(dataChanged()));
    connect(ui->checkBoxChangeAddress, SIGNAL(stateChanged(int)), this, SLOT(dataChanged()));
    connect(ui->checkBoxChangeAddress, SIGNAL(stateChanged(int)), this, SLOT(changeAddressChanged(int)));
    connect(ui->tokenComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(dataChanged()));
    connect(ui->assignTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(dataChanged()));

    ui->labelQualifier->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelQualifier->setFont(GUIUtil::getTopLabelFont());

    ui->labelAddress->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAddress->setFont(GUIUtil::getTopLabelFont());

    ui->labelAssignType->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelAssignType ->setFont(GUIUtil::getTopLabelFont());

    ui->labelTokenData->setStyleSheet(STRING_LABEL_COLOR);
    ui->labelTokenData ->setFont(GUIUtil::getTopLabelFont());

    ui->checkBoxChangeAddress->setStyleSheet(QString(".QCheckBox{ %1; }").arg(STRING_LABEL_COLOR));

    ui->lineEditChangeAddress->hide();
}


AssignQualifier::~AssignQualifier()
{
    delete ui;
}

void AssignQualifier::setClientModel(ClientModel *model)
{
    this->clientModel = model;
}

void AssignQualifier::setWalletModel(WalletModel *model)
{
    this->walletModel = model;

    tokenFilterProxy = new TokenFilterProxy(this);
    tokenFilterProxy->setSourceModel(model->getTokenTableModel());
    tokenFilterProxy->setDynamicSortFilter(true);
    tokenFilterProxy->setTokenNamePrefix("#");
    tokenFilterProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    tokenFilterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->tokenComboBox->setModel(tokenFilterProxy);

    ui->assignTypeComboBox->addItem(tr("Assign Qualifier"));
    ui->assignTypeComboBox->addItem(tr("Remove Qualifier"));
}

bool AssignQualifier::eventFilter(QObject* object, QEvent* event)
{
    if((object == ui->lineEditAddress || object == ui->lineEditChangeAddress || object == ui->lineEditTokenData) && event->type() == QEvent::FocusIn) {
        static_cast<QLineEdit*>(object)->setStyleSheet(STYLE_VALID);
        // bring up your custom edit
        return false; // lets the event continue to the edit
    }

    return false;
}

Ui::AssignQualifier* AssignQualifier::getUI()
{
    return this->ui;
}

void AssignQualifier::enableSubmitButton()
{
    showWarning(tr("Data has been validated, You can now submit the qualifier request"), false);
    ui->buttonSubmit->setEnabled(true);
}

void AssignQualifier::showWarning(QString string, bool failure)
{
    if (failure) {
        ui->labelWarning->setStyleSheet(STRING_LABEL_COLOR_WARNING);
    } else {
        ui->labelWarning->setStyleSheet("");
    }
    ui->labelWarning->setText(string);
    ui->labelWarning->show();
}

void AssignQualifier::hideWarning()
{
    ui->labelWarning->hide();
    ui->labelWarning->clear();
}

void AssignQualifier::clear()
{
    ui->lineEditAddress->clear();
    ui->lineEditTokenData->clear();
    ui->lineEditChangeAddress->clear();
    ui->buttonSubmit->setDisabled(true);
    ui->lineEditAddress->setStyleSheet(STYLE_VALID);
    ui->lineEditChangeAddress->setStyleSheet(STYLE_VALID);
    ui->lineEditTokenData->setStyleSheet(STYLE_VALID);
    ui->assignTypeComboBox->setCurrentIndex(0);
    hideWarning();
}

void AssignQualifier::dataChanged()
{
    ui->buttonSubmit->setDisabled(true);
    hideWarning();
}

void AssignQualifier::changeAddressChanged(int state)
{
    if (state == Qt::CheckState::Checked) {
        ui->lineEditChangeAddress->setEnabled(true);
        ui->lineEditChangeAddress->show();
    }
    if (state == Qt::CheckState::Unchecked) {
        ui->lineEditChangeAddress->setEnabled(false);
        ui->lineEditChangeAddress->hide();
    }
}

void AssignQualifier::check()
{
    QString qualifier = ui->tokenComboBox->currentData(TokenTableModel::RoleIndex::TokenNameRole).toString();
    QString address = ui->lineEditAddress->text();
    bool removing = ui->assignTypeComboBox->currentIndex() == 1;

    bool failed = false;
    if (!IsTokenNameAQualifier(qualifier.toStdString())){
        showWarning(tr("Must have a qualifier token selected"));
        failed = true;
    }

    std::string strAddress = address.toStdString();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        ui->lineEditAddress->setStyleSheet(STYLE_INVALID);
        failed = true;
    }

    if (ui->checkBoxChangeAddress->isChecked()) {
        std::string strChangeAddress = ui->lineEditChangeAddress->text().toStdString();
        if (!strChangeAddress.empty()) {
            CTxDestination changeDest = DecodeDestination(strChangeAddress);
            if (!IsValidDestination(changeDest)) {
                ui->lineEditChangeAddress->setStyleSheet(STYLE_INVALID);
                failed = true;
            }
        }
    }

    if (ui->lineEditTokenData->text().size()) {
        std::string strTokenData = ui->lineEditTokenData->text().toStdString();

        if (DecodeTokenData(strTokenData).empty()) {
            ui->lineEditTokenData->setStyleSheet(STYLE_INVALID);
            failed = true;
        }
    }

    if (failed) return;

    if (ptokens) {
        // returns true if the address has the qualifier
        if (ptokens->CheckForAddressQualifier(qualifier.toStdString(), address.toStdString(), true)) {
            if (removing) {
                enableSubmitButton();
            } else {
                showWarning(tr("Address already has the qualifier assigned to it"));
            }
        } else {
            if (!removing) {
                enableSubmitButton();
            } else {
                showWarning(tr("Address doesn't have the qualifier, so we can't remove it"));
            }
        }
    } else {
        showWarning(tr("Unable to preform action at this time"));
    }
};