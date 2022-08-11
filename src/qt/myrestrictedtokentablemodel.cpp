// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "myrestrictedtokentablemodel.h"

#include "addresstablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactiondesc.h"
#include "myrestrictedtokenrecord.h"
#include "walletmodel.h"

#include "core_io.h"
#include "validation.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
#include "wallet/wallet.h"

#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QList>

// Fixing Boost 1.73 compile errors
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

// Amount column is right-aligned it contains numbers
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignLeft|Qt::AlignVCenter /* tokenName */
};

// Comparison operator for sort/binary search of model tx list
struct TxLessThan
{
    bool operator()(const MyRestrictedTokenRecord &a, const MyRestrictedTokenRecord &b) const
    {
        return a.hash < b.hash;
    }
    bool operator()(const MyRestrictedTokenRecord &a, const uint256 &b) const
    {
        return a.hash < b;
    }
    bool operator()(const uint256 &a, const MyRestrictedTokenRecord &b) const
    {
        return a < b.hash;
    }
};

// Private implementation
class MyRestrictedTokensTablePriv
{
public:
    MyRestrictedTokensTablePriv(CWallet *_wallet, MyRestrictedTokensTableModel *_parent) :
            wallet(_wallet),
            parent(_parent)
    {
    }

    CWallet *wallet;
    MyRestrictedTokensTableModel *parent;

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QMap<QPair<QString,QString>,MyRestrictedTokenRecord> cacheMyTokenData;
    QList<QPair<QString, QString> > vectTokenData;

    /* Query entire wallet anew from core.
     */
    void refreshWallet()
    {
        qDebug() << "MyRestrictedTokensTablePriv::refreshWallet";
        cacheMyTokenData.clear();
        vectTokenData.clear();
        {
            std::vector<std::tuple<std::string, std::string, bool, uint32_t> > myTaggedAddresses;
            std::vector<std::tuple<std::string, std::string, bool, uint32_t> > myRestrictedAddresses;
            pmyrestricteddb->LoadMyTaggedAddresses(myTaggedAddresses);
            pmyrestricteddb->LoadMyRestrictedAddresses(myRestrictedAddresses);
            myTaggedAddresses.insert(myTaggedAddresses.end(), myRestrictedAddresses.begin(), myRestrictedAddresses.end());
            if(myTaggedAddresses.size()) {
                for (auto item : myTaggedAddresses) {
                    MyRestrictedTokenRecord sub;
                    sub.address = std::get<0>(item);
                    sub.tokenName = std::get<1>(item);
                    sub.time = std::get<3>(item);
                    if (IsTokenNameAQualifier(sub.tokenName))
                        sub.type = std::get<2>(item) ? MyRestrictedTokenRecord::Type::Tagged : MyRestrictedTokenRecord::Type::UnTagged;
                    else if (IsTokenNameAnRestricted(sub.tokenName))
                        sub.type = std::get<2>(item) ? MyRestrictedTokenRecord::Type::Frozen : MyRestrictedTokenRecord::Type::UnFrozen;
                    sub.involvesWatchAddress = this->wallet->IsMineDest(DecodeDestination(sub.address)) & ISMINE_WATCH_ONLY;
                    vectTokenData.push_back(qMakePair(QString::fromStdString(std::get<0>(item)), QString::fromStdString(std::get<1>(item))));
                    cacheMyTokenData[qMakePair(QString::fromStdString(std::get<0>(item)), QString::fromStdString(std::get<1>(item)))] = sub;
                }
            }
        }
    }

