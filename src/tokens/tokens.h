// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef YONACOIN_TOKEN_PROTOCOL_H
#define YONACOIN_TOKEN_PROTOCOL_H

#include "amount.h"
#include "tinyformat.h"
#include "tokentypes.h"

#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <list>

#define TOKEN_Y 121
#define TOKEN_N 110
#define TOKEN_A 97
#define TOKEN_Q 113
#define TOKEN_T 116
#define TOKEN_O 111
#define TOKEN_R 114

#define DEFAULT_UNITS 0
#define DEFAULT_REISSUABLE 1
#define DEFAULT_HAS_IPFS 0
#define DEFAULT_IPFS ""
#define MIN_TOKEN_LENGTH 3
#define MAX_TOKEN_LENGTH 32
#define OWNER_TAG "!"
#define OWNER_LENGTH 1
#define OWNER_UNITS 0
#define OWNER_TOKEN_AMOUNT 1 * COIN
#define UNIQUE_TOKEN_AMOUNT 1 * COIN
#define UNIQUE_TOKEN_UNITS 0
#define UNIQUE_TOKENS_REISSUABLE 0

#define RESTRICTED_CHAR '$'
#define QUALIFIER_CHAR '#'

#define QUALIFIER_TOKEN_MIN_AMOUNT 1 * COIN
#define QUALIFIER_TOKEN_MAX_AMOUNT 10 * COIN
#define QUALIFIER_TOKEN_UNITS 0

#define TOKEN_TRANSFER_STRING "transfer_token"
#define TOKEN_NEW_STRING "new_token"
#define TOKEN_REISSUE_STRING "reissue_token"

#define MINIMUM_REWARDS_PAYOUT_HEIGHT 60

class CScript;
class CDataStream;
class CTransaction;
class CTxOut;
class Coin;
class CWallet;
class CReserveKey;
class CWalletTx;
struct CTokenOutputEntry;
class CCoinControl;
struct CBlockTokenUndo;
class COutput;

// 2500 * 82 Bytes == 205 KB (kilobytes) of memory
#define MAX_CACHE_TOKENS_SIZE 2500

// Create map that store that state of current reissued transaction that the mempool as accepted.
// If an token name is in this map, any other reissue transactions wont be accepted into the mempool
extern std::map<uint256, std::string> mapReissuedTx;
extern std::map<std::string, uint256> mapReissuedTokens;

class CTokens {
public:
    std::map<std::pair<std::string, std::string>, CAmount> mapTokensAddressAmount; // pair < Token Name , Address > -> Quantity of tokens in the address

    // Dirty, Gets wiped once flushed to database
    std::map<std::string, CNewToken> mapReissuedTokenData; // Token Name -> New Token Data

    CTokens(const CTokens& tokens) {
        this->mapTokensAddressAmount = tokens.mapTokensAddressAmount;
        this->mapReissuedTokenData = tokens.mapReissuedTokenData;
    }

    CTokens& operator=(const CTokens& other) {
        mapTokensAddressAmount = other.mapTokensAddressAmount;
        mapReissuedTokenData = other.mapReissuedTokenData;
        return *this;
    }

    CTokens() {
        SetNull();
    }

    void SetNull() {
        mapTokensAddressAmount.clear();
        mapReissuedTokenData.clear();
    }
};

struct ErrorReport {

    enum ErrorType {
        NotSetError = 0,
        InvalidQualifierName = 1,
        EmptyString = 2,
        LengthToLarge = 3,
        InvalidSubExpressionFormula = 4,
        InvalidSyntax = 5,
        TokenDoesntExist = 6,
        FailedToVerifyAgainstAddress = 7,
        EmptySubExpression = 8,
        UnknownOperator = 9,
        ParenthesisParity = 10,
        VariableNotFound = 11
    };

    ErrorType type = ErrorType::NotSetError;
    std::string strDevData;
    std::vector<std::string> vecUserData;
};

std::string GetUserErrorString(const ErrorReport& report);

