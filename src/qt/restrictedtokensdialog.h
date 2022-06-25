// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AKILA_QT_RESTRICTEDTOKENSDIALOG_H
#define AKILA_QT_RESTRICTEDTOKENSDIALOG_H

#include "walletmodel.h"

#include <QDialog>
#include <QMessageBox>
#include <QString>

class ClientModel;
class PlatformStyle;
class SendTokensEntry;
class SendCoinsRecipient;
class TokenFilterProxy;
class AssignQualifier;
class MyRestrictedTokensTableModel;
class MyRestrictedTokensFilterProxy;
class QSortFilterProxyModel;


namespace Ui {
    class RestrictedTokensDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending akilas */
class RestrictedTokensDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RestrictedTokensDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~RestrictedTokensDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);
    void setupStyling(const PlatformStyle *platformStyle);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);
public Q_SLOTS:
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& stake,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                    const CAmount& watchOnlyStake, const CAmount& lockedBalance, const CAmount& offline);


private:
    Ui::RestrictedTokensDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    TokenFilterProxy *tokenFilterProxy;
    QSortFilterProxyModel *myRestrictedTokensFilterProxy;

    MyRestrictedTokensTableModel *myRestrictedTokensModel;

private Q_SLOTS:
    void updateDisplayUnit();
    void assignQualifierClicked();
    void freezeAddressClicked();


    Q_SIGNALS:
            // Fired when a message should be reported to the user
            void message(const QString &title, const QString &message, unsigned int style);
};

#endif // AKILA_QT_RESTRICTEDTOKENSSDIALOG_H
