// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "core_io.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "streams.h"
#include <univalue.h>
#include <iomanip>
#include <wallet/wallet.h>
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "tokens/tokens.h"

std::string ValueFromAmountString(const CAmount& amount, const int8_t units)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    remainder = remainder / pow(10, 8 - units);

    if (units == 0 && remainder == 0) {
        return strprintf("%s%d", sign ? "-" : "", quotient);
    }
    else {
        return strprintf("%s%d.%0" + std::to_string(units) + "d", sign ? "-" : "", quotient, remainder);
    }
}

UniValue ValueFromAmount(const CAmount& amount, const int8_t units)
{
    return UniValue(UniValue::VNUM, ValueFromAmountString(amount, units));
}

UniValue ValueFromAmount(const CAmount& amount)
{
    return ValueFromAmount(amount, 8);
}

std::string FormatScript(const CScript& script)
{
    std::string ret;
    CScript::const_iterator it = script.begin();
    opcodetype op;
    while (it != script.end()) {
        CScript::const_iterator it2 = it;
        std::vector<unsigned char> vch;
        if (script.GetOp2(it, op, &vch)) {
            if (op == OP_0) {
                ret += "0 ";
                continue;
            } else if ((op >= OP_1 && op <= OP_16) || op == OP_1NEGATE) {
                ret += strprintf("%i ", op - OP_1NEGATE - 1);
                continue;
            } else if (op >= OP_NOP && op <= OP_NOP10) {
                std::string str(GetOpName(op));
                if (str.substr(0, 3) == std::string("OP_")) {
                    ret += str.substr(3, std::string::npos) + " ";
                    continue;
                }
            }
            if (vch.size() > 0) {
                ret += strprintf("0x%x 0x%x ", HexStr(it2, it - vch.size()), HexStr(it - vch.size(), it));
            } else {
                ret += strprintf("0x%x ", HexStr(it2, it));
            }
            continue;
        }
        ret += strprintf("0x%x ", HexStr(it2, script.end()));
        break;
    }
    return ret.substr(0, ret.size() - 1);
}

