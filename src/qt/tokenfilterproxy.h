// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef YONACOIN_TOKENFILTERPROXY_H
#define YONACOIN_TOKENFILTERPROXY_H

#include <QSortFilterProxyModel>

class TokenFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TokenFilterProxy(QObject *parent = 0);

    void setTokenNamePrefix(const QString &tokenNamePrefix);

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex & source_parent) const;

private:
    QString tokenNamePrefix;
};


#endif //YONACOIN_TOKENFILTERPROXY_H
