// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "consensus/validation.h"
#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "pos.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/test_aokchain.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(offlinestake_tests, BasicTestingSetup)

static CScript OfflineScript(const CKey& staking, const CKey& spending)
{
    return GetScriptForDestination(std::make_pair(staking.GetPubKey().GetID(), spending.GetPubKey().GetID()));
}

static bool Verify(const CMutableTransaction& tx, unsigned int nIn, const CScript& scriptPubKey, CAmount amount)
{
    ScriptError error;
    const CTransaction txConst(tx);
    return VerifyScript(tx.vin[nIn].scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS,
                        TransactionSignatureChecker(&txConst, nIn, amount), &error);
}

// What the script of an offline staking output allows, on its own: the staking key signs in a
// coinstake and nowhere else, the spending key signs everywhere else.
BOOST_AUTO_TEST_CASE(offline_stake_script)
{
    CKey staking, ownerSpending, otherSpending;
    staking.MakeNewKey(true);
    ownerSpending.MakeNewKey(true);
    otherSpending.MakeNewKey(true);

    // the coins of an owner who delegated the staking, and a coin of somebody else with the
    // same staking key
    const CScript ownerScript = OfflineScript(staking, ownerSpending);
    const CScript otherScript = OfflineScript(staking, otherSpending);
    BOOST_CHECK(ownerScript.IsOfflineStaking());
    const CAmount ownerValue = 1000 * COIN, otherValue = 10 * COIN;

    CBasicKeyStore stakingOnly;
    stakingOnly.AddKey(staking);

    // an ordinary transaction: the staking key cannot spend the owner's coin
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(uint256S("01"), 0);
        tx.vout.push_back(CTxOut(ownerValue, otherScript));
        BOOST_CHECK(!CTransaction(tx).IsCoinStake());
        BOOST_CHECK(!SignSignature(stakingOnly, ownerScript, tx, 0, ownerValue, SIGHASH_ALL, false) || !Verify(tx, 0, ownerScript, ownerValue));
        CBasicKeyStore owner;
        owner.AddKey(ownerSpending);
        BOOST_CHECK(SignSignature(owner, ownerScript, tx, 0, ownerValue, SIGHASH_ALL, false));
        BOOST_CHECK(Verify(tx, 0, ownerScript, ownerValue));
    }

    // A coinstake whose kernel is the other coin and which takes the owner's coin as a second
    // input: the staking key signs both. The script of the owner's coin has no objection to the
    // outputs, which all pay the script of the kernel. It is the block validation that must
    // refuse this (CheckOfflineStakeScripts).
    {
        CMutableTransaction tx;
        tx.vin.resize(2);
        tx.vin[0].prevout = COutPoint(uint256S("02"), 0); // kernel: the other coin
        tx.vin[1].prevout = COutPoint(uint256S("01"), 0); // the owner's coin
        tx.vout.push_back(CTxOut(0, CScript()));          // marks a coinstake
        tx.vout.push_back(CTxOut(ownerValue + otherValue, otherScript));
        BOOST_CHECK(CTransaction(tx).IsCoinStake());
        BOOST_CHECK(SignSignature(stakingOnly, otherScript, tx, 0, otherValue, SIGHASH_ALL, true));
        BOOST_CHECK(SignSignature(stakingOnly, ownerScript, tx, 1, ownerValue, SIGHASH_ALL, true));
        BOOST_CHECK(Verify(tx, 0, otherScript, otherValue));
        BOOST_CHECK(Verify(tx, 1, ownerScript, ownerValue));
    }
}

