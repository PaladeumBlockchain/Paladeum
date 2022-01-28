// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "yonagui.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "overviewpage.h"
#include "platformstyle.h"
#include "receivecoinsdialog.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "transactiontablemodel.h"
#include "tokentablemodel.h"
#include "transactionview.h"
#include "walletmodel.h"
#include "tokensdialog.h"
#include "createtokendialog.h"
#include "reissuetokendialog.h"
#include "restrictedtokensdialog.h"
#include <validation.h>

#include "ui_interface.h"

#include <QAction>
#include <QToolBar>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>


WalletView::WalletView(const PlatformStyle *_platformStyle, QWidget *parent):
    QStackedWidget(parent),
    clientModel(0),
    walletModel(0),
    platformStyle(_platformStyle)
{
    // Create tabs
    overviewPage = new OverviewPage(platformStyle);

    transactionsPage = new QWidget(this);
    tokensOverview = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);
    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    sendCoinsPage = new SendCoinsDialog(platformStyle);

    tokensPage = new TokensDialog(platformStyle);
    createTokensPage = new CreateTokenDialog(platformStyle);
    manageTokensPage = new ReissueTokenDialog(platformStyle);
    restrictedTokensPage = new RestrictedTokensDialog(platformStyle);

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);

    tokensStack = new QStackedWidget(this);
    QVBoxLayout *tokensLayout = new QVBoxLayout();
    QActionGroup *tabGroup = new QActionGroup(this);

    QAction *transferTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_transfer_selected", ":/icons/token_transfer"), tr("&Transfer Tokens"), this);
    transferTokenAction->setStatusTip(tr("Transfer tokens to YONA addresses"));
    transferTokenAction->setToolTip(transferTokenAction->statusTip());
    transferTokenAction->setCheckable(true);
    tabGroup->addAction(transferTokenAction);

    QAction *createTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_create_selected", ":/icons/token_create"), tr("&Create Tokens"), this);
    createTokenAction->setStatusTip(tr("Create new main/sub/unique tokens"));
    createTokenAction->setToolTip(createTokenAction->statusTip());
    createTokenAction->setCheckable(true);
    tabGroup->addAction(createTokenAction);

    QAction *manageTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/token_manage_selected", ":/icons/token_manage"), tr("&Manage Tokens"), this);
    manageTokenAction->setStatusTip(tr("Manage tokens you are the administrator of"));
    manageTokenAction->setToolTip(manageTokenAction->statusTip());
    manageTokenAction->setCheckable(true);
    tabGroup->addAction(manageTokenAction);

    QAction *restrictedTokenAction = new QAction(platformStyle->SingleColorIconOnOff(":/icons/restricted_token_selected", ":/icons/restricted_token"), tr("&Restricted Tokens"), this);
    restrictedTokenAction->setStatusTip(tr("Manage restricted tokens"));
    restrictedTokenAction->setToolTip(restrictedTokenAction->statusTip());
    restrictedTokenAction->setCheckable(true);
    tabGroup->addAction(restrictedTokenAction);

    QToolBar *tokensToolbar = new QToolBar(this);
    tokensToolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    tokensToolbar->setMovable(false);
    tokensToolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tokensToolbar->addAction(transferTokenAction);
    tokensToolbar->addAction(createTokenAction);
    tokensToolbar->addAction(manageTokenAction);
    tokensToolbar->addAction(restrictedTokenAction);

    tokensLayout->addWidget(tokensToolbar);
    tokensLayout->addWidget(tokensStack);
    tokensOverview->setLayout(tokensLayout);

    connect(transferTokenAction, SIGNAL(triggered()), this, SLOT(gotoTokensPage()));
    connect(createTokenAction, SIGNAL(triggered()), this, SLOT(gotoCreateTokensPage()));
    connect(manageTokenAction, SIGNAL(triggered()), this, SLOT(gotoManageTokensPage()));
    connect(restrictedTokenAction, SIGNAL(triggered()), this, SLOT(gotoRestrictedTokensPage()));

    tokensStack->addWidget(tokensPage);
    tokensStack->addWidget(createTokensPage);
    tokensStack->addWidget(manageTokensPage);
    tokensStack->addWidget(restrictedTokensPage);

    /** TOKENS START */
    addWidget(tokensOverview);
    /** TOKENS END */

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));
    connect(overviewPage, SIGNAL(outOfSyncWarningClicked()), this, SLOT(requestedSyncWarningInfo()));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

    /** TOKENS START */
    connect(tokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(createTokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(manageTokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(restrictedTokensPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    connect(overviewPage, SIGNAL(tokenSendClicked(QModelIndex)), tokensPage, SLOT(focusToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenIssueSubClicked(QModelIndex)), createTokensPage, SLOT(focusSubToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenIssueUniqueClicked(QModelIndex)), createTokensPage, SLOT(focusUniqueToken(QModelIndex)));
    connect(overviewPage, SIGNAL(tokenReissueClicked(QModelIndex)), manageTokensPage, SLOT(focusReissueToken(QModelIndex)));
    /** TOKENS END */

    transferTokenAction->setChecked(true);
}

WalletView::~WalletView()
{
}

void WalletView::setYonaGUI(YonaGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));

        // Clicking on a token menu item Send
        connect(overviewPage, SIGNAL(tokenSendClicked(QModelIndex)), gui, SLOT(gotoTokensPage()));

        // Clicking on a token menu item Issue Sub
        connect(overviewPage, SIGNAL(tokenIssueSubClicked(QModelIndex)), gui, SLOT(gotoCreateTokensPage()));

        // Clicking on a token menu item Issue Unique
        connect(overviewPage, SIGNAL(tokenIssueUniqueClicked(QModelIndex)), gui, SLOT(gotoCreateTokensPage()));

        // Clicking on a token menu item Reissue
        connect(overviewPage, SIGNAL(tokenReissueClicked(QModelIndex)), gui, SLOT(gotoManageTokensPage()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString,int,CAmount,QString,QString,QString,QString)), gui, SLOT(incomingTransaction(QString,int,CAmount,QString,QString,QString,QString)));

        // Connect HD enabled state signal 
        connect(this, SIGNAL(hdEnabledStatusChanged(int)), gui, SLOT(setHDStatus(int)));

        // Pass through checkTokens calls to the GUI
        connect(this, SIGNAL(checkTokens()), gui, SLOT(checkTokens()));
    }
}

