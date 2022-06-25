// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tokentablemodel.h"
#include "tokenrecord.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "wallet/wallet.h"

#include "core_io.h"

#include "amount.h"
#include "tokens/tokens.h"
#include "validation.h"
#include "platformstyle.h"

#include <QDebug>
#include <QStringList>



class TokenTablePriv {
public:
    TokenTablePriv(TokenTableModel *_parent) :
            parent(_parent)
    {
    }

    TokenTableModel *parent;

    QList<TokenRecord> cachedBalances;

    // loads all current balances into cache
#ifdef ENABLE_WALLET
    void refreshWallet() {
        qDebug() << "TokenTablePriv::refreshWallet";
        cachedBalances.clear();
        auto currentActiveTokenCache = GetCurrentTokenCache();
        if (currentActiveTokenCache) {
            {
                LOCK(cs_main);
                std::map<std::string, CAmount> balances;
                std::map<std::string, CAmount> locked_balances;
                std::map<std::string, std::vector<COutput> > outputs;
                std::map<std::string, std::vector<COutput> > outputs_locked;

                if (!GetAllMyTokenBalances(outputs, balances)) {
                    qWarning("TokenTablePriv::refreshWallet: Error retrieving token balances");
                    return;
                }

                if (!GetAllMyLockedTokenBalances(outputs_locked, locked_balances)) {
                    qWarning("TokenTablePriv::refreshWallet: Error retrieving locked token balances");
                    return;
                }

                std::set<std::string> setTokensToSkip;
                auto bal = balances.begin();
                for (; bal != balances.end(); bal++) {
                    // retrieve units for token
                    uint8_t units = OWNER_UNITS;
                    bool fIsAdministrator = true;
                    std::string ipfsHash = "";

                    if (setTokensToSkip.count(bal->first))
                        continue;

                    if (!IsTokenNameAnOwner(bal->first)) {
                        // Token is not an administrator token
                        CNewToken tokenData;
                        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(bal->first, tokenData)) {
                            qWarning("TokenTablePriv::refreshWallet: Error retrieving token data");
                            return;
                        }
                        units = tokenData.units;
                        ipfsHash = tokenData.strIPFSHash;
                        // If we have the administrator token, add it to the skip listå
                        if (balances.count(bal->first + OWNER_TAG)) {
                            setTokensToSkip.insert(bal->first + OWNER_TAG);
                        } else {
                            fIsAdministrator = false;
                        }
                    } else {
                        // Token is an administrator token, if we own tokens that is administrators, skip this balance
                        std::string name = bal->first;
                        name.pop_back();
                        if (balances.count(name)) {
                            setTokensToSkip.insert(bal->first);
                            continue;
                        }
                    }
                    cachedBalances.append(TokenRecord(bal->first, bal->second, units, fIsAdministrator, false, EncodeTokenData(ipfsHash)));
                }

                auto lbal = locked_balances.begin();
                for (; lbal != locked_balances.end(); lbal++) {
                    CNewToken tokenData;
                    if (!currentActiveTokenCache->GetTokenMetaDataIfExists(lbal->first, tokenData)) {
                        qWarning("TokenTablePriv::refreshWallet: Error retrieving locked token data");
                        return;
                    }
                    cachedBalances.append(TokenRecord(lbal->first + " (LOCKED)", lbal->second, tokenData.units, false, true, EncodeTokenData(tokenData.strIPFSHash)));
                }
            }
        }
    }
#endif


    int size() {
        return cachedBalances.size();
    }

    TokenRecord *index(int idx) {
        if (idx >= 0 && idx < cachedBalances.size()) {
            return &cachedBalances[idx];
        }
        return 0;
    }

};

TokenTableModel::TokenTableModel(WalletModel *parent) :
        QAbstractTableModel(parent),
        walletModel(parent),
        priv(new TokenTablePriv(this))
{
    columns << tr("Name") << tr("Quantity");
#ifdef ENABLE_WALLET
    priv->refreshWallet();
#endif
};

TokenTableModel::~TokenTableModel()
{
    delete priv;
};

