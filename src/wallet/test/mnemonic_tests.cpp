//
// Created by ROSHii on 2019-06-01.
//
// Copyright (c) 2026 The Paladeum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CMnemonic (wallet/bip39.h): the 12 words that the wallet makes on its first run.

#include <algorithm>

#include "base58.h"
#include "bip39_wordlists.h"
#include "data/bip39_vectors.json.h"
#include "key.h"
#include "utilstrencodings.h"
#include "test/test_paladeum.h"
#include "wallet/bip39.h"
#include "wallet/bip39_english.h"

#include <boost/test/unit_test.hpp>

#include <univalue.h>

// In script_tests.cpp
extern UniValue read_json(const std::string& jsondata);

BOOST_FIXTURE_TEST_SUITE(mnemonic_tests, BasicTestingSetup)

// The English vectors of the BIP39 reference implementation (passphrase "TREZOR"):
// https://github.com/trezor/python-mnemonic/blob/b502451a33a440783926e04428115e0bed87d01f/vectors.json
// Each entry: entropy, mnemonic, seed, BIP32 root key (xprv).
BOOST_AUTO_TEST_CASE(mnemonic_vectors)
{
    UniValue tests = read_json(std::string(json_tests::bip39_vectors, json_tests::bip39_vectors + sizeof(json_tests::bip39_vectors)));
    BOOST_CHECK_EQUAL(tests.size(), 24U);

    for (unsigned int i = 0; i < tests.size(); i++) {
        UniValue test = tests[i];
        std::string strTest = test.write();
        if (test.size() < 4) {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }

        std::vector<uint8_t> vData = ParseHex(test[0].get_str());
        SecureVector data(vData.begin(), vData.end());

        SecureString m = CMnemonic::FromData(data, data.size());
        std::string strMnemonic = test[1].get_str();
        SecureString mnemonic(strMnemonic.begin(), strMnemonic.end());

        BOOST_CHECK_MESSAGE(m == mnemonic, strTest);
        BOOST_CHECK(CMnemonic::Check(mnemonic));

        SecureVector seed;
        SecureString passphrase("TREZOR");
        CMnemonic::ToSeed(mnemonic, passphrase, seed);
        BOOST_CHECK_EQUAL(HexStr(seed), test[2].get_str());

        // The root key. The vectors give it with Bitcoin's prefix (xprv); a Paladeum key has the
        // prefix of the network (tprv on the main network).
        CExtKey key;
        key.SetSeed(&seed[0], 64);
        CPaladeumExtKey b58key;
        b58key.SetKey(key);
        const std::string expected = ExtKeyWithNetworkPrefix(test[3].get_str(), CChainParams::EXT_SECRET_KEY);
        BOOST_CHECK(!expected.empty());
        BOOST_CHECK_EQUAL(b58key.ToString(), expected);
    }
}

// A changed word or a word that is not in the list fails the check.
BOOST_AUTO_TEST_CASE(mnemonic_check)
{
    BOOST_CHECK(CMnemonic::Check(SecureString("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about")));
    BOOST_CHECK(!CMnemonic::Check(SecureString("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon")));
    BOOST_CHECK(!CMnemonic::Check(SecureString("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abuot")));
    BOOST_CHECK(!CMnemonic::Check(SecureString("abandon abandon abandon")));
}

// The word list of CMnemonic (wallet/bip39_english.h) is the English list of BIP39, the one of
// bip39.cpp (bip39_wordlists.h, generated from the reference list).
BOOST_AUTO_TEST_CASE(mnemonic_wordlist)
{
    size_t count = 0;
    while (wordlist[count])
        count++;
    BOOST_CHECK_EQUAL(count, 2048U);
    for (size_t i = 0; i < count && i < 2048; i++)
        BOOST_CHECK_MESSAGE(std::string(wordlist[i]) == bip39_wordlist_english[i],
                            strprintf("word %u is \"%s\", BIP39 has \"%s\"", i, wordlist[i], bip39_wordlist_english[i]));
}

// Mnemonics with the words "asset" (109), "raven" (1427) and "token" (1823) of the BIP39 list:
// made from their entropy, and checked.
BOOST_AUTO_TEST_CASE(mnemonic_asset_raven_token)
{
    static const char* const vectors[][2] = {
        {"0da00000000000000000000000000000", "asset abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"},
        {"b2600000000000000000000000000000", "raven abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"},
        {"e3e00000000000000000000000000000", "token abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"},
    };
    for (const auto& vector : vectors) {
        std::vector<uint8_t> vData = ParseHex(vector[0]);
        SecureVector data(vData.begin(), vData.end());
        SecureString m = CMnemonic::FromData(data, data.size());
        BOOST_CHECK_EQUAL(std::string(m.begin(), m.end()), vector[1]);
        BOOST_CHECK_MESSAGE(CMnemonic::Check(SecureString(vector[1])), vector[1]);
    }
}

// Paladeum 0.1.x made its phrases with a list that had "token" for "asset" (109) and
// "paladeum" for "raven" (1427). Those phrases are still accepted, so that such a wallet can
// be restored; the seed comes from the words as written, so the keys are the same as before.
BOOST_AUTO_TEST_CASE(mnemonic_old_paladeum_words)
{
    // "asset ... absurd" and "raven ... absurd" written with the old words
    BOOST_CHECK(CMnemonic::Check(SecureString("token abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd")));
    BOOST_CHECK(CMnemonic::Check(SecureString("paladeum abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd")));
    // the real "token" (1823) still works, and so do both meanings in one phrase
    BOOST_CHECK(CMnemonic::Check(SecureString("token abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd")));
    // a wrong last word fails with the old words as well
    BOOST_CHECK(!CMnemonic::Check(SecureString("paladeum abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon")));
    BOOST_CHECK(!CMnemonic::Check(SecureString("paladeum paladeum")));
    // the seed is that of the words as written
    SecureVector seedOld, seedOld2;
    CMnemonic::ToSeed(SecureString("paladeum abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"), SecureString(""), seedOld);
    CMnemonic::ToSeed(SecureString("paladeum abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"), SecureString(""), seedOld2);
    BOOST_CHECK(seedOld == seedOld2);
    SecureVector seedStandard;
    CMnemonic::ToSeed(SecureString("raven abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon absurd"), SecureString(""), seedStandard);
    BOOST_CHECK(seedOld != seedStandard);
}

// Generate() gives 12 words for 128 bits, and they pass the check.
BOOST_AUTO_TEST_CASE(mnemonic_generate)
{
    for (int i = 0; i < 8; i++) {
        SecureString words = CMnemonic::Generate(128);
        BOOST_CHECK_MESSAGE(CMnemonic::Check(words), std::string(words.begin(), words.end()));
        BOOST_CHECK_EQUAL(std::count(words.begin(), words.end(), ' '), 11);
    }
}

BOOST_AUTO_TEST_SUITE_END()