class CTokensCache : public CTokens
{
private:
    bool AddBackSpentToken(const Coin& coin, const std::string& tokenName, const std::string& address, const CAmount& nAmount, const COutPoint& out);
    void AddToTokenBalance(const std::string& strName, const std::string& address, const CAmount& nAmount);
    bool UndoTransfer(const CTokenTransfer& transfer, const std::string& address, const COutPoint& outToRemove);
public :
    //! These are memory only containers that show dirty entries that will be databased when flushed
    std::vector<CTokenCacheUndoTokenAmount> vUndoTokenAmount;
    std::vector<CTokenCacheSpendToken> vSpentTokens;

    //! New Tokens Caches
    std::set<CTokenCacheNewToken> setNewTokensToRemove;
    std::set<CTokenCacheNewToken> setNewTokensToAdd;

    //! New Reissue Caches
    std::set<CTokenCacheReissueToken> setNewReissueToRemove;
    std::set<CTokenCacheReissueToken> setNewReissueToAdd;

    //! Ownership Tokens Caches
    std::set<CTokenCacheNewOwner> setNewOwnerTokensToAdd;
    std::set<CTokenCacheNewOwner> setNewOwnerTokensToRemove;

    //! Transfer Tokens Caches
    std::set<CTokenCacheNewTransfer> setNewTransferTokensToAdd;
    std::set<CTokenCacheNewTransfer> setNewTransferTokensToRemove;

    //! Qualfier Address Token Caches
    std::set<CTokenCacheQualifierAddress> setNewQualifierAddressToAdd;
    std::set<CTokenCacheQualifierAddress> setNewQualifierAddressToRemove;

    //! Restricted Address Token Caches
    std::set<CTokenCacheRestrictedAddress> setNewRestrictedAddressToAdd;
    std::set<CTokenCacheRestrictedAddress> setNewRestrictedAddressToRemove;

    //! Restricted Global Token Caches
    std::set<CTokenCacheRestrictedGlobal> setNewRestrictedGlobalToAdd;
    std::set<CTokenCacheRestrictedGlobal> setNewRestrictedGlobalToRemove;

    //! Restricted Tokens Verifier Caches
    std::set<CTokenCacheRestrictedVerifiers> setNewRestrictedVerifierToAdd;
    std::set<CTokenCacheRestrictedVerifiers> setNewRestrictedVerifierToRemove;

    //! Root Qualifier Address Map
    std::map<CTokenCacheRootQualifierChecker, std::set<std::string> > mapRootQualifierAddressesAdd;
    std::map<CTokenCacheRootQualifierChecker, std::set<std::string> > mapRootQualifierAddressesRemove;

    CTokensCache() : CTokens()
    {
        SetNull();
        ClearDirtyCache();
    }

    CTokensCache(const CTokensCache& cache) : CTokens(cache)
    {
        //! Copy dirty cache also
        this->vSpentTokens = cache.vSpentTokens;
        this->vUndoTokenAmount = cache.vUndoTokenAmount;

        //! Transfer Caches
        this->setNewTransferTokensToAdd = cache.setNewTransferTokensToAdd;
        this->setNewTransferTokensToRemove = cache.setNewTransferTokensToRemove;

        //! Issue Caches
        this->setNewTokensToRemove = cache.setNewTokensToRemove;
        this->setNewTokensToAdd = cache.setNewTokensToAdd;

        //! Reissue Caches
        this->setNewReissueToRemove = cache.setNewReissueToRemove;
        this->setNewReissueToAdd = cache.setNewReissueToAdd;

        //! Owner Caches
        this->setNewOwnerTokensToAdd = cache.setNewOwnerTokensToAdd;
        this->setNewOwnerTokensToRemove = cache.setNewOwnerTokensToRemove;

        //! Qualifier Caches
        this->setNewQualifierAddressToAdd = cache.setNewQualifierAddressToAdd;
        this->setNewQualifierAddressToRemove = cache.setNewQualifierAddressToRemove;

        //! Restricted Address Caches
        this->setNewRestrictedAddressToAdd = cache.setNewRestrictedAddressToAdd;
        this->setNewRestrictedAddressToRemove = cache.setNewRestrictedAddressToRemove;

        //! Restricted Global Caches
        this->setNewRestrictedGlobalToAdd = cache.setNewRestrictedGlobalToAdd;
        this->setNewRestrictedGlobalToRemove = cache.setNewRestrictedGlobalToRemove;

        //! Restricted Verifier Caches
        this->setNewRestrictedVerifierToAdd = cache.setNewRestrictedVerifierToAdd;
        this->setNewRestrictedVerifierToRemove = cache.setNewRestrictedVerifierToRemove;

        //! Root Qualifier Address Map
        this->mapRootQualifierAddressesAdd = cache.mapRootQualifierAddressesAdd;
        this->mapRootQualifierAddressesRemove = cache.mapRootQualifierAddressesRemove;
    }

