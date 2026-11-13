// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <governance/governance.h>
#include <test/test_aokchain.h>

#include <boost/test/unit_test.hpp>

// The levels of governance changes (CGovernance::BeginChanges): what a block commands is seen
// by the rest of the block at once, and is in the database only when the level is committed.

BOOST_FIXTURE_TEST_SUITE(governance_tests, TestingSetup)

static CScript Script(unsigned char n)
{
    return CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, n) << OP_EQUALVERIFY << OP_CHECKSIG;
}

static size_t Frozen(CGovernance& db)
{
    std::vector<CScript> frozen;
    db.GetFrozenScripts(&frozen);
    return frozen.size();
}

BOOST_AUTO_TEST_CASE(governance_levels)
{
    CGovernance db(1 << 20, true, true);
    BOOST_REQUIRE(db.Init(true, Params()));
    const CScript a = Script(1), b = Script(2);
    const CAmount root = db.GetCost(GOVERNANCE_COST_ROOT);
    const CScript fee = db.GetFeeScript();
    const uint256 initial = db.ContentHash();
    // (the database starts with a dummy entry in the freeze list, which is never an address)
    const size_t dummy = Frozen(db);

    // a discarded level leaves nothing behind
    {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.FreezeScript(a));
        BOOST_CHECK(!db.CanSend(a)); // seen while the level is open
        BOOST_CHECK(db.ScriptExist(a));
        BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 1U);
        BOOST_CHECK_EQUAL(Frozen(db), dummy + 1);
        BOOST_CHECK(db.UpdateCost(7 * COIN, GOVERNANCE_COST_ROOT, 100));
        BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 7 * COIN);
        BOOST_CHECK(db.UpdateFeeScript(b, 100));
        BOOST_CHECK(db.GetFeeScript() == b);
        BOOST_CHECK(db.ContentHash() == initial); // nothing written yet
    }
    BOOST_CHECK(db.CanSend(a));
    BOOST_CHECK(!db.ScriptExist(a));
    BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 0U);
    BOOST_CHECK_EQUAL(Frozen(db), dummy);
    BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), root);
    BOOST_CHECK(db.GetFeeScript() == fee);
    BOOST_CHECK(db.ContentHash() == initial);

    // a committed level is written
    {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.FreezeScript(a));
        BOOST_CHECK(db.UpdateCost(7 * COIN, GOVERNANCE_COST_ROOT, 100));
        BOOST_CHECK(db.UpdateFeeScript(b, 100));
        BOOST_CHECK(changes.Commit());
    }
    BOOST_CHECK(!db.CanSend(a));
    BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 1U);
    BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 7 * COIN);
    BOOST_CHECK(db.GetFeeScript() == b);
    const uint256 committed = db.ContentHash();
    BOOST_CHECK(committed != initial);

    // nested levels: an inner commit goes to the outer level; the outer one is discarded
    // (the check of the last blocks at startup)
    {
        CGovernanceChanges outer(&db);
        {
            CGovernanceChanges inner(&db);
            BOOST_CHECK(db.RevertFreezeScript(a));
            BOOST_CHECK(db.RevertUpdateCost(GOVERNANCE_COST_ROOT, 100));
            BOOST_CHECK(db.RevertUpdateFeeScript(100));
            BOOST_CHECK(inner.Commit());
        }
        // the reverts are seen: an erased value shows the one below it
        BOOST_CHECK(db.CanSend(a));
        BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 0U);
        BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), root);
        BOOST_CHECK(db.GetFeeScript() == fee);
        // reverting what is not there fails, as before
        BOOST_CHECK(!db.RevertUpdateCost(GOVERNANCE_COST_ROOT, 100));
        BOOST_CHECK(!db.RevertFreezeScript(a));
        {
            CGovernanceChanges inner(&db);
            BOOST_CHECK(db.FreezeScript(a));
            BOOST_CHECK(db.FreezeScript(b));
            BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 2U);
            BOOST_CHECK_EQUAL(Frozen(db), dummy + 2);
            // discarded
        }
        BOOST_CHECK(db.CanSend(a));
        BOOST_CHECK(db.CanSend(b));
        BOOST_CHECK(!db.ScriptExist(b));
        BOOST_CHECK(db.ContentHash() == committed);
    }
    BOOST_CHECK(db.ContentHash() == committed);
    BOOST_CHECK(!db.CanSend(a));
    BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 1U);
    BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 7 * COIN);

    // a second cost command for the same type and height does nothing, as before
    {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.UpdateCost(9 * COIN, GOVERNANCE_COST_ROOT, 100));
        BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 7 * COIN);
        BOOST_CHECK(db.UpdateCost(9 * COIN, GOVERNANCE_COST_ROOT, 101));
        BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 9 * COIN); // the highest height counts
        BOOST_CHECK(changes.Commit());
    }
    BOOST_CHECK_EQUAL(db.GetCost(GOVERNANCE_COST_ROOT), 9 * COIN);

    // without a level every command is written at once, as before
    BOOST_CHECK(db.UnfreezeScript(a));
    BOOST_CHECK(db.CanSend(a));
    BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 0U);
    BOOST_CHECK(db.ContentHash() != committed);
}

BOOST_AUTO_TEST_SUITE_END()
