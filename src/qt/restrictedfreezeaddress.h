// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef YONA_QT_FREEZEADDRESS_H
#define YONA_QT_FREEZEADDRESS_H

#include "amount.h"

#include <QWidget>
#include <QMenu>
#include <memory>

class ClientModel;
class PlatformStyle;
class WalletModel;
class QStringListModel;
class QSortFilterProxyModel;
class QCompleter;
class TokenFilterProxy;


namespace Ui {
    class FreezeAddress;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class FreezeAddress : public QWidget
{
    Q_OBJECT

public:
    explicit FreezeAddress(const PlatformStyle *_platformStyle, QWidget *parent = 0);
    ~FreezeAddress();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);
    Ui::FreezeAddress* getUI();
    bool eventFilter(QObject* object, QEvent* event);

    void enableSubmitButton();
    void showWarning(QString string, bool failure = true);
    void hideWarning();

    TokenFilterProxy *tokenFilterProxy;
    QCompleter* completer;

    void clear();

private:
    Ui::FreezeAddress *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;

private Q_SLOTS:
    void check();
    void dataChanged();
    void globalOptionSelected();
    void changeAddressChanged(int);
};

#endif // YONA_QT_FREEZEADDRESS_H
