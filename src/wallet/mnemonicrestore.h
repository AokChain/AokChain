// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_WALLET_MNEMONICRESTORE_H
#define AOKCHAIN_WALLET_MNEMONICRESTORE_H

#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <support/allocators/secure.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

class CWallet;

/**
 * Restore the keys of another wallet from its BIP39 recovery words: find the
 * places (derivation paths) where that wallet kept coins, by looking for its
 * addresses among the unspent outputs of the chain, and add those keys to
 * this wallet. Used by the GUI and by the importmnemonic RPC.
 *
 * The recovery words and the keys are never logged or stored outside the
 * wallet database.
 */
namespace mnemonicrestore {

/** Keys are imported this many places past the last one that holds coins. */
static const uint32_t GAP_LIMIT = 20;
/** How far the search goes along one path. */
static const uint32_t MAX_INDEX = 1000;

/** An address of the recovery words that holds coins now. */
struct FoundAddress {
    std::string path;
    CKeyID keyid;
    CAmount nAmount;     //!< AOK in unspent outputs
    int nOutputs;        //!< unspent outputs, token outputs included
    bool fTokens;        //!< some of the outputs carry tokens
};

/** A key to import. */
struct RestoreKey {
    std::string path;
    CKey key;
    bool fFound;         //!< holds coins now (gets a label)
};

struct Plan {
    std::vector<FoundAddress> found;
    std::vector<RestoreKey> keys;
    uint64_t nDerived = 0;   //!< keys tried
    std::string knownPath;   //!< path of the address given by the user, if it was found
    void Clear();
};

/**
 * Check the words (English and Korean word lists, checksum) and make the
 * BIP32 master key. The inputs are not kept.
 */
bool MasterFromMnemonic(const SecureString& words, const SecureString& passphrase, CExtKey& master, std::string& language, std::string& error);

/**
 * Find what to import. Looks at the usual layouts (bip39::CandidateLayouts)
 * for addresses with unspent outputs. If the user knows one address of that
 * wallet (pKnown), its path is searched among all coin types and imported
 * even without coins. Takes cs_main briefly to flush the chain state.
 * Returns false with an error if nothing was found or fCancel was set.
 */
bool BuildPlan(const CExtKey& master, const CKeyID* pKnown, const std::atomic<bool>& fCancel, Plan& plan, std::string& error);

/**
 * Add the keys of the plan to the wallet (which must be unlocked, and not for
 * staking only). Addresses that hold coins get the label. Returns the number
 * of keys that were new to the wallet, -1 on error.
 */
int ImportKeys(CWallet* pwallet, const Plan& plan, const std::string& label, std::string& error);

/**
 * Scan the whole chain for the transactions of the wallet, a few hundred
 * blocks at a time so that the locks are released in between. progress is
 * called with 0..100. Returns false if it was cancelled, the node shuts down
 * or blocks could not be read (error says which).
 */
bool Rescan(CWallet* pwallet, const std::atomic<bool>& fCancel, const std::function<void(int)>& progress, std::string& error);

} // namespace mnemonicrestore

#endif // AOKCHAIN_WALLET_MNEMONICRESTORE_H
