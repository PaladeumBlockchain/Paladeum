// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Why base-58 instead of standard base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking data.
 * - A string with non-alphanumeric characters is not as easily accepted as input.
 * - E-mail usually won't line-break if there's no punctuation to break at.
 * - Double-clicking selects the whole string as one word if it's all alphanumeric.
 */
#ifndef PLB_BASE58_H
#define PLB_BASE58_H

#include "chainparams.h"
#include "key.h"
#include "pubkey.h"
#include "script/standard.h"
#include "support/allocators/zeroafterfree.h"

#include <string>
#include <vector>

/**
 * Encode a byte sequence as a base58-encoded string.
 * pbegin and pend cannot be nullptr, unless both are.
 */
std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend);

/**
 * Encode a byte vector as a base58-encoded string
 */
std::string EncodeBase58(const std::vector<unsigned char>& vch);

/**
 * Decode a base58-encoded string (psz) into a byte vector (vchRet).
 * return true if decoding is successful.
 * psz cannot be nullptr.
 */
bool DecodeBase58(const char* psz, std::vector<unsigned char>& vchRet);

/**
 * Decode a base58-encoded string (str) into a byte vector (vchRet).
 * return true if decoding is successful.
 */
bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet);

/**
 * Encode a byte vector into a base58-encoded string, including checksum
 */
std::string EncodeBase58Check(const std::vector<unsigned char>& vchIn);

/**
 * Decode a base58-encoded string (psz) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet);

/**
 * Decode a base58-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet);

/**
 * Base class for all base58-encoded data
 */
class CBase58Data
{
protected:
    //! the version byte(s)
    std::vector<unsigned char> vchVersion;

    //! the actually encoded data
    typedef std::vector<unsigned char, zero_after_free_allocator<unsigned char> > vector_uchar;
    vector_uchar vchData;

    CBase58Data();
    void SetData(const std::vector<unsigned char> &vchVersionIn, const void* pdata, size_t nSize);
    void SetData(const std::vector<unsigned char> &vchVersionIn, const void* pdata, size_t nSize, const void* pdata2, size_t nSize2);
    void SetData(const std::vector<unsigned char> &vchVersionIn, const unsigned char *pbegin, const unsigned char *pend);

public:
    bool SetString(const char* psz, unsigned int nVersionBytes = 1);
    bool SetString(const std::string& str);
    std::string ToString() const;
    int CompareTo(const CBase58Data& b58) const;

    bool operator==(const CBase58Data& b58) const { return CompareTo(b58) == 0; }
    bool operator<=(const CBase58Data& b58) const { return CompareTo(b58) <= 0; }
    bool operator>=(const CBase58Data& b58) const { return CompareTo(b58) >= 0; }
    bool operator< (const CBase58Data& b58) const { return CompareTo(b58) <  0; }
    bool operator> (const CBase58Data& b58) const { return CompareTo(b58) >  0; }
};
/** base58-encoded Paladeum addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CPaladeumAddress : public CBase58Data {
public:
    bool Set(const CKeyID &id);
    bool Set(const CKeyID &id, const CKeyID &id2);
    bool Set(const CScriptID &id);
    bool Set(const CTxDestination &dest);
    bool IsValid() const;
    bool IsValid(const CChainParams &params) const;
    bool IsOfflineStakingAddress(const CChainParams& params) const;

    CPaladeumAddress() {}
    CPaladeumAddress(const CTxDestination &dest) { Set(dest); }
    CPaladeumAddress(const CKeyID &id, const CKeyID &id2) { Set(id, id2); }
    CPaladeumAddress(const std::string& strAddress) { SetString(strAddress); }
    CPaladeumAddress(const char* pszAddress) { SetString(pszAddress); }

    CTxDestination Get() const;
    bool GetKeyID(CKeyID &keyID) const;
    bool GetStakingKeyID(CKeyID &keyID) const;
    bool GetSpendingKeyID(CKeyID &keyID) const;
    bool GetIndexKey(uint160& hashBytes, int& type) const;

    bool GetStakingAddress(CPaladeumAddress &address) const;
    bool GetSpendingAddress(CPaladeumAddress &address) const;
};

/**
 * A base58-encoded secret key
 */
class CPaladeumSecret : public CBase58Data
{
public:
    void SetKey(const CKey& vchSecret);
    CKey GetKey();
    bool IsValid() const;
    bool SetString(const char* pszSecret);
    bool SetString(const std::string& strSecret);

    CPaladeumSecret(const CKey& vchSecret) { SetKey(vchSecret); }
    CPaladeumSecret() {}
};

template<typename K, int Size, CChainParams::Base58Type Type> class CPaladeumExtKeyBase : public CBase58Data
{
public:
    void SetKey(const K &key) {
        unsigned char vch[Size];
        key.Encode(vch);
        SetData(GetParams().Base58Prefix(Type), vch, vch + Size);
    }

    K GetKey() {
        K ret;
        if (vchData.size() == Size) {
            // If base58 encoded data does not hold an ext key, return a !IsValid() key
            ret.Decode(vchData.data());
        }
        return ret;
    }

    CPaladeumExtKeyBase(const K &key) {
        SetKey(key);
    }

    CPaladeumExtKeyBase(const std::string& strBase58c) {
        SetString(strBase58c.c_str(), GetParams().Base58Prefix(Type).size());
    }

    CPaladeumExtKeyBase() {}
};

typedef CPaladeumExtKeyBase<CExtKey, BIP32_EXTKEY_SIZE, CChainParams::EXT_SECRET_KEY> CPaladeumExtKey;
typedef CPaladeumExtKeyBase<CExtPubKey, BIP32_EXTKEY_SIZE, CChainParams::EXT_PUBLIC_KEY> CPaladeumExtPubKey;

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

#endif // PLB_BASE58_H