    void updateMyRestrictedTokens(const QString &address, const QString& token_name, const int type, const qint64& date) {
        MyRestrictedTokenRecord rec;

        if (IsTokenNameAQualifier(token_name.toStdString())) {
            rec.type = type ? MyRestrictedTokenRecord::Tagged : MyRestrictedTokenRecord::UnTagged;
        } else {
            rec.type = type ? MyRestrictedTokenRecord::Frozen : MyRestrictedTokenRecord::UnFrozen;
        }

        rec.time = date;
        rec.tokenName = token_name.toStdString();
        rec.address = address.toStdString();

        QPair<QString, QString> pair(address, token_name);
        if (cacheMyTokenData.contains(pair)) {
            rec.involvesWatchAddress = cacheMyTokenData[pair].involvesWatchAddress;
            cacheMyTokenData[pair] = rec;
        } else {
            rec.involvesWatchAddress =
                    wallet->IsMineDest(DecodeDestination(address.toStdString())) & ISMINE_WATCH_ONLY ? true : false;
            parent->beginInsertRows(QModelIndex(), 0, 0);
            cacheMyTokenData[pair] = rec;
            vectTokenData.push_front(pair);
            parent->endInsertRows();
        }
    }

    int size()
    {
        return cacheMyTokenData.size();
    }

    MyRestrictedTokenRecord *index(int idx)
    {
        if(idx >= 0 && idx < vectTokenData.size())
        {
            auto pair = vectTokenData[idx];
            if (cacheMyTokenData.contains(pair)) {
                MyRestrictedTokenRecord *rec = &cacheMyTokenData[pair];
                return rec;
            }
        }
        return 0;
    }

//    QString describe(TransactionRecord *rec, int unit)
//    {
//        {
//            LOCK2(cs_main, wallet->cs_wallet);
//            std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);
//            if(mi != wallet->mapWallet.end())
//            {
//                return TransactionDesc::toHTML(wallet, mi->second, rec, unit);
//            }
//        }
//        return QString();
//    }

//    QString getTxHex(TransactionRecord *rec)
//    {
//        LOCK2(cs_main, wallet->cs_wallet);
//        std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);
//        if(mi != wallet->mapWallet.end())
//        {
//            std::string strHex = EncodeHexTx(static_cast<CTransaction>(mi->second));
//            return QString::fromStdString(strHex);
//        }
//        return QString();
//    }
};

MyRestrictedTokensTableModel::MyRestrictedTokensTableModel(const PlatformStyle *_platformStyle, CWallet* _wallet, WalletModel *parent):
        QAbstractTableModel(parent),
        wallet(_wallet),
        walletModel(parent),
        priv(new MyRestrictedTokensTablePriv(_wallet, this)),
        fProcessingQueuedTransactions(false),
        platformStyle(_platformStyle)
{
    columns << tr("Date") << tr("Type") << tr("Address") << tr("Token Name");

    priv->refreshWallet();

    connect(walletModel->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    subscribeToCoreSignals();
}

MyRestrictedTokensTableModel::~MyRestrictedTokensTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

void MyRestrictedTokensTableModel::updateMyRestrictedTokens(const QString &address, const QString& token_name, const int type, const qint64 date)
{
    priv->updateMyRestrictedTokens(address, token_name, type, date);
}

int MyRestrictedTokensTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int MyRestrictedTokensTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QString MyRestrictedTokensTableModel::formatTxDate(const MyRestrictedTokenRecord *wtx) const
{
    if(wtx->time)
    {
        return GUIUtil::dateTimeStr(wtx->time);
    }
    return QString();
}

/* Look up address in address book, if found return label (address)
   otherwise just return (address)
 */
QString MyRestrictedTokensTableModel::lookupAddress(const std::string &address, bool tooltip) const
{
    QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(address));
    QString description;
    if(!label.isEmpty())
    {
        description += label;
    }
    if(label.isEmpty() || tooltip)
    {
        description += QString(" (") + QString::fromStdString(address) + QString(")");
    }
    return description;
}

QString MyRestrictedTokensTableModel::formatTxType(const MyRestrictedTokenRecord *wtx) const
{
    switch(wtx->type)
    {
        case MyRestrictedTokenRecord::Tagged:
            return tr("Tagged");
        case MyRestrictedTokenRecord::UnTagged:
            return tr("Untagged");
        case MyRestrictedTokenRecord::Frozen:
            return tr("Frozen");
        case MyRestrictedTokenRecord::UnFrozen:
            return tr("Unfrozen");
        case MyRestrictedTokenRecord::Other:
            return tr("Other");
        default:
            return QString();
    }
}

