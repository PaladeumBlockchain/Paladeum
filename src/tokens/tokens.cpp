// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <regex>
#include <script/script.h>
#include <version.h>
#include <streams.h>
#include <primitives/transaction.h>
#include <iostream>
#include <script/standard.h>
#include <util.h>
#include <chainparams.h>
#include <base58.h>
#include <validation.h>
#include <txmempool.h>
#include <tinyformat.h>
#include <wallet/wallet.h>
#include <boost/algorithm/string.hpp>
#include <consensus/validation.h>
#include <rpc/protocol.h>
#include <net.h>
#include "tokens.h"
#include "tokendb.h"
#include "tokentypes.h"
#include "protocol.h"
#include "wallet/coincontrol.h"
#include "utilmoneystr.h"
#include "coins.h"
#include "wallet/wallet.h"
#include "LibBoolEE.h"

#define SIX_MONTHS 15780000 // Six months worth of seconds

#define OFFSET_THREE 3
#define OFFSET_FOUR 4
#define OFFSET_TWENTY_THREE 23


std::map<uint256, std::string> mapReissuedTx;
std::map<std::string, uint256> mapReissuedTokens;

// excluding owner tag ('!')
static const auto MAX_NAME_LENGTH = 31;
static const auto MAX_CHANNEL_NAME_LENGTH = 12;

// min lengths are expressed by quantifiers
static const std::regex ROOT_NAME_CHARACTERS("^[A-Z0-9._]{3,}$");
static const std::regex SUB_NAME_CHARACTERS("^[A-Z0-9._]+$");
static const std::regex UNIQUE_TAG_CHARACTERS("^[-A-Za-z0-9@$%&*()[\\]{}_.?:]+$");
static const std::regex MSG_CHANNEL_TAG_CHARACTERS("^[A-Za-z0-9_]+$");
static const std::regex VOTE_TAG_CHARACTERS("^[A-Z0-9._]+$");

// Restricted tokens
static const std::regex QUALIFIER_NAME_CHARACTERS("#[A-Z0-9._]{3,}$");
static const std::regex SUB_QUALIFIER_NAME_CHARACTERS("#[A-Z0-9._]+$");
static const std::regex RESTRICTED_NAME_CHARACTERS("\\$[A-Z0-9._]{3,}$");

static const std::regex DOUBLE_PUNCTUATION("^.*[._]{2,}.*$");
static const std::regex LEADING_PUNCTUATION("^[._].*$");
static const std::regex TRAILING_PUNCTUATION("^.*[._]$");
static const std::regex QUALIFIER_LEADING_PUNCTUATION("^[#\\$][._].*$"); // Used for qualifier tokens, and restricted token only

static const std::string SUB_NAME_DELIMITER = "/";
static const std::string UNIQUE_TAG_DELIMITER = "#";
static const std::string MSG_CHANNEL_TAG_DELIMITER = "~";
static const std::string VOTE_TAG_DELIMITER = "^";
static const std::string RESTRICTED_TAG_DELIMITER = "$";

static const std::regex UNIQUE_INDICATOR(R"(^[^^~#!]+#[^~#!\/]+$)");
static const std::regex MSG_CHANNEL_INDICATOR(R"(^[^^~#!]+~[^~#!\/]+$)");
static const std::regex OWNER_INDICATOR(R"(^[^^~#!]+!$)");
static const std::regex VOTE_INDICATOR(R"(^[^^~#!]+\^[^~#!\/]+$)");

static const std::regex QUALIFIER_INDICATOR("^[#][A-Z0-9._]{3,}$"); // Starts with #
static const std::regex SUB_QUALIFIER_INDICATOR("^#[A-Z0-9._]+\\/#[A-Z0-9._]+$"); // Starts with #
static const std::regex RESTRICTED_INDICATOR("^[\\$][A-Z0-9._]{3,}$"); // Starts with $

static const std::regex YONA_NAMES("^YONA$|^YONA$|^YONACOIN$");

bool IsRootNameValid(const std::string& name)
{
    return std::regex_match(name, ROOT_NAME_CHARACTERS)
        && !std::regex_match(name, DOUBLE_PUNCTUATION)
        && !std::regex_match(name, LEADING_PUNCTUATION)
        && !std::regex_match(name, TRAILING_PUNCTUATION)
        && !std::regex_match(name, YONA_NAMES);
}

bool IsQualifierNameValid(const std::string& name)
{
    return std::regex_match(name, QUALIFIER_NAME_CHARACTERS)
           && !std::regex_match(name, DOUBLE_PUNCTUATION)
           && !std::regex_match(name, QUALIFIER_LEADING_PUNCTUATION)
           && !std::regex_match(name, TRAILING_PUNCTUATION)
           && !std::regex_match(name, YONA_NAMES);
}

bool IsRestrictedNameValid(const std::string& name)
{
    return std::regex_match(name, RESTRICTED_NAME_CHARACTERS)
           && !std::regex_match(name, DOUBLE_PUNCTUATION)
           && !std::regex_match(name, LEADING_PUNCTUATION)
           && !std::regex_match(name, TRAILING_PUNCTUATION)
           && !std::regex_match(name, YONA_NAMES);
}

bool IsSubQualifierNameValid(const std::string& name)
{
    return std::regex_match(name, SUB_QUALIFIER_NAME_CHARACTERS)
           && !std::regex_match(name, DOUBLE_PUNCTUATION)
           && !std::regex_match(name, LEADING_PUNCTUATION)
           && !std::regex_match(name, TRAILING_PUNCTUATION);
}

bool IsSubNameValid(const std::string& name)
{
    return std::regex_match(name, SUB_NAME_CHARACTERS)
        && !std::regex_match(name, DOUBLE_PUNCTUATION)
        && !std::regex_match(name, LEADING_PUNCTUATION)
        && !std::regex_match(name, TRAILING_PUNCTUATION);
}

bool IsUniqueTagValid(const std::string& tag)
{
    return std::regex_match(tag, UNIQUE_TAG_CHARACTERS);
}

bool IsVoteTagValid(const std::string& tag)
{
    return std::regex_match(tag, VOTE_TAG_CHARACTERS);
}

bool IsMsgChannelTagValid(const std::string &tag)
{
    return std::regex_match(tag, MSG_CHANNEL_TAG_CHARACTERS)
        && !std::regex_match(tag, DOUBLE_PUNCTUATION)
        && !std::regex_match(tag, LEADING_PUNCTUATION)
        && !std::regex_match(tag, TRAILING_PUNCTUATION);
}

bool IsNameValidBeforeTag(const std::string& name)
{
    std::vector<std::string> parts;
    boost::split(parts, name, boost::is_any_of(SUB_NAME_DELIMITER));

    if (!IsRootNameValid(parts.front())) return false;

    if (parts.size() > 1)
    {
        for (unsigned long i = 1; i < parts.size(); i++)
        {
            if (!IsSubNameValid(parts[i])) return false;
        }
    }

    return true;
}

bool IsQualifierNameValidBeforeTag(const std::string& name)
{
    std::vector<std::string> parts;
    boost::split(parts, name, boost::is_any_of(SUB_NAME_DELIMITER));

    if (!IsQualifierNameValid(parts.front())) return false;

    // Qualifiers can only have one sub qualifier under it
    if (parts.size() > 2) {
        return false;
    }

    if (parts.size() > 1)
    {

        for (unsigned long i = 1; i < parts.size(); i++)
        {
            if (!IsSubQualifierNameValid(parts[i])) return false;
        }
    }

    return true;
}

bool IsTokenNameASubtoken(const std::string& name)
{
    std::vector<std::string> parts;
    boost::split(parts, name, boost::is_any_of(SUB_NAME_DELIMITER));

    if (!IsRootNameValid(parts.front())) return false;

    return parts.size() > 1;
}

bool IsTokenNameASubQualifier(const std::string& name)
{
    std::vector<std::string> parts;
    boost::split(parts, name, boost::is_any_of(SUB_NAME_DELIMITER));

    if (!IsQualifierNameValid(parts.front())) return false;

    return parts.size() > 1;
}


bool IsTokenNameValid(const std::string& name, KnownTokenType& tokenType, std::string& error)
{
    // Do a max length check first to stop the possibility of a stack exhaustion.
    // We check for a value that is larger than the max token name
    if (name.length() > 40)
        return false;

    tokenType = KnownTokenType::INVALID;
    if (std::regex_match(name, UNIQUE_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::UNIQUE, name, error);
        if (ret)
            tokenType = KnownTokenType::UNIQUE;

        return ret;
    }
    else if (std::regex_match(name, MSG_CHANNEL_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::MSGCHANNEL, name, error);
        if (ret)
            tokenType = KnownTokenType::MSGCHANNEL;

        return ret;
    }
    else if (std::regex_match(name, OWNER_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::OWNER, name, error);
        if (ret)
            tokenType = KnownTokenType::OWNER;

        return ret;
    }
    else if (std::regex_match(name, VOTE_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::VOTE, name, error);
        if (ret)
            tokenType = KnownTokenType::VOTE;

        return ret;
    }
    else if (std::regex_match(name, QUALIFIER_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::QUALIFIER, name, error);
        if (ret) {
            if (IsTokenNameASubQualifier(name))
                tokenType = KnownTokenType::SUB_QUALIFIER;
            else
                tokenType = KnownTokenType::QUALIFIER;
        }

        return ret;
    }
    else if (std::regex_match(name, SUB_QUALIFIER_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::SUB_QUALIFIER, name, error);
        if (ret) {
            if (IsTokenNameASubQualifier(name))
                tokenType = KnownTokenType::SUB_QUALIFIER;
        }

        return ret;
    }
    else if (std::regex_match(name, RESTRICTED_INDICATOR))
    {
        bool ret = IsTypeCheckNameValid(KnownTokenType::RESTRICTED, name, error);
        if (ret)
            tokenType = KnownTokenType::RESTRICTED;

        return ret;
    }
    else
    {
        auto type = IsTokenNameASubtoken(name) ? KnownTokenType::SUB : KnownTokenType::ROOT;
        bool ret = IsTypeCheckNameValid(type, name, error);
        if (ret)
            tokenType = type;

        return ret;
    }
}

bool IsTokenNameValid(const std::string& name)
{
    KnownTokenType _tokenType;
    std::string _error;
    return IsTokenNameValid(name, _tokenType, _error);
}

bool IsTokenNameValid(const std::string& name, KnownTokenType& tokenType)
{
    std::string _error;
    return IsTokenNameValid(name, tokenType, _error);
}

bool IsTokenNameARoot(const std::string& name)
{
    KnownTokenType type;
    return IsTokenNameValid(name, type) && type == KnownTokenType::ROOT;
}

bool IsTokenNameAnOwner(const std::string& name)
{
    return IsTokenNameValid(name) && std::regex_match(name, OWNER_INDICATOR);
}

bool IsTokenNameAnRestricted(const std::string& name)
{
    return IsTokenNameValid(name) && std::regex_match(name, RESTRICTED_INDICATOR);
}

bool IsTokenNameAQualifier(const std::string& name, bool fOnlyQualifiers)
{
    if (fOnlyQualifiers) {
        return IsTokenNameValid(name) && std::regex_match(name, QUALIFIER_INDICATOR);
    }

    return IsTokenNameValid(name) && (std::regex_match(name, QUALIFIER_INDICATOR) || std::regex_match(name, SUB_QUALIFIER_INDICATOR));
}

bool IsTokenNameAnMsgChannel(const std::string& name)
{
    return IsTokenNameValid(name) && std::regex_match(name, MSG_CHANNEL_INDICATOR);
}

// TODO get the string translated below
bool IsTypeCheckNameValid(const KnownTokenType type, const std::string& name, std::string& error)
{
    if (type == KnownTokenType::UNIQUE) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        std::vector<std::string> parts;
        boost::split(parts, name, boost::is_any_of(UNIQUE_TAG_DELIMITER));
        bool valid = IsNameValidBeforeTag(parts.front()) && IsUniqueTagValid(parts.back());
        if (!valid) { error = "Unique name contains invalid characters (Valid characters are: A-Z a-z 0-9 @ $ % & * ( ) [ ] { } _ . ? : -)";  return false; }
        return true;
    } else if (type == KnownTokenType::MSGCHANNEL) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        std::vector<std::string> parts;
        boost::split(parts, name, boost::is_any_of(MSG_CHANNEL_TAG_DELIMITER));
        bool valid = IsNameValidBeforeTag(parts.front()) && IsMsgChannelTagValid(parts.back());
        if (parts.back().size() > MAX_CHANNEL_NAME_LENGTH) { error = "Channel name is greater than max length of " + std::to_string(MAX_CHANNEL_NAME_LENGTH); return false; }
        if (!valid) { error = "Message Channel name contains invalid characters (Valid characters are: A-Z 0-9 _ .) (special characters can't be the first or last characters)";  return false; }
        return true;
    } else if (type == KnownTokenType::OWNER) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        bool valid = IsNameValidBeforeTag(name.substr(0, name.size() - 1));
        if (!valid) { error = "Owner name contains invalid characters (Valid characters are: A-Z 0-9 _ .) (special characters can't be the first or last characters)";  return false; }
        return true;
    } else if (type == KnownTokenType::VOTE) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        std::vector<std::string> parts;
        boost::split(parts, name, boost::is_any_of(VOTE_TAG_DELIMITER));
        bool valid = IsNameValidBeforeTag(parts.front()) && IsVoteTagValid(parts.back());
        if (!valid) { error = "Vote name contains invalid characters (Valid characters are: A-Z 0-9 _ .) (special characters can't be the first or last characters)";  return false; }
        return true;
    } else if (type == KnownTokenType::QUALIFIER || type == KnownTokenType::SUB_QUALIFIER) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        bool valid = IsQualifierNameValidBeforeTag(name);
        if (!valid) { error = "Qualifier name contains invalid characters (Valid characters are: A-Z 0-9 _ .) (# must be the first character, _ . special characters can't be the first or last characters)";  return false; }
        return true;
    } else if (type == KnownTokenType::RESTRICTED) {
        if (name.size() > MAX_NAME_LENGTH) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH); return false; }
        bool valid = IsRestrictedNameValid(name);
        if (!valid) { error = "Restricted name contains invalid characters (Valid characters are: A-Z 0-9 _ .) ($ must be the first character, _ . special characters can't be the first or last characters)";  return false; }
        return true;
    } else {
        if (name.size() > MAX_NAME_LENGTH - 1) { error = "Name is greater than max length of " + std::to_string(MAX_NAME_LENGTH - 1); return false; }  //Tokens and sub-tokens need to leave one extra char for OWNER indicator
        if (!IsTokenNameASubtoken(name) && name.size() < MIN_TOKEN_LENGTH) { error = "Name must be contain " + std::to_string(MIN_TOKEN_LENGTH) + " characters"; return false; }
        bool valid = IsNameValidBeforeTag(name);
        if (!valid && IsTokenNameASubtoken(name) && name.size() < 3) { error = "Name must have at least 3 characters (Valid characters are: A-Z 0-9 _ .)";  return false; }
        if (!valid) { error = "Name contains invalid characters (Valid characters are: A-Z 0-9 _ .) (special characters can't be the first or last characters)";  return false; }
        return true;
    }
}

std::string RestrictedNameToOwnerName(const std::string& name)
{
    if (!IsTokenNameAnRestricted(name)) {
        return "";
    }

    std::string temp_owner = name.substr(1,name.length());
    temp_owner = temp_owner + OWNER_TAG;

    return temp_owner;
}

std::string GetParentName(const std::string& name)
{
    KnownTokenType type;
    if (!IsTokenNameValid(name, type))
        return "";

    auto index = std::string::npos;
    if (type == KnownTokenType::SUB) {
        index = name.find_last_of(SUB_NAME_DELIMITER);
    } else if (type == KnownTokenType::UNIQUE) {
        index = name.find_last_of(UNIQUE_TAG_DELIMITER);
    } else if (type == KnownTokenType::MSGCHANNEL) {
        index = name.find_last_of(MSG_CHANNEL_TAG_DELIMITER);
    } else if (type == KnownTokenType::VOTE) {
        index = name.find_last_of(VOTE_TAG_DELIMITER);
    } else if (type == KnownTokenType::ROOT) {
        return name;
    } else if (type == KnownTokenType::QUALIFIER) {
        return name;
    } else if (type == KnownTokenType::SUB_QUALIFIER) {
        index = name.find_last_of(SUB_NAME_DELIMITER);
    } else if (type == KnownTokenType::RESTRICTED) {
        return name;
    }

    if (std::string::npos != index)
    {
        return name.substr(0, index);
    }

    return name;
}

std::string GetUniqueTokenName(const std::string& parent, const std::string& tag)
{
    std::string unique = parent + "#" + tag;

    KnownTokenType type;
    if (!IsTokenNameValid(unique, type)) {
        return "";
    }

    if (type != KnownTokenType::UNIQUE)
        return "";

    return unique;
}

bool CNewToken::IsNull() const
{
    return strName == "";
}

CNewToken::CNewToken(const CNewToken& token)
{
    this->strName = token.strName;
    this->nAmount = token.nAmount;
    this->units = token.units;
    this->nHasIPFS = token.nHasIPFS;
    this->nReissuable = token.nReissuable;
    this->strIPFSHash = token.strIPFSHash;
}

CNewToken& CNewToken::operator=(const CNewToken& token)
{
    this->strName = token.strName;
    this->nAmount = token.nAmount;
    this->units = token.units;
    this->nHasIPFS = token.nHasIPFS;
    this->nReissuable = token.nReissuable;
    this->strIPFSHash = token.strIPFSHash;
    return *this;
}

std::string CNewToken::ToString()
{
    std::stringstream ss;
    ss << "Printing an token" << "\n";
    ss << "name : " << strName << "\n";
    ss << "amount : " << nAmount << "\n";
    ss << "units : " << std::to_string(units) << "\n";
    ss << "reissuable : " << std::to_string(nReissuable) << "\n";
    ss << "has_ipfs : " << std::to_string(nHasIPFS) << "\n";

    if (nHasIPFS)
        ss << "ipfs_hash : " << strIPFSHash;

    return ss.str();
}

CNewToken::CNewToken(const std::string& strName, const CAmount& nAmount, const int& units, const int& nReissuable, const int& nHasIPFS, const std::string& strIPFSHash)
{
    this->SetNull();
    this->strName = strName;
    this->nAmount = nAmount;
    this->units = int8_t(units);
    this->nReissuable = int8_t(nReissuable);
    this->nHasIPFS = int8_t(nHasIPFS);
    this->strIPFSHash = strIPFSHash;
}
CNewToken::CNewToken(const std::string& strName, const CAmount& nAmount)
{
    this->SetNull();
    this->strName = strName;
    this->nAmount = nAmount;
    this->units = int8_t(DEFAULT_UNITS);
    this->nReissuable = int8_t(DEFAULT_REISSUABLE);
    this->nHasIPFS = int8_t(DEFAULT_HAS_IPFS);
    this->strIPFSHash = DEFAULT_IPFS;
}

CDatabasedTokenData::CDatabasedTokenData(const CNewToken& token, const int& nHeight, const uint256& blockHash)
{
    this->SetNull();
    this->token = token;
    this->nHeight = nHeight;
    this->blockHash = blockHash;
}

CDatabasedTokenData::CDatabasedTokenData()
{
    this->SetNull();
}

/**
 * Constructs a CScript that carries the token name and quantity and adds to to the end of the given script
 * @param dest - The destination that the token will belong to
 * @param script - This script needs to be a pay to address script
 */
void CNewToken::ConstructTransaction(CScript& script) const
{
    CDataStream ssToken(SER_NETWORK, PROTOCOL_VERSION);
    ssToken << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(TOKEN_Y); // y
    vchMessage.push_back(TOKEN_N); // n
    vchMessage.push_back(TOKEN_A); // a
    vchMessage.push_back(TOKEN_Q); // q

    vchMessage.insert(vchMessage.end(), ssToken.begin(), ssToken.end());
    script << OP_YONA_TOKEN << ToByteVector(vchMessage) << OP_DROP;
}

void CNewToken::ConstructOwnerTransaction(CScript& script) const
{
    CDataStream ssOwner(SER_NETWORK, PROTOCOL_VERSION);
    ssOwner << std::string(this->strName + OWNER_TAG);

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(TOKEN_Y); // y
    vchMessage.push_back(TOKEN_N); // n
    vchMessage.push_back(TOKEN_A); // a
    vchMessage.push_back(TOKEN_O); // o

    vchMessage.insert(vchMessage.end(), ssOwner.begin(), ssOwner.end());
    script << OP_YONA_TOKEN << ToByteVector(vchMessage) << OP_DROP;
}

