// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus.h"
#include <validation.h>
#include <amount.h>

CAmount GetStaticFee(bool nTokenTransaction, int nSpendHeight) {
    return nTokenTransaction ? 0.005 * COIN : 0.008 * COIN;
}

unsigned int GetMaxBlockWeight()
{
    return MAX_BLOCK_WEIGHT;
}

unsigned int GetMaxBlockSerializedSize()
{
    return MAX_BLOCK_SERIALIZED_SIZE;
}