// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "validation.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "primitives/transaction.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#endif

#include <stdint.h>
#include "tokens/tokens.h"

#include <univalue.h>
#include <tinyformat.h>
#include <timedata.h>

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, bool expanded = false)
{
    // Call into TxToUniv() in akila-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in akila-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    if (expanded) {
        uint256 txid = tx.GetHash();
        if (!(tx.IsCoinBase())) {
            const UniValue& oldVin = entry["vin"];
            UniValue newVin(UniValue::VARR);
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const CTxIn& txin = tx.vin[i];
                UniValue in = oldVin[i];

                // Add address and value info if spentindex enabled
                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
                if (GetSpentIndex(spentKey, spentInfo)) {
                    in.pushKV("value", ValueFromAmount(spentInfo.satoshis));
                    in.pushKV("valueSat", spentInfo.satoshis);
                    if (spentInfo.addressType == 1) {
                        in.pushKV("address", CAkilaAddress(CKeyID(spentInfo.addressHash)).ToString());
                    } else if (spentInfo.addressType == 2) {
                        in.pushKV("address", CAkilaAddress(CScriptID(spentInfo.addressHash)).ToString());
                    }
                }
                newVin.push_back(in);
            }
            entry.pushKV("vin", newVin);
        }

        const UniValue& oldVout = entry["vout"];
        UniValue newVout(UniValue::VARR);
        for (unsigned int i = 0; i < tx.vout.size(); i++) {
            const CTxOut& txout = tx.vout[i];
            UniValue out = oldVout[i];

            // Add spent information if spentindex is enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txid, i);
            if (GetSpentIndex(spentKey, spentInfo)) {
                out.pushKV("spentTxId", spentInfo.txid.GetHex());
                out.pushKV("spentIndex", (int)spentInfo.inputIndex);
                out.pushKV("spentHeight", spentInfo.blockHeight);
            }

            out.pushKV("valueSat", txout.nValue);
            newVout.push_back(out);
        }
        entry.pushKV("vout", newVout);
    }

    if (!hashBlock.IsNull()) {
        entry.pushKV("blockhash", hashBlock.GetHex());
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.pushKV("height", pindex->nHeight);
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            } else {
                entry.pushKV("height", -1);
                entry.pushKV("confirmations", 0);
            }
        }
    }
}

UniValue getrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (bool, optional, default=false) If false, return a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) akila address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        if (request.params[1].isNum()) {
            if (request.params[1].get_int() != 0) {
                fVerbose = true;
            }
        }
        else if(request.params[1].isBool()) {
            if(request.params[1].isTrue()) {
                fVerbose = true;
            }
        }
        else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid type provided. Verbose parameter must be a boolean.");
        }
    }

    CTransactionRef tx;

    uint256 hashBlock;
    if (!GetTransaction(hash, tx, GetParams().GetConsensus(), hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string(fTxIndex ? "No such mempool or blockchain transaction"
            : "No such mempool transaction. Use -txindex to enable blockchain transaction queries") +
            ". Use gettransaction for wallet transactions.");

    if (!fVerbose)
        return EncodeHexTx(*tx, RPCSerializationFlags());

    UniValue result(UniValue::VOBJ);
    TxToJSON(*tx, hashBlock, result, true);

    return result;
}

UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    std::set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = nullptr;

    uint256 hashBlock;
    if (!request.params[1].isNull())
    {
        hashBlock = uint256S(request.params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(*pcoinsTip, tx);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.nHeight];
                break;
            }
        }
    }

    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        if (!GetTransaction(oneTxid, tx, GetParams().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex, GetParams().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx)
        if (setTxids.count(tx->GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;



    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetIndexHash()) || (!chainActive.Contains(mapBlockIndex[merkleBlock.header.GetIndexHash()])))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    for (const uint256& hash : vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":(amount or object),\"data\":\"hex\",...}\n"
            "                     ( locktime ) ( replaceable )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs are addresses (paired with a AKILA amount, data or object specifying an token operation) or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nPaying for Token Operations:\n"
            "  Some operations require an amount of AKILA to be sent to a burn address:\n"
            "\n"
            "    Operation          Amount + Burn Address\n"
            "    transfer                 0\n"
            "    transferwithmessage      0\n"
            "    issue                  " + i64tostr(GetBurnAmount(KnownTokenType::ROOT) / COIN) + " to " + GetBurnAddress(KnownTokenType::ROOT) + "\n"
            "    issue (subtoken)       " + i64tostr(GetBurnAmount(KnownTokenType::SUB) / COIN) + " to " + GetBurnAddress(KnownTokenType::SUB) + "\n"
            "    issue_unique             " + i64tostr(GetBurnAmount(KnownTokenType::UNIQUE) / COIN) + " to " + GetBurnAddress(KnownTokenType::UNIQUE) + "\n"
            "    reissue                " + i64tostr(GetBurnAmount(KnownTokenType::REISSUE) / COIN) + " to " + GetBurnAddress(KnownTokenType::REISSUE) + "\n"
            "    issue_restricted      " + i64tostr(GetBurnAmount(KnownTokenType::RESTRICTED) / COIN) + " to " + GetBurnAddress(KnownTokenType::RESTRICTED) + "\n"
            "    reissue_restricted     " + i64tostr(GetBurnAmount(KnownTokenType::REISSUE) / COIN) + " to " + GetBurnAddress(KnownTokenType::REISSUE) + "\n"
            "    issue_qualifier       " + i64tostr(GetBurnAmount(KnownTokenType::QUALIFIER) / COIN) + " to " + GetBurnAddress(KnownTokenType::QUALIFIER) + "\n"
            "    issue_qualifier (sub)  " + i64tostr(GetBurnAmount(KnownTokenType::SUB_QUALIFIER) / COIN) + " to " + GetBurnAddress(KnownTokenType::SUB_QUALIFIER) + "\n"
            "    tag_addresses          " + "0.1 to " + GetBurnAddress(KnownTokenType::NULL_ADD_QUALIFIER) + " (per address)\n"
            "    untag_addresses        " + "0.1 to " + GetBurnAddress(KnownTokenType::NULL_ADD_QUALIFIER) + " (per address)\n"
            "    freeze_addresses         0\n"
            "    unfreeze_addresses       0\n"
            "    freeze_token             0\n"
            "    unfreeze_token           0\n"

            "\nTokens For Authorization:\n"
            "  These operations require a specific token input for authorization:\n"
            "    Root Owner Token:\n"
            "      reissue\n"
            "      issue_unique\n"
            "      issue_restricted\n"
            "      reissue_restricted\n"
            "      freeze_addresses\n"
            "      unfreeze_addresses\n"
            "      freeze_token\n"
            "      unfreeze_token\n"
            "    Root Qualifier Token:\n"
            "      issue_qualifier (when issuing subqualifier)\n"
            "    Qualifier Token:\n"
            "      tag_addresses\n"
            "      untag_addresses\n"

            "\nOutput Ordering:\n"
            "  Token operations require the following:\n"
            "    1) All coin outputs come first (including the burn output).\n"
            "    2) The owner token change output comes next (if required).\n"
            "    3) An issue, reissue, or any number of transfers comes last\n"
            "       (different types can't be mixed in a single transaction).\n"

            "\nArguments:\n"
            "1. \"inputs\"                                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",                      (string, required) The transaction id\n"
            "         \"vout\":n,                         (number, required) The output number\n"
            "         \"sequence\":n                      (number, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"                               (object, required) a json object with outputs\n"
            "     {\n"
            "       \"address\":                          (string, required) The destination akila address.\n"
            "                                               Each output must have a different address.\n"
            "         x.xxx                             (number or string, required) The AKILA amount\n"
            "           or\n"
            "         {                                 (object) A json object of tokens to send\n"
            "           \"transfer\":\n"
            "             {\n"
            "               \"token-name\":               (string, required) token name\n"
            "               token-quantity              (number, required) the number of raw units to transfer\n"
            "               ,...\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object of describing the transfer and message contents to send\n"
            "           \"transferwithmessage\":\n"
            "             {\n"
            "               \"token-name\":              (string, required) token name\n"
            "               token-quantity,            (number, required) the number of raw units to transfer\n"
            "               \"message\":\"hash\",          (string, required) ipfs hash or a txid hash\n"
            "               \"expire_time\": n           (number, required) utc time in seconds to expire the message\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new tokens to issue\n"
            "           \"issue\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\",  (string, required) new token name\n"
            "               \"token_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"units\":[1-8],              (number, required) display units, between 1 (integral) to 8 (max precision)\n"
            "               \"reissuable\":[0-1],         (number, required) 1=reissuable token\n"
            "               \"has_ipfs\":[0-1],           (number, required) 1=passing ipfs_hash\n"
            "               \"ipfs_hash\":\"hash\"          (string, optional) an ipfs hash for discovering token metadata\n"
            // TODO if we decide to remove the consensus check from issue 675 https://github.com/AkilaProject/Akilacoin/issues/675
   //TODO"               \"custom_owner_address\": \"addr\" (string, optional) owner token will get sent to this address if set\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing new unique tokens to issue\n"
            "           \"issue_unique\":\n"
            "             {\n"
            "               \"root_name\":\"root-name\",         (string, required) name of the token the unique token(s) \n"
            "                                                      are being issued under\n"
            "               \"token_tags\":[\"token_tag\", ...], (array, required) the unique tag for each token which is to be issued\n"
            "               \"ipfs_hashes\":[\"hash\", ...],     (array, optional) ipfs hashes corresponding to each supplied tag \n"
            "                                                      (should be same size as \"token_tags\")\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing follow-on token issue.\n"
            "           \"reissue\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\", (string, required) name of token to be reissued\n"
            "               \"token_quantity\":n,          (number, required) the number of raw units to issue\n"
            "               \"reissuable\":[0-1],          (number, optional) default is 1, 1=reissuable token\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional) An ipfs hash for discovering token metadata, \n"
            "                                                Overrides the current ipfs hash if given\n"
            "               \"owner_change_address\"       (string, optional) the address where the owner token will be sent to. \n"
            "                                                If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing how restricted token to issue\n"
            "           \"issue_restricted\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\",(string, required) new token name\n"
            "               \"token_quantity\":n,         (number, required) the number of raw units to issue\n"
            "               \"verifier_string\":\"text\", (string, required) the verifier string to be used for a restricted \n"
            "                                               token transfer verification\n"
            "               \"units\":[0-8],              (number, required) display units, between 0 (integral) and 8 (max precision)\n"
            "               \"reissuable\":[0-1],         (number, required) 1=reissuable token\n"
            "               \"has_ipfs\":[0-1],           (number, required) 1=passing ipfs_hash\n"
            "               \"ipfs_hash\":\"hash\",       (string, optional) an ipfs hash for discovering token metadata\n"
            "               \"owner_change_address\"      (string, optional) the address where the owner token will be sent to. \n"
            "                                               If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing follow-on token issue.\n"
            "           \"reissue_restricted\":\n"
            "             {\n"
            "               \"token_name\":\"token-name\", (string, required) name of token to be reissued\n"
            "               \"token_quantity\":n,          (number, required) the number of raw units to issue\n"
            "               \"reissuable\":[0-1],          (number, optional) default is 1, 1=reissuable token\n"
            "               \"verifier_string\":\"text\",  (string, optional) the verifier string to be used for a restricted token \n"
            "                                                transfer verification\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional) An ipfs hash for discovering token metadata, \n"
            "                                                Overrides the current ipfs hash if given\n"
            "               \"owner_change_address\"       (string, optional) the address where the owner token will be sent to. \n"
            "                                                If not given, it will be sent to the output address\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing a new qualifier to issue.\n"
            "           \"issue_qualifier\":\n"
            "             {\n"
            "               \"token_name\":\"token_name\", (string, required) a qualifier name (starts with '#')\n"
            "               \"token_quantity\":n,          (numeric, optional, default=1) the number of units to be issued (1 to 10)\n"
            "               \"has_ipfs\":[0-1],            (boolean, optional, default=false), whether ifps hash is going \n"
            "                                                to be added to the token\n"
            "               \"ipfs_hash\":\"hash\",        (string, optional but required if has_ipfs = 1), an ipfs hash or a \n"
            "                                                txid hash once messaging is activated\n"
            "               \"root_change_address\"        (string, optional) Only applies when issuing subqualifiers.\n"
            "                                                The address where the root qualifier will be sent.\n"
            "                                                If not specified, it will be sent to the output address.\n"
            "               \"change_quantity\":\"qty\"    (numeric, optional) the token change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be tagged.\n"
            "                                             The address in the key will used as the token change address.\n"
            "           \"tag_addresses\":\n"
            "             {\n"
            "               \"qualifier\":\"qualifier\",          (string, required) a qualifier name (starts with '#')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be tagged (up to 10)\n"
            "               \"change_quantity\":\"qty\",          (numeric, optional) the token change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be untagged.\n"
            "                                             The address in the key will be used as the token change address.\n"
            "           \"untag_addresses\":\n"
            "             {\n"
            "               \"qualifier\":\"qualifier\",          (string, required) a qualifier name (starts with '#')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be untagged (up to 10)\n"
            "               \"change_quantity\":\"qty\",          (numeric, optional) the token change amount (defaults to 1)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be frozen.\n"
            "                                             The address in the key will used as the owner change address.\n"
            "           \"freeze_addresses\":\n"
            "             {\n"
            "               \"token_name\":\"token_name\",        (string, required) a restricted token name (starts with '$')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be frozen (up to 10)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing addresses to be frozen.\n"
            "                                             The address in the key will be used as the owner change address.\n"
            "           \"unfreeze_addresses\":\n"
            "             {\n"
            "               \"token_name\":\"token_name\",        (string, required) a restricted token name (starts with '$')\n"
            "               \"addresses\":[\"addr\", ...],        (array, required) the addresses to be untagged (up to 10)\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing an token to be frozen.\n"
            "                                             The address in the key will used as the owner change address.\n"
            "           \"freeze_token\":\n"
            "             {\n"
            "               \"token_name\":\"token_name\",        (string, required) a restricted token name (starts with '$')\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "         {                                 (object) A json object describing an token to be frozen.\n"
            "                                             The address in the key will be used as the owner change address.\n"
            "           \"unfreeze_token\":\n"
            "             {\n"
            "               \"token_name\":\"token_name\",        (string, required) a restricted token name (starts with '$')\n"
            "             }\n"
            "         }\n"
            "           or\n"
            "       \"data\": \"hex\"                       (string, required) The key is \"data\", the value is hex encoded data\n"
            "       ,...\n"
            "     }\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
//            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
//            "                                        Allows this transaction to be replaced by a transaction with higher fees.\n"
//            "                                        If provided, it is an error if explicit sequence numbers are incompatible.\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueTokenXXXXXXXXXXXXXXXXXhhZGt\\\":500,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue\\\":{\\\"token_name\\\":\\\"MYTOKEN\\\",\\\"token_quantity\\\":1000000,\\\"units\\\":1,\\\"reissuable\\\":0,\\\"has_ipfs\\\":1,\\\"ipfs_hash\\\":\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueRestrictedXXXXXXXXXXXXzJZ1q\\\":1500,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue_restricted\\\":{\\\"token_name\\\":\\\"$MYTOKEN\\\",\\\"token_quantity\\\":1000000,\\\"verifier_string\\\":\\\"#TAG & !KYC\\\",\\\"units\\\":1,\\\"reissuable\\\":0,\\\"has_ipfs\\\":1,\\\"ipfs_hash\\\":\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\" \"{\\\"RXissueUniqueTokenXXXXXXXXXXWEAe58\\\":20,\\\"change_address\\\":change_amount,\\\"issuer_address\\\":{\\\"issue_unique\\\":{\\\"root_name\\\":\\\"MYTOKEN\\\",\\\"token_tags\\\":[\\\"ALPHA\\\",\\\"BETA\\\"],\\\"ipfs_hashes\\\":[\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\",\\\"43f81c6f2c0593bde5a85e09ae662816eca80797\\\"]}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"mytoken\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":{\\\"transfer\\\":{\\\"MYTOKEN\\\":50}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"mytoken\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":{\\\"transferwithmessage\\\":{\\\"MYTOKEN\\\":50,\\\"message\\\":\\\"hash\\\",\\\"expire_time\\\": utc_time}}}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"myownership\\\",\\\"vout\\\":0}]\" \"{\\\"issuer_address\\\":{\\\"reissue\\\":{\\\"token_name\\\":\\\"MYTOKEN\\\",\\\"token_quantity\\\":2000000}}}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"mycoin\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ, UniValue::VNUM}, true);
    if (request.params[0].isNull() || request.params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = request.params[0].get_array();
    UniValue sendTo = request.params[1].get_obj();

    CMutableTransaction rawTx;
    rawTx.nTime = GetAdjustedTime();

    if (!request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

//    bool rbfOptIn = request.params[3].isTrue();

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence;
//        if (rbfOptIn) {
//            nSequence = MAX_BIP125_RBF_SEQUENCE;
//        } else if (rawTx.nLockTime) {
//            nSequence = std::numeric_limits<uint32_t>::max() - 1;
//        } else {
//            nSequence = std::numeric_limits<uint32_t>::max();
//        }

        if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    auto currentActiveTokenCache = GetCurrentTokenCache();

    std::set<CTxDestination> destinations;
    std::vector<std::string> addrList = sendTo.getKeys();
    for (const std::string& name_ : addrList) {

        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Akila address: ") + name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }

            CScript scriptPubKey = GetScriptForDestination(destination);
            CScript ownerPubKey = GetScriptForDestination(destination);


            if (sendTo[name_].type() == UniValue::VNUM || sendTo[name_].type() == UniValue::VSTR) {
                CAmount nAmount = AmountFromValue(sendTo[name_]);
                CTxOut out(nAmount, scriptPubKey);
                rawTx.vout.push_back(out);
            }
            /** AKILA COIN START **/
            else if (sendTo[name_].type() == UniValue::VOBJ) {
                auto token_ = sendTo[name_].get_obj();
                auto tokenKey_ = token_.getKeys()[0];

                if (tokenKey_ == "issue")
                {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_name");

                    const UniValue& token_quantity = find_value(tokenData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_quantity");

                    const UniValue& units = find_value(tokenData, "units");
                    if (!units.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: units");

                    const UniValue& reissuable = find_value(tokenData, "reissuable");
                    if (!reissuable.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: reissuable");

                    const UniValue& has_ipfs = find_value(tokenData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");
// TODO, if we decide to remove the consensus check https://github.com/AkilaProject/Akilacoin/issues/675, remove or add the code (requires consensus change)
//                    const UniValue& custom_owner_address = find_value(tokenData, "custom_owner_address");
//                    if (!custom_owner_address.isNull()) {
//                        CTxDestination dest = DecodeDestination(custom_owner_address.get_str());
//                        if (!IsValidDestination(dest)) {
//                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, invalid destination: custom_owner_address");
//                        }
//
//                        ownerPubKey = GetScriptForDestination(dest);
//                    }


                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        ipfs_hash = find_value(tokenData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");
                    }


                    if (IsTokenNameAnRestricted(token_name.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, token_name can't be a restricted token name. Please use issue_restricted with the correct parameters");

                    CAmount nAmount = AmountFromValue(token_quantity);

                    bool hasRoyalties = false;
                    std::string royaltiesAddress = "";
                    CAmount royaltiesAmount = 0;

                    // Create a new token
                    CNewToken token(
                        token_name.get_str(), nAmount, units.get_int(),
                        reissuable.get_int(), has_ipfs.get_int(), DecodeTokenData(ipfs_hash.get_str()),
                        hasRoyalties ? 1 : 0, royaltiesAddress, royaltiesAmount
                    );

                    // Verify that data
                    std::string strError = "";
                    if (!ContextualCheckNewToken(currentActiveTokenCache, token, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the token transaction
                    token.ConstructTransaction(scriptPubKey);

                    KnownTokenType type;
                    if (IsTokenNameValid(token.strName, type)) {
                        if (type != KnownTokenType::UNIQUE && type != KnownTokenType::USERNAME && type != KnownTokenType::MSGCHANNEL) {
                            token.ConstructOwnerTransaction(ownerPubKey);

                            // Push the scriptPubKey into the vouts.
                            CTxOut ownerOut(0, ownerPubKey);
                            rawTx.vout.push_back(ownerOut);
                        }
                    } else {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, ("Invalid parameter, invalid token name"));
                    }

                    // Push the scriptPubKey into the vouts.
                    CTxOut out(0, scriptPubKey);
                    rawTx.vout.push_back(out);

                }
                else if (tokenKey_ == "issue_unique")
                {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_unique\": {\"root_name\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& root_name = find_value(tokenData, "root_name");
                    if (!root_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: root_name");

                    const UniValue& token_tags = find_value(tokenData, "token_tags");
                    if (!token_tags.isArray() || token_tags.size() < 1)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_tags");

                    const UniValue& ipfs_hashes = find_value(tokenData, "ipfs_hashes");
                    if (!ipfs_hashes.isNull()) {
                        if (!ipfs_hashes.isArray() || ipfs_hashes.size() != token_tags.size()) {
                            if (!ipfs_hashes.isNum())
                                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                                   "Invalid parameter, missing token metadata for key: units");
                        }
                    }

                    // Create the scripts for the change of the ownership token
                    CScript scriptTransferOwnerToken = GetScriptForDestination(destination);
                    CTokenTransfer tokenTransfer(root_name.get_str() + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
                    tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, scriptTransferOwnerToken);
                    rawTx.vout.push_back(out);

                    // Create the tokens
                    for (int i = 0; i < (int)token_tags.size(); i++) {

                        // Create a new token
                        CNewToken token;
                        if (ipfs_hashes.isNull()) {
                            token = CNewToken(GetUniqueTokenName(root_name.get_str(), token_tags[i].get_str()),
                                              UNIQUE_TOKEN_AMOUNT,  UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE, 0, "",
                                              UNIQUE_TOKENS_HAS_ROYALTIES, UNIQUE_TOKENS_ROYALTIES_ADDRESS,
                                              UNIQUE_TOKENS_ROYALTIES_AMOUNT
                            );
                        } else {
                            token = CNewToken(GetUniqueTokenName(root_name.get_str(), token_tags[i].get_str()),
                                              UNIQUE_TOKEN_AMOUNT, UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE, 1, DecodeTokenData(ipfs_hashes[i].get_str()),
                                              UNIQUE_TOKENS_HAS_ROYALTIES, UNIQUE_TOKENS_ROYALTIES_ADDRESS,
                                              UNIQUE_TOKENS_ROYALTIES_AMOUNT
                            );
                        }

                        // Verify that data
                        std::string strError = "";
                        if (!ContextualCheckNewToken(currentActiveTokenCache, token, strError))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                        // Construct the token transaction
                        scriptPubKey = GetScriptForDestination(destination);
                        token.ConstructTransaction(scriptPubKey);

                        // Push the scriptPubKey into the vouts.
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);

                    }
                }
                else if (tokenKey_ == "reissue")
                {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"reissue\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto reissueData = token_.getValues()[0].get_obj();

                    CReissueToken reissueObj;

                    /**-------Process the reissue data-------**/
                    const UniValue& token_name = find_value(reissueData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: token_name");

                    const UniValue& token_quantity = find_value(reissueData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing reissue data for key: token_quantity");

                    const UniValue& reissuable = find_value(reissueData, "reissuable");
                    if (!reissuable.isNull()) {
                        if (!reissuable.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: reissuable");

                        int nReissuable = reissuable.get_int();
                        if (nReissuable > 1 || nReissuable < 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, reissuable data must be a 0 or 1");

                        reissueObj.nReissuable = int8_t(nReissuable);
                    }

                    const UniValue& ipfs_hash = find_value(reissueData, "ipfs_hash");
                    if (!ipfs_hash.isNull()) {
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: ipfs_hash");
                        reissueObj.strIPFSHash = DecodeTokenData(ipfs_hash.get_str());
                    }

                    bool fHasOwnerChange = false;
                    const UniValue& owner_change_address = find_value(reissueData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, owner_change_address is not a valid Akilacoin address");

                    if (IsTokenNameAnRestricted(token_name.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, token_name can't be a restricted token name. Please use reissue_restricted with the correct parameters");

                    // Add the received data into the reissue object
                    reissueObj.strName = token_name.get_str();
                    reissueObj.nAmount = AmountFromValue(token_quantity);

                    // Validate the the object is valid
                    std::string strError;
                    if (!ContextualCheckReissueToken(currentActiveTokenCache, reissueObj, strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                    // Create the scripts for the change of the ownership token
                    CScript owner_token_transfer_script;
                    if (fHasOwnerChange)
                        owner_token_transfer_script = GetScriptForDestination(DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_token_transfer_script = GetScriptForDestination(destination);

                    CTokenTransfer transfer_owner(token_name.get_str() + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
                    transfer_owner.ConstructTransaction(owner_token_transfer_script);

                    // Create the scripts for the reissued tokens
                    CScript scriptReissueToken = GetScriptForDestination(destination);
                    reissueObj.ConstructTransaction(scriptReissueToken);

                    // Create the CTxOut for the owner token
                    CTxOut out(0, owner_token_transfer_script);
                    rawTx.vout.push_back(out);

                    // Create the CTxOut for the reissue token
                    CTxOut out2(0, scriptReissueToken);
                    rawTx.vout.push_back(out2);

                } else if (tokenKey_ == "transfer") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"token_name\": amount, ...} }"));

                    UniValue transferData = token_.getValues()[0].get_obj();

                    auto keys = transferData.getKeys();

                    if (keys.size() == 0)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"transfer\": {\"token_name\": amount, ...} }"));

                    UniValue token_quantity;
                    for (auto token_name : keys) {
                        token_quantity = find_value(transferData, token_name);

                        if (!token_quantity.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or invalid quantity");

                        CAmount nAmount = AmountFromValue(token_quantity);

                        // Create a new transfer
                        // ToDo: Pass timelock here
                        CTokenTransfer transfer(token_name, nAmount, 0);

                        // Verify
                        std::string strError = "";
                        if (!transfer.IsValid(strError)) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                        }

                        // Construct transaction
                        CScript scriptPubKey = GetScriptForDestination(destination);
                        transfer.ConstructTransaction(scriptPubKey);

                        // Push into vouts
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);
                    }
                } else if (tokenKey_ == "transferwithmessage") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"transferwithmessage\": {\"token_name\": amount, \"message\": messagehash, \"expire_time\": utc_time} }"));

                    UniValue transferData = token_.getValues()[0].get_obj();

                    auto keys = transferData.getKeys();

                    if (keys.size() == 0)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"transferwithmessage\": {\"token_name\": amount, \"message\": messagehash, \"expire_time\": utc_time} }"));

                    UniValue token_quantity;
                    std::string token_name = keys[0];

                    if (!IsTokenNameValid(token_name)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing valid token name to transferwithmessage");

                        const UniValue &token_quantity = find_value(transferData, token_name);
                        if (!token_quantity.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing or invalid quantity");

                        const UniValue &message = find_value(transferData, "message");
                        if (!message.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue data for key: message");

                        const UniValue &expire_time = find_value(transferData, "expire_time");
                        if (!expire_time.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue data for key: expire_time");

                        CAmount nAmount = AmountFromValue(token_quantity);

                        // Create a new transfer
                        // ToDo: Pass timelock here
                        CTokenTransfer transfer(token_name, nAmount, 0, DecodeTokenData(message.get_str()),
                                                expire_time.get_int64());

                        // Verify
                        std::string strError = "";
                        if (!transfer.IsValid(strError)) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                        }

                        // Construct transaction
                        CScript scriptPubKey = GetScriptForDestination(destination);
                        transfer.ConstructTransaction(scriptPubKey);

                        // Push into vouts
                        CTxOut out(0, scriptPubKey);
                        rawTx.vout.push_back(out);
                    }
                } else if (tokenKey_ == "issue_restricted") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_restricted\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_name");

                    const UniValue& token_quantity = find_value(tokenData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_quantity");

                    const UniValue& verifier_string = find_value(tokenData, "verifier_string");
                    if (!verifier_string.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token_data for key: verifier_string");

                    const UniValue& units = find_value(tokenData, "units");
                    if (!units.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: units");

                    const UniValue& reissuable = find_value(tokenData, "reissuable");
                    if (!reissuable.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: reissuable");

                    const UniValue& has_ipfs = find_value(tokenData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");

                    bool fHasOwnerChange = false;
                    const UniValue& owner_change_address = find_value(tokenData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, owner_change_address is not a valid Akilacoin address");

                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        ipfs_hash = find_value(tokenData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");
                    }

                    std::string strTokenName = token_name.get_str();

                    if (!IsTokenNameAnRestricted(strTokenName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, token_name must be a restricted token name. e.g $TOKEN_NAME");

                    CAmount nAmount = AmountFromValue(token_quantity);

                    // Strip the white spaces from the verifier string
                    std::string strippedVerifierString = GetStrippedVerifierString(verifier_string.get_str());

                    // Check the restricted token destination address, and make sure it validates with the verifier string
                    std::string strError = "";
                    if (!ContextualCheckVerifierString(currentActiveTokenCache, strippedVerifierString, EncodeDestination(destination), strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parmeter, verifier string is not. Please check the syntax. Error Msg - " + strError));

                    bool hasRoyalties = false;
                    std::string royaltiesAddress = "";
                    CAmount royaltiesAmount = 0;

                    // Create a new token
                    CNewToken token(
                        strTokenName, nAmount, units.get_int(),
                        reissuable.get_int(), has_ipfs.get_int(), DecodeTokenData(ipfs_hash.get_str()),
                        hasRoyalties ? 1 : 0, royaltiesAddress, royaltiesAmount
                    );

                    // Verify the new token data
                    if (!ContextualCheckNewToken(currentActiveTokenCache, token, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the restricted issuance script
                    CScript restricted_issuance_script = GetScriptForDestination(destination);
                    token.ConstructTransaction(restricted_issuance_script);

                    // Construct the owner change script
                    CScript owner_token_transfer_script;
                    if (fHasOwnerChange)
                        owner_token_transfer_script = GetScriptForDestination(DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_token_transfer_script = GetScriptForDestination(destination);

                    CTokenTransfer transfer_owner(strTokenName.substr(1, strTokenName.size()) + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
                    transfer_owner.ConstructTransaction(owner_token_transfer_script);

                    // Construct the verifier string script
                    CScript verifier_string_script;
                    CNullTokenTxVerifierString verifierString(strippedVerifierString);
                    verifierString.ConstructTransaction(verifier_string_script);

                    // Create the CTxOut for each script we need to issue a restricted token
                    CTxOut resissue(0, restricted_issuance_script);
                    CTxOut owner_change(0, owner_token_transfer_script);
                    CTxOut verifier(0, verifier_string_script);

                    // Push the scriptPubKey into the vouts.
                    rawTx.vout.push_back(verifier);
                    rawTx.vout.push_back(owner_change);
                    rawTx.vout.push_back(resissue);

                } else if (tokenKey_ == "reissue_restricted") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                "Invalid parameter, the format must follow { \"reissue_restricted\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto reissueData = token_.getValues()[0].get_obj();

                    CReissueToken reissueObj;

                    /**-------Process the reissue data-------**/
                    const UniValue &token_name = find_value(reissueData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing reissue data for key: token_name");

                    const UniValue &token_quantity = find_value(reissueData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, missing reissue data for key: token_quantity");

                    const UniValue &reissuable = find_value(reissueData, "reissuable");
                    if (!reissuable.isNull()) {
                        if (!reissuable.isNum())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: reissuable");

                        int nReissuable = reissuable.get_int();
                        if (nReissuable > 1 || nReissuable < 0)
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, reissuable data must be a 0 or 1");

                        reissueObj.nReissuable = int8_t(nReissuable);
                    }

                    bool fHasVerifier = false;
                    const UniValue &verifier = find_value(reissueData, "verifier_string");
                    if (!verifier.isNull()) {
                        if (!verifier.isStr()) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, verifier_string must be a string");
                        }
                        fHasVerifier = true;
                    }

                    const UniValue &ipfs_hash = find_value(reissueData, "ipfs_hash");
                    if (!ipfs_hash.isNull()) {
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, missing reissue metadata for key: ipfs_hash");
                        reissueObj.strIPFSHash = DecodeTokenData(ipfs_hash.get_str());
                    }

                    bool fHasOwnerChange = false;
                    const UniValue &owner_change_address = find_value(reissueData, "owner_change_address");
                    if (!owner_change_address.isNull()) {
                        if (!owner_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, owner_change_address must be a string");
                        fHasOwnerChange = true;
                    }

                    if (fHasOwnerChange && !IsValidDestinationString(owner_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, owner_change_address is not a valid Akilacoin address");

                    std::string strTokenName = token_name.get_str();

                    if (!IsTokenNameAnRestricted(strTokenName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           "Invalid parameter, token_name must be a restricted token name. e.g $TOKEN_NAME");

                    std::string strippedVerifierString;
                    if (fHasVerifier) {
                        // Strip the white spaces from the verifier string
                        strippedVerifierString = GetStrippedVerifierString(verifier.get_str());

                        // Check the restricted token destination address, and make sure it validates with the verifier string
                        std::string strError = "";
                        if (!ContextualCheckVerifierString(currentActiveTokenCache, strippedVerifierString,
                                                           EncodeDestination(destination), strError))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                    "Invalid parmeter, verifier string is not. Please check the syntax. Error Msg - " +
                                    strError));
                    }

                    // Add the received data into the reissue object
                    reissueObj.strName = token_name.get_str();
                    reissueObj.nAmount = AmountFromValue(token_quantity);

                    // Validate the the object is valid
                    std::string strError;
                    if (!ContextualCheckReissueToken(currentActiveTokenCache, reissueObj, strError))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

                    // Create the scripts for the change of the ownership token
                    CScript owner_token_transfer_script;
                    if (fHasOwnerChange)
                        owner_token_transfer_script = GetScriptForDestination(
                                DecodeDestination(owner_change_address.get_str()));
                    else
                        owner_token_transfer_script = GetScriptForDestination(destination);

                    CTokenTransfer transfer_owner(RestrictedNameToOwnerName(token_name.get_str()), OWNER_TOKEN_AMOUNT, 0);
                    transfer_owner.ConstructTransaction(owner_token_transfer_script);

                    // Create the scripts for the reissued tokens
                    CScript scriptReissueToken = GetScriptForDestination(destination);
                    reissueObj.ConstructTransaction(scriptReissueToken);

                    // Construct the verifier string script
                    CScript verifier_string_script;
                    if (fHasVerifier) {
                        CNullTokenTxVerifierString verifierString(strippedVerifierString);
                        verifierString.ConstructTransaction(verifier_string_script);
                    }

                    // Create the CTxOut for the verifier script
                    CTxOut out_verifier(0, verifier_string_script);
                    rawTx.vout.push_back(out_verifier);

                    // Create the CTxOut for the owner token
                    CTxOut out_owner(0, owner_token_transfer_script);
                    rawTx.vout.push_back(out_owner);

                    // Create the CTxOut for the reissue token
                    CTxOut out_reissuance(0, scriptReissueToken);
                    rawTx.vout.push_back(out_reissuance);

                } else if (tokenKey_ == "issue_qualifier") {
                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"issue_qualifier\": {\"key\": value}, ...}"));

                    // Get the token data object from the json
                    auto tokenData = token_.getValues()[0].get_obj();

                    /**-------Process the tokens data-------**/
                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_name");

                    const UniValue& token_quantity = find_value(tokenData, "token_quantity");
                    if (!token_quantity.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token data for key: token_quantity");

                    const UniValue& has_ipfs = find_value(tokenData, "has_ipfs");
                    if (!has_ipfs.isNum())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");

                    bool fHasIpfs = false;
                    UniValue ipfs_hash = "";
                    if (has_ipfs.get_int() == 1) {
                        fHasIpfs = true;
                        ipfs_hash = find_value(tokenData, "ipfs_hash");
                        if (!ipfs_hash.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing token metadata for key: has_ipfs");
                    }

                    std::string strTokenName = token_name.get_str();
                    if (!IsTokenNameAQualifier(strTokenName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, token_name must be a qualifier or subqualifier name. e.g #MY_QUALIFIER or #MY_ROOT/#MY_SUB");
                    bool isSubQualifier = IsTokenNameASubQualifier(strTokenName);

                    bool fHasRootChange = false;
                    const UniValue& root_change_address = find_value(tokenData, "root_change_address");
                    if (!root_change_address.isNull()) {
                        if (!isSubQualifier)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, root_change_address only allowed when issuing a subqualifier.");
                        if (!root_change_address.isStr())
                            throw JSONRPCError(RPC_INVALID_PARAMETER,
                                               "Invalid parameter, root_change_address must be a string");
                        fHasRootChange = true;
                    }

                    if (fHasRootChange && !IsValidDestinationString(root_change_address.get_str()))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, root_change_address is not a valid Akilacoin address");

                    CAmount nAmount = AmountFromValue(token_quantity);
                    if (nAmount < QUALIFIER_TOKEN_MIN_AMOUNT || nAmount > QUALIFIER_TOKEN_MAX_AMOUNT)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, qualifiers are only allowed to be issued in quantities between 1 and 10.");

                    CAmount changeQty = COIN;
                    const UniValue& change_qty = find_value(tokenData, "change_quantity");
                    if (!change_qty.isNull()) {
                        if (!change_qty.isNum() || AmountFromValue(change_qty) < COIN)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, change_amount must be a positive number");
                        changeQty = AmountFromValue(change_qty);
                    }

                    int units = 0;
                    bool reissuable = false;

                    bool hasRoyalties = false;
                    std::string royaltiesAddress = "";
                    CAmount royaltiesAmount = 0;

                    // Create a new qualifier token
                    CNewToken token(
                        strTokenName, nAmount, units,
                        reissuable ? 1 : 0, fHasIpfs ? 1 : 0, DecodeTokenData(ipfs_hash.get_str()),
                        hasRoyalties ? 1 : 0, royaltiesAddress, royaltiesAmount
                    );

                    // Verify the new token data
                    std::string strError = "";
                    if (!ContextualCheckNewToken(currentActiveTokenCache, token, strError)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
                    }

                    // Construct the issuance script
                    CScript issuance_script = GetScriptForDestination(destination);
                    token.ConstructTransaction(issuance_script);

                    // Construct the root change script if issuing subqualifier
                    CScript root_token_transfer_script;
                    if (isSubQualifier) {
                        if (fHasRootChange)
                            root_token_transfer_script = GetScriptForDestination(
                                    DecodeDestination(root_change_address.get_str()));
                        else
                            root_token_transfer_script = GetScriptForDestination(destination);

                        CTokenTransfer transfer_root(GetParentName(strTokenName), changeQty, 0);
                        transfer_root.ConstructTransaction(root_token_transfer_script);
                    }

                    // Create the CTxOut for each script we need to issue
                    CTxOut issue(0, issuance_script);
                    CTxOut root_change;
                    if (isSubQualifier)
                        root_change = CTxOut(0, root_token_transfer_script);

                    // Push the scriptPubKey into the vouts.
                    if (isSubQualifier)
                        rawTx.vout.push_back(root_change);
                    rawTx.vout.push_back(issue);

                } else if (tokenKey_ == "tag_addresses" || tokenKey_ == "untag_addresses") {
                    int8_t tag_op = tokenKey_ == "tag_addresses" ? 1 : 0;

                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[tag|untag]_addresses\": {\"key\": value}, ...}"));
                    auto tokenData = token_.getValues()[0].get_obj();

                    const UniValue& qualifier = find_value(tokenData, "qualifier");
                    if (!qualifier.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: qualifier");
                    std::string strQualifier = qualifier.get_str();
                    if (!IsTokenNameAQualifier(strQualifier))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid qualifier name must be provided, e.g. #MY_QUALIFIER");

                    const UniValue& addresses = find_value(tokenData, "addresses");
                    if (!addresses.isArray() || addresses.size() < 1 || addresses.size() > 10)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, value for key address must be an array of size 1 to 10");
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        if (!IsValidDestinationString(addresses[i].get_str()))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, supplied address is not a valid Akilacoin address");
                    }

                    CAmount changeQty = COIN;
                    const UniValue& change_qty = find_value(tokenData, "change_quantity");
                    if (!change_qty.isNull()) {
                        if (!change_qty.isNum() || AmountFromValue(change_qty) < COIN)
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, change_amount must be a positive number");
                        changeQty = AmountFromValue(change_qty);
                    }

                    // change
                    CScript change_script = GetScriptForDestination(destination);
                    CTokenTransfer transfer_change(strQualifier, changeQty, 0);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // tagging
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        CScript tag_string_script = GetScriptForNullTokenDataDestination(DecodeDestination(addresses[i].get_str()));
                        CNullTokenTxData tagString(strQualifier, tag_op);
                        tagString.ConstructTransaction(tag_string_script);
                        CTxOut out_tag(0, tag_string_script);
                        rawTx.vout.push_back(out_tag);
                    }
                } else if (tokenKey_ == "freeze_addresses" || tokenKey_ == "unfreeze_addresses") {
                    int8_t freeze_op = tokenKey_ == "freeze_addresses" ? 1 : 0;

                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[freeze|unfreeze]_addresses\": {\"key\": value}, ...}"));
                    auto tokenData = token_.getValues()[0].get_obj();

                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: token_name");
                    std::string strTokenName = token_name.get_str();
                    if (!IsTokenNameAnRestricted(strTokenName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid restricted token name must be provided, e.g. $MY_TOKEN");

                    const UniValue& addresses = find_value(tokenData, "addresses");
                    if (!addresses.isArray() || addresses.size() < 1 || addresses.size() > 10)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, value for key address must be an array of size 1 to 10");
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        if (!IsValidDestinationString(addresses[i].get_str()))
                            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, supplied address is not a valid Akilacoin address");
                    }

                    // owner change
                    CScript change_script = GetScriptForDestination(destination);
                    CTokenTransfer transfer_change(RestrictedNameToOwnerName(strTokenName), OWNER_TOKEN_AMOUNT, 0);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // freezing
                    for (int i = 0; i < (int)addresses.size(); i++) {
                        CScript freeze_string_script = GetScriptForNullTokenDataDestination(DecodeDestination(addresses[i].get_str()));
                        CNullTokenTxData freezeString(strTokenName, freeze_op);
                        freezeString.ConstructTransaction(freeze_string_script);
                        CTxOut out_freeze(0, freeze_string_script);
                        rawTx.vout.push_back(out_freeze);
                    }
                } else if (tokenKey_ == "freeze_token" || tokenKey_ == "unfreeze_token") {
                    int8_t freeze_op = tokenKey_ == "freeze_token" ? 1 : 0;

                    if (token_[0].type() != UniValue::VOBJ)
                        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, the format must follow { \"[freeze|unfreeze]_token\": {\"key\": value}, ...}"));
                    auto tokenData = token_.getValues()[0].get_obj();

                    const UniValue& token_name = find_value(tokenData, "token_name");
                    if (!token_name.isStr())
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing data for key: token_name");
                    std::string strTokenName = token_name.get_str();
                    if (!IsTokenNameAnRestricted(strTokenName))
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, a valid restricted token name must be provided, e.g. $MY_TOKEN");

                    // owner change
                    CScript change_script = GetScriptForDestination(destination);
                    CTokenTransfer transfer_change(RestrictedNameToOwnerName(strTokenName), OWNER_TOKEN_AMOUNT, 0);
                    transfer_change.ConstructTransaction(change_script);
                    CTxOut out_change(0, change_script);
                    rawTx.vout.push_back(out_change);

                    // freezing
                    CScript freeze_string_script;
                    CNullTokenTxData freezeString(strTokenName, freeze_op);
                    freezeString.ConstructGlobalRestrictionTransaction(freeze_string_script);
                    CTxOut out_freeze(0, freeze_string_script);
                    rawTx.vout.push_back(out_freeze);

                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, unknown output type: " + tokenKey_));
                }
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, Output must be of the type object"));
            }
            /** AKILA COIN STOP **/
        }
    }

//    if (!request.params[3].isNull() && rbfOptIn != SignalsOptInRBF(rawTx)) {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
//    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"token\" : {               (json object) optional\n"
            "           \"name\" : \"name\",      (string) the token name\n"
            "           \"amount\" : n,           (numeric) the amount of token that was sent\n"
            "           \"message\" : \"message\", (string optional) the message if one was sent\n"
            "           \"expire_time\" : n,      (numeric optional) the message epoch expiration time if one was set\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) akila address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR});

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
}

UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"token\" : {               (json object) optional\n"
            "     \"name\" : \"name\",      (string) the token name\n"
            "     \"amount\" : n,           (numeric) the amount of token that was sent\n"
            "     \"message\" : \"message\", (string optional) the message if one was sent\n"
            "     \"expire_time\" : n,      (numeric optional ) the message epoch expiration time if one was set\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) akila address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\":\"address\",       (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH).\n"
            "  \"(The following only appears if the script is an token script)\n"
            "  \"token_name\":\"name\",      (string) Name of the token.\n"
            "  \"amount\":\"x.xx\",          (numeric) The amount of tokens interacted with.\n"
            "  \"units\": n,                (numeric) The units of the token. (Only appears in the type (new_token))\n"
            "  \"reissuable\": true|false, (boolean) If this token is reissuable. (Only appears in type (new_token|reissue_token))\n"
            "  \"hasIPFS\": true|false,    (boolean) If this token has an IPFS hash. (Only appears in type (new_token if hasIPFS is true))\n"
            "  \"ipfs_hash\": \"hash\",      (string) The ipfs hash for the new token. (Only appears in type (new_token))\n"
            "  \"new_ipfs_hash\":\"hash\",    (string) If new ipfs hash (Only appears in type. (reissue_token))\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if (type.isStr() && type.get_str() != "scripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.push_back(Pair("p2sh", EncodeDestination(CScriptID(script))));
    }

    /** TOKENS START */
    if (type.isStr() && type.get_str() == TOKEN_TRANSFER_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CTokenTransfer transfer;
        std::string address;

        if (!TransferTokenFromScript(script, transfer, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the transfer token script");

        r.push_back(Pair("token_name", transfer.strName));
        r.push_back(Pair("amount", ValueFromAmount(transfer.nAmount)));
        if (!transfer.message.empty())
            r.push_back(Pair("message", EncodeTokenData(transfer.message)));
        if (transfer.nExpireTime)
            r.push_back(Pair("expire_time", transfer.nExpireTime));

    } else if (type.isStr() && type.get_str() == TOKEN_REISSUE_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CReissueToken reissue;
        std::string address;

        if (!ReissueTokenFromScript(script, reissue, address))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the reissue token script");

        r.push_back(Pair("token_name", reissue.strName));
        r.push_back(Pair("amount", ValueFromAmount(reissue.nAmount)));

        bool reissuable = reissue.nReissuable ? true : false;
        r.push_back(Pair("reissuable", reissuable));

        if (reissue.strIPFSHash != "")
            r.push_back(Pair("new_ipfs_hash", EncodeTokenData(reissue.strIPFSHash)));

    } else if (type.isStr() && type.get_str() == TOKEN_NEW_STRING) {
        if (!AreTokensDeployed())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Tokens are not active");

        CNewToken token;
        std::string ownerToken;
        std::string address;

        if(TokenFromScript(script, token, address)) {
            r.push_back(Pair("token_name", token.strName));
            r.push_back(Pair("amount", ValueFromAmount(token.nAmount)));
            r.push_back(Pair("units", token.units));

            bool reissuable = token.nReissuable ? true : false;
            r.push_back(Pair("reissuable", reissuable));

            bool hasIPFS = token.nHasIPFS ? true : false;
            r.push_back(Pair("hasIPFS", hasIPFS));

            if (hasIPFS)
                r.push_back(Pair("ipfs_hash", EncodeTokenData(token.strIPFSHash)));
        }
        else if (OwnerTokenFromScript(script, ownerToken, address))
        {
            r.push_back(Pair("token_name", ownerToken));
            r.push_back(Pair("amount", ValueFromAmount(OWNER_TOKEN_AMOUNT)));
            r.push_back(Pair("units", OWNER_UNITS));
        }
        else
        {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Failed to deserialize the new token script");
        }
    } else {

    }
    /** TOKENS END */

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.hash.ToString()));
    entry.push_back(Pair("vout", (uint64_t)txin.prevout.n));
    UniValue witness(UniValue::VARR);
    for (unsigned int i = 0; i < txin.scriptWitness.stack.size(); i++) {
        witness.push_back(HexStr(txin.scriptWitness.stack[i].begin(), txin.scriptWitness.stack[i].end()));
    }
    entry.push_back(Pair("witness", witness));
    entry.push_back(Pair("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", (uint64_t)txin.nSequence));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

UniValue combinerawtransaction(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinerawtransaction [\"hexstring\",...]\n"
            "\nCombine multiple partially signed transactions into one transaction.\n"
            "The combined transaction may be another partially signed transaction or a \n"
            "fully signed transaction."

            "\nArguments:\n"
            "1. \"txs\"         (string) A json array of hex strings of partially signed transactions\n"
            "    [\n"
            "      \"hexstring\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "\"hex\"            (string) The hex-encoded raw transaction with signature(s)\n"

            "\nExamples:\n"
            + HelpExampleCli("combinerawtransaction", "[\"myhex1\", \"myhex2\", \"myhex3\"]")
        );


    UniValue txs = request.params[0].get_array();
    std::vector<CMutableTransaction> txVariants(txs.size());

    for (unsigned int idx = 0; idx < txs.size(); idx++) {
        if (!DecodeHexTx(txVariants[idx], txs[idx].get_str(), true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transactions");
    }

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mergedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Input not found or already spent");
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;

        // ... and merge in other signatures:
        for (const CMutableTransaction& txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i));
            }
        }

        UpdateTransaction(mergedTx, i, sigdata);
    }

    return EncodeHexTx(mergedTx);
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
#ifdef ENABLE_WALLET
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
#endif

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privkeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

    ObserveSafeMode();
#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwallet ? &pwallet->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (!request.params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = request.params[2].get_array();
        for (unsigned int idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CAkilaSecret vchSecret;
            bool fGood = vchSecret.SetString(k.get_str());
            if (!fGood)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            CKey key = vchSecret.GetKey();
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwallet) {
        EnsureWalletIsUnlocked(pwallet);
    }
#endif

    // Add previous txouts given in the RPC call:
    if (!request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash() || scriptPubKey.IsP2SHTokenScript())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"txid", UniValueType(UniValue::VSTR)},
                        {"vout", UniValueType(UniValue::VNUM)},
                        {"scriptPubKey", UniValueType(UniValue::VSTR)},
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwallet) ? tempKeystore : *pwallet);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (!request.params[3].isNull()) {
        static std::map<std::string, int> mapSigHashValues = {
            {std::string("ALL"), int(SIGHASH_ALL)},
            {std::string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY)},
            {std::string("NONE"), int(SIGHASH_NONE)},
            {std::string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY)},
            {std::string("SINGLE"), int(SIGHASH_SINGLE)},
            {std::string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY)},
        };
        std::string strHashType = request.params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mtx, i, amount, nHashType), prevPubKey, sigdata);
        sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(mtx, i));

        UpdateTransaction(mtx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(mtx)));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    ObserveSafeMode();
    LOCK(cs_main);
    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& hashTx = tx->GetHash();

    CAmount nMaxRawTxFee = maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool())
        nMaxRawTxFee = 0;

    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < tx->vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(mempool, state, std::move(tx), &fMissingInputs,
                                nullptr /* plTxnReplaced */, false /* bypass_limits */, nMaxRawTxFee)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });
    return hashTx.GetHex();
}

UniValue testmempoolaccept(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
                // clang-format off
                "testmempoolaccept [\"rawtxs\"] ( allowhighfees )\n"
                "\nReturns if raw transaction (serialized, hex-encoded) would be accepted by mempool.\n"
                "\nThis checks if the transaction violates the consensus or policy rules.\n"
                "\nSee sendrawtransaction call.\n"
                "\nArguments:\n"
                "1. [\"rawtxs\"]       (array, required) An array of hex strings of raw transactions.\n"
                "                                        Length must be one for now.\n"
                "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
                "\nResult:\n"
                "[                   (array) The result of the mempool acceptance test for each raw transaction in the input array.\n"
                "                            Length is exactly one for now.\n"
                " {\n"
                "  \"txid\"           (string) The transaction hash in hex\n"
                "  \"allowed\"        (boolean) If the mempool allows this tx to be inserted\n"
                "  \"reject-reason\"  (string) Rejection string (only present when 'allowed' is false)\n"
                " }\n"
                "]\n"
                "\nExamples:\n"
                "\nCreate a transaction\n"
                + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
                "Sign the transaction, and get back the hex\n"
                + HelpExampleCli("signrawtransaction", "\"myhex\"") +
                "\nTest acceptance of the transaction (signed hex)\n"
                + HelpExampleCli("testmempoolaccept", "\"signedhex\"") +
                "\nAs a json rpc call\n"
                + HelpExampleRpc("testmempoolaccept", "[\"signedhex\"]")
                // clang-format on
        );
    }

    ObserveSafeMode();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});
    if (request.params[0].get_array().size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Array must contain exactly one raw transaction for now");
    }

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_array()[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& tx_hash = tx->GetHash();

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    UniValue result(UniValue::VARR);
    UniValue result_0(UniValue::VOBJ);
    result_0.pushKV("txid", tx_hash.GetHex());

    CValidationState state;
    bool missing_inputs;
    bool test_accept_res;
    {
        LOCK(cs_main);
        test_accept_res = AcceptToMemoryPool(mempool, state, std::move(tx), &missing_inputs,
                                             nullptr /* plTxnReplaced */, false /* bypass_limits */, max_raw_tx_fee, /* test_accpet */ true);
    }
    result_0.pushKV("allowed", test_accept_res);
    if (!test_accept_res) {
        if (state.IsInvalid()) {
            result_0.pushKV("reject-reason", strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else if (missing_inputs) {
            result_0.pushKV("reject-reason", "missing-inputs");
        } else {
            result_0.pushKV("reject-reason", state.GetRejectReason());
        }
    }

    result.push_back(std::move(result_0));
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      {"txid","verbose"} },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   {"inputs","outputs","locktime"} },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   {"hexstring"} },
    { "rawtransactions",    "decodescript",           &decodescript,           {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     {"hexstring","allowhighfees"} },
    { "rawtransactions",    "combinerawtransaction",  &combinerawtransaction,  {"txs"} },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */
    { "rawtransactions",    "testmempoolaccept",      &testmempoolaccept,      {"rawtxs","allowhighfees"} },
    { "blockchain",         "gettxoutproof",          &gettxoutproof,          {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       {"proof"} },
};

void RegisterRawTransactionRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
