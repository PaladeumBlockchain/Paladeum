// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <tokens/tokens.h>
#include <test/test_yona.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(qualifier_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(qualifier_from_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Qualifier From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewToken qualifier_token("#QUALIFIER_NAME", 5 * COIN);
        qualifier_token.ConstructTransaction(newQualifierScript);

        CTxOut out(0, newQualifierScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(QualifierTokenFromTransaction(tx, fetched_token,address), "Failed to get qualifier from transaction");
        BOOST_CHECK_MESSAGE(fetched_token.strName == qualifier_token.strName, "Qualifier Tests: Failed token names check");
        BOOST_CHECK_MESSAGE(fetched_token.nAmount== qualifier_token.nAmount, "Qualifier Tests: Failed amount check");
        BOOST_CHECK_MESSAGE(address == GetParams().GlobalBurnAddress(), "Qualifier Tests: Failed address check");
    }

    BOOST_AUTO_TEST_CASE(qualifier_from_transaction__fail_test)
    {
        BOOST_TEST_MESSAGE("Running Qualifier From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewToken qualifier_token("NOT_QUALIFIER_NAME", 5 * COIN);
        qualifier_token.ConstructTransaction(newQualifierScript);

        CTxOut out(0, newQualifierScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewToken fetched_token;
        BOOST_CHECK_MESSAGE(!QualifierTokenFromTransaction(tx, fetched_token,address), "should have failed to get QualifierTokenFromTransaction");
    }


    BOOST_AUTO_TEST_CASE(verify_new_qualifier_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Verify New Qualifier From Transaction Test");

        // Create transaction and add burn to it
        CMutableTransaction mutableTransaction;
        CScript burnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(TokenType::QUALIFIER)));
        CTxOut burnOut(GetBurnAmount(TokenType::QUALIFIER), burnScript);
        mutableTransaction.vout.push_back(burnOut);

        // Create the new Qualifier Script
        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CNewToken qualifier_token("#QUALIFIER_NAME", 5 * COIN, 0, 0, 0, "");
        qualifier_token.ConstructTransaction(newQualifierScript);
        CTxOut tokenOut(0, newQualifierScript);
        mutableTransaction.vout.push_back(tokenOut);

        CTransaction tx(mutableTransaction);

        std::string error;
        BOOST_CHECK_MESSAGE(tx.VerifyNewQualfierToken(error), "Failed to Verify New Qualifier Token" + error);
    }

    BOOST_AUTO_TEST_CASE(verify_new_sub_qualifier_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Verify New Sub Qualifier From Transaction Test");

        // Create transaction and add burn to it
        CMutableTransaction mutableTransaction;
        CScript burnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(TokenType::SUB_QUALIFIER)));
        CTxOut burnOut(GetBurnAmount(TokenType::SUB_QUALIFIER), burnScript);
        mutableTransaction.vout.push_back(burnOut);

        // Add the parent transaction for sub qualifier tx
        CTokenTransfer parentTransfer("#QUALIFIER_NAME", OWNER_TOKEN_AMOUNT);
        CScript parentScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        parentTransfer.ConstructTransaction(parentScript);
        CTxOut parentOut(0, parentScript);
        mutableTransaction.vout.push_back(parentOut);

        // Create the new Qualifier Script
        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CNewToken qualifier_token("#QUALIFIER_NAME/#SUB1", 5 * COIN, 0, 0, 0, "");
        qualifier_token.ConstructTransaction(newQualifierScript);
        CTxOut tokenOut(0, newQualifierScript);
        mutableTransaction.vout.push_back(tokenOut);

        CTransaction tx(mutableTransaction);

        std::string strError = "";

        tx.VerifyNewQualfierToken(strError);
        BOOST_CHECK_MESSAGE(tx.VerifyNewQualfierToken(strError), "Failed to Verify New Sub Qualifier Token " + strError);
    }




BOOST_AUTO_TEST_SUITE_END()
