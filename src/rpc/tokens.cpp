// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include <amount.h>
//#include <base58.h>
#include "tokens/tokens.h"
#include "tokens/tokendb.h"
#include <map>
#include "tinyformat.h"
//#include <rpc/server.h>
//#include <script/standard.h>
//#include <utilstrencodings.h>

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

void CheckRestrictedTokenTransferInputs(const CWalletTx& transaction, const std::string& token_name) {
    // Do a validity check before commiting the transaction
    if (IsTokenNameAnRestricted(token_name)) {
        if (pcoinsTip && ptokens) {
            for (auto input : transaction.tx->vin) {
                const COutPoint &prevout = input.prevout;
                const Coin &coin = pcoinsTip->AccessCoin(prevout);

                if (coin.IsToken()) {
                    CTokenOutputEntry data;
                    if (!GetTokenData(coin.out.scriptPubKey, data))
                        throw JSONRPCError(RPC_DATABASE_ERROR, std::string(
                                _("Unable to get coin to verify restricted token transfer from address")));


                    if (IsTokenNameAnRestricted(data.tokenName)) {
                        if (ptokens->CheckForAddressRestriction(data.tokenName, EncodeDestination(data.destination),
                                                                true)) {
                            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string(
                                    _("Restricted token transfer from address that has been frozen")));
                        }
                    }
                }
            }
        }
    }
}

std::string TokenActivationWarning()
{
    return AreTokensDeployed() ? "" : "\nTHIS COMMAND IS NOT YET ACTIVE!\nhttps://github.com/YonaProject/rips/blob/master/rip-0002.mediawiki\n";
}

std::string RestrictedActivationWarning()
{
    return AreRestrictedTokensDeployed() ? "" : "\nTHIS COMMAND IS NOT YET ACTIVE! Restricted tokens must be active\n\n";
}

std::string KnownTokenTypeToString(KnownTokenType& tokenType)
{
    switch (tokenType)
    {
        case KnownTokenType::ROOT:               return "ROOT";
        case KnownTokenType::SUB:                return "SUB";
        case KnownTokenType::UNIQUE:             return "UNIQUE";
        case KnownTokenType::OWNER:              return "OWNER";
        case KnownTokenType::MSGCHANNEL:         return "MSGCHANNEL";
        case KnownTokenType::VOTE:               return "VOTE";
        case KnownTokenType::REISSUE:            return "REISSUE";
        case KnownTokenType::USERNAME:           return "USERNAME";
        case KnownTokenType::QUALIFIER:          return "QUALIFIER";
        case KnownTokenType::SUB_QUALIFIER:      return "SUB_QUALIFIER";
        case KnownTokenType::RESTRICTED:         return "RESTRICTED";
        case KnownTokenType::INVALID:            return "INVALID";
        default:                            return "UNKNOWN";
    }
}

UniValue UnitValueFromAmount(const CAmount& amount, const std::string token_name)
{

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (!currentActiveTokenCache)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Token cache isn't available.");

    uint8_t units = OWNER_UNITS;
    if (!IsTokenNameAnOwner(token_name)) {
        CNewToken tokenData;
        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(token_name, tokenData))
            units = MAX_UNIT;
            //throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't load token from cache: " + token_name);
        else
            units = tokenData.units;
    }

    return ValueFromAmount(amount, units);
}

#ifdef ENABLE_WALLET
UniValue UpdateAddressTag(const JSONRPCRequest &request, const int8_t &flag)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tag_name = request.params[0].get_str();

    if (!IsTokenNameAQualifier(tag_name)) {
        std::string temp = QUALIFIER_CHAR + tag_name;

        auto index = temp.find("/");
        if (index != std::string::npos) {
            temp.insert(index+1, "#");
        }
        tag_name = temp;
    }

    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(tag_name, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tag_name + std::string("\nError: ") + tokenError);
    }

    if (tokenType != KnownTokenType::QUALIFIER && tokenType != KnownTokenType::SUB_QUALIFIER) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    std::string address = request.params[1].get_str();
    CTxDestination destination = DecodeDestination(address);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
    }

    // Get the optional change address
    std::string change_address = "";
    if (request.params.size() > 2) {
        change_address = request.params[2].get_str();
        if (!change_address.empty()) {
           CTxDestination change_dest = DecodeDestination(change_address);
           if (!IsValidDestination(change_dest)) {
               throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona change address: ") + change_address);
           }
        }
    }

    // Get the optional token data
    std::string token_data = "";
    if (request.params.size() > 3) {
        token_data = request.params[3].get_str();
        token_data = DecodeTokenData(token_data);

        if (token_data.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token data hash"));
    }

    std::string message;
    if (request.params.size() > 4) {
        message = request.params[4].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    CCoinControl ctrl;

    ctrl.destChange = DecodeDestination(change_address);

    // If the optional change address wasn't given create a new change address for this wallet
    if (change_address == "") {
        CKeyID keyID;
        std::string strFailReason;
        if (!pwallet->CreateNewChangeAddress(reservekey, keyID, strFailReason))
            throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);

        change_address = EncodeDestination(keyID);
    }

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    // Always transfer 1 of the qualifier tokens to the change address
    vTransfers.emplace_back(std::make_pair(CTokenTransfer(tag_name, 1 * COIN, 0, token_data), change_address));

    // Add the token data with the flag to remove or add the tag 1 = Add, 0 = Remove
    std::vector< std::pair<CNullTokenTxData, std::string> > vecTokenData;
    vecTokenData.push_back(std::make_pair(CNullTokenTxData(tag_name, flag), address));

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message, &vecTokenData))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue UpdateAddressRestriction(const JSONRPCRequest &request, const int8_t &flag)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string restricted_name = request.params[0].get_str();

    if (!IsTokenNameAnRestricted(restricted_name)) {
        std::string temp = RESTRICTED_CHAR + restricted_name;
        restricted_name = temp;
    }

    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(restricted_name, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + restricted_name + std::string("\nError: ") + tokenError);
    }

    if (tokenType != KnownTokenType::RESTRICTED) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    std::string address = request.params[1].get_str();
    CTxDestination destination = DecodeDestination(address);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
    }

    // Get the optional change address
    std::string change_address = "";
    if (request.params.size() > 2) {
        change_address = request.params[2].get_str();
        if (!change_address.empty()) {
           CTxDestination change_dest = DecodeDestination(change_address);
           if (!IsValidDestination(change_dest)) {
               throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona change address: ") + change_address);
           }
        }
    }

    // Get the optional token data
    std::string token_data = "";
    if (request.params.size() > 3) {
        token_data = request.params[3].get_str();
        token_data = DecodeTokenData(token_data);

        if (token_data.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token data hash"));
    }

    std::string message;
    if (request.params.size() > 4) {
        message = request.params[4].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    CCoinControl ctrl;

    // If the optional change address wasn't given create a new change address for this wallet
    if (change_address == "") {
        CKeyID keyID;
        std::string strFailReason;
        if (!pwallet->CreateNewChangeAddress(reservekey, keyID, strFailReason))
            throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);

        change_address = EncodeDestination(keyID);
    }

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    // Always transfer 1 of the restricted tokens to the change address
    // Use the ROOT owner token to make this change occur. if $TOKEN -> Use TOKEN!
    vTransfers.emplace_back(std::make_pair(CTokenTransfer(restricted_name.substr(1, restricted_name.size()) + OWNER_TAG, 1 * COIN, 0, token_data), change_address));

    // Add the token data with the flag to remove or add the tag 1 = Freeze, 0 = Unfreeze
    std::vector< std::pair<CNullTokenTxData, std::string> > vecTokenData;
    vecTokenData.push_back(std::make_pair(CNullTokenTxData(restricted_name.substr(0, restricted_name.size()), flag), address));

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message, &vecTokenData))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}


UniValue UpdateGlobalRestrictedToken(const JSONRPCRequest &request, const int8_t &flag)
{
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string restricted_name = request.params[0].get_str();

    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(restricted_name, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + restricted_name + std::string("\nError: ") + tokenError);
    }

    if (tokenType != KnownTokenType::RESTRICTED) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    if (flag == 1 && mempool.mapGlobalFreezingTokenTransactions.count(restricted_name)){
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, std::string("Freezing transaction already in mempool"));
    }

    if (flag == 0 && mempool.mapGlobalUnFreezingTokenTransactions.count(restricted_name)){
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, std::string("Unfreezing transaction already in mempool"));
    }

    // Get the optional change address
    std::string change_address = "";
    if (request.params.size() > 1) {
        change_address = request.params[1].get_str();
        if (!change_address.empty()) {
           CTxDestination change_dest = DecodeDestination(change_address);
           if (!IsValidDestination(change_dest)) {
               throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona change address: ") + change_address);
           }
        }
    }

    // Get the optional token data
    std::string token_data = "";
    if (request.params.size() > 2) {
        token_data = request.params[2].get_str();
        token_data = DecodeTokenData(token_data);

        if (token_data.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token data hash"));
    }

    std::string message;
    if (request.params.size() > 3) {
        message = request.params[3].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    CCoinControl ctrl;

    // If the optional change address wasn't given create a new change address for this wallet
    if (change_address == "") {
        CKeyID keyID;
        std::string strFailReason;
        if (!pwallet->CreateNewChangeAddress(reservekey, keyID, strFailReason))
            throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);

        change_address = EncodeDestination(keyID);
    }

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    // Always transfer 1 of the restricted tokens to the change address
    // Use the ROOT owner token to make this change occur. if $TOKEN -> Use TOKEN!
    vTransfers.emplace_back(std::make_pair(CTokenTransfer(restricted_name.substr(1, restricted_name.size()) + OWNER_TAG, 1 * COIN, 0, token_data), change_address));

    // Add the global token data, 1 = Freeze all transfers, 0 = Allow transfers
    std::vector<CNullTokenTxData> vecGlobalTokenData;
    vecGlobalTokenData.push_back(CNullTokenTxData(restricted_name.substr(0, restricted_name.size()), flag));

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message, nullptr, &vecGlobalTokenData))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue issue(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1 || request.params.size() > 8)
        throw std::runtime_error(
            "issue \"token_name\" qty \"( to_address )\" \"( change_address )\" ( units ) ( reissuable ) ( has_ipfs ) \"( ipfs_hash )\"\n"
            + TokenActivationWarning() +
            "\nIssue an token, subtoken or unique token.\n"
            "Token name must not conflict with any existing token.\n"
            "Unit as the number of decimals precision for the token (0 for whole units (\"1\"), 8 for max precision (\"1.00000000\")\n"
            "Reissuable is true/false for whether additional units can be issued by the original issuer.\n"
            "If issuing a unique token these values are required (and will be defaulted to): qty=1, units=0, reissuable=false.\n"

            "\nArguments:\n"
            "1. \"token_name\"            (string, required) a unique name\n"
            "2. \"qty\"                   (numeric, optional, default=1) the number of units to be issued\n"
            "3. \"to_address\"            (string), optional, default=\"\"), address token will be sent to, if it is empty, address will be generated for you\n"
            "4. \"change_address\"        (string), optional, default=\"\"), address the the yona change will be sent to, if it is empty, change address will be generated for you\n"
            "5. \"units\"                 (integer, optional, default=0, min=0, max=8), the number of decimals precision for the token (0 for whole units (\"1\"), 8 for max precision (\"1.00000000\")\n"
            "6. \"reissuable\"            (boolean, optional, default=true (false for unique tokens)), whether future reissuance is allowed\n"
            "7. \"has_ipfs\"              (boolean, optional, default=false), whether ipfs hash is going to be added to the token\n"
            "8. \"ipfs_hash\"             (string, optional but required if has_ipfs = 1), an ipfs hash or a txid hash once messaging is activated\n"

            "\nResult:\n"
            "\"txid\"                     (string) The transaction id\n"

            "\nExamples:\n"
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\"")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 4")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 2 true")
            + HelpExampleCli("issue", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" 8 false true QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E")
            + HelpExampleCli("issue", "\"TOKEN_NAME/SUB_TOKEN\" 1000 \"myaddress\" \"changeaddress\" 2 true")
            + HelpExampleCli("issue", "\"TOKEN_NAME#uniquetag\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    // Push the user to use the issue restrictd rpc call if they are trying to issue a restricted token
    if (tokenType == KnownTokenType::RESTRICTED) {
        throw (JSONRPCError(RPC_INVALID_PARAMETER, std::string("Use the rpc call issuerestricted to issue a restricted token")));
    }

    // Push the user to use the issue restrictd rpc call if they are trying to issue a restricted token
    if (tokenType == KnownTokenType::QUALIFIER || tokenType == KnownTokenType::SUB_QUALIFIER  ) {
        throw (JSONRPCError(RPC_INVALID_PARAMETER, std::string("Use the rpc call issuequalifiertoken to issue a qualifier token")));
    }

    // Check for unsupported token types
    if (tokenType == KnownTokenType::VOTE || tokenType == KnownTokenType::REISSUE || tokenType == KnownTokenType::OWNER || tokenType == KnownTokenType::INVALID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    CAmount nAmount = COIN;
    if (request.params.size() > 1)
        nAmount = AmountFromValue(request.params[1]);

    std::string address = "";
    if (request.params.size() > 2)
        address = request.params[2].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    std::string change_address = "";
    if (request.params.size() > 3) {
        change_address = request.params[3].get_str();
        if (!change_address.empty()) {
            CTxDestination destination = DecodeDestination(change_address);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   std::string("Invalid Change Address: Invalid Yona address: ") + change_address);
            }
        }
    }

    int units = 0;
    if (request.params.size() > 4)
        units = request.params[4].get_int();

    bool reissuable = tokenType != KnownTokenType::UNIQUE && tokenType != KnownTokenType::USERNAME && tokenType != KnownTokenType::MSGCHANNEL && tokenType != KnownTokenType::QUALIFIER && tokenType != KnownTokenType::SUB_QUALIFIER;
    if (request.params.size() > 5)
        reissuable = request.params[5].get_bool();

    bool has_ipfs = false;
    if (request.params.size() > 6)
        has_ipfs = request.params[6].get_bool();

    // Check the ipfs
    std::string ipfs_hash = "";
    bool fMessageCheck = false;
    if (request.params.size() > 7 && has_ipfs) {
        fMessageCheck = true;
        ipfs_hash = request.params[7].get_str();
    }

    // Reissues don't have an expire time
    int64_t expireTime = 0;

    // Check the message data
    if (fMessageCheck)
        CheckIPFSTxidMessage(ipfs_hash, expireTime);

    // check for required unique token params
    if ((tokenType == KnownTokenType::UNIQUE || tokenType == KnownTokenType::USERNAME || tokenType == KnownTokenType::MSGCHANNEL) && (nAmount != COIN || units != 0 || reissuable)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameters for issuing a unique token."));
    }

    // check for required unique token params
    if ((tokenType == KnownTokenType::QUALIFIER || tokenType == KnownTokenType::SUB_QUALIFIER) && (nAmount < QUALIFIER_TOKEN_MIN_AMOUNT || nAmount > QUALIFIER_TOKEN_MAX_AMOUNT  || units != 0 || reissuable)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameters for issuing a qualifier token."));
    }

    CNewToken token(tokenName, nAmount, units, reissuable ? 1 : 0, has_ipfs ? 1 : 0, DecodeTokenData(ipfs_hash));

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(change_address);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, token, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue issueunique(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 2 || request.params.size() > 5)
        throw std::runtime_error(
                "issueunique \"root_name\" [token_tags] ( [ipfs_hashes] ) \"( to_address )\" \"( change_address )\"\n"
                + TokenActivationWarning() +
                "\nIssue unique token(s).\n"
                "root_name must be an token you own.\n"
                "An token will be created for each element of token_tags.\n"
                "If provided ipfs_hashes must be the same length as token_tags.\n"
                "Five (5) YONA will be burned for each token created.\n"

                "\nArguments:\n"
                "1. \"root_name\"             (string, required) name of the token the unique token(s) are being issued under\n"
                "2. \"token_tags\"            (array, required) the unique tag for each token which is to be issued\n"
                "3. \"ipfs_hashes\"           (array, optional) ipfs hashes or txid hashes corresponding to each supplied tag (should be same size as \"token_tags\")\n"
                "4. \"to_address\"            (string, optional, default=\"\"), address tokens will be sent to, if it is empty, address will be generated for you\n"
                "5. \"change_address\"        (string, optional, default=\"\"), address the the yona change will be sent to, if it is empty, change address will be generated for you\n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("issueunique", "\"MY_TOKEN\" \'[\"primo\",\"secundo\"]\'")
                + HelpExampleCli("issueunique", "\"MY_TOKEN\" \'[\"primo\",\"secundo\"]\' \'[\"first_hash\",\"second_hash\"]\'")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);


    const std::string rootName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(rootName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + rootName  + std::string("\nError: ") + tokenError);
    }
    if (tokenType != KnownTokenType::ROOT && tokenType != KnownTokenType::SUB) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Root token must be a regular top-level or sub-token."));
    }

    const UniValue& tokenTags = request.params[1];
    if (!tokenTags.isArray() || tokenTags.size() < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Token tags must be a non-empty array."));
    }

    const UniValue& ipfsHashes = request.params[2];
    if (!ipfsHashes.isNull()) {
        if (!ipfsHashes.isArray() || ipfsHashes.size() != tokenTags.size()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("If provided, IPFS hashes must be an array of the same size as the token tags array."));
        }
    }

    std::string address = "";
    if (request.params.size() > 3)
        address = request.params[3].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    std::string changeAddress = "";
    if (request.params.size() > 4)
        changeAddress = request.params[4].get_str();
    if (!changeAddress.empty()) {
        CTxDestination destination = DecodeDestination(changeAddress);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               std::string("Invalid Change Address: Invalid Yona address: ") + changeAddress);
        }
    }

    std::vector<CNewToken> tokens;
    for (int i = 0; i < (int)tokenTags.size(); i++) {
        std::string tag = tokenTags[i].get_str();

        if (!IsUniqueTagValid(tag)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unique token tag is invalid: " + tag));
        }

        std::string tokenName = GetUniqueTokenName(rootName, tag);
        CNewToken token;

        if (ipfsHashes.isNull())
        {
            token = CNewToken(tokenName, UNIQUE_TOKEN_AMOUNT, UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE, 0, "");
        }
        else
        {
            token = CNewToken(tokenName, UNIQUE_TOKEN_AMOUNT, UNIQUE_TOKEN_UNITS, UNIQUE_TOKENS_REISSUABLE, 1,
                              DecodeTokenData(ipfsHashes[i].get_str()));
        }

        tokens.push_back(token);
    }

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;

    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, tokens, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue registerusername(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1 || request.params.size() > 8)
        throw std::runtime_error(
            "registerusername \"username\" \"( to_address )\""

            "\nArguments:\n"
            "1. \"username\"              (string, required) a unique username\n"
            "2. \"to_address\"            (string), optional, default=\"\"), address token will be sent to, if it is empty, address will be generated for you\n"

            "\nResult:\n"
            "\"txid\"                     (string) The transaction id\n"

            "\nExamples:\n"
            + HelpExampleCli("registerusername", "\"@USERNAME\"")
            + HelpExampleCli("registerusername", "\"@USERNAME\" \"myaddress\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    // Check tokenType supported
    if (tokenType != KnownTokenType::USERNAME) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Usename is invalid"));
    }

    std::string address = "";
    if (request.params.size() > 1)
        address = request.params[1].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    CNewToken token(tokenName, COIN, 0, 0, 0, "");

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    std::string changeAddress = "";

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, token, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}
#endif

