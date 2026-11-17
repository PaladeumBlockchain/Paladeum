/**
 * Created by ROSHii on 2019-06-01.
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include "wallet/bip39.h"
#include "wallet/bip39_english.h"
#include "crypto/sha256.h"
#include "random.h"

#include <openssl/evp.h>

#include <vector>

SecureString CMnemonic::Generate(int strength)
{
    if (strength % 32 || strength < 128 || strength > 256) {
        return SecureString();
    }
    SecureVector data(32);
    GetStrongRandBytes(&data[0], 32);
    SecureString mnemonic = FromData(data, strength / 8);
    return mnemonic;
}

// SecureString CMnemonic::FromData(const uint8_t *data, int len)
SecureString CMnemonic::FromData(const SecureVector& data, int len)
{
    if (len % 4 || len < 16 || len > 32) {
        return SecureString();
    }

    SecureVector checksum(32);
    CSHA256().Write(&data[0], len).Finalize(&checksum[0]);

    // data
    SecureVector bits(len);
    memcpy(&bits[0], &data[0], len);
    // checksum
    bits.push_back(checksum[0]);

    int mlen = len * 3 / 4;
    SecureString mnemonic;

    int i, j, idx;
    for (i = 0; i < mlen; i++) {
        idx = 0;
        for (j = 0; j < 11; j++) {
            idx <<= 1;
            idx += (bits[(i * 11 + j) / 8] & (1 << (7 - ((i * 11 + j) % 8)))) > 0;
        }
        mnemonic.append(wordlist[idx]);
        if (i < mlen - 1) {
            mnemonic += ' ';
        }
    }

    return mnemonic;
}

// The index of a word of the list, -1 if it is not in it.
static int WordIndex(const SecureString& word)
{
    for (int i = 0; wordlist[i]; i++) {
        if (word == wordlist[i])
            return i;
    }
    return -1;
}

// Whether the indexes of the words (11 bits each) carry their checksum: the first
// (number of words / 3) bits of SHA256 of the entropy.
static bool ChecksumMatches(const std::vector<int>& indexes)
{
    const size_t nWordCount = indexes.size();
    const size_t nEntropyBytes = nWordCount * 4 / 3;
    SecureVector bits(33, 0);
    size_t nBit = 0;
    for (int index : indexes) {
        for (int k = 10; k >= 0; k--, nBit++) {
            if (index & (1 << k))
                bits[nBit / 8] |= 1 << (7 - (nBit % 8));
        }
    }
    const unsigned char checksum = bits[nEntropyBytes];
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(&bits[0], nEntropyBytes).Finalize(hash);
    const unsigned char mask = static_cast<unsigned char>(0xFF << (8 - nWordCount / 3));
    return (hash[0] & mask) == (checksum & mask);
}

bool CMnemonic::Check(SecureString mnemonic)
{
    if (mnemonic.empty()) {
        return false;
    }

    // the words, separated by single spaces
    std::vector<SecureString> words(1);
    for (char c : mnemonic) {
        if (c == ' ')
            words.emplace_back();
        else
            words.back() += c;
    }
    const size_t nWordCount = words.size();
    if (nWordCount != 12 && nWordCount != 18 && nWordCount != 24) {
        return false;
    }

    // The words of Paladeum 0.1.x, whose list had "token" in place of "asset" (109) and
    // "paladeum" in place of "raven" (1427): "paladeum" is word 1427, and a "token" is
    // word 1823 or word 109, whichever makes the checksum match.
    static const int RAVEN = 1427, ASSET = 109;
    std::vector<int> indexes;
    std::vector<size_t> tokenPositions;
    for (size_t i = 0; i < nWordCount; i++) {
        int index = WordIndex(words[i]);
        if (index < 0 && words[i] == "paladeum")
            index = RAVEN;
        if (index < 0)
            return false;
        if (words[i] == "token")
            tokenPositions.push_back(i);
        indexes.push_back(index);
    }
    if (tokenPositions.size() > 8) // 2^8 combinations at most
        return false;
    for (unsigned int choice = 0; choice < (1u << tokenPositions.size()); choice++) {
        std::vector<int> candidate = indexes;
        for (size_t t = 0; t < tokenPositions.size(); t++) {
            if (choice & (1u << t))
                candidate[tokenPositions[t]] = ASSET;
        }
        if (ChecksumMatches(candidate))
            return true;
    }
    return false;
}

void CMnemonic::ToSeed(SecureString mnemonic, SecureString passphrase, SecureVector& seedRet)
{
    SecureString ssSalt = SecureString("mnemonic") + passphrase;
    SecureVector vchSalt(ssSalt.begin(), ssSalt.end());
    seedRet.resize(64);
    PKCS5_PBKDF2_HMAC(mnemonic.c_str(), mnemonic.size(), &vchSalt[0], vchSalt.size(), 2048, EVP_sha512(), 64, &seedRet[0]);
}