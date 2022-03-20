// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokens.h>
#include <script/standard.h>
#include <util.h>
#include <validation.h>
#include "tx_verify.h"
#include "chainparams.h"

#include "consensus.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "validation.h"
#include <cmath>
#include <wallet/wallet.h>
#include <base58.h>
#include <tinyformat.h>

// TODO remove the following dependencies
#include "chain.h"
#include "coins.h"
#include "utilmoneystr.h"

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = false;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetPastTimeLimit();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetPastTimeLimit();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fCheckDuplicateInputs, bool fMempoolCheck, bool fBlockCheck)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    if (tx.nMessage.length() > MAX_MESSAGE_LEN)
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-message-length");

    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > GetMaxBlockWeight())
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    std::set<std::string> setTokenTransferNames;
    std::map<std::pair<std::string, std::string>, int> mapNullDataTxCount; // (token_name, address) -> int
    std::set<std::string> setNullGlobalTokenChanges;
    bool fContainsNewRestrictedToken = false;
    bool fContainsRestrictedTokenReissue = false;
    bool fContainsNullTokenVerifierTx = false;
    int nCountAddTagOuts = 0;

    for (const auto& txout : tx.vout)
    {
        if (txout.IsEmpty() && !tx.IsCoinBase() && !tx.IsCoinStake())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-empty");
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");

        if(txout.scriptPubKey.IsOfflineStaking() && !IsOfflineStakingEnabled(pindexBestHeader, GetParams().GetConsensus()))
            return state.DoS(100, false, REJECT_INVALID, "offline-staking-not-enabled");

        /** TOKENS START */
        // Find and handle all new OP_YONA_TOKEN null data transactions
        if (txout.scriptPubKey.IsNullToken()) {
            CNullTokenTxData data;
            std::string address;
            std::string strError = "";

            if (txout.scriptPubKey.IsNullTokenTxDataScript()) {
                if (!TokenNullDataFromScript(txout.scriptPubKey, data, address))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-token-data-serialization");

                if (!VerifyNullTokenDataFlag(data.flag, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                auto pair = std::make_pair(data.token_name, address);
                if(!mapNullDataTxCount.count(pair)){
                    mapNullDataTxCount.insert(std::make_pair(pair, 0));
                }

                mapNullDataTxCount.at(pair)++;

                if (mapNullDataTxCount.at(pair) > 1)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-change-per-token-address");

                // For each qualifier that is added, there is a burn fee
                if (IsTokenNameAQualifier(data.token_name)) {
                    if (data.flag == (int)QualifierType::ADD_QUALIFIER) {
                        nCountAddTagOuts++;
                    }
                }

            } else if (txout.scriptPubKey.IsNullGlobalRestrictionTokenTxDataScript()) {
                if (!GlobalTokenNullDataFromScript(txout.scriptPubKey, data))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-global-token-data-serialization");

                if (!VerifyNullTokenDataFlag(data.flag, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                if (setNullGlobalTokenChanges.count(data.token_name)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-global-change-per-token-name");
                }

                setNullGlobalTokenChanges.insert(data.token_name);

            } else if (txout.scriptPubKey.IsNullTokenVerifierTxDataScript()) {

                if (!CheckVerifierTokenTxOut(txout, strError))
                    return state.DoS(100, false, REJECT_INVALID, strError);

                if (fContainsNullTokenVerifierTx)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-null-data-only-one-verifier-per-tx");

                fContainsNullTokenVerifierTx = true;
            }
        }
        /** TOKENS END */

        /** TOKENS START */
        bool isToken = false;
        int nType;
        bool fIsOwner;
        if (txout.scriptPubKey.IsTokenScript(nType, fIsOwner))
            isToken = true;
        
        // Check for transfers that don't meet the tokens units only if the tokenCache is not null
        if (isToken) {
            // Get the transfer transaction data from the scriptPubKey
            if (nType == TX_TRANSFER_TOKEN) {
                CTokenTransfer transfer;
                std::string address;
                if (!TransferTokenFromScript(txout.scriptPubKey, transfer, address))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-token-bad-deserialize");

                // insert into set, so that later on we can check token null data transactions
                setTokenTransferNames.insert(transfer.strName);

                // Check token name validity and get type
                KnownTokenType tokenType;
                if (!IsTokenNameValid(transfer.strName, tokenType)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-token-name-invalid");
                }

                // If the transfer is an ownership token. Check to make sure that it is OWNER_TOKEN_AMOUNT
                if (IsTokenNameAnOwner(transfer.strName)) {
                    if (transfer.nAmount != OWNER_TOKEN_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-owner-amount-was-not-1");
                }

                // If the transfer is a unique token. Check to make sure that it is UNIQUE_TOKEN_AMOUNT
                if (tokenType == KnownTokenType::UNIQUE || tokenType == KnownTokenType::USERNAME) {
                    if (transfer.nAmount != UNIQUE_TOKEN_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-unique-amount-was-not-1");
                }

                // If the transfer is a restricted channel token.
                if (tokenType == KnownTokenType::RESTRICTED) {
                    // TODO add checks here if any
                }

                // If the transfer is a qualifier channel token.
                if (tokenType == KnownTokenType::QUALIFIER || tokenType == KnownTokenType::SUB_QUALIFIER) {
                    if (transfer.nAmount < QUALIFIER_TOKEN_MIN_AMOUNT || transfer.nAmount > QUALIFIER_TOKEN_MAX_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-qualifier-amount-must be between 1 - 100");
                }
                
                // Specific check and error message to go with to make sure the amount is 0
                if (txout.nValue != 0)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-transfer-amount-isn't-zero");

            } else if (nType == TX_NEW_TOKEN) {
                // Specific check and error message to go with to make sure the amount is 0
                if (txout.nValue != 0)
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-issued-amount-isn't-zero");
            } else if (nType == TX_REISSUE_TOKEN) {
                // We only want to not accept these txes when checking them from CheckBlock.
                if (fBlockCheck) {
                    if (txout.nValue != 0) {
                        return state.DoS(0, false, REJECT_INVALID, "bad-txns-token-reissued-amount-isn't-zero");
                    }
                }

                if (fMempoolCheck) {
                    // Don't accept to the mempool no matter what on these types of transactions
                    if (txout.nValue != 0) {
                        return state.DoS(0, false, REJECT_INVALID, "bad-mempool-txns-token-reissued-amount-isn't-zero");
                    }
                }
            } else {
                return state.DoS(0, false, REJECT_INVALID, "bad-token-type-not-any-of-the-main-three");
            }
        }
    }

    // Check for Add Tag Burn Fee
    if (nCountAddTagOuts) {
        if (!tx.CheckAddingTagBurnFee(nCountAddTagOuts))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-doesn't-contain-required-burn-fee-for-adding-tags");
    }

    for (auto entry: mapNullDataTxCount) {
        if (entry.first.first.front() == RESTRICTED_CHAR) {
            std::string ownerToken = entry.first.first.substr(1,  entry.first.first.size()); // $TOKEN into TOKEN
            if (!setTokenTransferNames.count(ownerToken + OWNER_TAG)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-contains-restricted-token-null-tx-without-token-transfer");
            }
        } else { // must be a qualifier token QUALIFIER_CHAR
            if (!setTokenTransferNames.count(entry.first.first)) {
                return state.DoS(100, false, REJECT_INVALID,
                                 "bad-txns-tx-contains-qualifier-token-null-tx-without-token-transfer");
            }
        }
    }

    for (auto name: setNullGlobalTokenChanges) {
        if (name.size() == 0)
            return state.DoS(100, false, REJECT_INVALID,"bad-txns-tx-contains-global-token-null-tx-with-null-token-name");

        std::string rootName = name.substr(1,  name.size()); // $TOKEN into TOKEN
        if (!setTokenTransferNames.count(rootName + OWNER_TAG)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-contains-global-token-null-tx-without-token-transfer");
        }
    }

    /** TOKENS END */

    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin)
        {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        }
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");

        for (auto vout : tx.vout) {
            if (vout.scriptPubKey.IsTokenScript() || vout.scriptPubKey.IsNullToken()) {
                return state.DoS(0, error("%s: coinbase contains token transaction", __func__),
                                 REJECT_INVALID, "bad-txns-coinbase-contains-token-txes");
            }
        }
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    /** TOKENS START */
    if (tx.IsNewToken()) {
        /** Verify the reissue tokens data */
        std::string strError = "";
        if(!tx.VerifyNewToken(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewToken token;
        std::string strAddress;
        if (!TokenFromTransaction(tx, token, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-token-from-transaction");

        // Validate the new tokens information
        if (!IsNewOwnerTxValid(tx, token.strName, strAddress, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        if(!CheckNewToken(token, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

    } else if (tx.IsReissueToken()) {

        /** Verify the reissue tokens data */
        std::string strError;
        if (!tx.VerifyReissueToken(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CReissueToken reissue;
        std::string strAddress;
        if (!ReissueTokenFromTransaction(tx, reissue, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-token");

        if (!CheckReissueToken(reissue, strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        // Get the tokenType
        KnownTokenType type;
        IsTokenNameValid(reissue.strName, type);

        // If this is a reissuance of a restricted token, mark it as such, so we can check to make sure only valid verifier string tx are added to the chain
        if (type == KnownTokenType::RESTRICTED) {
            CNullTokenTxVerifierString new_verifier;
            bool fNotFound = false;

            // Try and get the verifier string if it was changed
            if (!tx.GetVerifierStringFromTx(new_verifier, strError, fNotFound)) {
                // If it return false for any other reason besides not being found, fail the transaction check
                if (!fNotFound) {
                    return state.DoS(100, false, REJECT_INVALID,
                                     "bad-txns-reissue-restricted-verifier-" + strError);
                }
            }

            fContainsRestrictedTokenReissue = true;
        }

    } else if (tx.IsNewUniqueToken()) {

        /** Verify the unique tokens data */
        std::string strError = "";
        if (!tx.VerifyNewUniqueToken(strError)) {
            return state.DoS(100, false, REJECT_INVALID, strError);
        }


        for (auto out : tx.vout)
        {
            if (IsScriptNewUniqueToken(out.scriptPubKey))
            {
                CNewToken token;
                std::string strAddress;
                if (!TokenFromScript(out.scriptPubKey, token, strAddress))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-check-transaction-issue-unique-token-serialization");

                if (!CheckNewToken(token, strError))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-unique" + strError);
            }
        }
    } else if (tx.IsNewMsgChannelToken()) {
        /** Verify the msg channel tokens data */
        std::string strError = "";
        if(!tx.VerifyNewMsgChannelToken(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewToken token;
        std::string strAddress;
        if (!MsgChannelTokenFromTransaction(tx, token, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-from-transaction");

        if (!CheckNewToken(token, strError))
            return state.DoS(100, error("%s: %s", __func__, strError), REJECT_INVALID, "bad-txns-issue-msgchannel" + strError);

    } else if (tx.IsNewQualifierToken()) {
        /** Verify the qualifier channel tokens data */
        std::string strError = "";
        if(!tx.VerifyNewQualfierToken(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        CNewToken token;
        std::string strAddress;
        if (!QualifierTokenFromTransaction(tx, token, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-from-transaction");

        if (!CheckNewToken(token, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualfier" + strError);

    } else if (tx.IsNewRestrictedToken()) {
        /** Verify the restricted tokens data. */
        std::string strError = "";
        if(!tx.VerifyNewRestrictedToken(strError))
            return state.DoS(100, false, REJECT_INVALID, strError);

        // Get token data
        CNewToken token;
        std::string strAddress;
        if (!RestrictedTokenFromTransaction(tx, token, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-from-transaction");

        if (!CheckNewToken(token, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted" + strError);

        // Get verifier string
        CNullTokenTxVerifierString verifier;
        if (!tx.GetVerifierStringFromTx(verifier, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-verifier-search-" + strError);

        // Mark that this transaction has a restricted token issuance, for checks later with the verifier string tx
        fContainsNewRestrictedToken = true;
    } else if (tx.IsNewUsername()) {
        /** Verify the username tokens data */
        std::string strError = "";
        if (!tx.VerifyNewUsername(strError)) {
            return state.DoS(100, false, REJECT_INVALID, strError);
        }

        CNewToken token;
        std::string strAddress;
        if (!UsernameFromTransaction(tx, token, strAddress))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-username-from-transaction");

        if (!CheckNewToken(token, strError))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-username" + strError);

    } else {
        // Fail if transaction contains any non-transfer token scripts and hasn't conformed to one of the
        // above transaction types.  Also fail if it contains OP_YONA_TOKEN opcode but wasn't a valid script.
        for (auto out : tx.vout) {
            int nType;
            bool _isOwner;
            if (out.scriptPubKey.IsTokenScript(nType, _isOwner)) {
                if (nType != TX_TRANSFER_TOKEN) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-token-transaction");
                }
            } else {
                if (out.scriptPubKey.Find(OP_YONA_TOKEN)) {
                    if (out.scriptPubKey[0] != OP_YONA_TOKEN) {
                        return state.DoS(100, false, REJECT_INVALID,
                                         "bad-txns-op-yona-token-not-in-right-script-location");
                    }
                }
            }
        }
    }

    // Check to make sure that if there is a verifier string, that there is also a issue or reissuance of a restricted token
    if (fContainsNullTokenVerifierTx && !fContainsRestrictedTokenReissue && !fContainsNewRestrictedToken)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-cointains-verifier-string-without-restricted-token-issuance-or-reissuance");

    // If there is a restricted token issuance, verify that there is a verifier tx associated with it.
    if (fContainsNewRestrictedToken && !fContainsNullTokenVerifierTx) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-tx-cointains-restricted-token-issuance-without-verifier");
    }

    // we allow restricted token reissuance without having a verifier string transaction, we don't force it to be update
    /** TOKENS END */

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    CAmount nValueIn = 0;
    bool nTokenTransaction = false;

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        if (coin.IsToken())
            nTokenTransaction = true;

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // If prev is coinstake, check that it's matured
        if (coin.IsCoinStake() && nSpendHeight - coin.nHeight < COINSTAKE_MATURITY) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinstake",
                strprintf("tried to spend coinstake at depth %d, %d, %d", nSpendHeight, coin.nHeight, nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange", false, "", tx.GetHash());
        }
    }

    const CAmount value_out = tx.GetValueOut();
    CAmount txfee_aux = 0;

    if (!tx.IsCoinStake()) {
        txfee_aux = nValueIn - value_out;
    }

    if (!tx.IsCoinStake()) {
        if (nValueIn < value_out) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)));
        }

        // Tally transaction fees
        if (!MoneyRange(txfee_aux)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-out-of-range");
        }

        // Enforce transaction fees for every block
        const CAmount minfee = GetStaticFee(nTokenTransaction, nSpendHeight);
        if (txfee_aux < minfee)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-not-enough", false,
                strprintf("txfee (%s) < minfee (%s)", FormatMoney(txfee_aux), FormatMoney(minfee)));
    }

    txfee = txfee_aux;
    return true;
}

//! Check to make sure that the inputs and outputs CAmount match exactly.
bool Consensus::CheckTxTokens(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, int64_t nSpendTime, CTokensCache* tokenCache, bool fCheckMempool, std::vector<std::pair<std::string, uint256> >& vPairReissueTokens, const bool fRunningUnitTests, std::set<CMessage>* setMessages, int64_t nBlocktime,   std::vector<std::pair<std::string, CNullTokenTxData>>* myNullTokenData)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missing-or-spent", false,
                         strprintf("%s: inputs missing/spent", __func__), tx.GetHash());
    }

    // Create map that stores the amount of an token transaction input. Used to verify no tokens are burned
    std::map<std::string, CAmount> totalInputs;

    std::map<std::string, std::string> mapAddresses;

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        if (coin.IsToken()) {
            CTokenOutputEntry data;
            if (!GetTokenData(coin.out.scriptPubKey, data))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-failed-to-get-token-from-script", false, "", tx.GetHash());

            // Add to the total value of tokens in the inputs
            if (totalInputs.count(data.tokenName))
                totalInputs.at(data.tokenName) += data.nAmount;
            else
                totalInputs.insert(make_pair(data.tokenName, data.nAmount));

            if (AreMessagesDeployed()) {
                mapAddresses.insert(make_pair(data.tokenName,EncodeDestination(data.destination)));
            }

            if (IsTokenNameAnRestricted(data.tokenName)) {
                if (tokenCache->CheckForAddressRestriction(data.tokenName, EncodeDestination(data.destination), true)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-restricted-token-transfer-from-frozen-address", false, "", tx.GetHash());
                }
            }

            if ((int64_t)data.nTimeLock > ((int64_t)data.nTimeLock < LOCKTIME_THRESHOLD ? (int64_t)nSpendHeight : nSpendTime)) {
                std::string errorMsg = strprintf("Tried to spend token before %d", data.nTimeLock);
                return state.DoS(100, false,
                    REJECT_INVALID, "bad-txns-premature-spend-timelock" + errorMsg);
            }
        }
    }

    // Create map that stores the amount of an token transaction output. Used to verify no tokens are burned
    std::map<std::string, bool> tokenRoyalties;
    std::map<std::string, CAmount> totalOutputs;
    int index = 0;
    int64_t currentTime = GetTime();
    std::string strError = "";
    int i = 0;
    for (const auto& txout : tx.vout) {
        i++;
        bool fIsToken = false;
        int nType = 0;
        int nScriptType = 0;
        bool fIsOwner = false;
        if (txout.scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner))
            fIsToken = true;

        if (tokenCache) {
            if (fIsToken && !AreTokensDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-is-token-and-token-not-active");

            if (txout.scriptPubKey.IsNullToken()) {
                if (!AreRestrictedTokensDeployed())
                    return state.DoS(100, false, REJECT_INVALID,
                                     "bad-tx-null-token-data-before-restricted-tokens-activated");

                if (txout.scriptPubKey.IsNullTokenTxDataScript()) {
                    if (!ContextualCheckNullTokenTxOut(txout, tokenCache, strError, myNullTokenData))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullGlobalRestrictionTokenTxDataScript()) {
                    if (!ContextualCheckGlobalTokenTxOut(txout, tokenCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else if (txout.scriptPubKey.IsNullTokenVerifierTxDataScript()) {
                    if (!ContextualCheckVerifierTokenTxOut(txout, tokenCache, strError))
                        return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());
                } else {
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-null-token-data-unknown-type", false, "", tx.GetHash());
                }
            }
        }

        if (nType == TX_TRANSFER_TOKEN) {
            CTokenTransfer transfer;
            std::string address = "";
            if (!TransferTokenFromScript(txout.scriptPubKey, transfer, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-token-transfer-bad-deserialize", false, "",
                                 tx.GetHash());

            if (!ContextualCheckTransferToken(tokenCache, transfer, address, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

            // Add to the total value of tokens in the outputs
            if (totalOutputs.count(transfer.strName))
                totalOutputs.at(transfer.strName) += transfer.nAmount;
            else
                totalOutputs.insert(make_pair(transfer.strName, transfer.nAmount));

            if (!fRunningUnitTests) {
                if (IsTokenNameAnOwner(transfer.strName)) {
                    if (transfer.nAmount != OWNER_TOKEN_AMOUNT)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-owner-amount-was-not-1", false, "", tx.GetHash());
                } else {
                    // For all other types of tokens, make sure they are sending the right type of units
                    CNewToken token;
                    if (!tokenCache->GetTokenMetaDataIfExists(transfer.strName, token))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-token-not-exist", false, "", tx.GetHash());

                    if (token.strName != transfer.strName)
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-database-corrupted", false, "", tx.GetHash());

                    if (!CheckAmountWithUnits(transfer.nAmount, token.units))
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-transfer-token-amount-not-match-units", false, "", tx.GetHash());

                    if (token.nHasRoyalties && token.nRoyaltiesAmount > 0)
                    {
                        if (tokenRoyalties.find(transfer.strName) == tokenRoyalties.end())
                            tokenRoyalties[transfer.strName] = false;

                        if (address == token.nRoyaltiesAddress && transfer.nAmount >= token.nRoyaltiesAmount && transfer.nTimeLock == 0)
                            tokenRoyalties[transfer.strName] = true;
                    }
                }
            }

            /** Get messages from the transaction, only used when getting called from ConnectBlock **/
            // Get the messages from the Tx unless they are expired
            if (AreMessagesDeployed() && fMessaging && setMessages) {
                if (IsTokenNameAnOwner(transfer.strName) || IsTokenNameAnMsgChannel(transfer.strName)) {
                    if (!transfer.message.empty()) {
                        if (transfer.nExpireTime == 0 || transfer.nExpireTime > currentTime) {
                            if (mapAddresses.count(transfer.strName)) {
                                if (mapAddresses.at(transfer.strName) == address) {
                                    COutPoint out(tx.GetHash(), index);
                                    CMessage message(out, transfer.strName, transfer.message,
                                                     transfer.nExpireTime, nBlocktime);
                                    setMessages->insert(message);
                                    LogPrintf("Got message: %s\n", message.ToString()); // TODO remove after testing
                                }
                            }
                        }
                    }
                }
            }
        } else if (nType == TX_REISSUE_TOKEN) {
            CReissueToken reissue;
            std::string address;
            if (!ReissueTokenFromScript(txout.scriptPubKey, reissue, address))
                return state.DoS(100, false, REJECT_INVALID, "bad-tx-token-reissue-bad-deserialize", false, "", tx.GetHash());

            if (mapReissuedTokens.count(reissue.strName)) {
                if (mapReissuedTokens.at(reissue.strName) != tx.GetHash())
                    return state.DoS(100, false, REJECT_INVALID, "bad-tx-reissue-chaining-not-allowed", false, "", tx.GetHash());
            } else {
                vPairReissueTokens.emplace_back(std::make_pair(reissue.strName, tx.GetHash()));
            }
        }
        index++;
    }

    auto iter = tokenRoyalties.begin();
    while (iter != tokenRoyalties.end()) {
        if (!iter->second) {
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-token-royalty-missing", false, "", tx.GetHash());
        }
        ++iter;
    }

    if (tokenCache) {
        if (tx.IsNewToken()) {
            // Get the token type
            CNewToken token;
            std::string address;
            if (!TokenFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, token, address)) {
                error("%s : Failed to get new token from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-serialzation-failed", false, "", tx.GetHash());
            }

            KnownTokenType tokenType;
            IsTokenNameValid(token.strName, tokenType);

            if (!ContextualCheckNewToken(tokenCache, token, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, strError);

        } else if (tx.IsReissueToken()) {
            CReissueToken reissue_token;
            std::string address;
            if (!ReissueTokenFromScript(tx.vout[tx.vout.size() - 1].scriptPubKey, reissue_token, address)) {
                error("%s : Failed to get new token from transaction: %s", __func__, tx.GetHash().GetHex());
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-serialzation-failed", false, "", tx.GetHash());
            }
            if (!ContextualCheckReissueToken(tokenCache, reissue_token, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-reissue-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewUniqueToken()) {
            if (!ContextualCheckUniqueTokenTx(tokenCache, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-unique-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewUsername()) {
            if (!ContextualCheckUsernameTokenTx(tokenCache, strError, tx))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-username-contextual-" + strError, false, "", tx.GetHash());
        } else if (tx.IsNewMsgChannelToken()) {
            if (!AreMessagesDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-before-messaging-is-active", false, "", tx.GetHash());

            CNewToken token;
            std::string strAddress;
            if (!MsgChannelTokenFromTransaction(tx, token, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-msgchannel-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewToken(tokenCache, token, strError, fCheckMempool))
                return state.DoS(100, error("%s: %s", __func__, strError), REJECT_INVALID,
                                 "bad-txns-issue-msgchannel-contextual-" + strError);
        } else if (tx.IsNewQualifierToken()) {
            if (!AreRestrictedTokensDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-before-it-is-active", false, "", tx.GetHash());

            CNewToken token;
            std::string strAddress;
            if (!QualifierTokenFromTransaction(tx, token, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualifier-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewToken(tokenCache, token, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-qualfier-contextual" + strError, false, "", tx.GetHash());

        } else if (tx.IsNewRestrictedToken()) {
            if (!AreRestrictedTokensDeployed())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-before-it-is-active", false, "", tx.GetHash());

            // Get token data
            CNewToken token;
            std::string strAddress;
            if (!RestrictedTokenFromTransaction(tx, token, strAddress))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-serialzation-failed", false, "", tx.GetHash());

            if (!ContextualCheckNewToken(tokenCache, token, strError, fCheckMempool))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-contextual" + strError, false, "", tx.GetHash());

            // Get verifier string
            CNullTokenTxVerifierString verifier;
            if (!tx.GetVerifierStringFromTx(verifier, strError))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-issue-restricted-verifier-search-" + strError, false, "", tx.GetHash());

            // Check the verifier string against the destination address
            if (!ContextualCheckVerifierString(tokenCache, verifier.verifier_string, strAddress, strError))
                return state.DoS(100, false, REJECT_INVALID, strError, false, "", tx.GetHash());

        } else {
            for (auto out : tx.vout) {
                int nType;
                int nScriptType;
                bool _isOwner;
                if (out.scriptPubKey.IsTokenScript(nType, nScriptType, _isOwner)) {
                    if (nType != TX_TRANSFER_TOKEN) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-token-transaction", false, "", tx.GetHash());
                    }
                } else {
                    if (out.scriptPubKey.Find(OP_YONA_TOKEN)) {
                        if (AreRestrictedTokensDeployed()) {
                            if (out.scriptPubKey[0] != OP_YONA_TOKEN) {
                                return state.DoS(100, false, REJECT_INVALID,
                                                 "bad-txns-op-yona-token-not-in-right-script-location", false, "", tx.GetHash());
                            }
                        } else {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-bad-token-script", false, "", tx.GetHash());
                        }
                    }
                }
            }
        }
    }

    for (const auto& outValue : totalOutputs) {
        if (!totalInputs.count(outValue.first)) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Trying to create outpoint for token that you don't have: %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }

        if (totalInputs.at(outValue.first) != outValue.second) {
            std::string errorMsg;
            errorMsg = strprintf("Bad Transaction - Tokens would be burnt %s", outValue.first);
            return state.DoS(100, false, REJECT_INVALID, "bad-tx-inputs-outputs-mismatch " + errorMsg, false, "", tx.GetHash());
        }
    }

    // Check the input size and the output size
    if (totalOutputs.size() != totalInputs.size()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-tx-token-inputs-size-does-not-match-outputs-size", false, "", tx.GetHash());
    }
    return true;
}
