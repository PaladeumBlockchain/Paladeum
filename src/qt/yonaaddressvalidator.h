// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef YONA_QT_YONAADDRESSVALIDATOR_H
#define YONA_QT_YONAADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class YonaAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit YonaAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

/** Yona address widget validator, checks for a valid yona address.
 */
class YonaAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit YonaAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const;
};

#endif // YONA_QT_YONAADDRESSVALIDATOR_H