UniValue listtokenbalancesbyaddress(const JSONRPCRequest& request)
{
    if (!fTokenIndex) {
        return "_This rpc call is not functional unless -tokenindex is enabled. To enable, please run the wallet with -tokenindex, this will require a reindex to occur";
    }

    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1)
        throw std::runtime_error(
            "listtokenbalancesbyaddress \"address\" (onlytotal) (count) (start)\n"
            + TokenActivationWarning() +
            "\nReturns a list of all token balances for an address.\n"

            "\nArguments:\n"
            "1. \"address\"                  (string, required) a yona address\n"
            "2. \"onlytotal\"                (boolean, optional, default=false) when false result is just a list of tokens balances -- when true the result is just a single number representing the number of tokens\n"
            "3. \"count\"                    (integer, optional, default=50000, MAX=50000) truncates results to include only the first _count_ tokens found\n"
            "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

            "\nResult:\n"
            "{\n"
            "  (token_name) : (quantity),\n"
            "  ...\n"
            "}\n"


            "\nExamples:\n"
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\" false 2 0")
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\" true")
            + HelpExampleCli("listtokenbalancesbyaddress", "\"myaddress\"")
        );

    ObserveSafeMode();

    std::string address = request.params[0].get_str();
    CTxDestination destination = DecodeDestination(address);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
    }

    bool fOnlyTotal = false;
    if (request.params.size() > 1)
        fOnlyTotal = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    if (!ptokensdb)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "token db unavailable.");

    LOCK(cs_main);
    std::vector<std::pair<std::string, CAmount> > vecTokenAmounts;
    int nTotalEntries = 0;
    if (!ptokensdb->AddressDir(vecTokenAmounts, nTotalEntries, fOnlyTotal, address, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve address token directory.");

    // If only the number of addresses is wanted return it
    if (fOnlyTotal) {
        return nTotalEntries;
    }

    UniValue result(UniValue::VOBJ);
    for (auto& pair : vecTokenAmounts) {
        result.push_back(Pair(pair.first, UnitValueFromAmount(pair.second, pair.first)));
    }

    return result;
}

UniValue gettokendata(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "gettokendata \"token_name\"\n"
                + TokenActivationWarning() +
                "\nReturns tokens metadata if that token exists\n"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) the name of the token\n"

                "\nResult:\n"
                "{\n"
                "  name: (string),\n"
                "  amount: (number),\n"
                "  units: (number),\n"
                "  reissuable: (number),\n"
                "  has_ipfs: (number),\n"
                "  ipfs_hash: (hash), (only if has_ipfs = 1 and that data is a ipfs hash)\n"
                "  txid_hash: (hash), (only if has_ipfs = 1 and that data is a txid hash)\n"
                "  verifier_string: (string)\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("gettokendata", "\"TOKEN_NAME\"")
                + HelpExampleRpc("gettokendata", "\"TOKEN_NAME\"")
        );


    std::string token_name = request.params[0].get_str();

    LOCK(cs_main);
    UniValue result (UniValue::VOBJ);

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (currentActiveTokenCache) {
        CNewToken token;
        if (!currentActiveTokenCache->GetTokenMetaDataIfExists(token_name, token))
            return NullUniValue;

        result.push_back(Pair("name", token.strName));
        result.push_back(Pair("amount", UnitValueFromAmount(token.nAmount, token.strName)));
        result.push_back(Pair("units", token.units));
        result.push_back(Pair("reissuable", token.nReissuable));
        result.push_back(Pair("has_ipfs", token.nHasIPFS));

        if (token.nHasIPFS) {
            if (token.strIPFSHash.size() == 32) {
                result.push_back(Pair("txid", EncodeTokenData(token.strIPFSHash)));
            } else {
                result.push_back(Pair("ipfs_hash", EncodeTokenData(token.strIPFSHash)));
            }
        }

        CNullTokenTxVerifierString verifier;
        if (currentActiveTokenCache->GetTokenVerifierStringIfExists(token.strName, verifier)) {
            result.push_back(Pair("verifier_string", verifier.verifier_string));
        }

        return result;
    }

    return NullUniValue;
}

template <class Iter, class Incr>
void safe_advance(Iter& curr, const Iter& end, Incr n)
{
    size_t remaining(std::distance(curr, end));
    if (remaining < n)
    {
        n = remaining;
    }
    std::advance(curr, n);
};

