// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLD_QT_PLDADDRESSVALIDATOR_H
#define PLD_QT_PLDADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class PaladeumAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit PaladeumAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Paladeum address widget validator, checks for a valid paladeum address.
 */
class PaladeumAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit PaladeumAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // PLD_QT_PLDADDRESSVALIDATOR_H