bool TokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress)
{
    // Check to see if the transaction is an new token issue tx
    if (!tx.IsNewToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return TokenFromScript(scriptPubKey, token, strAddress);
}

bool MsgChannelTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress)
{
    // Check to see if the transaction is an new token issue tx
    if (!tx.IsNewMsgChannelToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return MsgChannelTokenFromScript(scriptPubKey, token, strAddress);
}

bool QualifierTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress)
{
    // Check to see if the transaction is an new token qualifier issue tx
    if (!tx.IsNewQualifierToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return QualifierTokenFromScript(scriptPubKey, token, strAddress);
}
bool RestrictedTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress)
{
    // Check to see if the transaction is an new token qualifier issue tx
    if (!tx.IsNewRestrictedToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return RestrictedTokenFromScript(scriptPubKey, token, strAddress);
}

bool ReissueTokenFromTransaction(const CTransaction& tx, CReissueToken& reissue, std::string& strAddress)
{
    // Check to see if the transaction is a reissue tx
    if (!tx.IsReissueToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return ReissueTokenFromScript(scriptPubKey, reissue, strAddress);
}

bool UniqueTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress)
{
    // Check to see if the transaction is an new token issue tx
    if (!tx.IsNewUniqueToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 1].scriptPubKey;

    return TokenFromScript(scriptPubKey, token, strAddress);
}

bool IsNewOwnerTxValid(const CTransaction& tx, const std::string& tokenName, const std::string& address, std::string& errorMsg)
{
    // TODO when ready to ship. Put the owner validation code in own method if needed
    std::string ownerName;
    std::string ownerAddress;
    if (!OwnerFromTransaction(tx, ownerName, ownerAddress)) {
        errorMsg = "bad-txns-bad-owner";
        return false;
    }

    int size = ownerName.size();

    if (ownerAddress != address) {
        errorMsg = "bad-txns-owner-address-mismatch";
        return false;
    }

    if (size < OWNER_LENGTH + MIN_TOKEN_LENGTH) {
        errorMsg = "bad-txns-owner-token-length";
        return false;
    }

    if (ownerName != std::string(tokenName + OWNER_TAG)) {
        errorMsg = "bad-txns-owner-name-mismatch";
        return false;
    }

    return true;
}

bool OwnerFromTransaction(const CTransaction& tx, std::string& ownerName, std::string& strAddress)
{
    // Check to see if the transaction is an new token issue tx
    if (!tx.IsNewToken())
        return false;

    // Get the scriptPubKey from the last tx in vout
    CScript scriptPubKey = tx.vout[tx.vout.size() - 2].scriptPubKey;

    return OwnerTokenFromScript(scriptPubKey, ownerName, strAddress);
}

bool TransferTokenFromScript(const CScript& scriptPubKey, CTokenTransfer& tokenTransfer, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptTransferToken(scriptPubKey, nStartingIndex)) {
        return false;
    }

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchTransferToken;

    vchTransferToken.insert(vchTransferToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());

    CDataStream ssToken(vchTransferToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssToken >> tokenTransfer;
    } catch(std::exception& e) {
        error("Failed to get the transfer token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool TokenFromScript(const CScript& scriptPubKey, CNewToken& tokenNew, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptNewToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchNewToken;
    vchNewToken.insert(vchNewToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssToken(vchNewToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssToken >> tokenNew;
    } catch(std::exception& e) {
        error("Failed to get the token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool MsgChannelTokenFromScript(const CScript& scriptPubKey, CNewToken& tokenNew, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptNewMsgChannelToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchNewToken;
    vchNewToken.insert(vchNewToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssToken(vchNewToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssToken >> tokenNew;
    } catch(std::exception& e) {
        error("Failed to get the msg channel token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool QualifierTokenFromScript(const CScript& scriptPubKey, CNewToken& tokenNew, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptNewQualifierToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchNewToken;
    vchNewToken.insert(vchNewToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssToken(vchNewToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssToken >> tokenNew;
    } catch(std::exception& e) {
        error("Failed to get the qualifier token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool RestrictedTokenFromScript(const CScript& scriptPubKey, CNewToken& tokenNew, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptNewRestrictedToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchNewToken;
    vchNewToken.insert(vchNewToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssToken(vchNewToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssToken >> tokenNew;
    } catch(std::exception& e) {
        error("Failed to get the restricted token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool OwnerTokenFromScript(const CScript& scriptPubKey, std::string& tokenName, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptOwnerToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchOwnerToken;
    vchOwnerToken.insert(vchOwnerToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssOwner(vchOwnerToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssOwner >> tokenName;
    } catch(std::exception& e) {
        error("Failed to get the owner token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool ReissueTokenFromScript(const CScript& scriptPubKey, CReissueToken& reissue, std::string& strAddress)
{
    int nStartingIndex = 0;
    if (!IsScriptReissueToken(scriptPubKey, nStartingIndex))
        return false;

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchReissueToken;
    vchReissueToken.insert(vchReissueToken.end(), scriptPubKey.begin() + nStartingIndex, scriptPubKey.end());
    CDataStream ssReissue(vchReissueToken, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssReissue >> reissue;
    } catch(std::exception& e) {
        error("Failed to get the reissue token from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool TokenNullDataFromScript(const CScript& scriptPubKey, CNullTokenTxData& tokenData, std::string& strAddress)
{
    if (!scriptPubKey.IsNullTokenTxDataScript()) {
        return false;
    }

    CTxDestination destination;
    ExtractDestination(scriptPubKey, destination);

    strAddress = EncodeDestination(destination);

    std::vector<unsigned char> vchTokenData;
    vchTokenData.insert(vchTokenData.end(), scriptPubKey.begin() + OFFSET_TWENTY_THREE, scriptPubKey.end());
    CDataStream ssData(vchTokenData, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssData >> tokenData;
    } catch(std::exception& e) {
        error("Failed to get the null token tx data from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool GlobalTokenNullDataFromScript(const CScript& scriptPubKey, CNullTokenTxData& tokenData)
{
    if (!scriptPubKey.IsNullGlobalRestrictionTokenTxDataScript()) {
        return false;
    }

    std::vector<unsigned char> vchTokenData;
    vchTokenData.insert(vchTokenData.end(), scriptPubKey.begin() + OFFSET_FOUR, scriptPubKey.end());
    CDataStream ssData(vchTokenData, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssData >> tokenData;
    } catch(std::exception& e) {
        error("Failed to get the global restriction token tx data from the stream: %s", e.what());
        return false;
    }

    return true;
}

bool TokenNullVerifierDataFromScript(const CScript& scriptPubKey, CNullTokenTxVerifierString& verifierData)
{
    if (!scriptPubKey.IsNullTokenVerifierTxDataScript()) {
        return false;
    }

    std::vector<unsigned char> vchTokenData;
    vchTokenData.insert(vchTokenData.end(), scriptPubKey.begin() + OFFSET_THREE, scriptPubKey.end());
    CDataStream ssData(vchTokenData, SER_NETWORK, PROTOCOL_VERSION);

    try {
        ssData >> verifierData;
    } catch(std::exception& e) {
        error("Failed to get the verifier string from the stream: %s", e.what());
        return false;
    }

    return true;
}

//! Call VerifyNewToken if this function returns true
bool CTransaction::IsNewToken() const
{
    // New Token transaction will always have at least three outputs.
    // 1. Owner Token output
    // 2. Issue Token output
    // 3. YONA Burn Fee
    if (vout.size() < 3) {
        return false;
    }

    // Check for the tokens data CTxOut. This will always be the last output in the transaction
    if (!CheckIssueDataTx(vout[vout.size() - 1]))
        return false;

    // Check to make sure the owner token is created
    if (!CheckOwnerDataTx(vout[vout.size() - 2]))
        return false;

    // Don't overlap with IsNewUniqueToken()
    CScript script = vout[vout.size() - 1].scriptPubKey;
    if (IsScriptNewUniqueToken(script)|| IsScriptNewRestrictedToken(script))
        return false;

    return true;
}

//! Make sure to call VerifyNewUniqueToken if this call returns true
bool CTransaction::IsNewUniqueToken() const
{
    // Check trailing outpoint for issue data with unique token name
    if (!CheckIssueDataTx(vout[vout.size() - 1]))
        return false;

    if (!IsScriptNewUniqueToken(vout[vout.size() - 1].scriptPubKey))
        return false;

    return true;
}

//! Call this function after IsNewUniqueToken
bool CTransaction::VerifyNewUniqueToken(std::string& strError) const
{
    // Must contain at least 3 outpoints (YONA burn, owner change and one or more new unique tokens that share a root (should be in trailing position))
    if (vout.size() < 3) {
        strError  = "bad-txns-unique-vout-size-to-small";
        return false;
    }

    // check for (and count) new unique token outpoints.  make sure they share a root.
    std::set<std::string> setUniqueTokens;
    std::string tokenRoot = "";
    int tokenOutpointCount = 0;

    for (auto out : vout) {
        if (IsScriptNewUniqueToken(out.scriptPubKey)) {
            CNewToken token;
            std::string address;
            if (!TokenFromScript(out.scriptPubKey, token, address)) {
                strError = "bad-txns-issue-unique-token-from-script";
                return false;
            }
            std::string root = GetParentName(token.strName);
            if (tokenRoot.compare("") == 0)
                tokenRoot = root;
            if (tokenRoot.compare(root) != 0) {
                strError = "bad-txns-issue-unique-token-compare-failed";
                return false;
            }

            // Check for duplicate unique tokens in the same transaction
            if (setUniqueTokens.count(token.strName)) {
                strError = "bad-txns-issue-unique-duplicate-name-in-same-tx";
                return false;
            }

            setUniqueTokens.insert(token.strName);
            tokenOutpointCount += 1;
        }
    }

    if (tokenOutpointCount == 0) {
        strError = "bad-txns-issue-unique-token-bad-outpoint-count";
        return false;
    }

    // check for burn outpoint (must account for each new token)
    bool fBurnOutpointFound = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, KnownTokenType::UNIQUE, tokenOutpointCount)) {
            fBurnOutpointFound = true;
            break;
        }
    }

    if (!fBurnOutpointFound) {
        strError = "bad-txns-issue-unique-token-burn-outpoints-not-found";
        return false;
    }

    // check for owner change outpoint that matches root
    bool fOwnerOutFound = false;
    for (auto out : vout) {
        CTokenTransfer transfer;
        std::string transferAddress;
        if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
            if (tokenRoot + OWNER_TAG == transfer.strName) {
                fOwnerOutFound = true;
                break;
            }
        }
    }

    if (!fOwnerOutFound) {
        strError = "bad-txns-issue-unique-token-missing-owner-token";
        return false;
    }

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners > 0 || nReissues > 0 || nIssues != tokenOutpointCount) {
        strError = "bad-txns-failed-unique-token-formatting-check";
        return false;
    }

    return true;
}

//! To be called on CTransactions where IsNewToken returns true
bool CTransaction::VerifyNewToken(std::string& strError) const {
    // Issuing an Token must contain at least 3 CTxOut( Yona Burn Tx, Any Number of other Outputs ..., Owner Token Tx, New Token Tx)
    if (vout.size() < 3) {
        strError = "bad-txns-issue-vout-size-to-small";
        return false;
    }

    // Check for the tokens data CTxOut. This will always be the last output in the transaction
    if (!CheckIssueDataTx(vout[vout.size() - 1])) {
        strError = "bad-txns-issue-data-not-found";
        return false;
    }

    // Check to make sure the owner token is created
    if (!CheckOwnerDataTx(vout[vout.size() - 2])) {
        strError = "bad-txns-issue-owner-data-not-found";
        return false;
    }

    // Get the token type
    CNewToken token;
    std::string address;
    if (!TokenFromScript(vout[vout.size() - 1].scriptPubKey, token, address)) {
        strError = "bad-txns-issue-serialzation-failed";
        return error("%s : Failed to get new token from transaction: %s", __func__, this->GetHash().GetHex());
    }

    KnownTokenType tokenType;
    IsTokenNameValid(token.strName, tokenType);

    std::string strOwnerName;
    if (!OwnerTokenFromScript(vout[vout.size() - 2].scriptPubKey, strOwnerName, address)) {
        strError = "bad-txns-issue-owner-serialzation-failed";
        return false;
    }

    if (strOwnerName != token.strName + OWNER_TAG) {
        strError = "bad-txns-issue-owner-name-doesn't-match";
        return false;
    }

    // Check for the Burn CTxOut in one of the vouts ( This is needed because the change CTxOut is places in a random position in the CWalletTx
    bool fFoundIssueBurnTx = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, tokenType)) {
            fFoundIssueBurnTx = true;
            break;
        }
    }

    if (!fFoundIssueBurnTx) {
        strError = "bad-txns-issue-burn-not-found";
        return false;
    }

    if (tokenType == KnownTokenType::SUB) {
        std::string root = GetParentName(token.strName);
        bool fOwnerOutFound = false;
        for (auto out : this->vout) {
            CTokenTransfer transfer;
            std::string transferAddress;
            if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
                if (root + OWNER_TAG == transfer.strName) {
                    fOwnerOutFound = true;
                    break;
                }
            }
        }

        if (!fOwnerOutFound) {
            strError = "bad-txns-issue-new-token-missing-owner-token";
            return false;
        }
    }

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners != 1 || nIssues != 1 || nReissues > 0) {
        strError = "bad-txns-failed-issue-token-formatting-check";
        return false;
    }

    return true;
}

//! Make sure to call VerifyNewUniqueToken if this call returns true
bool CTransaction::IsNewMsgChannelToken() const
{
    // Check trailing outpoint for issue data with unique token name
    if (!CheckIssueDataTx(vout[vout.size() - 1]))
        return false;

    if (!IsScriptNewMsgChannelToken(vout[vout.size() - 1].scriptPubKey))
        return false;

    return true;
}

//! To be called on CTransactions where IsNewToken returns true
bool CTransaction::VerifyNewMsgChannelToken(std::string &strError) const
{
    // Issuing an Token must contain at least 3 CTxOut( Yona Burn Tx, Any Number of other Outputs ..., Owner Token Tx, New Token Tx)
    if (vout.size() < 3) {
        strError  = "bad-txns-issue-msgchannel-vout-size-to-small";
        return false;
    }

    // Check for the tokens data CTxOut. This will always be the last output in the transaction
    if (!CheckIssueDataTx(vout[vout.size() - 1])) {
        strError  = "bad-txns-issue-data-not-found";
        return false;
    }

    // Get the token type
    CNewToken token;
    std::string address;
    if (!MsgChannelTokenFromScript(vout[vout.size() - 1].scriptPubKey, token, address)) {
        strError = "bad-txns-issue-msgchannel-serialzation-failed";
        return error("%s : Failed to get new msgchannel token from transaction: %s", __func__, this->GetHash().GetHex());
    }

    KnownTokenType tokenType;
    IsTokenNameValid(token.strName, tokenType);

    // Check for the Burn CTxOut in one of the vouts ( This is needed because the change CTxOut is places in a random position in the CWalletTx
    bool fFoundIssueBurnTx = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, KnownTokenType::MSGCHANNEL)) {
            fFoundIssueBurnTx = true;
            break;
        }
    }

    if (!fFoundIssueBurnTx) {
        strError = "bad-txns-issue-msgchannel-burn-not-found";
        return false;
    }

    // check for owner change outpoint that matches root
    std::string root = GetParentName(token.strName);
    bool fOwnerOutFound = false;
    for (auto out : vout) {
        CTokenTransfer transfer;
        std::string transferAddress;
        if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
            if (root + OWNER_TAG == transfer.strName) {
                fOwnerOutFound = true;
                break;
            }
        }
    }

    if (!fOwnerOutFound) {
        strError = "bad-txns-issue-msg-channel-token-bad-owner-token";
        return false;
    }

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners != 0 || nIssues != 1 || nReissues > 0) {
        strError = "bad-txns-failed-issue-msgchannel-token-formatting-check";
        return false;
    }

    return true;
}

//! Make sure to call VerifyNewQualifierToken if this call returns true
bool CTransaction::IsNewQualifierToken() const
{
    // Check trailing outpoint for issue data with unique token name
    if (!CheckIssueDataTx(vout[vout.size() - 1]))
        return false;

    if (!IsScriptNewQualifierToken(vout[vout.size() - 1].scriptPubKey))
        return false;

    return true;
}

//! To be called on CTransactions where IsNewQualifierToken returns true
bool CTransaction::VerifyNewQualfierToken(std::string &strError) const
{
    // Issuing an Token must contain at least 2 CTxOut( Yona Burn Tx, New Token Tx, Any Number of other Outputs...)
    if (vout.size() < 2) {
        strError  = "bad-txns-issue-qualifier-vout-size-to-small";
        return false;
    }

    // Check for the tokens data CTxOut. This will always be the last output in the transaction
    if (!CheckIssueDataTx(vout[vout.size() - 1])) {
        strError  = "bad-txns-issue-qualifider-data-not-found";
        return false;
    }

    // Get the token type
    CNewToken token;
    std::string address;
    if (!QualifierTokenFromScript(vout[vout.size() - 1].scriptPubKey, token, address)) {
        strError = "bad-txns-issue-qualifier-serialzation-failed";
        return error("%s : Failed to get new qualifier token from transaction: %s", __func__, this->GetHash().GetHex());
    }

    KnownTokenType tokenType;
    IsTokenNameValid(token.strName, tokenType);

    // Check for the Burn CTxOut in one of the vouts ( This is needed because the change CTxOut is places in a random position in the CWalletTx
    bool fFoundIssueBurnTx = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, tokenType)) {
            fFoundIssueBurnTx = true;
            break;
        }
    }

    if (!fFoundIssueBurnTx) {
        strError = "bad-txns-issue-qualifier-burn-not-found";
        return false;
    }

    if (tokenType == KnownTokenType::SUB_QUALIFIER) {
        // Check that there is an token transfer with the parent name, qualifier use just the parent name, they don't use not parent + !
        bool fOwnerOutFound = false;
        std::string root = GetParentName(token.strName);
        for (auto out : vout) {
            CTokenTransfer transfer;
            std::string transferAddress;
            if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
                if (root == transfer.strName) {
                    fOwnerOutFound = true;
                    break;
                }
            }
        }

        if (!fOwnerOutFound) {
            strError  = "bad-txns-issue-sub-qualifier-parent-outpoint-not-found";
            return false;
        }
    }

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners != 0 || nIssues != 1 || nReissues > 0) {
        strError = "bad-txns-failed-issue-token-formatting-check";
        return false;
    }

    return true;
}

//! Make sure to call VerifyNewToken if this call returns true
bool CTransaction::IsNewRestrictedToken() const
{
    // Check trailing outpoint for issue data with unique token name
    if (!CheckIssueDataTx(vout[vout.size() - 1]))
        return false;

    if (!IsScriptNewRestrictedToken(vout[vout.size() - 1].scriptPubKey))
        return false;

    return true;
}

//! To be called on CTransactions where IsNewRestrictedToken returns true
bool CTransaction::VerifyNewRestrictedToken(std::string& strError) const {
    // Issuing a restricted token must cointain at least 4 CTxOut(Yona Burn Tx, Token Creation, Root Owner Token Transfer, and CNullTokenTxVerifierString)
    if (vout.size() < 4) {
        strError = "bad-txns-issue-restricted-vout-size-to-small";
        return false;
    }

    // Check for the tokens data CTxOut. This will always be the last output in the transaction
    if (!CheckIssueDataTx(vout[vout.size() - 1])) {
        strError = "bad-txns-issue-restricted-data-not-found";
        return false;
    }

    // Get the token type
    CNewToken token;
    std::string address;
    if (!RestrictedTokenFromScript(vout[vout.size() - 1].scriptPubKey, token, address)) {
        strError = "bad-txns-issue-restricted-serialization-failed";
        return error("%s : Failed to get new restricted token from transaction: %s", __func__, this->GetHash().GetHex());
    }

    KnownTokenType tokenType;
    IsTokenNameValid(token.strName, tokenType);

    // Check for the Burn CTxOut in one of the vouts ( This is needed because the change CTxOut is places in a random position in the CWalletTx
    bool fFoundIssueBurnTx = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, tokenType)) {
            fFoundIssueBurnTx = true;
            break;
        }
    }

    if (!fFoundIssueBurnTx) {
        strError = "bad-txns-issue-restricted-burn-not-found";
        return false;
    }

    // Check that there is an token transfer with the parent name, restricted tokens use the root owner token. So issuing $TOKEN requires TOKEN!
    bool fRootOwnerOutFound = false;
    std::string root = GetParentName(token.strName);
    std::string strippedRoot = root.substr(1, root.size() -1) + OWNER_TAG; // $TOKEN checks for TOKEN!
    for (auto out : vout) {
        CTokenTransfer transfer;
        std::string transferAddress;
        if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
            if (strippedRoot == transfer.strName) {
                fRootOwnerOutFound = true;
                break;
            }
        }
    }

    if (!fRootOwnerOutFound) {
        strError  = "bad-txns-issue-restricted-root-owner-token-outpoint-not-found";
        return false;
    }

    // Check to make sure we can get the verifier string from the transaction
    CNullTokenTxVerifierString verifier;
    if (!GetVerifierStringFromTx(verifier, strError)) {
        return false;
    }

    // TODO is verifier string valid check, this happen automatically when processing the nulltoken tx outputs

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners != 0 || nIssues != 1 || nReissues > 0) {
        strError = "bad-txns-failed-issue-token-formatting-check";
        return false;
    }

    return true;
}

bool CTransaction::GetVerifierStringFromTx(CNullTokenTxVerifierString& verifier, std::string& strError, bool& fNotFound) const
{
    fNotFound = false;
    bool found = false;
    int count = 0;
    for (auto out : vout) {
        if (out.scriptPubKey.IsNullTokenVerifierTxDataScript()) {
            count++;

            if (count > 1) {
                strError = _("Multiple verifier strings found in transaction");
                return false;
            }
            if (!TokenNullVerifierDataFromScript(out.scriptPubKey, verifier)) {
                strError = _("Failed to get verifier string from output: ") + out.ToString();
                return false;
            }

            found = true;
        }
    }

    // Set error message, for if it returns false
    if (!found) {
        fNotFound = true;
        strError = _("Verifier string not found");
    }

    return found && count == 1;
}

bool CTransaction::GetVerifierStringFromTx(CNullTokenTxVerifierString& verifier, std::string& strError) const
{
    bool fNotFound = false;
    return GetVerifierStringFromTx(verifier, strError, fNotFound);
}

bool CTransaction::IsReissueToken() const
{
    // Check for the reissue token data CTxOut. This will always be the last output in the transaction
    if (!CheckReissueDataTx(vout[vout.size() - 1]))
        return false;

    return true;
}

//! To be called on CTransactions where IsReissueToken returns true
bool CTransaction::VerifyReissueToken(std::string& strError) const
{
    // Reissuing an Token must contain at least 3 CTxOut ( Yona Burn Tx, Any Number of other Outputs ..., Reissue Token Tx, Owner Token Change Tx)
    if (vout.size() < 3) {
        strError  = "bad-txns-vout-size-to-small";
        return false;
    }

    // Check for the reissue token data CTxOut. This will always be the last output in the transaction
    if (!CheckReissueDataTx(vout[vout.size() - 1])) {
        strError  = "bad-txns-reissue-data-not-found";
        return false;
    }

    CReissueToken reissue;
    std::string address;
    if (!ReissueTokenFromScript(vout[vout.size() - 1].scriptPubKey, reissue, address)) {
        strError  = "bad-txns-reissue-serialization-failed";
        return false;
    }

    // Reissuing a regular token checks the reissue_token_name + "!"
    KnownTokenType token_type = KnownTokenType::INVALID;
    IsTokenNameValid(reissue.strName, token_type);

    // This is going to be the token name that we need to verify that the owner token of was added to the transaction
    std::string token_name_to_check = reissue.strName;

    // If the token type is restricted, remove the $ from the name, so we can check for the correct owner token transfer
    if (token_type == KnownTokenType::RESTRICTED) {
        token_name_to_check = reissue.strName.substr(1, reissue.strName.size() -1);
    }

    // Check that there is an token transfer, this will be the owner token change
    bool fOwnerOutFound = false;
    for (auto out : vout) {
        CTokenTransfer transfer;
        std::string transferAddress;
        if (TransferTokenFromScript(out.scriptPubKey, transfer, transferAddress)) {
            if (token_name_to_check + OWNER_TAG == transfer.strName) {
                fOwnerOutFound = true;
                break;
            }
        }
    }

    if (!fOwnerOutFound) {
        strError  = "bad-txns-reissue-owner-outpoint-not-found";
        return false;
    }

    // Check for the Burn CTxOut in one of the vouts ( This is needed because the change CTxOut is placed in a random position in the CWalletTx
    bool fFoundReissueBurnTx = false;
    for (auto out : vout) {
        if (CheckReissueBurnTx(out)) {
            fFoundReissueBurnTx = true;
            break;
        }
    }

    if (!fFoundReissueBurnTx) {
        strError = "bad-txns-reissue-burn-outpoint-not-found";
        return false;
    }

    // Loop through all of the vouts and make sure only the expected token creations are taking place
    int nTransfers = 0;
    int nOwners = 0;
    int nIssues = 0;
    int nReissues = 0;
    GetTxOutKnownTokenTypes(vout, nIssues, nReissues, nTransfers, nOwners);

    if (nOwners > 0 || nReissues != 1 || nIssues > 0) {
        strError = "bad-txns-failed-reissue-token-formatting-check";
        return false;
    }

    return true;
}

bool CTransaction::CheckAddingTagBurnFee(const int& count) const
{
    // check for burn outpoint )
    bool fBurnOutpointFound = false;
    for (auto out : vout) {
        if (CheckIssueBurnTx(out, KnownTokenType::NULL_ADD_QUALIFIER, count)) {
            fBurnOutpointFound = true;
            break;
        }
    }

   return fBurnOutpointFound;
}

CTokenTransfer::CTokenTransfer(const std::string& strTokenName, const CAmount& nAmount, const uint32_t& nTimeLock, const std::string& message, const int64_t& nExpireTime)
{
    SetNull();
    this->strName = strTokenName;
    this->nAmount = nAmount;
    this->nTimeLock = nTimeLock;
    this->message = message;
    if (!message.empty()) {
        if (nExpireTime) {
            this->nExpireTime = nExpireTime;
        } else {
            this->nExpireTime = 0;
        }
    }
}

bool CTokenTransfer::IsValid(std::string& strError) const
{
    // Don't use this function with any sort of consensus checks
    // All of these checks are run with ContextualCheckTransferToken also

    strError = "";

    if (!IsTokenNameValid(std::string(strName))) {
        strError = "Invalid parameter: token_name must only consist of valid characters and have a size between 3 and 30 characters. See help for more details.";
        return false;
    }

    // this function is only being called in createrawtranasction, so it is fine to have a contextual check here
    // if this gets called anywhere else, we will need to move this to a Contextual function
    if (nAmount <= 0) {
        strError = "Invalid parameter: token amount can't be equal to or less than zero.";
        return false;
    }

    if (message.empty() && nExpireTime > 0) {
        strError = "Invalid parameter: token transfer expiration time requires a message to be attached to the transfer";
        return false;
    }

    if (nExpireTime < 0) {
        strError = "Invalid parameter: expiration time must be a positive value";
        return false;
    }

    if (message.size() && !CheckEncoded(message, strError)) {
        return false;
    }

    return true;
}