#ifdef ENABLE_WALLET
UniValue listmytokens(const JSONRPCRequest &request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 5)
        throw std::runtime_error(
                "listmytokens \"( token )\" ( verbose ) ( count ) ( start ) (confs) \n"
                + TokenActivationWarning() +
                "\nReturns a list of all token that are owned by this wallet\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false results only contain balances -- when true results include outpoints\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"
                "5. \"confs\"                    (integet, optional, default=0) results are skipped if they don't have this number of confirmations\n"

                "\nResult (verbose=false):\n"
                "{\n"
                "  (token_name): balance,\n"
                "  ...\n"
                "}\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      \"balance\": balance,\n"
                "      \"outpoints\":\n"
                "        [\n"
                "          {\n"
                "            \"txid\": txid,\n"
                "            \"vout\": vout,\n"
                "            \"amount\": amount\n"
                "          }\n"
                "          {...}, {...}\n"
                "        ]\n"
                "    }\n"
                "}\n"
                "{...}, {...}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listmytokens", "")
                + HelpExampleCli("listmytokens", "TOKEN")
                + HelpExampleCli("listmytokens", "\"TOKEN*\" true 10 20")
                  + HelpExampleCli("listmytokens", "\"TOKEN*\" true 10 20 1")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    int confs = 0;
    if (request.params.size() > 4) {
        confs = request.params[4].get_int();
    }

    // retrieve balances
    std::map<std::string, CAmount> balances;
    std::map<std::string, std::vector<COutput> > outputs;
    if (filter == "*") {
        if (!GetAllMyTokenBalances(outputs, balances, confs))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else if (filter.back() == '*') {
        std::vector<std::string> tokenNames;
        filter.pop_back();
        if (!GetAllMyTokenBalances(outputs, balances, confs, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else {
        if (!IsTokenNameValid(filter))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token name.");
        if (!GetAllMyTokenBalances(outputs, balances, confs, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }

    // pagination setup
    auto bal = balances.begin();
    if (start >= 0)
        safe_advance(bal, balances.end(), (size_t)start);
    else
        safe_advance(bal, balances.end(), balances.size() + start);
    auto end = bal;
    safe_advance(end, balances.end(), count);

    // generate output
    UniValue result(UniValue::VOBJ);
    if (verbose) {
        for (; bal != end && bal != balances.end(); bal++) {
            UniValue token(UniValue::VOBJ);
            token.push_back(Pair("balance", UnitValueFromAmount(bal->second, bal->first)));

            UniValue outpoints(UniValue::VARR);
            for (auto const& out : outputs.at(bal->first)) {
                UniValue tempOut(UniValue::VOBJ);
                tempOut.push_back(Pair("txid", out.tx->GetHash().GetHex()));
                tempOut.push_back(Pair("vout", (int)out.i));

                //
                // get amount for this outpoint
                CAmount txAmount = 0;
                auto it = pwallet->mapWallet.find(out.tx->GetHash());
                if (it == pwallet->mapWallet.end()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
                }
                const CWalletTx* wtx = out.tx;
                CTxOut txOut = wtx->tx->vout[out.i];
                std::string strAddress;
                int nTimeLock = 0;
                if (CheckIssueDataTx(txOut)) {
                    CNewToken token;
                    if (!TokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckReissueDataTx(txOut)) {
                    CReissueToken token;
                    if (!ReissueTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckTransferOwnerTx(txOut)) {
                    CTokenTransfer token;
                    if (!TransferTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                    nTimeLock = token.nTimeLock;
                }
                else if (CheckOwnerDataTx(txOut)) {
                    std::string tokenName;
                    if (!OwnerTokenFromScript(txOut.scriptPubKey, tokenName, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = OWNER_TOKEN_AMOUNT;
                }
                tempOut.push_back(Pair("amount", UnitValueFromAmount(txAmount, bal->first)));
                tempOut.pushKV("satoshis", txAmount);
                if (nTimeLock > 0) {
                    tempOut.push_back(Pair("timelock", (int)nTimeLock));
                }

                outpoints.push_back(tempOut);
            }
            token.push_back(Pair("outpoints", outpoints));
            result.push_back(Pair(bal->first, token));
        }
    }
    else {
        for (; bal != end && bal != balances.end(); bal++) {
            result.push_back(Pair(bal->first, UnitValueFromAmount(bal->second, bal->first)));
        }
    }
    return result;
}

#endif

UniValue listmylockedtokens(const JSONRPCRequest &request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4)
        throw std::runtime_error(
                "listmylockedtokens \"( token )\" ( verbose ) ( count ) ( start )\n"
                + TokenActivationWarning() +
                "\nReturns a list of all locked token that are owned by this wallet\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false results only contain balances -- when true results include outpoints\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult (verbose=false):\n"
                "{\n"
                "  (token_name): balance,\n"
                "  ...\n"
                "}\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      \"balance\": balance,\n"
                "      \"outpoints\":\n"
                "        [\n"
                "          {\n"
                "            \"txid\": txid,\n"
                "            \"vout\": vout,\n"
                "            \"amount\": amount\n"
                "          }\n"
                "          {...}, {...}\n"
                "        ]\n"
                "    }\n"
                "}\n"
                "{...}, {...}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listmylockedtokens", "")
                + HelpExampleCli("listmylockedtokens", "TOKEN")
                + HelpExampleCli("listmylockedtokens", "\"TOKEN*\" true 10 20")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    // retrieve balances
    std::map<std::string, CAmount> balances;
    std::map<std::string, std::vector<COutput> > outputs;
    if (filter == "*") {
        if (!GetAllMyLockedTokenBalances(outputs, balances))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else if (filter.back() == '*') {
        std::vector<std::string> tokenNames;
        filter.pop_back();
        if (!GetAllMyLockedTokenBalances(outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }
    else {
        if (!IsTokenNameValid(filter))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid token name.");
        if (!GetAllMyLockedTokenBalances(outputs, balances, filter))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token balances. For all tokens");
    }

    // pagination setup
    auto bal = balances.begin();
    if (start >= 0) {
        safe_advance(bal, balances.end(), (size_t)start);
    } else {
        safe_advance(bal, balances.end(), balances.size() + start);
    }

    auto end = bal;
    safe_advance(end, balances.end(), count);

    // generate output
    UniValue result(UniValue::VOBJ);
    if (verbose) {
        for (; bal != end && bal != balances.end(); bal++) {
            UniValue token(UniValue::VOBJ);
            token.pushKV("balance", UnitValueFromAmount(bal->second, bal->first));

            UniValue outpoints(UniValue::VARR);
            for (auto const& out : outputs.at(bal->first)) {
                UniValue tempOut(UniValue::VOBJ);
                tempOut.pushKV("txid", out.tx->GetHash().GetHex());
                tempOut.pushKV("vout", (int)out.i);

                //
                // get amount for this outpoint
                CAmount txAmount = 0;
                auto it = pwallet->mapWallet.find(out.tx->GetHash());
                if (it == pwallet->mapWallet.end()) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
                }
                const CWalletTx* wtx = out.tx;
                CTxOut txOut = wtx->tx->vout[out.i];
                std::string strAddress;
                int nTimeLock = 0;
                if (CheckIssueDataTx(txOut)) {
                    CNewToken token;
                    if (!TokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckReissueDataTx(txOut)) {
                    CReissueToken token;
                    if (!ReissueTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                }
                else if (CheckTransferOwnerTx(txOut)) {
                    CTokenTransfer token;
                    if (!TransferTokenFromScript(txOut.scriptPubKey, token, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = token.nAmount;
                    nTimeLock = token.nTimeLock;
                }
                else if (CheckOwnerDataTx(txOut)) {
                    std::string tokenName;
                    if (!OwnerTokenFromScript(txOut.scriptPubKey, tokenName, strAddress))
                        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't get token from script.");
                    txAmount = OWNER_TOKEN_AMOUNT;
                }
                tempOut.pushKV("amount", UnitValueFromAmount(txAmount, bal->first));
                tempOut.pushKV("satoshis", txAmount);
                if (nTimeLock > 0) {
                    tempOut.pushKV("timelock", (int)nTimeLock);
                }

                outpoints.push_back(tempOut);
            }
            token.pushKV("outpoints", outpoints);
            result.pushKV(bal->first, token);
        }
    }
    else {
        for (; bal != end && bal != balances.end(); bal++) {
            result.pushKV(bal->first, UnitValueFromAmount(bal->second, bal->first));
        }
    }
    return result;
}

UniValue listaddressesbytoken(const JSONRPCRequest &request)
{
    if (!fTokenIndex) {
        return "_This rpc call is not functional unless -tokenindex is enabled. To enable, please run the wallet with -tokenindex, this will require a reindex to occur";
    }

    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4 || request.params.size() < 1)
        throw std::runtime_error(
                "listaddressesbytoken \"token_name\" (onlytotal) (count) (start)\n"
                + TokenActivationWarning() +
                "\nReturns a list of all address that own the given token (with balances)"
                "\nOr returns the total size of how many address own the given token"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token\n"
                "2. \"onlytotal\"                (boolean, optional, default=false) when false result is just a list of addresses with balances -- when true the result is just a single number representing the number of addresses\n"
                "3. \"count\"                    (integer, optional, default=50000, MAX=50000) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult:\n"
                "[ "
                "  (address): balance,\n"
                "  ...\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\" false 2 0")
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\" true")
                + HelpExampleCli("listaddressesbytoken", "\"TOKEN_NAME\"")
        );

    LOCK(cs_main);

    std::string token_name = request.params[0].get_str();
    bool fOnlyTotal = false;
    if (request.params.size() > 1)
        fOnlyTotal = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    if (!IsTokenNameValid(token_name))
        return "_Not a valid token name";

    LOCK(cs_main);
    std::vector<std::pair<std::string, CAmount> > vecAddressAmounts;
    int nTotalEntries = 0;
    if (!ptokensdb->TokenAddressDir(vecAddressAmounts, nTotalEntries, fOnlyTotal, token_name, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve address token directory.");

    // If only the number of addresses is wanted return it
    if (fOnlyTotal) {
        return nTotalEntries;
    }

    UniValue result(UniValue::VOBJ);
    for (auto& pair : vecAddressAmounts) {
        result.push_back(Pair(pair.first, UnitValueFromAmount(pair.second, token_name)));
    }


    return result;
}
#ifdef ENABLE_WALLET

UniValue transfer(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 3 || request.params.size() > 9)
        throw std::runtime_error(
                "transfer \"token_name\" qty \"to_address\" timelock \"message\" \"token_message\" expire_time \"change_address\" \"token_change_address\"\n"
                + TokenActivationWarning() +
                "\nTransfers a quantity of an owned token to a given address"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token\n"
                "2. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
                "3. \"to_address\"               (string, required) address to send the token to\n"
                "4. \"timelock\"                 (integer, optional, default=0) Timelock for token UTXOs, could be height or timestamp\n"
                "5. \"message\"                  (string, optional, default="") Message attached to transaction. \n"
                "6. \"token_message\"            (string, optional) Once messaging is voted in ipfs hash or txid hash to send along with the transfer\n"
                "7. \"expire_time\"              (numeric, optional) UTC timestamp of when the message expires\n"
                "8. \"change_address\"           (string, optional, default = \"\") the transactions YONA change will be sent to this address\n"
                "9. \"token_change_address\"     (string, optional, default = \"\") the transactions Token change will be sent to this address\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transfer", "\"TOKEN_NAME\" 20 \"address\" 10 \"message\" \"\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 15863654")
                + HelpExampleCli("transfer", "\"TOKEN_NAME\" 20 \"address\" 10 \"message\" \"\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 15863654")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string token_name = request.params[0].get_str();

    if (IsTokenNameAQualifier(token_name))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Please use the rpc call transferqualifiertoken to send qualifier tokens from this wallet.");

    CAmount nAmount = AmountFromValue(request.params[1]);

    std::string to_address = request.params[2].get_str();

    if (IsUsernameValid(to_address)) {
        to_address = ptokensdb->UsernameAddress(to_address);
        if (to_address == "") {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "You specified invalid username.");
        }
    }

    // Time lock
    int timeLock = 0;
    if (!request.params[3].isNull())
        timeLock = request.params[3].get_int();

    CTxDestination to_dest = DecodeDestination(to_address);
    if (!IsValidDestination(to_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + to_address);
    }

    std::string message;
    if (request.params.size() > 4) {
        message = request.params[4].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    bool fMessageCheck = false;
    std::string token_message = "";
    if (request.params.size() > 5) {
        token_message = request.params[5].get_str();
        if (!token_message.empty())
            fMessageCheck = true;
    }

    int64_t expireTime = 0;
    if (!token_message.empty()) {
        if (request.params.size() > 6) {
            expireTime = request.params[6].get_int64();
        }
    }

    if (!token_message.empty() || expireTime > 0) {
        if (!AreMessagesDeployed()) {
            throw JSONRPCError(RPC_INVALID_PARAMS, std::string("Unable to send messages"));
        }
    }

    if (fMessageCheck)
        CheckIPFSTxidMessage(token_message, expireTime);

    std::string yona_change_address = "";
    if (request.params.size() > 7) {
        yona_change_address = request.params[7].get_str();
    }

    std::string token_change_address = "";
    if (request.params.size() > 8) {
        token_change_address = request.params[8].get_str();
    }

    CTxDestination yona_change_dest = DecodeDestination(yona_change_address);
    if (!yona_change_address.empty() && !IsValidDestination(yona_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("YONA change address must be a valid address. Invalid address: ") + yona_change_address);

    CTxDestination token_change_dest = DecodeDestination(token_change_address);
    if (!token_change_address.empty() && !IsValidDestination(token_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Token change address must be a valid address. Invalid address: ") + token_change_address);

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    CTokenTransfer transfer(token_name, nAmount, timeLock, DecodeTokenData(token_message), expireTime);

    vTransfers.emplace_back(std::make_pair(transfer, to_address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;
    ctrl.destChange = yona_change_dest;
    ctrl.tokenDestChange = token_change_dest;

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message))
        throw JSONRPCError(error.first, error.second);

    // Do a validity check before commiting the transaction
    CheckRestrictedTokenTransferInputs(transaction, token_name);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue transferfromaddresses(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 4 || request.params.size() > 10)
        throw std::runtime_error(
            "transferfromaddresses \"token_name\" [\"from_addresses\"] qty \"to_address\" timelock \"message\" \"token_message\" expire_time \"yona_change_address\" \"token_change_address\"\n"
            + TokenActivationWarning() +
            "\nTransfer a quantity of an owned token in specific address(es) to a given address"

            "\nArguments:\n"
            "1. \"token_name\"               (string, required) name of token\n"
            "2. \"from_addresses\"           (array, required) list of from addresses to send from\n"
            "3. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
            "4. \"to_address\"               (string, required) address to send the token to\n"
            "5. \"timelock\"                 (integer, optional, default=0) Timelock for token UTXOs, could be height or timestamp\n"
            "6. \"message\"                  (string, optional, default="") Message attached to transaction. \n"
            "7. \"token_message\"            (string, optional) Once messaging is voted in ipfs hash or txid hash to send along with the transfer\n"
            "8. \"expire_time\"              (numeric, optional) UTC timestamp of when the message expires\n"
            "9. \"yona_change_address\"      (string, optional, default = \"\") the transactions YONA change will be sent to this address\n"
            "10. \"token_change_address\"    (string, optional, default = \"\") the transactions Token change will be sent to this address\n"

            "\nResult:\n"
            "txid"
            "[ \n"
                "txid\n"
                "]\n"

            "\nExamples:\n"
            + HelpExampleCli("transferfromaddresses", "\"TOKEN_NAME\" \'[\"fromaddress1\", \"fromaddress2\"]\' 20 \"to_address\" 1000 \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 154652365")
            + HelpExampleRpc("transferfromaddresses", "\"TOKEN_NAME\" \'[\"fromaddress1\", \"fromaddress2\"]\' 20 \"to_address\" 1000 \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 154652365")
            );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string token_name = request.params[0].get_str();

    const UniValue& from_addresses = request.params[1];

    if (!from_addresses.isArray() || from_addresses.size() < 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("From addresses must be a non-empty array."));
    }

    std::set<std::string> setFromDestinations;

    // Add the given array of addresses into the set of destinations
    for (int i = 0; i < (int) from_addresses.size(); i++) {
        std::string address = from_addresses[i].get_str();

        if (IsUsernameValid(address)) {
            address = ptokensdb->UsernameAddress(address);
            if (address == "") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "You specified invalid username.");
            }
        }

        CTxDestination dest = DecodeDestination(address);
        if (!IsValidDestination(dest))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("From addresses must be valid addresses. Invalid address: ") + address);

        setFromDestinations.insert(address);
    }

    CAmount nAmount = AmountFromValue(request.params[2]);

    std::string address = request.params[3].get_str();

    // Time lock
    int timeLock = 0;
    if (!request.params[4].isNull())
        timeLock = request.params[4].get_int();

    std::string message;
    if (request.params.size() > 5) {
        message = request.params[5].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    bool fMessageCheck = false;
    std::string token_message = "";
    if (request.params.size() > 6) {
        token_message = request.params[6].get_str();
        if (!token_message.empty())
            fMessageCheck = true;
    }

    int64_t expireTime = 0;
    if (!token_message.empty()) {
        if (request.params.size() > 7) {
            expireTime = request.params[7].get_int64();
        }
    }

    if (fMessageCheck)
        CheckIPFSTxidMessage(token_message, expireTime);

    std::string yona_change_address = "";
    if (request.params.size() > 8) {
        yona_change_address = request.params[8].get_str();
    }

    std::string token_change_address = "";
    if (request.params.size() > 9) {
        token_change_address = request.params[9].get_str();
    }

    CTxDestination yona_change_dest = DecodeDestination(yona_change_address);
    if (!yona_change_address.empty() && !IsValidDestination(yona_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("YONA change address must be a valid address. Invalid address: ") + yona_change_address);

    CTxDestination token_change_dest = DecodeDestination(token_change_address);
    if (!token_change_address.empty() && !IsValidDestination(token_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Token change address must be a valid address. Invalid address: ") + token_change_address);

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, timeLock, DecodeTokenData(token_message), expireTime), address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;
    std::map<std::string, std::vector<COutput> > mapTokenCoins;
    pwallet->AvailableTokens(mapTokenCoins);

    // Set the change addresses
    ctrl.destChange = yona_change_dest;
    ctrl.tokenDestChange = token_change_dest;

    if (!mapTokenCoins.count(token_name)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Wallet doesn't own the token_name: " + token_name));
    }

    // Add all the token outpoints that match the set of given from addresses
    for (const auto& out : mapTokenCoins.at(token_name)) {
        // Get the address that the coin resides in, because to send a valid message. You need to send it to the same address that it currently resides in.
        CTxDestination dest;
        ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, dest);

        if (setFromDestinations.count(EncodeDestination(dest)))
            ctrl.SelectToken(COutPoint(out.tx->GetHash(), out.i));
    }

    std::vector<COutPoint> outs;
    ctrl.ListSelectedTokens(outs);
    if (!outs.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("No token outpoints are selected from the given addresses, failed to create the transaction"));
    }

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message))
    throw JSONRPCError(error.first, error.second);

    // Do a validity check before commiting the transaction
    CheckRestrictedTokenTransferInputs(transaction, token_name);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
    throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue transferfromaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 4 || request.params.size() > 10)
        throw std::runtime_error(
                "transferfromaddress \"token_name\" \"from_address\" qty \"to_address\" timelock \"message\" \"token_message\" expire_time \"yona_change_address\" \"token_change_address\"\n"
                + TokenActivationWarning() +
                "\nTransfer a quantity of an owned token in a specific address to a given address"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token\n"
                "2. \"from_address\"             (string, required) address that the token will be transferred from\n"
                "3. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
                "4. \"to_address\"               (string, required) address to send the token to\n"
                "5. \"timelock\"                 (integer, optional, default=0) Timelock for token UTXOs, could be height or timestamp\n"
                "6. \"message\"                  (string, optional, default="") Message attached to transaction. \n"
                "7. \"token_message\"            (string, optional) Once messaging is voted in ipfs hash or txid hash to send along with the transfer\n"
                "8. \"expire_time\"              (numeric, optional) UTC timestamp of when the message expires\n"
                "9. \"yona_change_address\"      (string, optional, default = \"\") the transaction YONA change will be sent to this address\n"
                "10. \"token_change_address\"    (string, optional, default = \"\") the transaction Token change will be sent to this address\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transferfromaddress", "\"TOKEN_NAME\" \"fromaddress\" 20 \"address\" 1000 \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\", 156545652")
                + HelpExampleRpc("transferfromaddress", "\"TOKEN_NAME\" \"fromaddress\" 20 \"address\" 1000 \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\", 156545652")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string token_name = request.params[0].get_str();

    std::string from_address = request.params[1].get_str();

    // Check to make sure the given from address is valid
    CTxDestination dest = DecodeDestination(from_address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("From address must be valid addresses. Invalid address: ") + from_address);

    CAmount nAmount = AmountFromValue(request.params[2]);

    std::string address = request.params[3].get_str();

    // Time lock
    int timeLock = 0;
    if (!request.params[4].isNull())
        timeLock = request.params[4].get_int();

    std::string message;
    if (request.params.size() > 5) {
        message = request.params[5].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    bool fMessageCheck = false;
    std::string token_message = "";
    if (request.params.size() > 6) {
        token_message = request.params[6].get_str();
        if (!token_message.empty())
            fMessageCheck = true;
    }

    int64_t expireTime = 0;
    if (!token_message.empty()) {
        if (request.params.size() > 7) {
            expireTime = request.params[7].get_int64();
        }
    }

    if (fMessageCheck)
        CheckIPFSTxidMessage(token_message, expireTime);

    std::string yona_change_address = "";
    if (request.params.size() > 8) {
        yona_change_address = request.params[8].get_str();
    }

    std::string token_change_address = "";
    if (request.params.size() > 9) {
        token_change_address = request.params[9].get_str();
    }

    CTxDestination yona_change_dest = DecodeDestination(yona_change_address);
    if (!yona_change_address.empty() && !IsValidDestination(yona_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("YONA change address must be a valid address. Invalid address: ") + yona_change_address);

    CTxDestination token_change_dest = DecodeDestination(token_change_address);
    if (!token_change_address.empty() && !IsValidDestination(token_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Token change address must be a valid address. Invalid address: ") + token_change_address);


    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    vTransfers.emplace_back(std::make_pair(CTokenTransfer(token_name, nAmount, timeLock, DecodeTokenData(token_message), expireTime), address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;
    std::map<std::string, std::vector<COutput> > mapTokenCoins;
    pwallet->AvailableTokens(mapTokenCoins);

    // Set the change addresses
    ctrl.destChange = yona_change_dest;
    ctrl.tokenDestChange = token_change_dest;

    if (!mapTokenCoins.count(token_name)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Wallet doesn't own the token_name: " + token_name));
    }

    // Add all the token outpoints that match the given from addresses
    for (const auto& out : mapTokenCoins.at(token_name)) {
        // Get the address that the coin resides in, because to send a valid message. You need to send it to the same address that it currently resides in.
        CTxDestination dest;
        ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, dest);

        if (from_address == EncodeDestination(dest))
            ctrl.SelectToken(COutPoint(out.tx->GetHash(), out.i));
    }

    std::vector<COutPoint> outs;
    ctrl.ListSelectedTokens(outs);
    if (!outs.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("No token outpoints are selected from the given address, failed to create the transaction"));
    }

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message))
        throw JSONRPCError(error.first, error.second);

    // Do a validity check before commiting the transaction
    CheckRestrictedTokenTransferInputs(transaction, token_name);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}


UniValue reissue(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 7 || request.params.size() < 3)
        throw std::runtime_error(
                "reissue \"token_name\" qty \"to_address\" \"change_address\" ( reissuable ) ( new_units) \"( new_ipfs )\" \n"
                + TokenActivationWarning() +
                "\nReissues a quantity of an token to an owned address if you own the Owner Token"
                "\nCan change the reissuable flag during reissuance"
                "\nCan change the ipfs hash during reissuance"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) name of token that is being reissued\n"
                "2. \"qty\"                      (numeric, required) number of tokens to reissue\n"
                "3. \"to_address\"               (string, required) address to send the token to\n"
                "4. \"change_address\"           (string, optional) address that the change of the transaction will be sent to\n"
                "5. \"reissuable\"               (boolean, optional, default=true), whether future reissuance is allowed\n"
                "6. \"new_units\"                (numeric, optional, default=-1), the new units that will be associated with the token\n"
                "7. \"new_ipfs\"                 (string, optional, default=\"\"), whether to update the current ipfs hash or txid once messaging is active\n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("reissue", "\"TOKEN_NAME\" 20 \"address\"")
                + HelpExampleRpc("reissue", "\"TOKEN_NAME\" 20 \"address\" \"change_address\" \"true\" 8 \"Qmd286K6pohQcTKYqnS1YhWrCiS4gz7Xi34sdwMe9USZ7u\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    // To send a transaction the wallet must be unlocked
    EnsureWalletIsUnlocked(pwallet);

    // Get that paramaters
    std::string token_name = request.params[0].get_str();
    CAmount nAmount = AmountFromValue(request.params[1]);
    std::string address = request.params[2].get_str();

    std::string changeAddress =  "";
    if (request.params.size() > 3)
        changeAddress = request.params[3].get_str();

    bool reissuable = true;
    if (request.params.size() > 4) {
        reissuable = request.params[4].get_bool();
    }

    int newUnits = -1;
    if (request.params.size() > 5) {
        newUnits = request.params[5].get_int();
    }

    std::string newipfs = "";
    bool fMessageCheck = false;

    if (request.params.size() > 6) {
        fMessageCheck = true;
        newipfs = request.params[6].get_str();
    }

    // Reissues don't have an expire time
    int64_t expireTime = 0;

    // Check the message data
    if (fMessageCheck)
        CheckIPFSTxidMessage(newipfs, expireTime);

    CReissueToken reissueToken(token_name, nAmount, newUnits, reissuable, DecodeTokenData(newipfs));

    std::pair<int, std::string> error;
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(changeAddress);

    // Create the Transaction
    if (!CreateReissueTokenTransaction(pwallet, crtl, reissueToken, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    std::string strError = "";
    if (!ContextualCheckReissueToken(ptokens, reissueToken, strError, *transaction.tx.get()))
        throw JSONRPCError(RPC_INVALID_REQUEST, strError);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

/**
 * Sweep
 *
 * Attempts to sweep from a private key. The default is to sweep all tokens and
 * YONA, but can be limited to either all of the YONA or one token type by passing
 * the optional argument `token_filter`.
 */
UniValue sweep(const JSONRPCRequest& request)
{
    // Ensure that we have a wallet to sweep into
    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    // If we are requesting help, have no tokens, or have passed in too many / few
    //   arguments then just show the help.
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 2 || request.params.size() < 1)
        throw std::runtime_error(
                "sweep \"privkey\" ( \"token_name\" | \"YONA\" ) \n"
                + TokenActivationWarning() +
                "\nCreates a transaction to transfer all YONA, and all Tokens from a given address -- with only the private key as input.\n"
                "\nDefault to funding from YONA held in the address, fallback to using YONA held in wallet for transaction fee."
                "\nDefault to sweeping all tokens, but can also all with YONA to sweep only YONA, or to sweep only one token."
                "\nThis differs from import because a paper certficate provided with artwork or a one-of-a-kind item can include a paper"
                " certficate-of-authenticity. Once swept it the paper certificate can be safely discarded as the token is secured by the new address.\n"

                "\nArguments:\n"
                "1. \"privkey\"               (string, required) private key of addresses from which to sweep\n"
                "2. \"token_name\"            (string, optional, default=\"\") name of the token to sweep or YONA"

                "\nResult:\n"
                "\"txhex\"                    (string) The transaction hash in hex\n"

                "\nExamples:\n"
                + HelpExampleCli("sweep", "\"privkey\"")
                + HelpExampleRpc("sweep", "\"privkey\" \"TOKEN_NAME\"")
                + HelpExampleRpc("sweep", "\"privkey\" \"YONA\"")
        );

    // See whether we should sweep everything or only a specific token
    // Default is to sweep everything (TODO: Should default be `YONA`?)
    std::string token_name = "";
    if (!request.params[1].isNull()) {
        token_name = request.params[1].get_str();
    }

    // Convert the private key to a usable key
    CYonaSecret secret;
    std::string private_key = request.params[0].get_str();
    if (!secret.SetString(private_key)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    // Verify that the key is valid
    CKey sweep_key = secret.GetKey();
    if (!sweep_key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    // Verify that the public address is also valid
    CPubKey pub_key = sweep_key.GetPubKey();
    assert(sweep_key.VerifyPubKey(pub_key));
    CKeyID addr = pub_key.GetID();
    std::string addr_str = EncodeDestination(addr);

    // Keep track of the private keys necessary for the transaction
    std::set<std::string> signatures = {private_key};

    // Create a base JSONRPCRequest to use for all subsequent operations as a copy
    //   of the original request
    JSONRPCRequest base_request = request;

    // Helper method for calling RPC calls
    auto CallRPC = [&base_request](const std::string& method, const UniValue& params)
    {
        base_request.strMethod = method;
        base_request.params = params;

        return tableRPC.execute(base_request);
    };

    // Get the balance for both ourselves and the swept address
    CAmount our_balance = pwallet->GetBalance();
    CAmount swept_balance;
    {
        UniValue swept_params = UniValue(UniValue::VARR);
        UniValue swept_nested = UniValue(UniValue::VOBJ);
        UniValue swept_addresses = UniValue(UniValue::VARR);

        swept_addresses.push_back(addr_str);
        swept_nested.pushKV("addresses", swept_addresses);
        swept_params.push_back(swept_nested);

        // Get the balances
        UniValue balance = CallRPC("getaddressbalance", swept_params);

        if (!ParseInt64(balance["balance"].getValStr(), &swept_balance)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid balance for swept address!");
        }
    }

    // Make sure that we can fund this transaction first
    // TODO: I think that there is a more accurate way to calculate the fee,
    //   as just always using the min transaction fee seems wrong.
    if (swept_balance + our_balance < DEFAULT_MIN_RELAY_TX_FEE) {
        throw JSONRPCError(
            RPC_WALLET_INSUFFICIENT_FUNDS,
            tfm::format(
                "Please add YONA to address '%s' to be able to sweep token '%s'",
                addr_str,
                token_name
            )
        );
    }

    // Get two new addresses to sweep into: one for all of the tokens and another
    //   for the YONA
    // TODO: Does generating multiple addresses which may not be used cause a performance hit?
    std::string dest_ast_str = CallRPC("getnewaddress", UniValue(UniValue::VOBJ)).getValStr();
    std::string dest_yona_str = CallRPC("getnewaddress", UniValue(UniValue::VOBJ)).getValStr();

    // Request the unspent transactions
    // TODO: Should this be replaced with a call to the api.yonacoin.network?
    // Format of params is as follows:
    // {
    //    addresses: ["PUB ADDR"],
    //    tokenName: "TOKEN NAME"
    // }
    UniValue unspent;
    UniValue unspent_yona;
    UniValue unspent_our_yona;
    {
        // Helper method for getting UTXOs from an address of a specific token
        auto get_unspent = [&CallRPC](const std::string& addr, const std::string& token) {
            UniValue utxo_params = UniValue(UniValue::VARR);
            UniValue utxo_inner  = UniValue(UniValue::VOBJ);
            UniValue utxo_addrs  = UniValue(UniValue::VARR);
            utxo_addrs.push_back(addr);
            utxo_inner.pushKV("addresses", utxo_addrs);
            utxo_inner.pushKV("tokenName", token);
            utxo_params.push_back(utxo_inner);

            return CallRPC("getaddressutxos", utxo_params);
        };

        // Get the specified token UTXOs
        unspent = get_unspent(addr_str, token_name);

        // We also get just the unspent YONA from the swept address for potential fee funding
        unspent_yona = get_unspent(addr_str, YONA);

        // Get our unspent YONA for funding if the swept address does not have enough
        unspent_our_yona = CallRPC("listunspent", UniValue(UniValue::VNULL));
    }

    // Short out if there is nothing to sweep
    if (unspent.size() == 0) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "No tokens to sweep!");
    }

    // Create a raw transaction with all of the unspent transactions
    UniValue created_transaction;
    {
        UniValue create_params = UniValue(UniValue::VARR);
        UniValue create_input = UniValue(UniValue::VARR);
        UniValue create_dest = UniValue(UniValue::VOBJ);

        // Keep track of how much more YONA we will need from either the swept
        //   address or our own wallet.
        // TODO: I think that there is a more accurate way to calculate the fee,
        //   as just always using the min transaction fee seems wrong.
        CAmount fee_left = DEFAULT_MIN_RELAY_TX_FEE;
        CAmount fee_paid_by_us = 0;

        // Calculate totals for the output of the transaction and map the inputs
        //   into the correct format of {txid, vout}
        std::map<std::string, CAmount> token_totals;
        for (size_t i = 0; i != unspent.size(); ++i) {
            UniValue current_input = UniValue(UniValue::VOBJ);

            const UniValue& current = unspent[i];
            std::string curr_token_name = current["tokenName"].getValStr();
            CAmount curr_amount;

            // Parse the amount safely
            if (!ParseInt64(current["satoshis"].getValStr(), &curr_amount)) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid amount in UTXOs!");
            }

            // Subtract from the fee if YONA is being added to the list of inputs and
            //   subtract from the total sent.
            // Note: We do this because this allows for any YONA being swept to pay
            //   the fee rather than our own address.
            if (fee_left != 0 && curr_token_name == YONA) {
                CAmount fee_diff = fee_left - curr_amount;

                fee_paid_by_us += (fee_diff > 0) ? curr_amount : fee_left;
                fee_left = (fee_diff > 0) ? fee_diff : 0;
            }

            // Update the total
            // Note: [] access creates a default value if not in map
            token_totals[curr_token_name] += curr_amount;

            // Add to input
            current_input.pushKV("txid", current["txid"]);
            current_input.pushKV("vout", current["outputIndex"]);
            create_input.push_back(current_input);
        }

        // If we still have some fee left, then try to fund from the swept address
        //   first (assumming we haven't swept for YONA or everything [which includes YONA])
        //   and then try to fund from our own wallets. Since we checked above
        //   if the balances worked out, then there is no way it will fail here.
        if (fee_left != 0) {
            if (
                (token_name != YONA && token_name != "") && // We haven't already considered YONA in our sweep above
                swept_balance != 0                         // We have funds to try
            ) {
                // Add as many UTXOs as needed until we either run out or successfully
                //   fund the transaction
                for (size_t i = 0; i != unspent_yona.size() && fee_left != 0; ++i) {
                    UniValue current_input = UniValue(UniValue::VOBJ);

                    const UniValue& current = unspent_yona[i];
                    CAmount curr_amount;

                    // Parse the amount safely
                    if (!ParseInt64(current["satoshis"].getValStr(), &curr_amount)) {
                        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid amount in UTXOs!");
                    }

                    // Add it to the input
                    current_input.pushKV("txid", current["txid"]);
                    current_input.pushKV("vout", current["outputIndex"]);
                    create_input.push_back(current_input);

                    // Update the fee and keep any change that may be incurred
                    if (fee_left >= curr_amount) {
                        fee_left -= curr_amount;
                    } else {
                        // Send change back to the swept address
                        CAmount change = curr_amount - fee_left;

                        create_dest.pushKV(addr_str, ValueFromAmount(change));
                        fee_left = 0;
                    }
                }
            }

            // Fund the rest with our wallet, if needed
            if (fee_left != 0) {
                // Add as many UTXOs as needed until we either run out or successfully
                //   fund the transaction
                for (size_t i = 0; i != unspent_our_yona.size() && fee_left != 0; ++i) {
                    UniValue current_input = UniValue(UniValue::VOBJ);

                    const UniValue& current = unspent_our_yona[i];
                    CAmount curr_amount = AmountFromValue(current["amount"]);
                    bool is_safe = current["safe"].getBool();

                    // Skip unsafe coins
                    // TODO: Is this wanted behaviour?
                    if (!is_safe) continue;

                    // Add it to the input
                    current_input.pushKV("txid", current["txid"]);
                    current_input.pushKV("vout", current["vout"]);
                    create_input.push_back(current_input);

                    // Add it to the totals
                    token_totals[YONA] += curr_amount;

                    // Add our private key to the transaction for signing
                    UniValue utxo_nested = UniValue(UniValue::VARR);
                    utxo_nested.push_back(current["address"].getValStr());
                    UniValue utxo_privkey = CallRPC("dumpprivkey", utxo_nested);
                    signatures.insert(utxo_privkey.getValStr());

                    // Update the fee and keep track of how much to pay off at the end
                    if (fee_left > curr_amount) {
                        fee_left -= curr_amount;
                        fee_paid_by_us += curr_amount;
                    } else {
                        fee_paid_by_us += fee_left;
                        fee_left = 0;
                    }
                }

                // Sanity check
                if (fee_left != 0) {
                    throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Funds available does not match funds required. Do you have unsafe transactions?");
                }
            }
        }

        // Convert the totals into their corresponding object types
        // Note: Structure of complete output is as follows
        // {
        //     "DESTINATION ADDRESS": {
        //         "transfer": {
        //             "YONA": Total YONA to sweep,
        //             "Example Token": Total Token count,
        //             ...
        //         }
        //     }
        // }
        UniValue curr_transfer = UniValue(UniValue::VOBJ);
        for (const auto& it : token_totals) {
            std::string curr_token_name = it.first;
            CAmount curr_amount = it.second;

            // We skip YONA here becuase we need to send that to another of our addresses
            if (curr_token_name == YONA) continue;

            curr_transfer.pushKV(curr_token_name, ValueFromAmount(curr_amount));
        }

        // Add the YONA output, if available
        if (token_totals.find(YONA) != token_totals.end()) {
            CAmount yona_amount = token_totals[YONA] - fee_paid_by_us;

            // Only add YONA to the output if there is some left over after the fee
            if (yona_amount != 0) {
                create_dest.pushKV(dest_yona_str, ValueFromAmount(yona_amount));
            }
        }

        // Finish wrapping the transfer, if there are any
        if (curr_transfer.size() != 0) {
            UniValue nested_transfer = UniValue(UniValue::VOBJ);
            nested_transfer.pushKV("transfer", curr_transfer);
            create_dest.pushKV(dest_ast_str, nested_transfer);
        }

        // Add the inputs and outputs
        create_params.push_back(create_input);
        create_params.push_back(create_dest);

        // Call the RPC to create the transaction
        created_transaction = CallRPC("createrawtransaction", create_params);
    }

    // Sign the transaction with the swept private key
    UniValue signed_transaction;
    {
        UniValue signed_params = UniValue(UniValue::VARR);
        UniValue signed_privkeys = UniValue(UniValue::VARR);

        // Use the supplied private key to allow for the transaction to occur
        for (const auto& it : signatures) {
            signed_privkeys.push_back(it);
        }

        signed_params.push_back(created_transaction);
        signed_params.push_back(UniValue(UniValue::VNULL)); // We use NULL for prevtxs since there aren't any
        signed_params.push_back(signed_privkeys);

        // Call the RPC to sign the transaction
        signed_transaction = CallRPC("signrawtransaction", signed_params);
    }

    // Commit the transaction to the network
    UniValue completed_transaction;
    {
        UniValue completed_params = UniValue(UniValue::VARR);

        // Only use the hex from the previous RPC
        // TODO: Should we allow high fees?
        completed_params.push_back(signed_transaction["hex"].getValStr());

        // Call the RPC to complete the transaction
        completed_transaction = CallRPC("sendrawtransaction", completed_params);
    }

    return completed_transaction;
}
#endif

UniValue listtokens(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() > 4)
        throw std::runtime_error(
                "listtokens \"( token )\" ( verbose ) ( count ) ( start )\n"
                + TokenActivationWarning() +
                "\nReturns a list of all tokens\n"
                "\nThis could be a slow/expensive operation as it reads from the database\n"

                "\nArguments:\n"
                "1. \"token\"                    (string, optional, default=\"*\") filters results -- must be an token name or a partial token name followed by '*' ('*' matches all trailing characters)\n"
                "2. \"verbose\"                  (boolean, optional, default=false) when false result is just a list of token names -- when true results are token name mapped to metadata\n"
                "3. \"count\"                    (integer, optional, default=ALL) truncates results to include only the first _count_ tokens found\n"
                "4. \"start\"                    (integer, optional, default=0) results skip over the first _start_ tokens found (if negative it skips back from the end)\n"

                "\nResult (verbose=false):\n"
                "[\n"
                "  token_name,\n"
                "  ...\n"
                "]\n"

                "\nResult (verbose=true):\n"
                "{\n"
                "  (token_name):\n"
                "    {\n"
                "      amount: (number),\n"
                "      units: (number),\n"
                "      reissuable: (number),\n"
                "      has_ipfs: (number),\n"
                "      ipfs_hash: (hash) (only if has_ipfs = 1 and data is a ipfs hash)\n"
                "      ipfs_hash: (hash) (only if has_ipfs = 1 and data is a txid hash)\n"
                "    },\n"
                "  {...}, {...}\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleRpc("listtokens", "")
                + HelpExampleCli("listtokens", "TOKEN")
                + HelpExampleCli("listtokens", "\"TOKEN*\" true 10 20")
        );

    ObserveSafeMode();

    if (!ptokensdb)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "token db unavailable.");

    std::string filter = "*";
    if (request.params.size() > 0)
        filter = request.params[0].get_str();

    if (filter == "")
        filter = "*";

    bool verbose = false;
    if (request.params.size() > 1)
        verbose = request.params[1].get_bool();

    size_t count = INT_MAX;
    if (request.params.size() > 2) {
        if (request.params[2].get_int() < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be greater than 1.");
        count = request.params[2].get_int();
    }

    long start = 0;
    if (request.params.size() > 3) {
        start = request.params[3].get_int();
    }

    std::vector<CDatabasedTokenData> tokens;
    if (!ptokensdb->TokenDir(tokens, filter, count, start))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "couldn't retrieve token directory.");

    UniValue result;
    result = verbose ? UniValue(UniValue::VOBJ) : UniValue(UniValue::VARR);

    for (auto data : tokens) {
        CNewToken token = data.token;
        if (verbose) {
            UniValue detail(UniValue::VOBJ);
            detail.push_back(Pair("name", token.strName));
            detail.push_back(Pair("amount", UnitValueFromAmount(token.nAmount, token.strName)));
            detail.push_back(Pair("units", token.units));
            detail.push_back(Pair("reissuable", token.nReissuable));
            detail.push_back(Pair("has_ipfs", token.nHasIPFS));
            detail.push_back(Pair("block_height", data.nHeight));
            detail.push_back(Pair("blockhash", data.blockHash.GetHex()));
            if (token.nHasIPFS) {
                if (token.strIPFSHash.size() == 32) {
                    detail.push_back(Pair("txid_hash", EncodeTokenData(token.strIPFSHash)));
                } else {
                    detail.push_back(Pair("ipfs_hash", EncodeTokenData(token.strIPFSHash)));
                }
            }
            result.push_back(Pair(token.strName, detail));
        } else {
            result.push_back(token.strName);
        }
    }

    return result;
}

UniValue getcacheinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size())
        throw std::runtime_error(
                "getcacheinfo \n"
                + TokenActivationWarning() +

                "\nResult:\n"
                "[\n"
                "  uxto cache size:\n"
                "  token total (exclude dirty):\n"
                "  token address map:\n"
                "  token address balance:\n"
                "  my unspent token:\n"
                "  reissue data:\n"
                "  token metadata map:\n"
                "  token metadata list (est):\n"
                "  dirty cache (est):\n"


                "]\n"

                "\nExamples:\n"
                + HelpExampleRpc("getcacheinfo", "")
                + HelpExampleCli("getcacheinfo", "")
        );

    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (!currentActiveTokenCache)
        throw JSONRPCError(RPC_VERIFY_ERROR, "token cache is null");

    if (!pcoinsTip)
        throw JSONRPCError(RPC_VERIFY_ERROR, "coins tip cache is null");

    if (!ptokensCache)
        throw JSONRPCError(RPC_VERIFY_ERROR, "token metadata cache is nul");

    UniValue result(UniValue::VARR);

    UniValue info(UniValue::VOBJ);
    info.push_back(Pair("uxto cache size", (int)pcoinsTip->DynamicMemoryUsage()));
    info.push_back(Pair("token total (exclude dirty)", (int)currentActiveTokenCache->DynamicMemoryUsage()));

    UniValue descendants(UniValue::VOBJ);

    descendants.push_back(Pair("token address balance",   (int)memusage::DynamicUsage(currentActiveTokenCache->mapTokensAddressAmount)));
    descendants.push_back(Pair("reissue data",   (int)memusage::DynamicUsage(currentActiveTokenCache->mapReissuedTokenData)));

    info.push_back(Pair("reissue tracking (memory only)", (int)memusage::DynamicUsage(mapReissuedTokens) + (int)memusage::DynamicUsage(mapReissuedTx)));
    info.push_back(Pair("token data", descendants));
    info.push_back(Pair("token metadata map",  (int)memusage::DynamicUsage(ptokensCache->GetItemsMap())));
    info.push_back(Pair("token metadata list (est)",  (int)ptokensCache->GetItemsList().size() * (32 + 80))); // Max 32 bytes for token name, 80 bytes max for token data
    info.push_back(Pair("dirty cache (est)",  (int)currentActiveTokenCache->GetCacheSize()));
    info.push_back(Pair("dirty cache V2 (est)",  (int)currentActiveTokenCache->GetCacheSizeV2()));

    result.push_back(info);
    return result;
}

UniValue getusernameaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "getusernameaddress @USERNAME\n"
                + TokenActivationWarning() +

                "\nExample:\n"
                + HelpExampleCli("getusernameaddress", "@USERNAME")
        );

    std::string address = ptokensdb->UsernameAddress(request.params[0].get_str());
    if (address == "") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You specified invalid username.");
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("address", address);

    return result;
}

#ifdef ENABLE_WALLET
UniValue addtagtoaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                "addtagtoaddress tag_name to_address (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nAssign a tag to a address\n"

                "\nArguments:\n"
                "1. \"tag_name\"            (string, required) the name of the tag you are assigning to the address, if it doens't have '#' at the front it will be added\n"
                "2. \"to_address\"          (string, required) the address that will be assigned the tag\n"
                "3. \"change_address\"      (string, optional) The change address for the qualifier token to be sent to\n"
                "4. \"token_data\"          (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the qualifier token\n"
                "5. \"message\"             (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("addtagtoaddress", "\"#TAG\" \"to_address\"")
                + HelpExampleRpc("addtagtoaddress", "\"#TAG\" \"to_address\"")
                + HelpExampleCli("addtagtoaddress", "\"#TAG\" \"to_address\" \"change_address\"")
                + HelpExampleRpc("addtagtoaddress", "\"#TAG\" \"to_address\" \"change_address\"")
        );

    // 1 - on
    return UpdateAddressTag(request, 1);
}

UniValue removetagfromaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                "removetagfromaddress tag_name to_address (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nRemove a tag from a address\n"

                "\nArguments:\n"
                "1. \"tag_name\"            (string, required) the name of the tag you are removing from the address\n"
                "2. \"to_address\"          (string, required) the address that the tag will be removed from\n"
                "3. \"change_address\"      (string, optional) The change address for the qualifier token to be sent to\n"
                "4. \"token_data\"          (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the qualifier token\n"
                "5. \"message\"             (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("removetagfromaddress", "\"#TAG\" \"to_address\"")
                + HelpExampleRpc("removetagfromaddress", "\"#TAG\" \"to_address\"")
                + HelpExampleCli("removetagfromaddress", "\"#TAG\" \"to_address\" \"change_address\"")
                + HelpExampleRpc("removetagfromaddress", "\"#TAG\" \"to_address\" \"change_address\"")
        );

    // 0 = off
    return UpdateAddressTag(request, 0);
}