    CTokensCache& operator=(const CTokensCache& cache)
    {
        this->mapTokensAddressAmount = cache.mapTokensAddressAmount;
        this->mapReissuedTokenData = cache.mapReissuedTokenData;

        //! Copy dirty cache also
        this->vSpentTokens = cache.vSpentTokens;
        this->vUndoTokenAmount = cache.vUndoTokenAmount;

        //! Transfer Caches
        this->setNewTransferTokensToAdd = cache.setNewTransferTokensToAdd;
        this->setNewTransferTokensToRemove = cache.setNewTransferTokensToRemove;

        //! Issue Caches
        this->setNewTokensToRemove = cache.setNewTokensToRemove;
        this->setNewTokensToAdd = cache.setNewTokensToAdd;

        //! Reissue Caches
        this->setNewReissueToRemove = cache.setNewReissueToRemove;
        this->setNewReissueToAdd = cache.setNewReissueToAdd;

        //! Owner Caches
        this->setNewOwnerTokensToAdd = cache.setNewOwnerTokensToAdd;
        this->setNewOwnerTokensToRemove = cache.setNewOwnerTokensToRemove;

        //! Qualifier Caches
        this->setNewQualifierAddressToAdd = cache.setNewQualifierAddressToAdd;
        this->setNewQualifierAddressToRemove = cache.setNewQualifierAddressToRemove;

        //! Restricted Address Caches
        this->setNewRestrictedAddressToAdd = cache.setNewRestrictedAddressToAdd;
        this->setNewRestrictedAddressToRemove = cache.setNewRestrictedAddressToRemove;

        //! Restricted Global Caches
        this->setNewRestrictedGlobalToAdd = cache.setNewRestrictedGlobalToAdd;
        this->setNewRestrictedGlobalToRemove = cache.setNewRestrictedGlobalToRemove;

        //! Restricted Verifier Caches
        this->setNewRestrictedVerifierToAdd = cache.setNewRestrictedVerifierToAdd;
        this->setNewRestrictedVerifierToRemove = cache.setNewRestrictedVerifierToRemove;

        //! Root Qualifier Address Map
        this->mapRootQualifierAddressesAdd = cache.mapRootQualifierAddressesAdd;
        this->mapRootQualifierAddressesRemove = cache.mapRootQualifierAddressesRemove;

        return *this;
    }

    //! Cache only undo functions
    bool RemoveNewToken(const CNewToken& token, const std::string address);
    bool RemoveTransfer(const CTokenTransfer& transfer, const std::string& address, const COutPoint& out);
    bool RemoveOwnerToken(const std::string& tokensName, const std::string address);
    bool RemoveReissueToken(const CReissueToken& reissue, const std::string address, const COutPoint& out, const std::vector<std::pair<std::string, CBlockTokenUndo> >& vUndoIPFS);
    bool UndoTokenCoin(const Coin& coin, const COutPoint& out);
    bool RemoveQualifierAddress(const std::string& tokenName, const std::string& address, const QualifierType type);
    bool RemoveRestrictedAddress(const std::string& tokenName, const std::string& address, const RestrictedType type);
    bool RemoveGlobalRestricted(const std::string& tokenName, const RestrictedType type);
    bool RemoveRestrictedVerifier(const std::string& tokenName, const std::string& verifier, const bool fUndoingReissue = false);

    //! Cache only add token functions
    bool AddNewToken(const CNewToken& token, const std::string address, const int& nHeight, const uint256& blockHash);
    bool AddTransferToken(const CTokenTransfer& transferToken, const std::string& address, const COutPoint& out, const CTxOut& txOut);
    bool AddOwnerToken(const std::string& tokensName, const std::string address);
    bool AddReissueToken(const CReissueToken& reissue, const std::string address, const COutPoint& out);
    bool AddQualifierAddress(const std::string& tokenName, const std::string& address, const QualifierType type);
    bool AddRestrictedAddress(const std::string& tokenName, const std::string& address, const RestrictedType type);
    bool AddGlobalRestricted(const std::string& tokenName, const RestrictedType type);
    bool AddRestrictedVerifier(const std::string& tokenName, const std::string& verifier);