QVariant MyRestrictedTokensTableModel::txAddressDecoration(const MyRestrictedTokenRecord *wtx) const
{
    if (wtx->involvesWatchAddress)
        return QIcon(":/icons/eye");

    return QVariant();

    // TODO get icons, and update when added
//    switch(wtx->type)
//    {
//        case MyRestrictedTokenRecord::Tagged:
//            return QIcon(":/icons/tx_mined");
//        case MyRestrictedTokenRecord::UnTagged:
//            return QIcon(":/icons/tx_input");
//        case MyRestrictedTokenRecord::Frozen:
//            return QIcon(":/icons/tx_output");
//        case MyRestrictedTokenRecord::UnFrozen:
//            return QIcon(":/icons/tx_token_input");
//        case MyRestrictedTokenRecord::Other:
//            return QIcon(":/icons/tx_inout");
//        default:
//            return QIcon(":/icons/tx_inout");
//    }
}

QString MyRestrictedTokensTableModel::formatTxToAddress(const MyRestrictedTokenRecord *wtx, bool tooltip) const
{
    QString watchAddress;
    if (tooltip) {
        // Mark transactions involving watch-only addresses by adding " (watch-only)"
        watchAddress = wtx->involvesWatchAddress ? QString(" (") + tr("watch-only") + QString(")") : "";
    }

    return QString::fromStdString(wtx->address) + watchAddress;
}

QVariant MyRestrictedTokensTableModel::addressColor(const MyRestrictedTokenRecord *wtx) const
{
    return QVariant();
}


QVariant MyRestrictedTokensTableModel::txWatchonlyDecoration(const MyRestrictedTokenRecord *wtx) const
{
    if (wtx->involvesWatchAddress)
        return QIcon(":/icons/eye");
    else
        return QVariant();
}

QString MyRestrictedTokensTableModel::formatTooltip(const MyRestrictedTokenRecord *rec) const
{
    QString tooltip = formatTxType(rec);
    return tooltip;
}

QVariant MyRestrictedTokensTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    MyRestrictedTokenRecord *rec = static_cast<MyRestrictedTokenRecord*>(index.internalPointer());

    switch(role)
    {
        case RawDecorationRole:
            switch(index.column())
            {
                case ToAddress:
                    return txAddressDecoration(rec);
                case TokenName:
                    return QString::fromStdString(rec->tokenName);
            }
            break;
        case Qt::DecorationRole:
        {
            QIcon icon = qvariant_cast<QIcon>(index.data(RawDecorationRole));
            return platformStyle->TextColorIcon(icon);
        }
        case Qt::DisplayRole:
            switch(index.column())
            {
                case Date:
                    return formatTxDate(rec);
                case Type:
                    return formatTxType(rec);
                case ToAddress:
                    return formatTxToAddress(rec, false);
                case TokenName:
                    return QString::fromStdString(rec->tokenName);
            }
            break;
        case Qt::EditRole:
            // Edit role is used for sorting, so return the unformatted values
            switch(index.column())
            {
                case Date:
                    return rec->time;
                case Type:
                    return formatTxType(rec);
                case ToAddress:
                    return formatTxToAddress(rec, true);
                case TokenName:
                    return QString::fromStdString(rec->tokenName);
            }
            break;
        case Qt::ToolTipRole:
            return formatTooltip(rec);
        case Qt::TextAlignmentRole:
            return column_alignments[index.column()];
        case Qt::ForegroundRole:
            if(index.column() == ToAddress)
            {
                return addressColor(rec);
            }
            break;
        case TypeRole:
            return rec->type;
        case DateRole:
            return QDateTime::fromTime_t(static_cast<uint>(rec->time));
        case WatchonlyRole:
            return rec->involvesWatchAddress;
        case WatchonlyDecorationRole:
            return txWatchonlyDecoration(rec);
        case AddressRole:
            return QString::fromStdString(rec->address);
        case LabelRole:
            return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
        case TxIDRole:
            return rec->getTxID();
        case TxHashRole:
            return QString::fromStdString(rec->hash.ToString());
        case TxHexRole:
            return ""; //priv->getTxHex(rec);
        case TxPlainTextRole:
        {
            QString details;
            QDateTime date = QDateTime::fromTime_t(static_cast<uint>(rec->time));
            QString txLabel = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));

            details.append(date.toString("M/d/yy HH:mm"));
            details.append(" ");
            details.append(". ");
            if(!formatTxType(rec).isEmpty()) {
                details.append(formatTxType(rec));
                details.append(" ");
            }
            if(!rec->address.empty()) {
                if(txLabel.isEmpty())
                    details.append(tr("(no label)") + " ");
                else {
                    details.append("(");
                    details.append(txLabel);
                    details.append(") ");
                }
                details.append(QString::fromStdString(rec->address));
                details.append(" ");
            }
            return details;
        }
        case TokenNameRole:
        {
            QString tokenName;
            tokenName.append(QString::fromStdString(rec->tokenName));
            return tokenName;
        }
    }
    return QVariant();
}

