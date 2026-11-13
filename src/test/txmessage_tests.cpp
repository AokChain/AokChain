// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/test_aokchain.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(txmessage_tests, BasicTestingSetup)

// The message of a version 2 transaction is part of the transaction and of its id, and is not
// part of what the signatures sign (doc/aokchain/04-security-audit.md, SEC-27). This test states
// how it is; it is to be turned round when a fork makes the signatures cover the message.
BOOST_AUTO_TEST_CASE(message_is_not_signed)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);
    const CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    const CAmount amount = 10 * COIN;

    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.nTime = 1700000000;
    tx.nMessage = "invoice 1001";
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(uint256S("01"), 0);
    tx.vout.push_back(CTxOut(9 * COIN, scriptPubKey));
    BOOST_REQUIRE(SignSignature(keystore, scriptPubKey, tx, 0, amount, SIGHASH_ALL));

    auto verifies = [&](const CMutableTransaction& candidate) {
        ScriptError error;
        const CTransaction txConst(candidate);
        return VerifyScript(candidate.vin[0].scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS,
                            TransactionSignatureChecker(&txConst, 0, amount), &error);
    };
    BOOST_CHECK(verifies(tx));
    const uint256 txid = CTransaction(tx).GetHash();

    // somebody else changes the message: the signature still holds, the id is another one
    CMutableTransaction changed(tx);
    changed.nMessage = "invoice 9999";
    BOOST_CHECK(verifies(changed));
    BOOST_CHECK(CTransaction(changed).GetHash() != txid);

    // what is signed cannot be changed: the time, an output
    CMutableTransaction other(tx);
    other.nTime++;
    BOOST_CHECK(!verifies(other));
    other = tx;
    other.vout[0].nValue--;
    BOOST_CHECK(!verifies(other));
}

BOOST_AUTO_TEST_SUITE_END()
