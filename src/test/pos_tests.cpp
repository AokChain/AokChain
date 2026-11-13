// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <pos.h>
#include <test/test_aokchain.h>

#include <limits>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pos_tests, BasicTestingSetup)

// Soft fork S-05: proof-of-stake block times must be aligned to the stake
// timestamp mask from nStakeTimestampMaskHeight on.
BOOST_AUTO_TEST_CASE(stake_timestamp_mask)
{
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& regtest = Params().GetConsensus();
    BOOST_CHECK_EQUAL(regtest.nStakeTimestampMask, 0xf);
    BOOST_CHECK_EQUAL(regtest.nStakeTimestampMaskHeight, 0);
    BOOST_CHECK(CheckStakeBlockTimestamp(1700000000, 1, regtest));      // multiple of 16
    BOOST_CHECK(!CheckStakeBlockTimestamp(1700000001, 1, regtest));
    BOOST_CHECK(!CheckStakeBlockTimestamp(1700000015, 1, regtest));

    // Not scheduled on mainnet yet: every timestamp is accepted, as before.
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params main = Params().GetConsensus();
    BOOST_CHECK_EQUAL(main.nStakeTimestampMaskHeight, std::numeric_limits<int>::max());
    BOOST_CHECK(CheckStakeBlockTimestamp(1700000001, 3030488, main));

    // Activation boundary
    main.nStakeTimestampMaskHeight = 100;
    BOOST_CHECK(CheckStakeBlockTimestamp(1700000001, 99, main));
    BOOST_CHECK(!CheckStakeBlockTimestamp(1700000001, 100, main));
    BOOST_CHECK(CheckStakeBlockTimestamp(1700000000, 100, main));
}

BOOST_AUTO_TEST_SUITE_END()
