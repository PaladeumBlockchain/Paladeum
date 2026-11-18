// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tokens/tokens.h>

#include <test/test_paladeum.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <script/standard.h>
#include <base58.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <validation.h>

BOOST_FIXTURE_TEST_SUITE(token_p2sh_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(token_is_p2sh_test)
    {
        BOOST_TEST_MESSAGE("Running token_is_p2sh_test");

        SelectParams(CBaseChainParams::MAIN);

        // A 2 of 3 multisig redeem script (createmultisig of three keys). The destination is
        // built from it.
        const std::vector<unsigned char> raw = ParseHex("522103ed288afb520cc40f885c8957638e13a2fd2c94ddd1f59ab7b9594de2b9dfcd9c21022d5fcf6afa30023b394526e11f4e45b43c4f36a429270170cc25d7bf12916eb32103ce679ae00fc1541cc4e6be5bd8fbe116fca24885284daacc3338fd36f87996c853ae");
        const CScript redeemScript(raw.begin(), raw.end());
        CTxDestination dest = CScriptID(redeemScript);
        BOOST_CHECK(IsValidDestination(DecodeDestination(EncodeDestination(dest))));

        // Create a transfer token script to a P2SH address
        CTokenTransfer token("P2SHTEST", 1000, 0);
        CScript scriptPubKey = GetScriptForDestination(dest);
        token.ConstructTransaction(scriptPubKey);

        BOOST_CHECK(scriptPubKey.IsTokenScript());
        BOOST_CHECK_MESSAGE(scriptPubKey.IsP2SHTokenScript(), "IsP2SHTokenScript failed to find P2SH token");
    }

    BOOST_AUTO_TEST_CASE(token_is_not_p2sh_test)
    {
        BOOST_TEST_MESSAGE("Running token_is_not_p2sh_test");

        SelectParams(CBaseChainParams::MAIN);

        // Create a transfer token script to a non P2SH address (pay to key hash)
        CTxDestination dest = CKeyID(uint160(ParseHex("0102030405060708090a0b0c0d0e0f1011121314")));
        CTokenTransfer token("NOTP2SHTEST", 1000, 0);
        CScript scriptPubKey = GetScriptForDestination(dest);
        token.ConstructTransaction(scriptPubKey);

        BOOST_CHECK(scriptPubKey.IsTokenScript());
        BOOST_CHECK_MESSAGE(!scriptPubKey.IsP2SHTokenScript(), "IsP2SHTokenScript should have failed to find a P2SH token");
    }

BOOST_AUTO_TEST_SUITE_END()
