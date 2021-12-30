// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "tokenfilterproxy.h"
#include "tokentablemodel.h"


TokenFilterProxy::TokenFilterProxy(QObject *parent) :
        QSortFilterProxyModel(parent),
        tokenNamePrefix()
{
}

bool TokenFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    QString tokenName = index.data(TokenTableModel::TokenNameRole).toString();

    if(!tokenName.startsWith(tokenNamePrefix, Qt::CaseInsensitive))
        return false;

    return true;
}

void TokenFilterProxy::setTokenNamePrefix(const QString &_tokenNamePrefix)
{
    this->tokenNamePrefix = _tokenNamePrefix;
    invalidateFilter();
}