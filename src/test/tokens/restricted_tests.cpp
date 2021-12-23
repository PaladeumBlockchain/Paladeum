// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <tokens/tokens.h>
#include <test/test_yona.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(restricted_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(restricted_from_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Restricted From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newRestrictedScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewToken restricted_token("$RESTRICTED_NAME", 5);
        restricted_token.ConstructTransaction(newRestrictedScript);

        CTxOut out(0, newRestrictedScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(RestrictedTokenFromTransaction(tx, fetched_token, address), "Failed to get restricted from transaction");
        BOOST_CHECK_MESSAGE(fetched_token.strName == restricted_token.strName, "Restricted Tests: Failed token names check");
        BOOST_CHECK_MESSAGE(fetched_token.nAmount == restricted_token.nAmount, "Restricted Tests: Failed amount check");
        BOOST_CHECK_MESSAGE(address == GetParams().GlobalBurnAddress(), "Restricted Tests: Failed address check");
    }

    BOOST_AUTO_TEST_CASE(restricted_from_transaction_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Restricted From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newRestrictedScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewToken restricted_token("NOT_RESTRICTED_NAME", 5);
        restricted_token.ConstructTransaction(newRestrictedScript);

        CTxOut out(0, newRestrictedScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(!RestrictedTokenFromTransaction(tx, fetched_token, address), "should have failed to RestrictedTokenFromTransaction");
    }

    BOOST_AUTO_TEST_CASE(verify_new_restricted_transaction_test) {
        BOOST_TEST_MESSAGE("Running Verify New Restricted transaction");

        /// Create CTxOut to use in the tests ///
        // Create filler yona tx
        CScript yonaTransfer = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CTxOut yonaOut(1*COIN, yonaTransfer);

        // Create transaction and add burn to it
        CScript burnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(KnownTokenType::RESTRICTED)));
        CTxOut burnOut(GetBurnAmount(KnownTokenType::RESTRICTED), burnScript);

        // Add the parent transaction for sub qualifier tx
        CTokenTransfer parentTransfer("RESTRICTED_NAME!", OWNER_TOKEN_AMOUNT);
        CScript parentScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        parentTransfer.ConstructTransaction(parentScript);
        CTxOut parentOut(0, parentScript);

        // Add the CNullVerifierString tx
        CScript verifierScript;
        CNullTokenTxVerifierString verifierStringData("true");
        verifierStringData.ConstructTransaction(verifierScript);
        CTxOut verifierOut(0, verifierScript);

        // Create the new restricted Script
        CScript newRestrictedScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CNewToken restricted_token("$RESTRICTED_NAME", 5 * COIN, 0, 0, 0, "");
        restricted_token.ConstructTransaction(newRestrictedScript);
        CTxOut tokenOut(0, newRestrictedScript);

        // Create a fake owner script
        CScript newRestrictedTokenOwnerScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        restricted_token.ConstructOwnerTransaction(newRestrictedTokenOwnerScript);
        CTxOut ownerOut(0, newRestrictedTokenOwnerScript);
        /// Finish Creating CTxOut to use in the tests ///

        // Run successful test 1
        std::string error;
        CMutableTransaction mutTxTest1;
        mutTxTest1.vout.emplace_back(yonaOut);
        mutTxTest1.vout.emplace_back(burnOut);
        mutTxTest1.vout.emplace_back(parentOut);
        mutTxTest1.vout.emplace_back(verifierOut);
        mutTxTest1.vout.emplace_back(tokenOut);
        CTransaction txTest1(mutTxTest1);
        BOOST_CHECK_MESSAGE(txTest1.VerifyNewRestrictedToken(error), "Test 1: Failed to Verify New Restricted Token" + error);

        // Run failure test 2
        CMutableTransaction mutTxTest2;
        mutTxTest2.vout.emplace_back(yonaOut);
        mutTxTest2.vout.emplace_back(burnOut);
        mutTxTest2.vout.emplace_back(verifierOut);
        mutTxTest2.vout.emplace_back(tokenOut);
        CTransaction txTest2(mutTxTest2);
        BOOST_CHECK_MESSAGE(!txTest2.VerifyNewRestrictedToken(error), "Test 2: should have failed missing parent tx");
        BOOST_CHECK(error == "bad-txns-issue-restricted-root-owner-token-outpoint-not-found");

        // Run failure test 3
        CMutableTransaction mutTxTest3;
        mutTxTest3.vout.emplace_back(yonaOut);
        mutTxTest3.vout.emplace_back(burnOut);
        mutTxTest3.vout.emplace_back(parentOut);
        mutTxTest3.vout.emplace_back(tokenOut);
        CTransaction txTest3(mutTxTest3);
        BOOST_CHECK_MESSAGE(!txTest3.VerifyNewRestrictedToken(error), "Test 3: should have failed missing verifier tx");
        BOOST_CHECK(error == "Verifier string not found");

        // Run failure test 4
        CMutableTransaction mutTxTest4;
        mutTxTest4.vout.emplace_back(yonaOut);
        mutTxTest4.vout.emplace_back(parentOut);
        mutTxTest4.vout.emplace_back(verifierOut);
        mutTxTest4.vout.emplace_back(tokenOut);
        CTransaction txTest4(mutTxTest4);
        BOOST_CHECK_MESSAGE(!txTest4.VerifyNewRestrictedToken(error),"Test 4: should have failed missing burn tx");
        BOOST_CHECK(error == "bad-txns-issue-restricted-burn-not-found");

        // Run failure test 5
        CMutableTransaction mutTxTest5;
        mutTxTest5.vout.emplace_back(yonaOut);
        mutTxTest5.vout.emplace_back(burnOut);
        mutTxTest5.vout.emplace_back(parentOut);
        mutTxTest5.vout.emplace_back(verifierOut);
        CTransaction txTest5(mutTxTest5);
        BOOST_CHECK_MESSAGE(!txTest5.VerifyNewRestrictedToken(error),"Test 5: should have failed missing token tx");
        BOOST_CHECK(error == "bad-txns-issue-restricted-data-not-found");

        // Run failure test 6
        CMutableTransaction mutTxTest6;
        mutTxTest6.vout.emplace_back(yonaOut);
        mutTxTest6.vout.emplace_back(burnOut);
        mutTxTest6.vout.emplace_back(parentOut);
        mutTxTest6.vout.emplace_back(verifierOut);
        mutTxTest6.vout.emplace_back(tokenOut);
        mutTxTest6.vout.emplace_back(tokenOut);
        CTransaction txTest6(mutTxTest6);
        BOOST_CHECK_MESSAGE(!txTest6.VerifyNewRestrictedToken(error),"Test 6: should have failed multiple issues in same tx");
        BOOST_CHECK(error == "bad-txns-failed-issue-token-formatting-check");

        // Run failure test 7
        CMutableTransaction mutTxTest7;
        mutTxTest7.vout.emplace_back(yonaOut);
        mutTxTest7.vout.emplace_back(burnOut);
        mutTxTest7.vout.emplace_back(parentOut);
        mutTxTest7.vout.emplace_back(verifierOut);
        mutTxTest7.vout.emplace_back(ownerOut);
        mutTxTest7.vout.emplace_back(tokenOut);
        CTransaction txTest7(mutTxTest7);
        BOOST_CHECK_MESSAGE(!txTest7.VerifyNewRestrictedToken(error),"Test 7: should have failed tried to create owner token for restricted token");
        BOOST_CHECK(error == "bad-txns-failed-issue-token-formatting-check");

        // Run failure test 8
        CMutableTransaction mutTxTest8;
        mutTxTest8.vout.emplace_back(yonaOut);
        mutTxTest8.vout.emplace_back(burnOut);
        mutTxTest8.vout.emplace_back(parentOut);
        mutTxTest8.vout.emplace_back(verifierOut);
        mutTxTest8.vout.emplace_back(verifierOut);
        mutTxTest8.vout.emplace_back(tokenOut);
        CTransaction txTest8(mutTxTest8);
        BOOST_CHECK_MESSAGE(!txTest8.VerifyNewRestrictedToken(error),"Test 8: should have failed multiple verifier tx");
        BOOST_CHECK(error == "Multiple verifier strings found in transaction");
    }


BOOST_AUTO_TEST_SUITE_END()