QVariant MyRestrictedTokensTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        } else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
                case Date:
                    return tr("Date and time that the transaction was received.");
                case Type:
                    return tr("Type of transaction.");
                case ToAddress:
                    return tr("User-defined intent/purpose of the transaction.");
                case TokenName:
                    return tr("The token (or PLB) removed or added to balance.");
            }
        }
    }
    return QVariant();
}

QModelIndex MyRestrictedTokensTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    MyRestrictedTokenRecord *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    return QModelIndex();
}

//// queue notifications to show a non freezing progress dialog e.g. for rescan
struct MyRestrictedTransactionNotification
{
public:
    MyRestrictedTransactionNotification() {}
    MyRestrictedTransactionNotification(std::string _address, std::string _token_name, int _type, uint32_t _date):
            address(_address), token_name(_token_name), type(_type), date(_date) {}

    void invoke(QObject *ttm)
    {
        QString strAddress = QString::fromStdString(address);
        QString strName= QString::fromStdString(token_name);
        qDebug() << "MyRestrictedTokenChanged: " + strAddress + " token_name= " + strName;
        QMetaObject::invokeMethod(ttm, "updateMyRestrictedTokens", Qt::QueuedConnection,
                                  Q_ARG(QString, strAddress),
                                  Q_ARG(QString, strName),
                                  Q_ARG(int, type),
                                  Q_ARG(qint64, date));
    }
private:
    std::string address;
    std::string token_name;
    int type;
    uint32_t date;
};

static bool fQueueNotifications = false;
static std::vector< MyRestrictedTransactionNotification > vQueueNotifications;

static void NotifyTransactionChanged(MyRestrictedTokensTableModel *ttm, CWallet *wallet, const std::string& address, const std::string& token_name,
                                     int type, uint32_t date)
{
    MyRestrictedTransactionNotification notification(address, token_name, type, date);

    if (fQueueNotifications)
    {
        vQueueNotifications.push_back(notification);
        return;
    }
    notification.invoke(ttm);
}

static void ShowProgress(MyRestrictedTokensTableModel *ttm, const std::string &title, int nProgress)
{
    if (nProgress == 0)
        fQueueNotifications = true;

    if (nProgress == 100)
    {
        fQueueNotifications = false;
        if (vQueueNotifications.size() > 10) // prevent balloon spam, show maximum 10 balloons
            QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, true));
        for (unsigned int i = 0; i < vQueueNotifications.size(); ++i)
        {
            if (vQueueNotifications.size() - i <= 10)
                QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, false));

            vQueueNotifications[i].invoke(ttm);
        }
        std::vector<MyRestrictedTransactionNotification >().swap(vQueueNotifications); // clear
    }
}

void MyRestrictedTokensTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyMyRestrictedTokensChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3, _4, _5));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
}

void MyRestrictedTokensTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyMyRestrictedTokensChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3, _4, _5));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
}