bool CTokenTransfer::ContextualCheckAgainstVerifyString(CTokensCache *tokenCache, const std::string& address, std::string& strError) const
{
    // Get the verifier string
    CNullTokenTxVerifierString verifier;
    if (!tokenCache->GetTokenVerifierStringIfExists(this->strName, verifier, true)) {
        // This shouldn't ever happen, but if it does we need to know
        strError = _("Verifier String doesn't exist for token: ") + this->strName;
        return false;
    }

    if (!ContextualCheckVerifierString(tokenCache, verifier.verifier_string, address, strError))
        return false;

    return true;
}

void CTokenTransfer::ConstructTransaction(CScript& script) const
{
    CDataStream ssTransfer(SER_NETWORK, PROTOCOL_VERSION);
    ssTransfer << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(TOKEN_Y); // y
    vchMessage.push_back(TOKEN_N); // n
    vchMessage.push_back(TOKEN_A); // a
    vchMessage.push_back(TOKEN_T); // t

    vchMessage.insert(vchMessage.end(), ssTransfer.begin(), ssTransfer.end());
    script << OP_YONA_TOKEN << ToByteVector(vchMessage) << OP_DROP;
}

CReissueToken::CReissueToken(const std::string &strTokenName, const CAmount &nAmount, const int &nUnits, const int &nReissuable,
                             const std::string &strIPFSHash)
{
    SetNull();
    this->strName = strTokenName;
    this->strIPFSHash = strIPFSHash;
    this->nReissuable = int8_t(nReissuable);
    this->nAmount = nAmount;
    this->nUnits = nUnits;
}

void CReissueToken::ConstructTransaction(CScript& script) const
{
    CDataStream ssReissue(SER_NETWORK, PROTOCOL_VERSION);
    ssReissue << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(TOKEN_Y); // y
    vchMessage.push_back(TOKEN_N); // n
    vchMessage.push_back(TOKEN_A); // a
    vchMessage.push_back(TOKEN_R); // r

    vchMessage.insert(vchMessage.end(), ssReissue.begin(), ssReissue.end());
    script << OP_YONA_TOKEN << ToByteVector(vchMessage) << OP_DROP;
}

bool CReissueToken::IsNull() const
{
    return strName == "" || nAmount < 0;
}

bool CTokensCache::AddTransferToken(const CTokenTransfer& transferToken, const std::string& address, const COutPoint& out, const CTxOut& txOut)
{
    AddToTokenBalance(transferToken.strName, address, transferToken.nAmount);

    // Add to cache so we can save to database
    CTokenCacheNewTransfer newTransfer(transferToken, address, out);

    if (setNewTransferTokensToRemove.count(newTransfer))
        setNewTransferTokensToRemove.erase(newTransfer);

    setNewTransferTokensToAdd.insert(newTransfer);

    return true;
}

void CTokensCache::AddToTokenBalance(const std::string& strName, const std::string& address, const CAmount& nAmount)
{
    if (fTokenIndex) {
        auto pair = std::make_pair(strName, address);
        // Add to map address -> amount map

        // Get the best amount
        if (!GetBestTokenAddressAmount(*this, strName, address))
            mapTokensAddressAmount.insert(make_pair(pair, 0));

        // Add the new amount to the balance
        if (IsTokenNameAnOwner(strName))
            mapTokensAddressAmount.at(pair) = OWNER_TOKEN_AMOUNT;
        else
            mapTokensAddressAmount.at(pair) += nAmount;
    }
}

bool CTokensCache::TrySpendCoin(const COutPoint& out, const CTxOut& txOut)
{
    // Placeholder strings that will get set if you successfully get the transfer or token from the script
    std::string address = "";
    std::string tokenName = "";
    CAmount nAmount = -1;

    // Get the token tx data
    int nType = -1;
    bool fIsOwner = false;
    if (txOut.scriptPubKey.IsTokenScript(nType, fIsOwner)) {

        // Get the New Token or Transfer Token from the scriptPubKey
        if (nType == TX_NEW_TOKEN && !fIsOwner) {
            CNewToken token;
            if (TokenFromScript(txOut.scriptPubKey, token, address)) {
                tokenName = token.strName;
                nAmount = token.nAmount;
            }
        } else if (nType == TX_TRANSFER_TOKEN) {
            CTokenTransfer transfer;
            if (TransferTokenFromScript(txOut.scriptPubKey, transfer, address)) {
                tokenName = transfer.strName;
                nAmount = transfer.nAmount;
            }
        } else if (nType == TX_NEW_TOKEN && fIsOwner) {
            if (!OwnerTokenFromScript(txOut.scriptPubKey, tokenName, address))
                return error("%s : ERROR Failed to get owner token from the OutPoint: %s", __func__,
                             out.ToString());
            nAmount = OWNER_TOKEN_AMOUNT;
        } else if (nType == TX_REISSUE_TOKEN) {
            CReissueToken reissue;
            if (ReissueTokenFromScript(txOut.scriptPubKey, reissue, address)) {
                tokenName = reissue.strName;
                nAmount = reissue.nAmount;
            }
        }
    } else {
        // If it isn't an token tx return true, we only fail if an error occurs
        return true;
    }

    // If we got the address and the tokenName, proceed to remove it from the database, and in memory objects
    if (address != "" && tokenName != "") {
        if (fTokenIndex && nAmount > 0) {
            CTokenCacheSpendToken spend(tokenName, address, nAmount);
            if (GetBestTokenAddressAmount(*this, tokenName, address)) {
                auto pair = make_pair(tokenName, address);
                if (mapTokensAddressAmount.count(pair))
                    mapTokensAddressAmount.at(pair) -= nAmount;

                if (mapTokensAddressAmount.at(pair) < 0)
                    mapTokensAddressAmount.at(pair) = 0;

                // Update the cache so we can save to database
                vSpentTokens.push_back(spend);
            }
        }
    } else {
        return error("%s : ERROR Failed to get token from the OutPoint: %s", __func__, out.ToString());
    }

    return true;
}

bool CTokensCache::ContainsToken(const CNewToken& token)
{
    return CheckIfTokenExists(token.strName);
}

bool CTokensCache::ContainsToken(const std::string& tokenName)
{
    return CheckIfTokenExists(tokenName);
}

bool CTokensCache::UndoTokenCoin(const Coin& coin, const COutPoint& out)
{
    std::string strAddress = "";
    std::string tokenName = "";
    CAmount nAmount = 0;

    // Get the token tx from the script
    int nType = -1;
    bool fIsOwner = false;
    if(coin.out.scriptPubKey.IsTokenScript(nType, fIsOwner)) {

        if (nType == TX_NEW_TOKEN && !fIsOwner) {
            CNewToken token;
            if (!TokenFromScript(coin.out.scriptPubKey, token, strAddress)) {
                return error("%s : Failed to get token from script while trying to undo token spend. OutPoint : %s",
                             __func__,
                             out.ToString());
            }
            tokenName = token.strName;

            nAmount = token.nAmount;
        } else if (nType == TX_TRANSFER_TOKEN) {
            CTokenTransfer transfer;
            if (!TransferTokenFromScript(coin.out.scriptPubKey, transfer, strAddress))
                return error(
                        "%s : Failed to get transfer token from script while trying to undo token spend. OutPoint : %s",
                        __func__,
                        out.ToString());

            tokenName = transfer.strName;
            nAmount = transfer.nAmount;
        } else if (nType == TX_NEW_TOKEN && fIsOwner) {
            std::string ownerName;
            if (!OwnerTokenFromScript(coin.out.scriptPubKey, ownerName, strAddress))
                return error(
                        "%s : Failed to get owner token from script while trying to undo token spend. OutPoint : %s",
                        __func__, out.ToString());
            tokenName = ownerName;
            nAmount = OWNER_TOKEN_AMOUNT;
        } else if (nType == TX_REISSUE_TOKEN) {
            CReissueToken reissue;
            if (!ReissueTokenFromScript(coin.out.scriptPubKey, reissue, strAddress))
                return error(
                        "%s : Failed to get reissue token from script while trying to undo token spend. OutPoint : %s",
                        __func__, out.ToString());
            tokenName = reissue.strName;
            nAmount = reissue.nAmount;
        }
    }

    if (tokenName == "" || strAddress == "" || nAmount == 0)
        return error("%s : TokenName, Address or nAmount is invalid., Token Name: %s, Address: %s, Amount: %d", __func__, tokenName, strAddress, nAmount);

    if (!AddBackSpentToken(coin, tokenName, strAddress, nAmount, out))
        return error("%s : Failed to add back the spent token. OutPoint : %s", __func__, out.ToString());

    return true;
}

//! Changes Memory Only
bool CTokensCache::AddBackSpentToken(const Coin& coin, const std::string& tokenName, const std::string& address, const CAmount& nAmount, const COutPoint& out)
{
    if (fTokenIndex) {
        // Update the tokens address balance
        auto pair = std::make_pair(tokenName, address);

        // Get the map address amount from database if the map doesn't have it already
        if (!GetBestTokenAddressAmount(*this, tokenName, address))
            mapTokensAddressAmount.insert(std::make_pair(pair, 0));

        mapTokensAddressAmount.at(pair) += nAmount;
    }

    // Add the undoAmount to the vector so we know what changes are dirty and what needs to be saved to database
    CTokenCacheUndoTokenAmount undoAmount(tokenName, address, nAmount);
    vUndoTokenAmount.push_back(undoAmount);

    return true;
}

//! Changes Memory Only
bool CTokensCache::UndoTransfer(const CTokenTransfer& transfer, const std::string& address, const COutPoint& outToRemove)
{
    if (fTokenIndex) {
        // Make sure we are in a valid state to undo the transfer of the token
        if (!GetBestTokenAddressAmount(*this, transfer.strName, address))
            return error("%s : Failed to get the tokens address balance from the database. Token : %s Address : %s",
                         __func__, transfer.strName, address);

        auto pair = std::make_pair(transfer.strName, address);
        if (!mapTokensAddressAmount.count(pair))
            return error(
                    "%s : Tried undoing a transfer and the map of address amount didn't have the token address pair. Token : %s Address : %s",
                    __func__, transfer.strName, address);

        if (mapTokensAddressAmount.at(pair) < transfer.nAmount)
            return error(
                    "%s : Tried undoing a transfer and the map of address amount had less than the amount we are trying to undo. Token : %s Address : %s",
                    __func__, transfer.strName, address);

        // Change the in memory balance of the token at the address
        mapTokensAddressAmount[pair] -= transfer.nAmount;
    }

    return true;
}

//! Changes Memory Only
bool CTokensCache::RemoveNewToken(const CNewToken& token, const std::string address)
{
    if (!CheckIfTokenExists(token.strName))
        return error("%s : Tried removing an token that didn't exist. Token Name : %s", __func__, token.strName);

    CTokenCacheNewToken newToken(token, address, 0 , uint256());

    if (setNewTokensToAdd.count(newToken))
        setNewTokensToAdd.erase(newToken);

    setNewTokensToRemove.insert(newToken);

    if (fTokenIndex)
        mapTokensAddressAmount[std::make_pair(token.strName, address)] = 0;

    return true;
}

//! Changes Memory Only
bool CTokensCache::AddNewToken(const CNewToken& token, const std::string address, const int& nHeight, const uint256& blockHash)
{
    if(CheckIfTokenExists(token.strName))
        return error("%s: Tried adding new token, but it already existed in the set of tokens: %s", __func__, token.strName);

    CTokenCacheNewToken newToken(token, address, nHeight, blockHash);

    if (setNewTokensToRemove.count(newToken))
        setNewTokensToRemove.erase(newToken);

    setNewTokensToAdd.insert(newToken);

    if (fTokenIndex) {
        // Insert the token into the assests address amount map
        mapTokensAddressAmount[std::make_pair(token.strName, address)] = token.nAmount;
    }

    return true;
}

//! Changes Memory Only
bool CTokensCache::AddReissueToken(const CReissueToken& reissue, const std::string address, const COutPoint& out)
{
    auto pair = std::make_pair(reissue.strName, address);

    CNewToken token;
    int tokenHeight;
    uint256 tokenBlockHash;
    if (!GetTokenMetaDataIfExists(reissue.strName, token, tokenHeight, tokenBlockHash))
        return error("%s: Failed to get the original token that is getting reissued. Token Name : %s",
                     __func__, reissue.strName);

    // Insert the reissue information into the reissue map
    if (!mapReissuedTokenData.count(reissue.strName)) {
        token.nAmount += reissue.nAmount;
        token.nReissuable = reissue.nReissuable;
        if (reissue.nUnits != -1)
            token.units = reissue.nUnits;

        if (reissue.strIPFSHash != "") {
            token.nHasIPFS = 1;
            token.strIPFSHash = reissue.strIPFSHash;
        }
        mapReissuedTokenData.insert(make_pair(reissue.strName, token));
    } else {
        mapReissuedTokenData.at(reissue.strName).nAmount += reissue.nAmount;
        mapReissuedTokenData.at(reissue.strName).nReissuable = reissue.nReissuable;
        if (reissue.nUnits != -1) {
            mapReissuedTokenData.at(reissue.strName).units = reissue.nUnits;
        }
        if (reissue.strIPFSHash != "") {
            mapReissuedTokenData.at(reissue.strName).nHasIPFS = 1;
            mapReissuedTokenData.at(reissue.strName).strIPFSHash = reissue.strIPFSHash;
        }
    }

    CTokenCacheReissueToken reissueToken(reissue, address, out, tokenHeight, tokenBlockHash);

    if (setNewReissueToRemove.count(reissueToken))
        setNewReissueToRemove.erase(reissueToken);

    setNewReissueToAdd.insert(reissueToken);

    if (fTokenIndex) {
        // Add the reissued amount to the address amount map
        if (!GetBestTokenAddressAmount(*this, reissue.strName, address))
            mapTokensAddressAmount.insert(make_pair(pair, 0));

        // Add the reissued amount to the amount in the map
        mapTokensAddressAmount[pair] += reissue.nAmount;
    }

    return true;

}

//! Changes Memory Only
bool CTokensCache::RemoveReissueToken(const CReissueToken& reissue, const std::string address, const COutPoint& out, const std::vector<std::pair<std::string, CBlockTokenUndo> >& vUndoIPFS)
{
    auto pair = std::make_pair(reissue.strName, address);

    CNewToken tokenData;
    int height;
    uint256 blockHash;
    if (!GetTokenMetaDataIfExists(reissue.strName, tokenData, height, blockHash))
        return error("%s: Tried undoing reissue of an token, but that token didn't exist: %s", __func__, reissue.strName);

    // Change the token data by undoing what was reissued
    tokenData.nAmount -= reissue.nAmount;
    tokenData.nReissuable = 1;

    bool fVerifierStringChanged = false;
    std::string verifierString = "";
    // Find the ipfs hash in the undoblock data and restore the ipfs hash to its previous hash
    for (auto undoItem : vUndoIPFS) {
        if (undoItem.first == reissue.strName) {
            if (undoItem.second.fChangedIPFS)
                tokenData.strIPFSHash = undoItem.second.strIPFS;
            if(undoItem.second.fChangedUnits)
                tokenData.units = undoItem.second.nUnits;
            if (tokenData.strIPFSHash == "")
                tokenData.nHasIPFS = 0;
            if (undoItem.second.fChangedVerifierString) {
                fVerifierStringChanged = true;
                verifierString = undoItem.second.verifierString;

            }
            break;
        }
    }

    mapReissuedTokenData[tokenData.strName] = tokenData;

    CTokenCacheReissueToken reissueToken(reissue, address, out, height, blockHash);

    if (setNewReissueToAdd.count(reissueToken))
        setNewReissueToAdd.erase(reissueToken);

    setNewReissueToRemove.insert(reissueToken);

    // If the verifier string was changed by this reissue, undo the change
    if (fVerifierStringChanged) {
        RemoveRestrictedVerifier(tokenData.strName, verifierString, true);
    }

    if (fTokenIndex) {
        // Get the best amount form the database or dirty cache
        if (!GetBestTokenAddressAmount(*this, reissue.strName, address)) {
            if (reissueToken.reissue.nAmount != 0)
                return error("%s : Trying to undo reissue of an token but the tokens amount isn't in the database",
                         __func__);
        }
        mapTokensAddressAmount[pair] -= reissue.nAmount;

        if (mapTokensAddressAmount[pair] < 0)
            return error("%s : Tried undoing reissue of an token, but the tokens amount went negative: %s", __func__,
                         reissue.strName);
    }

    return true;
}

//! Changes Memory Only
bool CTokensCache::AddOwnerToken(const std::string& tokensName, const std::string address)
{
    // Update the cache
    CTokenCacheNewOwner newOwner(tokensName, address);

    if (setNewOwnerTokensToRemove.count(newOwner))
        setNewOwnerTokensToRemove.erase(newOwner);

    setNewOwnerTokensToAdd.insert(newOwner);

    if (fTokenIndex) {
        // Insert the token into the assests address amount map
        mapTokensAddressAmount[std::make_pair(tokensName, address)] = OWNER_TOKEN_AMOUNT;
    }

    return true;
}

//! Changes Memory Only
bool CTokensCache::RemoveOwnerToken(const std::string& tokensName, const std::string address)
{
    // Update the cache
    CTokenCacheNewOwner newOwner(tokensName, address);
    if (setNewOwnerTokensToAdd.count(newOwner))
        setNewOwnerTokensToAdd.erase(newOwner);

    setNewOwnerTokensToRemove.insert(newOwner);

    if (fTokenIndex) {
        auto pair = std::make_pair(tokensName, address);
        mapTokensAddressAmount[pair] = 0;
    }

    return true;
}

//! Changes Memory Only
bool CTokensCache::RemoveTransfer(const CTokenTransfer &transfer, const std::string &address, const COutPoint &out)
{
    if (!UndoTransfer(transfer, address, out))
        return error("%s : Failed to undo the transfer", __func__);

    CTokenCacheNewTransfer newTransfer(transfer, address, out);
    if (setNewTransferTokensToAdd.count(newTransfer))
        setNewTransferTokensToAdd.erase(newTransfer);

    setNewTransferTokensToRemove.insert(newTransfer);

    return true;
}

//! Changes Memory Only, this only called when adding a block to the chain
bool CTokensCache::AddQualifierAddress(const std::string& tokenName, const std::string& address, const QualifierType type)
{
    CTokenCacheQualifierAddress newQualifier(tokenName, address, type);

    // We are adding a qualifier that was in a transaction, so, if the set of qualifiers
    // that contains qualifiers to undo contains the same qualfier tokenName, and address, erase it
    if (setNewQualifierAddressToRemove.count(newQualifier)) {
        setNewQualifierAddressToRemove.erase(newQualifier);
    }

    // If the set of qualifiers from transactions contains our qualifier already, we need to overwrite it
    if (setNewQualifierAddressToAdd.count(newQualifier)) {
        setNewQualifierAddressToAdd.erase(newQualifier);
    }

    if (IsTokenNameASubQualifier(tokenName)) {
        if (type == QualifierType::ADD_QUALIFIER) {
            mapRootQualifierAddressesAdd[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].insert(tokenName);
            mapRootQualifierAddressesRemove[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].erase(tokenName);
        } else {
            mapRootQualifierAddressesRemove[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].insert(tokenName);
            mapRootQualifierAddressesAdd[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].erase(tokenName);
        }
    }

    setNewQualifierAddressToAdd.insert(newQualifier);

    return true;
}

//! Changes Memory Only, this is only called when undoing a block from the chain
bool CTokensCache::RemoveQualifierAddress(const std::string& tokenName, const std::string& address, const QualifierType type)
{
    CTokenCacheQualifierAddress newQualifier(tokenName, address, type);

    // We are adding a qualifier that was in a transaction, so, if the set of qualifiers
    // that contains qualifiers to undo contains the same qualfier tokenName, and address, erase it
    if (setNewQualifierAddressToAdd.count(newQualifier)) {
        setNewQualifierAddressToAdd.erase(newQualifier);
    }

    // If the set of qualifiers from transactions contains our qualifier already, we need to overwrite it
    if (setNewQualifierAddressToRemove.count(newQualifier)) {
        setNewQualifierAddressToRemove.erase(newQualifier);
    }

    if (IsTokenNameASubQualifier(tokenName)) {
        if (type == QualifierType::ADD_QUALIFIER) {
            // When undoing a add, we want to remove it
            mapRootQualifierAddressesRemove[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].insert(tokenName);
            mapRootQualifierAddressesAdd[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].erase(tokenName);
        } else {
            // When undoing a remove, we want to add it
            mapRootQualifierAddressesAdd[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].insert(tokenName);
            mapRootQualifierAddressesRemove[CTokenCacheRootQualifierChecker(GetParentName(tokenName), address)].erase(tokenName);
        }
    }

    setNewQualifierAddressToRemove.insert(newQualifier);

    return true;
}


//! Changes Memory Only, this only called when adding a block to the chain
bool CTokensCache::AddRestrictedAddress(const std::string& tokenName, const std::string& address, const RestrictedType type)
{
    CTokenCacheRestrictedAddress newRestricted(tokenName, address, type);

    // We are adding a restricted address that was in a transaction, so, if the set of restricted addresses
    // to undo contains our restricted address. Erase it
    if (setNewRestrictedAddressToRemove.count(newRestricted)) {
        setNewRestrictedAddressToRemove.erase(newRestricted);
    }

    // If the set of restricted addresses from transactions contains our restricted token address already, we need to overwrite it
    if (setNewRestrictedAddressToAdd.count(newRestricted)) {
        setNewRestrictedAddressToAdd.erase(newRestricted);
    }

    setNewRestrictedAddressToAdd.insert(newRestricted);

    return true;
}

//! Changes Memory Only, this is only called when undoing a block from the chain
bool CTokensCache::RemoveRestrictedAddress(const std::string& tokenName, const std::string& address, const RestrictedType type)
{
    CTokenCacheRestrictedAddress newRestricted(tokenName, address, type);

    // We are undoing a restricted address transaction, so if the set that contains restricted address from new block
    // contains this restricted address, erase it.
    if (setNewRestrictedAddressToAdd.count(newRestricted)) {
        setNewRestrictedAddressToAdd.erase(newRestricted);
    }

    // If the set of restricted address to undo contains our restricted address already, we need to overwrite it
    if (setNewRestrictedAddressToRemove.count(newRestricted)) {
        setNewRestrictedAddressToRemove.erase(newRestricted);
    }

    setNewRestrictedAddressToRemove.insert(newRestricted);

    return true;
}

//! Changes Memory Only, this only called when adding a block to the chain
bool CTokensCache::AddGlobalRestricted(const std::string& tokenName, const RestrictedType type)
{
    CTokenCacheRestrictedGlobal newGlobalRestriction(tokenName, type);

    // We are adding a global restriction transaction, so if the set the contains undo global restrictions,
    // contains this global restriction, erase it
    if (setNewRestrictedGlobalToRemove.count(newGlobalRestriction)) {
        setNewRestrictedGlobalToRemove.erase(newGlobalRestriction);
    }

    // If the set of global restrictions to add already contains our set, overwrite it
    if (setNewRestrictedGlobalToAdd.count(newGlobalRestriction)) {
        setNewRestrictedGlobalToAdd.erase(newGlobalRestriction);
    }

    setNewRestrictedGlobalToAdd.insert(newGlobalRestriction);

    return true;
}

//! Changes Memory Only, this is only called when undoing a block from the chain
bool CTokensCache::RemoveGlobalRestricted(const std::string& tokenName, const RestrictedType type)
{
    CTokenCacheRestrictedGlobal newGlobalRestriction(tokenName, type);

    // We are undoing a global restriction transaction, so if the set the contains new global restrictions,
    // contains this global restriction, erase it
    if (setNewRestrictedGlobalToAdd.count(newGlobalRestriction)) {
        setNewRestrictedGlobalToAdd.erase(newGlobalRestriction);
    }

    // If the set of global restrictions to undo already contains our set, overwrite it
    if (setNewRestrictedGlobalToRemove.count(newGlobalRestriction)) {
        setNewRestrictedGlobalToRemove.erase(newGlobalRestriction);
    }

    setNewRestrictedGlobalToRemove.insert(newGlobalRestriction);

    return true;
}