    //! Cache only validation functions
    bool TrySpendCoin(const COutPoint& out, const CTxOut& coin);

    //! Help functions
    bool ContainsToken(const CNewToken& token);
    bool ContainsToken(const std::string& tokenName);

    //! Returns true if an token with this name already exists
    bool CheckIfTokenExists(const std::string& name, bool fForceDuplicateCheck = true);

    //! Returns true if an token with the name exists, and it was able to get the token metadata from database
    bool GetTokenMetaDataIfExists(const std::string &name, CNewToken &token);
    bool GetTokenMetaDataIfExists(const std::string &name, CNewToken &token, int& nHeight, uint256& blockHash);

    //! Returns true if the Token Verifier String was found for an token_name, if fSkipTempCache is true, it will only search ptokens pointer and databases
    bool GetTokenVerifierStringIfExists(const std::string &name, CNullTokenTxVerifierString& verifier, bool fSkipTempCache = false);

    //! Return true if the address has the given qualifier assigned to it
    bool CheckForAddressQualifier(const std::string &qualifier_name, const std::string& address, bool fSkipTempCache = false);

    //! Return true if the address is marked as frozen
    bool CheckForAddressRestriction(const std::string &restricted_name, const std::string& address, bool fSkipTempCache = false);

    //! Return true if the restricted token is globally freezing trading
    bool CheckForGlobalRestriction(const std::string &restricted_name, bool fSkipTempCache = false);

    //! Calculate the size of the CTokens (in bytes)
    size_t DynamicMemoryUsage() const;

    //! Get the size of the none databased cache
    size_t GetCacheSize() const;
    size_t GetCacheSizeV2() const;

    //! Flush all new cache entries into the ptokens global cache
    bool Flush();

    //! Write token cache data to database
    bool DumpCacheToDatabase();

    //! Clear all dirty cache sets, vetors, and maps
    void ClearDirtyCache() {

        vUndoTokenAmount.clear();
        vSpentTokens.clear();

        setNewTokensToRemove.clear();
        setNewTokensToAdd.clear();

        setNewReissueToAdd.clear();
        setNewReissueToRemove.clear();

        setNewTransferTokensToAdd.clear();
        setNewTransferTokensToRemove.clear();

        setNewOwnerTokensToAdd.clear();
        setNewOwnerTokensToRemove.clear();

        mapReissuedTokenData.clear();
        mapTokensAddressAmount.clear();

        setNewQualifierAddressToAdd.clear();
        setNewQualifierAddressToRemove.clear();

        setNewRestrictedAddressToAdd.clear();
        setNewRestrictedAddressToRemove.clear();

        setNewRestrictedGlobalToAdd.clear();
        setNewRestrictedGlobalToRemove.clear();

        setNewRestrictedVerifierToAdd.clear();
        setNewRestrictedVerifierToRemove.clear();

        mapRootQualifierAddressesAdd.clear();
        mapRootQualifierAddressesRemove.clear();
    }

   std::string CacheToString() const {

       return strprintf(
               "vNewTokensToRemove size : %d, vNewTokensToAdd size : %d, vNewTransfer size : %d, vSpentTokens : %d\n"
               "setNewQualifierAddressToAdd size : %d, setNewQualifierAddressToRemove size : %d, setNewRestrictedAddressToAdd size : %d\n"
               "setNewRestrictedAddressToRemove size : %d, setNewRestrictedGlobalToAdd size : %d, setNewRestrictedGlobalToRemove : %d",
               setNewTokensToRemove.size(), setNewTokensToAdd.size(), setNewTransferTokensToAdd.size(),
               vSpentTokens.size(), setNewQualifierAddressToAdd.size(), setNewQualifierAddressToRemove.size(), setNewRestrictedAddressToAdd.size(),
               setNewRestrictedAddressToRemove.size(), setNewRestrictedGlobalToAdd.size(), setNewRestrictedGlobalToRemove.size());
   }
};