// The rules of block validation for offline staking coins in a coinstake.
BOOST_AUTO_TEST_CASE(offline_stake_rules)
{
    CKey staking, ownerSpending, otherSpending, plain;
    staking.MakeNewKey(true);
    ownerSpending.MakeNewKey(true);
    otherSpending.MakeNewKey(true);
    plain.MakeNewKey(true);
    const CScript ownerScript = OfflineScript(staking, ownerSpending);
    const CScript otherScript = OfflineScript(staking, otherSpending);
    const CScript plainScript = GetScriptForDestination(plain.GetPubKey().GetID());

    CCoinsView base;
    CCoinsViewCache view(&base);
    const COutPoint owner1(uint256S("01"), 0), owner2(uint256S("01"), 1), other(uint256S("02"), 0), normal1(uint256S("03"), 0), normal2(uint256S("03"), 1);
    view.AddCoin(owner1, Coin(CTxOut(1000 * COIN, ownerScript), 10, false, false, 1), false);
    view.AddCoin(owner2, Coin(CTxOut(500 * COIN, ownerScript), 10, false, false, 1), false);
    view.AddCoin(other, Coin(CTxOut(10 * COIN, otherScript), 10, false, false, 1), false);
    view.AddCoin(normal1, Coin(CTxOut(10 * COIN, plainScript), 10, false, false, 1), false);
    view.AddCoin(normal2, Coin(CTxOut(20 * COIN, plainScript), 10, false, false, 1), false);

    auto stake = [](const std::vector<COutPoint>& inputs, const std::vector<CTxOut>& outputs) {
        CMutableTransaction tx;
        for (const COutPoint& input : inputs)
            tx.vin.push_back(CTxIn(input));
        tx.vout.push_back(CTxOut(0, CScript()));
        for (const CTxOut& output : outputs)
            tx.vout.push_back(output);
        return CTransaction(tx);
    };
    auto allowed = [&view](const CTransaction& tx) {
        CValidationState state;
        const bool ok = CheckOfflineStakeScripts(tx, view, state);
        BOOST_CHECK(ok || state.GetRejectReason() == "bad-offline-stake" || state.GetRejectReason() == "bad-cs-inputs-missingorspent");
        return ok;
    };

    // what the wallet builds: the coins of one script, paid back to that script with the reward
    BOOST_CHECK(allowed(stake({owner1}, {CTxOut(1004 * COIN, ownerScript)})));
    BOOST_CHECK(allowed(stake({owner1, owner2}, {CTxOut(752 * COIN, ownerScript), CTxOut(752 * COIN, ownerScript)})));
    BOOST_CHECK(allowed(stake({owner1}, {CTxOut(1000 * COIN, ownerScript)})));
    // coinstakes without offline staking coins are no business of this rule
    BOOST_CHECK(allowed(stake({normal1, normal2}, {CTxOut(34 * COIN, plainScript)})));
    BOOST_CHECK(allowed(stake({normal1}, {CTxOut(5 * COIN, ownerScript)})));

    // an output to another script (this much was refused before, for the kernel)
    BOOST_CHECK(!allowed(stake({owner1}, {CTxOut(1004 * COIN, otherScript)})));
    BOOST_CHECK(!allowed(stake({owner1}, {CTxOut(1000 * COIN, ownerScript), CTxOut(4 * COIN, plainScript)})));
    // the theft: a kernel of another script, the owner's coin behind it, all paid to the kernel's script
    BOOST_CHECK(!allowed(stake({other, owner1}, {CTxOut(1014 * COIN, otherScript)})));
    // the same with an ordinary coin as kernel, where no rule applied at all
    BOOST_CHECK(!allowed(stake({normal1, owner1}, {CTxOut(1014 * COIN, plainScript)})));
    BOOST_CHECK(!allowed(stake({normal1, owner1}, {CTxOut(1014 * COIN, ownerScript)})));
    // ordinary coins behind an offline staking kernel
    BOOST_CHECK(!allowed(stake({owner1, normal1}, {CTxOut(1014 * COIN, ownerScript)})));
    // destroying the coins: less is paid back than was taken
    BOOST_CHECK(!allowed(stake({owner1}, {CTxOut(999 * COIN, ownerScript)})));
    BOOST_CHECK(!allowed(stake({owner1, owner2}, {CTxOut(1000 * COIN, ownerScript)})));
    // an input that does not exist
    BOOST_CHECK(!allowed(stake({owner1, COutPoint(uint256S("09"), 0)}, {CTxOut(1004 * COIN, ownerScript)})));
}

BOOST_AUTO_TEST_SUITE_END()
