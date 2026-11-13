// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_BIP39_H
#define AOKCHAIN_BIP39_H

#include <stdint.h>
#include <string>
#include <vector>

/**
 * BIP39 mnemonic sentences (recovery words), as used by wallets other than
 * this one. AokChain Core does not create its own keys from a mnemonic; this
 * is for getting at keys that such a wallet created.
 */
namespace bip39 {

/**
 * Bring a mnemonic or passphrase into the form BIP39 hashes (UTF-8 NFKD).
 * ASCII is already in that form and Hangul syllables are decomposed; any
 * other non-ASCII text is refused, because it cannot be normalized here.
 * With fWords, whitespace (including the ideographic space) is collapsed to
 * single spaces and ASCII letters are lower-cased.
 */
bool Normalize(const std::string& str, bool fWords, std::string& out);

/**
 * Check the words and the checksum of a normalized mnemonic against the
 * English and Korean word lists. Sets the language on success, a message
 * otherwise.
 */
bool CheckMnemonic(const std::string& mnemonic, std::string& language, std::string& error);

/** The 64 byte seed: PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" + passphrase, 2048). */
std::vector<unsigned char> MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase);

/** Parse a BIP32 path such as m/44'/0'/0'/0 (' or h marks a hardened level). */
bool ParsePath(const std::string& path, std::vector<uint32_t>& out);

/** Format a BIP32 path. */
std::string FormatPath(const std::vector<uint32_t>& path);

/** A place where wallets keep keys: the children 0..nCount-1 of a path. */
struct PathLayout {
    std::vector<uint32_t> parent;
    uint32_t nCount;
    bool fHardened; //!< the children are hardened
};

/**
 * The key layouts that wallets commonly use, most likely first: simple
 * layouts without a coin type (m/k, m/0'/0'/k' and the like, 100 children),
 * then BIP44 (m/44'/coin'/account'/change/k, accounts 0 and 1, 20 children)
 * and the same under the BIP49 and BIP84 purposes for the common coin types.
 * With fAllCoinTypes, BIP44 is listed for every coin type from 0 to 2047
 * (about a quarter of a million keys); without it only for the common ones
 * (about seven thousand keys).
 */
std::vector<PathLayout> CandidateLayouts(bool fAllCoinTypes);

} // namespace bip39

#endif // AOKCHAIN_BIP39_H