//! Functions to be used to get access to the current burn amount required for specific token issuance transactions
CAmount GetIssueTokenBurnAmount();
CAmount GetReissueTokenBurnAmount();
CAmount GetIssueSubTokenBurnAmount();
CAmount GetIssueUniqueTokenBurnAmount();
CAmount GetIssueMsgChannelTokenBurnAmount();
CAmount GetIssueQualifierTokenBurnAmount();
CAmount GetIssueSubQualifierTokenBurnAmount();
CAmount GetIssueRestrictedTokenBurnAmount();
CAmount GetAddNullQualifierTagBurnAmount();
CAmount GetBurnAmount(const KnownTokenType type);
CAmount GetBurnAmount(const int nType);

//! Functions to be used to get access to the burn address for a given token type issuance
std::string GetBurnAddress(const KnownTokenType type);
std::string GetBurnAddress(const int nType);

void GetTxOutKnownTokenTypes(const std::vector<CTxOut>& vout, int& issues, int& reissues, int& transfers, int& owners);

//! Check is an token name is valid, and being able to return the token type if needed
bool IsTokenNameValid(const std::string& name);
bool IsTokenNameValid(const std::string& name, KnownTokenType& tokenType);
bool IsTokenNameValid(const std::string& name, KnownTokenType& tokenType, std::string& error);

//! Check if an unique tagname is valid
bool IsUniqueTagValid(const std::string& tag);

//! Check if an token is an owner
bool IsTokenNameAnOwner(const std::string& name);

//! Check if an token is a restricted token
bool IsTokenNameAnRestricted(const std::string& name);

//! Check if an token is a qualifier token or sub qualifier
bool IsTokenNameAQualifier(const std::string& name, bool fOnlyQualifiers = false);

//! Check if an token is a sub qualifier
bool IsTokenNameASubQualifier(const std::string& name);

//! Check if an token is a message channel
bool IsTokenNameAnMsgChannel(const std::string& name);

bool IsTokenNameARoot(const std::string& name);

//! Get the root name of an token
std::string GetParentName(const std::string& name); // Gets the parent name of a subtoken TEST/TESTSUB would return TEST

//! Get the owner token name belonging to a restricted token
std::string RestrictedNameToOwnerName(const std::string& name);

//! Build a unique token buy giving the root name, and the tag name (ROOT, TAG) => ROOT#TAG
std::string GetUniqueTokenName(const std::string& parent, const std::string& tag);

//! Given a type, and an token name, return if that name is valid based on the type
bool IsTypeCheckNameValid(const KnownTokenType type, const std::string& name, std::string& error);

//! These types of token tx, have specific metadata at certain indexes in the transaction.
//! These functions pull data from the scripts at those indexes
bool TokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress);
bool OwnerFromTransaction(const CTransaction& tx, std::string& ownerName, std::string& strAddress);
bool ReissueTokenFromTransaction(const CTransaction& tx, CReissueToken& reissue, std::string& strAddress);
bool UniqueTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress);
bool MsgChannelTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress);
bool QualifierTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress);
bool RestrictedTokenFromTransaction(const CTransaction& tx, CNewToken& token, std::string& strAddress);

//! Get specific token type metadata from the given scripts
bool TransferTokenFromScript(const CScript& scriptPubKey, CTokenTransfer& tokenTransfer, std::string& strAddress);
bool TokenFromScript(const CScript& scriptPubKey, CNewToken& token, std::string& strAddress);
bool OwnerTokenFromScript(const CScript& scriptPubKey, std::string& tokenName, std::string& strAddress);
bool ReissueTokenFromScript(const CScript& scriptPubKey, CReissueToken& reissue, std::string& strAddress);
bool MsgChannelTokenFromScript(const CScript& scriptPubKey, CNewToken& token, std::string& strAddress);
bool QualifierTokenFromScript(const CScript& scriptPubKey, CNewToken& token, std::string& strAddress);
bool RestrictedTokenFromScript(const CScript& scriptPubKey, CNewToken& token, std::string& strAddress);
bool TokenNullDataFromScript(const CScript& scriptPubKey, CNullTokenTxData& tokenData, std::string& strAddress);
bool TokenNullVerifierDataFromScript(const CScript& scriptPubKey, CNullTokenTxVerifierString& verifierData);
bool GlobalTokenNullDataFromScript(const CScript& scriptPubKey, CNullTokenTxData& tokenData);