UniValue freezeaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                "freezeaddress token_name address (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nFreeze an address from transferring a restricted token\n"

                "\nArguments:\n"
                "1. \"token_name\"       (string, required) the name of the restricted token you want to freeze\n"
                "2. \"address\"          (string, required) the address that will be frozen\n"
                "3. \"change_address\"   (string, optional) The change address for the owner token of the restricted token\n"
                "4. \"token_data\"       (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the owner token\n"
                "5. \"message\"          (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("freezeaddress", "\"$RESTRICTED_TOKEN\" \"address\"")
                + HelpExampleRpc("freezeaddress", "\"$RESTRICTED_TOKEN\" \"address\"")
                + HelpExampleCli("freezeaddress", "\"$RESTRICTED_TOKEN\" \"address\" \"change_address\"")
                + HelpExampleRpc("freezeaddress", "\"$RESTRICTED_TOKEN\" \"address\" \"change_address\"")
        );

    // 1 = Freeze
    return UpdateAddressRestriction(request, 1);
}

UniValue unfreezeaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
                "unfreezeaddress token_name address (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nUnfreeze an address from transferring a restricted token\n"

                "\nArguments:\n"
                "1. \"token_name\"       (string, required) the name of the restricted token you want to unfreeze\n"
                "2. \"address\"          (string, required) the address that will be unfrozen\n"
                "3. \"change_address\"   (string, optional) The change address for the owner token of the restricted token\n"
                "4. \"token_data\"       (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the owner token\n"
                "5. \"message\"          (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("unfreezeaddress", "\"$RESTRICTED_TOKEN\" \"address\"")
                + HelpExampleRpc("unfreezeaddress", "\"$RESTRICTED_TOKEN\" \"address\"")
                + HelpExampleCli("unfreezeaddress", "\"$RESTRICTED_TOKEN\" \"address\" \"change_address\"")
                + HelpExampleRpc("unfreezeaddress", "\"$RESTRICTED_TOKEN\" \"address\" \"change_address\"")
        );

    // 0 = Unfreeze
    return UpdateAddressRestriction(request, 0);
}

