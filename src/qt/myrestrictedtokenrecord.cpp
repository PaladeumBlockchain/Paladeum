// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "myrestrictedtokenrecord.h"

#include "tokens/tokens.h"
#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include <stdint.h>

#include <QDebug>


/* Return positive answer if transaction should be shown in list.
 */
bool MyRestrictedTokenRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}


/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<MyRestrictedTokenRecord> MyRestrictedTokenRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<MyRestrictedTokenRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    for(unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
        const CTxOut &txout = wtx.tx->vout[i];
        isminetype mine = ISMINE_NO;

        if (txout.scriptPubKey.IsNullTokenTxDataScript()) {
            CNullTokenTxData data;
            std::string address;
            if (!TokenNullDataFromScript(txout.scriptPubKey, data, address)) {
                continue;
            }
            mine = wallet->IsMineDest(DecodeDestination(address));
            if (mine & ISMINE_ALL) {
                MyRestrictedTokenRecord sub(hash, nTime);
                sub.involvesWatchAddress = mine & ISMINE_SPENDABLE ? false : true;
                sub.tokenName = data.token_name;
                sub.address = address;

                if (IsTokenNameAQualifier(data.token_name)) {
                    if (data.flag == (int) QualifierType::ADD_QUALIFIER) {
                        sub.type = MyRestrictedTokenRecord::Type::Tagged;
                    } else {
                        sub.type = MyRestrictedTokenRecord::Type::UnTagged;
                    }
                } else if (IsTokenNameAnRestricted(data.token_name)) {
                    if (data.flag == (int) RestrictedType::FREEZE_ADDRESS) {
                        sub.type = MyRestrictedTokenRecord::Type::Frozen;
                    } else {
                        sub.type = MyRestrictedTokenRecord::Type::UnFrozen;
                    }
                }
                parts.append(sub);
            }
        }
    }
    return parts;
}
QString MyRestrictedTokenRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int MyRestrictedTokenRecord::getOutputIndex() const
{
    return idx;
}