void TokenTableModel::checkBalanceChanged() {
    qDebug() << "TokenTableModel::CheckBalanceChanged";
    // TODO: optimize by 1) updating cache incrementally; and 2) emitting more specific dataChanged signals
    Q_EMIT layoutAboutToBeChanged();
#ifdef ENABLE_WALLET
    priv->refreshWallet();
#endif
    Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(priv->size(), columns.length()-1, QModelIndex()));
    Q_EMIT layoutChanged();
}

int TokenTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int TokenTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 2;
}

QVariant TokenTableModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(role);
    if(!index.isValid())
        return QVariant();
    TokenRecord *rec = static_cast<TokenRecord*>(index.internalPointer());

    switch (role)
    {
        case AmountRole:
            return (unsigned long long) rec->quantity;
        case TokenNameRole:
            return QString::fromStdString(rec->name);
        case FormattedAmountRole:
            return QString::fromStdString(rec->formattedQuantity());
        case AdministratorRole:
            return rec->fIsAdministrator;
        case TokenIPFSHashRole:
            return QString::fromStdString(rec->ipfshash);
        case TokenIPFSHashDecorationRole:
        {
            if (index.column() == Quantity)
                return QVariant();

            if (rec->ipfshash.size() == 0)
                return QVariant();

            QPixmap pixmap;

            if (darkModeEnabled)
                pixmap = QPixmap::fromImage(QImage(":/icons/external_link_dark"));
            else
                pixmap = QPixmap::fromImage(QImage(":/icons/external_link"));

            return pixmap;
        }
        case IsLockedRole:
        {
            return rec->fIsLocked;
        }
        case Qt::DecorationRole:
        {
            if (index.column() == Quantity)
                return QVariant();

            QPixmap pixmap;

            if (!rec->fIsAdministrator) {
                QVariant();
                if (rec->fIsLocked) {
                    pixmap = QPixmap::fromImage(QImage(":/icons/token_locked"));
                }
            } else {
                pixmap = QPixmap::fromImage(QImage(":/icons/token_administrator"));
            }

            return pixmap;
        }
        case Qt::DisplayRole: {
            if (index.column() == Name)
                return QString::fromStdString(rec->name);
            else if (index.column() == Quantity)
                return QString::fromStdString(rec->formattedQuantity());
        }
        case Qt::ToolTipRole:
            return formatTooltip(rec);
        case Qt::TextAlignmentRole:
        {
            if (index.column() == Quantity) {
                return Qt::AlignRight + Qt::AlignVCenter;
            }
        }
        default:
            return QVariant();
    }
}

QVariant TokenTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (orientation == Qt::Horizontal) {
            if (section < columns.size())
                return columns.at(section);
        } else {
            return section;
        }
    } else if (role == Qt::SizeHintRole) {
        if (orientation == Qt::Vertical)
            return QSize(30, 50);
    } else if (role == Qt::TextAlignmentRole) {
        if (orientation == Qt::Vertical)
            return Qt::AlignLeft + Qt::AlignVCenter;

        return Qt::AlignHCenter + Qt::AlignVCenter;
    }

    return QVariant();
}

QModelIndex TokenTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    TokenRecord *data = priv->index(row);
    if(data)
    {
        QModelIndex idx = createIndex(row, column, priv->index(row));
        return idx;
    }

    return QModelIndex();
}

QString TokenTableModel::formatTooltip(const TokenRecord *rec) const
{
    QString tooltip = formatTokenName(rec) + QString("\n") + formatTokenQuantity(rec) + QString("\n") + formatTokenData(rec);
    return tooltip;
}

QString TokenTableModel::formatTokenName(const TokenRecord *wtx) const
{
    return QString::fromStdString(wtx->name);
}

QString TokenTableModel::formatTokenQuantity(const TokenRecord *wtx) const
{
    return QString::fromStdString(wtx->formattedQuantity());
}

QString TokenTableModel::formatTokenData(const TokenRecord *wtx) const
{
    return QString::fromStdString(wtx->ipfshash);
}