UniValue freezerestrictedtoken(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
                "freezerestrictedtoken token_name (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nFreeze all trading for a specific restricted token\n"

                "\nArguments:\n"
                "1. \"token_name\"       (string, required) the name of the restricted token you want to unfreeze\n"
                "2. \"change_address\"   (string, optional) The change address for the owner token of the restricted token\n"
                "3. \"token_data\"       (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the owner token\n"
                "4. \"message\"          (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("freezerestrictedtoken", "\"$RESTRICTED_TOKEN\"")
                + HelpExampleRpc("freezerestrictedtoken", "\"$RESTRICTED_TOKEN\"")
                + HelpExampleCli("freezerestrictedtoken", "\"$RESTRICTED_TOKEN\" \"change_address\"")
                + HelpExampleRpc("freezerestrictedtoken", "\"$RESTRICTED_TOKEN\" \"change_address\"")
        );

    // 1 = Freeze all trading
    return UpdateGlobalRestrictedToken(request, 1);
}

UniValue unfreezerestrictedtoken(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
                "unfreezerestrictedtoken token_name (change_address) (token_data)\n"
                + RestrictedActivationWarning() +
                "\nUnfreeze all trading for a specific restricted token\n"

                "\nArguments:\n"
                "1. \"token_name\"       (string, required) the name of the restricted token you want to unfreeze\n"
                "2. \"change_address\"   (string, optional) The change address for the owner token of the restricted token\n"
                "3. \"token_data\"       (string, optional) The token data (ipfs or a hash) to be applied to the transfer of the owner token\n"
                "4. \"message\"          (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("unfreezerestrictedtoken", "\"$RESTRICTED_TOKEN\"")
                + HelpExampleRpc("unfreezerestrictedtoken", "\"$RESTRICTED_TOKEN\"")
                + HelpExampleCli("unfreezerestrictedtoken", "\"$RESTRICTED_TOKEN\" \"change_address\"")
                + HelpExampleRpc("unfreezerestrictedtoken", "\"$RESTRICTED_TOKEN\" \"change_address\"")
        );

    // 0 = Unfreeze all trading
    return UpdateGlobalRestrictedToken(request, 0);
}
#endif

