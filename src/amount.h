// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
 // Copyright (c) 2014-2016 The BlackCoin developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLB_AMOUNT_H
#define PLB_AMOUNT_H

#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <limits>

/** Amount in corbies (Can be negative) */
typedef int64_t CAmount;

static const CAmount COIN = 100000000;
static const CAmount CENT = 1000000;

/** No amount of PLB (in satoshi) larger than this is valid.
 *
 * Note that this constant is *not* the total money supply, which is about one
 * billion PLB and grows by ten with every block, but rather a sanity check. It
 * was the largest 64 bit number, which made the range checks pointless: sums
 * could overflow before a check saw them. As this sanity check is used by
 * consensus-critical validation code, the exact value of the MAX_MONEY
 * constant is consensus critical.
 * */
static const CAmount MAX_MONEY = 25000000000 * COIN;
/** No token amount larger than this is valid (the largest 64 bit number, as before:
 *  tokens had no smaller limit). */
static const CAmount MAX_MONEY_TOKENS = std::numeric_limits<int64_t>::max();

inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  PLB_AMOUNT_H
