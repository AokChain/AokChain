// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "validation.h"
#include "net.h"

#include "test/test_aokchain.h"

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

    // AokChain: 4 coins per block, halved every nSubsidyHalvingInterval blocks, nothing after ten
    // halvings; block 1 carries the premine.
    static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
    {
        const CAmount nInitialSubsidy = 4 * COIN;
        const int nInterval = consensusParams.nSubsidyHalvingInterval;
        for (int nHalvings = 0; nHalvings < 10; nHalvings++) {
            const CAmount nExpected = nInitialSubsidy >> nHalvings;
            // the first block of the period (but block 1), one in the middle and the last one
            if (nHalvings * nInterval != 1)
                BOOST_CHECK_EQUAL(GetBlockSubsidy(nHalvings * nInterval, consensusParams), nExpected);
            BOOST_CHECK_EQUAL(GetBlockSubsidy(nHalvings * nInterval + nInterval / 2, consensusParams), nExpected);
            BOOST_CHECK_EQUAL(GetBlockSubsidy((nHalvings + 1) * nInterval - 1, consensusParams), nExpected);
        }
        BOOST_CHECK_EQUAL(GetBlockSubsidy(9 * nInterval, consensusParams), 781250); // 0.0078125
        for (int nHalvings = 10; nHalvings < 70; nHalvings++)
            BOOST_CHECK_EQUAL(GetBlockSubsidy(nHalvings * nInterval, consensusParams), 0);
        BOOST_CHECK_EQUAL(GetBlockSubsidy(1, consensusParams), 2100000000 * COIN);
    }

    static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
    {
        Consensus::Params consensusParams;
        consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
        TestBlockSubsidyHalvings(consensusParams);
    }

    BOOST_AUTO_TEST_CASE(block_subsidy_test)
    {
        BOOST_TEST_MESSAGE("Running Block Subsidy Test");

        const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
        BOOST_CHECK_EQUAL(chainParams->GetConsensus().nSubsidyHalvingInterval, 525960);
        TestBlockSubsidyHalvings(chainParams->GetConsensus()); // As in main
        TestBlockSubsidyHalvings(240); // Short intervals
        TestBlockSubsidyHalvings(1000);
    }

    BOOST_AUTO_TEST_CASE(subsidy_limit_test)
    {
        BOOST_TEST_MESSAGE("Running Subsidy Limit Test");

        // every coin that will ever exist, block by block
        const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
        const Consensus::Params& params = chainParams->GetConsensus();
        CAmount nSum = 0;
        for (int nHeight = 1; nHeight < 11 * params.nSubsidyHalvingInterval; nHeight++)
        {
            nSum += GetBlockSubsidy(nHeight, params);
            BOOST_REQUIRE(MoneyRange(nSum));
        }
        // the premine, and 4 + 2 + 1 + ... + 4/512 coins for 525960 blocks each; block 0 has no
        // coins and block 1 has the premine instead of 4
        const CAmount nExpected = 2100000000 * COIN + 525960 * (CAmount)799218750 - 2 * 4 * COIN;
        BOOST_CHECK_EQUAL(nSum, nExpected);
        BOOST_CHECK_EQUAL(nSum, 210420356293750000LL); // 2,104,203,562.9375
        BOOST_CHECK(nSum < MAX_MONEY / 10);
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