//! Check to make sure the script contains the burn transaction
bool CheckIssueBurnTx(const CTxOut& txOut, const KnownTokenType& type, const int numberIssued);
bool CheckIssueBurnTx(const CTxOut& txOut, const KnownTokenType& type);

// TODO, maybe remove this function and input that check into the CheckIssueBurnTx.
//! Check to make sure the script contains the reissue burn data
bool CheckReissueBurnTx(const CTxOut& txOut);

//! issue token scripts to make sure script meets the standards
bool CheckIssueDataTx(const CTxOut& txOut); // OP_YONA_TOKEN YONAQ (That is a Q as in Que not an O)
bool CheckOwnerDataTx(const CTxOut& txOut);// OP_YONA_TOKEN YONAO
bool CheckReissueDataTx(const CTxOut& txOut);// OP_YONA_TOKEN YONAR
bool CheckTransferOwnerTx(const CTxOut& txOut);// OP_YONA_TOKEN YONAT

//! Check the Encoded hash and make sure it is either an IPFS hash or a OIP hash
bool CheckEncoded(const std::string& hash, std::string& strError);

//! Checks the amount and units, and makes sure that the amount uses the correct decimals
bool CheckAmountWithUnits(const CAmount& nAmount, const int8_t nUnits);

//! Check script and see if it matches the token issuance template
bool IsScriptNewToken(const CScript& scriptPubKey);
bool IsScriptNewToken(const CScript& scriptPubKey, int& nStartingIndex);

//! Check script and see if it matches the unquie issuance template
bool IsScriptNewUniqueToken(const CScript& scriptPubKey);
bool IsScriptNewUniqueToken(const CScript &scriptPubKey, int &nStartingIndex);

//! Check script and see if it matches the owner issuance template
bool IsScriptOwnerToken(const CScript& scriptPubKey);
bool IsScriptOwnerToken(const CScript& scriptPubKey, int& nStartingIndex);

//! Check script and see if it matches the reissue template
bool IsScriptReissueToken(const CScript& scriptPubKey);
bool IsScriptReissueToken(const CScript& scriptPubKey, int& nStartingIndex);

//! Check script and see if it matches the transfer token template
bool IsScriptTransferToken(const CScript& scriptPubKey);
bool IsScriptTransferToken(const CScript& scriptPubKey, int& nStartingIndex);

//! Check script and see if it matches the message channel issuance template
bool IsScriptNewMsgChannelToken(const CScript& scriptPubKey);
bool IsScriptNewMsgChannelToken(const CScript& scriptPubKey, int& nStartingIndex);

//! Check script and see if it matches the qualifier issuance template
bool IsScriptNewQualifierToken(const CScript& scriptPubKey);
bool IsScriptNewQualifierToken(const CScript &scriptPubKey, int &nStartingIndex);

//! Check script and see if it matches the restricted issueance template
bool IsScriptNewRestrictedToken(const CScript& scriptPubKey);
bool IsScriptNewRestrictedToken(const CScript &scriptPubKey, int &nStartingIndex);

bool IsNewOwnerTxValid(const CTransaction& tx, const std::string& tokenName, const std::string& address, std::string& errorMsg);

void GetAllAdministrativeTokens(CWallet *pwallet, std::vector<std::string> &names, int nMinConf = 1);
void GetAllMyTokens(CWallet* pwallet, std::vector<std::string>& names, int nMinConf = 1, bool fIncludeAdministrator = false, bool fOnlyAdministrator = false);

bool GetTokenInfoFromCoin(const Coin& coin, std::string& strName, CAmount& nAmount, uint32_t& nTimeLock);
bool GetTokenInfoFromScript(const CScript& scriptPubKey, std::string& strName, CAmount& nAmount, uint32_t& nTokenLockTime);

bool GetTokenData(const CScript& script, CTokenOutputEntry& data);

bool GetBestTokenAddressAmount(CTokensCache& cache, const std::string& tokenName, const std::string& address);


//! Decode and Encode IPFS hashes, or OIP hashes
std::string DecodeTokenData(std::string encoded);
std::string EncodeTokenData(std::string decoded);
std::string DecodeIPFS(std::string encoded);
std::string EncodeIPFS(std::string decoded);

#ifdef ENABLE_WALLET

