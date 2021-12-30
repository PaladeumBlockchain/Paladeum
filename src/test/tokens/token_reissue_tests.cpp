// Copyright (c) 2021-2022 The Yona developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <tokens/tokens.h>

#include <test/test_yona.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <script/standard.h>
#include <base58.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <validation.h>
BOOST_FIXTURE_TEST_SUITE(token_reissue_tests, BasicTestingSetup)


    BOOST_AUTO_TEST_CASE(reissue_cache_test_ipfs)
    {
        BOOST_TEST_MESSAGE("Running Reissue Cache Test");

        SelectParams(CBaseChainParams::MAIN);

        fTokenIndex = true; // We only cache if fTokenIndex is true
        ptokens = new CTokensCache();
        // Create tokens cache
        CTokensCache cache;

        CNewToken token1("YONATOKEN", CAmount(100 * COIN), 8, 1, 0, "");

        // Add an token to a valid yona address
        uint256 hash = uint256();
        BOOST_CHECK_MESSAGE(cache.AddNewToken(token1, GetParams().GlobalBurnAddress(), 0, hash), "Failed to add new token");

        // Create a reissuance of the token
        CReissueToken reissue1("YONATOKEN", CAmount(1 * COIN), 8, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));
        COutPoint out(uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A4"), 1);

        // Add an reissuance of the token to the cache
        BOOST_CHECK_MESSAGE(cache.AddReissueToken(reissue1, GetParams().GlobalBurnAddress(), out), "Failed to add reissue");

        // Check to see if the reissue changed the cache data correctly
        BOOST_CHECK_MESSAGE(cache.mapReissuedTokenData.count("YONATOKEN"), "Map Reissued Token should contain the token \"YONATOKEN\"");
        BOOST_CHECK_MESSAGE(cache.mapTokensAddressAmount.at(make_pair("YONATOKEN", GetParams().GlobalBurnAddress())) == CAmount(101 * COIN), "Reissued amount wasn't added to the previous total");

        // Get the new token data from the cache
        CNewToken token2;
        BOOST_CHECK_MESSAGE(cache.GetTokenMetaDataIfExists("YONATOKEN", token2), "Failed to get the token2");

        // Chech the token metadata
        BOOST_CHECK_MESSAGE(token2.nReissuable == 1, "Token2: Reissuable isn't 1");
        BOOST_CHECK_MESSAGE(token2.nAmount == CAmount(101 * COIN), "Token2: Amount isn't 101");
        BOOST_CHECK_MESSAGE(token2.strName == "YONATOKEN", "Token2: Token name is wrong");
        BOOST_CHECK_MESSAGE(token2.units == 8, "Token2: Units is wrong");
        BOOST_CHECK_MESSAGE(EncodeTokenData(token2.strIPFSHash) == "QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo", "Token2: IPFS hash is wrong");

        // Remove the reissue from the cache
        std::vector<std::pair<std::string, CBlockTokenUndo> > undoBlockData;
        undoBlockData.emplace_back(std::make_pair("YONATOKEN", CBlockTokenUndo{true, false, "", 0, TOKEN_UNDO_INCLUDES_VERIFIER_STRING, false, ""}));
        BOOST_CHECK_MESSAGE(cache.RemoveReissueToken(reissue1, GetParams().GlobalBurnAddress(), out, undoBlockData), "Failed to remove reissue");

        // Get the token data from the cache now that the reissuance was removed
        CNewToken token3;
        BOOST_CHECK_MESSAGE(cache.GetTokenMetaDataIfExists("YONATOKEN", token3), "Failed to get the token3");

        // Chech the token3 metadata and make sure all the changed from the reissue were removed
        BOOST_CHECK_MESSAGE(token3.nReissuable == 1, "Token3: Reissuable isn't 1");
        BOOST_CHECK_MESSAGE(token3.nAmount == CAmount(100 * COIN), "Token3: Amount isn't 100");
        BOOST_CHECK_MESSAGE(token3.strName == "YONATOKEN", "Token3: Token name is wrong");
        BOOST_CHECK_MESSAGE(token3.units == 8, "Token3: Units is wrong");
        BOOST_CHECK_MESSAGE(token3.strIPFSHash == "", "Token3: IPFS hash is wrong");

        // Check to see if the reissue removal updated the cache correctly
        BOOST_CHECK_MESSAGE(cache.mapReissuedTokenData.count("YONATOKEN"), "Map of reissued data was removed, even though changes were made and not databased yet");
        BOOST_CHECK_MESSAGE(cache.mapTokensAddressAmount.at(make_pair("YONATOKEN", GetParams().GlobalBurnAddress())) == CAmount(100 * COIN), "Tokens total wasn't undone when reissuance was");
    }

    BOOST_AUTO_TEST_CASE(reissue_cache_test_txid)
    {
        BOOST_TEST_MESSAGE("Running Reissue Cache Test");

        SelectParams(CBaseChainParams::MAIN);

        fTokenIndex = true; // We only cache if fTokenIndex is true
        ptokens = new CTokensCache();
        // Create tokens cache
        CTokensCache cache;

        CNewToken token1("YONATOKEN", CAmount(100 * COIN), 8, 1, 0, "");

        // Add an token to a valid yona address
        uint256 hash = uint256();
        BOOST_CHECK_MESSAGE(cache.AddNewToken(token1, GetParams().GlobalBurnAddress(), 0, hash), "Failed to add new token");

        // Create a reissuance of the token
        CReissueToken reissue1("YONATOKEN", CAmount(1 * COIN), 8, 1, DecodeTokenData("9c2c8e121a0139ba39bffd3ca97267bca9d4c0c1e84ac0c34a883c28e7a912ca"));
        COutPoint out(uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A4"), 1);

        // Add an reissuance of the token to the cache
        BOOST_CHECK_MESSAGE(cache.AddReissueToken(reissue1, GetParams().GlobalBurnAddress(), out), "Failed to add reissue");

        // Check to see if the reissue changed the cache data correctly
        BOOST_CHECK_MESSAGE(cache.mapReissuedTokenData.count("YONATOKEN"), "Map Reissued Token should contain the token \"YONATOKEN\"");
        BOOST_CHECK_MESSAGE(cache.mapTokensAddressAmount.at(make_pair("YONATOKEN", GetParams().GlobalBurnAddress())) == CAmount(101 * COIN), "Reissued amount wasn't added to the previous total");

        // Get the new token data from the cache
        CNewToken token2;
        BOOST_CHECK_MESSAGE(cache.GetTokenMetaDataIfExists("YONATOKEN", token2), "Failed to get the token2");

        // Chech the token metadata
        BOOST_CHECK_MESSAGE(token2.nReissuable == 1, "Token2: Reissuable isn't 1");
        BOOST_CHECK_MESSAGE(token2.nAmount == CAmount(101 * COIN), "Token2: Amount isn't 101");
        BOOST_CHECK_MESSAGE(token2.strName == "YONATOKEN", "Token2: Token name is wrong");
        BOOST_CHECK_MESSAGE(token2.units == 8, "Token2: Units is wrong");
        BOOST_CHECK_MESSAGE(EncodeTokenData(token2.strIPFSHash) == "9c2c8e121a0139ba39bffd3ca97267bca9d4c0c1e84ac0c34a883c28e7a912ca", "Token2: txid hash is wrong");

        // Remove the reissue from the cache
        std::vector<std::pair<std::string, CBlockTokenUndo> > undoBlockData;
        undoBlockData.emplace_back(std::make_pair("YONATOKEN", CBlockTokenUndo{true, false, "", 0, TOKEN_UNDO_INCLUDES_VERIFIER_STRING, false, ""}));
        BOOST_CHECK_MESSAGE(cache.RemoveReissueToken(reissue1, GetParams().GlobalBurnAddress(), out, undoBlockData), "Failed to remove reissue");

        // Get the token data from the cache now that the reissuance was removed
        CNewToken token3;
        BOOST_CHECK_MESSAGE(cache.GetTokenMetaDataIfExists("YONATOKEN", token3), "Failed to get the token3");

        // Chech the token3 metadata and make sure all the changed from the reissue were removed
        BOOST_CHECK_MESSAGE(token3.nReissuable == 1, "Token3: Reissuable isn't 1");
        BOOST_CHECK_MESSAGE(token3.nAmount == CAmount(100 * COIN), "Token3: Amount isn't 100");
        BOOST_CHECK_MESSAGE(token3.strName == "YONATOKEN", "Token3: Token name is wrong");
        BOOST_CHECK_MESSAGE(token3.units == 8, "Token3: Units is wrong");
        BOOST_CHECK_MESSAGE(token3.strIPFSHash == "", "Token3: IPFS/Txid hash is wrong");

        // Check to see if the reissue removal updated the cache correctly
        BOOST_CHECK_MESSAGE(cache.mapReissuedTokenData.count("YONATOKEN"), "Map of reissued data was removed, even though changes were made and not databased yet");
        BOOST_CHECK_MESSAGE(cache.mapTokensAddressAmount.at(make_pair("YONATOKEN", GetParams().GlobalBurnAddress())) == CAmount(100 * COIN), "Tokens total wasn't undone when reissuance was");
    }


    BOOST_AUTO_TEST_CASE(reissue_isvalid_test)
    {
        BOOST_TEST_MESSAGE("Running Reissue IsValid Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create tokens cache
        CTokensCache cache;

        CNewToken token1("YONATOKEN", CAmount(100 * COIN), 8, 1, 0, "");

        // Add an token to a valid yona address
        BOOST_CHECK_MESSAGE(cache.AddNewToken(token1, GetParams().GlobalBurnAddress(), 0, uint256()), "Failed to add new token");

        // Create a reissuance of the token that is valid
        CReissueToken reissue1("YONATOKEN", CAmount(1 * COIN), 8, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        std::string error;
        BOOST_CHECK_MESSAGE(ContextualCheckReissueToken(&cache, reissue1, error), "Reissue should have been valid");

        // Create a reissuance of the token that is not valid
        CReissueToken reissue2("NOTEXIST", CAmount(1 * COIN), 8, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        BOOST_CHECK_MESSAGE(!ContextualCheckReissueToken(&cache, reissue2, error), "Reissue shouldn't of been valid");

        // Create a reissuance of the token that is not valid (unit is smaller than current token)
        CReissueToken reissue3("YONATOKEN", CAmount(1 * COIN), 7, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        BOOST_CHECK_MESSAGE(!ContextualCheckReissueToken(&cache, reissue3, error), "Reissue shouldn't of been valid because of units");

        // Create a reissuance of the token that is not valid (unit is not changed)
        CReissueToken reissue4("YONATOKEN", CAmount(1 * COIN), -1, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        BOOST_CHECK_MESSAGE(ContextualCheckReissueToken(&cache, reissue4, error), "Reissue4 wasn't valid");

        // Create a new token object with units of 0
        CNewToken token2("YONATOKEN2", CAmount(100 * COIN), 0, 1, 0, "");

        // Add new token2 to a valid yona address
        BOOST_CHECK_MESSAGE(cache.AddNewToken(token2, GetParams().GlobalBurnAddress(), 0, uint256()), "Failed to add new token");

        // Create a reissuance of the token that is valid unit go from 0 -> 1 and change the ipfs hash
        CReissueToken reissue5("YONATOKEN2", CAmount(1 * COIN), 1, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        BOOST_CHECK_MESSAGE(ContextualCheckReissueToken(&cache, reissue5, error), "Reissue5 wasn't valid");

        // Create a reissuance of the token that is valid unit go from 1 -> 1 and change the ipfs hash
        CReissueToken reissue6("YONATOKEN2", CAmount(1 * COIN), 1, 1, DecodeTokenData("QmacSRmrkVmvJfbCpmU6pK72furJ8E8fbKHindrLxmYMQo"));

        BOOST_CHECK_MESSAGE(ContextualCheckReissueToken(&cache, reissue6, error), "Reissue6 wasn't valid");

        // Create a new token3 object
        CNewToken token3("DATAHASH", CAmount(100 * COIN), 8, 1, 0, "");

        // Add new token3 to a valid yona address
        BOOST_CHECK_MESSAGE(cache.AddNewToken(token3, GetParams().GlobalBurnAddress(), 0, uint256()), "Failed to add new token");

        // Create a reissuance of the token that is valid txid but messaging isn't active in unit tests
        CReissueToken reissue7("DATAHASH", CAmount(1 * COIN), 8, 1, DecodeTokenData("9c2c8e121a0139ba39bffd3ca97267bca9d4c0c1e84ac0c34a883c28e7a912ca"));

        BOOST_CHECK_MESSAGE(!ContextualCheckReissueToken(&cache, reissue7, error), "Reissue should have been not valid because messaging isn't active yet, and txid aren't allowed until messaging is active");
    }


BOOST_AUTO_TEST_SUITE_END()