void WalletView::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    overviewPage->setClientModel(_clientModel);
    sendCoinsPage->setClientModel(_clientModel);
}

void WalletView::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;

    // Put transaction list in tabs
    transactionView->setModel(_walletModel);
    overviewPage->setWalletModel(_walletModel);
    receiveCoinsPage->setModel(_walletModel);
    sendCoinsPage->setModel(_walletModel);
    usedReceivingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);
    usedSendingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);

    /** TOKENS START */
    tokensPage->setModel(_walletModel);
    createTokensPage->setModel(_walletModel);
    manageTokensPage->setModel(_walletModel);
    restrictedTokensPage->setModel(_walletModel);

    if (_walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(_walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

        // Handle changes in encryption status
        connect(_walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // update HD status
        Q_EMIT hdEnabledStatusChanged(_walletModel->hd44Enabled() ? YonaGUI::HD44_ENABLED : _walletModel->hdEnabled() ? YonaGUI::HD_ENABLED : YonaGUI::HD_DISABLED);

        // Balloon pop-up for new transaction
        connect(_walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(processNewTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(_walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Show progress dialog
        connect(_walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int end)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    /** TOKENS START */
    // With the addition of token transactions, there can be multiple transaction that need notifications
    // so we need to loop through all new transaction that were added to the transaction table and display
    // notifications for each individual transaction
    QString tokenName = "";
    for (int i = start; i <= end; i++) {
        QString date = ttm->index(i, TransactionTableModel::Date, parent).data().toString();
        qint64 amount = ttm->index(i, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
        QString type = ttm->index(i, TransactionTableModel::Type, parent).data().toString();
        QModelIndex index = ttm->index(i, 0, parent);
        QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
        QString label = ttm->data(index, TransactionTableModel::LabelRole).toString();
        tokenName = ttm->data(index, TransactionTableModel::TokenNameRole).toString();

        Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label,
                                   tokenName);
    }
    /** TOKENS END */

    /** Everytime we get an new transaction. We should check to see if tokens are enabled or not */
    overviewPage->showTokens();
    transactionView->showTokens();
    Q_EMIT checkTokens();

    tokensPage->processNewTransaction();
    createTokensPage->updateTokenList();
    manageTokensPage->updateTokensList();

}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
    Q_EMIT checkTokens();
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);
}

void WalletView::gotoTokensOverviewPage()
{
    setCurrentWidget(tokensOverview);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->backupWallet(filename)) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}
void WalletView::getMyWords()
{
    // Create the box and set the default text.
    QMessageBox box;
    box.setWindowTitle(tr("Recovery information"));
    box.setText(tr("No words available."));

    // Check for HD-wallet and set text if not HD-wallet.
    if(!walletModel->hd44Enabled())
        box.setText(tr("This wallet is not a HD wallet, words not supported."));

    // Unlock wallet requested by wallet model
    WalletView::unlockWallet();

    // Make sure wallet is unlocked before trying to fetch the words.
    // When unlocked, set the text to 12words and passphrase.
    if (walletModel->getEncryptionStatus() != WalletModel::Locked)
        box.setText(walletModel->getMyWords());

    // Show the box
    box.exec();
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;

    usedSendingAddressesPage->show();
    usedSendingAddressesPage->raise();
    usedSendingAddressesPage->activateWindow();
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;

    usedReceivingAddressesPage->show();
    usedReceivingAddressesPage->raise();
    usedReceivingAddressesPage->activateWindow();
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void WalletView::requestedSyncWarningInfo()
{
    Q_EMIT outOfSyncWarningClicked();
}

bool fFirstVisit = true;
/** TOKENS START */
void WalletView::gotoTokensPage()
{
    if (fFirstVisit){
        fFirstVisit = false;
        tokensPage->handleFirstSelection();
    }
    // setCurrentWidget(tokensPage);
    tokensPage->focusTokenListBox();

    tokensStack->setCurrentWidget(tokensPage);

    setCurrentWidget(tokensOverview);
}

void WalletView::gotoCreateTokensPage()
{
    tokensStack->setCurrentWidget(createTokensPage);
}

void WalletView::gotoManageTokensPage()
{
    tokensStack->setCurrentWidget(manageTokensPage);
}

void WalletView::gotoRestrictedTokensPage()
{
    tokensStack->setCurrentWidget(restrictedTokensPage);
}
/** TOKENS END */