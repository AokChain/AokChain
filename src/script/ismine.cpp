// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020 The AokChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util.h>
#include "ismine.h"

#include "key.h"
#include "keystore.h"
#include "script/script.h"
#include "script/standard.h"
#include "script/sign.h"
#include "validation.h"
#include "chain.h"


typedef std::vector<unsigned char> valtype;

unsigned int HaveKeys(const std::vector<valtype>& pubkeys, const CKeyStore& keystore)
{
    unsigned int nResult = 0;
    for (const valtype& pubkey : pubkeys)
    {
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (keystore.HaveKey(keyID))
            ++nResult;
    }
    return nResult;
}

isminetype IsMine(const CKeyStore& keystore, const CScript& scriptPubKey, CBlockIndex* bestBlock, SigVersion sigversion)
{
    bool isInvalid = false;
    return IsMine(keystore, scriptPubKey, bestBlock, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore& keystore, const CTxDestination& dest, CBlockIndex* bestBlock, SigVersion sigversion)
{
    bool isInvalid = false;
    return IsMine(keystore, dest, bestBlock, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore &keystore, const CTxDestination& dest, CBlockIndex* bestBlock, bool& isInvalid, SigVersion sigversion)
{
    CScript script = GetScriptForDestination(dest);
    return IsMine(keystore, script, bestBlock, isInvalid, sigversion);
}

isminetype IsMine(const CKeyStore &keystore, const CScript& scriptPubKey, CBlockIndex* bestBlock, bool& isInvalid, SigVersion sigversion)
{
    isInvalid = false;

    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions)) {
        if (keystore.HaveWatchOnly(scriptPubKey))
            return ISMINE_WATCH_UNSOLVABLE;
        return ISMINE_NO;
    }
    CKeyID keyID;
    switch (whichType) {
        case TX_NONSTANDARD:
        case TX_NULL_DATA:
            break;
        case TX_PUBKEY:
            keyID = CPubKey(vSolutions[0]).GetID();
            if (sigversion != SIGVERSION_BASE && vSolutions[0].size() != 33) {
                isInvalid = true;
                return ISMINE_NO;
            }
            if (keystore.HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;
        case TX_WITNESS_V0_KEYHASH: {
            if (!keystore.HaveCScript(CScriptID(CScript() << OP_0 << vSolutions[0]))) {
                // We do not support bare witness outputs unless the P2SH version of it would be
                // acceptable as well. This protects against matching before segwit activates.
                // This also applies to the P2WSH case.
                break;
            }
            isminetype ret = ::IsMine(keystore, GetScriptForDestination(CKeyID(uint160(vSolutions[0]))), bestBlock, isInvalid,
                                      SIGVERSION_WITNESS_V0);
            if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                return ret;
            break;
        }
        case TX_PUBKEYHASH:
            keyID = CKeyID(uint160(vSolutions[0]));
            if (sigversion != SIGVERSION_BASE) {
                CPubKey pubkey;
                if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
                    isInvalid = true;
                    return ISMINE_NO;
                }
            }
            if (keystore.HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;
        case TX_SCRIPTHASH: {
            CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
            CScript subscript;
            if (keystore.GetCScript(scriptID, subscript)) {
                isminetype ret = IsMine(keystore, subscript, bestBlock, isInvalid);
                if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                    return ret;
            }
            break;
        }
        case TX_WITNESS_V0_SCRIPTHASH: {
            if (!keystore.HaveCScript(CScriptID(CScript() << OP_0 << vSolutions[0]))) {
                break;
            }
            uint160 hash;
            CRIPEMD160().Write(&vSolutions[0][0], vSolutions[0].size()).Finalize(hash.begin());
            CScriptID scriptID = CScriptID(hash);
            CScript subscript;
            if (keystore.GetCScript(scriptID, subscript)) {
                isminetype ret = IsMine(keystore, subscript, bestBlock, isInvalid, SIGVERSION_WITNESS_V0);
                if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                    return ret;
            }
            break;
        }

        case TX_MULTISIG: {
            // Only consider transactions "mine" if we own ALL the
            // keys involved. Multi-signature transactions that are
            // partially owned (somebody else has a key that can spend
            // them) enable spend-out-from-under-you attacks, especially
            // in shared-wallet situations.
            std::vector<valtype> keys(vSolutions.begin() + 1, vSolutions.begin() + vSolutions.size() - 1);
            if (sigversion != SIGVERSION_BASE) {
                for (size_t i = 0; i < keys.size(); i++) {
                    if (keys[i].size() != 33) {
                        isInvalid = true;
                        return ISMINE_NO;
                    }
                }
            }
            if (HaveKeys(keys, keystore) == keys.size())
                return ISMINE_SPENDABLE;
            break;
        }
        case TX_CLTV:
        {
            keyID = CKeyID(uint160(vSolutions[1]));
            if (keystore.HaveKey(keyID))
            {
                CScriptNum nLockTime(vSolutions[0], true, 5);
                if (nLockTime < LOCKTIME_THRESHOLD) {
                    // locktime is a block
                    if (nLockTime > bestBlock->nHeight) {
                        return ISMINE_WATCH_SOLVABLE;
                    } else {
                        return ISMINE_SPENDABLE;
                    }
                } else {
                    // locktime is a time
                    if (nLockTime > bestBlock->GetMedianTimePast()) {
                        return ISMINE_WATCH_SOLVABLE;
                    } else {
                        return ISMINE_SPENDABLE;
                    }
                }

            } else {
                return ISMINE_NO;
            }
        }
        /** TOKENS START */
        case TX_NEW_TOKEN: {
            if (!AreTokensDeployed())
                return ISMINE_NO;
            keyID = CKeyID(uint160(vSolutions[0]));
            if (sigversion != SIGVERSION_BASE) {
                CPubKey pubkey;
                if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
                    isInvalid = true;
                    return ISMINE_NO;
                }
            }
            if (keystore.HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;

        }

        case TX_TRANSFER_TOKEN: {
            if (!AreTokensDeployed())
                return ISMINE_NO;
            keyID = CKeyID(uint160(vSolutions[0]));
            if (sigversion != SIGVERSION_BASE) {
                CPubKey pubkey;
                if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
                    isInvalid = true;
                    return ISMINE_NO;
                }
            }
            if (keystore.HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;
        }

        case TX_REISSUE_TOKEN: {
            if (!AreTokensDeployed())
                return ISMINE_NO;
            keyID = CKeyID(uint160(vSolutions[0]));
            if (sigversion != SIGVERSION_BASE) {
                CPubKey pubkey;
                if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
                    isInvalid = true;
                    return ISMINE_NO;
                }
            }
            if (keystore.HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;
        }
        /** TOKENS END*/
    }

    if (keystore.HaveWatchOnly(scriptPubKey)) {
        // TODO: This could be optimized some by doing some work after the above solver
        SignatureData sigs;
        return ProduceSignature(DummySignatureCreator(&keystore), scriptPubKey, sigs) ? ISMINE_WATCH_SOLVABLE : ISMINE_WATCH_UNSOLVABLE;
    }
    return ISMINE_NO;
}

bool IsTimeLock(const CKeyStore &keystore, const CScript& scriptPubKey, CScriptNum& nLockTime)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (Solver(scriptPubKey, whichType, vSolutions))
    {
        if (whichType == TX_SCRIPTHASH)
        {
            CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
            CScript subscript;
            if (keystore.GetCScript(scriptID, subscript))
                Solver(subscript, whichType, vSolutions);
        }

        if (whichType == TX_CLTV)
        {
            CScriptNum sn(vSolutions[0], true, 5);
            nLockTime = sn.getint64();
            return true;
        }
    }
    return false;
}

bool IsTimeLock(const CScript& scriptPubKey, CScriptNum& nLockTime)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (Solver(scriptPubKey, whichType, vSolutions))
    {
        if (whichType == TX_CLTV)
        {
            CScriptNum sn(vSolutions[0], true, 5);
            nLockTime = sn.getint64();
            return true;
        }
    }
    return false;
}