UniValue listtagsforaddress(const JSONRPCRequest &request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() !=1)
        throw std::runtime_error(
                "listtagsforaddress address\n"
                + RestrictedActivationWarning() +
                "\nList all tags assigned to an address\n"

                "\nArguments:\n"
                "1. \"address\"          (string, required) the address to list tags for\n"

                "\nResult:\n"
                "["
                "\"tag_name\",        (string) The tag name\n"
                "...,\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listtagsforaddress", "\"address\"")
                + HelpExampleRpc("listtagsforaddress", "\"address\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    std::string address = request.params[0].get_str();

    // Check to make sure the given from address is valid
    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Not valid YONA address: ") + address);

    std::vector<std::string> qualifiers;

    // This function forces a FlushStateToDisk so that a database scan and occur
    if (!prestricteddb->GetAddressQualifiers(address, qualifiers)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to search the database");
    }

    UniValue ret(UniValue::VARR);
    for (auto item : qualifiers) {
        ret.push_back(item);
    }

    return ret;
}

UniValue listaddressesfortag(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() !=1)
        throw std::runtime_error(
                "listaddressesfortag tag_name\n"
                + RestrictedActivationWarning() +
                "\nList all addresses that have been assigned a given tag\n"

                "\nArguments:\n"
                "1. \"tag_name\"          (string, required) the tag token name to search for\n"

                "\nResult:\n"
                "["
                "\"address\",        (string) The address\n"
                "...,\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listaddressesfortag", "\"#TAG\"")
                + HelpExampleRpc("listaddressesfortag", "\"#TAG\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    std::string qualifier_name = request.params[0].get_str();

    if (!IsTokenNameAQualifier(qualifier_name))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You must use qualifier token names only, qualifier tokens start with '#'");


    std::vector<std::string> addresses;

    // This function forces a FlushStateToDisk so that a database scan and occur
    if (!prestricteddb->GetQualifierAddresses(qualifier_name, addresses)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to search the database");
    }

    UniValue ret(UniValue::VARR);
    for (auto item : addresses) {
        ret.push_back(item);
    }

    return ret;
}

UniValue listaddressrestrictions(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() !=1)
        throw std::runtime_error(
                "listaddressrestrictions address\n"
                + RestrictedActivationWarning() +
                "\nList all tokens that have frozen this address\n"

                "\nArguments:\n"
                "1. \"address\"          (string), required) the address to list restrictions for\n"

                "\nResult:\n"
                "["
                "\"token_name\",        (string) The restriction name\n"
                "...,\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listaddressrestrictions", "\"address\"")
                + HelpExampleRpc("listaddressrestrictions", "\"address\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    std::string address = request.params[0].get_str();

    // Check to make sure the given from address is valid
    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Not valid YONA address: ") + address);

    std::vector<std::string> restrictions;

    if (!prestricteddb->GetAddressRestrictions(address, restrictions)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to search the database");
    }

    UniValue ret(UniValue::VARR);
    for (auto item : restrictions) {
        ret.push_back(item);
    }

    return ret;
}

UniValue listglobalrestrictions(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 0)
        throw std::runtime_error(
                "listglobalrestrictions\n"
                + RestrictedActivationWarning() +
                "\nList all global restricted tokens\n"


                "\nResult:\n"
                "["
                "\"token_name\", (string) The token name\n"
                "...,\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listglobalrestrictions", "")
                + HelpExampleRpc("listglobalrestrictions", "")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    std::vector<std::string> restrictions;

    if (!prestricteddb->GetGlobalRestrictions(restrictions)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to search the database");
    }

    UniValue ret(UniValue::VARR);
    for (auto item : restrictions) {
        ret.push_back(item);
    }

    return ret;
}

UniValue getverifierstring(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "getverifierstring restricted_name\n"
                + RestrictedActivationWarning() +
                "\nRetrieve the verifier string that belongs to the given restricted token\n"

                "\nArguments:\n"
                "1. \"restricted_name\"          (string, required) the token_name\n"

                "\nResult:\n"
                "\"verifier_string\", (string) The verifier for the token\n"

                "\nExamples:\n"
                + HelpExampleCli("getverifierstring", "\"restricted_name\"")
                + HelpExampleRpc("getverifierstring", "\"restricted_name\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    if (!ptokens) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Tokens cache not available");
    }

    std::string token_name = request.params[0].get_str();

    CNullTokenTxVerifierString verifier;
    if (!ptokens->GetTokenVerifierStringIfExists(token_name, verifier))
        throw JSONRPCError(RPC_INVALID_PARAMETER, _("Verifier not found for token: ") + token_name);

    return verifier.verifier_string;
}

UniValue checkaddresstag(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 2)
        throw std::runtime_error(
                "checkaddresstag address tag_name\n"
                + RestrictedActivationWarning() +
                "\nChecks to see if an address has the given tag\n"

                "\nArguments:\n"
                "1. \"address\"          (string, required) the YONA address to search\n"
                "1. \"tag_name\"         (string, required) the tag to search\n"

                "\nResult:\n"
                "\"true/false\", (boolean) If the address has the tag\n"

                "\nExamples:\n"
                + HelpExampleCli("checkaddresstag", "\"address\" \"tag_name\"")
                + HelpExampleRpc("checkaddresstag", "\"address\" \"tag_name\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    if (!ptokensQualifierCache)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Qualifier cache not available");

    if (!ptokens)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token cache not available");


    std::string address = request.params[0].get_str();
    std::string qualifier_name = request.params[1].get_str();

    // Check to make sure the given from address is valid
    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Not valid YONA address: ") + address);

    return ptokens->CheckForAddressQualifier(qualifier_name, address);
}

UniValue checkaddressrestriction(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 2)
        throw std::runtime_error(
                "checkaddressrestriction address restricted_name\n"
                + RestrictedActivationWarning() +
                "\nChecks to see if an address has been frozen by the given restricted token\n"

                "\nArguments:\n"
                "1. \"address\"          (string, required) the YONA address to search\n"
                "1. \"restricted_name\"   (string, required) the restricted token to search\n"

                "\nResult:\n"
                "\"true/false\", (boolean) If the address is frozen\n"

                "\nExamples:\n"
                + HelpExampleCli("checkaddressrestriction", "\"address\" \"restricted_name\"")
                + HelpExampleRpc("checkaddressrestriction", "\"address\" \"restricted_name\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    if (!ptokensRestrictionCache)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restriction cache not available");

    if (!ptokens)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token cache not available");

    std::string address = request.params[0].get_str();
    std::string restricted_name = request.params[1].get_str();

    // Check to make sure the given from address is valid
    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Not valid YONA address: ") + address);

    return ptokens->CheckForAddressRestriction(restricted_name, address);
}

UniValue checkglobalrestriction(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "checkglobalrestriction restricted_name\n"
                + RestrictedActivationWarning() +
                "\nChecks to see if a restricted token is globally frozen\n"

                "\nArguments:\n"
                "1. \"restricted_name\"   (string, required) the restricted token to search\n"

                "\nResult:\n"
                "\"true/false\", (boolean) If the restricted token is frozen globally\n"

                "\nExamples:\n"
                + HelpExampleCli("checkglobalrestriction", "\"restricted_name\"")
                + HelpExampleRpc("checkglobalrestriction", "\"restricted_name\"")
        );

    if (!prestricteddb)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restricted token database not available");

    if (!ptokensGlobalRestrictionCache)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Restriction cache not available");

    if (!ptokens)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token cache not available");

    std::string restricted_name = request.params[0].get_str();

    return ptokens->CheckForGlobalRestriction(restricted_name, true);
}

#ifdef ENABLE_WALLET

