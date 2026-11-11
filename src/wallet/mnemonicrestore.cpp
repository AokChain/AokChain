// Copyright (c) 2026 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <snapshot.h>
#include <wallet/mnemonicrestore.h>

#include <bip39.h>
#include <chain.h>
#include <coins.h>
#include <init.h>
#include <support/cleanse.h>
#include <txdb.h>
#include <util.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <map>
#include <set>

namespace mnemonicrestore {

void Plan::Clear()
{
    found.clear();
    keys.clear();
    nDerived = 0;
    knownPath.clear();
}

bool MasterFromMnemonic(const SecureString& words, const SecureString& passphrase, CExtKey& master, std::string& language, std::string& error)
{
    std::string mnemonic, pass;
    bool fOk = true;
    {
        std::string in(words.begin(), words.end());
        if (!bip39::Normalize(in, true, mnemonic)) {
            error = "The recovery words contain characters that cannot be used here (only English and Korean words are supported)";
            fOk = false;
        }
        memory_cleanse(&in[0], in.size());
    }
    if (fOk && !passphrase.empty()) {
        std::string in(passphrase.begin(), passphrase.end());
        if (!bip39::Normalize(in, false, pass)) {
            error = "The passphrase contains characters that cannot be used here (only ASCII and Hangul are supported)";
            fOk = false;
        }
        memory_cleanse(&in[0], in.size());
    }
    if (fOk && !bip39::CheckMnemonic(mnemonic, language, error))
        fOk = false;
    if (fOk) {
        std::vector<unsigned char> seed = bip39::MnemonicToSeed(mnemonic, pass);
        master.SetSeed(seed.data(), seed.size());
        memory_cleanse(seed.data(), seed.size());
    }
    if (!mnemonic.empty())
        memory_cleanse(&mnemonic[0], mnemonic.size());
    if (!pass.empty())
        memory_cleanse(&pass[0], pass.size());
    return fOk;
}

namespace {

/** The children of one path, derived as far as needed. */
struct Family {
    bip39::PathLayout layout;
    CExtKey parent;
    bool fValid = false;
    std::vector<CKey> keys; //!< child i, compressed; invalid if BIP32 skips the index
};

struct KeyRef {
    size_t nFamily;
    uint32_t nIndex;
    bool fCompressed;
};

struct Hit {
    CAmount nAmount = 0;
    int nOutputs = 0;
    bool fTokens = false;
};

typedef std::map<CKeyID, KeyRef> KeyIndex;

bool DeriveParent(const CExtKey& master, const std::vector<uint32_t>& path, CExtKey& out)
{
    out = master;
    for (uint32_t n : path) {
        CExtKey child;
        if (!out.Derive(child, n))
            return false;
        out = child;
    }
    return true;
}

/** Derive the children of a family up to nCount and enter the new ones into the index. */
void Extend(std::vector<Family>& families, size_t nFamily, uint32_t nCount, KeyIndex& index, std::set<CKeyID>* pNew, uint64_t& nDerived)
{
    Family& family = families[nFamily];
    if (!family.fValid)
        return;
    const uint32_t H = 0x80000000;
    for (uint32_t i = family.keys.size(); i < nCount; i++) {
        CExtKey child;
        CKey key;
        if (family.parent.Derive(child, family.layout.fHardened ? (i | H) : i)) {
            key = child.key;
            CPubKey pubkey = key.GetPubKey();
            const CKeyID compressed = pubkey.GetID();
            index.emplace(compressed, KeyRef{nFamily, i, true});
            if (pNew) pNew->insert(compressed);
            if (pubkey.Decompress()) {
                const CKeyID uncompressed = pubkey.GetID();
                index.emplace(uncompressed, KeyRef{nFamily, i, false});
                if (pNew) pNew->insert(uncompressed);
            }
            nDerived++;
        }
        family.keys.push_back(key);
    }
}

/**
 * Walk the unspent outputs of the chain and sum those that pay to one of the
 * keys: pay-to-pubkey-hash (with or without token data behind it) and
 * pay-to-pubkey.
 */
bool ScanUnspent(const KeyIndex& index, const std::set<CKeyID>* pOnly, const std::atomic<bool>& fCancel, std::map<CKeyID, Hit>& hits)
{
    FlushStateToDisk();
    std::unique_ptr<CCoinsViewCursor> pcursor(pcoinsdbview->Cursor());
    if (!pcursor)
        return false;
    uint64_t n = 0;
    for (; pcursor->Valid(); pcursor->Next()) {
        if ((++n & 0xfff) == 0 && (fCancel || ShutdownRequested()))
            return false;
        Coin coin;
        if (!pcursor->GetValue(coin))
            return false;
        const CScript& script = coin.out.scriptPubKey;
        CKeyID keyid;
        bool fPlain = false;
        if (script.size() >= 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
            keyid = CKeyID(uint160(std::vector<unsigned char>(script.begin() + 3, script.begin() + 23)));
            fPlain = script.size() == 25;
        } else if ((script.size() == 35 && script[0] == 33 && script[34] == OP_CHECKSIG) || (script.size() == 67 && script[0] == 65 && script[66] == OP_CHECKSIG)) {
            const CPubKey pubkey(script.begin() + 1, script.end() - 1);
            if (!pubkey.IsValid())
                continue;
            keyid = pubkey.GetID();
            fPlain = true;
        } else {
            continue;
        }
        if (pOnly ? !pOnly->count(keyid) : !index.count(keyid))
            continue;
        Hit& hit = hits[keyid];
        hit.nOutputs++;
        if (fPlain)
            hit.nAmount += coin.out.nValue;
        else
            hit.fTokens = true;
    }
    return true;
}

/** The other one of the receiving (…/0) and the change (…/1) chain of a BIP44 style path. */
bool Sibling(const bip39::PathLayout& layout, bip39::PathLayout& out)
{
    if (layout.fHardened || layout.parent.size() < 4 || layout.parent.back() > 1)
        return false;
    out = layout;
    out.parent.back() ^= 1;
    return true;
}

} // namespace

bool BuildPlan(const CExtKey& master, const CKeyID* pKnown, const std::atomic<bool>& fCancel, Plan& plan, std::string& error)
{
    plan.Clear();
    std::vector<Family> families;
    KeyIndex index;
    auto addFamily = [&](const bip39::PathLayout& layout) -> size_t {
        for (size_t i = 0; i < families.size(); i++) {
            if (families[i].layout.parent == layout.parent && families[i].layout.fHardened == layout.fHardened)
                return i;
        }
        Family family;
        family.layout = layout;
        family.fValid = DeriveParent(master, layout.parent, family.parent);
        families.push_back(family);
        return families.size() - 1;
    };

    // 1. the usual layouts
    for (const bip39::PathLayout& layout : bip39::CandidateLayouts(false)) {
        if (fCancel) { error = "Cancelled"; return false; }
        const size_t n = addFamily(layout);
        Extend(families, n, layout.nCount, index, nullptr, plan.nDerived);
    }

    // the families to import: (family, compressed) -> highest place that is in use, -1 for none
    std::map<std::pair<size_t, bool>, int64_t> chosen;

    // 2. the address the user knows: its path, among all coin types
    if (pKnown) {
        bool fKnown = index.count(*pKnown) > 0;
        if (!fKnown) {
            const uint32_t H = 0x80000000;
            for (const bip39::PathLayout& layout : bip39::CandidateLayouts(true)) {
                if (fCancel || ShutdownRequested()) { error = "Cancelled"; return false; }
                CExtKey parent;
                if (!DeriveParent(master, layout.parent, parent))
                    continue;
                for (uint32_t i = 0; i < layout.nCount && !fKnown; i++) {
                    CExtKey child;
                    if (!parent.Derive(child, layout.fHardened ? (i | H) : i))
                        continue;
                    plan.nDerived++;
                    CPubKey pubkey = child.key.GetPubKey();
                    fKnown = pubkey.GetID() == *pKnown || (pubkey.Decompress() && pubkey.GetID() == *pKnown);
                }
                if (fKnown) {
                    Extend(families, addFamily(layout), layout.nCount, index, nullptr, plan.nDerived);
                    break;
                }
            }
        }
        if (fKnown) {
            const KeyRef& ref = index.at(*pKnown);
            int64_t& nMax = chosen.emplace(std::make_pair(ref.nFamily, ref.fCompressed), -1).first->second;
            nMax = std::max<int64_t>(nMax, ref.nIndex);
            std::vector<uint32_t> path = families[ref.nFamily].layout.parent;
            path.push_back(families[ref.nFamily].layout.fHardened ? (ref.nIndex | 0x80000000) : ref.nIndex);
            plan.knownPath = bip39::FormatPath(path);
        }
    }

    // 3. which of these addresses hold coins now
    std::map<CKeyID, Hit> hits;
    if (!ScanUnspent(index, nullptr, fCancel, hits)) {
        error = fCancel ? "Cancelled" : "The unspent outputs of the chain could not be read";
        return false;
    }
    auto noteHits = [&]() {
        for (const std::pair<const CKeyID, Hit>& hit : hits) {
            const KeyRef& ref = index.at(hit.first);
            int64_t& nMax = chosen.emplace(std::make_pair(ref.nFamily, ref.fCompressed), -1).first->second;
            nMax = std::max<int64_t>(nMax, ref.nIndex);
        }
    };
    noteHits();
    if (chosen.empty()) {
        error = pKnown ? "The address does not belong to these recovery words (or the passphrase is different)"
                       : "No coins were found on the usual key paths of these recovery words";
        return false;
    }

    // 4. the change chain belongs to the receiving chain and the other way round
    {
        std::vector<std::pair<size_t, bool>> keys;
        for (const auto& entry : chosen)
            keys.push_back(entry.first);
        for (const std::pair<size_t, bool>& key : keys) {
            bip39::PathLayout sibling;
            if (Sibling(families[key.first].layout, sibling))
                chosen.emplace(std::make_pair(addFamily(sibling), key.second), -1);
        }
    }

    // 5. follow the chosen paths further: wallets go on past the first 20 addresses
    for (uint32_t nLimit : {200u, MAX_INDEX}) {
        std::set<CKeyID> fresh;
        for (const auto& entry : chosen) {
            const Family& family = families[entry.first.first];
            if (nLimit > 200 && entry.second + GAP_LIMIT < family.keys.size())
                continue; // the end of this path was seen
            Extend(families, entry.first.first, nLimit, index, &fresh, plan.nDerived);
        }
        if (fresh.empty())
            continue;
        if (!ScanUnspent(index, &fresh, fCancel, hits)) {
            error = fCancel ? "Cancelled" : "The unspent outputs of the chain could not be read";
            return false;
        }
        noteHits();
    }

    // 6. the keys to import: each chosen path up to the gap limit past its last used place
    const uint32_t H = 0x80000000;
    for (const auto& entry : chosen) {
        const Family& family = families[entry.first.first];
        const bool fCompressed = entry.first.second;
        const uint32_t nEnd = std::min<int64_t>(family.keys.size(), entry.second + 1 + GAP_LIMIT);
        for (uint32_t i = 0; i < nEnd; i++) {
            if (!family.keys[i].IsValid())
                continue;
            RestoreKey out;
            std::vector<uint32_t> path = family.layout.parent;
            path.push_back(family.layout.fHardened ? (i | H) : i);
            out.path = bip39::FormatPath(path);
            out.key.Set(family.keys[i].begin(), family.keys[i].end(), fCompressed);
            const CKeyID keyid = out.key.GetPubKey().GetID();
            auto hit = hits.find(keyid);
            out.fFound = hit != hits.end();
            if (out.fFound)
                plan.found.push_back({out.path, keyid, hit->second.nAmount, hit->second.nOutputs, hit->second.fTokens});
            plan.keys.push_back(out);
        }
    }
    return true;
}

int ImportKeys(CWallet* pwallet, const Plan& plan, const std::string& label, std::string& error)
{
    LOCK2(cs_main, pwallet->cs_wallet);
    if (pwallet->IsLocked()) {
        error = "The wallet is locked";
        return -1;
    }
    if (fWalletUnlockStakingOnly) {
        error = "The wallet is unlocked for staking only";
        return -1;
    }
    int nAdded = 0;
    pwallet->MarkDirty();
    for (const RestoreKey& entry : plan.keys) {
        const CPubKey pubkey = entry.key.GetPubKey();
        if (!entry.key.VerifyPubKey(pubkey)) {
            error = "Key derivation failed";
            return -1;
        }
        const CKeyID keyid = pubkey.GetID();
        if (entry.fFound && !pwallet->mapAddressBook.count(keyid))
            pwallet->SetAddressBook(keyid, label, "receive");
        if (pwallet->HaveKey(keyid))
            continue;
        pwallet->mapKeyMetadata[keyid].nCreateTime = 1;
        if (!pwallet->AddKeyPubKey(entry.key, pubkey)) {
            error = "A key could not be added to the wallet";
            return -1;
        }
        nAdded++;
    }
    // the keys may have been used at any time: the whole chain has to be scanned
    pwallet->UpdateTimeFirstKey(1);
    return nAdded;
}

bool Rescan(CWallet* pwallet, const std::atomic<bool>& fCancel, const std::function<void(int)>& progress, std::string& error)
{
    static const int CHUNK = 500;
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        pindex = chainActive.Genesis();
    }
    if (PastBlocksMissing()) {
        error = PastBlocksMessage();
        return false;
    }
    progress(0);
    bool fUnread = false;
    while (pindex) {
        if (fCancel) { error = "Cancelled"; return false; }
        if (ShutdownRequested()) { error = "The node is shutting down"; return false; }
        CBlockIndex* pstop = nullptr;
        {
            LOCK(cs_main);
            if (!chainActive.Contains(pindex)) {
                // a reorganization: go on from the common ancestor
                const CBlockIndex* pfork = chainActive.FindFork(pindex);
                pindex = pfork ? chainActive.Next(pfork) : chainActive.Genesis();
                if (!pindex)
                    break;
            }
            pstop = chainActive[std::min(pindex->nHeight + CHUNK - 1, chainActive.Height())];
        }
        if (pwallet->ScanForWalletTransactions(pindex, pstop, true))
            fUnread = true;
        {
            LOCK(cs_main);
            const int nTip = chainActive.Height();
            progress(nTip > 0 ? std::min(99, (int)(100.0 * pstop->nHeight / nTip)) : 99);
            if (chainActive.Contains(pstop))
                pindex = chainActive.Next(pstop);
            else
                pindex = pstop; // handled at the top of the loop
        }
    }
    pwallet->ReacceptWalletTransactions();
    progress(100);
    if (fUnread) {
        error = "Some blocks could not be read. Transactions in those blocks are missing from the wallet";
        return false;
    }
    return true;
}

} // namespace mnemonicrestore