//! Changes Memory Only
bool CTokensCache::AddRestrictedVerifier(const std::string& tokenName, const std::string& verifier)
{
    // Insert the reissue information into the reissue map
    CTokenCacheRestrictedVerifiers newVerifier(tokenName, verifier);

    if (setNewRestrictedVerifierToRemove.count(newVerifier))
        setNewRestrictedVerifierToRemove.erase(newVerifier);

    setNewRestrictedVerifierToAdd.insert(newVerifier);

    return true;
}

//! Changes Memory Only
bool CTokensCache::RemoveRestrictedVerifier(const std::string& tokenName, const std::string& verifier, const bool fUndoingReissue)
{
    CTokenCacheRestrictedVerifiers newVerifier(tokenName, verifier);
    newVerifier.fUndoingRessiue = fUndoingReissue;

    if (setNewRestrictedVerifierToAdd.count(newVerifier))
        setNewRestrictedVerifierToAdd.erase(newVerifier);

    setNewRestrictedVerifierToRemove.insert(newVerifier);

    return true;
}

bool CTokensCache::DumpCacheToDatabase()
{
    try {
        bool dirty = false;
        std::string message;

        // Remove new tokens from the database
        for (auto newToken : setNewTokensToRemove) {
            ptokensCache->Erase(newToken.token.strName);
            if (!ptokensdb->EraseTokenData(newToken.token.strName)) {
                dirty = true;
                message = "_Failed Erasing New Token Data from database";
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }

            if (!prestricteddb->EraseVerifier(newToken.token.strName)) {
                dirty = true;
                message = "_Failed Erasing verifier of new token removal data from database";
            }

            if (fTokenIndex) {
                if (!ptokensdb->EraseTokenAddressQuantity(newToken.token.strName, newToken.address)) {
                    dirty = true;
                    message = "_Failed Erasing Address Balance from database";
                }

                if (!ptokensdb->EraseAddressTokenQuantity(newToken.address, newToken.token.strName)) {
                    dirty = true;
                    message = "_Failed Erasing New Token Address Balance from AddressToken database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Add the new tokens to the database
        for (auto newToken : setNewTokensToAdd) {
            ptokensCache->Put(newToken.token.strName, CDatabasedTokenData(newToken.token, newToken.blockHeight, newToken.blockHash));
            if (!ptokensdb->WriteTokenData(newToken.token, newToken.blockHeight, newToken.blockHash)) {
                dirty = true;
                message = "_Failed Writing New Token Data to database";
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }

            if (fTokenIndex) {
                if (!ptokensdb->WriteTokenAddressQuantity(newToken.token.strName, newToken.address,
                                                          newToken.token.nAmount)) {
                    dirty = true;
                    message = "_Failed Writing Address Balance to database";
                }

                if (!ptokensdb->WriteAddressTokenQuantity(newToken.address, newToken.token.strName,
                                                          newToken.token.nAmount)) {
                    dirty = true;
                    message = "_Failed Writing Address Balance to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        if (fTokenIndex) {
            // Remove the new owners from database
            for (auto ownerToken : setNewOwnerTokensToRemove) {
                if (!ptokensdb->EraseTokenAddressQuantity(ownerToken.tokenName, ownerToken.address)) {
                    dirty = true;
                    message = "_Failed Erasing Owner Address Balance from database";
                }

                if (!ptokensdb->EraseAddressTokenQuantity(ownerToken.address, ownerToken.tokenName)) {
                    dirty = true;
                    message = "_Failed Erasing New Owner Address Balance from AddressToken database";
                }

                if (dirty) {
                    return error("%s : %s", __func__, message);
                }
            }

            // Add the new owners to database
            for (auto ownerToken : setNewOwnerTokensToAdd) {
                auto pair = std::make_pair(ownerToken.tokenName, ownerToken.address);
                if (mapTokensAddressAmount.count(pair) && mapTokensAddressAmount.at(pair) > 0) {
                    if (!ptokensdb->WriteTokenAddressQuantity(ownerToken.tokenName, ownerToken.address,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing Owner Address Balance to database";
                    }

                    if (!ptokensdb->WriteAddressTokenQuantity(ownerToken.address, ownerToken.tokenName,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing Address Balance to database";
                    }

                    if (dirty) {
                        return error("%s : %s", __func__, message);
                    }
                }
            }

            // Undo the transfering by updating the balances in the database

            for (auto undoTransfer : setNewTransferTokensToRemove) {
                auto pair = std::make_pair(undoTransfer.transfer.strName, undoTransfer.address);
                if (mapTokensAddressAmount.count(pair)) {
                    if (mapTokensAddressAmount.at(pair) == 0) {
                        if (!ptokensdb->EraseTokenAddressQuantity(undoTransfer.transfer.strName,
                                                                  undoTransfer.address)) {
                            dirty = true;
                            message = "_Failed Erasing Address Quantity from database";
                        }

                        if (!ptokensdb->EraseAddressTokenQuantity(undoTransfer.address,
                                                                  undoTransfer.transfer.strName)) {
                            dirty = true;
                            message = "_Failed Erasing UndoTransfer Address Balance from AddressToken database";
                        }

                        if (dirty) {
                            return error("%s : %s", __func__, message);
                        }
                    } else {
                        if (!ptokensdb->WriteTokenAddressQuantity(undoTransfer.transfer.strName,
                                                                  undoTransfer.address,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Writing updated Address Quantity to database when undoing transfers";
                        }

                        if (!ptokensdb->WriteAddressTokenQuantity(undoTransfer.address,
                                                                  undoTransfer.transfer.strName,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Writing Address Balance to database";
                        }

                        if (dirty) {
                            return error("%s : %s", __func__, message);
                        }
                    }
                }
            }


            // Save the new transfers by updating the quantity in the database
            for (auto newTransfer : setNewTransferTokensToAdd) {
                auto pair = std::make_pair(newTransfer.transfer.strName, newTransfer.address);
                // During init and reindex it disconnects and verifies blocks, can create a state where vNewTransfer will contain transfers that have already been spent. So if they aren't in the map, we can skip them.
                if (mapTokensAddressAmount.count(pair)) {
                    if (!ptokensdb->WriteTokenAddressQuantity(newTransfer.transfer.strName, newTransfer.address,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing new address quantity to database";
                    }

                    if (!ptokensdb->WriteAddressTokenQuantity(newTransfer.address, newTransfer.transfer.strName,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing Address Balance to database";
                    }

                    if (dirty) {
                        return error("%s : %s", __func__, message);
                    }
                }
            }
        }

        for (auto newReissue : setNewReissueToAdd) {
            auto reissue_name = newReissue.reissue.strName;
            auto pair = make_pair(reissue_name, newReissue.address);
            if (mapReissuedTokenData.count(reissue_name)) {
                if(!ptokensdb->WriteTokenData(mapReissuedTokenData.at(reissue_name), newReissue.blockHeight, newReissue.blockHash)) {
                    dirty = true;
                    message = "_Failed Writing reissue token data to database";
                }

                if (dirty) {
                    return error("%s : %s", __func__, message);
                }

                ptokensCache->Erase(reissue_name);

                if (fTokenIndex) {

                    if (mapTokensAddressAmount.count(pair) && mapTokensAddressAmount.at(pair) > 0) {
                        if (!ptokensdb->WriteTokenAddressQuantity(pair.first, pair.second,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Writing reissue token quantity to the address quantity database";
                        }

                        if (!ptokensdb->WriteAddressTokenQuantity(pair.second, pair.first,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Writing Address Balance to database";
                        }

                        if (dirty) {
                            return error("%s, %s", __func__, message);
                        }
                    }
                }
            }
        }

        for (auto undoReissue : setNewReissueToRemove) {
            // In the case the the issue and reissue are both being removed
            // we can skip this call because the removal of the issue should remove all data pertaining the to token
            // Fixes the issue where the reissue data will write over the removed token meta data that was removed above
            CNewToken token(undoReissue.reissue.strName, 0);
            CTokenCacheNewToken testNewTokenCache(token, "", 0 , uint256());
            if (setNewTokensToRemove.count(testNewTokenCache)) {
                continue;
            }

            auto reissue_name = undoReissue.reissue.strName;
            if (mapReissuedTokenData.count(reissue_name)) {
                if(!ptokensdb->WriteTokenData(mapReissuedTokenData.at(reissue_name), undoReissue.blockHeight, undoReissue.blockHash)) {
                    dirty = true;
                    message = "_Failed Writing undo reissue token data to database";
                }

                if (fTokenIndex) {
                    auto pair = make_pair(undoReissue.reissue.strName, undoReissue.address);
                    if (mapTokensAddressAmount.count(pair)) {
                        if (mapTokensAddressAmount.at(pair) == 0) {
                            if (!ptokensdb->EraseTokenAddressQuantity(reissue_name, undoReissue.address)) {
                                dirty = true;
                                message = "_Failed Erasing Address Balance from database";
                            }

                            if (!ptokensdb->EraseAddressTokenQuantity(undoReissue.address, reissue_name)) {
                                dirty = true;
                                message = "_Failed Erasing UndoReissue Balance from AddressToken database";
                            }
                        } else {
                            if (!ptokensdb->WriteTokenAddressQuantity(reissue_name, undoReissue.address,
                                                                      mapTokensAddressAmount.at(pair))) {
                                dirty = true;
                                message = "_Failed Writing the undo of reissue of token from database";
                            }

                            if (!ptokensdb->WriteAddressTokenQuantity(undoReissue.address, reissue_name,
                                                                      mapTokensAddressAmount.at(pair))) {
                                dirty = true;
                                message = "_Failed Writing Address Balance to database";
                            }
                        }
                    }
                }

                if (dirty) {
                    return error("%s : %s", __func__, message);
                }

                ptokensCache->Erase(reissue_name);
            }
        }

        // Add new verifier strings for restricted tokens
        for (auto newVerifier : setNewRestrictedVerifierToAdd) {
            auto tokenName = newVerifier.tokenName;
            if (!prestricteddb->WriteVerifier(tokenName, newVerifier.verifier)) {
                dirty = true;
                message = "_Failed Writing restricted verifier to database";
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }

            ptokensVerifierCache->Erase(tokenName);
        }

        // Undo verifier string for restricted tokens
        for (auto undoVerifiers : setNewRestrictedVerifierToRemove) {
            auto tokenName = undoVerifiers.tokenName;

            // If we are undoing a reissue, we need to save back the old verifier string to database
            if (undoVerifiers.fUndoingRessiue) {
                if (!prestricteddb->WriteVerifier(tokenName, undoVerifiers.verifier)) {
                    dirty = true;
                    message = "_Failed Writing undo restricted verifer to database";
                }
            } else {
                if (!prestricteddb->EraseVerifier(tokenName)) {
                    dirty = true;
                    message = "_Failed Writing undo restricted verifer to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }

            ptokensVerifierCache->Erase(tokenName);
        }

        // Add the new qualifier commands to the database
        for (auto newQualifierAddress : setNewQualifierAddressToAdd) {
            if (newQualifierAddress.type == QualifierType::REMOVE_QUALIFIER) {
                ptokensQualifierCache->Erase(newQualifierAddress.GetHash().GetHex());
                if (!prestricteddb->EraseAddressQualifier(newQualifierAddress.address, newQualifierAddress.tokenName)) {
                    dirty = true;
                    message = "_Failed Erasing address qualifier from database";
                }
                if (fTokenIndex && !dirty) {
                    if (!prestricteddb->EraseQualifierAddress(newQualifierAddress.address,
                                                              newQualifierAddress.tokenName)) {
                        dirty = true;
                        message = "_Failed Erasing qualifier address from database";
                    }
                }
            } else if (newQualifierAddress.type == QualifierType::ADD_QUALIFIER) {
                ptokensQualifierCache->Put(newQualifierAddress.GetHash().GetHex(), 1);
                if (!prestricteddb->WriteAddressQualifier(newQualifierAddress.address, newQualifierAddress.tokenName))
                {
                    dirty = true;
                    message = "_Failed Writing address qualifier to database";
                }
                if (fTokenIndex & !dirty) {
                    if (!prestricteddb->WriteQualifierAddress(newQualifierAddress.address, newQualifierAddress.tokenName))
                    {
                        dirty = true;
                        message = "_Failed Writing qualifier address to database";
                    }
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Undo the qualifier commands
        for (auto undoQualifierAddress : setNewQualifierAddressToRemove) {
            if (undoQualifierAddress.type == QualifierType::REMOVE_QUALIFIER) { // If we are undoing a removal, we write the data to database
                ptokensQualifierCache->Put(undoQualifierAddress.GetHash().GetHex(), 1);
                if (!prestricteddb->WriteAddressQualifier(undoQualifierAddress.address, undoQualifierAddress.tokenName)) {
                    dirty = true;
                    message = "_Failed undoing a removal of a address qualifier  from database";
                }
                if (fTokenIndex & !dirty) {
                    if (!prestricteddb->WriteQualifierAddress(undoQualifierAddress.address, undoQualifierAddress.tokenName))
                    {
                        dirty = true;
                        message = "_Failed undoing a removal of a qualifier address from database";
                    }
                }
            } else if (undoQualifierAddress.type == QualifierType::ADD_QUALIFIER) { // If we are undoing an addition, we remove the data from the database
                ptokensQualifierCache->Erase(undoQualifierAddress.GetHash().GetHex());
                if (!prestricteddb->EraseAddressQualifier(undoQualifierAddress.address, undoQualifierAddress.tokenName))
                {
                    dirty = true;
                    message = "_Failed undoing a addition of a address qualifier to database";
                }
                if (fTokenIndex && !dirty) {
                    if (!prestricteddb->EraseQualifierAddress(undoQualifierAddress.address,
                                                              undoQualifierAddress.tokenName)) {
                        dirty = true;
                        message = "_Failed undoing a addition of a qualifier address from database";
                    }
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Add new restricted address commands
        for (auto newRestrictedAddress : setNewRestrictedAddressToAdd) {
            if (newRestrictedAddress.type == RestrictedType::UNFREEZE_ADDRESS) {
                ptokensRestrictionCache->Erase(newRestrictedAddress.GetHash().GetHex());
                if (!prestricteddb->EraseRestrictedAddress(newRestrictedAddress.address, newRestrictedAddress.tokenName)) {
                    dirty = true;
                    message = "_Failed Erasing restricted address from database";
                }
            } else if (newRestrictedAddress.type == RestrictedType::FREEZE_ADDRESS) {
                ptokensRestrictionCache->Put(newRestrictedAddress.GetHash().GetHex(), 1);
                if (!prestricteddb->WriteRestrictedAddress(newRestrictedAddress.address, newRestrictedAddress.tokenName))
                {
                    dirty = true;
                    message = "_Failed Writing restricted address to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Undo the qualifier addresses from database
        for (auto undoRestrictedAddress : setNewRestrictedAddressToRemove) {
            if (undoRestrictedAddress.type == RestrictedType::UNFREEZE_ADDRESS) { // If we are undoing an unfreeze, we need to freeze the address
                ptokensRestrictionCache->Put(undoRestrictedAddress.GetHash().GetHex(), 1);
                if (!prestricteddb->WriteRestrictedAddress(undoRestrictedAddress.address, undoRestrictedAddress.tokenName)) {
                    dirty = true;
                    message = "_Failed undoing a removal of a restricted address from database";
                }
            } else if (undoRestrictedAddress.type == RestrictedType::FREEZE_ADDRESS) { // If we are undoing a freeze, we need to unfreeze the address
                ptokensRestrictionCache->Erase(undoRestrictedAddress.GetHash().GetHex());
                if (!prestricteddb->EraseRestrictedAddress(undoRestrictedAddress.address, undoRestrictedAddress.tokenName))
                {
                    dirty = true;
                    message = "_Failed undoing a addition of a restricted address to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Add new global restriction commands
        for (auto newGlobalRestriction : setNewRestrictedGlobalToAdd) {
            if (newGlobalRestriction.type == RestrictedType::GLOBAL_UNFREEZE) {
                ptokensGlobalRestrictionCache->Erase(newGlobalRestriction.tokenName);
                if (!prestricteddb->EraseGlobalRestriction(newGlobalRestriction.tokenName)) {
                    dirty = true;
                    message = "_Failed Erasing global restriction from database";
                }
            } else if (newGlobalRestriction.type == RestrictedType::GLOBAL_FREEZE) {
                ptokensGlobalRestrictionCache->Put(newGlobalRestriction.tokenName, 1);
                if (!prestricteddb->WriteGlobalRestriction(newGlobalRestriction.tokenName))
                {
                    dirty = true;
                    message = "_Failed Writing global restriction to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        // Undo the global restriction commands
        for (auto undoGlobalRestriction : setNewRestrictedGlobalToRemove) {
            if (undoGlobalRestriction.type == RestrictedType::GLOBAL_UNFREEZE) { // If we are undoing an global unfreeze, we need to write a global freeze
                ptokensGlobalRestrictionCache->Put(undoGlobalRestriction.tokenName, 1);
                if (!prestricteddb->WriteGlobalRestriction(undoGlobalRestriction.tokenName)) {
                    dirty = true;
                    message = "_Failed undoing a global unfreeze of a restricted token from database";
                }
            } else if (undoGlobalRestriction.type == RestrictedType::GLOBAL_FREEZE) { // If we are undoing a global freeze, erase the freeze from the database
                ptokensGlobalRestrictionCache->Erase(undoGlobalRestriction.tokenName);
                if (!prestricteddb->EraseGlobalRestriction(undoGlobalRestriction.tokenName))
                {
                    dirty = true;
                    message = "_Failed undoing a global freeze of a restricted token to database";
                }
            }

            if (dirty) {
                return error("%s : %s", __func__, message);
            }
        }

        if (fTokenIndex) {
            // Undo the token spends by updating there balance in the database
            for (auto undoSpend : vUndoTokenAmount) {
                auto pair = std::make_pair(undoSpend.tokenName, undoSpend.address);
                if (mapTokensAddressAmount.count(pair)) {
                    if (!ptokensdb->WriteTokenAddressQuantity(undoSpend.tokenName, undoSpend.address,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing updated Address Quantity to database when undoing spends";
                    }

                    if (!ptokensdb->WriteAddressTokenQuantity(undoSpend.address, undoSpend.tokenName,
                                                              mapTokensAddressAmount.at(pair))) {
                        dirty = true;
                        message = "_Failed Writing Address Balance to database";
                    }

                    if (dirty) {
                        return error("%s : %s", __func__, message);
                    }
                }
            }


            // Save the tokens that have been spent by erasing the quantity in the database
            for (auto spentToken : vSpentTokens) {
                auto pair = make_pair(spentToken.tokenName, spentToken.address);
                if (mapTokensAddressAmount.count(pair)) {
                    if (mapTokensAddressAmount.at(pair) == 0) {
                        if (!ptokensdb->EraseTokenAddressQuantity(spentToken.tokenName, spentToken.address)) {
                            dirty = true;
                            message = "_Failed Erasing a Spent Token, from database";
                        }

                        if (!ptokensdb->EraseAddressTokenQuantity(spentToken.address, spentToken.tokenName)) {
                            dirty = true;
                            message = "_Failed Erasing a Spent Token from AddressToken database";
                        }

                        if (dirty) {
                            return error("%s : %s", __func__, message);
                        }
                    } else {
                        if (!ptokensdb->WriteTokenAddressQuantity(spentToken.tokenName, spentToken.address,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Erasing a Spent Token, from database";
                        }

                        if (!ptokensdb->WriteAddressTokenQuantity(spentToken.address, spentToken.tokenName,
                                                                  mapTokensAddressAmount.at(pair))) {
                            dirty = true;
                            message = "_Failed Writing Address Balance to database";
                        }

                        if (dirty) {
                            return error("%s : %s", __func__, message);
                        }
                    }
                }
            }
        }

        ClearDirtyCache();

        return true;
    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing tokens: ") + e.what());
    }
}

// This function will put all current cache data into the global ptokens cache.
//! Do not call this function on the ptokens pointer
bool CTokensCache::Flush()
{

    if (!ptokens)
        return error("%s: Couldn't find ptokens pointer while trying to flush tokens cache", __func__);

    try {
        for (auto &item : setNewTokensToAdd) {
            if (ptokens->setNewTokensToRemove.count(item))
                ptokens->setNewTokensToRemove.erase(item);
            ptokens->setNewTokensToAdd.insert(item);
        }

        for (auto &item : setNewTokensToRemove) {
            if (ptokens->setNewTokensToAdd.count(item))
                ptokens->setNewTokensToAdd.erase(item);
            ptokens->setNewTokensToRemove.insert(item);
        }

        for (auto &item : mapTokensAddressAmount)
            ptokens->mapTokensAddressAmount[item.first] = item.second;

        for (auto &item : mapReissuedTokenData)
            ptokens->mapReissuedTokenData[item.first] = item.second;

        for (auto &item : setNewOwnerTokensToAdd) {
            if (ptokens->setNewOwnerTokensToRemove.count(item))
                ptokens->setNewOwnerTokensToRemove.erase(item);
            ptokens->setNewOwnerTokensToAdd.insert(item);
        }

        for (auto &item : setNewOwnerTokensToRemove) {
            if (ptokens->setNewOwnerTokensToAdd.count(item))
                ptokens->setNewOwnerTokensToAdd.erase(item);
            ptokens->setNewOwnerTokensToRemove.insert(item);
        }

        for (auto &item : setNewReissueToAdd) {
            if (ptokens->setNewReissueToRemove.count(item))
                ptokens->setNewReissueToRemove.erase(item);
            ptokens->setNewReissueToAdd.insert(item);
        }

        for (auto &item : setNewReissueToRemove) {
            if (ptokens->setNewReissueToAdd.count(item))
                ptokens->setNewReissueToAdd.erase(item);
            ptokens->setNewReissueToRemove.insert(item);
        }

        for (auto &item : setNewTransferTokensToAdd) {
            if (ptokens->setNewTransferTokensToRemove.count(item))
                ptokens->setNewTransferTokensToRemove.erase(item);
            ptokens->setNewTransferTokensToAdd.insert(item);
        }

        for (auto &item : setNewTransferTokensToRemove) {
            if (ptokens->setNewTransferTokensToAdd.count(item))
                ptokens->setNewTransferTokensToAdd.erase(item);
            ptokens->setNewTransferTokensToRemove.insert(item);
        }

        for (auto &item : vSpentTokens) {
            ptokens->vSpentTokens.emplace_back(item);
        }

        for (auto &item : vUndoTokenAmount) {
            ptokens->vUndoTokenAmount.emplace_back(item);
        }

        for(auto &item : setNewQualifierAddressToAdd) {
            if (ptokens->setNewQualifierAddressToRemove.count(item)) {
                ptokens->setNewQualifierAddressToRemove.erase(item);
            }

            if (ptokens->setNewQualifierAddressToAdd.count(item)) {
                ptokens->setNewQualifierAddressToAdd.erase(item);
            }

            ptokens->setNewQualifierAddressToAdd.insert(item);
        }

        for(auto &item : setNewQualifierAddressToRemove) {
            if (ptokens->setNewQualifierAddressToAdd.count(item)) {
                ptokens->setNewQualifierAddressToAdd.erase(item);
            }

            if (ptokens->setNewQualifierAddressToRemove.count(item)) {
                ptokens->setNewQualifierAddressToRemove.erase(item);
            }

            ptokens->setNewQualifierAddressToRemove.insert(item);
        }

        for(auto &item : setNewRestrictedAddressToAdd) {
            if (ptokens->setNewRestrictedAddressToRemove.count(item)) {
                ptokens->setNewRestrictedAddressToRemove.erase(item);
            }

            if (ptokens->setNewRestrictedAddressToAdd.count(item)) {
                ptokens->setNewRestrictedAddressToAdd.erase(item);
            }

            ptokens->setNewRestrictedAddressToAdd.insert(item);
        }

        for(auto &item : setNewRestrictedAddressToRemove) {
            if (ptokens->setNewRestrictedAddressToAdd.count(item)) {
                ptokens->setNewRestrictedAddressToAdd.erase(item);
            }

            if (ptokens->setNewRestrictedAddressToRemove.count(item)) {
                ptokens->setNewRestrictedAddressToRemove.erase(item);
            }

            ptokens->setNewRestrictedAddressToRemove.insert(item);
        }

        for(auto &item : setNewRestrictedGlobalToAdd) {
            if (ptokens->setNewRestrictedGlobalToRemove.count(item)) {
                ptokens->setNewRestrictedGlobalToRemove.erase(item);
            }

            if (ptokens->setNewRestrictedGlobalToAdd.count(item)) {
                ptokens->setNewRestrictedGlobalToAdd.erase(item);
            }

            ptokens->setNewRestrictedGlobalToAdd.insert(item);
        }

        for(auto &item : setNewRestrictedGlobalToRemove) {
            if (ptokens->setNewRestrictedGlobalToAdd.count(item)) {
                ptokens->setNewRestrictedGlobalToAdd.erase(item);
            }

            if (ptokens->setNewRestrictedGlobalToRemove.count(item)) {
                ptokens->setNewRestrictedGlobalToRemove.erase(item);
            }

            ptokens->setNewRestrictedGlobalToRemove.insert(item);
        }

        for (auto &item : setNewRestrictedVerifierToAdd) {
            if (ptokens->setNewRestrictedVerifierToRemove.count(item)) {
                ptokens->setNewRestrictedVerifierToRemove.erase(item);
            }

            if (ptokens->setNewRestrictedVerifierToAdd.count(item)) {
                ptokens->setNewRestrictedVerifierToAdd.erase(item);
            }

            ptokens->setNewRestrictedVerifierToAdd.insert(item);
        }

        for (auto &item : setNewRestrictedVerifierToRemove) {
            if (ptokens->setNewRestrictedVerifierToAdd.count(item)) {
                ptokens->setNewRestrictedVerifierToAdd.erase(item);
            }

            if (ptokens->setNewRestrictedVerifierToRemove.count(item)) {
                ptokens->setNewRestrictedVerifierToRemove.erase(item);
            }

            ptokens->setNewRestrictedVerifierToRemove.insert(item);
        }

        for (auto &item : mapRootQualifierAddressesAdd) {
            for (auto token : item.second) {
                ptokens->mapRootQualifierAddressesAdd[item.first].insert(token);
            }
        }

        for (auto &item : mapRootQualifierAddressesRemove) {
            for (auto token : item.second) {
                ptokens->mapRootQualifierAddressesAdd[item.first].insert(token);
            }
        }

        return true;

    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing tokens: ") + e.what());
    }
}

//! Get the amount of memory the cache is using
size_t CTokensCache::DynamicMemoryUsage() const
{
    // TODO make sure this is accurate
    return memusage::DynamicUsage(mapTokensAddressAmount) + memusage::DynamicUsage(mapReissuedTokenData);
}

//! Get an estimated size of the cache in bytes that will be needed inorder to save to database
size_t CTokensCache::GetCacheSize() const
{
    // COutPoint: 32 bytes
    // CNewToken: Max 80 bytes
    // CTokenTransfer: Token Name, CAmount ( 40 bytes)
    // CReissueToken: Max 80 bytes
    // CAmount: 8 bytes
    // Token Name: Max 32 bytes
    // Address: 40 bytes
    // Block hash: 32 bytes
    // CTxOut: CAmount + CScript (105 + 8 = 113 bytes)

    size_t size = 0;

    size += (32 + 40 + 8) * vUndoTokenAmount.size(); // Token Name, Address, CAmount

    size += (40 + 40 + 32) * setNewTransferTokensToRemove.size(); // CTokenTrasnfer, Address, COutPoint
    size += (40 + 40 + 32) * setNewTransferTokensToAdd.size(); // CTokenTrasnfer, Address, COutPoint

    size += 72 * setNewOwnerTokensToAdd.size(); // Token Name, Address
    size += 72 * setNewOwnerTokensToRemove.size(); // Token Name, Address

    size += (32 + 40 + 8) * vSpentTokens.size(); // Token Name, Address, CAmount

    size += (80 + 40 + 32 + sizeof(int)) * setNewTokensToAdd.size(); // CNewToken, Address, Block hash, int
    size += (80 + 40 + 32 + sizeof(int)) * setNewTokensToRemove.size(); // CNewToken, Address, Block hash, int

    size += (80 + 40 + 32 + 32 + sizeof(int)) * setNewReissueToAdd.size(); // CReissueToken, Address, COutPoint, Block hash, int
    size += (80 + 40 + 32 + 32 + sizeof(int)) * setNewReissueToRemove.size(); // CReissueToken, Address, COutPoint, Block hash, int

    // TODO add the qualfier, and restricted sets into this calculation

    return size;
}

//! Get an estimated size of the cache in bytes that will be needed inorder to save to database
size_t CTokensCache::GetCacheSizeV2() const
{
    // COutPoint: 32 bytes
    // CNewToken: Max 80 bytes
    // CTokenTransfer: Token Name, CAmount ( 40 bytes)
    // CReissueToken: Max 80 bytes
    // CAmount: 8 bytes
    // Token Name: Max 32 bytes
    // Address: 40 bytes
    // Block hash: 32 bytes
    // CTxOut: CAmount + CScript (105 + 8 = 113 bytes)

    size_t size = 0;
    size += memusage::DynamicUsage(vUndoTokenAmount);
    size += memusage::DynamicUsage(setNewTransferTokensToRemove);
    size += memusage::DynamicUsage(setNewTransferTokensToAdd);
    size += memusage::DynamicUsage(setNewOwnerTokensToAdd);
    size += memusage::DynamicUsage(setNewOwnerTokensToRemove);
    size += memusage::DynamicUsage(vSpentTokens);
    size += memusage::DynamicUsage(setNewTokensToAdd);
    size += memusage::DynamicUsage(setNewTokensToRemove);
    size += memusage::DynamicUsage(setNewReissueToAdd);
    size += memusage::DynamicUsage(setNewReissueToRemove);

    return size;
}

bool CheckIssueBurnTx(const CTxOut& txOut, const KnownTokenType& type, const int numberIssued)
{
    if (type == KnownTokenType::REISSUE || type == KnownTokenType::VOTE || type == KnownTokenType::OWNER || type == KnownTokenType::INVALID)
        return false;

    CAmount burnAmount = 0;
    std::string burnAddress = "";

    // Get the burn address and amount for the type of token
    burnAmount = GetBurnAmount(type);
    burnAddress = GetBurnAddress(type);

    // If issuing multiple (unique) tokens need to burn for each
    burnAmount *= numberIssued;

    // Check if script satisfies the burn amount
    if (!(txOut.nValue == burnAmount))
        return false;

    // Extract the destination
    CTxDestination destination;
    if (!ExtractDestination(txOut.scriptPubKey, destination))
        return false;

    // Verify destination is valid
    if (!IsValidDestination(destination))
        return false;

    // Check destination address is the burn address
    auto strDestination = EncodeDestination(destination);
    if (!(strDestination == burnAddress))
        return false;

    return true;
}

bool CheckIssueBurnTx(const CTxOut& txOut, const KnownTokenType& type)
{
    return CheckIssueBurnTx(txOut, type, 1);
}

bool CheckReissueBurnTx(const CTxOut& txOut)
{
    // Check the first transaction and verify that the correct YONA Amount
    if (txOut.nValue != GetReissueTokenBurnAmount())
        return false;

    // Extract the destination
    CTxDestination destination;
    if (!ExtractDestination(txOut.scriptPubKey, destination))
        return false;

    // Verify destination is valid
    if (!IsValidDestination(destination))
        return false;

    // Check destination address is the correct burn address
    if (EncodeDestination(destination) != GetParams().ReissueTokenBurnAddress())
        return false;

    return true;
}

bool CheckIssueDataTx(const CTxOut& txOut)
{
    // Verify 'yonaq' is in the transaction
    CScript scriptPubKey = txOut.scriptPubKey;

    int nStartingIndex = 0;
    return IsScriptNewToken(scriptPubKey, nStartingIndex);
}

bool CheckReissueDataTx(const CTxOut& txOut)
{
    // Verify 'yonar' is in the transaction
    CScript scriptPubKey = txOut.scriptPubKey;

    return IsScriptReissueToken(scriptPubKey);
}

bool CheckOwnerDataTx(const CTxOut& txOut)
{
    // Verify 'yonaq' is in the transaction
    CScript scriptPubKey = txOut.scriptPubKey;

    return IsScriptOwnerToken(scriptPubKey);
}

bool CheckTransferOwnerTx(const CTxOut& txOut)
{
    // Verify 'yonaq' is in the transaction
    CScript scriptPubKey = txOut.scriptPubKey;

    return IsScriptTransferToken(scriptPubKey);
}

bool IsScriptNewToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptNewToken(scriptPubKey, index);
}

bool IsScriptNewToken(const CScript& scriptPubKey, int& nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex)) {
        return nType == TX_NEW_TOKEN && !fIsOwner;
    }
    return false;
}

bool IsScriptNewUniqueToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptNewUniqueToken(scriptPubKey, index);
}

bool IsScriptNewUniqueToken(const CScript &scriptPubKey, int &nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (!scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex))
        return false;

    CNewToken token;
    std::string address;
    if (!TokenFromScript(scriptPubKey, token, address))
        return false;

    KnownTokenType tokenType;
    if (!IsTokenNameValid(token.strName, tokenType))
        return false;

    return KnownTokenType::UNIQUE == tokenType;
}

bool IsScriptNewMsgChannelToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptNewMsgChannelToken(scriptPubKey, index);
}

bool IsScriptNewMsgChannelToken(const CScript &scriptPubKey, int &nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (!scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex))
        return false;

    CNewToken token;
    std::string address;
    if (!TokenFromScript(scriptPubKey, token, address))
        return false;

    KnownTokenType tokenType;
    if (!IsTokenNameValid(token.strName, tokenType))
        return false;

    return KnownTokenType::MSGCHANNEL == tokenType;
}

bool IsScriptOwnerToken(const CScript& scriptPubKey)
{

    int index = 0;
    return IsScriptOwnerToken(scriptPubKey, index);
}

bool IsScriptOwnerToken(const CScript& scriptPubKey, int& nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner =false;
    if (scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex)) {
        return nType == TX_NEW_TOKEN && fIsOwner;
    }

    return false;
}

bool IsScriptReissueToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptReissueToken(scriptPubKey, index);
}

bool IsScriptReissueToken(const CScript& scriptPubKey, int& nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner =false;
    if (scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex)) {
        return nType == TX_REISSUE_TOKEN;
    }

    return false;
}

bool IsScriptTransferToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptTransferToken(scriptPubKey, index);
}

bool IsScriptTransferToken(const CScript& scriptPubKey, int& nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex)) {
        return nType == TX_TRANSFER_TOKEN;
    }

    return false;
}

bool IsScriptNewQualifierToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptNewQualifierToken(scriptPubKey, index);
}

bool IsScriptNewQualifierToken(const CScript &scriptPubKey, int &nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (!scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex))
        return false;

    CNewToken token;
    std::string address;
    if (!TokenFromScript(scriptPubKey, token, address))
        return false;

    KnownTokenType tokenType;
    if (!IsTokenNameValid(token.strName, tokenType))
        return false;

    return KnownTokenType::QUALIFIER == tokenType || KnownTokenType::SUB_QUALIFIER == tokenType;
}

bool IsScriptNewRestrictedToken(const CScript& scriptPubKey)
{
    int index = 0;
    return IsScriptNewRestrictedToken(scriptPubKey, index);
}

bool IsScriptNewRestrictedToken(const CScript &scriptPubKey, int &nStartingIndex)
{
    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (!scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, nStartingIndex))
        return false;

    CNewToken token;
    std::string address;
    if (!TokenFromScript(scriptPubKey, token, address))
        return false;

    KnownTokenType tokenType;
    if (!IsTokenNameValid(token.strName, tokenType))
        return false;

    return KnownTokenType::RESTRICTED == tokenType;
}


//! Returns a boolean on if the token exists
bool CTokensCache::CheckIfTokenExists(const std::string& name, bool fForceDuplicateCheck)
{
    // If we are reindexing, we don't know if an token exists when accepting blocks
    if (fReindex) {
        return true;
    }

    // Create objects that will be used to check the dirty cache
    CNewToken token;
    token.strName = name;
    CTokenCacheNewToken cachedToken(token, "", 0, uint256());

    // Check the dirty caches first and see if it was recently added or removed
    if (setNewTokensToRemove.count(cachedToken)) {
        return false;
    }

    // Check the dirty caches first and see if it was recently added or removed
    if (ptokens->setNewTokensToRemove.count(cachedToken)) {
        return false;
    }

    if (setNewTokensToAdd.count(cachedToken)) {
        if (fForceDuplicateCheck) {
            return true;
        }
        else {
            LogPrintf("%s : Found token %s in setNewTokensToAdd but force duplicate check wasn't true\n", __func__, name);
        }
    }

    if (ptokens->setNewTokensToAdd.count(cachedToken)) {
        if (fForceDuplicateCheck) {
            return true;
        }
        else {
            LogPrintf("%s : Found token %s in setNewTokensToAdd but force duplicate check wasn't true\n", __func__, name);
        }
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensCache) {
        if (ptokensCache->Exists(name)) {
            if (fForceDuplicateCheck) {
                return true;
            }
            else {
                LogPrintf("%s : Found token %s in ptokensCache but force duplicate check wasn't true\n", __func__, name);
            }
        } else {
            if (ptokensdb) {
                CNewToken readToken;
                int nHeight;
                uint256 hash;
                if (ptokensdb->ReadTokenData(name, readToken, nHeight, hash)) {
                    ptokensCache->Put(readToken.strName, CDatabasedTokenData(readToken, nHeight, hash));
                    if (fForceDuplicateCheck) {
                        return true;
                    }
                    else {
                        LogPrintf("%s : Found token %s in ptokensdb but force duplicate check wasn't true\n", __func__, name);
                    }
                }
            }
        }
    }
    return false;
}

bool CTokensCache::GetTokenMetaDataIfExists(const std::string &name, CNewToken &token)
{
    int height;
    uint256 hash;
    return GetTokenMetaDataIfExists(name, token, height, hash);
}

bool CTokensCache::GetTokenMetaDataIfExists(const std::string &name, CNewToken &token, int& nHeight, uint256& blockHash)
{
    // Check the map that contains the reissued token data. If it is in this map, it hasn't been saved to disk yet
    if (mapReissuedTokenData.count(name)) {
        token = mapReissuedTokenData.at(name);
        return true;
    }

    // Check the map that contains the reissued token data. If it is in this map, it hasn't been saved to disk yet
    if (ptokens->mapReissuedTokenData.count(name)) {
        token = ptokens->mapReissuedTokenData.at(name);
        return true;
    }

    // Create objects that will be used to check the dirty cache
    CNewToken tempToken;
    tempToken.strName = name;
    CTokenCacheNewToken cachedToken(tempToken, "", 0, uint256());

    // Check the dirty caches first and see if it was recently added or removed
    if (setNewTokensToRemove.count(cachedToken)) {
        LogPrintf("%s : Found in new tokens to Remove - Returning False\n", __func__);
        return false;
    }

    // Check the dirty caches first and see if it was recently added or removed
    if (ptokens->setNewTokensToRemove.count(cachedToken)) {
        LogPrintf("%s : Found in new tokens to Remove - Returning False\n", __func__);
        return false;
    }

    auto setIterator = setNewTokensToAdd.find(cachedToken);
    if (setIterator != setNewTokensToAdd.end()) {
        token = setIterator->token;
        nHeight = setIterator->blockHeight;
        blockHash = setIterator->blockHash;
        return true;
    }

    setIterator = ptokens->setNewTokensToAdd.find(cachedToken);
    if (setIterator != ptokens->setNewTokensToAdd.end()) {
        token = setIterator->token;
        nHeight = setIterator->blockHeight;
        blockHash = setIterator->blockHash;
        return true;
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensCache) {
        if (ptokensCache->Exists(name)) {
            CDatabasedTokenData data;
            data = ptokensCache->Get(name);
            token = data.token;
            nHeight = data.nHeight;
            blockHash = data.blockHash;
            return true;
        }
    }

    if (ptokensdb && ptokensCache) {
        CNewToken readToken;
        int height;
        uint256 hash;
        if (ptokensdb->ReadTokenData(name, readToken, height, hash)) {
            token = readToken;
            nHeight = height;
            blockHash = hash;
            ptokensCache->Put(readToken.strName, CDatabasedTokenData(readToken, height, hash));
            return true;
        }
    }

    LogPrintf("%s : Didn't find token meta data anywhere. Returning False\n", __func__);
    return false;
}

bool GetTokenInfoFromScript(const CScript& scriptPubKey, std::string& strName, CAmount& nAmount, uint32_t& nTimeLock)
{
    CTokenOutputEntry data;
    if(!GetTokenData(scriptPubKey, data))
        return false;

    strName = data.tokenName;
    nAmount = data.nAmount;
    nTimeLock = data.nTimeLock;

    return true;
}

bool GetTokenInfoFromCoin(const Coin& coin, std::string& strName, CAmount& nAmount, uint32_t& nTimeLock)
{
    return GetTokenInfoFromScript(coin.out.scriptPubKey, strName, nAmount, nTimeLock);
}

bool GetTokenData(const CScript& script, CTokenOutputEntry& data)
{
    // Placeholder strings that will get set if you successfully get the transfer or token from the script
    std::string address = "";
    std::string tokenName = "";

    int nType = 0;
    int nScriptType = 0;
    bool fIsOwner = false;
    if (!script.IsTokenScript(nType, nScriptType, fIsOwner)) {
        return false;
    }

    txnouttype type = txnouttype(nType);
    txnouttype scriptType = txnouttype(nScriptType);
    data.scriptType = scriptType;

    // Get the New Token or Transfer Token from the scriptPubKey
    if (type == TX_NEW_TOKEN && !fIsOwner) {
        CNewToken token;
        data.nTimeLock = 0;

        if (TokenFromScript(script, token, address)) {
            data.type = TX_NEW_TOKEN;
            data.nAmount = token.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = token.strName;
            return true;
        } else if (MsgChannelTokenFromScript(script, token, address)) {
            data.type = TX_NEW_TOKEN;
            data.nAmount = token.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = token.strName;
        } else if (QualifierTokenFromScript(script, token, address)) {
            data.type = TX_NEW_TOKEN;
            data.nAmount = token.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = token.strName;
        } else if (RestrictedTokenFromScript(script, token, address)) {
            data.type = TX_NEW_TOKEN;
            data.nAmount = token.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = token.strName;
        }
    } else if (type == TX_TRANSFER_TOKEN) {
        CTokenTransfer transfer;
        if (TransferTokenFromScript(script, transfer, address)) {
            data.type = TX_TRANSFER_TOKEN;
            data.nAmount = transfer.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = transfer.strName;
            data.nTimeLock = transfer.nTimeLock;
            data.message = transfer.message;
            data.expireTime = transfer.nExpireTime;
            return true;
        } else {
            LogPrintf("Failed to get transfer from script\n");
        }
    } else if (type == TX_NEW_TOKEN && fIsOwner) {
        data.nTimeLock = 0;

        if (OwnerTokenFromScript(script, tokenName, address)) {
            data.type = TX_NEW_TOKEN;
            data.nAmount = OWNER_TOKEN_AMOUNT;
            data.destination = DecodeDestination(address);
            data.tokenName = tokenName;
            return true;
        }
    } else if (type == TX_REISSUE_TOKEN) {
        CReissueToken reissue;
        data.nTimeLock = 0;

        if (ReissueTokenFromScript(script, reissue, address)) {
            data.type = TX_REISSUE_TOKEN;
            data.nAmount = reissue.nAmount;
            data.destination = DecodeDestination(address);
            data.tokenName = reissue.strName;
            return true;
        }
    }

    return false;
}

#ifdef ENABLE_WALLET
void GetAllAdministrativeTokens(CWallet *pwallet, std::vector<std::string> &names, int nMinConf)
{
    if(!pwallet)
        return;

    GetAllMyTokens(pwallet, names, nMinConf, true, true);
}

void GetAllMyTokens(CWallet* pwallet, std::vector<std::string>& names, int nMinConf, bool fIncludeAdministrator, bool fOnlyAdministrator)
{
    if(!pwallet)
        return;

    std::map<std::string, std::vector<COutput> > mapTokens;
    pwallet->AvailableTokens(mapTokens, true, nullptr, 1, MAX_MONEY, MAX_MONEY, 0, nMinConf); // Set the mincof, set the rest to the defaults

    for (auto item : mapTokens) {
        bool isOwner = IsTokenNameAnOwner(item.first);

        if (isOwner) {
            if (fOnlyAdministrator || fIncludeAdministrator)
                names.emplace_back(item.first);
        } else {
            if (fOnlyAdministrator)
                continue;
            names.emplace_back(item.first);
        }
    }
}
#endif

CAmount GetIssueTokenBurnAmount()
{
    return GetParams().IssueTokenBurnAmount();
}

CAmount GetReissueTokenBurnAmount()
{
    return GetParams().ReissueTokenBurnAmount();
}

CAmount GetIssueSubTokenBurnAmount()
{
    return GetParams().IssueSubTokenBurnAmount();
}

CAmount GetIssueUniqueTokenBurnAmount()
{
    return GetParams().IssueUniqueTokenBurnAmount();
}

CAmount GetIssueMsgChannelTokenBurnAmount()
{
    return GetParams().IssueMsgChannelTokenBurnAmount();
}

CAmount GetIssueQualifierTokenBurnAmount()
{
    return GetParams().IssueQualifierTokenBurnAmount();
}

CAmount GetIssueSubQualifierTokenBurnAmount()
{
    return GetParams().IssueSubQualifierTokenBurnAmount();
}

CAmount GetIssueRestrictedTokenBurnAmount()
{
    return GetParams().IssueRestrictedTokenBurnAmount();
}

CAmount GetAddNullQualifierTagBurnAmount()
{
    return GetParams().AddNullQualifierTagBurnAmount();
}

CAmount GetBurnAmount(const int nType)
{
    return GetBurnAmount((KnownTokenType(nType)));
}

CAmount GetBurnAmount(const KnownTokenType type)
{
    switch (type) {
        case KnownTokenType::ROOT:
            return GetIssueTokenBurnAmount();
        case KnownTokenType::SUB:
            return GetIssueSubTokenBurnAmount();
        case KnownTokenType::MSGCHANNEL:
            return GetIssueMsgChannelTokenBurnAmount();
        case KnownTokenType::OWNER:
            return 0;
        case KnownTokenType::UNIQUE:
            return GetIssueUniqueTokenBurnAmount();
        case KnownTokenType::VOTE:
            return 0;
        case KnownTokenType::REISSUE:
            return GetReissueTokenBurnAmount();
        case KnownTokenType::QUALIFIER:
            return GetIssueQualifierTokenBurnAmount();
        case KnownTokenType::SUB_QUALIFIER:
            return GetIssueSubQualifierTokenBurnAmount();
        case KnownTokenType::RESTRICTED:
            return GetIssueRestrictedTokenBurnAmount();
        case KnownTokenType::NULL_ADD_QUALIFIER:
            return GetAddNullQualifierTagBurnAmount();
        default:
            return 0;
    }
}

std::string GetBurnAddress(const int nType)
{
    return GetBurnAddress((KnownTokenType(nType)));
}

std::string GetBurnAddress(const KnownTokenType type)
{
    switch (type) {
        case KnownTokenType::ROOT:
            return GetParams().IssueTokenBurnAddress();
        case KnownTokenType::SUB:
            return GetParams().IssueSubTokenBurnAddress();
        case KnownTokenType::MSGCHANNEL:
            return GetParams().IssueMsgChannelTokenBurnAddress();
        case KnownTokenType::OWNER:
            return "";
        case KnownTokenType::UNIQUE:
            return GetParams().IssueUniqueTokenBurnAddress();
        case KnownTokenType::VOTE:
            return "";
        case KnownTokenType::REISSUE:
            return GetParams().ReissueTokenBurnAddress();
        case KnownTokenType::QUALIFIER:
            return GetParams().IssueQualifierTokenBurnAddress();
        case KnownTokenType::SUB_QUALIFIER:
            return GetParams().IssueSubQualifierTokenBurnAddress();
        case KnownTokenType::RESTRICTED:
            return GetParams().IssueRestrictedTokenBurnAddress();
        case KnownTokenType::NULL_ADD_QUALIFIER:
            return GetParams().AddNullQualifierTagBurnAddress();
        default:
            return "";
    }
}

//! This will get the amount that an address for a certain token contains from the database if they cache doesn't already have it
bool GetBestTokenAddressAmount(CTokensCache& cache, const std::string& tokenName, const std::string& address)
{
    if (fTokenIndex) {
        auto pair = make_pair(tokenName, address);

        // If the caches map has the pair, return true because the map already contains the best dirty amount
        if (cache.mapTokensAddressAmount.count(pair))
            return true;

        // If the caches map has the pair, return true because the map already contains the best dirty amount
        if (ptokens->mapTokensAddressAmount.count(pair)) {
            cache.mapTokensAddressAmount[pair] = ptokens->mapTokensAddressAmount.at(pair);
            return true;
        }

        // If the database contains the tokens address amount, insert it into the database and return true
        CAmount nDBAmount;
        if (ptokensdb->ReadTokenAddressQuantity(pair.first, pair.second, nDBAmount)) {
            cache.mapTokensAddressAmount.insert(make_pair(pair, nDBAmount));
            return true;
        }
    }

    // The amount wasn't found return false
    return false;
}

#ifdef ENABLE_WALLET
//! sets _balances_ with the total quantity of each owned token
bool GetAllMyTokenBalances(std::map<std::string, std::vector<COutput> >& outputs, std::map<std::string, CAmount>& amounts, const int confirmations, const std::string& prefix) {

    // Return false if no wallet was found to compute token balances
    if (!vpwallets.size())
        return false;

    // Get the map of tokennames to outputs
    vpwallets[0]->AvailableTokens(outputs, true, nullptr, 1, MAX_MONEY, MAX_MONEY, 0, confirmations);

    // Loop through all pairs of Token Name -> vector<COutput>
    for (const auto& pair : outputs) {
        if (prefix.empty() || pair.first.find(prefix) == 0) { // Check for prefix
            CAmount balance = 0;
            for (auto txout : pair.second) { // Compute balance of token by summing all Available Outputs
                CTokenOutputEntry data;
                if (GetTokenData(txout.tx->tx->vout[txout.i].scriptPubKey, data))
                    balance += data.nAmount;
            }
            amounts.insert(std::make_pair(pair.first, balance));
        }
    }

    return true;
}

bool GetMyTokenBalance(const std::string& name, CAmount& balance, const int& confirmations) {

    // Return false if no wallet was found to compute token balances
    if (!vpwallets.size())
        return false;

    // Get the map of tokennames to outputs
    std::map<std::string, std::vector<COutput> > outputs;
    vpwallets[0]->AvailableTokens(outputs, true, nullptr, 1, MAX_MONEY, MAX_MONEY, 0, confirmations);

    // Loop through all pairs of Token Name -> vector<COutput>
    if (outputs.count(name)) {
        auto& ref = outputs.at(name);
        for (const auto& txout : ref) {
            CTokenOutputEntry data;
            if (GetTokenData(txout.tx->tx->vout[txout.i].scriptPubKey, data)) {
                balance += data.nAmount;
            }
        }
    }

    return true;
}
#endif

// 46 char base58 --> 34 char KAW compatible
std::string DecodeTokenData(std::string encoded)
{
    if (encoded.size() == 46) {
        std::vector<unsigned char> b;
        DecodeBase58(encoded, b);
        return std::string(b.begin(), b.end());
    }

    else if (encoded.size() == 64 && IsHex(encoded)) {
        std::vector<unsigned char> vec = ParseHex(encoded);
        return std::string(vec.begin(), vec.end());
    }

    return "";

};

std::string EncodeTokenData(std::string decoded)
{
    if (decoded.size() == 34) {
        return EncodeIPFS(decoded);
    }
    else if (decoded.size() == 32){
        return HexStr(decoded);
    }

    return "";
}

// 46 char base58 --> 34 char KAW compatible
std::string DecodeIPFS(std::string encoded)
{
    std::vector<unsigned char> b;
    DecodeBase58(encoded, b);
    return std::string(b.begin(), b.end());
};

// 34 char KAW compatible --> 46 char base58
std::string EncodeIPFS(std::string decoded){
    std::vector<char> charData(decoded.begin(), decoded.end());
    std::vector<unsigned char> unsignedCharData;
    for (char c : charData)
        unsignedCharData.push_back(static_cast<unsigned char>(c));
    return EncodeBase58(unsignedCharData);
};

#ifdef ENABLE_WALLET
bool CreateTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const CNewToken& token, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string)
{
    std::vector<CNewToken> tokens;
    tokens.push_back(token);
    return CreateTokenTransaction(pwallet, coinControl, tokens, address, error, wtxNew, reservekey, nFeeRequired, verifier_string);
}

bool CreateTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const std::vector<CNewToken> tokens, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string)
{
    std::string change_address = EncodeDestination(coinControl.destChange);

    auto currentActiveTokenCache = GetCurrentTokenCache();
    // Validate the tokens data
    std::string strError;
    for (auto token : tokens) {
        if (!ContextualCheckNewToken(currentActiveTokenCache, token, strError)) {
            error = std::make_pair(RPC_INVALID_PARAMETER, strError);
            return false;
        }
    }

    if (!change_address.empty()) {
        CTxDestination destination = DecodeDestination(change_address);
        if (!IsValidDestination(destination)) {
            error = std::make_pair(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + change_address);
            return false;
        }
    } else {
        // no coin control: send change to newly generated address
        CKeyID keyID;
        std::string strFailReason;
        if (!pwallet->CreateNewChangeAddress(reservekey, keyID, strFailReason)) {
            error = std::make_pair(RPC_WALLET_KEYPOOL_RAN_OUT, strFailReason);
            return false;
        }

        change_address = EncodeDestination(keyID);
        coinControl.destChange = DecodeDestination(change_address);
    }

    KnownTokenType tokenType;
    std::string parentName;
    for (auto token : tokens) {
        if (!IsTokenNameValid(token.strName, tokenType)) {
            error = std::make_pair(RPC_INVALID_PARAMETER, "Token name not valid");
            return false;
        }
        if (tokens.size() > 1 && tokenType != KnownTokenType::UNIQUE) {
            error = std::make_pair(RPC_INVALID_PARAMETER, "Only unique tokens can be issued in bulk.");
            return false;
        }
        std::string parent = GetParentName(token.strName);
        if (parentName.empty())
            parentName = parent;
        if (parentName != parent) {
            error = std::make_pair(RPC_INVALID_PARAMETER, "All tokens must have the same parent.");
            return false;
        }
    }

    // Assign the correct burn amount and the correct burn address depending on the type of token issuance that is happening
    CAmount burnAmount = GetBurnAmount(tokenType) * tokens.size();
    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetBurnAddress(tokenType)));

    CAmount curBalance = pwallet->GetBalance();

    // Check to make sure the wallet has the YONA required by the burnAmount
    if (curBalance < burnAmount) {
        error = std::make_pair(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
        return false;
    }

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        error = std::make_pair(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
        return false;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Create and send the transaction
    std::string strTxError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fSubtractFeeFromAmount = false;

    CRecipient recipient = {scriptPubKey, burnAmount, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);

    // If the token is a subtoken or unique token. We need to send the ownertoken change back to ourselfs
    if (tokenType == KnownTokenType::SUB || tokenType == KnownTokenType::UNIQUE || tokenType == KnownTokenType::MSGCHANNEL) {
        // Get the script for the destination address for the tokens
        CScript scriptTransferOwnerToken = GetScriptForDestination(DecodeDestination(change_address));

        CTokenTransfer tokenTransfer(parentName + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
        tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);
        CRecipient rec = {scriptTransferOwnerToken, 0, fSubtractFeeFromAmount};
        vecSend.push_back(rec);
    }

    // If the token is a sub qualifier. We need to send the token parent change back to ourselfs
    if (tokenType == KnownTokenType::SUB_QUALIFIER) {
        // Get the script for the destination address for the tokens
        CScript scriptTransferQualifierToken = GetScriptForDestination(DecodeDestination(change_address));

        CTokenTransfer tokenTransfer(parentName, OWNER_TOKEN_AMOUNT, 0);
        tokenTransfer.ConstructTransaction(scriptTransferQualifierToken);
        CRecipient rec = {scriptTransferQualifierToken, 0, fSubtractFeeFromAmount};
        vecSend.push_back(rec);
    }

    // Get the owner outpoints if this is a subtoken or unique token
    if (tokenType == KnownTokenType::SUB || tokenType == KnownTokenType::UNIQUE || tokenType == KnownTokenType::MSGCHANNEL) {
        // Verify that this wallet is the owner for the token, and get the owner token outpoint
        for (auto token : tokens) {
            if (!VerifyWalletHasToken(parentName + OWNER_TAG, error)) {
                return false;
            }
        }
    }

    // Get the owner outpoints if this is a sub_qualifier token
    if (tokenType == KnownTokenType::SUB_QUALIFIER) {
        // Verify that this wallet is the owner for the token, and get the owner token outpoint
        for (auto token : tokens) {
            if (!VerifyWalletHasToken(parentName, error)) {
                return false;
            }
        }
    }

    if (tokenType == KnownTokenType::RESTRICTED) {
        // Restricted tokens require the ROOT! token to be sent with the issuance
        CScript scriptTransferOwnerToken = GetScriptForDestination(DecodeDestination(change_address));

        // Create a transaction that sends the ROOT owner token (e.g. $TOKEN requires TOKEN!)
        std::string strStripped = parentName.substr(1, parentName.size() - 1);

        // Verify that this wallet is the owner for the token, and get the owner token outpoint
        if (!VerifyWalletHasToken(strStripped + OWNER_TAG, error)) {
            return false;
        }

        CTokenTransfer tokenTransfer(strStripped + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
        tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);

        CRecipient ownerRec = {scriptTransferOwnerToken, 0, fSubtractFeeFromAmount};
        vecSend.push_back(ownerRec);

        // Every restricted token issuance must have a verifier string
        if (!verifier_string) {
            error = std::make_pair(RPC_INVALID_PARAMETER, "Error: Verifier string not found");
            return false;
        }

        // Create the token null data transaction that will get added to the issue transaction
        CScript verifierScript;
        CNullTokenTxVerifierString verifier(*verifier_string);
        verifier.ConstructTransaction(verifierScript);

        CRecipient rec = {verifierScript, 0, false};
        vecSend.push_back(rec);
    }

    if (!pwallet->CreateTransactionWithTokens(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strTxError, coinControl, tokens, DecodeDestination(address), tokenType)) {
        if (!fSubtractFeeFromAmount && burnAmount + nFeeRequired > curBalance)
            strTxError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
        error = std::make_pair(RPC_WALLET_ERROR, strTxError);
        return false;
    }
    return true;
}

bool CreateReissueTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const CReissueToken& reissueToken, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string)
{
    // Create transaction variables
    std::string strTxError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fSubtractFeeFromAmount = false;

    // Create token variables
    std::string token_name = reissueToken.strName;
    std::string change_address = EncodeDestination(coinControl.destChange);

    // Get the token type
    KnownTokenType token_type = KnownTokenType::INVALID;
    IsTokenNameValid(token_name, token_type);

    // Check that validitity of the address
    if (!IsValidDestinationString(address)) {
        error = std::make_pair(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
        return false;
    }

    // Build the change address
    if (!change_address.empty()) {
        CTxDestination destination = DecodeDestination(change_address);
        if (!IsValidDestination(destination)) {
            error = std::make_pair(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + change_address);
            return false;
        }
    } else {
        CKeyID keyID;
        std::string strFailReason;
        if (!pwallet->CreateNewChangeAddress(reservekey, keyID, strFailReason)) {
            error = std::make_pair(RPC_WALLET_KEYPOOL_RAN_OUT, strFailReason);
            return false;
        }

        change_address = EncodeDestination(keyID);
        coinControl.destChange = DecodeDestination(change_address);
    }

    // Check the tokens name
    if (!IsTokenNameValid(token_name)) {
        error = std::make_pair(RPC_INVALID_PARAMS, std::string("Invalid token name: ") + token_name);
        return false;
    }

    // Check to make sure this isn't an owner token
    if (IsTokenNameAnOwner(token_name)) {
        error = std::make_pair(RPC_INVALID_PARAMS, std::string("Owner Tokens are not able to be reissued"));
        return false;
    }

    // ptokens and ptokensCache need to be initialized
    auto currentActiveTokenCache = GetCurrentTokenCache();
    if (!currentActiveTokenCache) {
        error = std::make_pair(RPC_DATABASE_ERROR, std::string("ptokens isn't initialized"));
        return false;
    }

    // Fail if the token cache isn't initialized
    if (!ptokensCache) {
        error = std::make_pair(RPC_DATABASE_ERROR,
                               std::string("ptokensCache isn't initialized"));
        return false;
    }

    // Check to make sure that the reissue token data is valid
    std::string strError;
    if (!ContextualCheckReissueToken(currentActiveTokenCache, reissueToken, strError)) {
        error = std::make_pair(RPC_VERIFY_ERROR,
                               std::string("Failed to create reissue token object. Error: ") + strError);
        return false;
    }

    // strip of the first character of the token name, this is used for restricted tokens only
    std::string stripped_token_name = token_name.substr(1, token_name.size() - 1);

    // If we are reissuing a restricted token, check to see if we have the root owner token $TOKEN check for TOKEN!
    if (token_type == KnownTokenType::RESTRICTED) {
        // Verify that this wallet is the owner for the token, and get the owner token outpoint
        if (!VerifyWalletHasToken(stripped_token_name + OWNER_TAG, error)) {
            return false;
        }
    } else {
        // Verify that this wallet is the owner for the token, and get the owner token outpoint
        if (!VerifyWalletHasToken(token_name + OWNER_TAG, error)) {
            return false;
        }
    }

    // Check the wallet balance
    CAmount curBalance = pwallet->GetBalance();

    // Get the current burn amount for issuing an token
    CAmount burnAmount = GetReissueTokenBurnAmount();

    // Check to make sure the wallet has the YONA required by the burnAmount
    if (curBalance < burnAmount) {
        error = std::make_pair(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
        return false;
    }

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        error = std::make_pair(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
        return false;
    }

    // Get the script for the destination address for the tokens
    CScript scriptTransferOwnerToken = GetScriptForDestination(DecodeDestination(change_address));

    if (token_type == KnownTokenType::RESTRICTED) {
        CTokenTransfer tokenTransfer(stripped_token_name + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
        tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);
    } else {
        CTokenTransfer tokenTransfer(token_name + OWNER_TAG, OWNER_TOKEN_AMOUNT, 0);
        tokenTransfer.ConstructTransaction(scriptTransferOwnerToken);
    }

    if (token_type == KnownTokenType::RESTRICTED) {
        // If we are changing the verifier string, check to make sure the new address meets the new verifier string rules
        if (verifier_string) {
            if (reissueToken.nAmount > 0) {
                std::string strError = "";
                ErrorReport report;
                if (!ContextualCheckVerifierString(ptokens, *verifier_string, address, strError, &report)) {
                    error = std::make_pair(RPC_INVALID_PARAMETER, strError);
                    return false;
                }
            } else {
                // If we aren't adding any tokens but we are changing the verifier string, Check to make sure the verifier string parses correctly
                std::string strError = "";
                if (!ContextualCheckVerifierString(ptokens, *verifier_string, "", strError)) {
                    error = std::make_pair(RPC_INVALID_PARAMETER, strError);
                    return false;
                }
            }
        } else {
            // If the user is reissuing more tokens, and they aren't changing the verifier string, check it against the current verifier string
            if (reissueToken.nAmount > 0) {
                CNullTokenTxVerifierString verifier;
                if (!ptokens->GetTokenVerifierStringIfExists(reissueToken.strName, verifier)) {
                    error = std::make_pair(RPC_DATABASE_ERROR, "Failed to get the tokens cache pointer");
                    return false;
                }

                std::string strError = "";
                if (!ContextualCheckVerifierString(ptokens, verifier.verifier_string, address, strError)) {
                    error = std::make_pair(RPC_INVALID_PARAMETER, strError);
                    return false;
                }
            }
        }

        // Every restricted token issuance must have a verifier string
        if (verifier_string) {
            // Create the token null data transaction that will get added to the issue transaction
            CScript verifierScript;
            CNullTokenTxVerifierString verifier(*verifier_string);
            verifier.ConstructTransaction(verifierScript);

            CRecipient rec = {verifierScript, 0, false};
            vecSend.push_back(rec);
        }
    }

    // Get the script for the burn address
    CScript scriptPubKeyBurn = GetScriptForDestination(DecodeDestination(GetParams().ReissueTokenBurnAddress()));

    // Create and send the transaction
    CRecipient recipient = {scriptPubKeyBurn, burnAmount, fSubtractFeeFromAmount};
    CRecipient recipient2 = {scriptTransferOwnerToken, 0, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);
    vecSend.push_back(recipient2);
    if (!pwallet->CreateTransactionWithReissueToken(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strTxError, coinControl, reissueToken, DecodeDestination(address))) {
        if (!fSubtractFeeFromAmount && burnAmount + nFeeRequired > curBalance)
            strTxError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
        error = std::make_pair(RPC_WALLET_ERROR, strTxError);
        return false;
    }
    return true;
}


// nullTokenTxData -> Use this for freeze/unfreeze an address or adding a qualifier to an address
// nullGlobalRestrictionData -> Use this to globally freeze/unfreeze a restricted token.
bool CreateTransferTokenTransaction(CWallet* pwallet, const CCoinControl& coinControl, const std::vector< std::pair<CTokenTransfer, std::string> >vTransfers, const std::string& changeAddress, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::vector<std::pair<CNullTokenTxData, std::string> >* nullTokenTxData, std::vector<CNullTokenTxData>* nullGlobalRestrictionData)
{
    // Initialize Values for transaction
    std::string strTxError;
    std::vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    bool fSubtractFeeFromAmount = false;

    // Check for a balance before processing transfers
    CAmount curBalance = pwallet->GetBalance();
    if (curBalance == 0) {
        error = std::make_pair(RPC_WALLET_INSUFFICIENT_FUNDS, std::string("This wallet doesn't contain any YONA, transfering an token requires a network fee"));
        return false;
    }

    // Check for peers and connections
    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        error = std::make_pair(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
        return false;
    }

    // Loop through all transfers and create scriptpubkeys for them
    for (auto transfer : vTransfers) {
        std::string address = transfer.second;
        std::string token_name = transfer.first.strName;
        std::string message = transfer.first.message;
        CAmount nAmount = transfer.first.nAmount;
        uint32_t nTimeLock = transfer.first.nTimeLock;
        int64_t expireTime = transfer.first.nExpireTime;

        if (!IsValidDestinationString(address)) {
            error = std::make_pair(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Yona address: ") + address);
            return false;
        }
        auto currentActiveTokenCache = GetCurrentTokenCache();
        if (!currentActiveTokenCache) {
            error = std::make_pair(RPC_DATABASE_ERROR, std::string("ptokens isn't initialized"));
            return false;
        }

        if (!VerifyWalletHasToken(token_name, error)) // Sets error if it fails
            return false;

        // If it is an ownership transfer, make a quick check to make sure the amount is 1
        if (IsTokenNameAnOwner(token_name)) {
            if (nAmount != OWNER_TOKEN_AMOUNT) {
                error = std::make_pair(RPC_INVALID_PARAMS, std::string(
                        _("When transferring an 'Ownership Token' the amount must always be 1. Please try again with the amount of 1")));
                return false;
            }
        }

        // If the token is a restricted token, check the verifier script
        if(IsTokenNameAnRestricted(token_name)) {
            std::string strError = "";

            // Check for global restriction
            if (ptokens->CheckForGlobalRestriction(transfer.first.strName, true)) {
                error = std::make_pair(RPC_INVALID_PARAMETER, _("Unable to transfer restricted token, this restricted token has been globally frozen"));
                return false;
            }

            if (!transfer.first.ContextualCheckAgainstVerifyString(ptokens, address, strError)) {
                error = std::make_pair(RPC_INVALID_PARAMETER, strError);
                return false;
            }

            if (!coinControl.tokenDestChange.empty()) {
                std::string change_address = EncodeDestination(coinControl.tokenDestChange);
                // If this is a transfer of a restricted token, check the destination address against the verifier string
                CNullTokenTxVerifierString verifier;
                if (!ptokens->GetTokenVerifierStringIfExists(token_name, verifier)) {
                    error = std::make_pair(RPC_DATABASE_ERROR, _("Unable to get restricted tokens verifier string. Database out of sync. Reindex required"));
                    return false;
                }

                if (!ContextualCheckVerifierString(ptokens, verifier.verifier_string, change_address, strError)) {
                    error = std::make_pair(RPC_DATABASE_ERROR, std::string(_("Change address can not be sent to because it doesn't have the correct qualifier tags ") + strError));
                    return false;
                }
            }
        }

        // Get the script for the burn address
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(address));

        // Update the scriptPubKey with the transfer token information
        CTokenTransfer tokenTransfer(token_name, nAmount, nTimeLock, message, expireTime);
        tokenTransfer.ConstructTransaction(scriptPubKey);

        CRecipient recipient = {scriptPubKey, 0, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    // If tokenTxData is not nullptr, the user wants to add some OP_YONA_TOKEN data transactions into the transaction
    if (nullTokenTxData) {
        std::string strError = "";
        int nAddTagCount = 0;
        for (auto pair : *nullTokenTxData) {

            if (IsTokenNameAQualifier(pair.first.token_name)) {
                if (!VerifyQualifierChange(*ptokens, pair.first, pair.second, strError)) {
                    error = std::make_pair(RPC_INVALID_REQUEST, strError);
                    return false;
                }
                if (pair.first.flag == (int)QualifierType::ADD_QUALIFIER)
                    nAddTagCount++;
            } else if (IsTokenNameAnRestricted(pair.first.token_name)) {
                if (!VerifyRestrictedAddressChange(*ptokens, pair.first, pair.second, strError)) {
                    error = std::make_pair(RPC_INVALID_REQUEST, strError);
                    return false;
                }
            }

            CScript dataScript = GetScriptForNullTokenDataDestination(DecodeDestination(pair.second));
            pair.first.ConstructTransaction(dataScript);

            CRecipient recipient = {dataScript, 0, false};
            vecSend.push_back(recipient);
        }

        // Add the burn recipient for adding tags to addresses
        if (nAddTagCount) {
            CScript addTagBurnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(KnownTokenType::NULL_ADD_QUALIFIER)));
            CRecipient addTagBurnRecipient = {addTagBurnScript, GetBurnAmount(KnownTokenType::NULL_ADD_QUALIFIER) * nAddTagCount, false};
            vecSend.push_back(addTagBurnRecipient);
        }
    }

    // nullGlobalRestiotionData, the user wants to add OP_YONA_TOKEN OP_YONA_TOKEN OP_YONA_TOKENS data transaction to the transaction
    if (nullGlobalRestrictionData) {
        std::string strError = "";
        for (auto dataObject : *nullGlobalRestrictionData) {

            if (!VerifyGlobalRestrictedChange(*ptokens, dataObject, strError)) {
                error = std::make_pair(RPC_INVALID_REQUEST, strError);
                return false;
            }

            CScript dataScript;
            dataObject.ConstructGlobalRestrictionTransaction(dataScript);
            CRecipient recipient = {dataScript, 0, false};
            vecSend.push_back(recipient);
        }
    }

    // Create and send the transaction
    if (!pwallet->CreateTransactionWithTransferToken(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strTxError, coinControl)) {
        if (!fSubtractFeeFromAmount && nFeeRequired > curBalance) {
            error = std::make_pair(RPC_WALLET_ERROR, strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired)));
            return false;
        }
        error = std::make_pair(RPC_TRANSACTION_ERROR, strTxError);
        return false;
    }
    return true;
}

bool SendTokenTransaction(CWallet* pwallet, CWalletTx& transaction, CReserveKey& reserveKey, std::pair<int, std::string>& error, std::string& txid)
{
    CValidationState state;
    if (!pwallet->CommitTransaction(transaction, reserveKey, g_connman.get(), state)) {
        error = std::make_pair(RPC_WALLET_ERROR, strprintf("Error: The transaction was rejected! Reason given: %s", state.GetRejectReason()));
        return false;
    }

    txid = transaction.GetHash().GetHex();
    return true;
}

bool VerifyWalletHasToken(const std::string& token_name, std::pair<int, std::string>& pairError)
{
    CWallet* pwallet;
    if (vpwallets.size() > 0)
        pwallet = vpwallets[0];
    else {
        pairError = std::make_pair(RPC_WALLET_ERROR, strprintf("Wallet not found. Can't verify if it contains: %s", token_name));
        return false;
    }

    std::vector<COutput> vCoins;
    std::map<std::string, std::vector<COutput> > mapTokenCoins;
    pwallet->AvailableTokens(mapTokenCoins);

    if (mapTokenCoins.count(token_name))
        return true;

    pairError = std::make_pair(RPC_INVALID_REQUEST, strprintf("Wallet doesn't have token: %s", token_name));
    return false;
}

#endif

// Return true if the amount is valid with the units passed in
bool CheckAmountWithUnits(const CAmount& nAmount, const int8_t nUnits)
{
    return nAmount % int64_t(pow(10, (MAX_UNIT - nUnits))) == 0;
}

bool CheckEncoded(const std::string& hash, std::string& strError) {
    std::string encodedStr = EncodeTokenData(hash);
    if (encodedStr.substr(0, 2) == "Qm" && encodedStr.size() == 46) {
        return true;
    }

    if (AreMessagesDeployed()) {
        if (IsHex(encodedStr) && encodedStr.length() == 64) {
            return true;
        }
    }

    strError = _("Invalid parameter: ipfs_hash is not valid, or txid hash is not the right length");

    return false;
}

void GetTxOutKnownTokenTypes(const std::vector<CTxOut>& vout, int& issues, int& reissues, int& transfers, int& owners)
{
    for (auto out: vout) {
        int type;
        bool fIsOwner;
        if (out.scriptPubKey.IsTokenScript(type, fIsOwner)) {
            if (type == TX_NEW_TOKEN && !fIsOwner)
                issues++;
            else if (type == TX_NEW_TOKEN && fIsOwner)
                owners++;
            else if (type == TX_TRANSFER_TOKEN)
                transfers++;
            else if (type == TX_REISSUE_TOKEN)
                reissues++;
        }
    }
}

bool ParseTokenScript(CScript scriptPubKey, uint160 &hashBytes, int& nScriptType, std::string &tokenName, CAmount &tokenAmount) {
    int nType;
    bool fIsOwner;
    int _nStartingPoint;
    std::string _strAddress;
    bool isToken = false;
    if (scriptPubKey.IsTokenScript(nType, nScriptType, fIsOwner, _nStartingPoint)) {
        if (nType == TX_NEW_TOKEN) {
            if (fIsOwner) {
                if (OwnerTokenFromScript(scriptPubKey, tokenName, _strAddress)) {
                    tokenAmount = OWNER_TOKEN_AMOUNT;
                    isToken = true;
                } else {
                    LogPrintf("%s : Couldn't get new owner token from script: %s", __func__, HexStr(scriptPubKey));
                }
            } else {
                CNewToken token;
                if (TokenFromScript(scriptPubKey, token, _strAddress)) {
                    tokenName = token.strName;
                    tokenAmount = token.nAmount;
                    isToken = true;
                } else {
                    LogPrintf("%s : Couldn't get new token from script: %s", __func__, HexStr(scriptPubKey));
                }
            }
        } else if (nType == TX_REISSUE_TOKEN) {
            CReissueToken token;
            if (ReissueTokenFromScript(scriptPubKey, token, _strAddress)) {
                tokenName = token.strName;
                tokenAmount = token.nAmount;
                isToken = true;
            } else {
                LogPrintf("%s : Couldn't get reissue token from script: %s", __func__, HexStr(scriptPubKey));
            }
        } else if (nType == TX_TRANSFER_TOKEN) {
            CTokenTransfer token;
            if (TransferTokenFromScript(scriptPubKey, token, _strAddress)) {
                tokenName = token.strName;
                tokenAmount = token.nAmount;
                isToken = true;
            } else {
                LogPrintf("%s : Couldn't get transfer token from script: %s", __func__, HexStr(scriptPubKey));
            }
        } else {
            LogPrintf("%s : Unsupported token type: %s", __func__, nType);
        }
    } else {
//        LogPrintf("%s : Found no token in script: %s", __func__, HexStr(scriptPubKey));
    }
    if (isToken) {
        if (nScriptType == TX_SCRIPTHASH) {
            hashBytes = uint160(std::vector <unsigned char>(scriptPubKey.begin()+2, scriptPubKey.begin()+22));
        } else if (nScriptType == TX_PUBKEYHASH) {
            hashBytes = uint160(std::vector <unsigned char>(scriptPubKey.begin()+3, scriptPubKey.begin()+23));
        } else {
            return false;
        }

//        LogPrintf("%s : Found tokens in script at address %s : %s (%s)", __func__, _strAddress, tokenName, tokenAmount);

        return true;
    }
    return false;
}

CNullTokenTxData::CNullTokenTxData(const std::string &strTokenname, const int8_t &nFlag)
{
    SetNull();
    this->token_name = strTokenname;
    this->flag = nFlag;
}

bool CNullTokenTxData::IsValid(std::string &strError, CTokensCache &tokenCache, bool fForceCheckPrimaryTokenExists) const
{
    KnownTokenType type;
    if (!IsTokenNameValid(token_name, type)) {
        strError = _("Token name is not valid");
        return false;
    }

    if (type != KnownTokenType::QUALIFIER && type != KnownTokenType::SUB_QUALIFIER && type != KnownTokenType::RESTRICTED) {
        strError = _("Token must be a qualifier, sub qualifier, or a restricted token");
        return false;
    }

    if (flag != 0 && flag != 1) {
        strError = _("Flag must be 1 or 0");
        return false;
    }

    if (fForceCheckPrimaryTokenExists) {
        if (!tokenCache.CheckIfTokenExists(token_name)) {
            strError = _("Token doesn't exist: ") + token_name;
            return false;
        }
    }

    return true;
}

void CNullTokenTxData::ConstructTransaction(CScript &script) const
{
    CDataStream ssTokenTxData(SER_NETWORK, PROTOCOL_VERSION);
    ssTokenTxData << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.insert(vchMessage.end(), ssTokenTxData.begin(), ssTokenTxData.end());
    script << ToByteVector(vchMessage);
}

void CNullTokenTxData::ConstructGlobalRestrictionTransaction(CScript &script) const
{
    CDataStream ssTokenTxData(SER_NETWORK, PROTOCOL_VERSION);
    ssTokenTxData << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.insert(vchMessage.end(), ssTokenTxData.begin(), ssTokenTxData.end());
    script << OP_YONA_TOKEN << OP_RESERVED << OP_RESERVED << ToByteVector(vchMessage);
}

CNullTokenTxVerifierString::CNullTokenTxVerifierString(const std::string &verifier)
{
    SetNull();
    this->verifier_string = verifier;
}

void CNullTokenTxVerifierString::ConstructTransaction(CScript &script) const
{
    CDataStream ssTokenTxData(SER_NETWORK, PROTOCOL_VERSION);
    ssTokenTxData << *this;

    std::vector<unsigned char> vchMessage;
    vchMessage.insert(vchMessage.end(), ssTokenTxData.begin(), ssTokenTxData.end());
    script << OP_YONA_TOKEN << OP_RESERVED << ToByteVector(vchMessage);
}

bool CTokensCache::GetTokenVerifierStringIfExists(const std::string &name, CNullTokenTxVerifierString& verifierString, bool fSkipTempCache)
{

    /** There are circumstances where a blocks transactions could be changing an tokens verifier string, While at the
     * same time a transaction is added to the same block that is trying to transfer the tokens who verifier string is
     * changing.
     * Depending on the ordering of these two transactions. The verifier string used to verify the validity of the
     * transaction could be different.
     * To fix this all restricted token transfer validation checks will use only the latest connect block tips caches
     * and databases to validate it. This allows for token transfers and verify string change transactions to be added in the same block
     * without failing validation
    **/

    // Create objects that will be used to check the dirty cache
    CTokenCacheRestrictedVerifiers tempCacheVerifier {name, ""};

    auto setIterator = setNewRestrictedVerifierToRemove.find(tempCacheVerifier);
    // Check the dirty caches first and see if it was recently added or removed
    if (!fSkipTempCache && setIterator != setNewRestrictedVerifierToRemove.end()) {
        if (setIterator->fUndoingRessiue) {
            verifierString.verifier_string = setIterator->verifier;
            return true;
        }
        return false;
    }

    setIterator = ptokens->setNewRestrictedVerifierToRemove.find(tempCacheVerifier);
    // Check the dirty caches first and see if it was recently added or removed
    if (setIterator != ptokens->setNewRestrictedVerifierToRemove.end()) {
        if (setIterator->fUndoingRessiue) {
            verifierString.verifier_string = setIterator->verifier;
            return true;
        }
        return false;
    }

    setIterator = setNewRestrictedVerifierToAdd.find(tempCacheVerifier);
    if (!fSkipTempCache && setIterator != setNewRestrictedVerifierToAdd.end()) {
        verifierString.verifier_string = setIterator->verifier;
        return true;
    }

    setIterator = ptokens->setNewRestrictedVerifierToAdd.find(tempCacheVerifier);
    if (setIterator != ptokens->setNewRestrictedVerifierToAdd.end()) {
        verifierString.verifier_string = setIterator->verifier;
        return true;
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensVerifierCache) {
        if (ptokensVerifierCache->Exists(name)) {
            verifierString = ptokensVerifierCache->Get(name);
            return true;
        }
    }

    if (prestricteddb) {
        std::string verifier;
        if (prestricteddb->ReadVerifier(name, verifier)) {
            verifierString.verifier_string = verifier;
            if (ptokensVerifierCache)
                ptokensVerifierCache->Put(name, verifierString);
            return true;
        }
    }

    return false;
}

bool CTokensCache::CheckForAddressQualifier(const std::string &qualifier_name, const std::string& address, bool fSkipTempCache)
{
    /** There are circumstances where a blocks transactions could be removing or adding a qualifier to an address,
     * While at the same time a transaction is added to the same block that is trying to transfer to the same address.
     * Depending on the ordering of these two transactions. The qualifier database used to verify the validity of the
     * transactions could be different.
     * To fix this all restricted token transfer validation checks will use only the latest connect block tips caches
     * and databases to validate it. This allows for token transfers and address qualifier transactions to be added in the same block
     * without failing validation
    **/

    // Create cache object that will be used to check the dirty caches
    CTokenCacheQualifierAddress cachedQualifierAddress(qualifier_name, address, QualifierType::ADD_QUALIFIER);

    // Check the dirty caches first and see if it was recently added or removed
    auto setIterator = setNewQualifierAddressToRemove.find(cachedQualifierAddress);
    if (!fSkipTempCache &&setIterator != setNewQualifierAddressToRemove.end()) {
        // Undoing a remove qualifier command, means that we are adding the qualifier to the address
        return setIterator->type == QualifierType::REMOVE_QUALIFIER;
    }


    setIterator = ptokens->setNewQualifierAddressToRemove.find(cachedQualifierAddress);
    if (setIterator != ptokens->setNewQualifierAddressToRemove.end()) {
        // Undoing a remove qualifier command, means that we are adding the qualifier to the address
        return setIterator->type == QualifierType::REMOVE_QUALIFIER;
    }

    setIterator = setNewQualifierAddressToAdd.find(cachedQualifierAddress);
    if (!fSkipTempCache && setIterator != setNewQualifierAddressToAdd.end()) {
        // Return true if we are adding the qualifier, and false if we are removing it
        return setIterator->type == QualifierType::ADD_QUALIFIER;
    }


    setIterator = ptokens->setNewQualifierAddressToAdd.find(cachedQualifierAddress);
    if (setIterator != ptokens->setNewQualifierAddressToAdd.end()) {
        // Return true if we are adding the qualifier, and false if we are removing it
        return setIterator->type == QualifierType::ADD_QUALIFIER;
    }

    auto tempCache = CTokenCacheRootQualifierChecker(qualifier_name, address);
    if (!fSkipTempCache && mapRootQualifierAddressesAdd.count(tempCache)){
        if (mapRootQualifierAddressesAdd[tempCache].size()) {
            return true;
        }
    }

    if (ptokens->mapRootQualifierAddressesAdd.count(tempCache)) {
        if (ptokens->mapRootQualifierAddressesAdd[tempCache].size()) {
            return true;
        }
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensQualifierCache) {
        if (ptokensQualifierCache->Exists(cachedQualifierAddress.GetHash().GetHex())) {
            return true;
        }
    }

    if (prestricteddb) {

        // Check for exact qualifier, and add to cache if it exists
        if (prestricteddb->ReadAddressQualifier(address, qualifier_name)) {
            ptokensQualifierCache->Put(cachedQualifierAddress.GetHash().GetHex(), 1);
            return true;
        }

        // Look for sub qualifiers
        if (prestricteddb->CheckForAddressRootQualifier(address, qualifier_name)){
            return true;
        }
    }

    return false;
}


bool CTokensCache::CheckForAddressRestriction(const std::string &restricted_name, const std::string& address, bool fSkipTempCache)
{
    /** There are circumstances where a blocks transactions could be removing or adding a restriction to an address,
     * While at the same time a transaction is added to the same block that is trying to transfer from that address.
     * Depending on the ordering of these two transactions. The address restriction database used to verify the validity of the
     * transactions could be different.
     * To fix this all restricted token transfer validation checks will use only the latest connect block tips caches
     * and databases to validate it. This allows for token transfers and address restriction transactions to be added in the same block
     * without failing validation
    **/

    // Create cache object that will be used to check the dirty caches (type, doesn't matter in this search)
    CTokenCacheRestrictedAddress cachedRestrictedAddress(restricted_name, address, RestrictedType::FREEZE_ADDRESS);

    // Check the dirty caches first and see if it was recently added or removed
    auto setIterator = setNewRestrictedAddressToRemove.find(cachedRestrictedAddress);
    if (!fSkipTempCache && setIterator != setNewRestrictedAddressToRemove.end()) {
        // Undoing a unfreeze, means that we are adding back a freeze
        return setIterator->type == RestrictedType::UNFREEZE_ADDRESS;
    }

    setIterator = ptokens->setNewRestrictedAddressToRemove.find(cachedRestrictedAddress);
    if (setIterator != ptokens->setNewRestrictedAddressToRemove.end()) {
        // Undoing a unfreeze, means that we are adding back a freeze
        return setIterator->type == RestrictedType::UNFREEZE_ADDRESS;
    }

    setIterator = setNewRestrictedAddressToAdd.find(cachedRestrictedAddress);
    if (!fSkipTempCache && setIterator != setNewRestrictedAddressToAdd.end()) {
        // Return true if we are freezing the address
        return setIterator->type == RestrictedType::FREEZE_ADDRESS;
    }

    setIterator = ptokens->setNewRestrictedAddressToAdd.find(cachedRestrictedAddress);
    if (setIterator != ptokens->setNewRestrictedAddressToAdd.end()) {
        // Return true if we are freezing the address
        return setIterator->type == RestrictedType::FREEZE_ADDRESS;
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensRestrictionCache) {
        if (ptokensRestrictionCache->Exists(cachedRestrictedAddress.GetHash().GetHex())) {
            return true;
        }
    }

    if (prestricteddb) {
        if (prestricteddb->ReadRestrictedAddress(address, restricted_name)) {
            if (ptokensRestrictionCache) {
                ptokensRestrictionCache->Put(cachedRestrictedAddress.GetHash().GetHex(), 1);
            }
            return true;
        }
    }

    return false;
}

bool CTokensCache::CheckForGlobalRestriction(const std::string &restricted_name, bool fSkipTempCache)
{
    /** There are circumstances where a blocks transactions could be freezing all token transfers. While at
     * the same time a transaction is added to the same block that is trying to transfer the same token that is being
     * frozen.
     * Depending on the ordering of these two transactions. The global restriction database used to verify the validity of the
     * transactions could be different.
     * To fix this all restricted token transfer validation checks will use only the latest connect block tips caches
     * and databases to validate it. This allows for token transfers and global restriction transactions to be added in the same block
     * without failing validation
    **/

    // Create cache object that will be used to check the dirty caches (type, doesn't matter in this search)
    CTokenCacheRestrictedGlobal cachedRestrictedGlobal(restricted_name, RestrictedType::GLOBAL_FREEZE);

    // Check the dirty caches first and see if it was recently added or removed
    auto setIterator = setNewRestrictedGlobalToRemove.find(cachedRestrictedGlobal);
    if (!fSkipTempCache && setIterator != setNewRestrictedGlobalToRemove.end()) {
        // Undoing a removal of a global unfreeze, means that is will become frozen
        return setIterator->type == RestrictedType::GLOBAL_UNFREEZE;
    }

    setIterator = ptokens->setNewRestrictedGlobalToRemove.find(cachedRestrictedGlobal);
    if (setIterator != ptokens->setNewRestrictedGlobalToRemove.end()) {
        // Undoing a removal of a global unfreeze, means that is will become frozen
        return setIterator->type == RestrictedType::GLOBAL_UNFREEZE;
    }

    setIterator = setNewRestrictedGlobalToAdd.find(cachedRestrictedGlobal);
    if (!fSkipTempCache && setIterator != setNewRestrictedGlobalToAdd.end()) {
        // Return true if we are adding a freeze command
        return setIterator->type == RestrictedType::GLOBAL_FREEZE;
    }

    setIterator = ptokens->setNewRestrictedGlobalToAdd.find(cachedRestrictedGlobal);
    if (setIterator != ptokens->setNewRestrictedGlobalToAdd.end()) {
        // Return true if we are adding a freeze command
        return setIterator->type == RestrictedType::GLOBAL_FREEZE;
    }

    // Check the cache, if it doesn't exist in the cache. Try and read it from database
    if (ptokensGlobalRestrictionCache) {
        if (ptokensGlobalRestrictionCache->Exists(cachedRestrictedGlobal.tokenName)) {
            return true;
        }
    }

    if (prestricteddb) {
        if (prestricteddb->ReadGlobalRestriction(restricted_name)) {
            if (ptokensGlobalRestrictionCache)
                ptokensGlobalRestrictionCache->Put(cachedRestrictedGlobal.tokenName, 1);
            return true;
        }
    }

    return false;
}

void ExtractVerifierStringQualifiers(const std::string& verifier, std::set<std::string>& qualifiers)
{
    std::string s(verifier);

    std::regex regexSearch = std::regex(R"([A-Z0-9_.]+)");
    std::smatch match;

    while (std::regex_search(s,match,regexSearch)) {
        for (auto str : match)
            qualifiers.insert(str);
        s = match.suffix().str();
    }
}

std::string GetStrippedVerifierString(const std::string& verifier)
{
    // Remove all white spaces from the verifier string
    std::string str_without_whitespaces = LibBoolEE::removeWhitespaces(verifier);

    // Remove all '#' from the verifier string
    std::string str_without_qualifier_tags = LibBoolEE::removeCharacter(str_without_whitespaces, QUALIFIER_CHAR);

    return str_without_qualifier_tags;
}

bool CheckVerifierString(const std::string& verifier, std::set<std::string>& setFoundQualifiers, std::string& strError, ErrorReport* errorReport)
{
    // If verifier string is true, always return true
    if (verifier == "true") {
        return true;
    }

    // If verifier string is empty, return false
    if (verifier.empty()) {
        strError = _("Verifier string can not be empty. To default to true, use \"true\"");
        if (errorReport) {
            errorReport->type = ErrorReport::ErrorType::EmptyString;
            errorReport->strDevData = "bad-txns-null-verifier-empty";
        }
        return false;
    }

    // Remove all white spaces, and # from the string as this is how it will be stored in database, and in the script
    std::string strippedVerifier = GetStrippedVerifierString(verifier);

    // Check the stripped size to make sure it isn't over 80
    if (strippedVerifier.length() > 80){
        strError = _("Verifier string has length greater than 80 after whitespaces and '#' are removed");
        if (errorReport) {
            errorReport->type = ErrorReport::ErrorType::LengthToLarge;
            errorReport->strDevData = "bad-txns-null-verifier-length-greater-than-max-length";
            errorReport->vecUserData.emplace_back(strippedVerifier);
        }
        return false;
    }

    // Extract the qualifiers from the verifier string
    ExtractVerifierStringQualifiers(strippedVerifier, setFoundQualifiers);

    // Create an object that stores if an address contains a qualifier
    LibBoolEE::Vals vals;

    // If the check address is empty

    // set all qualifiers in the verifier to true
    for (auto qualifier : setFoundQualifiers) {

        std::string edited_qualifier;

        // Qualifer string was stripped above, so we need to add back the #
        edited_qualifier = QUALIFIER_CHAR + qualifier;

        if (!IsQualifierNameValid(edited_qualifier)) {
            strError = "bad-txns-null-verifier-invalid-token-name-" + qualifier;
            if (errorReport) {
                errorReport->type = ErrorReport::ErrorType::InvalidQualifierName;
                errorReport->vecUserData.emplace_back(edited_qualifier);
                errorReport->strDevData = "bad-txns-null-verifier-invalid-token-name-" + qualifier;
            }
            return false;
        }

        vals.insert(std::make_pair(qualifier, true));
    }

    try {
        LibBoolEE::resolve(verifier, vals, errorReport);
        return true;
    } catch (const std::runtime_error& run_error) {
        if (errorReport) {
            if (errorReport->type == ErrorReport::ErrorType::NotSetError) {
                errorReport->type = ErrorReport::ErrorType::InvalidSyntax;
                errorReport->vecUserData.emplace_back(run_error.what());
                errorReport->strDevData = "bad-txns-null-verifier-failed-syntax-check";
            }
        }
        strError = "bad-txns-null-verifier-failed-syntax-check";
        return error("%s : Verifier string failed to resolve. Please check string syntax - exception: %s\n", __func__, run_error.what());
    }
}

bool VerifyNullTokenDataFlag(const int& flag, std::string& strError)
{
    // Check the flag
    if (flag != 0 && flag != 1) {
        strError = "bad-txns-null-data-flag-must-be-0-or-1";
        return false;
    }

    return true;
}

bool VerifyQualifierChange(CTokensCache& cache, const CNullTokenTxData& data, const std::string& address, std::string& strError)
{
    // Check the flag
    if (!VerifyNullTokenDataFlag(data.flag, strError))
        return false;

    // Check to make sure we only allow changes to the current status
    bool fHasQualifier = cache.CheckForAddressQualifier(data.token_name, address, true);
    QualifierType type = data.flag ? QualifierType::ADD_QUALIFIER : QualifierType::REMOVE_QUALIFIER;
    if (type == QualifierType::ADD_QUALIFIER) {
        if (fHasQualifier) {
            strError = "bad-txns-null-data-add-qualifier-when-already-assigned";
            return false;
        }
    } else if (type == QualifierType::REMOVE_QUALIFIER) {
        if (!fHasQualifier) {
            strError = "bad-txns-null-data-removing-qualifier-when-not-assigned";
            return false;
        }
    }

    return true;
}

bool VerifyRestrictedAddressChange(CTokensCache& cache, const CNullTokenTxData& data, const std::string& address, std::string& strError)
{
    // Check the flag
    if (!VerifyNullTokenDataFlag(data.flag, strError))
        return false;

    // Get the current status of the token and the given address
    bool fIsFrozen = cache.CheckForAddressRestriction(data.token_name, address, true);

    // Assign the type based on the data
    RestrictedType type = data.flag ? RestrictedType::FREEZE_ADDRESS : RestrictedType::UNFREEZE_ADDRESS;

    if (type == RestrictedType::FREEZE_ADDRESS) {
        if (fIsFrozen) {
            strError = "bad-txns-null-data-freeze-address-when-already-frozen";
            return false;
        }
    } else if (type == RestrictedType::UNFREEZE_ADDRESS) {
        if (!fIsFrozen) {
            strError = "bad-txns-null-data-unfreeze-address-when-not-frozen";
            return false;
        }
    }

    return true;
}

bool VerifyGlobalRestrictedChange(CTokensCache& cache, const CNullTokenTxData& data, std::string& strError)
{
    // Check the flag
    if (!VerifyNullTokenDataFlag(data.flag, strError))
        return false;

    // Get the current status of the token globally
    bool fIsGloballyFrozen = cache.CheckForGlobalRestriction(data.token_name, true);

    // Assign the type based on the data
    RestrictedType type = data.flag ? RestrictedType::GLOBAL_FREEZE : RestrictedType::GLOBAL_UNFREEZE;

    if (type == RestrictedType::GLOBAL_FREEZE) {
        if (fIsGloballyFrozen) {
            strError = "bad-txns-null-data-global-freeze-when-already-frozen";
            return false;
        }
    } else if (type == RestrictedType::GLOBAL_UNFREEZE) {
        if (!fIsGloballyFrozen) {
            strError = "bad-txns-null-data-global-unfreeze-when-not-frozen";
            return false;
        }
    }

    return true;
}

////////////////


bool CheckVerifierTokenTxOut(const CTxOut& txout, std::string& strError)
{
    CNullTokenTxVerifierString verifier;
    if (!TokenNullVerifierDataFromScript(txout.scriptPubKey, verifier)) {
        strError = "bad-txns-null-verifier-data-serialization";
        return false;
    }

    // All restricted verifiers should have white spaces stripped from the data before it is added to a script
    if ((int)verifier.verifier_string.find_first_of(' ') != -1) {
        strError = "bad-txns-null-verifier-data-contained-whitespaces";
        return false;
    }

    // All restricted verifiers should have # stripped from that data before it is added to a script
    if ((int)verifier.verifier_string.find_first_of('#') != -1) {
        strError = "bad-txns-null-verifier-data-contained-qualifier-character-#";
        return false;
    }

    std::set<std::string> setFoundQualifiers;
    if (!CheckVerifierString(verifier.verifier_string, setFoundQualifiers, strError))
        return false;

    return true;
}
///////////////
bool ContextualCheckNullTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError, std::vector<std::pair<std::string, CNullTokenTxData>>* myNullTokenData)
{
    // Get the data from the script
    CNullTokenTxData data;
    std::string address;
    if (!TokenNullDataFromScript(txout.scriptPubKey, data, address)) {
        strError = "bad-txns-null-token-data-serialization";
        return false;
    }

    // Validate the tx data against the cache, and database
    if (tokenCache) {
        if (IsTokenNameAQualifier(data.token_name)) {
            if (!VerifyQualifierChange(*tokenCache, data, address, strError)) {
                return false;
            }

        } else if (IsTokenNameAnRestricted(data.token_name)) {
            if (!VerifyRestrictedAddressChange(*tokenCache, data, address, strError))
                return false;
        } else {
            strError = "bad-txns-null-token-data-on-non-restricted-or-qualifier-token";
            return false;
        }
    }

#ifdef ENABLE_WALLET
    if (myNullTokenData && vpwallets.size()) {
        if (IsMine(*vpwallets[0], DecodeDestination(address), chainActive.Tip()) & ISMINE_ALL) {
            myNullTokenData->emplace_back(std::make_pair(address, data));
        }
    }
#endif
    return true;
}

bool ContextualCheckGlobalTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError)
{
    // Get the data from the script
    CNullTokenTxData data;
    if (!GlobalTokenNullDataFromScript(txout.scriptPubKey, data)) {
        strError = "bad-txns-null-global-token-data-serialization";
        return false;
    }

    // Validate the tx data against the cache, and database
    if (tokenCache) {
        if (!VerifyGlobalRestrictedChange(*tokenCache, data, strError))
            return false;
    }
    return true;
}

bool ContextualCheckVerifierTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError)
{
    CNullTokenTxVerifierString verifier;
    if (!TokenNullVerifierDataFromScript(txout.scriptPubKey, verifier)) {
        strError = "bad-txns-null-verifier-data-serialization";
        return false;
    }

    if (tokenCache) {
        std::string strError = "";
        std::string address = "";
        std::string strVerifier = verifier.verifier_string;
        if (!ContextualCheckVerifierString(tokenCache, strVerifier, address, strError))
            return false;
    }

    return true;
}

bool ContextualCheckVerifierString(CTokensCache* cache, const std::string& verifier, const std::string& check_address, std::string& strError, ErrorReport* errorReport)
{
    // If verifier is set to true, return true
    if (verifier == "true")
        return true;

    // Check against the non contextual changes first
    std::set<std::string> setFoundQualifiers;
    if (!CheckVerifierString(verifier, setFoundQualifiers, strError, errorReport))
        return false;

    // Loop through each qualifier and make sure that the token exists
    for(auto qualifier : setFoundQualifiers) {
        std::string search = QUALIFIER_CHAR + qualifier;
        if (!cache->CheckIfTokenExists(search, true)) {
            if (errorReport) {
                errorReport->type = ErrorReport::ErrorType::TokenDoesntExist;
                errorReport->vecUserData.emplace_back(search);
                errorReport->strDevData = "bad-txns-null-verifier-contains-non-issued-qualifier";
            }
            strError = "bad-txns-null-verifier-contains-non-issued-qualifier";
            return false;
        }
    }

    // If we got this far, and the check_address is empty. The CheckVerifyString method already did the syntax checks
    // No need to do any more checks, as it will fail because the check_address is empty
    if (check_address.empty())
        return true;

    // Create an object that stores if an address contains a qualifier
    LibBoolEE::Vals vals;

    // Add the qualifiers into the vals object
    for (auto qualifier : setFoundQualifiers) {
        std::string search = QUALIFIER_CHAR + qualifier;

        // Check to see if the address contains the qualifier
        bool has_qualifier = cache->CheckForAddressQualifier(search, check_address, true);

        // Add the true or false value into the vals
        vals.insert(std::make_pair(qualifier, has_qualifier));
    }

    try {
        bool ret = LibBoolEE::resolve(verifier, vals, errorReport);
        if (!ret) {
            if (errorReport) {
                if (errorReport->type == ErrorReport::ErrorType::NotSetError) {
                    errorReport->type = ErrorReport::ErrorType::FailedToVerifyAgainstAddress;
                    errorReport->vecUserData.emplace_back(check_address);
                    errorReport->strDevData = "bad-txns-null-verifier-address-failed-verification";
                }
            }

            error("%s : The address %s failed to verify against: %s. Is null %d", __func__, check_address, verifier, errorReport ? 0 : 1);
            strError = "bad-txns-null-verifier-address-failed-verification";
        }
        return ret;

    } catch (const std::runtime_error& run_error) {

        if (errorReport) {
            if (errorReport->type == ErrorReport::ErrorType::NotSetError) {
                errorReport->type = ErrorReport::ErrorType::InvalidSyntax;
            }

            errorReport->vecUserData.emplace_back(run_error.what());
            errorReport->strDevData = "bad-txns-null-verifier-failed-contexual-syntax-check";
        }

        strError = "bad-txns-null-verifier-failed-contexual-syntax-check";
        return error("%s : Verifier string failed to resolve. Please check string syntax - exception: %s\n", __func__, run_error.what());
    }
}

bool ContextualCheckTransferToken(CTokensCache* tokenCache, const CTokenTransfer& transfer, const std::string& address, std::string& strError)
{
    strError = "";
    KnownTokenType tokenType;
    if (!IsTokenNameValid(transfer.strName, tokenType)) {
        strError = "Invalid parameter: token_name must only consist of valid characters and have a size between 3 and 30 characters. See help for more details.";
        return false;
    }

    if (transfer.nAmount <= 0) {
        strError = "Invalid parameter: token amount can't be equal to or less than zero.";
        return false;
    }

    if (AreMessagesDeployed()) {
        // This is for the current testnet6 only.
        if (transfer.nAmount <= 0) {
            strError = "Invalid parameter: token amount can't be equal to or less than zero.";
            return false;
        }

        if (transfer.message.empty() && transfer.nExpireTime > 0) {
            strError = "Invalid parameter: token transfer expiration time requires a message to be attached to the transfer";
            return false;
        }

        if (transfer.nExpireTime < 0) {
            strError = "Invalid parameter: expiration time must be a positive value";
            return false;
        }

        if (transfer.message.size() && !CheckEncoded(transfer.message, strError)) {
            return false;
        }
    }

    // If the transfer is a message channel token. Check to make sure that it is UNIQUE_TOKEN_AMOUNT
    if (tokenType == KnownTokenType::MSGCHANNEL) {
        if (!AreMessagesDeployed()) {
            strError = "bad-txns-transfer-msgchannel-before-messaging-is-active";
            return false;
        }
    }

    if (tokenType == KnownTokenType::RESTRICTED) {
        if (!AreRestrictedTokensDeployed()) {
            strError = "bad-txns-transfer-restricted-before-it-is-active";
            return false;
        }

        if (tokenCache) {
            if (tokenCache->CheckForGlobalRestriction(transfer.strName, true)) {
                strError = "bad-txns-transfer-restricted-token-that-is-globally-restricted";
                return false;
            }
        }


        std::string strError = "";
        if (!transfer.ContextualCheckAgainstVerifyString(tokenCache, address, strError)) {
            error("%s : %s", __func__, strError);
            return false;
        }
    }

    // If the transfer is a qualifier channel token.
    if (tokenType == KnownTokenType::QUALIFIER || tokenType == KnownTokenType::SUB_QUALIFIER) {
        if (!AreRestrictedTokensDeployed()) {
            strError = "bad-txns-transfer-qualifier-before-it-is-active";
            return false;
        }
    }
    return true;
}

bool CheckNewToken(const CNewToken& token, std::string& strError)
{
    strError = "";

    KnownTokenType tokenType;
    if (!IsTokenNameValid(std::string(token.strName), tokenType)) {
        strError = _("Invalid parameter: token_name must only consist of valid characters and have a size between 3 and 30 characters. See help for more details.");
        return false;
    }

    if (tokenType == KnownTokenType::UNIQUE || tokenType == KnownTokenType::MSGCHANNEL) {
        if (token.units != UNIQUE_TOKEN_UNITS) {
            strError = _("Invalid parameter: units must be ") + std::to_string(UNIQUE_TOKEN_UNITS);
            return false;
        }
        if (token.nAmount != UNIQUE_TOKEN_AMOUNT) {
            strError = _("Invalid parameter: amount must be ") + std::to_string(UNIQUE_TOKEN_AMOUNT);
            return false;
        }
        if (token.nReissuable != 0) {
            strError = _("Invalid parameter: reissuable must be 0");
            return false;
        }
    }

    if (tokenType == KnownTokenType::QUALIFIER || tokenType == KnownTokenType::SUB_QUALIFIER) {
        if (token.units != QUALIFIER_TOKEN_UNITS) {
            strError = _("Invalid parameter: units must be ") + std::to_string(QUALIFIER_TOKEN_UNITS);
            return false;
        }
        if (token.nAmount < QUALIFIER_TOKEN_MIN_AMOUNT || token.nAmount > QUALIFIER_TOKEN_MAX_AMOUNT) {
            strError = _("Invalid parameter: amount must be between ") + std::to_string(QUALIFIER_TOKEN_MIN_AMOUNT) + " - " + std::to_string(QUALIFIER_TOKEN_MAX_AMOUNT);
            return false;
        }
        if (token.nReissuable != 0) {
            strError = _("Invalid parameter: reissuable must be 0");
            return false;
        }
    }

    if (IsTokenNameAnOwner(std::string(token.strName))) {
        strError = _("Invalid parameters: token_name can't have a '!' at the end of it. See help for more details.");
        return false;
    }

    if (token.nAmount <= 0) {
        strError = _("Invalid parameter: token amount can't be equal to or less than zero.");
        return false;
    }

    if (token.nAmount > MAX_MONEY) {
        strError = _("Invalid parameter: token amount greater than max money: ") + std::to_string(MAX_MONEY / COIN);
        return false;
    }

    if (token.units < 0 || token.units > 8) {
        strError = _("Invalid parameter: units must be between 0-8.");
        return false;
    }

    if (!CheckAmountWithUnits(token.nAmount, token.units)) {
        strError = _("Invalid parameter: amount must be divisible by the smaller unit assigned to the token");
        return false;
    }

    if (token.nReissuable != 0 && token.nReissuable != 1) {
        strError = _("Invalid parameter: reissuable must be 0 or 1");
        return false;
    }

    if (token.nHasIPFS != 0 && token.nHasIPFS != 1) {
        strError = _("Invalid parameter: has_ipfs must be 0 or 1.");
        return false;
    }

    return true;
}

bool ContextualCheckNewToken(CTokensCache* tokenCache, const CNewToken& token, std::string& strError, bool fCheckMempool)
{
    if (!AreTokensDeployed() && !fUnitTest) {
        strError = "bad-txns-new-token-when-tokens-is-not-active";
        return false;
    }

    if (!CheckNewToken(token, strError))
        return false;

    // Check our current cache to see if the token has been created yet
    if (tokenCache->CheckIfTokenExists(token.strName, true)) {
        strError = std::string(_("Invalid parameter: token_name '")) + token.strName + std::string(_("' has already been used"));
        return false;
    }

    // Check the mempool
    if (fCheckMempool) {
        if (mempool.mapTokenToHash.count(token.strName)) {
            strError = _("Token with this name is already in the mempool");
            return false;
        }
    }

    // Check the ipfs hash as it changes when messaging goes active
    if (token.nHasIPFS && token.strIPFSHash.size() != 34) {
        if (!AreMessagesDeployed()) {
            strError = _("Invalid parameter: ipfs_hash must be 46 characters. Txid must be valid 64 character hash");
            return false;
        } else {
            if (token.strIPFSHash.size() != 32) {
                strError = _("Invalid parameter: ipfs_hash must be 46 characters. Txid must be valid 64 character hash");
                return false;
            }
        }
    }

    if (token.nHasIPFS) {
        if (!CheckEncoded(token.strIPFSHash, strError))
            return false;
    }

    return true;
}

bool CheckReissueToken(const CReissueToken& token, std::string& strError)
{
    strError = "";

    if (token.nAmount < 0 || token.nAmount >= MAX_MONEY) {
        strError = _("Unable to reissue token: amount must be 0 or larger");
        return false;
    }

    if (token.nUnits > MAX_UNIT || token.nUnits < -1) {
        strError = _("Unable to reissue token: unit must be between 8 and -1");
        return false;
    }

    /// -------- TESTNET ONLY ---------- ///
    // Testnet has a couple blocks that have invalid nReissue values before constriants were created
    bool fSkip = false;
    if (GetParams().NetworkIDString() == CBaseChainParams::TESTNET) {
        if (token.strName == "GAMINGWEB" && token.nReissuable == 109) {
            fSkip = true;
        } else if (token.strName == "UINT8" && token.nReissuable == -47) {
            fSkip = true;
        }
    }
    /// -------- TESTNET ONLY ---------- ///

    if (!fSkip && token.nReissuable != 0 && token.nReissuable != 1) {
        strError = _("Unable to reissue token: reissuable must be 0 or 1");
        return false;
    }

    KnownTokenType type;
    IsTokenNameValid(token.strName, type);

    if (type == KnownTokenType::RESTRICTED) {
        // TODO Add checks for restricted token if we can come up with any
    }

    return true;
}

bool ContextualCheckReissueToken(CTokensCache* tokenCache, const CReissueToken& reissue_token, std::string& strError, const CTransaction& tx)
{
    // We are using this just to get the strAddress
    CReissueToken reissue;
    std::string strAddress;
    if (!ReissueTokenFromTransaction(tx, reissue, strAddress)) {
        strError = "bad-txns-reissue-token-contextual-check";
        return false;
    }

    // run non contextual checks
    if (!CheckReissueToken(reissue_token, strError))
        return false;

    // Check previous token data with the reissuesd data
    CNewToken prev_token;
    if (!tokenCache->GetTokenMetaDataIfExists(reissue_token.strName, prev_token)) {
        strError = _("Unable to reissue token: token_name '") + reissue_token.strName + _("' doesn't exist in the database");
        return false;
    }

    if (!prev_token.nReissuable) {
        // Check to make sure the token can be reissued
        strError = _("Unable to reissue token: reissuable is set to false");
        return false;
    }

    if (prev_token.nAmount + reissue_token.nAmount > MAX_MONEY) {
        strError = _("Unable to reissue token: token_name '") + reissue_token.strName +
                   _("' the amount trying to reissue is to large");
        return false;
    }

    if (!CheckAmountWithUnits(reissue_token.nAmount, prev_token.units)) {
        strError = _("Unable to reissue token: amount must be divisible by the smaller unit assigned to the token");
        return false;
    }

    if (reissue_token.nUnits < prev_token.units && reissue_token.nUnits != -1) {
        strError = _("Unable to reissue token: unit must be larger than current unit selection");
        return false;
    }

    // Check the ipfs hash
    if (reissue_token.strIPFSHash != "" && reissue_token.strIPFSHash.size() != 34 && (AreMessagesDeployed() && reissue_token.strIPFSHash.size() != 32)) {
        strError = _("Invalid parameter: ipfs_hash must be 34 bytes, Txid must be 32 bytes");
        return false;
    }

    if (reissue_token.strIPFSHash != "") {
        if (!CheckEncoded(reissue_token.strIPFSHash, strError))
            return false;
    }

    if (IsTokenNameAnRestricted(reissue_token.strName)) {
        CNullTokenTxVerifierString new_verifier;
        bool fNotFound = false;

        // Try and get the verifier string if it was changed
        if (!tx.GetVerifierStringFromTx(new_verifier, strError, fNotFound)) {
            // If it return false for any other reason besides not being found, fail the transaction check
            if (!fNotFound) {
                return false;
            }
        }

        if (reissue_token.nAmount > 0) {
            // If it wasn't found, get the current verifier and validate against it
            if (fNotFound) {
                CNullTokenTxVerifierString current_verifier;
                if (tokenCache->GetTokenVerifierStringIfExists(reissue_token.strName, current_verifier)) {
                    if (!ContextualCheckVerifierString(tokenCache, current_verifier.verifier_string, strAddress, strError))
                        return false;
                } else {
                    // This should happen, but if it does. The wallet needs to shutdown,
                    // TODO, remove this after restricted tokens have been tested in testnet for some time, and this hasn't happened yet. It this has happened. Investigation is required by the dev team
                    error("%s : failed to get verifier string from a restricted token, this shouldn't happen, database is out of sync. Reindex required. Please report this is to development team token name: %s, txhash : %s",__func__, reissue_token.strName, tx.GetHash().GetHex());
                    strError = "failed to get verifier string from a restricted token, database is out of sync. Reindex required. Please report this is to development team";
                    return false;
                }
            } else {
                if (!ContextualCheckVerifierString(tokenCache, new_verifier.verifier_string, strAddress, strError))
                    return false;
            }
        }
    }


    return true;
}

bool ContextualCheckReissueToken(CTokensCache* tokenCache, const CReissueToken& reissue_token, std::string& strError)
{
    // run non contextual checks
    if (!CheckReissueToken(reissue_token, strError))
        return false;

    // Check previous token data with the reissuesd data
    if (tokenCache) {
        CNewToken prev_token;
        if (!tokenCache->GetTokenMetaDataIfExists(reissue_token.strName, prev_token)) {
            strError = _("Unable to reissue token: token_name '") + reissue_token.strName +
                       _("' doesn't exist in the database");
            return false;
        }

        if (!prev_token.nReissuable) {
            // Check to make sure the token can be reissued
            strError = _("Unable to reissue token: reissuable is set to false");
            return false;
        }

        if (prev_token.nAmount + reissue_token.nAmount > MAX_MONEY) {
            strError = _("Unable to reissue token: token_name '") + reissue_token.strName +
                       _("' the amount trying to reissue is to large");
            return false;
        }

        if (!CheckAmountWithUnits(reissue_token.nAmount, prev_token.units)) {
            strError = _("Unable to reissue token: amount must be divisible by the smaller unit assigned to the token");
            return false;
        }

        if (reissue_token.nUnits < prev_token.units && reissue_token.nUnits != -1) {
            strError = _("Unable to reissue token: unit must be larger than current unit selection");
            return false;
        }
    }

    // Check the ipfs hash
    if (reissue_token.strIPFSHash != "" && reissue_token.strIPFSHash.size() != 34 && (AreMessagesDeployed() && reissue_token.strIPFSHash.size() != 32)) {
        strError = _("Invalid parameter: ipfs_hash must be 34 bytes, Txid must be 32 bytes");
        return false;
    }

    if (reissue_token.strIPFSHash != "") {
        if (!CheckEncoded(reissue_token.strIPFSHash, strError))
            return false;
    }

    return true;
}

bool ContextualCheckUniqueTokenTx(CTokensCache* tokenCache, std::string& strError, const CTransaction& tx)
{
    for (auto out : tx.vout)
    {
        if (IsScriptNewUniqueToken(out.scriptPubKey))
        {
            CNewToken token;
            std::string strAddress;
            if (!TokenFromScript(out.scriptPubKey, token, strAddress)) {
                strError = "bad-txns-issue-unique-serialization-failed";
                return false;
            }

            if (!ContextualCheckUniqueToken(tokenCache, token, strError))
                return false;
        }
    }

    return true;
}

bool ContextualCheckUniqueToken(CTokensCache* tokenCache, const CNewToken& unique_token, std::string& strError)
{
    if (!ContextualCheckNewToken(tokenCache, unique_token, strError))
        return false;

    return true;
}

std::string GetUserErrorString(const ErrorReport& report)
{
    switch (report.type) {
        case ErrorReport::ErrorType::NotSetError: return _("Error not set");
        case ErrorReport::ErrorType::InvalidQualifierName: return _("Invalid Qualifier Name: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::EmptyString: return _("Verifier string is empty");
        case ErrorReport::ErrorType::LengthToLarge: return _("Length is to large. Please use a smaller length");
        case ErrorReport::ErrorType::InvalidSubExpressionFormula: return _("Invalid expressions in verifier string: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::InvalidSyntax: return _("Invalid syntax: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::TokenDoesntExist: return _("Token doesn't exist: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::FailedToVerifyAgainstAddress: return _("This address doesn't contain the correct tags to pass the verifier string check: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::EmptySubExpression: return _("The verifier string has two operators without a tag between them");
        case ErrorReport::ErrorType::UnknownOperator: return _("The symbol: '") + report.vecUserData[0] + _("' is not a valid character in the expression: ") + report.vecUserData[1];
        case ErrorReport::ErrorType::ParenthesisParity: return _("Every '(' must have a corresponding ')' in the expression: ") + report.vecUserData[0];
        case ErrorReport::ErrorType::VariableNotFound: return _("Variable is not allow in the expression: '") + report.vecUserData[0] + "'";;
        default:
            return _("Error not set");
    }
}