UniValue issuequalifiertoken(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 1 || request.params.size() > 6)
        throw std::runtime_error(
                "issuequalifiertoken \"token_name\" qty \"( to_address )\" \"( change_address )\" ( has_ipfs ) \"( ipfs_hash )\"\n"
                + RestrictedActivationWarning() +
                "\nIssue an qualifier or sub qualifier token\n"
                "If the '#' character isn't added, it will be added automatically\n"
                "Amount is a number between 1 and 10\n"
                "Token name must not conflict with any existing token.\n"
                "Unit is always set to Zero (0) for qualifier tokens\n"
                "Reissuable is always set to false for qualifier tokens\n"

                "\nArguments:\n"
                "1. \"token_name\"            (string, required) a unique name\n"
                "2. \"qty\"                   (numeric, optional, default=1) the number of units to be issued\n"
                "3. \"to_address\"            (string), optional, default=\"\"), address token will be sent to, if it is empty, address will be generated for you\n"
                "4. \"change_address\"        (string), optional, default=\"\"), address the the yona change will be sent to, if it is empty, change address will be generated for you\n"
                "5. \"has_ipfs\"              (boolean, optional, default=false), whether ipfs hash is going to be added to the token\n"
                "6. \"ipfs_hash\"             (string, optional but required if has_ipfs = 1), an ipfs hash or a txid hash once messaging is activated\n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("issuequalifiertoken", "\"#TOKEN_NAME\" 1000")
                + HelpExampleCli("issuequalifiertoken", "\"TOKEN_NAME\" 1000 \"myaddress\"")
                + HelpExampleCli("issuequalifiertoken", "\"#TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\"")
                + HelpExampleCli("issuequalifiertoken", "\"TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\"")
                + HelpExampleCli("issuequalifiertoken", "\"#TOKEN_NAME\" 1000 \"myaddress\" \"changeaddress\" true QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E")
                + HelpExampleCli("issuequalifiertoken", "\"TOKEN_NAME/SUB_QUALIFIER\" 1000 \"myaddress\" \"changeaddress\"")
                + HelpExampleCli("issuequalifiertoken", "\"#TOKEN_NAME\"")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();

    if (!IsTokenNameAQualifier(tokenName)) {
        std::string temp = QUALIFIER_CHAR + tokenName;
        tokenName = temp;
    }

    KnownTokenType tokenType;
    std::string tokenError = "";
    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    if (tokenType != KnownTokenType::QUALIFIER && tokenType != KnownTokenType::SUB_QUALIFIER) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType) +  " Please use a valid qualifier name" );
    }

    CAmount nAmount = COIN;
    if (request.params.size() > 1)
        nAmount = AmountFromValue(request.params[1]);

    if (nAmount < QUALIFIER_TOKEN_MIN_AMOUNT || nAmount > QUALIFIER_TOKEN_MAX_AMOUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameters for issuing a qualifier token. Amount must be between 1 and 10"));
    }

    std::string address = "";
    if (request.params.size() > 2)
        address = request.params[2].get_str();

    if (!address.empty()) {
        CTxDestination destination = DecodeDestination(address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
        }
    } else {
        // Create a new address
        std::string strAccount;

        if (!pwallet->IsLocked()) {
            pwallet->TopUpKeyPool();
        }

        // Generate a new key that is added to wallet
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        }
        CKeyID keyID = newKey.GetID();

        pwallet->SetAddressBook(keyID, strAccount, "receive");

        address = EncodeDestination(keyID);
    }

    std::string change_address = "";
    if (request.params.size() > 3) {
        change_address = request.params[3].get_str();
        if (!change_address.empty()) {
            CTxDestination destination = DecodeDestination(change_address);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   std::string("Invalid Change Address: Invalid Yona address: ") + change_address);
            }
        }
    }

    int units = 0;
    bool reissuable = false;

    bool has_ipfs = false;
    if (request.params.size() > 4)
        has_ipfs = request.params[4].get_bool();

    // Check the ipfs
    std::string ipfs_hash = "";
    bool fMessageCheck = false;
    if (request.params.size() > 5 && has_ipfs) {
        fMessageCheck = true;
        ipfs_hash = request.params[5].get_str();
    }

    // Reissues don't have an expire time
    int64_t expireTime = 0;

    // Check the message data
    if (fMessageCheck)
        CheckIPFSTxidMessage(ipfs_hash, expireTime);

    CNewToken token(tokenName, nAmount, units, reissuable ? 1 : 0, has_ipfs ? 1 : 0, DecodeTokenData(ipfs_hash));

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(change_address);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, token, address, error, transaction, reservekey, nRequiredFee))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue issuerestrictedtoken(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 4 || request.params.size() > 10)
        throw std::runtime_error(
                "issuerestrictedtoken \"token_name\" qty \"verifier\" \"to_address\" \"( change_address )\" (units) ( reissuable ) ( has_ipfs ) \"( ipfs_hash )\"\n"
                + RestrictedActivationWarning() +
                "\nIssue a restricted token.\n"
                "Restricted token names must not conflict with any existing restricted token.\n"
                "Restricted tokens have units set to 0.\n"
                "Reissuable is true/false for whether additional token quantity can be created and if the verifier string can be changed\n"

                "\nArguments:\n"
                "1. \"token_name\"            (string, required) a unique name, starts with '$', if '$' is not there it will be added automatically\n"
                "2. \"qty\"                   (numeric, required) the quantity of the token to be issued\n"
                "3. \"verifier\"              (string, required) the verifier string that will be evaluated when restricted token transfers are made\n"
                "4. \"to_address\"            (string, required) address token will be sent to, this address must meet the verifier string requirements\n"
                "5. \"change_address\"        (string, optional, default=\"\") address that the yona change will be sent to, if it is empty, change address will be generated for you\n"
                "6. \"units\"                 (integer, optional, default=0, min=0, max=8) the number of decimals precision for the token (0 for whole units (\"1\"), 8 for max precision (\"1.00000000\")\n"
                "7. \"reissuable\"            (boolean, optional, default=true (false for unique tokens)) whether future reissuance is allowed\n"
                "8. \"has_ipfs\"              (boolean, optional, default=false) whether an ipfs hash or txid hash is going to be added to the token\n"
                "9. \"ipfs_hash\"             (string, optional but required if has_ipfs = 1) an ipfs hash or a txid hash once messaging is activated\n"
                "10. \"message\"             (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("issuerestrictedtoken", "\"$TOKEN_NAME\" 1000 \"#KYC & !#AML\" \"myaddress\"")
                + HelpExampleCli("issuerestrictedtoken", "\"$TOKEN_NAME\" 1000 \"#KYC & !#AML\" \"myaddress\"")
                + HelpExampleCli("issuerestrictedtoken", "\"$TOKEN_NAME\" 1000 \"#KYC & !#AML\" \"myaddress\" \"changeaddress\" 5")
                + HelpExampleCli("issuerestrictedtoken", "\"$TOKEN_NAME\" 1000 \"#KYC & !#AML\" \"myaddress\" \"changeaddress\" 8 true")
                + HelpExampleCli("issuerestrictedtoken", "\"$TOKEN_NAME\" 1000 \"#KYC & !#AML\" \"myaddress\" \"changeaddress\" 0 false true QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";

    if (!IsTokenNameAnRestricted(tokenName))
    {
        std::string temp = RESTRICTED_CHAR + tokenName;
        tokenName = temp;
    }

    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    // Check for unsupported token types, only restricted tokens are allowed for this rpc call
    if (tokenType != KnownTokenType::RESTRICTED) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    // Get the remaining three required parameters
    CAmount nAmount = AmountFromValue(request.params[1]);
    std::string verifier_string = request.params[2].get_str();
    std::string to_address = request.params[3].get_str();

    // Validate the address
    CTxDestination destination = DecodeDestination(to_address);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + to_address);
    }


    std::string verifierStripped = GetStrippedVerifierString(verifier_string);

    // Validate the verifier string with the given to_address
    std::string strError = "";
    if (!ContextualCheckVerifierString(ptokens, verifierStripped, to_address, strError))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

    // Get the change address if one was given
    std::string change_address = "";
    if (request.params.size() > 4)
        change_address = request.params[4].get_str();
    if (!change_address.empty()) {
        CTxDestination destination = DecodeDestination(change_address);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               std::string("Invalid Change Address: Invalid Yona address: ") + change_address);
        }
    }

    int units = MIN_UNIT;
    if (request.params.size() > 5)
        units = request.params[5].get_int();

    if (units < MIN_UNIT || units > MAX_UNIT)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Units must be between 0 and 8");

    // Restristed tokens are reissuable by default
    bool reissuable = true;
    if (request.params.size() > 6)
        reissuable = request.params[6].get_bool();

    bool has_ipfs = false;
    if (request.params.size() > 7)
        has_ipfs = request.params[7].get_bool();

    // Check the ipfs
    std::string ipfs_hash = "";
    bool fMessageCheck = false;
    if (request.params.size() > 8 && has_ipfs) {
        fMessageCheck = true;
        ipfs_hash = request.params[8].get_str();
    }

    std::string message;
    if (request.params.size() > 9) {
        message = request.params[9].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    // issues don't have an expire time
    int64_t expireTime = 0;

    // Check the message data
    if (fMessageCheck)
        CheckIPFSTxidMessage(ipfs_hash, expireTime);

    CNewToken token(tokenName, nAmount, units, reissuable ? 1 : 0, has_ipfs ? 1 : 0, DecodeTokenData(ipfs_hash));

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(change_address);

    // Create the Transaction
    if (!CreateTokenTransaction(pwallet, crtl, token, to_address, error, transaction, reservekey, nRequiredFee, message, &verifierStripped))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue reissuerestrictedtoken(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() < 3 || request.params.size() > 10)
        throw std::runtime_error(
                "reissuerestrictedtoken \"token_name\" qty to_address ( change_verifier ) ( \"new_verifier\" ) \"( change_address )\" ( new_units ) ( reissuable ) \"( new_ipfs )\"\n"
                + RestrictedActivationWarning() +
                "\nReissue an already created restricted token\n"
                "Reissuable is true/false for whether additional token quantity can be created and if the verifier string can be changed\n"

                "\nArguments:\n"
                "1. \"token_name\"            (string, required) a unique name, starts with '$'\n"
                "2. \"qty\"                   (numeric, required) the additional quantity of the token to be issued\n"
                "3. \"to_address\"            (string, required) address token will be sent to, this address must meet the verifier string requirements\n"
                "4. \"change_verifier\"       (boolean, optional, default=false) if the verifier string will get changed\n"
                "5. \"new_verifier\"          (string, optional, default=\"\") the new verifier string that will be evaluated when restricted token transfers are made\n"
                "6. \"change_address\"        (string, optional, default=\"\") address that the yona change will be sent to, if it is empty, change address will be generated for you\n"
                "7. \"new_units\"             (numeric, optional, default=-1) the new units that will be associated with the token\n"
                "8. \"reissuable\"            (boolean, optional, default=true (false for unique tokens)) whether future reissuance is allowed\n"
                "9. \"new_ipfs\"              (string, optional, default=\"\") whether to update the current ipfs hash or txid once messaging is active\n"
                "10. \"message\"              (string, optional, default="") Message attached to transaction. \n"

                "\nResult:\n"
                "\"txid\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("reissuerestrictedtoken", "\"$TOKEN_NAME\" 1000  \"myaddress\" true \"KYC & !AML\"")
                + HelpExampleCli("reissuerestrictedtoken", "\"$TOKEN_NAME\" 1000  \"myaddress\" true \"KYC & !AML\" ")
                + HelpExampleCli("reissuerestrictedtoken", "\"$TOKEN_NAME\" 1000  \"myaddress\" true \"KYC & !AML\" \"changeaddress\"")
                + HelpExampleCli("reissuerestrictedtoken", "\"$TOKEN_NAME\" 1000  \"myaddress\" true \"KYC & !AML\" \"changeaddress\" -1 true")
                + HelpExampleCli("reissuerestrictedtoken", "\"$TOKEN_NAME\" 1000  \"myaddress\" false \"\" \"changeaddress\" -1 false QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Check token name and infer tokenType
    std::string tokenName = request.params[0].get_str();
    KnownTokenType tokenType;
    std::string tokenError = "";

    if (!IsTokenNameAnRestricted(tokenName))
    {
        std::string temp = RESTRICTED_CHAR + tokenName;
        tokenName = temp;
    }

    if (!IsTokenNameValid(tokenName, tokenType, tokenError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid token name: ") + tokenName + std::string("\nError: ") + tokenError);
    }

    // Check for unsupported token types, only restricted tokens are allowed for this rpc call
    if (tokenType != KnownTokenType::RESTRICTED) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Unsupported token type: ") + KnownTokenTypeToString(tokenType));
    }

    CAmount nAmount = AmountFromValue(request.params[1]);
    std::string to_address = request.params[2].get_str();

    CTxDestination to_dest = DecodeDestination(to_address);
    if (!IsValidDestination(to_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + to_address);
    }

    bool fChangeVerifier = false;
    if (request.params.size() > 3)
        fChangeVerifier = request.params[3].get_bool();

    std::string verifier_string = "";
    if (request.params.size() > 4)
        verifier_string = request.params[4].get_str();

    std::string change_address = "";
    if (request.params.size() > 5) {
        change_address = request.params[5].get_str();
        CTxDestination change_dest = DecodeDestination(change_address);
        if (!IsValidDestination(change_dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               std::string("Invalid Change Address: Invalid Yona address: ") + change_address);
        }
    }

    int newUnits = -1;
    if (request.params.size() > 6)
        newUnits = request.params[6].get_int();

    if (newUnits < -1 || newUnits > MAX_UNIT)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Units must be between -1 and 8, -1 means don't change the current units");

    bool reissuable = true;
    if (request.params.size() > 7)
        reissuable = request.params[7].get_bool();

    std::string new_ipfs_data = "";
    bool fMessageCheck = false;

    if (request.params.size() > 8) {
        fMessageCheck = true;
        new_ipfs_data = request.params[8].get_str();
    }

    std::string message;
    if (request.params.size() > 9) {
        message = request.params[9].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    // Reissues don't have an expire time
    int64_t expireTime = 0;

    // Check the message data
    if (fMessageCheck)
        CheckIPFSTxidMessage(new_ipfs_data, expireTime);

    CReissueToken reissueToken(tokenName, nAmount, newUnits, reissuable ? 1 : 0, DecodeTokenData(new_ipfs_data));

    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;
    std::pair<int, std::string> error;

    CCoinControl crtl;
    crtl.destChange = DecodeDestination(change_address);

    std::string verifierStripped = GetStrippedVerifierString(verifier_string);

    // Create the Transaction
    if (!CreateReissueTokenTransaction(pwallet, crtl, reissueToken, to_address, error, transaction, reservekey, nRequiredFee, message, fChangeVerifier ? &verifierStripped : nullptr))
        throw JSONRPCError(error.first, error.second);

    std::string strError = "";
    if (!ContextualCheckReissueToken(ptokens, reissueToken, strError, *transaction.tx.get()))
        throw JSONRPCError(RPC_INVALID_REQUEST, strError);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}

UniValue transferqualifier(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 3 || request.params.size() > 7)
        throw std::runtime_error(
                "transferqualifier \"qualifier_name\" qty \"to_address\" (\"change_address\") (\"token_message\") (\"token_message\") (expire_time) \n"
                + RestrictedActivationWarning() +
                "\nTransfer a qualifier token owned by this wallet to the given address"

                "\nArguments:\n"
                "1. \"qualifier_name\"           (string, required) name of qualifier token\n"
                "2. \"qty\"                      (numeric, required) number of tokens you want to send to the address\n"
                "3. \"to_address\"               (string, required) address to send the token to\n"
                "4. \"change_address\"           (string, optional, default = \"\") the transaction change will be sent to this address\n"
                "5. \"message\"                  (string, optional, default="") Message attached to transaction. \n"
                "6. \"token_message\"            (string, optional) Once messaging is voted in ipfs hash or txid hash to send along with the transfer\n"
                "7. \"expire_time\"              (numeric, optional) UTC timestamp of when the message expires\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transferqualifier", "\"#QUALIFIER\" 20 \"to_address\" \"\" \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 15863654")
                + HelpExampleCli("transferqualifier", "\"#QUALIFIER\" 20 \"to_address\" \"change_address\" \"message\" \"QmTqu3Lk3gmTsQVtjU7rYYM37EAW4xNmbuEAp2Mjr4AV7E\" 15863654")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string token_name = request.params[0].get_str();

    if (!IsTokenNameAQualifier(token_name))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Only use this rpc call to send Qualifier tokens. Qualifier tokens start with the character '#'");

    CAmount nAmount = AmountFromValue(request.params[1]);

    std::string to_address = request.params[2].get_str();
    CTxDestination to_dest = DecodeDestination(to_address);
    if (!IsValidDestination(to_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + to_address);
    }

    std::string change_address = "";
    if(request.params.size() > 3) {
        change_address = request.params[3].get_str();

        CTxDestination change_dest = DecodeDestination(change_address);
        if (!IsValidDestination(change_dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + change_address);
        }
    }

    if (request.params.size() > 4) {
        if (!AreMessagesDeployed()) {
            throw JSONRPCError(RPC_INVALID_PARAMS, std::string("Unable to send messages until Messaging messaging is enabled"));
        }
    }

    std::string message;
    if (request.params.size() > 4) {
        message = request.params[4].get_str();

        if (message.length() > MAX_MESSAGE_LEN)
            throw JSONRPCError(RPC_TYPE_ERROR,
                strprintf("Transaction message max length is %s", MAX_MESSAGE_LEN));
    }

    bool fMessageCheck = false;
    std::string token_message = "";
    if (request.params.size() > 5) {
        fMessageCheck = true;
        token_message = request.params[5].get_str();
    }

    int64_t expireTime = 0;
    if (!token_message.empty()) {
        if (request.params.size() > 6) {
            expireTime = request.params[6].get_int64();
        }
    }

    if (fMessageCheck)
        CheckIPFSTxidMessage(token_message, expireTime);

    std::pair<int, std::string> error;
    std::vector< std::pair<CTokenTransfer, std::string> >vTransfers;

    CTokenTransfer transfer(token_name, nAmount, 0, DecodeTokenData(token_message), expireTime);

    vTransfers.emplace_back(std::make_pair(transfer, to_address));
    CReserveKey reservekey(pwallet);
    CWalletTx transaction;
    CAmount nRequiredFee;

    CCoinControl ctrl;
    ctrl.destChange = DecodeDestination(change_address);

    // Create the Transaction
    if (!CreateTransferTokenTransaction(pwallet, ctrl, vTransfers, "", error, transaction, reservekey, nRequiredFee, message))
        throw JSONRPCError(error.first, error.second);

    // Send the Transaction to the network
    std::string txid;
    if (!SendTokenTransaction(pwallet, transaction, reservekey, error, txid))
        throw JSONRPCError(error.first, error.second);

    // Display the transaction id
    UniValue result(UniValue::VARR);
    result.push_back(txid);
    return result;
}
#endif

UniValue isvalidverifierstring(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreRestrictedTokensDeployed() || request.params.size() != 1)
        throw std::runtime_error(
                "isvalidverifierstring verifier_string\n"
                + RestrictedActivationWarning() +
                "\nChecks to see if the given verifier string is valid\n"

                "\nArguments:\n"
                "1. \"verifier_string\"   (string, required) the verifier string to check\n"

                "\nResult:\n"
                "\"xxxxxxx\", (string) If the verifier string is valid, and the reason\n"

                "\nExamples:\n"
                + HelpExampleCli("isvalidverifierstring", "\"verifier_string\"")
                + HelpExampleRpc("isvalidverifierstring", "\"verifier_string\"")
        );

    ObserveSafeMode();
    LOCK(cs_main);

    if (!ptokens)
        throw JSONRPCError(RPC_DATABASE_ERROR, "Token cache not available");

    std::string verifier_string = request.params[0].get_str();

    std::string stripped_verifier_string = GetStrippedVerifierString(verifier_string);

    std::string strError;
    if (!ContextualCheckVerifierString(ptokens, stripped_verifier_string, "", strError)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);
    }

    return _("Valid Verifier");
}