const std::map<unsigned char, std::string> mapSigHashTypes = {
    {static_cast<unsigned char>(SIGHASH_ALL), std::string("ALL")},
    {static_cast<unsigned char>(SIGHASH_ALL|SIGHASH_ANYONECANPAY), std::string("ALL|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_NONE), std::string("NONE")},
    {static_cast<unsigned char>(SIGHASH_NONE|SIGHASH_ANYONECANPAY), std::string("NONE|ANYONECANPAY")},
    {static_cast<unsigned char>(SIGHASH_SINGLE), std::string("SINGLE")},
    {static_cast<unsigned char>(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY), std::string("SINGLE|ANYONECANPAY")},
};

/**
 * Create the assembly string representation of a CScript object.
 * @param[in] script    CScript object to convert into the asm string representation.
 * @param[in] fAttemptSighashDecode    Whether to attempt to decode sighash types on data within the script that matches the format
 *                                     of a signature. Only pass true for scripts you believe could contain signatures. For example,
 *                                     pass false, or omit the this argument (defaults to false), for scriptPubKeys.
 */
std::string ScriptToAsmStr(const CScript& script, const bool fAttemptSighashDecode)
{
    std::string str;
    opcodetype opcode;
    std::vector<unsigned char> vch;
    CScript::const_iterator pc = script.begin();
    while (pc < script.end()) {
        if (!str.empty()) {
            str += " ";
        }

        if (!script.GetOp(pc, opcode, vch)) {
            str += "[error]";
            return str;
        }

        if (opcode == OP_YONA_TOKEN) {
            // Once we hit an OP_YONA_TOKEN, we know that all the next data should be considered as hex
            str += GetOpName(opcode);
            str += " ";
            str += HexStr(vch);
        } else if (0 <= opcode && opcode <= OP_PUSHDATA4) {
            if (vch.size() <= static_cast<std::vector<unsigned char>::size_type>(4)) {
                str += strprintf("%d", CScriptNum(vch, false).getint());
            } else {
                // the IsUnspendable check makes sure not to try to decode OP_RETURN data that may match the format of a signature
                if (fAttemptSighashDecode && !script.IsUnspendable()) {
                    std::string strSigHashDecode;
                    // goal: only attempt to decode a defined sighash type from data that looks like a signature within a scriptSig.
                    // this won't decode correctly formatted public keys in Pubkey or Multisig scripts due to
                    // the restrictions on the pubkey formats (see IsCompressedOrUncompressedPubKey) being incongruous with the
                    // checks in CheckSignatureEncoding.
                    if (CheckSignatureEncoding(vch, SCRIPT_VERIFY_STRICTENC, nullptr)) {
                        const unsigned char chSigHashType = vch.back();
                        if (mapSigHashTypes.count(chSigHashType)) {
                            strSigHashDecode = "[" + mapSigHashTypes.find(chSigHashType)->second + "]";
                            vch.pop_back(); // remove the sighash type byte. it will be replaced by the decode.
                        }
                    }
                    str += HexStr(vch) + strSigHashDecode;
                } else {
                    str += HexStr(vch);
                }
            }
        } else {
            str += GetOpName(opcode);
        }
    }
    return str;
}

std::string EncodeHexTx(const CTransaction& tx, const int serializeFlags)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | serializeFlags);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                        UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    txnouttype scriptType;
    std::vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, scriptType, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    /** TOKEN START */
    if (type == TX_NEW_TOKEN || type == TX_TRANSFER_TOKEN || type == TX_REISSUE_TOKEN) {
        UniValue tokenInfo(UniValue::VOBJ);

        std::string _tokenAddress;

        CTokenOutputEntry data;
        if (GetTokenData(scriptPubKey, data)) {
            tokenInfo.pushKV("name", data.tokenName);
            tokenInfo.pushKV("amount", ValueFromAmount(data.nAmount));
            if (!data.message.empty())
                tokenInfo.pushKV("message", EncodeTokenData(data.message));
            if(data.expireTime)
                tokenInfo.pushKV("expire_time", data.expireTime);

            switch (type) {
                case TX_NONSTANDARD:
                case TX_PUBKEY:
                case TX_PUBKEYHASH:
                case TX_SCRIPTHASH:
                case TX_MULTISIG:
                case TX_NULL_DATA:
                case TX_WITNESS_V0_SCRIPTHASH:
                case TX_WITNESS_V0_KEYHASH:
                case TX_RESTRICTED_TOKEN_DATA:
                default:
                    break;
                case TX_NEW_TOKEN:
                    if (IsTokenNameAnOwner(data.tokenName)) {
                        // pwnd n00b
                    } else {
                        CNewToken token;
                        if (TokenFromScript(scriptPubKey, token, _tokenAddress)) {
                            tokenInfo.pushKV("units", token.units);
                            tokenInfo.pushKV("reissuable", token.nReissuable > 0 ? true : false);
                            if (token.nHasIPFS > 0) {
                                tokenInfo.pushKV("ipfs_hash", EncodeTokenData(token.strIPFSHash));
                            }
                        }
                    }
                    break;
                case TX_TRANSFER_TOKEN:
                    break;
                case TX_REISSUE_TOKEN:
                    CReissueToken token;
                    if (ReissueTokenFromScript(scriptPubKey, token, _tokenAddress)) {
                        if (token.nUnits >= 0) {
                            tokenInfo.pushKV("units", token.nUnits);
                        }
                        tokenInfo.pushKV("reissuable", token.nReissuable > 0 ? true : false);
                        if (!token.strIPFSHash.empty()) {
                            tokenInfo.pushKV("ipfs_hash", EncodeTokenData(token.strIPFSHash));
                        }
                    }
                    break;
            }
        }

        out.pushKV("token", tokenInfo);
    }

    if (type == TX_RESTRICTED_TOKEN_DATA) {
        UniValue tokenInfo(UniValue::VOBJ);
        CNullTokenTxData data;
        CNullTokenTxVerifierString verifierData;
        std::string address;
        if (TokenNullDataFromScript(scriptPubKey, data, address)) {
            KnownTokenType type;
            IsTokenNameValid(data.token_name, type);
            if (type == KnownTokenType::QUALIFIER || type == KnownTokenType::SUB_QUALIFIER) {
                tokenInfo.pushKV("token_name", data.token_name);
                tokenInfo.pushKV("qualifier_type", data.flag ? "adding qualifier" : "removing qualifier");
                tokenInfo.pushKV("address", address);
            } else if (type == KnownTokenType::RESTRICTED) {
                tokenInfo.pushKV("token_name", data.token_name);
                tokenInfo.pushKV("restricted_type", data.flag ? "freezing address" : "unfreezing address");
                tokenInfo.pushKV("address", address);
            }
        } else if (GlobalTokenNullDataFromScript(scriptPubKey, data)) {
            tokenInfo.pushKV("restricted_name", data.token_name);
            tokenInfo.pushKV("restricted_type", data.flag ? "freezing" : "unfreezing");
            tokenInfo.pushKV("address", "all addresses");
        } else if (TokenNullVerifierDataFromScript(scriptPubKey, verifierData)) {
            tokenInfo.pushKV("verifier_string", verifierData.verifier_string);
        }

        out.pushKV("token_data", tokenInfo);
    }
     /** TOKEN END */

    UniValue a(UniValue::VARR);
    for (const CTxDestination& addr : addresses) {
        a.push_back(EncodeDestination(addr));
    }
    out.pushKV("addresses", a);
}

void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry, bool include_hex, int serialize_flags)
{
    entry.pushKV("txid", tx.GetHash().GetHex());
    entry.pushKV("hash", tx.GetWitnessHash().GetHex());
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    entry.pushKV("vsize", (GetTransactionWeight(tx) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR);
    entry.pushKV("locktime", (int64_t)tx.nLockTime);

    UniValue vin(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", o);
            if (!tx.vin[i].scriptWitness.IsNull()) {
                UniValue txinwitness(UniValue::VARR);
                for (const auto& item : tx.vin[i].scriptWitness.stack) {
                    txinwitness.push_back(HexStr(item.begin(), item.end()));
                }
                in.pushKV("txinwitness", txinwitness);
            }
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(in);
    }
    entry.pushKV("vin", vin);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        UniValue out(UniValue::VOBJ);

        out.pushKV("value", ValueFromAmount(txout.nValue));
        out.pushKV("n", (int64_t)i);

        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToUniv(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (!hashBlock.IsNull())
        entry.pushKV("blockhash", hashBlock.GetHex());

    if (include_hex) {
        entry.pushKV("hex", EncodeHexTx(tx, serialize_flags)); // the hex-encoded transaction. used the name "hex" to be consistent with the verbose output of "getrawtransaction".
    }
}
