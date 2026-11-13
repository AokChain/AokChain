// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "util.h"
#include "test/test_aokchain.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

    // AokChain retargets with every block: the target of the last proof-of-stake block is moved
    // towards the target spacing of 64 seconds by the time that the last block took,
    //     new = old * (14 * 64 + 2 * actual) / (16 * 64),
    // where the time counts as 64 seconds when it is negative and as 640 when it is longer, and
    // the result is not easier than the proof-of-stake limit. The values are those of the formula,
    // computed outside this program.
    static unsigned int NextTarget(unsigned int nBits, int64_t nActualSpacing)
    {
        const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
        CBlockIndex pindexLast;
        pindexLast.nHeight = 100000;
        pindexLast.nTime = 1700000000;
        pindexLast.nBits = nBits;
        pindexLast.SetProofOfStake();
        return CalculateNextTargetRequired(&pindexLast, pindexLast.nTime - nActualSpacing, chainParams->GetConsensus());
    }

    /* A block that took the target spacing leaves the target as it is */
    BOOST_AUTO_TEST_CASE(get_next_work_test)
    {
        BOOST_TEST_MESSAGE("Running Get Next Work Test");
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, 64), 0x1a01245f);
        // a fast block makes the next one harder, a slow one easier
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, 16), 0x1a0108f6);
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, 640), 0x1a026d49);
    }

    /* The target is never easier than the proof-of-stake limit */
    BOOST_AUTO_TEST_CASE(get_next_work_pow_limit_test)
    {
        BOOST_TEST_MESSAGE("Running Get Next Work POW Limit Test");
        BOOST_CHECK_EQUAL(NextTarget(0x1b00ffff, 640), 0x1b00ffff);
        BOOST_CHECK_EQUAL(NextTarget(0x1b00ff00, 640), 0x1b00ffff);
    }

    /* A time that runs backwards counts as the target spacing */
    BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual_test)
    {
        BOOST_TEST_MESSAGE("Running Get Next Work Lower Limit Actual Test");
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, -5), 0x1a01245f);
    }

    /* A block that took longer than ten times the target spacing counts as ten times */
    BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual_test)
    {
        BOOST_TEST_MESSAGE("Running Get Next Work Upper Limit Actual  Test");
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, 100000), 0x1a026d49);
        BOOST_CHECK_EQUAL(NextTarget(0x1a01245f, 641), 0x1a026d49);
    }

    BOOST_AUTO_TEST_CASE(get_block_proof_equivalent_time_test)
    {
        BOOST_TEST_MESSAGE("Running Get Block Proof Equivalent Time Test");

        const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
        std::vector<CBlockIndex> blocks(10000);
        for (int i = 0; i < 10000; i++)
        {
            blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
            blocks[i].nHeight = i;
            blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nTargetSpacing;
            blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
            blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
        }

        for (int j = 0; j < 1000; j++)
        {
            CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
            CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
            CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

            int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
            BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
        }
    }

BOOST_AUTO_TEST_SUITE_END()
