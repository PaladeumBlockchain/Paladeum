// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "validation.h"
#include "net.h"

#include "test/test_paladeum.h"

#include <limits>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

    // Paladeum: block 1 carries the premine (1,000,000,000 coins), every other block 10 coins;
    // there is no halving.
    static void TestBlockSubsidy(const Consensus::Params& consensusParams)
    {
        BOOST_CHECK_EQUAL(GetBlockSubsidy(1, consensusParams), 1000000000 * COIN);
        for (int nHeight : {2, 3, 150, 151, 152, 1000, 525960, 716779, 10000000, std::numeric_limits<int>::max()})
            BOOST_CHECK_EQUAL(GetBlockSubsidy(nHeight, consensusParams), 10 * COIN);
    }

    BOOST_AUTO_TEST_CASE(block_subsidy_test)
    {
        BOOST_TEST_MESSAGE("Running Block Subsidy Test");

        for (const std::string& chain : {CBaseChainParams::MAIN, CBaseChainParams::TESTNET, CBaseChainParams::REGTEST}) {
            const auto chainParams = CreateChainParams(chain);
            TestBlockSubsidy(chainParams->GetConsensus());
        }
    }

    BOOST_AUTO_TEST_CASE(subsidy_limit_test)
    {
        BOOST_TEST_MESSAGE("Running Subsidy Limit Test");

        // every coin of the first 2,000,000 blocks, block by block
        const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
        const Consensus::Params& params = chainParams->GetConsensus();
        CAmount nSum = 0;
        const int nBlocks = 2000000;
        for (int nHeight = 1; nHeight <= nBlocks; nHeight++)
        {
            nSum += GetBlockSubsidy(nHeight, params);
            BOOST_REQUIRE(MoneyRange(nSum));
        }
        BOOST_CHECK_EQUAL(nSum, 1000000000 * COIN + (CAmount)(nBlocks - 1) * 10 * COIN);

        // The supply grows by 10 coins a block without an end. At the last height a block can
        // have (INT_MAX) it is still below MAX_MONEY, so every sum of coins stays in range.
        const CAmount nLast = 1000000000 * COIN + (CAmount)(std::numeric_limits<int>::max() - 1) * 10 * COIN;
        BOOST_CHECK(MoneyRange(nLast));
        BOOST_CHECK(nLast < MAX_MONEY);
    }

    bool ReturnFalse()
    { return false; }

    bool ReturnTrue()
    { return true; }

    BOOST_AUTO_TEST_CASE(combiner_all_test)
    {
        BOOST_TEST_MESSAGE("Running Combiner All Test");

        boost::signals2::signal<bool(), CombinerAll> Test;
        BOOST_CHECK(Test());
        Test.connect(&ReturnFalse);
        BOOST_CHECK(!Test());
        Test.connect(&ReturnTrue);
        BOOST_CHECK(!Test());
        Test.disconnect(&ReturnFalse);
        BOOST_CHECK(Test());
        Test.disconnect(&ReturnTrue);
        BOOST_CHECK(Test());
    }

BOOST_AUTO_TEST_SUITE_END()