UniValue getsnapshot(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 2)
        throw std::runtime_error(
                "getsnapshot \"token_name\" block_height\n"
                + TokenActivationWarning() +
                "\nReturns details for the token snapshot, at the specified height\n"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) the name of the token\n"
                "2. block_height                 (int, required) the block height of the snapshot\n"

                "\nResult:\n"
                "{\n"
                "  name: (string),\n"
                "  height: (number),\n"
                "  owners: [\n"
                "    {\n"
                "      address: (string),\n"
                "      amount_owned: (number),\n"
                "    }\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleRpc("getsnapshot", "\"TOKEN_NAME\" 28546")
        );


    std::string token_name = request.params[0].get_str();
    int block_height = request.params[1].get_int();

    if (!pTokenSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Token Snapshot database is not setup. Please restart wallet to try again"));

    LOCK(cs_main);
    UniValue result (UniValue::VOBJ);

    CTokenSnapshotDBEntry snapshotDbEntry;

    if (pTokenSnapshotDb->RetrieveOwnershipSnapshot(token_name, block_height, snapshotDbEntry)) {
        result.push_back(Pair("name", snapshotDbEntry.tokenName));
        result.push_back(Pair("height", snapshotDbEntry.height));

        UniValue entries(UniValue::VARR);
        for (auto const & ownerAndAmt : snapshotDbEntry.ownersAndAmounts) {
            UniValue entry(UniValue::VOBJ);

            entry.push_back(Pair("address", ownerAndAmt.first));
            entry.push_back(Pair("amount_owned", UnitValueFromAmount(ownerAndAmt.second, snapshotDbEntry.tokenName)));

            entries.push_back(entry);
        }

        result.push_back(Pair("owners", entries));

        return result;
    }

    return NullUniValue;
}

UniValue purgesnapshot(const JSONRPCRequest& request)
{
    if (request.fHelp || !AreTokensDeployed() || request.params.size() < 2)
        throw std::runtime_error(
                "purgesnapshot \"token_name\" block_height\n"
                + TokenActivationWarning() +
                "\nRemoves details for the token snapshot, at the specified height\n"

                "\nArguments:\n"
                "1. \"token_name\"               (string, required) the name of the token\n"
                "2. block_height                 (int, required) the block height of the snapshot\n"

                "\nResult:\n"
                "{\n"
                "  name: (string),\n"
                "  height: (number),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("purgesnapshot", "\"TOKEN_NAME\" 28546")
                + HelpExampleRpc("purgesnapshot", "\"TOKEN_NAME\" 28546")
        );


    std::string token_name = request.params[0].get_str();
    int block_height = 0;
    if (request.params.size() > 1) {
        block_height = request.params[2].get_int();
    }

    if (!pTokenSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Token Snapshot database is not setup. Please restart wallet to try again"));

    LOCK(cs_main);
    UniValue result (UniValue::VOBJ);

    if (pTokenSnapshotDb->RemoveOwnershipSnapshot(token_name, block_height)) {
        result.push_back(Pair("name", token_name));
        if (block_height > 0) {
            result.push_back(Pair("height", block_height));
        }

        return result;
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category    name                          actor (function)             argNames
  //  ----------- ------------------------      -----------------------      ----------
#ifdef ENABLE_WALLET
    { "tokens",   "issue",                      &issue,                      {"token_name","qty","to_address","change_address","units","reissuable","has_ipfs","ipfs_hash"} },
    { "tokens",   "issueunique",                &issueunique,                {"root_name", "token_tags", "ipfs_hashes", "to_address", "change_address"}},
    { "tokens",   "registerusername",           &registerusername,           {"username","to_address"} },
    { "tokens",   "getusernameaddress",         &getusernameaddress,         {"username"} },
    { "tokens",   "listmytokens",               &listmytokens,               {"token", "verbose", "count", "start", "confs"}},
    { "tokens",   "listmylockedtokens",         &listmylockedtokens,         {"token", "verbose", "count", "start"}},
#endif
    { "tokens",   "listtokenbalancesbyaddress", &listtokenbalancesbyaddress, {"address", "onlytotal", "count", "start"} },
    { "tokens",   "gettokendata",               &gettokendata,               {"token_name"}},
    { "tokens",   "listaddressesbytoken",       &listaddressesbytoken,       {"token_name", "onlytotal", "count", "start"}},
#ifdef ENABLE_WALLET
    { "tokens",   "transferfromaddress",        &transferfromaddress,        {"token_name", "from_address", "qty", "to_address", "timelock", "message", "token_message", "expire_time", "yona_change_address", "token_change_address"}},
    { "tokens",   "transferfromaddresses",      &transferfromaddresses,      {"token_name", "from_addresses", "qty", "to_address", "timelock", "message", "token_message", "expire_time", "yona_change_address", "token_change_address"}},
    { "tokens",   "transfer",                   &transfer,                   {"token_name", "qty", "to_address", "timelock", "message", "token_message", "expire_time", "change_address", "token_change_address"}},
    { "tokens",   "reissue",                    &reissue,                    {"token_name", "qty", "to_address", "change_address", "reissuable", "new_units", "new_ipfs"}},
    { "tokens",   "sweep",                      &sweep,                      {"privkey", "token_name"}},
#endif
    { "tokens",   "listtokens",                 &listtokens,                 {"token", "verbose", "count", "start"}},
    { "tokens",   "getcacheinfo",               &getcacheinfo,               {}},

#ifdef ENABLE_WALLET
    { "restricted tokens",   "transferqualifier",          &transferqualifier,          {"qualifier_name", "qty", "to_address", "change_address", "message", "token_message", "expire_time"}},
    { "restricted tokens",   "issuerestrictedtoken",       &issuerestrictedtoken,       {"token_name","qty","verifier","to_address","change_address","units","reissuable","has_ipfs","ipfs_hash"} },
    { "restricted tokens",   "issuequalifiertoken",        &issuequalifiertoken,        {"token_name","qty","to_address","change_address","has_ipfs","ipfs_hash"} },
    { "restricted tokens",   "reissuerestrictedtoken",     &reissuerestrictedtoken,     {"token_name", "qty", "change_verifier", "new_verifier", "to_address", "change_address", "new_units", "reissuable", "new_ipfs"}},
    { "restricted tokens",   "addtagtoaddress",            &addtagtoaddress,            {"tag_name", "to_address", "change_address", "token_data"}},
    { "restricted tokens",   "removetagfromaddress",       &removetagfromaddress,       {"tag_name", "to_address", "change_address", "token_data"}},
    { "restricted tokens",   "freezeaddress",              &freezeaddress,              {"token_name", "address", "change_address", "token_data"}},
    { "restricted tokens",   "unfreezeaddress",            &unfreezeaddress,            {"token_name", "address", "change_address", "token_data"}},
    { "restricted tokens",   "freezerestrictedtoken",      &freezerestrictedtoken,      {"token_name", "change_address", "token_data"}},
    { "restricted tokens",   "unfreezerestrictedtoken",    &unfreezerestrictedtoken,    {"token_name", "change_address", "token_data"}},
#endif
    { "restricted tokens",   "listaddressesfortag",        &listaddressesfortag,        {"tag_name"}},
    { "restricted tokens",   "listtagsforaddress",         &listtagsforaddress,         {"address"}},
    { "restricted tokens",   "listaddressrestrictions",    &listaddressrestrictions,    {"address"}},
    { "restricted tokens",   "listglobalrestrictions",     &listglobalrestrictions,     {}},
    { "restricted tokens",   "getverifierstring",          &getverifierstring,          {"restricted_name"}},
    { "restricted tokens",   "checkaddresstag",            &checkaddresstag,            {"address", "tag_name"}},
    { "restricted tokens",   "checkaddressrestriction",    &checkaddressrestriction,    {"address", "restricted_name"}},
    { "restricted tokens",   "checkglobalrestriction",     &checkglobalrestriction,     {"restricted_name"}},
    { "restricted tokens",   "isvalidverifierstring",      &isvalidverifierstring,      {"verifier_string"}},

    { "tokens",   "getsnapshot",                &getsnapshot,                {"token_name", "block_height"}},
    { "tokens",   "purgesnapshot",              &purgesnapshot,              {"token_name", "block_height"}},
};

void RegisterTokenRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
