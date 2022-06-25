// Copyright (c) 2021-2022 The Akila developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <tokens/tokens.h>
#include <test/test_akila.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(unique_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(unique_from_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Unique From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newUniqueScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));

        CNewToken unique_token("ROOT#UNIQUE1", 1 , 0 , 0, 0, "");
        unique_token.ConstructTransaction(newUniqueScript);

        CTxOut out(0, newUniqueScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(UniqueTokenFromTransaction(tx, fetched_token,address), "Failed to get qualifier from transaction");
        BOOST_CHECK_MESSAGE(fetched_token.strName == unique_token.strName, "Unique Tests: Failed token names check");
        BOOST_CHECK_MESSAGE(fetched_token.nAmount== unique_token.nAmount, "Unique Tests: Failed amount check");
        BOOST_CHECK_MESSAGE(address == GetParams().GlobalFeeAddress(), "Unique Tests: Failed address check");
        BOOST_CHECK_MESSAGE(fetched_token.nReissuable == unique_token.nReissuable, "Unique Tests: Failed reissuable check");
    }

    BOOST_AUTO_TEST_CASE(unique_from_transaction_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Unique From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newUniqueScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));

        CNewToken unique_token("$NOT_UNIQUE", 1 , 0 , 0, 0, "");
        unique_token.ConstructTransaction(newUniqueScript);

        CTxOut out(0, newUniqueScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(!UniqueTokenFromTransaction(tx, fetched_token,address), "should have failed to get UniqueTokenFromTransaction");
    }


BOOST_AUTO_TEST_SUITE_END()
