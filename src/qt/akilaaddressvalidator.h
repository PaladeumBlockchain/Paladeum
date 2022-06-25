// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AKILA_QT_AKILAADDRESSVALIDATOR_H
#define AKILA_QT_AKILAADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class AkilaAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit AkilaAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Akila address widget validator, checks for a valid akila address.
 */
class AkilaAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit AkilaAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // AKILA_QT_AKILAADDRESSVALIDATOR_H