bool GetAllMyTokenBalances(std::map<std::string, std::vector<COutput> >& outputs, std::map<std::string, CAmount>& amounts, const int confirmations = 0, const std::string& prefix = "");
bool GetMyTokenBalance(const std::string& name, CAmount& balance, const int& confirmations);

//! Creates new token issuance transaction
bool CreateTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const CNewToken& token, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string = nullptr);
bool CreateTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const std::vector<CNewToken> tokens, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string = nullptr);

//! Create a reissue token transaction
bool CreateReissueTokenTransaction(CWallet* pwallet, CCoinControl& coinControl, const CReissueToken& token, const std::string& address, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::string* verifier_string = nullptr);


//! Create a transfer token transaction
bool CreateTransferTokenTransaction(CWallet* pwallet, const CCoinControl& coinControl, const std::vector< std::pair<CTokenTransfer, std::string> >vTransfers, const std::string& changeAddress, std::pair<int, std::string>& error, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRequired, std::vector<std::pair<CNullTokenTxData, std::string> >* nullTokenTxData = nullptr, std::vector<CNullTokenTxData>* nullGlobalRestrictionData = nullptr);

//! Send any type of token transaction to the network
bool SendTokenTransaction(CWallet* pwallet, CWalletTx& transaction, CReserveKey& reserveKey, std::pair<int, std::string>& error, std::string& txid);

/** Verifies that this wallet owns the give token */
bool VerifyWalletHasToken(const std::string& token_name, std::pair<int, std::string>& pairError);
#endif

/** Helper method for extracting address bytes, token name and amount from an token script */
bool ParseTokenScript(CScript scriptPubKey, uint160 &hashBytes, int& nScriptType, std::string &tokenName, CAmount &tokenAmount);

/** Helper method for extracting #TAGS from a verifier string */
void ExtractVerifierStringQualifiers(const std::string& verifier, std::set<std::string>& qualifiers);
bool CheckVerifierString(const std::string& verifier, std::set<std::string>& setFoundQualifiers, std::string& strError, ErrorReport* errorReport = nullptr);
std::string GetStrippedVerifierString(const std::string& verifier);

/** Helper methods that validate changes to null token data transaction databases */
bool VerifyNullTokenDataFlag(const int& flag, std::string& strError);
bool VerifyQualifierChange(CTokensCache& cache, const CNullTokenTxData& data, const std::string& address, std::string& strError);
bool VerifyRestrictedAddressChange(CTokensCache& cache, const CNullTokenTxData& data, const std::string& address, std::string& strError);
bool VerifyGlobalRestrictedChange(CTokensCache& cache, const CNullTokenTxData& data, std::string& strError);

//// Non Contextual Check functions
bool CheckVerifierTokenTxOut(const CTxOut& txout, std::string& strError);
bool CheckNewToken(const CNewToken& token, std::string& strError);
bool CheckReissueToken(const CReissueToken& token, std::string& strError);

//// Contextual Check functions
bool ContextualCheckNullTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError, std::vector<std::pair<std::string, CNullTokenTxData>>* myNullTokenData = nullptr);
bool ContextualCheckGlobalTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError);
bool ContextualCheckVerifierTokenTxOut(const CTxOut& txout, CTokensCache* tokenCache, std::string& strError);
bool ContextualCheckVerifierString(CTokensCache* cache, const std::string& verifier, const std::string& check_address, std::string& strError, ErrorReport* errorReport = nullptr);
bool ContextualCheckNewToken(CTokensCache* tokenCache, const CNewToken& token, std::string& strError, bool fCheckMempool = false);
bool ContextualCheckTransferToken(CTokensCache* tokenCache, const CTokenTransfer& transfer, const std::string& address, std::string& strError);
bool ContextualCheckReissueToken(CTokensCache* tokenCache, const CReissueToken& reissue_token, std::string& strError, const CTransaction& tx);
bool ContextualCheckReissueToken(CTokensCache* tokenCache, const CReissueToken& reissue_token, std::string& strError);
bool ContextualCheckUniqueTokenTx(CTokensCache* tokenCache, std::string& strError, const CTransaction& tx);
bool ContextualCheckUniqueToken(CTokensCache* tokenCache, const CNewToken& unique_token, std::string& strError);

#endif //YONACOIN_TOKEN_PROTOCOL_H
