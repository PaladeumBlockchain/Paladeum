// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <tokens/tokens.h>
#include <test/test_paladeum.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(null_token_data_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(null_data_from_script_test)
    {
        BOOST_TEST_MESSAGE("Running Null data from script");

        // Create the correct script
        CScript nullDataScript = GetScriptForNullTokenDataDestination(DecodeDestination(GetParams().GlobalFeeAddress()));

        CNullTokenTxData nullData("#ADDTAG", (int)QualifierType::ADD_QUALIFIER);
        nullData.ConstructTransaction(nullDataScript);

        CNullTokenTxData fetchedData;
        std::string fetchedAddress;

        BOOST_CHECK_MESSAGE(TokenNullDataFromScript(nullDataScript, fetchedData, fetchedAddress), "Null Data From Script Test 1: Failed to get NullDataFromScript");

    }

    BOOST_AUTO_TEST_CASE(null_data_from_script_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Null data from script failure");

        // Create an invalid script
        CScript nullDataScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalFeeAddress()));

        CNullTokenTxData fetchedData;
        std::string fetchedAddress;
        BOOST_CHECK_MESSAGE(!TokenNullDataFromScript(nullDataScript, fetchedData, fetchedAddress), "Null Data Failure Test 1: should have failed");
    }

    BOOST_AUTO_TEST_CASE(global_null_data_from_script_test)
    {
        BOOST_TEST_MESSAGE("Running Global Null data from script");

        // Create the correct script
        CScript nullGlobalDataScript;

        CNullTokenTxData nullGlobalData("$ADDRESTRICTION", (int)RestrictedType::GLOBAL_FREEZE);
        nullGlobalData.ConstructGlobalRestrictionTransaction(nullGlobalDataScript);

        CNullTokenTxData fetchedData;
        BOOST_CHECK_MESSAGE(GlobalTokenNullDataFromScript(nullGlobalDataScript, fetchedData), "Null Global Data From Script Test 1: Failed to get NullGlobalDataFromScript");
    }

    BOOST_AUTO_TEST_CASE(global_null_data_from_script_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Global Null data from script");

        // Create the correct script
        CScript nullGlobalDataScript;

        CNullTokenTxData nullGlobalData("$ADDRESTRICTION", (int)RestrictedType::GLOBAL_FREEZE);

        // Construct the wrong type of script
        nullGlobalData.ConstructTransaction(nullGlobalDataScript);

        CNullTokenTxData fetchedData;

        BOOST_CHECK_MESSAGE(!GlobalTokenNullDataFromScript(nullGlobalDataScript, fetchedData), "Null Global Data From Script Failure Test 1: should have failed");
    }


BOOST_AUTO_TEST_SUITE_END()
