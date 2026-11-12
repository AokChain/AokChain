// Copyright (c) 2015-2016 The BlackCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AOKCHAIN_POS_H
#define AOKCHAIN_POS_H

#include <chain.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <txdb.h>
#include <validation.h>
#include <arith_uint256.h>
#include <hash.h>
#include <timedata.h>
#include <chainparams.h>
#include <script/sign.h>
#include <consensus/consensus.h>

// To decrease granularity of timestamp
// Supposed to be 2^n-1
static const uint32_t STAKE_TIMESTAMP_MASK = 15;

struct CStakeCache{
    CStakeCache(uint32_t blockFromTime_, CAmount amount_) : blockFromTime(blockFromTime_), amount(amount_){
    }
    uint32_t blockFromTime;
    CAmount amount;
};

// Compute the hash modifier for proof-of-stake
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel);
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev, unsigned int nBits, CAmount nValueIn, const COutPoint& prevout, unsigned int nTimeTx, unsigned int nTimeTxPoS);
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);
bool CheckStakeBlockTimestamp(int64_t nTimeBlock, int nHeight, const Consensus::Params& params);
bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTimeBlock, const COutPoint& prevout, CCoinsViewCache& view);
bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTimeBlock, const COutPoint& prevout, CCoinsViewCache& view, const std::map<COutPoint, CStakeCache>& cache);
bool CheckProofOfStake(CBlockIndex* pindexPrev, CValidationState& state, const CTransaction& tx, unsigned int nBits, uint32_t nTimeBlock, uint256& hashProofOfStake, uint256& targetProofOfStake, CCoinsViewCache& view);

/**
 * What a coinstake may do with offline staking outputs. Such an output lets its staking key sign
 * in a coinstake (OP_OFFLINE_STAKE), so the rules about where the coins go are here and not in
 * the script: when a coinstake spends an offline staking output, every input must have that very
 * script, every output must pay it, and the outputs must not be worth less than the inputs.
 * Without this the holder of a staking key could take the coins that were delegated to it into a
 * stake of a coin of its own, or destroy them.
 */
bool CheckOfflineStakeScripts(const CTransaction& tx, const CCoinsViewCache& view, CValidationState& state);
#endif // AOKCHAIN_POS_H
