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

// The journal that keeps the database in step with the chain state (CGovernance::RecoverTo):
// what a start finds after a stop at each point between two writes of the chain state.
BOOST_AUTO_TEST_CASE(governance_journal)
{
    CGovernance db(1 << 20, true, true);
    BOOST_REQUIRE(db.Init(true, Params()));
    const CScript a = Script(1), b = Script(2);
    const uint256 block0 = uint256S("a0"), block1 = uint256S("a1"), block2 = uint256S("a2"), block3 = uint256S("a3"), other = uint256S("b0");
    const uint256 initial = db.ContentHash();
    std::string error;
    auto freeze = [&](const CScript& script, int height) {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.FreezeScript(script));
        BOOST_CHECK(db.UpdateCost(height * COIN, GOVERNANCE_COST_ROOT, height));
        BOOST_CHECK(changes.Commit());
    };

    // the chain state is at block0, two blocks change the governance state, the node stops:
    // the chain state was not written since, the changes are undone
    BOOST_CHECK(db.RecoverTo(block0, true, error));
    freeze(a, 100);
    freeze(b, 101);
    BOOST_CHECK(!db.CanSend(a) && !db.CanSend(b));
    BOOST_CHECK(db.RecoverTo(block0, true, error));
    BOOST_CHECK(db.CanSend(a) && db.CanSend(b));
    BOOST_CHECK_EQUAL(db.GetNumberOfFrozenScripts(), 0U);
    BOOST_CHECK(db.ContentHash() == initial);

    // a write of the chain state in between: what was changed before it stays
    freeze(a, 100);
    const uint256 at1 = db.ContentHash();
    BOOST_CHECK(db.BeginChainStateFlush(block1));
    BOOST_CHECK(db.EndChainStateFlush(block1));
    freeze(b, 101);
    BOOST_CHECK(db.RecoverTo(block1, true, error));
    BOOST_CHECK(!db.CanSend(a) && db.CanSend(b));
    BOOST_CHECK(db.ContentHash() == at1);

    // a stop after the chain state was written and before that was noted here: nothing is undone
    freeze(b, 101);
    const uint256 at2 = db.ContentHash();
    BOOST_CHECK(db.BeginChainStateFlush(block2));
    BOOST_CHECK(db.RecoverTo(block2, true, error));
    BOOST_CHECK(db.ContentHash() == at2);

    // a stop after the write was announced and before the chain state was written: back to the write before
    {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.UnfreezeScript(a));
        BOOST_CHECK(changes.Commit());
    }
    BOOST_CHECK(db.BeginChainStateFlush(block3));
    BOOST_CHECK(db.RecoverTo(block2, true, error));
    BOOST_CHECK(!db.CanSend(a) && !db.CanSend(b));
    BOOST_CHECK(db.ContentHash() == at2);

    // a chain state at a block that the journal does not know
    {
        CGovernanceChanges changes(&db);
        BOOST_CHECK(db.UnfreezeScript(a));
        BOOST_CHECK(changes.Commit());
    }
    const uint256 ahead = db.ContentHash();
    //  ...written by this version: the journal does not explain it, and nothing is touched
    BOOST_CHECK(!db.RecoverTo(other, true, error));
    BOOST_CHECK(!error.empty());
    BOOST_CHECK(db.ContentHash() == ahead);
    //  ...written by a version without the journal: the journal is dropped, the database stays
    BOOST_CHECK(db.RecoverTo(other, false, error));
    BOOST_CHECK(db.ContentHash() == ahead);
    BOOST_CHECK(db.RecoverTo(block0, true, error)); // (nothing is left of the journal)
    BOOST_CHECK(db.ContentHash() == ahead);
}

BOOST_AUTO_TEST_SUITE_END()
